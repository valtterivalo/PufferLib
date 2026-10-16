/* Run both normally and with -DNDEBUG: external request checks must survive release builds. */
#define main lookahead_server_main
#include "gs_lookahead_server.c"
#undef main
#include <sys/wait.h>
#include <unistd.h>

#define CHECK(c) do { if (!(c)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #c); exit(1); } } while (0)

static void prefix(FILE* f) {
    fputs("LA 41 100\nB 99 99 100 100 0 0 0 1 104 104 0 0 0 0 -1\n"
          "O 50 90 118 118 118 99 100 0 0 0 0 27 0 -1 0 0 0 0 0 0 4 0 4 27690 0 0 1 OBSERVED -1 -1\nW", f);
    for (int i = 0; i < TAPE_WORN; i++) fprintf(f, " %d", WORN_TO_GEAR[i] == GEAR_SLOT_WEAPON ? 27690 : -1);
    fputs("\nG", f);
    for (int i = 0; i < TAPE_BAG; i++) fprintf(f, " %d", i == 0 ? 13441 : -1);
    fputc('\n', f);
    fputs("SHIELD OBSERVED -1\nARMOR OBSERVED", f);
    for (int slot = 0; slot < GS_ARMOR_SLOTS; slot++) fputs(" -1", f);
    fputc('\n', f);
}

static FILE* wire(const char* body) {
    FILE* f = tmpfile();
    CHECK(f != NULL);
    prefix(f);
    CHECK(fputs(body, f) >= 0);
    rewind(f);
    return f;
}

static Request parse(const char* body) {
    FILE* f = wire(body);
    Request q;
    read_request(f, &q);
    CHECK(fclose(f) == 0);
    return q;
}

/* A malformed external request must fail through the server's status-2 channel,
 * not crash, accept a prefix, or return a fabricated empty queue. */
static void rejects_file(FILE* f, const char* label) {
    fflush(NULL);
    pid_t child = fork();
    CHECK(child >= 0);
    if (child == 0) {
        Request q;
        read_request(f, &q);
        _exit(99);
    }
    int status;
    CHECK(waitpid(child, &status, 0) == child);
    CHECK(fclose(f) == 0);
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 2) {
        fprintf(stderr, "malformed request accepted or crashed: %s, status=%d\n", label, status);
        exit(1);
    }
}

static void rejects(const char* body) { rejects_file(wire(body), body); }

