#ifndef GS_INFER_H
#define GS_INFER_H
/* Port of the Gear Swapper plugin's event handlers and AutoEater: everything the plugin infers about the opponent from what the
 * client shows. Our side is read through GsClient, the client as the recording or the sim's observation layer shows it. */
#include "gs_rules.h"
#include "gs_items.h"

enum { GS_WORN_SLOTS = 14, GS_WORN_WEAPON = 3, GS_WORN_RING = 12, GS_MAX_PLAYERS = 8, GS_MAX_EQUIPMENT = 16, GS_KIT_WEAPON = 3, GS_ITEM_OFFSET = 2048,
    GS_ARSENAL_SIZE = 12, GS_TRACKING_MAXIMUM_DISTANCE = 20 };

typedef enum { GS_HEAD_NONE, GS_HEAD_MELEE, GS_HEAD_RANGED, GS_HEAD_MAGIC, GS_HEAD_OTHER } GsHeadIcon;

typedef enum { GS_ANIMATION_UNKNOWN, GS_ANIMATION_OBSERVED } GsAnimationSource;
typedef struct { GsAnimationSource source; int id; } GsCurrentAnimation;

typedef struct {
    int hitpoints, attack, strength, defence, ranged, magic, prayer;
    uint64_t prayers;
    int spec_energy_varp, sa_attack, com_mode;
    int lx, ly, local_interacting;
    int worn[GS_WORN_SLOTS], inventory[28];
    int player_count, player_id[GS_MAX_PLAYERS], player_interacting[GS_MAX_PLAYERS];
    int tracked, tracked_interacting;
    int ox, oy, ratio, scale, oweapon;
    GsHeadIcon overhead;
    int equipment_count, equipment[GS_MAX_EQUIPMENT];
    GsCurrentAnimation opponent_animation;
} GsClient;

typedef struct {
    GsWeapon weapon;
    int style, normal, has_spec, spec, interval;
    GsDamage damage;
    int attack_bonus;
    GsBonuses bonuses;
} GsArmament;

typedef enum { GS_BAR_READ, GS_BAR_FRESH, GS_BAR_BEHIND_A_MEAL } GsBar;
typedef enum { GS_RECOIL_UNSEEN, GS_RECOIL_SEEN } GsRecoil;
typedef struct { int lands, least, most; double accuracy; } GsStrike;
enum { GS_UNSEEN_HEAL = 8, GS_FOOD_CAP_TICKS = 16, GS_FIGHTING_OVERHEAL = 115, GS_UNSEEN_LIMIT = 64 };

typedef struct {
    GsClient c;
    int tick;
    int tracker_current;
    int timed;
    GsTimerState local_timer;
    GsOpponentInference opponent;
    GsPidState pid;
    int has_damage_since_reading, damage_since_reading;
    int has_unprocced, unprocced;
    int has_pulse, pulse_tick, pulse_superseded, pulse_kinds;
    GsTimerState pulse_before;
    int local_attack_tick, own_hit_lands, own_hit_damage, attack_tick;
    int recoil_charges;

    int arsenal_count, arsenal_id[GS_ARSENAL_SIZE];
    GsArmament arsenal[GS_ARSENAL_SIZE];
    int inventory_units[3], ready[3];
    GsHealthRange opponent_hitpoints;
    int has_opponent_swing, swing_spec, swing_distance, swing_tick, swing_stacks; GsArmament swing_armament;
    int has_swung, swung_spec, swung_tick, swung_stacks; GsArmament swung_armament;
    int landed_tick, landed_damage;
    GsBonuses surplus;
    GsReflect swing_reflect;
    GsSoulStacks soul_stacks;
    int has_local_swing; GsSwing local_swing;
    int opponent_hard_food_ready, opponent_struck_tick, opponent_in_reach_since, wielded_weapon, wielded_since, spec_wielded_since, opponent_ate;
    GsBar bar;
    int damage_since_bar;
    int unseen[GS_UNSEEN_LIMIT], unseen_count;
    uint8_t watched[GS_WATCHED_LIMIT];
    int watched_count, ate_at, masked_at;
    GsHealthRange opponent_bar;
    GsRecoil recoil;
    int struck_tick, has_struck_damage, struck_damage;
} GsInfer;

static void gs_eater_reset(GsInfer* s) {
    s->arsenal_count = 0;
    s->opponent_hitpoints = (GsHealthRange){GS_OVERHEALED_HP, GS_OVERHEALED_HP, GS_OVERHEALED_HP};
    s->has_opponent_swing = 0;
    s->has_swung = 0;
    s->surplus = GS_NO_BONUSES;
    s->soul_stacks = GS_NO_STACKS;
    s->has_local_swing = 0;
    s->opponent_hard_food_ready = INT_MIN;
    s->opponent_struck_tick = INT_MIN;
    s->opponent_in_reach_since = INT_MIN;
    s->opponent_ate = INT_MIN;
    s->bar = GS_BAR_READ;
    s->damage_since_bar = 0;
    s->unseen_count = 0;
    s->watched_count = 0;
    s->ate_at = s->masked_at = INT_MIN;
    s->opponent_bar = (GsHealthRange){GS_OVERHEALED_HP, GS_OVERHEALED_HP, GS_OVERHEALED_HP};
    s->recoil = GS_RECOIL_UNSEEN;
    s->struck_tick = INT_MIN;
    s->has_struck_damage = 0;
    s->landed_tick = INT_MIN;
}

static void gs_infer_init(GsInfer* s) {
    memset(s, 0, sizeof(*s));
    s->tracker_current = -1;
    s->timed = -1;
    s->opponent = gs_inference_tracked(0);
    s->pid = gs_pid_unknown();
    s->own_hit_lands = INT_MIN;
    s->local_attack_tick = INT_MIN;
    s->attack_tick = INT_MIN;
    s->recoil_charges = 40;
    s->wielded_weapon = INT_MIN;
    s->wielded_since = INT_MIN;
    s->spec_wielded_since = INT_MIN;
    s->swing_reflect = GS_REFLECT_WEAK;
    for (int i = 0; i < 3; i++) s->ready[i] = INT_MIN;
    gs_eater_reset(s);
}

/* ---- the client */
static int gs_prayer_active(const GsClient* c, int prayer) { return (int)(c->prayers >> prayer & 1); }
static int gs_chebyshev(int ax, int ay, int bx, int by) { return imax(abs(ax - bx), abs(ay - by)); }
static int gs_distance(const GsClient* c) { return gs_chebyshev(c->lx, c->ly, c->ox, c->oy); }
static int gs_local_weapon(const GsClient* c) { return c->worn[GS_WORN_WEAPON]; }
static int gs_spec_energy(const GsClient* c) { return c->spec_energy_varp / 10; }

static GsWeapon gs_local_weapon_kind(const GsClient* c, int weapon) { (void)c; return gs_weapon_kind(weapon); }

