/**
 * @file osrs_inferno.h
 * @brief 5c env entry header for the OSRS Inferno encounter.
 *
 * Implements the puf_init/puf_reset/puf_step/puf_render/puf_close/puf_log
 * contract from src/pufferenv.h around the shared inferno encounter stack.
 * Observations, actions, rewards, terminals, and the action mask live in
 * env->agents[0].* (the trainer wires those pointers after puf_init).
 */

#ifndef OSRS_INFERNO_ENV_H
#define OSRS_INFERNO_ENV_H

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "pufferenv.h"
#include "inferno_profile.h"

/* the encounter stack carries static sim/obs helpers not all reachable from one
   translation unit; silence unused-function so -Wall stays clean. */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#include "../osrs/encounters/encounter_inferno.h"
#pragma GCC diagnostic pop

#define OBS_SIZE INF_NUM_OBS
#define NUM_ATNS INF_NUM_ACTION_HEADS
#define ACT_SIZES INF_ACTION_DIMS_INIT
typedef float obs_t;

#define INF_ENV_STATE(env) ((EncounterState*)&((env)->state))
#define INF_ENV_CONTEXT(env) ((EncounterContext*)&((env)->context))
#define INF_ENV_INFERNO(env) (&((env)->state))
#define INF_ENV_INFERNO_CONTEXT(env) (&((env)->context))

/* Required 5c prefix (Log log; num_agents; rng; agents; tag; boundary_reached),
   then the inferno sim state embedded after. */
struct Env {
    Log log;
    int num_agents;
    unsigned int rng;
    Agent agents[1];
    int tag;
    int boundary_reached;

    InfernoState state;
    InfernoContext context;
    int config_start_wave;  /* base start_wave (0-indexed); curriculum agents keep this */
};

/** lowbias32 hash: decorrelate consecutive env indices into independent seeds. */
static inline uint32_t inf_lowbias32(uint32_t x) {
    x ^= x >> 16;
    x *= 0x7feb352dU;
    x ^= x >> 15;
    x *= 0x846ca68bU;
    x ^= x >> 16;
    return x;
}

static void inferno_env_put_float(Env* env, const char* key, float value) {
    ENCOUNTER_INFERNO.put_float(INF_ENV_STATE(env), INF_ENV_CONTEXT(env), key, value);
}

static void inferno_env_put_int(Env* env, const char* key, int value) {
    ENCOUNTER_INFERNO.put_int(INF_ENV_STATE(env), INF_ENV_CONTEXT(env), key, value);
}

static void inferno_apply_obs_profile(Env* env, int obs_profile) {
    switch (obs_profile) {
        case 0:
            inferno_env_put_int(env, "step_out_forecast_obs_mode", 0);
            break;
        case 1:
            inferno_env_put_int(env, "step_out_forecast_obs_mode", 1);
            break;
        default:
            fprintf(stderr, "obs_profile must be 0 or 1, got %d\n", obs_profile);
            abort();
    }
}

static void inferno_zero_phase_reward_coeffs(Env* env) {
    static const char* const keys[] = {
        "jad_damage_reward_coeff",
        "zuk_healer_damage_reward_coeff",
        "set_damage_reward_coeff",
        "jad_kill_bonus",
        "zuk_healer_kill_bonus",
        "set_kill_bonus",
        "post_healer_zuk_damage_coeff",
        "zuk_healer_phase_hp_delta_coeff",
        "post_healer_set_damage_reward_coeff",
        "post_healer_set_kill_bonus",
        "post_healer_set_alive_tick_penalty_coeff",
        "post_healer_set_alive_penalty_cap",
        "zuk_untagged_healer_tick_penalty_coeff",
        "zuk_untagged_healer_target_bonus_coeff",
        "zuk_safe_untagged_healer_target_bonus_coeff",
    };
    for (size_t i = 0; i < sizeof(keys) / sizeof(*keys); i++)
        inferno_env_put_float(env, keys[i], 0.0f);
    inferno_env_put_float(env, "post_jad_zuk_multiplier", 1.0f);
    inferno_env_put_float(env, "jad_alive_zuk_multiplier", 1.0f);
}

static void inferno_apply_reward_profile(Env* env, int reward_profile) {
    switch (reward_profile) {
        case 0:
            inferno_env_put_int(env, "joseph_reward_mode", 0);
            inferno_env_put_float(env, "damage_reward_coeff", 0.0f);
            inferno_env_put_float(env, "shield_penalty_coeff", 0.0f);
            inferno_env_put_float(env, "tag_reward_coeff", 0.0f);
            inferno_env_put_float(env, "shield_tag_reward_coeff", 0.0f);
            inferno_env_put_float(
                env, "zuk_untagged_healer_nonmagic_attack_bonus_coeff", 0.0f);
            inferno_env_put_float(env, "zuk_healer_mage_attack_penalty_coeff", 0.0f);
            inferno_zero_phase_reward_coeffs(env);
            break;
        case 1:
            inferno_env_put_int(env, "joseph_reward_mode", 0);
            break;
        case 2:
            inferno_env_put_int(env, "joseph_reward_mode", 1);
            inferno_env_put_float(env, "shield_tag_reward_coeff", 0.0f);
            inferno_zero_phase_reward_coeffs(env);
            break;
        default:
            fprintf(stderr, "reward_profile must be in [0,2], got %d\n", reward_profile);
            abort();
    }
}

/* Curriculum wave mixing: a fraction of envs start at later waves for late-game
   signal (curriculum_agent=1 excludes them from scored metrics). The 5c per-env
   model has no total-env count in puf_init, so the contiguous-tail assignment of
   the old my_vec_init becomes a per-env draw keyed on env->rng: each env lands in
   base or a tier with probability equal to the configured fraction. Same wave
   distribution, deterministic per index. */
