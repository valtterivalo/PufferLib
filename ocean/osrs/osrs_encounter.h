/**
 * @fileoverview osrs_encounter.h — shared encounter mechanics for the current ocean OSRS envs.
 *
 * this header holds reusable mechanics that current encounters build on.
 * encounter-specific policy should stay in the encounter header; shared helpers
 * here should stay generic enough to be reused by future envs.
 *
 * SHARED SYSTEMS (in order of appearance in this file):
 *
 *   rendering:
 *     RenderEntity                     value struct for renderer (not Player*)
 *     render_entity_from_player()      copy Player fields to RenderEntity
 *     encounter_resolve_attack_target() match npc_slot to render entity index
 *     EncounterOverlay                 visual overlay (hazards, projectiles, boss)
 *
 *   prayer (set/refresh semantic for RL tick commands):
 *     ENCOUNTER_OVERHEAD_*                canonical overhead action encoding (5/6/7 dim)
 *     ENCOUNTER_OFFENSIVE_*               canonical offensive action encoding (5 dim)
 *     encounter_apply_overhead_action()   apply overhead action, returns 1 on activation
 *     encounter_apply_offensive_action()  apply offensive action, returns 1 on activation
 *     encounter_drain_all_prayers()       drain both slots per tick (activation-tick skip)
 *
 *   movement:
 *     ENCOUNTER_MOVE_TARGET_DX/DY[25]  direction tables (idle + 8 walk + 16 run)
 *     encounter_move_to_target()       player movement: walk 1 tile or run 2
 *     encounter_move_toward_dest()     BFS click-to-move toward destination
 *     encounter_pathfind()             shared BFS pathfind wrapper
 *
 *   NPC pathfinding:
 *     encounter_npc_step_out_from_under()  shuffle NPC off player tile (OSRS overlap rule)
 *     encounter_npc_step_toward()      OSRS size-aware chase step
 *
 *   damage:
 *     encounter_damage_player()        apply damage to player (HP, clamp, splat, tracker)
 *     encounter_damage_npc()           apply damage to NPC (HP, splat flags)
 *
 *   per-tick flags:
 *     encounter_clear_tick_flags()     reset animation/event flags each tick
 *
 *   gear switching:
 *     encounter_apply_loadout()        memcpy loadout + set gear state
 *     encounter_populate_inventory()   dedup items from multiple loadouts for GUI
 *
 *   combat stats:
 *     EncounterLoadoutStats            derived stats (att bonus, max hit, eff level...)
 *     Player.offensive_prayer          runtime state, source of truth for prayer multipliers
 *     encounter_compute_loadout_stats() derive all stats from ITEM_DATABASE + loadout
 *
 *   hit delays:
 *     EncounterPendingHit              queued damage with tick countdown
 *
 * ALSO SEE:
 *   osrs_combat.h                     hit chance, tbow formula, barrage AoE, delay formulas
 *   osrs_pvp_combat.h                 PvP-specific damage (prayer, veng, recoil, smite)
 */

#ifndef OSRS_ENCOUNTER_H
#define OSRS_ENCOUNTER_H

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "osrs_types.h"
#include "osrs_items.h"
#include "osrs_pathfinding.h"
#include "osrs_combat.h"
#include "osrs_consumables.h"
#include "osrs_item_effects.h"
#include "osrs_human_input_types.h"

/* opaque encounter runtime pieces — each encounter defines its own structs */
typedef struct EncounterState EncounterState;
typedef struct EncounterContext EncounterContext;

typedef struct {
    EncounterState* state;
    EncounterContext* context;
} EncounterRuntime;

static inline void encounter_abort_unknown_config(
    const char* encounter_name, const char* config_type, const char* key
) {
    fprintf(stderr, "%s unknown %s config key: %s\n",
        encounter_name, config_type, key);
    abort();
}

static inline int encounter_require_binary_config(
    const char* encounter_name, const char* key, int value
) {
    if (value != 0 && value != 1) {
        fprintf(stderr, "%s config %s must be 0 or 1, got %d\n",
            encounter_name, key, value);
        abort();
    }
    return value;
}

static inline int encounter_require_int_range_config(
    const char* encounter_name, const char* key, int value, int min_value, int max_value
) {
    if (value < min_value || value > max_value) {
        fprintf(stderr, "%s config %s must be in [%d, %d], got %d\n",
            encounter_name, key, min_value, max_value, value);
        abort();
    }
    return value;
}


#define ENCOUNTER_MAX_PENDING_HITS 32

/* spell types for barrage freeze/heal effects on pending hits */
#define ENCOUNTER_SPELL_NONE  0
#define ENCOUNTER_SPELL_ICE   1   /* ice barrage: freeze on hit */
#define ENCOUNTER_SPELL_BLOOD 2   /* blood barrage: heal 25% of AoE damage */

typedef struct {
    int active;
    int damage;
    int ticks_remaining;   /* countdown to landing */
    int attack_style;      /* ATTACK_STYLE_* for prayer check at land time */
    int check_prayer;      /* 1 = prayer has NOT been checked yet (deferred) */
    int prayer_check_delay;/* ticks until prayer is checked (0 = check immediately on next resolve).
                              jad uses 3 to model its T+3 DelayedAction — prayer at T+3 decides
                              whether the hit is blocked, independent of projectile flight time.
                              ref: InfernoTrainer JalTokJad.ts:49-57. */
    int spell_type;        /* ENCOUNTER_SPELL_* for freeze/heal effects */
    int source_npc_type;   /* encounter-local NPC type for custom delayed rolls */
    int source_npc_slot;   /* source NPC slot, for attacker-targeted effects (recoil); -1 if none */
    int hit_success;       /* accuracy result; 0 can still be a visible splash */
    int elysian_reduced;   /* shield proc already applied before queuing */
} EncounterPendingHit;

typedef struct {
    EncounterPendingHit hits[ENCOUNTER_MAX_PENDING_HITS];
    int count;
} EncounterPendingHitQueue;

static inline void encounter_pending_hit_queue_clear(EncounterPendingHitQueue* q) {
    memset(q, 0, sizeof(*q));
}

static inline EncounterPendingHit* encounter_pending_hit_queue_push(
    EncounterPendingHitQueue* q,
    EncounterPendingHit hit,
    const char* owner_label,
    int tick,
    int slot,
    int type
) {
    if (q->count < 0 || q->count > ENCOUNTER_MAX_PENDING_HITS) {
        fprintf(stderr,
            "%s pending-hit queue corrupt tick=%d slot=%d type=%d count=%d\n",
            owner_label, tick, slot, type, q->count);
        abort();
    }
    if (q->count >= ENCOUNTER_MAX_PENDING_HITS) {
        fprintf(stderr,
            "%s pending-hit queue overflow tick=%d slot=%d type=%d count=%d "
            "delay=%d style=%d spell=%d damage=%d\n",
            owner_label, tick, slot, type, q->count,
            hit.ticks_remaining, hit.attack_style, hit.spell_type, hit.damage);
        abort();
    }

    hit.active = 1;
    q->hits[q->count] = hit;
    return &q->hits[q->count++];
}

static inline void encounter_pending_hit_queue_remove(
    EncounterPendingHitQueue* q,
    int idx,
    const char* owner_label
) {
    if (q->count < 0 || q->count > ENCOUNTER_MAX_PENDING_HITS) {
        fprintf(stderr, "%s pending-hit queue corrupt before remove count=%d\n",
            owner_label, q->count);
        abort();
    }
    if (idx < 0 || idx >= q->count) {
        fprintf(stderr, "%s pending-hit queue invalid remove idx=%d count=%d\n",
            owner_label, idx, q->count);
        abort();
    }
    for (int i = idx + 1; i < q->count; i++)
        q->hits[i - 1] = q->hits[i];
    q->count--;
    memset(&q->hits[q->count], 0, sizeof(q->hits[q->count]));
}

static inline const EncounterPendingHit* encounter_pending_hit_queue_earliest(
    const EncounterPendingHitQueue* q
) {
    const EncounterPendingHit* best = NULL;
    for (int i = 0; i < q->count; i++) {
        const EncounterPendingHit* hit = &q->hits[i];
        if (!hit->active) continue;
        if (!best || hit->ticks_remaining < best->ticks_remaining)
            best = hit;
    }
    return best;
}

static inline int encounter_pending_hit_queue_damage_sum(
    const EncounterPendingHitQueue* q
) {
    int total = 0;
    for (int i = 0; i < q->count; i++) {
        if (q->hits[i].active)
            total += q->hits[i].damage;
    }
    return total;
}

/* visual overlay data: shared between encounter and renderer.
   encounter's render_post_tick populates this, renderer reads it. */
#define ENCOUNTER_MAX_OVERLAY_TILES 16
#define ENCOUNTER_MAX_OVERLAY_ADDS 4
#define ENCOUNTER_OVERLAY_STATUS_TEXT_LEN 64
/* inferno can legitimately exceed single-digit projectile counts in one tick,
   especially during Zuk healer spark volleys. size this from real encounter
   volume so the renderer never silently drops visual events. */
#define ENCOUNTER_MAX_OVERLAY_PROJECTILES 48

typedef enum {
    ENCOUNTER_PROJECTILE_MOTION_OSRS_FLIGHT = 0,
    ENCOUNTER_PROJECTILE_MOTION_TARGET_ANCHORED = 1,
} EncounterProjectileMotionMode;

typedef enum {
    ENCOUNTER_PROJECTILE_TARGET_FIXED = 0,
    ENCOUNTER_PROJECTILE_TARGET_PLAYER = 1,
    ENCOUNTER_PROJECTILE_TARGET_NPC_SLOT = 2,
} EncounterProjectileTargetKind;

typedef struct {
    /* encounter-defined area hazards. current users write 3x3 poison clouds. */
    struct { int x, y, active; } hazards[ENCOUNTER_MAX_OVERLAY_TILES];
    int hazard_count;

    /* boss state */
    int boss_x, boss_y, boss_visible;
    int boss_form;  /* encounter-specific form/phase index */
    int boss_size;  /* NPC size in tiles (e.g. 5 for Zulrah) */

    /* encounter adds or secondary mobs. variant is encounter-defined. */
    struct { int x, y, active, variant; } adds[ENCOUNTER_MAX_OVERLAY_ADDS];
    int add_count;

    /* visual projectiles: brief flash from source to target.
       encounters fire attacks instantly, but we show a 1-tick projectile
       for visual clarity. the renderer draws these and auto-expires them. */
    struct {
        int active;
        int src_x, src_y;   /* source tile (e.g. Zulrah position) */
        int dst_x, dst_y;   /* target tile (e.g. player position) */
        int style;           /* 0=ranged, 1=magic, 2=melee, 3=cloud, 4=spawn_orb */
        int damage;          /* for hit splat at destination */
        /* flight parameters — encounters set these, renderer reads them */
        int duration_ticks;  /* flight duration in client ticks (0 = use default 35) */
        int start_h;         /* start height in OSRS units /128 (0 = use default) */
        int end_h;           /* end height in OSRS units /128 (0 = use default) */
        int curve;           /* OSRS slope param (0 = use default 16) */
        float arc_height;    /* sinusoidal arc peak in tiles (0 = quadratic/straight) */
        int tracks_target;   /* 1 = re-aim toward target each tick */
        int source_kind;
        int source_npc_slot;
        int target_kind;
        int target_npc_slot;
        int start_delay;     /* ticks before projectile becomes visible (0 = immediate) */
        int motion_mode;     /* EncounterProjectileMotionMode */
        float offset_x, offset_y, offset_z; /* local multi-model offset */
        int src_size;        /* source entity size for center offset (0 = use boss_size) */
        int dst_size;        /* target entity size for center offset (1 = player) */
        uint32_t model_id;   /* GFX model from cache (0 = style-based fallback) */
        int anim_id;         /* spotanim animation sequence (-1 = static model) */
        int launch_gfx_id;
        int impact_gfx_id;   /* optional landing spotanim to spawn on arrival */
    } projectiles[ENCOUNTER_MAX_OVERLAY_PROJECTILES];
    int projectile_count;

    /* melee targeting: shows which tile Zulrah is staring at */
    int melee_target_active;
    int melee_target_x, melee_target_y;

    int status_text_active;
    char status_text[ENCOUNTER_OVERLAY_STATUS_TEXT_LEN];
} EncounterOverlay;

/* map AttackStyle enum to overlay projectile style index.
   used by encounter_emit_projectile and render overlay systems. */
static inline int encounter_attack_style_to_proj_style(int attack_style) {
    switch (attack_style) {
        case ATTACK_STYLE_RANGED: return 0;
        case ATTACK_STYLE_MAGIC:  return 1;
        case ATTACK_STYLE_MELEE:  return 2;
        default: return 0;
    }
}

/* populate an overlay projectile slot with flight parameters.
   encounters should call this instead of filling fields manually. */
static inline int encounter_emit_projectile(
    EncounterOverlay* ov,
    int src_x, int src_y, int dst_x, int dst_y,
    int style, int damage,
    int duration_ticks, int start_h, int end_h, int curve,
    float arc_height, int tracks_target, int src_size, int dst_size,
    uint32_t model_id, int impact_gfx_id
) {
    if (ov->projectile_count >= ENCOUNTER_MAX_OVERLAY_PROJECTILES) {
        fprintf(stderr, "encounter overlay projectile capacity exceeded: %d\n",
            ENCOUNTER_MAX_OVERLAY_PROJECTILES);
        abort();
    }
    int i = ov->projectile_count++;
    ov->projectiles[i].active = 1;
    ov->projectiles[i].src_x = src_x;
    ov->projectiles[i].src_y = src_y;
    ov->projectiles[i].dst_x = dst_x;
    ov->projectiles[i].dst_y = dst_y;
    ov->projectiles[i].style = style;
    ov->projectiles[i].damage = damage;
    ov->projectiles[i].duration_ticks = duration_ticks;
    ov->projectiles[i].start_h = start_h;
    ov->projectiles[i].end_h = end_h;
    ov->projectiles[i].curve = curve;
    ov->projectiles[i].arc_height = arc_height;
    ov->projectiles[i].start_delay = 0;
    ov->projectiles[i].motion_mode = ENCOUNTER_PROJECTILE_MOTION_OSRS_FLIGHT;
    ov->projectiles[i].offset_x = 0.0f;
    ov->projectiles[i].offset_y = 0.0f;
    ov->projectiles[i].offset_z = 0.0f;
    ov->projectiles[i].tracks_target = tracks_target;
    ov->projectiles[i].source_kind = ENCOUNTER_PROJECTILE_TARGET_FIXED;
    ov->projectiles[i].source_npc_slot = -1;
    ov->projectiles[i].target_kind = tracks_target
        ? ENCOUNTER_PROJECTILE_TARGET_PLAYER
        : ENCOUNTER_PROJECTILE_TARGET_FIXED;
    ov->projectiles[i].target_npc_slot = -1;
    ov->projectiles[i].src_size = src_size;
    ov->projectiles[i].dst_size = dst_size;
    ov->projectiles[i].model_id = model_id;
    ov->projectiles[i].anim_id = -1;
    ov->projectiles[i].launch_gfx_id = 0;
    ov->projectiles[i].impact_gfx_id = impact_gfx_id;
    return i;
}

