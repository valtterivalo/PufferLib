/**
 * @file osrs_pvp_api.h
 * @brief Public API for OSRS PvP simulation
 *
 * Provides the public interface for:
 * - Player initialization
 * - Environment reset (pvp_reset)
 * - Environment step (pvp_step)
 * - Seeding for deterministic runs (pvp_seed)
 * - Cleanup (pvp_close)
 */

#ifndef OSRS_PVP_API_H
#define OSRS_PVP_API_H

#include "osrs_types.h"
#include "osrs_pvp_gear.h"
#include "osrs_pvp_combat.h"
#include "osrs_pvp_movement.h"
#include "osrs_pvp_observations.h"
#include "osrs_pvp_actions.h"
#include "osrs_pvp_opponents.h"

typedef enum {
    PVP_PROF_C_STEP_TOTAL,
    PVP_PROF_ACTION_DECODE,
    PVP_PROF_OPPONENT_ROUTE,
    PVP_PROF_PVP_STEP,
    PVP_PROF_TERMINAL_LOG,
    PVP_PROF_RESET_OBS,
    PVP_PROF_MASK_COPY,
    PVP_PROF_STATE_STORE,
    PVP_PROF_API_TOTAL,
    PVP_PROF_API_CLEAR_FLAGS,
    PVP_PROF_API_ACTION_COPY,
    PVP_PROF_API_C_OPPONENT,
    PVP_PROF_API_SWITCHES,
    PVP_PROF_API_MOVEMENT,
    PVP_PROF_API_COMBAT,
    PVP_PROF_API_PENDING_HITS,
    PVP_PROF_API_REWARD_TERMINAL,
    PVP_PROF_API_OBS_MASK,
    PVP_PROF_API_OBS_GENERATE,
    PVP_PROF_API_OCEAN_WRITE,
    PVP_PROF_API_TERMINAL_SCORING,
    PVP_PROF_API_AUTO_RESET,
    PVP_PROF_COUNT,
} PvpProfileSlot;

#ifndef OSRS_PVP_PROFILE_ENABLED
#define OSRS_PVP_PROFILE_ENABLED() 0
#define OSRS_PVP_PROFILE_NOW_MS() 0.0
#define OSRS_PVP_PROFILE_MARK(slot) ((void)0)
#define OSRS_PVP_PROFILE_ADD(slot, ms) ((void)0)
#endif

/**
 * Initialize a player with default pure build stats and gear.
 *
 * Sets up:
 * - Base stats (75 attack, 99 strength, etc.)
 * - Current stats equal to base
 * - Starting gear (mage setup)
 * - Consumables (food, brews, restores)
 * - All timers reset to 0
 * - Sequential mode equipment state
 *
 * @param p Player to initialize
 */
