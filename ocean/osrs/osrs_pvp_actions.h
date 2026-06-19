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

static int pvp_can_use_inventory_consumable(Player* p, OsrsConsumableKind kind) {
    switch (kind) {
        case OSRS_CONSUMABLE_SHARK_FOOD:
            return can_eat_food(p);
        case OSRS_CONSUMABLE_KARAMBWAN:
            return can_eat_karambwan(p);
        case OSRS_CONSUMABLE_BREW:
            return can_use_potion(p, POTION_BREW) && can_use_brew_boost(p);
        case OSRS_CONSUMABLE_SUPER_RESTORE:
            return can_use_potion(p, POTION_RESTORE) && can_restore_stats(p);
        case OSRS_CONSUMABLE_SUPER_COMBAT:
            return can_use_potion(p, POTION_COMBAT) && can_boost_combat_skills(p);
        case OSRS_CONSUMABLE_RANGING:
            return can_use_potion(p, POTION_RANGED) && can_boost_ranged(p);
        case OSRS_CONSUMABLE_NONE:
            return 0;
        default:
            fprintf(stderr, "pvp_can_use_inventory_consumable: unsupported kind %d\n", (int)kind);
            abort();
    }
}

static void pvp_apply_inventory_consumable(Player* p, OsrsConsumableKind kind) {
    switch (kind) {
        case OSRS_CONSUMABLE_SHARK_FOOD:
            eat_food(p, 0);
            return;
        case OSRS_CONSUMABLE_KARAMBWAN:
            eat_food(p, 1);
            return;
        case OSRS_CONSUMABLE_BREW:
            drink_potion(p, POTION_BREW);
            return;
        case OSRS_CONSUMABLE_SUPER_RESTORE:
            drink_potion(p, POTION_RESTORE);
            return;
        case OSRS_CONSUMABLE_SUPER_COMBAT:
            drink_potion(p, POTION_COMBAT);
            return;
        case OSRS_CONSUMABLE_RANGING:
            drink_potion(p, POTION_RANGED);
            return;
        default:
            fprintf(stderr, "pvp_apply_inventory_consumable: unsupported kind %d\n", (int)kind);
            abort();
    }
}

static int pvp_inventory_interaction_action(OsrsClickAction action) {
    switch (action) {
        case OSRS_CLICK_EQUIP:
            return OSRS_IACT_EQUIP;
        case OSRS_CLICK_EAT:
            return OSRS_IACT_EAT;
        case OSRS_CLICK_DRINK:
            return OSRS_IACT_DRINK;
        case OSRS_CLICK_NONE:
            return OSRS_IACT_NONE;
        default:
            fprintf(stderr, "pvp_inventory_interaction_action: invalid action %d\n", (int)action);
            abort();
    }
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
    p->expected_damage_dealt_tick = 0.0f;
    p->expected_damage_received_tick = 0.0f;
    p->expected_damage_prevented_tick = 0.0f;
    p->ko_chance_prob_tick = 0.0f;
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
    p->attack_intent_pre_move_dist = 0;
    p->attack_style_this_tick = ATTACK_STYLE_NONE;
    p->attack_weapon_this_tick = ITEM_NONE;
    p->render_attack_target_this_tick = osrs_render_target_none();
    p->magic_type_this_tick = 0;
    p->hit_spell_type = 0;
    p->used_special_this_tick = 0;
    p->ate_food_this_tick = 0;
    p->ate_karambwan_this_tick = 0;
    p->ate_brew_this_tick = 0;
    p->cast_veng_this_tick = 0;
    p->clicks_this_tick = 0;
    p->weapon_equipped_this_tick = 0;
}

static void execute_switches(OsrsEnv* env, int agent_idx, const int* actions);
static void execute_attacks(OsrsEnv* env, int agent_idx, const int* actions);

static inline AttackStyle pvp_target_click_attack_style(Player* p) {
    AttackStyle weapon_style = get_slot_weapon_attack_style(p);
    return weapon_style == ATTACK_STYLE_MAGIC
        ? ATTACK_STYLE_MELEE
        : weapon_style;
}

static inline AttackStyle resolve_attack_style_for_action(Player* p, int attack_action) {
    if (attack_action == ATTACK_ATK) return pvp_target_click_attack_style(p);
    if (is_spell_attack_action(attack_action)) return ATTACK_STYLE_MAGIC;
    return ATTACK_STYLE_NONE;
}

