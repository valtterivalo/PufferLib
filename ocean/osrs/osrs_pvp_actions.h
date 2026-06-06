/**
 * @file osrs_pvp_actions.h
 * @brief Action processing for loadout-based action space
 *
 * Handles player actions including:
 * - Food and potion consumption
 * - Timer updates
 * - Loadout-based action processing (8 heads)
 * - Reward calculation
 */

#ifndef OSRS_PVP_ACTIONS_H
#define OSRS_PVP_ACTIONS_H

#include "osrs_types.h"
#include "osrs_items.h"
#include "osrs_consumables.h"
#include "osrs_player_consumables.h"
#include "osrs_pvp_gear.h"
#include "osrs_pvp_combat.h"
#include "osrs_pvp_movement.h"
#include "osrs_pvp_observations.h"  // For can_eat_food, can_use_potion, etc.
#include "osrs_encounter.h"         // For ENCOUNTER_OVERHEAD_*, encounter_apply_*_action, encounter_drain_all_prayers
// prayer drain: encounter_drain_all_prayers() in osrs_encounter.h drives both
// overhead and offensive drain in a single call with activation-tick skip.

// NH gear prayer bonus: fury amulet +3, neitiznot helm +3 = 6 total.
// hardcoded because these are always equipped regardless of gear set.
#define PRAYER_BONUS 6

/**
 * @param p            Player eating
 * @param is_karambwan 1 for karambwan, 0 for regular food
 */
static void eat_food(Player* p, int is_karambwan) {
    osrs_player_eat_food_type(p, is_karambwan ? FOOD_KARAMBWAN : FOOD_SHARK);
}

/**
 * Drink potion.
 *
 * Types:
 *   1 = Saradomin brew (heals HP, boosts defence, drains attack/str/magic/ranged)
 *   2 = Super restore (restores all stats + prayer)
 *   3 = Super combat (boosts attack/strength/defence 15%+5)
 *   4 = Ranged potion (boosts ranged 10%+4)
 *
 * @param p           Player drinking
 * @param potion_type Potion type (1-4)
 */
