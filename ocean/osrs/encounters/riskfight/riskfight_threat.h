#ifndef OSRS_RISKFIGHT_THREAT_H
#define OSRS_RISKFIGHT_THREAT_H
/* Threat features: what a player computes about the other side in nanoseconds from item ids, levels and the health bar, written at the end of the observation
 * (RF_THREAT_START). Two blocks, both functions of the same inputs in the sim and in gs_rl_server:
 *
 *   HIS weapons, RF_THREAT_HIS_SLOTS slots of RF_THREAT_HIS_WIDTH: slot 0 the weapon he wields, then the weapons he has swung this fight (certain), then the
 *   weapons the pool says a fighter in his visible gear carries (probability = likelihood the kit holds it, riskfight_pool_prior). Per slot:
 *       0 p            probability the weapon is in his kit: 1 for the wielded and the shown
 *       1 shown        he has swung it this fight
 *       2 max          normal max hit against us / RF_OBSERVATION_DAMAGE_SCALE (a Dharok's at his hitpoints; the bar's lower bound)
 *       3 hit          chance the normal attack hits our current defence
 *       4 speed        ticks between swings / RF_THREAT_SPEED_SCALE
 *       5-7 style      melee, ranged, magic
 *       8 spec_cost    special energy cost / 100, 0 for a weapon with no special
 *       9 spec_max     most the special can deal (all hits) / RF_OBSERVATION_DAMAGE_SCALE
 *       10 spec_hit    chance one accuracy roll of the special lands against our defence (1 for Voidwaker, which rolls none)
 *       11 spec_speed  ticks until his next attack after the special / RF_THREAT_SPEED_SCALE (1: the granite maul's instant special)
 *   OURS, RF_THREAT_OURS_SLOTS slots of RF_THREAT_OURS_WIDTH: the weapon we wield, then the weapons in our bag by item index. Against his visible defence:
 *       0 present, 1 max (a Dharok's at our hitpoints), 2 hit, 3 speed, 4 spec_cost, 5 spec_max, 6 spec_hit.
 *   STACKS, 2 floats at RF_THREAT_STACKS_OFFSET: [0] his Soulreaper stacks / 5 as a viewer counts them, [1] ours / 5. A special with 0 cost and a max (Behead) is told
 *   by spec_max > 0. Max and spec_max include a bolt's effect (ruby, diamond) against the hitpoints the defender has.
 * His stacks are not shown by the client (the wiki puts the counter in the wielder's own special-attack interface): they are counted as SoulStacks.java does,
 * +1 (cap 5) per swing of the axe, all of them gone after a Behead (a swing with the special animation), one lost per 50 ticks since his last axe swing. Stacks
 * he held when the fight began are not known: 0 is assumed. An undetected Behead leaves the estimate too high until the stacks decay.
 *
 * Assumptions a player makes, stated once: his levels are OSRS_THREAT_ASSUMED_* (a maxed account boosted), his prayer piety / rigour / augury by weapon style,
 * his defence level 99 on a defence prayer (x1.25); ours are our real levels, prayer and stance. Magic is the sim's spell (base 30).
 * Our own max and accuracy are in because the opponent's defence now varies with the pool: whether the maul's special or the Voidwaker's is worth the switch
 * depends on the armour he wears, which no input showed before. */
#include "../../osrs_pvp_threat.h"
#include "../../osrs_health_bar.h"
#include "riskfight_pool.h"

enum {
    RF_THREAT_HIS_SLOTS = 5, RF_THREAT_HIS_WIDTH = 12,
    RF_THREAT_OURS_SLOTS = 4, RF_THREAT_OURS_WIDTH = 7,
    RF_THREAT_OURS_OFFSET = RF_THREAT_HIS_SLOTS * RF_THREAT_HIS_WIDTH,
    RF_THREAT_STACKS_OFFSET = RF_THREAT_OURS_OFFSET + RF_THREAT_OURS_SLOTS * RF_THREAT_OURS_WIDTH,
    RF_THREAT_SIZE = RF_THREAT_STACKS_OFFSET + 2,
    RF_THREAT_SPEED_SCALE = 10,
    RF_THREAT_DAMAGE_SCALE = 128,
    RF_THREAT_BASE_HITPOINTS = 99,
    RF_THREAT_OVERHEAL_CAP = 121,
};
static const float RF_THREAT_PRIOR_FLOOR = 0.02f; /* a weapon the pool gives less than this chance does not take a slot */

