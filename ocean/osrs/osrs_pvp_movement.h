#ifndef OSRS_PVP_MOVEMENT_H
#define OSRS_PVP_MOVEMENT_H

#include "osrs_types.h"
#include "osrs_collision.h"
#include "osrs_encounter.h"
#include "osrs_encounter_player.h"
#include "osrs_pvp_gear.h"

#define PVP_LOCAL_PATHFIND_MARGIN 20

static int select_closest_adjacent_tile(Player* p, int target_x, int target_y, int* out_x, int* out_y, const CollisionMap* cmap) {
    int candidates[4][2] = {
        {target_x, target_y + 1},
        {target_x + 1, target_y},
        {target_x, target_y - 1},
        {target_x - 1, target_y}
    };

    int has_best = 0;
    int best_x = 0;
    int best_y = 0;
    int best_dist_agent = 0;
    int best_dist_target = 0;
    int best_hash = 0;

    for (int i = 0; i < 4; i++) {
        int cx = candidates[i][0];
        int cy = candidates[i][1];
        if (!is_in_wilderness(cx, cy)) {
            continue;
        }
        if (!collision_tile_walkable(cmap, 0, cx, cy)) {
            continue;
        }
        int dist_agent = chebyshev_distance(p->x, p->y, cx, cy);
        int dist_target = chebyshev_distance(cx, cy, target_x, target_y);
        int hash = tile_hash(cx, cy);
        if (!has_best ||
            dist_agent < best_dist_agent ||
            (dist_agent == best_dist_agent &&
             (dist_target < best_dist_target ||
              (dist_target == best_dist_target && hash < best_hash)))) {
            has_best = 1;
            best_x = cx;
            best_y = cy;
            best_dist_agent = dist_agent;
            best_dist_target = dist_target;
            best_hash = hash;
        }
    }

    if (!has_best) {
        return 0;
    }
    *out_x = best_x;
    *out_y = best_y;
    return 1;
}

static int select_closest_diagonal_tile(Player* p, int target_x, int target_y, int* out_x, int* out_y, const CollisionMap* cmap) {
    int candidates[4][2] = {
        {target_x + 1, target_y + 1},
        {target_x + 1, target_y - 1},
        {target_x - 1, target_y - 1},
        {target_x - 1, target_y + 1}
    };

    int has_best = 0;
    int best_x = 0;
    int best_y = 0;
    int best_dist_agent = 0;
    int best_dist_target = 0;
    int best_hash = 0;

    for (int i = 0; i < 4; i++) {
        int cx = candidates[i][0];
        int cy = candidates[i][1];
        if (!is_in_wilderness(cx, cy)) {
            continue;
        }
        if (!collision_tile_walkable(cmap, 0, cx, cy)) {
            continue;
        }
        int dist_agent = chebyshev_distance(p->x, p->y, cx, cy);
        int dist_target = chebyshev_distance(cx, cy, target_x, target_y);
        int hash = tile_hash(cx, cy);
        if (!has_best ||
            dist_agent < best_dist_agent ||
            (dist_agent == best_dist_agent &&
             (dist_target < best_dist_target ||
              (dist_target == best_dist_target && hash < best_hash)))) {
            has_best = 1;
            best_x = cx;
            best_y = cy;
            best_dist_agent = dist_agent;
            best_dist_target = dist_target;
            best_hash = hash;
        }
    }

    if (!has_best) {
        return 0;
    }
    *out_x = best_x;
    *out_y = best_y;
    return 1;
}

