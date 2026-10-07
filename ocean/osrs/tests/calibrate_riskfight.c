/* Calibrates the Riskfight opponent against recorded fights.
 *
 *   cc -std=c11 -O2 -I. -o /tmp/calibrate_riskfight ocean/osrs/tests/calibrate_riskfight.c -lm
 *   /tmp/calibrate_riskfight teacher TRAIN_DAY TEST_DAY tape...
 *   /tmp/calibrate_riskfight rollouts N K TRAIN_DAY TEST_DAY tape...
 *   /tmp/calibrate_riskfight branch N HORIZON tape...
 *
 * teacher scores the one-step action distribution of each model from the recorded state against what he did.
 * rollouts runs k-tick counterfactuals with our side closed loop. Tapes come from tools/rollout_export.py in the
 * Light repo. A day is the first 8 characters of the tape session. */
#define _POSIX_C_SOURCE 200809L
#include <time.h>
#include "riskfight_rollout.h"
#include "riskfight_spectator.h"

enum { MAX_TAPES = 64, METRICS = 12, PIT_BINS = 10, REL_BINS = 7, EPISODE_BINS = 9, MAX_ROLLOUTS = 4096,
    FIT_SAMPLES = 4, SCORE_SAMPLES = 16, PROFILE_SAMPLES = 64, BIG_HIT_EPISODE = 30, REARM_TICKS = 4, MARGIN = 10 };
enum { FLAG_EAT = 1, FLAG_SWING = 2, FLAG_VW = 4, FLAG_MAUL = 8, FLAG_VENG = 16 };

static double seconds(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec + t.tv_nsec * 1e-9;
}

static int tape_day(const Tape* tape) { return atoi(tape->session); }
static int in_day(const Tape* tape, int day) { return day == 0 || (day > 0 ? tape_day(tape) == day : tape_day(tape) != -day); }

static int flag_at(const Tape* tape, int tick, int flag) {
    return tick >= tape->lo && tick <= tape->hi && (tape->his_flags[tick - tape->lo] & flag);
}
static int rec_eat_at(const Tape* tape, int tick) { return flag_at(tape, tick, FLAG_EAT); }

static void prepare(Tape* tape) {
    static WireIndex wire_index;
    static int wire_indexed;
    const char* wire_path = getenv("GS_WIRE");
    observer_register_items();
    if (wire_path && !wire_indexed) { wire_index_build(wire_path, &wire_index); wire_indexed = 1; }
    int n = tape->hi - tape->lo + 1;
    if (wire_path) {
        tape->observer_series = calloc((size_t)n, sizeof(Observer));
        observer_series_build(wire_path, &wire_index, tape, tape->observer_series);
    }
    tape->his_flags = calloc(n, 1);
    tape->hist_series = calloc(n, sizeof(Hist));
    for (int i = 0; i < tape->anim_count; i++) {
        const TapeAnim* a = &tape->anims[i];
        if (a->side != SIDE_HIM || a->tick < tape->lo || a->tick > tape->hi) continue;
        uint8_t* f = &tape->his_flags[a->tick - tape->lo];
        if (a->anim == ANIM_EAT) *f |= FLAG_EAT;
        if (a->anim == ANIM_VW_SPEC) *f |= FLAG_VW;
        if (a->anim == ANIM_MAUL_SPEC) *f |= FLAG_MAUL;
        if (a->anim == ANIM_VENG_A || a->anim == ANIM_VENG_B) *f |= FLAG_VENG;
        for (size_t k = 0; k < sizeof(SWING_ANIMS) / sizeof(int); k++)
            if (a->anim == SWING_ANIMS[k] && a->anim != ANIM_MAUL_SPEC) *f |= FLAG_SWING;
    }
    hist_series(tape, tape->hist_series);
}

/* ---------- teacher forcing ---------- */

typedef struct {
    double n, y, p, logloss, brier;
    double rel_n[REL_BINS], rel_p[REL_BINS], rel_y[REL_BINS];
} Score;

static int rel_bin(double p) {
    return p < 0.02 ? 0 : p < 0.05 ? 1 : p < 0.1 ? 2 : p < 0.2 ? 3 : p < 0.35 ? 4 : p < 0.6 ? 5 : 6;
}
static const char* REL_LABEL[REL_BINS] = {"<2%", "2-5%", "5-10%", "10-20%", "20-35%", "35-60%", ">60%"};

static double clip(double p) { return p < 0.002 ? 0.002 : p > 0.998 ? 0.998 : p; }

static void score_add(Score* s, double p, int y) {
    double c = clip(p);
    s->n++; s->y += y; s->p += p;
    s->logloss -= y ? log(c) : log(1 - c);
    s->brier += (p - y) * (p - y);
    int b = rel_bin(p);
    s->rel_n[b]++; s->rel_p[b] += p; s->rel_y[b] += y;
}

typedef struct { int eligible; int y; } Conditioned;

static Conditioned conditioned(Action a, const Probe* probe, int flags) {
    int eat = flags & FLAG_EAT, swing = flags & FLAG_SWING;
    switch (a) {
        case ACT_EAT_BIG:
        case ACT_EAT_WARNING: return (Conditioned){probe->eligible[a], eat != 0};
        case ACT_SWING: return (Conditioned){probe->eligible[a] && !eat, swing != 0};
        case ACT_VW_SPEC: return (Conditioned){probe->eligible[a] && swing, (flags & FLAG_VW) != 0};
        case ACT_MAUL_SPEC: return (Conditioned){probe->eligible[a], (flags & FLAG_MAUL) != 0};
        case ACT_VENGEANCE: return (Conditioned){probe->eligible[a], (flags & FLAG_VENG) != 0};
        case ACT_COUNT: break;
    }
    abort();
}

static void load_hidden(Sim* sim, const Tape* tape, int t, int sample, Probe* probe) {
    uint32_t rng = osrs_lowbias32((uint32_t)(t * 7919 + sample * 104729 + tape->index * 13 + tape->lo)) | 1;
    RiskfightSnapshot snap;
    tape_snapshot(tape, t - 1, &DEFAULT_PRIORS, &rng, &snap);
    riskfight_load_state(&sim->state, &sim->context, &snap);
    const Hist* hist = &((const Hist*)tape->hist_series)[t - 1 - tape->lo];
    *probe = probe_state(&sim->state, hist, t, has_item(&sim->state, 1, ITEM_VOIDWAKER), has_maul(&sim->state, 1));
}

typedef struct { Sample* samples; int count, capacity; } SampleSet;

static void fit_model(Tape* tapes, int count, int day, OppFit* fit) {
    static SampleSet sets[ACT_COUNT];
    for (int a = 0; a < ACT_COUNT; a++) sets[a].count = 0;
    Sim* sim = malloc(sizeof(Sim));
    sim_init(sim);
    for (int i = 0; i < count; i++) {
        if (!in_day(&tapes[i], day)) continue;
        g_tape = &tapes[i];
        for (int t = tapes[i].lo + 1; t <= tapes[i].hi; t++) {
            int flags = tapes[i].his_flags[t - tapes[i].lo];
            for (int j = 0; j < FIT_SAMPLES; j++) {
                Probe probe;
                load_hidden(sim, &tapes[i], t, j, &probe);
                for (int a = 0; a < ACT_COUNT; a++) {
                    Conditioned c = conditioned((Action)a, &probe, flags);
                    if (!c.eligible) continue;
                    SampleSet* set = &sets[a];
                    if (set->count == set->capacity) {
                        set->capacity = set->capacity ? set->capacity * 2 : 4096;
                        set->samples = realloc(set->samples, set->capacity * sizeof(Sample));
                    }
                    set->samples[set->count++] = (Sample){probe.x[a], (float)c.y, 1.0f / FIT_SAMPLES};
                }
            }
        }
    }
    for (int a = 0; a < ACT_COUNT; a++) logit_fit(&fit->logit[a], ACTION_DIM[a], sets[a].samples, sets[a].count);
    free(sim);
}

enum { SERVING_MIN = 6, SERVING_SINGLE_MAX = 30, SERVING_DOUBLE_MAX = 48, SERVING_WINDOW = 10, BEFORE_WINDOW = 8 };