static inline void pvp_record_selected_attack_style(Player* p, AttackStyle style) {
    switch (style) {
        case ATTACK_STYLE_MELEE:
            p->selected_melee_attack_attempts++;
            break;
        case ATTACK_STYLE_RANGED:
            p->selected_ranged_attack_attempts++;
            break;
        case ATTACK_STYLE_MAGIC:
            p->selected_magic_attack_attempts++;
            break;
        default:
            break;
    }
}

static int execute_inventory_clicks(OsrsEnv* env, int agent_idx, const int* actions) {
    Player* p = &env->players[agent_idx];
    int clicks = 0;
    OsrsInventoryView view;
    OsrsInventoryClickEvent events[OSRS_INVENTORY_SIZE];
    osrs_inventory_view_build(p, &view);
    int event_count = osrs_inventory_click_collect_events(
        &view,
        actions + HEAD_INVENTORY_0,
        PVP_INVENTORY_CLICKS_PER_TICK,
        events,
        OSRS_INVENTORY_SIZE);

    for (int i = 0; i < event_count; i++) {
        OsrsInventoryClickEvent event = events[i];
        switch (event.resolution.click_action) {
            case OSRS_CLICK_EQUIP: {
                p->equip_click_attempts++;
                if (!osrs_player_can_equip_from_inventory_slot(p, event.inventory_slot) &&
                        !osrs_player_inventory_has_item(p, event.cell.item_idx)) {
                    continue;
                }
                uint8_t old_weapon = p->equipped[GEAR_SLOT_WEAPON];
                if (!osrs_player_equip_command_item(
                        p, event.inventory_slot, event.cell.item_idx)) {
                    continue;
                }
                p->equip_click_successes++;
                if (p->equipped[GEAR_SLOT_WEAPON] != old_weapon)
                    p->weapon_equipped_this_tick = 1;
                p->clicks_this_tick++;
                osrs_interaction_check_interrupt(&p->interaction, OSRS_IACT_EQUIP);
                clicks++;
                break;
            }
            case OSRS_CLICK_EAT:
            case OSRS_CLICK_DRINK:
                if (!pvp_can_use_inventory_consumable(
                        p, event.resolution.consumable_kind)) {
                    break;
                }
                pvp_apply_inventory_consumable(p, event.resolution.consumable_kind);
                p->consumable_used_this_tick = 1;
                p->clicks_this_tick++;
                osrs_interaction_check_interrupt(
                    &p->interaction,
                    pvp_inventory_interaction_action(event.resolution.click_action));
                clicks++;
                break;
            case OSRS_CLICK_NONE:
                break;
            default:
                fprintf(stderr, "execute_inventory_clicks: invalid action %d\n",
                    (int)event.resolution.click_action);
                abort();
        }
    }
    return clicks;
}

static void execute_special_action(Player* p, int special_action) {
    switch (special_action) {
        case SPECIAL_NOOP:
            return;
        case SPECIAL_ARM: {
            p->special_arm_attempts++;
            uint8_t weapon = p->equipped[GEAR_SLOT_WEAPON];
            int cost = osrs_spec_cost(weapon);
            if (cost > 0 && p->special_energy >= cost && !p->spec_armed) {
                p->spec_armed = 1;
                p->special_arm_successes++;
                p->clicks_this_tick++;
            }
            return;
        }
        case SPECIAL_DISARM:
            if (p->spec_armed) {
                p->spec_armed = 0;
                p->clicks_this_tick++;
            }
            return;
        default:
            fprintf(stderr, "execute_special_action: invalid action %d\n", special_action);
            abort();
    }
}

