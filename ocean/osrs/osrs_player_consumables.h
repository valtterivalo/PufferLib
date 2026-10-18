#ifndef OSRS_PLAYER_CONSUMABLES_H
#define OSRS_PLAYER_CONSUMABLES_H

#include <stdio.h>
#include <stdlib.h>

#include "osrs_types.h"
#include "osrs_consumables.h"

typedef enum { OSRS_FOOD_ACTIVE_COOLDOWN, OSRS_FOOD_CONTINUOUS_CLOCK } OsrsFoodAttackClock;

typedef struct {
    int hp_healed;
    int hp_wasted;
    int attack_delay_ticks;
    int consumed;
} OsrsPlayerEatResult;

static inline void osrs_require_player_food_action(FoodType type) {
    switch (type) {
        case FOOD_MARLIN:
        case FOOD_HALIBUT:
        case FOOD_SUMMER_PIE:
        case FOOD_SHARK:
        case FOOD_KARAMBWAN:
        case FOOD_MANTA_RAY:
        case FOOD_ANGLERFISH:
            return;
        default:
            fprintf(stderr, "unsupported player food action type: %d\n", (int)type);
            abort();
    }
}

static inline int osrs_player_food_timer(const Player* p, FoodType type) {
    osrs_require_player_food_action(type);
    return (type == FOOD_KARAMBWAN || type == FOOD_HALIBUT) ? p->karambwan_timer : p->food_timer;
}

static inline int osrs_player_food_count(const Player* p, FoodType type) {
    osrs_require_player_food_action(type);
    return (type == FOOD_KARAMBWAN || type == FOOD_HALIBUT) ? p->karambwan_count : p->food_count;
}

static inline int osrs_player_can_eat_food_type(const Player* p, FoodType type) {
    osrs_require_player_food_action(type);
    if (osrs_player_food_count(p, type) <= 0) return 0;
    EatResult r = osrs_eat_food(type, p->current_hitpoints,
        p->base_hitpoints, osrs_player_food_timer(p, type), p->pvp_overheal_block > 0);
    return r.consumed;
}

/** Whether `type` is eaten on the karambwan clock (karambwan, halibut: the combo foods) rather than the food clock. */
static inline int osrs_food_on_karambwan_clock(FoodType type) {
    return type == FOOD_KARAMBWAN || type == FOOD_HALIBUT;
}

/** The ticks a meal of `type` adds to the eater's attack clock: a karambwan-clock food 2, every other food 3 (both on one tick: 5). Drinks add none.
 * Sources: wiki Food ("Food delays": standard food 3, cooked karambwan / halibut / gnome food 2); our 159 own tapes (FINDINGS 2026-10-19, food attack delay):
 * a meal on the swing's due tick delayed it +3 anglerfish, +2 karambwan, +5 both in 101 of 106 meals. Do not change without new data. */
static inline int osrs_food_attack_delay_ticks(FoodType type) {
    return osrs_food_on_karambwan_clock(type) ? 2 : 3;
}

static inline OsrsPlayerEatResult osrs_player_eat_food_effects_with_clock(Player* p, FoodType type, OsrsFoodAttackClock clock) {
    osrs_require_player_food_action(type);
    OsrsPlayerEatResult out = {0, 0, 0, 0};

    EatResult r = osrs_eat_food(type, p->current_hitpoints,
        p->base_hitpoints, osrs_player_food_timer(p, type), p->pvp_overheal_block > 0);
    if (!r.consumed) return out;

    int heal_amount = osrs_food_heal_amount(type);
    out.consumed = 1;
    out.hp_healed = r.hp_healed;
    out.hp_wasted = heal_amount - r.hp_healed;

    if ((type == FOOD_KARAMBWAN || type == FOOD_HALIBUT)) {
        p->karambwan_timer = 3; /* Live: after a karambwan at T nothing, karambwan included, is consumed before T+3 (3,898 click-matched meals). */
        p->food_timer = 3;
        p->potion_timer = 3;
        p->ate_karambwan_this_tick = 1;
        p->last_karambwan_heal = out.hp_healed;
        p->last_karambwan_waste = out.hp_wasted;
        out.attack_delay_ticks = osrs_food_attack_delay_ticks(type);
    } else {
        p->food_timer = type == FOOD_SUMMER_PIE ? 1 : 3; /* eat delay: wiki Food "Food delays": members' pies 1 tick per bite, standard food 3; each bite still adds the attack delay */
        p->ate_food_this_tick = 1;
        p->last_food_heal = out.hp_healed;
        p->last_food_waste = out.hp_wasted;
        out.attack_delay_ticks = osrs_food_attack_delay_ticks(type);
    }

    p->current_hitpoints += out.hp_healed;

    /* OSRS_FOOD_CONTINUOUS_CLOCK: the attack clock keeps counting below 0 while idle, and food adds its delay to that signed count, so a meal is free only after
     * enough idle ticks to absorb it (3 for hard food). The wiki's wording ("if your weapon is ready to attack again then eating does not add any new delay")
     * reads as if the ready tick were free; our 159 own tapes say otherwise (FINDINGS 2026-10-19, food attack delay): a meal on the due tick delayed the swing
     * the full amount in 101 of 106 meals, each idle tick before the meal earned one tick back (e=1: 5 of 6), and a meal one tick before ready (timer 0) added
     * the full delay in 74 of 80. Pinned by test_osrs_eat_attack_delay and test_lookahead_attack_clock. Do not "fix" this to the wiki wording without new data. */
    if (clock == OSRS_FOOD_CONTINUOUS_CLOCK) {
        if (p->has_attack_timer) {
            p->attack_timer_uncapped += out.attack_delay_ticks;
            p->attack_timer = p->attack_timer_uncapped < -1 ? -1 : p->attack_timer_uncapped;
        }
    } else {
        if (p->attack_timer > 0) {
            p->attack_timer += out.attack_delay_ticks;
            p->attack_timer_uncapped = p->attack_timer;
        }
        p->has_attack_timer = 1;
    }

    return out;
}

static inline OsrsPlayerEatResult osrs_player_eat_food_effects(Player* p, FoodType type) {
    return osrs_player_eat_food_effects_with_clock(p, type, OSRS_FOOD_ACTIVE_COOLDOWN);
}

static inline OsrsPlayerEatResult osrs_player_eat_food_type_with_clock(Player* p, FoodType type, OsrsFoodAttackClock clock) {
    if (osrs_player_food_count(p, type) <= 0) return (OsrsPlayerEatResult){0, 0, 0, 0};
    OsrsPlayerEatResult out = osrs_player_eat_food_effects_with_clock(p, type, clock);
    if (out.consumed) {
        if ((type == FOOD_KARAMBWAN || type == FOOD_HALIBUT)) p->karambwan_count--;
        else p->food_count--;
    }
    return out;
}

#endif
