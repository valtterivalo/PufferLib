#include "../../osrs_riskfight/osrs_riskfight.h"
#include <assert.h>

static Env* env;
/** The wait between two fights is steps of its own: steps through them, to the first tick of the next fight. */
static void wait_out(void) { while (env->wait_left > 0) puf_step(env); }

int main(void) {
    DictItem items[] = {{.key = "opponent_type", .value = 0}, {.key = "self_play", .value = 1},
        {.key = "session_ticks", .value = 100000}, {.key = "curveball_probability", .value = 0}};
    Dict kwargs = {.items = items, .size = sizeof(items) / sizeof(*items)};
    env = (Env*)calloc(1, sizeof(*env));
    float obs[2][RF_OBS_SIZE], actions[2][RF_HEADS] = {{0}}, rewards[2], terminals[2];
    unsigned char masks[2][RF_MASK_SIZE];
    env->rng = 123;
    puf_init(env, &kwargs);
    assert(env->num_agents == 2 && env->agents[0].policy == 0 && env->agents[1].policy == 1);
    for (int i = 0; i < 2; i++) env->agents[i] = (Agent){
        .observations = obs[i], .actions = actions[i], .rewards = &rewards[i],
        .terminals = &terminals[i], .action_mask = masks[i], .policy = i};
    puf_reset(env);
    actions[0][RF_PRIMARY] = RF_TELEPORT;
    puf_step(env);
    /* A teleport ends the fight, not the session: the next fight starts at once from our side's view (refilled, tick 0). */
    assert(terminals[0] == 0 && terminals[1] == 0 && rewards[0] == 0 && rewards[1] == 0);
    wait_out(); /* the restock is a wait of its own ticks */
    assert(env->state.env.tick == 0 && env->log.escapes == 1);
    assert(env->log.self_teleports == 1 && env->log.opponent_teleports == 0);
    assert(env->log.self_escape_healing[OSRS_ESCAPE_TRIPLE_EATS] == 1);
    assert(obs[0][RF_OPPONENT_START + NUM_GEAR_SLOTS + 1] * RF_OBSERVATION_TILE_SCALE == 1);
    assert(obs[1][RF_OPPONENT_START + NUM_GEAR_SLOTS + 1] * RF_OBSERVATION_TILE_SCALE == -1);
    for (int i = 0; i < 2; i++) {
        unsigned char expected[RF_MASK_SIZE];
        riskfight_write_action_mask(&env->state, i, expected);
        for (int j = 0; j < RF_MASK_SIZE; j++) assert(masks[i][j] == expected[j]);
    }
    assert(env->log.policy_0_score == 0.5f && env->log.draw_rate == 1);
    memset(&env->log, 0, sizeof(env->log));
    actions[0][RF_PRIMARY] = 0;
    env->state.env.players[1].current_hitpoints = 0;
    puf_step(env);
    assert(env->log.policy_0_score == 1 && env->log.draw_rate == 0);
    assert(env->log.kills == 1 && env->session_stake == 1);
    wait_out();
    env->state.env.players[0].current_hitpoints = 0;
    puf_step(env);
    assert(env->log.policy_0_score == 1 && env->log.deaths == 1 && env->session_stake == 0);
    wait_out();
    env->state.env.players[0].current_hitpoints = 0;
    env->state.env.players[1].current_hitpoints = 0;
    puf_step(env);
    assert(env->log.policy_0_score == 1.5f && env->log.draw_rate == 1);
    wait_out();
    Dict output = {0};
    puf_log(&env->log, &output);
    assert(dict_get(&output, "policy_0_score") == 1.5);
    assert(dict_get(&output, "draw_rate") == 1);
    dict_clear(&output);
    actions[1][RF_PRIMARY] = RF_TELEPORT;
    puf_step(env);
    assert(env->log.opponent_teleports == 1 && env->log.self_teleports == 0);
    wait_out();
    actions[0][RF_PRIMARY] = RF_TELEPORT;
    puf_step(env);
    wait_out();
    assert(env->log.both_teleports == 1 && env->log.self_teleports == 1 && env->log.opponent_teleports == 2);
    assert(env->log.n == 0 && env->session_fights == 6);
    assert(env->log.episode_length > 0);
    {
        Dict output2 = {0};
        puf_log(&env->log, &output2);
        assert(dict_find(&output2, "episode_length") != NULL);
        assert(dict_find(&output2, "ticks_tentacle") != NULL);
        assert(dict_find(&output2, "ticks_maul") != NULL);
        assert(dict_find(&output2, "drink_combat") != NULL);
        assert(dict_find(&output2, "rude_teleports") != NULL);
        assert(dict_find(&output2, "tp_both_spec") != NULL);
        assert(dict_find(&output2, "teleport_penalty") == NULL);
        dict_clear(&output2);
    }
    puf_close(env); free(env);
    items[1].value = 0;
    env = (Env*)calloc(1, sizeof(*env));
    env->rng = 123;
    puf_init(env, &kwargs);
    assert(env->num_agents == 1 && env->agents[0].policy == 0);
    env->agents[0] = (Agent){.observations = obs[0], .actions = actions[0],
        .rewards = &rewards[0], .terminals = &terminals[0], .action_mask = masks[0]};
    for (int bot = 0; bot < RISKFIGHT_OPPONENTS; bot++) {
        puf_set_bot_policy(env, bot);
        puf_reset(env);
        assert(env->context.opponent == bot && env->num_agents == 1);
    }
    actions[0][RF_PRIMARY] = RF_TELEPORT;
    puf_step(env);
    assert(terminals[0] == 0 && rewards[0] == 0 && env->log.escapes == 1);
    puf_close(env); free(env);
    items[1].value = 1;
    items[2].value = 20000;
    env = (Env*)calloc(1, sizeof(*env));
    env->rng = 123;
    puf_init(env, &kwargs);
    for (int i = 0; i < 2; i++) env->agents[i] = (Agent){
        .observations = obs[i], .actions = actions[i], .rewards = &rewards[i],
        .terminals = &terminals[i], .action_mask = masks[i], .policy = i};
    puf_reset(env);
    memset(actions, 0, sizeof(actions));
    /* Three teleports in one session: our side is refilled every time, the session never ends on them. */
    for (int round = 0; round < 3; round++) {
        env->state.env.players[0].current_hitpoints = 50;
        actions[0][RF_PRIMARY] = RF_TELEPORT;
        puf_step(env);
        assert(terminals[0] == 0 && terminals[1] == 0);
        wait_out();
        assert(env->state.env.tick == 0 && env->state.env.players[0].current_hitpoints == RISKFIGHT_DEFAULT_LOADOUT.hitpoints);
        assert(!env->state.escaped[0] && env->log.self_teleports == round + 1);
    }
    assert(env->log.n == 0 && env->log.escapes == 3 && env->session_fights == 3);
    /* The session ends when its budget is spent; the log then holds its fights and its score per hour. */
    actions[0][RF_PRIMARY] = 0;
    env->session_elapsed = 20000 - 1;
    puf_step(env);
    assert(terminals[0] == 1 && terminals[1] == 1);
    assert(env->log.n == 1 && env->log.rounds == 3 && env->log.net_stake == 0);
    assert(env->session_elapsed == 0 && env->session_fights == 0 && env->session_stake == 0);
    env->state.env.players[1].current_hitpoints = 0;
    puf_step(env);
    assert(terminals[0] == 0 && env->log.kills == 1 && env->session_stake == 1);
    env->session_elapsed = 20000 - 1;
    puf_step(env);
    assert(terminals[0] == 1);
    assert(env->log.n == 2 && env->log.rounds == 4 && env->log.net_stake == (float)RF_TICKS_PER_HOUR / 20000);
    /* A fight that nobody ends costs a death at the round limit and goes on to the next. */
    actions[0][RF_PRIMARY] = 0;
    env->state.env.tick = RISKFIGHT_ROUND_TICKS - 1;
    puf_step(env);
    assert(terminals[0] == 0 && terminals[1] == 0 && rewards[0] == -RF_REWARD_SCALE && rewards[1] == -RF_REWARD_SCALE);
    wait_out();
    assert(env->state.env.tick == 0 && env->log.timeouts == 1);
    puf_close(env); free(env);
    enum { SEEDED_ENVS = 8192 };
    static uint32_t combat[SEEDED_ENVS];
    for (uint32_t i = 0; i < SEEDED_ENVS; i++) {
        combat[i] = osrs_env_seed(i, 0);
        assert(combat[i] != osrs_env_seed(i, 1));
        for (uint32_t j = 0; j < i; j++) assert(combat[i] != combat[j]);
    }
    puts("Riskfight native scripted and self-play contracts passed");
}
