/* Differential test of pure C AutoSpec decisions against fresh Java oracle inputs.
 * PL/AC require choice, absolute attack tick (zero means immediate), then hits;
 * D requires the selected attack tick after the eight decision fields.
 * This does not certify native timed producer, strip admission, or actuator parity.
 *
 *   cc -std=c11 -O2 -I. -o /tmp/test_gs_auto_spec ocean/osrs/tests/test_gs_auto_spec.c -lm
 *   /tmp/test_gs_auto_spec FRESH_ORACLE.tap
 *
 * The oracle is emitted by :gear-swapper:fightReplay -Precording=... -Pspectap=FILE.
 * Existing external fixtures must be regenerated, never assigned missing timing. */
#include <stdio.h>
#include <ctype.h>
#include <time.h>
#include "../gearswapper/gs_auto_spec.h"
#include "gs_target_oracle.h"

enum { MAX_PLANS = 256, MAX_HITS = 16, POOL = 1 << 20 };
typedef struct { GsSpecHit hits[MAX_HITS]; int count; GsChoiceKind choice; int attack_tick; } Plan;
typedef struct {
    GsSpecSituation s;
    GsSpecHit underway[MAX_HITS];
    Plan plans[MAX_PLANS]; int plan_count;
    Plan after[MAX_PLANS]; int after_count;
    int energy;
    int attack_tick;
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
    if (!strcmp(s, "HOLD")) return GS_CHOICE_HOLD;
    if (!strcmp(s, "VOIDWAKER")) return GS_CHOICE_VOIDWAKER;
    if (!strcmp(s, "MAUL_VOIDWAKER")) return GS_CHOICE_MAUL_VOIDWAKER;
    if (!strcmp(s, "MAUL")) return GS_CHOICE_MAUL;
    if (!strcmp(s, "VOIDWAKER_BURST")) return GS_CHOICE_VOIDWAKER_BURST;
    fprintf(stderr, "Unknown spec choice: %s\n", s);
    exit(EXIT_FAILURE);
}

static int read_hits(char* p, GsSpecHit* hits) {
    int count = 0;
    while (*p == ' ' && p[1] == 'H') {
        p += 2;
        if (count == MAX_HITS) { fputs("Too many oracle hits\n", stderr); exit(EXIT_FAILURE); }
        GsSpecHit* hit = &hits[count++];
        const char* cursor = p;
        hit->lands = gs_target_int(&cursor);
        hit->damages = gs_target_int(&cursor);
        if (hit->damages < 1 || hit->damages > POOL - pooled) {
            fputs("Invalid spec hit damage length\n", stderr);
            exit(EXIT_FAILURE);
        }
        char origin[16];
        int consumed;
        if (sscanf(cursor, " %15s%n", origin, &consumed) != 1 || (strcmp(origin, "OURS") && strcmp(origin, "REFLECTED"))) {
            fputs("Invalid spec hit origin\n", stderr);
            exit(EXIT_FAILURE);
        }
        hit->origin = !strcmp(origin, "OURS") ? GS_ORIGIN_OURS : GS_ORIGIN_REFLECTED;
        cursor += consumed;
        hit->first_positive_roll = gs_target_int(&cursor);
        p = (char*)cursor;
        char* end;
        hit->positive_each = strtod(p, &end);
        if (end == p || hit->first_positive_roll < 1 || hit->first_positive_roll > imax(1, hit->damages - 1)
                || !isfinite(hit->positive_each) || hit->positive_each < 0 || hit->positive_each > 1) {
            fputs("Invalid required spec hit roll metadata\n", stderr);
            exit(EXIT_FAILURE);
        }
        p = end;
        double* damage = &pool[pooled];
        pooled += hit->damages;
        for (int i = 0; i < hit->damages; i++) {
            damage[i] = strtod(p, &end);
            if (end == p || !isfinite(damage[i]) || damage[i] < 0
                    || (hit->origin == GS_ORIGIN_OURS && i > 0
                        && damage[i] != (i >= hit->first_positive_roll ? hit->positive_each : 0))) {
                fputs("Invalid spec hit damage distribution\n", stderr);
                exit(EXIT_FAILURE);
            }
            p = end;
        }
        if (fabs(jdk_sum(damage, hit->damages) - 1) > 1e-12) {
            fputs("Spec hit distribution must be normalized\n", stderr);
            exit(EXIT_FAILURE);
        }
        hit->damage = damage;
    }
    while (isspace((unsigned char)*p)) p++;
    if (*p) { fputs("Extra or malformed spec hit fields\n", stderr); exit(EXIT_FAILURE); }
    return count;
}

