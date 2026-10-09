/* Differential test of the C AutoSpec.decide port against the Java oracle.
 *
 *   cc -std=c11 -O2 -I. -o /tmp/test_gs_auto_spec ocean/osrs/tests/test_gs_auto_spec.c -lm
 *   /tmp/test_gs_auto_spec [ORACLE.tap]
 *
 * Without an argument it replays the fixture of tests/fixtures, run from the repo root. The oracle is written by AutoSpec.tap during
 * `./gradlew :gear-swapper:fightReplay -Precording=... -Pspectap=FILE` in the Light repo, each decision also at energies 0, 50 and 100. */
#include <stdio.h>
#include <time.h>
#include "../gearswapper/gs_auto_spec.h"

enum { MAX_PLANS = 16, MAX_HITS = 16, POOL = 1 << 20 };
typedef struct { GsSpecHit hits[MAX_HITS]; int count; GsChoiceKind choice; } Plan;
typedef struct {
    GsSpecSituation s;
    GsSpecHit underway[MAX_HITS];
    Plan plans[MAX_PLANS]; int plan_count;
    Plan after[MAX_PLANS]; int after_count;
    int energy;
    GsChoiceKind choice, aim; GsCast cast; double chance, bar, death, held, kept;
} Record;

static double pool[POOL];
static int pooled;

#ifdef GS_HP_BELIEF
static void read_watched(const char* p, GsWarning* w) {
    while (*p == ' ') p++;
    for (; *p && *p != '\n' && *p != '-'; p++) {
        assert(w->watched_count < GS_WATCHED_LIMIT);
        w->watched[w->watched_count++] = *p == 'Q' ? GS_SEEN_QUIET : *p == 'A' ? GS_SEEN_ATE : GS_SEEN_HIDDEN;
    }
}
#endif

static GsPid pid_of(const char* s) { return !strcmp(s, "LOCAL") ? GS_PID_LOCAL : !strcmp(s, "OPPONENT") ? GS_PID_OPPONENT : GS_PID_UNKNOWN; }
static GsVengeance vengeance_of(const char* s) {
    return !strcmp(s, "UP") ? GS_VENGEANCE_UP : !strcmp(s, "DOWN") ? GS_VENGEANCE_DOWN : !strcmp(s, "MAYBE") ? GS_VENGEANCE_MAYBE : GS_VENGEANCE_UNKNOWN;
}
static GsChoiceKind choice_of(const char* s) {
    return !strcmp(s, "VOIDWAKER") ? GS_CHOICE_VOIDWAKER : !strcmp(s, "MAUL_VOIDWAKER") ? GS_CHOICE_MAUL_VOIDWAKER : !strcmp(s, "MAUL") ? GS_CHOICE_MAUL : GS_CHOICE_HOLD;
}

static int read_hits(char* p, GsSpecHit* hits) {
    int count = 0;
    while (*p == ' ' && p[1] == 'H') {
        p += 2;
        GsSpecHit* hit = &hits[count++];
        hit->lands = (int)strtol(p, &p, 10);
        hit->damages = (int)strtol(p, &p, 10);
        while (*p == ' ') p++;
        hit->origin = *p == 'O' ? GS_ORIGIN_OURS : GS_ORIGIN_REFLECTED;
        while (*p != ' ') p++;
        assert(pooled + hit->damages <= POOL);
        double* damage = &pool[pooled];
        pooled += hit->damages;
        for (int i = 0; i < hit->damages; i++) damage[i] = strtod(p, &p);
        hit->damage = damage;
    }
    return count;
}

