#ifndef OSRS_COLLISION_H
#define OSRS_COLLISION_H

#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "osrs_assets.h"

#define COLLISION_NONE                   0x000000
#define COLLISION_WALL_NORTH_WEST        0x000001
#define COLLISION_WALL_NORTH             0x000002
#define COLLISION_WALL_NORTH_EAST        0x000004
#define COLLISION_WALL_EAST              0x000008
#define COLLISION_WALL_SOUTH_EAST        0x000010
#define COLLISION_WALL_SOUTH             0x000020
#define COLLISION_WALL_SOUTH_WEST        0x000040
#define COLLISION_WALL_WEST              0x000080

#define COLLISION_IMPENETRABLE_WALL_NORTH_WEST  0x000200
#define COLLISION_IMPENETRABLE_WALL_NORTH       0x000400
#define COLLISION_IMPENETRABLE_WALL_NORTH_EAST  0x000800
#define COLLISION_IMPENETRABLE_WALL_EAST        0x001000
#define COLLISION_IMPENETRABLE_WALL_SOUTH_EAST  0x002000
#define COLLISION_IMPENETRABLE_WALL_SOUTH       0x004000
#define COLLISION_IMPENETRABLE_WALL_SOUTH_WEST  0x008000
#define COLLISION_IMPENETRABLE_WALL_WEST        0x010000

#define COLLISION_IMPENETRABLE_BLOCKED   0x020000
#define COLLISION_BRIDGE                 0x040000
#define COLLISION_BLOCKED                0x200000

#define REGION_SIZE      64
#define REGION_HEIGHT_LEVELS 4

typedef struct {
    int flags[REGION_HEIGHT_LEVELS][REGION_SIZE][REGION_SIZE];
} CollisionRegion;

#define REGION_MAP_CAPACITY 256

typedef struct {
    int key;
    CollisionRegion* region;
} RegionMapEntry;

typedef struct {
    RegionMapEntry entries[REGION_MAP_CAPACITY];
    int count;
} CollisionMap;

static inline int collision_region_hash(int x, int y) {
    int region_x = x >> 6;
    int region_y = y >> 6;
    return region_x * 256 + region_y;
}

static inline int collision_local(int coord) {
    return coord & 0x3F;
}

static inline void collision_map_init(CollisionMap* map) {
    map->count = 0;
    for (int i = 0; i < REGION_MAP_CAPACITY; i++) {
        map->entries[i].key = -1;
        map->entries[i].region = NULL;
    }
}

static inline CollisionMap* collision_map_create(void) {
    CollisionMap* map = (CollisionMap*)malloc(sizeof(CollisionMap));
    collision_map_init(map);
    return map;
}

static inline CollisionRegion* collision_map_get(const CollisionMap* map, int key) {
    int idx = key & (REGION_MAP_CAPACITY - 1);
    for (int i = 0; i < REGION_MAP_CAPACITY; i++) {
        int slot = (idx + i) & (REGION_MAP_CAPACITY - 1);
        if (map->entries[slot].key == key) {
            return map->entries[slot].region;
        }
        if (map->entries[slot].key == -1) {
            return NULL;
        }
    }
    return NULL;
}

static inline void collision_map_put(CollisionMap* map, int key, CollisionRegion* region) {
    int idx = key & (REGION_MAP_CAPACITY - 1);
    for (int i = 0; i < REGION_MAP_CAPACITY; i++) {
        int slot = (idx + i) & (REGION_MAP_CAPACITY - 1);
        if (map->entries[slot].key == key) {
            map->entries[slot].region = region;
            return;
        }
        if (map->entries[slot].key == -1) {
            map->entries[slot].key = key;
            map->entries[slot].region = region;
            map->count++;
            return;
        }
    }
    fprintf(stderr, "collision_map_put: map full (capacity %d)\n", REGION_MAP_CAPACITY);
}

static inline void collision_map_free(CollisionMap* map) {
    if (map == NULL) return;
    for (int i = 0; i < REGION_MAP_CAPACITY; i++) {
        if (map->entries[i].region != NULL) {
            free(map->entries[i].region);
        }
    }
    free(map);
}

static inline int collision_get_flags(const CollisionMap* map, int height, int x, int y) {
    if (map == NULL) return COLLISION_NONE;
    int key = collision_region_hash(x, y);
    const CollisionRegion* region = collision_map_get(map, key);
    if (region == NULL) return COLLISION_NONE;
    int lx = collision_local(x);
    int ly = collision_local(y);
    int h = height < 0 ? 0 : (height >= REGION_HEIGHT_LEVELS ? REGION_HEIGHT_LEVELS - 1 : height);
    return region->flags[h][lx][ly];
}


static inline int collision_flags_traversable_step(
    uint32_t destination_flags,
    uint32_t horizontal_side_flags,
    uint32_t vertical_side_flags,
    int dx,
    int dy
) {
    if (dx == 0 && dy == 1)
        return (destination_flags & (COLLISION_WALL_SOUTH | COLLISION_BLOCKED)) == 0;
    if (dx == 0 && dy == -1)
        return (destination_flags & (COLLISION_WALL_NORTH | COLLISION_BLOCKED)) == 0;
    if (dx == 1 && dy == 0)
        return (destination_flags & (COLLISION_WALL_WEST | COLLISION_BLOCKED)) == 0;
    if (dx == -1 && dy == 0)
        return (destination_flags & (COLLISION_WALL_EAST | COLLISION_BLOCKED)) == 0;
    if (dx == 1 && dy == 1)
        return (destination_flags &
                (COLLISION_WALL_WEST | COLLISION_WALL_SOUTH |
                 COLLISION_WALL_SOUTH_WEST | COLLISION_BLOCKED)) == 0 &&
            (horizontal_side_flags &
                (COLLISION_WALL_WEST | COLLISION_BLOCKED)) == 0 &&
            (vertical_side_flags &
                (COLLISION_WALL_SOUTH | COLLISION_BLOCKED)) == 0;
    if (dx == -1 && dy == 1)
        return (destination_flags &
                (COLLISION_WALL_EAST | COLLISION_WALL_SOUTH |
                 COLLISION_WALL_SOUTH_EAST | COLLISION_BLOCKED)) == 0 &&
            (horizontal_side_flags &
                (COLLISION_WALL_EAST | COLLISION_BLOCKED)) == 0 &&
            (vertical_side_flags &
                (COLLISION_WALL_SOUTH | COLLISION_BLOCKED)) == 0;
    if (dx == 1 && dy == -1)
        return (destination_flags &
                (COLLISION_WALL_WEST | COLLISION_WALL_NORTH |
                 COLLISION_WALL_NORTH_WEST | COLLISION_BLOCKED)) == 0 &&
            (horizontal_side_flags &
                (COLLISION_WALL_WEST | COLLISION_BLOCKED)) == 0 &&
            (vertical_side_flags &
                (COLLISION_WALL_NORTH | COLLISION_BLOCKED)) == 0;
    if (dx == -1 && dy == -1)
        return (destination_flags &
                (COLLISION_WALL_EAST | COLLISION_WALL_NORTH |
                 COLLISION_WALL_NORTH_EAST | COLLISION_BLOCKED)) == 0 &&
            (horizontal_side_flags &
                (COLLISION_WALL_EAST | COLLISION_BLOCKED)) == 0 &&
            (vertical_side_flags &
                (COLLISION_WALL_NORTH | COLLISION_BLOCKED)) == 0;
    return 1;
}

static inline int collision_traversable_step(
    const CollisionMap* map,
    int height,
    int x,
    int y,
    int dx,
    int dy
) {
    if (map == NULL) return 1;
    uint32_t horizontal_side_flags = 0;
    uint32_t vertical_side_flags = 0;
    if (dx != 0 && dy != 0) {
        horizontal_side_flags =
            (uint32_t)collision_get_flags(map, height, x + dx, y);
        vertical_side_flags =
            (uint32_t)collision_get_flags(map, height, x, y + dy);
    }
    return collision_flags_traversable_step(
        (uint32_t)collision_get_flags(map, height, x + dx, y + dy),
        horizontal_side_flags,
        vertical_side_flags,
        dx,
        dy);
}









static inline int collision_tile_walkable(const CollisionMap* map, int height, int x, int y) {
    if (map == NULL) return 1;
    return (collision_get_flags(map, height, x, y) & COLLISION_BLOCKED) == 0;
}

