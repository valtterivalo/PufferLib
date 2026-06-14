/**
 * @fileoverview Scripted opponent policies implemented in C.
 *
 * Ports the Python opponent policies (opponents/ *.py) to C for use within
 * c_step(). Eliminates the Python round-trip for opponent action
 * generation during training with scripted opponents.
 *
 * Opponent reads game state directly from Player structs instead of parsing
 * observation arrays, which is both faster and avoids float normalization.
 *
 * Actions are direct head-value assignments: int actions[NUM_ACTION_HEADS].
 * Gear switches use loadout presets (LOADOUT_MELEE, LOADOUT_RANGE, etc.)
 * instead of per-slot equip actions.
 *
 * Phase 1 policies: TrueRandom, Panicking, WeakRandom, SemiRandom,
 * StickyPrayer, Beginner, BetterRandom, Improved.
 * Phase 2 policies: Onetick, UnpredictableImproved, UnpredictableOnetick.
 * Mixed wrappers: MixedEasy, MixedMedium, MixedHard, MixedHardBalanced.
 */

#ifndef OSRS_PVP_OPPONENTS_H
#define OSRS_PVP_OPPONENTS_H

/* This header is included from osrs_pvp.h AFTER all other headers,
 * so osrs_types.h, osrs_items.h (via gear.h), and
 * osrs_pvp_actions.h are already available. */

/* OpponentType enum and OpponentState struct are in osrs_types.h */

/* Attack style enum for opponent internal use */
#define OPP_STYLE_MAGE    0
#define OPP_STYLE_RANGED  1
#define OPP_STYLE_MELEE   2
#define OPP_STYLE_SPEC    3

#define OPP_STYLE_MASK_MAGE   (1 << OPP_STYLE_MAGE)
#define OPP_STYLE_MASK_RANGED (1 << OPP_STYLE_RANGED)
#define OPP_STYLE_MASK_MELEE  (1 << OPP_STYLE_MELEE)
#define OPP_STYLE_MASK_ALL    (OPP_STYLE_MASK_MAGE | OPP_STYLE_MASK_RANGED | OPP_STYLE_MASK_MELEE)

static inline const char* osrs_pvp_opponent_type_name(OpponentType type) {
    switch (type) {
        case OPP_NONE: return "Opponent";
        case OPP_TRUE_RANDOM: return "True Random";
        case OPP_PANICKING: return "Panicking";
        case OPP_WEAK_RANDOM: return "Weak Random";
        case OPP_SEMI_RANDOM: return "Semi Random";
        case OPP_STICKY_PRAYER: return "Sticky Prayer";
        case OPP_RANDOM_EATER: return "Random Eater";
        case OPP_PRAYER_ROOKIE: return "Prayer Rookie";
        case OPP_IMPROVED: return "Improved";
        case OPP_MIXED_EASY: return "Mixed Easy";
        case OPP_MIXED_MEDIUM: return "Mixed Medium";
        case OPP_ONETICK: return "Onetick";
        case OPP_UNPREDICTABLE_IMPROVED: return "Unpredictable Improved";
        case OPP_UNPREDICTABLE_ONETICK: return "Unpredictable Onetick";
        case OPP_MIXED_HARD: return "Mixed Hard";
        case OPP_MIXED_HARD_BALANCED: return "Mixed Hard Balanced";
        case OPP_PFSP: return "PFSP";
        case OPP_NOVICE_NH: return "Novice NH";
        case OPP_APPRENTICE_NH: return "Apprentice NH";
        case OPP_COMPETENT_NH: return "Competent NH";
        case OPP_INTERMEDIATE_NH: return "Intermediate NH";
        case OPP_ADVANCED_NH: return "Advanced NH";
        case OPP_PROFICIENT_NH: return "Proficient NH";
        case OPP_EXPERT_NH: return "Expert NH";
        case OPP_MASTER_NH: return "Master NH";
        case OPP_SAVANT_NH: return "Savant NH";
        case OPP_NIGHTMARE_NH: return "Nightmare NH";
        case OPP_VENG_FIGHTER: return "Veng Fighter";
        case OPP_BLOOD_HEALER: return "Blood Healer";
        case OPP_GMAUL_COMBO: return "Gmaul Combo";
        case OPP_RANGE_KITER: return "Range Kiter";
        case OPP_ADAPTIVE_NH: return "Adaptive NH";
        case OPP_SELFPLAY: return "Opponent Agent";
        case OPP_STRICT_KITER: return "Strict Kiter";
        default: break;
    }
    return "Opponent";
}

static inline const char* osrs_pvp_opponent_state_display_name(
    const OpponentState* opponent
) {
    if (!opponent) return "Opponent";
    OpponentType type = opponent->active_sub_policy
        ? opponent->active_sub_policy
        : opponent->type;
    return osrs_pvp_opponent_type_name(type);
}

static inline int opp_style_to_loadout(int style) {
    switch (style) {
        case OPP_STYLE_MAGE:   return LOADOUT_MAGE;
        case OPP_STYLE_RANGED: return LOADOUT_RANGE;
        case OPP_STYLE_MELEE:  return LOADOUT_MELEE;
        case OPP_STYLE_SPEC:   return LOADOUT_SPEC_MELEE;
        default: return LOADOUT_KEEP;
    }
}

static inline void opp_apply_gear_switch(int* actions, int style) {
    actions[HEAD_LOADOUT] = opp_style_to_loadout(style);
}

static inline void opp_apply_fake_switch(int* actions, int style) {
    actions[HEAD_LOADOUT] = opp_style_to_loadout(style);
}

static inline void opp_apply_tank_gear(int* actions) {
    actions[HEAD_LOADOUT] = LOADOUT_TANK;
}

typedef struct {
    int can_food;
    int can_brew;
    int can_karambwan;
    int can_restore;
    int can_combat_pot;
    int can_ranged_pot;
} OppConsumables;

static inline void opp_tick_cooldowns(OpponentState* opp) {
    if (opp->food_cooldown > 0) opp->food_cooldown--;
    if (opp->potion_cooldown > 0) opp->potion_cooldown--;
    if (opp->karambwan_cooldown > 0) opp->karambwan_cooldown--;
}

static inline OppConsumables opp_get_consumables(OpponentState* opp, Player* self) {
    float hp_pct = (float)self->current_hitpoints / (float)self->base_hitpoints;
    OppConsumables c;
    c.can_food = (opp->food_cooldown <= 0 && self->food_count > 0 && hp_pct < 1.0f);
    c.can_brew = (opp->potion_cooldown <= 0 && self->brew_doses > 0);
    c.can_karambwan = (opp->karambwan_cooldown <= 0 && self->karambwan_count > 0 && hp_pct < 1.0f);
    c.can_restore = (opp->potion_cooldown <= 0 && self->restore_doses > 0);
    c.can_combat_pot = (opp->potion_cooldown <= 0 && self->combat_potion_doses > 0);
    c.can_ranged_pot = (opp->potion_cooldown <= 0 && self->ranged_potion_doses > 0);
    return c;
}

static inline AttackStyle opp_get_gear_style(Player* p) {
    int s = get_item_attack_style(p->equipped[GEAR_SLOT_WEAPON]);
    if (s == 3) return ATTACK_STYLE_MAGIC;
    if (s == 2) return ATTACK_STYLE_RANGED;
    if (s == 1) return ATTACK_STYLE_MELEE;
    return ATTACK_STYLE_MAGIC;
}

static inline int opp_get_defensive_prayer(Player* target) {
    AttackStyle target_style = opp_get_gear_style(target);
    if (target_style == ATTACK_STYLE_MAGIC)  return OVERHEAD_MAGE;
    if (target_style == ATTACK_STYLE_RANGED) return OVERHEAD_RANGED;
    if (target_style == ATTACK_STYLE_MELEE)  return OVERHEAD_MELEE;
    return OVERHEAD_MAGE;
}

static inline int opp_has_prayer_active(Player* self, int prayer_action) {
    if (prayer_action == OVERHEAD_MELEE)  return self->prayer == PRAYER_PROTECT_MELEE;
    if (prayer_action == OVERHEAD_RANGED) return self->prayer == PRAYER_PROTECT_RANGED;
    if (prayer_action == OVERHEAD_MAGE)   return self->prayer == PRAYER_PROTECT_MAGIC;
    return 0;
}

static inline int opp_attack_ready(Player* self) {
    return self->attack_timer <= 0;
}

static inline int opp_target_frozen_after_pvp_timer_update(const Player* target) {
    return target->frozen_ticks > 1;
}

static inline int opp_melee_is_credible(Player* self, Player* target) {
    if (is_in_melee_range(self, target)) return 1;
    if (self->frozen_ticks > 0) return 0;
    int dist = chebyshev_distance(self->x, self->y, target->x, target->y);
    return dist <= 5;
}

/**
 * Get off-prayer attack styles (styles target isn't protecting).
 * Returns bitmask: bit 0 = mage, bit 1 = ranged, bit 2 = melee
 */
static inline int opp_get_off_prayer_mask(Player* self, Player* target) {
    int mask = 0;
    if (target->prayer != PRAYER_PROTECT_MAGIC)   mask |= (1 << OPP_STYLE_MAGE);
    if (target->prayer != PRAYER_PROTECT_RANGED)  mask |= (1 << OPP_STYLE_RANGED);
    if (target->prayer != PRAYER_PROTECT_MELEE && opp_melee_is_credible(self, target))
        mask |= (1 << OPP_STYLE_MELEE);
    if (mask == 0) mask = (1 << OPP_STYLE_MAGE);  /* Fallback to mage */
    return mask;
}

static inline int opp_pick_from_mask(OsrsEnv* env, int mask) {
    /* Count set bits and pick random */
    int choices[3];
    int count = 0;
    for (int i = 0; i < 3; i++) {
        if (mask & (1 << i)) choices[count++] = i;
    }
    return choices[rand_int(env, count)];
}

static inline int opp_pick_random_overhead(OsrsEnv* env) {
    int prayers[] = {OVERHEAD_MELEE, OVERHEAD_RANGED, OVERHEAD_MAGE};
    return prayers[rand_int(env, 3)];
}

typedef struct {
    int style;
    int can_hit_now;
} OppStyleChoice;

static inline uint8_t opp_resolved_style_weapon(Player* self, int style) {
    uint8_t resolved[NUM_DYNAMIC_GEAR_SLOTS];
    resolve_loadout(self, opp_style_to_loadout(style), resolved);
    return resolved[0];
}

static inline int opp_style_can_hit_now(OsrsEnv* env, Player* self, Player* target, int style) {
    int dist = chebyshev_distance(self->x, self->y, target->x, target->y);
    if (dist <= 0) return 0;
    const CollisionMap* cmap = (const CollisionMap*)env->collision_map;

    if (style == OPP_STYLE_MELEE) {
        return is_in_melee_range(self, target);
    }

    uint8_t weapon = opp_resolved_style_weapon(self, style);
    int weapon_style = get_item_attack_style(weapon);

    if (style == OPP_STYLE_RANGED) {
        if (weapon_style != 2) return 0;
    } else if (style == OPP_STYLE_MAGE) {
        if (weapon_style != 3) return 0;
        if (!can_cast_ice_spell(self) && !can_cast_blood_spell(self)) return 0;
    } else {
        return 0;
    }

    int range = ITEM_DATABASE[weapon].attack_range;
    if (range <= 0) return 0;
    OsrsAttackReachQuery reach = {
        .source = osrs_footprint(self->x, self->y, 1),
        .target = osrs_footprint(target->x, target->y, 1),
        .delivery = OSRS_ATTACK_DELIVERY_PROJECTILE,
        .range = range,
        .occlusion = osrs_projectile_occlusion_collision_map(cmap, 0),
    };
    return osrs_attack_can_reach(&reach);
}

static inline int opp_loadout_can_hit_now(
    OsrsEnv* env, Player* self, Player* target, int loadout, int style
) {
    int dist = chebyshev_distance(self->x, self->y, target->x, target->y);
    if (dist <= 0) return 0;
    const CollisionMap* cmap = (const CollisionMap*)env->collision_map;
    if (style == OPP_STYLE_MELEE || style == OPP_STYLE_SPEC) {
        return is_in_melee_range(self, target);
    }

    uint8_t resolved[NUM_DYNAMIC_GEAR_SLOTS];
    resolve_loadout(self, loadout, resolved);
    uint8_t weapon = resolved[0];
    int weapon_style = get_item_attack_style(weapon);

    if (style == OPP_STYLE_RANGED && weapon_style != 2) return 0;
    if (style == OPP_STYLE_MAGE && weapon_style != 3) return 0;

    int range = ITEM_DATABASE[weapon].attack_range;
    if (range <= 0) return 0;
    OsrsAttackReachQuery reach = {
        .source = osrs_footprint(self->x, self->y, 1),
        .target = osrs_footprint(target->x, target->y, 1),
        .delivery = OSRS_ATTACK_DELIVERY_PROJECTILE,
        .range = range,
        .occlusion = osrs_projectile_occlusion_collision_map(cmap, 0),
    };
    return osrs_attack_can_reach(&reach);
}

static inline int opp_hit_now_style_mask(OsrsEnv* env, Player* self, Player* target, int allowed_mask) {
    int mask = 0;
    allowed_mask &= OPP_STYLE_MASK_ALL;
    for (int style = 0; style < 3; style++) {
        if (!(allowed_mask & (1 << style))) continue;
        if (opp_style_can_hit_now(env, self, target, style)) mask |= (1 << style);
    }
    return mask;
}

static inline int opp_pick_biased_from_mask(
    OsrsEnv* env, OpponentState* opp, int mask
) {
    float weights[3] = {0};
    float total = 0;
    for (int i = 0; i < 3; i++) {
        if (mask & (1 << i)) {
            weights[i] = opp->style_bias[i];
            total += weights[i];
        }
    }
    if (total <= 0) return opp_pick_from_mask(env, mask);

    float r = rand_float(env) * total;
    float cum = 0;
    for (int i = 0; i < 3; i++) {
        if (!(mask & (1 << i))) continue;
        cum += weights[i];
        if (r < cum) return i;
    }
    return opp_pick_from_mask(env, mask);
}

static inline OppStyleChoice opp_resolve_attack_style(
    OsrsEnv* env,
    OpponentState* opp,
    Player* self,
    Player* target,
    int allowed_mask,
    int preference_mask
) {
    allowed_mask &= OPP_STYLE_MASK_ALL;
    if (allowed_mask == 0) allowed_mask = OPP_STYLE_MASK_ALL;

    int hit_mask = opp_hit_now_style_mask(env, self, target, allowed_mask);
    preference_mask &= allowed_mask;

    int choice_mask = preference_mask & hit_mask;
    if (choice_mask == 0 && hit_mask != 0) choice_mask = hit_mask;
    if (choice_mask == 0 && preference_mask != 0) choice_mask = preference_mask;
    if (choice_mask == 0) choice_mask = allowed_mask;

    int style = opp_pick_biased_from_mask(env, opp, choice_mask);
    OppStyleChoice choice = {
        .style = style,
        .can_hit_now = (hit_mask & (1 << style)) != 0,
    };
    return choice;
}

static inline int opp_random_style_preference(OsrsEnv* env, int allowed_mask) {
    allowed_mask &= OPP_STYLE_MASK_ALL;
    if (allowed_mask == 0) allowed_mask = OPP_STYLE_MASK_ALL;
    return 1 << opp_pick_from_mask(env, allowed_mask);
}

static inline int opp_choose_attack_style(
    OsrsEnv* env,
    OpponentState* opp,
    Player* self,
    Player* target,
    int allowed_mask
) {
    int preference_mask = rand_float(env) < opp->off_prayer_rate
        ? opp_get_off_prayer_mask(self, target)
        : opp_random_style_preference(env, allowed_mask);
    return opp_resolve_attack_style(
        env, opp, self, target, allowed_mask, preference_mask).style;
}

static inline int opp_choose_style_from_preference(
    OsrsEnv* env,
    OpponentState* opp,
    Player* self,
    Player* target,
    int allowed_mask,
    int style
) {
    return opp_resolve_attack_style(
        env, opp, self, target, allowed_mask, 1 << style).style;
}

static inline int opp_is_drained(Player* self) {
    // Any combat stat below base = drained (brew drain, SWH, etc.)
    return self->current_strength < self->base_strength ||
           self->current_attack < self->base_attack ||
           self->current_defence < self->base_defence ||
           self->current_ranged < self->base_ranged ||
           self->current_magic < self->base_magic;
}

static inline int opp_should_fc3(Player* self, Player* target) {
    int dist = chebyshev_distance(self->x, self->y, target->x, target->y);
    return target->freeze_immunity_ticks > 1 &&
           self->frozen_ticks == 0 &&
           self->attack_timer <= 2 &&
           dist > 3;
}

/* Anti-kite: update flee tracking based on distance trend */
static inline void opp_update_flee_tracking(OpponentState* opp, Player* self, Player* target) {
    int dist = chebyshev_distance(self->x, self->y, target->x, target->y);
    if (dist > opp->prev_dist_to_target && dist > 1) {
        opp->target_fleeing_ticks++;
    } else {
        opp->target_fleeing_ticks = 0;
    }
    opp->prev_dist_to_target = dist;
}

typedef struct { float base; float variance; } RandRange;

typedef struct {
    RandRange prayer_accuracy;
    RandRange off_prayer_rate;
    RandRange offensive_prayer_rate;
    RandRange action_delay_chance;
    RandRange mistake_rate;
    RandRange offensive_prayer_miss;  /* chance to attack without loadout switch (skips auto-prayer) */
} OpponentRandRanges;

#define RR(b, v) {(b), (v)}

/*                                    pray_acc      off_pray      off_pray_r    act_delay      mistake        off_pray_miss */
static const OpponentRandRanges OPP_RAND_RANGES[OPP_STRICT_KITER + 1] = {
    [OPP_NONE]                  = { RR(0,0),      RR(0,0),      RR(0,0),      RR(0,0),       RR(0,0),       RR(0,0) },
    [OPP_TRUE_RANDOM]           = { RR(0.33,0),   RR(0.33,0),   RR(0,0),      RR(0,0),       RR(0,0),       RR(0,0) },
    [OPP_PANICKING]             = { RR(0.33,0.1), RR(0.33,0),   RR(0,0),      RR(0.10,0.05), RR(0,0),       RR(0,0) },
    [OPP_WEAK_RANDOM]           = { RR(0.40,0.1), RR(0.33,0.1), RR(0,0),      RR(0.10,0.05), RR(0.05,0.03), RR(0,0) },
    [OPP_SEMI_RANDOM]           = { RR(0.50,0.1), RR(0.40,0.1), RR(0.05,0.03),RR(0.08,0.04), RR(0.05,0.03), RR(0,0) },
    [OPP_STICKY_PRAYER]         = { RR(0.33,0),   RR(0.33,0),   RR(0,0),      RR(0.10,0.05), RR(0,0),       RR(0,0) },
    [OPP_RANDOM_EATER]          = { RR(0.40,0.1), RR(0.33,0.1), RR(0,0),      RR(0.08,0.04), RR(0.05,0.03), RR(0,0) },
    [OPP_PRAYER_ROOKIE]         = { RR(0.30,0.1), RR(0.20,0.1), RR(0,0),      RR(0.12,0.05), RR(0.08,0.04), RR(0,0) },
    [OPP_IMPROVED]              = { RR(0.95,0.05),RR(0.95,0.05),RR(0.80,0.10),RR(0.05,0.03), RR(0.03,0.02), RR(0.05,0.03) },
    [OPP_MIXED_EASY]            = { RR(0,0),      RR(0,0),      RR(0,0),      RR(0,0),       RR(0,0),       RR(0,0) },
    [OPP_MIXED_MEDIUM]          = { RR(0,0),      RR(0,0),      RR(0,0),      RR(0,0),       RR(0,0),       RR(0,0) },
    [OPP_ONETICK]               = { RR(0.97,0.03),RR(0.97,0.03),RR(0.90,0.05),RR(0.03,0.02), RR(0.02,0.01), RR(0.03,0.02) },
    [OPP_UNPREDICTABLE_IMPROVED]= { RR(0.92,0.05),RR(0.90,0.05),RR(0.75,0.10),RR(0.08,0.04), RR(0.05,0.03), RR(0.08,0.04) },
    [OPP_UNPREDICTABLE_ONETICK] = { RR(0.95,0.03),RR(0.95,0.03),RR(0.85,0.08),RR(0.05,0.03), RR(0.03,0.02), RR(0.05,0.03) },
    [OPP_MIXED_HARD]            = { RR(0,0),      RR(0,0),      RR(0,0),      RR(0,0),       RR(0,0),       RR(0,0) },
    [OPP_MIXED_HARD_BALANCED]   = { RR(0,0),      RR(0,0),      RR(0,0),      RR(0,0),       RR(0,0),       RR(0,0) },
    [OPP_PFSP]                  = { RR(0,0),      RR(0,0),      RR(0,0),      RR(0,0),       RR(0,0),       RR(0,0) },
    [OPP_NOVICE_NH]             = { RR(0.60,0.10),RR(0.10,0.05),RR(0.10,0.05),RR(0.15,0.05), RR(0.10,0.05), RR(0.30,0.10) },
    [OPP_APPRENTICE_NH]         = { RR(0.60,0.10),RR(0.20,0.08),RR(0.20,0.08),RR(0.12,0.05), RR(0.08,0.04), RR(0.30,0.10) },
    [OPP_COMPETENT_NH]          = { RR(0.75,0.08),RR(0.25,0.08),RR(0.25,0.08),RR(0.10,0.04), RR(0.06,0.03), RR(0.20,0.08) },
    [OPP_INTERMEDIATE_NH]       = { RR(0.85,0.05),RR(0.70,0.08),RR(0.50,0.10),RR(0.08,0.04), RR(0.05,0.03), RR(0.20,0.08) },
    [OPP_ADVANCED_NH]           = { RR(0.95,0.05),RR(0.90,0.05),RR(0.75,0.08),RR(0.05,0.03), RR(0.03,0.02), RR(0.10,0.05) },
    [OPP_PROFICIENT_NH]         = { RR(0.95,0.03),RR(0.92,0.04),RR(0.80,0.08),RR(0.04,0.02), RR(0.03,0.02), RR(0.10,0.05) },
    [OPP_EXPERT_NH]             = { RR(0.97,0.03),RR(0.95,0.03),RR(0.85,0.05),RR(0.03,0.02), RR(0.02,0.01), RR(0.10,0.05) },
    [OPP_MASTER_NH]             = { RR(0.98,0.02),RR(0.97,0.03),RR(0.90,0.05),RR(0.02,0.01), RR(0.01,0.01), RR(0.01,0.01) },
    [OPP_SAVANT_NH]             = { RR(0.98,0.02),RR(0.97,0.03),RR(0.90,0.05),RR(0.02,0.01), RR(0.01,0.01), RR(0.01,0.01) },
    [OPP_NIGHTMARE_NH]          = { RR(0.99,0.01),RR(0.98,0.02),RR(0.95,0.03),RR(0.01,0.01), RR(0.005,0.005),RR(0.01,0.01) },
    [OPP_VENG_FIGHTER]          = { RR(0.92,0.05),RR(0.90,0.05),RR(0.85,0.10),RR(0.03,0.02), RR(0.02,0.01), RR(0.05,0.03) },
    [OPP_BLOOD_HEALER]          = { RR(0.90,0.05),RR(0.88,0.05),RR(0.80,0.10),RR(0.05,0.03), RR(0.04,0.02), RR(0.05,0.03) },
    [OPP_GMAUL_COMBO]           = { RR(0.96,0.03),RR(0.95,0.03),RR(0.90,0.05),RR(0.03,0.02), RR(0.02,0.01), RR(0.02,0.01) },
    [OPP_RANGE_KITER]           = { RR(0.93,0.04),RR(0.93,0.04),RR(0.85,0.08),RR(0.04,0.02), RR(0.03,0.02), RR(0.04,0.02) },
    [OPP_ADAPTIVE_NH]           = { RR(1.00,0),   RR(1.00,0),   RR(0.90,0),   RR(0,0),       RR(0,0),       RR(0,0) },
    [OPP_SELFPLAY]              = { RR(0,0),      RR(0,0),      RR(0,0),      RR(0,0),       RR(0,0),       RR(0,0) },
    [OPP_STRICT_KITER]          = { RR(1.00,0),   RR(1.00,0),   RR(0.95,0),   RR(0,0),       RR(0,0),       RR(0,0) },
};

