/**
 * @file osrs_pvp_combat.h
 * @brief Combat damage calculations and special attacks
 *
 * Implements OSRS combat formulas:
 * - Effective level calculations with prayer/style bonuses
 * - Hit chance formula (attack roll vs defence roll)
 * - Max hit calculations
 * - Special attack damage multipliers
 * - Dragon claws cascade algorithm
 *
 * Reference: OSRS wiki formulas (combat and special attacks)
 */

#ifndef OSRS_PVP_COMBAT_H
#define OSRS_PVP_COMBAT_H

#include "osrs_pvp_types.h"
#include "osrs_pvp_gear.h"

/** Check if ring of recoil or ring of suffering (i) is equipped. */
static inline int has_recoil_effect(Player* p) {
    int ring = p->equipped[GEAR_SLOT_RING];
    return ring == ITEM_RING_OF_RECOIL || ring == ITEM_RING_OF_SUFFERING_RI;
}

// ============================================================================
// FORWARD DECLARATIONS
// ============================================================================

static void register_hit_calculated(OsrsPvp* env, int attacker_idx, int defender_idx,
                                     AttackStyle style, int total_damage);

// ============================================================================
// SPEC WEAPON COSTS
// ============================================================================

static int get_melee_spec_cost(MeleeSpecWeapon weapon) {
    switch (weapon) {
        case MELEE_SPEC_AGS:             return 50;
        case MELEE_SPEC_DRAGON_CLAWS:    return 50;
        case MELEE_SPEC_GRANITE_MAUL:    return 50;
        case MELEE_SPEC_DRAGON_DAGGER:   return 25;
        case MELEE_SPEC_VOIDWAKER:       return 50;
        case MELEE_SPEC_DWH:             return 35;
        case MELEE_SPEC_BGS:             return 100;
        case MELEE_SPEC_ZGS:             return 50;
        case MELEE_SPEC_SGS:             return 50;
        case MELEE_SPEC_ANCIENT_GS:      return 50;
        case MELEE_SPEC_VESTAS:          return 25;
        case MELEE_SPEC_ABYSSAL_DAGGER:  return 50;
        case MELEE_SPEC_DRAGON_LONGSWORD:return 25;
        case MELEE_SPEC_DRAGON_MACE:     return 25;
        case MELEE_SPEC_ABYSSAL_BLUDGEON:return 50;
        default:                         return 50;
    }
}

static int get_ranged_spec_cost(RangedSpecWeapon weapon) {
    switch (weapon) {
        case RANGED_SPEC_DARK_BOW:    return 55;
        case RANGED_SPEC_BALLISTA:    return 65;
        case RANGED_SPEC_ACB:         return 50;
        case RANGED_SPEC_ZCB:         return 75;
        case RANGED_SPEC_DRAGON_KNIFE:return 25;
        case RANGED_SPEC_MSB:         return 55;
        case RANGED_SPEC_MORRIGANS:   return 50;
        default:                      return 50;
    }
}

static int get_magic_spec_cost(MagicSpecWeapon weapon) {
    switch (weapon) {
        case MAGIC_SPEC_VOLATILE_STAFF: return 55;
        default:                        return 50;
    }
}

// ============================================================================
// SPEC WEAPON MULTIPLIERS
// ============================================================================

static float get_melee_spec_str_mult(MeleeSpecWeapon weapon) {
    switch (weapon) {
        case MELEE_SPEC_AGS:             return 1.375f;
        case MELEE_SPEC_DRAGON_CLAWS:    return 1.0f;
        case MELEE_SPEC_GRANITE_MAUL:    return 1.0f;
        case MELEE_SPEC_DRAGON_DAGGER:   return 1.15f;
        case MELEE_SPEC_VOIDWAKER:       return 1.0f;
        case MELEE_SPEC_DWH:             return 1.25f;
        case MELEE_SPEC_BGS:             return 1.21f;
        case MELEE_SPEC_ZGS:             return 1.1f;
        case MELEE_SPEC_SGS:             return 1.1f;
        case MELEE_SPEC_ANCIENT_GS:      return 1.1f;
        case MELEE_SPEC_VESTAS:          return 1.20f;
        case MELEE_SPEC_ABYSSAL_DAGGER:  return 0.85f;
        case MELEE_SPEC_DRAGON_LONGSWORD:return 1.15f;
        case MELEE_SPEC_DRAGON_MACE:     return 1.5f;
        case MELEE_SPEC_ABYSSAL_BLUDGEON:return 1.20f;
        default:                         return 1.0f;
    }
}

static float get_melee_spec_acc_mult(MeleeSpecWeapon weapon) {
    switch (weapon) {
        case MELEE_SPEC_AGS:             return 2.0f;
        case MELEE_SPEC_DRAGON_CLAWS:    return 1.35f;
        case MELEE_SPEC_GRANITE_MAUL:    return 1.0f;
        case MELEE_SPEC_DRAGON_DAGGER:   return 1.20f;
        case MELEE_SPEC_VOIDWAKER:       return 1.0f;
        case MELEE_SPEC_DWH:             return 1.0f;
        case MELEE_SPEC_BGS:             return 1.5f;
        case MELEE_SPEC_ZGS:             return 2.0f;
        case MELEE_SPEC_SGS:             return 1.5f;
        case MELEE_SPEC_ANCIENT_GS:      return 2.0f;
        case MELEE_SPEC_VESTAS:          return 1.0f;
        case MELEE_SPEC_ABYSSAL_DAGGER:  return 1.25f;
        case MELEE_SPEC_DRAGON_LONGSWORD:return 1.25f;
        case MELEE_SPEC_DRAGON_MACE:     return 1.25f;
        case MELEE_SPEC_ABYSSAL_BLUDGEON:return 1.0f;
        default:                         return 1.0f;
    }
}

static float get_ranged_spec_str_mult(RangedSpecWeapon weapon) {
    switch (weapon) {
        case RANGED_SPEC_DARK_BOW:    return 1.5f;
        case RANGED_SPEC_BALLISTA:    return 1.25f;
        case RANGED_SPEC_ACB:         return 1.0f;
        case RANGED_SPEC_ZCB:         return 1.0f;
        case RANGED_SPEC_DRAGON_KNIFE:return 1.0f;
        case RANGED_SPEC_MSB:         return 1.0f;
        case RANGED_SPEC_MORRIGANS:   return 1.0f;
        default:                      return 1.0f;
    }
}
static float get_ranged_spec_acc_mult(RangedSpecWeapon weapon) {
    switch (weapon) {
        case RANGED_SPEC_DARK_BOW:    return 1.0f;
        case RANGED_SPEC_BALLISTA:    return 1.25f;
        case RANGED_SPEC_ACB:         return 2.0f;
        case RANGED_SPEC_ZCB:         return 2.0f;
        case RANGED_SPEC_DRAGON_KNIFE:return 1.0f;
        case RANGED_SPEC_MSB:         return 1.0f;
        case RANGED_SPEC_MORRIGANS:   return 1.0f;
        default:                      return 1.0f;
    }
}

static float get_magic_spec_acc_mult(MagicSpecWeapon weapon) {
    switch (weapon) {
        case MAGIC_SPEC_VOLATILE_STAFF: return 1.5f;
        default:                        return 1.0f;
    }
}

// ============================================================================
// PRAYER MULTIPLIERS
// ============================================================================

static inline float get_defence_prayer_mult(Player* p) {
    switch (p->offensive_prayer) {
        case OFFENSIVE_PRAYER_MELEE_LOW:
        case OFFENSIVE_PRAYER_RANGED_LOW:
        case OFFENSIVE_PRAYER_MAGIC_LOW:
            return 1.15f;
        case OFFENSIVE_PRAYER_PIETY:
        case OFFENSIVE_PRAYER_RIGOUR:
        case OFFENSIVE_PRAYER_AUGURY:
            return 1.25f;
        default:
            return 1.0f;
    }
}

// ============================================================================
// EFFECTIVE LEVEL CALCULATIONS
// ============================================================================