static inline void encounter_require_projectile_slots(const EncounterOverlay* ov, int slots) {
    if (slots < 0 || ov->projectile_count + slots > ENCOUNTER_MAX_OVERLAY_PROJECTILES) {
        fprintf(stderr, "encounter overlay projectile capacity exceeded: need %d free from %d/%d\n",
            slots, ov->projectile_count, ENCOUNTER_MAX_OVERLAY_PROJECTILES);
        abort();
    }
}

static inline void encounter_require_projectile_index(const EncounterOverlay* ov, int projectile_idx) {
    if (projectile_idx < 0 || projectile_idx >= ov->projectile_count) {
        fprintf(stderr, "encounter projectile index out of range: %d/%d\n",
            projectile_idx, ov->projectile_count);
        abort();
    }
}

static inline void encounter_set_projectile_source_player(
    EncounterOverlay* ov, int projectile_idx
) {
    encounter_require_projectile_index(ov, projectile_idx);
    ov->projectiles[projectile_idx].source_kind = ENCOUNTER_PROJECTILE_TARGET_PLAYER;
    ov->projectiles[projectile_idx].source_npc_slot = -1;
}

static inline void encounter_set_projectile_source_npc_slot(
    EncounterOverlay* ov, int projectile_idx, int npc_slot
) {
    encounter_require_projectile_index(ov, projectile_idx);
    if (npc_slot < 0) {
        fprintf(stderr, "encounter projectile source npc slot is invalid: %d\n", npc_slot);
        abort();
    }
    ov->projectiles[projectile_idx].source_kind = ENCOUNTER_PROJECTILE_TARGET_NPC_SLOT;
    ov->projectiles[projectile_idx].source_npc_slot = npc_slot;
}

static inline void encounter_set_projectile_target_npc_slot(
    EncounterOverlay* ov, int projectile_idx, int npc_slot
) {
    encounter_require_projectile_index(ov, projectile_idx);
    if (npc_slot < 0) {
        fprintf(stderr, "encounter projectile target npc slot is invalid: %d\n", npc_slot);
        abort();
    }
    ov->projectiles[projectile_idx].tracks_target = 1;
    ov->projectiles[projectile_idx].target_kind = ENCOUNTER_PROJECTILE_TARGET_NPC_SLOT;
    ov->projectiles[projectile_idx].target_npc_slot = npc_slot;
}

static inline void encounter_set_projectile_motion_mode(
    EncounterOverlay* ov, int projectile_idx, int motion_mode
) {
    encounter_require_projectile_index(ov, projectile_idx);
    ov->projectiles[projectile_idx].motion_mode = motion_mode;
}

static inline void encounter_set_projectile_animation(
    EncounterOverlay* ov, int projectile_idx, int anim_id
) {
    encounter_require_projectile_index(ov, projectile_idx);
    ov->projectiles[projectile_idx].anim_id = anim_id;
}

static inline void encounter_set_projectile_launch_gfx(
    EncounterOverlay* ov, int projectile_idx, int launch_gfx_id
) {
    encounter_require_projectile_index(ov, projectile_idx);
    ov->projectiles[projectile_idx].launch_gfx_id = launch_gfx_id;
}

static inline void encounter_set_projectile_offset(
    EncounterOverlay* ov, int projectile_idx,
    float offset_x, float offset_y, float offset_z
) {
    encounter_require_projectile_index(ov, projectile_idx);
    ov->projectiles[projectile_idx].offset_x = offset_x;
    ov->projectiles[projectile_idx].offset_y = offset_y;
    ov->projectiles[projectile_idx].offset_z = offset_z;
}


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
    FightStyle fight_style;
    AttackStyle attack_style_this_tick;
    int magic_type_this_tick;
    int hit_landed_this_tick;
    int hit_damage;
    int hit_was_successful;
    int hit_spell_type;  /* ENCOUNTER_SPELL_* for barrage impact effects on NPCs */
    int elysian_proc_this_tick;
    int cast_veng_this_tick;
    int ate_food_this_tick;
    int ate_karambwan_this_tick;
    int used_special_this_tick;
    uint8_t equipped[NUM_GEAR_SLOTS];
    int npc_slot;  /* source slot index in encounter's NPC array; -1 for player */
    uint32_t npc_instance_id;  /* stable for one NPC lifetime; 0 means slot+def only */
    int attack_target_entity_idx;  /* render entity index of attack target, -1 = none */
} RenderEntity;

typedef enum {
    RENDER_ENTITY_FACE_MOVEMENT = 0,
    RENDER_ENTITY_FACE_ATTACK_TARGET = 1,
    RENDER_ENTITY_FACE_DEST_TILE = 2,
} RenderEntityFacingMode;

static inline int render_entity_find_previous_identity_index(
    const RenderEntity* previous,
    int previous_count,
    const int* previous_used,
    const RenderEntity* entity
) {
    if (entity->entity_type == ENTITY_PLAYER) {
        for (int j = 0; j < previous_count; j++) {
            if (!previous_used[j] && previous[j].entity_type == ENTITY_PLAYER) {
                return j;
            }
        }
        return -1;
    }

    if (entity->entity_type != ENTITY_NPC || entity->npc_slot < 0)
        return -1;

    for (int i = 0; i < previous_count; i++) {
        if (previous_used[i]) continue;
        if (previous[i].entity_type == ENTITY_NPC &&
                previous[i].npc_slot == entity->npc_slot &&
                previous[i].npc_def_id == entity->npc_def_id) {
            if ((previous[i].npc_instance_id || entity->npc_instance_id) &&
                    previous[i].npc_instance_id != entity->npc_instance_id) {
                continue;
            }
            return i;
        }
    }
    return -1;
}

static inline RenderEntityFacingMode render_entity_select_facing_mode(
    const RenderEntity* entity, int moved
) {
    if (entity->attack_target_entity_idx >= 0 || entity->current_hitpoints <= 0)
        return RENDER_ENTITY_FACE_ATTACK_TARGET;
    if (entity->attack_style_this_tick != ATTACK_STYLE_NONE)
        return RENDER_ENTITY_FACE_DEST_TILE;
    if (moved)
        return RENDER_ENTITY_FACE_MOVEMENT;
    return RENDER_ENTITY_FACE_DEST_TILE;
}

/** Fill a RenderEntity from a Player struct. */
static inline void render_entity_from_player(const Player* p, RenderEntity* out) {
    memset(out, 0, sizeof(RenderEntity));
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
    out->fight_style = p->fight_style;
    out->attack_style_this_tick = p->attack_style_this_tick;
    out->magic_type_this_tick = p->magic_type_this_tick;
    out->hit_landed_this_tick = p->hit_landed_this_tick;
    out->hit_damage = p->hit_damage;
    out->hit_was_successful = p->hit_was_successful;
    out->hit_spell_type = 0;
    out->elysian_proc_this_tick = p->elysian_proc_this_tick;
    out->cast_veng_this_tick = p->cast_veng_this_tick;
    out->ate_food_this_tick = p->ate_food_this_tick;
    out->ate_karambwan_this_tick = p->ate_karambwan_this_tick;
    out->used_special_this_tick = p->used_special_this_tick;
    memcpy(out->equipped, p->equipped, NUM_GEAR_SLOTS);
    out->npc_slot = -1;  /* player, not an NPC */
    out->npc_instance_id = 0;
    out->attack_target_entity_idx = -1;
}

/** Resolve attack_target_entity_idx for entity 0 (player) by matching npc_slot.
    call after fill_render_entities populates the entity array. any encounter with
    NPC targeting should call this so the renderer faces the correct target. */
static inline void encounter_resolve_attack_target(
    RenderEntity* entities, int count, int target_npc_slot
) {
    entities[0].attack_target_entity_idx = -1;
    if (target_npc_slot < 0) return;
    for (int i = 1; i < count; i++) {
        if (entities[i].npc_slot == target_npc_slot) {
            entities[0].attack_target_entity_idx = i;
            return;
        }
    }
}
/* canonical prayer action encoding for simulator tick commands              */
/*                                                                           */
/* the RL action space uses explicit no_change, off, and set_refresh actions.*/
/* human UI clicks translate OSRS click-toggle behavior into those commands. */
/*                                                                           */
/* each encounter chooses its action-head dim based on which prayers it      */
/* exposes. PvE uses 5, PvE with Redemption uses 6, PvP uses 7. new          */
/* encounters                                                               */
/* wire up by:                                                               */
/*   1. declaring two action heads with encounter_overhead_dim /             */
/*      ENCOUNTER_OFFENSIVE_DIM                                              */
/*   2. calling encounter_apply_overhead_action()  on pretick                */
/*   3. calling encounter_apply_offensive_action() on pretick                */
/*   4. calling encounter_drain_all_prayers() on pretick (handles both slots */
/*      + activation-tick skip + pp=0 auto-clear)                            */
/* overhead action encoding. dim depends on encounter:
   - PvE: 5 dim, actions 0-4 only
   - PvE with Redemption: 6 dim, action 5 maps to Redemption locally
   - PvP: 7 dim, full range */
#define ENCOUNTER_OVERHEAD_NO_CHANGE                    0
#define ENCOUNTER_OVERHEAD_OFF                          1
#define ENCOUNTER_OVERHEAD_SET_REFRESH_MELEE            2
#define ENCOUNTER_OVERHEAD_SET_REFRESH_RANGED           3
#define ENCOUNTER_OVERHEAD_SET_REFRESH_MAGIC            4
#define ENCOUNTER_OVERHEAD_SET_REFRESH_SMITE            5
#define ENCOUNTER_OVERHEAD_SET_REFRESH_REDEMPTION       6
#define ENCOUNTER_OVERHEAD_DIM_PVE                      5
#define ENCOUNTER_OVERHEAD_DIM_PVE_REDEMPTION           6
#define ENCOUNTER_OVERHEAD_DIM_PVP                      7

/* offensive action encoding — 5 dim, shared by all encounters. */
#define ENCOUNTER_OFFENSIVE_NO_CHANGE                   0
#define ENCOUNTER_OFFENSIVE_OFF                         1
#define ENCOUNTER_OFFENSIVE_SET_REFRESH_PIETY           2
#define ENCOUNTER_OFFENSIVE_SET_REFRESH_RIGOUR          3
#define ENCOUNTER_OFFENSIVE_SET_REFRESH_AUGURY          4
#define ENCOUNTER_OFFENSIVE_DIM                         5

/** apply an overhead prayer action with set/refresh semantics.
    set/refresh leaves target active and marks the slot newly activated.
    off clears the slot. no_change preserves state and drains normally. */
static inline int encounter_apply_overhead_action(OverheadPrayer* overhead, int action) {
    OverheadPrayer target;
    switch (action) {
        case ENCOUNTER_OVERHEAD_NO_CHANGE:
            return 0;
        case ENCOUNTER_OVERHEAD_OFF:
            *overhead = PRAYER_NONE;
            return 0;
        case ENCOUNTER_OVERHEAD_SET_REFRESH_MELEE:       target = PRAYER_PROTECT_MELEE;  break;
        case ENCOUNTER_OVERHEAD_SET_REFRESH_RANGED:      target = PRAYER_PROTECT_RANGED; break;
        case ENCOUNTER_OVERHEAD_SET_REFRESH_MAGIC:       target = PRAYER_PROTECT_MAGIC;  break;
        case ENCOUNTER_OVERHEAD_SET_REFRESH_SMITE:       target = PRAYER_SMITE;          break;
        case ENCOUNTER_OVERHEAD_SET_REFRESH_REDEMPTION:  target = PRAYER_REDEMPTION;     break;
        default: return 0;
    }
    *overhead = target;
    return 1;
}

/** apply an offensive prayer action with set/refresh semantics. */
static inline int encounter_apply_offensive_action(OffensivePrayer* offensive, int action) {
    OffensivePrayer target;
    switch (action) {
        case ENCOUNTER_OFFENSIVE_NO_CHANGE:
            return 0;
        case ENCOUNTER_OFFENSIVE_OFF:
            *offensive = OFFENSIVE_PRAYER_NONE;
            return 0;
        case ENCOUNTER_OFFENSIVE_SET_REFRESH_PIETY:    target = OFFENSIVE_PRAYER_PIETY;  break;
        case ENCOUNTER_OFFENSIVE_SET_REFRESH_RIGOUR:   target = OFFENSIVE_PRAYER_RIGOUR; break;
        case ENCOUNTER_OFFENSIVE_SET_REFRESH_AUGURY:   target = OFFENSIVE_PRAYER_AUGURY; break;
        default: return 0;
    }
    *offensive = target;
    return 1;
}


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


/* shared BFS pathfind wrapper — translates local coords to world coords for pathfind_step.
   extra_blocked/blocked_ctx: optional callback for dynamic obstacles (pillars etc.).
   pass NULL/NULL for encounters with no dynamic obstacles. */
static inline PathResult encounter_pathfind(
    const CollisionMap* cmap, int world_offset_x, int world_offset_y,
    int src_x, int src_y, int dst_x, int dst_y,
    pathfind_blocked_fn extra_blocked, void* blocked_ctx
) {
    return pathfind_step(cmap, 0,
        src_x + world_offset_x, src_y + world_offset_y,
        dst_x + world_offset_x, dst_y + world_offset_y,
        extra_blocked, blocked_ctx);
}

/* arena-scoped BFS: same as encounter_pathfind but uses a smaller grid.
   arena_base_x/y: world-space origin of the arena.
   arena_w/h: arena dimensions in tiles (must be <= PATHFIND_ARENA_MAX). */
static inline PathResult encounter_pathfind_arena(
    const CollisionMap* cmap, int world_offset_x, int world_offset_y,
    int src_x, int src_y, int dst_x, int dst_y,
    pathfind_blocked_fn extra_blocked, void* blocked_ctx,
    int arena_base_x, int arena_base_y, int arena_w, int arena_h
) {
    return pathfind_step_arena(cmap, 0,
        src_x + world_offset_x, src_y + world_offset_y,
        dst_x + world_offset_x, dst_y + world_offset_y,
        extra_blocked, blocked_ctx,
        arena_base_x + world_offset_x, arena_base_y + world_offset_y,
        arena_w, arena_h);
}

/* shared click-to-move: BFS toward destination, take up to 2 steps (run).
   call each tick when player_dest is set. clears dest when arrived.
   extra_blocked/blocked_ctx: optional dynamic obstacle callback for BFS.
   returns steps taken (0, 1, or 2). */
static inline int encounter_move_toward_dest(
    Player* p, int* dest_x, int* dest_y,
    const CollisionMap* cmap, int world_offset_x, int world_offset_y,
    encounter_walkable_fn is_walkable, void* ctx,
    pathfind_blocked_fn extra_blocked, void* blocked_ctx,
    int arena_base_x, int arena_base_y, int arena_w, int arena_h
) {
    if (*dest_x < 0 || *dest_y < 0) return 0;
    if (p->x == *dest_x && p->y == *dest_y) {
        *dest_x = -1; *dest_y = -1;
        return 0;
    }
    int steps = 0;
    for (int step = 0; step < 2; step++) {
        if (p->x == *dest_x && p->y == *dest_y) break;
        PathResult pr = (arena_w > 0)
            ? encounter_pathfind_arena(cmap, world_offset_x, world_offset_y,
                                       p->x, p->y, *dest_x, *dest_y,
                                       extra_blocked, blocked_ctx,
                                       arena_base_x, arena_base_y, arena_w, arena_h)
            : encounter_pathfind(cmap, world_offset_x, world_offset_y,
                                  p->x, p->y, *dest_x, *dest_y,
                                  extra_blocked, blocked_ctx);
        if (!pr.found || (pr.next_dx == 0 && pr.next_dy == 0)) break;
        int nx = p->x + pr.next_dx, ny = p->y + pr.next_dy;
        if (!is_walkable(ctx, nx, ny)) break;
        p->x = nx; p->y = ny;
        steps++;
    }
    p->is_running = (steps == 2);
    p->dest_x = p->x; p->dest_y = p->y;
    return steps;
}


