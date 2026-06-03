/**
 * @file binding.c
 * @brief Static-native binding for OSRS PVP environment
 *
 * Bridges vecenv.h's contract (float actions, float terminals) with the PVP
 * env's internal types (int actions, unsigned char terminals) using a wrapper
 * struct. PVP source headers are untouched.
 */

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
    OsrsEnv pvp;
    int ocean_acts_staging[NUM_ACTION_HEADS];
    int ocean_acts_staging_p1[NUM_ACTION_HEADS];
    unsigned char ocean_term_staging;
} PvpStateSnapshot;

typedef PvpStateSnapshot State;

/* vecenv-compatible header fields must stay first. */
typedef struct {
    void* observations;
    float* actions;
    float* rewards;
    float* terminals;
    int num_agents;
    int rng;
    Log log;

    OsrsEnv pvp;
    PvpStateSnapshot state;

    int ocean_acts_staging[NUM_ACTION_HEADS];
    int ocean_acts_staging_p1[NUM_ACTION_HEADS];  /* slot 1's int actions, fed to external_opponent_actions */
    unsigned char ocean_term_staging;

    float ticks_per_second;
    double last_step_time;

    /* Self-play env-side opt-in (see vecenv.h MY_USES_TAGS / MY_USES_PERM).
       tag: 0 = primary selfplay, >=1 = playing frozen bank (tag-1). Used to
       attribute episode outcomes to the correct bank's hist_score_bank entry.
       boundary_reached: set on episode terminal so selfplay.step() knows
       this env finished its current matchup and can be counted into the swap
       decision via count_aligned. */
    int tag;
    int boundary_reached;

    /* Per-slot pointer arrays for num_agents=2 self-play opt-in (MY_USES_PERM).
       Routed by my_setup_perm using vec->agent_perm. Slot 0 = learner, slot 1
       = opponent (driven by primary policy in pure-selfplay envs, by frozen
       bank in historical envs, by C-heuristic in scripted-opp envs). */
    void* obs_ptr[2];
    float* action_ptr[2];
    float* reward_ptr[2];
    float* terminal_ptr[2];

    /* When 1, p1 actions come from the rollout (action_ptr[1]); when 0, the
       legacy C-heuristic opponent path is used (use_c_opponent driven).
       selfplay setups should flip this on via env kwargs. */
    int use_rollout_opponent;

    /* Per-env scripted opponent override. >= 0 = OpponentType to use for slot
       1 (C-heuristic). -1 = use whatever the global mode dictates (rollout if
       use_rollout_opponent else default opponent_type). Set by selfplay.setup
       via pufferl_set_env_scripted_opps to distribute envs across pure
       self-play (-1), frozen-bank (-1), and scripted-opp (>= 0) curricula. */
    int scripted_opp_type;
} PvpEnv;

#define OBS_SIZE OCEAN_OBS_SIZE
#define NUM_ATNS NUM_ACTION_HEADS
#define ACT_SIZES {LOADOUT_DIM, COMBAT_DIM, OVERHEAD_DIM, FOOD_DIM, POTION_DIM, KARAMBWAN_DIM, VENG_DIM, OFFENSIVE_DIM, MOVE_DIM}
#define OBS_TENSOR_T FloatTensor
#define Env PvpEnv
#define MY_USES_TAGS
#define MY_USES_PERM
#define MY_USES_SCRIPTED_OPPS
/* PvP uses obs-embedded action mask (rollout splitter handles via has_mask),
   not the separate MY_ACTION_MASK buffer path. */

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

static void pvp_state_store(Env* env, PvpStateSnapshot* out) {
    out->pvp = env->pvp;
    memcpy(out->ocean_acts_staging, env->ocean_acts_staging, sizeof(out->ocean_acts_staging));
    memcpy(out->ocean_acts_staging_p1, env->ocean_acts_staging_p1, sizeof(out->ocean_acts_staging_p1));
    out->ocean_term_staging = env->ocean_term_staging;
}

static void pvp_state_load(Env* env, const PvpStateSnapshot* in) {
    void* collision_map = env->pvp.collision_map;
    void* client = env->pvp.client;
    const void* encounter_def = env->pvp.encounter_def;
    void* encounter_state = env->pvp.encounter_state;
    void* encounter_context = env->pvp.encounter_context;
    env->pvp = in->pvp;
    memcpy(env->ocean_acts_staging, in->ocean_acts_staging, sizeof(env->ocean_acts_staging));
    memcpy(env->ocean_acts_staging_p1, in->ocean_acts_staging_p1, sizeof(env->ocean_acts_staging_p1));
    env->ocean_term_staging = in->ocean_term_staging;
    pvp_env_rewire_after_load(env, collision_map, client,
        encounter_def, encounter_state, encounter_context);
}

