/**
 * @file test_consumables.c
 * @brief Tests for shared food/potion/brew functions in osrs_consumables.h.
 *
 * Build: cc -std=c11 -O0 -g -I. -o test_consumables ocean/osrs/tests/test_consumables.c -lm
 */

#include <stdio.h>
#include <stdlib.h>
#include "ocean/osrs/osrs_consumables.h"
#include "ocean/osrs/osrs_player_consumables.h"

static int total_tests = 0;
static int passed_tests = 0;

#define ASSERT_EQ(a, b, msg) do { \
    total_tests++; \
    if ((a) != (b)) { \
        printf("FAIL: %s: got %d, expected %d\n", msg, (int)(a), (int)(b)); \
    } else { passed_tests++; } \
} while(0)

/* --- food healing amounts --- */
static void test_food_heal_amount(void) {
    printf("--- food heal amounts ---\n");
    ASSERT_EQ(osrs_food_heal_amount(FOOD_SHARK), 20, "shark heals 20");
    ASSERT_EQ(osrs_food_heal_amount(FOOD_KARAMBWAN), 18, "karambwan heals 18");
    ASSERT_EQ(osrs_food_heal_amount(FOOD_MANTA_RAY), 22, "manta ray heals 22");
    ASSERT_EQ(osrs_food_heal_amount(FOOD_ANGLERFISH), 22, "anglerfish heals 22");
}

/* --- eating food --- */
static void test_eat_food(void) {
    printf("--- osrs_eat_food ---\n");

    /* shark at 50/99 HP: heals 20 → 70 */
    EatResult r = osrs_eat_food(FOOD_SHARK, 50, 99, 0);
    ASSERT_EQ(r.consumed, 1, "shark consumed");
    ASSERT_EQ(r.hp_healed, 20, "shark heals 20");

    /* shark at 90/99: heals 9 (clamped to max) */
    r = osrs_eat_food(FOOD_SHARK, 90, 99, 0);
    ASSERT_EQ(r.consumed, 1, "shark at 90 consumed");
    ASSERT_EQ(r.hp_healed, 9, "shark at 90 heals 9 (clamped)");

    /* shark at 99/99: can't eat (full HP) */
    r = osrs_eat_food(FOOD_SHARK, 99, 99, 0);
    ASSERT_EQ(r.consumed, 0, "shark at full HP not consumed");

    /* shark with food_timer active: can't eat */
    r = osrs_eat_food(FOOD_SHARK, 50, 99, 2);
    ASSERT_EQ(r.consumed, 0, "shark timer active not consumed");

    /* anglerfish can overheal: at 99/99, heals to 121 */
    r = osrs_eat_food(FOOD_ANGLERFISH, 99, 99, 0);
    ASSERT_EQ(r.consumed, 1, "anglerfish at full HP consumed (overheal)");
    ASSERT_EQ(r.hp_healed, 22, "anglerfish overheals 22");
}

/* --- potions --- */
static void test_drink_potion(void) {
    printf("--- osrs_drink_potion ---\n");

    /* prayer pot: 7 + floor(prayer_level / 4).  at 77: 7 + 19 = 26 */
    DrinkResult dr = osrs_drink_potion(POTION_PRAYER_RESTORE, 30, 77, 0);
    ASSERT_EQ(dr.consumed, 1, "prayer pot consumed");
    ASSERT_EQ(dr.prayer_restored, 26, "prayer pot restores 26 at lvl 77");

    /* super restore: 8 + floor(prayer_level / 4).  at 77: 8 + 19 = 27 */
    dr = osrs_drink_potion(POTION_SUPER_RESTORE, 30, 77, 0);
    ASSERT_EQ(dr.consumed, 1, "super restore consumed");
    ASSERT_EQ(dr.prayer_restored, 27, "super restore restores 27 at lvl 77");

    /* prayer pot at full prayer: can't drink */
    dr = osrs_drink_potion(POTION_PRAYER_RESTORE, 77, 77, 0);
    ASSERT_EQ(dr.consumed, 0, "prayer pot at full prayer not consumed");

    /* potion timer active: can't drink */
    dr = osrs_drink_potion(POTION_PRAYER_RESTORE, 30, 77, 2);
    ASSERT_EQ(dr.consumed, 0, "prayer pot timer active not consumed");

    /* antivenom+: cures venom, grants immunity */
    dr = osrs_drink_potion(POTION_ANTIVENOM_PLUS, 50, 77, 0);
    ASSERT_EQ(dr.consumed, 1, "antivenom consumed");
    ASSERT_EQ(dr.venom_cured, 1, "antivenom cures venom");
    ASSERT_EQ(dr.antivenom_ticks, 300, "antivenom grants 300 tick immunity");
}

