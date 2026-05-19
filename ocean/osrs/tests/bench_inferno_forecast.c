#define _POSIX_C_SOURCE 200809L

/**
 * @file bench_inferno_forecast.c
 * @brief microbenchmark for Inferno step-out forecast and observation encoding.
 *
 * BUILD:
 *   cc -std=c11 -O3 -DNDEBUG -I. -o /tmp/bench_inferno_forecast \
 *       ocean/osrs/tests/bench_inferno_forecast.c -lm
 *   /tmp/bench_inferno_forecast
 */

#include <stdio.h>
#include <string.h>
#include <time.h>

#include "ocean/osrs/encounters/encounter_inferno.h"

static volatile double bench_sink = 0.0;

static double now_seconds(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1000000000.0;
}

static void init_bench_state(InfernoState* state, int player_x, int player_y) {
    inf_legacy_context()->config = inf_default_config();
    inf_legacy_context()->config.step_out_forecast_obs_enabled = 1;
    inf_build_npc_stats();
    memset(state, 0, sizeof(*state));
    memset(state->npc_los_cache, -1, sizeof(state->npc_los_cache));
    state->rng_state = 20260516u;
    state->wave = 63;
    state->player.entity_type = ENTITY_PLAYER;
    state->player.x = player_x;
    state->player.y = player_y;
    state->player.base_hitpoints = 99;
    state->player.current_hitpoints = 99;
    state->player.current_defence = 99;
    state->player.current_magic = 99;
    state->player.current_prayer = 99;
    state->player_last_interaction_target_slot = -1;
    state->player_last_interaction_age = 1;
    state->player_dest_x = -1;
    state->player_dest_y = -1;
    state->weapon_set = INF_GEAR_LONG_RANGE;
    osrs_interaction_init(&state->interaction);
    for (int p = 0; p < INF_NUM_PILLARS; p++) {
        state->pillars[p].x = INF_PILLAR_POS[p][0];
        state->pillars[p].y = INF_PILLAR_POS[p][1];
        state->pillars[p].hp = INF_PILLAR_HP;
        state->pillars[p].active = 1;
    }
    inf_rebuild_los(state);
    inf_rebuild_player_collision_flags(state);
}

static void add_bench_npc(
    InfernoState* state, int slot, InfNPCType type, int x, int y, int timer
) {
    inf_init_npc(state, slot, type, x, y);
    state->npcs[slot].attack_timer = timer;
    state->npcs[slot].stun_timer = 0;
    state->npcs[slot].frozen_ticks = 0;
}

static void init_empty_state(InfernoState* state) {
    init_bench_state(state, 29, 39);
}

static void init_pillar_stack_state(InfernoState* state) {
    init_bench_state(state, 29, 39);
    add_bench_npc(state, 0, INF_NPC_RANGER, 24, 31, 0);
    add_bench_npc(state, 1, INF_NPC_MAGER, 29, 30, 0);
    inf_rebuild_entity_collision_flags(state);
}

static void init_dense_wave_state(InfernoState* state) {
    init_bench_state(state, 29, 39);
    add_bench_npc(state, 0, INF_NPC_NIBBLER, 15, 39, 0);
    add_bench_npc(state, 1, INF_NPC_NIBBLER, 16, 39, 1);
    add_bench_npc(state, 2, INF_NPC_NIBBLER, 17, 39, 2);
    add_bench_npc(state, 3, INF_NPC_MAGER, 24, 31, 0);
    add_bench_npc(state, 4, INF_NPC_RANGER, 29, 30, 0);
    add_bench_npc(state, 5, INF_NPC_MELEER, 23, 34, 0);
    add_bench_npc(state, 6, INF_NPC_BLOB, 20, 32, 1);
    add_bench_npc(state, 7, INF_NPC_BLOB, 26, 35, 2);
    add_bench_npc(state, 8, INF_NPC_BAT, 18, 30, 3);
    add_bench_npc(state, 9, INF_NPC_BAT, 19, 30, 4);
    inf_rebuild_entity_collision_flags(state);
}

static void init_pillar_stack_no_forecast_state(InfernoState* state) {
    init_pillar_stack_state(state);
    inf_legacy_context()->config.step_out_forecast_obs_enabled = 0;
}

static void init_dense_wave_no_forecast_state(InfernoState* state) {
    init_dense_wave_state(state);
    inf_legacy_context()->config.step_out_forecast_obs_enabled = 0;
}

typedef void (*BenchInit)(InfernoState*);
typedef void (*BenchFn)(InfernoState*, float*);
typedef void (*FixedBenchFn)(const InfernoState*, float*);