#undef RR

static inline float rand_range(OsrsEnv* env, RandRange r) {
    float v = r.base + (rand_float(env) * 2.0f - 1.0f) * r.variance;
    return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
}

/* Tick-level action delay: skip prayer/attack/movement this tick (keep eating) */
static inline int opp_should_skip_offensive(OsrsEnv* env, OpponentState* opp) {
    return rand_float(env) < opp->action_delay_chance;
}

/**
 * Pick an off-prayer style when it can hit, otherwise any style that can hit.
 */
static inline int opp_pick_off_prayer_style_biased(OsrsEnv* env, OpponentState* opp,
                                                    Player* self, Player* target) {
    int off_mask = opp_get_off_prayer_mask(self, target);
    return opp_resolve_attack_style(
        env, opp, self, target, OPP_STYLE_MASK_ALL, off_mask).style;
}

/* Prayer mistake: small chance to pick random prayer instead of optimal */
static inline int opp_apply_prayer_mistake(OsrsEnv* env, OpponentState* opp, int correct_prayer) {
    if (rand_float(env) < opp->mistake_rate) {
        return opp_pick_random_overhead(env);
    }
    return correct_prayer;
}

/* unpredictable_improved prayer delays: 70% instant, 20% 1-tick, 8% 2-tick, 2% 3-tick */
static const float UNPREDICTABLE_IMP_PRAYER_CUM[] = {0.70f, 0.90f, 0.98f, 1.00f};
#define UNPREDICTABLE_IMP_PRAYER_CUM_LEN 4

/* unpredictable_improved action delays: 85% instant, 12% 1-tick, 3% 2-tick */
static const float UNPREDICTABLE_IMP_ACTION_CUM[] = {0.85f, 0.97f, 1.00f};
#define UNPREDICTABLE_IMP_ACTION_CUM_LEN 3

/* unpredictable_onetick prayer delays: 80% instant, 15% 1-tick, 4% 2-tick, 1% 3-tick */
static const float UNPREDICTABLE_OT_PRAYER_CUM[] = {0.80f, 0.95f, 0.99f, 1.00f};
#define UNPREDICTABLE_OT_PRAYER_CUM_LEN 4

/* unpredictable_onetick action delays: 90% instant, 8% 1-tick, 2% 2-tick */
static const float UNPREDICTABLE_OT_ACTION_CUM[] = {0.90f, 0.98f, 1.00f};
#define UNPREDICTABLE_OT_ACTION_CUM_LEN 3

/* mistake probabilities */
#define UNPREDICTABLE_IMP_WRONG_PRAYER      0.05f
#define UNPREDICTABLE_IMP_SUBOPTIMAL_ATTACK 0.03f
#define UNPREDICTABLE_OT_FAKE_FAIL          0.12f
#define UNPREDICTABLE_OT_WRONG_PREDICT      0.08f

/* Weighted delay sampling from cumulative weight array */
static inline int opp_sample_delay(OsrsEnv* env, const float* cum_weights, int num_weights) {
    float r = rand_float(env);
    for (int i = 0; i < num_weights; i++) {
        if (r < cum_weights[i]) return i;
    }
    return num_weights - 1;
}

/* Defensive prayer based on visible gear (uses actual weapon damage type). */
static inline int opp_get_defensive_prayer_with_spec(Player* target) {
    if (target->visible_gear == GEAR_MELEE)  return OVERHEAD_MELEE;
    if (target->visible_gear == GEAR_RANGED) return OVERHEAD_RANGED;
    if (target->visible_gear == GEAR_MAGE)   return OVERHEAD_MAGE;
    return opp_get_defensive_prayer(target);
}

typedef enum {
    OPP_DEF_PRAYER_TARGET_GEAR,
    OPP_DEF_PRAYER_TARGET_GEAR_WITH_SPEC,
} OppDefensivePrayerMode;

static inline int opp_pick_defensive_prayer(
    OsrsEnv* env,
    OpponentState* opp,
    Player* target,
    OppDefensivePrayerMode mode
) {
    int prayer = (mode == OPP_DEF_PRAYER_TARGET_GEAR_WITH_SPEC)
        ? opp_get_defensive_prayer_with_spec(target)
        : opp_get_defensive_prayer(target);

    if (rand_float(env) >= opp->prayer_accuracy) {
        prayer = opp_pick_random_overhead(env);
    }

    return opp_apply_prayer_mistake(env, opp, prayer);
}

/* Get opponent's current prayer style as OPP_STYLE_* (-1 if none) */
static inline int opp_get_opponent_prayer_style(Player* target) {
    if (target->prayer == PRAYER_PROTECT_MAGIC)  return OPP_STYLE_MAGE;
    if (target->prayer == PRAYER_PROTECT_RANGED) return OPP_STYLE_RANGED;
    if (target->prayer == PRAYER_PROTECT_MELEE)  return OPP_STYLE_MELEE;
    return -1;
}

/* Get target's visible gear style as GearSet value. */
static inline int opp_get_target_gear_style(Player* target) {
    return (int)target->visible_gear;
}

/* Choose ice vs blood barrage based on freeze state and HP */
static inline int opp_get_mage_attack(Player* self, Player* target) {
    int can_freeze = target->freeze_immunity_ticks <= 1 && target->frozen_ticks == 0;
    if (can_freeze) return ATTACK_ICE;
    float hp_pct = (float)self->current_hitpoints / (float)self->base_hitpoints;
    return (hp_pct > 0.98f) ? ATTACK_ICE : ATTACK_BLOOD;
}

static inline void opp_resolve_normal_attack(
    OsrsEnv* env,
    OpponentState* opp,
    Player* self,
    Player* target,
    int allowed_mask,
    int* actual_style,
    int* actual_attack
) {
    if (*actual_attack == 3) return;
    *actual_style = opp_choose_style_from_preference(
        env, opp, self, target, allowed_mask, *actual_style);
    *actual_attack = (*actual_style == OPP_STYLE_MAGE)
        ? (opp_get_mage_attack(self, target) == ATTACK_ICE ? 0 : 1)
        : 2;
}

/* (opp_apply_tank_gear is defined above as inline loadout assignment) */

/* Boost/restore potion logic (before attack, used by onetick+ opponents) */
static void opp_apply_boost_potion(OsrsEnv* env, OpponentState* opp, int* actions,
                                    Player* self, int attack_style, int potion_used) {
    (void)env;
    if (potion_used) return;
    if (opp->potion_cooldown > 0) return;
    float hp_pct = (float)self->current_hitpoints / (float)self->base_hitpoints;

    /* If drained (brew drain / SWH) and HP safe, restore before boosting.
     * 0.90 threshold ensures we finish brewing to full HP before restoring
     * (one restore dose undoes ~3 brew doses of stat drain). */
    if (opp_is_drained(self) && hp_pct > 0.90f && self->restore_doses > 0) {
        actions[HEAD_POTION] = POTION_RESTORE;
        opp->potion_cooldown = 3;
        return;
    }

    if (hp_pct <= 0.90f) return;  /* eat/brew to 90%+ before boosting */

    if (attack_style == OPP_STYLE_MELEE || attack_style == OPP_STYLE_SPEC) {
        /* Boost when at or below base (covers brew-drained stats too) */
        if (self->current_strength <= self->base_strength && self->combat_potion_doses > 0) {
            actions[HEAD_POTION] = POTION_COMBAT;
            opp->potion_cooldown = 3;
        }
    } else if (attack_style == OPP_STYLE_RANGED) {
        if (self->current_ranged <= self->base_ranged && self->ranged_potion_doses > 0) {
            actions[HEAD_POTION] = POTION_RANGED;
            opp->potion_cooldown = 3;
        }
    }
}

/* Check if eating was queued in actions (food/karambwan cancel attacks) */
static inline int opp_check_eating_queued(int* actions) {
    return actions[HEAD_FOOD] != FOOD_NONE || actions[HEAD_KARAMBWAN] != KARAM_NONE;
}

typedef enum {
    OPP_SURVIVAL_STANDARD,
    OPP_SURVIVAL_STANDARD_NO_DRAIN_RESTORE,
    OPP_SURVIVAL_LUNAR,
    OPP_SURVIVAL_BLOOD_HEALER,
} OppSurvivalPolicy;

typedef struct {
    int eating;
    int potion_used;
} OppSurvivalResult;

static inline void opp_queue_food(OpponentState* opp, int* actions) {
    actions[HEAD_FOOD] = FOOD_EAT;
    opp->food_cooldown = 3;
}

static inline void opp_queue_brew(OpponentState* opp, int* actions) {
    actions[HEAD_POTION] = POTION_BREW;
    opp->potion_cooldown = 3;
}

static inline void opp_queue_restore(OpponentState* opp, int* actions) {
    actions[HEAD_POTION] = POTION_RESTORE;
    opp->potion_cooldown = 3;
}

static inline void opp_queue_karambwan(OpponentState* opp, int* actions) {
    actions[HEAD_KARAMBWAN] = KARAM_EAT;
    opp->karambwan_cooldown = 2;
}

static inline OppSurvivalResult opp_apply_survival_actions(
    OpponentState* opp,
    int* actions,
    Player* self,
    OppConsumables cons,
    OppSurvivalPolicy policy
) {
    float hp_pct = (float)self->current_hitpoints / (float)self->base_hitpoints;
    float prayer_pct = (float)self->current_prayer / (float)self->base_prayer;
    int drained = opp_is_drained(self);
    int restore_prayer = policy != OPP_SURVIVAL_LUNAR;
    int restore_drain = policy != OPP_SURVIVAL_STANDARD_NO_DRAIN_RESTORE;
    int potion_used = 0;

    if (policy == OPP_SURVIVAL_BLOOD_HEALER) {
        if (hp_pct < 0.25f && cons.can_food && cons.can_brew && cons.can_karambwan) {
            opp_queue_food(opp, actions);
            opp_queue_brew(opp, actions);
            opp_queue_karambwan(opp, actions);
            potion_used = 1;
        } else if (hp_pct < 0.35f && cons.can_food && cons.can_brew) {
            opp_queue_food(opp, actions);
            opp_queue_brew(opp, actions);
            potion_used = 1;
        } else if (hp_pct < 0.35f && cons.can_food && cons.can_karambwan) {
            opp_queue_food(opp, actions);
            opp_queue_karambwan(opp, actions);
        } else if (hp_pct < 0.35f && cons.can_brew) {
            opp_queue_brew(opp, actions);
            potion_used = 1;
        } else if (drained && hp_pct < 0.50f && cons.can_brew) {
            opp_queue_brew(opp, actions);
            potion_used = 1;
        } else if (prayer_pct < 0.30f && cons.can_restore) {
            opp_queue_restore(opp, actions);
        } else if (drained && cons.can_restore) {
            opp_queue_restore(opp, actions);
        }

        return (OppSurvivalResult){
            .eating = opp_check_eating_queued(actions),
            .potion_used = potion_used,
        };
    }

    if (hp_pct < opp->eat_triple_threshold &&
            cons.can_food && cons.can_brew && cons.can_karambwan) {
        opp_queue_food(opp, actions);
        opp_queue_brew(opp, actions);
        opp_queue_karambwan(opp, actions);
        potion_used = 1;
    } else if (hp_pct < opp->eat_double_threshold && cons.can_food && cons.can_brew) {
        opp_queue_food(opp, actions);
        opp_queue_brew(opp, actions);
        potion_used = 1;
    } else if (hp_pct < opp->eat_double_threshold && cons.can_food && cons.can_karambwan) {
        opp_queue_food(opp, actions);
        opp_queue_karambwan(opp, actions);
    } else if (hp_pct < opp->eat_brew_threshold && cons.can_brew) {
        opp_queue_brew(opp, actions);
        potion_used = 1;
    } else if (hp_pct < 0.60f && cons.can_food) {
        opp_queue_food(opp, actions);
    } else if (hp_pct < 0.60f && cons.can_karambwan) {
        opp_queue_karambwan(opp, actions);
    } else if (drained && hp_pct < 0.90f && cons.can_brew) {
        opp_queue_brew(opp, actions);
        potion_used = 1;
    } else if (restore_prayer && prayer_pct < 0.30f && cons.can_restore) {
        opp_queue_restore(opp, actions);
    } else if (restore_drain && drained && cons.can_restore) {
        opp_queue_restore(opp, actions);
    }

    return (OppSurvivalResult){
        .eating = opp_check_eating_queued(actions),
        .potion_used = potion_used,
    };
}

static inline int opp_apply_survival_policy(
    OpponentState* opp,
    int* actions,
    Player* self,
    OppConsumables cons,
    OppSurvivalPolicy policy
) {
    return opp_apply_survival_actions(opp, actions, self, cons, policy).eating;
}

static inline void opp_emit_combat_attack(int* actions, int attack) {
    if (attack == 0) {
        actions[HEAD_COMBAT] = ATTACK_ICE;
    } else if (attack == 1) {
        actions[HEAD_COMBAT] = ATTACK_BLOOD;
    } else {
        actions[HEAD_COMBAT] = ATTACK_ATK;
    }
}

static inline void opp_emit_attack_with_style(
    OsrsEnv* env,
    OpponentState* opp,
    int* actions,
    int style,
    int attack
) {
    if (attack != 3 && rand_float(env) < opp->offensive_prayer_miss) {
        actions[HEAD_LOADOUT] = LOADOUT_KEEP;
    } else {
        opp_apply_gear_switch(actions, style);
    }

    opp_emit_combat_attack(actions, attack);
}

/* Improved-style consumable logic. Returns 1 if potion was used (for restore/boost tracking) */
static int opp_apply_consumables(OsrsEnv* env, OpponentState* opp, int* actions,
                                  Player* self) {
    OppConsumables cons = opp_get_consumables(opp, self);
    (void)env;
    return opp_apply_survival_actions(
        opp, actions, self, cons, OPP_SURVIVAL_STANDARD).potion_used;
}

/* map an OverheadPrayer to the set/refresh action that activates it. */
static inline int opp_set_refresh_for_prayer(OverheadPrayer p) {
    switch (p) {
        case PRAYER_PROTECT_MAGIC:  return ENCOUNTER_OVERHEAD_SET_REFRESH_MAGIC;
        case PRAYER_PROTECT_RANGED: return ENCOUNTER_OVERHEAD_SET_REFRESH_RANGED;
        case PRAYER_PROTECT_MELEE:  return ENCOUNTER_OVERHEAD_SET_REFRESH_MELEE;
        case PRAYER_SMITE:          return ENCOUNTER_OVERHEAD_SET_REFRESH_SMITE;
        case PRAYER_REDEMPTION:     return ENCOUNTER_OVERHEAD_SET_REFRESH_REDEMPTION;
        default:                    return ENCOUNTER_OVERHEAD_NO_CHANGE;
    }
}

/* Convert opponent prayer intent into the set/off action required by the
   encounter overhead encoding. */
static inline void opp_emit_prayer(int* actions, Player* self, int target_overhead_action) {
    OverheadPrayer target_prayer;
    switch (target_overhead_action) {
        case OVERHEAD_NONE:       target_prayer = PRAYER_NONE;           break;
        case OVERHEAD_MAGE:       target_prayer = PRAYER_PROTECT_MAGIC;  break;
        case OVERHEAD_RANGED:     target_prayer = PRAYER_PROTECT_RANGED; break;
        case OVERHEAD_MELEE:      target_prayer = PRAYER_PROTECT_MELEE;  break;
        case OVERHEAD_SMITE:      target_prayer = PRAYER_SMITE;          break;
        case OVERHEAD_REDEMPTION: target_prayer = PRAYER_REDEMPTION;     break;
        default: return;  /* invalid: no-op */
    }
    if (self->prayer == target_prayer) return;
    actions[HEAD_OVERHEAD] = (target_prayer == PRAYER_NONE)
        ? ENCOUNTER_OVERHEAD_OFF
        : opp_set_refresh_for_prayer(target_prayer);
}

static inline void opp_apply_defensive_prayer(
    OsrsEnv* env,
    OpponentState* opp,
    int* actions,
    Player* self,
    Player* target,
    OppDefensivePrayerMode mode
) {
    int prayer = opp_pick_defensive_prayer(env, opp, target, mode);
    if (!opp_has_prayer_active(self, prayer)) {
        opp_emit_prayer(actions, self, prayer);
    }
}

/* Process pending prayer delay: decrement, apply if ready. Returns 1 if applied. */
static inline int opp_process_pending_prayer(OpponentState* opp, int* actions, Player* self) {
    if (opp->pending_prayer_value == 0) return 0;
    if (opp->pending_prayer_delay > 0) {
        opp->pending_prayer_delay--;
        if (opp->pending_prayer_delay > 0) return 0;
    }
    OverheadPrayer target_prayer = PRAYER_NONE;
    int action = ENCOUNTER_OVERHEAD_NO_CHANGE;
    switch (opp->pending_prayer_value) {
        case OVERHEAD_MAGE:       target_prayer = PRAYER_PROTECT_MAGIC;  action = ENCOUNTER_OVERHEAD_SET_REFRESH_MAGIC; break;
        case OVERHEAD_RANGED:     target_prayer = PRAYER_PROTECT_RANGED; action = ENCOUNTER_OVERHEAD_SET_REFRESH_RANGED; break;
        case OVERHEAD_MELEE:      target_prayer = PRAYER_PROTECT_MELEE;  action = ENCOUNTER_OVERHEAD_SET_REFRESH_MELEE; break;
        case OVERHEAD_SMITE:      target_prayer = PRAYER_SMITE;          action = ENCOUNTER_OVERHEAD_SET_REFRESH_SMITE; break;
        case OVERHEAD_REDEMPTION: target_prayer = PRAYER_REDEMPTION;     action = ENCOUNTER_OVERHEAD_SET_REFRESH_REDEMPTION; break;
        default: break;
    }
    if (self->prayer != target_prayer) actions[HEAD_OVERHEAD] = action;
    opp->pending_prayer_value = 0;
    return 1;
}

/* Handle prayer switch with delay for unpredictable policies.
 * Detects target gear changes, samples delay, stores in pending state.
 * include_spec: if 1, also detect spec weapon (onetick/unpredictable_onetick). */
static void opp_handle_delayed_prayer(OsrsEnv* env, OpponentState* opp, int* actions,
                                       Player* self, Player* target,
                                       const float* cum_weights, int cum_len,
                                       float wrong_prayer_prob, int include_spec) {
    /* Detect target gear style change */
    int target_style = opp_get_target_gear_style(target);
    if (target_style != opp->last_target_gear_style) {
        opp->last_target_gear_style = target_style;

        /* Determine needed prayer */
        int needed_prayer = include_spec
            ? opp_get_defensive_prayer_with_spec(target)
            : opp_get_defensive_prayer(target);

        /* Check if we need to switch */
        int needs_switch = !opp_has_prayer_active(self, needed_prayer);

        if (needs_switch) {
            /* Small chance to pick wrong prayer */
            if (rand_float(env) < wrong_prayer_prob) {
                int wrong_options[2];
                int wcount = 0;
                int all_prayers[] = {OVERHEAD_MELEE, OVERHEAD_RANGED, OVERHEAD_MAGE};
                for (int i = 0; i < 3; i++) {
                    if (all_prayers[i] != needed_prayer)
                        wrong_options[wcount++] = all_prayers[i];
                }
                needed_prayer = wrong_options[rand_int(env, wcount)];
            }

            int delay = opp_sample_delay(env, cum_weights, cum_len);
            opp->pending_prayer_value = needed_prayer;
            opp->pending_prayer_delay = delay;
        }
    }

    /* Process pending prayer (may apply this tick if delay=0) */
    opp_process_pending_prayer(opp, actions, self);
}

