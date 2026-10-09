#pragma once

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef float obs_t;
#define PUF_HAS_BOT_POLICY
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
    float attacks_landed;
    float n;
};

struct Env {
    Log log;
    int num_agents;
    unsigned int rng;
    Agent agents[2];
    int tag;
    int boundary_reached;

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


void puf_init(Env* env, Dict* kwargs) {
    env->num_agents = (int)dict_get(kwargs, "self_play") ? 2 : 1;
    for (int i = 0; i < env->num_agents; i++) env->agents[i].policy = i;
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
        "use_c_opponent",
        "auto_reset",
    };
    for (size_t k = 0; k < sizeof(int_keys) / sizeof(*int_keys); k++) {
        ENCOUNTER_NH_PVP.put_int(
            NH_PVP_ENV_STATE(env), NH_PVP_ENV_CONTEXT(env),
            int_keys[k], (int)dict_get(kwargs, int_keys[k]));
    }
    ENCOUNTER_NH_PVP.put_int(
        NH_PVP_ENV_STATE(env), NH_PVP_ENV_CONTEXT(env), "seed", (int)env_seed);
    env->state.env.pvp_runtime.use_external_opponent_actions = env->num_agents == 2;
    assert(env->num_agents == 1 || !env->state.env.pvp_runtime.use_c_opponent);
    RewardShapingConfig* shaping = &env->state.env.shaping;
    shaping->enabled = (int)dict_get(kwargs, "shaping_enabled");
    shaping->shaping_scale = (float)dict_get(kwargs, "shaping_scale");
    shaping->damage_dealt_coef = 0.005f;
    shaping->damage_received_coef = -0.005f;
    shaping->correct_prayer_bonus = 0.03f;
    shaping->wrong_prayer_penalty = -0.02f;
    shaping->prayer_switch_no_attack_penalty = -0.01f;
    shaping->off_prayer_hit_bonus = 0.03f;
    shaping->melee_frozen_penalty = -0.05f;
    shaping->wasted_eat_penalty = -0.001f;
    shaping->premature_eat_penalty = -0.02f;
    shaping->magic_no_staff_penalty = -0.05f;
    shaping->gear_mismatch_penalty = -0.05f;
    shaping->spec_off_prayer_bonus = 0.02f;
    shaping->spec_low_defence_bonus = 0.01f;
    shaping->spec_low_hp_bonus = 0.02f;
    shaping->smart_triple_eat_bonus = 0.05f;
    shaping->wasted_triple_eat_penalty = -0.0005f;
    shaping->damage_burst_bonus = 0.002f;
    shaping->damage_burst_threshold = 30;
    shaping->premature_eat_threshold = 0.7071f;
    shaping->ko_bonus = 0.15f;
    shaping->wasted_resources_penalty = -0.07f;
    shaping->prayer_penalty_enabled = 1;

    memset(&env->log, 0, sizeof(env->log));
    ENCOUNTER_NH_PVP.finalize_context(
        NH_PVP_ENV_STATE(env), NH_PVP_ENV_CONTEXT(env));
}

static inline void puf_set_bot_policy(Env* env, int bot_policy) {
    OpponentType type = osrs_nh_opponent_type(bot_policy);
    assert(osrs_nh_is_scripted_opponent(type));
    env->state.env.pvp_runtime.opponent.type = type;
}

void puf_reset(Env* env) {
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

    if (is_terminal) {
        OsrsSharedLog* episode = &env->state.env.log;
        env->log.episode_return += episode->episode_return;
        env->log.episode_length += episode->episode_length;
        env->log.wins += episode->wins;
        env->log.policy_0_score += episode->wins;
        env->log.draw_rate += env->state.env.winner < 0;
        env->log.damage_dealt += episode->damage_dealt;
        env->log.damage_received += episode->damage_received;
        env->log.prayer_correct += episode->prayer_correct;
        env->log.prayer_total += episode->prayer_total;
        env->log.food_remaining += episode->food_remaining;
        env->log.karambwan_remaining += episode->karambwan_remaining;
        env->log.brews_remaining += episode->brews_remaining;
        env->log.spec_energy_remaining += episode->spec_energy_remaining;
        env->log.attacks_landed += episode->attacks_landed;
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
    dict_set(out, "attacks_landed", log->attacks_landed);
    dict_set(out, "damage_per_hit",
        log->attacks_landed > 0.0f ? log->damage_dealt / log->attacks_landed : 0.0f);
    dict_set(out, "score", log->wins);
    dict_set(out, "perf", log->wins);
}
