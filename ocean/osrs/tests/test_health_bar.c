#include "../osrs_health_bar.h"
#include <stdio.h>

static void test_round_trip(void) {
    const int scales[] = {1, 2, 30, 100};
    for (int base = 1; base <= 121; base++) {
        int cap = base + 22;
        for (int s = 0; s < 4; s++) {
            int scale = scales[s];
            for (int hp = 0; hp <= cap; hp++) {
                int ratio = osrs_health_bar_ratio(hp, base, scale);
                OsrsHealthBarRange range = osrs_health_bar_range(ratio, scale, base, cap);
                assert(range.kind == OSRS_HEALTH_BAR_KNOWN);
                assert(range.lower <= hp && hp <= range.upper);
                assert(osrs_health_bar_ratio(range.lower, base, scale) == ratio);
                assert(osrs_health_bar_ratio(range.upper, base, scale) == ratio);
                if (range.lower > 0)
                    assert(osrs_health_bar_ratio(range.lower - 1, base, scale) != ratio);
                if (range.upper < cap)
                    assert(osrs_health_bar_ratio(range.upper + 1, base, scale) != ratio);
            }
        }
    }
}

static void test_recorded_damage_sequence(void) {
    int matches = 0;
    for (int base = 1; base <= 99; base++) {
        for (int hp = 68; hp <= base; hp++) {
            if (osrs_health_bar_ratio(hp, base, 30) != 24 ||
                    osrs_health_bar_ratio(hp - 28, base, 30) != 16 ||
                    osrs_health_bar_ratio(hp - 28 - 39, base, 30) != 5) continue;
            assert(hp == 81 && (base == 98 || base == 99));
            matches++;
        }
    }
    assert(matches == 2);
    OsrsHealthBarRange twenty_one = osrs_health_bar_range(21, 30, 99, 121);
    assert(twenty_one.kind == OSRS_HEALTH_BAR_KNOWN);
    assert(twenty_one.lower == 69 && twenty_one.upper == 71);
    OsrsHealthBarRange twenty_four = osrs_health_bar_range(24, 30, 99, 121);
    assert(twenty_four.lower == 79 && twenty_four.upper == 81);
    OsrsHealthBarRange full = osrs_health_bar_range(30, 30, 99, 121);
    assert(full.lower == 99 && full.upper == 121);
    assert(osrs_health_bar_ratio(96, 99, 30) == 29);
}

static void test_observation_domains(void) {
    assert(osrs_health_bar_range(-1, 30, 99, 121).kind == OSRS_HEALTH_BAR_MISSING);
    assert(osrs_health_bar_range(-1, -1, 99, 121).kind == OSRS_HEALTH_BAR_MISSING);
    assert(osrs_health_bar_range(0, 0, 99, 121).kind == OSRS_HEALTH_BAR_INVALID);
    assert(osrs_health_bar_range(31, 30, 99, 121).kind == OSRS_HEALTH_BAR_INVALID);
    assert(osrs_health_bar_range(-2, 30, 99, 121).kind == OSRS_HEALTH_BAR_INVALID);
    assert(osrs_health_bar_range(10, 30, 0, 121).kind == OSRS_HEALTH_BAR_INVALID);
    assert(osrs_health_bar_range(10, 30, 99, 98).kind == OSRS_HEALTH_BAR_INVALID);
    assert(osrs_health_bar_range(1, 30, 1, 23).kind == OSRS_HEALTH_BAR_INVALID);
    OsrsHealthBarRange dead = osrs_health_bar_range(0, 30, 99, 121);
    assert(dead.kind == OSRS_HEALTH_BAR_KNOWN && dead.lower == 0 && dead.upper == 0);
}

static void test_recorded_stale_bar_healing_interval(void) {
    OsrsHealthBarRange after_23276 = osrs_health_bar_range(8, 30, 99, 121);
    OsrsHealthBarRange after_23285 = osrs_health_bar_range(25, 30, 99, 121);
    assert(after_23276.upper < 55);
    OsrsHealthChangeRange change = osrs_health_bar_unobserved_change(after_23276, after_23285, 55);
    assert(change.lower == 110 && change.upper == 116);
    for (int before = after_23276.lower; before <= after_23276.upper; before++)
        for (int after = after_23285.lower; after <= after_23285.upper; after++) {
            int gain = after - before + 55;
            assert(gain >= change.lower && gain <= change.upper);
        }
}

int main(void) {
    test_round_trip();
    test_recorded_damage_sequence();
    test_observation_domains();
    test_recorded_stale_bar_healing_interval();
    puts("Health-bar quantization and recorded damage constraints passed");
}