/* --- TrueRandom: random value per action head --- */
static void opp_true_random(OsrsEnv* env, int* actions) {
    actions[HEAD_LOADOUT] = rand_int(env, LOADOUT_DIM);
    actions[HEAD_COMBAT] = rand_int(env, COMBAT_DIM);
    actions[HEAD_OVERHEAD] = rand_int(env, OVERHEAD_DIM);
    actions[HEAD_FOOD] = rand_int(env, FOOD_DIM);
    actions[HEAD_POTION] = rand_int(env, POTION_DIM);
    actions[HEAD_KARAMBWAN] = rand_int(env, KARAMBWAN_DIM);
    actions[HEAD_VENG] = rand_int(env, VENG_DIM);
    actions[HEAD_OFFENSIVE] = rand_int(env, OFFENSIVE_DIM);
    actions[HEAD_MOVE] = rand_int(env, MOVE_DIM);
}

/* --- Panicking: fixed prayer, fixed style, 30% attack chance, panic eat --- */
static void opp_panicking(OsrsEnv* env, OpponentState* opp, int* actions) {
    Player* self = &env->players[1];
    float hp_pct = (float)self->current_hitpoints / (float)self->base_hitpoints;

    opp_tick_cooldowns(opp);
    OppConsumables cons = opp_get_consumables(opp, self);

    /* Set prayer if not already active */
    if (!opp_has_prayer_active(self, opp->chosen_prayer)) {
        opp_emit_prayer(actions, self, opp->chosen_prayer);
    }

    /* Panic eat at 25% HP */
    int eating = 0;
    if (hp_pct < 0.25f) {
        if (cons.can_food) {
            opp_queue_food(opp, actions);
            eating = 1;
        }
        if (cons.can_brew) {
            opp_queue_brew(opp, actions);
        }
    }

    /* Tick-level action delay */
    if (opp_should_skip_offensive(env, opp)) return;

    /* 30% chance to attack */
    if (opp_attack_ready(self) && !eating && rand_float(env) < 0.30f) {
        opp_apply_gear_switch(actions, opp->chosen_style);

        if (opp->chosen_style == OPP_STYLE_MAGE) {
            int spell = (rand_int(env, 2) == 0) ? ATTACK_ICE : ATTACK_BLOOD;
            actions[HEAD_COMBAT] = spell;
        } else {
            actions[HEAD_COMBAT] = ATTACK_ATK;
        }
    }
}

/* --- WeakRandom: random style, unreliable eating (50% skip) --- */
static void opp_weak_random(OsrsEnv* env, OpponentState* opp, int* actions) {
    Player* self = &env->players[1];
    float hp_pct = (float)self->current_hitpoints / (float)self->base_hitpoints;

    opp_tick_cooldowns(opp);
    OppConsumables cons = opp_get_consumables(opp, self);

    /* Random prayer each tick (includes NONE) */
    int prayers[] = {OVERHEAD_NONE, OVERHEAD_MELEE, OVERHEAD_RANGED, OVERHEAD_MAGE};
    opp_emit_prayer(actions, self, prayers[rand_int(env, 4)]);

    /* Unreliable eating at 30% with 50% skip chance */
    int eating = 0;
    if (hp_pct < 0.30f && rand_float(env) > 0.50f) {
        if (cons.can_food) {
            opp_queue_food(opp, actions);
            eating = 1;
        } else if (cons.can_brew) {
            opp_queue_brew(opp, actions);
            eating = 1;
        }
    }

    /* Tick-level action delay */
    if (opp_should_skip_offensive(env, opp)) return;

    /* Random attack when ready */
    if (opp_attack_ready(self) && !eating) {
        int style = rand_int(env, 3);  /* 0=mage, 1=ranged, 2=melee */
        opp_apply_gear_switch(actions, style);
        if (style == OPP_STYLE_MAGE) {
            int spell = (rand_int(env, 2) == 0) ? ATTACK_ICE : ATTACK_BLOOD;
            actions[HEAD_COMBAT] = spell;
        } else {
            actions[HEAD_COMBAT] = ATTACK_ATK;
        }
    }
}

/* --- SemiRandom: reliable eating at 30%, random everything else --- */
static void opp_semi_random(OsrsEnv* env, OpponentState* opp, int* actions) {
    Player* self = &env->players[1];
    float hp_pct = (float)self->current_hitpoints / (float)self->base_hitpoints;

    opp_tick_cooldowns(opp);
    OppConsumables cons = opp_get_consumables(opp, self);

    /* Random prayer each tick (no NONE) */
    opp_emit_prayer(actions, self, opp_pick_random_overhead(env));

    /* Reliable eating at 30% */
    int eating = 0;
    if (hp_pct < 0.30f) {
        if (cons.can_food) {
            opp_queue_food(opp, actions);
            eating = 1;
        } else if (cons.can_brew) {
            opp_queue_brew(opp, actions);
            eating = 1;
        }
    }

    /* Tick-level action delay */
    if (opp_should_skip_offensive(env, opp)) return;

    /* Random attack when ready */
    if (opp_attack_ready(self) && !eating) {
        int style = rand_int(env, 3);
        opp_apply_gear_switch(actions, style);
        if (style == OPP_STYLE_MAGE) {
            int spell = (rand_int(env, 2) == 0) ? ATTACK_ICE : ATTACK_BLOOD;
            actions[HEAD_COMBAT] = spell;
        } else {
            actions[HEAD_COMBAT] = ATTACK_ATK;
        }
    }
}

/* --- StickyPrayer: sticky prayer (~12 tick avg), simple eating --- */
static void opp_sticky_prayer(OsrsEnv* env, OpponentState* opp, int* actions) {
    Player* self = &env->players[1];
    float hp_pct = (float)self->current_hitpoints / (float)self->base_hitpoints;

    opp_tick_cooldowns(opp);
    OppConsumables cons = opp_get_consumables(opp, self);

    /* Sticky prayer: 8% chance to switch per tick (~12 tick avg) */
    if (!opp->current_prayer_set || rand_float(env) < 0.08f) {
        opp->current_prayer = opp_pick_random_overhead(env);
        opp->current_prayer_set = 1;
    }
    if (!opp_has_prayer_active(self, opp->current_prayer)) {
        opp_emit_prayer(actions, self, opp->current_prayer);
    }

    /* Simple eating at 30% */
    int eating = 0;
    if (hp_pct < 0.30f) {
        if (cons.can_food) {
            opp_queue_food(opp, actions);
            eating = 1;
        } else if (cons.can_brew) {
            opp_queue_brew(opp, actions);
            eating = 1;
        }
    }

    /* Tick-level action delay */
    if (opp_should_skip_offensive(env, opp)) return;

    /* Random attack when ready */
    if (opp_attack_ready(self) && !eating) {
        int style = rand_int(env, 3);
        opp_apply_gear_switch(actions, style);
        if (style == OPP_STYLE_MAGE) {
            int spell = (rand_int(env, 2) == 0) ? ATTACK_ICE : ATTACK_BLOOD;
            actions[HEAD_COMBAT] = spell;
        } else {
            actions[HEAD_COMBAT] = ATTACK_ATK;
        }
    }
}

/* --- Beginner: sticky prayer, multi-threshold eating, random spec --- */
static void opp_random_eater(OsrsEnv* env, OpponentState* opp, int* actions) {
    Player* self = &env->players[1];
    Player* target = &env->players[0];
    float hp_pct = (float)self->current_hitpoints / (float)self->base_hitpoints;
    float prayer_pct = (float)self->current_prayer / (float)self->base_prayer;

    opp_tick_cooldowns(opp);
    OppConsumables cons = opp_get_consumables(opp, self);

    /* 1. Sticky random prayer */
    if (!opp->current_prayer_set || rand_float(env) < 0.08f) {
        opp->current_prayer = opp_pick_random_overhead(env);
        opp->current_prayer_set = 1;
    }
    if (!opp_has_prayer_active(self, opp->current_prayer)) {
        opp_emit_prayer(actions, self, opp->current_prayer);
    }

    /* 2. Multi-threshold eating */
    int potion_used = 0;
    if (hp_pct < 0.35f) {
        /* Emergency: eat everything */
        if (cons.can_food) {
            opp_queue_food(opp, actions);
        }
        if (cons.can_brew) {
            opp_queue_brew(opp, actions);
            potion_used = 1;
        }
        if (cons.can_karambwan) {
            opp_queue_karambwan(opp, actions);
        }
    } else if (hp_pct < 0.55f) {
        if (cons.can_food) {
            opp_queue_food(opp, actions);
        } else if (cons.can_brew) {
            opp_queue_brew(opp, actions);
            potion_used = 1;
        }
    } else if (hp_pct < opp->eat_brew_threshold && cons.can_brew) {
        opp_queue_brew(opp, actions);
        potion_used = 1;
    }

    /* 3. Restore if low prayer */
    if (!potion_used && prayer_pct < 0.30f && cons.can_restore) {
        opp_queue_restore(opp, actions);
    }

    int eating = opp_check_eating_queued(actions);

    /* Tick-level action delay */
    if (opp_should_skip_offensive(env, opp)) return;

    /* 4. Attack when ready with random style */
    if (opp_attack_ready(self) && !eating) {
        int style = rand_int(env, 3);

        /* 30% spec chance */
        if (self->special_energy >= get_melee_spec_cost(self->melee_spec_weapon) && rand_float(env) < 0.30f) {
            opp_apply_gear_switch(actions, OPP_STYLE_SPEC);
            actions[HEAD_COMBAT] = ATTACK_ATK;
        } else {
            opp_apply_gear_switch(actions, style);
            if (style == OPP_STYLE_MAGE) {
                int spell = (rand_int(env, 2) == 0) ? ATTACK_ICE : ATTACK_BLOOD;
                actions[HEAD_COMBAT] = spell;
            } else {
                actions[HEAD_COMBAT] = ATTACK_ATK;
            }
        }
    }

    (void)target;
}

/* --- BetterRandom: multi-threshold eating, random prayers, random spec --- */
static void opp_prayer_rookie(OsrsEnv* env, OpponentState* opp, int* actions) {
    Player* self = &env->players[1];
    Player* target = &env->players[0];
    float hp_pct = (float)self->current_hitpoints / (float)self->base_hitpoints;

    opp_tick_cooldowns(opp);
    OppConsumables cons = opp_get_consumables(opp, self);

    /* 1. Defensive prayer */
    int def_prayer = opp_pick_defensive_prayer(
        env, opp, target, OPP_DEF_PRAYER_TARGET_GEAR);
    opp_emit_prayer(actions, self, def_prayer);

    /* 2. Multi-threshold eating */
    if (hp_pct < 0.35f) {
        if (cons.can_food) {
            opp_queue_food(opp, actions);
        }
        if (cons.can_brew) {
            opp_queue_brew(opp, actions);
        }
        if (cons.can_karambwan) {
            opp_queue_karambwan(opp, actions);
        }
    } else if (hp_pct < 0.55f) {
        if (cons.can_food) {
            opp_queue_food(opp, actions);
        } else if (cons.can_brew) {
            opp_queue_brew(opp, actions);
        }
    } else if (hp_pct < opp->eat_brew_threshold && cons.can_brew) {
        opp_queue_brew(opp, actions);
    }

    int eating = opp_check_eating_queued(actions);

    /* Tick-level action delay */
    if (opp_should_skip_offensive(env, opp)) return;

    /* 3. Attack with random style, random spec chance */
    if (opp_attack_ready(self) && !eating) {
        int style = rand_int(env, 3);

        if (self->special_energy >= get_melee_spec_cost(self->melee_spec_weapon) && rand_float(env) < 0.30f) {
            opp_apply_gear_switch(actions, OPP_STYLE_SPEC);
            actions[HEAD_COMBAT] = ATTACK_ATK;
        } else {
            opp_apply_gear_switch(actions, style);
            if (style == OPP_STYLE_MAGE) {
                int spell = (rand_int(env, 2) == 0) ? ATTACK_ICE : ATTACK_BLOOD;
                actions[HEAD_COMBAT] = spell;
            } else {
                actions[HEAD_COMBAT] = ATTACK_ATK;
            }
        }
    }
}

/* --- Improved: full NH (correct prayer, off-prayer attacks, combo eating,
       spec timing, offensive prayer, movement) --- */
static void opp_improved(OsrsEnv* env, OpponentState* opp, int* actions) {
    Player* self = &env->players[1];
    Player* target = &env->players[0];
    float hp_pct = (float)self->current_hitpoints / (float)self->base_hitpoints;

    opp_tick_cooldowns(opp);
    OppConsumables cons = opp_get_consumables(opp, self);

    /* 1. Defensive prayer based on target's weapon */
    opp_apply_defensive_prayer(
        env, opp, actions, self, target, OPP_DEF_PRAYER_TARGET_GEAR);

    /* 2. Consumables: triple/double/single eat */
    int eating = opp_apply_survival_policy(
        opp, actions, self, cons, OPP_SURVIVAL_STANDARD);

    /* Tick-level action delay: skip offensive actions this tick */
    if (opp_should_skip_offensive(env, opp)) return;

    /* 3. Attack decision */
    if (opp_attack_ready(self) && !eating) {
        int attack_style = opp_choose_attack_style(
            env, opp, self, target, OPP_STYLE_MASK_ALL);

        /* Boost potions before attack */
        opp_apply_boost_potion(env, opp, actions, self, attack_style, 0);

        /* Check spec */
        int dist = chebyshev_distance(self->x, self->y, target->x, target->y);
        int can_spec_range = (self->frozen_ticks > 0) ? (dist <= 1) : (dist <= 3);
        int should_spec = (self->special_energy >= get_melee_spec_cost(self->melee_spec_weapon) &&
                          target->prayer != PRAYER_PROTECT_MELEE &&
                          can_spec_range);

        /* Anti-kite: cancel melee spec if target fleeing, force mage to freeze */
        if (should_spec && opp->target_fleeing_ticks >= 2 && dist > 1) {
            should_spec = 0;
            attack_style = OPP_STYLE_MAGE;
        }

        int actual_style;
        int actual_attack;  /* 0=ice, 1=blood, 2=atk, 3=spec */
        if (should_spec) {
            actual_style = OPP_STYLE_SPEC;
            actual_attack = 3;
        } else if (attack_style == OPP_STYLE_MAGE) {
            actual_style = OPP_STYLE_MAGE;
            actual_attack = (hp_pct < 0.30f) ? 1 : 0;  /* blood if low HP, else ice */
        } else {
            actual_style = attack_style;
            actual_attack = 2;  /* ATK */
        }

        opp_emit_attack_with_style(env, opp, actions, actual_style, actual_attack);
    } else if (!opp_attack_ready(self)) {
        /* Movement when not attacking */
        int dist = chebyshev_distance(self->x, self->y, target->x, target->y);
        if (opp_target_frozen_after_pvp_timer_update(target) &&
                self->frozen_ticks == 0 && dist > 0) {
            actions[HEAD_COMBAT] = MOVE_UNDER;
        } else if (opp->target_fleeing_ticks >= 2 && dist > 3 && self->frozen_ticks == 0) {
            actions[HEAD_COMBAT] = MOVE_FARCAST_3;
        } else if (opp_should_fc3(self, target) && target->prayer != PRAYER_PROTECT_MELEE) {
            actions[HEAD_COMBAT] = MOVE_FARCAST_3;
        }
    }
}

static void opp_novice_nh(OsrsEnv* env, OpponentState* opp, int* actions) {
    Player* self = &env->players[1];
    Player* target = &env->players[0];

    opp_tick_cooldowns(opp);
    OppConsumables cons = opp_get_consumables(opp, self);

    /* 1. Defensive prayer */
    opp_apply_defensive_prayer(
        env, opp, actions, self, target, OPP_DEF_PRAYER_TARGET_GEAR);

    /* 2. Multi-threshold eating (same as improved) */
    int eating = opp_apply_survival_policy(
        opp, actions, self, cons, OPP_SURVIVAL_STANDARD_NO_DRAIN_RESTORE);

    /* Tick-level action delay */
    if (opp_should_skip_offensive(env, opp)) return;

    /* 3. Attack: off-prayer based on off_prayer_rate. Random spec. Offensive prayer. */
    if (opp_attack_ready(self) && !eating) {
        int style = opp_choose_attack_style(
            env, opp, self, target, OPP_STYLE_MASK_ALL);

        /* Boost potions before attack */
        opp_apply_boost_potion(env, opp, actions, self, style, 0);

        if (self->special_energy >= get_melee_spec_cost(self->melee_spec_weapon) && rand_float(env) < 0.15f) {
            int dist = chebyshev_distance(self->x, self->y, target->x, target->y);
            if (opp->target_fleeing_ticks >= 2 && dist > 1) {
                /* Anti-kite: cancel spec, use mage */
                opp_apply_gear_switch(actions, OPP_STYLE_MAGE);
                actions[HEAD_COMBAT] = ATTACK_ICE;
            } else {
                opp_apply_gear_switch(actions, OPP_STYLE_SPEC);
                actions[HEAD_COMBAT] = ATTACK_ATK;
            }
        } else {
            /* Gear switch — offensive_prayer_miss: skip switch to omit auto-prayer */
            if (rand_float(env) < opp->offensive_prayer_miss) {
                actions[HEAD_LOADOUT] = LOADOUT_KEEP;
            } else {
                opp_apply_gear_switch(actions, style);
            }

            /* Offensive prayer */
            if (rand_float(env) < opp->offensive_prayer_rate) {

            }

            if (style == OPP_STYLE_MAGE) {
                int spell = (rand_int(env, 2) == 0) ? ATTACK_ICE : ATTACK_BLOOD;
                actions[HEAD_COMBAT] = spell;
            } else {
                actions[HEAD_COMBAT] = ATTACK_ATK;
            }
        }
    }
}

static void opp_apprentice_nh(OsrsEnv* env, OpponentState* opp, int* actions) {
    Player* self = &env->players[1];
    Player* target = &env->players[0];
    float hp_pct = (float)self->current_hitpoints / (float)self->base_hitpoints;

    opp_tick_cooldowns(opp);
    OppConsumables cons = opp_get_consumables(opp, self);

    /* 1. Defensive prayer */
    opp_apply_defensive_prayer(
        env, opp, actions, self, target, OPP_DEF_PRAYER_TARGET_GEAR);

    /* 2. Multi-threshold eating (same as improved) + drain restore */
    int eating = opp_apply_survival_policy(
        opp, actions, self, cons, OPP_SURVIVAL_STANDARD);

    /* Tick-level action delay */
    if (opp_should_skip_offensive(env, opp)) return;

    /* 3. Attack */
    if (opp_attack_ready(self) && !eating) {
        int style = opp_choose_attack_style(
            env, opp, self, target, OPP_STYLE_MASK_ALL);

        /* Boost potions before attack */
        opp_apply_boost_potion(env, opp, actions, self, style, 0);

        if (self->special_energy >= get_melee_spec_cost(self->melee_spec_weapon) && rand_float(env) < 0.30f) {
            int dist = chebyshev_distance(self->x, self->y, target->x, target->y);
            if (opp->target_fleeing_ticks >= 2 && dist > 1) {
                opp_apply_gear_switch(actions, OPP_STYLE_MAGE);
                actions[HEAD_COMBAT] = ATTACK_ICE;
            } else {
                opp_apply_gear_switch(actions, OPP_STYLE_SPEC);
                actions[HEAD_COMBAT] = ATTACK_ATK;
            }
        } else {
            /* Gear switch — offensive_prayer_miss: skip switch to omit auto-prayer */
            if (rand_float(env) < opp->offensive_prayer_miss) {
                actions[HEAD_LOADOUT] = LOADOUT_KEEP;
            } else {
                opp_apply_gear_switch(actions, style);
            }

            /* Offensive prayer */
            if (rand_float(env) < opp->offensive_prayer_rate) {

            }

            if (style == OPP_STYLE_MAGE) {
                int spell = (hp_pct < 0.30f) ? ATTACK_BLOOD : ATTACK_ICE;
                actions[HEAD_COMBAT] = spell;
            } else {
                actions[HEAD_COMBAT] = ATTACK_ATK;
            }
        }
    }
}

static void opp_competent_nh(OsrsEnv* env, OpponentState* opp, int* actions) {
    Player* self = &env->players[1];
    Player* target = &env->players[0];
    float hp_pct = (float)self->current_hitpoints / (float)self->base_hitpoints;

    opp_tick_cooldowns(opp);
    OppConsumables cons = opp_get_consumables(opp, self);

    /* 1. Defensive prayer */
    opp_apply_defensive_prayer(
        env, opp, actions, self, target, OPP_DEF_PRAYER_TARGET_GEAR);

    /* 2. Multi-threshold eating (same as improved) + drain restore */
    int eating = opp_apply_survival_policy(
        opp, actions, self, cons, OPP_SURVIVAL_STANDARD);

    /* Tick-level action delay */
    if (opp_should_skip_offensive(env, opp)) return;

    /* 3. Attack */
    if (opp_attack_ready(self) && !eating) {
        int attack_style = opp_choose_attack_style(
            env, opp, self, target, OPP_STYLE_MASK_ALL);

        /* Boost potions before attack */
        opp_apply_boost_potion(env, opp, actions, self, attack_style, 0);

        /* Spec check: same condition as intermediate_nh but 50% trigger rate */
        float target_hp_pct = (float)target->current_hitpoints / (float)target->base_hitpoints;
        int dist = chebyshev_distance(self->x, self->y, target->x, target->y);
        int can_spec_range = (self->frozen_ticks > 0) ? (dist <= 1) : (dist <= 3);
        int should_spec = (self->special_energy >= get_melee_spec_cost(self->melee_spec_weapon) &&
                          target_hp_pct < 0.60f &&
                          target->prayer != PRAYER_PROTECT_MELEE &&
                          can_spec_range &&
                          rand_float(env) < 0.50f);

        /* Anti-kite: cancel melee spec if target fleeing, force mage to freeze */
        if (should_spec && opp->target_fleeing_ticks >= 2 && dist > 1) {
            should_spec = 0;
            attack_style = OPP_STYLE_MAGE;
        }

        int actual_style;
        int actual_attack;
        if (should_spec) {
            actual_style = OPP_STYLE_SPEC;
            actual_attack = 3;
        } else if (attack_style == OPP_STYLE_MAGE) {
            actual_style = OPP_STYLE_MAGE;
            actual_attack = (hp_pct < 0.30f) ? 1 : 0;  /* blood if low, else ice */
        } else {
            actual_style = attack_style;
            actual_attack = 2;
        }

        /* Gear switch — offensive_prayer_miss: skip switch to omit auto-prayer */
        if (actual_attack != 3 && rand_float(env) < opp->offensive_prayer_miss) {
            actions[HEAD_LOADOUT] = LOADOUT_KEEP;
        } else {
            opp_apply_gear_switch(actions, actual_style);
        }

        /* Offensive prayer */
        if (rand_float(env) < opp->offensive_prayer_rate) {

        }

        opp_emit_combat_attack(actions, actual_attack);
    }
}

