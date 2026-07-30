/**
 * Steps a fixed set of colosseum envs through a fixed action sequence, once with
 * one OpenMP worker and once with many, then compares every observation float.
 *
 * This isolates the environment from the trainer. Training diverges run to run
 * only when vec.num_threads > 1, and the trainer's env loop is
 * `#pragma omp parallel for schedule(static)` over independent Env structs, so
 * either the env carries thread-shared state or the cause is outside the env.
 * Nothing here is reimplemented: it drives col_step_ctx and col_write_obs_ctx.
 */
#define _POSIX_C_SOURCE 200809L
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <omp.h>

#include "ocean/osrs/encounters/encounter_colosseum.h"

#define NUM_ENVS 256
#define NUM_TICKS 300

typedef struct {
    ColosseumState state;
    ColosseumContext ctx;
} ProbeEnv;

static uint64_t sm(uint64_t* s) {
    uint64_t z = (*s += 0x9E3779B97F4A7C15ull);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}

static ProbeEnv* g_envs;
static float* g_obs;

static void run_pass(int workers, int late_start_mode, const int* actions) {
    for (int i = 0; i < NUM_ENVS; i++) {
        col_init_context_typed(&g_envs[i].ctx);
        g_envs[i].ctx.config.start_wave = i % 12;
        g_envs[i].ctx.config.late_start_state_mode = late_start_mode;
        memset(&g_envs[i].state, 0, sizeof(ColosseumState));
        col_reset_ctx((EncounterState*)&g_envs[i].state,
            (EncounterContext*)&g_envs[i].ctx, 900u + (unsigned)i * 7u);
    }
    for (int t = 0; t < NUM_TICKS; t++) {
        #pragma omp parallel for schedule(static) num_threads(workers)
        for (int i = 0; i < NUM_ENVS; i++) {
            const int* act = &actions[(size_t)(t * NUM_ENVS + i) * COLO_NUM_ACTION_HEADS];
            col_step_ctx((EncounterState*)&g_envs[i].state,
                (EncounterContext*)&g_envs[i].ctx, act);
        }
    }
    #pragma omp parallel for schedule(static) num_threads(workers)
    for (int i = 0; i < NUM_ENVS; i++) {
        col_write_obs_ctx((EncounterState*)&g_envs[i].state,
            (EncounterContext*)&g_envs[i].ctx, &g_obs[(size_t)i * COLO_NUM_OBS]);
    }
}

static int compare(const float* a, const float* b, const char* label) {
    long diffs = 0;
    int first_env = -1, first_idx = -1;
    for (int i = 0; i < NUM_ENVS; i++) {
        for (int k = 0; k < COLO_NUM_OBS; k++) {
            size_t o = (size_t)i * COLO_NUM_OBS + k;
            if (a[o] == b[o]) continue;
            if (first_env < 0) { first_env = i; first_idx = k; }
            diffs++;
        }
    }
    if (diffs == 0) {
        printf("  %-28s IDENTICAL across %d envs x %d obs floats\n",
            label, NUM_ENVS, COLO_NUM_OBS);
        return 0;
    }
    printf("  %-28s DIFFERS: %ld of %lld floats, first at env %d index %d (%.9g vs %.9g)\n",
        label, diffs, (long long)NUM_ENVS * COLO_NUM_OBS, first_env, first_idx,
        a[(size_t)first_env * COLO_NUM_OBS + first_idx],
        b[(size_t)first_env * COLO_NUM_OBS + first_idx]);
    return 1;
}

int main(int argc, char** argv) {
    int workers = argc > 1 ? atoi(argv[1]) : 32;

    size_t nact = (size_t)NUM_TICKS * NUM_ENVS * COLO_NUM_ACTION_HEADS;
    int* actions = (int*)malloc(nact * sizeof(int));
    uint64_t rng = 20260730u;
    for (size_t i = 0; i < nact; i++)
        actions[i] = (int)(sm(&rng) % (uint64_t)COLO_ACTION_DIMS[i % COLO_NUM_ACTION_HEADS]);

    g_envs = (ProbeEnv*)malloc(sizeof(ProbeEnv) * NUM_ENVS);
    g_obs = (float*)malloc(sizeof(float) * NUM_ENVS * COLO_NUM_OBS);
    float* ref = (float*)malloc(sizeof(float) * NUM_ENVS * COLO_NUM_OBS);
    size_t obs_bytes = sizeof(float) * NUM_ENVS * COLO_NUM_OBS;

    int failures = 0;
    for (int mode = 0; mode <= 2; mode += 2) {
        printf("late_start_state_mode = %d\n", mode);

        run_pass(1, mode, actions);
        memcpy(ref, g_obs, obs_bytes);
        run_pass(1, mode, actions);
        failures += compare(ref, g_obs, "1 worker vs 1 worker");

        run_pass(1, mode, actions);
        memcpy(ref, g_obs, obs_bytes);
        run_pass(workers, mode, actions);
        char label[64];
        snprintf(label, sizeof(label), "1 worker vs %d workers", workers);
        failures += compare(ref, g_obs, label);

        run_pass(workers, mode, actions);
        memcpy(ref, g_obs, obs_bytes);
        run_pass(workers, mode, actions);
        snprintf(label, sizeof(label), "%d workers, twice", workers);
        failures += compare(ref, g_obs, label);
        printf("\n");
    }

    printf("%s\n", failures ? "ENV IS THREAD-SENSITIVE" : "env is thread-invariant");
    return failures ? 1 : 0;
}
