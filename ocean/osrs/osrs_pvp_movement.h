/**
 * @file osrs_pvp_movement.h
 * @brief Movement and tile selection for OSRS PvP simulation
 *
 * Handles player movement including:
 * - Tile selection (adjacent, diagonal, farcast positions)
 * - Pathfinding via the shared encounter stepper
 * - Freeze mechanics integration
 * - Wilderness boundary checking
 *
 * Movement actions:
 *   0 = maintain current movement state
 *   1 = move to adjacent tile (melee range)
 *   2 = move to target's exact tile
 *   3 = move to farcast tile (ranged/magic)
 *   4 = move to diagonal tile
 */

#ifndef OSRS_PVP_MOVEMENT_H
#define OSRS_PVP_MOVEMENT_H

#include "osrs_types.h"
#include "osrs_collision.h"
#include "osrs_encounter.h"
#include "osrs_encounter_player.h"
#include "osrs_pvp_gear.h"

// is_in_wilderness and tile_hash are defined in osrs_types.h

/**
 * Select closest tile adjacent (cardinal) to target.
 *
 * Finds the north/east/south/west tile closest to the player.
 * Tie-breaking: distance to agent > distance to target > tile hash.
 *
 * @param p       Player seeking adjacent position
 * @param target_x Target's x coordinate
 * @param target_y Target's y coordinate
 * @param out_x   Output: selected tile x
 * @param out_y   Output: selected tile y
 * @return 1 if valid tile found, 0 if all candidates out of bounds
 */
static int select_closest_adjacent_tile(Player* p, int target_x, int target_y, int* out_x, int* out_y, const CollisionMap* cmap) {
    int candidates[4][2] = {
        {target_x, target_y + 1},
        {target_x + 1, target_y},
        {target_x, target_y - 1},
        {target_x - 1, target_y}
    };

    int has_best = 0;
    int best_x = 0;
    int best_y = 0;
    int best_dist_agent = 0;
    int best_dist_target = 0;
    int best_hash = 0;

    for (int i = 0; i < 4; i++) {
        int cx = candidates[i][0];
        int cy = candidates[i][1];
        if (!is_in_wilderness(cx, cy)) {
            continue;
        }
        if (!collision_tile_walkable(cmap, 0, cx, cy)) {
            continue;
        }
        int dist_agent = chebyshev_distance(p->x, p->y, cx, cy);
        int dist_target = chebyshev_distance(cx, cy, target_x, target_y);
        int hash = tile_hash(cx, cy);
        if (!has_best ||
            dist_agent < best_dist_agent ||
            (dist_agent == best_dist_agent &&
             (dist_target < best_dist_target ||
              (dist_target == best_dist_target && hash < best_hash)))) {
            has_best = 1;
            best_x = cx;
            best_y = cy;
            best_dist_agent = dist_agent;
            best_dist_target = dist_target;
            best_hash = hash;
        }
    }

    if (!has_best) {
        return 0;
    }
    *out_x = best_x;
    *out_y = best_y;
    return 1;
}

/**
 * Select closest tile diagonal to target.
 *
 * Finds the NE/SE/SW/NW tile closest to the player.
 * Useful for avoiding melee while maintaining attack range.
 *
 * @param p       Player seeking diagonal position
 * @param target_x Target's x coordinate
 * @param target_y Target's y coordinate
 * @param out_x   Output: selected tile x
 * @param out_y   Output: selected tile y
 * @return 1 if valid tile found, 0 if all candidates out of bounds
 */
