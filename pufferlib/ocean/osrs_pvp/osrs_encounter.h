/**
 * @file osrs_encounter.h
 * @brief Encounter interface for the OSRS simulation engine.
 *
 * An encounter is a specific training scenario (NH PvP, Jad, Cerberus, etc.)
 * that plugs into the shared OSRS engine (combat, movement, collision, rendering).
 *
 * Each encounter defines its own:
 *   - Entity configuration (agents, NPCs, adds)
 *   - Observation and action spaces
 *   - Reset/step logic and NPC AI
 *   - Reward function and termination conditions
 *   - Arena bounds and spawn positions
 *
 * The ocean binding and renderer dispatch through EncounterDef function pointers,
 * so adding a new encounter is just writing one header file + registering it.
 */

#ifndef OSRS_ENCOUNTER_H
#define OSRS_ENCOUNTER_H

#include <stdint.h>
#include <string.h>
#include "osrs_pvp_types.h"

/* opaque encounter state — each encounter defines its own struct */
typedef struct EncounterState EncounterState;

/* ======================================================================== */
/* shared pending hit system for delayed projectile damage                   */
/* ======================================================================== */

#define ENCOUNTER_MAX_PENDING_HITS 8

typedef struct {
    int active;
    int damage;
    int ticks_remaining;   /* countdown to landing */
    int attack_style;      /* ATTACK_STYLE_* for prayer check at land time */
    int check_prayer;      /* 1 = re-check prayer when hit lands (jad) */
} EncounterPendingHit;

/* visual overlay data: shared between encounter and renderer.
   encounter's render_post_tick populates this, renderer reads it. */
#define ENCOUNTER_MAX_OVERLAY_TILES 16
#define ENCOUNTER_MAX_OVERLAY_SNAKES 4
#define ENCOUNTER_MAX_OVERLAY_PROJECTILES 8

typedef struct {
    /* venom clouds */
    struct { int x, y, active; } clouds[ENCOUNTER_MAX_OVERLAY_TILES];
    int cloud_count;

    /* boss state */
    int boss_x, boss_y, boss_visible;
    int boss_form;  /* encounter-specific form/phase index */
    int boss_size;  /* NPC size in tiles (e.g. 5 for Zulrah) */

    /* snakelings / adds */
    struct { int x, y, active, is_magic; } snakelings[ENCOUNTER_MAX_OVERLAY_SNAKES];
    int snakeling_count;

    /* visual projectiles: brief flash from source to target.
       encounters fire attacks instantly, but we show a 1-tick projectile
       for visual clarity. the renderer draws these and auto-expires them. */
    struct {
        int active;
        int src_x, src_y;   /* source tile (e.g. Zulrah position) */
        int dst_x, dst_y;   /* target tile (e.g. player position) */
        int style;           /* 0=ranged, 1=magic, 2=melee */
        int damage;          /* for hit splat at destination */
    } projectiles[ENCOUNTER_MAX_OVERLAY_PROJECTILES];
    int projectile_count;

    /* melee targeting: shows which tile Zulrah is staring at */
    int melee_target_active;
    int melee_target_x, melee_target_y;
} EncounterOverlay;

/* ======================================================================== */
/* render entity: shared abstraction for renderer (value type, not pointer)  */
/* ======================================================================== */

typedef struct {
    EntityType entity_type;
    int npc_def_id;
    int npc_visible;
    int npc_size;
    int npc_anim_id;
    int x, y;
    int dest_x, dest_y;
    int current_hitpoints, base_hitpoints;
    int special_energy;
    OverheadPrayer prayer;
    GearSet visible_gear;
    int frozen_ticks;
    int veng_active;
    int is_running;
    AttackStyle attack_style_this_tick;
    int magic_type_this_tick;
    int hit_landed_this_tick;
    int hit_damage;
    int hit_was_successful;
    int cast_veng_this_tick;
    int ate_food_this_tick;
    int ate_karambwan_this_tick;
    int used_special_this_tick;
    uint8_t equipped[NUM_GEAR_SLOTS];
} RenderEntity;