static void drink_potion(Player* p, int potion_type) {
    if (p->potion_timer > 0) return;

    switch (potion_type) {
        case 1: {
            if (p->brew_doses <= 0) return;
            p->brew_doses--;
            // pass current levels for drain params — brew drains 10% of CURRENT not base
            BrewResult br = osrs_brew_effect(p->base_hitpoints, p->current_attack,
                                             p->current_strength, p->current_ranged,
                                             p->current_magic);
            int hp_before = p->current_hitpoints;
            int max_hp = p->base_hitpoints + br.hp_healed;
            int actual_heal = max_int(0, min_int(br.hp_healed, max_hp - hp_before));
            int waste = br.hp_healed - actual_heal;
            int def_before = p->current_defence;
            int max_def = p->is_lms ? p->base_defence : p->base_defence + br.def_boost;
            p->current_defence = clamp(def_before + br.def_boost, 0, max_def);
            p->current_hitpoints = clamp(hp_before + br.hp_healed, 0, max_hp);
            p->last_brew_heal = actual_heal;
            p->last_brew_waste = waste;
            p->current_attack = clamp(p->current_attack - br.att_drain, 0, 255);
            p->current_strength = clamp(p->current_strength - br.str_drain, 0, 255);
            p->current_magic = clamp(p->current_magic - br.magic_drain, 0, 255);
            p->current_ranged = clamp(p->current_ranged - br.range_drain, 0, 255);
            p->last_potion_type = potion_type;
            p->ate_brew_this_tick = 1;  // Track for reward shaping
            break;
        }

        case 2: {
            if (p->restore_doses <= 0) return;
            p->restore_doses--;
            int had_restore_need = (
                p->current_attack < p->base_attack ||
                p->current_strength < p->base_strength ||
                p->current_defence < p->base_defence ||
                p->current_ranged < p->base_ranged ||
                p->current_magic < p->base_magic ||
                p->current_prayer < p->base_prayer
            );
            // super restore: 8 + floor(level/4) for prayer and all stats
            DrinkResult dr = osrs_drink_potion(POTION_SUPER_RESTORE, 0, p->base_prayer, 0);
            p->current_prayer = clamp(p->current_prayer + dr.prayer_restored, 0, p->base_prayer);
            int atk_restore = osrs_drink_potion(POTION_SUPER_RESTORE, 0, p->base_attack, 0).prayer_restored;
            int str_restore = osrs_drink_potion(POTION_SUPER_RESTORE, 0, p->base_strength, 0).prayer_restored;
            int def_restore = osrs_drink_potion(POTION_SUPER_RESTORE, 0, p->base_defence, 0).prayer_restored;
            int rng_restore = osrs_drink_potion(POTION_SUPER_RESTORE, 0, p->base_ranged, 0).prayer_restored;
            int mag_restore = osrs_drink_potion(POTION_SUPER_RESTORE, 0, p->base_magic, 0).prayer_restored;
            if (p->current_attack < p->base_attack) {
                p->current_attack = clamp(p->current_attack + atk_restore, 0, p->base_attack);
            }
            if (p->current_strength < p->base_strength) {
                p->current_strength = clamp(p->current_strength + str_restore, 0, p->base_strength);
            }
            if (p->current_defence < p->base_defence) {
                p->current_defence = clamp(p->current_defence + def_restore, 0, p->base_defence);
            }
            if (p->current_ranged < p->base_ranged) {
                p->current_ranged = clamp(p->current_ranged + rng_restore, 0, p->base_ranged);
            }
            if (p->current_magic < p->base_magic) {
                p->current_magic = clamp(p->current_magic + mag_restore, 0, p->base_magic);
            }
            p->last_potion_type = potion_type;
            p->last_potion_was_waste = had_restore_need ? 0 : 1;
            break;
        }

        case 3: {
            if (p->combat_potion_doses <= 0) return;
            p->combat_potion_doses--;
            int atk_boost = osrs_drink_potion(POTION_SUPER_COMBAT, 0, p->base_attack, 0).level_boost;
            int str_boost = osrs_drink_potion(POTION_SUPER_COMBAT, 0, p->base_strength, 0).level_boost;
            int def_boost = osrs_drink_potion(POTION_SUPER_COMBAT, 0, p->base_defence, 0).level_boost;
            int atk_cap = p->base_attack + atk_boost;
            int str_cap = p->base_strength + str_boost;
            int def_cap = p->is_lms ? p->base_defence : p->base_defence + def_boost;
            int had_boost_need = (
                p->current_attack < atk_cap ||
                p->current_strength < str_cap ||
                p->current_defence < def_cap
            );
            if (p->current_attack < atk_cap) {
                p->current_attack = clamp(p->current_attack + atk_boost, 0, atk_cap);
            }
            if (p->current_strength < str_cap) {
                p->current_strength = clamp(p->current_strength + str_boost, 0, str_cap);
            }
            if (p->current_defence < def_cap) {
                p->current_defence = clamp(p->current_defence + def_boost, 0, def_cap);
            }
            p->last_potion_type = potion_type;
            p->last_potion_was_waste = had_boost_need ? 0 : 1;
            break;
        }

        case 4: {
            if (p->ranged_potion_doses <= 0) return;
            p->ranged_potion_doses--;
            int rng_boost = osrs_drink_potion(POTION_RANGING, 0, p->base_ranged, 0).level_boost;
            int rng_cap = p->base_ranged + rng_boost;
            int had_boost_need = p->current_ranged < rng_cap;
            if (p->current_ranged < rng_cap) {
                p->current_ranged = clamp(p->current_ranged + rng_boost, 0, rng_cap);
            }
            p->last_potion_type = potion_type;
            p->last_potion_was_waste = had_boost_need ? 0 : 1;
            break;
        }
    }

    p->potion_timer = 3;
    p->food_timer = 3;
}

/** Update all per-tick timers for a player. */
static void update_timers(Player* p) {
    p->damage_applied_this_tick = 0;

    if (p->has_attack_timer) {
        p->attack_timer_uncapped -= 1;
        if (p->attack_timer >= 0) {
            p->attack_timer -= 1;
        }
    }
    // food/potion/karambwan timers are decremented AFTER execute_switches in c_step
    // so that observations show the correct countdown (2, 1, Ready instead of 3, 2, 1)
    if (p->frozen_ticks > 0) p->frozen_ticks--;
    if (p->freeze_immunity_ticks > 0) p->freeze_immunity_ticks--;
    if (p->veng_cooldown > 0) p->veng_cooldown--;

    /* prayer drain — shared encounter_drain_all_prayers handles both overhead
       and offensive, activation-tick skip, and pp=0 auto-clear of both slots.
       LMS has no prayer drain (prayer points are unlimited) — still clear
       just-activated flags so they don't leak to next tick. */
    if (!p->is_lms) {
        encounter_drain_all_prayers(p, PRAYER_BONUS);
    } else {
        p->prayer_just_activated = 0;
        p->offensive_prayer_just_activated = 0;
    }

    if (p->run_energy < OSRS_RUN_ENERGY_FULL && (!p->is_moving || !p->is_running)) {
        p->run_recovery_ticks += 1;
        if (p->run_recovery_ticks >= RUN_ENERGY_RECOVER_TICKS) {
            p->run_energy = clamp(
                p->run_energy + OSRS_RUN_ENERGY_UNITS_PER_PERCENT,
                0,
                OSRS_RUN_ENERGY_FULL
            );
            p->run_recovery_ticks = 0;
        }
    } else {
        p->run_recovery_ticks = 0;
    }

    if (p->spec_regen_active && p->special_energy < 100) {
        encounter_tick_spec_regen(p);
    } else if (p->spec_regen_active) {
        p->item_effect_state.special_regen_ticks = 0;
    }
}