static void init_player(Player* p) {
    p->base_attack = MAXED_BASE_ATTACK;
    p->base_strength = MAXED_BASE_STRENGTH;
    p->base_defence = MAXED_BASE_DEFENCE;
    p->base_ranged = MAXED_BASE_RANGED;
    p->base_magic = MAXED_BASE_MAGIC;
    p->base_prayer = MAXED_BASE_PRAYER;
    p->base_hitpoints = MAXED_BASE_HITPOINTS;

    p->current_attack = p->base_attack;
    p->current_strength = p->base_strength;
    p->current_defence = p->base_defence;
    p->current_ranged = p->base_ranged;
    p->current_magic = p->base_magic;
    p->current_prayer = p->base_prayer;
    p->current_hitpoints = p->base_hitpoints;

    p->special_energy = 100;
    p->spec_regen_active = 0;
    p->spec_armed = 0;
    osrs_interaction_init(&p->interaction);
    osrs_item_effect_state_init(&p->item_effect_state);

    p->current_gear = GEAR_MAGE;
    p->visible_gear = GEAR_MAGE;

    p->food_count = MAXED_FOOD_COUNT;
    p->karambwan_count = MAXED_KARAMBWAN_COUNT;
    p->brew_doses = MAXED_BREW_DOSES;
    p->restore_doses = MAXED_RESTORE_DOSES;
    p->combat_potion_doses = MAXED_COMBAT_POTION_DOSES;
    p->ranged_potion_doses = MAXED_RANGED_POTION_DOSES;

    p->attack_timer = 0;
    p->attack_timer_uncapped = 0;
    p->has_attack_timer = 0;
    p->food_timer = 0;
    p->potion_timer = 0;
    p->karambwan_timer = 0;
    p->consumable_used_this_tick = 0;
    p->last_food_heal = 0;
    p->last_food_waste = 0;
    p->last_karambwan_heal = 0;
    p->last_karambwan_waste = 0;
    p->last_brew_heal = 0;
    p->last_brew_waste = 0;
    p->last_potion_type = 0;
    p->last_potion_was_waste = 0;

    p->frozen_ticks = 0;
    p->freeze_immunity_ticks = 0;

    p->veng_active = 0;
    p->veng_cooldown = 0;

    p->prayer = PRAYER_NONE;
    p->offensive_prayer = OFFENSIVE_PRAYER_NONE;
    p->fight_style = FIGHT_STYLE_ACCURATE;
    p->prayer_drain_counter = 0;
    p->morr_dot_remaining = 0;
    p->morr_dot_tick_counter = 0;

    p->x = 0;
    p->y = 0;
    p->dest_x = 0;
    p->dest_y = 0;
    p->is_moving = 0;
    p->is_running = 0;
    p->run_energy = OSRS_RUN_ENERGY_FULL;
    p->run_recovery_ticks = 0;
    p->last_obs_target_x = 0;
    p->last_obs_target_y = 0;

    p->just_attacked = 0;
    p->last_attack_style = ATTACK_STYLE_NONE;
    p->attack_weapon_this_tick = ITEM_NONE;
    p->attack_was_on_prayer = 0;
    p->last_attack_dx = 0;
    p->last_attack_dy = 0;
    p->last_attack_dist = 0;
    p->attack_intent_pre_move_dist = 0;
    p->attack_click_canceled = 0;
    p->attack_click_ready = 0;

    memset(p->pending_hits, 0, sizeof(p->pending_hits));
    p->num_pending_hits = 0;
    p->damage_applied_this_tick = 0;

    // Hit event tracking
    p->hit_landed_this_tick = 0;
    p->hit_was_successful = 0;
    p->hit_spell_type = 0;
    p->hit_damage = 0;
    p->hit_style = ATTACK_STYLE_NONE;
    p->hit_defender_prayer = PRAYER_NONE;
    p->hit_was_on_prayer = 0;
    p->hit_attacker_idx = -1;
    p->freeze_applied_this_tick = 0;

    p->last_target_health_percent = 0.0f;
    p->tick_damage_scale = 0.0f;
    p->damage_dealt_scale = 0.0f;
    p->damage_received_scale = 0.0f;
    p->expected_damage_dealt_tick = 0.0f;
    p->expected_damage_received_tick = 0.0f;
    p->expected_damage_prevented_tick = 0.0f;
    p->ko_chance_prob_tick = 0.0f;

    p->total_target_hit_count = 0;
    p->target_hit_melee_count = 0;
    p->target_hit_ranged_count = 0;
    p->target_hit_magic_count = 0;
    p->target_hit_off_prayer_count = 0;
    p->target_hit_correct_count = 0;

    p->total_target_pray_count = 0;
    p->target_pray_melee_count = 0;
    p->target_pray_ranged_count = 0;
    p->target_pray_magic_count = 0;
    p->target_pray_correct_count = 0;

    p->player_hit_melee_count = 0;
    p->player_hit_ranged_count = 0;
    p->player_hit_magic_count = 0;

    p->player_pray_melee_count = 0;
    p->player_pray_ranged_count = 0;
    p->player_pray_magic_count = 0;

    memset(p->recent_target_attack_styles, 0, sizeof(p->recent_target_attack_styles));
    memset(p->recent_player_attack_styles, 0, sizeof(p->recent_player_attack_styles));
    memset(p->recent_target_prayer_styles, 0, sizeof(p->recent_target_prayer_styles));
    memset(p->recent_player_prayer_styles, 0, sizeof(p->recent_player_prayer_styles));
    memset(p->recent_target_prayer_correct, 0, sizeof(p->recent_target_prayer_correct));
    memset(p->recent_target_hit_correct, 0, sizeof(p->recent_target_hit_correct));
    p->recent_target_attack_index = 0;
    p->recent_player_attack_index = 0;
    p->recent_target_prayer_index = 0;
    p->recent_player_prayer_index = 0;
    p->recent_target_prayer_correct_index = 0;
    p->recent_target_hit_correct_index = 0;

    p->target_magic_accuracy = -1;
    p->target_magic_strength = -1;
    p->target_ranged_accuracy = -1;
    p->target_ranged_strength = -1;
    p->target_melee_accuracy = -1;
    p->target_melee_strength = -1;
    p->target_magic_gear_magic_defence = -1;
    p->target_magic_gear_ranged_defence = -1;
    p->target_magic_gear_melee_defence = -1;
    p->target_ranged_gear_magic_defence = -1;
    p->target_ranged_gear_ranged_defence = -1;
    p->target_ranged_gear_melee_defence = -1;
    p->target_melee_gear_magic_defence = -1;
    p->target_melee_gear_ranged_defence = -1;
    p->target_melee_gear_melee_defence = -1;

    p->player_prayed_correct = 0;
    p->target_prayed_correct = 0;

    p->total_damage_dealt = 0;
    p->total_damage_received = 0;
    p->expected_damage_dealt = 0.0f;
    p->expected_damage_received = 0.0f;
    p->expected_damage_prevented = 0.0f;
    p->ko_chance_survival_prob = 1.0f;
    p->ko_chance_count = 0.0f;
    p->weapon_equipped_this_tick = 0;
    p->equip_click_attempts = 0;
    p->equip_click_successes = 0;
    p->special_arm_attempts = 0;
    p->special_arm_successes = 0;
    p->target_click_attempts = 0;
    p->target_click_successes = 0;
    p->spell_attack_attempts = 0;
    p->spell_attack_successes = 0;
    p->selected_melee_attack_attempts = 0;
    p->selected_ranged_attack_attempts = 0;
    p->selected_magic_attack_attempts = 0;
    p->target_click_chase_ticks = 0;
    p->explicit_move_ticks = 0;
    p->target_click_pre_move_dist_sum = 0;
    p->target_click_post_move_dist_sum = 0;
    p->target_click_success_pre_move_dist_sum = 0;
    p->target_click_success_post_move_dist_sum = 0;
    p->spell_attack_pre_move_dist_sum = 0;
    p->spell_attack_post_move_dist_sum = 0;
    p->spell_attack_success_pre_move_dist_sum = 0;
    p->spell_attack_success_post_move_dist_sum = 0;
    p->weapon_attack_successes = 0;
    p->melee_attack_successes = 0;
    p->ranged_attack_successes = 0;
    p->magic_attack_successes = 0;
    p->attack_after_equip_successes = 0;
    p->spec_after_equip_successes = 0;

    p->is_lunar_spellbook = 0;
    p->observed_target_lunar_spellbook = 0;
    p->has_blood_fury = 1;

    p->melee_spec_weapon = MELEE_SPEC_NONE;
    p->ranged_spec_weapon = RANGED_SPEC_NONE;
    p->magic_spec_weapon = MAGIC_SPEC_NONE;

    p->bolt_proc_damage = 0.2f;
    p->bolt_ignores_defense = 0;

    p->prev_hp_percent = 1.0f;  // Full HP at start
}

static int pvp_abs_int(int value) {
    return value < 0 ? -value : value;
}

static const CollisionMap* pvp_require_collision_map(OsrsEnv* env) {
    const CollisionMap* cmap = (const CollisionMap*)env->collision_map;
    if (cmap == NULL) {
        fprintf(stderr, "osrs_pvp: reset requires wilderness collision_map\n");
        abort();
    }
    return cmap;
}

static int pvp_spawn_tile_valid(
    const CollisionMap* cmap,
    int x,
    int y,
    int avoid_x,
    int avoid_y
) {
    if (x == avoid_x && y == avoid_y) return 0;
    return pvp_tile_walkable((void*)cmap, x, y);
}

