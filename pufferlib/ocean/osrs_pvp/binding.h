/**
 * @file binding.h
 * @brief PufferLib 4.0 static build binding for OSRS PvP environment
 *
 * Adapts the OSRS PvP simulation (which uses int actions and unsigned char
 * terminals internally) to 4.0's shared buffers (double actions, float
 * terminals). Type conversion happens at this boundary layer.
 *
 * The env internally simulates 2 players but exposes 1 agent to the
 * training loop. Internal buffers are preserved; 4.0 shared buffers
 * are accessed via bridge fields.
 */

#include "osrs_pvp.h"

// Override buffer assignment: store 4.0 shared pointers in properly-typed
// bridge fields instead of assigning to env->observations/actions/rewards/
// terminals (which have different types for the 2-agent internal model).
#define ENV_ASSIGN_BUFFERS(env, obs_ptr, act_ptr, rew_ptr, term_ptr) do { \
    (env)->ocean_obs = (float*)(obs_ptr); \
    (env)->_4_0_actions = (act_ptr); \
    (env)->ocean_rew = (rew_ptr); \
    (env)->_4_0_terminals = (term_ptr); \
} while(0)

// 4.0 binding config
#define OBS_SIZE OCEAN_OBS_SIZE
#define NUM_ATNS NUM_ACTION_HEADS
#define ACT_SIZES {LOADOUT_DIM, COMBAT_DIM, OVERHEAD_DIM, FOOD_DIM, POTION_DIM, KARAMBWAN_DIM, VENG_DIM}
#define OBS_TYPE FLOAT
#define ACT_TYPE DOUBLE
#define Env OsrsPvp
#define MY_PUT
#include "env_binding.h"

// ============================================================================
// 4.0 adapter functions (c_init/c_step/c_reset/c_close)
// Convert at the framework boundary: double actions → int, uchar terminal → float
// ============================================================================

void c_init(Env* env) {
    pvp_init(env);
}

void c_reset(Env* env) {
    if (env->ocean_acts == NULL) {
        // First call after create_static_vec — 4.0 shared pointers are stored
        // in ocean_obs, _4_0_actions, ocean_rew, _4_0_terminals by ENV_ASSIGN_BUFFERS.
        // Set up internal staging buffers for type conversion:
        env->ocean_acts = env->_ocean_acts_4_0;
        env->ocean_term = &env->_ocean_term_4_0;
    }
    pvp_reset(env);
    ocean_write_obs(env);
    if (env->ocean_obs_p1 != NULL) {
        ocean_write_obs_p1(env);
    }
    env->ocean_rew[0] = 0.0f;
    env->_4_0_terminals[0] = 0.0f;
}

void c_step(Env* env) {
    // double → int: convert 4.0 shared actions to int staging buffer
    for (int i = 0; i < NUM_ACTION_HEADS; i++) {
        env->_ocean_acts_4_0[i] = (int)env->_4_0_actions[i];
    }
    pvp_step(env);
    // obs + reward already written to ocean_obs/ocean_rew by pvp_step
    // unsigned char → float: convert terminal for 4.0
    env->_4_0_terminals[0] = (float)env->ocean_term[0];
    // After auto-reset, pvp_step wrote terminal-state obs then reset.
    // Overwrite with initial-state obs for the new episode.
    if (env->ocean_term[0] && env->auto_reset) {
        ocean_write_obs(env);
    }
}

void c_close(Env* env) {
    pvp_close(env);
}

// ============================================================================
// my_init: env setup from config dict
// ============================================================================

