/**
 * @fileoverview Sub-tile movement primitives for the OSRS renderer, ported
 * from the deobfuscated client (refs/osrs-client-deob Canvas.java:32-210 +
 * Actor.java:509-522).
 *
 * The real client feeds each actor a queue of up to 10 waypoints (one per
 * 600ms server tick, newest at index 0) and consumes the OLDEST at an integer
 * speed per 20ms client tick: base 4 sub-units (640ms/tile, deliberately
 * slower than the tick so a continuously walking actor always trails with
 * backlog and never arrives early), 6 at queue depth >2, 8 at depth >3 or
 * when repaying stall debt, all doubled while the waypoint is a run step.
 * Per-axis gaps beyond 2 tiles snap. Pure float math so render code and
 * tests share it without dragging raylib into the test binary.
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
#define OSRS_RENDER_WAYPOINT_QUEUE_DEPTH 10
#define OSRS_RENDER_AXIS_SNAP_SUB_UNITS 256.0f

typedef enum {
    RENDER_MOVEMENT_NORMAL = 0,
    RENDER_MOVEMENT_TELEPORT = 1,
} RenderMovementKind;

/**
 * Server-tick waypoint queue for one entity. Newest at index 0 (deob
 * shift-push, Actor.java method2557); consumption reads index length-1.
 */
typedef struct {
    float x[OSRS_RENDER_WAYPOINT_QUEUE_DEPTH];
    float y[OSRS_RENDER_WAYPOINT_QUEUE_DEPTH];
    int running[OSRS_RENDER_WAYPOINT_QUEUE_DEPTH];
    int length;
} OsrsRenderWaypointQueue;

static inline float osrs_render_entity_model_ground(float ground) {
    return ground + OSRS_RENDER_ENTITY_GROUND_LIFT;
}

/**
 * Integer speed ladder from the deob (Canvas.java:99-141). stall_debt
 * repays one debt tick at speed 8 when the queue is >1 deep; the run
 * doubling applies after the depth bumps (real speeds 8/12/16).
 */
static inline int osrs_render_speed_one_client_tick(
    int path_length,
    int waypoint_running,
    int* stall_debt
) {
    int speed = 4;
    if (path_length > 2) speed = 6;
    if (path_length > 3) speed = 8;
    if (stall_debt != NULL && *stall_debt > 0 && path_length > 1) {
        speed = 8;
        (*stall_debt)--;
    }
    if (waypoint_running) speed <<= 1;
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
 * never overshoots. Gaps beyond 2 tiles snap to dest (Canvas.java:80,199-208).
 */
static inline float osrs_render_advance_axis_toward(
    float sub, float dest, float speed
) {
    float dx = dest - sub;
    if (dx > OSRS_RENDER_AXIS_SNAP_SUB_UNITS ||
        dx < -OSRS_RENDER_AXIS_SNAP_SUB_UNITS)
        return dest;
    if (dx == 0.0f) return sub;
    if (dx > 0.0f) return sub + (dx > speed ? speed : dx);
    return sub + (dx < -speed ? -speed : dx);
}

static inline void osrs_render_waypoint_queue_clear(OsrsRenderWaypointQueue* q) {
    q->length = 0;
}

/**
 * Shift-push a waypoint at index 0; a full queue drops its oldest entry.
 */
static inline void osrs_render_waypoint_push(
    OsrsRenderWaypointQueue* q, float x, float y, int running
) {
    int n = q->length < OSRS_RENDER_WAYPOINT_QUEUE_DEPTH - 1
        ? q->length
        : OSRS_RENDER_WAYPOINT_QUEUE_DEPTH - 1;
    for (int i = n; i > 0; i--) {
        q->x[i] = q->x[i - 1];
        q->y[i] = q->y[i - 1];
        q->running[i] = q->running[i - 1];
    }
    q->x[0] = x;
    q->y[0] = y;
    q->running[0] = running;
    q->length = n + 1;
}

/**
 * Consume one client tick of movement toward the oldest waypoint, popping
 * it on arrival. Movement clamps at the waypoint (one waypoint per tick at
 * most, residual speed is discarded, matching the deob). Returns 1 when the
 * entity is moving this tick; dir_dx/dir_dy carry the pre-advance delta for
 * yaw selection.
 */
static inline int osrs_render_waypoint_advance_one_client_tick(
    OsrsRenderWaypointQueue* q,
    float* sub_x,
    float* sub_y,
    int* stall_debt,
    int* speed_out,
    float* dir_dx,
    float* dir_dy
) {
    if (q->length == 0) {
        *speed_out = 0;
        *dir_dx = 0.0f;
        *dir_dy = 0.0f;
        return 0;
    }
    int wi = q->length - 1;
    float wx = q->x[wi];
    float wy = q->y[wi];
    int speed = osrs_render_speed_one_client_tick(
        q->length, q->running[wi], stall_debt);
    *dir_dx = wx - *sub_x;
    *dir_dy = wy - *sub_y;
    *sub_x = osrs_render_advance_axis_toward(*sub_x, wx, (float)speed);
    *sub_y = osrs_render_advance_axis_toward(*sub_y, wy, (float)speed);
    if (*sub_x == wx && *sub_y == wy) q->length--;
    *speed_out = speed;
    return 1;
}

#endif
