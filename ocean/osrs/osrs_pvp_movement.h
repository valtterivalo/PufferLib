#ifndef OSRS_PVP_MOVEMENT_H
#define OSRS_PVP_MOVEMENT_H

#include "osrs_types.h"
#include "osrs_collision.h"
#include "osrs_encounter.h"
#include "osrs_encounter_player.h"
#include "osrs_pvp_gear.h"

static inline int pvp_topology_destination_selectable(
    const EncounterArenaTopology* topology,
    int x,
    int y
) {
    if (!is_in_wilderness(x, y)) return 0;
    if (encounter_arena_topology_contains(topology, x, y))
        return !encounter_arena_topology_tile_blocked(topology, x, y);

    int min_x = topology->origin_x;
    int max_x = min_x + topology->width - 1;
    int min_y = topology->origin_y;
    int max_y = min_y + topology->height - 1;
    int fallback_x = x < min_x ? min_x : (x > max_x ? max_x : x);
    int fallback_y = y < min_y ? min_y : (y > max_y ? max_y : y);
    return !encounter_arena_topology_tile_blocked(
        topology, fallback_x, fallback_y);
}

typedef struct {
    EncounterArenaTopology* topology;
    OsrsLocalMoveRoute
        local_move_routes[FIGHT_AREA_WIDTH * FIGHT_AREA_HEIGHT]
            [OSRS_PRIMARY_MOVE_ACTIONS];
    int local_move_routes_ready;
} PvpRouteTopologyOwner;

static PvpRouteTopologyOwner pvp_route_topology_owner;

static const uint8_t PVP_MOVE_ACTION_BY_DELTA[25] = {
    9, 10, 11, 12, 13,
    14, 1, 2, 3, 15,
    16, 4, 0, 5, 17,
    18, 6, 7, 8, 19,
    20, 21, 22, 23, 24,
};

#define PVP_SCENE "wilderness"

static uint32_t pvp_route_topology_flags(void* data, int x, int y) {
    if (!is_in_wilderness(x, y)) return COLLISION_BLOCKED | LOS_FULL_MASK;
    return collision_scene_flags((const CollisionMap*)data, 0, 0, x, y);
}

static void pvp_local_move_routes_build(PvpRouteTopologyOwner* owner) {
    const EncounterArenaTopology* topology = owner->topology;
    for (int x = topology->origin_x;
            x < topology->origin_x + topology->width;
            x++) {
        for (int y = topology->origin_y;
                y < topology->origin_y + topology->height;
                y++) {
            int source_index =
                encounter_arena_topology_index_raw(topology, x, y);
            for (int action = 1; action < OSRS_PRIMARY_MOVE_ACTIONS; action++) {
                EncounterRouteInput input = {
                    .topology = topology,
                    .source_x = x,
                    .source_y = y,
                    .actor_size = 1,
                    .target_x = x + ENCOUNTER_MOVE_TARGET_DX[action],
                    .target_y = y + ENCOUNTER_MOVE_TARGET_DY[action],
                    .target_size = 1,
                    .target_kind = ENCOUNTER_ROUTE_TARGET_TILE,
                    .movement_mode = ENCOUNTER_ROUTE_MOVEMENT_RUN,
                    .cost_policy = ENCOUNTER_ROUTE_COST_OSRS,
                };
                EncounterRouteResult route = encounter_route_solve(&input);
                owner->local_move_routes[source_index][action] =
                    (OsrsLocalMoveRoute){
                        .destination_dx =
                            (int16_t)(route.destination_x - x),
                        .destination_dy =
                            (int16_t)(route.destination_y - y),
                        .first_dx = (int8_t)route.first_dx,
                        .first_dy = (int8_t)route.first_dy,
                        .run_dx = (int8_t)route.run_dx,
                        .run_dy = (int8_t)route.run_dy,
                        .distance = route.distance,
                        .outcome = (uint8_t)route.outcome,
                    };
            }
        }
    }
    owner->local_move_routes_ready = 1;
}