#define COLLISION_MAP_MAGIC 0x50414D43
#define COLLISION_MAP_VERSION 1

static inline CollisionMap* collision_map_load(const char* path) {
    FILE* f = osrs_asset_fopen(path, "rb");
    if (f == NULL) {
        fprintf(stderr, "collision_map_load: cannot open %s\n", path);
        return NULL;
    }

    uint32_t magic, version, region_count;
    if (fread(&magic, 4, 1, f) != 1 || magic != COLLISION_MAP_MAGIC) {
        fprintf(stderr, "collision_map_load: bad magic in %s\n", path);
        fclose(f);
        return NULL;
    }
    if (fread(&version, 4, 1, f) != 1 || version != COLLISION_MAP_VERSION) {
        fprintf(stderr, "collision_map_load: unsupported version %u in %s\n", version, path);
        fclose(f);
        return NULL;
    }
    if (fread(&region_count, 4, 1, f) != 1) {
        fprintf(stderr, "collision_map_load: truncated header in %s\n", path);
        fclose(f);
        return NULL;
    }

    CollisionMap* map = collision_map_create();

    for (uint32_t i = 0; i < region_count; i++) {
        int32_t key;
        if (fread(&key, 4, 1, f) != 1) {
            fprintf(stderr, "collision_map_load: truncated at region %u in %s\n", i, path);
            collision_map_free(map);
            fclose(f);
            return NULL;
        }

        CollisionRegion* region = (CollisionRegion*)calloc(1, sizeof(CollisionRegion));
        size_t flags_size = sizeof(region->flags);
        if (fread(region->flags, 1, flags_size, f) != flags_size) {
            fprintf(stderr, "collision_map_load: truncated flags at region %u in %s\n", i, path);
            free(region);
            collision_map_free(map);
            fclose(f);
            return NULL;
        }

        collision_map_put(map, key, region);
    }

    fclose(f);
    return map;
}

#define LOS_FULL_MASK   0x20000
#define LOS_EAST_MASK   0x01000
#define LOS_WEST_MASK   0x10000
#define LOS_NORTH_MASK  0x00400
#define LOS_SOUTH_MASK  0x04000
#define LOS_FP_SCALE    65536
#define LOS_FP_HALF     32768

typedef struct {
    int x, y;
    int size;
    uint32_t los_mask;
} LOSBlocker;
static inline int los_aabb_overlap(
    int x1, int y1, int s1, int x2, int y2, int s2
) {
    return !(x1 >= x2 + s2 || x1 + s1 <= x2 ||
        y1 >= y2 + s2 || y1 + s1 <= y2);
}


typedef uint32_t (*los_tile_flags_fn)(void* ctx, int x, int y);
typedef int (*los_tile_blocked_fn)(void* ctx, int x, int y);

static inline int los_tile_ray_clear(
    los_tile_blocked_fn tile_blocked,
    void* tile_ctx,
    int x0,
    int y0,
    int x1,
    int y1
) {
    int dx = x1 - x0;
    int dy = y1 - y0;
    int adx = dx < 0 ? -dx : dx;
    int ady = dy < 0 ? -dy : dy;
    if (adx == 0 && ady == 0) return 1;
    if (tile_blocked(tile_ctx, x1, y1)) return 0;

    if (adx > ady) {
        int x = x0;
        int y_fp = y0 * LOS_FP_SCALE + LOS_FP_HALF;
        int slope = (dy * LOS_FP_SCALE) / adx;
        int x_inc = dx > 0 ? 1 : -1;
        if (dy < 0) y_fp--;
        while (x != x1) {
            x += x_inc;
            int y = y_fp >> 16;
            if (tile_blocked(tile_ctx, x, y)) return 0;
            y_fp += slope;
            int new_y = y_fp >> 16;
            if (new_y != y && tile_blocked(tile_ctx, x, new_y)) return 0;
        }
    } else {
        int y = y0;
        int x_fp = x0 * LOS_FP_SCALE + LOS_FP_HALF;
        int slope = (dx * LOS_FP_SCALE) / ady;
        int y_inc = dy > 0 ? 1 : -1;
        if (dx < 0) x_fp--;
        while (y != y1) {
            y += y_inc;
            int x = x_fp >> 16;
            if (tile_blocked(tile_ctx, x, y)) return 0;
            x_fp += slope;
            int new_x = x_fp >> 16;
            if (new_x != x && tile_blocked(tile_ctx, new_x, y)) return 0;
        }
    }
    return 1;
}

typedef struct {
    const LOSBlocker* blockers;
    int count;
} LOSBlockerFlagsContext;

static inline uint32_t los_check_tile(
    const LOSBlocker* blockers, int count, int px, int py
) {
    for (int i = 0; i < count; i++) {
        const LOSBlocker* b = &blockers[i];
        if (px >= b->x && px < b->x + b->size &&
                py >= b->y && py < b->y + b->size)
            return b->los_mask;
    }
    return 0;
}

static inline uint32_t los_blocker_tile_flags(void* ctx, int x, int y) {
    const LOSBlockerFlagsContext* blockers =
        (const LOSBlockerFlagsContext*)ctx;
    return los_check_tile(blockers->blockers, blockers->count, x, y);
}

static inline int los_has_line_of_sight_with_flags(
    los_tile_flags_fn tile_flags,
    void* tile_flags_ctx,
    int x1,
    int y1,
    int x2,
    int y2,
    int src_size,
    int range
) {
    int dx = x2 - x1;
    int dy = y2 - y1;

    if (tile_flags(tile_flags_ctx, x1, y1)) return 0;
    if (tile_flags(tile_flags_ctx, x2, y2)) return 0;
    if (los_aabb_overlap(x1, y1, src_size, x2, y2, 1)) return 0;

    int adx = dx < 0 ? -dx : dx;
    int ady = dy < 0 ? -dy : dy;
    if (range > 0 && (adx > range || ady > range)) return 0;

    if (adx > ady) {
        int x_tile = x1;
        int y_fp = y1 * LOS_FP_SCALE + LOS_FP_HALF;
        int slope = (dy * LOS_FP_SCALE) / adx;
        int x_inc = (dx > 0) ? 1 : -1;
        uint32_t x_mask = (dx > 0) ? (LOS_WEST_MASK | LOS_FULL_MASK)
                                    : (LOS_EAST_MASK | LOS_FULL_MASK);
        uint32_t y_mask = (dy < 0) ? (LOS_NORTH_MASK | LOS_FULL_MASK)
                                    : (LOS_SOUTH_MASK | LOS_FULL_MASK);
        if (dy < 0) y_fp -= 1;

        while (x_tile != x2) {
            x_tile += x_inc;
            int y_tile = y_fp >> 16;
            if (tile_flags(tile_flags_ctx, x_tile, y_tile) & x_mask)
                return 0;
            y_fp += slope;
            int new_y = y_fp >> 16;
            if (new_y != y_tile &&
                    (tile_flags(tile_flags_ctx, x_tile, new_y) & y_mask))
                return 0;
        }
    } else if (ady > 0) {
        int y_tile = y1;
        int x_fp = x1 * LOS_FP_SCALE + LOS_FP_HALF;
        int slope = (dx * LOS_FP_SCALE) / ady;
        int y_inc = (dy > 0) ? 1 : -1;
        uint32_t y_mask = (dy > 0) ? (LOS_SOUTH_MASK | LOS_FULL_MASK)
                                    : (LOS_NORTH_MASK | LOS_FULL_MASK);
        uint32_t x_mask = (dx < 0) ? (LOS_EAST_MASK | LOS_FULL_MASK)
                                    : (LOS_WEST_MASK | LOS_FULL_MASK);
        if (dx < 0) x_fp -= 1;

        while (y_tile != y2) {
            y_tile += y_inc;
            int x_tile = x_fp >> 16;
            if (tile_flags(tile_flags_ctx, x_tile, y_tile) & y_mask)
                return 0;
            x_fp += slope;
            int new_x = x_fp >> 16;
            if (new_x != x_tile &&
                    (tile_flags(tile_flags_ctx, new_x, y_tile) & x_mask))
                return 0;
        }
    }

    return 1;
}