static int read_record(FILE* f, Record* r) {
    memset(r, 0, sizeof(*r));
    pooled = 0;
    static char line[1 << 16];
    while (fgets(line, sizeof(line), f)) {
        char tag[8];
        int consumed = 0;
        if (sscanf(line, "%7s%n", tag, &consumed) < 1) continue;
        char* p = line + consumed;
        GsSpecSituation* s = &r->s;
        if (!strcmp(tag, "SS")) {
            char vengeance[16], pid[16], kind[16];
            sscanf(p, "%d %d %d %d %15s %15s %lf %15s %d %d %d %d", &s->hitpoints.low, &s->hitpoints.high, &s->hitpoints.max, &s->hard_food_ready, vengeance, pid, &s->pid_flip, kind,
                &s->rebound.cast, &s->recoil_charges, &s->tick, &r->energy);
            s->vengeance = vengeance_of(vengeance); s->pid = pid_of(pid);
            s->rebound.kind = !strcmp(kind, "UP") ? GS_REBOUND_UP : !strcmp(kind, "CASTABLE") ? GS_REBOUND_CASTABLE : GS_REBOUND_SPENT;
        } else if (!strcmp(tag, "LOCAL")) {
            s->local.hitpoints = (int)strtol(p, &p, 10);
            while (*p == ' ') {
                GsSpecHeal* heal = &s->local.heals[s->local.heal_count++];
                heal->tick = (int)strtol(p, &p, 10); p++;
                heal->amount = (int)strtol(p, &p, 10); p++;
                heal->cap = (int)strtol(p, &p, 10);
            }
        } else if (!strcmp(tag, "UW")) { s->underway = r->underway; s->underway_count = read_hits(p, r->underway); }
        else if (!strcmp(tag, "IN")) {
            char attack[16];
            s->has_incoming = 1;
            sscanf(p, "%d %d %d %lf %15s", &s->incoming.lands, &s->incoming.least, &s->incoming.most, &s->incoming.accuracy, attack);
            s->incoming.attack = strcmp(attack, "none") ? atoi(attack) : GS_NONE;
        } else if (!strcmp(tag, "W")) {
            s->warning.since = GS_NONE;
            if (strncmp(p + 1, "none", 4)) s->warning.since = (int)strtol(p, &p, 10); else p += 5;
#ifdef GS_HP_BELIEF
            read_watched(p, &s->warning);
            while (0) {
#else
            while (*p && *p != '\n') {
#endif
                char* end;
                long v = strtol(p, &end, 10);
                if (end == p) break;
                s->warning.unseen[s->warning.unseen_count++] = (int)v;
                p = end;
            }
        } else if (!strcmp(tag, "PL") || !strcmp(tag, "AC")) {
            Plan* plan = !strcmp(tag, "PL") ? &r->plans[r->plan_count++] : &r->after[r->after_count++];
            char choice[24];
            int n;
            sscanf(p, "%23s%n", choice, &n);
            plan->choice = choice_of(choice);
            plan->count = read_hits(p + n, plan->hits);
        } else if (!strcmp(tag, "D")) {
            char choice[24], cast[8], aim[24], bar[32];
            sscanf(p, "%23s %7s %23s %lf %31s %lf %lf %lf", choice, cast, aim, &r->chance, bar, &r->death, &r->held, &r->kept);
            r->choice = choice_of(choice); r->aim = choice_of(aim); r->cast = !strcmp(cast, "NOW") ? GS_CAST_NOW : GS_CAST_KEEP;
            r->bar = strtod(bar, NULL);
        } else if (!strcmp(tag, "END")) return 1;
    }
    return 0;
}

static void incoming_echo_contract(void) {
    Arena arena = arena_make((size_t)1 << 20);
    GsIncoming incoming = {101, 0, 100, 1, GS_NONE};
    GsHealth healed = gs_health_healing(gs_health_of(1), 101, 19, 101);
    GsIncoming local_range = gs_surviving_incoming(&arena, &healed, &incoming, GS_PID_LOCAL);
    assert(local_range.least == 0 && local_range.most == 19 && fabs(local_range.accuracy - 1) < 1e-12);
    arena.used = 0;
    GsIncoming opponent_range = gs_surviving_incoming(&arena, &healed, &incoming, GS_PID_OPPONENT);
    assert(opponent_range.least == 0 && opponent_range.most == 0 && fabs(opponent_range.accuracy - 1) < 1e-12);
    arena.used = 0;
    GsHealth hale = gs_health_of(101);
    GsIncoming surviving_range = gs_surviving_incoming(&arena, &hale, &incoming, GS_PID_LOCAL);
    assert(surviving_range.least == 0 && surviving_range.most == 100 && fabs(surviving_range.accuracy - 1) < 1e-12);

    const double zero_damage[] = {1};
    GsSpecHit planned = {103, zero_damage, 1, GS_ORIGIN_OURS};
    GsSpecSituation s = {0};
    s.hitpoints = (GsHealthRange){1, 1, 1};
    s.hard_food_ready = 200;
    s.vengeance = GS_VENGEANCE_DOWN;
    s.pid = GS_PID_LOCAL;
    s.incoming = incoming;
    s.has_incoming = 1;
    s.tick = 100;
    s.warning.since = GS_NONE;
    s.local = gs_health_of(1);
    GsOutcome fatal = gs_spec_outcome(&arena, &s, &planned, 1, GS_ECHO_VENGEANCE);
    assert(fatal.kill == 0);

#ifndef GS_HP_BELIEF
    arena.used = 0;
    s.hitpoints = (GsHealthRange){14, 14, 14};
    s.local = gs_health_healing(gs_health_of(1), 101, 19, 101);
    GsOutcome local = gs_spec_outcome(&arena, &s, &planned, 1, GS_ECHO_VENGEANCE);
    assert(fabs(local.kill - 1.0 / 101) < 1e-12);

    arena.used = 0;
    s.pid = GS_PID_OPPONENT;
    GsOutcome opponent = gs_spec_outcome(&arena, &s, &planned, 1, GS_ECHO_VENGEANCE);
    assert(opponent.kill == 0);

    arena.used = 0;
    s.pid = GS_PID_LOCAL;
    s.local = gs_health_of(101);
    GsOutcome all_survive = gs_spec_outcome(&arena, &s, &planned, 1, GS_ECHO_VENGEANCE);
    assert(fabs(all_survive.kill - 82.0 / 101) < 1e-12);
#endif
    free(arena.base);
}

int main(int argc, char** argv) {
    incoming_echo_contract();
    if (argc == 2 && !strcmp(argv[1], "--contract-only")) {
        puts("AutoSpec incoming echo contract verified");
        return 0;
    }
    FILE* f = fopen(argc > 1 ? argv[1] : "ocean/osrs/tests/fixtures/auto_spec_oracle.tap", "r");
    assert(f);
    Arena arena = arena_make((size_t)1 << 28);
    static Record record;
    long total = 0, matched = 0, choices[4] = {0}, casts = 0;
    int shown = 0;
    double seconds = 0;
    while (read_record(f, &record)) {
        GsSpecPlan plans[MAX_PLANS], after[MAX_PLANS];
        for (int i = 0; i < record.plan_count; i++) plans[i] = (GsSpecPlan){record.plans[i].choice, record.plans[i].hits, record.plans[i].count};
        for (int i = 0; i < record.after_count; i++) after[i] = (GsSpecPlan){record.after[i].choice, record.after[i].hits, record.after[i].count};
        struct timespec t0, t1;
        clock_gettime(CLOCK_MONOTONIC, &t0);
        arena.used = 0;
        GsSpecDecision d = gs_auto_spec_decide(&arena, &record.s, plans, record.plan_count, after, record.after_count, record.energy);
        clock_gettime(CLOCK_MONOTONIC, &t1);
        seconds += (t1.tv_sec - t0.tv_sec) + (t1.tv_nsec - t0.tv_nsec) * 1e-9;
        total++;
        int ok = d.choice == record.choice && d.cast == record.cast && d.aim == record.aim && d.chance == record.chance && d.bar == record.bar && d.death == record.death && d.held == record.held && d.kept == record.kept;
        matched += ok;
        choices[record.choice]++;
        casts += record.cast == GS_CAST_NOW;
        if (!ok && shown++ < 8)
            printf("MISMATCH call %ld tick %d energy %d: java %d/%d aim %d chance %.17g bar %.17g | c %d/%d aim %d chance %.17g bar %.17g\n", total, record.s.tick, record.energy,
                record.choice, record.cast, record.aim, record.chance, record.bar, d.choice, d.cast, d.aim, d.chance, d.bar);
    }
    printf("%ld calls, %ld bit-exact (%.3f%%); outcomes hold %ld voidwaker %ld maul+voidwaker %ld maul %ld, casts now %ld; C time %.3f s\n", total, matched,
        100.0 * matched / total, choices[0], choices[1], choices[2], choices[3], casts, seconds);
    return matched == total ? 0 : 1;
}
