#ifndef OSRS_PVP_ACTIONS_H
#define OSRS_PVP_ACTIONS_H

#include "osrs_types.h"
#include "osrs_items.h"
#include "osrs_consumables.h"
#include "osrs_player_consumables.h"
#include "osrs_human_input_types.h"
#include "osrs_pvp_gear.h"
#include "osrs_pvp_combat.h"
#include "osrs_pvp_maul.h"
#include "osrs_pvp_movement.h"
#include "osrs_pvp_observations.h"
#include "osrs_encounter.h"
#include "osrs_policy.h"

/* fury +3 + neitiznot +3, worn in every loadout */
#define PRAYER_BONUS 6

typedef enum {
    PVP_ATTACK_NONE = 0,
    PVP_ATTACK_WEAPON,
    PVP_ATTACK_ICE,
    PVP_ATTACK_BLOOD,
} PvpAttackIntent;

static void human_pvp_translate_inventory_cell(
    int* actions,
    const Player* player,
    int cell
) {
    if (cell < 0 || cell >= OSRS_INVENTORY_SIZE) return;
    const OsrsItemContentMetadata* metadata =
        osrs_inventory_cell_metadata(&player->inventory_cells[cell]);
    int click_action = cell + 1;
    if (metadata->click_action == OSRS_CLICK_EQUIP) {
        if (metadata->gear_slot >= 0 && metadata->gear_slot < NUM_GEAR_SLOTS)
            actions[OSRS_HEAD_EQUIP_SLOT(metadata->gear_slot)] = click_action;
    } else if (metadata->click_action == OSRS_CLICK_EAT) {
        actions[OSRS_HEAD_EAT] = click_action;
    } else if (metadata->click_action == OSRS_CLICK_DRINK) {
        actions[OSRS_HEAD_DRINK] = click_action;
    }
}

static int human_pvp_find_consumable_cell(
    const Player* player,
    OsrsConsumableKind kind
) {
    for (int cell = 0; cell < OSRS_INVENTORY_SIZE; cell++) {
        const OsrsItemContentMetadata* metadata =
            osrs_inventory_cell_metadata(&player->inventory_cells[cell]);
        if (metadata->consumable_kind == kind) return cell;
    }
    return -1;
}

static void human_to_pvp_actions(
    HumanInput* hi,
    int* actions,
    Player* agent
) {
    memset(actions, 0, OSRS_BASE_NUM_ACTION_HEADS * sizeof(int));
    if (hi->pending_move_x >= 0 && hi->pending_move_y >= 0) {
        int dx = hi->pending_move_x - agent->x;
        int dy = hi->pending_move_y - agent->y;
        if (dx < -2) dx = -2;
        if (dx > 2) dx = 2;
        if (dy < -2) dy = -2;
        if (dy > 2) dy = 2;
        for (int action = 1; action < OSRS_PRIMARY_MOVE_ACTIONS; action++) {
            if (ENCOUNTER_MOVE_TARGET_DX[action] == dx &&
                    ENCOUNTER_MOVE_TARGET_DY[action] == dy) {
                actions[OSRS_HEAD_PRIMARY] = action;
                break;
            }
        }
    }
    encounter_translate_prayer(hi, actions, OSRS_HEAD_OVERHEAD);
    encounter_translate_offensive_prayer(hi, actions, OSRS_HEAD_OFFENSIVE);

    if (hi->pending_attack) {
        actions[OSRS_HEAD_PRIMARY] = OSRS_PRIMARY_MOVE_ACTIONS;
        if (hi->pending_spell == PVP_ATTACK_ICE)
            actions[OSRS_HEAD_SPELL] = OSRS_SPELL_ICE_BARRAGE;
        else if (hi->pending_spell == PVP_ATTACK_BLOOD)
            actions[OSRS_HEAD_SPELL] = OSRS_SPELL_BLOOD_BARRAGE;
    }
    if (hi->pending_food) {
        int cell = human_pvp_find_consumable_cell(
            agent, OSRS_CONSUMABLE_SHARK_FOOD);
        if (cell >= 0) actions[OSRS_HEAD_EAT] = cell + 1;
    }
    if (hi->pending_karambwan) {
        int cell = human_pvp_find_consumable_cell(
            agent, OSRS_CONSUMABLE_KARAMBWAN);
        if (cell >= 0) actions[OSRS_HEAD_EAT] = cell + 1;
    }
    OsrsConsumableKind potion_kind = OSRS_CONSUMABLE_NONE;
    if (hi->pending_potion == POTION_BREW)
        potion_kind = OSRS_CONSUMABLE_BREW;
    else if (hi->pending_potion == POTION_RESTORE)
        potion_kind = OSRS_CONSUMABLE_SUPER_RESTORE;
    else if (hi->pending_potion == POTION_COMBAT)
        potion_kind = OSRS_CONSUMABLE_SUPER_COMBAT;
    else if (hi->pending_potion == POTION_RANGED)
        potion_kind = OSRS_CONSUMABLE_RANGING;
    if (potion_kind != OSRS_CONSUMABLE_NONE) {
        int cell = human_pvp_find_consumable_cell(agent, potion_kind);
        if (cell >= 0) actions[OSRS_HEAD_DRINK] = cell + 1;
    }
    for (int i = 0; i < hi->commands.count; i++) {
        const HumanCommand* command = &hi->commands.items[i];
        if (command->kind == HUMAN_COMMAND_EQUIP_INVENTORY_ITEM ||
                command->kind == HUMAN_COMMAND_INVENTORY_PRIMARY_CLICK ||
                command->kind == HUMAN_COMMAND_EAT ||
                command->kind == HUMAN_COMMAND_DRINK) {
            human_pvp_translate_inventory_cell(
                actions, agent, command->inventory_slot);
        }
    }
    if (hi->pending_veng)
        actions[OSRS_HEAD_SPELL] = OSRS_SPELL_VENGEANCE;
    if (hi->pending_spec)
        actions[OSRS_HEAD_SPECIAL] = agent->spec_armed ? 2 : 1;
}