static int pvp_find_nearest_walkable_spawn(
    const CollisionMap* cmap,
    int desired_x,
    int desired_y,
    int avoid_x,
    int avoid_y,
    int* out_x,
    int* out_y
) {
    int max_radius = max_int(FIGHT_AREA_WIDTH, FIGHT_AREA_HEIGHT);
    int min_x = FIGHT_AREA_BASE_X;
    int min_y = FIGHT_AREA_BASE_Y;
    int max_x = FIGHT_AREA_BASE_X + FIGHT_AREA_WIDTH;
    int max_y = FIGHT_AREA_BASE_Y + FIGHT_AREA_HEIGHT;

    for (int radius = 0; radius <= max_radius; radius++) {
        for (int dy = -radius; dy <= radius; dy++) {
            for (int dx = -radius; dx <= radius; dx++) {
                if (max_int(pvp_abs_int(dx), pvp_abs_int(dy)) != radius) {
                    continue;
                }
                int x = desired_x + dx;
                int y = desired_y + dy;
                if (x < min_x || x >= max_x || y < min_y || y >= max_y) {
                    continue;
                }
                if (!pvp_spawn_tile_valid(cmap, x, y, avoid_x, avoid_y)) {
                    continue;
                }
                *out_x = x;
                *out_y = y;
                return 1;
            }
        }
    }
    return 0;
}

#define PVP_MAX_RANDOM_SPAWN_TILES (FIGHT_AREA_WIDTH * FIGHT_AREA_HEIGHT)

typedef struct {
    const CollisionMap* cmap;
    int count;
    int x[PVP_MAX_RANDOM_SPAWN_TILES];
    int y[PVP_MAX_RANDOM_SPAWN_TILES];
} PvpRandomSpawnCache;

static PvpRandomSpawnCache PVP_RANDOM_SPAWN_CACHE = {0};

static void pvp_init_random_spawn_cache(const CollisionMap* cmap) {
    if (PVP_RANDOM_SPAWN_CACHE.cmap == cmap) return;

#ifdef _OPENMP
#pragma omp critical(pvp_random_spawn_cache)
#endif
    {
        if (PVP_RANDOM_SPAWN_CACHE.cmap != cmap) {
            PVP_RANDOM_SPAWN_CACHE.count = 0;
            for (int y = FIGHT_AREA_BASE_Y; y < FIGHT_AREA_BASE_Y + FIGHT_AREA_HEIGHT; y++) {
                for (int x = FIGHT_AREA_BASE_X; x < FIGHT_AREA_BASE_X + FIGHT_AREA_WIDTH; x++) {
                    if (!pvp_spawn_tile_valid(cmap, x, y, -1, -1)) continue;
                    int idx = PVP_RANDOM_SPAWN_CACHE.count;
                    if (idx >= PVP_MAX_RANDOM_SPAWN_TILES) {
                        fprintf(stderr, "osrs_pvp: random spawn cache overflow\n");
                        abort();
                    }
                    PVP_RANDOM_SPAWN_CACHE.x[idx] = x;
                    PVP_RANDOM_SPAWN_CACHE.y[idx] = y;
                    PVP_RANDOM_SPAWN_CACHE.count++;
                }
            }
            PVP_RANDOM_SPAWN_CACHE.cmap = cmap;
        }
    }
}

static inline int pvp_random_spawn_cache_tile_matches(
    int x,
    int y,
    int min_x,
    int min_y,
    int max_x,
    int max_y,
    int avoid_x,
    int avoid_y
) {
    if (x < min_x || x >= max_x || y < min_y || y >= max_y) return 0;
    return x != avoid_x || y != avoid_y;
}

static int pvp_find_random_walkable_spawn(
    OsrsEnv* env,
    const CollisionMap* cmap,
    int min_x,
    int min_y,
    int max_x,
    int max_y,
    int avoid_x,
    int avoid_y,
    int* out_x,
    int* out_y
) {
    pvp_init_random_spawn_cache(cmap);
    int count = 0;
    for (int i = 0; i < PVP_RANDOM_SPAWN_CACHE.count; i++) {
        count += pvp_random_spawn_cache_tile_matches(
            PVP_RANDOM_SPAWN_CACHE.x[i],
            PVP_RANDOM_SPAWN_CACHE.y[i],
            min_x,
            min_y,
            max_x,
            max_y,
            avoid_x,
            avoid_y);
    }
    if (count == 0) return 0;

    int pick = rand_int(env, count);
    for (int i = 0; i < PVP_RANDOM_SPAWN_CACHE.count; i++) {
        int x = PVP_RANDOM_SPAWN_CACHE.x[i];
        int y = PVP_RANDOM_SPAWN_CACHE.y[i];
        if (!pvp_random_spawn_cache_tile_matches(
                x, y, min_x, min_y, max_x, max_y, avoid_x, avoid_y)) {
            continue;
        }
        if (pick == 0) {
            *out_x = x;
            *out_y = y;
            return 1;
        }
        pick--;
    }
    return 0;
}

static void pvp_set_player_spawn(Player* p, int x, int y) {
    p->x = x;
    p->y = y;
    p->dest_x = x;
    p->dest_y = y;
    p->is_moving = 0;
}

static PvpStartMode pvp_start_mode_from_fixed_spawns(int fixed_spawns) {
    if (fixed_spawns == 0) return PVP_START_RANDOMIZED;
    if (fixed_spawns == 1) return PVP_START_FIXED_PAIR;
    fprintf(stderr, "osrs_pvp: fixed_spawns must be 0 or 1, got %d\n", fixed_spawns);
    abort();
}