static int select_closest_diagonal_tile(Player* p, int target_x, int target_y, int* out_x, int* out_y, const CollisionMap* cmap) {
    int candidates[4][2] = {
        {target_x + 1, target_y + 1},
        {target_x + 1, target_y - 1},
        {target_x - 1, target_y - 1},
        {target_x - 1, target_y + 1}
    };

    int has_best = 0;
    int best_x = 0;
    int best_y = 0;
    int best_dist_agent = 0;
    int best_dist_target = 0;
    int best_hash = 0;

    for (int i = 0; i < 4; i++) {
        int cx = candidates[i][0];
        int cy = candidates[i][1];
        if (!is_in_wilderness(cx, cy)) {
            continue;
        }
        if (!collision_tile_walkable(cmap, 0, cx, cy)) {
            continue;
        }
        int dist_agent = chebyshev_distance(p->x, p->y, cx, cy);
        int dist_target = chebyshev_distance(cx, cy, target_x, target_y);
        int hash = tile_hash(cx, cy);
        if (!has_best ||
            dist_agent < best_dist_agent ||
            (dist_agent == best_dist_agent &&
             (dist_target < best_dist_target ||
              (dist_target == best_dist_target && hash < best_hash)))) {
            has_best = 1;
            best_x = cx;
            best_y = cy;
            best_dist_agent = dist_agent;
            best_dist_target = dist_target;
            best_hash = hash;
        }
    }

    if (!has_best) {
        return 0;
    }
    *out_x = best_x;
    *out_y = best_y;
    return 1;
}

/**
 * Select closest tile at specified distance for farcasting.
 *
 * Searches ring of tiles at exact chebyshev distance from target.
 * Used for ranged/magic attacks from safe distance.
 *
 * @param p        Player seeking farcast position
 * @param target_x Target's x coordinate
 * @param target_y Target's y coordinate
 * @param distance Desired chebyshev distance from target
 * @param out_x    Output: selected tile x
 * @param out_y    Output: selected tile y
 * @return 1 if valid tile found, 0 otherwise
 */
static int select_farcast_tile(Player* p, int target_x, int target_y, int distance, int* out_x, int* out_y, const CollisionMap* cmap) {
    /* O(1) closest point on chebyshev ring of radius `distance` centered at target.
     * Clamp player->target delta to [-d, d], then push one axis to ±d if needed. */
    int raw_dx = p->x - target_x;
    int raw_dy = p->y - target_y;
    int d = distance;

    /* clamp to chebyshev ball */
    int dx = raw_dx < -d ? -d : (raw_dx > d ? d : raw_dx);
    int dy = raw_dy < -d ? -d : (raw_dy > d ? d : raw_dy);

    /* ensure we're on the ring (max(|dx|,|dy|) == d) */
    int adx = abs_int(dx);
    int ady = abs_int(dy);
    if (adx < d && ady < d) {
        /* push the axis with larger magnitude to ±d; if tied, push x */
        if (adx >= ady) {
            dx = (raw_dx >= 0) ? d : -d;
        } else {
            dy = (raw_dy >= 0) ? d : -d;
        }
    }

    int cx = target_x + dx;
    int cy = target_y + dy;

    if (is_in_wilderness(cx, cy) && collision_tile_walkable(cmap, 0, cx, cy)) {
        *out_x = cx;
        *out_y = cy;
        return 1;
    }

    /* wilderness boundary edge case: clamp to bounds and retry on ring */
    cx = cx < WILD_MIN_X ? WILD_MIN_X : (cx > WILD_MAX_X ? WILD_MAX_X : cx);
    cy = cy < WILD_MIN_Y ? WILD_MIN_Y : (cy > WILD_MAX_Y ? WILD_MAX_Y : cy);
    if (chebyshev_distance(cx, cy, target_x, target_y) == distance
        && collision_tile_walkable(cmap, 0, cx, cy)) {
        *out_x = cx;
        *out_y = cy;
        return 1;
    }

    /* very rare edge: target near corner of wilderness, no valid tile on ring */
    return 0;
}

static int pvp_tile_walkable(void* ctx, int x, int y) {
    const CollisionMap* cmap = (const CollisionMap*)ctx;
    return is_in_wilderness(x, y) && collision_tile_walkable(cmap, 0, x, y);
}

