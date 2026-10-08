#ifndef GS_ITEMS_H
#define GS_ITEMS_H
/* Ports of MaxHit, Accuracy, AttackStyle, AttackSpeeds, SpecCosts and Damage over a table of item stats keyed by the raw item id. */
#include "gs_machines.h"
#include "gs_tables.h"

typedef enum { GS_W_VOIDWAKER, GS_W_GRANITE_MAUL, GS_W_ARMADYL_GODSWORD, GS_W_ANCIENT_GODSWORD, GS_W_DRAGON_CLAWS, GS_W_DRAGON_DAGGER,
    GS_W_ABYSSAL_DAGGER, GS_W_VESTAS_SPEAR, GS_W_DRAGON_MACE_BH, GS_W_DRAGON_MACE, GS_W_VESTAS_LONGSWORD, GS_W_STATIUS_WARHAMMER,
    GS_W_MORRIGANS_THROWING_AXE, GS_W_MORRIGANS_JAVELIN, GS_W_ZURIELS_STAFF, GS_W_NOXIOUS_HALBERD,
    GS_W_DRAGON_WARHAMMER, GS_W_DARK_BOW, GS_W_BALLISTA,
    GS_W_ARMADYL_CROSSBOW, GS_W_ZARYTE_CROSSBOW, GS_W_TOXIC_BLOWPIPE, GS_W_MAGIC_SHORTBOW_I, GS_W_MAGIC_SHORTBOW,
    GS_W_VOLATILE_NIGHTMARE_STAFF, GS_W_DHAROKS_GREATAXE, GS_W_SOULREAPER_AXE, GS_W_OTHER, GS_WEAPONS } GsWeapon;

typedef struct { int cost; double least, spec_multiplier, accuracy; int assumed_ammo; const char* names[2]; } GsWeaponRow;
static const GsWeaponRow GS_WEAPON_ROWS[GS_WEAPONS] = {
    {50, 0.5, 1.5, 1, 0, {"voidwaker"}}, {50, 0, 1.0, 1, 0, {"granite maul"}}, {50, 0, 1.375, 2, 0, {"armadyl godsword"}}, {50, 0, 1.1, 2, 0, {"ancient godsword"}},
    {50, 0, 1.0, 1, 0, {"dragon claws"}}, {25, 0, 2.3, 1.25, 0, {"dragon dagger"}}, {25, 0, 0.85, 1.25, 0, {"abyssal dagger"}},
    {50, 0, 1.0, 1, 0, {"vesta's spear"}}, {15, 0, 1.5, 1.25, 0, {"dragon mace (bh)"}}, {25, 0, 1.5, 1.25, 0, {"dragon mace"}},
    {25, 0.2, 1.2, 1, 0, {"vesta's longsword", "vesta's blighted longsword"}}, {35, 0.25, 1.25, 1, 0, {"statius's warhammer"}},
    {50, 0.5, 1.5, 1.5, 0, {"morrigan's throwing axe"}}, {50, 0, 1.0, 1.5, 0, {"morrigan's javelin"}}, {0, 0, 1.0, 1, 0, {"zuriel's staff"}},
    {0, 0, 1.0, 1, 0, {"noxious halberd"}},
    {50, 0, 1.5, 1, 0, {"dragon warhammer"}}, {55, 0, 1.5, 1, 60, {"dark bow"}}, {65, 0, 1.25, 1.25, 150, {"heavy ballista", "light ballista"}},
    {40, 0, 1.0, 2, 122, {"armadyl crossbow"}}, {75, 0, 1.0, 1, 122, {"zaryte crossbow"}}, {50, 0, 1.5, 1, 35, {"toxic blowpipe"}},
    {50, 0, 1.0, 1, 55, {"magic shortbow (i)"}}, {55, 0, 1.0, 1, 55, {"magic shortbow"}}, {55, 0, 1.0, 1, 0, {"volatile nightmare staff"}},
    {0, 0, 1.0, 1, 0, {"dharok's greataxe"}}, {0, 0, 1.0, 1, 0, {"soulreaper axe"}}, {0, 0, 1.0, 1, 0, {0}},
};

typedef enum { GS_MELEE_STYLE_UNKNOWN = -1 } GsStyleUnknown;

