/* Calibrates the Riskfight opponent against recorded fights by open-loop counterfactual rollouts.
 *
 *   cc -std=c11 -O2 -I. -o /tmp/calibrate_riskfight ocean/osrs/tests/calibrate_riskfight.c -lm
 *   /tmp/calibrate_riskfight calibrate N K TRAIN_DAY TEST_DAY tape...
 *   /tmp/calibrate_riskfight branch N HORIZON tape...
 *
 * Tapes come from tools/rollout_export.py in the Light repo. A day is the first 8 characters of the tape session. */
#define _POSIX_C_SOURCE 200809L
#include <time.h>
#include "riskfight_rollout.h"

enum { MAX_TAPES = 64, METRICS = 10, PIT_BINS = 10, RELIABILITY_BINS = 7, EPISODE_BINS = 9, WARN_HP = 45, MAX_ROLLOUTS = 4096 };

static double seconds(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec + t.tv_nsec * 1e-9;
}

static int tape_day(const Tape* tape) { return atoi(tape->session); }

static int bar_hitpoints_mid(const TapeTick* tick) {
    OsrsHealthBarRange range = osrs_health_bar_range(tick->bar_ratio, tick->bar_scale, 99, 121);
    return range.kind == OSRS_HEALTH_BAR_KNOWN ? (range.lower + range.upper) / 2 : -1;
}

static int rec_eat_at(const Tape* tape, int tick) {
    for (int i = 0; i < tape->anim_count; i++)
        if (tape->anims[i].tick == tick && tape->anims[i].side == SIDE_HIM && tape->anims[i].anim == ANIM_EAT) return 1;
    return 0;
}

typedef struct {
    int eats, his_hits, his_damage, vw_specs, maul_specs, vengs, our_dead, his_dead, our_hits, our_damage;
} Recorded;

static Recorded recorded_window(const Tape* tape, int t0, int k) {
    Recorded r = {0};
    for (int i = 0; i < tape->anim_count; i++) {
        const TapeAnim* a = &tape->anims[i];
        if (a->tick <= t0 || a->tick > t0 + k || a->side != SIDE_HIM) continue;
        r.eats += a->anim == ANIM_EAT;
        r.vw_specs += a->anim == ANIM_VW_SPEC;
        r.maul_specs += a->anim == ANIM_MAUL_SPEC;
        r.vengs += a->anim == ANIM_VENG_A || a->anim == ANIM_VENG_B;
    }
    for (int i = 0; i < tape->hit_count; i++) {
        const TapeHit* h = &tape->hits[i];
        if (h->tick <= t0 || h->tick > t0 + k || h->side != SIDE_US) continue;
        r.his_hits += h->klass == 'A' || h->klass == 'B';
        r.his_damage += h->klass == 'A' ? h->amount : 0;
    }
    for (int i = 0; i < tape->hit_count; i++) {
        const TapeHit* h = &tape->hits[i];
        if (h->tick <= t0 || h->tick > t0 + k || h->side != SIDE_HIM) continue;
        r.our_hits += h->klass == 'A' || h->klass == 'B';
        r.our_damage += h->klass == 'A' ? h->amount : 0;
    }
    for (int t = t0 + 1; t <= t0 + k; t++) {
        r.our_dead |= tape_tick(tape, t)->hp <= 0;
        r.his_dead |= tape_tick(tape, t)->bar_ratio == 0;
    }
    return r;
}

typedef struct {
    double rec[METRICS], sim[METRICS];
    int pit[METRICS][PIT_BINS], pit_n[METRICS];
    double rel_pred[RELIABILITY_BINS], rel_hit[RELIABILITY_BINS];
    int rel_n[RELIABILITY_BINS];
    double brier, brier_base, logloss, logloss_base;
    int hazard_n;
    double hazard_rate;
    double die_pred[RELIABILITY_BINS], die_hit[RELIABILITY_BINS];
    int die_n[RELIABILITY_BINS];
    int windows;
} WindowStats;