static int read_record(FILE* f, Record* r) {
    gs_target_clear(&r->s.target);
    memset(r, 0, sizeof(*r));
    pooled = 0;
    static char line[1 << 16];
    int started = 0, decision_seen = 0;
    while (fgets(line, sizeof(line), f)) {
        char tag[16];
        int consumed = 0;
        if (sscanf(line, "%15s%n", tag, &consumed) < 1) continue;
        char* p = line + consumed;
        GsSpecSituation* s = &r->s;
        if (!strcmp(tag, "SS")) {
            if (started) { fputs("Spec record missing END\n", stderr); exit(EXIT_FAILURE); }
            started = 1;
            char vengeance[16], pid[16], kind[16];
            int fields = sscanf(p, "%d %d %d %d %15s %15s %lf %15s %d %d %d %d", &s->hitpoints.low, &s->hitpoints.high, &s->hitpoints.max, &s->hard_food_ready, vengeance, pid, &s->pid_flip, kind,
                &s->rebound.cast, &s->recoil_charges, &s->tick, &r->energy);
            if (fields != 12) { fprintf(stderr, "SS requires 12 fields, parsed %d\n", fields); exit(EXIT_FAILURE); }
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
            if ((!strcmp(tag, "PL") ? r->plan_count : r->after_count) == MAX_PLANS) {
                fputs("Too many oracle plans\n", stderr); exit(EXIT_FAILURE);
            }
            Plan* plan = !strcmp(tag, "PL") ? &r->plans[r->plan_count++] : &r->after[r->after_count++];
            char choice[24];
            int n;
            if (sscanf(p, "%23s%n", choice, &n) != 1) { fputs("Missing plan choice\n", stderr); exit(EXIT_FAILURE); }
            plan->choice = choice_of(choice);
            const char* cursor = p + n;
            plan->attack_tick = gs_target_int(&cursor);
            if (plan->attack_tick < 0) { fputs("Invalid absolute plan attack tick\n", stderr); exit(EXIT_FAILURE); }
            plan->count = read_hits((char*)cursor, plan->hits);
            if (plan->choice == GS_CHOICE_VOIDWAKER_BURST &&
                    (plan->count != 2 || plan->hits[1].lands <= plan->hits[0].lands)) {
                fputs("Voidwaker burst requires two separated hits\n", stderr); exit(EXIT_FAILURE);
            }
        } else if (!strcmp(tag, "D")) {
            if (!started || decision_seen) { fputs("Unexpected or duplicate spec decision\n", stderr); exit(EXIT_FAILURE); }
            decision_seen = 1;
            char choice[24], cast[8], aim[24], bar[32];
            int n;
            if (sscanf(p, "%23s %7s %23s %lf %31s %lf %lf %lf%n", choice, cast, aim, &r->chance, bar, &r->death, &r->held, &r->kept, &n) != 8) {
                fputs("Invalid spec decision fields\n", stderr); exit(EXIT_FAILURE);
            }
            const char* cursor = p + n;
            r->attack_tick = gs_target_int(&cursor);
            while (isspace((unsigned char)*cursor)) cursor++;
            if (*cursor || r->attack_tick < 0 || (strcmp(cast, "NOW") && strcmp(cast, "KEEP"))) {
                fputs("Invalid required decision timing or cast\n", stderr); exit(EXIT_FAILURE);
            }
            r->choice = choice_of(choice); r->aim = choice_of(aim); r->cast = !strcmp(cast, "NOW") ? GS_CAST_NOW : GS_CAST_KEEP;
            r->bar = strtod(bar, NULL);
        } else if (!strcmp(tag, "END")) {
            if (!started || !decision_seen) { fputs("Spec record missing required decision\n", stderr); exit(EXIT_FAILURE); }
            gs_target_validate(&s->target);
            return 1;
        } else if (!strcmp(tag, "RECORDING")) {
            if (started) { fputs("Spec recording provenance inside a record\n", stderr); exit(EXIT_FAILURE); }
        } else if (!gs_target_read_line(&s->target, tag, p)) {
            fprintf(stderr, "Unknown spec oracle tag: %s\n", tag);
            exit(EXIT_FAILURE);
        }
    }
    if (started) { fputs("Incomplete spec record\n", stderr); exit(EXIT_FAILURE); }
    return 0;
}