static void set_fight_positions(OsrsEnv* env) {
    const CollisionMap* cmap = pvp_require_collision_map(env);
    int x0 = FIGHT_AREA_BASE_X;
    int y0 = FIGHT_AREA_BASE_Y;
    int x1 = FIGHT_AREA_BASE_X + FIGHT_NEARBY_RADIUS;
    int y1 = FIGHT_AREA_BASE_Y + 1;

    switch (env->pvp_runtime.start_mode) {
        case PVP_START_FIXED_PAIR:
            if (!pvp_find_nearest_walkable_spawn(cmap, x0, y0, -1, -1, &x0, &y0) ||
                    !pvp_find_nearest_walkable_spawn(cmap, x1, y1, x0, y0, &x1, &y1)) {
                fprintf(stderr, "osrs_pvp: no walkable fixed spawn pair in fight area\n");
                abort();
            }
            pvp_set_player_spawn(&env->players[0], x0, y0);
            pvp_set_player_spawn(&env->players[1], x1, y1);
            return;
        case PVP_START_RANDOMIZED:
            break;
        default:
            fprintf(stderr, "osrs_pvp: invalid start_mode %d\n", (int)env->pvp_runtime.start_mode);
            abort();
    }

    int base_x = FIGHT_AREA_BASE_X;
    int base_y = FIGHT_AREA_BASE_Y;
    int max_x = base_x + FIGHT_AREA_WIDTH;
    int max_y = base_y + FIGHT_AREA_HEIGHT;

    if (!pvp_find_random_walkable_spawn(
            env, cmap, base_x, base_y, max_x, max_y, -1, -1, &x0, &y0)) {
        fprintf(stderr, "osrs_pvp: no walkable player spawn in fight area\n");
        abort();
    }

    int near_min_x = max_int(base_x, x0 - FIGHT_NEARBY_RADIUS);
    int near_min_y = max_int(base_y, y0 - FIGHT_NEARBY_RADIUS);
    int near_max_x = min_int(max_x, x0 + FIGHT_NEARBY_RADIUS + 1);
    int near_max_y = min_int(max_y, y0 + FIGHT_NEARBY_RADIUS + 1);

    if (!pvp_find_random_walkable_spawn(
            env, cmap, near_min_x, near_min_y, near_max_x, near_max_y,
            x0, y0, &x1, &y1) &&
            !pvp_find_nearest_walkable_spawn(
                cmap, x0 + FIGHT_NEARBY_RADIUS, y0, x0, y0, &x1, &y1)) {
        fprintf(stderr, "osrs_pvp: no walkable opponent spawn in fight area\n");
        abort();
    }

    pvp_set_player_spawn(&env->players[0], x0, y0);
    pvp_set_player_spawn(&env->players[1], x1, y1);
}

/**
 * Initialize internal buffer pointers for ocean pattern.
 *
 * Points observations/actions/rewards/terminals/action_masks at the internal
 * _*_buf arrays so game logic writes to local storage. PufferLib shared
 * buffers are accessed via ocean_* pointers set by the binding.
 *
 * @param env Environment to initialize
 */
void pvp_init(OsrsEnv* env) {
    env->observations = env->_obs_buf;
    env->actions = env->_acts_buf;
    env->rewards = env->_rews_buf;
    env->terminals = env->_terms_buf;
    env->action_masks = env->_masks_buf;
    env->action_masks_agents = 0x3;  // Both agents get masks

    memset(env->_obs_buf, 0, sizeof(env->_obs_buf));
    memset(env->_acts_buf, 0, sizeof(env->_acts_buf));
    memset(env->_rews_buf, 0, sizeof(env->_rews_buf));
    memset(env->_terms_buf, 0, sizeof(env->_terms_buf));
    memset(env->_masks_buf, 0, sizeof(env->_masks_buf));
    memset(env->step_rewards, 0, sizeof(env->step_rewards));
    memset(env->step_terminals, 0, sizeof(env->step_terminals));

    env->_episode_return = 0.0f;
    env->has_rng_seed = 0;
    env->is_lms = 1;
    env->pvp_runtime.is_pvp_arena = 0;
    env->auto_reset = 1;
    env->pvp_runtime.use_c_opponent = 0;
    env->pvp_runtime.use_c_opponent_p0 = 0;
    env->pvp_runtime.use_external_opponent_actions = 0;
    env->ocean_io.agent_obs_p1 = NULL;
    env->ocean_io.selfplay_mask = NULL;
    memset(env->pvp_runtime.external_opponent_actions, 0, sizeof(env->pvp_runtime.external_opponent_actions));
    memset(&env->pvp_runtime.opponent, 0, sizeof(env->pvp_runtime.opponent));
    memset(&env->pvp_runtime.opponent_p0, 0, sizeof(env->pvp_runtime.opponent_p0));
    memset(&env->pvp_runtime.pfsp, 0, sizeof(env->pvp_runtime.pfsp));
    pvp_terminal_presentation_clear(env);
    memset(env->pvp_runtime.gear_tier_weights, 0, sizeof(env->pvp_runtime.gear_tier_weights));
    env->pvp_runtime.start_mode = PVP_START_RANDOMIZED;
    env->pvp_runtime.scripted_prayer_reaction_mode =
        PVP_SCRIPTED_PRAYER_REACTION_HUMANIZED;
    for (int i = 0; i < NUM_AGENTS; i++) {
        env->pvp_runtime.walk_dest_x[i] = -1;
        env->pvp_runtime.walk_dest_y[i] = -1;
    }
    memset(&env->shaping, 0, sizeof(env->shaping));
    memset(&env->log, 0, sizeof(env->log));
}

void pvp_render(OsrsEnv* env);

static inline int pvp_should_generate_slot_observations_and_masks(
    const OsrsEnv* env,
    int agent_idx
) {
    if (agent_idx == 0) return 1;
    if (agent_idx == 1 && env->ocean_io.agent_obs_p1 != NULL) return 1;
    if (env->action_masks != NULL && (env->action_masks_agents & (1 << agent_idx))) {
        return 1;
    }
    return 0;
}

static inline void pvp_generate_exported_slot_observations_and_masks(OsrsEnv* env) {
    for (int i = 0; i < NUM_AGENTS; i++) {
        if (pvp_should_generate_slot_observations_and_masks(env, i)) {
            pvp_generate_slot_observations_and_masks(env, i);
        }
    }
}