static const char* METRIC[METRICS] = {"his meals", "his hits", "his damage", "his VW specs", "his maul specs", "his vengeance", "we die", "he dies", "our hits", "our damage"};

static int rel_bin(double p) {
    return p < 0.02 ? 0 : p < 0.05 ? 1 : p < 0.1 ? 2 : p < 0.2 ? 3 : p < 0.35 ? 4 : p < 0.6 ? 5 : 6;
}
static const char* REL_LABEL[RELIABILITY_BINS] = {"<2%", "2-5%", "5-10%", "10-20%", "20-35%", "35-60%", ">60%"};

static double pit_value(const int* sim, int n, int observed, uint32_t* rng) {
    int below = 0, equal = 0;
    for (int i = 0; i < n; i++) { below += sim[i] < observed; equal += sim[i] == observed; }
    return ((double)below + uniform(rng) * equal) / n;
}

static void evaluate_windows(const Tape* tape, const OppModel* model, int n, int k, int stride, WindowStats* stats) {
    static int sim_values[METRICS][MAX_ROLLOUTS];
    Sim sim; sim_init(&sim);
    OursPolicy replay = {OURS_REPLAY, 0};
    uint32_t pit_rng = 2463534242u;
    for (int t0 = tape->lo + 1; t0 + k <= tape->hi; t0 += stride) {
        Recorded rec = recorded_window(tape, t0, k);
        double sums[METRICS] = {0};
        int first_eat = 0;
        for (int i = 0; i < n; i++) {
            uint32_t rng = osrs_lowbias32((uint32_t)(t0 * 7919 + i * 104729 + tape->index * 13 + tape->lo)) | 1;
            RiskfightSnapshot snap;
            tape_snapshot(tape, t0, &DEFAULT_PRIORS, &rng, &snap);
            riskfight_load_state(&sim.state, &sim.context, &snap);
            Outcome out;
            run_steps(&sim, tape, t0, k, model, &replay, xorshift32(&rng), &out);
            int eats = 0;
            for (int j = 0; j < out.steps; j++) eats += out.his_eat[j];
            int values[METRICS] = {eats, out.his_hits, out.his_damage, out.his_vw_specs, out.his_maul_specs, out.his_vengs,
                out.our_death >= 0, out.his_death >= 0, out.our_hits, out.our_damage};
            for (int m = 0; m < METRICS; m++) { sim_values[m][i] = values[m]; sums[m] += values[m]; }
            first_eat += out.his_eat[0];
        }
        int rec_values[METRICS] = {rec.eats, rec.his_hits, rec.his_damage, rec.vw_specs, rec.maul_specs, rec.vengs, rec.our_dead, rec.his_dead,
            rec.our_hits, rec.our_damage};
        for (int m = 0; m < METRICS; m++) {
            stats->rec[m] += rec_values[m];
            stats->sim[m] += sums[m] / n;
            if (m < 6 || m >= 8) {
                double u = pit_value(sim_values[m], n, rec_values[m], &pit_rng);
                stats->pit[m][u >= 1 ? PIT_BINS - 1 : (int)(u * PIT_BINS)]++;
                stats->pit_n[m]++;
            }
        }
        double predicted = (double)first_eat / n;
        int hit = rec_eat_at(tape, t0 + 1);
        int b = rel_bin(predicted);
        stats->rel_pred[b] += predicted; stats->rel_hit[b] += hit; stats->rel_n[b]++;
        double clipped = predicted < 0.005 ? 0.005 : predicted > 0.995 ? 0.995 : predicted;
        stats->brier += (predicted - hit) * (predicted - hit);
        stats->logloss -= hit ? log(clipped) : log(1 - clipped);
        stats->hazard_n++;
        stats->hazard_rate += hit;
        double die = sums[6] / n;
        b = rel_bin(die);
        stats->die_pred[b] += die; stats->die_hit[b] += rec.our_dead; stats->die_n[b]++;
        stats->windows++;
    }
}