static inline int los_intervals_overlap(int a0, int a1, int b0, int b1) {
    return !(a1 < b0 || b1 < a0);
}
static inline int entity_has_line_of_sight_with_flags(
    los_tile_flags_fn tile_flags,
    void* tile_flags_ctx,
    int ax,
    int ay,
    int a_size,
    int tx,
    int ty,
    int t_size,
    int range);

static inline int entity_has_line_of_sight(
    const LOSBlocker* blockers,
    int blocker_count,
    int ax,
    int ay,
    int a_size,
    int tx,
    int ty,
    int t_size,
    int range
) {
    LOSBlockerFlagsContext ctx = {blockers, blocker_count};
    return entity_has_line_of_sight_with_flags(
        los_blocker_tile_flags,
        &ctx,
        ax,
        ay,
        a_size,
        tx,
        ty,
        t_size,
        range);
}

static inline int entity_has_line_of_sight_with_flags(
    los_tile_flags_fn tile_flags,
    void* tile_flags_ctx,
    int ax,
    int ay,
    int a_size,
    int tx,
    int ty,
    int t_size,
    int range
) {
    if (range == 1) {
        if (los_aabb_overlap(ax, ay, a_size, tx, ty, t_size)) return 0;

        int a_x1 = ax + a_size - 1;
        int a_y1 = ay + a_size - 1;
        int t_x1 = tx + t_size - 1;
        int t_y1 = ty + t_size - 1;

        return (a_x1 + 1 == tx &&
                    los_intervals_overlap(ay, a_y1, ty, t_y1)) ||
            (t_x1 + 1 == ax &&
                    los_intervals_overlap(ay, a_y1, ty, t_y1)) ||
            (a_y1 + 1 == ty &&
                    los_intervals_overlap(ax, a_x1, tx, t_x1)) ||
            (t_y1 + 1 == ay &&
                    los_intervals_overlap(ax, a_x1, tx, t_x1));
    }

    int a_px = tx;
    if (a_px < ax) a_px = ax;
    if (a_px >= ax + a_size) a_px = ax + a_size - 1;
    int a_py = ty;
    if (a_py < ay) a_py = ay;
    if (a_py >= ay + a_size) a_py = ay + a_size - 1;

    int t_px = ax;
    if (t_px < tx) t_px = tx;
    if (t_px >= tx + t_size) t_px = tx + t_size - 1;
    int t_py = ay;
    if (t_py < ty) t_py = ty;
    if (t_py >= ty + t_size) t_py = ty + t_size - 1;

    return los_has_line_of_sight_with_flags(
        tile_flags,
        tile_flags_ctx,
        a_px,
        a_py,
        t_px,
        t_py,
        1,
        range);
}

#define ENCOUNTER_ARENA_TOPOLOGY_MAX_DIMENSION 64
#define ENCOUNTER_ARENA_TOPOLOGY_MAX_TILES \
    (ENCOUNTER_ARENA_TOPOLOGY_MAX_DIMENSION * \
     ENCOUNTER_ARENA_TOPOLOGY_MAX_DIMENSION)
#define ENCOUNTER_ARENA_TOPOLOGY_MAX_FOOTPRINT_SIZE 7
#define ENCOUNTER_ARENA_TOPOLOGY_LOS_WORDS \
    ((ENCOUNTER_ARENA_TOPOLOGY_MAX_TILES * \
      ENCOUNTER_ARENA_TOPOLOGY_MAX_TILES + 63) / 64)

typedef uint32_t (*encounter_arena_tile_flags_fn)(void* ctx, int x, int y);
typedef enum {
    ENCOUNTER_ARENA_TOPOLOGY_LOS_BUILD_FLAGGED = 0,
    ENCOUNTER_ARENA_TOPOLOGY_LOS_BUILD_TILE_BLOCKED,
    ENCOUNTER_ARENA_TOPOLOGY_LOS_BUILD_OPEN,
} EncounterArenaTopologyLosBuildMode;


typedef struct {
    int origin_x;
    int origin_y;
    int width;
    int height;
    int max_footprint_size;
    uint64_t revision;
    encounter_arena_tile_flags_fn tile_flags;
    void* tile_flags_ctx;
    encounter_arena_tile_flags_fn los_tile_flags;
    void* los_tile_flags_ctx;
    EncounterArenaTopologyLosBuildMode los_build_mode;
} EncounterArenaTopologyBuildSpec;
typedef enum {
    ENCOUNTER_ARENA_TOPOLOGY_LOS_OPEN = 0,
    ENCOUNTER_ARENA_TOPOLOGY_LOS_FLAGGED,
    ENCOUNTER_ARENA_TOPOLOGY_LOS_TILE_BLOCKED,
} EncounterArenaTopologyLosMode;


typedef struct EncounterArenaTopology {
    int origin_x;
    int origin_y;
    int width;
    int height;
    int tile_count;
    int max_footprint_size;
    uint64_t revision;
    uint8_t finalized;
    EncounterArenaTopologyLosMode static_los_mode;
    EncounterArenaTopologyLosBuildMode los_build_mode;
    uint32_t static_collision_flags[ENCOUNTER_ARENA_TOPOLOGY_MAX_TILES];
    uint8_t static_blocked[ENCOUNTER_ARENA_TOPOLOGY_MAX_TILES];
    uint8_t footprint_blocked
        [ENCOUNTER_ARENA_TOPOLOGY_MAX_FOOTPRINT_SIZE]
        [ENCOUNTER_ARENA_TOPOLOGY_MAX_TILES];
    uint8_t legal_step_masks
        [ENCOUNTER_ARENA_TOPOLOGY_MAX_FOOTPRINT_SIZE]
        [ENCOUNTER_ARENA_TOPOLOGY_MAX_TILES];
    uint64_t static_los_bits[ENCOUNTER_ARENA_TOPOLOGY_LOS_WORDS];
    uint32_t nearby_unit_footprint_masks
        [ENCOUNTER_ARENA_TOPOLOGY_MAX_TILES];
} EncounterArenaTopology;

static inline void encounter_arena_topology_abort(
    const char* reason,
    int value
) {
    fprintf(stderr, "invalid OSRS arena topology %s: %d\n", reason, value);
    abort();
}

static inline int encounter_arena_topology_contains_raw(
    const EncounterArenaTopology* topology,
    int x,
    int y
) {
    int64_t local_x = (int64_t)x - topology->origin_x;
    int64_t local_y = (int64_t)y - topology->origin_y;
    return local_x >= 0 && local_x < topology->width &&
        local_y >= 0 && local_y < topology->height;
}

static inline int encounter_arena_topology_index_raw(
    const EncounterArenaTopology* topology,
    int x,
    int y
) {
    int64_t local_x = (int64_t)x - topology->origin_x;
    int64_t local_y = (int64_t)y - topology->origin_y;
    return (int)(local_x * topology->height + local_y);
}

static inline uint32_t encounter_arena_topology_build_flags(
    const EncounterArenaTopologyBuildSpec* spec,
    int x,
    int y
) {
    if (!spec->tile_flags) return 0;
    return spec->tile_flags(spec->tile_flags_ctx, x, y);
}

static inline int encounter_arena_topology_footprint_blocked_raw(
    const EncounterArenaTopology* topology,
    int x,
    int y,
    int size
) {
    int64_t local_x = (int64_t)x - topology->origin_x;
    int64_t local_y = (int64_t)y - topology->origin_y;
    if (local_x < 0 || local_y < 0 ||
            local_x + size > topology->width ||
            local_y + size > topology->height)
        return 1;
    int index = (int)(local_x * topology->height + local_y);
    return topology->footprint_blocked[size - 1][index] != 0;
}