static void puffer_state_refresh(Env* env) {
    pvp_state_load(env, &env->state);
    ocean_write_obs(&env->pvp);
    if (env->pvp.ocean_io.agent_obs_p1) ocean_write_obs_p1(&env->pvp);
    env->pvp.ocean_io.agent_rewards[0] = 0.0f;
    env->pvp.ocean_io.agent_terminals[0] = 0;
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

void c_step(Env* env) {
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
        /* Slot 1 routing — three modes:
             scripted_opp_type >= 0 → C-heuristic of that specific type drives p1
             use_rollout_opponent && action_ptr[1] → rollout drives p1 (pure selfplay)
             else → legacy default opponent_type C-heuristic */
        if (env->scripted_opp_type >= 0) {
            env->pvp.pvp_runtime.opponent.type = (OpponentType)env->scripted_opp_type;
            env->pvp.pvp_runtime.use_external_opponent_actions = 0;
            env->pvp.pvp_runtime.use_c_opponent = 1;
        } else if (env->use_rollout_opponent && env->action_ptr[1]) {
            env->pvp.pvp_runtime.use_external_opponent_actions = 1;
            env->pvp.pvp_runtime.use_c_opponent = 0;
            for (int i = 0; i < NUM_ATNS; i++) {
                env->ocean_acts_staging_p1[i] = (int)env->action_ptr[1][i];
                env->pvp.pvp_runtime.external_opponent_actions[i] = env->ocean_acts_staging_p1[i];
            }
        }
        pvp_step(&env->pvp);

        /* For scripted-opp envs, slot 1's reward reflects what the C-heuristic
           did, not what the policy's slot 1 logits chose. Zero it to suppress
           that noisy training signal — the policy only trains on slot 0
           (learner) in scripted-opp envs. */
        if (env->scripted_opp_type >= 0) {
            env->pvp._rews_buf[1] = 0.0f;
        }
    }

    /* Terminals: mirror to both slots. pvp_step writes the shared episode_over
       to env->pvp.terminals[0..1]; copy each cell into its rollout slot. */
    env->terminals[0] = (float)env->ocean_term_staging;
    if (env->terminal_ptr[1]) {
        *env->terminal_ptr[1] = (float)env->ocean_term_staging;
    }
    /* Rewards: p0 from pvp internal _rews_buf[0], p1 from _rews_buf[1]. */
    if (env->reward_ptr[0]) *env->reward_ptr[0] = env->pvp._rews_buf[0];
    if (env->reward_ptr[1]) *env->reward_ptr[1] = env->pvp._rews_buf[1];

    if (env->ocean_term_staging) {
        /* Self-play per-bank attribution. When env->tag > 0, this env is the
           learner playing against frozen bank (tag - 1). Accumulate the win
           (1.0) or loss (0.0) and game count so selfplay.step() sees this
           bank's winrate via my_log -> hist_score_bank_<tag-1>. Also set
           boundary_reached for static_vec_count_aligned. */
        if (env->tag > 0 && env->tag <= 8) {
            int b = env->tag - 1;
            float win = env->pvp.log.wins;  /* 1.0 if learner won, 0.0 otherwise */
            env->log.hist_score_bank[b] += win;
            env->log.hist_n_bank[b] += 1.0f;
        }
        env->boundary_reached = 1;
        env->log.episode_return += env->pvp.log.episode_return;
        env->log.episode_length += env->pvp.log.episode_length;
        env->log.wins += env->pvp.log.wins;
        env->log.damage_dealt += env->pvp.log.damage_dealt;
        env->log.damage_received += env->pvp.log.damage_received;
        env->log.prayer_correct += env->pvp.log.prayer_correct;
        env->log.prayer_total += env->pvp.log.prayer_total;
        env->log.food_remaining += env->pvp.log.food_remaining;
        env->log.karambwan_remaining += env->pvp.log.karambwan_remaining;
        env->log.brews_remaining += env->pvp.log.brews_remaining;
        env->log.spec_energy_remaining += env->pvp.log.spec_energy_remaining;
        env->log.attacks_landed += env->pvp.log.attacks_landed;
        env->log.off_prayer_hits += env->pvp.log.off_prayer_hits;
        env->log.n += env->pvp.log.n;
        memset(&env->pvp.log, 0, sizeof(env->pvp.log));
    }

    if (env->ocean_term_staging && env->pvp.auto_reset) {
        ocean_write_obs(&env->pvp);
    }
    pvp_state_store(env, &env->state);
}

void c_reset(Env* env) {
    pvp_env_rewire_internal_buffers(env);
    pvp_env_rewire_rollout_buffers(env);

    pvp_reset(&env->pvp);
    ocean_write_obs(&env->pvp);
    if (env->pvp.ocean_io.agent_obs_p1) ocean_write_obs_p1(&env->pvp);
    env->pvp.ocean_io.agent_rewards[0] = 0.0f;
    env->pvp.ocean_io.agent_terminals[0] = 0;
    env->terminals[0] = 0.0f;
    if (env->terminal_ptr[1]) *env->terminal_ptr[1] = 0.0f;
    if (env->reward_ptr[1]) *env->reward_ptr[1] = 0.0f;
    env->boundary_reached = 0;
    pvp_state_store(env, &env->state);
}

void c_close(Env* env) { pvp_close(&env->pvp); }

