/**
 * @file test_player_combat.c
 * @brief Tests for player-side combat primitives in osrs_combat.h.
 *
 * Verifies effective level, attack roll, max hit, prayer reduction,
 * double accuracy, and equipment bonus summation against osrs-dps-calc
 * reference values.
 *
 * Build: cc -std=c11 -O0 -g -I. -o test_player_combat ocean/osrs/tests/test_player_combat.c -lm
 */

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include "ocean/osrs/osrs_combat.h"

static int total_tests = 0;
static int passed_tests = 0;

#define ASSERT_EQ(a, b, msg) do { \
    total_tests++; \
    if ((a) != (b)) { \
        printf("FAIL: %s: got %d, expected %d\n", msg, (int)(a), (int)(b)); \
    } else { passed_tests++; } \
} while(0)

#define ASSERT_FLOAT_NEAR(a, b, tol, msg) do { \
    total_tests++; \
    if (fabsf((float)(a) - (float)(b)) > (float)(tol)) { \
        printf("FAIL: %s: got %.6f, expected %.6f\n", msg, (float)(a), (float)(b)); \
    } else { passed_tests++; } \
} while(0)

/* --- osrs_player_eff_level --- */
static void test_eff_level(void) {
    printf("--- osrs_player_eff_level ---\n");

    /* no prayer, no style bonus: floor(99 * 1.0) + 0 + 8 = 107 */
    ASSERT_EQ(osrs_player_eff_level(99, 1.0f, 0), 107, "base 99, no prayer, no style");

    /* rigour: floor(99 * 1.20) + 0 + 8 = floor(118.8) + 8 = 118 + 8 = 126 */
    ASSERT_EQ(osrs_player_eff_level(99, 1.20f, 0), 126, "base 99, rigour att mult");

    /* augury: floor(99 * 1.25) + 0 + 8 = floor(123.75) + 8 = 123 + 8 = 131 */
    ASSERT_EQ(osrs_player_eff_level(99, 1.25f, 0), 131, "base 99, augury");

    /* piety: floor(99 * 1.20) + 0 + 8 = 126 */
    ASSERT_EQ(osrs_player_eff_level(99, 1.20f, 0), 126, "base 99, piety att");

    /* piety str: floor(99 * 1.23) + 0 + 8 = floor(121.77) + 8 = 121 + 8 = 129 */
    ASSERT_EQ(osrs_player_eff_level(99, 1.23f, 0), 129, "base 99, piety str");

    /* accurate style (+3): floor(99 * 1.0) + 3 + 8 = 110 */
    ASSERT_EQ(osrs_player_eff_level(99, 1.0f, 3), 110, "base 99, accurate +3");

    /* rigour + rapid (+0): floor(99 * 1.20) + 0 + 8 = 126 */
    ASSERT_EQ(osrs_player_eff_level(99, 1.20f, 0), 126, "rigour, rapid");

    /* level 1, no prayer: floor(1 * 1.0) + 0 + 8 = 9 */
    ASSERT_EQ(osrs_player_eff_level(1, 1.0f, 0), 9, "level 1");

    /* boosted level (99+13 = 112 from imbued heart): floor(112*1.25)+8 = 148 */
    ASSERT_EQ(osrs_player_eff_level(112, 1.25f, 0), 148, "boosted 112, augury");
}

/* --- osrs_player_att_roll --- */
static void test_att_roll(void) {
    printf("--- osrs_player_att_roll ---\n");

    /* eff_level * (bonus + 64) */
    ASSERT_EQ(osrs_player_att_roll(107, 0), 107 * 64, "eff 107, bonus 0");
    ASSERT_EQ(osrs_player_att_roll(126, 100), 126 * 164, "eff 126, bonus 100");
    ASSERT_EQ(osrs_player_att_roll(148, 182), 148 * 246, "eff 148, bonus 182 (BIS mage)");
    ASSERT_EQ(osrs_player_att_roll(9, 0), 9 * 64, "eff 9, bonus 0 (level 1)");
}