#ifdef GS_HP_BELIEF
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
    GsSpecHit planned = {103, zero_damage, 1, GS_ORIGIN_OURS, 1, 0};
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

    free(arena.base);
}
#else
static void incoming_echo_contract(void) {
    Arena arena = arena_make((size_t)1 << 27);
    GsSpecSituation s = {0};
    s.tick = 100; s.warning.since = GS_NONE; s.hard_food_ready = 200;
    s.pid = GS_PID_LOCAL; s.vengeance = GS_VENGEANCE_UP;
    s.hitpoints = (GsHealthRange){20, 20, 20};
    s.local = gs_health_of(50);
    s.has_incoming = 1; s.incoming = (GsIncoming){101, 40, 40, 1, GS_NONE};
    GsSpecHit hit = gs_spec_hit_of(&arena, 102, 20, 20, 1);
    GsOutcome outcome = gs_spec_outcome(&arena, &s, &hit, 1, GS_ECHO_RECOIL, 103);
    assert(outcome.kill == 0 && fabs(outcome.death - 1) < 1e-12);
    arena.used = 0;
    s.has_incoming = 0; s.hitpoints = (GsHealthRange){100, 100, 100};
    GsSpecHit hits[] = {gs_spec_hit_of(&arena, 101, 4, 4, 1), gs_spec_hit_of(&arena, 102, 100, 100, 1)};
    outcome = gs_spec_outcome(&arena, &s, hits, 2, GS_ECHO_RECOIL, 103);
    assert(fabs(outcome.kill - 1) < 1e-12 && outcome.death == 0);
    arena.used = 0;
    s.vengeance = GS_VENGEANCE_DOWN; s.hitpoints = (GsHealthRange){1, 1, 1};
    s.has_incoming = 1; s.incoming = (GsIncoming){102, 100, 100, 1, GS_NONE};
    hit = gs_spec_hit_of(&arena, 101, 100, 100, 1);
    outcome = gs_spec_outcome(&arena, &s, &hit, 1, GS_ECHO_RECOIL, 103);
    assert(outcome.kill == 0 && outcome.death == 0);
    s.incoming.attack = 102;
    outcome = gs_spec_outcome(&arena, &s, &hit, 1, GS_ECHO_RECOIL, 103);
    assert(fabs(outcome.kill - 1) < 1e-12);
    s.has_incoming = 0; s.vengeance = GS_VENGEANCE_UP; s.local = gs_health_of(5);
    outcome = gs_spec_outcome(&arena, &s, &hit, 1, GS_ECHO_RECOIL, 103);
    assert(fabs(outcome.kill - 1) < 1e-12 && outcome.death == 0);
    s.local = gs_health_of(1);
    outcome = gs_spec_outcome(&arena, &s, &hit, 1, GS_ECHO_RECOIL, 103);
    assert(outcome.kill == 0 && fabs(outcome.death - 1) < 1e-12);
    s.local = gs_health_healing(gs_health_of(0), 100, 19, 121);
    outcome = gs_spec_outcome(&arena, &s, &hit, 1, GS_ECHO_RECOIL, 103);
    assert(outcome.kill == 0 && outcome.death == 0);
    s.vengeance = GS_VENGEANCE_DOWN; s.local = gs_health_healing(gs_health_of(1), 100, 19, 121);
    s.has_incoming = 1; s.incoming = (GsIncoming){101, 10, 10, 1, GS_NONE};
    outcome = gs_spec_outcome(&arena, &s, &hit, 1, GS_ECHO_RECOIL, 103);
    assert(fabs(outcome.kill - 1) < 1e-12);
    s.local = gs_health_healing(gs_health_of(1), 101, 19, 121);
    outcome = gs_spec_outcome(&arena, &s, &hit, 1, GS_ECHO_RECOIL, 103);
    assert(fabs(outcome.kill - 1) < 1e-12);
    s.pid = GS_PID_OPPONENT;
    outcome = gs_spec_outcome(&arena, &s, &hit, 1, GS_ECHO_RECOIL, 103);
    assert(outcome.kill == 0 && outcome.death == 0);
    s.has_incoming = 0; s.underway = &hit; s.underway_count = 1;
    s.pid_flip = 1;
    assert(gs_spec_horizon(&s, NULL, 0) == 102);
    const int targets[] = {10, 60, 95}, locals[] = {5, 50, 100}, landings[] = {106, 110};
    const double accuracies[] = {0.4, 1};
    s.hard_food_ready = 101; /* The independent enumeration offers all three foods at its meal. */
    int cases = 0;
    double error = 0;
    for (int t = 0; t < 3; t++) for (int l = 0; l < 3; l++)
    for (int pid = 0; pid < 2; pid++) for (int vengeance = 0; vengeance < 2; vengeance++)
    for (int committed = 0; committed < 2; committed++) for (int at = 0; at < 2; at++)
    for (int a = 0; a < 2; a++) {
        arena.used = 0;
        s.hitpoints = (GsHealthRange){targets[t], targets[t], 121};
        s.local = gs_health_of(locals[l]); s.pid = (GsPid)pid;
        s.vengeance = vengeance ? GS_VENGEANCE_UP : GS_VENGEANCE_DOWN;
        s.has_incoming = 1; s.incoming = (GsIncoming){104, 0, 7, accuracies[a], committed ? GS_NONE : 103};
        s.underway_count = 0; s.pid_flip = 0;
        hit = gs_spec_hit_of(&arena, landings[at], 60, 70, 1);
        GsEating meal = {101, 0, 3};
        outcome = gs_strikes(&arena, &s, &hit, 1, &meal, 1, GS_ECHO_RECOIL, 100, 111);
        double expected = 0;
        for (int wanted = 45; wanted <= 87; wanted++) {
            int hp = targets[t], eaten[GS_CONSUMABLES] = {0};
            for (int food = 0; food < 3 && hp < wanted; food++) {
                eaten[GS_MEAL_KIND[food]] = 1;
                hp = imax(hp, imin(GS_MEAL_CAPS[food], hp + GS_MEAL_HEALS[food]));
            }
            int delay = committed ? 0 : consumption_delay(consumption_of(eaten[GS_POTION], eaten[GS_FAST_FOOD], eaten[GS_HARD_FOOD]));
            for (int source = -1; source <= 7; source++) {
                int damage = imax(0, source);
                double mass = (source < 0 ? 1 - accuracies[a] : accuracies[a] / 8) / 43 / 11;
                for (int ours = 60; ours <= 70; ours++) {
                    if (ours < hp) continue;
                    int incoming_damage = !committed && 103 + delay > landings[at] ? 0 : damage;
                    int returned = vengeance ? imax(1, imin(ours, hp) * 3 / 4) : 0;
                    if (locals[l] > incoming_damage + returned) expected += mass;
                }
            }
        }
        error = fmax(error, fabs(outcome.kill - expected));
        assert(fabs(outcome.kill - expected) < 1e-11);
        cases++;
    }
    printf("%d independent joint food/incoming/own-roll cases, maximum error %.3g\n", cases, error);
    free(arena.base);
}
#endif

