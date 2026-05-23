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

#include "osrs_env.h"  /* pulls in osrs_types, encounter, pvp stack */
#include "osrs_assets.h"

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#include "encounters/encounter_zulrah.h"
#include "encounters/encounter_inferno.h"  /* render.h references InfernoState */
#include "osrs_render.h"
#include "osrs_scene_assets.h"
#pragma GCC diagnostic pop

#define ZUL_TOTAL_OBS (ZUL_NUM_OBS + ZUL_ACTION_MASK_SIZE)

/* vecenv-compatible header fields must stay first. */
typedef struct {
    void* observations;
    float* actions;
    float* rewards;
    float* terminals;
    int num_agents;
    int rng;
    Log log;

    EncounterState* enc_state;
    EncounterContext* enc_context;

    int acts_staging[ZUL_NUM_ACTION_HEADS];
    unsigned char term_staging;

    OsrsEnv render_env;
    int pending_render_reset;
    double last_step_time;
} ZulrahEnv;

#define OBS_SIZE ZUL_TOTAL_OBS
#define NUM_ATNS ZUL_NUM_ACTION_HEADS
#define ACT_SIZES {ZUL_MOVE_DIM, ZUL_ATTACK_DIM, ZUL_PRAYER_DIM, ZUL_FOOD_DIM, ZUL_POTION_DIM, ZUL_SPEC_DIM, ZUL_OFFENSIVE_DIM}
#define OBS_TENSOR_T FloatTensor
#define Env ZulrahEnv
#define ZUL_ENV_CONTEXT(env) ((EncounterContext*)((env)->enc_context))

static uint32_t zulrah_env_next_seed(Env* env) {
    uint32_t next = (uint32_t)env->rng + 0x9e3779b9u;
    if (next == 0) next = 1;
    env->rng = (int)next;
    return next;
}

static void zulrah_env_put_int(Env* env, const char* key, int value) {
    ENCOUNTER_ZULRAH.put_int(env->enc_state, ZUL_ENV_CONTEXT(env), key, value);
}

static void zulrah_env_put_float(Env* env, const char* key, float value) {
    ENCOUNTER_ZULRAH.put_float(env->enc_state, ZUL_ENV_CONTEXT(env), key, value);
}