static int calculate_effective_attack(Player* p, AttackStyle style) {
    int base_level;
    float prayer_mult = 1.0f;
    int style_bonus = 0;

    switch (style) {
        case ATTACK_STYLE_MELEE:
            base_level = p->current_attack;
            if (p->offensive_prayer == OFFENSIVE_PRAYER_PIETY) {
                prayer_mult = 1.20f;
            } else if (p->offensive_prayer == OFFENSIVE_PRAYER_MELEE_LOW) {
                prayer_mult = 1.15f;
            }
            break;
        case ATTACK_STYLE_RANGED:
            base_level = p->current_ranged;
            if (p->offensive_prayer == OFFENSIVE_PRAYER_RIGOUR) {
                prayer_mult = 1.20f;
            } else if (p->offensive_prayer == OFFENSIVE_PRAYER_RANGED_LOW) {
                prayer_mult = 1.15f;
            }
            break;
        case ATTACK_STYLE_MAGIC:
            base_level = p->current_magic;
            if (p->offensive_prayer == OFFENSIVE_PRAYER_AUGURY) {
                prayer_mult = 1.25f;
            } else if (p->offensive_prayer == OFFENSIVE_PRAYER_MAGIC_LOW) {
                prayer_mult = 1.15f;
            }
            break;
        default:
            return 0;
    }

    float effective = base_level * prayer_mult;
    effective = floorf(effective);

    if (style == ATTACK_STYLE_MELEE) {
        if (p->fight_style == FIGHT_STYLE_ACCURATE) {
            style_bonus = 3;
        } else if (p->fight_style == FIGHT_STYLE_CONTROLLED) {
            style_bonus = 1;
        }
    } else if (style == ATTACK_STYLE_RANGED) {
        if (p->fight_style == FIGHT_STYLE_ACCURATE) {
            style_bonus = 3;
        }
    }

    if (style == ATTACK_STYLE_MAGIC) {
        return (int)effective + 9;
    }
    return (int)effective + style_bonus + 8;
}

static int calculate_effective_strength(Player* p, AttackStyle style) {
    int base_level;
    float prayer_mult = 1.0f;
    int style_bonus = 0;

    switch (style) {
        case ATTACK_STYLE_MELEE:
            base_level = p->current_strength;
            if (p->offensive_prayer == OFFENSIVE_PRAYER_PIETY) {
                prayer_mult = 1.23f;
            } else if (p->offensive_prayer == OFFENSIVE_PRAYER_MELEE_LOW) {
                prayer_mult = 1.15f;
            }
            break;
        case ATTACK_STYLE_RANGED:
            base_level = p->current_ranged;
            if (p->offensive_prayer == OFFENSIVE_PRAYER_RIGOUR) {
                prayer_mult = 1.23f;
            } else if (p->offensive_prayer == OFFENSIVE_PRAYER_RANGED_LOW) {
                prayer_mult = 1.15f;
            }
            break;
        case ATTACK_STYLE_MAGIC:
            base_level = p->current_magic;
            break;
        default:
            return 0;
    }

    float effective = floorf(base_level * prayer_mult);

    if (style == ATTACK_STYLE_MELEE && p->fight_style == FIGHT_STYLE_AGGRESSIVE) {
        style_bonus = 3;
    } else if (style == ATTACK_STYLE_MELEE && p->fight_style == FIGHT_STYLE_CONTROLLED) {
        style_bonus = 1;
    }
    // NOTE: ranged accurate only boosts accuracy, not strength

    return (int)effective + style_bonus + 8;
}

static int calculate_effective_defence(Player* p, AttackStyle incoming_style) {
    int base_level = p->current_defence;
    float prayer_mult = get_defence_prayer_mult(p);
    int style_bonus = 0;

    if (p->fight_style == FIGHT_STYLE_DEFENSIVE) {
        style_bonus = 3;
    } else if (p->fight_style == FIGHT_STYLE_CONTROLLED) {
        style_bonus = 1;
    }

    if (incoming_style == ATTACK_STYLE_MAGIC) {
        float magic_prayer_mult = 1.0f;
        if (p->offensive_prayer == OFFENSIVE_PRAYER_AUGURY) {
            magic_prayer_mult = 1.25f;
        } else if (p->offensive_prayer == OFFENSIVE_PRAYER_MAGIC_LOW) {
            magic_prayer_mult = 1.15f;
        }
        int magic_level = (int)floorf(p->current_magic * magic_prayer_mult);
        int def_level = (int)floorf(p->current_defence * prayer_mult) + style_bonus + 8;
        int magic_part = (int)floorf(magic_level * 0.7f);
        int def_part = (int)floorf(def_level * 0.3f);
        return magic_part + def_part;
    }

    float effective = floorf(base_level * prayer_mult);
    return (int)effective + style_bonus + 8;
}

// ============================================================================
// ATTACK/DEFENCE BONUS LOOKUPS
// ============================================================================

static MeleeBonusType get_melee_bonus_type(Player* p) {
    if (p->current_gear == GEAR_SPEC) {
        return MELEE_SPEC_BONUS_TYPES[p->melee_spec_weapon];
    }
    return MELEE_BONUS_SLASH;
}

static int get_attack_bonus(Player* p, AttackStyle style) {
    GearBonuses* g = get_slot_gear_bonuses(p);
    switch (style) {
        case ATTACK_STYLE_MELEE: {
            MeleeBonusType bonus = get_melee_bonus_type(p);
            switch (bonus) {
                case MELEE_BONUS_STAB: return g->stab_attack;
                case MELEE_BONUS_SLASH: return g->slash_attack;
                case MELEE_BONUS_CRUSH: return g->crush_attack;
                default: return g->slash_attack;
            }
        }
        case ATTACK_STYLE_RANGED:
            return g->ranged_attack;
        case ATTACK_STYLE_MAGIC:
            return g->magic_attack;
        default:
            return 0;
    }
}

static int get_defence_bonus_for_melee_type(Player* p, MeleeBonusType melee_type) {
    GearBonuses* g = get_slot_gear_bonuses(p);
    switch (melee_type) {
        case MELEE_BONUS_STAB: return g->stab_defence;
        case MELEE_BONUS_SLASH: return g->slash_defence;
        case MELEE_BONUS_CRUSH: return g->crush_defence;
        default: return g->slash_defence;
    }
}

static int get_defence_bonus(Player* defender, AttackStyle style, Player* attacker) {
    GearBonuses* g = get_slot_gear_bonuses(defender);
    switch (style) {
        case ATTACK_STYLE_MELEE: {
            MeleeBonusType bonus = get_melee_bonus_type(attacker);
            return get_defence_bonus_for_melee_type(defender, bonus);
        }
        case ATTACK_STYLE_RANGED:
            return g->ranged_defence;
        case ATTACK_STYLE_MAGIC:
            return g->magic_defence;
        default:
            return 0;
    }
}

static int get_strength_bonus(Player* p, AttackStyle style) {
    GearBonuses* g = get_slot_gear_bonuses(p);
    switch (style) {
        case ATTACK_STYLE_MELEE:
            return g->melee_strength;
        case ATTACK_STYLE_RANGED:
            return g->ranged_strength;
        case ATTACK_STYLE_MAGIC:
            return g->magic_strength;
        default:
            return 0;
    }
}

// ============================================================================
// HIT CHANCE AND MAX HIT FORMULAS
// ============================================================================

/**
 * OSRS accuracy formula.
 * if (attRoll > defRoll) hitChance = 1 - (defRoll+2)/(2*(attRoll+1))
 * else hitChance = attRoll/(2*(defRoll+1))
 */
static float calculate_hit_chance(OsrsPvp* env, Player* attacker, Player* defender,
                                   AttackStyle style, float acc_mult) {
    (void)env;
    int eff_attack = calculate_effective_attack(attacker, style);
    int attack_bonus = get_attack_bonus(attacker, style);
    int attack_roll = (int)(eff_attack * (attack_bonus + 64) * acc_mult);

    int eff_defence = calculate_effective_defence(defender, style);
    int defence_bonus = get_defence_bonus(defender, style, attacker);
    int defence_roll = eff_defence * (defence_bonus + 64);

    float hit_chance;
    if (attack_roll > defence_roll) {
        hit_chance = 1.0f - ((float)(defence_roll + 2) / (2.0f * (attack_roll + 1)));
    } else {
        hit_chance = (float)attack_roll / (2.0f * (defence_roll + 1));
    }

    return clampf(hit_chance, 0.0f, 1.0f);
}

