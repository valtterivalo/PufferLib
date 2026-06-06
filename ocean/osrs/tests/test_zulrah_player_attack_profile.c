#include "../encounters/encounter_zulrah.h"
#include <stdio.h>

static int tests_run = 0;
static int tests_failed = 0;

static void expect_int(const char* name, int got, int want) {
    tests_run++;
    if (got != want) {
        tests_failed++;
        printf("FAIL: %s got %d expected %d\n", name, got, want);
    }
}

static void init_zulrah_state(ZulrahState* s, int gear_tier) {
    memset(s, 0, sizeof(*s));
    s->gear_tier_mode = ZUL_GEAR_TIER_FIXED;
    s->gear_tier_fixed = gear_tier;
    s->gear_tier = gear_tier;
    zul_reset((EncounterState*)s, 1234);
}

static void test_zulrah_cached_loadout_stats_use_shared_profile(void) {
    ZulrahState s;
    init_zulrah_state(&s, 0);

    OsrsPlayerAttackProfile mage_profile =
        osrs_player_attack_profile_for_loadout(
            ZUL_MAGE_LOADOUT[0],
            ATTACK_STYLE_MAGIC,
            FIGHT_STYLE_ACCURATE,
            30);
    OsrsPlayerAttackProfile range_profile =
        osrs_player_attack_profile_for_loadout(
            ZUL_RANGE_LOADOUT[0],
            ATTACK_STYLE_RANGED,
            FIGHT_STYLE_RAPID,
            0);

    expect_int("zulrah mage attack speed", s.mage_stats.attack_speed, mage_profile.cycle_ticks);
    expect_int("zulrah mage attack range", s.mage_stats.attack_range, mage_profile.range);
    expect_int("zulrah range attack speed", s.range_stats.attack_speed, range_profile.cycle_ticks);
    expect_int("zulrah range attack range", s.range_stats.attack_range, range_profile.range);
}

static void test_zulrah_msb_special_uses_shared_profile_timer(void) {
    ZulrahState s;
    init_zulrah_state(&s, 0);
    s.player_gear = ZUL_GEAR_RANGE;
    encounter_apply_loadout(&s.player, ZUL_RANGE_LOADOUT[0], GEAR_RANGED);
    s.player.fight_style = FIGHT_STYLE_RAPID;
    s.player.attack_timer = 0;
    s.player.special_energy = 100;

    int weapon = s.player.equipped[GEAR_SLOT_WEAPON];
    OsrsPlayerAttackProfile profile =
        osrs_player_attack_profile_for_special(
            (uint8_t)weapon,
            ATTACK_STYLE_RANGED,
            s.player.fight_style,
            weapon,
            (SpecResult){0});

    zul_player_spec(&s);

    expect_int("zulrah msb special timer", s.player.attack_timer, profile.cycle_ticks);
    expect_int("zulrah msb special visual style", s.player.attack_style_this_tick, profile.visual_style);
    expect_int("zulrah msb special damage style", s.player.last_attack_style, profile.damage_style);
}

static void test_zulrah_eye_of_ayak_special_uses_override_timer(void) {
    ZulrahState s;
    init_zulrah_state(&s, 2);
    s.player.attack_timer = 0;
    s.player.special_energy = 100;

    int weapon = s.player.equipped[GEAR_SLOT_WEAPON];
    SpecResult sr = {0};
    sr.attack_speed_override = 5;
    OsrsPlayerAttackProfile profile =
        osrs_player_attack_profile_for_special(
            (uint8_t)weapon,
            ATTACK_STYLE_MAGIC,
            s.player.fight_style,
            weapon,
            sr);

    zul_player_spec(&s);

    expect_int("zulrah eye special timer", s.player.attack_timer, profile.cycle_ticks);
    expect_int("zulrah eye special visual style", s.player.attack_style_this_tick, profile.visual_style);
    expect_int("zulrah eye special damage style", s.player.last_attack_style, profile.damage_style);
}

int main(void) {
    test_zulrah_cached_loadout_stats_use_shared_profile();
    test_zulrah_msb_special_uses_shared_profile_timer();
    test_zulrah_eye_of_ayak_special_uses_override_timer();

    if (tests_failed) {
        printf("zulrah player attack profile tests: %d failed / %d run\n", tests_failed, tests_run);
        return 1;
    }

    printf("zulrah player attack profile tests: %d passed / %d run\n", tests_run, tests_run);
    return 0;
}
