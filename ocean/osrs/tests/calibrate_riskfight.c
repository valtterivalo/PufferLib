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
    int death_tapes, our_death_tapes;
} WindowStats;

static const char* METRIC[METRICS] = {"his meals", "his hits", "his damage", "his VW specs", "his maul specs", "his vengeance", "we die", "he dies", "our hits", "our damage", "his bar at end", "our meals"};

static double pit_value(const int* sim, int n, int observed, uint32_t* rng) {
    int below = 0, equal = 0;
    for (int i = 0; i < n; i++) { below += sim[i] < observed; equal += sim[i] == observed; }
    return ((double)below + uniform(rng) * equal) / n;
}

static FILE* g_deaths;
static FILE* g_windows;


/* ---------- death autopsy: how each sim death differs from the recording of the same window ---------- */
enum { MECH_NO_RECORDED_HIT, MECH_ONE_TICK_OFF, MECH_ROLLED_HIGHER, MECH_REFLECTED, MECH_HP_SKIPPED_EAT, MECH_HP_LOW, MECH_CHAIN, MECHS };
static const char* MECH_NAME[MECHS] = {"sim hit with no recorded hit that tick (or a block)", "recorded hit one tick off", "rolled higher than the recorded hit (recorded roll would not kill)",
    "only reflected damage (echo or recoil) kills", "recorded hit also kills at his sim hp: he skipped an eat the real one made", "recorded hit also kills at his sim hp: no meal missed (hp start or heal size)",
    "earlier damage chain, lethal hit smaller than the recorded one"};
static long g_mech[MECHS];
static int g_dump_limit, g_dumped, g_death_seen, g_death_on;

static int rec_eats_between(const Tape* tape, int from, int to) { int n = 0; for (int x = from; x <= to; x++) n += tape_his_anim_at(tape, x, ANIM_EAT); return n; }

static void death_autopsy(const Tape* tape, int t0, int rollout, const Outcome* out) {
    int d = out->his_death;
    int x = t0 + 1 + d;
    int rec = tape_damage_on_him(tape, x), before = x > tape->lo ? tape_damage_on_him(tape, x - 1) : 0, after = x < tape->hi ? tape_damage_on_him(tape, x + 1) : 0;
    int hp_pre = d > 0 ? out->trace_hp[d - 1] : out->start_hp;
    int sim = out->trace_damage[d], direct = out->trace_direct[d];
    int mech;
    int sim_eats = 0;
    for (int j = 0; j < d; j++) sim_eats += out->trace_ate[j] > 0;
    int recorded_eats = rec_eats_between(tape, t0 + 1, x - 1);
    if (direct == 0 && sim > 0) mech = MECH_REFLECTED;
    else if (rec == 0 && (before > 0 || after > 0)) mech = MECH_ONE_TICK_OFF;
    else if (rec == 0) mech = MECH_NO_RECORDED_HIT;
    else if (sim > rec && hp_pre > rec) mech = MECH_ROLLED_HIGHER;
    else if (hp_pre <= rec) mech = sim_eats < recorded_eats ? MECH_HP_SKIPPED_EAT : MECH_HP_LOW;
    else mech = MECH_CHAIN;
    g_mech[mech]++;
    g_death_seen++;
    if (g_dumped < g_dump_limit && g_death_seen % 37 == 1) {
        g_dumped++;
        printf("--- death %d: %s window t0 %d rollout %d, died at step %d (tick %d): %s\n", g_dumped, tape->name, t0, rollout, d, x, MECH_NAME[mech]);
        printf("    start: recorded bar %d/%d, sim hp %d, recorded eats before the lethal tick %d, sim eats %d\n", tape_tick(tape, t0)->bar_ratio, tape_tick(tape, t0)->bar_scale, out->start_hp, recorded_eats, sim_eats);
        printf("    %5s | recorded: dmg eat his-swing pid | sim: his hp  dmg(direct) eat(1 hidden 2 shown) our-swing his-swing pid our hp\n", "tick");
        for (int j = 0; j <= d; j++) {
            int tick = t0 + 1 + j;
            const TapeTick* tt = tape_tick(tape, tick <= tape->hi ? tick : tape->hi);
            printf("    %5d |          %3d  %d   %d        %c   |      %5d  %3d(%3d)    %d             %d        %d       %d   %3d\n", tick, tape_damage_on_him(tape, tick), tape_his_anim_at(tape, tick, ANIM_EAT),
                (tape->his_flags[tick - tape->lo] & FLAG_SWING) != 0, tt->pid, out->trace_hp[j], out->trace_damage[j], out->trace_direct[j], out->trace_ate[j], out->trace_our_swing[j],
                out->trace_his_swing[j], out->trace_pid[j], out->trace_our_hp[j]);
        }
    }
}


static double g_ha_hist[10], g_ha_n, g_ha_sum, g_ha_dead, g_ha_band_n[3], g_ha_band_pit[3], g_ha_band_diff[3], g_ha_band_dead[3];
static double g_hb_hist[10], g_hb_n, g_hb_sum, g_hb_tails, g_hb_mean_diff, g_hb_band_n[3], g_hb_band_pit[3], g_hb_band_diff[3];

/* The first recorded hit on him in the window with a health bar after it: the tick and the hitpoints that leave him at, plus the damage, as the PIT of the recordings reads them. */
static int recorded_first_hit(const Tape* tape, int t0, int k, int* tick, int* low, int* high) {
    for (int x = t0 + 1; x <= t0 + k && x <= tape->hi; x++) {
        int damage = tape_damage_on_him(tape, x);
        const TapeTick* after = tape_tick(tape, x);
        if (damage > 0 && after->bar_ratio > 0 && after->bar_scale > 1 && after->last_bar_hit == x) {
            OsrsHealthBarRange shown = osrs_health_bar_range(after->bar_ratio, after->bar_scale, 99, 121);
            *tick = x;
            *low = shown.lower + damage;
            *high = after->bar_ratio == after->bar_scale ? (shown.upper + damage > 121 ? shown.upper + damage : 121) : shown.upper + damage;
            return 1;
        }
    }
    return 0;
}

