#pragma once

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

typedef float obs_t;
#include "pufferenv.h"

#define Log OsrsSharedLog
#include "../osrs/encounters/encounter_tfa.h"
#undef Log
#ifdef OSRS_PUFFER_RENDER
#include "../osrs/osrs_puffer_render.h"
#endif

#define OBS_SIZE TFA_NUM_OBS
#define NUM_ATNS TFA_NUM_ACTION_HEADS
#define ACT_SIZES TFA_ACTION_DIMS_INIT

#define TFA_ENV_STATE(env) ((EncounterState*)&(env)->state)
#define TFA_ENV_CONTEXT(env) ((EncounterContext*)&(env)->context)

struct Log {
    float episode_return;
    float episode_length;
    float perf;
    float score;
    float rooms_cleared;
    float raid_clears;
    float wipes;
    float timeouts;
    float deaths;
    float damage_dealt;
    float damage_taken;
    float n;
};

/* One env is one raid team: num_agents = team_size players sharing TfaState. */
struct Env {
    Log log;
    int num_agents;
    unsigned int rng;
    Agent agents[TFA_MAX_PLAYERS];
    int tag;
    int boundary_reached;

    TfaState state;
    TfaContext context;
    int acts_staging[TFA_MAX_PLAYERS * TFA_NUM_ACTION_HEADS];
#ifdef OSRS_PUFFER_RENDER
    void* renderer;
#endif
};

static void tfa_env_observe(Env* env) {
    for (int p = 0; p < env->num_agents; p++) {
        Agent* a = &env->agents[p];
        tfa_write_obs_for(&env->state, &env->context, p, (float*)a->observations);
        tfa_write_mask_for(&env->state, &env->context, p, a->action_mask);
    }
}

void puf_init(Env* env, Dict* kwargs) {
    ENCOUNTER_TFA.init_context(TFA_ENV_CONTEXT(env));
    ENCOUNTER_TFA.init_state(TFA_ENV_STATE(env), TFA_ENV_CONTEXT(env));
    env->state.rng_state = osrs_env_seed((uint32_t)env->rng, 0);
    memset(&env->log, 0, sizeof(Log));

    static const char* const int_keys[] = {
        "team_size", "episode_mode", "start_room", "path", "room_max_ticks",
    };
    for (size_t k = 0; k < sizeof(int_keys) / sizeof(*int_keys); k++)
        ENCOUNTER_TFA.put_int(TFA_ENV_STATE(env), TFA_ENV_CONTEXT(env), int_keys[k], (int)dict_get(kwargs, int_keys[k]));
    static const char* const float_keys[] = {
        "reward_room_clear", "reward_raid_clear", "reward_damage_dealt",
        "reward_damage_taken", "reward_death", "reward_wipe",
    };
    for (size_t k = 0; k < sizeof(float_keys) / sizeof(*float_keys); k++)
        ENCOUNTER_TFA.put_float(TFA_ENV_STATE(env), TFA_ENV_CONTEXT(env), float_keys[k], (float)dict_get(kwargs, float_keys[k]));

    env->num_agents = env->context.config.team_size;
    for (int p = 0; p < env->num_agents; p++) env->agents[p].policy = 0;
    ENCOUNTER_TFA.finalize_context(TFA_ENV_STATE(env), TFA_ENV_CONTEXT(env));
}

void puf_reset(Env* env) {
    tfa_reset_typed(&env->state, &env->context, 0);
    tfa_env_observe(env);
    for (int p = 0; p < env->num_agents; p++) {
        env->agents[p].rewards[0] = 0.0f;
        env->agents[p].terminals[0] = 0.0f;
    }
}

static void tfa_env_log_episode(Env* env) {
    TfaState* s = &env->state;
    Log* log = &env->log;
    float clear = s->outcome == TFA_OUTCOME_CLEARED;
    log->episode_return += s->episode_return / (float)env->num_agents;
    log->episode_length += (float)s->tick;
    log->rooms_cleared += (float)s->rooms_cleared;
    log->score += (float)s->rooms_cleared;
    log->perf += clear;
    log->raid_clears += clear * (s->episode_mode == TFA_EPISODE_RAID);
    log->wipes += s->outcome == TFA_OUTCOME_WIPED;
    log->timeouts += s->outcome == TFA_OUTCOME_TIMEOUT;
    for (int p = 0; p < env->num_agents; p++) {
        log->deaths += s->players[p].life == TFA_LIFE_DEAD;
        log->damage_dealt += s->players[p].damage_dealt_total;
        log->damage_taken += s->players[p].damage_taken_total;
    }
    log->n += 1.0f;
}

void puf_step(Env* env) {
    for (int p = 0; p < env->num_agents; p++)
        for (int h = 0; h < NUM_ATNS; h++)
            env->acts_staging[p * NUM_ATNS + h] = (int)env->agents[p].actions[h];
    tfa_step_team(&env->state, &env->context, env->acts_staging);
    int done = env->state.episode_over;
    for (int p = 0; p < env->num_agents; p++) {
        env->agents[p].rewards[0] = env->state.players[p].reward;
        env->agents[p].terminals[0] = (float)done;
    }
    if (done) {
        tfa_env_log_episode(env);
        tfa_reset_typed(&env->state, &env->context, 0);
    }
    tfa_env_observe(env);
}

void puf_render(Env* env) {
#ifdef OSRS_PUFFER_RENDER
    if (env->renderer == NULL)
        env->renderer = osrs_puffer_render_create(&ENCOUNTER_TFA, TFA_ENV_STATE(env), TFA_ENV_CONTEXT(env));
    osrs_puffer_render_draw(env->renderer);
#else
    (void)env;
#endif
}

void puf_close(Env* env) {
#ifdef OSRS_PUFFER_RENDER
    if (env->renderer != NULL) osrs_puffer_render_destroy(env->renderer);
#endif
    ENCOUNTER_TFA.destroy_context(TFA_ENV_CONTEXT(env));
}

void puf_log(Log* log, Dict* out) {
    dict_set(out, "episode_return", log->episode_return);
    dict_set(out, "episode_length", log->episode_length);
    dict_set(out, "perf", log->perf);
    dict_set(out, "score", log->score);
    dict_set(out, "rooms_cleared", log->rooms_cleared);
    dict_set(out, "raid_clears", log->raid_clears);
    dict_set(out, "wipes", log->wipes);
    dict_set(out, "timeouts", log->timeouts);
    dict_set(out, "deaths", log->deaths);
    dict_set(out, "damage_dealt", log->damage_dealt);
    dict_set(out, "damage_taken", log->damage_taken);
}
