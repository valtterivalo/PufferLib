/**
 * @file binding.c
 * @brief Static-native binding for OSRS Inferno encounter.
 *
 * Bridges vecenv.h's contract (float actions, float terminals) with the
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
    float* actions;
    float* rewards;
    float* terminals;
    int num_agents;
    int rng;
    Log log;

    EncounterState* enc_state;
    int config_start_wave;  /* the start_wave from config (not curriculum override) */

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
#define ACT_TYPE FLOAT
#define Env InfernoEnv

/* global best episode tracking */
static int g_best_wave = 0;
static int g_best_ticks = 999999;
static int g_best_zuk_hp = 999999;  /* lowest Zuk HP seen (for Zuk-only training) */

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
        env->log.brews_remaining = (float)s->player.brew_doses;
        env->log.restores_remaining = (float)s->player.restore_doses;
        env->log.prayer_at_death = (float)s->player.current_prayer;
        for (int t = 0; t < INF_NUM_NPC_TYPES; t++) {
            env->log.prayer_correct_by_type[t] = (float)s->prayer_correct_by_type[t];
            env->log.attacks_by_type[t] = (float)s->attacks_by_type[t];
            env->log.dmg_from_type[t] = s->dmg_from_type[t];
            env->log.killed_by_type[t] = (float)s->killed_by_type[t];
        }
        /* agents report to log if their start_wave matches the config's start_wave.
           curriculum agents (overridden to a different wave) are excluded from metrics
           so they don't pollute the sweep score. when training Zuk-only (config start_wave=69),
           all agents start at 69 and all report. */
        env->log.n = (s->start_wave == env->config_start_wave) ? 1.0f : 0.0f;
        env->log.npc_kills = (float)s->total_npc_kills;
        env->log.gear_switches = (float)s->total_gear_switches;
        env->log.current_ranged = (float)s->player.current_ranged;
        env->log.current_magic = (float)s->player.current_magic;

        /* Zuk shield tracking */
        env->log.behind_shield_pct = (s->total_zuk_ticks > 0)
            ? (float)s->behind_shield_ticks / (float)s->total_zuk_ticks : 0.0f;

        /* action noop rates */
        float at = (float)s->action_total_count;
        if (at > 0.0f) {
            env->log.noop_move   = (float)s->action_noop_count[0] / at;
            env->log.noop_prayer = (float)s->action_noop_count[1] / at;
            env->log.noop_target = (float)s->action_noop_count[2] / at;
            env->log.noop_gear   = (float)s->action_noop_count[3] / at;
            env->log.noop_eat    = (float)s->action_noop_count[4] / at;
            env->log.noop_potion = (float)s->action_noop_count[5] / at;
            env->log.noop_spell  = (float)s->action_noop_count[6] / at;
            env->log.noop_spec   = (float)s->action_noop_count[7] / at;
        }
    }

    if (is_term) {
        /* check if this episode is a new global best — if so, flush replay to disk.
           for full runs (start_wave 0): best = highest wave reached, then fewest ticks.
           for zuk-only (start_wave 68+): best = most damage to zuk (lowest zuk HP), then fewest ticks.
           curriculum starts from mid-waves also record. */
        if (env->episode_actions && env->episode_action_len > 0) {
            InfernoState* st = (InfernoState*)env->enc_state;
            int wave = st->wave;
            int ticks = env->episode_action_len;
            int is_new_best = 0;
            if (st->start_wave == 0) {
                /* full run: best wave, then fewest ticks */
                is_new_best = (wave > g_best_wave || (wave == g_best_wave && ticks < g_best_ticks));
            } else {
                /* partial/zuk run: best = most damage to zuk (lowest HP remaining).
                   if zuk is dead (winner==0), fastest kill (fewest ticks) wins. */
                int zuk_hp = 999999;
                for (int n = 0; n < INF_MAX_NPCS; n++) {
                    if (st->npcs[n].active && st->npcs[n].type == INF_NPC_ZUK) {
                        zuk_hp = st->npcs[n].hp;
                        break;
                    }
                }
                if (st->winner == 0) zuk_hp = 0;  /* zuk dead */
                is_new_best = (zuk_hp < g_best_zuk_hp ||
                              (zuk_hp == g_best_zuk_hp && zuk_hp == 0 && ticks < g_best_ticks));
                if (is_new_best) g_best_zuk_hp = zuk_hp;
            }
            if (is_new_best) {
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
                        if (st->start_wave >= 68) {
                            fprintf(stderr, "replay: new best zuk hp=%d (%d ticks, rng=%u) saved to %s\n",
                                    g_best_zuk_hp, env->episode_action_len, env->episode_rng_start, rpath);
                        } else {
                            fprintf(stderr, "replay: new best wave %d (%d ticks, rng=%u) saved to %s\n",
                                    wave, env->episode_action_len, env->episode_rng_start, rpath);
                        }
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

#define MY_VEC_INIT
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
    /* match the 1-indexed → 0-indexed conversion done by encounter's put_int */
    int sw = start_wave ? (int)start_wave->value : 0;
    env->config_start_wave = (sw > 0) ? sw - 1 : 0;

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

/* curriculum wave mixing: start some agents at later waves for late-game gradient signal.
   wave-0 agents are scored normally; curriculum agents train but don't affect sweep metric. */
#define MAX_CURRICULUM_TIERS 4

Env* my_vec_init(int* num_envs_out, int* buffer_env_starts, int* buffer_env_counts,
                 Dict* vec_kwargs, Dict* env_kwargs) {
    int total_agents = (int)dict_get(vec_kwargs, "total_agents")->value;
    int num_buffers = (int)dict_get(vec_kwargs, "num_buffers")->value;
    int agents_per_buffer = total_agents / num_buffers;

    /* parse curriculum tiers from env config */
    static const char* wave_keys[] = {
        "curriculum_wave_1","curriculum_wave_2","curriculum_wave_3","curriculum_wave_4"
    };
    static const char* frac_keys[] = {
        "curriculum_frac_1","curriculum_frac_2","curriculum_frac_3","curriculum_frac_4"
    };
    int curriculum_waves[MAX_CURRICULUM_TIERS];
    float curriculum_fracs[MAX_CURRICULUM_TIERS];
    int num_tiers = 0;
    for (int i = 0; i < MAX_CURRICULUM_TIERS; i++) {
        DictItem* w = dict_get_unsafe(env_kwargs, wave_keys[i]);
        DictItem* f = dict_get_unsafe(env_kwargs, frac_keys[i]);
        if (w && f && f->value > 0.0) {
            curriculum_waves[num_tiers] = (int)w->value;
            curriculum_fracs[num_tiers] = (float)f->value;
            num_tiers++;
        }
    }

    /* allocate and init all envs (same as default my_vec_init) */
    Env* envs = (Env*)calloc(total_agents, sizeof(Env));
    int num_envs = 0;
    int agents_created = 0;
    while (agents_created < total_agents) {
        srand(num_envs);
        envs[num_envs].rng = num_envs;
        my_init(&envs[num_envs], env_kwargs);
        agents_created += envs[num_envs].num_agents;
        num_envs++;
    }
    envs = (Env*)realloc(envs, num_envs * sizeof(Env));

    /* assign curriculum start_waves to agents at the end of the array */
    if (num_tiers > 0) {
        int tier_counts[MAX_CURRICULUM_TIERS];
        int curriculum_total = 0;
        for (int t = 0; t < num_tiers; t++) {
            tier_counts[t] = (int)(curriculum_fracs[t] * num_envs);
            if (tier_counts[t] < 1) tier_counts[t] = 1;
            curriculum_total += tier_counts[t];
        }
        int wave0_count = num_envs - curriculum_total;
        int cursor = wave0_count;
        for (int t = 0; t < num_tiers; t++) {
            for (int i = 0; i < tier_counts[t] && cursor < num_envs; i++, cursor++) {
                ENCOUNTER_INFERNO.put_int(envs[cursor].enc_state,
                    "start_wave", curriculum_waves[t]);
            }
        }
        fprintf(stderr, "curriculum: %d wave-0", wave0_count);
        for (int t = 0; t < num_tiers; t++)
            fprintf(stderr, ", %d wave-%d", tier_counts[t], curriculum_waves[t]);
        fprintf(stderr, " (%d total)\n", num_envs);
    }

    /* fill buffer info (same as default) */
    int buf = 0;
    int buf_agents = 0;
    buffer_env_starts[0] = 0;
    buffer_env_counts[0] = 0;
    for (int i = 0; i < num_envs; i++) {
        buf_agents += envs[i].num_agents;
        buffer_env_counts[buf]++;
        if (buf_agents >= agents_per_buffer && buf < num_buffers - 1) {
            buf++;
            buffer_env_starts[buf] = i + 1;
            buffer_env_counts[buf] = 0;
            buf_agents = 0;
        }
    }

    *num_envs_out = num_envs;
    return envs;
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
    dict_set(out, "behind_shield_pct", log->behind_shield_pct);
    dict_set(out, "noop_move", log->noop_move);
    dict_set(out, "noop_prayer", log->noop_prayer);
    dict_set(out, "noop_target", log->noop_target);
    dict_set(out, "noop_gear", log->noop_gear);
    dict_set(out, "noop_eat", log->noop_eat);
    dict_set(out, "noop_potion", log->noop_potion);
    dict_set(out, "noop_spell", log->noop_spell);
    dict_set(out, "noop_spec", log->noop_spec);
    float gear_switch_rate = (log->episode_length > 0.0f)
        ? log->gear_switches / log->episode_length : 0.0f;
    dict_set(out, "gear_switch_rate", gear_switch_rate);

    float wr = log->wins;
    float wave_progress = log->episode_length / (float)INF_MAX_TICKS;
    float score = wr + (1.0f - wr) * wave_progress * 0.5f - (1.0f - wr);
    dict_set(out, "score", score);

    /* per-NPC-type prayer rates and damage (wandb only).
       keys must be string literals — dict_set stores the pointer, not a copy. */
    static const char* pray_keys[] = {
        "pray_nibbler","pray_bat","pray_blob","pray_blob_mel","pray_blob_rng","pray_blob_mag",
        "pray_meleer","pray_ranger","pray_mager","pray_jad","pray_zuk","pray_heal_jad","pray_heal_zuk","pray_shield"
    };
    static const char* dmg_keys[] = {
        "dmg_from_nibbler","dmg_from_bat","dmg_from_blob","dmg_from_blob_mel","dmg_from_blob_rng","dmg_from_blob_mag",
        "dmg_from_meleer","dmg_from_ranger","dmg_from_mager","dmg_from_jad","dmg_from_zuk","dmg_from_heal_jad","dmg_from_heal_zuk","dmg_from_shield"
    };
    static const char* kill_keys[] = {
        "killed_by_nibbler","killed_by_bat","killed_by_blob","killed_by_blob_mel","killed_by_blob_rng","killed_by_blob_mag",
        "killed_by_meleer","killed_by_ranger","killed_by_mager","killed_by_jad","killed_by_zuk","killed_by_heal_jad","killed_by_heal_zuk","killed_by_shield"
    };
    for (int t = 0; t < INF_NUM_NPC_TYPES; t++) {
        if (log->attacks_by_type[t] > 0.0f) {
            dict_set(out, pray_keys[t], log->prayer_correct_by_type[t] / log->attacks_by_type[t]);
            dict_set(out, dmg_keys[t], log->dmg_from_type[t]);
        }
        if (log->killed_by_type[t] > 0.0f)
            dict_set(out, kill_keys[t], log->killed_by_type[t]);
    }
}