/* footprint helpers for player-vs-target chase and range checks. */
static inline int encounter_entity_footprint_distance(
    int ax, int ay, int a_size,
    int bx, int by, int b_size
) {
    int ax1 = ax + a_size - 1;
    int ay1 = ay + a_size - 1;
    int bx1 = bx + b_size - 1;
    int by1 = by + b_size - 1;

    int dx = 0;
    if (ax1 < bx) dx = bx - ax1;
    else if (bx1 < ax) dx = ax - bx1;

    int dy = 0;
    if (ay1 < by) dy = by - ay1;
    else if (by1 < ay) dy = ay - by1;

    return dx > dy ? dx : dy;
}

static inline int encounter_entity_footprints_overlap(
    int ax, int ay, int a_size,
    int bx, int by, int b_size
) {
    return !(ax + a_size <= bx || bx + b_size <= ax ||
             ay + a_size <= by || by + b_size <= ay);
}

/* check if player can attack: in range AND has LOS (if blockers present).
   returns 1 if ready to attack, 0 if blocked or out of range.
   encounters without LOS blockers pass NULL/0 for unconditional range check. */
static inline int encounter_player_can_attack(
    int player_x, int player_y,
    int target_x, int target_y, int target_size, int attack_range,
    const LOSBlocker* los_blockers, int los_blocker_count
) {
    int dist = encounter_entity_footprint_distance(player_x, player_y, 1,
                                                   target_x, target_y, target_size);
    if (dist < 1 || dist > attack_range) return 0;
    if (!los_blockers || los_blocker_count == 0) return 1;
    return entity_has_line_of_sight(los_blockers, los_blocker_count,
                                    player_x, player_y, 1,
                                    target_x, target_y, target_size,
                                    attack_range);
}

#define ENCOUNTER_ATTACK_SEEK_MAX_TILES 128

typedef struct {
    int x;
    int y;
} EncounterAttackSeekTile;

static inline void encounter_attack_seek_add_tile(
    EncounterAttackSeekTile* tiles, int* count,
    int x, int y, int world_offset_x, int world_offset_y,
    pathfind_blocked_fn extra_blocked, void* blocked_ctx
) {
    if (extra_blocked &&
            extra_blocked(blocked_ctx, x + world_offset_x, y + world_offset_y))
        return;
    if (*count >= ENCOUNTER_ATTACK_SEEK_MAX_TILES) {
        fprintf(stderr, "attack seek tile capacity exceeded: %d\n",
            ENCOUNTER_ATTACK_SEEK_MAX_TILES);
        abort();
    }
    tiles[*count] = (EncounterAttackSeekTile){x, y};
    (*count)++;
}

static inline int encounter_attack_seek_nearest_dsq(
    int x, int y, const EncounterAttackSeekTile* tiles, int count
) {
    int best = 0x3fffffff;
    for (int i = 0; i < count; i++) {
        int dx = x - tiles[i].x;
        int dy = y - tiles[i].y;
        int dsq = dx * dx + dy * dy;
        if (dsq < best) best = dsq;
    }
    return best;
}

static inline int encounter_attack_seek_has_exact_tile(
    int x, int y, const EncounterAttackSeekTile* tiles, int count
) {
    for (int i = 0; i < count; i++) {
        if (tiles[i].x == x && tiles[i].y == y) return 1;
    }
    return 0;
}

static inline PathResult encounter_pathfind_arena_attack_approach(
    const CollisionMap* cmap, int world_offset_x, int world_offset_y,
    int src_x, int src_y,
    int target_x, int target_y, int target_size, int attack_range,
    encounter_walkable_fn is_walkable, void* ctx,
    pathfind_blocked_fn extra_blocked, void* blocked_ctx,
    const LOSBlocker* los_blockers, int los_blocker_count,
    int arena_base_x, int arena_base_y, int arena_w, int arena_h
) {
    PathResult result = {0, 0, 0, src_x, src_y};

    if (arena_w <= 0 || arena_w > PATHFIND_ARENA_MAX ||
        arena_h <= 0 || arena_h > PATHFIND_ARENA_MAX) {
        fprintf(stderr, "attack approach arena dimensions out of bounds: %dx%d\n",
            arena_w, arena_h);
        abort();
    }

    int local_src_x = src_x - arena_base_x;
    int local_src_y = src_y - arena_base_y;
    if (local_src_x < 0 || local_src_x >= arena_w ||
        local_src_y < 0 || local_src_y >= arena_h) {
        return result;
    }

    EncounterAttackSeekTile seek_tiles[ENCOUNTER_ATTACK_SEEK_MAX_TILES];
    int seek_count = 0;
    for (int xx = 0; xx < target_size; xx++) {
        int x = target_x + xx;
        encounter_attack_seek_add_tile(
            seek_tiles, &seek_count, x, target_y - 1,
            world_offset_x, world_offset_y, extra_blocked, blocked_ctx);
        encounter_attack_seek_add_tile(
            seek_tiles, &seek_count, x, target_y + target_size,
            world_offset_x, world_offset_y, extra_blocked, blocked_ctx);
    }
    for (int yy = 0; yy < target_size; yy++) {
        int y = target_y + yy;
        encounter_attack_seek_add_tile(
            seek_tiles, &seek_count, target_x - 1, y,
            world_offset_x, world_offset_y, extra_blocked, blocked_ctx);
        encounter_attack_seek_add_tile(
            seek_tiles, &seek_count, target_x + target_size, y,
            world_offset_x, world_offset_y, extra_blocked, blocked_ctx);
    }
    static _Thread_local uint16_t approach_gen[PATHFIND_ARENA_MAX][PATHFIND_ARENA_MAX];
    static _Thread_local int8_t approach_via[PATHFIND_ARENA_MAX][PATHFIND_ARENA_MAX];
    static _Thread_local int16_t approach_cost[PATHFIND_ARENA_MAX][PATHFIND_ARENA_MAX];
    static _Thread_local uint16_t approach_gen_counter = 0;
    approach_gen_counter++;
    if (approach_gen_counter == 0) {
        memset(approach_gen, 0, sizeof(approach_gen));
        approach_gen_counter = 1;
    }
    uint16_t gen = approach_gen_counter;

    #define APPROACH_VISITED(x, y) (approach_gen[(x)][(y)] == gen)
    #define APPROACH_VISIT(x, y, v, c) do { \
        approach_gen[(x)][(y)] = gen; \
        approach_via[(x)][(y)] = (v); \
        approach_cost[(x)][(y)] = (c); \
    } while(0)
    #define APPROACH_VIA(x, y) approach_via[(x)][(y)]
    #define APPROACH_COST(x, y) approach_cost[(x)][(y)]
    #define APPROACH_EB(x, y) \
        (extra_blocked && extra_blocked( \
            blocked_ctx, (x) + world_offset_x, (y) + world_offset_y))

    int queue_x[PATHFIND_MAX_QUEUE_ARENA];
    int queue_y[PATHFIND_MAX_QUEUE_ARENA];
    int head = 0;
    int tail = 0;
    APPROACH_VISIT(local_src_x, local_src_y, VIA_START, 0);
    pathfind_enqueue_or_abort(
        queue_x, queue_y, &tail, PATHFIND_MAX_QUEUE_ARENA,
        local_src_x, local_src_y);

    int selected_x = -1;
    int selected_y = -1;
    int min_explored_x = local_src_x;
    int min_explored_y = local_src_y;
    int max_explored_x = local_src_x;
    int max_explored_y = local_src_y;

    static const int dir_dx[8] = {-1, 1, 0, 0, -1, 1, -1, 1};
    static const int dir_dy[8] = {0, 0, -1, 1, -1, -1, 1, 1};
    static const int dir_via[8] = {
        VIA_W, VIA_E, VIA_S, VIA_N, VIA_SW, VIA_SE, VIA_NW, VIA_NE
    };

    while (head < tail) {
        int cur_x = queue_x[head];
        int cur_y = queue_y[head];
        head++;

        int tile_x = arena_base_x + cur_x;
        int tile_y = arena_base_y + cur_y;
        if (is_walkable(ctx, tile_x, tile_y) &&
                !APPROACH_EB(tile_x, tile_y) &&
                (seek_count > 0
                    ? encounter_attack_seek_has_exact_tile(
                        tile_x, tile_y, seek_tiles, seek_count)
                    : encounter_player_can_attack(
                        tile_x, tile_y, target_x, target_y, target_size,
                        attack_range, los_blockers, los_blocker_count))) {
            selected_x = cur_x;
            selected_y = cur_y;
            break;
        }

        int abs_x = tile_x + world_offset_x;
        int abs_y = tile_y + world_offset_y;
        int next_cost = APPROACH_COST(cur_x, cur_y) + 1;

        for (int i = 0; i < 8; i++) {
            int dx = dir_dx[i];
            int dy = dir_dy[i];
            int next_x = cur_x + dx;
            int next_y = cur_y + dy;
            if (next_x < 0 || next_x >= arena_w ||
                    next_y < 0 || next_y >= arena_h)
                continue;
            if (APPROACH_VISITED(next_x, next_y)) continue;
            if (!collision_traversable_step(cmap, 0, abs_x, abs_y, dx, dy))
                continue;

            int next_tile_x = tile_x + dx;
            int next_tile_y = tile_y + dy;
            if (!is_walkable(ctx, next_tile_x, next_tile_y)) continue;
            if (APPROACH_EB(next_tile_x, next_tile_y)) continue;
            if (dx != 0 && dy != 0) {
                if (APPROACH_EB(tile_x + dx, tile_y)) continue;
                if (APPROACH_EB(tile_x, tile_y + dy)) continue;
                if (!is_walkable(ctx, tile_x + dx, tile_y)) continue;
                if (!is_walkable(ctx, tile_x, tile_y + dy)) continue;
            }

            pathfind_enqueue_or_abort(
                queue_x, queue_y, &tail, PATHFIND_MAX_QUEUE_ARENA,
                next_x, next_y);
            APPROACH_VISIT(next_x, next_y, dir_via[i], next_cost);
            if (next_x < min_explored_x) min_explored_x = next_x;
            if (next_y < min_explored_y) min_explored_y = next_y;
            if (next_x > max_explored_x) max_explored_x = next_x;
            if (next_y > max_explored_y) max_explored_y = next_y;
        }
    }

    if (selected_x < 0 && seek_count > 0) {
        int first_local_x = seek_tiles[0].x - arena_base_x;
        int first_local_y = seek_tiles[0].y - arena_base_y;
        int scan_min_x = min_explored_x > first_local_x - PATHFIND_MAX_FALLBACK_RADIUS
            ? min_explored_x : first_local_x - PATHFIND_MAX_FALLBACK_RADIUS;
        int scan_min_y = min_explored_y > first_local_y - PATHFIND_MAX_FALLBACK_RADIUS
            ? min_explored_y : first_local_y - PATHFIND_MAX_FALLBACK_RADIUS;
        int scan_max_x = max_explored_x > first_local_x + PATHFIND_MAX_FALLBACK_RADIUS
            ? max_explored_x : first_local_x + PATHFIND_MAX_FALLBACK_RADIUS;
        int scan_max_y = max_explored_y > first_local_y + PATHFIND_MAX_FALLBACK_RADIUS
            ? max_explored_y : first_local_y + PATHFIND_MAX_FALLBACK_RADIUS;
        if (scan_min_x < 0) scan_min_x = 0;
        if (scan_min_y < 0) scan_min_y = 0;
        if (scan_max_x >= arena_w) scan_max_x = arena_w - 1;
        if (scan_max_y >= arena_h) scan_max_y = arena_h - 1;

        int best_dsq = 0x3fffffff;
        int best_cost = 100;
        for (int x = scan_min_x; x <= scan_max_x; x++) {
            for (int y = scan_min_y; y <= scan_max_y; y++) {
                if (!APPROACH_VISITED(x, y)) continue;
                int tile_x = arena_base_x + x;
                int tile_y = arena_base_y + y;
                if (!is_walkable(ctx, tile_x, tile_y)) continue;
                int cost = APPROACH_COST(x, y);
                if (cost >= 100) continue;
                int dsq = encounter_attack_seek_nearest_dsq(
                    tile_x, tile_y, seek_tiles, seek_count);
                if (dsq < best_dsq || (dsq == best_dsq && cost < best_cost)) {
                    selected_x = x;
                    selected_y = y;
                    best_dsq = dsq;
                    best_cost = cost;
                }
            }
        }
    }

    int cur_x = selected_x;
    int cur_y = selected_y;
    if (selected_x < 0) goto approach_done;

    result.found = 1;
    result.dest_x = arena_base_x + selected_x;
    result.dest_y = arena_base_y + selected_y;
    if (selected_x == local_src_x && selected_y == local_src_y)
        goto approach_done;

    while (1) {
        int v = APPROACH_VIA(cur_x, cur_y);
        int prev_x = cur_x;
        int prev_y = cur_y;
        if (v & VIA_W) prev_x++;
        else if (v & VIA_E) prev_x--;
        if (v & VIA_S) prev_y++;
        else if (v & VIA_N) prev_y--;

        if (prev_x == local_src_x && prev_y == local_src_y) {
            result.next_dx = cur_x - local_src_x;
            result.next_dy = cur_y - local_src_y;
            goto approach_done;
        }

        cur_x = prev_x;
        cur_y = prev_y;
        if (APPROACH_VIA(cur_x, cur_y) == VIA_NONE ||
                APPROACH_VIA(cur_x, cur_y) == VIA_START) {
            result.found = 0;
            result.next_dx = 0;
            result.next_dy = 0;
            goto approach_done;
        }
    }

approach_done:
    #undef APPROACH_VISITED
    #undef APPROACH_VISIT
    #undef APPROACH_VIA
    #undef APPROACH_COST
    #undef APPROACH_EB
    return result;
}

/* auto-walk toward attack target: handles out-of-range, blocked LOS, and under-NPC.
   the caller owns the policy; this helper only computes the chase step.
   los_blockers/los_blocker_count: LOS blocking entities (pillars). NULL/0 = no LOS check.
   returns 1 if player moved (chasing), 0 if ready to attack or stuck. */