static int bar_mid(int ratio, int scale) { return (warning_bar_low(ratio, scale) + warning_bar_high(ratio, scale)) / 2; }

/* Meal size from the bar on both sides of an eat: heal = hp after + damage taken since - hp before, bars read at ticks a hit on him
 * refreshed them with no other meal between. Single is one hard food, double a hard food and one more, triple all three. */
static void infer_servings(const Tape* tapes, int count, int day, OppFit* fit, int* eats, int identified[2]) {
    int counts[2][3] = {{0}};
    *eats = 0;
    for (int i = 0; i < count; i++) {
        const Tape* tape = &tapes[i];
        if (!in_day(tape, day)) continue;
        const Hist* series = tape->hist_series;
        for (int e = tape->lo + 1; e + SERVING_WINDOW <= tape->hi; e++) {
            if (!rec_eat_at(tape, e)) continue;
            (*eats)++;
            const TapeTick* before = tape_tick(tape, e - 1);
            if (before->last_bar_hit < e - BEFORE_WINDOW || before->meals_since != 0 || before->bar_ratio <= 0 || before->bar_ratio >= before->bar_scale) continue;
            int hit_now = 0, after = -1, damage = 0;
            for (int x = e; x <= e + SERVING_WINDOW && after < 0; x++) {
                if (x > e && rec_eat_at(tape, x)) break;
                for (int h = 0; h < tape->hit_count; h++) {
                    const TapeHit* hit = &tape->hits[h];
                    if (hit->tick != x || hit->side != SIDE_HIM) continue;
                    if (x == e) hit_now = 1;
                    damage += hit->type == 65 ? 0 : hit->amount;
                    after = x;
                }
            }
            if (hit_now || after < 0) continue;
            const TapeTick* later = tape_tick(tape, after);
            if (later->bar_ratio <= 0 || later->bar_ratio >= later->bar_scale) continue;
            int heal = bar_mid(later->bar_ratio, later->bar_scale) + damage - bar_mid(before->bar_ratio, before->bar_scale);
            int state = warning_since(&series[e - 1 - tape->lo].warning) != WARNING_NONE;
            counts[state][heal < SERVING_SINGLE_MAX ? 0 : heal < SERVING_DOUBLE_MAX ? 1 : 2]++;
        }
    }
    int pooled[3] = {counts[0][0] + counts[1][0], counts[0][1] + counts[1][1], counts[0][2] + counts[1][2]};
    int pooled_total = pooled[0] + pooled[1] + pooled[2];
    for (int w = 0; w < 2; w++) {
        int total = counts[w][0] + counts[w][1] + counts[w][2];
        identified[w] = total;
        const int* c = total >= SERVING_MIN ? counts[w] : pooled_total >= SERVING_MIN ? pooled : NULL;
        if (!c) continue;
        double sum = c[0] + c[1] + c[2];
        fit->serving[w][0] = (float)(c[0] / sum);
        fit->serving[w][1] = fit->serving[w][2] = (float)(c[1] / sum / 2);
        fit->serving[w][3] = (float)(c[2] / sum);
        for (int k = 0; k < 3; k++) fit->served[w][k] = counts[w][k];
    }
}

static void print_fit(const char* title, const OppFit* fit, int eats, const int identified[2]) {
    static const char* TERMS[ACT_COUNT] = {
        "intercept (bar <=8), bar 9-13, bar 14-24, bar 25+, lag 1, lag 2, lag 3, lag 4-8 (ticks since a >=20 hit on him), warning active",
        "intercept (bar <=8), bar 9-13, bar 14-24, bar 25+, lag 1, lag 2, lag 3, lag 4-8 (ticks since the Java warning), warning active",
        "intercept (ready 1 tick), ready 2, ready 3-5, ready 6+, ate within 3 ticks",
        "intercept (our hp <45), our hp 45-85, our hp >85, VW spec within 5 ticks",
        "intercept, VW spec this or last tick",
        "intercept (eligible: not active, cooldown over)"};
    printf("%s\n", title);
    for (int a = 0; a < ACT_COUNT; a++) {
        const Logit* l = &fit->logit[a];
        printf("  %-24s %5.0f events / %6.0f ticks  w =", ACTION_NAME[a], (double)l->events / FIT_SAMPLES, (double)l->opportunities / FIT_SAMPLES);
        for (int d = 0; d < ACTION_DIM[a]; d++) printf(" %6.2f", l->w[d]);
        printf("\n      terms: %s\n", TERMS[a]);
    }
    printf("  meal size: %d eats on the day, identifiable %d with the warning off and %d with it on\n", eats, identified[0], identified[1]);
    for (int w = 0; w < 2; w++)
        printf("    warning %-3s single %d double %d triple %d -> shares %.2f %.2f %.2f %.2f%s\n", w ? "on" : "off", fit->served[w][0], fit->served[w][1], fit->served[w][2],
            fit->serving[w][0], fit->serving[w][1], fit->serving[w][2], fit->serving[w][3], identified[w] >= SERVING_MIN ? "" : (identified[0] + identified[1] >= SERVING_MIN ? " (pooled)" : " (AutoSpec.SERVING_SHARES)"));
}

static void print_score(const char* action, const Score* model, const Score* profiles, double train_rate) {
    double rate = model->y / model->n;
    double constant = -model->n * (rate * log(clip(train_rate)) + (1 - rate) * log(1 - clip(train_rate)));
    printf("  %-24s %5.0f ticks %4.0f events, rate %.3f (train %.3f) | logloss/tick fit %.4f const %.4f (%+.1f%%) profiles %.4f | Brier fit %.4f const %.4f\n",
        action, model->n, model->y, rate, train_rate, model->logloss / model->n, constant / model->n,
        100 * (constant - model->logloss) / constant, profiles->logloss / profiles->n, model->brier / model->n,
        rate * (1 - rate) + (rate - train_rate) * (rate - train_rate));
    for (int b = 0; b < REL_BINS; b++)
        if (model->rel_n[b]) printf("      pred %-7s %6.0f ticks  mean pred %.3f  observed %.3f\n", REL_LABEL[b], model->rel_n[b],
            model->rel_p[b] / model->rel_n[b], model->rel_y[b] / model->rel_n[b]);
}

/* GS_FIT=path replaces the fit on our own tapes with a saved one, the opponent fitted on spectated duels. */
static void fit_override(OppFit* fit) {
    const char* path = getenv("GS_FIT");
    if (!path) return;
    FILE* f = fopen(path, "rb");
    assert(f && fread(fit, sizeof(*fit), 1, f) == 1);
    fclose(f);
}

static void teacher_score(Tape* tapes, int count, const OppFit* fit_in, int test_day) {
    const OppFit fit = *fit_in;
    static Score fitted[ACT_COUNT], profiles[ACT_COUNT];
    memset(fitted, 0, sizeof(fitted));
    memset(profiles, 0, sizeof(profiles));
    Sim* sim = malloc(sizeof(Sim));
    sim_init(sim);
    OppModel model = {MODEL_PROFILES, NULL};
    OursPolicy replay = {OURS_REPLAY, 0};
    for (int i = 0; i < count; i++) {
        const Tape* tape = &tapes[i];
        if (tape_day(tape) != test_day) continue;
        for (int t = tape->lo + 1; t <= tape->hi; t++) {
            int flags = tape->his_flags[t - tape->lo];
            Probe probes[SCORE_SAMPLES];
            for (int j = 0; j < SCORE_SAMPLES; j++) load_hidden(sim, tape, t, j, &probes[j]);
            int eat = 0, swing_no_eat = 0, no_eat = 0, swing = 0, vw = 0, maul = 0, veng = 0;
            for (int j = 0; j < PROFILE_SAMPLES; j++) {
                uint32_t rng = osrs_lowbias32((uint32_t)(t * 31 + j * 104729 + tape->index)) | 1;
                RiskfightSnapshot snap;
                tape_snapshot(tape, t - 1, &DEFAULT_PRIORS, &rng, &snap);
                riskfight_load_state(&sim->state, &sim->context, &snap);
                Outcome out;
                run_steps(sim, tape, t - 1, 1, &model, &replay, xorshift32(&rng), &out);
                int swung = out.his_hit_mask & 1;
                eat += out.his_eat[0]; no_eat += !out.his_eat[0]; swing_no_eat += swung && !out.his_eat[0]; swing += swung;
                vw += out.his_vw_specs; maul += out.his_maul_specs > 0; veng += out.his_vengs;
            }
            double q[ACT_COUNT] = {(eat + 0.5) / (PROFILE_SAMPLES + 1), (eat + 0.5) / (PROFILE_SAMPLES + 1), (swing_no_eat + 0.5) / (no_eat + 1),
                (vw + 0.5) / (swing + 1), (maul + 0.5) / (PROFILE_SAMPLES + 1), (veng + 0.5) / (PROFILE_SAMPLES + 1)};
            for (int a = 0; a < ACT_COUNT; a++) {
                Conditioned c = conditioned((Action)a, &probes[0], flags);
                if (!c.eligible) continue;
                double p = 0;
                for (int j = 0; j < SCORE_SAMPLES; j++) p += action_probability(&fit, &probes[j], (Action)a);
                score_add(&fitted[a], p / SCORE_SAMPLES, c.y);
                score_add(&profiles[a], q[a], c.y);
            }
        }
    }
    printf("test day %d, per action (const = the train-day rate over eligible ticks, profiles = the existing mixture sampled 64 times)\n", test_day);
    for (int a = 0; a < ACT_COUNT; a++)
        print_score(ACTION_NAME[a], &fitted[a], &profiles[a], (double)fit.logit[a].events / fit.logit[a].opportunities);
    free(sim);
}

