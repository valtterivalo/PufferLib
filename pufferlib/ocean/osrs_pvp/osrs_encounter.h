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