static void human_to_nh_pvp_actions(HumanInput* hi, int* actions, Player* player) {
    memset(actions, 0, NH_PVP_NUM_ACTION_HEADS * sizeof(int));
    if (hi->pending_move_x >= 0 && hi->pending_move_y >= 0) {
        int dx = clamp(hi->pending_move_x - player->x, -2, 2);
        int dy = clamp(hi->pending_move_y - player->y, -2, 2);
        for (int action = 1; action < OSRS_PRIMARY_MOVE_ACTIONS; action++)
            if (ENCOUNTER_MOVE_TARGET_DX[action] == dx && ENCOUNTER_MOVE_TARGET_DY[action] == dy)
                actions[NH_PVP_HEAD_PRIMARY] = action;
    }
    encounter_translate_prayer(hi, actions, NH_PVP_HEAD_OVERHEAD);
    encounter_translate_offensive_prayer(hi, actions, NH_PVP_HEAD_OFFENSIVE);
    if (hi->pending_attack) actions[NH_PVP_HEAD_PRIMARY] =
        hi->pending_spell == PVP_ATTACK_BLOOD ? NH_PVP_PRIMARY_BLOOD :
        hi->pending_spell == PVP_ATTACK_ICE ? NH_PVP_PRIMARY_ICE : NH_PVP_PRIMARY_WEAPON;
    if (hi->pending_food) actions[NH_PVP_HEAD_FOOD] = NH_PVP_FOOD_SHARK;
    if (hi->pending_karambwan) actions[NH_PVP_HEAD_KARAMBWAN] = NH_PVP_KARAMBWAN_KARAMBWAN;
    if (hi->pending_potion == POTION_BREW) actions[NH_PVP_HEAD_DRINK] = NH_PVP_DRINK_BREW;
    else if (hi->pending_potion == POTION_RESTORE) actions[NH_PVP_HEAD_DRINK] = NH_PVP_DRINK_RESTORE;
    else if (hi->pending_potion == POTION_COMBAT) actions[NH_PVP_HEAD_DRINK] = NH_PVP_DRINK_COMBAT;
    else if (hi->pending_potion == POTION_RANGED) actions[NH_PVP_HEAD_DRINK] = NH_PVP_DRINK_RANGING;
    else if (hi->pending_potion == POTION_ANTIVENOM) actions[NH_PVP_HEAD_DRINK] = NH_PVP_DRINK_ANTIVENOM;
    for (int index = 0; index < hi->commands.count; index++) {
        const HumanCommand* command = &hi->commands.items[index];
        if (command->kind == HUMAN_COMMAND_UNEQUIP) {
            actions[NH_PVP_HEAD_EQUIP_SLOT(command->gear_slot)] = NH_PVP_EQUIPMENT_UNEQUIP;
            continue;
        }
        if (command->kind != HUMAN_COMMAND_EQUIP_INVENTORY_ITEM &&
            command->kind != HUMAN_COMMAND_INVENTORY_PRIMARY_CLICK &&
            command->kind != HUMAN_COMMAND_EAT && command->kind != HUMAN_COMMAND_DRINK) continue;
        assert(command->inventory_slot >= 0 && command->inventory_slot < OSRS_INVENTORY_SIZE);
        const OsrsItemContentMetadata* metadata =
            osrs_inventory_cell_metadata(&player->inventory_cells[command->inventory_slot]);
        if (metadata->click_action == OSRS_CLICK_EQUIP) {
            actions[NH_PVP_HEAD_EQUIP_SLOT(metadata->gear_slot)] =
                nh_pvp_equipment_action_for_item(metadata->gear_slot, metadata->item_idx);
            continue;
        }
        const int heads[] = {NH_PVP_HEAD_FOOD, NH_PVP_HEAD_DRINK, NH_PVP_HEAD_KARAMBWAN};
        const OsrsConsumableKind* kinds[] = {NH_PVP_FOOD_KINDS, NH_PVP_DRINK_KINDS, NH_PVP_KARAMBWAN_KINDS};
        for (int stage = 0; stage < 3; stage++)
            for (int action = 1; action < NH_PVP_ACTION_DIMS[heads[stage]]; action++)
                if (kinds[stage][action] == metadata->consumable_kind)
                    actions[heads[stage]] = action;
    }
    if (hi->pending_veng) actions[NH_PVP_HEAD_UTILITY] = NH_PVP_UTILITY_VENGEANCE;
    if (hi->pending_spec) actions[NH_PVP_HEAD_SPECIAL] = player->spec_armed ? 2 : 1;
}

