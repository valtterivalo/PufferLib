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

enum { MAX_TAPES = 64, METRICS = 11, PIT_BINS = 10, REL_BINS = 7, EPISODE_BINS = 9, MAX_ROLLOUTS = 4096,
    FIT_SAMPLES = 4, SCORE_SAMPLES = 16, PROFILE_SAMPLES = 64, BIG_HIT_EPISODE = 30, REARM_TICKS = 4, MARGIN = 10 };
enum { FLAG_EAT = 1, FLAG_SWING = 2, FLAG_VW = 4, FLAG_MAUL = 8, FLAG_VENG = 16 };

static double seconds(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec + t.tv_nsec * 1e-9;
}

static int tape_day(const Tape* tape) { return atoi(tape->session); }

static int flag_at(const Tape* tape, int tick, int flag) {
    return tick >= tape->lo && tick <= tape->hi && (tape->his_flags[tick - tape->lo] & flag);
}
static int rec_eat_at(const Tape* tape, int tick) { return flag_at(tape, tick, FLAG_EAT); }

static void prepare(Tape* tape) {
    int n = tape->hi - tape->lo + 1;
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
        if (tape_day(&tapes[i]) != day) continue;
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

static void print_fit(const char* title, const OppFit* fit) {
    static const char* TERMS[ACT_COUNT] = {
        "intercept (bar <=8), bar 9-13, bar 14-24, bar 25+, lag 1, lag 2, lag 3, lag 4-8 (ticks since a >=20 hit on him)",
        "intercept (bar <=8), bar 9-13, bar 14-24, bar 25+, lag 1, lag 2, lag 3, lag 4-8 (ticks since the Java warning)",
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

static void teacher(Tape* tapes, int count, int train_day, int test_day) {
    static OppFit fit;
    memset(&fit, 0, sizeof(fit));
    fit_model(tapes, count, train_day, &fit);
    char title[128];
    snprintf(title, sizeof(title), "=== teacher forcing: fit on day %d, score on day %d", train_day, test_day);
    print_fit(title, &fit);
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

/* ---------- k-tick rollouts ---------- */

typedef struct {
    int eats, his_hits, his_damage, vw_specs, maul_specs, vengs, our_dead, his_dead, our_hits, our_damage, bar_end;
} Recorded;

static Recorded recorded_window(const Tape* tape, int t0, int k) {
    Recorded r = {0};
    for (int x = t0 + 1; x <= t0 + k; x++) {
        r.eats += flag_at(tape, x, FLAG_EAT);
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
} WindowStats;

static const char* METRIC[METRICS] = {"his meals", "his hits", "his damage", "his VW specs", "his maul specs", "his vengeance", "we die", "he dies", "our hits", "our damage", "his bar at end"};

static double pit_value(const int* sim, int n, int observed, uint32_t* rng) {
    int below = 0, equal = 0;
    for (int i = 0; i < n; i++) { below += sim[i] < observed; equal += sim[i] == observed; }
    return ((double)below + uniform(rng) * equal) / n;
}

static void evaluate_windows(const Tape* tape, const OppModel* model, const OursPolicy* ours, int n, int k, WindowStats* stats) {
    static int sim_values[METRICS][MAX_ROLLOUTS];
    Sim* sim = malloc(sizeof(Sim));
    sim_init(sim);
    uint32_t pit_rng = 2463534242u;
    for (int t0 = tape->lo + 1; t0 + k <= tape->hi; t0++) {
        Recorded rec = recorded_window(tape, t0, k);
        double sums[METRICS] = {0};
        for (int i = 0; i < n; i++) {
            uint32_t rng = osrs_lowbias32((uint32_t)(t0 * 7919 + i * 104729 + tape->index * 13 + tape->lo)) | 1;
            RiskfightSnapshot snap;
            tape_snapshot(tape, t0, &DEFAULT_PRIORS, &rng, &snap);
            riskfight_load_state(&sim->state, &sim->context, &snap);
            Outcome out;
            run_steps(sim, tape, t0, k, model, ours, xorshift32(&rng), &out);
            int eats = 0;
            for (int j = 0; j < out.steps; j++) eats += out.his_eat[j];
            int values[METRICS] = {eats, out.his_hits, out.his_damage, out.his_vw_specs, out.his_maul_specs, out.his_vengs,
                out.our_death >= 0, out.his_death >= 0, out.our_hits, out.our_damage, out.bar_him};
            for (int m = 0; m < METRICS; m++) { sim_values[m][i] = values[m]; sums[m] += values[m]; }
        }
        int rec_values[METRICS] = {rec.eats, rec.his_hits, rec.his_damage, rec.vw_specs, rec.maul_specs, rec.vengs, rec.our_dead, rec.his_dead,
            rec.our_hits, rec.our_damage, rec.bar_end};
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
    free(sim);
}

static void print_window_stats(const char* title, const WindowStats* s) {
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
    fit_model(tapes, count, train_day, &fit_big);
    fit_big.eat_action = ACT_EAT_BIG;
    fit_warning = fit_big;
    fit_warning.eat_action = ACT_EAT_WARNING;
    OppModel models[3] = {{MODEL_PROFILES, NULL}, {MODEL_FIT, &fit_big}, {MODEL_FIT, &fit_warning}};
    const char* names[3] = {"profiles", "fit big-hit", "fit warning"};
    OursPolicy closed = {OURS_EAT_AT_RISK, MARGIN}, replay = {OURS_REPLAY, 0};
    printf("=== rollouts: fit on day %d, test on day %d, N=%d, k=%d ===\n", train_day, test_day, n, k);
    static WindowStats stats[4];
    static EpisodeStats episodes[2];
    memset(stats, 0, sizeof(stats));
    memset(episodes, 0, sizeof(episodes));
    for (int i = 0; i < count; i++) {
        if (tape_day(&tapes[i]) != test_day) continue;
        evaluate_windows(&tapes[i], &models[0], &replay, n, k, &stats[0]);
        for (int m = 0; m < 3; m++) evaluate_windows(&tapes[i], &models[m], &closed, n, k, &stats[m + 1]);
        for (int trigger = 0; trigger < 2; trigger++) evaluate_episodes(&tapes[i], (Trigger)trigger, models, &closed, n, &episodes[trigger]);
    }
    print_window_stats("profiles, our recorded actions replayed (open loop)", &stats[0]);
    for (int m = 0; m < 3; m++) {
        char title[96];
        snprintf(title, sizeof(title), "%s, our side eats at risk (closed loop)", names[m]);
        print_window_stats(title, &stats[m + 1]);
    }
    print_episodes("first meal after the Java warning (closed loop)", &episodes[0], names);
    print_episodes("first meal after our single hit of 30 or more (closed loop)", &episodes[1], names);
}

static void branch(int n, int horizon, Tape* tapes, int count) {
    OppModel model = {MODEL_PROFILES, NULL};
    OursPolicy policies[3] = {{OURS_REPLAY, 0}, {OURS_EAT_AT_THRESHOLD, 50}, {OURS_EAT_AT_RISK, MARGIN}};
    const char* names[3] = {"replay recorded", "eat below 50", "eat at risk"};
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
        printf("%s tick %d: our hp %d\n", tape->name, t0, base.env.players[0].current_hitpoints);
        for (int p = 0; p < 3; p++) {
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

int main(int argc, char** argv) {
    assert(argc >= 5);
    static Tape tapes[MAX_TAPES];
    const char* mode = argv[1];
    int first = !strcmp(mode, "rollouts") ? 6 : 4;
    int count = 0;
    for (int i = first; i < argc; i++) { tape_load(&tapes[count], argv[i]); prepare(&tapes[count]); count++; }
    if (!strcmp(mode, "teacher")) teacher(tapes, count, atoi(argv[2]), atoi(argv[3]));
    else if (!strcmp(mode, "rollouts")) rollouts(atoi(argv[2]), atoi(argv[3]), tapes, count, atoi(argv[4]), atoi(argv[5]));
    else if (!strcmp(mode, "branch")) branch(atoi(argv[2]), atoi(argv[3]), tapes, count);
    else abort();
    return 0;
}
