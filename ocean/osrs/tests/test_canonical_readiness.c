#define _POSIX_C_SOURCE 200809L
#include "riskfight_rollout.h"
#define main calibrator_main
#include "calibrate_riskfight.c"
#undef main

int main(void) {
    observer_register_items();
    Hist h = hist_start();
    h.ready_age = 4;
    for (int reading = GS_READING_UNKNOWN; reading <= GS_READING_READY; reading++) {
        HimInputs in = {.readiness = (GsReading)reading};
        Probe p = probe_from_inputs(&in, &h, 100);
        assert(p.eligible[ACT_SWING] == (reading == GS_READING_READY));
        assert(p.x[ACT_SWING].n == 2 && p.x[ACT_SWING].idx[1] == 2);
    }
    static TapeTick ticks[5];
    static Observer obs[5];
    for (int i = 0; i < 5; i++) {
        ticks[i].tick = 100 + i;
        ticks[i].bar_now_ratio = ticks[i].bar_now_scale = -1;
        ticks[i].swing_him_tick = 100;
        ticks[i].swing_him_speed = 4;
        for (int k = 0; k < TAPE_WORN; k++) ticks[i].worn[k] = -1;
    }
    obs[1].inf.opponent.timer = (GsTimerState){1, 105, 105};
    obs[2].inf.opponent.timer = (GsTimerState){1, 103, 105};
    obs[3].inf.opponent.timer = obs[4].inf.opponent.timer = (GsTimerState){1, 104, 104};
    Tape tape = {.lo = 100, .hi = 104, .ticks = ticks, .observer_series = obs};
    Hist history[5];
    hist_series(&tape, history);
    int ages[] = {0, 0, 0, 1, 2};
    for (int i = 0; i < 5; i++) assert(history[i].ready_age == ages[i]);
    tape.observer_series = NULL;
    hist_series(&tape, history);
    for (int i = 0; i < 5; i++) assert(history[i].ready_age == 0);
    static SpecRow rows[11];
    for (int i = 0; i < 11; i++) {
        rows[i].tick = 100 + i;
        for (int side = 0; side < 2; side++) rows[i].s[side].wep = rows[i].s[side].bar = -1;
    }
    rows[1].s[0].att = 1;
    rows[1].s[0].anims[0] = 390;
    rows[1].s[0].anim_count = 1;
    rows[2].s[0].wep = ITEM_DATABASE[ITEM_MAGIC_SHORTBOW_I].item_id;
    rows[2].s[0].att = 1;
    rows[2].s[0].anims[0] = 426;
    rows[2].s[0].anim_count = 1;
    rows[3].s[0].anims[0] = 424;
    rows[3].s[0].anim_count = 1;
    rows[6].s[0].att = rows[6].s[0].maul = 1;
    rows[6].s[0].anims[0] = 1667;
    rows[6].s[0].anim_count = 1;
    rows[7].s[0].eat829 = 1;
    rows[7].s[0].meal = MEAL_EAT;
    rows[7].s[0].anims[0] = 829;
    rows[7].s[0].anim_count = 1;
    rows[8].s[0].meal = MEAL_HIDDEN;
    rows[9].s[0].anims[0] = 860;
    rows[9].s[0].anim_count = 1;
    SpecDuel duel = {.rows = rows, .row_count = 11};
    SpecTick series[11];
    spec_series(&duel, 0, series);
    GsReading expected[] = {GS_READING_UNKNOWN, GS_READING_UNKNOWN, GS_READING_COOLDOWN, GS_READING_COOLDOWN,
        GS_READING_MAYBE, GS_READING_READY, GS_READING_READY, GS_READING_MAYBE, GS_READING_UNKNOWN,
        GS_READING_UNKNOWN, GS_READING_UNKNOWN};
    for (int i = 0; i < 11; i++) assert(series[i].in.readiness == expected[i]);
    assert(series[2].labels & SPEC_SWING);
    assert(!(series[3].labels & (SPEC_SWING | SPEC_SWING_UNKNOWN)));
    assert(!(series[6].labels & SPEC_SWING) && (series[6].labels & SPEC_MAUL));
    assert(series[9].labels & SPEC_SWING_UNKNOWN);
    puts("Canonical readiness preserves unknown state, speed intervals, instant attacks and observed-ready history");
}
