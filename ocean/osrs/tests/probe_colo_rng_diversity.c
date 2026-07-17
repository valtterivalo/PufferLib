/**
 * @file probe_colo_rng_diversity.c
 * @brief Verifies colosseum RNG is per-env AND per-episode diverse, the fix for the
 * all-envs-share-seed-12345 bug. Two invariants:
 *   (1) PER-ENV: seeding rng_state = lowbias32(env_index) (what my_init does) then
 *       resetting via the saved-rng path (c_reset's explicit_seed=0) yields a DISTINCT
 *       spawn for each env index -> envs do not all run the same scenario.
 *   (2) PER-EPISODE: within one env, stepping (which advances the rng chain) then
 *       resetting again (explicit_seed=0, preserving saved_rng) yields a DIFFERENT
 *       spawn than the first episode -> episodes are not identical.
 *
 * The lowbias32 here MUST match col_lowbias32 in ocean/osrs_colosseum/osrs_colosseum.h.
 *
 * BUILD: cc -std=c11 -O2 -I. -o /tmp/probe_rng ocean/osrs/tests/probe_colo_rng_diversity.c -lm
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "ocean/osrs/encounters/encounter_colosseum.h"

#define FNV_OFFSET 1469598103934665603ULL
#define FNV_PRIME 1099511628211ULL

static uint32_t lowbias32_ref(uint32_t x) {
    x ^= x >> 16;
    x *= 0x7feb352dU;
    x ^= x >> 15;
    x *= 0x846ca68bU;
    x ^= x >> 16;
    return x;
}

static uint64_t splitmix64(uint64_t* s) {
    uint64_t z = (*s += 0x9E3779B97F4A7C15ULL);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
}

/** digest the spawn layout: NPC type/position over all slots, plus wave + rng. */
static uint64_t spawn_digest(const ColosseumState* s) {
    uint64_t h = FNV_OFFSET;
    for (int i = 0; i < COLO_MAX_NPCS; i++) {
        int32_t v[3] = { (int32_t)s->npcs[i].type, s->npcs[i].x, s->npcs[i].y };
        for (int k = 0; k < 3; k++) { h ^= (uint64_t)(uint32_t)v[k]; h *= FNV_PRIME; }
    }
    h ^= (uint64_t)(uint32_t)s->wave; h *= FNV_PRIME;
    h ^= (uint64_t)s->rng_state; h *= FNV_PRIME;
    return h;
}

static void fill_actions(const ColosseumState* s, uint64_t* rng, int acts[COLO_NUM_ACTION_HEADS]) {
    for (int head = 0; head < COLO_NUM_ACTION_HEADS; head++)
        acts[head] = (int)(splitmix64(rng) % (uint64_t)COLO_ACTION_DIMS[head]);
    if (s->modifiers.draft_pending) {
        acts[COLO_HEAD_PRIMARY] = 0;
        acts[COLO_HEAD_MODIFIER_SELECT] = 1 + (int)(splitmix64(rng) % COLO_MODIFIER_DRAFT_OPTIONS);
    }
}

/** reset env i exactly as the fix does: my_init sets rng_state = lowbias32(index),
    then c_reset calls reset with explicit_seed=0 (saved-rng path). */
static uint64_t reset_env_seed(ColosseumState* s, ColosseumContext* ctx, uint32_t index) {
    memset(s, 0, sizeof(*s));
    s->rng_state = lowbias32_ref(index);
    col_reset_ctx((EncounterState*)s, (EncounterContext*)ctx, 0);
    return spawn_digest(s);
}

int main(void) {
    ColosseumContext ctx;
    col_init_context_typed(&ctx);
    ctx.config.start_wave = 0;

    /* (1) PER-ENV: 8 indices must give 8 distinct spawns. */
    enum { N = 8 };
    uint64_t dig[N];
    ColosseumState s;
    for (uint32_t i = 0; i < N; i++) dig[i] = reset_env_seed(&s, &ctx, i);
    int distinct = 1;
    for (int i = 0; i < N; i++)
        for (int j = i + 1; j < N; j++)
            if (dig[i] == dig[j]) distinct = 0;
    printf("per-env: %d indices, %s\n", N, distinct ? "all distinct" : "COLLISION");

    /* (2) PER-EPISODE: episode 1, step to advance the rng chain, reset again -> differs. */
    uint64_t ep1 = reset_env_seed(&s, &ctx, 0);
    uint64_t arng = 0xABCDEF01u;
    int acts[COLO_NUM_ACTION_HEADS];
    for (int t = 0; t < 120 && !s.episode_over; t++) {
        fill_actions(&s, &arng, acts);
        col_step_ctx((EncounterState*)&s, (EncounterContext*)&ctx, acts);
    }
    col_reset_ctx((EncounterState*)&s, (EncounterContext*)&ctx, 0);
    uint64_t ep2 = spawn_digest(&s);
    int episode_varies = (ep1 != ep2);
    printf("per-episode: ep1=%016llx ep2=%016llx -> %s\n",
        (unsigned long long)ep1, (unsigned long long)ep2,
        episode_varies ? "varies" : "IDENTICAL");

    if (distinct && episode_varies) { printf("PASS\n"); return 0; }
    printf("FAIL\n");
    return 1;
}
