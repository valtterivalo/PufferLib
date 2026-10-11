#define main calibrator_main
#include "calibrate_riskfight.c"
#undef main
#define main lookahead_server_main
#include "gs_lookahead_server.c"
#undef main
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

static void tape_fixture_require(int ok, const char* label) {
    if (!ok) {
        fprintf(stderr, "tape fixture failed: %s\n", label);
        abort();
    }
}

static void tape_pending_fixture(const char* label, const char* metadata, int first, int count,
        int rejected, int expected_source) {
    char path[] = "/tmp/gs-tape-pending-XXXXXX";
    int fd = mkstemp(path);
    tape_fixture_require(fd >= 0, label);
    FILE* f = fdopen(fd, "w");
    tape_fixture_require(f != NULL, label);
    tape_fixture_require(fputs(metadata, f) >= 0, label);
    for (int row = 0; row < count; row++) {
        tape_fixture_require(fprintf(f, "T %d", first + row) > 0, label);
        for (int i = 1; i < 32; i++) tape_fixture_require(fputs(" 0", f) >= 0, label);
        tape_fixture_require(fputs(" U NONE 0", f) >= 0, label);
        for (int i = 0; i < TAPE_WORN + TAPE_BAG; i++) tape_fixture_require(fputs(" 0", f) >= 0, label);
        tape_fixture_require(fputc('\n', f) != EOF, label);
    }
    tape_fixture_require(fclose(f) == 0, label);
    fflush(NULL);
    pid_t child = fork();
    tape_fixture_require(child >= 0, label);
    if (child == 0) {
        Tape loaded;
        int actual_count = tape_load(&loaded, path);
        if (!rejected) {
            tape_fixture_require(actual_count == count, label);
            for (int i = 0; i < count; i++)
                tape_fixture_require(loaded.ticks[i].pending_known == expected_source, label);
        }
        tape_free(&loaded);
        _exit(0);
    }
    int status;
    tape_fixture_require(waitpid(child, &status, 0) == child, label);
    tape_fixture_require(unlink(path) == 0, label);
    tape_fixture_require(rejected ? WIFSIGNALED(status) && WTERMSIG(status) == SIGABRT :
            WIFEXITED(status) && WEXITSTATUS(status) == 0, label);
}

static void tape_pending_parser_regression(void) {
    const char* invalid[] = {
        "FIGHT 0 s f 0 2147483647\nQ 2147483647 2\n",
        "FIGHT 0 s f -1 0\nQ 0 2\n",
        "FIGHT 0 s f 0 2147483648\nQ 0 2\n",
        "FIGHT 2147483648 s f 0 0\nQ 0 2\n",
        "FIGHT 0 s f 1 0\nQ 0 2\n",
        "FIGHT 0 s f 0 0\nQ -1 2\n",
        "FIGHT 0 s f 0 0\nQ 2147483648 2\n",
        "FIGHT 0 s f 0 0\nQ 999999999999999999999999999999 2\n",
        "FIGHT 0 s f 0 0\nQ 1 2\n",
        "FIGHT 0 s f 0 0\nQ 0 -1\n",
        "FIGHT 0 s f 0 0\nQ 0 3\n",
        "FIGHT 0 s f 0 0\nQ 0 4294967297\n",
        "FIGHT 0 s f 0 0\nQ 0\n",
        "FIGHT 0 s f 0 0\nQ 0 2 extra\n",
        "FIGHT 0 s f 0 0\nQ 0x0 2\n",
        "Q 0 2\nFIGHT 0 s f 0 0\n",
        "Q 0 2\n",
        "FIGHT 0 s f 0 0\nQ 0 0\nQ 0 2\n",
        "FIGHT 0 s f 0 0\nQ 0 2\nQ 0 1\n",
        "FIGHT 0 s f 0 0\nQ 0 2\nQ 0 2\n",
        "FIGHT 0 s f 0 0\nQ 0 2\nFIGHT 1 s other 0 0\n",
        "FIGHT 0 s f 0 1\nQ 0 2\n",
    };
    for (size_t i = 0; i < sizeof(invalid) / sizeof(*invalid); i++)
        tape_pending_fixture(invalid[i], invalid[i], 0, 1, 1, 0);
    tape_pending_fixture("excess tick rows", "FIGHT 0 s f 0 0\nQ 0 2\n", 0, 2, 1, 0);
    tape_pending_fixture("wrong tick sequence", "FIGHT 0 s f 0 0\nQ 0 2\n", 1, 1, 1, 0);
    tape_pending_fixture("legacy unavailable", "FIGHT 0 s f 0 0\n", 0, 1, 0, 0);
    tape_pending_fixture("source unavailable", "FIGHT 0 s f 0 0\nQ 0 0\n", 0, 1, 0, 0);
    tape_pending_fixture("known empty", "FIGHT 0 s f 0 0\nQ 0 1\n", 0, 1, 0, 1);
    tape_pending_fixture("observer history", "FIGHT 0 s f 0 0\nQ 0 2\n", 0, 1, 0, 2);
    tape_pending_fixture("maximum tick", "FIGHT 0 s f 2147483647 2147483647\nQ 2147483647 1\n",
            INT_MAX, 1, 0, 1);
}