void c_step(Env* env) {
    int used_human_commands = 0;
    RenderClient* render_client = (RenderClient*)env->render_env.client;

    if (render_client && render_client->human_input.enabled &&
        ENCOUNTER_ZULRAH.step_human_commands) {
        const char* record_path = getenv("RECORD_REPLAY");
        if (record_path && record_path[0] && render_client->human_input.commands.count > 0) {
            fprintf(stderr, "RECORD_REPLAY cannot record human command mode\n");
            abort();
        }
        ENCOUNTER_ZULRAH.step_human_commands(
            env->enc_state, ZUL_ENV_CONTEXT(env), &render_client->human_input);
        used_human_commands = 1;
    } else {
        if (render_client) {
            human_input_clear_pending(&render_client->human_input);
            human_input_clear_move(&render_client->human_input);
            ENCOUNTER_ZULRAH.put_int(env->enc_state, ZUL_ENV_CONTEXT(env), "player_dest_x", -1);
            ENCOUNTER_ZULRAH.put_int(env->enc_state, ZUL_ENV_CONTEXT(env), "player_dest_y", -1);
            ENCOUNTER_ZULRAH.put_int(
                env->enc_state, ZUL_ENV_CONTEXT(env), "human_command_mode", 0);
        }
        for (int i = 0; i < NUM_ATNS; i++) {
            env->acts_staging[i] = (int)env->actions[i];
        }
    }

    if (!used_human_commands)
        ENCOUNTER_ZULRAH.step(env->enc_state, ZUL_ENV_CONTEXT(env), env->acts_staging);

    float* obs = (float*)env->observations;
    ENCOUNTER_ZULRAH.write_obs(env->enc_state, ZUL_ENV_CONTEXT(env), obs);
    ENCOUNTER_ZULRAH.write_mask(env->enc_state, ZUL_ENV_CONTEXT(env), obs + ZUL_NUM_OBS);

    env->rewards[0] = ENCOUNTER_ZULRAH.get_reward(env->enc_state, ZUL_ENV_CONTEXT(env));

    int is_term = ENCOUNTER_ZULRAH.is_terminal(env->enc_state, ZUL_ENV_CONTEXT(env));
    env->term_staging = (unsigned char)is_term;
    env->terminals[0] = (float)is_term;

    /* log directly into env->log (vecenv accumulates + clears periodically).
       bypass get_log vtable to avoid double-counting from encounter's own += */
    if (is_term) {
        ZulrahState* zs = (ZulrahState*)env->enc_state;
        env->log.episode_return += zs->episode_return;
        env->log.episode_length += (float)zs->tick;
        float kills = (float)zs->kills_this_episode;
        float win = (zs->episode_mode == ZUL_EPISODE_TRIP)
            ? kills
            : ((zs->winner == 0) ? 1.0f : 0.0f);
        float partial = (zs->episode_mode == ZUL_EPISODE_TRIP)
            ? zul_current_kill_progress(zs)
            : 0.0f;
        float speed_bonus = (zs->episode_mode == ZUL_EPISODE_TRIP)
            ? zs->score_speed_bonus_sum
            : (win > 0.0f
                ? (1.0f - (float)zs->tick / (float)ZUL_MAX_TICKS) *
                    ZUL_SCORE_SPEED_BONUS_DEFAULT
                : 0.0f);
        env->log.wins += win;
        env->log.zulrah_kills += kills;
        env->log.damage_dealt += zs->total_damage_dealt;
        env->log.damage_received += zs->total_damage_received;
        env->log.prayer_correct += (float)zs->total_prayer_correct;
        env->log.prayer_total += (float)zs->total_prayer_total;
        env->log.gear_switches += (float)zs->total_gear_switches;
        env->log.npc_kills += (float)zs->total_food_eaten;
        env->log.idle_ticks += (float)zs->total_potions_used;
        env->log.brews_used += (float)zs->total_venom_ticks;
        env->log.wave += (float)zs->total_phases_completed;
        env->log.cloud_occupancy_ticks += (float)zs->total_cloud_occupancy_ticks;
        env->log.cloud_occupancy_frac += zs->tick > 0
            ? (float)zs->total_cloud_occupancy_ticks / (float)zs->tick
            : 0.0f;
        env->log.cloud_damage_received += zs->total_cloud_damage_received;
        env->log.active_cloud_count_ticks += (float)zs->total_active_cloud_ticks;
        env->log.pending_cloud_count_ticks += (float)zs->total_pending_cloud_ticks;
        int tier = zs->gear_tier;
        if (tier < 0 || tier >= ZUL_NUM_GEAR_TIERS) {
            fprintf(stderr, "zulrah invalid sampled gear tier %d\n", tier);
            abort();
        }
        env->log.zulrah_tier_n[tier] += 1.0f;
        env->log.zulrah_tier_wins[tier] += win;
        env->log.zulrah_tier_score_sum[tier] += win + partial + speed_bonus;
        env->log.zulrah_tier_damage_received[tier] += zs->total_damage_received;
        env->log.zulrah_tier_episode_length[tier] += (float)zs->tick;
        env->log.zulrah_tier_cloud_occupancy_ticks[tier] +=
            (float)zs->total_cloud_occupancy_ticks;
        env->log.zulrah_tier_cloud_damage_received[tier] +=
            zs->total_cloud_damage_received;
        env->log.n += 1.0f;

        env->pending_render_reset = 1;
        ENCOUNTER_ZULRAH.reset(
            env->enc_state, ZUL_ENV_CONTEXT(env), zulrah_env_next_seed(env));
        ENCOUNTER_ZULRAH.write_obs(env->enc_state, ZUL_ENV_CONTEXT(env), obs);
        ENCOUNTER_ZULRAH.write_mask(env->enc_state, ZUL_ENV_CONTEXT(env), obs + ZUL_NUM_OBS);
    }
}

void c_reset(Env* env) {
    ENCOUNTER_ZULRAH.reset(
        env->enc_state, ZUL_ENV_CONTEXT(env), zulrah_env_next_seed(env));

    float* obs = (float*)env->observations;
    ENCOUNTER_ZULRAH.write_obs(env->enc_state, ZUL_ENV_CONTEXT(env), obs);
    ENCOUNTER_ZULRAH.write_mask(env->enc_state, ZUL_ENV_CONTEXT(env), obs + ZUL_NUM_OBS);

    env->rewards[0] = 0.0f;
    env->term_staging = 0;
    env->terminals[0] = 0.0f;
}

void c_close(Env* env) {
    if (env->enc_state) {
        ENCOUNTER_ZULRAH.destroy(env->enc_state);
        env->enc_state = NULL;
    }
    if (env->enc_context) {
        if (ENCOUNTER_ZULRAH.destroy_context)
            ENCOUNTER_ZULRAH.destroy_context(ZUL_ENV_CONTEXT(env));
        free(env->enc_context);
        env->enc_context = NULL;
    }
    if (env->render_env.client) {
        render_destroy_client((RenderClient*)env->render_env.client);
        env->render_env.client = NULL;
    }
    if (env->render_env.collision_map) {
        collision_map_free((CollisionMap*)env->render_env.collision_map);
        env->render_env.collision_map = NULL;
    }
}