static int pvp_local_move_route_lookup(
    const void* data,
    const EncounterRouteInput* input,
    EncounterRouteResult* result
) {
    const PvpRouteTopologyOwner* owner =
        (const PvpRouteTopologyOwner*)data;
    if (!owner || !input || !result) abort();
    if (!owner->local_move_routes_ready ||
            input->topology != owner->topology ||
            input->blockers.is_blocked ||
            input->actor_size != 1 ||
            input->target_size != 1 ||
            input->target_kind != ENCOUNTER_ROUTE_TARGET_TILE ||
            input->movement_mode != ENCOUNTER_ROUTE_MOVEMENT_RUN ||
            input->cost_policy != ENCOUNTER_ROUTE_COST_OSRS ||
            !encounter_arena_topology_contains(
                input->topology, input->source_x, input->source_y))
        return 0;
    int dx = input->target_x - input->source_x;
    int dy = input->target_y - input->source_y;
    if (dx < -2 || dx > 2 || dy < -2 || dy > 2) return 0;
    int action = PVP_MOVE_ACTION_BY_DELTA[(dx + 2) * 5 + dy + 2];
    if (action == 0) return 0;
    int source_index = encounter_arena_topology_index_raw(
        input->topology, input->source_x, input->source_y);
    const OsrsLocalMoveRoute* route =
        &owner->local_move_routes[source_index][action];
    *result = (EncounterRouteResult){
        .outcome = (EncounterRouteOutcome)route->outcome,
        .destination_x = input->source_x + route->destination_dx,
        .destination_y = input->source_y + route->destination_dy,
        .first_dx = route->first_dx,
        .first_dy = route->first_dy,
        .run_dx = route->run_dx,
        .run_dy = route->run_dy,
        .distance = route->distance,
    };
    return 1;
}


static inline int pvp_topology_tile_walkable(
    const EncounterArenaTopology* topology,
    int x,
    int y
) {
    return !encounter_arena_topology_tile_blocked(topology, x, y);
}

static const EncounterArenaTopology* pvp_route_topology_finalize(void) {
    EncounterArenaTopologyBuildSpec spec = {
        .origin_x = FIGHT_AREA_BASE_X,
        .origin_y = FIGHT_AREA_BASE_Y,
        .width = FIGHT_AREA_WIDTH,
        .height = FIGHT_AREA_HEIGHT,
        .max_footprint_size = 1,
        .revision = UINT64_C(0x4e48505650000006),
        .tile_flags = pvp_route_topology_flags,
        .tile_flags_ctx = (void*)collision_map_scene(PVP_SCENE),
        .los_build_mode = ENCOUNTER_ARENA_TOPOLOGY_LOS_BUILD_FLAGGED,
    };
    if (!pvp_route_topology_owner.topology) {
        pvp_route_topology_owner.topology =
            encounter_arena_topology_build(&spec);
        encounter_arena_topology_finalize(pvp_route_topology_owner.topology);
        pvp_local_move_routes_build(&pvp_route_topology_owner);
    } else {
        encounter_arena_topology_require_spec(
            pvp_route_topology_owner.topology,
            &spec,
            "nh_pvp");
    }
    return pvp_route_topology_owner.topology;
}



/* skipped when either player is frozen: walking under a frozen opponent is a
   legal, intentional position in OSRS PvP */
static void resolve_same_tile(
    Player* mover,
    Player* blocker,
    const EncounterArenaTopology* topology
) {
    if (blocker->frozen_ticks > 0 || mover->frozen_ticks > 0) return;

    static const int OFFSETS[8][2] = {
        {-1, 0}, {1, 0}, {0, -1}, {0, 1},
        {-1, -1}, {1, -1}, {-1, 1}, {1, 1}
    };

    for (int i = 0; i < 8; i++) {
        int nx = mover->x + OFFSETS[i][0];
        int ny = mover->y + OFFSETS[i][1];
        if (pvp_topology_tile_walkable(topology, nx, ny) &&
                !(nx == blocker->x && ny == blocker->y)) {
            mover->x = nx;
            mover->y = ny;
            mover->dest_x = nx;
            mover->dest_y = ny;
            mover->is_moving = 0;
            return;
        }
    }
}

