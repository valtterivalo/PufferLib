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
    assert(obs[23] == 0);  // schema 3: idle maul exposes no prepared hits
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

static void observe_next_tick(void) {
    state.env.tick++;
    state.env.players[1].hit_landed_this_tick = 0;
    state.env.players[1].hit_damage = 0;
}

static void received_hit(int damage, OsrsHitKind kind) {
    PendingHit hit = {0};
    hit.damage = damage;
    hit.kind = kind;
    hit.attack_type = ATTACK_STYLE_MELEE;
    hit.hit_success = damage > 0;
    apply_damage(&state.env, 0, 1, &hit);
    riskfight_observe_visible(&state, 0, 0);
    assert(state.visible[0].health_bar == osrs_health_bar_ratio(
        state.env.players[1].current_hitpoints, 99, OSRS_PLAYER_HEALTH_BAR_SCALE));
}

static void use_consumable(OsrsConsumableKind kind) {
    Player* player = &state.env.players[1];
    int slot = -1;
    for (int i = 0; i < OSRS_INVENTORY_SIZE; i++)
        if (osrs_inventory_cell_metadata(&player->inventory_cells[i])->consumable_kind == kind) {
            slot = i;
            break;
        }
    assert(slot >= 0);
    OsrsInventoryUseResult result = osrs_player_use_inventory(player,
        &state.inventory_use[1], NULL, slot, state.env.tick);
    assert(result == OSRS_INVENTORY_USE_CONSUMED);
    riskfight_observe_visible(&state, 0, 0);
}

static void test_health_bar_refresh_requires_received_hit(void) {
    reset();
    state.env.players[1].veng_active = 0;
    observe_next_tick();
    received_hit(91, OSRS_HIT_DIRECT);
    int damaged_bar = state.visible[0].health_bar;
    observe_next_tick();
    use_consumable(OSRS_CONSUMABLE_MARLIN);
    assert(state.env.players[1].current_hitpoints == 54);
    assert(state.visible[0].health_bar == damaged_bar);
    float obs[RF_OBS_SIZE];
    riskfight_write_observation(&state, 0, obs);
    assert(obs[RF_HISTORY_START + 3] == 0);
    observe_next_tick();
    received_hit(0, OSRS_HIT_DIRECT);
    assert(state.visible[0].health_bar != damaged_bar);
    riskfight_write_observation(&state, 0, obs);
    assert(obs[RF_HISTORY_START + 3] == 1);
    int refreshed_bar = state.visible[0].health_bar;
    observe_next_tick();
    use_consumable(OSRS_CONSUMABLE_BREW);
    assert(state.env.players[1].current_hitpoints == 70);
    assert(state.visible[0].health_bar == refreshed_bar);
    observe_next_tick();
    use_consumable(OSRS_CONSUMABLE_LOCATOR_ORB);
    assert(state.env.players[1].current_hitpoints == 60);
    assert(state.visible[0].health_bar == osrs_health_bar_ratio(60, 99, 30));
    observe_next_tick();
    received_hit(3, OSRS_HIT_RECOIL);
    observe_next_tick();
    received_hit(4, OSRS_HIT_VENGEANCE);
    observe_next_tick();
    received_hit(100, OSRS_HIT_DIRECT);
    assert(state.visible[0].health_bar == 0);
}

static void test_passive_healing_keeps_last_known_bar(void) {
    reset();
    Player* opponent = &state.env.players[1];
    opponent->current_hitpoints = 68;
    riskfight_observe_visible(&state, 0, 1);
    int initial_bar = state.visible[0].health_bar;
    observe_next_tick();
    state.inventory_use[1].stat_drift_timer = ENCOUNTER_STAT_DRIFT_TICKS - 1;
    osrs_player_inventory_tick(opponent, &state.inventory_use[1]);
    assert(opponent->current_hitpoints == 69);
    riskfight_observe_visible(&state, 0, 0);
    assert(osrs_health_bar_ratio(opponent->current_hitpoints, 99, 30) != initial_bar);
    assert(state.visible[0].health_bar == initial_bar);
}

int main(void) {
    _Static_assert(RF_OBS_SIZE == 349, "Riskfight observation shape");
    _Static_assert(RF_OBSERVATION_SCHEMA_VERSION == 3, "Riskfight observation schema");
    riskfight_init_context((EncounterContext*)&context);
    riskfight_finalize_context((EncounterState*)&state, (EncounterContext*)&context);
    test_initial_scale_and_purity();
    test_unbounded_values_are_not_clipped();
    test_event_and_position_roundtrip();
    test_script_readiness_units();
    test_health_bar_refresh_requires_received_hit();
    test_passive_healing_keeps_last_known_bar();
    riskfight_destroy_context((EncounterContext*)&context);
    puts("Riskfight observation scaling contracts passed");
}