static void test_boundary(void) {
    const char* bad[] = {
        "R KNOWN 0\nEND\n", "P KNOWN 0\nEND\n",
        "P UNKNOWN 0\nR KNOWN 0\nEND\n", "P MAYBE\nR KNOWN 0\nEND\n",
        "P KNOWN -1\nR KNOWN 0\nEND\n", "P KNOWN 2147483648\nR KNOWN 0\nEND\n",
        "P KNOWN 999999999999999999999999999999\nR KNOWN 0\nEND\n",
        "P KNOWN 1\nR KNOWN 0\nEND\n", "P KNOWN 0\nP KNOWN 0\nR KNOWN 0\nEND\n",
        "P UNKNOWN\nD 0 1 0 0 0 1 1 20 0 20 10000 0 0\nR KNOWN 0\nEND\n",
        "P KNOWN 0\nR UNKNOWN 0\nEND\n", "P KNOWN 0\nR MAYBE\nEND\n",
        "P KNOWN 0\nR KNOWN -1\nEND\n", "P KNOWN 0\nR KNOWN 1\nEND\n",
        "P KNOWN 0\nR UNKNOWN\nC 101 1 0 1\nEND\n",
        "P KNOWN 0\nR KNOWN 0\nC 101 1 0 1\nEND\n",
        "P KNOWN 0\nR KNOWN 1\nC 101 14 0 1\nEND\n",
        "P KNOWN 0\nR KNOWN 1\nC 101 -1 0 1\nEND\n",
        "P KNOWN 0\nR KNOWN 1\nC 100 1 0 1\nEND\n",
        "P KNOWN 0\nR KNOWN 1\nC 101 0 28 1\nEND\n",
        "P KNOWN 0\nR KNOWN 1\nC 101 0 -1 1\nEND\n",
        "P KNOWN 0\nR KNOWN 1\nC 101 2 1 1\nEND\n",
        "P KNOWN 0\nR KNOWN 1\nC 101 8 101 1\nEND\n",
        "P KNOWN 0\nR KNOWN 2\nC 102 1 0 1\nC 101 1 0 1\nEND\n",
        "P KNOWN 0\nR KNOWN 1\nC 101 1 0 1 extra\nEND\n",
        "P KNOWN 0\nR KNOWN 1\nC 101 1 0\nEND\n",
        "P KNOWN 0\nR KNOWN 1\nC 101 1 0 -1\nEND\n",
        "P KNOWN 0\nR KNOWN 1\nC 101 1 0 2\nEND\n",
        "P KNOWN 0\nR KNOWN 0\nBOGUS\nEND\n",
        "P KNOWN 0\nR KNOWN 0\n", "P KNOWN 0\nR KNOWN 0\nEND",
        "P VARIANTS 1 0\nR KNOWN 0\nEND\n",
        "P VARIANTS 1 0\nD 1 1 0 0 0 0 1 2 0 20 40 10000 0 -1\nR KNOWN 0\nEND\n",
        "P VARIANTS 1 0\nD 2 1 0 0 0 0 1 2 0 20 40 10000 0 -1\nR KNOWN 0\nEND\n",
        "P VARIANTS 1 0\nD 0 2 0 0 0 0 1 2 0 20 40 10000 0 -1\nR KNOWN 0\nEND\n",
        "P KNOWN 0\nR KNOWN 1\nC 101 14 0 0 0\nEND\n",
        "P KNOWN 0\nR KNOWN 1\nC 101 15 0 0 1 -27690\nEND\n",
        "RA OBSERVED -1\nP KNOWN 0\nR KNOWN 0\nEND\n",
        "RA OBSERVED 2\nP KNOWN 0\nR KNOWN 0\nEND\n",
        "RA UNKNOWN 1\nP KNOWN 0\nR KNOWN 0\nEND\n",
        "RA OFF_PRIOR 1\nP KNOWN 0\nR KNOWN 0\nEND\n",
        "RA OBSERVED 1\nRA UNKNOWN\nP KNOWN 0\nR KNOWN 0\nEND\n"
    };
    for (size_t i = 0; i < sizeof(bad) / sizeof(*bad); i++) rejects(bad[i]);
    /* Each field independently crosses a pending-hit boundary. */
    const int field[] = {0, 0, 1, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 12};
    const int value[] = {-1, 2, -1, 1001, 999, 999, 2, 2, 3, -1, -1, 1001, 10001, 999, 2, INT_MAX};
    for (size_t i = 0; i < sizeof(field) / sizeof(*field); i++) {
        int d[] = {0, 1, OSRS_HIT_DIRECT, ATTACK_STYLE_MAGIC, 0, 1, 1, 20, 0, 20, 10000, PRAYER_NONE, 0};
        d[field[i]] = value[i];
        FILE* f = tmpfile(); CHECK(f != NULL); prefix(f);
        fputs("P KNOWN 1\nD", f);
        for (int j = 0; j < 13; j++) fprintf(f, " %d", d[j]);
        fputs("\nR KNOWN 0\nEND\n", f); rewind(f);
        rejects_file(f, "pending field boundary");
    }
    char body[128];
    snprintf(body, sizeof(body), "P KNOWN %d\nR KNOWN 0\nEND\n", 2 * MAX_PENDING_HITS + 1);
    rejects(body);
    snprintf(body, sizeof(body), "P KNOWN 0\nR KNOWN %d\nEND\n", LA_SCRIPT + 1);
    rejects(body);
    FILE* f = tmpfile(); CHECK(f != NULL); prefix(f);
    fprintf(f, "P KNOWN %d\n", MAX_PENDING_HITS + 1);
    for (int i = 0; i <= MAX_PENDING_HITS; i++) fputs("D 0 1 0 0 0 1 1 20 0 20 10000 0 0\n", f);
    fputs("R KNOWN 0\nEND\n", f); rewind(f);
    rejects_file(f, "per-source pending capacity");
    f = tmpfile(); CHECK(f != NULL); prefix(f);
    fputs("P KNOWN ", f);
    for (int i = 0; i < 5000; i++) fputc('0', f);
    fputs("\nR KNOWN 0\nEND\n", f); rewind(f);
    rejects_file(f, "overlong line");
    f = tmpfile(); CHECK(f != NULL); prefix(f);
    fprintf(f, "P KNOWN %d\n", 2 * MAX_PENDING_HITS);
    for (int source = 0; source < 2; source++)
        for (int i = 0; i < MAX_PENDING_HITS; i++)
            fprintf(f, "D %d 1 %d %d 0 1 %d %d %d %d %d %d %d\n",
                source, OSRS_HIT_DIRECT, ATTACK_STYLE_MAGIC, i % 3, i, source, 100 + i, 9000 + i, PRAYER_NONE, source - 1);
    fprintf(f, "R KNOWN %d\n", LA_SCRIPT);
    for (int i = 0; i < LA_SCRIPT; i++) fprintf(f, "C %d 1 0 1\n", 101 + i);
    fputs("END\n", f); rewind(f);
    Request q;
    read_request(f, &q);
    CHECK(fclose(f) == 0);
    CHECK(q.pending_read == 2 * MAX_PENDING_HITS && q.rules.n == LA_SCRIPT);
    for (int source = 0; source < 2; source++) {
        CHECK(q.pending_count[source] == MAX_PENDING_HITS);
        for (int i = 0; i < MAX_PENDING_HITS; i++) {
            CHECK(q.pending[source][i].damage == i);
            const PendingHitMetadata* metadata = &q.pending_metadata[source][i];
            CHECK(metadata->damage_mode == i % 3 && metadata->damage_min == source);
            CHECK(metadata->damage_max == 100 + i && metadata->accuracy_bp == 9000 + i);
            CHECK(metadata->smite_snapshot == source);
        }
    }
    for (int i = 0; i < LA_SCRIPT; i++) CHECK(q.rules.due[i] == 101 + i);
}

