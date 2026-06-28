/**
 * @fileoverview Raylib debug viewer for OSRS PvP simulation.
 *
 * 3D OSRS visual runner with GUI, overheads, hit splats, projectiles,
 * collision overlays, and encounter debug panels.
 * Included conditionally via OSRS_VISUAL define.
 *
 * Follows PufferLib's Client + make_client + c_render pattern.
 */

#ifndef OSRS_RENDER_H
#define OSRS_RENDER_H

#include "raylib.h"
#include "rlgl.h"
#include "raymath.h"
#include "osrs_models.h"
#include "osrs_asset_raylib.h"
#include "osrs_anim.h"
#include "osrs_combat_visuals.h"
#include "osrs_combat.h"
#include "osrs_pvp_combat.h"
#include "osrs_pvp_effects.h"
#include "osrs_projectile_orientation.h"
#include "osrs_render_motion.h"
#include "osrs_render_click_hull.h"
#include "data/player_models.h"
#include "data/npc_models.h"
#include "osrs_terrain.h"
#include "osrs_objects.h"
#include "osrs_gui.h"
#include "osrs_human_input.h"
#include "encounters/encounter_colosseum.h"
#include <ctype.h>
#include <limits.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>


#define RENDER_TILE_SIZE       20
/* window dimensions. OSRS resizable mode UI is STATIC pixel size — the panel
   column is always 241 wide regardless of window dimensions, with the game
   viewport filling the remaining width. */
#define RENDER_WINDOW_W        1148
#define RENDER_WINDOW_H        755
#define RENDER_PANEL_WIDTH     GUI_SIDE_MENU_W
#define RENDER_HEADER_HEIGHT   0     /* OSRS client has no top header strip */
#define RENDER_SPLATS_PER_PLAYER 4   /* OSRS max: 4 simultaneous splats per entity */
#define RENDER_HISTORY_INITIAL_CAPACITY 2000
#define MAX_RENDER_ENTITIES    64    /* max entities rendered (players + NPCs/bosses/adds) */

#define ANIM_SEQ_IDLE           808
#define ANIM_SEQ_WALK           819
#define ANIM_SEQ_RUN            824
#define ANIM_SEQ_EAT            829
#define ANIM_SEQ_DEATH          836
#define ANIM_SEQ_CAST_STANDARD  1162
#define ANIM_SEQ_CAST_BARRAGE   1979
#define ANIM_SEQ_CAST_VENG      4410
#define ANIM_SEQ_BLOCK_SHIELD   1156
#define ANIM_SEQ_BLOCK_MELEE    424

#define RENDER_GRID_W (RENDER_WINDOW_W - RENDER_PANEL_WIDTH)  /* = 575, OSRS ≈ 512 */
#define RENDER_GRID_H (RENDER_WINDOW_H - RENDER_HEADER_HEIGHT)  /* = 503, OSRS ≈ 334 */

/* minimap + orbs occupy the top of the right-hand panel column. OSRS resizable
   mode uses STATIC native sprite sizes — no scaling — so the chrome takes up
   the same pixel footprint regardless of window size. */
#define RENDER_MINIMAP_AREA_H    GUI_MAP_CONTAINER_H
#define RENDER_MINIMAP_CIRCLE_R  (GUI_MINIMAP_W / 2)
#define RENDER_MINIMAP_CIRCLE_CY (GUI_MINIMAP_Y + GUI_MINIMAP_H / 2)
#define RENDER_ORB_R             16

/* Side panel layout (resizable mode): two anchored blocks.
   - Minimap chrome anchored to TOP-RIGHT (y=0..RENDER_MINIMAP_AREA_H).
   - Inventory/tabs panel anchored to BOTTOM-RIGHT (top tabs + content + bottom tabs).
   The middle of the right column shows the game viewport. */
#define RENDER_TAB_ROW_H        37   /* native rm_tabs_top_row height */
#define RENDER_PANEL_CONTENT_H  GUI_SIDE_CONTENT_H

/* colors */
#define COLOR_BG          CLITERAL(Color){ 20, 20, 25, 255 }
#define COLOR_GRID        CLITERAL(Color){ 45, 45, 55, 255 }
#define COLOR_HEADER_BG   CLITERAL(Color){ 30, 30, 40, 255 }
#define COLOR_PANEL_BG    CLITERAL(Color){ 25, 25, 35, 255 }
#define COLOR_P0          CLITERAL(Color){ 80, 140, 255, 255 }
#define COLOR_P1          CLITERAL(Color){ 255, 90, 90, 255 }
#define COLOR_P0_LIGHT    CLITERAL(Color){ 80, 140, 255, 60 }
#define COLOR_P1_LIGHT    CLITERAL(Color){ 255, 90, 90, 60 }
#define COLOR_FREEZE      CLITERAL(Color){ 100, 170, 255, 90 }
#define COLOR_VENG        CLITERAL(Color){ 255, 220, 50, 255 }
#define COLOR_BLOCKED     CLITERAL(Color){ 200, 50, 50, 50 }
#define COLOR_WALL        CLITERAL(Color){ 220, 150, 40, 50 }
#define COLOR_BRIDGE      CLITERAL(Color){ 50, 120, 220, 50 }
#define COLOR_WALL_LINE   CLITERAL(Color){ 255, 180, 50, 180 }
#define COLOR_HP_GREEN    CLITERAL(Color){ 50, 200, 50, 255 }
#define COLOR_HP_RED      CLITERAL(Color){ 200, 50, 50, 255 }
#define COLOR_HP_BG       CLITERAL(Color){ 40, 40, 40, 200 }
#define COLOR_SPEC_BAR    CLITERAL(Color){ 230, 170, 30, 255 }
#define COLOR_TEXT         CLITERAL(Color){ 200, 200, 210, 255 }
#define COLOR_TEXT_DIM     CLITERAL(Color){ 130, 130, 140, 255 }
#define COLOR_LABEL        CLITERAL(Color){ 170, 170, 180, 255 }


/* OSRS projectile flight parameters (from deob client Projectile.java):
 *   x/y: linear interpolation from source to target
 *   height: parabolic arc — initial slope from 'curve' param,
 *           quadratic correction to hit end_height exactly.
 *   Zulrah attacks: delay=1, duration=35 client ticks, startH=85, endH=40,
 *                   curve=16 (~22.5 degree launch angle).
 *   1 client tick = 20ms, 1 server tick = 600ms = 30 client ticks.
 */

/* inferno can keep multiple Zuk healer spark volleys and other flights alive
   at once. size this from the real encounter envelope instead of a tiny demo
   value so visuals never silently disappear. */
#define MAX_FLIGHT_PROJECTILES 64
#define RENDER_CLIENT_TICK_SECONDS 0.020
#define RENDER_CLIENT_TICKS_PER_GAME_TICK 30.0f
#define RENDER_DEFAULT_GAME_TICKS_PER_SECOND 1.667f
#define RENDER_MAX_VISUAL_GAME_TICKS_PER_SECOND 50.0f
#define PROJ_OSRS_SLOPE_TO_RAD 0.02454369f  /* pi/128, converts OSRS slope units to radians */

typedef struct {
    int active;
    float src_x, src_y;         /* source tile position */
    float dst_x, dst_y;         /* target tile position (updated each tick if tracking) */
    float x, y;                 /* current interpolated position */
    float progress;             /* 0.0 (spawned) → 1.0 (arrived) */
    float speed;                /* progress per client tick (1.0/duration) */
    float start_height;         /* height at source (tiles above ground) */
    float end_height;           /* height at target (tiles above ground) */
    float curve;                /* OSRS slope param (16 = ~22.5 degrees) */
    int style;                  /* 0=ranged, 1=magic, 2=melee, 3=cloud */
    int damage;                 /* hit splat value at arrival */

    /* OSRS tracking: projectiles re-aim toward target each sub-tick */
    float vel_x, vel_y;         /* current horizontal velocity (tiles per progress unit) */
    float height_vel;           /* current vertical velocity */
    float height_accel;         /* quadratic height correction */
    float yaw;                  /* current facing direction (radians) */
    float pitch;                /* current vertical tilt (radians) */
    float arc_height;           /* sinusoidal arc peak in tiles (0 = use quadratic) */
    int tracks_target;          /* 1 = re-aim toward target each tick */
    int source_kind;
    int source_npc_slot;
    int target_kind;
    int target_npc_slot;
    int start_delay;            /* client ticks before projectile becomes visible/moves */
    int motion_mode;            /* EncounterProjectileMotionMode */
    float offset_x, offset_y, offset_z;
    uint32_t model_id;          /* GFX model from cache (0 = style-based fallback) */
    int anim_id;                /* spotanim animation sequence (-1 = static model) */
    int travel_gfx_id;
    int travel_gfx_drives_model;
    int anim_frame;
    int anim_tick_counter;
    AnimModelState* anim_state;
    int launch_gfx_id;
    int impact_gfx_id;          /* landing spotanim to spawn on arrival */
} FlightProjectile;


/* OSRS composites all body parts + equipment into a single merged model
 * before animating. this ensures vertex skin label groups span the full
 * body, so origin/pivot transforms compute correct centroids.
 * we replicate that here: one composite mesh per player. */

#define COMPOSITE_MAX_BASE_VERTS 12000  /* ~16 models * ~750 base verts each */
#define COMPOSITE_MAX_FACES      8000   /* ~16 models * ~500 faces each */
#define COMPOSITE_MAX_EXP_VERTS  (COMPOSITE_MAX_FACES * 3)

typedef struct {
    /* merged base geometry for animation */
    int16_t   base_vertices[COMPOSITE_MAX_BASE_VERTS * 3];
    uint8_t   vertex_skins[COMPOSITE_MAX_BASE_VERTS];
    uint16_t  face_indices[COMPOSITE_MAX_FACES * 3];
    int       base_vert_count;
    int       face_count;

    /* raylib mesh (pre-allocated at max capacity, updated per frame) */
    Mesh  mesh;
    Model model;
    int   gpu_ready;

    /* animation working state (rebuilt on equipment change) */
    AnimModelState* anim_state;

    /* change detection: last-seen equipment (players) or NPC def ID (NPCs) */
    uint8_t last_equipped[NUM_GEAR_SLOTS];
    int     last_npc_def_id;
    int     needs_rebuild;
} PlayerComposite;


#define HULL_MAX_POINTS 512
#define RENDER_CLICK_HULL_MAX_INPUT_POINTS \
    (COMPOSITE_MAX_EXP_VERTS + RENDER_CLICKBOX_PRISM_POINT_COUNT)

typedef struct {
    int xs[HULL_MAX_POINTS];
    int ys[HULL_MAX_POINTS];
    int count;
} ConvexHull2D;

/** Jarvis march: compute 2D convex hull from screen-space points.
    xs/ys are input arrays of length n. out is populated with the hull.
    ported from RuneLite Jarvis.java. */
static void hull_compute(const int* xs, const int* ys, int n, ConvexHull2D* out) {
    out->count = 0;
    if (n < 3) return;

    /* find leftmost point */
    int left = 0;
    for (int i = 1; i < n; i++) {
        if (xs[i] < xs[left] || (xs[i] == xs[left] && ys[i] < ys[left]))
            left = i;
    }

    int current = left;
    do {
        int cx = xs[current], cy = ys[current];
        if (out->count >= HULL_MAX_POINTS) { out->count = 0; return; }
        out->xs[out->count] = cx;
        out->ys[out->count] = cy;
        out->count++;

        /* safety: hull can't have more points than input */
        if (out->count > n) { out->count = 0; return; }

        int next = 0;
        int nx = xs[0], ny = ys[0];
        for (int i = 1; i < n; i++) {
            /* cross product: positive means i is to the left of current→next */
            long long cp = (long long)(ys[i] - cy) * (nx - xs[i])
                         - (long long)(xs[i] - cx) * (ny - ys[i]);
            if (cp > 0) {
                next = i; nx = xs[i]; ny = ys[i];
            } else if (cp == 0) {
                /* collinear: pick the farther point */
                long long d_i = (long long)(cx - xs[i]) * (cx - xs[i])
                              + (long long)(cy - ys[i]) * (cy - ys[i]);
                long long d_n = (long long)(cx - nx) * (cx - nx)
                              + (long long)(cy - ny) * (cy - ny);
                if (d_i > d_n) { next = i; nx = xs[i]; ny = ys[i]; }
            }
        }
        current = next;
    } while (current != left);
}

/** Point-in-polygon test (ray casting method).
    returns 1 if (px, py) is inside the convex hull. */
static int hull_contains(const ConvexHull2D* hull, int px, int py) {
    if (hull->count < 3) return 0;
    int inside = 0;
    for (int i = 0, j = hull->count - 1; i < hull->count; j = i++) {
        int xi = hull->xs[i], yi = hull->ys[i];
        int xj = hull->xs[j], yj = hull->ys[j];
        if (((yi > py) != (yj > py)) &&
            (px < (xj - xi) * (py - yi) / (yj - yi) + xi))
            inside = !inside;
    }
    return inside;
}

static void hull_append_projected_world_point(
    const Camera3D* cam,
    Vector3 world_point,
    int* xs,
    int* ys,
    int* count,
    int capacity
) {
    if (*count >= capacity) return;
    Vector2 sv = GetWorldToScreen(world_point, *cam);
    if (sv.x < -1000 || sv.x > 5000 || sv.y < -1000 || sv.y > 5000) return;
    xs[*count] = (int)sv.x;
    ys[*count] = (int)sv.y;
    (*count)++;
}


/* per-entity hitsplat slot matching OSRS Entity.java exactly:
   - hitmarkMove starts at +5.0, decreases by 0.25/client-tick, clamps at -5.0
   - hitmarkTrans (opacity) starts at 230, stays there (mode 2 never fades)
   - hitsLoopCycle: expires after 70 client ticks
   - slot layout from Client.java:6052: slot 0=center, 1=up20, 2=left15+up10, 3=right15+up10 */
typedef struct {
    int active;
    int damage;
    int type;
    double hitmark_move;   /* OSRS hitmarkMove: starts +5, decrements to -5 */
    int hitmark_trans;     /* OSRS hitmarkTrans: opacity 0-230, starts 230 */
    int ticks_remaining;   /* counts down from 70 client ticks */
} HitSplat;

typedef struct {
    int primary_seq_id;
    int primary_frame_idx;
    int primary_ticks;
    int primary_loops;
    int secondary_seq_id;
    int secondary_frame_idx;
    int secondary_ticks;
} RenderAnimationState;

typedef struct {
    RenderAnimationState anim;
    int primary_event_tick;
    int last_primary_event_tick;
    float sub_x;
    float sub_y;
    float dest_x;
    float dest_y;
    int visual_moving;
    int visual_running;
    int visual_explicit_running;
    int visual_backlog;
    float visual_effective_speed;
    int step_tracker;
    float yaw;
    float target_yaw;
    int facing_opponent;
    int hp_bar_visible_until;
    HitSplat splats[RENDER_SPLATS_PER_PLAYER];
} RenderVisualSlotSnapshot;


#define CONTEXT_MENU_MAX_ITEMS 64
#define CONTEXT_MENU_ROW_H     20
#define CONTEXT_MENU_ITEM_TOP  22
#define CONTEXT_MENU_ITEM_H    18
#define CONTEXT_MENU_TITLE_H   24
#define CONTEXT_MENU_PADDING    4
#define CONTEXT_MENU_MIN_W    158

typedef enum {
    CMENU_ACTION_NONE = 0,
    CMENU_ACTION_WALK_HERE,
    CMENU_ACTION_ATTACK,
    CMENU_ACTION_LAB_SELECT_NPC,
    CMENU_ACTION_LAB_MOVE_SELECTED_NPC,
    CMENU_ACTION_LAB_PLACE_PLAYER,
    CMENU_ACTION_LAB_SPAWN_NPC,
    CMENU_ACTION_LAB_KILL_NPC,
    CMENU_ACTION_LAB_DELETE_NPC,
    CMENU_ACTION_LAB_TOGGLE_PILLAR,
    CMENU_ACTION_GUI_INVENTORY_PRIMARY,
    CMENU_ACTION_GUI_INVENTORY_USE,
    CMENU_ACTION_GUI_PRAYER,
    CMENU_ACTION_GUI_SPELL,
    CMENU_ACTION_GUI_COMBAT_STYLE,
    CMENU_ACTION_GUI_AUTOCAST,
    CMENU_ACTION_GUI_SPEC_TOGGLE,
    CMENU_ACTION_CANCEL,
} ContextMenuAction;

typedef struct {
    ContextMenuAction action;
    int entity_idx;         /* render entity index for ATTACK, -1 for walk/cancel */
    int npc_type;
    int pillar_idx;
    int inventory_slot;
    int prayer_idx;
    int spell_idx;
    int fight_style;
    int autocast_spell;
    int autocast_defensive;
    char label[64];         /* display text, e.g. "Attack Jal-Zek" */
} ContextMenuItem;

typedef struct {
    int visible;
    int screen_x, screen_y; /* top-left of the menu popup */
    int width;               /* computed from widest label */
    int item_count;
    ContextMenuItem items[CONTEXT_MENU_MAX_ITEMS];
    int walk_tile_x, walk_tile_y;  /* world tile for "Walk here" */
    int click_screen_x, click_screen_y;
    int hover_idx;           /* item currently hovered, -1 = none */
} ContextMenu;

static int context_menu_height(const ContextMenu* cm) {
    return CONTEXT_MENU_TITLE_H + cm->item_count * CONTEXT_MENU_ROW_H;
}

static int context_menu_row_at(const ContextMenu* cm, int mx, int my) {
    int menu_h = context_menu_height(cm);
    if (mx < cm->screen_x || mx >= cm->screen_x + cm->width ||
            my < cm->screen_y || my >= cm->screen_y + menu_h) {
        return -1;
    }
    if (my < cm->screen_y + CONTEXT_MENU_ITEM_TOP) return -1;
    int row = (my - cm->screen_y - CONTEXT_MENU_ITEM_TOP) / CONTEXT_MENU_ROW_H;
    if (row < 0 || row >= cm->item_count) return -1;
    return row;
}

static void context_menu_draw_text_shadow(
    const GuiState* gs,
    const char* text,
    int x,
    int y,
    int size,
    Color color
) {
    gui_text_shadow(gs, text, x, y, size, color);
}

typedef struct RenderClient {
    /* viewer state */
    int is_paused;
    float ticks_per_second;
    int step_once;
    int step_back;

    /* overlay toggles */
    int show_collision;
    int show_pathfinding;
    int show_models;
    int show_safe_spots;
    int show_debug;       /* toggle raycast debug, hulls, hitboxes, projectile trails */
    int lab_enabled;
    int lab_show_forecast;
    int lab_selected_npc_slot;
    int lab_prev_paused;
    int lab_prev_human_enabled;
    void* lab_entry_snapshot;
    size_t lab_entry_snapshot_size;
    int lab_restore_requested;
    int lab_restore_generation;

    /* UI layout mode: 0 = fixed (1182/1183 chrome), 1 = resizable (1177/1178).
       L key toggles. mirrors OSRS client display modes. */
    int layout_mode;

    /* 3D model rendering */
    ModelCache* model_cache;
    AnimCache* anim_cache;
    ModelCache* projectile_model_cache;
    OsrsSpotAnimSet* spotanims;
    ModelCache* npc_model_cache;  /* secondary cache for encounter-specific NPC models */
    AnimCache* npc_anim_cache;    /* secondary cache for encounter-specific NPC anims */
    float model_scale;

    /* overhead prayer icon textures (from headicons_prayer sprites) */
    Texture2D prayer_icons[6];  /* indexed by headIcon: 0=melee,1=ranged,2=magic,3=retri,4=smite,5=redemp */
    int prayer_icons_loaded;

    Texture2D colosseum_modifier_icons[COLO_NUM_REAL_MODIFIERS][3];

    /* hitsplat sprite textures (from hitmarks sprites, 317 mode 0).
       0=blue(miss), 1=red(regular), 2=green(poison), 3=dark(venom), 4=yellow(shield) */
    Texture2D hitmark_sprites[5];
    int hitmark_sprites_loaded;

    /* click cross sprites: 4 yellow (move) + 4 red (attack) animation frames */
    Texture2D click_cross_sprites[8];
    int click_cross_loaded;

    /* debug: last raycast-selected tile (-1 = none) */
    int debug_hit_wx, debug_hit_wy;
    float debug_ray_hit_x, debug_ray_hit_y, debug_ray_hit_z;
    /* ray-plane comparison */
    int debug_plane_wx, debug_plane_wy;
    /* ray info */
    Vector3 debug_ray_origin, debug_ray_dir;

    /* render entities: populated per-frame from env->players or encounter vtable.
       index 0 = agent, 1+ = opponents/NPCs/bosses.
       stored by value (not pointer) via fill_render_entities. */
    RenderEntity entities[MAX_RENDER_ENTITIES];
    int entity_count;

    /* per-entity composite model (merged body + equipment, animated as one) */
    PlayerComposite composites[MAX_RENDER_ENTITIES];

    /* per-entity 2D convex hull for click detection (projected model vertices).
       recomputed every frame after 3D rendering, used by click handler. */
    ConvexHull2D entity_hulls[MAX_RENDER_ENTITIES];
    float entity_visual_top_y[MAX_RENDER_ENTITIES];  /* world-space top of animated mesh */
    float entity_visual_mid_y[MAX_RENDER_ENTITIES];  /* world-space middle of animated mesh */

    /* per-entity two-track animation (matches OSRS primary + secondary system) */
    RenderAnimationState anim[MAX_RENDER_ENTITIES];
    int primary_event_tick[MAX_RENDER_ENTITIES];
    int last_primary_event_tick[MAX_RENDER_ENTITIES];

    /* entity identity tracking — detect slot compaction shifts to reset stale anim/composite */
    int prev_npc_slot[MAX_RENDER_ENTITIES];
    int prev_entity_count;

    /* terrain */
    TerrainMesh* terrain;

    /* placed objects (walls, buildings, trees) */
    ObjectMesh* objects;
    ObjectMesh* objects_zuk;  /* post-Zuk variant (prison walls removed) */
    int zuk_active;           /* set when Zuk NPC (7706) is present */

    /* NPC models at spawn positions */
    ObjectMesh* npcs;

    float cam_yaw;      /* radians, 0 = looking north */
    float cam_pitch;     /* radians, clamped */
    float cam_dist;      /* distance from target */
    float cam_target_x;  /* world X (tile coords) */
    float cam_target_z;  /* world Z (tile coords) */

    /* camera zoom (scroll wheel zooms entire view) */
    float zoom;

    /* per-entity hit splats (4 slots each, OSRS style) */
    HitSplat splats[MAX_RENDER_ENTITIES][RENDER_SPLATS_PER_PLAYER];

    /* per-entity sub-tile position and facing (OSRS: 128 units per tile) */
    float sub_x[MAX_RENDER_ENTITIES], sub_y[MAX_RENDER_ENTITIES];
    float dest_x[MAX_RENDER_ENTITIES], dest_y[MAX_RENDER_ENTITIES];
    int visual_moving[MAX_RENDER_ENTITIES];
    int visual_running[MAX_RENDER_ENTITIES];
    int visual_explicit_running[MAX_RENDER_ENTITIES];
    int visual_backlog[MAX_RENDER_ENTITIES];
    float visual_effective_speed[MAX_RENDER_ENTITIES];
    int step_tracker[MAX_RENDER_ENTITIES];
    float yaw[MAX_RENDER_ENTITIES];
    float target_yaw[MAX_RENDER_ENTITIES];
    int facing_opponent[MAX_RENDER_ENTITIES];

    /* HP bar visibility timer: only shown after taking damage.
       matches OSRS Entity.cycleStatus (300 client ticks = 6s).
       in game ticks: set to env->tick + 10, visible while tick < this. */
    int hp_bar_visible_until[MAX_RENDER_ENTITIES];

    /* visual effects: spell impacts, projectiles */
    ActiveEffect effects[MAX_ACTIVE_EFFECTS];
    int effect_client_tick_counter;  /* monotonic 50 Hz counter for effect timing */

    /* client-tick accumulator: OSRS runs both movement AND animation at 50 Hz
       (20ms per client tick). we accumulate real time and process the correct
       number of steps per render frame, matching the real client exactly. */
    double client_tick_accumulator;

    /* arena bounds (overridden by encounter, defaults to FIGHT_AREA_*) */
    int arena_base_x, arena_base_y;
    int arena_width, arena_height;

    /* encounter visual overlay (populated by encounter's render_post_tick) */
    EncounterOverlay encounter_overlay;

    /* pre-built static models for overlay rendering (clouds, projectiles, snakelings).
       built once at init from model cache, drawn at overlay positions each frame. */
    Model cloud_model;       int cloud_model_ready;
    Model molten_model;      int molten_model_ready; int molten_model_attempted;
    Model snakeling_model;   int snakeling_model_ready;
    Model ranged_proj_model; int ranged_proj_model_ready;
    Model magic_proj_model;  int magic_proj_model_ready;
    Model cloud_proj_model;  int cloud_proj_model_ready;
    Model pillar_models[4];  int pillar_models_ready;  /* 0=100%, 1=75%, 2=50%, 3=25% HP */

    /* active projectile flights: interpolated at 50Hz between game ticks.
       spawned from encounter overlay events, auto-expired on arrival. */
    FlightProjectile flights[MAX_FLIGHT_PROJECTILES];

    /* dynamic projectile model cache: lazily loads per-NPC-type projectile models */
#define MAX_PROJ_MODELS 24
    struct { uint32_t id; Model model; int ready; } proj_models[MAX_PROJ_MODELS];
    int proj_model_count;

    /* collision map: pointer to env's CollisionMap (shared, not owned).
       world offset translates arena coords to collision map world coords. */
    const CollisionMap* collision_map;
    int collision_world_offset_x;
    int collision_world_offset_y;

    /* tick pacing */
    double last_tick_time;

    /* rewind history: env snapshots */
    OsrsEnv* history;
    int history_count;
    int history_capacity;
    int history_cursor;   /* current position when rewinding (-1 = live) */

    /* OSRS GUI panel system (inventory, equipment, prayer, combat, spellbook) */
    GuiState gui;
    RenderTexture2D minimap_surface;
    int minimap_surface_w;
    int minimap_surface_h;

    /* interactive human control (H key toggle) */
    HumanInput human_input;

    /* cursor hover tile: tile under mouse cursor, updated every frame.
       -1 = no valid tile under cursor (off-arena or off-screen). */
    int hover_tile_x, hover_tile_y;

    /* right-click context menu (OSRS-style popup) */
    ContextMenu context_menu;
} RenderClient;

/* forward declarations */
static Camera3D render_build_3d_camera(RenderClient* rc);
static void render_populate_entities(RenderClient* rc, OsrsEnv* env);
static void render_seed_entity_visual_slot(RenderClient* rc, int i);
static void render_reset_episode_visual_state(RenderClient* rc, OsrsEnv* env);
static void context_menu_dismiss(ContextMenu* cm);
static inline int render_world_to_screen_x_rc(RenderClient* rc, int world_x);
static inline int render_world_to_screen_y_rc(RenderClient* rc, int world_y);

static float render_effective_ticks_per_second(RenderClient* rc) {
    if (!rc) return RENDER_DEFAULT_GAME_TICKS_PER_SECOND;
    if (rc->ticks_per_second > 0.0f) return rc->ticks_per_second;
    return RENDER_MAX_VISUAL_GAME_TICKS_PER_SECOND;
}

static double render_scaled_frame_dt(RenderClient* rc, double frame_dt) {
    float tps = render_effective_ticks_per_second(rc);
    return frame_dt * (double)(tps / RENDER_DEFAULT_GAME_TICKS_PER_SECOND);
}

static int render_offhand_uses_shield_block_anim(uint8_t item_idx) {
    switch (item_idx) {
        case ITEM_DRAGON_DEFENDER:
        case ITEM_AVERNIC_DEFENDER:
        case ITEM_MAGES_BOOK:
        case ITEM_BOOK_OF_DARKNESS:
        case ITEM_ELIDINIS_WARD_F:
            return 0;
        case ITEM_SPIRIT_SHIELD:
        case ITEM_BLESSED_SPIRIT_SHIELD:
        case ITEM_SPECTRAL_SPIRIT_SHIELD:
        case ITEM_ELYSIAN_SPIRIT_SHIELD:
        case ITEM_CRYSTAL_SHIELD:
        case ITEM_DRAGONFIRE_SHIELD:
            return 1;
        default:
            return item_idx < NUM_ITEMS && ITEM_DATABASE[item_idx].slot == SLOT_SHIELD;
    }
}

static int render_projectile_profile_value_or(int value, int fallback) {
    return osrs_combat_projectile_value_or(value, fallback);
}

static int render_spawn_profile_projectile(
    RenderClient* rc,
    const OsrsCombatProjectileProfile* profile,
    int src_x, int src_y, int dst_x, int dst_y,
    int delay_client_ticks, int duration_client_ticks,
    int fallback_start_height, int fallback_end_height,
    int fallback_slope
) {
    if (!profile) return -1;
    if (profile->travel_spotanim_id < 0) return -1;
    return effect_spawn_projectile(
        rc->effects, profile->travel_spotanim_id,
        src_x, src_y, dst_x, dst_y,
        delay_client_ticks, duration_client_ticks,
        render_projectile_profile_value_or(
            profile->projectile_start_height, fallback_start_height),
        render_projectile_profile_value_or(
            profile->projectile_end_height, fallback_end_height),
        render_projectile_profile_value_or(
            profile->projectile_angle, fallback_slope),
        rc->effect_client_tick_counter,
        rc->spotanims, rc->model_cache, rc->npc_model_cache,
        rc->projectile_model_cache);
}

/** Get the raw Player* for a given entity index (for GUI functions that need full Player state).
    Returns the Player* from get_entity for encounters that use Player structs (PvP, Zulrah).
    Returns NULL if no encounter or index is out of range. GUI code must NULL-check. */
static Player* render_get_player_ptr(OsrsEnv* env, int index) {
    if (env->encounter_def && env->encounter_state) {
        const EncounterDef* def = (const EncounterDef*)env->encounter_def;
        return (Player*)def->get_entity(
            (EncounterState*)env->encounter_state,
            (EncounterContext*)env->encounter_context,
            index);
    }
    if (index >= 0 && index < NUM_AGENTS)
        return &env->players[index];
    return NULL;
}

/** Look up an animation sequence, checking secondary NPC cache as fallback. */
static AnimSequence* render_get_anim_sequence(RenderClient* rc, uint16_t seq_id) {
    AnimSequence* seq = NULL;
    if (rc->anim_cache) seq = anim_get_sequence(rc->anim_cache, seq_id);
    if (!seq && rc->npc_anim_cache) seq = anim_get_sequence(rc->npc_anim_cache, seq_id);
    return seq;
}

static int render_sequence_stalls_movement(const AnimSequence* seq) {
    if (!seq) return 0;
    if (seq->walk_flag >= 0) return seq->walk_flag == 0;
    return seq->interleave_count == 0;
}

/** Look up an animation framebase, checking secondary NPC cache as fallback. */
static AnimFrameBase* render_get_framebase(RenderClient* rc, uint16_t base_id) {
    AnimFrameBase* fb = NULL;
    if (rc->anim_cache) fb = anim_get_framebase(rc->anim_cache, base_id);
    if (!fb && rc->npc_anim_cache) fb = anim_get_framebase(rc->npc_anim_cache, base_id);
    return fb;
}

typedef struct {
    AnimSequenceFrame* sequence_frame;
    AnimFrameBase* framebase;
} RenderAnimTrackFrame;

/** Resolve a current sequence frame into a legacy framebase-backed frame or a
    baked Maya frame. Legacy sentinel and missing framebase handling matches the
    old render path. */
static RenderAnimTrackFrame render_resolve_anim_track_frame(
    RenderClient* rc,
    AnimSequence* seq,
    int frame_idx
) {
    RenderAnimTrackFrame out = {0};
    if (!seq || seq->frame_count <= 0) return out;
    int fidx = frame_idx % seq->frame_count;
    AnimSequenceFrame* sf = &seq->frames[fidx];
    if (sf->frame.kind == ANIM_FRAME_LEGACY) {
        if (sf->frame.framebase_id != 0xFFFF) {
            AnimFrameBase* fb = render_get_framebase(rc, sf->frame.framebase_id);
            if (fb) {
                out.sequence_frame = sf;
                out.framebase = fb;
            }
        }
    } else if (sf->frame.kind == ANIM_FRAME_MAYA_BAKED) {
        if (!sf->frame.maya_vertices || sf->frame.maya_vertex_count == 0) {
            fprintf(stderr,
                "render: Maya baked animation frame missing vertices in sequence %u\n",
                seq->seq_id);
            abort();
        }
        out.sequence_frame = sf;
    } else {
        fprintf(stderr, "render: unknown animation frame kind %u in sequence %u\n",
            sf->frame.kind, seq->seq_id);
        abort();
    }
    return out;
}

/** Apply one legacy or baked Maya sequence frame to a standalone OSRS model. */
static void render_apply_anim_sequence_frame_to_model_state(
    RenderClient* rc,
    AnimModelState* anim_state,
    OsrsModel* om,
    AnimSequence* seq,
    int frame_idx,
    const char* context
) {
    if (!anim_state || !om || !seq || seq->frame_count <= 0) {
        fprintf(stderr, "render: %s animation state is incomplete\n", context);
        abort();
    }
    AnimSequenceFrame* sf = &seq->frames[frame_idx % seq->frame_count];
    if (sf->frame.kind == ANIM_FRAME_LEGACY) {
        if (sf->frame.framebase_id == 0xFFFF) {
            fprintf(stderr, "render: %s legacy animation frame has no framebase\n",
                context);
            abort();
        }
        AnimFrameBase* fb = render_get_framebase(rc, sf->frame.framebase_id);
        if (!fb) {
            fprintf(stderr, "render: %s legacy animation framebase %u is missing\n",
                context, sf->frame.framebase_id);
            abort();
        }
        anim_apply_frame(anim_state, om->base_vertices, &sf->frame, fb);
    } else if (sf->frame.kind == ANIM_FRAME_MAYA_BAKED) {
        anim_apply_maya_baked_frame(anim_state, &sf->frame);
    } else {
        fprintf(stderr, "render: %s unknown animation frame kind %u in sequence %u\n",
            context, sf->frame.kind, seq->seq_id);
        abort();
    }
    anim_update_mesh(om->mesh.vertices, anim_state,
        om->face_indices, om->mesh.triangleCount);
    UpdateMeshBuffer(om->mesh, 0, om->mesh.vertices,
        om->mesh.triangleCount * 9 * sizeof(float), 0);
    anim_update_mesh_alpha(om->mesh.colors, anim_state,
        om->mesh.triangleCount);
    if (anim_state->face_alphas) {
        UpdateMeshBuffer(om->mesh, 3, om->mesh.colors,
            om->mesh.vertexCount * 4, 0);
    }
}

static InfernoState* render_inferno_state_from_env(OsrsEnv* env) {
    if (!env || !env->encounter_def || !env->encounter_state) return NULL;
    const EncounterDef* def = (const EncounterDef*)env->encounter_def;
    if (strcmp(def->name, "inferno") != 0) return NULL;
    return (InfernoState*)env->encounter_state;
}

static InfernoState* render_inferno_state_from_client(RenderClient* rc) {
    if (!rc || !rc->gui.encounter_def || !rc->gui.encounter_state) return NULL;
    const EncounterDef* def = (const EncounterDef*)rc->gui.encounter_def;
    if (strcmp(def->name, "inferno") != 0) return NULL;
    return (InfernoState*)rc->gui.encounter_state;
}

static ColosseumState* render_colosseum_state_from_env(OsrsEnv* env) {
    if (!env || !env->encounter_def || !env->encounter_state) return NULL;
    const EncounterDef* def = (const EncounterDef*)env->encounter_def;
    if (strcmp(def->name, "colosseum") != 0) return NULL;
    return (ColosseumState*)env->encounter_state;
}

static Color render_inferno_lab_forecast_color(
    const InfStepOutForecastAction* action
) {
    if (!action->valid) return (Color){ 120, 120, 120, 120 };
    if (action->melee_fallback_exposure) return (Color){ 190, 80, 255, 190 };
    if (action->same_tick_mixed_style_conflict) return (Color){ 255, 60, 60, 190 };
    if (action->ranger_mager_offtick_opportunity) return (Color){ 70, 220, 255, 190 };
    for (int t = 0; t < INF_STEP_OUT_FORECAST_HORIZON; t++) {
        if (inf_step_out_forecast_tick_has_event(&action->ticks[t]))
            return (Color){ 255, 170, 40, 180 };
    }
    return (Color){ 60, 220, 80, 170 };
}

static void render_inferno_lab_draw_forecast_3d(RenderClient* rc) {
    InfernoState* s = render_inferno_state_from_client(rc);
    if (!s || !rc->lab_enabled || !rc->lab_show_forecast) return;

    InfStepOutForecast forecast;
    inf_build_step_out_forecast(s, &forecast);
    int has_terrain = rc->terrain && rc->terrain->loaded;
    for (int action_idx = 0; action_idx < ENCOUNTER_MOVE_ACTIONS; action_idx++) {
        const InfStepOutForecastAction* action = &forecast.actions[action_idx];
        float ground = has_terrain
            ? terrain_height_avg(rc->terrain, action->land_x, action->land_y)
            : 2.0f;
        float fx = (float)action->land_x + 0.5f;
        float fz = -(float)(action->land_y + 1) + 0.5f;
        Color color = render_inferno_lab_forecast_color(action);
        DrawCube((Vector3){ fx, ground + 0.06f, fz },
            0.92f, 0.04f, 0.92f, color);
        Color wire = color;
        wire.a = 255;
        DrawCubeWires((Vector3){ fx, ground + 0.09f, fz },
            0.94f, 0.04f, 0.94f, wire);
    }
}