static GsLevels gs_local_levels_with(const GsClient* c, int strength_level, int ranged_level) {
    double strength = gs_prayer_active(c, GS_PRAYER_PIETY) ? 1.23 : gs_prayer_active(c, GS_PRAYER_CHIVALRY) ? 1.18 : gs_prayer_active(c, GS_PRAYER_ULTIMATE_STRENGTH) ? 1.15 : 1;
    double ranged = gs_prayer_active(c, GS_PRAYER_RIGOUR) ? 1.23 : gs_prayer_active(c, GS_PRAYER_DEADEYE) ? 1.18 : gs_prayer_active(c, GS_PRAYER_EAGLE_EYE) ? 1.15 : 1;
    double magic = gs_prayer_active(c, GS_PRAYER_AUGURY) ? 1.04 : gs_prayer_active(c, GS_PRAYER_MYSTIC_VIGOUR) ? 1.03 : gs_prayer_active(c, GS_PRAYER_MYSTIC_MIGHT) ? 1.02 : 1;
    return (GsLevels){strength_level, ranged_level, strength, ranged, magic};
}
static GsLevels gs_local_levels(const GsClient* c) { return gs_local_levels_with(c, c->strength, c->ranged); }

static GsBonuses gs_local_bonuses(const GsClient* c) {
    GsBonuses bonuses = GS_NO_BONUSES;
    for (int i = 0; i < GS_WORN_SLOTS; i++) if (c->worn[i] > 0) bonuses = gs_bonuses_plus(bonuses, gs_bonuses_of(c->worn[i]));
    return bonuses;
}

static int gs_local_style(int weapon) {
    int style = gs_weapon_style(weapon);
    assert(style >= 0);
    return style;
}
static int gs_local_normal(const GsClient* c, int weapon) { return gs_max_normal(gs_weapon_kind(weapon), gs_local_style(weapon), gs_local_levels(c), gs_local_bonuses(c)); }
static int gs_local_spec(const GsClient* c, int weapon, int* out) {
    return gs_max_spec(gs_weapon_kind(weapon), gs_local_style(weapon), gs_local_levels(c), gs_local_bonuses(c), out);
}
static int gs_local_spec_or_normal(const GsClient* c, int weapon) {
    int spec;
    return gs_local_spec(c, weapon, &spec) ? spec : gs_local_normal(c, weapon);
}

static int gs_protected_damage(const GsClient* c, int style, int damage) {
    int prayer = style == GS_MELEE ? GS_PRAYER_PROTECT_FROM_MELEE : style == GS_RANGED ? GS_PRAYER_PROTECT_FROM_MISSILES : GS_PRAYER_PROTECT_FROM_MAGIC;
    return gs_prayer_active(c, prayer) ? damage * 6 / 10 : damage;
}

/* ---- HealthRange */
static GsHealthRange gs_health_of_bar(int ratio, int scale, int max) {
    if (ratio == 0) return (GsHealthRange){0, 0, max};
    int low = ratio == 1 ? 1 : (max * (ratio - 1) + scale - 2) / (scale - 1);
    int high = imin(max, (max * ratio - 1) / (scale - 1));
    return (GsHealthRange){low, high, max};
}

static GsHealthRange gs_health_healed(GsHealthRange r) {
    int least = 16;
    int ceiling = imax(r.max, 115);
    return (GsHealthRange){imin(r.low + least, r.max), ceiling, ceiling};
}

/* ---- AutoEater: armaments */
static int gs_arrows(GsWeapon w) { return w == GS_W_DARK_BOW ? 2 : 1; }
static int gs_most_per_arrow(const GsArmament* a, int most) { return most / gs_arrows(a->weapon); }

static GsBonuses gs_ammo_bonuses(GsWeapon w) { return (GsBonuses){0, GS_WEAPON_ROWS[w].assumed_ammo, 0}; }

/* AutoEater.armed: the opponent's visible gear with its weapon swapped for weapon_id */
static int gs_armed(const GsClient* c, GsBonuses surplus, int weapon_id, GsArmament* out) {
    int style = gs_weapon_style(weapon_id);
    if (style < 0) return 0;
    GsWeapon weapon = weapon_id == -1 ? GS_W_OTHER : gs_item(weapon_id)->weapon;
    int base = gs_base_speed(weapon_id);
    if (base < 0) return 0;
    GsBonuses bonuses = gs_bonuses_plus(gs_bonuses_plus(GS_ASSUMED_RING, gs_ammo_bonuses(weapon)), surplus);
    int ids[GS_MAX_EQUIPMENT + 1], count = 0;
    for (int slot = 0; slot < c->equipment_count; slot++)
        if (slot != GS_KIT_WEAPON && c->equipment[slot] >= GS_ITEM_OFFSET) ids[count++] = c->equipment[slot] - GS_ITEM_OFFSET;
    if (weapon_id != -1) ids[count++] = weapon_id;
    const GsItem* worn[GS_MAX_EQUIPMENT + 1];
    int worn_count = 0;
    for (int i = 0; i < count; i++) {
        bonuses = gs_bonuses_plus(bonuses, gs_bonuses_of(ids[i]));
        if (gs_item(ids[i])->has_stats) worn[worn_count++] = gs_item(ids[i]);
    }
    GsDamage damage = gs_damage_of(style, worn, worn_count);
    GsArmament a = {.weapon = weapon, .style = style, .interval = gs_speed_of(base, style, GS_PACE_UNKNOWN).shortest, .damage = damage,
        .attack_bonus = gs_sum_attack(damage, worn, worn_count), .bonuses = bonuses};
    a.normal = gs_max_normal(weapon, style, GS_ASSUMED_LEVELS, bonuses);
    a.has_spec = gs_max_spec(weapon, style, GS_ASSUMED_LEVELS, bonuses, &a.spec);
    *out = a;
    return 1;
}

static GsArmament gs_armament_max(GsArmament self, GsArmament other) {
    GsArmament stronger = other.attack_bonus > self.attack_bonus ? other : self;
    GsArmament out = self;
    out.normal = imax(self.normal, other.normal);
    if (self.has_spec) out.spec = imax(self.spec, other.has_spec ? other.spec : 0);
    else { out.has_spec = other.has_spec; out.spec = other.spec; }
    out.damage = stronger.damage;
    out.attack_bonus = stronger.attack_bonus;
    out.bonuses = other.normal > self.normal ? other.bonuses : self.bonuses;
    return out;
}

static GsArmament* gs_arsenal_merge(GsInfer* s, int weapon_id, GsArmament seen) {
    for (int i = 0; i < s->arsenal_count; i++)
        if (s->arsenal_id[i] == weapon_id) {
            s->arsenal[i] = gs_armament_max(s->arsenal[i], seen);
            return &s->arsenal[i];
        }
    assert(s->arsenal_count < GS_ARSENAL_SIZE);
    s->arsenal_id[s->arsenal_count] = weapon_id;
    s->arsenal[s->arsenal_count] = seen;
    return &s->arsenal[s->arsenal_count++];
}

/* AutoEater.armamentOf: the opponent's current weapon recorded in the arsenal */
static GsArmament* gs_armament_of(GsInfer* s) {
    GsArmament seen;
    if (!gs_armed(&s->c, s->surplus, s->c.oweapon, &seen)) return NULL;
    return gs_arsenal_merge(s, s->c.oweapon, seen);
}

static GsArmament* gs_granite_maul(GsInfer* s) {
    for (int i = 0; i < s->arsenal_count; i++) if (s->arsenal[i].weapon == GS_W_GRANITE_MAUL) return &s->arsenal[i];
    return NULL;
}

static int gs_dharok_most(const GsInfer* s, const GsArmament* a) {
    return a->weapon == GS_W_DHAROKS_GREATAXE ? gs_dharok(a->normal, s->opponent_hitpoints.low, GS_ASSUMED_MAX_HP) : a->normal;
}

static int gs_held_by(const GsInfer* s, const GsArmament* a, int tick) {
    return a->weapon == GS_W_SOULREAPER_AXE ? gs_soul_at(s->soul_stacks, tick) : 0;
}

