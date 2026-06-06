#ifndef OSRS_ATTACK_REACH_H
#define OSRS_ATTACK_REACH_H

#include <stdio.h>
#include <stdlib.h>

#include "osrs_collision.h"

typedef struct {
    int x;
    int y;
    int size;
} OsrsFootprint;

typedef enum {
    OSRS_ATTACK_DELIVERY_MELEE = 0,
    OSRS_ATTACK_DELIVERY_PROJECTILE,
} OsrsAttackDelivery;

typedef enum {
    OSRS_PROJECTILE_OCCLUSION_OPEN = 0,
    OSRS_PROJECTILE_OCCLUSION_LOS_BLOCKERS,
    OSRS_PROJECTILE_OCCLUSION_COLLISION_MAP,
    OSRS_PROJECTILE_OCCLUSION_COLLISION_MAP_AND_BLOCKERS,
} OsrsProjectileOcclusionKind;

typedef struct {
    OsrsProjectileOcclusionKind kind;
    union {
        OsrsLosBlockers blockers;
        OsrsCollisionLos collision;
        OsrsCombinedLos combined;
    } data;
} OsrsProjectileOcclusion;

typedef struct {
    OsrsFootprint source;
    OsrsFootprint target;
    OsrsAttackDelivery delivery;
    int range;
    OsrsProjectileOcclusion occlusion;
} OsrsAttackReachQuery;

typedef enum {
    OSRS_ATTACK_REACH_OK = 0,
    OSRS_ATTACK_REACH_SAME_TILE,
    OSRS_ATTACK_REACH_OUT_OF_RANGE,
    OSRS_ATTACK_REACH_LOS_BLOCKED,
} OsrsAttackReachResult;

static inline OsrsFootprint osrs_footprint(int x, int y, int size) {
    OsrsFootprint out;
    out.x = x;
    out.y = y;
    out.size = size;
    return out;
}

static inline OsrsProjectileOcclusion osrs_projectile_occlusion_open(void) {
    OsrsProjectileOcclusion out;
    out.kind = OSRS_PROJECTILE_OCCLUSION_OPEN;
    return out;
}

static inline OsrsProjectileOcclusion osrs_projectile_occlusion_los_blockers(
    const LOSBlocker* blockers,
    int count
) {
    OsrsProjectileOcclusion out;
    out.kind = OSRS_PROJECTILE_OCCLUSION_LOS_BLOCKERS;
    out.data.blockers.blockers = blockers;
    out.data.blockers.count = count;
    return out;
}

static inline OsrsProjectileOcclusion osrs_projectile_occlusion_collision_map(
    const CollisionMap* map,
    int height
) {
    OsrsProjectileOcclusion out;
    out.kind = OSRS_PROJECTILE_OCCLUSION_COLLISION_MAP;
    out.data.collision.map = map;
    out.data.collision.height = height;
    return out;
}

static inline OsrsProjectileOcclusion osrs_projectile_occlusion_collision_map_and_blockers(
    const CollisionMap* map,
    int height,
    const LOSBlocker* blockers,
    int blocker_count
) {
    OsrsProjectileOcclusion out;
    out.kind = OSRS_PROJECTILE_OCCLUSION_COLLISION_MAP_AND_BLOCKERS;
    out.data.combined.map = map;
    out.data.combined.height = height;
    out.data.combined.blockers = blockers;
    out.data.combined.blocker_count = blocker_count;
    return out;
}

static inline void osrs_footprint_require_valid(OsrsFootprint footprint) {
    if (footprint.size <= 0) {
        fprintf(stderr, "invalid OSRS footprint size: %d\n", footprint.size);
        abort();
    }
}

static inline void osrs_projectile_occlusion_require_valid(
    const OsrsProjectileOcclusion* occlusion
) {
    switch (occlusion->kind) {
        case OSRS_PROJECTILE_OCCLUSION_OPEN:
            return;
        case OSRS_PROJECTILE_OCCLUSION_LOS_BLOCKERS:
            if (occlusion->data.blockers.count < 0 ||
                    (occlusion->data.blockers.count > 0 &&
                     occlusion->data.blockers.blockers == NULL)) {
                fprintf(stderr, "invalid LOS blocker occlusion\n");
                abort();
            }
            return;
        case OSRS_PROJECTILE_OCCLUSION_COLLISION_MAP:
            if (occlusion->data.collision.map == NULL) {
                fprintf(stderr, "collision-map occlusion requires a collision map\n");
                abort();
            }
            return;
        case OSRS_PROJECTILE_OCCLUSION_COLLISION_MAP_AND_BLOCKERS:
            if (occlusion->data.combined.map == NULL ||
                    occlusion->data.combined.blocker_count < 0 ||
                    (occlusion->data.combined.blocker_count > 0 &&
                     occlusion->data.combined.blockers == NULL)) {
                fprintf(stderr, "invalid combined projectile occlusion\n");
                abort();
            }
            return;
        default:
            fprintf(stderr, "invalid projectile occlusion kind: %d\n", occlusion->kind);
            abort();
    }
}

