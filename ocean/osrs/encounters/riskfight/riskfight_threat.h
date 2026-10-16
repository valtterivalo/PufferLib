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
    RF_THREAT_SIZE = RF_THREAT_OURS_OFFSET + RF_THREAT_OURS_SLOTS * RF_THREAT_OURS_WIDTH,
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
    int built_for_shown;
    uint8_t key[NUM_GEAR_SLOTS];
    int valid, count;
    RiskfightThreatSlot slots[RF_THREAT_HIS_SLOTS];
    OsrsDefenceRolls his_defence;
} RiskfightThreat;

/** He swung `weapon` (a sim item index, ITEM_NONE for fists). A sixth distinct weapon in one fight pushes out the oldest: no fight of the 159 tapes shows more than four. */
static void riskfight_threat_note_swing(RiskfightThreat* t, uint8_t weapon) {
    if (weapon == ITEM_NONE) return;
    uint16_t raw = ITEM_DATABASE[weapon].item_id;
    for (int i = 0; i < t->shown_count; i++) if (t->shown_raw[i] == raw) return;
    if (t->shown_count == RF_THREAT_HIS_SLOTS) {
        memmove(t->shown_raw, t->shown_raw + 1, sizeof(t->shown_raw[0]) * (RF_THREAT_HIS_SLOTS - 1));
        t->shown_count--;
    }
    t->shown_raw[t->shown_count++] = raw;
}

/** Rebuilds the slots when what he wears or has shown changed since the last build. `equipment` is what the viewer sees of him (ring and ammunition blank). */
static void riskfight_threat_refresh(RiskfightThreat* t, const uint8_t equipment[NUM_GEAR_SLOTS]) {
    if (t->valid && t->built_for_shown == t->shown_count && !memcmp(t->key, equipment, NUM_GEAR_SLOTS)) return;
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
    int wielded_slot = 0;
    for (int i = 0; i < t->shown_count; i++) {
        int index = osrs_item_index_from_raw_id(t->shown_raw[i]);
        if (index < 0) { fprintf(stderr, "[threat] swung weapon raw %d has no item row: no threat slot\n", t->shown_raw[i]); continue; }
        int at = -1;
        for (int j = 0; j < n; j++) if (items[j] == index) at = j;
        if (at < 0) { items[n] = (uint8_t)index; p[n] = 1; shown[n++] = 1; at = n - 1; }
        else shown[at] = 1;
        (void)wielded_slot;
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
        t->slots[i].w = osrs_threat_weapon(equipment, items[i], &who);
    }
    t->count = n;
    OsrsThreatActor defender = osrs_threat_actor_assumed(ATTACK_STYLE_MELEE);
    t->his_defence = osrs_defence_rolls(equipment, &defender);
}

/** The levels, prayer and stance one of our Players fights with, as the sim's calculate_effective_* read them. */
static OsrsThreatActor riskfight_threat_actor_of(const Player* p) {
    return (OsrsThreatActor){p->current_attack, p->current_strength, p->current_ranged, p->current_magic, p->current_defence,
        (OffensivePrayer)p->offensive_prayer, (FightStyle)p->fight_style};
}

/** Writes the RF_THREAT_SIZE floats of agent `p` against the other side `t` (its slots, its health bar). */
static void riskfight_threat_write(const Player* p, const OsrsInventoryCell* cells, const RiskfightThreat* t, int bar_ratio, float* out) {
    memset(out, 0, RF_THREAT_SIZE * sizeof(float));
    OsrsThreatActor us = riskfight_threat_actor_of(p);
    OsrsDefenceRolls ours = osrs_defence_rolls(p->equipped, &us);
    OsrsHealthBarRange bar = osrs_health_bar_range(bar_ratio, OSRS_PLAYER_HEALTH_BAR_SCALE, RF_THREAT_BASE_HITPOINTS, RF_THREAT_OVERHEAL_CAP);
    int his_hp_lower = bar.kind == OSRS_HEALTH_BAR_KNOWN ? bar.lower : RF_THREAT_BASE_HITPOINTS;
    for (int i = 0; i < t->count; i++) {
        const RiskfightThreatSlot* s = &t->slots[i];
        const OsrsThreatWeapon* w = &s->w;
        float* o = out + i * RF_THREAT_HIS_WIDTH;
        o[0] = s->p;
        o[1] = s->shown;
        o[2] = (float)osrs_threat_max_hit_at(w, RF_THREAT_BASE_HITPOINTS, his_hp_lower) / RF_THREAT_DAMAGE_SCALE;
        o[3] = osrs_threat_hit_chance(w, &ours);
        o[4] = (float)w->speed / RF_THREAT_SPEED_SCALE;
        o[4 + w->style] = 1; /* style 1..3 -> 5..7 */
        if (w->spec_cost > 0) {
            o[8] = w->spec_cost / 100.0f;
            o[9] = (float)w->spec_max / RF_THREAT_DAMAGE_SCALE;
            o[10] = osrs_threat_spec_hit_chance(w, &ours);
            o[11] = (float)w->spec_speed / RF_THREAT_SPEED_SCALE;
        }
    }
    /* Ours: the wielded weapon, then the bag's weapons in item order. */
    uint8_t weapons[RF_THREAT_OURS_SLOTS];
    int n = 0;
    weapons[n++] = p->equipped[GEAR_SLOT_WEAPON];
    for (int item = 0; item < ITEM_NONE && n < RF_THREAT_OURS_SLOTS; item++) {
        if (item == weapons[0]) continue;
        for (int slot = 0; slot < OSRS_INVENTORY_SIZE; slot++) {
            const OsrsItemContentMetadata* m = osrs_inventory_cell_metadata(&cells[slot]);
            if (m->gear_slot == GEAR_SLOT_WEAPON && m->item_idx == item) { weapons[n++] = (uint8_t)item; break; }
        }
    }
    float* block = out + RF_THREAT_OURS_OFFSET;
    for (int i = 0; i < n; i++) {
        OsrsThreatWeapon w = osrs_threat_weapon(p->equipped, weapons[i], &us);
        float* o = block + i * RF_THREAT_OURS_WIDTH;
        o[0] = 1;
        o[1] = (float)osrs_threat_max_hit_at(&w, RF_THREAT_BASE_HITPOINTS, p->current_hitpoints) / RF_THREAT_DAMAGE_SCALE;
        o[2] = osrs_threat_hit_chance(&w, &t->his_defence);
        o[3] = (float)w.speed / RF_THREAT_SPEED_SCALE;
        if (w.spec_cost > 0) {
            o[4] = w.spec_cost / 100.0f;
            o[5] = (float)w.spec_max / RF_THREAT_DAMAGE_SCALE;
            o[6] = osrs_threat_spec_hit_chance(&w, &t->his_defence);
        }
    }
}
#endif
