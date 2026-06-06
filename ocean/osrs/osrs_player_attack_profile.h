#ifndef OSRS_PLAYER_ATTACK_PROFILE_H
#define OSRS_PLAYER_ATTACK_PROFILE_H

#include <stdio.h>
#include <stdlib.h>

#include "osrs_attack_reach.h"
#include "osrs_special_attacks.h"

typedef enum {
    OSRS_PLAYER_ATTACK_ACTION_WEAPON = 0,
    OSRS_PLAYER_ATTACK_ACTION_TRADITIONAL_SPELL,
    OSRS_PLAYER_ATTACK_ACTION_POWERED_STAFF,
    OSRS_PLAYER_ATTACK_ACTION_SPECIAL,
} OsrsPlayerAttackActionKind;

typedef enum {
    OSRS_PLAYER_ATTACK_COOLDOWN_NONE = 0,
    OSRS_PLAYER_ATTACK_COOLDOWN_STANDARD,
} OsrsPlayerAttackCooldownKind;

typedef struct {
    uint8_t weapon_item;
    OsrsPlayerAttackActionKind action_kind;
    AttackStyle action_style;
    FightStyle fight_style;
    OsrsMagicAttackKind magic_kind;
    int special_item;
    SpecResult special_result;
} OsrsPlayerAttackProfileQuery;

typedef struct {
    OsrsPlayerAttackCooldownKind cooldown_kind;
    int cycle_ticks;
    int post_action_timer;
    int range;
    OsrsAttackDelivery delivery;
    AttackStyle visual_style;
    AttackStyle damage_style;
} OsrsPlayerAttackProfile;

static inline int osrs_item_is_powered_staff(uint8_t item_index) {
    switch (item_index) {
        case ITEM_TRIDENT_OF_SWAMP:
        case ITEM_SANGUINESTI_STAFF:
        case ITEM_EYE_OF_AYAK:
            return 1;
        default:
            return 0;
    }
}

static inline void osrs_player_attack_require_weapon(uint8_t item_index) {
    if (item_index >= NUM_ITEMS || ITEM_DATABASE[item_index].slot != SLOT_WEAPON) {
        fprintf(stderr, "invalid player attack weapon item: %u\n", item_index);
        abort();
    }
}

static inline void osrs_player_attack_require_style(AttackStyle style) {
    switch (style) {
        case ATTACK_STYLE_MELEE:
        case ATTACK_STYLE_RANGED:
        case ATTACK_STYLE_MAGIC:
            return;
        default:
            fprintf(stderr, "invalid player attack style: %d\n", style);
            abort();
    }
}

static inline OsrsAttackDelivery osrs_player_attack_delivery_from_style(
    AttackStyle style
) {
    osrs_player_attack_require_style(style);
    return style == ATTACK_STYLE_MELEE
        ? OSRS_ATTACK_DELIVERY_MELEE
        : OSRS_ATTACK_DELIVERY_PROJECTILE;
}

static inline int osrs_player_attack_weapon_speed(uint8_t item_index) {
    osrs_player_attack_require_weapon(item_index);
    int speed = ITEM_DATABASE[item_index].attack_speed;
    if (speed <= 0) {
        fprintf(stderr, "weapon has invalid attack speed: item=%u speed=%d\n",
            item_index, speed);
        abort();
    }
    return speed;
}

static inline int osrs_player_attack_weapon_range(uint8_t item_index) {
    osrs_player_attack_require_weapon(item_index);
    int range = ITEM_DATABASE[item_index].attack_range;
    if (range <= 0) {
        fprintf(stderr, "weapon has invalid attack range: item=%u range=%d\n",
            item_index, range);
        abort();
    }
    return range;
}

static inline OsrsPlayerAttackActionKind osrs_player_attack_action_kind(
    uint8_t weapon_item,
    AttackStyle style,
    int spell_base_damage
) {
    osrs_player_attack_require_style(style);
    if (style != ATTACK_STYLE_MAGIC) return OSRS_PLAYER_ATTACK_ACTION_WEAPON;
    if (osrs_item_is_powered_staff(weapon_item))
        return OSRS_PLAYER_ATTACK_ACTION_POWERED_STAFF;
    if (spell_base_damage > 0)
        return OSRS_PLAYER_ATTACK_ACTION_TRADITIONAL_SPELL;
    return OSRS_PLAYER_ATTACK_ACTION_WEAPON;
}

static inline int osrs_player_attack_base_cycle_ticks(
    const OsrsPlayerAttackProfileQuery* query
) {
    switch (query->action_kind) {
        case OSRS_PLAYER_ATTACK_ACTION_WEAPON: {
            int ticks = osrs_player_attack_weapon_speed(query->weapon_item);
            if (query->action_style == ATTACK_STYLE_RANGED &&
                    query->fight_style == FIGHT_STYLE_RAPID) {
                ticks -= 1;
            }
            if (ticks < 1) ticks = 1;
            return ticks;
        }
        case OSRS_PLAYER_ATTACK_ACTION_TRADITIONAL_SPELL:
            if (query->action_style != ATTACK_STYLE_MAGIC) {
                fprintf(stderr, "traditional spell requires magic style\n");
                abort();
            }
            return 5;
        case OSRS_PLAYER_ATTACK_ACTION_POWERED_STAFF:
            if (query->action_style != ATTACK_STYLE_MAGIC ||
                    !osrs_item_is_powered_staff(query->weapon_item)) {
                fprintf(stderr, "powered staff action requires powered staff magic\n");
                abort();
            }
            return osrs_player_attack_weapon_speed(query->weapon_item);
        case OSRS_PLAYER_ATTACK_ACTION_SPECIAL:
            if (query->special_item == ITEM_NONE) {
                fprintf(stderr, "special attack requires a special item\n");
                abort();
            }
            if (query->special_result.attack_speed_override > 0)
                return query->special_result.attack_speed_override;
            return osrs_player_attack_weapon_speed((uint8_t)query->special_item);
        default:
            fprintf(stderr, "invalid player attack action kind: %d\n",
                query->action_kind);
            abort();
    }
}

