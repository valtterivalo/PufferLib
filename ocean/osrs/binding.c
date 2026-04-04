/**
 * @file binding.c
 * @brief Metal static-native binding for OSRS PVP environment
 *
 * Bridges vecenv.h's contract (float actions, float terminals) with the PVP
 * env's internal types (int actions, unsigned char terminals) using a wrapper
 * struct. PVP source headers are untouched.
 */

#include "osrs_env.h"

/* Wrapper struct: vecenv-compatible fields at top + embedded OsrsEnv.
 * vecenv.h's create_static_vec assigns to env->observations, env->actions,
 * env->rewards, env->terminals directly. These fields must match vecenv's
 * expected types (void*, float*, float*, float*). The embedded OsrsEnv has
 * its own identically-named fields with different types — pvp_init sets those
 * to internal inline buffers, so there's no conflict. */
typedef struct {
    void* observations;
    float* actions;
    float* rewards;
    float* terminals;
    int num_agents;
    int rng;
    Log log;

    OsrsEnv pvp;

    /* staging buffers for type conversion */
    int ocean_acts_staging[NUM_ACTION_HEADS];
    unsigned char ocean_term_staging;
} MetalPvpEnv;

#define OBS_SIZE OCEAN_OBS_SIZE
#define NUM_ATNS NUM_ACTION_HEADS
#define ACT_SIZES {LOADOUT_DIM, COMBAT_DIM, OVERHEAD_DIM, FOOD_DIM, POTION_DIM, KARAMBWAN_DIM, VENG_DIM}
#define OBS_TENSOR_T FloatTensor
#define Env MetalPvpEnv

/* c_step/c_reset/c_close/c_render must be defined BEFORE including vecenv.h
 * because vecenv.h calls them inside its implementation section without
 * forward-declaring them (they're expected to come from the env header). */

void c_step(Env* env) {
    /* float actions from vecenv → int staging for PVP */
    for (int i = 0; i < NUM_ATNS; i++) {
        env->ocean_acts_staging[i] = (int)env->actions[i];
    }

    pvp_step(&env->pvp);

    /* terminal: unsigned char → float for vecenv */
    env->terminals[0] = (float)env->ocean_term_staging;

    /* copy PVP log to wrapper log on episode end */
    if (env->ocean_term_staging) {
        env->log.episode_return = env->pvp.log.episode_return;
        env->log.episode_length = env->pvp.log.episode_length;
        env->log.wins = env->pvp.log.wins;
        env->log.damage_dealt = env->pvp.log.damage_dealt;
        env->log.damage_received = env->pvp.log.damage_received;
        env->log.n = env->pvp.log.n;
        memset(&env->pvp.log, 0, sizeof(env->pvp.log));
    }

    if (env->ocean_term_staging && env->pvp.auto_reset) {
        ocean_write_obs(&env->pvp);
    }
}

void c_reset(Env* env) {
    /* Wire ocean pointers to vecenv shared buffers (deferred from my_init because
     * create_static_vec assigns env->observations/rewards AFTER my_vec_init). */
    env->pvp.ocean_obs = (float*)env->observations;
    env->pvp.ocean_rew = env->rewards;
    env->pvp.ocean_term = &env->ocean_term_staging;
    env->pvp.ocean_acts = env->ocean_acts_staging;

    pvp_reset(&env->pvp);
    ocean_write_obs(&env->pvp);
    env->pvp.ocean_rew[0] = 0.0f;
    env->pvp.ocean_term[0] = 0;
    env->terminals[0] = 0.0f;
}

void c_close(Env* env) { pvp_close(&env->pvp); }
void c_render(Env* env) { (void)env; }

#include "vecenv.h"

