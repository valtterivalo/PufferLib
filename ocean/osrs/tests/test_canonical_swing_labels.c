#define _POSIX_C_SOURCE 200809L
#include "riskfight_rollout.h"
#define main calibrator_main
#include "calibrate_riskfight.c"
#undef main

int main(int argc, char** argv) {
    assert(argc == 3);
    static Tape recorded;
    tape_load(&recorded, argv[1]);
    prepare(&recorded);
    assert(flag_at(&recorded, 170, FLAG_SWING));
    assert(!flag_at(&recorded, 171, FLAG_SWING));
    assert(!flag_at(&recorded, 171, FLAG_SWING_UNKNOWN));
    GsTimerState before = recorded.observer_series[170 - recorded.lo].inf.opponent.timer;
    GsTimerState after = recorded.observer_series[171 - recorded.lo].inf.opponent.timer;
    assert(before.seen && before.earliest == 175 && before.latest == 175);
    assert(after.seen && after.earliest == 175 && after.latest == 175);
    assert(flag_at(&recorded, 240, FLAG_SWING_UNKNOWN));
    Probe probe = {0};
    probe.eligible[ACT_SWING] = 1;
    probe.eligible[ACT_VW_SPEC] = 1;
    assert(conditioned(ACT_SWING, &probe, recorded.his_flags[171 - recorded.lo]).eligible);
    assert(!conditioned(ACT_SWING, &probe, recorded.his_flags[240 - recorded.lo]).eligible);
    assert(!conditioned(ACT_VW_SPEC, &probe, FLAG_SWING | FLAG_SWING_UNKNOWN).eligible);
    unsetenv("GS_WIRE");
    TapeAnim callbacks[] = {{171, SIDE_HIM, 401}, {172, SIDE_HIM, 1667}, {173, SIDE_HIM, 860}, {174, SIDE_HIM, -1}};
    Tape fixture = recorded;
    fixture.anims = callbacks;
    fixture.anim_count = 4;
    fixture.hit_count = 0;
    fixture.observer_series = NULL;
    prepare(&fixture);
    assert(flag_at(&fixture, 171, FLAG_SWING));
    assert(!flag_at(&fixture, 172, FLAG_SWING));
    assert(flag_at(&fixture, 172, FLAG_MAUL));
    assert(flag_at(&fixture, 173, FLAG_SWING_UNKNOWN));
    assert(fixture.his_flags[174 - fixture.lo] == 0);
    assert(conditioned(ACT_SWING, &probe, fixture.his_flags[174 - fixture.lo]).eligible);
    const Hist* history = fixture.hist_series;
    assert(history[173 - fixture.lo].warning.unseen_count == 3);
    assert(history[174 - fixture.lo].warning.unseen_count == 3);
    fixture.kit_count = 1;
    fixture.kit_anims[0][0] = 424;
    fixture.kit_anims[0][1] = 1;
    assert(his_kit(&fixture).main == ITEM_WHIP);
    static Tape maul;
    tape_load(&maul, argv[2]);
    prepare(&maul);
    uint32_t rng = 7;
    RiskfightSnapshot snapshot;
    (void)tape_snapshot(&maul, 3346, &DEFAULT_PRIORS, &rng, &snapshot); /* Inspect equipment only; this fixture does not load a combat world. */
    assert(snapshot.loadout[1].equipment[GEAR_SLOT_WEAPON] == ITEM_GRANITE_MAUL_ORNATE);
    puts("Canonical labels preserve raw block, timer, unknown classification and maul context");
}
