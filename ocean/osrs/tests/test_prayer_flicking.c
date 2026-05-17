/**
 * @file test_prayer_flicking.c
 * @brief Tests for set/refresh prayer actions and activation-tick drain skip.
 *
 * Covers the mechanics that make OSRS prayer flicking work:
 *   - set/refresh semantics for RL tick commands
 *   - explicit off actions for human click parity
 *   - activation-tick drain skip (wiki: no drain on the tick a prayer is activated)
 *   - pp=0 auto-clear of all prayer slots
 *   - 1-tick flicking burns 0 pp across many ticks (the whole point of this work)
 *   - tick ordering: prayer activated in pretick is effective for same-tick attack resolution
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "osrs_encounter.h"

static int tests_run = 0;
static int tests_passed = 0;

#define ASSERT_EQ(label, actual, expected) do { \
    tests_run++; \
    if ((actual) == (expected)) { tests_passed++; } \
    else { printf("  FAIL %s: expected %d, got %d\n", label, (int)(expected), (int)(actual)); } \
} while (0)

#define ASSERT(label, cond) do { \
    tests_run++; \
    if (cond) { tests_passed++; } \
    else { printf("  FAIL %s\n", label); } \
} while (0)

/* zero a player to a fresh prayer-ready state. */
static void reset_player(Player* p, int prayer_bonus) {
    memset(p, 0, sizeof(*p));
    p->base_prayer = 99;
    p->current_prayer = 99;
    p->base_hitpoints = 99;
    p->current_hitpoints = 99;
    for (int i = 0; i < NUM_GEAR_SLOTS; i++)
        p->equipped[i] = ITEM_NONE;
    (void)prayer_bonus;  /* passed to drain calls separately */
}

/* --- overhead set/refresh: activate, refresh, off, replace --- */
static void test_overhead_set_refresh(void) {
    printf("--- overhead set/refresh semantics ---\n");
    Player p; reset_player(&p, 0);

    int act = encounter_apply_overhead_action(&p.prayer, ENCOUNTER_OVERHEAD_SET_REFRESH_MELEE);
    ASSERT_EQ("activation returns 1", act, 1);
    ASSERT_EQ("prayer == melee", p.prayer, PRAYER_PROTECT_MELEE);

    act = encounter_apply_overhead_action(&p.prayer, ENCOUNTER_OVERHEAD_SET_REFRESH_MELEE);
    ASSERT_EQ("same-prayer refresh returns 1", act, 1);
    ASSERT_EQ("prayer still melee", p.prayer, PRAYER_PROTECT_MELEE);

    act = encounter_apply_overhead_action(&p.prayer, ENCOUNTER_OVERHEAD_OFF);
    ASSERT_EQ("off returns 0", act, 0);
    ASSERT_EQ("prayer == none", p.prayer, PRAYER_NONE);

    encounter_apply_overhead_action(&p.prayer, ENCOUNTER_OVERHEAD_SET_REFRESH_RANGED);
    act = encounter_apply_overhead_action(&p.prayer, ENCOUNTER_OVERHEAD_SET_REFRESH_MAGIC);
    ASSERT_EQ("replace ranged to magic returns 1", act, 1);
    ASSERT_EQ("prayer == magic", p.prayer, PRAYER_PROTECT_MAGIC);

    OverheadPrayer before = p.prayer;
    act = encounter_apply_overhead_action(&p.prayer, ENCOUNTER_OVERHEAD_NO_CHANGE);
    ASSERT_EQ("no-change returns 0", act, 0);
    ASSERT_EQ("prayer unchanged after no-change", p.prayer, before);
}

/* --- offensive set/refresh: mirror overhead semantics --- */
static void test_offensive_set_refresh(void) {
    printf("--- offensive set/refresh semantics ---\n");
    Player p; reset_player(&p, 0);

    int act = encounter_apply_offensive_action(&p.offensive_prayer, ENCOUNTER_OFFENSIVE_SET_REFRESH_PIETY);
    ASSERT_EQ("activate piety returns 1", act, 1);
    ASSERT_EQ("offensive == piety", p.offensive_prayer, OFFENSIVE_PRAYER_PIETY);

    act = encounter_apply_offensive_action(&p.offensive_prayer, ENCOUNTER_OFFENSIVE_SET_REFRESH_PIETY);
    ASSERT_EQ("same-prayer refresh returns 1", act, 1);
    ASSERT_EQ("offensive still piety", p.offensive_prayer, OFFENSIVE_PRAYER_PIETY);

    act = encounter_apply_offensive_action(&p.offensive_prayer, ENCOUNTER_OFFENSIVE_OFF);
    ASSERT_EQ("off returns 0", act, 0);
    ASSERT_EQ("offensive == none", p.offensive_prayer, OFFENSIVE_PRAYER_NONE);

    encounter_apply_offensive_action(&p.offensive_prayer, ENCOUNTER_OFFENSIVE_SET_REFRESH_RIGOUR);
    act = encounter_apply_offensive_action(&p.offensive_prayer, ENCOUNTER_OFFENSIVE_SET_REFRESH_AUGURY);
    ASSERT_EQ("replace rigour to augury returns 1", act, 1);
    ASSERT_EQ("offensive == augury", p.offensive_prayer, OFFENSIVE_PRAYER_AUGURY);
}