static inline int encounter_chase_attack_target(
    Player* p, int target_x, int target_y, int target_size, int attack_range,
    const CollisionMap* cmap, int world_offset_x, int world_offset_y,
    encounter_walkable_fn is_walkable, void* ctx,
    pathfind_blocked_fn extra_blocked, void* blocked_ctx,
    const LOSBlocker* los_blockers, int los_blocker_count,
    int arena_base_x, int arena_base_y, int arena_w, int arena_h
) {
    int dist = encounter_entity_footprint_distance(p->x, p->y, 1,
                                                   target_x, target_y, target_size);

    /* player under NPC (dist=0): walk to nearest tile outside the target footprint. */
    if (dist == 0) {
        int max_r = (target_size + 1) / 2 + 1;
        int best_dsq = 9999, bx = -1, by = -1;
        for (int dy = -max_r; dy <= max_r; dy++) {
            for (int dx = -max_r; dx <= max_r; dx++) {
                if (dx == 0 && dy == 0) continue;
                int nx = p->x + dx, ny = p->y + dy;
                if (!is_walkable(ctx, nx, ny)) continue;
                if (encounter_entity_footprints_overlap(nx, ny, 1,
                                                        target_x, target_y, target_size))
                    continue;
                int d = dx * dx + dy * dy;
                if (d < best_dsq) { best_dsq = d; bx = nx; by = ny; }
            }
        }
        if (bx < 0) return 0;
        int steps = 0;
        for (int step = 0; step < 2; step++) {
            if (p->x == bx && p->y == by) break;
            PathResult pr = (arena_w > 0)
                ? encounter_pathfind_arena(cmap, world_offset_x, world_offset_y,
                                           p->x, p->y, bx, by,
                                           extra_blocked, blocked_ctx,
                                           arena_base_x, arena_base_y, arena_w, arena_h)
                : encounter_pathfind(cmap, world_offset_x, world_offset_y,
                                      p->x, p->y, bx, by,
                                      extra_blocked, blocked_ctx);
            if (!pr.found || (pr.next_dx == 0 && pr.next_dy == 0)) break;
            int nx = p->x + pr.next_dx, ny = p->y + pr.next_dy;
            if (!is_walkable(ctx, nx, ny)) break;
            p->x = nx; p->y = ny;
            steps++;
        }
        p->is_running = (steps == 2);
        p->dest_x = p->x; p->dest_y = p->y;
        return steps > 0 ? 1 : 0;
    }

    /* in range + LOS: ready to attack, no movement needed */
    if (encounter_player_can_attack(p->x, p->y, target_x, target_y,
                                     target_size, attack_range,
                                     los_blockers, los_blocker_count))
        return 0;

    int cx, cy;
    cx = -1;
    cy = -1;

    if (arena_w <= 0) {
        int scan_min_x = target_x - attack_range;
        int scan_max_x = target_x + target_size - 1 + attack_range;
        int scan_min_y = target_y - attack_range;
        int scan_max_y = target_y + target_size - 1 + attack_range;

        cx = -1;
        cy = -1;
        int best_player_dsq = 0x3fffffff;
        int best_target_dist = 0x3fffffff;
        if (scan_min_x <= scan_max_x && scan_min_y <= scan_max_y) {
            for (int yy = scan_min_y; yy <= scan_max_y; yy++) {
                for (int xx = scan_min_x; xx <= scan_max_x; xx++) {
                    if (!is_walkable(ctx, xx, yy)) continue;
                    if (!encounter_player_can_attack(xx, yy, target_x, target_y,
                            target_size, attack_range,
                            los_blockers, los_blocker_count))
                        continue;
                    int dx = xx - p->x;
                    int dy = yy - p->y;
                    int player_dsq = dx * dx + dy * dy;
                    int target_dist = encounter_entity_footprint_distance(
                        xx, yy, 1, target_x, target_y, target_size);
                    if (player_dsq < best_player_dsq ||
                            (player_dsq == best_player_dsq &&
                             target_dist < best_target_dist)) {
                        best_player_dsq = player_dsq;
                        best_target_dist = target_dist;
                        cx = xx;
                        cy = yy;
                    }
                }
            }
        }

        if (cx < 0) {
            cx = p->x < target_x ? target_x :
                 (p->x > target_x + target_size - 1 ? target_x + target_size - 1 : p->x);
            cy = p->y < target_y ? target_y :
                 (p->y > target_y + target_size - 1 ? target_y + target_size - 1 : p->y);
        }
    }

    int steps = 0;
    for (int step = 0; step < 2; step++) {
        if (encounter_player_can_attack(p->x, p->y, target_x, target_y,
                                         target_size, attack_range,
                                         los_blockers, los_blocker_count))
            break;
        PathResult pr = (arena_w > 0)
            ? encounter_pathfind_arena_attack_approach(
                cmap, world_offset_x, world_offset_y,
                p->x, p->y,
                target_x, target_y, target_size, attack_range,
                is_walkable, ctx,
                extra_blocked, blocked_ctx,
                los_blockers, los_blocker_count,
                arena_base_x, arena_base_y, arena_w, arena_h)
            : encounter_pathfind(cmap, world_offset_x, world_offset_y,
                p->x, p->y, cx, cy,
                extra_blocked, blocked_ctx);
        if (!pr.found || (pr.next_dx == 0 && pr.next_dy == 0)) break;
        int nx = p->x + pr.next_dx, ny = p->y + pr.next_dy;
        if (!is_walkable(ctx, nx, ny)) break;
        p->x = nx; p->y = ny;
        steps++;
    }
    p->is_running = (steps == 2);
    p->dest_x = p->x; p->dest_y = p->y;
    return steps > 0 ? 1 : 0;
}


typedef int (*encounter_npc_blocked_fn)(void* ctx, int x, int y, int size);
typedef int (*encounter_npc_overlap_hold_fn)(void* ctx);

#define ENCOUNTER_NPC_UNDER_PLAYER_NONE  0
#define ENCOUNTER_NPC_UNDER_PLAYER_MOVED 1
#define ENCOUNTER_NPC_UNDER_PLAYER_HELD  2

/* when an NPC overlaps the player (AABB overlap), it shuffles one tile in a
   random cardinal direction. matches osrs-sdk Mob.ts:109-153 behavior:
   50% pick X-axis vs Y-axis, then 50% +1 or -1 on that axis.
   hold_overlap lets the caller preserve the one-tick "player just clicked this
   mob, so it cannot move off" rule. returns MOVED, HELD, or NONE. */
static inline int encounter_npc_step_out_from_under(
    int* npc_x, int* npc_y, int npc_size,
    int player_x, int player_y,
    encounter_npc_blocked_fn is_blocked, void* ctx,
    encounter_npc_overlap_hold_fn hold_overlap,
    uint32_t* rng
) {
    /* AABB overlap check (handles multi-tile NPCs) */
    int overlap = !(*npc_x >= player_x + 1 || *npc_x + npc_size <= player_x ||
                    *npc_y >= player_y + 1 || *npc_y + npc_size <= player_y);
    if (!overlap) return ENCOUNTER_NPC_UNDER_PLAYER_NONE;
    if (hold_overlap && hold_overlap(ctx)) return ENCOUNTER_NPC_UNDER_PLAYER_HELD;

    /* 4 cardinal directions: +x, -x, +y, -y */
    int dirs[4][2] = {{1,0}, {-1,0}, {0,1}, {0,-1}};

    /* random start: 50% X-axis first (dirs 0,1) vs Y-axis first (dirs 2,3),
       then 50% positive vs negative on that axis */
    int axis = encounter_rand_int(rng, 2);       /* 0=X, 1=Y */
    int sign = encounter_rand_int(rng, 2);        /* 0=positive, 1=negative */
    int order[4];
    order[0] = axis * 2 + sign;         /* primary: chosen axis+sign */
    order[1] = axis * 2 + (1 - sign);   /* secondary: chosen axis, other sign */
    order[2] = (1 - axis) * 2 + sign;   /* tertiary: other axis, same sign */
    order[3] = (1 - axis) * 2 + (1 - sign); /* last: other axis, other sign */

    for (int i = 0; i < 4; i++) {
        int nx = *npc_x + dirs[order[i]][0];
        int ny = *npc_y + dirs[order[i]][1];
        /* InfernoTrainer Mob.ts:128-142: 1-tile shuffle per tick, validated
           via normal edge-tile movement system. for size>1 NPCs, full escape
           takes multiple ticks. anchor walkability matches InfernoTrainer's
           canTileBePathedTo check on the leading edge. */
        if (!is_blocked(ctx, nx, ny, npc_size)) {
            *npc_x = nx;
            *npc_y = ny;
            return ENCOUNTER_NPC_UNDER_PLAYER_MOVED;
        }
    }
    return ENCOUNTER_NPC_UNDER_PLAYER_NONE;
}


/** check if the leading edge tiles are clear for an NPC moving in direction (dx, dy).
    for size>1 NPCs, OSRS checks the tiles along the leading edge that the NPC
    sweeps through — not the full destination footprint. for diagonal moves, each
    edge strip extends by 1 tile to cover the corner.
    ref: InfernoTrainer Mob.ts:229-270 getXMovementTiles/getYMovementTiles.
    is_blocked is called with size=1 for each individual edge tile. */
static inline int encounter_npc_x_edge_clear(
    int x, int y, int size, int dx, int dy,
    encounter_npc_blocked_fn is_blocked, void* ctx
) {
    if (dx == 0) return 1;
    int ex = (dx == 1) ? x + size : x - 1;
    int y_start = (dy == -1) ? y - 1 : y;
    int y_end = (dy == 1) ? y + size : y + size - 1;
    for (int ey = y_start; ey <= y_end; ey++)
        if (is_blocked(ctx, ex, ey, 1)) return 0;
    return 1;
}

static inline int encounter_npc_y_edge_clear(
    int x, int y, int size, int dx, int dy,
    encounter_npc_blocked_fn is_blocked, void* ctx
) {
    if (dy == 0) return 1;
    int ey = (dy == 1) ? y + size : y - 1;
    int x_start = (dx == -1) ? x - 1 : x;
    int x_end = (dx == 1) ? x + size : x + size - 1;
    for (int ex = x_start; ex <= x_end; ex++)
        if (is_blocked(ctx, ex, ey, 1)) return 0;
    return 1;
}

static inline int encounter_npc_axis_gap(int a, int a_size, int b, int b_size) {
    int a_max = a + a_size - 1;
    int b_max = b + b_size - 1;
    if (a_max < b) return b - a_max;
    if (b_max < a) return a - b_max;
    return 0;
}

static inline int encounter_npc_axis_dir(int a, int a_size, int b, int b_size) {
    int a_max = a + a_size - 1;
    int b_max = b + b_size - 1;
    if (a_max < b) return 1;
    if (b_max < a) return -1;
    return 0;
}

static inline int encounter_npc_try_step(
    int* x, int* y, int size, int dx, int dy,
    encounter_npc_blocked_fn is_blocked, void* ctx
) {
    if (dx == 0 && dy == 0) return 0;
    if (size <= 1) {
        if (!is_blocked(ctx, *x + dx, *y + dy, 1)) {
            *x += dx;
            *y += dy;
            return 1;
        }
        return 0;
    }

    int x_clear = encounter_npc_x_edge_clear(*x, *y, size, dx, dy, is_blocked, ctx);
    int y_clear = encounter_npc_y_edge_clear(*x, *y, size, dx, dy, is_blocked, ctx);
    if (x_clear && y_clear) {
        *x += dx;
        *y += dy;
        return 1;
    }
    return 0;
}

/** OSRS-shaped NPC step toward target. tries diagonal first, then x-only,
    then y-only when RuneLite's travel rule allows the y fallback.

    for size>1 NPCs, validates movement by checking EDGE TILES the NPC sweeps
    through, not just the destination footprint. for diagonal moves, both the
    x-edge and y-edge must be clear (each extended by 1 tile for the corner).
    ref: RuneLite WorldArea.calculateNextTravellingPoint and osrs-sdk
    Mob.ts:160-270 movementStep + getX/YMovementTiles.

    stop_at_melee_distance matches RuneLite WorldArea.calculateNextTravellingPoint:
    overlap returns no normal step, cardinal melee contact returns no step,
    and diagonal contact tries x-only.

    returns 1 if moved, 0 if blocked or already at target. */
static inline int encounter_npc_step_toward(
    int* x, int* y, int tx, int ty, int npc_size,
    int target_size, int stop_at_melee_distance,
    encounter_npc_blocked_fn is_blocked, void* ctx
) {
    int size = npc_size;
    int x_gap = encounter_npc_axis_gap(*x, size, tx, target_size);
    int y_gap = encounter_npc_axis_gap(*y, size, ty, target_size);
    int raw_dx = tx - *x;
    int raw_dy = ty - *y;
    int dx = (raw_dx > 0) - (raw_dx < 0);
    int dy = (raw_dy > 0) - (raw_dy < 0);

    if (x_gap == 0 && y_gap == 0) return 0;
    if (stop_at_melee_distance && x_gap + y_gap == 1) return 0;
    if (dx == 0 && dy == 0) return 0;

    if (stop_at_melee_distance && x_gap == 1 && y_gap == 1) {
        return encounter_npc_try_step(x, y, size, dx, 0, is_blocked, ctx);
    }

    if (dx != 0 && dy != 0 &&
        encounter_npc_try_step(x, y, size, dx, dy, is_blocked, ctx))
        return 1;
    if (dx != 0 && encounter_npc_try_step(x, y, size, dx, 0, is_blocked, ctx))
        return 1;
    int max_abs_delta = abs(raw_dx) > abs(raw_dy) ? abs(raw_dx) : abs(raw_dy);
    if (dy != 0 && max_abs_delta > 1 &&
        encounter_npc_try_step(x, y, size, 0, dy, is_blocked, ctx))
        return 1;
    return 0;
}
/* shared damage application helpers                                         */
/*                                                                           */
/* ENCOUNTERS: use these instead of manually subtracting HP, clamping,       */
/* and setting hit splat flags. prevents bugs from forgetting a step.        */
/** apply damage to a player. updates HP (clamped to 0), sets hit splat flags,
    and accumulates damage into a per-tick tracker (for reward calculation).
    damage_tracker can be NULL if not needed.
    always sets hit_landed_this_tick so the renderer shows a splat —
    0 damage produces a blue "miss" splat (standard OSRS behavior). */
static inline void encounter_damage_player(
    Player* p, int damage, float* damage_tracker
) {
    if (damage > 0) {
        p->current_hitpoints -= damage;
        if (p->current_hitpoints < 0) p->current_hitpoints = 0;
        if (damage_tracker) *damage_tracker += (float)damage;
    }
    p->hit_landed_this_tick = 1;
    p->hit_damage = damage > 0 ? damage : 0;
}

/** apply damage to an NPC-like entity via raw field pointers.
    works with any struct that has hp/hit_landed/hit_damage int fields.
    returns the damage applied after capping to current HP.
    always sets hit_landed so the renderer shows a splat —
    0 damage produces a blue "miss" splat (standard OSRS behavior). */
static inline int encounter_damage_npc(
    int* hp, int* hit_landed, int* hit_damage, int damage
) {
    int applied = 0;
    if (damage > 0) {
        applied = damage > *hp ? *hp : damage;
        if (applied < 0) applied = 0;
        *hp -= applied;
        if (*hp < 0) *hp = 0;
    }
    *hit_landed = 1;
    *hit_damage = applied;
    return applied;
}


/** resolve a single NPC's pending hit. tick down, apply damage when it lands.
    ice barrage: sets *frozen_ticks = BARRAGE_FREEZE_TICKS on hit.
    blood barrage: accumulates landed damage into *blood_heal_acc for 25% heal.
    returns 1 if hit landed this call, 0 otherwise. */