static void teacher(Tape* tapes, int count, int train_day, int test_day) {
    static OppFit fit;
    memset(&fit, 0, sizeof(fit));
    fit_default_servings(&fit);
    fit_model(tapes, count, train_day, &fit);
    int eats, identified[2];
    infer_servings(tapes, count, train_day, &fit, &eats, identified);
    fit_override(&fit);
    char title[128];
    snprintf(title, sizeof(title), "=== teacher forcing: fit on day %d, score on day %d", train_day, test_day);
    print_fit(title, &fit, eats, identified);
    teacher_score(tapes, count, &fit, test_day);
}

/* ---------- k-tick rollouts ---------- */

typedef struct {
    int eats, his_hits, his_damage, vw_specs, maul_specs, vengs, our_dead, his_dead, our_hits, our_damage, bar_end, our_meals;
} Recorded;

static Recorded recorded_window(const Tape* tape, int t0, int k) {
    Recorded r = {0};
    for (int x = t0 + 1; x <= t0 + k; x++) {
        r.eats += flag_at(tape, x, FLAG_EAT);
        for (int i = 0; i < tape->action_count; i++)
            if (tape->actions[i].tick == x && tape->actions[i].kind == ACTION_CLICK && is_consumable_raw(tape->actions[i].raw_id)) { r.our_meals++; break; }
        r.vw_specs += flag_at(tape, x, FLAG_VW);
        r.maul_specs += flag_at(tape, x, FLAG_MAUL);
        r.vengs += flag_at(tape, x, FLAG_VENG);
        r.our_dead |= tape_tick(tape, x)->hp <= 0;
        r.his_dead |= tape_tick(tape, x)->bar_ratio == 0;
    }
    r.bar_end = tape_tick(tape, t0 + k)->bar_ratio;
    for (int i = 0; i < tape->hit_count; i++) {
        const TapeHit* h = &tape->hits[i];
        if (h->tick <= t0 || h->tick > t0 + k) continue;
        int attack = h->klass == 'A' || h->klass == 'B';
        if (h->side == SIDE_US) { r.his_hits += attack; r.his_damage += h->klass == 'A' ? h->amount : 0; }
        else { r.our_hits += attack; r.our_damage += h->klass == 'A' ? h->amount : 0; }
    }
    return r;
}

typedef struct {
    double rec[METRICS], sim[METRICS];
    int pit[METRICS][PIT_BINS];
    int windows;
    double seconds, decisions, ticks, rollouts, spec_counted, spec_predicted, spec_realized;
} WindowStats;

static const char* METRIC[METRICS] = {"his meals", "his hits", "his damage", "his VW specs", "his maul specs", "his vengeance", "we die", "he dies", "our hits", "our damage", "his bar at end", "our meals"};

static double pit_value(const int* sim, int n, int observed, uint32_t* rng) {
    int below = 0, equal = 0;
    for (int i = 0; i < n; i++) { below += sim[i] < observed; equal += sim[i] == observed; }
    return ((double)below + uniform(rng) * equal) / n;
}

static FILE* g_deaths;
static FILE* g_windows;

static void evaluate_windows(const Tape* tape, const OppModel* model, const OursPolicy* ours, int n, int k, WindowStats* stats) {
    static int sim_values[METRICS][MAX_ROLLOUTS];
    g_tape = tape;
    Sim* sim = malloc(sizeof(Sim));
    sim_init(sim);
    uint32_t pit_rng = 2463534242u;
    double started = seconds();
    for (int t0 = tape->lo + 1; t0 + k <= tape->hi; t0++) {
        Recorded rec = recorded_window(tape, t0, k);
        double sums[METRICS] = {0};
        for (int i = 0; i < n; i++) {
            const char* trace = getenv("GS_TRACE");
            g_trace = 0;
            if (trace) {
                char want[64];
                int at;
                sscanf(trace, "%63[^:]:%d", want, &at);
                g_trace = !strcmp(want, tape->name) && at == t0;
                if (g_trace) printf("rollout %d tape %s t0 %d recorded damage on him %d eats %d bar %d/%d\n", i, tape->name, t0, rec.our_damage, rec.eats, tape_tick(tape, t0)->bar_ratio, tape_tick(tape, t0)->bar_scale);
            }
            uint32_t rng = osrs_lowbias32((uint32_t)(t0 * 7919 + i * 104729 + tape->index * 13 + tape->lo)) | 1;
            RiskfightSnapshot snap;
            tape_snapshot(tape, t0, &DEFAULT_PRIORS, &rng, &snap);
            riskfight_load_state(&sim->state, &sim->context, &snap);
            Outcome out;
            long decisions = sim->ours.decisions, ticks = sim->ours.ticks;
            run_steps(sim, tape, t0, k, model, ours, xorshift32(&rng), &out);
            stats->decisions += sim->ours.decisions - decisions;
            stats->ticks += sim->ours.ticks - ticks;
            stats->rollouts++;
            int eats = 0;
            for (int j = 0; j < out.steps; j++) eats += out.his_eat[j];
            int values[METRICS] = {eats, out.his_hits, out.his_damage, out.his_vw_specs, out.his_maul_specs, out.his_vengs,
                out.our_death >= 0, out.his_death >= 0, out.our_hits, out.our_damage, out.bar_him, out.our_meals};
            for (int m = 0; m < METRICS; m++) { sim_values[m][i] = values[m]; sums[m] += values[m]; }
        }
        int rec_values[METRICS] = {rec.eats, rec.his_hits, rec.his_damage, rec.vw_specs, rec.maul_specs, rec.vengs, rec.our_dead, rec.his_dead,
            rec.our_hits, rec.our_damage, rec.bar_end, rec.our_meals};
        if (g_windows)
            fprintf(g_windows, "%s,%d,%d,%d,%d,%d,%d,%d,%.4f,%.3f,%.3f,%.1f,%.1f,%.1f\n", tape->name, t0, tape_tick(tape, t0)->bar_ratio, tape_tick(tape, t0)->bar_scale, rec.eats, rec.our_damage,
                rec.bar_end, rec.his_dead, sums[7] / n, sums[0] / n, sums[9] / n, sums[10] / n, sums[2] / n, sums[1] / n);
        if (g_deaths && sums[7] > 0)
            fprintf(g_deaths, "%s t0 %d bar %d/%d escape %d died %.3f recorded_eats %d recorded_damage_on_him %d\n", tape->name, t0, tape_tick(tape, t0)->bar_ratio,
                tape_tick(tape, t0)->bar_scale, tape_escape_tick(tape) > t0 && tape_escape_tick(tape) <= t0 + k, sums[7] / n, rec.eats, rec.our_damage);
        for (int m = 0; m < METRICS; m++) {
            stats->rec[m] += rec_values[m];
            stats->sim[m] += sums[m] / n;
            if (m < 6 || m >= 8) {
                double u = pit_value(sim_values[m], n, rec_values[m], &pit_rng);
                stats->pit[m][u >= 1 ? PIT_BINS - 1 : (int)(u * PIT_BINS)]++;
            }
        }
        stats->windows++;
    }
    stats->seconds += seconds() - started;
    stats->spec_counted += sim->spec_counted;
    stats->spec_predicted += sim->spec_predicted;
    stats->spec_realized += sim->spec_realized;
    free(sim);
}