static void evaluate_windows(const Tape* tape, const OppModel* model, const OursPolicy* ours, int n, int k, WindowStats* stats) {
    static int sim_values[METRICS][MAX_ROLLOUTS];
    g_tape = tape;
    Sim* sim = malloc(sizeof(Sim));
    sim_init(sim);
    uint32_t pit_rng = 2463534242u;
    double started = seconds();
    int tape_rec_dead = 0, tape_rec_our_dead = 0;
    for (int t0 = tape->lo + 1; t0 + k <= tape->hi; t0++) {
        Recorded rec = recorded_window(tape, t0, k);
        double sums[METRICS] = {0};
        int hb_tick = 0, hb_low = 0, hb_high = 0, hb_count = 0;
        int hb_values[MAX_ROLLOUTS], ha_values[MAX_ROLLOUTS];
        int hb_known = recorded_first_hit(tape, t0, k, &hb_tick, &hb_low, &hb_high);
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
            if (hb_known) {
                int step = hb_tick - t0 - 1, quiet = 1;
                for (int j = 0; j < step; j++) quiet &= out.trace_damage[j] == 0;
                if (quiet && step < out.steps && out.trace_damage[step] > 0) { ha_values[hb_count] = out.trace_hp[step]; hb_values[hb_count++] = step > 0 ? out.trace_hp[step - 1] : out.start_hp; }
            }
            if (g_death_on && g_dump_limit >= 0 && out.his_death >= 0) death_autopsy(tape, t0, i, &out);
            int eats = 0;
            for (int j = 0; j < out.steps; j++) eats += out.his_eat[j];
            int values[METRICS] = {eats, out.his_hits, out.his_damage, out.his_vw_specs, out.his_maul_specs, out.his_vengs,
                out.our_death >= 0, out.his_death >= 0, out.our_hits, out.our_damage, out.bar_him, out.our_meals};
            for (int m = 0; m < METRICS; m++) { sim_values[m][i] = values[m]; sums[m] += values[m]; }
        }
        if (hb_known && hb_count >= 3) {
            double pit = 0;
            for (int v = hb_low; v <= hb_high; v++) {
                int below = 0, equal = 0;
                for (int j = 0; j < hb_count; j++) { below += hb_values[j] < v; equal += hb_values[j] == v; }
                pit += (below + 0.5 * equal) / hb_count / (hb_high - hb_low + 1);
            }
            {
                const TapeTick* after = tape_tick(tape, hb_tick);
                OsrsHealthBarRange shown = osrs_health_bar_range(after->bar_ratio, after->bar_scale, 99, 121);
                int lo = shown.lower, hi = shown.upper, dead = 0;
                double apit = 0, amean = 0;
                for (int v = lo; v <= hi; v++) {
                    int below = 0, equal = 0;
                    for (int j = 0; j < hb_count; j++) { below += ha_values[j] < v; equal += ha_values[j] == v; }
                    apit += (below + 0.5 * equal) / hb_count / (hi - lo + 1);
                }
                for (int j = 0; j < hb_count; j++) { amean += ha_values[j]; dead += ha_values[j] <= 0; }
                amean /= hb_count;
                double mid = (lo + hi) / 2.0;
                int band = mid <= 44 ? 0 : mid <= 87 ? 1 : 2;
                int ad = (int)(apit * 10); if (ad > 9) ad = 9;
                g_ha_hist[ad]++; g_ha_n++; g_ha_sum += apit; g_ha_dead += dead / (double)hb_count;
                g_ha_band_n[band]++; g_ha_band_pit[band] += apit; g_ha_band_diff[band] += amean - mid; g_ha_band_dead[band] += dead / (double)hb_count;
            }
            int d = (int)(pit * 10); if (d > 9) d = 9;
            g_hb_hist[d]++; g_hb_n++; g_hb_sum += pit; g_hb_tails += pit < 0.1 || pit > 0.9;
            double mean = 0; for (int j = 0; j < hb_count; j++) mean += hb_values[j]; mean /= hb_count;
            double mid = (hb_low + hb_high) / 2.0;
            int band = mid <= 44 ? 0 : mid <= 87 ? 1 : 2;
            g_hb_band_n[band]++; g_hb_band_pit[band] += pit; g_hb_band_diff[band] += mean - mid; g_hb_mean_diff += mean - mid;
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
        tape_rec_dead |= rec.his_dead;
        tape_rec_our_dead |= rec.our_dead;
        stats->windows++;
    }
    stats->death_tapes += tape_rec_dead;
    stats->our_death_tapes += tape_rec_our_dead;
    stats->seconds += seconds() - started;
    stats->spec_counted += sim->spec_counted;
    stats->spec_predicted += sim->spec_predicted;
    stats->spec_realized += sim->spec_realized;
    free(sim);
}

