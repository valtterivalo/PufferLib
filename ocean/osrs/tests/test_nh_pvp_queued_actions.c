#define main existing_actions_main
#include "test_nh_pvp_actions.c"
#undef main

static int range_x, range_y;

static void test_pending_cast(int actor, int pid, int primary) {
    reset_case(actor, pid, ITEM_RUNE_CROSSBOW);
    Player* player = &state.env.players[actor];
    player->has_attack_timer = 1;
    player->attack_timer = 1;
    int actions[NH_PVP_NUM_ACTION_HEADS] = {0};
    actions[NH_PVP_HEAD_PRIMARY] = primary;
    step_case(actor, actions);
    CHECK("busy manual cast queues without launch", !player->just_attacked && state.env.pvp_runtime.pending_manual_cast[actor] != NH_PVP_PENDING_NONE);
    memset(actions, 0, sizeof(actions));
    step_case(actor, actions);
    CHECK("retain executes requested delayed magic", player->just_attacked && player->attack_style_this_tick == ATTACK_STYLE_MAGIC && player->magic_type_this_tick == (primary == NH_PVP_PRIMARY_ICE ? OSRS_MAGIC_ATTACK_ANCIENT_ICE : OSRS_MAGIC_ATTACK_ANCIENT_BLOOD));
    CHECK("manual launch consumes request and continuation", state.env.pvp_runtime.pending_manual_cast[actor] == NH_PVP_PENDING_NONE && !osrs_interaction_active(&player->interaction));
    for (int tick = 0; tick < 7; tick++) {
        step_case(actor, actions);
        CHECK("completed manual cast cannot autocast or fire weapon", !player->just_attacked);
    }

    reset_case(actor, pid, ITEM_RUNE_CROSSBOW);
    player = &state.env.players[actor];
    player->x = range_x;
    player->y = range_y;
    state.env.players[1 - actor].x = range_x + 12;
    state.env.players[1 - actor].y = range_y;
    actions[NH_PVP_HEAD_PRIMARY] = primary;
    step_case(actor, actions);
    CHECK("distant ready cast approaches and launches in one tick", player->just_attacked && player->attack_style_this_tick == ATTACK_STYLE_MAGIC && state.env.pvp_runtime.pending_manual_cast[actor] == NH_PVP_PENDING_NONE);
    actions[NH_PVP_HEAD_PRIMARY] = NH_PVP_PRIMARY_RETAIN;
    step_case(actor, actions);
    CHECK("retain cannot repeat completed approach cast", !player->just_attacked && state.env.pvp_runtime.pending_manual_cast[actor] == NH_PVP_PENDING_NONE);

    for (int interrupt = 0; interrupt < 5; interrupt++) {
        reset_case(actor, pid, ITEM_RUNE_CROSSBOW);
        player = &state.env.players[actor];
        player->has_attack_timer = 1;
        player->attack_timer = 2;
        player->current_hitpoints = 30;
        player->food_count = 1;
        player->brew_doses = 4;
        player->inventory_cells[0] = osrs_inventory_cell_from_raw_osrs_id(385);
        player->inventory_cells[1] = osrs_inventory_cell_from_item(ITEM_AHRIMS_ROBESKIRT);
        player->inventory_cells[2] = osrs_inventory_cell_from_raw_osrs_id(6685);
        memset(actions, 0, sizeof(actions));
        actions[NH_PVP_HEAD_PRIMARY] = primary;
        step_case(actor, actions);
        memset(actions, 0, sizeof(actions));
        if (interrupt == 0)
            for (int move = 1; move < OSRS_PRIMARY_MOVE_ACTIONS; move++)
                if (ENCOUNTER_MOVE_TARGET_DX[move] == 0 && ENCOUNTER_MOVE_TARGET_DY[move] == 1) actions[0] = move;
        if (interrupt == 1) actions[NH_PVP_HEAD_FOOD] = NH_PVP_FOOD_SHARK;
        if (interrupt == 2) actions[NH_PVP_HEAD_EQUIP_SLOT(GEAR_SLOT_LEGS)] = nh_pvp_equipment_action_for_item(GEAR_SLOT_LEGS, ITEM_AHRIMS_ROBESKIRT);
        if (interrupt == 3) actions[NH_PVP_HEAD_PRIMARY] = NH_PVP_PRIMARY_WEAPON;
        if (interrupt == 4) actions[NH_PVP_HEAD_DRINK] = NH_PVP_DRINK_BREW;
        step_case(actor, actions);
        CHECK("explicit interrupt clears pending manual cast", state.env.pvp_runtime.pending_manual_cast[actor] == NH_PVP_PENDING_NONE);
        CHECK("weapon replacement preserves only weapon continuation", osrs_interaction_active(&player->interaction) == (interrupt == 3));
        memset(actions, 0, sizeof(actions));
        int launches = 0;
        for (int tick = 0; tick < 4; tick++) {
            step_case(actor, actions);
            launches += player->just_attacked;
            CHECK("interrupt cannot launch stale manual magic", !player->just_attacked || player->attack_style_this_tick == ATTACK_STYLE_RANGED);
        }
        CHECK("only explicit weapon replacement keeps attacks", interrupt == 3 ? launches == 1 : launches == 0);
    }

    reset_case(actor, pid, ITEM_RUNE_CROSSBOW);
    player = &state.env.players[actor];
    player->current_magic = 40;
    player->restore_doses = 4;
    player->inventory_cells[0] = osrs_inventory_cell_from_raw_osrs_id(3024);
    memset(actions, 0, sizeof(actions));
    actions[NH_PVP_HEAD_PRIMARY] = primary;
    CHECK("available restore optimistically exposes spell", allowed(actor, NH_PVP_HEAD_PRIMARY, primary));
    step_case(actor, actions);
    CHECK("ineligible request does not queue weapon or spell", !player->just_attacked && state.env.pvp_runtime.pending_manual_cast[actor] == NH_PVP_PENDING_NONE && !osrs_interaction_active(&player->interaction));
    memset(actions, 0, sizeof(actions));
    actions[NH_PVP_HEAD_DRINK] = NH_PVP_DRINK_RESTORE;
    step_case(actor, actions);
    CHECK("later restore does not revive invalid cast", !player->just_attacked && state.env.pvp_runtime.pending_manual_cast[actor] == NH_PVP_PENDING_NONE);
    actions[NH_PVP_HEAD_PRIMARY] = primary;
    actions[NH_PVP_HEAD_DRINK] = 0;
    step_case(actor, actions);
    CHECK("new eligible cast still executes", player->just_attacked && player->attack_style_this_tick == ATTACK_STYLE_MAGIC);

    reset_case(actor, pid, ITEM_RUNE_CROSSBOW);
    player = &state.env.players[actor];
    player->is_lunar_spellbook = 1;
    memset(actions, 0, sizeof(actions));
    actions[NH_PVP_HEAD_PRIMARY] = primary;
    step_case(actor, actions);
    CHECK("wrong spellbook does not queue manual cast", !player->just_attacked && state.env.pvp_runtime.pending_manual_cast[actor] == NH_PVP_PENDING_NONE && !osrs_interaction_active(&player->interaction));

    for (int prepared = NH_PVP_SPECIAL_ARM; prepared <= NH_PVP_SPECIAL_PREPARE_DOUBLE; prepared += 2) {
        reset_case(actor, pid, ITEM_GRANITE_MAUL_ORNATE);
        player = &state.env.players[actor];
        memset(actions, 0, sizeof(actions));
        actions[NH_PVP_HEAD_SPECIAL] = prepared;
        step_case(actor, actions);
        actions[NH_PVP_HEAD_SPECIAL] = NH_PVP_SPECIAL_RETAIN;
        actions[NH_PVP_HEAD_PRIMARY] = primary;
        step_case(actor, actions);
        CHECK("prepared maul cannot steal manual spell target", player->just_attacked && player->attack_style_this_tick == ATTACK_STYLE_MAGIC && !player->used_special_this_tick && player->special_energy == 100);
    }
}

