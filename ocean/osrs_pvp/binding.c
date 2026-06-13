/**
 * @file binding.c
 * @brief Static-native binding for OSRS PVP environment
 *
 * Bridges vecenv.h's contract (float actions, float terminals) with the PVP
 * env's internal types (int actions, unsigned char terminals) using a wrapper
 * struct. PVP source headers are untouched.
 */

#include <time.h>
#include <stdlib.h>

static int pvp_profile_enabled(void);
static double pvp_profile_now_ms(void);
static void pvp_profile_add(int slot, double ms);
static void pvp_profile_mark(int enabled, double* last_ms, int slot);

#define OSRS_PVP_PROFILE_ENABLED() pvp_profile_enabled()
#define OSRS_PVP_PROFILE_NOW_MS() pvp_profile_now_ms()
#define OSRS_PVP_PROFILE_ADD(slot, ms) pvp_profile_add((slot), (ms))
#define OSRS_PVP_PROFILE_MARK(slot) \
    pvp_profile_mark(osrs_pvp_prof_enabled, &osrs_pvp_prof_t0, (slot))

#include "../osrs/osrs_env.h"

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#include "../osrs/encounters/encounter_nh_pvp.h"
#ifdef OSRS_VISUAL
#include "../osrs/encounters/encounter_inferno.h"
#include "../osrs/encounters/encounter_zulrah.h"
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
#endif
#pragma GCC diagnostic pop

typedef struct {
    Log log;
    Player players[NUM_AGENTS];
    int tick;
    int episode_over;
    int winner;
    int auto_reset;
    int pid_holder;
    int pid_shuffle_countdown;
    int is_lms;
    uint32_t rng_state;
    uint32_t rng_seed;
    uint32_t rng_reset_count;
    int has_rng_seed;
    int pending_actions[NUM_AGENTS * NUM_ACTION_HEADS];
    int last_executed_actions[NUM_AGENTS * NUM_ACTION_HEADS];
    RewardShapingConfig shaping;
    OsrsPvpRuntime pvp_runtime;
    float episode_return;
} PvpStateSnapshot;

typedef PvpStateSnapshot State;

static int g_pvp_profile_enabled = -1;
static double g_pvp_profile_ms[PVP_PROF_COUNT];

static int pvp_profile_enabled(void) {
    if (g_pvp_profile_enabled < 0) {
        const char* text = getenv("PUFFER_PVP_PROFILE");
        g_pvp_profile_enabled = (text && text[0] && text[0] != '0') ? 1 : 0;
    }
    return g_pvp_profile_enabled;
}

static double pvp_profile_now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1000000.0;
}

static void pvp_profile_add(int slot, double ms) {
    if (slot < 0 || slot >= PVP_PROF_COUNT) abort();
    #pragma omp atomic update
    g_pvp_profile_ms[slot] += ms;
}

static void pvp_profile_mark(int enabled, double* last_ms, int slot) {
    if (!enabled) return;
    double now = pvp_profile_now_ms();
    pvp_profile_add(slot, now - *last_ms);
    *last_ms = now;
}

static double pvp_profile_read_reset_ms(int slot) {
    if (slot < 0 || slot >= PVP_PROF_COUNT) abort();
    double value;
    #pragma omp atomic read
    value = g_pvp_profile_ms[slot];
    #pragma omp atomic write
    g_pvp_profile_ms[slot] = 0.0;
    return value;
}

#define PVP_PROFILE_ENABLED() pvp_profile_enabled()
#define PVP_PROFILE_NOW_MS() pvp_profile_now_ms()
#define PVP_PROFILE_ADD(slot, ms) pvp_profile_add((slot), (ms))
#define PVP_PROFILE_MARK(slot) pvp_profile_mark(pvp_prof_enabled, &pvp_prof_t0, (slot))

/* vecenv-compatible header fields must stay first. */
typedef struct {
    void* observations;
    float* actions;
    float* rewards;
    float* terminals;
    unsigned char* action_mask;
    int num_agents;
    int rng;
    Log log;

    OsrsEnv pvp;
    PvpStateSnapshot state;

    int ocean_acts_staging[NUM_ACTION_HEADS];
    unsigned char ocean_term_staging;

    float ticks_per_second;
    double last_step_time;

    int tag;
    int boundary_reached;

    void* obs_ptr[2];
    float* action_ptr[2];
    float* reward_ptr[2];
    float* terminal_ptr[2];
    unsigned char* action_mask_ptr[2];

    int use_rollout_opponent;

    int scripted_opp_type;
} PvpEnv;

#define OBS_SIZE OCEAN_OBS_SIZE
#define NUM_ATNS NUM_ACTION_HEADS
#define ACT_SIZES { \
    EQUIP_CLICK_DIM, EQUIP_CLICK_DIM, EQUIP_CLICK_DIM, EQUIP_CLICK_DIM, \
    ATTACK_DIM, SPECIAL_DIM, OVERHEAD_DIM, FOOD_DIM, POTION_DIM, \
    KARAMBWAN_DIM, VENG_DIM, OFFENSIVE_DIM, MOVE_DIM \
}
#define OBS_TENSOR_T FloatTensor
#define Env PvpEnv
#define MY_USES_TAGS
#define MY_USES_PERM
#define MY_USES_SCRIPTED_OPPS
#define MY_ACTION_MASK ACTION_MASK_SIZE

