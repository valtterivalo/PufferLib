#define main existing_actions_main
#include "test_nh_pvp_actions.c"
#undef main

static void test_range_entry(int actor, int pid, int primary, int distance) {
    reset_case(actor, pid, ITEM_RUNE_CROSSBOW);
    state.env.players[actor].x = origin_x;
    state.env.players[1 - actor].x = origin_x + distance;
    int actions[NH_PVP_NUM_ACTION_HEADS] = {0};
    actions[NH_PVP_HEAD_PRIMARY] = primary;
    step_case(actor, actions);
    Player* player = &state.env.players[actor];
    CHECK("attack approach consumes both run steps", player->x == origin_x + 2);
    CHECK("ready attack launches on entering range", player->just_attacked);
    CHECK("range entry launches requested attack", player->attack_style_this_tick ==
        (primary == NH_PVP_PRIMARY_WEAPON ? ATTACK_STYLE_RANGED : ATTACK_STYLE_MAGIC));
}

static void test_death_at_horizon(int actor, int pid, int damage) {
    reset_case(actor, pid, ITEM_RUNE_CROSSBOW);
    state.env.tick = MAX_EPISODE_TICKS - 1;
    Player* player = &state.env.players[actor];
    Player* target = &state.env.players[1 - actor];
    player->current_hitpoints = 0;
    target->current_hitpoints = 20;
    queue_hit(state.env.tick, actor, 1 - actor, player, target, damage,
        ATTACK_STYLE_RANGED, 2, 0, 1, 0, 0, 0, 0, 0);
    int idle[NH_PVP_NUM_ACTION_HEADS] = {0};
    step_case(actor, idle);
    CHECK("horizon preserves already launched death trade", !state.env.episode_over);
    step_case(actor, idle);
    CHECK("outgoing hit remains pending until its landing tick", !state.env.episode_over);
    step_case(actor, idle);
    CHECK("death trade settles at horizon", state.env.episode_over);
    CHECK("settled outcome is death or mutual death", state.env.winner ==
        (damage >= 20 ? -1 : 1 - actor));
}

static uint32_t corner_flags(void* data, int x, int y) {
    (void)data;
    return x == origin_x + 1 && y >= origin_y - 1 && y <= origin_y + 1
        ? COLLISION_BLOCKED | LOS_FULL_MASK : 0;
}

static void test_melee_corner(int actor, int pid, const EncounterArenaTopology* corner) {
    reset_case(actor, pid, ITEM_WHIP);
    state.env.players[1 - actor].x = origin_x + 4;
    const EncounterArenaTopology* saved = context.route_topology;
    context.route_topology = corner;
    int actions[NH_PVP_NUM_ACTION_HEADS] = {0};
    actions[NH_PVP_HEAD_PRIMARY] = NH_PVP_PRIMARY_WEAPON;
    step_case(actor, actions);
    Player* player = &state.env.players[actor];
    CHECK("melee approach routes around a reachable corner", player->x != origin_x || player->y != origin_y);
    CHECK("melee approach respects corner collision", !encounter_arena_topology_tile_blocked(corner, player->x, player->y));
    CHECK("blocked melee attack does not fire through wall", !player->just_attacked);
    context.route_topology = saved;
}

static uint32_t diagonal_flags(void* data, int x, int y) {
    (void)data;
    return x == origin_x + 1 && y == origin_y + 1 ? COLLISION_BLOCKED | LOS_FULL_MASK : 0;
}

static void test_ground_route(int actor, int pid, const EncounterArenaTopology* corner) {
    reset_case(actor, pid, ITEM_WHIP);
    const EncounterArenaTopology* saved = context.route_topology;
    context.route_topology = corner;
    int actions[NH_PVP_NUM_ACTION_HEADS] = {0};
    for (int action = 1; action < OSRS_PRIMARY_MOVE_ACTIONS; action++)
        if (ENCOUNTER_MOVE_TARGET_DX[action] == 2 && ENCOUNTER_MOVE_TARGET_DY[action] == 2)
            actions[NH_PVP_HEAD_PRIMARY] = action;
    CHECK("ground fixture submits northeast destination", actions[NH_PVP_HEAD_PRIMARY] != 0);
    step_case(actor, actions);
    Player* player = &state.env.players[actor];
    CHECK("equal ground routes follow west east south north BFS priority", player->x == origin_x + 2 && player->y == origin_y);
    CHECK("ground route keeps its reachable destination", state.env.pvp_runtime.walk_dest_x[actor] == origin_x + 2 && state.env.pvp_runtime.walk_dest_y[actor] == origin_y + 2);
    context.route_topology = saved;
}

int main(void) {
    nh_pvp_init_state((EncounterState*)&state, NULL);
    nh_pvp_init_context((EncounterContext*)&context);
    nh_pvp_finalize_context((EncounterState*)&state, (EncounterContext*)&context);
    const EncounterArenaTopology* topology = context.route_topology;
    for (int x = topology->origin_x; x < topology->origin_x + topology->width; x++)
        for (int y = topology->origin_y; y < topology->origin_y + topology->height; y++)
            if (origin_x == 0 && encounter_arena_topology_player_can_attack(topology, x, y, x + 12, y, 1, 12) &&
                    encounter_arena_topology_step_allowed(topology, x, y, 1, 0, 1) &&
                    encounter_arena_topology_step_allowed(topology, x + 1, y, 1, 0, 1)) {
                origin_x = x;
                origin_y = y;
            }
    assert(origin_x != 0);
    EncounterArenaTopologyBuildSpec spec = {.origin_x = origin_x - 2, .origin_y = origin_y - 2,
        .width = 16, .height = 8, .max_footprint_size = 1, .revision = 2,
        .tile_flags = corner_flags, .los_build_mode = ENCOUNTER_ARENA_TOPOLOGY_LOS_BUILD_FLAGGED};
    EncounterArenaTopology* corner = encounter_arena_topology_build(&spec);
    encounter_arena_topology_finalize(corner);
    spec.tile_flags = diagonal_flags;
    spec.revision = 3;
    EncounterArenaTopology* diagonal = encounter_arena_topology_build(&spec);
    encounter_arena_topology_finalize(diagonal);
    for (int actor = 0; actor < NUM_AGENTS; actor++)
        for (int pid = 0; pid < NUM_AGENTS; pid++) {
            test_range_entry(actor, pid, NH_PVP_PRIMARY_ICE, COMBAT_SPELL_ATTACK_RANGE + 2);
            test_range_entry(actor, pid, NH_PVP_PRIMARY_BLOOD, COMBAT_SPELL_ATTACK_RANGE + 1);
            test_range_entry(actor, pid, NH_PVP_PRIMARY_WEAPON, get_attack_range(&state.env.players[actor], ATTACK_STYLE_RANGED) + 1);
            test_death_at_horizon(actor, pid, 5);
            test_death_at_horizon(actor, pid, 20);
            test_melee_corner(actor, pid, corner);
            test_ground_route(actor, pid, diagonal);
        }
    free(corner);
    free(diagonal);
    return osrs_test_summary();
}
