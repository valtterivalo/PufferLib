/**
 * @fileoverview Sub-tile interpolation primitives for the OSRS renderer.
 *
 * Pure float math so render code and tests can share it without dragging
 * raylib into the test binary.
 */

#ifndef OSRS_RENDER_MOTION_H
#define OSRS_RENDER_MOTION_H

#include <math.h>
#include <stddef.h>

#define OSRS_RENDER_SUB_UNITS_PER_TILE 128.0f
#define OSRS_RENDER_CLIENT_TICKS_PER_GAME_TICK 30.0f
#define OSRS_RENDER_ENTITY_GROUND_LIFT_OSRS_UNITS 8.0f
#define OSRS_RENDER_ENTITY_GROUND_LIFT \
    (OSRS_RENDER_ENTITY_GROUND_LIFT_OSRS_UNITS / OSRS_RENDER_SUB_UNITS_PER_TILE)
#define OSRS_RENDER_RUN_SPEED_THRESHOLD 8.0f

typedef enum {
    RENDER_MOVEMENT_NORMAL = 0,
    RENDER_MOVEMENT_TELEPORT = 1,
} RenderMovementKind;

static inline float osrs_render_entity_model_ground(float ground) {
    return ground + OSRS_RENDER_ENTITY_GROUND_LIFT;
}

/**
 * Per-client-tick base walk speed in sub-units. 128/30 = 4.2667.
 *
 * Float math guarantees that a walking entity reaches dest exactly after
 * RENDER_CLIENT_TICKS_PER_GAME_TICK ticks (30 * 4.2667 = 128.0).
 */
static inline float osrs_render_base_walk_speed_one_client_tick(void) {
    return OSRS_RENDER_SUB_UNITS_PER_TILE
        / OSRS_RENDER_CLIENT_TICKS_PER_GAME_TICK;
}

static inline int osrs_render_visual_backlog(
    float sub_x,
    float sub_y,
    float dest_x,
    float dest_y
) {
    float dx = fabsf(dest_x - sub_x);
    float dy = fabsf(dest_y - sub_y);
    float gap = dx > dy ? dx : dy;
    if (gap <= 0.0f) return 0;
    int backlog = (int)(gap / OSRS_RENDER_SUB_UNITS_PER_TILE);
    if ((float)backlog * OSRS_RENDER_SUB_UNITS_PER_TILE < gap)
        backlog++;
    return backlog;
}

static inline float osrs_render_effective_speed_one_client_tick(
    int running,
    int visual_backlog,
    int* step_tracker
) {
    float base_walk = OSRS_RENDER_SUB_UNITS_PER_TILE
        / OSRS_RENDER_CLIENT_TICKS_PER_GAME_TICK;
    float speed = base_walk;
    int has_stall_debt = step_tracker != NULL && *step_tracker > 0;
    if (visual_backlog > 3 || (has_stall_debt && visual_backlog > 1)) {
        speed *= 2.0f;
        if (has_stall_debt)
            (*step_tracker)--;
    } else if (visual_backlog > 2) {
        speed *= 1.5f;
    }
    if (running)
        speed *= 2.0f;
    return speed;
}

static inline int osrs_render_speed_uses_run_pose(float effective_speed) {
    return effective_speed >= OSRS_RENDER_RUN_SPEED_THRESHOLD;
}

static inline int osrs_render_should_seed_visual_position(
    int visible,
    int new_identity,
    int became_visible,
    RenderMovementKind movement_kind
) {
    if (!visible) return 0;
    return new_identity || became_visible ||
        movement_kind == RENDER_MOVEMENT_TELEPORT;
}

/**
 * Advance one sub-tile axis toward dest by at most speed, clamped so sub
 * never overshoots in either direction.
 */
static inline float osrs_render_advance_axis_toward(
    float sub, float dest, float speed
) {
    float dx = dest - sub;
    if (dx == 0.0f) return sub;
    if (dx > 0.0f) return sub + (dx > speed ? speed : dx);
    return sub + (dx < -speed ? -speed : dx);
}

/**
 * Convenience wrapper: compute speed and advance one axis in one call.
 * For two-axis motion, prefer calling osrs_render_effective_speed_one_client_tick
 * once and osrs_render_advance_axis_toward twice so step_tracker is consumed
 * once per client tick.
 */
static inline float osrs_render_advance_axis_one_client_tick(
    float sub, float dest, int running, int* step_tracker
) {
    int backlog = osrs_render_visual_backlog(sub, 0.0f, dest, 0.0f);
    float speed = osrs_render_effective_speed_one_client_tick(
        running, backlog, step_tracker);
    return osrs_render_advance_axis_toward(sub, dest, speed);
}

#endif