static CollisionMap* pvp_shared_wilderness_collision_map(void) {
    static CollisionMap* cmap = NULL;
    if (cmap == NULL) {
        osrs_asset_require_group(OSRS_ASSET_GROUP_PVP);
        cmap = collision_map_load(OSRS_ASSET("wilderness.cmap"));
        if (cmap == NULL) {
            fprintf(stderr, "osrs_pvp: failed to load wilderness.cmap\n");
            abort();
        }
    }
    return cmap;
}

static void pvp_env_rewire_internal_buffers(Env* env) {
    env->pvp.observations = env->pvp._obs_buf;
    env->pvp.actions = env->pvp._acts_buf;
    env->pvp.rewards = env->pvp._rews_buf;
    env->pvp.terminals = env->pvp._terms_buf;
    env->pvp.action_masks = env->pvp._masks_buf;
    env->pvp.ocean_io.agent_actions = env->ocean_acts_staging;
    env->pvp.ocean_io.agent_terminals = &env->ocean_term_staging;
}

static void pvp_env_rewire_rollout_buffers(Env* env) {
    env->pvp.ocean_io.agent_obs = env->obs_ptr[0]
        ? (float*)env->obs_ptr[0] : (float*)env->observations;
    env->pvp.ocean_io.agent_obs_p1 = env->obs_ptr[1] ? (float*)env->obs_ptr[1] : NULL;
    env->pvp.ocean_io.agent_rewards = env->rewards;
    env->pvp.ocean_io.agent_actions = env->ocean_acts_staging;
    env->pvp.ocean_io.agent_terminals = &env->ocean_term_staging;
    env->pvp.action_masks_agents = 0x1;
    if (env->action_mask_ptr[1] != NULL) {
        env->pvp.action_masks_agents |= 0x2;
    }
}

static void pvp_env_rewire_after_load(Env* env, void* collision_map, void* client,
        const void* encounter_def, void* encounter_state, void* encounter_context) {
    pvp_env_rewire_internal_buffers(env);
    env->pvp.collision_map = collision_map;
    env->pvp.client = client;
    env->pvp.encounter_def = encounter_def;
    env->pvp.encounter_state = encounter_state;
    env->pvp.encounter_context = encounter_context;
    pvp_env_rewire_rollout_buffers(env);
}

static void pvp_env_copy_action_masks_to_rollout(Env* env) {
    if (env->action_mask_ptr[0]) {
        memcpy(env->action_mask_ptr[0], env->pvp._masks_buf, ACTION_MASK_SIZE);
    }
    if (env->action_mask_ptr[1]) {
        memcpy(env->action_mask_ptr[1], env->pvp._masks_buf + ACTION_MASK_SIZE,
            ACTION_MASK_SIZE);
    }
}

static void pvp_state_store(Env* env, PvpStateSnapshot* out) {
    out->log = env->pvp.log;
    memcpy(out->players, env->pvp.players, sizeof(out->players));
    out->tick = env->pvp.tick;
    out->episode_over = env->pvp.episode_over;
    out->winner = env->pvp.winner;
    out->auto_reset = env->pvp.auto_reset;
    out->pid_holder = env->pvp.pid_holder;
    out->pid_shuffle_countdown = env->pvp.pid_shuffle_countdown;
    out->is_lms = env->pvp.is_lms;
    out->rng_state = env->pvp.rng_state;
    out->rng_seed = env->pvp.rng_seed;
    out->rng_reset_count = env->pvp.rng_reset_count;
    out->has_rng_seed = env->pvp.has_rng_seed;
    memcpy(out->pending_actions, env->pvp.pending_actions, sizeof(out->pending_actions));
    memcpy(out->last_executed_actions, env->pvp.last_executed_actions,
        sizeof(out->last_executed_actions));
    out->shaping = env->pvp.shaping;
    out->pvp_runtime = env->pvp.pvp_runtime;
    out->episode_return = env->pvp._episode_return;
}

static void pvp_state_load(Env* env, const PvpStateSnapshot* in) {
    void* collision_map = env->pvp.collision_map;
    void* client = env->pvp.client;
    const void* encounter_def = env->pvp.encounter_def;
    void* encounter_state = env->pvp.encounter_state;
    void* encounter_context = env->pvp.encounter_context;
    env->pvp.log = in->log;
    memcpy(env->pvp.players, in->players, sizeof(env->pvp.players));
    env->pvp.tick = in->tick;
    env->pvp.episode_over = in->episode_over;
    env->pvp.winner = in->winner;
    env->pvp.auto_reset = in->auto_reset;
    env->pvp.pid_holder = in->pid_holder;
    env->pvp.pid_shuffle_countdown = in->pid_shuffle_countdown;
    env->pvp.is_lms = in->is_lms;
    env->pvp.rng_state = in->rng_state;
    env->pvp.rng_seed = in->rng_seed;
    env->pvp.rng_reset_count = in->rng_reset_count;
    env->pvp.has_rng_seed = in->has_rng_seed;
    memcpy(env->pvp.pending_actions, in->pending_actions, sizeof(env->pvp.pending_actions));
    memcpy(env->pvp.last_executed_actions, in->last_executed_actions,
        sizeof(env->pvp.last_executed_actions));
    env->pvp.shaping = in->shaping;
    env->pvp.pvp_runtime = in->pvp_runtime;
    env->pvp._episode_return = in->episode_return;
    pvp_env_rewire_after_load(env, collision_map, client,
        encounter_def, encounter_state, encounter_context);
}