static void opp_intermediate_nh(OsrsEnv* env, OpponentState* opp, int* actions) {
    Player* self = &env->players[1];
    Player* target = &env->players[0];
    float hp_pct = (float)self->current_hitpoints / (float)self->base_hitpoints;

    opp_tick_cooldowns(opp);
    OppConsumables cons = opp_get_consumables(opp, self);

    /* 1. Defensive prayer */
    opp_apply_defensive_prayer(
        env, opp, actions, self, target, OPP_DEF_PRAYER_TARGET_GEAR);

    /* 2. Multi-threshold eating (same as improved) */
    int eating = opp_apply_survival_policy(
        opp, actions, self, cons, OPP_SURVIVAL_STANDARD);

    /* Tick-level action delay */
    if (opp_should_skip_offensive(env, opp)) return;

    /* 3. Attack */
    if (opp_attack_ready(self) && !eating) {
        int attack_style = opp_choose_attack_style(
            env, opp, self, target, OPP_STYLE_MASK_ALL);

        /* Boost potions before attack */
        opp_apply_boost_potion(env, opp, actions, self, attack_style, 0);

        /* Spec check: target HP < 60%, not on melee prayer, in range */
        float target_hp_pct = (float)target->current_hitpoints / (float)target->base_hitpoints;
        int dist = chebyshev_distance(self->x, self->y, target->x, target->y);
        int can_spec_range = (self->frozen_ticks > 0) ? (dist <= 1) : (dist <= 3);
        int should_spec = (self->special_energy >= get_melee_spec_cost(self->melee_spec_weapon) &&
                          target_hp_pct < 0.60f &&
                          target->prayer != PRAYER_PROTECT_MELEE &&
                          can_spec_range);

        /* Anti-kite: cancel melee spec if target fleeing, force mage to freeze */
        if (should_spec && opp->target_fleeing_ticks >= 2 && dist > 1) {
            should_spec = 0;
            attack_style = OPP_STYLE_MAGE;
        }

        int actual_style;
        int actual_attack;
        if (should_spec) {
            actual_style = OPP_STYLE_SPEC;
            actual_attack = 3;
        } else if (attack_style == OPP_STYLE_MAGE) {
            actual_style = OPP_STYLE_MAGE;
            actual_attack = (hp_pct < 0.30f) ? 1 : 0;  /* blood if low, else ice */
        } else {
            actual_style = attack_style;
            actual_attack = 2;
        }

        /* Gear switch — offensive_prayer_miss: skip switch to omit auto-prayer */
        if (actual_attack != 3 && rand_float(env) < opp->offensive_prayer_miss) {
            actions[HEAD_LOADOUT] = LOADOUT_KEEP;
        } else {
            opp_apply_gear_switch(actions, actual_style);
        }

        /* Offensive prayer */
        if (rand_float(env) < opp->offensive_prayer_rate) {

        }

        opp_emit_combat_attack(actions, actual_attack);
    }
}

static void opp_advanced_nh(OsrsEnv* env, OpponentState* opp, int* actions) {
    Player* self = &env->players[1];
    Player* target = &env->players[0];
    float hp_pct = (float)self->current_hitpoints / (float)self->base_hitpoints;

    opp_tick_cooldowns(opp);
    OppConsumables cons = opp_get_consumables(opp, self);

    /* 1. Defensive prayer */
    opp_apply_defensive_prayer(
        env, opp, actions, self, target, OPP_DEF_PRAYER_TARGET_GEAR);

    /* 2. Multi-threshold eating (same as improved) + drain restore */
    int eating = opp_apply_survival_policy(
        opp, actions, self, cons, OPP_SURVIVAL_STANDARD);

    /* Tick-level action delay */
    if (opp_should_skip_offensive(env, opp)) return;

    /* 3. Attack */
    if (opp_attack_ready(self) && !eating) {
        int attack_style = opp_choose_attack_style(
            env, opp, self, target, OPP_STYLE_MASK_ALL);

        /* Boost potions before attack */
        opp_apply_boost_potion(env, opp, actions, self, attack_style, 0);

        /* Spec: same as improved (no HP threshold, just not praying melee + in range) */
        int dist = chebyshev_distance(self->x, self->y, target->x, target->y);
        int can_spec_range = (self->frozen_ticks > 0) ? (dist <= 1) : (dist <= 3);
        int should_spec = (self->special_energy >= get_melee_spec_cost(self->melee_spec_weapon) &&
                          target->prayer != PRAYER_PROTECT_MELEE &&
                          can_spec_range);

        /* Anti-kite: cancel melee spec if target fleeing, force mage to freeze */
        if (should_spec && opp->target_fleeing_ticks >= 2 && dist > 1) {
            should_spec = 0;
            attack_style = OPP_STYLE_MAGE;
        }

        int actual_style;
        int actual_attack;
        if (should_spec) {
            actual_style = OPP_STYLE_SPEC;
            actual_attack = 3;
        } else if (attack_style == OPP_STYLE_MAGE) {
            actual_style = OPP_STYLE_MAGE;
            actual_attack = (hp_pct < 0.30f) ? 1 : 0;  /* blood if low, else ice */
        } else {
            actual_style = attack_style;
            actual_attack = 2;
        }

        /* Gear switch — offensive_prayer_miss: skip switch to omit auto-prayer */
        if (actual_attack != 3 && rand_float(env) < opp->offensive_prayer_miss) {
            actions[HEAD_LOADOUT] = LOADOUT_KEEP;
        } else {
            opp_apply_gear_switch(actions, actual_style);
        }

        /* Offensive prayer */
        if (rand_float(env) < opp->offensive_prayer_rate) {

        }

        opp_emit_combat_attack(actions, actual_attack);
    } else if (!opp_attack_ready(self)) {
        /* Movement: farcast 3 only (no step under) */
        int mv_dist = chebyshev_distance(self->x, self->y, target->x, target->y);
        if (opp->target_fleeing_ticks >= 2 && mv_dist > 3 && self->frozen_ticks == 0) {
            actions[HEAD_COMBAT] = MOVE_FARCAST_3;
        } else if (opp_should_fc3(self, target) && target->prayer != PRAYER_PROTECT_MELEE) {
            actions[HEAD_COMBAT] = MOVE_FARCAST_3;
        }
    }
}

static void opp_proficient_nh(OsrsEnv* env, OpponentState* opp, int* actions) {
    Player* self = &env->players[1];
    Player* target = &env->players[0];
    float hp_pct = (float)self->current_hitpoints / (float)self->base_hitpoints;

    opp_tick_cooldowns(opp);
    OppConsumables cons = opp_get_consumables(opp, self);

    /* 1. Defensive prayer */
    opp_apply_defensive_prayer(
        env, opp, actions, self, target, OPP_DEF_PRAYER_TARGET_GEAR);

    /* 2. Multi-threshold eating + drain restore */
    int eating = opp_apply_survival_policy(
        opp, actions, self, cons, OPP_SURVIVAL_STANDARD);

    /* Tick-level action delay */
    if (opp_should_skip_offensive(env, opp)) return;

    /* 3. Attack */
    if (opp_attack_ready(self) && !eating) {
        int attack_style = opp_choose_attack_style(
            env, opp, self, target, OPP_STYLE_MASK_ALL);

        /* Boost potions before attack */
        opp_apply_boost_potion(env, opp, actions, self, attack_style, 0);

        /* Spec: same as improved */
        int dist = chebyshev_distance(self->x, self->y, target->x, target->y);
        int can_spec_range = (self->frozen_ticks > 0) ? (dist <= 1) : (dist <= 3);
        int should_spec = (self->special_energy >= get_melee_spec_cost(self->melee_spec_weapon) &&
                          target->prayer != PRAYER_PROTECT_MELEE &&
                          can_spec_range);

        /* Anti-kite: cancel melee spec if target fleeing, force mage to freeze */
        if (should_spec && opp->target_fleeing_ticks >= 2 && dist > 1) {
            should_spec = 0;
            attack_style = OPP_STYLE_MAGE;
        }

        int actual_style;
        int actual_attack;
        if (should_spec) {
            actual_style = OPP_STYLE_SPEC;
            actual_attack = 3;
        } else if (attack_style == OPP_STYLE_MAGE) {
            actual_style = OPP_STYLE_MAGE;
            actual_attack = (hp_pct < 0.30f) ? 1 : 0;
        } else {
            actual_style = attack_style;
            actual_attack = 2;
        }

        /* Gear switch — offensive_prayer_miss: skip switch to omit auto-prayer */
        if (actual_attack != 3 && rand_float(env) < opp->offensive_prayer_miss) {
            actions[HEAD_LOADOUT] = LOADOUT_KEEP;
        } else {
            opp_apply_gear_switch(actions, actual_style);
        }

        /* Offensive prayer */
        if (rand_float(env) < opp->offensive_prayer_rate) {

        }

        opp_emit_combat_attack(actions, actual_attack);
    } else if (!opp_attack_ready(self)) {
        /* Movement: farcast 3 + 25% step under */
        int dist = chebyshev_distance(self->x, self->y, target->x, target->y);
        if (opp_target_frozen_after_pvp_timer_update(target) &&
            self->frozen_ticks == 0 && dist > 0 &&
            rand_float(env) < 0.25f) {
            actions[HEAD_COMBAT] = MOVE_UNDER;
        } else if (opp->target_fleeing_ticks >= 2 && dist > 3 && self->frozen_ticks == 0) {
            actions[HEAD_COMBAT] = MOVE_FARCAST_3;
        } else if (opp_should_fc3(self, target) && target->prayer != PRAYER_PROTECT_MELEE) {
            actions[HEAD_COMBAT] = MOVE_FARCAST_3;
        }
    }
}

static void opp_expert_nh(OsrsEnv* env, OpponentState* opp, int* actions) {
    Player* self = &env->players[1];
    Player* target = &env->players[0];
    float hp_pct = (float)self->current_hitpoints / (float)self->base_hitpoints;

    opp_tick_cooldowns(opp);
    OppConsumables cons = opp_get_consumables(opp, self);

    /* 1. Defensive prayer */
    opp_apply_defensive_prayer(
        env, opp, actions, self, target, OPP_DEF_PRAYER_TARGET_GEAR);

    /* 2. Multi-threshold eating (same as improved) + drain restore */
    int eating = opp_apply_survival_policy(
        opp, actions, self, cons, OPP_SURVIVAL_STANDARD);

    /* Tick-level action delay */
    if (opp_should_skip_offensive(env, opp)) return;

    /* 3. Attack */
    if (opp_attack_ready(self) && !eating) {
        int attack_style = opp_choose_attack_style(
            env, opp, self, target, OPP_STYLE_MASK_ALL);

        /* Boost potions before attack */
        opp_apply_boost_potion(env, opp, actions, self, attack_style, 0);

        /* Spec: same as improved */
        int dist = chebyshev_distance(self->x, self->y, target->x, target->y);
        int can_spec_range = (self->frozen_ticks > 0) ? (dist <= 1) : (dist <= 3);
        int should_spec = (self->special_energy >= get_melee_spec_cost(self->melee_spec_weapon) &&
                          target->prayer != PRAYER_PROTECT_MELEE &&
                          can_spec_range);

        /* Anti-kite: cancel melee spec if target fleeing, force mage to freeze */
        if (should_spec && opp->target_fleeing_ticks >= 2 && dist > 1) {
            should_spec = 0;
            attack_style = OPP_STYLE_MAGE;
        }

        int actual_style;
        int actual_attack;
        if (should_spec) {
            actual_style = OPP_STYLE_SPEC;
            actual_attack = 3;
        } else if (attack_style == OPP_STYLE_MAGE) {
            actual_style = OPP_STYLE_MAGE;
            actual_attack = (hp_pct < 0.30f) ? 1 : 0;
        } else {
            actual_style = attack_style;
            actual_attack = 2;
        }

        /* Gear switch — offensive_prayer_miss: skip switch to omit auto-prayer */
        if (actual_attack != 3 && rand_float(env) < opp->offensive_prayer_miss) {
            actions[HEAD_LOADOUT] = LOADOUT_KEEP;
        } else {
            opp_apply_gear_switch(actions, actual_style);
        }

        /* Offensive prayer */
        if (rand_float(env) < opp->offensive_prayer_rate) {

        }

        opp_emit_combat_attack(actions, actual_attack);
    } else if (!opp_attack_ready(self)) {
        /* Movement: farcast 3 + 50% step under */
        int dist = chebyshev_distance(self->x, self->y, target->x, target->y);
        if (opp_target_frozen_after_pvp_timer_update(target) &&
            self->frozen_ticks == 0 && dist > 0 &&
            rand_float(env) < 0.50f) {
            actions[HEAD_COMBAT] = MOVE_UNDER;
        } else if (opp->target_fleeing_ticks >= 2 && dist > 3 && self->frozen_ticks == 0) {
            actions[HEAD_COMBAT] = MOVE_FARCAST_3;
        } else if (opp_should_fc3(self, target) && target->prayer != PRAYER_PROTECT_MELEE) {
            actions[HEAD_COMBAT] = MOVE_FARCAST_3;
        }
    }
}

static void opp_onetick(OsrsEnv* env, OpponentState* opp, int* actions) {
    Player* self = &env->players[1];
    Player* target = &env->players[0];
    float hp_pct = (float)self->current_hitpoints / (float)self->base_hitpoints;

    opp_tick_cooldowns(opp);

    /* 0. Tank gear switch when not about to attack */
    if (!opp_attack_ready(self)) {
        opp_apply_tank_gear(actions);
    }

    /* 1. Defensive prayer with spec detection */
    opp_apply_defensive_prayer(
        env, opp, actions, self, target, OPP_DEF_PRAYER_TARGET_GEAR_WITH_SPEC);

    /* 2. Consumables (same thresholds as improved) */
    int potion_used = opp_apply_consumables(env, opp, actions, self);

    /* Check if eating was queued */
    int eating_queued = opp_check_eating_queued(actions);

    /* 3. Get off-prayer mask */
    int off_mask = opp_get_off_prayer_mask(self, target);

    /* 4. Fake switch logic */
    if (opp->fake_switch_pending && opp_attack_ready(self)) {
        /* Clear fake state when attack ready */
        opp->fake_switch_pending = 0;
        opp->fake_switch_style = -1;
    } else if (!opp_attack_ready(self) && !opp->fake_switch_pending && rand_float(env) < 0.30f) {
        /* Initiate fake switch */
        int current_style = (int)self->current_gear;
        /* Don't fake melee if frozen at distance */
        int can_fake_melee = self->frozen_ticks <= 10 ||
                             chebyshev_distance(self->x, self->y, target->x, target->y) <= 1;

        /* Build fake options: off-prayer, not current style, melee only if credible */
        int fake_options[3];
        int fake_count = 0;
        for (int s = 0; s < 3; s++) {
            if (!(off_mask & (1 << s))) continue;
            if (s == current_style) continue;
            if (s == OPP_STYLE_MELEE && !can_fake_melee) continue;
            fake_options[fake_count++] = s;
        }

        if (fake_count > 0) {
            opp->fake_switch_pending = 1;
            opp->fake_switch_style = fake_options[rand_int(env, fake_count)];
            opp->opponent_prayer_at_fake = opp_get_opponent_prayer_style(target);

            /* Fake switch: set loadout but no attack */
            opp_apply_fake_switch(actions, opp->fake_switch_style);

            /* Step under if target frozen */
            int dist = chebyshev_distance(self->x, self->y, target->x, target->y);
            if (opp_target_frozen_after_pvp_timer_update(target) &&
                    self->frozen_ticks == 0 && dist > 0) {
                actions[HEAD_COMBAT] = MOVE_UNDER;
            }

            /* Early return -- fake switch done this tick */
            return;
        }
    }

    /* 5. Determine attack style */
    /* If we just faked, anticipate opponent's prayer switch */
    int preferred_style = -1;
    if (opp->opponent_prayer_at_fake >= 0) {
        preferred_style = opp->opponent_prayer_at_fake;
        opp->opponent_prayer_at_fake = -1;
    }

    int dist = chebyshev_distance(self->x, self->y, target->x, target->y);
    int can_melee_spec_range = (self->frozen_ticks > 0) ? (dist <= 1) : (dist <= 3);
    float target_hp_pct = (float)target->current_hitpoints / (float)target->base_hitpoints;

    /* Spec checks: melee, ranged, magic */
    uint8_t ranged_spec = find_best_ranged_spec(self);
    uint8_t magic_spec = find_best_magic_spec(self);
    int has_ranged_or_magic_spec = (ranged_spec != ITEM_NONE || magic_spec != ITEM_NONE);

    /* If ranged/magic specs available, gate melee spec behind HP threshold too
     * so the boss saves energy for ranged/magic finishing blows */
    int should_melee_spec = opp_attack_ready(self) &&
                      self->special_energy >= get_melee_spec_cost(self->melee_spec_weapon) &&
                      target->prayer != PRAYER_PROTECT_MELEE &&
                      can_melee_spec_range &&
                      (!has_ranged_or_magic_spec || target_hp_pct < 0.55f);

    int should_ranged_spec = opp_attack_ready(self) && ranged_spec != ITEM_NONE &&
                      self->special_energy >= get_ranged_spec_cost(self->ranged_spec_weapon) &&
                      target->prayer != PRAYER_PROTECT_RANGED &&
                      target_hp_pct < 0.55f &&
                      opp_loadout_can_hit_now(env, self, target, LOADOUT_SPEC_RANGE, OPP_STYLE_RANGED);

    int should_magic_spec = opp_attack_ready(self) && magic_spec != ITEM_NONE &&
                      self->special_energy >= get_magic_spec_cost(self->magic_spec_weapon) &&
                      target->prayer != PRAYER_PROTECT_MAGIC &&
                      target_hp_pct < 0.55f &&
                      opp_loadout_can_hit_now(env, self, target, LOADOUT_SPEC_MAGIC, OPP_STYLE_MAGE);

    /* Anti-kite: cancel melee spec if target fleeing */
    if (should_melee_spec && opp->target_fleeing_ticks >= 2 && dist > 1) {
        should_melee_spec = 0;
    }

    int actual_style;
    int actual_attack;  /* 0=ice, 1=blood, 2=atk, 3=spec */
    int spec_loadout = LOADOUT_SPEC_MELEE;  /* default, overridden below */

    /* Spec priority: ranged at distance > magic off-prayer > melee in range */
    if (should_ranged_spec && (dist >= 3 || target->frozen_ticks > 0)) {
        actual_style = OPP_STYLE_RANGED;
        actual_attack = 3;
        spec_loadout = LOADOUT_SPEC_RANGE;
    } else if (should_magic_spec) {
        actual_style = OPP_STYLE_MAGE;
        actual_attack = 3;
        spec_loadout = LOADOUT_SPEC_MAGIC;
    } else if (should_melee_spec) {
        actual_style = OPP_STYLE_SPEC;
        actual_attack = 3;
    } else if (target->frozen_ticks == 0 && (off_mask & (1 << OPP_STYLE_MAGE))) {
        actual_style = OPP_STYLE_MAGE;
        actual_attack = (hp_pct < 0.98f)
            ? ((target->freeze_immunity_ticks <= 1 && target->frozen_ticks == 0) ? 0 : 1)
            : 0;  /* ice at full HP */
        /* Simplified: use opp_get_mage_attack for ice/blood decision */
        actual_attack = opp_get_mage_attack(self, target) == ATTACK_ICE ? 0 : 1;
    } else {
        /* Target frozen or mage not off-prayer — choose based on fake anticipation */
        int can_use_preferred = preferred_style >= 0 &&
            (preferred_style != OPP_STYLE_MELEE || self->frozen_ticks <= 10 || dist <= 1);

        if (can_use_preferred) {
            actual_style = preferred_style;
            if (preferred_style == OPP_STYLE_MAGE) {
                actual_attack = (hp_pct < 0.98f) ? 1 : 0;  /* blood if not full HP */
            } else {
                actual_attack = 2;  /* ATK */
            }
        } else if (off_mask & (1 << OPP_STYLE_MAGE)) {
            actual_style = OPP_STYLE_MAGE;
            actual_attack = (hp_pct < 0.98f) ? 1 : 0;
        } else {
            /* Pick non-mage from off-prayer */
            int non_mage[2];
            int nm_count = 0;
            for (int s = 1; s < 3; s++) {
                if (off_mask & (1 << s)) non_mage[nm_count++] = s;
            }
            if (nm_count == 0) {
                actual_style = OPP_STYLE_RANGED;
            } else {
                actual_style = non_mage[rand_int(env, nm_count)];
            }
            actual_attack = 2;  /* ATK */
        }
    }

    opp_resolve_normal_attack(
        env, opp, self, target, OPP_STYLE_MASK_ALL, &actual_style, &actual_attack);

    /* 6. Boost potions (before attack) */
    opp_apply_boost_potion(env, opp, actions, self, actual_style, potion_used);

    /* Tick-level action delay: skip attack but keep prayer/eating/fakes */
    if (opp_should_skip_offensive(env, opp)) return;

    /* 7. Gear + offensive prayer + attack */
    if (opp_attack_ready(self) && !eating_queued) {
        /* Gear switch — offensive_prayer_miss: skip switch to omit auto-prayer */
        if (actual_attack == 3) {
            actions[HEAD_LOADOUT] = spec_loadout;
        } else if (rand_float(env) < opp->offensive_prayer_miss) {
            actions[HEAD_LOADOUT] = LOADOUT_KEEP;
        } else {
            opp_apply_gear_switch(actions, actual_style);
        }

        opp_emit_combat_attack(actions, actual_attack);
    } else if (!opp_attack_ready(self)) {
        /* Movement when not attacking */
        if (opp_target_frozen_after_pvp_timer_update(target) &&
                self->frozen_ticks == 0 && dist > 0) {
            actions[HEAD_COMBAT] = MOVE_UNDER;
        } else if (opp->target_fleeing_ticks >= 2 && dist > 3 && self->frozen_ticks == 0) {
            actions[HEAD_COMBAT] = MOVE_FARCAST_3;
        } else if (opp_should_fc3(self, target) && target->prayer != PRAYER_PROTECT_MELEE) {
            actions[HEAD_COMBAT] = MOVE_FARCAST_3;
        }
    }

}