static inline int encounter_arena_topology_build_step_allowed(
    const EncounterArenaTopologyBuildSpec* spec,
    const EncounterArenaTopology* topology,
    int x,
    int y,
    int size,
    int dx,
    int dy
) {
    if (encounter_arena_topology_footprint_blocked_raw(
            topology, x, y, size))
        return 0;
    int64_t destination_x = (int64_t)x + dx;
    int64_t destination_y = (int64_t)y + dy;
    int64_t destination_local_x = destination_x - topology->origin_x;
    int64_t destination_local_y = destination_y - topology->origin_y;
    if (destination_local_x < 0 || destination_local_y < 0 ||
            destination_local_x + size > topology->width ||
            destination_local_y + size > topology->height)
        return 0;
    int destination_x_int = (int)destination_x;
    int destination_y_int = (int)destination_y;
    if (encounter_arena_topology_footprint_blocked_raw(
            topology, destination_x_int, destination_y_int, size))
        return 0;

    if (dx != 0) {
        int leading_x = dx > 0 ? x + size - 1 : x;
        for (int offset = 0; offset < size; offset++) {
            int source_y = y + offset;
            if (!collision_flags_traversable_step(
                    encounter_arena_topology_build_flags(
                        spec, leading_x + dx, source_y),
                    encounter_arena_topology_build_flags(
                        spec, leading_x + dx, source_y),
                    encounter_arena_topology_build_flags(
                        spec, leading_x, source_y),
                    dx,
                    0))
                return 0;
        }
    }

    if (dy != 0) {
        int leading_y = dy > 0 ? y + size - 1 : y;
        for (int offset = 0; offset < size; offset++) {
            int source_x = x + offset;
            if (!collision_flags_traversable_step(
                    encounter_arena_topology_build_flags(
                        spec, source_x, leading_y + dy),
                    encounter_arena_topology_build_flags(
                        spec, source_x, leading_y),
                    encounter_arena_topology_build_flags(
                        spec, source_x, leading_y + dy),
                    0,
                    dy))
                return 0;
        }
    }

    if (dx != 0 && dy != 0) {
        int corner_x = dx > 0 ? x + size - 1 : x;
        int corner_y = dy > 0 ? y + size - 1 : y;
        if (!collision_flags_traversable_step(
                encounter_arena_topology_build_flags(
                    spec, corner_x + dx, corner_y + dy),
                encounter_arena_topology_build_flags(
                    spec, corner_x + dx, corner_y),
                encounter_arena_topology_build_flags(
                    spec, corner_x, corner_y + dy),
                dx,
                dy))
            return 0;
    }

    return 1;
}

typedef struct {
    const EncounterArenaTopology* topology;
    const EncounterArenaTopologyBuildSpec* spec;
} EncounterArenaTopologyLosBuildContext;

static uint32_t encounter_arena_topology_los_flags(
    void* data,
    int x,
    int y
) {
    const EncounterArenaTopologyLosBuildContext* build =
        (const EncounterArenaTopologyLosBuildContext*)data;
    if (!encounter_arena_topology_contains_raw(build->topology, x, y))
        return LOS_FULL_MASK;
    encounter_arena_tile_flags_fn tile_flags =
        build->spec->los_tile_flags
            ? build->spec->los_tile_flags
            : build->spec->tile_flags;
    void* tile_flags_ctx = build->spec->los_tile_flags
        ? build->spec->los_tile_flags_ctx
        : build->spec->tile_flags_ctx;
    uint32_t flags = tile_flags ? tile_flags(tile_flags_ctx, x, y) : 0;
    if (flags & COLLISION_BLOCKED) return LOS_FULL_MASK;
    return flags &
        (LOS_FULL_MASK | LOS_EAST_MASK | LOS_WEST_MASK |
         LOS_NORTH_MASK | LOS_SOUTH_MASK);
}
static int encounter_arena_topology_los_tile_blocked(
    void* data,
    int x,
    int y
) {
    return encounter_arena_topology_los_flags(data, x, y) != 0;
}


static inline void encounter_arena_topology_set_los(
    EncounterArenaTopology* topology,
    int source_index,
    int target_index
) {
    size_t bit_index =
        (size_t)source_index * (size_t)topology->tile_count +
        (size_t)target_index;
    topology->static_los_bits[bit_index >> 6] |=
        UINT64_C(1) << (bit_index & 63);
}

static inline void encounter_arena_topology_clear_los(
    EncounterArenaTopology* topology,
    int source_index,
    int target_index
) {
    size_t bit_index =
        (size_t)source_index * (size_t)topology->tile_count +
        (size_t)target_index;
    topology->static_los_bits[bit_index >> 6] &=
        ~(UINT64_C(1) << (bit_index & 63));
}


static inline uint32_t encounter_arena_topology_los_grid_at(
    const uint32_t* flags,
    int origin_x,
    int origin_y,
    int width,
    int height,
    int x,
    int y
) {
    int64_t local_x = (int64_t)x - origin_x;
    int64_t local_y = (int64_t)y - origin_y;
    if (local_x < 0 || local_y < 0 ||
            local_x >= width || local_y >= height)
        return LOS_FULL_MASK;
    return flags[(int)(local_x * height + local_y)];
}

static inline uint32_t los_grid_local(
    const uint32_t* flags,
    int width,
    int height,
    int x,
    int y
) {
    if ((unsigned)x >= (unsigned)width ||
            (unsigned)y >= (unsigned)height)
        return LOS_FULL_MASK;
    return flags[x * height + y];
}

static inline int los_tile_ray_clear_grid_clipped(
    const uint32_t* flags,
    int origin_x,
    int origin_y,
    int width,
    int height,
    int x0,
    int y0,
    int x1,
    int y1,
    int cx0,
    int cy0,
    int cx1,
    int cy1
) {
    x0 -= origin_x;
    y0 -= origin_y;
    x1 -= origin_x;
    y1 -= origin_y;
    int dx = x1 - x0;
    int dy = y1 - y0;
    int adx = dx < 0 ? -dx : dx;
    int ady = dy < 0 ? -dy : dy;
    if (adx == 0 && ady == 0) return 1;
    if (los_grid_local(flags, width, height, x1, y1))
        return 0;
    if (cx0 > 0) cx0--;
    if (cy0 > 0) cy0--;
    if (cx1 < width - 1) cx1++;
    if (cy1 < height - 1) cy1++;

    if (adx > ady) {
        int x = x0;
        int y_fp = y0 * LOS_FP_SCALE + LOS_FP_HALF;
        int slope = (dy * LOS_FP_SCALE) / adx;
        int x_inc = dx > 0 ? 1 : -1;
        if (dy < 0) y_fp--;
        int n_pre = x_inc > 0 ? (cx0 - x0 - 1) : (x0 - cx1 - 1);
        if (n_pre < 0) n_pre = 0;
        if (n_pre > adx) n_pre = adx;
        x += n_pre * x_inc;
        y_fp += n_pre * slope;
        while (x != x1) {
            x += x_inc;
            int y = y_fp >> 16;
            if (los_grid_local(flags, width, height, x, y))
                return 0;
            y_fp += slope;
            int new_y = y_fp >> 16;
            if (new_y != y &&
                    los_grid_local(flags, width, height, x, new_y))
                return 0;
            if ((x_inc > 0 && x >= cx1) || (x_inc < 0 && x <= cx0))
                break;
        }
    } else {
        int y = y0;
        int x_fp = x0 * LOS_FP_SCALE + LOS_FP_HALF;
        int slope = (dx * LOS_FP_SCALE) / ady;
        int y_inc = dy > 0 ? 1 : -1;
        if (dx < 0) x_fp--;
        int n_pre = y_inc > 0 ? (cy0 - y0 - 1) : (y0 - cy1 - 1);
        if (n_pre < 0) n_pre = 0;
        if (n_pre > ady) n_pre = ady;
        y += n_pre * y_inc;
        x_fp += n_pre * slope;
        while (y != y1) {
            y += y_inc;
            int x = x_fp >> 16;
            if (los_grid_local(flags, width, height, x, y))
                return 0;
            x_fp += slope;
            int new_x = x_fp >> 16;
            if (new_x != x &&
                    los_grid_local(flags, width, height, new_x, y))
                return 0;
            if ((y_inc > 0 && y >= cy1) || (y_inc < 0 && y <= cy0))
                break;
        }
    }
    return 1;
}

static inline int los_tile_ray_clear_grid(
    const uint32_t* flags,
    int origin_x,
    int origin_y,
    int width,
    int height,
    int x0,
    int y0,
    int x1,
    int y1
) {
    return los_tile_ray_clear_grid_clipped(
        flags, origin_x, origin_y, width, height,
        x0, y0, x1, y1, 0, 0, width - 1, height - 1);
}

static inline int los_occ_row_blocked(
    const uint64_t* rows,
    int width,
    int height,
    int x,
    int y
) {
    if ((unsigned)x >= (unsigned)width ||
            (unsigned)y >= (unsigned)height)
        return 1;
    return (int)((rows[y] >> x) & 1u);
}

