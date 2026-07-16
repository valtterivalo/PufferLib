/**
 * @file osrs_pvp_api.h
 * @brief Public OSRS PvP API: pvp_init, pvp_reset, pvp_step, pvp_seed, pvp_close.
 */

#ifndef OSRS_PVP_API_H
#define OSRS_PVP_API_H

#include "osrs_types.h"
#include "osrs_pvp_gear.h"
#include "osrs_pvp_combat.h"
#include "osrs_pvp_movement.h"
#include "osrs_pvp_observations.h"
#include "osrs_pvp_actions.h"

/**
 * Initialize a player to the default maxed pure build: base = current stats, mage
 * gear, full consumables, all timers/counters/combat history cleared.
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
    p->attack_was_on_prayer = 0;
    p->last_attack_dx = 0;
    p->last_attack_dy = 0;
    p->last_attack_dist = 0;
    p->attack_click_canceled = 0;
    p->attack_click_ready = 0;

    memset(p->pending_hits, 0, sizeof(p->pending_hits));
    p->num_pending_hits = 0;
    p->damage_applied_this_tick = 0;
    p->did_attack_auto_move = 0;

    p->hit_landed_this_tick = 0;
    p->hit_was_successful = 0;
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

    p->is_lunar_spellbook = 0;
    p->observed_target_lunar_spellbook = 0;
    p->has_blood_fury = 1;

    p->melee_spec_weapon = MELEE_SPEC_NONE;
    p->ranged_spec_weapon = RANGED_SPEC_NONE;
    p->magic_spec_weapon = MAGIC_SPEC_NONE;

    p->bolt_proc_damage = 0.2f;
    p->bolt_ignores_defense = 0;

    p->prev_hp_percent = 1.0f;
}

/**
 * Place both players in the fight area: deterministic when seeded, else random
 * and nearby each other.
 */
static void set_fight_positions(OsrsEnv* env) {
    if (env->has_rng_seed) {
        int x0 = FIGHT_AREA_BASE_X;
        int y0 = FIGHT_AREA_BASE_Y;
        int x1 = FIGHT_AREA_BASE_X + FIGHT_NEARBY_RADIUS;
        int y1 = FIGHT_AREA_BASE_Y;

        env->players[0].x = x0;
        env->players[0].y = y0;
        env->players[0].dest_x = x0;
        env->players[0].dest_y = y0;
        env->players[0].is_moving = 0;

        env->players[1].x = x1;
        env->players[1].y = y1;
        env->players[1].dest_x = x1;
        env->players[1].dest_y = y1;
        env->players[1].is_moving = 0;
        return;
    }

    int base_x = FIGHT_AREA_BASE_X;
    int base_y = FIGHT_AREA_BASE_Y;
    int max_x = base_x + FIGHT_AREA_WIDTH;
    int max_y = base_y + FIGHT_AREA_HEIGHT;

    int x0 = base_x + rand_int(env, FIGHT_AREA_WIDTH);
    int y0 = base_y + rand_int(env, FIGHT_AREA_HEIGHT);

    int near_min_x = max_int(base_x, x0 - FIGHT_NEARBY_RADIUS);
    int near_min_y = max_int(base_y, y0 - FIGHT_NEARBY_RADIUS);
    int near_max_x = min_int(max_x, x0 + FIGHT_NEARBY_RADIUS);
    int near_max_y = min_int(max_y, y0 + FIGHT_NEARBY_RADIUS);

    int x1 = near_min_x + rand_int(env, near_max_x - near_min_x);
    int y1 = near_min_y + rand_int(env, near_max_y - near_min_y);

    env->players[0].x = x0;
    env->players[0].y = y0;
    env->players[0].dest_x = x0;
    env->players[0].dest_y = y0;
    env->players[0].is_moving = 0;

    env->players[1].x = x1;
    env->players[1].y = y1;
    env->players[1].dest_x = x1;
    env->players[1].dest_y = y1;
    env->players[1].is_moving = 0;
}

/**
 * Point observations/actions/rewards/terminals/action_masks at the internal
 * _*_buf arrays (game logic writes local storage; PufferLib shared buffers are
 * reached via ocean_* pointers set by the binding) and zero all runtime state.
 */