static void inferno_apply_curriculum(Env* env, Dict* kwargs) {
    if ((int)dict_get(kwargs, "classic_curriculum_mode") != 1)
        return;

    static const char* const wave_keys[] = {
        "curriculum_wave_1", "curriculum_wave_2", "curriculum_wave_3", "curriculum_wave_4",
        "curriculum_wave_5", "curriculum_wave_6", "curriculum_wave_7", "curriculum_wave_8",
    };
    static const char* const frac_keys[] = {
        "curriculum_frac_1", "curriculum_frac_2", "curriculum_frac_3", "curriculum_frac_4",
        "curriculum_frac_5", "curriculum_frac_6", "curriculum_frac_7", "curriculum_frac_8",
    };
    int waves[8];
    float fracs[8];
    int num_tiers = 0;
    float total_frac = 0.0f;
    for (int i = 0; i < 8; i++) {
        DictItem* w = dict_find(kwargs, wave_keys[i]);
        float f = (float)dict_get(kwargs, frac_keys[i]);
        if (w && f > 0.0f) {
            waves[num_tiers] = (int)w->value;
            fracs[num_tiers] = f;
            total_frac += f;
            num_tiers++;
        }
    }
    if (num_tiers == 0)
        return;

    float u = (float)inf_lowbias32(inf_lowbias32((uint32_t)env->rng) ^ 0x9e3779b9U)
        / 4294967296.0f;
    float cursor = 1.0f - total_frac;  /* base occupies [0, cursor) */
    if (u < cursor)
        return;
    for (int t = 0; t < num_tiers; t++) {
        if (u < cursor + fracs[t]) {
            inferno_env_put_int(env, "start_wave", waves[t]);
            inferno_env_put_int(env, "curriculum_agent", 1);
            return;
        }
        cursor += fracs[t];
    }
}

void puf_init(Env* env, Dict* kwargs) {
    env->num_agents = 1;
    env->agents[0].policy = 0;
    ENCOUNTER_INFERNO.init_context(INF_ENV_CONTEXT(env));
    ENCOUNTER_INFERNO.init_state(INF_ENV_STATE(env), INF_ENV_CONTEXT(env));

    /* per-env scenario RNG: init_state seeds every env to a fixed constant, so
       without this all envs run the same world. Hash the trainer-assigned index
       (+PUFFER_ENV_SEED_OFFSET for held-out runs). */
    uint32_t seed_offset = 0;
    const char* seed_offset_str = getenv("PUFFER_ENV_SEED_OFFSET");
    if (seed_offset_str)
        seed_offset = (uint32_t)strtoul(seed_offset_str, NULL, 10);
    uint32_t env_seed = inf_lowbias32((uint32_t)env->rng + seed_offset);
    if (env_seed == 0)
        env_seed = 1;
    env->state.rng_state = env_seed;

    memset(&env->log, 0, sizeof(Log));
    env->log.best_min_zuk_hp_normal = 1200.0f;

    int start_wave = (int)dict_get(kwargs, "start_wave");
    inferno_env_put_int(env, "start_wave", start_wave);
    inferno_env_put_float(env, "damage_reward_coeff",
        (float)dict_get(kwargs, "damage_reward_coeff"));
    inferno_env_put_float(env, "shield_penalty_coeff",
        (float)dict_get(kwargs, "shield_penalty_coeff"));
    inferno_env_put_float(env, "tag_reward_coeff",
        (float)dict_get(kwargs, "tag_reward_coeff"));

    static const char* const float_keys[] = {
        "shield_tag_reward_coeff",
        "budget_loadout_fraction",
        "offensive_prayer_reward_coeff",
        "death_penalty_coeff",
        "phase_900_bonus", "phase_600_bonus", "phase_300_bonus",
        "shield_penalty_episode_cap",
        "supply_milestone_brew_reward_coeff",
        "supply_milestone_restore_reward_coeff",
        "jad_damage_reward_coeff", "zuk_healer_damage_reward_coeff",
        "set_damage_reward_coeff",
        "jad_kill_bonus", "zuk_healer_kill_bonus", "set_kill_bonus",
        "post_healer_zuk_damage_coeff", "zuk_healer_phase_hp_delta_coeff",
        "post_healer_set_damage_reward_coeff", "post_healer_set_kill_bonus",
        "post_healer_set_alive_tick_penalty_coeff",
        "post_healer_set_alive_penalty_cap",
        "zuk_untagged_healer_tick_penalty_coeff",
        "zuk_untagged_healer_target_bonus_coeff",
        "zuk_safe_untagged_healer_target_bonus_coeff",
        "zuk_untagged_healer_nonmagic_attack_bonus_coeff",
        "zuk_healer_mage_attack_penalty_coeff",
        "post_jad_zuk_multiplier", "jad_alive_zuk_multiplier",
        "curriculum_supply_shared_jitter",
        "curriculum_supply_brew_jitter",
        "curriculum_supply_restore_jitter",
        "curriculum_no_brew_frac",
    };
    for (size_t k = 0; k < sizeof(float_keys) / sizeof(*float_keys); k++)
        inferno_env_put_float(env, float_keys[k], (float)dict_get(kwargs, float_keys[k]));

    inferno_env_put_float(env, "late_start_supply_profile_scale",
        (float)dict_get(kwargs, "late_start_supply_profile_scale"));
    inferno_env_put_int(env, "oracle_mode", (int)dict_get(kwargs, "oracle_mode"));
    inferno_env_put_int(env, "terminal_penalty_enabled",
        (int)dict_get(kwargs, "terminal_penalty_enabled"));

    static const char* const int_keys[] = {
        "curriculum_supply_jitter_mode",
        "curriculum_no_brew_mode",
    };
    for (size_t k = 0; k < sizeof(int_keys) / sizeof(*int_keys); k++)
        inferno_env_put_int(env, int_keys[k], (int)dict_get(kwargs, int_keys[k]));

    inferno_apply_obs_profile(env, (int)dict_get(kwargs, "obs_profile"));
    /* direct step_out_forecast_obs_mode wins over the obs_profile default; the
       _enabled key is read for completeness and used only if mode is absent. */
    (void)dict_get(kwargs, "step_out_forecast_obs_enabled");
    inferno_env_put_int(env, "step_out_forecast_obs_mode",
        (int)dict_get(kwargs, "step_out_forecast_obs_mode"));
    inferno_env_put_int(env, "loadout_profile_mode",
        (int)dict_get(kwargs, "loadout_profile_mode"));
    inferno_env_put_int(env, "zuk_healer_reward_mode",
        (int)dict_get(kwargs, "zuk_healer_reward_mode"));
    inferno_env_put_int(env, "joseph_reward_mode",
        (int)dict_get(kwargs, "joseph_reward_mode"));
    inferno_apply_reward_profile(env, (int)dict_get(kwargs, "reward_profile"));
    inferno_env_put_int(env, "zuk_safe_untagged_healer_target_mask",
        (int)dict_get(kwargs, "zuk_safe_untagged_healer_target_mask"));
    inferno_env_put_int(env, "zuk_force_safe_untagged_healer_target_mask",
        (int)dict_get(kwargs, "zuk_force_safe_untagged_healer_target_mask"));

    /* match the 1-indexed -> 0-indexed conversion done by the encounter's put_int */
    env->config_start_wave = (start_wave > 0) ? start_wave - 1 : 0;

    inferno_apply_curriculum(env, kwargs);
}