static void print_window_stats(const char* title, const WindowStats* s) {
    if (s->ticks > 0) printf("%s: %.0f rollouts in %.2f s = %.0f rollouts/s with decide in the loop, %.3f decisions per tick (%.0f of %.0f ticks); played spec decisions %.0f, predicted kills %.1f, realized %.1f\n", title, s->rollouts, s->seconds, s->rollouts / s->seconds, s->decisions / s->ticks, s->decisions, s->ticks, s->spec_counted, s->spec_predicted, s->spec_realized);
    printf("%s: recorded deaths of him in %d tapes, of us in %d tapes; the window sums of the two lines below hold each death in about %d windows when it falls mid-fight and 3-5 when it ends the tape\n", title, s->death_tapes, s->our_death_tapes, 10);
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
    g_thrown_only = getenv("GS_THROWN") != NULL;
    g_no_eat = getenv("GS_NOEAT") != NULL;
    if (getenv("GS_HIDDEN")) g_hidden_meal_chance = atof(getenv("GS_HIDDEN"));
    if (getenv("GS_GATE_LOW")) g_gate_low = atof(getenv("GS_GATE_LOW"));
    if (getenv("GS_GATE_HIGH")) g_gate_high = atof(getenv("GS_GATE_HIGH"));
    if (getenv("GS_EAT_RISK")) g_eat_risk = atof(getenv("GS_EAT_RISK"));
    if (getenv("GS_EAT_INERT")) g_eat_inert = atof(getenv("GS_EAT_INERT"));
    if (getenv("GS_NOSURV")) g_survival_seed = 0;
    if (getenv("GS_ESCFLAT")) g_escape_by_hp = 0;
    if (getenv("GS_GATED")) g_hidden_gated = atof(getenv("GS_GATED"));
    if (getenv("GS_SEED_INERT")) g_seed_inert = atof(getenv("GS_SEED_INERT"));
    g_abort_per_8 = getenv("GS_ABORT") ? atoi(getenv("GS_ABORT")) : 0;
    g_vw_only = getenv("GS_VWONLY") != NULL;
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
    OursPolicy replay = {OURS_REPLAY, 0}, rules = {OURS_RULES, 0, getenv("GS_IDEAL") ? TIMING_IDEAL : TIMING_MEASURED, getenv("GS_EQUAL") ? STAKES_EQUAL : STAKES_COMPUTED};
    static WindowStats open, closed;
    memset(&open, 0, sizeof(open));
    memset(&closed, 0, sizeof(closed));
    if (getenv("GS_FULL_FIGHT")) {
        for (int i = 0; i < count; i++) {
            if (!in_day(&tapes[i], test_day)) continue;
            const Tape* tp = &tapes[i];
            g_tape = tp;
            Sim* sim = malloc(sizeof(Sim));
            sim_init(sim);
            int t0 = tp->lo + 1, kk = tp->hi - t0;
            assert(kk <= MAX_STEPS);
            double hd = 0, od = 0, meals = 0, swings = 0, specs = 0, ticks = 0, hd_first = 0, rr = 0;
            for (int r = 0; r < n; r++) {
                uint32_t rng = osrs_lowbias32((uint32_t)(t0 * 7919 + r * 104729 + tp->index * 13 + tp->lo)) | 1;
                RiskfightSnapshot snap;
                tape_snapshot(tp, t0, &DEFAULT_PRIORS, &rng, &snap);
                riskfight_load_state(&sim->state, &sim->context, &snap);
                Outcome out;
                run_steps(sim, tp, t0, kk, &model, &rules, xorshift32(&rng), &out);
                hd += out.his_death >= 0; od += out.our_death >= 0; meals += out.our_meals; ticks += out.steps;
                for (int j = 0; j < out.steps; j++) { swings += out.trace_our_swing[j]; specs += out.trace_our_spec[j]; }
                rr = sim->obs.stakes.us > 0 ? (double)sim->obs.stakes.him / sim->obs.stakes.us : 1;
                (void)hd_first;
            }
            printf("FIGHT %s rollouts %d his_dead %.4f our_dead %.4f meals %.3f swings %.3f specs %.3f ticks %.2f r %.3f\n", tp->name, n, hd / n, od / n, meals / n, swings / n, specs / n, ticks / n, rr);
            free(sim);
        }
        return;
    }
    g_dump_limit = getenv("GS_DEATH_AUTOPSY") ? atoi(getenv("GS_DEATH_AUTOPSY")) : -1;
    const char* log = getenv("GS_DEATHS");
    g_deaths = log ? fopen(log, "w") : NULL;
    g_windows = getenv("GS_WINDOWS") ? fopen(getenv("GS_WINDOWS"), "w") : NULL;
    for (int i = 0; i < count; i++) {
        if (!in_day(&tapes[i], test_day)) continue;
        int death_open = getenv("GS_DEATH_LOOP") && !strcmp(getenv("GS_DEATH_LOOP"), "open");
        g_death_on = death_open;
        evaluate_windows(&tapes[i], &model, &replay, n, k, &open);
        g_death_on = !death_open;
        if (!getenv("GS_OPEN_ONLY")) evaluate_windows(&tapes[i], &model, &rules, n, k, &closed);
        g_death_on = 0;
    }
    if (getenv("GS_DEATH_AUTOPSY")) {
        printf("death mechanisms over %d sim deaths in the closed loop:\n", g_death_seen);
        for (int m = 0; m < MECHS; m++) printf("  %5ld  %5.1f%%  %s\n", g_mech[m], 100.0 * g_mech[m] / (g_death_seen ? g_death_seen : 1), MECH_NAME[m]);
    }
    printf("=== autopsy N=%d k=%d day %d ablation hp %d eats %d escape %d pid %d damage %d ===\n", n, k, test_day, g_ablation.hp, g_ablation.eats, g_ablation.escape, g_ablation.pid, g_ablation.damage);
    print_window_stats("open loop, fit warning", &open);
    if (!getenv("GS_OPEN_ONLY")) print_window_stats("closed loop, fit warning", &closed);
    printf("planner kills by his true hp at the spec decision (<=44, 45-87, 88+): played %.0f %.0f %.0f, predicted %.1f %.1f %.1f, realized %.1f %.1f %.1f\n", g_spec_counted[0], g_spec_counted[1], g_spec_counted[2],
        g_spec_predicted[0], g_spec_predicted[1], g_spec_predicted[2], g_spec_realized[0], g_spec_realized[1], g_spec_realized[2]);
    printf("planner kills by the planner's expected hp (<=60, 60-75, 75-90, 90+): played %.0f %.0f %.0f %.0f, predicted %.1f %.1f %.1f %.1f, realized %.1f %.1f %.1f %.1f\n", g_exp_counted[0], g_exp_counted[1], g_exp_counted[2], g_exp_counted[3],
        g_exp_predicted[0], g_exp_predicted[1], g_exp_predicted[2], g_exp_predicted[3], g_exp_realized[0], g_exp_realized[1], g_exp_realized[2], g_exp_realized[3]);
    for (int b = 0; b < 3; b++) if (g_dm_n[b]) printf("window outcome, true hp band %d: n %.0f mean damage %.1f any damage %.3f damage >= hp %.3f he dies %.3f he ate %.3f\n", b, g_dm_n[b], g_dm_sum[b] / g_dm_n[b], g_dm_any[b] / g_dm_n[b], g_dm_lethal[b] / g_dm_n[b], g_dm_dead[b] / g_dm_n[b], g_dm_ate[b] / g_dm_n[b]);
    {
        for (int i = 0; i < count; i++) {
            if (!in_day(&tapes[i], test_day)) continue;
            const Tape* tp = &tapes[i];
            for (int a = 0; a < tp->action_count; a++) {
                if (tp->actions[a].kind != ACTION_SPEC || tp->actions[a].tick <= tp->lo || tp->actions[a].tick + 3 > tp->hi) continue;
                int t = tp->actions[a].tick;
                const TapeTick* at = tape_tick(tp, t);
                if (at->bar_ratio <= 0 || at->bar_scale <= 1) continue;
                OsrsHealthBarRange bar = osrs_health_bar_range(at->bar_ratio, at->bar_scale, 99, 121);
                double m = (bar.lower + bar.upper) / 2.0;
                int rb = m <= 44 ? 0 : m <= 60 ? 1 : m <= 87 ? 2 : 3, ate3 = 0;
                for (int x = t; x <= t + 3; x++) ate3 |= tape_his_anim_at(tp, x, ANIM_EAT);
                g_react_rec[rb][ate3]++;
            }
        }
        static const char* NAME[4] = {"44 or less", "45-60", "61-87", "88+"};
        printf("his eat within 3 ticks of our spec decision, by the bar's hp (recorded spec toggles vs sim played decisions): band recorded n / share ate | sim n / share ate\n");
        for (int b = 0; b < 4; b++) {
            double rn = g_react_rec[b][0] + g_react_rec[b][1], sn = g_react_sim[b][0] + g_react_sim[b][1];
            printf("  %-10s %6.0f %.3f | %6.0f %.3f\n", NAME[b], rn, rn ? g_react_rec[b][1] / rn : 0, sn, sn ? g_react_sim[b][1] / sn : 0);
        }
    }
    printf("combo groups started %ld, aborted by the hand %ld, Voidwaker leg preempted by our meal %ld\n", g_cb_total, g_cb_abort, g_cb_preempt);
    printf("combo decisions: not preempted %.0f of which both specs thrown %.0f; preempted %.0f of which both specs thrown %.0f\n", g_cb_split[0][0] + g_cb_split[0][1], g_cb_split[0][1], g_cb_split[1][0] + g_cb_split[1][1], g_cb_split[1][1]);
    for (int c = 1; c < 4; c++) if (g_pc_n[c]) { printf("specs thrown per decision %.2f; ", g_pc_specs[c] / g_pc_n[c]);
        printf("choice %d (1 vw, 2 maul+vw, 3 maul): n %.0f planned mean damage %.1f realized damage in the plan window %.1f, first hit planned +%.2f realized +%.2f, any damage %.3f, planner chance %.3f he dies %.3f\n", c, g_pc_n[c], g_pc_plan[c] / g_pc_n[c], g_pc_act[c] / g_pc_n[c], g_pc_land[c] / g_pc_n[c], g_pc_actland[c] / g_pc_n[c], g_pc_any[c] / g_pc_n[c], g_pc_chance[c] / g_pc_n[c], g_pc_dead[c] / g_pc_n[c]); }
    {
        static const char* FB[4] = {"hp<=44", "45-60", "61-87", "88+"};
        for (int b = 0; b < 4; b++) {
            double n = 0; for (int j = 0; j < 11; j++) n += g_fm[b][j];
            printf("first meal after the decision, %s (n %.0f), hazard per tick 0..9:", FB[b], n);
            double risk = n;
            for (int j = 0; j < 10; j++) { printf(" %.2f", risk > 0 ? g_fm[b][j] / risk : 0); risk -= g_fm[b][j]; }
            printf("  none %.2f\n", n > 0 ? g_fm[b][10] / n : 0);
        }
    }
    for (int c = 0; c < 3; c++) printf("plugin class %s: ticks %.0f, true meal on %.3f\n", c == 0 ? "QUIET" : c == 1 ? "ATE" : "HIDDEN", g_cls[c][0] + g_cls[c][1], g_cls[c][1] / (g_cls[c][0] + g_cls[c][1] + 1e-9));
    printf("belief PIT deciles of his true hp, all decisions:"); for (int d = 0; d < 10; d++) printf(" %.3f", g_pit_n ? g_pit_all[d] / g_pit_n : 0);
    printf("\n  expected <= 60:"); for (int d = 0; d < 10; d++) printf(" %.3f", g_pit_low_n ? g_pit_low[d] / g_pit_low_n : 0);
    printf("\n  played decisions:"); for (int d = 0; d < 10; d++) printf(" %.3f", g_pit_played_n ? g_pit_played[d] / g_pit_played_n : 0);
    printf("\n");
    printf("belief calibration over every decision, by the planner's expected hp (<=30,<=45,<=60,<=75,<=90,90+): n, mean expected, mean true, share 15 under, share 15 over:\n");
    for (int b = 0; b < 6; b++) if (g_eb_n[b]) printf("  [%.0f %.1f %.1f %.2f %.2f]", g_eb_n[b], g_eb_exp[b] / g_eb_n[b], g_eb_true[b] / g_eb_n[b], g_eb_low[b] / g_eb_n[b], g_eb_high[b] / g_eb_n[b]);
    printf("\n");
    {
        static const char* KEY[4] = {"hidden ticks (0,1,2-3,4+)", "ate ticks (0,1,2+)", "quiet ticks (<3,<8,<20,20+)", "bar width (<3,<10,10+)"};
        for (int k = 0; k < 4; k++) {
            printf("belief bias by %s: n, mean expected, mean true:", KEY[k]);
            for (int b = 0; b < 4; b++) if (g_diag_n[k][b]) printf("  [%.0f %.1f %.1f]", g_diag_n[k][b], g_diag_exp[k][b] / g_diag_n[k][b], g_diag_true[k][b] / g_diag_n[k][b]);
            printf("\n");
        }
    }
    if (g_hb_n) {
        printf("sim hp before our first hit against the recordings' bar-derived hp (the PIT quantity), %.0f windows: mean PIT %.3f, tails %.3f, mean sim minus recorded %.1f; by recorded hp 44-, 45-87, 88+: PIT", g_hb_n, g_hb_sum / g_hb_n, g_hb_tails / g_hb_n, g_hb_mean_diff / g_hb_n);
        for (int b = 0; b < 3; b++) printf(" %.3f (n %.0f, diff %.1f)", g_hb_band_n[b] ? g_hb_band_pit[b] / g_hb_band_n[b] : 0, g_hb_band_n[b], g_hb_band_n[b] ? g_hb_band_diff[b] / g_hb_band_n[b] : 0);
        printf("\n  deciles:"); for (int d = 0; d < 10; d++) printf(" %.3f", g_hb_hist[d] / g_hb_n);
        printf("\n");
    }
    if (g_ha_n) {
        printf("sim hp right after the first recorded hit against the bar after it, %.0f windows: mean PIT %.3f, share of rollouts dead there %.3f; by recorded hp after 44-, 45-87, 88+:", g_ha_n, g_ha_sum / g_ha_n, g_ha_dead / g_ha_n);
        for (int b = 0; b < 3; b++) printf(" PIT %.3f dead %.3f diff %.1f (n %.0f)", g_ha_band_n[b] ? g_ha_band_pit[b] / g_ha_band_n[b] : 0, g_ha_band_n[b] ? g_ha_band_dead[b] / g_ha_band_n[b] : 0, g_ha_band_n[b] ? g_ha_band_diff[b] / g_ha_band_n[b] : 0, g_ha_band_n[b]);
        printf("\n  deciles:"); for (int d = 0; d < 10; d++) printf(" %.3f", g_ha_hist[d] / g_ha_n);
        printf("\n");
    }
    printf("by the planner's expected hp band: mean expected hp, mean true hp:\n");
    for (int b = 0; b < 4; b++) if (g_exp_counted[b]) printf("  band %d  n %5.0f  expected %5.1f  true %5.1f\n", b, g_exp_counted[b], g_exp_mean[b] / g_exp_counted[b], g_exp_true[b] / g_exp_counted[b]);
    {
        long rec_our_a = 0, rec_our_b = 0, rec_his_a = 0, rec_his_b = 0;
        for (int i = 0; i < count; i++) {
            if (!in_day(&tapes[i], test_day)) continue;
            for (int h = 0; h < tapes[i].hit_count; h++) {
                const TapeHit* hit = &tapes[i].hits[h];
                if (hit->tick <= tapes[i].lo || hit->tick > tapes[i].hi) continue;
                if (hit->side == SIDE_HIM) { rec_our_a += hit->klass == 'A' && hit->amount > 0; rec_our_b += hit->klass == 'B' || (hit->klass == 'A' && hit->amount == 0); }
                else { rec_his_a += hit->klass == 'A' && hit->amount > 0; rec_his_b += hit->klass == 'B' || (hit->klass == 'A' && hit->amount == 0); }
            }
        }
        printf("share of hits that deal damage: ours recorded %.3f (%ld of %ld), sim %.3f; his recorded %.3f (%ld of %ld), sim %.3f\n", rec_our_a / (double)(rec_our_a + rec_our_b), rec_our_a, rec_our_a + rec_our_b,
            g_our_events ? g_our_positive / (double)g_our_events : 0, rec_his_a / (double)(rec_his_a + rec_his_b), rec_his_a, rec_his_a + rec_his_b, g_his_events ? g_his_positive / (double)g_his_events : 0);
    }
    {
        long total = 0, below = 0;
        double sum = 0;
        for (int d = 1; d < 160; d++) { total += g_hit_hist[d]; sum += (double)d * g_hit_hist[d]; }
        int q50 = 0, q90 = 0, q99 = 0;
        long run = 0, ge40 = 0, ge60 = 0;
        for (int d = 1; d < 160; d++) { run += g_hit_hist[d]; if (!q50 && run >= total * 0.5) q50 = d; if (!q90 && run >= total * 0.9) q90 = d; if (!q99 && run >= total * 0.99) q99 = d; if (d >= 40) ge40 += g_hit_hist[d]; if (d >= 60) ge60 += g_hit_hist[d]; }
        (void)below;
        printf("our damaging hits in the sim: mean %.1f, median %d, p90 %d, p99 %d, share 40+ %.3f, share 60+ %.3f (recorded: mean 23.7, median 23, p90 44, p99 67, share 40+ 0.181, share 60+ 0.023)\n", sum / total, q50, q90, q99, ge40 / (double)total, ge60 / (double)total);
    }
    {
        static long recorded_ticks[13];
        Sim* seeds = malloc(sizeof(Sim));
        sim_init(seeds);
        for (int i = 0; i < count; i++) {
            if (!in_day(&tapes[i], test_day)) continue;
            g_tape = &tapes[i];
            for (int t = tapes[i].lo + 1; t <= tapes[i].hi; t++)
                for (int j = 0; j < 8; j++) {
                    uint32_t rng = osrs_lowbias32((uint32_t)(t * 7919 + j * 104729 + tapes[i].index * 13 + tapes[i].lo)) | 1;
                    RiskfightSnapshot snap;
                    tape_snapshot(&tapes[i], t, &DEFAULT_PRIORS, &rng, &snap);
                    recorded_ticks[imin(12, imax(0, snap.loadout[1].hitpoints) / 10)]++;
                }
        }
        free(seeds);
        long rec_total = 0, sim_total = 0;
        for (int b = 0; b < 13; b++) { rec_total += recorded_ticks[b]; sim_total += g_sim_hp_ticks[b]; }
        printf("share of ticks by his hp: recorded (seeded hp of every tape tick), sim rollouts\n");
        for (int b = 0; b < 13; b++) printf("    hp %3d-%3d  recorded %.4f  sim %.4f\n", b * 10, b == 12 ? 199 : b * 10 + 9, recorded_ticks[b] / (double)rec_total, g_sim_hp_ticks[b] / (double)sim_total);
    }
    {
        int tapes_n = 0, escapes = 0, in_window = 0, windows = 0;
        double mid_sum = 0; int low_hp = 0;
        for (int i = 0; i < count; i++) {
            if (!in_day(&tapes[i], test_day)) continue;
            tapes_n++;
            int e = tape_escape_tick(&tapes[i]);
            windows += tapes[i].hi - tapes[i].lo - k;
            if (e == INT_MAX) continue;
            escapes++;
            const TapeTick* at = tape_tick(&tapes[i], e > tapes[i].lo ? e - 1 : e);
            if (at->bar_ratio > 0 && at->bar_scale > 1) { OsrsHealthBarRange bar = osrs_health_bar_range(at->bar_ratio, at->bar_scale, 99, 121); double m = (bar.lower + bar.upper) / 2.0; mid_sum += m; low_hp += m <= 60; }
            in_window += k;
        }
        printf("recorded escapes: %d of %d tapes, %.2f per 1000 windows' rollouts at the window rate (%d windows), at bar hp mean %.0f, %d at 60 or less\n", escapes, tapes_n, 1000.0 * in_window / (windows > 0 ? windows : 1), windows, escapes ? mid_sum / escapes : 0.0, low_hp);
    }
    {
        double exposure[4] = {0}, left[4] = {0};
        for (int i = 0; i < count; i++) {
            if (!in_day(&tapes[i], test_day)) continue;
            int e = tape_escape_tick(&tapes[i]);
            for (int t = tapes[i].lo + 1; t <= tapes[i].hi; t++) {
                if (t > e) break;
                const TapeTick* at = tape_tick(&tapes[i], t);
                if (at->bar_ratio <= 0 || at->bar_scale <= 1) continue;
                OsrsHealthBarRange bar = osrs_health_bar_range(at->bar_ratio, at->bar_scale, 99, 121);
                double m = (bar.lower + bar.upper) / 2.0;
                int b = m <= 44 ? 0 : m <= 60 ? 1 : m <= 87 ? 2 : 3;
                exposure[b]++;
                if (t == e) left[b]++;
            }
        }
        printf("recorded escape hazard per tick by the bar's hp (44-, 45-60, 61-87, 88+):");
        for (int b = 0; b < 4; b++) printf(" %.4f (%0.f of %.0f ticks)", exposure[b] ? left[b] / exposure[b] : 0, left[b], exposure[b]);
        printf("\n");
    }
    printf("seed hp survival conditioning: %ld draws, %ld rejected, %ld windows with no surviving draw in 400\n", g_seed_draws, g_seed_rejected, g_seed_unsatisfied);
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
                r->type = 0;
                r->fight = i * FIT_SAMPLES + j;
                r->group = tape_day(&tapes[i]);
                for (int k = 0; k < tapes[i].anim_count; k++)
                    r->masked |= tapes[i].anims[k].tick == t && tapes[i].anims[k].side == SIDE_HIM && tapes[i].anims[k].anim != ANIM_EAT;
                for (int k = 0; k < tapes[i].hit_count; k++) r->masked |= tapes[i].hits[k].tick == t && tapes[i].hits[k].side == SIDE_HIM;
                r->weight = 1.0 / FIT_SAMPLES;
            }
        }
    }
}



