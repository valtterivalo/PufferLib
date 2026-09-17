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
    // Tentacle is the default (axe is a low-HP finisher only): with full
    // supplies the weapon head stays 0 and all three armour pieces unequip
    // for the vengeance bait.
    riskfight_script(obs, RISKFIGHT_TACTICIAN, actions);
    assert(actions[RF_VENGEANCE]);
    assert(actions[RF_WEAPON] == 0);
    assert(actions[RF_HEAD] == RF_UNEQUIP && actions[RF_BODY] == RF_UNEQUIP && actions[RF_LEGS] == RF_UNEQUIP);
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

static void test_dead_fighter_waits_for_pending_hits(void) {
    reset();
    state.env.players[0].current_hitpoints = 0;
    float obs[RF_OBS_SIZE];
    riskfight_write_observation(&state, 0, obs);
    int actions[RF_HEADS];
    riskfight_script(obs, RISKFIGHT_TACTICIAN, actions);
    for (int head = 0; head < RF_HEADS; head++) assert(actions[head] == 0);
}

static void test_retained_health_evidence(void) {
    reset();
    float obs[RF_OBS_SIZE];
    riskfight_write_observation(&state, 0, obs);
    obs[RF_OPPONENT_START + NUM_GEAR_SLOTS] = 8.0f / 30;
    RiskfightInferredHp missing = riskfight_inferred_opponent_hp(obs);
    assert(missing.evidence == RISKFIGHT_HP_HISTORY_MISSING && missing.range.upper == 121);
    obs[RF_HISTORY_START + 3] = 1;
    RiskfightInferredHp fresh = riskfight_inferred_opponent_hp(obs);
    assert(fresh.evidence == RISKFIGHT_HP_FRESH);
    assert(fresh.range.lower == 24 && fresh.range.upper == 27);
    obs[RF_HISTORY_START + 6] = 1;
    RiskfightInferredHp simultaneous = riskfight_inferred_opponent_hp(obs);
    assert(simultaneous.evidence == RISKFIGHT_HP_CONSUMPTION_AMBIGUOUS && simultaneous.range.upper == 121);
    obs[RF_HISTORY_START + 3] = 0;
    obs[RF_HISTORY_START + 3 * RF_EVENT_WIDTH + 3] = 1;
    RiskfightInferredHp consumed = riskfight_inferred_opponent_hp(obs);
    assert(consumed.evidence == RISKFIGHT_HP_CONSUMPTION_AMBIGUOUS && consumed.range.upper == 121);
    obs[RF_HISTORY_START + 6] = 0;
    RiskfightInferredHp retained = riskfight_inferred_opponent_hp(obs);
    assert(retained.evidence == RISKFIGHT_HP_RETAINED);
    assert(retained.range.lower == 24 && retained.range.upper == 28);
    obs[RF_HISTORY_START + 4 * RF_EVENT_WIDTH + 6] = 1;
    RiskfightInferredHp old_consumption = riskfight_inferred_opponent_hp(obs);
    assert(old_consumption.evidence == RISKFIGHT_HP_RETAINED && old_consumption.range.upper == 28);
    obs[RF_HISTORY_START + 3] = 1;
    RiskfightInferredHp refreshed = riskfight_inferred_opponent_hp(obs);
    assert(refreshed.evidence == RISKFIGHT_HP_FRESH && refreshed.range.upper == 27);
}