static void puffer_state_refresh(Env* env) {
    pvp_state_load(env, &env->state);
    pvp_generate_exported_slot_observations_and_masks(&env->pvp);
    ocean_write_obs(&env->pvp);
    if (env->pvp.ocean_io.agent_obs_p1) ocean_write_obs_p1(&env->pvp);
    pvp_env_copy_action_masks_to_rollout(env);
    env->pvp.ocean_io.agent_rewards[0] = 0.0f;
    env->pvp.ocean_io.agent_terminals[0] = 0;
    memset(env->pvp.step_rewards, 0, sizeof(env->pvp.step_rewards));
    memset(env->pvp.step_terminals, 0, sizeof(env->pvp.step_terminals));
    env->terminals[0] = 0.0f;
    if (env->terminal_ptr[1]) *env->terminal_ptr[1] = 0.0f;
    if (env->reward_ptr[1]) *env->reward_ptr[1] = 0.0f;
    env->boundary_reached = 0;
}

static void pvp_env_set_gear_tier(Env* env, int tier) {
    if (tier == -1) {
        env->pvp.pvp_runtime.gear_tier_weights[0] = 0.60f;
        env->pvp.pvp_runtime.gear_tier_weights[1] = 0.25f;
        env->pvp.pvp_runtime.gear_tier_weights[2] = 0.10f;
        env->pvp.pvp_runtime.gear_tier_weights[3] = 0.05f;
        return;
    }

    if (tier < 0 || tier > 3) {
        fprintf(stderr, "osrs_pvp invalid gear_tier %d\n", tier);
        abort();
    }

    for (int i = 0; i < 4; i++) env->pvp.pvp_runtime.gear_tier_weights[i] = 0.0f;
    env->pvp.pvp_runtime.gear_tier_weights[tier] = 1.0f;
}

static void pvp_env_accumulate_terminal_log(Env* env) {
    if (env->tag > 0 && env->tag <= 8) {
        int b = env->tag - 1;
        float win = env->pvp.log.wins;
        env->log.hist_score_bank[b] += win;
        env->log.hist_n_bank[b] += 1.0f;
    }
    env->boundary_reached = 1;
    env->log.episode_return += env->pvp.log.episode_return;
    env->log.episode_length += env->pvp.log.episode_length;
    env->log.wins += env->pvp.log.wins;
    env->log.draws += env->pvp.log.draws;
    env->log.damage_dealt += env->pvp.log.damage_dealt;
    env->log.damage_received += env->pvp.log.damage_received;
    env->log.expected_damage_dealt += env->pvp.log.expected_damage_dealt;
    env->log.expected_damage_received += env->pvp.log.expected_damage_received;
    env->log.expected_damage_prevented += env->pvp.log.expected_damage_prevented;
    env->log.expected_damage_diff += env->pvp.log.expected_damage_diff;
    env->log.expected_damage_score += env->pvp.log.expected_damage_score;
    env->log.ko_supply_score += env->pvp.log.ko_supply_score;
    env->log.performance_score += env->pvp.log.performance_score;
    env->log.prayer_correct += env->pvp.log.prayer_correct;
    env->log.prayer_total += env->pvp.log.prayer_total;
    env->log.food_remaining += env->pvp.log.food_remaining;
    env->log.karambwan_remaining += env->pvp.log.karambwan_remaining;
    env->log.brews_remaining += env->pvp.log.brews_remaining;
    env->log.spec_energy_remaining += env->pvp.log.spec_energy_remaining;
    env->log.attacks_landed += env->pvp.log.attacks_landed;
    env->log.off_prayer_hits += env->pvp.log.off_prayer_hits;
    env->log.equip_click_attempts += env->pvp.log.equip_click_attempts;
    env->log.equip_click_noop_rate += env->pvp.log.equip_click_noop_rate;
    env->log.special_arm_attempts += env->pvp.log.special_arm_attempts;
    env->log.special_arm_noop_rate += env->pvp.log.special_arm_noop_rate;
    env->log.target_click_attempts += env->pvp.log.target_click_attempts;
    env->log.target_click_no_fire_rate += env->pvp.log.target_click_no_fire_rate;
    env->log.spell_attack_attempts += env->pvp.log.spell_attack_attempts;
    env->log.spell_attack_no_fire_rate += env->pvp.log.spell_attack_no_fire_rate;
    env->log.weapon_attack_rate += env->pvp.log.weapon_attack_rate;
    env->log.melee_attack_rate += env->pvp.log.melee_attack_rate;
    env->log.ranged_attack_rate += env->pvp.log.ranged_attack_rate;
    env->log.magic_attack_rate += env->pvp.log.magic_attack_rate;
    env->log.attack_after_equip_rate += env->pvp.log.attack_after_equip_rate;
    env->log.spec_after_equip_rate += env->pvp.log.spec_after_equip_rate;
    env->log.n += env->pvp.log.n;
}

