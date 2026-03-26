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
#include "osrs_types.h"
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

    /* best-episode replay recording: all envs buffer their current episode's actions.
       on terminal, if the episode reached a new global best wave, flush to disk.
       binary format: [int32 num_ticks] [uint32 rng_state] [num_heads int32 per tick] */
    int* episode_actions;    /* buffer: episode_len * NUM_ATNS ints */
    int episode_action_cap;  /* max ticks we can buffer */
    int episode_action_len;  /* ticks buffered so far this episode */
    uint32_t episode_rng_start; /* RNG state at start of current episode */
} InfernoEnv;

#define OBS_SIZE INF_TOTAL_OBS
#define NUM_ATNS INF_NUM_ACTION_HEADS
#define ACT_SIZES { ENCOUNTER_MOVE_ACTIONS, 5, INF_MAX_NPCS+1, 5, 2, 4, 3, 2 }
#define OBS_TYPE FLOAT
#define ACT_TYPE DOUBLE
#define Env InfernoEnv

/* global best episode tracking — save if higher wave, or same wave but fewer ticks */
static int g_best_wave = 0;
static int g_best_ticks = 999999;

void c_step(Env* env) {
    for (int i = 0; i < NUM_ATNS; i++)
        env->acts_staging[i] = (int)env->actions[i];

    /* buffer actions for best-episode recording */
    if (env->episode_actions) {
        /* capture RNG state at the very start of the episode (before first action) */
        if (env->episode_action_len == 0)
            env->episode_rng_start = ((InfernoState*)env->enc_state)->rng_state;
        if (env->episode_action_len < env->episode_action_cap) {
            memcpy(&env->episode_actions[env->episode_action_len * NUM_ATNS],
                   env->acts_staging, NUM_ATNS * sizeof(int));
            env->episode_action_len++;
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
        env->log.unavoidable_off_prayer = (float)s->total_unavoidable_off;
        env->log.brews_remaining = (float)s->player_brew_doses;
        env->log.restores_remaining = (float)s->player_restore_doses;
        env->log.prayer_at_death = (float)s->player.current_prayer;
        env->log.n = 1.0f;  /* always report so sweep has continuous signal */
        env->log.npc_kills = (float)s->total_npc_kills;
        env->log.gear_switches = (float)s->total_gear_switches;
        env->log.current_ranged = (float)s->player.current_ranged;
        env->log.current_magic = (float)s->player.current_magic;
    }

    if (is_term) {
        /* check if this episode is a new global best — if so, flush replay to disk */
        if (env->episode_actions && env->episode_action_len > 0) {
            InfernoState* st = (InfernoState*)env->enc_state;
            int wave = st->wave;
            int ticks = env->episode_action_len;
            if (wave > g_best_wave || (wave == g_best_wave && ticks < g_best_ticks)) {
                g_best_wave = wave;
                g_best_ticks = ticks;
                const char* rpath = getenv("RECORD_REPLAY");
                if (rpath && rpath[0]) {
                    FILE* fp = fopen(rpath, "wb");
                    if (fp) {
                        fwrite(&env->episode_action_len, sizeof(int), 1, fp);
                        fwrite(&env->episode_rng_start, sizeof(uint32_t), 1, fp);
                        fwrite(env->episode_actions, sizeof(int),
                               env->episode_action_len * NUM_ATNS, fp);
                        fclose(fp);
                        fprintf(stderr, "replay: new best wave %d (%d ticks, rng=%u) saved to %s\n",
                                wave, env->episode_action_len, env->episode_rng_start, rpath);
                    }
                }
            }
        }
        env->episode_action_len = 0;

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
    free(env->episode_actions);
    env->episode_actions = NULL;
    if (env->enc_state) {
        ENCOUNTER_INFERNO.destroy(env->enc_state);
        env->enc_state = NULL;
    }
}

void c_render(Env* env) { (void)env; }

#include "vecenv.h"

/* max episode length for action buffer (INF_MAX_TICKS from encounter) */
#define REPLAY_MAX_TICKS INF_MAX_TICKS

void my_init(Env* env, Dict* kwargs) {
    env->num_agents = 1;
    env->enc_state = ENCOUNTER_INFERNO.create();
    memset(&env->log, 0, sizeof(Log));

    DictItem* start_wave = dict_get_unsafe(kwargs, "start_wave");
    if (start_wave)
        ENCOUNTER_INFERNO.put_int(env->enc_state, "start_wave", (int)start_wave->value);

    /* allocate action buffer for best-episode recording (all envs buffer) */
    if (getenv("RECORD_REPLAY") && getenv("RECORD_REPLAY")[0]) {
        env->episode_actions = (int*)malloc(REPLAY_MAX_TICKS * NUM_ATNS * sizeof(int));
        env->episode_action_cap = REPLAY_MAX_TICKS;
    } else {
        env->episode_actions = NULL;
        env->episode_action_cap = 0;
    }
    env->episode_action_len = 0;
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

    /* prayer analysis: correct rate + unavoidable breakdown */
    float prayer_rate = (log->prayer_total > 0.0f)
        ? log->prayer_correct / log->prayer_total : 0.0f;
    dict_set(out, "prayer_correct_rate", prayer_rate);
    /* what fraction of off-prayer hits were unavoidable (multi-style same tick) */
    float off_prayer = log->prayer_total - log->prayer_correct;
    float unavoidable_rate = (off_prayer > 0.0f)
        ? log->unavoidable_off_prayer / off_prayer : 0.0f;
    dict_set(out, "unavoidable_off_prayer_rate", unavoidable_rate);
    dict_set(out, "unavoidable_off_prayer", log->unavoidable_off_prayer);

    dict_set(out, "brews_remaining", log->brews_remaining);
    dict_set(out, "restores_remaining", log->restores_remaining);
    dict_set(out, "prayer_at_death", log->prayer_at_death);

    dict_set(out, "npc_kills", log->npc_kills);
    dict_set(out, "gear_switches", log->gear_switches);
    dict_set(out, "current_ranged", log->current_ranged);
    dict_set(out, "current_magic", log->current_magic);
    float gear_switch_rate = (log->episode_length > 0.0f)
        ? log->gear_switches / log->episode_length : 0.0f;
    dict_set(out, "gear_switch_rate", gear_switch_rate);

    float wr = log->wins;
    float wave_progress = log->episode_length / (float)INF_MAX_TICKS;
    float score = wr + (1.0f - wr) * wave_progress * 0.5f - (1.0f - wr);
    dict_set(out, "score", score);
}