static void pvp_apply_drink_one_dose_effect(
    void* ctx,
    OsrsConsumableKind kind
) {
    Player* p = (Player*)ctx;
    if (kind == OSRS_CONSUMABLE_SANFEW) {
        encounter_sanfew_restore_stats(p);
        encounter_add_prayer_restore(p, osrs_sanfew_restore_amount(p->base_prayer));
        encounter_cap_prayer_restore(p);
        osrs_status_cure(&p->status, 600, 0, OSRS_STATUS_CURE_ANTIPOISON);
        p->last_potion_type = POTION_RESTORE;
        return;
    }
    if (kind == OSRS_CONSUMABLE_ANTIVENOM_PLUS) {
        osrs_status_cure(&p->status, 1500, 360, OSRS_STATUS_CURE_ANTIVENOM);
        p->last_potion_type = POTION_ANTIVENOM;
        return;
    }
    int potion_type = POTION_NONE;
    if (kind == OSRS_CONSUMABLE_BREW) potion_type = POTION_BREW;
    else if (kind == OSRS_CONSUMABLE_SUPER_RESTORE) potion_type = POTION_RESTORE;
    else if (kind == OSRS_CONSUMABLE_SUPER_COMBAT) potion_type = POTION_COMBAT;
    else if (kind == OSRS_CONSUMABLE_RANGING) potion_type = POTION_RANGED;
    else return;

    switch (potion_type) {
        case POTION_BREW: {
            if (p->brew_doses <= 0) return;
            p->brew_doses--;
            BrewResult br = osrs_brew_effect(p->base_hitpoints, p->base_defence,
                                             p->current_attack, p->current_strength,
                                             p->current_ranged, p->current_magic);
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
            p->ate_brew_this_tick = 1;
            break;
        }

        case POTION_RESTORE: {
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
            p->current_prayer = clamp(
                p->current_prayer + osrs_super_restore_amount(p->base_prayer),
                0, p->base_prayer);
            int atk_restore = osrs_super_restore_amount(p->base_attack);
            int str_restore = osrs_super_restore_amount(p->base_strength);
            int def_restore = osrs_super_restore_amount(p->base_defence);
            int rng_restore = osrs_super_restore_amount(p->base_ranged);
            int mag_restore = osrs_super_restore_amount(p->base_magic);
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

        case POTION_COMBAT: {
            if (p->combat_potion_doses <= 0) return;
            p->combat_potion_doses--;
            int atk_boost = osrs_super_combat_boost_amount(p->base_attack);
            int str_boost = osrs_super_combat_boost_amount(p->base_strength);
            int def_boost = osrs_super_combat_boost_amount(p->base_defence);
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

        case POTION_RANGED: {
            if (p->ranged_potion_doses <= 0) return;
            p->ranged_potion_doses--;
            int rng_boost = osrs_ranging_boost_amount(p->base_ranged);
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
    p->food_timer = 3;
}

/* consumable timers are NOT decremented here: they tick after execute_switches
   in pvp_step so observations show the post-use countdown */
static void update_player_timers(Player* p, int prayer_bonus) {
    p->damage_applied_this_tick = 0;

    if (p->has_attack_timer) {
        p->attack_timer_uncapped -= 1;
        if (p->attack_timer >= 0) {
            p->attack_timer -= 1;
        }
    }
    if (p->frozen_ticks > 0) p->frozen_ticks--;
    if (p->freeze_immunity_ticks > 0) p->freeze_immunity_ticks--;
    if (p->veng_cooldown > 0) p->veng_cooldown--;

    if (!p->is_lms) {
        encounter_drain_all_prayers(p, prayer_bonus);
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
        osrs_tick_special_regen(p);
    } else if (p->spec_regen_active) {
        p->item_effect_state.special_regen_ticks = 0;
    }
}

static void update_timers(Player* p) {
    update_player_timers(p, PRAYER_BONUS);
}

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
    p->attack_style_this_tick = ATTACK_STYLE_NONE;
    p->magic_type_this_tick = 0;
    p->used_special_this_tick = 0;
    p->ate_food_this_tick = 0;
    p->ate_karambwan_this_tick = 0;
    p->ate_brew_this_tick = 0;
    p->cast_veng_this_tick = 0;
    p->said_taste_vengeance_this_tick = 0;
    p->clicks_this_tick = 0;
}

static void pvp_refresh_visible_gear(Player* p) {
    uint8_t weapon = p->equipped[GEAR_SLOT_WEAPON];
    update_spec_weapons_for_weapon(p, weapon);
    AttackStyle style = (AttackStyle)get_item_attack_style(weapon);
    if (item_is_spec_weapon(weapon)) p->current_gear = GEAR_SPEC;
    else if (style == ATTACK_STYLE_MELEE) p->current_gear = GEAR_MELEE;
    else if (style == ATTACK_STYLE_RANGED) p->current_gear = GEAR_RANGED;
    else if (style == ATTACK_STYLE_MAGIC) p->current_gear = GEAR_MAGE;
    p->visible_gear = weapon == ITEM_VOIDWAKER ? GEAR_MAGE : p->current_gear;
}

static int pvp_consume_food_cell(Player* player, int cell) {
    OsrsConsumableKind kind = (OsrsConsumableKind)osrs_inventory_cell_metadata(&player->inventory_cells[cell])->consumable_kind;
    FoodType type;
    switch (kind) {
        case OSRS_CONSUMABLE_SHARK_FOOD: type = FOOD_SHARK; break;
        case OSRS_CONSUMABLE_MARLIN: type = FOOD_MARLIN; break;
        case OSRS_CONSUMABLE_SUMMER_PIE: type = FOOD_SUMMER_PIE; break;
        case OSRS_CONSUMABLE_KARAMBWAN: type = FOOD_KARAMBWAN; break;
        case OSRS_CONSUMABLE_HALIBUT: type = FOOD_HALIBUT; break;
        default: abort();
    }
    if (!osrs_player_eat_food_effects(player, type).consumed) return 0;
    osrs_inventory_cell_consume_eat(&player->inventory_cells[cell]);
    if (osrs_inventory_cell_metadata(&player->inventory_cells[cell])->consumable_kind != kind) {
        if (type == FOOD_KARAMBWAN || type == FOOD_HALIBUT) player->karambwan_count--;
        else player->food_count--;
    }
    return 1;
}

static void pvp_apply_control_actions(OsrsEnv* env, int agent_idx, int* actions, int utility_spell);

static void execute_switches(
    OsrsEnv* env,
    int agent_idx,
    int* actions,
    const EncounterArenaTopology* topology
) {
    Player* p = &env->players[agent_idx];
    p->consumable_used_this_tick = 0;
    if (p->current_hitpoints <= 0) return;

    OsrsInventoryClickActions clicks = {0};
    for (int slot = 0; slot < NUM_GEAR_SLOTS; slot++)
        clicks.equip_by_slot[slot] = actions[OSRS_HEAD_EQUIP_SLOT(slot)];
    clicks.eat = actions[OSRS_HEAD_EAT];
    clicks.drink = actions[OSRS_HEAD_DRINK];
    OsrsInventoryTickIntent intent = osrs_resolve_inventory_tick_intent(
        p, p->inventory_cells, &clicks);
    if (intent.drink_cell >= 0 &&
            !pvp_drink_kind_available(
                p, intent.drink_resolution.consumable_kind)) {
        intent.drink_cell = -1;
    }
    if (osrs_inventory_tick_intent_has_effect(&intent))
        osrs_interaction_check_interrupt(&p->interaction, OSRS_IACT_EQUIP);

    OsrsInventoryApplyStep step;
    while (osrs_inventory_intent_next(&intent, &step)) {
        if (step.kind == OSRS_INVENTORY_APPLY_EQUIP) {
            int old_weapon = p->equipped[GEAR_SLOT_WEAPON];
            if (osrs_equip_from_cell(
                    p, p->inventory_cells, step.cell_idx) >= 0) {
                pvp_maul_weapon_changed(env, agent_idx, old_weapon);
                p->clicks_this_tick++;
            }
        } else if (step.kind == OSRS_INVENTORY_APPLY_EAT) {
            if (pvp_consume_food_cell(p, step.cell_idx)) {
                p->consumable_used_this_tick = 1;
                p->clicks_this_tick++;
            }
        } else {
            OsrsInventoryDrinkConsumeResult result =
                osrs_inventory_cell_consume_drink_one_dose(
                    &p->inventory_cells[step.cell_idx],
                    step.resolution,
                    &p->potion_timer,
                    pvp_apply_drink_one_dose_effect,
                    p);
            if (result.consumed) {
                p->consumable_used_this_tick = 1;
                p->clicks_this_tick++;
            }
        }
    }
    pvp_apply_control_actions(env, agent_idx, actions, actions[OSRS_HEAD_SPELL]);
    (void)topology;
}

static void pvp_apply_control_actions(OsrsEnv* env, int agent_idx, int* actions, int utility_spell) {
    Player* p = &env->players[agent_idx];
    pvp_refresh_visible_gear(p);
    p->fight_style = get_slot_weapon_attack_style(p) == ATTACK_STYLE_RANGED
        ? FIGHT_STYLE_RAPID : FIGHT_STYLE_ACCURATE;

    int overhead_action = actions[OSRS_HEAD_OVERHEAD];
    int offensive_action = actions[OSRS_HEAD_OFFENSIVE];
    if (env->is_lms &&
        (overhead_action == ENCOUNTER_OVERHEAD_SET_REFRESH_SMITE ||
         overhead_action == ENCOUNTER_OVERHEAD_SET_REFRESH_REDEMPTION))
        overhead_action = ENCOUNTER_OVERHEAD_NO_CHANGE;
    if (p->current_prayer <= 0) {
        if (overhead_action >= ENCOUNTER_OVERHEAD_SET_REFRESH_MELEE)
            overhead_action = ENCOUNTER_OVERHEAD_NO_CHANGE;
        if (offensive_action >= ENCOUNTER_OFFENSIVE_SET_REFRESH_PIETY)
            offensive_action = ENCOUNTER_OFFENSIVE_NO_CHANGE;
    }
    OverheadPrayer previous_prayer = p->prayer;
    OffensivePrayer previous_offensive = p->offensive_prayer;
    if (encounter_apply_overhead_action(&p->prayer, overhead_action))
        p->prayer_just_activated = 1;
    if (encounter_apply_offensive_action(&p->offensive_prayer, offensive_action))
        p->offensive_prayer_just_activated = 1;
    if (p->prayer != previous_prayer ||
            p->offensive_prayer != previous_offensive)
        p->clicks_this_tick++;
    env->pvp_runtime.overhead_changed_this_tick[agent_idx] = p->prayer != previous_prayer;

    int special_action = actions[OSRS_HEAD_SPECIAL];
    if (pvp_is_maul(p->equipped[GEAR_SLOT_WEAPON])) {
        if ((special_action == 1 && !p->spec_armed && can_toggle_spec(p)) ||
                (special_action == 2 && p->spec_armed))
            pvp_maul_special_click(env, agent_idx);
    } else {
        if (special_action == 1 && can_toggle_spec(p)) p->spec_armed = 1;
        else if (special_action == 2) p->spec_armed = 0;
    }

    int primary = actions[OSRS_HEAD_PRIMARY];
    int manual_cast = primary >= OSRS_PRIMARY_MOVE_ACTIONS && primary < OSRS_PRIMARY_DIM(1) &&
        (actions[OSRS_HEAD_SPELL] == OSRS_SPELL_ICE_BARRAGE ||
         actions[OSRS_HEAD_SPELL] == OSRS_SPELL_BLOOD_BARRAGE);
    if (primary >= OSRS_PRIMARY_MOVE_ACTIONS &&
            primary < OSRS_PRIMARY_DIM(1)) {
        osrs_interaction_set(&p->interaction, 1 - agent_idx);
        env->pvp_runtime.walk_dest_x[agent_idx] = -1;
        env->pvp_runtime.walk_dest_y[agent_idx] = -1;
        if (pvp_is_maul(p->equipped[GEAR_SLOT_WEAPON]) && !manual_cast)
            pvp_maul_target_click(env, agent_idx, 1 - agent_idx);
    } else if (primary > 0 && primary < OSRS_PRIMARY_MOVE_ACTIONS) {
        pvp_set_walk_dest_from_head_move(env, agent_idx, primary);
        p->clicks_this_tick++;
        osrs_interaction_check_interrupt(&p->interaction, OSRS_IACT_MOVE);
    }

    if (utility_spell == OSRS_SPELL_VENGEANCE &&
            p->is_lunar_spellbook && !p->veng_active &&
            remaining_ticks(p->veng_cooldown) == 0 &&
            p->current_magic >= OSRS_VENGEANCE_MAGIC_LEVEL) {
        p->veng_active = 1;
        p->veng_cooldown = 50;
        p->cast_veng_this_tick = 1;
        p->clicks_this_tick++;
    }
    if (!manual_cast)
        pvp_maul_finish_inputs(env, agent_idx);
}

static void nh_pvp_decode_control_actions(const int* semantic, int* engine) {
    memset(engine, 0, OSRS_BASE_NUM_ACTION_HEADS * sizeof(int));
    for (int head = 0; head < NH_PVP_NUM_ACTION_HEADS; head++)
        assert(semantic[head] >= 0 && semantic[head] < NH_PVP_ACTION_DIMS[head]);
    int primary = semantic[NH_PVP_HEAD_PRIMARY];
    engine[OSRS_HEAD_PRIMARY] = primary >= NH_PVP_PRIMARY_WEAPON
        ? OSRS_PRIMARY_MOVE_ACTIONS : primary;
    if (primary == NH_PVP_PRIMARY_BLOOD) engine[OSRS_HEAD_SPELL] = OSRS_SPELL_BLOOD_BARRAGE;
    if (primary == NH_PVP_PRIMARY_ICE) engine[OSRS_HEAD_SPELL] = OSRS_SPELL_ICE_BARRAGE;
    engine[OSRS_HEAD_OVERHEAD] = semantic[NH_PVP_HEAD_OVERHEAD];
    engine[OSRS_HEAD_SPECIAL] = semantic[NH_PVP_HEAD_SPECIAL] < NH_PVP_SPECIAL_PREPARE_DOUBLE
        ? semantic[NH_PVP_HEAD_SPECIAL] : 0;
    engine[OSRS_HEAD_OFFENSIVE] = semantic[NH_PVP_HEAD_OFFENSIVE];
}

static void execute_nh_switches(OsrsEnv* env, int actor, const int* semantic, int* engine) {
    Player* player = &env->players[actor];
    player->consumable_used_this_tick = 0;
    if (player->current_hitpoints <= 0) return;
    for (int slot = 0; slot < NUM_GEAR_SLOTS; slot++) {
        int action = semantic[NH_PVP_HEAD_EQUIP_SLOT(slot)];
        if (action == NH_PVP_EQUIPMENT_NO_CHANGE) continue;
        int old_weapon = player->equipped[GEAR_SLOT_WEAPON];
        int changed = 0;
        if (action == NH_PVP_EQUIPMENT_UNEQUIP) {
            changed = osrs_unequip_to_inventory(player, player->inventory_cells, slot) == OSRS_UNEQUIP_SUCCESS;
        } else {
            uint8_t item = NH_PVP_EQUIPMENT_ITEMS[slot][action - NH_PVP_EQUIPMENT_ITEM_BASE];
            if (player->equipped[slot] == item) continue;
            int cell = nh_pvp_find_item_cell(player, item);
            if (cell >= 0) changed = osrs_equip_from_cell(player, player->inventory_cells, cell) >= 0;
        }
        if (changed) {
            if (action != NH_PVP_EQUIPMENT_UNEQUIP)
                osrs_interaction_check_interrupt(&player->interaction, OSRS_IACT_EQUIP);
            pvp_maul_weapon_changed(env, actor, old_weapon);
            player->clicks_this_tick++;
        }
    }
    const int heads[] = {NH_PVP_HEAD_FOOD, NH_PVP_HEAD_DRINK, NH_PVP_HEAD_KARAMBWAN};
    const OsrsConsumableKind* kinds[] = {NH_PVP_FOOD_KINDS, NH_PVP_DRINK_KINDS, NH_PVP_KARAMBWAN_KINDS};
    for (int stage = 0; stage < 3; stage++) {
        int action = semantic[heads[stage]];
        if (action == 0) continue;
        OsrsConsumableKind kind = kinds[stage][action];
        int cell = nh_pvp_find_consumable_cell(player, kind);
        if (cell < 0) continue;
        int consumed;
        if (stage == 1) {
            if (!pvp_drink_kind_available(player, kind)) continue;
            OsrsInventoryDrinkConsumeResult result = osrs_inventory_cell_consume_drink_one_dose(
                &player->inventory_cells[cell],
                osrs_inventory_cell_click_classify(&player->inventory_cells[cell]),
                &player->potion_timer, pvp_apply_drink_one_dose_effect, player);
            consumed = result.consumed;
        } else {
            consumed = pvp_consume_food_cell(player, cell);
        }
        if (consumed) {
            osrs_interaction_check_interrupt(&player->interaction, OSRS_IACT_EQUIP);
            player->consumable_used_this_tick = 1;
            player->clicks_this_tick++;
        }
    }
    int primary = semantic[NH_PVP_HEAD_PRIMARY];
    if (env->pvp_runtime.pending_manual_cast[actor] == NH_PVP_PENDING_NONE &&
            (primary == NH_PVP_PRIMARY_ICE || primary == NH_PVP_PRIMARY_BLOOD)) {
        int eligible = primary == NH_PVP_PRIMARY_ICE ? can_cast_ice_spell(player) : can_cast_blood_spell(player);
        env->pvp_runtime.pending_manual_cast[actor] = !eligible ? NH_PVP_PENDING_NONE
            : primary == NH_PVP_PRIMARY_ICE ? NH_PVP_PENDING_ICE : NH_PVP_PENDING_BLOOD;
        if (!eligible) {
            engine[OSRS_HEAD_PRIMARY] = 0;
            engine[OSRS_HEAD_SPELL] = 0;
            osrs_interaction_clear(&player->interaction);
        }
    }
    NhPvpPendingManualCast pending = env->pvp_runtime.pending_manual_cast[actor];
    if (pending != NH_PVP_PENDING_NONE) {
        engine[OSRS_HEAD_PRIMARY] = OSRS_PRIMARY_MOVE_ACTIONS;
        engine[OSRS_HEAD_SPELL] = pending == NH_PVP_PENDING_ICE
            ? OSRS_SPELL_ICE_BARRAGE : OSRS_SPELL_BLOOD_BARRAGE;
    }
    int special = semantic[NH_PVP_HEAD_SPECIAL];
    if (pvp_is_maul(player->equipped[GEAR_SLOT_WEAPON])) {
        if (special == NH_PVP_SPECIAL_DISARM) {
            env->pvp_runtime.maul[actor] = osrs_granite_maul_clear_preparation(env->pvp_runtime.maul[actor]);
            player->spec_armed = 0;
            engine[OSRS_HEAD_SPECIAL] = 0;
        } else if (special == NH_PVP_SPECIAL_PREPARE_DOUBLE &&
                player->special_energy >= 2 * osrs_spec_cost(player->equipped[GEAR_SLOT_WEAPON])) {
            OsrsGraniteMaulState maul = osrs_granite_maul_expire(env->pvp_runtime.maul[actor], env->tick);
            if (maul.preparation != OSRS_GRANITE_MAUL_DESELECTED || maul.prepared_hits != 2) {
                maul = osrs_granite_maul_clear_preparation(maul);
                maul = osrs_granite_maul_special_click(maul, env->tick);
                maul = osrs_granite_maul_special_click(maul, env->tick);
            }
            env->pvp_runtime.maul[actor] = maul;
            player->spec_armed = 0;
        }
    }
    pvp_apply_control_actions(env, actor, engine,
        semantic[NH_PVP_HEAD_UTILITY] == NH_PVP_UTILITY_VENGEANCE
            ? OSRS_SPELL_VENGEANCE : OSRS_SPELL_NONE);
}


#define PVP_MOVE_NONE 0

typedef struct {
    int attack_action;
    int move_action;
    int explicit_move_in_progress;
} PvpAttackDecode;

static PvpAttackDecode pvp_decode_attack_actions(
    OsrsEnv* env, int agent_idx, Player* p, const int* actions
) {
    int primary = actions[OSRS_HEAD_PRIMARY];
    int spell = actions[OSRS_HEAD_SPELL];
    int attack_action = PVP_ATTACK_NONE;
    if (primary >= OSRS_PRIMARY_MOVE_ACTIONS &&
            primary < OSRS_PRIMARY_DIM(1)) {
        if (spell == OSRS_SPELL_ICE_BARRAGE) attack_action = PVP_ATTACK_ICE;
        else if (spell == OSRS_SPELL_BLOOD_BARRAGE) attack_action = PVP_ATTACK_BLOOD;
        else attack_action = PVP_ATTACK_WEAPON;
    }
    int explicit_move_in_progress =
        (primary > 0 && primary < OSRS_PRIMARY_MOVE_ACTIONS) ||
        env->pvp_runtime.walk_dest_x[agent_idx] >= 0;
    return (PvpAttackDecode){
        .attack_action = attack_action,
        .move_action = PVP_MOVE_NONE,
        .explicit_move_in_progress = explicit_move_in_progress,
    };
}

/** NULL topology selects coordinate-only attack geometry. */
static inline int pvp_attack_position_valid(Player* player, Player* target,
    AttackStyle style, int range, const EncounterArenaTopology* topology) {
    if (topology != NULL)
        return encounter_arena_topology_player_can_attack(topology,
            player->x, player->y, target->x, target->y, 1, range);
    if (style == ATTACK_STYLE_MELEE && range == 1) return is_in_melee_range(player, target);
    int distance = chebyshev_distance(player->x, player->y, target->x, target->y);
    return distance > 0 && distance <= range;
}

static void execute_attack_movement(
    OsrsEnv* env,
    int agent_idx,
    int* actions,
    const EncounterArenaTopology* topology,
    OsrsActorRouteCache* route_cache
) {
    Player* p = &env->players[agent_idx];
    Player* t = &env->players[1 - agent_idx];
    PvpAttackDecode decode =
        pvp_decode_attack_actions(env, agent_idx, p, actions);

    if (decode.attack_action != PVP_ATTACK_NONE)
        osrs_interaction_set(&p->interaction, 1 - agent_idx);

    int has_attack =
        decode.attack_action != PVP_ATTACK_NONE ||
        osrs_interaction_active(&p->interaction);
    AttackStyle attack_style = ATTACK_STYLE_NONE;
    if (decode.attack_action == PVP_ATTACK_WEAPON)
        attack_style = pvp_weapon_attack_style(p);
    else if (decode.attack_action == PVP_ATTACK_ICE ||
            decode.attack_action == PVP_ATTACK_BLOOD)
        attack_style = ATTACK_STYLE_MAGIC;
    else if (osrs_interaction_active(&p->interaction))
        attack_style = pvp_weapon_attack_style(p);

    if (decode.attack_action == PVP_ATTACK_ICE && !can_cast_ice_spell(p))
        attack_style = ATTACK_STYLE_NONE;
    if (decode.attack_action == PVP_ATTACK_BLOOD && !can_cast_blood_spell(p))
        attack_style = ATTACK_STYLE_NONE;

    if (!has_attack ||
            decode.move_action != PVP_MOVE_NONE ||
            decode.explicit_move_in_progress ||
            !can_move(p))
        return;

    int range = attack_style == ATTACK_STYLE_MAGIC
        ? COMBAT_SPELL_ATTACK_RANGE : get_attack_range(p, attack_style);
    if (attack_style == ATTACK_STYLE_NONE ||
            pvp_attack_position_valid(p, t, attack_style, range, topology))
        return;
    EncounterRouteInput route_input = {
        .topology = topology,
        .source_x = p->x,
        .source_y = p->y,
        .actor_size = 1,
        .target_x = t->x,
        .target_y = t->y,
        .target_size = 1,
        .target_kind = ENCOUNTER_ROUTE_TARGET_CARDINAL_ADJACENCY,
        .movement_mode = ENCOUNTER_ROUTE_MOVEMENT_RUN,
        .cost_policy = ENCOUNTER_ROUTE_COST_OSRS,
    };
    EncounterRouteResult route = encounter_route_solve(&route_input);
    int moved = osrs_player_step_apply_route(p, &route);
    p->is_moving = moved > 0;
    osrs_actor_route_cache_clear(route_cache);
}

static void execute_attack_combat(
    OsrsEnv* env,
    int agent_idx,
    int* actions,
    const EncounterArenaTopology* topology
) {
    Player* p = &env->players[agent_idx];
    Player* t = &env->players[1 - agent_idx];


    PvpAttackDecode decode = pvp_decode_attack_actions(env, agent_idx, p, actions);

    if (decode.attack_action == PVP_ATTACK_NONE && osrs_interaction_active(&p->interaction)) {
        AttackStyle weapon_style = get_slot_weapon_attack_style(p);
        if (weapon_style != ATTACK_STYLE_MAGIC) {
            decode.attack_action = PVP_ATTACK_WEAPON;
        }
    }

    if (decode.attack_action == PVP_ATTACK_WEAPON)
        pvp_maul_continue_attack(env, agent_idx);
    int attack_ready = can_attack_now(p) &&
        env->pvp_runtime.maul[agent_idx].last_special_tick != env->tick;
    int dist = chebyshev_distance(p->x, p->y, t->x, t->y);

    AttackStyle attack_style = ATTACK_STYLE_NONE;

    switch (decode.attack_action) {
        case PVP_ATTACK_WEAPON:
            attack_style = pvp_weapon_attack_style(p);
            break;
        case PVP_ATTACK_ICE:
            attack_style = ATTACK_STYLE_MAGIC;
            break;
        case PVP_ATTACK_BLOOD:
            attack_style = ATTACK_STYLE_MAGIC;
            break;
        default:
            break;
    }
    if (decode.attack_action == PVP_ATTACK_ICE && !can_cast_ice_spell(p)) {
        attack_style = ATTACK_STYLE_NONE;
    }
    if (decode.attack_action == PVP_ATTACK_BLOOD && !can_cast_blood_spell(p)) {
        attack_style = ATTACK_STYLE_NONE;
    }

    switch (decode.attack_action) {
        case PVP_ATTACK_WEAPON:
            if (attack_ready && attack_style != ATTACK_STYLE_NONE) {
                int is_special = !pvp_is_maul(p->equipped[GEAR_SLOT_WEAPON]) &&
                    p->spec_armed && is_special_ready(p, attack_style);
                if (pvp_attack_position_valid(p, t, attack_style, get_attack_range(p, attack_style), topology)) {
                    perform_attack(env, agent_idx, 1 - agent_idx, attack_style, is_special, 0, dist);
                    if (is_special)
                        osrs_spec_disarm(&p->spec_armed);
                    p->clicks_this_tick++;
                }
            }
            break;
        case PVP_ATTACK_ICE:
        case PVP_ATTACK_BLOOD:
            if (attack_ready && attack_style == ATTACK_STYLE_MAGIC) {
                if (pvp_attack_position_valid(p, t, ATTACK_STYLE_MAGIC, COMBAT_SPELL_ATTACK_RANGE, topology)) {
                    perform_attack(env, agent_idx, 1 - agent_idx, ATTACK_STYLE_MAGIC, 0, decode.attack_action == PVP_ATTACK_ICE ? 1 : 2, dist);
                    if (p->just_attacked && env->pvp_runtime.pending_manual_cast[agent_idx] != NH_PVP_PENDING_NONE) {
                        env->pvp_runtime.pending_manual_cast[agent_idx] = NH_PVP_PENDING_NONE;
                        osrs_interaction_clear(&p->interaction);
                    }
                    p->clicks_this_tick++;
                }
            }
            break;
        default:
            break;
    }
}

static inline int pvp_remaining_supply_units(const Player* p) {
    return p->food_count + p->karambwan_count +
        p->brew_doses + p->restore_doses +
        p->combat_potion_doses + p->ranged_potion_doses;
}

static float calculate_reward(OsrsEnv* env, int agent_idx) {
    float reward = 0.0f;
    Player* p = &env->players[agent_idx];
    Player* t = &env->players[1 - agent_idx];
    const RewardShapingConfig* cfg = &env->shaping;

    if (env->episode_over && env->winner >= 0) {
        reward = env->winner == agent_idx
            ? env->shaping.terminal_reward_coef : -env->shaping.terminal_reward_coef;
    }
    if (!cfg->enabled) {
        return reward;
    }
    float shaping_reward = 0.0f;

    if (cfg->prayer_penalty_enabled && env->pvp_runtime.overhead_changed_this_tick[agent_idx]
            && !t->just_attacked) {
        int overhead = env->last_executed_actions[
            agent_idx * OSRS_BASE_NUM_ACTION_HEADS + OSRS_HEAD_OVERHEAD];
        if (overhead == OVERHEAD_MAGE || overhead == OVERHEAD_RANGED || overhead == OVERHEAD_MELEE) {
            shaping_reward += cfg->prayer_switch_no_attack_penalty;
        }
    }

    if (cfg->click_penalty_enabled && p->clicks_this_tick > cfg->click_penalty_threshold) {
        int excess = p->clicks_this_tick - cfg->click_penalty_threshold;
        shaping_reward += cfg->click_penalty_coef * (float)excess;
    }

    float base_hp = (float)p->base_hitpoints;
    if (env->episode_over) {
        if (env->winner == agent_idx) {
            if (t->food_count > 0 || t->karambwan_count > 0 || t->brew_doses > 0) {
                shaping_reward += cfg->ko_bonus;
            }
            float opp_total = (float)pvp_remaining_supply_units(t);
            int initial_supply_units =
                env->pvp_runtime.initial_supply_units[1 - agent_idx];
            if (initial_supply_units <= 0) {
                fprintf(stderr,
                    "pvp reward: invalid initial supply count %d\n",
                    initial_supply_units);
                abort();
            }
            shaping_reward += cfg->ko_supplies_bonus_coef *
                (opp_total / (float)initial_supply_units);
        } else if (env->winner == (1 - agent_idx)) {
            if (p->food_count > 0 || p->karambwan_count > 0 || p->brew_doses > 0) {
                shaping_reward += cfg->wasted_resources_penalty;
            }
        }
    }

    if (p->damage_dealt_scale > 0.0f) {
        float damage_hp = p->damage_dealt_scale * base_hp;
        shaping_reward += damage_hp * cfg->damage_dealt_coef;
        if (damage_hp >= (float)cfg->damage_burst_threshold) {
            shaping_reward += (damage_hp - (float)cfg->damage_burst_threshold + 1.0f)
                          * cfg->damage_burst_bonus;
        }
    }

    if (p->damage_received_scale > 0.0f) {
        shaping_reward += p->damage_received_scale * base_hp * cfg->damage_received_coef;
    }

    if (t->just_attacked) {
        if (p->player_prayed_correct) {
            shaping_reward += cfg->correct_prayer_bonus;
        } else {
            shaping_reward += cfg->wrong_prayer_penalty;
        }
    }

    if (p->just_attacked) {
        if (!p->target_prayed_correct) {
            shaping_reward += cfg->off_prayer_hit_bonus;
        }

        if (p->used_special_this_tick) {
            if (!p->attack_was_on_prayer) {
                shaping_reward += cfg->spec_off_prayer_bonus;
            }
            AttackStyle target_style = get_slot_weapon_attack_style(t);
            if (target_style == ATTACK_STYLE_MAGIC) {
                shaping_reward += cfg->spec_low_defence_bonus;
            }
            float target_hp_pct = (float)t->current_hitpoints / (float)t->base_hitpoints;
            if (target_hp_pct < 0.5f) {
                shaping_reward += cfg->spec_low_hp_bonus;
            }
        }

        if (p->magic_type_this_tick != 0
            && p->attack_style_this_tick == ATTACK_STYLE_MAGIC) {
            AttackStyle weapon_style = get_slot_weapon_attack_style(p);
            if (weapon_style != ATTACK_STYLE_MAGIC) {
                shaping_reward += cfg->magic_no_staff_penalty;
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
        if (attack_bonus < 0 && !(p->used_special_this_tick
                && p->equipped[GEAR_SLOT_WEAPON] == ITEM_VOIDWAKER)) {
            shaping_reward += cfg->gear_mismatch_penalty;
        }
    }

    int ate_food = p->ate_food_this_tick;
    int ate_karam = p->ate_karambwan_this_tick;
    int ate_brew = p->ate_brew_this_tick;

    if (ate_food || ate_karam) {
        float hp_before = p->prev_hp_percent;
        if (hp_before > cfg->premature_eat_threshold) {
            shaping_reward += cfg->premature_eat_penalty;
        }
        shaping_reward += cfg->wasted_eat_penalty
            * (float)(p->last_food_waste + p->last_karambwan_waste);
    }

    if (ate_food && ate_brew && ate_karam) {
        float hp_before = p->prev_hp_percent;
        float hp_threshold = 45.0f / base_hp;
        if (hp_before <= hp_threshold) {
            shaping_reward += cfg->smart_triple_eat_bonus;
        } else {
            shaping_reward += cfg->wasted_triple_eat_penalty
                * (float)p->last_karambwan_waste;
        }
    }

    reward += shaping_reward * cfg->shaping_scale;

    return reward;
}

#endif // OSRS_PVP_ACTIONS_H
