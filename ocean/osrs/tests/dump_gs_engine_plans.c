/* Writes the macro -> A-record fixture the Java side runs through EnginePlan and the action run (plugins/gear-swapper EnginePlanSelfTest).
 *
 *   cc -std=c11 -O2 -I. -o dump_gs_engine_plans ocean/osrs/tests/dump_gs_engine_plans.c -lm      (cwd = repo root)
 *   ./dump_gs_engine_plans plugins/gear-swapper/src/test/resources/engine-plans.txt             (path of the Java worktree's fixture)
 *
 * One state per section: the request tail (pending hits, committed remainder) on top of the first warm-up request, then for each macro the macro's
 * script as the server would answer it (server_respond, the exact response line with request id 0) and, for every unsent suffix of that plan, the R
 * section the plugin must write after the steps before it were sent (due rebased past REBASE, boundaries only between two unsent groups, nothing
 * of a gate). The expected R text is also read back through read_request here: what Java writes, C accepts.
 *
 * `literal` carries only `continue`: the plan the request's R section holds is what continue echoes.
 *
 * File grammar (one record per line, `#` lines ignored):
 *   STATE name tick T hp H energy E attack_timer N speed S rebase R
 *   WORN 13 raw ids        BAG 28 raw ids
 *   RESPONSE macro-index | macro name | the response line
 *   INFEASIBLE macro-index | macro name
 *   REMAINDER macro-index n | R KNOWN n;C due kind arg timing [count cand...];...      (';' stands for a newline)
 */
#define main lookahead_server_main
#include "gs_lookahead_server.c"
#undef main

#define CHECK(c) do { if (!(c)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #c); exit(1); } } while (0)

typedef struct { int due, kind, arg, timing, count, cand[GS_WIRE_MAX_CANDIDATES]; } Rec;

static void parse_text(const char* text, Request* q) {
    FILE* in = fmemopen((void*)text, strlen(text), "r");
    CHECK(in != NULL);
    read_request(in, q);
    fclose(in);
}

/* The warm-up request 0 with its tick replaced, up to its pending queue, then `tail` (P, R, END). */
static void compose(char* out, size_t capacity, int tick, const char* tail) {
    const char* base = WARM_REQUESTS[0];
    const char* cut = strstr(base, "P KNOWN");
    const char* body = strchr(base, '\n');
    CHECK(cut != NULL && body != NULL);
    int n = snprintf(out, capacity, "LA 434 %d", tick);
    CHECK(n > 0 && (size_t)n + (size_t)(cut - body) + strlen(tail) + 1 < capacity);
    memcpy(out + n, body, (size_t)(cut - body));
    strcpy(out + n + (cut - body), tail);
}

/* The A section of a response line into records, returns the count. */
static int parse_plan(const char* response, Rec* recs) {
    const char* a = strstr(response, " A ");
    CHECK(a != NULL);
    int count, used;
    CHECK(sscanf(a, " A %d%n", &count, &used) == 1 && count >= 0 && count <= GS_WIRE_MAX_RECORDS);
    const char* p = a + used;
    for (int i = 0; i < count; i++) {
        int consumed;
        Rec* r = &recs[i];
        CHECK(sscanf(p, " %d %d %d %d%n", &r->due, &r->kind, &r->arg, &r->timing, &consumed) == 4);
        p += consumed;
        r->count = 0;
        if (r->kind >= GS_WIRE_EAT_RECIPE && r->kind <= GS_WIRE_UNEQUIP_RECIPE) {
            CHECK(sscanf(p, " %d%n", &r->count, &consumed) == 1 && r->count >= 0 && r->count <= GS_WIRE_MAX_CANDIDATES);
            p += consumed;
            for (int c = 0; c < r->count; c++) {
                CHECK(sscanf(p, " %d%n", &r->cand[c], &consumed) == 1);
                p += consumed;
            }
        }
    }
    return count;
}

static int write_record(char* out, size_t room, const Rec* r, int rebase) {
    int due = r->due > rebase + 1 ? r->due : rebase + 1;
    int arg = r->kind == GS_WIRE_ALIGNED_READY && r->arg < rebase + 1 ? rebase + 1 : r->arg;
    int at = snprintf(out, room, "C %d %d %d %d", due, r->kind, arg, r->timing);
    if (r->kind >= GS_WIRE_EAT_RECIPE && r->kind <= GS_WIRE_UNEQUIP_RECIPE) {
        at += snprintf(out + at, room - (size_t)at, " %d", r->count);
        for (int c = 0; c < r->count; c++) at += snprintf(out + at, room - (size_t)at, " %d", r->cand[c]);
    }
    CHECK((size_t)at < room);
    return at;
}

/* The records of the plan the plugin has not sent when the steps before `start` have: a boundary survives only between two unsent records
 * (the last of several in a row), a leading one and a trailing one are dropped. */
static int suffix_of(const Rec* plan, int count, int start, Rec* out) {
    int n = 0, boundary = -1;
    for (int i = start; i < count; i++) {
        if (plan[i].kind == GS_WIRE_GROUP) { boundary = i; continue; }
        if (boundary >= 0 && n > 0) out[n++] = plan[boundary];
        boundary = -1;
        out[n++] = plan[i];
    }
    return n;
}

