#ifndef OSRS_PVP_THREAT_H
#define OSRS_PVP_THREAT_H

#include "osrs_combat.h"
#include "osrs_item_effects.h"
#include "osrs_special_attacks.h"

typedef struct {
    int normal_max;
    int special_max;
    int special_count;
    int special_stack_max;
    int instant_special_max;
    int normal_plus_instant_max;
} OsrsMeleeThreat;

/** Visible loadout and explicit stat/energy assumptions to same-weapon damage bounds.
    HP lower bound maximizes Dharok damage. Excludes hidden switches, reflection and timing. */
static inline OsrsMeleeThreat osrs_melee_threat(
    const uint8_t equipment[NUM_GEAR_SLOTS], int assumed_effective_strength,
    int assumed_base_hp, int observed_hp_lower, int assumed_special_energy
) {
    assert(assumed_effective_strength > 0);
    assert(assumed_base_hp > 0 && observed_hp_lower >= 1);
    assert(assumed_special_energy >= 0 && assumed_special_energy <= 100);
    EquipmentBonuses bonuses;
    OsrsEquipmentEffectProfile effects;
    osrs_sum_equipment_bonuses(equipment, &bonuses);
    osrs_derive_equipment_effect_profile(equipment, &effects);
    OsrsMeleeThreat threat = {0};
    threat.normal_max = osrs_player_melee_max_hit(
        assumed_effective_strength, bonuses.melee_strength);
    if (effects.dharok_piece_count >= 4)
        threat.normal_max = osrs_dharok_max_hit(threat.normal_max,
            assumed_base_hp, observed_hp_lower);
    threat.normal_plus_instant_max = threat.normal_max;
    int weapon = equipment[GEAR_SLOT_WEAPON];
    int cost = osrs_spec_cost(weapon);
    if (cost == 0 || assumed_special_energy < cost) return threat;
    SpecResult special = {0};
    osrs_spec_result_force_max(&special, weapon, threat.normal_max, 0);
    threat.special_max = special.total_damage;
    threat.special_count = 1;
    if (weapon == ITEM_GRANITE_MAUL || weapon == ITEM_GRANITE_MAUL_ORNATE) {
        threat.special_count = assumed_special_energy / cost;
        threat.instant_special_max = threat.special_count * threat.special_max;
        threat.normal_plus_instant_max += threat.instant_special_max;
    }
    threat.special_stack_max = threat.special_count * threat.special_max;
    return threat;
}

/* ---- What a fighter can do to another: max hit, accuracy, speed and spec of every weapon, from item rows and levels alone.
 *
 * Pure functions of (loadout, levels, prayer, stance), the formulas of calculate_effective_attack / _strength / _defence, calculate_base_max_hit and
 * calculate_hit_chance (osrs_pvp_combat.h) without a Player: the sim and the live server both compute them from what a player can see, tests/test_threat_features.c
 * holds them equal to the sim on real Players. Magic is the sim's spell (base 30, the ice spell family) because the sim's riskfight has no spell choice; a
 * live staff gets the same number. */

/** Levels, prayer and stance one fighter fights with. */
typedef struct {
    int attack, strength, ranged, magic, defence;
    OffensivePrayer prayer;
    FightStyle stance;
    int soul_stacks; /* Soulreaper axe stacks held (osrs_special_attacks.h); only that weapon reads them */
} OsrsThreatActor;

/** The levels a fighter is assumed to fight with when only his gear is seen: a maxed account on piety / rigour / augury with a super combat (melee 118) or a
 * ranging potion (112), the stance a player of that weapon uses (aggressive, rapid; magic accurate). */
enum { OSRS_THREAT_ASSUMED_MELEE = 118, OSRS_THREAT_ASSUMED_RANGED = 112, OSRS_THREAT_ASSUMED_MAGIC = 99, OSRS_THREAT_ASSUMED_DEFENCE = 99 };

static inline int osrs_threat_style_of_weapon(uint8_t weapon) {
    if (weapon == ITEM_NONE) return ATTACK_STYLE_MELEE;
    int style = get_item_attack_style(weapon);
    assert(style != ATTACK_STYLE_NONE);
    return style;
}

