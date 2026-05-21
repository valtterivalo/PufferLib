/**
 * @file test_gui_inventory.c
 * @brief Regression tests for GUI inventory snapshot/reset logic used by inferno human mode.
 *
 * BUILD:
 *   cc -std=c11 -O0 -g -I. -Iocean/osrs -I./ocean/osrs/raylib-5.5_macos/include \
 *       -o /tmp/test_gui_inventory \
 *       ocean/osrs/tests/test_gui_inventory.c ./ocean/osrs/raylib-5.5_macos/lib/libraylib.a \
 *       -framework Cocoa -framework OpenGL -framework IOKit -framework CoreVideo -lm
 *   /tmp/test_gui_inventory
 */

#include <stdio.h>
#include <string.h>

#include "ocean/osrs/osrs_pvp_actions.h"
#include "ocean/osrs/encounters/encounter_inferno.h"
#include "ocean/osrs/osrs_gui.h"
#include "ocean/osrs/osrs_human_input.h"

static int tests_run = 0;
static int tests_passed = 0;
static int tests_failed = 0;

#define ASSERT_INT_EQ(label, actual, expected) do { \
    tests_run++; \
    if ((actual) == (expected)) { \
        tests_passed++; \
    } else { \
        tests_failed++; \
        printf("  FAIL: %s — got %d, expected %d\n", (label), (actual), (expected)); \
    } \
} while (0)

static int find_slot_of_type(const GuiState* gs, InvSlotType type) {
    for (int i = 0; i < INV_GRID_SLOTS; i++) {
        if (gs->inv_grid[i].type == type) return i;
    }
    return -1;
}

static int count_slots_of_type(const GuiState* gs, InvSlotType type) {
    int count = 0;
    for (int i = 0; i < INV_GRID_SLOTS; i++) {
        if (gs->inv_grid[i].type == type) count++;
    }
    return count;
}

static int expected_vial_count(int doses) {
    return (doses + 3) / 4;
}

static int file_exists(const char* path) {
    FILE* file = fopen(path, "rb");
    if (!file) return 0;
    fclose(file);
    return 1;
}

static int load_test_ui_interfaces(OsrsUiInterfaceStore* store) {
    return osrs_ui_interfaces_load(store, OSRS_ASSET("ui/interfaces.bin"));
}

static void assert_rect_eq(const char* label, Rectangle actual, Rectangle expected) {
    char part[128];
    snprintf(part, sizeof(part), "%s x", label);
    ASSERT_INT_EQ(part, (int)actual.x, (int)expected.x);
    snprintf(part, sizeof(part), "%s y", label);
    ASSERT_INT_EQ(part, (int)actual.y, (int)expected.y);
    snprintf(part, sizeof(part), "%s width", label);
    ASSERT_INT_EQ(part, (int)actual.width, (int)expected.width);
    snprintf(part, sizeof(part), "%s height", label);
    ASSERT_INT_EQ(part, (int)actual.height, (int)expected.height);
}

static void test_gui_populate_tracks_bastion_and_stamina(void) {
    printf("--- gui populate tracks inferno potion snapshots ---\n");

    GuiState gs;
    Player p;
    memset(&gs, 0, sizeof(gs));
    memset(&p, 0, sizeof(p));

    p.bastion_doses = 8;
    p.stamina_doses = 4;

    gui_populate_inventory(&gs, &p);

    ASSERT_INT_EQ("snapshot keeps bastion doses", gs.inv_prev_bastion_doses, 8);
    ASSERT_INT_EQ("snapshot keeps stamina doses", gs.inv_prev_stamina_doses, 4);
    ASSERT_INT_EQ("bastion vials present", count_slots_of_type(&gs, INV_SLOT_BASTION_POT), 2);
    ASSERT_INT_EQ("stamina vial present", find_slot_of_type(&gs, INV_SLOT_STAMINA_POT) >= 0, 1);
}

static void test_inventory_slot_geometry_matches_osrs_reference(void) {
    printf("--- inventory slot geometry matches osrs reference ---\n");

    GuiState gs;
    memset(&gs, 0, sizeof(gs));

    gs.panel_x = 100;
    gs.panel_y = 200;
    gs.panel_w = 241;
    gs.tab_h = 37;

    int x = 0;
    int y = 0;
    gui_inv_slot_pos(&gs, 0, &x, &y);
    ASSERT_INT_EQ("slot 0 x", x, 139);
    ASSERT_INT_EQ("slot 0 y", y, 245);

    gui_inv_slot_pos(&gs, 27, &x, &y);
    ASSERT_INT_EQ("slot 27 x", x, 265);
    ASSERT_INT_EQ("slot 27 y", y, 461);

    ASSERT_INT_EQ("inventory sprite width", INV_SPRITE_W, 32);
    ASSERT_INT_EQ("inventory sprite height", INV_SPRITE_H, 32);
}

static void test_side_panel_geometry_matches_runec_reference(void) {
    printf("--- side panel geometry matches runec reference ---\n");

    GuiState gs;
    memset(&gs, 0, sizeof(gs));
    gs.panel_x = 100;
    gs.panel_y = 200;
    gs.panel_w = 241;
    gs.tab_h = 37;
    gs.active_tab = GUI_TAB_COMBAT;

    ASSERT_INT_EQ("inventory tab hit",
        gui_handle_tab_click(&gs, 100 + 104 + 16, 200 + 18), 1);
    ASSERT_INT_EQ("inventory tab selected", gs.active_tab, GUI_TAB_INVENTORY);
    ASSERT_INT_EQ("inventory tab press starts",
        gs.tab_press_timer[GUI_TAB_INVENTORY], GUI_TAB_PRESS_TICKS);
    gui_tick(&gs);
    ASSERT_INT_EQ("inventory tab press ticks",
        gs.tab_press_timer[GUI_TAB_INVENTORY], GUI_TAB_PRESS_TICKS - 1);
    for (int i = 0; i < GUI_TAB_PRESS_TICKS; i++) {
        gui_tick(&gs);
    }
    ASSERT_INT_EQ("inventory tab press clears",
        gs.tab_press_timer[GUI_TAB_INVENTORY], 0);
    ASSERT_INT_EQ("spellbook tab hit",
        gui_handle_tab_click(&gs, 100 + 203 + 16, 200 + 18), 1);
    ASSERT_INT_EQ("spellbook tab selected", gs.active_tab, GUI_TAB_SPELLBOOK);
    ASSERT_INT_EQ("bottom options tab is presentation only",
        gui_handle_tab_click(&gs, 100 + 137 + 1, 200 + 298 + 1), 0);

    int gx = 0;
    int gy = 0;
    int cell = 0;
    int gap = 0;
    gui_prayer_grid_metrics(&gs, &gx, &gy, &cell, &gap);
    ASSERT_INT_EQ("prayer grid x", gx, 133);
    ASSERT_INT_EQ("prayer grid y", gy, 245);
    ASSERT_INT_EQ("prayer cell size", cell, 34);
    ASSERT_INT_EQ("prayer gap", gap, 2);
}

