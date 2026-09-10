#include "../../osrs_riskfight/osrs_riskfight.h"
#include <assert.h>

int main(void) {
    DictItem items[] = {{.key = "opponent_type", .value = 0}, {.key = "self_play", .value = 1}};
    Dict kwargs = {.items = items, .size = 2};
    Env* env = calloc(1, sizeof(*env));
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
    assert(obs[0][RF_OPPONENT_START + NUM_GEAR_SLOTS + 1] == 1);
    assert(obs[1][RF_OPPONENT_START + NUM_GEAR_SLOTS + 1] == -1);
    for (int i = 0; i < 2; i++) {
        float expected[RF_MASK_SIZE];
        riskfight_write_action_mask(&env->state, i, expected);
        for (int j = 0; j < RF_MASK_SIZE; j++) assert(masks[i][j] == expected[j]);
    }
    puf_close(env); free(env);
    items[1].value = 0;
    env = calloc(1, sizeof(*env));
    env->rng = 123;
    puf_init(env, &kwargs);
    assert(env->num_agents == 1 && env->agents[0].policy == 0);
    env->agents[0] = (Agent){.observations = obs[0], .actions = actions[0],
        .rewards = &rewards[0], .terminals = &terminals[0], .action_mask = masks[0]};
    puf_reset(env);
    puf_step(env);
    assert(terminals[0] == 1 && rewards[0] == 0 && env->log.escapes == 1);
    puf_close(env); free(env);
    puts("Riskfight native scripted and self-play contracts passed");
}