static int gs_armament_most(const GsInfer* s, const GsArmament* a, int stacks) {
    return stacks == 0 ? gs_dharok_most(s, a) : gs_max_behead(GS_ASSUMED_LEVELS, a->bonuses, stacks);
}

static int gs_spec_cost_for(int animation, int weapon) {
    int shown = gs_animation_spec_cost(animation), own = gs_weapon_spec_cost(weapon);
    return shown && own ? own : shown;
}

static int gs_spec_least(const GsArmament* a) { return (int)(GS_WEAPON_ROWS[a->weapon].least * a->normal); }
static int gs_spec_style(const GsArmament* a) { return a->weapon == GS_W_VOIDWAKER ? GS_MAGIC : a->style; }

static double gs_accuracy_scaled(const GsClient* c, const GsArmament* a, double attack_scale, GsDamage defended, double defence_scale) {
    int defence = c->defence;
    int defence_prayer = gs_prayer_active(c, GS_PRAYER_PIETY) || gs_prayer_active(c, GS_PRAYER_RIGOUR) || gs_prayer_active(c, GS_PRAYER_AUGURY) ? 125
        : gs_prayer_active(c, GS_PRAYER_CHIVALRY) ? 120 : gs_prayer_active(c, GS_PRAYER_STEEL_SKIN) ? 115 : 100;
    int own_bonus = 0;
    for (int i = 0; i < GS_WORN_SLOTS; i++)
        if (c->worn[i] > 0 && gs_item(c->worn[i])->has_stats) own_bonus += gs_damage_defence(defended, gs_item(c->worn[i]));
    int attack_level, defence_level;
    switch (a->style) {
        case GS_MELEE:
            attack_level = gs_effective_level(118, 120, 3);
            defence_level = gs_effective_level(defence, defence_prayer, 0);
            break;
        case GS_RANGED:
            attack_level = gs_effective_level(112, 120, 3);
            defence_level = gs_effective_level(defence, defence_prayer, 0);
            break;
        default:
            attack_level = gs_effective_level(99, 125, 3);
            defence_level = gs_effective_magic_defence(c->magic, gs_prayer_active(c, GS_PRAYER_AUGURY) ? 125 : 100, defence, defence_prayer);
            break;
    }
    return gs_chance((int)(gs_roll(attack_level, a->attack_bonus) * attack_scale), (int)(gs_roll(defence_level, own_bonus) * defence_scale));
}

static double gs_armament_accuracy(const GsClient* c, const GsArmament* a) { return gs_accuracy_scaled(c, a, 1, a->damage, 1); }

static double gs_spec_accuracy(const GsClient* c, const GsArmament* a) {
    switch (a->weapon) {
        case GS_W_VOIDWAKER: return 1;
        case GS_W_DRAGON_MACE_BH: return gs_accuracy_scaled(c, a, gs_spec_accuracy_multiplier(a->weapon), a->damage, 0.6);
        case GS_W_VESTAS_LONGSWORD: return gs_accuracy_scaled(c, a, 1, GS_DAMAGE_STAB, 0.25);
        default: return gs_accuracy_scaled(c, a, gs_spec_accuracy_multiplier(a->weapon), a->damage, 1);
    }
}

/* AutoEater.strike */
static GsStrike gs_strike(const GsInfer* s, const GsArmament* a, int stacks) {
    const GsClient* c = &s->c;
    int most = gs_armament_most(s, a, stacks);
    if (stacks == 0) return (GsStrike){0, 0, gs_protected_damage(c, a->style, most), gs_armament_accuracy(c, a)};
    return (GsStrike){0, gs_protected_damage(c, a->style, gs_max_behead_least(most, stacks)), gs_protected_damage(c, a->style, most),
        gs_accuracy_scaled(c, a, 1 + 0.12 * stacks, a->damage, 1)};
}

static int gs_lands_for(int attack_tick, GsStyle style, int distance, GsLead lead) { return gs_lands(attack_tick, style, distance, lead); }

static GsSwing gs_opponent_swing(int attack_tick, GsStyle style, int distance, GsPid pid, int least, int most, double accuracy) {
    return (GsSwing){gs_lands_for(attack_tick, style, distance, gs_opponent_lead(pid)), least, most, accuracy, 1};
}
static GsSwing gs_local_swing_of(int attack_tick, GsStyle style, int distance, GsPid pid, int most, double accuracy) {
    return (GsSwing){gs_lands_for(attack_tick, style, distance, gs_local_lead(pid)), 0, most, accuracy, 1};
}

static GsSwing gs_swing_of(const GsInfer* s, const GsArmament* a, int distance, int stacks, int tick, GsPid pid) {
    GsStrike strike = gs_strike(s, a, stacks);
    return gs_opponent_swing(tick, (GsStyle)a->style, distance, pid, strike.least, gs_most_per_arrow(a, strike.most), strike.accuracy);
}

static GsReflect gs_reflect_of_max(int max) { return max >= 40 ? GS_REFLECT_WORTH : GS_REFLECT_WEAK; }

/* AutoEater.opponentSwing: the animated swing as the pid applies, the numbers read from the client now */
static GsSwing gs_opponent_swing_for(const GsInfer* s, GsPid pid) {
    const GsArmament* a = &s->swing_armament;
    GsSwing swing;
    if (s->swing_spec)
        swing = gs_opponent_swing(s->swing_tick, (GsStyle)a->style, s->swing_distance, pid, gs_protected_damage(&s->c, gs_spec_style(a), gs_spec_least(a)),
            gs_protected_damage(&s->c, gs_spec_style(a), gs_most_per_arrow(a, a->has_spec ? a->spec : a->normal)), gs_spec_accuracy(&s->c, a));
    else swing = gs_swing_of(s, a, s->swing_distance, s->swing_stacks, s->swing_tick, pid);
    swing.arrows = gs_arrows(a->weapon);
    return swing;
}

static GsAttack gs_spec_attack(const GsInfer* s, const GsArmament* a) {
    return (GsAttack){(GsStyle)a->style, gs_protected_damage(&s->c, gs_spec_style(a), gs_spec_least(a)),
        gs_protected_damage(&s->c, gs_spec_style(a), gs_most_per_arrow(a, a->spec)), GS_WEAPON_ROWS[a->weapon].cost, a->interval, gs_spec_accuracy(&s->c, a),
        a->weapon == GS_W_ARMADYL_GODSWORD, gs_arrows(a->weapon)};
}

