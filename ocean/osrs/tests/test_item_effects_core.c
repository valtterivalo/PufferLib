/**
 * @file test_item_effects_core.c
 * @brief tests for shared passive item effects in osrs_item_effects.h
 *
 * BUILD:
 *   cd pufferlib-metal
 *   cc -std=c11 -O0 -g -I. -o test_item_effects_core \
 *       ocean/osrs/tests/test_item_effects_core.c -lm
 *   ./test_item_effects_core
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ocean/osrs/osrs_item_effects.h"

static int tests_run = 0;
static int tests_passed = 0;
static int tests_failed = 0;

#define ASSERT_INT_EQ(label, actual, expected) do { \
    tests_run++; \
    int _actual = (actual); \
    int _expected = (expected); \
    if (_actual == _expected) { \
        tests_passed++; \
    } else { \
        tests_failed++; \
        printf("  FAIL: %s — got %d, expected %d\n", (label), _actual, _expected); \
    } \
} while (0)

static void clear_loadout(uint8_t loadout[NUM_GEAR_SLOTS]) {
    memset(loadout, ITEM_NONE, NUM_GEAR_SLOTS);
}

static void test_generated_effect_tags(void) {
    printf("--- generated effect tags ---\n");
    ASSERT_INT_EQ(
        "tbow tagged",
        (ITEM_DATABASE[ITEM_TWISTED_BOW].effect_mask & OSRS_ITEM_EFFECT_TWISTED_BOW) != 0,
        1
    );
    ASSERT_INT_EQ(
        "confliction tagged",
        (ITEM_DATABASE[ITEM_CONFLICTION_GAUNTLETS].effect_mask & OSRS_ITEM_EFFECT_CONFLICTION) != 0,
        1
    );
    ASSERT_INT_EQ(
        "virtus tagged",
        (ITEM_DATABASE[ITEM_VIRTUS_ROBE_TOP].effect_mask & OSRS_ITEM_EFFECT_VIRTUS_PIECE) != 0,
        1
    );
    ASSERT_INT_EQ(
        "sang tagged",
        (ITEM_DATABASE[ITEM_SANGUINESTI_STAFF].effect_mask & OSRS_ITEM_EFFECT_SANG_HEAL) != 0,
        1
    );
    ASSERT_INT_EQ(
        "elysian tagged",
        (ITEM_DATABASE[ITEM_ELYSIAN_SPIRIT_SHIELD].effect_mask & OSRS_ITEM_EFFECT_ELYSIAN) != 0,
        1
    );
}

static void test_profile_derivation(void) {
    printf("--- profile derivation ---\n");

    uint8_t loadout[NUM_GEAR_SLOTS];
    clear_loadout(loadout);
    loadout[GEAR_SLOT_WEAPON] = ITEM_EYE_OF_AYAK;
    loadout[GEAR_SLOT_SHIELD] = ITEM_ELYSIAN_SPIRIT_SHIELD;
    loadout[GEAR_SLOT_BODY] = ITEM_VIRTUS_ROBE_TOP;
    loadout[GEAR_SLOT_LEGS] = ITEM_VIRTUS_ROBE_BOTTOM;
    loadout[GEAR_SLOT_HANDS] = ITEM_CONFLICTION_GAUNTLETS;
    loadout[GEAR_SLOT_RING] = ITEM_VENATOR_RING;

    OsrsEquipmentEffectProfile profile;
    osrs_derive_equipment_effect_profile(loadout, &profile);

    ASSERT_INT_EQ("virtus count", profile.virtus_piece_count, 2);
    ASSERT_INT_EQ("confliction effect", osrs_effect_profile_has(&profile, OSRS_ITEM_EFFECT_CONFLICTION), 1);
    ASSERT_INT_EQ("elysian effect", osrs_effect_profile_has(&profile, OSRS_ITEM_EFFECT_ELYSIAN), 1);
    ASSERT_INT_EQ("recoil source none", profile.recoil_source, OSRS_RECOIL_SOURCE_NONE);
    ASSERT_INT_EQ("spec regen normal", profile.spec_regen_mode, OSRS_SPEC_REGEN_MODE_NORMAL);
}

static void test_confliction_state_machine(void) {
    printf("--- confliction state machine ---\n");

    uint8_t loadout[NUM_GEAR_SLOTS];
    clear_loadout(loadout);
    loadout[GEAR_SLOT_WEAPON] = ITEM_EYE_OF_AYAK;
    loadout[GEAR_SLOT_HANDS] = ITEM_CONFLICTION_GAUNTLETS;

    OsrsEquipmentEffectProfile profile;
    OsrsItemEffectState state;
    osrs_derive_equipment_effect_profile(loadout, &profile);
    osrs_item_effect_state_init(&state);

    OsrsTargetRef target_a = { .kind = OSRS_TARGET_NPC, .id = 7 };
    OsrsTargetRef target_b = { .kind = OSRS_TARGET_NPC, .id = 8 };

    OsrsPreparedAttackEffects prepared = osrs_prepare_attack_effects(
        &profile, &state, ITEM_EYE_OF_AYAK, ATTACK_STYLE_MAGIC,
        OSRS_MAGIC_ATTACK_POWERED_STAFF, target_a, 1,
        12000, 30, 0, 0, 99, 99
    );
    ASSERT_INT_EQ("unprimed attack single-roll", prepared.use_double_accuracy, 0);

    osrs_finalize_attack_effects(
        &profile, &state, ITEM_EYE_OF_AYAK, ATTACK_STYLE_MAGIC,
        OSRS_MAGIC_ATTACK_POWERED_STAFF, target_a, 1,
        0, 0, 0, &(uint32_t){123}
    );
    ASSERT_INT_EQ("miss primes confliction", state.confliction_is_primed, 1);

    prepared = osrs_prepare_attack_effects(
        &profile, &state, ITEM_EYE_OF_AYAK, ATTACK_STYLE_MAGIC,
        OSRS_MAGIC_ATTACK_POWERED_STAFF, target_a, 1,
        12000, 30, 0, 0, 99, 99
    );
    ASSERT_INT_EQ("same target double-roll", prepared.use_double_accuracy, 1);

    prepared = osrs_prepare_attack_effects(
        &profile, &state, ITEM_EYE_OF_AYAK, ATTACK_STYLE_MAGIC,
        OSRS_MAGIC_ATTACK_POWERED_STAFF, target_b, 1,
        12000, 30, 0, 0, 99, 99
    );
    ASSERT_INT_EQ("different target no double-roll", prepared.use_double_accuracy, 0);

    prepared = osrs_prepare_attack_effects(
        &profile, &state, ITEM_EYE_OF_AYAK, ATTACK_STYLE_MAGIC,
        OSRS_MAGIC_ATTACK_POWERED_STAFF, target_a, 0,
        12000, 30, 0, 0, 99, 99
    );
    ASSERT_INT_EQ("aoe secondary no double-roll", prepared.use_double_accuracy, 0);

    osrs_finalize_attack_effects(
        &profile, &state, ITEM_EYE_OF_AYAK, ATTACK_STYLE_MAGIC,
        OSRS_MAGIC_ATTACK_POWERED_STAFF, target_a, 1,
        1, 1, 20, &(uint32_t){456}
    );
    ASSERT_INT_EQ("double-roll attack clears prime", state.confliction_is_primed, 0);

    osrs_finalize_attack_effects(
        &profile, &state, ITEM_EYE_OF_AYAK, ATTACK_STYLE_MAGIC,
        OSRS_MAGIC_ATTACK_POWERED_STAFF, target_a, 0,
        0, 0, 0, &(uint32_t){789}
    );
    ASSERT_INT_EQ("aoe secondary miss does not prime", state.confliction_is_primed, 0);
}

static void test_elysian_damage_reduction(void) {
    printf("--- elysian damage reduction ---\n");

    uint8_t loadout[NUM_GEAR_SLOTS];
    clear_loadout(loadout);
    loadout[GEAR_SLOT_SHIELD] = ITEM_ELYSIAN_SPIRIT_SHIELD;

    OsrsEquipmentEffectProfile profile;
    OsrsItemEffectState state;
    osrs_derive_equipment_effect_profile(loadout, &profile);
    osrs_item_effect_state_init(&state);

    uint32_t proc_seed = 4;
    DamageResult proc = osrs_apply_passive_damage_pipeline(
        20, ATTACK_STYLE_MELEE, PRAYER_NONE, 0, 0, 0, &profile, &state, &proc_seed
    );
    ASSERT_INT_EQ("elysian proc flag", proc.elysian_reduced, 1);
    ASSERT_INT_EQ("elysian proc damage", proc.final_damage, 15);

    uint32_t miss_seed = 1;
    DamageResult miss = osrs_apply_passive_damage_pipeline(
        20, ATTACK_STYLE_MELEE, PRAYER_NONE, 0, 0, 0, &profile, &state, &miss_seed
    );
    ASSERT_INT_EQ("elysian miss flag", miss.elysian_reduced, 0);
    ASSERT_INT_EQ("elysian miss damage", miss.final_damage, 20);
}

static void test_lightbearer_regen_timing(void) {
    printf("--- lightbearer regen timing ---\n");

    Player player;
    memset(&player, 0, sizeof(player));
    memset(player.equipped, ITEM_NONE, sizeof(player.equipped));
    osrs_item_effect_state_init(&player.item_effect_state);
    player.special_energy = 90;

    osrs_refresh_player_equipment(&player);
    for (int tick = 0; tick < 49; tick++) {
        osrs_tick_special_regen(&player);
    }
    ASSERT_INT_EQ("normal regen before 50 ticks", player.special_energy, 90);
    osrs_tick_special_regen(&player);
    ASSERT_INT_EQ("normal regen at 50 ticks", player.special_energy, 100);

    player.special_energy = 80;
    player.item_effect_state.special_regen_ticks = 30;
    player.equipped[GEAR_SLOT_RING] = ITEM_LIGHTBEARER;
    osrs_refresh_player_equipment(&player);
    ASSERT_INT_EQ("lightbearer switch resets overlong timer", player.item_effect_state.special_regen_ticks, 0);

    for (int tick = 0; tick < 24; tick++) {
        osrs_tick_special_regen(&player);
    }
    ASSERT_INT_EQ("lightbearer before 25 ticks", player.special_energy, 80);
    osrs_tick_special_regen(&player);
    ASSERT_INT_EQ("lightbearer at 25 ticks", player.special_energy, 90);
}

static void test_recoil_ring_shatters(void) {
    printf("--- recoil ring shatters ---\n");

    Player player;
    memset(&player, 0, sizeof(player));
    memset(player.equipped, ITEM_NONE, sizeof(player.equipped));
    osrs_item_effect_state_init(&player.item_effect_state);
    player.equipped[GEAR_SLOT_RING] = ITEM_RING_OF_RECOIL;
    osrs_refresh_player_equipment(&player);

    ASSERT_INT_EQ("recoil charges init", player.item_effect_state.recoil_charges, RECOIL_MAX_CHARGES);
    osrs_consume_recoil_charges(&player, RECOIL_MAX_CHARGES);
    ASSERT_INT_EQ("recoil charges empty", player.item_effect_state.recoil_charges, 0);
    ASSERT_INT_EQ("recoil ring removed", player.equipped[GEAR_SLOT_RING], ITEM_NONE);
}

int main(void) {
    printf("=== osrs passive item effects tests ===\n");

    test_generated_effect_tags();
    test_profile_derivation();
    test_confliction_state_machine();
    test_elysian_damage_reduction();
    test_lightbearer_regen_timing();
    test_recoil_ring_shatters();

    printf("\n=== results ===\n");
    printf("  run:    %d\n", tests_run);
    printf("  passed: %d\n", tests_passed);
    printf("  failed: %d\n", tests_failed);

    if (tests_failed > 0) {
        printf("\nFAIL\n");
        return 1;
    }

    printf("\nPASS\n");
    return 0;
}