#ifndef GS_HP_BELIEF
static void target_contract(void) {
    Arena arena = arena_make((size_t)1 << 27);
    GsSpecSituation s = {0};
    s.tick = 100; s.warning.since = GS_NONE; s.hard_food_ready = 100;
    s.hitpoints = (GsHealthRange){20, 80, 99};
    s.local = gs_health_of(99); s.pid = GS_PID_LOCAL; s.vengeance = GS_VENGEANCE_DOWN;
    double low[81] = {0}, high[81] = {0};
    low[20] = 0.25; high[80] = 0.75;
    GsTargetBranch belief[] = {{{100, 100, 100}, low, 81}, {{200, 200, 200}, high, 81}};
    s.target = (GsTarget){.has_belief = 1, .base_hp = 99, .belief = belief, .belief_count = 2};
    GsEating meal = {101, 0, 3};
    GsSpecHit hit = gs_spec_hit_of(&arena, 102, 70, 70, 1);
    GsOutcome outcome = gs_strikes(&arena, &s, &hit, 1, &meal, 1, GS_ECHO_RECOIL, 100, 103);
    assert(fabs(outcome.kill - 0.25 * (1 - gs_wants(58))) < 1e-12);
    belief[0].ready = (GsTargetReady){200, 200, 200};
    belief[1].ready = (GsTargetReady){100, 100, 100};
    outcome = gs_strikes(&arena, &s, &hit, 1, &meal, 1, GS_ECHO_RECOIL, 100, 103);
    assert(fabs(outcome.kill - 0.25) < 1e-12);

    /* Alternative clocks for one supported HP form a set, not equally weighted states. */
    double support[81] = {0}; support[20] = 1;
    GsTargetBranch history[] = {{{100, 200, 200}, support, 81}, {{200, 100, 200}, support, 81}};
    s.target = (GsTarget){.has_history = 1, .base_hp = 99, .history = history, .history_count = 2};
    hit = gs_spec_hit_of(&arena, 102, 40, 40, 1);
    outcome = gs_strikes(&arena, &s, &hit, 1, &meal, 1, GS_ECHO_RECOIL, 100, 103);
    assert(outcome.kill == 0);

    /* A refused hard food remains pending until its own clock, beyond the sole scheduled meal. */
    support[20] = 0; support[1] = 1;
    history[0].ready = (GsTargetReady){103, 200, 200};
    s.target.history_count = 1;
    hit = gs_spec_hit_of(&arena, 104, 10, 10, 1);
    outcome = gs_strikes(&arena, &s, &hit, 1, &meal, 1, GS_ECHO_RECOIL, 100, 105);
    assert(outcome.kill == 0);

    /* Brewing uses the opponent's actual base HP, not the range ceiling or local base. */
    history[0].ready = (GsTargetReady){200, 100, 200};
    s.target.base_hp = 50;
    hit = gs_spec_hit_of(&arena, 102, 11, 11, 1);
    outcome = gs_strikes(&arena, &s, &hit, 1, &meal, 1, GS_ECHO_RECOIL, 100, 103);
    assert(fabs(outcome.kill - 1) < 1e-12);
    s.target.base_hp = 99;
    outcome = gs_strikes(&arena, &s, &hit, 1, &meal, 1, GS_ECHO_RECOIL, 100, 103);
    assert(outcome.kill == 0);
    GsSpecPlan single = {GS_CHOICE_VOIDWAKER, &hit, 1};
    GsSpecDecision at = gs_spec_continue_burst(&arena, &s, &single, 0, 0, 103);
    assert(at.chance > 0);
    double boundary = at.chance;
    at = gs_spec_continue_burst(&arena, &s, &single, boundary, 0, 103);
    assert(at.choice == GS_CHOICE_VOIDWAKER);
    at = gs_spec_continue_burst(&arena, &s, &single, nextafter(boundary, INFINITY), 0, 103);
    assert(at.choice == GS_CHOICE_HOLD);
    at = gs_spec_continue_burst(&arena, &s, &single, nextafter(boundary, 0), 0, 103);
    assert(at.choice == GS_CHOICE_VOIDWAKER);
    free(arena.base);
}