/* AutoEater.localAccuracy */
static double gs_local_accuracy_dropped(const GsClient* c, int weapon, int level_drop) {
    int style = gs_local_style(weapon);
    const GsItem* ours[GS_WORN_SLOTS];
    int count = 0;
    for (int i = 0; i < GS_WORN_SLOTS; i++)
        if (c->worn[i] > 0) {
            if (!gs_item(c->worn[i])->has_stats) return 1.0;
            ours[count++] = gs_item(c->worn[i]);
        }
    GsDamage damage = gs_damage_of(style, ours, count);
    const GsItem* theirs[GS_MAX_EQUIPMENT];
    int theirs_count = 0;
    for (int i = 0; i < c->equipment_count; i++)
        if (c->equipment[i] >= GS_ITEM_OFFSET) {
            if (!gs_item(c->equipment[i] - GS_ITEM_OFFSET)->has_stats) return 1.0;
            theirs[theirs_count++] = gs_item(c->equipment[i] - GS_ITEM_OFFSET);
        }
    int stance = c->com_mode == 0 ? 3 : 0;
    int defence_bonus = 0;
    for (int i = 0; i < theirs_count; i++) defence_bonus += gs_damage_defence(damage, theirs[i]);
    int attack_bonus = gs_sum_attack(damage, ours, count);
    switch (style) {
        case GS_MELEE: {
            int prayer = gs_prayer_active(c, GS_PRAYER_PIETY) ? 120 : gs_prayer_active(c, GS_PRAYER_CHIVALRY) || gs_prayer_active(c, GS_PRAYER_INCREDIBLE_REFLEXES) ? 115
                : gs_prayer_active(c, GS_PRAYER_IMPROVED_REFLEXES) ? 110 : gs_prayer_active(c, GS_PRAYER_CLARITY_OF_THOUGHT) ? 105 : 100;
            return gs_chance(gs_roll(gs_effective_level(c->attack + level_drop, prayer, stance), attack_bonus), gs_roll(gs_effective_level(120, 125, 3), defence_bonus));
        }
        case GS_RANGED: {
            int prayer = gs_prayer_active(c, GS_PRAYER_RIGOUR) ? 120 : gs_prayer_active(c, GS_PRAYER_DEADEYE) ? 118 : gs_prayer_active(c, GS_PRAYER_EAGLE_EYE) ? 115
                : gs_prayer_active(c, GS_PRAYER_SHARP_EYE) ? 110 : gs_prayer_active(c, GS_PRAYER_HAWK_EYE) ? 105 : 100;
            return gs_chance(gs_roll(gs_effective_level(c->ranged + level_drop, prayer, stance), attack_bonus), gs_roll(gs_effective_level(120, 125, 3), defence_bonus));
        }
        default: {
            int prayer = gs_prayer_active(c, GS_PRAYER_AUGURY) ? 125 : gs_prayer_active(c, GS_PRAYER_MYSTIC_VIGOUR) ? 118
                : gs_prayer_active(c, GS_PRAYER_MYSTIC_MIGHT) ? 115 : gs_prayer_active(c, GS_PRAYER_MYSTIC_LORE) ? 110 : gs_prayer_active(c, GS_PRAYER_MYSTIC_WILL) ? 105 : 100;
            return gs_chance(gs_roll(gs_effective_level(c->magic + level_drop, prayer, 0), attack_bonus), gs_roll(gs_effective_magic_defence(99, 125, 120, 125), defence_bonus));
        }
    }
}

static double gs_local_accuracy(const GsClient* c, int weapon) { return gs_local_accuracy_dropped(c, weapon, 0); }

/* ---- AutoEater: events */
static void gs_eater_opponent_hitsplat(GsInfer* s) { s->bar = GS_BAR_FRESH; }
static void gs_eater_opponent_struck(GsInfer* s, int tick, int amount) {
    s->opponent_struck_tick = tick;
    s->damage_since_bar += amount;
}

static int gs_eater_opponent_warned(const GsInfer* s, int* out) {
    int warned = imax(s->opponent_in_reach_since, s->spec_wielded_since);
    if (s->opponent_in_reach_since != INT_MIN && warned > s->opponent_ate) { *out = warned; return 1; }
    return 0;
}

static GsWarning gs_eater_warning(const GsInfer* s) {
    GsWarning w = {GS_NONE, {0}, 0};
    int warned;
    if (gs_eater_opponent_warned(s, &warned)) w.since = warned;
    w.unseen_count = s->unseen_count;
    assert(s->unseen_count <= 64);
    memcpy(w.unseen, s->unseen, (size_t)s->unseen_count * sizeof(int));
    w.watched_count = s->watched_count;
    memcpy(w.watched, s->watched, (size_t)s->watched_count);
    return w;
}

static void gs_eater_observe(GsInfer* s, int tick) {
    const GsClient* c = &s->c;
    gs_armament_of(s);
    if (c->opponent_animation.source == GS_ANIMATION_OBSERVED && c->opponent_animation.id >= 0 && s->ate_at != tick && s->masked_at != tick) {
        assert(s->unseen_count < GS_UNSEEN_LIMIT);
        s->unseen[s->unseen_count++] = tick;
        s->masked_at = tick;
    }
#ifdef GS_HP_BELIEF
    assert(s->watched_count < GS_WATCHED_LIMIT);
    s->watched[s->watched_count++] = s->ate_at == tick ? GS_SEEN_ATE : s->masked_at == tick ? GS_SEEN_HIDDEN : GS_SEEN_QUIET;
#endif
    if (c->ratio >= 0 && s->bar != GS_BAR_BEHIND_A_MEAL) {
        GsHealthRange shown = gs_health_of_bar(c->ratio, c->scale, GS_ASSUMED_MAX_HP);
        if (s->bar == GS_BAR_FRESH) {
            if (shown.low - (s->opponent_hitpoints.high - s->damage_since_bar) >= GS_UNSEEN_HEAL) s->opponent_ate = tick - 1;
            int kept = 0;
            for (int i = 0; i < s->unseen_count; i++) if (s->unseen[i] >= tick) s->unseen[kept++] = s->unseen[i];
            s->unseen_count = kept;
            s->watched_count = 0;
            s->damage_since_bar = 0;
            s->bar = GS_BAR_READ;
        }
        int overheal = s->opponent_struck_tick >= tick - GS_FOOD_CAP_TICKS ? GS_FIGHTING_OVERHEAL : GS_OVERHEALED_HP;
        s->opponent_hitpoints = c->ratio == c->scale ? (GsHealthRange){shown.low, overheal, overheal} : shown;
        s->opponent_bar = s->opponent_hitpoints;
    }
    int weapon = gs_local_weapon(c);
    if (weapon != s->wielded_weapon) {
        s->wielded_weapon = weapon;
        s->wielded_since = tick;
    }
    int cost = GS_WEAPON_ROWS[gs_weapon_kind(weapon)].cost;
    s->spec_wielded_since = cost ? s->wielded_since : INT_MIN;
    int reach = cost && gs_spec_energy(c) >= cost ? gs_local_spec_or_normal(c, weapon) : gs_local_normal(c, weapon);
    s->opponent_in_reach_since = s->opponent_hitpoints.high > reach ? INT_MIN : s->opponent_in_reach_since <= s->opponent_ate ? tick : s->opponent_in_reach_since;
}

enum { GS_GRANITE_MAUL_SPEC_ANIMATION = 1667 };