static void execute_switches(OsrsEnv* env, int agent_idx, const int* actions) {
    Player* p = &env->players[agent_idx];
    const CollisionMap* cmap = (const CollisionMap*)env->collision_map;

    p->consumable_used_this_tick = 0;

    int overhead_action = actions[HEAD_OVERHEAD];
    int offensive_action = actions[HEAD_OFFENSIVE];

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
    execute_inventory_clicks(env, agent_idx, actions);
    execute_special_action(p, actions[HEAD_SPECIAL]);
    int combat_action = actions[HEAD_ATTACK];
    int head_move = actions[HEAD_MOVE];

    int command_issued = 0;
    if (head_move > 0 && head_move < MOVE_DIM) {
        pvp_set_walk_dest_from_head_move(env, agent_idx, head_move);
        command_issued = 1;
    } else if (is_move_action(combat_action)) {
        command_issued = pvp_set_walk_dest_from_legacy_target_move(
            env, agent_idx, combat_action, cmap);
    }
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
    const int* actions
) {
    Player* p = &env->players[agent_idx];

    int combat_action = actions[HEAD_ATTACK];
    int attack_action = is_attack_action(combat_action) ? combat_action : ATTACK_NONE;

    AttackStyle attack_style = ATTACK_STYLE_NONE;
    if (attack_action != ATTACK_NONE) {
        attack_style = resolve_attack_style_for_action(p, attack_action);
    } else if (osrs_interaction_active(&p->interaction)) {
        attack_style = pvp_target_click_attack_style(p);
    }
    if (is_spell_attack_action(attack_action) &&
            !pvp_spell_action_can_cast(p, attack_action)) {
        attack_style = ATTACK_STYLE_NONE;
    }
    if (attack_style != ATTACK_STYLE_NONE) {
        Player* target = &env->players[1 - agent_idx];
        p->attack_intent_pre_move_dist = chebyshev_distance(
            p->x, p->y, target->x, target->y);
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

static void execute_attack_combat(OsrsEnv* env, int agent_idx, const int* actions) {
    Player* p = &env->players[agent_idx];
    Player* t = &env->players[1 - agent_idx];
    const CollisionMap* cmap = (const CollisionMap*)env->collision_map;

    int combat_action = actions[HEAD_ATTACK];
    int attack_action = is_attack_action(combat_action) ? combat_action : ATTACK_NONE;
    int selected_attack_action = attack_action;

    if (attack_action == ATTACK_NONE && osrs_interaction_active(&p->interaction)) {
        AttackStyle target_click_style = pvp_target_click_attack_style(p);
        if (target_click_style != ATTACK_STYLE_NONE) {
            attack_action = ATTACK_ATK;
        }
    }

    int attack_ready = can_attack_now(p);
    int dist = chebyshev_distance(p->x, p->y, t->x, t->y);

    AttackStyle attack_style = ATTACK_STYLE_NONE;
    int magic_type = 0;

    if (attack_action == ATTACK_ATK) {
        attack_style = pvp_target_click_attack_style(p);
    } else if (is_spell_attack_action(attack_action)) {
        PvpAncientSpellProfile spell_profile =
            pvp_spell_profile_for_action(attack_action);
        if (pvp_spell_profile_active(spell_profile)) {
            attack_style = ATTACK_STYLE_MAGIC;
            magic_type = spell_profile.visual_spell;
        }
    }
    if (is_spell_attack_action(attack_action) &&
            !pvp_spell_action_can_cast(p, attack_action)) {
        attack_style = ATTACK_STYLE_NONE;
    }
    if (selected_attack_action != ATTACK_NONE && attack_style != ATTACK_STYLE_NONE) {
        pvp_record_selected_attack_style(p, attack_style);
    }

    int gmaul_spec_ready = p->spec_armed &&
        p->equipped[GEAR_SLOT_WEAPON] == ITEM_GRANITE_MAUL &&
        is_granite_maul_attack_available(p);
    int can_attack = attack_ready || gmaul_spec_ready;

    switch (attack_action) {
        case ATTACK_ATK:
            p->target_click_attempts++;
            p->target_click_pre_move_dist_sum += p->attack_intent_pre_move_dist;
            p->target_click_post_move_dist_sum += dist;
            if (can_attack && attack_style != ATTACK_STYLE_NONE) {
                OsrsAttackReachQuery reach = pvp_attack_reach_query(
                    cmap, p, t, attack_style);
                int in_attack_range = osrs_attack_can_reach(&reach);
                if (in_attack_range) {
                    int is_special = p->spec_armed && is_special_ready(p, attack_style);
                    perform_attack(env, agent_idx, 1 - agent_idx, attack_style, is_special, 0, dist);
                    p->target_click_successes++;
                    p->target_click_success_pre_move_dist_sum += p->attack_intent_pre_move_dist;
                    p->target_click_success_post_move_dist_sum += dist;
                    p->weapon_attack_successes++;
                    if (p->weapon_equipped_this_tick)
                        p->attack_after_equip_successes++;
                    if (is_special && p->weapon_equipped_this_tick)
                        p->spec_after_equip_successes++;
                    if (is_special)
                        osrs_spec_disarm(&p->spec_armed);
                    p->clicks_this_tick++;
                }
            }
            break;
        case ATTACK_ICE_RUSH:
        case ATTACK_ICE_BURST:
        case ATTACK_ICE_BLITZ:
        case ATTACK_ICE_BARRAGE:
        case ATTACK_BLOOD_RUSH:
        case ATTACK_BLOOD_BURST:
        case ATTACK_BLOOD_BLITZ:
        case ATTACK_BLOOD_BARRAGE:
            p->spell_attack_attempts++;
            p->spell_attack_pre_move_dist_sum += p->attack_intent_pre_move_dist;
            p->spell_attack_post_move_dist_sum += dist;
            if (attack_ready && attack_style == ATTACK_STYLE_MAGIC) {
                int can_cast = pvp_spell_action_can_cast(p, attack_action);
                if (!can_cast) break;
                OsrsAttackReachQuery reach = pvp_attack_reach_query(
                    cmap, p, t, ATTACK_STYLE_MAGIC);
                if (osrs_attack_can_reach(&reach)) {
                    perform_attack(env, agent_idx, 1 - agent_idx, ATTACK_STYLE_MAGIC, 0, magic_type, dist);
                    p->spell_attack_successes++;
                    p->spell_attack_success_pre_move_dist_sum += p->attack_intent_pre_move_dist;
                    p->spell_attack_success_post_move_dist_sum += dist;
                    p->clicks_this_tick++;
                }
            }
            break;
        default:
            break;
    }
}

static void execute_attacks(OsrsEnv* env, int agent_idx, const int* actions) {
    execute_attack_combat(env, agent_idx, actions);
}

__attribute__((unused))
static void execute_actions(OsrsEnv* env, int agent_idx, const int* actions) {
    execute_switches(env, agent_idx, actions);
    execute_attacks(env, agent_idx, actions);
}

static float pvp_remaining_supply_hp_fraction(const Player* p) {
    BrewResult brew = osrs_brew_effect(p->base_hitpoints, p->base_attack,
        p->base_strength, p->base_ranged, p->base_magic);
    float remaining = 20.0f * (float)p->food_count
        + 18.0f * (float)p->karambwan_count
        + (float)brew.hp_healed * (float)p->brew_doses;
    float max_supply = 20.0f * (float)MAXED_FOOD_COUNT
        + 18.0f * (float)MAXED_KARAMBWAN_COUNT
        + (float)brew.hp_healed * (float)MAXED_BREW_DOSES;
    return max_supply > 0.0f
        ? clampf(remaining / max_supply, 0.0f, 1.0f)
        : 0.0f;
}

static float calculate_reward(OsrsEnv* env, int agent_idx) {
    float reward = 0.0f;
    Player* p = &env->players[agent_idx];
    Player* t = &env->players[1 - agent_idx];
    const RewardShapingConfig* cfg = &env->shaping;

    reward += cfg->expected_damage_reward_coef * p->expected_damage_dealt_tick;
    reward += cfg->incoming_damage_avoidance_reward_coef
        * p->expected_damage_prevented_tick;
    reward += cfg->ko_chance_reward_coef * p->ko_chance_prob_tick;

    if (env->episode_over) {
        if (env->winner == agent_idx) {
            reward += 1.0f;
            reward += cfg->ko_supply_reward_coef * pvp_remaining_supply_hp_fraction(t);
        } else if (env->winner == 1 - agent_idx) {
            reward -= cfg->terminal_death_penalty_coef;
        } else {
            reward -= cfg->terminal_draw_penalty_coef;
        }
    }

    if (!cfg->enabled) {
        return reward;
    }

    float shaping = 0.0f;

    if (cfg->prayer_penalty_enabled && !t->just_attacked) {
        int overhead = env->last_executed_actions[agent_idx * NUM_ACTION_HEADS + HEAD_OVERHEAD];
        if (overhead == ENCOUNTER_OVERHEAD_SET_REFRESH_MELEE ||
            overhead == ENCOUNTER_OVERHEAD_SET_REFRESH_RANGED ||
            overhead == ENCOUNTER_OVERHEAD_SET_REFRESH_MAGIC) {
            shaping += cfg->prayer_switch_no_attack_penalty;
        }
    }

    if (cfg->click_penalty_enabled && p->clicks_this_tick > cfg->click_penalty_threshold) {
        int excess = p->clicks_this_tick - cfg->click_penalty_threshold;
        shaping += cfg->click_penalty_coef * (float)excess;
    }

    float base_hp = (float)p->base_hitpoints;

    if (env->episode_over) {
        if (env->winner == agent_idx) {
            if (t->food_count > 0 || t->karambwan_count > 0 || t->brew_doses > 0) {
                shaping += cfg->ko_bonus;
            }
            float opp_total = (float)(t->food_count + t->karambwan_count
                                       + t->brew_doses + t->restore_doses
                                       + t->combat_potion_doses
                                       + t->ranged_potion_doses);
            float max_total = (float)(MAXED_FOOD_COUNT + MAXED_KARAMBWAN_COUNT
                                      + MAXED_BREW_DOSES + MAXED_RESTORE_DOSES
                                      + MAXED_COMBAT_POTION_DOSES
                                      + MAXED_RANGED_POTION_DOSES);
            if (max_total > 0.0f) {
                shaping += cfg->ko_supplies_bonus_coef * (opp_total / max_total);
            }
        } else if (env->winner == (1 - agent_idx)) {
            if (p->food_count > 0 || p->karambwan_count > 0 || p->brew_doses > 0) {
                shaping += cfg->wasted_resources_penalty;
            }
        }
    }

    if (p->damage_dealt_scale > 0.0f) {
        float damage_hp = p->damage_dealt_scale * base_hp;
        shaping += damage_hp * cfg->damage_dealt_coef;
        if (damage_hp >= (float)cfg->damage_burst_threshold) {
            shaping += (damage_hp - (float)cfg->damage_burst_threshold + 1.0f)
                          * cfg->damage_burst_bonus;
        }
    }

    if (p->damage_received_scale > 0.0f) {
        shaping += p->damage_received_scale * base_hp * cfg->damage_received_coef;
    }

    if (t->just_attacked) {
        if (p->player_prayed_correct) {
            shaping += cfg->correct_prayer_bonus;
        } else {
            shaping += cfg->wrong_prayer_penalty;
        }
    }

    if (p->just_attacked) {
        if (!p->target_prayed_correct) {
            shaping += cfg->off_prayer_hit_bonus;
        }

        if (p->attack_style_this_tick == ATTACK_STYLE_MELEE
            && p->frozen_ticks > 0 && !is_in_melee_range(p, t)) {
            shaping += cfg->melee_frozen_penalty;
        }

        if (p->used_special_this_tick) {
            if (!p->target_prayed_correct) {
                shaping += cfg->spec_off_prayer_bonus;
            }
            AttackStyle target_style = get_slot_weapon_attack_style(t);
            if (target_style == ATTACK_STYLE_MAGIC) {
                shaping += cfg->spec_low_defence_bonus;
            }
            float target_hp_pct = (float)t->current_hitpoints / (float)t->base_hitpoints;
            if (target_hp_pct < 0.5f) {
                shaping += cfg->spec_low_hp_bonus;
            }
        }

        if (p->attack_style_this_tick == ATTACK_STYLE_MAGIC) {
            AttackStyle weapon_style = get_slot_weapon_attack_style(p);
            if (weapon_style != ATTACK_STYLE_MAGIC) {
                shaping += cfg->magic_no_staff_penalty;
            }
        }

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
                attack_bonus = gear->slash_attack;
                if (gear->stab_attack > attack_bonus) attack_bonus = gear->stab_attack;
                if (gear->crush_attack > attack_bonus) attack_bonus = gear->crush_attack;
                break;
            default:
                break;
        }
        if (attack_bonus < 0) {
            shaping += cfg->gear_mismatch_penalty;
        }
    }

    int ate_food = p->ate_food_this_tick;
    int ate_karam = p->ate_karambwan_this_tick;
    int ate_brew = p->ate_brew_this_tick;

    if (ate_food || ate_karam) {
        float hp_before = p->prev_hp_percent;
        if (hp_before > cfg->premature_eat_threshold) {
            shaping += cfg->premature_eat_penalty;
        }
        float max_heal;
        if (ate_food) {
            max_heal = 20.0f / base_hp;
        } else {
            max_heal = 18.0f / base_hp;
        }
        float wasted = hp_before + max_heal - 1.0f;
        if (wasted > 0.0f) {
            float wasted_hp = wasted * base_hp;
            shaping += cfg->wasted_eat_penalty * wasted_hp;
        }
    }

    if (ate_food && ate_brew && ate_karam) {
        float hp_before = p->prev_hp_percent;
        float hp_threshold = 45.0f / base_hp;
        if (hp_before <= hp_threshold) {
            shaping += cfg->smart_triple_eat_bonus;
        } else {
            float food_brew_heal = (20.0f + 16.0f) / base_hp;
            float hp_after_food_brew = hp_before + food_brew_heal;
            if (hp_after_food_brew > 1.0f) hp_after_food_brew = 1.0f;
            float missing_after = 1.0f - hp_after_food_brew;
            float karam_heal_norm = 18.0f / base_hp;
            float wasted_karam = karam_heal_norm - missing_after;
            if (wasted_karam > 0.0f) {
                float wasted_karam_hp = wasted_karam * base_hp;
                shaping += cfg->wasted_triple_eat_penalty * wasted_karam_hp;
            }
        }
    }

    reward += shaping * cfg->shaping_scale;
    return reward;
}

#endif // OSRS_PVP_ACTIONS_H