static void render_lab_snap_entity_visual(RenderClient* rc, int entity_idx) {
    if (entity_idx < 0 || entity_idx >= rc->entity_count) return;
    render_seed_entity_visual_slot(rc, entity_idx);
    rc->visual_moving[entity_idx] = 0;
    rc->visual_running[entity_idx] = 0;
    rc->visual_backlog[entity_idx] = 0;
    rc->visual_effective_speed[entity_idx] = 0.0f;
    rc->step_tracker[entity_idx] = 0;
}

static void render_lab_snap_all_visuals(RenderClient* rc) {
    for (int i = 0; i < rc->entity_count; i++)
        render_lab_snap_entity_visual(rc, i);
    rc->prev_entity_count = rc->entity_count;
}

static int render_lab_line_command_is(const char* line, const char* command) {
    while (*line && isspace((unsigned char)*line)) line++;
    size_t command_len = strlen(command);
    return strncmp(line, command, command_len) == 0 &&
        (line[command_len] == '\0' ||
            isspace((unsigned char)line[command_len]));
}

static int render_lab_line_slot_value(const char* line, int* slot) {
    const char* token = strstr(line, "slot=");
    if (!token) return 0;
    char* end = NULL;
    long value = strtol(token + 5, &end, 10);
    if (end == token + 5 || value < 0 || value > INT_MAX) return 0;
    *slot = (int)value;
    return 1;
}

static int render_npc_entity_idx_for_slot(RenderClient* rc, int slot) {
    for (int i = 0; i < rc->entity_count; i++) {
        if (rc->entities[i].entity_type == ENTITY_NPC &&
                rc->entities[i].npc_slot == slot)
            return i;
    }
    return -1;
}

static void render_lab_snap_line_visuals(RenderClient* rc, const char* line) {
    if (render_lab_line_command_is(line, "player") ||
            render_lab_line_command_is(line, "set_player")) {
        render_lab_snap_entity_visual(rc, 0);
        return;
    }
    if (render_lab_line_command_is(line, "move_npc")) {
        int slot = -1;
        if (render_lab_line_slot_value(line, &slot)) {
            int entity_idx = render_npc_entity_idx_for_slot(rc, slot);
            if (entity_idx >= 0) {
                render_lab_snap_entity_visual(rc, entity_idx);
                return;
            }
        }
    }
    if (render_lab_line_command_is(line, "npc") ||
            render_lab_line_command_is(line, "spawn_npc") ||
            render_lab_line_command_is(line, "reset") ||
            render_lab_line_command_is(line, "wave") ||
            render_lab_line_command_is(line, "spawn_wave") ||
            render_lab_line_command_is(line, "delete_npc") ||
            render_lab_line_command_is(line, "clear_npcs")) {
        render_lab_snap_all_visuals(rc);
    }
}

/** the active encounter def iff it supports the scenario lab (a command hook plus
    a snapshot/restore contract for entry/exit), else NULL. Encounter-neutral: the
    viewer drives inferno and colosseum through the same hook. */
static const EncounterDef* render_lab_def(OsrsEnv* env) {
    if (!env || !env->encounter_def || !env->encounter_state) return NULL;
    const EncounterDef* def = (const EncounterDef*)env->encounter_def;
    if (!def->apply_lab_command ||
            !def->snapshot_size || !def->snapshot || !def->restore)
        return NULL;
    return def;
}

/** apply one lab command line to the live encounter via the vtable hook, then
    refresh the renderer's entity list + visual slots so the edit shows at once.
    Drops a stale selected-NPC slot once its entity is gone. */
static void render_lab_apply_line(RenderClient* rc, OsrsEnv* env, const char* line) {
    const EncounterDef* def = render_lab_def(env);
    if (!def) return;
    (void)def->apply_lab_command(
        (EncounterState*)env->encounter_state,
        (EncounterContext*)env->encounter_context,
        line);
    render_populate_entities(rc, env);
    if (rc->lab_selected_npc_slot >= 0) {
        int still_active = 0;
        for (int i = 0; i < rc->entity_count; i++) {
            if (rc->entities[i].entity_type == ENTITY_NPC &&
                    rc->entities[i].npc_slot == rc->lab_selected_npc_slot) {
                still_active = 1;
                break;
            }
        }
        if (!still_active) rc->lab_selected_npc_slot = -1;
    }
    render_lab_snap_line_visuals(rc, line);
}

static void render_lab_clear_entry_snapshot(RenderClient* rc) {
    if (!rc) return;
    free(rc->lab_entry_snapshot);
    rc->lab_entry_snapshot = NULL;
    rc->lab_entry_snapshot_size = 0;
}

static void render_lab_capture_entry_snapshot(RenderClient* rc, OsrsEnv* env) {
    const EncounterDef* def = render_lab_def(env);
    if (!def) return;
    size_t size = def->snapshot_size(
        (EncounterState*)env->encounter_state,
        (EncounterContext*)env->encounter_context);
    void* snapshot = malloc(size);
    if (!snapshot) {
        fprintf(stderr, "lab: snapshot allocation failed\n");
        abort();
    }
    def->snapshot(
        (EncounterState*)env->encounter_state,
        (EncounterContext*)env->encounter_context,
        snapshot);

    render_lab_clear_entry_snapshot(rc);
    rc->lab_entry_snapshot = snapshot;
    rc->lab_entry_snapshot_size = size;
}

static void render_lab_restore_controls(RenderClient* rc) {
    rc->lab_enabled = 0;
    rc->lab_show_forecast = 0;
    rc->lab_selected_npc_slot = -1;
    rc->is_paused = rc->lab_prev_paused;
    rc->human_input.enabled = rc->lab_prev_human_enabled;
    human_input_clear_pending(&rc->human_input);
    human_input_clear_move(&rc->human_input);
    human_input_clear_selected_ui_target(&rc->human_input);
    context_menu_dismiss(&rc->context_menu);
}

static int render_lab_restore_entry_snapshot(RenderClient* rc, OsrsEnv* env) {
    if (!rc || !rc->lab_entry_snapshot) return 0;
    const EncounterDef* def = render_lab_def(env);
    if (!def) return 0;
    def->restore(
        (EncounterState*)env->encounter_state,
        (EncounterContext*)env->encounter_context,
        rc->lab_entry_snapshot,
        rc->lab_entry_snapshot_size);
    if (def->get_tick) {
        env->tick = def->get_tick(
            (EncounterState*)env->encounter_state,
            (EncounterContext*)env->encounter_context);
    }
    rc->lab_restore_requested = 1;
    rc->lab_restore_generation++;
    render_lab_restore_controls(rc);
    render_reset_episode_visual_state(rc, env);
    fprintf(stderr, "lab: restored entry snapshot\n");
    return 1;
}


static inline int render_world_to_screen_x_rc(RenderClient* rc, int world_x) {
    return (world_x - rc->arena_base_x) * RENDER_TILE_SIZE;
}

static inline int render_world_to_screen_y_rc(RenderClient* rc, int world_y) {
    /* flip Y: OSRS Y increases north, screen Y increases down */
    int local_y = world_y - rc->arena_base_y;
    int flipped = (rc->arena_height - 1) - local_y;
    return RENDER_HEADER_HEIGHT + flipped * RENDER_TILE_SIZE;
}

static inline int render_world_to_screen_x(int world_x) {
    return (world_x - FIGHT_AREA_BASE_X) * RENDER_TILE_SIZE;
}

static inline int render_world_to_screen_y(int world_y) {
    int local_y = world_y - FIGHT_AREA_BASE_Y;
    int flipped = (FIGHT_AREA_HEIGHT - 1) - local_y;
    return RENDER_HEADER_HEIGHT + flipped * RENDER_TILE_SIZE;
}

/* forward declarations for composite model system (defined after lifecycle) */
static void composite_free(PlayerComposite* comp);
static int render_select_secondary(RenderClient* rc, int player_idx);

/* forward declaration: inferno_npc_name is defined later in drawing section */
static const char* inferno_npc_name(int npc_def_id);

/** Resolve Fortis Colosseum NPC display names from cache definition ids. */
static const char* colosseum_npc_name(int npc_def_id) {
    switch (npc_def_id) {
        case 12810: return "Jaguar Warrior";
        case 12811: return "Serpent Shaman";
        case 12812: return "Minotaur";
        case 12813: return "Minotaur";
        case 12814: return "Fremennik Archer";
        case 12815: return "Fremennik Seer";
        case 12816: return "Fremennik Berserker";
        case 12817: return "Javelin Colossus";
        case 12818: return "Manticore";
        case 12819: return "Shockwave Colossus";
        case 12821: return "Sol Heredit";
        case 12823: return "Bee Swarm";
        case 12825: return "Healing Totem";
        default: return NULL;
    }
}


/** Resolve display name for a render entity (NPC or player).
    Uses the same lookup chain as render_draw_panel_npc: zulrah forms,
    inferno NPCs, colosseum NPCs, then fallback to "NPC <def_id>". */
static const char* render_entity_display_name(RenderEntity* ent) {
    if (ent->entity_type == ENTITY_PLAYER) return "Player";

    /* zulrah forms */
    if (ent->npc_def_id == 2042) return "Zulrah";
    if (ent->npc_def_id == 2043) return "Zulrah";
    if (ent->npc_def_id == 2044) return "Zulrah";

    /* inferno NPCs */
    const char* inf = inferno_npc_name(ent->npc_def_id);
    if (inf) return inf;

    const char* colo = colosseum_npc_name(ent->npc_def_id);
    if (colo) return colo;

    return TextFormat("NPC %d", ent->npc_def_id);
}

typedef struct {
    RenderClient* rc;
    OsrsEnv* env;
} RenderHumanAttackCtx;

static int render_can_human_attack_entity(
    void* ctx, const RenderEntity* entity, int entity_idx, int gui_entity_idx
) {
    RenderHumanAttackCtx* attack_ctx = (RenderHumanAttackCtx*)ctx;
    if (entity_idx == gui_entity_idx && entity->entity_type == ENTITY_PLAYER) {
        return 0;
    }

    if (entity->entity_type == ENTITY_NPC && !entity->npc_visible) {
        return 0;
    }

    if (attack_ctx->env->encounter_def && attack_ctx->env->encounter_state) {
        const EncounterDef* def = (const EncounterDef*)attack_ctx->env->encounter_def;
        if (entity->entity_type == ENTITY_NPC && def->is_human_targetable_npc_slot) {
            return def->is_human_targetable_npc_slot(
                (EncounterState*)attack_ctx->env->encounter_state,
                (EncounterContext*)attack_ctx->env->encounter_context,
                entity->npc_slot);
        }
    }

    return 1;
}

/** Clear/hide the context menu. */
static void context_menu_dismiss(ContextMenu* cm) {
    cm->visible = 0;
    cm->item_count = 0;
    cm->hover_idx = -1;
}

static ContextMenuItem* context_menu_add(
    ContextMenu* cm, ContextMenuAction action, int entity_idx, const char* label
) {
    if (cm->item_count >= CONTEXT_MENU_MAX_ITEMS) return NULL;
    ContextMenuItem* item = &cm->items[cm->item_count++];
    item->action = action;
    item->entity_idx = entity_idx;
    item->npc_type = -1;
    item->pillar_idx = -1;
    item->inventory_slot = -1;
    item->prayer_idx = -1;
    item->spell_idx = -1;
    item->fight_style = -1;
    item->autocast_spell = -1;
    item->autocast_defensive = 0;
    snprintf(item->label, sizeof(item->label), "%s", label);
    return item;
}

static void context_menu_add_lab_npc(
    ContextMenu* cm, int npc_type, const char* label
) {
    ContextMenuItem* item = context_menu_add(
        cm, CMENU_ACTION_LAB_SPAWN_NPC, -1, label);
    if (!item) return;
    item->npc_type = npc_type;
}

/** entity index of the lab's selected NPC slot, or -1 if it has no live entity.
    encounter-neutral: reads the renderer's populated entity list, not encounter
    state. */
static int render_lab_selected_npc_entity_idx(RenderClient* rc) {
    if (rc->lab_selected_npc_slot < 0) return -1;
    for (int i = 0; i < rc->entity_count; i++) {
        if (rc->entities[i].entity_type == ENTITY_NPC &&
                rc->entities[i].npc_slot == rc->lab_selected_npc_slot)
            return i;
    }
    return -1;
}

/** add the per-encounter "Lab spawn X" palette. item->npc_type holds the
    encounter's own NPC type id; the executor formats it as a numeric type token
    that the encounter's lab parser accepts, so no per-encounter executor branch
    is needed. */
static void render_lab_add_spawn_palette(ContextMenu* cm, const char* encounter_name) {
    if (strcmp(encounter_name, "inferno") == 0) {
        context_menu_add_lab_npc(cm, INF_NPC_RANGER, "Lab spawn ranger");
        context_menu_add_lab_npc(cm, INF_NPC_MAGER, "Lab spawn mager");
        context_menu_add_lab_npc(cm, INF_NPC_JAD, "Lab spawn jad");
        context_menu_add_lab_npc(cm, INF_NPC_ZUK, "Lab spawn Zuk");
        context_menu_add_lab_npc(cm, INF_NPC_BLOB, "Lab spawn blob");
        context_menu_add_lab_npc(cm, INF_NPC_MELEER, "Lab spawn meleer");
        context_menu_add_lab_npc(cm, INF_NPC_BAT, "Lab spawn bat");
        context_menu_add_lab_npc(cm, INF_NPC_NIBBLER, "Lab spawn nibbler");
        context_menu_add_lab_npc(cm, INF_NPC_HEALER_JAD, "Lab spawn Jad healer");
        context_menu_add_lab_npc(cm, INF_NPC_HEALER_ZUK, "Lab spawn Zuk healer");
        context_menu_add_lab_npc(cm, INF_NPC_ZUK_SHIELD, "Lab spawn Zuk shield");
        return;
    }
    if (strcmp(encounter_name, "colosseum") == 0) {
        context_menu_add_lab_npc(cm, COLO_FREMENNIK_BERSERKER, "Lab spawn berserker");
        context_menu_add_lab_npc(cm, COLO_FREMENNIK_ARCHER, "Lab spawn archer");
        context_menu_add_lab_npc(cm, COLO_FREMENNIK_SEER, "Lab spawn seer");
        context_menu_add_lab_npc(cm, COLO_SERPENT_SHAMAN, "Lab spawn shaman");
        context_menu_add_lab_npc(cm, COLO_JAGUAR_WARRIOR, "Lab spawn jaguar");
        context_menu_add_lab_npc(cm, COLO_JAVELIN_COLOSSUS, "Lab spawn javelin");
        context_menu_add_lab_npc(cm, COLO_SHOCKWAVE_COLOSSUS, "Lab spawn shockwave");
        context_menu_add_lab_npc(cm, COLO_MINOTAUR, "Lab spawn minotaur");
        context_menu_add_lab_npc(cm, COLO_MANTICORE, "Lab spawn manticore");
        context_menu_add_lab_npc(cm, COLO_SOL_HEREDIT, "Lab spawn Sol Heredit");
    }
}

static void context_menu_finish_layout(ContextMenu* cm, int mx, int my) {
    int max_w = CONTEXT_MENU_MIN_W;
    int title_w = MeasureText("Choose Option", 11) + CONTEXT_MENU_PADDING * 2 + 10;
    if (title_w > max_w) max_w = title_w;
    for (int i = 0; i < cm->item_count; i++) {
        int w = MeasureText(cm->items[i].label, 11) + CONTEXT_MENU_PADDING * 2 + 12;
        if (w > max_w) max_w = w;
    }
    cm->width = max_w;
    cm->click_screen_x = mx;
    cm->click_screen_y = my;

    int menu_h = context_menu_height(cm);
    cm->screen_x = mx;
    cm->screen_y = my;
    if (cm->screen_x + cm->width > RENDER_WINDOW_W)
        cm->screen_x = RENDER_WINDOW_W - cm->width;
    if (cm->screen_y + menu_h > RENDER_WINDOW_H)
        cm->screen_y = RENDER_WINDOW_H - menu_h;
    if (cm->screen_x < 0) cm->screen_x = 0;
    if (cm->screen_y < 0) cm->screen_y = 0;

    cm->visible = (cm->item_count > 0);
}

/** Build context menu from a right-click at screen position (mx, my). */
static void context_menu_build(RenderClient* rc, OsrsEnv* env, int mx, int my) {
    ContextMenu* cm = &rc->context_menu;
    RenderHumanAttackCtx attack_ctx = { .rc = rc, .env = env };
    const EncounterDef* lab_def = rc->lab_enabled ? render_lab_def(env) : NULL;
    int lab_on = lab_def != NULL;
    cm->item_count = 0;
    cm->hover_idx = -1;
    cm->walk_tile_x = -1;
    cm->walk_tile_y = -1;

    int hit_entities[MAX_RENDER_ENTITIES];
    int hit_count = 0;

    for (int ei = 0; ei < rc->entity_count; ei++) {
        RenderEntity* ent = &rc->entities[ei];
        int usable_entity = lab_on
            ? (ent->entity_type == ENTITY_NPC && ent->npc_slot >= 0)
            : render_can_human_attack_entity(
                &attack_ctx, ent, ei, rc->gui.gui_entity_idx);
        if (!usable_entity) {
            continue;
        }
        if (hull_contains(&rc->entity_hulls[ei], mx, my)) {
            if (hit_count < MAX_RENDER_ENTITIES)
                hit_entities[hit_count++] = ei;
        }
    }

    Camera3D cam = render_build_3d_camera(rc);
    Ray ray = GetScreenToWorldRay((Vector2){ (float)mx, (float)my }, cam);
    float best_dist = 1e30f;
    for (int dy = 0; dy < rc->arena_height; dy++) {
        for (int dx = 0; dx < rc->arena_width; dx++) {
            int wx = rc->arena_base_x + dx;
            int wy = rc->arena_base_y + dy;
            float tx = (float)wx;
            float tz = -(float)(wy + 1);
            float ground_y = rc->terrain
                ? terrain_height_avg(rc->terrain, wx, wy) : 2.0f;
            BoundingBox box = {
                .min = { tx, ground_y - 0.1f, tz },
                .max = { tx + 1.0f, ground_y, tz + 1.0f },
            };
            RayCollision col = GetRayCollisionBox(ray, box);
            if (col.hit && col.distance < best_dist) {
                best_dist = col.distance;
                cm->walk_tile_x = wx;
                cm->walk_tile_y = wy;
            }
        }
    }

    /* build menu items: "Attack <NPC>" for each hit entity, then "Walk here" */
    for (int i = 0; i < hit_count; i++) {
        int ei = hit_entities[i];
        RenderEntity* ent = &rc->entities[ei];
        const char* name = render_entity_display_name(ent);
        char label[64];
        if (lab_on) {
            snprintf(label, sizeof(label), "Lab select %s", name);
            context_menu_add(cm, CMENU_ACTION_LAB_SELECT_NPC, ei, label);
            snprintf(label, sizeof(label), "Lab kill %s", name);
            context_menu_add(cm, CMENU_ACTION_LAB_KILL_NPC, ei, label);
            snprintf(label, sizeof(label), "Lab delete %s", name);
            context_menu_add(cm, CMENU_ACTION_LAB_DELETE_NPC, ei, label);
        }
        if (!lab_on || render_can_human_attack_entity(
                &attack_ctx, ent, ei, rc->gui.gui_entity_idx)) {
            snprintf(label, sizeof(label), "Attack %s", name);
            context_menu_add(cm, CMENU_ACTION_ATTACK, ei, label);
        }
    }

    if (lab_on && cm->walk_tile_x >= 0) {
        context_menu_add(cm, CMENU_ACTION_LAB_PLACE_PLAYER, -1, "Lab place player");
        if (render_lab_selected_npc_entity_idx(rc) >= 0)
            context_menu_add(cm, CMENU_ACTION_LAB_MOVE_SELECTED_NPC, -1,
                "Lab move selected NPC");
        render_lab_add_spawn_palette(cm, lab_def->name);
        if (strcmp(lab_def->name, "inferno") == 0) {
            InfernoState* inf = render_inferno_state_from_env(env);
            int pillar_idx = inf
                ? inf_lab_nearest_pillar_idx(inf, cm->walk_tile_x, cm->walk_tile_y) : -1;
            if (pillar_idx >= 0) {
                ContextMenuItem* item = context_menu_add(
                    cm, CMENU_ACTION_LAB_TOGGLE_PILLAR, -1,
                    TextFormat("Lab toggle pillar %d", pillar_idx));
                if (item) item->pillar_idx = pillar_idx;
            }
        }
    }

    if (cm->walk_tile_x >= 0)
        context_menu_add(cm, CMENU_ACTION_WALK_HERE, -1, "Walk here");

    context_menu_add(cm, CMENU_ACTION_CANCEL, -1, "Cancel");

    context_menu_finish_layout(cm, mx, my);
}

static void context_menu_build_gui(RenderClient* rc, Player* p, int mx, int my) {
    ContextMenu* cm = &rc->context_menu;
    cm->item_count = 0;
    cm->hover_idx = -1;
    cm->walk_tile_x = -1;
    cm->walk_tile_y = -1;

    switch (rc->gui.active_tab) {
        case GUI_TAB_INVENTORY: {
            int slot = gui_inv_slot_at(&rc->gui, mx, my);
            if (slot >= 0) {
                InvSlot* inv = &rc->gui.inv_grid[slot];
                const char* action = gui_inv_primary_action_label(inv);
                const char* name = gui_inv_slot_display_name(inv);
                if (action && name[0]) {
                    ContextMenuItem* item = context_menu_add(
                        cm,
                        CMENU_ACTION_GUI_INVENTORY_PRIMARY,
                        -1,
                        TextFormat("%s %s", action, name));
                    if (item) item->inventory_slot = slot;
                    item = context_menu_add(
                        cm,
                        CMENU_ACTION_GUI_INVENTORY_USE,
                        -1,
                        TextFormat("Use %s", name));
                    if (item) item->inventory_slot = slot;
                }
            }
            break;
        }

        case GUI_TAB_PRAYER: {
            int idx = human_gui_prayer_idx_at(&rc->gui, mx, my);
            if (idx >= 0) {
                const char* name = human_gui_prayer_name((GuiPrayerIdx)idx);
                if (name) {
                    int active = gui_prayer_is_active((GuiPrayerIdx)idx, p);
                    ContextMenuItem* item = context_menu_add(
                        cm,
                        CMENU_ACTION_GUI_PRAYER,
                        -1,
                        TextFormat("%s %s", active ? "Deactivate" : "Activate", name));
                    if (item) item->prayer_idx = idx;
                }
            }
            break;
        }

        case GUI_TAB_SPELLBOOK: {
            int idx = human_gui_spell_idx_at(&rc->gui, mx, my);
            if (idx >= 0 && gui_spell_castable((GuiSpellIdx)idx)) {
                const char* name = human_gui_spell_name((GuiSpellIdx)idx);
                if (name) {
                    ContextMenuItem* item = context_menu_add(
                        cm,
                        CMENU_ACTION_GUI_SPELL,
                        -1,
                        TextFormat("Cast %s", name));
                    if (item) item->spell_idx = idx;
                }
            }
            break;
        }

        case GUI_TAB_COMBAT: {
            GuiCombatStyleOptions styles = gui_combat_style_options(
                p->equipped[GEAR_SLOT_WEAPON]);
            int style_idx = human_gui_combat_style_index_at(&rc->gui, p, mx, my);
            if (style_idx >= 0 && style_idx < styles.count) {
                ContextMenuItem* item = context_menu_add(
                    cm,
                    CMENU_ACTION_GUI_COMBAT_STYLE,
                    -1,
                    TextFormat("Select %s", styles.names[style_idx]));
                if (item) item->fight_style = styles.values[style_idx];
            }

            if (human_gui_autocast_button_hit(&rc->gui, p, mx, my)) {
                int defensive = p->fight_style == FIGHT_STYLE_DEFENSIVE_AUTOCAST ||
                    p->autocast_defensive;
                ContextMenuItem* blood = context_menu_add(
                    cm, CMENU_ACTION_GUI_AUTOCAST, -1, "Autocast Blood Barrage");
                if (blood) {
                    blood->autocast_spell = ENCOUNTER_SPELL_BLOOD;
                    blood->autocast_defensive = defensive;
                }
                ContextMenuItem* ice = context_menu_add(
                    cm, CMENU_ACTION_GUI_AUTOCAST, -1, "Autocast Ice Barrage");
                if (ice) {
                    ice->autocast_spell = ENCOUNTER_SPELL_ICE;
                    ice->autocast_defensive = defensive;
                }
            } else if (rc->gui.autocast_selector_open) {
                int spell = human_gui_autocast_spell_at(&rc->gui, p, mx, my);
                if (spell >= 0) {
                    int defensive = p->fight_style == FIGHT_STYLE_DEFENSIVE_AUTOCAST ||
                        p->autocast_defensive;
                    ContextMenuItem* item = context_menu_add(
                        cm,
                        CMENU_ACTION_GUI_AUTOCAST,
                        -1,
                        TextFormat("Autocast %s", gui_autocast_spell_name(spell)));
                    if (item) {
                        item->autocast_spell = spell;
                        item->autocast_defensive = defensive;
                    }
                }
            }

            if (human_gui_spec_hit(&rc->gui, mx, my)) {
                context_menu_add(cm, CMENU_ACTION_GUI_SPEC_TOGGLE, -1,
                    "Use Special Attack");
            }
            break;
        }

        default:
            break;
    }

    if (cm->item_count == 0) {
        context_menu_dismiss(cm);
        return;
    }

    context_menu_add(cm, CMENU_ACTION_CANCEL, -1, "Cancel");
    context_menu_finish_layout(cm, mx, my);
}

/** Execute a context menu item action on the HumanInput staging buffer. */
static void context_menu_execute(RenderClient* rc, OsrsEnv* env, int item_idx) {
    ContextMenu* cm = &rc->context_menu;
    if (item_idx < 0 || item_idx >= cm->item_count) return;
    ContextMenuItem* item = &cm->items[item_idx];
    int lab_on = render_lab_def(env) != NULL;

    switch (item->action) {
        case CMENU_ACTION_WALK_HERE:
            rc->human_input.pending_move_x = cm->walk_tile_x;
            rc->human_input.pending_move_y = cm->walk_tile_y;
            rc->human_input.pending_attack = 0;
            human_input_queue_walk(&rc->human_input, cm->walk_tile_x, cm->walk_tile_y);
            human_set_click_cross(&rc->human_input, cm->click_screen_x, cm->click_screen_y, 0);
            break;

        case CMENU_ACTION_ATTACK: {
            int ei = item->entity_idx;
            if (ei >= 0 && ei < rc->entity_count) {
                int target_slot = rc->entities[ei].npc_slot;
                if (rc->human_input.cursor_mode == CURSOR_ITEM_TARGET) {
                    human_input_clear_selected_ui_target(&rc->human_input);
                } else if (rc->human_input.cursor_mode == CURSOR_SPELL_TARGET) {
                    human_input_apply_ui_intent(
                        &rc->human_input,
                        osrs_ui_intent_spell_on_target(
                            rc->human_input.selected_spell,
                            rc->human_input.selected_spell_gui_idx,
                            target_slot));
                } else {
                    rc->human_input.pending_attack = 1;
                    rc->human_input.pending_target_idx = target_slot;
                    rc->human_input.pending_move_x = -1;
                    rc->human_input.pending_move_y = -1;
                    human_input_queue_attack_npc(
                        &rc->human_input,
                        rc->human_input.pending_target_idx);
                }
                human_set_click_cross(&rc->human_input, cm->click_screen_x, cm->click_screen_y, 1);
            }
            break;
        }

        case CMENU_ACTION_GUI_INVENTORY_PRIMARY: {
            Player* p = rc->entity_count > 0 && rc->gui.gui_entity_idx < rc->entity_count
                ? render_get_player_ptr(env, rc->gui.gui_entity_idx)
                : NULL;
            if (p) {
                gui_inv_click(&rc->gui, p, item->inventory_slot, &rc->human_input);
            }
            break;
        }

        case CMENU_ACTION_GUI_INVENTORY_USE:
            gui_inv_select_item(&rc->gui, &rc->human_input, item->inventory_slot);
            break;

        case CMENU_ACTION_GUI_PRAYER: {
            Player* p = rc->entity_count > 0 && rc->gui.gui_entity_idx < rc->entity_count
                ? render_get_player_ptr(env, rc->gui.gui_entity_idx)
                : NULL;
            if (item->prayer_idx >= 0 &&
                    rc->human_input.cursor_mode != CURSOR_NORMAL) {
                human_apply_selected_target_to_widget(
                    &rc->human_input,
                    osrs_ui_intent_widget_component_id(
                        OSRS_UI_GROUP_PRAYERBOOK, item->prayer_idx));
                break;
            }
            if (p && item->prayer_idx >= 0) {
                human_apply_prayer_idx(&rc->human_input, p, (GuiPrayerIdx)item->prayer_idx);
            }
            break;
        }

        case CMENU_ACTION_GUI_SPELL:
            if (item->spell_idx >= 0) {
                if (rc->human_input.cursor_mode != CURSOR_NORMAL) {
                    human_apply_selected_target_to_widget(
                        &rc->human_input,
                        osrs_ui_intent_widget_component_id(
                            OSRS_UI_GROUP_MAGIC_SPELLBOOK, item->spell_idx));
                    break;
                }
                human_select_spell_idx(&rc->human_input, (GuiSpellIdx)item->spell_idx);
            }
            break;

        case CMENU_ACTION_GUI_COMBAT_STYLE: {
            Player* p = rc->entity_count > 0 && rc->gui.gui_entity_idx < rc->entity_count
                ? render_get_player_ptr(env, rc->gui.gui_entity_idx)
                : NULL;
            if (p && item->fight_style >= 0) {
                if (rc->human_input.cursor_mode != CURSOR_NORMAL) {
                    human_apply_selected_target_to_widget(
                        &rc->human_input,
                        osrs_ui_intent_widget_component_id(
                            OSRS_UI_GROUP_COMBAT_INTERFACE, item->fight_style));
                    break;
                }
                human_apply_combat_style(
                    &rc->human_input,
                    &rc->gui,
                    p,
                    (FightStyle)item->fight_style);
            }
            break;
        }

        case CMENU_ACTION_GUI_AUTOCAST: {
            Player* p = rc->entity_count > 0 && rc->gui.gui_entity_idx < rc->entity_count
                ? render_get_player_ptr(env, rc->gui.gui_entity_idx)
                : NULL;
            if (p && item->autocast_spell >= 0) {
                if (rc->human_input.cursor_mode != CURSOR_NORMAL) {
                    human_apply_selected_target_to_widget(
                        &rc->human_input,
                        osrs_ui_intent_widget_component_id(
                            OSRS_UI_GROUP_COMBAT_INTERFACE, 110 + item->autocast_spell));
                    break;
                }
                human_apply_autocast_spell(
                    &rc->human_input,
                    &rc->gui,
                    p,
                    item->autocast_spell,
                    item->autocast_defensive);
            }
            break;
        }

        case CMENU_ACTION_GUI_SPEC_TOGGLE:
            if (rc->human_input.cursor_mode != CURSOR_NORMAL) {
                human_apply_selected_target_to_widget(
                    &rc->human_input,
                    osrs_ui_intent_widget_component_id(OSRS_UI_GROUP_COMBAT_INTERFACE, 120));
                break;
            }
            human_apply_spec_toggle(&rc->human_input);
            break;

        case CMENU_ACTION_LAB_SELECT_NPC: {
            int ei = item->entity_idx;
            if (ei >= 0 && ei < rc->entity_count)
                rc->lab_selected_npc_slot = rc->entities[ei].npc_slot;
            break;
        }

        case CMENU_ACTION_LAB_MOVE_SELECTED_NPC:
            if (lab_on && rc->lab_selected_npc_slot >= 0) {
                char line[96];
                snprintf(line, sizeof(line), "move_npc slot=%d x=%d y=%d",
                    rc->lab_selected_npc_slot, cm->walk_tile_x, cm->walk_tile_y);
                render_lab_apply_line(rc, env, line);
            }
            break;

        case CMENU_ACTION_LAB_PLACE_PLAYER: {
            char line[64];
            snprintf(line, sizeof(line), "player x=%d y=%d",
                cm->walk_tile_x, cm->walk_tile_y);
            render_lab_apply_line(rc, env, line);
            break;
        }

        case CMENU_ACTION_LAB_SPAWN_NPC:
            if (lab_on) {
                char line[96];
                snprintf(line, sizeof(line), "npc type=%d x=%d y=%d",
                    item->npc_type, cm->walk_tile_x, cm->walk_tile_y);
                render_lab_apply_line(rc, env, line);
                /* re-select: the just-spawned NPC is the highest active slot. */
                int newest = -1;
                for (int i = 0; i < rc->entity_count; i++) {
                    if (rc->entities[i].entity_type == ENTITY_NPC &&
                            rc->entities[i].npc_slot > newest)
                        newest = rc->entities[i].npc_slot;
                }
                rc->lab_selected_npc_slot = newest;
            }
            break;

        case CMENU_ACTION_LAB_KILL_NPC: {
            int ei = item->entity_idx;
            if (lab_on && ei >= 0 && ei < rc->entity_count) {
                char line[48];
                snprintf(line, sizeof(line), "kill_npc slot=%d",
                    rc->entities[ei].npc_slot);
                render_lab_apply_line(rc, env, line);
            }
            break;
        }

        case CMENU_ACTION_LAB_DELETE_NPC: {
            int ei = item->entity_idx;
            if (lab_on && ei >= 0 && ei < rc->entity_count) {
                char line[48];
                snprintf(line, sizeof(line), "delete_npc slot=%d",
                    rc->entities[ei].npc_slot);
                render_lab_apply_line(rc, env, line);
            }
            break;
        }

        case CMENU_ACTION_LAB_TOGGLE_PILLAR: {
            /* pillars are inferno-only; toggle the current state via a lab line. */
            InfernoState* inf = render_inferno_state_from_env(env);
            if (inf && item->pillar_idx >= 0) {
                int idx = item->pillar_idx;
                int active = inf->pillars[idx].active;
                char line[64];
                snprintf(line, sizeof(line), "pillar idx=%d active=%d",
                    idx, active ? 0 : 1);
                render_lab_apply_line(rc, env, line);
            }
            break;
        }

        case CMENU_ACTION_CANCEL:
        case CMENU_ACTION_NONE:
            break;
    }

    context_menu_dismiss(cm);
}

/** Draw the context menu as a 2D overlay. Call just before EndDrawing().
    OSRS style: dark brown/black rectangle, white text, yellow highlight on hover. */
static void context_menu_draw(RenderClient* rc) {
    ContextMenu* cm = &rc->context_menu;
    if (!cm->visible || cm->item_count == 0) return;

    int mx = GetMouseX();
    int my = GetMouseY();
    int menu_h = context_menu_height(cm);

    cm->hover_idx = context_menu_row_at(cm, mx, my);

    Color bg = (Color){53, 44, 31, 244};
    Color border = (Color){170, 137, 72, 255};
    Color row_bg = (Color){28, 23, 17, 215};
    Color hover_bg = (Color){74, 60, 38, 235};
    Color title_color = (Color){255, 255, 0, 255};
    Color text_normal = (Color){255, 152, 31, 255};
    Color text_hover = (Color){255, 255, 0, 255};

    DrawRectangle(cm->screen_x, cm->screen_y, cm->width, menu_h, bg);
    DrawRectangleLinesEx(
        (Rectangle){
            (float)cm->screen_x,
            (float)cm->screen_y,
            (float)cm->width,
            (float)menu_h,
        },
        1.0f,
        border
    );
    context_menu_draw_text_shadow(
        &rc->gui,
        "Choose Option", cm->screen_x + 5, cm->screen_y + 4, 11, title_color);

    for (int i = 0; i < cm->item_count; i++) {
        int iy = cm->screen_y + CONTEXT_MENU_ITEM_TOP + i * CONTEXT_MENU_ROW_H;
        Color bg_color = (i == cm->hover_idx) ? hover_bg : row_bg;
        DrawRectangle(
            cm->screen_x + CONTEXT_MENU_PADDING,
            iy,
            cm->width - CONTEXT_MENU_PADDING * 2,
            CONTEXT_MENU_ITEM_H,
            bg_color
        );
        Color tc = (i == cm->hover_idx) ? text_hover : text_normal;
        context_menu_draw_text_shadow(
            &rc->gui,
            cm->items[i].label,
            cm->screen_x + CONTEXT_MENU_PADDING + 4,
            iy + 3,
            11,
            tc
        );
    }
}

/** the active encounter def iff it supports the scenario lab, from the render
    client (no env handle). Encounter-neutral mirror of render_lab_def. */
static const EncounterDef* render_lab_def_from_client(RenderClient* rc) {
    if (!rc || !rc->gui.encounter_def || !rc->gui.encounter_state) return NULL;
    const EncounterDef* def = (const EncounterDef*)rc->gui.encounter_def;
    if (!def->apply_lab_command ||
            !def->snapshot_size || !def->snapshot || !def->restore)
        return NULL;
    return def;
}