static void test_pending_equipment_remove(int actor, int pid, int primary) {
    reset_case(actor, pid, ITEM_RUNE_CROSSBOW);
    Player* player = &state.env.players[actor];
    player->has_attack_timer = 1;
    player->attack_timer = 2;
    int actions[NH_PVP_NUM_ACTION_HEADS] = {0};
    actions[NH_PVP_HEAD_PRIMARY] = primary;
    step_case(actor, actions);
    NhPvpPendingManualCast pending = state.env.pvp_runtime.pending_manual_cast[actor];
    CHECK("busy cast establishes removable-equipment continuation", pending != NH_PVP_PENDING_NONE && !player->just_attacked);
    actions[NH_PVP_HEAD_PRIMARY] = NH_PVP_PRIMARY_RETAIN;
    actions[NH_PVP_HEAD_EQUIP_SLOT(GEAR_SLOT_LEGS)] = NH_PVP_EQUIPMENT_UNEQUIP;
    CHECK("equipment-tab remove remains legal during pending cast", allowed(actor, NH_PVP_HEAD_EQUIP_SLOT(GEAR_SLOT_LEGS), NH_PVP_EQUIPMENT_UNEQUIP));
    step_case(actor, actions);
    CHECK("equipment-tab remove actually removes worn legs", player->equipped[GEAR_SLOT_LEGS] == ITEM_NONE);
    CHECK("equipment-tab remove preserves pending spell interaction", state.env.pvp_runtime.pending_manual_cast[actor] == pending && osrs_interaction_active(&player->interaction));
    memset(actions, 0, sizeof(actions));
    step_case(actor, actions);
    CHECK("removed-equipment cast launches when ready", player->just_attacked && player->attack_style_this_tick == ATTACK_STYLE_MAGIC && player->magic_type_this_tick == pending);
    CHECK("removed-equipment cast consumes continuation exactly once", state.env.pvp_runtime.pending_manual_cast[actor] == NH_PVP_PENDING_NONE && !osrs_interaction_active(&player->interaction));
    step_case(actor, actions);
    CHECK("equipment-tab remove does not create autocast", !player->just_attacked);
}