static void burst_contract(void) {
    Arena arena = arena_make((size_t)1 << 27);
    GsSpecSituation s = {0};
    s.tick = 100; s.warning.since = GS_NONE; s.hard_food_ready = 102;
    s.pid = GS_PID_LOCAL; s.vengeance = GS_VENGEANCE_DOWN;
    s.rebound.kind = GS_REBOUND_SPENT;
    s.hitpoints = (GsHealthRange){80, 80, 99};
    s.local = gs_health_of(99);
    GsSpecHit hits[] = {gs_spec_hit_of(&arena, 101, 50, 50, 1), gs_spec_hit_of(&arena, 105, 50, 50, 1)};
    GsEating meal = {102, 0, GS_MEAL_FOODS};
    GsOutcome fed = gs_strikes(&arena, &s, hits, 2, &meal, 1, GS_ECHO_RECOIL, 100, 106);
    GsOutcome unfed = gs_strikes(&arena, &s, hits, 2, NULL, 0, GS_ECHO_RECOIL, 100, 106);
    assert(fed.kill == 0 && fabs(unfed.kill - 1) < 1e-12);
    hits[1] = gs_spec_hit_of(&arena, 105, 121, 121, 1);
    GsSpecPlan plans[] = {{GS_CHOICE_VOIDWAKER, hits, 1}, {GS_CHOICE_VOIDWAKER_BURST, hits, 2}};
    size_t mark = arena.used;
    for (int energy = 0; energy <= 100; energy += 50) {
        arena.used = mark;
        GsSpecDecision d = gs_auto_spec_decide(&arena, &s, plans, 2, NULL, 0, energy);
        if (energy < 100) assert(d.choice != GS_CHOICE_VOIDWAKER_BURST);
        else {
            assert(d.choice == GS_CHOICE_VOIDWAKER_BURST);
            assert(d.bar == gs_spec_bar(100, 2) && d.kept == 0 && d.held == gs_spec_held(100));
            assert(d.burst_bar == d.held);
            GsSpecDecision followup = gs_spec_continue_burst(&arena, &s, &plans[0], d.burst_bar, 0, d.through);
            assert(followup.choice == GS_CHOICE_HOLD && followup.kept == 0 && followup.bar == d.burst_bar);
        }
    }
    assert(isinf(gs_spec_bar(99, GS_CHOICE_TICKETS[GS_CHOICE_VOIDWAKER_BURST])));
    free(arena.base);
}