/* ---------- latent eater types: his eating as a mixture of K types drawn once per fight ---------- */

typedef struct { int types; double pi[MAX_TYPES]; EatLogit logit; } EatMixture;

static int eat_fights(const EatRows* rows) { int n = 0; for (int i = 0; i < rows->count; i++) n = imax(n, rows->rows[i].fight + 1); return n; }

/* Log likelihood of each fight's eat sequence under each type, weight of the fight (its rows' weight per tick are the draws of his hitpoints, one fight per draw). */
static void mix_fight_ll(const EatMixture* m, const EatRows* rows, int fights, double* ll) {
    memset(ll, 0, (size_t)fights * MAX_TYPES * sizeof(double));
    for (int i = 0; i < rows->count; i++) {
        EatRow r = rows->rows[i];
        for (int k = 0; k < m->types; k++) {
            r.type = k;
            double p = clip(eat_p(&m->logit, &r, EAT_MIXTURE));
            ll[r.fight * MAX_TYPES + k] += r.y ? log(p) : log(1 - p);
        }
    }
}

static void mix_posteriors(const EatMixture* m, const double* ll, int fights, double* post, double* marginal) {
    for (int f = 0; f < fights; f++) {
        double top = -1e300;
        for (int k = 0; k < m->types; k++) top = fmax(top, log(m->pi[k]) + ll[f * MAX_TYPES + k]);
        double sum = 0;
        for (int k = 0; k < m->types; k++) { post[f * MAX_TYPES + k] = exp(log(m->pi[k]) + ll[f * MAX_TYPES + k] - top); sum += post[f * MAX_TYPES + k]; }
        for (int k = 0; k < m->types; k++) post[f * MAX_TYPES + k] /= sum;
        marginal[f] = top + log(sum);
    }
}

