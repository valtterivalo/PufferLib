/**
 * @file test_inferno_golden.c
 * @brief Characterization (golden-master) test for the Inferno env.
 *
 * Drives deterministic episodes across a battery of (start_wave, seed) configs,
 * stepping with a fixed pseudo-random action stream, and folds the observation
 * vector + reward + outcome flags of every tick into an FNV-1a digest.
 *
 * The digest reads ONLY the public env interface (inf_write_obs, state->reward,
 * episode_over, winner, wave, tick). It is therefore independent of InfNPC /
 * InfernoState memory layout: a behavior-preserving refactor must reproduce the
 * baseline digests bit-for-bit, while any change the agent or reward could
 * observe (including obs drift) flips a digest.
 *
 * The law under test: refactor => identical trajectory.
 *
 * BASELINE is branch-local: re-seeded 2026-06-10 on valtteri/osrs-colosseum
 * after the shared consumable/spec dedup (digests verified bit-identical
 * across that refactor). The previous baseline predated this branch's shared
 * items/effects work and no longer corresponded to any blessed state.
 *
 * BUILD:
 *   cc -std=c11 -O2 -I. -o /tmp/test_inferno_golden \
 *       ocean/osrs/tests/test_inferno_golden.c -lm
 * RUN:
 *   /tmp/test_inferno_golden          # assert against baked-in baseline
 *   /tmp/test_inferno_golden --print  # print digests (to seed the baseline)
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "ocean/osrs/encounters/encounter_inferno.h"

/* ---- FNV-1a 64-bit rolling digest -------------------------------------- */

#define FNV_OFFSET 1469598103934665603ULL
#define FNV_PRIME  1099511628211ULL

static inline uint64_t fnv_bytes(uint64_t h, const void* p, size_t n) {
    const uint8_t* b = (const uint8_t*)p;
    for (size_t i = 0; i < n; i++) {
        h ^= b[i];
        h *= FNV_PRIME;
    }
    return h;
}

static inline uint64_t fnv_f32(uint64_t h, float v) {
    uint32_t bits;
    memcpy(&bits, &v, sizeof(bits));
    return fnv_bytes(h, &bits, sizeof(bits));
}

static inline uint64_t fnv_i32(uint64_t h, int v) {
    int32_t w = (int32_t)v;
    return fnv_bytes(h, &w, sizeof(w));
}

/* ---- deterministic action stream (splitmix64) -------------------------- */

static inline uint64_t splitmix64(uint64_t* s) {
    uint64_t z = (*s += 0x9E3779B97F4A7C15ULL);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
}

/* ---- one episode -> digest --------------------------------------------- */

static uint64_t run_episode(int start_wave, uint32_t seed, int max_ticks) {
    EncounterState* state = inf_create();
    inf_put_int(state, "start_wave", start_wave);
    inf_reset(state, seed);

    InfernoState* s = (InfernoState*)state;

    static float obs[INF_NUM_OBS];
    int actions[INF_NUM_ACTION_HEADS];

    /* action RNG seeded distinctly from the env RNG so the policy stream is
       independent of internal env randomness. */
    uint64_t arng = ((uint64_t)seed << 20) ^ (uint64_t)(start_wave + 1) ^ 0xD1B54A32D192ED03ULL;

    uint64_t h = FNV_OFFSET;

    for (int t = 0; t < max_ticks; t++) {
        for (int head = 0; head < INF_NUM_ACTION_HEADS; head++) {
            int dim = INF_ACTION_DIMS[head];
            actions[head] = (int)(splitmix64(&arng) % (uint64_t)dim);
        }

        inf_step(state, actions);
        inf_write_obs(state, obs);

        for (int i = 0; i < INF_NUM_OBS; i++) {
            h = fnv_f32(h, obs[i]);
        }
        h = fnv_f32(h, s->reward);
        h = fnv_i32(h, s->wave);
        h = fnv_i32(h, s->tick);
        h = fnv_i32(h, s->episode_over);
        h = fnv_i32(h, s->winner);

        if (s->episode_over) break;
    }

    inf_destroy(state);
    return h;
}

/* ---- config battery ----------------------------------------------------- */

typedef struct {
    const char* name;
    int start_wave;   /* internal 1-indexed start wave (see inf_reset bounds) */
    uint32_t seed;
} GoldenConfig;