static int calculate_max_hit(Player* p, AttackStyle style, float str_mult, int magic_base_hit) {
    int eff_strength = calculate_effective_strength(p, style);
    int strength_bonus = get_strength_bonus(p, style);

    int max_hit;
    if (style == ATTACK_STYLE_MAGIC) {
        int base_damage = magic_base_hit;
        float magic_mult = 1.0f;
        // Augury provides +4% magic damage
        if (p->offensive_prayer == OFFENSIVE_PRAYER_AUGURY) {
            magic_mult = 1.04f;
        }
        max_hit = (int)(base_damage * (1.0f + strength_bonus / 100.0f) * str_mult * magic_mult);
    } else {
        max_hit = (int)(((eff_strength * (strength_bonus + 64) + 320) / 640.0f) * str_mult);
    }

    if (p->has_dharok && style == ATTACK_STYLE_MELEE) {
        float hp_ratio = 1.0f - ((float)p->current_hitpoints / p->base_hitpoints);
        max_hit = (int)(max_hit * (1.0f + hp_ratio * hp_ratio));
    }

    return max_hit;
}

// ============================================================================
// MAGIC SPELL HELPERS
// ============================================================================

static inline int get_ice_freeze_ticks(int current_magic) {
    if (current_magic >= ICE_BARRAGE_LEVEL) return 32;
    if (current_magic >= ICE_BLITZ_LEVEL) return 24;
    if (current_magic >= ICE_BURST_LEVEL) return 16;
    return 8;
}

static inline int get_ice_base_hit(int current_magic) {
    if (current_magic >= ICE_BARRAGE_LEVEL) return ICE_BARRAGE_MAX_HIT;
    if (current_magic >= ICE_BLITZ_LEVEL) return ICE_BLITZ_MAX_HIT;
    if (current_magic >= ICE_BURST_LEVEL) return ICE_BURST_MAX_HIT;
    return ICE_RUSH_MAX_HIT;
}

static inline int get_blood_base_hit(int current_magic) {
    if (current_magic >= BLOOD_BARRAGE_LEVEL) return BLOOD_BARRAGE_MAX_HIT;
    if (current_magic >= BLOOD_BLITZ_LEVEL) return BLOOD_BLITZ_MAX_HIT;
    if (current_magic >= BLOOD_BURST_LEVEL) return BLOOD_BURST_MAX_HIT;
    return BLOOD_RUSH_MAX_HIT;
}

static inline int get_blood_heal_percent(int current_magic) {
    if (current_magic >= BLOOD_BARRAGE_LEVEL) return 25;
    if (current_magic >= BLOOD_BLITZ_LEVEL) return 20;
    if (current_magic >= BLOOD_BURST_LEVEL) return 15;
    return 10;
}

// ============================================================================
// HIT DELAY CALCULATIONS
// ============================================================================

static inline int magic_hit_delay(int distance) {
    return 1 + ((1 + distance) / 3);
}

static inline int ranged_hit_delay_default(int distance) {
    return 1 + ((3 + distance) / 6);
}

static inline int ranged_hit_delay_fast(int distance) {
    return 1 + (distance / 6);
}

static inline int ranged_hit_delay_ballista(int distance) {
    return 2 + ((1 + distance) / 6);
}

static inline int ranged_hit_delay_dbow_second(int distance) {
    return 1 + ((2 + distance) / 3);
}

static inline int ranged_hit_delay(int distance, int is_special, RangedSpecWeapon weapon) {
    if (!is_special) {
        return ranged_hit_delay_default(distance);
    }
    switch (weapon) {
        case RANGED_SPEC_DRAGON_KNIFE:
        case RANGED_SPEC_MORRIGANS:
            return ranged_hit_delay_fast(distance);
        case RANGED_SPEC_BALLISTA:
            return ranged_hit_delay_ballista(distance);
        case RANGED_SPEC_DARK_BOW:
        case RANGED_SPEC_MSB:
        case RANGED_SPEC_ACB:
        case RANGED_SPEC_ZCB:
        case RANGED_SPEC_NONE:
        default:
            return ranged_hit_delay_default(distance);
    }
}

// ============================================================================
// PRAYER CHECK
// ============================================================================

static int is_prayer_correct(Player* defender, AttackStyle incoming_style) {
    switch (incoming_style) {
        case ATTACK_STYLE_MELEE:
            return defender->prayer == PRAYER_PROTECT_MELEE;
        case ATTACK_STYLE_RANGED:
            return defender->prayer == PRAYER_PROTECT_RANGED;
        case ATTACK_STYLE_MAGIC:
            return defender->prayer == PRAYER_PROTECT_MAGIC;
        default:
            return 0;
    }
}

// ============================================================================
// HIT QUEUE
// ============================================================================

static void queue_hit(Player* attacker, Player* defender, int damage,
                     AttackStyle style, int delay, int is_special, int hit_success,
                     int freeze_ticks, int heal_percent, int drain_type, int drain_percent,
                     int flat_heal) {
    if (attacker->num_pending_hits >= MAX_PENDING_HITS) return;

    PendingHit* hit = &attacker->pending_hits[attacker->num_pending_hits++];
    hit->damage = damage;
    hit->ticks_until_hit = delay;
    hit->attack_type = style;
    hit->is_special = is_special;
    hit->hit_success = hit_success;
    hit->freeze_ticks = freeze_ticks;
    hit->heal_percent = heal_percent;
    hit->drain_type = drain_type;
    hit->drain_percent = drain_percent;
    hit->flat_heal = flat_heal;
    hit->is_morr_bleed = 0;
    hit->defender_prayer_at_attack = defender->prayer;

    // Track damage for XP drop equivalent (actual damage after prayer reduction)
    int actual_damage = damage;
    if (is_prayer_correct(defender, style)) {
        actual_damage = (int)(damage * 0.6f);
    }
    attacker->last_queued_hit_damage += actual_damage;
}

// ============================================================================
// DAMAGE APPLICATION
// ============================================================================

