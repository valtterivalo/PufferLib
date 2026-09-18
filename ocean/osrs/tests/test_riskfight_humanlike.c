#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../encounters/encounter_riskfight.h"

static RiskfightState state;
static RiskfightContext context;

static void reset(void) {
    riskfight_reset((EncounterState*)&state, (EncounterContext*)&context, 12345);
    context.self_play = 1;
}

static void decide_at_tick(int tick, int* actions) {
    state.env.tick = tick;
    float obs[RF_OBS_SIZE];
    riskfight_write_observation(&state, 0, obs);
    // Writer stamps obs[20] from env.tick, so the sampler sees `tick`.
    riskfight_script(obs, RISKFIGHT_HUMANLIKE, actions);
}

static void decide(int* actions) {
    decide_at_tick(state.env.tick, actions);
}

int main(void) {
    riskfight_init_context((EncounterContext*)&context);
    riskfight_finalize_context((EncounterState*)&state, (EncounterContext*)&context);
    assert(RISKFIGHT_HUMANLIKE == 9);

    // Replay-safe: identical obs+tick yields identical actions.
    reset();
    state.env.tick = 77;
    float obs[RF_OBS_SIZE];
    riskfight_write_observation(&state, 0, obs);
    int first[RF_HEADS], second[RF_HEADS];
    riskfight_script(obs, RISKFIGHT_HUMANLIKE, first);
    riskfight_script(obs, RISKFIGHT_HUMANLIKE, second);
    assert(memcmp(first, second, sizeof(first)) == 0);

    // Sampling live both ways: fixed mid-HP obs over 1000 ticks eats
    // on some ticks and skips on others (any sane mined 0 < p < 1000).
    reset();
    state.env.players[0].current_hitpoints = 60;
    int eat_ticks = 0;
    for (int tick = 0; tick < 1000; tick++) {
        int actions[RF_HEADS];
        decide_at_tick(tick, actions);
        eat_ticks += actions[RF_FOOD] || actions[RF_DRINK] || actions[RF_COMBO];
        for (int head = 0; head < RF_HEADS; head++)
            assert(actions[head] >= 0 && actions[head] < RF_ACTION_DIMS[head]);
    }
    assert(eat_ticks > 0 && eat_ticks < 1000);

    // Full HP with supplies: no food/combo/teleport over 200 ticks.
    reset();
    state.env.players[0].current_hitpoints = 121;
    for (int tick = 0; tick < 200; tick++) {
        int actions[RF_HEADS];
        decide_at_tick(tick, actions);
        assert(!actions[RF_FOOD] && !actions[RF_COMBO]);
        assert(actions[RF_PRIMARY] != RF_TELEPORT);
        for (int head = 0; head < RF_HEADS; head++)
            assert(actions[head] >= 0 && actions[head] < RF_ACTION_DIMS[head]);
    }

    // Zero energy: no spec over 200 ticks.
    reset();
    state.env.players[0].special_energy = 0;
    state.env.players[0].current_hitpoints = 60;
    for (int tick = 0; tick < 200; tick++) {
        int actions[RF_HEADS];
        decide_at_tick(tick, actions);
        assert(actions[RF_SPECIAL] == 0);
    }

    riskfight_destroy_context((EncounterContext*)&context);
    puts("Riskfight humanlike contracts passed");
}
