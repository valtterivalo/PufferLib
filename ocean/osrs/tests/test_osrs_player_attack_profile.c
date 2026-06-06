#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include "ocean/osrs/osrs_player_attack_profile.h"

static int tests_run = 0;
static int tests_passed = 0;
static int tests_failed = 0;

#define ASSERT_INT_EQ(label, actual, expected) do { \
    tests_run++; \
    if ((actual) == (expected)) { \
        tests_passed++; \
    } else { \
        tests_failed++; \
        printf("  FAIL: %s got %d expected %d\n", (label), (actual), (expected)); \
    } \
} while (0)

static void test_loadout(uint8_t weapon, uint8_t loadout[NUM_GEAR_SLOTS]) {
    for (int i = 0; i < NUM_GEAR_SLOTS; i++) {
        loadout[i] = ITEM_NONE;
    }
    loadout[GEAR_SLOT_WEAPON] = weapon;
}

static void test_traditional_barrage_is_five_ticks(void) {
    uint8_t loadout[NUM_GEAR_SLOTS];
    test_loadout(ITEM_KODAI_WAND, loadout);

    OsrsPlayerAttackProfile profile =
        osrs_player_attack_profile_for_loadout(
            loadout, ATTACK_STYLE_MAGIC, FIGHT_STYLE_AUTOCAST, 30);

    ASSERT_INT_EQ("traditional spell cycle", profile.cycle_ticks, 5);
    ASSERT_INT_EQ("traditional spell post timer", profile.post_action_timer, 4);
    ASSERT_INT_EQ("traditional spell range", profile.range, 10);
    ASSERT_INT_EQ("traditional spell delivery",
        profile.delivery, OSRS_ATTACK_DELIVERY_PROJECTILE);
}

static void test_powered_staff_uses_weapon_speed(void) {
    uint8_t loadout[NUM_GEAR_SLOTS];
    test_loadout(ITEM_TRIDENT_OF_SWAMP, loadout);

    OsrsPlayerAttackProfile profile =
        osrs_player_attack_profile_for_loadout(
            loadout, ATTACK_STYLE_MAGIC, FIGHT_STYLE_ACCURATE, 30);

    ASSERT_INT_EQ("powered staff cycle",
        profile.cycle_ticks, ITEM_DATABASE[ITEM_TRIDENT_OF_SWAMP].attack_speed);
    ASSERT_INT_EQ("powered staff range",
        profile.range, ITEM_DATABASE[ITEM_TRIDENT_OF_SWAMP].attack_range);
}

static void test_ranged_rapid_reduces_speed(void) {
    uint8_t loadout[NUM_GEAR_SLOTS];
    test_loadout(ITEM_RUNE_CROSSBOW, loadout);

    OsrsPlayerAttackProfile profile =
        osrs_player_attack_profile_for_loadout(
            loadout, ATTACK_STYLE_RANGED, FIGHT_STYLE_RAPID, 0);

    ASSERT_INT_EQ("ranged rapid speed",
        profile.cycle_ticks, ITEM_DATABASE[ITEM_RUNE_CROSSBOW].attack_speed - 1);
}

static void test_representative_weapon_speeds(void) {
    uint8_t loadout[NUM_GEAR_SLOTS];

    test_loadout(ITEM_AGS, loadout);
    OsrsPlayerAttackProfile ags =
        osrs_player_attack_profile_for_loadout(
            loadout, ATTACK_STYLE_MELEE, FIGHT_STYLE_AGGRESSIVE, 0);
    ASSERT_INT_EQ("godsword speed", ags.cycle_ticks, 6);

    test_loadout(ITEM_HEAVY_BALLISTA, loadout);
    OsrsPlayerAttackProfile ballista =
        osrs_player_attack_profile_for_loadout(
            loadout, ATTACK_STYLE_RANGED, FIGHT_STYLE_ACCURATE, 0);
    ASSERT_INT_EQ("heavy ballista speed", ballista.cycle_ticks, 7);
}

static void test_granite_maul_special_has_no_normal_cooldown(void) {
    SpecResult sr = {0};
    sr.attack_speed_override = 1;

    OsrsPlayerAttackProfile profile =
        osrs_player_attack_profile_for_special(
            ITEM_GRANITE_MAUL,
            ATTACK_STYLE_MELEE,
            FIGHT_STYLE_AGGRESSIVE,
            ITEM_GRANITE_MAUL,
            sr);

    ASSERT_INT_EQ("gmaul cooldown kind",
        profile.cooldown_kind, OSRS_PLAYER_ATTACK_COOLDOWN_NONE);
    ASSERT_INT_EQ("gmaul cycle", profile.cycle_ticks, 0);
    ASSERT_INT_EQ("gmaul post timer", profile.post_action_timer, 0);
}

static void test_voidwaker_visual_and_damage_styles_split(void) {
    SpecResult sr = {0};

    OsrsPlayerAttackProfile profile =
        osrs_player_attack_profile_for_special(
            ITEM_VOIDWAKER,
            ATTACK_STYLE_MELEE,
            FIGHT_STYLE_AGGRESSIVE,
            ITEM_VOIDWAKER,
            sr);

    ASSERT_INT_EQ("voidwaker visual style",
        profile.visual_style, ATTACK_STYLE_MELEE);
    ASSERT_INT_EQ("voidwaker damage style",
        profile.damage_style, ATTACK_STYLE_MAGIC);
    ASSERT_INT_EQ("voidwaker delivery",
        profile.delivery, OSRS_ATTACK_DELIVERY_MELEE);
}

static void test_invalid_profile_aborts(void) {
    tests_run++;
    pid_t pid = fork();
    if (pid == 0) {
        OsrsPlayerAttackProfileQuery query = {
            .weapon_item = ITEM_WHIP,
            .action_kind = OSRS_PLAYER_ATTACK_ACTION_POWERED_STAFF,
            .action_style = ATTACK_STYLE_MAGIC,
            .fight_style = FIGHT_STYLE_ACCURATE,
            .magic_kind = OSRS_MAGIC_ATTACK_POWERED_STAFF,
            .special_item = ITEM_NONE,
            .special_result = {0},
        };
        (void)osrs_player_attack_profile(&query);
        _exit(0);
    }
    if (pid < 0) {
        tests_failed++;
        printf("  FAIL: fork failed for invalid profile test\n");
        return;
    }
    int status = 0;
    waitpid(pid, &status, 0);
    if (WIFSIGNALED(status) && WTERMSIG(status) == SIGABRT) {
        tests_passed++;
    } else {
        tests_failed++;
        printf("  FAIL: invalid profile did not abort\n");
    }
}

int main(void) {
    test_traditional_barrage_is_five_ticks();
    test_powered_staff_uses_weapon_speed();
    test_ranged_rapid_reduces_speed();
    test_representative_weapon_speeds();
    test_granite_maul_special_has_no_normal_cooldown();
    test_voidwaker_visual_and_damage_styles_split();
    test_invalid_profile_aborts();

    printf("osrs player attack profile tests: %d passed / %d run\n",
        tests_passed, tests_run);
    return tests_failed == 0 ? 0 : 1;
}