void c_step(Env* env) {
    int pvp_prof_enabled = PVP_PROFILE_ENABLED();
    double pvp_prof_start = pvp_prof_enabled ? PVP_PROFILE_NOW_MS() : 0.0;
    double pvp_prof_t0 = pvp_prof_start;

#ifdef OSRS_VISUAL
    RenderClient* rc = (RenderClient*)env->pvp.client;
    int used_human_commands = 0;

    if (rc && rc->human_input.enabled && ENCOUNTER_NH_PVP.step_human_commands) {
        ENCOUNTER_NH_PVP.step_human_commands(
            (EncounterState*)&env->pvp, NULL, &rc->human_input);
        used_human_commands = 1;
    }
#else
    int used_human_commands = 0;
#endif

    if (!used_human_commands) {
        /* slot 0 actions: float → int into ocean_acts_staging.
           Read from action_ptr[0] which my_setup_perm wired to the perm-routed
           rollout row (identity perm = the env's natural slot 0). */
        float* p0_acts = env->action_ptr[0] ? env->action_ptr[0] : env->actions;
        for (int i = 0; i < NUM_ATNS; i++) {
            env->ocean_acts_staging[i] = (int)p0_acts[i];
        }
        PVP_PROFILE_MARK(PVP_PROF_ACTION_DECODE);
        if (env->scripted_opp_type >= 0) {
            env->pvp.pvp_runtime.opponent.type = (OpponentType)env->scripted_opp_type;
            env->pvp.pvp_runtime.use_external_opponent_actions = 0;
            env->pvp.pvp_runtime.use_c_opponent = 1;
        } else if (env->use_rollout_opponent && env->action_ptr[1]) {
            env->pvp.pvp_runtime.use_external_opponent_actions = 1;
            env->pvp.pvp_runtime.use_c_opponent = 0;
            for (int i = 0; i < NUM_ATNS; i++) {
                env->pvp.pvp_runtime.external_opponent_actions[i] =
                    (int)env->action_ptr[1][i];
            }
        }
        PVP_PROFILE_MARK(PVP_PROF_OPPONENT_ROUTE);
        pvp_step(&env->pvp);
        PVP_PROFILE_MARK(PVP_PROF_PVP_STEP);

        if (env->scripted_opp_type >= 0) {
            env->pvp._rews_buf[1] = 0.0f;
            env->pvp.step_rewards[1] = 0.0f;
        }
    }

    env->terminals[0] = (float)env->pvp.step_terminals[0];
    if (env->terminal_ptr[1]) {
        *env->terminal_ptr[1] = (float)env->pvp.step_terminals[1];
    }
    if (env->reward_ptr[0]) *env->reward_ptr[0] = env->pvp.step_rewards[0];
    if (env->reward_ptr[1]) *env->reward_ptr[1] = env->pvp.step_rewards[1];

    if (env->pvp.step_terminals[0]) {
        pvp_env_accumulate_terminal_log(env);
        memset(&env->pvp.log, 0, sizeof(env->pvp.log));
    }
    PVP_PROFILE_MARK(PVP_PROF_TERMINAL_LOG);

    if (env->pvp.step_terminals[0] && env->pvp.auto_reset) {
        ocean_write_obs(&env->pvp);
        if (env->pvp.ocean_io.agent_obs_p1) ocean_write_obs_p1(&env->pvp);
    }
    PVP_PROFILE_MARK(PVP_PROF_RESET_OBS);
    pvp_env_copy_action_masks_to_rollout(env);
    PVP_PROFILE_MARK(PVP_PROF_MASK_COPY);
    pvp_state_store(env, &env->state);
    PVP_PROFILE_MARK(PVP_PROF_STATE_STORE);
    if (pvp_prof_enabled) {
        PVP_PROFILE_ADD(PVP_PROF_C_STEP_TOTAL, PVP_PROFILE_NOW_MS() - pvp_prof_start);
    }
}

void c_reset(Env* env) {
    pvp_env_rewire_internal_buffers(env);
    pvp_env_rewire_rollout_buffers(env);

    pvp_reset(&env->pvp);
    ocean_write_obs(&env->pvp);
    if (env->pvp.ocean_io.agent_obs_p1) ocean_write_obs_p1(&env->pvp);
    pvp_env_copy_action_masks_to_rollout(env);
    env->pvp.ocean_io.agent_rewards[0] = 0.0f;
    env->pvp.ocean_io.agent_terminals[0] = 0;
    memset(env->pvp.step_rewards, 0, sizeof(env->pvp.step_rewards));
    memset(env->pvp.step_terminals, 0, sizeof(env->pvp.step_terminals));
    env->terminals[0] = 0.0f;
    if (env->terminal_ptr[1]) *env->terminal_ptr[1] = 0.0f;
    if (env->reward_ptr[1]) *env->reward_ptr[1] = 0.0f;
    env->boundary_reached = 0;
    pvp_state_store(env, &env->state);
}