static void opp_unpredictable_improved(OsrsEnv* env, OpponentState* opp, int* actions) {
    Player* self = &env->players[1];
    Player* target = &env->players[0];
    float hp_pct = (float)self->current_hitpoints / (float)self->base_hitpoints;

    opp_tick_cooldowns(opp);

    /* 1. Handle prayer switch with delay */
    opp_handle_delayed_prayer(env, opp, actions, self, target,
                               UNPREDICTABLE_IMP_PRAYER_CUM, UNPREDICTABLE_IMP_PRAYER_CUM_LEN,
                               UNPREDICTABLE_IMP_WRONG_PRAYER, 0 /* no spec detection */);

    /* 2. Consumables (no delay — survival instinct) */
    int potion_used = opp_apply_consumables(env, opp, actions, self);

    int eating_queued = opp_check_eating_queued(actions);

    /* Tick-level action delay (additional layer on top of built-in delays) */
    if (opp_should_skip_offensive(env, opp)) return;

    /* 3. Attack style decision (with small mistake chance + style bias) */
    int attack_style;

    if (rand_float(env) < UNPREDICTABLE_IMP_SUBOPTIMAL_ATTACK) {
        attack_style = opp_resolve_attack_style(
            env, opp, self, target, OPP_STYLE_MASK_ALL,
            opp_random_style_preference(env, OPP_STYLE_MASK_ALL)).style;
    } else {
        attack_style = opp_pick_off_prayer_style_biased(env, opp, self, target);
    }

    /* Boost potions before attack */
    opp_apply_boost_potion(env, opp, actions, self, attack_style, potion_used);

    /* 4. Determine actual attack */
    if (opp_attack_ready(self) && !eating_queued) {
        int dist = chebyshev_distance(self->x, self->y, target->x, target->y);
        int can_spec_range = (self->frozen_ticks > 0) ? (dist <= 1) : (dist <= 3);
        int should_spec = self->special_energy >= get_melee_spec_cost(self->melee_spec_weapon) &&
                          target->prayer != PRAYER_PROTECT_MELEE &&
                          can_spec_range;

        /* Anti-kite: cancel melee spec if target fleeing, force mage to freeze */
        if (should_spec && opp->target_fleeing_ticks >= 2 && dist > 1) {
            should_spec = 0;
            attack_style = OPP_STYLE_MAGE;
        }

        int actual_style;
        int actual_attack;

        if (should_spec) {
            actual_style = OPP_STYLE_SPEC;
            actual_attack = 3;
        } else if (attack_style == OPP_STYLE_MAGE) {
            actual_style = OPP_STYLE_MAGE;
            actual_attack = (hp_pct < 0.30f) ? 1 : 0;  /* blood if very low */
        } else {
            actual_style = attack_style;
            actual_attack = 2;
        }

        /* 5. Gear switch — offensive_prayer_miss: skip switch to omit auto-prayer */
        if (actual_attack != 3 && rand_float(env) < opp->offensive_prayer_miss) {
            actions[HEAD_LOADOUT] = LOADOUT_KEEP;
        } else {
            opp_apply_gear_switch(actions, actual_style);
        }

        /* 6. Offensive prayer */


        /* 7. Attack with delay — sample delay, skip if > 0 */
        int action_delay = opp_sample_delay(env, UNPREDICTABLE_IMP_ACTION_CUM, UNPREDICTABLE_IMP_ACTION_CUM_LEN);
        if (action_delay == 0) {
            opp_emit_combat_attack(actions, actual_attack);
        }
        /* else: missed attack window due to delay */
    } else if (!opp_attack_ready(self)) {
        int dist = chebyshev_distance(self->x, self->y, target->x, target->y);
        if (opp_target_frozen_after_pvp_timer_update(target) &&
                self->frozen_ticks == 0 && dist > 0) {
            actions[HEAD_COMBAT] = MOVE_UNDER;
        } else if (opp->target_fleeing_ticks >= 2 && dist > 3 && self->frozen_ticks == 0) {
            actions[HEAD_COMBAT] = MOVE_FARCAST_3;
        } else if (opp_should_fc3(self, target) && target->prayer != PRAYER_PROTECT_MELEE) {
            actions[HEAD_COMBAT] = MOVE_FARCAST_3;
        }
    }

    (void)potion_used;
}

static void opp_unpredictable_onetick(OsrsEnv* env, OpponentState* opp, int* actions) {
    Player* self = &env->players[1];
    Player* target = &env->players[0];
    float hp_pct = (float)self->current_hitpoints / (float)self->base_hitpoints;

    opp_tick_cooldowns(opp);

    /* 0. Tank gear when not about to attack */
    if (!opp_attack_ready(self)) {
        opp_apply_tank_gear(actions);
    }

    /* 1. Handle prayer switch with delay (with spec detection) */
    opp_handle_delayed_prayer(env, opp, actions, self, target,
                               UNPREDICTABLE_OT_PRAYER_CUM, UNPREDICTABLE_OT_PRAYER_CUM_LEN,
                               0.0f /* no wrong prayer for onetick */, 1 /* include spec */);

    /* 2. Consumables */
    int potion_used = opp_apply_consumables(env, opp, actions, self);

    int eating_queued = opp_check_eating_queued(actions);

    /* 3. Get off-prayer mask */
    int off_mask = opp_get_off_prayer_mask(self, target);

    /* 4. Fake switch logic (same as onetick + failure chance) */
    if (opp->fake_switch_pending && opp_attack_ready(self)) {
        opp->fake_switch_pending = 0;
        opp->fake_switch_style = -1;
        opp->fake_switch_failed = 0;
    } else if (!opp_attack_ready(self) && !opp->fake_switch_pending && rand_float(env) < 0.30f) {
        int current_style = (int)self->current_gear;
        int can_fake_melee = self->frozen_ticks <= 10 ||
                             chebyshev_distance(self->x, self->y, target->x, target->y) <= 1;

        int fake_options[3];
        int fake_count = 0;
        for (int s = 0; s < 3; s++) {
            if (!(off_mask & (1 << s))) continue;
            if (s == current_style) continue;
            if (s == OPP_STYLE_MELEE && !can_fake_melee) continue;
            fake_options[fake_count++] = s;
        }

        if (fake_count > 0) {
            opp->fake_switch_pending = 1;
            opp->fake_switch_style = fake_options[rand_int(env, fake_count)];
            opp->opponent_prayer_at_fake = opp_get_opponent_prayer_style(target);

            /* Roll fake execution failure */
            opp->fake_switch_failed = (rand_float(env) < UNPREDICTABLE_OT_FAKE_FAIL) ? 1 : 0;

            /* Fake switch: set loadout but no attack */
            opp_apply_fake_switch(actions, opp->fake_switch_style);

            int dist = chebyshev_distance(self->x, self->y, target->x, target->y);
            if (opp_target_frozen_after_pvp_timer_update(target) &&
                    self->frozen_ticks == 0 && dist > 0) {
                actions[HEAD_COMBAT] = MOVE_UNDER;
            }

            return;  /* Early return: fake switch done */
        }
    }

    /* 5. Determine attack style with fake anticipation + failure/prediction errors */
    int preferred_style = -1;

    if (opp->opponent_prayer_at_fake >= 0 && !opp->fake_switch_failed) {
        /* Fake succeeded — but small chance of wrong prediction */
        if (rand_float(env) < UNPREDICTABLE_OT_WRONG_PREDICT) {
            preferred_style = rand_int(env, 3);  /* random style */
        } else {
            preferred_style = opp->opponent_prayer_at_fake;
        }
        opp->opponent_prayer_at_fake = -1;
    } else if (opp->fake_switch_failed) {
        /* Fake failed — no preferred style */
        opp->opponent_prayer_at_fake = -1;
        opp->fake_switch_failed = 0;
    }

    int dist = chebyshev_distance(self->x, self->y, target->x, target->y);
    int can_melee_spec_range = (self->frozen_ticks > 0) ? (dist <= 1) : (dist <= 3);
    float target_hp_pct = (float)target->current_hitpoints / (float)target->base_hitpoints;

    /* Spec checks: melee, ranged, magic */
    uint8_t ranged_spec = find_best_ranged_spec(self);
    uint8_t magic_spec = find_best_magic_spec(self);
    int has_ranged_or_magic_spec = (ranged_spec != ITEM_NONE || magic_spec != ITEM_NONE);

    int should_melee_spec = opp_attack_ready(self) &&
                      self->special_energy >= get_melee_spec_cost(self->melee_spec_weapon) &&
                      target->prayer != PRAYER_PROTECT_MELEE &&
                      can_melee_spec_range &&
                      (!has_ranged_or_magic_spec || target_hp_pct < 0.55f);

    int should_ranged_spec = opp_attack_ready(self) && ranged_spec != ITEM_NONE &&
                      self->special_energy >= get_ranged_spec_cost(self->ranged_spec_weapon) &&
                      target->prayer != PRAYER_PROTECT_RANGED &&
                      target_hp_pct < 0.55f &&
                      opp_loadout_can_hit_now(env, self, target, LOADOUT_SPEC_RANGE, OPP_STYLE_RANGED);

    int should_magic_spec = opp_attack_ready(self) && magic_spec != ITEM_NONE &&
                      self->special_energy >= get_magic_spec_cost(self->magic_spec_weapon) &&
                      target->prayer != PRAYER_PROTECT_MAGIC &&
                      target_hp_pct < 0.55f &&
                      opp_loadout_can_hit_now(env, self, target, LOADOUT_SPEC_MAGIC, OPP_STYLE_MAGE);

    /* Anti-kite: cancel melee spec if target fleeing */
    if (should_melee_spec && opp->target_fleeing_ticks >= 2 && dist > 1) {
        should_melee_spec = 0;
    }

    int actual_style;
    int actual_attack;
    int spec_loadout = LOADOUT_SPEC_MELEE;

    /* Spec priority: ranged at distance > magic off-prayer > melee in range */
    if (should_ranged_spec && (dist >= 3 || target->frozen_ticks > 0)) {
        actual_style = OPP_STYLE_RANGED;
        actual_attack = 3;
        spec_loadout = LOADOUT_SPEC_RANGE;
    } else if (should_magic_spec) {
        actual_style = OPP_STYLE_MAGE;
        actual_attack = 3;
        spec_loadout = LOADOUT_SPEC_MAGIC;
    } else if (should_melee_spec) {
        actual_style = OPP_STYLE_SPEC;
        actual_attack = 3;
    } else if (target->frozen_ticks == 0 && (off_mask & (1 << OPP_STYLE_MAGE))) {
        actual_style = OPP_STYLE_MAGE;
        actual_attack = opp_get_mage_attack(self, target) == ATTACK_ICE ? 0 : 1;
    } else {
        int can_use_preferred = preferred_style >= 0 &&
            (preferred_style != OPP_STYLE_MELEE || self->frozen_ticks <= 10 || dist <= 1);

        if (can_use_preferred) {
            actual_style = preferred_style;
            actual_attack = (preferred_style == OPP_STYLE_MAGE)
                ? ((hp_pct < 0.98f) ? 1 : 0)
                : 2;
        } else if (off_mask & (1 << OPP_STYLE_MAGE)) {
            actual_style = OPP_STYLE_MAGE;
            actual_attack = (hp_pct < 0.98f) ? 1 : 0;
        } else {
            int non_mage[2];
            int nm_count = 0;
            for (int s = 1; s < 3; s++) {
                if (off_mask & (1 << s)) non_mage[nm_count++] = s;
            }
            actual_style = (nm_count > 0) ? non_mage[rand_int(env, nm_count)] : OPP_STYLE_RANGED;
            actual_attack = 2;
        }
    }

    opp_resolve_normal_attack(
        env, opp, self, target, OPP_STYLE_MASK_ALL, &actual_style, &actual_attack);

    /* 6. Boost potions */
    opp_apply_boost_potion(env, opp, actions, self, actual_style, potion_used);

    /* Tick-level action delay (additional layer) */
    if (opp_should_skip_offensive(env, opp)) return;

    /* 7. Gear + attack with delay chance */
    if (opp_attack_ready(self) && !eating_queued) {
        int action_delay = opp_sample_delay(env, UNPREDICTABLE_OT_ACTION_CUM, UNPREDICTABLE_OT_ACTION_CUM_LEN);
        if (action_delay == 0) {
            /* Gear switch — spec uses spec_loadout directly */
            if (actual_attack == 3) {
                actions[HEAD_LOADOUT] = spec_loadout;
            } else if (rand_float(env) < opp->offensive_prayer_miss) {
                actions[HEAD_LOADOUT] = LOADOUT_KEEP;
            } else {
                opp_apply_gear_switch(actions, actual_style);
            }


            opp_emit_combat_attack(actions, actual_attack);
        }
        /* else: missed attack window due to delay */
    } else if (!opp_attack_ready(self)) {
        if (opp_target_frozen_after_pvp_timer_update(target) &&
                self->frozen_ticks == 0 && dist > 0) {
            actions[HEAD_COMBAT] = MOVE_UNDER;
        } else if (opp->target_fleeing_ticks >= 2 && dist > 3 && self->frozen_ticks == 0) {
            actions[HEAD_COMBAT] = MOVE_FARCAST_3;
        } else if (opp_should_fc3(self, target) && target->prayer != PRAYER_PROTECT_MELEE) {
            actions[HEAD_COMBAT] = MOVE_FARCAST_3;
        }
    }
}

static uint8_t pvp_predict_action_weapon_after_equip_clicks(Player* p, const int* actions) {
    uint8_t weapon = p->equipped[GEAR_SLOT_WEAPON];

    for (int h = 0; h < PVP_EQUIP_CLICKS_PER_TICK; h++) {
        int action = actions[HEAD_EQUIP_0 + h];
        if (action <= 0 || action > OSRS_INVENTORY_SIZE) continue;

        int inventory_slot = action - 1;
        uint8_t item = p->inventory[inventory_slot];
        if (item == ITEM_NONE || item >= NUM_ITEMS) continue;

        int gear_slot = osrs_item_gear_slot(item);
        if (gear_slot == GEAR_SLOT_WEAPON) {
            weapon = item;
        } else if (gear_slot == GEAR_SLOT_SHIELD &&
                weapon < NUM_ITEMS && item_is_two_handed(weapon)) {
            weapon = ITEM_NONE;
        }
    }

    return weapon;
}

static void opp_read_agent_action(OsrsEnv* env, OpponentState* opp) {
    opp->has_read_this_tick = 0;
    opp->read_agent_style = ATTACK_STYLE_NONE;
    opp->read_agent_prayer = PRAYER_NONE;
    opp->read_agent_moving = 0;

    if (opp->read_chance <= 0.0f || rand_float(env) >= opp->read_chance) {
        return;  /* Read failed or no read ability */
    }

    /* Read succeeded - read agent's CURRENT tick actions (player 0)
     * IMPORTANT: Read from env->actions, not pending_actions.
     * pending_actions contains PREVIOUS tick's actions.
     * env->actions is populated from ocean_acts before opponent generation. */
    int* agent_actions = &env->actions[0];

    int attack = agent_actions[HEAD_ATTACK];

    if (attack == ATTACK_ICE || attack == ATTACK_BLOOD) {
        opp->read_agent_style = ATTACK_STYLE_MAGIC;
        opp->has_read_this_tick = 1;
    } else if (attack == ATTACK_ATK) {
        uint8_t weapon = pvp_predict_action_weapon_after_equip_clicks(
            &env->players[0], agent_actions);
        int style = get_item_attack_style(weapon);
        if (style == 1) opp->read_agent_style = ATTACK_STYLE_MELEE;
        else if (style == 2) opp->read_agent_style = ATTACK_STYLE_RANGED;
        else if (style == 3) opp->read_agent_style = ATTACK_STYLE_MAGIC;
        opp->has_read_this_tick = 1;
    }

    /* Extract overhead prayer intent from the agent's explicit set/refresh action. */
    int overhead = agent_actions[HEAD_OVERHEAD];
    if (overhead == ENCOUNTER_OVERHEAD_SET_REFRESH_MELEE)       opp->read_agent_prayer = PRAYER_PROTECT_MELEE;
    else if (overhead == ENCOUNTER_OVERHEAD_SET_REFRESH_RANGED) opp->read_agent_prayer = PRAYER_PROTECT_RANGED;
    else if (overhead == ENCOUNTER_OVERHEAD_SET_REFRESH_MAGIC)  opp->read_agent_prayer = PRAYER_PROTECT_MAGIC;
    else if (overhead == ENCOUNTER_OVERHEAD_SET_REFRESH_SMITE)  opp->read_agent_prayer = PRAYER_SMITE;
    else if (overhead == ENCOUNTER_OVERHEAD_SET_REFRESH_REDEMPTION) opp->read_agent_prayer = PRAYER_REDEMPTION;

    opp->read_agent_moving = (agent_actions[HEAD_MOVE] != 0 || is_move_action(attack)) ? 1 : 0;
}

static inline int opp_get_read_defensive_prayer(const OpponentState* opp) {
    if (opp->read_agent_style == ATTACK_STYLE_MAGIC) return OVERHEAD_MAGE;
    if (opp->read_agent_style == ATTACK_STYLE_RANGED) return OVERHEAD_RANGED;
    if (opp->read_agent_style == ATTACK_STYLE_MELEE) return OVERHEAD_MELEE;
    return -1;
}

static inline int opp_style_off_read_prayer(OpponentState* opp, int style) {
    if (opp->read_agent_prayer == PRAYER_NONE) return 1;  /* No read, assume off */
    if (style == OPP_STYLE_MAGE && opp->read_agent_prayer != PRAYER_PROTECT_MAGIC) return 1;
    if (style == OPP_STYLE_RANGED && opp->read_agent_prayer != PRAYER_PROTECT_RANGED) return 1;
    if (style == OPP_STYLE_MELEE && opp->read_agent_prayer != PRAYER_PROTECT_MELEE) return 1;
    return 0;  /* Would hit on-prayer */
}

typedef struct {
    int presents_mage;
    int protects_magic;
    int adjacent;
    int melee_attack_resolved;
    int melee_recent_count;
    int mage_camp_ticks;
    int melee_threat_ticks;
    int attack_ready_soon;
} PvpMageCampMeleeSignal;

static inline int pvp_recent_attack_style_count(
    const AttackStyle attacks[HISTORY_SIZE],
    AttackStyle style
);

static inline PvpMageCampMeleeSignal pvp_mage_camp_melee_signal(
    const OpponentState* opp,
    const Player* self,
    const Player* target
);

static inline int pvp_should_counter_mage_camp_melee(PvpMageCampMeleeSignal s);
static inline int pvp_should_pray_melee_against_mage_camp(PvpMageCampMeleeSignal s);

