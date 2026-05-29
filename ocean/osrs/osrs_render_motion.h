/**
 * @fileoverview Sub-tile interpolation primitives for the OSRS renderer.
 *
 * Pure float math so render code and tests can share it without dragging
 * raylib into the test binary.
 */

#ifndef OSRS_RENDER_MOTION_H
#define OSRS_RENDER_MOTION_H

#include <stddef.h>

#define OSRS_RENDER_SUB_UNITS_PER_TILE 128.0f
#define OSRS_RENDER_CLIENT_TICKS_PER_GAME_TICK 30.0f
#define OSRS_RENDER_ENTITY_GROUND_LIFT_OSRS_UNITS 8.0f
#define OSRS_RENDER_ENTITY_GROUND_LIFT \
    (OSRS_RENDER_ENTITY_GROUND_LIFT_OSRS_UNITS / OSRS_RENDER_SUB_UNITS_PER_TILE)

static inline float osrs_render_entity_model_ground(float ground) {
    return ground + OSRS_RENDER_ENTITY_GROUND_LIFT;
}

/**
 * Per-client-tick walk speed in sub-units. 128/30 = 4.2667 for walk,
 * doubled while running, doubled again while a stall-catchup credit
 * is consumed.
 *
 * Float math guarantees that a walking entity reaches dest exactly after
 * RENDER_CLIENT_TICKS_PER_GAME_TICK ticks (30 * 4.2667 = 128.0).
 */
static inline float osrs_render_walk_speed_one_client_tick(
    int running, int* step_tracker
) {
    float base_walk = OSRS_RENDER_SUB_UNITS_PER_TILE
        / OSRS_RENDER_CLIENT_TICKS_PER_GAME_TICK;
    float speed = running ? base_walk * 2.0f : base_walk;
    if (step_tracker != NULL && *step_tracker > 0) {
        speed *= 2.0f;
        (*step_tracker)--;
    }
    return speed;
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
 * For two-axis motion, prefer calling osrs_render_walk_speed_one_client_tick
 * once and osrs_render_advance_axis_toward twice so step_tracker is only
 * consumed once per client tick.
 */
static inline float osrs_render_advance_axis_one_client_tick(
    float sub, float dest, int running, int* step_tracker
) {
    float speed = osrs_render_walk_speed_one_client_tick(running, step_tracker);
    return osrs_render_advance_axis_toward(sub, dest, speed);
}

#endif
