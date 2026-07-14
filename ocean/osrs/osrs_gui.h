/**
 * @fileoverview OSRS-style GUI panel system for the debug viewer.
 *
 * Renders inventory, equipment, prayer, combat, and spellbook panels
 * using real sprites exported from the OSRS cache (index 8). Tab bar
 * at the TOP matches the real OSRS fixed-mode client (7 tabs).
 *
 * Sprite sources (exported by scripts/export_sprites_modern.py):
 *   - equipment slot backgrounds: sprite IDs 156-165, 170
 *   - prayer icons (enabled/disabled): sprite IDs 115-154, 502-509, 945-951, 1420-1425
 *   - tab icons: sprite IDs 168, 898, 899, 900, 901, 779, 780
 *   - spell icons: sprite IDs 325-348, 375-398, 557, 561, 564, 607, 611, 614
 *   - special attack bar: sprite ID 657
 *
 * Layout constants derived from OSRS client widget definitions:
 *   - inventory: 4 columns x 7 rows, 36x32 item sprites
 *   - equipment: 11 slots in paperdoll layout (interface 387)
 *   - prayer: 5 columns x 6 rows grid (interface 541)
 *   - combat: 4 attack style buttons + special bar (interface 593)
 *   - spellbook: grid layout (interface 218)
 */

#ifndef OSRS_GUI_H
#define OSRS_GUI_H

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "osrs_asset_raylib.h"
#include "osrs_human_input_types.h"

#if __has_include("raylib.h")
#include "raylib.h"
#elif __has_include("raylib-5.5_macos/include/raylib.h")
#include "raylib-5.5_macos/include/raylib.h"
#else
#error "raylib.h not found"
#endif
#include "osrs_types.h"
#include "osrs_items.h"
#include "osrs_pvp_gear.h"
#include "osrs_inventory_clicks.h"
#include "osrs_ui_interfaces.h"


#define GUI_BG_DARK     CLITERAL(Color){ 62, 53, 41, 255 }
#define GUI_BG_MEDIUM   CLITERAL(Color){ 75, 67, 54, 255 }
#define GUI_BG_SLOT     CLITERAL(Color){ 56, 48, 38, 255 }
#define GUI_BG_SLOT_HL  CLITERAL(Color){ 90, 80, 60, 255 }
#define GUI_BORDER      CLITERAL(Color){ 42, 36, 28, 255 }
#define GUI_BORDER_LT   CLITERAL(Color){ 100, 90, 70, 255 }
#define GUI_TEXT_YELLOW  CLITERAL(Color){ 255, 255, 0, 255 }
#define GUI_TEXT_ORANGE  CLITERAL(Color){ 255, 152, 31, 255 }
#define GUI_TEXT_WHITE   CLITERAL(Color){ 255, 255, 255, 255 }
#define GUI_TEXT_GREEN   CLITERAL(Color){ 0, 255, 0, 255 }
#define GUI_TEXT_RED     CLITERAL(Color){ 255, 0, 0, 255 }
#define GUI_TEXT_CYAN    CLITERAL(Color){ 0, 255, 255, 255 }
#define GUI_TAB_ACTIVE   CLITERAL(Color){ 100, 90, 70, 255 }
#define GUI_TAB_INACTIVE CLITERAL(Color){ 50, 44, 35, 255 }
#define GUI_PRAYER_ON   CLITERAL(Color){ 200, 200, 100, 80 }
#define GUI_SPEC_GREEN  CLITERAL(Color){ 0, 180, 0, 255 }
#define GUI_SPEC_DARK   CLITERAL(Color){ 30, 30, 20, 255 }
#define GUI_HP_GREEN    CLITERAL(Color){ 0, 146, 0, 255 }
#define GUI_HP_RED      CLITERAL(Color){ 160, 0, 0, 255 }

/* OSRS text shadow: draw black at (+1,+1) then color on top */
#define GUI_TEXT_SHADOW CLITERAL(Color){ 0, 0, 0, 255 }

#define GUI_MAP_CONTAINER_W 211
#define GUI_MAP_CONTAINER_H 207
#define GUI_MINIMAP_X 53
#define GUI_MINIMAP_Y 8
#define GUI_MINIMAP_W 152
#define GUI_MINIMAP_H 152
#define GUI_MINIMAP_MASK_CENTER 75.5f
#define GUI_MINIMAP_MASK_RADIUS 76.0f
#define GUI_MAP_SURROUND_X 29
#define GUI_MAP_SURROUND_Y 0
#define GUI_MAP_SURROUND_W 182
#define GUI_MAP_SURROUND_H 166
#define GUI_COMPASS_X 34
#define GUI_COMPASS_Y 5
#define GUI_COMPASS_W 35
#define GUI_COMPASS_H 35

#define GUI_ORBS_X 0
#define GUI_ORBS_Y 10
#define GUI_ORBS_W 207
#define GUI_ORBS_H 197
#define GUI_XP_X 0
#define GUI_XP_Y 17
#define GUI_HP_X 0
#define GUI_HP_Y 37
#define GUI_PRAYER_X 0
#define GUI_PRAYER_Y 71
#define GUI_RUN_X 10
#define GUI_RUN_Y 103
#define GUI_SPEC_X 32
#define GUI_SPEC_Y 128
#define GUI_WORLDMAP_X 177
#define GUI_WORLDMAP_Y 137

#define GUI_SIDE_MENU_W 241
#define GUI_SIDE_MENU_H 335
#define GUI_SIDE_CONTENT_X 25
#define GUI_SIDE_CONTENT_Y 37
#define GUI_SIDE_CONTENT_W 190
#define GUI_SIDE_CONTENT_H 261
#define GUI_SIDE_TOP_Y 0
#define GUI_SIDE_BOTTOM_Y 298

#define GUI_TAB_PRESS_TICKS 6


typedef enum {
    GUI_TAB_COMBAT = 0,
    GUI_TAB_STATS = 1,       /* empty (no content) */
    GUI_TAB_QUESTS = 2,      /* empty (no content) */
    GUI_TAB_INVENTORY = 3,
    GUI_TAB_EQUIPMENT = 4,
    GUI_TAB_PRAYER = 5,
    GUI_TAB_SPELLBOOK = 6,
    GUI_TAB_COUNT = 7
} GuiTab;


/* slot background sprite IDs from cache index 8:
   head=156, cape=157, neck=158, weapon=159, ring=160,
   body=161, shield=162, legs=163, hands=164, feet=165, tile=170 */
#define GUI_NUM_SLOT_SPRITES 12  /* 11 slots + tile background */


/* prayer icons — authoritative 29-entry standard book.
   enum order IS display order (left→right, top→bottom) in the 5×6 grid.
   sprite IDs match the real OSRS SpriteID.Prayeron / Prayeroff mapping. */
typedef enum {
    GUI_PRAY_THICK_SKIN = 0,      /* row 0: sprite 115 / 135 */
    GUI_PRAY_BURST_STR,           /*        sprite 116 / 136 */
    GUI_PRAY_CLARITY,             /*        sprite 117 / 137 */
    GUI_PRAY_SHARP_EYE,           /*        sprite 133 / 153 */
    GUI_PRAY_MYSTIC_WILL,         /*        sprite 134 / 154 */
    GUI_PRAY_ROCK_SKIN,           /* row 1: sprite 118 / 138 */
    GUI_PRAY_SUPERHUMAN,          /*        sprite 119 / 139 */
    GUI_PRAY_IMPROVED_REFLEX,     /*        sprite 120 / 140 */
    GUI_PRAY_RAPID_RESTORE,       /*        sprite 121 / 141 */
    GUI_PRAY_RAPID_HEAL,          /*        sprite 122 / 142 */
    GUI_PRAY_PROTECT_ITEM,        /* row 2: sprite 123 / 143 */
    GUI_PRAY_HAWK_EYE,            /*        sprite 502 / 506 */
    GUI_PRAY_MYSTIC_LORE,         /*        sprite 503 / 507 */
    GUI_PRAY_STEEL_SKIN,          /*        sprite 124 / 144 */
    GUI_PRAY_ULTIMATE_STR,        /*        sprite 125 / 145 */
    GUI_PRAY_INCREDIBLE_REFLEX,   /* row 3: sprite 126 / 146 */
    GUI_PRAY_PROTECT_MAGIC,       /*        sprite 127 / 147 */
    GUI_PRAY_PROTECT_MISSILES,    /*        sprite 128 / 148 */
    GUI_PRAY_PROTECT_MELEE,       /*        sprite 129 / 149 */
    GUI_PRAY_EAGLE_EYE,           /*        sprite 504 / 508 */
    GUI_PRAY_MYSTIC_MIGHT,        /* row 4: sprite 505 / 509 */
    GUI_PRAY_RETRIBUTION,         /*        sprite 131 / 151 */
    GUI_PRAY_REDEMPTION,          /*        sprite 130 / 150 */
    GUI_PRAY_SMITE,               /*        sprite 132 / 152 */
    GUI_PRAY_PRESERVE,            /*        sprite 947 / 951 */
    GUI_PRAY_CHIVALRY,            /* row 5: sprite 945 / 949 */
    GUI_PRAY_PIETY,               /*        sprite 946 / 950 */
    GUI_PRAY_RIGOUR,              /*        sprite 1420 / 1424 */
    GUI_PRAY_AUGURY,              /*        sprite 1421 / 1425 */
    GUI_NUM_PRAYERS               /* = 29 */
} GuiPrayerIdx;


/* Ancient spellbook sorted by level: combat spells followed by teleports. */
typedef enum {
    GUI_SPELL_SMOKE_RUSH = 0,     /* sprite 329 / 379 */
    GUI_SPELL_SHADOW_RUSH,        /* sprite 337 / 387 */
    GUI_SPELL_BLOOD_RUSH,         /* sprite 333 / 383 */
    GUI_SPELL_ICE_RUSH,           /* sprite 325 / 375 */
    GUI_SPELL_SMOKE_BURST,        /* sprite 330 / 380 */
    GUI_SPELL_SHADOW_BURST,       /* sprite 338 / 388 */
    GUI_SPELL_BLOOD_BURST,        /* sprite 334 / 384 */
    GUI_SPELL_ICE_BURST,          /* sprite 326 / 376 */
    GUI_SPELL_SMOKE_BLITZ,        /* sprite 331 / 381 */
    GUI_SPELL_SHADOW_BLITZ,       /* sprite 339 / 389 */
    GUI_SPELL_BLOOD_BLITZ,        /* sprite 335 / 385 */
    GUI_SPELL_ICE_BLITZ,          /* sprite 327 / 377 */
    GUI_SPELL_SMOKE_BARRAGE,      /* sprite 332 / 382 */
    GUI_SPELL_SHADOW_BARRAGE,     /* sprite 340 / 390 */
    GUI_SPELL_BLOOD_BARRAGE,      /* sprite 336 / 386 */
    GUI_SPELL_ICE_BARRAGE,        /* sprite 328 / 378 */
    GUI_SPELL_PADDEWWA_TELEPORT,   /* sprite 341 / 391 */
    GUI_SPELL_SENNTISTEN_TELEPORT, /* sprite 342 / 392 */
    GUI_SPELL_KHARYRLL_TELEPORT,   /* sprite 343 / 393 */
    GUI_SPELL_LASSAR_TELEPORT,     /* sprite 344 / 394 */
    GUI_SPELL_DAREEYAK_TELEPORT,   /* sprite 345 / 395 */
    GUI_SPELL_CARRALLANGER_TELEPORT, /* sprite 346 / 396 */
    GUI_SPELL_ANNAKARL_TELEPORT,   /* sprite 347 / 397 */
    GUI_SPELL_GHORROCK_TELEPORT,   /* sprite 348 / 398 */
    GUI_SPELL_VENGEANCE,          /* sprite 564 */
    GUI_NUM_SPELLS
} GuiSpellIdx;


/* inventory slot types: either an equipment item (ITEM_DATABASE index) or a consumable.
   consumables are tracked as counts in Player, not as individual ITEM_DATABASE entries,
   so we use dedicated types with known OSRS item IDs for sprite lookup. */
typedef enum {
    INV_SLOT_EMPTY = 0,
    INV_SLOT_EQUIPMENT,     /* item_db_idx holds ITEM_DATABASE index */
    INV_SLOT_FOOD,          /* shark (OSRS ID 385) */
    INV_SLOT_KARAMBWAN,     /* cooked karambwan (OSRS ID 3144) */
    INV_SLOT_BREW,          /* saradomin brew (OSRS IDs 6685/6687/6689/6691 for 4/3/2/1 dose) */
    INV_SLOT_RESTORE,       /* super restore (OSRS IDs 3024/3026/3028/3030) */
    INV_SLOT_COMBAT_POT,    /* super combat (OSRS IDs 12695/12697/12699/12701) */
    INV_SLOT_RANGED_POT,    /* ranging potion (OSRS IDs 2444/169/171/173) */
    INV_SLOT_ANTIVENOM,     /* anti-venom+ (OSRS IDs 12913/12915/12917/12919) */
    INV_SLOT_PRAYER_POT,    /* prayer potion (OSRS IDs 2434/139/141/143 for 4/3/2/1 dose) */
    INV_SLOT_BASTION_POT,   /* bastion potion (OSRS IDs 22461/22464/22467/22470) */
    INV_SLOT_STAMINA_POT,   /* stamina potion (OSRS IDs 12625/12627/12629/12631) */
    INV_SLOT_SATURATED_HEART, /* saturated heart (OSRS ID 27641) */
} InvSlotType;

/* OSRS item IDs for consumable sprites (4-dose shown by default) */
#define OSRS_ID_SHARK         385
#define OSRS_ID_KARAMBWAN     3144
#define OSRS_ID_BREW_4        6685
#define OSRS_ID_BREW_3        6687
#define OSRS_ID_BREW_2        6689
#define OSRS_ID_BREW_1        6691
#define OSRS_ID_RESTORE_4     3024
#define OSRS_ID_RESTORE_3     3026
#define OSRS_ID_RESTORE_2     3028
#define OSRS_ID_RESTORE_1     3030
#define OSRS_ID_COMBAT_4      12695
#define OSRS_ID_COMBAT_3      12697
#define OSRS_ID_COMBAT_2      12699
#define OSRS_ID_COMBAT_1      12701
#define OSRS_ID_RANGED_4      2444
#define OSRS_ID_RANGED_3      169
#define OSRS_ID_RANGED_2      171
#define OSRS_ID_RANGED_1      173
#define OSRS_ID_ANTIVENOM_4   12913
#define OSRS_ID_ANTIVENOM_3   12915
#define OSRS_ID_ANTIVENOM_2   12917
#define OSRS_ID_ANTIVENOM_1   12919
#define OSRS_ID_PRAYER_POT_4  2434
#define OSRS_ID_PRAYER_POT_3  139
#define OSRS_ID_PRAYER_POT_2  141
#define OSRS_ID_PRAYER_POT_1  143
#define OSRS_ID_BASTION_4     22461
#define OSRS_ID_BASTION_3     22464
#define OSRS_ID_BASTION_2     22467
#define OSRS_ID_BASTION_1     22470
#define OSRS_ID_STAMINA_4     12625
#define OSRS_ID_STAMINA_3     12627
#define OSRS_ID_STAMINA_2     12629
#define OSRS_ID_STAMINA_1     12631
#define OSRS_ID_SATURATED_HEART 27641

#define INV_GRID_SLOTS 28  /* 4 columns x 7 rows */

typedef struct {
    InvSlotType type;
    uint8_t     item_db_idx;   /* ITEM_DATABASE index (for INV_SLOT_EQUIPMENT) */
    int         osrs_id;       /* OSRS item ID (for sprite lookup, all types) */
} InvSlot;

/* click/drag interaction state */
#define INV_DIM_TICKS 15       /* client ticks (50 Hz) to show dim after click */
#define INV_DRAG_DEAD_ZONE 5   /* pixels before drag activates */
#define INV_DRAG_HOLD_SECONDS 0.030  /* anti-drag: button must be held this long
                                        before a drag can start; a faster
                                        press-move-release stays a click */

typedef enum {
    INV_ACTION_NONE = 0,
    INV_ACTION_EQUIP,
    INV_ACTION_EAT,
    INV_ACTION_DRINK,
    INV_ACTION_ITEM_ON_ITEM,
} InvAction;

#define GUI_MAX_NAMED_ASSETS 768
#define GUI_UI_ITEM_CONTAINER_MAX_SLOTS 64
#define GUI_UI_MAX_COMPONENT_OVERRIDES 64
#define GUI_ITEM_STACK_VARIANT_MAX 2048

typedef struct {
    char name[64];
    Texture2D tex;
} GuiNamedAsset;

typedef struct {
    int base_item_id;
    int threshold;
    int display_item_id;
} GuiItemStackVariant;

typedef struct {
    int present;
    int enabled;
    uint8_t item_db_idx;
    int osrs_id;
    int quantity;
    int selected;
    unsigned char alpha;
    int gear_slot;
    const char* empty_asset;
} GuiUiItemSlot;

typedef struct {
    uint32_t component_id;
    int hidden;
    int force_visible;
    const char* text;
    const char* sprite_asset;
    int sprite_present;
    int sprite_id;
    GuiUiItemSlot item;
} GuiUiComponentOverride;

typedef struct {
    uint32_t component_id;
    int slot_count;
    int columns;
    float x0;
    float y0;
    float step_x;
    float step_y;
    float slot_w;
    float slot_h;
    GuiUiItemSlot slots[GUI_UI_ITEM_CONTAINER_MAX_SLOTS];
} GuiUiItemContainerOverride;

typedef struct {
    const GuiUiComponentOverride* components;
    int component_count;
    const GuiUiItemContainerOverride* item_containers;
    int item_container_count;
} GuiUiOverrides;

typedef struct {
    Rectangle current;
    int active;
} GuiUiClipState;

typedef struct {
    GuiTab active_tab;
    int panel_x, panel_y;
    int panel_w, panel_h;
    int tab_h;
    int status_bar_h;    /* compact HP/prayer/spec bar height */

    /* chrome draw-time zoom set by the render client (0 = treat as 1.0).
       panel/minimap coordinates stay native; drawing and hit-testing map
       through the fixed-point transforms in gui_mouse_to_*_space. */
    float ui_scale;

    /* multi-entity cycling (G key) */
    int gui_entity_idx;
    int gui_entity_count;

    /* encounter state (for boss info display below panel) */
    void* encounter_state;
    const void* encounter_def;

    /* textures loaded from exported cache sprites */
    int sprites_loaded;
    GuiNamedAsset named_assets[GUI_MAX_NAMED_ASSETS];
    int named_asset_count;
    OsrsUiInterfaceStore ui_interfaces;
    Font font;
    Font small_font;
    int font_loaded;
    int small_font_loaded;
    GuiItemStackVariant item_stack_variants[GUI_ITEM_STACK_VARIANT_MAX];
    int item_stack_variant_count;

    /* equipment slot background sprites (indexed by GEAR_SLOT_*) */
    Texture2D slot_sprites[GUI_NUM_SLOT_SPRITES];
    Texture2D slot_tile_bg;   /* sprite 170: tile/background */

    /* tab icons: 7 tabs (combat, stats, quests, inventory, equipment, prayer, spellbook) */
    Texture2D tab_icons[GUI_TAB_COUNT];
    int tab_press_timer[GUI_TAB_COUNT];

    /* prayer icons: enabled and disabled variants */
    Texture2D prayer_on[GUI_NUM_PRAYERS];
    Texture2D prayer_off[GUI_NUM_PRAYERS];

    /* spell icons: enabled and disabled variants */
    Texture2D spell_on[GUI_NUM_SPELLS];
    Texture2D spell_off[GUI_NUM_SPELLS];

    /* special attack bar sprite */
    Texture2D spec_bar;
    int spec_bar_loaded;

    /* interface chrome sprites */
    Texture2D side_panel_bg;       /* 1031: stone background tile */
    Texture2D tabs_row_bottom;     /* 1032: bottom tab row strip */
    Texture2D tabs_row_top;        /* 1036: top tab row strip */
    Texture2D tab_stone_sel[5];    /* 1026-1030: selected tab corners + middle */
    Texture2D slanted_tab;         /* 952: inactive tab button */
    Texture2D slanted_tab_hover;   /* 953: hovered tab button */
    Texture2D slot_tile;           /* 170: equipment slot background */
    Texture2D slot_selected;       /* 179: equipment slot selected */
    Texture2D orb_frame;           /* 1071: minimap orb frame */
    int chrome_loaded;

    /* minimap chrome sprites (canonical OSRS sprite IDs from RuneLite SpriteID).
       loaded once at init, then composited each frame in render_draw_minimap_area.
       both fixed-mode (1182/1183/1184) and resizable-mode (1177/1178/1179)
       variants are kept resident so the layout can be switched at runtime. */
    Texture2D minimap_compass;       /* 169: compass disc, rotates with cam yaw */
    Texture2D minimap_compass_masked;
    Texture2D minimap_alpha_mask;    /* 1183: fixed-mode circular cutout */
    Texture2D minimap_frame;         /* 1182: fixed-mode frame chrome */
    Texture2D rm_minimap_alpha_mask; /* 1178: resizable-mode circular cutout */
    Texture2D rm_minimap_frame;      /* 1177: resizable-mode frame chrome */
    Texture2D rm_compass_alpha_mask; /* 1179: resizable-mode compass mask */
    Texture2D rm_side_panel_bg;        /* 897: tiled side panel background */
    Texture2D rm_side_panel_edge_left; /* 1175: 26x261 left vertical strip */
    Texture2D rm_side_panel_edge_right;/* 1176: 26x261 right vertical strip */
    Texture2D rm_tabs_top_row;         /* 1173: 241x37 top tab strip */
    Texture2D rm_tabs_bottom_row;      /* 1174: 241x37 bottom tab strip */
    Texture2D rm_tab_stone;            /* 1180: 33x36 inactive tab stone */
    Texture2D rm_tab_stone_selected;   /* 1181: 33x36 active tab stone */
    Texture2D orb_empty;             /* 1059: greyed orb disc (base chrome) */
    Texture2D orb_hp;                /* 1060: green-fill HP orb */
    Texture2D orb_prayer;            /* 1063: prayer orb chrome */
    Texture2D orb_run;               /* 1064: run-energy orb chrome */
    Texture2D orb_run_active;        /* 1065: run-energy orb when running */
    Texture2D orb_icon_hp;           /* 1067: heart icon inside HP orb */
    Texture2D orb_icon_prayer;       /* 1068: prayer icon */
    Texture2D orb_icon_walk;         /* 1069: walking-foot icon */
    Texture2D orb_icon_run;          /* 1070: running-foot icon */
    Texture2D minimap_dot_player;    /* 512: white square for player */
    Texture2D minimap_dot_npc;       /* 511: yellow square for NPCs */
    Texture2D minimap_dot_friend;    /* 513: green square for friendlies */
    Texture2D minimap_dot_item;      /* 510: red square for ground items */
    int minimap_chrome_loaded;

    /* skill icons for stats tab (25x25 from RuneLite skill_icons) */
    #define GUI_NUM_SKILL_ICONS 7
    Texture2D skill_icons[7];  /* attack, strength, defence, ranged, prayer, magic, hitpoints */
    int skill_icons_loaded;

    /* item sprites: keyed by OSRS item ID (from data/sprites/items/{id}.png) */
    #define GUI_MAX_ITEM_SPRITES 256
    int item_sprite_ids[GUI_MAX_ITEM_SPRITES];     /* OSRS item ID, 0 = empty */
    Texture2D item_sprite_tex[GUI_MAX_ITEM_SPRITES]; /* corresponding texture */
    int item_sprite_count;

    /* inventory grid: 28 slots (4x7). initialized once at reset, then updated
       incrementally — items stay in their assigned slots (no compaction on eat).
       positions are user-rearrangeable via drag-and-drop. */
    InvSlot inv_grid[INV_GRID_SLOTS];
    int inv_grid_dirty;   /* 1 = needs full rebuild from player state */

    /* render-only inventory override: when display_inventory_count > 0, the panel
       renders this fixed list of OSRS item ids instead of the derived grid. The
       colosseum render bridge sets it to the wiki kit's exact 28-slot inventory so
       the panel matches the reference 1:1; 0 = use the derived grid. Never read by
       the sim. */
    int display_inventory_osrs_ids[INV_GRID_SLOTS];
    int display_inventory_count;

    /* previous player state for incremental inventory updates.
       compared each tick to detect gear switches and consumable use. */
    uint8_t inv_prev_equipped[NUM_GEAR_SLOTS];
    int inv_prev_food_count;
    int inv_prev_karambwan_count;
    int inv_prev_brew_doses;
    int inv_prev_restore_doses;
    int inv_prev_prayer_pot_doses;
    int inv_prev_combat_doses;
    int inv_prev_ranged_doses;
    int inv_prev_bastion_doses;
    int inv_prev_stamina_doses;
    int inv_prev_antivenom_doses;
    int inv_prev_saturated_heart_count;

    /* human-clicked inventory slot: when a human clicks a consumable, this records
       the exact slot so gui_update_inventory removes from that slot instead of the
       last one. -1 = no human click pending, use default last-slot removal. */
    int human_clicked_inv_slot;

    /* click dim animation: slot index and countdown (50 Hz client ticks) */
    int inv_dim_slot;     /* -1 = none */
    int inv_dim_timer;    /* counts down from INV_DIM_TICKS */

    /* drag state */
    int inv_drag_active;       /* 1 = currently dragging */
    int inv_drag_src_slot;     /* slot being dragged */
    int inv_drag_start_x;     /* mouse position at drag start */
    int inv_drag_start_y;
    int inv_drag_mouse_x;     /* current mouse position during drag */
    int inv_drag_mouse_y;
    double inv_drag_press_time; /* GetTime() at mouse-down on the src slot */

    /* spell targeting: GuiSpellIdx of the spell awaiting an enemy click, or
       -1 when not targeting. render code sets this before calling gui_draw. */
    int pending_spell_highlight;
    int autocast_selector_open;
} GuiState;

