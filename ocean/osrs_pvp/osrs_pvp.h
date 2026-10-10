#pragma once

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef float obs_t;
#define PUF_HAS_BOT_POLICY
#define MY_VEC_INIT
#define MY_POLICY_ASSIGNMENT
#include "pufferenv.h"

#define Log OsrsSharedLog
#include "../osrs/encounters/encounter_nh_pvp.h"
#undef Log
#ifdef OSRS_PUFFER_RENDER
#include "../osrs/osrs_puffer_render.h"
#endif

#define OBS_SIZE NH_PVP_NUM_OBS
#define NUM_ATNS NH_PVP_NUM_ACTION_HEADS
#define ACT_SIZES NH_PVP_ACTION_DIMS_INIT

#define NH_PVP_ENV_STATE(env) ((EncounterState*)&(env)->state)
#define NH_PVP_ENV_CONTEXT(env) ((EncounterContext*)&(env)->context)

struct Log {
    float episode_return;
    float episode_length;
    float wins;
    float policy_0_score;
    float draw_rate;
    float damage_dealt;
    float damage_received;
    float prayer_correct;
    float prayer_total;
    float food_remaining;
    float karambwan_remaining;
    float brews_remaining;
    float spec_energy_remaining;
    float bot_games;
    float bot_wins;
    float historical_games;
    float historical_wins;
    float current_games;
    float n;
};

struct Env {
    Log log;
    int num_agents;
    unsigned int rng;
    Agent agents[2];
    int tag;
    int boundary_reached;
    float learner_return;

    NhPvpState state;
    NhPvpContext context;
    int acts_staging[2 * NH_PVP_NUM_ACTION_HEADS];
#ifdef OSRS_PUFFER_RENDER
    void* renderer;
#endif
};

static void nh_pvp_native_observe(Env* env) {
    for (int i = 0; i < env->num_agents; i++) {
        pvp_write_observations(env->agents[i].observations, &env->state.env, i);
        pvp_write_action_mask_bytes(env->agents[i].action_mask, &env->state.env,
            i, env->context.route_topology);
    }
}


static void nh_pvp_native_init(Env* env, Dict* kwargs, int num_agents) {
    env->num_agents = num_agents;
    ENCOUNTER_NH_PVP.init_context(NH_PVP_ENV_CONTEXT(env));
    ENCOUNTER_NH_PVP.init_state(NH_PVP_ENV_STATE(env), NH_PVP_ENV_CONTEXT(env));

    uint32_t env_seed = osrs_env_seed((uint32_t)env->rng, 0);
    double opponent_type = dict_get(kwargs, "opponent_type");
    assert(opponent_type == (int)opponent_type);
    ENCOUNTER_NH_PVP.put_int(
        NH_PVP_ENV_STATE(env), NH_PVP_ENV_CONTEXT(env),
        "opponent_type", (int)opponent_type);

    static const char* const int_keys[] = {
        "gear_tier",
        "is_lms",
        "auto_reset",
    };
    for (size_t k = 0; k < sizeof(int_keys) / sizeof(*int_keys); k++) {
        ENCOUNTER_NH_PVP.put_int(
            NH_PVP_ENV_STATE(env), NH_PVP_ENV_CONTEXT(env),
            int_keys[k], (int)dict_get(kwargs, int_keys[k]));
    }
    ENCOUNTER_NH_PVP.put_int(
        NH_PVP_ENV_STATE(env), NH_PVP_ENV_CONTEXT(env), "seed", (int)env_seed);
    ENCOUNTER_NH_PVP.put_int(NH_PVP_ENV_STATE(env), NH_PVP_ENV_CONTEXT(env),
        "use_c_opponent", num_agents == 1 ? (int)dict_get(kwargs, "use_c_opponent") : 0);
    env->state.env.pvp_runtime.use_external_opponent_actions = env->num_agents == 2;
    assert(env->num_agents == 1 || !env->state.env.pvp_runtime.use_c_opponent);
#define NH_PVP_LOAD_REWARD_INT(key, field, default_value) \
    ENCOUNTER_NH_PVP.put_int(NH_PVP_ENV_STATE(env), NH_PVP_ENV_CONTEXT(env), \
        key, (int)dict_get(kwargs, key));
    OSRS_PVP_REWARD_INT_FIELDS(NH_PVP_LOAD_REWARD_INT)
#undef NH_PVP_LOAD_REWARD_INT
#define NH_PVP_LOAD_REWARD_FLOAT(key, field, default_value) \
    ENCOUNTER_NH_PVP.put_float(NH_PVP_ENV_STATE(env), NH_PVP_ENV_CONTEXT(env), \
        key, (float)dict_get(kwargs, key));
    OSRS_PVP_REWARD_FLOAT_FIELDS(NH_PVP_LOAD_REWARD_FLOAT)
#undef NH_PVP_LOAD_REWARD_FLOAT

    memset(&env->log, 0, sizeof(env->log));
    ENCOUNTER_NH_PVP.finalize_context(
        NH_PVP_ENV_STATE(env), NH_PVP_ENV_CONTEXT(env));
}