static void test_knowledge(void) {
    Request q = parse("P KNOWN 0\nR KNOWN 0\nEND\n");
    CHECK(q.pending_known && q.pending_read == 0 && q.rules.n == 0);
    CHECK(request_unavailable(&q) == NULL);
    q = parse("P UNKNOWN\nR KNOWN 0\nEND\n");
    CHECK(!q.pending_known && !strcmp(request_unavailable(&q), "pending_unknown"));
    rejects("P KNOWN 0\nR UNKNOWN\nEND\n"); /* R UNKNOWN is not part of protocol v2 */
    /* Own bag items the sim has no model for become inert filler, never UNAVAILABLE. */
    q = parse("P KNOWN 0\nR KNOWN 0\nEND\n");
    const int inert[] = {229, 995, 27997, 30000, 26651, 28098, 24589, 9999};
    for (size_t i = 0; i < sizeof(inert) / sizeof(inert[0]); i++) {
        q.bag[0] = inert[i];
        request_gear_priors(&q);
        CHECK(q.bag[0] == INERT_ID && request_unavailable(&q) == NULL);
        RiskfightSnapshot snapshot;
        snapshot_of(&q, &snapshot);
        OsrsInventoryCell cell = osrs_inventory_cell_from_content_code(snapshot.loadout[0].inventory[0]);
        CHECK(!osrs_inventory_cell_is_empty(&cell));
        CHECK(osrs_inventory_cell_metadata(&cell)->consumable_kind == OSRS_CONSUMABLE_NONE);
    }
    q.worn[0] = 9999; /* an id with no stats anywhere: bare slot */
    request_gear_priors(&q);
    CHECK(q.worn[0] == -1 && request_unavailable(&q) == NULL);
    q = parse("P KNOWN 0\nR KNOWN 3\nC 101 14 0 0 1 -6685\nC 102 15 0 0 1 27690\nC 103 16 0 0 0\nEND\n");
    CHECK(request_unavailable(&q) == NULL && q.rules.n == 3 && q.rules.op[0] == 8 && q.rules.op[1] == 9 && q.rules.op[2] == 10);
    /* Several bag slots matching one recipe are no longer a reason to give up: the sim binds the first match. */
    q.bag[0] = 6685; q.bag[1] = 6687;
    CHECK(request_unavailable(&q) == NULL);
    /* Recipe candidates the engine could not have emitted are protocol violations. */
    rejects("P KNOWN 0\nR KNOWN 2\nC 101 14 0 0 1 -24589\nC 102 1 0 0 0\nEND\n");
    rejects("P KNOWN 0\nR KNOWN 1\nC 101 14 0 0 1 24592\nEND\n");
    rejects("P KNOWN 0\nR KNOWN 2\nC 101 15 0 0 1 20513\nC 102 16 0 0 1 20513\nEND\n");
}

