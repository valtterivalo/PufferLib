/**
 * @fileoverview osrs_consumables.h — shared food, potion, and brew consumption.
 *
 * pure functions that compute the effect of consuming food/potions/brews.
 * encounters call these instead of inlining eat/drink calculations.
 *
 * SHARED FUNCTIONS:
 *   osrs_food_heal_amount(type)       heal amount for a food type
 *   osrs_eat_food(type, hp, max, tmr) compute food eat result
 *   osrs_drink_potion(type, ...)      compute potion drink result
 *   osrs_brew_effect(base levels)     compute saradomin brew effect
 *   osrs_can_eat(timer)               check if food timer allows eating
 *   osrs_can_drink(timer)             check if potion timer allows drinking
 *
 * ref: OSRS wiki food/potion articles, osrs-dps-calc
 */

#ifndef OSRS_CONSUMABLES_H
#define OSRS_CONSUMABLES_H

#include <stdint.h>

/* food types */
typedef enum {
    FOOD_SHARK = 0,
    FOOD_KARAMBWAN,
    FOOD_MANTA_RAY,
    FOOD_ANGLERFISH,
    NUM_FOOD_TYPES
} FoodType;

/* potion types */
typedef enum {
    POTION_PRAYER_RESTORE = 0,
    POTION_SUPER_RESTORE,
    POTION_ANTIVENOM_PLUS,
    POTION_RANGING,
    POTION_SUPER_COMBAT,
    POTION_SATURATED_HEART,
    POTION_SANFEW,
    NUM_POTION_TYPES
} PotionType;

/* result from eating food */
typedef struct {
    int hp_healed;
    int consumed;       /* 1 if food was actually eaten */
} EatResult;

/* result from drinking a potion */
typedef struct {
    int prayer_restored;
    int level_boost;
    int venom_cured;
    int antivenom_ticks;
    int consumed;
} DrinkResult;

/* result from saradomin brew */
typedef struct {
    int hp_healed;
    int def_boost;
    int att_drain;
    int str_drain;
    int range_drain;
    int magic_drain;
} BrewResult;

/* food heal amounts (wiki-sourced) */
static inline int osrs_food_heal_amount(FoodType type) {
    switch (type) {
        case FOOD_SHARK:       return 20;
        case FOOD_KARAMBWAN:   return 18;
        case FOOD_MANTA_RAY:   return 22;
        case FOOD_ANGLERFISH:  return 22;
        default: return 0;
    }
}

static inline int osrs_saturated_heart_magic_boost(int base_magic) {
    return 4 + base_magic / 10;
}

/* per-stat amount formulas (wiki-sourced). THE single formula home: the
   encounter Player-application helpers and the PvP drink layer both build on
   these. all take the relevant BASE level. */
static inline int osrs_super_restore_amount(int level) {
    return 8 + level / 4;            /* ref: OSRS wiki Super restore */
}

static inline int osrs_sanfew_restore_amount(int level) {
    return 4 + level * 30 / 100;     /* ref: OSRS wiki Sanfew serum */
}

static inline int osrs_super_combat_boost_amount(int level) {
    return 5 + level * 15 / 100;     /* ref: OSRS wiki Super combat potion */
}

static inline int osrs_ranging_boost_amount(int level) {
    return 4 + level / 10;           /* ref: OSRS wiki Ranging potion */
}

static inline int osrs_brew_heal_amount(int base_hp) {
    return base_hp * 15 / 100 + 2;   /* ref: OSRS wiki Saradomin brew */
}

static inline int osrs_brew_defence_boost_amount(int base_defence) {
    return base_defence * 20 / 100 + 2;   /* ref: OSRS wiki Saradomin brew */
}

/* eat food: compute result. caller applies hp change and timer.
   anglerfish can overheal (eat at full HP). all others require HP < max.
   heal is clamped so HP doesn't exceed max (except anglerfish overheal). */
static inline EatResult osrs_eat_food(FoodType type, int current_hp, int max_hp, int food_timer) {
    EatResult r = {0, 0};
    if (food_timer > 0) return r;

    int heal = osrs_food_heal_amount(type);
    if (heal <= 0) return r;

    /* anglerfish can overheal — always consumable */
    if (type == FOOD_ANGLERFISH) {
        r.consumed = 1;
        /* full heal allowed to overheal; encounters clamp to their own cap. */
        r.hp_healed = heal;
        return r;
    }

    /* normal food: can't eat at full HP */
    if (current_hp >= max_hp) return r;

    r.consumed = 1;
    r.hp_healed = heal;
    /* clamp so total doesn't exceed max */
    if (current_hp + heal > max_hp) r.hp_healed = max_hp - current_hp;
    return r;
}

/* drink potion: compute result. caller applies effect and timer.
   prayer pots can't be drunk at full prayer. antivenom always drinkable. */
static inline DrinkResult osrs_drink_potion(PotionType type, int current_prayer,
                                             int prayer_level, int potion_timer) {
    DrinkResult r = {0, 0, 0, 0, 0};
    if (potion_timer > 0) return r;

    switch (type) {
        case POTION_PRAYER_RESTORE:
            if (current_prayer >= prayer_level) return r;
            r.consumed = 1;
            r.prayer_restored = 7 + prayer_level / 4;
            break;
        case POTION_SUPER_RESTORE:
            if (current_prayer >= prayer_level) return r;
            r.consumed = 1;
            r.prayer_restored = osrs_super_restore_amount(prayer_level);
            break;
        case POTION_SANFEW:
            r.consumed = 1;
            r.prayer_restored = osrs_sanfew_restore_amount(prayer_level);
            r.venom_cured = 1;
            break;
        case POTION_ANTIVENOM_PLUS:
            r.consumed = 1;
            r.venom_cured = 1;
            r.antivenom_ticks = 300;
            break;
        case POTION_RANGING:
            r.consumed = 1;
            r.level_boost = osrs_ranging_boost_amount(prayer_level);
            break;
        case POTION_SUPER_COMBAT:
            r.consumed = 1;
            r.level_boost = osrs_super_combat_boost_amount(prayer_level);
            break;
        case POTION_SATURATED_HEART:
            r.consumed = 1;
            r.level_boost = osrs_saturated_heart_magic_boost(prayer_level);
            break;
        default:
            break;
    }
    return r;
}

/* saradomin brew effect. wiki-exact bases: the HP heal (15% + 2) and the
   defence boost (20% + 2) compute from BASE levels; the att/str/range/magic
   drains (10% + 2) compute from CURRENT levels, so repeated sips drain
   diminishingly. ref: OSRS wiki Saradomin brew. */
static inline BrewResult osrs_brew_effect(int base_hp, int base_def,
                                           int current_att, int current_str,
                                           int current_range, int current_magic) {
    BrewResult r;
    r.hp_healed = osrs_brew_heal_amount(base_hp);
    r.def_boost = osrs_brew_defence_boost_amount(base_def);
    r.att_drain = current_att * 10 / 100 + 2;
    r.str_drain = current_str * 10 / 100 + 2;
    r.range_drain = current_range * 10 / 100 + 2;
    r.magic_drain = current_magic * 10 / 100 + 2;
    return r;
}

#endif /* OSRS_CONSUMABLES_H */
