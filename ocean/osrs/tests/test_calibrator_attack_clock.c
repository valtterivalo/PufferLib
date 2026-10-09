#define main calibrator_main
#include "calibrate_riskfight.c"
#undef main
#define main lookahead_server_main
#include "gs_lookahead_server.c"
#undef main

int main(int argc, char** argv) {
    assert(argc == 2);
    static Observer observed[4];
    observed[1].inf.local_timer = (GsTimerState){.seen = 1, .earliest = 104, .latest = 107};
    observed[2].inf.local_timer = (GsTimerState){.seen = 1, .earliest = 104, .latest = 104};
    observed[3].inf.local_timer = observed[2].inf.local_timer;
    Tape tape = {.lo = 100, .hi = 103, .observer_series = observed};
    strcpy(tape.name, "clock_fixture");
    assert(clock_status(&tape, 99) == CLOCK_OUTSIDE);
    assert(clock_status(&tape, 100) == CLOCK_UNKNOWN);
    assert(clock_status(&tape, 101) == CLOCK_INTERVAL);
    assert(clock_status(&tape, 102) == CLOCK_EXACT);
    assert(clock_anchor(&tape, 100) == 102);
    assert(clock_counts[CLOCK_WHOLE_FIGHT][CLOCK_UNKNOWN] == 1);
    assert(clock_counts[CLOCK_WHOLE_FIGHT][CLOCK_INTERVAL] == 1);
    assert(clock_counts[CLOCK_WHOLE_FIGHT][CLOCK_EXACT] == 1);
    assert(tape.lo == 100 && tape.hi == 103 && tape.observer_series == observed);
    EatRow ready = {.timer = imax(0, -7)}, soon = {.timer = 1};
    int ready_idx[EAT_DIM], soon_idx[EAT_DIM];
    assert(eat_features_of(&ready, 2, ready_idx) == 2);
    assert(eat_features_of(&soon, 2, soon_idx) == 2);
    assert(ready_idx[1] == 14 && soon_idx[1] == 15);
    static Tape recorded;
    tape_load(&recorded, argv[1]);
    prepare(&recorded);
    g_tape = &recorded;
    g_unseen_ticks = unseen_ticks;
    static Sim sim;
    sim_init(&sim);
    int exact = 0, unknown = 0;
    for (int t = recorded.lo + 1; t <= recorded.hi; t++) {
        RiskfightSnapshot snap;
        Probe pure = hidden_probe(&recorded, t, 0, &snap);
        GsTimerState opponent = recorded.observer_series[t - 1 - recorded.lo].inf.opponent.timer;
        assert(snap.side[1].attack_history == RISKFIGHT_ATTACK_LATENT);
        if (opponent.seen) {
            assert(snap.side[1].attack_timer_uncapped >= opponent.earliest - t);
            assert(snap.side[1].attack_timer_uncapped <= opponent.latest - t);
        } else assert(snap.side[1].attack_timer_uncapped == 0);
        if (clock_status(&recorded, t - 1) != CLOCK_EXACT) {
            unknown++;
            assert(snap.side[0].attack_history == RISKFIGHT_ATTACK_UNKNOWN);
            continue;
        }
        exact++;
        riskfight_load_state(&sim.state, &sim.context, &snap);
        const Hist* hist = &((const Hist*)recorded.hist_series)[t - 1 - recorded.lo];
        Probe loaded = probe_state(&sim.state, hist, t, has_item(&sim.state, 1, ITEM_VOIDWAKER), has_maul(&sim.state, 1));
        for (int action = 0; action < ACT_COUNT; action++) {
            if (action == ACT_SWING) {
                assert(pure.eligible[action] == (gs_timer_reading(opponent, t) == GS_READING_READY));
                const Player* him = &sim.state.env.players[1];
                assert(loaded.eligible[action] == (!him->has_attack_timer || him->attack_timer <= 0));
            } else assert(pure.eligible[action] == loaded.eligible[action]);
            assert(pure.x[action].n == loaded.x[action].n);
            for (int k = 0; k < pure.x[action].n; k++) assert(pure.x[action].idx[k] == loaded.x[action].idx[k]);
        }
    }
    sim.state.env.players[0].has_attack_timer = 1;
    sim.state.env.players[0].attack_timer = -1;
    sim.state.env.players[0].attack_timer_uncapped = -6;
    LaBelief belief = {.timer_seen = 1, .timer_first = 104, .timer_last = 107};
    LaFeasible feasible = {0};
    LaScript rules = {0};
    FILE* stream = tmpfile();
    assert(stream);
    la_dump_request(stream, &sim, &belief, 110, &feasible, (GsStakes){0}, &rules);
    rewind(stream);
    Request request;
    read_request(stream, &request);
    assert(request.id == 110 && request.tick == 110);
    assert(request.attack_history == RISKFIGHT_ATTACK_OBSERVED);
    assert(request.attack_clock_low == -6 && request.attack_clock_high == -6);
    assert(request.belief.timer_seen && request.belief.timer_first == 104 && request.belief.timer_last == 107);
    fclose(stream);
    assert(exact > 0 && unknown > 0);
    printf("Hazard feature parity exact %d retained unknown %d source_frames %d\n", exact, unknown, recorded.hi - recorded.lo);
    puts("Calibrator attack-clock eligibility passed");
    return 0;
}
