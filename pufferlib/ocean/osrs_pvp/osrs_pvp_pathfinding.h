/**
 * @file osrs_pvp_pathfinding.h
 * @brief BFS pathfinder for OSRS tile-based movement
 *
 * BFS pathfinder (OSRS uses BFS despite some implementations naming it "Dijkstra").
 * Operates on a 104x104 local grid (OSRS client scene size).
 * Uses collision_traversable_step() from osrs_pvp_collision.h for wall/block checks.
 *
 * Returns the next step direction for the agent to take. Full path reconstruction
 * is not needed since agents re-plan every tick.
 *
 * All working arrays are stack-allocated (no malloc per query).
 * Worst case: ~10k tiles visited for 104x104 grid = fast enough at ~1M steps/sec.
 */

#ifndef OSRS_PVP_PATHFINDING_H
#define OSRS_PVP_PATHFINDING_H

#include "osrs_pvp_collision.h"

#define PATHFIND_GRID_SIZE 104
#define PATHFIND_MAX_QUEUE 9000
#define PATHFIND_MAX_FALLBACK_RADIUS 10

/**
 * BFS direction encoding (OSRS bitfield convention).
 *
 * Bits encode which cardinal components the path came FROM:
 *   1 = S, 2 = W, 4 = N, 8 = E
 *   3 = SW, 6 = NW, 9 = SE, 12 = NE
 */
#define VIA_NONE  0
#define VIA_S     1
#define VIA_W     2
#define VIA_SW    3   /* S | W */
#define VIA_N     4
#define VIA_NW    6   /* N | W */
#define VIA_E     8
#define VIA_SE    9   /* S | E */
#define VIA_NE    12  /* N | E */
#define VIA_START 99  /* sentinel for source tile */

/**
 * Result of a pathfinding query.
 *
 * next_dx/next_dy: the direction of the FIRST step from source toward dest.
 * Each is -1, 0, or 1. If found==0, no path exists and dx/dy are 0.
 */
typedef struct {
    int found;    /* 1 = path found (exact or fallback), 0 = no reachable tile */
    int next_dx;  /* first step x direction (-1, 0, 1) */
    int next_dy;  /* first step y direction (-1, 0, 1) */
    int dest_x;   /* actual destination reached (may differ from requested if fallback) */
    int dest_y;
} PathResult;

/**
 * Find the first step direction from (src_x, src_y) toward (dest_x, dest_y)
 * using BFS on a 104x104 local grid with collision checks.
 *
 * If the exact destination is unreachable, falls back to the closest reachable
 * tile within PATHFIND_MAX_FALLBACK_RADIUS of the destination.
 *
 * All working memory is stack-allocated.
 *
 * @param map     Collision map (may be NULL = no obstacles)
 * @param height  Height plane (0 for standard PvP)
 * @param src_x   Source global x
 * @param src_y   Source global y
 * @param dest_x  Destination global x
 * @param dest_y  Destination global y
 * @return PathResult with first step direction
 */
