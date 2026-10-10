#include "ocean/osrs/encounters/encounter_nh_pvp.h"
#include "ocean/osrs/tests/osrs_test_check.h"

static NhPvpState state;
static NhPvpContext context;

static void reset(void) {
    ENCOUNTER_NH_PVP.init_state((EncounterState*)&state, (EncounterContext*)&context);
    ENCOUNTER_NH_PVP.init_context((EncounterContext*)&context);
    ENCOUNTER_NH_PVP.put_int((EncounterState*)&state, (EncounterContext*)&context,
        "use_c_opponent", 0);
    ENCOUNTER_NH_PVP.finalize_context((EncounterState*)&state, (EncounterContext*)&context);
    ENCOUNTER_NH_PVP.reset((EncounterState*)&state, (EncounterContext*)&context, 73);
    state.env.auto_reset = 0;
    state.env.pvp_runtime.use_c_opponent_p0 = 0;
    state.env.pvp_runtime.use_external_opponent_actions = 1;
    state.env.players[0].x = FIGHT_AREA_BASE_X + 10;
    state.env.players[1].x = FIGHT_AREA_BASE_X + 11;
    state.env.players[0].y = FIGHT_AREA_BASE_Y + 10;
    state.env.players[1].y = FIGHT_AREA_BASE_Y + 10;
    for (int actor = 0; actor < NUM_AGENTS; actor++) {
        state.env.players[actor].has_attack_timer = 0;
        state.env.players[actor].attack_timer = 0;
    }
}

static void step_target(const int actions[NH_PVP_NUM_ACTION_HEADS]) {
    int idle[NH_PVP_NUM_ACTION_HEADS] = {0};
    nh_pvp_set_external_actions(&state, actions);
    nh_pvp_step((EncounterState*)&state, (EncounterContext*)&context, idle);
}

static void test_consumption_refreshes_quantized_health(void) {
    reset();
    Player* target = &state.env.players[1];
    target->current_hitpoints = 20;
    CHECK("food fixture starts with a stale cached bar",
        state.env.pvp_runtime.visible[1].health_bar == OSRS_PLAYER_HEALTH_BAR_SCALE);
    target->inventory_cells[0] = osrs_inventory_cell_from_raw_osrs_id(385);
    int actions[NH_PVP_NUM_ACTION_HEADS] = {0};
    actions[NH_PVP_HEAD_FOOD] = NH_PVP_FOOD_SHARK;
    step_target(actions);

    int expected_bar = osrs_health_bar_ratio(target->current_hitpoints,
        target->base_hitpoints, OSRS_PLAYER_HEALTH_BAR_SCALE);
    CHECK("food refreshes visible health without a later hit",
        target->consumable_used_this_tick &&
        target->current_hitpoints == 40 &&
        state.env.pvp_runtime.visible[1].health_bar == expected_bar);

    float obs[NH_PVP_NUM_OBS];
    pvp_write_observations(obs, &state.env, 0);
    CHECK("observation exposes quantized health only",
        obs[NH_PVP_TARGET_START] == (float)expected_bar / OSRS_PLAYER_HEALTH_BAR_SCALE);
}

static void test_brew_refreshes_quantized_health(void) {
    reset();
    Player* target = &state.env.players[1];
    target->current_hitpoints = 20;
    target->inventory_cells[0] = osrs_inventory_cell_from_raw_osrs_id(6685);
    target->brew_doses = 4;
    int actions[NH_PVP_NUM_ACTION_HEADS] = {0};
    actions[NH_PVP_HEAD_DRINK] = NH_PVP_DRINK_BREW;
    step_target(actions);

    int expected_bar = osrs_health_bar_ratio(target->current_hitpoints,
        target->base_hitpoints, OSRS_PLAYER_HEALTH_BAR_SCALE);
    CHECK("brew refreshes visible health without a later hit",
        target->ate_brew_this_tick && target->current_hitpoints == 36 &&
        state.env.pvp_runtime.visible[1].health_bar == expected_bar);
}

static void test_blood_heal_refreshes_attacker_health(void) {
    reset();
    Player* attacker = &state.env.players[0];
    Player* defender = &state.env.players[1];
    attacker->current_hitpoints = 70;
    defender->current_hitpoints = 70;
    PendingHit hit = {
        .kind = OSRS_HIT_DIRECT,
        .damage = 20,
        .attack_type = ATTACK_STYLE_MAGIC,
        .hit_success = 1,
        .heal_percent = 50,
        .defender_prayer_at_attack = PRAYER_NONE,
    };
    apply_damage(&state.env, 0, 1, &hit);

    int attacker_bar = osrs_health_bar_ratio(attacker->current_hitpoints,
        attacker->base_hitpoints, OSRS_PLAYER_HEALTH_BAR_SCALE);
    int defender_bar = osrs_health_bar_ratio(defender->current_hitpoints,
        defender->base_hitpoints, OSRS_PLAYER_HEALTH_BAR_SCALE);
    CHECK("blood healing refreshes the attacker's public bar",
        attacker->current_hitpoints == 80 &&
        state.env.pvp_runtime.visible[0].health_bar == attacker_bar);
    CHECK("hit refreshes the defender's public bar",
        state.env.pvp_runtime.visible[1].health_bar == defender_bar);
}

static void test_special_launch_event_and_reset(void) {
    reset();
    Player* attacker = &state.env.players[1];
    attacker->equipped[GEAR_SLOT_WEAPON] = ITEM_AGS;
    attacker->melee_spec_weapon = MELEE_SPEC_AGS;
    attacker->special_energy = 100;
    osrs_refresh_player_equipment(attacker);
    pvp_refresh_visible_gear(attacker);
    perform_attack(&state.env, 1, 0, ATTACK_STYLE_MELEE, 1, 0, 1);
    CHECK("actual AGS special records its public launch tick",
        attacker->used_special_this_tick &&
        state.env.pvp_runtime.visible[1].last_special_tick == state.env.tick);

    reset();
    attacker = &state.env.players[1];
    attacker->equipped[GEAR_SLOT_WEAPON] = ITEM_AGS;
    attacker->melee_spec_weapon = MELEE_SPEC_AGS;
    osrs_refresh_player_equipment(attacker);
    pvp_refresh_visible_gear(attacker);
    perform_attack(&state.env, 1, 0, ATTACK_STYLE_MELEE, 0, 0, 1);
    CHECK("ordinary same-weapon melee does not record a special event",
        !attacker->used_special_this_tick &&
        state.env.pvp_runtime.visible[1].last_attack_tick == state.env.tick &&
        state.env.pvp_runtime.visible[1].last_special_tick == -1);

    reset();
    CHECK("reset initializes special event as absent",
        state.env.pvp_runtime.visible[1].last_special_tick == -1);
}

int main(void) {
    test_consumption_refreshes_quantized_health();
    test_brew_refreshes_quantized_health();
    test_blood_heal_refreshes_attacker_health();
    test_special_launch_event_and_reset();
    return osrs_test_summary();
}
