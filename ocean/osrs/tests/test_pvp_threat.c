#include "../osrs_pvp_threat.h"
#include <stdio.h>

static void test_dharok_health_interval(void) {
    uint8_t equipment[NUM_GEAR_SLOTS];
    memset(equipment, ITEM_NONE, sizeof(equipment));
    equipment[GEAR_SLOT_HEAD] = ITEM_DHAROKS_HELM;
    equipment[GEAR_SLOT_BODY] = ITEM_DHAROKS_PLATEBODY;
    equipment[GEAR_SLOT_LEGS] = ITEM_DHAROKS_PLATELEGS;
    equipment[GEAR_SLOT_WEAPON] = ITEM_DHAROKS_GREATAXE;
    uint8_t original[NUM_GEAR_SLOTS];
    memcpy(original, equipment, sizeof(original));
    int strength = osrs_player_eff_level(118, 1.23f, 3);
    int previous_max = 1000;
    for (int hp = 1; hp <= 121; hp++) {
        OsrsMeleeThreat threat = osrs_melee_threat(equipment, strength, 99, hp, 100);
        assert(threat.normal_max <= previous_max);
        assert(threat.special_max == 0 && threat.instant_special_max == 0);
        assert(threat.normal_plus_instant_max == threat.normal_max);
        previous_max = threat.normal_max;
    }
    OsrsMeleeThreat full = osrs_melee_threat(equipment, strength, 99, 99, 100);
    OsrsMeleeThreat overhealed = osrs_melee_threat(equipment, strength, 99, 121, 100);
    OsrsMeleeThreat low = osrs_melee_threat(equipment, strength, 99, 1, 100);
    assert(full.normal_max == overhealed.normal_max);
    assert(low.normal_max > full.normal_max);
    assert(memcmp(original, equipment, sizeof(original)) == 0);
    equipment[GEAR_SLOT_HEAD] = ITEM_NONE;
    OsrsMeleeThreat missing = osrs_melee_threat(equipment, strength, 99, 1, 100);
    assert(missing.normal_max == full.normal_max);
}

static void test_energy_and_same_tick_specials(void) {
    uint8_t equipment[NUM_GEAR_SLOTS];
    memset(equipment, ITEM_NONE, sizeof(equipment));
    int strength = osrs_player_eff_level(118, 1.23f, 3);
    const int weapons[] = {ITEM_VOIDWAKER, ITEM_GRANITE_MAUL, ITEM_GRANITE_MAUL_ORNATE};
    for (int index = 0; index < 3; index++) {
        equipment[GEAR_SLOT_WEAPON] = weapons[index];
        int cost = osrs_spec_cost(weapons[index]);
        for (int energy = 0; energy <= 100; energy++) {
            OsrsMeleeThreat threat = osrs_melee_threat(equipment, strength, 99, 50, energy);
            int count = energy / cost;
            if (index == 0 && count > 1) count = 1;
            assert(threat.special_count == count);
            assert(threat.special_stack_max == count * threat.special_max);
            if (index == 0) {
                assert(threat.instant_special_max == 0);
                assert(threat.normal_plus_instant_max == threat.normal_max);
                if (count) assert(threat.special_max == threat.normal_max * 3 / 2);
            } else {
                assert(threat.instant_special_max == count * threat.normal_max);
                assert(threat.normal_plus_instant_max == (count + 1) * threat.normal_max);
            }
        }
    }
}

int main(void) {
    test_dharok_health_interval();
    test_energy_and_same_tick_specials();
    puts("pvp threat: passed");
}
