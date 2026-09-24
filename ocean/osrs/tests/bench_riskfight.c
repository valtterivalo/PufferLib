#define _POSIX_C_SOURCE 200809L
#include <time.h>
#include "../encounters/encounter_riskfight.h"

typedef struct {
    RiskfightState state;
    RiskfightContext context;
    float obs[2][RF_OBS_SIZE];
    float masks[2][RF_MASK_SIZE];
} BenchFight;

static double seconds(void) {
    struct timespec t;
    int result = clock_gettime(CLOCK_MONOTONIC, &t);
    assert(result == 0);
    return t.tv_sec + t.tv_nsec * 1e-9;
}

int main(int argc, char** argv) {
    assert(argc == 4);
    long ticks = strtol(argv[1], NULL, 10);
    int count = atoi(argv[2]);
    int bot = atoi(argv[3]);
    assert(ticks > 0 && count > 0);
    assert(bot == RISKFIGHT_TRADER || bot == RISKFIGHT_TACTICIAN || bot == RISKFIGHT_FLOOR);
    BenchFight* fights = calloc(count, sizeof(*fights));
    assert(fights);
    for (int i = 0; i < count; i++) {
        BenchFight* f = fights + i;
        riskfight_init_context((EncounterContext*)&f->context);
        riskfight_init_state((EncounterState*)&f->state, (EncounterContext*)&f->context);
        f->context.self_play = 1;
        riskfight_finalize_context((EncounterState*)&f->state, (EncounterContext*)&f->context);
        riskfight_reset((EncounterState*)&f->state, (EncounterContext*)&f->context, i + 73);
        for (int side = 0; side < 2; side++) riskfight_write_observation(&f->state, side, f->obs[side]);
    }
    double script = 0, step = 0, reset = 0, observation = 0, mask = 0;
    uint64_t checksum = 0;
    long episodes = 0;
    double start = seconds();
    for (long tick = 0; tick < ticks; tick++) {
        BenchFight* f = fights + tick % count;
        int actions[2 * RF_HEADS];
        double t = seconds();
        for (int side = 0; side < 2; side++)
            riskfight_script(f->obs[side], (RiskfightOpponent)bot, f->state.script_seed[side], actions + side * RF_HEADS);
        double next = seconds(); script += next - t; t = next;
        riskfight_step((EncounterState*)&f->state, (EncounterContext*)&f->context, actions);
        next = seconds(); step += next - t; t = next;
        checksum = checksum * 31 + f->state.env.tick + f->state.env.players[0].current_hitpoints;
        if (f->state.env.episode_over) {
            episodes++;
            riskfight_reset((EncounterState*)&f->state, (EncounterContext*)&f->context, 0);
        }
        next = seconds(); reset += next - t; t = next;
        for (int side = 0; side < 2; side++) riskfight_write_observation(&f->state, side, f->obs[side]);
        next = seconds(); observation += next - t; t = next;
        for (int side = 0; side < 2; side++) {
            riskfight_write_action_mask(&f->state, side, f->masks[side]);
            checksum += (uint64_t)f->masks[side][RF_ATTACK];
        }
        mask += seconds() - t;
    }
    printf("{\"ticks\":%ld,\"agents_per_tick\":2,\"envs\":%d,\"bot\":%d,\"state_bytes\":%zu,"
        "\"wall_s\":%.6f,\"script_s\":%.6f,\"step_s\":%.6f,\"reset_s\":%.6f,"
        "\"observation_s\":%.6f,\"mask_s\":%.6f,\"episodes\":%ld,\"checksum\":%llu}\n",
        ticks, count, bot, sizeof(RiskfightState), seconds()-start, script, step, reset,
        observation, mask, episodes, (unsigned long long)checksum);
    for (int i = 0; i < count; i++) riskfight_destroy_context((EncounterContext*)&fights[i].context);
    free(fights);
}