static inline PathResult pathfind_step(const CollisionMap* map, int height,
                                       int src_x, int src_y, int dest_x, int dest_y) {
    PathResult result = {0, 0, 0, dest_x, dest_y};

    /* already there */
    if (src_x == dest_x && src_y == dest_y) {
        result.found = 1;
        return result;
    }

    /* don't pathfind across huge distances */
    int dist = abs(src_x - dest_x);
    int dy_abs = abs(src_y - dest_y);
    if (dy_abs > dist) dist = dy_abs;
    if (dist > 64) {
        return result;
    }

    /* compute region origin for local coordinate conversion.
     * OSRS uses chunkX << 3 as the origin:
     * regionX = (src_x >> 3) - 6, then origin = regionX << 3.
     * this centers the 104x104 grid roughly around the source. */
    int origin_x = ((src_x >> 3) - 6) << 3;
    int origin_y = ((src_y >> 3) - 6) << 3;

    /* convert to local coordinates */
    int local_src_x = src_x - origin_x;
    int local_src_y = src_y - origin_y;
    int local_dest_x = dest_x - origin_x;
    int local_dest_y = dest_y - origin_y;

    /* bounds check: dest must be within the 104x104 grid */
    if (local_dest_x < 0 || local_dest_x >= PATHFIND_GRID_SIZE ||
        local_dest_y < 0 || local_dest_y >= PATHFIND_GRID_SIZE) {
        return result;
    }

    /* BFS working arrays (stack allocated, ~43KB each for int[104][104]) */
    int via[PATHFIND_GRID_SIZE][PATHFIND_GRID_SIZE];
    int cost[PATHFIND_GRID_SIZE][PATHFIND_GRID_SIZE];
    memset(via, 0, sizeof(via));
    memset(cost, 0, sizeof(cost));

    /* BFS queue (circular buffer) */
    int queue_x[PATHFIND_MAX_QUEUE];
    int queue_y[PATHFIND_MAX_QUEUE];
    int head = 0;
    int tail = 0;

    /* seed the source */
    via[local_src_x][local_src_y] = VIA_START;
    cost[local_src_x][local_src_y] = 1;
    queue_x[tail] = local_src_x;
    queue_y[tail] = local_src_y;
    tail++;

    int found_path = 0;
    int cur_x, cur_y;

    /* BFS expansion */
    while (head < tail && tail < PATHFIND_MAX_QUEUE) {
        cur_x = queue_x[head];
        cur_y = queue_y[head];
        head++;

        /* reached exact destination? */
        if (cur_x == local_dest_x && cur_y == local_dest_y) {
            found_path = 1;
            break;
        }

        int abs_x = origin_x + cur_x;
        int abs_y = origin_y + cur_y;
        int next_cost = cost[cur_x][cur_y] + 1;

        /* try south (y - 1) */
        if (cur_y > 0 && via[cur_x][cur_y - 1] == 0
            && collision_traversable_south(map, height, abs_x, abs_y)) {
            queue_x[tail] = cur_x;
            queue_y[tail] = cur_y - 1;
            tail++;
            via[cur_x][cur_y - 1] = VIA_S;
            cost[cur_x][cur_y - 1] = next_cost;
        }

        /* try west (x - 1) */
        if (cur_x > 0 && via[cur_x - 1][cur_y] == 0
            && collision_traversable_west(map, height, abs_x, abs_y)) {
            queue_x[tail] = cur_x - 1;
            queue_y[tail] = cur_y;
            tail++;
            via[cur_x - 1][cur_y] = VIA_W;
            cost[cur_x - 1][cur_y] = next_cost;
        }

        /* try north (y + 1) */
        if (cur_y < PATHFIND_GRID_SIZE - 1 && via[cur_x][cur_y + 1] == 0
            && collision_traversable_north(map, height, abs_x, abs_y)) {
            queue_x[tail] = cur_x;
            queue_y[tail] = cur_y + 1;
            tail++;
            via[cur_x][cur_y + 1] = VIA_N;
            cost[cur_x][cur_y + 1] = next_cost;
        }

        /* try east (x + 1) */
        if (cur_x < PATHFIND_GRID_SIZE - 1 && via[cur_x + 1][cur_y] == 0
            && collision_traversable_east(map, height, abs_x, abs_y)) {
            queue_x[tail] = cur_x + 1;
            queue_y[tail] = cur_y;
            tail++;
            via[cur_x + 1][cur_y] = VIA_E;
            cost[cur_x + 1][cur_y] = next_cost;
        }

        /* try south-west */
        if (cur_x > 0 && cur_y > 0 && via[cur_x - 1][cur_y - 1] == 0
            && collision_traversable_south_west(map, height, abs_x, abs_y)
            && collision_traversable_south(map, height, abs_x, abs_y)
            && collision_traversable_west(map, height, abs_x, abs_y)) {
            queue_x[tail] = cur_x - 1;
            queue_y[tail] = cur_y - 1;
            tail++;
            via[cur_x - 1][cur_y - 1] = VIA_SW;
            cost[cur_x - 1][cur_y - 1] = next_cost;
        }

        /* try north-west */
        if (cur_x > 0 && cur_y < PATHFIND_GRID_SIZE - 1 && via[cur_x - 1][cur_y + 1] == 0
            && collision_traversable_north_west(map, height, abs_x, abs_y)
            && collision_traversable_north(map, height, abs_x, abs_y)
            && collision_traversable_west(map, height, abs_x, abs_y)) {
            queue_x[tail] = cur_x - 1;
            queue_y[tail] = cur_y + 1;
            tail++;
            via[cur_x - 1][cur_y + 1] = VIA_NW;
            cost[cur_x - 1][cur_y + 1] = next_cost;
        }

        /* try south-east */
        if (cur_x < PATHFIND_GRID_SIZE - 1 && cur_y > 0 && via[cur_x + 1][cur_y - 1] == 0
            && collision_traversable_south_east(map, height, abs_x, abs_y)
            && collision_traversable_south(map, height, abs_x, abs_y)
            && collision_traversable_east(map, height, abs_x, abs_y)) {
            queue_x[tail] = cur_x + 1;
            queue_y[tail] = cur_y - 1;
            tail++;
            via[cur_x + 1][cur_y - 1] = VIA_SE;
            cost[cur_x + 1][cur_y - 1] = next_cost;
        }

        /* try north-east */
        if (cur_x < PATHFIND_GRID_SIZE - 1 && cur_y < PATHFIND_GRID_SIZE - 1
            && via[cur_x + 1][cur_y + 1] == 0
            && collision_traversable_north_east(map, height, abs_x, abs_y)
            && collision_traversable_north(map, height, abs_x, abs_y)
            && collision_traversable_east(map, height, abs_x, abs_y)) {
            queue_x[tail] = cur_x + 1;
            queue_y[tail] = cur_y + 1;
            tail++;
            via[cur_x + 1][cur_y + 1] = VIA_NE;
            cost[cur_x + 1][cur_y + 1] = next_cost;
        }
    }

    /* fallback: if no exact path, find closest reachable tile near dest */
    if (!found_path) {
        int best_dist_sq = PATHFIND_MAX_FALLBACK_RADIUS * PATHFIND_MAX_FALLBACK_RADIUS + 1;
        int best_cost = 999999;
        int best_x = -1, best_y = -1;
        int r = PATHFIND_MAX_FALLBACK_RADIUS;

        for (int fx = local_dest_x - r; fx <= local_dest_x + r; fx++) {
            for (int fy = local_dest_y - r; fy <= local_dest_y + r; fy++) {
                if (fx < 0 || fx >= PATHFIND_GRID_SIZE || fy < 0 || fy >= PATHFIND_GRID_SIZE)
                    continue;
                if (cost[fx][fy] == 0) continue;  /* not reached by BFS */

                int ddx = fx - local_dest_x;
                int ddy = fy - local_dest_y;
                int dist_sq = ddx * ddx + ddy * ddy;

                if (dist_sq < best_dist_sq ||
                    (dist_sq == best_dist_sq && cost[fx][fy] < best_cost)) {
                    best_dist_sq = dist_sq;
                    best_cost = cost[fx][fy];
                    best_x = fx;
                    best_y = fy;
                }
            }
        }

        if (best_x == -1) {
            return result;  /* completely unreachable */
        }

        cur_x = best_x;
        cur_y = best_y;
        found_path = 1;
        result.dest_x = origin_x + best_x;
        result.dest_y = origin_y + best_y;
    }

    /* backtrack from cur to source to find the FIRST step */
    while (1) {
        int v = via[cur_x][cur_y];
        /* trace back one step */
        int prev_x = cur_x;
        int prev_y = cur_y;

        if (v & VIA_W) prev_x++;       /* came from east, step back east */
        else if (v & VIA_E) prev_x--;  /* came from west, step back west */

        if (v & VIA_S) prev_y++;       /* came from north, step back north */
        else if (v & VIA_N) prev_y--;  /* came from south, step back south */

        if (prev_x == local_src_x && prev_y == local_src_y) {
            /* cur is the first step from source */
            result.found = 1;
            result.next_dx = cur_x - local_src_x;
            result.next_dy = cur_y - local_src_y;
            return result;
        }

        cur_x = prev_x;
        cur_y = prev_y;

        /* safety: shouldn't happen, but prevent infinite loop */
        if (via[cur_x][cur_y] == VIA_NONE || via[cur_x][cur_y] == VIA_START) {
            break;
        }
    }

    return result;
}

#endif /* OSRS_PVP_PATHFINDING_H */
