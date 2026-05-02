/* Phase 2 backward curriculum: per-env reset hook decides between a normal
   c_reset and restoring from a demo's snapshot ladder. Cursor advancement
   per demo lives elsewhere (caller-driven outcome reporting). */

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
    float randomize_future_rng_frac;

    int active_pool_size;
    int* active_pool;
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
    ctx->rng = seed ? seed : 1ULL;
    ctx->normal_start_frac = 0.25f;
    ctx->randomize_future_rng_frac = 0.25f;
    ctx->active_pool_size = store->num_demos;
    ctx->active_pool = (int*)calloc((size_t)store->num_demos, sizeof(int));
    for (int i = 0; i < store->num_demos; i++) ctx->active_pool[i] = i;
    return ctx;
}

static inline void phase2_ctx_destroy(Phase2Context* ctx) {
    if (!ctx) return;
    free(ctx->env_states);
    free(ctx->active_pool);
    free(ctx);
}

/* Pick demo+slot for a fresh env, or return demo_id=-1 for a normal c_reset.
   Slot is sampled at the demo's cursor +/- 1 stride (clamped). */
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

    if (phase2_rand_unit(ctx) < ctx->randomize_future_rng_frac) {
        d.randomize_rng = 1;
        d.fresh_rng_seed = (uint32_t)phase2_splitmix64(&ctx->rng);
    }
    return d;
}

#ifdef __cplusplus
}
#endif

#endif