/** Map a raw screen coordinate into the side panel's native coordinate space.
    The panel draws at native sprite size under a ui_scale zoom about the
    window's bottom-right corner, which coincides with the panel rect's own
    bottom-right, so the inverse is a fixed-point scale about that corner. */
static inline void gui_mouse_to_panel_space(
    const GuiState* gs, int mx, int my, int* out_x, int* out_y
) {
    float k = gs->ui_scale > 0.0f ? gs->ui_scale : 1.0f;
    float fx = (float)(gs->panel_x + gs->panel_w);
    float fy = (float)(gs->panel_y + gs->panel_h);
    *out_x = (int)lroundf(fx + ((float)mx - fx) / k);
    *out_y = (int)lroundf(fy + ((float)my - fy) / k);
}

/** Same mapping for the minimap block, which zooms about the window's
    top-right corner (= the panel column's right edge at y 0). */
static inline void gui_mouse_to_minimap_space(
    const GuiState* gs, int mx, int my, int* out_x, int* out_y
) {
    float k = gs->ui_scale > 0.0f ? gs->ui_scale : 1.0f;
    float fx = (float)(gs->panel_x + gs->panel_w);
    *out_x = (int)lroundf(fx + ((float)mx - fx) / k);
    *out_y = (int)lroundf((float)my / k);
}


/** Try loading a texture, returns 1 on success. */
static int gui_try_load(Texture2D* tex, const char* path) {
    if (osrs_asset_exists(path)) {
        *tex = osrs_asset_load_texture(path);
        return 1;
    }
    return 0;
}

