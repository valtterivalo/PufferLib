#ifndef OSRS_PVP_OBSERVATIONS_H
#define OSRS_PVP_OBSERVATIONS_H

#include <string.h>

#include "osrs_policy.h"
#include "osrs_nh_actions.h"
#include "osrs_player_consumables.h"
#include "osrs_player_inventory_use.h"
#include "osrs_pvp_combat.h"
#include "osrs_pvp_gear.h"
#include "osrs_pvp_movement.h"
#include "osrs_nh_observation_layout.h"


static inline int can_use_brew_boost(Player* p) {
    int def_boost = osrs_brew_defence_boost_amount(p->base_defence);
    int def_cap = p->is_lms ? p->base_defence : p->base_defence + def_boost;
    return p->current_defence < def_cap - 1 ||
        p->current_hitpoints <= p->base_hitpoints;
}

static inline int can_restore_stats(Player* p) {
    int stats_drained = p->current_attack < p->base_attack ||
        p->current_defence < p->base_defence ||
        p->current_strength < p->base_strength ||
        p->current_ranged < p->base_ranged ||
        p->current_magic < p->base_magic;
    int prayer_low = p->current_prayer < (int)(p->base_prayer * 0.9f);
    return stats_drained || prayer_low;
}

static inline int can_boost_combat_skills(Player* p) {
    int max_att = p->base_attack + osrs_super_combat_boost_amount(p->base_attack);
    int max_str = p->base_strength + osrs_super_combat_boost_amount(p->base_strength);
    int def_boost = osrs_super_combat_boost_amount(p->base_defence);
    int max_def = p->is_lms ? p->base_defence : p->base_defence + def_boost;
    return max_att > p->current_attack + 1 ||
        max_def > p->current_defence + 1 ||
        max_str > p->current_strength + 1;
}

static inline int can_boost_ranged(Player* p) {
    int max_ranged = p->base_ranged + osrs_ranging_boost_amount(p->base_ranged);
    return max_ranged > p->current_ranged + 1;
}

static inline int pvp_drink_kind_available(
    Player* p, OsrsConsumableKind kind
) {
    if (p->potion_timer > 0) return 0;
    switch (kind) {
        case OSRS_CONSUMABLE_BREW:
            return p->brew_doses > 0 && can_use_brew_boost(p);
        case OSRS_CONSUMABLE_SUPER_RESTORE:
            return p->restore_doses > 0 && can_restore_stats(p);
        case OSRS_CONSUMABLE_SUPER_COMBAT:
            return p->combat_potion_doses > 0 && can_boost_combat_skills(p);
        case OSRS_CONSUMABLE_RANGING:
            return p->ranged_potion_doses > 0 && can_boost_ranged(p);
        case OSRS_CONSUMABLE_SANFEW:
        case OSRS_CONSUMABLE_ANTIVENOM_PLUS:
            return 1;
        default:
            return 0;
    }
}

static inline void nh_pvp_write_item_bonuses(float* out, const int bonuses[NH_PVP_ITEM_BONUS_FEATURES]) {
    const float scales[NH_PVP_ITEM_BONUS_FEATURES] = {
        STAT_NORM_ATTACK, STAT_NORM_ATTACK, STAT_NORM_ATTACK, STAT_NORM_ATTACK, STAT_NORM_ATTACK,
        STAT_NORM_DEFENCE, STAT_NORM_DEFENCE, STAT_NORM_DEFENCE, STAT_NORM_DEFENCE, STAT_NORM_DEFENCE,
        STAT_NORM_STRENGTH, STAT_NORM_STRENGTH, STAT_NORM_MAGIC_DMG, STAT_NORM_PRAYER,
    };
    for (int feature = 0; feature < NH_PVP_ITEM_BONUS_FEATURES; feature++)
        out[feature] = bonuses[feature] / scales[feature];
}

static inline void nh_pvp_write_equipment_bonuses(float* out, const uint8_t equipped[NUM_GEAR_SLOTS]) {
    EquipmentBonuses equipment;
    osrs_sum_equipment_bonuses(equipped, &equipment);
    const int bonuses[NH_PVP_ITEM_BONUS_FEATURES] = {
        equipment.attack_stab, equipment.attack_slash, equipment.attack_crush,
        equipment.attack_magic, equipment.attack_ranged,
        equipment.defence_stab, equipment.defence_slash, equipment.defence_crush,
        equipment.defence_magic, equipment.defence_ranged,
        equipment.melee_strength, equipment.ranged_strength, equipment.magic_damage, equipment.prayer,
    };
    nh_pvp_write_item_bonuses(out, bonuses);
}

