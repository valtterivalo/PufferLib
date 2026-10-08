/* Differential test of the C AutoEat port against the Java oracle.
 *
 *   cc -std=c11 -O2 -I. -o /tmp/test_gs_auto_eat ocean/osrs/tests/test_gs_auto_eat.c -lm
 *   /tmp/test_gs_auto_eat [ORACLE.tap]
 *
 * Without an argument it replays the 150 record fixture of tests/fixtures, run from the repo root. The full oracle of 26368 calls
 * (6592 recorded decisions, each with three repriced variants) is made by the Light repo's rollout-export branch.
 *
 * The oracle is a tap file written by AutoEat.tap during `./gradlew :gear-swapper:fightReplay -Precording=... -Ptap=FILE`
 * in the Light repo: one record per AutoEat.decide call with its inputs and verdict. */
#include <stdio.h>
#include <time.h>
#include "../gearswapper/gs_auto_eat.h"

static GsStyle style_of(const char* s) { return !strcmp(s, "MELEE") ? GS_MELEE : !strcmp(s, "RANGED") ? GS_RANGED : GS_MAGIC; }
static GsPid pid_of(const char* s) { return !strcmp(s, "LOCAL") ? GS_PID_LOCAL : !strcmp(s, "OPPONENT") ? GS_PID_OPPONENT : GS_PID_UNKNOWN; }
static GsVengeance vengeance_of(const char* s) {
    return !strcmp(s, "UP") ? GS_VENGEANCE_UP : !strcmp(s, "DOWN") ? GS_VENGEANCE_DOWN : !strcmp(s, "MAYBE") ? GS_VENGEANCE_MAYBE : GS_VENGEANCE_UNKNOWN;
}
static GsDecision decision_of(const char* s) { return !strcmp(s, "EAT_NOW") ? GS_EAT_NOW : !strcmp(s, "WAIT") ? GS_WAIT : GS_EXPOSED; }

#ifdef GS_HP_BELIEF
static void read_watched(const char* p, GsWarning* w) {
    while (*p == ' ') p++;
    for (; *p && *p != '\n' && *p != '-'; p++) {
        assert(w->watched_count < GS_WATCHED_LIMIT);
        w->watched[w->watched_count++] = *p == 'Q' ? GS_SEEN_QUIET : *p == 'A' ? GS_SEEN_ATE : GS_SEEN_HIDDEN;
    }
}
#endif

static GsAttack read_attack(char** p) {
    GsAttack a;
    char style[16], source[24];
    int n;
    sscanf(*p, "%15s %d %d %d %d %lf %23s %d%n", style, &a.least, &a.most, &a.cost, &a.interval, &a.accuracy, source, &a.arrows, &n);
    a.style = style_of(style);
    a.godsword = !strcmp(source, "GODSWORD_SPEC");
    *p += n;
    return a;
}

static int read_list(const char* text, int* out) {
    if (!strcmp(text, "-")) return 0;
    int n = 0;
    while (*text) {
        out[n++] = (int)strtol(text, (char**)&text, 10);
        if (*text == ',') text++;
    }
    return n;
}

typedef struct {
    GsEatSituation s;
    GsFood foods[GS_MAX_FOODS]; int food_count;
    GsMeal meals[GS_MAX_MEALS]; int meal_count;
    char decision[16]; double risk, meal_risk; int verdict_foods[GS_MAX_MEAL], verdict_food_count, complete; GsUrgency urgency; GsHealth health;
} Record;

