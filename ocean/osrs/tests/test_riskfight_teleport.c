#include <assert.h>
#include <stdio.h>
#include "../encounters/encounter_riskfight.h"

static RiskfightState state;
static RiskfightContext context;

static void reset(void) {
    riskfight_reset((EncounterState*)&state, (EncounterContext*)&context, 12345);
    context.self_play = 1;
    for (int i = 0; i < 2; i++) state.env.players[i].veng_active = 0;
}

static int teleport_slot(void) {
    for (int i = 0; i < OSRS_INVENTORY_SIZE; i++)
        if (osrs_inventory_cell_metadata(&state.env.players[0].inventory_cells[i])->click_action == OSRS_CLICK_TELEPORT)
            return i;
    assert(0);
    return -1;
}

static int mask_allows_teleport(void) {
    float mask[RF_MASK_SIZE];
    riskfight_write_action_mask(&state, 0, mask);
    int offset = 0;
    for (int i = 0; i < RF_PRIMARY; i++) offset += RF_ACTION_DIMS[i];
    return mask[offset + RF_TELEPORT];
}

static void command(HumanCommand value) {
    riskfight_execute_command(&state, &context, 0, &value);
}

static void equip(int slot) {
    command((HumanCommand){.kind = HUMAN_COMMAND_INVENTORY_PRIMARY_CLICK, .inventory_slot = slot});
}

static void spec(void) {
    command((HumanCommand){.kind = HUMAN_COMMAND_SPEC_TOGGLE});
    command((HumanCommand){.kind = HUMAN_COMMAND_ATTACK_NPC, .npc_slot = 1});
}

static void test_context_and_boundaries(void) {
    assert(osrs_teleport_combat_context(OSRS_TELEPORT_WORLD_STANDARD, 0) == OSRS_TELEPORT_COMBAT_UNRESTRICTED);
    assert(osrs_teleport_combat_context(OSRS_TELEPORT_WORLD_STANDARD, 1) == OSRS_TELEPORT_COMBAT_PVP_AREA);
    assert(osrs_teleport_combat_context(OSRS_TELEPORT_WORLD_PVP, 0) == OSRS_TELEPORT_COMBAT_PVP_AREA);
    OsrsTeleportState unlocked = {0};
    assert(osrs_teleport_record_offensive_pvp_special(unlocked, OSRS_TELEPORT_COMBAT_UNRESTRICTED, 30).blocked_until_tick == 0);
    OsrsTeleportState lock = osrs_teleport_record_offensive_pvp_special(unlocked, OSRS_TELEPORT_COMBAT_PVP_AREA, 30);
    assert(!osrs_teleport_allowed(lock, 30 + OSRS_PVP_SPECIAL_TELEPORT_LOCK_TICKS - 1));
    assert(osrs_teleport_allowed(lock, 30 + OSRS_PVP_SPECIAL_TELEPORT_LOCK_TICKS));
    OsrsTeleportState recorded = osrs_teleport_record_offensive_pvp_special(
        unlocked, OSRS_TELEPORT_COMBAT_PVP_AREA, 217);
    assert(!osrs_teleport_allowed(recorded, 225));
    assert(osrs_teleport_allowed(recorded, 226));
}

static void test_executed_special_and_consumption(void) {
    reset();
    int slot = teleport_slot();
    OsrsInventoryCell before = state.env.players[0].inventory_cells[slot];
    equip(24);
    state.env.tick = 30;
    spec();
    assert(state.env.players[0].special_energy == 50);
    assert(state.env.pvp_runtime.teleport[0].blocked_until_tick == 30 + OSRS_PVP_SPECIAL_TELEPORT_LOCK_TICKS);
    assert(!mask_allows_teleport());
    equip(slot);
    assert(!state.escaped[0]);
    assert(memcmp(&before, &state.env.players[0].inventory_cells[slot], sizeof(before)) == 0);
    state.env.tick = 31;
    spec();
    assert(state.env.players[0].special_energy == 0);
    assert(state.env.pvp_runtime.teleport[0].blocked_until_tick == 31 + OSRS_PVP_SPECIAL_TELEPORT_LOCK_TICKS);
    state.env.tick = 31 + OSRS_PVP_SPECIAL_TELEPORT_LOCK_TICKS - 1;
    assert(!mask_allows_teleport());
    equip(slot);
    assert(!state.escaped[0]);
    state.env.tick++;
    assert(mask_allows_teleport());
    equip(slot);
    assert(state.escaped[0]);
    assert(osrs_inventory_cell_is_empty(&state.env.players[0].inventory_cells[slot]));
    assert(!mask_allows_teleport());
}