static inline int los_segment_hits_closed_aabb(
    int x0,
    int y0,
    int x1,
    int y1,
    int xmin,
    int ymin,
    int xmax,
    int ymax
) {
    double dx = (double)(x1 - x0);
    double dy = (double)(y1 - y0);
    double t0 = 0.0;
    double t1 = 1.0;
    double p[4];
    double q[4];
    p[0] = -dx;
    p[1] = dx;
    p[2] = -dy;
    p[3] = dy;
    q[0] = (double)(x0 - xmin);
    q[1] = (double)(xmax - x0);
    q[2] = (double)(y0 - ymin);
    q[3] = (double)(ymax - y0);
    for (int i = 0; i < 4; i++) {
        if (p[i] == 0.0) {
            if (q[i] < 0.0) return 0;
        } else {
            double t = q[i] / p[i];
            if (p[i] < 0.0) {
                if (t > t0) t0 = t;
            } else {
                if (t < t1) t1 = t;
            }
            if (t0 > t1) return 0;
        }
    }
    return 1;
}


static inline int los_tile_ray_clear_occ_rows(
    const uint64_t* rows,
    int width,
    int height,
    int x0,
    int y0,
    int x1,
    int y1,
    int cx0,
    int cy0,
    int cx1,
    int cy1
) {
    int dx = x1 - x0;
    int dy = y1 - y0;
    int adx = dx < 0 ? -dx : dx;
    int ady = dy < 0 ? -dy : dy;
    if (adx == 0 && ady == 0) return 1;
    if (los_occ_row_blocked(rows, width, height, x1, y1))
        return 0;
    if (cx0 > 0) cx0--;
    if (cy0 > 0) cy0--;
    if (cx1 < width - 1) cx1++;
    if (cy1 < height - 1) cy1++;
    if (!los_segment_hits_closed_aabb(
            x0, y0, x1, y1, cx0, cy0, cx1, cy1))
        return 1;

    if (adx > ady) {
        int x = x0;
        int y_fp = y0 * LOS_FP_SCALE + LOS_FP_HALF;
        int slope = (dy * LOS_FP_SCALE) / adx;
        int x_inc = dx > 0 ? 1 : -1;
        if (dy < 0) y_fp--;
        int n_pre = x_inc > 0 ? (cx0 - x0 - 1) : (x0 - cx1 - 1);
        if (n_pre < 0) n_pre = 0;
        if (n_pre > adx) n_pre = adx;
        x += n_pre * x_inc;
        y_fp += n_pre * slope;
        while (x != x1) {
            x += x_inc;
            int y = y_fp >> 16;
            if (los_occ_row_blocked(rows, width, height, x, y))
                return 0;
            y_fp += slope;
            int new_y = y_fp >> 16;
            if (new_y != y &&
                    los_occ_row_blocked(rows, width, height, x, new_y))
                return 0;
            if ((x_inc > 0 && x >= cx1) || (x_inc < 0 && x <= cx0))
                break;
        }
    } else {
        int y = y0;
        int x_fp = x0 * LOS_FP_SCALE + LOS_FP_HALF;
        int slope = (dx * LOS_FP_SCALE) / ady;
        int y_inc = dy > 0 ? 1 : -1;
        if (dx < 0) x_fp--;
        int n_pre = y_inc > 0 ? (cy0 - y0 - 1) : (y0 - cy1 - 1);
        if (n_pre < 0) n_pre = 0;
        if (n_pre > ady) n_pre = ady;
        y += n_pre * y_inc;
        x_fp += n_pre * slope;
        while (y != y1) {
            y += y_inc;
            int x = x_fp >> 16;
            if (los_occ_row_blocked(rows, width, height, x, y))
                return 0;
            x_fp += slope;
            int new_x = x_fp >> 16;
            if (new_x != x &&
                    los_occ_row_blocked(rows, width, height, new_x, y))
                return 0;
            if ((y_inc > 0 && y >= cy1) || (y_inc < 0 && y <= cy0))
                break;
        }
    }
    return 1;
}



static inline int los_has_line_of_sight_grid(
    const uint32_t* flags,
    int origin_x,
    int origin_y,
    int width,
    int height,
    int x1,
    int y1,
    int x2,
    int y2
) {
    int dx = x2 - x1;
    int dy = y2 - y1;

    if (encounter_arena_topology_los_grid_at(
            flags, origin_x, origin_y, width, height, x1, y1))
        return 0;
    if (encounter_arena_topology_los_grid_at(
            flags, origin_x, origin_y, width, height, x2, y2))
        return 0;
    if (los_aabb_overlap(x1, y1, 1, x2, y2, 1)) return 0;

    int adx = dx < 0 ? -dx : dx;
    int ady = dy < 0 ? -dy : dy;

    if (adx > ady) {
        int x_tile = x1;
        int y_fp = y1 * LOS_FP_SCALE + LOS_FP_HALF;
        int slope = (dy * LOS_FP_SCALE) / adx;
        int x_inc = (dx > 0) ? 1 : -1;
        uint32_t x_mask = (dx > 0) ? (LOS_WEST_MASK | LOS_FULL_MASK)
                                    : (LOS_EAST_MASK | LOS_FULL_MASK);
        uint32_t y_mask = (dy < 0) ? (LOS_NORTH_MASK | LOS_FULL_MASK)
                                    : (LOS_SOUTH_MASK | LOS_FULL_MASK);
        if (dy < 0) y_fp -= 1;

        while (x_tile != x2) {
            x_tile += x_inc;
            int y_tile = y_fp >> 16;
            if (encounter_arena_topology_los_grid_at(
                    flags, origin_x, origin_y, width, height,
                    x_tile, y_tile) & x_mask)
                return 0;
            y_fp += slope;
            int new_y = y_fp >> 16;
            if (new_y != y_tile &&
                    (encounter_arena_topology_los_grid_at(
                        flags, origin_x, origin_y, width, height,
                        x_tile, new_y) & y_mask))
                return 0;
        }
    } else if (ady > 0) {
        int y_tile = y1;
        int x_fp = x1 * LOS_FP_SCALE + LOS_FP_HALF;
        int slope = (dx * LOS_FP_SCALE) / ady;
        int y_inc = (dy > 0) ? 1 : -1;
        uint32_t y_mask = (dy > 0) ? (LOS_SOUTH_MASK | LOS_FULL_MASK)
                                    : (LOS_NORTH_MASK | LOS_FULL_MASK);
        uint32_t x_mask = (dx < 0) ? (LOS_EAST_MASK | LOS_FULL_MASK)
                                    : (LOS_WEST_MASK | LOS_FULL_MASK);
        if (dx < 0) x_fp -= 1;

        while (y_tile != y2) {
            y_tile += y_inc;
            int x_tile = x_fp >> 16;
            if (encounter_arena_topology_los_grid_at(
                    flags, origin_x, origin_y, width, height,
                    x_tile, y_tile) & y_mask)
                return 0;
            x_fp += slope;
            int new_x = x_fp >> 16;
            if (new_x != x_tile &&
                    (encounter_arena_topology_los_grid_at(
                        flags, origin_x, origin_y, width, height,
                        new_x, y_tile) & x_mask))
                return 0;
        }
    }

    return 1;
}

static inline int encounter_arena_topology_local_aabb_blocked(
    const uint16_t prefix
        [ENCOUNTER_ARENA_TOPOLOGY_MAX_DIMENSION + 1]
        [ENCOUNTER_ARENA_TOPOLOGY_MAX_DIMENSION + 1],
    int ax,
    int ay,
    int bx,
    int by
) {
    int min_x = ax < bx ? ax : bx;
    int max_x = ax < bx ? bx : ax;
    int min_y = ay < by ? ay : by;
    int max_y = ay < by ? by : ay;
    return (prefix[max_x + 1][max_y + 1]
        - prefix[min_x][max_y + 1]
        - prefix[max_x + 1][min_y]
        + prefix[min_x][min_y]) != 0;
}



