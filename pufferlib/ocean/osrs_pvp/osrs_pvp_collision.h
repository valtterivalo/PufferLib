/**
 * @file osrs_pvp_collision.h
 * @brief Tile collision flag system for OSRS world simulation
 *
 * Based on OSRS collision system (TraversalConstants + TraversalMap).
 * Implements the OSRS collision flag bitmask system:
 * - Per-tile int flags storing wall directions, blocked state, impenetrability
 * - Region-based storage (64x64 tiles, 4 height planes per region)
 * - Directional traversal checks (N/S/E/W + diagonals)
 * - Hash-map region manager with lazy allocation
 *
 * When collision_map is NULL, all traversal checks return true (backwards
 * compatible with the flat arena).
 */

#ifndef OSRS_PVP_COLLISION_H
#define OSRS_PVP_COLLISION_H

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* =========================================================================
 * COLLISION FLAG CONSTANTS (from TraversalConstants.java)
 * ========================================================================= */

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

/* =========================================================================
 * REGION DATA STRUCTURE
 * ========================================================================= */

#define REGION_SIZE      64
#define REGION_HEIGHT_LEVELS 4

/** A single 64x64 OSRS region with 4 height planes of collision flags. */
typedef struct {
    int flags[REGION_HEIGHT_LEVELS][REGION_SIZE][REGION_SIZE];
} CollisionRegion;

/* =========================================================================
 * REGION MAP (hash map of regions keyed by region hash)
 * ========================================================================= */

#define REGION_MAP_CAPACITY 256  /* power of 2, enough for wilderness + surroundings */

typedef struct {
    int key;                  /* region hash: (regionX << 8) | regionY, or -1 if empty */
    CollisionRegion* region;
} RegionMapEntry;

typedef struct {
    RegionMapEntry entries[REGION_MAP_CAPACITY];
    int count;
} CollisionMap;

/* =========================================================================
 * COORDINATE HELPERS
 * ========================================================================= */

/** Compute region hash from global tile coordinates. */
static inline int collision_region_hash(int x, int y) {
    return ((x >> 6) << 8) | (y >> 6);
}

/** Extract local coordinate within a 64x64 region. */
static inline int collision_local(int coord) {
    return coord & 0x3F;
}

/* =========================================================================
 * REGION MAP OPERATIONS
 * ========================================================================= */

/** Initialize a collision map (all slots empty). */
static inline void collision_map_init(CollisionMap* map) {
    map->count = 0;
    for (int i = 0; i < REGION_MAP_CAPACITY; i++) {
        map->entries[i].key = -1;
        map->entries[i].region = NULL;
    }
}

/** Allocate and initialize a new collision map. */
static inline CollisionMap* collision_map_create(void) {
    CollisionMap* map = (CollisionMap*)malloc(sizeof(CollisionMap));
    collision_map_init(map);
    return map;
}

/** Look up a region by hash. Returns NULL if not present. */
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

/** Insert a region into the map. Overwrites if key exists. */
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
    /* map full — shouldn't happen with 256 capacity for wilderness */
    fprintf(stderr, "collision_map_put: map full (capacity %d)\n", REGION_MAP_CAPACITY);
}

/** Get or lazily create a region for the given global tile coordinates. */
static inline CollisionRegion* collision_map_get_or_create(CollisionMap* map, int x, int y) {
    int key = collision_region_hash(x, y);
    CollisionRegion* region = collision_map_get(map, key);
    if (region == NULL) {
        region = (CollisionRegion*)calloc(1, sizeof(CollisionRegion));
        collision_map_put(map, key, region);
    }
    return region;
}

/** Free all regions and the map itself. */
static inline void collision_map_free(CollisionMap* map) {
    if (map == NULL) return;
    for (int i = 0; i < REGION_MAP_CAPACITY; i++) {
        if (map->entries[i].region != NULL) {
            free(map->entries[i].region);
        }
    }
    free(map);
}

/* =========================================================================
 * FLAG READ/WRITE
 * ========================================================================= */

/** Get collision flags for a global tile coordinate. Returns 0 if region not loaded. */
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

/** Set (OR) collision flags on a global tile coordinate. */
static inline void collision_set_flag(CollisionMap* map, int height, int x, int y, int flag) {
    CollisionRegion* region = collision_map_get_or_create(map, x, y);
    int lx = collision_local(x);
    int ly = collision_local(y);
    int h = height < 0 ? 0 : (height >= REGION_HEIGHT_LEVELS ? REGION_HEIGHT_LEVELS - 1 : height);
    region->flags[h][lx][ly] |= flag;
}