void puf_init(Env* env, Dict* kwargs) {
    nh_pvp_native_init(env, kwargs, (int)dict_get(kwargs, "self_play") ? 2 : 1);
    for (int i = 0; i < env->num_agents; i++)
        env->agents[i].policy = i ^ (env->num_agents == 2 ? env->rng % 2 : 0);
}

Env* my_vec_init(int* num_envs, int* starts, int* counts, Dict* vec, Dict* kwargs) {
    int actors = (int)dict_get(vec, "total_agents");
    int buffers = (int)dict_get(vec, "num_buffers");
    assert(actors % buffers == 0);
    int apb = actors / buffers;
    double fraction = (int)dict_get(kwargs, "self_play")
        ? dict_get(kwargs, "selfplay_match_fraction") : 0;
    assert(fraction >= 0 && fraction <= 1);
    int selfplay_envs = (int)round(apb * fraction / (1 + fraction));
    assert(selfplay_envs * 2 <= apb);
    int bot_envs = apb - 2 * selfplay_envs;
    int policies = (int)dict_get(vec, "num_policies");
    double history_fraction = dict_get(vec, "hist_policy_percent");
    assert(history_fraction >= 0 && history_fraction <= 1);
    int historical_envs = policies > 1 ? (int)(selfplay_envs * history_fraction) : 0;
    if (historical_envs > 1)
        historical_envs = historical_envs / (2 * (policies - 1)) * (2 * (policies - 1));
    assert(policies == 1 || historical_envs > 0);
    int count = bot_envs + selfplay_envs;
    Env* envs = (Env*)calloc(buffers * count, sizeof(Env));
    for (int buf = 0; buf < buffers; buf++) {
        starts[buf] = buf * count;
        counts[buf] = count;
        for (int e = 0; e < count; e++) {
            Env* env = &envs[starts[buf] + e];
            env->rng = starts[buf] + e;
            nh_pvp_native_init(env, kwargs, e < bot_envs ? 1 : 2);
            int hist = e - (count - historical_envs);
            if (hist >= 0 && historical_envs > 0)
                env->agents[hist % 2].policy = 1 + hist / 2 % (policies - 1);
        }
    }
    *num_envs = buffers * count;
    return envs;
}

static inline void puf_set_bot_policy(Env* env, int bot_policy) {
    OpponentType type = osrs_nh_opponent_type(bot_policy);
    assert(osrs_nh_is_scripted_opponent(type));
    env->state.env.pvp_runtime.opponent.type = type;
}

void puf_reset(Env* env) {
    env->learner_return = 0;
    ENCOUNTER_NH_PVP.reset(NH_PVP_ENV_STATE(env), NH_PVP_ENV_CONTEXT(env), 0);
    nh_pvp_native_observe(env);
    for (int i = 0; i < env->num_agents; i++) {
        env->agents[i].rewards[0] = 0;
        env->agents[i].terminals[0] = 0;
    }
}