static void gs_eater_opponent_animated(GsInfer* s, GsKind kind, int animation, int tick) {
    const GsClient* c = &s->c;
    if (kind == GS_KIND_EAT) {
        s->opponent_hitpoints = gs_health_healed(s->opponent_hitpoints);
        s->bar = GS_BAR_BEHIND_A_MEAL;
        s->opponent_hard_food_ready = tick + 3;
        s->opponent_ate = tick;
        s->ate_at = tick;
    } else {
        assert(s->unseen_count < GS_UNSEEN_LIMIT);
        s->unseen[s->unseen_count++] = tick;
        s->masked_at = tick;
    }
    if (kind != GS_KIND_ATTACK && kind != GS_KIND_INSTANT_SPEC) return;
    GsArmament* armed;
    if (animation == GS_GRANITE_MAUL_SPEC_ANIMATION) {
        armed = gs_granite_maul(s);
        GsArmament seen;
        if (!armed && gs_armed(c, s->surplus, GS_ITEM_GRANITE_MAUL_PLUS, &seen)) armed = gs_arsenal_merge(s, GS_ITEM_GRANITE_MAUL_PLUS, seen);
    } else armed = gs_armament_of(s);
    if (!armed) return;
    int spec_cost = gs_animation_spec_cost(animation);
    s->has_swung = 1;
    s->swung_armament = *armed;
    s->swung_spec = spec_cost != 0;
    s->swung_tick = tick;
    int soul = armed->weapon == GS_W_SOULREAPER_AXE;
    GsSoulStacks before = s->soul_stacks;
    if (soul && kind == GS_KIND_ATTACK) s->soul_stacks = animation == GS_BEHEAD_ANIMATION ? gs_soul_beheaded(tick) : gs_soul_swung(before, tick);
    int stacks = !soul ? 0 : animation == GS_BEHEAD_ANIMATION ? gs_soul_at(before, tick) : gs_soul_at(s->soul_stacks, tick);
    s->swung_stacks = stacks;
    s->swing_stacks = stacks;
    s->swing_reflect = spec_cost ? GS_REFLECT_SPEC : gs_reflect_of_max(gs_most_per_arrow(armed, gs_armament_most(s, armed, stacks)));
    s->has_opponent_swing = 1;
    s->swing_armament = *armed;
    s->swing_spec = spec_cost != 0;
    s->swing_distance = gs_distance(c);
    s->swing_tick = tick;
}

static GsBonuses gs_bonuses_of_style(GsStyle style, int points) {
    switch (style) {
        case GS_MELEE: return (GsBonuses){points, 0, 0};
        case GS_RANGED: return (GsBonuses){0, points, 0};
        default: return (GsBonuses){0, 0, points};
    }
}

static GsArmament gs_armament_plus(GsArmament a, GsBonuses extra) {
    GsBonuses learned = gs_bonuses_plus(a.bonuses, extra);
    a.normal = gs_max_normal(a.weapon, a.style, GS_ASSUMED_LEVELS, learned);
    a.has_spec = gs_max_spec(a.weapon, a.style, GS_ASSUMED_LEVELS, learned, &a.spec);
    a.bonuses = learned;
    return a;
}

static int gs_most_of(const GsInfer* s, const GsArmament* a, int spec, int stacks) {
    return gs_most_per_arrow(a, spec ? a->spec : gs_armament_most(s, a, stacks));
}

/* AutoEater.maxHit */
enum { GS_MAX_SURPLUS = 200 };
static void gs_eater_max_hit(GsInfer* s, int amount) {
    if (!s->has_swung) return;
    GsArmament latest = s->swung_armament;
    int points;
    for (points = -GS_MAX_SURPLUS; points <= GS_MAX_SURPLUS; points++) {
        GsArmament candidate = gs_armament_plus(latest, gs_bonuses_of_style((GsStyle)latest.style, points));
        if (gs_protected_damage(&s->c, latest.style, gs_most_of(s, &candidate, s->swung_spec, s->swung_stacks)) >= amount) break;
    }
    assert(points <= GS_MAX_SURPLUS);
    GsBonuses delta = gs_bonuses_of_style((GsStyle)latest.style, points);
    s->surplus = gs_bonuses_plus(s->surplus, delta);
    for (int i = 0; i < s->arsenal_count; i++) s->arsenal[i] = gs_armament_plus(s->arsenal[i], delta);
    s->swung_armament = gs_armament_plus(latest, delta);
}

static void gs_eater_local_animated(GsInfer* s, GsKind kind, int animation, int tick, GsPid pid) {
    if (kind != GS_KIND_ATTACK && kind != GS_KIND_INSTANT_SPEC) return;
    const GsClient* c = &s->c;
    int weapon = gs_local_weapon(c);
    int cost = gs_animation_spec_cost(animation);
    s->has_local_swing = 1;
    s->local_swing = gs_local_swing_of(tick, (GsStyle)gs_local_style(weapon), gs_distance(c), pid,
        cost ? gs_local_spec_or_normal(c, weapon) : gs_local_normal(c, weapon), cost ? 1 : gs_local_accuracy(c, weapon));
    if (s->local_swing.lands <= s->opponent.immune_through) s->local_swing.accuracy = 0;
}

static void gs_eater_struck(GsInfer* s, int damage, int tick) {
    s->struck_tick = tick;
    s->has_struck_damage = 1;
    s->struck_damage = damage;
}

static int gs_recoil_of(int struck, int echo) {
    for (int damage = struck - 1; damage <= struck + 1; damage++) if (echo == damage / 10 + 1) return 1;
    return 0;
}

static void gs_eater_struck_back(GsInfer* s, int amount, int tick) {
    if (s->has_struck_damage && tick >= s->struck_tick && tick <= s->struck_tick + 1 && gs_recoil_of(s->struck_damage, amount)) {
        s->recoil = GS_RECOIL_SEEN;
        s->has_struck_damage = 0;
    }
}

static void gs_eater_settle(GsInfer* s, int tick) {
    if (s->has_struck_damage && tick > s->struck_tick + 1) {
        s->recoil = GS_RECOIL_UNSEEN;
        s->has_struck_damage = 0;
    }
}

static int gs_consumable_ready_after(int self, int consumed, int tick, int previous) {
    return consumed == 1 || self == 2 || self == consumed ? imax(previous, tick + 3) : previous;
}

/* AutoEater.inventoryChanged: the kinds whose units fell, as a mask */
static int gs_eater_inventory_changed(GsInfer* s, const int units[3], int tick) {
    int changed = 0;
    for (int k = 0; k < 3; k++) if (units[k] < s->inventory_units[k]) changed |= 1 << k;
    for (int k = 0; k < 3; k++)
        for (int consumed = 0; consumed < 3; consumed++)
            if (changed >> consumed & 1) s->ready[k] = gs_consumable_ready_after(k, consumed, tick, s->ready[k]);
    memcpy(s->inventory_units, units, sizeof(s->inventory_units));
    return changed;
}

/* ---- the plugin's handlers */
static int gs_find_interacting(const GsClient* c, int id) {
    for (int i = 0; i < c->player_count; i++) if (c->player_id[i] == id) return c->player_interacting[i];
    return -1;
}

static GsPid gs_pid_at(const GsInfer* s, int tick) { (void)tick; return s->pid.owner; }

static void gs_sync_opponent_timer(GsInfer* s, int observed_tick) {
    if (s->tracker_current != s->timed) {
        s->timed = s->tracker_current;
        s->opponent = gs_inference_tracked(observed_tick);
        s->pid = gs_pid_unknown();
        s->has_damage_since_reading = 0;
        s->has_unprocced = 0;
        s->own_hit_lands = INT_MIN;
        gs_eater_reset(s);
    }
}

static void gs_on_interacting(GsInfer* s, int source, int target) {
    if (source < 0 || target < 0 || source == target) return;
    if (source == 0) s->tracker_current = target;
    else if (s->tracker_current < 0 && target == 0) s->tracker_current = source;
}

static int gs_duel(const GsInfer* s, int attacker, int target) {
    for (int i = 0; i < s->c.player_count; i++)
        if (s->c.player_id[i] != attacker && s->c.player_interacting[i] == target) return 0;
    return 1;
}