static inline EncounterArenaTopology* encounter_arena_topology_build(
    const EncounterArenaTopologyBuildSpec* spec
) {
    if (!spec) encounter_arena_topology_abort("build spec", 0);
    if (spec->width < 1 ||
            spec->width > ENCOUNTER_ARENA_TOPOLOGY_MAX_DIMENSION)
        encounter_arena_topology_abort("width", spec->width);
    if (spec->height < 1 ||
            spec->height > ENCOUNTER_ARENA_TOPOLOGY_MAX_DIMENSION)
        encounter_arena_topology_abort("height", spec->height);
    if (spec->origin_x > INT_MAX - (spec->width - 1))
        encounter_arena_topology_abort("origin x", spec->origin_x);
    if (spec->origin_y > INT_MAX - (spec->height - 1))
        encounter_arena_topology_abort("origin y", spec->origin_y);
    if (spec->max_footprint_size < 1 ||
            spec->max_footprint_size >
                ENCOUNTER_ARENA_TOPOLOGY_MAX_FOOTPRINT_SIZE)
        encounter_arena_topology_abort(
            "footprint size", spec->max_footprint_size);
    if (spec->revision == 0)
        encounter_arena_topology_abort("revision", 0);
    if (spec->los_build_mode <
            ENCOUNTER_ARENA_TOPOLOGY_LOS_BUILD_FLAGGED ||
            spec->los_build_mode >
                ENCOUNTER_ARENA_TOPOLOGY_LOS_BUILD_OPEN)
        encounter_arena_topology_abort(
            "LOS build mode", spec->los_build_mode);

    EncounterArenaTopology* topology =
        (EncounterArenaTopology*)calloc(1, sizeof(*topology));
    if (!topology) {
        fprintf(stderr, "failed to allocate OSRS arena topology\n");
        abort();
    }

    topology->origin_x = spec->origin_x;
    topology->origin_y = spec->origin_y;
    topology->width = spec->width;
    topology->height = spec->height;
    topology->tile_count = spec->width * spec->height;
    topology->max_footprint_size = spec->max_footprint_size;
    topology->revision = spec->revision;
    topology->los_build_mode = spec->los_build_mode;

    for (int local_x = 0; local_x < topology->width; local_x++) {
        for (int local_y = 0; local_y < topology->height; local_y++) {
            int x = topology->origin_x + local_x;
            int y = topology->origin_y + local_y;
            int index = encounter_arena_topology_index_raw(topology, x, y);
            uint32_t flags =
                encounter_arena_topology_build_flags(spec, x, y);
            topology->static_collision_flags[index] = flags;
            topology->static_blocked[index] =
                (uint8_t)((flags & COLLISION_BLOCKED) != 0);
        }
    }

    for (int size = 1; size <= topology->max_footprint_size; size++) {
        for (int local_x = 0; local_x < topology->width; local_x++) {
            for (int local_y = 0; local_y < topology->height; local_y++) {
                int x = topology->origin_x + local_x;
                int y = topology->origin_y + local_y;
                int index =
                    encounter_arena_topology_index_raw(topology, x, y);
                int blocked =
                    local_x + size > topology->width ||
                    local_y + size > topology->height;
                for (int footprint_x = 0;
                        footprint_x < size && !blocked;
                        footprint_x++) {
                    for (int footprint_y = 0;
                            footprint_y < size;
                            footprint_y++) {
                        int footprint_index =
                            encounter_arena_topology_index_raw(
                                topology,
                                x + footprint_x,
                                y + footprint_y);
                        if (topology->static_blocked[footprint_index]) {
                            blocked = 1;
                            break;
                        }
                    }
                }
                topology->footprint_blocked[size - 1][index] =
                    (uint8_t)blocked;
            }
        }
    }
    for (int local_x = 0; local_x < topology->width; local_x++) {
        for (int local_y = 0; local_y < topology->height; local_y++) {
            int x = topology->origin_x + local_x;
            int y = topology->origin_y + local_y;
            int index = local_x * topology->height + local_y;
            uint32_t mask = 0;
            for (int dy = -2; dy <= 2; dy++) {
                for (int dx = -2; dx <= 2; dx++) {
                    if (dx == 0 && dy == 0) continue;
                    int bit = (dy + 2) * 5 + (dx + 2);
                    if (bit > 12) bit--;
                    if (!encounter_arena_topology_footprint_blocked_raw(
                            topology, x + dx, y + dy, 1))
                        mask |= UINT32_C(1) << bit;
                }
            }
            topology->nearby_unit_footprint_masks[index] = mask;
        }
    }


    for (int size = 1; size <= topology->max_footprint_size; size++) {
        for (int local_x = 0; local_x < topology->width; local_x++) {
            for (int local_y = 0; local_y < topology->height; local_y++) {
                int x = topology->origin_x + local_x;
                int y = topology->origin_y + local_y;
                int index =
                    encounter_arena_topology_index_raw(topology, x, y);
                uint8_t mask = 0;
                for (int dy = -1; dy <= 1; dy++) {
                    for (int dx = -1; dx <= 1; dx++) {
                        if (dx == 0 && dy == 0) continue;
                        int bit = (dy + 1) * 3 + (dx + 1);
                        if (bit > 4) bit--;
                        if (encounter_arena_topology_build_step_allowed(
                                spec, topology, x, y, size, dx, dy))
                            mask |= (uint8_t)(1u << bit);
                    }
                }
                topology->legal_step_masks[size - 1][index] = mask;
            }
        }
    }

    EncounterArenaTopologyLosBuildContext los_build = {
        .topology = topology,
        .spec = spec,
    };
    if (topology->los_build_mode ==
            ENCOUNTER_ARENA_TOPOLOGY_LOS_BUILD_OPEN) {
        topology->static_los_mode =
            ENCOUNTER_ARENA_TOPOLOGY_LOS_OPEN;
    } else {
        uint32_t los_flag_grid[ENCOUNTER_ARENA_TOPOLOGY_MAX_TILES];
        int flagged = 0;
        int occupancy_only = 1;
        for (int source = 0; source < topology->tile_count; source++) {
            int source_x =
                topology->origin_x + source / topology->height;
            int source_y =
                topology->origin_y + source % topology->height;
            uint32_t flags = encounter_arena_topology_los_flags(
                &los_build, source_x, source_y);
            los_flag_grid[source] = flags;
            if (flags) flagged = 1;
            if (flags & ~(uint32_t)LOS_FULL_MASK) occupancy_only = 0;
        }
        uint16_t blocked_prefix
            [ENCOUNTER_ARENA_TOPOLOGY_MAX_DIMENSION + 1]
            [ENCOUNTER_ARENA_TOPOLOGY_MAX_DIMENSION + 1];
        memset(blocked_prefix, 0, sizeof(blocked_prefix));
        for (int local_x = 0; local_x < topology->width; local_x++) {
            for (int local_y = 0; local_y < topology->height; local_y++) {
                int blocked = los_flag_grid[
                    local_x * topology->height + local_y] != 0;
                blocked_prefix[local_x + 1][local_y + 1] =
                    (uint16_t)(blocked_prefix[local_x][local_y + 1]
                        + blocked_prefix[local_x + 1][local_y]
                        - blocked_prefix[local_x][local_y]
                        + blocked);
            }
        }
        if (topology->los_build_mode ==
                ENCOUNTER_ARENA_TOPOLOGY_LOS_BUILD_TILE_BLOCKED) {
            topology->static_los_mode =
                ENCOUNTER_ARENA_TOPOLOGY_LOS_TILE_BLOCKED;
            for (int source = 0; source < topology->tile_count; source++) {
                int source_lx = source / topology->height;
                int source_ly = source % topology->height;
                int source_x = topology->origin_x + source_lx;
                int source_y = topology->origin_y + source_ly;
                for (int target = 0;
                        target < topology->tile_count;
                        target++) {
                    int target_lx = target / topology->height;
                    int target_ly = target % topology->height;
                    int target_x = topology->origin_x + target_lx;
                    int target_y = topology->origin_y + target_ly;
                    int clear;
                    if (!encounter_arena_topology_local_aabb_blocked(
                            blocked_prefix,
                            source_lx, source_ly,
                            target_lx, target_ly)) {
                        clear = 1;
                    } else {
                        clear = los_tile_ray_clear_grid(
                            los_flag_grid,
                            topology->origin_x,
                            topology->origin_y,
                            topology->width,
                            topology->height,
                            source_x,
                            source_y,
                            target_x,
                            target_y);
                    }
                    if (clear)
                        encounter_arena_topology_set_los(
                            topology, source, target);
                }
            }
        } else if (!flagged) {
            topology->static_los_mode =
                ENCOUNTER_ARENA_TOPOLOGY_LOS_OPEN;
        } else {
            topology->static_los_mode =
                ENCOUNTER_ARENA_TOPOLOGY_LOS_FLAGGED;
            int occ_x0 = topology->width;
            int occ_y0 = topology->height;
            int occ_x1 = -1;
            int occ_y1 = -1;
            uint64_t occ_row[ENCOUNTER_ARENA_TOPOLOGY_MAX_DIMENSION];
            memset(occ_row, 0, sizeof(occ_row));
            if (occupancy_only) {
                size_t los_bit_count =
                    (size_t)topology->tile_count *
                    (size_t)topology->tile_count;
                size_t los_words = (los_bit_count + 63) / 64;
                memset(
                    topology->static_los_bits,
                    0xff,
                    los_words * sizeof(topology->static_los_bits[0]));
                for (int i = 0; i < topology->tile_count; i++) {
                    if (!los_flag_grid[i]) continue;
                    int lx = i / topology->height;
                    int ly = i % topology->height;
                    if (lx < occ_x0) occ_x0 = lx;
                    if (ly < occ_y0) occ_y0 = ly;
                    if (lx > occ_x1) occ_x1 = lx;
                    if (ly > occ_y1) occ_y1 = ly;
                    occ_row[ly] |= UINT64_C(1) << lx;
                    for (int j = 0; j < topology->tile_count; j++) {
                        encounter_arena_topology_clear_los(topology, i, j);
                        encounter_arena_topology_clear_los(topology, j, i);
                    }
                }
            }


            for (int source = 0; source < topology->tile_count; source++) {
                if (los_flag_grid[source]) continue;
                int source_lx = source / topology->height;
                int source_ly = source % topology->height;
                int source_x = topology->origin_x + source_lx;
                int source_y = topology->origin_y + source_ly;
                if (!occupancy_only)
                    encounter_arena_topology_set_los(
                        topology, source, source);

                for (int target = source + 1;
                        target < topology->tile_count;
                        target++) {
                    if (los_flag_grid[target]) continue;
                    int target_lx = target / topology->height;
                    int target_ly = target % topology->height;
                    int target_x = topology->origin_x + target_lx;
                    int target_y = topology->origin_y + target_ly;
                    if (!encounter_arena_topology_local_aabb_blocked(
                            blocked_prefix,
                            source_lx, source_ly,
                            target_lx, target_ly)) {
                        if (!occupancy_only) {
                            encounter_arena_topology_set_los(
                                topology, source, target);
                            encounter_arena_topology_set_los(
                                topology, target, source);
                        }
                        continue;
                    }
                    if (occupancy_only) {
                        if (!los_tile_ray_clear_occ_rows(
                                occ_row,
                                topology->width,
                                topology->height,
                                source_lx,
                                source_ly,
                                target_lx,
                                target_ly,
                                occ_x0,
                                occ_y0,
                                occ_x1,
                                occ_y1))
                            encounter_arena_topology_clear_los(
                                topology, source, target);
                        if (!los_tile_ray_clear_occ_rows(
                                occ_row,
                                topology->width,
                                topology->height,
                                target_lx,
                                target_ly,
                                source_lx,
                                source_ly,
                                occ_x0,
                                occ_y0,
                                occ_x1,
                                occ_y1))
                            encounter_arena_topology_clear_los(
                                topology, target, source);
                    } else {
                        if (los_has_line_of_sight_grid(
                                los_flag_grid,
                                topology->origin_x,
                                topology->origin_y,
                                topology->width,
                                topology->height,
                                source_x,
                                source_y,
                                target_x,
                                target_y))
                            encounter_arena_topology_set_los(
                                topology, source, target);
                        if (los_has_line_of_sight_grid(
                                los_flag_grid,
                                topology->origin_x,
                                topology->origin_y,
                                topology->width,
                                topology->height,
                                target_x,
                                target_y,
                                source_x,
                                source_y))
                            encounter_arena_topology_set_los(
                                topology, target, source);
                    }
                }
            }
        }
    }

    return topology;
}

