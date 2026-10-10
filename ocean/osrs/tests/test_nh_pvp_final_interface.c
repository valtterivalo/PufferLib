#include "ocean/osrs/encounters/encounter_nh_pvp.h"
#include "ocean/osrs/tests/osrs_test_check.h"

static NhPvpState state;
static NhPvpContext context;
static float obs[2][NH_PVP_NUM_OBS];

static void reset_case(int actor, int pid, uint8_t weapon) {
    state.env.rng_reset_count = 0;
    nh_pvp_reset((EncounterState*)&state, (EncounterContext*)&context, 73);
    state.env.auto_reset = 0;
    state.env.pid_holder = pid;
    state.env.pvp_runtime.use_c_opponent = 0;
    state.env.pvp_runtime.use_c_opponent_p0 = 0;
    state.env.pvp_runtime.use_external_opponent_actions = 1;
    memset(state.external_actions, 0, sizeof(state.external_actions));
    int x = state.env.players[actor].x;
    int y = state.env.players[actor].y;
    for (int side = 0; side < NUM_AGENTS; side++) {
        Player* p = &state.env.players[side];
        p->x = x + (side != actor);
        p->y = y;
        p->dest_x = p->x;
        p->dest_y = p->y;
        p->equipped[GEAR_SLOT_WEAPON] = weapon;
        p->equipped[GEAR_SLOT_SHIELD] = ITEM_NONE;
        memset(p->inventory_cells, 0, sizeof(p->inventory_cells));
        p->is_lunar_spellbook = 0;
        p->current_magic = 99;
        p->current_prayer = 99;
        p->prayer = PRAYER_NONE;
        p->offensive_prayer = OFFENSIVE_PRAYER_NONE;
        p->attack_timer = 0;
        p->has_attack_timer = 0;
        p->special_energy = 100;
        p->spec_armed = 0;
        osrs_interaction_clear(&p->interaction);
        osrs_refresh_player_equipment(p);
        pvp_refresh_visible_gear(p);
        CHECK("fixture actor tile is legal", !encounter_arena_topology_tile_blocked(context.route_topology, p->x, p->y));
    }
}

static int allowed(int actor, int head, int action) {
    unsigned char mask[NH_PVP_ACTION_MASK_SIZE];
    pvp_write_action_mask_bytes(mask, &state.env, actor, context.route_topology);
    return mask[nh_pvp_action_head_offset(head) + action];
}

static void step_case(int actor, int* actions) {
    int idle[NH_PVP_NUM_ACTION_HEADS] = {0};
    nh_pvp_set_external_actions(&state, actor == 1 ? actions : idle);
    nh_pvp_step((EncounterState*)&state, (EncounterContext*)&context, actor == 0 ? actions : idle);
}

