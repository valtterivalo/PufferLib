/**
 * @file test_projectile_orientation.c
 * @brief regression tests for OSRS projectile tangent orientation.
 *
 * BUILD:
 *   cc -std=c11 -O0 -g -DRAYMATH_IMPLEMENTATION -I. \
 *       -Iocean/osrs/raylib-5.5_macos/include \
 *       -o /tmp/test_projectile_orientation \
 *       ocean/osrs/tests/test_projectile_orientation.c -lm
 *   /tmp/test_projectile_orientation
 */

#include <math.h>
#include <stdio.h>

#include "raymath.h"
#include "ocean/osrs/osrs_projectile_orientation.h"

static int tests_run = 0;
static int tests_passed = 0;
static int tests_failed = 0;

#define ASSERT_NEAR(label, actual, expected, tolerance) do { \
    tests_run++; \
    float diff = fabsf((actual) - (expected)); \
    if (diff <= (tolerance)) { \
        tests_passed++; \
    } else { \
        tests_failed++; \
        printf("  FAIL: %s - got %.6f, expected %.6f\n", \
            (label), (double)(actual), (double)(expected)); \
    } \
} while (0)

#define ASSERT_TRUE(label, condition) do { \
    tests_run++; \
    if (condition) { \
        tests_passed++; \
    } else { \
        tests_failed++; \
        printf("  FAIL: %s\n", (label)); \
    } \
} while (0)

static Vector3 transformed_arrow_forward(OsrsProjectileOrientation orientation) {
    Matrix transform = MatrixScale(-1.0f, 1.0f, 1.0f);
    transform = MatrixMultiply(transform,
        MatrixMultiply(MatrixRotateX(orientation.pitch), MatrixRotateY(orientation.yaw)));
    return Vector3Transform((Vector3){0.0f, 0.0f, -1.0f}, transform);
}

static void test_cardinal_yaw_aligns_local_negative_z_to_world_tangent(void) {
    OsrsProjectileOrientation east = osrs_projectile_orientation_from_step(1.0f, 0.0f, 0.0f);
    Vector3 east_forward = transformed_arrow_forward(east);
    ASSERT_NEAR("east forward x", east_forward.x, 1.0f, 0.0001f);
    ASSERT_NEAR("east forward y", east_forward.y, 0.0f, 0.0001f);
    ASSERT_NEAR("east forward z", east_forward.z, 0.0f, 0.0001f);

    OsrsProjectileOrientation north = osrs_projectile_orientation_from_step(0.0f, 1.0f, 0.0f);
    Vector3 north_forward = transformed_arrow_forward(north);
    ASSERT_NEAR("north forward x", north_forward.x, 0.0f, 0.0001f);
    ASSERT_NEAR("north forward y", north_forward.y, 0.0f, 0.0001f);
    ASSERT_NEAR("north forward z", north_forward.z, -1.0f, 0.0001f);
}

static void test_pitch_survives_yaw_for_eastbound_projectile(void) {
    OsrsProjectileOrientation orientation =
        osrs_projectile_orientation_from_step(1.0f, 0.0f, 0.5f);
    Vector3 forward = transformed_arrow_forward(orientation);
    ASSERT_TRUE("eastbound upward pitch has vertical component", forward.y > 0.4f);
    ASSERT_TRUE("eastbound upward pitch still travels east", forward.x > 0.8f);
    ASSERT_NEAR("eastbound upward pitch has no z drift", forward.z, 0.0f, 0.0001f);
}

static void test_arc_height_delta_drives_pitch_sign(void) {
    float early_0 = osrs_projectile_height_at_progress(
        0.0f, 1.0f, 1.0f, 1.0f, 0.0f, 0.0f);
    float early_1 = osrs_projectile_height_at_progress(
        0.1f, 1.0f, 1.0f, 1.0f, 0.0f, 0.0f);
    OsrsProjectileOrientation early =
        osrs_projectile_orientation_from_step(1.0f, 0.0f, early_1 - early_0);
    ASSERT_TRUE("arc projectile pitches up early", early.pitch > 0.0f);

    float late_0 = osrs_projectile_height_at_progress(
        0.8f, 1.0f, 1.0f, 1.0f, 0.0f, 0.0f);
    float late_1 = osrs_projectile_height_at_progress(
        0.9f, 1.0f, 1.0f, 1.0f, 0.0f, 0.0f);
    OsrsProjectileOrientation late =
        osrs_projectile_orientation_from_step(1.0f, 0.0f, late_1 - late_0);
    ASSERT_TRUE("arc projectile pitches down late", late.pitch < 0.0f);
}

static void test_target_anchor_uses_visual_subtile_center(void) {
    ASSERT_NEAR("tile 16 center maps to flight coord 16",
        osrs_projectile_anchor_coord_from_subtile(16 * 128 + 64),
        16.0f, 0.0001f);
    ASSERT_NEAR("halfway movement maps between tiles",
        osrs_projectile_anchor_coord_from_subtile(16 * 128 + 96),
        16.25f, 0.0001f);
}

int main(void) {
    printf("Projectile orientation tests\n");
    test_cardinal_yaw_aligns_local_negative_z_to_world_tangent();
    test_pitch_survives_yaw_for_eastbound_projectile();
    test_arc_height_delta_drives_pitch_sign();
    test_target_anchor_uses_visual_subtile_center();

    printf("Passed: %d/%d\n", tests_passed, tests_run);
    if (tests_failed > 0) {
        printf("Failed: %d\n", tests_failed);
        return 1;
    }
    return 0;
}