static void test_pending_safe_controls(int actor, int pid, int primary) {
    reset_case(actor, pid, ITEM_RUNE_CROSSBOW);
    Player* player = &state.env.players[actor];
    player->inventory_cells[0] = osrs_inventory_cell_from_item(ITEM_RUNE_PLATELEGS);
    player->has_attack_timer = 1;
    player->attack_timer = 1;
    int actions[NH_PVP_NUM_ACTION_HEADS] = {0};
    actions[NH_PVP_HEAD_PRIMARY] = primary;
    step_case(actor, actions);
    actions[NH_PVP_HEAD_PRIMARY] = NH_PVP_PRIMARY_RETAIN;
    actions[NH_PVP_HEAD_EQUIP_SLOT(GEAR_SLOT_LEGS)] = nh_pvp_equipment_action_for_item(GEAR_SLOT_LEGS, ITEM_RUNE_PLATELEGS);
    actions[NH_PVP_HEAD_OVERHEAD] = ENCOUNTER_OVERHEAD_SET_REFRESH_MAGIC;
    actions[NH_PVP_HEAD_OFFENSIVE] = ENCOUNTER_OFFENSIVE_SET_REFRESH_AUGURY;
    step_case(actor, actions);
    CHECK("worn identity request and prayer clicks preserve delayed cast", player->just_attacked && player->attack_style_this_tick == ATTACK_STYLE_MAGIC && player->magic_type_this_tick == (primary == NH_PVP_PRIMARY_ICE ? OSRS_MAGIC_ATTACK_ANCIENT_ICE : OSRS_MAGIC_ATTACK_ANCIENT_BLOOD));
    CHECK("safe control prayers apply at actual delayed launch", player->prayer == PRAYER_PROTECT_MAGIC && player->offensive_prayer == OFFENSIVE_PRAYER_AUGURY);
    CHECK("worn identity request produces no equipment swap", player->equipped[GEAR_SLOT_LEGS] == ITEM_RUNE_PLATELEGS && osrs_inventory_cell_item_index(&player->inventory_cells[0]) == ITEM_RUNE_PLATELEGS);

    reset_case(actor, pid, ITEM_RUNE_CROSSBOW);
    player = &state.env.players[actor];
    player->inventory_cells[0] = osrs_inventory_cell_from_item(ITEM_AHRIMS_ROBESKIRT);
    memset(actions, 0, sizeof(actions));
    actions[NH_PVP_HEAD_EQUIP_SLOT(GEAR_SLOT_LEGS)] = nh_pvp_equipment_action_for_item(GEAR_SLOT_LEGS, ITEM_AHRIMS_ROBESKIRT);
    actions[NH_PVP_HEAD_PRIMARY] = primary;
    step_case(actor, actions);
    CHECK("inventory equip then new spell request executes both", player->equipped[GEAR_SLOT_LEGS] == ITEM_AHRIMS_ROBESKIRT && player->just_attacked && player->attack_style_this_tick == ATTACK_STYLE_MAGIC);
}