static void test_joint_clicks(int actor, int pid) {
    int actions[NH_PVP_NUM_ACTION_HEADS] = {0};
    reset_case(actor, pid, ITEM_WHIP);
    Player* p = &state.env.players[actor];
    p->inventory_cells[0] = osrs_inventory_cell_from_item(ITEM_AGS);
    p->inventory_cells[1] = osrs_inventory_cell_from_item(ITEM_DRAGON_DEFENDER);
    int weapon_action = nh_pvp_equipment_action_for_item(GEAR_SLOT_WEAPON, ITEM_AGS);
    int shield_action = nh_pvp_equipment_action_for_item(GEAR_SLOT_SHIELD, ITEM_DRAGON_DEFENDER);
    CHECK("two handed weapon and shield are independently selectable", allowed(actor, NH_PVP_HEAD_EQUIP_SLOT(GEAR_SLOT_WEAPON), weapon_action) && allowed(actor, NH_PVP_HEAD_EQUIP_SLOT(GEAR_SLOT_SHIELD), shield_action));
    actions[NH_PVP_HEAD_PRIMARY] = NH_PVP_PRIMARY_WEAPON;
    actions[NH_PVP_HEAD_EQUIP_SLOT(GEAR_SLOT_WEAPON)] = weapon_action;
    actions[NH_PVP_HEAD_EQUIP_SLOT(GEAR_SLOT_SHIELD)] = shield_action;
    step_case(actor, actions);
    CHECK("later shield click canonically displaces two handed weapon", p->equipped[GEAR_SLOT_WEAPON] == ITEM_NONE && p->equipped[GEAR_SLOT_SHIELD] == ITEM_DRAGON_DEFENDER && nh_pvp_find_item_cell(p, ITEM_AGS) >= 0);
    printf("{\"case\":\"twohand_then_shield\",\"actor\":%d,\"pid\":%d,\"weapon\":%d,\"shield\":%d,\"attacked\":%d}\n", actor, pid, p->equipped[GEAR_SLOT_WEAPON], p->equipped[GEAR_SLOT_SHIELD], p->just_attacked);

    for (int drink = NH_PVP_DRINK_NONE; drink <= NH_PVP_DRINK_RESTORE; drink += NH_PVP_DRINK_RESTORE) {
        reset_case(actor, pid, ITEM_RUNE_CROSSBOW);
        p = &state.env.players[actor];
        p->current_magic = ICE_RUSH_LEVEL - 1;
        p->current_prayer = 0;
        p->restore_doses = 4;
        p->inventory_cells[0] = osrs_inventory_cell_from_raw_osrs_id(3024);
        CHECK("restore availability makes cast and prayer existentially selectable", allowed(actor, NH_PVP_HEAD_PRIMARY, NH_PVP_PRIMARY_ICE) && allowed(actor, NH_PVP_HEAD_OFFENSIVE, ENCOUNTER_OFFENSIVE_SET_REFRESH_AUGURY));
        memset(actions, 0, sizeof(actions));
        actions[NH_PVP_HEAD_PRIMARY] = NH_PVP_PRIMARY_ICE;
        actions[NH_PVP_HEAD_OFFENSIVE] = ENCOUNTER_OFFENSIVE_SET_REFRESH_AUGURY;
        actions[NH_PVP_HEAD_DRINK] = drink;
        step_case(actor, actions);
        CHECK("unselected restore cannot confer spell or prayer effects", drink != NH_PVP_DRINK_NONE || (!p->just_attacked && p->offensive_prayer == OFFENSIVE_PRAYER_NONE));
        CHECK("selected restore enables both actual cast and prayer", drink != NH_PVP_DRINK_RESTORE || (p->just_attacked && p->magic_type_this_tick == OSRS_MAGIC_ATTACK_ANCIENT_ICE && p->offensive_prayer == OFFENSIVE_PRAYER_AUGURY));
        printf("{\"case\":\"restore_joint\",\"actor\":%d,\"pid\":%d,\"drink\":%d,\"magic\":%d,\"prayer_points\":%d,\"attacked\":%d,\"offensive\":%d}\n", actor, pid, drink, p->current_magic, p->current_prayer, p->just_attacked, p->offensive_prayer);
    }
    reset_case(actor, pid, ITEM_RUNE_CROSSBOW);
    p = &state.env.players[actor];
    p->current_magic = ICE_RUSH_LEVEL;
    p->current_hitpoints = 30;
    p->brew_doses = 4;
    p->inventory_cells[0] = osrs_inventory_cell_from_raw_osrs_id(6685);
    CHECK("brew and currently eligible Ice are independently selectable", allowed(actor, NH_PVP_HEAD_PRIMARY, NH_PVP_PRIMARY_ICE) && allowed(actor, NH_PVP_HEAD_DRINK, NH_PVP_DRINK_BREW));
    memset(actions, 0, sizeof(actions));
    actions[NH_PVP_HEAD_PRIMARY] = NH_PVP_PRIMARY_ICE;
    actions[NH_PVP_HEAD_DRINK] = NH_PVP_DRINK_BREW;
    step_case(actor, actions);
    CHECK("brew commits before cast and canonically removes level eligibility", p->ate_brew_this_tick && p->current_magic < ICE_RUSH_LEVEL && !p->just_attacked && state.env.pvp_runtime.pending_manual_cast[actor] == NH_PVP_PENDING_NONE);
    printf("{\"case\":\"brew_then_ice\",\"actor\":%d,\"pid\":%d,\"magic\":%d,\"attacked\":%d}\n", actor, pid, p->current_magic, p->just_attacked);

    reset_case(actor, pid, ITEM_RUNE_CROSSBOW);
    p = &state.env.players[actor];
    state.env.pvp_runtime.pending_manual_cast[actor] = NH_PVP_PENDING_ICE;
    osrs_interaction_set(&p->interaction, 1 - actor);
    p->inventory_cells[0] = osrs_inventory_cell_from_item(ITEM_RUNE_CROSSBOW);
    memset(actions, 0, sizeof(actions));
    actions[NH_PVP_HEAD_EQUIP_SLOT(GEAR_SLOT_WEAPON)] = nh_pvp_equipment_action_for_item(GEAR_SLOT_WEAPON, ITEM_RUNE_CROSSBOW);
    step_case(actor, actions);
    CHECK("redundant identity request preserves queued manual launch", p->just_attacked && p->magic_type_this_tick == OSRS_MAGIC_ATTACK_ANCIENT_ICE && state.env.pvp_runtime.pending_manual_cast[actor] == NH_PVP_PENDING_NONE);
}

