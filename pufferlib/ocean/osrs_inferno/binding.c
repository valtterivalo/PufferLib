/**
 * @file binding.c
 * @brief Static-native binding for OSRS Inferno encounter.
 *
 * Bridges vecenv.h's contract (double actions, float terminals) with the
 * Inferno encounter's vtable interface.
 */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "osrs_encounter.h"
#include "osrs_pvp_types.h"
#include "encounters/encounter_inferno.h"

#define INF_TOTAL_OBS (INF_NUM_OBS + INF_ACTION_MASK_SIZE)

typedef struct {
    void* observations;
    double* actions;
    float* rewards;
    float* terminals;
    int num_agents;
    int rng;
    Log log;

    EncounterState* enc_state;

    int acts_staging[INF_NUM_ACTION_HEADS];
    unsigned char term_staging;
} InfernoEnv;

#define OBS_SIZE INF_TOTAL_OBS
#define NUM_ATNS INF_NUM_ACTION_HEADS
#define ACT_SIZES { ENCOUNTER_MOVE_ACTIONS, 4, INF_MAX_NPCS+1, 5, 2, 4, 2 }
#define OBS_TYPE FLOAT
#define ACT_TYPE DOUBLE
#define Env InfernoEnv

void c_step(Env* env) {
    for (int i = 0; i < NUM_ATNS; i++)
        env->acts_staging[i] = (int)env->actions[i];

    ENCOUNTER_INFERNO.step(env->enc_state, env->acts_staging);

    float* obs = (float*)env->observations;
    ENCOUNTER_INFERNO.write_obs(env->enc_state, obs);
    ENCOUNTER_INFERNO.write_mask(env->enc_state, obs + INF_NUM_OBS);

    env->rewards[0] = ENCOUNTER_INFERNO.get_reward(env->enc_state);

    int is_term = ENCOUNTER_INFERNO.is_terminal(env->enc_state);
    env->term_staging = (unsigned char)is_term;
    env->terminals[0] = (float)is_term;

    /* continuously update log with running stats so the sweep always has signal,
       even mid-episode. vecenv clears env->log periodically via memset. */
    {
        InfernoState* s = (InfernoState*)env->enc_state;
        env->log.episode_return = s->episode_return;
        env->log.episode_length = (float)s->tick;
        env->log.damage_dealt = s->total_damage_dealt;
        env->log.damage_received = s->total_damage_received;
        env->log.wins = (is_term && s->winner == 0) ? 1.0f : 0.0f;
        env->log.n = 1.0f;  /* always report so sweep has continuous signal */
    }

    if (is_term) {
        ENCOUNTER_INFERNO.reset(env->enc_state, 0);
        ENCOUNTER_INFERNO.write_obs(env->enc_state, obs);
        ENCOUNTER_INFERNO.write_mask(env->enc_state, obs + INF_NUM_OBS);
    }
}

void c_reset(Env* env) {
    ENCOUNTER_INFERNO.reset(env->enc_state, 0);

    float* obs = (float*)env->observations;
    ENCOUNTER_INFERNO.write_obs(env->enc_state, obs);
    ENCOUNTER_INFERNO.write_mask(env->enc_state, obs + INF_NUM_OBS);

    env->rewards[0] = 0.0f;
    env->term_staging = 0;
    env->terminals[0] = 0.0f;
}

void c_close(Env* env) {
    if (env->enc_state) {
        ENCOUNTER_INFERNO.destroy(env->enc_state);
        env->enc_state = NULL;
    }
}

void c_render(Env* env) { (void)env; }

#include "vecenv.h"

void my_init(Env* env, Dict* kwargs) {
    env->num_agents = 1;
    env->enc_state = ENCOUNTER_INFERNO.create();
    memset(&env->log, 0, sizeof(Log));

    DictItem* start_wave = dict_get_unsafe(kwargs, "start_wave");
    if (start_wave)
        ENCOUNTER_INFERNO.put_int(env->enc_state, "start_wave", (int)start_wave->value);

    DictItem* mask_in_obs = dict_get_unsafe(kwargs, "mask_in_obs");
    (void)mask_in_obs;  /* always embedded for inferno */
}

void my_log(Log* log, Dict* out) {
    dict_set(out, "episode_return", log->episode_return);
    dict_set(out, "damage_dealt", log->damage_dealt);
    dict_set(out, "damage_received", log->damage_received);
    dict_set(out, "episode_length", log->episode_length);
    dict_set(out, "wins", log->wins);

    float wr = log->wins;
    float wave_progress = log->episode_length / (float)INF_MAX_TICKS;
    float score = wr + (1.0f - wr) * wave_progress * 0.5f - (1.0f - wr);
    dict_set(out, "score", score);
}