typedef struct {
    uint8_t item;
    uint8_t shown;
    float p;
    OsrsThreatWeapon w;
} RiskfightThreatSlot;

/** What the viewer knows of the other side's weapons: the swings he has seen (as raw item ids, stable where item indices are not: gs_rl_server rewrites its
 * stat-priced rows every request), and the slots built from them for the gear he wears now. */
typedef struct {
    uint16_t shown_raw[RF_THREAT_HIS_SLOTS];
    int shown_count;
    int built_for_shown, built_stacks;
    int soul_held, soul_tick; /* his Soulreaper stacks as of the last swing with the axe (SoulStacks.java) */
    uint8_t shield_seen, has_shield_seen; /* the last shield he wore: a one-handed weapon he is not holding is rated with it, the two-hander he holds now has pushed it off */
    uint8_t key[NUM_GEAR_SLOTS];
    int valid, count;
    RiskfightThreatSlot slots[RF_THREAT_HIS_SLOTS];
    OsrsDefenceRolls his_defence;
} RiskfightThreat;

/** His Soulreaper stacks as read at fight tick `now`: one lost per 50 ticks since his last axe swing. A swing of tick T is noted at T and read first at T + 1, so
 * `now - 1 - T` ticks have run since it; the sim loses the stack in the step of tick T + 50 (osrs_soul_tick), which is read at T + 51. */
static inline int riskfight_threat_stacks(const RiskfightThreat* t, int now) {
    if (t->soul_held == 0) return 0;
    int left = t->soul_held - (now - 1 - t->soul_tick) / OSRS_SOUL_DECAY_TICKS;
    return left > 0 ? left : 0;
}

/** He swung `weapon` (a sim item index, ITEM_NONE for fists) at fight tick `tick`; `spec` says the swing had a special attack's animation. */
static void riskfight_threat_note_swing(RiskfightThreat* t, uint8_t weapon, int tick, int spec) {
    if (weapon == ITEM_NONE) return;
    if (weapon == ITEM_SOULREAPER_AXE) {
        int held = spec ? 0 : riskfight_threat_stacks(t, tick + 1) + 1;
        t->soul_held = held > OSRS_SOUL_STACKS_MAX ? OSRS_SOUL_STACKS_MAX : held;
        t->soul_tick = tick;
    }
    uint16_t raw = ITEM_DATABASE[weapon].item_id;
    for (int i = 0; i < t->shown_count; i++) if (t->shown_raw[i] == raw) return;
    if (t->shown_count == RF_THREAT_HIS_SLOTS) {
        memmove(t->shown_raw, t->shown_raw + 1, sizeof(t->shown_raw[0]) * (RF_THREAT_HIS_SLOTS - 1));
        t->shown_count--;
    }
    t->shown_raw[t->shown_count++] = raw;
}

/** Rebuilds the slots when what he wears, has shown or holds (stacks) changed since the last build. `equipment` is what the viewer sees of him (ring and ammunition
 * blank), `now` the fight tick the observation is read at. */