static void dump_state(FILE* f, const char* name, int tick, int only_continue, const char* tail) {
    static char text[1 << 14];
    static Request q;
    compose(text, sizeof(text), tick, tail);
    parse_text(text, &q);
    CHECK(q.tick == tick && server_unavailable(&q) == NULL);
    RiskfightSnapshot snap;
    snapshot_of(&q, &snap);
    riskfight_load_state(&g_base.state, &g_base.context, &snap);
    static LaScript scripts[LA_MACROS];
    int feasible[LA_MACROS];
    la_scripts(&g_base.state, q.tick, &q.feasible, &q.rules, scripts, feasible);
    int rebase = tick + 3;
    fprintf(f, "STATE %s tick %d hp %d energy %d attack_timer %d speed %d rebase %d\n", name, tick, q.ours.hp, q.feasible.energy, q.attack_timer, q.ours_speed, rebase);
    /* the raw client ids as the plugin observed them, not the sim's canonical ones */
    for (const char* tag = "W"; tag; tag = tag[0] == 'W' ? "G" : NULL) {
        char key[4];
        snprintf(key, sizeof(key), "\n%s ", tag);
        const char* at = strstr(text, key);
        CHECK(at != NULL);
        fprintf(f, "%s", tag[0] == 'W' ? "WORN" : "BAG");
        for (at += 2; *at != '\n'; at++) fputc(*at, f);
        fputc('\n', f);
    }
    q.id = 0;
    for (int m = 0; m < (only_continue ? 1 : LA_MACROS); m++) {
        if (!feasible[m]) { fprintf(f, "INFEASIBLE %d | %s\n", m, LA_NAME[m]); continue; }
        memset(&g_decision, 0, sizeof(g_decision));
        memcpy(g_decision.scripts, scripts, sizeof(scripts));
        memcpy(g_decision.feasible, feasible, sizeof(feasible));
        for (int k = 0; k < LA_MACROS; k++) if (feasible[k]) g_decision.score[k] = (LaScore){.kill = 0.5, .death = 0.25, .value = 0.125, .worlds = 1};
        g_decision.best = (LaMacro)m;
        static char line[1 << 15];
        FILE* out = fmemopen(line, sizeof(line), "w");
        CHECK(out != NULL);
        server_respond(out, &q, 12.0);
        fclose(out);
        fprintf(f, "RESPONSE %d | %s | %s\n", m, LA_NAME[m], line);
        static Rec plan[GS_WIRE_MAX_RECORDS], rest[GS_WIRE_MAX_RECORDS];
        int count = parse_plan(line, plan);
        CHECK(count == scripts[m].n);
        for (int start = 0; start < count; start++) {
            if (plan[start].kind == GS_WIRE_GROUP) continue;
            int n = suffix_of(plan, count, start, rest);
            static char expected[1 << 14], request[1 << 15];
            int at = snprintf(expected, sizeof(expected), "R KNOWN %d\n", n);
            for (int i = 0; i < n; i++) {
                at += write_record(expected + at, sizeof(expected) - (size_t)at, &rest[i], rebase);
                expected[at++] = '\n';
            }
            expected[at] = 0;
            /* what the plugin writes must be a request the server accepts, with the plan the records say */
            static char tail2[1 << 14];
            snprintf(tail2, sizeof(tail2), "P KNOWN 2\nD 1 4 0 1 0 1 0 20 0 30 8000 0 0\nD 1 7 0 1 0 1 0 20 0 30 8000 0 0\n%sEND\n", expected);
            compose(request, sizeof(request), rebase, tail2);
            static Request again;
            parse_text(request, &again);
            CHECK(again.rules.n == n && again.rules_expected == n);
            for (int i = 0; i < n; i++) {
                char back[256];
                FILE* mem = fmemopen(back, sizeof(back), "w");
                CHECK(mem != NULL);
                fputc('C', mem);
                la_script_emit_record(mem, &again.rules, i);
                fclose(mem);
                char want[256];
                write_record(want, sizeof(want), &rest[i], rebase);
                CHECK(strcmp(back + 1, want + 1) == 0); /* the record, re-emitted from the parsed script */
            }
            fprintf(f, "REMAINDER %d %d | ", m, n);
            for (const char* c = expected; *c; c++) fputc(*c == '\n' ? ';' : *c, f);
            fputc('\n', f);
        }
    }
}

#define PENDING "P KNOWN 2\nD 1 4 0 1 0 1 0 20 0 30 8000 0 0\nD 1 7 0 1 0 1 0 20 0 30 8000 0 0\n"

int main(int argc, char** argv) {
    CHECK(argc == 2);
    g_ablation.hp = HP_UNIFORM;
    observer_register_items();
    sim_init(&g_base);
    FILE* f = fopen(argv[1], "w");
    CHECK(f != NULL);
    fprintf(f, "# Generated by ocean/osrs/tests/dump_gs_engine_plans.c, do not edit. Grammar: header of that file.\n");
    /* The first warm-up request with a committed spec-bar enable, aligned attack, attack and hit wait (a burst's tail) as the remainder `continue` echoes. */
    dump_state(f, "canonical", 804, 0, PENDING "R KNOWN 4\nC 805 2 0 0\nC 805 18 807 1\nC 805 1 0 0\nC 805 7 -1 1\n" "END\n");
    /* A search plan: literal ticked clicks, timing 1, every due past the next tick (a tick gate before each in the plugin). */
    dump_state(f, "literal", 804, 1, PENDING "R KNOWN 11\n"
        "C 806 0 7 1\nC 806 1 0 1\nC 807 13 0 1\nC 807 3 0 1\nC 808 5 3 1\nC 808 17 0 1\nC 809 12 6685 1\nC 809 11 27690 1\nC 810 2 0 1\nC 810 4 0 1\nC 811 0 15 1\n" "END\n");
    fclose(f);
    return 0;
}
