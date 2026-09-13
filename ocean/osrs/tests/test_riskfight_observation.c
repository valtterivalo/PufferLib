#include <assert.h>
#include <math.h>
#include <stdio.h>
#include "../encounters/encounter_riskfight.h"

static RiskfightState state;
static RiskfightState before;
static RiskfightContext context;

static void reset(void) {
    riskfight_reset((EncounterState*)&state, (EncounterContext*)&context, 12345);
}

static void test_initial_scale_and_purity(void) {
    reset();
    float obs[RF_OBS_SIZE];
    before = state;
    riskfight_write_observation(&state, 0, obs);
    assert(memcmp(&state, &before, sizeof(state)) == 0);
    for (int i = 0; i < RF_OBS_SIZE; i++) assert(fabsf(obs[i]) <= 1);
    assert(obs[21] == 0 && obs[22] == 0);
    assert(obs[RF_OPPONENT_START + NUM_GEAR_SLOTS + 1] * RF_OBSERVATION_TILE_SCALE == 1);
    for (int slot = 0; slot < NUM_GEAR_SLOTS; slot++)
        assert(obs[RF_EQUIPPED_START + slot] * RF_OBSERVATION_ITEM_SCALE == state.env.players[0].equipped[slot]);
    assert(obs[RF_OPPONENT_START + GEAR_SLOT_RING] * RF_OBSERVATION_ITEM_SCALE == ITEM_NONE);
    assert(obs[RF_OPPONENT_START + GEAR_SLOT_AMMO] * RF_OBSERVATION_ITEM_SCALE == ITEM_NONE);
}

static void test_unbounded_values_are_not_clipped(void) {
    reset();
    const int ticks[] = {1, 1024, 65536, 1048576};
    for (int i = 0; i < 4; i++) {
        state.env.tick = ticks[i];
        state.visible[0].last_attack_tick = 0;
        state.visible[0].last_attack_speed = 7;
        float obs[RF_OBS_SIZE];
        riskfight_write_observation(&state, 0, obs);
        assert(obs[20] * RF_OBSERVATION_TICK_SCALE == ticks[i]);
        assert(obs[RF_OPPONENT_START + NUM_GEAR_SLOTS + 5] * RF_OBSERVATION_ATTACK_AGE_SCALE == ticks[i]);
    }
}

static void test_event_and_position_roundtrip(void) {
    reset();
    state.env.tick = 1;
    state.env.players[0].x = FIGHT_AREA_BASE_X;
    state.env.players[0].y = FIGHT_AREA_BASE_Y;
    RiskfightVisibleOpponent* visible = &state.visible[0];
    visible->x = FIGHT_AREA_BASE_X + FIGHT_AREA_WIDTH - 1;
    visible->y = FIGHT_AREA_BASE_Y + FIGHT_AREA_HEIGHT - 1;
    visible->last_attack_tick = 0;
    visible->last_attack_speed = 7;
    float event[RF_EVENT_WIDTH] = {2, ITEM_GRANITE_MAUL_ORNATE, ATTACK_STYLE_MELEE, 1, 173, 1, 0, 29};
    memcpy(visible->events[0], event, sizeof(event));
    float obs[RF_OBS_SIZE];
    riskfight_write_observation(&state, 0, obs);
    assert(obs[21] * RF_OBSERVATION_TILE_SCALE + FIGHT_AREA_BASE_X + FIGHT_AREA_WIDTH / 2 == state.env.players[0].x);
    assert(obs[22] * RF_OBSERVATION_TILE_SCALE + FIGHT_AREA_BASE_Y + FIGHT_AREA_HEIGHT / 2 == state.env.players[0].y);
    assert(obs[RF_OPPONENT_START + NUM_GEAR_SLOTS + 1] * RF_OBSERVATION_TILE_SCALE == FIGHT_AREA_WIDTH - 1);
    assert(obs[RF_OPPONENT_START + NUM_GEAR_SLOTS + 2] * RF_OBSERVATION_TILE_SCALE == FIGHT_AREA_HEIGHT - 1);
    const float scales[RF_EVENT_WIDTH] = {RF_OBSERVATION_ATTACK_COUNT_SCALE,
        RF_OBSERVATION_ITEM_SCALE, RF_OBSERVATION_STYLE_SCALE, 1,
        RF_OBSERVATION_DAMAGE_SCALE, 1, 1, RF_OBSERVATION_HEALTH_BAR_SCALE};
    for (int i = 0; i < RF_EVENT_WIDTH; i++)
        assert(obs[RF_HISTORY_START + i] * scales[i] == event[i]);
    assert(obs[RF_OPPONENT_START + NUM_GEAR_SLOTS + 6] * RF_OBSERVATION_ATTACK_AGE_SCALE == 6);
}

static void test_script_readiness_units(void) {
    reset();
    state.env.players[0].current_hitpoints = 80;
    state.env.players[0].special_energy = 0;
    state.visible[0].last_attack_tick = 0;
    state.visible[0].last_attack_speed = 7;
    for (int tick = 1; tick <= 7; tick++) {
        state.env.tick = tick;
        float obs[RF_OBS_SIZE];
        int actions[RF_HEADS];
        riskfight_write_observation(&state, 0, obs);
        riskfight_script(obs, RISKFIGHT_AGGRESSIVE, actions);
        assert(actions[RF_ORB] == (7 - tick > 2));
    }
}

int main(void) {
    _Static_assert(RF_OBS_SIZE == 349, "Riskfight observation shape");
    _Static_assert(RF_OBSERVATION_SCHEMA_VERSION == 2, "Riskfight observation schema");
    riskfight_init_context((EncounterContext*)&context);
    riskfight_finalize_context((EncounterState*)&state, (EncounterContext*)&context);
    test_initial_scale_and_purity();
    test_unbounded_values_are_not_clipped();
    test_event_and_position_roundtrip();
    test_script_readiness_units();
    riskfight_destroy_context((EncounterContext*)&context);
    puts("Riskfight observation scaling contracts passed");
}