static void render_lab_draw_hud(RenderClient* rc) {
    const EncounterDef* def = render_lab_def_from_client(rc);
    if (!def || !rc->lab_enabled) return;

    int selected = rc->lab_selected_npc_slot;
    char selected_text[64] = "none";
    for (int i = 0; i < rc->entity_count; i++) {
        if (rc->entities[i].entity_type == ENTITY_NPC &&
                rc->entities[i].npc_slot == selected) {
            snprintf(selected_text, sizeof(selected_text), "%d %s", selected,
                render_entity_display_name(&rc->entities[i]));
            break;
        }
    }
    int is_inferno = strcmp(def->name, "inferno") == 0;
    char title[48];
    snprintf(title, sizeof(title), "%s LAB", def->name);
    for (char* c = title; *c; c++) *c = (char)toupper((unsigned char)*c);

    DrawRectangle(8, 8, 650, 58, CLITERAL(Color){ 15, 10, 18, 220 });
    DrawRectangleLines(8, 8, 650, 58, CLITERAL(Color){ 190, 80, 255, 255 });
    DrawText(title, 16, 14, 16, CLITERAL(Color){ 230, 210, 255, 255 });
    if (is_inferno) {
        DrawText(TextFormat("F8 commit  F6 restore  F7 forecast %s  F9 dump  selected: %s",
            rc->lab_show_forecast ? "on" : "off", selected_text),
            16, 38, 12, CLITERAL(Color){ 230, 230, 230, 255 });
    } else {
        DrawText(TextFormat("F8 commit  F6 restore  F9 dump  selected: %s",
            selected_text),
            16, 38, 12, CLITERAL(Color){ 230, 230, 230, 255 });
    }
}

static void render_draw_encounter_status_text(RenderClient* rc) {
    EncounterOverlay* ov = &rc->encounter_overlay;
    if (!ov->status_text_active || ov->status_text[0] == '\0') return;

    int font_size = 22;
    int text_w = MeasureText(ov->status_text, font_size);
    int pad_x = 12;
    int pad_y = 6;
    int box_w = text_w + pad_x * 2;
    int box_h = font_size + pad_y * 2;
    int x = (RENDER_WINDOW_W - box_w) / 2;
    int y = 42;

    DrawRectangle(x, y, box_w, box_h, (Color){ 35, 15, 15, 220 });
    DrawRectangleLines(x, y, box_w, box_h, (Color){ 190, 65, 45, 255 });
    DrawText(ov->status_text, x + pad_x + 1, y + pad_y + 1,
        font_size, (Color){ 0, 0, 0, 180 });
    DrawText(ov->status_text, x + pad_x, y + pad_y,
        font_size, (Color){ 255, 220, 190, 255 });
}


static RenderClient* render_make_client(void) {
    osrs_asset_require_group(OSRS_ASSET_GROUP_CORE);
    osrs_asset_require_group(OSRS_ASSET_GROUP_GUI);

    RenderClient* rc = (RenderClient*)calloc(1, sizeof(RenderClient));
    rc->ticks_per_second = RENDER_DEFAULT_GAME_TICKS_PER_SECOND;
    rc->last_tick_time = 0.0;
    rc->model_scale = 0.15f;  /* ~20px tile / ~150 model units */
    rc->zoom = 1.0f;
    rc->arena_base_x = FIGHT_AREA_BASE_X;
    rc->arena_base_y = FIGHT_AREA_BASE_Y;
    rc->arena_width = FIGHT_AREA_WIDTH;
    rc->arena_height = FIGHT_AREA_HEIGHT;
    rc->show_safe_spots = 0;
    rc->show_debug = 0;
    rc->lab_enabled = 0;
    rc->lab_show_forecast = 0;
    rc->lab_selected_npc_slot = -1;
    rc->lab_prev_paused = 0;
    rc->lab_prev_human_enabled = 0;
    rc->lab_entry_snapshot = NULL;
    rc->lab_entry_snapshot_size = 0;
    rc->lab_restore_requested = 0;
    rc->lab_restore_generation = 0;
    rc->layout_mode = 1;  /* default to resizable mode (modern OSRS layout) */
    rc->cam_yaw = 0.0f;
    rc->cam_pitch = 0.6f;    /* ~34 degrees, similar to OSRS default */
    rc->cam_dist = 40.0f;
    /* fight area center (Z negated: OSRS +Y = north maps to -Z) */
    rc->cam_target_x = (float)rc->arena_base_x + (float)rc->arena_width / 2.0f;
    rc->cam_target_z = -((float)rc->arena_base_y + (float)rc->arena_height / 2.0f);
    rc->history_capacity = RENDER_HISTORY_INITIAL_CAPACITY;
    rc->history = (OsrsEnv*)calloc((size_t)rc->history_capacity, sizeof(OsrsEnv));
    if (!rc->history) {
        fprintf(stderr, "render history allocation failed\n");
        abort();
    }
    rc->history_count = 0;
    rc->history_cursor = -1;  /* -1 = live (not rewinding) */
    rc->entity_count = 0;  /* populated by render_populate_entities */
    rc->prev_entity_count = 0;
    rc->hover_tile_x = -1;
    rc->hover_tile_y = -1;
    for (int i = 0; i < MAX_RENDER_ENTITIES; i++) {
        rc->anim[i].primary_seq_id = -1;
        rc->anim[i].secondary_seq_id = ANIM_SEQ_IDLE;
        rc->primary_event_tick[i] = -1;
        rc->last_primary_event_tick[i] = -2;
        rc->prev_npc_slot[i] = -1;
    }

    InitWindow(RENDER_WINDOW_W, RENDER_WINDOW_H, "OSRS PvP Debug Viewer");
    SetTargetFPS(60);

    /* load overhead prayer icon textures from exported sprites.
       OSRS headIcon index: 0=melee, 1=ranged, 2=magic, 3=retribution, 4=smite, 5=redemption */
    {
        const char* paths[] = {
            OSRS_ASSET("sprites/gui/headicons_prayer_0.png"),
            OSRS_ASSET("sprites/gui/headicons_prayer_1.png"),
            OSRS_ASSET("sprites/gui/headicons_prayer_2.png"),
            OSRS_ASSET("sprites/gui/headicons_prayer_3.png"),
            OSRS_ASSET("sprites/gui/headicons_prayer_4.png"),
            OSRS_ASSET("sprites/gui/headicons_prayer_5.png"),
        };
        rc->prayer_icons_loaded = 1;
        for (int i = 0; i < 6; i++) {
            if (osrs_asset_exists(paths[i])) {
                rc->prayer_icons[i] = osrs_asset_load_texture(paths[i]);
            } else {
                rc->prayer_icons_loaded = 0;
            }
        }
    }

    /* load hitsplat sprite textures (317 classic: hitmarks_0..4.png) */
    {
        rc->hitmark_sprites_loaded = 1;
        for (int i = 0; i < 5; i++) {
            const char* path = TextFormat(OSRS_ASSET("sprites/gui/hitmarks_%d.png"), i);
            if (osrs_asset_exists(path)) {
                rc->hitmark_sprites[i] = osrs_asset_load_texture(path);
            } else {
                rc->hitmark_sprites_loaded = 0;
            }
        }
    }

    /* load click cross sprite textures (4 yellow + 4 red animation frames) */
    {
        static const char* cross_names[8] = {
            "cross_yellow_1", "cross_yellow_2", "cross_yellow_3", "cross_yellow_4",
            "cross_red_1", "cross_red_2", "cross_red_3", "cross_red_4",
        };
        rc->click_cross_loaded = 1;
        for (int i = 0; i < 8; i++) {
            const char* path = TextFormat(OSRS_ASSET("sprites/gui/%s.png"), cross_names[i]);
            if (osrs_asset_exists(path)) {
                rc->click_cross_sprites[i] = osrs_asset_load_texture(path);
            } else {
                rc->click_cross_loaded = 0;
            }
        }
    }

    rc->debug_hit_wx = -1;
    rc->debug_hit_wy = -1;

    /* initialize GUI panel system */
    rc->gui.active_tab = GUI_TAB_INVENTORY;
    /* OSRS resizable layout: minimap chrome anchored to top-right corner,
       inventory/tabs panel anchored to bottom-right corner. they share the
       same column (panel_x..panel_x+panel_w) but are separate blocks; the
       game viewport extends through the right column between them. */
    rc->gui.panel_x = RENDER_GRID_W;
    rc->gui.panel_w = RENDER_PANEL_WIDTH;
    /* tabs+content panel anchored to bottom of window */
    rc->gui.panel_h = RENDER_TAB_ROW_H + RENDER_PANEL_CONTENT_H + RENDER_TAB_ROW_H;
    rc->gui.panel_y = RENDER_WINDOW_H - rc->gui.panel_h;
    rc->gui.tab_h = RENDER_TAB_ROW_H;
    rc->gui.status_bar_h = 0;  /* minimap is a separate block at top-right */
    rc->gui.gui_entity_idx = 0;
    rc->gui.gui_entity_count = 0;

    /* inventory interaction state */
    gui_reset_inventory_ui_state(&rc->gui);

    /* human input control */
    human_input_init(&rc->human_input);

    /* context menu (calloc zeroes everything, just set hover_idx sentinel) */
    rc->context_menu.hover_idx = -1;

    /* load GUI sprites from exported cache data */
    gui_load_sprites(&rc->gui);

    return rc;
}

/**
 * Build a static raylib Model from a cached OsrsModel.
 * Copies expanded vertex + color data into a new Mesh and uploads to GPU.
 * Returns 1 on success, 0 if model not found.
 */
static int render_build_static_model(ModelCache* cache, uint32_t model_id, Model* out) {
    OsrsModel* om = model_cache_get(cache, model_id);
    if (!om || om->mesh.vertexCount == 0) return 0;

    Mesh mesh = { 0 };
    mesh.vertexCount = om->mesh.vertexCount;
    mesh.triangleCount = om->mesh.triangleCount;
    mesh.vertices = (float*)RL_MALLOC(mesh.vertexCount * 3 * sizeof(float));
    mesh.colors = (unsigned char*)RL_MALLOC(mesh.vertexCount * 4);
    memcpy(mesh.vertices, om->mesh.vertices, mesh.vertexCount * 3 * sizeof(float));
    memcpy(mesh.colors, om->mesh.colors, mesh.vertexCount * 4);

    UploadMesh(&mesh, false);
    *out = LoadModelFromMesh(mesh);
    return 1;
}

static void render_load_projectile_assets(RenderClient* rc) {
    if (!rc->spotanims) {
        rc->spotanims = osrs_spotanims_load(OSRS_ASSET("spotanims.bin"));
    }
    if (!rc->projectile_model_cache) {
        rc->projectile_model_cache = model_cache_load(OSRS_ASSET("projectiles.models"));
    }
}

/** Lazily load and cache an explicit projectile model by GFX model ID.
 *  Searches projectile, equipment, and NPC model caches. model_id 0 means the
 *  caller intentionally wants style-based fallback; missing explicit models
 *  abort so backend/render drift fails loudly. */
static Model* render_get_proj_model(RenderClient* rc, uint32_t model_id) {
    if (model_id == 0) return NULL;
    for (int i = 0; i < rc->proj_model_count; i++) {
        if (rc->proj_models[i].id == model_id)
            return rc->proj_models[i].ready ? &rc->proj_models[i].model : NULL;
    }
    if (rc->proj_model_count >= MAX_PROJ_MODELS) {
        fprintf(stderr, "render: projectile model cache exhausted while loading model %u\n",
                model_id);
        abort();
    }
    int idx = rc->proj_model_count++;
    rc->proj_models[idx].id = model_id;
    rc->proj_models[idx].ready = render_build_static_model(
        rc->projectile_model_cache, model_id, &rc->proj_models[idx].model);
    if (!rc->proj_models[idx].ready) {
        rc->proj_models[idx].ready = render_build_static_model(
            rc->model_cache, model_id, &rc->proj_models[idx].model);
    }
    if (!rc->proj_models[idx].ready && rc->npc_model_cache) {
        rc->proj_models[idx].ready = render_build_static_model(
            rc->npc_model_cache, model_id, &rc->proj_models[idx].model);
    }
    if (!rc->proj_models[idx].ready) {
        fprintf(stderr, "render: explicit projectile model %u is missing from loaded caches\n",
                model_id);
        abort();
    }
    return &rc->proj_models[idx].model;
}

static OsrsModel* render_get_projectile_osrs_model(RenderClient* rc, uint32_t model_id) {
    if (model_id == 0) return NULL;
    OsrsModel* om = model_cache_get(rc->projectile_model_cache, model_id);
    if (!om) om = model_cache_get(rc->model_cache, model_id);
    if (!om && rc->npc_model_cache)
        om = model_cache_get(rc->npc_model_cache, model_id);
    if (!om) {
        fprintf(stderr, "render: explicit projectile model %u is missing from loaded caches\n",
                model_id);
        abort();
    }
    return om;
}

static const OsrsSpotAnimDef* render_require_travel_spotanim(
    RenderClient* rc,
    int travel_gfx_id
) {
    const OsrsSpotAnimDef* meta = osrs_spotanim_find(rc->spotanims, travel_gfx_id);
    if (!meta || meta->model_id < 0) {
        fprintf(stderr, "render: travel spotanim %d is missing a model\n",
                travel_gfx_id);
        abort();
    }
    return meta;
}

static OsrsModel* render_get_travel_spotanim_osrs_model(
    RenderClient* rc,
    int travel_gfx_id
) {
    const OsrsSpotAnimDef* meta = render_require_travel_spotanim(rc, travel_gfx_id);
    OsrsModel* om = effect_find_model(
        meta, rc->model_cache, rc->npc_model_cache, rc->projectile_model_cache);
    if (!om) {
        fprintf(stderr, "render: travel spotanim %d model %d is missing from loaded caches\n",
                travel_gfx_id, meta->model_id);
        abort();
    }
    return om;
}

static OsrsModel* render_get_flight_osrs_model(
    RenderClient* rc,
    const FlightProjectile* fp
) {
    if (fp->travel_gfx_drives_model)
        return render_get_travel_spotanim_osrs_model(rc, fp->travel_gfx_id);
    return render_get_projectile_osrs_model(rc, fp->model_id);
}

static int render_projectile_anim_has_dynamic_frames(RenderClient* rc, int anim_id) {
    if (anim_id < 0) return 0;
    AnimSequence* seq = render_get_anim_sequence(rc, (uint16_t)anim_id);
    if (!seq || seq->frame_count <= 0) {
        fprintf(stderr, "render: projectile animation %d is missing\n", anim_id);
        abort();
    }
    return !(seq->frame_count == 1 &&
             (seq->frames[0].delay == 0 || seq->frames[0].delay >= 0x8000u));
}

static AnimModelState* render_create_projectile_anim_state_from_model(
    OsrsModel* om,
    uint32_t model_id,
    int anim_id
) {
    if (anim_id < 0 || !om) return NULL;
    if (!om->vertex_skins || om->base_vert_count == 0) {
        fprintf(stderr, "render: projectile model %u cannot play animation %d\n",
                model_id, anim_id);
        abort();
    }
    return anim_model_state_create_with_face_alpha(
        om->vertex_skins, om->base_vert_count,
        om->face_alpha_labels, om->base_face_alphas, om->mesh.triangleCount);
}

/**
 * Build all overlay models (clouds, projectiles, snakelings) from the model cache.
 * Call after model_cache is loaded.
 */
static void render_init_overlay_models(RenderClient* rc) {
    if (!rc->model_cache) return;

    rc->cloud_model_ready = render_build_static_model(
        rc->model_cache, GFX_TOXIC_CLOUD_MODEL, &rc->cloud_model);
    rc->snakeling_model_ready = render_build_static_model(
        rc->model_cache, SNAKELING_MODEL_ID, &rc->snakeling_model);
    rc->ranged_proj_model_ready = render_build_static_model(
        rc->model_cache, GFX_RANGED_PROJ_MODEL, &rc->ranged_proj_model);
    rc->magic_proj_model_ready = render_build_static_model(
        rc->model_cache, GFX_MAGIC_PROJ_MODEL, &rc->magic_proj_model);

    rc->cloud_proj_model_ready = render_build_static_model(
        rc->model_cache, GFX_CLOUD_PROJ_MODEL, &rc->cloud_proj_model);
    {
        uint32_t pillar_ids[4] = { INF_PILLAR_MODEL_100, INF_PILLAR_MODEL_75,
                                    INF_PILLAR_MODEL_50, INF_PILLAR_MODEL_25 };
        rc->pillar_models_ready = 1;
        for (int i = 0; i < 4; i++) {
            if (!render_build_static_model(rc->model_cache, pillar_ids[i], &rc->pillar_models[i]))
                rc->pillar_models_ready = 0;
        }
    }

    if (rc->cloud_model_ready) printf("overlay: cloud model loaded\n");
    if (rc->pillar_models_ready) printf("overlay: pillar models loaded (4 HP levels)\n");
    if (rc->snakeling_model_ready) printf("overlay: snakeling model loaded\n");
    if (rc->ranged_proj_model_ready) printf("overlay: ranged projectile model loaded\n");
    if (rc->magic_proj_model_ready) printf("overlay: magic projectile model loaded\n");
    if (rc->cloud_proj_model_ready) printf("overlay: cloud projectile model loaded\n");
}


/**
 * Spawn a flight projectile with OSRS-accurate parabolic arc and target tracking.
 *
 * Matches Projectile.java setDestination():
 *   - position re-computed each sub-tick toward current target
 *   - yaw/pitch updated from velocity vector each tick
 *   - height follows parabolic arc with quadratic correction
 */
static void flight_deactivate(FlightProjectile* fp) {
    if (fp->anim_state) {
        anim_model_state_free(fp->anim_state);
        fp->anim_state = NULL;
    }
    fp->active = 0;
}

static void flight_clear_all(RenderClient* rc) {
    for (int i = 0; i < MAX_FLIGHT_PROJECTILES; i++) {
        flight_deactivate(&rc->flights[i]);
    }
}

static void flight_finish(RenderClient* rc, FlightProjectile* fp) {
    if (fp->impact_gfx_id > 0) {
        effect_spawn_spotanim_subtile(
            rc->effects, fp->impact_gfx_id,
            osrs_projectile_subtile_from_anchor_coord(fp->dst_x),
            osrs_projectile_subtile_from_anchor_coord(fp->dst_y),
            rc->effect_client_tick_counter + 1,
            rc->spotanims, rc->anim_cache, rc->model_cache,
            rc->npc_model_cache, rc->projectile_model_cache);
    }
    flight_deactivate(fp);
}

static int render_find_npc_entity_idx(const RenderClient* rc, int npc_slot) {
    for (int i = 0; i < rc->entity_count; i++) {
        const RenderEntity* entity = &rc->entities[i];
        if (entity->entity_type == ENTITY_NPC &&
                entity->npc_slot == npc_slot &&
                entity->npc_visible) {
            return i;
        }
    }
    return -1;
}

static int render_resolve_projectile_anchor(
    const RenderClient* rc,
    int anchor_kind,
    int npc_slot,
    float fixed_x,
    float fixed_y,
    float* out_x,
    float* out_y
) {
    int entity_idx = -1;
    switch (anchor_kind) {
        case ENCOUNTER_PROJECTILE_TARGET_FIXED:
            *out_x = fixed_x;
            *out_y = fixed_y;
            return 1;
        case ENCOUNTER_PROJECTILE_TARGET_PLAYER:
            entity_idx = rc->entity_count > 0 ? 0 : -1;
            break;
        case ENCOUNTER_PROJECTILE_TARGET_NPC_SLOT:
            entity_idx = render_find_npc_entity_idx(rc, npc_slot);
            break;
        default:
            fprintf(stderr, "render: unknown projectile anchor kind %d\n", anchor_kind);
            abort();
    }

    if (entity_idx < 0) return 0;
    *out_x = osrs_projectile_anchor_coord_from_subtile(rc->sub_x[entity_idx]);
    *out_y = osrs_projectile_anchor_coord_from_subtile(rc->sub_y[entity_idx]);
    return 1;
}

static int render_resolve_flight_target_anchor(
    const RenderClient* rc,
    const FlightProjectile* fp,
    float* out_x,
    float* out_y
) {
    return render_resolve_projectile_anchor(
        rc, fp->target_kind, fp->target_npc_slot,
        fp->dst_x, fp->dst_y, out_x, out_y);
}

static void flight_update_live_destination(RenderClient* rc, FlightProjectile* fp) {
    if (!fp->tracks_target) return;
    float x = 0.0f;
    float y = 0.0f;
    if (render_resolve_flight_target_anchor(rc, fp, &x, &y)) {
        fp->dst_x = x;
        fp->dst_y = y;
    }
}

static void flight_spawn(RenderClient* rc,
                         float src_x, float src_y, float dst_x, float dst_y,
                         int style, int damage,
                         int duration_ticks, int start_h, int end_h, int curve,
                         float arc_height, int tracks_target, uint32_t model_id,
                         int anim_id, int travel_gfx_id,
                         int launch_gfx_id, int impact_gfx_id,
                         int start_delay, int motion_mode,
                         float offset_x, float offset_y, float offset_z,
                         int source_kind, int source_npc_slot,
                         int target_kind, int target_npc_slot) {
    int slot = -1;
    for (int i = 0; i < MAX_FLIGHT_PROJECTILES; i++) {
        if (!rc->flights[i].active) { slot = i; break; }
    }
    if (slot < 0) {
        fprintf(stderr, "osrs render: flight projectile overflow at %d slots\n",
            MAX_FLIGHT_PROJECTILES);
        abort();
    }

    FlightProjectile* fp = &rc->flights[slot];
    memset(fp, 0, sizeof(FlightProjectile));
    fp->active = 1;
    fp->src_x = src_x;
    fp->src_y = src_y;
    fp->dst_x = dst_x;
    fp->dst_y = dst_y;
    fp->x = src_x;
    fp->y = src_y;
    fp->progress = 0.0f;
    fp->speed = 1.0f / (float)duration_ticks;
    fp->start_height = (float)start_h / 128.0f;
    fp->end_height = (float)end_h / 128.0f;
    fp->curve = (float)curve;
    fp->style = style;
    fp->damage = damage;
    fp->arc_height = arc_height;
    fp->tracks_target = tracks_target;
    fp->source_kind = source_kind;
    fp->source_npc_slot = source_npc_slot;
    fp->target_kind = target_kind;
    fp->target_npc_slot = target_npc_slot;
    fp->model_id = model_id;
    fp->anim_id = anim_id;
    fp->travel_gfx_id = travel_gfx_id;
    fp->travel_gfx_drives_model = (model_id == 0 && travel_gfx_id > 0);
    if (travel_gfx_id > 0) {
        const OsrsSpotAnimDef* meta = render_require_travel_spotanim(rc, travel_gfx_id);
        if (fp->travel_gfx_drives_model) {
            fp->model_id = (uint32_t)meta->model_id;
            if (fp->anim_id < 0)
                fp->anim_id = meta->animation_id;
        }
        if (!render_projectile_anim_has_dynamic_frames(rc, fp->anim_id))
            fp->anim_id = -1;
    }
    fp->anim_frame = 0;
    fp->anim_tick_counter = 0;
    fp->anim_state = render_create_projectile_anim_state_from_model(
        render_get_flight_osrs_model(rc, fp), fp->model_id, fp->anim_id);
    fp->launch_gfx_id = launch_gfx_id;
    fp->impact_gfx_id = impact_gfx_id;
    fp->start_delay = start_delay;
    fp->motion_mode = motion_mode;
    fp->offset_x = offset_x;
    fp->offset_y = offset_y;
    fp->offset_z = offset_z;
    flight_update_live_destination(rc, fp);

    /* height arc: OSRS SceneProjectile.calculateIncrements
       skip quadratic computation when using sinusoidal arc */
    float dx = dst_x - src_x, dy = dst_y - src_y;
    float dist = sqrtf(dx * dx + dy * dy);
    if (dist < 0.01f) dist = 1.0f;
    if (arc_height > 0.0f) {
        fp->height_vel = 0.0f;
        fp->height_accel = 0.0f;
    } else {
        fp->height_vel = -dist * tanf(curve * PROJ_OSRS_SLOPE_TO_RAD);
        fp->height_accel = 2.0f * (fp->end_height - fp->start_height - fp->height_vel);
    }

    float h0 = osrs_projectile_height_at_progress(0.0f,
        fp->start_height, fp->end_height, fp->arc_height,
        fp->height_vel, fp->height_accel);
    float h1 = osrs_projectile_height_at_progress(fp->speed,
        fp->start_height, fp->end_height, fp->arc_height,
        fp->height_vel, fp->height_accel);
    OsrsProjectileOrientation orientation = osrs_projectile_orientation_from_step(
        dx * fp->speed, dy * fp->speed, h1 - h0);
    fp->yaw = orientation.yaw;
    fp->pitch = orientation.pitch;
}

static void flight_advance_animation(RenderClient* rc, FlightProjectile* fp) {
    if (fp->anim_id < 0) return;
    AnimSequence* seq = render_get_anim_sequence(rc, (uint16_t)fp->anim_id);
    if (!seq || seq->frame_count <= 0) {
        fprintf(stderr, "render: projectile animation %d is missing\n", fp->anim_id);
        abort();
    }
    fp->anim_tick_counter++;
    while (fp->anim_tick_counter >= seq->frames[fp->anim_frame].delay) {
        fp->anim_tick_counter -= seq->frames[fp->anim_frame].delay;
        fp->anim_frame++;
        if (fp->anim_frame >= seq->frame_count) {
            fp->anim_frame = 0;
        }
    }
}

static inline Matrix render_projectile_transform(
    float scale_x, float scale_y, float scale_z,
    float yaw, float pitch, Vector3 position
) {
    Matrix transform = MatrixScale(-scale_x, scale_y, scale_z);
    transform = MatrixMultiply(
        transform,
        MatrixMultiply(MatrixRotateX(pitch), MatrixRotateY(yaw)));
    transform = MatrixMultiply(transform, MatrixTranslate(position.x, position.y, position.z));
    return transform;
}

static inline Matrix render_projectile_transform_offset(
    float scale_x, float scale_y, float scale_z,
    float yaw, float pitch, Vector3 position,
    float offset_x, float offset_y, float offset_z
) {
    Matrix transform = MatrixScale(-scale_x, scale_y, scale_z);
    transform = MatrixMultiply(
        transform, MatrixTranslate(offset_x, offset_z, offset_y));
    transform = MatrixMultiply(
        transform,
        MatrixMultiply(MatrixRotateX(pitch), MatrixRotateY(yaw)));
    transform = MatrixMultiply(transform, MatrixTranslate(position.x, position.y, position.z));
    return transform;
}

static inline void flight_update_target_anchored_position(
    RenderClient* rc,
    FlightProjectile* fp
) {
    float old_x = fp->x;
    float old_y = fp->y;
    float target_x = 0.0f;
    float target_y = 0.0f;
    if (render_resolve_flight_target_anchor(rc, fp, &target_x, &target_y)) {
        fp->x = target_x;
        fp->y = target_y;
    } else {
        fp->x = fp->dst_x;
        fp->y = fp->dst_y;
    }
    OsrsProjectileOrientation orientation =
        osrs_projectile_orientation_from_step(fp->x - old_x, fp->y - old_y, 0.0f);
    fp->yaw = orientation.yaw;
    fp->pitch = orientation.pitch;
}

static inline RangedSpecWeapon render_pvp_ranged_spec_weapon_for_item(uint8_t weapon_db_idx) {
    switch (weapon_db_idx) {
        case ITEM_DARK_BOW:           return RANGED_SPEC_DARK_BOW;
        case ITEM_HEAVY_BALLISTA:     return RANGED_SPEC_BALLISTA;
        case ITEM_ARMADYL_CROSSBOW:   return RANGED_SPEC_ACB;
        case ITEM_ZARYTE_CROSSBOW:    return RANGED_SPEC_ZCB;
        case ITEM_MAGIC_SHORTBOW_I:   return RANGED_SPEC_MSB;
        case ITEM_MORRIGANS_JAVELIN:  return RANGED_SPEC_MORRIGANS;
        default:                      return RANGED_SPEC_NONE;
    }
}

static inline int render_pvp_distance_to_target(
    const RenderEntity* attacker, const RenderEntity* target
) {
    int target_size = target->entity_type == ENTITY_NPC && target->npc_size > 1
        ? target->npc_size : 1;
    return encounter_dist_to_npc(
        attacker->x, attacker->y, target->x, target->y, target_size);
}

/**
 * Advance all active flights by one client tick (20ms).
 *
 * Matches OSRS Projectile.setDestination() tracking:
 *   remaining = (cycleEnd - currentCycle)
 *   vel = (target - current) / remaining
 *   orientation = atan2(vel_x, vel_y)
 *   pitch = atan2(height_vel, horiz_speed)
 */
static void flight_client_tick(RenderClient* rc) {
    for (int i = 0; i < MAX_FLIGHT_PROJECTILES; i++) {
        FlightProjectile* fp = &rc->flights[i];
        if (!fp->active) continue;

        /* start delay: count down before projectile becomes visible/moves */
        if (fp->start_delay > 0) {
            fp->start_delay--;
            if (fp->start_delay == 0 &&
                fp->motion_mode == ENCOUNTER_PROJECTILE_MOTION_TARGET_ANCHORED) {
                flight_update_target_anchored_position(rc, fp);
            }
            continue;
        }

        if (fp->motion_mode == ENCOUNTER_PROJECTILE_MOTION_TARGET_ANCHORED) {
            flight_update_target_anchored_position(rc, fp);
            flight_advance_animation(rc, fp);
            fp->progress += fp->speed;
            if (fp->progress >= 1.0f) {
                flight_finish(rc, fp);
            }
            continue;
        }

        float start_progress = fp->progress;
        float start_height = osrs_projectile_height_at_progress(start_progress,
            fp->start_height, fp->end_height, fp->arc_height,
            fp->height_vel, fp->height_accel);
        float old_x = fp->x;
        float old_y = fp->y;

        /* remaining sub-ticks (avoid div by zero) */
        float remaining = (1.0f - fp->progress) / fp->speed;
        if (remaining < 0.5f) remaining = 0.5f;

        flight_update_live_destination(rc, fp);

        /* re-aim velocity toward current target (OSRS tracking) */
        float vx = (fp->dst_x - fp->x) / remaining;
        float vy = (fp->dst_y - fp->y) / remaining;

        fp->x += vx;
        fp->y += vy;

        float next_progress = start_progress + fp->speed;
        float end_height = osrs_projectile_height_at_progress(next_progress,
            fp->start_height, fp->end_height, fp->arc_height,
            fp->height_vel, fp->height_accel);
        OsrsProjectileOrientation orientation = osrs_projectile_orientation_from_step(
            fp->x - old_x, fp->y - old_y, end_height - start_height);
        fp->yaw = orientation.yaw;
        fp->pitch = orientation.pitch;

        flight_advance_animation(rc, fp);
        fp->progress += fp->speed;
        if (fp->progress >= 1.0f) {
            flight_finish(rc, fp);
        }
    }
}

/**
 * Get the interpolated world position of a flight projectile.
 */
static Vector3 flight_get_position(const FlightProjectile* fp, float src_ground, float dst_ground) {
    if (fp->motion_mode == ENCOUNTER_PROJECTILE_MOTION_TARGET_ANCHORED) {
        return (Vector3){ fp->x + 0.5f, dst_ground + fp->end_height,
            -(fp->y + 1.0f) + 0.5f };
    }

    float t = fp->progress;
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;

    float ground = src_ground + (dst_ground - src_ground) * t;
    float h = osrs_projectile_height_at_progress(t,
        fp->start_height, fp->end_height, fp->arc_height,
        fp->height_vel, fp->height_accel);

    return (Vector3){ fp->x + 0.5f, ground + h, -(fp->y + 1.0f) + 0.5f };
}

static void __attribute__((unused)) render_destroy_client(RenderClient* rc) {
    flight_clear_all(rc);
    if (rc->minimap_surface.id != 0) {
        UnloadRenderTexture(rc->minimap_surface);
    }
    /* free GUI panel sprites */
    gui_unload_sprites(&rc->gui);
    /* free prayer icon textures */
    if (rc->prayer_icons_loaded) {
        for (int i = 0; i < 6; i++) {
            UnloadTexture(rc->prayer_icons[i]);
        }
    }
    for (int mod = 0; mod < COLO_NUM_REAL_MODIFIERS; mod++) {
        for (int tier = 0; tier < 3; tier++) {
            if (rc->colosseum_modifier_icons[mod][tier].id != 0)
                UnloadTexture(rc->colosseum_modifier_icons[mod][tier]);
        }
    }
    /* free hitsplat sprite textures */
    if (rc->hitmark_sprites_loaded) {
        for (int i = 0; i < 5; i++) {
            UnloadTexture(rc->hitmark_sprites[i]);
        }
    }
    /* free click cross sprite textures */
    if (rc->click_cross_loaded) {
        for (int i = 0; i < 8; i++) {
            UnloadTexture(rc->click_cross_sprites[i]);
        }
    }
    /* free overlay models */
    if (rc->cloud_model_ready) UnloadModel(rc->cloud_model);
    if (rc->molten_model_ready) UnloadModel(rc->molten_model);
    if (rc->snakeling_model_ready) UnloadModel(rc->snakeling_model);
    if (rc->ranged_proj_model_ready) UnloadModel(rc->ranged_proj_model);
    if (rc->magic_proj_model_ready) UnloadModel(rc->magic_proj_model);
    if (rc->cloud_proj_model_ready) UnloadModel(rc->cloud_proj_model);
    if (rc->pillar_models_ready) {
        for (int i = 0; i < 4; i++) UnloadModel(rc->pillar_models[i]);
    }
    /* free dynamic projectile model cache */
    for (int i = 0; i < rc->proj_model_count; i++) {
        if (rc->proj_models[i].ready) UnloadModel(rc->proj_models[i].model);
    }
    /* free per-entity composite models */
    for (int p = 0; p < MAX_RENDER_ENTITIES; p++) {
        composite_free(&rc->composites[p]);
    }
    if (rc->model_cache) {
        model_cache_free(rc->model_cache);
        rc->model_cache = NULL;
    }
    if (rc->projectile_model_cache) {
        model_cache_free(rc->projectile_model_cache);
        rc->projectile_model_cache = NULL;
    }
    if (rc->spotanims) {
        osrs_spotanims_free(rc->spotanims);
        rc->spotanims = NULL;
    }
    if (rc->anim_cache) {
        anim_cache_free(rc->anim_cache);
        rc->anim_cache = NULL;
    }
    if (rc->terrain) {
        terrain_free(rc->terrain);
        rc->terrain = NULL;
    }
    if (rc->objects) {
        objects_free(rc->objects);
        rc->objects = NULL;
    }
    if (rc->objects_zuk) {
        objects_free(rc->objects_zuk);
        rc->objects_zuk = NULL;
    }
    if (rc->npcs) {
        objects_free(rc->npcs);
        rc->npcs = NULL;
    }
    human_input_destroy(&rc->human_input);
    CloseWindow();
    render_lab_clear_entry_snapshot(rc);
    free(rc->history);
    free(rc);
}


static Rectangle render_colosseum_draft_card_rect(int option);