void pvp_reset(OsrsEnv* env) {
    if (env->has_rng_seed) {
        if (env->rng_seed == 0) {
            fprintf(stderr, "Error: seed must be non-zero (use seed=1 or higher in reset())\n");
            abort();
        }
        env->rng_state = env->rng_seed + 0x9E3779B9u * env->rng_reset_count;
        if (env->rng_state == 0) env->rng_state = 0x6D2B79F5u;
        env->rng_reset_count += 1;
    } else {
        env->rng_state = (uint32_t)(size_t)env ^ 0xDEADBEEF;
    }

    init_player(&env->players[0]);
    init_player(&env->players[1]);

    // LMS overrides: defence capped at 75, prayer is 99 (no drain in LMS)
    for (int i = 0; i < NUM_AGENTS; i++) {
        env->players[i].is_lms = env->is_lms;
        if (env->is_lms) {
            env->players[i].base_defence = LMS_BASE_DEFENCE;
            env->players[i].current_defence = LMS_BASE_DEFENCE;
            env->players[i].base_prayer = 99;
            env->players[i].current_prayer = 99;
        }
    }

    set_fight_positions(env);

    // Initialize last_obs_target to actual opponent positions
    // (needed for first-tick movement commands like farcast)
    env->players[0].last_obs_target_x = env->players[1].x;
    env->players[0].last_obs_target_y = env->players[1].y;
    env->players[1].last_obs_target_x = env->players[0].x;
    env->players[1].last_obs_target_y = env->players[0].y;

    env->tick = 0;
    env->episode_over = 0;
    env->winner = -1;
    env->pid_holder = rand_int(env, 2);
    env->pid_shuffle_countdown = 100 + rand_int(env, 51);

    env->pvp_runtime.is_pvp_arena = 0;
    for (int i = 0; i < NUM_AGENTS; i++) {
        env->pvp_runtime.walk_dest_x[i] = -1;
        env->pvp_runtime.walk_dest_y[i] = -1;
    }

    env->_episode_return = 0.0f;

    memset(env->rewards, 0, NUM_AGENTS * sizeof(float));
    memset(env->terminals, 0, NUM_AGENTS);

    memset(env->pending_actions, 0, sizeof(env->pending_actions));
    memset(env->last_executed_actions, 0, sizeof(env->last_executed_actions));

    // Initialize slot mode equipment with correlated per-episode gear randomization
    // LMS tier distribution: 80% same, 15% ±1 tier, 5% ±2 tiers
    int base_tier = sample_gear_tier(env->pvp_runtime.gear_tier_weights, &env->rng_state);
    int p1_tier = base_tier;

    float tier_roll = (float)xorshift32(&env->rng_state) / (float)UINT32_MAX;
    if (tier_roll >= 0.80f && tier_roll < 0.95f) {
        int dir = (xorshift32(&env->rng_state) & 1) ? 1 : -1;
        p1_tier = base_tier + dir;
    } else if (tier_roll >= 0.95f) {
        int dir = (xorshift32(&env->rng_state) & 1) ? 1 : -1;
        p1_tier = base_tier + dir * 2;
    }
    if (p1_tier < 0) p1_tier = 0;
    if (p1_tier > 3) p1_tier = 3;

    int tiers[NUM_AGENTS] = { base_tier, p1_tier };
    for (int i = 0; i < NUM_AGENTS; i++) {
        init_player_gear_randomized(&env->players[i], tiers[i], &env->rng_state);
        env->players[i].food_count = compute_food_count(&env->players[i]);
        osrs_refresh_player_equipment(&env->players[i]);
    }

    // Reset C-side opponent state for new episode
    // Always reset when PFSP is configured (selfplay toggle happens inside opponent_reset)
    if (env->pvp_runtime.use_c_opponent || env->pvp_runtime.opponent.type == OPP_PFSP) {
        opponent_reset(env, &env->pvp_runtime.opponent);
    }
    if (env->pvp_runtime.use_c_opponent_p0) {
        opponent_reset(env, &env->pvp_runtime.opponent_p0);
    }

    pvp_generate_exported_slot_observations_and_masks(env);
}

static const char* pvp_terminal_opponent_name(OsrsEnv* env) {
    if (env->pvp_runtime.use_external_opponent_actions ||
            env->pvp_runtime.opponent.type == OPP_SELFPLAY) {
        return "Opponent Agent";
    }
    if (env->pvp_runtime.opponent.type != OPP_NONE ||
            env->pvp_runtime.opponent.active_sub_policy != OPP_NONE) {
        return osrs_pvp_opponent_state_display_name(&env->pvp_runtime.opponent);
    }
    return "Opponent";
}

static void pvp_terminal_presentation_capture(OsrsEnv* env) {
    PvpTerminalPresentation* p = &env->pvp_runtime.terminal_presentation;
    memset(p, 0, sizeof(*p));
    p->phase = PVP_TERMINAL_PRESENTATION_DEATH;
    p->winner = env->winner;
    memcpy(p->players, env->players, sizeof(p->players));
    snprintf(p->opponent_name, sizeof(p->opponent_name), "%s",
        pvp_terminal_opponent_name(env));

    if (env->winner >= 0 && env->winner < NUM_AGENTS) {
        int loser = 1 - env->winner;
        p->players[loser].current_hitpoints = 0;
        osrs_interaction_set(&p->players[env->winner].interaction, loser);
        osrs_interaction_set(&p->players[loser].interaction, env->winner);
    }
}