static void print_window_stats(const char* title, const WindowStats* s) {
    if (s->ticks > 0) printf("%s: %.0f rollouts in %.2f s = %.0f rollouts/s with decide in the loop, %.3f decisions per tick (%.0f of %.0f ticks); played spec decisions %.0f, predicted kills %.1f, realized %.1f\n", title, s->rollouts, s->seconds, s->rollouts / s->seconds, s->decisions / s->ticks, s->decisions, s->ticks, s->spec_counted, s->spec_predicted, s->spec_realized);
    printf("%s: %d windows\n  %-16s %10s %10s %7s   PIT histogram (10 bins, flat means calibrated)\n", title, s->windows, "metric", "recorded", "sim mean", "ratio");
    for (int m = 0; m < METRICS; m++) {
        printf("  %-16s %10.0f %10.1f %7.2f  ", METRIC[m], s->rec[m], s->sim[m], s->rec[m] > 0 ? s->sim[m] / s->rec[m] : 0.0);
        if (m < 6 || m >= 8) for (int b = 0; b < PIT_BINS; b++) printf("%5d", s->pit[m][b]);
        printf("\n");
    }
}

typedef enum { TRIGGER_WARNING, TRIGGER_BIG_HIT } Trigger;
static const double FIRST_MEAL[EPISODE_BINS] = {0.01, 0.21, 0.49, 0.18, 0.03, 0.05, 0.01, 0.01, 0.01};

typedef struct {
    int compared;
    int recorded[EPISODE_BINS];
    double sim[3][EPISODE_BINS];
} EpisodeStats;

static int damage_on_him(const Tape* tape, int tick) {
    int total = 0;
    for (int i = 0; i < tape->hit_count; i++)
        if (tape->hits[i].tick == tick && tape->hits[i].side == SIDE_HIM && tape->hits[i].klass == 'A') total += tape->hits[i].amount;
    return total;
}

/* Anchor tick u: the Java warning tick, or the tick of our single hit of BIG_HIT_EPISODE or more. Rollouts start at the tick x
 * the anchor is known and score the first meal by ticks after u. */
static void evaluate_episodes(const Tape* tape, Trigger trigger, const OppModel* models, const OursPolicy* ours, int n, EpisodeStats* stats) {
    Sim* sim = malloc(sizeof(Sim));
    sim_init(sim);
    const Hist* series = tape->hist_series;
    int last_big = -100;
    for (int x = tape->lo + 1; x + 8 <= tape->hi; x++) {
        int u;
        if (trigger == TRIGGER_WARNING) {
            int now = warning_since(&series[x - tape->lo].warning), before = warning_since(&series[x - 1 - tape->lo].warning);
            if (now == WARNING_NONE || before != WARNING_NONE) continue;
            u = now;
        } else {
            if (damage_on_him(tape, x) < BIG_HIT_EPISODE || x - last_big < REARM_TICKS) continue;
            u = x;
            last_big = x;
        }
        int e = x + 1;
        while (e <= x + 8 && !rec_eat_at(tape, e)) e++;
        int d = e - u;
        stats->compared++;
        stats->recorded[e > x + 8 || d > 7 ? 8 : d]++;
        for (int m = 0; m < 3; m++)
            for (int i = 0; i < n; i++) {
                uint32_t rng = osrs_lowbias32((uint32_t)(x * 7919 + i * 104729 + tape->index * 13)) | 1;
                RiskfightSnapshot snap;
                tape_snapshot(tape, x, &DEFAULT_PRIORS, &rng, &snap);
                riskfight_load_state(&sim->state, &sim->context, &snap);
                Outcome out;
                run_steps(sim, tape, x, 8, &models[m], ours, xorshift32(&rng), &out);
                int delay = out.first_eat < 0 ? 8 : x + 1 + out.first_eat - u;
                stats->sim[m][delay > 7 ? 8 : delay] += 1.0 / n;
            }
    }
    free(sim);
}

static void print_episodes(const char* title, const EpisodeStats* s, const char* names[3]) {
    printf("%s: %d episodes\n  %-14s", title, s->compared, "ticks after u");
    for (int d = 0; d < EPISODE_BINS; d++) printf("%8s", d == 8 ? "none" : (char[]){(char)('0' + d), 0});
    printf("\n  %-14s", "recorded");
    for (int d = 0; d < EPISODE_BINS; d++) printf("%8.3f", (double)s->recorded[d] / s->compared);
    double tv[4] = {0};
    for (int m = 0; m < 3; m++) {
        printf("\n  %-14s", names[m]);
        for (int d = 0; d < EPISODE_BINS; d++) {
            printf("%8.3f", s->sim[m][d] / s->compared);
            tv[m] += fabs((double)s->recorded[d] / s->compared - s->sim[m][d] / s->compared) / 2;
        }
    }
    printf("\n  %-14s", "FIRST_MEAL");
    for (int d = 0; d < EPISODE_BINS; d++) {
        printf("%8.3f", FIRST_MEAL[d]);
        tv[3] += fabs((double)s->recorded[d] / s->compared - FIRST_MEAL[d]) / 2;
    }
    printf("\n  total variation to recorded: %s %.3f, %s %.3f, %s %.3f, FIRST_MEAL %.3f\n", names[0], tv[0], names[1], tv[1], names[2], tv[2], tv[3]);
}

static void rollouts(int n, int k, Tape* tapes, int count, int train_day, int test_day) {
    static OppFit fit_big, fit_warning;
    memset(&fit_big, 0, sizeof(fit_big));
    fit_default_servings(&fit_big);
    fit_model(tapes, count, train_day, &fit_big);
    int eats, identified[2];
    infer_servings(tapes, count, train_day, &fit_big, &eats, identified);
    fit_override(&fit_big);
    fit_big.eat_action = ACT_EAT_BIG;
    fit_warning = fit_big;
    fit_warning.eat_action = ACT_EAT_WARNING;
    OppModel models[3] = {{MODEL_PROFILES, NULL}, {MODEL_FIT, &fit_big}, {MODEL_FIT, &fit_warning}};
    const char* names[3] = {"profiles", "fit big-hit", "fit warning"};
    OursPolicy proxy = {OURS_EAT_AT_RISK, MARGIN}, replay = {OURS_REPLAY, 0}, rules = {OURS_RULES, 0};
    printf("=== rollouts: fit on day %d, test on day %d, N=%d, k=%d ===\n", train_day, test_day, n, k);
    static WindowStats stats[5];
    static EpisodeStats episodes[2];
    memset(stats, 0, sizeof(stats));
    memset(episodes, 0, sizeof(episodes));
    const OppModel* chosen[5] = {&models[0], &models[2], &models[0], &models[1], &models[2]};
    const OursPolicy* sides[5] = {&replay, &proxy, &rules, &rules, &rules};
    const char* titles[5] = {"profiles, our recorded actions replayed (open loop)", "fit warning, our side eats at risk (proxy)", "profiles, our side on the ported rules",
        "fit big-hit, our side on the ported rules", "fit warning, our side on the ported rules"};
    for (int i = 0; i < count; i++) {
        if (tape_day(&tapes[i]) != test_day) continue;
        for (int c = 0; c < 5; c++) evaluate_windows(&tapes[i], chosen[c], sides[c], n, k, &stats[c]);
        for (int trigger = 0; trigger < 2; trigger++) evaluate_episodes(&tapes[i], (Trigger)trigger, models, &rules, n, &episodes[trigger]);
    }
    for (int c = 0; c < 5; c++) print_window_stats(titles[c], &stats[c]);
    print_episodes("first meal after the Java warning (ported rules)", &episodes[0], names);
    print_episodes("first meal after our single hit of 30 or more (ported rules)", &episodes[1], names);
}

