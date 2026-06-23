/**
 * @file binding.c
 * @brief Static-native binding for the OSRS Fortis Colosseum encounter.
 *
 * Bridges vecenv.h's contract (float actions, float terminals) with the
 * Colosseum encounter's vtable. Lean version: no trace/replay machinery.
 *
 * Ordering matters: vecenv.h calls c_reset/c_step/c_close/c_render directly, so
 * those are defined before the include. my_init/my_vec_init/my_log use Dict
 * (declared by vecenv.h), so they are defined after the include.
 */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <pthread.h>
#include <stdint.h>

#ifdef __cplusplus
#define _Thread_local thread_local
#endif

#include "../osrs/osrs_env.h"  /* pulls in osrs_types, encounter, pvp stack */
#include "../osrs/osrs_assets.h"

#define COLOSSEUM_ENV_EXPORT __attribute__((visibility("default")))

#include "colosseum_profile.h"

/* encounter headers + render.h have many static helpers only used by the
   standalone viewer (not c_render) — suppress unused-function noise. */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#include "../osrs/encounters/encounter_colosseum.h"
#include "../osrs/encounters/encounter_inferno.h"  /* render.h references InfernoState */
#include "../osrs/encounters/encounter_zulrah.h"   /* render.h references ZulrahState */
#ifdef __CUDACC__
#define float3 raymath_float3
#define float16 raymath_float16
#endif
#include "../osrs/osrs_render.h"
#ifdef __CUDACC__
#undef float3
#undef float16
#endif
#include "../osrs/osrs_scene_assets.h"
#pragma GCC diagnostic pop

#define COLO_TOTAL_OBS (COLO_NUM_OBS + COLO_ACTION_MASK_SIZE)

typedef struct ColosseumEnv {
    void* observations;
    float* actions;
    float* rewards;
    float* terminals;
    float* truncations;
    int num_agents;
    int rng;
    Log log;

    ColosseumState state;
    ColosseumContext context;
    int config_start_wave;  /* start_wave from config (not curriculum override) */

    int acts_staging[COLO_NUM_ACTION_HEADS];
    unsigned char term_staging;

    float ticks_per_second;
    double last_step_time;

    int pending_render_reset;
    OsrsEnv render_env;
} ColosseumEnv;

#define COLO_ENV_STATE(env) ((EncounterState*)&((env)->state))
#define COLO_ENV_CONTEXT(env) ((EncounterContext*)&((env)->context))

#define OBS_SIZE COLO_TOTAL_OBS
#define NUM_ATNS COLO_NUM_ACTION_HEADS
#define ACT_SIZES COLO_ACTION_DIMS_INIT
#define OBS_TENSOR_T FloatTensor
#define Env ColosseumEnv
#define MY_TRUNCATIONS 1

#define MAX_CURRICULUM_TIERS 8

typedef enum {
    COLO_LOG_CURRENT_SET_ARGMAX_DPT_SLOT = 0,
    COLO_LOG_ATTACKED_ARGMAX_SET_SLOT = 1,
} ColoLogDptSlot;

static void col_log_dpt_rate_sample(Log* log, ColoLogDptSlot slot, int sample) {
    if (slot < 0 || slot >= 8) abort();
    if (sample < 0) return;
    log->hist_score_bank[slot] += sample ? 1.0f : 0.0f;
    log->hist_n_bank[slot] += 1.0f;
}

static float col_log_dpt_rate(const Log* log, ColoLogDptSlot slot) {
    if (slot < 0 || slot >= 8) abort();
    float n = log->hist_n_bank[slot];
    return n > 0.0f ? log->hist_score_bank[slot] / n : 0.0f;
}


void c_reset(Env* env) {
    ENCOUNTER_COLOSSEUM.reset(COLO_ENV_STATE(env), COLO_ENV_CONTEXT(env), 0);
    float* obs = (float*)env->observations;
    ENCOUNTER_COLOSSEUM.write_obs(COLO_ENV_STATE(env), COLO_ENV_CONTEXT(env), obs);
    ENCOUNTER_COLOSSEUM.write_mask(COLO_ENV_STATE(env), COLO_ENV_CONTEXT(env), obs + COLO_NUM_OBS);
}