typedef struct {
    int present, has_stats;
    int astab, aslash, acrush, arange, amagic, dstab, dslash, dcrush, drange, dmagic, str, rstr;
    double mdmg;
    int aspeed;
    GsWeapon weapon;
    int style;
} GsItem;

enum { GS_ITEM_LIMIT = 40000 };
static GsItem GS_ITEMS[GS_ITEM_LIMIT];

static GsWeapon gs_weapon_named(const char* name) {
    char lower[128];
    size_t n = strlen(name);
    assert(n < sizeof(lower));
    for (size_t i = 0; i <= n; i++) lower[i] = (char)((name[i] >= 'A' && name[i] <= 'Z') ? name[i] + 32 : name[i]);
    for (int w = 0; w < GS_WEAPONS; w++)
        for (int k = 0; k < 2; k++)
            if (GS_WEAPON_ROWS[w].names[k] && strncmp(lower, GS_WEAPON_ROWS[w].names[k], strlen(GS_WEAPON_ROWS[w].names[k])) == 0) return (GsWeapon)w;
    return GS_W_OTHER;
}

static int gs_style_of(int stab, int slash, int crush, int ranged, int ranged_strength, int magic) {
    if (magic >= 10) return GS_MAGIC;
    if (ranged_strength > 0) return GS_RANGED;
    return ranged > imax(stab, imax(slash, crush)) && ranged > magic ? GS_RANGED : GS_MELEE;
}

static void gs_item_register(int id, const char* name, int has_stats, const int stats[14], double mdmg, int aspeed) {
    assert(id >= 0 && id < GS_ITEM_LIMIT);
    GsItem* item = &GS_ITEMS[id];
    memset(item, 0, sizeof(*item));
    item->present = 1;
    item->has_stats = has_stats;
    item->weapon = gs_weapon_named(name);
    if (has_stats) {
        item->astab = stats[0]; item->aslash = stats[1]; item->acrush = stats[2]; item->arange = stats[3]; item->amagic = stats[4];
        item->dstab = stats[5]; item->dslash = stats[6]; item->dcrush = stats[7]; item->drange = stats[8]; item->dmagic = stats[9];
        item->str = stats[10]; item->rstr = stats[11];
        item->mdmg = mdmg;
        item->aspeed = aspeed;
        item->style = gs_style_of(item->astab, item->aslash, item->acrush, item->arange, item->rstr, item->amagic);
    } else item->style = -1;
}

static const GsItem* gs_item(int id) {
    static const GsItem unknown = {.weapon = GS_W_OTHER, .style = -1};
    return id >= 0 && id < GS_ITEM_LIMIT && GS_ITEMS[id].present ? &GS_ITEMS[id] : &unknown;
}

/* AttackStyle.ofWeapon: -1 for unarmed is melee, -2 for no stats */
static int gs_weapon_style(int weapon_id) {
    if (weapon_id == -1) return GS_MELEE;
    return gs_item(weapon_id)->style;
}

static GsWeapon gs_weapon_kind(int weapon_id) { return weapon_id == -1 ? GS_W_OTHER : gs_item(weapon_id)->weapon; }

static const int GS_MISSING_SPEEDS[6][2] = {{27690, 4}, {29607, 4}, {33335, 5}, {28338, 5}, {29000, 3}, {29605, 6}};

/* AttackSpeeds.base, -1 when unknown */
static int gs_base_speed(int weapon_id) {
    if (weapon_id == -1) return 4;
    for (int i = 0; i < 6; i++) if (GS_MISSING_SPEEDS[i][0] == weapon_id) return GS_MISSING_SPEEDS[i][1];
    const GsItem* item = gs_item(weapon_id);
    return item->has_stats ? item->aspeed : -1;
}

typedef enum { GS_PACE_RAPID, GS_PACE_NOT_RAPID, GS_PACE_UNKNOWN } GsPace;

static GsSpeed gs_speed_of(int base, int style, GsPace pace) {
    if (style != GS_RANGED) return (GsSpeed){1, base, base};
    switch (pace) {
        case GS_PACE_RAPID: return (GsSpeed){1, base - 1, base - 1};
        case GS_PACE_NOT_RAPID: return (GsSpeed){1, base, base};
        case GS_PACE_UNKNOWN: return (GsSpeed){1, base - 1, base};
    }
    abort();
}