static void apply_damage(OsrsPvp* env, int attacker_idx, int defender_idx,
                         PendingHit* hit) {
    Player* attacker = &env->players[attacker_idx];
    Player* defender = &env->players[defender_idx];

    int damage = hit->damage;
    AttackStyle style = hit->attack_type;

    OverheadPrayer recorded_prayer = hit->defender_prayer_at_attack;
    int prayer_protects = 0;
    switch (style) {
        case ATTACK_STYLE_MELEE:
            prayer_protects = (recorded_prayer == PRAYER_PROTECT_MELEE);
            break;
        case ATTACK_STYLE_RANGED:
            prayer_protects = (recorded_prayer == PRAYER_PROTECT_RANGED);
            break;
        case ATTACK_STYLE_MAGIC:
            prayer_protects = (recorded_prayer == PRAYER_PROTECT_MAGIC);
            break;
        default:
            break;
    }
    if (prayer_protects) {
        damage = (int)(damage * 0.6f);
    }

    // Record hit event for event log
    defender->hit_landed_this_tick = 1;
    defender->hit_was_successful = hit->hit_success;
    defender->hit_damage += damage;
    defender->hit_style = style;
    defender->hit_defender_prayer = recorded_prayer;
    defender->hit_was_on_prayer = prayer_protects;
    defender->hit_attacker_idx = attacker_idx;
    defender->damage_applied_this_tick = damage;

    if (defender->veng_active && damage > 0) {
        int reflect_damage = (int)(damage * 0.75f);
        attacker->current_hitpoints -= reflect_damage;
        if (attacker->current_hitpoints < 0) {
            attacker->current_hitpoints = 0;
        }
        float reflect_scale = (float)reflect_damage / (float)attacker->base_hitpoints;
        attacker->total_damage_received += reflect_scale;
        defender->total_damage_dealt += reflect_scale;
        attacker->damage_received_scale += reflect_scale;
        defender->damage_dealt_scale += reflect_scale;
        defender->veng_active = 0;
    }

    /* ring of recoil / ring of suffering (i): reflects floor(damage * 0.1) + 1
       back to attacker. charges track total reflected damage (starts at 40).
       ring of recoil shatters at 0 charges; ring of suffering (i) never shatters. */
    if (has_recoil_effect(defender) && damage > 0 && defender->recoil_charges > 0) {
        int recoil = damage / 10 + 1;
        if (recoil > defender->recoil_charges) {
            recoil = defender->recoil_charges;
        }
        attacker->current_hitpoints -= recoil;
        if (attacker->current_hitpoints < 0) attacker->current_hitpoints = 0;
        float recoil_scale = (float)recoil / (float)attacker->base_hitpoints;
        attacker->total_damage_received += recoil_scale;
        defender->total_damage_dealt += recoil_scale;
        attacker->damage_received_scale += recoil_scale;
        defender->damage_dealt_scale += recoil_scale;

        /* ring of suffering (i) has infinite charges; ring of recoil shatters */
        if (defender->equipped[GEAR_SLOT_RING] == ITEM_RING_OF_RECOIL) {
            defender->recoil_charges -= recoil;
            if (defender->recoil_charges <= 0) {
                defender->recoil_charges = 0;
                defender->equipped[GEAR_SLOT_RING] = ITEM_NONE;
            }
        }
    }

    defender->current_hitpoints -= damage;
    if (defender->current_hitpoints < 0) {
        defender->current_hitpoints = 0;
    }
    float damage_scale = (float)damage / (float)defender->base_hitpoints;
    defender->total_damage_received += damage_scale;
    attacker->total_damage_dealt += damage_scale;
    defender->damage_received_scale += damage_scale;
    attacker->damage_dealt_scale += damage_scale;
    attacker->last_target_health_percent =
        (float)defender->current_hitpoints / (float)defender->base_hitpoints;

    if (hit->hit_success) {
        // Defence drain: percentage drain requires damage > 0 (not just accuracy success)
        if (hit->drain_type == 1 && damage > 0) {
            int drain = (int)(defender->current_defence * hit->drain_percent / 100.0f);
            defender->current_defence = clamp(defender->current_defence - drain, 1, 255);
        } else if (hit->drain_type == 2 && damage > 0) {
            defender->current_defence = clamp(defender->current_defence - damage, 1, 255);
        }

        if (hit->freeze_ticks > 0 && hit->hit_success && defender->freeze_immunity_ticks == 0 && defender->frozen_ticks == 0) {
            defender->frozen_ticks = hit->freeze_ticks;
            defender->freeze_immunity_ticks = hit->freeze_ticks + 5;
            defender->freeze_applied_this_tick = 1;
        }

        if (hit->heal_percent > 0) {
            int heal = (damage * hit->heal_percent) / 100;
            attacker->current_hitpoints = clamp(attacker->current_hitpoints + heal, 0, attacker->base_hitpoints);
        }
        if (hit->flat_heal > 0) {
            attacker->current_hitpoints = clamp(attacker->current_hitpoints + hit->flat_heal, 0, attacker->base_hitpoints);
        }
    }

    // Morrigan's javelin: set bleed amount when spec hit lands (post-prayer damage)
    if (hit->is_morr_bleed && hit->hit_success && damage > 0) {
        defender->morr_dot_remaining = damage;
    }

    if (attacker->prayer == PRAYER_SMITE && damage > 0 && !defender->is_lms) {
        int prayer_drain = damage / 4;
        defender->current_prayer = clamp(defender->current_prayer - prayer_drain, 0, defender->base_prayer);
    }
}

static void process_pending_hits(OsrsPvp* env, int attacker_idx, int defender_idx) {
    Player* attacker = &env->players[attacker_idx];

    for (int i = 0; i < attacker->num_pending_hits; i++) {
        PendingHit* hit = &attacker->pending_hits[i];
        hit->ticks_until_hit--;

        if (hit->ticks_until_hit < 0) {
            apply_damage(env, attacker_idx, defender_idx, hit);

            for (int j = i; j < attacker->num_pending_hits - 1; j++) {
                attacker->pending_hits[j] = attacker->pending_hits[j + 1];
            }
            attacker->num_pending_hits--;
            i--;
        }
    }
}

// ============================================================================
// HIT STATISTICS TRACKING
// ============================================================================

static inline void push_recent_attack(AttackStyle* buffer, int* index, AttackStyle style) {
    buffer[*index] = style;
    *index = (*index + 1) % HISTORY_SIZE;
}

static inline void push_recent_prayer(AttackStyle* buffer, int* index, OverheadPrayer prayer) {
    AttackStyle style = ATTACK_STYLE_NONE;
    if (prayer == PRAYER_PROTECT_MAGIC) {
        style = ATTACK_STYLE_MAGIC;
    } else if (prayer == PRAYER_PROTECT_RANGED) {
        style = ATTACK_STYLE_RANGED;
    } else if (prayer == PRAYER_PROTECT_MELEE) {
        style = ATTACK_STYLE_MELEE;
    }
    if (style == ATTACK_STYLE_NONE) {
        return;
    }
    buffer[*index] = style;
    *index = (*index + 1) % HISTORY_SIZE;
}

static inline void push_recent_bool(int* buffer, int* index, int value) {
    buffer[*index] = value ? 1 : 0;
    *index = (*index + 1) % HISTORY_SIZE;
}

static void register_hit_calculated(
    OsrsPvp* env,
    int attacker_idx,
    int defender_idx,
    AttackStyle style,
    int total_damage
) {
    Player* attacker = &env->players[attacker_idx];
    Player* defender = &env->players[defender_idx];
    GearBonuses* atk_gear = get_slot_gear_bonuses(attacker);
    VisibleGearBonuses visible_buf = {
        .magic_attack = atk_gear->magic_attack,
        .magic_strength = atk_gear->magic_strength,
        .ranged_attack = atk_gear->ranged_attack,
        .ranged_strength = atk_gear->ranged_strength,
        .melee_attack = max_int(atk_gear->stab_attack, max_int(atk_gear->slash_attack, atk_gear->crush_attack)),
        .melee_strength = atk_gear->melee_strength,
        .magic_defence = atk_gear->magic_defence,
        .ranged_defence = atk_gear->ranged_defence,
        .melee_defence = max_int(atk_gear->stab_defence, max_int(atk_gear->slash_defence, atk_gear->crush_defence)),
    };
    const VisibleGearBonuses* visible = &visible_buf;

    defender->total_target_hit_count += 1;
    push_recent_attack(defender->recent_target_attack_styles, &defender->recent_target_attack_index, style);

    if (style == ATTACK_STYLE_MAGIC) {
        defender->target_hit_magic_count += 1;
        defender->target_magic_accuracy = visible->magic_attack;
        defender->target_magic_strength = visible->magic_strength;
        defender->target_magic_gear_magic_defence = visible->magic_defence;
        defender->target_magic_gear_ranged_defence = visible->ranged_defence;
        defender->target_magic_gear_melee_defence = visible->melee_defence;
    } else if (style == ATTACK_STYLE_RANGED) {
        defender->target_hit_ranged_count += 1;
        defender->target_ranged_accuracy = visible->ranged_attack;
        defender->target_ranged_strength = visible->ranged_strength;
        defender->target_ranged_gear_magic_defence = visible->magic_defence;
        defender->target_ranged_gear_ranged_defence = visible->ranged_defence;
        defender->target_ranged_gear_melee_defence = visible->melee_defence;
    } else if (style == ATTACK_STYLE_MELEE) {
        defender->target_hit_melee_count += 1;
        if (visible->melee_strength >= defender->target_melee_strength) {
            defender->target_melee_accuracy = visible->melee_attack;
            defender->target_melee_strength = visible->melee_strength;
            defender->target_melee_gear_magic_defence = visible->magic_defence;
            defender->target_melee_gear_ranged_defence = visible->ranged_defence;
            defender->target_melee_gear_melee_defence = visible->melee_defence;
        }
    }

    if (defender->prayer == PRAYER_PROTECT_MAGIC) {
        defender->player_pray_magic_count += 1;
        push_recent_prayer(defender->recent_player_prayer_styles, &defender->recent_player_prayer_index, defender->prayer);
    } else if (defender->prayer == PRAYER_PROTECT_RANGED) {
        defender->player_pray_ranged_count += 1;
        push_recent_prayer(defender->recent_player_prayer_styles, &defender->recent_player_prayer_index, defender->prayer);
    } else if (defender->prayer == PRAYER_PROTECT_MELEE) {
        defender->player_pray_melee_count += 1;
        push_recent_prayer(defender->recent_player_prayer_styles, &defender->recent_player_prayer_index, defender->prayer);
    }

    int defender_prayed_correctly = is_prayer_correct(defender, style);
    if (!defender_prayed_correctly) {
        defender->target_hit_correct_count += 1;
        push_recent_bool(defender->recent_target_hit_correct, &defender->recent_target_hit_correct_index, 1);
    } else {
        defender->player_prayed_correct = 1;
        push_recent_bool(defender->recent_target_hit_correct, &defender->recent_target_hit_correct_index, 0);
    }

    // Track whether attack was "on prayer" for event logging
    attacker->attack_was_on_prayer = defender_prayed_correctly;

    push_recent_attack(attacker->recent_player_attack_styles, &attacker->recent_player_attack_index, style);
    if (style == ATTACK_STYLE_MAGIC) {
        attacker->player_hit_magic_count += 1;
    } else if (style == ATTACK_STYLE_RANGED) {
        attacker->player_hit_ranged_count += 1;
    } else if (style == ATTACK_STYLE_MELEE) {
        attacker->player_hit_melee_count += 1;
    }
    attacker->tick_damage_scale = (float)total_damage / (float)defender->base_hitpoints;
    attacker->total_target_pray_count += 1;

    if (defender->prayer == PRAYER_PROTECT_MAGIC) {
        attacker->target_pray_magic_count += 1;
        push_recent_prayer(attacker->recent_target_prayer_styles, &attacker->recent_target_prayer_index, defender->prayer);
    } else if (defender->prayer == PRAYER_PROTECT_RANGED) {
        attacker->target_pray_ranged_count += 1;
        push_recent_prayer(attacker->recent_target_prayer_styles, &attacker->recent_target_prayer_index, defender->prayer);
    } else if (defender->prayer == PRAYER_PROTECT_MELEE) {
        attacker->target_pray_melee_count += 1;
        push_recent_prayer(attacker->recent_target_prayer_styles, &attacker->recent_target_prayer_index, defender->prayer);
    }

    if (is_prayer_correct(defender, style)) {
        attacker->target_pray_correct_count += 1;
        attacker->target_prayed_correct = 1;
        push_recent_bool(attacker->recent_target_prayer_correct, &attacker->recent_target_prayer_correct_index, 1);
    } else {
        push_recent_bool(attacker->recent_target_prayer_correct, &attacker->recent_target_prayer_correct_index, 0);
    }
}