/* --- osrs_player_melee_max_hit --- */
static void test_melee_max_hit(void) {
    printf("--- osrs_player_melee_max_hit ---\n");

    /* formula: floor((eff * (str + 64) + 320) / 640)
       ref: BaseCalc.ts:107 — floor((effectiveLevel * gearBonus + 320) / 640)
       where gearBonus = str_bonus + 64 */

    /* whip (str 82) + piety str eff 129: floor((129 * (82+64) + 320) / 640) */
    int eff = 129, str = 82;
    int expected = (eff * (str + 64) + 320) / 640;
    ASSERT_EQ(osrs_player_melee_max_hit(129, 82), expected, "whip, piety");

    /* level 1, str 0: floor((9*(0+64)+320)/640) = floor(896/640) = 1 */
    ASSERT_EQ(osrs_player_melee_max_hit(9, 0), (9 * 64 + 320) / 640, "level 1, no gear");
}

/* --- osrs_player_ranged_max_hit --- */
static void test_ranged_max_hit(void) {
    printf("--- osrs_player_ranged_max_hit ---\n");

    /* same formula as melee. rigour str eff = floor(99*1.23)+8 = 129
       ranged str 98 (BIS from spec): floor((129*(98+64)+320)/640) = floor((129*162+320)/640) */
    int eff = 129, str = 98;
    int expected = (eff * (str + 64) + 320) / 640;
    ASSERT_EQ(osrs_player_ranged_max_hit(129, 98), expected, "rigour, BIS ranged str");
}

/* --- osrs_player_magic_max_hit --- */
static void test_magic_max_hit(void) {
    printf("--- osrs_player_magic_max_hit ---\n");

    /* ice barrage base 30, magic dmg bonus 30%: floor(30 * (100 + 30) / 100) = floor(39) = 39 */
    ASSERT_EQ(osrs_player_magic_max_hit(30, 30), 30 * 130 / 100, "barrage, 30% dmg");

    /* trident base: floor(magic_level / 3) - 6 for seas = floor(99/3)-6 = 27
       with 0% bonus: 27 * 100 / 100 = 27 */
    ASSERT_EQ(osrs_player_magic_max_hit(27, 0), 27, "trident, no bonus");

    /* sang staff base: floor(magic_level / 3) - 1 = 32
       with 15% bonus (occult + tormented): floor(32 * 115 / 100) = 36 */
    ASSERT_EQ(osrs_player_magic_max_hit(32, 15), 32 * 115 / 100, "sang, 15% bonus");
}

/* --- osrs_prayer_reduce_damage --- */
static void test_prayer_reduce(void) {
    printf("--- osrs_prayer_reduce_damage ---\n");

    /* PvE: correct prayer blocks 100% */
    ASSERT_EQ(osrs_prayer_reduce_damage(50, PRAYER_PROTECT_MAGIC, ATTACK_STYLE_MAGIC, 0), 0,
              "PvE magic pray vs magic");
    ASSERT_EQ(osrs_prayer_reduce_damage(50, PRAYER_PROTECT_RANGED, ATTACK_STYLE_RANGED, 0), 0,
              "PvE range pray vs range");
    ASSERT_EQ(osrs_prayer_reduce_damage(50, PRAYER_PROTECT_MELEE, ATTACK_STYLE_MELEE, 0), 0,
              "PvE melee pray vs melee");

    /* PvE: wrong prayer passes through */
    ASSERT_EQ(osrs_prayer_reduce_damage(50, PRAYER_PROTECT_MAGIC, ATTACK_STYLE_MELEE, 0), 50,
              "PvE magic pray vs melee");
    ASSERT_EQ(osrs_prayer_reduce_damage(50, PRAYER_NONE, ATTACK_STYLE_MAGIC, 0), 50,
              "PvE no pray vs magic");

    /* PvP: correct prayer reduces by 40% (player takes 60%) */
    ASSERT_EQ(osrs_prayer_reduce_damage(50, PRAYER_PROTECT_MAGIC, ATTACK_STYLE_MAGIC, 1), 30,
              "PvP magic pray vs magic: 50*0.6=30");
    ASSERT_EQ(osrs_prayer_reduce_damage(41, PRAYER_PROTECT_RANGED, ATTACK_STYLE_RANGED, 1), (int)(41 * 0.6f),
              "PvP range pray vs range: 41*0.6");

    /* PvP: wrong prayer passes through */
    ASSERT_EQ(osrs_prayer_reduce_damage(50, PRAYER_PROTECT_MAGIC, ATTACK_STYLE_MELEE, 1), 50,
              "PvP magic pray vs melee");

    /* zero damage stays zero */
    ASSERT_EQ(osrs_prayer_reduce_damage(0, PRAYER_PROTECT_MAGIC, ATTACK_STYLE_MAGIC, 0), 0,
              "zero damage PvE");
    ASSERT_EQ(osrs_prayer_reduce_damage(0, PRAYER_PROTECT_MAGIC, ATTACK_STYLE_MAGIC, 1), 0,
              "zero damage PvP");
}