static void print_window_stats(const char* title, WindowStats* s) {
    printf("%s: %d windows\n", title, s->windows);
    printf("  %-16s %10s %10s %7s   PIT histogram (10 bins, flat means calibrated)\n", "metric", "recorded", "sim mean", "ratio");
    for (int m = 0; m < METRICS; m++) {
        printf("  %-16s %10.0f %10.1f %7.2f  ", METRIC[m], s->rec[m], s->sim[m], s->rec[m] > 0 ? s->sim[m] / s->rec[m] : 0.0);
        if (m < 6 || m >= 8) for (int b = 0; b < PIT_BINS; b++) printf("%5d", s->pit[m][b]);
        printf("\n");
    }
    double rate = s->hazard_rate / s->hazard_n;
    s->brier_base = rate * (1 - rate);
    s->logloss_base = -(rate * log(rate) + (1 - rate) * log(1 - rate)) * s->hazard_n;
    printf("  first-step meal probability, %d ticks, base rate %.3f: Brier %.4f (constant %.4f), log-loss/tick %.4f (constant %.4f)\n",
        s->hazard_n, rate, s->brier / s->hazard_n, s->brier_base, s->logloss / s->hazard_n, s->logloss_base / s->hazard_n);
    printf("  %-8s %7s %10s %10s\n", "pred", "ticks", "mean pred", "observed");
    for (int b = 0; b < RELIABILITY_BINS; b++)
        if (s->rel_n[b]) printf("  %-8s %7d %10.3f %10.3f\n", REL_LABEL[b], s->rel_n[b], s->rel_pred[b] / s->rel_n[b], s->rel_hit[b] / s->rel_n[b]);
    printf("  we die within window, windows by predicted probability:\n");
    for (int b = 0; b < RELIABILITY_BINS; b++)
        if (s->die_n[b]) printf("  %-8s %7d %10.3f %10.3f\n", REL_LABEL[b], s->die_n[b], s->die_pred[b] / s->die_n[b], s->die_hit[b] / s->die_n[b]);
}

enum { GAP_BINS = 12 };

typedef struct { double recorded[GAP_BINS], sim[2][GAP_BINS]; } GapStats;

static void evaluate_gaps(const Tape* tape, const OppModel* models, int model_count, int n, GapStats* stats) {
    Sim sim; sim_init(&sim);
    OursPolicy replay = {OURS_REPLAY, 0};
    int previous = -1;
    for (int t = tape->lo; t <= tape->hi; t++) {
        for (int i = 0; i < tape->hit_count; i++) {
            const TapeHit* h = &tape->hits[i];
            if (h->tick != t || h->side != SIDE_US || (h->klass != 'A' && h->klass != 'B')) continue;
            if (previous >= 0) stats->recorded[t - previous >= GAP_BINS ? GAP_BINS - 1 : t - previous]++;
            previous = t;
            break;
        }
    }
    for (int t0 = tape->lo + 1; t0 + 30 <= tape->hi; t0 += 15)
        for (int m = 0; m < model_count; m++)
            for (int i = 0; i < n; i++) {
                uint32_t rng = osrs_lowbias32((uint32_t)(t0 * 7919 + i * 104729 + tape->index * 13 + 5)) | 1;
                RiskfightSnapshot snap;
                tape_snapshot(tape, t0, &DEFAULT_PRIORS, &rng, &snap);
                riskfight_load_state(&sim.state, &sim.context, &snap);
                Outcome out;
                run_steps(&sim, tape, t0, 30, &models[m], &replay, xorshift32(&rng), &out);
                int last = -1;
                for (int j = 0; j < 30; j++) {
                    if (!(out.his_hit_mask >> j & 1)) continue;
                    if (last >= 0) stats->sim[m][j - last >= GAP_BINS ? GAP_BINS - 1 : j - last] += 1.0 / n;
                    last = j;
                }
            }
}

typedef struct {
    int events[HAZARD_BANDS][HAZARD_DELAYS], opportunities[HAZARD_BANDS][HAZARD_DELAYS];
} HazardCounts;