static int gui_try_load_masked_compass(Texture2D* tex, const char* path) {
    if (!osrs_asset_exists(path)) return 0;
    Image image = osrs_asset_load_image(path);
    if (!image.data) return 0;

    ImageFormat(&image, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
    Color* pixels = (Color*)image.data;
    int min_side = image.width < image.height ? image.width : image.height;
    float cx = (float)image.width * 0.5f;
    float cy = (float)image.height * 0.5f;
    float radius = (float)min_side * (19.0f / 51.0f);
    float radius_sq = radius * radius;
    for (int y = 0; y < image.height; y++) {
        for (int x = 0; x < image.width; x++) {
            float dx = ((float)x + 0.5f) - cx;
            float dy = ((float)y + 0.5f) - cy;
            if (dx * dx + dy * dy > radius_sq) {
                pixels[x + y * image.width].a = 0;
            }
        }
    }

    *tex = LoadTextureFromImage(image);
    UnloadImage(image);
    return tex->id != 0;
}

static int gui_rect_has_area(Rectangle rect) {
    return rect.width > 0.0f && rect.height > 0.0f;
}

static Rectangle gui_rect_intersect(Rectangle a, Rectangle b) {
    float x1 = a.x > b.x ? a.x : b.x;
    float y1 = a.y > b.y ? a.y : b.y;
    float x2 = a.x + a.width < b.x + b.width ? a.x + a.width : b.x + b.width;
    float y2 = a.y + a.height < b.y + b.height ? a.y + a.height : b.y + b.height;
    if (x2 <= x1 || y2 <= y1) return (Rectangle){0};
    return (Rectangle){x1, y1, x2 - x1, y2 - y1};
}

static void gui_apply_scissor(Rectangle rect) {
    int x = (int)(rect.x + 0.5f);
    int y = (int)(rect.y + 0.5f);
    int w = (int)(rect.width + 0.5f);
    int h = (int)(rect.height + 0.5f);
    if (w < 0) w = 0;
    if (h < 0) h = 0;
    BeginScissorMode(x, y, w, h);
}

static void gui_push_clip(
    GuiUiClipState* clip,
    Rectangle next,
    Rectangle* prev,
    int* prev_active
) {
    *prev = clip->current;
    *prev_active = clip->active;
    if (clip->active) next = gui_rect_intersect(next, clip->current);
    if (!gui_rect_has_area(next)) next = (Rectangle){0};
    if (clip->active) EndScissorMode();
    gui_apply_scissor(next);
    clip->current = next;
    clip->active = 1;
}

static void gui_pop_clip(GuiUiClipState* clip, Rectangle prev, int prev_active) {
    if (clip->active) EndScissorMode();
    clip->current = prev;
    clip->active = prev_active;
    if (clip->active) gui_apply_scissor(clip->current);
}

static Font gui_font_for_size(const GuiState* gs, int size) {
    if (gs && size <= 12 && gs->small_font_loaded) return gs->small_font;
    if (gs && gs->font_loaded) return gs->font;
    return GetFontDefault();
}

static int gui_measure_text(const GuiState* gs, const char* text, int size) {
    if (!text || !text[0]) return 0;
    Font font = gui_font_for_size(gs, size);
    if (font.texture.id == 0) return MeasureText(text, size);
    Vector2 measured = MeasureTextEx(font, text, (float)size, 0.0f);
    return (int)(measured.x + 0.5f);
}

static void gui_load_fonts(GuiState* gs) {
    gs->font = osrs_asset_load_font("fonts/runescape.ttf", 14);
    gs->font_loaded = gs->font.texture.id != 0;
    if (gs->font_loaded) SetTextureFilter(gs->font.texture, TEXTURE_FILTER_POINT);
    gs->small_font = osrs_asset_load_font("fonts/runescape_small.ttf", 12);
    gs->small_font_loaded = gs->small_font.texture.id != 0;
    if (gs->small_font_loaded) SetTextureFilter(gs->small_font.texture, TEXTURE_FILTER_POINT);
}

static void gui_load_item_stack_variants(GuiState* gs) {
    gs->item_stack_variant_count = 0;
    FILE* f = osrs_asset_fopen("sprites/items/item_stack_variants.tsv", "rb");
    if (!f) return;
    char line[128];
    while (fgets(line, sizeof(line), f)) {
        if (line[0] == '#' || line[0] == '\n' || line[0] == '\0') continue;
        GuiItemStackVariant variant = {0};
        if (sscanf(line, "%d\t%d\t%d",
                &variant.base_item_id,
                &variant.threshold,
                &variant.display_item_id) != 3) {
            continue;
        }
        if (gs->item_stack_variant_count >= GUI_ITEM_STACK_VARIANT_MAX) {
            fprintf(stderr, "too many item stack variants\n");
            abort();
        }
        gs->item_stack_variants[gs->item_stack_variant_count++] = variant;
    }
    fclose(f);
}

static Texture2D gui_asset(const GuiState* gs, const char* name) {
    for (int i = 0; i < gs->named_asset_count; i++) {
        if (strcmp(gs->named_assets[i].name, name) == 0) {
            return gs->named_assets[i].tex;
        }
    }
    return (Texture2D){0};
}

static Texture2D gui_load_named_asset_path(GuiState* gs, const char* name, const char* path) {
    if (gs->named_asset_count >= GUI_MAX_NAMED_ASSETS) {
        fprintf(stderr, "GUI named asset capacity exceeded\n");
        abort();
    }

    Texture2D tex = {0};
    if (!gui_try_load(&tex, path)) return tex;

    GuiNamedAsset* asset = &gs->named_assets[gs->named_asset_count++];
    snprintf(asset->name, sizeof(asset->name), "%s", name);
    asset->tex = tex;
    return tex;
}

static void gui_load_named_asset(GuiState* gs, const char* name) {
    char path[160];
    snprintf(path, sizeof(path), OSRS_ASSET("sprites/gui/%s.png"), name);
    gui_load_named_asset_path(gs, name, path);
}

static Texture2D gui_sprite_asset(GuiState* gs, int sprite_id) {
    if (sprite_id < 0) return (Texture2D){0};

    char name[32];
    snprintf(name, sizeof(name), "#%d", sprite_id);
    Texture2D tex = gui_asset(gs, name);
    if (tex.id != 0) return tex;

    char path[160];
    snprintf(path, sizeof(path), OSRS_ASSET("sprites/gui/%d.png"), sprite_id);
    return gui_load_named_asset_path(gs, name, path);
}

static void gui_load_named_asset_range(GuiState* gs, const char* prefix, int first, int last) {
    char name[64];
    for (int i = first; i <= last; i++) {
        snprintf(name, sizeof(name), "%s_%d", prefix, i);
        gui_load_named_asset(gs, name);
    }
}

static int gui_load_ui_interfaces(GuiState* gs) {
    return osrs_ui_interfaces_load(&gs->ui_interfaces, OSRS_ASSET("ui/interfaces.bin"));
}

static const int GUI_PRAYER_ON_SPRITE_IDS[GUI_NUM_PRAYERS] = {
    115, 116, 117, 133, 134,
    118, 119, 120, 121, 122,
    123, 502, 503, 124, 125,
    126, 127, 128, 129, 504,
    505, 131, 130, 132, 947,
    945, 946, 1420, 1421,
};

static const int GUI_PRAYER_OFF_SPRITE_IDS[GUI_NUM_PRAYERS] = {
    135, 136, 137, 153, 154,
    138, 139, 140, 141, 142,
    143, 506, 507, 144, 145,
    146, 147, 148, 149, 508,
    509, 151, 150, 152, 951,
    949, 950, 1424, 1425,
};

static const int GUI_SPELL_ON_SPRITE_IDS[GUI_NUM_SPELLS] = {
    329, 337, 333, 325,
    330, 338, 334, 326,
    331, 339, 335, 327,
    332, 340, 336, 328,
    341, 342, 343, 344,
    345, 346, 347, 348,
    564,
};

static const int GUI_SPELL_OFF_SPRITE_IDS[GUI_NUM_SPELLS] = {
    379, 387, 383, 375,
    380, 388, 384, 376,
    381, 389, 385, 377,
    382, 390, 386, 378,
    391, 392, 393, 394,
    395, 396, 397, 398,
    614,
};

static int gui_prayer_on_sprite_id(GuiPrayerIdx idx) {
    assert(idx >= 0 && idx < GUI_NUM_PRAYERS);
    return GUI_PRAYER_ON_SPRITE_IDS[idx];
}

static int gui_prayer_off_sprite_id(GuiPrayerIdx idx) {
    assert(idx >= 0 && idx < GUI_NUM_PRAYERS);
    return GUI_PRAYER_OFF_SPRITE_IDS[idx];
}

static int gui_spell_on_sprite_id(GuiSpellIdx idx) {
    assert(idx >= 0 && idx < GUI_NUM_SPELLS);
    return GUI_SPELL_ON_SPRITE_IDS[idx];
}

static int gui_spell_off_sprite_id(GuiSpellIdx idx) {
    assert(idx >= 0 && idx < GUI_NUM_SPELLS);
    return GUI_SPELL_OFF_SPRITE_IDS[idx];
}

static int gui_find_sprite_index_by_osrs_id(const GuiState* gs, int osrs_id) {
    if (osrs_id <= 0) return -1;
    for (int i = 0; i < gs->item_sprite_count; i++) {
        if (gs->item_sprite_ids[i] == osrs_id) return i;
    }
    return -1;
}

static Texture2D gui_load_sprite_by_osrs_id_if_present(GuiState* gs, int osrs_id) {
    Texture2D empty = {0};
    int existing_idx = gui_find_sprite_index_by_osrs_id(gs, osrs_id);
    if (existing_idx >= 0) return gs->item_sprite_tex[existing_idx];
    if (osrs_id <= 0 || gs->item_sprite_count >= GUI_MAX_ITEM_SPRITES) return empty;

    const char* path = TextFormat(OSRS_ASSET("sprites/items/%d.png"), osrs_id);
    if (!osrs_asset_exists(path)) return empty;

    Texture2D tex = osrs_asset_load_texture(path);
    if (tex.id == 0) return empty;

    int idx = gs->item_sprite_count++;
    gs->item_sprite_ids[idx] = osrs_id;
    gs->item_sprite_tex[idx] = tex;
    return tex;
}

static void gui_require_sprite_by_osrs_id(GuiState* gs, int osrs_id) {
    Texture2D tex = gui_load_sprite_by_osrs_id_if_present(gs, osrs_id);
    if (tex.id != 0) return;

    fprintf(stderr, "GUI: missing required item sprite raw id %d at %s\n",
        osrs_id,
        TextFormat(OSRS_ASSET("sprites/items/%d.png"), osrs_id));
    abort();
}

/** Load all GUI sprites from data/sprites/gui/. */
static void gui_load_sprites(GuiState* gs) {
    gs->sprites_loaded = 1;
    int ok = 1;
    gs->named_asset_count = 0;
    gui_load_ui_interfaces(gs);
    gui_load_fonts(gs);
    gui_load_item_stack_variants(gs);

    /* equipment slot backgrounds: sprite IDs mapped to GEAR_SLOT_* order.
       GEAR_SLOT: HEAD=0, CAPE=1, NECK=2, AMMO=3, WEAPON=4, SHIELD=5,
                  BODY=6, LEGS=7, HANDS=8, FEET=9, RING=10 */
    const char* slot_files[] = {
        OSRS_ASSET("sprites/gui/slot_head.png"),    /* GEAR_SLOT_HEAD */
        OSRS_ASSET("sprites/gui/slot_cape.png"),    /* GEAR_SLOT_CAPE */
        OSRS_ASSET("sprites/gui/slot_neck.png"),    /* GEAR_SLOT_NECK */
        OSRS_ASSET("sprites/gui/slot_tile.png"),    /* GEAR_SLOT_AMMO (use tile bg) */
        OSRS_ASSET("sprites/gui/slot_weapon.png"),  /* GEAR_SLOT_WEAPON */
        OSRS_ASSET("sprites/gui/slot_shield.png"),  /* GEAR_SLOT_SHIELD */
        OSRS_ASSET("sprites/gui/slot_body.png"),    /* GEAR_SLOT_BODY */
        OSRS_ASSET("sprites/gui/slot_legs.png"),    /* GEAR_SLOT_LEGS */
        OSRS_ASSET("sprites/gui/slot_hands.png"),   /* GEAR_SLOT_HANDS */
        OSRS_ASSET("sprites/gui/slot_feet.png"),    /* GEAR_SLOT_FEET */
        OSRS_ASSET("sprites/gui/slot_ring.png"),    /* GEAR_SLOT_RING */
        OSRS_ASSET("sprites/gui/slot_tile.png"),    /* spare tile bg */
    };
    for (int i = 0; i < GUI_NUM_SLOT_SPRITES; i++) {
        ok &= gui_try_load(&gs->slot_sprites[i], slot_files[i]);
    }
    gui_try_load(&gs->slot_tile_bg, OSRS_ASSET("sprites/gui/slot_tile.png"));

    /* tab icons: mapped to GuiTab enum order (7 tabs) */
    const char* tab_files[] = {
        OSRS_ASSET("sprites/gui/tab_combat.png"),    /* GUI_TAB_COMBAT */
        OSRS_ASSET("sprites/gui/tab_stats.png"),     /* GUI_TAB_STATS */
        OSRS_ASSET("sprites/gui/tab_quests.png"),    /* GUI_TAB_QUESTS */
        OSRS_ASSET("sprites/gui/tab_inventory.png"), /* GUI_TAB_INVENTORY */
        OSRS_ASSET("sprites/gui/tab_equipment.png"), /* GUI_TAB_EQUIPMENT */
        OSRS_ASSET("sprites/gui/tab_prayer.png"),    /* GUI_TAB_PRAYER */
        OSRS_ASSET("sprites/gui/tab_magic.png"),     /* GUI_TAB_SPELLBOOK */
    };
    for (int i = 0; i < GUI_TAB_COUNT; i++) {
        ok &= gui_try_load(&gs->tab_icons[i], tab_files[i]);
    }

    /* skill icons for stats tab (OSRS skill_icons from RuneLite resources) */
    const char* skill_icon_files[] = {
        OSRS_ASSET("sprites/gui/skill_attack.png"),
        OSRS_ASSET("sprites/gui/skill_strength.png"),
        OSRS_ASSET("sprites/gui/skill_defence.png"),
        OSRS_ASSET("sprites/gui/skill_ranged.png"),
        OSRS_ASSET("sprites/gui/skill_prayer.png"),
        OSRS_ASSET("sprites/gui/skill_magic.png"),
        OSRS_ASSET("sprites/gui/skill_hitpoints.png"),
    };
    gs->skill_icons_loaded = 1;
    for (int i = 0; i < 7; i++) {
        gs->skill_icons_loaded &= gui_try_load(&gs->skill_icons[i], skill_icon_files[i]);
    }

    for (int i = 0; i < GUI_NUM_PRAYERS; i++) {
        const char* on_path = TextFormat(OSRS_ASSET("sprites/gui/%d.png"),
            gui_prayer_on_sprite_id((GuiPrayerIdx)i));
        const char* off_path = TextFormat(OSRS_ASSET("sprites/gui/%d.png"),
            gui_prayer_off_sprite_id((GuiPrayerIdx)i));
        gui_try_load(&gs->prayer_on[i], on_path);
        gui_try_load(&gs->prayer_off[i], off_path);
    }

    for (int i = 0; i < GUI_NUM_SPELLS; i++) {
        const char* on_path = TextFormat(OSRS_ASSET("sprites/gui/%d.png"),
            gui_spell_on_sprite_id((GuiSpellIdx)i));
        const char* off_path = TextFormat(OSRS_ASSET("sprites/gui/%d.png"),
            gui_spell_off_sprite_id((GuiSpellIdx)i));
        gui_try_load(&gs->spell_on[i], on_path);
        gui_try_load(&gs->spell_off[i], off_path);
    }

    /* special attack bar */
    gs->spec_bar_loaded = gui_try_load(&gs->spec_bar, OSRS_ASSET("sprites/gui/special_attack.png"));

    /* interface chrome */
    gs->chrome_loaded = 1;
    gs->chrome_loaded &= gui_try_load(&gs->side_panel_bg, OSRS_ASSET("sprites/gui/side_panel_bg.png"));
    gs->chrome_loaded &= gui_try_load(&gs->tabs_row_bottom, OSRS_ASSET("sprites/gui/tabs_row_bottom.png"));
    gs->chrome_loaded &= gui_try_load(&gs->tabs_row_top, OSRS_ASSET("sprites/gui/tabs_row_top.png"));
    gui_try_load(&gs->slanted_tab, OSRS_ASSET("sprites/gui/slanted_tab.png"));
    gui_try_load(&gs->slanted_tab_hover, OSRS_ASSET("sprites/gui/slanted_tab_hover.png"));
    gui_try_load(&gs->slot_tile, OSRS_ASSET("sprites/gui/slot_tile.png"));
    gui_try_load(&gs->slot_selected, OSRS_ASSET("sprites/gui/slot_selected.png"));
    gui_try_load(&gs->orb_frame, OSRS_ASSET("sprites/gui/orb_frame.png"));

    /* canonical OSRS minimap chrome (loaded best-effort; gracefully omitted if
       the asset bundle predates the export pipeline update). */
    gs->minimap_chrome_loaded = 1;
    gs->minimap_chrome_loaded &= gui_try_load(&gs->minimap_compass,
        OSRS_ASSET("sprites/gui/compass.png"));
    gui_try_load_masked_compass(&gs->minimap_compass_masked,
        OSRS_ASSET("sprites/gui/compass.png"));
    gs->minimap_chrome_loaded &= gui_try_load(&gs->minimap_alpha_mask,
        OSRS_ASSET("sprites/gui/minimap_alpha_mask.png"));
    gs->minimap_chrome_loaded &= gui_try_load(&gs->minimap_frame,
        OSRS_ASSET("sprites/gui/minimap_and_compass_frame.png"));
    gui_try_load(&gs->rm_minimap_alpha_mask,
        OSRS_ASSET("sprites/gui/rm_minimap_alpha_mask.png"));
    gui_try_load(&gs->rm_minimap_frame,
        OSRS_ASSET("sprites/gui/rm_minimap_and_compass_frame.png"));
    gui_try_load(&gs->rm_compass_alpha_mask,
        OSRS_ASSET("sprites/gui/rm_compass_alpha_mask.png"));
    gui_try_load(&gs->rm_side_panel_bg,
        OSRS_ASSET("sprites/gui/rm_side_panel_bg.png"));
    gui_try_load(&gs->rm_side_panel_edge_left,
        OSRS_ASSET("sprites/gui/rm_side_panel_edge_left.png"));
    gui_try_load(&gs->rm_side_panel_edge_right,
        OSRS_ASSET("sprites/gui/rm_side_panel_edge_right.png"));
    gui_try_load(&gs->rm_tabs_top_row,
        OSRS_ASSET("sprites/gui/rm_tabs_top_row.png"));
    gui_try_load(&gs->rm_tabs_bottom_row,
        OSRS_ASSET("sprites/gui/rm_tabs_bottom_row.png"));
    gui_try_load(&gs->rm_tab_stone,
        OSRS_ASSET("sprites/gui/rm_tab_stone_middle.png"));
    gui_try_load(&gs->rm_tab_stone_selected,
        OSRS_ASSET("sprites/gui/rm_tab_stone_middle_selected.png"));
    gs->minimap_chrome_loaded &= gui_try_load(&gs->orb_empty,
        OSRS_ASSET("sprites/gui/orb_empty.png"));
    gs->minimap_chrome_loaded &= gui_try_load(&gs->orb_hp,
        OSRS_ASSET("sprites/gui/orb_hp.png"));
    gs->minimap_chrome_loaded &= gui_try_load(&gs->orb_prayer,
        OSRS_ASSET("sprites/gui/orb_prayer.png"));
    gs->minimap_chrome_loaded &= gui_try_load(&gs->orb_run,
        OSRS_ASSET("sprites/gui/orb_run.png"));
    gs->minimap_chrome_loaded &= gui_try_load(&gs->orb_run_active,
        OSRS_ASSET("sprites/gui/orb_run_active.png"));
    gs->minimap_chrome_loaded &= gui_try_load(&gs->orb_icon_hp,
        OSRS_ASSET("sprites/gui/orb_icon_hp.png"));
    gs->minimap_chrome_loaded &= gui_try_load(&gs->orb_icon_prayer,
        OSRS_ASSET("sprites/gui/orb_icon_prayer.png"));
    gs->minimap_chrome_loaded &= gui_try_load(&gs->orb_icon_walk,
        OSRS_ASSET("sprites/gui/orb_icon_walk.png"));
    gs->minimap_chrome_loaded &= gui_try_load(&gs->orb_icon_run,
        OSRS_ASSET("sprites/gui/orb_icon_run.png"));
    gs->minimap_chrome_loaded &= gui_try_load(&gs->minimap_dot_player,
        OSRS_ASSET("sprites/gui/minimap_dot_player.png"));
    gs->minimap_chrome_loaded &= gui_try_load(&gs->minimap_dot_npc,
        OSRS_ASSET("sprites/gui/minimap_dot_npc.png"));
    gs->minimap_chrome_loaded &= gui_try_load(&gs->minimap_dot_friend,
        OSRS_ASSET("sprites/gui/minimap_dot_friend.png"));
    gs->minimap_chrome_loaded &= gui_try_load(&gs->minimap_dot_item,
        OSRS_ASSET("sprites/gui/minimap_dot_item.png"));

    const char* tab_sel_files[] = {
        OSRS_ASSET("sprites/gui/tab_stone_tl_sel.png"),
        OSRS_ASSET("sprites/gui/tab_stone_tr_sel.png"),
        OSRS_ASSET("sprites/gui/tab_stone_bl_sel.png"),
        OSRS_ASSET("sprites/gui/tab_stone_br_sel.png"),
        OSRS_ASSET("sprites/gui/tab_stone_mid_sel.png"),
    };
    for (int i = 0; i < 5; i++) gui_try_load(&gs->tab_stone_sel[i], tab_sel_files[i]);

    static const char* ui_asset_names[] = {
        "tradebacking_dark",
        "osrs_stretch_side_topbottom_0",
        "osrs_stretch_side_topbottom_1",
        "osrs_stretch_side_columns_0",
        "osrs_stretch_side_columns_1",
        "osrs_stretch_mapsurround",
        "compass",
        "compass_outline",
        "resize_map_mask",
        "resize_compass_mask",
        "tli_button01_orb01_34x34_0",
        "ring_34_0",
        "orb_xp_0",
        "ring_30",
        "worldmap_icon_0",
        "wiki_icon_0",
        "combatboxes_0",
        "combatboxes_1",
        "combatboxes_special_attack",
        "combat_shield",
        "miscgraphics_2",
        "miscgraphics_3",
        "options_icons_16",
        "options_icons_18",
        "options_icons_28",
        "whistle",
    };
    int ui_asset_count = (int)(sizeof(ui_asset_names) / sizeof(ui_asset_names[0]));
    for (int i = 0; i < ui_asset_count; i++) {
        gui_load_named_asset(gs, ui_asset_names[i]);
    }
    gui_load_named_asset_range(gs, "side_stone_highlights", 0, 4);
    gui_load_named_asset_range(gs, "sideicons_interface", 0, 16);
    gui_load_named_asset_range(gs, "combatboxes", 0, 3);
    gui_load_named_asset_range(gs, "combaticons", 0, 19);
    gui_load_named_asset_range(gs, "combaticons2", 0, 19);
    gui_load_named_asset_range(gs, "combaticons3", 0, 19);
    gui_load_named_asset_range(gs, "orb_frame", 0, 2);
    gui_load_named_asset_range(gs, "orb_filler", 0, 14);
    gui_load_named_asset_range(gs, "orb_icon", 0, 15);
    gui_load_named_asset_range(gs, "wornicons", 0, 11);
    gui_load_named_asset_range(gs, "skill_icon", 0, 23);
    gui_load_named_asset_range(gs, "prayeron", 0, 28);
    gui_load_named_asset_range(gs, "prayeroff", 0, 28);
    gui_load_named_asset_range(gs, "magicon", 0, 47);
    gui_load_named_asset_range(gs, "magicoff", 0, 47);
    gui_load_named_asset_range(gs, "standard_spell_on", 0, 79);

    static const char* side_icon_names[] = {
        "side_icon_combat",
        "side_icon_stats",
        "side_icon_quests",
        "side_icon_inventory",
        "side_icon_equipment",
        "side_icon_prayer",
        "side_icon_magic",
        "side_icon_magic_ancient",
        "side_icon_clan",
        "side_icon_friends",
        "side_icon_grouping",
        "side_icon_logout",
        "side_icon_options",
        "side_icon_emotes",
        "side_icon_music",
    };
    int side_icon_count = (int)(sizeof(side_icon_names) / sizeof(side_icon_names[0]));
    for (int i = 0; i < side_icon_count; i++) {
        gui_load_named_asset(gs, side_icon_names[i]);
    }

    if (!ok) {
        TraceLog(LOG_WARNING, "GUI: some sprites missing from data/sprites/gui/");
    }

    /* load item sprites from data/sprites/items/{item_id}.png */
    gs->item_sprite_count = 0;
    for (int i = 0; i < NUM_ITEMS && gs->item_sprite_count < GUI_MAX_ITEM_SPRITES; i++) {
        int item_id = ITEM_DATABASE[i].item_id;
        if (item_id <= 0) continue;
        gui_load_sprite_by_osrs_id_if_present(gs, item_id);
    }

    /* consumable sprites: not in ITEM_DATABASE, load by OSRS item ID directly */
    static const int consumable_ids[] = {
        OSRS_ID_SHARK, OSRS_ID_KARAMBWAN,
        OSRS_ID_BREW_4, OSRS_ID_BREW_3, OSRS_ID_BREW_2, OSRS_ID_BREW_1,
        OSRS_ID_RESTORE_4, OSRS_ID_RESTORE_3, OSRS_ID_RESTORE_2, OSRS_ID_RESTORE_1,
        OSRS_ID_COMBAT_4, OSRS_ID_COMBAT_3, OSRS_ID_COMBAT_2, OSRS_ID_COMBAT_1,
        OSRS_ID_RANGED_4, OSRS_ID_RANGED_3, OSRS_ID_RANGED_2, OSRS_ID_RANGED_1,
        OSRS_ID_ANTIVENOM_4, OSRS_ID_ANTIVENOM_3, OSRS_ID_ANTIVENOM_2, OSRS_ID_ANTIVENOM_1,
        OSRS_ID_PRAYER_POT_4, OSRS_ID_PRAYER_POT_3, OSRS_ID_PRAYER_POT_2, OSRS_ID_PRAYER_POT_1,
        OSRS_ID_BASTION_4, OSRS_ID_BASTION_3, OSRS_ID_BASTION_2, OSRS_ID_BASTION_1,
        OSRS_ID_STAMINA_4, OSRS_ID_STAMINA_3, OSRS_ID_STAMINA_2, OSRS_ID_STAMINA_1,
        OSRS_ID_SATURATED_HEART,
    };
    for (int i = 0; i < (int)(sizeof(consumable_ids)/sizeof(consumable_ids[0])); i++) {
        if (gs->item_sprite_count >= GUI_MAX_ITEM_SPRITES) break;
        int cid = consumable_ids[i];
        gui_load_sprite_by_osrs_id_if_present(gs, cid);
    }
    TraceLog(LOG_INFO, "GUI: loaded %d item sprites (incl consumables)", gs->item_sprite_count);
}

/** Look up item sprite texture by item database index. Returns NULL texture (id=0) if not found. */
static Texture2D gui_get_sprite_by_osrs_id(GuiState* gs, int osrs_id);

static Texture2D gui_get_item_sprite(GuiState* gs, uint8_t item_idx) {
    Texture2D empty = { 0 };
    if (item_idx == ITEM_NONE || item_idx >= NUM_ITEMS) return empty;
    int item_id = ITEM_DATABASE[item_idx].item_id;
    return gui_get_sprite_by_osrs_id(gs, item_id);
}

/** Look up item sprite texture by OSRS item ID directly (for consumables). */
static Texture2D gui_get_sprite_by_osrs_id(GuiState* gs, int osrs_id) {
    Texture2D empty = { 0 };
    if (osrs_id <= 0) return empty;
    return gui_load_sprite_by_osrs_id_if_present(gs, osrs_id);
}

static int gui_coin_stack_display_id(int quantity) {
    if (quantity <= 1) return 995;
    if (quantity == 2) return 996;
    if (quantity == 3) return 997;
    if (quantity == 4) return 998;
    if (quantity < 25) return 999;
    if (quantity < 100) return 1000;
    if (quantity < 250) return 1001;
    if (quantity < 1000) return 1002;
    return 1003;
}

static int gui_item_display_id_for_quantity(const GuiState* gs, int item_id, int quantity) {
    if (item_id <= 0 || quantity <= 1) return item_id;
    if (item_id == 995) return gui_coin_stack_display_id(quantity);
    int best_threshold = 0;
    int best_display_id = item_id;
    for (int i = 0; gs && i < gs->item_stack_variant_count; i++) {
        const GuiItemStackVariant* variant = &gs->item_stack_variants[i];
        if (variant->base_item_id != item_id) continue;
        if (quantity < variant->threshold) continue;
        if (variant->threshold < best_threshold) continue;
        best_threshold = variant->threshold;
        best_display_id = variant->display_item_id;
    }
    return best_display_id;
}

static Texture2D gui_get_item_sprite_for_quantity(
    GuiState* gs,
    uint8_t item_idx,
    int quantity
) {
    Texture2D empty = {0};
    if (item_idx == ITEM_NONE || item_idx >= NUM_ITEMS) return empty;
    int item_id = ITEM_DATABASE[item_idx].item_id;
    int display_id = gui_item_display_id_for_quantity(gs, item_id, quantity);
    Texture2D tex = gui_get_sprite_by_osrs_id(gs, display_id);
    if (tex.id == 0 && display_id != item_id) {
        tex = gui_get_sprite_by_osrs_id(gs, item_id);
    }
    return tex;
}

static Texture2D gui_get_sprite_by_osrs_id_for_quantity(
    GuiState* gs,
    int osrs_id,
    int quantity
) {
    int display_id = gui_item_display_id_for_quantity(gs, osrs_id, quantity);
    Texture2D tex = gui_get_sprite_by_osrs_id(gs, display_id);
    if (tex.id == 0 && display_id != osrs_id) {
        tex = gui_get_sprite_by_osrs_id(gs, osrs_id);
    }
    return tex;
}

static Color gui_stack_text_color(int quantity) {
    if (quantity >= 10000000) return GUI_TEXT_GREEN;
    if (quantity >= 100000) return GUI_TEXT_WHITE;
    return GUI_TEXT_YELLOW;
}

static void gui_format_stack_quantity(int quantity, char* dst, size_t cap) {
    if (!dst || cap == 0) return;
    if (quantity >= 10000000) {
        snprintf(dst, cap, "%dM", quantity / 1000000);
    } else if (quantity >= 100000) {
        snprintf(dst, cap, "%dK", quantity / 1000);
    } else {
        snprintf(dst, cap, "%d", quantity);
    }
}

/** Unload all GUI textures. */
static void gui_unload_sprites(GuiState* gs) {
    osrs_ui_interfaces_unload(&gs->ui_interfaces);
    if (!gs->sprites_loaded) return;
    for (int i = 0; i < gs->named_asset_count; i++) {
        if (gs->named_assets[i].tex.id) UnloadTexture(gs->named_assets[i].tex);
    }
    gs->named_asset_count = 0;
    for (int i = 0; i < GUI_NUM_SLOT_SPRITES; i++) UnloadTexture(gs->slot_sprites[i]);
    UnloadTexture(gs->slot_tile_bg);
    for (int i = 0; i < GUI_TAB_COUNT; i++) UnloadTexture(gs->tab_icons[i]);
    for (int i = 0; i < GUI_NUM_PRAYERS; i++) {
        UnloadTexture(gs->prayer_on[i]);
        UnloadTexture(gs->prayer_off[i]);
    }
    for (int i = 0; i < GUI_NUM_SPELLS; i++) {
        UnloadTexture(gs->spell_on[i]);
        UnloadTexture(gs->spell_off[i]);
    }
    if (gs->spec_bar_loaded) UnloadTexture(gs->spec_bar);
    if (gs->chrome_loaded) {
        UnloadTexture(gs->side_panel_bg);
        UnloadTexture(gs->tabs_row_bottom);
        UnloadTexture(gs->tabs_row_top);
        for (int i = 0; i < 5; i++) UnloadTexture(gs->tab_stone_sel[i]);
    }
    if (gs->slanted_tab.id) UnloadTexture(gs->slanted_tab);
    if (gs->slanted_tab_hover.id) UnloadTexture(gs->slanted_tab_hover);
    if (gs->slot_tile.id) UnloadTexture(gs->slot_tile);
    if (gs->slot_selected.id) UnloadTexture(gs->slot_selected);
    if (gs->orb_frame.id) UnloadTexture(gs->orb_frame);
    if (gs->minimap_compass.id) UnloadTexture(gs->minimap_compass);
    if (gs->minimap_compass_masked.id) UnloadTexture(gs->minimap_compass_masked);
    if (gs->minimap_alpha_mask.id) UnloadTexture(gs->minimap_alpha_mask);
    if (gs->minimap_frame.id) UnloadTexture(gs->minimap_frame);
    if (gs->rm_minimap_alpha_mask.id) UnloadTexture(gs->rm_minimap_alpha_mask);
    if (gs->rm_minimap_frame.id) UnloadTexture(gs->rm_minimap_frame);
    if (gs->rm_compass_alpha_mask.id) UnloadTexture(gs->rm_compass_alpha_mask);
    if (gs->rm_side_panel_bg.id) UnloadTexture(gs->rm_side_panel_bg);
    if (gs->rm_side_panel_edge_left.id) UnloadTexture(gs->rm_side_panel_edge_left);
    if (gs->rm_side_panel_edge_right.id) UnloadTexture(gs->rm_side_panel_edge_right);
    if (gs->rm_tabs_top_row.id) UnloadTexture(gs->rm_tabs_top_row);
    if (gs->rm_tabs_bottom_row.id) UnloadTexture(gs->rm_tabs_bottom_row);
    if (gs->rm_tab_stone.id) UnloadTexture(gs->rm_tab_stone);
    if (gs->rm_tab_stone_selected.id) UnloadTexture(gs->rm_tab_stone_selected);
    if (gs->orb_empty.id) UnloadTexture(gs->orb_empty);
    if (gs->orb_hp.id) UnloadTexture(gs->orb_hp);
    if (gs->orb_prayer.id) UnloadTexture(gs->orb_prayer);
    if (gs->orb_run.id) UnloadTexture(gs->orb_run);
    if (gs->orb_run_active.id) UnloadTexture(gs->orb_run_active);
    if (gs->orb_icon_hp.id) UnloadTexture(gs->orb_icon_hp);
    if (gs->orb_icon_prayer.id) UnloadTexture(gs->orb_icon_prayer);
    if (gs->orb_icon_walk.id) UnloadTexture(gs->orb_icon_walk);
    if (gs->orb_icon_run.id) UnloadTexture(gs->orb_icon_run);
    if (gs->minimap_dot_player.id) UnloadTexture(gs->minimap_dot_player);
    if (gs->minimap_dot_npc.id) UnloadTexture(gs->minimap_dot_npc);
    if (gs->minimap_dot_friend.id) UnloadTexture(gs->minimap_dot_friend);
    if (gs->minimap_dot_item.id) UnloadTexture(gs->minimap_dot_item);
    for (int i = 0; i < gs->item_sprite_count; i++) UnloadTexture(gs->item_sprite_tex[i]);
    if (gs->font_loaded) UnloadFont(gs->font);
    if (gs->small_font_loaded) UnloadFont(gs->small_font);
    gs->item_sprite_count = 0;
    gs->font_loaded = 0;
    gs->small_font_loaded = 0;
    gs->item_stack_variant_count = 0;
    gs->sprites_loaded = 0;
}


static const char* gui_item_short_name(uint8_t item_idx) {
    if (item_idx == ITEM_NONE || item_idx >= NUM_ITEMS) return "";
    const char* full = ITEM_DATABASE[item_idx].name;
    switch (item_idx) {
        case ITEM_HELM_NEITIZNOT:    return "Neit helm";
        case ITEM_GOD_CAPE:          return "God cape";
        case ITEM_GLORY:             return "Glory";
        case ITEM_BLACK_DHIDE_BODY:  return "Dhide body";
        case ITEM_MYSTIC_TOP:        return "Mystic top";
        case ITEM_RUNE_PLATELEGS:    return "Rune legs";
        case ITEM_MYSTIC_BOTTOM:     return "Mystic bot";
        case ITEM_WHIP:              return "Whip";
        case ITEM_RUNE_CROSSBOW:     return "Rune cbow";
        case ITEM_AHRIM_STAFF:       return "Ahrim stf";
        case ITEM_DRAGON_DAGGER:     return "DDS";
        case ITEM_DRAGON_DEFENDER:   return "D defender";
        case ITEM_SPIRIT_SHIELD:     return "Spirit sh";
        case ITEM_BARROWS_GLOVES:    return "B gloves";
        case ITEM_CLIMBING_BOOTS:    return "Climb boot";
        case ITEM_BERSERKER_RING:    return "B ring";
        case ITEM_DIAMOND_BOLTS_E:   return "D bolts(e)";
        case ITEM_GHRAZI_RAPIER:     return "Rapier";
        case ITEM_INQUISITORS_MACE:  return "Inq mace";
        case ITEM_STAFF_OF_DEAD:     return "SOTD";
        case ITEM_KODAI_WAND:        return "Kodai";
        case ITEM_DRAGON_HUNTER_WAND: return "DH wand";
        case ITEM_VOLATILE_STAFF:    return "Volatile";
        case ITEM_ZURIELS_STAFF:     return "Zuriel stf";
        case ITEM_ARMADYL_CROSSBOW:  return "ACB";
        case ITEM_ZARYTE_CROSSBOW:   return "ZCB";
        case ITEM_DRAGON_CLAWS:      return "D claws";
        case ITEM_AGS:               return "AGS";
        case ITEM_ANCIENT_GS:        return "Anc GS";
        case ITEM_GRANITE_MAUL:      return "G maul";
        case ITEM_ELDER_MAUL:        return "Elder maul";
        case ITEM_DARK_BOW:          return "Dark bow";
        case ITEM_HEAVY_BALLISTA:    return "Ballista";
        case ITEM_VESTAS:            return "Vesta's";
        case ITEM_VOIDWAKER:         return "Voidwaker";
        case ITEM_STATIUS_WARHAMMER: return "SWH";
        case ITEM_MORRIGANS_JAVELIN: return "Morr jav";
        case ITEM_ANCESTRAL_HAT:     return "Anc hat";
        case ITEM_ANCESTRAL_TOP:     return "Anc top";
        case ITEM_ANCESTRAL_BOTTOM:  return "Anc bot";
        case ITEM_AHRIMS_ROBETOP:    return "Ahrim top";
        case ITEM_AHRIMS_ROBESKIRT:  return "Ahrim skrt";
        case ITEM_KARILS_TOP:        return "Karil top";
        case ITEM_BANDOS_TASSETS:    return "Tassets";
        case ITEM_BLESSED_SPIRIT_SHIELD: return "BSS";
        case ITEM_FURY:              return "Fury";
        case ITEM_OCCULT_NECKLACE:   return "Occult";
        case ITEM_INFERNAL_CAPE:     return "Infernal";
        case ITEM_ETERNAL_BOOTS:     return "Eternal";
        case ITEM_SEERS_RING_I:      return "Seers (i)";
        case ITEM_LIGHTBEARER:       return "Lightbear";
        case ITEM_MAGES_BOOK:        return "Mage book";
        case ITEM_DRAGON_ARROWS:     return "D arrows";
        case ITEM_TORAGS_PLATELEGS:  return "Torag legs";
        case ITEM_DHAROKS_PLATELEGS: return "DH legs";
        case ITEM_VERACS_PLATESKIRT: return "Verac skrt";
        case ITEM_TORAGS_HELM:       return "Torag helm";
        case ITEM_DHAROKS_HELM:      return "DH helm";
        case ITEM_VERACS_HELM:       return "Verac helm";
        case ITEM_GUTHANS_HELM:      return "Guth helm";
        case ITEM_OPAL_DRAGON_BOLTS: return "Opal bolt";
        case ITEM_IMBUED_SARA_CAPE:  return "Sara cape";
        case ITEM_EYE_OF_AYAK:       return "Eye Ayak";
        case ITEM_ELIDINIS_WARD_F:   return "Eld ward";
        case ITEM_CONFLICTION_GAUNTLETS: return "Confl gnt";
        case ITEM_AVERNIC_TREADS:    return "Avernic bt";
        case ITEM_RING_OF_SUFFERING_RI: return "Suff (ri)";
        case ITEM_TWISTED_BOW:       return "T bow";
        case ITEM_ELYSIAN_SPIRIT_SHIELD: return "Elysian";
        case ITEM_MASORI_MASK_F:     return "Masori msk";
        case ITEM_MASORI_BODY_F:     return "Masori bod";
        case ITEM_MASORI_CHAPS_F:    return "Masori chp";
        case ITEM_NECKLACE_OF_ANGUISH: return "Anguish";
        case ITEM_DIZANAS_QUIVER:    return "Dizana qvr";
        case ITEM_ZARYTE_VAMBRACES:  return "Zaryte vam";
        case ITEM_TOXIC_BLOWPIPE:    return "Blowpipe";
        case ITEM_AHRIMS_HOOD:       return "Ahrim hood";
        case ITEM_TORMENTED_BRACELET: return "Tormented";
        case ITEM_SANGUINESTI_STAFF: return "Sang staff";
        case ITEM_INFINITY_BOOTS:    return "Inf boots";
        case ITEM_GOD_BLESSING:      return "Blessing";
        case ITEM_RING_OF_RECOIL:    return "Recoil";
        case ITEM_VENATOR_RING:      return "Venator";
        case ITEM_VIRTUS_MASK:       return "Virtus msk";
        case ITEM_VIRTUS_ROBE_TOP:   return "Virtus top";
        case ITEM_VIRTUS_ROBE_BOTTOM:return "Virtus bot";
        case ITEM_CRYSTAL_HELM:      return "Crystal hm";
        case ITEM_AVAS_ASSEMBLER:    return "Assembler";
        case ITEM_CRYSTAL_BODY:      return "Crystal bd";
        case ITEM_CRYSTAL_LEGS:      return "Crystal lg";
        case ITEM_BOW_OF_FAERDHINEN: return "Fbow";
        case ITEM_CRYSTAL_SHIELD:    return "Crystal sh";
        case ITEM_ECHO_BOOTS:        return "Echo boots";
        case ITEM_BLESSED_DHIDE_BOOTS: return "Bless boot";
        case ITEM_MYSTIC_HAT:        return "Mystic hat";
        case ITEM_TRIDENT_OF_SWAMP:  return "Trident";
        case ITEM_BOOK_OF_DARKNESS:  return "Book dark";
        case ITEM_AMETHYST_ARROW:    return "Ameth arw";
        case ITEM_MYSTIC_BOOTS:      return "Myst boots";
        case ITEM_BLESSED_COIF:      return "Bless coif";
        case ITEM_BLACK_DHIDE_CHAPS: return "Dhide chap";
        case ITEM_MAGIC_SHORTBOW_I:  return "MSB (i)";
        case ITEM_AVAS_ACCUMULATOR:  return "Accumulate";
        default: return full;
    }
}


/** Draw text with OSRS-style shadow (black at +1,+1, then color). */
static void gui_text_shadow(
    const GuiState* gs,
    const char* text,
    int x,
    int y,
    int size,
    Color color
) {
    if (!text || !text[0]) return;
    Font font = gui_font_for_size(gs, size);
    Vector2 shadow = {(float)x + 1.0f, (float)y + 1.0f};
    Vector2 pos = {(float)x, (float)y};
    DrawTextEx(font, text, shadow, (float)size, 0.0f, GUI_TEXT_SHADOW);
    DrawTextEx(font, text, pos, (float)size, 0.0f, color);
}

/** Draw an OSRS-style beveled slot rectangle. */
static void gui_draw_slot(int x, int y, int w, int h, Color fill) {
    DrawRectangle(x, y, w, h, fill);
    DrawRectangleLines(x, y, w, h, GUI_BORDER);
    DrawLine(x + 1, y + 1, x + w - 2, y + 1, GUI_BORDER_LT);
    DrawLine(x + 1, y + 1, x + 1, y + h - 2, GUI_BORDER_LT);
}

/** Draw texture centered within a box, scaled to fit. */
static void gui_draw_tex_centered(Texture2D tex, int bx, int by, int bw, int bh) {
    if (tex.id == 0) return;
    /* scale to fit while maintaining aspect ratio */
    float sx = (float)(bw - 4) / (float)tex.width;
    float sy = (float)(bh - 4) / (float)tex.height;
    float s = (sx < sy) ? sx : sy;
    int dw = (int)(tex.width * s);
    int dh = (int)(tex.height * s);
    int dx = bx + (bw - dw) / 2;
    int dy = by + (bh - dh) / 2;
    DrawTextureEx(tex, (Vector2){ (float)dx, (float)dy }, 0.0f, s, WHITE);
}

/** Draw an equipment slot using real OSRS slot tile sprite + item/silhouette sprite. */
static void gui_draw_equip_slot(GuiState* gs, int x, int y, int w, int h,
                                int gear_slot, uint8_t item_idx) {
    /* draw slot_tile (real OSRS 36x36 stone square) as background */
    if (gs->slot_tile.id != 0) {
        Rectangle src = { 0, 0, (float)gs->slot_tile.width, (float)gs->slot_tile.height };
        Rectangle dst = { (float)x, (float)y, (float)w, (float)h };
        DrawTexturePro(gs->slot_tile, src, dst, (Vector2){0,0}, 0.0f, WHITE);
    } else {
        gui_draw_slot(x, y, w, h, GUI_BG_SLOT);
    }

    /* draw item sprite if equipped, else slot silhouette */
    if (item_idx != ITEM_NONE && item_idx < NUM_ITEMS) {
        Texture2D item_tex = gui_get_item_sprite(gs, item_idx);
        if (item_tex.id != 0) {
            gui_draw_tex_centered(item_tex, x, y, w, h);
        } else {
            const char* name = gui_item_short_name(item_idx);
            gui_text_shadow(gs, name, x + 2, y + h / 2 - 4, 7, GUI_TEXT_YELLOW);
        }
    } else if (gs->sprites_loaded && gear_slot >= 0 && gear_slot < GUI_NUM_SLOT_SPRITES) {
        Texture2D bg = gs->slot_sprites[gear_slot];
        if (bg.id != 0) {
            gui_draw_tex_centered(bg, x, y, w, h);
        }
    }
}


typedef struct {
    int logical_tab;
    const char* stone_asset;
    const char* icon_asset;
    Rectangle rect;
    Rectangle icon_rect;
} GuiSideStoneRef;

static const GuiSideStoneRef GUI_SIDE_STONES[] = {
    {GUI_TAB_COMBAT,    "side_stone_highlights_0", "side_icon_combat",    {0,   0, 38, 36}, {4,   0, 33, 36}},
    {GUI_TAB_STATS,     "side_stone_highlights_4", "side_icon_stats",     {38,  0, 33, 36}, {38,  0, 33, 36}},
    {GUI_TAB_QUESTS,    "side_stone_highlights_4", "side_icon_quests",    {71,  0, 38, 36}, {71,  0, 33, 36}},
    {GUI_TAB_INVENTORY, "side_stone_highlights_4", "side_icon_inventory", {104, 0, 33, 36}, {104, 0, 33, 36}},
    {GUI_TAB_EQUIPMENT, "side_stone_highlights_4", "side_icon_equipment", {137, 0, 33, 36}, {137, 0, 33, 36}},
    {GUI_TAB_PRAYER,    "side_stone_highlights_4", "side_icon_prayer",    {170, 0, 33, 36}, {170, 0, 33, 36}},
    {GUI_TAB_SPELLBOOK, "side_stone_highlights_1", "side_icon_magic_ancient", {203, 0, 38, 36}, {204, 0, 33, 36}},
    {-1, "side_stone_highlights_2", "side_icon_clan",     {0,   0, 38, 36}, {4,   0, 33, 36}},
    {-1, "side_stone_highlights_4", "side_icon_friends",  {38,  0, 33, 36}, {38,  0, 33, 36}},
    {-1, "side_stone_highlights_4", "side_icon_grouping", {71,  0, 33, 36}, {71,  0, 33, 36}},
    {-1, "side_stone_highlights_4", "side_icon_logout",   {104, 0, 33, 36}, {104, 0, 33, 36}},
    {-1, "side_stone_highlights_4", "side_icon_options",  {137, 0, 33, 36}, {137, 0, 33, 36}},
    {-1, "side_stone_highlights_4", "side_icon_emotes",   {170, 0, 33, 36}, {170, 0, 33, 36}},
    {-1, "side_stone_highlights_3", "side_icon_music",    {203, 0, 38, 36}, {204, 0, 33, 36}},
};

static Rectangle gui_side_content_rect(GuiState* gs) {
    return (Rectangle){
        (float)(gs->panel_x + GUI_SIDE_CONTENT_X),
        (float)(gs->panel_y + GUI_SIDE_CONTENT_Y),
        (float)GUI_SIDE_CONTENT_W,
        (float)GUI_SIDE_CONTENT_H,
    };
}

static Rectangle gui_side_ref_rect(GuiState* gs, Rectangle ref) {
    Rectangle content = gui_side_content_rect(gs);
    return (Rectangle){content.x + ref.x, content.y + ref.y, ref.width, ref.height};
}

static Rectangle gui_side_component_rect(
    GuiState* gs,
    const char* group_name,
    const char* component_name,
    Rectangle fallback
) {
    Rectangle out = {0};
    if (osrs_ui_interfaces_component_rect(
            &gs->ui_interfaces,
            group_name,
            component_name,
            gui_side_content_rect(gs),
            &out)) {
        return out;
    }
    return gui_side_ref_rect(gs, fallback);
}

static void gui_draw_texture(Texture2D tex, Rectangle dst, Color tint) {
    if (tex.id == 0) return;
    Rectangle src = {0, 0, (float)tex.width, (float)tex.height};
    DrawTexturePro(tex, src, dst, (Vector2){0, 0}, 0.0f, tint);
}

static Rectangle gui_texture_fit_rect(
    int texture_w,
    int texture_h,
    Rectangle rect,
    float max_w,
    float max_h
) {
    assert(texture_w > 0);
    assert(texture_h > 0);
    float scale_x = max_w / (float)texture_w;
    float scale_y = max_h / (float)texture_h;
    float scale = scale_x < scale_y ? scale_x : scale_y;
    if (scale > 1.0f) scale = 1.0f;
    float width = (float)texture_w * scale;
    float height = (float)texture_h * scale;
    return (Rectangle){
        rect.x + (rect.width - width) * 0.5f,
        rect.y + (rect.height - height) * 0.5f,
        width,
        height,
    };
}

static void gui_draw_texture_centered(
    Texture2D tex,
    Rectangle rect,
    float max_w,
    float max_h,
    Color tint
) {
    if (tex.id == 0) return;
    gui_draw_texture(tex, gui_texture_fit_rect(tex.width, tex.height, rect, max_w, max_h), tint);
}

static void gui_draw_named_asset(GuiState* gs, const char* name, Rectangle dst, Color tint) {
    gui_draw_texture(gui_asset(gs, name), dst, tint);
}

static int gui_draw_named_asset_centered(
    GuiState* gs,
    const char* name,
    Rectangle rect,
    float max_w,
    float max_h,
    Color tint
) {
    Texture2D tex = gui_asset(gs, name);
    if (tex.id == 0) return 0;
    gui_draw_texture_centered(tex, rect, max_w, max_h, tint);
    return 1;
}

static void gui_draw_named_asset_tiled(GuiState* gs, const char* name, Rectangle dst, Color tint) {
    Texture2D tex = gui_asset(gs, name);
    if (tex.id == 0) {
        DrawRectangleRec(dst, GUI_BG_DARK);
        return;
    }
    for (float y = dst.y; y < dst.y + dst.height; y += (float)tex.height) {
        for (float x = dst.x; x < dst.x + dst.width; x += (float)tex.width) {
            float w = (x + tex.width > dst.x + dst.width) ? dst.x + dst.width - x : (float)tex.width;
            float h = (y + tex.height > dst.y + dst.height) ? dst.y + dst.height - y : (float)tex.height;
            Rectangle src = {0, 0, w, h};
            Rectangle part = {x, y, w, h};
            DrawTexturePro(tex, src, part, (Vector2){0, 0}, 0.0f, tint);
        }
    }
}

static Color gui_ui_color_from_rgb(int rgb, unsigned char opacity) {
    return (Color){
        (unsigned char)((rgb >> 16) & 0xff),
        (unsigned char)((rgb >> 8) & 0xff),
        (unsigned char)(rgb & 0xff),
        (unsigned char)(255 - opacity),
    };
}

static const GuiUiComponentOverride* gui_ui_component_override(
    const GuiUiOverrides* overrides,
    uint32_t component_id
) {
    if (!overrides) return NULL;
    for (int i = 0; i < overrides->component_count; i++) {
        if (overrides->components[i].component_id == component_id) {
            return &overrides->components[i];
        }
    }
    return NULL;
}

static GuiUiComponentOverride* gui_ui_push_component_override(
    GuiUiComponentOverride* overrides,
    int* count,
    uint32_t component_id
) {
    assert(*count < GUI_UI_MAX_COMPONENT_OVERRIDES);
    GuiUiComponentOverride* override = &overrides[(*count)++];
    memset(override, 0, sizeof(*override));
    override->component_id = component_id;
    return override;
}

static const GuiUiItemContainerOverride* gui_ui_item_container_override(
    const GuiUiOverrides* overrides,
    uint32_t component_id
) {
    if (!overrides) return NULL;
    for (int i = 0; i < overrides->item_container_count; i++) {
        if (overrides->item_containers[i].component_id == component_id) {
            return &overrides->item_containers[i];
        }
    }
    return NULL;
}

static void gui_draw_ui_item_slot(GuiState* gs, const GuiUiItemSlot* slot, Rectangle rect) {
    if (slot->gear_slot >= 0) {
        uint8_t item_idx = slot->enabled ? slot->item_db_idx : ITEM_NONE;
        gui_draw_equip_slot(
            gs,
            (int)rect.x,
            (int)rect.y,
            (int)rect.width,
            (int)rect.height,
            slot->gear_slot,
            item_idx);
        if (!slot->enabled && slot->empty_asset) {
            gui_draw_named_asset_centered(gs, slot->empty_asset, rect, rect.width, rect.height, WHITE);
        }
    } else {
        DrawRectangleRec(rect, (Color){0, 0, 0, 30});
        if (!slot->enabled) return;

        Texture2D tex = {0};
        if (slot->item_db_idx != ITEM_NONE) {
            tex = gui_get_item_sprite_for_quantity(gs, slot->item_db_idx, slot->quantity);
        } else if (slot->osrs_id > 0) {
            tex = gui_get_sprite_by_osrs_id_for_quantity(gs, slot->osrs_id, slot->quantity);
        }

        Color tint = WHITE;
        tint.a = slot->alpha == 0 ? 255 : slot->alpha;
        if (tex.id != 0) {
            Rectangle src = {0, 0, (float)tex.width, (float)tex.height};
            DrawTexturePro(tex, src, rect, (Vector2){0, 0}, 0.0f, tint);
        }

        if (slot->quantity > 1) {
            char text[16];
            gui_format_stack_quantity(slot->quantity, text, sizeof(text));
            gui_text_shadow(gs, text, (int)rect.x + 1, (int)rect.y - 1, 10,
                gui_stack_text_color(slot->quantity));
        }
    }

    if (slot->selected) {
        DrawRectangleLinesEx((Rectangle){rect.x - 1, rect.y - 1, rect.width + 2, rect.height + 2},
            2.0f, GUI_TEXT_YELLOW);
    }
}

static void gui_draw_ui_item_container(
    GuiState* gs,
    const GuiUiItemContainerOverride* container,
    Rectangle rect
) {
    if (!container || container->columns <= 0) return;
    int count = container->slot_count;
    if (count > GUI_UI_ITEM_CONTAINER_MAX_SLOTS) count = GUI_UI_ITEM_CONTAINER_MAX_SLOTS;
    for (int i = 0; i < count; i++) {
        int col = i % container->columns;
        int row = i / container->columns;
        Rectangle slot = {
            rect.x + container->x0 + (float)col * container->step_x,
            rect.y + container->y0 + (float)row * container->step_y,
            container->slot_w,
            container->slot_h,
        };
        gui_draw_ui_item_slot(gs, &container->slots[i], slot);
    }
}

static void gui_draw_ui_sprite_component(
    GuiState* gs,
    const OsrsUiComponent* component,
    const GuiUiComponentOverride* override,
    Rectangle rect
) {
    Texture2D tex = {0};
    if (override && override->sprite_asset) {
        tex = gui_asset(gs, override->sprite_asset);
    } else {
        int sprite_id = override && override->sprite_present ? override->sprite_id : component->sprite_id;
        tex = gui_sprite_asset(gs, sprite_id);
    }
    if (tex.id == 0) return;

    if (rect.width <= 0) rect.width = (float)tex.width;
    if (rect.height <= 0) rect.height = (float)tex.height;
    Color tint = WHITE;
    tint.a = (unsigned char)(255 - component->opacity);
    Rectangle src = {
        component->flipped_horizontally ? (float)tex.width : 0.0f,
        component->flipped_vertically ? (float)tex.height : 0.0f,
        component->flipped_horizontally ? -(float)tex.width : (float)tex.width,
        component->flipped_vertically ? -(float)tex.height : (float)tex.height,
    };

    if (component->shadow_color != 0) {
        Color shadow = gui_ui_color_from_rgb(component->shadow_color, component->opacity);
        DrawTexturePro(tex, src,
            (Rectangle){rect.x + 1.0f, rect.y + 1.0f, rect.width, rect.height},
            (Vector2){0, 0}, 0.0f, shadow);
    }
    if (component->border_type > 0) {
        Color border = BLACK;
        border.a = tint.a;
        int layers = component->border_type < 3 ? component->border_type : 3;
        for (int layer = 0; layer < layers; layer++) {
            float offset = (float)(layer + 1);
            DrawTexturePro(tex, src,
                (Rectangle){rect.x - offset, rect.y, rect.width, rect.height},
                (Vector2){0, 0}, 0.0f, border);
            DrawTexturePro(tex, src,
                (Rectangle){rect.x + offset, rect.y, rect.width, rect.height},
                (Vector2){0, 0}, 0.0f, border);
            DrawTexturePro(tex, src,
                (Rectangle){rect.x, rect.y - offset, rect.width, rect.height},
                (Vector2){0, 0}, 0.0f, border);
            DrawTexturePro(tex, src,
                (Rectangle){rect.x, rect.y + offset, rect.width, rect.height},
                (Vector2){0, 0}, 0.0f, border);
        }
    }

    if (component->sprite_tiling) {
        for (float y = rect.y; y < rect.y + rect.height; y += (float)tex.height) {
            for (float x = rect.x; x < rect.x + rect.width; x += (float)tex.width) {
                float w = x + tex.width > rect.x + rect.width
                    ? rect.x + rect.width - x
                    : (float)tex.width;
                float h = y + tex.height > rect.y + rect.height
                    ? rect.y + rect.height - y
                    : (float)tex.height;
                Rectangle part_src = src;
                part_src.width = component->flipped_horizontally ? -w : w;
                part_src.height = component->flipped_vertically ? -h : h;
                DrawTexturePro(tex, part_src, (Rectangle){x, y, w, h},
                    (Vector2){0, 0}, 0.0f, tint);
            }
        }
    } else {
        DrawTexturePro(tex, src, rect, (Vector2){0, 0}, 0.0f, tint);
    }
}

static void gui_draw_ui_text_component(
    const GuiState* gs,
    const OsrsUiComponent* component,
    const GuiUiComponentOverride* override,
    Rectangle rect
) {
    const char* text = override && override->text ? override->text : component->text;
    if (!text || !text[0]) return;
    int size = component->line_height > 0 ? component->line_height + 9 : 12;
    if (size < 10) size = 10;
    Color color = gui_ui_color_from_rgb(component->text_color, component->opacity);
    int width = gui_measure_text(gs, text, size);
    int x = (int)rect.x;
    int y = (int)rect.y;
    if (component->x_text_alignment == 1) {
        x = (int)(rect.x + (rect.width - width) * 0.5f);
    } else if (component->x_text_alignment == 2) {
        x = (int)(rect.x + rect.width - width);
    }
    if (component->y_text_alignment == 1) {
        y = (int)(rect.y + (rect.height - size) * 0.5f);
    } else if (component->y_text_alignment == 2) {
        y = (int)(rect.y + rect.height - size);
    }
    Font font = gui_font_for_size(gs, size);
    if (component->text_shadowed) {
        DrawTextEx(font, text, (Vector2){(float)x + 1.0f, (float)y + 1.0f},
            (float)size, 0.0f, BLACK);
    }
    DrawTextEx(font, text, (Vector2){(float)x, (float)y}, (float)size, 0.0f, color);
}

static void gui_draw_ui_component(
    GuiState* gs,
    const OsrsUiInterfaceGroup* group,
    const OsrsUiComponent* component,
    Rectangle rect,
    GuiUiClipState* clip,
    const GuiUiOverrides* overrides
) {
    const GuiUiComponentOverride* override = gui_ui_component_override(overrides, component->id);
    if ((component->hidden && !(override && override->force_visible))
        || (override && override->hidden)) {
        return;
    }
    if (clip && clip->active && !gui_rect_has_area(gui_rect_intersect(rect, clip->current))) {
        return;
    }

    if (override && (override->sprite_asset || override->sprite_present) && component->type != 5) {
        gui_draw_ui_sprite_component(gs, component, override, rect);
    }

    if (component->type == 3) {
        Color color = gui_ui_color_from_rgb(component->text_color, component->opacity);
        if (component->filled) {
            DrawRectangleRec(rect, color);
        } else {
            DrawRectangleLinesEx(rect, 1, color);
        }
    } else if (component->type == 4) {
        gui_draw_ui_text_component(gs, component, override, rect);
    } else if (component->type == 5) {
        gui_draw_ui_sprite_component(gs, component, override, rect);
    } else if (component->type == 6 && component->model_type == 4 && component->model_id > 0) {
        GuiUiItemSlot slot = {
            .enabled = 1,
            .osrs_id = component->model_id,
            .quantity = 1,
            .alpha = 255,
            .gear_slot = -1,
        };
        gui_draw_ui_item_slot(gs, &slot, rect);
    } else if (component->type == 9) {
        Color color = gui_ui_color_from_rgb(component->text_color, component->opacity);
        Vector2 a = {rect.x, component->line_direction ? rect.y + rect.height : rect.y};
        Vector2 b = {rect.x + rect.width, component->line_direction ? rect.y : rect.y + rect.height};
        DrawLineEx(a, b, component->line_width > 0 ? (float)component->line_width : 1.0f, color);
    }

    Rectangle child_parent = component->type == 0
        ? osrs_ui_rect_expand_to_scroll(rect, component)
        : rect;
    for (int i = 0; i < group->component_count; i++) {
        const OsrsUiComponent* child = &group->components[i];
        if (child->parent_id != (int32_t)component->id) continue;
        Rectangle child_rect = osrs_ui_layout_component(child, child_parent, 0);
        if (component->type == 0 && gui_rect_has_area(rect)) {
            Rectangle prev = {0};
            int prev_active = 0;
            gui_push_clip(clip, rect, &prev, &prev_active);
            gui_draw_ui_component(gs, group, child, child_rect, clip, overrides);
            gui_pop_clip(clip, prev, prev_active);
        } else {
            gui_draw_ui_component(gs, group, child, child_rect, clip, overrides);
        }
    }

    const GuiUiItemContainerOverride* container =
        gui_ui_item_container_override(overrides, component->id);
    if (container) gui_draw_ui_item_container(gs, container, rect);
    if (override && override->item.present) {
        gui_draw_ui_item_slot(gs, &override->item, rect);
    }
}

static int gui_draw_ui_group(
    GuiState* gs,
    const char* group_name,
    Rectangle mount,
    const GuiUiOverrides* overrides
) {
    const OsrsUiInterfaceGroup* group = osrs_ui_interface_group(&gs->ui_interfaces, group_name);
    if (!group) return 0;
    GuiUiClipState clip = {0};
    Rectangle prev = {0};
    int prev_active = 0;
    gui_push_clip(&clip, mount, &prev, &prev_active);
    for (int i = 0; i < group->component_count; i++) {
        const OsrsUiComponent* component = &group->components[i];
        if (component->parent_id != -1) continue;
        Rectangle rect = osrs_ui_layout_component(
            component, mount, osrs_ui_component_uses_mount_rect(component));
        gui_draw_ui_component(gs, group, component, rect, &clip, overrides);
    }
    gui_pop_clip(&clip, prev, prev_active);
    return 1;
}

static void gui_draw_side_chrome(GuiState* gs) {
    Rectangle backing = {(float)(gs->panel_x + 20), (float)(gs->panel_y + 27), 200.0f, 281.0f};
    gui_draw_named_asset_tiled(gs, "tradebacking_dark", backing, WHITE);

    gui_draw_named_asset(gs, "osrs_stretch_side_topbottom_0",
        (Rectangle){(float)gs->panel_x, (float)(gs->panel_y + GUI_SIDE_TOP_Y), GUI_SIDE_MENU_W, 37}, WHITE);
    gui_draw_named_asset(gs, "osrs_stretch_side_topbottom_1",
        (Rectangle){(float)gs->panel_x, (float)(gs->panel_y + GUI_SIDE_BOTTOM_Y), GUI_SIDE_MENU_W, 37}, WHITE);
    gui_draw_named_asset(gs, "osrs_stretch_side_columns_0",
        (Rectangle){(float)(gs->panel_x + 2), (float)(gs->panel_y + 37), 26, 261}, WHITE);
    gui_draw_named_asset(gs, "osrs_stretch_side_columns_1",
        (Rectangle){(float)(gs->panel_x + 212), (float)(gs->panel_y + 37), 26, 261}, WHITE);

    int count = (int)(sizeof(GUI_SIDE_STONES) / sizeof(GUI_SIDE_STONES[0]));
    for (int i = 0; i < count; i++) {
        const GuiSideStoneRef* ref = &GUI_SIDE_STONES[i];
        if (ref->logical_tab != (int)gs->active_tab) continue;
        int row_y = gs->panel_y + (i < 7 ? GUI_SIDE_TOP_Y : GUI_SIDE_BOTTOM_Y);
        int pressed = gs->tab_press_timer[gs->active_tab] > 0 ? 1 : 0;
        Rectangle stone = {
            (float)(gs->panel_x + (int)ref->rect.x),
            (float)(row_y + (int)ref->rect.y + pressed),
            ref->rect.width,
            ref->rect.height,
        };
        gui_draw_named_asset_tiled(gs, ref->stone_asset, stone, WHITE);
        DrawRectangleRec(stone, (Color){145, 22, 18, (unsigned char)(pressed ? 72 : 44)});
        break;
    }

    for (int i = 0; i < count; i++) {
        const GuiSideStoneRef* ref = &GUI_SIDE_STONES[i];
        int row_y = gs->panel_y + (i < 7 ? GUI_SIDE_TOP_Y : GUI_SIDE_BOTTOM_Y);
        int pressed = ref->logical_tab == (int)gs->active_tab &&
            gs->tab_press_timer[gs->active_tab] > 0 ? 1 : 0;
        Rectangle icon = {
            (float)(gs->panel_x + (int)ref->icon_rect.x),
            (float)(row_y + (int)ref->icon_rect.y + pressed),
            ref->icon_rect.width,
            ref->icon_rect.height,
        };
        gui_draw_named_asset(gs, ref->icon_asset, icon, WHITE);
    }
}

static void gui_draw_tab_bar(GuiState* gs) {
    gui_draw_side_chrome(gs);
}

static int gui_handle_tab_click(GuiState* gs, int mouse_x, int mouse_y) {
    int count = (int)(sizeof(GUI_SIDE_STONES) / sizeof(GUI_SIDE_STONES[0]));
    for (int i = 0; i < count; i++) {
        const GuiSideStoneRef* ref = &GUI_SIDE_STONES[i];
        if (ref->logical_tab < 0) continue;
        int row_y = gs->panel_y + (i < 7 ? GUI_SIDE_TOP_Y : GUI_SIDE_BOTTOM_Y);
        int x = gs->panel_x + (int)ref->rect.x;
        int y = row_y + (int)ref->rect.y;
        if (mouse_x >= x && mouse_x < x + (int)ref->rect.width &&
            mouse_y >= y && mouse_y < y + (int)ref->rect.height) {
            gs->active_tab = (GuiTab)ref->logical_tab;
            gs->tab_press_timer[ref->logical_tab] = GUI_TAB_PRESS_TICKS;
            return 1;
        }
    }
    return 0;
}


static int gui_content_y(GuiState* gs) {
    return gs->panel_y + gs->status_bar_h + gs->tab_h;
}


/* inventory grid: OSRS native static-pixel layout */
#define INV_COLS 4
#define INV_ROWS 7
#define INV_PANEL_CONTENT_X GUI_SIDE_CONTENT_X
#define INV_SLOT_X 14
#define INV_SLOT_Y 8
#define INV_CELL_W 42
#define INV_CELL_H 36
#define INV_SPRITE_W 32
#define INV_SPRITE_H 32

/** Get the OSRS item ID for a consumable based on remaining doses/count. */
static int gui_consumable_osrs_id(InvSlotType type, int doses) {
    switch (type) {
        case INV_SLOT_FOOD:       return OSRS_ID_SHARK;
        case INV_SLOT_KARAMBWAN:  return OSRS_ID_KARAMBWAN;
        case INV_SLOT_BREW:
            if (doses >= 4) return OSRS_ID_BREW_4;
            if (doses == 3) return OSRS_ID_BREW_3;
            if (doses == 2) return OSRS_ID_BREW_2;
            return OSRS_ID_BREW_1;
        case INV_SLOT_RESTORE:
            if (doses >= 4) return OSRS_ID_RESTORE_4;
            if (doses == 3) return OSRS_ID_RESTORE_3;
            if (doses == 2) return OSRS_ID_RESTORE_2;
            return OSRS_ID_RESTORE_1;
        case INV_SLOT_COMBAT_POT:
            if (doses >= 4) return OSRS_ID_COMBAT_4;
            if (doses == 3) return OSRS_ID_COMBAT_3;
            if (doses == 2) return OSRS_ID_COMBAT_2;
            return OSRS_ID_COMBAT_1;
        case INV_SLOT_RANGED_POT:
            if (doses >= 4) return OSRS_ID_RANGED_4;
            if (doses == 3) return OSRS_ID_RANGED_3;
            if (doses == 2) return OSRS_ID_RANGED_2;
            return OSRS_ID_RANGED_1;
        case INV_SLOT_ANTIVENOM:
            if (doses >= 4) return OSRS_ID_ANTIVENOM_4;
            if (doses == 3) return OSRS_ID_ANTIVENOM_3;
            if (doses == 2) return OSRS_ID_ANTIVENOM_2;
            return OSRS_ID_ANTIVENOM_1;
        case INV_SLOT_PRAYER_POT:
            if (doses >= 4) return OSRS_ID_PRAYER_POT_4;
            if (doses == 3) return OSRS_ID_PRAYER_POT_3;
            if (doses == 2) return OSRS_ID_PRAYER_POT_2;
            return OSRS_ID_PRAYER_POT_1;
        case INV_SLOT_BASTION_POT:
            if (doses >= 4) return OSRS_ID_BASTION_4;
            if (doses == 3) return OSRS_ID_BASTION_3;
            if (doses == 2) return OSRS_ID_BASTION_2;
            return OSRS_ID_BASTION_1;
        case INV_SLOT_STAMINA_POT:
            if (doses >= 4) return OSRS_ID_STAMINA_4;
            if (doses == 3) return OSRS_ID_STAMINA_3;
            if (doses == 2) return OSRS_ID_STAMINA_2;
            return OSRS_ID_STAMINA_1;
        case INV_SLOT_SATURATED_HEART:
            return OSRS_ID_SATURATED_HEART;
        default: return 0;
    }
}

/** Find first empty slot in inventory grid (scanning left→right, top→bottom).
    Returns -1 if inventory is full. */
static int gui_inv_first_empty(GuiState* gs) {
    for (int i = 0; i < INV_GRID_SLOTS; i++) {
        if (gs->inv_grid[i].type == INV_SLOT_EMPTY) return i;
    }
    return -1;
}

/** Find the slot index of an equipment item in the inventory grid.
    Returns -1 if not found. */
static int gui_inv_find_equipment(GuiState* gs, uint8_t item_db_idx) {
    for (int i = 0; i < INV_GRID_SLOTS; i++) {
        if (gs->inv_grid[i].type == INV_SLOT_EQUIPMENT &&
            gs->inv_grid[i].item_db_idx == item_db_idx) return i;
    }
    return -1;
}

/** Remove the last occurrence of a consumable type from the inventory grid.
    In OSRS, eating removes from the slot the item is in — we remove from the
    last slot of that type (bottom-right first) since that's where the cursor
    typically is when spam-eating. Returns 1 if removed, 0 if not found. */
static int gui_inv_remove_last_consumable(GuiState* gs, InvSlotType type) {
    for (int i = INV_GRID_SLOTS - 1; i >= 0; i--) {
        if (gs->inv_grid[i].type == type) {
            gs->inv_grid[i].type = INV_SLOT_EMPTY;
            gs->inv_grid[i].item_db_idx = 0;
            gs->inv_grid[i].osrs_id = 0;
            return 1;
        }
    }
    return 0;
}

/** Place an equipment item into the inventory grid at the first empty slot.
    Returns the slot index, or -1 if full. */
static int gui_inv_place_equipment(GuiState* gs, uint8_t item_db_idx) {
    int slot = gui_inv_first_empty(gs);
    if (slot < 0) return -1;
    gs->inv_grid[slot].type = INV_SLOT_EQUIPMENT;
    gs->inv_grid[slot].item_db_idx = item_db_idx;
    gs->inv_grid[slot].osrs_id = ITEM_DATABASE[item_db_idx].item_id;
    return slot;
}

/** Copy the player-side inventory snapshot that incremental GUI updates diff against. */
static void gui_snapshot_inventory_state(GuiState* gs, const Player* p) {
    memcpy(gs->inv_prev_equipped, p->equipped, NUM_GEAR_SLOTS);
    gs->inv_prev_food_count = p->food_count;
    gs->inv_prev_karambwan_count = p->karambwan_count;
    gs->inv_prev_brew_doses = p->brew_doses;
    gs->inv_prev_restore_doses = p->restore_doses;
    gs->inv_prev_prayer_pot_doses = p->prayer_pot_doses;
    gs->inv_prev_combat_doses = p->combat_potion_doses;
    gs->inv_prev_ranged_doses = p->ranged_potion_doses;
    gs->inv_prev_bastion_doses = p->bastion_doses;
    gs->inv_prev_stamina_doses = p->stamina_doses;
    gs->inv_prev_antivenom_doses = p->antivenom_doses;
    gs->inv_prev_saturated_heart_count = p->saturated_heart_count;
}

/** Return 1 when any inventory-tracked consumable count changed. */
static int gui_inventory_consumables_changed(const GuiState* gs, const Player* p) {
    return p->food_count != gs->inv_prev_food_count
        || p->karambwan_count != gs->inv_prev_karambwan_count
        || p->brew_doses != gs->inv_prev_brew_doses
        || p->restore_doses != gs->inv_prev_restore_doses
        || p->prayer_pot_doses != gs->inv_prev_prayer_pot_doses
        || p->combat_potion_doses != gs->inv_prev_combat_doses
        || p->ranged_potion_doses != gs->inv_prev_ranged_doses
        || p->bastion_doses != gs->inv_prev_bastion_doses
        || p->stamina_doses != gs->inv_prev_stamina_doses
        || p->antivenom_doses != gs->inv_prev_antivenom_doses
        || p->saturated_heart_count != gs->inv_prev_saturated_heart_count;
}

/** Clear inventory-only GUI state that must not leak across resets. */
static void gui_reset_inventory_ui_state(GuiState* gs) {
    gs->inv_grid_dirty = 1;
    gs->human_clicked_inv_slot = -1;
    gs->inv_dim_slot = -1;
    gs->inv_dim_timer = 0;
    gs->inv_drag_active = 0;
    gs->inv_drag_src_slot = -1;
    gs->inv_drag_start_x = 0;
    gs->inv_drag_start_y = 0;
    gs->inv_drag_mouse_x = 0;
    gs->inv_drag_mouse_y = 0;
    gs->inv_drag_press_time = 0.0;
    for (int i = 0; i < GUI_TAB_COUNT; i++) {
        gs->tab_press_timer[i] = 0;
    }
}

/** Full inventory grid build from player state. Called once at reset.
    Equipment items go first (unequipped gear), then consumables.
    After this, use gui_update_inventory() for incremental changes. */
static void gui_populate_inventory(GuiState* gs, Player* p) {
    memset(gs->inv_grid, 0, sizeof(gs->inv_grid));
    int n = 0;

    /* unequipped gear items from the slot inventory */
    for (int s = 0; s < NUM_GEAR_SLOTS && n < INV_GRID_SLOTS; s++) {
        for (int i = 0; i < p->num_items_in_slot[s] && n < INV_GRID_SLOTS; i++) {
            uint8_t item = p->inventory[s][i];
            if (item == ITEM_NONE) continue;
            /* skip if currently equipped */
            int is_equipped = 0;
            for (int e = 0; e < NUM_GEAR_SLOTS; e++) {
                if (p->equipped[e] == item) { is_equipped = 1; break; }
            }
            if (is_equipped) continue;
            /* skip duplicates */
            int dup = 0;
            for (int j = 0; j < n; j++) {
                if (gs->inv_grid[j].type == INV_SLOT_EQUIPMENT &&
                    gs->inv_grid[j].item_db_idx == item) { dup = 1; break; }
            }
            if (dup) continue;
            gs->inv_grid[n].type = INV_SLOT_EQUIPMENT;
            gs->inv_grid[n].item_db_idx = item;
            gs->inv_grid[n].osrs_id = ITEM_DATABASE[item].item_id;
            n++;
        }
    }

    /* consumables: food/potions are NOT stackable in OSRS.
       each shark = 1 slot. each potion vial = 1 slot (with dose-specific sprite).
       total doses are split into individual vials: e.g. 7 brew doses = 1x3-dose + 1x4-dose. */

    /* food: each unit = 1 slot */
    for (int i = 0; i < p->food_count && n < INV_GRID_SLOTS; i++) {
        gs->inv_grid[n].type = INV_SLOT_FOOD;
        gs->inv_grid[n].osrs_id = OSRS_ID_SHARK;
        n++;
    }
    for (int i = 0; i < p->karambwan_count && n < INV_GRID_SLOTS; i++) {
        gs->inv_grid[n].type = INV_SLOT_KARAMBWAN;
        gs->inv_grid[n].osrs_id = OSRS_ID_KARAMBWAN;
        n++;
    }

    /* potions: split doses into individual vials (4-dose first, remainder last) */
    #define ADD_POTION_VIALS(doses_total, slot_type) do { \
        int _rem = (doses_total); \
        while (_rem > 0 && n < INV_GRID_SLOTS) { \
            int _d = (_rem >= 4) ? 4 : _rem; \
            gs->inv_grid[n].type = (slot_type); \
            gs->inv_grid[n].osrs_id = gui_consumable_osrs_id((slot_type), _d); \
            _rem -= _d; \
            n++; \
        } \
    } while(0)

    ADD_POTION_VIALS(p->brew_doses, INV_SLOT_BREW);
    ADD_POTION_VIALS(p->restore_doses, INV_SLOT_RESTORE);
    ADD_POTION_VIALS(p->combat_potion_doses, INV_SLOT_COMBAT_POT);
    ADD_POTION_VIALS(p->ranged_potion_doses, INV_SLOT_RANGED_POT);
    ADD_POTION_VIALS(p->bastion_doses, INV_SLOT_BASTION_POT);
    ADD_POTION_VIALS(p->stamina_doses, INV_SLOT_STAMINA_POT);
    ADD_POTION_VIALS(p->antivenom_doses, INV_SLOT_ANTIVENOM);
    ADD_POTION_VIALS(p->prayer_pot_doses, INV_SLOT_PRAYER_POT);
    #undef ADD_POTION_VIALS

    for (int i = 0; i < p->saturated_heart_count && n < INV_GRID_SLOTS; i++) {
        gs->inv_grid[n].type = INV_SLOT_SATURATED_HEART;
        gs->inv_grid[n].osrs_id = OSRS_ID_SATURATED_HEART;
        n++;
    }

    /* snapshot player state for incremental change detection */
    gui_snapshot_inventory_state(gs, p);
}

