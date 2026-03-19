/**
 * @fileoverview osrs_encounter.h — shared encounter interface and core game mechanics.
 *
 * this is the single source of truth for shared OSRS mechanics. all encounters
 * MUST use these abstractions instead of reimplementing their own. adding a
 * new encounter = one header file that calls into these shared systems.
 *
 * SHARED SYSTEMS (in order of appearance in this file):
 *
 *   rendering:
 *     RenderEntity                     value struct for renderer (not Player*)
 *     render_entity_from_player()      copy Player fields to RenderEntity
 *     encounter_resolve_attack_target() match npc_slot to render entity index
 *     EncounterOverlay                 visual overlay (clouds, projectiles, boss)
 *
 *   prayer:
 *     ENCOUNTER_PRAYER_*               canonical 5-value prayer action encoding
 *     encounter_apply_prayer_action()  apply prayer action to OverheadPrayer state
 *
 *   movement:
 *     ENCOUNTER_MOVE_TARGET_DX/DY[25]  direction tables (idle + 8 walk + 16 run)
 *     encounter_move_to_target()       player movement: walk 1 tile or run 2
 *     encounter_move_toward_dest()     BFS click-to-move toward destination
 *     encounter_pathfind()             shared BFS pathfind wrapper
 *
 *   NPC pathfinding:
 *     encounter_npc_step_out_from_under()  shuffle NPC off player tile (OSRS overlap rule)
 *     encounter_npc_step_toward()      greedy 1-tile step (diagonal > x > y)
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
 *     EncounterPrayer                  prayer multiplier enum
 *     encounter_compute_loadout_stats() derive all stats from ITEM_DATABASE + loadout
 *
 *   hit delays:
 *     EncounterPendingHit              queued damage with tick countdown
 *
 * ALSO SEE:
 *   osrs_combat_shared.h              hit chance, tbow formula, barrage AoE, delay formulas
 *   osrs_pvp_combat.h                 PvP-specific damage (prayer, veng, recoil, smite)
 */

#ifndef OSRS_ENCOUNTER_H
#define OSRS_ENCOUNTER_H

#include <stdint.h>
#include <string.h>
#include "osrs_pvp_types.h"
#include "osrs_pvp_items.h"
#include "osrs_pvp_pathfinding.h"
#include "osrs_combat_shared.h"

/* opaque encounter state — each encounter defines its own struct */
typedef struct EncounterState EncounterState;

/* ======================================================================== */
/* shared pending hit system for delayed projectile damage                   */
/* ======================================================================== */

#define ENCOUNTER_MAX_PENDING_HITS 8

/* spell types for barrage freeze/heal effects on pending hits */
#define ENCOUNTER_SPELL_NONE  0
#define ENCOUNTER_SPELL_ICE   1   /* ice barrage: freeze on hit */
#define ENCOUNTER_SPELL_BLOOD 2   /* blood barrage: heal 25% of AoE damage */

typedef struct {
    int active;
    int damage;
    int ticks_remaining;   /* countdown to landing */
    int attack_style;      /* ATTACK_STYLE_* for prayer check at land time */
    int check_prayer;      /* 1 = re-check prayer when hit lands (jad) */
    int spell_type;        /* ENCOUNTER_SPELL_* for freeze/heal effects */
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
        int style;           /* 0=ranged, 1=magic, 2=melee, 3=cloud, 4=spawn_orb */
        int damage;          /* for hit splat at destination */
        /* flight parameters — encounters set these, renderer reads them */
        int duration_ticks;  /* flight duration in client ticks (0 = use default 35) */
        int start_h;         /* start height in OSRS units /128 (0 = use default) */
        int end_h;           /* end height in OSRS units /128 (0 = use default) */
        int curve;           /* OSRS slope param (0 = use default 16) */
        float arc_height;    /* sinusoidal arc peak in tiles (0 = quadratic/straight) */
        int tracks_target;   /* 1 = re-aim toward target each tick */
        int src_size;        /* source entity size for center offset (0 = use boss_size) */
        uint32_t model_id;   /* GFX model from cache (0 = style-based fallback) */
    } projectiles[ENCOUNTER_MAX_OVERLAY_PROJECTILES];
    int projectile_count;

    /* melee targeting: shows which tile Zulrah is staring at */
    int melee_target_active;
    int melee_target_x, melee_target_y;
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
    float arc_height, int tracks_target, int src_size,
    uint32_t model_id
) {
    if (ov->projectile_count >= ENCOUNTER_MAX_OVERLAY_PROJECTILES) return -1;
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
    ov->projectiles[i].tracks_target = tracks_target;
    ov->projectiles[i].src_size = src_size;
    ov->projectiles[i].model_id = model_id;
    return i;
}

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
    int npc_slot;  /* source slot index in encounter's NPC array; -1 for player */
    int attack_target_entity_idx;  /* render entity index of attack target, -1 = none */
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
    out->npc_slot = -1;  /* player, not an NPC */
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