/* Where the planner's predicted kills go: the played spec decisions of the ported rules against what the sim opponent then did, with one term changed at a time. */
static void eat_model_setup(Tape* tapes, int count);

static void autopsy(int n, int k, Tape* tapes, int count, int test_day) {
    static OppFit fit;
    memset(&fit, 0, sizeof(fit));
    fit_default_servings(&fit);
    if (getenv("GS_TRAIN")) {
        int train = atoi(getenv("GS_TRAIN")), eats, identified[2];
        fit_model(tapes, count, train, &fit);
        infer_servings(tapes, count, train, &fit, &eats, identified);
        print_fit("fit on own tapes", &fit, eats, identified);
    }
    fit_override(&fit);
    fit.eat_action = ACT_EAT_WARNING;
    eat_model_setup(tapes, count);
    OppModel model = {MODEL_FIT, &fit};
    OursPolicy replay = {OURS_REPLAY, 0}, rules = {OURS_RULES, 0};
    static WindowStats open, closed;
    memset(&open, 0, sizeof(open));
    memset(&closed, 0, sizeof(closed));
    const char* log = getenv("GS_DEATHS");
    g_deaths = log ? fopen(log, "w") : NULL;
    g_windows = getenv("GS_WINDOWS") ? fopen(getenv("GS_WINDOWS"), "w") : NULL;
    for (int i = 0; i < count; i++) {
        if (!in_day(&tapes[i], test_day)) continue;
        evaluate_windows(&tapes[i], &model, &replay, n, k, &open);
        if (!getenv("GS_OPEN_ONLY")) evaluate_windows(&tapes[i], &model, &rules, n, k, &closed);
    }
    printf("=== autopsy N=%d k=%d day %d ablation hp %d eats %d escape %d pid %d damage %d ===\n", n, k, test_day, g_ablation.hp, g_ablation.eats, g_ablation.escape, g_ablation.pid, g_ablation.damage);
    print_window_stats("open loop, fit warning", &open);
    if (!getenv("GS_OPEN_ONLY")) print_window_stats("closed loop, fit warning", &closed);
    printf("his escapes: sim %.1f per 1000 rollouts\n", 1000.0 * g_sim_escapes / g_sim_rollouts);
    if (g_deaths) fclose(g_deaths);
    if (g_windows) fclose(g_windows);
}


/* ---------- eat model study: candidate hazards for his meals, fitted on one set of days and scored on the others ---------- */

enum { EAT_MODELS = 6 };
static void eat_fit(EatLogit* l, int model, const EatRows* rows) {
    memset(l, 0, sizeof(*l));
    double ridge = 0.5;
    for (int iteration = 0; iteration < 40; iteration++) {
        static double g[EAT_DIM], h[EAT_DIM][EAT_DIM];
        memset(g, 0, sizeof(g));
        memset(h, 0, sizeof(h));
        for (int d = 1; d < EAT_DIM; d++) { g[d] -= ridge * l->w[d]; h[d][d] += ridge; }
        for (int i = 0; i < rows->count; i++) {
            const EatRow* r = &rows->rows[i];
            int idx[16];
            int n = eat_features_of(r, model, idx);
            double p = eat_p(l, r, model), rr = r->weight * (r->y - p), v = r->weight * p * (1 - p);
            for (int a = 0; a < n; a++) {
                g[idx[a]] += rr;
                for (int b = 0; b < n; b++) h[idx[a]][idx[b]] += v;
            }
        }
        static double m[EAT_DIM][EAT_DIM + 1];
        for (int a = 0; a < EAT_DIM; a++) { for (int b = 0; b < EAT_DIM; b++) m[a][b] = h[a][b] + (a == b ? 1e-6 : 0); m[a][EAT_DIM] = g[a]; }
        for (int c = 0; c < EAT_DIM; c++) {
            int pivot = c;
            for (int r = c + 1; r < EAT_DIM; r++) if (fabs(m[r][c]) > fabs(m[pivot][c])) pivot = r;
            for (int k = 0; k <= EAT_DIM; k++) { double t = m[c][k]; m[c][k] = m[pivot][k]; m[pivot][k] = t; }
            for (int r = 0; r < EAT_DIM; r++) {
                if (r == c) continue;
                double f = m[r][c] / m[c][c];
                for (int k = c; k <= EAT_DIM; k++) m[r][k] -= f * m[c][k];
            }
        }
        for (int a = 0; a < EAT_DIM; a++) l->w[a] += m[a][EAT_DIM] / m[a][a];
    }
}

/* The planner's structure: he eats with chance c while under a level he wants (uniform 44 to 87), more on the tick before our hit. */
static double appetite_p(double c, double anticipate, const EatRow* r) {
    double wants = fmax(0, fmin(1, (87 - r->hp) / 43.0));
    double p = c * wants + (r->timer == 1 ? anticipate * wants : 0);
    return p;
}

static double eat_ll(double p, const EatRow* r) { double c = clip(p); return r->weight * (r->y ? log(c) : log(1 - c)); }

static void eat_rows_add(EatRows* rows, Tape* tapes, int count, int day, Sim* sim) {
    for (int i = 0; i < count; i++) {
        if (!in_day(&tapes[i], day)) continue;
        g_tape = &tapes[i];
        for (int t = tapes[i].lo + 1; t <= tapes[i].hi; t++) {
            int flags = tapes[i].his_flags[t - tapes[i].lo];
            for (int j = 0; j < FIT_SAMPLES; j++) {
                Probe probe;
                load_hidden(sim, &tapes[i], t, j, &probe);
                if (!probe.eligible[ACT_EAT_WARNING]) continue;
                if (rows->count == rows->capacity) { rows->capacity = rows->capacity ? rows->capacity * 2 : 8192; rows->rows = realloc(rows->rows, rows->capacity * sizeof(EatRow)); }
                EatRow* r = &rows->rows[rows->count++];
                r->base_n = probe.x[ACT_EAT_WARNING].n;
                for (int k = 0; k < r->base_n; k++) r->base[k] = probe.x[ACT_EAT_WARNING].idx[k];
                r->hp = sim->state.env.players[1].current_hitpoints;
                r->timer = sim->state.env.players[0].attack_timer;
                r->y = (flags & FLAG_EAT) != 0;
                r->masked = 0;
                for (int k = 0; k < tapes[i].anim_count; k++)
                    r->masked |= tapes[i].anims[k].tick == t && tapes[i].anims[k].side == SIDE_HIM && tapes[i].anims[k].anim != ANIM_EAT;
                for (int k = 0; k < tapes[i].hit_count; k++) r->masked |= tapes[i].hits[k].tick == t && tapes[i].hits[k].side == SIDE_HIM;
                r->weight = 1.0 / FIT_SAMPLES;
            }
        }
    }
}


/* His meals are drawn from a hazard fitted on the ticks his eat animation could show (not hidden behind another animation), the current logit plus his hitpoints bands:
 * fixtures/eat_unmasked.fit by default, GS_EAT_MODEL=m refits model m (2 adds our swing timer, 3 the low hp interaction) on the days GS_EAT_TRAIN names (minus a day for all others),
 * GS_EAT_LOAD loads a saved fit, GS_EAT_SAVE writes it, GS_EAT_MODEL=off keeps the logit of the spectator fit. */

/* His meals are drawn from a hazard fitted on the ticks his eat animation could show (not hidden behind another animation), the current logit plus his hitpoints bands:
 * fixtures/eat_unmasked.fit by default, GS_EAT_MODEL=m refits model m (2 adds our swing timer, 3 the low hp interaction) on the days GS_EAT_TRAIN names (minus a day for all others),
 * GS_EAT_LOAD loads a saved fit, GS_EAT_SAVE writes it, GS_EAT_MODEL=off keeps the logit of the spectator fit. */

/* His meals are drawn from a hazard fitted on the ticks his eat animation could show (not hidden behind another animation), the current logit plus his hitpoints bands:
 * fixtures/eat_unmasked.fit by default, GS_EAT_MODEL=m refits model m (2 adds our swing timer, 3 the low hp interaction) on the days GS_EAT_TRAIN names (minus a day for all others),
 * GS_EAT_LOAD loads a saved fit, GS_EAT_SAVE writes it, GS_EAT_MODEL=off keeps the logit of the spectator fit. */
