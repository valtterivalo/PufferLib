/* Phase 2 backward curriculum: env-reset hook + cursor management. */

#ifndef PHASE2_CURRICULUM_H
#define PHASE2_CURRICULUM_H

#include <stdint.h>
#include <stdlib.h>
#include "demostore.h"

#ifdef __cplusplus
extern "C" {
#endif

/* aligned to 32 bytes so adjacent env states don't share a cache line under
   the OMP parallel-for in c_step — multiple worker threads write rng_state
   concurrently. */
typedef struct __attribute__((aligned(32))) {
    int demo_id;
    int slot;
    int start_tick;
    float start_q;
    int start_active_set_count;
    uint64_t rng_state;
    int needs_hidden_restore;
    int first_action_pending;
} Phase2EnvState;

typedef struct {
    DemoStore* store;
    DemoSnapshotLadder** ladders;
    int num_envs;
    Phase2EnvState* env_states;

    uint64_t rng;
    float normal_start_frac;
    float randomize_rng_frac;

    int active_pool_size;
    int* active_pool;

    int* demo_attempts;
    int* demo_successes;
    int promote_attempts;
    float promote_rate;
    int demote_attempts;
    float demote_rate;
    int backstep_ticks;
    float success_q_delta;
    float bc_coef;
    int bc_demos_per_minibatch;
} Phase2Context;

typedef struct {
    int demo_id;
    int slot;
    int randomize_rng;
    uint32_t fresh_rng_seed;
} Phase2ResetDecision;

static inline uint64_t phase2_splitmix64(uint64_t* s) {
    uint64_t z = (*s += 0x9e3779b97f4a7c15ULL);
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
    return z ^ (z >> 31);
}

static inline float phase2_rand_unit_state(uint64_t* state) {
    return (phase2_splitmix64(state) >> 40) * (1.0f / (float)(1u << 24));
}

static inline int phase2_rand_int_state(uint64_t* state, int max_exclusive) {
    return (int)(phase2_splitmix64(state) % (uint64_t)max_exclusive);
}

static inline float phase2_rand_unit(Phase2Context* ctx) {
    return phase2_rand_unit_state(&ctx->rng);
}

static inline int phase2_rand_int(Phase2Context* ctx, int max_exclusive) {
    return phase2_rand_int_state(&ctx->rng, max_exclusive);
}

static inline Phase2Context* phase2_ctx_create(
    DemoStore* store, DemoSnapshotLadder** ladders, int num_envs, uint64_t seed
) {
    Phase2Context* ctx = (Phase2Context*)calloc(1, sizeof(*ctx));
    ctx->store = store;
    ctx->ladders = ladders;
    ctx->num_envs = num_envs;
    ctx->env_states = (Phase2EnvState*)calloc((size_t)num_envs, sizeof(Phase2EnvState));
    for (int i = 0; i < num_envs; i++) {
        ctx->env_states[i].demo_id = -1;
        ctx->env_states[i].slot = -1;
        uint64_t s = seed ^ (((uint64_t)i + 1) * 0x9e3779b97f4a7c15ULL);
        ctx->env_states[i].rng_state = phase2_splitmix64(&s);
    }
    ctx->rng = seed;
    ctx->normal_start_frac = 0.25f;
    ctx->randomize_rng_frac = 0.25f;
    ctx->active_pool_size = store->num_demos;
    ctx->active_pool = (int*)calloc((size_t)store->num_demos, sizeof(int));
    for (int i = 0; i < store->num_demos; i++) ctx->active_pool[i] = i;
    ctx->demo_attempts = (int*)calloc((size_t)store->num_demos, sizeof(int));
    ctx->demo_successes = (int*)calloc((size_t)store->num_demos, sizeof(int));
    ctx->promote_attempts = 64;
    ctx->promote_rate = 0.30f;
    ctx->demote_attempts = 128;
    ctx->demote_rate = 0.10f;
    ctx->backstep_ticks = 4;
    ctx->success_q_delta = 0.005f;
    ctx->bc_coef = 0.0f;
    ctx->bc_demos_per_minibatch = 0;
    return ctx;
}

static inline void phase2_ctx_destroy(Phase2Context* ctx) {
    free(ctx->env_states);
    free(ctx->active_pool);
    free(ctx->demo_attempts);
    free(ctx->demo_successes);
    free(ctx);
}

static inline void phase2_record_success(
    Phase2Context* ctx, int demo_id, int success
) {
    if (demo_id < 0) return;
    __atomic_fetch_add(&ctx->demo_attempts[demo_id], 1, __ATOMIC_RELAXED);
    if (success) {
        __atomic_fetch_add(&ctx->demo_successes[demo_id], 1, __ATOMIC_RELAXED);
    }
}

static inline void phase2_record_outcome(
    Phase2Context* ctx, int demo_id, int won, float q_delta
) {
    phase2_record_success(
        ctx,
        demo_id,
        won || q_delta > ctx->success_q_delta);
}

typedef struct {
    float mean_frac;
    float min_frac;
    float max_frac;
    int num_at_start;
} Phase2CursorStats;

static inline Phase2CursorStats phase2_cursor_stats(Phase2Context* ctx) {
    Phase2CursorStats s = {.mean_frac = 0.0f, .min_frac = 1.0f, .max_frac = 0.0f, .num_at_start = 0};
    int n = ctx->store->num_demos;
    for (int i = 0; i < n; i++) {
        DemoTrajectory* d = &ctx->store->demos[i];
        float frac = (float)d->cursor_tick / (float)d->length_ticks;
        s.mean_frac += frac;
        if (frac < s.min_frac) s.min_frac = frac;
        if (frac > s.max_frac) s.max_frac = frac;
        if (d->cursor_tick == 0) s.num_at_start++;
    }
    s.mean_frac /= (float)n;
    return s;
}

static inline void phase2_apply_cursor_gate(Phase2Context* ctx) {
    for (int i = 0; i < ctx->store->num_demos; i++) {
        int attempts = __atomic_load_n(&ctx->demo_attempts[i], __ATOMIC_RELAXED);
        if (attempts < ctx->demote_attempts && attempts < ctx->promote_attempts) continue;
        int successes = __atomic_load_n(&ctx->demo_successes[i], __ATOMIC_RELAXED);
        float rate = (float)successes / (float)attempts;
        DemoTrajectory* d = &ctx->store->demos[i];
        if (attempts >= ctx->promote_attempts && rate >= ctx->promote_rate) {
            d->cursor_tick -= ctx->backstep_ticks;
            if (d->cursor_tick < 0) d->cursor_tick = 0;
            __atomic_store_n(&ctx->demo_attempts[i], 0, __ATOMIC_RELAXED);
            __atomic_store_n(&ctx->demo_successes[i], 0, __ATOMIC_RELAXED);
        } else if (attempts >= ctx->demote_attempts && rate < ctx->demote_rate) {
            d->cursor_tick += ctx->backstep_ticks / 2;
            if (d->cursor_tick >= d->length_ticks) d->cursor_tick = d->length_ticks - 1;
            __atomic_store_n(&ctx->demo_attempts[i], 0, __ATOMIC_RELAXED);
            __atomic_store_n(&ctx->demo_successes[i], 0, __ATOMIC_RELAXED);
        }
    }
}

static inline Phase2ResetDecision phase2_decide_reset(Phase2Context* ctx, uint64_t* rng_state) {
    Phase2ResetDecision d = {.demo_id = -1, .slot = -1, .randomize_rng = 0, .fresh_rng_seed = 0};
    if (ctx->active_pool_size == 0 ||
        phase2_rand_unit_state(rng_state) < ctx->normal_start_frac) return d;

    d.demo_id = ctx->active_pool[phase2_rand_int_state(rng_state, ctx->active_pool_size)];
    DemoSnapshotLadder* ladder = ctx->ladders[d.demo_id];
    DemoTrajectory* demo = &ctx->store->demos[d.demo_id];

    int cursor_slot = demo_snapshot_ladder_slot_for_tick(ladder, demo->cursor_tick);
    int jitter = phase2_rand_int_state(rng_state, 3) - 1;
    int slot = cursor_slot + jitter;
    if (slot < 0) slot = 0;
    if (slot >= ladder->num_snapshots) slot = ladder->num_snapshots - 1;
    d.slot = slot;

    if (phase2_rand_unit_state(rng_state) < ctx->randomize_rng_frac) {
        d.randomize_rng = 1;
        d.fresh_rng_seed = (uint32_t)phase2_splitmix64(rng_state);
    }
    return d;
}

#ifdef __cplusplus
}
#endif

#endif
