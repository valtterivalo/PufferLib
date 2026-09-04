#define _POSIX_C_SOURCE 200809L
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#if defined(BENCH_INFERNO)
#include "ocean/osrs/encounters/encounter_inferno.h"
#elif defined(BENCH_COLOSSEUM)
#include "ocean/osrs/encounters/encounter_colosseum.h"
#else
#error "compile with -DBENCH_INFERNO or -DBENCH_COLOSSEUM"
#endif

static uint64_t splitmix64(uint64_t* s) {
    uint64_t z = (*s += 0x9E3779B97F4A7C15ULL);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
}

static double now_s(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

#ifdef BENCH_INFERNO
static uint64_t run_inferno(uint64_t* sink) {
    static const int waves[] = {1, 18, 35, 67, 69};
    static const uint32_t seeds[] = {1u, 0x0BADF00Du, 0x1234567u};
    enum { TICKS = 2000 };
    uint64_t steps = 0;
    InfernoContext context;
    InfernoState state;
    static float obs[INF_NUM_OBS];
    static float mask[INF_ACTION_MASK_SIZE];
    int actions[INF_NUM_ACTION_HEADS];

    for (int w = 0; w < (int)(sizeof(waves) / sizeof(waves[0])); w++) {
        for (int s = 0; s < (int)(sizeof(seeds) / sizeof(seeds[0])); s++) {
            inf_init_context_typed(&context);
            inf_init_state_typed(&state, &context);
            inf_put_int_ctx(
                (EncounterState*)&state,
                (EncounterContext*)&context,
                "start_wave",
                waves[w]);
            inf_finalize_route_topology(&context);
            inf_reset_ctx(
                (EncounterState*)&state,
                (EncounterContext*)&context,
                seeds[s]);
            uint64_t arng =
                ((uint64_t)seeds[s] << 20) ^ (uint64_t)(waves[w] + 1) ^
                0xD1B54A32D192ED03ULL;
            for (int t = 0; t < TICKS; t++) {
                inf_refresh_current_obs_slots_ctx(&state, &context);
                for (int head = 0; head < INF_NUM_ACTION_HEADS; head++) {
                    actions[head] = (int)(
                        splitmix64(&arng) % (uint64_t)INF_ACTION_DIMS[head]);
                }
                inf_step_ctx(
                    (EncounterState*)&state,
                    (EncounterContext*)&context,
                    actions);
                inf_write_obs_ctx(
                    (EncounterState*)&state,
                    (EncounterContext*)&context,
                    obs);
                inf_write_mask_ctx(
                    (EncounterState*)&state,
                    (EncounterContext*)&context,
                    mask);
                *sink += (uint64_t)(int)state.tick;
                *sink += (uint64_t)(int)state.player.current_hitpoints;
                *sink += (uint64_t)(int)obs[0];
                *sink += (uint64_t)(int)mask[0];
                steps++;
                if (state.episode_over) {
                    inf_reset_ctx(
                        (EncounterState*)&state,
                        (EncounterContext*)&context,
                        0);
                }
            }
            inf_destroy_context((EncounterContext*)&context);
        }
    }
    return steps;
}
#endif

#ifdef BENCH_COLOSSEUM
static uint64_t run_colosseum(uint64_t* sink) {
    static const int waves[] = {1, 6, 11, 12};
    static const uint32_t seeds[] = {0x00010001u, 0x0006000du, 0x000b001fu};
    enum { TICKS = 2500 };
    uint64_t steps = 0;
    ColosseumContext ctx;
    ColosseumState state;
    static float obs[COLO_NUM_OBS];
    static float mask[COLO_ACTION_MASK_SIZE];
    int actions[COLO_NUM_ACTION_HEADS];

    for (int w = 0; w < (int)(sizeof(waves) / sizeof(waves[0])); w++) {
        for (int s = 0; s < (int)(sizeof(seeds) / sizeof(seeds[0])); s++) {
            col_init_context_typed(&ctx);
            ctx.config.start_wave = waves[w] - 1;
            col_finalize_route_topology(&ctx);
            memset(&state, 0, sizeof(state));
            col_reset_ctx(
                (EncounterState*)&state,
                (EncounterContext*)&ctx,
                seeds[s]);
            uint64_t arng =
                0x6a09e667f3bcc909ULL ^ ((uint64_t)seeds[s] << 17) ^
                (uint64_t)waves[w];
            for (int t = 0; t < TICKS; t++) {
                for (int head = 0; head < COLO_NUM_ACTION_HEADS; head++) {
                    actions[head] = (int)(
                        splitmix64(&arng) % (uint64_t)COLO_ACTION_DIMS[head]);
                }
                if (state.modifiers.draft_pending) {
                    actions[COLO_HEAD_PRIMARY] = 0;
                    actions[COLO_HEAD_MODIFIER_SELECT] =
                        1 + (int)(splitmix64(&arng) % COLO_MODIFIER_DRAFT_OPTIONS);
                }
                col_step_ctx(
                    (EncounterState*)&state, (EncounterContext*)&ctx, actions);
                col_write_obs_ctx(
                    (EncounterState*)&state, (EncounterContext*)&ctx, obs);
                col_write_mask_ctx(
                    (EncounterState*)&state, (EncounterContext*)&ctx, mask);
                *sink += (uint64_t)(int)state.tick;
                *sink += (uint64_t)(int)state.player.current_hitpoints;
                *sink += (uint64_t)(int)obs[0];
                *sink += (uint64_t)(int)mask[0];
                steps++;
                if (state.episode_over) {
                    col_reset_ctx(
                        (EncounterState*)&state, (EncounterContext*)&ctx, 0);
                }
            }
            col_destroy_context((EncounterContext*)&ctx);
        }
    }
    return steps;
}
#endif

int main(void) {
    uint64_t sink = 0;
    double t0 = now_s();
#ifdef BENCH_INFERNO
    const char* name = "inferno";
    uint64_t steps = run_inferno(&sink);
#else
    const char* name = "colosseum";
    uint64_t steps = run_colosseum(&sink);
#endif
    double elapsed = now_s() - t0;
    if (elapsed < 1e-9) elapsed = 1e-9;
    double sps = (double)steps / elapsed;
    printf("BENCH %s steps=%llu seconds=%.6f sps=%.3f sink=%llu\n",
        name,
        (unsigned long long)steps,
        elapsed,
        sps,
        (unsigned long long)sink);
    printf("STEPS %llu\n", (unsigned long long)steps);
    printf("SECONDS %.6f\n", elapsed);
    printf("SPS %.3f\n", sps);
    return 0;
}
