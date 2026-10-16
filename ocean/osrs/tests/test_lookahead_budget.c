/* The anytime budget of the lookahead server (la_evaluate g_la_deadline, gs_lookahead_server.c --budget-ms):
 *  - a budget that expires stops sampling worlds at a prefix [0, N); the response is byte-identical to an unbudgeted run with N worlds (a pure function of
 *    the completed worlds), at a stage boundary or in the middle of a stage, and below the minimum the evaluation finishes anyway and says BUDGET exceeded;
 *  - an ample budget answers exactly the unbudgeted response and says nothing; budget 0 never sets a deadline;
 *  - a helper that does not publish (a descheduled thread: test hook g_la_helper_stall_us) is not waited for under a deadline: its worlds are stolen, the
 *    answer comes in far less than the helper's stall and is unchanged; la_quiesce() then lets the helper leave before the next request is loaded.
 *
 *   cc -std=c11 -O2 -I. -o test_lookahead_budget ocean/osrs/tests/test_lookahead_budget.c -lm -lpthread      (cwd = repo root; reads the .fit files of tests/fixtures) */
#define main lookahead_server_main
#include "gs_lookahead_server.c"
#undef main

#define CHECK(c) do { if (!(c)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #c); exit(1); } } while (0)

static Request g_request;

static void load_request(int i) {
    const char* text = WARM_REQUESTS[i % WARM_REQUEST_COUNT];
    FILE* in = fmemopen((void*)text, strlen(text), "r");
    CHECK(in != NULL);
    read_request(in, &g_request);
    fclose(in);
    g_request.id = 1000 + i;
    request_declare_priors(&g_request);
    CHECK(server_unavailable(&g_request) == NULL);
}

typedef struct { char line[8192]; int completed; int budget_note; double seconds; int alive_worlds_min; long stolen; } Answer;

/* One decision on the loaded request under `budget_ms` and `worlds`; the response text without its BUDGET note (micros fixed at 0). */
static void decide(double budget_ms, int worlds, Answer* a) {
    g_budget_ms = budget_ms;
    g_la_worlds = worlds;
    long stolen = g_la_stolen;
    double began = la_seconds();
    server_decide(&g_request, began);
    a->seconds = la_seconds() - began;
    a->stolen = g_la_stolen - stolen;
    a->completed = g_la_completed;
    char* text = NULL;
    size_t size = 0;
    FILE* out = open_memstream(&text, &size);
    server_respond(out, &g_request, 0);
    fclose(out);
    CHECK(size < sizeof(a->line));
    char* note = strstr(text, " BUDGET exceeded ");
    a->budget_note = note != NULL;
    if (note) { CHECK(atol(note + strlen(" BUDGET exceeded ")) > 0); strcpy(note, "\n"); } /* the note is the last token before the newline */
    strcpy(a->line, text);
    free(text);
    la_quiesce(); /* before the caller loads the next request, as the server does after each answer */
    a->alive_worlds_min = 1 << 30;
    for (int m = 0; m < LA_MACROS; m++)
        if (g_decision.alive[m] && g_decision.feasible[m] && g_decision.score[m].worlds < a->alive_worlds_min) a->alive_worlds_min = g_decision.score[m].worlds;
}

static void same(const Answer* a, const Answer* b) {
    if (strcmp(a->line, b->line)) fprintf(stderr, "budgeted: %.400s\nreference: %.400s\n", a->line, b->line);
    CHECK(!strcmp(a->line, b->line));
}

