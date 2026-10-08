#include "../../osrs_pvp/osrs_pvp.h"
#include "osrs_test_check.h"

static Env env;
static float observations[2][OBS_SIZE], actions[2][NUM_ATNS], rewards[2], terminals[2];
static unsigned char masks[2][NH_PVP_ACTION_MASK_SIZE];

static void reset(int self_play) {
    memset(&env, 0, sizeof(env));
    memset(actions, 0, sizeof(actions));
    DictItem items[] = {
        {.key = "opponent_type", .value = OPP_IMPROVED},
        {.key = "gear_tier", .value = 3},
        {.key = "is_lms", .value = 1},
        {.key = "use_c_opponent", .value = !self_play},
        {.key = "self_play", .value = self_play},
        {.key = "auto_reset", .value = 0},
        {.key = "shaping_enabled", .value = 0},
        {.key = "shaping_scale", .value = 0},
    };
    Dict kwargs = {.items = items, .size = sizeof(items) / sizeof(*items)};
    env.rng = 123;
    puf_init(&env, &kwargs);
    for (int i = 0; i < env.num_agents; i++) env.agents[i] = (Agent){
        .observations = observations[i], .actions = actions[i], .rewards = &rewards[i],
        .terminals = &terminals[i], .action_mask = masks[i], .policy = i,
    };
    puf_reset(&env);
}

int main(void) {
    reset(1);
    CHECK("both players have policy slots", env.num_agents == 2 && env.agents[1].policy == 1);
    for (int i = 0; i < 2; i++) {
        Player* player = &env.state.env.players[i];
        player->x = FIGHT_AREA_BASE_X + 10 + 5 * i;
        player->y = FIGHT_AREA_BASE_Y + 10;
    }
    actions[1][OSRS_HEAD_PRIMARY] = 1;
    puf_step(&env);
    CHECK("second policy actions reach engine", env.state.env.last_executed_actions[NUM_ATNS] == 1);
    for (int i = 0; i < 2; i++) {
        unsigned char expected[NH_PVP_ACTION_MASK_SIZE];
        pvp_write_action_mask_bytes(expected, &env.state.env, i, env.context.route_topology);
        CHECK("each policy receives its own mask", memcmp(expected, masks[i], sizeof(expected)) == 0);
    }
    float before[OBS_SIZE], after[OBS_SIZE];
    pvp_write_observations(before, &env.state.env, 0);
    Player* opponent = &env.state.env.players[1];
    opponent->current_hitpoints = 42;
    opponent->current_prayer = 7;
    opponent->special_energy = 0;
    opponent->attack_timer = 6;
    opponent->food_timer = 2;
    opponent->potion_timer = 2;
    opponent->frozen_ticks = 31;
    opponent->freeze_immunity_ticks = 4;
    opponent->veng_active = 1;
    opponent->veng_cooldown = 49;
    opponent->offensive_prayer = OFFENSIVE_PRAYER_PIETY;
    opponent->num_pending_hits = 1;
    opponent->pending_hits[0].damage = 70;
    env.state.env.players[0].num_pending_hits = 1;
    env.state.env.players[0].pending_hits[0].damage = 50;
    pvp_write_observations(after, &env.state.env, 0);
    CHECK("hidden opponent state and rolled damage do not change observations",
        memcmp(before, after, sizeof(before)) == 0);
    CHECK("health bar stays stale until a hit", after[OSRS_SHARED_OBS_SIZE] == 1);
    PendingHit hit = {.kind = OSRS_HIT_DIRECT};
    pvp_observe_hit(&env.state.env, 0, 1, &hit, 0, 42);
    pvp_write_observations(after, &env.state.env, 0);
    CHECK("visible hit refreshes quantized health bar", after[OSRS_SHARED_OBS_SIZE] == 13.0f / 30.0f);
    puf_close(&env);

    reset(1);
    env.state.env.players[1].morr_dot_remaining = 5;
    env.state.env.players[1].morr_dot_tick_counter = 1;
    puf_step(&env);
    CHECK("bleed refreshes visible health bar", env.state.env.players[1].current_hitpoints == 94 &&
        observations[0][OSRS_SHARED_OBS_SIZE] == 28.0f / 30.0f);
    puf_close(&env);

    reset(1);
    env.state.env.tick = MAX_EPISODE_TICKS - 1;
    puf_step(&env);
    CHECK("timeout is a draw for both policies", terminals[0] == 1 && terminals[1] == 1 &&
        rewards[0] == 0 && rewards[1] == 0 && env.log.draw_rate == 1);
    CHECK("timeout gives no selection credit", env.log.wins == 0);
    env.state.env.players[1].current_hitpoints = 0;
    puf_step(&env);
    CHECK("a kill credits the winning policy", rewards[0] == 1 && rewards[1] == 0 && env.log.wins == 1);
    CHECK("terminal resets both policy observations", env.state.env.tick == 0 &&
        observations[0][0] == 1 && observations[1][0] == 1);
    puf_close(&env);

    reset(0);
    CHECK("bot evaluation uses one policy", env.num_agents == 1);
    puf_set_bot_policy(&env, OPP_EXPERT_NH);
    puf_reset(&env);
    CHECK("ladder opponent survives reset", env.state.env.pvp_runtime.opponent.type == OPP_EXPERT_NH);
    env.log.wins = 0;
    env.log.damage_dealt = 99;
    Dict output = {0};
    puf_log(&env.log, &output);
    CHECK("damage without a win earns no selection score", dict_get(&output, "perf") == 0);
    dict_clear(&output);
    puf_close(&env);
    return osrs_test_summary();
}