static inline void encounter_arena_topology_finalize(
    EncounterArenaTopology* topology
) {
    if (!topology) encounter_arena_topology_abort("finalize", 0);
    if (topology->finalized)
        encounter_arena_topology_abort("double finalization", 1);
    topology->finalized = 1;
}
static inline void encounter_arena_topology_require_spec(
    const EncounterArenaTopology* topology,
    const EncounterArenaTopologyBuildSpec* spec,
    const char* encounter_name
) {
    if (!topology || !topology->finalized)
        encounter_arena_topology_abort("unfinalized spec validation", 0);
    if (!spec ||
            topology->origin_x != spec->origin_x ||
            topology->origin_y != spec->origin_y ||
            topology->width != spec->width ||
            topology->height != spec->height ||
            topology->max_footprint_size != spec->max_footprint_size ||
            topology->revision != spec->revision ||
            topology->los_build_mode != spec->los_build_mode) {
        fprintf(stderr, "%s route topology spec changed\n", encounter_name);
        abort();
    }
    for (int local_x = 0; local_x < topology->width; local_x++) {
        for (int local_y = 0; local_y < topology->height; local_y++) {
            int x = topology->origin_x + local_x;
            int y = topology->origin_y + local_y;
            int index = local_x * topology->height + local_y;
            uint32_t flags = encounter_arena_topology_build_flags(spec, x, y);
            if (topology->static_collision_flags[index] != flags) {
                fprintf(stderr,
                    "%s route topology contents changed at (%d,%d)\n",
                    encounter_name,
                    x,
                    y);
                abort();
            }
        }
    }
}

static inline void encounter_arena_topology_require_finalized(
    const EncounterArenaTopology* topology
) {
    if (!topology || !topology->finalized)
        encounter_arena_topology_abort("unfinalized query", 0);
}

static inline const EncounterArenaTopology*
encounter_arena_topology_require_revision(
    const EncounterArenaTopology* topology,
    uint64_t revision
) {
    encounter_arena_topology_require_finalized(topology);
    if (revision == 0 || topology->revision != revision) {
        fprintf(stderr,
            "stale OSRS arena topology revision: expected %llu got %llu\n",
            (unsigned long long)topology->revision,
            (unsigned long long)revision);
        abort();
    }
    return topology;
}

static inline int encounter_arena_topology_contains(
    const EncounterArenaTopology* topology,
    int x,
    int y
) {
    encounter_arena_topology_require_finalized(topology);
    return encounter_arena_topology_contains_raw(topology, x, y);
}

static inline int encounter_arena_topology_tile_blocked(
    const EncounterArenaTopology* topology,
    int x,
    int y
) {
    encounter_arena_topology_require_finalized(topology);
    if (!encounter_arena_topology_contains_raw(topology, x, y)) return 1;
    return topology->static_blocked[
        encounter_arena_topology_index_raw(topology, x, y)] != 0;
}

static inline void encounter_arena_topology_require_footprint_size(
    const EncounterArenaTopology* topology,
    int size
) {
    if (size < 1 || size > topology->max_footprint_size)
        encounter_arena_topology_abort("query footprint size", size);
}

static inline int
encounter_arena_topology_footprint_blocked_assume_finalized_size_in_range(
    const EncounterArenaTopology* topology,
    int x,
    int y,
    int size
) {
    return encounter_arena_topology_footprint_blocked_raw(
        topology, x, y, size);
}
static inline int
encounter_arena_topology_footprint_in_bounds_assume_finalized_size_in_range(
    const EncounterArenaTopology* topology,
    int x,
    int y,
    int size
) {
    int64_t local_x = (int64_t)x - topology->origin_x;
    int64_t local_y = (int64_t)y - topology->origin_y;
    return local_x >= 0 && local_y >= 0 &&
        local_x + size <= topology->width &&
        local_y + size <= topology->height;
}


static inline int encounter_arena_topology_footprint_blocked(
    const EncounterArenaTopology* topology,
    int x,
    int y,
    int size
) {
    encounter_arena_topology_require_finalized(topology);
    encounter_arena_topology_require_footprint_size(topology, size);
    return
        encounter_arena_topology_footprint_blocked_assume_finalized_size_in_range(
            topology, x, y, size);
}

static inline uint32_t encounter_arena_topology_nearby_unit_footprint_mask(
    const EncounterArenaTopology* topology,
    int x,
    int y
) {
    encounter_arena_topology_require_finalized(topology);
    if (!encounter_arena_topology_contains_raw(topology, x, y)) return 0;
    int index = encounter_arena_topology_index_raw(topology, x, y);
    return topology->nearby_unit_footprint_masks[index];
}