static inline int osrs_footprints_overlap(OsrsFootprint a, OsrsFootprint b) {
    return !(a.x + a.size <= b.x || b.x + b.size <= a.x ||
             a.y + a.size <= b.y || b.y + b.size <= a.y);
}

static inline int osrs_footprint_distance(OsrsFootprint a, OsrsFootprint b) {
    int ax1 = a.x + a.size - 1;
    int ay1 = a.y + a.size - 1;
    int bx1 = b.x + b.size - 1;
    int by1 = b.y + b.size - 1;

    int dx = 0;
    if (ax1 < b.x) dx = b.x - ax1;
    else if (bx1 < a.x) dx = a.x - bx1;

    int dy = 0;
    if (ay1 < b.y) dy = b.y - ay1;
    else if (by1 < a.y) dy = a.y - by1;

    return dx > dy ? dx : dy;
}

static inline int osrs_footprints_cardinally_adjacent(OsrsFootprint a, OsrsFootprint b) {
    int ax0 = a.x;
    int ax1 = a.x + a.size - 1;
    int ay0 = a.y;
    int ay1 = a.y + a.size - 1;
    int bx0 = b.x;
    int bx1 = b.x + b.size - 1;
    int by0 = b.y;
    int by1 = b.y + b.size - 1;

    return (ax1 + 1 == bx0 && los_intervals_overlap(ay0, ay1, by0, by1)) ||
           (bx1 + 1 == ax0 && los_intervals_overlap(ay0, ay1, by0, by1)) ||
           (ay1 + 1 == by0 && los_intervals_overlap(ax0, ax1, bx0, bx1)) ||
           (by1 + 1 == ay0 && los_intervals_overlap(ax0, ax1, bx0, bx1));
}

static inline int osrs_projectile_occlusion_has_los(
    const OsrsProjectileOcclusion* occlusion,
    OsrsFootprint source,
    OsrsFootprint target,
    int range
) {
    switch (occlusion->kind) {
        case OSRS_PROJECTILE_OCCLUSION_OPEN:
            return 1;
        case OSRS_PROJECTILE_OCCLUSION_LOS_BLOCKERS: {
            OsrsLosBlockers ctx = occlusion->data.blockers;
            OsrsLosTileMaskSource mask_source = {
                .mask_at = osrs_los_blockers_mask_at,
                .ctx = &ctx,
            };
            return osrs_entity_has_line_of_sight_from_source(
                &mask_source,
                source.x, source.y, source.size,
                target.x, target.y, target.size,
                range);
        }
        case OSRS_PROJECTILE_OCCLUSION_COLLISION_MAP: {
            OsrsCollisionLos ctx = occlusion->data.collision;
            OsrsLosTileMaskSource mask_source = {
                .mask_at = osrs_los_collision_mask_at,
                .ctx = &ctx,
            };
            return osrs_entity_has_line_of_sight_from_source(
                &mask_source,
                source.x, source.y, source.size,
                target.x, target.y, target.size,
                range);
        }
        case OSRS_PROJECTILE_OCCLUSION_COLLISION_MAP_AND_BLOCKERS: {
            OsrsCombinedLos ctx = occlusion->data.combined;
            OsrsLosTileMaskSource mask_source = {
                .mask_at = osrs_los_combined_mask_at,
                .ctx = &ctx,
            };
            return osrs_entity_has_line_of_sight_from_source(
                &mask_source,
                source.x, source.y, source.size,
                target.x, target.y, target.size,
                range);
        }
        default:
            fprintf(stderr, "invalid projectile occlusion kind: %d\n", occlusion->kind);
            abort();
    }
}

static inline OsrsAttackReachResult osrs_attack_reach(
    const OsrsAttackReachQuery* query
) {
    if (query == NULL) {
        fprintf(stderr, "attack reach query is null\n");
        abort();
    }
    osrs_footprint_require_valid(query->source);
    osrs_footprint_require_valid(query->target);

    if (osrs_footprints_overlap(query->source, query->target))
        return OSRS_ATTACK_REACH_SAME_TILE;

    switch (query->delivery) {
        case OSRS_ATTACK_DELIVERY_MELEE:
            return osrs_footprints_cardinally_adjacent(query->source, query->target)
                ? OSRS_ATTACK_REACH_OK
                : OSRS_ATTACK_REACH_OUT_OF_RANGE;
        case OSRS_ATTACK_DELIVERY_PROJECTILE:
            osrs_projectile_occlusion_require_valid(&query->occlusion);
            if (query->range <= 0) {
                fprintf(stderr, "projectile attack range must be positive: %d\n", query->range);
                abort();
            }
            if (osrs_footprint_distance(query->source, query->target) > query->range)
                return OSRS_ATTACK_REACH_OUT_OF_RANGE;
            if (!osrs_projectile_occlusion_has_los(
                    &query->occlusion, query->source, query->target, query->range))
                return OSRS_ATTACK_REACH_LOS_BLOCKED;
            return OSRS_ATTACK_REACH_OK;
        default:
            fprintf(stderr, "invalid attack delivery kind: %d\n", query->delivery);
            abort();
    }
}

static inline int osrs_attack_can_reach(const OsrsAttackReachQuery* query) {
    return osrs_attack_reach(query) == OSRS_ATTACK_REACH_OK;
}

#endif
