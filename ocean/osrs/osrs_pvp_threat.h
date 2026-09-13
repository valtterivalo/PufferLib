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

#endif
