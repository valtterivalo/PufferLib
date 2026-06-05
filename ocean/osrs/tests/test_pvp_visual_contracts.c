/**
 * @file test_pvp_visual_contracts.c
 * @brief regression tests for PvP render targets, labels, and inventory projection.
 *
 * BUILD:
 *   cc -std=c11 -O0 -g -I. -o /tmp/test_pvp_visual_contracts \
 *       ocean/osrs/tests/test_pvp_visual_contracts.c -lm
 *   /tmp/test_pvp_visual_contracts
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ocean/osrs/osrs_env.h"
#include "ocean/osrs/encounters/encounter_nh_pvp.h"
#include "ocean/osrs/osrs_gui.h"

static int tests_run = 0;
static int tests_passed = 0;
static int tests_failed = 0;

#define ASSERT_INT_EQ(label, actual, expected) do { \
    tests_run++; \
    if ((actual) == (expected)) { \
        tests_passed++; \
    } else { \
        tests_failed++; \
        printf("  FAIL: %s got %d expected %d\n", (label), (actual), (expected)); \
    } \
} while (0)

#define ASSERT_STR_EQ(label, actual, expected) do { \
    tests_run++; \
    if (strcmp((actual), (expected)) == 0) { \
        tests_passed++; \
    } else { \
        tests_failed++; \
        printf("  FAIL: %s got '%s' expected '%s'\n", (label), (actual), (expected)); \
    } \
} while (0)

static void setup_pvp_state(NhPvpState* state) {
    memset(state, 0, sizeof(*state));
    pvp_init(&state->env);
    state->env.ocean_io.agent_obs = state->env._obs_buf;
    state->env.ocean_io.agent_actions = state->env._acts_buf;
    state->env.ocean_io.agent_rewards = state->env._rews_buf;
    state->env.ocean_io.agent_terminals = state->env._terms_buf;
    pvp_seed(&state->env, 73);
    pvp_reset(&state->env);
}

static void fill_pvp_entities(NhPvpState* state, RenderEntity* entities, int* count) {
    nh_pvp_fill_render_entities(
        (EncounterState*)state, NULL, entities, NUM_AGENTS, count);
    ASSERT_INT_EQ("render entity count", *count, NUM_AGENTS);
}

static void set_agent_actions(NhPvpState* state, const int* actions) {
    memcpy(state->env.ocean_io.agent_actions, actions,
        NUM_ACTION_HEADS * sizeof(int));
}

static void clear_agent_actions(NhPvpState* state) {
    memset(state->env.ocean_io.agent_actions, 0,
        NUM_ACTION_HEADS * sizeof(int));
}

static uint8_t first_weapon_switch(Player* player) {
    uint8_t equipped = player->equipped[GEAR_SLOT_WEAPON];
    for (int i = 0; i < player->num_items_in_slot[GEAR_SLOT_WEAPON]; i++) {
        uint8_t item = player->inventory[GEAR_SLOT_WEAPON][i];
        if (item != ITEM_NONE && item != equipped) return item;
    }
    fprintf(stderr, "test setup: player has no weapon switch\n");
    abort();
}

static void assert_player_item_sprites_exist(const Player* player) {
    for (int slot = 0; slot < NUM_GEAR_SLOTS; slot++) {
        for (int i = 0; i < player->num_items_in_slot[slot]; i++) {
            uint8_t item = player->inventory[slot][i];
            if (item == ITEM_NONE) continue;
            char path[128];
            snprintf(path, sizeof(path), "sprites/items/%d.png",
                ITEM_DATABASE[item].item_id);
            ASSERT_INT_EQ("PvP item sprite exists", osrs_asset_exists(path), 1);
        }
    }
}

static void test_reset_has_no_forced_targets(void) {
    printf("--- PvP reset has no forced render targets ---\n");

    NhPvpState state;
    setup_pvp_state(&state);

    RenderEntity entities[NUM_AGENTS];
    int count = 0;
    fill_pvp_entities(&state, entities, &count);

    ASSERT_INT_EQ("agent render target inactive",
        entities[0].attack_target_entity_idx, -1);
    ASSERT_INT_EQ("opponent render target inactive",
        entities[1].attack_target_entity_idx, -1);
}

static void test_attack_sets_render_target(void) {
    printf("--- PvP attack sets render target ---\n");

    NhPvpState state;
    setup_pvp_state(&state);

    int actions[NUM_ACTION_HEADS] = {0};
    actions[HEAD_LOADOUT] = LOADOUT_MELEE;
    actions[HEAD_COMBAT] = ATTACK_ATK;
    set_agent_actions(&state, actions);
    pvp_step(&state.env);

    RenderEntity entities[NUM_AGENTS];
    int count = 0;
    fill_pvp_entities(&state, entities, &count);

    ASSERT_INT_EQ("agent render target follows interaction",
        entities[0].attack_target_entity_idx, 1);
}

static void test_explicit_move_clears_render_target(void) {
    printf("--- PvP explicit move clears render target ---\n");

    NhPvpState state;
    setup_pvp_state(&state);
    osrs_interaction_set(&state.env.players[0].interaction, 1);

    int actions[NUM_ACTION_HEADS] = {0};
    actions[HEAD_MOVE] = 7;
    set_agent_actions(&state, actions);
    pvp_step(&state.env);

    RenderEntity entities[NUM_AGENTS];
    int count = 0;
    fill_pvp_entities(&state, entities, &count);

    ASSERT_INT_EQ("agent render target cleared after move",
        entities[0].attack_target_entity_idx, -1);
    ASSERT_INT_EQ("moving untargeted entity faces movement",
        render_entity_select_facing_mode(&entities[0], 1),
        RENDER_ENTITY_FACE_MOVEMENT);
}

static void test_opponent_target_is_independent(void) {
    printf("--- PvP opponent render target is independent ---\n");

    NhPvpState state;
    setup_pvp_state(&state);
    osrs_interaction_set(&state.env.players[1].interaction, 0);

    RenderEntity entities[NUM_AGENTS];
    int count = 0;
    fill_pvp_entities(&state, entities, &count);

    ASSERT_INT_EQ("agent render target remains inactive",
        entities[0].attack_target_entity_idx, -1);
    ASSERT_INT_EQ("opponent render target follows own interaction",
        entities[1].attack_target_entity_idx, 0);
}

static void test_inventory_projection_matches_player(void) {
    printf("--- PvP inventory projection matches slot inventory ---\n");

    NhPvpState state;
    setup_pvp_state(&state);
    Player* player = &state.env.players[0];

    GuiState gui;
    memset(&gui, 0, sizeof(gui));
    gui.gui_entity_idx = 0;
    gui_reset_inventory_ui_state(&gui);
    gui_populate_inventory(&gui, player);

    ASSERT_INT_EQ("initial inventory projection",
        gui_inventory_projection_matches_player(&gui, player), 1);

    uint8_t previous_weapon = player->equipped[GEAR_SLOT_WEAPON];
    uint8_t next_weapon = first_weapon_switch(player);
    slot_equip_item(player, GEAR_SLOT_WEAPON, next_weapon);
    gui_update_inventory(&gui, player);

    ASSERT_INT_EQ("switched inventory projection",
        gui_inventory_projection_matches_player(&gui, player), 1);
    ASSERT_INT_EQ("old weapon visible after switch",
        gui_inventory_equipment_count(&gui, previous_weapon), 1);
    ASSERT_INT_EQ("new weapon hidden while equipped",
        gui_inventory_equipment_count(&gui, next_weapon), 0);
}

static void test_inventory_cycle_marks_rebuild(void) {
    printf("--- PvP inventory entity cycle marks rebuild ---\n");

    NhPvpState state;
    setup_pvp_state(&state);

    GuiState gui;
    memset(&gui, 0, sizeof(gui));
    gui.gui_entity_idx = 0;
    gui.gui_entity_count = 2;
    gui_reset_inventory_ui_state(&gui);
    gui_populate_inventory(&gui, &state.env.players[0]);
    gui.inv_grid_dirty = 0;

    gui_cycle_entity(&gui);
    ASSERT_INT_EQ("cycle marks inventory dirty", gui.inv_grid_dirty, 1);
    gui_populate_inventory(&gui, &state.env.players[1]);
    gui.inv_grid_dirty = 0;
    ASSERT_INT_EQ("cycled inventory projection",
        gui_inventory_projection_matches_player(&gui, &state.env.players[1]), 1);
}

static void test_pvp_item_sprites_exist(void) {
    printf("--- PvP item sprites exist ---\n");

    NhPvpState state;
    setup_pvp_state(&state);
    assert_player_item_sprites_exist(&state.env.players[0]);
    assert_player_item_sprites_exist(&state.env.players[1]);
}

static void test_pvp_display_names_and_label_target(void) {
    printf("--- PvP display names and target labels ---\n");

    NhPvpState state;
    setup_pvp_state(&state);
    state.env.pvp_runtime.opponent.type = OPP_NIGHTMARE_NH;

    RenderEntity entities[NUM_AGENTS];
    int count = 0;
    fill_pvp_entities(&state, entities, &count);

    ASSERT_STR_EQ("agent display name", entities[0].display_name, "Agent");
    ASSERT_STR_EQ("opponent display name", entities[1].display_name, "Nightmare NH");
    ASSERT_INT_EQ("watching opponent labels agent",
        render_target_label_entity_idx_from_entities(entities, count, 1), 0);
}

int main(void) {
    test_reset_has_no_forced_targets();
    test_attack_sets_render_target();
    test_explicit_move_clears_render_target();
    test_opponent_target_is_independent();
    test_inventory_projection_matches_player();
    test_inventory_cycle_marks_rebuild();
    test_pvp_item_sprites_exist();
    test_pvp_display_names_and_label_target();

    printf("\n%d/%d tests passed\n", tests_passed, tests_run);
    return tests_failed == 0 ? 0 : 1;
}