static void render_handle_input(RenderClient* rc, OsrsEnv* env) {
    RenderHumanAttackCtx attack_ctx = { .rc = rc, .env = env };
    if (IsKeyPressed(KEY_SPACE))  rc->is_paused = !rc->is_paused;

    if (IsKeyPressed(KEY_RIGHT) && rc->is_paused) {
        if (rc->history_cursor >= 0) {
            /* in rewind mode: advance through history */
            if (rc->history_cursor < rc->history_count - 1) {
                rc->history_cursor++;
                rc->step_back = 1;  /* triggers restore in main loop */
            } else {
                /* restore latest snapshot then return to live */
                rc->history_cursor = rc->history_count - 1;
                rc->step_back = 1;
            }
        } else {
            rc->step_once = 1;  /* live mode: step sim forward */
        }
    }

    if (IsKeyPressed(KEY_LEFT) && rc->is_paused) {
        if (rc->history_cursor == -1 && rc->history_count > 1) {
            /* enter rewind from live: go to second-to-last snapshot */
            rc->history_cursor = rc->history_count - 2;
        } else if (rc->history_cursor > 0) {
            rc->history_cursor--;
        }
        rc->step_back = 1;
    }

    if (IsKeyPressed(KEY_TAB))    ToggleFullscreen();
    if (IsKeyPressed(KEY_C))      rc->show_collision = !rc->show_collision;
    if (IsKeyPressed(KEY_P))      rc->show_pathfinding = !rc->show_pathfinding;
    if (IsKeyPressed(KEY_M))      rc->show_models = !rc->show_models;
    if (IsKeyPressed(KEY_S))      rc->show_safe_spots = !rc->show_safe_spots;
    if (IsKeyPressed(KEY_D))      rc->show_debug = !rc->show_debug;
    if (IsKeyPressed(KEY_L))      rc->layout_mode = !rc->layout_mode;
    if (IsKeyPressed(KEY_F8)) {
        const EncounterDef* lab_def = render_lab_def(env);
        if (lab_def) {
            int enable_lab = !rc->lab_enabled;
            if (enable_lab) {
                rc->lab_prev_paused = rc->is_paused;
                rc->lab_prev_human_enabled = rc->human_input.enabled;
                render_lab_capture_entry_snapshot(rc, env);
            }
            rc->lab_enabled = enable_lab;
            rc->lab_show_forecast = rc->lab_enabled;
            rc->lab_selected_npc_slot = -1;
            context_menu_dismiss(&rc->context_menu);
            if (rc->lab_enabled) {
                rc->is_paused = 1;
                rc->human_input.enabled = 1;
                human_input_clear_pending(&rc->human_input);
                human_input_clear_move(&rc->human_input);
                render_populate_entities(rc, env);
                render_lab_snap_all_visuals(rc);
            } else {
                render_lab_restore_controls(rc);
            }
            fprintf(stderr, "%s lab: %s\n",
                lab_def->name, rc->lab_enabled ? "ON" : "OFF");
        }
    }
    if (IsKeyPressed(KEY_F6)) {
        if (render_lab_def(env) && !render_lab_restore_entry_snapshot(rc, env)) {
            fprintf(stderr, "lab: no entry snapshot to restore\n");
        }
    }
    if (IsKeyPressed(KEY_F7) && rc->lab_enabled) {
        rc->lab_show_forecast = !rc->lab_show_forecast;
    }
    if (IsKeyPressed(KEY_F9) && rc->lab_enabled) {
        if (render_lab_def(env)) render_lab_apply_line(rc, env, "dump");
    }

    float wheel = GetMouseWheelMove();

    if (!rc->human_input.enabled && IsMouseButtonDown(MOUSE_BUTTON_RIGHT)) {
        Vector2 delta = GetMouseDelta();
        rc->cam_yaw -= delta.x * 0.005f;
        rc->cam_pitch += delta.y * 0.005f;
        if (rc->cam_pitch < 0.1f) rc->cam_pitch = 0.1f;
        if (rc->cam_pitch > 1.4f) rc->cam_pitch = 1.4f;
    }
    if (IsMouseButtonDown(MOUSE_BUTTON_MIDDLE)) {
        Vector2 delta = GetMouseDelta();
        if (rc->human_input.enabled) {
            rc->cam_yaw -= delta.x * 0.005f;
            rc->cam_pitch += delta.y * 0.005f;
            if (rc->cam_pitch < 0.1f) rc->cam_pitch = 0.1f;
            if (rc->cam_pitch > 1.4f) rc->cam_pitch = 1.4f;
        } else {
            float cs = cosf(rc->cam_yaw), sn = sinf(rc->cam_yaw);
            rc->cam_target_x += (delta.x * cs + delta.y * sn) * 0.05f;
            rc->cam_target_z += (-delta.x * sn + delta.y * cs) * 0.05f;
        }
    }
    if (wheel != 0.0f) {
        rc->cam_dist *= (wheel > 0) ? (1.0f / 1.15f) : 1.15f;
        if (rc->cam_dist < 5.0f) rc->cam_dist = 5.0f;
        if (rc->cam_dist > 200.0f) rc->cam_dist = 200.0f;
    }
    if (rc->human_input.enabled && rc->entity_count > 0) {
        int eidx = rc->gui.gui_entity_idx;
        if (eidx < rc->entity_count) {
            float tx = (float)rc->sub_x[eidx] / 128.0f;
            float tz = -(float)rc->sub_y[eidx] / 128.0f;
            float dt = GetFrameTime();
            float lerp = 1.0f - powf(0.85f, dt * 60.0f);
            rc->cam_target_x += (tx - rc->cam_target_x) * lerp;
            rc->cam_target_z += (tz - rc->cam_target_z) * lerp;
        }
    }

    /* number keys 1-5: GUI tab switching */
    if (IsKeyPressed(KEY_ONE))    rc->gui.active_tab = GUI_TAB_INVENTORY;
    if (IsKeyPressed(KEY_TWO))    rc->gui.active_tab = GUI_TAB_COMBAT;
    if (IsKeyPressed(KEY_THREE))  rc->gui.active_tab = GUI_TAB_PRAYER;
    if (IsKeyPressed(KEY_FOUR))   rc->gui.active_tab = GUI_TAB_SPELLBOOK;
    if (IsKeyPressed(KEY_FIVE))   rc->gui.active_tab = GUI_TAB_EQUIPMENT;

    /* 9/0: replay speed control (discrete steps) */
    {
        static const float speed_steps[] = {
            0.5f,
            1.0f,
            RENDER_DEFAULT_GAME_TICKS_PER_SECOND,
            5.0f,
            15.0f,
            50.0f,
            0.0f,
        };
        static const int num_steps = sizeof(speed_steps) / sizeof(speed_steps[0]);
        if (IsKeyPressed(KEY_NINE) || IsKeyPressed(KEY_ZERO)) {
            /* find current step index */
            int cur = -1;
            for (int i = 0; i < num_steps; i++) {
                if (speed_steps[i] == rc->ticks_per_second) { cur = i; break; }
            }
            if (cur < 0) cur = 2; /* default to OSRS speed if not on a step */
            if (IsKeyPressed(KEY_NINE) && cur > 0)             rc->ticks_per_second = speed_steps[cur - 1];
            if (IsKeyPressed(KEY_ZERO) && cur < num_steps - 1) rc->ticks_per_second = speed_steps[cur + 1];
        }
    }

    /* H key: toggle human control */
    if (IsKeyPressed(KEY_H)) {
        rc->human_input.enabled = !rc->human_input.enabled;
        if (!rc->human_input.enabled) {
            human_input_clear_pending(&rc->human_input);
            human_input_clear_move(&rc->human_input);
            human_input_clear_selected_ui_target(&rc->human_input);
            context_menu_dismiss(&rc->context_menu);
        }
        fprintf(stderr, "human control: %s\n", rc->human_input.enabled ? "ON" : "OFF");
    }

    /* Colosseum-only human control (no inferno analog): B parries the currently-
       called Sol grapple body slot, staging a pending_* intent consumed at the
       tick boundary. The between-wave modifier draft is picked by clicking the
       modal (render_draw_colosseum_modifier_draft + its hit-test below). */
    if (rc->human_input.enabled) {
        ColosseumState* cs = render_colosseum_state_from_env(env);
        if (cs && IsKeyPressed(KEY_B) && cs->sol.grapple_active)
            rc->human_input.pending_grapple_slot = cs->sol.grapple_body_slot + 1;
    }

    /* ESC: dismiss context menu first, then cancel spell targeting */
    if (IsKeyPressed(KEY_ESCAPE)) {
        if (rc->context_menu.visible) {
            context_menu_dismiss(&rc->context_menu);
        } else if (rc->human_input.cursor_mode != CURSOR_NORMAL) {
            human_input_clear_selected_ui_target(&rc->human_input);
        }
    }

    /* GUI: G cycles viewed entity, tab clicks switch panels */
    if (IsKeyPressed(KEY_G))      gui_cycle_entity(&rc->gui);
    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        int mx = GetMouseX();
        int my = GetMouseY();
        int handled = 0;

        /* 0a. colosseum modifier draft: the mandatory between-wave pick freezes
           the world, so intercept the click first. A click on a card commits the
           pick (option o -> pending_modifier_select = o+1); clicks anywhere else
           are swallowed so nothing queues underneath the modal. */
        if (!handled && rc->human_input.enabled) {
            ColosseumState* cs = render_colosseum_state_from_env(env);
            if (cs && cs->modifiers.draft_pending) {
                for (int o = 0; o < COLO_MODIFIER_DRAFT_OPTIONS; o++) {
                    if (cs->modifiers.draft_options[o] < 0) continue;
                    Rectangle card = render_colosseum_draft_card_rect(o);
                    if (CheckCollisionPointRec(CLITERAL(Vector2){(float)mx, (float)my}, card)) {
                        rc->human_input.pending_modifier_select = o + 1;
                        break;
                    }
                }
                handled = 1;
            }
        }

        /* 0. context menu: if visible, intercept click for item selection or dismissal */
        if (rc->context_menu.visible) {
            ContextMenu* cm = &rc->context_menu;
            int menu_h = context_menu_height(cm);
            if (mx >= cm->screen_x && mx < cm->screen_x + cm->width &&
                my >= cm->screen_y && my < cm->screen_y + menu_h) {
                int row = context_menu_row_at(cm, mx, my);
                if (row >= 0 && row < cm->item_count)
                    context_menu_execute(rc, env, row);
                else
                    context_menu_dismiss(cm);
            } else {
                context_menu_dismiss(cm);
            }
            handled = 1;
        }

        /* 1. tab bar click */
        if (!handled)
            handled = gui_handle_tab_click(&rc->gui, mx, my);

        /* 2. panel content area (when human control is on) */
        if (!handled && rc->human_input.enabled &&
            mx >= rc->gui.panel_x && mx < rc->gui.panel_x + rc->gui.panel_w &&
            my >= rc->gui.panel_y && my < rc->gui.panel_y + rc->gui.panel_h) {

            Player* viewed = (rc->entity_count > 0 && rc->gui.gui_entity_idx < rc->entity_count)
                ? render_get_player_ptr(env, rc->gui.gui_entity_idx) : NULL;

            if (viewed) {
                switch (rc->gui.active_tab) {
                    case GUI_TAB_PRAYER:
                        human_handle_prayer_click(&rc->human_input, &rc->gui, viewed, mx, my);
                        handled = 1;
                        break;
                    case GUI_TAB_SPELLBOOK:
                        human_handle_spell_click(&rc->human_input, &rc->gui, mx, my);
                        handled = 1;
                        break;
                    case GUI_TAB_COMBAT:
                        human_handle_combat_click(&rc->human_input, &rc->gui, viewed, mx, my);
                        handled = 1;
                        break;
                    default:
                        break;  /* inventory handled separately by gui_inv_handle_mouse */
                }
            }
        }

        /* 3. ground/entity click (game grid area, left of panel) */
        if (!handled && rc->human_input.enabled && mx < rc->gui.panel_x) {
            int entity_hit = 0;
            for (int ei = 0; ei < rc->entity_count; ei++) {
                RenderEntity* ent = &rc->entities[ei];
                if (!render_can_human_attack_entity(
                        &attack_ctx, ent, ei, rc->gui.gui_entity_idx)) {
                    continue;
                }
                if (hull_contains(&rc->entity_hulls[ei], mx, my)) {
                    int target_slot = rc->entities[ei].npc_slot;
                    if (rc->human_input.cursor_mode == CURSOR_ITEM_TARGET) {
                        human_input_clear_selected_ui_target(&rc->human_input);
                    } else if (rc->human_input.cursor_mode == CURSOR_SPELL_TARGET) {
                        human_input_apply_ui_intent(
                            &rc->human_input,
                            osrs_ui_intent_spell_on_target(
                                rc->human_input.selected_spell,
                                rc->human_input.selected_spell_gui_idx,
                                target_slot));
                    } else {
                        rc->human_input.pending_attack = 1;
                        rc->human_input.pending_target_idx = target_slot;
                        rc->human_input.pending_move_x = -1;
                        rc->human_input.pending_move_y = -1;
                        human_input_queue_attack_npc(
                            &rc->human_input,
                            rc->human_input.pending_target_idx);
                    }
                    human_set_click_cross(&rc->human_input, mx, my, 1);
                    entity_hit = 1;
                    break;
                }
            }

            if (!entity_hit) {
                Camera3D cam = render_build_3d_camera(rc);
                Ray ray = GetScreenToWorldRay((Vector2){ (float)mx, (float)my }, cam);
                rc->debug_ray_origin = ray.position;
                rc->debug_ray_dir = ray.direction;

                float best_dist = 1e30f;
                int best_wx = -1, best_wy = -1;
                for (int dy = 0; dy < rc->arena_height; dy++) {
                    for (int dx = 0; dx < rc->arena_width; dx++) {
                        int wx = rc->arena_base_x + dx;
                        int wy = rc->arena_base_y + dy;
                        float tx = (float)wx;
                        float tz = -(float)(wy + 1);
                        float ground_y = rc->terrain
                            ? terrain_height_avg(rc->terrain, wx, wy)
                            : 2.0f;
                        BoundingBox box = {
                            .min = { tx, ground_y - 0.1f, tz },
                            .max = { tx + 1.0f, ground_y, tz + 1.0f },
                        };
                        RayCollision col = GetRayCollisionBox(ray, box);
                        if (col.hit && col.distance < best_dist) {
                            best_dist = col.distance;
                            best_wx = wx;
                            best_wy = wy;
                            rc->debug_ray_hit_x = col.point.x;
                            rc->debug_ray_hit_y = col.point.y;
                            rc->debug_ray_hit_z = col.point.z;
                        }
                    }
                }
                rc->debug_hit_wx = best_wx;
                rc->debug_hit_wy = best_wy;
                rc->debug_plane_wx = -1;
                rc->debug_plane_wy = -1;
                if (best_wx >= 0) {
                    if (rc->human_input.cursor_mode != CURSOR_NORMAL) {
                        human_input_clear_selected_ui_target(&rc->human_input);
                    }
                    rc->human_input.pending_move_x = best_wx;
                    rc->human_input.pending_move_y = best_wy;
                    human_input_queue_walk(&rc->human_input, best_wx, best_wy);
                    human_set_click_cross(&rc->human_input, mx, my, 0);
                }
            }
        }
    }

    /* right-click: open context menu (human mode) or cancel spell targeting */
    if (IsMouseButtonPressed(MOUSE_BUTTON_RIGHT)) {
        if (rc->human_input.enabled) {
            int rmx = GetMouseX();
            int rmy = GetMouseY();
            if (rmx < rc->gui.panel_x) {
                /* cancel spell targeting on right-click (OSRS behavior) */
                if (rc->human_input.cursor_mode != CURSOR_NORMAL)
                    human_input_clear_selected_ui_target(&rc->human_input);
                context_menu_build(rc, env, rmx, rmy);
            } else if (rmx < rc->gui.panel_x + rc->gui.panel_w &&
                    rmy >= rc->gui.panel_y &&
                    rmy < rc->gui.panel_y + rc->gui.panel_h) {
                Player* gui_p = rc->entity_count > 0 && rc->gui.gui_entity_idx < rc->entity_count
                    ? render_get_player_ptr(env, rc->gui.gui_entity_idx)
                    : NULL;
                if (gui_p)
                    context_menu_build_gui(rc, gui_p, rmx, rmy);
                else
                    context_menu_dismiss(&rc->context_menu);
            } else {
                context_menu_dismiss(&rc->context_menu);
            }
        } else if (rc->human_input.cursor_mode != CURSOR_NORMAL) {
            human_input_clear_selected_ui_target(&rc->human_input);
        }
    }

    rc->hover_tile_x = -1;
    rc->hover_tile_y = -1;
    int hmx = GetMouseX();
    int hmy = GetMouseY();
    if (hmx >= 0 && hmx < rc->gui.panel_x &&
        hmy >= 0 && hmy < RENDER_WINDOW_H) {
        Camera3D hcam = render_build_3d_camera(rc);
        Ray hray = GetScreenToWorldRay((Vector2){ (float)hmx, (float)hmy }, hcam);
        float best_dist = 1e30f;
        for (int dy = 0; dy < rc->arena_height; dy++) {
            for (int dx = 0; dx < rc->arena_width; dx++) {
                int wx = rc->arena_base_x + dx;
                int wy = rc->arena_base_y + dy;
                float tx = (float)wx;
                float tz = -(float)(wy + 1);
                float ground_y = rc->terrain
                    ? terrain_height_avg(rc->terrain, wx, wy)
                    : 2.0f;
                BoundingBox box = {
                    .min = { tx, ground_y - 0.1f, tz },
                    .max = { tx + 1.0f, ground_y, tz + 1.0f },
                };
                RayCollision col = GetRayCollisionBox(hray, box);
                if (col.hit && col.distance < best_dist) {
                    best_dist = col.distance;
                    rc->hover_tile_x = wx;
                    rc->hover_tile_y = wy;
                }
            }
        }
    }
}


/* save current env state to history (call after each pvp_step) */
static void render_save_snapshot(RenderClient* rc, OsrsEnv* env) {
    if (rc->history_count >= rc->history_capacity) {
        int new_capacity = rc->history_capacity * 2;
        OsrsEnv* next = (OsrsEnv*)realloc(
            rc->history, (size_t)new_capacity * sizeof(OsrsEnv));
        if (!next) {
            fprintf(stderr, "render history growth failed at %d snapshots\n",
                rc->history_capacity);
            abort();
        }
        rc->history = next;
        rc->history_capacity = new_capacity;
    }
    rc->history[rc->history_count] = *env;
    rc->history_count++;
}

/* restore env state from history snapshot, preserving render-side pointers */
static void render_restore_snapshot(RenderClient* rc, OsrsEnv* env) {
    if (rc->history_cursor < 0 || rc->history_cursor >= rc->history_count) return;

    void* saved_client = env->client;
    void* saved_cmap = env->collision_map;
    float* saved_ocean_obs = env->ocean_io.agent_obs;
    int* saved_ocean_acts = env->ocean_io.agent_actions;
    float* saved_ocean_rew = env->ocean_io.agent_rewards;
    unsigned char* saved_ocean_term = env->ocean_io.agent_terminals;

    *env = rc->history[rc->history_cursor];

    env->client = saved_client;
    env->collision_map = saved_cmap;
    env->ocean_io.agent_obs = saved_ocean_obs;
    env->ocean_io.agent_actions = saved_ocean_acts;
    env->ocean_io.agent_rewards = saved_ocean_rew;
    env->ocean_io.agent_terminals = saved_ocean_term;
}

/* reset history (call on episode reset) */
static void render_clear_history(RenderClient* rc) {
    rc->history_count = 0;
    rc->history_cursor = -1;
}

static void render_push_splat_type(RenderClient* rc, int damage, int pidx, int type);

static int render_hit_splat_type_for_damage(int damage) {
    return damage > 0 ? 1 : 0;
}


/* populate rc->entities from env->players or encounter vtable.
   call before render_post_tick and pvp_render so all draw code uses rc->entities.
   uses fill_render_entities when available, falls back to get_entity + cast. */
static void render_populate_entities(RenderClient* rc, OsrsEnv* env) {
    if (env->encounter_def && env->encounter_state) {
        const EncounterDef* def = (const EncounterDef*)env->encounter_def;
        if (def->fill_render_entities) {
            int count = 0;
            def->fill_render_entities(
                (EncounterState*)env->encounter_state,
                (EncounterContext*)env->encounter_context,
                rc->entities,
                MAX_RENDER_ENTITIES,
                &count);
            rc->entity_count = count;
            /* detect Zuk presence for object variant swap */
            rc->zuk_active = 0;
            for (int zi = 0; zi < count; zi++) {
                if (rc->entities[zi].npc_def_id == 7706) { rc->zuk_active = 1; break; }
            }
        } else {
            int count = def->get_entity_count(
                (EncounterState*)env->encounter_state,
                (EncounterContext*)env->encounter_context);
            if (count > MAX_RENDER_ENTITIES) count = MAX_RENDER_ENTITIES;
            rc->entity_count = count;
            for (int i = 0; i < count; i++) {
                Player* p = (Player*)def->get_entity(
                    (EncounterState*)env->encounter_state,
                    (EncounterContext*)env->encounter_context,
                    i);
                if (p) render_entity_from_player(p, &rc->entities[i]);
            }
        }
        /* override arena bounds from encounter if set */
        if (def->arena_width > 0 && def->arena_height > 0) {
            rc->arena_base_x = def->arena_base_x;
            rc->arena_base_y = def->arena_base_y;
            rc->arena_width = def->arena_width;
            rc->arena_height = def->arena_height;
        }
    } else {
        rc->entity_count = NUM_AGENTS;
        for (int i = 0; i < NUM_AGENTS; i++) {
            render_entity_from_player(&env->players[i], &rc->entities[i]);
        }
    }
}

static int render_default_secondary_for_entity(const RenderEntity* entity) {
    if (entity->entity_type != ENTITY_NPC) {
        return ANIM_SEQ_IDLE;
    }

    const NpcModelMapping* nm = npc_model_lookup((uint16_t)entity->npc_def_id);
    return nm ? (int)nm->idle_anim : -1;
}

static int render_entity_is_visible(const RenderEntity* entity) {
    return entity->entity_type != ENTITY_NPC || entity->npc_visible;
}

static void render_reset_entity_visual_slot(RenderClient* rc, int i) {
    rc->anim[i].primary_seq_id = -1;
    rc->anim[i].primary_frame_idx = 0;
    rc->anim[i].primary_ticks = 0;
    rc->anim[i].primary_loops = 0;
    rc->anim[i].secondary_seq_id = -1;
    rc->anim[i].secondary_frame_idx = 0;
    rc->anim[i].secondary_ticks = 0;
    rc->primary_event_tick[i] = -1;
    rc->last_primary_event_tick[i] = -2;
    rc->composites[i].needs_rebuild = 1;
    rc->prev_npc_slot[i] = -1;
    rc->sub_x[i] = 0;
    rc->sub_y[i] = 0;
    rc->dest_x[i] = 0;
    rc->dest_y[i] = 0;
    rc->step_tracker[i] = 0;
    rc->visual_moving[i] = 0;
    rc->visual_running[i] = 0;
    rc->visual_explicit_running[i] = 0;
    rc->visual_backlog[i] = 0;
    rc->visual_effective_speed[i] = 0.0f;
    rc->facing_opponent[i] = 0;
    rc->yaw[i] = 0.0f;
    rc->target_yaw[i] = 0.0f;
    rc->hp_bar_visible_until[i] = 0;
    rc->entity_hulls[i].count = 0;
    rc->entity_visual_top_y[i] = 0.0f;
    rc->entity_visual_mid_y[i] = 0.0f;
    for (int s = 0; s < RENDER_SPLATS_PER_PLAYER; s++)
        rc->splats[i][s].active = 0;
}

static RenderVisualSlotSnapshot render_snapshot_entity_visual_slot(
    const RenderClient* rc, int i
) {
    RenderVisualSlotSnapshot out;
    memset(&out, 0, sizeof(out));
    out.anim = rc->anim[i];
    out.primary_event_tick = rc->primary_event_tick[i];
    out.last_primary_event_tick = rc->last_primary_event_tick[i];
    out.sub_x = rc->sub_x[i];
    out.sub_y = rc->sub_y[i];
    out.dest_x = rc->dest_x[i];
    out.dest_y = rc->dest_y[i];
    out.visual_moving = rc->visual_moving[i];
    out.visual_running = rc->visual_running[i];
    out.visual_explicit_running = rc->visual_explicit_running[i];
    out.visual_backlog = rc->visual_backlog[i];
    out.visual_effective_speed = rc->visual_effective_speed[i];
    out.step_tracker = rc->step_tracker[i];
    out.yaw = rc->yaw[i];
    out.target_yaw = rc->target_yaw[i];
    out.facing_opponent = rc->facing_opponent[i];
    out.hp_bar_visible_until = rc->hp_bar_visible_until[i];
    memcpy(out.splats, rc->splats[i], sizeof(out.splats));
    return out;
}

static void render_restore_entity_visual_slot(
    RenderClient* rc, int i, const RenderVisualSlotSnapshot* snapshot
) {
    rc->anim[i] = snapshot->anim;
    rc->primary_event_tick[i] = snapshot->primary_event_tick;
    rc->last_primary_event_tick[i] = snapshot->last_primary_event_tick;
    rc->sub_x[i] = snapshot->sub_x;
    rc->sub_y[i] = snapshot->sub_y;
    rc->dest_x[i] = snapshot->dest_x;
    rc->dest_y[i] = snapshot->dest_y;
    rc->visual_moving[i] = snapshot->visual_moving;
    rc->visual_running[i] = snapshot->visual_running;
    rc->visual_explicit_running[i] = snapshot->visual_explicit_running;
    rc->visual_backlog[i] = snapshot->visual_backlog;
    rc->visual_effective_speed[i] = snapshot->visual_effective_speed;
    rc->step_tracker[i] = snapshot->step_tracker;
    rc->yaw[i] = snapshot->yaw;
    rc->target_yaw[i] = snapshot->target_yaw;
    rc->facing_opponent[i] = snapshot->facing_opponent;
    rc->hp_bar_visible_until[i] = snapshot->hp_bar_visible_until;
    memcpy(rc->splats[i], snapshot->splats, sizeof(snapshot->splats));
}

static void render_seed_entity_visual_slot(RenderClient* rc, int i) {
    int size = rc->entities[i].npc_size > 1 ? rc->entities[i].npc_size : 1;
    rc->sub_x[i] = rc->entities[i].x * 128 + size * 64;
    rc->sub_y[i] = rc->entities[i].y * 128 + size * 64;
    rc->dest_x[i] = rc->sub_x[i];
    rc->dest_y[i] = rc->sub_y[i];
    rc->visual_explicit_running[i] = rc->entities[i].is_running;
    rc->visual_backlog[i] = 0;
    rc->visual_effective_speed[i] = 0.0f;
    rc->visual_running[i] = 0;
    rc->anim[i].secondary_seq_id = render_default_secondary_for_entity(&rc->entities[i]);
    rc->prev_npc_slot[i] = rc->entities[i].npc_slot;
}

static void render_ensure_entity_visual_slots(RenderClient* rc) {
    for (int i = 0; i < rc->entity_count; i++) {
        if (rc->sub_x[i] == 0 && rc->sub_y[i] == 0) {
            render_seed_entity_visual_slot(rc, i);
        }
    }
}

static void render_reset_episode_visual_state(RenderClient* rc, OsrsEnv* env) {
    render_clear_history(rc);
    effect_clear_all(rc->effects);
    flight_clear_all(rc);
    gui_reset_inventory_ui_state(&rc->gui);
    rc->client_tick_accumulator = 0.0;

    render_populate_entities(rc, env);
    for (int i = 0; i < MAX_RENDER_ENTITIES; i++) {
        render_reset_entity_visual_slot(rc, i);
        if (i < rc->entity_count) {
            render_seed_entity_visual_slot(rc, i);
        }
    }
    rc->prev_entity_count = rc->entity_count;
}


/**
 * Call BEFORE pvp_step to record pre-tick positions for movement direction.
 */
static void render_pre_tick(RenderClient* rc, OsrsEnv* env) {
    (void)rc; (void)env;
    /* destination is updated in post_tick after positions change */
}

/**
 * Call AFTER pvp_step to update movement destination and facing direction.
 *
 * Movement model matches OSRS client (Entity.java nextStep):
 * - positions stored as sub-tile coords (128 units per tile)
 * - each client frame, visual position moves toward destination at fixed speed
 * - walk = 4 sub-units/frame, run = 8 sub-units/frame (at 50 FPS client ticks)
 * - new identity, visible appearance, and explicit teleports seed instantly
 * - persistent visible identities catch up without distance-based snapping
 * - animation stalls (walkFlag=0) pause movement, then catch up at double speed
 */
static void render_post_tick(RenderClient* rc, OsrsEnv* env) {
    RenderEntity previous_entities[MAX_RENDER_ENTITIES];
    RenderVisualSlotSnapshot previous_visuals[MAX_RENDER_ENTITIES];
    int previous_used[MAX_RENDER_ENTITIES] = {0};
    int new_identity[MAX_RENDER_ENTITIES] = {0};
    int became_visible[MAX_RENDER_ENTITIES] = {0};
    int previous_count = rc->entity_count;
    memcpy(previous_entities, rc->entities,
        (size_t)previous_count * sizeof(previous_entities[0]));
    for (int i = 0; i < previous_count; i++)
        previous_visuals[i] = render_snapshot_entity_visual_slot(rc, i);

    render_populate_entities(rc, env);

    for (int i = 0; i < rc->entity_count; i++) {
        int previous_idx = render_entity_find_previous_identity_index(
            previous_entities, previous_count, previous_used, &rc->entities[i]);
        if (previous_idx >= 0) {
            previous_used[previous_idx] = 1;
            became_visible[i] =
                render_entity_is_visible(&rc->entities[i]) &&
                !render_entity_is_visible(&previous_entities[previous_idx]);
            if (previous_idx != i) {
                render_restore_entity_visual_slot(rc, i, &previous_visuals[previous_idx]);
                rc->composites[i].needs_rebuild = 1;
            }
        } else {
            new_identity[i] = 1;
            render_reset_entity_visual_slot(rc, i);
        }
        rc->prev_npc_slot[i] = rc->entities[i].npc_slot;
    }
    for (int i = rc->entity_count; i < rc->prev_entity_count; i++)
        rc->prev_npc_slot[i] = -1;
    rc->prev_entity_count = rc->entity_count;

    for (int i = 0; i < rc->entity_count; i++) {
        RenderEntity* p = &rc->entities[i];

        /* convert game tile to sub-tile destination (128 units/tile, centered).
           the entity's (x,y) is the SW anchor tile. for size-1 entities,
           center on that tile (+ 64 sub-units). for NxN NPCs, center on
           the NxN footprint (offset by size/2 tiles from SW corner). */
        int size = p->npc_size > 1 ? p->npc_size : 1;
        int new_dest_x = p->x * 128 + size * 64;
        int new_dest_y = p->y * 128 + size * 64;

        if (osrs_render_should_seed_visual_position(
                render_entity_is_visible(p),
                new_identity[i],
                became_visible[i],
                p->render_movement_kind)) {
            render_seed_entity_visual_slot(rc, i);
        }

        /* detect if player moved this tick (destination changed) */
        int moved = (new_dest_x != rc->dest_x[i] || new_dest_y != rc->dest_y[i]);

        /* update destination — NO snap-to-previous-dest. the real OSRS client
           (Canvas.java:165-188) simply advances actor.x toward dest by speed
           each client tick with no snap. sub smoothly interpolates from wherever
           it currently is toward the new dest. the dynamic walk speed in
           render_client_tick ensures arrival within one game tick. */
        rc->dest_x[i] = new_dest_x;
        rc->dest_y[i] = new_dest_y;

        /* latch explicit run state from the game. effective speed drives pose
           selection in the client-tick loop. */
        rc->visual_explicit_running[i] = p->is_running;
        rc->visual_backlog[i] = osrs_render_visual_backlog(
            rc->sub_x[i], rc->sub_y[i], (float)new_dest_x, (float)new_dest_y);

        RenderEntityFacingMode facing_mode = render_entity_select_facing_mode(p, moved);
        if (facing_mode == RENDER_ENTITY_FACE_ATTACK_TARGET) {
            rc->facing_opponent[i] = 1;
        } else if (facing_mode == RENDER_ENTITY_FACE_MOVEMENT) {
            float dx = (float)(new_dest_x - rc->sub_x[i]);
            float dy = (float)(new_dest_y - rc->sub_y[i]);
            if (dx != 0.0f || dy != 0.0f)
                rc->target_yaw[i] = atan2f(-dx, dy);
            rc->facing_opponent[i] = 0;
        } else {
            int face_x = p->dest_x * 128 + 64;
            int face_y = p->dest_y * 128 + 64;
            float dx = (float)(face_x - rc->sub_x[i]);
            float dy = (float)(face_y - rc->sub_y[i]);
            if (dx != 0.0f || dy != 0.0f)
                rc->target_yaw[i] = atan2f(-dx, dy);
            rc->facing_opponent[i] = 0;
        }

        /* shield always faces south (yaw = PI) */
        if (p->npc_def_id == 7707) {
            rc->target_yaw[i] = 3.14159265f;
            rc->facing_opponent[i] = 0;
        }

        /* HP bar + hitsplat: triggered once per game tick when a hit lands.
           HP bar: OSRS cycleStatus = clientTick + 300 (6s = 10 game ticks).
           hitsplat: one splat per hit, fills the next available slot (0-3). */
        int render_hit_count = p->render_hit_count;
        if (render_hit_count == 0 && p->hit_landed_this_tick)
            render_hit_count = 1;
        if (render_hit_count < 0 || render_hit_count > ENCOUNTER_RENDER_HITS_MAX) {
            fprintf(stderr, "invalid render hit count\n");
            abort();
        }
        if (render_hit_count > 0) {
            rc->hp_bar_visible_until[i] = env->tick + 10;
            for (int h = 0; h < render_hit_count; h++) {
                int damage = p->render_hit_count > 0
                    ? p->render_hit_damage[h] : p->hit_damage;
                render_push_splat_type(
                    rc, damage, i, render_hit_splat_type_for_damage(damage));
            }
        }
        if (p->npc_anim_id >= 0 ||
            p->attack_style_this_tick != ATTACK_STYLE_NONE ||
            p->cast_veng_this_tick ||
            p->ate_food_this_tick ||
            p->ate_karambwan_this_tick ||
            p->used_special_this_tick ||
            render_hit_count > 0) {
            rc->primary_event_tick[i] = env->tick;
        }
    }

    /* spawn visual effects (projectiles, spell impacts) based on this tick's events.
       works for any entity count — uses attack_target_entity_idx for multi-entity encounters. */
    int ct = rc->effect_client_tick_counter;
    for (int i = 0; i < rc->entity_count; i++) {
        RenderEntity* p = &rc->entities[i];
        /* resolve target: use attack_target_entity_idx if set, otherwise PvP fallback */
        int target_i;
        if (p->attack_target_entity_idx >= 0) {
            target_i = p->attack_target_entity_idx;
        } else if (rc->entity_count == 2) {
            target_i = 1 - i;
        } else {
            target_i = (i == 0) ? 1 : 0;
        }
        if (target_i < 0 || target_i >= rc->entity_count) continue;
        RenderEntity* t = &rc->entities[target_i];

        /* attacker projectile effects: only for PvP (no encounter overlay).
           encounters with render_post_tick handle their own projectiles via
           encounter_emit_projectile -> flight system. */
        int has_encounter_overlay = (env->encounter_def &&
            ((const EncounterDef*)env->encounter_def)->render_post_tick);

        if (!has_encounter_overlay) {
            /* attacker cast a spell this tick — spawn projectile */
            if (p->attack_style_this_tick == ATTACK_STYLE_MAGIC) {
                uint8_t wpn = p->equipped[GEAR_SLOT_WEAPON];
                int dist = render_pvp_distance_to_target(p, t);
                int duration_ticks = pvp_magic_hit_delay(dist) * 30;
                const OsrsCombatProjectileProfile* profile =
                    osrs_combat_visual_magic_projectile_profile(wpn);
                if (profile) {
                    render_spawn_profile_projectile(rc, profile,
                        p->x, p->y, t->x, t->y,
                        0, duration_ticks, 40 * 4, 30 * 4, 16);
                } else {
                    profile = osrs_combat_visual_spell_projectile(p->magic_type_this_tick);
                    if (profile && profile->travel_spotanim_id >= 0) {
                        render_spawn_profile_projectile(rc, profile,
                            t->x, t->y, t->x, t->y,
                            0, 56, 43 * 4, 0, 16);
                    }
                }
            }

            /* attacker fired a ranged attack this tick */
            if (p->attack_style_this_tick == ATTACK_STYLE_RANGED) {
                uint8_t wpn = p->equipped[GEAR_SLOT_WEAPON];
                int dist = render_pvp_distance_to_target(p, t);
                const OsrsCombatProjectileProfile* special_profile =
                    p->used_special_this_tick
                        ? osrs_combat_visual_ranged_special_projectile_profile(wpn)
                        : NULL;
                const OsrsCombatProjectileProfile* base_profile =
                    osrs_combat_visual_ranged_projectile_profile(
                        wpn, OSRS_COMBAT_PROJECTILE_BOLT);
                int duration_ticks = p->used_special_this_tick
                    ? pvp_ranged_hit_delay_for_weapon(
                        dist, 1, render_pvp_ranged_spec_weapon_for_item(wpn)) * 30
                    : pvp_ranged_hit_delay(dist) * 30;
                if (special_profile) {
                    render_spawn_profile_projectile(rc, special_profile,
                        p->x, p->y, t->x, t->y,
                        0, duration_ticks, 43 * 4, 31 * 4, 16);
                }
                if (!special_profile || special_profile->travel_spotanim_id < 0) {
                    render_spawn_profile_projectile(rc, base_profile,
                        p->x, p->y, t->x, t->y,
                        0, duration_ticks, 43 * 4, 31 * 4, 16);
                }
            }
        }

        /* defender: check what landed on entity p this tick.
           for NPC defenders, the attacker is entity 0 (the player).
           for player (entity 0), attacker is the current target entity. */
        if (p->hit_landed_this_tick) {
            RenderEntity* att;
            if (i == 0) {
                att = t;  /* player was hit — attacker is target entity */
            } else {
                att = &rc->entities[0];  /* NPC was hit — attacker is player */
            }

            /* check if attacker used a powered staff (trident/sang/ayak) */
            uint8_t att_wpn = att->equipped[GEAR_SLOT_WEAPON];
            const OsrsCombatProjectileProfile* att_magic_profile =
                osrs_combat_visual_magic_projectile_profile(att_wpn);
            int att_is_powered_staff = att_magic_profile &&
                att_magic_profile->impact_spotanim_id >= 0;

            int overlay_handles_powered_staff_impact = has_encounter_overlay &&
                att_is_powered_staff && att->attack_style_this_tick == ATTACK_STYLE_MAGIC;

            if (overlay_handles_powered_staff_impact) continue;

            if (att_is_powered_staff &&
                    att->attack_style_this_tick == ATTACK_STYLE_MAGIC) {
                /* powered staff hit: trident impact splash */
                if (p->hit_was_successful) {
                    effect_spawn_spotanim(rc->effects, att_magic_profile->impact_spotanim_id,
                        p->x, p->y, ct, rc->spotanims, rc->anim_cache,
                        rc->model_cache, rc->npc_model_cache,
                        rc->projectile_model_cache);
                } else {
                    effect_spawn_spotanim(rc->effects, GFX_SPLASH,
                        p->x, p->y, ct, rc->spotanims, rc->anim_cache,
                        rc->model_cache, rc->npc_model_cache,
                        rc->projectile_model_cache);
                }
            } else {
                /* barrage impact: use hit_spell_type (set when pending hit resolves)
                   instead of magic_type_this_tick (stale by deferred hit landing).
                   ENCOUNTER_SPELL_ICE=1 -> ice barrage, ENCOUNTER_SPELL_BLOOD=2 -> blood. */
                /* use hit_spell_type from pending hit resolution only. the magic_type_this_tick
                   fallback caused blood/ice effects on tbow hits when barrage fired same tick. */
                int spell = p->hit_spell_type;
                if (spell > 0) {
                    const OsrsCombatProjectileProfile* profile =
                        osrs_combat_visual_spell_projectile(spell);
                    if (!profile || profile->impact_spotanim_id < 0) {
                        fprintf(stderr, "render: missing spell impact visual %d\n", spell);
                        abort();
                    }
                    /* center effect on NPC footprint center using sub-tile precision.
                       for size 2: center at (x*128 + 128, y*128 + 128) = between 4 tiles.
                       for size 3: center at (x*128 + 192, y*128 + 192) = middle tile center. */
                    float fx = (float)p->x * 128.0f + (float)p->npc_size * 64.0f;
                    float fy = (float)p->y * 128.0f + (float)p->npc_size * 64.0f;
                    if (p->hit_was_successful) {
                        effect_spawn_spotanim_subtile(rc->effects, profile->impact_spotanim_id,
                            fx, fy, ct, rc->spotanims, rc->anim_cache,
                            rc->model_cache,
                            rc->npc_model_cache, rc->projectile_model_cache);
                    } else {
                        effect_spawn_spotanim_subtile(rc->effects, GFX_SPLASH,
                            fx, fy, ct, rc->spotanims, rc->anim_cache,
                            rc->model_cache,
                            rc->npc_model_cache, rc->projectile_model_cache);
                    }
                }
            }
        }
    }

    /* update encounter overlay (clouds, boss state) */
    if (env->encounter_def && env->encounter_state) {
        const EncounterDef* edef = (const EncounterDef*)env->encounter_def;
        if (edef->render_post_tick) {
            edef->render_post_tick(
                (EncounterState*)env->encounter_state,
                (EncounterContext*)env->encounter_context,
                &rc->encounter_overlay);

            /* spawn flight projectiles from overlay events.
               per-projectile params with backward-compat defaults. */
            EncounterOverlay* ov = &rc->encounter_overlay;
            for (int i = 0; i < ov->projectile_count; i++) {
                if (!ov->projectiles[i].active) continue;
                int src_sz = ov->projectiles[i].src_size > 0 ? ov->projectiles[i].src_size : ov->boss_size;
                int dst_sz = ov->projectiles[i].dst_size > 0 ? ov->projectiles[i].dst_size : 1;
                float fixed_sx = (float)ov->projectiles[i].src_x + (float)(src_sz - 1) / 2.0f + 0.5f;
                float fixed_sy = (float)ov->projectiles[i].src_y + (float)(src_sz - 1) / 2.0f + 0.5f;
                float fixed_dx = (float)ov->projectiles[i].dst_x + (float)(dst_sz - 1) / 2.0f + 0.5f;
                float fixed_dy = (float)ov->projectiles[i].dst_y + (float)(dst_sz - 1) / 2.0f + 0.5f;
                float sx = fixed_sx;
                float sy = fixed_sy;
                float dx = fixed_dx;
                float dy = fixed_dy;
                if (!render_resolve_projectile_anchor(
                        rc, ov->projectiles[i].source_kind,
                        ov->projectiles[i].source_npc_slot,
                        fixed_sx, fixed_sy, &sx, &sy)) {
                    fprintf(stderr, "render: projectile source anchor missing kind=%d npc_slot=%d\n",
                        ov->projectiles[i].source_kind,
                        ov->projectiles[i].source_npc_slot);
                    abort();
                }
                if (!render_resolve_projectile_anchor(
                        rc, ov->projectiles[i].target_kind,
                        ov->projectiles[i].target_npc_slot,
                        fixed_dx, fixed_dy, &dx, &dy)) {
                    fprintf(stderr, "render: projectile target anchor missing kind=%d npc_slot=%d\n",
                        ov->projectiles[i].target_kind,
                        ov->projectiles[i].target_npc_slot);
                    abort();
                }

                /* use per-projectile params, with defaults for backward compat */
                int dur = ov->projectiles[i].duration_ticks > 0 ? ov->projectiles[i].duration_ticks : 35;
                int sh  = ov->projectiles[i].start_h > 0 ? ov->projectiles[i].start_h : 85;
                int eh  = ov->projectiles[i].end_h > 0 ? ov->projectiles[i].end_h : 40;
                int cv  = ov->projectiles[i].curve != 0 ? ov->projectiles[i].curve : 16;
                float arc = ov->projectiles[i].arc_height;
                int trk = ov->projectiles[i].tracks_target;

                /* cloud/orb styles: offset dst to tile center */
                if (ov->projectiles[i].style == 3 || ov->projectiles[i].style == 4) {
                    dx += 0.5f;
                    dy += 0.5f;
                }

                flight_spawn(rc, sx, sy, dx, dy,
                    ov->projectiles[i].style, ov->projectiles[i].damage,
                    dur, sh, eh, cv, arc, trk,
                    ov->projectiles[i].model_id,
                    ov->projectiles[i].anim_id,
                    ov->projectiles[i].travel_gfx_id,
                    ov->projectiles[i].launch_gfx_id,
                    ov->projectiles[i].impact_gfx_id,
                    ov->projectiles[i].start_delay,
                    ov->projectiles[i].motion_mode,
                    ov->projectiles[i].offset_x,
                    ov->projectiles[i].offset_y,
                    ov->projectiles[i].offset_z,
                    ov->projectiles[i].source_kind,
                    ov->projectiles[i].source_npc_slot,
                    ov->projectiles[i].target_kind,
                    ov->projectiles[i].target_npc_slot);
            }
        }
    }
}