static inline int encounter_resolve_npc_pending_hit(
    EncounterPendingHit* ph,
    int* npc_hp, int* hit_landed, int* hit_damage,
    int* frozen_ticks, int* blood_heal_acc, float* damage_dealt_acc
) {
    (void)frozen_ticks;  /* freeze applied at cast time, not land time */
    if (!ph->active) return 0;
    ph->ticks_remaining--;
    if (ph->ticks_remaining > 0) return 0;

    /* hit landed */
    int dmg = encounter_damage_npc(npc_hp, hit_landed, hit_damage, ph->damage);
    if (damage_dealt_acc) *damage_dealt_acc += dmg;

    /* blood barrage: accumulate damage for 25% heal (at land time — heal depends on damage) */
    if (ph->spell_type == ENCOUNTER_SPELL_BLOOD && blood_heal_acc)
        *blood_heal_acc += dmg;

    ph->active = 0;
    return 1;
}

/** resolve player pending hits (NPC attacks landing on the player).
    ticks down each hit, applies damage when it lands, handles deferred
    prayer checks (jad-style: check_prayer=1 re-checks at land time).
    encounters MUST call this each tick for projectile-based NPC attacks.
    prayer_correct_count: incremented for each deferred prayer check that succeeds.
    multiple hits can land on the same tick (e.g. mager + ranger). */
static inline void encounter_resolve_player_pending_hits(
    EncounterPendingHitQueue* queue,
    Player* player, OverheadPrayer active_prayer,
    float* damage_received_acc, int* prayer_correct_count,
    int* off_prayer_hit_count
) {
    for (int i = 0; i < queue->count; i++) {
        EncounterPendingHit* hit = &queue->hits[i];
        /* deferred prayer check (jad): lock in damage at T + prayer_check_delay.
           runs BEFORE ticks_remaining decrement so the check happens on the exact
           tick prayer_check_delay reaches 0, regardless of whether the hit lands
           same tick or later. after the check, damage is frozen (possibly 0)
           and further flicks don't affect this hit. */
        if (hit->check_prayer && hit->prayer_check_delay > 0) {
            hit->prayer_check_delay--;
            if (hit->prayer_check_delay == 0) {
                if (encounter_prayer_correct_for_style(active_prayer, hit->attack_style)) {
                    hit->damage = 0;
                    if (prayer_correct_count) (*prayer_correct_count)++;
                } else if (hit->damage > 0 && hit->attack_style != ATTACK_STYLE_NONE) {
                    if (off_prayer_hit_count) (*off_prayer_hit_count)++;
                }
                hit->check_prayer = 0;
            }
        }
        hit->ticks_remaining--;
        if (hit->ticks_remaining <= 0) {
            int dmg = hit->damage;
            if (hit->check_prayer) {
                if (encounter_prayer_correct_for_style(active_prayer, hit->attack_style)) {
                    dmg = 0;
                    if (prayer_correct_count) (*prayer_correct_count)++;
                } else if (dmg > 0 && hit->attack_style != ATTACK_STYLE_NONE) {
                    if (off_prayer_hit_count) (*off_prayer_hit_count)++;
                }
            } else if (dmg > 0 && hit->attack_style != ATTACK_STYLE_NONE) {
                if (off_prayer_hit_count) (*off_prayer_hit_count)++;
            }

            encounter_damage_player(player, dmg, damage_received_acc);
            encounter_pending_hit_queue_remove(queue, i, "player");
            i--;
        }
    }
}


/** clear all per-tick animation/event flags on a player.
    call at the start of each encounter tick, then set flags as events happen.
    the renderer reads these once per frame via RenderEntity. */
static inline void encounter_clear_tick_flags(Player* p) {
    p->attack_style_this_tick = ATTACK_STYLE_NONE;
    p->magic_type_this_tick = 0;
    p->hit_landed_this_tick = 0;
    p->hit_damage = 0;
    p->hit_was_successful = 0;
    p->elysian_proc_this_tick = 0;
    p->cast_veng_this_tick = 0;
    p->ate_food_this_tick = 0;
    p->ate_karambwan_this_tick = 0;
    p->used_special_this_tick = 0;
}


/** resolve RNG seed for encounter reset. priority: explicit seed > saved state > default.
    all encounters MUST use this to ensure consistent RNG initialization. */
static inline uint32_t encounter_resolve_seed(uint32_t saved_rng, uint32_t explicit_seed) {
    uint32_t rng = 12345;
    if (saved_rng != 0) rng = saved_rng;
    if (explicit_seed != 0) rng = explicit_seed;
    return rng;
}
/* shared prayer drain                                                       */
/*                                                                           */
/* ENCOUNTERS: call encounter_drain_all_prayers() each tick to drain prayer  */
/* points at the correct OSRS rate. all encounters with overhead prayers     */
/* MUST use this — do not hand-roll prayer drain logic.                      */
/*                                                                           */
/* OSRS drain formula: each prayer has a "drain effect" value.               */
/* drain rate = 1 point per floor((18 + floor(bonus/4)) / drain_effect)      */
/* seconds. the drain counter increments each tick; when it reaches the      */
/* threshold, 1 prayer point is drained and the counter resets.              */
/*                                                                           */
/* protection prayers (melee/ranged/magic): drain_effect = 12               */
/* rigour: drain_effect = 24, augury: drain_effect = 24                     */
/** drain effect values for overhead prayers.
    from the OSRS prayer table — higher values drain faster.
    used by both PvE encounters and PvP. */
static inline int encounter_overhead_drain_effect(OverheadPrayer prayer) {
    switch (prayer) {
        case PRAYER_PROTECT_MELEE:  return 12;
        case PRAYER_PROTECT_RANGED: return 12;
        case PRAYER_PROTECT_MAGIC:  return 12;
        case PRAYER_SMITE:          return 12;
        case PRAYER_REDEMPTION:     return 6;
        default: return 0;
    }
}

/** drain effect values for offensive prayers. ref: osrs wiki prayer table. */
static inline int encounter_offensive_drain_effect(OffensivePrayer prayer) {
    switch (prayer) {
        case OFFENSIVE_PRAYER_PIETY:       return 24;
        case OFFENSIVE_PRAYER_RIGOUR:      return 24;
        case OFFENSIVE_PRAYER_AUGURY:      return 24;
        case OFFENSIVE_PRAYER_MELEE_LOW:   return 6;
        case OFFENSIVE_PRAYER_RANGED_LOW:  return 6;
        case OFFENSIVE_PRAYER_MAGIC_LOW:   return 6;
        default: return 0;
    }
}

static inline int encounter_player_prayer_bonus(const Player* p) {
    EquipmentBonuses bonuses;
    osrs_sum_equipment_bonuses(p->equipped, &bonuses);
    return bonuses.prayer;
}

/** drain both overhead and offensive prayer for one tick.
    handles activation-tick skip (prayers activated this tick do not drain),
    the shared drain counter, and pp=0 auto-clear (all prayers off when empty).
    prayer_bonus: player's total prayer equipment bonus (typically 0-30).
    ref: osrs-sdk PrayerController.ts:44-59, wiki "Prayer flicking" section. */
static inline void encounter_drain_all_prayers(Player* p, int prayer_bonus) {
    /* active-but-not-just-activated prayers contribute to drain this tick.
       ref: wiki: "the game does not drain prayer for prayers on the tick
       they are activated". this is what makes 1-tick flicking free. */
    int overhead_effect  = p->prayer_just_activated
        ? 0 : encounter_overhead_drain_effect(p->prayer);
    int offensive_effect = p->offensive_prayer_just_activated
        ? 0 : encounter_offensive_drain_effect(p->offensive_prayer);
    int total = overhead_effect + offensive_effect;

    /* clear just-activated flags even on zero-drain paths so they don't leak
       into next tick. */
    p->prayer_just_activated = 0;
    p->offensive_prayer_just_activated = 0;

    /* pp already at/below 0 entering this tick — force prayers off and skip
       drain math. covers: external drain (smite), or activation attempted
       at pp=0 (the pretick apply_*_action helpers don't gate on pp, so the
       enum may have been set this tick before we arrived). without this,
       the prayer enum can stay active indefinitely at pp=0. */
    if (p->current_prayer <= 0) {
        p->current_prayer = 0;
        p->prayer_drain_counter = 0;
        p->prayer = PRAYER_NONE;
        p->offensive_prayer = OFFENSIVE_PRAYER_NONE;
        return;
    }
    if (total <= 0) return;

    int drain_resistance = 60 + prayer_bonus * 2;
    p->prayer_drain_counter += total;
    while (p->prayer_drain_counter > drain_resistance) {
        p->current_prayer--;
        p->prayer_drain_counter -= drain_resistance;
        if (p->current_prayer <= 0) {
            p->current_prayer = 0;
            p->prayer_drain_counter = 0;
            p->prayer = PRAYER_NONE;
            p->offensive_prayer = OFFENSIVE_PRAYER_NONE;
            break;
        }
    }
}
/* shared loadout stat computation                                           */
/*                                                                           */
/* ENCOUNTERS: do NOT manually compute attack bonuses, max hits, or          */
/* effective levels. call encounter_compute_loadout_stats() with a loadout   */
/* array and it derives everything from ITEM_DATABASE automatically.         */
/*                                                                           */
/* available structs/functions:                                               */
/*   EncounterLoadoutStats — computed combat stats for one gear loadout      */
/*   OffensivePrayer       — prayer enum (NONE, PIETY, RIGOUR, AUGURY, low-tiers) */
/*   encounter_compute_loadout_stats() — derive stats from loadout + prayer  */
/** combat stats derived from a gear loadout + prayer + style.
    computed once at reset, read during combat.
    prayer multipliers and style_bonus are stored for dynamic recomputation
    when stats change (brew drain, potion boost). */
typedef struct {
    int attack_bonus;     /* primary attack bonus for the style */
    int strength_bonus;   /* ranged_strength, magic_damage %, or melee_strength */
    int eff_level;        /* effective attack level (floor(base*prayer) + style + 8) */
    int max_hit;          /* base max hit (before tbow/set bonuses) */
    int attack_speed;     /* ticks between attacks (includes stance speed mod) */
    int attack_range;     /* max chebyshev distance (includes stance range mod) */
    AttackStyle style;
    FightStyle fight_style;  /* stance picked for this loadout — drives stance bonuses + speed/range mods */
    /* defence bonuses from gear */
    int def_stab, def_slash, def_crush, def_magic, def_ranged;
    /* stored for dynamic max hit recomputation after brew drain / potion boost */
    float att_prayer_mult;
    float str_prayer_mult;
    int spell_base_damage;
} EncounterLoadoutStats;

/** E12 magic accuracy effective level from dps-calc:
    floor(magic * prayer) + 2 for Accurate powered staff stance, then +9.
    Longrange is selectable for powered staffs but contributes no attack level. */
static inline int encounter_magic_effective_attack_level(
    int magic_level, float prayer_mult, FightStyle fight_style
) {
    return osrs_magic_effective_attack_level(magic_level, prayer_mult, fight_style);
}

/** Copy a loadout and suppress the shield slot when the weapon is two-handed.
    E3 requires the shared equipment interpretation to ignore both shield stats
    and shield effect bits for two-handed weapons. */
static inline void encounter_effective_loadout_for_equipment(
    const uint8_t loadout[NUM_GEAR_SLOTS],
    uint8_t out[NUM_GEAR_SLOTS]
) {
    memcpy(out, loadout, NUM_GEAR_SLOTS);
    out[GEAR_SLOT_SHIELD] = osrs_suppress_shield_for_two_handed_weapon(
        out[GEAR_SLOT_WEAPON], out[GEAR_SLOT_SHIELD]);
}

/** Derive item effects from the same two-handed-compatible loadout used for stats. */
static inline void encounter_derive_loadout_effect_profile(
    const uint8_t loadout[NUM_GEAR_SLOTS],
    OsrsEquipmentEffectProfile* out
) {
    uint8_t effective_loadout[NUM_GEAR_SLOTS];
    encounter_effective_loadout_for_equipment(loadout, effective_loadout);
    osrs_derive_equipment_effect_profile(effective_loadout, out);
}

/** offensive prayer multipliers for effective level computation.
    single source of truth: the multipliers used in encounter_compute_loadout_stats()
    and encounter_update_loadout_level(). also used by PvP combat math (via
    osrs_pvp_combat.h) so all combat paths agree on prayer effects.
    ref: osrs wiki prayer table. */
static inline void encounter_offensive_prayer_mults(
    OffensivePrayer op, float* att_out, float* str_out
) {
    float att = 1.0f, str = 1.0f;
    switch (op) {
        case OFFENSIVE_PRAYER_PIETY:       att = 1.20f; str = 1.23f; break;
        case OFFENSIVE_PRAYER_RIGOUR:      att = 1.20f; str = 1.23f; break;
        case OFFENSIVE_PRAYER_AUGURY:      att = 1.25f; str = 1.00f; break;
        case OFFENSIVE_PRAYER_MELEE_LOW:   att = 1.15f; str = 1.15f; break;
        case OFFENSIVE_PRAYER_RANGED_LOW:  att = 1.15f; str = 1.15f; break;
        case OFFENSIVE_PRAYER_MAGIC_LOW:   att = 1.15f; str = 1.00f; break;
        default: break;
    }
    *att_out = att;
    *str_out = str;
}

/** augury adds +4% magic damage on top of its accuracy mult (PvP parity). */
static inline float encounter_offensive_magic_dmg_mult(OffensivePrayer op) {
    return osrs_offensive_magic_dmg_mult(op);
}

/** derive all combat stats from a loadout array + prayer + fight stance.
    sums equipment bonuses from ITEM_DATABASE, applies prayer multiplier,
    computes effective level and max hit. attack_speed and attack_range are
    also adjusted for the stance (rapid -1 tick, longrange +2 tiles).

    @param loadout          gear array indexed by GEAR_SLOT_* (ITEM_NONE=255 for empty)
    @param style            ATTACK_STYLE_MAGIC, ATTACK_STYLE_RANGED, or ATTACK_STYLE_MELEE
    @param offensive_prayer current offensive prayer (piety/rigour/augury/none/low tiers)
    @param base_level       base combat level (usually 99)
    @param fight_style      stance — drives attack/str/def bonuses, attack speed, range
    @param spell_base_damage 0 for ranged/melee, 30 for ice/blood barrage
    @param out              output struct to fill. prayer multipliers are stored so
                            encounter_update_loadout_level() can recompute eff/max without
                            needing the prayer arg again (callers must re-call update
                            whenever offensive prayer changes). */