static int ratio_band(int r) { return r <= 8 ? 0 : r <= 13 ? 1 : r <= 19 ? 2 : r <= 24 ? 3 : r <= 29 ? 4 : 5; }

static void count_hazards(const Tape* tape, HazardCounts* c) {
    for (int t = tape->lo + 1; t <= tape->hi; t++) {
        if (rec_eat_at(tape, t - 1) || rec_eat_at(tape, t - 2)) continue;
        const TapeTick* before = tape_tick(tape, t - 1);
        if (before->bar_ratio < 0) continue;
        int since = 0;
        for (int d = 1; d <= 8 && !since; d++)
            for (int i = 0; i < tape->hit_count; i++)
                if (tape->hits[i].tick == t - d && tape->hits[i].side == SIDE_HIM && tape->hits[i].amount >= BIG_HIT) { since = d; break; }
        int band = ratio_band(before->bar_ratio), delay = hazard_delay(since);
        c->opportunities[band][delay]++;
        c->events[band][delay] += rec_eat_at(tape, t);
    }
}

static void fit_pauses(const Tape* tape, OppModel* model, int* pairs, int* paused) {
    int previous = -1;
    for (int t = tape->lo; t <= tape->hi; t++) {
        int hit = 0;
        for (int i = 0; i < tape->hit_count && !hit; i++)
            hit = tape->hits[i].tick == t && tape->hits[i].side == SIDE_US && (tape->hits[i].klass == 'A' || tape->hits[i].klass == 'B');
        if (!hit) continue;
        int ate = 0;
        for (int x = previous; previous >= 0 && x <= t; x++) ate |= rec_eat_at(tape, x);
        if (previous >= 0 && !ate) {
            (*pairs)++;
            if (t - previous >= 6 && model->pause_lengths < PAUSE_LENGTHS) {
                (*paused)++;
                model->pause_length[model->pause_lengths++] = t - previous - 4;
            }
        }
        previous = t;
    }
}

static const float SMOOTHING = 5;

static void fit_hazard(const HazardCounts* c, OppModel* model) {
    model->kind = MODEL_HAZARD;
    int events = 0, opportunities = 0;
    for (int b = 0; b < HAZARD_BANDS; b++) for (int d = 0; d < HAZARD_DELAYS; d++) { events += c->events[b][d]; opportunities += c->opportunities[b][d]; }
    float overall = (float)events / opportunities;
    for (int b = 0; b < HAZARD_BANDS; b++) {
        int be = 0, bo = 0;
        for (int d = 0; d < HAZARD_DELAYS; d++) { be += c->events[b][d]; bo += c->opportunities[b][d]; }
        float band_rate = (be + SMOOTHING * overall) / (bo + SMOOTHING);
        for (int d = 0; d < HAZARD_DELAYS; d++)
            model->hazard[b][d] = (c->events[b][d] + SMOOTHING * band_rate) / (c->opportunities[b][d] + SMOOTHING);
    }
}

static void print_hazard(const char* title, const HazardCounts* c, const OppModel* m) {
    static const char* DELAY[HAZARD_DELAYS] = {"no big hit", "1 after", "2 after", "3 after", "4-8 after"};
    printf("%s, eat hazard per eligible tick (events/opportunities) by his bar band x ticks since a >=%d hit on him\n  %-10s", title, BIG_HIT, "bar ratio");
    for (int d = 0; d < HAZARD_DELAYS; d++) printf("%16s", DELAY[d]);
    printf("\n");
    static const char* BAND[HAZARD_BANDS] = {"<=8", "9-13", "14-19", "20-24", "25-29", "30"};
    for (int b = 0; b < HAZARD_BANDS; b++) {
        printf("  %-10s", BAND[b]);
        for (int d = 0; d < HAZARD_DELAYS; d++) printf("  %3d/%-4d %.3f", c->events[b][d], c->opportunities[b][d], m->hazard[b][d]);
        printf("\n");
    }
}