/**
 * One client-tick step: movement + animation advancement.
 *
 * Matches OSRS client processMovement() (Client.java:12996) which calls
 * nextStep() then updateAnimation() once per 20ms client tick. By running
 * both movement and animation at the same rate, they stay perfectly in sync.
 *
 * Movement: faithful to Entity.nextStep() (Client.java:13074)
 * Animation: faithful to updateAnimation() (Client.java:13272)
 */
static void render_client_tick(RenderClient* rc, int player_idx) {
    /* --- nextStep: advance sub-tile position toward destination ---
       faithful to Entity.nextStep() (Client.java:13074-13213).

       when a non-melee animation is playing (walkFlag==0), sub-tile
       movement stalls. stepTracker accumulates stalled frames, then raises
       speed when the visible position has backlog to drain. */
    float dx = rc->dest_x[player_idx] - rc->sub_x[player_idx];
    float dy = rc->dest_y[player_idx] - rc->sub_y[player_idx];

    if (dx == 0.0f && dy == 0.0f) {
        rc->visual_moving[player_idx] = 0;
        rc->visual_running[player_idx] = 0;
        rc->visual_backlog[player_idx] = 0;
        rc->visual_effective_speed[player_idx] = 0.0f;
        rc->step_tracker[player_idx] = 0;
    } else {
        rc->visual_backlog[player_idx] = osrs_render_visual_backlog(
            rc->sub_x[player_idx],
            rc->sub_y[player_idx],
            rc->dest_x[player_idx],
            rc->dest_y[player_idx]);
        int stall = 0;
        if (rc->anim[player_idx].primary_seq_id >= 0 &&
            rc->anim[player_idx].primary_loops == 0) {
            AnimSequence* seq = render_get_anim_sequence(
                rc, (uint16_t)rc->anim[player_idx].primary_seq_id);
            stall = render_sequence_stalls_movement(seq);
        }

        if (stall) {
            rc->step_tracker[player_idx]++;
            rc->visual_moving[player_idx] = 0;
            rc->visual_running[player_idx] = 0;
            rc->visual_effective_speed[player_idx] = 0.0f;
        } else {
            rc->visual_moving[player_idx] = 1;

            float speed = osrs_render_effective_speed_one_client_tick(
                rc->visual_explicit_running[player_idx],
                rc->visual_backlog[player_idx],
                &rc->step_tracker[player_idx]);
            rc->visual_effective_speed[player_idx] = speed;
            rc->visual_running[player_idx] =
                osrs_render_speed_uses_run_pose(speed);
            rc->sub_x[player_idx] = osrs_render_advance_axis_toward(
                rc->sub_x[player_idx], rc->dest_x[player_idx], speed);
            rc->sub_y[player_idx] = osrs_render_advance_axis_toward(
                rc->sub_y[player_idx], rc->dest_y[player_idx], speed);
            rc->visual_backlog[player_idx] = osrs_render_visual_backlog(
                rc->sub_x[player_idx],
                rc->sub_y[player_idx],
                rc->dest_x[player_idx],
                rc->dest_y[player_idx]);

            /* when walking (not facing opponent), update target_yaw to movement
               direction each client tick, matching nextStep's turnDirection
               assignment from step delta. */
            if (!rc->facing_opponent[player_idx] && rc->entities[player_idx].npc_def_id != 7707) {
                if (dx != 0.0f || dy != 0.0f) {
                    rc->target_yaw[player_idx] = atan2f(-dx, dy);
                }
            }
        }
    }

    /* --- appendFocusDestination: gradual turn toward target yaw ---
       matches Entity.appendFocusDestination (Client.java:13215).
       turn rate = 32 / 2048 of a full circle per client tick.
       when facing opponent, recompute target_yaw from visual positions
       every client tick (reference: interactingEntity != -1 path). */
    {
        if (rc->facing_opponent[player_idx]) {
            /* recompute target yaw from current visual positions each client tick,
               matching how appendFocusDestination recalculates from live coords */
            int opp;
            if (rc->entities[player_idx].attack_target_entity_idx >= 0) {
                opp = rc->entities[player_idx].attack_target_entity_idx;
            } else {
                opp = (rc->entity_count == 2) ? (1 - player_idx) : (player_idx == 0 ? 1 : 0);
            }
            float dx = (float)(rc->sub_x[opp] - rc->sub_x[player_idx]);
            float dy = (float)(rc->sub_y[opp] - rc->sub_y[player_idx]);
            if (dx != 0.0f || dy != 0.0f) {
                rc->target_yaw[player_idx] = atan2f(-dx, dy);
            }
        }

        /* step current yaw toward target by turn_speed per client tick.
           32 / 2048 * 2π ≈ 0.0982 radians. snap if within turn_speed. */
        float turn_speed = 32.0f / 2048.0f * 2.0f * 3.14159265f;
        float diff = rc->target_yaw[player_idx] - rc->yaw[player_idx];

        /* normalize to [-π, π] for shortest-path turning */
        while (diff > 3.14159265f) diff -= 2.0f * 3.14159265f;
        while (diff < -3.14159265f) diff += 2.0f * 3.14159265f;

        if (fabsf(diff) <= turn_speed) {
            rc->yaw[player_idx] = rc->target_yaw[player_idx];
        } else if (diff > 0.0f) {
            rc->yaw[player_idx] += turn_speed;
        } else {
            rc->yaw[player_idx] -= turn_speed;
        }

        /* normalize yaw to [-π, π] */
        while (rc->yaw[player_idx] > 3.14159265f) rc->yaw[player_idx] -= 2.0f * 3.14159265f;
        while (rc->yaw[player_idx] < -3.14159265f) rc->yaw[player_idx] += 2.0f * 3.14159265f;
    }

    /* --- updateAnimation: advance both animation tracks --- */

    /* secondary (pose): select based on visual movement state.
       NPCs switch between idle and walk animations on the secondary track
       (matching real OSRS client — walk/idle are secondary, attacks are primary).
       this prevents the stall mechanism from freezing movement during walk. */
    int new_secondary;
    if (rc->entities[player_idx].entity_type == ENTITY_NPC) {
        const NpcModelMapping* nm = npc_model_lookup(
            (uint16_t)rc->entities[player_idx].npc_def_id);
        if (nm) {
            if (!rc->visual_moving[player_idx]) {
                new_secondary = (int)nm->idle_anim;
            } else if (osrs_render_speed_uses_run_pose(
                    rc->visual_effective_speed[player_idx]) &&
                    nm->run_anim != 65535) {
                new_secondary = (int)nm->run_anim;
            } else {
                new_secondary = nm->walk_anim != 65535
                    ? (int)nm->walk_anim
                    : (int)nm->idle_anim;
            }
        } else {
            new_secondary = -1;
        }
    } else {
        new_secondary = render_select_secondary(rc, player_idx);
    }
    if (rc->anim[player_idx].secondary_seq_id != new_secondary) {
        rc->anim[player_idx].secondary_seq_id = new_secondary;
        rc->anim[player_idx].secondary_frame_idx = 0;
        rc->anim[player_idx].secondary_ticks = 0;
    }

    /* advance secondary frame timing */
    if (rc->anim[player_idx].secondary_seq_id >= 0) {
        AnimSequence* seq = render_get_anim_sequence(
            rc, (uint16_t)rc->anim[player_idx].secondary_seq_id);
        if (seq && seq->frame_count > 0) {
            int fidx = rc->anim[player_idx].secondary_frame_idx % seq->frame_count;
            int delay = seq->frames[fidx].delay > 0 ? seq->frames[fidx].delay : 1;
            rc->anim[player_idx].secondary_ticks++;
            if (rc->anim[player_idx].secondary_ticks >= delay) {
                rc->anim[player_idx].secondary_ticks = 0;
                rc->anim[player_idx].secondary_frame_idx =
                    (fidx + 1) % seq->frame_count;
            }
        }
    }

    /* advance primary frame timing (if active) */
    if (rc->anim[player_idx].primary_seq_id >= 0) {
        AnimSequence* seq = render_get_anim_sequence(
            rc, (uint16_t)rc->anim[player_idx].primary_seq_id);
        if (seq && seq->frame_count > 0) {
            int fidx = rc->anim[player_idx].primary_frame_idx % seq->frame_count;
            int delay = seq->frames[fidx].delay > 0 ? seq->frames[fidx].delay : 1;
            rc->anim[player_idx].primary_ticks++;
            if (rc->anim[player_idx].primary_ticks >= delay) {
                rc->anim[player_idx].primary_ticks = 0;
                int next = (fidx + 1) % seq->frame_count;
                rc->anim[player_idx].primary_frame_idx = next;
                /* detect loop completion (wrapped back to 0) */
                if (next == 0) {
                    rc->anim[player_idx].primary_loops++;
                }
            }
        }
    }
}

/**
 * Get world position from sub-tile coordinates (128 units = 1 tile).
 */
static void render_get_visual_pos(
    RenderClient* rc, int player_idx,
    float* out_x, float* out_z, float* out_ground
) {
    /* convert sub-tile to world (128 units per tile) */
    float tile_x = (float)rc->sub_x[player_idx] / 128.0f;
    float tile_y = (float)rc->sub_y[player_idx] / 128.0f;

    *out_x = tile_x;
    *out_z = -tile_y;

    if (rc->terrain) {
        *out_ground = terrain_height_avg(rc->terrain,
            (int)tile_x, (int)tile_y);
    } else {
        *out_ground = 2.0f;
    }
}


/* advance splat animation by one client tick (20ms).
   exact OSRS logic from Client.java:6107-6143 (mode 2 animated):
   - hitmarkMove starts at +5.0, decrements by 0.25 until -5.0 (40 ticks to settle)
   - hitmarkTrans starts at 230, stays there (mode 2 clamp means fade at -26 never fires)
   - hitsLoopCycle expires after 70 client ticks → splat just disappears */
static void render_update_splats_client_tick(RenderClient* rc) {
    for (int p = 0; p < rc->entity_count; p++) {
        for (int i = 0; i < RENDER_SPLATS_PER_PLAYER; i++) {
            HitSplat* s = &rc->splats[p][i];
            if (!s->active) continue;
            s->hitmark_move -= 0.25;
            if (s->hitmark_move < -5.0) {
                s->hitmark_move = -5.0;
            }
            s->ticks_remaining--;
            if (s->ticks_remaining <= 0) {
                s->active = 0;
            }
        }
    }
}

/* OSRS Entity.damage(): find first expired slot, init with standard values */
static void render_push_splat_type(RenderClient* rc, int damage, int pidx, int type) {
    for (int i = 0; i < RENDER_SPLATS_PER_PLAYER; i++) {
        if (!rc->splats[pidx][i].active) {
            rc->splats[pidx][i] = (HitSplat){
                .active = 1,
                .damage = damage,
                .type = type,
                .hitmark_move = 5.0,
                .hitmark_trans = 230,
                .ticks_remaining = 70,
            };
            return;
        }
    }
    /* all 4 slots full: overwrite the one closest to expiry */
    int oldest = 0;
    for (int i = 1; i < RENDER_SPLATS_PER_PLAYER; i++) {
        if (rc->splats[pidx][i].ticks_remaining < rc->splats[pidx][oldest].ticks_remaining)
            oldest = i;
    }
    rc->splats[pidx][oldest] = (HitSplat){
        .active = 1,
        .damage = damage,
        .type = type,
        .hitmark_move = 5.0,
        .hitmark_trans = 230,
        .ticks_remaining = 70,
    };
}

/* draw a hitsplat using the actual cache sprites (317 mode 0).
   Client.java:6052-6073: hitMarks[type].drawSprite(spriteDrawX - 12, spriteDrawY - 12)
   then smallFont.drawText centered on the sprite.
   sprite index: 0=blue(miss), 1=red(regular hit). sprites are 24x23px. */
static void render_draw_hitmark(RenderClient* rc, int cx, int cy, int damage, int opacity, int type) {
    unsigned char a = (unsigned char)(opacity > 255 ? 255 : (opacity < 0 ? 0 : opacity));
    int sprite_idx = (type >= 0 && type < 5) ? type : ((damage > 0) ? 1 : 0);

    if (rc->hitmark_sprites_loaded) {
        /* draw the actual cache sprite, centered at (cx, cy).
           OSRS draws at spriteDrawX-12, spriteDrawY-12 (centering a 24x23 sprite) */
        Texture2D tex = rc->hitmark_sprites[sprite_idx];
        float draw_x = (float)cx - (float)tex.width / 2.0f;
        float draw_y = (float)cy - (float)tex.height / 2.0f;
        DrawTexture(tex, (int)draw_x, (int)draw_y, (Color){ 255, 255, 255, a });
    } else {
        /* fallback: colored circle if sprites missing */
        Color bg = (sprite_idx == 4) ? (Color){ 220, 185, 45, a } :
            ((damage > 0) ? (Color){ 175, 25, 25, a } : (Color){ 65, 105, 225, a });
        DrawCircle(cx, cy, 12.0f, bg);
    }

    /* damage number: white text with black shadow, centered on the sprite.
       OSRS Client.java:6070-6071: smallFont.drawText at spriteDrawY+5, spriteDrawX */
    const char* txt = TextFormat("%d", damage);
    int tw = MeasureText(txt, 10);
    DrawText(txt, cx - tw / 2 + 1, cy - 4, 10, (Color){ 0, 0, 0, a });
    DrawText(txt, cx - tw / 2, cy - 5, 10, (Color){ 255, 255, 255, a });
}

/* slot offset layout from Client.java:6052-6072 (mode 0, used across modes):
   slot 0: center
   slot 1: up 20px
   slot 2: left 15px, up 10px
   slot 3: right 15px, up 10px */
static void render_splat_slot_offset(int slot, int* dx, int* dy) {
    switch (slot) {
        case 0: *dx = 0;   *dy = 0;   break;
        case 1: *dx = 0;   *dy = -20; break;
        case 2: *dx = -15; *dy = -10; break;
        case 3: *dx = 15;  *dy = -10; break;
        default: *dx = 0;  *dy = 0;   break;
    }
}

static void render_splat_screen_pos(
    int base_x,
    int base_y,
    int slot,
    const HitSplat* s,
    int* out_x,
    int* out_y
) {
    int slot_dx, slot_dy;
    render_splat_slot_offset(slot, &slot_dx, &slot_dy);
    *out_x = base_x + slot_dx;
    *out_y = base_y + slot_dy + (int)s->hitmark_move;
}

static float render_overhead_anchor_y(float visual_top_y) {
    return visual_top_y + 15.0f / 128.0f;
}


/** Look up inferno NPC name from npc_def_id. returns NULL if not an inferno NPC. */
static const char* inferno_npc_name(int npc_def_id) {
    switch (npc_def_id) {
        case 7691: return "Jal-Nib";
        case 7692: return "Jal-MejRah";
        case 7693: return "Jal-Ak";
        case 7694: return "Jal-AkRek-Ket";
        case 7695: return "Jal-AkRek-Xil";
        case 7696: return "Jal-AkRek-Mej";
        case 7697: return "Jal-ImKot";
        case 7698: return "Jal-Xil";
        case 7699: return "Jal-Zek";
        case 7700: return "JalTok-Jad";
        case 7701: return "Yt-HurKot";
        case 7706: return "TzKal-Zuk";
        case 7707: return "Ancestral Glyph";
        case 7708: return "Jal-MejJak";
        default:   return NULL;
    }
}

static void render_draw_panel_npc(int x, int y, RenderEntity* p, OsrsEnv* env) {
    int line_h = 14;

    /* determine NPC display name and color from npc_def_id */
    const char* npc_name = NULL;
    Color name_color = COLOR_TEXT;

    /* zulrah forms */
    if (p->npc_def_id == 2042)      { npc_name = "Zulrah [GREEN]"; name_color = GREEN; }
    else if (p->npc_def_id == 2043) { npc_name = "Zulrah [RED]"; name_color = RED; }
    else if (p->npc_def_id == 2044) { npc_name = "Zulrah [BLUE]"; name_color = CLITERAL(Color){ 80, 140, 255, 255 }; }

    /* inferno NPCs */
    if (!npc_name) {
        const char* inf_name = inferno_npc_name(p->npc_def_id);
        if (inf_name) {
            npc_name = inf_name;
            name_color = CLITERAL(Color){ 255, 120, 50, 255 };  /* inferno orange */
        }
    }

    if (!npc_name) {
        const char* colo_name = colosseum_npc_name(p->npc_def_id);
        if (colo_name) {
            npc_name = colo_name;
            name_color = CLITERAL(Color){ 230, 200, 110, 255 };
        }
    }

    if (!npc_name) npc_name = TextFormat("NPC %d", p->npc_def_id);

    DrawText(npc_name, x, y, 14, name_color);
    y += line_h + 4;

    DrawText(TextFormat("HP:     %d / %d", p->current_hitpoints, p->base_hitpoints), x, y, 10, COLOR_TEXT);
    y += line_h;
    DrawText(TextFormat("Pos:    (%d, %d)", p->x, p->y), x, y, 10, COLOR_TEXT_DIM);
    y += line_h;

    /* encounter-specific state overlay */
    if (env->encounter_def && env->encounter_state) {
        const EncounterDef* edef = (const EncounterDef*)env->encounter_def;

        if (strcmp(edef->name, "zulrah") == 0) {
            /* zulrah-specific state */
            ZulrahState* zs = (ZulrahState*)env->encounter_state;
            DrawText(TextFormat("Visible: %s", zs->zulrah_visible ? "yes" : "no"), x, y, 10, COLOR_TEXT_DIM);
            y += line_h;
            DrawText(TextFormat("Phase: %d  Surface: %d  %s", zs->phase_timer, zs->surface_timer,
                zs->is_diving ? "DIVING" : ""), x, y, 10, zs->is_diving ? COLOR_FREEZE : COLOR_TEXT_DIM);
            y += line_h;
            const char* rot_names[] = { "Magma A", "Magma B", "Serp", "Tanz" };
            const char* rot_name = (zs->rotation_index >= 0 && zs->rotation_index < 4)
                ? rot_names[zs->rotation_index] : "???";
            DrawText(TextFormat("Rotation: %s (phase %d/%d)", rot_name,
                zs->phase_index + 1,
                (zs->rotation_index >= 0 && zs->rotation_index < 4)
                    ? ZUL_ROT_LENGTHS[zs->rotation_index] : 0),
                x, y, 10, COLOR_TEXT);
            y += line_h;
            DrawText(TextFormat("Action:  %d/%d (timer %d)", zs->action_index,
                zs->action_progress, zs->action_timer), x, y, 10, COLOR_TEXT_DIM);
            y += line_h;

            int snakes = 0, clouds = 0;
            for (int i = 0; i < ZUL_MAX_SNAKELINGS; i++)
                if (zs->snakelings[i].active) snakes++;
            for (int i = 0; i < ZUL_MAX_CLOUDS; i++)
                if (zs->clouds[i].active) clouds++;
            DrawText(TextFormat("Snakelings: %d  Clouds: %d", snakes, clouds), x, y, 10, COLOR_TEXT_DIM);

        } else if (strcmp(edef->name, "inferno") == 0) {
            /* inferno-specific state */
            InfernoState* is = (InfernoState*)env->encounter_state;
            DrawText(TextFormat("Wave:   %d / %d", is->wave + 1, INF_NUM_WAVES), x, y, 10, COLOR_TEXT);
            y += line_h;

            int active_npcs = 0;
            for (int i = 0; i < INF_MAX_NPCS; i++)
                if (is->npcs[i].active) active_npcs++;
            DrawText(TextFormat("NPCs:   %d active", active_npcs), x, y, 10, COLOR_TEXT_DIM);
            y += line_h;

            int pillars_alive = 0;
            for (int i = 0; i < INF_NUM_PILLARS; i++)
                if (is->pillars[i].active) pillars_alive++;
            DrawText(TextFormat("Pillars: %d / %d", pillars_alive, INF_NUM_PILLARS), x, y, 10, COLOR_TEXT_DIM);
        }
    }
    (void)y;
}

/* render_draw_panel removed — replaced by gui_draw() in osrs_pvp_gui.h */


static Camera3D render_build_3d_camera(RenderClient* rc) {
    Camera3D cam = { 0 };
    float cx = rc->cam_target_x;
    float cz = rc->cam_target_z;
    /* sample terrain height at camera target (heightmap uses OSRS coords, negate Z back) */
    float cy = (rc->terrain) ? terrain_height_at(rc->terrain, (int)cx, (int)(-cz)) : 2.0f;

    float d = rc->cam_dist;
    float px = cx + d * cosf(rc->cam_pitch) * sinf(rc->cam_yaw);
    float py = cy + d * sinf(rc->cam_pitch);
    float pz = cz + d * cosf(rc->cam_pitch) * cosf(rc->cam_yaw);

    cam.position = (Vector3){ px, py, pz };
    cam.target = (Vector3){ cx, cy, cz };
    cam.up = (Vector3){ 0.0f, 1.0f, 0.0f };
    cam.fovy = 60.0f;
    cam.projection = CAMERA_PERSPECTIVE;
    return cam;
}


/** Return the short debug label for an attack style. */
static const char* render_debug_attack_style_name(int style) {
    switch (style) {
        case ATTACK_STYLE_NONE: return "NONE";
        case ATTACK_STYLE_MELEE: return "MEL";
        case ATTACK_STYLE_RANGED: return "RNG";
        case ATTACK_STYLE_MAGIC: return "MAG";
        default: return "???";
    }
}

/** Return the debug text color for an attack style. */
static Color render_debug_attack_style_color(int style) {
    switch (style) {
        case ATTACK_STYLE_MELEE: return RED;
        case ATTACK_STYLE_RANGED: return GREEN;
        case ATTACK_STYLE_MAGIC: return BLUE;
        default: return WHITE;
    }
}

/** Draw one centered debug line and advance the vertical cursor. */
static void render_draw_centered_debug_line(
    const char* text, int center_x, int* y, int font_size, Color color
) {
    int text_width = MeasureText(text, font_size);
    DrawText(text, center_x - text_width / 2, *y, font_size, color);
    *y += font_size + 1;
}

/** Draw render-entity debug metadata populated by an encounter fill hook. */
static void render_draw_entity_debug_metadata(
    const RenderEntity* entity, Vector2 screen_head
) {
    int y = (int)screen_head.y + 10;
    int x = (int)screen_head.x;
    int font_size = 10;

    if (entity->debug_npc_type_name) {
        render_draw_centered_debug_line(
            entity->debug_npc_type_name, x, &y, font_size, COLOR_TEXT);
    }

    render_draw_centered_debug_line(
        TextFormat("HP:%d/%d", entity->current_hitpoints, entity->base_hitpoints),
        x, &y, font_size, COLOR_TEXT);

    int style = entity->debug_attack_style;
    render_draw_centered_debug_line(
        TextFormat("ATK:%d %s",
            entity->debug_attack_timer,
            render_debug_attack_style_name(style)),
        x, &y, font_size, render_debug_attack_style_color(style));

    if (entity->frozen_ticks > 0) {
        render_draw_centered_debug_line(
            TextFormat("FRZ:%d", entity->frozen_ticks),
            x, &y, font_size, CLITERAL(Color){100, 200, 255, 255});
    }

    if (entity->debug_manticore_state_active) {
        render_draw_centered_debug_line(
            TextFormat("MC:%d %s/%s/%s",
                entity->debug_manticore_cycle_step,
                render_debug_attack_style_name(entity->debug_manticore_orb_style[0]),
                render_debug_attack_style_name(entity->debug_manticore_orb_style[1]),
                render_debug_attack_style_name(entity->debug_manticore_orb_style[2])),
            x, &y, font_size, YELLOW);
    }
}


/**
 * Determine the primary (action) animation for this tick.
 * Returns -1 if no action animation should play.
 * Primary animations are server-driven in the real client: attacks, casts, etc.
 * They play once then auto-expire (loopCount=1 effectively).
 */
static int render_select_primary(RenderEntity* p) {
    if (p->current_hitpoints <= 0) return ANIM_SEQ_DEATH;

    if (p->attack_style_this_tick != ATTACK_STYLE_NONE) {
        if (p->attack_style_this_tick == ATTACK_STYLE_MAGIC) {
            uint8_t wpn = p->equipped[GEAR_SLOT_WEAPON];
            return osrs_combat_visual_magic_attack_anim_for_fight_style(
                wpn, p->fight_style, p->used_special_this_tick, ANIM_SEQ_CAST_BARRAGE);
        }
        return osrs_combat_visual_weapon_attack_anim_for_fight_style(
            p->equipped[GEAR_SLOT_WEAPON],
            (AttackStyle)p->attack_style_this_tick,
            p->fight_style,
            p->used_special_this_tick,
            OSRS_PLAYER_UNARMED_ATTACK_ANIM);
    }

    if (p->ate_food_this_tick || p->ate_karambwan_this_tick) {
        return ANIM_SEQ_EAT;
    }

    if (p->cast_veng_this_tick) {
        return ANIM_SEQ_CAST_VENG;
    }

    if (p->hit_landed_this_tick &&
            render_offhand_uses_shield_block_anim(p->equipped[GEAR_SLOT_SHIELD])) {
        return ANIM_SEQ_BLOCK_SHIELD;
    }

    return -1; /* no action this tick */
}

typedef enum {
    RENDER_ITEM_READY_ANIM,
    RENDER_ITEM_WALK_ANIM,
    RENDER_ITEM_RUN_ANIM,
} RenderItemAnimKind;

static uint32_t render_item_anim_id(uint16_t item_id, RenderItemAnimKind kind) {
    switch (kind) {
        case RENDER_ITEM_READY_ANIM:
            return item_render_ready_anim(item_id);
        case RENDER_ITEM_WALK_ANIM:
            return item_render_walk_anim(item_id);
        case RENDER_ITEM_RUN_ANIM:
            return item_render_run_anim(item_id);
    }
    abort();
}

static int render_weapon_anim_or_fallback(
    RenderClient* rc, const RenderEntity* p, RenderItemAnimKind kind, int fallback
) {
    uint8_t weapon_idx = p->equipped[GEAR_SLOT_WEAPON];
    if (weapon_idx >= NUM_ITEMS) return fallback;

    uint16_t item_id = ITEM_DATABASE[weapon_idx].item_id;
    uint32_t anim_id = render_item_anim_id(item_id, kind);
    if (anim_id == ITEM_RENDER_MODEL_MISSING || anim_id > UINT16_MAX) return fallback;
    if (!render_get_anim_sequence(rc, (uint16_t)anim_id)) return fallback;
    return (int)anim_id;
}

/**
 * Determine the secondary (pose) animation based on VISUAL movement state.
 *
 * In the real client (nextStep), this is set based on the entity's sub-tile
 * movement: idle when not moving, walk or run based on moveSpeed. We use
 * visual_moving plus the effective speed from the client-tick loop.
 */
static int render_select_secondary(RenderClient* rc, int player_idx) {
    const RenderEntity* p = &rc->entities[player_idx];
    if (!rc->visual_moving[player_idx]) {
        return render_weapon_anim_or_fallback(rc, p, RENDER_ITEM_READY_ANIM, ANIM_SEQ_IDLE);
    }
    if (osrs_render_speed_uses_run_pose(rc->visual_effective_speed[player_idx])) {
        return render_weapon_anim_or_fallback(rc, p, RENDER_ITEM_RUN_ANIM, ANIM_SEQ_RUN);
    }
    return render_weapon_anim_or_fallback(rc, p, RENDER_ITEM_WALK_ANIM, ANIM_SEQ_WALK);
}


/**
 * Append a single OsrsModel's geometry into the player composite.
 * Offsets face indices by the current base vertex count so the merged
 * index buffer references the correct vertices.
 */
static OsrsModelAppendResult composite_try_add_model(PlayerComposite* comp, OsrsModel* om) {
    if (!comp) {
        fprintf(stderr, "render: composite_try_add_model got null composite\n");
        abort();
    }

    int bv_off = comp->base_vert_count;
    int fc_off = comp->face_count;

    OsrsModelAppendResult append_result = osrs_model_append_check(
        bv_off, fc_off, om, COMPOSITE_MAX_BASE_VERTS, COMPOSITE_MAX_FACES);
    if (append_result != OSRS_MODEL_APPEND_OK) {
        return append_result;
    }
    if (!comp->mesh.colors || !om->mesh.colors || !om->face_indices ||
            !om->vertex_skins) {
        fprintf(stderr, "render: composite model %u has incomplete geometry buffers\n",
                om->model_id);
        abort();
    }

    /* append base vertices */
    memcpy(comp->base_vertices + bv_off * 3,
           om->base_vertices, om->base_vert_count * 3 * sizeof(int16_t));

    /* append vertex skins */
    memcpy(comp->vertex_skins + bv_off,
           om->vertex_skins, om->base_vert_count);

    /* append face indices (offset by base vertex count) */
    int nfi = om->mesh.triangleCount * 3;
    for (int f = 0; f < nfi; f++) {
        comp->face_indices[fc_off * 3 + f] = om->face_indices[f] + (uint16_t)bv_off;
    }

    /* append expanded colors into the composite mesh color buffer */
    int exp_off = fc_off * 3;
    memcpy(comp->mesh.colors + exp_off * 4,
           om->mesh.colors, om->mesh.triangleCount * 3 * 4);
    if (comp->mesh.texcoords) {
        if (om->mesh.texcoords) {
            memcpy(comp->mesh.texcoords + exp_off * 2,
                   om->mesh.texcoords,
                   om->mesh.triangleCount * 3 * 2 * sizeof(float));
        } else {
            memset(comp->mesh.texcoords + exp_off * 2,
                   0,
                   om->mesh.triangleCount * 3 * 2 * sizeof(float));
        }
    }

    comp->base_vert_count += om->base_vert_count;
    comp->face_count += om->mesh.triangleCount;
    return OSRS_MODEL_APPEND_OK;
}

static void composite_add_model_or_abort(
    PlayerComposite* comp,
    OsrsModel* om,
    uint32_t requested_model_id,
    const char* label
) {
    OsrsModelAppendResult result = composite_try_add_model(comp, om);
    if (result == OSRS_MODEL_APPEND_OK) return;

    int model_faces = om ? om->mesh.triangleCount : 0;
    int model_base_verts = om ? (int)om->base_vert_count : 0;
    uint32_t actual_model_id = om ? om->model_id : requested_model_id;
    fprintf(stderr,
            "render: composite append failed for %s model requested=%u actual=%u: %s "
            "(base=%d/%d + %d, faces=%d/%d + %d)\n",
            label ? label : "unknown",
            requested_model_id,
            actual_model_id,
            osrs_model_append_result_name(result),
            comp ? comp->base_vert_count : -1,
            COMPOSITE_MAX_BASE_VERTS,
            model_base_verts,
            comp ? comp->face_count : -1,
            COMPOSITE_MAX_FACES,
            model_faces);
    abort();
}

static OsrsModel* composite_get_model_or_abort(
    ModelCache* cache,
    uint32_t model_id,
    const char* label
) {
    OsrsModel* model = model_cache_get(cache, model_id);
    if (model) return model;
    fprintf(stderr, "render: missing %s model %u in model cache\n",
            label ? label : "unknown", model_id);
    abort();
}

/**
 * Rebuild a player's composite model from their visible body parts + equipment.
 * Called when equipment changes or on first frame.
 */
