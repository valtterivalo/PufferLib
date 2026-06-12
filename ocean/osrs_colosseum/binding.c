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

#define MAX_CURRICULUM_TIERS 8


void c_reset(Env* env) {
    ENCOUNTER_COLOSSEUM.reset(COLO_ENV_STATE(env), COLO_ENV_CONTEXT(env), 0);
    float* obs = (float*)env->observations;
    ENCOUNTER_COLOSSEUM.write_obs(COLO_ENV_STATE(env), COLO_ENV_CONTEXT(env), obs);
    ENCOUNTER_COLOSSEUM.write_mask(COLO_ENV_STATE(env), COLO_ENV_CONTEXT(env), obs + COLO_NUM_OBS);
}

void c_step(Env* env) {
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

    if (!used_human_commands)
        ENCOUNTER_COLOSSEUM.step(COLO_ENV_STATE(env), COLO_ENV_CONTEXT(env), env->acts_staging);

    float* obs = (float*)env->observations;
    ENCOUNTER_COLOSSEUM.write_obs(COLO_ENV_STATE(env), COLO_ENV_CONTEXT(env), obs);
    ENCOUNTER_COLOSSEUM.write_mask(COLO_ENV_STATE(env), COLO_ENV_CONTEXT(env), obs + COLO_NUM_OBS);

    env->rewards[0] = ENCOUNTER_COLOSSEUM.get_reward(COLO_ENV_STATE(env), COLO_ENV_CONTEXT(env));
    int is_term = ENCOUNTER_COLOSSEUM.is_terminal(COLO_ENV_STATE(env), COLO_ENV_CONTEXT(env));
    env->term_staging = (unsigned char)is_term;
    env->terminals[0] = (float)is_term;

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
            env->log.prayer_correct += (float)clog->total_prayer_correct;
            env->log.prayer_total += (float)clog->total_npc_attacks;
        }
        ENCOUNTER_COLOSSEUM.reset(COLO_ENV_STATE(env), COLO_ENV_CONTEXT(env), 0);
        ENCOUNTER_COLOSSEUM.write_obs(COLO_ENV_STATE(env), COLO_ENV_CONTEXT(env), obs);
        ENCOUNTER_COLOSSEUM.write_mask(
            COLO_ENV_STATE(env), COLO_ENV_CONTEXT(env), obs + COLO_NUM_OBS);
        env->pending_render_reset = 1;
    }
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
        EncounterSceneConfig scene = {
            .required_groups = {
                OSRS_ASSET_GROUP_INFERNO,
                OSRS_ASSET_GROUP_COMBAT_VISUALS,
                (OsrsAssetGroupKind)-1,
                (OsrsAssetGroupKind)-1,
            },
            .terrain_path = OSRS_ASSET("inferno.terrain"),
            .objects_path = OSRS_ASSET("inferno.objects"),
            .objects_secondary_path = NULL,
            .npc_models_path = OSRS_ASSET("inferno.models"),
            .npc_anims_path = OSRS_ASSET("inferno.anims"),
            .world_origin_x = 2246,
            .world_origin_y = 5315,
        };
        encounter_load_scene_assets(rc, &scene);
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
        "death_penalty_coeff",
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
        "terminal_penalty_enabled",
        "loadout_profile_mode",
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
    for (int i = 0; i < MAX_CURRICULUM_TIERS; i++) {
        DictItem* w = dict_get_unsafe(env_kwargs, wave_keys[i]);
        DictItem* f = dict_get_unsafe(env_kwargs, frac_keys[i]);
        if (w && f && f->value > 0.0) {
            curriculum_waves[num_tiers] = (int)w->value;
            curriculum_fracs[num_tiers] = (float)f->value;
            num_tiers++;
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

    /* score: win-rate plus partial credit for wave progress on losses. */
    float wr = log->wins;
    float wave_frac = log->wave / (float)COLO_NUM_WAVES;
    float score = wr + (1.0f - wr) * wave_frac * 0.5f;
    dict_set(out, "score", score);
}