/* ======================================================================== */
/* canonical prayer action encoding                                          */
/* ======================================================================== */

/* all encounters MUST use this encoding for the prayer action head.
   0 = no change (prayer persists from previous tick)
   1 = turn off prayer (PRAYER_NONE)
   2 = protect melee
   3 = protect ranged
   4 = protect magic
   action dim = 5 for any encounter using this encoding. */
#define ENCOUNTER_PRAYER_NO_CHANGE  0
#define ENCOUNTER_PRAYER_OFF        1
#define ENCOUNTER_PRAYER_MELEE      2
#define ENCOUNTER_PRAYER_RANGED     3
#define ENCOUNTER_PRAYER_MAGIC      4
#define ENCOUNTER_PRAYER_DIM        5

/* apply a prayer action to the active prayer state. 0=no change. */
static inline void encounter_apply_prayer_action(OverheadPrayer* prayer, int action) {
    switch (action) {
        case ENCOUNTER_PRAYER_NO_CHANGE: break;
        case ENCOUNTER_PRAYER_OFF:    *prayer = PRAYER_NONE; break;
        case ENCOUNTER_PRAYER_MELEE:  *prayer = PRAYER_PROTECT_MELEE; break;
        case ENCOUNTER_PRAYER_RANGED: *prayer = PRAYER_PROTECT_RANGED; break;
        case ENCOUNTER_PRAYER_MAGIC:  *prayer = PRAYER_PROTECT_MAGIC; break;
    }
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
/* shared BFS click-to-move (human mode + destination-based movement)        */
/* ======================================================================== */

/* shared BFS pathfind wrapper — translates local coords to world coords for pathfind_step.
   extra_blocked/blocked_ctx: optional callback for dynamic obstacles (pillars etc.).
   pass NULL/NULL for encounters with no dynamic obstacles. */
static inline PathResult encounter_pathfind(
    const CollisionMap* cmap, int world_offset_x, int world_offset_y,
    int src_x, int src_y, int dst_x, int dst_y,
    pathfind_blocked_fn extra_blocked, void* blocked_ctx
) {
    if (!cmap) {
        /* no collision map: greedy direction */
        PathResult pr = {0, 0, 0, 0, 0};
        pr.found = 1;
        int dx = dst_x - src_x, dy = dst_y - src_y;
        pr.next_dx = dx > 0 ? 1 : (dx < 0 ? -1 : 0);
        pr.next_dy = dy > 0 ? 1 : (dy < 0 ? -1 : 0);
        return pr;
    }
    return pathfind_step(cmap, 0,
        src_x + world_offset_x, src_y + world_offset_y,
        dst_x + world_offset_x, dst_y + world_offset_y,
        extra_blocked, blocked_ctx);
}

/* shared click-to-move: BFS toward destination, take up to 2 steps (run).
   call each tick when player_dest is set. clears dest when arrived.
   extra_blocked/blocked_ctx: optional dynamic obstacle callback for BFS.
   returns steps taken (0, 1, or 2). */
static inline int encounter_move_toward_dest(
    Player* p, int* dest_x, int* dest_y,
    const CollisionMap* cmap, int world_offset_x, int world_offset_y,
    encounter_walkable_fn is_walkable, void* ctx,
    pathfind_blocked_fn extra_blocked, void* blocked_ctx
) {
    if (*dest_x < 0 || *dest_y < 0) return 0;
    if (p->x == *dest_x && p->y == *dest_y) {
        *dest_x = -1; *dest_y = -1;
        return 0;
    }
    int steps = 0;
    for (int step = 0; step < 2; step++) {
        if (p->x == *dest_x && p->y == *dest_y) break;
        PathResult pr = encounter_pathfind(cmap, world_offset_x, world_offset_y,
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

/* ======================================================================== */
/* shared attack-target chase (auto-walk toward out-of-range NPC)            */
/* ======================================================================== */

/* auto-walk toward attack target when out of range.
   OSRS: player pathfinds toward NPC every tick until in weapon range.
   extra_blocked/blocked_ctx: optional dynamic obstacle callback for BFS.
   returns 1 if player moved (chasing), 0 if already in range or stuck. */
static inline int encounter_chase_attack_target(
    Player* p, int target_x, int target_y, int target_size, int attack_range,
    const CollisionMap* cmap, int world_offset_x, int world_offset_y,
    encounter_walkable_fn is_walkable, void* ctx,
    pathfind_blocked_fn extra_blocked, void* blocked_ctx
) {
    int dist = encounter_dist_to_npc(p->x, p->y, target_x, target_y, target_size);
    if (dist >= 1 && dist <= attack_range) return 0;

    /* nearest tile of the NPC to pathfind toward */
    int cx = p->x < target_x ? target_x :
             (p->x > target_x + target_size - 1 ? target_x + target_size - 1 : p->x);
    int cy = p->y < target_y ? target_y :
             (p->y > target_y + target_size - 1 ? target_y + target_size - 1 : p->y);

    /* BFS up to 2 steps (run) toward closest NPC tile */
    int steps = 0;
    for (int step = 0; step < 2; step++) {
        if (encounter_dist_to_npc(p->x, p->y, target_x, target_y, target_size) <= attack_range)
            break;
        PathResult pr = encounter_pathfind(cmap, world_offset_x, world_offset_y,
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

/* ======================================================================== */
/* shared NPC step-out-from-under (OSRS: NPC shuffles off player tile)       */
/* ======================================================================== */

/* when an NPC overlaps the player (AABB overlap), it shuffles one tile in a
   random cardinal direction. matches osrs-sdk Mob.ts:109-153 behavior:
   50% pick X-axis vs Y-axis, then 50% +1 or -1 on that axis.
   returns 1 if the NPC moved, 0 if stuck or no overlap. */
static inline int encounter_npc_step_out_from_under(
    int* npc_x, int* npc_y, int npc_size,
    int player_x, int player_y,
    encounter_walkable_fn is_walkable, void* ctx, uint32_t* rng
) {
    /* AABB overlap check (handles multi-tile NPCs) */
    int overlap = !(*npc_x >= player_x + 1 || *npc_x + npc_size <= player_x ||
                    *npc_y >= player_y + 1 || *npc_y + npc_size <= player_y);
    if (!overlap) return 0;

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
        if (is_walkable(ctx, nx, ny)) {
            *npc_x = nx;
            *npc_y = ny;
            return 1;
        }
    }
    return 0;
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
/* shared damage application helpers                                         */
/*                                                                           */
/* ENCOUNTERS: use these instead of manually subtracting HP, clamping,       */
/* and setting hit splat flags. prevents bugs from forgetting a step.        */
/* ======================================================================== */

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
    always sets hit_landed so the renderer shows a splat —
    0 damage produces a blue "miss" splat (standard OSRS behavior). */
static inline void encounter_damage_npc(
    int* hp, int* hit_landed, int* hit_damage, int damage
) {
    if (damage > 0) {
        *hp -= damage;
    }
    *hit_landed = 1;
    *hit_damage = damage > 0 ? damage : 0;
}

/* ======================================================================== */
/* shared NPC pending hit resolution (barrage freeze + blood heal)           */
/* ======================================================================== */

/** resolve a single NPC's pending hit. tick down, apply damage when it lands.
    ice barrage: sets *frozen_ticks = BARRAGE_FREEZE_TICKS on hit.
    blood barrage: accumulates landed damage into *blood_heal_acc for 25% heal.
    returns 1 if hit landed this call, 0 otherwise. */
static inline int encounter_resolve_npc_pending_hit(
    EncounterPendingHit* ph,
    int* npc_hp, int* hit_landed, int* hit_damage,
    int* frozen_ticks, int* blood_heal_acc, float* damage_dealt_acc
) {
    if (!ph->active) return 0;
    ph->ticks_remaining--;
    if (ph->ticks_remaining > 0) return 0;

    /* hit landed */
    int dmg = ph->damage;
    encounter_damage_npc(npc_hp, hit_landed, hit_damage, dmg);
    if (damage_dealt_acc) *damage_dealt_acc += dmg;

    /* ice barrage: freeze on hit (including 0 dmg — only splashes don't freeze,
       and splashes never enter the pending hit queue) */
    if (ph->spell_type == ENCOUNTER_SPELL_ICE && frozen_ticks)
        *frozen_ticks = BARRAGE_FREEZE_TICKS;

    /* blood barrage: accumulate damage for 25% heal */
    if (ph->spell_type == ENCOUNTER_SPELL_BLOOD && blood_heal_acc)
        *blood_heal_acc += dmg;

    ph->active = 0;
    return 1;
}

/** resolve player pending hits (NPC attacks landing on the player).
    ticks down each hit, applies damage when it lands, handles deferred
    prayer checks (jad-style: check_prayer=1 re-checks at land time).
    encounters MUST call this each tick for projectile-based NPC attacks.
    prayer_correct_flag: set to 1 when a deferred prayer check succeeds. */
static inline void encounter_resolve_player_pending_hits(
    EncounterPendingHit* hits, int* hit_count,
    Player* player, OverheadPrayer active_prayer,
    float* damage_received_acc, int* prayer_correct_flag
) {
    for (int i = 0; i < *hit_count; i++) {
        hits[i].ticks_remaining--;
        if (hits[i].ticks_remaining <= 0) {
            int dmg = hits[i].damage;
            if (hits[i].check_prayer) {
                if (encounter_prayer_correct_for_style(active_prayer, hits[i].attack_style)) {
                    dmg = 0;
                    if (prayer_correct_flag) *prayer_correct_flag = 1;
                }
            }
            encounter_damage_player(player, dmg, damage_received_acc);
            hits[i] = hits[--(*hit_count)];
            i--;
        }
    }
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
/* shared reset helpers                                                      */
/* ======================================================================== */

/** resolve RNG seed for encounter reset. priority: explicit seed > saved state > default.
    all encounters MUST use this to ensure consistent RNG initialization. */
static inline uint32_t encounter_resolve_seed(uint32_t saved_rng, uint32_t explicit_seed) {
    uint32_t rng = 12345;
    if (saved_rng != 0) rng = saved_rng;
    if (explicit_seed != 0) rng = explicit_seed;
    return rng;
}

/* ======================================================================== */
/* shared prayer drain                                                       */
/*                                                                           */
/* ENCOUNTERS: call encounter_drain_prayer() each tick to drain prayer       */
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
/* ======================================================================== */

/** drain effect values for prayers used in encounters.
    from the OSRS prayer table — higher values drain faster. */
static inline int encounter_prayer_drain_effect(OverheadPrayer prayer) {
    switch (prayer) {
        case PRAYER_PROTECT_MELEE:  return 12;
        case PRAYER_PROTECT_RANGED: return 12;
        case PRAYER_PROTECT_MAGIC:  return 12;
        default: return 0;  /* PRAYER_NONE / PRAYER_OFF — no drain */
    }
}

/** drain prayer points at the correct OSRS rate. call once per game tick.
    prayer_bonus is the player's total prayer equipment bonus (typically 0-30).
    drain_counter is persistent state that accumulates fractional drain between ticks.
    automatically deactivates prayer when points reach 0. */
static inline void encounter_drain_prayer(
    int* current_prayer, OverheadPrayer* active_prayer,
    int prayer_bonus, int* drain_counter
) {
    if (*active_prayer == PRAYER_NONE) return;
    int drain_effect = encounter_prayer_drain_effect(*active_prayer);
    if (drain_effect == 0) return;

    /* OSRS: drain interval = floor((18 + floor(bonus/4)) / drain_effect) seconds
       converted to ticks (1 tick = 0.6s): multiply by 5/3.
       but OSRS actually uses a point-based counter system, not seconds.
       the drain_effect is subtracted from a counter each tick, and when the
       counter goes below 0, a prayer point is drained and counter is reset
       to (60 + prayer_bonus * 15). this is the actual OSRS implementation. */
    *drain_counter -= drain_effect;
    if (*drain_counter <= 0) {
        *drain_counter += 60 + prayer_bonus * 15;
        (*current_prayer)--;
        if (*current_prayer <= 0) {
            *current_prayer = 0;
            *active_prayer = PRAYER_NONE;
        }
    }
}

/* ======================================================================== */
/* shared loadout stat computation                                           */
/*                                                                           */
/* ENCOUNTERS: do NOT manually compute attack bonuses, max hits, or          */
/* effective levels. call encounter_compute_loadout_stats() with a loadout   */
/* array and it derives everything from ITEM_DATABASE automatically.         */
/*                                                                           */
/* available structs/functions:                                               */
/*   EncounterLoadoutStats — computed combat stats for one gear loadout      */
/*   EncounterPrayer       — prayer enum (NONE, AUGURY, RIGOUR, PIETY)      */
/*   encounter_compute_loadout_stats() — derive stats from loadout + prayer  */
/* ======================================================================== */

/** combat stats derived from a gear loadout + prayer + style.
    computed once at reset, read during combat. */
typedef struct {
    int attack_bonus;     /* primary attack bonus for the style */
    int strength_bonus;   /* ranged_strength, magic_damage %, or melee_strength */
    int eff_level;        /* effective attack level (floor(base*prayer) + style + 8) */
    int max_hit;          /* base max hit (before tbow/set bonuses) */
    int attack_speed;     /* ticks between attacks */
    int attack_range;     /* max chebyshev distance */
    AttackStyle style;
    /* defence bonuses from gear */
    int def_stab, def_slash, def_crush, def_magic, def_ranged;
} EncounterLoadoutStats;

/** overhead prayer multipliers for effective level computation. */
typedef enum {
    ENCOUNTER_PRAYER_NONE = 0,
    ENCOUNTER_PRAYER_AUGURY,   /* +25% magic attack, +25% magic defence */
    ENCOUNTER_PRAYER_RIGOUR,   /* +20% ranged attack, +23% ranged strength */
    ENCOUNTER_PRAYER_PIETY,    /* +20% melee attack, +23% melee strength, +25% defence */
} EncounterPrayer;

/** derive all combat stats from a loadout array + prayer + style.
    sums equipment bonuses from ITEM_DATABASE, applies prayer multiplier,
    computes effective level and max hit.

    @param loadout          gear array indexed by GEAR_SLOT_* (ITEM_NONE=255 for empty)
    @param style            ATTACK_STYLE_MAGIC, ATTACK_STYLE_RANGED, or ATTACK_STYLE_MELEE
    @param prayer           prayer enum for level multiplier
    @param base_level       base combat level (usually 99)
    @param style_bonus      +0 for rapid/autocast, +3 for accurate, +1 for controlled
    @param spell_base_damage 0 for ranged/melee, 30 for ice/blood barrage
    @param out              output struct to fill */
static void encounter_compute_loadout_stats(
    const uint8_t loadout[NUM_GEAR_SLOTS],
    AttackStyle style,
    EncounterPrayer prayer,
    int base_level,
    int style_bonus,
    int spell_base_damage,
    EncounterLoadoutStats* out
) {
    memset(out, 0, sizeof(*out));
    out->style = style;

    /* sum equipment bonuses from all gear slots */
    int sum_attack_stab = 0, sum_attack_slash = 0, sum_attack_crush = 0;
    int sum_attack_magic = 0, sum_attack_ranged = 0;
    int sum_melee_strength = 0, sum_ranged_strength = 0, sum_magic_damage = 0;
    int sum_def_stab = 0, sum_def_slash = 0, sum_def_crush = 0;
    int sum_def_magic = 0, sum_def_ranged = 0;

    for (int slot = 0; slot < NUM_GEAR_SLOTS; slot++) {
        uint8_t item_idx = loadout[slot];
        if (item_idx == 255) continue;  /* ITEM_NONE */
        const Item* item = &ITEM_DATABASE[item_idx];
        sum_attack_stab += item->attack_stab;
        sum_attack_slash += item->attack_slash;
        sum_attack_crush += item->attack_crush;
        sum_attack_magic += item->attack_magic;
        sum_attack_ranged += item->attack_ranged;
        sum_melee_strength += item->melee_strength;
        sum_ranged_strength += item->ranged_strength;
        sum_magic_damage += item->magic_damage;
        sum_def_stab += item->defence_stab;
        sum_def_slash += item->defence_slash;
        sum_def_crush += item->defence_crush;
        sum_def_magic += item->defence_magic;
        sum_def_ranged += item->defence_ranged;
    }

    out->def_stab = sum_def_stab;
    out->def_slash = sum_def_slash;
    out->def_crush = sum_def_crush;
    out->def_magic = sum_def_magic;
    out->def_ranged = sum_def_ranged;

    /* weapon slot determines attack_speed and attack_range */
    uint8_t weapon_idx = loadout[GEAR_SLOT_WEAPON];
    if (weapon_idx != 255) {
        const Item* weapon = &ITEM_DATABASE[weapon_idx];
        out->attack_speed = weapon->attack_speed;
        out->attack_range = weapon->attack_range;
    }

    /* primary attack bonus based on style */
    if (style == ATTACK_STYLE_MAGIC) {
        out->attack_bonus = sum_attack_magic;
    } else if (style == ATTACK_STYLE_RANGED) {
        out->attack_bonus = sum_attack_ranged;
    } else {
        /* melee: best of stab/slash/crush */
        out->attack_bonus = sum_attack_stab;
        if (sum_attack_slash > out->attack_bonus) out->attack_bonus = sum_attack_slash;
        if (sum_attack_crush > out->attack_bonus) out->attack_bonus = sum_attack_crush;
    }

    /* prayer multipliers */
    float att_prayer_mult = 1.0f;
    float str_prayer_mult = 1.0f;
    switch (prayer) {
        case ENCOUNTER_PRAYER_AUGURY:
            att_prayer_mult = 1.25f;
            break;
        case ENCOUNTER_PRAYER_RIGOUR:
            att_prayer_mult = 1.20f;
            str_prayer_mult = 1.23f;
            break;
        case ENCOUNTER_PRAYER_PIETY:
            att_prayer_mult = 1.20f;
            str_prayer_mult = 1.23f;
            break;
        case ENCOUNTER_PRAYER_NONE:
            break;
    }

    /* effective attack level: floor(base * prayer_mult) + style_bonus + 8 */
    out->eff_level = (int)(base_level * att_prayer_mult) + style_bonus + 8;

    /* effective strength level (for max hit): floor(base * str_prayer_mult) + style_bonus + 8
       note: style_bonus for strength is typically 0 for rapid/autocast, +3 for aggressive */
    int eff_str_level = (int)(base_level * str_prayer_mult) + style_bonus + 8;

    /* max hit and strength bonus depend on combat style */
    if (style == ATTACK_STYLE_RANGED) {
        out->strength_bonus = sum_ranged_strength;
        out->max_hit = (int)(0.5 + eff_str_level * (sum_ranged_strength + 64) / 640.0);
    } else if (style == ATTACK_STYLE_MAGIC) {
        out->strength_bonus = sum_magic_damage;
        out->max_hit = (int)(spell_base_damage * (1.0 + sum_magic_damage / 100.0));
    } else {
        out->strength_bonus = sum_melee_strength;
        out->max_hit = (int)(0.5 + eff_str_level * (sum_melee_strength + 64) / 640.0);
    }
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

    /* named action head indices for human mode input translation.
       set to -1 if the encounter doesn't have that action head.
       the generic human input translator uses these instead of hardcoded indices. */
    int head_move;     /* movement (walk/run) */
    int head_prayer;   /* prayer switching */
    int head_target;   /* NPC target selection (index into NPC array) */
    int head_gear;     /* gear/loadout switching */
    int head_eat;      /* eat food/brew */
    int head_potion;   /* drink potion (restore, bastion, stamina) */
    int head_spell;    /* spell selection (blood/ice barrage) */
    int head_spec;     /* special attack */
    int head_attack;   /* attack style (mage/range -- zulrah) */

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