static inline OsrsThreatActor osrs_threat_actor_assumed(int attack_style) {
    OsrsThreatActor a = {OSRS_THREAT_ASSUMED_MELEE, OSRS_THREAT_ASSUMED_MELEE, OSRS_THREAT_ASSUMED_RANGED, OSRS_THREAT_ASSUMED_MAGIC,
        OSRS_THREAT_ASSUMED_DEFENCE, OFFENSIVE_PRAYER_PIETY, FIGHT_STYLE_AGGRESSIVE};
    if (attack_style == ATTACK_STYLE_RANGED) { a.prayer = OFFENSIVE_PRAYER_RIGOUR; a.stance = FIGHT_STYLE_RAPID; }
    else if (attack_style == ATTACK_STYLE_MAGIC) { a.prayer = OFFENSIVE_PRAYER_AUGURY; a.stance = FIGHT_STYLE_ACCURATE; }
    return a;
}

/** Ammunition a wielder of `weapon` is assumed to carry: the sim has no observation of a quiver (and the client shows none). Thrown weapons carry their own. */
static inline uint8_t osrs_ammo_assumption(uint8_t weapon) {
    switch (weapon) {
        case ITEM_MAGIC_SHORTBOW_I: return ITEM_AMETHYST_ARROW;
        case ITEM_DARK_BOW: case ITEM_TWISTED_BOW: return ITEM_DRAGON_ARROWS;
        case ITEM_RUNE_CROSSBOW: case ITEM_ARMADYL_CROSSBOW: case ITEM_ZARYTE_CROSSBOW: return ITEM_RUBY_DRAGON_BOLTS_E;
        case ITEM_HEAVY_BALLISTA: return ITEM_DRAGON_JAVELIN;
        default: return ITEM_NONE;
    }
}

static inline float osrs_threat_attack_prayer(OffensivePrayer prayer, int style) {
    switch (style) {
        case ATTACK_STYLE_MELEE: return prayer == OFFENSIVE_PRAYER_PIETY ? 1.20f : prayer == OFFENSIVE_PRAYER_MELEE_LOW ? 1.15f : 1.0f;
        case ATTACK_STYLE_RANGED: return prayer == OFFENSIVE_PRAYER_RIGOUR ? 1.20f : prayer == OFFENSIVE_PRAYER_RANGED_LOW ? 1.15f : 1.0f;
        default: return prayer == OFFENSIVE_PRAYER_AUGURY ? 1.25f : prayer == OFFENSIVE_PRAYER_MAGIC_LOW ? 1.15f : 1.0f;
    }
}
static inline float osrs_threat_strength_prayer(OffensivePrayer prayer, int style) {
    switch (style) {
        case ATTACK_STYLE_MELEE: return prayer == OFFENSIVE_PRAYER_PIETY ? 1.23f : prayer == OFFENSIVE_PRAYER_MELEE_LOW ? 1.15f : 1.0f;
        case ATTACK_STYLE_RANGED: return prayer == OFFENSIVE_PRAYER_RIGOUR ? 1.23f : prayer == OFFENSIVE_PRAYER_RANGED_LOW ? 1.15f : 1.0f;
        default: return 1.0f;
    }
}
static inline float osrs_threat_defence_prayer(OffensivePrayer prayer) {
    switch (prayer) {
        case OFFENSIVE_PRAYER_MELEE_LOW: case OFFENSIVE_PRAYER_RANGED_LOW: case OFFENSIVE_PRAYER_MAGIC_LOW: return 1.15f;
        case OFFENSIVE_PRAYER_PIETY: case OFFENSIVE_PRAYER_RIGOUR: case OFFENSIVE_PRAYER_AUGURY: return 1.25f;
        default: return 1.0f;
    }
}

/** Defence rolls of one fighter against each kind of attack: melee by MeleeBonusType (stab, slash, crush), ranged, magic. */
typedef struct { int melee[3]; int ranged, magic; } OsrsDefenceRolls;