static void composite_rebuild(
    PlayerComposite* comp, ModelCache* cache, RenderEntity* p
) {
    comp->base_vert_count = 0;
    comp->face_count = 0;

    OsrsPlayerAppearance appearance = osrs_resolve_player_appearance(p->equipped);

    for (int bp = 0; bp < BODY_PART_COUNT; bp++) {
        if (!appearance.body_visible[bp]) continue;
        uint32_t model_id = appearance.body_model_ids[bp];
        OsrsModel* om = composite_get_model_or_abort(cache, model_id, "body");
        composite_add_model_or_abort(comp, om, model_id, "body");
    }

    for (int s = 0; s < OSRS_VISIBLE_EQUIP_SLOT_COUNT; s++) {
        int slot = OSRS_VISIBLE_EQUIP_SLOTS[s];
        if (!appearance.item_visible[slot]) continue;
        uint32_t model_id = appearance.item_model_ids[slot];
        OsrsModel* om = composite_get_model_or_abort(cache, model_id, "equipment");
        composite_add_model_or_abort(comp, om, model_id, "equipment");
    }

    /* rebuild animation state for the new composite geometry */
    if (comp->anim_state) {
        anim_model_state_free(comp->anim_state);
        comp->anim_state = NULL;
    }
    if (comp->base_vert_count > 0) {
        comp->anim_state = anim_model_state_create(
            comp->vertex_skins, comp->base_vert_count);
    }

    /* save equipment state for change detection */
    memcpy(comp->last_equipped, p->equipped, NUM_GEAR_SLOTS);
    comp->needs_rebuild = 0;
}

/**
 * Rebuild an NPC's composite from a single cache model (no equipment composition).
 * Used for Zulrah forms, snakelings, and other encounter NPCs.
 */
static void composite_rebuild_npc(
    PlayerComposite* comp, ModelCache* cache, ModelCache* npc_cache, int npc_def_id
) {
    comp->base_vert_count = 0;
    comp->face_count = 0;

    /* zero mesh buffers to prevent stale GPU data from showing as garbled geometry
       if the model fails to load or exceeds composite limits */
    if (comp->mesh.vertices)
        memset(comp->mesh.vertices, 0, COMPOSITE_MAX_EXP_VERTS * 3 * sizeof(float));
    if (comp->mesh.colors)
        memset(comp->mesh.colors, 0, COMPOSITE_MAX_EXP_VERTS * 4);
    if (comp->mesh.texcoords)
        memset(comp->mesh.texcoords, 0, COMPOSITE_MAX_EXP_VERTS * 2 * sizeof(float));

    /* look up model ID from NPC definition */
    uint32_t model_id = 0;
    const NpcModelMapping* mapping = npc_model_lookup((uint16_t)npc_def_id);
    if (!mapping) {
        fprintf(stderr, "render: missing NPC model mapping for npc_def_id=%d\n", npc_def_id);
        abort();
    }
    model_id = mapping->model_id;

    OsrsModel* om = model_cache_get(cache, model_id);
    /* fallback: check secondary NPC model cache (inferno etc.) */
    if (!om && npc_cache)
        om = model_cache_get(npc_cache, model_id);
    if (!om) {
        fprintf(stderr, "render: npc_def_id=%d mapped model %u is missing from loaded caches\n",
                npc_def_id, model_id);
        abort();
    }
    composite_add_model_or_abort(comp, om, model_id, "npc");

    /* rebuild animation state */
    if (comp->anim_state) {
        anim_model_state_free(comp->anim_state);
        comp->anim_state = NULL;
    }
    if (comp->base_vert_count > 0) {
        comp->anim_state = anim_model_state_create(
            comp->vertex_skins, comp->base_vert_count);
    }

    comp->last_npc_def_id = npc_def_id;
    comp->needs_rebuild = 0;
}

/**
 * Initialize the composite's GPU resources (once, at max capacity).
 * Uses dynamic=true since we update vertices every frame.
 */
static void composite_init_gpu(PlayerComposite* comp, ModelCache* cache) {
    if (comp->gpu_ready) return;

    comp->mesh.vertexCount = COMPOSITE_MAX_EXP_VERTS;
    comp->mesh.triangleCount = COMPOSITE_MAX_FACES;
    comp->mesh.vertices = (float*)RL_CALLOC(COMPOSITE_MAX_EXP_VERTS * 3, sizeof(float));
    comp->mesh.colors = (unsigned char*)RL_CALLOC(COMPOSITE_MAX_EXP_VERTS, 4);
    if (cache && cache->has_atlas) {
        comp->mesh.texcoords = (float*)RL_CALLOC(COMPOSITE_MAX_EXP_VERTS * 2, sizeof(float));
    }

    UploadMesh(&comp->mesh, true);  /* dynamic VBO for per-frame updates */
    comp->model = LoadModelFromMesh(comp->mesh);
    if (cache && cache->has_atlas) {
        comp->model.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture =
            cache->atlas_texture;
    }
    comp->gpu_ready = 1;
}

/**
 * Animate composite model, upload to GPU, and draw.
 */
/**
 * Apply animation(s), re-expand vertices, upload to GPU, and draw.
 *
 * When both primary and secondary are provided with an interleave_order,
 * uses interleaved application (upper body from primary, legs from secondary).
 * Otherwise falls back to single-frame application.
 */
static void composite_animate_and_draw(
    PlayerComposite* comp,
    const AnimFrameData* secondary_frame, const AnimFrameBase* secondary_fb,
    const AnimFrameData* primary_frame, const AnimFrameBase* primary_fb,
    const uint8_t* interleave_order, int interleave_count,
    Matrix transform
) {
    if (!comp->anim_state || comp->face_count == 0) return;

    /* apply animation transforms to base vertices */
    if (primary_frame && secondary_frame && interleave_order && interleave_count > 0) {
        anim_apply_frame_interleaved(
            comp->anim_state, comp->base_vertices,
            secondary_frame, secondary_fb,
            primary_frame, primary_fb,
            interleave_order, interleave_count);
    } else if (primary_frame) {
        if (primary_frame->kind == ANIM_FRAME_LEGACY) {
            anim_apply_frame(comp->anim_state, comp->base_vertices,
                primary_frame, primary_fb);
        } else if (primary_frame->kind == ANIM_FRAME_MAYA_BAKED) {
            anim_apply_maya_baked_frame(comp->anim_state, primary_frame);
        } else {
            fprintf(stderr, "render: unknown primary animation frame kind %u\n",
                primary_frame->kind);
            abort();
        }
    } else if (secondary_frame) {
        if (secondary_frame->kind == ANIM_FRAME_LEGACY) {
            anim_apply_frame(comp->anim_state, comp->base_vertices,
                secondary_frame, secondary_fb);
        } else if (secondary_frame->kind == ANIM_FRAME_MAYA_BAKED) {
            anim_apply_maya_baked_frame(comp->anim_state, secondary_frame);
        } else {
            fprintf(stderr, "render: unknown secondary animation frame kind %u\n",
                secondary_frame->kind);
            abort();
        }
    } else {
        anim_apply_rest_pose(comp->anim_state, comp->base_vertices);
    }

    /* re-expand animated base verts into mesh vertex buffer */
    anim_update_mesh(comp->mesh.vertices, comp->anim_state,
                     comp->face_indices, comp->face_count);

    /* sanity clamp: catch degenerate animation frames that produce extreme
       vertex positions (int16_t overflow in animation math). without this,
       a single bad frame can create a screen-filling triangle. OSRS model
       coords are typically ±2000; 10000 is already way beyond any real model. */
    {
        int nv = comp->face_count * 3 * 3;
        for (int i = 0; i < nv; i++) {
            if (comp->mesh.vertices[i] > 10000.0f) comp->mesh.vertices[i] = 10000.0f;
            else if (comp->mesh.vertices[i] < -10000.0f) comp->mesh.vertices[i] = -10000.0f;
        }
    }

    /* upload updated vertices and colors to GPU */
    int exp_verts = comp->face_count * 3;
    UpdateMeshBuffer(comp->mesh, 0, comp->mesh.vertices,
                     exp_verts * 3 * sizeof(float), 0);
    if (comp->mesh.texcoords) {
        UpdateMeshBuffer(comp->mesh, 1, comp->mesh.texcoords,
                         exp_verts * 2 * sizeof(float), 0);
    }
    UpdateMeshBuffer(comp->mesh, 3, comp->mesh.colors,
                     exp_verts * 4, 0);

    /* draw with the current face count. CRITICAL: must set vertexCount on
       model.meshes[0], NOT comp->mesh — LoadModelFromMesh copies the mesh
       struct by value, so comp->mesh and model.meshes[0] are independent.
       DrawModel reads model.meshes[0].vertexCount for glDrawArrays count. */
    comp->model.meshes[0].vertexCount = exp_verts;
    comp->model.meshes[0].triangleCount = comp->face_count;
    comp->model.transform = transform;
    DrawModel(comp->model, (Vector3){ 0, 0, 0 }, 1.0f, WHITE);

    /* restore max counts so the VBO stays valid for next UpdateMeshBuffer */
    comp->model.meshes[0].vertexCount = COMPOSITE_MAX_EXP_VERTS;
    comp->model.meshes[0].triangleCount = COMPOSITE_MAX_FACES;
}

static void composite_free(PlayerComposite* comp) {
    if (comp->gpu_ready) {
        UnloadModel(comp->model);
        comp->gpu_ready = 0;
    }
    anim_model_state_free(comp->anim_state);
    comp->anim_state = NULL;
}


/**
 * Rebuild composite if equipment changed, run two-track animation, draw.
 *
 * Two-track animation system (matches OSRS client):
 *   - secondary: always running (idle/walk/run), loops forever
 *   - primary: triggered by actions (attack/cast/eat/block/death), plays
 *     once then expires. when active with interleave_order, overrides
 *     secondary for upper body groups.
 */
static void render_player_composite(
    RenderClient* rc, int player_idx, Matrix transform
) {
    if (!rc->model_cache) return;

    PlayerComposite* comp = &rc->composites[player_idx];
    RenderEntity* p = &rc->entities[player_idx];

    composite_init_gpu(comp, rc->model_cache);

    /* branch on entity type: NPCs use single-model composites */
    if (p->entity_type == ENTITY_NPC) {
        if (comp->needs_rebuild || comp->last_npc_def_id != p->npc_def_id) {
            composite_rebuild_npc(comp, rc->model_cache, rc->npc_model_cache, p->npc_def_id);
        }
    } else {
        if (comp->needs_rebuild ||
            memcmp(comp->last_equipped, p->equipped, NUM_GEAR_SLOTS) != 0) {
            composite_rebuild(comp, rc->model_cache, p);
        }
    }

    if ((!rc->anim_cache && !rc->npc_anim_cache) || !comp->anim_state) {
        /* no animation: draw static */
        if (comp->face_count > 0) {
            int exp_verts = comp->face_count * 3;
            comp->model.meshes[0].vertexCount = exp_verts;
            comp->model.meshes[0].triangleCount = comp->face_count;
            comp->model.transform = transform;
            DrawModel(comp->model, (Vector3){ 0, 0, 0 }, 1.0f, WHITE);
            comp->model.meshes[0].vertexCount = COMPOSITE_MAX_EXP_VERTS;
            comp->model.meshes[0].triangleCount = COMPOSITE_MAX_FACES;
        }
        return;
    }

    /* --- primary track: trigger new actions and expire finished ones ---
       primary is triggered per game tick (render_post_tick sets flags),
       but frame advancement happens in render_client_tick at 50 Hz.

       bug fix: when the same anim fires again after expiry (e.g. two
       consecutive whip attacks), we must restart it. check both seq_id
       change AND whether the current one has already finished (loops > 0). */
    int new_primary;
    if (p->entity_type == ENTITY_NPC) {
        /* NPCs set their animation via npc_anim_id from the encounter.
           idle is secondary (looping), attack/dive/surface are primary (play-once). */
        const NpcModelMapping* nm = npc_model_lookup((uint16_t)p->npc_def_id);
        int idle = nm ? (int)nm->idle_anim : -1;
        new_primary = (p->npc_anim_id >= 0 && p->npc_anim_id != idle)
            ? p->npc_anim_id : -1;
    } else {
        new_primary = render_select_primary(p);
    }
    if (new_primary >= 0) {
        int event_changed =
            rc->primary_event_tick[player_idx] !=
            rc->last_primary_event_tick[player_idx];
        int need_restart = (rc->anim[player_idx].primary_seq_id != new_primary) ||
                           (rc->anim[player_idx].primary_loops > 0) ||
                           (event_changed && new_primary != ANIM_SEQ_DEATH);
        if (need_restart) {
            rc->anim[player_idx].primary_seq_id = new_primary;
            rc->anim[player_idx].primary_frame_idx = 0;
            rc->anim[player_idx].primary_ticks = 0;
            rc->anim[player_idx].primary_loops = 0;
            rc->last_primary_event_tick[player_idx] =
                rc->primary_event_tick[player_idx];
        }
    }

    /* expire primary after one loop (death never expires) */
    if (rc->anim[player_idx].primary_seq_id >= 0 &&
        rc->anim[player_idx].primary_loops > 0 &&
        rc->anim[player_idx].primary_seq_id != ANIM_SEQ_DEATH) {
        rc->anim[player_idx].primary_seq_id = -1;
    }

    /* --- read current frame data (set by render_client_tick at 50 Hz) --- */
    RenderAnimTrackFrame sec_track = {0};
    RenderAnimTrackFrame pri_track = {0};

    /* secondary frame */
    if (rc->anim[player_idx].secondary_seq_id >= 0) {
        AnimSequence* seq = render_get_anim_sequence(
            rc, (uint16_t)rc->anim[player_idx].secondary_seq_id);
        sec_track = render_resolve_anim_track_frame(
            rc, seq, rc->anim[player_idx].secondary_frame_idx);
    }

    /* primary frame */
    if (rc->anim[player_idx].primary_seq_id >= 0) {
        AnimSequence* seq = render_get_anim_sequence(
            rc, (uint16_t)rc->anim[player_idx].primary_seq_id);
        pri_track = render_resolve_anim_track_frame(
            rc, seq, rc->anim[player_idx].primary_frame_idx);
    }

    /* --- resolve interleave_order from the primary sequence --- */
    const uint8_t* interleave = NULL;
    int interleave_count = 0;
    if (pri_track.sequence_frame) {
        AnimSequence* prim_seq = render_get_anim_sequence(
            rc, (uint16_t)rc->anim[player_idx].primary_seq_id);
        if (prim_seq && prim_seq->interleave_order) {
            interleave = prim_seq->interleave_order;
            interleave_count = prim_seq->interleave_count;
        }
    }

    /* --- animate and draw --- */
    composite_animate_and_draw(
        comp,
        sec_track.sequence_frame ? &sec_track.sequence_frame->frame : NULL,
        sec_track.framebase,
        pri_track.sequence_frame ? &pri_track.sequence_frame->frame : NULL,
        pri_track.framebase,
        interleave, interleave_count,
        transform);
}

static void render_draw_3d_world(RenderClient* rc) {
    /* tighten near/far clip planes for depth buffer precision.
       default 0.01/1000 = 100,000:1 ratio wastes precision and causes
       z-fighting across the entire scene. 0.5/500 = 1000:1 is sufficient
       for our tile-scale world (camera is never closer than ~1 tile). */
    rlSetClipPlanes(0.5, 500.0);

    Camera3D cam = render_build_3d_camera(rc);
    BeginMode3D(cam);

    /* terrain mesh (PvP wilderness) or flat ground plane (encounters) */
    if (rc->terrain && rc->terrain->loaded) {
        DrawModel(rc->terrain->model, (Vector3){ 0, 0, 0 }, 1.0f, WHITE);

        /* 3D collision overlay on terrain: semi-transparent quads at tile height */
        if (rc->show_collision && rc->collision_map) {
            for (int dx = 0; dx < rc->arena_width; dx++) {
                for (int dy = 0; dy < rc->arena_height; dy++) {
                    int wx = rc->arena_base_x + dx + rc->collision_world_offset_x;
                    int wy = rc->arena_base_y + dy + rc->collision_world_offset_y;
                    int flags = collision_get_flags(rc->collision_map, 0, wx, wy);

                    Color col = { 0, 0, 0, 0 };
                    if (flags & COLLISION_BLOCKED) {
                        col = CLITERAL(Color){ 200, 50, 50, 80 };
                    } else if (flags & COLLISION_BRIDGE) {
                        col = CLITERAL(Color){ 50, 120, 220, 80 };
                    } else if (flags & 0x0FF) {
                        col = CLITERAL(Color){ 220, 150, 40, 60 };
                    } else {
                        col = CLITERAL(Color){ 50, 200, 50, 40 };
                    }

                    float tx = (float)(rc->arena_base_x + dx);
                    float tz = -(float)(rc->arena_base_y + dy + 1);
                    /* sample terrain height at tile */
                    float ground = terrain_height_avg(rc->terrain,
                        rc->arena_base_x + dx, rc->arena_base_y + dy);
                    DrawCube((Vector3){ tx + 0.5f, ground + 0.05f, tz + 0.5f },
                             1.0f, 0.02f, 1.0f, col);
                }
            }
        }
    } else if (rc->npc_model_cache) {
        /* inferno: dark cave floor. all tiles are walkable ground. */
        float plat_y = 2.0f;
        for (int dx = 0; dx < rc->arena_width; dx++) {
            for (int dy = 0; dy < rc->arena_height; dy++) {
                float tx = (float)(rc->arena_base_x + dx);
                float tz = -(float)(rc->arena_base_y + dy + 1);

                /* volcanic rock with subtle variation — bright enough to distinguish from background */
                int shade = 45 + ((dx * 7 + dy * 13) % 15);
                int r = shade + ((dx * 3 + dy * 11) % 10);  /* slight reddish tint */
                Color c = { (unsigned char)r, (unsigned char)(shade - 3), (unsigned char)(shade - 6), 255 };
                DrawCube((Vector3){ tx + 0.5f, plat_y - 0.05f, tz + 0.5f },
                         1.0f, 0.1f, 1.0f, c);
            }
        }
    } else {
        /* zulrah / generic encounter: raised green platform over blue water.
           the real arena is instanced so it can't be exported from the cache. */
        float water_y = 1.5f;
        float plat_y = 2.0f;

        for (int dx = 0; dx < rc->arena_width; dx++) {
            for (int dy = 0; dy < rc->arena_height; dy++) {
                float tx = (float)(rc->arena_base_x + dx);
                float tz = -(float)(rc->arena_base_y + dy + 1);

                /* determine platform vs water: use collision map if available,
                   otherwise fall back to hardcoded platform bounds */
                int on_plat;
                if (rc->collision_map) {
                    int wx = rc->arena_base_x + dx + rc->collision_world_offset_x;
                    int wy = rc->arena_base_y + dy + rc->collision_world_offset_y;
                    on_plat = collision_tile_walkable(rc->collision_map, 0, wx, wy);
                } else {
                    on_plat = (dx >= ZUL_PLATFORM_MIN && dx <= ZUL_PLATFORM_MAX &&
                               dy >= ZUL_PLATFORM_MIN && dy <= ZUL_PLATFORM_MAX);
                }

                if (on_plat) {
                    int shade = 35 + ((dx * 7 + dy * 13) % 15);
                    Color c = { (unsigned char)shade, (unsigned char)(shade * 2), (unsigned char)shade, 255 };
                    DrawCube((Vector3){ tx + 0.5f, plat_y - 0.05f, tz + 0.5f },
                             1.0f, 0.1f, 1.0f, c);
                } else {
                    int shade = 15 + ((dx * 3 + dy * 5) % 10);
                    Color c = { (unsigned char)(shade / 2), (unsigned char)shade, (unsigned char)(shade * 3), 255 };
                    DrawCube((Vector3){ tx + 0.5f, water_y - 0.05f, tz + 0.5f },
                             1.0f, 0.1f, 1.0f, c);
                }
            }
        }
    }

    /* inferno pillars: "Rocky support" objects with 4 HP-level models.
       dynamically spawned (not in static objects file). */
    InfernoState* pillar_state = render_inferno_state_from_client(rc);
    if (rc->npc_model_cache && pillar_state) {
        InfernoState* is = pillar_state;
        float plat_y = 2.0f;
        float ms = 1.0f / 128.0f;
        for (int p = 0; p < INF_NUM_PILLARS; p++) {
            if (!is->pillars[p].active) continue;
            float hp_frac = (float)is->pillars[p].hp / (float)INF_PILLAR_HP;

            float cx = (float)is->pillars[p].x + INF_PILLAR_SIZE / 2.0f;
            float cz = -(float)(is->pillars[p].y + INF_PILLAR_SIZE / 2) - 0.5f;

            if (rc->pillar_models_ready) {
                /* select model by HP: 100%, 75%, 50%, 25% */
                int mi = 0;
                if (hp_frac <= 0.25f) mi = 3;
                else if (hp_frac <= 0.50f) mi = 2;
                else if (hp_frac <= 0.75f) mi = 1;

                rlDisableBackfaceCulling();
                rc->pillar_models[mi].transform = MatrixMultiply(
                    MatrixScale(-ms, ms, ms),
                    MatrixTranslate(cx, plat_y, cz));
                DrawModel(rc->pillar_models[mi], (Vector3){0,0,0}, 1.0f, WHITE);
                rlEnableBackfaceCulling();
            } else {
                /* fallback: colored DrawCube blocks */
                int base_r = (int)(140 * hp_frac + 180 * (1.0f - hp_frac));
                int base_g = (int)(130 * hp_frac + 40 * (1.0f - hp_frac));
                int base_b = (int)(100 * hp_frac + 20 * (1.0f - hp_frac));
                Color pillar_col = { (unsigned char)base_r, (unsigned char)base_g, (unsigned char)base_b, 240 };
                for (int dx = 0; dx < INF_PILLAR_SIZE; dx++) {
                    for (int dy = 0; dy < INF_PILLAR_SIZE; dy++) {
                        float tx = (float)(is->pillars[p].x + dx);
                        float tz2 = -(float)(is->pillars[p].y + dy + 1);
                        for (int h = 0; h < 3; h++) {
                            DrawCube((Vector3){ tx + 0.5f, plat_y + 0.5f + (float)h, tz2 + 0.5f },
                                     0.95f, 0.95f, 0.95f, pillar_col);
                        }
                    }
                }
            }
        }
    }

    /* debug: highlight the last raycast-selected tile */
    if (rc->show_debug && rc->debug_hit_wx >= 0) {
        float dtx = (float)rc->debug_hit_wx;
        float dtz = -(float)(rc->debug_hit_wy + 1);
        float dgy = rc->terrain
            ? terrain_height_avg(rc->terrain, rc->debug_hit_wx, rc->debug_hit_wy)
            : 2.0f;
        DrawCube((Vector3){ dtx + 0.5f, dgy + 0.02f, dtz + 0.5f },
                 1.0f, 0.02f, 1.0f, (Color){ 255, 0, 255, 180 });
        DrawSphere((Vector3){ rc->debug_ray_hit_x, rc->debug_ray_hit_y, rc->debug_ray_hit_z },
                   0.1f, RED);
    }
    /* debug: draw game-logic tile positions for all entities.
       green = player, cyan = NPCs. shows where the game thinks entities are
       vs where the 3D model renders (which uses sub_x/sub_y interpolation). */
    if (rc->show_debug) {
        for (int i = 0; i < rc->entity_count; i++) {
            RenderEntity* ep = &rc->entities[i];
            if (ep->entity_type == ENTITY_NPC && !ep->npc_visible) continue;
            float tx = (float)ep->x;
            float ty = (float)ep->y;
            float tz = -(ty + 1.0f);
            float ground = rc->terrain
                ? terrain_height_avg(rc->terrain, ep->x, ep->y) : 2.0f;
            int sz = ep->npc_size > 1 ? ep->npc_size : 1;
            Color col = (ep->entity_type == ENTITY_PLAYER)
                ? CLITERAL(Color){ 0, 255, 0, 100 }
                : CLITERAL(Color){ 0, 200, 255, 80 };
            for (int dx = 0; dx < sz; dx++) {
                for (int dy = 0; dy < sz; dy++) {
                    float mx = tx + (float)dx;
                    float mz = tz - (float)dy;
                    Color tile_col = col;
                    if (ep->entity_type == ENTITY_NPC && dx == 0 && dy == 0)
                        tile_col = CLITERAL(Color){ 255, 230, 40, 150 };
                    DrawCube((Vector3){ mx + 0.5f, ground + 0.08f, mz + 0.5f },
                             0.9f, 0.04f, 0.9f, tile_col);
                }
            }
        }
    }

    /* entity click hitboxes are now drawn as 2D convex hulls after EndMode3D */

    /* encounter overlay: drawn on top of terrain or procedural arena */
    {
        EncounterOverlay* ov = &rc->encounter_overlay;
        int has_terrain = rc->terrain && rc->terrain->loaded;

        /* helper: get ground height at a tile position */
        #define OV_GROUND(tile_x, tile_y) \
            (has_terrain ? terrain_height_avg(rc->terrain, (tile_x), (tile_y)) : 2.0f)

        /* current hazard renderer: object 11700 centered on a 3x3 damage area */
        float ms = 1.0f / 128.0f;
        for (int i = 0; i < ov->tile_shadow_count; i++) {
            EncounterTileShadow* shadow = &ov->tile_shadows[i];
            if (!shadow->active) continue;
            float ground = OV_GROUND(shadow->x, shadow->y);
            float cx = (float)shadow->x + 0.5f;
            float cz = -(float)(shadow->y + 1) + 0.5f;
            float radius = 0.45f * shadow->scale;
            unsigned char alpha = (unsigned char)(70.0f + 70.0f * shadow->scale);
            DrawCylinder(
                (Vector3){ cx, ground + 0.045f, cz },
                radius,
                radius,
                0.025f,
                36,
                CLITERAL(Color){ 8, 6, 4, alpha });
        }

        if (ov->floating_model_count > 0) {
            render_load_projectile_assets(rc);
        }
        /* colosseum-only: manticore orbs bob + spin so the telegraph reads as
           a live hovering projectile rather than a frozen model. Gated on the
           encounter name; no other encounter emits floating models, and this
           branch is never taken for inferno/zulrah/pvp. */
        int floating_bob_spin = 0;
        if (rc->gui.encounter_def) {
            const EncounterDef* fm_def = (const EncounterDef*)rc->gui.encounter_def;
            floating_bob_spin = (strcmp(fm_def->name, "colosseum") == 0);
        }
        float floating_anim_t = (float)rc->effect_client_tick_counter;
        for (int i = 0; i < ov->floating_model_count; i++) {
            EncounterFloatingModel* floating = &ov->floating_models[i];
            if (!floating->active) continue;
            float anchor_x = 0.0f;
            float anchor_y = 0.0f;
            if (!render_resolve_projectile_anchor(
                    rc, floating->anchor_kind, floating->npc_slot,
                    (float)floating->x, (float)floating->y,
                    &anchor_x, &anchor_y)) {
                continue;
            }
            Model* model = render_get_proj_model(rc, floating->model_id);
            float ground = OV_GROUND((int)anchor_x, (int)anchor_y);
            Vector3 pos = {
                anchor_x + 0.5f + floating->lateral_offset,
                ground + floating->height_offset,
                -(anchor_y + 1.0f) + 0.5f
            };
            /* a stacked telegraph orb is attached to the manticore, so it inherits
               the live body facing and turns rigidly with it (matches the game,
               where waiting orbs face the NPC front until launched). */
            float npc_yaw = 0.0f;
            if (floating->anchor_kind == ENCOUNTER_PROJECTILE_TARGET_NPC_SLOT) {
                int npc_idx = render_find_npc_entity_idx(rc, floating->npc_slot);
                if (npc_idx >= 0) npc_yaw = rc->yaw[npc_idx];
            }
            float model_scale = (floating->scale > 0.0f ? floating->scale : 1.0f) / 128.0f;
            float spin = 0.0f;
            if (floating_bob_spin) {
                float bob_phase = floating_anim_t * (2.0f * PI / 100.0f)
                    + (float)i * 1.7f;
                pos.y += 0.08f * sinf(bob_phase);
                spin = floating_anim_t * (2.0f * PI / 150.0f);
            }
            /* manticore orbs spin on the axis the real game uses per style: the
               melee orb (model 51213) pitches toward its front (X), the magic orb
               (51215) yaws like a spinning top (Y), the ranged orb (51221) rolls
               (Z). Any other floating model keeps the default yaw. */
            Matrix spin_rot;
            switch (floating->model_id) {
                case 51213u: spin_rot = MatrixRotateX(spin); break;
                case 51221u: spin_rot = MatrixRotateZ(spin); break;
                default:     spin_rot = MatrixRotateY(spin); break;
            }
            rlDisableBackfaceCulling();
            model->transform = MatrixMultiply(
                MatrixMultiply(
                    MatrixMultiply(
                        MatrixScale(-model_scale, model_scale, model_scale),
                        MatrixRotateY(npc_yaw)),
                    spin_rot),
                MatrixTranslate(pos.x, pos.y, pos.z));
            DrawModel(*model, (Vector3){0,0,0}, 1.0f, WHITE);
            rlEnableBackfaceCulling();
        }

        for (int i = 0; i < ov->hazard_count; i++) {
            if (!ov->hazards[i].active) continue;
            float ground = OV_GROUND(ov->hazards[i].x + 1, ov->hazards[i].y + 1);

            if (rc->cloud_model_ready) {
                float cx = (float)ov->hazards[i].x + 1.5f;
                float cz = -(float)(ov->hazards[i].y + 2) + 0.5f;
                rlDisableBackfaceCulling();
                rc->cloud_model.transform = MatrixMultiply(
                    MatrixScale(ms, ms, ms),
                    MatrixTranslate(cx, ground + 0.1f, cz));
                DrawModel(rc->cloud_model, (Vector3){0,0,0}, 1.0f, WHITE);
                rlEnableBackfaceCulling();
            } else {
                /* fallback: semi-transparent tiles if model not loaded */
                for (int cdx = 0; cdx < 3; cdx++) {
                    for (int cdy = 0; cdy < 3; cdy++) {
                        float fx = (float)(ov->hazards[i].x + cdx);
                        float fz = -(float)(ov->hazards[i].y + cdy + 1);
                        float tg = OV_GROUND(ov->hazards[i].x + cdx, ov->hazards[i].y + cdy);
                        DrawCube((Vector3){ fx + 0.5f, tg + 0.08f, fz + 0.5f },
                                 0.95f, 0.06f, 0.95f,
                                 CLITERAL(Color){ 80, 180, 50, 100 });
                    }
                }
            }
        }

        /* boss hitbox: NxN form-colored tiles on the ground */
        if (rc->show_debug && ov->boss_visible && ov->boss_size > 0) {
            Color form_col;
            switch (ov->boss_form) {
                case 0: form_col = CLITERAL(Color){ 50, 200, 50, 80 }; break;  /* green */
                case 1: form_col = CLITERAL(Color){ 200, 50, 50, 80 }; break;  /* red */
                case 2: form_col = CLITERAL(Color){ 50, 100, 255, 80 }; break; /* blue */
                default: form_col = CLITERAL(Color){ 200, 200, 200, 80 }; break;
            }
            Color border_col = form_col;
            border_col.a = 200;
            int sz = ov->boss_size;
            for (int bx = 0; bx < sz; bx++) {
                for (int by = 0; by < sz; by++) {
                    int tx = ov->boss_x + bx;
                    int ty = ov->boss_y + by;
                    float ground = OV_GROUND(tx, ty);
                    float fx = (float)tx;
                    float fz = -(float)(ty + 1);
                    DrawCube((Vector3){ fx + 0.5f, ground + 0.04f, fz + 0.5f },
                             1.0f, 0.02f, 1.0f, form_col);
                }
            }
            /* border outline */
            float x0 = (float)ov->boss_x;
            float x1 = (float)(ov->boss_x + sz);
            float z0 = -(float)(ov->boss_y + sz);
            float z1 = -(float)ov->boss_y;
            float border_y = OV_GROUND(ov->boss_x + sz/2, ov->boss_y + sz/2) + 0.06f;
            DrawLine3D((Vector3){x0, border_y, z0}, (Vector3){x1, border_y, z0}, border_col);
            DrawLine3D((Vector3){x1, border_y, z0}, (Vector3){x1, border_y, z1}, border_col);
            DrawLine3D((Vector3){x1, border_y, z1}, (Vector3){x0, border_y, z1}, border_col);
            DrawLine3D((Vector3){x0, border_y, z1}, (Vector3){x0, border_y, z0}, border_col);
        }

        /* colosseum molten pools (Reentry / Volatility T3): orange-red ground
           decals on each damaging sand tile. Gated colosseum-only by encounter
           name; reads ColosseumState directly like the zulrah safe-spot block
           below. Does NOT touch any shared overlay channel, so inferno/zulrah/
           pvp rendering is unchanged. Tile->world mapping matches the boss
           hitbox decal block above. */
        {
            const EncounterDef* edef_molten =
                (const EncounterDef*)rc->gui.encounter_def;
            if (edef_molten && rc->gui.encounter_state &&
                    strcmp(edef_molten->name, "colosseum") == 0) {
                ColosseumState* cs_molten = (ColosseumState*)rc->gui.encounter_state;
                /* one-time lazy load of the molten-sand pool mesh (OSRS spotanim
                   GFX 3080 -> synthetic projectile model id 0xA2000000 + 3080).
                   render_build_static_model is non-aborting: if the mesh is not
                   in the loaded caches (e.g. run from a checkout that never
                   exported it) molten_model_ready stays 0 and we fall back to the
                   flat orange tile, so the viewer degrades gracefully. */
                if (!rc->molten_model_attempted) {
                    rc->molten_model_attempted = 1;
                    render_load_projectile_assets(rc);
                    rc->molten_model_ready = render_build_static_model(
                        rc->projectile_model_cache,
                        0xA2000000u + 3080u,
                        &rc->molten_model);
                }
                /* render both hazard sources with one mesh: the wave 1-11
                   Reentry/Volatility pools (molten_*) AND the Sol-fight molten
                   tiles (sol.hazard_*), which were previously not drawn at all. */
                const int* molten_xs[2] = { cs_molten->molten_x, cs_molten->sol.hazard_tile_x };
                const int* molten_ys[2] = { cs_molten->molten_y, cs_molten->sol.hazard_tile_y };
                int molten_counts[2] = { cs_molten->molten_count, cs_molten->sol.hazard_tile_count };
                for (int src = 0; src < 2; src++) {
                    int molten_n = molten_counts[src];
                    if (molten_n > COLO_SOL_HAZARD_TILES_MAX)
                        molten_n = COLO_SOL_HAZARD_TILES_MAX;
                    for (int mi = 0; mi < molten_n; mi++) {
                        int tx = molten_xs[src][mi];
                        int ty = molten_ys[src][mi];
                        float ground = OV_GROUND(tx, ty);
                        float fx = (float)tx + 0.5f;
                        float fz = -(float)(ty + 1) + 0.5f;
                        if (rc->molten_model_ready) {
                            /* scale maps OSRS model units (128/tile) to tiles; the
                               exact pool footprint may want tuning after a look. */
                            rlDisableBackfaceCulling();
                            rc->molten_model.transform = MatrixMultiply(
                                MatrixScale(-1.0f / 128.0f, 1.0f / 128.0f, 1.0f / 128.0f),
                                MatrixTranslate(fx, ground + 0.02f, fz));
                            DrawModel(rc->molten_model, (Vector3){0,0,0}, 1.0f, WHITE);
                            rlEnableBackfaceCulling();
                        } else {
                            DrawCube((Vector3){ fx, ground + 0.05f, fz },
                                     0.95f, 0.04f, 0.95f,
                                     CLITERAL(Color){ 220, 90, 30, 150 });
                        }
                    }
                }
            }
        }

        /* melee targeting indicator: red tile where boss is aiming */
        if (ov->melee_target_active) {
            float ground = OV_GROUND(ov->melee_target_x, ov->melee_target_y);
            float mx = (float)ov->melee_target_x;
            float mz = -(float)(ov->melee_target_y + 1);
            DrawCube((Vector3){ mx + 0.5f, ground + 0.06f, mz + 0.5f },
                     1.0f, 0.04f, 1.0f,
                     CLITERAL(Color){ 255, 50, 50, 150 });
        }

        /* encounter adds: current renderer uses the snakeling model or cubes. */
        for (int i = 0; i < ov->add_count; i++) {
            if (!ov->adds[i].active) continue;
            float ground = OV_GROUND(ov->adds[i].x, ov->adds[i].y);
            float sx = (float)ov->adds[i].x + 0.5f;
            float sz = -(float)(ov->adds[i].y + 1) + 0.5f;
            if (rc->snakeling_model_ready) {
                rlDisableBackfaceCulling();
                rc->snakeling_model.transform = MatrixMultiply(
                    MatrixScale(ms, ms, ms),
                    MatrixTranslate(sx, ground, sz));
                DrawModel(rc->snakeling_model, (Vector3){0,0,0}, 1.0f, WHITE);
                rlEnableBackfaceCulling();
            } else {
                Color sc = ov->adds[i].variant
                    ? CLITERAL(Color){ 100, 150, 255, 200 }
                    : CLITERAL(Color){ 255, 150, 50, 200 };
                DrawCube((Vector3){ sx, ground + 0.2f, sz },
                         0.6f, 0.3f, 0.6f, sc);
            }
        }

        /* projectiles: render in-flight projectiles with interpolated positions.
           flight_spawn() creates flights from overlay events (in render_post_tick),
           flight_client_tick() advances progress at 50Hz, we just draw here. */
        for (int i = 0; i < MAX_FLIGHT_PROJECTILES; i++) {
            FlightProjectile* fp = &rc->flights[i];
            if (!fp->active || fp->start_delay > 0) continue;

            float src_ground = OV_GROUND((int)fp->src_x, (int)fp->src_y);
            float dst_ground = OV_GROUND((int)fp->dst_x, (int)fp->dst_y);
            Vector3 pos = flight_get_position(fp, src_ground, dst_ground);

            Model* proj_model = NULL;
            if (fp->anim_id >= 0 && (fp->model_id > 0 || fp->travel_gfx_drives_model)) {
                OsrsModel* om = render_get_flight_osrs_model(rc, fp);
                AnimSequence* seq = render_get_anim_sequence(rc, (uint16_t)fp->anim_id);
                if (!fp->anim_state || !seq || seq->frame_count <= 0 || !om->face_indices) {
                    fprintf(stderr, "render: projectile model %u cannot render animation %d\n",
                            fp->model_id, fp->anim_id);
                    abort();
                }
                if (fp->anim_frame >= seq->frame_count) fp->anim_frame = 0;
                render_apply_anim_sequence_frame_to_model_state(
                    rc, fp->anim_state, om, seq, fp->anim_frame,
                    "projectile");
                proj_model = &om->model;
            } else if (fp->travel_gfx_drives_model) {
                OsrsModel* om = render_get_flight_osrs_model(rc, fp);
                proj_model = &om->model;
            } else if (fp->model_id > 0) {
                proj_model = render_get_proj_model(rc, fp->model_id);
            }
            if (!proj_model) {
                /* model_id 0 intentionally falls back to the generic style mesh */
                if (fp->style == 0 && rc->ranged_proj_model_ready)
                    proj_model = &rc->ranged_proj_model;
                else if (fp->style == 1 && rc->magic_proj_model_ready)
                    proj_model = &rc->magic_proj_model;
                else if (fp->style == 3 && rc->cloud_proj_model_ready)
                    proj_model = &rc->cloud_proj_model;
                else if (fp->style == 4 && rc->ranged_proj_model_ready)
                    proj_model = &rc->ranged_proj_model;  /* spawn orb reuses ranged mesh */
            }

            if (proj_model) {
                rlDisableBackfaceCulling();
                float pms = 1.0f / 128.0f;
                proj_model->transform = render_projectile_transform_offset(
                    pms, pms, pms, fp->yaw, fp->pitch, pos,
                    fp->offset_x, fp->offset_y, fp->offset_z);
                DrawModel(*proj_model, (Vector3){0,0,0}, 1.0f, WHITE);
                rlEnableBackfaceCulling();
            }

            /* trail line from source to current position */
            if (rc->show_debug &&
                fp->motion_mode != ENCOUNTER_PROJECTILE_MOTION_TARGET_ANCHORED) {
                Color pc;
                switch (fp->style) {
                    case 0: pc = CLITERAL(Color){ 80, 220, 80, 150 }; break;
                    case 1: pc = CLITERAL(Color){ 80, 130, 255, 150 }; break;
                    case 2: pc = CLITERAL(Color){ 255, 80, 80, 150 }; break;
                    case 3: pc = CLITERAL(Color){ 50, 180, 50, 150 }; break;
                    case 4: pc = CLITERAL(Color){ 230, 230, 230, 150 }; break;
                    default: pc = WHITE; break;
                }
                Vector3 src_pos = { fp->src_x + 0.5f, src_ground + fp->start_height,
                                   -(fp->src_y + 1.0f) + 0.5f };
                DrawLine3D(src_pos, pos, pc);
                DrawSphere(pos, 0.12f, pc);
            }
        }

        /* safe spot markers: colored quads on ground at each stand location */
        if (rc->show_safe_spots && rc->gui.encounter_state) {
            const EncounterDef* edef_ss = (const EncounterDef*)rc->gui.encounter_def;
            if (edef_ss && strcmp(edef_ss->name, "zulrah") == 0) {
                ZulrahState* zs_ss = (ZulrahState*)rc->gui.encounter_state;
                const ZulRotationPhase* phase_ss = zul_current_phase(zs_ss);
                int act_stand = phase_ss->stand;
                int act_stall = phase_ss->stall;

                for (int si = 0; si < ZUL_NUM_STAND_LOCATIONS; si++) {
                    int lx = ZUL_STAND_COORDS[si][0];
                    int ly = ZUL_STAND_COORDS[si][1];
                    float sx = (float)(rc->arena_base_x + lx) + 0.5f;
                    float sz = -(float)(rc->arena_base_y + ly + 1) + 0.5f;
                    float gy = OV_GROUND(rc->arena_base_x + lx, rc->arena_base_y + ly);

                    Color col;
                    if (si == act_stand)
                        col = (Color){0, 255, 0, 160};
                    else if (si == act_stall)
                        col = (Color){255, 255, 0, 160};
                    else
                        col = (Color){0, 180, 180, 80};

                    DrawCube((Vector3){ sx, gy + 0.08f, sz },
                             0.7f, 0.04f, 0.7f, col);
                }
            }
        }
        render_inferno_lab_draw_forecast_3d(rc);

        #undef OV_GROUND
    }

    /* placed objects — disable backface culling since OSRS uses flat
       billboard-style quads for trees/plants (two crossing planes) */
    {
        /* use post-Zuk objects (prison walls removed) when Zuk is present */
        ObjectMesh* obj = (rc->objects_zuk && rc->objects_zuk->loaded && rc->zuk_active)
            ? rc->objects_zuk : rc->objects;
        if (obj && obj->loaded) {
            rlDisableBackfaceCulling();
            DrawModel(obj->model, (Vector3){ 0, 0, 0 }, 1.0f, WHITE);
            rlEnableBackfaceCulling();
        }
    }

    /* NPC models at spawn positions */
    if (rc->npcs && rc->npcs->loaded) {
        rlDisableBackfaceCulling();
        DrawModel(rc->npcs->model, (Vector3){ 0, 0, 0 }, 1.0f, WHITE);
        rlEnableBackfaceCulling();
    }

    /* entity 3D models: composite body + equipment, animated as one unit */
    if (rc->model_cache) {
        float ms = 1.0f / 128.0f;

        /* OSRS models are one-sided: the client culls every face by screen-space
           winding (Model.draw0). Our mesh applies two reflections before draw
           (anim_update_mesh writes x,-y,z then the model matrix mirrors x via
           MatrixScale(-ms,ms,ms)), which preserves handedness, so the standard
           back-face cull is correct. Culling the front side inverts every model. */
        rlEnableBackfaceCulling();
        rlSetCullFace(RL_CULL_FACE_BACK);
        for (int i = 0; i < rc->entity_count; i++) {
            RenderEntity* ep = &rc->entities[i];

            /* skip invisible NPCs (diving, dead, etc.) */
            if (ep->entity_type == ENTITY_NPC && !ep->npc_visible) continue;

            /* hide opponent player when stacked on the camera-followed tile
               (real OSRS draws only the local player when stacked) */
            if (ep->entity_type == ENTITY_PLAYER && i != rc->gui.gui_entity_idx) {
                int fi = rc->gui.gui_entity_idx;
                if (fi >= 0 && fi < rc->entity_count &&
                        ep->x == rc->entities[fi].x &&
                        ep->y == rc->entities[fi].y) {
                    continue;
                }
            }

            float px, pz, ground;
            render_get_visual_pos(rc, i, &px, &pz, &ground);
            float model_ground = osrs_render_entity_model_ground(ground);

            /* negate X scale to fix model mirroring: OSRS models are authored
               in a left-handed coordinate system but we render in right-handed
               (raylib/OpenGL). negating X flips the handedness so weapons
               appear in the correct (right) hand. */
            Matrix base = MatrixScale(-ms, ms, ms);
            base = MatrixMultiply(base, MatrixRotateY(rc->yaw[i]));
            base = MatrixMultiply(base, MatrixTranslate(px, model_ground, pz));

            /* rebuild composite if equipment changed, animate, upload, draw */
            render_player_composite(rc, i, base);


            /* project animated mesh vertices to 2D screen for convex hull click detection.
               ported from RuneLite RSModelMixin.getConvexHull → Perspective.modelToCanvas. */
            PlayerComposite* comp = &rc->composites[i];
            Camera3D hull_cam = render_build_3d_camera(rc);
            int nv = comp->face_count * 3;  /* actual used verts, not pre-allocated capacity */
            int hull_n = 0;
            float min_model_y = 1000000.0f;
            float max_model_y = -1000000.0f;
            int hull_xs[RENDER_CLICK_HULL_MAX_INPUT_POINTS];
            int hull_ys[RENDER_CLICK_HULL_MAX_INPUT_POINTS];
            for (int vi = 0; vi < nv; vi++) {
                float vy = comp->mesh.vertices[vi * 3 + 1];
                if (vy < min_model_y) min_model_y = vy;
                if (vy > max_model_y) max_model_y = vy;
            }
            float visual_height_tiles = 0.0f;
            if (nv > 0 && min_model_y <= max_model_y) {
                visual_height_tiles = (max_model_y - min_model_y) * ms;
                rc->entity_visual_mid_y[i] = model_ground + min_model_y * ms
                    + (max_model_y - min_model_y) * ms * 0.5f;
                rc->entity_visual_top_y[i] = model_ground + max_model_y * ms;
            } else {
                int ent_size = (ep->entity_type == ENTITY_NPC && ep->npc_size > 1) ? ep->npc_size : 1;
                rc->entity_visual_mid_y[i] = model_ground + 0.75f + 0.25f * (float)ent_size;
                rc->entity_visual_top_y[i] = model_ground + 1.5f + 0.5f * (float)ent_size;
                visual_height_tiles = (float)ent_size;
            }
            for (int vi = 0; vi < nv; vi++) {
                float vx = comp->mesh.vertices[vi * 3 + 0];
                float vy = comp->mesh.vertices[vi * 3 + 1];
                float vz = comp->mesh.vertices[vi * 3 + 2];
                Vector3 wv = Vector3Transform((Vector3){ vx, vy, vz }, base);
                hull_append_projected_world_point(&hull_cam, wv,
                    hull_xs, hull_ys, &hull_n, RENDER_CLICK_HULL_MAX_INPUT_POINTS);
            }
            Vector3 clickbox_points[RENDER_CLICKBOX_PRISM_POINT_COUNT];
            int clickbox_n = render_build_entity_clickbox_prism_points(
                ep, px, pz, model_ground, visual_height_tiles,
                clickbox_points, RENDER_CLICKBOX_PRISM_POINT_COUNT);
            for (int ci = 0; ci < clickbox_n; ci++) {
                hull_append_projected_world_point(&hull_cam, clickbox_points[ci],
                    hull_xs, hull_ys, &hull_n, RENDER_CLICK_HULL_MAX_INPUT_POINTS);
            }
            hull_compute(hull_xs, hull_ys, hull_n, &rc->entity_hulls[i]);
        }
        rlEnableBackfaceCulling();
    }

    /* visual effects: spell impacts, projectiles */
    if (rc->model_cache || rc->projectile_model_cache) {
        rlDisableBackfaceCulling();
        float eff_scale = 1.0f / 128.0f;
        int eff_ct = rc->effect_client_tick_counter;

        for (int i = 0; i < MAX_ACTIVE_EFFECTS; i++) {
            ActiveEffect* e = &rc->effects[i];
            if (e->type == EFFECT_NONE) continue;
            if (!e->meta) continue;

            /* look up model */
            OsrsModel* om = effect_find_model(e->meta, rc->model_cache,
                rc->npc_model_cache, rc->projectile_model_cache);
            if (!om) continue;

            /* position: sub-tile coords -> tile coords -> raylib world */
            float ex = (float)(e->cur_x / 128.0);
            float ez = -(float)(e->cur_y / 128.0);
            float ground = rc->terrain
                ? terrain_height_avg(rc->terrain, (int)ex, (int)(e->cur_y / 128.0))
                : 2.0f;
            float ey = ground + (float)(e->height / 128.0);

            /* apply scale from spotanim def */
            float scale_xy = eff_scale * (float)e->meta->resize_xy / 128.0f;
            float scale_y = eff_scale * (float)e->meta->resize_z / 128.0f;

            /* animate: apply current frame to per-effect anim state,
               then write transformed vertices into the shared mesh.
               note: this temporarily modifies the shared OsrsModel mesh,
               which is fine since effects render sequentially. */
            if (e->anim_state && e->meta->animation_id >= 0 && rc->anim_cache
                && om->face_indices) {
                AnimSequence* seq = render_get_anim_sequence(rc, e->meta->animation_id);
                if (seq && e->anim_frame < seq->frame_count) {
                    render_apply_anim_sequence_frame_to_model_state(
                        rc, e->anim_state, om, seq, e->anim_frame,
                        "spotanim");
                }
            }

            /* build transform */
            Matrix t;

            /* projectile orientation: yaw + pitch from trajectory direction.
               uses atan2 on the velocity vector (same approach as the flight
               system) to orient the model from source toward target. */
            if (e->type == EFFECT_PROJECTILE && e->started) {
                OsrsProjectileOrientation orientation =
                    osrs_projectile_orientation_from_step(
                        (float)e->x_increment,
                        (float)e->y_increment,
                        (float)e->height_increment);
                t = render_projectile_transform(scale_xy, scale_y, scale_xy,
                    orientation.yaw, orientation.pitch,
                    (Vector3){ ex, ey, ez });
            } else {
                t = MatrixMultiply(
                    MatrixScale(-scale_xy, scale_y, scale_xy),
                    MatrixTranslate(ex, ey, ez));
            }
            om->model.transform = t;

            /* spotanim fade: 20% fade in, 60% full, 20% fade out */
            Color tint = WHITE;
            if (e->type == EFFECT_SPOTANIM && e->stop_tick > e->start_tick) {
                int total = e->stop_tick - e->start_tick;
                int elapsed = eff_ct - e->start_tick;
                float progress = (float)elapsed / (float)total;
                float alpha = 1.0f;
                if (progress < 0.2f) alpha = progress / 0.2f;
                else if (progress > 0.8f) alpha = (1.0f - progress) / 0.2f;
                if (alpha < 0.0f) alpha = 0.0f;
                if (alpha > 1.0f) alpha = 1.0f;
                tint = (Color){ 255, 255, 255, (unsigned char)(alpha * 255) };
            }
            DrawModel(om->model, (Vector3){ 0, 0, 0 }, 1.0f, tint);
        }
        rlEnableBackfaceCulling();
    }

    /* fight area boundary wireframe (Z negated) */
    float fa_x = (float)rc->arena_base_x;
    float fa_z = -(float)rc->arena_base_y;
    float fa_w = (float)rc->arena_width;
    float fa_h = -(float)rc->arena_height;  /* negative because Z is negated */
    float bh = rc->terrain ? terrain_height_at(rc->terrain, rc->arena_base_x, rc->arena_base_y) : 2.0f;
    DrawLine3D(
        (Vector3){ fa_x, bh, fa_z },
        (Vector3){ fa_x + fa_w, bh, fa_z }, YELLOW);
    DrawLine3D(
        (Vector3){ fa_x + fa_w, bh, fa_z },
        (Vector3){ fa_x + fa_w, bh, fa_z + fa_h }, YELLOW);
    DrawLine3D(
        (Vector3){ fa_x + fa_w, bh, fa_z + fa_h },
        (Vector3){ fa_x, bh, fa_z + fa_h }, YELLOW);
    DrawLine3D(
        (Vector3){ fa_x, bh, fa_z + fa_h },
        (Vector3){ fa_x, bh, fa_z }, YELLOW);

    /* click cross is now drawn as 2D overlay in pvp_render, not in 3D world */

    /* debug: player→NPC LOS lines (green=can attack, red=blocked/out of range) */
    InfernoState* debug_inferno_state = render_inferno_state_from_client(rc);
    if (rc->show_debug && rc->entity_count > 0) {
        int player_idx = rc->gui.gui_entity_idx;
        if (player_idx < 0 || player_idx >= rc->entity_count ||
                rc->entities[player_idx].entity_type != ENTITY_PLAYER) {
            player_idx = -1;
            for (int i = 0; i < rc->entity_count; i++) {
                if (rc->entities[i].entity_type == ENTITY_PLAYER) {
                    player_idx = i;
                    break;
                }
            }
        }

        if (player_idx >= 0) {
            RenderEntity* player = &rc->entities[player_idx];
            float player_x, player_z, player_ground;
            render_get_visual_pos(rc, player_idx, &player_x, &player_z, &player_ground);
            float player_y = rc->entity_visual_mid_y[player_idx] > player_ground
                ? rc->entity_visual_mid_y[player_idx]
                : player_ground + 1.0f;

            for (int i = 0; i < rc->entity_count; i++) {
                if (i == player_idx) continue;
                RenderEntity* target = &rc->entities[i];
                if (target->entity_type == ENTITY_NPC && !target->npc_visible) continue;

                int is_target = target->entity_type == ENTITY_NPC ||
                    player->attack_target_entity_idx == i;
                if (!is_target) continue;

                Color lc = GREEN;
                if (debug_inferno_state && target->entity_type == ENTITY_NPC) {
                    int slot = target->npc_slot;
                    if (slot < 0 || slot >= INF_MAX_NPCS) continue;
                    InfNPC* npc = &debug_inferno_state->npcs[slot];
                    if (!npc->active || npc->death_ticks > 0) continue;
                    const EncounterLoadoutStats* ls =
                        &debug_inferno_state->loadout_stats[debug_inferno_state->weapon_set];
                    OsrsLosQuery los_query = osrs_los_blockers(
                        debug_inferno_state->los_blockers,
                        debug_inferno_state->los_blocker_count);
                    int can_atk = encounter_player_can_attack(
                        debug_inferno_state->player.x,
                        debug_inferno_state->player.y,
                        npc->x, npc->y, npc->size,
                        ls->attack_range,
                        &los_query);
                    lc = can_atk ? GREEN : RED;
                }

                float target_x, target_z, target_ground;
                render_get_visual_pos(rc, i, &target_x, &target_z, &target_ground);
                float target_y = rc->entity_visual_mid_y[i] > target_ground
                    ? rc->entity_visual_mid_y[i]
                    : target_ground + 1.0f;
                float line_y = player_y < target_y ? player_y : target_y;
                DrawLine3D(
                    (Vector3){ player_x, line_y, player_z },
                    (Vector3){ target_x, line_y, target_z },
                    lc);
            }
        }
    }

    /* hover tile outline: semi-transparent cyan border on the tile under cursor.
       similar to RuneLite's "Tile Indicators" plugin. drawn as 4 lines slightly
       above ground to avoid z-fighting with terrain/floor. */
    if (rc->hover_tile_x >= 0) {
        float htx = (float)rc->hover_tile_x;
        float htz = -(float)(rc->hover_tile_y + 1);
        float hgy = rc->terrain
            ? terrain_height_avg(rc->terrain, rc->hover_tile_x, rc->hover_tile_y)
            : 2.0f;
        float hy = hgy + 0.03f;  /* slight offset above ground */
        Color hcol = CLITERAL(Color){ 0, 220, 220, 180 };
        DrawLine3D((Vector3){ htx,       hy, htz },       (Vector3){ htx + 1.0f, hy, htz },       hcol);
        DrawLine3D((Vector3){ htx + 1.0f, hy, htz },       (Vector3){ htx + 1.0f, hy, htz + 1.0f }, hcol);
        DrawLine3D((Vector3){ htx + 1.0f, hy, htz + 1.0f }, (Vector3){ htx,       hy, htz + 1.0f }, hcol);
        DrawLine3D((Vector3){ htx,       hy, htz + 1.0f }, (Vector3){ htx,       hy, htz },       hcol);
    }

    EndMode3D();
}