static inline void pvp_write_observations(
    float* obs,
    OsrsEnv* env,
    int agent_idx
) {
    Player* p = &env->players[agent_idx];
    Player* target = &env->players[1 - agent_idx];
    const OsrsPvpVisibleActor* visible = &env->pvp_runtime.visible[1 - agent_idx];
    p->last_obs_target_x = target->x;
    p->last_obs_target_y = target->y;

    AttackStyle style = get_slot_weapon_attack_style(p);
    if (style == ATTACK_STYLE_MAGIC) style = ATTACK_STYLE_MELEE;
    GearBonuses* gear = get_slot_gear_bonuses(p);
    OsrsSharedObservationInput shared_input = {
        .player = p,
        .interaction = &p->interaction,
        .arena_min_x = FIGHT_AREA_BASE_X,
        .arena_max_x = FIGHT_AREA_BASE_X + FIGHT_AREA_WIDTH,
        .arena_min_y = FIGHT_AREA_BASE_Y,
        .arena_max_y = FIGHT_AREA_BASE_Y + FIGHT_AREA_HEIGHT,
        .attack_style = style,
        .attack_range = style == ATTACK_STYLE_NONE ? 0 : get_attack_range(p, style),
        .max_hit = style == ATTACK_STYLE_NONE ? 0 : calculate_max_hit(p, style, 1.0f, 0),
        .attack_speed = style == ATTACK_STYLE_NONE ? 0 : gear->attack_speed +
            (style == ATTACK_STYLE_RANGED ? osrs_stance_speed_mod(p->fight_style) : 0),
        .defence_stab = gear->stab_defence,
        .defence_slash = gear->slash_defence,
        .defence_crush = gear->crush_defence,
        .defence_magic = gear->magic_defence,
        .defence_ranged = gear->ranged_defence,
        .effective_level = style == ATTACK_STYLE_NONE ? 0 : calculate_effective_attack(
            p, style, PVP_MAGIC_ACCURACY_WEAPON_STYLE),
        .attack_bonus = style == ATTACK_STYLE_NONE ? 0 : get_attack_bonus(p, style),
        .strength_bonus = style == ATTACK_STYLE_NONE ? 0 : get_strength_bonus(p, style),
        .spell_base_damage = 0,
        .special_attack_cost = osrs_spec_cost(p->equipped[GEAR_SLOT_WEAPON]),
    };
    int shared_size = osrs_write_self_observations(obs, &shared_input);
    assert(shared_size == NH_PVP_SELF_CONTROL_START);
    int i = NH_PVP_SELF_CONTROL_START;
    const int spell_eligible[] = {can_cast_ice_spell(p), can_cast_blood_spell(p)};
    const int spell_base[] = {get_ice_base_hit(p->current_magic), get_blood_base_hit(p->current_magic)};
    for (int spell = 0; spell < 2; spell++) {
        obs[i++] = spell_eligible[spell];
        obs[i++] = spell_eligible[spell] ? osrs_policy_ratio(COMBAT_SPELL_ATTACK_RANGE, 15) : 0;
        obs[i++] = spell_eligible[spell] ? osrs_policy_ratio(COMBAT_SPELL_CAST_SPEED, 8) : 0;
        obs[i++] = spell_eligible[spell] ? osrs_policy_ratio(
            calculate_base_max_hit(p, ATTACK_STYLE_MAGIC, 1.0f, spell_base[spell]), 80) : 0;
    }
    int has_destination = env->pvp_runtime.walk_dest_x[agent_idx] >= 0;
    obs[i++] = has_destination;
    obs[i++] = has_destination ? osrs_policy_ratio(env->pvp_runtime.walk_dest_x[agent_idx] - p->x, FIGHT_AREA_WIDTH) : 0;
    obs[i++] = has_destination ? osrs_policy_ratio(env->pvp_runtime.walk_dest_y[agent_idx] - p->y, FIGHT_AREA_HEIGHT) : 0;
    OsrsGraniteMaulState maul = osrs_granite_maul_expire(
        env->pvp_runtime.maul[agent_idx], env->tick);
    obs[i++] = maul.preparation == OSRS_GRANITE_MAUL_IDLE;
    obs[i++] = maul.preparation == OSRS_GRANITE_MAUL_SELECTED;
    obs[i++] = maul.preparation == OSRS_GRANITE_MAUL_DESELECTED;
    obs[i++] = osrs_policy_ratio(maul.prepared_hits, 2);
    obs[i++] = p->is_lunar_spellbook;
    obs[i++] = p->veng_active;
    obs[i++] = osrs_policy_ratio(p->veng_cooldown, 50);
    assert(i == NH_PVP_SELF_CAST_INTENT_START);
    NhPvpPendingManualCast pending = env->pvp_runtime.pending_manual_cast[agent_idx];
    obs[i++] = pending == NH_PVP_PENDING_NONE;
    obs[i++] = pending == NH_PVP_PENDING_ICE;
    obs[i++] = pending == NH_PVP_PENDING_BLOOD;
    assert(i == NH_PVP_INVENTORY_START);
    for (int cell = 0; cell < OSRS_INVENTORY_SIZE; cell++) {
        const OsrsItemContentMetadata* metadata = osrs_inventory_cell_metadata(&p->inventory_cells[cell]);
        float* record = obs + i;
        record[0] = osrs_inventory_cell_obs_code_encode(p->inventory_cells[cell].content_code);
        osrs_write_item_content_affordance_features_compact(record + 1, metadata, 0,
            p->base_hitpoints, p->base_prayer, p->base_ranged);
        int bonuses[NH_PVP_ITEM_BONUS_FEATURES] = {0};
        if (metadata->item) {
            const Item* item = metadata->item;
            const int values[NH_PVP_ITEM_BONUS_FEATURES] = {
                item->attack_stab, item->attack_slash, item->attack_crush, item->attack_magic, item->attack_ranged,
                item->defence_stab, item->defence_slash, item->defence_crush, item->defence_magic, item->defence_ranged,
                item->melee_strength, item->ranged_strength, item->magic_damage, item->prayer,
            };
            memcpy(bonuses, values, sizeof(bonuses));
        }
        nh_pvp_write_item_bonuses(record + 1 + OSRS_INVENTORY_CELL_OBS_FEATURES_COMPACT, bonuses);
        record[NH_PVP_INVENTORY_FEATURES - 1] = osrs_policy_ratio(metadata->gear_slot + 1, NUM_GEAR_SLOTS);
        i += NH_PVP_INVENTORY_FEATURES;
    }
    assert(i == NH_PVP_EQUIPPED_START);
    for (int slot = 0; slot < NUM_GEAR_SLOTS; slot++) {
        uint8_t item = p->equipped[slot];
        obs[i++] = osrs_inventory_cell_obs_code_encode(item == ITEM_NONE ? 0 : osrs_inventory_content_code_from_item(item));
    }
    assert(i == NH_PVP_SELF_BONUS_START);
    nh_pvp_write_equipment_bonuses(obs + i, p->equipped);
    i += NH_PVP_ITEM_BONUS_FEATURES;
    assert(i == NH_PVP_EFFECT_START);
    osrs_write_equipment_effect_aggregate(obs + i, &p->equipment_effect_profile);
    i += OSRS_EQUIPMENT_EFFECT_AGGREGATE_FEATURES;
    assert(i == NH_PVP_TARGET_START);

    obs[i++] = osrs_policy_ratio(visible->health_bar, OSRS_PLAYER_HEALTH_BAR_SCALE);
    obs[i++] = osrs_policy_ratio(target->x - p->x, FIGHT_AREA_WIDTH);
    obs[i++] = osrs_policy_ratio(target->y - p->y, FIGHT_AREA_HEIGHT);
    obs[i++] = target->prayer == PRAYER_PROTECT_MELEE ? 1.0f : 0.0f;
    obs[i++] = target->prayer == PRAYER_PROTECT_RANGED ? 1.0f : 0.0f;
    obs[i++] = target->prayer == PRAYER_PROTECT_MAGIC ? 1.0f : 0.0f;
    obs[i++] = target->prayer == PRAYER_SMITE ? 1.0f : 0.0f;
    obs[i++] = target->prayer == PRAYER_REDEMPTION ? 1.0f : 0.0f;
    obs[i++] = target->last_attack_style == ATTACK_STYLE_MELEE ? 1.0f : 0.0f;
    obs[i++] = target->last_attack_style == ATTACK_STYLE_RANGED ? 1.0f : 0.0f;
    obs[i++] = target->last_attack_style == ATTACK_STYLE_MAGIC ? 1.0f : 0.0f;
    obs[i++] = visible->move_dx != 0 || visible->move_dy != 0;
    AttackStyle target_style = get_slot_weapon_attack_style(target);
    obs[i++] = target_style == ATTACK_STYLE_MELEE ? 1.0f : 0.0f;
    obs[i++] = target_style == ATTACK_STYLE_RANGED ? 1.0f : 0.0f;
    obs[i++] = target_style == ATTACK_STYLE_MAGIC ? 1.0f : 0.0f;
    obs[i++] = osrs_policy_ratio(
        chebyshev_distance(p->x, p->y, target->x, target->y), 10);
    assert(i == NH_PVP_TARGET_EVENT_START);
    const int event_ticks[] = {visible->last_attack_tick, visible->last_special_tick, visible->last_consume_tick,
        visible->last_vengeance_tick};
    const int event_scales[] = {8, 8, 3, 50};
    for (int event = 0; event < 4; event++) {
        obs[i++] = event_ticks[event] >= 0;
        obs[i++] = event_ticks[event] >= 0
            ? osrs_policy_ratio(env->tick - event_ticks[event], event_scales[event]) : 0;
    }
    assert(i == NH_PVP_TARGET_EQUIPPED_START);
    uint8_t visible_equipped[NUM_GEAR_SLOTS];
    memset(visible_equipped, ITEM_NONE, sizeof(visible_equipped));
    for (int slot = 0; slot < NH_PVP_VISIBLE_EQUIPPED_SLOTS; slot++) {
        int gear_slot = NH_PVP_VISIBLE_GEAR_SLOTS[slot];
        uint8_t item = target->equipped[gear_slot];
        visible_equipped[gear_slot] = item;
        obs[i++] = osrs_inventory_cell_obs_code_encode(item == ITEM_NONE ? 0 : osrs_inventory_content_code_from_item(item));
    }
    assert(i == NH_PVP_TARGET_BONUS_START);
    nh_pvp_write_equipment_bonuses(obs + i, visible_equipped);
    i += NH_PVP_ITEM_BONUS_FEATURES;
    assert(i == NH_PVP_TARGET_FREEZE_START);
    obs[i++] = visible->last_freeze_tick >= 0;
    obs[i++] = visible->last_freeze_tick >= 0 ? osrs_policy_ratio(env->tick - visible->last_freeze_tick, 32) : 0;
    assert(i == NH_PVP_PROJECTILE_START);
    int counts[OSRS_PVP_PROJECTILE_COUNT] = {0};
    int launch_ticks[OSRS_PVP_PROJECTILE_COUNT] = {0};
    for (int hit_index = 0; hit_index < target->num_pending_hits; hit_index++) {
        const OsrsPvpProjectile* projectile = &env->pvp_runtime.projectiles[1 - agent_idx][hit_index];
        if (projectile->cue == OSRS_PVP_PROJECTILE_NONE) continue;
        counts[projectile->cue]++;
        launch_ticks[projectile->cue] = max_int(launch_ticks[projectile->cue], projectile->launch_tick);
    }
    for (int cue = OSRS_PVP_PROJECTILE_RANGED; cue < OSRS_PVP_PROJECTILE_COUNT; cue++)
        obs[i++] = osrs_policy_ratio(counts[cue], MAX_PENDING_HITS);
    for (int cue = OSRS_PVP_PROJECTILE_RANGED; cue < OSRS_PVP_PROJECTILE_COUNT; cue++)
        obs[i++] = counts[cue] ? osrs_policy_ratio(env->tick - launch_ticks[cue], 8) : 0;
    if (i != NH_PVP_NUM_OBS) abort();
}

