#include <assert.h>
#include <stdio.h>
#include "ocean/osrs/encounters/encounter_zulrah.h"

static void test_reflection_basis(void) {
    for (int hp = 0; hp <= 121; hp++) {
        for (int damage = 0; damage <= 200; damage++) {
            DamageResult result = osrs_apply_post_mitigation_pipeline(damage, hp, 0, 1, 1, 1);
            int taken = damage < hp ? damage : hp;
            int vengeance = taken * 3 / 4;
            if (taken > 0 && vengeance == 0) vengeance = 1;
            assert(result.final_damage == damage);
            assert(result.veng_damage == vengeance);
            assert(result.recoil_damage == (taken > 0 ? taken / 10 + 1 : 0));
            assert(result.smite_drain == damage / 4);
        }
    }
}

static void test_zulrah_recoil_after_hp_application(void) {
    ZulrahState state = {0};
    state.player.current_hitpoints = 30;
    state.player.base_hitpoints = 99;
    state.player.equipped[GEAR_SLOT_RING] = ITEM_RING_OF_RECOIL;
    osrs_refresh_player_equipment(&state.player);
    state.zulrah.current_hitpoints = 100;
    zul_apply_player_damage(&state, 45, ATTACK_STYLE_MELEE, &state.zulrah);
    assert(state.player.current_hitpoints == 0);
    assert(state.player.hit_damage == 45);
    assert(state.zulrah.current_hitpoints == 96);
    assert(state.player.item_effect_state.recoil_charges == 36);

    state.zulrah.current_hitpoints = 100;
    EncounterPendingHit hit = {.attack_style = ATTACK_STYLE_RANGED};
    zul_player_hit_landed(&state, &hit, 45, 30, 0, 0);
    assert(state.zulrah.current_hitpoints == 96);
    assert(state.player.item_effect_state.recoil_charges == 32);
}

int main(void) {
    test_reflection_basis();
    test_zulrah_recoil_after_hp_application();
    puts("Damage reflection contracts passed");
    return 0;
}