static int select_farcast_tile(Player* p, int target_x, int target_y, int distance, int* out_x, int* out_y, const CollisionMap* cmap) {
    int raw_dx = p->x - target_x;
    int raw_dy = p->y - target_y;
    int d = distance;

    int dx = raw_dx < -d ? -d : (raw_dx > d ? d : raw_dx);
    int dy = raw_dy < -d ? -d : (raw_dy > d ? d : raw_dy);

    int adx = abs_int(dx);
    int ady = abs_int(dy);
    if (adx < d && ady < d) {
        if (adx >= ady) {
            dx = (raw_dx >= 0) ? d : -d;
        } else {
            dy = (raw_dy >= 0) ? d : -d;
        }
    }

    int cx = target_x + dx;
    int cy = target_y + dy;

    if (is_in_wilderness(cx, cy) && collision_tile_walkable(cmap, 0, cx, cy)) {
        *out_x = cx;
        *out_y = cy;
        return 1;
    }

    cx = cx < WILD_MIN_X ? WILD_MIN_X : (cx > WILD_MAX_X ? WILD_MAX_X : cx);
    cy = cy < WILD_MIN_Y ? WILD_MIN_Y : (cy > WILD_MAX_Y ? WILD_MAX_Y : cy);
    if (chebyshev_distance(cx, cy, target_x, target_y) == distance
        && collision_tile_walkable(cmap, 0, cx, cy)) {
        *out_x = cx;
        *out_y = cy;
        return 1;
    }

    return 0;
}

static int pvp_tile_walkable(void* ctx, int x, int y) {
    const CollisionMap* cmap = (const CollisionMap*)ctx;
    return is_in_wilderness(x, y) && collision_tile_walkable(cmap, 0, x, y);
}

static void resolve_same_tile(Player* mover, Player* blocker, const CollisionMap* cmap) {
    if (blocker->frozen_ticks > 0) {
        return;
    }
    if (mover->frozen_ticks > 0) {
        return;
    }

    static const int OFFSETS[8][2] = {
        {-1, 0}, {1, 0}, {0, -1}, {0, 1},
        {-1, -1}, {1, -1}, {-1, 1}, {1, 1}
    };

    for (int i = 0; i < 8; i++) {
        int nx = mover->x + OFFSETS[i][0];
        int ny = mover->y + OFFSETS[i][1];
        if (is_in_wilderness(nx, ny)
            && collision_tile_walkable(cmap, 0, nx, ny)
            && !(nx == blocker->x && ny == blocker->y)) {
            mover->x = nx;
            mover->y = ny;
            mover->dest_x = nx;
            mover->dest_y = ny;
            mover->is_moving = 0;
            return;
        }
    }
}

typedef struct {
    OsrsEnv* env;
    int agent_idx;
    int has_new_target;
    int target_slot;
    AttackStyle style;
    int range;
} PvpAttackMoveIntent;

static int pvp_lookup_attack_target(void* ctx, int target_slot, OsrsAttackTarget* out) {
    if (target_slot < 0 || target_slot >= NUM_AGENTS) return 0;
    PvpAttackMoveIntent* intent = (PvpAttackMoveIntent*)ctx;
    OsrsEnv* env = intent->env;
    Player* target = &env->players[target_slot];
    out->slot = target_slot;
    out->x = target->x;
    out->y = target->y;
    out->size = 1;
    out->attack_range = intent->range;
    out->delivery = (intent->style == ATTACK_STYLE_MELEE || intent->style == ATTACK_STYLE_NONE)
        ? OSRS_ATTACK_DELIVERY_MELEE
        : OSRS_ATTACK_DELIVERY_PROJECTILE;
    return 1;
}

static inline OsrsEncounterArena pvp_build_arena(OsrsEnv* env) {
    OsrsEncounterArena arena;
    arena.collision_map = (const CollisionMap*)env->collision_map;
    arena.world_offset_x = 0;
    arena.world_offset_y = 0;
    arena.is_walkable = pvp_tile_walkable;
    arena.walkable_ctx = (void*)arena.collision_map;
    arena.extra_blocked = NULL;
    arena.blocked_ctx = NULL;
    arena.projectile_occlusion = osrs_projectile_occlusion_collision_map(
        arena.collision_map, 0);
    arena.arena_base_x = 0;
    arena.arena_base_y = 0;
    arena.arena_w = 0;
    arena.arena_h = 0;
    return arena;
}