static void test_spellbook_icon_fit_preserves_projectile_aspect(void) {
    printf("--- spellbook icon fit preserves projectile aspect ---\n");

    GuiState gs;
    memset(&gs, 0, sizeof(gs));
    gs.panel_x = 100;
    gs.panel_y = 200;
    gs.panel_w = 241;
    gs.tab_h = 37;

    int gx = 0;
    int gy = 0;
    int cell = 0;
    int gap = 0;
    gui_spell_grid_metrics(&gs, &gx, &gy, &cell, &gap);
    ASSERT_INT_EQ("spell grid x", gx, 144);
    ASSERT_INT_EQ("spell grid y", gy, 245);
    ASSERT_INT_EQ("spell cell size", cell, 34);
    ASSERT_INT_EQ("spell gap", gap, 5);

    Rectangle fit = gui_texture_fit_rect(24, 24, (Rectangle){0, 0, 30, 30}, 24, 24);
    assert_rect_eq("ancient spell fit", fit, (Rectangle){3, 3, 24, 24});

    fit = gui_texture_fit_rect(40, 40, (Rectangle){0, 0, 30, 30}, 22, 22);
    assert_rect_eq("standard spell fit", fit, (Rectangle){4, 4, 22, 22});

    ASSERT_INT_EQ("ancient spellbook grid count", GUI_SPELL_GRID_COUNT, 24);
    for (int i = 0; i < GUI_SPELL_GRID_COUNT; i++) {
        ASSERT_INT_EQ("ancient grid excludes vengeance",
            GUI_SPELL_GRID[i].idx != GUI_SPELL_VENGEANCE, 1);
    }
    ASSERT_INT_EQ("first teleport slot", GUI_SPELL_GRID[16].idx, GUI_SPELL_PADDEWWA_TELEPORT);
    ASSERT_INT_EQ("last teleport slot", GUI_SPELL_GRID[23].idx, GUI_SPELL_GHORROCK_TELEPORT);
    ASSERT_INT_EQ("paddewwa on sprite", gui_spell_on_sprite_id(GUI_SPELL_PADDEWWA_TELEPORT), 341);
    ASSERT_INT_EQ("ghorrock off sprite", gui_spell_off_sprite_id(GUI_SPELL_GHORROCK_TELEPORT), 398);
    ASSERT_INT_EQ("smoke burst is inert", gui_spell_castable(GUI_SPELL_SMOKE_BURST), 0);
    ASSERT_INT_EQ("shadow burst is inert", gui_spell_castable(GUI_SPELL_SHADOW_BURST), 0);
    ASSERT_INT_EQ("blood burst is castable", gui_spell_castable(GUI_SPELL_BLOOD_BURST), 1);
    ASSERT_INT_EQ("ice burst is castable", gui_spell_castable(GUI_SPELL_ICE_BURST), 1);
    ASSERT_INT_EQ("ancient teleport is inert", gui_spell_castable(GUI_SPELL_PADDEWWA_TELEPORT), 0);
    ASSERT_INT_EQ("vengeance is not ancient castable", gui_spell_castable(GUI_SPELL_VENGEANCE), 0);
}

static void test_prayer_sprite_mapping_matches_player_state(void) {
    printf("--- prayer sprite mapping matches player state ---\n");

    Player p;
    memset(&p, 0, sizeof(p));

    p.prayer = PRAYER_PROTECT_MAGIC;
    ASSERT_INT_EQ("protect magic active",
        gui_prayer_is_active(GUI_PRAY_PROTECT_MAGIC, &p), 1);
    ASSERT_INT_EQ("protect ranged inactive",
        gui_prayer_is_active(GUI_PRAY_PROTECT_MISSILES, &p), 0);
    ASSERT_INT_EQ("protect melee inactive",
        gui_prayer_is_active(GUI_PRAY_PROTECT_MELEE, &p), 0);
    ASSERT_INT_EQ("retribution inactive",
        gui_prayer_is_active(GUI_PRAY_RETRIBUTION, &p), 0);
    ASSERT_INT_EQ("redemption inactive",
        gui_prayer_is_active(GUI_PRAY_REDEMPTION, &p), 0);
    ASSERT_INT_EQ("smite inactive",
        gui_prayer_is_active(GUI_PRAY_SMITE, &p), 0);
    ASSERT_INT_EQ("protect magic on sprite",
        gui_prayer_on_sprite_id(GUI_PRAY_PROTECT_MAGIC), 127);
    ASSERT_INT_EQ("protect magic off sprite",
        gui_prayer_off_sprite_id(GUI_PRAY_PROTECT_MAGIC), 147);
    ASSERT_INT_EQ("protect ranged on sprite",
        gui_prayer_on_sprite_id(GUI_PRAY_PROTECT_MISSILES), 128);
    ASSERT_INT_EQ("protect melee on sprite",
        gui_prayer_on_sprite_id(GUI_PRAY_PROTECT_MELEE), 129);
    ASSERT_INT_EQ("smite on sprite",
        gui_prayer_on_sprite_id(GUI_PRAY_SMITE), 132);
    ASSERT_INT_EQ("redemption on sprite",
        gui_prayer_on_sprite_id(GUI_PRAY_REDEMPTION), 130);

    p.prayer = PRAYER_NONE;
    p.offensive_prayer = OFFENSIVE_PRAYER_RIGOUR;
    ASSERT_INT_EQ("rigour active", gui_prayer_is_active(GUI_PRAY_RIGOUR, &p), 1);
    ASSERT_INT_EQ("augury inactive", gui_prayer_is_active(GUI_PRAY_AUGURY, &p), 0);
    ASSERT_INT_EQ("rigour on sprite", gui_prayer_on_sprite_id(GUI_PRAY_RIGOUR), 1420);
    ASSERT_INT_EQ("augury on sprite", gui_prayer_on_sprite_id(GUI_PRAY_AUGURY), 1421);
}