/* --- osrs_hit_chance_double --- */
static void test_hit_chance_double(void) {
    printf("--- osrs_hit_chance_double ---\n");

    /* osmumten's fang / confliction gauntlets formula.
       when att >= def: 1 - (def+2)(2*def+3) / (6*(att+1)^2)
       when att < def:  att*(4*att+5) / (6*(att+1)*(def+1))
       ref: zul_hit_chance_double in encounter_zulrah.h:782-789 */

    /* large att vs small def: should be near 1.0 */
    float c1 = osrs_hit_chance_double(50000, 1000);
    ASSERT_FLOAT_NEAR(c1, 1.0f, 0.01f, "large att vs small def");

    /* equal rolls: should be higher than single roll at equal */
    float single = osrs_hit_chance(10000, 10000);
    float double_r = osrs_hit_chance_double(10000, 10000);
    total_tests++;
    if (double_r > single) { passed_tests++; }
    else { printf("FAIL: double roll should exceed single at equal rolls\n"); }

    /* zero att: should be 0 */
    ASSERT_FLOAT_NEAR(osrs_hit_chance_double(0, 10000), 0.0f, 0.001f, "zero att");
}

/* --- osrs_sum_equipment_bonuses --- */
static void test_equipment_bonuses(void) {
    printf("--- osrs_sum_equipment_bonuses ---\n");

    /* empty loadout: all zeros */
    uint8_t empty[NUM_GEAR_SLOTS];
    for (int i = 0; i < NUM_GEAR_SLOTS; i++) empty[i] = ITEM_NONE;
    EquipmentBonuses eb;
    osrs_sum_equipment_bonuses(empty, &eb);
    ASSERT_EQ(eb.attack_stab, 0, "empty: stab 0");
    ASSERT_EQ(eb.attack_magic, 0, "empty: magic 0");
    ASSERT_EQ(eb.melee_strength, 0, "empty: melee str 0");
    ASSERT_EQ(eb.attack_speed, 0, "empty: speed 0");

    /* single item: whip in weapon slot */
    uint8_t whip_only[NUM_GEAR_SLOTS];
    for (int i = 0; i < NUM_GEAR_SLOTS; i++) whip_only[i] = ITEM_NONE;
    whip_only[GEAR_SLOT_WEAPON] = ITEM_WHIP;
    osrs_sum_equipment_bonuses(whip_only, &eb);
    const Item* whip = &ITEM_DATABASE[ITEM_WHIP];
    ASSERT_EQ(eb.attack_slash, whip->attack_slash, "whip slash matches DB");
    ASSERT_EQ(eb.melee_strength, whip->melee_strength, "whip str matches DB");
    ASSERT_EQ(eb.attack_speed, whip->attack_speed, "whip speed matches DB");
    ASSERT_EQ(eb.attack_range, whip->attack_range, "whip range matches DB");

    /* full loadout: ancestral hat + occult + kodai wand — verify they sum */
    uint8_t mage_partial[NUM_GEAR_SLOTS];
    for (int i = 0; i < NUM_GEAR_SLOTS; i++) mage_partial[i] = ITEM_NONE;
    mage_partial[GEAR_SLOT_HEAD] = ITEM_ANCESTRAL_HAT;
    mage_partial[GEAR_SLOT_NECK] = ITEM_OCCULT_NECKLACE;
    mage_partial[GEAR_SLOT_WEAPON] = ITEM_KODAI_WAND;
    osrs_sum_equipment_bonuses(mage_partial, &eb);
    int expected_magic = ITEM_DATABASE[ITEM_ANCESTRAL_HAT].attack_magic
                       + ITEM_DATABASE[ITEM_OCCULT_NECKLACE].attack_magic
                       + ITEM_DATABASE[ITEM_KODAI_WAND].attack_magic;
    ASSERT_EQ(eb.attack_magic, expected_magic, "mage partial: magic att sum");
}

int main(void) {
    test_eff_level();
    test_att_roll();
    test_melee_max_hit();
    test_ranged_max_hit();
    test_magic_max_hit();
    test_prayer_reduce();
    test_hit_chance_double();
    test_equipment_bonuses();

    printf("\n=== results: %d/%d passed ===\n", passed_tests, total_tests);
    return (passed_tests == total_tests) ? 0 : 1;
}