/** Reset per-tick flags at end of tick. */
static void reset_tick_flags(Player* p) {
    p->just_attacked = 0;
    p->last_queued_hit_damage = 0;
    p->attack_was_on_prayer = 0;
    p->player_prayed_correct = 0;
    p->target_prayed_correct = 0;
    p->tick_damage_scale = 0.0f;
    p->damage_dealt_scale = 0.0f;
    p->damage_received_scale = 0.0f;
    p->last_food_heal = 0;
    p->last_food_waste = 0;
    p->last_karambwan_heal = 0;
    p->last_karambwan_waste = 0;
    p->last_brew_heal = 0;
    p->last_brew_waste = 0;
    p->last_potion_type = 0;
    p->last_potion_was_waste = 0;
    p->attack_click_canceled = 0;
    p->attack_click_ready = 0;
    // Reset reward shaping action flags
    p->attack_style_this_tick = ATTACK_STYLE_NONE;
    p->attack_weapon_this_tick = ITEM_NONE;
    p->magic_type_this_tick = 0;
    p->hit_spell_type = 0;
    p->used_special_this_tick = 0;
    p->ate_food_this_tick = 0;
    p->ate_karambwan_this_tick = 0;
    p->ate_brew_this_tick = 0;
    p->cast_veng_this_tick = 0;
    p->clicks_this_tick = 0;
}

// Forward declarations for phased execution
static void execute_switches(OsrsEnv* env, int agent_idx, int* actions);
static void execute_attacks(OsrsEnv* env, int agent_idx, int* actions);

/** Resolve attack style from attack action value. */
static inline AttackStyle resolve_attack_style_for_action(Player* p, int attack_action) {
    switch (attack_action) {
        case ATTACK_ATK:
            return get_slot_weapon_attack_style(p);
        case ATTACK_ICE:
        case ATTACK_BLOOD:
            return ATTACK_STYLE_MAGIC;
        default:
            return ATTACK_STYLE_NONE;
    }
}

/**
 * Execute switch-phase actions for an agent (Phase 1).
 *
 * Execution order: overhead prayer → loadout → auto-offensive prayer →
 * consumables → movement → vengeance.
 *
 * CRITICAL: Prayer switches MUST be processed for BOTH players BEFORE
 * any attacks are processed. This ensures attacks check the correct
 * prayer state (the state after this tick's switches, not before).
 */
