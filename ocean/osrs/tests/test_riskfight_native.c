#include "../../osrs_riskfight/osrs_riskfight.h"
#include <assert.h>

int main(void) {
    DictItem items[] = {{.key = "opponent_type", .value = 0}, {.key = "self_play", .value = 1}};
    Dict kwargs = {.items = items, .size = 2};
    Env* env = (Env*)calloc(1, sizeof(*env));
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
    assert(terminals[0] == 1 && terminals[1] == 1 && rewards[0] == 0 && rewards[1] == 0);
    assert(env->state.env.tick == 0 && env->log.escapes == 1);
    assert(env->log.self_teleports == 1 && env->log.opponent_teleports == 0);
    assert(env->log.self_escape_healing[OSRS_ESCAPE_TRIPLE_EATS] == 1);
    assert(obs[0][RF_OPPONENT_START + NUM_GEAR_SLOTS + 1] * RF_OBSERVATION_TILE_SCALE == 1);
    assert(obs[1][RF_OPPONENT_START + NUM_GEAR_SLOTS + 1] * RF_OBSERVATION_TILE_SCALE == -1);
    for (int i = 0; i < 2; i++) {
        float expected[RF_MASK_SIZE];
        riskfight_write_action_mask(&env->state, i, expected);
        for (int j = 0; j < RF_MASK_SIZE; j++) assert(masks[i][j] == expected[j]);
    }
    assert(env->log.policy_0_score == 0.5f && env->log.draw_rate == 1);
    memset(&env->log, 0, sizeof(env->log));
    actions[0][RF_PRIMARY] = 0;
    env->state.env.players[1].current_hitpoints = 0;
    puf_step(env);
    assert(env->log.policy_0_score == 1 && env->log.draw_rate == 0);
    assert(env->log.kills == 1 && env->log.net_stake == 1);
    env->state.env.players[0].current_hitpoints = 0;
    puf_step(env);
    assert(env->log.policy_0_score == 1 && env->log.deaths == 1 && env->log.net_stake == 0);
    env->state.env.players[0].current_hitpoints = 0;
    env->state.env.players[1].current_hitpoints = 0;
    puf_step(env);
    assert(env->log.policy_0_score == 1.5f && env->log.draw_rate == 1);
    Dict output = {0};
    puf_log(&env->log, &output);
    assert(dict_get(&output, "policy_0_score") == 1.5);
    assert(dict_get(&output, "draw_rate") == 1);
    dict_clear(&output);
    actions[1][RF_PRIMARY] = RF_TELEPORT;
    puf_step(env);
    assert(env->log.opponent_teleports == 1 && env->log.self_teleports == 0);
    actions[0][RF_PRIMARY] = RF_TELEPORT;
    puf_step(env);
    assert(env->log.both_teleports == 1 && env->log.self_teleports == 1 && env->log.opponent_teleports == 2);
    puf_close(env); free(env);
    items[1].value = 0;
    env = (Env*)calloc(1, sizeof(*env));
    env->rng = 123;
    puf_init(env, &kwargs);
    assert(env->num_agents == 1 && env->agents[0].policy == 0);
    env->agents[0] = (Agent){.observations = obs[0], .actions = actions[0],
        .rewards = &rewards[0], .terminals = &terminals[0], .action_mask = masks[0]};
    const int bots[] = {RISKFIGHT_TRADER, RISKFIGHT_CAUTIOUS, RISKFIGHT_AGGRESSIVE, RISKFIGHT_TACTICIAN, RISKFIGHT_PRESSURE, RISKFIGHT_SURVIVAL, RISKFIGHT_HELDOUT, RISKFIGHT_FLOOR};
    for (size_t i = 0; i < sizeof(bots) / sizeof(*bots); i++) {
        int bot = bots[i];
        puf_set_bot_policy(env, bot);
        puf_reset(env);
        assert(env->context.opponent == bot && env->num_agents == 1);
    }
    actions[0][RF_PRIMARY] = RF_TELEPORT;
    puf_step(env);
    assert(terminals[0] == 1 && rewards[0] == 0 && env->log.escapes == 1);
    puf_close(env); free(env);
    puts("Riskfight native scripted and self-play contracts passed");
}
