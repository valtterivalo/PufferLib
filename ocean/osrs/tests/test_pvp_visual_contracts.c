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

#define ASSERT_FLOAT_NEAR(label, actual, expected, eps) do { \
    tests_run++; \
    float _actual = (actual); \
    float _expected = (expected); \
    float _delta = _actual - _expected; \
    if (_delta < 0.0f) _delta = -_delta; \
    if (_delta <= (eps)) { \
        tests_passed++; \
    } else { \
        tests_failed++; \
        printf("  FAIL: %s got %.6f expected %.6f\n", (label), _actual, _expected); \
    } \
} while (0)

static CollisionMap* test_wilderness_collision_map(void) {
    static CollisionMap* cmap = NULL;
    if (cmap == NULL) {
        osrs_asset_require_group(OSRS_ASSET_GROUP_PVP);
        cmap = collision_map_load(OSRS_ASSET("wilderness.cmap"));
        if (cmap == NULL) {
            fprintf(stderr, "test setup: failed to load wilderness.cmap\n");
            abort();
        }
    }
    return cmap;
}

static void setup_pvp_state(NhPvpState* state) {
    memset(state, 0, sizeof(*state));
    pvp_init(&state->env);
    state->env.collision_map = test_wilderness_collision_map();
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

static void fill_pvp_entities_raw(NhPvpState* state, RenderEntity* entities, int* count) {
    nh_pvp_fill_render_entities(
        (EncounterState*)state, NULL, entities, NUM_AGENTS, count);
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

static void test_terminal_presentation_captures_loser_before_auto_reset(void) {
    printf("--- PvP terminal presentation captures loser before auto reset ---\n");

    NhPvpState state;
    setup_pvp_state(&state);
    state.env.pvp_runtime.opponent.type = OPP_NIGHTMARE_NH;
    clear_agent_actions(&state);
    state.env.players[1].current_hitpoints = 0;
    pvp_step(&state.env);

    PvpTerminalPresentation* p = &state.env.pvp_runtime.terminal_presentation;
    ASSERT_INT_EQ("presentation phase", p->phase, PVP_TERMINAL_PRESENTATION_DEATH);
    ASSERT_INT_EQ("presentation winner", p->winner, 0);
    ASSERT_INT_EQ("snapshot loser dead", p->players[1].current_hitpoints, 0);
    ASSERT_INT_EQ("live opponent reset", state.env.players[1].current_hitpoints,
        state.env.players[1].base_hitpoints);
    ASSERT_STR_EQ("snapshot opponent name", p->opponent_name, "Nightmare NH");
}

static void test_terminal_status_text_for_player_and_opponent_wins(void) {
    printf("--- PvP terminal status text reports winner ---\n");

    NhPvpState player_state;
    setup_pvp_state(&player_state);
    player_state.env.pvp_runtime.opponent.type = OPP_NIGHTMARE_NH;
    player_state.env.players[1].current_hitpoints = 0;
    pvp_step(&player_state.env);

    EncounterOverlay overlay = {0};
    nh_pvp_render_post_tick((EncounterState*)&player_state, NULL, &overlay);
    ASSERT_INT_EQ("player win status active", overlay.status_text_active, 1);
    ASSERT_STR_EQ("player win status text", overlay.status_text, "Player 0 won");

    NhPvpState opponent_state;
    setup_pvp_state(&opponent_state);
    opponent_state.env.pvp_runtime.opponent.type = OPP_NIGHTMARE_NH;
    opponent_state.env.players[0].current_hitpoints = 0;
    pvp_step(&opponent_state.env);

    memset(&overlay, 0, sizeof(overlay));
    nh_pvp_render_post_tick((EncounterState*)&opponent_state, NULL, &overlay);
    ASSERT_INT_EQ("opponent win status active", overlay.status_text_active, 1);
    ASSERT_STR_EQ("opponent win status text", overlay.status_text, "Nightmare NH won");
}

static void test_terminal_render_entities_use_snapshot(void) {
    printf("--- PvP terminal render entities use snapshot ---\n");

    NhPvpState state;
    setup_pvp_state(&state);
    state.env.pvp_runtime.opponent.type = OPP_NIGHTMARE_NH;
    state.env.players[1].current_hitpoints = 0;
    pvp_step(&state.env);

    RenderEntity entities[NUM_AGENTS];
    int count = 0;
    fill_pvp_entities_raw(&state, entities, &count);

    ASSERT_INT_EQ("death phase count", count, 2);
    ASSERT_INT_EQ("death phase loser hp", entities[1].current_hitpoints, 0);
    ASSERT_STR_EQ("death phase opponent name", entities[1].display_name, "Nightmare NH");
}

static void test_terminal_winner_phase_removes_loser(void) {
    printf("--- PvP terminal winner phase removes loser ---\n");

    NhPvpState state;
    setup_pvp_state(&state);
    state.env.pvp_runtime.opponent.type = OPP_NIGHTMARE_NH;
    state.env.players[0].current_hitpoints = 0;
    pvp_step(&state.env);
    state.env.pvp_runtime.terminal_presentation.phase =
        PVP_TERMINAL_PRESENTATION_WINNER;

    RenderEntity entities[NUM_AGENTS];
    int count = 0;
    fill_pvp_entities_raw(&state, entities, &count);
    Player* shown = nh_pvp_get_entity((EncounterState*)&state, NULL, 0);

    ASSERT_INT_EQ("winner phase count", count, 1);
    ASSERT_STR_EQ("winner phase name", entities[0].display_name, "Nightmare NH");
    ASSERT_INT_EQ("winner phase entity maps opponent", shown == NULL ? -1 : shown->current_hitpoints,
        state.env.pvp_runtime.terminal_presentation.players[1].current_hitpoints);
    ASSERT_INT_EQ("winner phase target inactive", entities[0].attack_target_entity_idx, -1);
}

static void test_performance_tracker_values(void) {
    printf("--- PvP performance tracker values ---\n");

    Player p0 = {0};
    Player p1 = {0};
    p0.total_damage_dealt = 61.0f;
    p1.total_damage_dealt = 48.5f;
    p0.expected_damage_dealt = 55.25f;
    p1.expected_damage_dealt = 62.75f;

    PvpPerformanceTrackerValues v = pvp_performance_tracker_values(&p0, &p1);

    ASSERT_FLOAT_NEAR("actual damage diff", v.damage_diff, 12.5f, 1e-6f);
    ASSERT_FLOAT_NEAR("expected damage diff", v.expected_damage_diff, -7.5f, 1e-6f);
    ASSERT_INT_EQ("expected leader", v.expected_leader, 1);
}

static void test_terminal_render_clear_resets_visual_state_source(void) {
    printf("--- PvP terminal render clear resets visual state source ---\n");

    FILE* f = fopen("ocean/osrs_pvp/binding.c", "rb");
    ASSERT_INT_EQ("binding source opens", f != NULL, 1);
    if (!f) return;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    rewind(f);
    char* text = (char*)malloc((size_t)n + 1);
    ASSERT_INT_EQ("binding source allocates", text != NULL, 1);
    if (!text) {
        fclose(f);
        return;
    }
    size_t read_n = fread(text, 1, (size_t)n, f);
    text[read_n] = '\0';
    fclose(f);

    ASSERT_INT_EQ("terminal clear present",
        strstr(text, "pvp_terminal_presentation_clear(&env->pvp)") != NULL, 1);
    ASSERT_INT_EQ("visual reset after terminal clear",
        strstr(text, "render_reset_episode_visual_state(rc, &env->pvp)") != NULL, 1);
    free(text);
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
    test_terminal_presentation_captures_loser_before_auto_reset();
    test_terminal_status_text_for_player_and_opponent_wins();
    test_terminal_render_entities_use_snapshot();
    test_terminal_winner_phase_removes_loser();
    test_performance_tracker_values();
    test_terminal_render_clear_resets_visual_state_source();

    printf("\n%d/%d tests passed\n", tests_passed, tests_run);
    return tests_failed == 0 ? 0 : 1;
}