void puf_step(Env* env) {
    for (int i = 0; i < env->num_agents; i++)
        for (int head = 0; head < NUM_ATNS; head++)
            env->acts_staging[i * NUM_ATNS + head] = (int)env->agents[i].actions[head];
    if (env->num_agents == 2)
        nh_pvp_set_external_actions(&env->state, env->acts_staging + NUM_ATNS);

    ENCOUNTER_NH_PVP.step(
        NH_PVP_ENV_STATE(env), NH_PVP_ENV_CONTEXT(env), env->acts_staging);
    int is_terminal = ENCOUNTER_NH_PVP.is_terminal(
        NH_PVP_ENV_STATE(env), NH_PVP_ENV_CONTEXT(env));
    for (int i = 0; i < env->num_agents; i++) {
        env->agents[i].rewards[0] = env->state.env.rewards[i];
        env->agents[i].terminals[0] = is_terminal;
    }
    int learners = 0;
    float reward = 0;
    float wins = 0;
    for (int role = 0; role < env->num_agents; role++) {
        if (env->agents[role].policy == 0) {
            learners++;
            reward += env->state.env.rewards[role];
            wins += env->state.env.winner == role;
        }
    }
    assert(learners > 0);
    env->learner_return += reward / learners;

    if (is_terminal) {
        OsrsSharedLog* episode = &env->state.env.log;
        env->log.episode_return += env->learner_return;
        env->learner_return = 0;
        env->log.episode_length += episode->episode_length;
        if (env->num_agents == 1) {
            env->log.bot_games++;
            env->log.bot_wins += wins;
        } else if (learners == 1) {
            env->log.historical_games++;
            env->log.historical_wins += wins;
        } else {
            env->log.current_games++;
        }
        env->log.wins += wins / learners;
        env->log.policy_0_score += wins / learners;
        env->log.draw_rate += env->state.env.winner < 0;
        env->log.damage_dealt += episode->damage_dealt;
        env->log.damage_received += episode->damage_received;
        env->log.prayer_correct += episode->prayer_correct;
        env->log.prayer_total += episode->prayer_total;
        env->log.food_remaining += episode->food_remaining;
        env->log.karambwan_remaining += episode->karambwan_remaining;
        env->log.brews_remaining += episode->brews_remaining;
        env->log.spec_energy_remaining += episode->spec_energy_remaining;
        env->log.n += episode->n;
        memset(episode, 0, sizeof(*episode));

        ENCOUNTER_NH_PVP.reset(
            NH_PVP_ENV_STATE(env), NH_PVP_ENV_CONTEXT(env), 0);
    }
    nh_pvp_native_observe(env);
}

void puf_render(Env* env) {
#ifdef OSRS_PUFFER_RENDER
    if (env->renderer == NULL) {
        env->renderer = osrs_puffer_render_create(
            &ENCOUNTER_NH_PVP,
            NH_PVP_ENV_STATE(env),
            NH_PVP_ENV_CONTEXT(env));
    }
    osrs_puffer_render_draw(env->renderer);
#else
    (void)env;
#endif
}

void puf_close(Env* env) {
#ifdef OSRS_PUFFER_RENDER
    if (env->renderer != NULL) osrs_puffer_render_destroy(env->renderer);
#endif
    pvp_close(&env->state.env);
    ENCOUNTER_NH_PVP.destroy_context(NH_PVP_ENV_CONTEXT(env));
}

void puf_log(Log* log, Dict* out) {
    dict_set(out, "bot_match_fraction", log->bot_games);
    dict_set(out, "historical_match_fraction", log->historical_games);
    dict_set(out, "current_match_fraction", log->current_games);
    if (log->bot_games > 0)
        dict_set(out, "bot_win_rate", log->bot_wins / log->bot_games);
    if (log->historical_games > 0)
        dict_set(out, "historical_win_rate", log->historical_wins / log->historical_games);
    dict_set(out, "episode_return", log->episode_return);
    dict_set(out, "episode_length", log->episode_length);
    dict_set(out, "wins", log->wins);
    dict_set(out, "policy_0_score", log->policy_0_score);
    dict_set(out, "draw_rate", log->draw_rate);
    dict_set(out, "damage_dealt", log->damage_dealt);
    dict_set(out, "damage_received", log->damage_received);
    dict_set(out, "opponent_protection_rate",
        log->prayer_total > 0.0f ? log->prayer_correct / log->prayer_total : 0.0f);
    dict_set(out, "food_remaining", log->food_remaining);
    dict_set(out, "karambwan_remaining", log->karambwan_remaining);
    dict_set(out, "brews_remaining", log->brews_remaining);
    dict_set(out, "spec_remaining", log->spec_energy_remaining);
    dict_set(out, "score", log->wins);
    dict_set(out, "perf", log->wins);
}
