/**
 * @file osrs_pathfinding.h
 * @brief BFS pathfinder for OSRS tile-based movement
 *
 * BFS pathfinder (OSRS uses BFS despite some implementations naming it "Dijkstra").
 * Default grid is 104x104 (OSRS client scene size). Encounters with smaller arenas
 * can use pathfind_step_arena() with custom dimensions to reduce stack/memset cost.
 *
 * Returns the next step direction for the agent to take. Full path reconstruction
 * is not needed since agents re-plan every tick.
 */

#ifndef OSRS_PATHFINDING_H
#define OSRS_PATHFINDING_H

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef __cplusplus
#define OSRS_THREAD_LOCAL thread_local
#else
#define OSRS_THREAD_LOCAL _Thread_local
#endif

#include "osrs_collision.h"

#define PATHFIND_GRID_SIZE 104
#define PATHFIND_ARENA_MAX 48   /* max arena dimension for pathfind_step_arena */
#define PATHFIND_MAX_QUEUE_FULL (PATHFIND_GRID_SIZE * PATHFIND_GRID_SIZE)
#define PATHFIND_MAX_QUEUE_ARENA (PATHFIND_ARENA_MAX * PATHFIND_ARENA_MAX)
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
 * @param map            Collision map (may be NULL = no obstacles)
 * @param height         Height plane (0 for standard PvP)
 * @param src_x          Source global x
 * @param src_y          Source global y
 * @param dest_x         Destination global x
 * @param dest_y         Destination global y
 * @param extra_blocked  Optional callback: returns 1 if tile is blocked by dynamic
 *                       objects (pillars, etc.). NULL = no extra checks.
 * @param blocked_ctx    Context pointer passed to extra_blocked
 * @return PathResult with first step direction
 */
typedef int (*pathfind_blocked_fn)(void* ctx, int abs_x, int abs_y);

static inline void pathfind_enqueue_or_abort(
    int* queue_x, int* queue_y, int* tail, int capacity, int x, int y
) {
    if (*tail >= capacity) {
        fprintf(stderr, "pathfind queue overflow: capacity=%d\n", capacity);
        abort();
    }

    queue_x[*tail] = x;
    queue_y[*tail] = y;
    (*tail)++;
}

