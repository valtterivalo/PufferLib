#include "../encounters/encounter_nh_pvp.h"
#include "osrs_test_check.h"

static void check_spawn_routes(OsrsEnv* env, const EncounterArenaTopology* topology) {
    CHECK("spawn actors occupy distinct tiles", env->players[0].x != env->players[1].x || env->players[0].y != env->players[1].y);
    CHECK("spawn actors remain nearby", chebyshev_distance(env->players[0].x, env->players[0].y,
        env->players[1].x, env->players[1].y) <= FIGHT_NEARBY_RADIUS);
    for (int actor = 0; actor < NUM_AGENTS; actor++) {
        Player* player = &env->players[actor];
        Player* target = &env->players[1 - actor];
        int start_x = player->x;
        int start_y = player->y;
        CHECK("spawn tile is inside the supplied arena", encounter_arena_topology_contains(topology, start_x, start_y));
        CHECK("spawn tile is walkable", !encounter_arena_topology_tile_blocked(topology, start_x, start_y));
        EncounterRouteInput input = {.topology = topology, .source_x = start_x, .source_y = start_y,
            .actor_size = 1, .target_x = target->x, .target_y = target->y, .target_size = 1,
            .target_kind = ENCOUNTER_ROUTE_TARGET_TILE, .movement_mode = ENCOUNTER_ROUTE_MOVEMENT_RUN,
            .cost_policy = ENCOUNTER_ROUTE_COST_SOUTH_FIRST_BFS};
        EncounterRouteResult route = encounter_route_solve(&input);
        CHECK("spawn actors can route to each other", route.outcome == ROUTE_REACHED_TARGET && route.distance > 0);
        env->pvp_runtime.walk_dest_x[actor] = target->x;
        env->pvp_runtime.walk_dest_y[actor] = target->y;
        OsrsActorRouteCache cache = {0};
        OsrsPlayerStepResult moved = pvp_step_player_movement(env, actor, topology, &cache);
        CHECK("actual movement leaves each spawn", moved.moved && (player->x != start_x || player->y != start_y));
        player->x = start_x;
        player->y = start_y;
    }
}

static uint32_t fixture_flags(void* context, int x, int y) {
    const int* blocked = context;
    return x == blocked[0] && (blocked[1] < 0 || y == blocked[1]) ? COLLISION_BLOCKED : 0;
}

int main(void) {
    NhPvpState* left = (NhPvpState*)ENCOUNTER_NH_PVP.create();
    NhPvpState* right = (NhPvpState*)ENCOUNTER_NH_PVP.create();
    NhPvpContext context;
    ENCOUNTER_NH_PVP.init_context((EncounterContext*)&context);
    ENCOUNTER_NH_PVP.finalize_context((EncounterState*)left, (EncounterContext*)&context);
    for (uint32_t seed = 1; seed <= 256; seed++) {
        ENCOUNTER_NH_PVP.reset((EncounterState*)left, (EncounterContext*)&context, seed);
        ENCOUNTER_NH_PVP.reset((EncounterState*)right, (EncounterContext*)&context, seed);
        CHECK("seeded reset reproduces both spawn positions", left->env.players[0].x == right->env.players[0].x &&
            left->env.players[0].y == right->env.players[0].y && left->env.players[1].x == right->env.players[1].x &&
            left->env.players[1].y == right->env.players[1].y);
        CHECK("seeded reset reproduces post-reset RNG", left->env.rng_state == right->env.rng_state);
        check_spawn_routes(&left->env, context.route_topology);
        left->env.has_rng_seed = 0;
        left->env.rng_state = seed;
        pvp_reset(&left->env, context.route_topology);
        check_spawn_routes(&left->env, context.route_topology);
    }
    int blocked[] = {FIGHT_AREA_BASE_X + 21, FIGHT_AREA_BASE_Y + 1};
    EncounterArenaTopologyBuildSpec spec = {.origin_x = FIGHT_AREA_BASE_X + 20, .origin_y = FIGHT_AREA_BASE_Y,
        .width = 4, .height = 4, .max_footprint_size = 1, .revision = 1,
        .tile_flags = fixture_flags, .tile_flags_ctx = blocked,
        .los_build_mode = ENCOUNTER_ARENA_TOPOLOGY_LOS_BUILD_OPEN};
    EncounterArenaTopology* alternate = encounter_arena_topology_build(&spec);
    encounter_arena_topology_finalize(alternate);
    for (uint32_t seed = 1; seed <= 32; seed++) {
        left->env.has_rng_seed = 1;
        left->env.rng_seed = seed;
        pvp_reset(&left->env, alternate);
        check_spawn_routes(&left->env, alternate);
    }
    free(alternate);
    blocked[0] = FIGHT_AREA_BASE_X + 25;
    blocked[1] = -1;
    spec.width = 12;
    spec.revision = 2;
    alternate = encounter_arena_topology_build(&spec);
    encounter_arena_topology_finalize(alternate);
    for (uint32_t seed = 1; seed <= 32; seed++) {
        left->env.has_rng_seed = 1;
        left->env.rng_seed = seed;
        pvp_reset(&left->env, alternate);
        check_spawn_routes(&left->env, alternate);
    }
    free(alternate);
    ENCOUNTER_NH_PVP.destroy((EncounterState*)left);
    ENCOUNTER_NH_PVP.destroy((EncounterState*)right);
    return osrs_test_summary();
}