void c_close(Env* env) { pvp_close(&env->pvp); }

#ifdef OSRS_VISUAL
static const double PVP_TERMINAL_DEATH_SECONDS = 1.2;
static const double PVP_TERMINAL_WINNER_SECONDS = 0.8;

static void pvp_render_frame(Env* env, RenderClient* rc) {
    render_post_tick(rc, &env->pvp);
    pvp_render(&env->pvp);
    rc->last_tick_time = GetTime();
    env->last_step_time = rc->last_tick_time;
}

static void pvp_render_terminal_presentation(Env* env, RenderClient* rc) {
    PvpTerminalPresentation* p = &env->pvp.pvp_runtime.terminal_presentation;
    if (p->phase == PVP_TERMINAL_PRESENTATION_INACTIVE) return;

    p->phase = PVP_TERMINAL_PRESENTATION_DEATH;
    render_reset_episode_visual_state(rc, &env->pvp);
    double start = GetTime();
    int winner_phase_started = 0;
    double total = PVP_TERMINAL_DEATH_SECONDS + PVP_TERMINAL_WINNER_SECONDS;

    while (GetTime() - start < total) {
        double elapsed = GetTime() - start;
        if (!winner_phase_started && elapsed >= PVP_TERMINAL_DEATH_SECONDS) {
            p->phase = PVP_TERMINAL_PRESENTATION_WINNER;
            render_reset_episode_visual_state(rc, &env->pvp);
            winner_phase_started = 1;
        }
        pvp_render_frame(env, rc);
    }

    pvp_terminal_presentation_clear(&env->pvp);
    pvp_state_store(env, &env->state);
    render_reset_episode_visual_state(rc, &env->pvp);
}

void c_render(Env* env) {
    env->pvp.encounter_def = (const void*)&ENCOUNTER_NH_PVP;
    env->pvp.encounter_state = (void*)&env->pvp;
    env->pvp.encounter_context = NULL;

    int first_call = env->pvp.client == NULL;
    if (first_call) {
        env->pvp.client = render_make_client_for_encounter(&ENCOUNTER_NH_PVP);
        RenderClient* rc = (RenderClient*)env->pvp.client;
        rc->ticks_per_second = env->ticks_per_second;
        EncounterSceneConfig scene = {
            .required_groups = {
                OSRS_ASSET_GROUP_PVP,
                OSRS_ASSET_GROUP_COMBAT_VISUALS,
                (OsrsAssetGroupKind)-1,
                (OsrsAssetGroupKind)-1,
            },
            .terrain_path = OSRS_ASSET("wilderness.terrain"),
            .objects_path = OSRS_ASSET("wilderness.objects"),
            .cmap_path = NULL,
        };
        encounter_load_scene_assets(rc, &scene);
        CollisionMap* cmap = pvp_shared_wilderness_collision_map();
        rc->collision_map = cmap;
        rc->collision_world_offset_x = 0;
        rc->collision_world_offset_y = 0;
        render_set_world_bounds(rc, WILD_MIN_X, WILD_MIN_Y, WILD_MAX_X, WILD_MAX_Y);
        rc->show_arena_boundary = 0;
        env->pvp.collision_map = cmap;
        env->last_step_time = GetTime();
    }

    RenderClient* rc = (RenderClient*)env->pvp.client;
    if (!rc) return;
    rc->show_arena_boundary = 0;

    if (pvp_terminal_presentation_active(&env->pvp)) {
        pvp_render_terminal_presentation(env, rc);
        return;
    }

    render_post_tick(rc, &env->pvp);

    if (rc->ticks_per_second <= 0.0f) {
        pvp_render(&env->pvp);
        rc->last_tick_time = GetTime();
        env->last_step_time = rc->last_tick_time;
        return;
    }

    float tps = render_effective_ticks_per_second(rc);
    double interval = 1.0 / (double)tps;
    double deadline = env->last_step_time + interval;
    int rendered = 0;
    while (GetTime() < deadline) {
        pvp_render(&env->pvp);
        rendered = 1;
    }
    if (!rendered) pvp_render(&env->pvp);

    rc->last_tick_time = GetTime();
    env->last_step_time = rc->last_tick_time;
}
#else
void c_render(Env* env) { (void)env; }
#endif

#include "vecenv.h"