static inline OsrsDefenceRolls osrs_defence_rolls_from(const EquipmentBonuses* b, const OsrsThreatActor* who) {
    float prayer = osrs_threat_defence_prayer(who->prayer);
    int stance = osrs_stance_def_bonus(who->stance);
    int melee = osrs_player_eff_level(who->defence, prayer, stance);
    float magic_prayer = who->prayer == OFFENSIVE_PRAYER_AUGURY ? 1.25f : who->prayer == OFFENSIVE_PRAYER_MAGIC_LOW ? 1.15f : 1.0f;
    int magic_level = (int)floorf(who->magic * magic_prayer);
    int def_level = (int)floorf(who->defence * prayer);
    int magic = (int)(magic_level * 0.7f + def_level * 0.3f) + stance + 8;
    return (OsrsDefenceRolls){{melee * (b->defence_stab + 64), melee * (b->defence_slash + 64), melee * (b->defence_crush + 64)},
        melee * (b->defence_ranged + 64), magic * (b->defence_magic + 64)};
}

static inline OsrsDefenceRolls osrs_defence_rolls(const uint8_t equipment[NUM_GEAR_SLOTS], const OsrsThreatActor* who) {
    EquipmentBonuses b;
    osrs_sum_equipment_bonuses(equipment, &b);
    return osrs_defence_rolls_from(&b, who);
}

/** The sums a Player caches (slot_cached_bonuses) back as EquipmentBonuses; the prayer bonus is not part of what the calculators read. */
static inline EquipmentBonuses osrs_equipment_bonuses_of_gear(const GearBonuses* g) {
    EquipmentBonuses b;
    memset(&b, 0, sizeof(b));
    b.attack_stab = g->stab_attack; b.attack_slash = g->slash_attack; b.attack_crush = g->crush_attack; b.attack_magic = g->magic_attack; b.attack_ranged = g->ranged_attack;
    b.defence_stab = g->stab_defence; b.defence_slash = g->slash_defence; b.defence_crush = g->crush_defence; b.defence_magic = g->magic_defence; b.defence_ranged = g->ranged_defence;
    b.melee_strength = g->melee_strength; b.ranged_strength = g->ranged_strength; b.magic_damage = g->magic_strength;
    b.attack_speed = g->attack_speed; b.attack_range = g->attack_range;
    return b;
}

/** One weapon in one loadout: its normal attack and its special. Rolls are attack rolls, to be set against OsrsDefenceRolls. */
typedef struct {
    uint8_t item, style, speed, dharok;
    int8_t melee_type;          /* MeleeBonusType of the normal attack, -1 for ranged and magic */
    int att_roll, max_hit;      /* max_hit at full hitpoints: osrs_threat_max_hit_at scales a Dharok's */
    uint8_t bolt, zcb;          /* ranged: the ammunition and whether it is fired from a Zaryte crossbow (bolt effects: osrs_threat_effect_max) */
    int spec_cost;              /* special energy it costs; 0 with spec_hits > 0: a special that costs stacks (Behead) */
    int spec_hits, spec_max;    /* hits of one special (0: none) and the most all of them can deal */
    int spec_min;               /* least a landed special deals (Behead's minimum) */
    uint8_t spec_speed;         /* ticks until the next attack (1: the granite maul's instant special) */
    int8_t spec_type;           /* defence the special rolls against: MeleeBonusType, -1 for ranged and magic */
    int spec_att_roll, spec_def_num, spec_def_den, spec_certain;
} OsrsThreatWeapon;

/** Loadout as wielded `weapon` makes it: the weapon slot, no shield under a two-hander, the assumed ammunition for a ranged weapon. */
static inline void osrs_threat_loadout(const uint8_t equipment[NUM_GEAR_SLOTS], uint8_t weapon, uint8_t out[NUM_GEAR_SLOTS]) {
    memcpy(out, equipment, NUM_GEAR_SLOTS);
    out[GEAR_SLOT_WEAPON] = weapon;
    if (weapon != ITEM_NONE && item_is_two_handed(weapon)) out[GEAR_SLOT_SHIELD] = ITEM_NONE;
    uint8_t ammo = osrs_ammo_assumption(weapon);
    if (ammo != ITEM_NONE) out[GEAR_SLOT_AMMO] = ammo;
}

static inline int osrs_threat_best_melee_type(const EquipmentBonuses* b) {
    int best = MELEE_BONUS_STAB, value = b->attack_stab;
    if (b->attack_slash > value) { best = MELEE_BONUS_SLASH; value = b->attack_slash; }
    if (b->attack_crush > value) best = MELEE_BONUS_CRUSH;
    return best;
}
static inline int osrs_threat_melee_attack(const EquipmentBonuses* b, int type) {
    return type == MELEE_BONUS_STAB ? b->attack_stab : type == MELEE_BONUS_SLASH ? b->attack_slash : b->attack_crush;
}

