#ifndef OSRS_SPECIAL_ATTACKS_H
#define OSRS_SPECIAL_ATTACKS_H

#include <assert.h>

#include "osrs_combat.h"
#include "osrs_items.h"

#define BLOWPIPE_SPEC_ACC_MULT  2
#define BLOWPIPE_SPEC_DMG_NUM   3
#define BLOWPIPE_SPEC_DMG_DEN   2
#define BLOWPIPE_SPEC_HEAL_PCT  50
#define BLOWPIPE_SPEC_COST      50

static inline int osrs_voidwaker_min_hit(int max_hit) { return max_hit / 2; }
static inline int osrs_voidwaker_max_hit(int max_hit) { return max_hit * 3 / 2; }

/* Saradomin sword, Saradomin's Lightning (wiki Saradomin_sword, Special attack): 100% energy, the melee hit deals 10% more damage (max hit * 11/10) and
 * adds 1-16 Magic damage. The accuracy roll uses the attack bonus of the current stance against the target's Slash defence; when the melee hit misses the
 * Magic hit fails too. The Magic part always splashes on a player praying Protect from Magic (Mod Ash, 19 April 2020): perform_attack zeroes it there.
 * damage[0] is the melee hit, damage[1] the Magic hit; the Magic hit rolls no accuracy of its own. */
#define OSRS_SARADOMIN_LIGHTNING_COST 100
#define OSRS_SARADOMIN_LIGHTNING_MAGIC_MAX 16

/** Blowpipe special: accuracy and damage roll only; the caller applies the heal. */
static inline int osrs_blowpipe_spec_resolve(
    int base_att_roll, int base_max_hit,
    int target_def_level, int target_ranged_def_bonus,
    uint32_t* rng_state
) {
    int att_roll = base_att_roll * BLOWPIPE_SPEC_ACC_MULT;
    int def_roll = (target_def_level + 9) * (target_ranged_def_bonus + 64);
    int spec_max = base_max_hit * BLOWPIPE_SPEC_DMG_NUM / BLOWPIPE_SPEC_DMG_DEN;
    if (encounter_roll_hit_chance(rng_state, att_roll, def_roll))
        return encounter_rand_int(rng_state, spec_max + 1);
    return 0;
}

typedef struct {
    int num_hits;
    int damage[4];
    int total_damage;
    int heal;
    int def_drain;
    int magic_def_drain;
    int prayer_restore;
    int freeze_ticks;
    int spec_cost;
    int attack_speed_override;  /* 0 = use weapon speed */
    int landed;                 /* the accuracy roll passed (set by the specials whose follow-up needs it: Zaryte crossbow) */
    int def_drain_magic_pct;    /* the caller lowers Defence by this percent of the target's Magic level (Tonalztics of ralos) */
} SpecResult;