static void bench_forecast(InfernoState* state, float* obs) {
    (void)obs;
    InfStepOutForecast forecast;
    inf_build_step_out_forecast(state, &forecast);
    bench_sink += forecast.actions[0].valid;
    bench_sink += forecast.actions[ENCOUNTER_MOVE_ACTIONS - 1].ticks[0].max_hit;
}

static void bench_obs(InfernoState* state, float* obs) {
    inf_write_obs((EncounterState*)state, obs);
    bench_sink += obs[0];
    bench_sink += obs[INF_NUM_OBS - 1];
}

static void bench_mask(InfernoState* state, float* obs) {
    inf_write_mask((EncounterState*)state, obs);
    bench_sink += obs[0];
    bench_sink += obs[INF_ACTION_MASK_SIZE - 1];
}

static void bench_copy_fixed(const InfernoState* template, float* obs) {
    (void)obs;
    InfernoState state;
    memcpy(&state, template, sizeof(state));
    bench_sink += state.player.x;
}

static void bench_step_fixed(const InfernoState* template, float* obs) {
    (void)obs;
    InfernoState state;
    memcpy(&state, template, sizeof(state));
    int actions[INF_NUM_ACTION_HEADS] = {0};
    inf_step((EncounterState*)&state, actions);
    bench_sink += state.player.current_hitpoints;
}

static void bench_step_obs_mask_fixed(const InfernoState* template, float* obs) {
    InfernoState state;
    memcpy(&state, template, sizeof(state));
    int actions[INF_NUM_ACTION_HEADS] = {0};
    inf_step((EncounterState*)&state, actions);
    inf_write_obs((EncounterState*)&state, obs);
    inf_write_mask((EncounterState*)&state, obs + INF_NUM_OBS);
    bench_sink += state.player.current_hitpoints;
    bench_sink += obs[0];
}

static void run_bench(const char* label, BenchInit init, BenchFn fn, int iters) {
    InfernoState state;
    float obs[INF_NUM_OBS + INF_ACTION_MASK_SIZE];
    init(&state);
    fn(&state, obs);
    double start = now_seconds();
    for (int i = 0; i < iters; i++) {
        fn(&state, obs);
    }
    double elapsed = now_seconds() - start;
    printf("%-24s %9d calls  %9.3f ms  %9.3f us/call\n",
        label, iters, elapsed * 1000.0, elapsed * 1000000.0 / (double)iters);
}

static void run_fixed_bench(const char* label, BenchInit init, FixedBenchFn fn, int iters) {
    InfernoState template;
    float obs[INF_NUM_OBS + INF_ACTION_MASK_SIZE];
    init(&template);
    fn(&template, obs);
    double start = now_seconds();
    for (int i = 0; i < iters; i++) {
        fn(&template, obs);
    }
    double elapsed = now_seconds() - start;
    printf("%-24s %9d calls  %9.3f ms  %9.3f us/call\n",
        label, iters, elapsed * 1000.0, elapsed * 1000000.0 / (double)iters);
}

int main(void) {
    printf("sizeof(InfernoState) = %zu\n", sizeof(InfernoState));
    printf("INF_NUM_OBS = %d\n", INF_NUM_OBS);
    printf("INF_STEP_OUT_FORECAST_OBS_SIZE = %d\n", INF_STEP_OUT_FORECAST_OBS_SIZE);
    run_bench("empty forecast", init_empty_state, bench_forecast, 200000);
    run_bench("empty obs", init_empty_state, bench_obs, 200000);
    run_bench("empty mask", init_empty_state, bench_mask, 200000);
    run_bench("stack forecast", init_pillar_stack_state, bench_forecast, 100000);
    run_bench("stack obs", init_pillar_stack_state, bench_obs, 100000);
    run_bench("stack obs no forecast", init_pillar_stack_no_forecast_state, bench_obs, 100000);
    run_bench("stack mask", init_pillar_stack_state, bench_mask, 100000);
    run_bench("dense forecast", init_dense_wave_state, bench_forecast, 50000);
    run_bench("dense obs", init_dense_wave_state, bench_obs, 50000);
    run_bench("dense obs no forecast", init_dense_wave_no_forecast_state, bench_obs, 50000);
    run_bench("dense mask", init_dense_wave_state, bench_mask, 50000);
    run_fixed_bench("dense copy fixed", init_dense_wave_state, bench_copy_fixed, 50000);
    run_fixed_bench("dense step fixed", init_dense_wave_state, bench_step_fixed, 50000);
    run_fixed_bench("dense step+obs+mask", init_dense_wave_state, bench_step_obs_mask_fixed, 50000);
    run_fixed_bench("step+obs+mask no fc", init_dense_wave_no_forecast_state, bench_step_obs_mask_fixed, 50000);
    printf("bench_sink = %.3f\n", bench_sink);
    return 0;
}