static void timed_contract(void) {
    Arena arena = arena_make((size_t)1 << 27);
    GsSpecSituation s = {0};
    s.tick = 100; s.warning.since = GS_NONE; s.hard_food_ready = 200;
    s.pid = GS_PID_LOCAL; s.vengeance = GS_VENGEANCE_DOWN; s.rebound.kind = GS_REBOUND_SPENT;
    s.hitpoints = (GsHealthRange){40, 40, 99}; s.local = gs_health_of(99);
    double support[41] = {0}; support[40] = 1;
    GsTargetBranch history = {{200, 200, 200}, support, 41};
    s.target = (GsTarget){.has_history = 1, .base_hp = 99, .history = &history, .history_count = 1};
    GsSpecHit single_hit = gs_spec_hit_of(&arena, 105, 40, 40, 0.5);
    GsSpecHit combo_hits[] = {gs_spec_hit_of(&arena, 105, 40, 40, 0.6), gs_spec_hit_of(&arena, 106, 0, 0, 1)};
    GsSpecPlan options[] = {{GS_CHOICE_MAUL_VOIDWAKER, combo_hits, 2, 104}, {GS_CHOICE_VOIDWAKER, &single_hit, 1, 104}};
    GsSpecDecision d = gs_auto_spec_decide(&arena, &s, options, 2, NULL, 0, 100);
    assert(d.choice == GS_CHOICE_VOIDWAKER);
    GsSpecHit good = gs_spec_hit_of(&arena, 105, 40, 40, 0.8);
    GsSpecHit better = gs_spec_hit_of(&arena, 105, 40, 40, 0.81);
    GsSpecHit zero = gs_spec_hit_of(&arena, 105, 0, 0, 1);
    options[0] = (GsSpecPlan){GS_CHOICE_VOIDWAKER, &good, 1, 104};
    options[1] = (GsSpecPlan){GS_CHOICE_VOIDWAKER, &good, 1, 0};
    d = gs_auto_spec_decide(&arena, &s, options, 2, NULL, 0, 100);
    assert(d.choice == GS_CHOICE_VOIDWAKER && d.attack_tick == 0);
    options[0].attack_tick = 0; options[1].attack_tick = 104;
    d = gs_auto_spec_decide(&arena, &s, options, 2, NULL, 0, 100);
    assert(d.attack_tick == 0);
    options[1].hits = &better;
    d = gs_auto_spec_decide(&arena, &s, options, 2, NULL, 0, 100);
    assert(d.attack_tick == 104);
    for (int mode = 0; mode < 2; mode++) {
        int accepted_tick = mode ? 104 : 0;
        GsSpecPlan original = {GS_CHOICE_VOIDWAKER, &good, 1, accepted_tick};
        GsSpecDecision accepted = gs_auto_spec_decide(&arena, &s, &original, 1, NULL, 0, 100);
        options[0] = (GsSpecPlan){GS_CHOICE_VOIDWAKER, &good, 1, mode ? 103 : 104};
        options[1] = (GsSpecPlan){GS_CHOICE_VOIDWAKER, &zero, 1, accepted_tick};
        d = gs_spec_commitment(&arena, &s, &accepted, options, 2, NULL, 0, 100, 1);
        assert(d.choice == GS_CHOICE_HOLD);
        options[0] = options[1]; options[1] = original;
        d = gs_spec_commitment(&arena, &s, &accepted, options, 2, NULL, 0, 100, 1);
        assert(d.choice == GS_CHOICE_VOIDWAKER && d.attack_tick == accepted_tick);
    }
    free(arena.base);
}
#endif

