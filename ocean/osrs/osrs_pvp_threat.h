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

static inline OsrsDefenceRolls osrs_defence_rolls(const uint8_t equipment[NUM_GEAR_SLOTS], const OsrsThreatActor* who) {
    EquipmentBonuses b;
    osrs_sum_equipment_bonuses(equipment, &b);
    float prayer = osrs_threat_defence_prayer(who->prayer);
    int stance = osrs_stance_def_bonus(who->stance);
    int melee = osrs_player_eff_level(who->defence, prayer, stance);
    float magic_prayer = who->prayer == OFFENSIVE_PRAYER_AUGURY ? 1.25f : who->prayer == OFFENSIVE_PRAYER_MAGIC_LOW ? 1.15f : 1.0f;
    int magic_level = (int)floorf(who->magic * magic_prayer);
    int def_level = (int)floorf(who->defence * prayer);
    int magic = (int)(magic_level * 0.7f + def_level * 0.3f) + stance + 8;
    return (OsrsDefenceRolls){{melee * (b.defence_stab + 64), melee * (b.defence_slash + 64), melee * (b.defence_crush + 64)},
        melee * (b.defence_ranged + 64), magic * (b.defence_magic + 64)};
}

/** One weapon in one loadout: its normal attack and its special. Rolls are attack rolls, to be set against OsrsDefenceRolls. */
typedef struct {
    uint8_t item, style, speed, dharok;
    int8_t melee_type;          /* MeleeBonusType of the normal attack, -1 for ranged and magic */
    int att_roll, max_hit;      /* max_hit at full hitpoints: osrs_threat_max_hit_at scales a Dharok's */
    int spec_cost;              /* 0: no special */
    int spec_hits, spec_max;    /* hits of one special and the most all of them can deal */
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

/** The weapon `weapon` as `who` wields it in `equipment` (weapon slot overwritten; ITEM_NONE fights bare-handed). */
static inline OsrsThreatWeapon osrs_threat_weapon(const uint8_t equipment[NUM_GEAR_SLOTS], uint8_t weapon, const OsrsThreatActor* who) {
    uint8_t loadout[NUM_GEAR_SLOTS];
    osrs_threat_loadout(equipment, weapon, loadout);
    EquipmentBonuses b;
    osrs_sum_equipment_bonuses(loadout, &b);
    int style = osrs_threat_style_of_weapon(weapon);
    OsrsThreatWeapon w = {.item = weapon, .style = (uint8_t)style, .melee_type = -1, .spec_type = -1};
    int speed = b.attack_speed + osrs_stance_speed_mod(who->stance);
    assert(speed >= 1);
    w.speed = (uint8_t)speed;
    int stance_att = osrs_stance_att_bonus(who->stance, style == ATTACK_STYLE_MAGIC ? ATTACK_STYLE_MAGIC : style);
    if (style == ATTACK_STYLE_MELEE) {
        w.melee_type = (int8_t)osrs_threat_best_melee_type(&b);
        int eff_att = osrs_player_eff_level(who->attack, osrs_threat_attack_prayer(who->prayer, style), stance_att);
        int eff_str = osrs_player_eff_level(who->strength, osrs_threat_strength_prayer(who->prayer, style), osrs_stance_str_bonus(who->stance));
        w.att_roll = eff_att * (osrs_threat_melee_attack(&b, w.melee_type) + 64);
        w.max_hit = osrs_player_melee_max_hit(eff_str, b.melee_strength);
        OsrsEquipmentEffectProfile effects;
        osrs_derive_equipment_effect_profile(loadout, &effects);
        w.dharok = effects.dharok_piece_count >= 4;
    } else if (style == ATTACK_STYLE_RANGED) {
        int eff_att = osrs_player_eff_level(who->ranged, osrs_threat_attack_prayer(who->prayer, style), stance_att);
        int eff_str = osrs_player_eff_level(who->ranged, osrs_threat_strength_prayer(who->prayer, style), 0);
        w.att_roll = eff_att * (b.attack_ranged + 64);
        w.max_hit = osrs_player_ranged_max_hit(eff_str, b.ranged_strength);
    } else {
        int eff_att = osrs_magic_effective_attack_level(who->magic, osrs_threat_attack_prayer(who->prayer, style), who->stance);
        w.att_roll = eff_att * (b.attack_magic + 64);
        w.max_hit = (int)(30 * (1.0f + b.magic_damage / 100.0f) * 1.0f * osrs_offensive_magic_dmg_mult(who->prayer));
    }
    w.spec_cost = osrs_spec_cost(weapon);
    if (w.spec_cost > 0) {
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
    assert(w->spec_cost > 0);
    if (w->spec_certain) return 1;
    int def = osrs_threat_defence_roll(d, w->style, w->spec_type) * w->spec_def_num / w->spec_def_den;
    float chance = osrs_hit_chance(w->spec_att_roll, def);
    return chance < 0 ? 0 : chance > 1 ? 1 : chance;
}

/** The normal max hit with a Dharok set's bonus at the attacker's hitpoints (`hp_lower`, a lower bound maximizes it). */
static inline int osrs_threat_max_hit_at(const OsrsThreatWeapon* w, int base_hp, int hp_lower) {
    return w->dharok ? osrs_dharok_max_hit(w->max_hit, base_hp, hp_lower) : w->max_hit;
}

#endif