void my_init(Env* env, Dict* kwargs) {
    env->num_agents = 1;
    c_init(env);

    // LMS mode (default on)
    env->is_lms = 1;

    // C opponent (default on)
    env->use_c_opponent = 1;

    // Reward shaping defaults
    env->shaping.enabled = 1;
    env->shaping.shaping_scale = 1.0f;
    env->shaping.damage_dealt_coef = 0.005f;
    env->shaping.damage_received_coef = -0.005f;
    env->shaping.correct_prayer_bonus = 0.03f;
    env->shaping.wrong_prayer_penalty = -0.02f;
    env->shaping.prayer_switch_no_attack_penalty = -0.01f;
    env->shaping.off_prayer_hit_bonus = 0.03f;
    env->shaping.melee_frozen_penalty = -0.05f;
    env->shaping.wasted_eat_penalty = -0.001f;
    env->shaping.premature_eat_penalty = -0.02f;
    env->shaping.magic_no_staff_penalty = -0.05f;
    env->shaping.gear_mismatch_penalty = -0.05f;
    env->shaping.spec_off_prayer_bonus = 0.02f;
    env->shaping.spec_low_defence_bonus = 0.01f;
    env->shaping.spec_low_hp_bonus = 0.02f;
    env->shaping.smart_triple_eat_bonus = 0.05f;
    env->shaping.wasted_triple_eat_penalty = -0.0005f;
    env->shaping.damage_burst_bonus = 0.002f;
    env->shaping.damage_burst_threshold = 30;
    env->shaping.premature_eat_threshold = 0.7071f;
    env->shaping.ko_bonus = 0.15f;
    env->shaping.wasted_resources_penalty = -0.07f;

    // Gear tier weights: default 100% basic (tier 0)
    env->gear_tier_weights[0] = 1.0f;
    env->gear_tier_weights[1] = 0.0f;
    env->gear_tier_weights[2] = 0.0f;
    env->gear_tier_weights[3] = 0.0f;

    // Override from config
    DictItem* item;
    if ((item = dict_get_unsafe(kwargs, "shaping_enabled")))
        env->shaping.enabled = (int)item->value;
    if ((item = dict_get_unsafe(kwargs, "shaping_scale")))
        env->shaping.shaping_scale = (float)item->value;
    if ((item = dict_get_unsafe(kwargs, "use_c_opponent")))
        env->use_c_opponent = (int)item->value;
    if ((item = dict_get_unsafe(kwargs, "auto_reset")))
        env->auto_reset = (int)item->value;
    if ((item = dict_get_unsafe(kwargs, "opponent0_type")))
        env->opponent.type = (OpponentType)(int)item->value;
    if ((item = dict_get_unsafe(kwargs, "gear_tier_weight_0")))
        env->gear_tier_weights[0] = (float)item->value;
    if ((item = dict_get_unsafe(kwargs, "gear_tier_weight_1")))
        env->gear_tier_weights[1] = (float)item->value;
    if ((item = dict_get_unsafe(kwargs, "gear_tier_weight_2")))
        env->gear_tier_weights[2] = (float)item->value;
    if ((item = dict_get_unsafe(kwargs, "gear_tier_weight_3")))
        env->gear_tier_weights[3] = (float)item->value;
}

// ============================================================================
// my_log: episode metrics
// ============================================================================

void my_log(Log* log, Dict* out) {
    dict_set(out, "episode_return", log->episode_return);
    dict_set(out, "episode_length", log->episode_length);
    dict_set(out, "wins", log->wins);
    dict_set(out, "damage_dealt", log->damage_dealt);
    dict_set(out, "damage_received", log->damage_received);
}

// ============================================================================
// PFSP: set/get opponent pool weights across all envs in the flat array
// Called from Python via pybind wrappers in bindings.cpp
// ============================================================================

void osrs_pvp_set_pfsp_weights(void* vec_ptr, int* pool, int* cum_weights, int pool_size) {
    StaticVec* vec = (StaticVec*)vec_ptr;
    Env* envs = (Env*)vec->envs;
    if (pool_size > MAX_OPPONENT_POOL) pool_size = MAX_OPPONENT_POOL;
    for (int e = 0; e < vec->size; e++) {
        envs[e].pfsp.pool_size = pool_size;
        for (int i = 0; i < pool_size; i++) {
            envs[e].pfsp.pool[i] = (OpponentType)pool[i];
            envs[e].pfsp.cum_weights[i] = cum_weights[i];
        }
    }
}