static int pvp_lookup_attack_target(void* ctx, int target_slot, OsrsAttackTarget* out) {
    if (target_slot < 0 || target_slot >= NUM_AGENTS) return 0;
    OsrsEnv* env = (OsrsEnv*)ctx;
    Player* target = &env->players[target_slot];
    Player* self = &env->players[1 - target_slot];
    AttackStyle style = pvp_weapon_attack_style(self);
    int range = get_attack_range(self, style);
    out->slot = target_slot;
    out->x = target->x;
    out->y = target->y;
    out->size = 1;
    out->attack_range = range;
    return 1;
}

static inline OsrsPlayerStepResult pvp_step_player_movement(
    OsrsEnv* env,
    int agent_idx,
    const EncounterArenaTopology* topology,
    OsrsActorRouteCache* route_cache
) {
    OsrsPlayerStepResult result = {.target_slot = -1};
    int* dest_x = &env->pvp_runtime.walk_dest_x[agent_idx];
    int* dest_y = &env->pvp_runtime.walk_dest_y[agent_idx];

    if (*dest_x < 0 || *dest_y < 0) return result;

    Player* p = &env->players[agent_idx];
    if (p->frozen_ticks <= 0) {
        osrs_interaction_check_interrupt(&p->interaction, OSRS_IACT_MOVE);
        if (p->x == *dest_x && p->y == *dest_y) {
            *dest_x = -1;
            *dest_y = -1;
            p->is_moving = 0;
            return result;
        }
        EncounterRouteInput route_input = {
            .topology = topology,
            .source_x = p->x,
            .source_y = p->y,
            .actor_size = 1,
            .target_x = *dest_x,
            .target_y = *dest_y,
            .target_size = 1,
            .target_kind = ENCOUNTER_ROUTE_TARGET_TILE,
            .movement_mode = ENCOUNTER_ROUTE_MOVEMENT_RUN,
            .cost_policy = ENCOUNTER_ROUTE_COST_OSRS,
        };
        EncounterRouteResult route;
        if (pvp_local_move_route_lookup(
                &pvp_route_topology_owner, &route_input, &route)) {
            result.moved = osrs_player_step_apply_route(p, &route) > 0;
            result.explicit_moved = result.moved;
            p->is_moving = 1;
            return result;
        }
    }
    OsrsEncounterArena arena = {
        .topology = topology,
        .blockers = {0},
        .movement_mode = ENCOUNTER_ROUTE_MOVEMENT_RUN,
        .cost_policy = ENCOUNTER_ROUTE_COST_OSRS_TARGET_BFS,
        .destination_cost_policy = ENCOUNTER_ROUTE_COST_OSRS,
        .attack_geometry = ENCOUNTER_ROUTE_ATTACK_GEOMETRY_TOPOLOGY,
    };
    OsrsPlayerStepInput input = {
        .player = p,
        .interaction = &p->interaction,
        .route_cache = route_cache,
        .target_lookup = pvp_lookup_attack_target,
        .target_ctx = env,
        .command = {
            .kind = OSRS_PLAYER_CMD_MOVE,
            .move_kind = OSRS_PLAYER_MOVE_DESTINATION,
        },
        .dest_x = dest_x,
        .dest_y = dest_y,
        .blocked_ticks = p->frozen_ticks,
        .arena = arena,
    };
    result = osrs_encounter_player_step(&input);
    p->is_moving = (*dest_x >= 0) ? 1 : 0;
    return result;
}

static inline void pvp_set_walk_dest_from_head_move(OsrsEnv* env, int agent_idx, int move_action) {
    Player* p = &env->players[agent_idx];
    if (move_action <= 0 || move_action >= OSRS_PRIMARY_MOVE_ACTIONS) return;
    env->pvp_runtime.walk_dest_x[agent_idx] = p->x + ENCOUNTER_MOVE_TARGET_DX[move_action];
    env->pvp_runtime.walk_dest_y[agent_idx] = p->y + ENCOUNTER_MOVE_TARGET_DY[move_action];
}

#endif // OSRS_PVP_MOVEMENT_H