static void execute_switches(OsrsEnv* env, int agent_idx, int* actions) {
    Player* p = &env->players[agent_idx];
    const CollisionMap* cmap = (const CollisionMap*)env->collision_map;

    p->consumable_used_this_tick = 0;

    int overhead_action = actions[HEAD_OVERHEAD];
    int offensive_action = actions[HEAD_OFFENSIVE];

    /* LMS restricts smite/redemption. */
    if (env->is_lms &&
        (overhead_action == ENCOUNTER_OVERHEAD_SET_REFRESH_SMITE ||
         overhead_action == ENCOUNTER_OVERHEAD_SET_REFRESH_REDEMPTION)) {
        overhead_action = ENCOUNTER_OVERHEAD_NO_CHANGE;
    }
    if (p->current_prayer <= 0) {
        if (overhead_action >= ENCOUNTER_OVERHEAD_SET_REFRESH_MELEE)
            overhead_action = ENCOUNTER_OVERHEAD_NO_CHANGE;
        if (offensive_action >= ENCOUNTER_OFFENSIVE_SET_REFRESH_PIETY)
            offensive_action = ENCOUNTER_OFFENSIVE_NO_CHANGE;
    }

    OverheadPrayer prev_prayer = p->prayer;
    OffensivePrayer prev_offensive = p->offensive_prayer;
    int prayer_commanded =
        overhead_action != ENCOUNTER_OVERHEAD_NO_CHANGE ||
        offensive_action != ENCOUNTER_OFFENSIVE_NO_CHANGE;
    if (encounter_apply_overhead_action(&p->prayer, overhead_action)) {
        p->prayer_just_activated = 1;
    }
    if (encounter_apply_offensive_action(&p->offensive_prayer, offensive_action)) {
        p->offensive_prayer_just_activated = 1;
    }
    if (prayer_commanded || p->prayer != prev_prayer || p->offensive_prayer != prev_offensive)
        p->clicks_this_tick++;
    int loadout_action = actions[HEAD_LOADOUT];
    int loadout_switches = apply_loadout(p, loadout_action);
    p->clicks_this_tick += loadout_switches;
    if (loadout_switches > 0)
        osrs_interaction_check_interrupt(&p->interaction, OSRS_IACT_EQUIP);

    /* spec toggle: LOADOUT_SPEC_* arms spec for next attack */
    if (loadout_action == LOADOUT_SPEC_MELEE || loadout_action == LOADOUT_SPEC_RANGE ||
        loadout_action == LOADOUT_SPEC_MAGIC || loadout_action == LOADOUT_GMAUL) {
        p->spec_armed = 1;
    }
    int food_action = actions[HEAD_FOOD];
    if (food_action == FOOD_EAT && can_eat_food(p)) {
        eat_food(p, 0);
        p->consumable_used_this_tick = 1;
        p->clicks_this_tick++;
        osrs_interaction_check_interrupt(&p->interaction, OSRS_IACT_EAT);
    }

    int potion_action = actions[HEAD_POTION];
    switch (potion_action) {
        case POTION_BREW:
            if (can_use_potion(p, 1) && can_use_brew_boost(p)) {
                drink_potion(p, 1);
                p->consumable_used_this_tick = 1;
                p->clicks_this_tick++;
                osrs_interaction_check_interrupt(&p->interaction, OSRS_IACT_DRINK);
            }
            break;
        case POTION_RESTORE:
            if (can_use_potion(p, 2) && can_restore_stats(p)) {
                drink_potion(p, 2);
                p->consumable_used_this_tick = 1;
                p->clicks_this_tick++;
                osrs_interaction_check_interrupt(&p->interaction, OSRS_IACT_DRINK);
            }
            break;
        case POTION_COMBAT:
            if (can_use_potion(p, 3) && can_boost_combat_skills(p)) {
                drink_potion(p, 3);
                p->consumable_used_this_tick = 1;
                p->clicks_this_tick++;
                osrs_interaction_check_interrupt(&p->interaction, OSRS_IACT_DRINK);
            }
            break;
        case POTION_RANGED:
            if (can_use_potion(p, 4) && can_boost_ranged(p)) {
                drink_potion(p, 4);
                p->consumable_used_this_tick = 1;
                p->clicks_this_tick++;
                osrs_interaction_check_interrupt(&p->interaction, OSRS_IACT_DRINK);
            }
            break;
        default:
            break;
    }

    int karam_action = actions[HEAD_KARAMBWAN];
    if (karam_action == KARAM_EAT && can_eat_karambwan(p)) {
        eat_food(p, 1);
        p->consumable_used_this_tick = 1;
        p->clicks_this_tick++;
        osrs_interaction_check_interrupt(&p->interaction, OSRS_IACT_EAT);
    }
    int combat_action = actions[HEAD_COMBAT];
    int head_move = actions[HEAD_MOVE];
    int is_spec_loadout = (loadout_action == LOADOUT_SPEC_MELEE ||
                           loadout_action == LOADOUT_SPEC_RANGE ||
                           loadout_action == LOADOUT_SPEC_MAGIC ||
                           loadout_action == LOADOUT_GMAUL);

    int command_issued = 0;
    if (!is_spec_loadout && head_move > 0 && head_move < MOVE_DIM) {
        pvp_set_walk_dest_from_head_move(env, agent_idx, head_move);
        command_issued = 1;
    } else if (!is_spec_loadout && is_move_action(combat_action)) {
        int tx = p->last_obs_target_x;
        int ty = p->last_obs_target_y;
        int dest_x = -1, dest_y = -1;
        switch (combat_action) {
            case MOVE_ADJACENT:
                if (!select_closest_adjacent_tile(p, tx, ty, &dest_x, &dest_y, cmap)) {
                    dest_x = -1; dest_y = -1;
                }
                break;
            case MOVE_UNDER:
                if (is_in_wilderness(tx, ty) && collision_tile_walkable(cmap, 0, tx, ty)) {
                    dest_x = tx; dest_y = ty;
                }
                break;
            case MOVE_DIAGONAL:
                if (!select_closest_diagonal_tile(p, tx, ty, &dest_x, &dest_y, cmap)) {
                    dest_x = -1; dest_y = -1;
                }
                break;
            case MOVE_FARCAST_2:
            case MOVE_FARCAST_3:
            case MOVE_FARCAST_4:
            case MOVE_FARCAST_5:
            case MOVE_FARCAST_6:
            case MOVE_FARCAST_7: {
                int fd = combat_action - MOVE_FARCAST_2 + 2;
                if (!select_farcast_tile(p, tx, ty, fd, &dest_x, &dest_y, cmap)) {
                    dest_x = -1; dest_y = -1;
                }
                break;
            }
            default:
                break;
        }
        env->pvp_runtime.walk_dest_x[agent_idx] = dest_x;
        env->pvp_runtime.walk_dest_y[agent_idx] = dest_y;
        command_issued = (dest_x >= 0);
    }
    /* no else: when no movement command is issued this tick, walk_dest
       persists from a prior tick. matches OSRS click semantics — a click
       walks the player until arrival or a new command overrides it. the
       shared SDK clears walk_dest = -1 when the player arrives. */
    if (command_issued) {
        p->clicks_this_tick++;
        osrs_interaction_check_interrupt(&p->interaction, OSRS_IACT_MOVE);
    }
    int veng_action = actions[HEAD_VENG];
    if (veng_action == VENG_CAST && p->is_lunar_spellbook &&
        !p->veng_active && remaining_ticks(p->veng_cooldown) == 0 &&
        p->current_magic >= 94) {
        p->veng_active = 1;
        p->veng_cooldown = 50;
        p->cast_veng_this_tick = 1;
        p->clicks_this_tick++;
    }
}