void osrs_pvp_get_pfsp_stats(void* vec_ptr, float* out_wins, float* out_episodes, int* out_pool_size) {
    StaticVec* vec = (StaticVec*)vec_ptr;
    Env* envs = (Env*)vec->envs;
    int pool_size = 0;

    // Find max pool_size and zero output arrays
    for (int e = 0; e < vec->size; e++) {
        if (envs[e].pfsp.pool_size > pool_size)
            pool_size = envs[e].pfsp.pool_size;
    }
    *out_pool_size = pool_size;
    for (int i = 0; i < pool_size; i++) {
        out_wins[i] = 0.0f;
        out_episodes[i] = 0.0f;
    }

    // Aggregate and reset (read-and-reset pattern)
    for (int e = 0; e < vec->size; e++) {
        for (int i = 0; i < envs[e].pfsp.pool_size; i++) {
            out_wins[i] += envs[e].pfsp.wins[i];
            out_episodes[i] += envs[e].pfsp.episodes[i];
        }
        memset(envs[e].pfsp.wins, 0, sizeof(envs[e].pfsp.wins));
        memset(envs[e].pfsp.episodes, 0, sizeof(envs[e].pfsp.episodes));
    }
}

// ============================================================================
// Selfplay: enable P1 obs buffer + mask, push opponent actions
// Called from Python via pybind wrappers in bindings.cpp
// ============================================================================

void osrs_pvp_enable_selfplay(void* vec_ptr, float* obs_p1_buf, unsigned char* mask_buf) {
    StaticVec* vec = (StaticVec*)vec_ptr;
    Env* envs = (Env*)vec->envs;
    for (int e = 0; e < vec->size; e++) {
        envs[e].ocean_obs_p1 = obs_p1_buf + e * OCEAN_OBS_SIZE;
        envs[e].ocean_selfplay_mask = &mask_buf[e];
    }
}

void osrs_pvp_set_opponent_actions(void* vec_ptr, int* actions_buf) {
    StaticVec* vec = (StaticVec*)vec_ptr;
    Env* envs = (Env*)vec->envs;
    for (int e = 0; e < vec->size; e++) {
        memcpy(envs[e].external_opponent_actions,
               actions_buf + e * NUM_ACTION_HEADS,
               NUM_ACTION_HEADS * sizeof(int));
    }
}

void osrs_pvp_set_env_opponent_actions(void* vec_ptr, int env_idx, int* actions) {
    StaticVec* vec = (StaticVec*)vec_ptr;
    Env* envs = (Env*)vec->envs;
    memcpy(envs[env_idx].external_opponent_actions, actions, NUM_ACTION_HEADS * sizeof(int));
}

// ============================================================================
// my_put: runtime config updates
// ============================================================================

int my_put(void* _env, Dict* kwargs) {
    Env* env = (Env*)_env;
    DictItem* item;
    if ((item = dict_get_unsafe(kwargs, "shaping_enabled")))
        env->shaping.enabled = (int)item->value;
    if ((item = dict_get_unsafe(kwargs, "shaping_scale")))
        env->shaping.shaping_scale = (float)item->value;
    if ((item = dict_get_unsafe(kwargs, "use_c_opponent")))
        env->use_c_opponent = (int)item->value;
    if ((item = dict_get_unsafe(kwargs, "auto_reset")))
        env->auto_reset = (int)item->value;
    if ((item = dict_get_unsafe(kwargs, "seed"))) {
        env->rng_seed = (uint32_t)item->value;
        env->has_rng_seed = 1;
        pvp_seed(env, env->rng_seed);
    }
    if ((item = dict_get_unsafe(kwargs, "gear_tier_weight_0")))
        env->gear_tier_weights[0] = (float)item->value;
    if ((item = dict_get_unsafe(kwargs, "gear_tier_weight_1")))
        env->gear_tier_weights[1] = (float)item->value;
    if ((item = dict_get_unsafe(kwargs, "gear_tier_weight_2")))
        env->gear_tier_weights[2] = (float)item->value;
    if ((item = dict_get_unsafe(kwargs, "gear_tier_weight_3")))
        env->gear_tier_weights[3] = (float)item->value;
    return 0;
}