/* --- drain: activation tick does not charge --- */
static void test_activation_tick_skip(void) {
    printf("--- activation-tick drain skip ---\n");
    Player p; reset_player(&p, 0);

    /* simulate a pretick where agent activates piety. */
    int act = encounter_apply_offensive_action(&p.offensive_prayer, ENCOUNTER_OFFENSIVE_SET_REFRESH_PIETY);
    if (act) p.offensive_prayer_just_activated = 1;

    int pp_before = p.current_prayer;
    encounter_drain_all_prayers(&p, 0);
    ASSERT_EQ("no drain on activation tick", p.current_prayer, pp_before);
    ASSERT_EQ("just_activated cleared after drain", p.offensive_prayer_just_activated, 0);

    /* next tick: no activation, piety still on → drain should accumulate. */
    for (int i = 0; i < 3; i++) encounter_drain_all_prayers(&p, 0);
    /* piety drain effect = 24, resistance = 60 (bonus=0) → +24*3=72 counter, crosses 60 once → -1 pp. */
    ASSERT_EQ("after 3 non-activation ticks: pp drops by 1", p.current_prayer, pp_before - 1);
}

/* --- 1-tick flick: set/refresh each tick saves everything --- */
static void test_one_tick_flick_saves_pp(void) {
    printf("--- 1-tick flicking burns 0 pp over many ticks ---\n");
    Player p; reset_player(&p, 0);

    encounter_apply_offensive_action(&p.offensive_prayer, ENCOUNTER_OFFENSIVE_SET_REFRESH_PIETY);
    p.offensive_prayer_just_activated = 1;
    encounter_drain_all_prayers(&p, 0);

    int pp_before = p.current_prayer;
    for (int tick = 0; tick < 100; tick++) {
        int r = encounter_apply_offensive_action(
            &p.offensive_prayer, ENCOUNTER_OFFENSIVE_SET_REFRESH_PIETY);
        if (r) p.offensive_prayer_just_activated = 1;
        encounter_drain_all_prayers(&p, 0);
    }
    ASSERT_EQ("100 flicks burn 0 pp", p.current_prayer, pp_before);
    ASSERT_EQ("prayer still active after flicks", p.offensive_prayer, OFFENSIVE_PRAYER_PIETY);
}

/* --- direct style replacement flick: new slot prayer is activation-free --- */
static void test_replacing_active_prayer_skips_drain(void) {
    printf("--- replacing active prayer skips drain ---\n");
    Player p; reset_player(&p, 0);

    int act = encounter_apply_overhead_action(&p.prayer, ENCOUNTER_OVERHEAD_SET_REFRESH_MAGIC);
    if (act) p.prayer_just_activated = 1;
    encounter_drain_all_prayers(&p, 0);

    int pp_before = p.current_prayer;
    for (int tick = 0; tick < 100; tick++) {
        int action = (tick & 1)
            ? ENCOUNTER_OVERHEAD_SET_REFRESH_MAGIC
            : ENCOUNTER_OVERHEAD_SET_REFRESH_RANGED;
        act = encounter_apply_overhead_action(&p.prayer, action);
        if (act) p.prayer_just_activated = 1;
        encounter_drain_all_prayers(&p, 0);
    }

    ASSERT_EQ("100 direct replacements burn 0 pp", p.current_prayer, pp_before);
    ASSERT_EQ("direct replacements leave no drain counter", p.prayer_drain_counter, 0);
    ASSERT_EQ("final replacement prayer active", p.prayer, PRAYER_PROTECT_MAGIC);
}

static void test_equipped_prayer_bonus_feeds_drain_resistance(void) {
    printf("--- equipped prayer bonus feeds drain resistance ---\n");
    Player p; reset_player(&p, 0);
    p.equipped[GEAR_SLOT_NECK] = ITEM_OCCULT_NECKLACE;
    p.equipped[GEAR_SLOT_SHIELD] = ITEM_ELYSIAN_SPIRIT_SHIELD;
    ASSERT_EQ("equipped prayer bonus sums", encounter_player_prayer_bonus(&p), 5);

    int act = encounter_apply_overhead_action(&p.prayer, ENCOUNTER_OVERHEAD_SET_REFRESH_MAGIC);
    if (act) p.prayer_just_activated = 1;
    encounter_drain_all_prayers(&p, encounter_player_prayer_bonus(&p));

    int pp_before = p.current_prayer;
    for (int tick = 0; tick < 5; tick++)
        encounter_drain_all_prayers(&p, encounter_player_prayer_bonus(&p));

    ASSERT_EQ("bonus 5 delays protection drain for five ticks", p.current_prayer, pp_before);
    encounter_drain_all_prayers(&p, encounter_player_prayer_bonus(&p));
    ASSERT_EQ("sixth charged tick crosses resistance", p.current_prayer, pp_before - 1);
}