static inline int pvp_local_pathfind_window(
    const Player* p,
    int dest_x,
    int dest_y,
    int* out_base_x,
    int* out_base_y,
    int* out_w,
    int* out_h
) {
    if (!is_in_wilderness(p->x, p->y) || !is_in_wilderness(dest_x, dest_y))
        return 0;

    int min_x = min_int(p->x, dest_x) - PVP_LOCAL_PATHFIND_MARGIN;
    int min_y = min_int(p->y, dest_y) - PVP_LOCAL_PATHFIND_MARGIN;
    int max_x = max_int(p->x, dest_x) + PVP_LOCAL_PATHFIND_MARGIN;
    int max_y = max_int(p->y, dest_y) + PVP_LOCAL_PATHFIND_MARGIN;

    min_x = max_int(min_x, WILD_MIN_X);
    min_y = max_int(min_y, WILD_MIN_Y);
    max_x = min_int(max_x, WILD_MAX_X);
    max_y = min_int(max_y, WILD_MAX_Y);

    int w = max_x - min_x + 1;
    int h = max_y - min_y + 1;
    if (w <= 0 || h <= 0 || w > PATHFIND_ARENA_MAX || h > PATHFIND_ARENA_MAX)
        return 0;

    *out_base_x = min_x;
    *out_base_y = min_y;
    *out_w = w;
    *out_h = h;
    return 1;
}

static inline void pvp_enable_local_pathfind_for_dest(
    OsrsEncounterArena* arena,
    const Player* p,
    int dest_x,
    int dest_y
) {
    int base_x;
    int base_y;
    int w;
    int h;
    if (!pvp_local_pathfind_window(p, dest_x, dest_y, &base_x, &base_y, &w, &h))
        return;

    arena->arena_base_x = base_x;
    arena->arena_base_y = base_y;
    arena->arena_w = w;
    arena->arena_h = h;
}

static inline int pvp_destination_reachable_this_tick(
    const Player* p,
    int dest_x,
    int dest_y,
    const CollisionMap* cmap
) {
    if (!is_in_wilderness(dest_x, dest_y)) return 0;
    if (!collision_tile_walkable(cmap, 0, dest_x, dest_y)) return 0;

    int base_x;
    int base_y;
    int w;
    int h;
    if (!pvp_local_pathfind_window(p, dest_x, dest_y, &base_x, &base_y, &w, &h))
        return 0;

    Player probe = *p;
    int walk_dest_x = dest_x;
    int walk_dest_y = dest_y;
    int steps = encounter_move_toward_dest(
        &probe,
        &walk_dest_x,
        &walk_dest_y,
        cmap,
        0,
        0,
        pvp_tile_walkable,
        (void*)cmap,
        NULL,
        NULL,
        base_x,
        base_y,
        w,
        h);

    return steps > 0 && probe.x == dest_x && probe.y == dest_y;
}

static inline OsrsPlayerStepResult pvp_step_player_movement(
    OsrsEnv* env,
    int agent_idx,
    PvpAttackMoveIntent intent
) {
    OsrsPlayerStepResult result = {.target_slot = -1};
    int* dest_x = &env->pvp_runtime.walk_dest_x[agent_idx];
    int* dest_y = &env->pvp_runtime.walk_dest_y[agent_idx];
    Player* p = &env->players[agent_idx];

    if (intent.has_new_target) {
        *dest_x = -1;
        *dest_y = -1;
    }

    int has_dest = *dest_x >= 0 && *dest_y >= 0;
    if (!has_dest && !intent.has_new_target &&
            !osrs_interaction_active(&p->interaction)) {
        return result;
    }

    OsrsEncounterArena arena = pvp_build_arena(env);
    if (has_dest)
        pvp_enable_local_pathfind_for_dest(&arena, p, *dest_x, *dest_y);
    intent.env = env;
    intent.agent_idx = agent_idx;
    OsrsPlayerStepInput input = {
        .player = p,
        .interaction = &p->interaction,
        .target_lookup = pvp_lookup_attack_target,
        .target_ctx = &intent,
        .has_new_target = intent.has_new_target,
        .new_target_slot = intent.target_slot,
        .move_kind = has_dest ? OSRS_PLAYER_MOVE_DESTINATION : OSRS_PLAYER_MOVE_NONE,
        .target_move_policy = OSRS_PLAYER_TARGET_MOVE_EXPLICIT_FIRST,
        .move_action = 0,
        .dest_x = dest_x,
        .dest_y = dest_y,
        .blocked_ticks = p->frozen_ticks,
        .arena = arena,
    };
    result = osrs_encounter_player_step(&input);
    if (result.chased_target) p->target_click_chase_ticks++;
    if (result.explicit_moved) p->explicit_move_ticks++;
    p->is_moving = (*dest_x >= 0) ? 1 : 0;
    return result;
}