static void continuation_contract(void) {
    /* These are the production Java policy probabilities, not fitted test outputs. */
    const double continuation[] = {0, 0.30164267160692, 0.39811423486135994};
    for (int energy = 0; energy <= 100; energy++) {
        int tickets = energy / GS_TICKET;
        assert(fabs(gs_spec_held(energy) - continuation[tickets]) < 1e-12);
        for (int cost = 0; cost <= GS_MOST_TICKETS; cost++) {
            double kept = tickets < cost ? 0 : continuation[tickets - cost];
            assert(fabs(gs_spec_kept(energy, cost) - kept) < 1e-12);
            if (tickets < cost) assert(isinf(gs_spec_bar(energy, cost)));
            else assert(fabs(gs_spec_bar(energy, cost) - (continuation[tickets] - kept) / (1 - kept)) < 1e-12);
        }
    }
}

int main(int argc, char** argv) {
    continuation_contract();
    incoming_echo_contract();
#ifndef GS_HP_BELIEF
    target_contract();
    burst_contract();
    timed_contract();
#endif
    if (argc == 2 && !strcmp(argv[1], "--contract-only")) {
        puts("AutoSpec incoming echo contract verified");
        return 0;
    }
    FILE* f = fopen(argc > 1 ? argv[1] : "ocean/osrs/tests/fixtures/auto_spec_oracle.tap", "r");
    assert(f);
    Arena arena = arena_make((size_t)1 << 28);
    static Record record;
    long total = 0, matched = 0, choices[GS_CHOICE_COUNT] = {0}, casts = 0;
    int shown = 0;
    double seconds = 0;
    while (read_record(f, &record)) {
        GsSpecPlan plans[MAX_PLANS], after[MAX_PLANS];
        for (int i = 0; i < record.plan_count; i++) plans[i] = (GsSpecPlan){record.plans[i].choice, record.plans[i].hits, record.plans[i].count, record.plans[i].attack_tick};
        for (int i = 0; i < record.after_count; i++) after[i] = (GsSpecPlan){record.after[i].choice, record.after[i].hits, record.after[i].count, record.after[i].attack_tick};
        struct timespec t0, t1;
        clock_gettime(CLOCK_MONOTONIC, &t0);
        arena.used = 0;
        GsSpecDecision d = gs_auto_spec_decide(&arena, &record.s, plans, record.plan_count, after, record.after_count, record.energy);
        clock_gettime(CLOCK_MONOTONIC, &t1);
        seconds += (t1.tv_sec - t0.tv_sec) + (t1.tv_nsec - t0.tv_nsec) * 1e-9;
        total++;
        int ok = d.choice == record.choice && d.cast == record.cast && d.aim == record.aim && d.attack_tick == record.attack_tick && d.chance == record.chance && d.bar == record.bar && d.death == record.death && d.held == record.held && d.kept == record.kept;
        matched += ok;
        choices[record.choice]++;
        casts += record.cast == GS_CAST_NOW;
        if (!ok && shown++ < 8)
            printf("MISMATCH call %ld tick %d energy %d: java %d/%d aim %d attack %d chance %.17g bar %.17g death %.17g held %.17g kept %.17g | c %d/%d aim %d attack %d chance %.17g bar %.17g death %.17g held %.17g kept %.17g\n", total, record.s.tick, record.energy,
                record.choice, record.cast, record.aim, record.attack_tick, record.chance, record.bar, record.death, record.held, record.kept,
                d.choice, d.cast, d.aim, d.attack_tick, d.chance, d.bar, d.death, d.held, d.kept);
    }
    if (!total) { fputs("Spec oracle contains no decisions\n", stderr); exit(EXIT_FAILURE); }
    printf("%ld calls, %ld bit-exact (%.3f%%); outcomes hold %ld voidwaker %ld maul+voidwaker %ld maul %ld voidwaker-burst %ld, casts now %ld; C time %.3f s\n", total, matched,
        100.0 * matched / total, choices[GS_CHOICE_HOLD], choices[GS_CHOICE_VOIDWAKER], choices[GS_CHOICE_MAUL_VOIDWAKER], choices[GS_CHOICE_MAUL], choices[GS_CHOICE_VOIDWAKER_BURST], casts, seconds);
    return matched == total ? 0 : 1;
}