/** Unset (clear) collision flags on a global tile coordinate. */
static inline void collision_unset_flag(CollisionMap* map, int height, int x, int y, int flag) {
    int key = collision_region_hash(x, y);
    CollisionRegion* region = collision_map_get(map, key);
    if (region == NULL) return;
    int lx = collision_local(x);
    int ly = collision_local(y);
    int h = height < 0 ? 0 : (height >= REGION_HEIGHT_LEVELS ? REGION_HEIGHT_LEVELS - 1 : height);
    region->flags[h][lx][ly] &= ~flag;
}

/** Mark a tile as fully blocked (terrain). */
static inline void collision_mark_blocked(CollisionMap* map, int height, int x, int y) {
    collision_set_flag(map, height, x, y, COLLISION_BLOCKED);
}

/** Mark a multi-tile occupant (game object) as blocked + optionally impenetrable. */
static inline void collision_mark_occupant(CollisionMap* map, int height, int x, int y,
                                           int width, int length, int impenetrable) {
    int flag = COLLISION_BLOCKED;
    if (impenetrable) flag |= COLLISION_IMPENETRABLE_BLOCKED;
    for (int xi = x; xi < x + width; xi++) {
        for (int yi = y; yi < y + length; yi++) {
            collision_set_flag(map, height, xi, yi, flag);
        }
    }
}

/* =========================================================================
 * TRAVERSAL CHECKS (ported from TraversalMap.java)
 *
 * Each check tests the DESTINATION tile for incoming wall flags + BLOCKED.
 * For diagonals: also checks the two cardinal intermediate tiles.
 *
 * All functions take a CollisionMap* which may be NULL (= all traversable).
 * Height is always 0 for PvP (single plane). The height param is kept for
 * future multi-plane support.
 * ========================================================================= */

/** Check if flag bits are INACTIVE (none set) on a tile. */
static inline int collision_is_inactive(const CollisionMap* map, int height, int x, int y, int flag) {
    return (collision_get_flags(map, height, x, y) & flag) == 0;
}

static inline int collision_traversable_north(const CollisionMap* map, int height, int x, int y) {
    if (map == NULL) return 1;
    return collision_is_inactive(map, height, x, y + 1,
        COLLISION_WALL_SOUTH | COLLISION_BLOCKED);
}

static inline int collision_traversable_south(const CollisionMap* map, int height, int x, int y) {
    if (map == NULL) return 1;
    return collision_is_inactive(map, height, x, y - 1,
        COLLISION_WALL_NORTH | COLLISION_BLOCKED);
}

static inline int collision_traversable_east(const CollisionMap* map, int height, int x, int y) {
    if (map == NULL) return 1;
    return collision_is_inactive(map, height, x + 1, y,
        COLLISION_WALL_WEST | COLLISION_BLOCKED);
}

static inline int collision_traversable_west(const CollisionMap* map, int height, int x, int y) {
    if (map == NULL) return 1;
    return collision_is_inactive(map, height, x - 1, y,
        COLLISION_WALL_EAST | COLLISION_BLOCKED);
}

static inline int collision_traversable_north_east(const CollisionMap* map, int height, int x, int y) {
    if (map == NULL) return 1;
    /* diagonal tile: no SW wall, not blocked */
    /* east tile: no west wall, not blocked */
    /* north tile: no south wall, not blocked */
    return collision_is_inactive(map, height, x + 1, y + 1,
               COLLISION_WALL_WEST | COLLISION_WALL_SOUTH | COLLISION_WALL_SOUTH_WEST | COLLISION_BLOCKED)
        && collision_is_inactive(map, height, x + 1, y,
               COLLISION_WALL_WEST | COLLISION_BLOCKED)
        && collision_is_inactive(map, height, x, y + 1,
               COLLISION_WALL_SOUTH | COLLISION_BLOCKED);
}

static inline int collision_traversable_north_west(const CollisionMap* map, int height, int x, int y) {
    if (map == NULL) return 1;
    return collision_is_inactive(map, height, x - 1, y + 1,
               COLLISION_WALL_EAST | COLLISION_WALL_SOUTH | COLLISION_WALL_SOUTH_EAST | COLLISION_BLOCKED)
        && collision_is_inactive(map, height, x - 1, y,
               COLLISION_WALL_EAST | COLLISION_BLOCKED)
        && collision_is_inactive(map, height, x, y + 1,
               COLLISION_WALL_SOUTH | COLLISION_BLOCKED);
}