static void pending_consumer_regression(const RiskfightSnapshot* baseline) {
    static Observer history[4];
    static TapeTick ticks[4];
    TapeHit hits[] = {{102, SIDE_HIM, 20, 16, 'A'}};
    Tape tape = {.lo = 100, .hi = 103, .ticks = ticks, .observer_series = history, .hits = hits, .hit_count = 1};
    for (int i = 0; i < 4; i++) {
        ticks[i].tick = 100 + i;
        ticks[i].pending_known = 2;
        gs_infer_init(&history[i].inf);
        GsInfer* inf = &history[i].inf;
        inf->tracker_current = inf->timed = 1;
        inf->pid.owner = GS_PID_LOCAL;
        inf->local_launch_count = i < 2 ? 1 : 0;
        inf->local_launches[0].tick = 100;
        inf->local_launches[0].exact = 1;
        for (int pid = 0; pid < 3; pid++)
            inf->local_launches[0].by_pid[pid] = (GsSwing){.lands = 102, .least = 20, .most = 20, .accuracy = 1, .arrows = 1};
        inf->pending_launch_count = 1;
        for (int pid = 0; pid < 3; pid++)
            inf->pending_launches[0].by_pid[pid] = (GsSwing){.lands = 101, .least = 10, .most = 10, .accuracy = 1, .arrows = 2};
        if (i >= 2) inf->landed_tick = 102;
    }
    RiskfightSnapshot before = *baseline, changed = *baseline;
    memset(before.pending_pid_count, 0, sizeof(before.pending_pid_count));
    memset(before.pending_pid, 0, sizeof(before.pending_pid));
    memset(before.pending_pid_metadata, 0, sizeof(before.pending_pid_metadata));
    memset(changed.pending_pid_count, 0, sizeof(changed.pending_pid_count));
    memset(changed.pending_pid, 0, sizeof(changed.pending_pid));
    memset(changed.pending_pid_metadata, 0, sizeof(changed.pending_pid_metadata));
    tape_pending_snapshot(&tape, 100, &before);
    int holder = before.pid_holder;
    assert(before.pending_pid_active && before.pending_pid_count[holder][0] == 1);
    assert(before.pending_pid_metadata[holder][0][0].damage_mode == 1);
    assert(before.pending_pid[holder][0][0].damage == 20 && before.pending_pid[holder][0][0].ticks_until_hit == 1);
    hits[0].amount = 99;
    tape_pending_snapshot(&tape, 100, &changed);
    assert(memcmp(before.pending_pid, changed.pending_pid, sizeof(before.pending_pid)) == 0);
    assert(memcmp(before.pending_pid_metadata, changed.pending_pid_metadata, sizeof(before.pending_pid_metadata)) == 0);
    assert(before.pending_pid_count[holder][1] == 2 && before.pending_pid_metadata[holder][1][0].damage_mode == 2);
    RiskfightSnapshot tail = {.pid_holder = 0};
    tape_pending_snapshot(&tape, 101, &tail);
    assert(tail.pending_pid_count[0][1] == 1 && tail.pending_pid[0][1][0].ticks_until_hit == 0);
    before.side[0].hitpoints = 50;
    before.side[0].veng_active = 0;
    before.loadout[0].equipment[GEAR_SLOT_RING] = ITEM_NONE;
    before.loadout[1].equipment[GEAR_SLOT_RING] = ITEM_NONE;
    before.side[1].hitpoints = 50;
    before.side[1].veng_active = 0;
    static Sim sim;
    sim_init(&sim);
    riskfight_load_state(&sim.state, &sim.context, &before);
    uint32_t rng = 42;
    riskfight_sample_pending(&sim.state, &rng);
    uint32_t sampled_rng = rng;
    PendingHit sampled = sim.state.env.players[1].pending_hits[0];
    PendingHitMetadata sampled_metadata = sim.state.env.pending_hit_metadata[1][0];
    assert(sampled_metadata.damage_mode == 1);
    riskfight_sample_pending(&sim.state, &rng);
    assert(rng == sampled_rng);
    assert(memcmp(&sampled, &sim.state.env.players[1].pending_hits[0], sizeof(sampled)) == 0);
    assert(memcmp(&sampled_metadata, &sim.state.env.pending_hit_metadata[1][0], sizeof(sampled_metadata)) == 0);
    pvp_process_incoming_hits(&sim.state.env, 0);
    assert(sim.state.env.players[0].current_hitpoints == 40);
    pvp_process_incoming_hits(&sim.state.env, 1);
    assert(sim.state.env.players[1].current_hitpoints == 50);
    assert(sim.state.env.players[0].num_pending_hits == 1);
    pvp_process_incoming_hits(&sim.state.env, 0);
    assert(sim.state.env.players[0].current_hitpoints == 30);
    pvp_process_incoming_hits(&sim.state.env, 1);
    assert(sim.state.env.players[1].current_hitpoints == 30);
    assert(sim.state.env.players[0].num_pending_hits == 0);
    RiskfightSnapshot after = {.pid_holder = 0};
    tape_pending_snapshot(&tape, 102, &after);
    for (int pid = 0; pid < 2; pid++) {
        assert(after.pending_pid_count[pid][0] == 0);
        assert(after.pending_pid_count[pid][1] == 0);
    }
    history[1].inf.pending_launch_count = 0;
    history[1].inf.reflection_count = 1;
    history[1].inf.reflection_swings[0] = (GsSwing){.lands = 102, .least = 15, .most = 15, .accuracy = 1, .arrows = 1};
    RiskfightSnapshot echo = {.pid_holder = 0};
    tape_pending_snapshot(&tape, 101, &echo);
    assert(echo.pending_pid_count[0][1] == 1 && echo.pending_pid_count[1][1] == 0);
    assert(echo.pending_pid[0][1][0].kind == OSRS_HIT_VENGEANCE && echo.pending_pid[0][1][0].damage == 15);
    gs_unknown_applied_echo(&history[0].inf, 100, 0);
    assert(!tape_pending_available(&tape, 100));
    history[0].inf.echo_unknown = 0;
    ticks[0].pending_known = 0;
    assert(!tape_pending_available(&tape, 100));
}

