#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include "ocean/osrs/osrs_env.h"

static int tests_run = 0;
static int tests_failed = 0;

static float reference_expected_reduced_uniform_damage(
    int low,
    int high,
    int target_prayer,
    AttackStyle style
) {
    if (high < low) return 0.0f;
    float total = 0.0f;
    for (int damage = low; damage <= high; damage++) {
        total += (float)osrs_prayer_reduce_damage(damage, target_prayer, style, 1);
    }
    return total / (float)(high - low + 1);
}

static void assert_float_eq(const char* label, float actual, float expected) {
    tests_run++;
    if (actual == expected) return;
    tests_failed++;
    printf("FAIL %s actual=%.9g expected=%.9g\n", label, actual, expected);
}

static void test_expected_reduced_uniform_damage_matches_reference(void) {
    char label[128];
    for (int prayer = PRAYER_NONE; prayer <= PRAYER_REDEMPTION; prayer++) {
        for (int style = ATTACK_STYLE_NONE; style <= ATTACK_STYLE_MAGIC; style++) {
            for (int low = 0; low <= 128; low++) {
                for (int high = low; high <= 128; high++) {
                    float actual = pvp_expected_reduced_uniform_damage(
                        low, high, prayer, (AttackStyle)style);
                    float expected = reference_expected_reduced_uniform_damage(
                        low, high, prayer, (AttackStyle)style);
                    snprintf(label, sizeof(label), "uniform p%d s%d %d..%d",
                        prayer, style, low, high);
                    assert_float_eq(label, actual, expected);
                }
            }
        }
    }
}

static void test_expected_reduced_uniform_damage_fallbacks_match_reference(void) {
    const struct {
        int low;
        int high;
        int prayer;
        AttackStyle style;
    } cases[] = {
        {-3, 4, PRAYER_PROTECT_MAGIC, ATTACK_STYLE_MAGIC},
        {500, 520, PRAYER_PROTECT_RANGED, ATTACK_STYLE_RANGED},
        {0, 8, 99, ATTACK_STYLE_MELEE},
        {0, 8, PRAYER_PROTECT_MELEE, 99},
        {7, 3, PRAYER_PROTECT_MAGIC, ATTACK_STYLE_MAGIC},
    };

    for (int i = 0; i < (int)(sizeof(cases) / sizeof(cases[0])); i++) {
        float actual = pvp_expected_reduced_uniform_damage(
            cases[i].low, cases[i].high, cases[i].prayer, cases[i].style);
        float expected = reference_expected_reduced_uniform_damage(
            cases[i].low, cases[i].high, cases[i].prayer, cases[i].style);
        assert_float_eq("uniform fallback", actual, expected);
    }
}

int main(void) {
    test_expected_reduced_uniform_damage_matches_reference();
    test_expected_reduced_uniform_damage_fallbacks_match_reference();
    if (tests_failed) {
        printf("%d/%d tests failed\n", tests_failed, tests_run);
        return 1;
    }
    printf("%d/%d tests passed\n", tests_run, tests_run);
    return 0;
}