void my_setup_perm(StaticVec* vec, Env* env, int slot_base) {
    int n = env->num_agents;
    if (n > 2) n = 2;
    size_t obs_elem = obs_element_size();
    for (int s = 0; s < n; s++) {
        int phys = vec->agent_perm ? vec->agent_perm[slot_base + s] : (slot_base + s);
        env->obs_ptr[s]      = (char*)vec->observations.data + (size_t)phys * OBS_SIZE * obs_elem;
        env->action_ptr[s]   = vec->actions   + (size_t)phys * NUM_ATNS;
        env->reward_ptr[s]   = vec->rewards   + phys;
        env->terminal_ptr[s] = vec->terminals + phys;
        env->action_mask_ptr[s] = vec->action_mask + (size_t)phys * MY_ACTION_MASK;
    }
    for (int s = n; s < 2; s++) {
        env->obs_ptr[s] = NULL;
        env->action_ptr[s] = NULL;
        env->reward_ptr[s] = NULL;
        env->terminal_ptr[s] = NULL;
        env->action_mask_ptr[s] = NULL;
    }
    env->observations = env->obs_ptr[0];
    env->actions      = env->action_ptr[0];
    env->rewards      = env->reward_ptr[0];
    env->terminals    = env->terminal_ptr[0];
    env->action_mask  = env->action_mask_ptr[0];
    env->pvp.ocean_io.agent_obs    = (float*)env->obs_ptr[0];
    if (n >= 2) {
        env->pvp.ocean_io.agent_obs_p1 = (float*)env->obs_ptr[1];
    } else {
        env->pvp.ocean_io.agent_obs_p1 = NULL;
    }
}

void my_init(Env* env, Dict* kwargs) {
    DictItem* use_roll_opp_kw = dict_get_unsafe(kwargs, "use_rollout_opponent");
    int rollout_opponent = use_roll_opp_kw ? (int)use_roll_opp_kw->value : 0;
    env->num_agents = rollout_opponent ? 2 : 1;
    env->ticks_per_second = 1.667f;
    env->last_step_time = 0.0;
    env->tag = 0;
    env->boundary_reached = 0;

    pvp_init(&env->pvp);
    pvp_env_rewire_internal_buffers(env);
    env->pvp.collision_map = pvp_shared_wilderness_collision_map();
    DictItem* seed_kw = dict_get_unsafe(kwargs, "seed");
    if (!seed_kw) {
        fprintf(stderr, "osrs_pvp env.seed is required for deterministic native runs\n");
        abort();
    }
    int seed = (int)seed_kw->value;
    if (seed <= 0) {
        fprintf(stderr, "osrs_pvp env.seed must be positive\n");
        abort();
    }
    env->pvp.rng_seed = (uint32_t)seed + 9973u * (uint32_t)env->rng;
    env->pvp.rng_reset_count = 0;
    env->pvp.has_rng_seed = 1;

    DictItem* fixed_spawns = dict_get_unsafe(kwargs, "fixed_spawns");
    if (fixed_spawns) {
        env->pvp.pvp_runtime.start_mode =
            pvp_start_mode_from_fixed_spawns((int)fixed_spawns->value);
    }

    env->pvp.ocean_io.agent_obs = NULL;
    env->pvp.ocean_io.agent_rewards = env->pvp._rews_buf;
    env->pvp.ocean_io.agent_obs_p1 = NULL;
    env->pvp.ocean_io.selfplay_mask = NULL;

    env->pvp.pvp_runtime.use_c_opponent = 1;
    env->pvp.auto_reset = 1;
    env->pvp.is_lms = 1;

    DictItem* opp = dict_get_unsafe(kwargs, "opponent_type");
    env->pvp.pvp_runtime.opponent.type = opp ? (OpponentType)(int)opp->value : OPP_IMPROVED;

    /* use_rollout_opponent was read above to decide num_agents. Mirror it
       into pvp_runtime so pvp_step reads p1 actions from
       external_opponent_actions (filled in c_step from action_ptr[1]). */
    env->use_rollout_opponent = rollout_opponent;
    env->pvp.pvp_runtime.use_external_opponent_actions = rollout_opponent;
    if (rollout_opponent) {
        env->pvp.pvp_runtime.use_c_opponent = 0;
    }

    /* Scripted opponent override starts unset. selfplay.setup can set this
       per-env via pufferl_set_env_scripted_opps after vec creation. */
    env->scripted_opp_type = -1;

    DictItem* shaping_scale = dict_get_unsafe(kwargs, "shaping_scale");
    env->pvp.shaping.shaping_scale = shaping_scale ? (float)shaping_scale->value : 0.0f;

    DictItem* shaping_en = dict_get_unsafe(kwargs, "shaping_enabled");
    env->pvp.shaping.enabled = shaping_en ? (int)shaping_en->value : 0;

    DictItem* expected_damage_reward = dict_get_unsafe(
        kwargs, "expected_damage_reward_coef");
    env->pvp.shaping.expected_damage_reward_coef = expected_damage_reward
        ? (float)expected_damage_reward->value : 0.0f;

    DictItem* incoming_damage_avoidance_reward = dict_get_unsafe(
        kwargs, "incoming_damage_avoidance_reward_coef");
    env->pvp.shaping.incoming_damage_avoidance_reward_coef =
        incoming_damage_avoidance_reward
        ? (float)incoming_damage_avoidance_reward->value : 0.0f;

    DictItem* ko_supply_reward = dict_get_unsafe(kwargs, "ko_supply_reward_coef");
    env->pvp.shaping.ko_supply_reward_coef = ko_supply_reward
        ? (float)ko_supply_reward->value : 0.0f;

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
    DictItem* ko_sup = dict_get_unsafe(kwargs, "ko_supplies_bonus_coef");
    env->pvp.shaping.ko_supplies_bonus_coef = ko_sup ? (float)ko_sup->value : 0.0f;
    env->pvp.shaping.prayer_penalty_enabled = 1;
    env->pvp.shaping.click_penalty_enabled = 0;
    env->pvp.shaping.click_penalty_threshold = 5;
    env->pvp.shaping.click_penalty_coef = -0.003f;

    DictItem* gear_tier = dict_get_unsafe(kwargs, "gear_tier");
    pvp_env_set_gear_tier(env, gear_tier ? (int)gear_tier->value : 3);

    pvp_reset(&env->pvp);
    pvp_state_store(env, &env->state);
}