static void test_raw_continuation(int actor, int pid) {
    reset_case(actor, pid, ITEM_RUNE_CROSSBOW);
    Player* player = &state.env.players[actor];
    player->has_attack_timer = 1;
    player->attack_timer = 1;
    int raw[OSRS_BASE_NUM_ACTION_HEADS] = {0};
    raw[OSRS_HEAD_PRIMARY] = OSRS_PRIMARY_MOVE_ACTIONS;
    raw[OSRS_HEAD_SPELL] = OSRS_SPELL_ICE_BARRAGE;
    int* destination = actor == 0 ? state.env.ocean_io.agent_actions : state.env.pvp_runtime.external_opponent_actions;
    memcpy(destination, raw, sizeof(raw));
    pvp_step(&state.env, context.route_topology, context.player_route_cache);
    CHECK("raw spell does not create NH pending state", state.env.pvp_runtime.pending_manual_cast[actor] == NH_PVP_PENDING_NONE && !player->just_attacked);
    memset(destination, 0, sizeof(raw));
    pvp_step(&state.env, context.route_topology, context.player_route_cache);
    CHECK("raw target retain preserves established ranged continuation", player->just_attacked && player->attack_style_this_tick == ATTACK_STYLE_RANGED);

    reset_case(actor, pid, ITEM_GRANITE_MAUL_ORNATE);
    player = &state.env.players[actor];
    state.env.pvp_runtime.maul[actor] = osrs_granite_maul_record_attack(state.env.pvp_runtime.maul[actor], state.env.tick, 1 - actor);
    memset(raw, 0, sizeof(raw));
    raw[OSRS_HEAD_SPELL] = OSRS_SPELL_ICE_BARRAGE;
    raw[OSRS_HEAD_SPECIAL] = 1;
    destination = actor == 0 ? state.env.ocean_io.agent_actions : state.env.pvp_runtime.external_opponent_actions;
    memcpy(destination, raw, sizeof(raw));
    pvp_step(&state.env, context.route_topology, context.player_route_cache);
    CHECK("unused raw spell head cannot block maul homing click", player->used_special_this_tick && player->special_energy == 50);
}