void c_step(Env* env) {
    int col_prof_enabled = COLO_PROFILE_ENABLED();
    double col_prof_total_t0 = col_prof_enabled ? COLO_PROFILE_NOW_MS() : 0.0;
    double col_prof_t0 = col_prof_total_t0;
    RenderClient* render_client = (RenderClient*)env->render_env.client;
    int used_human_commands = 0;
    if (render_client && render_client->human_input.enabled &&
            ENCOUNTER_COLOSSEUM.step_human_commands) {
        ENCOUNTER_COLOSSEUM.step_human_commands(
            COLO_ENV_STATE(env), COLO_ENV_CONTEXT(env), &render_client->human_input);
        used_human_commands = 1;
    } else {
        for (int i = 0; i < NUM_ATNS; i++)
            env->acts_staging[i] = (int)env->actions[i];
    }

    COLO_PROFILE_MARK(COLO_PROF_C_ACTIONS);

    if (!used_human_commands)
        ENCOUNTER_COLOSSEUM.step(COLO_ENV_STATE(env), COLO_ENV_CONTEXT(env), env->acts_staging);
    COLO_PROFILE_MARK(COLO_PROF_C_ENCOUNTER_STEP);

    float* obs = (float*)env->observations;
    ENCOUNTER_COLOSSEUM.write_obs(COLO_ENV_STATE(env), COLO_ENV_CONTEXT(env), obs);
    COLO_PROFILE_MARK(COLO_PROF_C_WRITE_OBS);
    ENCOUNTER_COLOSSEUM.write_mask(COLO_ENV_STATE(env), COLO_ENV_CONTEXT(env), obs + COLO_NUM_OBS);
    COLO_PROFILE_MARK(COLO_PROF_C_WRITE_MASK);

    env->rewards[0] = ENCOUNTER_COLOSSEUM.get_reward(COLO_ENV_STATE(env), COLO_ENV_CONTEXT(env));
    int is_term = ENCOUNTER_COLOSSEUM.is_terminal(COLO_ENV_STATE(env), COLO_ENV_CONTEXT(env));
    int is_trunc = is_term && env->state.time_limit_truncated;
    env->term_staging = (unsigned char)is_term;
    env->terminals[0] = (float)(is_term && !is_trunc);
    env->truncations[0] = (float)is_trunc;
    COLO_PROFILE_MARK(COLO_PROF_C_REWARD_TERMINAL);

    if (env->state.start_wave == env->config_start_wave) {
        col_log_dpt_rate_sample(
            &env->log,
            COLO_LOG_CURRENT_SET_ARGMAX_DPT_SLOT,
            col_current_set_is_argmax_dpt_for_target(&env->state));
        col_log_dpt_rate_sample(
            &env->log,
            COLO_LOG_ATTACKED_ARGMAX_SET_SLOT,
            col_attacked_with_argmax_set(&env->state));
    }

    if (is_term) {
        ColosseumState* s = &env->state;
        ColosseumLog* clog = (ColosseumLog*)ENCOUNTER_COLOSSEUM.get_log(
            COLO_ENV_STATE(env), COLO_ENV_CONTEXT(env));
        if (s->start_wave == env->config_start_wave) {
            env->log.n += 1.0f;
            env->log.episode_return += clog->episode_return;
            env->log.episode_length += (float)clog->episode_length;
            env->log.wins += (float)clog->win;
            env->log.wave += (float)clog->wave_reached;
            env->log.damage_dealt += clog->total_damage_dealt;
            env->log.damage_received += clog->total_damage_received;
            env->log.npc_kills += (float)clog->total_npc_kills;
            env->log.colo_outcome_score += clog->outcome_score;
            env->log.colo_min_sol_hp += (float)s->min_sol_hp_seen;
            env->log.prayer_correct += (float)clog->total_prayer_correct;
            env->log.prayer_total += (float)clog->total_npc_attacks;
            for (int t = 0; t < COLO_NUM_NPC_TYPES; t++) {
                env->log.colo_pray_faced_by_type[t] += clog->pray_faced_by_type[t];
                env->log.colo_pray_correct_by_type[t] += clog->pray_correct_by_type[t];
                env->log.colo_offpray_damage_by_type[t] += clog->offpray_damage_by_type[t];
                env->log.colo_total_damage_by_type[t] += clog->total_damage_by_type[t];
                env->log.colo_death_by_type[t] += clog->death_by_type[t];
            }
            env->log.colo_death_fatal_damage += clog->death_fatal_damage;
            env->log.colo_offpray_damage_conflict += clog->offpray_damage_conflict;
            env->log.colo_offpray_damage_solo += clog->offpray_damage_solo;
            env->log.colo_death_on_conflict_tick += clog->death_on_conflict_tick;
        }
        COLO_PROFILE_MARK(COLO_PROF_C_TERMINAL_LOG);
        ENCOUNTER_COLOSSEUM.reset(COLO_ENV_STATE(env), COLO_ENV_CONTEXT(env), 0);
        ENCOUNTER_COLOSSEUM.write_obs(COLO_ENV_STATE(env), COLO_ENV_CONTEXT(env), obs);
        ENCOUNTER_COLOSSEUM.write_mask(
            COLO_ENV_STATE(env), COLO_ENV_CONTEXT(env), obs + COLO_NUM_OBS);
        env->pending_render_reset = 1;
        COLO_PROFILE_MARK(COLO_PROF_C_RESET);
    }
    if (col_prof_enabled)
        COLO_PROFILE_ADD(COLO_PROF_C_STEP_TOTAL, COLO_PROFILE_NOW_MS() - col_prof_total_t0);
}