static inline void encounter_compute_loadout_stats(
    const uint8_t loadout[NUM_GEAR_SLOTS],
    AttackStyle style,
    OffensivePrayer offensive_prayer,
    int base_level,
    FightStyle fight_style,
    int spell_base_damage,
    EncounterLoadoutStats* out
) {
    memset(out, 0, sizeof(*out));
    out->style = style;
    out->fight_style = fight_style;

    uint8_t effective_loadout[NUM_GEAR_SLOTS];
    encounter_effective_loadout_for_equipment(loadout, effective_loadout);

    /* sum equipment bonuses using shared function */
    EquipmentBonuses eb;
    osrs_sum_equipment_bonuses(effective_loadout, &eb);

    out->def_stab = eb.defence_stab;
    out->def_slash = eb.defence_slash;
    out->def_crush = eb.defence_crush;
    out->def_magic = eb.defence_magic;
    out->def_ranged = eb.defence_ranged;
    /* apply stance modifiers to weapon base speed/range. equipment.json stores
       the base (accurate/longrange speed, non-longrange range). rapid and
       longrange shift them. */
    out->attack_speed = eb.attack_speed + osrs_stance_speed_mod(fight_style);
    out->attack_range = eb.attack_range + osrs_stance_range_mod(fight_style);

    /* primary attack bonus based on style */
    if (style == ATTACK_STYLE_MAGIC) {
        out->attack_bonus = eb.attack_magic;
    } else if (style == ATTACK_STYLE_RANGED) {
        out->attack_bonus = eb.attack_ranged;
    } else {
        /* melee: best of stab/slash/crush */
        out->attack_bonus = eb.attack_stab;
        if (eb.attack_slash > out->attack_bonus) out->attack_bonus = eb.attack_slash;
        if (eb.attack_crush > out->attack_bonus) out->attack_bonus = eb.attack_crush;
    }

    /* prayer multipliers — single source of truth in encounter_offensive_prayer_mults(). */
    float att_prayer_mult, str_prayer_mult;
    encounter_offensive_prayer_mults(offensive_prayer, &att_prayer_mult, &str_prayer_mult);

    /* store for dynamic recomputation after brew drain / potion boost / prayer toggle */
    out->att_prayer_mult = att_prayer_mult;
    out->str_prayer_mult = str_prayer_mult;
    out->spell_base_damage = spell_base_damage;

    int att_stance_bonus = osrs_stance_att_bonus(fight_style, style);
    int str_stance_bonus = osrs_stance_str_bonus(fight_style);

    if (style == ATTACK_STYLE_MAGIC) {
        out->eff_level = encounter_magic_effective_attack_level(
            base_level, att_prayer_mult, fight_style);
    } else {
        out->eff_level = (int)(base_level * att_prayer_mult) + att_stance_bonus + 8;
    }

    /* effective strength level (for max hit): floor(base * str_prayer_mult) + str_stance_bonus + 8.
       str_stance_bonus is non-zero only for melee (aggressive/controlled). */
    int eff_str_level = (int)(base_level * str_prayer_mult) + str_stance_bonus + 8;

    /* augury magic damage multiplier: +4% (matches PvP calculate_max_hit). */
    float magic_dmg_prayer_mult = encounter_offensive_magic_dmg_mult(offensive_prayer);

    /* max hit and strength bonus depend on combat style */
    if (style == ATTACK_STYLE_RANGED) {
        out->strength_bonus = eb.ranged_strength;
        out->max_hit = (int)(0.5 + eff_str_level * (eb.ranged_strength + 64) / 640.0);
    } else if (style == ATTACK_STYLE_MAGIC) {
        out->strength_bonus = eb.magic_damage;
        out->max_hit = (int)(spell_base_damage * (1.0 + eb.magic_damage / 100.0) * magic_dmg_prayer_mult);
    } else {
        out->strength_bonus = eb.melee_strength;
        out->max_hit = (int)(0.5 + eff_str_level * (eb.melee_strength + 64) / 640.0);
    }
}
/* dynamic max hit recomputation (after brew drain / potion boost)            */
/*                                                                           */
/* ENCOUNTERS: call encounter_update_loadout_level() whenever the player's   */
/* current combat level changes (brew drain, restore, bastion boost).        */
/* this recomputes eff_level and max_hit using the stored prayer multiplier  */
/* and strength bonus from the initial encounter_compute_loadout_stats().    */
/** recompute eff_level and max_hit for a loadout using a (possibly drained/boosted)
    current combat level AND current offensive prayer. call whenever either changes:
      - offensive prayer toggle (pretick action)
      - brew drain / super restore / bastion boost (consumable effects)
    current_att_level: the player's current attack/ranged/magic level (for accuracy).
    current_str_level: the player's current strength/ranged/magic level (for max hit).
    for ranged: both are current_ranged. for melee: att=current_attack, str=current_strength.
    for magic: max hit doesn't depend on level (spell base damage), but eff_level does.
    offensive_prayer is the current Player.offensive_prayer — mults are rewritten from it. */
static inline void encounter_update_loadout_level(
    EncounterLoadoutStats* ls, OffensivePrayer offensive_prayer,
    int current_att_level, int current_str_level
) {
    float att_prayer_mult, str_prayer_mult;
    encounter_offensive_prayer_mults(offensive_prayer, &att_prayer_mult, &str_prayer_mult);
    ls->att_prayer_mult = att_prayer_mult;
    ls->str_prayer_mult = str_prayer_mult;

    int att_stance_bonus = osrs_stance_att_bonus(ls->fight_style, ls->style);
    int str_stance_bonus = osrs_stance_str_bonus(ls->fight_style);
    if (ls->style == ATTACK_STYLE_MAGIC) {
        ls->eff_level = encounter_magic_effective_attack_level(
            current_att_level, att_prayer_mult, ls->fight_style);
        float magic_dmg_mult = encounter_offensive_magic_dmg_mult(offensive_prayer);
        ls->max_hit = (int)(ls->spell_base_damage * (1.0 + ls->strength_bonus / 100.0) * magic_dmg_mult);
    } else {
        ls->eff_level = (int)(current_att_level * att_prayer_mult) + att_stance_bonus + 8;
        int eff_str = (int)(current_str_level * str_prayer_mult) + str_stance_bonus + 8;
        ls->max_hit = (int)(0.5 + eff_str * (ls->strength_bonus + 64) / 640.0);
    }
}

static inline void encounter_compute_player_equipped_stats(
    Player* p,
    AttackStyle style,
    FightStyle fight_style,
    int spell_base_damage,
    EncounterLoadoutStats* out
) {
    int current_att = p->current_attack;
    int current_str = p->current_strength;
    if (style == ATTACK_STYLE_RANGED) {
        current_att = p->current_ranged;
        current_str = p->current_ranged;
    } else if (style == ATTACK_STYLE_MAGIC) {
        current_att = p->current_magic;
        current_str = p->current_magic;
    }
    encounter_compute_loadout_stats(
        p->equipped,
        style,
        p->offensive_prayer,
        current_att,
        fight_style,
        spell_base_damage,
        out);
    encounter_update_loadout_level(out, p->offensive_prayer, current_att, current_str);
}
/* shared potion stat effects (brew drain, restore, bastion boost)           */
/*                                                                           */
/* ENCOUNTERS: call these when the player drinks a potion. they modify the   */
/* player's current combat levels and recompute max hit for affected loadouts.*/
/* these implement the real OSRS formulas for stat modification.             */
/*                                                                           */
/* sara brew:     heals HP, boosts def, drains att/str/ranged/magic          */
/* super restore: restores all drained stats toward base (caps at base)      */
/* bastion:       boosts ranged above base, boosts def                       */
static inline void encounter_init_maxed_player_combat_stats(
    Player* p,
    int prayer_level
) {
    p->base_attack = MAXED_BASE_ATTACK;
    p->base_strength = MAXED_BASE_STRENGTH;
    p->base_defence = MAXED_BASE_DEFENCE;
    p->base_ranged = MAXED_BASE_RANGED;
    p->base_magic = MAXED_BASE_MAGIC;
    p->base_prayer = prayer_level;
    p->base_hitpoints = MAXED_BASE_HITPOINTS;

    p->current_attack = p->base_attack;
    p->current_strength = p->base_strength;
    p->current_defence = p->base_defence;
    p->current_ranged = p->base_ranged;
    p->current_magic = p->base_magic;
    p->current_prayer = p->base_prayer;
    p->current_hitpoints = p->base_hitpoints;
}

static inline void encounter_apply_saturated_heart_boost(Player* p) {
    int boost = osrs_saturated_heart_magic_boost(p->base_magic);
    int cap = p->base_magic + boost;
    p->current_magic += boost;
    if (p->current_magic > cap) p->current_magic = cap;
    p->saturated_heart_active_ticks = 500;
}

static inline int encounter_tick_saturated_heart(Player* p) {
    if (p->saturated_heart_active_ticks <= 0) return 0;
    p->saturated_heart_active_ticks -= 1;
    if (p->saturated_heart_active_ticks > 0) return 0;
    if (p->current_magic <= p->base_magic) return 0;
    p->current_magic = p->base_magic;
    return 1;
}

static inline int encounter_saturated_heart_protects_magic(const Player* p) {
    if (p->saturated_heart_active_ticks <= 0) return 0;
    int cap = p->base_magic + osrs_saturated_heart_magic_boost(p->base_magic);
    return p->current_magic <= cap && p->current_magic > p->base_magic;
}

static inline int encounter_decay_stat_toward_base(int* current, int base) {
    if (*current > base) {
        *current -= 1;
        return 1;
    }
    if (*current < base) {
        *current += 1;
        return 1;
    }
    return 0;
}

static inline int encounter_decay_player_combat_stats_toward_base(Player* p) {
    int changed = 0;
    changed |= encounter_decay_stat_toward_base(&p->current_attack, p->base_attack);
    changed |= encounter_decay_stat_toward_base(&p->current_strength, p->base_strength);
    changed |= encounter_decay_stat_toward_base(&p->current_defence, p->base_defence);
    changed |= encounter_decay_stat_toward_base(&p->current_ranged, p->base_ranged);
    if (!encounter_saturated_heart_protects_magic(p))
        changed |= encounter_decay_stat_toward_base(&p->current_magic, p->base_magic);
    return changed;
}

#define ENCOUNTER_STAT_DRIFT_TICKS 100
#define ENCOUNTER_DIVINE_POTION_TICKS 500
#define ENCOUNTER_STAT_DRIFT_UNPINNED (-1)

typedef struct {
    int attack_floor;
    int strength_floor;
    int defence_floor;
    int ranged_floor;
    int magic_floor;
} EncounterStatDriftPins;

/** no active divine potion floor for the 100-tick stat drift helper. */
static inline EncounterStatDriftPins encounter_stat_drift_no_pins(void) {
    return (EncounterStatDriftPins){
        .attack_floor = ENCOUNTER_STAT_DRIFT_UNPINNED,
        .strength_floor = ENCOUNTER_STAT_DRIFT_UNPINNED,
        .defence_floor = ENCOUNTER_STAT_DRIFT_UNPINNED,
        .ranged_floor = ENCOUNTER_STAT_DRIFT_UNPINNED,
        .magic_floor = ENCOUNTER_STAT_DRIFT_UNPINNED,
    };
}

/** merge active divine floors so overlapping potion timers compose by maximum. */
static inline int encounter_merge_stat_drift_floor(int a, int b) {
    if (a == ENCOUNTER_STAT_DRIFT_UNPINNED) return b;
    if (b == ENCOUNTER_STAT_DRIFT_UNPINNED) return a;
    return a > b ? a : b;
}

/** merge two sets of active divine floors by per-stat maximum. */
static inline EncounterStatDriftPins encounter_merge_stat_drift_pins(
    EncounterStatDriftPins a,
    EncounterStatDriftPins b
) {
    return (EncounterStatDriftPins){
        .attack_floor = encounter_merge_stat_drift_floor(a.attack_floor, b.attack_floor),
        .strength_floor = encounter_merge_stat_drift_floor(a.strength_floor, b.strength_floor),
        .defence_floor = encounter_merge_stat_drift_floor(a.defence_floor, b.defence_floor),
        .ranged_floor = encounter_merge_stat_drift_floor(a.ranged_floor, b.ranged_floor),
        .magic_floor = encounter_merge_stat_drift_floor(a.magic_floor, b.magic_floor),
    };
}

/** divine super combat floor values for Attack, Strength, and Defence. */
static inline EncounterStatDriftPins encounter_divine_super_combat_pins(const Player* p) {
    EncounterStatDriftPins pins = encounter_stat_drift_no_pins();
    pins.attack_floor = p->base_attack + osrs_super_combat_boost_amount(p->base_attack);
    pins.strength_floor =
        p->base_strength + osrs_super_combat_boost_amount(p->base_strength);
    pins.defence_floor = p->base_defence + osrs_super_combat_boost_amount(p->base_defence);
    return pins;
}

/** divine ranging floor value for Ranged. */
static inline EncounterStatDriftPins encounter_divine_ranging_pins(const Player* p) {
    EncounterStatDriftPins pins = encounter_stat_drift_no_pins();
    pins.ranged_floor = p->base_ranged + osrs_ranging_boost_amount(p->base_ranged);
    return pins;
}

/** apply one active divine floor. Floors below or equal to base are ignored. */
static inline int encounter_enforce_stat_drift_floor(int* current, int base, int floor) {
    if (floor <= base) return 0;
    if (*current >= floor) return 0;
    *current = floor;
    return 1;
}

/** apply every active divine floor to the player's combat levels. */
static inline int encounter_enforce_stat_drift_pins(Player* p, EncounterStatDriftPins pins) {
    int changed = 0;
    changed |= encounter_enforce_stat_drift_floor(
        &p->current_attack, p->base_attack, pins.attack_floor);
    changed |= encounter_enforce_stat_drift_floor(
        &p->current_strength, p->base_strength, pins.strength_floor);
    changed |= encounter_enforce_stat_drift_floor(
        &p->current_defence, p->base_defence, pins.defence_floor);
    changed |= encounter_enforce_stat_drift_floor(
        &p->current_ranged, p->base_ranged, pins.ranged_floor);
    changed |= encounter_enforce_stat_drift_floor(
        &p->current_magic, p->base_magic, pins.magic_floor);
    return changed;
}

/** move one stat one level toward base without crossing an active divine floor. */
static inline int encounter_drift_stat_toward_base_with_floor(
    int* current,
    int base,
    int floor
) {
    if (floor > base && *current <= floor) {
        if (*current == floor) return 0;
        *current = floor;
        return 1;
    }
    if (*current > base) {
        *current -= 1;
        if (floor > base && *current < floor) *current = floor;
        return 1;
    }
    if (*current < base) {
        *current += 1;
        return 1;
    }
    return 0;
}

/** E13: tick the OSRS 100-tick natural stat drift cycle once.
    Every completed cycle moves Attack, Strength, Defence, Ranged, Magic, and
    Hitpoints one level toward base from either direction. Active divine pins
    hold their potion stats at the boosted floor until the caller expires the
    potion timer. */
static inline int encounter_tick_stat_drift(
    Player* p,
    int* stat_drift_timer,
    EncounterStatDriftPins pins
) {
    assert(p && stat_drift_timer);
    assert(*stat_drift_timer >= 0 && *stat_drift_timer < ENCOUNTER_STAT_DRIFT_TICKS);

    int changed = encounter_enforce_stat_drift_pins(p, pins);
    *stat_drift_timer += 1;
    if (*stat_drift_timer < ENCOUNTER_STAT_DRIFT_TICKS) return changed;
    *stat_drift_timer = 0;

    changed |= encounter_drift_stat_toward_base_with_floor(
        &p->current_attack, p->base_attack, pins.attack_floor);
    changed |= encounter_drift_stat_toward_base_with_floor(
        &p->current_strength, p->base_strength, pins.strength_floor);
    changed |= encounter_drift_stat_toward_base_with_floor(
        &p->current_defence, p->base_defence, pins.defence_floor);
    changed |= encounter_drift_stat_toward_base_with_floor(
        &p->current_ranged, p->base_ranged, pins.ranged_floor);
    changed |= encounter_drift_stat_toward_base_with_floor(
        &p->current_magic, p->base_magic, pins.magic_floor);
    if (p->current_hitpoints > 0)
        changed |= encounter_decay_stat_toward_base(&p->current_hitpoints, p->base_hitpoints);
    return changed;
}

