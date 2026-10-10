#include "../../osrs_pvp/osrs_pvp.h"
#include "osrs_test_check.h"
#include "osrs_test_nh_pvp.h"

int main(void) {
    DictItem items[] = {
        {.key = "opponent_type", .value = OPP_MIXED_NH},
        {.key = "gear_tier", .value = 3}, {.key = "is_lms", .value = 1},
        {.key = "use_c_opponent", .value = 1}, {.key = "self_play", .value = 1},
        {.key = "selfplay_match_fraction", .value = 0.5},
        {.key = "auto_reset", .value = 0}, OSRS_TEST_NH_PVP_REWARD_ITEMS
    };
    Dict kwargs = {.items = items, .size = sizeof(items) / sizeof(*items)};
    dict_set(&kwargs, "shaping_enabled", 0);
    dict_set(&kwargs, "terminal_reward_coef", 1);
    Dict vec = {0};
    dict_set(&vec, "total_agents", 48);
    dict_set(&vec, "num_buffers", 2);
    dict_set(&vec, "num_policies", 2);
    dict_set(&vec, "hist_policy_percent", 0.5);
    int size, starts[2], counts[2];
    Env* envs = my_vec_init(&size, starts, counts, &vec, &kwargs);
    CHECK("mixed vector has 32 matches from 48 actor slots", size == 32);
    float obs[2][OBS_SIZE], actions[2][NUM_ATNS] = {{0}}, rewards[2], terminals[2];
    unsigned char masks[2][NH_PVP_ACTION_MASK_SIZE];
    for (int buf = 0; buf < 2; buf++) {
        int bots = 0, current = 0, hist_roles[2] = {0}, actors = 0;
        for (int e = starts[buf]; e < starts[buf] + counts[buf]; e++) {
            Env* env = &envs[e];
            actors += env->num_agents;
            int learner = 0;
            for (int role = 0; role < env->num_agents; role++) {
                int policy = env->agents[role].policy;
                env->agents[role] = (Agent){.observations = obs[role], .actions = actions[role],
                    .rewards = &rewards[role], .terminals = &terminals[role],
                    .action_mask = masks[role], .policy = policy};
                if (policy == 0) learner = role;
                else hist_roles[role]++;
            }
            if (env->num_agents == 1) bots++;
            else if (env->agents[0].policy == env->agents[1].policy) current++;
            CHECK("bot and external actions are mutually exclusive",
                env->state.env.pvp_runtime.use_c_opponent == (env->num_agents == 1)
                && env->state.env.pvp_runtime.use_external_opponent_actions == (env->num_agents == 2));
            CHECK("native seeds follow unchanged environment index", env->state.env.rng_seed == osrs_env_seed(e, 0));
            puf_reset(env);
            for (int role = 0; role < env->num_agents; role++) {
                float expected[OBS_SIZE];
                pvp_write_observations(expected, &env->state.env, role);
                CHECK("role observes its actual player", memcmp(expected, obs[role], sizeof(expected)) == 0);
            }
            if (env->num_agents == 2 && env->agents[0].policy != env->agents[1].policy) {
                env->state.env.players[1 - learner].current_hitpoints = 0;
                puf_step(env);
                CHECK("historical learner wins in either role", env->log.wins == 1 && env->log.policy_0_score == 1);
                CHECK("historical rewards route to actual roles", rewards[learner] == 1 && rewards[1 - learner] == -1);
                CHECK("terminal resets retain both role terminals", terminals[0] == 1 && terminals[1] == 1 && env->state.env.tick == 0);
                CHECK("return follows primary policy", env->log.episode_return == 1);
            }
            puf_close(env);
        }
        CHECK("each buffer owns exact actor budget", actors == 24);
        CHECK("mixture halves matches", bots == 8 && current == 4);
        CHECK("historical roles balance in each buffer", hist_roles[0] == 2 && hist_roles[1] == 2);
    }
    free(envs);
    dict_clear(&vec);
    return osrs_test_summary();
}