int main(int argc, char** argv) {
    assert(argc == 2);
    tape_pending_parser_regression();
    static Observer observed[4];
    static TapeTick fixture_ticks[4];
    for (int i = 0; i < 4; i++) fixture_ticks[i] = (TapeTick){.tick = 100 + i, .pending_known = 1};
    observed[1].inf.local_timer = (GsTimerState){.seen = 1, .earliest = 104, .latest = 107};
    observed[2].inf.local_timer = (GsTimerState){.seen = 1, .earliest = 104, .latest = 104};
    observed[3].inf.local_timer = observed[2].inf.local_timer;
    Tape tape = {.lo = 100, .hi = 103, .ticks = fixture_ticks, .observer_series = observed};
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
    RiskfightSnapshot baseline = {0};
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
        if (!snap.pending_known) continue;
        exact++;
        riskfight_load_state(&sim.state, &sim.context, &snap);
        baseline = snap;
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
    assert(exact > 0);
    pending_consumer_regression(&baseline);
    sim.state.env.players[0].has_attack_timer = 1;
    sim.state.env.players[0].attack_timer = -1;
    sim.state.env.players[0].attack_timer_uncapped = -6;
    LaBelief belief = {.hp_low = 30, .hp_high = 90, .timer_seen = 1, .timer_first = 104, .timer_last = 107};
    LaFeasible feasible = {0};
    LaScript rules = {.n = 3};
    for (int i = 0; i < rules.n; i++) {
        rules.due[i] = 111 + i;
        rules.command[i] = (HumanCommand){.kind = HUMAN_COMMAND_SPEC_TOGGLE, .spec_mode = (HumanSpecMode)i};
    }
    FILE* stream = tmpfile();
    sim.state.pending_pid_active = 0; /* This fixture constructs an authoritative fixed queue. */
    for (int source = 0; source < 2; source++) {
        Player* player = &sim.state.env.players[source];
        player->num_pending_hits = 2;
        for (int i = 0; i < 2; i++) {
            player->pending_hits[i] = (PendingHit){.ticks_until_hit = 3 + i, .kind = OSRS_HIT_DIRECT,
                .attack_type = ATTACK_STYLE_MAGIC, .hit_success = 1, .damage = 24 + source + i,
                .defender_prayer_at_attack = PRAYER_NONE};
            sim.state.env.pending_hit_metadata[source][i] = (PendingHitMetadata){.damage_mode = 1 + i,
                .damage_min = 10 + source, .damage_max = 30 + i, .accuracy_bp = 8000 + source + i,
                .smite_snapshot = source + i};
        }
    }
    assert(stream);
    la_dump_request(stream, &sim, &belief, 110, &feasible, (GsStakes){0}, &rules);
    rewind(stream);
    Request request;
    read_request(stream, &request);
    assert(request.id == 110 && request.tick == 110);
    assert(request.attack_history == RISKFIGHT_ATTACK_OBSERVED);
    assert(request.attack_clock_low == -6 && request.attack_clock_high == -6);
    assert(request.belief.timer_seen && request.belief.timer_first == 104 && request.belief.timer_last == 107);
    assert(request.pending_known);
    assert(request.pending_expected == sim.state.env.players[0].num_pending_hits + sim.state.env.players[1].num_pending_hits);
    for (int source = 0; source < 2; source++) {
        assert(request.pending_count[source] == 2);
        for (int i = 0; i < 2; i++) {
            assert(memcmp(&request.pending[source][i], &sim.state.env.players[source].pending_hits[i], sizeof(PendingHit)) == 0);
            assert(memcmp(&request.pending_metadata[source][i], &sim.state.env.pending_hit_metadata[source][i], sizeof(PendingHitMetadata)) == 0);
        }
    }
    RiskfightSnapshot request_snapshot;
    snapshot_of(&request, &request_snapshot);
    assert(!request_snapshot.pending_pid_active);
    assert(memcmp(request_snapshot.pending, request.pending, sizeof(request.pending)) == 0);
    assert(memcmp(request_snapshot.pending_metadata, request.pending_metadata, sizeof(request.pending_metadata)) == 0);
    assert(request.rules_known && request.rules.n == 3);
    assert(request.rules.command[0].kind == HUMAN_COMMAND_SPEC_TOGGLE && request.rules.command[0].spec_mode == HUMAN_SPEC_TOGGLE);
    assert(request.rules.op[1] == 1 && request.rules.op[2] == 2);
    assert(request.rules.fixed[0] == 1 && request.rules.fixed[1] == 1 && request.rules.fixed[2] == 1);
    fclose(stream);
    sim.state.pending_pid_active = 1;
    for (int holder = 0; holder < 2; holder++) for (int source = 0; source < 2; source++) {
        sim.state.pending_pid_count[holder][source] = holder + 1;
        memcpy(sim.state.pending_pid[holder][source], sim.state.env.players[source].pending_hits, (size_t)(holder + 1) * sizeof(PendingHit));
        memcpy(sim.state.pending_pid_metadata[holder][source], sim.state.env.pending_hit_metadata[source], (size_t)(holder + 1) * sizeof(PendingHitMetadata));
    }
    stream = tmpfile();
    assert(stream);
    la_dump_request(stream, &sim, &belief, 110, &feasible, (GsStakes){0}, &rules);
    rewind(stream);
    read_request(stream, &request);
    assert(request.pending_pid_active && request.pending_expected == 6);
    assert(!memcmp(request.pending_pid_count, sim.state.pending_pid_count, sizeof(request.pending_pid_count)));
    for (int holder = 0; holder < 2; holder++) for (int source = 0; source < 2; source++) {
        size_t count = (size_t)request.pending_pid_count[holder][source];
        assert(!memcmp(request.pending_pid[holder][source], sim.state.pending_pid[holder][source], count * sizeof(PendingHit)));
        assert(!memcmp(request.pending_pid_metadata[holder][source], sim.state.pending_pid_metadata[holder][source], count * sizeof(PendingHitMetadata)));
    }
    snapshot_of(&request, &request_snapshot);
    assert(request_snapshot.pending_pid_active);
    assert(!memcmp(request_snapshot.pending_pid, request.pending_pid, sizeof(request.pending_pid)));
    assert(!memcmp(request_snapshot.pending_pid_metadata, request.pending_pid_metadata, sizeof(request.pending_pid_metadata)));
    fclose(stream);
    stream = tmpfile();
    assert(stream);
    rules.native_rules = &sim.ours;
    la_dump_request(stream, &sim, &belief, 110, &feasible, (GsStakes){0}, &rules);
    rewind(stream);
    read_request(stream, &request);
    assert(!request.rules_known && request.rules.n == 0);
    fclose(stream);
    printf("Hazard feature parity exact %d retained unknown %d source_frames %d\n", exact, unknown, recorded.hi - recorded.lo);
    puts("Calibrator attack-clock eligibility passed");
    return 0;
}