static void test_minimap_geometry_matches_runec_reference(void) {
    printf("--- minimap geometry matches runec reference ---\n");

    ASSERT_INT_EQ("map container width", GUI_MAP_CONTAINER_W, 211);
    ASSERT_INT_EQ("map container height", GUI_MAP_CONTAINER_H, 207);
    ASSERT_INT_EQ("minimap x", GUI_MINIMAP_X, 53);
    ASSERT_INT_EQ("minimap y", GUI_MINIMAP_Y, 8);
    ASSERT_INT_EQ("minimap width", GUI_MINIMAP_W, 152);
    ASSERT_INT_EQ("minimap height", GUI_MINIMAP_H, 152);
    ASSERT_INT_EQ("minimap mask center x2", (int)(GUI_MINIMAP_MASK_CENTER * 2.0f), 151);
    ASSERT_INT_EQ("minimap mask radius", (int)GUI_MINIMAP_MASK_RADIUS, 76);
    ASSERT_INT_EQ("compass x", GUI_COMPASS_X, 34);
    ASSERT_INT_EQ("compass y", GUI_COMPASS_Y, 5);
    ASSERT_INT_EQ("xp orb x", GUI_ORBS_X + GUI_XP_X, 0);
    ASSERT_INT_EQ("xp orb y", GUI_ORBS_Y + GUI_XP_Y, 27);
    ASSERT_INT_EQ("hp orb x", GUI_ORBS_X + GUI_HP_X, 0);
    ASSERT_INT_EQ("hp orb y", GUI_ORBS_Y + GUI_HP_Y, 47);
    ASSERT_INT_EQ("prayer orb x", GUI_ORBS_X + GUI_PRAYER_X, 0);
    ASSERT_INT_EQ("prayer orb y", GUI_ORBS_Y + GUI_PRAYER_Y, 81);
    ASSERT_INT_EQ("run orb x", GUI_ORBS_X + GUI_RUN_X, 10);
    ASSERT_INT_EQ("run orb y", GUI_ORBS_Y + GUI_RUN_Y, 113);
    ASSERT_INT_EQ("spec orb x", GUI_ORBS_X + GUI_SPEC_X, 32);
    ASSERT_INT_EQ("spec orb y", GUI_ORBS_Y + GUI_SPEC_Y, 138);
    ASSERT_INT_EQ("worldmap x", GUI_ORBS_X + GUI_WORLDMAP_X, 177);
    ASSERT_INT_EQ("worldmap y", GUI_ORBS_Y + GUI_WORLDMAP_Y, 147);
}

static void test_spellbook_tab_uses_ancient_icon(void) {
    printf("--- spellbook tab uses ancient icon ---\n");

    int count = (int)(sizeof(GUI_SIDE_STONES) / sizeof(GUI_SIDE_STONES[0]));
    const GuiSideStoneRef* spellbook = NULL;
    for (int i = 0; i < count; i++) {
        if (GUI_SIDE_STONES[i].logical_tab == GUI_TAB_SPELLBOOK) {
            spellbook = &GUI_SIDE_STONES[i];
            break;
        }
    }

    ASSERT_INT_EQ("spellbook tab ref exists", spellbook != NULL, 1);
    ASSERT_INT_EQ("spellbook tab ancient icon",
        strcmp(spellbook->icon_asset, "side_icon_magic_ancient"), 0);
    ASSERT_INT_EQ("standard spellbook icon remains available",
        file_exists("ocean/osrs/data/sprites/gui/side_icon_magic.png"), 1);
    ASSERT_INT_EQ("ancient spellbook icon exists",
        file_exists("ocean/osrs/data/sprites/gui/side_icon_magic_ancient.png"), 1);
}

static void test_human_combat_clicks_use_runec_geometry(void) {
    printf("--- human combat clicks use runec geometry ---\n");

    GuiState gs;
    Player p;
    HumanInput hi;
    memset(&gs, 0, sizeof(gs));
    memset(&p, 0, sizeof(p));
    human_input_init(&hi);

    gs.panel_x = 100;
    gs.panel_y = 200;
    gs.panel_w = 241;
    gs.tab_h = 37;
    p.equipped[GEAR_SLOT_WEAPON] = ITEM_TWISTED_BOW;

    human_handle_combat_click(&hi, &gs, &p, 100 + 25 + 102 + 1, 200 + 37 + 46 + 1);
    ASSERT_INT_EQ("ranged rapid style selected", p.fight_style, FIGHT_STYLE_RAPID);

    human_handle_combat_click(&hi, &gs, &p, 100 + 25 + 20 + 1, 200 + 37 + 200 + 1);
    ASSERT_INT_EQ("spec click queued", hi.pending_spec, 1);

    p.equipped[GEAR_SLOT_WEAPON] = ITEM_KODAI_WAND;
    human_handle_combat_click(&hi, &gs, &p, 100 + 25 + 20 + 1, 200 + 37 + 153 + 1);
    ASSERT_INT_EQ("autocast selector opens", gs.autocast_selector_open, 1);
    human_handle_combat_click(&hi, &gs, &p, 100 + 25 + 100 + 1, 200 + 37 + 182 + 1);
    ASSERT_INT_EQ("ice autocast selected", p.autocast_spell, ENCOUNTER_SPELL_ICE);
    ASSERT_INT_EQ("autocast selector closes", gs.autocast_selector_open, 0);

    human_input_destroy(&hi);
}