/** Spec energy cost by weapon item index; 0 = the weapon has no special (the Soulreaper axe has one that costs stacks, not energy: osrs_spec_is_stack_special). */
static inline int osrs_spec_cost(int weapon_item_idx) {
    switch (weapon_item_idx) {
        case ITEM_AGS:                  return 50;
        case ITEM_DRAGON_CLAWS:         return 50;
        case ITEM_STATIUS_WARHAMMER:    return 35;
        case ITEM_BGS:                  return 50;
        case ITEM_ZGS:                  return 50;
        case ITEM_SGS:                  return 50;
        case ITEM_ANCIENT_GS:           return 50;
        case ITEM_VESTAS:               return 25;
        case ITEM_VESTAS_BLIGHTED:      return 25;
        case ITEM_ABYSSAL_DAGGER_BH:    return 35;
        case ITEM_ABYSSAL_DAGGER_BH_P_PLUS_PLUS: return 35;
        case ITEM_DRAGON_MACE_BH:       return 15;
        case ITEM_VOIDWAKER:            return 50;
        case ITEM_GRANITE_MAUL:         return 60; /* Quick Smash costs 60%; only the ornate handle version costs 50% (wiki Granite_maul) */
        case ITEM_GRANITE_MAUL_ORNATE:  return 50;
        case ITEM_DRAGON_DAGGER:        return 25;
        case ITEM_ELDER_MAUL:           return 50;
        case ITEM_DRAGON_WARHAMMER:     return 50;
        case ITEM_TONALZTICS_OF_RALOS:  return 50;
        case ITEM_TOXIC_BLOWPIPE:       return 50;
        case ITEM_MAGIC_SHORTBOW_I:     return 50;
        case ITEM_DARK_BOW:             return 55;
        case ITEM_ZARYTE_CROSSBOW:      return 75;
        case ITEM_HEAVY_BALLISTA:       return 65;
        case ITEM_MORRIGANS_JAVELIN:    return 50;
        case ITEM_ARMADYL_CROSSBOW:    return 50;
        case ITEM_VOLATILE_STAFF:       return 55;
        case ITEM_EYE_OF_AYAK:          return 50;
        case ITEM_SARADOMIN_SWORD:      return OSRS_SARADOMIN_LIGHTNING_COST;
        default:                        return 0;
    }
}
/** Resolve a special attack by weapon item index. */
static inline SpecResult osrs_resolve_spec(
    int weapon_item_idx, int att_roll, int max_hit,
    int def_roll, int target_def_level, uint32_t* rng_state
) {
    SpecResult r = {0, {0, 0, 0, 0}, 0, 0, 0, 0, 0, 0, 0, 0, 0};

    switch (weapon_item_idx) {

    case ITEM_AGS: {
        int spec_att = att_roll * 2;
        int spec_max = max_hit * 11 / 8;
        r.spec_cost = 50;
        r.num_hits = 1;
        if (encounter_roll_hit_chance(rng_state, spec_att, def_roll))
            r.damage[0] = encounter_rand_int(rng_state, spec_max + 1);
        r.total_damage = r.damage[0];
        break;
    }

    case ITEM_DRAGON_CLAWS: {
        r.spec_cost = 50;
        r.num_hits = 4;

        int roll1 = encounter_roll_hit_chance(rng_state, att_roll, def_roll);
        int roll2 = encounter_roll_hit_chance(rng_state, att_roll, def_roll);
        int roll3 = encounter_roll_hit_chance(rng_state, att_roll, def_roll);
        int roll4 = encounter_roll_hit_chance(rng_state, att_roll, def_roll);

        if (roll1) {
            int low = max_hit;
            int high = max_hit + low - 1;
            int total = low + encounter_rand_int(rng_state, high - low + 1);
            r.damage[0] = total / 2;
            r.damage[1] = total / 4;
            r.damage[2] = total / 8;
            r.damage[3] = total / 8 + 1;
        } else if (roll2) {
            int low = max_hit * 3 / 4;
            int high = max_hit + low - 1;
            int total = low + encounter_rand_int(rng_state, high - low + 1);
            r.damage[0] = total / 2;
            r.damage[1] = total / 4;
            r.damage[2] = total / 4 + 1;
            r.damage[3] = 0;
        } else if (roll3) {
            int low = max_hit / 2;
            int high = max_hit + low - 1;
            int total = low + encounter_rand_int(rng_state, high - low + 1);
            r.damage[0] = total / 2;
            r.damage[1] = total / 2 + 1;
            r.damage[2] = 0;
            r.damage[3] = 0;
        } else if (roll4) {
            int low = max_hit / 4;
            int high = max_hit + low - 1;
            int total = low + encounter_rand_int(rng_state, high - low + 1);
            r.damage[0] = total + 1;
            r.damage[1] = 0;
            r.damage[2] = 0;
            r.damage[3] = 0;
        } else {
            if (encounter_rand_int(rng_state, 3) < 2) {
                r.damage[0] = 1; r.damage[1] = 1;
            }
            r.damage[2] = 0; r.damage[3] = 0;
        }
        r.total_damage = r.damage[0] + r.damage[1] + r.damage[2] + r.damage[3];
        break;
    }

    case ITEM_STATIUS_WARHAMMER: {
        int spec_att = att_roll * 5 / 4;
        int spec_max = max_hit * 5 / 4;
        r.spec_cost = 35;
        r.num_hits = 1;
        if (encounter_roll_hit_chance(rng_state, spec_att, def_roll)) {
            r.damage[0] = encounter_rand_int(rng_state, spec_max + 1);
            r.def_drain = target_def_level * 30 / 100;
        }
        r.total_damage = r.damage[0];
        break;
    }

    case ITEM_BGS: {
        int spec_att = att_roll * 2;
        int spec_max = max_hit * 121 / 100;
        r.spec_cost = 50;
        r.num_hits = 1;
        if (encounter_roll_hit_chance(rng_state, spec_att, def_roll)) {
            r.damage[0] = encounter_rand_int(rng_state, spec_max + 1);
            r.def_drain = r.damage[0];
        }
        r.total_damage = r.damage[0];
        break;
    }

    case ITEM_ZGS: {
        int spec_att = att_roll * 2;
        int spec_max = max_hit * 11 / 10;
        r.spec_cost = 50;
        r.num_hits = 1;
        if (encounter_roll_hit_chance(rng_state, spec_att, def_roll)) {
            r.damage[0] = encounter_rand_int(rng_state, spec_max + 1);
            if (r.damage[0] > 0) r.freeze_ticks = 32;
        }
        r.total_damage = r.damage[0];
        break;
    }

    case ITEM_SGS: {
        int spec_att = att_roll * 2;
        int spec_max = max_hit * 11 / 10;
        r.spec_cost = 50;
        r.num_hits = 1;
        if (encounter_roll_hit_chance(rng_state, spec_att, def_roll))
            r.damage[0] = encounter_rand_int(rng_state, spec_max + 1);
        r.total_damage = r.damage[0];
        if (r.total_damage > 0) {
            r.heal = r.total_damage / 2;
            if (r.heal < 10) r.heal = 10;
            r.prayer_restore = r.total_damage / 4;
            if (r.prayer_restore < 5) r.prayer_restore = 5;
        }
        break;
    }

    /* ancient godsword blood-prison delayed damage is NOT modeled here; the pvp
       layer approximates it with its own delayed hit */
    case ITEM_ANCIENT_GS: {
        int spec_att = att_roll * 2;
        int spec_max = max_hit * 11 / 10;
        r.spec_cost = 50;
        r.num_hits = 1;
        if (encounter_roll_hit_chance(rng_state, spec_att, def_roll))
            r.damage[0] = encounter_rand_int(rng_state, spec_max + 1);
        r.total_damage = r.damage[0];
        break;
    }

    case ITEM_VESTAS_BLIGHTED:
    case ITEM_VESTAS: {
        int vls_max = max_hit * 6 / 5;
        int vls_min = max_hit / 5;
        int reduced_def = def_roll / 4;
        r.spec_cost = 25;
        r.num_hits = 1;
        if (encounter_roll_hit_chance(rng_state, att_roll, reduced_def))
            r.damage[0] = vls_min + encounter_rand_int(rng_state, vls_max - vls_min + 1);
        r.total_damage = r.damage[0];
        break;
    }

    case ITEM_VOIDWAKER: {
        int vw_min = osrs_voidwaker_min_hit(max_hit);
        int vw_max = osrs_voidwaker_max_hit(max_hit);
        r.spec_cost = 50;
        r.num_hits = 1;
        r.damage[0] = vw_min + encounter_rand_int(rng_state, vw_max - vw_min + 1);
        r.total_damage = r.damage[0];
        break;
    }

    case ITEM_GRANITE_MAUL_ORNATE:
    case ITEM_GRANITE_MAUL: {
        r.spec_cost = osrs_spec_cost(weapon_item_idx);
        r.num_hits = 1;
        r.attack_speed_override = 1;
        if (encounter_roll_hit_chance(rng_state, att_roll, def_roll))
            r.damage[0] = encounter_rand_int(rng_state, max_hit + 1);
        r.total_damage = r.damage[0];
        break;
    }

    case ITEM_DRAGON_DAGGER: {
        int spec_att = att_roll * 23 / 20;
        int spec_max = max_hit * 23 / 20;
        r.spec_cost = 25;
        r.num_hits = 2;
        for (int i = 0; i < 2; i++) {
            if (encounter_roll_hit_chance(rng_state, spec_att, def_roll))
                r.damage[i] = encounter_rand_int(rng_state, spec_max + 1);
        }
        r.total_damage = r.damage[0] + r.damage[1];
        break;
    }
    /* OSRS Wiki: Abyssal dagger (bh), Dragon mace (bh).
       The caller supplies slash/crush defence respectively. */
    case ITEM_ABYSSAL_DAGGER_BH_P_PLUS_PLUS:
    case ITEM_ABYSSAL_DAGGER_BH: {
        int spec_att = att_roll * 5 / 4;
        int spec_max = max_hit * 19 / 20;
        r.spec_cost = 35;
        r.num_hits = 2;
        if (encounter_roll_hit_chance(rng_state, spec_att, def_roll)) {
            r.damage[0] = encounter_rand_int(rng_state, spec_max + 1);
            r.damage[1] = encounter_rand_int(rng_state, spec_max + 1);
        }
        r.total_damage = r.damage[0] + r.damage[1];
        break;
    }

    case ITEM_DRAGON_MACE_BH: {
        int spec_att = att_roll * 5 / 4;
        int spec_max = max_hit * 3 / 2;
        r.spec_cost = 15;
        r.num_hits = 1;
        if (encounter_roll_hit_chance(rng_state, spec_att, def_roll * 3 / 5))
            r.damage[0] = encounter_rand_int(rng_state, spec_max + 1);
        r.total_damage = r.damage[0];
        break;
    }

    case ITEM_SARADOMIN_SWORD: {
        r.spec_cost = OSRS_SARADOMIN_LIGHTNING_COST;
        r.num_hits = 2;
        if (encounter_roll_hit_chance(rng_state, att_roll, def_roll)) {
            r.landed = 1;
            r.damage[0] = encounter_rand_int(rng_state, max_hit * 11 / 10 + 1);
            r.damage[1] = 1 + encounter_rand_int(rng_state, OSRS_SARADOMIN_LIGHTNING_MAGIC_MAX);
        }
        r.total_damage = r.damage[0] + r.damage[1];
        break;
    }


    case ITEM_ELDER_MAUL: {
        int spec_att = att_roll * 5 / 4;
        r.spec_cost = 50;
        r.num_hits = 1;
        if (encounter_roll_hit_chance(rng_state, spec_att, def_roll)) {
            r.damage[0] = encounter_rand_int(rng_state, max_hit + 1);
            r.def_drain = target_def_level * 35 / 100;
        }
        r.total_damage = r.damage[0];
        break;
    }

    /* Smash (wiki Dragon_warhammer): unboosted accuracy, 50% more damage, a landed hit lowers Defence by 30%. */
    case ITEM_DRAGON_WARHAMMER: {
        r.spec_cost = 50;
        r.num_hits = 1;
        if (encounter_roll_hit_chance(rng_state, att_roll, def_roll)) {
            r.damage[0] = encounter_rand_int(rng_state, max_hit * 3 / 2 + 1);
            r.def_drain = target_def_level * 30 / 100;
        }
        r.total_damage = r.damage[0];
        break;
    }

    /* Division (wiki Tonalztics_of_Ralos): two throws at 50% more accuracy, each landed throw lowers Defence by 10% of the target's Magic level. */
    case ITEM_TONALZTICS_OF_RALOS: {
        int spec_att = att_roll * 3 / 2;
        r.spec_cost = 50;
        r.num_hits = 2;
        for (int i = 0; i < 2; i++) {
            if (!encounter_roll_hit_chance(rng_state, spec_att, def_roll)) continue;
            r.damage[i] = encounter_rand_int(rng_state, max_hit + 1);
            r.def_drain_magic_pct += 10;
        }
        r.total_damage = r.damage[0] + r.damage[1];
        break;
    }

    case ITEM_TOXIC_BLOWPIPE: {
        int spec_att = att_roll * 2;
        int spec_max = max_hit * 3 / 2;
        r.spec_cost = 50;
        r.num_hits = 1;
        if (encounter_roll_hit_chance(rng_state, spec_att, def_roll))
            r.damage[0] = encounter_rand_int(rng_state, spec_max + 1);
        r.total_damage = r.damage[0];
        r.heal = r.total_damage / 2;
        break;
    }

    case ITEM_MAGIC_SHORTBOW_I: {
        int spec_att = att_roll * 10 / 7;
        r.spec_cost = 50;
        r.num_hits = 2;
        for (int i = 0; i < 2; i++) {
            if (encounter_roll_hit_chance(rng_state, spec_att, def_roll))
                r.damage[i] = encounter_rand_int(rng_state, max_hit + 1);
        }
        r.total_damage = r.damage[0] + r.damage[1];
        break;
    }

    case ITEM_DARK_BOW: {
        int spec_max = max_hit * 3 / 2;
        if (spec_max > 48) spec_max = 48;
        r.spec_cost = 55;
        r.num_hits = 2;
        for (int i = 0; i < 2; i++) {
            if (encounter_roll_hit_chance(rng_state, att_roll, def_roll)) {
                int dmg = encounter_rand_int(rng_state, spec_max + 1);
                r.damage[i] = dmg < 8 ? 8 : dmg;
            } else {
                r.damage[i] = 8;
            }
        }
        r.total_damage = r.damage[0] + r.damage[1];
        break;
    }

    case ITEM_HEAVY_BALLISTA: {
        int spec_att = att_roll * 5 / 4;
        int spec_max = max_hit * 5 / 4;
        r.spec_cost = 65;
        r.num_hits = 1;
        if (encounter_roll_hit_chance(rng_state, spec_att, def_roll))
            r.damage[0] = encounter_rand_int(rng_state, spec_max + 1);
        r.total_damage = r.damage[0];
        break;
    }

    /* the ZCB guaranteed bolt proc is NOT rolled here: the caller must pass
       is_zcb_spec=1 to osrs_resolve_bolt_proc after this */
    case ITEM_ZARYTE_CROSSBOW: {
        int spec_att = att_roll * 2;
        r.spec_cost = 75;
        r.num_hits = 1;
        if (encounter_roll_hit_chance(rng_state, spec_att, def_roll)) {
            r.landed = 1;
            r.damage[0] = encounter_rand_int(rng_state, max_hit + 1);
        }
        r.total_damage = r.damage[0];
        break;
    }

    /* approximation: modeled VLS-like; the real javelin is an initial hit plus a
       bleed, which the pvp layer adds separately */
    case ITEM_MORRIGANS_JAVELIN: {
        int morr_max = max_hit * 6 / 5;
        int morr_min = max_hit / 5;
        int reduced_def = def_roll / 4;
        r.spec_cost = 50;
        r.num_hits = 1;
        if (encounter_roll_hit_chance(rng_state, att_roll, reduced_def))
            r.damage[0] = morr_min + encounter_rand_int(rng_state, morr_max - morr_min + 1);
        r.total_damage = r.damage[0];
        break;
    }

    case ITEM_ARMADYL_CROSSBOW: {
        int spec_att = att_roll * 2;
        r.spec_cost = 50;
        r.num_hits = 1;
        if (encounter_roll_hit_chance(rng_state, spec_att, def_roll))
            r.damage[0] = encounter_rand_int(rng_state, max_hit + 1);
        r.total_damage = r.damage[0];
        break;
    }

    /* volatile max hit scales with magic level; the sim assumes 99 magic = 58 */
    case ITEM_VOLATILE_STAFF: {
        int spec_att = att_roll * 3 / 2;
        int vol_max = 58;
        r.spec_cost = 55;
        r.num_hits = 1;
        if (encounter_roll_hit_chance(rng_state, spec_att, def_roll))
            r.damage[0] = encounter_rand_int(rng_state, vol_max + 1);
        r.total_damage = r.damage[0];
        break;
    }

    case ITEM_EYE_OF_AYAK: {
        int spec_att = att_roll * 2;
        int spec_max = max_hit * 13 / 10;
        r.spec_cost = 50;
        r.num_hits = 1;
        r.attack_speed_override = 5;
        if (encounter_roll_hit_chance(rng_state, spec_att, def_roll)) {
            r.damage[0] = encounter_rand_int(rng_state, spec_max + 1);
            r.magic_def_drain = r.damage[0];
        }
        r.total_damage = r.damage[0];
        break;
    }

    default:
        break;
    }

    return r;
}