void c_close(Env* env) {
    ENCOUNTER_COLOSSEUM.destroy_context(COLO_ENV_CONTEXT(env));
    if (env->render_env.client) {
        render_destroy_client((RenderClient*)env->render_env.client);
        env->render_env.client = NULL;
    }
}

void c_render(Env* env) {
    OsrsEnv* re = &env->render_env;
    re->encounter_def = (void*)&ENCOUNTER_COLOSSEUM;
    re->encounter_state = COLO_ENV_STATE(env);
    re->encounter_context = COLO_ENV_CONTEXT(env);
    re->tick = ENCOUNTER_COLOSSEUM.get_tick(COLO_ENV_STATE(env), COLO_ENV_CONTEXT(env));

    int first_call = (re->client == NULL);
    if (first_call) {
        re->client = render_make_client();
        RenderClient* rc = (RenderClient*)re->client;
        rc->ticks_per_second = env->ticks_per_second;
        /* Fortis Colosseum stadium scene, mirroring the standalone osrs_visual
           setup: colosseum terrain/objects/NPC models at the world SW anchor
           (1808, 3090). The old inferno scene here was a scaffold copy that
           never rendered colosseum-only NPCs (e.g. the Fremennik warband), so
           the eval-render path aborted on the first missing model. */
        EncounterSceneConfig scene = {
            .required_groups = {
                OSRS_ASSET_GROUP_COLOSSEUM,
                OSRS_ASSET_GROUP_COMBAT_VISUALS,
                (OsrsAssetGroupKind)-1,
                (OsrsAssetGroupKind)-1,
            },
            .terrain_path = OSRS_ASSET("colosseum.terrain"),
            .objects_path = OSRS_ASSET("colosseum.objects"),
            .objects_secondary_path = NULL,
            .cmap_path = OSRS_ASSET("colosseum.cmap"),
            .npc_models_path = OSRS_ASSET("colosseum_npcs.models"),
            .npc_anims_path = OSRS_ASSET("colosseum_npcs.anims"),
            .world_origin_x = 1808,
            .world_origin_y = 3090,
        };
        re->collision_map = encounter_load_scene_assets(rc, &scene);
        ENCOUNTER_COLOSSEUM.put_ptr(
            COLO_ENV_STATE(env), COLO_ENV_CONTEXT(env), "collision_map", re->collision_map);
        ENCOUNTER_COLOSSEUM.put_int(
            COLO_ENV_STATE(env), COLO_ENV_CONTEXT(env), "world_offset_x", 1808);
        ENCOUNTER_COLOSSEUM.put_int(
            COLO_ENV_STATE(env), COLO_ENV_CONTEXT(env), "world_offset_y", 3090);
        render_populate_entities(rc, re);
        rc->cam_target_x = (float)rc->arena_base_x + (float)rc->arena_width / 2.0f;
        rc->cam_target_z = -((float)rc->arena_base_y + (float)rc->arena_height / 2.0f);
        for (int i = 0; i < rc->entity_count; i++) {
            int size = rc->entities[i].npc_size > 1 ? rc->entities[i].npc_size : 1;
            rc->sub_x[i] = rc->entities[i].x * 128 + size * 64;
            rc->sub_y[i] = rc->entities[i].y * 128 + size * 64;
            rc->dest_x[i] = rc->sub_x[i];
            rc->dest_y[i] = rc->sub_y[i];
        }
        env->last_step_time = GetTime();
    }
    RenderClient* rc = (RenderClient*)re->client;
    if (!rc) return;

    if (env->pending_render_reset) {
        render_reset_episode_visual_state(rc, re);
        env->pending_render_reset = 0;
    }

    render_post_tick(rc, re);

    if (rc->ticks_per_second <= 0.0f) {
        pvp_render(re);
        env->last_step_time = GetTime();
        return;
    }
    float tps = render_effective_ticks_per_second(rc);
    double interval = 1.0 / (double)tps;
    double deadline = env->last_step_time + interval;
    int rendered = 0;
    while (GetTime() < deadline) {
        pvp_render(re);
        rendered = 1;
    }
    if (!rendered) pvp_render(re);
    env->last_step_time = GetTime();
}