static void test_human_gui_context_action_helpers(void) {
    printf("--- human gui context action helpers ---\n");

    GuiState gs;
    Player p;
    HumanInput hi;
    memset(&gs, 0, sizeof(gs));
    memset(&p, 0, sizeof(p));
    human_input_init(&hi);

    hi.enabled = 1;
    gs.panel_x = 100;
    gs.panel_y = 200;
    gs.panel_w = 241;
    gs.tab_h = 37;

    gs.inv_grid[0].type = INV_SLOT_EQUIPMENT;
    gs.inv_grid[0].item_db_idx = ITEM_TWISTED_BOW;
    ASSERT_INT_EQ("weapon context action label",
        strcmp(gui_inv_primary_action_label(&gs.inv_grid[0]), "Wield"), 0);
    ASSERT_INT_EQ("equipment context display name",
        strcmp(gui_inv_slot_display_name(&gs.inv_grid[0]), "T bow"), 0);
    gs.inv_grid[1].type = INV_SLOT_BASTION_POT;
    ASSERT_INT_EQ("potion context action label",
        strcmp(gui_inv_primary_action_label(&gs.inv_grid[1]), "Drink"), 0);
    ASSERT_INT_EQ("potion context display name",
        strcmp(gui_inv_slot_display_name(&gs.inv_grid[1]), "Bastion potion"), 0);

    int gx = 0;
    int gy = 0;
    int cell = 0;
    int gap = 0;
    gui_prayer_grid_metrics(&gs, &gx, &gy, &cell, &gap);
    int idx = GUI_PRAY_PROTECT_MISSILES;
    int pray_x = gx + (idx % GUI_PRAYER_GRID_COLS) * (cell + gap) + 1;
    int pray_y = gy + (idx / GUI_PRAYER_GRID_COLS) * (cell + gap) + 1;
    ASSERT_INT_EQ("context prayer hit test",
        human_gui_prayer_idx_at(&gs, pray_x, pray_y), GUI_PRAY_PROTECT_MISSILES);
    ASSERT_INT_EQ("context prayer name",
        strcmp(human_gui_prayer_name(GUI_PRAY_PROTECT_MISSILES), "Protect from Missiles"), 0);
    ASSERT_INT_EQ("context prayer apply returns handled",
        human_apply_prayer_idx(&hi, &p, GUI_PRAY_PROTECT_MISSILES), 1);
    ASSERT_INT_EQ("context prayer command queued",
        hi.commands.items[0].overhead_prayer, ENCOUNTER_OVERHEAD_SET_REFRESH_RANGED);

    gui_spell_grid_metrics(&gs, &gx, &gy, &cell, &gap);
    int spell_grid_slot = 15;
    int spell_x = gx + (spell_grid_slot % GUI_SPELL_GRID_COLS) * (cell + gap) + 1;
    int spell_y = gy + (spell_grid_slot / GUI_SPELL_GRID_COLS) * (cell + gap) + 1;
    ASSERT_INT_EQ("context spell hit test",
        human_gui_spell_idx_at(&gs, spell_x, spell_y), GUI_SPELL_ICE_BARRAGE);
    ASSERT_INT_EQ("context spell name",
        strcmp(human_gui_spell_name(GUI_SPELL_ICE_BARRAGE), "Ice Barrage"), 0);
    ASSERT_INT_EQ("context spell apply returns handled",
        human_select_spell_idx(&hi, GUI_SPELL_ICE_BARRAGE), 1);
    ASSERT_INT_EQ("context spell cursor selected", hi.cursor_mode, CURSOR_SPELL_TARGET);
    ASSERT_INT_EQ("context spell attack selected", hi.selected_spell, ATTACK_ICE);

    p.equipped[GEAR_SLOT_WEAPON] = ITEM_KODAI_WAND;
    int style_x = 100 + 25 + 20 + 1;
    int style_y = 200 + 37 + 46 + 1;
    ASSERT_INT_EQ("context combat style hit test",
        human_gui_combat_style_index_at(&gs, &p, style_x, style_y), 0);
    human_apply_combat_style(&hi, &gs, &p, FIGHT_STYLE_DEFENSIVE_AUTOCAST);
    ASSERT_INT_EQ("context combat style command",
        hi.commands.items[1].kind, HUMAN_COMMAND_FIGHT_STYLE);
    ASSERT_INT_EQ("context autocast command from defensive style",
        hi.commands.items[2].kind, HUMAN_COMMAND_SET_AUTOCAST);
    ASSERT_INT_EQ("context autocast defensive",
        hi.commands.items[2].autocast_defensive, 1);

    int ac_x = 100 + 25 + 20 + 1;
    int ac_y = 200 + 37 + 153 + 1;
    ASSERT_INT_EQ("context autocast button hit",
        human_gui_autocast_button_hit(&gs, &p, ac_x, ac_y), 1);
    human_apply_autocast_spell(&hi, &gs, &p, ENCOUNTER_SPELL_ICE, 1);
    ASSERT_INT_EQ("context explicit autocast command",
        hi.commands.items[3].kind, HUMAN_COMMAND_SET_AUTOCAST);
    ASSERT_INT_EQ("context explicit autocast spell",
        hi.commands.items[3].autocast_spell, ENCOUNTER_SPELL_ICE);

    int spec_x = 100 + 25 + 20 + 1;
    int spec_y = 200 + 37 + 200 + 1;
    ASSERT_INT_EQ("context spec hit", human_gui_spec_hit(&gs, spec_x, spec_y), 1);
    human_apply_spec_toggle(&hi);
    ASSERT_INT_EQ("context spec command",
        hi.commands.items[4].kind, HUMAN_COMMAND_SPEC_TOGGLE);

    human_input_destroy(&hi);
}

static void test_human_prayer_clicks_match_prayer_grid(void) {
    printf("--- human prayer clicks match prayer grid ---\n");

    GuiState gs;
    Player p;
    HumanInput hi;
    memset(&gs, 0, sizeof(gs));
    memset(&p, 0, sizeof(p));
    human_input_init(&hi);

    gs.panel_x = 100;
    gs.panel_y = 200;
    gs.panel_w = 241;
    gs.tab_h = 37;

    int gx = 0;
    int gy = 0;
    int cell = 0;
    int gap = 0;
    gui_prayer_grid_metrics(&gs, &gx, &gy, &cell, &gap);

    int idx = GUI_PRAY_PROTECT_MAGIC;
    int col = idx % GUI_PRAYER_GRID_COLS;
    int row = idx / GUI_PRAYER_GRID_COLS;
    int x = gx + col * (cell + gap) + 1;
    int y = gy + row * (cell + gap) + 1;

    human_handle_prayer_click(&hi, &gs, &p, x, y);
    ASSERT_INT_EQ("protect magic click queues set",
        hi.pending_prayer, ENCOUNTER_OVERHEAD_SET_REFRESH_MAGIC);
    ASSERT_INT_EQ("protect magic command kind",
        hi.commands.items[0].kind, HUMAN_COMMAND_OVERHEAD_PRAYER);
    ASSERT_INT_EQ("protect magic command queued",
        hi.commands.items[0].overhead_prayer, ENCOUNTER_OVERHEAD_SET_REFRESH_MAGIC);

    p.prayer = PRAYER_PROTECT_MAGIC;
    human_handle_prayer_click(&hi, &gs, &p, x, y);
    ASSERT_INT_EQ("protect magic active click queues off",
        hi.pending_prayer, ENCOUNTER_OVERHEAD_OFF);
    ASSERT_INT_EQ("second prayer command queued", hi.commands.count, 2);
    ASSERT_INT_EQ("second command is off",
        hi.commands.items[1].overhead_prayer, ENCOUNTER_OVERHEAD_OFF);

    human_input_destroy(&hi);
}

static void test_human_spell_clicks_match_ancient_grid(void) {
    printf("--- human spell clicks match ancient grid ---\n");

    GuiState gs;
    HumanInput hi;
    memset(&gs, 0, sizeof(gs));
    human_input_init(&hi);

    gs.panel_x = 100;
    gs.panel_y = 200;
    gs.panel_w = 241;
    gs.tab_h = 37;

    int gx = 0;
    int gy = 0;
    int cell = 0;
    int gap = 0;
    gui_spell_grid_metrics(&gs, &gx, &gy, &cell, &gap);

    int blood_burst = 6;
    int blood_x = gx + (blood_burst % GUI_SPELL_GRID_COLS) * (cell + gap) + 1;
    int blood_y = gy + (blood_burst / GUI_SPELL_GRID_COLS) * (cell + gap) + 1;
    human_handle_spell_click(&hi, &gs, blood_x, blood_y);
    ASSERT_INT_EQ("blood spell selects target cursor", hi.cursor_mode, CURSOR_SPELL_TARGET);
    ASSERT_INT_EQ("blood spell selects blood attack", hi.selected_spell, ATTACK_BLOOD);
    ASSERT_INT_EQ("blood spell highlight records exact cell",
        hi.selected_spell_gui_idx, GUI_SPELL_BLOOD_BURST);

    hi.cursor_mode = CURSOR_NORMAL;
    hi.selected_spell = 0;
    hi.selected_spell_gui_idx = -1;
    int smoke_burst = 4;
    int smoke_x = gx + (smoke_burst % GUI_SPELL_GRID_COLS) * (cell + gap) + 1;
    int smoke_y = gy + (smoke_burst / GUI_SPELL_GRID_COLS) * (cell + gap) + 1;
    human_handle_spell_click(&hi, &gs, smoke_x, smoke_y);
    ASSERT_INT_EQ("smoke spell stays inert", hi.cursor_mode, CURSOR_NORMAL);

    int paddewwa = 16;
    int paddewwa_x = gx + (paddewwa % GUI_SPELL_GRID_COLS) * (cell + gap) + 1;
    int paddewwa_y = gy + (paddewwa / GUI_SPELL_GRID_COLS) * (cell + gap) + 1;
    human_handle_spell_click(&hi, &gs, paddewwa_x, paddewwa_y);
    ASSERT_INT_EQ("ancient teleport stays inert", hi.cursor_mode, CURSOR_NORMAL);

    human_input_destroy(&hi);
}

