/**
 * @file osrs_pvp_movement.h
 * @brief Movement and tile selection for OSRS PvP simulation
 *
 * Handles player movement including:
 * - Tile selection (adjacent, diagonal, farcast positions)
 * - Pathfinding via step_toward_destination
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

#include "osrs_pvp_types.h"
#include "osrs_pvp_collision.h"

// is_in_wilderness and tile_hash are defined in osrs_pvp_types.h

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

/**
 * Move player one tile toward their destination (collision-aware).
 *
 * Tries diagonal first when both dx and dy are non-zero. If the diagonal
 * step is blocked by collision, falls back to cardinal x then cardinal y.
 * If all directions are blocked, the player doesn't move.
 *
 * When cmap is NULL, all tiles are traversable (flat arena behavior).
 *
 * @param p    Player to move
 * @param cmap Collision map (may be NULL)
 * @return 1 if moved, 0 if already at destination or blocked
 */
static int step_toward_destination(Player* p, const CollisionMap* cmap) {
    int dx = p->dest_x - p->x;
    int dy = p->dest_y - p->y;
    if (dx == 0 && dy == 0) {
        return 0;
    }

    int step_x = (dx > 0) ? 1 : (dx < 0 ? -1 : 0);
    int step_y = (dy > 0) ? 1 : (dy < 0 ? -1 : 0);

    /* diagonal movement: try diagonal first, then cardinal fallbacks */
    if (step_x != 0 && step_y != 0) {
        if (collision_traversable_step(cmap, 0, p->x, p->y, step_x, step_y)) {
            p->x += step_x;
            p->y += step_y;
            return 1;
        }
        /* diagonal blocked — try cardinal x */
        if (collision_traversable_step(cmap, 0, p->x, p->y, step_x, 0)) {
            p->x += step_x;
            return 1;
        }
        /* try cardinal y */
        if (collision_traversable_step(cmap, 0, p->x, p->y, 0, step_y)) {
            p->y += step_y;
            return 1;
        }
        /* all blocked */
        return 0;
    }

    /* cardinal movement */
    if (collision_traversable_step(cmap, 0, p->x, p->y, step_x, step_y)) {
        p->x += step_x;
        p->y += step_y;
        return 1;
    }

    /* blocked */
    return 0;
}

/**
 * Set player destination and initiate movement.
 *
 * Running moves 2 tiles per tick (OSRS default for PvP).
 * Takes first step, then second step if not at destination.
 *
 * @param p      Player
 * @param dest_x Destination x coordinate
 * @param dest_y Destination y coordinate
 * @param cmap   Collision map (may be NULL)
 */
static void set_destination(Player* p, int dest_x, int dest_y, const CollisionMap* cmap) {
    p->dest_x = dest_x;
    p->dest_y = dest_y;
    if (p->x == dest_x && p->y == dest_y) {
        p->is_moving = 0;
        return;
    }
    // First step (walk)
    if (!step_toward_destination(p, cmap)) {
        p->is_moving = 0;
        return;
    }
    // Second step (run) - only if not at destination yet
    if (p->x != dest_x || p->y != dest_y) {
        step_toward_destination(p, cmap);
    }
    // Still moving if not at destination
    p->is_moving = (p->x != dest_x || p->y != dest_y) ? 1 : 0;
}

/**
 * Process movement action for a player.
 *
 * Movement is blocked when frozen. Otherwise:
 *   0 = maintain current movement state
 *   1 = move to adjacent tile
 *   2 = move to target's tile
 *   3 = farcast (move to distance)
 *   4 = move to diagonal tile
 *
 * @param p               Player processing movement
 * @param target          Target player (for position reference)
 * @param movement_action Action index (0-4)
 * @param farcast_distance Distance for farcast action
 */
