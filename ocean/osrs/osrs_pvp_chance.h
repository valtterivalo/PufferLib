#ifndef OSRS_PVP_CHANCE_H
#define OSRS_PVP_CHANCE_H

#include "osrs_item_effects.h"
#include "osrs_damage.h"

/* A roll uniform over [min_hit, max_hit], each damage reduced by his overhead; `max_hit` already carries every multiplier of the max (the Dharok set's included). */
static inline float osrs_direct_ko_probability(int min_hit, int max_hit, float accuracy,
    int target_hp, OverheadPrayer prayer, AttackStyle style) {
    assert(min_hit >= 0 && max_hit >= min_hit);
    assert(accuracy >= 0 && accuracy <= 1);
    if (target_hp <= 0) return 0;
    int lower = min_hit, upper = max_hit + 1;
    while (lower < upper) {
        int roll = lower + (upper - lower) / 2;
        int damage = osrs_prayer_reduce_damage(roll, prayer, style, 1);
        if (damage >= target_hp) upper = roll;
        else lower = roll + 1;
    }
    return accuracy * (float)(max_hit - lower + 1) / (float)(max_hit - min_hit + 1);
}

#endif