static PvpAttackMoveIntent pvp_attack_move_intent(
    OsrsEnv* env,
    int agent_idx,
    int* actions
) {
    Player* p = &env->players[agent_idx];

    int loadout_action = actions[HEAD_LOADOUT];
    int combat_action = actions[HEAD_COMBAT];
    int attack_action = is_attack_action(combat_action) ? combat_action : ATTACK_NONE;

    int is_gmaul = (loadout_action == LOADOUT_GMAUL);
    if (is_gmaul) {
        attack_action = ATTACK_ATK;
    }

    int current_loadout = get_current_loadout(p);
    int in_mage_loadout = (current_loadout == LOADOUT_MAGE);
    int in_tank_loadout = (current_loadout == LOADOUT_TANK);
    if (attack_action == ATTACK_ATK && (in_mage_loadout || in_tank_loadout) && !is_gmaul) {
        attack_action = ATTACK_NONE;
    }

    AttackStyle attack_style = ATTACK_STYLE_NONE;
    if (attack_action != ATTACK_NONE) {
        switch (attack_action) {
            case ATTACK_ATK:
                attack_style = get_slot_weapon_attack_style(p);
                break;
            case ATTACK_ICE:
                attack_style = ATTACK_STYLE_MAGIC;
                break;
            case ATTACK_BLOOD:
                attack_style = ATTACK_STYLE_MAGIC;
                break;
            default:
                break;
        }
    } else if (osrs_interaction_active(&p->interaction)) {
        attack_style = get_slot_weapon_attack_style(p);
    }
    if (attack_action == ATTACK_ICE && !can_cast_ice_spell(p)) {
        attack_style = ATTACK_STYLE_NONE;
    }
    if (attack_action == ATTACK_BLOOD && !can_cast_blood_spell(p)) {
        attack_style = ATTACK_STYLE_NONE;
    }

    int range = 1;
    if (attack_style != ATTACK_STYLE_NONE) {
        range = get_attack_range(p, attack_style);
    }

    return (PvpAttackMoveIntent){
        .env = env,
        .agent_idx = agent_idx,
        .has_new_target = attack_action != ATTACK_NONE &&
            attack_style != ATTACK_STYLE_NONE,
        .target_slot = 1 - agent_idx,
        .style = attack_style,
        .range = range,
    };
}

/**
 * Attack combat phase: range check + perform attack.
 * Called for ALL players AFTER all attack movements have resolved, so
 * dist is computed from final positions (fixes PID-dependent same-tile bug).
 */