typedef ColosseumState State;

static inline void puffer_state_refresh(Env* env) {
    col_refresh_after_state_load(&env->state, &env->context);
}

#define MY_VEC_INIT
#include "vecenv.h"


void my_init(Env* env, Dict* kwargs) {
    env->num_agents = 1;
    /* render at the real OSRS tick rate (600ms/tick) by default, like inferno;
       0 would render uncapped (every eval step) and look fast-forwarded. */
    env->ticks_per_second = 1.667f;
    env->last_step_time = 0.0;
    ENCOUNTER_COLOSSEUM.init_context(COLO_ENV_CONTEXT(env));
    ENCOUNTER_COLOSSEUM.init_state(COLO_ENV_STATE(env), COLO_ENV_CONTEXT(env));
    memset(&env->log, 0, sizeof(Log));

    DictItem* start_wave = dict_get_unsafe(kwargs, "start_wave");
    if (start_wave) {
        ENCOUNTER_COLOSSEUM.put_int(
            COLO_ENV_STATE(env), COLO_ENV_CONTEXT(env), "start_wave", (int)start_wave->value);
        env->config_start_wave = (int)start_wave->value - 1;
    }

    static const char* const optional_float_keys[] = {
        "damage_reward_coeff",
        "boss_damage_reward_coeff",
        "wave_clear_bonus",
        "boss_phase_bonus",
        "win_bonus",
        "prayer_correct_reward",
        "offpray_damage_penalty_coeff",
        "avoided_damage_coeff",
        "death_penalty_coeff",
        "timeout_penalty",
        "beginner_loadout_fraction",
    };
    for (size_t k = 0; k < sizeof(optional_float_keys) / sizeof(*optional_float_keys); k++) {
        DictItem* item = dict_get_unsafe(kwargs, optional_float_keys[k]);
        if (item)
            ENCOUNTER_COLOSSEUM.put_float(
                COLO_ENV_STATE(env), COLO_ENV_CONTEXT(env),
                optional_float_keys[k], (float)item->value);
    }

    static const char* const optional_int_keys[] = {
        "loadout_profile_mode",
        "step_out_forecast_obs_enabled",
        "forecast_horizon",
        "forecast_run_tile_mode",
        "mask_inventory_heads",
        "action_debug_log",
        "prayer_oracle_mode",
        "bis_gear_oracle_mode",
        "invuln_mode",
        "episode_max_ticks_override",
        "remove_brews",
    };
    for (size_t k = 0; k < sizeof(optional_int_keys) / sizeof(*optional_int_keys); k++) {
        DictItem* item = dict_get_unsafe(kwargs, optional_int_keys[k]);
        if (item)
            ENCOUNTER_COLOSSEUM.put_int(
                COLO_ENV_STATE(env), COLO_ENV_CONTEXT(env),
                optional_int_keys[k], (int)item->value);
    }
}

