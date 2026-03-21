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

    /* replay recording: env 0 writes actions to this file during c_step.
       binary format matches run_visual's replay loader:
       header: int32 num_ticks (patched at close), then num_heads int32 per tick. */
    FILE* record_fp;
    int record_ticks;
} InfernoEnv;

#define OBS_SIZE INF_TOTAL_OBS
#define NUM_ATNS INF_NUM_ACTION_HEADS
#define ACT_SIZES { ENCOUNTER_MOVE_ACTIONS, 5, INF_MAX_NPCS+1, 5, 2, 4, 2 }
#define OBS_TYPE FLOAT
#define ACT_TYPE DOUBLE
#define Env InfernoEnv

void c_step(Env* env) {
    for (int i = 0; i < NUM_ATNS; i++)
        env->acts_staging[i] = (int)env->actions[i];

    /* record actions for replay (env 0 only, when recording is active) */
    if (env->record_fp) {
        fwrite(env->acts_staging, sizeof(int), NUM_ATNS, env->record_fp);
        env->record_ticks++;
        /* update header + flush every 1000 ticks so data survives kills */
        if (env->record_ticks % 1000 == 0) {
            long pos = ftell(env->record_fp);
            fseek(env->record_fp, 0, SEEK_SET);
            fwrite(&env->record_ticks, sizeof(int), 1, env->record_fp);
            fseek(env->record_fp, pos, SEEK_SET);
            fflush(env->record_fp);
        }
    }

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
        env->log.wave = (float)s->wave;
        env->log.prayer_correct = (float)s->total_prayer_correct;
        env->log.prayer_total = (float)s->total_npc_attacks;
        env->log.idle_ticks = (float)s->total_idle_ticks;
        env->log.brews_used = (float)s->total_brews_used;
        env->log.blood_healed = (float)s->total_blood_healed;
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
    /* finalize replay recording: patch the tick count in the header */
    if (env->record_fp) {
        fseek(env->record_fp, 0, SEEK_SET);
        fwrite(&env->record_ticks, sizeof(int), 1, env->record_fp);
        fclose(env->record_fp);
        env->record_fp = NULL;
        fprintf(stderr, "replay: recorded %d ticks\n", env->record_ticks);
    }
    if (env->enc_state) {
        ENCOUNTER_INFERNO.destroy(env->enc_state);
        env->enc_state = NULL;
    }
}

void c_render(Env* env) { (void)env; }

#include "vecenv.h"

/* global: only env 0 records. set by first my_init if RECORD_REPLAY is set. */
static int g_record_claimed = 0;

void my_init(Env* env, Dict* kwargs) {
    env->num_agents = 1;
    env->enc_state = ENCOUNTER_INFERNO.create();
    memset(&env->log, 0, sizeof(Log));
    env->record_fp = NULL;
    env->record_ticks = 0;

    DictItem* start_wave = dict_get_unsafe(kwargs, "start_wave");
    if (start_wave)
        ENCOUNTER_INFERNO.put_int(env->enc_state, "start_wave", (int)start_wave->value);

    DictItem* mask_in_obs = dict_get_unsafe(kwargs, "mask_in_obs");
    (void)mask_in_obs;  /* always embedded for inferno */

    /* reward shaping config (sweepable via [env] section) */
    static const char* reward_keys[] = {
        "wave_reward_base", "wave_reward_scale", "brew_penalty",
        "brew_penalty_midpoint", "brew_penalty_width", "blood_heal_reward"
    };
    for (int i = 0; i < 6; i++) {
        DictItem* item = dict_get_unsafe(kwargs, reward_keys[i]);
        if (item) ENCOUNTER_INFERNO.put_float(env->enc_state, reward_keys[i], item->value);
    }

    /* env 0 records actions when RECORD_REPLAY env var is set */
    if (!g_record_claimed) {
        g_record_claimed = 1;
        const char* rpath = getenv("RECORD_REPLAY");
        if (rpath && rpath[0]) {
            env->record_fp = fopen(rpath, "wb");
            if (env->record_fp) {
                int placeholder = 0;
                fwrite(&placeholder, sizeof(int), 1, env->record_fp);
                fprintf(stderr, "replay: recording env 0 to %s\n", rpath);
            }
        }
    }
}

void my_log(Log* log, Dict* out) {
    dict_set(out, "episode_return", log->episode_return);
    dict_set(out, "damage_dealt", log->damage_dealt);
    dict_set(out, "damage_received", log->damage_received);
    dict_set(out, "episode_length", log->episode_length);
    dict_set(out, "wins", log->wins);
    dict_set(out, "wave", log->wave);
    dict_set(out, "idle_ticks", log->idle_ticks);
    dict_set(out, "brews_used", log->brews_used);
    dict_set(out, "blood_healed", log->blood_healed);

    /* prayer correct rate: fraction of NPC attacks blocked by correct prayer */
    float prayer_rate = (log->prayer_total > 0.0f)
        ? log->prayer_correct / log->prayer_total : 0.0f;
    dict_set(out, "prayer_correct_rate", prayer_rate);

    float wr = log->wins;
    float wave_progress = log->episode_length / (float)INF_MAX_TICKS;
    float score = wr + (1.0f - wr) * wave_progress * 0.5f - (1.0f - wr);
    dict_set(out, "score", score);
}
