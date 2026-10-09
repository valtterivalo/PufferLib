#include "../../osrs_pvp/osrs_pvp.h"
#include "osrs_test_check.h"

static Env env;
static float observations[OBS_SIZE], actions[NUM_ATNS], reward, terminal;
static unsigned char mask[NH_PVP_ACTION_MASK_SIZE];
static float episode_observations[32][2][OBS_SIZE];
static uint32_t episode_rng[32][2];

int main(void) {
    uint32_t seeds[8192];
    int collisions = 0;
    for (int i = 0; i < 8192; i++) {
        seeds[i] = osrs_env_seed(i, 0);
        for (int j = 0; j < i; j++) collisions += seeds[i] == seeds[j];
    }
    CHECK("8192 native environment indices have distinct seeds", collisions == 0);

    DictItem items[] = {
        {.key = "opponent_type", .value = OPP_GENERALIST_NH},
        {.key = "gear_tier", .value = 3},
        {.key = "is_lms", .value = 1},
        {.key = "use_c_opponent", .value = 1},
        {.key = "self_play", .value = 0},
        {.key = "auto_reset", .value = 0},
        {.key = "shaping_enabled", .value = 0},
        {.key = "shaping_scale", .value = 0},
    };
    Dict kwargs = {.items = items, .size = sizeof(items) / sizeof(*items)};
    for (int run = 0; run < 2; run++) {
        for (int index = 0; index < 32; index++) {
            memset(&env, 0, sizeof(env));
            env.rng = index;
            puf_init(&env, &kwargs);
            env.agents[0] = (Agent){.observations = observations,
                .actions = actions, .rewards = &reward, .terminals = &terminal,
                .action_mask = mask};
            CHECK("native initialization keeps its environment seed",
                env.state.env.has_rng_seed && env.state.env.rng_seed == seeds[index]);
            puf_reset(&env);
            for (int episode = 0; episode < 2; episode++) {
                CHECK("each native episode advances its seed counter",
                    env.state.env.rng_reset_count == (uint32_t)episode + 1);
                if (run == 0) {
                    episode_rng[index][episode] = env.state.env.rng_state;
                    memcpy(episode_observations[index][episode], observations,
                        sizeof(observations));
                } else {
                    CHECK("fresh native runs reproduce episode RNG",
                        env.state.env.rng_state == episode_rng[index][episode]);
                    CHECK("fresh native runs reproduce episode observations",
                        memcmp(observations, episode_observations[index][episode],
                            sizeof(observations)) == 0);
                }
                if (episode == 0) {
                    env.state.env.tick = MAX_EPISODE_TICKS - 1;
                    puf_step(&env);
                    CHECK("native terminal path starts the next episode",
                        terminal == 1 && env.state.env.tick == 0);
                    CHECK("successive native episodes have different RNG",
                        env.state.env.rng_state != episode_rng[index][0]);
                    CHECK("successive native episodes have different inputs",
                        memcmp(observations, episode_observations[index][0],
                            sizeof(observations)) != 0);
                }
            }
            puf_close(&env);
        }
    }
    return osrs_test_summary();
}