static int read_record(FILE* f, Record* r) {
    memset(r, 0, sizeof(*r));
    char line[4096];
    int started = 0;
    while (fgets(line, sizeof(line), f)) {
        char tag[8];
        int consumed = 0;
        if (sscanf(line, "%7s %n", tag, &consumed) < 1) continue;
        char* p = line + consumed;
        started = 1;
        GsEatSituation* s = &r->s;
        if (!strcmp(tag, "S")) {
            char pid[16], style[16], offence[16], vengeance[16], recoil[16];
            sscanf(p, "%d %d %15s %d %15s %d %d %d %lf %lf %15s %lf %lf %lf %d %15s %15s %d", &s->tick, &s->hitpoints, pid, &s->distance, style,
                &s->local_most, &s->local_normal, &s->local_interval, &s->local_opening_accuracy, &s->local_accuracy, offence, &s->tolerance,
                &s->pid_flip, &s->stakes, &s->floor, vengeance, recoil, &s->energy_high);
            s->pid = pid_of(pid); s->local_style = style_of(style); s->offence = !strcmp(offence, "ENGAGED") ? GS_ENGAGED : GS_IDLE;
            s->vengeance = vengeance_of(vengeance); s->recoil_seen = !strcmp(recoil, "SEEN");
        } else if (!strcmp(tag, "DR")) sscanf(p, "%d %d %lf %lf", &s->drained_normal, &s->drained_most, &s->drained_accuracy, &s->drained_opening_accuracy);
        else if (!strcmp(tag, "OT")) sscanf(p, "%d %d", &s->opponent_timer.seen, &s->opponent_timer.next);
        else if (!strcmp(tag, "LT")) sscanf(p, "%d %d %d", &s->local_timer.seen, &s->local_timer.next, &s->local_timer.earliest);
        else if (!strcmp(tag, "OH")) sscanf(p, "%d %d %d", &s->opponent_hp.low, &s->opponent_hp.high, &s->opponent_hp.max);
        else if (!strcmp(tag, "OB")) sscanf(p, "%d %d %d", &s->opponent_bar.low, &s->opponent_bar.high, &s->opponent_bar.max);
        else if (!strcmp(tag, "OA")) { assert(s->attack_count < GS_MAX_ATTACKS); s->attacks[s->attack_count++] = read_attack(&p); }
        else if (!strcmp(tag, "GM")) { s->has_maul = 1; s->maul = read_attack(&p); }
        else if (!strcmp(tag, "HID")) {
            assert(s->hidden_count < GS_MAX_HIDDEN);
            GsHidden* h = &s->hidden[s->hidden_count++];
            int slot, n;
            sscanf(p, "%d%n", &slot, &n);
            p += n;
            h->is_maul = slot == 1;
            h->attack = read_attack(&p);
            h->probability = strtod(p, NULL);
        } else if (!strcmp(tag, "OSW")) { s->has_swing = 1; sscanf(p, "%d %d %d %lf %d", &s->swing.lands, &s->swing.least, &s->swing.most, &s->swing.accuracy, &s->swing.arrows); }
        else if (!strcmp(tag, "OE")) { s->has_owed = 1; s->owed.source = SRC_VENGEANCE_ECHO; sscanf(p, "%d %d", &s->owed.tick, &s->owed.most); s->owed.least = s->owed.most; s->owed.accuracy = 1; }
        else if (!strcmp(tag, "LSW")) { s->has_local_swing = 1; sscanf(p, "%d %d %d %lf %d", &s->local_swing.lands, &s->local_swing.least, &s->local_swing.most, &s->local_swing.accuracy, &s->local_swing.arrows); }
        else if (!strcmp(tag, "W")) {
            s->warning.since = GS_NONE;
            if (strncmp(p, "none", 4)) s->warning.since = (int)strtol(p, &p, 10); else p += 4;
#ifdef GS_HP_BELIEF
            read_watched(p, &s->warning);
            while (0) {
#else
            while (*p && *p != '\n') {
#endif
                char* end;
                long v = strtol(p, &end, 10);
                if (end == p) break;
                assert(s->warning.unseen_count < 64);
                s->warning.unseen[s->warning.unseen_count++] = (int)v;
                p = end;
            }
        } else if (!strcmp(tag, "F")) {
            GsFood* food = &r->foods[r->food_count++];
            int kind, drained;
            char drain[16];
            sscanf(p, "%d %d %d %d %15s", &kind, &food->heal, &food->ready, &food->remaining, drain);
            drained = !strcmp(drain, "DRAINED");
            food->kind = (GsConsumable)kind; food->drained = drained;
        } else if (!strcmp(tag, "M")) {
            assert(r->meal_count < GS_MAX_MEALS);
            GsMeal* m = &r->meals[r->meal_count++];
            char a[64], b[64], c[64];
            sscanf(p, "%63s %63s %63s %d %d %d", a, b, c, &m->earliest_process, &m->resume, &m->complete);
            m->n = read_list(a, m->foods);
            read_list(b, m->offsets);
            read_list(c, m->following);
        } else if (!strcmp(tag, "V")) {
            char foods[64], urgency[16];
            sscanf(p, "%15s %lf %lf %63s %d %15s", r->decision, &r->risk, &r->meal_risk, foods, &r->complete, urgency);
            r->verdict_food_count = read_list(foods, r->verdict_foods);
            r->urgency = !strcmp(urgency, "DEFERRABLE") ? GS_DEFERRABLE : GS_URGENT;
        } else if (!strcmp(tag, "VH")) {
            r->health.hitpoints = (int)strtol(p, &p, 10);
            while (*p == ' ') {
                GsSpecHeal* heal = &r->health.heals[r->health.heal_count++];
                heal->tick = (int)strtol(p, &p, 10); p++;
                heal->amount = (int)strtol(p, &p, 10); p++;
                heal->cap = (int)strtol(p, &p, 10);
            }
        } else if (!strcmp(tag, "END")) return 1;
    }
    return started && 0;
}

int main(int argc, char** argv) {
    FILE* f = fopen(argc > 1 ? argv[1] : "ocean/osrs/tests/fixtures/auto_eat_oracle.tap", "r");
    assert(f);
    Arena arena = arena_make((size_t)1 << 30);
    static Record record;
    long total = 0, matched = 0, decision_bad = 0, foods_bad = 0, complete_bad = 0, risk_bad = 0, calls_with_foods = 0, matched_with_foods = 0;
    double worst = 0;
    double seconds = 0;
    int shown = 0;
    while (read_record(f, &record)) {
        struct timespec t0, t1;
        clock_gettime(CLOCK_MONOTONIC, &t0);
        arena.used = 0;
        GsVerdict v = gs_auto_eat_decide(&record.s, record.foods, record.food_count, record.meals, record.meal_count, &arena);
        clock_gettime(CLOCK_MONOTONIC, &t1);
        seconds += (t1.tv_sec - t0.tv_sec) + (t1.tv_nsec - t0.tv_nsec) * 1e-9;
        total++;
        int bad_decision = v.decision != decision_of(record.decision);
        int bad_foods = v.food_count != record.verdict_food_count || memcmp(v.foods, record.verdict_foods, (size_t)v.food_count * sizeof(int));
        int bad_complete = v.complete != record.complete || v.urgency != record.urgency || memcmp(&v.health, &record.health, sizeof(GsHealth));
        double diff = fmax(fabs(v.risk - record.risk), fabs(v.meal_risk - record.meal_risk));
        int bad_risk = diff > 1e-9 * fmax(1, fabs(record.risk));
        decision_bad += bad_decision; foods_bad += bad_foods; complete_bad += bad_complete; risk_bad += bad_risk;
        if (diff > worst && !isinf(diff)) worst = diff;
        int ok = !bad_decision && !bad_foods && !bad_complete && !bad_risk;
        matched += ok;
        calls_with_foods += record.food_count > 0;
        matched_with_foods += ok && record.food_count > 0;
        if (!ok && shown < 8) {
            shown++;
            printf("MISMATCH call %ld tick %d hp %d: java %s risk %.9g meal %.9g foods %d complete %d | c %d risk %.9g meal %.9g foods %d complete %d\n", total, record.s.tick, record.s.hitpoints,
                record.decision, record.risk, record.meal_risk, record.verdict_food_count, record.complete, v.decision, v.risk, v.meal_risk, v.food_count, v.complete);
        }
    }
    printf("%ld calls, %ld match (%.3f%%); with foods on offer %ld calls, %ld match; mismatches: decision %ld foods %ld complete %ld risk %ld; worst |risk diff| %.3g; C time %.3f s\n",
        total, matched, 100.0 * matched / total, calls_with_foods, matched_with_foods, decision_bad, foods_bad, complete_bad, risk_bad, worst, seconds);
    return matched == total ? 0 : 1;
}
