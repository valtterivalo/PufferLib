#include <sys/wait.h>
#include <unistd.h>
#include "ocean/osrs/encounters/encounter_nh_pvp.h"
#include "ocean/osrs/tests/osrs_test_check.h"

static NhPvpState state;
static NhPvpContext context;
static int origin_x, origin_y;

static void reset_case(int actor, int pid, uint8_t weapon) {
    nh_pvp_reset((EncounterState*)&state, (EncounterContext*)&context, 73);
    state.env.auto_reset = 0;
    state.env.pid_holder = pid;
    state.env.pvp_runtime.use_c_opponent = 0;
    state.env.pvp_runtime.use_c_opponent_p0 = 0;
    state.env.pvp_runtime.use_external_opponent_actions = 1;
    memset(state.external_actions, 0, sizeof(state.external_actions));
    for (int side = 0; side < NUM_AGENTS; side++) {
        Player* player = &state.env.players[side];
        player->x = origin_x + (side != actor);
        player->y = origin_y;
        player->dest_x = player->x;
        player->dest_y = player->y;
        player->equipped[GEAR_SLOT_WEAPON] = weapon;
        player->equipped[GEAR_SLOT_SHIELD] = ITEM_NONE;
        player->equipped[GEAR_SLOT_LEGS] = ITEM_RUNE_PLATELEGS;
        memset(player->inventory_cells, 0, sizeof(player->inventory_cells));
        player->is_lunar_spellbook = 0;
        player->current_magic = 99;
        player->current_prayer = 99;
        player->prayer = PRAYER_NONE;
        player->offensive_prayer = OFFENSIVE_PRAYER_NONE;
        player->attack_timer = 0;
        player->has_attack_timer = 0;
        player->special_energy = 100;
        player->spec_armed = 0;
        osrs_refresh_player_equipment(player);
        pvp_refresh_visible_gear(player);
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
    CHECK("semantic actor actions retained", memcmp(state.last_semantic_actions[actor], actions,
        NH_PVP_NUM_ACTION_HEADS * sizeof(int)) == 0);
}

static void test_casts_specials_and_thaw(int actor, int pid) {
    for (int primary = NH_PVP_PRIMARY_BLOOD; primary <= NH_PVP_PRIMARY_ICE; primary++) {
        reset_case(actor, pid, ITEM_RUNE_CROSSBOW);
        int actions[NH_PVP_NUM_ACTION_HEADS] = {0};
        actions[NH_PVP_HEAD_PRIMARY] = primary;
        CHECK("single primary cast is legal", allowed(actor, NH_PVP_HEAD_PRIMARY, primary));
        step_case(actor, actions);
        Player* player = &state.env.players[actor];
        CHECK("single primary launches magic", player->just_attacked && player->attack_style_this_tick == ATTACK_STYLE_MAGIC);
        ASSERT_INT_EQ("cast family matches primary", player->magic_type_this_tick,
            primary == NH_PVP_PRIMARY_BLOOD ? OSRS_MAGIC_ATTACK_ANCIENT_BLOOD : OSRS_MAGIC_ATTACK_ANCIENT_ICE);
    }
    reset_case(actor, pid, ITEM_RUNE_CROSSBOW);
    Player* player = &state.env.players[actor];
    player->inventory_cells[0] = osrs_inventory_cell_from_item(ITEM_AGS);
    int actions[NH_PVP_NUM_ACTION_HEADS] = {0};
    actions[NH_PVP_HEAD_PRIMARY] = NH_PVP_PRIMARY_WEAPON;
    actions[NH_PVP_HEAD_EQUIP_SLOT(GEAR_SLOT_WEAPON)] = nh_pvp_equipment_action_for_item(GEAR_SLOT_WEAPON, ITEM_AGS);
    actions[NH_PVP_HEAD_SPECIAL] = 1;
    CHECK("post-switch special is unmasked", allowed(actor, NH_PVP_HEAD_SPECIAL, 1));
    step_case(actor, actions);
    CHECK("post-switch special actually launches", player->just_attacked && player->used_special_this_tick);
    ASSERT_INT_EQ("special pays actual weapon cost", player->special_energy, 50);
    reset_case(actor, pid, ITEM_AGS);
    player = &state.env.players[actor];
    player->inventory_cells[0] = osrs_inventory_cell_from_item(ITEM_RUNE_CROSSBOW);
    memset(actions, 0, sizeof(actions));
    actions[NH_PVP_HEAD_PRIMARY] = NH_PVP_PRIMARY_WEAPON;
    actions[NH_PVP_HEAD_EQUIP_SLOT(GEAR_SLOT_WEAPON)] = nh_pvp_equipment_action_for_item(GEAR_SLOT_WEAPON, ITEM_RUNE_CROSSBOW);
    actions[NH_PVP_HEAD_SPECIAL] = 1;
    step_case(actor, actions);
    CHECK("nonspecial post-switch weapon does not consume energy", !player->used_special_this_tick && player->special_energy == 100);
    reset_case(actor, pid, ITEM_WHIP);
    player = &state.env.players[actor];
    player->frozen_ticks = 1;
    memset(actions, 0, sizeof(actions));
    for (int move = 1; move < OSRS_PRIMARY_MOVE_ACTIONS; move++) {
        if (ENCOUNTER_MOVE_TARGET_DX[move] == 0 && ENCOUNTER_MOVE_TARGET_DY[move] == 1) actions[0] = move;
    }
    CHECK("thaw movement uses next clock", allowed(actor, NH_PVP_HEAD_PRIMARY, actions[0]));
    step_case(actor, actions);
    ASSERT_INT_EQ("thaw move executes", player->y, origin_y + 1);
}

static void test_equipment_and_consumables(int actor, int pid) {
    reset_case(actor, pid, ITEM_WHIP);
    Player* player = &state.env.players[actor];
    player->inventory_cells[6] = osrs_inventory_cell_from_item(ITEM_AHRIMS_ROBESKIRT);
    int actions[NH_PVP_NUM_ACTION_HEADS] = {0};
    actions[NH_PVP_HEAD_EQUIP_SLOT(GEAR_SLOT_LEGS)] = nh_pvp_equipment_action_for_item(GEAR_SLOT_LEGS, ITEM_AHRIMS_ROBESKIRT);
    step_case(actor, actions);
    ASSERT_INT_EQ("identity request equips selected legs", player->equipped[GEAR_SLOT_LEGS], ITEM_AHRIMS_ROBESKIRT);
    step_case(actor, actions);
    ASSERT_INT_EQ("repeated identity cannot toggle displaced legs", player->equipped[GEAR_SLOT_LEGS], ITEM_AHRIMS_ROBESKIRT);
    ASSERT_INT_EQ("displaced legs remain in original cell", osrs_inventory_cell_item_index(&player->inventory_cells[6]), ITEM_RUNE_PLATELEGS);
    player->inventory_cells[7] = osrs_inventory_cell_from_item(ITEM_AHRIMS_ROBESKIRT);
    int before = player->clicks_this_tick;
    int engine[OSRS_BASE_NUM_ACTION_HEADS] = {0};
    execute_nh_switches(&state.env, actor, actions, engine);
    ASSERT_INT_EQ("duplicate worn identity is strict no-op", player->clicks_this_tick, before);
    actions[NH_PVP_HEAD_EQUIP_SLOT(GEAR_SLOT_LEGS)] = NH_PVP_EQUIPMENT_UNEQUIP;
    CHECK("explicit unequip is legal", allowed(actor, NH_PVP_HEAD_EQUIP_SLOT(GEAR_SLOT_LEGS), NH_PVP_EQUIPMENT_UNEQUIP));
    step_case(actor, actions);
    ASSERT_INT_EQ("unequip removes chosen slot", player->equipped[GEAR_SLOT_LEGS], ITEM_NONE);
    reset_case(actor, pid, ITEM_WHIP);
    player = &state.env.players[actor];
    player->current_hitpoints = 30;
    player->inventory_cells[0] = osrs_inventory_cell_from_raw_osrs_id(385);
    player->inventory_cells[1] = osrs_inventory_cell_from_raw_osrs_id(6685);
    player->inventory_cells[2] = osrs_inventory_cell_from_raw_osrs_id(3144);
    player->food_count = 1;
    player->karambwan_count = 1;
    player->brew_doses = 4;
    memset(actions, 0, sizeof(actions));
    actions[NH_PVP_HEAD_FOOD] = NH_PVP_FOOD_SHARK;
    actions[NH_PVP_HEAD_DRINK] = NH_PVP_DRINK_BREW;
    actions[NH_PVP_HEAD_KARAMBWAN] = NH_PVP_KARAMBWAN_KARAMBWAN;
    player->food_timer = player->potion_timer = player->karambwan_timer = 1;
    CHECK("food timer one remains masked", !allowed(actor, NH_PVP_HEAD_FOOD, NH_PVP_FOOD_SHARK));
    CHECK("potion timer one remains masked", !allowed(actor, NH_PVP_HEAD_DRINK, NH_PVP_DRINK_BREW));
    CHECK("karambwan timer one remains masked", !allowed(actor, NH_PVP_HEAD_KARAMBWAN, NH_PVP_KARAMBWAN_KARAMBWAN));
    step_case(actor, actions);
    CHECK("consumable timer one cannot execute before clock", !player->ate_food_this_tick && !player->ate_brew_this_tick && !player->ate_karambwan_this_tick);
    CHECK("timer one reaches zero after switches", player->food_timer == 0 && player->potion_timer == 0 && player->karambwan_timer == 0);
    step_case(actor, actions);
    CHECK("food potion karambwan all execute", player->ate_food_this_tick && player->ate_brew_this_tick && player->ate_karambwan_this_tick);
    CHECK("triple eating pays actual supplies", player->food_count == 0 && player->karambwan_count == 0 && player->brew_doses == 3);
    CHECK("triple eating consumes actual cells", osrs_inventory_cell_is_empty(&player->inventory_cells[0]) && osrs_inventory_cell_is_empty(&player->inventory_cells[2]) && osrs_inventory_cell_dose_count(&player->inventory_cells[1]) == 3);
    CHECK("ready eating leaves attack readiness intact", !player->has_attack_timer && player->attack_timer == 0);
    reset_case(actor, pid, ITEM_WHIP);
    player = &state.env.players[actor];
    player->current_hitpoints = 30;
    player->food_count = 1;
    player->inventory_cells[0] = osrs_inventory_cell_from_raw_osrs_id(7218);
    memset(actions, 0, sizeof(actions));
    actions[NH_PVP_HEAD_FOOD] = NH_PVP_FOOD_SUMMER_PIE;
    CHECK("full summer pie is mask eligible", allowed(actor, NH_PVP_HEAD_FOOD, NH_PVP_FOOD_SUMMER_PIE));
    step_case(actor, actions);
    ASSERT_INT_EQ("first pie bite preserves half pie", osrs_inventory_cell_raw_osrs_id(&player->inventory_cells[0]), 7220);
    ASSERT_INT_EQ("half pie remains one food item", player->food_count, 1);
    ASSERT_INT_EQ("first pie bite heals eleven", player->current_hitpoints, 41);
    CHECK("half pie remains mask eligible", allowed(actor, NH_PVP_HEAD_FOOD, NH_PVP_FOOD_SUMMER_PIE));
    step_case(actor, actions);
    ASSERT_INT_EQ("second pie bite leaves canonical dish", osrs_inventory_cell_metadata(&player->inventory_cells[0])->consumable_kind, OSRS_CONSUMABLE_PIE_DISH);
    ASSERT_INT_EQ("finished pie exhausts food count", player->food_count, 0);
    ASSERT_INT_EQ("second pie bite heals eleven", player->current_hitpoints, 52);
    CHECK("pie dish cannot be eaten", !allowed(actor, NH_PVP_HEAD_FOOD, NH_PVP_FOOD_SUMMER_PIE));

    reset_case(actor, pid, ITEM_WHIP);
    player = &state.env.players[actor];
    player->current_hitpoints = 30;
    player->food_count = 1;
    player->inventory_cells[0] = osrs_inventory_cell_from_raw_osrs_id(7218);
    int raw[OSRS_BASE_NUM_ACTION_HEADS] = {0};
    raw[OSRS_HEAD_EAT] = 1;
    int* raw_destination = actor == 0 ? state.env.ocean_io.agent_actions : state.env.pvp_runtime.external_opponent_actions;
    memcpy(raw_destination, raw, sizeof(raw));
    pvp_step(&state.env, context.route_topology, context.player_route_cache);
    ASSERT_INT_EQ("raw pie first bite preserves half", osrs_inventory_cell_raw_osrs_id(&player->inventory_cells[0]), 7220);
    ASSERT_INT_EQ("raw pie first bite keeps supply count", player->food_count, 1);
    ASSERT_INT_EQ("raw pie first bite heals eleven", player->current_hitpoints, 41);
    pvp_step(&state.env, context.route_topology, context.player_route_cache);
    ASSERT_INT_EQ("raw pie second bite preserves dish", osrs_inventory_cell_metadata(&player->inventory_cells[0])->consumable_kind, OSRS_CONSUMABLE_PIE_DISH);
    ASSERT_INT_EQ("raw pie second bite exhausts supply", player->food_count, 0);
    ASSERT_INT_EQ("raw pie second bite heals eleven", player->current_hitpoints, 52);

}

static void test_schema_and_human_input(void) {
    int covered[NUM_ITEMS] = {0};
    int logits = 0;
    for (int head = 0; head < NH_PVP_NUM_ACTION_HEADS; head++) logits += NH_PVP_ACTION_DIMS[head];
    ASSERT_INT_EQ("NH mask matches all semantic heads", logits, NH_PVP_ACTION_MASK_SIZE);
    for (int slot = 0; slot < NUM_GEAR_SLOTS; slot++) {
        int count = NH_PVP_ACTION_DIMS[NH_PVP_HEAD_EQUIP_SLOT(slot)] - NH_PVP_EQUIPMENT_ITEM_BASE;
        for (int index = 0; index < count; index++) {
            uint8_t item = NH_PVP_EQUIPMENT_ITEMS[slot][index];
            covered[item]++;
            ASSERT_INT_EQ("catalogue slot agrees with shared item metadata", osrs_item_gear_slot(item), slot);
            ASSERT_INT_EQ("catalogue identity roundtrips", nh_pvp_equipment_action_for_item(slot, item), index + NH_PVP_EQUIPMENT_ITEM_BASE);
        }
    }
    for (int item = 0; item < NUM_ITEMS; item++) ASSERT_INT_EQ("each shared equipment item has one canonical intent", covered[item], 1);
    reset_case(0, 0, ITEM_WHIP);
    HumanInput human = {0};
    human.pending_move_x = human.pending_move_y = -1;
    human.pending_food = 1;
    human.pending_karambwan = 1;
    human.pending_potion = POTION_BREW;
    human.pending_attack = 1;
    human.pending_spell = PVP_ATTACK_ICE;
    human_input_queue_unequip(&human, GEAR_SLOT_LEGS);
    int actions[NH_PVP_NUM_ACTION_HEADS];
    human_to_nh_pvp_actions(&human, actions, &state.env.players[0]);
    CHECK("human adapter preserves triple eat intentions", actions[NH_PVP_HEAD_FOOD] == NH_PVP_FOOD_SHARK &&
        actions[NH_PVP_HEAD_DRINK] == NH_PVP_DRINK_BREW && actions[NH_PVP_HEAD_KARAMBWAN] == NH_PVP_KARAMBWAN_KARAMBWAN);
    CHECK("human adapter expresses cast and unequip", actions[NH_PVP_HEAD_PRIMARY] == NH_PVP_PRIMARY_ICE &&
        actions[NH_PVP_HEAD_EQUIP_SLOT(GEAR_SLOT_LEGS)] == NH_PVP_EQUIPMENT_UNEQUIP);
    for (int head = 0; head < NH_PVP_NUM_ACTION_HEADS; head++) {
        fflush(NULL);
        pid_t child = fork();
        assert(child >= 0);
        if (child == 0) {
            int malformed[NH_PVP_NUM_ACTION_HEADS] = {0};
            int engine[OSRS_BASE_NUM_ACTION_HEADS];
            malformed[head] = NH_PVP_ACTION_DIMS[head];
            nh_pvp_decode_control_actions(malformed, engine);
            _exit(0);
        }
        int status;
        assert(waitpid(child, &status, 0) == child);
        CHECK("out-of-domain semantic head rejects", WIFSIGNALED(status) && WTERMSIG(status) == SIGABRT);
    }
}

int main(void) {
    nh_pvp_init_state((EncounterState*)&state, NULL);
    nh_pvp_init_context((EncounterContext*)&context);
    state.env.pvp_runtime.use_c_opponent = 0;
    nh_pvp_finalize_context((EncounterState*)&state, (EncounterContext*)&context);
    const EncounterArenaTopology* topology = context.route_topology;
    for (int x = topology->origin_x; x < topology->origin_x + topology->width; x++)
        for (int y = topology->origin_y; y < topology->origin_y + topology->height; y++)
            if (origin_x == 0 && encounter_arena_topology_player_can_attack(topology, x, y, x + 1, y, 1, 1) &&
                encounter_arena_topology_step_allowed(topology, x, y, 1, 0, 1) &&
                encounter_arena_topology_step_allowed(topology, x + 1, y, 1, 0, 1)) {
                origin_x = x;
                origin_y = y;
            }
    assert(origin_x != 0);
    test_schema_and_human_input();
    for (int actor = 0; actor < NUM_AGENTS; actor++)
        for (int pid = 0; pid < NUM_AGENTS; pid++) {
            test_casts_specials_and_thaw(actor, pid);
            test_equipment_and_consumables(actor, pid);
        }
    return osrs_test_summary();
}