static void riskfight_threat_refresh(RiskfightThreat* t, const uint8_t equipment[NUM_GEAR_SLOTS], int now) {
    int stacks = riskfight_threat_stacks(t, now);
    if (t->valid && t->built_for_shown == t->shown_count && t->built_stacks == stacks && !memcmp(t->key, equipment, NUM_GEAR_SLOTS)) return;
    t->built_stacks = stacks;
    if (equipment[GEAR_SLOT_SHIELD] != ITEM_NONE) { t->shield_seen = equipment[GEAR_SLOT_SHIELD]; t->has_shield_seen = 1; }
    memcpy(t->key, equipment, NUM_GEAR_SLOTS);
    t->built_for_shown = t->shown_count;
    t->valid = 1;
    t->count = 0;
    uint8_t items[RF_THREAT_HIS_SLOTS];
    uint8_t shown[RF_THREAT_HIS_SLOTS];
    float p[RF_THREAT_HIS_SLOTS];
    int n = 0;
    uint8_t wielded = equipment[GEAR_SLOT_WEAPON];
    items[n] = wielded; p[n] = 1; shown[n] = 0; n++;
    for (int i = 0; i < t->shown_count; i++) {
        int index = osrs_item_index_from_raw_id(t->shown_raw[i]);
        if (index < 0) { fprintf(stderr, "[threat] swung weapon raw %d has no item row: no threat slot\n", t->shown_raw[i]); continue; }
        int at = -1;
        for (int j = 0; j < n; j++) if (items[j] == index) at = j;
        if (at < 0) { items[n] = (uint8_t)index; p[n] = 1; shown[n++] = 1; at = n - 1; }
        else shown[at] = 1;
    }
    uint8_t prior_items[RF_THREAT_HIS_SLOTS];
    float prior_p[RF_THREAT_HIS_SLOTS];
    int prior_n = riskfight_pool_prior(equipment, prior_items, prior_p, RF_THREAT_HIS_SLOTS);
    for (int i = 0; i < prior_n && n < RF_THREAT_HIS_SLOTS; i++) {
        int known = 0;
        for (int j = 0; j < n; j++) known |= items[j] == prior_items[i];
        if (known || prior_p[i] < RF_THREAT_PRIOR_FLOOR) continue;
        items[n] = prior_items[i]; p[n] = prior_p[i]; shown[n++] = 0;
    }
    for (int i = 0; i < n; i++) {
        t->slots[i].item = items[i];
        t->slots[i].p = p[i];
        t->slots[i].shown = shown[i];
        OsrsThreatActor who = osrs_threat_actor_assumed(osrs_threat_style_of_weapon(items[i]));
        who.soul_stacks = stacks;
        uint8_t worn[NUM_GEAR_SLOTS];
        memcpy(worn, equipment, NUM_GEAR_SLOTS);
        if (worn[GEAR_SLOT_SHIELD] == ITEM_NONE && t->has_shield_seen) worn[GEAR_SLOT_SHIELD] = t->shield_seen;
        t->slots[i].w = osrs_threat_weapon(worn, items[i], &who);
    }
    t->count = n;
    OsrsThreatActor defender = osrs_threat_actor_assumed(ATTACK_STYLE_MELEE);
    t->his_defence = osrs_defence_rolls(equipment, &defender);
}

/** The levels, prayer and stance one of our Players fights with, as the sim's calculate_effective_* read them. */
static OsrsThreatActor riskfight_threat_actor_of(const Player* p) {
    return (OsrsThreatActor){p->current_attack, p->current_strength, p->current_ranged, p->current_magic, p->current_defence,
        (OffensivePrayer)p->offensive_prayer, (FightStyle)p->fight_style, p->soul_stacks};
}

/** The weapons of our bag that take an OURS slot after the wielded one: distinct, ascending by item index, at most RF_THREAT_OURS_SLOTS - 1. Feed every bag weapon to
 * riskfight_threat_note_bag_weapon while the inventory is walked anyway. */
typedef struct { uint8_t item[RF_THREAT_OURS_SLOTS - 1]; int count; } RiskfightBagWeapons;

static inline void riskfight_threat_note_bag_weapon(RiskfightBagWeapons* bag, uint8_t item) {
    int at = 0;
    while (at < bag->count && bag->item[at] < item) at++;
    if (at < bag->count && bag->item[at] == item) return;
    if (at == RF_THREAT_OURS_SLOTS - 1) return; /* four slots: the wielded weapon and the three lowest item indices of the bag */
    int last = bag->count < RF_THREAT_OURS_SLOTS - 1 ? bag->count : RF_THREAT_OURS_SLOTS - 2;
    for (int i = last; i > at; i--) bag->item[i] = bag->item[i - 1];
    bag->item[at] = item;
    if (bag->count < RF_THREAT_OURS_SLOTS - 1) bag->count++;
}

/** The two stack floats alone (the scripts read our own from them, see riskfight_write_script_observation). `out` is the start of the threat block. */
static inline void riskfight_threat_write_stacks(const Player* p, const RiskfightThreat* t, float* out) {
    out[RF_THREAT_STACKS_OFFSET] = (float)t->built_stacks / OSRS_SOUL_STACKS_MAX;
    out[RF_THREAT_STACKS_OFFSET + 1] = (float)p->soul_stacks / OSRS_SOUL_STACKS_MAX;
}