static void test_gui_update_tracks_bastion_and_stamina(void) {
    printf("--- gui update tracks inferno potion deltas ---\n");

    GuiState gs;
    Player p;
    memset(&gs, 0, sizeof(gs));
    memset(&p, 0, sizeof(p));

    p.bastion_doses = 8;
    p.stamina_doses = 4;
    gui_populate_inventory(&gs, &p);

    int bastion_slot = find_slot_of_type(&gs, INV_SLOT_BASTION_POT);
    int stamina_slot = find_slot_of_type(&gs, INV_SLOT_STAMINA_POT);

    gs.human_clicked_inv_slot = bastion_slot;
    p.bastion_doses = 7;
    gui_update_inventory(&gs, &p);

    ASSERT_INT_EQ("bastion click latch clears after use", gs.human_clicked_inv_slot, -1);
    ASSERT_INT_EQ("bastion snapshot updates", gs.inv_prev_bastion_doses, 7);
    ASSERT_INT_EQ("bastion keeps two vials after one sip", count_slots_of_type(&gs, INV_SLOT_BASTION_POT), 2);
    ASSERT_INT_EQ("bastion sprite downgrades to 3-dose",
        gs.inv_grid[bastion_slot].osrs_id, gui_consumable_osrs_id(INV_SLOT_BASTION_POT, 3));

    gs.human_clicked_inv_slot = stamina_slot;
    p.stamina_doses = 3;
    gui_update_inventory(&gs, &p);

    ASSERT_INT_EQ("stamina click latch clears after use", gs.human_clicked_inv_slot, -1);
    ASSERT_INT_EQ("stamina snapshot updates", gs.inv_prev_stamina_doses, 3);
    ASSERT_INT_EQ("stamina sprite downgrades to 3-dose",
        gs.inv_grid[stamina_slot].osrs_id, gui_consumable_osrs_id(INV_SLOT_STAMINA_POT, 3));
}

static void test_gui_reset_helper_clears_inventory_interaction_state(void) {
    printf("--- gui reset helper clears inventory interaction state ---\n");

    GuiState gs;
    memset(&gs, 0, sizeof(gs));
    gs.inv_grid_dirty = 0;
    gs.human_clicked_inv_slot = 7;
    gs.inv_dim_slot = 3;
    gs.inv_dim_timer = 9;
    gs.inv_drag_active = 1;
    gs.inv_drag_src_slot = 12;
    gs.inv_drag_start_x = 100;
    gs.inv_drag_start_y = 120;
    gs.inv_drag_mouse_x = 130;
    gs.inv_drag_mouse_y = 140;

    gui_reset_inventory_ui_state(&gs);

    ASSERT_INT_EQ("grid marked dirty", gs.inv_grid_dirty, 1);
    ASSERT_INT_EQ("clicked slot cleared", gs.human_clicked_inv_slot, -1);
    ASSERT_INT_EQ("dim slot cleared", gs.inv_dim_slot, -1);
    ASSERT_INT_EQ("dim timer cleared", gs.inv_dim_timer, 0);
    ASSERT_INT_EQ("drag deactivated", gs.inv_drag_active, 0);
    ASSERT_INT_EQ("drag source cleared", gs.inv_drag_src_slot, -1);
}

static void test_gui_reset_rebuild_restores_potions(void) {
    printf("--- gui reset rebuild restores inferno potions ---\n");

    GuiState gs;
    Player p;
    memset(&gs, 0, sizeof(gs));
    memset(&p, 0, sizeof(p));

    p.bastion_doses = 8;
    p.stamina_doses = 4;
    gui_populate_inventory(&gs, &p);

    p.bastion_doses = 0;
    p.stamina_doses = 0;
    gui_update_inventory(&gs, &p);
    ASSERT_INT_EQ("bastion gone after depletion", find_slot_of_type(&gs, INV_SLOT_BASTION_POT), -1);
    ASSERT_INT_EQ("stamina gone after depletion", find_slot_of_type(&gs, INV_SLOT_STAMINA_POT), -1);

    p.bastion_doses = 8;
    p.stamina_doses = 4;
    gui_reset_inventory_ui_state(&gs);
    if (gs.inv_grid_dirty) {
        gui_populate_inventory(&gs, &p);
        gs.inv_grid_dirty = 0;
    }

    ASSERT_INT_EQ("bastion restored after rebuild", count_slots_of_type(&gs, INV_SLOT_BASTION_POT), 2);
    ASSERT_INT_EQ("stamina restored after rebuild", find_slot_of_type(&gs, INV_SLOT_STAMINA_POT) >= 0, 1);
    ASSERT_INT_EQ("bastion snapshot restored", gs.inv_prev_bastion_doses, 8);
    ASSERT_INT_EQ("stamina snapshot restored", gs.inv_prev_stamina_doses, 4);
}

static void test_gui_populate_late_start_inferno_supplies(void) {
    printf("--- gui populate late-start inferno supplies ---\n");

    GuiState gs;
    Player p;
    memset(&gs, 0, sizeof(gs));
    memset(&p, 0, sizeof(p));

    InfSupplyDoses full = inf_full_starting_supplies();
    InfSupplyDoses late = inf_supplies_for_start_wave(full, INF_NUM_WAVES - 1, 1.0f);
    p.brew_doses = late.brew_doses;
    p.restore_doses = late.restore_doses;
    p.bastion_doses = late.bastion_doses;
    p.stamina_doses = late.stamina_doses;

    gui_populate_inventory(&gs, &p);

    ASSERT_INT_EQ("late-start brew vial count",
        count_slots_of_type(&gs, INV_SLOT_BREW), expected_vial_count(late.brew_doses));
    ASSERT_INT_EQ("late-start restore vial count",
        count_slots_of_type(&gs, INV_SLOT_RESTORE), expected_vial_count(late.restore_doses));
    ASSERT_INT_EQ("late-start bastion vial count",
        count_slots_of_type(&gs, INV_SLOT_BASTION_POT), expected_vial_count(late.bastion_doses));
    ASSERT_INT_EQ("late-start stamina vial count",
        count_slots_of_type(&gs, INV_SLOT_STAMINA_POT), expected_vial_count(late.stamina_doses));
}