void pvp_init(OsrsEnv* env) {
    env->observations = env->_obs_buf;
    env->actions = env->_acts_buf;
    env->rewards = env->_rews_buf;
    env->terminals = env->_terms_buf;
    env->action_masks = env->_masks_buf;
    env->action_masks_agents = 0x3;  /* both agents get masks */

    memset(env->_obs_buf, 0, sizeof(env->_obs_buf));
    memset(env->_acts_buf, 0, sizeof(env->_acts_buf));
    memset(env->_rews_buf, 0, sizeof(env->_rews_buf));
    memset(env->_terms_buf, 0, sizeof(env->_terms_buf));
    memset(env->_masks_buf, 0, sizeof(env->_masks_buf));

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
    memset(env->pvp_runtime.gear_tier_weights, 0, sizeof(env->pvp_runtime.gear_tier_weights));
    for (int i = 0; i < NUM_AGENTS; i++) {
        env->pvp_runtime.walk_dest_x[i] = -1;
        env->pvp_runtime.walk_dest_y[i] = -1;
    }
    memset(&env->shaping, 0, sizeof(env->shaping));
    memset(&env->log, 0, sizeof(env->log));
}

/* Forward decl only: binding.c provides the stub, osrs_render.h the real impl. */
void pvp_render(OsrsEnv* env);

/**
 * Reset to initial state: init both players, seed RNG, apply LMS overrides, roll
 * gear tiers, place fight positions, and generate the first observations + masks.
 */
void pvp_reset(OsrsEnv* env) {
    if (env->has_rng_seed) {
        if (env->rng_seed == 0) {
            fprintf(stderr, "Error: seed must be non-zero (use seed=1 or higher in reset())\n");
            abort();
        }
        env->rng_state = env->rng_seed + 0x9E3779B9u * env->rng_reset_count;
        env->rng_reset_count += 1;
    } else {
        env->rng_state = (uint32_t)(size_t)env ^ 0xDEADBEEF;
    }

    init_player(&env->players[0]);
    init_player(&env->players[1]);

    /* LMS: defence capped at LMS_BASE_DEFENCE, prayer 99 (no drain) */
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

    /* seed last_obs_target so first-tick moves (e.g. farcast) have a target */
    env->players[0].last_obs_target_x = env->players[1].x;
    env->players[0].last_obs_target_y = env->players[1].y;
    env->players[1].last_obs_target_x = env->players[0].x;
    env->players[1].last_obs_target_y = env->players[0].y;

    env->tick = 0;
    env->episode_over = 0;
    env->winner = -1;
    if (env->has_rng_seed) {
        env->pid_holder = 1 - (int)(env->rng_seed & 1u);
    } else {
        env->pid_holder = rand_int(env, 2);
    }
    env->pid_shuffle_countdown = 100 + rand_int(env, 51); // 100-150 ticks

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

    /* per-episode gear tiers: p1 = base tier 80%, base ±1 15%, base ±2 5% */
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

    /* reset C opponent; always reset for PFSP (selfplay toggle lives in opponent_reset) */
    if (env->pvp_runtime.use_c_opponent || env->pvp_runtime.opponent.type == OPP_PFSP) {
        opponent_reset(env, &env->pvp_runtime.opponent);
    }
    if (env->pvp_runtime.use_c_opponent_p0) {
        opponent_reset(env, &env->pvp_runtime.opponent_p0);
    }

    for (int i = 0; i < NUM_AGENTS; i++) {
        generate_slot_observations(env, i);
        if (env->action_masks != NULL && (env->action_masks_agents & (1 << i))) {
            compute_action_masks(env, i);
        }
    }
}

/**
 * Execute one game tick. Actions submitted this step apply immediately to produce
 * the next state (OSRS 1-tick delay: action at tick N shows at N+1). Order: gather
 * model/external/C-opponent actions, clamp cross-head combos, run switches then
 * movement then attacks, resolve hits, then win check, reward, observations.
 */