/**
 * Draw overhead prayer icons and HP bars above players in 3D mode.
 *
 * Layout matches OSRS client (Client.java:6011-6049):
 * - HP bar: 30px wide, 5px tall, green fill + red remainder.
 * - Prayer icon: drawn above the HP bar.
 *
 * Both use the OSRS actor height + 15 unit projection anchor.
 */
static void render_draw_overhead_status(RenderClient* rc, OsrsEnv* env) {
    Camera3D cam = render_build_3d_camera(rc);
    InfernoState* debug_state = render_inferno_state_from_client(rc);

    /* map our OverheadPrayer enum → OSRS headIcon sprite index */
    static const int prayer_to_headicon[] = {
        -1, /* PRAYER_NONE */
         2, /* PRAYER_PROTECT_MAGIC  → headIcon 2 (magic) */
         1, /* PRAYER_PROTECT_RANGED → headIcon 1 (ranged) */
         0, /* PRAYER_PROTECT_MELEE  → headIcon 0 (melee) */
         4, /* PRAYER_SMITE          → headIcon 4 (smite) */
         5, /* PRAYER_REDEMPTION     → headIcon 5 (redemption) */
    };

    for (int i = 0; i < rc->entity_count; i++) {
        RenderEntity* p = &rc->entities[i];

        /* skip invisible NPCs */
        if (p->entity_type == ENTITY_NPC && !p->npc_visible) continue;

        /* project entity positions to screen coordinates.
           OSRS draws splats at entity.height/2 (abdomen), HP bar + prayer at top.
           head height scales with NPC size — larger models need higher overhead bars.
           approximate: model height in tiles ~ 1.5 + 0.5*size (player=2.0, zuk=5.0). */
        float px, pz, ground;
        render_get_visual_pos(rc, i, &px, &pz, &ground);
        int ent_size = (p->entity_type == ENTITY_NPC && p->npc_size > 1) ? p->npc_size : 1;
        float head_y = rc->entity_visual_top_y[i];
        float abdomen_y = rc->entity_visual_mid_y[i];
        if (head_y <= abdomen_y) {
            head_y = ground + 1.5f + 0.5f * (float)ent_size;
            abdomen_y = ground + 0.75f + 0.25f * (float)ent_size;
        }
        Vector2 screen_head = GetWorldToScreen((Vector3){ px, head_y, pz }, cam);
        Vector2 screen_overhead =
            GetWorldToScreen((Vector3){ px, render_overhead_anchor_y(head_y), pz }, cam);
        Vector2 screen_abdomen = GetWorldToScreen((Vector3){ px, abdomen_y, pz }, cam);

        /* skip if off screen */
        if (screen_overhead.x < -50 || screen_overhead.x > RENDER_WINDOW_W + 50 ||
            screen_overhead.y < -50 || screen_overhead.y > RENDER_WINDOW_H + 50) continue;

        /* hitsplats: drawn at entity.height/2 (abdomen) with slot-based layout. */
        for (int si = 0; si < RENDER_SPLATS_PER_PLAYER; si++) {
            HitSplat* s = &rc->splats[i][si];
            if (!s->active) continue;
            int sx, sy;
            render_splat_screen_pos(
                (int)screen_abdomen.x, (int)screen_abdomen.y, si, s, &sx, &sy);
            render_draw_hitmark(rc, sx, sy, s->damage, s->hitmark_trans, s->type);
        }

        /* track vertical offset for stacking elements above the player.
           screen Y increases downward, so we go negative to go up. */
        float cursor_y = screen_overhead.y;

        /* HP bar: width scales with NPC size, matching OSRS HealthBarDefinition
           widths (30 for size 1, up to ~160 for size 7). plain colored rectangle
           matches the no-sprite fallback path in the engine. */
        if (env->tick < rc->hp_bar_visible_until[i]) {
            /* OSRS bar widths by common NPC sizes (from cache HealthBarDefinitions):
               size 1→30, 2→40, 3→50, 4→60, 5→80, 7→120 */
            static const int BAR_WIDTH_BY_SIZE[] = {
                30, 30, 40, 50, 60, 80, 100, 120
            };
            int bw_idx = ent_size;
            if (bw_idx > 7) bw_idx = 7;
            int bar_w = BAR_WIDTH_BY_SIZE[bw_idx];
            int bar_h = 5;
            float hp_frac = (float)p->current_hitpoints / (float)p->base_hitpoints;
            if (hp_frac < 0.0f) hp_frac = 0.0f;
            if (hp_frac > 1.0f) hp_frac = 1.0f;
            /* a live entity always shows >=1px of green; only a dead one is fully red
               (ceil instead of truncating to 0 for a high-max-HP entity near death). */
            int green_w = p->current_hitpoints > 0 ? (int)ceilf(hp_frac * bar_w) : 0;
            if (green_w > bar_w) green_w = bar_w;

            int bar_x = (int)screen_overhead.x - bar_w / 2;
            int bar_y = (int)cursor_y - bar_h / 2;
            DrawRectangle(bar_x, bar_y, green_w, bar_h, COLOR_HP_GREEN);
            DrawRectangle(bar_x + green_w, bar_y, bar_w - green_w, bar_h, COLOR_HP_RED);
            cursor_y -= (float)(bar_h + 2);
        }

        /* prayer icon: drawn above the HP bar */
        if (rc->prayer_icons_loaded &&
            p->prayer > PRAYER_NONE && p->prayer <= PRAYER_REDEMPTION) {
            int icon_idx = prayer_to_headicon[p->prayer];
            if (icon_idx >= 0 && icon_idx < 6) {
                Texture2D tex = rc->prayer_icons[icon_idx];
                float scale = 1.0f;
                float draw_x = screen_overhead.x - (float)tex.width * scale / 2.0f;
                float draw_y = cursor_y - (float)tex.height * scale;
                DrawTextureEx(tex, (Vector2){ draw_x, draw_y }, 0.0f, scale, WHITE);
            }
        }

        /* debug: per-NPC combat state below the entity (only for NPCs) */
        if (rc->show_debug && p->entity_type == ENTITY_NPC &&
                p->debug_npc_type_name) {
            render_draw_entity_debug_metadata(p, screen_head);
        } else if (rc->show_debug && p->entity_type == ENTITY_NPC && debug_state) {
            InfernoState* is = debug_state;
            int slot = p->npc_slot;
            if (slot >= 0 && slot < INF_MAX_NPCS && is->npcs[slot].active) {
                InfNPC* npc = &is->npcs[slot];
                int dy = (int)screen_head.y + 10;
                int dx = (int)screen_head.x;
                int fs = 10;

                /* attack timer + style */
                const char* style_str = "???";
                Color style_col = WHITE;
                int style = (npc->type == INF_NPC_JAD) ? inf_npc_jad(npc)->attack_style : npc->attack_style;
                if (style == ATTACK_STYLE_MAGIC)  { style_str = "MAG"; style_col = BLUE; }
                if (style == ATTACK_STYLE_RANGED) { style_str = "RNG"; style_col = GREEN; }
                if (style == ATTACK_STYLE_MELEE)  { style_str = "MEL"; style_col = RED; }

                const char* atk_txt = TextFormat("ATK:%d %s", npc->attack_timer, style_str);
                int tw = MeasureText(atk_txt, fs);
                DrawText(atk_txt, dx - tw/2, dy, fs, style_col);
                dy += fs + 1;

                /* frozen ticks */
                if (npc->frozen_ticks > 0) {
                    const char* frz_txt = TextFormat("FRZ:%d", npc->frozen_ticks);
                    int fw = MeasureText(frz_txt, fs);
                    DrawText(frz_txt, dx - fw/2, dy, fs, (Color){100, 200, 255, 255});
                    dy += fs + 1;
                }

                /* NPC→player LOS (skip nibblers — they target pillars, not player) */
                if (npc->type != INF_NPC_NIBBLER) {
                    int npc_los = inf_npc_has_los(is, slot);
                    const char* los_txt = npc_los ? "NPC>P" : "NPC>P X";
                    Color los_col = npc_los ? GREEN : RED;
                    int lw = MeasureText(los_txt, fs);
                    DrawText(los_txt, dx - lw/2, dy, fs, los_col);
                    dy += fs + 1;
                }

                /* player→NPC LOS + range */
                {
                    const EncounterLoadoutStats* ls = &is->loadout_stats[is->weapon_set];
                    OsrsLosQuery los_query = osrs_los_blockers(
                        is->los_blockers,
                        is->los_blocker_count);
                    int can_atk = encounter_player_can_attack(
                        is->player.x, is->player.y,
                        npc->x, npc->y, npc->size,
                        ls->attack_range, &los_query);
                    const char* patk_txt = can_atk ? "P>NPC" : "P>NPC X";
                    Color patk_col = can_atk ? GREEN : RED;
                    int pw = MeasureText(patk_txt, fs);
                    DrawText(patk_txt, dx - pw/2, dy, fs, patk_col);
                    dy += fs + 1;
                }

                /* blob scan state */
                if (npc->type == INF_NPC_BLOB && npc->blob_scanned_prayer >= 0) {
                    const char* scan = "SCAN:???";
                    if (npc->blob_scanned_prayer == PRAYER_PROTECT_MAGIC) scan = "SCAN>RNG";
                    else if (npc->blob_scanned_prayer == PRAYER_PROTECT_RANGED) scan = "SCAN>MAG";
                    int sw = MeasureText(scan, fs);
                    DrawText(scan, dx - sw/2, dy, fs, YELLOW);
                }
            }
        }
    }
}


static void render_draw_minimap_orb(
    GuiState* gs,
    Rectangle rect,
    const char* filler_asset,
    const char* icon_asset,
    int value,
    int max_value,
    Color fallback_fill
) {
    gui_draw_named_asset(gs, "orb_frame_0", rect, WHITE);
    if (gui_asset(gs, "orb_frame_0").id == 0) {
        DrawRectangleRounded((Rectangle){rect.x, rect.y + 7, 34, 20}, 0.22f, 5,
            (Color){50, 46, 37, 235});
    }

    Rectangle fill = {rect.x + 27, rect.y + 4, 26, 26};
    gui_draw_named_asset(gs, "orb_filler_0", fill, WHITE);
    gui_draw_named_asset(gs, filler_asset, fill, WHITE);
    if (gui_asset(gs, filler_asset).id == 0) {
        DrawCircle((int)(fill.x + 13), (int)(fill.y + 13), 12, fallback_fill);
    }
    gui_draw_named_asset_centered(gs, icon_asset, fill, 22, 22, WHITE);

    char text[16];
    snprintf(text, sizeof(text), "%d", value);
    Color text_color = max_value > 0 && value < max_value / 3
        ? (Color){255, 80, 70, 255}
        : (Color){70, 255, 70, 255};
    Rectangle text_rect = {rect.x + 3, rect.y + 14, 24, 13};
    int tw = MeasureText(text, 12);
    int tx = (int)(text_rect.x + text_rect.width / 2 - tw / 2);
    int ty = (int)(text_rect.y + 1);
    DrawText(text, tx + 1, ty + 1, 12, BLACK);
    DrawText(text, tx, ty, 12, text_color);
}

static int render_minimap_inside(int x, int y) {
    float dx = (float)x - GUI_MINIMAP_MASK_CENTER;
    float dy = (float)y - GUI_MINIMAP_MASK_CENTER;
    return dx * dx + dy * dy <= GUI_MINIMAP_MASK_RADIUS * GUI_MINIMAP_MASK_RADIUS;
}

static void render_minimap_put_pixel(int x, int y, Color c) {
    if (x < 0 || y < 0 || x >= GUI_MINIMAP_W || y >= GUI_MINIMAP_H) return;
    if (!render_minimap_inside(x, y)) return;
    DrawPixel(x, y, c);
}

static void render_minimap_fill_rect(int x, int y, int w, int h, Color c) {
    for (int yy = 0; yy < h; yy++) {
        for (int xx = 0; xx < w; xx++) {
            render_minimap_put_pixel(x + xx, y + yy, c);
        }
    }
}

static int render_minimap_collision_flags(RenderClient* rc, int wx, int wy) {
    if (rc->collision_map == NULL) return COLLISION_NONE;
    return collision_get_flags(rc->collision_map, 0,
        wx + rc->collision_world_offset_x,
        wy + rc->collision_world_offset_y);
}

static Color render_minimap_tile_color(RenderClient* rc, int wx, int wy) {
    if (wx < rc->arena_base_x || wy < rc->arena_base_y ||
        wx >= rc->arena_base_x + rc->arena_width ||
        wy >= rc->arena_base_y + rc->arena_height) {
        return (Color){10, 8, 6, 255};
    }

    int flags = render_minimap_collision_flags(rc, wx, wy);
    if (flags & COLLISION_BLOCKED) return (Color){14, 11, 9, 255};
    if (flags & COLLISION_BRIDGE) return (Color){48, 42, 34, 255};
    return (Color){58, 35, 29, 255};
}

static void render_minimap_draw_tile_features(RenderClient* rc, int wx, int wy, int sx, int sy) {
    int flags = render_minimap_collision_flags(rc, wx, wy);
    if (flags == COLLISION_NONE) return;

    if (flags & COLLISION_BLOCKED) {
        render_minimap_fill_rect(sx - 1, sy - 1, 3, 3, (Color){36, 25, 20, 255});
    }

    Color wall = (Color){224, 221, 198, 255};
    if (flags & (COLLISION_WALL_NORTH | COLLISION_IMPENETRABLE_WALL_NORTH)) {
        render_minimap_fill_rect(sx - 2, sy - 2, 5, 1, wall);
    }
    if (flags & (COLLISION_WALL_SOUTH | COLLISION_IMPENETRABLE_WALL_SOUTH)) {
        render_minimap_fill_rect(sx - 2, sy + 2, 5, 1, wall);
    }
    if (flags & (COLLISION_WALL_WEST | COLLISION_IMPENETRABLE_WALL_WEST)) {
        render_minimap_fill_rect(sx - 2, sy - 2, 1, 5, wall);
    }
    if (flags & (COLLISION_WALL_EAST | COLLISION_IMPENETRABLE_WALL_EAST)) {
        render_minimap_fill_rect(sx + 2, sy - 2, 1, 5, wall);
    }
}

static float render_minimap_entity_center_x(RenderClient* rc, int entity_idx) {
    int sub = rc->sub_x[entity_idx];
    if (sub != 0) return (float)sub / 128.0f;
    RenderEntity* ent = &rc->entities[entity_idx];
    int size = ent->npc_size > 1 ? ent->npc_size : 1;
    return (float)ent->x + (float)size * 0.5f;
}

static float render_minimap_entity_center_y(RenderClient* rc, int entity_idx) {
    int sub = rc->sub_y[entity_idx];
    if (sub != 0) return (float)sub / 128.0f;
    RenderEntity* ent = &rc->entities[entity_idx];
    int size = ent->npc_size > 1 ? ent->npc_size : 1;
    return (float)ent->y + (float)size * 0.5f;
}

/* Draw a single entity dot on the minimap. Picks the right canonical sprite
   (player/NPC/friend) when sprites are loaded, otherwise falls back to a
   colored rectangle. */
static void render_draw_minimap_entity_dot(
    GuiState* gs, int sx, int sy, int sz_px,
    Texture2D sprite, Color fallback
) {
    if (sprite.id != 0) {
        Rectangle src = { 0, 0, (float)sprite.width, (float)sprite.height };
        Rectangle dst = { (float)(sx + sz_px / 2 - 2),
                          (float)(sy + sz_px / 2 - 2), 4.0f, 4.0f };
        DrawTexturePro(sprite, src, dst, (Vector2){0,0}, 0.0f, WHITE);
    } else {
        DrawRectangle(sx, sy, sz_px, sz_px, fallback);
    }
    (void)gs;
}

static void render_ensure_minimap_surface(RenderClient* rc, int w, int h) {
    if (rc->minimap_surface.id != 0 &&
        rc->minimap_surface_w == w &&
        rc->minimap_surface_h == h) {
        return;
    }

    if (rc->minimap_surface.id != 0) {
        UnloadRenderTexture(rc->minimap_surface);
    }

    rc->minimap_surface = LoadRenderTexture(w, h);
    if (rc->minimap_surface.id == 0 || rc->minimap_surface.texture.id == 0) {
        fprintf(stderr, "minimap render texture allocation failed\n");
        abort();
    }
    SetTextureFilter(rc->minimap_surface.texture, TEXTURE_FILTER_POINT);
    rc->minimap_surface_w = w;
    rc->minimap_surface_h = h;
}

static void render_draw_minimap_compass(RenderClient* rc, GuiState* gs, Rectangle compass) {
    int masked = gs->minimap_compass_masked.id != 0;
    Texture2D comp = masked ? gs->minimap_compass_masked : gui_asset(gs, "compass");
    if (comp.id == 0) comp = gs->minimap_compass;
    if (comp.id != 0) {
        Rectangle src = {0, 0, (float)comp.width, (float)comp.height};
        float draw_w = masked ? (float)comp.width : compass.width;
        float draw_h = masked ? (float)comp.height : compass.height;
        Rectangle dst = {
            compass.x + compass.width * 0.5f,
            compass.y + compass.height * 0.5f,
            draw_w,
            draw_h,
        };
        Vector2 origin = {draw_w * 0.5f, draw_h * 0.5f};
        float angle_deg = rc->cam_yaw * (180.0f / 3.14159265f);
        DrawTexturePro(comp, src, dst, origin, angle_deg, WHITE);
    } else {
        gui_draw_named_asset(gs, "resize_compass_mask", compass, WHITE);
        gui_text_shadow(gs, "N", (int)compass.x + 16, (int)compass.y + 12, 14, GUI_TEXT_ORANGE);
    }
}

/* Draw the minimap area at the top of the right-hand panel: dark backdrop, the
   circular minimap with arena tiles (terrain base color + walls + entity dots),
   the rotating compass at top-left, and four stat orbs (HP, prayer, run, spec).
   Mirrors the OSRS fixed-client layout. */