static Sim loaded;
static void test_auto_retaliate(void) {
    const char* sources[] = {"", "RA UNKNOWN\n", "RA OBSERVED 0\n", "RA OBSERVED 1\n", "RA OFF_PRIOR\n"};
    for (int source = 0; source < 5; source++) {
        char body[512];
        snprintf(body, sizeof(body), "%sPS 0 KNOWN 0 0 0\nPS 1 CLEAR_PRIOR\n"
            "P KNOWN 1\nD 1 1 0 0 0 1 1 24 0 24 10000 0 0\nR KNOWN 0\nEND\n", sources[source]);
        Request q = parse(body);
        CHECK(request_unavailable(&q) == NULL);
        CHECK(q.poison_source[0] == 1 && q.poison_source[1] == 3);
        CHECK(q.pending_count[1] == 1 && q.pending[1][0].damage == 24);
        snprintf(body, sizeof(body), "%sP KNOWN 0\nR KNOWN 1\nC 101 17 0 0\nEND\n", sources[source]);
        q = parse(body);
        const char* unavailable = request_unavailable(&q);
        if (source >= 3) CHECK(unavailable == NULL);
        else CHECK(unavailable && !strcmp(unavailable, source == 2 ? "auto_retaliate_on" : "auto_retaliate_unknown"));
    }
    Request q = parse("RA OFF_PRIOR\nP VARIANTS 1 1\n"
        "D 0 1 2 0 0 0 1 1 24 0 24 10000 0 0\n"
        "D 1 1 5 0 0 0 1 1 24 0 24 10000 0 0\nR KNOWN 0\nEND\n");
    /* Isolate PID-conditioned pending queues from the separately observed swing clock. */
    q.belief.timer_seen = 0;
    request_alignment_windows(&q);
    CHECK(q.feasible.incoming_count == 1 && q.feasible.incoming[0] == 103);
    q.belief.pid = GS_PID_OPPONENT;
    q.feasible.incoming_count = 0;
    request_alignment_windows(&q);
    CHECK(q.feasible.incoming_count == 1 && q.feasible.incoming[0] == 106);
    q.belief.pid = GS_PID_UNKNOWN;
    q.feasible.incoming_count = 0;
    request_alignment_windows(&q);
    CHECK(q.feasible.pid == GS_PID_UNKNOWN && q.feasible.incoming_count == 0);
}