static inline void inferno_env_write_obs_mask(Env* env) {
    ENCOUNTER_INFERNO.write_obs(INF_ENV_STATE(env), INF_ENV_CONTEXT(env),
        (float*)env->agents[0].observations);
    float mask_f[INF_ACTION_MASK_SIZE];
    ENCOUNTER_INFERNO.write_mask(INF_ENV_STATE(env), INF_ENV_CONTEXT(env), mask_f);
    unsigned char* mask = env->agents[0].action_mask;
    for (int i = 0; i < INF_ACTION_MASK_SIZE; i++)
        mask[i] = mask_f[i] > 0.0f ? 1u : 0u;
}

void puf_reset(Env* env) {
    ENCOUNTER_INFERNO.reset(INF_ENV_STATE(env), INF_ENV_CONTEXT(env), 0);
    inferno_env_write_obs_mask(env);
    env->agents[0].rewards[0] = 0.0f;
    env->agents[0].terminals[0] = 0.0f;
}

static int inferno_terminal_shield_active(const InfernoState* s) {
    int si = s->zuk.shield_idx;
    return si >= 0 &&
        s->npcs[si].active &&
        s->npcs[si].death_ticks == 0 &&
        s->npcs[si].hp > 0;
}

static int inferno_terminal_behind_shield(const InfernoState* s) {
    if (!inferno_terminal_shield_active(s)) return 0;

    int si = s->zuk.shield_idx;
    int sx = s->npcs[si].x;
    int sz = INF_NPC_STATS[INF_NPC_ZUK_SHIELD].size;
    return s->player.x >= sx &&
        s->player.x < sx + sz &&
        s->player.y >= 41;
}

