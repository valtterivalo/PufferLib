#define main calibrator_main
#include "calibrate_riskfight.c"
#undef main
#include "osrs_test_check.h"

int main(void) {
    TapeTick ticks[4] = {
        {.tick = 10, .bar_ratio = 13, .bar_scale = 30, .last_bar_hit = 10, .bar_now_ratio = 13, .bar_now_scale = 30},
        {.tick = 11, .bar_ratio = 13, .bar_scale = 30, .last_bar_hit = 11, .bar_now_ratio = -1, .bar_now_scale = -1},
        {.tick = 12, .bar_ratio = 13, .bar_scale = 30, .last_bar_hit = 12, .bar_now_ratio = 5, .bar_now_scale = 30},
        {.tick = 13, .bar_ratio = 0, .bar_scale = 30, .last_bar_hit = 13, .bar_now_ratio = 0, .bar_now_scale = 30},
    };
    TapeHit hits[3] = {
        {.tick = 11, .side = SIDE_HIM, .amount = 42, .type = 16, .klass = 'A'},
        {.tick = 12, .side = SIDE_HIM, .amount = 10, .type = 16, .klass = 'A'},
        {.tick = 13, .side = SIDE_HIM, .amount = 70, .type = 16, .klass = 'A'},
    };
    Tape tape = {.lo = 10, .hi = 13, .ticks = ticks, .hits = hits, .hit_count = 3};
    int tick = -1, low = -1, high = -1;
    CHECK("carried bar is not a first-hit HP reveal", !recorded_first_hit(&tape, 10, 1, &tick, &low, &high));
    CHECK("next raw bar supplies the reveal", recorded_first_hit(&tape, 10, 2, &tick, &low, &high));
    CHECK("reveal uses raw ratio rather than cached ratio", tick == 12 && low == 24 && high == 27);
    CHECK("terminal overkill does not infer pre-hit HP", !recorded_first_hit(&tape, 12, 1, &tick, &low, &high));
    g_tape = &tape;
    g_ablation.hp = HP_ORACLE;
    uint32_t rng = 1;
    CHECK("HP oracle skips carried reveal and reads raw ratio", sample_his_hitpoints(&tape, &ticks[0], &rng) == 25);
    Hist history[4];
    for (int i = 0; i < 4; i++) history[i] = hist_start();
    tape.hist_series = history;
    Tape prefix = tape;
    prefix.hi = 10;
    Tape future = tape;
    future.hi = 12;
    g_ablation.hp = HP_HIDDEN;
    g_unseen_ticks = unseen_ticks;
    uint32_t whole_rng = 1, prefix_rng = 1;
    int whole_hp = sample_his_hitpoints(&future, &ticks[0], &whole_rng);
    int prefix_hp = sample_his_hitpoints(&prefix, &ticks[0], &prefix_rng);
    CHECK("future observations do not change sampled HP", whole_hp == prefix_hp);
    CHECK("future observations do not change HP sampling randomness", whole_rng == prefix_rng);
    return osrs_test_summary();
}