static void test_weapon_switch_does_not_reset_observed_readiness(void) {
    reset();
    state.env.tick = 10;
    state.visible[0].last_attack_tick = 8;
    state.visible[0].last_attack_speed = 4;
    float obs[RF_OBS_SIZE];
    riskfight_write_observation(&state, 0, obs);
    int remaining = riskfight_threat_window(obs).ticks_until;
    obs[RF_OPPONENT_START + GEAR_SLOT_WEAPON] = (float)ITEM_DHAROKS_GREATAXE / RF_OBSERVATION_ITEM_SCALE;
    obs[RF_OPPONENT_START + GEAR_SLOT_SHIELD] = (float)ITEM_NONE / RF_OBSERVATION_ITEM_SCALE;
    assert(riskfight_threat_window(obs).ticks_until == remaining);
}
static void test_dharok_finisher_discipline(void) {
    // Helper gates: unboosted HP, idle tick, or full-bar opponent never axe.
    assert(!riskfight_dharok_finisher(1, 121, 99, 60, 30, 60,
        RISKFIGHT_CONTINUE, 0, 0));
    assert(!riskfight_dharok_finisher(0, 50, 99, 60, 30, 60,
        RISKFIGHT_CONTINUE, 0, 0));
    assert(!riskfight_dharok_finisher(1, 50, 99, 60, 121, 60,
        RISKFIGHT_CONTINUE, 0, 0));
    // Boosted + fresh low upper bound within axe_max + margin: finisher.
    assert(riskfight_dharok_finisher(1, 50, 99, 60, 90, 60,
        RISKFIGHT_CONTINUE, 0, 0));
    // Reflecting an incoming animation is a finisher even at high upper.
    assert(riskfight_dharok_finisher(1, 50, 99, 60, 121, 60,
        RISKFIGHT_CONTINUE, 1, 1));
    // Profile: at 121 HP vs a full bar the tactician holds the tentacle...
    reset();
    float obs[RF_OBS_SIZE];
    int actions[RF_HEADS];
    riskfight_write_observation(&state, 0, obs);
    riskfight_script(obs, RISKFIGHT_TACTICIAN, actions);
    assert(actions[RF_WEAPON] == 0);
    // ...stays tentacle between hits (attack_timer > 1)...
    state.env.players[0].attack_timer = 4;
    state.env.players[0].current_hitpoints = 50;
    riskfight_write_observation(&state, 0, obs);
    riskfight_script(obs, RISKFIGHT_TACTICIAN, actions);
    assert(actions[RF_WEAPON] == 0);
    // ...and may axe only with a fresh low opponent upper, own ready, and
    // boosted HP: force a fresh hit event with a low bar (upper ~27) while
    // own HP is safe enough to skip eating (threat ~51, HP 80).
    reset();
    state.env.players[0].attack_timer = 0;
    state.env.players[0].current_hitpoints = 60;
    state.env.players[0].special_energy = 0;
    riskfight_observe_visible(&state, 0, 1);
    state.env.tick = 11;
    state.env.players[1].hit_landed_this_tick = 1;
    state.env.players[1].hit_damage = 95;
    state.env.players[1].current_hitpoints = 25;
    riskfight_observe_visible(&state, 0, 0);
    riskfight_write_observation(&state, 0, obs);
    RiskfightInferredHp hp = riskfight_inferred_opponent_hp(obs);
    assert(hp.evidence == RISKFIGHT_HP_FRESH && hp.range.upper <= 60);
    riskfight_script(obs, RISKFIGHT_TACTICIAN, actions);
    int axe_slot = 0;
    for (int i = 0; i < OSRS_INVENTORY_SIZE; i++)
        if (osrs_inventory_cell_metadata(&state.env.players[0].inventory_cells[i])->item_idx == ITEM_DHAROKS_GREATAXE)
            axe_slot = i + 1;
    assert(axe_slot > 0 && actions[RF_WEAPON] == axe_slot);
}

static void test_profile_determinism_and_masks(void) {
    const RiskfightTacticianProfile profiles[] = {RISKFIGHT_PROFILE_BALANCED,
        RISKFIGHT_PROFILE_PRESSURE, RISKFIGHT_PROFILE_CAUTIOUS, RISKFIGHT_PROFILE_HELDOUT};
    uint32_t signatures[4] = {0};
    for (int profile = 0; profile < 4; profile++) {
        reset();
        for (int frame = 0; frame < 400 && !state.env.episode_over; frame++) {
            float obs[RF_OBS_SIZE], opponent_obs[RF_OBS_SIZE], mask[RF_MASK_SIZE];
            int actions[2 * RF_HEADS] = {0}, repeated[RF_HEADS] = {0};
            riskfight_write_observation(&state, 0, obs);
            riskfight_write_action_mask(&state, 0, mask);
            riskfight_tactician_profile(obs, actions, profiles[profile]);
            riskfight_tactician_profile(obs, repeated, profiles[profile]);
            assert(memcmp(actions, repeated, sizeof(repeated)) == 0);
            if (profile == 0) {
                memset(repeated, 0, sizeof(repeated));
                riskfight_tactician(obs, repeated);
                assert(memcmp(actions, repeated, sizeof(repeated)) == 0);
            }
            int offset = 0;
            for (int head = 0; head < RF_HEADS; head++) {
                assert(actions[head] >= 0 && actions[head] < RF_ACTION_DIMS[head]);
                if (!mask[offset + actions[head]]) {
                    fprintf(stderr, "profile=%d frame=%d masked head=%d action=%d\n",
                        profile, frame, head, actions[head]);
                    assert(0);
                }
                signatures[profile] = signatures[profile] * 16777619u + (uint32_t)actions[head];
                offset += RF_ACTION_DIMS[head];
            }
            riskfight_write_observation(&state, 1, opponent_obs);
            riskfight_script(opponent_obs, RISKFIGHT_TRADER, actions + RF_HEADS);
            riskfight_step((EncounterState*)&state, (EncounterContext*)&context, actions);
        }
    }
    assert(signatures[0] != signatures[1] || signatures[0] != signatures[2] || signatures[0] != signatures[3]);
}

int main(void) {
    test_timed_combo_eating();
    test_pie_and_blocked_food();
    test_animation_and_maul_threat();
    test_veng_armour_and_hidden_state();
    test_supply_and_equipment_plans();
    test_dead_fighter_waits_for_pending_hits();
    test_retained_health_evidence();
    test_weapon_switch_does_not_reset_observed_readiness();
    test_profile_determinism_and_masks();
    test_dharok_finisher_discipline();
    puts("Riskfight tactician contracts passed");
    return 0;
}