/** The weapon `weapon` for bonuses `b` of a loadout that wields it (weapon row included, shield and ammunition as the loadout has them); `dharok` says the loadout is
 * a full Dharok's set. The two callers: osrs_threat_weapon (sums a loadout) and osrs_threat_weapon_swapped (derives it from what is worn). */
static inline OsrsThreatWeapon osrs_threat_weapon_core(const EquipmentBonuses* bonuses, int dharok_set, uint8_t ammo, uint8_t weapon, const OsrsThreatActor* who) {
    const EquipmentBonuses b = *bonuses;
    int style = osrs_threat_style_of_weapon(weapon);
    OsrsThreatWeapon w;
    memset(&w, 0, sizeof(w)); /* the struct is hashed byte for byte by the determinism proofs: no indeterminate padding */
    w.item = weapon; w.style = (uint8_t)style; w.melee_type = -1; w.spec_type = -1;
    int speed = b.attack_speed + osrs_stance_speed_mod(who->stance);
    assert(speed >= 1);
    w.speed = (uint8_t)speed;
    int stance_att = osrs_stance_att_bonus(who->stance, (AttackStyle)style);
    if (style == ATTACK_STYLE_MELEE) {
        w.melee_type = (int8_t)osrs_threat_best_melee_type(&b);
        int eff_att = osrs_player_eff_level(who->attack, osrs_threat_attack_prayer(who->prayer, style), stance_att);
        float str_mult = osrs_threat_strength_prayer(who->prayer, style);
        if (weapon == ITEM_SOULREAPER_AXE) str_mult = osrs_soul_strength_mult(str_mult, who->soul_stacks);
        int eff_str = osrs_player_eff_level(who->strength, str_mult, osrs_stance_str_bonus(who->stance));
        w.att_roll = eff_att * (osrs_threat_melee_attack(&b, w.melee_type) + 64);
        w.max_hit = osrs_player_melee_max_hit(eff_str, b.melee_strength);
        w.dharok = dharok_set;
    } else if (style == ATTACK_STYLE_RANGED) {
        int eff_att = osrs_player_eff_level(who->ranged, osrs_threat_attack_prayer(who->prayer, style), stance_att);
        int eff_str = osrs_player_eff_level(who->ranged, osrs_threat_strength_prayer(who->prayer, style), 0);
        w.att_roll = eff_att * (b.attack_ranged + 64);
        w.max_hit = osrs_player_ranged_max_hit(eff_str, b.ranged_strength);
        w.bolt = ammo;
        w.zcb = weapon == ITEM_ZARYTE_CROSSBOW;
    } else {
        int eff_att = osrs_magic_effective_attack_level(who->magic, osrs_threat_attack_prayer(who->prayer, style), who->stance);
        w.att_roll = eff_att * (b.attack_magic + 64);
        w.max_hit = (int)(30 * (1.0f + b.magic_damage / 100.0f) * 1.0f * osrs_offensive_magic_dmg_mult(who->prayer));
    }
    w.spec_cost = osrs_spec_cost(weapon);
    if (osrs_spec_is_stack_special(weapon)) {
        /* Behead: the stacks' strength bonus goes first, then +6% max and min and +12% accuracy per stack, against slash defence. */
        float str_mult = osrs_threat_strength_prayer(who->prayer, style);
        int eff_str = osrs_player_eff_level(who->strength, str_mult, osrs_stance_str_bonus(who->stance));
        int base_max = osrs_player_melee_max_hit(eff_str, b.melee_strength);
        int eff_att = osrs_player_eff_level(who->attack, osrs_threat_attack_prayer(who->prayer, style), stance_att);
        w.spec_hits = 1;
        w.spec_max = osrs_behead_max(base_max, who->soul_stacks);
        w.spec_min = osrs_behead_min(base_max, who->soul_stacks);
        w.spec_speed = w.speed;
        w.spec_type = MELEE_BONUS_SLASH;
        w.spec_def_num = w.spec_def_den = 1;
        w.spec_att_roll = eff_att * (b.attack_slash + 64) * (100 + 12 * who->soul_stacks) / 100;
    } else if (w.spec_cost > 0) {
        SpecResult forced;
        osrs_spec_result_force_max(&forced, weapon, w.max_hit, 0);
        w.spec_hits = forced.num_hits;
        w.spec_max = forced.total_damage;
        w.spec_speed = forced.attack_speed_override > 0 ? (uint8_t)forced.attack_speed_override : w.speed;
        OsrsSpecAccuracy acc = osrs_spec_accuracy(weapon);
        w.spec_certain = acc.certain;
        w.spec_def_num = acc.def_num;
        w.spec_def_den = acc.def_den;
        int spec_att = w.att_roll;
        if (style == ATTACK_STYLE_MELEE) {
            int attack_type = acc.attack_type >= 0 ? acc.attack_type : osrs_threat_best_melee_type(&b);
            w.spec_type = (int8_t)(acc.defence_type >= 0 ? acc.defence_type : attack_type);
            int eff_att = osrs_player_eff_level(who->attack, osrs_threat_attack_prayer(who->prayer, style), stance_att);
            spec_att = eff_att * (osrs_threat_melee_attack(&b, attack_type) + 64);
        }
        w.spec_att_roll = spec_att * acc.att_num / acc.att_den;
    }
    return w;
}