static void gs_on_animation(GsInfer* s, int observed_tick, int who, int animation) {
    int local = who == 0;
    if (!local && s->tracker_current != who) return;
    if (animation == -1) return;
    GsKind kind = gs_animation_kind(animation);
    gs_sync_opponent_timer(s, observed_tick);
    int rival = s->timed >= 0;
    const GsClient* c = &s->c;
    if (local) {
        if (rival) gs_eater_local_animated(s, kind, animation, observed_tick, gs_pid_at(s, observed_tick));
        if (kind != GS_KIND_EAT)
            s->local_timer = gs_timer_observe(s->local_timer, kind, observed_tick, gs_speed_after(gs_local_weapon(c), animation, GS_PACE_NOT_RAPID));
        if (kind == GS_KIND_ATTACK || kind == GS_KIND_INSTANT_SPEC) {
            s->local_attack_tick = observed_tick;
            if (s->has_pulse) s->pulse_superseded = 1;
        }
        if (rival && gs_pid_counts(animation, gs_weapon_style(gs_local_weapon(c))) && gs_duel(s, 0, s->timed)) s->pid = gs_pid_local_attacked(s->pid, observed_tick);
        return;
    }
    if (rival && s->timed == who) {
        gs_eater_opponent_animated(s, kind, animation, observed_tick);
        s->opponent = gs_inference_animated(s->opponent, kind, gs_spec_cost_for(animation, c->oweapon), animation == GS_ANIMATION_SPEAR_WALL, observed_tick, gs_speed_after(c->oweapon, animation, GS_PACE_UNKNOWN));
        if (gs_pid_counts(animation, gs_weapon_style(c->oweapon)) && gs_duel(s, s->timed, 0)) s->pid = gs_pid_opponent_attacked(s->pid, observed_tick);
        if (kind == GS_KIND_ATTACK || kind == GS_KIND_INSTANT_SPEC) s->attack_tick = observed_tick;
    }
}

static void gs_on_overhead_text(GsInfer* s, int observed_tick, int who) {
    if (s->tracker_current != who) return;
    gs_sync_opponent_timer(s, observed_tick);
    s->opponent = gs_inference_veng_procced(s->opponent, observed_tick);
}

static void gs_on_surge(GsInfer* s, int observed_tick) {
    gs_sync_opponent_timer(s, observed_tick);
    s->opponent = gs_inference_surged(s->opponent, observed_tick);
}

static void gs_on_graphic(GsInfer* s, int observed_tick, int who) {
    if (s->tracker_current != who) return;
    gs_sync_opponent_timer(s, observed_tick);
    s->opponent = gs_inference_veng_cast(s->opponent, observed_tick);
}

/* GearSwapperPlugin.onAreaSoundEffectPlayed: the cast sound on the tracked player's own tile, not ours */
static void gs_on_sound(GsInfer* s, int observed_tick, int sound, int x, int y) {
    if (s->tracker_current < 0) return;
    if (s->c.ox != x || s->c.oy != y || (s->c.ox == s->c.lx && s->c.oy == s->c.ly)) return;
    gs_on_graphic(s, observed_tick, s->tracker_current);
}

static int gs_parse_int_after(const char* text, const char* prefix, int* out) {
    size_t n = strlen(prefix);
    if (strncmp(text, prefix, n) != 0) return 0;
    *out = atoi(text + n);
    return 1;
}

/* RecoilRing.message: "You can inflict N more point(s) of damage before a ring will shatter" and the shattered line */
static int gs_recoil_reflectable(const GsInfer* s) {
    int ring = s->c.worn[GS_WORN_RING];
    if (ring == GS_ITEM_RING_OF_RECOIL) return s->recoil_charges;
    return ring == GS_ITEM_RING_OF_SUFFERING_RECOIL_0 || ring == GS_ITEM_RING_OF_SUFFERING_RECOIL_1 || ring == GS_ITEM_RING_OF_SUFFERING_RECOIL_2
        || ring == GS_ITEM_RING_OF_SUFFERING_RECOIL_3 ? INT_MAX : 0;
}

static void gs_on_chat(GsInfer* s, const char* text) {
    const char* check = strstr(text, "You can inflict ");
    int charges;
    if (check && gs_parse_int_after(check, "You can inflict ", &charges) && strstr(check, " of damage before a ring will shatter")) s->recoil_charges = charges;
    else if (strstr(text, "Your Ring of Recoil has shattered.")) s->recoil_charges = 40;
}

static int gs_ring_charges_taken(GsInfer* s, int ring, int damage) {
    if (ring == GS_ITEM_RING_OF_RECOIL && damage > 0) s->recoil_charges = imax(0, s->recoil_charges - (damage / 10 + 1));
    return s->recoil_charges;
}

enum { GS_ATTACK_HITSPLAT_ME = 16, GS_ATTACK_HITSPLAT_OTHER = 17, GS_ATTACK_HITSPLAT_MAX_ME = 43 };
static int gs_attack_hitsplat(int type) { return type == GS_ATTACK_HITSPLAT_ME || type == GS_ATTACK_HITSPLAT_OTHER || type == GS_ATTACK_HITSPLAT_MAX_ME; }

static void gs_on_hitsplat(GsInfer* s, int hit_tick, int who, int amount, int type, int mine) {
    if (who != 0 && s->tracker_current != who) return;
    gs_sync_opponent_timer(s, hit_tick);
    int rival = s->timed >= 0;
    int struck = gs_attack_hitsplat(type);
    if (struck && who == 0) gs_ring_charges_taken(s, s->c.worn[GS_WORN_RING], amount);
    if (rival && who == s->timed) gs_eater_opponent_hitsplat(s);
    if (struck && rival && who == s->timed) gs_eater_opponent_struck(s, hit_tick, amount);
    if (struck && rival && who == 0 && gs_duel(s, s->timed, 0)) {
        s->pid = gs_pid_hit_on_local(s->pid, hit_tick, amount);
        gs_eater_struck_back(s, amount, hit_tick);
        if (type == GS_ATTACK_HITSPLAT_MAX_ME) gs_eater_max_hit(s, amount);
    }
    if (struck && rival && who == s->timed && mine && gs_duel(s, 0, s->timed)) {
        s->pid = gs_pid_hit_on_opponent(s->pid, hit_tick, amount);
        s->landed_damage = hit_tick == s->landed_tick ? imax(s->landed_damage, amount) : amount;
        s->landed_tick = hit_tick;
    }
    if (who != 0 && s->timed == who) {
        s->damage_since_reading = (s->has_damage_since_reading ? s->damage_since_reading : 0) + amount;
        s->has_damage_since_reading = 1;
    }
}

static int gs_damage_of_experience(int experience, int combat_level) {
    int per_thirty = 40 + imin(combat_level / 20, 5);
    return (experience * 30 + per_thirty / 2) / per_thirty;
}

/* StatChanged of hitpoints with a gain: our hit on the tracked player */
static void gs_on_hitpoints_gain(GsInfer* s, int observed_tick, int gained, int combat_level) {
    gs_sync_opponent_timer(s, observed_tick);
    if (!(s->timed >= 0 && s->c.local_interacting == s->timed)) return;
    int damage = gs_damage_of_experience(gained, combat_level);
    int style = gs_weapon_style(gs_local_weapon(&s->c));
    if (style < 0) return;
    s->own_hit_damage = damage;
    s->own_hit_lands = gs_local_swing_of(observed_tick, (GsStyle)style, gs_distance(&s->c), gs_pid_at(s, observed_tick), damage, 1).lands;
    s->unprocced = s->has_unprocced ? s->unprocced : s->own_hit_lands;
    s->has_unprocced = 1;
    gs_eater_struck(s, damage, s->own_hit_lands);
}

