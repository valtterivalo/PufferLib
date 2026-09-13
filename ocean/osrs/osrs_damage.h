#ifndef OSRS_DAMAGE_H
#define OSRS_DAMAGE_H

#include "osrs_combat.h"
#include "osrs_items.h"

typedef struct {
    int final_damage;
    int veng_damage;
    int recoil_damage;
    int smite_drain;
    int prayer_blocked;
    int elysian_reduced;
} DamageResult;

/** Pure damage chain after prayer/mitigation: vengeance reflect, recoil reflect,
    smite drain. Computes amounts only; the caller applies them to game state. */
static inline DamageResult osrs_apply_post_mitigation_pipeline(
    int mitigated_damage,
    int target_hitpoints,
    int prayer_blocked,
    int target_veng_active,
    int target_has_recoil,
    int attacker_smite_active
) {
    DamageResult r = {0, 0, 0, 0, prayer_blocked, 0};
    r.final_damage = mitigated_damage;

    int damage_taken = mitigated_damage < target_hitpoints ? mitigated_damage : target_hitpoints;

    if (target_veng_active && damage_taken > 0) {
        r.veng_damage = damage_taken * 3 / 4;
        if (r.veng_damage == 0) r.veng_damage = 1;
    }

    if (target_has_recoil && damage_taken > 0) {
        r.recoil_damage = damage_taken / 10 + 1;
    }

    if (attacker_smite_active && r.final_damage > 0) {
        r.smite_drain = r.final_damage / 4;
    }

    return r;
}

#endif /* OSRS_DAMAGE_H */