/** Rewrite an already-resolved special attack to the weapon's deterministic
    best outcome (guaranteed-max path). */
static inline void osrs_spec_result_force_max(
    SpecResult* r, int weapon_item_idx, int max_hit, int target_def_level
) {
    assert(r != NULL);
    SpecResult forced = {0, {0, 0, 0, 0}, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    forced.spec_cost = osrs_spec_cost(weapon_item_idx);

    switch (weapon_item_idx) {
    case ITEM_AGS:
        forced.num_hits = 1;
        forced.damage[0] = max_hit * 11 / 8;
        break;

    case ITEM_DRAGON_CLAWS: {
        int total = 2 * max_hit - 1;
        forced.num_hits = 4;
        forced.damage[0] = total / 2;
        forced.damage[1] = total / 4;
        forced.damage[2] = total / 8;
        forced.damage[3] = total / 8 + 1;
        break;
    }

    case ITEM_STATIUS_WARHAMMER:
        forced.num_hits = 1;
        forced.damage[0] = max_hit * 5 / 4;
        forced.def_drain = target_def_level * 30 / 100;
        break;

    case ITEM_DRAGON_WARHAMMER:
        forced.num_hits = 1;
        forced.damage[0] = max_hit * 3 / 2;
        forced.def_drain = target_def_level * 30 / 100;
        break;

    case ITEM_TONALZTICS_OF_RALOS:
        forced.num_hits = 2;
        forced.damage[0] = max_hit;
        forced.damage[1] = max_hit;
        forced.def_drain_magic_pct = 20;
        break;

    case ITEM_BGS:
        forced.num_hits = 1;
        forced.damage[0] = max_hit * 121 / 100;
        forced.def_drain = forced.damage[0];
        break;

    case ITEM_ZGS:
        forced.num_hits = 1;
        forced.damage[0] = max_hit * 11 / 10;
        if (forced.damage[0] > 0) forced.freeze_ticks = 32;
        break;

    case ITEM_SGS:
        forced.num_hits = 1;
        forced.damage[0] = max_hit * 11 / 10;
        if (forced.damage[0] > 0) {
            forced.heal = forced.damage[0] / 2;
            if (forced.heal < 10) forced.heal = 10;
            forced.prayer_restore = forced.damage[0] / 4;
            if (forced.prayer_restore < 5) forced.prayer_restore = 5;
        }
        break;

    case ITEM_ANCIENT_GS:
        forced.num_hits = 1;
        forced.damage[0] = max_hit * 11 / 10;
        break;

    case ITEM_VESTAS:
    case ITEM_VESTAS_BLIGHTED:
        forced.num_hits = 1;
        forced.damage[0] = max_hit * 6 / 5;
        break;

    case ITEM_VOIDWAKER:
        forced.num_hits = 1;
        forced.damage[0] = osrs_voidwaker_max_hit(max_hit);
        break;

    case ITEM_GRANITE_MAUL_ORNATE:
    case ITEM_GRANITE_MAUL:
        forced.num_hits = 1;
        forced.damage[0] = max_hit;
        forced.attack_speed_override = 1;
        break;

    case ITEM_DRAGON_DAGGER: {
        int spec_max = max_hit * 23 / 20;
        forced.num_hits = 2;
        forced.damage[0] = spec_max;
        forced.damage[1] = spec_max;
        break;
    }
    case ITEM_ABYSSAL_DAGGER_BH:
    case ITEM_ABYSSAL_DAGGER_BH_P_PLUS_PLUS:
        forced.num_hits = 2;
        forced.damage[0] = max_hit * 19 / 20;
        forced.damage[1] = max_hit * 19 / 20;
        break;

    case ITEM_DRAGON_MACE_BH:
        forced.num_hits = 1;
        forced.damage[0] = max_hit * 3 / 2;
        break;

    case ITEM_SARADOMIN_SWORD:
        forced.num_hits = 2;
        forced.damage[0] = max_hit * 11 / 10;
        forced.damage[1] = OSRS_SARADOMIN_LIGHTNING_MAGIC_MAX;
        break;


    case ITEM_ELDER_MAUL:
        forced.num_hits = 1;
        forced.damage[0] = max_hit;
        forced.def_drain = target_def_level * 35 / 100;
        break;

    case ITEM_TOXIC_BLOWPIPE:
        forced.num_hits = 1;
        forced.damage[0] = max_hit * 3 / 2;
        forced.heal = forced.damage[0] / 2;
        break;

    case ITEM_MAGIC_SHORTBOW_I:
        forced.num_hits = 2;
        forced.damage[0] = max_hit;
        forced.damage[1] = max_hit;
        break;

    case ITEM_DARK_BOW: {
        int spec_max = max_hit * 3 / 2;
        if (spec_max > 48) spec_max = 48;
        if (spec_max < 8) spec_max = 8;
        forced.num_hits = 2;
        forced.damage[0] = spec_max;
        forced.damage[1] = spec_max;
        break;
    }

    case ITEM_HEAVY_BALLISTA:
        forced.num_hits = 1;
        forced.damage[0] = max_hit * 5 / 4;
        break;

    case ITEM_ZARYTE_CROSSBOW:
        forced.num_hits = 1;
        forced.damage[0] = max_hit;
        break;

    case ITEM_MORRIGANS_JAVELIN:
        forced.num_hits = 1;
        forced.damage[0] = max_hit * 6 / 5;
        break;

    case ITEM_ARMADYL_CROSSBOW:
        forced.num_hits = 1;
        forced.damage[0] = max_hit;
        break;

    case ITEM_VOLATILE_STAFF:
        forced.num_hits = 1;
        forced.damage[0] = 58;
        break;

    case ITEM_EYE_OF_AYAK:
        forced.num_hits = 1;
        forced.damage[0] = max_hit * 13 / 10;
        forced.magic_def_drain = forced.damage[0];
        forced.attack_speed_override = 5;
        break;

    default:
        assert(!"osrs_spec_result_force_max called for a non-special weapon");
        break;
    }

    for (int i = 0; i < forced.num_hits && i < 4; i++)
        forced.total_damage += forced.damage[i];
    *r = forced;
}

/** What a spec's accuracy roll is made of, mirroring the multipliers inside osrs_resolve_spec: the attack roll becomes att_roll * att_num / att_den, the
    defence roll def_roll * def_num / def_den; `certain` specs roll nothing (Voidwaker). For melee specs `attack_type` / `defence_type` are the
    MeleeBonusType the roll reads (-1: the best bonus of the loadout / the same as the attack type), matching MELEE_SPEC_BONUS_TYPES and the defence
    overrides of perform_attack; -1 and -1 for ranged and magic specs. */
typedef struct { int att_num, att_den, def_num, def_den, certain, attack_type, defence_type; } OsrsSpecAccuracy;

static inline OsrsSpecAccuracy osrs_spec_accuracy(int weapon_item_idx) {
    OsrsSpecAccuracy a = {1, 1, 1, 1, 0, -1, -1};
    switch (weapon_item_idx) {
        case ITEM_AGS: case ITEM_BGS: case ITEM_ZGS: case ITEM_SGS: case ITEM_ANCIENT_GS:
            a.att_num = 2; a.attack_type = MELEE_BONUS_SLASH; break;
        case ITEM_DRAGON_CLAWS: a.attack_type = MELEE_BONUS_SLASH; break;
        case ITEM_STATIUS_WARHAMMER: a.att_num = 5; a.att_den = 4; a.attack_type = MELEE_BONUS_CRUSH; break;
        case ITEM_DRAGON_WARHAMMER: a.attack_type = MELEE_BONUS_CRUSH; break;
        case ITEM_TONALZTICS_OF_RALOS: a.att_num = 3; a.att_den = 2; break;
        case ITEM_VESTAS: case ITEM_VESTAS_BLIGHTED: a.def_den = 4; a.attack_type = MELEE_BONUS_SLASH; a.defence_type = MELEE_BONUS_STAB; break;
        case ITEM_VOIDWAKER: a.certain = 1; a.attack_type = MELEE_BONUS_SLASH; break;
        case ITEM_GRANITE_MAUL: case ITEM_GRANITE_MAUL_ORNATE: a.attack_type = MELEE_BONUS_CRUSH; break;
        case ITEM_DRAGON_DAGGER: a.att_num = 23; a.att_den = 20; a.attack_type = MELEE_BONUS_STAB; break;
        case ITEM_ABYSSAL_DAGGER_BH: case ITEM_ABYSSAL_DAGGER_BH_P_PLUS_PLUS: a.att_num = 5; a.att_den = 4; a.defence_type = MELEE_BONUS_SLASH; break;
        case ITEM_DRAGON_MACE_BH: a.att_num = 5; a.att_den = 4; a.def_num = 3; a.def_den = 5; a.defence_type = MELEE_BONUS_CRUSH; break;
        case ITEM_SARADOMIN_SWORD: a.defence_type = MELEE_BONUS_SLASH; break;
        case ITEM_ELDER_MAUL: a.att_num = 5; a.att_den = 4; a.attack_type = MELEE_BONUS_SLASH; break; /* the sim reads MELEE_SPEC_BONUS_TYPES[NONE] for it */
        case ITEM_TOXIC_BLOWPIPE: case ITEM_ZARYTE_CROSSBOW: case ITEM_ARMADYL_CROSSBOW: case ITEM_EYE_OF_AYAK: a.att_num = 2; break;
        case ITEM_MAGIC_SHORTBOW_I: a.att_num = 10; a.att_den = 7; break;
        case ITEM_HEAVY_BALLISTA: a.att_num = 5; a.att_den = 4; break;
        case ITEM_MORRIGANS_JAVELIN: a.def_den = 4; break;
        case ITEM_VOLATILE_STAFF: a.att_num = 3; a.att_den = 2; break;
        case ITEM_DARK_BOW: break;
        default: assert(!"osrs_spec_accuracy called for a non-special weapon");
    }
    return a;
}

/* ---- Soulreaper axe (wiki Soulreaper_axe, state of 22 July 2026).
 * Soul stacks: every attack made with the axe, hit or miss, adds one after its damage is rolled (cap 5). Each stack is +6% on the Strength level, additive with the
 * prayer multiplier (Piety 1.23 and 5 stacks: 1.53). A stack decays every 50 ticks without an attack with the axe; stacks stay when the weapon is switched
 * and do nothing for other weapons. Gaining stacks costs no hitpoints now.
 * Behead (special, no energy): consumes every stack; the strength bonus of the stacks is consumed first, then the swing gets +12% accuracy and +6% maximum and
 * minimum damage per stack consumed, against the defence of the style in use (slash here). Not modelled: the healing a decayed stack gave in 2024 (the wiki
 * leaves it unsaid after the 2026 change). Whether the minimum is 6% per stack of the unboosted maximum or of the boosted one is not stated: the unboosted is used. */
enum { OSRS_SOUL_STACKS_MAX = 5, OSRS_SOUL_DECAY_TICKS = 50 };

static inline int osrs_spec_is_stack_special(int weapon_item_idx) { return weapon_item_idx == ITEM_SOULREAPER_AXE; }

static inline float osrs_soul_strength_mult(float prayer_mult, int stacks) { return prayer_mult + 0.06f * (float)stacks; }

/** An attack with the axe was made: a normal one adds a stack, Behead has consumed them all. */
static inline void osrs_soul_attacked(Player* p, int behead) {
    p->soul_stacks = behead ? 0 : (p->soul_stacks < OSRS_SOUL_STACKS_MAX ? p->soul_stacks + 1 : OSRS_SOUL_STACKS_MAX);
    p->soul_decay_ticks = OSRS_SOUL_DECAY_TICKS;
}

/** One tick passed. */
static inline void osrs_soul_tick(Player* p) {
    if (p->soul_stacks > 0 && --p->soul_decay_ticks <= 0) {
        p->soul_stacks--;
        p->soul_decay_ticks = OSRS_SOUL_DECAY_TICKS;
    }
}

static inline int osrs_behead_max(int base_max_hit, int stacks) { return base_max_hit * (100 + 6 * stacks) / 100; }
static inline int osrs_behead_min(int base_max_hit, int stacks) { return base_max_hit * 6 * stacks / 100; }

/** Behead: `att_roll` is the plain slash attack roll, `base_max_hit` the max hit with the stacks' strength bonus consumed. */
static inline SpecResult osrs_resolve_behead(int att_roll, int base_max_hit, int def_roll, int stacks, uint32_t* rng_state) {
    SpecResult r = {0, {0, 0, 0, 0}, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    r.num_hits = 1;
    int spec_att = att_roll * (100 + 12 * stacks) / 100;
    if (encounter_roll_hit_chance(rng_state, spec_att, def_roll)) {
        int lo = osrs_behead_min(base_max_hit, stacks), hi = osrs_behead_max(base_max_hit, stacks);
        r.landed = 1;
        r.damage[0] = lo + encounter_rand_int(rng_state, hi - lo + 1);
    }
    r.total_damage = r.damage[0];
    return r;
}

#endif /* OSRS_SPECIAL_ATTACKS_H */