/* Expectation maximisation: types share the slopes of the logit and differ by an intercept. */
static void mix_fit(EatMixture* m, int types, const EatRows* rows) {
    int fights = eat_fights(rows);
    double* ll = malloc((size_t)fights * MAX_TYPES * sizeof(double));
    double* post = malloc((size_t)fights * MAX_TYPES * sizeof(double));
    double* marginal = malloc((size_t)fights * sizeof(double));
    static EatRows expanded;
    memset(m, 0, sizeof(*m));
    m->types = types;
    for (int k = 0; k < types; k++) m->pi[k] = 1.0 / types;
    EatRows plain = *rows;
    eat_fit(&m->logit, 1, &plain);
    if (types > 1) m->logit.w[21] = 0.8;
    if (types > 2) m->logit.w[22] = -0.8;
    for (int iteration = 0; iteration < 30; iteration++) {
        mix_fight_ll(m, rows, fights, ll);
        mix_posteriors(m, ll, fights, post, marginal);
        double total[MAX_TYPES] = {0};
        double fights_seen = 0;
        expanded.count = 0;
        for (int i = 0; i < rows->count; i++) {
            if (expanded.count + types > expanded.capacity) { expanded.capacity = expanded.capacity ? expanded.capacity * 2 : 1 << 16; expanded.rows = realloc(expanded.rows, expanded.capacity * sizeof(EatRow)); }
            for (int k = 0; k < types; k++) {
                EatRow r = rows->rows[i];
                r.type = k;
                r.weight *= post[r.fight * MAX_TYPES + k];
                expanded.rows[expanded.count++] = r;
            }
        }
        static char seen_fight[1 << 16];
        memset(seen_fight, 0, sizeof(seen_fight));
        assert(fights < (int)sizeof(seen_fight));
        for (int i = 0; i < rows->count; i++) {
            int f = rows->rows[i].fight;
            if (seen_fight[f]) continue;
            seen_fight[f] = 1;
            double w = rows->rows[i].weight;
            fights_seen += w;
            for (int k = 0; k < types; k++) total[k] += w * post[f * MAX_TYPES + k];
        }
        for (int k = 0; k < types; k++) m->pi[k] = fmax(1e-3, total[k] / fights_seen);
        double norm = 0;
        for (int k = 0; k < types; k++) norm += m->pi[k];
        for (int k = 0; k < types; k++) m->pi[k] /= norm;
        eat_fit(&m->logit, EAT_MIXTURE, &expanded);
    }
    free(ll); free(post); free(marginal);
}