static const GsAnimationRow* gs_animation_row(int animation) {
    static const GsAnimationRow* index[20000];
    static int built;
    if (!built) {
        for (size_t i = 0; i < sizeof(GS_ANIMATIONS) / sizeof(*GS_ANIMATIONS); i++) index[GS_ANIMATIONS[i].animation] = &GS_ANIMATIONS[i];
        built = 1;
    }
    return animation >= 0 && animation < 20000 ? index[animation] : NULL;
}

static GsKind gs_animation_kind(int animation) {
    const GsAnimationRow* row = gs_animation_row(animation);
    return row ? (GsKind)row->kind : GS_KIND_OTHER;
}
static int gs_animation_spec_cost(int animation) {
    const GsAnimationRow* row = gs_animation_row(animation);
    return row ? row->spec_cost : 0;
}

/* AttackSpeeds.after: the interval after an attack animation with the weapon */
static GsSpeed gs_speed_after(int weapon_id, int animation, GsPace pace) {
    const GsAnimationRow* row = gs_animation_row(animation);
    if (row && row->spell_speed) return (GsSpeed){1, row->spell_speed, row->spell_speed};
    int base = gs_base_speed(weapon_id);
    if (base < 0) return (GsSpeed){0, 0, 0};
    if (animation == GS_ANIMATION_SPEAR_WALL) return (GsSpeed){1, base - 1, base};
    int style = gs_weapon_style(weapon_id);
    return gs_speed_of(base, style < 0 ? GS_MELEE : style, pace);
}

static int gs_weapon_spec_cost(int item) {
    for (size_t i = 0; i < sizeof(GS_WEAPON_COSTS) / sizeof(*GS_WEAPON_COSTS); i++) if (GS_WEAPON_COSTS[i].item == item) return GS_WEAPON_COSTS[i].cost;
    return 0;
}

/* Pid.counts */
static int gs_pid_counts(int animation, int style) {
    return gs_animation_kind(animation) == GS_KIND_ATTACK && style == GS_MELEE;
}

/* MaxHit.java */
typedef struct { int strength, ranged; double strength_prayer, ranged_prayer, magic_prayer; } GsLevels;
typedef struct { int strength, ranged_strength; double magic_damage; } GsBonuses;
static const GsLevels GS_ASSUMED_LEVELS = {118, 112, 1.23, 1.23, 1.04};
static const GsBonuses GS_NO_BONUSES = {0, 0, 0};
static const GsBonuses GS_ASSUMED_RING = {8, 0, 0};

static GsBonuses gs_bonuses_plus(GsBonuses a, GsBonuses b) {
    return (GsBonuses){a.strength + b.strength, a.ranged_strength + b.ranged_strength, a.magic_damage + b.magic_damage};
}

static int gs_max_melee(GsLevels l, GsBonuses b) {
    int effective = (int)floor(l.strength * l.strength_prayer) + 8 + 3;
    return (int)floor(0.5 + effective * (b.strength + 64) / 640.0);
}
static int gs_max_ranged(GsLevels l, GsBonuses b) {
    double effective = floor(l.ranged * l.ranged_prayer + 8);
    return (int)floor(0.5 + effective * (b.ranged_strength + 64) / 640.0);
}
static int gs_max_magic(int spell, GsLevels l, GsBonuses b) { return (int)(spell * (1 + b.magic_damage / 100.0) * l.magic_prayer); }

static int gs_max_normal(GsWeapon weapon, int style, GsLevels l, GsBonuses b) {
    switch (style) {
        case GS_MELEE: return gs_max_melee(l, b);
        case GS_RANGED: return weapon == GS_W_DARK_BOW ? 2 * gs_max_ranged(l, b) : gs_max_ranged(l, b);
        case GS_MAGIC: return gs_max_magic(30, l, b);
    }
    abort();
}