static void eat_model_setup(Tape* tapes, int count) {
    const char* model = getenv("GS_EAT_MODEL");
    if (model && !strcmp(model, "off")) return;
    g_eat_model.on = 1;
    g_eat_model.model = model ? atoi(model) : 1;
    const char* load = getenv("GS_EAT_LOAD");
    if (!model && !load) load = "ocean/osrs/tests/fixtures/eat_unmasked.fit";
    if (load) {
        FILE* f = fopen(load, "rb");
        assert(f && fread(&g_eat_model.logit, sizeof(g_eat_model.logit), 1, f) == 1);
        fclose(f);
        return;
    }
    Sim* sim = malloc(sizeof(Sim));
    sim_init(sim);
    static EatRows rows;
    rows.count = 0;
    eat_rows_add(&rows, tapes, count, atoi(getenv("GS_EAT_TRAIN")), sim);
    int kept = 0;
    for (int i = 0; i < rows.count; i++) if (!rows.rows[i].masked) rows.rows[kept++] = rows.rows[i];
    rows.count = kept;
    eat_fit(&g_eat_model.logit, g_eat_model.model, &rows);
    free(sim);
    const char* save = getenv("GS_EAT_SAVE");
    if (save) {
        FILE* f = fopen(save, "wb");
        fwrite(&g_eat_model.logit, sizeof(g_eat_model.logit), 1, f);
        fclose(f);
    }
}

static void eat_study(Tape* tapes, int count, int train_day, int test_day) {
    Sim* sim = malloc(sizeof(Sim));
    sim_init(sim);
    static EatRows train, test;
    train.count = test.count = 0;
    eat_rows_add(&train, tapes, count, train_day, sim);
    eat_rows_add(&test, tapes, count, test_day, sim);
    for (EatRows* rows = &train; rows; rows = rows == &train ? &test : NULL) {
        int kept = 0;
        for (int i = 0; i < rows->count; i++) if (!rows->rows[i].masked) rows->rows[kept++] = rows->rows[i];
        rows->count = kept;
    }
    double events_train = 0, events_test = 0;
    for (int i = 0; i < train.count; i++) events_train += train.rows[i].y * train.rows[i].weight;
    for (int i = 0; i < test.count; i++) events_test += test.rows[i].y * test.rows[i].weight;
    printf("=== eat study on unmasked ticks: fit on day %d (%.0f ticks, %.0f eats), score on day %d (%.0f ticks, %.0f eats) ===\n", train_day, train.count / (double)FIT_SAMPLES, events_train,
        test_day, test.count / (double)FIT_SAMPLES, events_test);
    static const char* NAMES[4] = {"current logit (bar band, lag, warning)", "+ his hp bands", "+ our swing timer", "+ low hp x timer"};
    double rate = events_train / (train.count / (double)FIT_SAMPLES), constant = 0;
    for (int i = 0; i < test.count; i++) constant += eat_ll(rate, &test.rows[i]);
    printf("  %-44s test log likelihood %9.2f (rate %.4f)\n", "constant", constant, rate);
    for (int m = 0; m < 4; m++) {
        static EatLogit l;
        eat_fit(&l, m, &train);
        double ll_train = 0, ll_test = 0;
        for (int i = 0; i < train.count; i++) ll_train += eat_ll(eat_p(&l, &train.rows[i], m), &train.rows[i]);
        for (int i = 0; i < test.count; i++) ll_test += eat_ll(eat_p(&l, &test.rows[i], m), &test.rows[i]);
        printf("  %-44s train %9.2f test %9.2f\n", NAMES[m], ll_train, ll_test);
    }
    for (int anticipation = 0; anticipation < 2; anticipation++) {
        double best = -1e300, bc = 0, ba = 0, bl = 0, bh = 0;
        for (double c = 0.1; c <= 1.0; c += 0.05)
            for (int low = 20; low <= 80; low += 10)
                for (int high = low + 10; high <= 120; high += 10)
                    for (double a = 0; a <= (anticipation ? 1.0 : 0.0); a += 0.1) {
                        double ll = 0;
                        for (int i = 0; i < train.count; i++) {
                            const EatRow* r = &train.rows[i];
                            double wants = fmax(0, fmin(1, (high - r->hp) / (double)(high - low)));
                            ll += eat_ll(c * wants + (r->timer == 1 ? a * wants : 0), r);
                        }
                        if (ll > best) { best = ll; bc = c; ba = a; bl = low; bh = high; }
                    }
        double ll_test = 0;
        for (int i = 0; i < test.count; i++) {
            const EatRow* r = &test.rows[i];
            double wants = fmax(0, fmin(1, (bh - r->hp) / (bh - bl)));
            ll_test += eat_ll(bc * wants + (r->timer == 1 ? ba * wants : 0), r);
        }
        printf("  %-44s train %9.2f test %9.2f (c %.2f anticipation %.1f levels %.0f-%.0f)\n", anticipation ? "appetite c*wants(hp) + anticipation" : "appetite c*wants(hp)", best, ll_test, bc, ba, bl, bh);
    }
    {
        static const int EDGES[6] = {0, 31, 45, 61, 88, 200};
        printf("  unmasked eat rate by his hp (train + test): band, ticks, rate, planner 0.481 * wants at the band middle\n");
        for (int b = 0; b < 5; b++) {
            double n = 0, e = 0;
            for (int pass = 0; pass < 2; pass++) {
                const EatRows* rows = pass ? &test : &train;
                for (int i = 0; i < rows->count; i++) if (rows->rows[i].hp >= EDGES[b] && rows->rows[i].hp < EDGES[b + 1]) { n += rows->rows[i].weight; e += rows->rows[i].y * rows->rows[i].weight; }
            }
            double middle = b == 4 ? 100 : (EDGES[b] + EDGES[b + 1]) / 2.0;
            printf("    hp %3d-%3d  %6.0f ticks  rate %.3f  planner %.3f\n", EDGES[b], EDGES[b + 1] - 1, n, n > 0 ? e / n : 0, 0.481 * fmax(0, fmin(1, (87 - middle) / 43.0)));
        }
    }
    {
        double ll_test = 0;
        for (int i = 0; i < test.count; i++) {
            const EatRow* r = &test.rows[i];
            double wants = fmax(0, fmin(1, (87 - r->hp) / 43.0));
            ll_test += eat_ll(0.481 * wants + (r->timer == 1 ? 0.066 * wants : 0), r);
        }
        printf("  %-44s test %9.2f\n", "planner constants (0.481, 44-87, 0.066)", ll_test);
    }
    free(sim);
}

static void attribute(int n, int k, Tape* tapes, int count, int train_day, int test_day) {
    static OppFit fit, heavy, eager, slow;
    memset(&fit, 0, sizeof(fit));
    fit_default_servings(&fit);
    fit_model(tapes, count, train_day, &fit);
    int eats, identified[2];
    infer_servings(tapes, count, train_day, &fit, &eats, identified);
    fit_override(&fit);
    fit.eat_action = ACT_EAT_WARNING;
    heavy = fit;
    for (int w = 0; w < 2; w++) { heavy.serving[w][0] = heavy.serving[w][1] = heavy.serving[w][2] = 0; heavy.serving[w][3] = 1; }
    eager = fit;
    eager.logit[ACT_EAT_WARNING].w[0] += 0.7;
    slow = fit;
    slow.logit[ACT_EAT_WARNING].w[0] -= 0.7;
    OppModel models[4] = {{MODEL_FIT, &fit}, {MODEL_FIT, &heavy}, {MODEL_FIT, &eager}, {MODEL_FIT, &slow}};
    const char* names[4] = {"fitted opponent", "every meal a triple", "eats 2x as often", "eats half as often"};
    OursPolicy real = {OURS_RULES, 0, TIMING_MEASURED, STAKES_COMPUTED};
    OursPolicy steps[4] = {{OURS_RULES, 0, TIMING_IDEAL, STAKES_EQUAL}, {OURS_RULES, 0, TIMING_IDEAL, STAKES_COMPUTED}, {OURS_RULES, 0, TIMING_MEASURED, STAKES_EQUAL}, real};
    const char* step_names[4] = {"inferred, ideal clicks, equal stakes", "+ stakes computed", "+ measured clicks, equal stakes", "inferred, measured clicks, stakes"};
    printf("=== attribution: fit on day %d, test on day %d, N=%d, k=%d; played spec decisions, planner chance against kills within %d ticks ===\n", train_day, test_day, n, k, OURS_HORIZON);
    for (int variant = 0; variant < 8; variant++) {
        const OppModel* model = variant < 4 ? &models[0] : &models[variant - 4];
        const OursPolicy* side = variant < 4 ? &steps[variant] : &real;
        WindowStats stats = {{0}};
        for (int i = 0; i < count; i++) if (tape_day(&tapes[i]) == test_day) evaluate_windows(&tapes[i], model, side, n, k, &stats);
        printf("  %-38s played %5.0f  predicted kills %7.1f  realized %7.1f  realized/predicted %.2f   he dies %.1f we die %.1f\n",
            variant < 4 ? step_names[variant] : names[variant - 4], stats.spec_counted, stats.spec_predicted, stats.spec_realized, stats.spec_realized / stats.spec_predicted, stats.sim[7], stats.sim[6]);
        fflush(stdout);
    }
}