/** Update potion vial doses in-place when doses change.
    E.g. drinking 1 dose from a 4-dose brew changes it to 3-dose (different sprite).
    When human_clicked_inv_slot targets a vial of this type, that specific vial loses
    the dose first (OSRS behavior: you drink from the vial you clicked). */
static void gui_inv_update_potion_doses(GuiState* gs, InvSlotType type,
                                         int total_doses) {
    /* collect existing vials of this type */
    int vial_slots[INV_GRID_SLOTS];
    int vial_count = 0;
    for (int i = 0; i < INV_GRID_SLOTS; i++) {
        if (gs->inv_grid[i].type == type) {
            vial_slots[vial_count++] = i;
        }
    }
    if (vial_count == 0) return;

    /* figure out how many doses were lost */
    int old_total = 0;
    for (int v = 0; v < vial_count; v++) {
        /* reverse-lookup current dose count from OSRS ID */
        int oid = gs->inv_grid[vial_slots[v]].osrs_id;
        int d4 = gui_consumable_osrs_id(type, 4);
        int d3 = gui_consumable_osrs_id(type, 3);
        int d2 = gui_consumable_osrs_id(type, 2);
        int d1 = gui_consumable_osrs_id(type, 1);
        if (oid == d4) old_total += 4;
        else if (oid == d3) old_total += 3;
        else if (oid == d2) old_total += 2;
        else if (oid == d1) old_total += 1;
    }
    int doses_lost = old_total - total_doses;

    /* if a human clicked a specific vial of this type, decrement that one first */
    int clicked = gs->human_clicked_inv_slot;
    if (doses_lost > 0 && clicked >= 0 && clicked < INV_GRID_SLOTS &&
        gs->inv_grid[clicked].type == type) {
        /* find current dose count of clicked vial */
        int oid = gs->inv_grid[clicked].osrs_id;
        int cur_dose = 0;
        for (int d = 4; d >= 1; d--) {
            if (oid == gui_consumable_osrs_id(type, d)) { cur_dose = d; break; }
        }
        if (cur_dose > 0) {
            int take = (doses_lost < cur_dose) ? doses_lost : cur_dose;
            cur_dose -= take;
            doses_lost -= take;
            if (cur_dose <= 0) {
                gs->inv_grid[clicked].type = INV_SLOT_EMPTY;
                gs->inv_grid[clicked].item_db_idx = 0;
                gs->inv_grid[clicked].osrs_id = 0;
            } else {
                gs->inv_grid[clicked].osrs_id = gui_consumable_osrs_id(type, cur_dose);
            }
        }
    }

    /* if doses still need removing (non-human or multiple doses lost),
       take from remaining vials in reverse order (last first) */
    for (int v = vial_count - 1; v >= 0 && doses_lost > 0; v--) {
        int slot = vial_slots[v];
        if (slot == clicked) continue; /* already handled */
        if (gs->inv_grid[slot].type != type) continue;
        int oid = gs->inv_grid[slot].osrs_id;
        int cur_dose = 0;
        for (int d = 4; d >= 1; d--) {
            if (oid == gui_consumable_osrs_id(type, d)) { cur_dose = d; break; }
        }
        if (cur_dose <= 0) continue;
        int take = (doses_lost < cur_dose) ? doses_lost : cur_dose;
        cur_dose -= take;
        doses_lost -= take;
        if (cur_dose <= 0) {
            gs->inv_grid[slot].type = INV_SLOT_EMPTY;
            gs->inv_grid[slot].item_db_idx = 0;
            gs->inv_grid[slot].osrs_id = 0;
        } else {
            gs->inv_grid[slot].osrs_id = gui_consumable_osrs_id(type, cur_dose);
        }
    }
}