/** Fill a RenderEntity from a Player struct (PvP, Zulrah, snakelings). */
static inline void render_entity_from_player(const Player* p, RenderEntity* out) {
    out->entity_type = p->entity_type;
    out->npc_def_id = p->npc_def_id;
    out->npc_visible = p->npc_visible;
    out->npc_size = p->npc_size;
    out->npc_anim_id = p->npc_anim_id;
    out->x = p->x;
    out->y = p->y;
    out->dest_x = p->dest_x;
    out->dest_y = p->dest_y;
    out->current_hitpoints = p->current_hitpoints;
    out->base_hitpoints = p->base_hitpoints;
    out->special_energy = p->special_energy;
    out->prayer = p->prayer;
    out->visible_gear = p->visible_gear;
    out->frozen_ticks = p->frozen_ticks;
    out->veng_active = p->veng_active;
    out->is_running = p->is_running;
    out->attack_style_this_tick = p->attack_style_this_tick;
    out->magic_type_this_tick = p->magic_type_this_tick;
    out->hit_landed_this_tick = p->hit_landed_this_tick;
    out->hit_damage = p->hit_damage;
    out->hit_was_successful = p->hit_was_successful;
    out->cast_veng_this_tick = p->cast_veng_this_tick;
    out->ate_food_this_tick = p->ate_food_this_tick;
    out->ate_karambwan_this_tick = p->ate_karambwan_this_tick;
    out->used_special_this_tick = p->used_special_this_tick;
    memcpy(out->equipped, p->equipped, NUM_GEAR_SLOTS);
}

/* ======================================================================== */
/* shared movement: 25-action system (idle + 8 walk + 16 run)                */
/* ======================================================================== */

/* 25 movement actions: idle(0), walk(1-8), run(9-24) */
#define ENCOUNTER_MOVE_ACTIONS 25

/* target offsets: (dx, dy) relative to player position */
static const int ENCOUNTER_MOVE_TARGET_DX[25] = {
    0,                          /* 0: idle */
    -1, -1, -1, 0, 0, 1, 1, 1, /* 1-8: walk (dist 1) */
    -2, -2, -2, -2, -2,        /* 9-13: run west edge */
    -1, -1,                     /* 14-15: run inner */
    0, 0,                       /* 16-17: run N/S 2 tiles */
    1, 1,                       /* 18-19: run inner */
    2, 2, 2, 2, 2              /* 20-24: run east edge */
};
static const int ENCOUNTER_MOVE_TARGET_DY[25] = {
    0,
    -1, 0, 1, -1, 1, -1, 0, 1,
    -2, -1, 0, 1, 2,
    -2, 2,
    -2, 2,
    -2, 2,
    -2, -1, 0, 1, 2
};

/* callback: returns 1 if tile (x, y) is walkable for the encounter.
   ctx is encounter-specific state (InfernoState*, ZulrahState*, etc.) */
typedef int (*encounter_walkable_fn)(void* ctx, int x, int y);

/** move player toward target offset via up to 2 greedy steps.
    walk actions (dist 1) take 1 step, run actions (dist 2) take up to 2.
    sets is_running = 1 if 2 steps were taken.
    returns number of tiles moved (0, 1, or 2). */
static inline int encounter_move_to_target(
    Player* p, int target_dx, int target_dy,
    encounter_walkable_fn is_walkable, void* ctx
) {
    int tx = p->x + target_dx;
    int ty = p->y + target_dy;
    int dist = abs(target_dx) > abs(target_dy) ? abs(target_dx) : abs(target_dy);
    int max_steps = dist;  /* 1 for walk, 2 for run */
    int steps = 0;

    for (int step = 0; step < max_steps; step++) {
        if (p->x == tx && p->y == ty) break;
        /* greedy step toward target */
        int dx = 0, dy = 0;
        if (tx > p->x) dx = 1; else if (tx < p->x) dx = -1;
        if (ty > p->y) dy = 1; else if (ty < p->y) dy = -1;

        /* try diagonal, x-only, y-only */
        int moved = 0;
        if (dx != 0 && dy != 0 && is_walkable(ctx, p->x + dx, p->y + dy)) {
            p->x += dx; p->y += dy; moved = 1;
        } else if (dx != 0 && is_walkable(ctx, p->x + dx, p->y)) {
            p->x += dx; moved = 1;
        } else if (dy != 0 && is_walkable(ctx, p->x, p->y + dy)) {
            p->y += dy; moved = 1;
        }
        if (!moved) break;
        steps++;
    }

    p->is_running = (steps == 2);
    p->dest_x = p->x;
    p->dest_y = p->y;
    return steps;
}