static void test_maul_expiry_alias(int actor, int pid) {
    int spent[2];
    for (int future = 0; future < 2; future++) {
        reset_case(actor, pid, ITEM_GRANITE_MAUL_ORNATE);
        Player* p = &state.env.players[actor];
        OsrsGraniteMaulState* maul = &state.env.pvp_runtime.maul[actor];
        maul->preparation = OSRS_GRANITE_MAUL_DESELECTED;
        maul->prepared_hits = 2;
        maul->preparation_expires_tick = state.env.tick + future * OSRS_GRANITE_MAUL_PREPARED_TICKS;
        OsrsGraniteMaulState before = *maul;
        pvp_write_observations(obs[future], &state.env, actor);
        CHECK("maul observation does not mutate runtime preparation", memcmp(maul, &before, sizeof(before)) == 0);
        int actions[NH_PVP_NUM_ACTION_HEADS] = {0};
        actions[NH_PVP_HEAD_PRIMARY] = NH_PVP_PRIMARY_WEAPON;
        step_case(actor, actions);
        spent[future] = 100 - p->special_energy;
    }
    CHECK("expired maul preparation is distinguished before selection", memcmp(obs[0], obs[1], sizeof(obs[0])) != 0);
    CHECK("expired preparation exposes idle and no prepared hits", obs[0][NH_PVP_SELF_CONTROL_START + 11] == 1 && obs[0][NH_PVP_SELF_CONTROL_START + 14] == 0);
    CHECK("live preparation retains deselected double hits", obs[1][NH_PVP_SELF_CONTROL_START + 13] == 1 && obs[1][NH_PVP_SELF_CONTROL_START + 14] == 1);
    CHECK("actual prepared specials respect canonical expiry", spent[0] != spent[1]);
    printf("{\"case\":\"maul_expiry_alias\",\"actor\":%d,\"pid\":%d,\"expired_spent\":%d,\"live_spent\":%d}\n", actor, pid, spent[0], spent[1]);
}

int main(void) {
    nh_pvp_init_state((EncounterState*)&state, (EncounterContext*)&context);
    nh_pvp_init_context((EncounterContext*)&context);
    nh_pvp_finalize_context((EncounterState*)&state, (EncounterContext*)&context);
    for (int actor = 0; actor < NUM_AGENTS; actor++)
        for (int pid = 0; pid < NUM_AGENTS; pid++) {
            test_joint_clicks(actor, pid);
            test_maul_expiry_alias(actor, pid);
        }
    return osrs_test_summary();
}
