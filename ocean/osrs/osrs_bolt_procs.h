#ifndef OSRS_BOLT_PROCS_H
#define OSRS_BOLT_PROCS_H

#include "osrs_combat.h"
#include "osrs_items.h"

/* Enchanted bolt effects, PvP (wiki Ruby_bolts_(e), Ruby_dragon_bolts_(e), Diamond_bolts_(e), Zaryte_crossbow, state of 2026):
 *   Blood Forfeit (ruby): 11% in PvP (12.1% with the Kandarin diary, not modelled); the hit becomes 20% of the target's CURRENT hitpoints capped at 100, at the cost
 *     of 10% of the attacker's current hitpoints; nothing happens when the attacker cannot spare at least 1 hitpoint. Rolled whether or not the accuracy roll passed.
 *   Armour Piercing (diamond): 5% against another player (5.5% with the diary); the maximum hit of that shot is 15% higher and it cannot miss. Rolled
 *     whether or not the accuracy roll passed.
 *   Zaryte crossbow passive: ruby 22% of current hitpoints capped at 110, diamond +26% maximum, opal ranged level / 9, on every shot. Its special (Evoke) makes
 *     the effect certain provided the special's accuracy roll landed.
 * Opal (e) keeps its older rate below. */
enum { OSRS_RUBY_PVP_PER_MILLE = 110, OSRS_DIAMOND_PVP_PER_MILLE = 50 };

typedef struct {
    int proc_triggered;
    int modified_damage;
    int attacker_hp_cost; /* hitpoints the attacker pays for the effect (ruby) */
} BoltProcResult;

/** `zcb_weapon`: the shot is fired from a Zaryte crossbow (passive boost). `guaranteed`: the shot is its special (the effect is certain once `hit_accurate`). */
static inline BoltProcResult osrs_resolve_bolt_proc(
    int bolt_item_idx, int base_damage, int hit_accurate,
    int max_hit, int ranged_level, int target_current_hp, int attacker_current_hp,
    int zcb_weapon, int guaranteed, uint32_t* rng_state
) {
    BoltProcResult r = { 0, base_damage, 0 };

    switch (bolt_item_idx) {

    case ITEM_DIAMOND_BOLTS_E:
    case ITEM_DIAMOND_DRAGON_BOLTS_E: {
        if (guaranteed ? !hit_accurate : !encounter_roll_ratio_u16(rng_state, OSRS_DIAMOND_PVP_PER_MILLE, 1000)) break;
        int effect_max = max_hit * (zcb_weapon ? 126 : 115) / 100;
        r.proc_triggered = 1;
        r.modified_damage = encounter_rand_int(rng_state, effect_max + 1);
        break;
    }

    case ITEM_OPAL_DRAGON_BOLTS: {
        if (guaranteed || encounter_roll_ratio_u16(rng_state, 11, 200)) {
            int bonus = ranged_level / (zcb_weapon ? 9 : 10);
            r.proc_triggered = 1;
            r.modified_damage = base_damage + bonus;
        }
        break;
    }

    case ITEM_RUBY_DRAGON_BOLTS_E: {
        if (guaranteed ? !hit_accurate : !encounter_roll_ratio_u16(rng_state, OSRS_RUBY_PVP_PER_MILLE, 1000)) break;
        int cost = attacker_current_hp / 10;
        if (cost < 1) break;
        int cap = zcb_weapon ? 110 : 100;
        int effect_dmg = target_current_hp * (zcb_weapon ? 22 : 20) / 100;
        if (effect_dmg > cap) effect_dmg = cap;
        r.proc_triggered = 1;
        r.modified_damage = effect_dmg;
        r.attacker_hp_cost = cost;
        break;
    }

    default:
        break;
    }

    return r;
}

#endif