/** The weapon `weapon` as `who` wields it in `equipment` (weapon slot overwritten; ITEM_NONE fights bare-handed): the loadout summed from scratch. */
static inline OsrsThreatWeapon osrs_threat_weapon(const uint8_t equipment[NUM_GEAR_SLOTS], uint8_t weapon, const OsrsThreatActor* who) {
    uint8_t loadout[NUM_GEAR_SLOTS];
    osrs_threat_loadout(equipment, weapon, loadout);
    EquipmentBonuses b;
    osrs_sum_equipment_bonuses(loadout, &b);
    OsrsEquipmentEffectProfile effects;
    osrs_derive_equipment_effect_profile(loadout, &effects);
    return osrs_threat_weapon_core(&b, effects.dharok_piece_count >= 4, loadout[GEAR_SLOT_AMMO], weapon, who);
}

static inline void osrs_threat_bonus_item(EquipmentBonuses* b, uint8_t item, int sign) {
    const Item* it = &ITEM_DATABASE[item];
    b->attack_stab += sign * it->attack_stab; b->attack_slash += sign * it->attack_slash; b->attack_crush += sign * it->attack_crush;
    b->attack_magic += sign * it->attack_magic; b->attack_ranged += sign * it->attack_ranged;
    b->defence_stab += sign * it->defence_stab; b->defence_slash += sign * it->defence_slash; b->defence_crush += sign * it->defence_crush;
    b->defence_magic += sign * it->defence_magic; b->defence_ranged += sign * it->defence_ranged;
    b->melee_strength += sign * it->melee_strength; b->ranged_strength += sign * it->ranged_strength; b->magic_damage += sign * it->magic_damage;
    b->prayer += sign * it->prayer;
}

/** osrs_threat_weapon for a fighter whose worn loadout is `equipment` with summed bonuses `worn` (e.g. a Player's cache) and `worn_dharok_armour` Dharok's pieces among
 * its armour: the weapon row swaps in, a two-hander takes the shield off, the assumed ammunition goes on: a handful of adds instead of summing the loadout again. */