/* Fight level log likelihood marginalised over the type, each fight counted once (a fight is one draw of his hitpoints, `weight` of them per tick). */
static double mix_score(const EatMixture* m, const EatRows* rows, double* fights_out) {
    int fights = eat_fights(rows);
    double* ll = malloc((size_t)fights * MAX_TYPES * sizeof(double));
    double* post = malloc((size_t)fights * MAX_TYPES * sizeof(double));
    double* marginal = malloc((size_t)fights * sizeof(double));
    mix_fight_ll(m, rows, fights, ll);
    mix_posteriors(m, ll, fights, post, marginal);
    static char seen[1 << 16];
    memset(seen, 0, sizeof(seen));
    double total = 0, count = 0;
    for (int i = 0; i < rows->count; i++) {
        int f = rows->rows[i].fight;
        if (seen[f]) continue;
        seen[f] = 1;
        total += marginal[f] * rows->rows[i].weight;
        count += rows->rows[i].weight;
    }
    free(ll); free(post); free(marginal);
    if (fights_out) *fights_out = count;
    return total;
}

/* His meals are drawn from a hazard fitted on the ticks his eat animation could show (not hidden behind another animation), the current logit plus his hitpoints bands,
 * with a type drawn once per rollout (GS_EAT_MODEL=mix, the default: fixtures/eat_mixture.fit from `eatmix`); GS_EAT_MODEL=1 is the single logit of fixtures/eat_unmasked.fit, GS_EAT_MODEL=m refits model m (2 adds our swing timer, 3 the low hp interaction) on the days GS_EAT_TRAIN names (minus a day for all others),
 * GS_EAT_LOAD loads a saved fit, GS_EAT_SAVE writes it, GS_EAT_MODEL=off keeps the logit of the spectator fit. */

/* His meals are drawn from a hazard fitted on the ticks his eat animation could show (not hidden behind another animation), the current logit plus his hitpoints bands,
 * with a type drawn once per rollout (GS_EAT_MODEL=mix, the default: fixtures/eat_mixture.fit from `eatmix`); GS_EAT_MODEL=1 is the single logit of fixtures/eat_unmasked.fit, GS_EAT_MODEL=m refits model m (2 adds our swing timer, 3 the low hp interaction) on the days GS_EAT_TRAIN names (minus a day for all others),
 * GS_EAT_LOAD loads a saved fit, GS_EAT_SAVE writes it, GS_EAT_MODEL=off keeps the logit of the spectator fit. */

/* His meals are drawn from a hazard fitted on the ticks his eat animation could show (not hidden behind another animation), the current logit plus his hitpoints bands,
 * with a type drawn once per rollout (GS_EAT_MODEL=mix, the default: fixtures/eat_mixture.fit from `eatmix`); GS_EAT_MODEL=1 is the single logit of fixtures/eat_unmasked.fit, GS_EAT_MODEL=m refits model m (2 adds our swing timer, 3 the low hp interaction) on the days GS_EAT_TRAIN names (minus a day for all others),
 * GS_EAT_LOAD loads a saved fit, GS_EAT_SAVE writes it, GS_EAT_MODEL=off keeps the logit of the spectator fit. */