static void opp_master_nh(OsrsEnv* env, OpponentState* opp, int* actions) {
    Player* self = &env->players[1];
    Player* target = &env->players[0];

    opp_tick_cooldowns(opp);

    /* Attempt to read agent's pending action */
    opp_read_agent_action(env, opp);
    PvpMageCampMeleeSignal mage_camp_signal =
        pvp_mage_camp_melee_signal(opp, self, target);
    int counter_mage_camp =
        pvp_should_counter_mage_camp_melee(mage_camp_signal);

    /* 0. Tank gear switch when not about to attack */
    if (!opp_attack_ready(self)) {
        opp_apply_tank_gear(actions);
    }

    /* 1. Defensive prayer - use read info if available, else detect from gear */
    int def_prayer = -1;
    if (opp->has_read_this_tick && opp->read_agent_style != ATTACK_STYLE_NONE) {
        def_prayer = opp_get_read_defensive_prayer(opp);
    }
    if (def_prayer < 0 && pvp_should_pray_melee_against_mage_camp(mage_camp_signal)) {
        def_prayer = OVERHEAD_MELEE;
    }
    if (def_prayer < 0) {
        def_prayer = opp_pick_defensive_prayer(
            env, opp, target, OPP_DEF_PRAYER_TARGET_GEAR_WITH_SPEC);
    } else {
        def_prayer = opp_apply_prayer_mistake(env, opp, def_prayer);
    }
    if (!opp_has_prayer_active(self, def_prayer)) {
        opp_emit_prayer(actions, self, def_prayer);
    }

    /* 2. Consumables (same as onetick) */
    int potion_used = opp_apply_consumables(env, opp, actions, self);
    int eating_queued = opp_check_eating_queued(actions);

    /* 3. Get off-prayer mask (normal) and check read info for better targeting */
    int off_mask = opp_get_off_prayer_mask(self, target);

    /* 4. Fake switch logic (same as onetick) */
    if (opp->fake_switch_pending && opp_attack_ready(self)) {
        opp->fake_switch_pending = 0;
        opp->fake_switch_style = -1;
    } else if (!opp_attack_ready(self) && !opp->fake_switch_pending && rand_float(env) < 0.30f) {
        int current_style = (int)self->current_gear;
        int can_fake_melee = self->frozen_ticks <= 10 ||
                             chebyshev_distance(self->x, self->y, target->x, target->y) <= 1;

        int fake_options[3];
        int fake_count = 0;
        for (int s = 0; s < 3; s++) {
            if (!(off_mask & (1 << s))) continue;
            if (s == current_style) continue;
            if (s == OPP_STYLE_MELEE && !can_fake_melee) continue;
            fake_options[fake_count++] = s;
        }

        if (fake_count > 0) {
            opp->fake_switch_pending = 1;
            opp->fake_switch_style = fake_options[rand_int(env, fake_count)];
            opp->opponent_prayer_at_fake = opp_get_opponent_prayer_style(target);

            opp_apply_fake_switch(actions, opp->fake_switch_style);

            int dist = chebyshev_distance(self->x, self->y, target->x, target->y);
            if (opp_target_frozen_after_pvp_timer_update(target) &&
                    self->frozen_ticks == 0 && dist > 0) {
                actions[HEAD_COMBAT] = MOVE_UNDER;
            }
            return;
        }
    }

    /* 5. Determine attack style - use read info if available */
    int preferred_style = -1;
    if (opp->opponent_prayer_at_fake >= 0) {
        preferred_style = opp->opponent_prayer_at_fake;
        opp->opponent_prayer_at_fake = -1;
    }

    /* If we read agent's prayer, pick a style they're NOT praying against */
    if (opp->has_read_this_tick && opp->read_agent_prayer != PRAYER_NONE) {
        /* Find best off-prayer style using read info */
        int read_off_styles[3];
        int read_off_count = 0;
        for (int s = 0; s < 3; s++) {
            if (!(off_mask & (1 << s))) continue;
            if (opp_style_off_read_prayer(opp, s)) {
                read_off_styles[read_off_count++] = s;
            }
        }
        if (read_off_count > 0) {
            preferred_style = read_off_styles[rand_int(env, read_off_count)];
        }
    }

    int dist = chebyshev_distance(self->x, self->y, target->x, target->y);
    int can_melee_spec_range = (self->frozen_ticks > 0) ? (dist <= 1) : (dist <= 3);
    float target_hp_pct = (float)target->current_hitpoints / (float)target->base_hitpoints;
    int should_control_freeze = target->frozen_ticks == 0 &&
        target->freeze_immunity_ticks == 0 &&
        (dist == 0 || opp_style_can_hit_now(env, self, target, OPP_STYLE_MAGE));

    /* Spec checks: melee, ranged, magic */
    uint8_t ranged_spec = find_best_ranged_spec(self);
    uint8_t magic_spec = find_best_magic_spec(self);
    int has_ranged_or_magic_spec = (ranged_spec != ITEM_NONE || magic_spec != ITEM_NONE);

    int should_melee_spec = opp_attack_ready(self) &&
                      self->special_energy >= get_melee_spec_cost(self->melee_spec_weapon) &&
                      target->prayer != PRAYER_PROTECT_MELEE &&
                      can_melee_spec_range &&
                      (!has_ranged_or_magic_spec || target_hp_pct < 0.55f);

    int should_ranged_spec = opp_attack_ready(self) && ranged_spec != ITEM_NONE &&
                      self->special_energy >= get_ranged_spec_cost(self->ranged_spec_weapon) &&
                      target->prayer != PRAYER_PROTECT_RANGED &&
                      target_hp_pct < 0.55f &&
                      opp_loadout_can_hit_now(env, self, target, LOADOUT_SPEC_RANGE, OPP_STYLE_RANGED);

    int should_magic_spec = opp_attack_ready(self) && magic_spec != ITEM_NONE &&
                      self->special_energy >= get_magic_spec_cost(self->magic_spec_weapon) &&
                      target->prayer != PRAYER_PROTECT_MAGIC &&
                      target_hp_pct < 0.55f &&
                      opp_loadout_can_hit_now(env, self, target, LOADOUT_SPEC_MAGIC, OPP_STYLE_MAGE);

    /* With read, cancel specs the agent is praying against */
    if (opp->has_read_this_tick) {
        if (should_melee_spec && opp->read_agent_prayer == PRAYER_PROTECT_MELEE)
            should_melee_spec = 0;
        if (should_ranged_spec && opp->read_agent_prayer == PRAYER_PROTECT_RANGED)
            should_ranged_spec = 0;
        if (should_magic_spec && opp->read_agent_prayer == PRAYER_PROTECT_MAGIC)
            should_magic_spec = 0;
    }

    /* Anti-kite: cancel melee spec if target fleeing */
    if (should_melee_spec && opp->target_fleeing_ticks >= 2 && dist > 1) {
        should_melee_spec = 0;
    }

    /* Read-based anti-kite: if agent about to move away, cancel melee spec */
    if (should_melee_spec && opp->has_read_this_tick && opp->read_agent_moving && dist > 1) {
        should_melee_spec = 0;
    }

    int actual_style;
    int actual_attack;
    int spec_loadout = LOADOUT_SPEC_MELEE;

    if (counter_mage_camp && mage_camp_signal.adjacent && self->frozen_ticks == 0 &&
            should_control_freeze) {
        actual_style = OPP_STYLE_MAGE;
        actual_attack = 0;
    } else if (counter_mage_camp && mage_camp_signal.adjacent && self->frozen_ticks == 0) {
        actual_style = OPP_STYLE_RANGED;
        actual_attack = 2;
    } else if (should_ranged_spec && (dist >= 3 || target->frozen_ticks > 0)) {
        actual_style = OPP_STYLE_RANGED;
        actual_attack = 3;
        spec_loadout = LOADOUT_SPEC_RANGE;
    } else if (should_magic_spec) {
        actual_style = OPP_STYLE_MAGE;
        actual_attack = 3;
        spec_loadout = LOADOUT_SPEC_MAGIC;
    } else if (should_melee_spec) {
        actual_style = OPP_STYLE_SPEC;
        actual_attack = 3;
    } else if (should_control_freeze) {
        actual_style = OPP_STYLE_MAGE;
        actual_attack = 0;
    } else if (preferred_style >= 0) {
        actual_style = preferred_style;
        actual_attack = (preferred_style == OPP_STYLE_MAGE)
            ? (opp_get_mage_attack(self, target) == ATTACK_ICE ? 0 : 1)
            : 2;
    } else if (target->frozen_ticks == 0 && (off_mask & (1 << OPP_STYLE_MAGE))) {
        actual_style = OPP_STYLE_MAGE;
        actual_attack = opp_get_mage_attack(self, target) == ATTACK_ICE ? 0 : 1;
    } else {
        actual_style = opp_resolve_attack_style(
            env, opp, self, target, OPP_STYLE_MASK_ALL, off_mask).style;
        actual_attack = (actual_style == OPP_STYLE_MAGE)
            ? (opp_get_mage_attack(self, target) == ATTACK_ICE ? 0 : 1)
            : 2;
    }

    opp_resolve_normal_attack(
        env, opp, self, target, OPP_STYLE_MASK_ALL, &actual_style, &actual_attack);

    /* 6. Boost potions */
    opp_apply_boost_potion(env, opp, actions, self, actual_style, potion_used);

    /* Tick-level action delay */
    if (opp_should_skip_offensive(env, opp)) return;

    /* 7. Gear + attack */
    if (opp_attack_ready(self) && !eating_queued) {
        /* Spec: use spec_loadout directly; normal: gear switch with prayer miss */
        if (actual_attack == 3) {
            actions[HEAD_LOADOUT] = spec_loadout;
        } else if (rand_float(env) < opp->offensive_prayer_miss) {
            actions[HEAD_LOADOUT] = LOADOUT_KEEP;
        } else {
            opp_apply_gear_switch(actions, actual_style);
        }

        opp_emit_combat_attack(actions, actual_attack);
    } else if (!opp_attack_ready(self)) {
        if (opp_target_frozen_after_pvp_timer_update(target) &&
                self->frozen_ticks == 0 && dist > 0) {
            actions[HEAD_COMBAT] = MOVE_UNDER;
        } else if (opp->target_fleeing_ticks >= 2 && dist > 3 && self->frozen_ticks == 0) {
            actions[HEAD_COMBAT] = MOVE_FARCAST_3;
        } else if (opp_should_fc3(self, target) && target->prayer != PRAYER_PROTECT_MELEE) {
            actions[HEAD_COMBAT] = MOVE_FARCAST_3;
        }
    }
}

static void opp_savant_nh(OsrsEnv* env, OpponentState* opp, int* actions) {
    /* Savant uses the same logic as master, just with higher read_chance (set in reset) */
    opp_master_nh(env, opp, actions);
}

static void opp_nightmare_nh(OsrsEnv* env, OpponentState* opp, int* actions) {
    /* Nightmare uses the same logic as master, just with 50% read_chance (set in reset) */
    opp_master_nh(env, opp, actions);
}

static void opp_veng_fighter(OsrsEnv* env, OpponentState* opp, int* actions) {
    Player* self = &env->players[1];
    Player* target = &env->players[0];

    opp_tick_cooldowns(opp);
    OppConsumables cons = opp_get_consumables(opp, self);

    /* 1. Defensive prayer (same as expert_nh) */
    opp_apply_defensive_prayer(
        env, opp, actions, self, target, OPP_DEF_PRAYER_TARGET_GEAR);

    /* 2. Multi-threshold eating (same as expert_nh) */
    int eating = opp_apply_survival_policy(
        opp, actions, self, cons, OPP_SURVIVAL_LUNAR);

    /* 3. Vengeance on cooldown */
    if (!self->veng_active && remaining_ticks(self->veng_cooldown) == 0) {
        actions[HEAD_VENG] = VENG_CAST;
    }

    /* Tick-level action delay */
    if (opp_should_skip_offensive(env, opp)) return;

    /* 4. Attack: melee/range only (no mage — lunar spellbook) */
    if (opp_attack_ready(self) && !eating) {
        int allowed_mask = OPP_STYLE_MASK_RANGED | OPP_STYLE_MASK_MELEE;
        int preference_mask;
        if (rand_float(env) < opp->off_prayer_rate) {
            preference_mask = opp_get_off_prayer_mask(self, target) & allowed_mask;
            if (preference_mask == 0) preference_mask = allowed_mask;
        } else {
            preference_mask = opp_random_style_preference(env, allowed_mask);
        }
        int attack_style = opp_resolve_attack_style(
            env, opp, self, target, allowed_mask, preference_mask).style;

        /* Boost potions */
        opp_apply_boost_potion(env, opp, actions, self, attack_style, 0);

        /* Spec: melee spec only */
        int dist = chebyshev_distance(self->x, self->y, target->x, target->y);
        int can_spec_range = (self->frozen_ticks > 0) ? (dist <= 1) : (dist <= 3);
        int should_spec = (self->special_energy >= get_melee_spec_cost(self->melee_spec_weapon) &&
                          target->prayer != PRAYER_PROTECT_MELEE &&
                          can_spec_range);

        /* Anti-kite: cancel spec if target fleeing */
        if (should_spec && opp->target_fleeing_ticks >= 2 && dist > 1) {
            should_spec = 0;
        }

        if (should_spec) {
            opp_apply_gear_switch(actions, OPP_STYLE_SPEC);
            actions[HEAD_COMBAT] = ATTACK_ATK;
        } else {
            /* Gear switch — offensive_prayer_miss: skip switch to omit auto-prayer */
            if (rand_float(env) < opp->offensive_prayer_miss) {
                actions[HEAD_LOADOUT] = LOADOUT_KEEP;
            } else {
                opp_apply_gear_switch(actions, attack_style);
            }
            actions[HEAD_COMBAT] = ATTACK_ATK;
        }
    } else if (!opp_attack_ready(self)) {
        /* Movement: step under frozen target */
        int dist = chebyshev_distance(self->x, self->y, target->x, target->y);
        if (opp_target_frozen_after_pvp_timer_update(target) &&
            self->frozen_ticks == 0 && dist > 0 &&
            rand_float(env) < 0.40f) {
            actions[HEAD_COMBAT] = MOVE_UNDER;
        } else if (opp->target_fleeing_ticks >= 2 && dist > 3 && self->frozen_ticks == 0) {
            actions[HEAD_COMBAT] = MOVE_FARCAST_3;
        }
    }
}

static void opp_blood_healer(OsrsEnv* env, OpponentState* opp, int* actions) {
    Player* self = &env->players[1];
    Player* target = &env->players[0];
    float hp_pct = (float)self->current_hitpoints / (float)self->base_hitpoints;

    opp_tick_cooldowns(opp);
    OppConsumables cons = opp_get_consumables(opp, self);

    /* 1. Defensive prayer */
    opp_apply_defensive_prayer(
        env, opp, actions, self, target, OPP_DEF_PRAYER_TARGET_GEAR);

    /* 2. Reduced eating — relies on blood barrage for sustain above ~35%.
     * Emergency triple-eat below 35%, otherwise only brew/food below 25%. */
    int eating = opp_apply_survival_policy(
        opp, actions, self, cons, OPP_SURVIVAL_BLOOD_HEALER);

    if (opp_should_skip_offensive(env, opp)) return;

    /* 3. Attack: blood barrage emphasis for sustain */
    if (opp_attack_ready(self) && !eating) {
        int attack_style;
        int actual_attack;  /* 0=ice, 1=blood, 2=atk */

        if (hp_pct < 0.40f) {
            /* Low HP: blood barrage for heal + triple-eat */
            attack_style = opp_choose_style_from_preference(
                env, opp, self, target, OPP_STYLE_MASK_ALL, OPP_STYLE_MAGE);
            actual_attack = (attack_style == OPP_STYLE_MAGE) ? 1 : 2;
        } else if (hp_pct < 0.70f) {
            /* Medium HP: strongly prefer blood barrage (~80%) for sustain */
            if (rand_float(env) < 0.80f) {
                attack_style = opp_choose_style_from_preference(
                    env, opp, self, target, OPP_STYLE_MASK_ALL, OPP_STYLE_MAGE);
                actual_attack = (attack_style == OPP_STYLE_MAGE) ? 1 : 2;
            } else {
                attack_style = opp_choose_attack_style(
                    env, opp, self, target, OPP_STYLE_MASK_ALL);
                if (attack_style == OPP_STYLE_MAGE) {
                    /* Ice to freeze, not blood (already handled blood above) */
                    actual_attack = (target->frozen_ticks == 0 && target->freeze_immunity_ticks == 0)
                                    ? 0 : 1;  /* ice if can freeze, else blood */
                } else {
                    actual_attack = 2;  /* ATK */
                }
            }
        } else {
            /* High HP: normal off-prayer targeting with ice barrage for freeze */
            attack_style = opp_choose_attack_style(
                env, opp, self, target, OPP_STYLE_MASK_ALL);
            if (attack_style == OPP_STYLE_MAGE) {
                actual_attack = (target->frozen_ticks == 0 && target->freeze_immunity_ticks == 0)
                                ? 0 : 1;  /* ice if can freeze, else blood for sustain */
            } else {
                actual_attack = 2;  /* ATK */
            }
        }

        /* Apply boost potions */
        opp_apply_boost_potion(env, opp, actions, self, attack_style, 0);

        /* Gear switch — offensive_prayer_miss: skip switch to omit auto-prayer */
        if (rand_float(env) < opp->offensive_prayer_miss) {
            actions[HEAD_LOADOUT] = LOADOUT_KEEP;
        } else {
            opp_apply_gear_switch(actions, attack_style);
        }

        /* Tank gear when critically low and not casting blood */
        if (hp_pct < 0.35f && actual_attack != 1) {
            actions[HEAD_LOADOUT] = LOADOUT_TANK;
        }

        opp_emit_combat_attack(actions, actual_attack);
    } else if (!opp_attack_ready(self)) {
        /* Movement: maintain farcast-5 distance */
        int dist = chebyshev_distance(self->x, self->y, target->x, target->y);
        if (self->frozen_ticks == 0) {
            if (opp_target_frozen_after_pvp_timer_update(target) && dist < 5) {
                /* Step back to range 5 from frozen target */
                actions[HEAD_COMBAT] = MOVE_FARCAST_5;
            } else if (dist < 4 && target->frozen_ticks == 0) {
                /* Maintain distance from unfrozen target */
                actions[HEAD_COMBAT] = MOVE_FARCAST_5;
            } else if (opp->target_fleeing_ticks >= 2 && dist > 5) {
                /* Anti-kite: close to farcast-5 range */
                actions[HEAD_COMBAT] = MOVE_FARCAST_5;
            }
        }
    }
}

#define COMBO_IDLE       0
#define COMBO_SPEC_FIRED 1

static void opp_gmaul_combo(OsrsEnv* env, OpponentState* opp, int* actions) {
    Player* self = &env->players[1];
    Player* target = &env->players[0];
    float target_hp_pct = (float)target->current_hitpoints / (float)target->base_hitpoints;
    int has_gmaul = player_has_gmaul(self);

    opp_tick_cooldowns(opp);
    OppConsumables cons = opp_get_consumables(opp, self);

    /* 1. Defensive prayer */
    opp_apply_defensive_prayer(
        env, opp, actions, self, target, OPP_DEF_PRAYER_TARGET_GEAR);

    /* 2. Multi-threshold eating (same as improved) */
    int eating = opp_apply_survival_policy(
        opp, actions, self, cons, OPP_SURVIVAL_STANDARD);

    if (opp_should_skip_offensive(env, opp)) return;

    /* 3. Combo state machine: follow-up gmaul after spec fired */
    if (opp->combo_state == COMBO_SPEC_FIRED && has_gmaul && !eating) {
        /* Gmaul follow-up — instant spec, bypasses attack timer */
        actions[HEAD_LOADOUT] = LOADOUT_GMAUL;
        actions[HEAD_COMBAT] = ATTACK_ATK;
        opp->combo_state = COMBO_IDLE;
        return;
    }
    /* Reset combo if we ate (can't follow up) or don't have gmaul */
    opp->combo_state = COMBO_IDLE;

    /* 4. Attack decision */
    if (opp_attack_ready(self) && !eating) {
        int dist = chebyshev_distance(self->x, self->y, target->x, target->y);

        /* KO opportunity: target in KO range and we have enough spec energy */
        int melee_spec_cost = get_melee_spec_cost(self->melee_spec_weapon);
        int gmaul_cost = 50;  /* granite maul always 50% */
        int can_spec_range = (self->frozen_ticks > 0) ? (dist <= 1) : (dist <= 3);
        int should_combo = (has_gmaul &&
                           target_hp_pct < opp->ko_threshold &&
                           self->special_energy >= melee_spec_cost + gmaul_cost &&
                           target->prayer != PRAYER_PROTECT_MELEE &&
                           can_spec_range);

        /* Also check ranged spec for variety (no gmaul follow-up, just raw spec) */
        uint8_t ranged_spec = find_best_ranged_spec(self);
        int should_ranged_spec = (ranged_spec != 0 &&
                                 target_hp_pct < opp->ko_threshold &&
                                 self->special_energy >= get_ranged_spec_cost(self->ranged_spec_weapon) &&
                                 target->prayer != PRAYER_PROTECT_RANGED &&
                                 opp_loadout_can_hit_now(env, self, target, LOADOUT_SPEC_RANGE, OPP_STYLE_RANGED) &&
                                 rand_float(env) < 0.25f);  /* 25% chance to use ranged spec */

        /* Anti-kite: cancel melee combo if target fleeing */
        if ((should_combo || should_ranged_spec) &&
            opp->target_fleeing_ticks >= 2 && dist > 1) {
            should_combo = 0;
            should_ranged_spec = 0;
        }

        if (should_combo) {
            /* Fire melee spec → next tick gmaul follows */
            opp_apply_gear_switch(actions, OPP_STYLE_SPEC);
            actions[HEAD_COMBAT] = ATTACK_ATK;
            opp->combo_state = COMBO_SPEC_FIRED;
        } else if (should_ranged_spec) {
            /* Ranged spec (no gmaul follow-up) */
            actions[HEAD_LOADOUT] = LOADOUT_SPEC_RANGE;
            actions[HEAD_COMBAT] = ATTACK_ATK;
        } else {
            /* Normal improved-style play */
            int attack_style = opp_choose_attack_style(
                env, opp, self, target, OPP_STYLE_MASK_ALL);

            opp_apply_boost_potion(env, opp, actions, self, attack_style, 0);

            /* Regular melee spec (DDS at tier 0, better at higher tiers) — no gmaul combo */
            int should_regular_spec = (!has_gmaul &&
                                      self->special_energy >= melee_spec_cost &&
                                      target->prayer != PRAYER_PROTECT_MELEE &&
                                      target_hp_pct < 0.50f &&
                                      can_spec_range);
            if (should_regular_spec && opp->target_fleeing_ticks < 2) {
                opp_apply_gear_switch(actions, OPP_STYLE_SPEC);
                actions[HEAD_COMBAT] = ATTACK_ATK;
            } else {
                /* Gear switch — offensive_prayer_miss: skip switch to omit auto-prayer */
                if (rand_float(env) < opp->offensive_prayer_miss) {
                    actions[HEAD_LOADOUT] = LOADOUT_KEEP;
                } else {
                    opp_apply_gear_switch(actions, attack_style);
                }

                if (attack_style == OPP_STYLE_MAGE) {
                    actions[HEAD_COMBAT] = (target->frozen_ticks == 0 &&
                                           target->freeze_immunity_ticks == 0)
                                          ? ATTACK_ICE : ATTACK_BLOOD;
                } else {
                    actions[HEAD_COMBAT] = ATTACK_ATK;
                }
            }
        }
    } else if (!opp_attack_ready(self)) {
        /* Movement: step under frozen target, farcast-3 for anti-kite */
        int dist = chebyshev_distance(self->x, self->y, target->x, target->y);
        if (opp_target_frozen_after_pvp_timer_update(target) &&
                self->frozen_ticks == 0 && dist > 0) {
            actions[HEAD_COMBAT] = MOVE_UNDER;
        } else if (opp->target_fleeing_ticks >= 2 && dist > 3 && self->frozen_ticks == 0) {
            actions[HEAD_COMBAT] = MOVE_FARCAST_3;
        } else if (opp_should_fc3(self, target) && target->prayer != PRAYER_PROTECT_MELEE) {
            actions[HEAD_COMBAT] = MOVE_FARCAST_3;
        }
    }
}

