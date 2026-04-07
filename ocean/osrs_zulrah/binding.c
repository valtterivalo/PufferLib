/**
 * @file binding.c
 * @brief Static-native binding for OSRS Zulrah encounter.
 *
 * Bridges vecenv.h's contract (float actions, float terminals) with the
 * Zulrah encounter's vtable interface. Uses the encounter system (EncounterDef)
 * rather than OsrsEnv directly.
 */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "osrs_encounter.h"
#include "osrs_types.h"
#include "encounters/encounter_zulrah.h"

/* total obs = raw obs + action mask */
#define ZUL_TOTAL_OBS (ZUL_NUM_OBS + ZUL_ACTION_MASK_SIZE)

/* wrapper struct: vecenv-compatible fields at top + encounter state.
 * vecenv.h's create_static_vec assigns env->observations, env->actions,
 * env->rewards, env->terminals directly. */
typedef struct {
    void* observations;
    float* actions;
    float* rewards;
    float* terminals;
    int num_agents;
    int rng;
    Log log;

    EncounterState* enc_state;

    /* staging buffer for action type conversion */
    int acts_staging[ZUL_NUM_ACTION_HEADS];
    unsigned char term_staging;
} ZulrahEnv;

#define OBS_SIZE ZUL_TOTAL_OBS
#define NUM_ATNS ZUL_NUM_ACTION_HEADS
#define ACT_SIZES {ZUL_MOVE_DIM, ZUL_ATTACK_DIM, ZUL_PRAYER_DIM, ZUL_FOOD_DIM, ZUL_POTION_DIM, ZUL_SPEC_DIM}
#define OBS_TYPE FLOAT
#define ACT_TYPE FLOAT
#define Env ZulrahEnv

/* c_step/c_reset/c_close/c_render must be defined BEFORE including vecenv.h */

void c_step(Env* env) {
    /* float actions -> int staging */
    for (int i = 0; i < NUM_ATNS; i++) {
        env->acts_staging[i] = (int)env->actions[i];
    }

    ENCOUNTER_ZULRAH.step(env->enc_state, env->acts_staging);

    /* write obs + mask directly (mask appended after raw obs) */
    float* obs = (float*)env->observations;
    ENCOUNTER_ZULRAH.write_obs(env->enc_state, obs);
    ENCOUNTER_ZULRAH.write_mask(env->enc_state, obs + ZUL_NUM_OBS);

    /* reward */
    env->rewards[0] = ENCOUNTER_ZULRAH.get_reward(env->enc_state);

    /* terminal */
    int is_term = ENCOUNTER_ZULRAH.is_terminal(env->enc_state);
    env->term_staging = (unsigned char)is_term;
    env->terminals[0] = (float)is_term;

    /* log directly into env->log (vecenv accumulates + clears periodically).
       bypass get_log vtable to avoid double-counting from encounter's own += */
    if (is_term) {
        ZulrahState* zs = (ZulrahState*)env->enc_state;
        env->log.episode_return += zs->episode_return;
        env->log.episode_length += (float)zs->tick;
        env->log.wins += (zs->winner == 0) ? 1.0f : 0.0f;
        env->log.damage_dealt += zs->total_damage_dealt;
        env->log.damage_received += zs->total_damage_received;
        env->log.prayer_correct += (float)zs->total_prayer_correct;
        env->log.prayer_total += (float)zs->total_prayer_total;
        env->log.gear_switches += (float)zs->total_gear_switches;
        env->log.npc_kills += (float)zs->total_food_eaten;
        env->log.idle_ticks += (float)zs->total_potions_used;
        env->log.brews_used += (float)zs->total_venom_ticks;
        env->log.wave += (float)zs->total_phases_completed;
        env->log.n += 1.0f;

        /* auto-reset */
        ENCOUNTER_ZULRAH.reset(env->enc_state, 0);
        ENCOUNTER_ZULRAH.write_obs(env->enc_state, obs);
        ENCOUNTER_ZULRAH.write_mask(env->enc_state, obs + ZUL_NUM_OBS);
    }
}

void c_reset(Env* env) {
    ENCOUNTER_ZULRAH.reset(env->enc_state, 0);

    float* obs = (float*)env->observations;
    ENCOUNTER_ZULRAH.write_obs(env->enc_state, obs);
    ENCOUNTER_ZULRAH.write_mask(env->enc_state, obs + ZUL_NUM_OBS);

    env->rewards[0] = 0.0f;
    env->term_staging = 0;
    env->terminals[0] = 0.0f;
}

void c_close(Env* env) {
    if (env->enc_state) {
        ENCOUNTER_ZULRAH.destroy(env->enc_state);
        env->enc_state = NULL;
    }
}

void c_render(Env* env) { (void)env; }

#include "vecenv.h"

void my_init(Env* env, Dict* kwargs) {
    env->num_agents = 1;
    env->enc_state = ENCOUNTER_ZULRAH.create();
    memset(&env->log, 0, sizeof(Log));

    /* gear tier config (default 0 = budget) */
    DictItem* gear = dict_get_unsafe(kwargs, "gear_tier");
    if (gear) {
        ENCOUNTER_ZULRAH.put_int(env->enc_state, "gear_tier", (int)gear->value);
    }
}

void my_log(Log* log, Dict* out) {
    dict_set(out, "episode_return", log->episode_return);
    dict_set(out, "episode_length", log->episode_length);
    dict_set(out, "wins", log->wins);
    dict_set(out, "damage_dealt", log->damage_dealt);
    dict_set(out, "damage_received", log->damage_received);

    /* prayer correctness rate */
    float prayer_rate = (log->prayer_total > 0.0f)
        ? log->prayer_correct / log->prayer_total : 0.0f;
    dict_set(out, "prayer_correct_rate", prayer_rate);

    /* behavioral metrics (stored in reused Log fields) */
    dict_set(out, "gear_switches", log->gear_switches);
    dict_set(out, "food_eaten", log->npc_kills);      /* reused field */
    dict_set(out, "potions_used", log->idle_ticks);    /* reused field */
    dict_set(out, "venom_ticks", log->brews_used);     /* reused field */
    dict_set(out, "phases_completed", log->wave);      /* reused field */

    /* composite score: winrate-gated efficiency */
    float wr = log->wins;
    float speed_bonus = (wr > 0.1f)
        ? (1.0f - log->episode_length / (float)ZUL_MAX_TICKS) * 0.3f : 0.0f;
    float dmg_penalty = (wr > 0.1f)
        ? (log->damage_received / (float)ZUL_BASE_HP) * 0.2f : 0.0f;
    float score = wr + speed_bonus - dmg_penalty;
    dict_set(out, "score", score);
}