/* ======================================================================== */
/* shared NPC greedy pathfinding                                             */
/* ======================================================================== */

/* callback: returns 1 if tile (x, y) is blocked for an NPC of given size */
typedef int (*encounter_npc_blocked_fn)(void* ctx, int x, int y, int size);

/** greedy NPC step toward target. tries diagonal first, then x-only, then y-only.
    this is the standard OSRS NPC movement algorithm — 99.9% of NPCs use this.
    returns 1 if moved, 0 if blocked or already at target. */
static inline int encounter_npc_step_toward(
    int* x, int* y, int tx, int ty, int size,
    encounter_npc_blocked_fn is_blocked, void* ctx
) {
    int dx = 0, dy = 0;
    if (tx > *x) dx = 1;
    else if (tx < *x) dx = -1;
    if (ty > *y) dy = 1;
    else if (ty < *y) dy = -1;
    if (dx == 0 && dy == 0) return 0;

    /* try diagonal */
    if (dx != 0 && dy != 0) {
        if (!is_blocked(ctx, *x + dx, *y + dy, size)) {
            *x += dx; *y += dy; return 1;
        }
    }
    /* try x-only */
    if (dx != 0 && !is_blocked(ctx, *x + dx, *y, size)) {
        *x += dx; return 1;
    }
    /* try y-only */
    if (dy != 0 && !is_blocked(ctx, *x, *y + dy, size)) {
        *y += dy; return 1;
    }
    return 0;
}

/* ======================================================================== */
/* shared per-tick flag clearing for encounters                              */
/* ======================================================================== */

/** clear all per-tick animation/event flags on a player.
    call at the start of each encounter tick, then set flags as events happen.
    the renderer reads these once per frame via RenderEntity. */
static inline void encounter_clear_tick_flags(Player* p) {
    p->attack_style_this_tick = ATTACK_STYLE_NONE;
    p->magic_type_this_tick = 0;
    p->hit_landed_this_tick = 0;
    p->hit_damage = 0;
    p->hit_was_successful = 0;
    p->cast_veng_this_tick = 0;
    p->ate_food_this_tick = 0;
    p->ate_karambwan_this_tick = 0;
    p->used_special_this_tick = 0;
}

/* ======================================================================== */
/* shared gear switching helpers for encounters                              */
/* ======================================================================== */

/** apply a full static loadout to player equipment and set gear state.
    used by Zulrah, Inferno, and future boss encounters with fixed loadouts. */
static inline void encounter_apply_loadout(
    Player* p, const uint8_t loadout[NUM_GEAR_SLOTS], GearSet gear_set
) {
    memcpy(p->equipped, loadout, NUM_GEAR_SLOTS);
    p->current_gear = gear_set;
    p->visible_gear = gear_set;
}

/** populate player inventory from multiple loadouts (deduped per slot).
    extra_items is an optional overlay array (e.g. justiciar for tank), NULL to skip.
    the GUI reads p->inventory[][] to display available gear switches. */
