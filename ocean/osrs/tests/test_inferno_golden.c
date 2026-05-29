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
   --print only when an intentional behavior change is made, and explain why. */
static const uint64_t BASELINE[NUM_CONFIGS] = {
    0xf8e22fba0907f6cbULL,  /* wave1_a */
    0xaf760c0ade931e35ULL,  /* wave1_b */
    0xc02614998d57e614ULL,  /* wave1_c */
    0x269c9050934cc788ULL,  /* meleer_a */
    0x4bfb2de091d58916ULL,  /* meleer_b */
    0xd416490824ee42e9ULL,  /* ranger_a */
    0xce59a3778406a0eeULL,  /* ranger_b */
    0xa4b5214ecaadd78fULL,  /* mager_a */
    0xfdf2d56e3814b949ULL,  /* mager_b */
    0x6e163506a4a3aabcULL,  /* jad_a */
    0xa0fa046c872a6580ULL,  /* jad_b */
    0x99c910e532cb08f4ULL,  /* jad_c */
    0x3f46b41f4cdd8908ULL,  /* zuk_a */
    0xcc9c5ce8b1094880ULL,  /* zuk_b */
    0x50e4007acb51903cULL,  /* zuk_c */
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
