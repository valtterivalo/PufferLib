/* Rollout speed of the fitted opponent against a scripted profile on the same sim:
 *
 *   cc -std=c11 -O2 -I. -o /tmp/bench_opponent ocean/osrs/tests/bench_opponent.c -lm
 *   /tmp/bench_opponent TICKS ENVS fit|profile
 *
 * Side 0 is always a scripted profile; side 1 is opp_fit_actions with the committed fixtures (and its history kept as a rollout keeps it) or a profile. */
#define _POSIX_C_SOURCE 200809L
#include <time.h>
#include "riskfight_opp.h"

typedef struct { RiskfightState state; RiskfightContext context; float obs[2][RF_OBS_SIZE]; Hist hist; int tick; } BenchFight;

static double seconds(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec + t.tv_nsec * 1e-9;
}

int main(int argc, char** argv) {
    assert(argc == 4);
    long ticks = strtol(argv[1], NULL, 10);
    int count = atoi(argv[2]), fitted = !strcmp(argv[3], "fit");
    static OppFit fit;
    opp_load("ocean/osrs/tests/fixtures/model.fit", "ocean/osrs/tests/fixtures/eat_mixture.fit", &fit, &g_eat_model);
    g_ablation.escape_model = 1;
    BenchFight* fights = calloc(count, sizeof(*fights));
    for (int i = 0; i < count; i++) {
        BenchFight* f = fights + i;
        riskfight_init_context((EncounterContext*)&f->context);
        riskfight_init_state((EncounterState*)&f->state, (EncounterContext*)&f->context);
        f->context.self_play = 1;
        riskfight_finalize_context((EncounterState*)&f->state, (EncounterContext*)&f->context);
        riskfight_reset((EncounterState*)&f->state, (EncounterContext*)&f->context, i + 73);
        f->hist = hist_start();
        for (int side = 0; side < 2; side++) riskfight_write_observation(&f->state, side, f->obs[side]);
    }
    uint64_t checksum = 0;
    long episodes = 0;
    double start = seconds();
    for (long tick = 0; tick < ticks; tick++) {
        BenchFight* f = fights + tick % count;
        int actions[2 * RF_HEADS];
        riskfight_script(f->obs[0], riskfight_pick_profile((RiskfightOpponent)0, f->state.script_seed[0]), f->state.script_seed[0], f->state.consume_ticks[0], actions);
        g_rollout_seed = (uint32_t)(tick * 2654435761u);
        if (fitted) opp_fit_actions(&fit, &f->state, &f->hist, f->tick, f->state.script_seed[1], actions + RF_HEADS);
        else riskfight_script(f->obs[1], riskfight_pick_profile((RiskfightOpponent)0, f->state.script_seed[1]), f->state.script_seed[1], f->state.consume_ticks[1], actions + RF_HEADS);
        int vw = f->state.spec_voidwaker[1], maul = f->state.spec_maul[1];
        riskfight_bag_to_physical(&f->state.env.players[0], actions); /* the scripts read the net's observation */
        if (!fitted) riskfight_bag_to_physical(&f->state.env.players[1], actions + RF_HEADS);
        riskfight_step((EncounterState*)&f->state, (EncounterContext*)&f->context, actions);
        StepEvents e = {.swung = f->state.visible[0].last_attack_tick == f->state.env.tick - 1, .vw = f->state.spec_voidwaker[1] - vw, .maul = f->state.spec_maul[1] - maul};
        if (fitted) hist_step(&f->hist, &f->state, f->tick++, &e);
        checksum = checksum * 31 + f->state.env.tick + f->state.env.players[0].current_hitpoints;
        if (f->state.env.episode_over) { episodes++; riskfight_reset((EncounterState*)&f->state, (EncounterContext*)&f->context, 0); f->hist = hist_start(); f->tick = 0; }
        for (int side = 0; side < 2; side++) riskfight_write_observation(&f->state, side, f->obs[side]);
    }
    double wall = seconds() - start;
    printf("{\"opponent\":\"%s\",\"ticks\":%ld,\"envs\":%d,\"wall_s\":%.6f,\"ticks_per_s\":%.0f,\"episodes\":%ld,\"checksum\":%llu}\n", argv[3], ticks, count, wall, ticks / wall, episodes, (unsigned long long)checksum);
    for (int i = 0; i < count; i++) riskfight_destroy_context((EncounterContext*)&fights[i].context);
    free(fights);
}
