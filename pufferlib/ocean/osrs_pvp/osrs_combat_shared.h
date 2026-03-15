/**
 * @file osrs_combat_shared.h
 * @brief Shared OSRS combat formulas for all encounters.
 *
 * Standard accuracy roll, twisted bow scaling, and other combat utilities
 * reusable across PvP, Zulrah, Inferno, and future encounters.
 */

#ifndef OSRS_COMBAT_SHARED_H
#define OSRS_COMBAT_SHARED_H

#include <math.h>

/* standard OSRS accuracy formula.
   att_roll and def_roll are pre-computed: eff_level * (bonus + 64).
   returns hit probability in [0, 1]. */
static inline float osrs_hit_chance(int att_roll, int def_roll) {
    if (att_roll > def_roll)
        return 1.0f - (float)(def_roll + 2) / (2.0f * (float)(att_roll + 1));
    else
        return (float)att_roll / (2.0f * (float)(def_roll + 1));
}

/* twisted bow accuracy multiplier.
   target_magic = min(max(npc_magic_level, npc_magic_attack_bonus), 250).
   formula from RuneLite TwistedBow._accuracyMultiplier. */
static inline float osrs_tbow_acc_mult(int target_magic) {
    int m = target_magic < 250 ? target_magic : 250;
    float t = (float)(3 * m) / 10.0f;
    float mult = (140.0f + (t - 10.0f) / 100.0f - (t - 100.0f) * (t - 100.0f) / 100.0f) / 100.0f;
    if (mult > 1.4f) mult = 1.4f;
    if (mult < 0.0f) mult = 0.0f;
    return mult;
}

/* twisted bow damage multiplier.
   same input as accuracy multiplier.
   formula from RuneLite TwistedBow._damageMultiplier. */
static inline float osrs_tbow_dmg_mult(int target_magic) {
    int m = target_magic < 250 ? target_magic : 250;
    float t = (float)(3 * m) / 10.0f;
    float mult = (250.0f + (t - 14.0f) / 100.0f - (t - 140.0f) * (t - 140.0f) / 100.0f) / 100.0f;
    if (mult > 2.5f) mult = 2.5f;
    if (mult < 0.0f) mult = 0.0f;
    return mult;
}

#endif /* OSRS_COMBAT_SHARED_H */
