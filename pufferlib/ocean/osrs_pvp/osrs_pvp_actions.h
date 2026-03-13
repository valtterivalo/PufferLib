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

#include "osrs_pvp_types.h"
#include "osrs_pvp_items.h"
#include "osrs_pvp_gear.h"
#include "osrs_pvp_combat.h"
#include "osrs_pvp_movement.h"
#include "osrs_pvp_observations.h"  // For can_eat_food, can_use_potion, etc.

// ============================================================================
// PRAYER DRAIN CONSTANTS
// ============================================================================
// Drain effects derived from OSRS wiki: drain_effect = 36 / seconds_per_point
// drain_resistance = 60 + 2 * prayer_bonus (we use 0 prayer bonus for NH gear)

#define PRAYER_DRAIN_RESISTANCE_BASE 60
// NOTE: Hardcoded prayer bonus for NH gear (fury amulet +3, neitiznot helm +3)
// These are always equipped regardless of gear set. Could be computed dynamically
// from gear_bonuses if we add prayer_bonus field, but not worth the complexity.
#define PRAYER_BONUS 6

// Drain effects per tick for each prayer type
#define DRAIN_EFFECT_NONE 0
#define DRAIN_EFFECT_PROTECT_MAGIC 12   // 1 point per 3 seconds
#define DRAIN_EFFECT_PROTECT_RANGED 12
#define DRAIN_EFFECT_PROTECT_MELEE 12
#define DRAIN_EFFECT_SMITE 12           // 1 point per 3 seconds
#define DRAIN_EFFECT_REDEMPTION 6       // 1 point per 6 seconds
#define DRAIN_EFFECT_PIETY 24           // 1 point per 1.5 seconds
#define DRAIN_EFFECT_RIGOUR 24
#define DRAIN_EFFECT_AUGURY 24
#define DRAIN_EFFECT_LOW_OFFENSIVE 6    // rock skin tier: 1 point per 6 seconds

/** Get drain effect for an overhead prayer. */
static inline int get_overhead_drain_effect(OverheadPrayer prayer) {
    switch (prayer) {
        case PRAYER_NONE: return DRAIN_EFFECT_NONE;
        case PRAYER_PROTECT_MAGIC: return DRAIN_EFFECT_PROTECT_MAGIC;
        case PRAYER_PROTECT_RANGED: return DRAIN_EFFECT_PROTECT_RANGED;
        case PRAYER_PROTECT_MELEE: return DRAIN_EFFECT_PROTECT_MELEE;
        case PRAYER_SMITE: return DRAIN_EFFECT_SMITE;
        case PRAYER_REDEMPTION: return DRAIN_EFFECT_REDEMPTION;
        default: return DRAIN_EFFECT_NONE;
    }
}

/** Get drain effect for an offensive prayer. */
static inline int get_offensive_drain_effect(OffensivePrayer prayer) {
    switch (prayer) {
        case OFFENSIVE_PRAYER_NONE: return DRAIN_EFFECT_NONE;
        case OFFENSIVE_PRAYER_MELEE_LOW: return DRAIN_EFFECT_LOW_OFFENSIVE;
        case OFFENSIVE_PRAYER_RANGED_LOW: return DRAIN_EFFECT_LOW_OFFENSIVE;
        case OFFENSIVE_PRAYER_MAGIC_LOW: return DRAIN_EFFECT_LOW_OFFENSIVE;
        case OFFENSIVE_PRAYER_PIETY: return DRAIN_EFFECT_PIETY;
        case OFFENSIVE_PRAYER_RIGOUR: return DRAIN_EFFECT_RIGOUR;
        case OFFENSIVE_PRAYER_AUGURY: return DRAIN_EFFECT_AUGURY;
        default: return DRAIN_EFFECT_NONE;
    }
}

// ============================================================================
// CONSUMABLE ACTIONS
// ============================================================================