/* Which belief drives the planner's overconfidence: the ported rules with one belief about him read from the sim at a time. */
static void ablate(int n, int k, Tape* tapes, int count, int train_day, int test_day) {
    static OppFit fit;
    memset(&fit, 0, sizeof(fit));
    fit_default_servings(&fit);
    fit_model(tapes, count, train_day, &fit);
    int eats, identified[2];
    infer_servings(tapes, count, train_day, &fit, &eats, identified);
    fit_override(&fit);
    fit.eat_action = ACT_EAT_WARNING;
    eat_model_setup(tapes, count);
    OppModel model = {MODEL_FIT, &fit};
    static const struct { const char* name; int truth; } variants[] = {
        {"all inferred", TRUTH_NONE}, {"PID from the sim", TRUTH_PID}, {"his spec energy from the sim", TRUTH_ENERGY}, {"his Vengeance from the sim", TRUTH_VENGEANCE},
        {"his HP from the sim", TRUTH_HP}, {"his attack timer from the sim", TRUTH_TIMER}, {"all beliefs from the sim", TRUTH_ALL}};
    printf("=== ablation: fit on day %d, test on day %d, N=%d, k=%d; measured clicks and stakes ===\n", train_day, test_day, n, k);
    for (size_t v = 0; v < sizeof(variants) / sizeof(*variants); v++) {
        OursPolicy side = {OURS_RULES, 0, TIMING_MEASURED, STAKES_COMPUTED, variants[v].truth};
        WindowStats stats = {{0}};
        for (int i = 0; i < count; i++) if (tape_day(&tapes[i]) == test_day) evaluate_windows(&tapes[i], &model, &side, n, k, &stats);
        printf("  %-32s played %5.0f  predicted kills %7.1f  realized %7.1f  realized/predicted %.2f   he dies %.1f we die %.1f\n", variants[v].name,
            stats.spec_counted, stats.spec_predicted, stats.spec_realized, stats.spec_realized / stats.spec_predicted, stats.sim[7], stats.sim[6]);
        fflush(stdout);
    }
}

static void branch(int n, int horizon, Tape* tapes, int count) {
    OppModel model = {MODEL_PROFILES, NULL};
    OursPolicy policies[4] = {{OURS_REPLAY, 0}, {OURS_EAT_AT_THRESHOLD, 50}, {OURS_EAT_AT_RISK, MARGIN}, {OURS_RULES, 0}};
    const char* names[4] = {"replay recorded", "eat below 50", "eat at risk", "ported rules"};
    Sim* sim = malloc(sizeof(Sim));
    sim_init(sim);
    for (int i = 0; i < count; i++) {
        Tape* tape = &tapes[i];
        int t0 = tape->lo + (tape->hi - tape->lo) / 2;
        uint32_t rng = 99;
        RiskfightSnapshot snap;
        tape_snapshot(tape, t0, &DEFAULT_PRIORS, &rng, &snap);
        riskfight_load_state(&sim->state, &sim->context, &snap);
        RiskfightState base = sim->state;
        printf("%s tick %d: our hp %d  stakes r %.1f (us %ld gp, him %ld gp)\n", tape->name, t0, base.env.players[0].current_hitpoints,
            tape->observer_series ? gs_stakes_ratio(((const Observer*)tape->observer_series)[t0 - tape->lo].stakes) : 0.0,
            tape->observer_series ? ((const Observer*)tape->observer_series)[t0 - tape->lo].stakes.us : 0L, tape->observer_series ? ((const Observer*)tape->observer_series)[t0 - tape->lo].stakes.him : 0L);
        for (int p = 0; p < 4; p++) {
            int die = 0, kill = 0;
            double start = seconds();
            for (int r = 0; r < n; r++) {
                sim->state = base;
                sim->state.env.rng_state = (uint32_t)(r * 2654435761u + 1) | 1;
                for (int side = 0; side < 2; side++) sim->state.script_seed[side] = osrs_lowbias32(sim->state.env.rng_state + side);
                Outcome out;
                run_steps(sim, tape, t0, horizon, &model, &policies[p], sim->state.env.rng_state, &out);
                die += out.our_death >= 0;
                kill += out.his_death >= 0;
            }
            double elapsed = seconds() - start;
            printf("  %-16s P(we die) %.3f  P(kill) %.3f  %d rollouts x %d ticks in %.3f s = %.0f rollouts/s\n",
                names[p], (double)die / n, (double)kill / n, n, horizon, elapsed, n / elapsed);
        }
    }
    free(sim);
}

/* ---------- the opponent fitted on spectated duels ---------- */

static SpecCorpus corpus;
static SpecTick* spec_ticks[2048];

static void spec_prepare(void) {
    assert(corpus.count <= 2048);
    for (int i = 0; i < corpus.count; i++) {
        spec_ticks[i] = malloc((size_t)corpus.duels[i].row_count * 2 * sizeof(SpecTick));
        for (int side = 0; side < 2; side++) spec_series(&corpus.duels[i], side, spec_ticks[i] + side * corpus.duels[i].row_count);
    }
}

/* the hazards of one fighter's ticks, one sample per tick for each action he could take; skip: the session held out */
static int spec_dharok = -1;

static void spec_samples(int session_in, int session_out, SampleSet sets[ACT_COUNT]) {
    for (int i = 0; i < corpus.count; i++) {
        const SpecDuel* d = &corpus.duels[i];
        if ((session_in >= 0 && d->session != session_in) || d->session == session_out) continue;
        for (int side = 0; side < 2; side++) {
            if (spec_dharok >= 0 && d->dharok[side] != spec_dharok) continue;
            const SpecTick* series = spec_ticks[i] + side * d->row_count;
            for (int r = 1; r < d->row_count; r++) {
                if (d->rows[r].third) continue;
                Probe probe = probe_from_inputs(&series[r - 1].in, &series[r - 1].h, d->rows[r].tick);
                int labels = series[r].labels;
                int flags = (labels & SPEC_EAT ? FLAG_EAT : 0) | (labels & SPEC_SWING ? FLAG_SWING : 0) | (labels & SPEC_VW ? FLAG_VW : 0) |
                    (labels & SPEC_MAUL ? FLAG_MAUL : 0) | (labels & SPEC_VENG ? FLAG_VENG : 0);
                for (int a = 0; a < ACT_COUNT; a++) {
                    Conditioned c = conditioned((Action)a, &probe, flags);
                    if (!c.eligible) continue;
                    SampleSet* set = &sets[a];
                    for (int copy = 0; copy < FIT_SAMPLES; copy++) {
                        if (set->count == set->capacity) {
                            set->capacity = set->capacity ? set->capacity * 2 : 4096;
                            set->samples = realloc(set->samples, set->capacity * sizeof(Sample));
                        }
                        set->samples[set->count++] = (Sample){probe.x[a], (float)c.y, 1.0f / FIT_SAMPLES};
                    }
                }
            }
        }
    }
}

