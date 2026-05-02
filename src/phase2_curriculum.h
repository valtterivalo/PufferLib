/* Phase 2 backward curriculum: env-reset hook + cursor management. */

#ifndef PHASE2_CURRICULUM_H
#define PHASE2_CURRICULUM_H

#include <stdint.h>
#include <stdlib.h>
#include "demostore.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int demo_id;
    int slot;
    int start_tick;
    float start_q;
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

    /* BC. When bc_coef > 0 and bc_demos_per_minibatch > 0, run_minibatch
       overwrites the last bc_demos_per_minibatch rows of the minibatch
       with demo windows and adds a head-weighted CE auxiliary loss. */
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

static inline float phase2_rand_unit(Phase2Context* ctx) {
    return (phase2_splitmix64(&ctx->rng) >> 40) * (1.0f / (float)(1u << 24));
}

static inline int phase2_rand_int(Phase2Context* ctx, int max_exclusive) {
    return (int)(phase2_splitmix64(&ctx->rng) % (uint64_t)max_exclusive);
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
    ctx->promote_rate = 0.60f;
    ctx->demote_attempts = 128;
    ctx->demote_rate = 0.20f;
    ctx->backstep_ticks = 16;
    ctx->success_q_delta = 0.03f;
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

static inline void phase2_record_outcome(
    Phase2Context* ctx, int demo_id, int won, float q_delta
) {
    if (demo_id < 0) return;
    ctx->demo_attempts[demo_id]++;
    if (won || q_delta > ctx->success_q_delta) ctx->demo_successes[demo_id]++;
}

static inline void phase2_apply_cursor_gate(Phase2Context* ctx) {
    for (int i = 0; i < ctx->store->num_demos; i++) {
        int attempts = ctx->demo_attempts[i];
        if (attempts < ctx->demote_attempts && attempts < ctx->promote_attempts) continue;
        float rate = (float)ctx->demo_successes[i] / (float)attempts;
        DemoTrajectory* d = &ctx->store->demos[i];
        if (attempts >= ctx->promote_attempts && rate >= ctx->promote_rate) {
            d->cursor_tick -= ctx->backstep_ticks;
            if (d->cursor_tick < 0) d->cursor_tick = 0;
            ctx->demo_attempts[i] = 0;
            ctx->demo_successes[i] = 0;
        } else if (attempts >= ctx->demote_attempts && rate < ctx->demote_rate) {
            d->cursor_tick += ctx->backstep_ticks / 2;
            if (d->cursor_tick >= d->length_ticks) d->cursor_tick = d->length_ticks - 1;
            ctx->demo_attempts[i] = 0;
            ctx->demo_successes[i] = 0;
        }
    }
}

static inline Phase2ResetDecision phase2_decide_reset(Phase2Context* ctx) {
    Phase2ResetDecision d = {.demo_id = -1, .slot = -1, .randomize_rng = 0, .fresh_rng_seed = 0};
    if (ctx->active_pool_size == 0 ||
        phase2_rand_unit(ctx) < ctx->normal_start_frac) return d;

    d.demo_id = ctx->active_pool[phase2_rand_int(ctx, ctx->active_pool_size)];
    DemoSnapshotLadder* ladder = ctx->ladders[d.demo_id];
    DemoTrajectory* demo = &ctx->store->demos[d.demo_id];

    int cursor_slot = demo_snapshot_ladder_slot_for_tick(ladder, demo->cursor_tick);
    int jitter = phase2_rand_int(ctx, 3) - 1;
    int slot = cursor_slot + jitter;
    if (slot < 0) slot = 0;
    if (slot >= ladder->num_snapshots) slot = ladder->num_snapshots - 1;
    d.slot = slot;

    if (phase2_rand_unit(ctx) < ctx->randomize_rng_frac) {
        d.randomize_rng = 1;
        d.fresh_rng_seed = (uint32_t)phase2_splitmix64(&ctx->rng);
    }
    return d;
}

#ifdef __cplusplus
}
#endif

#endif