static int gs_max_spec(GsWeapon weapon, int style, GsLevels l, GsBonuses b, int* out) {
    const GsWeaponRow* row = &GS_WEAPON_ROWS[weapon];
    switch (weapon) {
        case GS_W_OTHER: case GS_W_NOXIOUS_HALBERD: case GS_W_DHAROKS_GREATAXE: case GS_W_SOULREAPER_AXE: return 0;
        case GS_W_DRAGON_CLAWS: { int first = gs_max_melee(l, b) - 1; *out = first + first / 2 + first / 4 + first / 4 + 1; return 1; }
        case GS_W_DARK_BOW: *out = imin(96, (int)(2 * row->spec_multiplier * gs_max_ranged(l, b))); return 1;
        case GS_W_ABYSSAL_DAGGER: *out = 2 * (int)(row->spec_multiplier * gs_max_normal(weapon, style, l, b)); return 1;
        case GS_W_MAGIC_SHORTBOW: case GS_W_MAGIC_SHORTBOW_I: *out = 2 * gs_max_ranged(l, b); return 1;
        case GS_W_VOLATILE_NIGHTMARE_STAFF: *out = gs_max_magic(66, l, b); return 1;
        default: *out = (int)(row->spec_multiplier * gs_max_normal(weapon, style, l, b)); return 1;
    }
}

static int gs_max_behead(GsLevels l, GsBonuses b, int stacks) { return (int)(gs_max_melee(l, b) * (1 + 0.06 * stacks)); }
static int gs_max_behead_least(int behead_most, int stacks) { return (int)(0.06 * stacks * behead_most); }

static double gs_spec_accuracy_multiplier(GsWeapon weapon) { return GS_WEAPON_ROWS[weapon].accuracy; }

static int gs_dharok(int most, int hitpoints, int max_hitpoints) {
    return (int)(most * (1 + imax(0, max_hitpoints - hitpoints) / 100.0 * max_hitpoints / 100.0));
}

/* Accuracy.java */
static int gs_effective_level(int level, int prayer_percent, int stance) { return level * prayer_percent / 100 + 8 + stance; }
static int gs_effective_magic_defence(int magic, int magic_prayer, int defence, int defence_prayer) {
    return (magic * magic_prayer / 100 * 7 + defence * defence_prayer / 100 * 3) / 10 + 8;
}
static int gs_roll(int effective_level, int bonus) { return effective_level * (bonus + 64); }
static double gs_chance(int attack_roll, int defence_roll) {
    return attack_roll > defence_roll ? 1 - (defence_roll + 2) / (2.0 * (attack_roll + 1)) : attack_roll / (2.0 * (defence_roll + 1));
}

/* Damage.java */
typedef enum { GS_DAMAGE_STAB, GS_DAMAGE_SLASH, GS_DAMAGE_CRUSH, GS_DAMAGE_RANGED, GS_DAMAGE_MAGIC } GsDamage;
static int gs_damage_attack(GsDamage d, const GsItem* i) {
    switch (d) {
        case GS_DAMAGE_STAB: return i->astab; case GS_DAMAGE_SLASH: return i->aslash; case GS_DAMAGE_CRUSH: return i->acrush;
        case GS_DAMAGE_RANGED: return i->arange; case GS_DAMAGE_MAGIC: return i->amagic;
    }
    abort();
}
static int gs_damage_defence(GsDamage d, const GsItem* i) {
    switch (d) {
        case GS_DAMAGE_STAB: return i->dstab; case GS_DAMAGE_SLASH: return i->dslash; case GS_DAMAGE_CRUSH: return i->dcrush;
        case GS_DAMAGE_RANGED: return i->drange; case GS_DAMAGE_MAGIC: return i->dmagic;
    }
    abort();
}
static int gs_sum_attack(GsDamage d, const GsItem* const* worn, int count) {
    int sum = 0;
    for (int i = 0; i < count; i++) sum += gs_damage_attack(d, worn[i]);
    return sum;
}
static GsDamage gs_damage_of(int style, const GsItem* const* worn, int count) {
    if (style == GS_RANGED) return GS_DAMAGE_RANGED;
    if (style == GS_MAGIC) return GS_DAMAGE_MAGIC;
    GsDamage best = GS_DAMAGE_STAB;
    int best_sum = gs_sum_attack(GS_DAMAGE_STAB, worn, count);
    for (GsDamage d = GS_DAMAGE_SLASH; d <= GS_DAMAGE_CRUSH; d++) {
        int sum = gs_sum_attack(d, worn, count);
        if (sum > best_sum) { best = d; best_sum = sum; }
    }
    return best;
}

static GsBonuses gs_bonuses_of(int item_id) {
    const GsItem* item = gs_item(item_id);
    return item->has_stats ? (GsBonuses){item->str, item->rstr, item->mdmg} : GS_NO_BONUSES;
}
#endif