/* covers every NPC-behavior family so the type-specific InfNPC fields
   (blob scan, meleer dig, mager resurrect, jad, zuk shield/healer/spark)
   are all exercised under the digest. */
static const GoldenConfig CONFIGS[] = {
    { "wave1_a",     1, 0x0000001u },
    { "wave1_b",     1, 0x0BADF00Du },
    { "wave1_c",     1, 0x1234567u },
    { "meleer_a",    9, 0x0000001u },
    { "meleer_b",    9, 0x0BADF00Du },
    { "ranger_a",   18, 0x0000001u },
    { "ranger_b",   18, 0x0BADF00Du },
    { "mager_a",    35, 0x0000001u },
    { "mager_b",    35, 0x0BADF00Du },
    { "jad_a",      67, 0x0000001u },
    { "jad_b",      67, 0x0BADF00Du },
    { "jad_c",      67, 0x1234567u },
    { "zuk_a",      69, 0x0000001u },
    { "zuk_b",      69, 0x0BADF00Du },
    { "zuk_c",      69, 0x1234567u },
};

#define NUM_CONFIGS ((int)(sizeof(CONFIGS) / sizeof(CONFIGS[0])))
#define EPISODE_TICKS 2000

/* baseline digests captured on the pre-refactor commit. regenerate with
   --print only when an intentional behavior change is made, and explain why.
   2026-06-14: the 6 jad and zuk digests were re-seeded for the no-active-pillar
   forecast gate. On pillar-less waves (Jad, Zuk) the step-out forecast skips its
   rollout and emits zero danger features (the forecast is useless there: no
   safespotting, no attack overlap). Obs-only change on those waves; sim and
   reward stay byte-identical (verified: state-hash + reward unchanged on every
   record, obs differs only by danger features going to zero with valid
   preserved). The 9 pillar-ful configs are unchanged. */
static const uint64_t BASELINE[NUM_CONFIGS] = {
    0x8b5b754c26c82822ULL,  /* wave1_a */
    0xcee535f8947e69c7ULL,  /* wave1_b */
    0x2f037285fba496e0ULL,  /* wave1_c */
    0x470e44514c01ef1cULL,  /* meleer_a */
    0x89030381ef0d8a45ULL,  /* meleer_b */
    0x75ad082f11f8febaULL,  /* ranger_a */
    0x70999457798cf9f2ULL,  /* ranger_b */
    0x362a09d876d6070dULL,  /* mager_a */
    0x437655bfabd32761ULL,  /* mager_b */
    0xb6206c4a2192fd4eULL,  /* jad_a */
    0x42b9b1d5bc586ab5ULL,  /* jad_b */
    0xeeb78eb3f418246fULL,  /* jad_c */
    0xe34ad53c7c7d1d8aULL,  /* zuk_a */
    0x28aeb435e56e6527ULL,  /* zuk_b */
    0x7d34388640e2036fULL,  /* zuk_c */
};

int main(int argc, char** argv) {
    int print_mode = (argc > 1 && strcmp(argv[1], "--print") == 0);

    inf_build_npc_stats();

    printf("inferno golden-master (%d configs, <=%d ticks each)\n\n",
           NUM_CONFIGS, EPISODE_TICKS);

    int failed = 0;
    for (int c = 0; c < NUM_CONFIGS; c++) {
        uint64_t h = run_episode(CONFIGS[c].start_wave, CONFIGS[c].seed, EPISODE_TICKS);
        if (print_mode) {
            printf("    0x%016llxULL,  /* %s */\n", (unsigned long long)h, CONFIGS[c].name);
        } else {
            int ok = (h == BASELINE[c]);
            printf("  %-12s 0x%016llx  %s\n", CONFIGS[c].name,
                   (unsigned long long)h, ok ? "PASS" : "FAIL");
            if (!ok) {
                printf("               expected 0x%016llx\n",
                       (unsigned long long)BASELINE[c]);
                failed++;
            }
        }
    }

    if (print_mode) {
        printf("\npaste the array above into BASELINE[].\n");
        return 0;
    }

    printf("\n%d/%d configs match baseline\n", NUM_CONFIGS - failed, NUM_CONFIGS);
    return failed > 0 ? 1 : 0;
}