void my_init(Env* env, Dict* kwargs) {
    env->num_agents = 1;

    pvp_init(&env->pvp);

    /* Ocean pointer wiring is DEFERRED to c_reset because my_init runs inside
     * my_vec_init BEFORE create_static_vec assigns the shared buffer pointers
     * (env->observations, env->actions, env->rewards, env->terminals are NULL
     * at this point). c_reset runs after buffer assignment and does the wiring.
     *
     * For now, point ocean pointers at internal staging so pvp_reset doesn't
     * crash on writes to ocean_term/ocean_rew. */
    env->pvp.ocean_obs = NULL;
    env->pvp.ocean_rew = env->pvp._rews_buf;
    env->pvp.ocean_term = &env->ocean_term_staging;
    env->pvp.ocean_acts = env->ocean_acts_staging;
    env->pvp.ocean_obs_p1 = NULL;
    env->pvp.ocean_selfplay_mask = NULL;

    /* config from Dict (all values are double) */
    env->pvp.use_c_opponent = 1;
    env->pvp.auto_reset = 1;
    env->pvp.is_lms = 1;

    DictItem* opp = dict_get_unsafe(kwargs, "opponent_type");
    env->pvp.opponent.type = opp ? (OpponentType)(int)opp->value : OPP_IMPROVED;

    DictItem* shaping_scale = dict_get_unsafe(kwargs, "shaping_scale");
    env->pvp.shaping.shaping_scale = shaping_scale ? (float)shaping_scale->value : 0.0f;

    DictItem* shaping_en = dict_get_unsafe(kwargs, "shaping_enabled");
    env->pvp.shaping.enabled = shaping_en ? (int)shaping_en->value : 0;

    /* reward shaping coefficients (same defaults as ocean_binding.c) */
    env->pvp.shaping.damage_dealt_coef = 0.005f;
    env->pvp.shaping.damage_received_coef = -0.005f;
    env->pvp.shaping.correct_prayer_bonus = 0.03f;
    env->pvp.shaping.wrong_prayer_penalty = -0.02f;
    env->pvp.shaping.prayer_switch_no_attack_penalty = -0.01f;
    env->pvp.shaping.off_prayer_hit_bonus = 0.03f;
    env->pvp.shaping.melee_frozen_penalty = -0.05f;
    env->pvp.shaping.wasted_eat_penalty = -0.001f;
    env->pvp.shaping.premature_eat_penalty = -0.02f;
    env->pvp.shaping.magic_no_staff_penalty = -0.05f;
    env->pvp.shaping.gear_mismatch_penalty = -0.05f;
    env->pvp.shaping.spec_off_prayer_bonus = 0.02f;
    env->pvp.shaping.spec_low_defence_bonus = 0.01f;
    env->pvp.shaping.spec_low_hp_bonus = 0.02f;
    env->pvp.shaping.smart_triple_eat_bonus = 0.05f;
    env->pvp.shaping.wasted_triple_eat_penalty = -0.0005f;
    env->pvp.shaping.damage_burst_bonus = 0.002f;
    env->pvp.shaping.damage_burst_threshold = 30;
    env->pvp.shaping.premature_eat_threshold = 0.7071f;
    env->pvp.shaping.ko_bonus = 0.15f;
    env->pvp.shaping.wasted_resources_penalty = -0.07f;
    env->pvp.shaping.prayer_penalty_enabled = 1;
    env->pvp.shaping.click_penalty_enabled = 0;
    env->pvp.shaping.click_penalty_threshold = 5;
    env->pvp.shaping.click_penalty_coef = -0.003f;

    /* gear: default tier 0 (basic LMS) */
    env->pvp.gear_tier_weights[0] = 1.0f;
    env->pvp.gear_tier_weights[1] = 0.0f;
    env->pvp.gear_tier_weights[2] = 0.0f;
    env->pvp.gear_tier_weights[3] = 0.0f;

    /* pvp_reset sets up game state (players, positions, gear, etc.)
     * but does NOT write to ocean buffers — that happens in c_reset. */
    pvp_reset(&env->pvp);
}

void my_log(Log* log, Dict* out) {
    dict_set(out, "episode_return", log->episode_return);
    dict_set(out, "episode_length", log->episode_length);
    dict_set(out, "wins", log->wins);
    dict_set(out, "damage_dealt", log->damage_dealt);
    dict_set(out, "damage_received", log->damage_received);
}

/* ========================================================================
 * PFSP: set/get opponent pool weights across all envs
 * Called from Python via pybind11 wrappers in metal_bindings.mm
 * ======================================================================== */

void binding_set_pfsp_weights(StaticVec* vec, int* pool, int* cum_weights, int pool_size) {
    Env* envs = (Env*)vec->envs;
    if (pool_size > MAX_OPPONENT_POOL) pool_size = MAX_OPPONENT_POOL;
    for (int e = 0; e < vec->size; e++) {
        int was_unconfigured = (envs[e].pvp.pfsp.pool_size == 0);
        envs[e].pvp.pfsp.pool_size = pool_size;
        for (int i = 0; i < pool_size; i++) {
            envs[e].pvp.pfsp.pool[i] = (OpponentType)pool[i];
            envs[e].pvp.pfsp.cum_weights[i] = cum_weights[i];
        }
        /* Only reset on first configuration — restarts the episode that was started
         * during env creation before the pool was set (would have used fallback opponent).
         * Periodic weight updates must NOT reset: that would corrupt PufferLib's rollout. */
        if (was_unconfigured) {
            c_reset(&envs[e]);
        }
    }
}

void binding_get_pfsp_stats(StaticVec* vec, float* out_wins, float* out_episodes, int* out_pool_size) {
    Env* envs = (Env*)vec->envs;
    int pool_size = 0;

    for (int e = 0; e < vec->size; e++) {
        if (envs[e].pvp.pfsp.pool_size > pool_size)
            pool_size = envs[e].pvp.pfsp.pool_size;
    }
    *out_pool_size = pool_size;
    for (int i = 0; i < pool_size; i++) {
        out_wins[i] = 0.0f;
        out_episodes[i] = 0.0f;
    }

    /* Aggregate and reset (read-and-reset pattern) */
    for (int e = 0; e < vec->size; e++) {
        for (int i = 0; i < envs[e].pvp.pfsp.pool_size; i++) {
            out_wins[i] += envs[e].pvp.pfsp.wins[i];
            out_episodes[i] += envs[e].pvp.pfsp.episodes[i];
        }
        memset(envs[e].pvp.pfsp.wins, 0, sizeof(envs[e].pvp.pfsp.wins));
        memset(envs[e].pvp.pfsp.episodes, 0, sizeof(envs[e].pvp.pfsp.episodes));
    }
}