/** Writes the RF_THREAT_SIZE floats of agent `p` against the other side `t` (its slots, its health bar). `bag` holds the weapons of p's inventory. */
static void riskfight_threat_write(const Player* p, const RiskfightBagWeapons* bag, const RiskfightThreat* t, int bar_ratio, float* out) {
    memset(out, 0, RF_THREAT_SIZE * sizeof(float));
    OsrsThreatActor us = riskfight_threat_actor_of(p);
    /* What we wear, summed: the Player's cache when it is current, else summed here. */
    EquipmentBonuses worn;
    int dharok_armour;
    uint8_t wielded = p->equipped[GEAR_SLOT_WEAPON];
    if (!p->slot_gear_dirty) {
        worn = osrs_equipment_bonuses_of_gear(&p->slot_cached_bonuses);
        dharok_armour = p->equipment_effect_profile.dharok_piece_count;
    } else {
        osrs_sum_equipment_bonuses(p->equipped, &worn);
        OsrsEquipmentEffectProfile effects;
        osrs_derive_equipment_effect_profile(p->equipped, &effects);
        dharok_armour = effects.dharok_piece_count;
    }
    if (wielded != ITEM_NONE && (ITEM_DATABASE[wielded].effect_mask & OSRS_ITEM_EFFECT_DHAROK_PIECE)) dharok_armour--;
    OsrsDefenceRolls ours = osrs_defence_rolls_from(&worn, &us);
    int his_hp_lower = -1, his_hp_upper = -1;
    for (int i = 0; i < t->count; i++) {
        const RiskfightThreatSlot* s = &t->slots[i];
        const OsrsThreatWeapon* w = &s->w;
        float* o = out + i * RF_THREAT_HIS_WIDTH;
        o[0] = s->p;
        o[1] = s->shown;
        int max_hit = w->max_hit;
        if (w->dharok) {
            if (his_hp_lower < 0) {
                OsrsHealthBarRange bar = osrs_health_bar_range(bar_ratio, OSRS_PLAYER_HEALTH_BAR_SCALE, RF_THREAT_BASE_HITPOINTS, RF_THREAT_OVERHEAL_CAP);
                his_hp_lower = bar.kind == OSRS_HEALTH_BAR_KNOWN ? bar.lower : RF_THREAT_BASE_HITPOINTS;
            }
            max_hit = osrs_threat_max_hit_at(w, RF_THREAT_BASE_HITPOINTS, his_hp_lower);
        }
        o[2] = (float)osrs_threat_effect_max(w, max_hit, p->current_hitpoints) / RF_THREAT_DAMAGE_SCALE;
        o[3] = osrs_threat_hit_chance(w, &ours);
        o[4] = (float)w->speed / RF_THREAT_SPEED_SCALE;
        o[4 + w->style] = 1; /* style 1..3 -> 5..7 */
        if (w->spec_hits > 0) {
            o[8] = w->spec_cost / 100.0f;
            o[9] = (float)osrs_threat_effect_max(w, w->spec_max, p->current_hitpoints) / RF_THREAT_DAMAGE_SCALE;
            o[10] = osrs_threat_spec_hit_chance(w, &ours);
            o[11] = (float)w->spec_speed / RF_THREAT_SPEED_SCALE;
        }
    }
    /* Ours: the wielded weapon, then the bag's weapons in item order. */
    float* block = out + RF_THREAT_OURS_OFFSET;
    for (int i = 0; i <= bag->count; i++) {
        uint8_t item = i == 0 ? wielded : bag->item[i - 1];
        OsrsThreatWeapon w = osrs_threat_weapon_swapped(&worn, dharok_armour, p->equipped, item, &us);
        float* o = block + i * RF_THREAT_OURS_WIDTH;
        o[0] = 1;
        if (w.style == ATTACK_STYLE_RANGED && his_hp_upper < 0) {
            OsrsHealthBarRange bar = osrs_health_bar_range(bar_ratio, OSRS_PLAYER_HEALTH_BAR_SCALE, RF_THREAT_BASE_HITPOINTS, RF_THREAT_OVERHEAL_CAP);
            his_hp_upper = bar.kind == OSRS_HEALTH_BAR_KNOWN ? bar.upper : RF_THREAT_OVERHEAL_CAP;
        }
        o[1] = (float)osrs_threat_effect_max(&w, osrs_threat_max_hit_at(&w, RF_THREAT_BASE_HITPOINTS, p->current_hitpoints), his_hp_upper) / RF_THREAT_DAMAGE_SCALE;
        o[2] = osrs_threat_hit_chance(&w, &t->his_defence);
        o[3] = (float)w.speed / RF_THREAT_SPEED_SCALE;
        if (w.spec_hits > 0) {
            o[4] = w.spec_cost / 100.0f;
            o[5] = (float)osrs_threat_effect_max(&w, w.spec_max, his_hp_upper) / RF_THREAT_DAMAGE_SCALE;
            o[6] = osrs_threat_spec_hit_chance(&w, &t->his_defence);
        }
    }
    riskfight_threat_write_stacks(p, t, out);
}
#endif