void puf_step(Env* env) {
    int inf_prof_enabled = INF_PROFILE_ENABLED();
    double inf_prof_total_t0 = inf_prof_enabled ? INF_PROFILE_NOW_MS() : 0.0;
    double inf_prof_t0 = inf_prof_total_t0;

    int acts[NUM_ATNS];
    for (int i = 0; i < NUM_ATNS; i++)
        acts[i] = (int)env->agents[0].actions[i];
    INF_PROFILE_MARK(INF_PROF_C_ACTIONS);

    INF_PROFILE_MARK(INF_PROF_C_PRE_STEP_TRACES);
    ENCOUNTER_INFERNO.step(INF_ENV_STATE(env), INF_ENV_CONTEXT(env), acts);
    INF_PROFILE_MARK(INF_PROF_C_ENCOUNTER_STEP);

    ENCOUNTER_INFERNO.write_obs(INF_ENV_STATE(env), INF_ENV_CONTEXT(env),
        (float*)env->agents[0].observations);
    INF_PROFILE_MARK(INF_PROF_C_WRITE_OBS);
    {
        float mask_f[INF_ACTION_MASK_SIZE];
        ENCOUNTER_INFERNO.write_mask(INF_ENV_STATE(env), INF_ENV_CONTEXT(env), mask_f);
        unsigned char* mask = env->agents[0].action_mask;
        for (int i = 0; i < INF_ACTION_MASK_SIZE; i++)
            mask[i] = mask_f[i] > 0.0f ? 1u : 0u;
    }
    INF_PROFILE_MARK(INF_PROF_C_WRITE_MASK);

    env->agents[0].rewards[0] =
        ENCOUNTER_INFERNO.get_reward(INF_ENV_STATE(env), INF_ENV_CONTEXT(env));

    int is_term = ENCOUNTER_INFERNO.is_terminal(INF_ENV_STATE(env), INF_ENV_CONTEXT(env));
    /* TRUNCATION SEAM: timeout-while-alive is reported as terminal for now; the
       dedicated truncation channel (agents[].terminals split) rides as a
       separate later commit. This is the one call site to revisit. */
    env->agents[0].terminals[0] = (float)is_term;
    INF_PROFILE_MARK(INF_PROF_C_REWARD_TERMINAL);

    INF_PROFILE_MARK(INF_PROF_C_POST_STEP_TRACES);

    /* terminal-only: accumulate completed-episode stats; vec_log sums each Log
       field across envs and divides by n for per-episode averages. */
    if (is_term) {
        InfernoState* s = INF_ENV_INFERNO(env);
        float min_zuk_hp_term = (s->winner == INF_OUTCOME_PLAYER_WON)
            ? 0.0f
            : (s->min_zuk_hp_seen > 0.0f ? s->min_zuk_hp_seen : 1200.0f);
        int terminal_shield_active = inferno_terminal_shield_active(s);
        int terminal_behind_shield = inferno_terminal_behind_shield(s);

        /* only count episodes at the configured start_wave; curriculum agents excluded. */
        if (s->start_wave != env->config_start_wave) goto skip_log;

        env->log.episode_return += s->episode_return;
        env->log.episode_length += (float)s->tick;
        env->log.damage_dealt += s->total_damage_dealt;
        env->log.zuk_healer_damage += s->total_zuk_healer_damage;
        env->log.damage_received += s->total_damage_received;
        env->log.hp_restored += s->total_hp_restored;
        env->log.wins += (s->winner == INF_OUTCOME_PLAYER_WON) ? 1.0f : 0.0f;
        env->log.wave += (float)s->wave;
        env->log.prayer_correct += (float)s->total_prayer_correct;
        env->log.prayer_total += (float)s->total_npc_attacks;
        env->log.offensive_prayer_attacks +=
            (float)s->total_offensive_prayer_attacks;
        env->log.offensive_prayer_correct +=
            (float)s->total_offensive_prayer_correct;
        for (int i = 0; i < 4; i++) {
            env->log.offensive_prayer_attacks_by_style[i] +=
                (float)s->offensive_prayer_attacks_by_style[i];
            env->log.offensive_prayer_correct_by_style[i] +=
                (float)s->offensive_prayer_correct_by_style[i];
        }
        env->log.idle_ticks += (float)s->total_idle_ticks;
        env->log.attack_ready_no_attack_ticks +=
            (float)s->total_attack_ready_no_attack_ticks;
        env->log.target_available_no_attack_ticks +=
            (float)s->total_target_available_no_attack_ticks;
        env->log.safe_attack_opportunity_missed_ticks +=
            (float)s->total_safe_attack_opportunity_missed_ticks;
        env->log.progressless_ticks += (float)s->total_progressless_ticks;
        env->log.npc_pressure_if_ready_count +=
            s->total_npc_pressure_if_ready_count;
        env->log.npc_pressure_this_tick_count +=
            s->total_npc_pressure_this_tick_count;
        env->log.npc_pressure_if_ready_max_hit +=
            s->total_npc_pressure_if_ready_max_hit;
        env->log.npc_pressure_this_tick_max_hit +=
            s->total_npc_pressure_this_tick_max_hit;
        env->log.npc_pressure_max_incoming_hit +=
            s->max_npc_pressure_incoming_hit;
        for (int i = 0; i < OSRS_INFERNO_IDLE_PHASE_COUNT; i++) {
            env->log.attack_ready_no_attack_ticks_by_phase[i] +=
                (float)s->attack_ready_no_attack_ticks_by_phase[i];
            env->log.target_available_no_attack_ticks_by_phase[i] +=
                (float)s->target_available_no_attack_ticks_by_phase[i];
            env->log.safe_attack_opportunity_missed_ticks_by_phase[i] +=
                (float)s->safe_attack_opportunity_missed_ticks_by_phase[i];
            env->log.progressless_ticks_by_phase[i] +=
                (float)s->progressless_ticks_by_phase[i];
        }
        env->log.brews_used += (float)s->total_brews_used;
        env->log.blood_healed += (float)s->total_blood_healed;
        env->log.unavoidable_off_prayer += (float)s->total_unavoidable_off;
        env->log.ranger_mager_same_tick_attacks +=
            (float)s->total_ranger_mager_same_tick_attacks;
        env->log.step_out_ranger_mager_same_tick_attacks +=
            (float)s->total_step_out_ranger_mager_same_tick_attacks;
        env->log.brews_remaining += (float)s->player.brew_doses;
        env->log.restores_remaining += (float)s->player.restore_doses;
        env->log.prayer_at_death += (float)s->player.current_prayer;
        env->log.npc_kills += (float)s->total_npc_kills;
        env->log.gear_switches += (float)s->total_gear_switches;
        env->log.current_ranged += (float)s->player.current_ranged;
        env->log.current_magic += (float)s->player.current_magic;
        env->log.start_wave += (float)env->config_start_wave;

        for (int t = 0; t < INF_NUM_NPC_TYPES; t++) {
            env->log.prayer_correct_by_type[t] += (float)s->prayer_correct_by_type[t];
            env->log.attacks_by_type[t] += (float)s->attacks_by_type[t];
            env->log.dmg_from_type[t] += s->dmg_from_type[t];
            env->log.killed_by_type[t] += (float)s->killed_by_type[t];
        }

        env->log.behind_shield_pct += (s->total_zuk_ticks > 0)
            ? (float)s->behind_shield_ticks / (float)s->total_zuk_ticks : 0.0f;

        {
            float zhp = 1200.0f;
            for (int n = 0; n < INF_MAX_NPCS; n++) {
                if (s->npcs[n].type == INF_NPC_ZUK) {
                    zhp = (float)s->npcs[n].hp;
                    break;
                }
            }
            if (s->winner == INF_OUTCOME_PLAYER_WON) zhp = 0.0f;
            env->log.zuk_hp_remaining += zhp;
        }
        env->log.min_zuk_hp_seen += min_zuk_hp_term;

        env->log.n += 1.0f;

        {
            env->log.episode_return_normal += s->episode_return;
            env->log.wins_normal += (s->winner == INF_OUTCOME_PLAYER_WON) ? 1.0f : 0.0f;
            env->log.min_zuk_hp_normal += min_zuk_hp_term;
            env->log.n_normal += 1.0f;
            int won = (s->winner == INF_OUTCOME_PLAYER_WON);
            int phase_bucket = won ? 4
                : (min_zuk_hp_term <= 300.0f) ? 3
                : (min_zuk_hp_term <= 600.0f) ? 2
                : (min_zuk_hp_term <= 900.0f) ? 1 : 0;
            env->log.phase_reached_normal_sum += (float)phase_bucket;
            if (!won) {
                env->log.episode_length_normal_died += (float)s->tick;
                env->log.n_normal_died += 1.0f;
                env->log.brews_remaining_normal_died += (float)s->player.brew_doses;
                env->log.restores_remaining_normal_died += (float)s->player.restore_doses;
                env->log.prayer_at_death_normal_died += (float)s->player.current_prayer;
                if (terminal_shield_active)
                    env->log.count_died_with_shield_active_normal += 1.0f;
                if (terminal_behind_shield)
                    env->log.count_died_behind_shield_normal += 1.0f;
                for (int t = 0; t < INF_NUM_NPC_TYPES; t++)
                    env->log.killed_by_type_normal[t] += (float)s->killed_by_type[t];
            }
            if (min_zuk_hp_term <= 300.0f) env->log.count_min_hp_le_300_normal += 1.0f;
            if (min_zuk_hp_term <= 240.0f) env->log.count_min_hp_le_240_normal += 1.0f;
            if (min_zuk_hp_term <= 150.0f) env->log.count_min_hp_le_150_normal += 1.0f;
            if (min_zuk_hp_term < env->log.best_min_zuk_hp_normal)
                env->log.best_min_zuk_hp_normal = min_zuk_hp_term;

            /* Per-threshold survival and damage, accumulated only after crossing. */
            if (s->tick_at_le_300 >= 0) {
                env->log.ticks_after_300_normal_sum += (float)(s->tick - s->tick_at_le_300);
                env->log.damage_after_300_normal_sum += s->damage_after_300;
            }
            if (s->tick_at_le_240 >= 0) {
                env->log.ticks_after_240_normal_sum += (float)(s->tick - s->tick_at_le_240);
                env->log.damage_after_240_normal_sum += s->damage_after_240;
            }
            if (s->tick_at_le_150 >= 0) {
                env->log.ticks_after_150_normal_sum += (float)(s->tick - s->tick_at_le_150);
                env->log.damage_after_150_normal_sum += s->damage_after_150;
            }
            if (s->tick_at_zuk_healer_spawn >= 0 || s->zuk.healer_spawned) {
                env->log.count_healer_spawned_normal += 1.0f;
                env->log.zuk_hp_max_after_healer_spawn_normal_sum +=
                    s->zuk_hp_max_after_healer_spawn;
            }
            env->log.shield_tags_normal_sum += (float)s->total_shield_tags;
            if (s->total_shield_tags >= 1)
                env->log.count_shield_tags_ge_1_normal += 1.0f;
            if (s->total_zuk_healer_tags >= 1)
                env->log.count_zuk_healers_tagged_ge_1_normal += 1.0f;
            if (s->total_zuk_healer_tags >= 2)
                env->log.count_zuk_healers_tagged_ge_2_normal += 1.0f;
            if (s->total_zuk_healer_tags >= 4)
                env->log.count_zuk_healers_tagged_ge_4_normal += 1.0f;
            if (s->total_zuk_healer_kills >= 1)
                env->log.count_zuk_healers_killed_ge_1_normal += 1.0f;
            if (s->total_zuk_healer_kills >= 2)
                env->log.count_zuk_healers_killed_ge_2_normal += 1.0f;
            if (s->total_zuk_healer_kills >= 4)
                env->log.count_zuk_healers_killed_ge_4_normal += 1.0f;
            if (s->tick_at_all_zuk_healers_dead >= 0) {
                env->log.count_all_zuk_healers_dead_normal += 1.0f;
                float post_healer_survival =
                    (float)(s->tick - s->tick_at_all_zuk_healers_dead);
                env->log.post_healer_survival_ticks_normal_sum +=
                    post_healer_survival;
                env->log.damage_after_all_zuk_healers_dead_normal_sum +=
                    s->damage_after_all_zuk_healers_dead;
                env->log.zuk_hp_at_all_zuk_healers_dead_normal_sum +=
                    s->zuk_hp_at_all_zuk_healers_dead;
                env->log.offshield_ticks_after_all_zuk_healers_dead_normal_sum +=
                    (float)s->offshield_ticks_after_all_zuk_healers_dead;
                if (post_healer_survival >= 20.0f || won)
                    env->log.count_healer_resolved_20_normal += 1.0f;
                if (s->damage_after_all_zuk_healers_dead > 0.0f)
                    env->log.count_reengaged_zuk_after_healers_normal += 1.0f;
                if (s->tick_at_first_zuk_hit_after_all_healers_dead >= 0) {
                    env->log.ticks_all_healers_dead_to_first_zuk_hit_normal_sum +=
                        (float)(s->tick_at_first_zuk_hit_after_all_healers_dead -
                            s->tick_at_all_zuk_healers_dead);
                }
            }
            if (s->total_zuk_healer_target_ticks >= 1)
                env->log.count_zuk_healers_targeted_ge_1_normal += 1.0f;
            if (s->total_zuk_healer_attack_fires >= 1)
                env->log.count_zuk_healers_attacked_ge_1_normal += 1.0f;
            if (s->total_zuk_healer_attackable_ticks >= 1)
                env->log.count_zuk_healers_attackable_ge_1_normal += 1.0f;
            env->log.zuk_untagged_healer_target_bonus_coeff_normal_sum +=
                INF_ENV_INFERNO_CONTEXT(env)->config.zuk_untagged_healer_target_bonus_coeff;
            env->log.zuk_safe_untagged_healer_target_bonus_coeff_normal_sum +=
                INF_ENV_INFERNO_CONTEXT(env)->config.zuk_safe_untagged_healer_target_bonus_coeff;
            env->log.zuk_healer_reward_mode_normal_sum +=
                (float)INF_ENV_INFERNO_CONTEXT(env)->config.zuk_healer_reward_mode;
            env->log.zuk_untagged_healer_targets_normal_sum +=
                (float)s->total_zuk_untagged_healer_targets;
            env->log.zuk_safe_untagged_healer_targets_normal_sum +=
                (float)s->total_zuk_safe_untagged_healer_targets;
            env->log.zuk_unsafe_untagged_healer_targets_normal_sum +=
                (float)s->total_zuk_unsafe_untagged_healer_targets;
            env->log.zuk_untagged_healer_target_reward_count_normal_sum +=
                (float)s->total_zuk_untagged_healer_target_rewards;
            env->log.zuk_safe_untagged_healer_target_reward_count_normal_sum +=
                (float)s->total_zuk_safe_untagged_healer_target_rewards;
            env->log.post_healer_set_damage_reward_coeff_normal_sum +=
                INF_ENV_INFERNO_CONTEXT(env)->config.post_healer_set_damage_reward_coeff;
            env->log.post_healer_set_kill_bonus_coeff_normal_sum +=
                INF_ENV_INFERNO_CONTEXT(env)->config.post_healer_set_kill_bonus;
            env->log.post_healer_set_alive_penalty_coeff_normal_sum +=
                INF_ENV_INFERNO_CONTEXT(env)->config.post_healer_set_alive_tick_penalty_coeff;
            env->log.post_healer_set_alive_penalty_cap_normal_sum +=
                INF_ENV_INFERNO_CONTEXT(env)->config.post_healer_set_alive_penalty_cap;
            env->log.post_healer_set_damage_reward_normal_sum +=
                s->post_healer_set_damage_reward_total;
            env->log.post_healer_set_kill_bonus_reward_normal_sum +=
                s->post_healer_set_kill_bonus_total;
            env->log.post_healer_set_alive_penalty_normal_sum +=
                s->post_healer_set_alive_penalty_total;
            env->log.post_healer_set_pressure_normal_sum +=
                s->post_healer_set_pressure_total;
            env->log.action_mask_checks_normal_sum +=
                (float)s->total_action_mask_checks;
            env->log.target_head_valid_healer_count_normal_sum +=
                (float)s->target_head_valid_healer_count;
            env->log.target_head_valid_zuk_count_normal_sum +=
                (float)s->target_head_valid_zuk_count;
            env->log.target_head_valid_set_count_normal_sum +=
                (float)s->target_head_valid_set_count;
            if (s->total_action_mask_checks > 0) {
                for (int h = 0; h < 9; h++) {
                    env->log.zero_valid_action_head_count_normal_sum[h] +=
                        (float)s->zero_valid_action_head_count[h];
                    env->log.valid_action_count_min_by_head_normal_sum[h] +=
                        (float)s->min_valid_action_count_by_head[h];
                }
            }
            if (s->total_zuk_healer_target_ticks >= 1) {
                env->log.zuk_healer_target_cannot_attack_ticks_normal_sum +=
                    (float)s->total_zuk_healer_cannot_attack_ticks;
                env->log.zuk_healer_target_cooldown_ticks_normal_sum +=
                    (float)s->total_zuk_healer_cooldown_ticks;
                env->log.zuk_healer_target_out_of_range_ticks_normal_sum +=
                    (float)s->total_zuk_healer_out_of_range_ticks;
                env->log.zuk_healer_target_attackable_ticks_normal_sum +=
                    (float)s->total_zuk_healer_attackable_ticks;
            }
            if (s->tick_at_le_240 >= 0) {
                env->log.hp_restored_after_240_normal_sum += s->hp_restored_after_240;
                env->log.spark_damage_after_240_normal_sum += s->spark_damage_after_240;
                env->log.offshield_ticks_after_240_normal_sum +=
                    (float)s->offshield_ticks_after_240;
                if (s->tick_at_first_zuk_healer_target >= 0) {
                    env->log.ticks_240_to_first_healer_target_normal_sum +=
                        (float)(s->tick_at_first_zuk_healer_target - s->tick_at_le_240);
                }
                if (s->tick_at_first_zuk_healer_attack >= 0) {
                    env->log.ticks_240_to_first_healer_attack_normal_sum +=
                        (float)(s->tick_at_first_zuk_healer_attack - s->tick_at_le_240);
                }
                if (s->tick_at_first_zuk_healer_tag >= 0) {
                    env->log.ticks_240_to_first_healer_tag_normal_sum +=
                        (float)(s->tick_at_first_zuk_healer_tag - s->tick_at_le_240);
                }
                if (s->tick_at_all_zuk_healers_tagged >= 0) {
                    env->log.ticks_240_to_all_healers_tagged_normal_sum +=
                        (float)(s->tick_at_all_zuk_healers_tagged - s->tick_at_le_240);
                }
                if (s->tick_at_all_zuk_healers_dead >= 0) {
                    env->log.ticks_240_to_all_healers_dead_normal_sum +=
                        (float)(s->tick_at_all_zuk_healers_dead - s->tick_at_le_240);
                }
            }
            if (!won) {
                int jad_alive = 0, zuk_healer_alive = 0, jad_healer_alive = 0, set_alive = 0;
                for (int n = 0; n < INF_MAX_NPCS; n++) {
                    if (s->npcs[n].hp <= 0) continue;
                    int t = s->npcs[n].type;
                    if (t == INF_NPC_JAD) jad_alive = 1;
                    else if (t == INF_NPC_HEALER_ZUK) zuk_healer_alive = 1;
                    else if (t == INF_NPC_HEALER_JAD) jad_healer_alive = 1;
                    else if (inf_npc_type_is_set_pressure(t)) set_alive = 1;
                }
                if (jad_alive) env->log.count_died_with_jad_alive_normal += 1.0f;
                if (zuk_healer_alive || jad_healer_alive)
                    env->log.count_died_with_healer_alive_normal += 1.0f;
                if (zuk_healer_alive) env->log.count_died_with_zuk_healer_alive_normal += 1.0f;
                if (jad_healer_alive) env->log.count_died_with_jad_healer_alive_normal += 1.0f;
                if (set_alive) env->log.count_died_with_set_alive_normal += 1.0f;
                if (s->tick_at_le_240 >= 0) {
                    env->log.count_died_after_240_normal += 1.0f;
                    env->log.brews_remaining_after_240_death_normal_sum +=
                        (float)s->player.brew_doses;
                    env->log.restores_remaining_after_240_death_normal_sum +=
                        (float)s->player.restore_doses;
                    env->log.prayer_at_death_after_240_normal_sum +=
                        (float)s->player.current_prayer;
                    if (terminal_shield_active)
                        env->log.count_died_after_240_shield_active_normal += 1.0f;
                    if (terminal_behind_shield)
                        env->log.count_died_after_240_behind_shield_normal += 1.0f;
                    if (s->tick_at_all_zuk_healers_dead >= 0 ||
                            s->total_zuk_healer_kills >= 4) {
                        env->log.count_died_after_240_all_healers_dead_normal += 1.0f;
                        if (set_alive)
                            env->log.count_died_after_all_healers_dead_with_set_alive_normal += 1.0f;
                        if (s->killed_by_type[INF_NPC_ZUK] > 0)
                            env->log.count_died_after_all_healers_dead_killed_by_zuk_normal += 1.0f;
                        if (s->killed_by_type[INF_NPC_RANGER] > 0)
                            env->log.count_died_after_all_healers_dead_killed_by_ranger_normal += 1.0f;
                        if (s->killed_by_type[INF_NPC_MAGER] > 0)
                            env->log.count_died_after_all_healers_dead_killed_by_mager_normal += 1.0f;
                        if (terminal_shield_active)
                            env->log.count_died_after_all_healers_dead_with_shield_active_normal += 1.0f;
                        if (terminal_behind_shield)
                            env->log.count_died_after_all_healers_dead_behind_shield_normal += 1.0f;
                        env->log.brews_remaining_after_all_healers_dead_death_normal_sum +=
                            (float)s->player.brew_doses;
                        env->log.restores_remaining_after_all_healers_dead_death_normal_sum +=
                            (float)s->player.restore_doses;
                    } else if (s->total_zuk_healer_kills > 0) {
                        env->log.count_died_after_240_some_healers_killed_normal += 1.0f;
                    } else if (s->total_zuk_healer_tags > 0) {
                        env->log.count_died_after_240_some_healers_tagged_normal += 1.0f;
                    } else {
                        env->log.count_died_after_240_never_tagged_healer_normal += 1.0f;
                    }
                }
            }
        }
    skip_log:
        INF_PROFILE_MARK(INF_PROF_C_TERMINAL_LOG);
        ENCOUNTER_INFERNO.reset(INF_ENV_STATE(env), INF_ENV_CONTEXT(env), 0);
        inferno_env_write_obs_mask(env);
        INF_PROFILE_MARK(INF_PROF_C_RESET);
    }

    if (inf_prof_enabled)
        INF_PROFILE_ADD(INF_PROF_C_STEP_TOTAL, INF_PROFILE_NOW_MS() - inf_prof_total_t0);
}