static void test_semantic_maul(int actor, int pid) {
    reset_case(actor, pid, ITEM_GRANITE_MAUL_ORNATE);
    Player* player = &state.env.players[actor];
    int actions[NH_PVP_NUM_ACTION_HEADS] = {0};
    actions[NH_PVP_HEAD_SPECIAL] = NH_PVP_SPECIAL_ARM;
    step_case(actor, actions);
    step_case(actor, actions);
    CHECK("semantic arm is idempotent", player->spec_armed && state.env.pvp_runtime.maul[actor].prepared_hits == 1);
    actions[NH_PVP_HEAD_SPECIAL] = NH_PVP_SPECIAL_DISARM;
    step_case(actor, actions);
    step_case(actor, actions);
    CHECK("semantic disarm clears all preparation", !player->spec_armed && state.env.pvp_runtime.maul[actor].prepared_hits == 0);
    actions[NH_PVP_HEAD_SPECIAL] = NH_PVP_SPECIAL_RETAIN;
    actions[NH_PVP_HEAD_PRIMARY] = NH_PVP_PRIMARY_WEAPON;
    step_case(actor, actions);
    CHECK("disarm then target performs normal attack", player->just_attacked && !player->used_special_this_tick && player->special_energy == 100);

    reset_case(actor, pid, ITEM_GRANITE_MAUL_ORNATE);
    player = &state.env.players[actor];
    memset(actions, 0, sizeof(actions));
    actions[NH_PVP_HEAD_SPECIAL] = NH_PVP_SPECIAL_PREPARE_DOUBLE;
    CHECK("legal ornate double preparation is unmasked", allowed(actor, NH_PVP_HEAD_SPECIAL, NH_PVP_SPECIAL_PREPARE_DOUBLE));
    step_case(actor, actions);
    int expiry = state.env.pvp_runtime.maul[actor].preparation_expires_tick;
    step_case(actor, actions);
    CHECK("double preparation is idempotent without expiry extension", state.env.pvp_runtime.maul[actor].prepared_hits == 2 && state.env.pvp_runtime.maul[actor].preparation_expires_tick == expiry);
    actions[NH_PVP_HEAD_SPECIAL] = NH_PVP_SPECIAL_RETAIN;
    actions[NH_PVP_HEAD_PRIMARY] = NH_PVP_PRIMARY_WEAPON;
    step_case(actor, actions);
    CHECK("explicit double preserves legal two special hits", player->used_special_this_tick && player->special_energy == 0);

    reset_case(actor, pid, ITEM_RUNE_CROSSBOW);
    player = &state.env.players[actor];
    player->inventory_cells[0] = osrs_inventory_cell_from_item(ITEM_GRANITE_MAUL_ORNATE);
    memset(actions, 0, sizeof(actions));
    actions[NH_PVP_HEAD_EQUIP_SLOT(GEAR_SLOT_WEAPON)] = nh_pvp_equipment_action_for_item(GEAR_SLOT_WEAPON, ITEM_GRANITE_MAUL_ORNATE);
    actions[NH_PVP_HEAD_SPECIAL] = NH_PVP_SPECIAL_PREPARE_DOUBLE;
    actions[NH_PVP_HEAD_PRIMARY] = NH_PVP_PRIMARY_WEAPON;
    CHECK("same tick maul equip double is unmasked", allowed(actor, NH_PVP_HEAD_SPECIAL, NH_PVP_SPECIAL_PREPARE_DOUBLE));
    step_case(actor, actions);
    CHECK("same tick equip double target executes", player->used_special_this_tick && player->special_energy == 0);

    reset_case(actor, pid, ITEM_GRANITE_MAUL_ORNATE);
    player = &state.env.players[actor];
    player->special_energy = 50;
    memset(actions, 0, sizeof(actions));
    actions[NH_PVP_HEAD_SPECIAL] = NH_PVP_SPECIAL_PREPARE_DOUBLE;
    CHECK("unfunded double is masked", !allowed(actor, NH_PVP_HEAD_SPECIAL, NH_PVP_SPECIAL_PREPARE_DOUBLE));
    step_case(actor, actions);
    CHECK("unfunded double cannot prepare or spend", state.env.pvp_runtime.maul[actor].prepared_hits == 0 && player->special_energy == 50);
}

int main(void) {
    assert(existing_actions_main() == 0);
    const EncounterArenaTopology* topology = context.route_topology;
    for (int x = topology->origin_x; x < topology->origin_x + topology->width; x++)
        for (int y = topology->origin_y; y < topology->origin_y + topology->height; y++)
            if (range_x == 0 && encounter_arena_topology_player_can_attack(topology, x, y, x + 12, y, 1, 12)) {
                range_x = x;
                range_y = y;
            }
    assert(range_x != 0);
    for (int actor = 0; actor < NUM_AGENTS; actor++)
        for (int pid = 0; pid < NUM_AGENTS; pid++) {
            test_pending_cast(actor, pid, NH_PVP_PRIMARY_ICE);
            test_pending_cast(actor, pid, NH_PVP_PRIMARY_BLOOD);
            test_pending_equipment_remove(actor, pid, NH_PVP_PRIMARY_ICE);
            test_pending_equipment_remove(actor, pid, NH_PVP_PRIMARY_BLOOD);
            test_pending_safe_controls(actor, pid, NH_PVP_PRIMARY_ICE);
            test_pending_safe_controls(actor, pid, NH_PVP_PRIMARY_BLOOD);
            test_semantic_maul(actor, pid);
            test_raw_continuation(actor, pid);
        }
    return osrs_test_summary();
}