/** Incremental inventory update. Detects gear switches and consumable changes
    by comparing against the previous snapshot, then modifies only affected slots.
    Items stay in their assigned positions — no compaction on eat/drink.

    OSRS gear swap rule: when you click an inventory item to equip it, the
    previously equipped item goes into that exact inventory slot (direct swap).
    Exception: equipping a 2H weapon while a shield is equipped — the shield
    goes to the first empty inventory slot since it wasn't directly clicked. */
static void gui_update_inventory(GuiState* gs, Player* p) {
    /* --- gear switches: direct slot swaps --- */
    for (int s = 0; s < NUM_GEAR_SLOTS; s++) {
        uint8_t prev = gs->inv_prev_equipped[s];
        uint8_t curr = p->equipped[s];
        if (prev == curr) continue;

        if (curr != ITEM_NONE && prev != ITEM_NONE) {
            /* swap: new item was in inventory, old item takes its exact slot */
            int src = gui_inv_find_equipment(gs, curr);
            if (src >= 0) {
                /* check if old item is still a valid swap item */
                int in_loadout = 0;
                for (int g = 0; g < NUM_GEAR_SLOTS; g++) {
                    for (int i = 0; i < p->num_items_in_slot[g]; i++) {
                        if (p->inventory[g][i] == prev) { in_loadout = 1; break; }
                    }
                    if (in_loadout) break;
                }
                if (in_loadout) {
                    /* direct swap: old item goes into the slot the new item came from */
                    gs->inv_grid[src].type = INV_SLOT_EQUIPMENT;
                    gs->inv_grid[src].item_db_idx = prev;
                    gs->inv_grid[src].osrs_id = ITEM_DATABASE[prev].item_id;
                } else {
                    /* old item not in loadout — just clear the slot */
                    gs->inv_grid[src].type = INV_SLOT_EMPTY;
                    gs->inv_grid[src].item_db_idx = 0;
                    gs->inv_grid[src].osrs_id = 0;
                }
            }
        } else if (curr != ITEM_NONE) {
            /* equipping from inventory, nothing was in this gear slot before */
            int src = gui_inv_find_equipment(gs, curr);
            if (src >= 0) {
                gs->inv_grid[src].type = INV_SLOT_EMPTY;
                gs->inv_grid[src].item_db_idx = 0;
                gs->inv_grid[src].osrs_id = 0;
            }
        } else if (prev != ITEM_NONE) {
            /* gear slot cleared (e.g. shield removed by 2H weapon equip).
               the old item goes to the first empty inventory slot. */
            int in_loadout = 0;
            for (int g = 0; g < NUM_GEAR_SLOTS; g++) {
                for (int i = 0; i < p->num_items_in_slot[g]; i++) {
                    if (p->inventory[g][i] == prev) { in_loadout = 1; break; }
                }
                if (in_loadout) break;
            }
            if (in_loadout && gui_inv_find_equipment(gs, prev) < 0) {
                gui_inv_place_equipment(gs, prev);
            }
        }
    }

    /* --- consumable changes: remove clicked slot or fall back to last --- */

    /* if a human clicked a specific consumable slot, remove that exact slot first */
    int clicked = gs->human_clicked_inv_slot;
    int clicked_used = 0;

    /* food */
    int food_diff = gs->inv_prev_food_count - p->food_count;
    for (int i = 0; i < food_diff; i++) {
        if (!clicked_used && clicked >= 0 && clicked < INV_GRID_SLOTS &&
            gs->inv_grid[clicked].type == INV_SLOT_FOOD) {
            gs->inv_grid[clicked].type = INV_SLOT_EMPTY;
            gs->inv_grid[clicked].item_db_idx = 0;
            gs->inv_grid[clicked].osrs_id = 0;
            clicked_used = 1;
        } else {
            gui_inv_remove_last_consumable(gs, INV_SLOT_FOOD);
        }
    }

    /* karambwan */
    int karam_diff = gs->inv_prev_karambwan_count - p->karambwan_count;
    for (int i = 0; i < karam_diff; i++) {
        if (!clicked_used && clicked >= 0 && clicked < INV_GRID_SLOTS &&
            gs->inv_grid[clicked].type == INV_SLOT_KARAMBWAN) {
            gs->inv_grid[clicked].type = INV_SLOT_EMPTY;
            gs->inv_grid[clicked].item_db_idx = 0;
            gs->inv_grid[clicked].osrs_id = 0;
            clicked_used = 1;
        } else {
            gui_inv_remove_last_consumable(gs, INV_SLOT_KARAMBWAN);
        }
    }

    /* potions: dose changes update existing vials in-place (sprite change),
       and remove empty vials when a full vial is consumed.
       human_clicked_inv_slot is still set here so the clicked vial loses the dose. */
    if (p->brew_doses != gs->inv_prev_brew_doses) {
        gui_inv_update_potion_doses(gs, INV_SLOT_BREW, p->brew_doses);
    }
    if (p->restore_doses != gs->inv_prev_restore_doses) {
        gui_inv_update_potion_doses(gs, INV_SLOT_RESTORE, p->restore_doses);
    }
    if (p->combat_potion_doses != gs->inv_prev_combat_doses) {
        gui_inv_update_potion_doses(gs, INV_SLOT_COMBAT_POT, p->combat_potion_doses);
    }
    if (p->ranged_potion_doses != gs->inv_prev_ranged_doses) {
        gui_inv_update_potion_doses(gs, INV_SLOT_RANGED_POT, p->ranged_potion_doses);
    }
    if (p->bastion_doses != gs->inv_prev_bastion_doses) {
        gui_inv_update_potion_doses(gs, INV_SLOT_BASTION_POT, p->bastion_doses);
    }
    if (p->stamina_doses != gs->inv_prev_stamina_doses) {
        gui_inv_update_potion_doses(gs, INV_SLOT_STAMINA_POT, p->stamina_doses);
    }
    if (p->antivenom_doses != gs->inv_prev_antivenom_doses) {
        gui_inv_update_potion_doses(gs, INV_SLOT_ANTIVENOM, p->antivenom_doses);
    }
    if (p->prayer_pot_doses != gs->inv_prev_prayer_pot_doses) {
        gui_inv_update_potion_doses(gs, INV_SLOT_PRAYER_POT, p->prayer_pot_doses);
    }
    int heart_diff = gs->inv_prev_saturated_heart_count - p->saturated_heart_count;
    for (int i = 0; i < heart_diff; i++)
        gui_inv_remove_last_consumable(gs, INV_SLOT_SATURATED_HEART);
    for (int i = 0; i < -heart_diff; i++) {
        int slot = gui_inv_first_empty(gs);
        if (slot < 0) break;
        gs->inv_grid[slot].type = INV_SLOT_SATURATED_HEART;
        gs->inv_grid[slot].osrs_id = OSRS_ID_SATURATED_HEART;
    }

    /* only clear human click when a consumable was actually used this frame.
       if no diff happened yet, keep it for the next tick when the sim processes the action. */
    if (clicked_used || gui_inventory_consumables_changed(gs, p)) {
        gs->human_clicked_inv_slot = -1;
    }

    /* update snapshot */
    gui_snapshot_inventory_state(gs, p);
}