void pvp_step(OsrsEnv* env) {
    int osrs_pvp_prof_enabled = OSRS_PVP_PROFILE_ENABLED();
    double osrs_pvp_prof_start = osrs_pvp_prof_enabled ? OSRS_PVP_PROFILE_NOW_MS() : 0.0;
    double osrs_pvp_prof_t0 = osrs_pvp_prof_start;

    memset(env->rewards, 0, NUM_AGENTS * sizeof(float));
    memset(env->terminals, 0, NUM_AGENTS);

    // Reset per-tick flags at START (clears flags from PREVIOUS tick)
    // This allows get_state() to read flags after pvp_step() returns
    for (int i = 0; i < NUM_AGENTS; i++) {
        env->players[i].hit_landed_this_tick = 0;
        env->players[i].hit_was_successful = 0;
        env->players[i].hit_spell_type = 0;
        env->players[i].hit_damage = 0;
        env->players[i].hit_style = ATTACK_STYLE_NONE;
        env->players[i].hit_defender_prayer = PRAYER_NONE;
        env->players[i].hit_was_on_prayer = 0;
        env->players[i].hit_attacker_idx = -1;
        env->players[i].freeze_applied_this_tick = 0;
    }
    reset_tick_flags(&env->players[0]);
    reset_tick_flags(&env->players[1]);
    OSRS_PVP_PROFILE_MARK(PVP_PROF_API_CLEAR_FLAGS);

    // Copy model's actions (player 0) or clear if C opponent controls p0
    if (env->pvp_runtime.use_c_opponent_p0) {
        memset(env->actions, 0, NUM_ACTION_HEADS * sizeof(int));
    } else {
        memcpy(env->actions, env->ocean_io.agent_actions, NUM_ACTION_HEADS * sizeof(int));
    }

    // Copy external opponent actions (player 1) or clear for C opponent
    if (env->pvp_runtime.use_external_opponent_actions) {
        memcpy(
            env->actions + NUM_ACTION_HEADS,
            env->pvp_runtime.external_opponent_actions,
            NUM_ACTION_HEADS * sizeof(int)
        );
    } else {
        memset(env->actions + NUM_ACTION_HEADS, 0, NUM_ACTION_HEADS * sizeof(int));
    }
    OSRS_PVP_PROFILE_MARK(PVP_PROF_API_ACTION_COPY);

    // Generate C opponent actions (writes to pending_actions, then copy to actions)
    if (env->pvp_runtime.use_c_opponent && !env->pvp_runtime.use_external_opponent_actions) {
        generate_opponent_action(env, &env->pvp_runtime.opponent);
        // Copy C opponent's action from pending to actions buffer
        memcpy(
            env->actions + NUM_ACTION_HEADS,
            env->pending_actions + NUM_ACTION_HEADS,
            NUM_ACTION_HEADS * sizeof(int)
        );
    }
    if (env->pvp_runtime.use_c_opponent_p0) {
        generate_opponent_action_for_player0(env, &env->pvp_runtime.opponent_p0);
        // Copy C opponent's action from pending to actions buffer
        memcpy(
            env->actions,
            env->pending_actions,
            NUM_ACTION_HEADS * sizeof(int)
        );
    }
    OSRS_PVP_PROFILE_MARK(PVP_PROF_API_C_OPPONENT);

    int first = env->pid_holder;
    int second = 1 - env->pid_holder;

    memcpy(
        env->last_executed_actions,
        env->actions,
        NUM_AGENTS * NUM_ACTION_HEADS * sizeof(int)
    );

    update_timers(&env->players[0]);
    update_timers(&env->players[1]);

    for (int i = 0; i < NUM_AGENTS; i++) {
        Player* pi = &env->players[i];
        pi->prev_hp_percent = (float)pi->current_hitpoints / (float)pi->base_hitpoints;
    }

    const int* agent_actions[NUM_AGENTS];
    agent_actions[0] = env->actions;
    agent_actions[1] = env->actions + NUM_ACTION_HEADS;

    int pre_move_x[NUM_AGENTS], pre_move_y[NUM_AGENTS];
    for (int i = 0; i < NUM_AGENTS; i++) {
        pre_move_x[i] = env->players[i].x;
        pre_move_y[i] = env->players[i].y;
    }

    execute_switches(env, first, agent_actions[first]);
    execute_switches(env, second, agent_actions[second]);
    OSRS_PVP_PROFILE_MARK(PVP_PROF_API_SWITCHES);

    for (int i = 0; i < NUM_AGENTS; i++) {
        Player* pi = &env->players[i];
        if (pi->food_timer > 0) pi->food_timer--;
        if (pi->potion_timer > 0) pi->potion_timer--;
        if (pi->karambwan_timer > 0) pi->karambwan_timer--;
    }

    PvpAttackMoveIntent move_intents[NUM_AGENTS];
    move_intents[first] = pvp_attack_move_intent(env, first, agent_actions[first]);
    move_intents[second] = pvp_attack_move_intent(env, second, agent_actions[second]);

    pvp_step_player_movement(env, first, move_intents[first]);
    pvp_step_player_movement(env, second, move_intents[second]);

    if (env->players[0].x == env->players[1].x &&
        env->players[0].y == env->players[1].y) {
        resolve_same_tile(&env->players[second], &env->players[first], (const CollisionMap*)env->collision_map);
    }
    OSRS_PVP_PROFILE_MARK(PVP_PROF_API_MOVEMENT);

    execute_attack_combat(env, first, agent_actions[first]);
    execute_attack_combat(env, second, agent_actions[second]);

    if (env->players[0].x == env->players[1].x &&
        env->players[0].y == env->players[1].y) {
        resolve_same_tile(&env->players[second], &env->players[first], (const CollisionMap*)env->collision_map);
    }

    for (int i = 0; i < NUM_AGENTS; i++) {
        int dx = abs(env->players[i].x - pre_move_x[i]);
        int dy = abs(env->players[i].y - pre_move_y[i]);
        int dist = (dx > dy) ? dx : dy;
        env->players[i].is_running = (dist >= 2) ? 1 : 0;
    }
    OSRS_PVP_PROFILE_MARK(PVP_PROF_API_COMBAT);

    process_pending_hits(env, 0, 1);
    process_pending_hits(env, 1, 0);

    // Morrigan's javelin DoT: 5 HP every 3 ticks from calc tick
    for (int i = 0; i < NUM_AGENTS; i++) {
        Player* p = &env->players[i];
        if (p->morr_dot_remaining > 0) {
            p->morr_dot_tick_counter--;
            if (p->morr_dot_tick_counter <= 0) {
                int dot_dmg = (p->morr_dot_remaining >= 5) ? 5 : p->morr_dot_remaining;
                p->current_hitpoints -= dot_dmg;
                p->morr_dot_remaining -= dot_dmg;
                p->damage_applied_this_tick += dot_dmg;
                if (p->current_hitpoints < 0) p->current_hitpoints = 0;
                p->morr_dot_tick_counter = 3;
            }
        }
    }

    if (env->players[0].veng_active) {
        env->players[1].observed_target_lunar_spellbook = 1;
    }
    if (env->players[1].veng_active) {
        env->players[0].observed_target_lunar_spellbook = 1;
    }
    env->tick++;

    env->pid_shuffle_countdown--;
    if (env->pid_shuffle_countdown <= 0) {
        env->pid_holder = 1 - env->pid_holder;
        env->pid_shuffle_countdown = 100 + rand_int(env, 51);
    }
    OSRS_PVP_PROFILE_MARK(PVP_PROF_API_PENDING_HITS);

    memcpy(env->pending_actions, env->actions,
           NUM_AGENTS * NUM_ACTION_HEADS * sizeof(int));
    memset(env->step_rewards, 0, sizeof(env->step_rewards));
    memset(env->step_terminals, 0, sizeof(env->step_terminals));

    int p0_dead = env->players[0].current_hitpoints <= 0;
    int p1_dead = env->players[1].current_hitpoints <= 0;
    if (p0_dead || p1_dead) {
        env->episode_over = 1;
        env->winner = (p0_dead && p1_dead) ? -1 : (p0_dead ? 1 : 0);
    }

    if (!env->episode_over && env->tick >= MAX_EPISODE_TICKS) {
        env->episode_over = 1;
        env->winner = -1;
    }
    for (int i = 0; i < NUM_AGENTS; i++) {
        env->rewards[i] = calculate_reward(env, i);
        env->terminals[i] = env->episode_over ? 1 : 0;
        env->step_rewards[i] = env->rewards[i];
        env->step_terminals[i] = env->terminals[i];
    }
    OSRS_PVP_PROFILE_MARK(PVP_PROF_API_REWARD_TERMINAL);

    env->_episode_return += env->rewards[0];
    double pvp_obs_generate_start = osrs_pvp_prof_enabled ? OSRS_PVP_PROFILE_NOW_MS() : 0.0;
    pvp_generate_exported_slot_observations_and_masks(env);
    if (osrs_pvp_prof_enabled) {
        OSRS_PVP_PROFILE_ADD(
            PVP_PROF_API_OBS_GENERATE,
            OSRS_PVP_PROFILE_NOW_MS() - pvp_obs_generate_start);
    }

    double pvp_obs_write_start = osrs_pvp_prof_enabled ? OSRS_PVP_PROFILE_NOW_MS() : 0.0;
    ocean_write_obs(env);
    if (env->ocean_io.agent_obs_p1 != NULL) {
        ocean_write_obs_p1(env);
    }
    if (osrs_pvp_prof_enabled) {
        OSRS_PVP_PROFILE_ADD(
            PVP_PROF_API_OCEAN_WRITE,
            OSRS_PVP_PROFILE_NOW_MS() - pvp_obs_write_start);
    }
    env->ocean_io.agent_rewards[0] = env->rewards[0];
    OSRS_PVP_PROFILE_MARK(PVP_PROF_API_OBS_MASK);

    if (env->episode_over) {
        env->ocean_io.agent_terminals[0] = 1;

        // PFSP win tracking (all in C, zero Python overhead).
        // Skip if pool_idx is -1 (sentinel for pre-pool-config first episode).
        if (env->pvp_runtime.opponent.type == OPP_PFSP && env->pvp_runtime.pfsp.active_pool_idx >= 0) {
            int idx = env->pvp_runtime.pfsp.active_pool_idx;
            env->pvp_runtime.pfsp.episodes[idx] += 1.0f;
            if (env->winner == 0) {
                env->pvp_runtime.pfsp.wins[idx] += 1.0f;
            }
        }

        Player* p0 = &env->players[0];
        Player* p1 = &env->players[1];
        float expected_diff = p0->expected_damage_dealt - p0->expected_damage_received;
        float expected_damage_score = clampf(0.5f + expected_diff / 198.0f, 0.0f, 1.0f);
        BrewResult opp_brew = osrs_brew_effect(p1->base_hitpoints, p1->base_attack,
            p1->base_strength, p1->base_ranged, p1->base_magic);
        float remaining_supply_hp = 20.0f * (float)p1->food_count
            + 18.0f * (float)p1->karambwan_count
            + (float)opp_brew.hp_healed * (float)p1->brew_doses;
        float max_supply_hp = 20.0f * (float)MAXED_FOOD_COUNT
            + 18.0f * (float)MAXED_KARAMBWAN_COUNT
            + (float)opp_brew.hp_healed * (float)MAXED_BREW_DOSES;
        float ko_supply_score = (env->winner == 0 && max_supply_hp > 0.0f)
            ? clampf(remaining_supply_hp / max_supply_hp, 0.0f, 1.0f)
            : 0.0f;
        env->log.episode_return = env->_episode_return;
        env->log.episode_length = (float)env->tick;
        env->log.damage_dealt = p0->total_damage_dealt;
        env->log.damage_received = p0->total_damage_received;
        env->log.expected_damage_dealt = p0->expected_damage_dealt;
        env->log.expected_damage_received = p0->expected_damage_received;
        env->log.expected_damage_prevented = p0->expected_damage_prevented;
        env->log.expected_damage_diff = expected_diff;
        env->log.expected_damage_score = expected_damage_score;
        env->log.ko_supply_score = ko_supply_score;
        env->log.ko_chance_count = p0->ko_chance_count;
        env->log.ko_chance_prob = 1.0f - p0->ko_chance_survival_prob;
        env->log.performance_score = 0.55f * expected_damage_score
            + 0.30f * ((env->winner == 0) ? 1.0f : 0.0f)
            + 0.15f * ko_supply_score;
        env->log.wins = (env->winner == 0) ? 1.0f : 0.0f;
        env->log.draws = (env->winner < 0) ? 1.0f : 0.0f;
        env->log.prayer_correct = (float)p0->target_pray_correct_count;
        env->log.prayer_total = (float)(p0->target_pray_melee_count +
            p0->target_pray_ranged_count + p0->target_pray_magic_count);
        env->log.food_remaining = (float)p0->food_count;
        env->log.karambwan_remaining = (float)p0->karambwan_count;
        env->log.brews_remaining = (float)p0->brew_doses;
        env->log.spec_energy_remaining = (float)p0->special_energy;
        env->log.attacks_landed = (float)p0->total_target_hit_count;
        env->log.off_prayer_hits = (float)p0->target_hit_off_prayer_count;
        env->log.equip_click_attempts = (float)p0->equip_click_attempts;
        env->log.equip_click_noop_rate = p0->equip_click_attempts > 0
            ? 1.0f - (float)p0->equip_click_successes /
                (float)p0->equip_click_attempts
            : 0.0f;
        env->log.special_arm_attempts = (float)p0->special_arm_attempts;
        env->log.special_arm_noop_rate = p0->special_arm_attempts > 0
            ? 1.0f - (float)p0->special_arm_successes /
                (float)p0->special_arm_attempts
            : 0.0f;
        env->log.target_click_attempts = (float)p0->target_click_attempts;
        env->log.target_click_no_fire_rate = p0->target_click_attempts > 0
            ? 1.0f - (float)p0->target_click_successes /
                (float)p0->target_click_attempts
            : 0.0f;
        env->log.spell_attack_attempts = (float)p0->spell_attack_attempts;
        env->log.spell_attack_no_fire_rate = p0->spell_attack_attempts > 0
            ? 1.0f - (float)p0->spell_attack_successes /
                (float)p0->spell_attack_attempts
            : 0.0f;
        float selected_attacks = (float)(p0->selected_melee_attack_attempts +
            p0->selected_ranged_attack_attempts +
            p0->selected_magic_attack_attempts);
        env->log.selected_melee_attack_rate = selected_attacks > 0.0f
            ? (float)p0->selected_melee_attack_attempts / selected_attacks
            : 0.0f;
        env->log.selected_ranged_attack_rate = selected_attacks > 0.0f
            ? (float)p0->selected_ranged_attack_attempts / selected_attacks
            : 0.0f;
        env->log.selected_magic_attack_rate = selected_attacks > 0.0f
            ? (float)p0->selected_magic_attack_attempts / selected_attacks
            : 0.0f;
        env->log.target_click_chase_ticks = (float)p0->target_click_chase_ticks;
        env->log.explicit_move_ticks = (float)p0->explicit_move_ticks;
        env->log.target_click_pre_move_dist = p0->target_click_attempts > 0
            ? (float)p0->target_click_pre_move_dist_sum /
                (float)p0->target_click_attempts
            : 0.0f;
        env->log.target_click_post_move_dist = p0->target_click_attempts > 0
            ? (float)p0->target_click_post_move_dist_sum /
                (float)p0->target_click_attempts
            : 0.0f;
        env->log.target_click_success_pre_move_dist = p0->target_click_successes > 0
            ? (float)p0->target_click_success_pre_move_dist_sum /
                (float)p0->target_click_successes
            : 0.0f;
        env->log.target_click_success_post_move_dist = p0->target_click_successes > 0
            ? (float)p0->target_click_success_post_move_dist_sum /
                (float)p0->target_click_successes
            : 0.0f;
        env->log.spell_attack_pre_move_dist = p0->spell_attack_attempts > 0
            ? (float)p0->spell_attack_pre_move_dist_sum /
                (float)p0->spell_attack_attempts
            : 0.0f;
        env->log.spell_attack_post_move_dist = p0->spell_attack_attempts > 0
            ? (float)p0->spell_attack_post_move_dist_sum /
                (float)p0->spell_attack_attempts
            : 0.0f;
        env->log.spell_attack_success_pre_move_dist = p0->spell_attack_successes > 0
            ? (float)p0->spell_attack_success_pre_move_dist_sum /
                (float)p0->spell_attack_successes
            : 0.0f;
        env->log.spell_attack_success_post_move_dist = p0->spell_attack_successes > 0
            ? (float)p0->spell_attack_success_post_move_dist_sum /
                (float)p0->spell_attack_successes
            : 0.0f;
        float attack_successes = (float)(p0->weapon_attack_successes +
            p0->spell_attack_successes);
        env->log.weapon_attack_rate = attack_successes > 0.0f
            ? (float)p0->weapon_attack_successes / attack_successes
            : 0.0f;
        env->log.melee_attack_rate = attack_successes > 0.0f
            ? (float)p0->melee_attack_successes / attack_successes
            : 0.0f;
        env->log.ranged_attack_rate = attack_successes > 0.0f
            ? (float)p0->ranged_attack_successes / attack_successes
            : 0.0f;
        env->log.magic_attack_rate = attack_successes > 0.0f
            ? (float)p0->magic_attack_successes / attack_successes
            : 0.0f;
        env->log.attack_after_equip_rate = p0->target_click_successes > 0
            ? (float)p0->attack_after_equip_successes /
                (float)p0->target_click_successes
            : 0.0f;
        env->log.spec_after_equip_rate = p0->target_click_successes > 0
            ? (float)p0->spec_after_equip_successes /
                (float)p0->target_click_successes
            : 0.0f;
        env->log.n = 1.0f;
        pvp_terminal_presentation_capture(env);
        OSRS_PVP_PROFILE_MARK(PVP_PROF_API_TERMINAL_SCORING);

        if (env->auto_reset) {
            pvp_reset(env);
        }
        OSRS_PVP_PROFILE_MARK(PVP_PROF_API_AUTO_RESET);
    } else {
        env->ocean_io.agent_terminals[0] = 0;
        OSRS_PVP_PROFILE_MARK(PVP_PROF_API_TERMINAL_SCORING);
        OSRS_PVP_PROFILE_MARK(PVP_PROF_API_AUTO_RESET);
    }
    if (osrs_pvp_prof_enabled) {
        OSRS_PVP_PROFILE_ADD(
            PVP_PROF_API_TOTAL, OSRS_PVP_PROFILE_NOW_MS() - osrs_pvp_prof_start);
    }
}

/**
 * Set RNG seed for deterministic runs.
 *
 * @param env  Environment
 * @param seed Seed value (must be non-zero)
 */
void pvp_seed(OsrsEnv* env, uint32_t seed) {
    env->rng_seed = seed;
    env->rng_reset_count = 0;
    env->has_rng_seed = 1;
}

/**
 * Cleanup environment resources.
 *
 * Currently a no-op since all memory is statically allocated.
 *
 * @param env Environment
 */
void pvp_close(OsrsEnv* env) {
    (void)env;
}

#endif // OSRS_PVP_API_H
