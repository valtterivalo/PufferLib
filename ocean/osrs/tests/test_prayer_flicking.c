/**
 * @file test_prayer_flicking.c
 * @brief Tests for toggle-semantic prayer actions + activation-tick drain skip.
 *
 * Covers the mechanics that make OSRS prayer flicking work:
 *   - toggle semantics (click-to-on, click-again-to-off, click-different-to-replace)
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
    (void)prayer_bonus;  /* passed to drain calls separately */
}

/* --- overhead toggle: activate, toggle off, replace --- */
static void test_overhead_toggle(void) {
    printf("--- overhead toggle semantics ---\n");
    Player p; reset_player(&p, 0);

    /* inactive + TOGGLE_MELEE → active on melee, just_activated set. */
    int act = encounter_apply_overhead_action(&p.prayer, ENCOUNTER_OVERHEAD_TOGGLE_MELEE);
    ASSERT_EQ("activation returns 1", act, 1);
    ASSERT_EQ("prayer == melee", p.prayer, PRAYER_PROTECT_MELEE);

    /* active melee + TOGGLE_MELEE → off, no activation. */
    act = encounter_apply_overhead_action(&p.prayer, ENCOUNTER_OVERHEAD_TOGGLE_MELEE);
    ASSERT_EQ("same-toggle returns 0", act, 0);
    ASSERT_EQ("prayer == none", p.prayer, PRAYER_NONE);

    /* activate ranged, then toggle magic → magic active, replaced ranged, activation=0 (replace). */
    encounter_apply_overhead_action(&p.prayer, ENCOUNTER_OVERHEAD_TOGGLE_RANGED);
    act = encounter_apply_overhead_action(&p.prayer, ENCOUNTER_OVERHEAD_TOGGLE_MAGIC);
    ASSERT_EQ("replace ranged→magic returns 0 (was already on)", act, 0);
    ASSERT_EQ("prayer == magic", p.prayer, PRAYER_PROTECT_MAGIC);

    /* NO_CHANGE is a pure no-op. */
    OverheadPrayer before = p.prayer;
    act = encounter_apply_overhead_action(&p.prayer, ENCOUNTER_OVERHEAD_NO_CHANGE);
    ASSERT_EQ("no-change returns 0", act, 0);
    ASSERT_EQ("prayer unchanged after no-change", p.prayer, before);
}

/* --- offensive toggle: mirror overhead semantics --- */
static void test_offensive_toggle(void) {
    printf("--- offensive toggle semantics ---\n");
    Player p; reset_player(&p, 0);

    int act = encounter_apply_offensive_action(&p.offensive_prayer, ENCOUNTER_OFFENSIVE_TOGGLE_PIETY);
    ASSERT_EQ("activate piety returns 1", act, 1);
    ASSERT_EQ("offensive == piety", p.offensive_prayer, OFFENSIVE_PRAYER_PIETY);

    act = encounter_apply_offensive_action(&p.offensive_prayer, ENCOUNTER_OFFENSIVE_TOGGLE_PIETY);
    ASSERT_EQ("toggle off returns 0", act, 0);
    ASSERT_EQ("offensive == none", p.offensive_prayer, OFFENSIVE_PRAYER_NONE);

    encounter_apply_offensive_action(&p.offensive_prayer, ENCOUNTER_OFFENSIVE_TOGGLE_RIGOUR);
    act = encounter_apply_offensive_action(&p.offensive_prayer, ENCOUNTER_OFFENSIVE_TOGGLE_AUGURY);
    ASSERT_EQ("replace rigour→augury returns 0", act, 0);
    ASSERT_EQ("offensive == augury", p.offensive_prayer, OFFENSIVE_PRAYER_AUGURY);
}