/** Get the inventory grid screen position for a slot index. */
static void gui_inv_slot_pos(GuiState* gs, int slot, int* out_x, int* out_y) {
    int grid_x = gs->panel_x + INV_PANEL_CONTENT_X + INV_SLOT_X;
    int grid_y = gui_content_y(gs) + INV_SLOT_Y;
    int col = slot % INV_COLS;
    int row = slot / INV_COLS;
    *out_x = grid_x + col * INV_CELL_W;
    *out_y = grid_y + row * INV_CELL_H;
}

/** Hit test: return inventory slot index at screen position, or -1. */
static int gui_inv_slot_at(GuiState* gs, int mx, int my) {
    for (int i = 0; i < INV_GRID_SLOTS; i++) {
        int sx, sy;
        gui_inv_slot_pos(gs, i, &sx, &sy);
        if (mx >= sx && mx < sx + INV_CELL_W && my >= sy && my < sy + INV_CELL_H) {
            return i;
        }
    }
    return -1;
}

static const char* gui_inv_primary_action_label(const InvSlot* inv) {
    uint16_t raw_osrs_id =
        inv->osrs_id > 0 && inv->osrs_id <= UINT16_MAX ? (uint16_t)inv->osrs_id : 0;
    uint8_t item_idx = inv->type == INV_SLOT_EQUIPMENT
        ? inv->item_db_idx
        : ITEM_NONE;
    OsrsInventoryClickResolution resolution = osrs_inventory_click_interpret(
        item_idx, raw_osrs_id, OSRS_CLICK_TICK_FIRST);
    switch (resolution.click_action) {
        case OSRS_CLICK_EQUIP: {
            int gear_slot = item_idx != ITEM_NONE ? item_to_gear_slot(item_idx) : -1;
            return gear_slot == GEAR_SLOT_WEAPON || gear_slot == GEAR_SLOT_AMMO
                ? "Wield"
                : "Wear";
        }
        case OSRS_CLICK_EAT:
            return "Eat";
        case OSRS_CLICK_DRINK:
            return "Drink";
        case OSRS_CLICK_NONE:
            break;
        default:
            fprintf(stderr, "gui inventory: bad click action %d\n",
                (int)resolution.click_action);
            abort();
    }
    switch (inv->type) {
        case INV_SLOT_EQUIPMENT: {
            if (inv->item_db_idx == ITEM_NONE) return NULL;
            int gear_slot = item_to_gear_slot(inv->item_db_idx);
            return gear_slot == GEAR_SLOT_WEAPON || gear_slot == GEAR_SLOT_AMMO
                ? "Wield"
                : "Wear";
        }
        case INV_SLOT_FOOD:
        case INV_SLOT_KARAMBWAN:
            return "Eat";
        case INV_SLOT_BREW:
        case INV_SLOT_RESTORE:
        case INV_SLOT_COMBAT_POT:
        case INV_SLOT_RANGED_POT:
        case INV_SLOT_ANTIVENOM:
        case INV_SLOT_PRAYER_POT:
        case INV_SLOT_BASTION_POT:
        case INV_SLOT_STAMINA_POT:
            return "Drink";
        case INV_SLOT_SATURATED_HEART:
            return NULL;
        case INV_SLOT_EMPTY:
        default:
            return NULL;
    }
}

static const char* gui_inv_raw_osrs_id_display_name(int osrs_id) {
    switch (osrs_id) {
        case 27610:
            return "Venator bow";
        case 12006:
            return "Abyssal tentacle";
        case 27281:
            return "Divine rune pouch";
        case 23685:
            return "Divine super combat";
        case 23733:
            return "Divine ranging potion";
        case 10925:
            return "Sanfew serum";
        case 4417:
            return "Guthix rest";
        case 30875:
            return "Surge potion";
        default:
            return "";
    }
}

static const char* gui_inv_slot_display_name(const InvSlot* inv) {
    switch (inv->type) {
        case INV_SLOT_EQUIPMENT:
            if (inv->item_db_idx != ITEM_NONE) return gui_item_short_name(inv->item_db_idx);
            return gui_inv_raw_osrs_id_display_name(inv->osrs_id);
        case INV_SLOT_FOOD:
            return "Shark";
        case INV_SLOT_KARAMBWAN:
            return "Karambwan";
        case INV_SLOT_BREW:
            return "Saradomin brew";
        case INV_SLOT_RESTORE:
            return "Super restore";
        case INV_SLOT_COMBAT_POT:
            return "Super combat";
        case INV_SLOT_RANGED_POT:
            return "Ranging potion";
        case INV_SLOT_ANTIVENOM:
            return "Anti-venom+";
        case INV_SLOT_PRAYER_POT:
            return "Prayer potion";
        case INV_SLOT_BASTION_POT:
            return "Bastion potion";
        case INV_SLOT_STAMINA_POT:
            return "Stamina potion";
        case INV_SLOT_SATURATED_HEART:
            return "Saturated heart";
        case INV_SLOT_EMPTY:
        default:
            return "";
    }
}

/** Select an inventory item as the source for the next item target click. */
static int gui_inv_select_item(GuiState* gs, HumanInput* hi, int slot) {
    if (!hi || !hi->enabled) return 0;
    if (slot < 0 || slot >= INV_GRID_SLOTS) return 0;
    InvSlot* inv = &gs->inv_grid[slot];
    if (inv->type == INV_SLOT_EMPTY) return 0;

    int item_db_idx = inv->type == INV_SLOT_EQUIPMENT ? inv->item_db_idx : -1;
    int osrs_id = inv->osrs_id;
    if (osrs_id == 0 && inv->type == INV_SLOT_EQUIPMENT) {
        osrs_id = ITEM_DATABASE[inv->item_db_idx].item_id;
    }

    gs->inv_dim_slot = slot;
    gs->inv_dim_timer = INV_DIM_TICKS;
    human_input_apply_ui_intent(hi,
        osrs_ui_intent_select_item(slot, item_db_idx, osrs_id));
    return 1;
}

/** Handle inventory click: equip gear items, eat/drink consumables.
    hi is a HumanInput* (from osrs_pvp_human_input_types.h, included above).
    When non-NULL and enabled, food/potion clicks set pending_* fields instead of
    directly mutating player state, so the action system handles timers. */

static InvAction gui_inv_click(GuiState* gs, Player* p, int slot,
                                HumanInput* hi) {
    if (slot < 0 || slot >= INV_GRID_SLOTS) return INV_ACTION_NONE;
    InvSlot* inv = &gs->inv_grid[slot];
    if (inv->type == INV_SLOT_EMPTY) return INV_ACTION_NONE;

    /* start dim animation */
    gs->inv_dim_slot = slot;
    gs->inv_dim_timer = INV_DIM_TICKS;

    /* when human control is active, route food/potion through action system
       instead of directly mutating player state (respects timers) */
    int human_active = (hi && hi->enabled);
    if (human_active && hi->cursor_mode == CURSOR_ITEM_TARGET) {
        OsrsUiIntent intent = osrs_ui_intent_item_on_item(
            hi->selected_item_inventory_slot,
            slot,
            hi->selected_item_db_idx,
            hi->selected_item_osrs_id);
        human_input_apply_ui_intent(hi, intent);
        return INV_ACTION_ITEM_ON_ITEM;
    }

    switch (inv->type) {
        case INV_SLOT_EQUIPMENT: {
            if (human_active && gs->display_inventory_count > 0) {
                human_input_queue_inventory_primary_click(hi, slot);
                gs->human_clicked_inv_slot = slot;
                return INV_ACTION_EQUIP;
            }
            int gear_slot = item_to_gear_slot(inv->item_db_idx);
            if (gear_slot >= 0) {
                if (human_active) {
                    human_input_queue_equip_inventory_item(hi, slot, inv->item_db_idx, gear_slot);
                    gs->human_clicked_inv_slot = slot;
                } else {
                    slot_equip_item(p, gear_slot, inv->item_db_idx);
                }
            }
            return INV_ACTION_EQUIP;
        }
        case INV_SLOT_FOOD:
            if (human_active) {
                hi->pending_food = 1;
                human_input_queue_eat(hi, 0, slot);
                gs->human_clicked_inv_slot = slot;
            }
            else { eat_food(p, 0); }
            return INV_ACTION_EAT;
        case INV_SLOT_KARAMBWAN:
            if (human_active) {
                hi->pending_karambwan = 1;
                human_input_queue_eat(hi, 1, slot);
                gs->human_clicked_inv_slot = slot;
            }
            else { eat_food(p, 1); }
            return INV_ACTION_EAT;
        case INV_SLOT_BREW:
            if (human_active) {
                hi->pending_potion = POTION_BREW;
                human_input_queue_drink(hi, POTION_BREW, slot);
                gs->human_clicked_inv_slot = slot;
            }
            return INV_ACTION_DRINK;
        case INV_SLOT_RESTORE:
            if (human_active) {
                hi->pending_potion = POTION_RESTORE;
                human_input_queue_drink(hi, POTION_RESTORE, slot);
                gs->human_clicked_inv_slot = slot;
            }
            return INV_ACTION_DRINK;
        case INV_SLOT_COMBAT_POT:
            if (human_active) {
                hi->pending_potion = POTION_COMBAT;
                human_input_queue_drink(hi, POTION_COMBAT, slot);
                gs->human_clicked_inv_slot = slot;
            }
            return INV_ACTION_DRINK;
        case INV_SLOT_RANGED_POT:
            if (human_active) {
                hi->pending_potion = POTION_RANGED;
                human_input_queue_drink(hi, POTION_RANGED, slot);
                gs->human_clicked_inv_slot = slot;
            }
            return INV_ACTION_DRINK;
        case INV_SLOT_ANTIVENOM:
            if (human_active) {
                hi->pending_potion = POTION_ANTIVENOM;
                human_input_queue_drink(hi, POTION_ANTIVENOM, slot);
                gs->human_clicked_inv_slot = slot;
            }
            return INV_ACTION_DRINK;
        case INV_SLOT_PRAYER_POT:
            if (human_active) {
                hi->pending_potion = POTION_PRAYER_POT;
                human_input_queue_drink(hi, POTION_PRAYER_POT, slot);
                gs->human_clicked_inv_slot = slot;
            }
            return INV_ACTION_DRINK;
        case INV_SLOT_BASTION_POT:
            if (human_active) {
                hi->pending_potion = POTION_BASTION;
                human_input_queue_drink(hi, POTION_BASTION, slot);
                gs->human_clicked_inv_slot = slot;
            }
            return INV_ACTION_DRINK;
        case INV_SLOT_STAMINA_POT:
            if (human_active) {
                hi->pending_potion = POTION_STAMINA;
                human_input_queue_drink(hi, POTION_STAMINA, slot);
                gs->human_clicked_inv_slot = slot;
            }
            return INV_ACTION_DRINK;
        default:
            return INV_ACTION_NONE;
    }
}

/** Handle inventory mouse input: clicks, drag start/move/release.
    When hi is non-NULL and enabled, food/potion clicks route through the
    action system instead of directly mutating player state. */
static void gui_inv_handle_mouse(GuiState* gs, Player* p, HumanInput* hi) {
    if (gs->active_tab != GUI_TAB_INVENTORY) return;

    int mx, my;
    gui_mouse_to_panel_space(gs, GetMouseX(), GetMouseY(), &mx, &my);

    /* drag in progress */
    if (gs->inv_drag_active) {
        gs->inv_drag_mouse_x = mx;
        gs->inv_drag_mouse_y = my;

        if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
            /* drop: swap src and target slots */
            int target = gui_inv_slot_at(gs, mx, my);
            if (target >= 0 && target != gs->inv_drag_src_slot) {
                InvSlot source = gs->inv_grid[gs->inv_drag_src_slot];
                if (hi && hi->enabled) {
                    human_input_queue_item_on_item(
                        hi,
                        gs->inv_drag_src_slot,
                        target,
                        source.type == INV_SLOT_EQUIPMENT
                            ? source.item_db_idx
                            : ITEM_NONE,
                        source.osrs_id);
                }
                InvSlot tmp = gs->inv_grid[target];
                gs->inv_grid[target] = gs->inv_grid[gs->inv_drag_src_slot];
                gs->inv_grid[gs->inv_drag_src_slot] = tmp;
            }
            gs->inv_drag_active = 0;
            gs->inv_drag_src_slot = -1;
        }
        return;
    }

    /* new click */
    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        int slot = gui_inv_slot_at(gs, mx, my);
        if (slot >= 0 && gs->inv_grid[slot].type != INV_SLOT_EMPTY) {
            gs->inv_drag_start_x = mx;
            gs->inv_drag_start_y = my;
            gs->inv_drag_src_slot = slot;
            gs->inv_drag_press_time = GetTime();
        }
    }

    /* held past the anti-drag threshold AND moved past the dead zone → start
       drag; a quicker press-move-release still resolves as a click below. */
    if (IsMouseButtonDown(MOUSE_BUTTON_LEFT) && gs->inv_drag_src_slot >= 0 && !gs->inv_drag_active &&
        GetTime() - gs->inv_drag_press_time >= INV_DRAG_HOLD_SECONDS) {
        int dx = mx - gs->inv_drag_start_x;
        int dy = my - gs->inv_drag_start_y;
        if (dx > INV_DRAG_DEAD_ZONE || dx < -INV_DRAG_DEAD_ZONE ||
            dy > INV_DRAG_DEAD_ZONE || dy < -INV_DRAG_DEAD_ZONE) {
            gs->inv_drag_active = 1;
            gs->inv_drag_mouse_x = mx;
            gs->inv_drag_mouse_y = my;
            /* dim the source slot during drag */
            gs->inv_dim_slot = gs->inv_drag_src_slot;
            gs->inv_dim_timer = 9999;  /* stays dim during entire drag */
        }
    }

    /* click release without drag = activate item */
    if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT) && gs->inv_drag_src_slot >= 0 && !gs->inv_drag_active) {
        gui_inv_click(gs, p, gs->inv_drag_src_slot, hi);
        gs->inv_drag_src_slot = -1;
    }
}

static void gui_tick(GuiState* gs) {
    for (int i = 0; i < GUI_TAB_COUNT; i++) {
        if (gs->tab_press_timer[i] > 0) {
            gs->tab_press_timer[i]--;
        }
    }
    if (gs->inv_dim_timer > 0 && !gs->inv_drag_active) {
        gs->inv_dim_timer--;
        if (gs->inv_dim_timer <= 0) {
            gs->inv_dim_slot = -1;
        }
    }
}

static GuiUiItemSlot gui_ui_slot_from_inv_slot(const GuiState* gs, const InvSlot* inv, int slot) {
    GuiUiItemSlot out = {
        .present = 1,
        .enabled = inv->type != INV_SLOT_EMPTY,
        .item_db_idx = ITEM_NONE,
        .osrs_id = inv->osrs_id,
        .quantity = 1,
        .selected = 0,
        .alpha = 255,
        .gear_slot = -1,
    };
    if (inv->type == INV_SLOT_EQUIPMENT) {
        out.item_db_idx = inv->item_db_idx;
    }
    if (gs->inv_dim_slot == slot && gs->inv_dim_timer > 0) {
        out.alpha = 128;
    }
    if (gs->inv_drag_active && slot == gs->inv_drag_src_slot) {
        out.alpha = 80;
    }
    return out;
}

static void gui_draw_inventory_drag(GuiState* gs) {
    if (!(gs->inv_drag_active && gs->inv_drag_src_slot >= 0)) return;
    InvSlot* drag = &gs->inv_grid[gs->inv_drag_src_slot];
    Texture2D tex = {0};
    if (drag->type == INV_SLOT_EQUIPMENT) {
        tex = gui_get_item_sprite(gs, drag->item_db_idx);
    } else {
        tex = gui_get_sprite_by_osrs_id(gs, drag->osrs_id);
    }
    if (tex.id != 0) {
        int dx = gs->inv_drag_mouse_x - INV_SPRITE_W / 2;
        int dy = gs->inv_drag_mouse_y - INV_SPRITE_H / 2;
        Rectangle src = {0, 0, (float)tex.width, (float)tex.height};
        Rectangle dst = {(float)dx, (float)dy, (float)INV_SPRITE_W, (float)INV_SPRITE_H};
        DrawTexturePro(tex, src, dst, (Vector2){0, 0}, 0.0f,
            CLITERAL(Color){255, 255, 255, 200});
    }

    int target = gui_inv_slot_at(gs, gs->inv_drag_mouse_x, gs->inv_drag_mouse_y);
    if (target >= 0 && target != gs->inv_drag_src_slot) {
        int tx, ty;
        gui_inv_slot_pos(gs, target, &tx, &ty);
        DrawRectangle(tx, ty, INV_CELL_W, INV_CELL_H, CLITERAL(Color){255, 255, 255, 40});
    }
}

static int gui_draw_inventory_decoded(GuiState* gs) {
    GuiUiItemContainerOverride container = {
        .component_id = OSRS_UI_COMPONENT_ID(OSRS_UI_GROUP_INVENTORY, 0),
        .slot_count = INV_GRID_SLOTS,
        .columns = 4,
        .x0 = 14,
        .y0 = 8,
        .step_x = 42,
        .step_y = 36,
        .slot_w = INV_SPRITE_W,
        .slot_h = INV_SPRITE_H,
    };
    for (int slot = 0; slot < INV_GRID_SLOTS; slot++) {
        container.slots[slot] = gui_ui_slot_from_inv_slot(gs, &gs->inv_grid[slot], slot);
    }
    GuiUiOverrides overrides = {
        .item_containers = &container,
        .item_container_count = 1,
    };
    return gui_draw_ui_group(gs, "inventory", gui_side_content_rect(gs), &overrides);
}