typedef enum {
    ENCOUNTER_CONSUMABLE_STAT_EFFECT_NONE = 0,
    ENCOUNTER_CONSUMABLE_STAT_EFFECT_BREW_DRAIN,
    ENCOUNTER_CONSUMABLE_STAT_EFFECT_RESTORE,
    ENCOUNTER_CONSUMABLE_STAT_EFFECT_SANFEW,
} EncounterConsumableStatEffect;

/** Apply the shared brew HP, dose, timer, and food-event state. */
static inline void encounter_apply_brew_heal_and_timer(Player* p, int brew_heal) {
    p->current_hitpoints += brew_heal;
    if (p->current_hitpoints > p->base_hitpoints + brew_heal)
        p->current_hitpoints = p->base_hitpoints + brew_heal;
    p->brew_doses--;
    p->potion_timer = 3;
    p->ate_food_this_tick = 1;
}

/** Add a prayer restore amount before the caller applies any local hooks. */
static inline void encounter_add_prayer_restore(Player* p, int restore_amount) {
    p->current_prayer += restore_amount;
}

/** Cap current prayer at the player's base prayer after a restore drink. */
static inline void encounter_cap_prayer_restore(Player* p) {
    if (p->current_prayer > p->base_prayer)
        p->current_prayer = p->base_prayer;
}

/** Decrement a potion dose counter and arm the shared 3-tick potion timer. */
static inline void encounter_finish_potion_dose(Player* p, int* doses) {
    (*doses)--;
    p->potion_timer = 3;
}

/** sara brew stat drain. call AFTER healing HP (which is encounter-specific).
    applies osrs_brew_effect: drains att/str/ranged/magic by floor(current/10)+2
    (CURRENT levels, diminishing on repeat sips), boosts defence by
    floor(base/5)+2 (BASE level), capped at base + boost. floors drained stats
    at 0. ref: OSRS wiki Saradomin brew. */
static inline void encounter_brew_drain_stats(Player* p) {
    BrewResult brew = osrs_brew_effect(p->base_hitpoints, p->base_defence,
                                       p->current_attack, p->current_strength,
                                       p->current_ranged, p->current_magic);

    p->current_attack -= brew.att_drain;
    if (p->current_attack < 0) p->current_attack = 0;
    p->current_strength -= brew.str_drain;
    if (p->current_strength < 0) p->current_strength = 0;
    p->current_ranged -= brew.range_drain;
    if (p->current_ranged < 0) p->current_ranged = 0;
    p->current_magic -= brew.magic_drain;
    if (p->current_magic < 0) p->current_magic = 0;

    p->current_defence += brew.def_boost;
    int def_cap = p->base_defence + brew.def_boost;
    if (p->current_defence > def_cap) p->current_defence = def_cap;
}

/** restore every combat stat toward base by amount(base), capped at base.
    the shared shape under super restore and sanfew serum. */
static inline void encounter_apply_stat_restore(Player* p, int (*amount)(int)) {
    int restore = amount(p->base_attack);
    p->current_attack += restore;
    if (p->current_attack > p->base_attack) p->current_attack = p->base_attack;
    restore = amount(p->base_strength);
    p->current_strength += restore;
    if (p->current_strength > p->base_strength) p->current_strength = p->base_strength;
    restore = amount(p->base_defence);
    p->current_defence += restore;
    if (p->current_defence > p->base_defence) p->current_defence = p->base_defence;
    restore = amount(p->base_ranged);
    p->current_ranged += restore;
    if (p->current_ranged > p->base_ranged) p->current_ranged = p->base_ranged;
    restore = amount(p->base_magic);
    p->current_magic += restore;
    if (p->current_magic > p->base_magic) p->current_magic = p->base_magic;
}

/** super restore stat recovery: floor(base * 0.25) + 8 per stat, capped at base.
    ref: OSRS wiki Super restore. */
static inline void encounter_restore_stats(Player* p) {
    encounter_apply_stat_restore(p, osrs_super_restore_amount);
}

/** sanfew serum stat recovery: floor(base * 0.30) + 4 per stat, capped at base
    (out-restores super restore above level 80). the drink also cures venom —
    the caller owns venom state. ref: OSRS wiki Sanfew serum. */
static inline void encounter_sanfew_restore_stats(Player* p) {
    encounter_apply_stat_restore(p, osrs_sanfew_restore_amount);
}

/** Apply the shared stat side of a consumable effect. */
static inline void encounter_apply_consumable_stat_effect(
    Player* p,
    EncounterConsumableStatEffect effect
) {
    switch (effect) {
        case ENCOUNTER_CONSUMABLE_STAT_EFFECT_NONE:
            return;
        case ENCOUNTER_CONSUMABLE_STAT_EFFECT_BREW_DRAIN:
            encounter_brew_drain_stats(p);
            return;
        case ENCOUNTER_CONSUMABLE_STAT_EFFECT_RESTORE:
            encounter_restore_stats(p);
            return;
        case ENCOUNTER_CONSUMABLE_STAT_EFFECT_SANFEW:
            encounter_sanfew_restore_stats(p);
            return;
    }
    abort();
}

/** bastion potion boost. boosts ranged by floor(base * 0.10) + 4. can exceed base.
    also boosts defence by floor(base * 0.15) + 5. can exceed base.
    ref: OSRS wiki Bastion potion. */
static inline void encounter_bastion_boost(Player* p) {
    int rng_boost = osrs_ranging_boost_amount(p->base_ranged);
    int def_boost = osrs_super_combat_boost_amount(p->base_defence);
    p->current_ranged += rng_boost;
    int rng_cap = p->base_ranged + rng_boost;
    if (p->current_ranged > rng_cap) p->current_ranged = rng_cap;
    p->current_defence += def_boost;
    int def_cap = p->base_defence + def_boost;
    if (p->current_defence > def_cap) p->current_defence = def_cap;
}

/** super combat boost: att/str/def each +floor(base * 0.15) + 5, capped at
    base + boost. ref: OSRS wiki Super combat potion. */
static inline void encounter_super_combat_boost(Player* p) {
    int boost = osrs_super_combat_boost_amount(p->base_attack);
    p->current_attack += boost;
    if (p->current_attack > p->base_attack + boost) p->current_attack = p->base_attack + boost;
    boost = osrs_super_combat_boost_amount(p->base_strength);
    p->current_strength += boost;
    if (p->current_strength > p->base_strength + boost) p->current_strength = p->base_strength + boost;
    boost = osrs_super_combat_boost_amount(p->base_defence);
    p->current_defence += boost;
    if (p->current_defence > p->base_defence + boost) p->current_defence = p->base_defence + boost;
}

/** ranging potion boost: ranged +floor(base * 0.10) + 4, capped at base + boost.
    ref: OSRS wiki Ranging potion. */
static inline void encounter_ranging_boost(Player* p) {
    int boost = osrs_ranging_boost_amount(p->base_ranged);
    p->current_ranged += boost;
    if (p->current_ranged > p->base_ranged + boost) p->current_ranged = p->base_ranged + boost;
}

/** recompute max hit for all loadouts after a stat change or prayer change.
    encounters should call this after brew_drain_stats, restore_stats, bastion_boost,
    or when Player.offensive_prayer toggles. ranged loadouts use current_ranged, magic
    uses current_magic, melee uses current_attack/current_strength. prayer multipliers
    are rewritten from p->offensive_prayer. */
static inline void encounter_recompute_loadout_max_hits(
    EncounterLoadoutStats* loadouts, int num_loadouts, Player* p
) {
    for (int i = 0; i < num_loadouts; i++) {
        EncounterLoadoutStats* ls = &loadouts[i];
        if (ls->style == ATTACK_STYLE_RANGED) {
            encounter_update_loadout_level(ls, p->offensive_prayer, p->current_ranged, p->current_ranged);
        } else if (ls->style == ATTACK_STYLE_MAGIC) {
            encounter_update_loadout_level(ls, p->offensive_prayer, p->current_magic, p->current_magic);
        } else {
            encounter_update_loadout_level(ls, p->offensive_prayer, p->current_attack, p->current_strength);
        }
    }
}
/* shared special attack energy                                              */
/*                                                                           */
/* ENCOUNTERS: call encounter_tick_spec_regen() every game tick. call         */
/* encounter_use_spec() when the player activates a special attack.          */
/* OSRS: energy 0-100, starts at 100, regens +10 every 50 ticks (30s).      */
/* lightbearer halves regen interval to 25 ticks.                            */
/** tick special attack energy regeneration from current equipped gear. */
static inline void encounter_tick_spec_regen(Player* p) {
    osrs_tick_special_regen(p);
}

/** attempt to use special attack energy. returns 1 if successful (enough energy),
    0 if not enough energy. drains on success. */
static inline int encounter_use_spec(Player* p, int cost) {
    if (p->special_energy < cost) return 0;
    p->special_energy -= cost;
    return 1;
}


/** apply a full static loadout to player equipment and set gear state.
    used by Zulrah, Inferno, and future boss encounters with fixed loadouts. */