static void eat_model_setup(Tape* tapes, int count) {
    const char* model = getenv("GS_EAT_MODEL");
    if (model && !strcmp(model, "off")) return;
    g_eat_model.on = 1;
    if (!model || !strcmp(model, "mix")) {
        const char* path = getenv("GS_EAT_LOAD") ? getenv("GS_EAT_LOAD") : "ocean/osrs/tests/fixtures/eat_mixture.fit";
        FILE* f = fopen(path, "rb");
        EatMixture mixture;
        assert(f && fread(&mixture, sizeof(mixture), 1, f) == 1);
        fclose(f);
        g_eat_model.model = EAT_MIXTURE;
        g_eat_model.types = mixture.types;
        memcpy(g_eat_model.pi, mixture.pi, sizeof(mixture.pi));
        g_eat_model.logit = mixture.logit;
        return;
    }
    g_eat_model.model = atoi(model);
    const char* load = getenv("GS_EAT_LOAD");
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


/* Rows of the spectated duels: every fighter-duel is a fight, his true meals (the spectator log shows hidden ones) on the ticks he could eat, hitpoints read off his bar. */
static void spec_eat_rows(EatRows* rows, int session_in, int session_out, int fight_offset) {
    for (int i = 0; i < corpus.count; i++) {
        const SpecDuel* d = &corpus.duels[i];
        if ((session_in >= 0 && d->session != session_in) || d->session == session_out) continue;
        for (int side = 0; side < 2; side++) {
            if (d->dharok[side] != 0) continue;
            const SpecTick* series = spec_ticks[i] + side * d->row_count;
            for (int r = 1; r < d->row_count; r++) {
                if (d->rows[r].third) continue;
                Probe probe = probe_from_inputs(&series[r - 1].in, &series[r - 1].h, d->rows[r].tick);
                if (!probe.eligible[ACT_EAT_WARNING]) continue;
                if (rows->count == rows->capacity) { rows->capacity = rows->capacity ? rows->capacity * 2 : 8192; rows->rows = realloc(rows->rows, rows->capacity * sizeof(EatRow)); }
                EatRow* row = &rows->rows[rows->count++];
                int ratio = series[r - 1].in.hp_ratio;
                row->base_n = probe.x[ACT_EAT_WARNING].n;
                for (int k = 0; k < row->base_n; k++) row->base[k] = probe.x[ACT_EAT_WARNING].idx[k];
                row->hp = ratio >= 30 ? 105 : (warning_bar_low(ratio, 30) + warning_bar_high(ratio, 30)) / 2;
                row->timer = 3;
                row->y = (series[r].labels & SPEC_EAT) != 0;
                row->masked = 0;
                row->type = 0;
                row->fight = fight_offset + i * 2 + side;
                row->group = d->session;
                row->weight = 1;
            }
        }
    }
}

static void eat_rows_unmasked(EatRows* rows) {
    int kept = 0;
    for (int i = 0; i < rows->count; i++) if (!rows->rows[i].masked) rows->rows[kept++] = rows->rows[i];
    rows->count = kept;
}

static void mix_print(const char* name, const EatMixture* m) {
    printf("    %s: types %d, shares", name, m->types);
    for (int k = 0; k < m->types; k++) printf(" %.2f", m->pi[k]);
    printf(", intercept shifts 0.00");
    if (m->types > 1) printf(" %+.2f", m->logit.w[21]);
    if (m->types > 2) printf(" %+.2f", m->logit.w[22]);
    printf(", slopes (bar band, lag, warning, hp bands):");
    for (int d = 0; d < 14; d++) printf(" %.2f", m->logit.w[d]);
    printf("\n");
}

/* Posterior over the types after each fight's visible ticks, as the plugin could keep it live: ticks of one fight until the leading type holds 80 percent. */
static void mix_separation(const EatMixture* m, const EatRows* rows, const char* name, int fights_total) {
    int fights = eat_fights(rows);
    double* ll = calloc((size_t)fights * MAX_TYPES, sizeof(double));
    double* post = malloc((size_t)fights * MAX_TYPES * sizeof(double));
    double* marginal = malloc((size_t)fights * sizeof(double));
    mix_fight_ll(m, rows, fights, ll);
    mix_posteriors(m, ll, fights, post, marginal);
    static int ticks_seen[1 << 16], reached[1 << 16], first_tick[1 << 16];
    static double running[1 << 16][MAX_TYPES];
    memset(ticks_seen, 0, sizeof(ticks_seen));
    memset(reached, 0, sizeof(reached));
    for (int f = 0; f < fights; f++) for (int k = 0; k < m->types; k++) running[f][k] = log(m->pi[k]);
    static int final_type[1 << 16];
    for (int f = 0; f < fights; f++) { final_type[f] = 0; for (int k = 1; k < m->types; k++) if (post[f * MAX_TYPES + k] > post[f * MAX_TYPES + final_type[f]]) final_type[f] = k; }
    (void)fights_total;
    static int lengths[1 << 16];
    memset(lengths, 0, sizeof(lengths));
    for (int i = 0; i < rows->count; i++) lengths[rows->rows[i].fight]++;
    for (int i = 0; i < rows->count; i++) {
        EatRow r = rows->rows[i];
        int f = r.fight;
        ticks_seen[f]++;
        for (int k = 0; k < m->types; k++) { r.type = k; double p = clip(eat_p(&m->logit, &r, EAT_MIXTURE)); running[f][k] += r.y ? log(p) : log(1 - p); }
        double top = -1e300, sum = 0;
        for (int k = 0; k < m->types; k++) top = fmax(top, running[f][k]);
        for (int k = 0; k < m->types; k++) sum += exp(running[f][k] - top);
        double lead = exp(running[f][final_type[f]] - top) / sum;
        if (!reached[f] && lead >= 0.8) { reached[f] = 1; first_tick[f] = ticks_seen[f]; }
    }
    static char seen[1 << 16];
    memset(seen, 0, sizeof(seen));
    int list[MAX_TYPES][1 << 13], n[MAX_TYPES] = {0}, total[MAX_TYPES] = {0};
    for (int i = 0; i < rows->count; i++) {
        int f = rows->rows[i].fight;
        if (seen[f]) continue;
        seen[f] = 1;
        if (lengths[f] < 20 || post[f * MAX_TYPES + final_type[f]] < 0.9) continue;
        total[final_type[f]]++;
        if (reached[f]) list[final_type[f]][n[final_type[f]]++] = first_tick[f];
    }
    printf("  %s: ticks of one fight (eatable ticks) until the posterior of its final type holds 80%%, among fights of 20 or more eatable ticks that end 90%% sure:\n", name);
    for (int k = 0; k < m->types; k++) {
        int* l = list[k];
        for (int i = 1; i < n[k]; i++) { int v = l[i], j = i - 1; while (j >= 0 && l[j] > v) { l[j + 1] = l[j]; j--; } l[j + 1] = v; }
        printf("    type %d: %d fights, %d reach it, median %d, quartiles %d..%d, fights never reaching it %d\n", k, total[k], n[k], n[k] ? l[n[k] / 2] : -1, n[k] ? l[n[k] / 4] : -1, n[k] ? l[(3 * n[k]) / 4] : -1, total[k] - n[k]);
    }
    (void)marginal;
    free(ll); free(post); free(marginal);
}

/* Eat rate by his hitpoints for each fight's most likely type, against the planner's. */
static void mix_rates(const EatMixture* m, const EatRows* rows, const char* name) {
    int fights = eat_fights(rows);
    double* ll = malloc((size_t)fights * MAX_TYPES * sizeof(double));
    double* post = malloc((size_t)fights * MAX_TYPES * sizeof(double));
    double* marginal = malloc((size_t)fights * sizeof(double));
    mix_fight_ll(m, rows, fights, ll);
    mix_posteriors(m, ll, fights, post, marginal);
    static const int EDGES[6] = {0, 31, 45, 61, 88, 200};
    double n[MAX_TYPES][5] = {{0}}, e[MAX_TYPES][5] = {{0}}, fights_of[MAX_TYPES] = {0};
    static char seen[1 << 16];
    memset(seen, 0, sizeof(seen));
    for (int i = 0; i < rows->count; i++) {
        const EatRow* r = &rows->rows[i];
        int type = 0;
        for (int k = 1; k < m->types; k++) if (post[r->fight * MAX_TYPES + k] > post[r->fight * MAX_TYPES + type]) type = k;
        if (!seen[r->fight]) { seen[r->fight] = 1; fights_of[type] += r->weight; }
        for (int b = 0; b < 5; b++) if (r->hp >= EDGES[b] && r->hp < EDGES[b + 1]) { n[type][b] += r->weight; e[type][b] += r->y * r->weight; }
    }
    printf("  %s eat rate by his hp and most likely type (ticks, rate); planner 0.481*wants(hp) at the band middle:\n", name);
    printf("    %-10s", "hp band");
    for (int k = 0; k < m->types; k++) printf("   type %d (%.0f fights)", k, fights_of[k]);
    printf("   planner\n");
    for (int b = 0; b < 5; b++) {
        double middle = b == 4 ? 100 : (EDGES[b] + EDGES[b + 1]) / 2.0;
        printf("    %3d-%-3d   ", EDGES[b], b == 4 ? 199 : EDGES[b + 1] - 1);
        for (int k = 0; k < m->types; k++) printf("   %6.0f  %.3f       ", n[k][b], n[k][b] > 0 ? e[k][b] / n[k][b] : 0);
        printf("   %.3f\n", 0.481 * fmax(0, fmin(1, (87 - middle) / 43.0)));
    }
    free(ll); free(post); free(marginal);
}

static void eat_mixture_study(Tape* tapes, int count, int train_day, int test_day, const char* spectator_path, const char* save) {
    Sim* sim = malloc(sizeof(Sim));
    sim_init(sim);
    static EatRows train, test, all;
    eat_rows_add(&train, tapes, count, train_day, sim);
    eat_rows_add(&test, tapes, count, test_day, sim);
    eat_rows_add(&all, tapes, count, 0, sim);
    eat_rows_unmasked(&train);
    eat_rows_unmasked(&test);
    eat_rows_unmasked(&all);
    printf("=== eater types, own fights: fit on day %d, held out day %d (fight level log likelihood, marginal over the type) ===\n", train_day, test_day);
    static EatMixture fit[MAX_TYPES + 1];
    for (int k = 1; k <= 3; k++) {
        mix_fit(&fit[k], k, &train);
        double train_fights, test_fights;
        double ll_train = mix_score(&fit[k], &train, &train_fights), ll_test = mix_score(&fit[k], &test, &test_fights);
        printf("  K=%d  train %9.2f (%.0f fights)  held out %9.2f (%.0f fights)\n", k, ll_train, train_fights, ll_test, test_fights);
        mix_print("fit", &fit[k]);
    }
    if (getenv("GS_MIX_SAVE_TRAIN")) {
        FILE* f = fopen(getenv("GS_MIX_SAVE_TRAIN"), "wb");
        fwrite(&fit[getenv("GS_TYPES") ? atoi(getenv("GS_TYPES")) : 2], sizeof(EatMixture), 1, f);
        fclose(f);
    }
    printf("=== eater types, own fights: all tapes (in sample) ===\n");
    static EatMixture full[MAX_TYPES + 1];
    for (int k = 1; k <= 3; k++) {
        mix_fit(&full[k], k, &all);
        double fights;
        printf("  K=%d  %9.2f (%.0f fights)\n", k, mix_score(&full[k], &all, &fights), fights);
        mix_print("fit", &full[k]);
    }
    for (int k = 2; k <= 3; k++) {
        mix_separation(&full[k], &all, k == 2 ? "own fights K=2" : "own fights K=3", 0);
        mix_rates(&full[k], &all, k == 2 ? "own fights K=2" : "own fights K=3");
    }
    if (save) {
        FILE* f = fopen(save, "wb");
        fwrite(&full[atoi(getenv("GS_TYPES") ? getenv("GS_TYPES") : "2")], sizeof(EatMixture), 1, f);
        fclose(f);
    }
    if (spectator_path) {
        spec_load(&corpus, spectator_path);
        spec_prepare();
        printf("=== eater types, spectator corpus (non-Dharok), %d sessions: leave the last session out ===\n", corpus.session_count);
        static EatRows s_train, s_test, s_all;
        spec_eat_rows(&s_train, -1, corpus.session_count - 1, 0);
        spec_eat_rows(&s_test, corpus.session_count - 1, -1, 0);
        spec_eat_rows(&s_all, -1, -1, 0);
        static EatMixture sfit[MAX_TYPES + 1], sfull[MAX_TYPES + 1];
        for (int k = 1; k <= 3; k++) {
            mix_fit(&sfit[k], k, &s_train);
            double tf, hf;
            double lt = mix_score(&sfit[k], &s_train, &tf), lh = mix_score(&sfit[k], &s_test, &hf);
            printf("  K=%d  train %10.2f (%.0f fighter-duels)  held out %10.2f (%.0f)\n", k, lt, tf, lh, hf);
            mix_print("fit", &sfit[k]);
        }
        for (int k = 2; k <= 3; k++) {
            mix_fit(&sfull[k], k, &s_all);
            mix_separation(&sfull[k], &s_all, k == 2 ? "spectator K=2" : "spectator K=3", 0);
            mix_rates(&sfull[k], &s_all, k == 2 ? "spectator K=2" : "spectator K=3");
        }
    }
    free(sim);
}

int main(int argc, char** argv) {
    assert(argc >= 4);
    static Tape tapes[MAX_TAPES];
    const char* mode = argv[1];
    g_ablation.hp = HP_HIDDEN;
    g_ablation.escape_model = 1;
    g_ablation.his_defence = 99;
    g_ablation.panic = 0.8;
    g_ablation.panic_hp = 25;
    g_unseen_ticks = unseen_ticks;
    const char* ablation = getenv("GS_ABL");
    if (ablation) {
        g_ablation.hp = strstr(ablation, "hp=hidden") ? HP_HIDDEN : strstr(ablation, "hp=uniform") ? HP_UNIFORM : strstr(ablation, "hp=oracle") ? HP_ORACLE : HP_HIDDEN;
        const char* panic = strstr(ablation, "panic=");
        g_ablation.panic = panic ? atof(panic + 6) : 0.8;
        const char* panic_hp = strstr(ablation, "panichp=");
        g_ablation.panic_hp = panic_hp ? atoi(panic_hp + 8) : 25;
        const char* def = strstr(ablation, "defence=");
        g_ablation.his_defence = def ? atoi(def + 8) : 99;
        g_ablation.no_piety = strstr(ablation, "nopiety") != NULL;
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
    int first = !strcmp(mode, "autopsy") ? 5 : !strcmp(mode, "eatstudy") ? 4 : !strcmp(mode, "eatmix") ? 5 : !strcmp(mode, "spectator") ? 4 : !strcmp(mode, "rollouts") || !strcmp(mode, "attribute") || !strcmp(mode, "ablate") ? 6 : 4;
    int count = 0;
    for (int i = first; i < argc; i++) { tape_load(&tapes[count], argv[i]); prepare(&tapes[count]); count++; }
    if (!strcmp(mode, "spectator")) { spec_load(&corpus, argv[2]); spectator(tapes, count, argv[3]); }
    else if (!strcmp(mode, "teacher")) teacher(tapes, count, atoi(argv[2]), atoi(argv[3]));
    else if (!strcmp(mode, "rollouts")) rollouts(atoi(argv[2]), atoi(argv[3]), tapes, count, atoi(argv[4]), atoi(argv[5]));
    else if (!strcmp(mode, "attribute")) attribute(atoi(argv[2]), atoi(argv[3]), tapes, count, atoi(argv[4]), atoi(argv[5]));
    else if (!strcmp(mode, "ablate")) ablate(atoi(argv[2]), atoi(argv[3]), tapes, count, atoi(argv[4]), atoi(argv[5]));
    else if (!strcmp(mode, "eatmix")) eat_mixture_study(tapes, count, atoi(argv[2]), atoi(argv[3]), strcmp(argv[4], "-") ? argv[4] : NULL, getenv("GS_MIX_SAVE"));
    else if (!strcmp(mode, "eatstudy")) eat_study(tapes, count, atoi(argv[2]), atoi(argv[3]));
    else if (!strcmp(mode, "autopsy")) autopsy(atoi(argv[2]), atoi(argv[3]), tapes, count, atoi(argv[4]));
    else if (!strcmp(mode, "branch")) branch(atoi(argv[2]), atoi(argv[3]), tapes, count);
    else abort();
    return 0;
}