static inline PathResult pathfind_step(const CollisionMap* map, int height,
                                       int src_x, int src_y, int dest_x, int dest_y,
                                       pathfind_blocked_fn extra_blocked, void* blocked_ctx) {
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
    int queue_x[PATHFIND_MAX_QUEUE_FULL];
    int queue_y[PATHFIND_MAX_QUEUE_FULL];
    int head = 0;
    int tail = 0;

    /* seed the source */
    via[local_src_x][local_src_y] = VIA_START;
    cost[local_src_x][local_src_y] = 1;
    pathfind_enqueue_or_abort(
        queue_x, queue_y, &tail, PATHFIND_MAX_QUEUE_FULL, local_src_x, local_src_y);

    int found_path = 0;
    int cur_x, cur_y;

    /* BFS expansion */
    while (head < tail) {
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

        /* macro: check extra_blocked callback for dynamic obstacles (pillars etc.) */
        #define EB(ax, ay) (extra_blocked && extra_blocked(blocked_ctx, (ax), (ay)))

        /* try south (y - 1) */
        if (cur_y > 0 && via[cur_x][cur_y - 1] == 0
            && collision_traversable_south(map, height, abs_x, abs_y)
            && !EB(abs_x, abs_y - 1)) {
            pathfind_enqueue_or_abort(
                queue_x, queue_y, &tail, PATHFIND_MAX_QUEUE_FULL, cur_x, cur_y - 1);
            via[cur_x][cur_y - 1] = VIA_S;
            cost[cur_x][cur_y - 1] = next_cost;
        }

        /* try west (x - 1) */
        if (cur_x > 0 && via[cur_x - 1][cur_y] == 0
            && collision_traversable_west(map, height, abs_x, abs_y)
            && !EB(abs_x - 1, abs_y)) {
            pathfind_enqueue_or_abort(
                queue_x, queue_y, &tail, PATHFIND_MAX_QUEUE_FULL, cur_x - 1, cur_y);
            via[cur_x - 1][cur_y] = VIA_W;
            cost[cur_x - 1][cur_y] = next_cost;
        }

        /* try north (y + 1) */
        if (cur_y < PATHFIND_GRID_SIZE - 1 && via[cur_x][cur_y + 1] == 0
            && collision_traversable_north(map, height, abs_x, abs_y)
            && !EB(abs_x, abs_y + 1)) {
            pathfind_enqueue_or_abort(
                queue_x, queue_y, &tail, PATHFIND_MAX_QUEUE_FULL, cur_x, cur_y + 1);
            via[cur_x][cur_y + 1] = VIA_N;
            cost[cur_x][cur_y + 1] = next_cost;
        }

        /* try east (x + 1) */
        if (cur_x < PATHFIND_GRID_SIZE - 1 && via[cur_x + 1][cur_y] == 0
            && collision_traversable_east(map, height, abs_x, abs_y)
            && !EB(abs_x + 1, abs_y)) {
            pathfind_enqueue_or_abort(
                queue_x, queue_y, &tail, PATHFIND_MAX_QUEUE_FULL, cur_x + 1, cur_y);
            via[cur_x + 1][cur_y] = VIA_E;
            cost[cur_x + 1][cur_y] = next_cost;
        }

        /* try south-west */
        if (cur_x > 0 && cur_y > 0 && via[cur_x - 1][cur_y - 1] == 0
            && collision_traversable_south_west(map, height, abs_x, abs_y)
            && collision_traversable_south(map, height, abs_x, abs_y)
            && collision_traversable_west(map, height, abs_x, abs_y)
            && !EB(abs_x - 1, abs_y - 1) && !EB(abs_x, abs_y - 1) && !EB(abs_x - 1, abs_y)) {
            pathfind_enqueue_or_abort(
                queue_x, queue_y, &tail, PATHFIND_MAX_QUEUE_FULL, cur_x - 1, cur_y - 1);
            via[cur_x - 1][cur_y - 1] = VIA_SW;
            cost[cur_x - 1][cur_y - 1] = next_cost;
        }

        /* try north-west */
        if (cur_x > 0 && cur_y < PATHFIND_GRID_SIZE - 1 && via[cur_x - 1][cur_y + 1] == 0
            && collision_traversable_north_west(map, height, abs_x, abs_y)
            && collision_traversable_north(map, height, abs_x, abs_y)
            && collision_traversable_west(map, height, abs_x, abs_y)
            && !EB(abs_x - 1, abs_y + 1) && !EB(abs_x, abs_y + 1) && !EB(abs_x - 1, abs_y)) {
            pathfind_enqueue_or_abort(
                queue_x, queue_y, &tail, PATHFIND_MAX_QUEUE_FULL, cur_x - 1, cur_y + 1);
            via[cur_x - 1][cur_y + 1] = VIA_NW;
            cost[cur_x - 1][cur_y + 1] = next_cost;
        }

        /* try south-east */
        if (cur_x < PATHFIND_GRID_SIZE - 1 && cur_y > 0 && via[cur_x + 1][cur_y - 1] == 0
            && collision_traversable_south_east(map, height, abs_x, abs_y)
            && collision_traversable_south(map, height, abs_x, abs_y)
            && collision_traversable_east(map, height, abs_x, abs_y)
            && !EB(abs_x + 1, abs_y - 1) && !EB(abs_x, abs_y - 1) && !EB(abs_x + 1, abs_y)) {
            pathfind_enqueue_or_abort(
                queue_x, queue_y, &tail, PATHFIND_MAX_QUEUE_FULL, cur_x + 1, cur_y - 1);
            via[cur_x + 1][cur_y - 1] = VIA_SE;
            cost[cur_x + 1][cur_y - 1] = next_cost;
        }

        /* try north-east */
        if (cur_x < PATHFIND_GRID_SIZE - 1 && cur_y < PATHFIND_GRID_SIZE - 1
            && via[cur_x + 1][cur_y + 1] == 0
            && collision_traversable_north_east(map, height, abs_x, abs_y)
            && collision_traversable_north(map, height, abs_x, abs_y)
            && collision_traversable_east(map, height, abs_x, abs_y)
            && !EB(abs_x + 1, abs_y + 1) && !EB(abs_x, abs_y + 1) && !EB(abs_x + 1, abs_y)) {
            pathfind_enqueue_or_abort(
                queue_x, queue_y, &tail, PATHFIND_MAX_QUEUE_FULL, cur_x + 1, cur_y + 1);
            via[cur_x + 1][cur_y + 1] = VIA_NE;
            cost[cur_x + 1][cur_y + 1] = next_cost;
        }

        #undef EB
    }

    /* fallback: if no exact path, find the BFS-reached tile with minimum Manhattan
       distance to the requested dest. matches RuneC rc_find_path; the prior
       radius-bounded scan silently failed when the nearest approach was farther
       than 10 tiles. */
    if (!found_path) {
        int best_manhattan = PATHFIND_GRID_SIZE * 2;
        int best_cost = 999999;
        int best_x = -1, best_y = -1;

        for (int fx = 0; fx < PATHFIND_GRID_SIZE; fx++) {
            for (int fy = 0; fy < PATHFIND_GRID_SIZE; fy++) {
                if (cost[fx][fy] == 0) continue;

                int ddx = fx - local_dest_x;
                int ddy = fy - local_dest_y;
                int manhattan = (ddx < 0 ? -ddx : ddx) + (ddy < 0 ? -ddy : ddy);

                if (manhattan < best_manhattan ||
                    (manhattan == best_manhattan && cost[fx][fy] < best_cost)) {
                    best_manhattan = manhattan;
                    best_cost = cost[fx][fy];
                    best_x = fx;
                    best_y = fy;
                }
            }
        }

        if (best_x == -1) {
            return result;
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

/**
 * Arena-scoped BFS: same algorithm as pathfind_step but with a smaller grid.
 * Caller provides the arena origin and dimensions. All coordinates are in the
 * same world-space as the full pathfinder — the function subtracts the origin
 * internally.
 *
 * Stack cost: ~2 * W * H * 4 + 2 * QUEUE * 4 bytes.
 * For a 32x32 arena: ~8KB + ~20KB queue = ~28KB (vs ~155KB for 104x104).
 */
static inline PathResult pathfind_step_arena(
    const CollisionMap* map, int height,
    int src_x, int src_y, int dest_x, int dest_y,
    pathfind_blocked_fn extra_blocked, void* blocked_ctx,
    int arena_origin_x, int arena_origin_y, int arena_w, int arena_h
) {
    PathResult result = {0, 0, 0, dest_x, dest_y};

    if (arena_w <= 0 || arena_w > PATHFIND_ARENA_MAX ||
        arena_h <= 0 || arena_h > PATHFIND_ARENA_MAX) {
        fprintf(stderr, "pathfind arena dimensions out of bounds: %dx%d\n", arena_w, arena_h);
        abort();
    }

    if (src_x == dest_x && src_y == dest_y) {
        result.found = 1;
        return result;
    }

    /* convert to arena-local coordinates */
    int local_src_x = src_x - arena_origin_x;
    int local_src_y = src_y - arena_origin_y;
    int local_dest_x = dest_x - arena_origin_x;
    int local_dest_y = dest_y - arena_origin_y;

    /* bounds check */
    if (local_src_x < 0 || local_src_x >= arena_w ||
        local_src_y < 0 || local_src_y >= arena_h ||
        local_dest_x < 0 || local_dest_x >= arena_w ||
        local_dest_y < 0 || local_dest_y >= arena_h) {
        return result;
    }

    /* BFS working arrays with generation counter — no memset needed.
       a cell is "visited" when gen[x][y] == current_gen. via/cost are only
       valid when gen matches. this eliminates the ~18KB memset that was the
       dominant cost (651K calls × 18KB = ~11GB of zeroing per training run). */
    static OSRS_THREAD_LOCAL uint16_t bfs_gen[PATHFIND_ARENA_MAX][PATHFIND_ARENA_MAX];
    static OSRS_THREAD_LOCAL int8_t   bfs_via[PATHFIND_ARENA_MAX][PATHFIND_ARENA_MAX];
    static OSRS_THREAD_LOCAL int16_t  bfs_cost[PATHFIND_ARENA_MAX][PATHFIND_ARENA_MAX];
    static OSRS_THREAD_LOCAL uint16_t bfs_gen_counter = 0;
    bfs_gen_counter++;
    if (bfs_gen_counter == 0) {
        /* wraparound: rare (every 65536 calls), just clear the gen array */
        memset(bfs_gen, 0, sizeof(bfs_gen));
        bfs_gen_counter = 1;
    }
    uint16_t gen = bfs_gen_counter;
    #define BFS_VISITED(x, y) (bfs_gen[(x)][(y)] == gen)
    #define BFS_VISIT(x, y, v, c) do { \
        bfs_gen[(x)][(y)] = gen; bfs_via[(x)][(y)] = (v); bfs_cost[(x)][(y)] = (c); \
    } while(0)
    #define BFS_VIA(x, y)  bfs_via[(x)][(y)]
    #define BFS_COST(x, y) bfs_cost[(x)][(y)]

    int queue_x[PATHFIND_MAX_QUEUE_ARENA];
    int queue_y[PATHFIND_MAX_QUEUE_ARENA];
    int head = 0, tail = 0;

    BFS_VISIT(local_src_x, local_src_y, VIA_START, 1);
    pathfind_enqueue_or_abort(
        queue_x, queue_y, &tail, PATHFIND_MAX_QUEUE_ARENA, local_src_x, local_src_y);

    int found_path = 0;
    int cur_x, cur_y;

    while (head < tail) {
        cur_x = queue_x[head];
        cur_y = queue_y[head];
        head++;

        if (cur_x == local_dest_x && cur_y == local_dest_y) {
            found_path = 1;
            break;
        }

        int abs_x = arena_origin_x + cur_x;
        int abs_y = arena_origin_y + cur_y;
        int next_cost = BFS_COST(cur_x, cur_y) + 1;

        #define EB(ax, ay) (extra_blocked && extra_blocked(blocked_ctx, (ax), (ay)))

        /* south */
        if (cur_y > 0 && !BFS_VISITED(cur_x, cur_y - 1)
            && collision_traversable_south(map, height, abs_x, abs_y)
            && !EB(abs_x, abs_y - 1)) {
            pathfind_enqueue_or_abort(
                queue_x, queue_y, &tail, PATHFIND_MAX_QUEUE_ARENA, cur_x, cur_y - 1);
            BFS_VISIT(cur_x, cur_y - 1, VIA_S, next_cost);
        }
        /* west */
        if (cur_x > 0 && !BFS_VISITED(cur_x - 1, cur_y)
            && collision_traversable_west(map, height, abs_x, abs_y)
            && !EB(abs_x - 1, abs_y)) {
            pathfind_enqueue_or_abort(
                queue_x, queue_y, &tail, PATHFIND_MAX_QUEUE_ARENA, cur_x - 1, cur_y);
            BFS_VISIT(cur_x - 1, cur_y, VIA_W, next_cost);
        }
        /* north */
        if (cur_y < arena_h - 1 && !BFS_VISITED(cur_x, cur_y + 1)
            && collision_traversable_north(map, height, abs_x, abs_y)
            && !EB(abs_x, abs_y + 1)) {
            pathfind_enqueue_or_abort(
                queue_x, queue_y, &tail, PATHFIND_MAX_QUEUE_ARENA, cur_x, cur_y + 1);
            BFS_VISIT(cur_x, cur_y + 1, VIA_N, next_cost);
        }
        /* east */
        if (cur_x < arena_w - 1 && !BFS_VISITED(cur_x + 1, cur_y)
            && collision_traversable_east(map, height, abs_x, abs_y)
            && !EB(abs_x + 1, abs_y)) {
            pathfind_enqueue_or_abort(
                queue_x, queue_y, &tail, PATHFIND_MAX_QUEUE_ARENA, cur_x + 1, cur_y);
            BFS_VISIT(cur_x + 1, cur_y, VIA_E, next_cost);
        }
        /* south-west */
        if (cur_x > 0 && cur_y > 0 && !BFS_VISITED(cur_x - 1, cur_y - 1)
            && collision_traversable_south_west(map, height, abs_x, abs_y)
            && collision_traversable_south(map, height, abs_x, abs_y)
            && collision_traversable_west(map, height, abs_x, abs_y)
            && !EB(abs_x - 1, abs_y - 1) && !EB(abs_x, abs_y - 1) && !EB(abs_x - 1, abs_y)) {
            pathfind_enqueue_or_abort(
                queue_x, queue_y, &tail, PATHFIND_MAX_QUEUE_ARENA, cur_x - 1, cur_y - 1);
            BFS_VISIT(cur_x - 1, cur_y - 1, VIA_SW, next_cost);
        }
        /* north-west */
        if (cur_x > 0 && cur_y < arena_h - 1 && !BFS_VISITED(cur_x - 1, cur_y + 1)
            && collision_traversable_north_west(map, height, abs_x, abs_y)
            && collision_traversable_north(map, height, abs_x, abs_y)
            && collision_traversable_west(map, height, abs_x, abs_y)
            && !EB(abs_x - 1, abs_y + 1) && !EB(abs_x, abs_y + 1) && !EB(abs_x - 1, abs_y)) {
            pathfind_enqueue_or_abort(
                queue_x, queue_y, &tail, PATHFIND_MAX_QUEUE_ARENA, cur_x - 1, cur_y + 1);
            BFS_VISIT(cur_x - 1, cur_y + 1, VIA_NW, next_cost);
        }
        /* south-east */
        if (cur_x < arena_w - 1 && cur_y > 0 && !BFS_VISITED(cur_x + 1, cur_y - 1)
            && collision_traversable_south_east(map, height, abs_x, abs_y)
            && collision_traversable_south(map, height, abs_x, abs_y)
            && collision_traversable_east(map, height, abs_x, abs_y)
            && !EB(abs_x + 1, abs_y - 1) && !EB(abs_x, abs_y - 1) && !EB(abs_x + 1, abs_y)) {
            pathfind_enqueue_or_abort(
                queue_x, queue_y, &tail, PATHFIND_MAX_QUEUE_ARENA, cur_x + 1, cur_y - 1);
            BFS_VISIT(cur_x + 1, cur_y - 1, VIA_SE, next_cost);
        }
        /* north-east */
        if (cur_x < arena_w - 1 && cur_y < arena_h - 1 && !BFS_VISITED(cur_x + 1, cur_y + 1)
            && collision_traversable_north_east(map, height, abs_x, abs_y)
            && collision_traversable_north(map, height, abs_x, abs_y)
            && collision_traversable_east(map, height, abs_x, abs_y)
            && !EB(abs_x + 1, abs_y + 1) && !EB(abs_x, abs_y + 1) && !EB(abs_x + 1, abs_y)) {
            pathfind_enqueue_or_abort(
                queue_x, queue_y, &tail, PATHFIND_MAX_QUEUE_ARENA, cur_x + 1, cur_y + 1);
            BFS_VISIT(cur_x + 1, cur_y + 1, VIA_NE, next_cost);
        }

        #undef EB
    }

    /* fallback: BFS-reached tile with minimum Manhattan distance to dest.
       see pathfind_step for rationale. */
    if (!found_path) {
        int best_manhattan = arena_w + arena_h;
        int best_cost = 999999;
        int best_x = -1, best_y = -1;

        for (int fx = 0; fx < arena_w; fx++) {
            for (int fy = 0; fy < arena_h; fy++) {
                if (!BFS_VISITED(fx, fy) || BFS_COST(fx, fy) == 0) continue;
                int ddx = fx - local_dest_x, ddy = fy - local_dest_y;
                int manhattan = (ddx < 0 ? -ddx : ddx) + (ddy < 0 ? -ddy : ddy);
                if (manhattan < best_manhattan ||
                    (manhattan == best_manhattan && BFS_COST(fx, fy) < best_cost)) {
                    best_manhattan = manhattan;
                    best_cost = BFS_COST(fx, fy);
                    best_x = fx; best_y = fy;
                }
            }
        }

        if (best_x == -1) return result;
        cur_x = best_x; cur_y = best_y;
        found_path = 1;
        result.dest_x = arena_origin_x + best_x;
        result.dest_y = arena_origin_y + best_y;
    }

    /* backtrack to find first step */
    while (1) {
        int v = BFS_VIA(cur_x, cur_y);
        int prev_x = cur_x, prev_y = cur_y;
        if (v & VIA_W) prev_x++; else if (v & VIA_E) prev_x--;
        if (v & VIA_S) prev_y++; else if (v & VIA_N) prev_y--;
        if (prev_x == local_src_x && prev_y == local_src_y) {
            result.found = 1;
            result.next_dx = cur_x - local_src_x;
            result.next_dy = cur_y - local_src_y;
            return result;
        }
        cur_x = prev_x; cur_y = prev_y;
        if (BFS_VIA(cur_x, cur_y) == VIA_NONE || BFS_VIA(cur_x, cur_y) == VIA_START) break;
    }

    return result;
}

#endif /* OSRS_PATHFINDING_H */
