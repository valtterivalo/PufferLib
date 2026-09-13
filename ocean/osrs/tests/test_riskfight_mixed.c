#include <assert.h>
#include <stdio.h>
#include "../encounters/encounter_riskfight.h"

int main(void) {
    RiskfightContext context;
    riskfight_init_context((EncounterContext*)&context);
    RiskfightState* state = (RiskfightState*)riskfight_create();
    RiskfightState* repeated = (RiskfightState*)riskfight_create();
    riskfight_finalize_context((EncounterState*)state, (EncounterContext*)&context);
    context.opponent = RISKFIGHT_MIXED;
    int observed[3] = {0};
    for (unsigned seed = 1; seed <= 64; seed++) {
        riskfight_reset((EncounterState*)state, (EncounterContext*)&context, seed);
        riskfight_reset((EncounterState*)repeated, (EncounterContext*)&context, seed);
        assert(memcmp(state, repeated, sizeof(*state)) == 0);
        RiskfightOpponent selected = state->mixed_opponent;
        assert(selected >= RISKFIGHT_TRADER && selected <= RISKFIGHT_AGGRESSIVE);
        observed[selected]++;
        int actions[RF_HEADS] = {0};
        riskfight_step((EncounterState*)state, (EncounterContext*)&context, actions);
        context.opponent = selected;
        riskfight_step((EncounterState*)repeated, (EncounterContext*)&context, actions);
        assert(memcmp(state, repeated, sizeof(*state)) == 0);
        context.opponent = RISKFIGHT_MIXED;
        assert(state->mixed_opponent == selected);
    }
    for (int i = 0; i < 3; i++) assert(observed[i] > 0);
    context.self_play = 1;
    riskfight_reset((EncounterState*)state, (EncounterContext*)&context, 123);
    context.opponent = RISKFIGHT_TRADER;
    riskfight_reset((EncounterState*)repeated, (EncounterContext*)&context, 123);
    assert(memcmp(state, repeated, sizeof(*state)) == 0);
    riskfight_destroy((EncounterState*)state);
    riskfight_destroy((EncounterState*)repeated);
    riskfight_destroy_context((EncounterContext*)&context);
    puts("Riskfight mixed opponents PASS: seeded selection, fixed within fight, script dispatch, self-play independence");
}