static void load(Request* q) {
    RiskfightSnapshot snap;
    snapshot_of(q, &snap);
    CHECK(snap.side[0].hitpoints == q->ours.hp && snap.side[1].hitpoints > 0);
    CHECK(!snap.pending_pid_active);
    CHECK(memcmp(snap.pending, q->pending, sizeof(q->pending)) == 0);
    CHECK(memcmp(snap.pending_metadata, q->pending_metadata, sizeof(q->pending_metadata)) == 0);
    /* Isolate launched damage and requested commands from autonomous interaction. */
    snap.side[0].interacting = snap.side[1].interacting = 0;
    snap.side[0].veng_active = snap.side[1].veng_active = 0;
    snap.loadout[0].equipment[GEAR_SLOT_RING] = snap.loadout[1].equipment[GEAR_SLOT_RING] = ITEM_NONE;
    riskfight_load_state(&loaded.state, &loaded.context, &snap);
    for (int source = 0; source < 2; source++) {
        CHECK(loaded.state.env.players[source].num_pending_hits == q->pending_count[source]);
        for (int i = 0; i < q->pending_count[source]; i++) {
            CHECK(memcmp(&loaded.state.env.players[source].pending_hits[i], &q->pending[source][i], sizeof(PendingHit)) == 0);
            CHECK(memcmp(&loaded.state.env.pending_hit_metadata[source][i], &q->pending_metadata[source][i], sizeof(PendingHitMetadata)) == 0);
        }
    }
}

static void test_loaded_damage(void) {
    char body[256];
    for (int source = 0; source < 2; source++) {
        snprintf(body, sizeof(body), "P KNOWN 1\nD %d 1 %d %d 0 1 1 1000 0 1000 10000 %d 0\nR KNOWN 0\nEND\n",
            source, OSRS_HIT_DIRECT, ATTACK_STYLE_MAGIC, PRAYER_NONE);
        Request q = parse(body);
        CHECK(q.pending_count[source] == 1 && q.pending[source][0].damage == 1000);
        load(&q);
        Player* target = &loaded.state.env.players[1 - source];
        int hp = target->current_hitpoints;
        CHECK(hp > 0);
        target->prayer = PRAYER_PROTECT_MAGIC;
        target->equipped[GEAR_SLOT_BODY] = ITEM_NONE;
        HumanCommandQueue idle = {0};
        riskfight_step_queues(&loaded.state, &loaded.context, &idle, &idle);
        CHECK(target->current_hitpoints == hp);
        riskfight_step_queues(&loaded.state, &loaded.context, &idle, &idle);
        CHECK(target->current_hitpoints == 0);
        CHECK(loaded.state.env.players[source].num_pending_hits == 0);
    }
    snprintf(body, sizeof(body), "P KNOWN 1\nD 1 1 %d %d 0 1 1 24 0 24 10000 %d 0\nR KNOWN 0\nEND\n",
        OSRS_HIT_DIRECT, ATTACK_STYLE_MAGIC, PRAYER_NONE);
    Request q = parse(body);
    load(&q);
    CHECK(loaded.state.env.pending_hit_metadata[1][0].damage_mode == 1);
    CHECK(loaded.state.env.pending_hit_metadata[1][0].smite_snapshot == 1);
    Player* us = &loaded.state.env.players[0];
    us->prayer = PRAYER_PROTECT_MAGIC;
    us->equipped[GEAR_SLOT_BODY] = ITEM_NONE;
    HumanCommandQueue idle = {0};
    riskfight_step_queues(&loaded.state, &loaded.context, &idle, &idle);
    CHECK(us->current_hitpoints == 50);
    riskfight_step_queues(&loaded.state, &loaded.context, &idle, &idle);
    CHECK(us->current_hitpoints == 26);
    riskfight_step_queues(&loaded.state, &loaded.context, &idle, &idle);
    CHECK(us->current_hitpoints == 26);
}