// ============================================================================
// SPECIAL ATTACK IMPLEMENTATIONS
// ============================================================================

/** Dragon claws 4-hit cascade algorithm. */
static void perform_dragon_claws_spec(OsrsPvp* env, int attacker_idx, int defender_idx) {
    Player* attacker = &env->players[attacker_idx];
    Player* defender = &env->players[defender_idx];

    float acc_mult = get_melee_spec_acc_mult(MELEE_SPEC_DRAGON_CLAWS);
    float hit_chance = calculate_hit_chance(env, attacker, defender, ATTACK_STYLE_MELEE, acc_mult);
    int max_hit = calculate_max_hit(attacker, ATTACK_STYLE_MELEE, 1.0f, 30);
    /* prayer reduction is handled uniformly in apply_damage() for all attacks */

    int first, second, third, fourth;

    int roll1 = rand_float(env) < hit_chance;
    int roll2 = rand_float(env) < hit_chance;
    int roll3 = rand_float(env) < hit_chance;
    int roll4 = rand_float(env) < hit_chance;

    if (roll1) {
        int min_first = (int)(max_hit * 0.5f);
        first = min_first + rand_int(env, max_hit - min_first);
        second = first / 2;
        third = second / 2;
        fourth = third + rand_int(env, 2);
    } else if (roll2) {
        first = 0;
        int min_second = (int)(max_hit * 0.375f);
        int max_second = (int)(max_hit * 0.875f);
        second = min_second + rand_int(env, max_second - min_second + 1);
        third = second / 2;
        fourth = third + rand_int(env, 2);
    } else if (roll3) {
        first = 0;
        second = 0;
        int min_third = (int)(max_hit * 0.25f);
        int max_third = (int)(max_hit * 0.75f);
        third = min_third + rand_int(env, max_third - min_third + 1);
        fourth = third + rand_int(env, 2);
    } else if (roll4) {
        first = 0;
        second = 0;
        third = 0;
        int min_fourth = (int)(max_hit * 0.25f);
        int max_fourth = (int)(max_hit * 1.25f);
        fourth = min_fourth + rand_int(env, max_fourth - min_fourth + 1);
    } else {
        first = 0;
        second = 0;
        third = rand_int(env, 2);
        fourth = third;
    }

    queue_hit(attacker, defender, first, ATTACK_STYLE_MELEE, 0, 1, first > 0, 0, 0, 0, 0, 0);
    queue_hit(attacker, defender, second, ATTACK_STYLE_MELEE, 0, 1, second > 0, 0, 0, 0, 0, 0);
    queue_hit(attacker, defender, third, ATTACK_STYLE_MELEE, 0, 1, third > 0, 0, 0, 0, 0, 0);
    queue_hit(attacker, defender, fourth, ATTACK_STYLE_MELEE, 0, 1, fourth > 0, 0, 0, 0, 0, 0);

    int total_damage = first + second + third + fourth;
    register_hit_calculated(env, attacker_idx, defender_idx, ATTACK_STYLE_MELEE, total_damage);
}

/** Voidwaker: guaranteed magic damage 50-150% of melee max hit. */
static void perform_voidwaker_spec(OsrsPvp* env, int attacker_idx, int defender_idx) {
    Player* attacker = &env->players[attacker_idx];
    Player* defender = &env->players[defender_idx];

    int max_melee_hit = calculate_max_hit(attacker, ATTACK_STYLE_MELEE, 1.0f, 30);
    int min_damage = (int)(max_melee_hit * 0.5f);
    int max_damage = (int)(max_melee_hit * 1.5f);
    int damage = min_damage + rand_int(env, max_damage - min_damage + 1);

    // prayer reduction handled uniformly in apply_damage()
    queue_hit(attacker, defender, damage, ATTACK_STYLE_MAGIC, 0, 1, damage > 0, 0, 0, 0, 0, 0);
    register_hit_calculated(env, attacker_idx, defender_idx, ATTACK_STYLE_MAGIC, damage);
}

/** VLS "Feint": 20-120% of base max hit, accuracy vs 25% of def. */
static void perform_vestas_spec(OsrsPvp* env, int attacker_idx, int defender_idx) {
    Player* attacker = &env->players[attacker_idx];
    Player* defender = &env->players[defender_idx];

    int base_max = calculate_max_hit(attacker, ATTACK_STYLE_MELEE, 1.0f, 30);
    int max_hit = (int)(base_max * 1.20f);
    int min_hit = (int)(base_max * 0.20f);

    // Accuracy rolled against 25% of opponent's defence roll
    int eff_attack = calculate_effective_attack(attacker, ATTACK_STYLE_MELEE);
    int attack_bonus = get_attack_bonus(attacker, ATTACK_STYLE_MELEE);
    int attack_roll = eff_attack * (attack_bonus + 64);

    int eff_defence = calculate_effective_defence(defender, ATTACK_STYLE_MELEE);
    int defence_bonus = get_defence_bonus(defender, ATTACK_STYLE_MELEE, attacker);
    int defence_roll = (int)(eff_defence * (defence_bonus + 64) * 0.25f);

    float hit_chance;
    if (attack_roll > defence_roll) {
        hit_chance = 1.0f - ((float)(defence_roll + 2) / (2.0f * (attack_roll + 1)));
    } else {
        hit_chance = (float)attack_roll / (2.0f * (defence_roll + 1));
    }
    hit_chance = clampf(hit_chance, 0.0f, 1.0f);

    int damage = 0;
    int hit_success = 0;
    if (rand_float(env) < hit_chance) {
        damage = min_hit + rand_int(env, max_hit - min_hit + 1);
        hit_success = 1;
    }

    queue_hit(attacker, defender, damage, ATTACK_STYLE_MELEE, 0, 1, hit_success, 0, 0, 0, 0, 0);
    register_hit_calculated(env, attacker_idx, defender_idx, ATTACK_STYLE_MELEE, damage);
}