void puf_render(Env* env) {
    (void)env;
}

void puf_close(Env* env) {
    ENCOUNTER_INFERNO.destroy_context(INF_ENV_CONTEXT(env));
}

static void inferno_log_idle_metric(
    Dict* out,
    const char* name,
    float total,
    const float by_phase[OSRS_INFERNO_IDLE_PHASE_COUNT]
) {
    static const char* phases[OSRS_INFERNO_IDLE_PHASE_COUNT] = {
        "set",
        "jad",
        "zuk_pre_jad",
        "zuk_jad",
        "zuk_healers",
        "zuk_post_healers",
    };
    /* dict_set keeps the key POINTER, so phase keys use a static pool (not a stack
       buffer) to stay unique within one puf_log flush. */
    static char key_pool[64][96];
    static int key_pool_next = 0;
    dict_set(out, name, total);
    for (int i = 0; i < OSRS_INFERNO_IDLE_PHASE_COUNT; i++) {
        char* key = key_pool[key_pool_next];
        key_pool_next = (key_pool_next + 1) & 63;
        snprintf(key, sizeof(key_pool[0]), "%s_%s", name, phases[i]);
        dict_set(out, key, by_phase[i]);
    }
}

void puf_log(Log* log, Dict* out) {
    dict_set(out, "episode_return", log->episode_return);
    dict_set(out, "damage_dealt", log->damage_dealt);
    dict_set(out, "damage_received", log->damage_received);
    dict_set(out, "episode_length", log->episode_length);

    float damage_per_tick = log->episode_length > 0.0f
        ? log->damage_dealt / log->episode_length : 0.0f;
    dict_set(out, "damage_per_100_ticks", damage_per_tick * 100.0f);
    dict_set(out, "wins", log->wins);
    dict_set(out, "wave", log->wave);
    dict_set(out, "idle_ticks", log->idle_ticks);
    inferno_log_idle_metric(
        out,
        "attack_ready_no_attack_ticks",
        log->attack_ready_no_attack_ticks,
        log->attack_ready_no_attack_ticks_by_phase);
    inferno_log_idle_metric(
        out,
        "target_available_no_attack_ticks",
        log->target_available_no_attack_ticks,
        log->target_available_no_attack_ticks_by_phase);
    inferno_log_idle_metric(
        out,
        "safe_attack_opportunity_missed_ticks",
        log->safe_attack_opportunity_missed_ticks,
        log->safe_attack_opportunity_missed_ticks_by_phase);
    inferno_log_idle_metric(
        out,
        "progressless_ticks",
        log->progressless_ticks,
        log->progressless_ticks_by_phase);
    float pressure_denom = log->episode_length > 0.0f
        ? log->episode_length : 1.0f;
    dict_set(out, "npc_pressure_if_ready_count_per_tick",
        log->npc_pressure_if_ready_count / pressure_denom);
    dict_set(out, "npc_pressure_this_tick_count_per_tick",
        log->npc_pressure_this_tick_count / pressure_denom);
    dict_set(out, "npc_pressure_max_incoming_hit",
        log->npc_pressure_max_incoming_hit);
    dict_set(out, "brews_used", log->brews_used);

    float prayer_rate = (log->prayer_total > 0.0f)
        ? log->prayer_correct / log->prayer_total : 0.0f;
    dict_set(out, "prayer_correct_rate", prayer_rate);
    float offensive_prayer_rate = log->offensive_prayer_attacks > 0.0f
        ? log->offensive_prayer_correct / log->offensive_prayer_attacks : 0.0f;
    dict_set(out, "offensive_prayer_correct_rate", offensive_prayer_rate);
    dict_set(out, "offensive_prayer_attacks", log->offensive_prayer_attacks);
    dict_set(out, "offensive_prayer_ranged_correct_rate",
        log->offensive_prayer_attacks_by_style[ATTACK_STYLE_RANGED] > 0.0f
            ? log->offensive_prayer_correct_by_style[ATTACK_STYLE_RANGED] /
                log->offensive_prayer_attacks_by_style[ATTACK_STYLE_RANGED]
            : 0.0f);
    dict_set(out, "offensive_prayer_magic_correct_rate",
        log->offensive_prayer_attacks_by_style[ATTACK_STYLE_MAGIC] > 0.0f
            ? log->offensive_prayer_correct_by_style[ATTACK_STYLE_MAGIC] /
                log->offensive_prayer_attacks_by_style[ATTACK_STYLE_MAGIC]
            : 0.0f);
    dict_set(out, "brews_remaining", log->brews_remaining);
    dict_set(out, "restores_remaining", log->restores_remaining);
    dict_set(out, "behind_shield_pct", log->behind_shield_pct);
    dict_set(out, "min_zuk_hp_seen", log->min_zuk_hp_seen);
    dict_set(out, "hp_restored", log->hp_restored);
    dict_set(out, "zuk_healer_damage", log->zuk_healer_damage);

    float wr = log->wins;
    float score;
    int start_wave = (int)(log->start_wave + 0.5f);
    if (start_wave >= 68) {
        score = (1200.0f - log->min_zuk_hp_seen) / 1200.0f;
    } else {
        float wave_frac = log->wave / (float)INF_NUM_WAVES;
        score = wr + (1.0f - wr) * wave_frac * 0.5f;
    }
    dict_set(out, "score", score);

    if (log->n_normal > 0.0f) {
        float min_zuk_hp_normal = log->min_zuk_hp_normal / log->n_normal;
        float score_normal = (1200.0f - min_zuk_hp_normal) / 1200.0f;
        float frac_all_healers_dead =
            log->count_all_zuk_healers_dead_normal / log->n_normal;
        float frac_died_after_240 =
            log->count_died_after_240_normal / log->n_normal;
        float post_healer_zuk_damage =
            log->count_all_zuk_healers_dead_normal > 0.0f
                ? log->damage_after_all_zuk_healers_dead_normal_sum /
                    log->count_all_zuk_healers_dead_normal : 0.0f;
        float frac_died_with_zuk_healer =
            log->count_died_with_zuk_healer_alive_normal / log->n_normal;
        float healer_resolve =
            log->count_healer_resolved_20_normal / log->n_normal;
        dict_set(out, "score_normal", score_normal);
        dict_set(out, "phase_reached_normal",
            log->phase_reached_normal_sum / log->n_normal);
        dict_set(out, "min_zuk_hp_normal", min_zuk_hp_normal);
        dict_set(out, "frac_min_hp_le_240_normal",
            log->count_min_hp_le_240_normal / log->n_normal);
        dict_set(out, "frac_all_zuk_healers_dead_normal", frac_all_healers_dead);
        dict_set(out, "frac_died_after_240_normal", frac_died_after_240);
        dict_set(out, "post_healer_objective_normal",
            healer_resolve + 0.001f * post_healer_zuk_damage -
                0.1f * frac_died_with_zuk_healer);
        dict_set(out, "spark_damage_after_240_normal",
            log->spark_damage_after_240_normal_sum / log->n_normal);
        dict_set(out, "hp_restored_after_240_normal",
            log->hp_restored_after_240_normal_sum / log->n_normal);
    } else {
        dict_set(out, "score_normal", 0.0f);
        dict_set(out, "phase_reached_normal", 0.0f);
        dict_set(out, "min_zuk_hp_normal", 1200.0f);
        dict_set(out, "frac_min_hp_le_240_normal", 0.0f);
        dict_set(out, "frac_all_zuk_healers_dead_normal", 0.0f);
        dict_set(out, "frac_died_after_240_normal", 0.0f);
        dict_set(out, "post_healer_objective_normal", 0.0f);
        dict_set(out, "spark_damage_after_240_normal", 0.0f);
        dict_set(out, "hp_restored_after_240_normal", 0.0f);
    }
}

#endif