static void gui_draw_inventory_manual(GuiState* gs) {
    for (int slot = 0; slot < INV_GRID_SLOTS; slot++) {
        int cx, cy;
        gui_inv_slot_pos(gs, slot, &cx, &cy);
        int sx = cx + (INV_CELL_W - INV_SPRITE_W) / 2;
        int sy = cy + (INV_CELL_H - INV_SPRITE_H) / 2;
        DrawRectangle(sx, sy, INV_SPRITE_W, INV_SPRITE_H, CLITERAL(Color){0, 0, 0, 30});
    }

    for (int slot = 0; slot < INV_GRID_SLOTS; slot++) {
        int cx, cy;
        gui_inv_slot_pos(gs, slot, &cx, &cy);
        InvSlot* inv = &gs->inv_grid[slot];

        if (inv->type == INV_SLOT_EMPTY) continue;

        /* determine sprite */
        Texture2D tex = { 0 };
        if (inv->type == INV_SLOT_EQUIPMENT) {
            tex = gui_get_item_sprite(gs, inv->item_db_idx);
        } else {
            tex = gui_get_sprite_by_osrs_id(gs, inv->osrs_id);
        }

        /* dim tint: 50% alpha when clicked/dragged (matches OSRS var17=128) */
        int is_dimmed = (gs->inv_dim_slot == slot && gs->inv_dim_timer > 0);
        Color tint = is_dimmed ? CLITERAL(Color){ 255, 255, 255, 128 } : WHITE;

        int dx = cx + (INV_CELL_W - INV_SPRITE_W) / 2;
        int dy = cy + (INV_CELL_H - INV_SPRITE_H) / 2;

        /* skip drawing at grid position if being dragged (drawn at cursor instead) */
        if (gs->inv_drag_active && slot == gs->inv_drag_src_slot) {
            if (tex.id != 0) {
                Rectangle src = { 0, 0, (float)tex.width, (float)tex.height };
                Rectangle dst = { (float)dx, (float)dy, (float)INV_SPRITE_W, (float)INV_SPRITE_H };
                DrawTexturePro(tex, src, dst, (Vector2){0,0}, 0.0f,
                               CLITERAL(Color){ 255, 255, 255, 80 });
            }
            continue;
        }

        if (tex.id != 0) {
            Rectangle src = { 0, 0, (float)tex.width, (float)tex.height };
            Rectangle dst = { (float)dx, (float)dy, (float)INV_SPRITE_W, (float)INV_SPRITE_H };
            DrawTexturePro(tex, src, dst, (Vector2){0,0}, 0.0f, tint);
        } else {
            const char* name = (inv->type == INV_SLOT_EQUIPMENT)
                ? gui_item_short_name(inv->item_db_idx) : "???";
            gui_text_shadow(gs, name, cx + 2, cy + 12, 7, GUI_TEXT_YELLOW);
        }
    }

    gui_draw_inventory_drag(gs);
}

/** Render-only: load the panel from a fixed per-kit OSRS-id list (the colosseum
    wiki inventory) instead of deriving it from sets + dose counts. Every entry is
    drawn as an equipment-style display slot keyed by osrs_id; no sim state reads
    this. */
static void gui_load_display_inventory(GuiState* gs) {
    memset(gs->inv_grid, 0, sizeof(gs->inv_grid));
    int count = gs->display_inventory_count;
    if (count > INV_GRID_SLOTS) count = INV_GRID_SLOTS;
    for (int i = 0; i < count; i++) {
        int osrs_id = gs->display_inventory_osrs_ids[i];
        if (osrs_id <= 0) continue;
        uint8_t item_idx = osrs_id <= UINT16_MAX
            ? osrs_item_index_for_raw_osrs_id((uint16_t)osrs_id)
            : ITEM_NONE;
        gs->inv_grid[i].type = INV_SLOT_EQUIPMENT;
        gs->inv_grid[i].item_db_idx = item_idx;
        gs->inv_grid[i].osrs_id = osrs_id;
    }
}

static void gui_draw_inventory(GuiState* gs, Player* p) {
    if (gs->display_inventory_count > 0) {
        gui_load_display_inventory(gs);
        gs->inv_grid_dirty = 0;
        if (gui_draw_inventory_decoded(gs)) {
            gui_draw_inventory_drag(gs);
            return;
        }
        gui_draw_inventory_manual(gs);
        return;
    }
    if (gs->inv_grid_dirty) {
        gui_populate_inventory(gs, p);
        gs->inv_grid_dirty = 0;
    } else {
        gui_update_inventory(gs, p);
    }

    if (gui_draw_inventory_decoded(gs)) {
        gui_draw_inventory_drag(gs);
        return;
    }

    gui_draw_inventory_manual(gs);
}


typedef struct {
    int gear_slot;
    const char* worn_asset;
    const char* component_name;
    uint32_t component_file_id;
    Rectangle rect;
} GuiWornSlotRef;

static const GuiWornSlotRef GUI_WORN_SLOT_REFS[] = {
    {GEAR_SLOT_HEAD,   "wornicons_0",  "slot0",  15, {77,  4,   36, 36}},
    {GEAR_SLOT_CAPE,   "wornicons_1",  "slot1",  16, {36,  43,  36, 36}},
    {GEAR_SLOT_NECK,   "wornicons_2",  "slot2",  17, {77,  43,  36, 36}},
    {GEAR_SLOT_AMMO,   "wornicons_6",  "slot13", 25, {133, 43,  36, 36}},
    {GEAR_SLOT_WEAPON, "wornicons_3",  "slot3",  18, {21,  82,  36, 36}},
    {GEAR_SLOT_BODY,   "wornicons_4",  "slot4",  19, {77,  82,  36, 36}},
    {GEAR_SLOT_SHIELD, "wornicons_5",  "slot5",  20, {133, 82,  36, 36}},
    {GEAR_SLOT_LEGS,   "wornicons_7",  "slot7",  21, {77,  122, 36, 36}},
    {GEAR_SLOT_HANDS,  "wornicons_9",  "slot9",  22, {21,  162, 36, 36}},
    {GEAR_SLOT_FEET,   "wornicons_10", "slot10", 23, {77,  162, 36, 36}},
    {GEAR_SLOT_RING,   "wornicons_11", "slot12", 24, {133, 162, 36, 36}},
};

typedef struct {
    const char* component_name;
    const char* icon_component_name;
    const char* asset;
    Rectangle rect;
    Rectangle icon_rect;
} GuiWornButtonRef;

static const GuiWornButtonRef GUI_WORN_BUTTON_REFS[] = {
    {"equipment",     "equipment_icon", "options_icons_16", {7,   208, 40, 40}, {10,  210, 32, 32}},
    {"pricechecker",  "com_4",          "options_icons_28", {52,  208, 40, 40}, {56,  212, 32, 32}},
    {"deathkeep",     "com_6",          "options_icons_18", {97,  208, 40, 40}, {99,  211, 34, 34}},
    {"call_follower", "com_8",          "whistle",          {142, 208, 40, 40}, {145, 211, 32, 32}},
};

static int gui_draw_equipment_decoded(GuiState* gs, Player* p) {
    GuiUiComponentOverride overrides[GUI_UI_MAX_COMPONENT_OVERRIDES];
    memset(overrides, 0, sizeof(overrides));
    int override_count = 0;

    int slot_count = (int)(sizeof(GUI_WORN_SLOT_REFS) / sizeof(GUI_WORN_SLOT_REFS[0]));
    for (int i = 0; i < slot_count; i++) {
        const GuiWornSlotRef* ref = &GUI_WORN_SLOT_REFS[i];
        int item_idx = p->equipped[ref->gear_slot];
        GuiUiComponentOverride* override = gui_ui_push_component_override(
            overrides,
            &override_count,
            OSRS_UI_COMPONENT_ID(OSRS_UI_GROUP_WORNITEMS, ref->component_file_id));
        override->item = (GuiUiItemSlot){
            .present = 1,
            .enabled = item_idx != ITEM_NONE,
            .item_db_idx = item_idx,
            .quantity = 1,
            .alpha = 255,
            .gear_slot = ref->gear_slot,
            .empty_asset = ref->worn_asset,
        };
    }

    GuiUiOverrides ui_overrides = {
        .components = overrides,
        .component_count = override_count,
    };

    int button_count = (int)(sizeof(GUI_WORN_BUTTON_REFS) / sizeof(GUI_WORN_BUTTON_REFS[0]));
    for (int i = 0; i < button_count; i++) {
        const GuiWornButtonRef* ref = &GUI_WORN_BUTTON_REFS[i];
        Rectangle rect = gui_side_component_rect(gs, "wornitems", ref->component_name, ref->rect);
        gui_draw_named_asset(gs, "combatboxes_0", rect, WHITE);
        if (gui_asset(gs, "combatboxes_0").id == 0) {
            gui_draw_slot((int)rect.x, (int)rect.y, (int)rect.width, (int)rect.height, GUI_BG_SLOT);
        }
    }

    return gui_draw_ui_group(gs, "wornitems", gui_side_content_rect(gs), &ui_overrides);
}

static void gui_draw_equipment(GuiState* gs, Player* p) {
    if (gui_draw_equipment_decoded(gs, p)) return;

    gui_draw_named_asset_tiled(gs, "miscgraphics_2",
        gui_side_ref_rect(gs, (Rectangle){77, 39, 36, 124}), WHITE);
    gui_draw_named_asset_tiled(gs, "miscgraphics_2",
        gui_side_ref_rect(gs, (Rectangle){21, 118, 36, 45}), WHITE);
    gui_draw_named_asset_tiled(gs, "miscgraphics_2",
        gui_side_ref_rect(gs, (Rectangle){133, 118, 36, 45}), WHITE);
    gui_draw_named_asset_tiled(gs, "miscgraphics_3",
        gui_side_ref_rect(gs, (Rectangle){56, 81, 78, 36}), WHITE);
    gui_draw_named_asset_tiled(gs, "miscgraphics_3",
        gui_side_ref_rect(gs, (Rectangle){71, 42, 48, 36}), WHITE);

    int slot_count = (int)(sizeof(GUI_WORN_SLOT_REFS) / sizeof(GUI_WORN_SLOT_REFS[0]));
    for (int i = 0; i < slot_count; i++) {
        const GuiWornSlotRef* ref = &GUI_WORN_SLOT_REFS[i];
        Rectangle dst = gui_side_component_rect(gs, "wornitems", ref->component_name, ref->rect);
        int item_idx = p->equipped[ref->gear_slot];
        gui_draw_equip_slot(
            gs,
            (int)dst.x,
            (int)dst.y,
            (int)dst.width,
            (int)dst.height,
            ref->gear_slot,
            item_idx);
        if (item_idx == ITEM_NONE) {
            gui_draw_named_asset_centered(gs, ref->worn_asset, dst, dst.width, dst.height, WHITE);
        }
    }

    int button_count = (int)(sizeof(GUI_WORN_BUTTON_REFS) / sizeof(GUI_WORN_BUTTON_REFS[0]));
    for (int i = 0; i < button_count; i++) {
        const GuiWornButtonRef* ref = &GUI_WORN_BUTTON_REFS[i];
        Rectangle rect = gui_side_component_rect(gs, "wornitems", ref->component_name, ref->rect);
        Rectangle icon = gui_side_component_rect(
            gs, "wornitems", ref->icon_component_name, ref->icon_rect);
        gui_draw_named_asset(gs, "combatboxes_0", rect, WHITE);
        if (gui_asset(gs, "combatboxes_0").id == 0) {
            gui_draw_slot((int)rect.x, (int)rect.y, (int)rect.width, (int)rect.height, GUI_BG_SLOT);
        }
        gui_draw_named_asset_centered(gs, ref->asset, icon, icon.width, icon.height, WHITE);
    }
}


#define GUI_PRAYER_GRID_COUNT GUI_NUM_PRAYERS

#define GUI_PRAYER_GRID_COLS 5
#define GUI_PRAYER_CELL_PX 34
#define GUI_SPELL_GRID_COLS 4
#define GUI_SPELL_CELL_PX 34

static int gui_fit_cell_size(int panel_w, int cols, int gap, int native_px) {
    int fitted = (panel_w - 16 - gap * (cols - 1)) / cols;
    return fitted < native_px ? fitted : native_px;
}

static void gui_prayer_grid_metrics(GuiState* gs, int* gx, int* gy, int* cell, int* gap) {
    *gap = 2;
    *cell = GUI_PRAYER_CELL_PX;
    *gx = gs->panel_x + GUI_SIDE_CONTENT_X + 8;
    *gy = gui_content_y(gs) + 8;
}

static void gui_spell_grid_metrics(GuiState* gs, int* gx, int* gy, int* cell, int* gap) {
    *gap = 5;
    *cell = gui_fit_cell_size(gs->panel_w, GUI_SPELL_GRID_COLS, *gap, GUI_SPELL_CELL_PX);
    int grid_w = GUI_SPELL_GRID_COLS * *cell + (GUI_SPELL_GRID_COLS - 1) * *gap;
    Rectangle content = gui_side_content_rect(gs);
    *gx = (int)(content.x + (content.width - grid_w) / 2);
    *gy = gui_content_y(gs) + 8;
}

typedef struct {
    const char* names[4];
    FightStyle values[4];
    int count;
} GuiCombatStyleOptions;

static GuiCombatStyleOptions gui_combat_style_options(uint8_t weapon) {
    GuiCombatStyleOptions out = {
        .names = { "Accurate", "Aggressive", "Controlled", "Defensive" },
        .values = {
            FIGHT_STYLE_ACCURATE,
            FIGHT_STYLE_AGGRESSIVE,
            FIGHT_STYLE_CONTROLLED,
            FIGHT_STYLE_DEFENSIVE,
        },
        .count = 4,
    };

    switch (weapon) {
        case ITEM_TOXIC_BLOWPIPE:
        case ITEM_ZARYTE_CROSSBOW:
        case ITEM_BOW_OF_FAERDHINEN:
        case ITEM_TWISTED_BOW:
            out.names[0] = "Accurate";
            out.names[1] = "Rapid";
            out.names[2] = "Longrange";
            out.values[0] = FIGHT_STYLE_ACCURATE;
            out.values[1] = FIGHT_STYLE_RAPID;
            out.values[2] = FIGHT_STYLE_LONGRANGE;
            out.count = 3;
            break;
        case ITEM_SCYTHE_OF_VITUR:
            out.names[0] = "Chop";
            out.names[1] = "Jab";
            out.names[2] = "Block";
            out.count = 3;
            break;
        case ITEM_SGS:
            out.names[0] = "Chop";
            out.names[1] = "Slash";
            out.names[2] = "Smash";
            out.names[3] = "Block";
            break;
        case ITEM_DRAGON_CLAWS:
            out.names[0] = "Chop";
            out.names[1] = "Slash";
            out.names[2] = "Lunge";
            out.names[3] = "Block";
            break;
        case ITEM_KODAI_WAND:
        case ITEM_DRAGON_HUNTER_WAND:
            out.names[0] = "Autocast";
            out.names[1] = "Defensive";
            out.values[0] = FIGHT_STYLE_AUTOCAST;
            out.values[1] = FIGHT_STYLE_DEFENSIVE_AUTOCAST;
            out.count = 2;
            break;
        default:
            break;
    }

    return out;
}

static Rectangle gui_combat_style_fallback_rect(int index) {
    static const Rectangle rects[4] = {
        {20,  46, 68, 47},
        {102, 46, 68, 47},
        {20,  99, 68, 47},
        {102, 99, 68, 47},
    };
    assert(index >= 0 && index < 4);
    return rects[index];
}

static Rectangle gui_combat_style_rect(GuiState* gs, int index) {
    assert(index >= 0 && index < 4);
    char component_name[2] = {(char)('0' + index), '\0'};
    return gui_side_component_rect(
        gs,
        "combat_interface",
        component_name,
        gui_combat_style_fallback_rect(index));
}

static Rectangle gui_combat_style_icon_rect(int index) {
    static const Rectangle rects[4] = {
        {37,  51, 34, 24},
        {119, 51, 34, 24},
        {37, 104, 34, 24},
        {119,104, 34, 24},
    };
    assert(index >= 0 && index < 4);
    return rects[index];
}

static Rectangle gui_combat_style_text_rect(int index) {
    static const Rectangle rects[4] = {
        {20,  67, 68, 13},
        {102, 76, 68, 13},
        {20, 129, 68, 13},
        {102,129, 68, 13},
    };
    assert(index >= 0 && index < 4);
    return rects[index];
}

static Rectangle gui_combat_autocast_rect(void) {
    return (Rectangle){20, 153, 150, 26};
}

static Rectangle gui_combat_autocast_spell_rect(int index) {
    static const Rectangle rects[2] = {
        {20, 182, 70, 26},
        {100, 182, 70, 26},
    };
    assert(index >= 0 && index < 2);
    return rects[index];
}

static Rectangle gui_combat_special_rect(void) {
    return (Rectangle){20, 200, 150, 26};
}

static Rectangle gui_combat_category_rect(void) {
    return (Rectangle){0, 233, 190, 28};
}

static void gui_draw_combat_box(GuiState* gs, Rectangle rect, int selected) {
    gui_draw_named_asset(gs, selected ? "combatboxes_1" : "combatboxes_0", rect, WHITE);
    if (gui_asset(gs, selected ? "combatboxes_1" : "combatboxes_0").id == 0) {
        DrawRectangleRec(rect, selected ? (Color){83, 61, 43, 245} : (Color){45, 39, 31, 235});
        DrawRectangleLinesEx(rect, 1, selected ? GUI_TEXT_YELLOW : (Color){103, 89, 63, 255});
    }
    if (selected) {
        DrawRectangleRec(rect, (Color){120, 27, 20, 54});
    }
}

static const char* gui_combat_icon_asset(uint8_t weapon, FightStyle style, int index) {
    switch (weapon) {
        case ITEM_TOXIC_BLOWPIPE:
        case ITEM_ZARYTE_CROSSBOW:
        case ITEM_BOW_OF_FAERDHINEN:
        case ITEM_TWISTED_BOW:
            switch (style) {
                case FIGHT_STYLE_ACCURATE:  return "combaticons2_15";
                case FIGHT_STYLE_RAPID:     return "combaticons2_16";
                case FIGHT_STYLE_LONGRANGE: return "combaticons2_17";
                default: break;
            }
            break;
        case ITEM_KODAI_WAND:
        case ITEM_DRAGON_HUNTER_WAND:
            return index == 0 ? "sideicons_interface_14" : "sideicons_interface_15";
        default:
            break;
    }

    switch (style) {
        case FIGHT_STYLE_ACCURATE:   return "combaticons_6";
        case FIGHT_STYLE_AGGRESSIVE: return "combaticons_5";
        case FIGHT_STYLE_CONTROLLED: return "combaticons_7";
        case FIGHT_STYLE_DEFENSIVE:  return "combaticons_4";
        default: return "sideicons_interface_0";
    }
}

static int gui_autocast_spell(const Player* p) {
    return p->autocast_spell == ENCOUNTER_SPELL_ICE
        ? ENCOUNTER_SPELL_ICE
        : ENCOUNTER_SPELL_BLOOD;
}

static const char* gui_autocast_spell_name(int spell) {
    return spell == ENCOUNTER_SPELL_ICE ? "Ice Barrage" : "Blood Barrage";
}

static uint32_t gui_combat_component_id(uint32_t file_id) {
    return OSRS_UI_COMPONENT_ID(OSRS_UI_GROUP_COMBAT_INTERFACE, file_id);
}

static const char* gui_combat_selected_style_name(
    const GuiCombatStyleOptions* styles,
    FightStyle fight_style
) {
    for (int i = 0; i < styles->count; i++) {
        if (fight_style == styles->values[i]) return styles->names[i];
    }
    return "Accurate";
}

static int gui_draw_combat_decoded(
    GuiState* gs,
    Player* p,
    const char* wpn_name,
    const GuiCombatStyleOptions* styles
) {
    GuiUiComponentOverride overrides[GUI_UI_MAX_COMPONENT_OVERRIDES];
    int override_count = 0;

    char combat_level[32];
    snprintf(combat_level, sizeof(combat_level), "Combat Lvl: %d",
        p->base_attack + p->base_strength + p->base_defence);

    char category_text[64];
    snprintf(category_text, sizeof(category_text), "Attack style: %s",
        gui_combat_selected_style_name(styles, p->fight_style));

    GuiUiComponentOverride* title = gui_ui_push_component_override(
        overrides, &override_count, gui_combat_component_id(3));
    title->text = wpn_name;

    GuiUiComponentOverride* level = gui_ui_push_component_override(
        overrides, &override_count, gui_combat_component_id(4));
    level->text = combat_level;

    GuiUiComponentOverride* category = gui_ui_push_component_override(
        overrides, &override_count, gui_combat_component_id(5));
    category->text = category_text;

    static const uint32_t hidden_file_ids[] = {22, 32, 37, 38, 45, 46, 47, 48, 49};
    int hidden_count = (int)(sizeof(hidden_file_ids) / sizeof(hidden_file_ids[0]));
    for (int i = 0; i < hidden_count; i++) {
        GuiUiComponentOverride* hidden = gui_ui_push_component_override(
            overrides, &override_count, gui_combat_component_id(hidden_file_ids[i]));
        hidden->hidden = 1;
    }

    static const uint32_t root_file_ids[4] = {6, 10, 14, 18};
    static const uint32_t icon_file_ids[4] = {8, 12, 16, 20};
    static const uint32_t text_file_ids[4] = {9, 13, 17, 21};
    for (int i = 0; i < styles->count; i++) {
        int active = p->fight_style == styles->values[i];

        GuiUiComponentOverride* root = gui_ui_push_component_override(
            overrides, &override_count, gui_combat_component_id(root_file_ids[i]));
        root->force_visible = 1;
        root->sprite_asset = active ? "combatboxes_1" : "combatboxes_0";

        GuiUiComponentOverride* icon = gui_ui_push_component_override(
            overrides, &override_count, gui_combat_component_id(icon_file_ids[i]));
        icon->sprite_asset = gui_combat_icon_asset(p->equipped[GEAR_SLOT_WEAPON],
            styles->values[i], i);

        GuiUiComponentOverride* text = gui_ui_push_component_override(
            overrides, &override_count, gui_combat_component_id(text_file_ids[i]));
        text->text = styles->names[i];
    }

    GuiUiOverrides ui_overrides = {
        .components = overrides,
        .component_count = override_count,
    };
    return gui_draw_ui_group(gs, "combat_interface", gui_side_content_rect(gs), &ui_overrides);
}

