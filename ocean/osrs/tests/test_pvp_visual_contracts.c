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
#include "ocean/osrs/osrs_anim.h"
#include "ocean/osrs/osrs_gui.h"
#include "ocean/osrs/osrs_models.h"
#include "ocean/osrs/osrs_pvp_effects.h"
#include "ocean/osrs/osrs_spotanims.h"

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

static AnimCache* test_equipment_animation_cache(void) {
    static AnimCache* cache = NULL;
    if (cache == NULL) {
        cache = anim_cache_load("ocean/osrs/data/equipment.anims");
        if (cache == NULL) {
            fprintf(stderr, "test setup: failed to load equipment.anims\n");
            abort();
        }
    }
    return cache;
}

static OsrsSpotAnimSet* test_spotanim_set(void) {
    static OsrsSpotAnimSet* set = NULL;
    if (set == NULL) {
        set = osrs_spotanims_load("ocean/osrs/data/spotanims.bin");
        if (set == NULL) {
            fprintf(stderr, "test setup: failed to load spotanims.bin\n");
            abort();
        }
    }
    return set;
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

static void test_wilderness_collision_asset_spans_pvp_area(void) {
    printf("--- PvP wilderness collision asset spans full area ---\n");

    CollisionMap* cmap = test_wilderness_collision_map();
    ASSERT_INT_EQ("expanded wilderness region count", cmap->count >= 100, 1);
    ASSERT_INT_EQ("southwest wilderness region loaded",
        collision_map_get(cmap,
            collision_region_hash(WILD_MIN_X, WILD_MIN_Y)) != NULL, 1);
    ASSERT_INT_EQ("northeast wilderness region loaded",
        collision_map_get(cmap,
            collision_region_hash(WILD_MAX_X - 1, WILD_MAX_Y - 1)) != NULL, 1);
}

static void test_runtime_animation_assets_are_anm2(void) {
    printf("--- runtime animation assets are ANM2 ---\n");

    const char* paths[] = {
        "ocean/osrs/data/equipment.anims",
        "ocean/osrs/data/zulrah.anims",
        "ocean/osrs/data/inferno.anims",
        "ocean/osrs/data/inferno_npcs.anims",
    };
    for (size_t i = 0; i < sizeof(paths) / sizeof(paths[0]); i++) {
        FILE* f = fopen(paths[i], "rb");
        ASSERT_INT_EQ("animation asset opens", f != NULL, 1);
        if (!f) continue;
        unsigned char magic[4] = {0};
        size_t n = fread(magic, 1, sizeof(magic), f);
        fclose(f);
        ASSERT_INT_EQ("animation asset has ANM2 header",
            n == 4 &&
            magic[0] == 'A' && magic[1] == 'N' &&
            magic[2] == 'M' && magic[3] == '2',
            1);
    }
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

static void test_shared_render_target_refs_resolve(void) {
    printf("--- shared render target refs resolve ---\n");

    RenderEntity entities[3] = {0};
    entities[0].entity_type = ENTITY_PLAYER;
    entities[0].player_slot = 0;
    entities[0].attack_target_entity_idx = -1;
    entities[1].entity_type = ENTITY_PLAYER;
    entities[1].player_slot = 1;
    entities[1].attack_target_entity_idx = -1;
    entities[2].entity_type = ENTITY_NPC;
    entities[2].player_slot = -1;
    entities[2].npc_slot = 7;
    entities[2].attack_target_entity_idx = -1;

    ASSERT_INT_EQ("none ref resolves inactive",
        osrs_render_target_ref_resolve_entity_idx(
            entities, 3, osrs_render_target_none()),
        -1);
    ASSERT_INT_EQ("player-slot ref resolves player",
        osrs_render_target_ref_resolve_entity_idx(
            entities, 3, osrs_render_target_player_slot(1)),
        1);
    ASSERT_INT_EQ("npc-slot ref resolves npc",
        osrs_render_target_ref_resolve_entity_idx(
            entities, 3, osrs_render_target_npc_slot(7)),
        2);
    ASSERT_INT_EQ("entity-index ref resolves direct index",
        osrs_render_target_ref_resolve_entity_idx(
            entities, 3, osrs_render_target_entity_index(0)),
        0);

    osrs_render_entity_set_preferred_attack_target_ref(
        entities,
        3,
        0,
        osrs_render_target_player_slot(1),
        osrs_render_target_npc_slot(7));
    ASSERT_INT_EQ("preferred target uses primary",
        entities[0].attack_target_entity_idx, 1);

    osrs_render_entity_set_preferred_attack_target_ref(
        entities,
        3,
        0,
        osrs_render_target_player_slot(8),
        osrs_render_target_npc_slot(7));
    ASSERT_INT_EQ("preferred target falls back",
        entities[0].attack_target_entity_idx, 2);
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
    for (int i = 0; i < OSRS_INVENTORY_SIZE; i++) {
        uint8_t item = player->inventory[i];
        if (item != ITEM_NONE && osrs_item_gear_slot(item) != GEAR_SLOT_WEAPON)
            continue;
        if (item != ITEM_NONE && item != equipped) return item;
    }
    fprintf(stderr, "test setup: player has no weapon switch\n");
    abort();
}

static void assert_player_item_sprites_exist(const Player* player) {
    for (int i = 0; i < OSRS_INVENTORY_SIZE; i++) {
        uint8_t item = player->inventory[i];
        if (item == ITEM_NONE) continue;
        char path[128];
        snprintf(path, sizeof(path), "sprites/items/%d.png",
            ITEM_DATABASE[item].item_id);
        ASSERT_INT_EQ("PvP item sprite exists", osrs_asset_exists(path), 1);
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

static void test_attack_event_faces_target_after_interaction_clear(void) {
    printf("--- PvP attack event faces target after interaction clear ---\n");

    NhPvpState state;
    setup_pvp_state(&state);
    Player* attacker = &state.env.players[0];
    Player* defender = &state.env.players[1];

    attacker->x = 3041;
    attacker->y = 3530;
    attacker->dest_x = attacker->x;
    attacker->dest_y = attacker->y;
    defender->x = 3042;
    defender->y = 3530;
    defender->dest_x = defender->x;
    defender->dest_y = defender->y;

    perform_attack(&state.env, 0, 1, ATTACK_STYLE_MELEE, 0, 0, 1);
    osrs_interaction_clear(&attacker->interaction);

    RenderEntity entities[NUM_AGENTS];
    int count = 0;
    fill_pvp_entities(&state, entities, &count);

    ASSERT_INT_EQ("attack event target resolves player slot",
        entities[0].attack_target_entity_idx, 1);
    ASSERT_INT_EQ("attack event faces target",
        render_entity_select_facing_mode(&entities[0], 0),
        RENDER_ENTITY_FACE_ATTACK_TARGET);
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
    printf("--- PvP inventory projection matches flat inventory ---\n");

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
    osrs_player_equip_inventory_item(player, next_weapon);
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

static void test_flat_inventory_equipment_swaps_clicked_slot(void) {
    printf("--- flat inventory equipment swaps clicked slot ---\n");

    NhPvpState state;
    setup_pvp_state(&state);
    Player* player = &state.env.players[0];
    uint8_t old_weapon = player->equipped[GEAR_SLOT_WEAPON];
    uint8_t next_weapon = first_weapon_switch(player);
    int slot = osrs_player_inventory_find(player, next_weapon);

    ASSERT_INT_EQ("switch weapon is in flat bag", slot >= 0, 1);
    ASSERT_INT_EQ("equip from slot succeeds",
        osrs_player_equip_from_inventory_slot(player, slot), 1);
    ASSERT_INT_EQ("new weapon equipped",
        player->equipped[GEAR_SLOT_WEAPON], next_weapon);
    ASSERT_INT_EQ("old weapon returns to clicked slot",
        player->inventory[slot], old_weapon);
}

static void test_flat_inventory_two_handed_weapon_moves_shield(void) {
    printf("--- flat inventory two-handed weapon moves shield ---\n");

    NhPvpState state;
    setup_pvp_state(&state);
    Player* player = &state.env.players[0];
    uint8_t old_weapon = player->equipped[GEAR_SLOT_WEAPON];
    uint8_t old_shield = player->equipped[GEAR_SLOT_SHIELD];
    int slot = osrs_player_inventory_add(player, ITEM_AGS);

    ASSERT_INT_EQ("ags inserted into flat bag", slot >= 0, 1);
    ASSERT_INT_EQ("equip ags succeeds",
        osrs_player_equip_from_inventory_slot(player, slot), 1);
    ASSERT_INT_EQ("ags equipped", player->equipped[GEAR_SLOT_WEAPON], ITEM_AGS);
    ASSERT_INT_EQ("shield cleared", player->equipped[GEAR_SLOT_SHIELD], ITEM_NONE);
    ASSERT_INT_EQ("old weapon is clicked-slot swap",
        player->inventory[slot], old_weapon);
    ASSERT_INT_EQ("old shield remains owned",
        osrs_player_inventory_has_item(player, old_shield), 1);
}

static void test_flat_inventory_policy_loadout_uses_bag(void) {
    printf("--- flat inventory policy loadout uses bag ---\n");

    NhPvpState state;
    setup_pvp_state(&state);
    Player* player = &state.env.players[0];
    uint8_t old_weapon = player->equipped[GEAR_SLOT_WEAPON];

    ASSERT_INT_EQ("range loadout changed gear",
        apply_loadout(player, LOADOUT_RANGE) > 0, 1);
    ASSERT_INT_EQ("range weapon equipped",
        player->equipped[GEAR_SLOT_WEAPON], ITEM_RUNE_CROSSBOW);
    ASSERT_INT_EQ("old weapon returned to bag",
        osrs_player_inventory_has_item(player, old_weapon), 1);
}

static void test_pvp_gmaul_loadout_requires_owned_item(void) {
    printf("--- PvP gmaul loadout requires owned item ---\n");

    Player player;
    memset(&player, 0, sizeof(player));
    init_slot_equipment_lms(&player);

    uint8_t old_weapon = player.equipped[GEAR_SLOT_WEAPON];
    ASSERT_INT_EQ("gmaul starts unowned",
        player_has_gmaul(&player), 0);
    ASSERT_INT_EQ("unowned gmaul loadout is noop",
        apply_loadout(&player, LOADOUT_GMAUL), 0);
    ASSERT_INT_EQ("unowned gmaul does not arm spec",
        pvp_loadout_can_arm_spec(&player, LOADOUT_GMAUL), 0);
    ASSERT_INT_EQ("weapon unchanged",
        player.equipped[GEAR_SLOT_WEAPON], old_weapon);

    ASSERT_INT_EQ("gmaul inserted",
        osrs_player_inventory_add(&player, ITEM_GRANITE_MAUL) >= 0, 1);
    ASSERT_INT_EQ("owned gmaul can arm spec",
        pvp_loadout_can_arm_spec(&player, LOADOUT_GMAUL), 1);
    ASSERT_INT_EQ("owned gmaul equips",
        apply_loadout(&player, LOADOUT_GMAUL) > 0, 1);
    ASSERT_INT_EQ("gmaul equipped",
        player.equipped[GEAR_SLOT_WEAPON], ITEM_GRANITE_MAUL);
    ASSERT_INT_EQ("gmaul clears shield",
        player.equipped[GEAR_SLOT_SHIELD], ITEM_NONE);
}

static void test_pvp_randomized_resets_keep_valid_weapon_profiles(void) {
    printf("--- PvP randomized resets keep valid weapon profiles ---\n");

    NhPvpState state;
    setup_pvp_state(&state);
    memset(state.env.pvp_runtime.gear_tier_weights, 0,
        sizeof(state.env.pvp_runtime.gear_tier_weights));
    state.env.pvp_runtime.gear_tier_weights[3] = 1.0f;

    for (int seed = 1; seed <= 512; seed++) {
        pvp_seed(&state.env, (uint32_t)seed);
        pvp_reset(&state.env);
        for (int agent = 0; agent < NUM_AGENTS; agent++) {
            Player* player = &state.env.players[agent];
            AttackStyle style = get_slot_weapon_attack_style(player);
            ASSERT_INT_EQ("reset weapon style valid",
                style != ATTACK_STYLE_NONE, 1);
            OsrsPlayerAttackProfile profile =
                osrs_player_attack_profile_for_loadout(
                    player->equipped,
                    style,
                    player->fight_style,
                    style == ATTACK_STYLE_MAGIC ? 30 : 0);
            ASSERT_INT_EQ("reset attack profile has cycle",
                profile.cycle_ticks > 0, 1);
        }
    }
}

static void test_pvp_invalid_spec_loadout_does_not_clear_weapon(void) {
    printf("--- PvP invalid spec loadout does not clear weapon ---\n");

    Player player;
    memset(&player, 0, sizeof(player));
    init_slot_equipment_lms(&player);
    ASSERT_INT_EQ("gmaul inserted",
        osrs_player_inventory_add(&player, ITEM_GRANITE_MAUL) >= 0, 1);
    ASSERT_INT_EQ("gmaul equips",
        apply_loadout(&player, LOADOUT_GMAUL) > 0, 1);
    ASSERT_INT_EQ("magic spec unavailable",
        pvp_loadout_can_arm_spec(&player, LOADOUT_SPEC_MAGIC), 0);

    ASSERT_INT_EQ("invalid spec magic noops",
        apply_loadout(&player, LOADOUT_SPEC_MAGIC), 0);
    ASSERT_INT_EQ("gmaul still equipped",
        player.equipped[GEAR_SLOT_WEAPON], ITEM_GRANITE_MAUL);
    ASSERT_INT_EQ("shield stays empty",
        player.equipped[GEAR_SLOT_SHIELD], ITEM_NONE);
}

static void test_pvp_loot_replacement_preserves_owned_set(void) {
    printf("--- PvP loot replacement preserves owned set ---\n");

    Player player;
    memset(&player, 0, sizeof(player));
    init_slot_equipment_lms(&player);

    ASSERT_INT_EQ("ahrim staff starts owned",
        osrs_player_owns_item(&player, ITEM_AHRIM_STAFF), 1);
    add_loot_item(&player, ITEM_KODAI_WAND);
    ASSERT_INT_EQ("kodai added",
        osrs_player_owns_item(&player, ITEM_KODAI_WAND), 1);
    ASSERT_INT_EQ("ahrim staff removed",
        osrs_player_owns_item(&player, ITEM_AHRIM_STAFF), 0);
}

static void test_pvp_human_item_click_equips_and_attacks_with_weapon(void) {
    printf("--- PvP human item click equips and attacks with weapon ---\n");

    NhPvpState state;
    setup_pvp_state(&state);
    Player* player = &state.env.players[0];
    int slot = osrs_player_inventory_find(player, ITEM_RUNE_CROSSBOW);

    HumanInput hi;
    human_input_init(&hi);
    hi.enabled = 1;
    human_input_queue_equip_inventory_item(
        &hi, slot, ITEM_RUNE_CROSSBOW, GEAR_SLOT_WEAPON);
    human_input_queue_attack_npc(&hi, 1);

    nh_pvp_step_human_commands((EncounterState*)&state, NULL, &hi);

    ASSERT_INT_EQ("human click equipped crossbow",
        player->equipped[GEAR_SLOT_WEAPON], ITEM_RUNE_CROSSBOW);
    ASSERT_INT_EQ("human attack used ranged style",
        player->attack_style_this_tick, ATTACK_STYLE_RANGED);
    ASSERT_INT_EQ("human command queue cleared", hi.commands.count, 0);

    human_input_destroy(&hi);
}

static void test_pvp_slotclick_action_equips_and_attacks_same_tick(void) {
    printf("--- PvP slot-click action equips and attacks same tick ---\n");

    NhPvpState state;
    setup_pvp_state(&state);
    Player* player = &state.env.players[0];
    Player* target = &state.env.players[1];
    int slot = osrs_player_inventory_find(player, ITEM_RUNE_CROSSBOW);
    ASSERT_INT_EQ("slot-click crossbow exists", slot >= 0, 1);

    pvp_set_player_spawn(player, 3041, 3530);
    pvp_set_player_spawn(target, 3043, 3530);
    player->attack_timer = 0;

    int actions[NUM_ACTION_HEADS];
    memset(actions, 0, sizeof(actions));
    actions[HEAD_EQUIP_0] = slot + 1;
    actions[HEAD_ATTACK] = ATTACK_ATK;

    nh_pvp_step((EncounterState*)&state, NULL, actions);

    ASSERT_INT_EQ("slot-click equipped crossbow",
        player->equipped[GEAR_SLOT_WEAPON], ITEM_RUNE_CROSSBOW);
    ASSERT_INT_EQ("slot-click attack used ranged",
        player->attack_style_this_tick, ATTACK_STYLE_RANGED);
}

static void test_pvp_human_armor_click_updates_equipment(void) {
    printf("--- PvP human armor click updates equipment ---\n");

    NhPvpState state;
    setup_pvp_state(&state);
    Player* player = &state.env.players[0];
    int slot = osrs_player_inventory_find(player, ITEM_MYSTIC_TOP);

    HumanInput hi;
    human_input_init(&hi);
    hi.enabled = 1;
    human_input_queue_equip_inventory_item(
        &hi, slot, ITEM_MYSTIC_TOP, GEAR_SLOT_BODY);

    nh_pvp_step_human_commands((EncounterState*)&state, NULL, &hi);

    ASSERT_INT_EQ("human click equipped mystic top",
        player->equipped[GEAR_SLOT_BODY], ITEM_MYSTIC_TOP);
    ASSERT_INT_EQ("human command queue cleared", hi.commands.count, 0);

    human_input_destroy(&hi);
}

static void test_pvp_human_command_frame_maps_actions(void) {
    printf("--- PvP human command frame maps actions ---\n");

    NhPvpState state;
    setup_pvp_state(&state);
    int actions[NUM_ACTION_HEADS];

    HumanInput hi;
    human_input_init(&hi);
    hi.enabled = 1;
    human_input_queue_overhead_prayer(&hi, ENCOUNTER_OVERHEAD_SET_REFRESH_MAGIC);
    human_input_queue_offensive_prayer(&hi, ENCOUNTER_OFFENSIVE_SET_REFRESH_RIGOUR);
    human_input_queue_eat(&hi, 0);
    human_input_queue_eat(&hi, 1);
    human_input_queue_drink(&hi, POTION_RESTORE, 7);
    human_input_queue_spec_toggle(&hi);
    human_input_queue_spell_target(&hi, ATTACK_ICE, 1);
    hi.pending_veng = 1;

    nh_pvp_translate_human_commands(&hi, actions, &state.env);

    ASSERT_INT_EQ("human spell target maps ice", actions[HEAD_COMBAT], ATTACK_ICE);
    ASSERT_INT_EQ("human overhead maps",
        actions[HEAD_OVERHEAD], ENCOUNTER_OVERHEAD_SET_REFRESH_MAGIC);
    ASSERT_INT_EQ("human offensive maps",
        actions[HEAD_OFFENSIVE], ENCOUNTER_OFFENSIVE_SET_REFRESH_RIGOUR);
    ASSERT_INT_EQ("human food maps", actions[HEAD_FOOD], FOOD_EAT);
    ASSERT_INT_EQ("human karambwan maps", actions[HEAD_KARAMBWAN], KARAM_EAT);
    ASSERT_INT_EQ("human potion maps", actions[HEAD_POTION], POTION_RESTORE);
    ASSERT_INT_EQ("human vengeance maps", actions[HEAD_VENG], VENG_CAST);
    ASSERT_INT_EQ("human spec maps", actions[HEAD_SPECIAL], SPECIAL_ARM);

    human_input_destroy(&hi);
}

static void test_pvp_human_walk_persists_until_runtime_clears(void) {
    printf("--- PvP human walk persists until runtime clears ---\n");

    NhPvpState state;
    setup_pvp_state(&state);

    HumanInput hi;
    human_input_init(&hi);
    hi.enabled = 1;
    human_input_queue_walk(&hi, state.env.players[0].x + 3, state.env.players[0].y);

    nh_pvp_step_human_commands((EncounterState*)&state, NULL, &hi);

    ASSERT_INT_EQ("walk destination remains in runtime",
        state.env.pvp_runtime.walk_dest_x[0] >= 0, 1);
    ASSERT_INT_EQ("command queue clears after step", hi.commands.count, 0);

    human_input_destroy(&hi);
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

static void assert_special_visual_row(
    const char* name,
    int item_idx,
    AttackStyle style,
    int attack_anim_id,
    int launch_spotanim_id,
    int travel_spotanim_id,
    int impact_spotanim_id,
    int projectile_model_id,
    int projectile_anim_id
) {
    char label[160];
    uint16_t item_id = ITEM_DATABASE[item_idx].item_id;
    const OsrsCombatVisualRow* row =
        osrs_combat_visual_find_special_item_id(item_id, style);

    snprintf(label, sizeof(label), "%s special row exists", name);
    ASSERT_INT_EQ(label, row != NULL, 1);
    if (!row) return;

    snprintf(label, sizeof(label), "%s special attack anim", name);
    ASSERT_INT_EQ(label,
        osrs_combat_visual_weapon_attack_anim(
            (uint8_t)item_idx, style, 1, OSRS_COMBAT_VISUAL_NO_ANIMATION),
        attack_anim_id);
    snprintf(label, sizeof(label), "%s attack anim is cached", name);
    ASSERT_INT_EQ(label,
        anim_get_sequence(test_equipment_animation_cache(),
            (uint16_t)attack_anim_id) != NULL,
        1);

    snprintf(label, sizeof(label), "%s launch spotanim", name);
    ASSERT_INT_EQ(label, row->projectile.launch_spotanim_id, launch_spotanim_id);
    snprintf(label, sizeof(label), "%s travel spotanim", name);
    ASSERT_INT_EQ(label, row->projectile.travel_spotanim_id, travel_spotanim_id);
    snprintf(label, sizeof(label), "%s impact spotanim", name);
    ASSERT_INT_EQ(label, row->projectile.impact_spotanim_id, impact_spotanim_id);
    snprintf(label, sizeof(label), "%s projectile model", name);
    ASSERT_INT_EQ(label, row->projectile.projectile_model_id, projectile_model_id);
    snprintf(label, sizeof(label), "%s projectile anim", name);
    ASSERT_INT_EQ(label, row->projectile.projectile_anim_id, projectile_anim_id);

    int spotanim_ids[] = {launch_spotanim_id, travel_spotanim_id, impact_spotanim_id};
    for (size_t i = 0; i < sizeof(spotanim_ids) / sizeof(spotanim_ids[0]); i++) {
        int spotanim_id = spotanim_ids[i];
        if (spotanim_id == OSRS_COMBAT_PROJECTILE_MISSING) continue;
        const OsrsSpotAnimDef* def =
            osrs_spotanim_find(test_spotanim_set(), spotanim_id);
        snprintf(label, sizeof(label), "%s spotanim %d is cached", name, spotanim_id);
        ASSERT_INT_EQ(label, def != NULL, 1);
        if (def && def->animation_id >= 0) {
            snprintf(label, sizeof(label), "%s spotanim %d animation is cached",
                name, spotanim_id);
            ASSERT_INT_EQ(label,
                anim_get_sequence(test_equipment_animation_cache(),
                    (uint16_t)def->animation_id) != NULL,
                1);
        }
    }

    if (launch_spotanim_id != OSRS_COMBAT_PROJECTILE_MISSING ||
            travel_spotanim_id != OSRS_COMBAT_PROJECTILE_MISSING ||
            impact_spotanim_id != OSRS_COMBAT_PROJECTILE_MISSING) {
        const OsrsCombatVisualRow* projectile_row =
            osrs_combat_visual_find_special_projectile_item_id(item_id, style);
        snprintf(label, sizeof(label), "%s special projectile lookup", name);
        ASSERT_INT_EQ(label, projectile_row == row, 1);
    }
}

static void test_pvp_current_special_visual_rows(void) {
    printf("--- PvP current special visual rows ---\n");

    assert_special_visual_row("Granite maul", ITEM_GRANITE_MAUL,
        ATTACK_STYLE_MELEE, 1667, 340,
        OSRS_COMBAT_PROJECTILE_MISSING, OSRS_COMBAT_PROJECTILE_MISSING,
        OSRS_COMBAT_PROJECTILE_MISSING, OSRS_COMBAT_PROJECTILE_MISSING);
    assert_special_visual_row("Dragon claws", ITEM_DRAGON_CLAWS,
        ATTACK_STYLE_MELEE, 7514, 1171,
        OSRS_COMBAT_PROJECTILE_MISSING, OSRS_COMBAT_PROJECTILE_MISSING,
        OSRS_COMBAT_PROJECTILE_MISSING, OSRS_COMBAT_PROJECTILE_MISSING);
    assert_special_visual_row("Statius warhammer", ITEM_STATIUS_WARHAMMER,
        ATTACK_STYLE_MELEE, 1378, 844,
        OSRS_COMBAT_PROJECTILE_MISSING, OSRS_COMBAT_PROJECTILE_MISSING,
        OSRS_COMBAT_PROJECTILE_MISSING, OSRS_COMBAT_PROJECTILE_MISSING);
    assert_special_visual_row("Vesta longsword", ITEM_VESTAS,
        ATTACK_STYLE_MELEE, 7515, OSRS_COMBAT_PROJECTILE_MISSING,
        OSRS_COMBAT_PROJECTILE_MISSING, OSRS_COMBAT_PROJECTILE_MISSING,
        OSRS_COMBAT_PROJECTILE_MISSING, OSRS_COMBAT_PROJECTILE_MISSING);
    assert_special_visual_row("Ancient godsword", ITEM_ANCIENT_GS,
        ATTACK_STYLE_MELEE, 9171, 1996,
        OSRS_COMBAT_PROJECTILE_MISSING, OSRS_COMBAT_PROJECTILE_MISSING,
        OSRS_COMBAT_PROJECTILE_MISSING, OSRS_COMBAT_PROJECTILE_MISSING);
    assert_special_visual_row("Voidwaker", ITEM_VOIDWAKER,
        ATTACK_STYLE_MELEE, 11275, 2834,
        OSRS_COMBAT_PROJECTILE_MISSING, 2363,
        OSRS_COMBAT_PROJECTILE_MISSING, OSRS_COMBAT_PROJECTILE_MISSING);
    assert_special_visual_row("Armadyl crossbow", ITEM_ARMADYL_CROSSBOW,
        ATTACK_STYLE_RANGED, 4230, 301,
        OSRS_COMBAT_PROJECTILE_MISSING, OSRS_COMBAT_PROJECTILE_MISSING,
        OSRS_COMBAT_PROJECTILE_MISSING, OSRS_COMBAT_PROJECTILE_MISSING);
    assert_special_visual_row("Zaryte crossbow", ITEM_ZARYTE_CROSSBOW,
        ATTACK_STYLE_RANGED, 9166, 1995,
        OSRS_COMBAT_PROJECTILE_MISSING, OSRS_COMBAT_PROJECTILE_MISSING,
        OSRS_COMBAT_PROJECTILE_MISSING, OSRS_COMBAT_PROJECTILE_MISSING);
    assert_special_visual_row("Morrigan javelin", ITEM_MORRIGANS_JAVELIN,
        ATTACK_STYLE_RANGED, 11467, 2919, 2920,
        OSRS_COMBAT_PROJECTILE_MISSING, 54231, 11472);
    assert_special_visual_row("Volatile staff", ITEM_VOLATILE_STAFF,
        ATTACK_STYLE_MAGIC, 8532, 1760,
        OSRS_COMBAT_PROJECTILE_MISSING, 1759,
        OSRS_COMBAT_PROJECTILE_MISSING, OSRS_COMBAT_PROJECTILE_MISSING);
}

static void test_pvp_godsword_and_voidwaker_specials_are_distinct(void) {
    printf("--- PvP godsword and Voidwaker specials are distinct ---\n");

    const OsrsCombatVisualRow* ancient =
        osrs_combat_visual_find_special_item_id(
            ITEM_DATABASE[ITEM_ANCIENT_GS].item_id, ATTACK_STYLE_MELEE);
    const OsrsCombatVisualRow* ags =
        osrs_combat_visual_find_special_item_id(
            ITEM_DATABASE[ITEM_AGS].item_id, ATTACK_STYLE_MELEE);
    const OsrsCombatVisualRow* voidwaker =
        osrs_combat_visual_find_special_item_id(
            ITEM_DATABASE[ITEM_VOIDWAKER].item_id, ATTACK_STYLE_MELEE);

    ASSERT_INT_EQ("Ancient row exists", ancient != NULL, 1);
    ASSERT_INT_EQ("AGS row exists", ags != NULL, 1);
    ASSERT_INT_EQ("Voidwaker row exists", voidwaker != NULL, 1);
    if (!ancient || !ags || !voidwaker) return;

    ASSERT_INT_EQ("Ancient anim", ancient->attack_anim_id, 9171);
    ASSERT_INT_EQ("AGS anim", ags->attack_anim_id, 7644);
    ASSERT_INT_EQ("Voidwaker anim", voidwaker->attack_anim_id, 11275);
    ASSERT_INT_EQ("Ancient launch differs from AGS",
        ancient->projectile.launch_spotanim_id != ags->projectile.launch_spotanim_id,
        1);
    ASSERT_INT_EQ("Voidwaker impact differs from Ancient",
        voidwaker->projectile.impact_spotanim_id !=
            ancient->projectile.impact_spotanim_id,
        1);
}

static void test_pvp_ancient_priority_has_wield_model(void) {
    printf("--- PvP Ancient priority has wield model ---\n");

    NhPvpState state;
    setup_pvp_state(&state);
    Player* attacker = &state.env.players[0];

    osrs_player_inventory_clear(attacker);
    osrs_player_set_equipment_slot(attacker, GEAR_SLOT_WEAPON, ITEM_WHIP);
    ASSERT_INT_EQ("add Voidwaker", osrs_player_inventory_add(attacker, ITEM_VOIDWAKER) >= 0, 1);
    ASSERT_INT_EQ("add Ancient godsword",
        osrs_player_inventory_add(attacker, ITEM_ANCIENT_GS) >= 0, 1);
    ASSERT_INT_EQ("spec melee loadout applies",
        apply_loadout(attacker, LOADOUT_SPEC_MELEE) > 0, 1);

    ASSERT_INT_EQ("Ancient priority remains above Voidwaker",
        attacker->equipped[GEAR_SLOT_WEAPON], ITEM_ANCIENT_GS);
    ASSERT_INT_EQ("Ancient spec enum selected",
        attacker->melee_spec_weapon, MELEE_SPEC_ANCIENT_GS);
    ASSERT_INT_EQ("Ancient wield model exists",
        item_to_wield_model(ITEM_DATABASE[ITEM_ANCIENT_GS].item_id) !=
            ITEM_RENDER_MODEL_MISSING,
        1);
    ASSERT_INT_EQ("Ancient is treated as two-handed",
        item_render_is_two_handed(ITEM_DATABASE[ITEM_ANCIENT_GS].item_id), 1);
}

static void test_special_visual_fallbacks_do_not_overlap_local_rows(void) {
    printf("--- special visual fallbacks do not overlap local rows ---\n");

    for (size_t i = 0; i < OSRS_COMBAT_VISUAL_LOCAL_ROW_COUNT; i++) {
        const OsrsCombatVisualRow* row = &OSRS_COMBAT_VISUAL_LOCAL_ROWS[i];
        if (row->kind != OSRS_COMBAT_VISUAL_KIND_SPECIAL) continue;
        ASSERT_INT_EQ("local special row has no fallback",
            osrs_combat_visual_special_fallback_anim((uint16_t)row->key_id),
            OSRS_COMBAT_VISUAL_NO_ANIMATION);
    }
}

static void assert_item_pose_anims(
    const char* name,
    int item_idx,
    uint32_t ready,
    uint32_t walk,
    uint32_t run
) {
    char label[160];
    uint16_t item_id = ITEM_DATABASE[item_idx].item_id;

    snprintf(label, sizeof(label), "%s ready anim", name);
    ASSERT_INT_EQ(label, item_render_ready_anim(item_id), ready);
    snprintf(label, sizeof(label), "%s walk anim", name);
    ASSERT_INT_EQ(label, item_render_walk_anim(item_id), walk);
    snprintf(label, sizeof(label), "%s run anim", name);
    ASSERT_INT_EQ(label, item_render_run_anim(item_id), run);

    uint32_t ids[] = {ready, walk, run};
    for (size_t i = 0; i < sizeof(ids) / sizeof(ids[0]); i++) {
        snprintf(label, sizeof(label), "%s pose anim %u cached", name, ids[i]);
        ASSERT_INT_EQ(label,
            anim_get_sequence(test_equipment_animation_cache(),
                (uint16_t)ids[i]) != NULL,
            1);
    }
}

static void test_pvp_weapon_pose_anims_are_mapped(void) {
    printf("--- PvP weapon pose anims are mapped ---\n");

    assert_item_pose_anims("Granite maul", ITEM_GRANITE_MAUL, 1662, 1663, 1664);
    assert_item_pose_anims("Heavy ballista", ITEM_HEAVY_BALLISTA, 7220, 7223, 7221);
    assert_item_pose_anims("Voidwaker", ITEM_VOIDWAKER, 244, 247, 248);
}

static void test_pvp_voidwaker_special_uses_melee_visual_and_magic_damage(void) {
    printf("--- PvP Voidwaker special visual and damage styles ---\n");

    NhPvpState state;
    setup_pvp_state(&state);
    Player* attacker = &state.env.players[0];
    Player* defender = &state.env.players[1];

    attacker->x = 3041;
    attacker->y = 3530;
    attacker->dest_x = attacker->x;
    attacker->dest_y = attacker->y;
    defender->x = 3042;
    defender->y = 3530;
    defender->dest_x = defender->x;
    defender->dest_y = defender->y;

    osrs_player_set_equipment_slot(attacker, GEAR_SLOT_WEAPON, ITEM_WHIP);
    attacker->melee_spec_weapon = MELEE_SPEC_VOIDWAKER;
    attacker->special_energy = 100;

    perform_attack(&state.env, 0, 1, ATTACK_STYLE_MELEE, 1, 0, 1);

    RenderEntity entities[NUM_AGENTS];
    int count = 0;
    fill_pvp_entities(&state, entities, &count);

    ASSERT_INT_EQ("Voidwaker records special weapon",
        attacker->attack_weapon_this_tick, ITEM_VOIDWAKER);
    ASSERT_INT_EQ("Voidwaker render entity records special weapon",
        entities[0].attack_weapon_this_tick, ITEM_VOIDWAKER);
    ASSERT_INT_EQ("Voidwaker render entity shows special weapon",
        entities[0].equipped[GEAR_SLOT_WEAPON], ITEM_VOIDWAKER);
    ASSERT_INT_EQ("Voidwaker render target resolves defender",
        entities[0].attack_target_entity_idx, 1);
    ASSERT_INT_EQ("Voidwaker wield model exists",
        item_to_wield_model(ITEM_DATABASE[ITEM_VOIDWAKER].item_id) !=
            ITEM_RENDER_MODEL_MISSING,
        1);
    ASSERT_INT_EQ("Voidwaker visual style",
        attacker->attack_style_this_tick, ATTACK_STYLE_MELEE);
    ASSERT_INT_EQ("Voidwaker damage style",
        attacker->last_attack_style, ATTACK_STYLE_MAGIC);
    ASSERT_INT_EQ("Voidwaker queues one hit",
        attacker->num_pending_hits, 1);
    ASSERT_INT_EQ("Voidwaker pending hit is magic",
        attacker->pending_hits[0].attack_type, ATTACK_STYLE_MAGIC);
    ASSERT_INT_EQ("Voidwaker special animation",
        osrs_combat_visual_weapon_attack_anim(
            attacker->attack_weapon_this_tick,
            attacker->attack_style_this_tick,
            attacker->used_special_this_tick,
            OSRS_COMBAT_VISUAL_NO_ANIMATION),
        11275);
}

static void test_pvp_special_attack_visual_weapon_and_effect_contract(void) {
    printf("--- PvP special attack visual weapon and effect contract ---\n");

    NhPvpState state;
    setup_pvp_state(&state);
    Player* attacker = &state.env.players[0];
    Player* defender = &state.env.players[1];

    attacker->x = 3041;
    attacker->y = 3530;
    attacker->dest_x = attacker->x;
    attacker->dest_y = attacker->y;
    defender->x = 3042;
    defender->y = 3530;
    defender->dest_x = defender->x;
    defender->dest_y = defender->y;

    osrs_player_set_equipment_slot(attacker, GEAR_SLOT_WEAPON, ITEM_WHIP);
    attacker->melee_spec_weapon = MELEE_SPEC_AGS;
    attacker->special_energy = 100;

    perform_attack(&state.env, 0, 1, ATTACK_STYLE_MELEE, 1, 0, 1);

    RenderEntity entities[NUM_AGENTS];
    int count = 0;
    fill_pvp_entities(&state, entities, &count);

    ASSERT_INT_EQ("special event records AGS weapon",
        attacker->attack_weapon_this_tick, ITEM_AGS);
    ASSERT_INT_EQ("render entity records AGS attack weapon",
        entities[0].attack_weapon_this_tick, ITEM_AGS);
    ASSERT_INT_EQ("render entity shows AGS for attack frame",
        entities[0].equipped[GEAR_SLOT_WEAPON], ITEM_AGS);
    ASSERT_INT_EQ("render entity clears shield for AGS attack frame",
        entities[0].equipped[GEAR_SLOT_SHIELD], ITEM_NONE);

    const OsrsCombatVisualRow* ags_effect =
        osrs_combat_visual_find_special_projectile_item_id(
            ITEM_DATABASE[ITEM_AGS].item_id, ATTACK_STYLE_MELEE);
    ASSERT_INT_EQ("AGS special visual row exists", ags_effect != NULL, 1);
    if (!ags_effect) return;
    ASSERT_INT_EQ("AGS special launch gfx", ags_effect->projectile.launch_spotanim_id, 1206);
    ASSERT_INT_EQ("AGS special has no travel projectile",
        ags_effect->projectile.travel_spotanim_id, OSRS_COMBAT_PROJECTILE_MISSING);
    ASSERT_INT_EQ("AGS special animation",
        osrs_combat_visual_weapon_attack_anim(
            ITEM_AGS, ATTACK_STYLE_MELEE, 1, -1),
        7644);
}

static void test_pvp_magic_landing_keeps_spell_visual_context(void) {
    printf("--- PvP magic landing keeps spell visual context ---\n");

    NhPvpState state;
    setup_pvp_state(&state);
    Player* attacker = &state.env.players[0];
    Player* defender = &state.env.players[1];

    queue_hit(attacker, defender, 0, ATTACK_STYLE_MAGIC, 0, 0, 0,
        OSRS_COMBAT_VISUAL_SPELL_BLOOD_BARRAGE, 0, 0, 0, 0, 0);
    process_pending_hits(&state.env, 0, 1);

    RenderEntity entities[NUM_AGENTS];
    int count = 0;
    fill_pvp_entities(&state, entities, &count);

    ASSERT_INT_EQ("splashing player emits landed visual event",
        entities[1].hit_landed_this_tick, 1);
    ASSERT_INT_EQ("splashing player records failed accuracy",
        entities[1].hit_was_successful, 0);
    ASSERT_INT_EQ("splashing player records blood barrage spell",
        entities[1].hit_spell_type, OSRS_COMBAT_VISUAL_SPELL_BLOOD_BARRAGE);
    ASSERT_INT_EQ("blood barrage miss uses splash gfx",
        osrs_combat_visual_spell_impact_gfx(
            OSRS_COMBAT_VISUAL_SPELL_BLOOD_BARRAGE, 0),
        GFX_SPLASH);

    defender->hit_landed_this_tick = 0;
    defender->hit_was_successful = 0;
    defender->hit_spell_type = 0;
    defender->hit_damage = 0;

    queue_hit(attacker, defender, 7, ATTACK_STYLE_MAGIC, 0, 0, 1,
        OSRS_COMBAT_VISUAL_SPELL_ICE_BARRAGE, 0, 0, 0, 0, 0);
    process_pending_hits(&state.env, 0, 1);
    fill_pvp_entities(&state, entities, &count);

    ASSERT_INT_EQ("ice barrage hit records successful accuracy",
        entities[1].hit_was_successful, 1);
    ASSERT_INT_EQ("ice barrage hit records spell",
        entities[1].hit_spell_type, OSRS_COMBAT_VISUAL_SPELL_ICE_BARRAGE);
    ASSERT_INT_EQ("ice barrage hit uses ice impact gfx",
        osrs_combat_visual_spell_impact_gfx(
            OSRS_COMBAT_VISUAL_SPELL_ICE_BARRAGE, 1),
        GFX_ICE_BARRAGE_HIT);
    ASSERT_INT_EQ("blood barrage hit uses blood impact gfx",
        osrs_combat_visual_spell_impact_gfx(
            OSRS_COMBAT_VISUAL_SPELL_BLOOD_BARRAGE, 1),
        GFX_BLOOD_BARRAGE_HIT);
}

static void test_spell_impact_entity_center_normalizes_size(void) {
    printf("--- spell impact entity center normalizes size ---\n");

    RenderEntity player = {0};
    player.entity_type = ENTITY_PLAYER;
    player.x = 10;
    player.y = 20;
    player.npc_size = 0;

    ASSERT_FLOAT_NEAR("player center x",
        render_entity_center_subtile_x(&player), 1344.0f, 0.001f);
    ASSERT_FLOAT_NEAR("player center y",
        render_entity_center_subtile_y(&player), 2624.0f, 0.001f);
    ASSERT_INT_EQ("player footprint size",
        render_entity_footprint_size(&player), 1);

    RenderEntity npc = {0};
    npc.entity_type = ENTITY_NPC;
    npc.x = 7;
    npc.y = 9;
    npc.npc_size = 3;

    ASSERT_FLOAT_NEAR("npc center x",
        render_entity_center_subtile_x(&npc), 1088.0f, 0.001f);
    ASSERT_FLOAT_NEAR("npc center y",
        render_entity_center_subtile_y(&npc), 1344.0f, 0.001f);
    ASSERT_INT_EQ("npc footprint size",
        render_entity_footprint_size(&npc), 3);
}

static void test_spell_impact_profile_heights(void) {
    printf("--- spell impact profile heights ---\n");

    ASSERT_INT_EQ("ice barrage hit height",
        osrs_combat_visual_spell_impact_height_subtile(
            OSRS_COMBAT_VISUAL_SPELL_ICE_BARRAGE, 1),
        0);
    ASSERT_INT_EQ("blood barrage hit height",
        osrs_combat_visual_spell_impact_height_subtile(
            OSRS_COMBAT_VISUAL_SPELL_BLOOD_BARRAGE, 1),
        124);
    ASSERT_INT_EQ("ice barrage miss height",
        osrs_combat_visual_spell_impact_height_subtile(
            OSRS_COMBAT_VISUAL_SPELL_ICE_BARRAGE, 0),
        0);
}

static void test_height_aware_spotanim_spawn_stores_height(void) {
    printf("--- height-aware spotanim spawn stores height ---\n");

    ActiveEffect effects[MAX_ACTIVE_EFFECTS] = {0};
    int slot = effect_spawn_spotanim_subtile_height(
        effects, GFX_BLOOD_BARRAGE_HIT, 1344.0f, 2624.0f, 124.0f, 7,
        test_spotanim_set(), NULL, NULL, NULL, NULL);

    ASSERT_INT_EQ("spotanim spawn succeeds", slot >= 0, 1);
    if (slot < 0) return;
    ASSERT_INT_EQ("spotanim type", effects[slot].type, EFFECT_SPOTANIM);
    ASSERT_FLOAT_NEAR("spotanim x fixed",
        (float)effects[slot].cur_x, 1344.0f, 0.001f);
    ASSERT_FLOAT_NEAR("spotanim y fixed",
        (float)effects[slot].cur_y, 2624.0f, 0.001f);
    ASSERT_FLOAT_NEAR("spotanim height stored",
        (float)effects[slot].height, 124.0f, 0.001f);

    effect_clear_all(effects);
}

static void test_actor_spotanim_follows_owner_and_slot_replaces(void) {
    printf("--- actor spotanim follows owner and slot replaces ---\n");

    ActiveEffectActorOwner owner = {
        .entity_idx = 1,
        .entity_type = ENTITY_PLAYER,
        .npc_slot = -1,
        .npc_instance_id = 0,
    };
    ActiveEffect effects[MAX_ACTIVE_EFFECTS] = {0};
    int slot = effect_spawn_actor_spotanim_height(
        effects, GFX_ICE_BARRAGE_HIT, owner, 0, 1344.0f, 2624.0f, 0.0f, 11,
        test_spotanim_set(), NULL, NULL, NULL, NULL);

    ASSERT_INT_EQ("ice actor spotanim spawn succeeds", slot >= 0, 1);
    if (slot < 0) return;
    ASSERT_INT_EQ("ice actor spotanim type",
        effects[slot].type, EFFECT_ACTOR_SPOTANIM);
    ASSERT_INT_EQ("actor owner entity index",
        effects[slot].actor_owner.entity_idx, 1);
    ASSERT_INT_EQ("actor spotanim slot",
        effects[slot].actor_spotanim_slot, 0);
    ASSERT_FLOAT_NEAR("actor spotanim follows owner x",
        (float)effect_resolve_subtile_x(&effects[slot], 1472.0), 1472.0f, 0.001f);
    ASSERT_FLOAT_NEAR("actor spotanim follows owner y",
        (float)effect_resolve_subtile_y(&effects[slot], 2752.0), 2752.0f, 0.001f);

    int replacement_slot = effect_spawn_actor_spotanim_height(
        effects, GFX_BLOOD_BARRAGE_HIT, owner, 0, 1472.0f, 2752.0f, 124.0f, 12,
        test_spotanim_set(), NULL, NULL, NULL, NULL);
    ASSERT_INT_EQ("same actor spotanim slot is replaced",
        replacement_slot, slot);
    ASSERT_INT_EQ("replacement gfx stored",
        effects[slot].gfx_id, GFX_BLOOD_BARRAGE_HIT);
    ASSERT_FLOAT_NEAR("replacement height stored",
        (float)effects[slot].height, 124.0f, 0.001f);

    int active = 0;
    for (int i = 0; i < MAX_ACTIVE_EFFECTS; i++) {
        if (effects[i].type != EFFECT_NONE) active++;
    }
    ASSERT_INT_EQ("slot replacement leaves one active effect", active, 1);

    effect_clear_all(effects);
}

static void test_actor_spotanim_explicit_slot_clear_removes_ice(void) {
    printf("--- actor spotanim explicit slot clear removes ice ---\n");

    ActiveEffectActorOwner owner = {
        .entity_idx = 1,
        .entity_type = ENTITY_PLAYER,
        .npc_slot = -1,
        .npc_instance_id = 0,
    };
    ActiveEffect effects[MAX_ACTIVE_EFFECTS] = {0};
    int slot = effect_spawn_actor_spotanim_height(
        effects, GFX_ICE_BARRAGE_HIT, owner, 0, 1344.0f, 2624.0f, 0.0f, 11,
        test_spotanim_set(), NULL, NULL, NULL, NULL);

    ASSERT_INT_EQ("ice actor spotanim spawn succeeds", slot >= 0, 1);
    if (slot < 0) return;
    effect_clear_actor_spotanim_slot(effects, owner, 0);
    ASSERT_INT_EQ("explicit combat slot clear removes ice",
        effects[slot].type, EFFECT_NONE);
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

static void test_pvp_render_uses_wilderness_world_bounds(void) {
    printf("--- PvP render uses wilderness world bounds ---\n");

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

    ASSERT_INT_EQ("PvP sets wilderness render bounds",
        strstr(text, "render_set_world_bounds(rc, WILD_MIN_X, WILD_MIN_Y, WILD_MAX_X, WILD_MAX_Y)") != NULL, 1);
    free(text);

    f = fopen("ocean/osrs/osrs_render.h", "rb");
    ASSERT_INT_EQ("render source opens", f != NULL, 1);
    if (!f) return;
    fseek(f, 0, SEEK_END);
    n = ftell(f);
    rewind(f);
    text = (char*)malloc((size_t)n + 1);
    ASSERT_INT_EQ("render source allocates", text != NULL, 1);
    if (!text) {
        fclose(f);
        return;
    }
    read_n = fread(text, 1, (size_t)n, f);
    text[read_n] = '\0';
    fclose(f);

    ASSERT_INT_EQ("minimap uses render world bounds",
        strstr(text, "render_tile_in_world_bounds(rc, wx, wy)") != NULL, 1);
    ASSERT_INT_EQ("ground picking uses render world bounds",
        strstr(text, "render_pick_ground_tile_from_ray") != NULL &&
        strstr(text, "render_tile_in_world_bounds(rc, wx, wy)") != NULL, 1);
    free(text);
}

static void test_shared_special_effect_render_contract(void) {
    printf("--- shared special effect render contract ---\n");

    FILE* f = fopen("ocean/osrs/osrs_render.h", "rb");
    ASSERT_INT_EQ("render source opens", f != NULL, 1);
    if (!f) return;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    rewind(f);
    char* text = (char*)malloc((size_t)n + 1);
    ASSERT_INT_EQ("render source allocates", text != NULL, 1);
    if (!text) {
        fclose(f);
        return;
    }
    size_t read_n = fread(text, 1, (size_t)n, f);
    text[read_n] = '\0';
    fclose(f);

    ASSERT_INT_EQ("render defines typed effect attachments",
        strstr(text, "RenderEffectAttachmentKind") != NULL &&
        strstr(text, "RENDER_EFFECT_ATTACHMENT_ACTOR") != NULL, 1);
    ASSERT_INT_EQ("render spawns launch spotanims through attachments",
        strstr(text, "profile->launch_spotanim_id") != NULL &&
        strstr(text, "render_spawn_attached_spotanim_height(\n"
            "            rc, launch_attachment") != NULL, 1);
    ASSERT_INT_EQ("render spawns impact-only spotanims through attachments",
        strstr(text, "profile->travel_spotanim_id < 0") != NULL &&
        strstr(text, "profile->impact_spotanim_id") != NULL &&
        strstr(text, "render_spawn_attached_spotanim_height(\n"
            "                rc, impact_attachment") != NULL, 1);
    ASSERT_INT_EQ("PvP passes actor attachments into combat profiles",
        strstr(text, "RenderEffectAttachment attacker_attachment") != NULL &&
        strstr(text, "render_effect_attachment_actor(p, i)") != NULL &&
        strstr(text, "RenderEffectAttachment target_attachment") != NULL &&
        strstr(text, "render_effect_attachment_actor(t, target_i)") != NULL, 1);
    ASSERT_INT_EQ("render uses attack visual weapon",
        strstr(text, "render_entity_attack_visual_weapon") != NULL, 1);
    ASSERT_INT_EQ("render uses actor-attached entity impact helper",
        strstr(text, "render_spawn_entity_spotanim_height") != NULL, 1);
    ASSERT_INT_EQ("render does not generic-clear actor spotanims on attacks",
        strstr(text, "render_clear_entity_combat_spotanim") == NULL, 1);
    ASSERT_INT_EQ("render builds special projectile sequences",
        strstr(text, "osrs_combat_visual_build_projectile_sequence") != NULL, 1);
    free(text);
}

int main(void) {
    test_wilderness_collision_asset_spans_pvp_area();
    test_runtime_animation_assets_are_anm2();
    test_shared_render_target_refs_resolve();
    test_reset_has_no_forced_targets();
    test_attack_sets_render_target();
    test_attack_event_faces_target_after_interaction_clear();
    test_explicit_move_clears_render_target();
    test_opponent_target_is_independent();
    test_inventory_projection_matches_player();
    test_inventory_cycle_marks_rebuild();
    test_flat_inventory_equipment_swaps_clicked_slot();
    test_flat_inventory_two_handed_weapon_moves_shield();
    test_flat_inventory_policy_loadout_uses_bag();
    test_pvp_gmaul_loadout_requires_owned_item();
    test_pvp_randomized_resets_keep_valid_weapon_profiles();
    test_pvp_invalid_spec_loadout_does_not_clear_weapon();
    test_pvp_loot_replacement_preserves_owned_set();
    test_pvp_human_item_click_equips_and_attacks_with_weapon();
    test_pvp_slotclick_action_equips_and_attacks_same_tick();
    test_pvp_human_armor_click_updates_equipment();
    test_pvp_human_command_frame_maps_actions();
    test_pvp_human_walk_persists_until_runtime_clears();
    test_pvp_item_sprites_exist();
    test_pvp_display_names_and_label_target();
    test_terminal_presentation_captures_loser_before_auto_reset();
    test_terminal_status_text_for_player_and_opponent_wins();
    test_terminal_render_entities_use_snapshot();
    test_terminal_winner_phase_removes_loser();
    test_performance_tracker_values();
    test_pvp_current_special_visual_rows();
    test_pvp_godsword_and_voidwaker_specials_are_distinct();
    test_pvp_ancient_priority_has_wield_model();
    test_special_visual_fallbacks_do_not_overlap_local_rows();
    test_pvp_weapon_pose_anims_are_mapped();
    test_pvp_voidwaker_special_uses_melee_visual_and_magic_damage();
    test_pvp_special_attack_visual_weapon_and_effect_contract();
    test_pvp_magic_landing_keeps_spell_visual_context();
    test_spell_impact_entity_center_normalizes_size();
    test_spell_impact_profile_heights();
    test_height_aware_spotanim_spawn_stores_height();
    test_actor_spotanim_follows_owner_and_slot_replaces();
    test_actor_spotanim_explicit_slot_clear_removes_ice();
    test_terminal_render_clear_resets_visual_state_source();
    test_pvp_render_uses_wilderness_world_bounds();
    test_shared_special_effect_render_contract();

    printf("\n%d/%d tests passed\n", tests_passed, tests_run);
    return tests_failed == 0 ? 0 : 1;
}