static void test_human_equipment_click_queues_without_mutating_player(void) {
    printf("--- human equipment click queues without mutating player ---\n");

    GuiState gs;
    Player p;
    HumanInput hi;
    memset(&gs, 0, sizeof(gs));
    memset(&p, 0, sizeof(p));
    human_input_init(&hi);

    hi.enabled = 1;
    p.equipped[GEAR_SLOT_WEAPON] = ITEM_KODAI_WAND;
    gs.inv_grid[0].type = INV_SLOT_EQUIPMENT;
    gs.inv_grid[0].item_db_idx = ITEM_TOXIC_BLOWPIPE;
    gs.inv_grid[0].osrs_id = ITEM_DATABASE[ITEM_TOXIC_BLOWPIPE].item_id;

    InvAction action = gui_inv_click(&gs, &p, 0, &hi);

    ASSERT_INT_EQ("equipment click returns equip action", action, INV_ACTION_EQUIP);
    ASSERT_INT_EQ("weapon not mutated before tick",
        p.equipped[GEAR_SLOT_WEAPON], ITEM_KODAI_WAND);
    ASSERT_INT_EQ("one command queued", hi.commands.count, 1);
    ASSERT_INT_EQ("queued equip command",
        hi.commands.items[0].kind, HUMAN_COMMAND_EQUIP_INVENTORY_ITEM);
    ASSERT_INT_EQ("queued source slot", hi.commands.items[0].inventory_slot, 0);
    ASSERT_INT_EQ("queued item", hi.commands.items[0].item_db_idx, ITEM_TOXIC_BLOWPIPE);

    human_input_destroy(&hi);
}

static void test_gui_selected_item_click_queues_item_on_item(void) {
    printf("--- gui selected item click queues item on item ---\n");

    GuiState gs;
    Player p;
    HumanInput hi;
    memset(&gs, 0, sizeof(gs));
    memset(&p, 0, sizeof(p));
    human_input_init(&hi);

    hi.enabled = 1;
    gs.inv_grid[0].type = INV_SLOT_EQUIPMENT;
    gs.inv_grid[0].item_db_idx = ITEM_TOXIC_BLOWPIPE;
    gs.inv_grid[0].osrs_id = ITEM_DATABASE[ITEM_TOXIC_BLOWPIPE].item_id;
    gs.inv_grid[1].type = INV_SLOT_EQUIPMENT;
    gs.inv_grid[1].item_db_idx = ITEM_KODAI_WAND;
    gs.inv_grid[1].osrs_id = ITEM_DATABASE[ITEM_KODAI_WAND].item_id;

    ASSERT_INT_EQ("item source selected",
        gui_inv_select_item(&gs, &hi, 0), 1);
    ASSERT_INT_EQ("item target cursor", hi.cursor_mode, CURSOR_ITEM_TARGET);
    ASSERT_INT_EQ("selected source slot", hi.selected_item_inventory_slot, 0);
    ASSERT_INT_EQ("selected source item id",
        hi.selected_item_osrs_id, ITEM_DATABASE[ITEM_TOXIC_BLOWPIPE].item_id);

    InvAction action = gui_inv_click(&gs, &p, 1, &hi);
    ASSERT_INT_EQ("selected item click returns item-on-item",
        action, INV_ACTION_ITEM_ON_ITEM);
    ASSERT_INT_EQ("item-on-item command count", hi.commands.count, 1);
    ASSERT_INT_EQ("item-on-item command kind",
        hi.commands.items[0].kind, HUMAN_COMMAND_ITEM_ON_ITEM);
    ASSERT_INT_EQ("item-on-item source slot", hi.commands.items[0].inventory_slot, 0);
    ASSERT_INT_EQ("item-on-item target slot", hi.commands.items[0].target_inventory_slot, 1);
    ASSERT_INT_EQ("item-on-item source item id",
        hi.commands.items[0].item_osrs_id, ITEM_DATABASE[ITEM_TOXIC_BLOWPIPE].item_id);
    ASSERT_INT_EQ("item target cursor clears", hi.cursor_mode, CURSOR_NORMAL);

    human_input_destroy(&hi);
}

static void test_human_selected_target_widget_helpers_queue_commands(void) {
    printf("--- human selected target widget helpers queue commands ---\n");

    GuiState gs;
    HumanInput hi;
    memset(&gs, 0, sizeof(gs));
    human_input_init(&hi);
    hi.enabled = 1;

    human_select_spell_idx(&hi, GUI_SPELL_ICE_BARRAGE);
    ASSERT_INT_EQ("spell target selected before widget",
        hi.cursor_mode, CURSOR_SPELL_TARGET);
    ASSERT_INT_EQ("spell-on-widget handled",
        human_apply_selected_target_to_widget(&hi,
            osrs_ui_intent_widget_component_id(OSRS_UI_GROUP_MAGIC_SPELLBOOK, 47)), 1);
    ASSERT_INT_EQ("spell-on-widget command count", hi.commands.count, 1);
    ASSERT_INT_EQ("spell-on-widget command kind",
        hi.commands.items[0].kind, HUMAN_COMMAND_SPELL_ON_WIDGET);
    ASSERT_INT_EQ("spell-on-widget spell", hi.commands.items[0].spell, ATTACK_ICE);
    ASSERT_INT_EQ("spell-on-widget gui idx",
        hi.commands.items[0].spell_gui_idx, GUI_SPELL_ICE_BARRAGE);
    ASSERT_INT_EQ("spell-on-widget group",
        osrs_ui_intent_widget_group_id(hi.commands.items[0].widget_component_id),
        OSRS_UI_GROUP_MAGIC_SPELLBOOK);
    ASSERT_INT_EQ("spell target cursor clears", hi.cursor_mode, CURSOR_NORMAL);

    gs.inv_grid[3].type = INV_SLOT_EQUIPMENT;
    gs.inv_grid[3].item_db_idx = ITEM_KODAI_WAND;
    gs.inv_grid[3].osrs_id = ITEM_DATABASE[ITEM_KODAI_WAND].item_id;
    ASSERT_INT_EQ("item source selected before widget",
        gui_inv_select_item(&gs, &hi, 3), 1);
    ASSERT_INT_EQ("item-on-widget handled",
        human_apply_selected_target_to_widget(&hi,
            osrs_ui_intent_widget_component_id(OSRS_UI_GROUP_WORNITEMS, 15)), 1);
    ASSERT_INT_EQ("item-on-widget command count", hi.commands.count, 2);
    ASSERT_INT_EQ("item-on-widget command kind",
        hi.commands.items[1].kind, HUMAN_COMMAND_ITEM_ON_WIDGET);
    ASSERT_INT_EQ("item-on-widget source slot", hi.commands.items[1].inventory_slot, 3);
    ASSERT_INT_EQ("item-on-widget source item id",
        hi.commands.items[1].item_osrs_id, ITEM_DATABASE[ITEM_KODAI_WAND].item_id);
    ASSERT_INT_EQ("item-on-widget group",
        osrs_ui_intent_widget_group_id(hi.commands.items[1].widget_component_id),
        OSRS_UI_GROUP_WORNITEMS);
    ASSERT_INT_EQ("item-on-widget child",
        osrs_ui_intent_widget_child_id(hi.commands.items[1].widget_component_id), 15);

    human_input_destroy(&hi);
}