static inline void pvp_set_walk_dest_from_head_move(OsrsEnv* env, int agent_idx, int move_action) {
    Player* p = &env->players[agent_idx];
    if (move_action <= 0 || move_action >= MOVE_DIM) return;
    env->pvp_runtime.walk_dest_x[agent_idx] = p->x + ENCOUNTER_MOVE_TARGET_DX[move_action];
    env->pvp_runtime.walk_dest_y[agent_idx] = p->y + ENCOUNTER_MOVE_TARGET_DY[move_action];
}

static inline int pvp_select_legacy_target_move_destination(
    OsrsEnv* env,
    int agent_idx,
    int legacy_move,
    const CollisionMap* cmap,
    int* dest_x,
    int* dest_y
) {
    Player* p = &env->players[agent_idx];
    int tx = p->last_obs_target_x;
    int ty = p->last_obs_target_y;
    *dest_x = -1;
    *dest_y = -1;

    switch (legacy_move) {
        case MOVE_ADJACENT:
            return select_closest_adjacent_tile(p, tx, ty, dest_x, dest_y, cmap);
        case MOVE_UNDER:
            if (is_in_wilderness(tx, ty) && collision_tile_walkable(cmap, 0, tx, ty)) {
                *dest_x = tx;
                *dest_y = ty;
                return 1;
            }
            return 0;
        case MOVE_DIAGONAL:
            return select_closest_diagonal_tile(p, tx, ty, dest_x, dest_y, cmap);
        case MOVE_FARCAST_2:
        case MOVE_FARCAST_3:
        case MOVE_FARCAST_4:
        case MOVE_FARCAST_5:
        case MOVE_FARCAST_6:
        case MOVE_FARCAST_7: {
            int distance = legacy_move - MOVE_FARCAST_2 + 2;
            return select_farcast_tile(p, tx, ty, distance, dest_x, dest_y, cmap);
        }
        default:
            return 0;
    }
}

static inline int pvp_set_walk_dest_from_legacy_target_move(
    OsrsEnv* env,
    int agent_idx,
    int legacy_move,
    const CollisionMap* cmap
) {
    int dest_x = -1;
    int dest_y = -1;
    if (!pvp_select_legacy_target_move_destination(
            env, agent_idx, legacy_move, cmap, &dest_x, &dest_y)) {
        return 0;
    }

    env->pvp_runtime.walk_dest_x[agent_idx] = dest_x;
    env->pvp_runtime.walk_dest_y[agent_idx] = dest_y;
    return 1;
}

static inline int pvp_head_move_toward_tile(const Player* p, int dest_x, int dest_y) {
    int dx = dest_x - p->x;
    int dy = dest_y - p->y;
    if (dx < -2) dx = -2;
    if (dx > 2) dx = 2;
    if (dy < -2) dy = -2;
    if (dy > 2) dy = 2;

    for (int action = 1; action < MOVE_DIM; action++) {
        if (ENCOUNTER_MOVE_TARGET_DX[action] == dx &&
                ENCOUNTER_MOVE_TARGET_DY[action] == dy) {
            return action;
        }
    }
    return 0;
}

static inline int pvp_head_move_from_legacy_target_move(
    OsrsEnv* env,
    int agent_idx,
    int legacy_move,
    const CollisionMap* cmap
) {
    int dest_x = -1;
    int dest_y = -1;
    if (!pvp_select_legacy_target_move_destination(
            env, agent_idx, legacy_move, cmap, &dest_x, &dest_y)) {
        return 0;
    }

    return pvp_head_move_toward_tile(&env->players[agent_idx], dest_x, dest_y);
}

#endif // OSRS_PVP_MOVEMENT_H