static inline int collision_traversable_south_east(const CollisionMap* map, int height, int x, int y) {
    if (map == NULL) return 1;
    return collision_is_inactive(map, height, x + 1, y - 1,
               COLLISION_WALL_WEST | COLLISION_WALL_NORTH | COLLISION_WALL_NORTH_WEST | COLLISION_BLOCKED)
        && collision_is_inactive(map, height, x + 1, y,
               COLLISION_WALL_WEST | COLLISION_BLOCKED)
        && collision_is_inactive(map, height, x, y - 1,
               COLLISION_WALL_NORTH | COLLISION_BLOCKED);
}

static inline int collision_traversable_south_west(const CollisionMap* map, int height, int x, int y) {
    if (map == NULL) return 1;
    return collision_is_inactive(map, height, x - 1, y - 1,
               COLLISION_WALL_EAST | COLLISION_WALL_NORTH | COLLISION_WALL_NORTH_EAST | COLLISION_BLOCKED)
        && collision_is_inactive(map, height, x - 1, y,
               COLLISION_WALL_EAST | COLLISION_BLOCKED)
        && collision_is_inactive(map, height, x, y - 1,
               COLLISION_WALL_NORTH | COLLISION_BLOCKED);
}

/**
 * Check if a tile is walkable (not blocked, no incoming walls from any direction).
 * Simple check — just tests the BLOCKED flag on the tile itself.
 */
static inline int collision_tile_walkable(const CollisionMap* map, int height, int x, int y) {
    if (map == NULL) return 1;
    return (collision_get_flags(map, height, x, y) & COLLISION_BLOCKED) == 0;
}

/**
 * Check directional traversability given dx/dy step direction.
 * dx and dy should each be -1, 0, or 1.
 */
static inline int collision_traversable_step(const CollisionMap* map, int height,
                                             int x, int y, int dx, int dy) {
    if (map == NULL) return 1;

    if (dx == 0 && dy == 1)  return collision_traversable_north(map, height, x, y);
    if (dx == 0 && dy == -1) return collision_traversable_south(map, height, x, y);
    if (dx == 1 && dy == 0)  return collision_traversable_east(map, height, x, y);
    if (dx == -1 && dy == 0) return collision_traversable_west(map, height, x, y);
    if (dx == 1 && dy == 1)  return collision_traversable_north_east(map, height, x, y);
    if (dx == -1 && dy == 1) return collision_traversable_north_west(map, height, x, y);
    if (dx == 1 && dy == -1) return collision_traversable_south_east(map, height, x, y);
    if (dx == -1 && dy == -1) return collision_traversable_south_west(map, height, x, y);

    /* dx == 0 && dy == 0: no movement */
    return 1;
}

/* =========================================================================
 * BINARY COLLISION MAP I/O
 *
 * Format:
 *   4 bytes: magic "CMAP"
 *   4 bytes: version (1)
 *   4 bytes: region_count
 *   For each region:
 *     4 bytes: region_hash (key)
 *     REGION_HEIGHT_LEVELS * REGION_SIZE * REGION_SIZE * 4 bytes: flags
 * ========================================================================= */

#define COLLISION_MAP_MAGIC 0x50414D43  /* "CMAP" in little-endian */
#define COLLISION_MAP_VERSION 1

/** Load collision map from a binary file. Returns NULL on failure. */
static inline CollisionMap* collision_map_load(const char* path) {
    FILE* f = fopen(path, "rb");
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

/** Save collision map to a binary file. Returns 0 on success, -1 on failure. */
static inline int collision_map_save(const CollisionMap* map, const char* path) {
    FILE* f = fopen(path, "wb");
    if (f == NULL) return -1;

    uint32_t magic = COLLISION_MAP_MAGIC;
    uint32_t version = COLLISION_MAP_VERSION;
    uint32_t region_count = (uint32_t)map->count;

    fwrite(&magic, 4, 1, f);
    fwrite(&version, 4, 1, f);
    fwrite(&region_count, 4, 1, f);

    for (int i = 0; i < REGION_MAP_CAPACITY; i++) {
        if (map->entries[i].key == -1) continue;
        int32_t key = map->entries[i].key;
        fwrite(&key, 4, 1, f);
        fwrite(map->entries[i].region->flags, 1, sizeof(map->entries[i].region->flags), f);
    }

    fclose(f);
    return 0;
}

#endif /* OSRS_PVP_COLLISION_H */