static void test_loaded_commands(void) {
    Request q = parse("P KNOWN 0\nR KNOWN 2\nC 101 12 13441 1\nC 101 1 0 1\nEND\n");
    load(&q);
    HumanInput* hi = &loaded.context.policy_commands[0];
    human_input_clear_pending(hi);
    int cursor = 0, resume = 0;
    la_script_step(&q.rules, &loaded.state, hi, 101, &cursor, &resume, 0, 0);
    CHECK(cursor == 2 && hi->commands.count == 2);
    HumanCommandQueue idle = {0};
    riskfight_step_queues(&loaded.state, &loaded.context, &hi->commands, &idle);
    Player* us = &loaded.state.env.players[0];
    CHECK(us->current_hitpoints == 72);
    CHECK(osrs_inventory_cell_is_empty(&us->inventory_cells[0]));
    CHECK(us->attack_timer > 0 && loaded.state.spec_voidwaker[0] == 0);
    for (int wanted = 0; wanted <= 1; wanted++) {
        char body[128];
        snprintf(body, sizeof(body), "P KNOWN 0\nR KNOWN 2\nC 101 %d 0 1\nC 101 %d 0 1\nEND\n", wanted ? 2 : 4, wanted ? 2 : 4);
        q = parse(body); load(&q);
        loaded.state.env.players[0].spec_armed = !wanted;
        human_input_clear_pending(hi); cursor = resume = 0;
        la_script_step(&q.rules, &loaded.state, hi, 101, &cursor, &resume, 0, 0);
        CHECK(cursor == 2 && hi->commands.count == 2);
        riskfight_step_queues(&loaded.state, &loaded.context, &hi->commands, &idle);
        CHECK(loaded.state.env.players[0].spec_armed == wanted);
    }
}

static void test_loaded_timing(void) {
    /* Six preceding clicks force the unsent meal past the cutoff even at the
     * minimum press gaps; the same already-known meal must remain at tick 103. */
    for (int fixed = 0; fixed <= 1; fixed++) {
        FILE* f = tmpfile();
        CHECK(f != NULL);
        prefix(f);
        fputs("P KNOWN 0\nR KNOWN 7\n", f);
        for (int i = 0; i < 6; i++) fprintf(f, "C 103 4 0 %d\n", fixed);
        fprintf(f, "C 103 12 13441 %d\nEND\n", fixed);
        rewind(f);
        Request q;
        read_request(f, &q);
        CHECK(fclose(f) == 0);
        CHECK(request_unavailable(&q) == NULL);
        load(&q);
        LaScript scripts[LA_MACROS];
        int feasible[LA_MACROS];
        la_scripts(&loaded.state, q.tick, &q.feasible, &q.rules, scripts, feasible);
        LaScript* script = &scripts[LA_CONTINUE];
        CHECK(feasible[LA_CONTINUE]);
        int meal_tick = -1;
        HumanInput* hi = &loaded.context.policy_commands[0];
        HumanCommandQueue idle = {0};
        Player* us = &loaded.state.env.players[0];
        int cursor = 0, resume = 0;
        CHECK(us->current_hitpoints == 50);
        for (int now = q.tick + 1; now <= q.tick + 32 && cursor < script->n; now++) {
            human_input_clear_pending(hi);
            la_script_step(script, &loaded.state, hi, now, &cursor, &resume, 0, 0);
            if (now < 103) CHECK(cursor == 0 && hi->commands.count == 0);
            riskfight_step_queues(&loaded.state, &loaded.context, &hi->commands, &idle);
            if (osrs_inventory_cell_is_empty(&us->inventory_cells[0]) && meal_tick < 0) meal_tick = now;
            CHECK(us->current_hitpoints == (meal_tick < 0 ? 50 : 72));
        }
        CHECK(fixed ? meal_tick == 103 : meal_tick > 103);
        CHECK(cursor == script->n && !us->spec_armed);
    }
}

static void test_mixed_timing(void) {
    /* An unsent lower bound may precede the prior known landing, but execution
     * must still respect the selected plan's command order. */
    Request q = parse("P KNOWN 0\nR KNOWN 2\nC 103 4 0 1\nC 101 12 13441 0\nEND\n");
    CHECK(request_unavailable(&q) == NULL);
    load(&q);
    LaScript scripts[LA_MACROS];
    int feasible[LA_MACROS];
    la_scripts(&loaded.state, q.tick, &q.feasible, &q.rules, scripts, feasible);
    LaScript* script = &scripts[LA_CONTINUE];
    CHECK(feasible[LA_CONTINUE]);
    HumanInput* hi = &loaded.context.policy_commands[0];
    HumanCommandQueue idle = {0};
    int cursor = 0, resume = 0;
    for (int now = q.tick + 1; now <= q.tick + 32 && cursor < script->n; now++) {
        human_input_clear_pending(hi);
        la_script_step(script, &loaded.state, hi, now, &cursor, &resume, 0, 0);
        if (now < 103) CHECK(cursor == 0 && hi->commands.count == 0);
        riskfight_step_queues(&loaded.state, &loaded.context, &hi->commands, &idle);
        CHECK(loaded.state.env.players[0].current_hitpoints ==
            (osrs_inventory_cell_is_empty(&loaded.state.env.players[0].inventory_cells[0]) ? 72 : 50));
    }
    CHECK(cursor == 2);
    CHECK(osrs_inventory_cell_is_empty(&loaded.state.env.players[0].inventory_cells[0]));
}

