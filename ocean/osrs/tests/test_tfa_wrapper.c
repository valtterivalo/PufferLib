/* Drives the puffer wrapper the way the CPU vec does: one Env per team, buffers per agent, auto reset. */
#include "../../osrs_tfa/osrs_tfa.h"

int main(void) {
    Dict kw = {0};
    const char* keys[] = { "team_size", "episode_mode", "start_room", "path", "room_max_ticks",
        "reward_room_clear", "reward_raid_clear", "reward_damage_dealt", "reward_damage_taken", "reward_death", "reward_wipe" };
    const double vals[] = { 3, 0, 0, 0, 3000, 1, 2, 0.05, 0.05, 0.5, 1 };
    for (int i = 0; i < 11; i++) dict_set(&kw, keys[i], vals[i]);
    Env* env = calloc(1, sizeof(Env));
    puf_init(env, &kw);
    assert(env->num_agents == 3);
    static float obs[3][OBS_SIZE], acts[3][NUM_ATNS], rew[3], term[3], resets[3];
    static unsigned char mask[3][TFA_ACTION_MASK_SIZE];
    for (int p = 0; p < 3; p++)
        env->agents[p] = (Agent){ obs[p], acts[p], &rew[p], &term[p], &resets[p], mask[p], 0 };
    puf_reset(env);
    uint32_t rng = 5;
    int episodes = 0;
    for (int step = 0; step < 200000; step++) {
        for (int p = 0; p < 3; p++) {
            int off = 0;
            for (int h = 0; h < NUM_ATNS; h++) {
                int legal[64], n = 0;
                for (int a = 0; a < TFA_ACTION_DIMS[h]; a++) if (mask[p][off + a]) legal[n++] = a;
                acts[p][h] = (float)legal[encounter_rand_int(&rng, n)];
                off += TFA_ACTION_DIMS[h];
            }
        }
        puf_step(env);
        episodes += term[0] > 0.0f;
        assert(term[0] == term[1] && term[1] == term[2]);
    }
    Dict out = {0};
    Log log = env->log;
    float n = log.n;
    float* f = (float*)&log;
    for (size_t j = 0; j < sizeof(Log) / sizeof(float); j++) f[j] /= n;
    puf_log(&log, &out);
    printf("episodes %d (log n %.0f), mean length %.1f, wipes %.2f, timeouts %.2f\n",
        episodes, n, dict_get(&out, "episode_length"), dict_get(&out, "wipes"), dict_get(&out, "timeouts"));
    assert(episodes == (int)n);
    puf_close(env);
    return 0;
}
