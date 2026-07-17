/**
 * @file osrs_bolt_procs.h
 * @brief enchanted crossbow bolt proc system (diamond, opal, ruby).
 *
 * bolt procs trigger on ranged crossbow hits with a % chance per hit.
 * zaryte crossbow (ZCB) special attack guarantees the proc and uses
 * enhanced multipliers. assumes kandarin hard diary completed (1.1x chance).
 *
 * SHARED FUNCTIONS:
 *   osrs_resolve_bolt_proc(bolt, dmg, acc, max, rlvl, hp, zcb, rng)
 *
 * REFERENCE:
 *   .refs/osrs-dps-calc/src/lib/dists/bolts.ts
 *
 * SUPPORTED BOLT TYPES:
 *   ITEM_DIAMOND_BOLTS_E / ITEM_DIAMOND_DRAGON_BOLTS_E
 *     - 11% chance, re-rolls damage from [0, floor(maxHit*115/100)]
 *     - ZCB: guaranteed, effectMax uses 126/100
 *     - accurate hits only (ZCB spec bypasses)
 *
 *   ITEM_OPAL_DRAGON_BOLTS
 *     - 5.5% chance, adds floor(rangedLvl/10) bonus damage
 *     - ZCB: guaranteed, divisor 9 instead of 10
 *     - works on misses too
 *
 *   ITEM_RUBY_DRAGON_BOLTS_E
 *     - 6.6% chance, deals floor(targetHP*20/100) capped at 100
 *     - ZCB: guaranteed, 22/100 capped at 110
 *     - accurate hits only
 */

#ifndef OSRS_BOLT_PROCS_H
#define OSRS_BOLT_PROCS_H

#include "osrs_combat.h"
#include "osrs_items.h"

typedef struct {
    int proc_triggered;     /* 1 if bolt effect fired */
    int modified_damage;    /* new damage value (replaces base_damage when proc fires) */
} BoltProcResult;

/* resolve bolt proc on a crossbow hit.
   returns proc_triggered=0 if no proc (or bolt type not recognized).
   when proc_triggered=1, modified_damage is the new damage to use.

   bolt_item_idx: ITEM_DIAMOND_BOLTS_E, ITEM_OPAL_DRAGON_BOLTS, etc.
   base_damage: damage from the normal accuracy+damage roll
   hit_accurate: 1 if the attack passed accuracy, 0 if miss
   max_hit: base max hit before bolt effect (for diamond re-roll)
   ranged_level: visible ranged level (for opal bonus)
   target_current_hp: target's current HP (for ruby bolt)
   is_zcb_spec: 1 if zaryte crossbow spec active (guaranteed proc + enhanced)
   rng_state: xorshift32 state pointer */
static inline BoltProcResult osrs_resolve_bolt_proc(
    int bolt_item_idx, int base_damage, int hit_accurate,
    int max_hit, int ranged_level, int target_current_hp,
    int is_zcb_spec, uint32_t* rng_state
) {
    BoltProcResult r = { 0, base_damage };

    switch (bolt_item_idx) {

    case ITEM_DIAMOND_BOLTS_E:
    case ITEM_DIAMOND_DRAGON_BOLTS_E: {
        if (!hit_accurate && !is_zcb_spec) break;
        if (is_zcb_spec || encounter_roll_ratio_u16(rng_state, 11, 100)) {
            int effect_max = max_hit * (is_zcb_spec ? 126 : 115) / 100;
            r.proc_triggered = 1;
            r.modified_damage = encounter_rand_int(rng_state, effect_max + 1);
        }
        break;
    }

    case ITEM_OPAL_DRAGON_BOLTS: {
        if (is_zcb_spec || encounter_roll_ratio_u16(rng_state, 11, 200)) {
            int bonus = ranged_level / (is_zcb_spec ? 9 : 10);
            r.proc_triggered = 1;
            r.modified_damage = base_damage + bonus;
        }
        break;
    }

    case ITEM_RUBY_DRAGON_BOLTS_E: {
        if (!hit_accurate) break;
        if (is_zcb_spec || encounter_roll_ratio_u16(rng_state, 33, 500)) {
            int cap = is_zcb_spec ? 110 : 100;
            int effect_dmg = target_current_hp * (is_zcb_spec ? 22 : 20) / 100;
            if (effect_dmg > cap) effect_dmg = cap;
            r.proc_triggered = 1;
            r.modified_damage = effect_dmg;
        }
        break;
    }

    default:
        break;
    }

    return r;
}

#endif /* OSRS_BOLT_PROCS_H */