void my_log(Log* log, Dict* out) {
    dict_set(out, "action_schema_id", (float)PVP_ACTION_SCHEMA);
    dict_set(out, "obs_schema_id", (float)PVP_OBS_SCHEMA);
    dict_set(out, "mask_schema_id", (float)PVP_ACTION_SCHEMA);
    dict_set(out, "episode_return", log->episode_return);
    dict_set(out, "episode_length", log->episode_length);
    dict_set(out, "wins", log->wins);
    dict_set(out, "slot_0_score", log->wins);
    dict_set(out, "slot_1_score", 1.0f - log->wins - log->draws);
    dict_set(out, "draw_rate", log->draws);
    dict_set(out, "damage_dealt", log->damage_dealt);
    dict_set(out, "damage_received", log->damage_received);
    dict_set(out, "expected_damage_dealt", log->expected_damage_dealt);
    dict_set(out, "expected_damage_received", log->expected_damage_received);
    dict_set(out, "expected_damage_prevented", log->expected_damage_prevented);
    dict_set(out, "expected_damage_diff", log->expected_damage_diff);
    dict_set(out, "expected_damage_score", log->expected_damage_score);
    dict_set(out, "ko_supply_score", log->ko_supply_score);
    dict_set(out, "performance_score", log->performance_score);

    float prayer_rate = (log->prayer_total > 0.0f)
        ? log->prayer_correct / log->prayer_total : 0.0f;
    dict_set(out, "prayer_correct_rate", prayer_rate);

    dict_set(out, "food_remaining", log->food_remaining);
    dict_set(out, "karambwan_remaining", log->karambwan_remaining);
    dict_set(out, "brews_remaining", log->brews_remaining);
    dict_set(out, "spec_remaining", log->spec_energy_remaining);
    dict_set(out, "attacks_landed", log->attacks_landed);
    dict_set(out, "off_prayer_hits", log->off_prayer_hits);
    dict_set(out, "equip_click_attempts", log->equip_click_attempts);
    dict_set(out, "equip_click_noop_rate", log->equip_click_noop_rate);
    dict_set(out, "special_arm_attempts", log->special_arm_attempts);
    dict_set(out, "special_arm_noop_rate", log->special_arm_noop_rate);
    dict_set(out, "target_click_attempts", log->target_click_attempts);
    dict_set(out, "target_click_no_fire_rate", log->target_click_no_fire_rate);
    dict_set(out, "spell_attack_attempts", log->spell_attack_attempts);
    dict_set(out, "spell_attack_no_fire_rate", log->spell_attack_no_fire_rate);
    dict_set(out, "weapon_attack_rate", log->weapon_attack_rate);
    dict_set(out, "melee_attack_rate", log->melee_attack_rate);
    dict_set(out, "ranged_attack_rate", log->ranged_attack_rate);
    dict_set(out, "magic_attack_rate", log->magic_attack_rate);
    dict_set(out, "attack_after_equip_rate", log->attack_after_equip_rate);
    dict_set(out, "spec_after_equip_rate", log->spec_after_equip_rate);

    /* Per-bank PFSP stats. Keys MUST be literal strings — dict_set stores the
       key pointer, so a stack-formatted "hist_score_bank_%d" would alias to
       the same address across all iterations. selfplay.step() reads these. */
    dict_set(out, "hist_score_bank_0", log->hist_score_bank[0]);
    dict_set(out, "hist_score_bank_1", log->hist_score_bank[1]);
    dict_set(out, "hist_score_bank_2", log->hist_score_bank[2]);
    dict_set(out, "hist_score_bank_3", log->hist_score_bank[3]);
    dict_set(out, "hist_score_bank_4", log->hist_score_bank[4]);
    dict_set(out, "hist_score_bank_5", log->hist_score_bank[5]);
    dict_set(out, "hist_score_bank_6", log->hist_score_bank[6]);
    dict_set(out, "hist_score_bank_7", log->hist_score_bank[7]);
    dict_set(out, "hist_n_bank_0", log->hist_n_bank[0]);
    dict_set(out, "hist_n_bank_1", log->hist_n_bank[1]);
    dict_set(out, "hist_n_bank_2", log->hist_n_bank[2]);
    dict_set(out, "hist_n_bank_3", log->hist_n_bank[3]);
    dict_set(out, "hist_n_bank_4", log->hist_n_bank[4]);
    dict_set(out, "hist_n_bank_5", log->hist_n_bank[5]);
    dict_set(out, "hist_n_bank_6", log->hist_n_bank[6]);
    dict_set(out, "hist_n_bank_7", log->hist_n_bank[7]);

    float dph = (log->attacks_landed > 0.0f)
        ? log->damage_dealt / log->attacks_landed : 0.0f;
    dict_set(out, "damage_per_hit", dph);

    float wr = log->wins;
    float dmg_dealt_norm = log->damage_dealt;
    float dmg_recv_norm  = log->damage_received;
    float dmg_diff = dmg_dealt_norm - dmg_recv_norm;
    float dmg_diff_score = 0.5f + 0.25f * dmg_diff;
    if (dmg_diff_score < 0.0f) dmg_diff_score = 0.0f;
    if (dmg_diff_score > 1.0f) dmg_diff_score = 1.0f;

    float score = 0.7f * wr + 0.3f * dmg_diff_score;
    dict_set(out, "score", score);
    dict_set(out, "dmg_diff_score", dmg_diff_score);
    if (PVP_PROFILE_ENABLED()) {
        static const char* keys[PVP_PROF_COUNT] = {
            "profile_pvp_c_step_total_ms",
            "profile_pvp_action_decode_ms",
            "profile_pvp_opponent_route_ms",
            "profile_pvp_pvp_step_ms",
            "profile_pvp_terminal_log_ms",
            "profile_pvp_reset_obs_ms",
            "profile_pvp_mask_copy_ms",
            "profile_pvp_state_store_ms",
            "profile_pvp_api_total_ms",
            "profile_pvp_api_clear_flags_ms",
            "profile_pvp_api_action_copy_ms",
            "profile_pvp_api_c_opponent_ms",
            "profile_pvp_api_switches_ms",
            "profile_pvp_api_movement_ms",
            "profile_pvp_api_combat_ms",
            "profile_pvp_api_pending_hits_ms",
            "profile_pvp_api_reward_terminal_ms",
            "profile_pvp_api_obs_mask_ms",
            "profile_pvp_api_obs_generate_ms",
            "profile_pvp_api_ocean_write_ms",
            "profile_pvp_api_terminal_scoring_ms",
            "profile_pvp_api_auto_reset_ms",
        };
        for (int i = 0; i < PVP_PROF_COUNT; i++) {
            dict_set(out, keys[i], pvp_profile_read_reset_ms(i));
        }
    }
}