static inline int osrs_player_attack_base_range(
    const OsrsPlayerAttackProfileQuery* query
) {
    switch (query->action_kind) {
        case OSRS_PLAYER_ATTACK_ACTION_TRADITIONAL_SPELL:
            return 10;
        case OSRS_PLAYER_ATTACK_ACTION_SPECIAL:
            if (query->special_item == ITEM_NONE) {
                fprintf(stderr, "special attack requires a special item\n");
                abort();
            }
            if (query->action_style == ATTACK_STYLE_MELEE) return 1;
            return osrs_player_attack_weapon_range((uint8_t)query->special_item)
                + osrs_stance_range_mod(query->fight_style);
        case OSRS_PLAYER_ATTACK_ACTION_WEAPON:
        case OSRS_PLAYER_ATTACK_ACTION_POWERED_STAFF:
            if (query->action_style == ATTACK_STYLE_MELEE) return 1;
            return osrs_player_attack_weapon_range(query->weapon_item)
                + osrs_stance_range_mod(query->fight_style);
        default:
            fprintf(stderr, "invalid player attack action kind: %d\n",
                query->action_kind);
            abort();
    }
}

static inline OsrsPlayerAttackProfile osrs_player_attack_profile(
    const OsrsPlayerAttackProfileQuery* query
) {
    if (query == NULL) {
        fprintf(stderr, "player attack profile query is null\n");
        abort();
    }
    osrs_player_attack_require_style(query->action_style);
    osrs_player_attack_require_weapon(query->weapon_item);

    int cycle_ticks = osrs_player_attack_base_cycle_ticks(query);
    OsrsPlayerAttackCooldownKind cooldown_kind =
        query->action_kind == OSRS_PLAYER_ATTACK_ACTION_SPECIAL &&
        query->special_item == ITEM_GRANITE_MAUL
            ? OSRS_PLAYER_ATTACK_COOLDOWN_NONE
            : OSRS_PLAYER_ATTACK_COOLDOWN_STANDARD;

    AttackStyle damage_style = query->action_style;
    if (query->action_kind == OSRS_PLAYER_ATTACK_ACTION_SPECIAL &&
            query->special_item == ITEM_VOIDWAKER) {
        damage_style = ATTACK_STYLE_MAGIC;
    }

    OsrsPlayerAttackProfile out;
    out.cooldown_kind = cooldown_kind;
    out.cycle_ticks = cooldown_kind == OSRS_PLAYER_ATTACK_COOLDOWN_NONE
        ? 0
        : cycle_ticks;
    out.post_action_timer = cooldown_kind == OSRS_PLAYER_ATTACK_COOLDOWN_NONE
        ? 0
        : cycle_ticks - 1;
    out.range = osrs_player_attack_base_range(query);
    out.delivery = osrs_player_attack_delivery_from_style(query->action_style);
    out.visual_style = query->action_style;
    out.damage_style = damage_style;
    return out;
}

static inline OsrsPlayerAttackProfile osrs_player_attack_profile_for_loadout(
    const uint8_t loadout[NUM_GEAR_SLOTS],
    AttackStyle style,
    FightStyle fight_style,
    int spell_base_damage
) {
    uint8_t weapon = loadout[GEAR_SLOT_WEAPON];
    SpecResult no_spec = {0};
    OsrsPlayerAttackProfileQuery query;
    query.weapon_item = weapon;
    query.action_kind = osrs_player_attack_action_kind(
        weapon, style, spell_base_damage);
    query.action_style = style;
    query.fight_style = fight_style;
    query.magic_kind = OSRS_MAGIC_ATTACK_NONE;
    query.special_item = ITEM_NONE;
    query.special_result = no_spec;
    return osrs_player_attack_profile(&query);
}

static inline OsrsPlayerAttackProfile osrs_player_attack_profile_for_special(
    uint8_t current_weapon,
    AttackStyle action_style,
    FightStyle fight_style,
    int special_item,
    SpecResult special_result
) {
    OsrsPlayerAttackProfileQuery query;
    query.weapon_item = current_weapon;
    query.action_kind = OSRS_PLAYER_ATTACK_ACTION_SPECIAL;
    query.action_style = action_style;
    query.fight_style = fight_style;
    query.magic_kind = OSRS_MAGIC_ATTACK_NONE;
    query.special_item = special_item;
    query.special_result = special_result;
    return osrs_player_attack_profile(&query);
}

#endif