/* --- pp=0 auto-clears both slots --- */
static void test_pp_zero_clears_all(void) {
    printf("--- pp=0 auto-clears overhead + offensive ---\n");
    Player p; reset_player(&p, 0);
    p.current_prayer = 1;  /* about to run out */

    encounter_apply_overhead_action(&p.prayer, ENCOUNTER_OVERHEAD_SET_REFRESH_MELEE);
    encounter_apply_offensive_action(&p.offensive_prayer, ENCOUNTER_OFFENSIVE_SET_REFRESH_PIETY);
    /* both just-activated → no drain this tick */
    p.prayer_just_activated = 1;
    p.offensive_prayer_just_activated = 1;
    encounter_drain_all_prayers(&p, 0);
    ASSERT_EQ("pp unchanged on activation tick", p.current_prayer, 1);

    /* next tick: both prayers active, drain charges — overhead(12) + piety(24) = 36/tick.
       resistance = 60, so takes 2 ticks to drain 1 pp. pp=1 → after enough ticks, pp=0. */
    for (int i = 0; i < 10; i++) encounter_drain_all_prayers(&p, 0);
    ASSERT_EQ("pp floors at 0", p.current_prayer, 0);
    ASSERT_EQ("overhead cleared at pp=0", p.prayer, PRAYER_NONE);
    ASSERT_EQ("offensive cleared at pp=0", p.offensive_prayer, OFFENSIVE_PRAYER_NONE);
}

/* --- camped prayer: no refresh means drain accumulates --- */
static void test_camped_prayer_costs_pp(void) {
    printf("--- camped prayer without refresh costs pp ---\n");
    Player p; reset_player(&p, 0);

    /* activate piety, drain skipped, 4 idle ticks drain, deactivate, repeat. */
    int pp_start = p.current_prayer;
    for (int cycle = 0; cycle < 5; cycle++) {
        encounter_apply_offensive_action(&p.offensive_prayer, ENCOUNTER_OFFENSIVE_SET_REFRESH_PIETY);
        p.offensive_prayer_just_activated = 1;
        encounter_drain_all_prayers(&p, 0);
        for (int i = 0; i < 4; i++) encounter_drain_all_prayers(&p, 0);
        encounter_apply_offensive_action(&p.offensive_prayer, ENCOUNTER_OFFENSIVE_OFF);
    }
    /* 5 cycles × 4 draining ticks × 24 drain = 480 counter units. / 60 resistance = 8 pp drained. */
    ASSERT("camped prayer drains some pp", p.current_prayer < pp_start);
    ASSERT("camped prayer doesn't drain everything", p.current_prayer > 0);
}

/* --- regression: activating a prayer at pp=0 must not leave enum set ---
   observed in eval playback: agent's overhead enum stuck on after pp hit 0.
   root cause: pretick apply_*_action() doesn't gate on pp; drain then returned
   early on pp<=0 without clearing the enum. */
static void test_activate_at_zero_pp_clears(void) {
    printf("--- activation at pp=0 does not persist in enum ---\n");
    Player p; reset_player(&p, 0);
    p.current_prayer = 0;

    encounter_apply_overhead_action(&p.prayer, ENCOUNTER_OVERHEAD_SET_REFRESH_MELEE);
    encounter_apply_offensive_action(&p.offensive_prayer, ENCOUNTER_OFFENSIVE_SET_REFRESH_PIETY);
    p.prayer_just_activated = 1;
    p.offensive_prayer_just_activated = 1;

    encounter_drain_all_prayers(&p, 0);
    ASSERT_EQ("overhead cleared after activation at pp=0", p.prayer, PRAYER_NONE);
    ASSERT_EQ("offensive cleared after activation at pp=0", p.offensive_prayer, OFFENSIVE_PRAYER_NONE);
    ASSERT_EQ("pp stays 0", p.current_prayer, 0);

    /* second tick: stale enum from before drain=0 path should also be cleared
       if somehow pp enters at 0 with enum already set. */
    p.prayer = PRAYER_PROTECT_MAGIC;
    p.offensive_prayer = OFFENSIVE_PRAYER_AUGURY;
    encounter_drain_all_prayers(&p, 0);
    ASSERT_EQ("overhead cleared from stale state", p.prayer, PRAYER_NONE);
    ASSERT_EQ("offensive cleared from stale state", p.offensive_prayer, OFFENSIVE_PRAYER_NONE);
}

int main(void) {
    test_overhead_set_refresh();
    test_offensive_set_refresh();
    test_activation_tick_skip();
    test_one_tick_flick_saves_pp();
    test_replacing_active_prayer_skips_drain();
    test_equipped_prayer_bonus_feeds_drain_resistance();
    test_pp_zero_clears_all();
    test_camped_prayer_costs_pp();
    test_activate_at_zero_pp_clears();

    printf("\n=== results: %d/%d passed ===\n", tests_passed, tests_run);
    return (tests_passed == tests_run) ? 0 : 1;
}