static void execute_attack_combat(OsrsEnv* env, int agent_idx, int* actions) {
    Player* p = &env->players[agent_idx];
    Player* t = &env->players[1 - agent_idx];
    const CollisionMap* cmap = (const CollisionMap*)env->collision_map;

    int loadout_action = actions[HEAD_LOADOUT];
    int combat_action = actions[HEAD_COMBAT];
    int attack_action = is_attack_action(combat_action) ? combat_action : ATTACK_NONE;

    int is_gmaul = (loadout_action == LOADOUT_GMAUL);
    if (is_gmaul) {
        attack_action = ATTACK_ATK;
    }

    int current_loadout = get_current_loadout(p);
    int in_mage_loadout = (current_loadout == LOADOUT_MAGE);
    int in_tank_loadout = (current_loadout == LOADOUT_TANK);
    if (attack_action == ATTACK_ATK && (in_mage_loadout || in_tank_loadout) && !is_gmaul) {
        attack_action = ATTACK_NONE;
    }

    if (attack_action == ATTACK_NONE && osrs_interaction_active(&p->interaction)) {
        AttackStyle weapon_style = get_slot_weapon_attack_style(p);
        if (weapon_style != ATTACK_STYLE_MAGIC) {
            attack_action = ATTACK_ATK;
        }
    }

    int attack_ready = can_attack_now(p);
    int dist = chebyshev_distance(p->x, p->y, t->x, t->y);

    AttackStyle attack_style = ATTACK_STYLE_NONE;
    int magic_type = 0;

    switch (attack_action) {
        case ATTACK_ATK:
            attack_style = get_slot_weapon_attack_style(p);
            break;
        case ATTACK_ICE:
            attack_style = ATTACK_STYLE_MAGIC;
            magic_type = 1;
            break;
        case ATTACK_BLOOD:
            attack_style = ATTACK_STYLE_MAGIC;
            magic_type = 2;
            break;
        default:
            break;
    }
    if (attack_action == ATTACK_ICE && !can_cast_ice_spell(p)) {
        attack_style = ATTACK_STYLE_NONE;
    }
    if (attack_action == ATTACK_BLOOD && !can_cast_blood_spell(p)) {
        attack_style = ATTACK_STYLE_NONE;
    }

    /* gmaul is instant: bypasses attack timer */
    int can_attack = attack_ready || (is_gmaul && is_granite_maul_attack_available(p));

    switch (attack_action) {
        case ATTACK_ATK:
            if (can_attack && attack_style != ATTACK_STYLE_NONE) {
                /* ATK with magic staff uses melee (staff bash) */
                AttackStyle actual_style = (attack_style == ATTACK_STYLE_MAGIC)
                    ? ATTACK_STYLE_MELEE
                    : attack_style;
                OsrsAttackReachQuery reach = pvp_attack_reach_query(
                    cmap, p, t, actual_style);
                int in_attack_range = osrs_attack_can_reach(&reach);
                if (in_attack_range) {
                    int is_special = p->spec_armed && is_special_ready(p, actual_style);
                    perform_attack(env, agent_idx, 1 - agent_idx, actual_style, is_special, 0, dist);
                    if (is_special)
                        osrs_spec_disarm(&p->spec_armed);
                    p->clicks_this_tick++;
                }
            }
            break;
        case ATTACK_ICE:
        case ATTACK_BLOOD:
            if (attack_ready && attack_style == ATTACK_STYLE_MAGIC) {
                int can_cast = (attack_action == ATTACK_ICE)
                    ? can_cast_ice_spell(p)
                    : can_cast_blood_spell(p);
                if (!can_cast) break;
                OsrsAttackReachQuery reach = pvp_attack_reach_query(
                    cmap, p, t, ATTACK_STYLE_MAGIC);
                if (osrs_attack_can_reach(&reach)) {
                    perform_attack(env, agent_idx, 1 - agent_idx, ATTACK_STYLE_MAGIC, 0, magic_type, dist);
                    p->clicks_this_tick++;
                }
            }
            break;
        default:
            break;
    }
}

static void execute_attacks(OsrsEnv* env, int agent_idx, int* actions) {
    execute_attack_combat(env, agent_idx, actions);
}

/**
 * Execute all actions for an agent (convenience for opponents).
 * For correct prayer timing, c_step calls execute_switches for both
 * players FIRST, then execute_attacks for both players.
 */
__attribute__((unused))
static void execute_actions(OsrsEnv* env, int agent_idx, int* actions) {
    execute_switches(env, agent_idx, actions);
    execute_attacks(env, agent_idx, actions);
}