#ifdef OSRS_VISUAL
void c_render(Env* env) {
    env->pvp.encounter_def = (const void*)&ENCOUNTER_NH_PVP;
    env->pvp.encounter_state = (void*)&env->pvp;
    env->pvp.encounter_context = NULL;

    int first_call = env->pvp.client == NULL;
    if (first_call) {
        env->pvp.client = render_make_client();
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
            .cmap_path = OSRS_ASSET("wilderness.cmap"),
        };
        CollisionMap* cmap = encounter_load_scene_assets(rc, &scene);
        if (cmap) env->pvp.collision_map = cmap;
        env->last_step_time = GetTime();
    }

    RenderClient* rc = (RenderClient*)env->pvp.client;
    if (!rc) return;

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

/* Self-play opt-in: route both rollout slots' obs/action/reward/terminal
   pointers through vec->agent_perm (identity when NULL). vecenv calls this
   from create_static_vec (init) and from static_vec_set_perm (when selfplay
   reroutes envs to frozen-bank slots). Both ocean_io.agent_obs and
   agent_obs_p1 also get rewired so pvp_step's ocean_write_obs / _p1 land in
   the rollout buffer rather than internal scratch. */
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
    }
    /* Keep header fields pointing at slot 0 so vecenv reset/log scanning still
       finds the expected per-env reward/terminal cell. */
    env->observations = env->obs_ptr[0];
    env->actions      = env->action_ptr[0];
    env->rewards      = env->reward_ptr[0];
    env->terminals    = env->terminal_ptr[0];
    /* Wire pvp_step's observation writes to land in the rollout buffer. */
    env->pvp.ocean_io.agent_obs    = (float*)env->obs_ptr[0];
    if (n >= 2) {
        env->pvp.ocean_io.agent_obs_p1 = (float*)env->obs_ptr[1];
    }
}

void my_init(Env* env, Dict* kwargs) {
    /* num_agents is determined at init by the use_rollout_opponent kwarg.
       When 1, both players are rollout-driven (self-play). When 0 (default),
       only slot 0 is exposed and the C-heuristic opponent drives slot 1
       internally, preserving OPP_IMPROVED backward compat. */
    DictItem* use_roll_opp_kw = dict_get_unsafe(kwargs, "use_rollout_opponent");
    int rollout_opponent = use_roll_opp_kw ? (int)use_roll_opp_kw->value : 0;
    env->num_agents = rollout_opponent ? 2 : 1;
    env->ticks_per_second = 1.667f;
    env->last_step_time = 0.0;
    env->tag = 0;
    env->boundary_reached = 0;

    pvp_init(&env->pvp);
    pvp_env_rewire_internal_buffers(env);
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
    /* ko_supplies_bonus_coef: proportional bonus when KO'ing with opponent
       supplies remaining. Sweep over [0, ~0.5] to find if fast-KO incentive
       helps. Defaults to 0 so existing OPP_IMPROVED training is unchanged. */
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
    dict_set(out, "episode_return", log->episode_return);
    dict_set(out, "episode_length", log->episode_length);
    dict_set(out, "wins", log->wins);
    dict_set(out, "damage_dealt", log->damage_dealt);
    dict_set(out, "damage_received", log->damage_received);

    float prayer_rate = (log->prayer_total > 0.0f)
        ? log->prayer_correct / log->prayer_total : 0.0f;
    dict_set(out, "prayer_correct_rate", prayer_rate);

    dict_set(out, "food_remaining", log->food_remaining);
    dict_set(out, "karambwan_remaining", log->karambwan_remaining);
    dict_set(out, "brews_remaining", log->brews_remaining);
    dict_set(out, "spec_remaining", log->spec_energy_remaining);
    dict_set(out, "attacks_landed", log->attacks_landed);
    dict_set(out, "off_prayer_hits", log->off_prayer_hits);

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

    /* Score (fixed across all sweep trials — this is the Protein yardstick).
       Two components:
         - wins (0/1): the static "you KO'd them" signal, the only thing that
           truly matters in OSRS PvP.
         - damage_differential: dealt - received, normalized by 2 * base_hp
           (so range [-1, +1]) then shifted to [0, 1]. Captures "how decisively
           did you outpace them in the damage race" as a tiebreaker.

       Reward shaping (separate from score, swept per trial) controls the
       training-time incentives. Score stays fixed so Protein has a stable
       metric. */
    float wr = log->wins;
    float dmg_dealt_norm = log->damage_dealt / 99.0f;
    float dmg_recv_norm  = log->damage_received / 99.0f;
    float dmg_diff = dmg_dealt_norm - dmg_recv_norm;
    /* Map [-2, +2] linearly to [0, 1] (saturates beyond ±base_hp on each side). */
    float dmg_diff_score = 0.5f + 0.25f * dmg_diff;
    if (dmg_diff_score < 0.0f) dmg_diff_score = 0.0f;
    if (dmg_diff_score > 1.0f) dmg_diff_score = 1.0f;

    float score = 0.7f * wr + 0.3f * dmg_diff_score;
    dict_set(out, "score", score);
    dict_set(out, "dmg_diff_score", dmg_diff_score);
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