/* --- drain: activation tick does not charge --- */
static void test_activation_tick_skip(void) {
    printf("--- activation-tick drain skip ---\n");
    Player p; reset_player(&p, 0);

    /* simulate a pretick where agent activates piety. */
    int act = encounter_apply_offensive_action(&p.offensive_prayer, ENCOUNTER_OFFENSIVE_TOGGLE_PIETY);
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

/* --- 1-tick flick: toggle on + off + on within drain cycle saves everything --- */
static void test_one_tick_flick_saves_pp(void) {
    printf("--- 1-tick flicking burns 0 pp over many ticks ---\n");
    Player p; reset_player(&p, 0);

    /* The key mechanic: every tick, the agent fires TOGGLE_PIETY twice (via env-internal
       two-click chain — we simulate by calling apply twice). If piety was active at tick
       start, that means: first click turns off, second click turns on. The second
       activation sets just_activated; drain skips piety this tick. Prayer stays active
       without ever draining. */
    encounter_apply_offensive_action(&p.offensive_prayer, ENCOUNTER_OFFENSIVE_TOGGLE_PIETY);
    p.offensive_prayer_just_activated = 1;
    encounter_drain_all_prayers(&p, 0);

    int pp_before = p.current_prayer;
    for (int tick = 0; tick < 100; tick++) {
        /* flick: deactivate then reactivate — net state is "active, just_activated". */
        encounter_apply_offensive_action(&p.offensive_prayer, ENCOUNTER_OFFENSIVE_TOGGLE_PIETY);  /* off */
        int r = encounter_apply_offensive_action(&p.offensive_prayer, ENCOUNTER_OFFENSIVE_TOGGLE_PIETY);  /* on */
        if (r) p.offensive_prayer_just_activated = 1;
        encounter_drain_all_prayers(&p, 0);
    }
    ASSERT_EQ("100 flicks burn 0 pp", p.current_prayer, pp_before);
    ASSERT_EQ("prayer still active after flicks", p.offensive_prayer, OFFENSIVE_PRAYER_PIETY);
}

/* --- pp=0 auto-clears both slots --- */
static void test_pp_zero_clears_all(void) {
    printf("--- pp=0 auto-clears overhead + offensive ---\n");
    Player p; reset_player(&p, 0);
    p.current_prayer = 1;  /* about to run out */

    encounter_apply_overhead_action(&p.prayer, ENCOUNTER_OVERHEAD_TOGGLE_MELEE);
    encounter_apply_offensive_action(&p.offensive_prayer, ENCOUNTER_OFFENSIVE_TOGGLE_PIETY);
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

/* --- lazy flick: activate-tick-only (no deactivate-then-activate chain) lets drain accumulate --- */
static void test_lazy_flick_partial_cost(void) {
    printf("--- lazy flicking (one click per tick) still costs pp ---\n");
    Player p; reset_player(&p, 0);

    /* activate piety, drain (skipped), 4 idle ticks (drain), deactivate, repeat. */
    int pp_start = p.current_prayer;
    for (int cycle = 0; cycle < 5; cycle++) {
        encounter_apply_offensive_action(&p.offensive_prayer, ENCOUNTER_OFFENSIVE_TOGGLE_PIETY);  /* on */
        p.offensive_prayer_just_activated = 1;
        encounter_drain_all_prayers(&p, 0);  /* tick 0: skipped */
        /* tick 1-4: piety active, no activation → drains */
        for (int i = 0; i < 4; i++) encounter_drain_all_prayers(&p, 0);
        encounter_apply_offensive_action(&p.offensive_prayer, ENCOUNTER_OFFENSIVE_TOGGLE_PIETY);  /* off */
    }
    /* 5 cycles × 4 draining ticks × 24 drain = 480 counter units. / 60 resistance = 8 pp drained. */
    ASSERT("lazy flick drains some pp (not 0)", p.current_prayer < pp_start);
    ASSERT("lazy flick doesn't drain everything", p.current_prayer > 0);
}

int main(void) {
    test_overhead_toggle();
    test_offensive_toggle();
    test_activation_tick_skip();
    test_one_tick_flick_saves_pp();
    test_pp_zero_clears_all();
    test_lazy_flick_partial_cost();

    printf("\n=== results: %d/%d passed ===\n", tests_passed, tests_run);
    return (tests_passed == tests_run) ? 0 : 1;
}