static inline int
encounter_arena_topology_step_allowed_assume_finalized_size_in_range(
    const EncounterArenaTopology* topology,
    int x,
    int y,
    int size,
    int dx,
    int dy
) {
    if (!encounter_arena_topology_contains_raw(topology, x, y)) return 0;
    int bit = (dy + 1) * 3 + (dx + 1);
    if (bit > 4) bit--;
    int index = encounter_arena_topology_index_raw(topology, x, y);
    return (topology->legal_step_masks[size - 1][index] &
        (uint8_t)(1u << bit)) != 0;
}

static inline int encounter_arena_topology_step_allowed(
    const EncounterArenaTopology* topology,
    int x,
    int y,
    int size,
    int dx,
    int dy
) {
    encounter_arena_topology_require_finalized(topology);
    encounter_arena_topology_require_footprint_size(topology, size);
    if (dx < -1 || dx > 1 || dy < -1 || dy > 1 ||
            (dx == 0 && dy == 0))
        encounter_arena_topology_abort("step direction", 0);
    return encounter_arena_topology_step_allowed_assume_finalized_size_in_range(
        topology, x, y, size, dx, dy);
}

static inline int
encounter_arena_topology_los_clear_assume_finalized_footprints_in_bounds(
    const EncounterArenaTopology* topology,
    int actor_x,
    int actor_y,
    int actor_size,
    int target_x,
    int target_y,
    int target_size,
    int attack_range
) {
    int64_t actor_max_x = (int64_t)actor_x + actor_size - 1;
    int64_t actor_max_y = (int64_t)actor_y + actor_size - 1;
    int64_t target_max_x = (int64_t)target_x + target_size - 1;
    int64_t target_max_y = (int64_t)target_y + target_size - 1;
    int x_overlap =
        (int64_t)actor_x <= target_max_x &&
        (int64_t)target_x <= actor_max_x;
    int y_overlap =
        (int64_t)actor_y <= target_max_y &&
        (int64_t)target_y <= actor_max_y;

    if (attack_range == 1) {
        return (actor_max_x + 1 == target_x && y_overlap) ||
            (target_max_x + 1 == actor_x && y_overlap) ||
            (actor_max_y + 1 == target_y && x_overlap) ||
            (target_max_y + 1 == actor_y && x_overlap);
    }

    if (x_overlap && y_overlap &&
            !(topology->static_los_mode ==
                ENCOUNTER_ARENA_TOPOLOGY_LOS_TILE_BLOCKED &&
              attack_range == 0))
        return 0;

    int64_t dx = 0;
    int64_t dy = 0;
    if (actor_max_x < target_x) dx = (int64_t)target_x - actor_max_x;
    else if (target_max_x < actor_x) dx = (int64_t)actor_x - target_max_x;
    if (actor_max_y < target_y) dy = (int64_t)target_y - actor_max_y;
    else if (target_max_y < actor_y) dy = (int64_t)actor_y - target_max_y;
    if (attack_range > 0 && (dx > attack_range || dy > attack_range))
        return 0;
    if (topology->static_los_mode ==
            ENCOUNTER_ARENA_TOPOLOGY_LOS_OPEN)
        return 1;

    int64_t actor_los_x = target_x;
    if (actor_los_x < actor_x) actor_los_x = actor_x;
    if (actor_los_x > actor_max_x) actor_los_x = actor_max_x;
    int64_t actor_los_y = target_y;
    if (actor_los_y < actor_y) actor_los_y = actor_y;
    if (actor_los_y > actor_max_y) actor_los_y = actor_max_y;
    int64_t target_los_x = actor_x;
    if (target_los_x < target_x) target_los_x = target_x;
    if (target_los_x > target_max_x) target_los_x = target_max_x;
    int64_t target_los_y = actor_y;
    if (target_los_y < target_y) target_los_y = target_y;
    if (target_los_y > target_max_y) target_los_y = target_max_y;

    int actor_index = encounter_arena_topology_index_raw(
        topology, (int)actor_los_x, (int)actor_los_y);
    int target_index = encounter_arena_topology_index_raw(
        topology, (int)target_los_x, (int)target_los_y);
    size_t bit_index =
        (size_t)actor_index * (size_t)topology->tile_count +
        (size_t)target_index;
    return (int)((topology->static_los_bits[bit_index >> 6] >>
        (bit_index & 63)) & UINT64_C(1));
}

static inline int encounter_arena_topology_los_clear(
    const EncounterArenaTopology* topology,
    int actor_x,
    int actor_y,
    int actor_size,
    int target_x,
    int target_y,
    int target_size,
    int attack_range
) {
    encounter_arena_topology_require_finalized(topology);
    encounter_arena_topology_require_footprint_size(
        topology, actor_size);
    encounter_arena_topology_require_footprint_size(
        topology, target_size);
    if (!encounter_arena_topology_footprint_in_bounds_assume_finalized_size_in_range(
            topology, actor_x, actor_y, actor_size) ||
            !encounter_arena_topology_footprint_in_bounds_assume_finalized_size_in_range(
                topology, target_x, target_y, target_size))
        return 0;
    return encounter_arena_topology_los_clear_assume_finalized_footprints_in_bounds(
        topology,
        actor_x,
        actor_y,
        actor_size,
        target_x,
        target_y,
        target_size,
        attack_range);
}

static inline int encounter_arena_topology_player_can_attack_trusted(
    const EncounterArenaTopology* topology,
    int player_x,
    int player_y,
    int target_x,
    int target_y,
    int target_size,
    int attack_range
) {
    int target_max_x = target_x + target_size - 1;
    int target_max_y = target_y + target_size - 1;
    int dx = player_x < target_x
        ? target_x - player_x
        : (player_x > target_max_x ? player_x - target_max_x : 0);
    int dy = player_y < target_y
        ? target_y - player_y
        : (player_y > target_max_y ? player_y - target_max_y : 0);
    int distance = dx > dy ? dx : dy;
    if (distance < 1 || distance > attack_range) return 0;

    if (attack_range == 1) {
        uint32_t flags = topology->static_collision_flags[
            encounter_arena_topology_index_raw(
                topology, player_x, player_y)];
        if (player_x + 1 == target_x &&
                player_y >= target_y && player_y <= target_max_y)
            return (flags & COLLISION_WALL_EAST) == 0;
        if (player_x == target_max_x + 1 &&
                player_y >= target_y && player_y <= target_max_y)
            return (flags & COLLISION_WALL_WEST) == 0;
        if (player_y + 1 == target_y &&
                player_x >= target_x && player_x <= target_max_x)
            return (flags & COLLISION_WALL_NORTH) == 0;
        if (player_y == target_max_y + 1 &&
                player_x >= target_x && player_x <= target_max_x)
            return (flags & COLLISION_WALL_SOUTH) == 0;
        return 0;
    }

    if (topology->static_los_mode ==
            ENCOUNTER_ARENA_TOPOLOGY_LOS_OPEN)
        return 1;
    return encounter_arena_topology_los_clear_assume_finalized_footprints_in_bounds(
        topology,
        player_x,
        player_y,
        1,
        target_x,
        target_y,
        target_size,
        attack_range);
}

static inline int encounter_arena_topology_player_can_attack(
    const EncounterArenaTopology* topology,
    int player_x,
    int player_y,
    int target_x,
    int target_y,
    int target_size,
    int attack_range
) {
    encounter_arena_topology_require_finalized(topology);
    encounter_arena_topology_require_footprint_size(topology, 1);
    encounter_arena_topology_require_footprint_size(
        topology, target_size);
    if (!encounter_arena_topology_footprint_in_bounds_assume_finalized_size_in_range(
            topology, player_x, player_y, 1) ||
            !encounter_arena_topology_footprint_in_bounds_assume_finalized_size_in_range(
                topology, target_x, target_y, target_size))
        return 0;
    if (attack_range < 1)
        return encounter_arena_topology_los_clear_assume_finalized_footprints_in_bounds(
            topology,
            player_x,
            player_y,
            1,
            target_x,
            target_y,
            target_size,
            attack_range);
    return encounter_arena_topology_player_can_attack_trusted(
        topology,
        player_x,
        player_y,
        target_x,
        target_y,
        target_size,
        attack_range);
}

#endif