/** Statius warhammer "Smash": 35% cost, 25-125% of max hit, 30% defence drain on damage > 0. */
static void perform_statius_spec(OsrsPvp* env, int attacker_idx, int defender_idx) {
    Player* attacker = &env->players[attacker_idx];
    Player* defender = &env->players[defender_idx];

    float str_mult = get_melee_spec_str_mult(MELEE_SPEC_DWH);
    int max_hit = calculate_max_hit(attacker, ATTACK_STYLE_MELEE, str_mult, 30);
    int min_hit = (int)(max_hit * 0.25f);

    float acc_mult = get_melee_spec_acc_mult(MELEE_SPEC_DWH);
    float hit_chance = calculate_hit_chance(env, attacker, defender, ATTACK_STYLE_MELEE, acc_mult);

    int damage = 0;
    int hit_success = 0;
    if (rand_float(env) < hit_chance) {
        damage = min_hit + rand_int(env, max_hit - min_hit + 1);
        hit_success = 1;
    }

    // drain_type=1, drain_percent=30: 30% of current defence drained on damage > 0
    queue_hit(attacker, defender, damage, ATTACK_STYLE_MELEE, 0, 1, hit_success, 0, 0, 1, 30, 0);
    register_hit_calculated(env, attacker_idx, defender_idx, ATTACK_STYLE_MELEE, damage);
}

/** Dark bow: 2 hits, each min 8 max 48, 1.5x str mult. */
static void perform_dark_bow_spec(OsrsPvp* env, int attacker_idx, int defender_idx) {
    Player* attacker = &env->players[attacker_idx];
    Player* defender = &env->players[defender_idx];

    float acc_mult = get_ranged_spec_acc_mult(RANGED_SPEC_DARK_BOW);
    float str_mult = get_ranged_spec_str_mult(RANGED_SPEC_DARK_BOW);
    float hit_chance = calculate_hit_chance(env, attacker, defender, ATTACK_STYLE_RANGED, acc_mult);
    int max_hit = calculate_max_hit(attacker, ATTACK_STYLE_RANGED, str_mult, 30);
    int distance = chebyshev_distance(attacker->x, attacker->y, defender->x, defender->y);
    int first_delay = ranged_hit_delay_default(distance);
    int second_delay = ranged_hit_delay_dbow_second(distance);

    int total_damage = 0;
    for (int i = 0; i < 2; i++) {
        int damage;
        if (rand_float(env) < hit_chance) {
            damage = rand_int(env, max_hit + 1);
            damage = clamp(damage, 8, 48);
        } else {
            damage = 8;
        }
        total_damage += damage;
        int hit_delay = (i == 0) ? first_delay : second_delay;
        queue_hit(attacker, defender, damage, ATTACK_STYLE_RANGED, hit_delay, 1, damage > 0, 0, 0, 0, 0, 0);
    }
    register_hit_calculated(env, attacker_idx, defender_idx, ATTACK_STYLE_RANGED, total_damage);
}

// ============================================================================
// ATTACK AVAILABILITY CHECKS
// ============================================================================

/** Check if attack is available (respects config). */
static inline int is_attack_available(Player* p) {
    if (ONLY_SWITCH_GEAR_WHEN_ATTACK_SOON && remaining_ticks(p->attack_timer) > 0) {
        return 0;
    }
    return 1;
}

static inline int is_melee_weapon_equipped(Player* p) {
    return get_slot_weapon_attack_style(p) == ATTACK_STYLE_MELEE;
}

static inline int is_ranged_weapon_equipped(Player* p) {
    return get_slot_weapon_attack_style(p) == ATTACK_STYLE_RANGED;
}

static inline int is_melee_spec_weapon_equipped(Player* p) {
    return p->melee_spec_weapon != MELEE_SPEC_NONE;
}

static inline int is_ranged_spec_weapon_equipped(Player* p) {
    return p->ranged_spec_weapon != RANGED_SPEC_NONE;
}

static inline int is_magic_spec_weapon_equipped(Player* p) {
    return p->magic_spec_weapon != MAGIC_SPEC_NONE;
}

/** Check if player can cast ice spells. */
static inline int can_cast_ice_spell(Player* p) {
    if (p->is_lunar_spellbook) {
        return 0;
    }
    return p->current_magic >= ICE_RUSH_LEVEL;
}

/** Check if player can cast blood spells. */
static inline int can_cast_blood_spell(Player* p) {
    if (p->is_lunar_spellbook) {
        return 0;
    }
    return p->current_magic >= BLOOD_RUSH_LEVEL;
}

/** Check if ranged attack is available. */
static inline int is_ranged_attack_available(Player* p) {
    if (!is_attack_available(p)) {
        return 0;
    }
    return is_ranged_weapon_equipped(p);
}

/** Check if melee is possible (in range or can move). */
static inline int can_melee(Player* p, Player* t) {
    return is_in_melee_range(p, t) || can_move(p);
}

/** Check if melee attack is available. */
static inline int is_melee_attack_available(Player* p, Player* t) {
    if (!is_attack_available(p)) {
        return 0;
    }
    (void)t;
    return is_melee_weapon_equipped(p);
}

/** Check if melee spec weapon is two-handed. */
static inline int is_melee_spec_two_handed(MeleeSpecWeapon weapon) {
    switch (weapon) {
        case MELEE_SPEC_AGS:
        case MELEE_SPEC_DRAGON_CLAWS:
        case MELEE_SPEC_BGS:
        case MELEE_SPEC_ZGS:
        case MELEE_SPEC_SGS:
        case MELEE_SPEC_ANCIENT_GS:
        case MELEE_SPEC_ABYSSAL_BLUDGEON:
            return 1;
        case MELEE_SPEC_NONE:
        case MELEE_SPEC_GRANITE_MAUL:
        case MELEE_SPEC_DRAGON_DAGGER:
        case MELEE_SPEC_VOIDWAKER:
        case MELEE_SPEC_DWH:
        case MELEE_SPEC_VESTAS:
        case MELEE_SPEC_ABYSSAL_DAGGER:
        case MELEE_SPEC_DRAGON_LONGSWORD:
        case MELEE_SPEC_DRAGON_MACE:
            return 0;
    }
    return 0;
}

/** Check if player has free inventory slot (for 2h weapon). */
static inline int has_free_inventory_slot(Player* p) {
    int food_slots = p->food_count + p->karambwan_count;
    int max_food_slots = MAXED_FOOD_COUNT + MAXED_KARAMBWAN_COUNT;
    return food_slots < max_food_slots;
}

/** Check if player can equip two-handed weapon. */
static inline int can_equip_two_handed_weapon(Player* p) {
    return has_free_inventory_slot(p) || p->equipped[GEAR_SLOT_SHIELD] == ITEM_NONE;
}

/** Check if melee spec is usable. */
static inline int can_spec(Player* p) {
    int cost = get_melee_spec_cost(p->melee_spec_weapon);
    return p->melee_spec_weapon != MELEE_SPEC_NONE && p->special_energy >= cost;
}

/** Check if granite maul spec is available. */
static inline int is_granite_maul_attack_available(Player* p) {
    if (p->melee_spec_weapon != MELEE_SPEC_GRANITE_MAUL) {
        return 0;
    }
    return p->special_energy >= get_melee_spec_cost(MELEE_SPEC_GRANITE_MAUL);
}

/** Check if melee spec attack is available. */
static inline int is_melee_spec_attack_available(Player* p, Player* t) {
    (void)t;
    if (!is_granite_maul_attack_available(p) && !is_attack_available(p)) {
        return 0;
    }
    if (is_melee_spec_two_handed(p->melee_spec_weapon) && !can_equip_two_handed_weapon(p)) {
        return 0;
    }
    if (!is_melee_weapon_equipped(p) || !is_melee_spec_weapon_equipped(p)) {
        return 0;
    }
    return can_spec(p);
}

/** Check if ranged spec attack is available. */
static inline int is_ranged_spec_attack_available(Player* p) {
    if (!is_attack_available(p)) {
        return 0;
    }
    if (!is_ranged_attack_available(p)) {
        return 0;
    }
    if (p->ranged_spec_weapon == RANGED_SPEC_NONE) {
        return 0;
    }
    if (!is_ranged_spec_weapon_equipped(p)) {
        return 0;
    }
    return p->special_energy >= get_ranged_spec_cost(p->ranged_spec_weapon);
}