/**
 * Resolve same-tile stacking after movement.
 *
 * OSRS prevents two unfrozen players from occupying the same tile.
 * When both end up on the same tile, the second mover gets bumped
 * to the nearest valid tile using OSRS BFS priority:
 * W, E, S, N, SW, SE, NW, NE.
 *
 * Exception: walking under a frozen opponent is intentional OSRS
 * strategy (frozen player can't attack you on their tile). Only
 * resolve stacking when the blocker is NOT frozen.
 *
 * @param mover     Player to move off the shared tile
 * @param blocker   The other player (checked for freeze status)
 */
static void resolve_same_tile(Player* mover, Player* blocker, const CollisionMap* cmap) {
    // Walking under a frozen opponent is valid OSRS behavior — skip resolution
    if (blocker->frozen_ticks > 0) {
        return;
    }
    // Frozen mover can't be bumped
    if (mover->frozen_ticks > 0) {
        return;
    }

    // OSRS BFS priority: W, E, S, N, SW, SE, NW, NE
    static const int OFFSETS[8][2] = {
        {-1, 0}, {1, 0}, {0, -1}, {0, 1},
        {-1, -1}, {1, -1}, {-1, 1}, {1, 1}
    };

    for (int i = 0; i < 8; i++) {
        int nx = mover->x + OFFSETS[i][0];
        int ny = mover->y + OFFSETS[i][1];
        if (is_in_wilderness(nx, ny)
            && collision_tile_walkable(cmap, 0, nx, ny)
            && !(nx == blocker->x && ny == blocker->y)) {
            mover->x = nx;
            mover->y = ny;
            mover->dest_x = nx;
            mover->dest_y = ny;
            mover->is_moving = 0;
            return;
        }
    }
}

/* ---------------------------------------------------------------------------
 * Shared encounter SDK glue: lookup callback, arena builder, and the per-tick
 * player step that routes PvP movement through osrs_encounter_player_step.
 *
 * This is the canonical click-anywhere path: HEAD_MOVE picks a 25-action
 * delta target (or a far human-clicked tile sits in walk_dest_x/y), the BFS
 * pathfinder walks one or two steps per tick toward that target, and the
 * shared SDK handles auto-chase when an attack interaction is active.
 * ------------------------------------------------------------------------- */

/**
 * Lookup callback for osrs_encounter_player_step.
 *
 * Resolves the opposing-player slot index to its tile, footprint, and weapon
 * attack range. Both PvP players are 1x1 footprints. Attack range comes from
 * the equipped weapon via get_attack_range — melee returns 1, ranged/magic
 * return weapon-specific values, and the rare halberd case bumps to 2.
 *
 * @param ctx          OsrsEnv* — passes opponent players directly.
 * @param target_slot  0 or 1 (the opposing player index).
 * @param out          Populated on success.
 * @return 1 if target found, 0 if slot invalid.
 */
typedef struct {
    OsrsEnv* env;
    int agent_idx;
    int has_new_target;
    int target_slot;
    AttackStyle style;
    int range;
} PvpAttackMoveIntent;

static int pvp_lookup_attack_target(void* ctx, int target_slot, OsrsAttackTarget* out) {
    if (target_slot < 0 || target_slot >= NUM_AGENTS) return 0;
    PvpAttackMoveIntent* intent = (PvpAttackMoveIntent*)ctx;
    OsrsEnv* env = intent->env;
    Player* target = &env->players[target_slot];
    out->slot = target_slot;
    out->x = target->x;
    out->y = target->y;
    out->size = 1;
    out->attack_range = intent->range;
    out->delivery = (intent->style == ATTACK_STYLE_MELEE || intent->style == ATTACK_STYLE_NONE)
        ? OSRS_ATTACK_DELIVERY_MELEE
        : OSRS_ATTACK_DELIVERY_PROJECTILE;
    return 1;
}