static void test_non_triggering_actions(void) {
    reset();
    equip(24);
    command((HumanCommand){.kind = HUMAN_COMMAND_SPEC_TOGGLE});
    assert(state.env.pvp_runtime.teleport[0].blocked_until_tick == 0);
    state.env.players[0].special_energy = 0;
    command((HumanCommand){.kind = HUMAN_COMMAND_ATTACK_NPC, .npc_slot = 1});
    assert(state.env.pvp_runtime.teleport[0].blocked_until_tick == 0);

    reset();
    perform_attack(&state.env, 0, 1, ATTACK_STYLE_MELEE, 0, 0, 1);
    assert(state.env.pvp_runtime.teleport[0].blocked_until_tick == 0);
    reset();
    equip(23);
    state.env.players[0].special_energy = 0;
    perform_attack(&state.env, 0, 1, ATTACK_STYLE_MELEE, 1, 0, 1);
    assert(state.env.pvp_runtime.teleport[0].blocked_until_tick == 0);

    reset();
    equip(24);
    state.env.pvp_runtime.teleport_world = OSRS_TELEPORT_WORLD_STANDARD;
    state.env.players[0].x = 3200;
    state.env.players[0].y = 3200;
    state.env.players[1].x = 3201;
    state.env.players[1].y = 3200;
    spec();
    assert(state.env.players[0].special_energy == 50);
    assert(state.env.pvp_runtime.teleport[0].blocked_until_tick == 0);

    reset();
    equip(24);
    state.env.pvp_runtime.teleport_world = OSRS_TELEPORT_WORLD_STANDARD;
    state.env.players[0].x = WILD_MIN_X + 10;
    state.env.players[0].y = WILD_MIN_Y + 10;
    state.env.players[1].x = state.env.players[0].x + 1;
    state.env.players[1].y = state.env.players[0].y;
    spec();
    assert(state.env.pvp_runtime.teleport[0].blocked_until_tick == OSRS_PVP_SPECIAL_TELEPORT_LOCK_TICKS);
}

static void test_policy_and_human_blocked_teleport(void) {
    reset();
    equip(24);
    spec();
    RiskfightState saved = state;
    int actions[RF_HEADS] = {0};
    actions[RF_PRIMARY] = RF_TELEPORT;
    HumanInput input = {0};
    riskfight_policy_commands(&state, 0, actions, &input);
    for (int i = 0; i < input.commands.count; i++) command(input.commands.items[i]);
    assert(!state.escaped[0]);
    free(input.commands.items);
    OsrsInventoryCell policy_cell = state.env.players[0].inventory_cells[teleport_slot()];
    state = saved;
    equip(teleport_slot());
    assert(!state.escaped[0]);
    assert(memcmp(&policy_cell, &state.env.players[0].inventory_cells[teleport_slot()], sizeof(policy_cell)) == 0);
}

int main(void) {
    riskfight_init_context((EncounterContext*)&context);
    riskfight_finalize_context((EncounterState*)&state, (EncounterContext*)&context);
    test_context_and_boundaries();
    test_executed_special_and_consumption();
    test_non_triggering_actions();
    test_policy_and_human_blocked_teleport();
    riskfight_destroy_context((EncounterContext*)&context);
    puts("Riskfight teleport contracts passed");
}