static void test_budget_item_labels_and_sprites_resolve(void) {
    printf("--- budget item labels and sprites resolve ---\n");

    ASSERT_INT_EQ("dragon hunter wand label",
        strcmp(gui_item_short_name(ITEM_DRAGON_HUNTER_WAND), "DH wand"), 0);
    ASSERT_INT_EQ("echo boots label",
        strcmp(gui_item_short_name(ITEM_ECHO_BOOTS), "Echo boots"), 0);
    ASSERT_INT_EQ("dragon hunter wand sprite exists",
        file_exists(OSRS_ASSET("sprites/items/30070.png")), 1);
    ASSERT_INT_EQ("echo boots sprite exists",
        file_exists(OSRS_ASSET("sprites/items/28945.png")), 1);
}

static void test_runec_ui_asset_aliases_exist(void) {
    printf("--- runec ui asset aliases exist ---\n");

    ASSERT_INT_EQ("side background alias exists",
        file_exists(OSRS_ASSET("sprites/gui/tradebacking_dark.png")), 1);
    ASSERT_INT_EQ("top tab strip alias exists",
        file_exists(OSRS_ASSET("sprites/gui/osrs_stretch_side_topbottom_0.png")), 1);
    ASSERT_INT_EQ("inventory tab icon alias exists",
        file_exists(OSRS_ASSET("sprites/gui/side_icon_inventory.png")), 1);
    ASSERT_INT_EQ("ancient magic tab icon alias exists",
        file_exists(OSRS_ASSET("sprites/gui/side_icon_magic_ancient.png")), 1);
    ASSERT_INT_EQ("combat button alias exists",
        file_exists(OSRS_ASSET("sprites/gui/combatboxes_0.png")), 1);
    ASSERT_INT_EQ("worn equipment icon alias exists",
        file_exists(OSRS_ASSET("sprites/gui/wornicons_11.png")), 1);
    ASSERT_INT_EQ("skill icon alias exists",
        file_exists(OSRS_ASSET("sprites/gui/skill_icon_23.png")), 1);
    ASSERT_INT_EQ("prayer icon alias exists",
        file_exists(OSRS_ASSET("sprites/gui/prayeron_24.png")), 1);
    ASSERT_INT_EQ("magic icon alias exists",
        file_exists(OSRS_ASSET("sprites/gui/magicon_47.png")), 1);
    ASSERT_INT_EQ("standard spell icon alias exists",
        file_exists(OSRS_ASSET("sprites/gui/standard_spell_on_79.png")), 1);
    ASSERT_INT_EQ("ancient ice barrage icon exists",
        file_exists(OSRS_ASSET("sprites/gui/328.png")), 1);
    ASSERT_INT_EQ("ancient ghorrock teleport icon exists",
        file_exists(OSRS_ASSET("sprites/gui/348.png")), 1);
    ASSERT_INT_EQ("ancient ghorrock teleport disabled icon exists",
        file_exists(OSRS_ASSET("sprites/gui/398.png")), 1);
    ASSERT_INT_EQ("minimap surround alias exists",
        file_exists(OSRS_ASSET("sprites/gui/osrs_stretch_mapsurround.png")), 1);
    ASSERT_INT_EQ("compass alias exists",
        file_exists(OSRS_ASSET("sprites/gui/compass.png")), 1);
    ASSERT_INT_EQ("runescape font exists",
        file_exists(OSRS_ASSET("fonts/runescape.ttf")), 1);
    ASSERT_INT_EQ("runescape small font exists",
        file_exists(OSRS_ASSET("fonts/runescape_small.ttf")), 1);
    ASSERT_INT_EQ("item stack variants exist",
        file_exists(OSRS_ASSET("sprites/items/item_stack_variants.tsv")), 1);
}

static void test_runec_stack_quantity_formatting(void) {
    printf("--- runec stack quantity formatting ---\n");

    char text[16];
    gui_format_stack_quantity(99999, text, sizeof(text));
    ASSERT_INT_EQ("low stack text", strcmp(text, "99999"), 0);
    Color color = gui_stack_text_color(99999);
    ASSERT_INT_EQ("low stack is yellow r", color.r, GUI_TEXT_YELLOW.r);
    ASSERT_INT_EQ("low stack is yellow g", color.g, GUI_TEXT_YELLOW.g);

    gui_format_stack_quantity(100000, text, sizeof(text));
    ASSERT_INT_EQ("mid stack text", strcmp(text, "100K"), 0);
    color = gui_stack_text_color(100000);
    ASSERT_INT_EQ("mid stack is white r", color.r, GUI_TEXT_WHITE.r);
    ASSERT_INT_EQ("mid stack is white g", color.g, GUI_TEXT_WHITE.g);

    gui_format_stack_quantity(10000000, text, sizeof(text));
    ASSERT_INT_EQ("large stack text", strcmp(text, "10M"), 0);
    color = gui_stack_text_color(10000000);
    ASSERT_INT_EQ("large stack is green r", color.r, GUI_TEXT_GREEN.r);
    ASSERT_INT_EQ("large stack is green g", color.g, GUI_TEXT_GREEN.g);
}

static void test_runec_stack_variant_selection(void) {
    printf("--- runec stack variant selection ---\n");

    GuiState gs;
    memset(&gs, 0, sizeof(gs));
    gs.item_stack_variants[0] = (GuiItemStackVariant){
        .base_item_id = 45,
        .threshold = 2,
        .display_item_id = 9199,
    };
    gs.item_stack_variants[1] = (GuiItemStackVariant){
        .base_item_id = 45,
        .threshold = 5,
        .display_item_id = 9202,
    };
    gs.item_stack_variant_count = 2;

    ASSERT_INT_EQ("single stack keeps base",
        gui_item_display_id_for_quantity(&gs, 45, 1), 45);
    ASSERT_INT_EQ("threshold stack uses first variant",
        gui_item_display_id_for_quantity(&gs, 45, 2), 9199);
    ASSERT_INT_EQ("between thresholds keeps best variant",
        gui_item_display_id_for_quantity(&gs, 45, 4), 9199);
    ASSERT_INT_EQ("higher threshold uses later variant",
        gui_item_display_id_for_quantity(&gs, 45, 5), 9202);
    ASSERT_INT_EQ("coins use coin display ids",
        gui_item_display_id_for_quantity(&gs, 995, 1000), 1003);
}

static void test_runec_stack_variants_file_loads(void) {
    printf("--- runec stack variants file loads ---\n");

    GuiState gs;
    memset(&gs, 0, sizeof(gs));
    gui_load_item_stack_variants(&gs);

    ASSERT_INT_EQ("stack variant file has rows",
        gs.item_stack_variant_count > 100, 1);
    ASSERT_INT_EQ("arrow stack variant from file",
        gui_item_display_id_for_quantity(&gs, 882, 5), 897);
}