static void test_variant_worlds(void) {
    for (int ranged = 0; ranged <= 1; ranged++) {
        char body[256];
        snprintf(body, sizeof(body), "P VARIANTS 1 %d\nD 0 1 %d 0 0 0 1 2 0 20 40 10000 0 -1\n%sR KNOWN 0\nEND\n",
                ranged, ranged, ranged ? "D 1 1 0 0 0 0 1 2 0 20 40 10000 0 -1\n" : "");
        Request q = parse(body);
        q.belief.pid = GS_PID_UNKNOWN;
        q.belief.gear = (GsObservedGear){.source = GS_GEAR_COMPOSITION_OBSERVED, .weapon = 27690, .shield = -1};
        for (int slot = 0; slot < GS_ARMOR_SLOTS; slot++) q.belief.gear.armor[slot] = -1;
        RiskfightSnapshot snap;
        snapshot_of(&q, &snap);
        CHECK(snap.pending_pid_active && snap.pending_pid_count[0][1] == 1 && snap.pending_pid_count[1][1] == ranged);
        snap.side[0].interacting = snap.side[1].interacting = 0;
        snap.side[0].veng_active = snap.side[1].veng_active = 0;
        riskfight_load_state(&loaded.state, &loaded.context, &snap);
        int holders[2] = {0};
        for (uint32_t seed = 1; seed <= 64; seed++) {
            uint32_t rng = seed;
            static RiskfightState world;
            Hist hist;
            la_world(&loaded.state, &loaded.hist, &q.belief, q.tick, &rng, &world, &hist);
            int holder = world.env.pid_holder, present = ranged || holder == 0;
            holders[holder]++;
            CHECK(!world.pending_pid_active && world.env.players[1].num_pending_hits == present);
            if (present) {
                CHECK(world.env.players[1].pending_hits[0].ticks_until_hit == (ranged && holder == 0));
                CHECK(world.env.players[1].pending_hits[0].damage >= 20 && world.env.players[1].pending_hits[0].damage <= 40);
                CHECK(world.env.pending_hit_metadata[1][0].damage_mode == 1);
            }
            uint32_t once = rng;
            riskfight_sample_pending(&world, &rng);
            CHECK(rng == once);
        }
        CHECK(holders[0] && holders[1] && loaded.state.pending_pid_active);
    }
}

/* U carries the first damage on him that no meal has answered; absent or `none` it is no damage. */
static void test_unanswered_damage(void) {
    Request q = parse("U 412 37\nRA OFF_PRIOR\nP KNOWN 0\nR KNOWN 0\nEND\n");
    CHECK(q.unanswered_since == 412 && q.unanswered_damage == 37);
    q = parse("U none 0\nRA OFF_PRIOR\nP KNOWN 0\nR KNOWN 0\nEND\n");
    CHECK(q.unanswered_since == WARNING_NONE && q.unanswered_damage == 0);
    q = parse("RA OFF_PRIOR\nP KNOWN 0\nR KNOWN 0\nEND\n");
    CHECK(q.unanswered_since == WARNING_NONE && q.unanswered_damage == 0);
}

int main(void) {
    test_boundary();
    test_unanswered_damage();
    test_knowledge();
    observer_register_items();
    sim_init(&loaded);
    test_auto_retaliate();
    test_loaded_damage();
    test_loaded_commands();
    test_loaded_timing();
    test_mixed_timing();
    test_variant_worlds();
    puts("Lookahead pending protocol contracts passed");
    return 0;
}