Env* my_vec_init(int* num_envs_out, int* buffer_env_starts, int* buffer_env_counts,
                 Dict* vec_kwargs, Dict* env_kwargs) {
    int total_agents = (int)dict_get(vec_kwargs, "total_agents")->value;
    int num_buffers = (int)dict_get(vec_kwargs, "num_buffers")->value;
    int agents_per_buffer = total_agents / num_buffers;
    DictItem* base_start_wave_item = dict_get_unsafe(env_kwargs, "start_wave");
    int base_start_wave = base_start_wave_item ? (int)base_start_wave_item->value : 0;
    DictItem* classic_curriculum_mode_item =
        dict_get_unsafe(env_kwargs, "classic_curriculum_mode");
    int classic_curriculum_mode = classic_curriculum_mode_item
        ? (int)classic_curriculum_mode_item->value : 1;
    if (classic_curriculum_mode < 0 || classic_curriculum_mode > 1) {
        fprintf(stderr, "classic_curriculum_mode must be 0 or 1, got %d\n",
            classic_curriculum_mode);
        abort();
    }
    DictItem* curriculum_num_tiers_item =
        dict_get_unsafe(env_kwargs, "curriculum_num_tiers");
    int curriculum_num_tiers = curriculum_num_tiers_item
        ? (int)curriculum_num_tiers_item->value : MAX_CURRICULUM_TIERS;
    if (curriculum_num_tiers < 0 || curriculum_num_tiers > MAX_CURRICULUM_TIERS) {
        fprintf(stderr, "curriculum_num_tiers must be between 0 and %d, got %d\n",
            MAX_CURRICULUM_TIERS, curriculum_num_tiers);
        abort();
    }

    static const char* wave_keys[] = {
        "curriculum_wave_1", "curriculum_wave_2", "curriculum_wave_3", "curriculum_wave_4",
        "curriculum_wave_5", "curriculum_wave_6", "curriculum_wave_7", "curriculum_wave_8",
    };
    static const char* frac_keys[] = {
        "curriculum_frac_1", "curriculum_frac_2", "curriculum_frac_3", "curriculum_frac_4",
        "curriculum_frac_5", "curriculum_frac_6", "curriculum_frac_7", "curriculum_frac_8",
    };
    int curriculum_waves[MAX_CURRICULUM_TIERS];
    float curriculum_fracs[MAX_CURRICULUM_TIERS];
    int num_tiers = 0;
    if (classic_curriculum_mode == 1) {
        for (int i = 0; i < curriculum_num_tiers; i++) {
            DictItem* w = dict_get_unsafe(env_kwargs, wave_keys[i]);
            DictItem* f = dict_get_unsafe(env_kwargs, frac_keys[i]);
            if (w && f && f->value > 0.0) {
                curriculum_waves[num_tiers] = (int)w->value;
                curriculum_fracs[num_tiers] = (float)f->value;
                num_tiers++;
            }
        }
    }

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

    if (num_tiers > 0) {
        int tier_counts[MAX_CURRICULUM_TIERS];
        int curriculum_total = 0;
        for (int t = 0; t < num_tiers; t++) {
            tier_counts[t] = (int)(curriculum_fracs[t] * num_envs);
            if (tier_counts[t] < 1) tier_counts[t] = 1;
            curriculum_total += tier_counts[t];
        }
        while (curriculum_total > num_envs) {
            for (int t = num_tiers - 1; t >= 0 && curriculum_total > num_envs; t--) {
                if (tier_counts[t] > 0) {
                    tier_counts[t]--;
                    curriculum_total--;
                }
            }
        }
        int base_count = num_envs - curriculum_total;
        int cursor = base_count;
        for (int t = 0; t < num_tiers; t++) {
            for (int i = 0; i < tier_counts[t] && cursor < num_envs; i++, cursor++) {
                ENCOUNTER_COLOSSEUM.put_int(
                    COLO_ENV_STATE(&envs[cursor]), COLO_ENV_CONTEXT(&envs[cursor]),
                    "start_wave", curriculum_waves[t]);
                ENCOUNTER_COLOSSEUM.put_int(
                    COLO_ENV_STATE(&envs[cursor]), COLO_ENV_CONTEXT(&envs[cursor]),
                    "curriculum_agent", 1);
            }
        }
        fprintf(stderr, "curriculum: %d wave-%d", base_count, base_start_wave);
        for (int t = 0; t < num_tiers; t++)
            fprintf(stderr, ", %d wave-%d", tier_counts[t], curriculum_waves[t]);
        fprintf(stderr, " (%d total)\n", num_envs);
    }

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
    dict_set(out, "episode_length", log->episode_length);
    dict_set(out, "damage_dealt", log->damage_dealt);
    dict_set(out, "damage_received", log->damage_received);
    dict_set(out, "wins", log->wins);
    dict_set(out, "wave", log->wave);
    dict_set(out, "npc_kills", log->npc_kills);

    float prayer_rate = log->prayer_total > 0.0f
        ? log->prayer_correct / log->prayer_total : 0.0f;
    dict_set(out, "prayer_correct_rate", prayer_rate);

    dict_set(out, "score", log->colo_outcome_score);
    dict_set(out, "sol_min_hp", log->colo_min_sol_hp);
    dict_set(out, "current_set_is_argmax_dpt_for_target",
        col_log_dpt_rate(log, COLO_LOG_CURRENT_SET_ARGMAX_DPT_SLOT));
    dict_set(out, "attacked_with_argmax_set",
        col_log_dpt_rate(log, COLO_LOG_ATTACKED_ARGMAX_SET_SLOT));

    /* per-NPC-type prayer outcomes: off-prayer exposure rate, mean off-prayer
       damage, and mean total damage taken per episode. Indexed by ColoNpcType.
       Keys are string literals because dict_set stores the key POINTER, not a
       copy, so formatted stack buffers alias every entry onto one item. */
    static const char* OFFPRAY_RATE_KEYS[COLO_NUM_NPC_TYPES] = {
        "offpray_rate_berserker", "offpray_rate_archer", "offpray_rate_seer",
        "offpray_rate_serpent", "offpray_rate_jaguar", "offpray_rate_javelin",
        "offpray_rate_shockwave", "offpray_rate_minotaur", "offpray_rate_manticore",
        "offpray_rate_sol", "offpray_rate_totem", "offpray_rate_bee"};
    static const char* OFFPRAY_DMG_KEYS[COLO_NUM_NPC_TYPES] = {
        "offpray_dmg_berserker", "offpray_dmg_archer", "offpray_dmg_seer",
        "offpray_dmg_serpent", "offpray_dmg_jaguar", "offpray_dmg_javelin",
        "offpray_dmg_shockwave", "offpray_dmg_minotaur", "offpray_dmg_manticore",
        "offpray_dmg_sol", "offpray_dmg_totem", "offpray_dmg_bee"};
    static const char* TOTAL_DMG_KEYS[COLO_NUM_NPC_TYPES] = {
        "total_dmg_berserker", "total_dmg_archer", "total_dmg_seer",
        "total_dmg_serpent", "total_dmg_jaguar", "total_dmg_javelin",
        "total_dmg_shockwave", "total_dmg_minotaur", "total_dmg_manticore",
        "total_dmg_sol", "total_dmg_totem", "total_dmg_bee"};
    for (int t = 0; t < COLO_NUM_NPC_TYPES; t++) {
        float faced = log->colo_pray_faced_by_type[t];
        float off_rate = faced > 0.0f
            ? (faced - log->colo_pray_correct_by_type[t]) / faced : 0.0f;
        dict_set(out, OFFPRAY_RATE_KEYS[t], off_rate);
        dict_set(out, OFFPRAY_DMG_KEYS[t], log->colo_offpray_damage_by_type[t]);
        dict_set(out, TOTAL_DMG_KEYS[t], log->colo_total_damage_by_type[t]);
    }

    /* death attribution (diagnostic): kill-share per NPC type (which landed the
       killing blow) + mean fatal-tick damage. Same literal-key rule as above. */
    static const char* DEATH_BY_KEYS[COLO_NUM_NPC_TYPES] = {
        "death_by_berserker", "death_by_archer", "death_by_seer",
        "death_by_serpent", "death_by_jaguar", "death_by_javelin",
        "death_by_shockwave", "death_by_minotaur", "death_by_manticore",
        "death_by_sol", "death_by_totem", "death_by_bee"};
    for (int t = 0; t < COLO_NUM_NPC_TYPES; t++)
        dict_set(out, DEATH_BY_KEYS[t], log->colo_death_by_type[t]);
    dict_set(out, "death_fatal_damage", log->colo_death_fatal_damage);
    dict_set(out, "offpray_dmg_conflict", log->colo_offpray_damage_conflict);
    dict_set(out, "offpray_dmg_solo", log->colo_offpray_damage_solo);
    dict_set(out, "death_on_conflict_tick", log->colo_death_on_conflict_tick);
}