/** Check if ice attack is available. */
static inline int is_ice_attack_available(Player* p) {
    if (p->is_lunar_spellbook) {
        return 0;
    }
    return can_cast_ice_spell(p) && is_attack_available(p);
}

/** Check if blood attack is available. */
static inline int is_blood_attack_available(Player* p) {
    if (p->is_lunar_spellbook) {
        return 0;
    }
    return can_cast_blood_spell(p) && is_attack_available(p);
}

/** Check if special attack energy is available for any equipped spec weapon. */
static inline int can_toggle_spec(Player* p) {
    if (is_melee_spec_weapon_equipped(p) && p->melee_spec_weapon != MELEE_SPEC_NONE) {
        if (is_melee_spec_two_handed(p->melee_spec_weapon) && !can_equip_two_handed_weapon(p)) {
            return 0;
        }
        return p->special_energy >= get_melee_spec_cost(p->melee_spec_weapon);
    }
    if (is_ranged_spec_weapon_equipped(p) && p->ranged_spec_weapon != RANGED_SPEC_NONE) {
        return p->special_energy >= get_ranged_spec_cost(p->ranged_spec_weapon);
    }
    if (is_magic_spec_weapon_equipped(p) && p->magic_spec_weapon != MAGIC_SPEC_NONE) {
        return p->special_energy >= get_magic_spec_cost(p->magic_spec_weapon);
    }
    return 0;
}

/** Check if special attack should fire for the current weapon style.
 *  Auto-specs when a spec weapon is equipped and energy is sufficient. */
static inline int is_special_ready(Player* p, AttackStyle style) {
    switch (style) {
        case ATTACK_STYLE_MELEE:
            if (!is_melee_spec_weapon_equipped(p) || p->melee_spec_weapon == MELEE_SPEC_NONE) {
                return 0;
            }
            if (is_melee_spec_two_handed(p->melee_spec_weapon) && !can_equip_two_handed_weapon(p)) {
                return 0;
            }
            return p->special_energy >= get_melee_spec_cost(p->melee_spec_weapon);
        case ATTACK_STYLE_RANGED:
            if (!is_ranged_spec_weapon_equipped(p) || p->ranged_spec_weapon == RANGED_SPEC_NONE) {
                return 0;
            }
            return p->special_energy >= get_ranged_spec_cost(p->ranged_spec_weapon);
        case ATTACK_STYLE_MAGIC:
            if (!is_magic_spec_weapon_equipped(p) || p->magic_spec_weapon == MAGIC_SPEC_NONE) {
                return 0;
            }
            return p->special_energy >= get_magic_spec_cost(p->magic_spec_weapon);
        default:
            return 0;
    }
}

/** Get ticks until next hit lands on opponent. */
static inline int get_ticks_until_next_hit(Player* p) {
    int min_ticks = -1;
    for (int i = 0; i < p->num_pending_hits; i++) {
        if (min_ticks < 0 || p->pending_hits[i].ticks_until_hit < min_ticks) {
            min_ticks = p->pending_hits[i].ticks_until_hit;
        }
    }
    return min_ticks;
}

// ============================================================================
// WEAPON RANGE
// ============================================================================

/** Weapon type for attack ranges. */
typedef enum {
    WEAPON_TYPE_STANDARD = 0,
    WEAPON_TYPE_HALBERD
} WeaponType;

/** Check if melee spec weapon is halberd-type (2-tile range). */
static inline int is_halberd_weapon(MeleeSpecWeapon weapon) {
    (void)weapon;
    return 0;
}

/**
 * Get effective attack range for player based on current gear and weapon.
 *
 * @param p     Player
 * @param style Attack style
 * @return Attack range in tiles
 */
static inline int get_attack_range(Player* p, AttackStyle style) {
    switch (style) {
        case ATTACK_STYLE_MELEE:
            if (is_halberd_weapon(p->melee_spec_weapon)) {
                return 2;
            }
            return 1;
        case ATTACK_STYLE_RANGED:
            return get_slot_gear_bonuses(p)->attack_range;
        case ATTACK_STYLE_MAGIC:
            return get_slot_gear_bonuses(p)->attack_range;
        default:
            return 1;
    }
}

// ============================================================================
// ATTACK EXECUTION
// ============================================================================

/**
 * Perform attack on defender.
 *
 * Handles all attack types (melee/ranged/magic), special attacks,
 * and weapon-specific mechanics (dragon claws, voidwaker, dark bow, etc.).
 *
 * @param env          Environment state
 * @param attacker_idx Attacker player index
 * @param defender_idx Defender player index
 * @param style        Attack style (melee/ranged/magic)
 * @param is_special   1 if using special attack
 * @param magic_type   Magic spell type (1=ice, 2=blood, 0=none)
 * @param distance     Distance to target in tiles
 */