static void process_movement(Player* p, Player* target, int movement_action, int farcast_distance, const CollisionMap* cmap) {
    if (p->frozen_ticks > 0) {
        p->is_moving = 0;
        return;
    }

    int moved_before = p->is_moving;
    p->is_moving = 0;

    (void)target;
    int target_x = p->last_obs_target_x;
    int target_y = p->last_obs_target_y;

    switch (movement_action) {
        case 0:
            p->is_moving = moved_before ? 1 : 0;
            break;
        case 1: {
            int dest_x = 0;
            int dest_y = 0;
            if (select_closest_adjacent_tile(p, target_x, target_y, &dest_x, &dest_y, cmap)) {
                set_destination(p, dest_x, dest_y, cmap);
            } else {
                set_destination(p, p->x, p->y, cmap);
            }
            break;
        }
        case 2:
            set_destination(p, target_x, target_y, cmap);
            break;
        case 3: {
            int dest_x = 0;
            int dest_y = 0;
            int target_dist = farcast_distance;
            if (select_farcast_tile(p, target_x, target_y, target_dist, &dest_x, &dest_y, cmap)) {
                set_destination(p, dest_x, dest_y, cmap);
            } else {
                set_destination(p, p->x, p->y, cmap);
            }
            break;
        }
        case 4: {
            int dest_x = 0;
            int dest_y = 0;
            if (select_closest_diagonal_tile(p, target_x, target_y, &dest_x, &dest_y, cmap)) {
                set_destination(p, dest_x, dest_y, cmap);
            } else {
                set_destination(p, p->x, p->y, cmap);
            }
            break;
        }
    }
}

/**
 * Simple chase movement - move toward target's position.
 *
 * Blocked by freeze. Used for basic follow behavior.
 *
 * @param p      Player to move
 * @param target Target to chase
 */
static void move_toward_target(Player* p, Player* target, const CollisionMap* cmap) {
    if (p->frozen_ticks > 0) {
        return;
    }
    set_destination(p, target->x, target->y, cmap);
}

/**
 * Step out from same tile as target to an adjacent tile.
 *
 * When on the same tile as target (distance=0), you cannot attack.
 * This function steps to an adjacent tile so you can attack next tick.
 * Tries directions in order: West, East, South, North (matches Java clippedStep).
 *
 * Blocked by freeze - if frozen on same tile, you're stuck.
 *
 * @param p      Player to move
 * @param target Target (used for position reference)
 */
static void step_out_from_same_tile(Player* p, Player* target, const CollisionMap* cmap) {
    if (p->frozen_ticks > 0) {
        return;
    }

    // Try West (x-1, y)
    int dest_x = target->x - 1;
    int dest_y = target->y;
    if (is_in_wilderness(dest_x, dest_y) && collision_tile_walkable(cmap, 0, dest_x, dest_y)) {
        set_destination(p, dest_x, dest_y, cmap);
        return;
    }
    // Try East (x+1, y)
    dest_x = target->x + 1;
    if (is_in_wilderness(dest_x, dest_y) && collision_tile_walkable(cmap, 0, dest_x, dest_y)) {
        set_destination(p, dest_x, dest_y, cmap);
        return;
    }
    // Try South (x, y-1)
    dest_x = target->x;
    dest_y = target->y - 1;
    if (is_in_wilderness(dest_x, dest_y) && collision_tile_walkable(cmap, 0, dest_x, dest_y)) {
        set_destination(p, dest_x, dest_y, cmap);
        return;
    }
    // Try North (x, y+1)
    dest_y = target->y + 1;
    if (is_in_wilderness(dest_x, dest_y) && collision_tile_walkable(cmap, 0, dest_x, dest_y)) {
        set_destination(p, dest_x, dest_y, cmap);
        return;
    }
    // All directions blocked
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

/**
 * Continue movement for a player who is already moving.
 *
 * Used in sequential mode where movement clicks set destination but
 * don't immediately step. Each tick, players with is_moving=1 should
 * continue moving toward their destination.
 *
 * Running moves 2 tiles per tick (OSRS default for PvP).
 *
 * @param p Player to continue moving
 */
static void continue_movement(Player* p, const CollisionMap* cmap) {
    if (!p->is_moving) {
        return;
    }
    if (p->frozen_ticks > 0) {
        p->is_moving = 0;
        return;
    }
    // First step (walk)
    if (!step_toward_destination(p, cmap)) {
        p->is_moving = 0;
        return;
    }
    // Second step (run) - only if not at destination yet
    if (p->x != p->dest_x || p->y != p->dest_y) {
        step_toward_destination(p, cmap);
    }
    // Still moving if not at destination
    p->is_moving = (p->x != p->dest_x || p->y != p->dest_y) ? 1 : 0;
}

#endif // OSRS_PVP_MOVEMENT_H
