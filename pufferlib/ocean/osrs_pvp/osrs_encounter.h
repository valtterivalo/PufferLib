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