static inline OsrsThreatWeapon osrs_threat_weapon_swapped(const EquipmentBonuses* worn, int worn_dharok_armour, const uint8_t equipment[NUM_GEAR_SLOTS], uint8_t weapon,
        const OsrsThreatActor* who) {
    uint8_t worn_weapon = equipment[GEAR_SLOT_WEAPON];
    int dharok_set = worn_dharok_armour >= 3 && weapon != ITEM_NONE && (ITEM_DATABASE[weapon].effect_mask & OSRS_ITEM_EFFECT_DHAROK_PIECE);
    if (osrs_weapon_is_thrown(weapon) || osrs_weapon_is_thrown(worn_weapon)) { /* a thrown weapon drops the worn ammunition: not a delta */
        uint8_t loadout[NUM_GEAR_SLOTS];
        osrs_threat_loadout(equipment, weapon, loadout);
        EquipmentBonuses b;
        osrs_sum_equipment_bonuses(loadout, &b);
        return osrs_threat_weapon_core(&b, dharok_set, loadout[GEAR_SLOT_AMMO], weapon, who);
    }
    EquipmentBonuses b = *worn;
    if (worn_weapon != ITEM_NONE) osrs_threat_bonus_item(&b, worn_weapon, -1);
    if (weapon != ITEM_NONE) {
        osrs_threat_bonus_item(&b, weapon, 1);
        if (item_is_two_handed(weapon) && equipment[GEAR_SLOT_SHIELD] != ITEM_NONE) osrs_threat_bonus_item(&b, equipment[GEAR_SLOT_SHIELD], -1);
    }
    uint8_t ammo = osrs_ammo_assumption(weapon);
    if (ammo != ITEM_NONE && equipment[GEAR_SLOT_AMMO] != ammo) {
        if (equipment[GEAR_SLOT_AMMO] != ITEM_NONE) osrs_threat_bonus_item(&b, equipment[GEAR_SLOT_AMMO], -1);
        osrs_threat_bonus_item(&b, ammo, 1);
    }
    b.attack_speed = weapon == ITEM_NONE ? 4 : ITEM_DATABASE[weapon].attack_speed;
    b.attack_range = weapon == ITEM_NONE ? 1 : ITEM_DATABASE[weapon].attack_range;
    return osrs_threat_weapon_core(&b, dharok_set, ammo != ITEM_NONE ? ammo : equipment[GEAR_SLOT_AMMO], weapon, who);
}

static inline int osrs_threat_defence_roll(const OsrsDefenceRolls* d, int style, int melee_type) {
    return style == ATTACK_STYLE_MELEE ? d->melee[melee_type] : style == ATTACK_STYLE_RANGED ? d->ranged : d->magic;
}

/** Chance the normal attack hits a defender with these rolls. */
static inline float osrs_threat_hit_chance(const OsrsThreatWeapon* w, const OsrsDefenceRolls* d) {
    float chance = osrs_hit_chance(w->att_roll, osrs_threat_defence_roll(d, w->style, w->melee_type));
    return chance < 0 ? 0 : chance > 1 ? 1 : chance;
}

/** Chance one accuracy roll of the special lands (1 for a special that rolls none). */
static inline float osrs_threat_spec_hit_chance(const OsrsThreatWeapon* w, const OsrsDefenceRolls* d) {
    assert(w->spec_hits > 0);
    if (w->spec_certain) return 1;
    int def = osrs_threat_defence_roll(d, w->style, w->spec_type) * w->spec_def_num / w->spec_def_den;
    float chance = osrs_hit_chance(w->spec_att_roll, def);
    return chance < 0 ? 0 : chance > 1 ? 1 : chance;
}

/** The normal max hit with a Dharok set's bonus at the attacker's hitpoints (`hp_lower`, a lower bound maximizes it). */
static inline int osrs_threat_max_hit_at(const OsrsThreatWeapon* w, int base_hp, int hp_lower) {
    return w->dharok ? osrs_dharok_max_hit(w->max_hit, base_hp, hp_lower) : w->max_hit;
}


/** The most one shot can deal once the bolt's effect is counted, against a defender with `defender_hp`: `base` is the shot's own maximum (w->max_hit, or the special's).
 * Ruby: 20% of the defender's current hitpoints capped at 100 (22%, 110 from a Zaryte crossbow); diamond: the maximum hit +15% (+26%). No effect: `base`. */
static inline int osrs_threat_effect_max(const OsrsThreatWeapon* w, int base, int defender_hp) {
    if (w->style != ATTACK_STYLE_RANGED) return base;
    int effect = 0;
    switch (w->bolt) {
        case ITEM_RUBY_DRAGON_BOLTS_E:
            effect = defender_hp * (w->zcb ? 22 : 20) / 100;
            if (effect > (w->zcb ? 110 : 100)) effect = w->zcb ? 110 : 100;
            break;
        case ITEM_DIAMOND_BOLTS_E: case ITEM_DIAMOND_DRAGON_BOLTS_E:
            effect = w->max_hit * (w->zcb ? 126 : 115) / 100;
            break;
        default: break;
    }
    return effect > base ? effect : base;
}

#endif