static void gs_on_inventory(GsInfer* s, int observed_tick, const int units[3]) {
    int kinds = gs_eater_inventory_changed(s, units, observed_tick);
    if (!kinds) return;
    if (!s->has_pulse || s->pulse_tick != observed_tick) {
        s->has_pulse = 1;
        s->pulse_tick = observed_tick;
        s->pulse_before = s->local_timer;
        s->pulse_superseded = s->local_attack_tick >= observed_tick;
        s->pulse_kinds = 0;
    }
    s->pulse_kinds |= kinds;
    if (!s->pulse_superseded) {
        int hard = s->pulse_kinds >> 2 & 1, fast = s->pulse_kinds >> 1 & 1;
        int consumption = hard && fast ? 3 : hard ? 2 : fast ? 1 : 0;
        int delay = GS_CONSUMPTIONS[consumption].delay;
        s->local_timer = gs_timer_eat(s->pulse_before, (GsDelays){delay, delay});
    }
}

static void gs_on_game_tick(GsInfer* s, int tick) {
    s->tick = tick;
    if (s->tracker_current >= 0 && gs_chebyshev(s->c.lx, s->c.ly, s->c.ox, s->c.oy) > GS_TRACKING_MAXIMUM_DISTANCE) s->tracker_current = -1;
    gs_sync_opponent_timer(s, tick);
    if (s->c.tracked >= 0 && s->c.ox == s->c.lx && s->c.oy == s->c.ly) s->opponent = gs_inference_stacked(s->opponent, tick);
    gs_eater_settle(s, tick);
    if (s->has_unprocced && tick > s->unprocced) {
        s->opponent = gs_inference_veng_unprocced(s->opponent, s->unprocced);
        s->has_unprocced = 0;
    }
    if (s->has_damage_since_reading && s->c.ratio >= 0) {
        s->opponent = gs_inference_revealed(s->opponent, gs_consumption_hp(s->c.ratio, s->c.scale), s->damage_since_reading);
        s->has_damage_since_reading = 0;
    }
}

/* ---- the eat situation of judgeLethality, with the settings and stakes given */
static int gs_hash_bucket(int id) { unsigned h = (unsigned)id; return (int)((h ^ (h >> 16)) & 15); }

static int gs_arsenal_order(const GsInfer* s, int* order) {
    int n = s->arsenal_count;
    for (int i = 0; i < n; i++) order[i] = n - 1 - i;
    for (int i = 1; i < n; i++)
        for (int j = i; j > 0 && gs_hash_bucket(s->arsenal_id[order[j - 1]]) > gs_hash_bucket(s->arsenal_id[order[j]]); j--) { int t = order[j]; order[j] = order[j - 1]; order[j - 1] = t; }
    return n;
}

static GsTimer gs_timer_view(GsTimerState t, int tick) {
    return (GsTimer){t.seen, t.seen ? gs_next_attack(t, tick) : 0, t.seen ? t.earliest : 0};
}

/* HiddenWeapon.perAttack */
static double gs_hidden_per_attack(double empty, int hitpoints) {
    return empty * gs_exp(-3.75 * imin(hitpoints, GS_ASSUMED_MAX_HP) / GS_ASSUMED_MAX_HP);
}

static int gs_brew_in_bag(const GsClient* c) {
    for (int i = 0; i < 28; i++) if (c->inventory[i] == 6685 || c->inventory[i] == 6687 || c->inventory[i] == 6689 || c->inventory[i] == 6691) return 1;
    return 0;
}

static int gs_brew_drop(int level) { return level > 1 ? -((int)(level * 0.1) + 2) : 0; }

static double gs_pid_flip_odds(const GsPidState* pid, int tick) {
    static const int FLIPPED[3] = {228, 175, 222}, PAIRED[3] = {1916, 982, 1004};
    if (pid->owner == GS_PID_UNKNOWN) return 0;
    int age = tick - pid->observed_tick;
    int bin = age <= 3 ? 0 : age <= 7 ? 1 : 2;
    return (double)FLIPPED[bin] / PAIRED[bin];
}

static void gs_eat_situation(GsInfer* s, int tick, GsOffence offence, double tolerance, double stakes, GsEatSituation* out) {
    const GsClient* c = &s->c;
    memset(out, 0, sizeof(*out));
    int weapon = gs_local_weapon(c);
    int cost = GS_WEAPON_ROWS[gs_weapon_kind(weapon)].cost;
    int armed = c->sa_attack == 1 && cost && gs_spec_energy(c) >= cost;
    out->tick = tick;
    out->hitpoints = c->hitpoints;
    out->pid = gs_pid_at(s, tick);
    out->distance = gs_distance(c);
    GsTimerState his = s->opponent.timer;
    out->opponent_timer = his.seen ? (GsTimer){1, gs_next_attack(his, tick), his.earliest} : (GsTimer){0, tick + 1, 0};
    out->energy_high = gs_energy_range(&s->opponent.spec, tick).high;
    out->vengeance = gs_vengeance_reading(s->opponent.vengeance, tick);
    out->recoil_seen = s->recoil == GS_RECOIL_SEEN;
    GsTimerState mine = s->local_timer;
    out->local_timer = mine.seen ? (GsTimer){1, gs_next_attack(mine, tick), mine.earliest} : (GsTimer){0, tick + 1, tick + 1};
    int style = gs_local_style(weapon);
    int normal = gs_local_normal(c, weapon);
    int most = armed ? gs_local_spec_or_normal(c, weapon) : normal;
    int base = gs_base_speed(weapon);
    assert(base >= 0);
    double accuracy = gs_local_accuracy(c, weapon);
    out->local_style = (GsStyle)style;
    out->local_most = armed ? most : normal;
    out->local_normal = normal;
    out->local_interval = gs_speed_of(base, style, GS_PACE_NOT_RAPID).shortest;
    out->local_opening_accuracy = armed ? 1 : accuracy;
    out->local_accuracy = accuracy;
    out->opponent_hp = s->opponent_hitpoints;
    out->opponent_bar = s->opponent_bar;
    out->offence = offence;
    out->tolerance = tolerance;
    out->pid_flip = gs_pid_flip_odds(&s->pid, tick);
    out->drained_normal = normal;
    out->drained_most = armed ? out->local_most : normal;
    out->drained_accuracy = accuracy;
    out->drained_opening_accuracy = armed ? 1 : accuracy;
    if (gs_brew_in_bag(c)) {
        int drop_strength = gs_brew_drop(c->strength), drop_ranged = gs_brew_drop(c->ranged);
        int drop_style = style == GS_MELEE ? gs_brew_drop(c->attack) : style == GS_RANGED ? drop_ranged : gs_brew_drop(c->magic);
        GsLevels drained = gs_local_levels_with(c, c->strength + drop_strength, c->ranged + drop_ranged);
        GsBonuses bonuses = gs_local_bonuses(c);
        GsWeapon kind = gs_weapon_kind(weapon);
        int spec;
        out->drained_normal = gs_max_normal(kind, style, drained, bonuses);
        out->drained_most = armed ? (gs_max_spec(kind, style, drained, bonuses, &spec) ? spec : out->drained_normal) : out->drained_normal;
        out->drained_accuracy = gs_local_accuracy_dropped(c, weapon, drop_style);
        out->drained_opening_accuracy = armed ? 1 : out->drained_accuracy;
    }
    out->warning = gs_eater_warning(s);
    out->stakes = stakes;
    if (s->opponent.vengeance.phase == GS_VPHASE_PROCCED && s->opponent.vengeance.proc >= s->landed_tick && s->opponent.vengeance.proc <= s->landed_tick + 1) {
        int lands = s->landed_tick + (out->pid == GS_PID_LOCAL ? 1 : 0);
        if (lands > tick) {
            out->has_owed = 1;
            out->owed = (GsHit){SRC_VENGEANCE_ECHO, lands, s->landed_damage * 3 / 4, s->landed_damage * 3 / 4, 1};
        }
    }
    out->floor = GS_NONE;
    gs_armament_of(s);
    int order[GS_ARSENAL_SIZE];
    int n = gs_arsenal_order(s, order);
    for (int k = 0; k < n; k++) {
        const GsArmament* a = &s->arsenal[order[k]];
        GsStrike strike = gs_strike(s, a, gs_held_by(s, a, tick));
        out->attacks[out->attack_count++] = (GsAttack){(GsStyle)a->style, strike.least, gs_most_per_arrow(a, strike.most), 0, a->interval, strike.accuracy, 0, gs_arrows(a->weapon)};
        if (a->weapon == GS_W_GRANITE_MAUL) {
            out->has_maul = 1;
            out->maul = (GsAttack){GS_MELEE, 0, gs_protected_damage(c, a->style, a->spec), 50, 1, gs_armament_accuracy(c, a), 0, 1};
        } else if (a->has_spec) out->attacks[out->attack_count++] = gs_spec_attack(s, a);
    }
    int has_vw = 0;
    for (int i = 0; i < s->arsenal_count; i++) has_vw |= s->arsenal[i].weapon == GS_W_VOIDWAKER;
    GsArmament hidden;
    if (!has_vw && gs_armed(c, s->surplus, GS_ITEM_VOIDWAKER, &hidden))
        out->hidden[out->hidden_count++] = (GsHidden){0, gs_spec_attack(s, &hidden), gs_hidden_per_attack(0.14, c->hitpoints)};
    if (!gs_granite_maul(s) && gs_armed(c, s->surplus, GS_ITEM_GRANITE_MAUL_PLUS, &hidden))
        out->hidden[out->hidden_count++] = (GsHidden){1, (GsAttack){GS_MELEE, 0, gs_protected_damage(c, hidden.style, hidden.spec), 50, 1, gs_armament_accuracy(c, &hidden), 0, 1},
            gs_hidden_per_attack(0.22, c->hitpoints)};
    if (s->has_opponent_swing) {
        GsSwing swing = gs_opponent_swing_for(s, out->pid);
        int landed = imax(0, imin(swing.arrows, tick - swing.lands + 1));
        if (landed != swing.arrows) {
            out->has_swing = 1;
            out->swing = (GsSwing){swing.lands + landed, swing.least, swing.most, swing.accuracy, swing.arrows - landed};
        }
    }
    if (s->has_local_swing && s->local_swing.lands > tick) { out->has_local_swing = 1; out->local_swing = s->local_swing; }
}