/**
 * Build the standard PvP arena descriptor. Wilderness collision via
 * pvp_tile_walkable, full BFS (arena_w = 0) since the wilderness exceeds
 * the 48-tile arena cap.
 */
static inline OsrsEncounterArena pvp_build_arena(OsrsEnv* env) {
    OsrsEncounterArena arena;
    arena.collision_map = (const CollisionMap*)env->collision_map;
    arena.world_offset_x = 0;
    arena.world_offset_y = 0;
    arena.is_walkable = pvp_tile_walkable;
    arena.walkable_ctx = (void*)arena.collision_map;
    arena.extra_blocked = NULL;
    arena.blocked_ctx = NULL;
    arena.projectile_occlusion = osrs_projectile_occlusion_collision_map(
        arena.collision_map, 0);
    arena.arena_base_x = 0;
    arena.arena_base_y = 0;
    arena.arena_w = 0;
    arena.arena_h = 0;
    return arena;
}

/**
 * Apply one tick of player movement via the shared encounter SDK.
 *
 * Reads walk_dest_x/y from the PvP runtime; if -1 the player is idle and
 * the SDK may still auto-chase an active attack interaction (chase ranges
 * to the lookup target). EXPLICIT_FIRST policy means an in-progress walk
 * always wins over a chase. Returns the SDK result for the caller to log.
 */
static inline OsrsPlayerStepResult pvp_step_player_movement(
    OsrsEnv* env,
    int agent_idx,
    PvpAttackMoveIntent intent
) {
    OsrsPlayerStepResult result = {.target_slot = -1};
    int* dest_x = &env->pvp_runtime.walk_dest_x[agent_idx];
    int* dest_y = &env->pvp_runtime.walk_dest_y[agent_idx];
    Player* p = &env->players[agent_idx];

    if (intent.has_new_target) {
        *dest_x = -1;
        *dest_y = -1;
    }

    int has_dest = *dest_x >= 0 && *dest_y >= 0;
    if (!has_dest && !intent.has_new_target &&
            !osrs_interaction_active(&p->interaction)) {
        return result;
    }

    OsrsEncounterArena arena = pvp_build_arena(env);
    intent.env = env;
    intent.agent_idx = agent_idx;
    OsrsPlayerStepInput input = {
        .player = p,
        .interaction = &p->interaction,
        .target_lookup = pvp_lookup_attack_target,
        .target_ctx = &intent,
        .has_new_target = intent.has_new_target,
        .new_target_slot = intent.target_slot,
        .move_kind = has_dest ? OSRS_PLAYER_MOVE_DESTINATION : OSRS_PLAYER_MOVE_NONE,
        .target_move_policy = OSRS_PLAYER_TARGET_MOVE_EXPLICIT_FIRST,
        .move_action = 0,
        .dest_x = dest_x,
        .dest_y = dest_y,
        .blocked_ticks = p->frozen_ticks,
        .arena = arena,
    };
    result = osrs_encounter_player_step(&input);
    p->is_moving = (*dest_x >= 0) ? 1 : 0;
    return result;
}

/**
 * Convert a HEAD_MOVE action index (1..MOVE_DIM-1) into a wilderness-walkable
 * destination tile and store it in walk_dest. Action 0 (idle) clears walk_dest
 * unless the existing walk_dest is still valid (multi-tick BFS in flight).
 */
static inline void pvp_set_walk_dest_from_head_move(OsrsEnv* env, int agent_idx, int move_action) {
    Player* p = &env->players[agent_idx];
    if (move_action <= 0 || move_action >= MOVE_DIM) return;
    env->pvp_runtime.walk_dest_x[agent_idx] = p->x + ENCOUNTER_MOVE_TARGET_DX[move_action];
    env->pvp_runtime.walk_dest_y[agent_idx] = p->y + ENCOUNTER_MOVE_TARGET_DY[move_action];
}

#endif // OSRS_PVP_MOVEMENT_H