static void test_prefix_equals_unbudgeted(int i) {
    load_request(i);
    Answer full, tiny, mid, ample, ref;
    decide(0, SERVER_WORLDS, &full);
    CHECK(full.completed == SERVER_WORLDS && !full.budget_note && g_la_overrun == 0);

    /* The deadline has passed before the first world: exactly the minimum runs (at a stage boundary, and inside a stage), the overrun is reported. */
    int minimums[] = {16, 20, 64, 100};
    for (size_t k = 0; k < sizeof(minimums) / sizeof(*minimums); k++) {
        g_la_min_worlds = minimums[k];
        decide(1e-6, SERVER_WORLDS, &tiny);
        CHECK(tiny.completed == minimums[k] && tiny.completed < SERVER_WORLDS);
        CHECK(tiny.budget_note && g_la_overrun > 0);
        CHECK(tiny.alive_worlds_min == minimums[k]); /* every macro still alive when the evaluation stopped has all the completed worlds */
        decide(0, minimums[k], &ref);
        same(&tiny, &ref);
    }

    /* A budget that cuts the evaluation somewhere after the minimum: whatever prefix completed, the answer is that prefix's unbudgeted answer. */
    g_la_min_worlds = 16;
    for (double share = 0.15; share < 0.9; share += 0.25) {
        decide(full.seconds * 1e3 * share, SERVER_WORLDS, &mid);
        CHECK(mid.completed >= 16 && mid.completed <= SERVER_WORLDS);
        decide(0, mid.completed, &ref);
        same(&mid, &ref);
        CHECK(mid.alive_worlds_min == mid.completed);
        printf("request %d budget %.2f ms (full %.2f ms): %d worlds, %.2f ms\n", i, full.seconds * 1e3 * share, full.seconds * 1e3, mid.completed, mid.seconds * 1e3);
    }

    /* A budget nothing reaches changes nothing. */
    g_la_min_worlds = SERVER_MIN_WORLDS;
    decide(5000, SERVER_WORLDS, &ample);
    CHECK(ample.completed == SERVER_WORLDS && !ample.budget_note && g_la_overrun == 0);
    same(&ample, &full);
}

/* Surviving macros hold at least the minimum when the deadline passed early; the committed `continue` is always among them. */
static void test_minimum_for_survivors(void) {
    load_request(0);
    Answer a;
    g_la_min_worlds = SERVER_MIN_WORLDS;
    decide(1e-6, SERVER_WORLDS, &a);
    CHECK(a.completed == SERVER_MIN_WORLDS);
    CHECK(g_decision.alive[LA_CONTINUE] && g_decision.score[LA_CONTINUE].worlds == SERVER_MIN_WORLDS);
    for (int m = 0; m < LA_MACROS; m++)
        if (g_decision.alive[m] && g_decision.feasible[m]) CHECK(g_decision.score[m].worlds == SERVER_MIN_WORLDS);
}

/* Sleeping helpers (each world they claim takes 150 ms to publish) never delay a decision under a budget: the caller finishes and steals what they hold. */
static void test_stalled_helpers(void) {
    Answer full[WARM_REQUEST_COUNT];
    g_la_min_worlds = SERVER_MIN_WORLDS;
    for (int i = 0; i < WARM_REQUEST_COUNT; i++) { load_request(i); decide(0, SERVER_WORLDS, &full[i]); }
    g_la_helper_stall_us = 150000;
    long stolen = 0;
    for (int round = 0; round < 8; round++) {
        int i = round % WARM_REQUEST_COUNT;
        load_request(i);
        Answer a;
        decide(5000, SERVER_WORLDS, &a); /* ample: the whole prefix completes, the deadline only enables stealing */
        CHECK(a.completed == SERVER_WORLDS && !a.budget_note);
        CHECK(a.seconds < 0.1);
        same(&a, &full[i]);
        stolen += a.stolen;
    }
    CHECK(stolen > 0);
    g_la_helper_stall_us = 0;
    for (int i = 0; i < WARM_REQUEST_COUNT; i++) { /* with the stall gone the helpers publish their own worlds again */
        load_request(i);
        Answer a;
        decide(5000, SERVER_WORLDS, &a);
        same(&a, &full[i]);
    }
}

int main(void) {
    g_ablation.hp = HP_UNIFORM;
    g_ablation.escape_model = 1;
    g_ablation.his_defence = 99;
    g_ablation.panic = 0.8;
    g_ablation.panic_hp = 25;
    static OppFit fit;
    opp_load("ocean/osrs/tests/fixtures/model.fit", "ocean/osrs/tests/fixtures/eat_mixture.fit", &fit, &g_eat_model);
    g_la_horizon = 15;
    g_la_threads = 4;
    assert_fitted(&fit, "test");
    static OppModel model;
    model = (OppModel){MODEL_FIT, &fit};
    g_la_model = &model;
    g_la_cache = 0;
    observer_register_items();
    sim_init(&g_base);
    for (int i = 0; i < WARM_REQUEST_COUNT; i++) test_prefix_equals_unbudgeted(i);
    test_minimum_for_survivors();
    test_stalled_helpers();
    puts("Lookahead anytime budget contracts passed");
    return 0;
}