static float calculate_reward(OsrsEnv* env, int agent_idx) {
    float reward = 0.0f;
    Player* p = &env->players[agent_idx];
    Player* t = &env->players[1 - agent_idx];
    const RewardShapingConfig* cfg = &env->shaping;

    // Sparse terminal reward: +1 win, 0 loss (forfeiting future rewards is the penalty)
    if (env->episode_over) {
        if (env->winner == agent_idx) {
            reward += 1.0f;
        }
    }

    // Always-on behavioral penalties (independent of reward_shaping toggle)

    // Prayer switch penalty: switched protection prayer but opponent didn't attack
    if (cfg->prayer_penalty_enabled && !t->just_attacked) {
        int overhead = env->last_executed_actions[agent_idx * NUM_ACTION_HEADS + HEAD_OVERHEAD];
        if (overhead == OVERHEAD_MAGE || overhead == OVERHEAD_RANGED || overhead == OVERHEAD_MELEE) {
            reward += cfg->prayer_switch_no_attack_penalty;
        }
    }

    // Progressive click penalty: linear ramp above threshold
    if (cfg->click_penalty_enabled && p->clicks_this_tick > cfg->click_penalty_threshold) {
        int excess = p->clicks_this_tick - cfg->click_penalty_threshold;
        reward += cfg->click_penalty_coef * (float)excess;
    }

    // Always-on positive signals (dense reward for bootstrapping learning)
    float base_hp = (float)p->base_hitpoints;
    if (p->damage_dealt_scale > 0.0f) {
        reward += p->damage_dealt_scale * base_hp * 0.005f;
    }
    if (t->just_attacked && p->player_prayed_correct) {
        reward += 0.01f;
    }

    if (!cfg->enabled) {
        return reward;
    }

    // Terminal shaping bonuses (only when shaping enabled)
    if (env->episode_over) {
        if (env->winner == agent_idx) {
            // KO bonus: opponent still had food — we killed through supplies
            if (t->food_count > 0 || t->karambwan_count > 0 || t->brew_doses > 0) {
                reward += cfg->ko_bonus;
            }
            // Proportional KO supplies bonus: linear in fraction of starting
            // supplies the opponent still had at death. Sweep this coef in
            // [0, ~0.5] to find if "fast KOs" matter as a training signal.
            float opp_total = (float)(t->food_count + t->karambwan_count
                                       + t->brew_doses + t->restore_doses
                                       + t->combat_potion_doses
                                       + t->ranged_potion_doses);
            float max_total = (float)(MAXED_FOOD_COUNT + MAXED_KARAMBWAN_COUNT
                                      + MAXED_BREW_DOSES + MAXED_RESTORE_DOSES
                                      + MAXED_COMBAT_POTION_DOSES
                                      + MAXED_RANGED_POTION_DOSES);
            if (max_total > 0.0f) {
                reward += cfg->ko_supplies_bonus_coef * (opp_total / max_total);
            }
        } else if (env->winner == (1 - agent_idx)) {
            // Wasted resources: we died with food left — failed to use supplies
            if (p->food_count > 0 || p->karambwan_count > 0 || p->brew_doses > 0) {
                reward += cfg->wasted_resources_penalty;
            }
        }
    }
    // Per-tick reward shaping
    float tick_shaping = 0.0f;

    // Damage dealt: reward aggression
    if (p->damage_dealt_scale > 0.0f) {
        float damage_hp = p->damage_dealt_scale * base_hp;
        tick_shaping += damage_hp * cfg->damage_dealt_coef;
        // Burst bonus: reward big hits that set up KOs
        if (damage_hp >= (float)cfg->damage_burst_threshold) {
            tick_shaping += (damage_hp - (float)cfg->damage_burst_threshold + 1.0f)
                          * cfg->damage_burst_bonus;
        }
    }

    // Damage received: small penalty
    if (p->damage_received_scale > 0.0f) {
        tick_shaping += p->damage_received_scale * base_hp * cfg->damage_received_coef;
    }

    // Correct defensive prayer: opponent attacked and we prayed correctly
    if (t->just_attacked) {
        if (p->player_prayed_correct) {
            tick_shaping += cfg->correct_prayer_bonus;
        } else {
            tick_shaping += cfg->wrong_prayer_penalty;
        }
    }

    // Off-prayer hit and offensive prayer checks: we attacked
    if (p->just_attacked) {
        if (!p->target_prayed_correct) {
            tick_shaping += cfg->off_prayer_hit_bonus;
        }

        // Bad behavior: melee attack when frozen and out of range
        if (p->attack_style_this_tick == ATTACK_STYLE_MELEE
            && p->frozen_ticks > 0 && !is_in_melee_range(p, t)) {
            tick_shaping += cfg->melee_frozen_penalty;
        }

        // Spec timing rewards
        if (p->used_special_this_tick) {
            // Off prayer: target NOT praying melee
            if (t->prayer != PRAYER_PROTECT_MELEE) {
                tick_shaping += cfg->spec_off_prayer_bonus;
            }
            // Low defence: target in mage gear (mystic has no melee def)
            AttackStyle target_style = get_slot_weapon_attack_style(t);
            if (target_style == ATTACK_STYLE_MAGIC) {
                tick_shaping += cfg->spec_low_defence_bonus;
            }
            // Low HP: target below 50%
            float target_hp_pct = (float)t->current_hitpoints / (float)t->base_hitpoints;
            if (target_hp_pct < 0.5f) {
                tick_shaping += cfg->spec_low_hp_bonus;
            }
        }

        // Bad behavior: magic attack without staff equipped
        if (p->attack_style_this_tick == ATTACK_STYLE_MAGIC) {
            AttackStyle weapon_style = get_slot_weapon_attack_style(p);
            if (weapon_style != ATTACK_STYLE_MAGIC) {
                tick_shaping += cfg->magic_no_staff_penalty;
            }
        }

        // Gear mismatch penalty: attacking with negative attack bonus for the style
        GearBonuses* gear = get_slot_gear_bonuses(p);
        int attack_bonus = 0;
        switch (p->attack_style_this_tick) {
            case ATTACK_STYLE_MAGIC:
                attack_bonus = gear->magic_attack;
                break;
            case ATTACK_STYLE_RANGED:
                attack_bonus = gear->ranged_attack;
                break;
            case ATTACK_STYLE_MELEE:
                // Use max of stab/slash/crush for melee
                attack_bonus = gear->slash_attack;
                if (gear->stab_attack > attack_bonus) attack_bonus = gear->stab_attack;
                if (gear->crush_attack > attack_bonus) attack_bonus = gear->crush_attack;
                break;
            default:
                break;
        }
        if (attack_bonus < 0) {
            tick_shaping += cfg->gear_mismatch_penalty;
        }
    }

    // Eating penalties (not attack-related)
    int ate_food = p->ate_food_this_tick;
    int ate_karam = p->ate_karambwan_this_tick;
    int ate_brew = p->ate_brew_this_tick;

    if (ate_food || ate_karam) {
        float hp_before = p->prev_hp_percent;
        // Premature eating: penalize eating above threshold
        if (hp_before > cfg->premature_eat_threshold) {
            tick_shaping += cfg->premature_eat_penalty;
        }
        // Wasted healing: penalize overflow past max HP
        float max_heal;
        if (ate_food) {
            max_heal = 20.0f / base_hp;  // Sharks heal 20
        } else {
            max_heal = 18.0f / base_hp;  // Karambwan heals 18
        }
        float wasted = hp_before + max_heal - 1.0f;
        if (wasted > 0.0f) {
            float wasted_hp = wasted * base_hp;
            tick_shaping += cfg->wasted_eat_penalty * wasted_hp;
        }
    }

    // Triple eat timing (shark + brew + karam = 54 HP)
    if (ate_food && ate_brew && ate_karam) {
        float hp_before = p->prev_hp_percent;
        float hp_threshold = 45.0f / base_hp;
        if (hp_before <= hp_threshold) {
            tick_shaping += cfg->smart_triple_eat_bonus;
        } else {
            float food_brew_heal = (20.0f + 16.0f) / base_hp;
            float hp_after_food_brew = hp_before + food_brew_heal;
            if (hp_after_food_brew > 1.0f) hp_after_food_brew = 1.0f;
            float missing_after = 1.0f - hp_after_food_brew;
            float karam_heal_norm = 18.0f / base_hp;
            float wasted_karam = karam_heal_norm - missing_after;
            if (wasted_karam > 0.0f) {
                float wasted_karam_hp = wasted_karam * base_hp;
                tick_shaping += cfg->wasted_triple_eat_penalty * wasted_karam_hp;
            }
        }
    }

    reward += tick_shaping * cfg->shaping_scale;

    // KO bonus and wasted resources are in the base terminal reward (not shaped)

    return reward;
}

#endif // OSRS_PVP_ACTIONS_H