#ifdef __cplusplus
extern "C" {
#endif

void binding_set_pfsp_weights(StaticVec* vec, int* pool, int* cum_weights, int pool_size) {
    Env* envs = (Env*)vec->envs;
    if (pool_size > MAX_OPPONENT_POOL) pool_size = MAX_OPPONENT_POOL;
    for (int e = 0; e < vec->size; e++) {
        int was_unconfigured = (envs[e].pvp.pvp_runtime.pfsp.pool_size == 0);
        envs[e].pvp.pvp_runtime.pfsp.pool_size = pool_size;
        for (int i = 0; i < pool_size; i++) {
            envs[e].pvp.pvp_runtime.pfsp.pool[i] = (OpponentType)pool[i];
            envs[e].pvp.pvp_runtime.pfsp.cum_weights[i] = cum_weights[i];
        }
        if (was_unconfigured) {
            c_reset(&envs[e]);
        }
    }
}

void binding_get_pfsp_stats(StaticVec* vec, float* out_wins, float* out_episodes, int* out_pool_size) {
    Env* envs = (Env*)vec->envs;
    int pool_size = 0;

    for (int e = 0; e < vec->size; e++) {
        if (envs[e].pvp.pvp_runtime.pfsp.pool_size > pool_size)
            pool_size = envs[e].pvp.pvp_runtime.pfsp.pool_size;
    }
    *out_pool_size = pool_size;
    for (int i = 0; i < pool_size; i++) {
        out_wins[i] = 0.0f;
        out_episodes[i] = 0.0f;
    }

    for (int e = 0; e < vec->size; e++) {
        for (int i = 0; i < envs[e].pvp.pvp_runtime.pfsp.pool_size; i++) {
            out_wins[i] += envs[e].pvp.pvp_runtime.pfsp.wins[i];
            out_episodes[i] += envs[e].pvp.pvp_runtime.pfsp.episodes[i];
        }
        memset(envs[e].pvp.pvp_runtime.pfsp.wins, 0, sizeof(envs[e].pvp.pvp_runtime.pfsp.wins));
        memset(envs[e].pvp.pvp_runtime.pfsp.episodes, 0, sizeof(envs[e].pvp.pvp_runtime.pfsp.episodes));
    }
}

#ifdef __cplusplus
}
#endif