/* ---- AutoEater.incoming and the inferred inputs of the spec decision */
static int gs_living_rival(const GsInfer* s) { return s->timed >= 0 && s->c.ratio != 0; }

static int gs_swing_underway(const GsInfer* s, int tick, GsPid pid, GsIncoming* out) {
    if (!s->has_opponent_swing) return 0;
    GsSwing swing = gs_opponent_swing_for(s, pid);
    if (swing.lands <= tick) return 0;
    *out = (GsIncoming){swing.lands, swing.least, swing.most, swing.accuracy, GS_NONE};
    return 1;
}

static int gs_incoming(GsInfer* s, int tick, GsPid pid, GsIncoming* out) {
    if (gs_swing_underway(s, tick, pid, out)) return 1;
    if (gs_timer_reading(s->opponent.timer, tick) != GS_READING_COOLDOWN) return 0;
    int due = tick + 1 + gs_timer_until_earliest(s->opponent.timer, tick + 1);
    GsArmament* armament = gs_armament_of(s);
    if (!armament) return 0;
    GsSwing next = gs_swing_of(s, armament, gs_distance(&s->c), gs_held_by(s, armament, due), due, pid);
    *out = (GsIncoming){next.lands, next.least, next.most, next.accuracy, due};
    return 1;
}

typedef struct { int certain; int lands, damage; } GsUnderway;

static void gs_spec_inputs(GsInfer* s, int tick, GsSpecSituation* out, GsUnderway* underway) {
    memset(out, 0, sizeof(*out));
    GsPid pid = gs_pid_at(s, tick);
#ifdef GS_HP_BELIEF
    out->hitpoints = s->opponent_bar;
#else
    out->hitpoints = s->opponent_hitpoints;
#endif
    out->hard_food_ready = s->opponent_hard_food_ready;
    out->vengeance = gs_vengeance_reading(s->opponent.vengeance, tick);
    out->pid = pid;
    out->has_incoming = gs_incoming(s, tick, pid, &out->incoming);
    out->recoil_charges = gs_recoil_reflectable(s);
    out->tick = tick;
    out->warning = gs_eater_warning(s);
    out->local = gs_health_of(s->c.hitpoints);
    *underway = (GsUnderway){s->own_hit_lands > tick, s->own_hit_lands, s->own_hit_damage};
}

static int gs_cardinal(const GsClient* c) { return abs(c->lx - c->ox) + abs(c->ly - c->oy) == 1; }

/* AutoSpecialAttack.plan's gates: a Voidwaker wielder adjacent to a target it fights with the energy */
static int gs_spec_plannable(const GsInfer* s) {
    const GsClient* c = &s->c;
    if (!gs_living_rival(s) || gs_weapon_kind(gs_local_weapon(c)) != GS_W_VOIDWAKER) return 0;
    int engaged = c->local_interacting == s->timed;
    return gs_spec_energy(c) >= 50 && gs_cardinal(c) && (engaged || c->tracked_interacting == 0);
}

/* AutoEater.weaponReflect and opponentSpecCost, the inputs of the Vengeance and Bait rules */
static GsReflect gs_weapon_reflect(GsInfer* s, int tick) {
    GsArmament* a = gs_armament_of(s);
    return a ? gs_reflect_of_max(gs_most_per_arrow(a, gs_armament_most(s, a, gs_held_by(s, a, tick)))) : GS_REFLECT_WEAK;
}

static int gs_opponent_spec_cost(const GsInfer* s) {
    int cost = INT_MAX;
    for (int i = 0; i < s->arsenal_count; i++)
        if (s->arsenal[i].has_spec) cost = imin(cost, GS_WEAPON_ROWS[s->arsenal[i].weapon].cost);
    return cost;
}

/* The held ornate maul and the bag */
static void gs_own_of(const GsClient* c, int* maul_id, int* inventory_count, int* shield_worn) {
    *maul_id = 0;
    *inventory_count = 0;
    for (int i = 0; i < 28; i++) {
        if (c->inventory[i] < 0) continue;
        (*inventory_count)++;
        if (!*maul_id && (c->inventory[i] == GS_ITEM_GRANITE_MAUL_PLUS || c->inventory[i] == GS_ITEM_GRANITE_MAUL_PRETTY_PLUS)) *maul_id = c->inventory[i];
    }
    *shield_worn = c->worn[5] > 0;
}
#endif
