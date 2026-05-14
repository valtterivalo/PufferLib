/**
 * @file test_render_click_hulls.c
 * @brief Regression tests for human-mode 3D NPC click hull sizing.
 *
 * BUILD:
 *   cc -std=c11 -O0 -g -I. -Iocean/osrs -I./ocean/osrs/raylib-5.5_macos/include \
 *       -o /tmp/test_render_click_hulls \
 *       ocean/osrs/tests/test_render_click_hulls.c -lm
 *   /tmp/test_render_click_hulls
 */

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "ocean/osrs/osrs_render_click_hull.h"

static int tests_run = 0;
static int tests_passed = 0;
static int tests_failed = 0;

#define ASSERT_INT_EQ(label, actual, expected) do { \
    tests_run++; \
    if ((actual) == (expected)) { \
        tests_passed++; \
    } else { \
        tests_failed++; \
        printf("  FAIL: %s - got %d, expected %d\n", (label), (actual), (expected)); \
    } \
} while (0)

#define ASSERT_FLOAT_CLOSE(label, actual, expected, eps) do { \
    tests_run++; \
    float _a = (float)(actual); \
    float _e = (float)(expected); \
    if (fabsf(_a - _e) <= (eps)) { \
        tests_passed++; \
    } else { \
        tests_failed++; \
        printf("  FAIL: %s - got %.6f, expected %.6f\n", (label), _a, _e); \
    } \
} while (0)

static void bounds_for_points(
    const Vector3* points, int count,
    float* min_x, float* max_x,
    float* min_y, float* max_y,
    float* min_z, float* max_z
) {
    *min_x = *min_y = *min_z = 1000000.0f;
    *max_x = *max_y = *max_z = -1000000.0f;
    for (int i = 0; i < count; i++) {
        if (points[i].x < *min_x) *min_x = points[i].x;
        if (points[i].x > *max_x) *max_x = points[i].x;
        if (points[i].y < *min_y) *min_y = points[i].y;
        if (points[i].y > *max_y) *max_y = points[i].y;
        if (points[i].z < *min_z) *min_z = points[i].z;
        if (points[i].z > *max_z) *max_z = points[i].z;
    }
}

static void test_size_one_npc_gets_osrs_sdk_style_click_prism(void) {
    printf("--- size-one NPC gets osrs-sdk style click prism ---\n");

    RenderEntity entity;
    memset(&entity, 0, sizeof(entity));
    entity.entity_type = ENTITY_NPC;
    entity.npc_size = 1;

    Vector3 points[RENDER_CLICKBOX_PRISM_POINT_COUNT];
    int count = render_build_entity_clickbox_prism_points(
        &entity, 10.5f, -20.5f, 2.0f, 0.25f,
        points, RENDER_CLICKBOX_PRISM_POINT_COUNT);

    ASSERT_INT_EQ("size-one point count", count, RENDER_CLICKBOX_PRISM_POINT_COUNT);

    float min_x, max_x, min_y, max_y, min_z, max_z;
    bounds_for_points(points, count, &min_x, &max_x, &min_y, &max_y, &min_z, &max_z);

    ASSERT_FLOAT_CLOSE("size-one radius west", min_x, 10.1f, 0.0001f);
    ASSERT_FLOAT_CLOSE("size-one radius east", max_x, 10.9f, 0.0001f);
    ASSERT_FLOAT_CLOSE("size-one bottom", min_y, 2.0f, 0.0001f);
    ASSERT_FLOAT_CLOSE("size-one minimum height", max_y, 3.0f, 0.0001f);
    ASSERT_FLOAT_CLOSE("size-one hex north", min_z, -20.846411f, 0.0001f);
    ASSERT_FLOAT_CLOSE("size-one hex south", max_z, -20.153589f, 0.0001f);
}

static void test_blob_size_scales_click_prism_radius(void) {
    printf("--- blob-size NPC scales click prism radius ---\n");

    RenderEntity entity;
    memset(&entity, 0, sizeof(entity));
    entity.entity_type = ENTITY_NPC;
    entity.npc_size = 3;

    Vector3 points[RENDER_CLICKBOX_PRISM_POINT_COUNT];
    int count = render_build_entity_clickbox_prism_points(
        &entity, 15.5f, -31.5f, 2.0f, 2.0f,
        points, RENDER_CLICKBOX_PRISM_POINT_COUNT);

    ASSERT_INT_EQ("blob point count", count, RENDER_CLICKBOX_PRISM_POINT_COUNT);

    float min_x, max_x, min_y, max_y, min_z, max_z;
    bounds_for_points(points, count, &min_x, &max_x, &min_y, &max_y, &min_z, &max_z);

    ASSERT_FLOAT_CLOSE("blob radius west", min_x, 14.3f, 0.0001f);
    ASSERT_FLOAT_CLOSE("blob radius east", max_x, 16.7f, 0.0001f);
    ASSERT_FLOAT_CLOSE("blob minimum height", max_y, 5.0f, 0.0001f);
}

int main(void) {
    test_size_one_npc_gets_osrs_sdk_style_click_prism();
    test_blob_size_scales_click_prism_radius();

    if (tests_failed > 0) {
        printf("\n%d/%d click hull checks failed\n", tests_failed, tests_run);
        return 1;
    }

    printf("\n%d/%d click hull checks passed\n", tests_passed, tests_run);
    return 0;
}