static void render_draw_minimap_area(RenderClient* rc, OsrsEnv* env, Player* p) {
    GuiState* gs = &rc->gui;
    int map_x = GetScreenWidth() - GUI_MAP_CONTAINER_W;
    int map_y = 0;
    int mask_w = GUI_MINIMAP_W;
    int mask_h = GUI_MINIMAP_H;
    int mask_x = map_x + GUI_MINIMAP_X;
    int mask_y = map_y + GUI_MINIMAP_Y;
    int compass_x = map_x + GUI_COMPASS_X;
    int compass_y = map_y + GUI_COMPASS_Y;

    render_ensure_minimap_surface(rc, mask_w, mask_h);
    BeginTextureMode(rc->minimap_surface);
    ClearBackground(BLANK);
    int map_cx = mask_w / 2;
    int map_cy = mask_h / 2;

    int player_idx = -1;
    for (int e = 0; e < rc->entity_count; e++) {
        if (rc->entities[e].entity_type == ENTITY_PLAYER) { player_idx = e; break; }
    }

    float player_x = player_idx >= 0 ? render_minimap_entity_center_x(rc, player_idx) : 0.0f;
    float player_y = player_idx >= 0 ? render_minimap_entity_center_y(rc, player_idx) : 0.0f;
    const float scale = 4.0f;

    for (int y = 0; y < GUI_MINIMAP_H; y++) {
        for (int x = 0; x < GUI_MINIMAP_W; x++) {
            if (!render_minimap_inside(x, y)) continue;
            float sx = (float)x - 76.0f;
            float sy = (float)y - 76.0f;
            int wx = (int)roundf(player_x + sx / scale);
            int wy = (int)roundf(player_y - sy / scale);
            DrawPixel(x, y, render_minimap_tile_color(rc, wx, wy));
        }
    }

    for (int dy = -20; dy <= 20; dy++) {
        for (int dx = -20; dx <= 20; dx++) {
            int sx = (int)roundf(76.0f + (float)dx * scale);
            int sy = (int)roundf(76.0f - (float)dy * scale);
            render_minimap_draw_tile_features(rc,
                (int)roundf(player_x) + dx,
                (int)roundf(player_y) + dy,
                sx, sy);
        }
    }

    for (int e = 0; e < rc->entity_count; e++) {
        RenderEntity* ent = &rc->entities[e];
        if (ent->entity_type == ENTITY_NPC && !ent->npc_visible) continue;

        float dx = render_minimap_entity_center_x(rc, e) - player_x;
        float dy = render_minimap_entity_center_y(rc, e) - player_y;
        if (dx < -40.0f || dx > 40.0f || dy < -40.0f || dy > 40.0f) continue;

        int dot_cx = (int)roundf((float)map_cx + dx * scale);
        int dot_cy = (int)roundf((float)map_cy - dy * scale);
        int rel_x = dot_cx - map_cx;
        int rel_y = dot_cy - map_cy;
        if (rel_x * rel_x + rel_y * rel_y > 68 * 68) continue;

        Texture2D dot_sprite = ent->entity_type == ENTITY_PLAYER
            ? gs->minimap_dot_player
            : gs->minimap_dot_npc;
        Color fallback = ent->entity_type == ENTITY_PLAYER
            ? WHITE
            : (Color){220, 60, 60, 255};
        int dot_sz = ent->entity_type == ENTITY_PLAYER ? 5 : 4;
        render_draw_minimap_entity_dot(gs,
            dot_cx - dot_sz / 2,
            dot_cy - dot_sz / 2,
            dot_sz,
            dot_sprite,
            fallback);

        if (player_idx >= 0 && e != player_idx &&
            rc->entities[player_idx].attack_target_entity_idx == e) {
            DrawRectangleLines(dot_cx - 3, dot_cy - 3, 7, 7, YELLOW);
        }
    }
    EndTextureMode();
    Rectangle surface_src = { 0, 0, (float)mask_w, -(float)mask_h };
    Rectangle surface_dst = { (float)mask_x, (float)mask_y,
                              (float)mask_w, (float)mask_h };
    DrawTexturePro(rc->minimap_surface.texture, surface_src, surface_dst,
                   (Vector2){0, 0}, 0.0f, WHITE);

    Rectangle compass = {
        (float)compass_x,
        (float)compass_y,
        (float)GUI_COMPASS_W,
        (float)GUI_COMPASS_H,
    };
    render_draw_minimap_compass(rc, gs, compass);

    Rectangle cover = {
        (float)(map_x + GUI_MAP_SURROUND_X),
        (float)(map_y + GUI_MAP_SURROUND_Y),
        (float)GUI_MAP_SURROUND_W,
        (float)GUI_MAP_SURROUND_H,
    };
    if (gui_asset(gs, "osrs_stretch_mapsurround").id != 0) {
        gui_draw_named_asset(gs, "osrs_stretch_mapsurround", cover, WHITE);
    } else {
        Texture2D chrome_sprite = rc->layout_mode == 1 ? gs->rm_minimap_frame : gs->minimap_frame;
        if (chrome_sprite.id != 0) {
            Rectangle src = {0, 0, (float)chrome_sprite.width, (float)chrome_sprite.height};
            DrawTexturePro(chrome_sprite, src, cover, (Vector2){0, 0}, 0.0f, WHITE);
        }
    }

    int orbs_x = map_x + GUI_ORBS_X;
    int orbs_y = map_y + GUI_ORBS_Y;
    Rectangle xp_orb = {(float)(orbs_x + GUI_XP_X), (float)(orbs_y + GUI_XP_Y), 27, 27};
    Rectangle hp_orb = {(float)(orbs_x + GUI_HP_X), (float)(orbs_y + GUI_HP_Y), 57, 34};
    Rectangle prayer_orb = {(float)(orbs_x + GUI_PRAYER_X), (float)(orbs_y + GUI_PRAYER_Y), 57, 34};
    Rectangle run_orb = {(float)(orbs_x + GUI_RUN_X), (float)(orbs_y + GUI_RUN_Y), 57, 34};
    Rectangle spec_orb = {(float)(orbs_x + GUI_SPEC_X), (float)(orbs_y + GUI_SPEC_Y), 57, 34};
    Rectangle worldmap_button = {(float)(orbs_x + GUI_WORLDMAP_X), (float)(orbs_y + GUI_WORLDMAP_Y), 30, 30};
    Rectangle wiki_button = {(float)(map_x + 166), (float)(map_y + 173), 40, 34};

    gui_draw_named_asset(gs, "tli_button01_orb01_34x34_0", xp_orb, WHITE);
    if (gui_asset(gs, "tli_button01_orb01_34x34_0").id == 0) {
        gui_draw_named_asset(gs, "ring_34_0", xp_orb, WHITE);
    }
    gui_draw_named_asset_centered(gs, "orb_xp_0", xp_orb, 24, 24, WHITE);

    if (p) {
        render_draw_minimap_orb(gs, hp_orb, "orb_filler_1", "orb_icon_0",
            p->current_hitpoints, p->base_hitpoints, (Color){210, 50, 45, 255});
        render_draw_minimap_orb(gs, prayer_orb, "orb_filler_4", "orb_icon_1",
            p->current_prayer, p->base_prayer, (Color){70, 120, 255, 255});
        render_draw_minimap_orb(gs, run_orb, "orb_filler_5", "orb_icon_2",
            osrs_run_energy_percent(p->run_energy), 100, (Color){60, 210, 80, 255});
        render_draw_minimap_orb(gs, spec_orb, "orb_filler_9", "orb_icon_6",
            p->special_energy, 100, (Color){225, 205, 55, 255});
    }

    gui_draw_named_asset(gs, "ring_30", worldmap_button, WHITE);
    gui_draw_named_asset_centered(gs, "worldmap_icon_0", worldmap_button, 22, 22, WHITE);
    gui_draw_named_asset(gs, "wiki_icon_0", wiki_button, WHITE);

    (void)env;
}

static int render_target_label_entity_idx(RenderClient* rc) {
    if (rc->entity_count <= 0) return -1;
    int target = rc->entities[0].attack_target_entity_idx;
    if (target >= 0 && target < rc->entity_count) return target;
    int gui_idx = rc->gui.gui_entity_idx;
    if (gui_idx >= 0 && gui_idx < rc->entity_count) {
        target = rc->entities[gui_idx].attack_target_entity_idx;
        if (target >= 0 && target < rc->entity_count) return target;
    }
    for (int ei = 0; ei < rc->entity_count; ei++) {
        if (rc->entities[ei].entity_type == ENTITY_NPC) return ei;
    }
    return rc->entity_count > 1 ? 1 : -1;
}

static void render_draw_target_label(RenderClient* rc) {
    int target = render_target_label_entity_idx(rc);
    if (target < 0) return;
    const char* label = TextFormat("Target: %s", render_entity_display_name(&rc->entities[target]));
    int w = MeasureText(label, 16);
    int x = (RENDER_GRID_W - w) / 2;
    if (x < 120) x = 120;
    DrawText(label, x + 1, 13, 16, BLACK);
    DrawText(label, x, 12, 16, COLOR_TEXT);
}

static int render_display_tick(OsrsEnv* env) {
    if (env->encounter_def && env->encounter_state) {
        return ((const EncounterDef*)env->encounter_def)->get_tick(
            (EncounterState*)env->encounter_state,
            (EncounterContext*)env->encounter_context);
    }
    return env->tick;
}

static int render_scene_is_pvp(OsrsEnv* env) {
    if (!env->encounter_def) return 1;
    const EncounterDef* def = (const EncounterDef*)env->encounter_def;
    return strcmp(def->name, "nh_pvp") == 0 || strcmp(def->name, "pvp") == 0;
}

static int render_scene_is_inferno(OsrsEnv* env) {
    if (!env->encounter_def) return 0;
    const EncounterDef* def = (const EncounterDef*)env->encounter_def;
    return strcmp(def->name, "inferno") == 0;
}

static const char* render_control_hint_text(OsrsEnv* env) {
    if (render_scene_is_inferno(env)) {
        return "Right-drag: orbit  Mid-drag: pan  Scroll: zoom  D: debug  H: human  F8: lab";
    }
    return "Right-drag: orbit  Mid-drag: pan  Scroll: zoom  SPACE: pause  S: safe spots  D: debug  G: cycle entity  H: human";
}

static void render_draw_default_top_hud(RenderClient* rc, int display_tick) {
    DrawText(TextFormat("Tick: %d", display_tick), 10, 12, 16, COLOR_TEXT);
    render_draw_target_label(rc);

    if (rc->entity_count >= 2) {
        RenderEntity* p0 = &rc->entities[0];
        RenderEntity* p1 = &rc->entities[1];
        const char* hp_txt = TextFormat("P0: %d/%d   P1: %d/%d",
            p0->current_hitpoints, p0->base_hitpoints,
            p1->current_hitpoints, p1->base_hitpoints);
        int hp_w = MeasureText(hp_txt, 16);
        DrawText(hp_txt, RENDER_GRID_W - hp_w - 12, 12, 16, COLOR_TEXT);
    }
}

static void render_draw_inferno_top_hud(OsrsEnv* env, int display_tick) {
    InfernoState* s = render_inferno_state_from_env(env);
    if (!s) {
        DrawText(TextFormat("Tick: %d", display_tick), 10, 12, 16, COLOR_TEXT);
        return;
    }
    DrawText(TextFormat("Tick: %d   Wave: %d / %d",
        display_tick, s->wave + 1, INF_NUM_WAVES), 10, 12, 16, COLOR_TEXT);
}

static void render_draw_colosseum_top_hud(RenderClient* rc, OsrsEnv* env) {
    ColosseumState* s = render_colosseum_state_from_env(env);
    if (!s) return;
    const char* wave_txt = s->wave == COLO_WAVE_BOSS
        ? TextFormat("Wave: %d / %d  (Sol Heredit)", s->wave + 1, COLO_NUM_WAVES)
        : TextFormat("Wave: %d / %d", s->wave + 1, COLO_NUM_WAVES);
    int wave_w = MeasureText(wave_txt, 16);
    /* second line (y=32): the default HUD already centers the "Target:" label at
       y=12, so the wave readout sits just below it to avoid overlapping. */
    DrawText(wave_txt, (RENDER_GRID_W - wave_w) / 2, 32, 16, COLOR_TEXT);
}

static const int COLOSSEUM_MODIFIER_ICON_SPRITE_IDS[COLO_NUM_REAL_MODIFIERS][3] = {
    [COLO_MOD_BEES] = {5544, 5559, 5574},
    [COLO_MOD_BLASPHEMY] = {5538, 5553, 5568},
    [COLO_MOD_DOOM] = {5543, 5558, 5573},
    [COLO_MOD_DYNAMIC_DUO] = {5545, 0, 0},
    [COLO_MOD_FRAILTY] = {5541, 5556, 5571},
    [COLO_MOD_MANTIMAYHEM] = {5539, 5554, 5569},
    [COLO_MOD_MYOPIA] = {5547, 5562, 5577},
    [COLO_MOD_REENTRY] = {5536, 5551, 5566},
    [COLO_MOD_RED_FLAG] = {5540, 0, 0},
    [COLO_MOD_RELENTLESS] = {5535, 5550, 5565},
    [COLO_MOD_SOLARFLARE] = {5537, 5552, 5567},
    [COLO_MOD_QUARTET] = {5546, 0, 0},
    [COLO_MOD_TOTEMIC] = {5542, 0, 0},
    [COLO_MOD_VOLATILITY] = {5534, 5549, 5564},
};

static Texture2D* render_require_colosseum_modifier_icon(
    RenderClient* rc,
    int modifier,
    int tier
) {
    if (modifier < 0 || modifier >= COLO_NUM_REAL_MODIFIERS || tier < 1 || tier > 3) {
        fprintf(stderr, "render: invalid colosseum modifier icon modifier=%d tier=%d\n",
            modifier, tier);
        abort();
    }
    int sprite_id = COLOSSEUM_MODIFIER_ICON_SPRITE_IDS[modifier][tier - 1];
    if (sprite_id <= 0) {
        fprintf(stderr, "render: missing colosseum modifier icon mapping modifier=%d tier=%d\n",
            modifier, tier);
        abort();
    }
    Texture2D* texture = &rc->colosseum_modifier_icons[modifier][tier - 1];
    if (texture->id == 0) {
        char path[96];
        snprintf(path, sizeof(path),
            "sprites/colosseum/modifiers/%d.png", sprite_id);
        *texture = osrs_asset_load_texture(OSRS_ASSET(path));
        if (texture->id == 0) {
            fprintf(stderr, "render: missing colosseum modifier icon asset %s\n", path);
            abort();
        }
        SetTextureFilter(*texture, TEXTURE_FILTER_POINT);
    }
    return texture;
}

/* Colosseum-only: display name + one-line effect for the modifier HUD hover
   tooltip. Text mirrors the ColoModifier enum comments in
   encounters/encounter_colosseum.h. Names/descs are file-local to the render TU
   and used only by render_draw_colosseum_modifier_hud, so nothing here is
   reachable from inferno/zulrah/pvp draw paths. */
static void render_colosseum_modifier_tooltip_text(
    int modifier, const char** out_name, const char** out_desc
) {
    switch (modifier) {
        case COLO_MOD_BEES:        *out_name = "Bees!";        *out_desc = "Roaming poison swarms (1/2/3 per tier)."; break;
        case COLO_MOD_BLASPHEMY:   *out_name = "Blasphemy";    *out_desc = "Drains prayer for 20/40/60% of damage taken."; break;
        case COLO_MOD_DOOM:        *out_name = "Doom";         *out_desc = "Stacks on damage; die at 15/10/5 stacks."; break;
        case COLO_MOD_DYNAMIC_DUO: *out_name = "Dynamic Duo";  *out_desc = "Shockwave Colossi spawn in pairs."; break;
        case COLO_MOD_FRAILTY:     *out_name = "Frailty";      *out_desc = "-10/-20/-40% max HP (T1+ disables overheal)."; break;
        case COLO_MOD_MANTIMAYHEM: *out_name = "Mantimayhem";  *out_desc = "Manticore: extra orb / venom / unpredictable."; break;
        case COLO_MOD_MYOPIA:      *out_name = "Myopia";       *out_desc = "Player attack range -2/-4/-6."; break;
        case COLO_MOD_REENTRY:     *out_name = "Reentry";      *out_desc = "Javelin skyfall leaves molten sand."; break;
        case COLO_MOD_RED_FLAG:    *out_name = "Red Flag";     *out_desc = "Minotaurs route around obstacles."; break;
        case COLO_MOD_RELENTLESS:  *out_name = "Relentless";   *out_desc = "Bypass 33/66/100% def, +1/+3/+6 max hit."; break;
        case COLO_MOD_SOLARFLARE:  *out_name = "Solarflare";   *out_desc = "Orb circling the boss pillars."; break;
        case COLO_MOD_QUARTET:     *out_name = "Quartet";      *out_desc = "+1 random warbander each wave (incl. W12)."; break;
        case COLO_MOD_TOTEMIC:     *out_name = "Totemic";      *out_desc = "NPCs at 50% HP spawn a healing totem."; break;
        case COLO_MOD_VOLATILITY:  *out_name = "Volatility";   *out_desc = "Death explosion / molten pool."; break;
        default:                   *out_name = "Unknown";      *out_desc = ""; break;
    }
}

static void render_draw_colosseum_modifier_hud(RenderClient* rc) {
    EncounterOverlay* ov = &rc->encounter_overlay;
    if (ov->active_modifier_count <= 0) return;
    const int icon_size = 24;
    const int gap = 4;
    const int x0 = 10;
    const int y0 = 36;
    Vector2 mouse = GetMousePosition();
    int hover_modifier = -1;
    int hover_tier = 0;
    int hover_x = 0;
    for (int i = 0; i < ov->active_modifier_count; i++) {
        EncounterActiveModifier* active = &ov->active_modifiers[i];
        if (!active->active) continue;
        Texture2D* texture = render_require_colosseum_modifier_icon(
            rc, active->modifier, active->tier);
        int x = x0 + i * (icon_size + gap);
        DrawRectangle(x - 2, y0 - 2, icon_size + 4, icon_size + 4,
            CLITERAL(Color){22, 19, 17, 210});
        DrawRectangleLines(x - 2, y0 - 2, icon_size + 4, icon_size + 4,
            CLITERAL(Color){184, 146, 72, 230});
        Rectangle src = {0, 0, (float)texture->width, (float)texture->height};
        Rectangle dst = {(float)x, (float)y0, (float)icon_size, (float)icon_size};
        DrawTexturePro(*texture, src, dst, (Vector2){0, 0}, 0.0f, WHITE);
        Rectangle hit = {(float)(x - 2), (float)(y0 - 2),
            (float)(icon_size + 4), (float)(icon_size + 4)};
        if (CheckCollisionPointRec(mouse, hit)) {
            hover_modifier = active->modifier;
            hover_tier = active->tier;
            hover_x = x;
        }
    }
    if (hover_modifier >= 0) {
        const char* name = NULL;
        const char* desc = NULL;
        render_colosseum_modifier_tooltip_text(hover_modifier, &name, &desc);
        char title[64];
        if (hover_tier >= 1 && hover_tier <= 3) {
            static const char* roman[4] = {"", "I", "II", "III"};
            snprintf(title, sizeof(title), "%s %s", name, roman[hover_tier]);
        } else {
            snprintf(title, sizeof(title), "%s", name);
        }
        const int title_fs = 12;
        const int desc_fs = 10;
        const int pad = 6;
        int title_w = MeasureText(title, title_fs);
        int desc_w = MeasureText(desc, desc_fs);
        int box_w = (title_w > desc_w ? title_w : desc_w) + pad * 2;
        int box_h = title_fs + desc_fs + pad * 2 + 2;
        int box_x = hover_x;
        int box_y = y0 + icon_size + 6;
        if (box_x + box_w > RENDER_WINDOW_W - 4) box_x = RENDER_WINDOW_W - 4 - box_w;
        if (box_x < 4) box_x = 4;
        DrawRectangle(box_x, box_y, box_w, box_h, CLITERAL(Color){18, 16, 14, 235});
        DrawRectangleLines(box_x, box_y, box_w, box_h, CLITERAL(Color){184, 146, 72, 230});
        DrawText(title, box_x + pad, box_y + pad, title_fs, CLITERAL(Color){238, 222, 180, 255});
        DrawText(desc, box_x + pad, box_y + pad + title_fs + 2, desc_fs, CLITERAL(Color){200, 200, 200, 255});
    }
}

/* greedy word-wrap a short fixed string to `max_w`, one shadowed line per run. */
static void render_colosseum_draft_wrap_text(
    const GuiState* gs, const char* text, int x, int y, int max_w, int fs, int line_h, Color color
) {
    char line[160];
    int line_len = 0;
    int cur_y = y;
    int i = 0;
    while (text[i] != '\0') {
        int ws = i;
        while (text[i] != '\0' && text[i] != ' ') i++;
        int word_len = i - ws;
        if (word_len > (int)sizeof(line) - 1) word_len = (int)sizeof(line) - 1;
        char candidate[160];
        int cand_len = line_len;
        memcpy(candidate, line, (size_t)line_len);
        if (line_len > 0) candidate[cand_len++] = ' ';
        memcpy(candidate + cand_len, text + ws, (size_t)word_len);
        cand_len += word_len;
        candidate[cand_len] = '\0';
        if (line_len > 0 && MeasureText(candidate, fs) > max_w) {
            line[line_len] = '\0';
            context_menu_draw_text_shadow(gs, line, x, cur_y, fs, color);
            cur_y += line_h;
            memcpy(line, text + ws, (size_t)word_len);
            line_len = word_len;
        } else {
            memcpy(line, candidate, (size_t)cand_len);
            line_len = cand_len;
        }
        while (text[i] == ' ') i++;
    }
    if (line_len > 0) {
        line[line_len] = '\0';
        context_menu_draw_text_shadow(gs, line, x, cur_y, fs, color);
    }
}

/* the three draft cards are laid out in a centered row across the game-grid
   area (left of the side panel). Shared by the draw pass and the click hit-test
   so the geometry lives in exactly one place. */
static Rectangle render_colosseum_draft_card_rect(int option) {
    const int card_w = 176;
    const int card_h = 170;
    const int gap = 14;
    int total_w = COLO_MODIFIER_DRAFT_OPTIONS * card_w + (COLO_MODIFIER_DRAFT_OPTIONS - 1) * gap;
    int x0 = (RENDER_GRID_W - total_w) / 2;
    int y0 = RENDER_WINDOW_H / 2 - card_h / 2;
    return CLITERAL(Rectangle){
        (float)(x0 + option * (card_w + gap)), (float)y0, (float)card_w, (float)card_h };
}

/* Colosseum-only: the mandatory between-wave modifier-draft modal. While the env
   holds a pending draft (modifiers.draft_pending) the world is frozen and the
   only valid action is the pick; this renders the offered modifiers as clickable
   cards over the dimmed arena. Viewer-only: it reads draft state and the click
   handler stages pending_modifier_select, which the existing human-command path
   maps to COLO_HEAD_MODIFIER_SELECT (no training/obs surface touched). */
static void render_draw_colosseum_modifier_draft(RenderClient* rc, OsrsEnv* env) {
    ColosseumState* cs = render_colosseum_state_from_env(env);
    if (!cs || !cs->modifiers.draft_pending) return;

    DrawRectangle(0, 0, RENDER_GRID_W, RENDER_WINDOW_H, CLITERAL(Color){0, 0, 0, 150});

    Rectangle first = render_colosseum_draft_card_rect(0);
    const char* banner = "Choose a modifier to start the next wave";
    int banner_fs = 16;
    int banner_w = MeasureText(banner, banner_fs);
    context_menu_draw_text_shadow(&rc->gui, banner,
        (RENDER_GRID_W - banner_w) / 2, (int)first.y - 34, banner_fs,
        CLITERAL(Color){255, 255, 0, 255});

    static const char* roman[4] = {"", "I", "II", "III"};
    Vector2 mouse = GetMousePosition();
    for (int o = 0; o < COLO_MODIFIER_DRAFT_OPTIONS; o++) {
        int mod = cs->modifiers.draft_options[o];
        if (mod < 0) continue;   /* fewer than 3 eligible: empty slots are mask-invalid */
        Rectangle card = render_colosseum_draft_card_rect(o);
        int hover = CheckCollisionPointRec(mouse, card);
        DrawRectangleRec(card, hover ? CLITERAL(Color){74, 60, 38, 245}
                                     : CLITERAL(Color){53, 44, 31, 244});
        DrawRectangleLinesEx(card, hover ? 2.0f : 1.0f, CLITERAL(Color){170, 137, 72, 255});

        const char* name = NULL;
        const char* desc = NULL;
        render_colosseum_modifier_tooltip_text(mod, &name, &desc);

        int next_tier = cs->modifiers.tier[mod] + 1;
        if (next_tier > COLO_MODIFIER_MAX_TIER[mod]) next_tier = COLO_MODIFIER_MAX_TIER[mod];
        char title[64];
        if (COLO_MODIFIER_MAX_TIER[mod] > 1 && next_tier >= 1 && next_tier <= 3)
            snprintf(title, sizeof(title), "%s %s", name, roman[next_tier]);
        else
            snprintf(title, sizeof(title), "%s", name);

        const int pad = 9;
        context_menu_draw_text_shadow(&rc->gui, title,
            (int)card.x + pad, (int)card.y + pad, 15, CLITERAL(Color){255, 255, 0, 255});
        render_colosseum_draft_wrap_text(&rc->gui, desc,
            (int)card.x + pad, (int)card.y + pad + 28, (int)card.width - pad * 2, 11, 14,
            CLITERAL(Color){255, 152, 31, 255});
        context_menu_draw_text_shadow(&rc->gui, hover ? "> click to pick <" : "click to pick",
            (int)card.x + pad, (int)card.y + (int)card.height - 22, 11,
            hover ? CLITERAL(Color){255, 255, 255, 255} : CLITERAL(Color){170, 170, 170, 255});
    }
}

static void render_draw_top_hud(RenderClient* rc, OsrsEnv* env) {
    int display_tick = render_display_tick(env);
    if (render_scene_is_inferno(env)) {
        render_draw_inferno_top_hud(env, display_tick);
        return;
    }
    render_draw_default_top_hud(rc, display_tick);
    if (env->encounter_def &&
            strcmp(((const EncounterDef*)env->encounter_def)->name, "colosseum") == 0) {
        render_draw_colosseum_top_hud(rc, env);
        render_draw_colosseum_modifier_hud(rc);
    }
}

static void render_follow_pvp_fighter_midpoint(RenderClient* rc, OsrsEnv* env, double frame_dt) {
    if (!render_scene_is_pvp(env) || rc->human_input.enabled || rc->entity_count < 2)
        return;

    float x0 = (float)rc->sub_x[0] / 128.0f;
    float y0 = (float)rc->sub_y[0] / 128.0f;
    float x1 = (float)rc->sub_x[1] / 128.0f;
    float y1 = (float)rc->sub_y[1] / 128.0f;
    float target_x = (x0 + x1) * 0.5f;
    float target_z = -((y0 + y1) * 0.5f);
    float lerp = 1.0f - powf(0.85f, (float)frame_dt * 60.0f);

    rc->cam_target_x += (target_x - rc->cam_target_x) * lerp;
    rc->cam_target_z += (target_z - rc->cam_target_z) * lerp;
}


void pvp_render(OsrsEnv* env) {
    RenderClient* rc = (RenderClient*)env->client;
    if (rc == NULL) {
        rc = render_make_client();
        env->client = rc;
    }

    /* ensure entity pointers are current (may be called without render_post_tick
       during pause, rewind, or initial frame) */
    render_populate_entities(rc, env);
    render_ensure_entity_visual_slots(rc);

    render_handle_input(rc, env);
    double frame_dt = GetFrameTime();
    double visual_dt = render_scaled_frame_dt(rc, frame_dt);
    render_follow_pvp_fighter_midpoint(rc, env, frame_dt);
    model_cache_update_texture_anims(rc->model_cache, (float)visual_dt);
    model_cache_update_texture_anims(rc->npc_model_cache, (float)visual_dt);
    model_cache_update_texture_anims(rc->projectile_model_cache, (float)visual_dt);

    /* inventory mouse interaction (clicks, drags) — runs every frame.
       gui functions need the full Player* (inventory, stats, etc.) */
    if (rc->entity_count > 0 && rc->gui.gui_entity_idx < rc->entity_count) {
        Player* gui_p = render_get_player_ptr(env, rc->gui.gui_entity_idx);
        if (gui_p) gui_inv_handle_mouse(&rc->gui, gui_p, &rc->human_input);
    }

    /* run client ticks at 50 Hz scaled by replay speed. this keeps movement,
       animation, splats, effects, and projectile flights on one visual clock. */
    {
        rc->client_tick_accumulator += visual_dt;
        double client_tick = RENDER_CLIENT_TICK_SECONDS;
        int steps = (int)(rc->client_tick_accumulator / client_tick);
        if (steps > 0) {
            rc->client_tick_accumulator -= steps * client_tick;
            for (int s = 0; s < steps; s++) {
                for (int i = 0; i < rc->entity_count; i++) {
                    render_client_tick(rc, i);
                }
                /* advance visual effects, hitsplats, and projectile flights at 50 Hz */
                render_update_splats_client_tick(rc);
                flight_client_tick(rc);
                rc->effect_client_tick_counter++;
                effect_client_tick(rc->effects, rc->effect_client_tick_counter,
                    rc->anim_cache);
	                gui_tick(&rc->gui);
                human_tick_visuals(&rc->human_input);
            }
        }
    }

    BeginDrawing();
    ClearBackground(COLOR_BG);

    render_draw_3d_world(rc);

    render_draw_overhead_status(rc, env);

    if (rc->show_debug && env->encounter_state && env->encounter_def &&
        strcmp(((const EncounterDef*)env->encounter_def)->name, "inferno") == 0) {
            InfernoState* is = (InfernoState*)env->encounter_state;
            int dy = RENDER_WINDOW_H - 160;
            int dx = 10;
            int fs = 12;
            Color dc = (Color){220, 220, 220, 255};

            /* target */
            int tgt = is->interaction.target_slot;
            if (tgt >= 0 && tgt < INF_MAX_NPCS) {
                InfNPC* tn = &is->npcs[tgt];
                const char* tname = inferno_npc_name(INF_NPC_DEF_IDS[tn->type]);
                DrawText(TextFormat("TARGET: %s [#%d]", tname, tgt), dx, dy, fs, dc);
            } else {
                DrawText("TARGET: none", dx, dy, fs, (Color){180, 80, 80, 255});
            }
            dy += fs + 2;

            /* weapon + attack timer */
            const char* gear = is->weapon_set == INF_GEAR_MAGE ? "mage" :
                               is->weapon_set == INF_GEAR_LONG_RANGE ? "long" : "bp";
            const char* spell = "none";
            if (inf_autocast_is_active(is))
                spell = inf_player_autocast_spell(&is->player) == ENCOUNTER_SPELL_ICE
                    ? "ice"
                    : "blood";
            DrawText(TextFormat("GEAR: %s  ATK: %d/%d  SPELL: %s", gear,
                is->player.attack_timer, is->loadout_stats[is->weapon_set].attack_speed, spell),
                dx, dy, fs, dc);
            dy += fs + 2;

            /* stats */
            DrawText(TextFormat("RNG:%d MAG:%d DEF:%d",
                is->player.current_ranged, is->player.current_magic, is->player.current_defence),
                dx, dy, fs, dc);
            dy += fs + 2;

            /* consumables */
            DrawText(TextFormat("BREW:%d REST:%d BAST:%d STAM:%d",
                is->player.brew_doses, is->player.restore_doses,
                is->player.bastion_doses, is->player.stamina_doses),
                dx, dy, fs, dc);
            dy += fs + 2;

            /* pending hits */
            int mag_hits = 0, rng_hits = 0;
            for (int h = 0; h < is->player_pending_hits.count; h++) {
                if (is->player_pending_hits.hits[h].attack_style == ATTACK_STYLE_MAGIC) mag_hits++;
                else rng_hits++;
            }
            if (is->player_pending_hits.count > 0) {
                DrawText(TextFormat("INCOMING: %d (%dM %dR)",
                    is->player_pending_hits.count, mag_hits, rng_hits),
                    dx, dy, fs, (Color){255, 150, 150, 255});
            }
        }

    if (rc->show_debug) {
            for (int ei = 0; ei < rc->entity_count; ei++) {
                ConvexHull2D* h = &rc->entity_hulls[ei];
                if (h->count < 3) continue;
                Color col = (ei == rc->gui.gui_entity_idx)
                    ? (Color){ 0, 255, 0, 180 } : (Color){ 255, 0, 0, 180 };
                for (int hi = 0; hi < h->count; hi++) {
                    int ni = (hi + 1) % h->count;
                    DrawLine(h->xs[hi], h->ys[hi], h->xs[ni], h->ys[ni], col);
                }
            }
        }

    render_draw_top_hud(rc, env);
    DrawText(render_control_hint_text(env), 10, RENDER_WINDOW_H - 20, 10, COLOR_TEXT_DIM);

    /* OSRS GUI panel system: shows selected entity's state.
       Renders in both 2D and 3D mode as a side panel overlay.
       G key cycles through entities (player 0, player 1, NPCs, etc). */
    rc->gui.gui_entity_count = rc->entity_count;
    rc->gui.encounter_state = env->encounter_state;
    rc->gui.encounter_def = env->encounter_def;
    if (rc->gui.gui_entity_idx >= rc->entity_count)
        rc->gui.gui_entity_idx = 0;
    /* draw click cross at screen-space position (2D overlay, like real OSRS) */
    human_draw_click_cross(&rc->human_input,
                            rc->click_cross_sprites,
                            rc->click_cross_loaded);

    /* debug: show raycast tile selection info */
    if (rc->show_debug) {
        char dbg[256];
        snprintf(dbg, sizeof(dbg), "box: (%d,%d) plane: (%d,%d) hit3d: (%.1f,%.1f,%.1f)",
                 rc->debug_hit_wx, rc->debug_hit_wy,
                 rc->debug_plane_wx, rc->debug_plane_wy,
                 rc->debug_ray_hit_x, rc->debug_ray_hit_y, rc->debug_ray_hit_z);
        DrawText(dbg, 10, 30, 16, MAGENTA);
        snprintf(dbg, sizeof(dbg), "ray org: (%.1f,%.1f,%.1f) dir: (%.3f,%.3f,%.3f)",
                 rc->debug_ray_origin.x, rc->debug_ray_origin.y, rc->debug_ray_origin.z,
                 rc->debug_ray_dir.x, rc->debug_ray_dir.y, rc->debug_ray_dir.z);
        DrawText(dbg, 10, 48, 16, MAGENTA);
    }

    if (rc->entity_count > 0) {
        /* gui_draw needs full Player* for inventory/stats/prayers.
           render_get_player_ptr fetches from encounter vtable. */
        Player* gui_player = render_get_player_ptr(env, rc->gui.gui_entity_idx);
        /* plumb spell-targeting state into GUI so the selected spell shows a
           highlight border while awaiting enemy click */
        rc->gui.pending_spell_highlight = -1;
        if (rc->human_input.cursor_mode == CURSOR_SPELL_TARGET) {
            rc->gui.pending_spell_highlight = rc->human_input.selected_spell_gui_idx;
        }
        /* render-only: feed the colosseum kit's 28-slot wiki inventory so the panel
           reads 1:1 at the start, recomputed live each frame so vials deplete and
           the worn weapon leaves the grid; other encounters leave the derived
           grid (count 0). */
        rc->gui.display_inventory_count = 0;
        {
            ColosseumState* colo_inv = render_colosseum_state_from_env(env);
            if (colo_inv && colo_inv->active_loadout_profile >= 0 &&
                    colo_inv->active_loadout_profile < COLO_NUM_LOADOUT_PROFILES) {
                int live_kit[COLO_INVENTORY_DISPLAY_SLOTS];
                col_build_live_inventory_display(colo_inv, live_kit);
                for (int i = 0; i < COLO_INVENTORY_DISPLAY_SLOTS &&
                        i < INV_GRID_SLOTS; i++)
                    rc->gui.display_inventory_osrs_ids[i] = live_kit[i];
                rc->gui.display_inventory_count = COLO_INVENTORY_DISPLAY_SLOTS;
            }
        }
        if (gui_player) gui_draw(&rc->gui, gui_player);

        /* draw minimap + orbs on top of the panel area (post gui_draw so the
           side-panel background stops at the minimap area's bottom edge) */
        render_draw_minimap_area(rc, env, gui_player);

        if (rc->show_debug) {
            int target_idx = render_target_label_entity_idx(rc);
            if (target_idx >= 0 && rc->entities[target_idx].entity_type == ENTITY_NPC) {
                render_draw_panel_npc(10, 70, &rc->entities[target_idx], env);
            }
        }
    }

    render_draw_encounter_status_text(rc);
    render_lab_draw_hud(rc);

    /* colosseum mandatory modifier draft: modal over the frozen arena, under the
       right-click menu. No-op for other encounters / when no draft is pending. */
    render_draw_colosseum_modifier_draft(rc, env);

    /* right-click context menu: drawn last so it renders on top of everything */
    context_menu_draw(rc);

    EndDrawing();
}

#endif /* OSRS_RENDER_H */