/* --- brew --- */
static void test_brew(void) {
    printf("--- osrs_brew ---\n");

    /* sara brew: heals 15% + 2 of max HP = floor(99*0.15)+2 = 16.
       boosts def: floor(99*0.20)+2 = 21.
       drains att/str/range/magic: floor(99*0.10)+2 = 11 each. */
    BrewResult br = osrs_brew_effect(99, 99, 99, 99, 99);
    ASSERT_EQ(br.hp_healed, 16, "brew heals floor(99*0.15)+2=16");
    ASSERT_EQ(br.def_boost, 21, "brew def boost floor(99*0.20)+2=21");
    ASSERT_EQ(br.att_drain, 11, "brew att drain floor(99*0.10)+2=11");
    ASSERT_EQ(br.str_drain, 11, "brew str drain");
    ASSERT_EQ(br.range_drain, 11, "brew range drain");
    ASSERT_EQ(br.magic_drain, 11, "brew magic drain");
}

/* --- combo eat timing --- */
static void test_combo_timing(void) {
    printf("--- combo eat timing ---\n");

    ASSERT_EQ(osrs_can_eat(0), 1, "can eat when timer=0");
    ASSERT_EQ(osrs_can_eat(1), 0, "can't eat when timer=1");
    ASSERT_EQ(osrs_can_eat(3), 0, "can't eat when timer=3");
    ASSERT_EQ(osrs_can_drink(0), 1, "can drink when timer=0");
    ASSERT_EQ(osrs_can_drink(2), 0, "can't drink when timer=2");
}

static void test_player_food_transition_allows_shark_karambwan_combo(void) {
    printf("--- player food transition ---\n");

    Player p = {0};
    p.base_hitpoints = 99;
    p.current_hitpoints = 40;
    p.food_count = 1;
    p.karambwan_count = 1;

    ASSERT_EQ(osrs_player_can_eat_food_type(&p, FOOD_SHARK), 1, "player can eat shark");
    OsrsPlayerEatResult r = osrs_player_eat_food_type(&p, FOOD_SHARK);
    ASSERT_EQ(r.consumed, 1, "player shark consumed");
    ASSERT_EQ(r.hp_healed, 20, "player shark heals 20");
    ASSERT_EQ(p.current_hitpoints, 60, "player shark hp");
    ASSERT_EQ(p.food_count, 0, "player shark decrements food");
    ASSERT_EQ(p.food_timer, 3, "player shark sets food timer");
    ASSERT_EQ(p.attack_timer, 3, "player shark delays attack");
    ASSERT_EQ(p.ate_food_this_tick, 1, "player shark event set");

    ASSERT_EQ(osrs_player_can_eat_food_type(&p, FOOD_SHARK), 0, "player food timer blocks shark");
    ASSERT_EQ(osrs_player_can_eat_food_type(&p, FOOD_KARAMBWAN), 1, "player food timer does not block karambwan");

    r = osrs_player_eat_food_type(&p, FOOD_KARAMBWAN);
    ASSERT_EQ(r.consumed, 1, "player karambwan consumed");
    ASSERT_EQ(r.hp_healed, 18, "player karambwan heals 18");
    ASSERT_EQ(p.current_hitpoints, 78, "player karambwan hp");
    ASSERT_EQ(p.karambwan_count, 0, "player karambwan decrements count");
    ASSERT_EQ(p.karambwan_timer, 2, "player karambwan sets timer");
    ASSERT_EQ(p.potion_timer, 3, "player karambwan sets potion timer");
    ASSERT_EQ(p.attack_timer, 5, "player karambwan stacks attack delay");
    ASSERT_EQ(p.ate_karambwan_this_tick, 1, "player karambwan event set");
}

int main(void) {
    test_food_heal_amount();
    test_eat_food();
    test_drink_potion();
    test_brew();
    test_combo_timing();
    test_player_food_transition_allows_shark_karambwan_combo();

    printf("\n=== results: %d/%d passed ===\n", passed_tests, total_tests);
    return (passed_tests == total_tests) ? 0 : 1;
}