/* Meal size from the heal the bar showed: one hard food, a second food, or three. */
static void spec_servings(int session_out, OppFit* fit, int identified[2]) {
    int counts[2][3] = {{0}};
    for (int i = 0; i < corpus.count; i++) {
        const SpecDuel* d = &corpus.duels[i];
        if (d->session == session_out) continue;
        for (int side = 0; side < 2; side++) {
            if (spec_dharok >= 0 && d->dharok[side] != spec_dharok) continue;
            const SpecTick* series = spec_ticks[i] + side * d->row_count;
            for (int r = 1; r < d->row_count; r++) {
                const SpecSide* x = &d->rows[r].s[side];
                if (d->rows[r].third || (x->meal != MEAL_EAT && x->meal != MEAL_HIDDEN) || x->meal_heal < 0) continue;
                int warned = warning_since(&series[r - 1].h.warning) != WARNING_NONE;
                counts[warned][x->meal_heal < SERVING_SINGLE_MAX ? 0 : x->meal_heal < SERVING_DOUBLE_MAX ? 1 : 2]++;
            }
        }
    }
    for (int w = 0; w < 2; w++) {
        int total = counts[w][0] + counts[w][1] + counts[w][2];
        identified[w] = total;
        if (!total) continue;
        fit->serving[w][0] = (float)counts[w][0] / total;
        fit->serving[w][1] = fit->serving[w][2] = (float)counts[w][1] / total / 2;
        fit->serving[w][3] = (float)counts[w][2] / total;
        for (int k = 0; k < 3; k++) fit->served[w][k] = counts[w][k];
    }
}

static void spec_fit(int session_out, OppFit* fit, int identified[2]) {
    static SampleSet sets[ACT_COUNT];
    for (int a = 0; a < ACT_COUNT; a++) sets[a].count = 0;
    spec_samples(-1, session_out, sets);
    memset(fit, 0, sizeof(*fit));
    fit_default_servings(fit);
    for (int a = 0; a < ACT_COUNT; a++) logit_fit(&fit->logit[a], ACTION_DIM[a], sets[a].samples, sets[a].count);
    fit->eat_action = ACT_EAT_WARNING;
    spec_servings(session_out, fit, identified);
}

static void spec_score(const OppFit* fit, int session, Score scores[ACT_COUNT]) {
    static SampleSet sets[ACT_COUNT];
    for (int a = 0; a < ACT_COUNT; a++) sets[a].count = 0;
    spec_samples(session, -1, sets);
    memset(scores, 0, ACT_COUNT * sizeof(Score));
    for (int a = 0; a < ACT_COUNT; a++)
        for (int i = 0; i < sets[a].count; i += FIT_SAMPLES) {
            const Sample* sample = &sets[a].samples[i];
            score_add(&scores[a], logit_p(&fit->logit[a], &sample->x), sample->y > 0);
        }
}

static void fit_save(const char* path, const OppFit* fit) {
    FILE* f = fopen(path, "wb");
    assert(f && fwrite(fit, sizeof(*fit), 1, f) == 1);
    fclose(f);
}

static int fit_load(const char* path, OppFit* fit) {
    FILE* f = fopen(path, "rb");
    if (!f) return 0;
    assert(fread(fit, sizeof(*fit), 1, f) == 1);
    fclose(f);
    return 1;
}

/* Fit on two spectated sessions, score on the third, for each; then fit on all and test on our own fights. */
static void spectator(Tape* tapes, int count, const char* save) {
    spec_prepare();
    if (getenv("GS_DHAROK")) spec_dharok = atoi(getenv("GS_DHAROK"));
    printf("=== opponent fitted on %d spectated duels in %d sessions ===\n", corpus.count, corpus.session_count);
    for (int out = 0; out < corpus.session_count; out++) {
        static OppFit fit;
        int identified[2];
        spec_fit(out, &fit, identified);
        static Score scores[ACT_COUNT];
        spec_score(&fit, out, scores);
        static OppFit base;
        printf("held out session %s (fit on the others)\n", corpus.sessions[out]);
        for (int a = 0; a < ACT_COUNT; a++) {
            const Score* sc = &scores[a];
            if (!sc->n) continue;
            double rate = sc->y / sc->n, train_rate = (double)fit.logit[a].events / fit.logit[a].opportunities;
            double constant = -sc->n * (rate * log(clip(train_rate)) + (1 - rate) * log(1 - clip(train_rate)));
            printf("  %-24s %6.0f ticks %4.0f events rate %.3f (train %.3f)  logloss/tick %.4f const %.4f (%+.1f%%)\n", ACTION_NAME[a], sc->n, sc->y, rate, train_rate,
                sc->logloss / sc->n, constant / sc->n, 100 * (constant - sc->logloss) / constant);
        }
        (void)base;
    }
    static OppFit all;
    int identified[2];
    spec_fit(-1, &all, identified);
    print_fit("=== fit on all spectated sessions", &all, 0, identified);
    if (save) { fit_save(save, &all); printf("saved the fit to %s\n", save); }
    for (int day = 20261005; day <= 20261006; day++) {
        printf("=== transfer: spectator fit scored on our own fights of day %d\n", day);
        teacher_score(tapes, count, &all, day);
    }
}

int main(int argc, char** argv) {
    assert(argc >= 4);
    static Tape tapes[MAX_TAPES];
    const char* mode = argv[1];
    g_ablation.hp = HP_HIDDEN;
    g_ablation.escape_model = 1;
    g_unseen_ticks = unseen_ticks;
    const char* ablation = getenv("GS_ABL");
    if (ablation) {
        g_ablation.hp = strstr(ablation, "hp=hidden") ? HP_HIDDEN : strstr(ablation, "hp=uniform") ? HP_UNIFORM : strstr(ablation, "hp=oracle") ? HP_ORACLE : HP_HIDDEN;
        const char* appetite = strstr(ablation, "appetite=");
        g_ablation.appetite = appetite ? atof(appetite + 9) : 0;
        const char* low = strstr(ablation, "levels=");
        g_ablation.level_low = low ? atoi(low + 7) : 44;
        g_ablation.level_high = low ? atoi(strchr(low, ':') + 1) : 87;
        const char* threat = strstr(ablation, "threat=");
        g_ablation.threat = threat ? atof(threat + 7) : 0;
        const char* threat_timer = strstr(ablation, "timer=");
        g_ablation.threat_timer = threat_timer ? atoi(threat_timer + 6) : 2;
        g_ablation.eats = strstr(ablation, "eats") != NULL;
        g_ablation.escape = strstr(ablation, "escape") != NULL && !strstr(ablation, "escape=off");
        g_ablation.escape_model = strstr(ablation, "escape=off") == NULL;
        g_ablation.pid = strstr(ablation, "pid") != NULL;
        g_ablation.damage = strstr(ablation, "damage") != NULL;
    }
    int first = !strcmp(mode, "autopsy") ? 5 : !strcmp(mode, "eatstudy") ? 4 : !strcmp(mode, "spectator") ? 4 : !strcmp(mode, "rollouts") || !strcmp(mode, "attribute") || !strcmp(mode, "ablate") ? 6 : 4;
    int count = 0;
    for (int i = first; i < argc; i++) { tape_load(&tapes[count], argv[i]); prepare(&tapes[count]); count++; }
    if (!strcmp(mode, "spectator")) { spec_load(&corpus, argv[2]); spectator(tapes, count, argv[3]); }
    else if (!strcmp(mode, "teacher")) teacher(tapes, count, atoi(argv[2]), atoi(argv[3]));
    else if (!strcmp(mode, "rollouts")) rollouts(atoi(argv[2]), atoi(argv[3]), tapes, count, atoi(argv[4]), atoi(argv[5]));
    else if (!strcmp(mode, "attribute")) attribute(atoi(argv[2]), atoi(argv[3]), tapes, count, atoi(argv[4]), atoi(argv[5]));
    else if (!strcmp(mode, "ablate")) ablate(atoi(argv[2]), atoi(argv[3]), tapes, count, atoi(argv[4]), atoi(argv[5]));
    else if (!strcmp(mode, "eatstudy")) eat_study(tapes, count, atoi(argv[2]), atoi(argv[3]));
    else if (!strcmp(mode, "autopsy")) autopsy(atoi(argv[2]), atoi(argv[3]), tapes, count, atoi(argv[4]));
    else if (!strcmp(mode, "branch")) branch(atoi(argv[2]), atoi(argv[3]), tapes, count);
    else abort();
    return 0;
}