static const double FIRST_MEAL[EPISODE_BINS] = {0.01, 0.21, 0.49, 0.18, 0.03, 0.05, 0.01, 0.01, 0.01};

typedef struct {
    int episodes, immediate;
    int recorded[EPISODE_BINS];
    double sim[2][EPISODE_BINS];
} EpisodeStats;

typedef enum { TRIGGER_WARNING, TRIGGER_BIG_HIT } Trigger;
enum { BIG_HIT_EPISODE = 30, REARM_TICKS = 4 };

static int damage_on_him(const Tape* tape, int tick) {
    int total = 0;
    for (int i = 0; i < tape->hit_count; i++)
        if (tape->hits[i].tick == tick && tape->hits[i].side == SIDE_HIM && tape->hits[i].klass == 'A') total += tape->hits[i].amount;
    return total;
}

static void evaluate_episodes(const Tape* tape, Trigger trigger, const OppModel* models, int model_count, int n, EpisodeStats* stats) {
    Sim sim; sim_init(&sim);
    OursPolicy replay = {OURS_REPLAY, 0};
    int armed = 1, last_trigger = -100;
    for (int u = tape->lo + 1; u + 8 <= tape->hi; u++) {
        int fired;
        if (trigger == TRIGGER_WARNING) {
            int hp = bar_hitpoints_mid(tape_tick(tape, u));
            if (hp > WARN_HP) armed = 1;
            fired = hp >= 0 && hp <= WARN_HP && armed;
            armed &= !fired;
        } else {
            fired = damage_on_him(tape, u) >= BIG_HIT_EPISODE && u - last_trigger >= REARM_TICKS;
        }
        if (!fired) continue;
        last_trigger = u;
        stats->episodes++;
        int d = 0;
        while (d <= 7 && !rec_eat_at(tape, u + d)) d++;
        if (d == 0) { stats->immediate++; continue; }
        stats->recorded[d > 7 ? 8 : d]++;
        for (int m = 0; m < model_count; m++)
            for (int i = 0; i < n; i++) {
                uint32_t rng = osrs_lowbias32((uint32_t)(u * 7919 + i * 104729 + tape->index * 13)) | 1;
                RiskfightSnapshot snap;
                tape_snapshot(tape, u, &DEFAULT_PRIORS, &rng, &snap);
                riskfight_load_state(&sim.state, &sim.context, &snap);
                Outcome out;
                run_steps(&sim, tape, u, 7, &models[m], &replay, xorshift32(&rng), &out);
                stats->sim[m][out.first_eat < 0 ? 8 : out.first_eat + 1] += 1.0 / n;
            }
    }
}

static void print_episodes(const char* title, const EpisodeStats* s, const char* names[2]) {
    int counted = s->episodes - s->immediate;
    double norm = 0;
    for (int d = 1; d < EPISODE_BINS; d++) norm += FIRST_MEAL[d];
    printf("%s: %d episodes (warning = bar midpoint at or below %d HP), %d already ate on the anchor tick, %d compared\n", title, s->episodes, WARN_HP, s->immediate, counted);
    printf("  %-12s", "ticks after");
    for (int d = 1; d < EPISODE_BINS; d++) printf("%8s", d == 8 ? "none" : (char[]){(char)('0' + d), 0});
    printf("\n  %-12s", "recorded");
    for (int d = 1; d < EPISODE_BINS; d++) printf("%8.3f", (double)s->recorded[d] / counted);
    for (int m = 0; m < 2; m++) {
        printf("\n  %-12s", names[m]);
        for (int d = 1; d < EPISODE_BINS; d++) printf("%8.3f", s->sim[m][d] / counted);
    }
    printf("\n  %-12s", "FIRST_MEAL");
    for (int d = 1; d < EPISODE_BINS; d++) printf("%8.3f", FIRST_MEAL[d] / norm);
    double mean_rec = 0, mean_sim[2] = {0, 0}, mean_fm = 0;
    for (int d = 1; d < EPISODE_BINS; d++) {
        mean_rec += d * (double)s->recorded[d] / counted;
        for (int m = 0; m < 2; m++) mean_sim[m] += d * s->sim[m][d] / counted;
        mean_fm += d * FIRST_MEAL[d] / norm;
    }
    double tv[3] = {0, 0, 0};
    for (int d = 1; d < EPISODE_BINS; d++) {
        double r = (double)s->recorded[d] / counted;
        tv[0] += fabs(r - s->sim[0][d] / counted) / 2;
        tv[1] += fabs(r - s->sim[1][d] / counted) / 2;
        tv[2] += fabs(r - FIRST_MEAL[d] / norm) / 2;
    }
    printf("\n  mean bin (none=8): recorded %.2f, %s %.2f, %s %.2f, FIRST_MEAL %.2f\n", mean_rec, names[0], mean_sim[0], names[1], mean_sim[1], mean_fm);
    printf("  total variation to recorded: %s %.3f, %s %.3f, FIRST_MEAL %.3f\n", names[0], tv[0], names[1], tv[1], tv[2]);
}