static void encounter_populate_inventory(
    Player* p,
    const uint8_t* const* loadouts, int num_loadouts,
    const uint8_t extra_items[NUM_GEAR_SLOTS]
) {
    memset(p->inventory, 255 /* ITEM_NONE */, sizeof(p->inventory));
    memset(p->num_items_in_slot, 0, sizeof(p->num_items_in_slot));

    for (int s = 0; s < NUM_GEAR_SLOTS; s++) {
        int n = 0;
        for (int l = 0; l < num_loadouts && n < MAX_ITEMS_PER_SLOT; l++) {
            uint8_t item = loadouts[l][s];
            if (item == 255 /* ITEM_NONE */) continue;
            int dup = 0;
            for (int j = 0; j < n; j++) { if (p->inventory[s][j] == item) { dup = 1; break; } }
            if (dup) continue;
            p->inventory[s][n++] = item;
        }
        if (extra_items && extra_items[s] != 255 /* ITEM_NONE */ && n < MAX_ITEMS_PER_SLOT) {
            int dup = 0;
            for (int j = 0; j < n; j++) { if (p->inventory[s][j] == extra_items[s]) { dup = 1; break; } }
            if (!dup) p->inventory[s][n++] = extra_items[s];
        }
        p->num_items_in_slot[s] = n;
    }
}

/* ======================================================================== */
/* encounter definition (vtable)                                             */
/* ======================================================================== */

typedef struct {
    const char* name;           /* "nh_pvp", "cerberus", "jad", etc. */

    /* observation/action space dimensions */
    int obs_size;               /* raw observation features (before mask) */
    int num_action_heads;
    const int* action_head_dims; /* array of per-head dimensions */
    int mask_size;              /* sum of action_head_dims */

    /* lifecycle: create/destroy encounter state */
    EncounterState* (*create)(void);
    void (*destroy)(EncounterState* state);

    /* episode lifecycle */
    void (*reset)(EncounterState* state, uint32_t seed);
    void (*step)(EncounterState* state, const int* actions);

    /* RL interface */
    void (*write_obs)(EncounterState* state, float* obs_out);
    void (*write_mask)(EncounterState* state, float* mask_out);
    float (*get_reward)(EncounterState* state);
    int (*is_terminal)(EncounterState* state);

    /* entity access for renderer (returns entity count, writes entity pointers).
       renderer uses this to draw all entities generically. */
    int (*get_entity_count)(EncounterState* state);
    void* (*get_entity)(EncounterState* state, int index);  /* returns Player* */

    /* render entity population: fills array of RenderEntity structs for the renderer.
       replaces get_entity casting for rendering. NULL = renderer falls back to get_entity. */
    void (*fill_render_entities)(EncounterState* state, RenderEntity* out, int max_entities, int* count);

    /* encounter-specific config (key-value put/get for binding kwargs) */
    void (*put_int)(EncounterState* state, const char* key, int value);
    void (*put_float)(EncounterState* state, const char* key, float value);
    void (*put_ptr)(EncounterState* state, const char* key, void* value);

    /* arena bounds for renderer (0 = use FIGHT_AREA_* defaults) */
    int arena_base_x, arena_base_y;
    int arena_width, arena_height;

    /* render hooks (optional — NULL if not implemented).
       populates visual overlay data for the renderer. */
    void (*render_post_tick)(EncounterState* state, EncounterOverlay* overlay);

    /* logging (returns pointer to encounter's Log struct, or NULL) */
    void* (*get_log)(EncounterState* state);

    /* tick access */
    int (*get_tick)(EncounterState* state);
    int (*get_winner)(EncounterState* state);
} EncounterDef;

/* ======================================================================== */
/* encounter registry                                                        */
/* ======================================================================== */

#define MAX_ENCOUNTERS 32

typedef struct {
    const EncounterDef* defs[MAX_ENCOUNTERS];
    int count;
} EncounterRegistry;

static EncounterRegistry g_encounter_registry = { .count = 0 };

static void encounter_register(const EncounterDef* def) {
    if (g_encounter_registry.count < MAX_ENCOUNTERS) {
        g_encounter_registry.defs[g_encounter_registry.count++] = def;
    }
}

static const EncounterDef* encounter_find(const char* name) {
    for (int i = 0; i < g_encounter_registry.count; i++) {
        if (strcmp(g_encounter_registry.defs[i]->name, name) == 0) {
            return g_encounter_registry.defs[i];
        }
    }
    return NULL;
}

#endif /* OSRS_ENCOUNTER_H */