/**
 * Eat food (regular or karambwan).
 *
 * Food: heals based on HP level (capped at 22), no overheal.
 * Karambwan: heals 18, shares timer with food.
 *
 * @param p            Player eating
 * @param is_karambwan 1 for karambwan, 0 for regular food
 */
static void eat_food(Player* p, int is_karambwan) {
    if (is_karambwan) {
        if (p->karambwan_count <= 0 || p->karambwan_timer > 0) return;
        p->karambwan_count--;
        int hp_before = p->current_hitpoints;
        int heal_amount = 18;
        int max_hp = p->base_hitpoints;
        int actual_heal = max_int(0, min_int(heal_amount, max_hp - hp_before));
        int waste = heal_amount - actual_heal;
        p->current_hitpoints = clamp(hp_before + heal_amount, 0, max_hp);
        p->last_karambwan_heal = actual_heal;
        p->last_karambwan_waste = waste;
        p->karambwan_timer = 2;  // 2-tick self-cooldown: karam, 1, Ready
        p->food_timer = 3;      // 3-tick cross-lockout on food
        p->potion_timer = 3;    // 3-tick cross-lockout on potions
        p->ate_karambwan_this_tick = 1;  // Track for reward shaping
        // Eating always delays attack timer (clamp to 0 so idle-negative timer doesn't skip delay)
        int combat_ticks = p->has_attack_timer ? max_int(p->attack_timer_uncapped, 0) : 0;
        p->attack_timer = combat_ticks + 2;
        p->attack_timer_uncapped = combat_ticks + 2;
        p->has_attack_timer = 1;
    } else {
        if (p->food_count <= 0 || p->food_timer > 0) return;
        p->food_count--;
        // Sharks heal 20, capped by missing HP
        int heal_amount = 20;
        int hp_before = p->current_hitpoints;
        int max_hp = p->base_hitpoints;
        int actual_heal = min_int(heal_amount, max_hp - hp_before);
        int waste = heal_amount - actual_heal;
        p->current_hitpoints = hp_before + actual_heal;
        p->last_food_heal = actual_heal;
        p->last_food_waste = waste;
        p->food_timer = 3;
        p->ate_food_this_tick = 1;  // Track for reward shaping
        // Eating always delays attack timer (clamp to 0 so idle-negative timer doesn't skip delay)
        int combat_ticks = p->has_attack_timer ? max_int(p->attack_timer_uncapped, 0) : 0;
        p->attack_timer = combat_ticks + 3;
        p->attack_timer_uncapped = combat_ticks + 3;
        p->has_attack_timer = 1;
    }
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
            int def_boost = (int)floorf(2.0f + (0.20f * p->base_defence));
            int hp_boost = (int)floorf(2.0f + (0.15f * p->base_hitpoints));
            int hp_before = p->current_hitpoints;
            int max_hp = p->base_hitpoints + hp_boost;
            int actual_heal = max_int(0, min_int(hp_boost, max_hp - hp_before));
            int waste = hp_boost - actual_heal;
            int def_before = p->current_defence;
            int max_def = p->is_lms ? p->base_defence : p->base_defence + def_boost;
            p->current_defence = clamp(def_before + def_boost, 0, max_def);
            p->current_hitpoints = clamp(hp_before + hp_boost, 0, max_hp);
            p->last_brew_heal = actual_heal;
            p->last_brew_waste = waste;
            int att_down = (int)floorf(0.10f * p->current_attack) + 2;
            int str_down = (int)floorf(0.10f * p->current_strength) + 2;
            int mag_down = (int)floorf(0.10f * p->current_magic) + 2;
            int rng_down = (int)floorf(0.10f * p->current_ranged) + 2;
            p->current_attack = clamp(p->current_attack - att_down, 0, 255);
            p->current_strength = clamp(p->current_strength - str_down, 0, 255);
            p->current_magic = clamp(p->current_magic - mag_down, 0, 255);
            p->current_ranged = clamp(p->current_ranged - rng_down, 0, 255);
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
            int prayer_restore = 8 + (p->base_prayer / 4);
            p->current_prayer = clamp(p->current_prayer + prayer_restore, 0, p->base_prayer);
            int atk_restore = 8 + (p->base_attack / 4);
            int str_restore = 8 + (p->base_strength / 4);
            int def_restore = 8 + (p->base_defence / 4);
            int rng_restore = 8 + (p->base_ranged / 4);
            int mag_restore = 8 + (p->base_magic / 4);
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
            int atk_boost = (int)floorf(p->base_attack * 0.15f) + 5;
            int str_boost = (int)floorf(p->base_strength * 0.15f) + 5;
            int def_boost = (int)floorf(p->base_defence * 0.15f) + 5;
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
            int rng_boost = (int)floorf(p->base_ranged * 0.10f) + 4;
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

// ============================================================================
// TIMER UPDATES
// ============================================================================

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

    // Prayer drain: accumulate drain effect, subtract resistance when threshold met
    // LMS has no prayer drain (prayer points are unlimited)
    if (p->current_prayer > 0 && !p->is_lms) {
        int drain_effect = get_overhead_drain_effect(p->prayer)
                         + get_offensive_drain_effect(p->offensive_prayer);
        if (drain_effect > 0) {
            int drain_resistance = PRAYER_DRAIN_RESISTANCE_BASE + 2 * PRAYER_BONUS;
            p->prayer_drain_counter += drain_effect;
            // Can drain multiple points per tick if counter exceeds resistance multiple times
            while (p->prayer_drain_counter >= drain_resistance) {
                p->current_prayer--;
                p->prayer_drain_counter -= drain_resistance;
                if (p->current_prayer <= 0) {
                    p->current_prayer = 0;
                    p->prayer_drain_counter = 0;
                    // Turn off prayers when out of prayer points
                    p->prayer = PRAYER_NONE;
                    p->offensive_prayer = OFFENSIVE_PRAYER_NONE;
                    break;
                }
            }
        }
    }

    if (p->run_energy < 100 && (!p->is_moving || !p->is_running)) {
        p->run_recovery_ticks += 1;
        if (p->run_recovery_ticks >= RUN_ENERGY_RECOVER_TICKS) {
            p->run_energy = clamp(p->run_energy + 1, 0, 100);
            p->run_recovery_ticks = 0;
        }
    } else {
        p->run_recovery_ticks = 0;
    }

    int has_lightbearer = is_lightbearer_equipped(p);
    if (has_lightbearer != p->was_lightbearer_equipped) {
        if (has_lightbearer) {
            if (p->special_regen_ticks > 25) {
                p->special_regen_ticks = 0;
            }
        } else {
            p->special_regen_ticks = 0;
        }
        p->was_lightbearer_equipped = has_lightbearer;
    }
    if (p->spec_regen_active && p->special_energy < 100) {
        int regen_interval = has_lightbearer ? 25 : 50;
        p->special_regen_ticks += 1;
        if (p->special_regen_ticks >= regen_interval) {
            p->special_energy = clamp(p->special_energy + 10, 0, 100);
            p->special_regen_ticks = 0;
        }
    } else if (p->spec_regen_active) {
        p->special_regen_ticks = 0;
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
    p->magic_type_this_tick = 0;
    p->used_special_this_tick = 0;
    p->ate_food_this_tick = 0;
    p->ate_karambwan_this_tick = 0;
    p->ate_brew_this_tick = 0;
    p->cast_veng_this_tick = 0;
    p->clicks_this_tick = 0;
}

// ============================================================================
// LOADOUT-BASED ACTION EXECUTION
// ============================================================================

// Forward declarations for phased execution
static void execute_switches(OsrsPvp* env, int agent_idx, int* actions);
static void execute_attacks(OsrsPvp* env, int agent_idx, int* actions);

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
static void execute_switches(OsrsPvp* env, int agent_idx, int* actions) {
    Player* p = &env->players[agent_idx];
    Player* t = &env->players[1 - agent_idx];
    const CollisionMap* cmap = (const CollisionMap*)env->collision_map;

    p->consumable_used_this_tick = 0;

    // =========================================================================
    // PHASE 1: OVERHEAD PRAYER - must happen first so attacks see new prayer
    // =========================================================================

    int overhead_action = actions[HEAD_OVERHEAD];
    OverheadPrayer prev_prayer = p->prayer;
    switch (overhead_action) {
        case OVERHEAD_MAGE:
            if (p->current_prayer > 0) p->prayer = PRAYER_PROTECT_MAGIC;
            break;
        case OVERHEAD_RANGED:
            if (p->current_prayer > 0) p->prayer = PRAYER_PROTECT_RANGED;
            break;
        case OVERHEAD_MELEE:
            if (p->current_prayer > 0) p->prayer = PRAYER_PROTECT_MELEE;
            break;
        case OVERHEAD_SMITE:
            if (p->current_prayer > 0 && !env->is_lms) p->prayer = PRAYER_SMITE;
            break;
        case OVERHEAD_REDEMPTION:
            if (p->current_prayer > 0 && !env->is_lms) p->prayer = PRAYER_REDEMPTION;
            break;
    }
    if (p->prayer != prev_prayer) p->clicks_this_tick++;

    // =========================================================================
    // PHASE 2: LOADOUT SWITCH - equips dynamic gear slots, returns # changed
    // =========================================================================

    int loadout_action = actions[HEAD_LOADOUT];
    p->clicks_this_tick += apply_loadout(p, loadout_action);

    // =========================================================================
    // PHASE 3: AUTO-OFFENSIVE PRAYER
    // Loadout determines prayer if switching, attack head is fallback for KEEP
    // =========================================================================

    if (p->current_prayer > 0 && p->base_prayer >= 70) {
        AttackStyle pray_style = ATTACK_STYLE_NONE;
        if (loadout_action != LOADOUT_KEEP) {
            switch (loadout_action) {
                case LOADOUT_MELEE:
                case LOADOUT_SPEC_MELEE:
                case LOADOUT_GMAUL:
                    pray_style = ATTACK_STYLE_MELEE;
                    break;
                case LOADOUT_RANGE:
                case LOADOUT_SPEC_RANGE:
                    pray_style = ATTACK_STYLE_RANGED;
                    break;
                case LOADOUT_MAGE:
                case LOADOUT_TANK:
                case LOADOUT_SPEC_MAGIC:
                    pray_style = ATTACK_STYLE_MAGIC;
                    break;
            }
        } else {
            int combat_action_val = actions[HEAD_COMBAT];
            pray_style = resolve_attack_style_for_action(p, combat_action_val);
        }
        switch (pray_style) {
            case ATTACK_STYLE_MELEE:
                p->offensive_prayer = OFFENSIVE_PRAYER_PIETY;
                break;
            case ATTACK_STYLE_RANGED:
                p->offensive_prayer = OFFENSIVE_PRAYER_RIGOUR;
                break;
            case ATTACK_STYLE_MAGIC:
                p->offensive_prayer = OFFENSIVE_PRAYER_AUGURY;
                break;
            default:
                break;
        }
    }

    // =========================================================================
    // PHASE 4: CONSUMABLES - eating delays attack timer
    // =========================================================================

    int food_action = actions[HEAD_FOOD];
    if (food_action == FOOD_EAT && can_eat_food(p)) {
        eat_food(p, 0);
        p->consumable_used_this_tick = 1;
        p->clicks_this_tick++;
    }

    int potion_action = actions[HEAD_POTION];
    switch (potion_action) {
        case POTION_BREW:
            if (can_use_potion(p, 1) && can_use_brew_boost(p)) {
                drink_potion(p, 1);
                p->consumable_used_this_tick = 1;
                p->clicks_this_tick++;
            }
            break;
        case POTION_RESTORE:
            if (can_use_potion(p, 2) && can_restore_stats(p)) {
                drink_potion(p, 2);
                p->consumable_used_this_tick = 1;
                p->clicks_this_tick++;
            }
            break;
        case POTION_COMBAT:
            if (can_use_potion(p, 3) && can_boost_combat_skills(p)) {
                drink_potion(p, 3);
                p->consumable_used_this_tick = 1;
                p->clicks_this_tick++;
            }
            break;
        case POTION_RANGED:
            if (can_use_potion(p, 4) && can_boost_ranged(p)) {
                drink_potion(p, 4);
                p->consumable_used_this_tick = 1;
                p->clicks_this_tick++;
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
    }

    // =========================================================================
    // PHASE 5: MOVEMENT
    // =========================================================================

    int combat_action = actions[HEAD_COMBAT];
    int is_spec_loadout = (loadout_action == LOADOUT_SPEC_MELEE ||
                           loadout_action == LOADOUT_SPEC_RANGE ||
                           loadout_action == LOADOUT_SPEC_MAGIC ||
                           loadout_action == LOADOUT_GMAUL);
    int move_action = (!is_spec_loadout && is_move_action(combat_action))
                      ? combat_action : MOVE_NONE;

    int farcast_dist = 0;
    switch (move_action) {
        case MOVE_ADJACENT:
            process_movement(p, t, 1, 0, cmap);
            p->clicks_this_tick++;
            break;
        case MOVE_UNDER:
            process_movement(p, t, 2, 0, cmap);
            p->clicks_this_tick++;
            break;
        case MOVE_DIAGONAL:
            process_movement(p, t, 4, 0, cmap);
            p->clicks_this_tick++;
            break;
        case MOVE_FARCAST_2:
        case MOVE_FARCAST_3:
        case MOVE_FARCAST_4:
        case MOVE_FARCAST_5:
        case MOVE_FARCAST_6:
        case MOVE_FARCAST_7:
            farcast_dist = move_action - MOVE_FARCAST_2 + 2;
            process_movement(p, t, 3, farcast_dist, cmap);
            p->clicks_this_tick++;
            break;
        default:
            break;
    }

    // =========================================================================
    // PHASE 6: VENGEANCE
    // =========================================================================

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

/**
 * Execute attack-phase actions for an agent (Phase 2).
 *
 * Processes attacks AFTER all switches have been applied for BOTH players.
 * SPEC loadout overrides the ATTACK head (atomic spec = equip + enable + attack).
 */
/**
 * Attack movement phase: auto-walk to melee range + step out from same tile.
 * Called for ALL players before any attack combat checks, so positions are
 * fully resolved before range checks happen (matches OSRS tick processing).
 */
static void execute_attack_movement(OsrsPvp* env, int agent_idx, int* actions) {
    Player* p = &env->players[agent_idx];
    Player* t = &env->players[1 - agent_idx];
    const CollisionMap* cmap = (const CollisionMap*)env->collision_map;

    int loadout_action = actions[HEAD_LOADOUT];
    int combat_action = actions[HEAD_COMBAT];
    int attack_action = is_attack_action(combat_action) ? combat_action : ATTACK_NONE;
    int move_action = is_move_action(combat_action) ? combat_action : MOVE_NONE;

    int is_spec_attack = (loadout_action == LOADOUT_SPEC_MELEE ||
                          loadout_action == LOADOUT_SPEC_RANGE ||
                          loadout_action == LOADOUT_SPEC_MAGIC ||
                          loadout_action == LOADOUT_GMAUL);
    if (is_spec_attack) {
        attack_action = ATTACK_ATK;
        move_action = MOVE_NONE;
    }

    int current_loadout = get_current_loadout(p);
    int in_mage_loadout = (current_loadout == LOADOUT_MAGE);
    int in_tank_loadout = (current_loadout == LOADOUT_TANK);
    if (attack_action == ATTACK_ATK && (in_mage_loadout || in_tank_loadout) && !is_spec_attack) {
        attack_action = ATTACK_NONE;
    }

    int has_attack = (attack_action != ATTACK_NONE);
    int dist = chebyshev_distance(p->x, p->y, t->x, t->y);

    AttackStyle attack_style = ATTACK_STYLE_NONE;
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
    if (attack_action == ATTACK_ICE && !can_cast_ice_spell(p)) {
        attack_style = ATTACK_STYLE_NONE;
    }
    if (attack_action == ATTACK_BLOOD && !can_cast_blood_spell(p)) {
        attack_style = ATTACK_STYLE_NONE;
    }

    p->did_attack_auto_move = 0;

    // Auto-move into melee range if melee attack queued
    if (has_attack && move_action == MOVE_NONE && can_move(p)) {
        if (attack_style == ATTACK_STYLE_MELEE && !is_in_melee_range(p, t)) {
            int adj_x, adj_y;
            if (select_closest_adjacent_tile(p, t->x, t->y, &adj_x, &adj_y, cmap)) {
                set_destination(p, adj_x, adj_y, cmap);
            }
            p->did_attack_auto_move = 1;
            dist = chebyshev_distance(p->x, p->y, t->x, t->y);
        }
    }

    // Step out from same tile (OSRS behavior: can't attack from same tile)
    if (has_attack && dist == 0 && can_move(p)) {
        step_out_from_same_tile(p, t, cmap);
    }
}

/**
 * Attack combat phase: range check + perform attack + post-attack chase.
 * Called for ALL players AFTER all attack movements have resolved, so
 * dist is computed from final positions (fixes PID-dependent same-tile bug).
 */
static void execute_attack_combat(OsrsPvp* env, int agent_idx, int* actions) {
    Player* p = &env->players[agent_idx];
    Player* t = &env->players[1 - agent_idx];
    const CollisionMap* cmap = (const CollisionMap*)env->collision_map;

    int loadout_action = actions[HEAD_LOADOUT];
    int combat_action = actions[HEAD_COMBAT];
    int attack_action = is_attack_action(combat_action) ? combat_action : ATTACK_NONE;
    int move_action = is_move_action(combat_action) ? combat_action : MOVE_NONE;

    int is_spec_attack = (loadout_action == LOADOUT_SPEC_MELEE ||
                          loadout_action == LOADOUT_SPEC_RANGE ||
                          loadout_action == LOADOUT_SPEC_MAGIC ||
                          loadout_action == LOADOUT_GMAUL);
    if (is_spec_attack) {
        attack_action = ATTACK_ATK;
        move_action = MOVE_NONE;
    }

    int current_loadout = get_current_loadout(p);
    int in_mage_loadout = (current_loadout == LOADOUT_MAGE);
    int in_tank_loadout = (current_loadout == LOADOUT_TANK);
    if (attack_action == ATTACK_ATK && (in_mage_loadout || in_tank_loadout) && !is_spec_attack) {
        attack_action = ATTACK_NONE;
    }

    int attack_ready = can_attack_now(p);
    int has_attack = (attack_action != ATTACK_NONE);
    // Recompute dist from CURRENT positions (after all movements resolved)
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

    // Gmaul is instant: bypasses attack timer
    int is_gmaul = (loadout_action == LOADOUT_GMAUL);
    int can_attack = attack_ready || (is_gmaul && is_granite_maul_attack_available(p));

    switch (attack_action) {
        case ATTACK_ATK:
            if (can_attack && attack_style != ATTACK_STYLE_NONE) {
                // ATK with magic staff uses melee (staff bash)
                AttackStyle actual_style = (attack_style == ATTACK_STYLE_MAGIC)
                    ? ATTACK_STYLE_MELEE
                    : attack_style;
                // Melee uses cardinal adjacency check; ranged uses Chebyshev range
                int in_attack_range = 0;
                if (actual_style == ATTACK_STYLE_MELEE) {
                    in_attack_range = is_in_melee_range(p, t);
                } else {
                    int range = get_attack_range(p, actual_style);
                    in_attack_range = (dist > 0 && dist <= range);
                }
                if (in_attack_range) {
                    int is_special = is_spec_attack && is_special_ready(p, actual_style);
                    perform_attack(env, agent_idx, 1 - agent_idx, actual_style, is_special, 0, dist);
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
                int range = get_attack_range(p, ATTACK_STYLE_MAGIC);
                if (dist > 0 && dist <= range) {
                    perform_attack(env, agent_idx, 1 - agent_idx, ATTACK_STYLE_MAGIC, 0, magic_type, dist);
                    p->clicks_this_tick++;
                }
            }
            break;
        default:
            break;
    }

    // Auto-walk to target if attack queued but out of range (post-attack chase)
    if (has_attack && move_action == MOVE_NONE && can_move(p) && !p->did_attack_auto_move) {
        int in_range = 0;
        switch (attack_style) {
            case ATTACK_STYLE_MELEE:
                in_range = is_in_melee_range(p, t);
                break;
            case ATTACK_STYLE_RANGED: {
                int range = get_attack_range(p, ATTACK_STYLE_RANGED);
                in_range = (dist <= range);
                break;
            }
            case ATTACK_STYLE_MAGIC: {
                int range = get_attack_range(p, ATTACK_STYLE_MAGIC);
                in_range = (dist <= range);
                break;
            }
            default:
                in_range = 1;
                break;
        }
        if (!in_range) {
            move_toward_target(p, t, cmap);
        }
    }
}

/**
 * Legacy wrapper: runs both attack phases sequentially for a single player.
 * Used by execute_actions (scripted opponent convenience function).
 * For correct PID-independent behavior in pvp_step, call execute_attack_movement
 * for ALL players first, then execute_attack_combat for ALL players.
 */
static void execute_attacks(OsrsPvp* env, int agent_idx, int* actions) {
    execute_attack_movement(env, agent_idx, actions);
    execute_attack_combat(env, agent_idx, actions);
}

/**
 * Execute all actions for an agent (convenience for opponents).
 * For correct prayer timing, c_step calls execute_switches for both
 * players FIRST, then execute_attacks for both players.
 */
static void execute_actions(OsrsPvp* env, int agent_idx, int* actions) {
    execute_switches(env, agent_idx, actions);
    execute_attacks(env, agent_idx, actions);
}

// ============================================================================
// REWARD CALCULATION
// ============================================================================

/**
 * Calculate reward for an agent.
 *
 * Sparse terminal signal:
 * - +1.0 for win
 * - -0.5 for loss
 * - 0 for ongoing ticks
 *
 * When shaping is enabled, adds per-tick reward shaping (scaled by shaping_scale):
 * - Damage dealt/received
 * - Defensive prayer correctness
 * - Off-prayer hits
 * - Eating penalties (premature, wasted)
 * - Spec timing bonuses
 * - Bad behavior penalties (melee frozen, magic no staff)
 * - Terminal shaping (KO bonus, wasted resources penalty)
 */
static float calculate_reward(OsrsPvp* env, int agent_idx) {
    float reward = 0.0f;
    Player* p = &env->players[agent_idx];
    Player* t = &env->players[1 - agent_idx];
    const RewardShapingConfig* cfg = &env->shaping;

    // Sparse terminal reward: +1 win, -1 loss
    if (env->episode_over) {
        if (env->winner == agent_idx) {
            reward += 1.0f;
        } else if (env->winner == (1 - agent_idx)) {
            reward += -1.0f;
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
        } else if (env->winner == (1 - agent_idx)) {
            // Wasted resources: we died with food left — failed to use supplies
            if (p->food_count > 0 || p->karambwan_count > 0 || p->brew_doses > 0) {
                reward += cfg->wasted_resources_penalty;
            }
        }
    }

    // ==========================================================================
    // Per-tick reward shaping
    // ==========================================================================
    float tick_shaping = 0.0f;
    float base_hp = (float)p->base_hitpoints;

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

    // NOTE: prayer switch penalty moved above !cfg->enabled gate (always-on).
    // Not duplicated here to avoid double-counting when shaping is enabled.

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
