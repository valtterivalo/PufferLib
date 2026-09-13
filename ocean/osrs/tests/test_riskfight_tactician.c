#include <assert.h>
#include <stdio.h>
#include "../encounters/encounter_riskfight.h"

static RiskfightState state;
static RiskfightContext context;

static void reset(void) {
    context.self_play = 1;
    riskfight_reset((EncounterState*)&state, (EncounterContext*)&context, 12345);
}

static void test_timed_combo_eating(void) {
    reset();
    state.env.players[0].current_hitpoints = 30;
    float obs[RF_OBS_SIZE];
    riskfight_write_observation(&state, 0, obs);
    Player p = riskfight_observed_self(obs);
    RiskfightEatPlan wait = riskfight_timed_eat(&p, (RiskfightThreatWindow){85, 2, 0});
    assert(wait.clicks == 0);
    RiskfightEatPlan heal = riskfight_timed_eat(&p, (RiskfightThreatWindow){85, 0, 0});
    assert(heal.food && heal.drink && heal.combo);
    assert(heal.healed_hp == 90);
    int actions[2 * RF_HEADS] = {0};
    actions[RF_FOOD] = heal.food;
    actions[RF_DRINK] = heal.drink;
    actions[RF_COMBO] = heal.combo;
    riskfight_step((EncounterState*)&state, (EncounterContext*)&context, actions);
    assert(state.env.players[0].current_hitpoints == heal.healed_hp);
    assert(state.env.players[0].current_strength < p.current_strength);
}

static void test_pie_and_blocked_food(void) {
    reset();
    state.env.players[0].current_hitpoints = 70;
    float obs[RF_OBS_SIZE];
    riskfight_write_observation(&state, 0, obs);
    Player p = riskfight_observed_self(obs);
    RiskfightEatPlan plan = riskfight_choose_eat(&p, 80);
    assert(plan.clicks == 1 && plan.food);
    assert(osrs_inventory_cell_metadata(&p.inventory_cells[plan.food - 1])->consumable_kind == OSRS_CONSUMABLE_SUMMER_PIE);
    p.food_timer = p.potion_timer = p.karambwan_timer = 2;
    plan = riskfight_choose_eat(&p, 90);
    assert(plan.clicks == 0 && plan.healed_hp == 70);
}

static void test_animation_and_maul_threat(void) {
    reset();
    state.env.tick = 10;
    state.visible[0].last_attack_tick = 8;
    state.visible[0].last_attack_speed = 4;
    float obs[RF_OBS_SIZE];
    riskfight_write_observation(&state, 0, obs);
    assert(riskfight_threat_window(obs).ticks_until == 2);
    float pending_obs[RF_OBS_SIZE];
    memcpy(pending_obs, obs, sizeof(obs));
    pending_obs[RF_HISTORY_START] = 1.0f / RF_OBSERVATION_ATTACK_COUNT_SCALE;
    pending_obs[RF_HISTORY_START + 1] = (float)ITEM_DHAROKS_GREATAXE / RF_OBSERVATION_ITEM_SCALE;
    RiskfightThreatWindow switched = riskfight_threat_window(pending_obs);
    assert(switched.ticks_until == 0 && switched.damage > riskfight_threat_window(obs).damage);
    state.visible[0].last_attack_tick = 9;
    riskfight_write_observation(&state, 0, obs);
    RiskfightThreatWindow pending = riskfight_threat_window(obs);
    assert(pending.incoming_animation && pending.ticks_until == 0);
    state.visible[0].last_attack_tick = 8;
    state.visible[0].equipment[GEAR_SLOT_WEAPON] = ITEM_GRANITE_MAUL_ORNATE;
    state.visible[0].equipment[GEAR_SLOT_SHIELD] = ITEM_NONE;
    riskfight_write_observation(&state, 0, obs);
    assert(riskfight_threat_window(obs).ticks_until == 0);
}

static void test_veng_armour_and_hidden_state(void) {
    reset();
    state.env.tick = 10;
    state.visible[0].last_attack_tick = 9;
    state.visible[0].last_attack_speed = 4;
    Player* p = &state.env.players[0];
    p->veng_active = 0;
    p->veng_cooldown = 0;
    p->attack_timer = 3;
    p->special_energy = 0;
    for (int i = 0; i < 2; i++) p->inventory_cells[i] = osrs_inventory_cell_empty();
    float obs[RF_OBS_SIZE], changed[RF_OBS_SIZE];
    int actions[RF_HEADS], repeated[RF_HEADS];
    riskfight_write_observation(&state, 0, obs);
    riskfight_script(obs, RISKFIGHT_TACTICIAN, actions);
    assert(actions[RF_VENGEANCE]);
    assert(actions[RF_HEAD] == RF_UNEQUIP && actions[RF_BODY] == RF_UNEQUIP && actions[RF_LEGS] == 0);
    state.env.players[1].attack_timer = 99;
    state.env.players[1].special_energy = 0;
    state.env.players[1].inventory_cells[0] = osrs_inventory_cell_empty();
    state.env.pid_holder ^= 1;
    riskfight_write_observation(&state, 0, changed);
    assert(memcmp(obs, changed, sizeof(obs)) == 0);
    riskfight_script(changed, RISKFIGHT_TACTICIAN, repeated);
    assert(memcmp(actions, repeated, sizeof(actions)) == 0);
    p->veng_active = 1;
    riskfight_write_observation(&state, 0, obs);
    riskfight_script(obs, RISKFIGHT_TACTICIAN, actions);
    assert(actions[RF_VENGEANCE] == 0);
}

static void test_supply_and_equipment_plans(void) {
    reset();
    Player* p = &state.env.players[0];
    p->current_hitpoints = 30;
    p->current_prayer = 0;
    p->veng_active = 0;
    state.env.tick = 10;
    state.visible[0].last_attack_tick = 4;
    state.visible[0].last_attack_speed = 7;
    state.visible[0].equipment[GEAR_SLOT_WEAPON] = ITEM_DHAROKS_GREATAXE;
    state.visible[0].equipment[GEAR_SLOT_SHIELD] = ITEM_NONE;
    state.visible[0].health_bar = 8;
    float obs[RF_OBS_SIZE];
    int actions[RF_HEADS];
    riskfight_write_observation(&state, 0, obs);
    assert(riskfight_threat_window(obs).ticks_until == 1);
    riskfight_script(obs, RISKFIGHT_TACTICIAN, actions);
    assert(actions[RF_DRINK] == 0);
    state.visible[0].last_attack_tick = 9;
    riskfight_write_observation(&state, 0, obs);
    riskfight_script(obs, RISKFIGHT_TACTICIAN, actions);
    assert(actions[RF_DRINK] > 0);
    assert(actions[RF_VENGEANCE] == 0);
    reset();
    p = &state.env.players[0];
    p->inventory_cells[27] = osrs_inventory_cell_from_item(ITEM_ULTOR_RING);
    state.visible[0].health_bar = 5;
    riskfight_write_observation(&state, 0, obs);
    riskfight_script(obs, RISKFIGHT_TACTICIAN, actions);
    if (actions[RF_WEAPON]) {
        int weapon = osrs_inventory_cell_metadata(&p->inventory_cells[actions[RF_WEAPON] - 1])->item_idx;
        assert(!item_is_two_handed(weapon));
    }
}

int main(void) {
    test_timed_combo_eating();
    test_pie_and_blocked_food();
    test_animation_and_maul_threat();
    test_veng_armour_and_hidden_state();
    test_supply_and_equipment_plans();
    puts("Riskfight tactician contracts passed");
    return 0;
}