static inline void encounter_apply_loadout(
    Player* p, const uint8_t loadout[NUM_GEAR_SLOTS], GearSet gear_set
) {
    encounter_effective_loadout_for_equipment(loadout, p->equipped);
    p->current_gear = gear_set;
    p->visible_gear = gear_set;
    osrs_refresh_player_equipment(p);
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

/**
 * Clear ammo-slot switch candidates after loadout inventory population.
 */
static inline void encounter_clear_ammo_inventory_slot(Player* p) {
    for (int i = 0; i < MAX_ITEMS_PER_SLOT; i++)
        p->inventory[GEAR_SLOT_AMMO][i] = ITEM_NONE;
    p->num_items_in_slot[GEAR_SLOT_AMMO] = 0;
}


/** translate movement: convert absolute tile to 8-directional walk action.
    writes to actions[head_move]. head_move < 0 = skip. */
static inline void encounter_translate_movement(HumanInput* hi, int* actions,
                                                 int head_move,
                                                 void* (*get_entity)(void*, int),
                                                 void* state) {
    if (hi->pending_move_x < 0 || hi->pending_move_y < 0 || head_move < 0) return;
    Player* player = (Player*)get_entity(state, 0);
    if (!player) return;
    int dx = hi->pending_move_x - player->x;
    int dy = hi->pending_move_y - player->y;
    int sx = (dx > 0) ? 1 : (dx < 0) ? -1 : 0;
    int sy = (dy > 0) ? 1 : (dy < 0) ? -1 : 0;
    static const int DX8[9] = { 0, 0, 1, 1, 1, 0, -1, -1, -1 };
    static const int DY8[9] = { 0, 1, 1, 0, -1, -1, -1, 0, 1 };
    for (int m = 1; m < 9; m++) {
        if (DX8[m] == sx && DY8[m] == sy) {
            actions[head_move] = m;
            break;
        }
    }
}

/** translate overhead prayer: pending_prayer stores the new ENCOUNTER_OVERHEAD_*
    value directly (set by GUI click handlers). writes to actions[head_prayer].
    head_prayer < 0 = skip. */
static inline void encounter_translate_prayer(HumanInput* hi, int* actions, int head_prayer) {
    if (hi->pending_prayer < 0 || head_prayer < 0) return;
    actions[head_prayer] = hi->pending_prayer;
}

/** translate offensive prayer: pending_offensive_prayer stores the new
    ENCOUNTER_OFFENSIVE_* value directly. writes to actions[head_offensive].
    head_offensive < 0 = skip (encounter doesn't expose offensive as an action). */
static inline void encounter_translate_offensive_prayer(
    HumanInput* hi, int* actions, int head_offensive
) {
    if (hi->pending_offensive_prayer < 0 || head_offensive < 0) return;
    actions[head_offensive] = hi->pending_offensive_prayer;
}

/** translate NPC target: 0=none, 1+=NPC index.
    writes to actions[head_target]. head_target < 0 = skip. */
static inline void encounter_translate_target(HumanInput* hi, int* actions, int head_target) {
    if (hi->pending_target_idx < 0 || head_target < 0) return;
    actions[head_target] = hi->pending_target_idx + 1;
}

/**
 * Find the observed NPC target slot for a raw encounter NPC slot.
 */
static inline int encounter_find_observed_target_slot(
    const int* current_obs_slots,
    int observed_slot_count,
    int raw_npc_slot
) {
    for (int slot = 0; slot < observed_slot_count; slot++)
        if (current_obs_slots[slot] == raw_npc_slot) return slot;
    return -1;
}

/** translate human attack-or-move click for encounters with a merged combat head
    (ATTACK_*, MOVE_UNDER/ADJACENT/DIAGONAL, MOVE_FARCAST_2..7).
    pending_attack takes precedence; otherwise the move click is resolved against
    the target's tile to pick UNDER/ADJACENT/DIAGONAL or a farcast at the
    chebyshev distance, clamped to [2, 7]. */
static inline void encounter_translate_attack_or_move(
    HumanInput* hi,
    int* actions,
    int head_combat,
    const Player* target
) {
    if (head_combat < 0 || !target) return;

    if (hi->pending_attack) {
        if (hi->pending_spell == ATTACK_ICE) actions[head_combat] = ATTACK_ICE;
        else if (hi->pending_spell == ATTACK_BLOOD) actions[head_combat] = ATTACK_BLOOD;
        else actions[head_combat] = ATTACK_ATK;
        return;
    }

    if (hi->pending_move_x < 0 || hi->pending_move_y < 0) return;

    int dx = hi->pending_move_x - target->x;
    int dy = hi->pending_move_y - target->y;
    int adx = dx < 0 ? -dx : dx;
    int ady = dy < 0 ? -dy : dy;
    int dist = adx > ady ? adx : ady;
    if (dist == 0) {
        actions[head_combat] = MOVE_UNDER;
    } else if (dist == 1) {
        actions[head_combat] = (dx == 0 || dy == 0) ? MOVE_ADJACENT : MOVE_DIAGONAL;
    } else {
        int fc = dist < 2 ? 2 : (dist > 7 ? 7 : dist);
        actions[head_combat] = MOVE_FARCAST_2 + (fc - 2);
    }
}


/**
 * Scenario lab optional integer.
 */
typedef enum {
    ENCOUNTER_LAB_OPTIONAL_INT_UNSET = 0,
    ENCOUNTER_LAB_OPTIONAL_INT_SET,
} EncounterLabOptionalIntKind;

typedef struct {
    EncounterLabOptionalIntKind kind;
    int value;
} EncounterLabOptionalInt;

static inline EncounterLabOptionalInt encounter_lab_optional_int_unset(void) {
    return (EncounterLabOptionalInt){ .kind = ENCOUNTER_LAB_OPTIONAL_INT_UNSET };
}

static inline EncounterLabOptionalInt encounter_lab_optional_int_set(int value) {
    return (EncounterLabOptionalInt){
        .kind = ENCOUNTER_LAB_OPTIONAL_INT_SET,
        .value = value,
    };
}

/**
 * Growable scenario lab string buffer for JSON dump assembly.
 */
typedef struct {
    char* data;
    size_t len;
    size_t cap;
    const char* owner_label;
} EncounterLabString;

static inline void encounter_lab_abort(const char* owner_label, const char* fmt, ...) {
    va_list args;
    fprintf(stderr, "%s: ", owner_label);
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
    fprintf(stderr, "\n");
    abort();
}

static inline void encounter_lab_string_init(
    EncounterLabString* out, const char* owner_label
) {
    out->len = 0;
    out->cap = 4096;
    out->owner_label = owner_label;
    out->data = (char*)malloc(out->cap);
    if (!out->data) encounter_lab_abort(owner_label, "out of memory");
    out->data[0] = '\0';
}

static inline void encounter_lab_string_reserve(EncounterLabString* out, size_t need) {
    if (need <= out->cap) return;
    size_t next = out->cap;
    while (next < need) {
        if (next > SIZE_MAX / 2)
            encounter_lab_abort(out->owner_label, "json output too large");
        next *= 2;
    }
    char* data = (char*)realloc(out->data, next);
    if (!data) encounter_lab_abort(out->owner_label, "out of memory");
    out->data = data;
    out->cap = next;
}

static inline void encounter_lab_string_append(EncounterLabString* out, const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    va_list copy;
    va_copy(copy, args);
    int needed = vsnprintf(NULL, 0, fmt, copy);
    va_end(copy);
    if (needed < 0) encounter_lab_abort(out->owner_label, "json formatting failed");
    encounter_lab_string_reserve(out, out->len + (size_t)needed + 1);
    int written = vsnprintf(out->data + out->len, out->cap - out->len, fmt, args);
    va_end(args);
    if (written != needed)
        encounter_lab_abort(out->owner_label, "json formatting length mismatch");
    out->len += (size_t)written;
}

static inline void encounter_lab_string_free(EncounterLabString* out) {
    if (!out) return;
    free(out->data);
    out->data = NULL;
    out->len = 0;
    out->cap = 0;
    out->owner_label = NULL;
}

static inline int encounter_lab_parse_int_value(
    const char* owner_label, const char* value
) {
    char* end = NULL;
    errno = 0;
    long parsed = strtol(value, &end, 10);
    if (errno != 0 || !end || *end != '\0' ||
            parsed < INT32_MIN || parsed > INT32_MAX) {
        encounter_lab_abort(owner_label, "invalid integer %s", value);
    }
    return (int)parsed;
}

static inline uint32_t encounter_lab_parse_seed_value(
    const char* owner_label, const char* value
) {
    char* end = NULL;
    errno = 0;
    unsigned long parsed = strtoul(value, &end, 10);
    if (errno != 0 || !end || *end != '\0' || parsed > UINT32_MAX)
        encounter_lab_abort(owner_label, "invalid seed %s", value);
    return (uint32_t)parsed;
}

static inline EncounterLabOptionalInt encounter_lab_parse_optional_full_int(
    const char* owner_label, const char* value
) {
    if (strcmp(value, "full") == 0) return encounter_lab_optional_int_unset();
    return encounter_lab_optional_int_set(
        encounter_lab_parse_int_value(owner_label, value));
}

static inline char* encounter_lab_trim(char* s) {
    while (*s && isspace((unsigned char)*s)) s++;
    char* end = s + strlen(s);
    while (end > s && isspace((unsigned char)end[-1])) end--;
    *end = '\0';
    return s;
}

static inline void encounter_lab_parse_key_value(
    const char* owner_label, char* token, const char** key, const char** value
) {
    char* eq = strchr(token, '=');
    if (!eq || eq == token || eq[1] == '\0')
        encounter_lab_abort(owner_label, "expected key=value token, got %s", token);
    *eq = '\0';
    *key = token;
    *value = eq + 1;
}

static inline char* encounter_lab_next_token(const char* owner_label, char** cursor) {
    if (!cursor || !*cursor) encounter_lab_abort(owner_label, "null token cursor");
    char* start = *cursor + strspn(*cursor, " \t\r\n");
    if (*start == '\0') {
        *cursor = start;
        return NULL;
    }
    char* end = start + strcspn(start, " \t\r\n");
    if (*end != '\0') {
        *end = '\0';
        *cursor = end + 1;
    } else {
        *cursor = end;
    }
    return start;
}

/**
 * Command alias row for scenario lab command tables.
 */
typedef struct {
    const char* name;
    int kind;
} EncounterLabCommandAlias;

static inline int encounter_lab_lookup_command_kind(
    const char* owner_label,
    const char* command,
    const EncounterLabCommandAlias* aliases,
    size_t alias_count
) {
    for (size_t i = 0; i < alias_count; i++) {
        if (strcmp(command, aliases[i].name) == 0) return aliases[i].kind;
    }
    encounter_lab_abort(owner_label, "unknown script command %s", command);
    return 0;
}

typedef struct {
    uint32_t magic;
    uint32_t version;
    uint32_t state_size;
    uint32_t reserved;
} EncounterSnapshotFrame;

static inline void encounter_snapshot_write_frame(
    void* snapshot,
    size_t snapshot_size,
    uint32_t magic,
    uint32_t version,
    size_t state_size
) {
    memset(snapshot, 0, snapshot_size);
    EncounterSnapshotFrame* frame = (EncounterSnapshotFrame*)snapshot;
    frame->magic = magic;
    frame->version = version;
    frame->state_size = (uint32_t)state_size;
}

static inline const EncounterSnapshotFrame* encounter_snapshot_validate_frame(
    const char* owner_label,
    const void* data,
    size_t actual_size,
    size_t expected_size,
    uint32_t magic,
    uint32_t version,
    size_t state_size
) {
    if (actual_size != expected_size) {
        fprintf(stderr, "%s: bad snapshot size %zu (expected %zu)\n",
            owner_label, actual_size, expected_size);
        abort();
    }
    const EncounterSnapshotFrame* frame = (const EncounterSnapshotFrame*)data;
    if (frame->magic != magic || frame->version != version) {
        fprintf(stderr, "%s: bad magic/version (got 0x%08x v%u, want 0x%08x v%u)\n",
            owner_label, frame->magic, frame->version, magic, version);
        abort();
    }
    if (frame->state_size != state_size) {
        fprintf(stderr, "%s: state size mismatch (got %u, want %zu)\n",
            owner_label, frame->state_size, state_size);
        abort();
    }
    return frame;
}

static inline void encounter_snapshot_copy_state_to(
    void* snapshot, size_t state_offset, const void* state, size_t state_size
) {
    memcpy((uint8_t*)snapshot + state_offset, state, state_size);
}

static inline void encounter_snapshot_copy_state_from(
    void* state, const void* snapshot, size_t state_offset, size_t state_size
) {
    memcpy(state, (const uint8_t*)snapshot + state_offset, state_size);
}

static inline void encounter_write_terminal_status_text(
    int episode_over,
    int winner,
    int win_outcome,
    const char* win_text,
    const char* killed_by_name,
    char* out,
    size_t cap
) {
    if (cap == 0) return;
    out[0] = '\0';
    if (!episode_over) return;
    if (winner == win_outcome) {
        snprintf(out, cap, "%s", win_text);
        return;
    }
    snprintf(out, cap, "Killed by %s", killed_by_name);
}

typedef int (*EncounterLabApplyLineAllocJsonFn)(
    void* lab_state, const char* line, char** out_json);

static inline int encounter_apply_lab_command_dump_wrapper(
    void* lab_state,
    const char* line,
    int dump_result,
    EncounterLabApplyLineAllocJsonFn apply_line_alloc_json
) {
    char* json = NULL;
    int result = apply_line_alloc_json(lab_state, line, &json);
    if (result == dump_result && json) {
        printf("%s\n", json);
        fflush(stdout);
    }
    free(json);
    return result == dump_result ? 1 : 0;
}


typedef struct {
    const char* name;           /* "nh_pvp", "cerberus", "jad", etc. */

    /* observation/action space dimensions */
    int obs_size;               /* raw observation features (before mask) */
    int num_action_heads;
    const int* action_head_dims; /* array of per-head dimensions */
    int mask_size;              /* sum of action_head_dims */

    /* lifecycle: allocate typed encounter runtime state and context */
    size_t state_size;
    size_t context_size;
    void (*init_context)(EncounterContext* context);
    void (*destroy_context)(EncounterContext* context);
    void (*init_state)(EncounterState* state, EncounterContext* context);

    /* legacy lifecycle for callers that still need a one-pointer runtime */
    EncounterState* (*create)(void);
    void (*destroy)(EncounterState* state);

    /* episode lifecycle */
    void (*reset)(EncounterState* state, EncounterContext* context, uint32_t seed);
    void (*step)(EncounterState* state, EncounterContext* context, const int* actions);
    void (*step_human_commands)(
        EncounterState* state, EncounterContext* context, struct HumanInput* hi);

    /* state snapshot/restore for archive-based exploration. NULL = not supported.
       snapshot_size returns the byte count the caller must allocate before calling
       snapshot. snapshot writes the full encounter state to `out`. restore loads
       it back. snapshot+restore must round-trip exactly: stepping from a restored
       state with a fixed action sequence reproduces the same trajectory the
       snapshot was taken from. */
    size_t (*snapshot_size)(EncounterState* state, EncounterContext* context);
    void (*snapshot)(EncounterState* state, EncounterContext* context, void* out);
    void (*restore)(
        EncounterState* state, EncounterContext* context, const void* data, size_t n);

    /* Archive cell representation for Go-Explore-style exploration. NULL = not
       supported. write_cell_key writes a fixed-size byte struct into `out` that
       discretizes the state into a cell. Two states map to the same cell iff
       their cell keys are byte-equal. progress_score returns a scalar in roughly
       [0, 1.5] where higher = closer to solving the encounter; used to compare
       elites within a cell and to weight cell selection. */
    size_t (*cell_key_size)(EncounterState* state, EncounterContext* context);
    void (*write_cell_key)(EncounterState* state, EncounterContext* context, void* out);
    float (*progress_score)(EncounterState* state, EncounterContext* context);

    /* RL interface */
    void (*write_obs)(EncounterState* state, EncounterContext* context, float* obs_out);
    void (*write_mask)(EncounterState* state, EncounterContext* context, float* mask_out);
    float (*get_reward)(EncounterState* state, EncounterContext* context);
    int (*is_terminal)(EncounterState* state, EncounterContext* context);

    /* entity access for renderer (returns entity count, writes entity pointers).
       renderer uses this to draw all entities generically. */
    int (*get_entity_count)(EncounterState* state, EncounterContext* context);
    void* (*get_entity)(
        EncounterState* state, EncounterContext* context, int index);  /* returns Player* */

    /* render entity population: fills array of RenderEntity structs for the renderer.
       replaces get_entity casting for rendering. NULL = renderer falls back to get_entity. */
    void (*fill_render_entities)(
        EncounterState* state,
        EncounterContext* context,
        RenderEntity* out,
        int max_entities,
        int* count);

    /* encounter-specific config (key-value put/get for binding kwargs) */
    void (*put_int)(EncounterState* state, EncounterContext* context, const char* key, int value);
    void (*put_float)(
        EncounterState* state, EncounterContext* context, const char* key, float value);
    void (*put_ptr)(
        EncounterState* state, EncounterContext* context, const char* key, void* value);

    /* arena bounds for renderer (0 = use FIGHT_AREA_* defaults) */
    int arena_base_x, arena_base_y;
    int arena_width, arena_height;

    /* human mode input translation (per-encounter, NULL = no human mode).
       translates semantic HumanInput intents to encounter-specific action arrays.
       each encounter owns its own mapping since action head layouts differ. */
    void (*translate_human_input)(
        struct HumanInput* hi, int* actions, EncounterState* state, EncounterContext* context);
    int (*is_human_targetable_npc_slot)(
        EncounterState* state, EncounterContext* context, int npc_slot);

    /* scenario lab: apply one line of the encounter's line-based command language
       to hand-edit the live scenario (spawn/move/kill NPCs, set wave/boss/player,
       etc). NULL = encounter has no lab. The viewer drives both encounters through
       this one hook. Returns 1 if the line requested a JSON dump (the encounter
       printed it to stdout), 0 otherwise. Invalid lines abort loudly. */
    int (*apply_lab_command)(
        EncounterState* state, EncounterContext* context, const char* line);

    /* action head indices used by shared translate helpers and renderer.
       set to -1 if the encounter doesn't have that action head. */
    int head_move;     /* movement (walk/run) */
    int head_prayer;   /* prayer switching */
    int head_target;   /* NPC target selection (index into NPC array) */

    /* render hooks (optional — NULL if not implemented).
       populates visual overlay data for the renderer. */
    void (*render_post_tick)(
        EncounterState* state, EncounterContext* context, EncounterOverlay* overlay);

    /* logging (returns pointer to encounter's Log struct, or NULL) */
    void* (*get_log)(EncounterState* state, EncounterContext* context);

    /* tick access */
    int (*get_tick)(EncounterState* state, EncounterContext* context);
    int (*get_winner)(EncounterState* state, EncounterContext* context);
} EncounterDef;

static inline EncounterRuntime encounter_runtime_create(const EncounterDef* def) {
    EncounterRuntime runtime = {0};
    if (!def || def->state_size == 0 || def->context_size == 0) {
        fprintf(stderr, "encounter_runtime_create: %s has no typed runtime\n",
            def ? def->name : "(null)");
        abort();
    }
    runtime.state = (EncounterState*)calloc(1, def->state_size);
    runtime.context = (EncounterContext*)calloc(1, def->context_size);
    if (!runtime.state || !runtime.context) {
        fprintf(stderr, "encounter_runtime_create: out of memory for %s\n", def->name);
        abort();
    }
    if (def->init_context) def->init_context(runtime.context);
    if (def->init_state) def->init_state(runtime.state, runtime.context);
    return runtime;
}

static inline void encounter_runtime_destroy(const EncounterDef* def, EncounterRuntime* runtime) {
    if (!runtime) return;
    if (def && def->destroy_context && runtime->context) {
        def->destroy_context(runtime->context);
    }
    free(runtime->state);
    free(runtime->context);
    runtime->state = NULL;
    runtime->context = NULL;
}


#define MAX_ENCOUNTERS 32

typedef struct {
    const EncounterDef* defs[MAX_ENCOUNTERS];
    int count;
} EncounterRegistry;

/* WARNING: static in header — each TU gets its own copy. only works correctly
   when all encounter headers are included from a single compilation unit. */
static EncounterRegistry g_encounter_registry = { .count = 0 };

static inline void encounter_register(const EncounterDef* def) {
    if (g_encounter_registry.count >= MAX_ENCOUNTERS) {
        fprintf(stderr, "encounter registry capacity exceeded: %d\n", MAX_ENCOUNTERS);
        abort();
    }
    g_encounter_registry.defs[g_encounter_registry.count++] = def;
}

static inline const EncounterDef* encounter_find(const char* name) {
    for (int i = 0; i < g_encounter_registry.count; i++) {
        if (strcmp(g_encounter_registry.defs[i]->name, name) == 0) {
            return g_encounter_registry.defs[i];
        }
    }
    return NULL;
}

#endif /* OSRS_ENCOUNTER_H */