static void opp_range_kiter(OsrsEnv* env, OpponentState* opp, int* actions) {
    Player* self = &env->players[1];
    Player* target = &env->players[0];
    float hp_pct = (float)self->current_hitpoints / (float)self->base_hitpoints;
    float target_hp_pct = (float)target->current_hitpoints / (float)target->base_hitpoints;
    int dist = chebyshev_distance(self->x, self->y, target->x, target->y);

    opp_tick_cooldowns(opp);
    OppConsumables cons = opp_get_consumables(opp, self);

    /* 1. Defensive prayer */
    opp_apply_defensive_prayer(
        env, opp, actions, self, target, OPP_DEF_PRAYER_TARGET_GEAR);

    /* 2. Multi-threshold eating + emergency blood barrage sustain */
    int eating = opp_apply_survival_policy(
        opp, actions, self, cons, OPP_SURVIVAL_STANDARD);

    if (opp_should_skip_offensive(env, opp)) return;

    /* 3. Attack: ranged-dominant with freeze support and ranged specs */
    if (opp_attack_ready(self) && !eating) {
        /* Check ranged spec availability */
        uint8_t ranged_spec = find_best_ranged_spec(self);
        int has_ranged_spec = (ranged_spec != 0);
        int ranged_spec_cost = has_ranged_spec
                               ? get_ranged_spec_cost(self->ranged_spec_weapon) : 100;

        /* Ranged spec: freeze → spec from distance is primary KO pattern */
        int should_ranged_spec = (has_ranged_spec &&
                                 self->special_energy >= ranged_spec_cost &&
                                 target->prayer != PRAYER_PROTECT_RANGED &&
                                 opp_loadout_can_hit_now(env, self, target, LOADOUT_SPEC_RANGE, OPP_STYLE_RANGED) &&
                                 target_hp_pct < 0.55f);

        /* Anti-kite not needed for ranged — we WANT distance */

        if (should_ranged_spec && (target->frozen_ticks > 0 || dist >= 3)) {
            /* Fire ranged spec from distance */
            actions[HEAD_LOADOUT] = LOADOUT_SPEC_RANGE;
            actions[HEAD_COMBAT] = ATTACK_ATK;
        } else {
            /* Style selection: ranged-biased via style_bias (initialized with range preference)
             * + force ranged at distance, force melee only when adjacent and frozen */
            int attack_style;
            int force_melee = (self->frozen_ticks > 0 && dist <= 1);
            int prefer_ranged = (dist >= 3 || target->frozen_ticks > 0);

            if (force_melee) {
                attack_style = opp_choose_style_from_preference(
                    env, opp, self, target, OPP_STYLE_MASK_ALL, OPP_STYLE_MELEE);
            } else if (prefer_ranged && rand_float(env) < 0.80f) {
                attack_style = opp_choose_style_from_preference(
                    env, opp, self, target, OPP_STYLE_MASK_ALL, OPP_STYLE_RANGED);
            } else {
                attack_style = opp_choose_attack_style(
                    env, opp, self, target, OPP_STYLE_MASK_ALL);
            }

            /* Emergency blood barrage healing */
            int actual_attack;
            if (hp_pct < 0.30f && attack_style == OPP_STYLE_MAGE) {
                actual_attack = 1;  /* blood */
            } else if (attack_style == OPP_STYLE_MAGE) {
                actual_attack = (target->frozen_ticks == 0 &&
                                target->freeze_immunity_ticks == 0)
                               ? 0 : 2;  /* ice if can freeze, else just ATK (ranged fallback) */
                if (actual_attack == 2) {
                    attack_style = opp_choose_style_from_preference(
                        env, opp, self, target, OPP_STYLE_MASK_ALL, OPP_STYLE_RANGED);
                    if (attack_style == OPP_STYLE_MAGE) {
                        actual_attack = 1;
                    }
                }
            } else {
                actual_attack = 2;  /* ATK */
            }

            opp_apply_boost_potion(env, opp, actions, self, attack_style, 0);

            int melee_spec_cost = get_melee_spec_cost(self->melee_spec_weapon);
            int can_melee_spec = (self->special_energy >= melee_spec_cost &&
                                 target->prayer != PRAYER_PROTECT_MELEE &&
                                 dist <= 1 && self->frozen_ticks == 0);
            if (can_melee_spec && target_hp_pct < 0.40f && !has_ranged_spec) {
                opp_apply_gear_switch(actions, OPP_STYLE_SPEC);
                actions[HEAD_COMBAT] = ATTACK_ATK;
            } else {
                /* Gear switch — offensive_prayer_miss: skip switch to omit auto-prayer */
                if (rand_float(env) < opp->offensive_prayer_miss) {
                    actions[HEAD_LOADOUT] = LOADOUT_KEEP;
                } else {
                    opp_apply_gear_switch(actions, attack_style);
                }

                opp_emit_combat_attack(actions, actual_attack);
            }
        }
    } else if (!opp_attack_ready(self)) {
        /* Movement: maintain farcast-5, step back after freeze */
        if (self->frozen_ticks == 0) {
            if (opp_target_frozen_after_pvp_timer_update(target) && dist < 5) {
                /* Step back to range 5 from frozen target */
                actions[HEAD_COMBAT] = MOVE_FARCAST_5;
            } else if (dist < 4) {
                /* Maintain distance from approaching target */
                actions[HEAD_COMBAT] = MOVE_FARCAST_5;
            } else if (dist > 7) {
                /* Don't let them get too far — close to farcast-5 */
                actions[HEAD_COMBAT] = MOVE_FARCAST_5;
            }
        }
    }
}

static void opp_strict_kiter(OsrsEnv* env, OpponentState* opp, int* actions) {
    Player* self = &env->players[1];
    Player* target = &env->players[0];
    int dist = chebyshev_distance(self->x, self->y, target->x, target->y);

    opp_tick_cooldowns(opp);
    opp_read_agent_action(env, opp);
    OppConsumables cons = opp_get_consumables(opp, self);
    opp_apply_defensive_prayer(
        env, opp, actions, self, target, OPP_DEF_PRAYER_TARGET_GEAR_WITH_SPEC);
    int eating = opp_apply_survival_policy(
        opp, actions, self, cons, OPP_SURVIVAL_STANDARD);

    if (self->frozen_ticks == 0 && dist <= 1) {
        actions[HEAD_COMBAT] = opp_target_frozen_after_pvp_timer_update(target) && dist > 0
            ? MOVE_UNDER
            : MOVE_FARCAST_5;
        return;
    }

    if (opp_should_skip_offensive(env, opp)) return;

    if (!opp_attack_ready(self) || eating) {
        if (self->frozen_ticks == 0) {
            if (opp_target_frozen_after_pvp_timer_update(target) && dist < 5) {
                actions[HEAD_COMBAT] = MOVE_FARCAST_5;
            } else if (dist > 7) {
                actions[HEAD_COMBAT] = MOVE_FARCAST_5;
            }
        }
        return;
    }

    int actual_style;
    int actual_attack;
    if (target->frozen_ticks == 0 &&
            target->freeze_immunity_ticks == 0 &&
            opp_style_can_hit_now(env, self, target, OPP_STYLE_MAGE)) {
        actual_style = OPP_STYLE_MAGE;
        actual_attack = 0;
    } else {
        int allowed = OPP_STYLE_MASK_MAGE | OPP_STYLE_MASK_RANGED;
        if (dist <= 1 && target->prayer != PRAYER_PROTECT_MELEE) {
            allowed |= OPP_STYLE_MASK_MELEE;
        }
        OppStyleChoice choice = opp_resolve_attack_style(
            env, opp, self, target, allowed, opp_get_off_prayer_mask(self, target));
        actual_style = choice.style;
        actual_attack = actual_style == OPP_STYLE_MAGE
            ? (opp_get_mage_attack(self, target) == ATTACK_ICE ? 0 : 1)
            : 2;
    }

    opp_apply_boost_potion(env, opp, actions, self, actual_style, 0);
    opp_emit_attack_with_style(env, opp, actions, actual_style, actual_attack);
}

static inline int pvp_recent_attack_style_count(
    const AttackStyle attacks[HISTORY_SIZE],
    AttackStyle style
) {
    int count = 0;
    for (int i = 0; i < HISTORY_SIZE; i++) {
        if (attacks[i] == style) count++;
    }
    return count;
}

static inline void pvp_adaptive_nh_update_memory(
    OpponentState* opp,
    const Player* self,
    const Player* target
) {
    int adjacent = chebyshev_distance(self->x, self->y, target->x, target->y) <= 1;
    int mage_camp =
        target->visible_gear == GEAR_MAGE &&
        target->prayer == PRAYER_PROTECT_MAGIC &&
        adjacent;
    int melee_threat =
        target->last_attack_style == ATTACK_STYLE_MELEE ||
        target->attack_style_this_tick == ATTACK_STYLE_MELEE;

    if (mage_camp) {
        opp->adaptive_mage_camp_ticks++;
    } else if (opp->adaptive_mage_camp_ticks > 0) {
        opp->adaptive_mage_camp_ticks--;
    }

    if (melee_threat) {
        opp->adaptive_melee_threat_ticks = 16;
    } else if (opp->adaptive_melee_threat_ticks > 0) {
        opp->adaptive_melee_threat_ticks--;
    }
}

static inline PvpMageCampMeleeSignal pvp_mage_camp_melee_signal(
    const OpponentState* opp,
    const Player* self,
    const Player* target
) {
    PvpMageCampMeleeSignal out;
    out.presents_mage = target->visible_gear == GEAR_MAGE;
    out.protects_magic = target->prayer == PRAYER_PROTECT_MAGIC;
    out.adjacent = chebyshev_distance(self->x, self->y, target->x, target->y) <= 1;
    out.melee_attack_resolved =
        target->last_attack_style == ATTACK_STYLE_MELEE ||
        target->attack_style_this_tick == ATTACK_STYLE_MELEE;
    out.melee_recent_count = pvp_recent_attack_style_count(
        self->recent_target_attack_styles,
        ATTACK_STYLE_MELEE);
    out.mage_camp_ticks = opp->adaptive_mage_camp_ticks;
    out.melee_threat_ticks = opp->adaptive_melee_threat_ticks;
    out.attack_ready_soon = remaining_ticks(target->attack_timer) <= 1;
    return out;
}

static inline int pvp_should_counter_mage_camp_melee(PvpMageCampMeleeSignal s) {
    return s.presents_mage &&
        s.protects_magic &&
        s.adjacent &&
        (s.melee_attack_resolved ||
         s.melee_recent_count >= 1 ||
         s.melee_threat_ticks > 0 ||
         s.mage_camp_ticks >= 4);
}

static inline int pvp_should_pray_melee_against_mage_camp(PvpMageCampMeleeSignal s) {
    return pvp_should_counter_mage_camp_melee(s) &&
        (s.attack_ready_soon ||
         s.melee_recent_count >= 1 ||
         s.melee_threat_ticks > 0 ||
         s.mage_camp_ticks >= 4);
}

static inline void pvp_adaptive_nh_apply_defensive_prayer(
    const OpponentState* opp,
    int* actions,
    Player* self,
    Player* target,
    PvpMageCampMeleeSignal signal
) {
    int prayer = -1;
    if (opp->has_read_this_tick && opp->read_agent_style != ATTACK_STYLE_NONE) {
        prayer = opp_get_read_defensive_prayer(opp);
    }
    if (prayer < 0) {
        prayer = pvp_should_pray_melee_against_mage_camp(signal)
            ? OVERHEAD_MELEE
            : opp_get_defensive_prayer_with_spec(target);
    }
    if (!opp_has_prayer_active(self, prayer)) {
        opp_emit_prayer(actions, self, prayer);
    }
}

static inline MeleeSpecWeapon pvp_adaptive_nh_melee_spec_for_item(uint8_t item) {
    switch (item) {
        case ITEM_AGS:              return MELEE_SPEC_AGS;
        case ITEM_DRAGON_CLAWS:     return MELEE_SPEC_DRAGON_CLAWS;
        case ITEM_GRANITE_MAUL:     return MELEE_SPEC_GRANITE_MAUL;
        case ITEM_DRAGON_DAGGER:    return MELEE_SPEC_DRAGON_DAGGER;
        case ITEM_VOIDWAKER:        return MELEE_SPEC_VOIDWAKER;
        case ITEM_STATIUS_WARHAMMER:return MELEE_SPEC_DWH;
        case ITEM_ANCIENT_GS:       return MELEE_SPEC_ANCIENT_GS;
        case ITEM_VESTAS:           return MELEE_SPEC_VESTAS;
        default:                    return MELEE_SPEC_NONE;
    }
}

static inline void pvp_adaptive_nh_apply_counter_attack(
    OsrsEnv* env,
    OpponentState* opp,
    int* actions,
    Player* self,
    Player* target,
    PvpMageCampMeleeSignal signal
) {
    if (!pvp_should_counter_mage_camp_melee(signal)) return;
    if (!opp_attack_ready(self)) return;
    if (opp_check_eating_queued(actions)) return;

    uint8_t melee_spec_item = find_best_melee_spec(self);
    MeleeSpecWeapon melee_spec = pvp_adaptive_nh_melee_spec_for_item(melee_spec_item);
    if (target->prayer != PRAYER_PROTECT_MELEE &&
            melee_spec != MELEE_SPEC_NONE &&
            self->special_energy >= get_melee_spec_cost(melee_spec) &&
            opp_loadout_can_hit_now(env, self, target, LOADOUT_SPEC_MELEE, OPP_STYLE_SPEC)) {
        actions[HEAD_LOADOUT] = LOADOUT_SPEC_MELEE;
        actions[HEAD_COMBAT] = ATTACK_ATK;
        return;
    }

    if (target->frozen_ticks == 0 &&
            target->freeze_immunity_ticks == 0 &&
            opp_style_can_hit_now(env, self, target, OPP_STYLE_MAGE)) {
        opp_emit_attack_with_style(env, opp, actions, OPP_STYLE_MAGE, 0);
        return;
    }

    if (signal.adjacent && self->frozen_ticks == 0 && target->frozen_ticks == 0) {
        actions[HEAD_COMBAT] = MOVE_FARCAST_5;
        return;
    }

    if (target->prayer != PRAYER_PROTECT_MELEE &&
            opp_style_can_hit_now(env, self, target, OPP_STYLE_MELEE)) {
        opp_emit_attack_with_style(env, opp, actions, OPP_STYLE_MELEE, 2);
        return;
    }

    if (target->prayer != PRAYER_PROTECT_RANGED &&
            opp_style_can_hit_now(env, self, target, OPP_STYLE_RANGED)) {
        opp_emit_attack_with_style(env, opp, actions, OPP_STYLE_RANGED, 2);
    }
}

static void opp_adaptive_nh(OsrsEnv* env, OpponentState* opp, int* actions) {
    Player* self = &env->players[1];
    Player* target = &env->players[0];
    pvp_adaptive_nh_update_memory(opp, self, target);
    PvpMageCampMeleeSignal signal = pvp_mage_camp_melee_signal(opp, self, target);
    int dist = chebyshev_distance(self->x, self->y, target->x, target->y);
    int counter_camp = pvp_should_counter_mage_camp_melee(signal);
    float base_read_chance = opp->read_chance;
    float base_triple_threshold = opp->eat_triple_threshold;
    float base_double_threshold = opp->eat_double_threshold;
    float base_brew_threshold = opp->eat_brew_threshold;

    if (counter_camp) {
        opp->read_chance = 1.0f;
        if (opp->eat_triple_threshold < 0.45f) opp->eat_triple_threshold = 0.45f;
        if (opp->eat_double_threshold < 0.65f) opp->eat_double_threshold = 0.65f;
        if (opp->eat_brew_threshold < 0.88f) opp->eat_brew_threshold = 0.88f;
    }
    opp_nightmare_nh(env, opp, actions);
    opp->read_chance = base_read_chance;
    opp->eat_triple_threshold = base_triple_threshold;
    opp->eat_double_threshold = base_double_threshold;
    opp->eat_brew_threshold = base_brew_threshold;

    pvp_adaptive_nh_apply_defensive_prayer(opp, actions, self, target, signal);
    pvp_adaptive_nh_apply_counter_attack(env, opp, actions, self, target, signal);
    if (!opp_attack_ready(self) &&
            counter_camp &&
            dist <= 1 &&
            self->frozen_ticks == 0) {
        actions[HEAD_COMBAT] = MOVE_FARCAST_5;
    }
}

/* MixedEasy weights: panicking=0.18, true_random=0.18, weak_random=0.18,
   semi_random=0.15, sticky_prayer=0.10, random_eater=0.10, prayer_rookie=0.06,
   improved=0.05 */
static const OpponentType MIXED_EASY_POOL[] = {
    OPP_PANICKING, OPP_TRUE_RANDOM, OPP_WEAK_RANDOM, OPP_SEMI_RANDOM,
    OPP_STICKY_PRAYER, OPP_RANDOM_EATER, OPP_PRAYER_ROOKIE, OPP_IMPROVED,
};
/* Cumulative weights * 100 for integer comparison */
static const int MIXED_EASY_CUM_WEIGHTS[] = {18, 36, 54, 69, 79, 89, 95, 100};
#define MIXED_EASY_POOL_SIZE 8

/* MixedMedium weights: random_eater=0.25, prayer_rookie=0.20, sticky_prayer=0.20,
   semi_random=0.15, improved=0.10, (patient deferred to Python) */
static const OpponentType MIXED_MEDIUM_POOL[] = {
    OPP_RANDOM_EATER, OPP_PRAYER_ROOKIE, OPP_STICKY_PRAYER,
    OPP_SEMI_RANDOM, OPP_IMPROVED,
};
static const int MIXED_MEDIUM_CUM_WEIGHTS[] = {25, 45, 65, 80, 100};
#define MIXED_MEDIUM_POOL_SIZE 5

/* MixedHard: uniform over 5 policies (20% each) */
static const OpponentType MIXED_HARD_POOL[] = {
    OPP_IMPROVED, OPP_ONETICK, OPP_UNPREDICTABLE_IMPROVED,
    OPP_UNPREDICTABLE_ONETICK, OPP_RANDOM_EATER,
};
static const int MIXED_HARD_CUM_WEIGHTS[] = {20, 40, 60, 80, 100};
#define MIXED_HARD_POOL_SIZE 5

/* MixedHardBalanced: random_eater=25%, improved=30%, unpredictable_improved=20%,
   onetick=15%, unpredictable_onetick=10% */
static const OpponentType MIXED_HARD_BALANCED_POOL[] = {
    OPP_RANDOM_EATER, OPP_IMPROVED, OPP_UNPREDICTABLE_IMPROVED,
    OPP_ONETICK, OPP_UNPREDICTABLE_ONETICK,
};
static const int MIXED_HARD_BALANCED_CUM_WEIGHTS[] = {25, 55, 75, 90, 100};
#define MIXED_HARD_BALANCED_POOL_SIZE 5

static OpponentType opp_select_from_pool(
    OsrsEnv* env, const OpponentType* pool, const int* cum_weights, int pool_size
) {
    int r = rand_int(env, 100);
    for (int i = 0; i < pool_size; i++) {
        if (r < cum_weights[i]) return pool[i];
    }
    return pool[pool_size - 1];
}

