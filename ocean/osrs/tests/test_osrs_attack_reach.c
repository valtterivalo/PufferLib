#include <stdio.h>
#include <stdlib.h>

#include "ocean/osrs/osrs_attack_reach.h"

static int tests_run = 0;
static int tests_passed = 0;
static int tests_failed = 0;

#define ASSERT_INT_EQ(label, actual, expected) do { \
    tests_run++; \
    if ((actual) == (expected)) { \
        tests_passed++; \
    } else { \
        tests_failed++; \
        printf("  FAIL: %s got %d expected %d\n", (label), (actual), (expected)); \
    } \
} while (0)

static OsrsAttackReachQuery reach_query(
    int sx,
    int sy,
    int ssize,
    int tx,
    int ty,
    int tsize,
    OsrsAttackDelivery delivery,
    int range,
    OsrsProjectileOcclusion occlusion
) {
    return (OsrsAttackReachQuery){
        .source = osrs_footprint(sx, sy, ssize),
        .target = osrs_footprint(tx, ty, tsize),
        .delivery = delivery,
        .range = range,
        .occlusion = occlusion,
    };
}

static void test_melee_cardinal_adjacency(void) {
    OsrsAttackReachQuery reach = reach_query(
        0, 0, 1,
        1, 0, 1,
        OSRS_ATTACK_DELIVERY_MELEE,
        1,
        osrs_projectile_occlusion_open());
    ASSERT_INT_EQ("melee cardinal adjacency succeeds",
        osrs_attack_reach(&reach), OSRS_ATTACK_REACH_OK);

    reach = reach_query(
        0, 0, 1,
        1, 1, 1,
        OSRS_ATTACK_DELIVERY_MELEE,
        1,
        osrs_projectile_occlusion_open());
    ASSERT_INT_EQ("melee diagonal fails",
        osrs_attack_reach(&reach), OSRS_ATTACK_REACH_OUT_OF_RANGE);

    reach = reach_query(
        0, 0, 1,
        0, 0, 1,
        OSRS_ATTACK_DELIVERY_MELEE,
        1,
        osrs_projectile_occlusion_open());
    ASSERT_INT_EQ("same-tile melee fails",
        osrs_attack_reach(&reach), OSRS_ATTACK_REACH_SAME_TILE);
}

static void test_projectile_range_boundary(void) {
    OsrsAttackReachQuery reach = reach_query(
        0, 0, 1,
        3, 3, 1,
        OSRS_ATTACK_DELIVERY_PROJECTILE,
        3,
        osrs_projectile_occlusion_open());
    ASSERT_INT_EQ("projectile range boundary succeeds",
        osrs_attack_reach(&reach), OSRS_ATTACK_REACH_OK);

    reach.range = 2;
    ASSERT_INT_EQ("projectile out of range fails",
        osrs_attack_reach(&reach), OSRS_ATTACK_REACH_OUT_OF_RANGE);
}

static void test_los_blocker_occlusion(void) {
    LOSBlocker blocker = {2, 0, 1, LOS_FULL_MASK};
    OsrsAttackReachQuery reach = reach_query(
        0, 0, 1,
        5, 0, 1,
        OSRS_ATTACK_DELIVERY_PROJECTILE,
        10,
        osrs_projectile_occlusion_los_blockers(&blocker, 1));
    ASSERT_INT_EQ("LOS blocker blocks projectile",
        osrs_attack_reach(&reach), OSRS_ATTACK_REACH_LOS_BLOCKED);
}

static void test_collision_map_occlusion(void) {
    CollisionMap* cmap = collision_map_create();
    collision_mark_occupant(cmap, 0, 2, 0, 1, 1, 1);
    OsrsAttackReachQuery reach = reach_query(
        0, 0, 1,
        5, 0, 1,
        OSRS_ATTACK_DELIVERY_PROJECTILE,
        10,
        osrs_projectile_occlusion_collision_map(cmap, 0));
    ASSERT_INT_EQ("collision impenetrable tile blocks projectile",
        osrs_attack_reach(&reach), OSRS_ATTACK_REACH_LOS_BLOCKED);
    collision_map_free(cmap);
}

static void test_combined_occlusion(void) {
    CollisionMap* cmap = collision_map_create();
    LOSBlocker blocker = {2, 0, 1, LOS_FULL_MASK};
    OsrsAttackReachQuery reach = reach_query(
        0, 0, 1,
        5, 0, 1,
        OSRS_ATTACK_DELIVERY_PROJECTILE,
        10,
        osrs_projectile_occlusion_collision_map_and_blockers(cmap, 0, &blocker, 1));
    ASSERT_INT_EQ("combined occlusion blocks on blocker",
        osrs_attack_reach(&reach), OSRS_ATTACK_REACH_LOS_BLOCKED);

    collision_mark_occupant(cmap, 0, 2, 0, 1, 1, 1);
    reach.occlusion = osrs_projectile_occlusion_collision_map_and_blockers(cmap, 0, NULL, 0);
    ASSERT_INT_EQ("combined occlusion blocks on collision",
        osrs_attack_reach(&reach), OSRS_ATTACK_REACH_LOS_BLOCKED);
    collision_map_free(cmap);
}

static void test_open_occlusion_ignores_blockers(void) {
    OsrsAttackReachQuery reach = reach_query(
        0, 0, 1,
        5, 0, 1,
        OSRS_ATTACK_DELIVERY_PROJECTILE,
        10,
        osrs_projectile_occlusion_open());
    ASSERT_INT_EQ("open occlusion ignores LOS sources",
        osrs_attack_reach(&reach), OSRS_ATTACK_REACH_OK);
}

int main(void) {
    test_melee_cardinal_adjacency();
    test_projectile_range_boundary();
    test_los_blocker_occlusion();
    test_collision_map_occlusion();
    test_combined_occlusion();
    test_open_occlusion_ignores_blockers();

    printf("osrs attack reach tests: %d passed / %d run\n",
        tests_passed, tests_run);
    return tests_failed == 0 ? 0 : 1;
}