static inline void pvp_write_action_mask_bytes(
    unsigned char* mask, OsrsEnv* env, int actor,
    const EncounterArenaTopology* topology
) {
    Player* player = &env->players[actor];
    Player* target = &env->players[1 - actor];
    memset(mask, 0, NH_PVP_ACTION_MASK_SIZE);
    int offset = nh_pvp_action_head_offset(NH_PVP_HEAD_PRIMARY);
    mask[offset + NH_PVP_PRIMARY_RETAIN] = 1;
    for (int action = 1; action < OSRS_PRIMARY_MOVE_ACTIONS; action++) {
        int x = player->x + ENCOUNTER_MOVE_TARGET_DX[action];
        int y = player->y + ENCOUNTER_MOVE_TARGET_DY[action];
        mask[offset + action] = player->frozen_ticks <= 1 &&
            pvp_topology_tile_walkable(topology, x, y);
    }
    int magic = player->current_magic;
    int has_prayer = player->current_prayer > 0;
    for (int action = 1; action < NH_PVP_DRINK_DIM; action++) {
        OsrsConsumableKind kind = NH_PVP_DRINK_KINDS[action];
        if (nh_pvp_find_consumable_cell(player, kind) < 0 ||
                !pvp_drink_kind_available(player, kind)) continue;
        if (kind == OSRS_CONSUMABLE_SUPER_RESTORE || kind == OSRS_CONSUMABLE_SANFEW) {
            int amount = kind == OSRS_CONSUMABLE_SANFEW
                ? osrs_sanfew_restore_amount(player->base_magic)
                : osrs_super_restore_amount(player->base_magic);
            magic = max_int(magic, min_int(player->base_magic, player->current_magic + amount));
            has_prayer = 1;
        }
    }
    mask[offset + NH_PVP_PRIMARY_WEAPON] = target->current_hitpoints > 0;
    mask[offset + NH_PVP_PRIMARY_BLOOD] = target->current_hitpoints > 0 &&
        !player->is_lunar_spellbook && magic >= BLOOD_RUSH_LEVEL;
    mask[offset + NH_PVP_PRIMARY_ICE] = target->current_hitpoints > 0 &&
        !player->is_lunar_spellbook && magic >= ICE_RUSH_LEVEL;
    if (env->pvp_runtime.pending_manual_cast[actor] != NH_PVP_PENDING_NONE) {
        memset(mask + offset, 0, NH_PVP_ACTION_DIMS[NH_PVP_HEAD_PRIMARY]);
        mask[offset + NH_PVP_PRIMARY_RETAIN] = 1;
    }

    offset = nh_pvp_action_head_offset(NH_PVP_HEAD_OVERHEAD);
    mask[offset + ENCOUNTER_OVERHEAD_NO_CHANGE] = 1;
    mask[offset + ENCOUNTER_OVERHEAD_OFF] = player->prayer != PRAYER_NONE;
    mask[offset + ENCOUNTER_OVERHEAD_SET_REFRESH_MELEE] = has_prayer;
    mask[offset + ENCOUNTER_OVERHEAD_SET_REFRESH_RANGED] = has_prayer;
    mask[offset + ENCOUNTER_OVERHEAD_SET_REFRESH_MAGIC] = has_prayer;
    mask[offset + ENCOUNTER_OVERHEAD_SET_REFRESH_SMITE] = has_prayer && !env->is_lms;
    mask[offset + ENCOUNTER_OVERHEAD_SET_REFRESH_REDEMPTION] = has_prayer && !env->is_lms;

    int has_empty_cell = osrs_first_empty_inventory_cell(player->inventory_cells, -1) >= 0;
    int can_arm = item_is_spec_weapon(player->equipped[GEAR_SLOT_WEAPON]) &&
        player->special_energy >= osrs_spec_cost(player->equipped[GEAR_SLOT_WEAPON]);
    int can_prepare_double = pvp_is_maul(player->equipped[GEAR_SLOT_WEAPON]) &&
        player->special_energy >= 2 * osrs_spec_cost(player->equipped[GEAR_SLOT_WEAPON]);
    for (int slot = 0; slot < NUM_GEAR_SLOTS; slot++) {
        offset = nh_pvp_action_head_offset(NH_PVP_HEAD_EQUIP_SLOT(slot));
        mask[offset + NH_PVP_EQUIPMENT_NO_CHANGE] = 1;
        mask[offset + NH_PVP_EQUIPMENT_UNEQUIP] = has_empty_cell && player->equipped[slot] != ITEM_NONE;
        int count = NH_PVP_ACTION_DIMS[NH_PVP_HEAD_EQUIP_SLOT(slot)] - NH_PVP_EQUIPMENT_ITEM_BASE;
        for (int index = 0; index < count; index++) {
            uint8_t item = NH_PVP_EQUIPMENT_ITEMS[slot][index];
            int cell = nh_pvp_find_item_cell(player, item);
            if (cell < 0) continue;
            int equippable = osrs_can_equip_metadata(player,
                osrs_inventory_cell_metadata(&player->inventory_cells[cell]), has_empty_cell);
            mask[offset + index + NH_PVP_EQUIPMENT_ITEM_BASE] = equippable;
            if (slot == GEAR_SLOT_WEAPON && equippable && item_is_spec_weapon(item) &&
                    player->special_energy >= osrs_spec_cost(item)) can_arm = 1;
            if (slot == GEAR_SLOT_WEAPON && equippable && pvp_is_maul(item) &&
                    player->special_energy >= 2 * osrs_spec_cost(item)) can_prepare_double = 1;
        }
    }
    const int heads[] = {NH_PVP_HEAD_FOOD, NH_PVP_HEAD_DRINK, NH_PVP_HEAD_KARAMBWAN};
    const OsrsConsumableKind* kinds[] = {NH_PVP_FOOD_KINDS, NH_PVP_DRINK_KINDS, NH_PVP_KARAMBWAN_KINDS};
    for (int stage = 0; stage < 3; stage++) {
        offset = nh_pvp_action_head_offset(heads[stage]);
        mask[offset] = 1;
        for (int action = 1; action < NH_PVP_ACTION_DIMS[heads[stage]]; action++) {
            OsrsConsumableKind kind = kinds[stage][action];
            int present = nh_pvp_find_consumable_cell(player, kind) >= 0;
            mask[offset + action] = present && (stage == 1
                ? pvp_drink_kind_available(player, kind)
                : osrs_can_eat_consumable_kind(player, kind));
        }
    }
    offset = nh_pvp_action_head_offset(NH_PVP_HEAD_UTILITY);
    mask[offset + NH_PVP_UTILITY_NONE] = 1;
    mask[offset + NH_PVP_UTILITY_VENGEANCE] = !env->is_lms &&
        player->is_lunar_spellbook && !player->veng_active &&
        player->veng_cooldown <= 1 && magic >= OSRS_VENGEANCE_MAGIC_LEVEL;
    offset = nh_pvp_action_head_offset(NH_PVP_HEAD_SPECIAL);
    mask[offset + NH_PVP_SPECIAL_RETAIN] = 1;
    mask[offset + NH_PVP_SPECIAL_ARM] = can_arm;
    const OsrsGraniteMaulState* maul = &env->pvp_runtime.maul[actor];
    mask[offset + NH_PVP_SPECIAL_DISARM] = player->spec_armed || maul->prepared_hits > 0;
    mask[offset + NH_PVP_SPECIAL_PREPARE_DOUBLE] = can_prepare_double;
    offset = nh_pvp_action_head_offset(NH_PVP_HEAD_OFFENSIVE);
    mask[offset + ENCOUNTER_OFFENSIVE_NO_CHANGE] = 1;
    mask[offset + ENCOUNTER_OFFENSIVE_OFF] = player->offensive_prayer != OFFENSIVE_PRAYER_NONE;
    mask[offset + ENCOUNTER_OFFENSIVE_SET_REFRESH_PIETY] = has_prayer;
    mask[offset + ENCOUNTER_OFFENSIVE_SET_REFRESH_RIGOUR] = has_prayer;
    mask[offset + ENCOUNTER_OFFENSIVE_SET_REFRESH_AUGURY] = has_prayer;
}

static inline void pvp_write_action_mask(
    float* mask, OsrsEnv* env, int actor,
    const EncounterArenaTopology* topology
) {
    unsigned char bytes[NH_PVP_ACTION_MASK_SIZE];
    pvp_write_action_mask_bytes(bytes, env, actor, topology);
    for (int index = 0; index < NH_PVP_ACTION_MASK_SIZE; index++) mask[index] = bytes[index];
}

#endif