void pvp_step(OsrsEnv* env) {
    memset(env->rewards, 0, NUM_AGENTS * sizeof(float));
    memset(env->terminals, 0, NUM_AGENTS);

    /* reset per-tick flags at start so get_state() can read them after the step returns */
    for (int i = 0; i < NUM_AGENTS; i++) {
        env->players[i].hit_landed_this_tick = 0;
        env->players[i].hit_was_successful = 0;
        env->players[i].hit_damage = 0;
        env->players[i].hit_style = ATTACK_STYLE_NONE;
        env->players[i].hit_defender_prayer = PRAYER_NONE;
        env->players[i].hit_was_on_prayer = 0;
        env->players[i].hit_attacker_idx = -1;
        env->players[i].freeze_applied_this_tick = 0;
    }
    reset_tick_flags(&env->players[0]);
    reset_tick_flags(&env->players[1]);

    /* p0 actions: model, or cleared when a C opponent drives p0 */
    if (env->pvp_runtime.use_c_opponent_p0) {
        memset(env->actions, 0, NUM_ACTION_HEADS * sizeof(int));
    } else {
        memcpy(env->actions, env->ocean_io.agent_actions, NUM_ACTION_HEADS * sizeof(int));
    }

    /* p1 actions: external opponent, or cleared for a C opponent */
    if (env->pvp_runtime.use_external_opponent_actions) {
        memcpy(
            env->actions + NUM_ACTION_HEADS,
            env->pvp_runtime.external_opponent_actions,
            NUM_ACTION_HEADS * sizeof(int)
        );
    } else {
        memset(env->actions + NUM_ACTION_HEADS, 0, NUM_ACTION_HEADS * sizeof(int));
    }

    /* C opponent writes pending_actions; copy its slice into env->actions */
    if (env->pvp_runtime.use_c_opponent && !env->pvp_runtime.use_external_opponent_actions) {
        generate_opponent_action(env, &env->pvp_runtime.opponent);
        memcpy(
            env->actions + NUM_ACTION_HEADS,
            env->pending_actions + NUM_ACTION_HEADS,
            NUM_ACTION_HEADS * sizeof(int)
        );
    }
    if (env->pvp_runtime.use_c_opponent_p0) {
        generate_opponent_action_for_player0(env, &env->pvp_runtime.opponent_p0);
        memcpy(
            env->actions,
            env->pending_actions,
            NUM_ACTION_HEADS * sizeof(int)
        );
    }

    int first = env->pid_holder;
    int second = 1 - env->pid_holder;

    int actions_p0[NUM_ACTION_HEADS];
    int actions_p1[NUM_ACTION_HEADS];
    memcpy(actions_p0, env->actions, NUM_ACTION_HEADS * sizeof(int));
    memcpy(actions_p1, env->actions + NUM_ACTION_HEADS, NUM_ACTION_HEADS * sizeof(int));

    /* clamp impossible cross-head combo: MAGE/TANK/SPEC_MAGIC loadouts drop ATTACK_ATK */
    for (int i = 0; i < NUM_AGENTS; i++) {
        int* a = (i == 0) ? actions_p0 : actions_p1;
        int lo = a[HEAD_LOADOUT];
        int cv = a[HEAD_COMBAT];
        if (lo == LOADOUT_MAGE || lo == LOADOUT_TANK || lo == LOADOUT_SPEC_MAGIC) {
            if (cv == ATTACK_ATK) {
                a[HEAD_COMBAT] = ATTACK_NONE;
            }
        }
    }

    memcpy(env->actions, actions_p0, NUM_ACTION_HEADS * sizeof(int));
    memcpy(env->actions + NUM_ACTION_HEADS, actions_p1, NUM_ACTION_HEADS * sizeof(int));

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

    int* agent_actions[NUM_AGENTS];
    agent_actions[0] = actions_p0;
    agent_actions[1] = actions_p1;

    int pre_move_x[NUM_AGENTS], pre_move_y[NUM_AGENTS];
    for (int i = 0; i < NUM_AGENTS; i++) {
        pre_move_x[i] = env->players[i].x;
        pre_move_y[i] = env->players[i].y;
    }

    execute_switches(env, first, agent_actions[first]);
    execute_switches(env, second, agent_actions[second]);

    for (int i = 0; i < NUM_AGENTS; i++) {
        Player* pi = &env->players[i];
        if (pi->food_timer > 0) pi->food_timer--;
        if (pi->potion_timer > 0) pi->potion_timer--;
        if (pi->karambwan_timer > 0) pi->karambwan_timer--;
    }

    /* canonical movement via the shared SDK; fires only when walk_dest is set
       (HEAD_MOVE > 0, legacy HEAD_COMBAT MOVE_*, or a persistent human click).
       Attack-driven auto-chase still runs through execute_attack_movement below. */
    pvp_step_player_movement(env, first);
    pvp_step_player_movement(env, second);

    if (env->players[0].x == env->players[1].x &&
        env->players[0].y == env->players[1].y) {
        resolve_same_tile(&env->players[second], &env->players[first], (const CollisionMap*)env->collision_map);
    }

    execute_attack_movement(env, first, agent_actions[first]);
    execute_attack_movement(env, second, agent_actions[second]);

    if (env->players[0].x == env->players[1].x &&
        env->players[0].y == env->players[1].y) {
        resolve_same_tile(&env->players[second], &env->players[first], (const CollisionMap*)env->collision_map);
    }

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

    process_pending_hits(env, 0, 1);
    process_pending_hits(env, 1, 0);

    /* Morrigan's javelin DoT: 5 HP every 3 ticks */
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

    if (!env->has_rng_seed) {
        env->pid_shuffle_countdown--;
        if (env->pid_shuffle_countdown <= 0) {
            env->pid_holder = 1 - env->pid_holder;
            env->pid_shuffle_countdown = 100 + rand_int(env, 51); // 100-150 ticks
        }
    }

    memcpy(env->pending_actions, env->actions,
           NUM_AGENTS * NUM_ACTION_HEADS * sizeof(int));
    for (int i = 0; i < NUM_AGENTS; i++) {
        if (env->players[i].current_hitpoints <= 0) {
            env->episode_over = 1;
            env->winner = 1 - i;
        }
    }

    /* timeout counts as an agent 0 loss */
    if (!env->episode_over && env->tick >= MAX_EPISODE_TICKS) {
        env->episode_over = 1;
        env->winner = 1;
    }
    for (int i = 0; i < NUM_AGENTS; i++) {
        env->rewards[i] = calculate_reward(env, i);

        if (env->episode_over) {
            env->terminals[i] = 1;
        }
    }

    env->_episode_return += env->rewards[0];
    for (int i = 0; i < NUM_AGENTS; i++) {
        generate_slot_observations(env, i);
        if (env->action_masks != NULL && (env->action_masks_agents & (1 << i))) {
            compute_action_masks(env, i);
        }
    }

    ocean_write_obs(env);
    if (env->ocean_io.agent_obs_p1 != NULL) {
        ocean_write_obs_p1(env);
    }
    env->ocean_io.agent_rewards[0] = env->rewards[0];

    if (env->episode_over) {
        env->ocean_io.agent_terminals[0] = 1;

        /* PFSP win tracking; pool_idx -1 = pre-pool-config first episode, skip */
        if (env->pvp_runtime.opponent.type == OPP_PFSP && env->pvp_runtime.pfsp.active_pool_idx >= 0) {
            int idx = env->pvp_runtime.pfsp.active_pool_idx;
            env->pvp_runtime.pfsp.episodes[idx] += 1.0f;
            if (env->winner == 0) {
                env->pvp_runtime.pfsp.wins[idx] += 1.0f;
            }
        }

        Player* p0 = &env->players[0];
        env->log.episode_return = env->_episode_return;
        env->log.episode_length = (float)env->tick;
        env->log.damage_dealt = p0->total_damage_dealt;
        env->log.damage_received = p0->total_damage_received;
        env->log.wins = (env->winner == 0) ? 1.0f : 0.0f;
        env->log.prayer_correct = (float)p0->target_pray_correct_count;
        env->log.prayer_total = (float)(p0->target_pray_melee_count +
            p0->target_pray_ranged_count + p0->target_pray_magic_count);
        env->log.food_remaining = (float)p0->food_count;
        env->log.karambwan_remaining = (float)p0->karambwan_count;
        env->log.brews_remaining = (float)p0->brew_doses;
        env->log.spec_energy_remaining = (float)p0->special_energy;
        env->log.attacks_landed = (float)p0->total_target_hit_count;
        env->log.off_prayer_hits = (float)p0->target_hit_off_prayer_count;
        env->log.n = 1.0f;

        if (env->auto_reset) {
            pvp_reset(env);
        }
    } else {
        env->ocean_io.agent_terminals[0] = 0;
    }
}

/** Set the RNG seed for deterministic runs (seed must be non-zero). */
void pvp_seed(OsrsEnv* env, uint32_t seed) {
    env->rng_seed = seed;
    env->rng_reset_count = 0;
    env->has_rng_seed = 1;
}

/** No-op: all environment memory is statically allocated. */
void pvp_close(OsrsEnv* env) {
    (void)env;
}

#endif // OSRS_PVP_API_H