void c_render(Env* env) {
    OsrsEnv* re = &env->render_env;
    re->encounter_def = (void*)&ENCOUNTER_ZULRAH;
    re->encounter_state = env->enc_state;
    re->encounter_context = env->enc_context;
    re->tick = ENCOUNTER_ZULRAH.get_tick(env->enc_state, ZUL_ENV_CONTEXT(env));

    int first_call = (re->client == NULL);
    if (first_call) {
        re->client = render_make_client();
        RenderClient* rc = (RenderClient*)re->client;
        EncounterSceneConfig scene = {
            .required_groups = {
                OSRS_ASSET_GROUP_ZULRAH, OSRS_ASSET_GROUP_COMBAT_VISUALS, -1, -1,
            },
            .terrain_path = OSRS_ASSET("zulrah.terrain"),
            .objects_path = OSRS_ASSET("zulrah.objects"),
            .cmap_path = OSRS_ASSET("zulrah.cmap"),
            .npc_models_path = OSRS_ASSET("zulrah.models"),
            .npc_anims_path = OSRS_ASSET("zulrah.anims"),
            /* zulrah regions (35,47)+(35,48) start at world (2240, 3008);
               island platform at world ~(2256, 3061). */
            .world_origin_x = 2256,
            .world_origin_y = 3061,
        };
        CollisionMap* cmap = encounter_load_scene_assets(rc, &scene);
        if (!cmap) {
            fprintf(stderr, "zulrah eval render failed to load collision map\n");
            abort();
        }
        ENCOUNTER_ZULRAH.put_ptr(env->enc_state, ZUL_ENV_CONTEXT(env), "collision_map", cmap);
        ENCOUNTER_ZULRAH.put_int(env->enc_state, ZUL_ENV_CONTEXT(env), "world_offset_x", 2256);
        ENCOUNTER_ZULRAH.put_int(env->enc_state, ZUL_ENV_CONTEXT(env), "world_offset_y", 3061);
        re->collision_map = cmap;

        render_populate_entities(rc, re);
        rc->cam_target_x = (float)rc->arena_base_x + (float)rc->arena_width / 2.0f;
        rc->cam_target_z = -((float)rc->arena_base_y + (float)rc->arena_height / 2.0f);
        for (int i = 0; i < rc->entity_count; i++)
            render_seed_entity_visual_slot(rc, i);
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
        rc->last_tick_time = GetTime();
        env->last_step_time = rc->last_tick_time;
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

    rc->last_tick_time = GetTime();
    env->last_step_time = rc->last_tick_time;
}

#include "vecenv.h"

void my_init(Env* env, Dict* kwargs) {
    env->num_agents = 1;
    env->enc_context = (EncounterContext*)calloc(1, ENCOUNTER_ZULRAH.context_size);
    if (!env->enc_context) abort();
    if (ENCOUNTER_ZULRAH.init_context)
        ENCOUNTER_ZULRAH.init_context(ZUL_ENV_CONTEXT(env));
    env->enc_state = ENCOUNTER_ZULRAH.create();
    memset(&env->log, 0, sizeof(Log));

    DictItem* gear = dict_get_unsafe(kwargs, "gear_tier");
    if (gear) zulrah_env_put_int(env, "gear_tier", (int)gear->value);

    DictItem* gear_mode = dict_get_unsafe(kwargs, "gear_tier_mode");
    if (gear_mode) zulrah_env_put_int(env, "gear_tier_mode", (int)gear_mode->value);

    DictItem* episode_mode = dict_get_unsafe(kwargs, "episode_mode");
    if (episode_mode) zulrah_env_put_int(env, "episode_mode", (int)episode_mode->value);

    const char* weight_keys[ZUL_NUM_GEAR_TIERS] = {
        "gear_tier_weight_0",
        "gear_tier_weight_1",
        "gear_tier_weight_2",
    };
    for (int i = 0; i < ZUL_NUM_GEAR_TIERS; i++) {
        DictItem* item = dict_get_unsafe(kwargs, weight_keys[i]);
        if (item) zulrah_env_put_float(env, weight_keys[i], (float)item->value);
    }

    const char* reward_keys[] = {
        "reward_win",
        "reward_loss_penalty",
        "reward_damage_dealt",
        "reward_correct_style",
        "reward_damage_received_penalty",
        "reward_cloud_occupancy_penalty",
    };
    for (int i = 0; i < (int)(sizeof(reward_keys) / sizeof(reward_keys[0])); i++) {
        DictItem* item = dict_get_unsafe(kwargs, reward_keys[i]);
        if (item) zulrah_env_put_float(env, reward_keys[i], (float)item->value);
    }
}

void my_log(Log* log, Dict* out) {
    dict_set(out, "episode_return", log->episode_return);
    dict_set(out, "episode_length", log->episode_length);
    dict_set(out, "wins", log->wins);
    dict_set(out, "damage_dealt", log->damage_dealt);
    dict_set(out, "damage_received", log->damage_received);

    float prayer_rate = (log->prayer_total > 0.0f)
        ? log->prayer_correct / log->prayer_total : 0.0f;
    dict_set(out, "prayer_correct_rate", prayer_rate);

    /* behavioral metrics (stored in reused Log fields) */
    dict_set(out, "gear_switches", log->gear_switches);
    dict_set(out, "food_eaten", log->npc_kills);      /* reused field */
    dict_set(out, "potions_used", log->idle_ticks);    /* reused field */
    dict_set(out, "venom_ticks", log->brews_used);     /* reused field */
    dict_set(out, "phases_completed", log->wave);      /* reused field */
    dict_set(out, "cloud_occupancy_ticks", log->cloud_occupancy_ticks);
    dict_set(out, "cloud_occupancy_frac", log->cloud_occupancy_frac);
    dict_set(out, "cloud_damage_received", log->cloud_damage_received);
    dict_set(out, "active_cloud_count_ticks", log->active_cloud_count_ticks);
    dict_set(out, "pending_cloud_count_ticks", log->pending_cloud_count_ticks);
    dict_set(out, "kills", log->zulrah_kills);

    float wr = log->wins;
    float speed_bonus = (wr > 0.1f)
        ? (1.0f - log->episode_length / (float)ZUL_MAX_TICKS) *
            ZUL_SCORE_SPEED_BONUS_DEFAULT
        : 0.0f;
    float score = wr + speed_bonus;
    dict_set(out, "score", score);

    const char* tier_frac_keys[ZUL_NUM_GEAR_TIERS] = {
        "gear_tier_0_frac",
        "gear_tier_1_frac",
        "gear_tier_2_frac",
    };
    const char* tier_win_keys[ZUL_NUM_GEAR_TIERS] = {
        "wins_tier_0",
        "wins_tier_1",
        "wins_tier_2",
    };
    const char* tier_score_keys[ZUL_NUM_GEAR_TIERS] = {
        "score_tier_0",
        "score_tier_1",
        "score_tier_2",
    };
    const char* tier_damage_keys[ZUL_NUM_GEAR_TIERS] = {
        "damage_received_tier_0",
        "damage_received_tier_1",
        "damage_received_tier_2",
    };
    const char* tier_length_keys[ZUL_NUM_GEAR_TIERS] = {
        "episode_length_tier_0",
        "episode_length_tier_1",
        "episode_length_tier_2",
    };
    const char* tier_cloud_keys[ZUL_NUM_GEAR_TIERS] = {
        "cloud_occupancy_ticks_tier_0",
        "cloud_occupancy_ticks_tier_1",
        "cloud_occupancy_ticks_tier_2",
    };
    const char* tier_cloud_damage_keys[ZUL_NUM_GEAR_TIERS] = {
        "cloud_damage_received_tier_0",
        "cloud_damage_received_tier_1",
        "cloud_damage_received_tier_2",
    };

    float tier_scores[ZUL_NUM_GEAR_TIERS];
    for (int i = 0; i < ZUL_NUM_GEAR_TIERS; i++) {
        float n = log->zulrah_tier_n[i];
        float tier_frac = (log->n > 0.0f) ? n / log->n : 0.0f;
        float tier_wins = n > 0.0f ? log->zulrah_tier_wins[i] / n : 0.0f;
        float tier_score = n > 0.0f ? log->zulrah_tier_score_sum[i] / n : 0.0f;
        float tier_damage = n > 0.0f ? log->zulrah_tier_damage_received[i] / n : 0.0f;
        float tier_length = n > 0.0f ? log->zulrah_tier_episode_length[i] / n : 0.0f;
        float tier_cloud = n > 0.0f
            ? log->zulrah_tier_cloud_occupancy_ticks[i] / n : 0.0f;
        float tier_cloud_damage = n > 0.0f
            ? log->zulrah_tier_cloud_damage_received[i] / n : 0.0f;
        tier_scores[i] = tier_score;
        dict_set(out, tier_frac_keys[i], tier_frac);
        dict_set(out, tier_win_keys[i], tier_wins);
        dict_set(out, tier_score_keys[i], tier_score);
        dict_set(out, tier_damage_keys[i], tier_damage);
        dict_set(out, tier_length_keys[i], tier_length);
        dict_set(out, tier_cloud_keys[i], tier_cloud);
        dict_set(out, tier_cloud_damage_keys[i], tier_cloud_damage);
    }

    float score_general = tier_scores[0];
    if (tier_scores[1] < score_general) score_general = tier_scores[1];
    if (tier_scores[2] < score_general) score_general = tier_scores[2];
    float score_low_budget_weighted =
        0.50f * tier_scores[0] + 0.30f * tier_scores[1] + 0.20f * tier_scores[2];
    dict_set(out, "score_general", score_general);
    dict_set(out, "score_low_budget_weighted", score_low_budget_weighted);
}
