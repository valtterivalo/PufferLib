/* Hashes SIMULATION state only, never observation floats.
 *
 * test_colosseum_golden folds COLO_NUM_OBS obs floats and the sim scalars into one
 * hash, so re-seeding it after an observation change also re-seeds the simulation
 * half and a sim regression landed in the same commit is invisible. This probe is
 * the half that must not move when the observation is recut.
 *
 * Covers the whole ColosseumState with the obs memo caches zeroed (they are derived
 * and their size tracks the obs), plus the full action mask. */

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "ocean/osrs/encounters/encounter_colosseum.h"

#define FNV_OFFSET 1469598103934665603ULL
#define FNV_PRIME 1099511628211ULL

static inline uint64_t fnv_bytes(uint64_t h, const void* p, size_t n) {
    const uint8_t* b = (const uint8_t*)p;
    for (size_t i = 0; i < n; i++) { h ^= b[i]; h *= FNV_PRIME; }
    return h;
}

static inline uint64_t fnv_f32(uint64_t h, float v) {
    uint32_t bits; memcpy(&bits, &v, sizeof(bits));
    return fnv_bytes(h, &bits, sizeof(bits));
}

static inline uint64_t splitmix64(uint64_t* s) {
    uint64_t z = (*s += 0x9E3779B97F4A7C15ULL);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
}

typedef struct {
    const char* name;
    int public_start_wave;
    uint32_t env_seed;
    uint64_t action_seed;
    uint64_t baseline;
} SimConfig;

/* Baselines are generated on the tree that contains the pathfinding and reservoir
 * determinism fixes. Regenerate with --print only when a SIMULATION change is
 * intended, never to make an observation edit pass. */
static SimConfig CONFIGS[] = {
    {"w01",  1, 1001ULL, 0xC0FFEE01ULL, 0x6275317f96d2047dULL},
    {"w02",  2, 1002ULL, 0xC0FFEE02ULL, 0x45e93262b17998d0ULL},
    {"w03",  3, 1003ULL, 0xC0FFEE03ULL, 0x8519d08310e0c1deULL},
    {"w04",  4, 1004ULL, 0xC0FFEE04ULL, 0xd13d1e9571ec270dULL},
    {"w05",  5, 1005ULL, 0xC0FFEE05ULL, 0x3a1ccf6e45ce4401ULL},
    {"w06",  6, 1006ULL, 0xC0FFEE06ULL, 0xd24e0b46eebcb45cULL},
    {"w07",  7, 1007ULL, 0xC0FFEE07ULL, 0x3de9958e7bc72b44ULL},
    {"w08",  8, 1008ULL, 0xC0FFEE08ULL, 0xa3ed6cfd43287c30ULL},
    {"w09",  9, 1009ULL, 0xC0FFEE09ULL, 0xae207c5c3d9911acULL},
    {"w10", 10, 1010ULL, 0xC0FFEE10ULL, 0xf829b5ecb41d7933ULL},
    {"w11", 11, 1011ULL, 0xC0FFEE11ULL, 0x4eb1681f6cbd04b5ULL},
    {"w12", 12, 1012ULL, 0xC0FFEE12ULL, 0xe92dc857d82bcf0aULL},
};

static void fill_actions(
    const ColosseumState* s, uint64_t* rng, int actions[COLO_NUM_ACTION_HEADS]
) {
    for (int head = 0; head < COLO_NUM_ACTION_HEADS; head++)
        actions[head] = (int)(splitmix64(rng) % (uint64_t)COLO_ACTION_DIMS[head]);
    if (s->modifiers.draft_pending) {
        actions[COLO_HEAD_PRIMARY] = 0;
        actions[COLO_HEAD_MODIFIER_SELECT] =
            1 + (int)(splitmix64(rng) % COLO_MODIFIER_DRAFT_OPTIONS);
    }
}

static uint64_t hash_sim(uint64_t h, const ColosseumState* s, const float* mask) {
    /* Copy so the memo caches can be zeroed without disturbing the live run. They
     * are derived from state and their width tracks the observation, so hashing
     * them would couple this probe to exactly what it exists to be independent of. */
    ColosseumState* c = (ColosseumState*)malloc(sizeof(*c));
    memcpy(c, s, sizeof(*c));
    memset(&c->obs_memos, 0, sizeof(c->obs_memos));
    h = fnv_bytes(h, c, sizeof(*c));
    free(c);
    for (int i = 0; i < COLO_ACTION_MASK_SIZE; i++) h = fnv_f32(h, mask[i]);
    return h;
}

static uint64_t run_episode(const SimConfig* cfg, int max_ticks) {
    ColosseumContext ctx;
    ColosseumState s;
    static float mask[COLO_ACTION_MASK_SIZE];
    int actions[COLO_NUM_ACTION_HEADS];

    col_init_context_typed(&ctx);
    ctx.config.start_wave = cfg->public_start_wave - 1;

    memset(&s, 0, sizeof(s));
    col_reset_ctx((EncounterState*)&s, (EncounterContext*)&ctx, cfg->env_seed);

    uint64_t rng = cfg->action_seed;
    uint64_t h = FNV_OFFSET;

    for (int t = 0; t < max_ticks; t++) {
        col_write_mask_ctx((EncounterState*)&s, (EncounterContext*)&ctx, mask);
        fill_actions(&s, &rng, actions);
        col_step_ctx((EncounterState*)&s, (EncounterContext*)&ctx, actions);
        h = hash_sim(h, &s, mask);
        if (s.episode_over) break;
    }
    return h;
}

int main(int argc, char** argv) {
    int print = (argc > 1 && strcmp(argv[1], "--print") == 0);
    int n = (int)(sizeof(CONFIGS) / sizeof(CONFIGS[0]));
    int failed = 0;

    for (int i = 0; i < n; i++) {
        uint64_t h = run_episode(&CONFIGS[i], 4000);
        if (print) {
            printf("    {\"%s\", %2d, %luULL, 0x%08lXULL, 0x%016llxULL},\n",
                CONFIGS[i].name, CONFIGS[i].public_start_wave,
                (unsigned long)CONFIGS[i].env_seed,
                (unsigned long)CONFIGS[i].action_seed,
                (unsigned long long)h);
            continue;
        }
        int ok = (h == CONFIGS[i].baseline);
        printf("  %-6s 0x%016llx  %s\n", CONFIGS[i].name,
            (unsigned long long)h, ok ? "PASS" : "FAIL");
        if (!ok) {
            printf("         expected 0x%016llx\n",
                (unsigned long long)CONFIGS[i].baseline);
            failed++;
        }
    }
    if (print) return 0;
    printf("\n%d/%d sim invariants match baseline\n", n - failed, n);
    return failed ? 1 : 0;
}