static void opponent_reset(OsrsEnv* env, OpponentState* opp) {
    opp->food_cooldown = 0;
    opp->potion_cooldown = 0;
    opp->karambwan_cooldown = 0;
    opp->current_prayer_set = 0;

    opp->fake_switch_pending = 0;
    opp->fake_switch_style = -1;
    opp->opponent_prayer_at_fake = -1;
    opp->fake_switch_failed = 0;
    opp->pending_prayer_value = 0;
    opp->pending_prayer_delay = 0;
    opp->last_target_gear_style = -1;

    /* Per-episode eating thresholds with noise */
    opp->eat_triple_threshold = 0.30f + (rand_float(env) * 0.10f - 0.05f);
    opp->eat_double_threshold = 0.50f + (rand_float(env) * 0.10f - 0.05f);
    opp->eat_brew_threshold   = 0.70f + (rand_float(env) * 0.10f - 0.05f);

    /* Boss opponent reading ability — reset per-tick state */
    opp->has_read_this_tick = 0;
    opp->read_agent_style = ATTACK_STYLE_NONE;
    opp->read_agent_prayer = PRAYER_NONE;
    opp->read_chance = 0.0f;
    opp->read_agent_moving = 0;
    opp->prev_dist_to_target = 0;
    opp->target_fleeing_ticks = 0;
    opp->adaptive_mage_camp_ticks = 0;
    opp->adaptive_melee_threat_ticks = 0;

    /* Per-episode resets for specific policies */
    if (opp->type == OPP_PANICKING) {
        opp->chosen_prayer = opp_pick_random_overhead(env);
        opp->chosen_style = rand_int(env, 3);
    }

    /* Mixed policies: select sub-policy */
    if (opp->type == OPP_MIXED_EASY) {
        opp->active_sub_policy = opp_select_from_pool(
            env, MIXED_EASY_POOL, MIXED_EASY_CUM_WEIGHTS, MIXED_EASY_POOL_SIZE);
    } else if (opp->type == OPP_MIXED_MEDIUM) {
        opp->active_sub_policy = opp_select_from_pool(
            env, MIXED_MEDIUM_POOL, MIXED_MEDIUM_CUM_WEIGHTS, MIXED_MEDIUM_POOL_SIZE);
    } else if (opp->type == OPP_MIXED_HARD) {
        opp->active_sub_policy = opp_select_from_pool(
            env, MIXED_HARD_POOL, MIXED_HARD_CUM_WEIGHTS, MIXED_HARD_POOL_SIZE);
    } else if (opp->type == OPP_MIXED_HARD_BALANCED) {
        opp->active_sub_policy = opp_select_from_pool(
            env, MIXED_HARD_BALANCED_POOL, MIXED_HARD_BALANCED_CUM_WEIGHTS,
            MIXED_HARD_BALANCED_POOL_SIZE);
    } else if (opp->type == OPP_PFSP && env->pvp_runtime.pfsp.pool_size > 0) {
        int idx = 0;
        int r = rand_int(env, 1000);
        for (int i = 0; i < env->pvp_runtime.pfsp.pool_size; i++) {
            if (r < env->pvp_runtime.pfsp.cum_weights[i]) { idx = i; break; }
        }
        env->pvp_runtime.pfsp.active_pool_idx = idx;
        opp->active_sub_policy = env->pvp_runtime.pfsp.pool[idx];

        // Toggle opponent mode: selfplay uses external Python actions,
        // scripted opponents use C-generated actions
        if (opp->active_sub_policy == OPP_SELFPLAY) {
            env->pvp_runtime.use_c_opponent = 0;
            env->pvp_runtime.use_external_opponent_actions = 1;
            if (env->ocean_io.selfplay_mask) *env->ocean_io.selfplay_mask = 1;
        } else {
            env->pvp_runtime.use_c_opponent = 1;
            env->pvp_runtime.use_external_opponent_actions = 0;
            if (env->ocean_io.selfplay_mask) *env->ocean_io.selfplay_mask = 0;
        }
    } else if (opp->type == OPP_PFSP) {
        // PFSP pool not yet configured (set_pfsp_weights called after env creation).
        // Fall back to OPP_IMPROVED so the first episode isn't against a no-op opponent.
        opp->active_sub_policy = OPP_IMPROVED;
        env->pvp_runtime.pfsp.active_pool_idx = -1;  // sentinel: don't track in PFSP stats
    }

    /* Per-episode randomized decision parameters — resolved from sub-policy
     * so PFSP and mixed pools get the correct ranges. */
    OpponentType resolved = opp->active_sub_policy ? opp->active_sub_policy : opp->type;
    if (resolved > 0 && resolved <= OPP_STRICT_KITER &&
            resolved != OPP_SELFPLAY) {
        const OpponentRandRanges* r = &OPP_RAND_RANGES[resolved];
        opp->prayer_accuracy = rand_range(env, r->prayer_accuracy);
        opp->off_prayer_rate = rand_range(env, r->off_prayer_rate);
        opp->offensive_prayer_rate = rand_range(env, r->offensive_prayer_rate);
        opp->action_delay_chance = rand_range(env, r->action_delay_chance);
        opp->mistake_rate = rand_range(env, r->mistake_rate);
        opp->offensive_prayer_miss = rand_range(env, r->offensive_prayer_miss);
    }

    /* Boss reading ability */
    if (resolved == OPP_MASTER_NH) {
        opp->read_chance = 0.10f;
    } else if (resolved == OPP_SAVANT_NH) {
        opp->read_chance = 0.25f;
    } else if (resolved == OPP_NIGHTMARE_NH) {
        opp->read_chance = 0.50f;
    } else if (resolved == OPP_ADAPTIVE_NH) {
        opp->read_chance = 0.75f;
    } else if (resolved == OPP_STRICT_KITER) {
        opp->read_chance = 0.50f;
    }

    /* Vengeance fighter: lunar spellbook (no freeze/blood, has veng) */
    if (resolved == OPP_VENG_FIGHTER) {
        env->players[1].is_lunar_spellbook = 1;
    }

    /* Per-episode style bias: weighted preference for mage/ranged/melee.
     * Sampled for improved+ opponents that use off-prayer targeting. */
    if (resolved == OPP_IMPROVED || resolved == OPP_ONETICK ||
        resolved == OPP_UNPREDICTABLE_IMPROVED || resolved == OPP_UNPREDICTABLE_ONETICK ||
        (resolved >= OPP_ADVANCED_NH && resolved <= OPP_NIGHTMARE_NH) ||
        resolved == OPP_BLOOD_HEALER || resolved == OPP_GMAUL_COMBO ||
        resolved == OPP_RANGE_KITER || resolved == OPP_ADAPTIVE_NH ||
        resolved == OPP_STRICT_KITER) {
        float raw[3];
        for (int i = 0; i < 3; i++) raw[i] = 0.33f + (rand_float(env) - 0.5f) * 0.4f;
        float sum = raw[0] + raw[1] + raw[2];
        for (int i = 0; i < 3; i++) opp->style_bias[i] = raw[i] / sum;
    } else {
        opp->style_bias[0] = opp->style_bias[1] = opp->style_bias[2] = 0.333f;
    }

    /* gmaul_combo: per-episode KO threshold + combo state reset */
    if (resolved == OPP_GMAUL_COMBO) {
        opp->combo_state = 0;
        opp->ko_threshold = 0.45f + rand_float(env) * 0.15f;  /* 45-60% */
    }
}

static void pvp_legacy_loadout_to_slotclicks(Player* p, int legacy_loadout, int* actions) {
    if (legacy_loadout <= LOADOUT_KEEP || legacy_loadout > LOADOUT_GMAUL) return;

    uint8_t resolved[NUM_DYNAMIC_GEAR_SLOTS];
    resolve_loadout(p, legacy_loadout, resolved);

    int click_head = 0;
    for (int i = 0; i < NUM_DYNAMIC_GEAR_SLOTS && click_head < PVP_EQUIP_CLICKS_PER_TICK; i++) {
        uint8_t item = resolved[i];
        if (item == ITEM_NONE) continue;

        int gear_slot = DYNAMIC_GEAR_SLOTS[i];
        if (p->equipped[gear_slot] == item) continue;

        int inventory_slot = osrs_player_inventory_find(p, item);
        if (inventory_slot < 0) continue;

        actions[HEAD_EQUIP_0 + click_head] = inventory_slot + 1;
        click_head++;
    }

    if (legacy_loadout == LOADOUT_SPEC_MELEE ||
            legacy_loadout == LOADOUT_SPEC_RANGE ||
            legacy_loadout == LOADOUT_SPEC_MAGIC ||
            legacy_loadout == LOADOUT_GMAUL) {
        actions[HEAD_SPECIAL] = SPECIAL_ARM;
    }
}

static void pvp_translate_legacy_loadout_action_to_slotclicks(
    OsrsEnv* env,
    int agent_idx,
    int* actions
) {
    int legacy_loadout = actions[HEAD_LOADOUT];
    int legacy_combat = actions[HEAD_COMBAT];

    for (int h = 0; h < PVP_EQUIP_CLICKS_PER_TICK; h++) {
        actions[HEAD_EQUIP_0 + h] = 0;
    }
    actions[HEAD_SPECIAL] = SPECIAL_NOOP;
    actions[HEAD_ATTACK] = ATTACK_NONE;

    pvp_legacy_loadout_to_slotclicks(&env->players[agent_idx], legacy_loadout, actions);
    if (is_attack_action(legacy_combat)) {
        actions[HEAD_ATTACK] = legacy_combat;
        return;
    }
    if (!is_move_action(legacy_combat)) return;

    actions[HEAD_MOVE] = MOVE_NONE;
    const CollisionMap* cmap = (const CollisionMap*)env->collision_map;
    int dest_x = -1;
    int dest_y = -1;
    Player* self = &env->players[agent_idx];
    Player* target = &env->players[1 - agent_idx];
    if (!pvp_select_target_move_destination(
            self,
            target->x,
            target->y,
            legacy_combat,
            cmap,
            &dest_x,
            &dest_y)) {
        return;
    }

    int head_move = pvp_exact_head_move_toward_tile(
        self, dest_x, dest_y);
    if (head_move != MOVE_NONE) {
        actions[HEAD_MOVE] = head_move;
        return;
    }

    env->pvp_runtime.walk_dest_x[agent_idx] = dest_x;
    env->pvp_runtime.walk_dest_y[agent_idx] = dest_y;
}

static inline int opp_type_uses_hard_spacing_guard(OpponentType type) {
    return (type >= OPP_ADVANCED_NH && type <= OPP_NIGHTMARE_NH) ||
        type == OPP_BLOOD_HEALER ||
        type == OPP_GMAUL_COMBO ||
        type == OPP_RANGE_KITER ||
        type == OPP_ADAPTIVE_NH ||
        type == OPP_STRICT_KITER;
}

typedef enum {
    OPP_STEP_OUT_DUMB,
    OPP_STEP_OUT_SMART,
} OppStepOutMode;

typedef enum {
    OPP_SPACING_KEEP,
    OPP_SPACING_LEGACY_MOVE,
    OPP_SPACING_HEAD_MOVE,
} OppSpacingDecisionKind;

typedef struct {
    OppSpacingDecisionKind kind;
    int move;
} OppSpacingDecision;

static inline OppSpacingDecision opp_spacing_keep(void) {
    return (OppSpacingDecision){.kind = OPP_SPACING_KEEP, .move = 0};
}

static inline OppSpacingDecision opp_spacing_legacy_move(int legacy_move) {
    return (OppSpacingDecision){.kind = OPP_SPACING_LEGACY_MOVE, .move = legacy_move};
}

static inline OppSpacingDecision opp_spacing_head_move(int head_move) {
    return (OppSpacingDecision){.kind = OPP_SPACING_HEAD_MOVE, .move = head_move};
}

static inline OppStepOutMode opp_step_out_mode_for_type(OpponentType type) {
    switch (type) {
        case OPP_MASTER_NH:
        case OPP_SAVANT_NH:
        case OPP_NIGHTMARE_NH:
        case OPP_RANGE_KITER:
        case OPP_ADAPTIVE_NH:
        case OPP_STRICT_KITER:
            return OPP_STEP_OUT_SMART;
        default:
            return OPP_STEP_OUT_DUMB;
    }
}

static inline int opp_target_click_style_from_weapon(uint8_t weapon) {
    int weapon_style = get_item_attack_style(weapon);
    if (weapon_style == 2) return OPP_STYLE_RANGED;
    return OPP_STYLE_MELEE;
}

static inline int opp_legacy_attack_style_for_actions(Player* self, int* actions) {
    int legacy_combat = actions[HEAD_COMBAT];
    if (legacy_combat == ATTACK_ICE || legacy_combat == ATTACK_BLOOD) {
        return OPP_STYLE_MAGE;
    }
    if (legacy_combat != ATTACK_ATK) {
        return -1;
    }

    int legacy_loadout = actions[HEAD_LOADOUT];
    switch (legacy_loadout) {
        case LOADOUT_RANGE:
        case LOADOUT_SPEC_RANGE:
            return OPP_STYLE_RANGED;
        case LOADOUT_SPEC_MAGIC:
            return OPP_STYLE_MAGE;
        case LOADOUT_MELEE:
        case LOADOUT_MAGE:
        case LOADOUT_TANK:
        case LOADOUT_SPEC_MELEE:
        case LOADOUT_GMAUL:
            return OPP_STYLE_MELEE;
        case LOADOUT_KEEP:
            return opp_target_click_style_from_weapon(self->equipped[GEAR_SLOT_WEAPON]);
        default:
            return -1;
    }
}

static inline AttackStyle opp_attack_style_from_internal(int style) {
    switch (style) {
        case OPP_STYLE_MAGE: return ATTACK_STYLE_MAGIC;
        case OPP_STYLE_RANGED: return ATTACK_STYLE_RANGED;
        case OPP_STYLE_MELEE: return ATTACK_STYLE_MELEE;
        default:
            fprintf(stderr, "invalid opponent attack style: %d\n", style);
            abort();
    }
}

static inline int opp_smart_step_candidate_can_projectile_attack(
    OsrsEnv* env,
    AttackStyle style,
    int legacy_loadout,
    int dest_x,
    int dest_y
) {
    const CollisionMap* cmap = (const CollisionMap*)env->collision_map;
    Player* self = &env->players[1];
    Player* target = &env->players[0];
    if (dest_x == target->x && dest_y == target->y) return 0;
    if (abs_int(dest_x - target->x) + abs_int(dest_y - target->y) == 1)
        return 0;
    if (!pvp_destination_reachable_this_tick(self, dest_x, dest_y, cmap))
        return 0;

    Player probe = *self;
    if (legacy_loadout > LOADOUT_KEEP && legacy_loadout <= LOADOUT_GMAUL)
        apply_loadout(&probe, legacy_loadout);
    probe.x = dest_x;
    probe.y = dest_y;
    probe.dest_x = dest_x;
    probe.dest_y = dest_y;
    OsrsAttackReachQuery reach =
        pvp_attack_reach_query(cmap, &probe, target, style);
    return osrs_attack_can_reach(&reach);
}

static inline OppSpacingDecision opp_select_smart_projectile_step_out(
    OsrsEnv* env,
    AttackStyle style,
    int legacy_loadout,
    const int offsets[][2],
    int offset_count
) {
    Player* self = &env->players[1];
    Player* target = &env->players[0];
    int has_best = 0;
    int best_move = MOVE_NONE;
    int best_dist = 0;
    int best_hash = 0;

    for (int i = 0; i < offset_count; i++) {
        int dest_x = target->x + offsets[i][0];
        int dest_y = target->y + offsets[i][1];
        if (!opp_smart_step_candidate_can_projectile_attack(
                env, style, legacy_loadout, dest_x, dest_y)) {
            continue;
        }
        int head_move = pvp_head_move_toward_tile(self, dest_x, dest_y);
        if (head_move == MOVE_NONE) continue;
        int dist = chebyshev_distance(self->x, self->y, dest_x, dest_y);
        int hash = tile_hash(dest_x, dest_y);
        if (!has_best || dist < best_dist ||
                (dist == best_dist && hash < best_hash)) {
            has_best = 1;
            best_move = head_move;
            best_dist = dist;
            best_hash = hash;
        }
    }

    return has_best ? opp_spacing_head_move(best_move) : opp_spacing_keep();
}

static inline OppSpacingDecision opp_smart_projectile_spacing_decision(
    OsrsEnv* env,
    AttackStyle style,
    int legacy_loadout
) {
    Player* target = &env->players[0];
    Player* self = &env->players[1];
    const CollisionMap* cmap = (const CollisionMap*)env->collision_map;

    if (target->frozen_ticks <= 1) {
        int dest_x = -1;
        int dest_y = -1;
        if (select_farcast_tile(self, target->x, target->y, 5, &dest_x, &dest_y, cmap)) {
            int move = pvp_head_move_toward_tile(self, dest_x, dest_y);
            if (move != MOVE_NONE) return opp_spacing_head_move(move);
        }
        return opp_spacing_legacy_move(MOVE_FARCAST_5);
    }

    static const int diagonal_offsets[4][2] = {
        {1, 1}, {1, -1}, {-1, -1}, {-1, 1}
    };
    OppSpacingDecision diagonal =
        opp_select_smart_projectile_step_out(
            env, style, legacy_loadout, diagonal_offsets, 4);
    if (diagonal.kind != OPP_SPACING_KEEP) return diagonal;

    int farcast_offsets[16][2];
    int count = 0;
    for (int dx = -2; dx <= 2; dx++) {
        for (int dy = -2; dy <= 2; dy++) {
            if (max_int(abs_int(dx), abs_int(dy)) != 2) continue;
            farcast_offsets[count][0] = dx;
            farcast_offsets[count][1] = dy;
            count++;
        }
    }

    OppSpacingDecision farcast =
        opp_select_smart_projectile_step_out(
            env, style, legacy_loadout, farcast_offsets, count);
    if (farcast.kind != OPP_SPACING_KEEP) return farcast;

    int dest_x = -1;
    int dest_y = -1;
    if (select_farcast_tile(self, target->x, target->y, 5, &dest_x, &dest_y, cmap)) {
        int move = pvp_head_move_toward_tile(self, dest_x, dest_y);
        if (move != MOVE_NONE) return opp_spacing_head_move(move);
    }
    return opp_spacing_legacy_move(MOVE_FARCAST_5);
}

static inline OppSpacingDecision opp_dumb_projectile_spacing_decision(
    Player* target,
    int dist
) {
    return opp_target_frozen_after_pvp_timer_update(target) && dist > 0
        ? opp_spacing_legacy_move(MOVE_UNDER)
        : opp_spacing_legacy_move(MOVE_FARCAST_5);
}

static inline void opp_apply_spacing_decision(
    int* actions,
    OppSpacingDecision decision
) {
    switch (decision.kind) {
        case OPP_SPACING_KEEP:
            return;
        case OPP_SPACING_LEGACY_MOVE:
            actions[HEAD_MOVE] = MOVE_NONE;
            actions[HEAD_COMBAT] = decision.move;
            return;
        case OPP_SPACING_HEAD_MOVE:
            actions[HEAD_COMBAT] = ATTACK_NONE;
            actions[HEAD_MOVE] = decision.move;
            return;
        default:
            fprintf(stderr, "invalid opponent spacing decision: %d\n",
                decision.kind);
            abort();
    }
}

static inline void opp_apply_hard_spacing_guard(
    OsrsEnv* env,
    OpponentType active,
    int* actions
) {
    if (!opp_type_uses_hard_spacing_guard(active)) return;

    Player* self = &env->players[1];
    Player* target = &env->players[0];
    int style = opp_legacy_attack_style_for_actions(self, actions);
    if (style < 0 || style == OPP_STYLE_MELEE || style == OPP_STYLE_SPEC) return;
    if (self->frozen_ticks > 0) return;

    int dx = abs_int(self->x - target->x);
    int dy = abs_int(self->y - target->y);
    int same_tile = dx == 0 && dy == 0;
    int cardinal_adjacent = dx + dy == 1;
    if (!same_tile && !cardinal_adjacent) return;
    int dist = max_int(dx, dy);

    OppSpacingDecision decision =
        opp_step_out_mode_for_type(active) == OPP_STEP_OUT_SMART
            ? opp_smart_projectile_spacing_decision(
                env, opp_attack_style_from_internal(style), actions[HEAD_LOADOUT])
            : opp_dumb_projectile_spacing_decision(target, dist);
    opp_apply_spacing_decision(actions, decision);
}

static void generate_opponent_action(OsrsEnv* env, OpponentState* opp) {
    int* actions = &env->pending_actions[1 * NUM_ACTION_HEADS];

    /* Clear actions to zero (KEEP/NONE for all heads) */
    memset(actions, 0, NUM_ACTION_HEADS * sizeof(int));

    /* Update flee tracking for all opponents */
    opp_update_flee_tracking(opp, &env->players[1], &env->players[0]);

    /* Resolve active policy for mixed types */
    OpponentType active = opp->type;
    if (active == OPP_MIXED_EASY || active == OPP_MIXED_MEDIUM ||
        active == OPP_MIXED_HARD || active == OPP_MIXED_HARD_BALANCED ||
        active == OPP_PFSP) {
        active = opp->active_sub_policy;
    }

    /* Dispatch to policy implementation */
    switch (active) {
        case OPP_TRUE_RANDOM:
            opp_true_random(env, actions);
            break;
        case OPP_PANICKING:
            opp_panicking(env, opp, actions);
            break;
        case OPP_WEAK_RANDOM:
            opp_weak_random(env, opp, actions);
            break;
        case OPP_SEMI_RANDOM:
            opp_semi_random(env, opp, actions);
            break;
        case OPP_STICKY_PRAYER:
            opp_sticky_prayer(env, opp, actions);
            break;
        case OPP_RANDOM_EATER:
            opp_random_eater(env, opp, actions);
            break;
        case OPP_PRAYER_ROOKIE:
            opp_prayer_rookie(env, opp, actions);
            break;
        case OPP_IMPROVED:
            opp_improved(env, opp, actions);
            break;
        case OPP_ONETICK:
            opp_onetick(env, opp, actions);
            break;
        case OPP_UNPREDICTABLE_IMPROVED:
            opp_unpredictable_improved(env, opp, actions);
            break;
        case OPP_UNPREDICTABLE_ONETICK:
            opp_unpredictable_onetick(env, opp, actions);
            break;
        case OPP_NOVICE_NH:
            opp_novice_nh(env, opp, actions);
            break;
        case OPP_APPRENTICE_NH:
            opp_apprentice_nh(env, opp, actions);
            break;
        case OPP_COMPETENT_NH:
            opp_competent_nh(env, opp, actions);
            break;
        case OPP_INTERMEDIATE_NH:
            opp_intermediate_nh(env, opp, actions);
            break;
        case OPP_ADVANCED_NH:
            opp_advanced_nh(env, opp, actions);
            break;
        case OPP_PROFICIENT_NH:
            opp_proficient_nh(env, opp, actions);
            break;
        case OPP_EXPERT_NH:
            opp_expert_nh(env, opp, actions);
            break;
        case OPP_MASTER_NH:
            opp_master_nh(env, opp, actions);
            break;
        case OPP_SAVANT_NH:
            opp_savant_nh(env, opp, actions);
            break;
        case OPP_NIGHTMARE_NH:
            opp_nightmare_nh(env, opp, actions);
            break;
        case OPP_VENG_FIGHTER:
            opp_veng_fighter(env, opp, actions);
            break;
        case OPP_BLOOD_HEALER:
            opp_blood_healer(env, opp, actions);
            break;
        case OPP_GMAUL_COMBO:
            opp_gmaul_combo(env, opp, actions);
            break;
        case OPP_RANGE_KITER:
            opp_range_kiter(env, opp, actions);
            break;
        case OPP_ADAPTIVE_NH:
            opp_adaptive_nh(env, opp, actions);
            break;
        case OPP_STRICT_KITER:
            opp_strict_kiter(env, opp, actions);
            break;
        default:
            break;
    }

    opp_apply_hard_spacing_guard(env, active, actions);
    pvp_translate_legacy_loadout_action_to_slotclicks(env, 1, actions);
}

static void swap_players_and_pending(OsrsEnv* env) {
    Player tmp_player = env->players[0];
    env->players[0] = env->players[1];
    env->players[1] = tmp_player;

    int tmp_walk_dest_x = env->pvp_runtime.walk_dest_x[0];
    int tmp_walk_dest_y = env->pvp_runtime.walk_dest_y[0];
    env->pvp_runtime.walk_dest_x[0] = env->pvp_runtime.walk_dest_x[1];
    env->pvp_runtime.walk_dest_y[0] = env->pvp_runtime.walk_dest_y[1];
    env->pvp_runtime.walk_dest_x[1] = tmp_walk_dest_x;
    env->pvp_runtime.walk_dest_y[1] = tmp_walk_dest_y;

    int tmp_actions[NUM_ACTION_HEADS];
    memcpy(tmp_actions, env->pending_actions, NUM_ACTION_HEADS * sizeof(int));
    memcpy(
        env->pending_actions,
        env->pending_actions + NUM_ACTION_HEADS,
        NUM_ACTION_HEADS * sizeof(int)
    );
    memcpy(
        env->pending_actions + NUM_ACTION_HEADS,
        tmp_actions,
        NUM_ACTION_HEADS * sizeof(int)
    );
}

static void generate_opponent_action_for_player0(OsrsEnv* env, OpponentState* opp) {
    swap_players_and_pending(env);
    generate_opponent_action(env, opp);
    swap_players_and_pending(env);
}

#endif /* OSRS_PVP_OPPONENTS_H */