static void perform_attack(OsrsPvp* env, int attacker_idx, int defender_idx,
                           AttackStyle style, int is_special, int magic_type, int distance) {
    Player* attacker = &env->players[attacker_idx];
    Player* defender = &env->players[defender_idx];

    int dx = abs_int(attacker->x - defender->x);
    int dy = abs_int(attacker->y - defender->y);
    attacker->last_attack_dx = dx;
    attacker->last_attack_dy = dy;
    attacker->last_attack_dist = (dx > dy) ? dx : dy;

    if (style == ATTACK_STYLE_MELEE && !is_in_melee_range(attacker, defender)) {
        return;
    }

    float acc_mult = 1.0f;
    float str_mult = 1.0f;
    int hit_count = 1;
    int spec_cost = 0;
    int is_instant = 0;
    int applies_freeze = 0;
    int drains_def = 0;
    int def_drain_percent = 0;
    int heals_attacker = 0;
    int bleed_damage = 0;
    int use_dragon_claws = 0;
    int use_voidwaker = 0;
    int use_vestas = 0;
    int use_statius = 0;
    int use_dark_bow = 0;
    int was_special_requested = is_special;

    if (is_special) {
        switch (style) {
            case ATTACK_STYLE_MELEE: {
                MeleeSpecWeapon weapon = attacker->melee_spec_weapon;
                spec_cost = get_melee_spec_cost(weapon);
                acc_mult = get_melee_spec_acc_mult(weapon);
                str_mult = get_melee_spec_str_mult(weapon);

                switch (weapon) {
                    case MELEE_SPEC_DRAGON_CLAWS:
                        use_dragon_claws = 1;
                        break;
                    case MELEE_SPEC_VOIDWAKER:
                        use_voidwaker = 1;
                        break;
                    case MELEE_SPEC_VESTAS:
                        use_vestas = 1;
                        break;
                    case MELEE_SPEC_DRAGON_DAGGER:
                    case MELEE_SPEC_ABYSSAL_DAGGER:
                        hit_count = 2;
                        break;
                    case MELEE_SPEC_GRANITE_MAUL:
                        is_instant = 1;
                        break;
                    case MELEE_SPEC_DWH:
                        use_statius = 1;
                        break;
                    case MELEE_SPEC_BGS:
                        drains_def = 2;
                        break;
                    case MELEE_SPEC_ZGS:
                        applies_freeze = 1;
                        break;
                    case MELEE_SPEC_SGS:
                        heals_attacker = 1;
                        break;
                    case MELEE_SPEC_ANCIENT_GS:
                        bleed_damage = 25;
                        break;
                    default:
                        break;
                }
                break;
            }
            case ATTACK_STYLE_RANGED: {
                RangedSpecWeapon weapon = attacker->ranged_spec_weapon;
                spec_cost = get_ranged_spec_cost(weapon);
                acc_mult = get_ranged_spec_acc_mult(weapon);
                str_mult = get_ranged_spec_str_mult(weapon);

                switch (weapon) {
                    case RANGED_SPEC_DARK_BOW:
                        use_dark_bow = 1;
                        break;
                    case RANGED_SPEC_DRAGON_KNIFE:
                    case RANGED_SPEC_MSB:
                        hit_count = 2;
                        break;
                    default:
                        break;
                }
                break;
            }
            case ATTACK_STYLE_MAGIC: {
                MagicSpecWeapon weapon = attacker->magic_spec_weapon;
                spec_cost = get_magic_spec_cost(weapon);
                acc_mult = get_magic_spec_acc_mult(weapon);
                break;
            }
            default:
                break;
        }

        if (attacker->special_energy < spec_cost) {
            is_special = 0;
            acc_mult = 1.0f;
            str_mult = 1.0f;
            hit_count = 1;
            use_dragon_claws = 0;
            use_voidwaker = 0;
            use_vestas = 0;
            use_statius = 0;
            use_dark_bow = 0;
            attacker->special_active = 0;
        } else if (is_special) {
            attacker->special_energy -= spec_cost;
            if (!attacker->spec_regen_active && attacker->special_energy < 100) {
                attacker->spec_regen_active = 1;
                attacker->special_regen_ticks = 0;
            }
            attacker->special_active = 0;
        }
    }

    // Update gear based on attack style
    // When spec fails (insufficient energy), the player is still holding the
    // spec weapon with spec gear equipped — use GEAR_SPEC bonuses, not GEAR_MELEE.
    // Using GEAR_MELEE here gave whip-level str bonus (115) to DDS pokes (should be 73).
    if (style == ATTACK_STYLE_MELEE) {
        attacker->current_gear = was_special_requested ? GEAR_SPEC : GEAR_MELEE;
    } else if (style == ATTACK_STYLE_RANGED) {
        attacker->current_gear = GEAR_RANGED;
    } else if (style == ATTACK_STYLE_MAGIC) {
        attacker->current_gear = GEAR_MAGE;
    }

    if (is_special && use_dragon_claws) {
        perform_dragon_claws_spec(env, attacker_idx, defender_idx);
        goto post_attack;
    }
    if (is_special && use_voidwaker) {
        perform_voidwaker_spec(env, attacker_idx, defender_idx);
        goto post_attack;
    }
    if (is_special && use_vestas) {
        perform_vestas_spec(env, attacker_idx, defender_idx);
        goto post_attack;
    }
    if (is_special && use_statius) {
        perform_statius_spec(env, attacker_idx, defender_idx);
        goto post_attack;
    }
    if (is_special && use_dark_bow) {
        perform_dark_bow_spec(env, attacker_idx, defender_idx);
        goto post_attack;
    }

    // Zuriel's staff passive: 10% increased accuracy on ice spells
    int has_zuriels = (attacker->equipped[GEAR_SLOT_WEAPON] == ITEM_ZURIELS_STAFF);
    if (has_zuriels && style == ATTACK_STYLE_MAGIC && !is_special && magic_type == 1) {
        acc_mult *= 1.10f;
    }

    float hit_chance = calculate_hit_chance(env, attacker, defender, style, acc_mult);
    int magic_base_hit = 30;
    if (style == ATTACK_STYLE_MAGIC && !is_special) {
        if (magic_type == 1) {
            magic_base_hit = get_ice_base_hit(attacker->current_magic);
        } else if (magic_type == 2) {
            magic_base_hit = get_blood_base_hit(attacker->current_magic);
        }
    }
    int max_hit = calculate_max_hit(attacker, style, str_mult, magic_base_hit);

    int hit_delay;
    switch (style) {
        case ATTACK_STYLE_MELEE:
            hit_delay = 0;
            break;
        case ATTACK_STYLE_RANGED:
            hit_delay = ranged_hit_delay(distance, is_special, attacker->ranged_spec_weapon);
            break;
        case ATTACK_STYLE_MAGIC:
            hit_delay = magic_hit_delay(distance);
            break;
        default:
            hit_delay = 0;
    }

    int freeze_ticks = 0;
    int heal_percent = 0;
    int drain_type = 0;
    int drain_percent = 0;

    if (is_special) {
        if (drains_def == 1) {
            drain_type = 1;
            drain_percent = def_drain_percent;
        } else if (drains_def == 2) {
            drain_type = 2;
        }
        if (applies_freeze) {
            freeze_ticks = 32;
        }
        if (heals_attacker) {
            heal_percent = 50;
        }
    }

    if (style == ATTACK_STYLE_MAGIC && !is_special) {
        if (magic_type == 1) {
            freeze_ticks = get_ice_freeze_ticks(attacker->current_magic);
            // Zuriel's staff passive: 10% increased freeze duration
            if (has_zuriels) {
                freeze_ticks = (int)(freeze_ticks * 1.10f);
            }
        } else if (magic_type == 2) {
            heal_percent = get_blood_heal_percent(attacker->current_magic);
            // Zuriel's staff passive: 50% increased blood spell healing
            if (has_zuriels) {
                heal_percent = (int)(heal_percent * 1.50f);
            }
        }
    }

    int total_damage = 0;
    int any_hit_success = 0;
    int apply_magic_freeze_on_calc = (style == ATTACK_STYLE_MAGIC && !is_special && magic_type == 1);

    // Diamond bolt (e) armor piercing: 11% chance (Kandarin hard diary active in LMS)
    // Effect: guaranteed hit, 0-115% max hit
    int has_diamond_bolts = (attacker->equipped[GEAR_SLOT_AMMO] == ITEM_DIAMOND_BOLTS_E);

    for (int i = 0; i < hit_count; i++) {
        int damage = 0;
        int hit_success = 0;

        // Check for diamond bolt armor piercing proc on ranged non-special attacks
        int armor_piercing_proc = 0;
        if (style == ATTACK_STYLE_RANGED && !is_special && has_diamond_bolts) {
            if (rand_float(env) < 0.11f) {
                armor_piercing_proc = 1;
            }
        }

        if (armor_piercing_proc) {
            // Armor piercing: guaranteed hit, 0-115% max hit
            hit_success = 1;
            any_hit_success = 1;
            int boosted_max = (int)(max_hit * 1.15f);
            damage = rand_int(env, boosted_max + 1);
            total_damage += damage;
        } else if (rand_float(env) < hit_chance) {
            hit_success = 1;
            any_hit_success = 1;
            damage = rand_int(env, max_hit + 1);
            total_damage += damage;
        }
        int queued_freeze_ticks = freeze_ticks;
        if (apply_magic_freeze_on_calc) {
            if (hit_success && defender->freeze_immunity_ticks == 0 && defender->frozen_ticks == 0) {
                defender->frozen_ticks = freeze_ticks;
                defender->freeze_immunity_ticks = freeze_ticks + 5;
                defender->freeze_applied_this_tick = 1;
                defender->hit_attacker_idx = attacker_idx;
            }
            queued_freeze_ticks = 0;
        }
        queue_hit(attacker, defender, damage, style, hit_delay, is_special,
                  hit_success, queued_freeze_ticks, heal_percent, drain_type, drain_percent, 0);
    }
    register_hit_calculated(env, attacker_idx, defender_idx, style, total_damage);

    // Ancient GS Blood Sacrifice: 25 magic damage at 8 ticks + heal attacker 15% target max HP (cap 15 PvP)
    if (is_special && bleed_damage > 0 && any_hit_success) {
        int ags_heal = clamp((int)(defender->base_hitpoints * 0.15f), 0, 15);
        queue_hit(attacker, defender, bleed_damage, ATTACK_STYLE_MAGIC, 8, 1, 1, 0, 0, 0, 0, ags_heal);
    }

    // Morrigan's javelin Phantom Strike: timer starts at calc, bleed amount set on hit landing
    if (is_special && style == ATTACK_STYLE_RANGED &&
        attacker->ranged_spec_weapon == RANGED_SPEC_MORRIGANS &&
        any_hit_success && total_damage > 0) {
        attacker->pending_hits[attacker->num_pending_hits - 1].is_morr_bleed = 1;
        defender->morr_dot_tick_counter = 3;
    }

post_attack:
    attacker->just_attacked = 1;
    // Voidwaker deals magic damage despite being a melee spec weapon
    attacker->last_attack_style = (is_special && use_voidwaker) ? ATTACK_STYLE_MAGIC : style;
    // Track actual attack style for reward shaping (set AFTER attack fires)
    attacker->attack_style_this_tick = (is_special && use_voidwaker) ? ATTACK_STYLE_MAGIC : style;
    attacker->magic_type_this_tick = magic_type;  // 0=none, 1=ice, 2=blood
    attacker->used_special_this_tick = is_special;

    int attack_speed = get_slot_gear_bonuses(attacker)->attack_speed;
    if (!is_instant) {
        attacker->attack_timer = attack_speed - 1;
        attacker->attack_timer_uncapped = attack_speed - 1;
        attacker->has_attack_timer = 1;
    }
}

#endif // OSRS_PVP_COMBAT_H