static void test_decoded_ui_clip_rect_math(void) {
    printf("--- decoded ui clip rect math ---\n");

    Rectangle a = {10, 20, 80, 40};
    Rectangle b = {30, 10, 25, 25};
    Rectangle hit = gui_rect_intersect(a, b);
    assert_rect_eq("clip intersection", hit, (Rectangle){30, 20, 25, 15});
    ASSERT_INT_EQ("clip intersection has area", gui_rect_has_area(hit), 1);

    Rectangle miss = gui_rect_intersect(
        (Rectangle){0, 0, 10, 10},
        (Rectangle){10, 10, 10, 10});
    ASSERT_INT_EQ("touching edges have no area", gui_rect_has_area(miss), 0);
}

static void test_decoded_runec_ui_interfaces_resolve(void) {
    printf("--- decoded runec ui interfaces resolve ---\n");

    OsrsUiInterfaceStore store;
    memset(&store, 0, sizeof(store));

    ASSERT_INT_EQ("ui interface binary exists", file_exists(OSRS_ASSET("ui/interfaces.bin")), 1);
    ASSERT_INT_EQ("ui interface binary loads", load_test_ui_interfaces(&store), 1);
    ASSERT_INT_EQ("wornitems group exists",
        osrs_ui_interface_group(&store, "wornitems") != NULL, 1);
    ASSERT_INT_EQ("combat interface group exists",
        osrs_ui_interface_group(&store, "combat_interface") != NULL, 1);
    ASSERT_INT_EQ("stats group exists",
        osrs_ui_interface_group(&store, "stats") != NULL, 1);

    Rectangle mount = {125, 237, 190, 261};
    Rectangle rect = {0};
    ASSERT_INT_EQ("worn slot0 rect resolves",
        osrs_ui_interfaces_component_rect(&store, "wornitems", "slot0", mount, &rect), 1);
    assert_rect_eq("worn slot0 rect", rect, (Rectangle){202, 241, 36, 36});

    ASSERT_INT_EQ("worn ammo rect resolves",
        osrs_ui_interfaces_component_rect(&store, "wornitems", "slot13", mount, &rect), 1);
    assert_rect_eq("worn ammo rect", rect, (Rectangle){243, 280, 36, 36});

    ASSERT_INT_EQ("combat rapid rect resolves",
        osrs_ui_interfaces_component_rect(&store, "combat_interface", "1", mount, &rect), 1);
    assert_rect_eq("combat rapid rect", rect, (Rectangle){227, 283, 68, 47});

    ASSERT_INT_EQ("stats attack rect resolves",
        osrs_ui_interfaces_component_rect(&store, "stats", "attack", mount, &rect), 1);
    assert_rect_eq("stats attack rect", rect, (Rectangle){126, 238, 62, 30});

    ASSERT_INT_EQ("stats total rect resolves",
        osrs_ui_interfaces_component_rect(&store, "stats", "total", mount, &rect), 1);
    assert_rect_eq("stats total rect", rect, (Rectangle){125, 478, 190, 19});

    OsrsUiHitResult hit;
    memset(&hit, 0, sizeof(hit));
    ASSERT_INT_EQ("worn slot0 hit resolves",
        osrs_ui_interfaces_hit_test(
            &store,
            "wornitems",
            mount,
            (Vector2){203, 242},
            &hit),
        1);
    ASSERT_INT_EQ("worn slot0 hit component", strcmp(hit.name, "slot0"), 0);
    ASSERT_INT_EQ("worn slot0 hit file id", hit.file_id, 15);

    osrs_ui_interfaces_unload(&store);
}

static void test_gui_uses_decoded_runec_panel_rects(void) {
    printf("--- gui uses decoded runec panel rects ---\n");

    GuiState gs;
    memset(&gs, 0, sizeof(gs));
    gs.panel_x = 100;
    gs.panel_y = 200;
    gs.panel_w = 241;
    gs.tab_h = 37;

    ASSERT_INT_EQ("ui interface binary loads on gui state", load_test_ui_interfaces(&gs.ui_interfaces), 1);

    Rectangle ammo = gui_side_component_rect(
        &gs,
        "wornitems",
        "slot13",
        (Rectangle){133, 43, 36, 36});
    assert_rect_eq("decoded ammo rect", ammo, (Rectangle){243, 280, 36, 36});

    Rectangle rapid = gui_combat_style_rect(&gs, 1);
    assert_rect_eq("decoded combat rapid rect", rapid, (Rectangle){227, 283, 68, 47});

    Rectangle attack = gui_side_component_rect(
        &gs,
        "stats",
        "attack",
        (Rectangle){1, 1, 62, 30});
    assert_rect_eq("decoded stats attack rect", attack, (Rectangle){126, 238, 62, 30});

    Rectangle sailing = gui_side_component_rect(
        &gs,
        "stats",
        "sailing",
        (Rectangle){127, 211, 62, 32});
    assert_rect_eq("decoded stats sailing rect", sailing, (Rectangle){252, 448, 62, 32});

    osrs_ui_interfaces_unload(&gs.ui_interfaces);
}

int main(void) {
    test_gui_populate_tracks_bastion_and_stamina();
    test_inventory_slot_geometry_matches_osrs_reference();
    test_side_panel_geometry_matches_runec_reference();
    test_spellbook_icon_fit_preserves_projectile_aspect();
    test_prayer_sprite_mapping_matches_player_state();
    test_minimap_geometry_matches_runec_reference();
    test_spellbook_tab_uses_ancient_icon();
    test_human_combat_clicks_use_runec_geometry();
    test_human_gui_context_action_helpers();
    test_human_prayer_clicks_match_prayer_grid();
    test_human_spell_clicks_match_ancient_grid();
    test_gui_update_tracks_bastion_and_stamina();
    test_gui_reset_helper_clears_inventory_interaction_state();
    test_gui_reset_rebuild_restores_potions();
    test_gui_populate_late_start_inferno_supplies();
    test_human_equipment_click_queues_without_mutating_player();
    test_gui_selected_item_click_queues_item_on_item();
    test_human_selected_target_widget_helpers_queue_commands();
    test_budget_item_labels_and_sprites_resolve();
    test_runec_ui_asset_aliases_exist();
    test_runec_stack_quantity_formatting();
    test_runec_stack_variant_selection();
    test_runec_stack_variants_file_loads();
    test_decoded_ui_clip_rect_math();
    test_decoded_runec_ui_interfaces_resolve();
    test_gui_uses_decoded_runec_panel_rects();

    printf("\n%d/%d tests passed", tests_passed, tests_run);
    if (tests_failed > 0) {
        printf(" (%d failed)\n", tests_failed);
        return 1;
    }
    printf("\n");
    return 0;
}