/** Check if a prayer grid slot is currently active based on player state. */
static int gui_prayer_is_active(GuiPrayerIdx pidx, Player* p) {
    switch (pidx) {
        case GUI_PRAY_PROTECT_MAGIC:    return p->prayer == PRAYER_PROTECT_MAGIC;
        case GUI_PRAY_PROTECT_MISSILES: return p->prayer == PRAYER_PROTECT_RANGED;
        case GUI_PRAY_PROTECT_MELEE:    return p->prayer == PRAYER_PROTECT_MELEE;
        case GUI_PRAY_REDEMPTION:       return p->prayer == PRAYER_REDEMPTION;
        case GUI_PRAY_SMITE:            return p->prayer == PRAYER_SMITE;
        case GUI_PRAY_PIETY:            return p->offensive_prayer == OFFENSIVE_PRAYER_PIETY;
        case GUI_PRAY_RIGOUR:           return p->offensive_prayer == OFFENSIVE_PRAYER_RIGOUR;
        case GUI_PRAY_AUGURY:           return p->offensive_prayer == OFFENSIVE_PRAYER_AUGURY;
        default: return 0;
    }
}

static void gui_draw_prayer(GuiState* gs, Player* p) {
    int cols = GUI_PRAYER_GRID_COLS;
    int gap, icon_sz, gx, gy;
    gui_prayer_grid_metrics(gs, &gx, &gy, &icon_sz, &gap);

    for (int i = 0; i < GUI_PRAYER_GRID_COUNT; i++) {
        int col = i % cols;
        int row = i / cols;
        int ix = gx + col * (icon_sz + gap);
        int iy = gy + row * (icon_sz + gap);

        GuiPrayerIdx pidx = (GuiPrayerIdx)i;
        int active = gui_prayer_is_active(pidx, p);
        Rectangle cell_rect = {(float)ix, (float)iy, (float)icon_sz, (float)icon_sz};

        DrawRectangleRec(cell_rect, (Color){16, 13, 10, 95});
        if (active) {
            DrawRectangleRec(cell_rect, (Color){255, 224, 64, 34});
        }

        if (gs->sprites_loaded) {
            Texture2D tex = active ? gs->prayer_on[pidx] : gs->prayer_off[pidx];
            gui_draw_texture_centered(tex, cell_rect, 30, 30, WHITE);
        }
    }
}


static void gui_draw_combat(GuiState* gs, Player* p) {
    const char* wpn_name = "Unarmed";
    if (p->equipped[GEAR_SLOT_WEAPON] != ITEM_NONE &&
        p->equipped[GEAR_SLOT_WEAPON] < NUM_ITEMS) {
        wpn_name = gui_item_short_name(p->equipped[GEAR_SLOT_WEAPON]);
    }

    GuiCombatStyleOptions styles = gui_combat_style_options(p->equipped[GEAR_SLOT_WEAPON]);
    int decoded = gui_draw_combat_decoded(gs, p, wpn_name, &styles);

    if (!decoded) {
        Rectangle title = gui_side_ref_rect(gs, (Rectangle){10, 6, 170, 14});
        Rectangle level = gui_side_ref_rect(gs, (Rectangle){10, 26, 170, 12});
        int tw = gui_measure_text(gs, wpn_name, 11);
        gui_text_shadow(gs, wpn_name, (int)(title.x + title.width / 2 - tw / 2),
            (int)title.y, 11, GUI_TEXT_ORANGE);
        const char* combat_level = TextFormat("Combat Lvl: %d",
            p->base_attack + p->base_strength + p->base_defence);
        int cw = gui_measure_text(gs, combat_level, 10);
        gui_text_shadow(gs, combat_level, (int)(level.x + level.width / 2 - cw / 2),
            (int)level.y, 10, GUI_TEXT_YELLOW);

        for (int i = 0; i < styles.count; i++) {
            Rectangle rect = gui_combat_style_rect(gs, i);
            Rectangle icon = gui_side_ref_rect(gs, gui_combat_style_icon_rect(i));
            Rectangle text = gui_side_ref_rect(gs, gui_combat_style_text_rect(i));
            int active = p->fight_style == styles.values[i];
            gui_draw_combat_box(gs, rect, active);
            gui_draw_named_asset_centered(
                gs,
                gui_combat_icon_asset(p->equipped[GEAR_SLOT_WEAPON], styles.values[i], i),
                icon,
                icon.width,
                icon.height,
                WHITE);
            Color txt_c = active ? GUI_TEXT_YELLOW : GUI_TEXT_WHITE;
            int txt_w = gui_measure_text(gs, styles.names[i], 10);
            gui_text_shadow(
                gs,
                styles.names[i],
                (int)(text.x + text.width / 2 - txt_w / 2),
                (int)(text.y + 1),
                10,
                txt_c);
        }
    }

    if (item_supports_ancient_autocast(p->equipped[GEAR_SLOT_WEAPON])) {
        Rectangle ac = gui_side_ref_rect(gs, gui_combat_autocast_rect());
        int spell = gui_autocast_spell(p);
        const char* spell_name = gui_autocast_spell_name(spell);
        gui_draw_named_asset(gs, "combatboxes_1", ac, WHITE);
        DrawRectangleLines((int)ac.x, (int)ac.y, (int)ac.width, (int)ac.height,
            p->autocast_enabled ? GUI_TEXT_YELLOW : GUI_BORDER);
        gui_text_shadow(
            gs,
            TextFormat("Autocast: %s", spell_name),
            (int)ac.x + 8,
            (int)ac.y + 7,
            10,
            p->autocast_enabled ? GUI_TEXT_YELLOW : GUI_TEXT_WHITE);

        if (gs->autocast_selector_open) {
            const char* names[2] = { "Blood", "Ice" };
            int spells[2] = { ENCOUNTER_SPELL_BLOOD, ENCOUNTER_SPELL_ICE };
            for (int i = 0; i < 2; i++) {
                Rectangle rect = gui_side_ref_rect(gs, gui_combat_autocast_spell_rect(i));
                Color c = spells[i] == spell ? GUI_TEXT_YELLOW : GUI_TEXT_WHITE;
                gui_draw_named_asset(gs, "combatboxes_1", rect, WHITE);
                DrawRectangleLines((int)rect.x, (int)rect.y, (int)rect.width, (int)rect.height,
                    spells[i] == spell ? GUI_TEXT_YELLOW : GUI_BORDER);
                int tw = gui_measure_text(gs, names[i], 10);
                gui_text_shadow(
                    gs,
                    names[i],
                    (int)(rect.x + rect.width / 2 - tw / 2),
                    (int)rect.y + 7,
                    10,
                    c);
            }
        }
    }

    Rectangle spec = gui_side_ref_rect(gs, gui_combat_special_rect());
    float spec_pct = (float)p->special_energy / 100.0f;
    gui_draw_named_asset(gs, "combatboxes_special_attack", spec, WHITE);
    if (gui_asset(gs, "combatboxes_special_attack").id == 0) {
        DrawRectangleRec(spec, (Color){32, 28, 22, 235});
    }
    Rectangle empty = {spec.x + 2, spec.y + 7, spec.width - 4, 12};
    DrawRectangleRec(empty, (Color){115, 6, 6, 255});
    Rectangle fill = empty;
    fill.width *= spec_pct;
    DrawRectangleRec(fill, p->spec_armed ? GUI_SPEC_GREEN : (Color){57, 125, 59, 255});
    if (p->spec_armed) {
        DrawRectangleRec(spec, CLITERAL(Color){ 200, 200, 50, 60 });
    }
    DrawRectangleLinesEx((Rectangle){spec.x + 2, spec.y + 6, spec.width - 4, 14}, 1,
        (Color){44, 42, 35, 255});
    const char* spec_text = TextFormat("Special Attack: %d%%", p->special_energy);
    int spec_w = gui_measure_text(gs, spec_text, 10);
    gui_text_shadow(gs, spec_text, (int)(spec.x + spec.width / 2 - spec_w / 2),
        (int)(spec.y + 8), 10, GUI_TEXT_YELLOW);

    if (!decoded) {
        Rectangle category = gui_side_ref_rect(gs, gui_combat_category_rect());
        const char* category_text = TextFormat("Attack style: %s",
            gui_combat_selected_style_name(&styles, p->fight_style));
        int category_w = gui_measure_text(gs, category_text, 12);
        gui_text_shadow(gs, category_text, (int)(category.x + category.width / 2 - category_w / 2),
            (int)(category.y + 7), 12, GUI_TEXT_ORANGE);
    }
}


typedef struct {
    const char* name;
    GuiSpellIdx idx;
} GuiSpellEntry;

static const GuiSpellEntry GUI_SPELL_GRID[] = {
    { "Smoke Rush",    GUI_SPELL_SMOKE_RUSH },
    { "Shadow Rush",   GUI_SPELL_SHADOW_RUSH },
    { "Blood Rush",    GUI_SPELL_BLOOD_RUSH },
    { "Ice Rush",      GUI_SPELL_ICE_RUSH },
    { "Smoke Burst",   GUI_SPELL_SMOKE_BURST },
    { "Shadow Burst",  GUI_SPELL_SHADOW_BURST },
    { "Blood Burst",   GUI_SPELL_BLOOD_BURST },
    { "Ice Burst",     GUI_SPELL_ICE_BURST },
    { "Smoke Blitz",   GUI_SPELL_SMOKE_BLITZ },
    { "Shadow Blitz",  GUI_SPELL_SHADOW_BLITZ },
    { "Blood Blitz",   GUI_SPELL_BLOOD_BLITZ },
    { "Ice Blitz",     GUI_SPELL_ICE_BLITZ },
    { "Smoke Barrage", GUI_SPELL_SMOKE_BARRAGE },
    { "Shadow Barrage",GUI_SPELL_SHADOW_BARRAGE },
    { "Blood Barrage", GUI_SPELL_BLOOD_BARRAGE },
    { "Ice Barrage",   GUI_SPELL_ICE_BARRAGE },
    { "Paddewwa Teleport",     GUI_SPELL_PADDEWWA_TELEPORT },
    { "Senntisten Teleport",   GUI_SPELL_SENNTISTEN_TELEPORT },
    { "Kharyrll Teleport",     GUI_SPELL_KHARYRLL_TELEPORT },
    { "Lassar Teleport",       GUI_SPELL_LASSAR_TELEPORT },
    { "Dareeyak Teleport",     GUI_SPELL_DAREEYAK_TELEPORT },
    { "Carrallanger Teleport", GUI_SPELL_CARRALLANGER_TELEPORT },
    { "Annakarl Teleport",     GUI_SPELL_ANNAKARL_TELEPORT },
    { "Ghorrock Teleport",     GUI_SPELL_GHORROCK_TELEPORT },
};
#define GUI_SPELL_GRID_COUNT ((int)(sizeof(GUI_SPELL_GRID) / sizeof(GUI_SPELL_GRID[0])))

static inline int gui_spell_is_ice(GuiSpellIdx s) {
    return s == GUI_SPELL_ICE_RUSH
        || s == GUI_SPELL_ICE_BURST
        || s == GUI_SPELL_ICE_BLITZ
        || s == GUI_SPELL_ICE_BARRAGE;
}

static inline int gui_spell_is_blood(GuiSpellIdx s) {
    return s == GUI_SPELL_BLOOD_RUSH
        || s == GUI_SPELL_BLOOD_BURST
        || s == GUI_SPELL_BLOOD_BLITZ
        || s == GUI_SPELL_BLOOD_BARRAGE;
}

static inline int gui_spell_castable(GuiSpellIdx s) {
    return gui_spell_is_ice(s) || gui_spell_is_blood(s);
}

static void gui_draw_spellbook(GuiState* gs, Player* p) {
    (void)p;
    int cols = GUI_SPELL_GRID_COLS;
    int gap, icon_sz, gx, gy;
    gui_spell_grid_metrics(gs, &gx, &gy, &icon_sz, &gap);

    for (int i = 0; i < GUI_SPELL_GRID_COUNT; i++) {
        int col = i % cols;
        int row = i / cols;
        int ix = gx + col * (icon_sz + gap);
        int iy = gy + row * (icon_sz + gap);

        GuiSpellIdx sidx_here = GUI_SPELL_GRID[i].idx;
        int targeting = (gs->pending_spell_highlight >= 0 &&
                         (int)sidx_here == gs->pending_spell_highlight);
        Rectangle cell_rect = {(float)ix, (float)iy, (float)icon_sz, (float)icon_sz};

        DrawRectangleRec(cell_rect, (Color){12, 12, 28, 105});

        GuiSpellIdx sidx = GUI_SPELL_GRID[i].idx;
        if (gs->sprites_loaded) {
            int castable = gui_spell_castable(sidx);
            Texture2D tex = castable ? gs->spell_on[sidx] : gs->spell_off[sidx];
            if (tex.id != 0) {
                gui_draw_texture_centered(
                    tex,
                    cell_rect,
                    24,
                    24,
                    castable ? WHITE : (Color){170, 170, 170, 220});
            }
        }
        if (targeting) {
            DrawRectangleLinesEx(cell_rect, 2.0f, YELLOW);
        }
    }
}
typedef enum {
    GUI_SKILL_PLAYER_ATTACK = 0,
    GUI_SKILL_PLAYER_STRENGTH,
    GUI_SKILL_PLAYER_DEFENCE,
    GUI_SKILL_PLAYER_RANGED,
    GUI_SKILL_PLAYER_PRAYER,
    GUI_SKILL_PLAYER_MAGIC,
    GUI_SKILL_PLAYER_HITPOINTS,
    GUI_SKILL_PLAYER_OTHER,
} GuiSkillValueKind;

typedef struct {
    const char* component_name;
    const char* icon_asset;
    GuiSkillValueKind value_kind;
    Rectangle fallback;
} GuiSkillPanelSlot;

static const GuiSkillPanelSlot GUI_SKILL_PANEL_SLOTS[] = {
    {"attack",       "skill_icon_0",  GUI_SKILL_PLAYER_ATTACK,    {1,   1,   62, 30}},
    {"strength",     "skill_icon_1",  GUI_SKILL_PLAYER_STRENGTH,  {1,   31,  62, 30}},
    {"defence",      "skill_icon_2",  GUI_SKILL_PLAYER_DEFENCE,   {1,   61,  62, 30}},
    {"ranged",       "skill_icon_3",  GUI_SKILL_PLAYER_RANGED,    {1,   91,  62, 30}},
    {"prayer",       "skill_icon_4",  GUI_SKILL_PLAYER_PRAYER,    {1,   121, 62, 30}},
    {"magic",        "skill_icon_5",  GUI_SKILL_PLAYER_MAGIC,     {1,   151, 62, 30}},
    {"runecraft",    "skill_icon_18", GUI_SKILL_PLAYER_OTHER,     {1,   181, 62, 30}},
    {"construction", "skill_icon_22", GUI_SKILL_PLAYER_OTHER,     {1,   211, 62, 32}},
    {"hitpoints",    "skill_icon_6",  GUI_SKILL_PLAYER_HITPOINTS, {64,  1,   62, 30}},
    {"agility",      "skill_icon_7",  GUI_SKILL_PLAYER_OTHER,     {64,  31,  62, 30}},
    {"herblore",     "skill_icon_8",  GUI_SKILL_PLAYER_OTHER,     {64,  61,  62, 30}},
    {"thieving",     "skill_icon_9",  GUI_SKILL_PLAYER_OTHER,     {64,  91,  62, 30}},
    {"crafting",     "skill_icon_10", GUI_SKILL_PLAYER_OTHER,     {64,  121, 62, 30}},
    {"fletching",    "skill_icon_11", GUI_SKILL_PLAYER_OTHER,     {64,  151, 62, 30}},
    {"slayer",       "skill_icon_19", GUI_SKILL_PLAYER_OTHER,     {64,  181, 62, 30}},
    {"hunter",       "skill_icon_21", GUI_SKILL_PLAYER_OTHER,     {64,  211, 62, 32}},
    {"mining",       "skill_icon_12", GUI_SKILL_PLAYER_OTHER,     {127, 1,   62, 30}},
    {"smithing",     "skill_icon_13", GUI_SKILL_PLAYER_OTHER,     {127, 31,  62, 30}},
    {"fishing",      "skill_icon_14", GUI_SKILL_PLAYER_OTHER,     {127, 61,  62, 30}},
    {"cooking",      "skill_icon_15", GUI_SKILL_PLAYER_OTHER,     {127, 91,  62, 30}},
    {"firemaking",   "skill_icon_16", GUI_SKILL_PLAYER_OTHER,     {127, 121, 62, 30}},
    {"woodcutting",  "skill_icon_17", GUI_SKILL_PLAYER_OTHER,     {127, 151, 62, 30}},
    {"farming",      "skill_icon_20", GUI_SKILL_PLAYER_OTHER,     {127, 181, 62, 30}},
    {"sailing",      "skill_icon_23", GUI_SKILL_PLAYER_OTHER,     {127, 211, 62, 32}},
};

static void gui_skill_values(const Player* p, GuiSkillValueKind kind, int* current, int* base) {
    switch (kind) {
        case GUI_SKILL_PLAYER_ATTACK:
            *current = p->current_attack;
            *base = p->base_attack;
            break;
        case GUI_SKILL_PLAYER_STRENGTH:
            *current = p->current_strength;
            *base = p->base_strength;
            break;
        case GUI_SKILL_PLAYER_DEFENCE:
            *current = p->current_defence;
            *base = p->base_defence;
            break;
        case GUI_SKILL_PLAYER_RANGED:
            *current = p->current_ranged;
            *base = p->base_ranged;
            break;
        case GUI_SKILL_PLAYER_PRAYER:
            *current = p->current_prayer;
            *base = p->base_prayer;
            break;
        case GUI_SKILL_PLAYER_MAGIC:
            *current = p->current_magic;
            *base = p->base_magic;
            break;
        case GUI_SKILL_PLAYER_HITPOINTS:
            *current = p->current_hitpoints;
            *base = p->base_hitpoints;
            break;
        case GUI_SKILL_PLAYER_OTHER:
            *current = 99;
            *base = 99;
            break;
    }
    if (*base <= 0) *base = 1;
    if (*current <= 0) *current = *base;
}

static Color gui_skill_current_color(int current, int base) {
    if (current < base) return (Color){220, 45, 31, 255};
    if (current > base) return GUI_TEXT_GREEN;
    return GUI_TEXT_YELLOW;
}

static void gui_draw_skill_panel_slot(
    GuiState* gs,
    const GuiSkillPanelSlot* slot,
    Rectangle rect,
    int current,
    int base
) {
    DrawRectangleRec(rect, (Color){72, 70, 60, 232});
    DrawLineEx((Vector2){rect.x, rect.y}, (Vector2){rect.x + rect.width - 1, rect.y},
        1, (Color){139, 130, 104, 255});
    DrawLineEx((Vector2){rect.x, rect.y}, (Vector2){rect.x, rect.y + rect.height - 1},
        1, (Color){139, 130, 104, 255});
    DrawLineEx((Vector2){rect.x, rect.y + rect.height - 1},
        (Vector2){rect.x + rect.width - 1, rect.y + rect.height - 1},
        1, (Color){28, 25, 21, 255});
    DrawLineEx((Vector2){rect.x + rect.width - 1, rect.y},
        (Vector2){rect.x + rect.width - 1, rect.y + rect.height - 1},
        1, (Color){28, 25, 21, 255});

    Rectangle icon_rect = {rect.x + 3, rect.y + 3, 24, 24};
    gui_draw_named_asset_centered(gs, slot->icon_asset, icon_rect, 24, 24, WHITE);

    char current_text[8];
    char base_text[8];
    snprintf(current_text, sizeof(current_text), "%d", current);
    snprintf(base_text, sizeof(base_text), "%d", base);
    gui_text_shadow(gs, current_text, (int)rect.x + 39, (int)rect.y + 2, 10,
        gui_skill_current_color(current, base));
    gui_text_shadow(gs, base_text, (int)rect.x + 39, (int)rect.y + 17, 9, GUI_TEXT_GREEN);
}

static void gui_draw_stats(GuiState* gs, Player* p) {
    int total_level = 0;
    int count = (int)(sizeof(GUI_SKILL_PANEL_SLOTS) / sizeof(GUI_SKILL_PANEL_SLOTS[0]));

    for (int i = 0; i < count; i++) {
        const GuiSkillPanelSlot* slot = &GUI_SKILL_PANEL_SLOTS[i];
        int current = 0;
        int base = 0;
        gui_skill_values(p, slot->value_kind, &current, &base);
        total_level += base;

        Rectangle rect = gui_side_component_rect(gs, "stats", slot->component_name, slot->fallback);
        gui_draw_skill_panel_slot(gs, slot, rect, current, base);
    }

    Rectangle total = gui_side_component_rect(gs, "stats", "total", (Rectangle){0, 241, 190, 19});
    DrawRectangleRec(total, (Color){7, 7, 7, 238});
    DrawRectangleLinesEx(total, 1, (Color){99, 91, 68, 255});
    const char* total_text = TextFormat("Total level: %d", total_level);
    int width = gui_measure_text(gs, total_text, 10);
    gui_text_shadow(gs, total_text, (int)(total.x + (total.width - width) * 0.5f),
        (int)(total.y + 5), 10, GUI_TEXT_YELLOW);
}


static void gui_cycle_entity(GuiState* gs) {
    if (gs->gui_entity_count <= 0) return;
    gs->gui_entity_idx = (gs->gui_entity_idx + 1) % gs->gui_entity_count;
}

/* Draw the resizable-mode side panel: minimap area at top (handled outside),
   then top tab row, content area, and bottom tab row at the very bottom. */
static void gui_draw(GuiState* gs, Player* p) {
    int content_y = gs->panel_y + gs->status_bar_h + gs->tab_h;
    gui_draw_tab_bar(gs);

    /* entity selector header */
    if (gs->gui_entity_count > 1) {
        int hx = gs->panel_x + GUI_SIDE_CONTENT_X + 4;
        int hy = content_y + 2;
        const char* etype = (p->entity_type == ENTITY_NPC) ? "NPC" : "Player";
        gui_text_shadow(gs, TextFormat("[G] %s %d/%d", etype,
                        gs->gui_entity_idx + 1, gs->gui_entity_count),
                        hx, hy, 8, GUI_TEXT_ORANGE);
    }

    /* active tab content */
    switch (gs->active_tab) {
        case GUI_TAB_COMBAT:    gui_draw_combat(gs, p);    break;
        case GUI_TAB_INVENTORY: gui_draw_inventory(gs, p); break;
        case GUI_TAB_EQUIPMENT: gui_draw_equipment(gs, p); break;
        case GUI_TAB_PRAYER:    gui_draw_prayer(gs, p);    break;
        case GUI_TAB_SPELLBOOK: gui_draw_spellbook(gs, p); break;
        case GUI_TAB_STATS:     gui_draw_stats(gs, p);     break;
        case GUI_TAB_QUESTS:    /* empty tab */ break;
        default: break;
    }
}

#endif /* OSRS_GUI_H */
