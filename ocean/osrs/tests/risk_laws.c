/* The law of one attack as a viewer computes it from what both fighters show (osrs_pvp_threat.h, the threat block's own math): for scripts/measure_risk_ledger.py,
 * so the kill chances it reads off real and sim duels come from the engine and nowhere else.
 *
 *   cc -std=gnu11 -O2 -I. -o risk_laws ocean/osrs/tests/risk_laws.c -lm
 *   risk_laws < queries > laws
 *
 * A query line: `weapon a0 .. a11 d0 .. d11`, the attacker's weapon (raw item id, -1 bare-handed) and the two compositions (spectator.duels E lines: head cape neck
 * weapon body shield arms legs hair hands feet jaw, raw item ids, -1 empty). The attacker wears his composition with `weapon` wielded (a two-hander takes the shield
 * off), at the assumed levels and prayer of osrs_threat_actor_assumed; the defender wears his on the assumed melee actor's defence (osrs_threat's his_defence).
 *
 * Answer line, in query order: `unknown_weapon` when the weapon's raw id has no item row, else
 *   `hit max dharok speed spec_cost spec_hits spec_hit spec_lo spec_hi unknown_items [max_1 .. max_121]`
 * hit the normal attack's chance to land, max its max hit (a Dharok's at full hitpoints), dharok 1 when the set's bonus applies and then max_hp for the attacker's
 * hitpoints 1..121 follow; spec_* the special (spec_hits 0: none; spec_hit the chance one roll lands, 1 for the Voidwaker; a landed single-hit special deals uniform
 * spec_lo..spec_hi); unknown_items the composition slots whose raw id has no item row, worn as empty. */
#include "../encounters/encounter_riskfight.h"

static int load(const int comp[12], uint16_t equipment[NUM_GEAR_SLOTS]) {
    static const int GEAR_OF_COMP[12] = {GEAR_SLOT_HEAD, GEAR_SLOT_CAPE, GEAR_SLOT_NECK, GEAR_SLOT_WEAPON, GEAR_SLOT_BODY, GEAR_SLOT_SHIELD, -1, GEAR_SLOT_LEGS, -1,
        GEAR_SLOT_HANDS, GEAR_SLOT_FEET, -1};
    int unknown = 0;
    memset(equipment, ITEM_NONE, NUM_GEAR_SLOTS * sizeof(uint16_t));
    for (int k = 0; k < 12; k++) {
        if (GEAR_OF_COMP[k] < 0 || comp[k] < 0) continue;
        int item = osrs_item_index_from_raw_id(comp[k]);
        if (item < 0) { unknown++; continue; }
        equipment[GEAR_OF_COMP[k]] = (uint8_t)item;
    }
    return unknown;
}

int main(void) {
    int weapon_raw, a[12], d[12];
    for (;;) {
        if (scanf("%d", &weapon_raw) != 1) break;
        for (int k = 0; k < 12; k++) if (scanf("%d", &a[k]) != 1) { fprintf(stderr, "risk_laws: short query\n"); return 2; }
        for (int k = 0; k < 12; k++) if (scanf("%d", &d[k]) != 1) { fprintf(stderr, "risk_laws: short query\n"); return 2; }
        int weapon = weapon_raw < 0 ? ITEM_NONE : osrs_item_index_from_raw_id(weapon_raw);
        if (weapon < 0) { printf("unknown_weapon\n"); continue; }
        uint16_t att[NUM_GEAR_SLOTS], def[NUM_GEAR_SLOTS];
        int unknown = load(a, att) + load(d, def);
        OsrsThreatActor who = osrs_threat_actor_assumed(osrs_threat_style_of_weapon((uint8_t)weapon));
        OsrsThreatWeapon w = osrs_threat_weapon(att, (uint8_t)weapon, &who);
        OsrsThreatActor defender = osrs_threat_actor_assumed(ATTACK_STYLE_MELEE);
        OsrsDefenceRolls rolls = osrs_defence_rolls(def, &defender);
        float spec_hit = w.spec_hits > 0 ? osrs_threat_spec_hit_chance(&w, &rolls) : 0;
        int spec_lo = weapon == ITEM_VOIDWAKER ? osrs_voidwaker_min_hit(w.max_hit) : w.spec_min;
        printf("%.6f %d %d %d %d %d %.6f %d %d %d", osrs_threat_hit_chance(&w, &rolls), w.max_hit, w.dharok, w.speed, w.spec_cost, w.spec_hits, spec_hit,
            spec_lo, w.spec_max, unknown);
        if (w.dharok) for (int hp = 1; hp <= 121; hp++) printf(" %d", osrs_threat_max_hit_at(&w, 99, hp));
        printf("\n");
    }
    return 0;
}