static void calibrate(int n, int k, int train_day, int test_day, Tape* tapes, int count) {
    HazardCounts counts = {{{0}}};
    int train_tapes = 0, test_tapes = 0;
    for (int i = 0; i < count; i++) {
        if (tape_day(&tapes[i]) == train_day) { count_hazards(&tapes[i], &counts); train_tapes++; }
        test_tapes += tape_day(&tapes[i]) == test_day;
    }
    OppModel models[2] = {{MODEL_PROFILES, {{0}}}, {MODEL_HAZARD, {{0}}}};
    fit_hazard(&counts, &models[1]);
    int pairs = 0, paused = 0;
    for (int i = 0; i < count; i++) if (tape_day(&tapes[i]) == train_day) fit_pauses(&tapes[i], &models[1], &pairs, &paused);
    models[1].pause_chance = (float)paused / pairs;
    for (int m = 0; m < RATES; m++) models[1].rate_scale[m] = 1;
    for (int iteration = 0; iteration < 4; iteration++) {
        WindowStats fit = {{0}};
        for (int i = 0; i < count; i++) if (tape_day(&tapes[i]) == train_day) evaluate_windows(&tapes[i], &models[1], 16, k, 3, &fit);
        const int metric[RATES] = {5, 3, 4, 0};
        for (int m = 0; m < RATES; m++) {
            float ratio = fit.sim[metric[m]] > 0 ? (float)(fit.rec[metric[m]] / fit.sim[metric[m]]) : 10.0f;
            models[1].rate_scale[m] *= fminf(10.0f, fmaxf(0.1f, ratio));
        }
    }
    printf("fitted: pause chance %.3f per swing (%d of %d swing pairs without a meal, %d lengths), rate scales veng %.2f, VW spec %.2f, maul spec %.2f, meal %.2f\n",
        models[1].pause_chance, paused, pairs, models[1].pause_lengths, models[1].rate_scale[0], models[1].rate_scale[1], models[1].rate_scale[2], models[1].rate_scale[3]);
    printf("=== fit on day %d (%d fights), test on day %d (%d fights), N=%d rollouts, k=%d ticks ===\n", train_day, train_tapes, test_day, test_tapes, n, k);
    print_hazard("train", &counts, &models[1]);
    const char* names[2] = {"profiles", "hazard fit"};
    static WindowStats stats[2];
    static EpisodeStats episodes[2];
    static GapStats gaps;
    memset(&gaps, 0, sizeof(gaps));
    memset(stats, 0, sizeof(stats));
    memset(episodes, 0, sizeof(episodes));
    for (int i = 0; i < count; i++) {
        if (tape_day(&tapes[i]) != test_day) continue;
        for (int m = 0; m < 2; m++) evaluate_windows(&tapes[i], &models[m], n, k, 1, &stats[m]);
        for (int trigger = 0; trigger < 2; trigger++) evaluate_episodes(&tapes[i], (Trigger)trigger, models, 2, n, &episodes[trigger]);
        evaluate_gaps(&tapes[i], models, 2, n, &gaps);
    }
    for (int m = 0; m < 2; m++) { char title[64]; snprintf(title, sizeof(title), "test day %d, model %s", test_day, names[m]); print_window_stats(title, &stats[m]); }
    print_episodes("test, warning = his bar within reach", &episodes[0], names);
    print_episodes("test, anchor = our single-tick hit of 30 or more", &episodes[1], names);
    double rec_total = 0, sim_total[2] = {0, 0};
    for (int g = 1; g < GAP_BINS; g++) { rec_total += gaps.recorded[g]; sim_total[0] += gaps.sim[0][g]; sim_total[1] += gaps.sim[1][g]; }
    printf("his gap between consecutive hits on us, share by ticks (last bin is 11+)\n  %-12s", "gap");
    for (int g = 1; g < GAP_BINS; g++) printf("%7d", g);
    printf("\n  %-12s", "recorded");
    for (int g = 1; g < GAP_BINS; g++) printf("%7.3f", gaps.recorded[g] / rec_total);
    for (int m = 0; m < 2; m++) { printf("\n  %-12s", names[m]); for (int g = 1; g < GAP_BINS; g++) printf("%7.3f", gaps.sim[m][g] / sim_total[m]); }
    printf("\n");
}

static void branch(int n, int horizon, Tape* tapes, int count) {
    OppModel model = {MODEL_PROFILES, {{0}}};
    OursPolicy policies[3] = {{OURS_REPLAY, 0}, {OURS_EAT_AT_THRESHOLD, 50}, {OURS_EAT_AT_THRESHOLD, 80}};
    const char* names[3] = {"replay recorded", "eat below 50", "eat below 80"};
    Sim sim; sim_init(&sim);
    for (int i = 0; i < count; i++) {
        Tape* tape = &tapes[i];
        int t0 = tape->lo + (tape->hi - tape->lo) / 2;
        uint32_t rng = 99;
        RiskfightSnapshot snap;
        tape_snapshot(tape, t0, &DEFAULT_PRIORS, &rng, &snap);
        riskfight_load_state(&sim.state, &sim.context, &snap);
        RiskfightState base = sim.state;
        printf("%s tick %d: our hp %d\n", tape->name, t0, base.env.players[0].current_hitpoints);
        for (int p = 0; p < 3; p++) {
            int die = 0, kill = 0;
            double start = seconds();
            for (int r = 0; r < n; r++) {
                sim.state = base;
                sim.state.env.rng_state = (uint32_t)(r * 2654435761u + 1) | 1;
                for (int side = 0; side < 2; side++) sim.state.script_seed[side] = osrs_lowbias32(sim.state.env.rng_state + side);
                Outcome out;
                run_steps(&sim, tape, t0, horizon, &model, &policies[p], sim.state.env.rng_state, &out);
                die += out.our_death >= 0;
                kill += out.his_death >= 0;
            }
            double elapsed = seconds() - start;
            printf("  %-16s P(we die) %.3f  P(kill) %.3f  %d rollouts x %d ticks in %.3f s = %.0f rollouts/s\n",
                names[p], (double)die / n, (double)kill / n, n, horizon, elapsed, n / elapsed);
        }
    }
}

int main(int argc, char** argv) {
    assert(argc >= 5);
    static Tape tapes[MAX_TAPES];
    const char* mode = argv[1];
    int first = !strcmp(mode, "calibrate") ? 6 : 4;
    int count = 0;
    for (int i = first; i < argc; i++) { tape_load(&tapes[count], argv[i]); count++; }
    if (!strcmp(mode, "calibrate")) calibrate(atoi(argv[2]), atoi(argv[3]), atoi(argv[4]), atoi(argv[5]), tapes, count);
    else if (!strcmp(mode, "branch")) branch(atoi(argv[2]), atoi(argv[3]), tapes, count);
    else abort();
    return 0;
}
