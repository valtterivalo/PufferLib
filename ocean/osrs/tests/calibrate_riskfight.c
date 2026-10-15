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

enum { MAX_TAPES = 2048, METRICS = 12, PIT_BINS = 10, REL_BINS = 7, EPISODE_BINS = 9, MAX_ROLLOUTS = 4096,
    FIT_SAMPLES = 4, SCORE_SAMPLES = 16, PROFILE_SAMPLES = 64, BIG_HIT_EPISODE = 30, REARM_TICKS = 4, MARGIN = 10 };
enum { FLAG_EAT = 1, FLAG_SWING = 2, FLAG_VW = 4, FLAG_MAUL = 8, FLAG_VENG = 16, FLAG_SWING_UNKNOWN = 32 };

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
        if (a->side != SIDE_HIM || a->tick < tape->lo || a->tick > tape->hi || a->anim == -1) continue;
        uint8_t* f = &tape->his_flags[a->tick - tape->lo];
        if (a->anim == ANIM_EAT) *f |= FLAG_EAT;
        if (a->anim == ANIM_VW_SPEC) *f |= FLAG_VW;
        if (a->anim == ANIM_MAUL_SPEC) *f |= FLAG_MAUL;
        if (a->anim == ANIM_VENG_A || a->anim == ANIM_VENG_B) *f |= FLAG_VENG;
        const GsAnimationRow* row = gs_animation_row(a->anim);
        if (!row) *f |= FLAG_SWING_UNKNOWN;
        else if (row->kind == GS_KIND_ATTACK) *f |= FLAG_SWING;
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
        case ACT_SWING: return (Conditioned){probe->eligible[a] && !eat && !(flags & FLAG_SWING_UNKNOWN), swing != 0};
        case ACT_VW_SPEC: return (Conditioned){probe->eligible[a] && swing && !(flags & FLAG_SWING_UNKNOWN), (flags & FLAG_VW) != 0};
        case ACT_MAUL_SPEC: return (Conditioned){probe->eligible[a], (flags & FLAG_MAUL) != 0};
        case ACT_VENGEANCE: return (Conditioned){probe->eligible[a], (flags & FLAG_VENG) != 0};
        case ACT_COUNT: break;
    }
    abort();
}

typedef enum { CLOCK_IRRELEVANT, CLOCK_REQUIRE_EXACT } ClockRequirement;
typedef enum { CLOCK_EXACT, CLOCK_UNKNOWN, CLOCK_INTERVAL, CLOCK_OUTSIDE, CLOCK_STATUS_COUNT } ClockStatus;
typedef enum { CLOCK_TEACHER, CLOCK_WINDOWS, CLOCK_EPISODES, CLOCK_WHOLE_FIGHT, CLOCK_BRANCH, CLOCK_BRANCHPOINT,
    CLOCK_SPEC, CLOCK_OPPSTEP, CLOCK_LEAKCHECK, CLOCK_CALIBRATION, CLOCK_EAT_FEATURES, CLOCK_HAZARD_INPUTS, CLOCK_EAT_HP_INPUTS, CLOCK_COHORT_COUNT } ClockCohort;
static const char* CLOCK_COHORT_NAME[CLOCK_COHORT_COUNT] = {"teacher_profiles", "windows", "episodes", "whole_fight", "branch", "branchpoint",
    "spec_points", "oppstep_points", "leakcheck", "calibration_points", "eat_timer_features", "hazard_inputs", "eat_hp_inputs"};
static const char* CLOCK_STATUS_NAME[CLOCK_STATUS_COUNT] = {"exact", "unknown", "interval", "outside"};
static long clock_counts[CLOCK_COHORT_COUNT][CLOCK_STATUS_COUNT];

static ClockStatus clock_status(const Tape* tape, int t) {
    if (t < tape->lo || t > tape->hi) return CLOCK_OUTSIDE;
    if (!tape->observer_series) return CLOCK_UNKNOWN;
    GsTimerState timer = tape->observer_series[t - tape->lo].inf.local_timer;
    return !timer.seen ? CLOCK_UNKNOWN : timer.earliest != timer.latest ? CLOCK_INTERVAL : CLOCK_EXACT;
}

static int clock_eligible(const Tape* tape, int t, ClockCohort cohort) {
    ClockStatus status = clock_status(tape, t);
    clock_counts[cohort][status]++;
    int loads_snapshot = cohort != CLOCK_EAT_FEATURES && cohort != CLOCK_HAZARD_INPUTS && cohort != CLOCK_EAT_HP_INPUTS;
    if (status == CLOCK_EXACT && loads_snapshot && !tape_pending_available(tape, t)) {
        printf("pending unavailable %s %s tick %d reason missing_or_unsupported_observed_queue\n", CLOCK_COHORT_NAME[cohort], tape->name, t);
        return 0;
    }
    return status == CLOCK_EXACT;
}

static long gear_counts[CLOCK_COHORT_COUNT][LA_GEAR_STATUS_COUNT];

static int gear_eligible(const Tape* tape, int t, ClockCohort cohort) {
    GsObservedGear gear = tape->observer_series[t - tape->lo].inf.c.opponent_gear;
    LaGearIssue issue = la_gear_status(gear);
    LaGearStatus status = issue.status;
    gear_counts[cohort][status]++;
    if (status != LA_GEAR_SUPPORTED)
        printf("gear unavailable %s %s tick %d reason %s weapon %d shield %d\n", CLOCK_COHORT_NAME[cohort], tape->name, t, LA_GEAR_STATUS_NAME[status], gear.weapon, gear.shield);
    if (status == LA_GEAR_ARMOR_UNSUPPORTED) printf("  slot %s raw %d\n", GS_ARMOR_SLOT_NAME[issue.slot], issue.raw);
    return status == LA_GEAR_SUPPORTED;
}

static int clock_anchor(const Tape* tape, int start) {
    int t = start;
    while (t <= tape->hi && !clock_eligible(tape, t, CLOCK_WHOLE_FIGHT)) t++;
    printf("clock anchor %s requested %d selected %d unavailable_prefix %d source_frames %d\n", tape->name, start,
        t <= tape->hi ? t : -1, t - start, tape->hi - tape->lo + 1);
    return t <= tape->hi ? t : -1;
}

static int snapshot_has_item(const RiskfightLoadout* loadout, int item) {
    for (int i = 0; i < NUM_GEAR_SLOTS; i++) if (loadout->equipment[i] == item) return 1;
    for (int i = 0; i < OSRS_INVENTORY_SIZE; i++) {
        OsrsInventoryCell cell = osrs_inventory_cell_from_content_code(loadout->inventory[i]);
        if (osrs_inventory_cell_metadata(&cell)->item_idx == item) return 1;
    }
    return 0;
}

static Probe hidden_probe(const Tape* tape, int t, int sample, RiskfightSnapshot* snap) {
    uint32_t rng = osrs_lowbias32((uint32_t)(t * 7919 + sample * 104729 + tape->index * 13 + tape->lo)) | 1;
    (void)tape_snapshot(tape, t - 1, &DEFAULT_PRIORS, &rng, snap); /* Pure resource probe; does not load the pending queue. */
    const RiskfightSideSnapshot* him = &snap->side[1];
    HimInputs in = {
        .hp_ratio = osrs_health_bar_ratio(him->hitpoints, 99, 30),
        .readiness = tape->observer_series ? gs_timer_reading(tape->observer_series[t - 1 - tape->lo].inf.opponent.timer, t) : GS_READING_UNKNOWN,
        .our_bar_ratio = osrs_health_bar_ratio(snap->side[0].hitpoints, MAXED_BASE_HITPOINTS, OSRS_PLAYER_HEALTH_BAR_SCALE),
        .vw_ok = snapshot_has_item(&snap->loadout[1], ITEM_VOIDWAKER) && him->special_energy >= ENERGY_FOR_SPEC,
        .maul_ok = (snapshot_has_item(&snap->loadout[1], ITEM_GRANITE_MAUL) || snapshot_has_item(&snap->loadout[1], ITEM_GRANITE_MAUL_ORNATE)) && him->special_energy >= ENERGY_FOR_SPEC,
        .veng_ok = !him->veng_active && him->veng_cooldown <= 0,
    };
    const Hist* hist = &((const Hist*)tape->hist_series)[t - 1 - tape->lo];
    return probe_from_inputs(&in, hist, t);
}

typedef struct { Sample* samples; int count, capacity; } SampleSet;

static void fit_model(Tape* tapes, int count, int day, OppFit* fit) {
    static SampleSet sets[ACT_COUNT];
    for (int a = 0; a < ACT_COUNT; a++) sets[a].count = 0;
    for (int i = 0; i < count; i++) {
        if (!in_day(&tapes[i], day)) continue;
        g_tape = &tapes[i];
        for (int t = tapes[i].lo + 1; t <= tapes[i].hi; t++) {
            int flags = tapes[i].his_flags[t - tapes[i].lo];
            clock_counts[CLOCK_HAZARD_INPUTS][clock_status(&tapes[i], t - 1)]++;
            for (int j = 0; j < FIT_SAMPLES; j++) {
                RiskfightSnapshot snap;
                Probe probe = hidden_probe(&tapes[i], t, j, &snap);
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
            if (before->last_bar_hit < imax(tape->lo, e - BEFORE_WINDOW) || tape_tick(tape, before->last_bar_hit)->bar_now_ratio < 0 || before->meals_since != 0 || before->bar_ratio <= 0 || before->bar_ratio >= before->bar_scale) continue;
            int hit_now = 0, after = -1, damage = 0;
            for (int x = e; x <= e + SERVING_WINDOW && after < 0; x++) {
                if (x > e && rec_eat_at(tape, x)) break;
                for (int h = 0; h < tape->hit_count; h++) {
                    const TapeHit* hit = &tape->hits[h];
                    if (hit->tick != x || hit->side != SIDE_HIM) continue;
                    if (x == e) hit_now = 1;
                    damage += hit->type == 65 ? 0 : hit->amount;
                    if (tape_tick(tape, x)->bar_now_ratio >= 0) after = x;
                }
            }
            if (hit_now || after < 0) continue;
            const TapeTick* later = tape_tick(tape, after);
            if (later->bar_now_ratio <= 0 || later->bar_now_ratio >= later->bar_now_scale) continue;
            int heal = bar_mid(later->bar_now_ratio, later->bar_now_scale) + damage - bar_mid(before->bar_ratio, before->bar_scale);
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
    if (model->n == 0) {
        printf("  %-24s no eligible observations\n", action);
        return;
    }
    double rate = model->y / model->n;
    double constant = -model->n * (rate * log(clip(train_rate)) + (1 - rate) * log(1 - clip(train_rate)));
    printf("  %-24s %5.0f ticks %4.0f events, rate %.3f (train %.3f) | logloss/tick fit %.4f const %.4f (%+.1f%%) profiles ",
        action, model->n, model->y, rate, train_rate, model->logloss / model->n, constant / model->n,
        100 * (constant - model->logloss) / constant);
    if (profiles->n == 0) printf("no exact-clock observations");
    else printf("%.4f (%g exact-clock ticks)", profiles->logloss / profiles->n, profiles->n);
    printf(" | Brier fit %.4f const %.4f\n", model->brier / model->n,
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

static int g_quiet;
static const Tape* g_only_tape;
static Score g_last_fitted[ACT_COUNT];

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
        if (g_only_tape ? tape != g_only_tape : tape_day(tape) != test_day) continue;
        g_tape = tape;
        for (int t = tape->lo + 1; t <= tape->hi; t++) {
            int flags = tape->his_flags[t - tape->lo];
            Probe probes[SCORE_SAMPLES];
            for (int j = 0; j < SCORE_SAMPLES; j++) {
                RiskfightSnapshot snap;
                probes[j] = hidden_probe(tape, t, j, &snap);
            }
            int profile_available = clock_eligible(tape, t - 1, CLOCK_TEACHER);
            int eat = 0, swing_no_eat = 0, no_eat = 0, swing = 0, vw = 0, maul = 0, veng = 0;
            for (int j = 0; profile_available && j < PROFILE_SAMPLES; j++) {
                uint32_t rng = osrs_lowbias32((uint32_t)(t * 31 + j * 104729 + tape->index)) | 1;
                RiskfightSnapshot snap;
                if (!tape_snapshot(tape, t - 1, &DEFAULT_PRIORS, &rng, &snap)) continue;
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
                if (profile_available) score_add(&profiles[a], q[a], c.y);
            }
        }
    }
    memcpy(g_last_fitted, fitted, sizeof(fitted));
    if (g_quiet) { sim_clear(sim); free(sim); return; }
    printf("test day %d, per action (const = the train-day rate over eligible ticks, profiles = the existing mixture sampled 64 times)\n", test_day);
    for (int a = 0; a < ACT_COUNT; a++)
        print_score(ACTION_NAME[a], &fitted[a], &profiles[a], (double)fit.logit[a].events / fit.logit[a].opportunities);
    sim_clear(sim); free(sim);
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
static double g_haz_exposure[3], g_haz_deaths[3];
static double g_hb_hist[10], g_hb_n, g_hb_sum, g_hb_tails, g_hb_mean_diff, g_hb_band_n[3], g_hb_band_pit[3], g_hb_band_diff[3];

/* The first recorded hit on him in the window with a health bar after it: the tick and the hitpoints that leave him at, plus the damage, as the PIT of the recordings reads them. */
static int recorded_first_hit(const Tape* tape, int t0, int k, int* tick, int* low, int* high) {
    for (int x = t0 + 1; x <= t0 + k && x <= tape->hi; x++) {
        int damage = tape_damage_on_him(tape, x);
        const TapeTick* after = tape_tick(tape, x);
        if (damage > 0 && after->bar_now_ratio > 0 && after->bar_now_scale > 1 && after->last_bar_hit == x) {
            OsrsHealthBarRange shown = osrs_health_bar_range(after->bar_now_ratio, after->bar_now_scale, 99, 121);
            *tick = x;
            *low = shown.lower + damage;
            *high = after->bar_now_ratio == after->bar_now_scale ? (shown.upper + damage > 121 ? shown.upper + damage : 121) : shown.upper + damage;
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
        if (!clock_eligible(tape, t0, CLOCK_WINDOWS)) continue;
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
            if (!tape_snapshot(tape, t0, &DEFAULT_PRIORS, &rng, &snap)) continue;
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
                OsrsHealthBarRange shown = osrs_health_bar_range(after->bar_now_ratio, after->bar_now_scale, 99, 121);
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
    sim_clear(sim); free(sim);
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
        if (!clock_eligible(tape, x, CLOCK_EPISODES)) continue;
        int e = x + 1;
        while (e <= x + 8 && !rec_eat_at(tape, e)) e++;
        int d = e - u;
        stats->compared++;
        stats->recorded[e > x + 8 || d > 7 ? 8 : d]++;
        for (int m = 0; m < 3; m++)
            for (int i = 0; i < n; i++) {
                uint32_t rng = osrs_lowbias32((uint32_t)(x * 7919 + i * 104729 + tape->index * 13)) | 1;
                RiskfightSnapshot snap;
                if (!tape_snapshot(tape, x, &DEFAULT_PRIORS, &rng, &snap)) continue;
                riskfight_load_state(&sim->state, &sim->context, &snap);
                Outcome out;
                run_steps(sim, tape, x, 8, &models[m], ours, xorshift32(&rng), &out);
                int delay = out.first_eat < 0 ? 8 : x + 1 + out.first_eat - u;
                stats->sim[m][delay > 7 ? 8 : delay] += 1.0 / n;
            }
    }
    sim_clear(sim); free(sim);
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
            int t0 = clock_anchor(tp, tp->lo + 1);
            if (t0 < 0) { sim_clear(sim); free(sim); continue; }
            int kk = tp->hi - t0;
            assert(kk <= MAX_STEPS);
            double hd = 0, od = 0, meals = 0, swings = 0, specs = 0, ticks = 0, hd_first = 0, rr = 0;
            for (int r = 0; r < n; r++) {
                uint32_t rng = osrs_lowbias32((uint32_t)(t0 * 7919 + r * 104729 + tp->index * 13 + tp->lo)) | 1;
                RiskfightSnapshot snap;
                if (!tape_snapshot(tp, t0, &DEFAULT_PRIORS, &rng, &snap)) continue;
                riskfight_load_state(&sim->state, &sim->context, &snap);
                Outcome out;
                run_steps(sim, tp, t0, kk, &model, getenv("GS_REPLAY") ? &replay : &rules, xorshift32(&rng), &out);
                for (int j = 0; j < out.steps; j++) {
                    int before = j > 0 ? out.trace_hp[j - 1] : out.start_hp;
                    int band = before <= 44 ? 0 : before <= 87 ? 1 : 2;
                    if (out.his_death >= 0 && j > out.his_death) break;
                    g_haz_exposure[band]++;
                    if (j == out.his_death) g_haz_deaths[band]++;
                }
                hd += out.his_death >= 0; od += out.our_death >= 0; meals += out.our_meals; ticks += out.steps;
                for (int j = 0; j < out.steps; j++) { swings += out.trace_our_swing[j]; specs += out.trace_our_spec[j]; }
                rr = sim->obs.stakes.us > 0 ? (double)sim->obs.stakes.him / sim->obs.stakes.us : 1;
                (void)hd_first;
            }
            printf("FIGHT %s rollouts %d his_dead %.4f our_dead %.4f meals %.3f swings %.3f specs %.3f ticks %.2f r %.3f\n", tp->name, n, hd / n, od / n, meals / n, swings / n, specs / n, ticks / n, rr);
            sim_clear(sim); free(sim);
        }
        printf("HAZARD by his hp before the tick (44-, 45-87, 88+), exposure ticks / deaths:");
        for (int b = 0; b < 3; b++) printf(" %.0f / %.1f", g_haz_exposure[b] / n, g_haz_deaths[b] / n);
        printf("\n");
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
    for (int c = 1; c < GS_CHOICE_COUNT; c++) if (g_pc_n[c]) { printf("specs thrown per decision %.2f; ", g_pc_specs[c] / g_pc_n[c]);
        printf("choice %d (1 vw, 2 maul+vw, 3 maul, 4 vw burst): n %.0f planned mean damage %.1f realized damage in the plan window %.1f, first hit planned +%.2f realized +%.2f, any damage %.3f, planner chance %.3f he dies %.3f\n", c, g_pc_n[c], g_pc_plan[c] / g_pc_n[c], g_pc_act[c] / g_pc_n[c], g_pc_land[c] / g_pc_n[c], g_pc_actland[c] / g_pc_n[c], g_pc_any[c] / g_pc_n[c], g_pc_chance[c] / g_pc_n[c], g_pc_dead[c] / g_pc_n[c]); }
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
                    (void)tape_snapshot(&tapes[i], t, &DEFAULT_PRIORS, &rng, &snap); /* HP marginal only. */
                    recorded_ticks[imin(12, imax(0, snap.loadout[1].hitpoints) / 10)]++;
                }
        }
        sim_clear(seeds); free(seeds);
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

static void eat_rows_add(EatRows* rows, Tape* tapes, int count, int day, ClockRequirement clock_requirement) {
    for (int i = 0; i < count; i++) {
        if (!in_day(&tapes[i], day)) continue;
        g_tape = &tapes[i];
        for (int t = tapes[i].lo + 1; t <= tapes[i].hi; t++) {
            int flags = tapes[i].his_flags[t - tapes[i].lo];
            if (clock_requirement == CLOCK_REQUIRE_EXACT && !clock_eligible(&tapes[i], t - 1, CLOCK_EAT_FEATURES)) continue;
            if (clock_requirement == CLOCK_IRRELEVANT) clock_counts[CLOCK_EAT_HP_INPUTS][clock_status(&tapes[i], t - 1)]++;
            for (int j = 0; j < FIT_SAMPLES; j++) {
                RiskfightSnapshot snap;
                Probe probe = hidden_probe(&tapes[i], t, j, &snap);
                if (!probe.eligible[ACT_EAT_WARNING]) continue;
                if (rows->count == rows->capacity) { rows->capacity = rows->capacity ? rows->capacity * 2 : 8192; rows->rows = realloc(rows->rows, rows->capacity * sizeof(EatRow)); }
                EatRow* r = &rows->rows[rows->count++];
                r->base_n = probe.x[ACT_EAT_WARNING].n;
                for (int k = 0; k < r->base_n; k++) r->base[k] = probe.x[ACT_EAT_WARNING].idx[k];
                r->hp = snap.side[1].hitpoints;
                r->timer = clock_status(&tapes[i], t - 1) == CLOCK_EXACT ? imax(0, snap.side[0].attack_timer_uncapped) : INT_MIN;
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
    static EatRows rows;
    rows.count = 0;
    eat_rows_add(&rows, tapes, count, atoi(getenv("GS_EAT_TRAIN")), g_eat_model.model >= EAT_SWING_TIMER ? CLOCK_REQUIRE_EXACT : CLOCK_IRRELEVANT);
    int kept = 0;
    for (int i = 0; i < rows.count; i++) if (!rows.rows[i].masked) rows.rows[kept++] = rows.rows[i];
    rows.count = kept;
    eat_fit(&g_eat_model.logit, g_eat_model.model, &rows);
    const char* save = getenv("GS_EAT_SAVE");
    if (save) {
        FILE* f = fopen(save, "wb");
        fwrite(&g_eat_model.logit, sizeof(g_eat_model.logit), 1, f);
        fclose(f);
    }
}

static void eat_study(Tape* tapes, int count, int train_day, int test_day) {
    static EatRows train, test;
    train.count = test.count = 0;
    eat_rows_add(&train, tapes, count, train_day, CLOCK_REQUIRE_EXACT);
    eat_rows_add(&test, tapes, count, test_day, CLOCK_REQUIRE_EXACT);
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
        g_tape = tape;
        int t0 = tape->lo + (tape->hi - tape->lo) / 2;
        if (!clock_eligible(tape, t0, CLOCK_BRANCH)) continue;
        uint32_t rng = 99;
        RiskfightSnapshot snap;
        if (!tape_snapshot(tape, t0, &DEFAULT_PRIORS, &rng, &snap)) continue;
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
    sim_clear(sim); free(sim);
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
                    (labels & SPEC_MAUL ? FLAG_MAUL : 0) | (labels & SPEC_VENG ? FLAG_VENG : 0) |
                    (labels & SPEC_SWING_UNKNOWN ? FLAG_SWING_UNKNOWN : 0);
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
    static EatRows train, test, all;
    eat_rows_add(&train, tapes, count, train_day, CLOCK_IRRELEVANT);
    eat_rows_add(&test, tapes, count, test_day, CLOCK_IRRELEVANT);
    eat_rows_add(&all, tapes, count, 0, CLOCK_IRRELEVANT);
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
}

static int cmp_double(const void* a, const void* b) { double x = *(const double*)a, y = *(const double*)b; return (x > y) - (x < y); }

enum { CMP_METRICS = 9 };
static const char* const CMP_NAME[CMP_METRICS] = {"kills per fight", "deaths per fight", "specs thrown", "damage dealt", "damage taken", "meals", "ticks", "swings", "kills without dying"};

static void fight_metrics(const Outcome* out, double m[CMP_METRICS]) {
    double specs = 0, swings = 0;
    for (int j = 0; j < out->steps; j++) { specs += out->trace_our_spec[j]; swings += out->trace_our_swing[j]; }
    m[0] = out->his_death >= 0; m[1] = out->our_death >= 0; m[2] = specs; m[3] = out->our_damage; m[4] = out->his_damage; m[5] = out->our_meals; m[6] = out->steps; m[7] = swings;
    m[8] = out->his_death >= 0 && out->our_death < 0;
}

static void leakcheck(Tape* tapes, int count);

static void fightcmp(int n, Tape* tapes, int count, int test_day) {
    leakcheck(tapes, count);
    g_abort_per_8 = 0;
    if (getenv("GS_EAT_RISK")) g_eat_risk = atof(getenv("GS_EAT_RISK"));
    if (getenv("GS_LA_N")) g_la_worlds = atoi(getenv("GS_LA_N"));
    if (getenv("GS_LA_H")) g_la_horizon = atoi(getenv("GS_LA_H"));
    if (getenv("GS_LA_STAGE")) g_la_stage = atoi(getenv("GS_LA_STAGE"));
    if (getenv("GS_LA_PRUNE")) g_la_prune = atof(getenv("GS_LA_PRUNE"));
    if (getenv("GS_LA_CACHE")) g_la_cache = atoi(getenv("GS_LA_CACHE"));
    if (getenv("GS_LA_EAT_SCALE")) g_la_eat_scale_belief = atof(getenv("GS_LA_EAT_SCALE"));
    if (getenv("GS_LA_HP_SHIFT")) g_la_belief_hp_shift = atof(getenv("GS_LA_HP_SHIFT"));
    if (getenv("GS_LA_BURST")) g_la_burst = atoi(getenv("GS_LA_BURST"));
    if (getenv("GS_LA_WORTH_MODE")) g_la_worth_mode = atoi(getenv("GS_LA_WORTH_MODE"));
    if (getenv("GS_LA_WORTH")) g_la_worth_scale = atof(getenv("GS_LA_WORTH"));
    if (getenv("GS_LA_BOOST")) g_la_boost = atoi(getenv("GS_LA_BOOST"));
    if (getenv("GS_HIS_BOOST")) g_his_boost = atoi(getenv("GS_HIS_BOOST"));
    double world_eat_scale = getenv("GS_EAT_SCALE") ? atof(getenv("GS_EAT_SCALE")) : 1;
    static OppFit fit;
    memset(&fit, 0, sizeof(fit));
    fit_default_servings(&fit);
    if (getenv("GS_FIT")) fit_override(&fit);
    else {
        int eats, identified[2];
        fit_model(tapes, count, 0, &fit);
        infer_servings(tapes, count, 0, &fit, &eats, identified);
    }
    fit.eat_action = ACT_EAT_WARNING;
    eat_model_setup(tapes, count);
    assert_fitted(&fit, "fightcmp");
    OppModel model = {MODEL_FIT, &fit};
    g_la_model = &model;
    OursPolicy policies[2] = {{OURS_RULES, 0, TIMING_MEASURED, STAKES_COMPUTED}, {OURS_LOOKAHEAD, 0, TIMING_MEASURED, STAKES_COMPUTED}};
    double sum[2][CMP_METRICS] = {{0}}, diff[CMP_METRICS] = {0}, diff2[CMP_METRICS] = {0}, seconds_policy[2] = {0};
    long rollouts = 0;
    static double calib_n[LA_MACROS][2], calib_predicted[LA_MACROS][2], calib_realized[LA_MACROS][2];
    Sim* sim = malloc(sizeof(Sim));
    sim_init(sim);
    for (int i = 0; i < count; i++) {
        if (!in_day(&tapes[i], test_day) || (getenv("GS_ONLY") && !strstr(tapes[i].name, getenv("GS_ONLY")))) continue;
        const Tape* tp = &tapes[i];
        g_tape = tp;
        int t0 = clock_anchor(tp, tp->lo + 1);
        if (t0 < 0) continue;
        int kk = tp->hi - t0;
        assert(kk <= MAX_STEPS);
        double fight[2][CMP_METRICS] = {{0}};
        for (int r = 0; r < n; r++) {
            uint32_t rng = osrs_lowbias32((uint32_t)(t0 * 7919 + r * 104729 + tp->index * 13 + tp->lo)) | 1;
            RiskfightSnapshot snap;
            if (!tape_snapshot(tp, t0, &DEFAULT_PRIORS, &rng, &snap)) continue;
            uint32_t seed = xorshift32(&rng);
            double m[2][CMP_METRICS];
            for (int p = 0; p < 2; p++) {
                if (getenv("GS_ARM") && atoi(getenv("GS_ARM")) != p) { memset(m[p], 0, sizeof(m[p])); continue; }
                riskfight_load_state(&sim->state, &sim->context, &snap);
                memset(g_la_memo, 0, sizeof(g_la_memo));
                g_eat_scale = world_eat_scale;
                double started = seconds();
                Outcome* out = malloc(sizeof(Outcome));
                g_la_decision_count = 0;
                run_steps(sim, tp, t0, kk, &model, &policies[p], seed, out);
                if (p == 1)
                    for (int d = 0; d < g_la_decision_count; d++) {
                        int macro = g_la_decisions[d].macro, with_spec = g_la_decisions[d].spec, step = g_la_decisions[d].step;
                        int realized = out->his_death >= step && out->his_death <= step + g_la_horizon;
                        calib_n[macro][with_spec]++; calib_predicted[macro][with_spec] += g_la_decisions[d].kill; calib_realized[macro][with_spec] += realized;
                    }
                seconds_policy[p] += seconds() - started;
                fight_metrics(out, m[p]);
                free(out);
            }
            rollouts++;
            for (int k = 0; k < CMP_METRICS; k++) {
                sum[0][k] += m[0][k]; sum[1][k] += m[1][k];
                fight[0][k] += m[0][k]; fight[1][k] += m[1][k];
                double d = m[1][k] - m[0][k];
                diff[k] += d; diff2[k] += d * d;
            }
        }
        printf("FIGHT %-44s rules kill %.3f die %.3f specs %.2f | lookahead kill %.3f die %.3f specs %.2f\n", tp->name, fight[0][0] / n, fight[0][1] / n, fight[0][2] / n,
            fight[1][0] / n, fight[1][1] / n, fight[1][2] / n);
        fflush(stdout);
    }
    printf("N=%d worlds=%d horizon=%d stage=%d prune=%.3f cache=%d eat_scale world %.2f planner %.2f rollouts %ld (paired, same snapshot and seed)\n", n, g_la_worlds, g_la_horizon, g_la_stage, g_la_prune,
        g_la_cache, world_eat_scale, g_la_eat_scale_belief, rollouts);
    printf("%-26s %10s %10s %10s %18s\n", "metric", "rules", "lookahead", "diff", "95% CI of diff");
    for (int k = 0; k < CMP_METRICS; k++) {
        double mean = diff[k] / rollouts, var = (diff2[k] - rollouts * mean * mean) / (rollouts - 1), se = sqrt(var / rollouts);
        printf("%-26s %10.4f %10.4f %+10.4f   [%+.4f, %+.4f]\n", CMP_NAME[k], sum[0][k] / rollouts, sum[1][k] / rollouts, mean, mean - 1.96 * se, mean + 1.96 * se);
    }
    printf("seconds per rollout-fight: rules %.4f lookahead %.4f\n", seconds_policy[0] / rollouts, seconds_policy[1] / rollouts);
    printf("lookahead calls %ld (%.1f per fight), cache hits %ld, rollouts per call %.1f\n", g_la.calls, (double)g_la.calls / rollouts, g_la.cache_hits, g_la.calls > g_la.cache_hits ? (double)g_la.rollouts / (g_la.calls - g_la.cache_hits) : 0);
    printf("calibration of the lookahead's own kill chance, executed macro, decisions that throw a spec or not: predicted vs realized kill within %d ticks in the sim\n", g_la_horizon);
    for (int m = 0; m < LA_MACROS; m++)
        for (int sp = 0; sp < 2; sp++)
            if (calib_n[m][sp]) printf("  %-14s spec %d: %7.0f decisions, predicted %.4f realized %.4f\n", LA_NAME[m], sp, calib_n[m][sp], calib_predicted[m][sp] / calib_n[m][sp], calib_realized[m][sp] / calib_n[m][sp]);
    printf("macro chosen:");
    for (int m = 0; m < LA_MACROS; m++) printf(" %s %ld,", LA_NAME[m], g_la.chosen[m]);
    printf("\n");
    if (g_la.latency_count) {
        qsort(g_la.latency, (size_t)g_la.latency_count, sizeof(double), cmp_double);
        double mean = g_la.seconds / g_la.latency_count;
        printf("latency per call (one core, includes cache hits): mean %.2f ms p50 %.2f p95 %.2f p99 %.2f max %.2f\n", mean * 1e3, g_la.latency[g_la.latency_count / 2] * 1e3,
            g_la.latency[g_la.latency_count * 95 / 100] * 1e3, g_la.latency[g_la.latency_count * 99 / 100] * 1e3, g_la.worst * 1e3);
    }
    sim_clear(sim); free(sim);
}

static void la_dump_request(FILE* out, const Sim* sim, const LaBelief* b, int tick, const LaFeasible* f, GsStakes stakes, const LaScript* rules) {
    const Player* us = &sim->state.env.players[0];
    const Player* him = &sim->state.env.players[1];
    fprintf(out, "LA %d %d\n", tick, tick);
    fprintf(out, "B %d %d %d %d %d %d %.17g %d %d %d %d %d %d %d %d\n", b->hp_low, b->hp_high, b->energy_low, b->energy_high, b->vengeance, b->pid, b->flip, b->timer_seen,
        b->timer_first, b->timer_last, b->food_ready, b->seen_since_eat, b->seen_vw, b->seen_maul, b->bar_ratio);
    fprintf(out, "K %ld %ld\nF %d %d %d %d\n", stakes.him, stakes.us, f->spec_ok, f->eat_ok, f->veng_ok, f->energy);
    fprintf(out, "RA %s\n", f->pause_ok ? "OFF_PRIOR" : "UNKNOWN");
    int overhead = us->prayer == PRAYER_SMITE ? 1 : us->prayer == PRAYER_PROTECT_MELEE ? 2 : us->prayer == PRAYER_PROTECT_RANGED ? 3 : us->prayer == PRAYER_PROTECT_MAGIC ? 4 : us->prayer == PRAYER_REDEMPTION ? 5 : 0;
    const RiskfightVisibleOpponent* seen = &sim->state.visible[0];
    int his_raw = b->gear.source != GS_GEAR_UNKNOWN ? b->gear.weapon : raw_of_item(him->equipped[GEAR_SLOT_WEAPON]);
    fprintf(out, "O %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d OBSERVED %d %d\n", us->current_hitpoints, us->current_prayer, us->current_attack, us->current_strength,
        us->current_defence, us->current_magic, us->special_energy, us->item_effect_state.special_regen_ticks, us->spec_armed, us->veng_active, us->veng_cooldown,
        sim->state.inventory_use[0].vengeance_sacks, us->item_effect_state.recoil_damage_used, us->attack_timer, us->food_timer, us->potion_timer, us->karambwan_timer,
        us->offensive_prayer == OFFENSIVE_PRAYER_PIETY, overhead, osrs_interaction_active(&us->interaction) ? 0 : -1, 4, seen->last_attack_tick >= 0 ? tick - (seen->last_attack_tick - RISKFIGHT_LOAD_TICK + sim->tick_base) : -1,
        seen->last_attack_speed, his_raw, 0, him->x - us->x, him->y - us->y, us->attack_timer_uncapped, us->attack_timer_uncapped);
    fprintf(out, "W");
    for (int i = 0; i < TAPE_WORN; i++) fprintf(out, " %d", WORN_TO_GEAR[i] < 0 || us->equipped[WORN_TO_GEAR[i]] == ITEM_NONE ? -1 : ITEM_DATABASE[us->equipped[WORN_TO_GEAR[i]]].item_id);
    fprintf(out, "\nG");
    for (int i = 0; i < TAPE_BAG; i++) {
        const OsrsItemContentMetadata* m = osrs_inventory_cell_metadata(&us->inventory_cells[i]);
        static const int BREW[5] = {-1, 6691, 6689, 6687, 6685}, RESTORE[5] = {-1, 3030, 3028, 3026, 3024}, COMBAT[5] = {-1, 12701, 12699, 12697, 12695};
        int raw = m->item_idx != ITEM_NONE ? ITEM_DATABASE[m->item_idx].item_id : -1;
        switch ((OsrsConsumableKind)m->consumable_kind) {
            case OSRS_CONSUMABLE_ANGLERFISH: raw = 13441; break;
            case OSRS_CONSUMABLE_KARAMBWAN: raw = 3144; break;
            case OSRS_CONSUMABLE_BREW: raw = BREW[m->dose_count]; break;
            case OSRS_CONSUMABLE_SUPER_RESTORE: raw = RESTORE[m->dose_count]; break;
            case OSRS_CONSUMABLE_SUPER_COMBAT: raw = COMBAT[m->dose_count]; break;
            case OSRS_CONSUMABLE_VENGEANCE_SACK: raw = 24621; break;
            default: break;
        }
        fprintf(out, " %d", raw);
    }
    if (b->gear.source != GS_GEAR_UNKNOWN) fprintf(out, "\nSHIELD OBSERVED %d", b->gear.shield);
    else fprintf(out, "\nSHIELD UNKNOWN");
    if (b->gear.source == GS_GEAR_COMPOSITION_OBSERVED) {
        fprintf(out, "\nARMOR OBSERVED");
        for (int slot = 0; slot < GS_ARMOR_SLOTS; slot++) fprintf(out, " %d", b->gear.armor[slot]);
    } else fprintf(out, "\nARMOR UNKNOWN");
    {
        int since = warning_since(&sim->hist.warning);
        if (since == WARNING_NONE) fprintf(out, "\nQ none %d", sim->hist.warning.unseen_count); else fprintf(out, "\nQ %d %d", since, sim->hist.warning.unseen_count);
        for (int i = 0; i < sim->hist.warning.unseen_count; i++) fprintf(out, " %d", sim->hist.warning.unseen[i]);
    }
    fprintf(out, "\nH %d %d %d %d %d", sim->hist.big_hit_tick, sim->hist.since_vw_spec, sim->hist.since_maul_spec, sim->hist.ready_age, sim->hist.last_swing_tick);
    int pending_known = 1, pending_count[2] = {0}, variants = sim->state.pending_pid_active ? 2 : 1;
    for (int holder = 0; holder < variants; holder++) for (int source = 0; source < 2; source++) {
        int count = sim->state.pending_pid_active ? sim->state.pending_pid_count[holder][source] : sim->state.env.players[source].num_pending_hits;
        const PendingHit* hits = sim->state.pending_pid_active ? sim->state.pending_pid[holder][source] : sim->state.env.players[source].pending_hits;
        pending_count[holder] += count;
        for (int i = 0; i < count; i++) {
            const PendingHit* h = &hits[i];
            if (h->kind > OSRS_HIT_VENGEANCE || h->freeze_ticks || h->heal_percent || h->drain_type || h->drain_percent || h->flat_heal)
                pending_known = 0;
        }
    }
    if (!pending_known) fprintf(out, "\nP UNKNOWN\n");
    else {
        if (sim->state.pending_pid_active) fprintf(out, "\nP VARIANTS %d %d\n", pending_count[0], pending_count[1]);
        else fprintf(out, "\nP KNOWN %d\n", pending_count[0]);
        for (int holder = 0; holder < variants; holder++) for (int source = 0; source < 2; source++) {
            int count = sim->state.pending_pid_active ? sim->state.pending_pid_count[holder][source] : sim->state.env.players[source].num_pending_hits;
            const PendingHit* hits = sim->state.pending_pid_active ? sim->state.pending_pid[holder][source] : sim->state.env.players[source].pending_hits;
            const PendingHitMetadata* metadata = sim->state.pending_pid_active ? sim->state.pending_pid_metadata[holder][source] : sim->state.env.pending_hit_metadata[source];
            for (int i = 0; i < count; i++) {
                const PendingHit* h = &hits[i];
                const PendingHitMetadata* metadata_at = &metadata[i];
                if (sim->state.pending_pid_active) fprintf(out, "D %d ", holder);
                else fprintf(out, "D ");
                fprintf(out, "%d %d %d %d %d %d %d %d %d %d %d %d %d\n", source, h->ticks_until_hit,
                    h->kind, h->attack_type, h->is_special, h->hit_success, metadata_at->damage_mode, h->damage,
                    metadata_at->damage_min, metadata_at->damage_max, metadata_at->accuracy_bp, h->defender_prayer_at_attack, metadata_at->smite_snapshot - 1);
            }
        }
    }
    if (rules->native_rules) { fprintf(stderr, "a closed-loop rules baseline has no wire form: protocol v2 only carries committed C records\n"); abort(); }
    fprintf(out, "R KNOWN %d\n", rules->n);
    for (int i = 0; i < rules->n; i++) {
        fputc('C', out);
        la_script_emit_record(out, rules, i);
        fputc('\n', out);
    }
    fprintf(out, "END\n");
}

static FILE* g_dump_file;
static Sim* g_dump_sim;
static void dump_to_file(const Sim* sim, const LaBelief* b, int tick, const LaFeasible* f, GsStakes stakes, const LaScript* rules) { la_dump_request(g_dump_file, sim, b, tick, f, stakes, rules); }

static void branchpoint(const char* ticks, int worlds, Tape* tapes, int count) {
    if (getenv("GS_LA_H")) g_la_horizon = atoi(getenv("GS_LA_H"));
    if (getenv("GS_LA_PRUNE")) g_la_prune = atof(getenv("GS_LA_PRUNE"));
    g_la_worlds = worlds;
    g_la_stage = worlds;
    g_la_cache = 0;
    g_la_report = 1;
    if (getenv("GS_LA_DUMP")) { g_dump_file = fopen(getenv("GS_LA_DUMP"), "w"); g_la_dump = dump_to_file; }
    static OppFit fit;
    memset(&fit, 0, sizeof(fit));
    fit_default_servings(&fit);
    if (getenv("GS_FIT")) fit_override(&fit);
    else {
        int eats, identified[2];
        fit_model(tapes, count, 0, &fit);
        infer_servings(tapes, count, 0, &fit, &eats, identified);
    }
    fit.eat_action = ACT_EAT_WARNING;
    eat_model_setup(tapes, count);
    assert_fitted(&fit, "branchpoint");
    OppModel model = {MODEL_FIT, &fit};
    g_la_model = &model;
    Sim* sim = malloc(sizeof(Sim));
    sim_init(sim);
    for (int i = 0; i < count; i++) {
        const Tape* tp = &tapes[i];
        g_tape = tp;
        char list[256];
        strcpy(list, ticks);
        for (char* tok = strtok(list, ","); tok; tok = strtok(NULL, ",")) {
            int t = atoi(tok);
            if (!clock_eligible(tp, t, CLOCK_BRANCHPOINT)) {
                printf("clock unavailable branchpoint %s tick %d reason %s\n", tp->name, t, CLOCK_STATUS_NAME[clock_status(tp, t)]);
                continue;
            }
            if (!gear_eligible(tp, t, CLOCK_BRANCHPOINT)) continue;
            uint32_t rng = osrs_lowbias32((uint32_t)t) | 1;
            RiskfightSnapshot snap;
            if (!tape_snapshot(tp, t, &DEFAULT_PRIORS, &rng, &snap)) continue;
            riskfight_load_state(&sim->state, &sim->context, &snap);
            observer_assign(&sim->obs, &tp->observer_series[t - tp->lo]);
            ours_reset(&sim->ours, rng, TIMING_MEASURED, STAKES_COMPUTED, 0);
            observer_branch(&sim->state, &sim->obs);
            sim->hist = ((const Hist*)tp->hist_series)[t - tp->lo];
            sim->seen_since_eat = 99;
            sim->tick_base = t;
            sim->ours.arena.used = 0;
            printf("%s ", tp->name);
            int moved = la_override(sim, &sim->ours, 0, t);
            printf("  -> %s\n", moved ? "override queued" : "rules stand");
        }
    }
    sim_clear(sim); free(sim);
}

static int recorded_death_tick(const Tape* tape) {
    for (int t = tape->lo; t <= tape->hi; t++) {
        const TapeTick* x = tape_tick(tape, t);
        if (x->bar_ratio == 0 && x->bar_scale > 1 && x->last_bar_hit > 0) return t;
    }
    return -1;
}

/** One line for a new recording: its held-out score under the model fitted on every earlier fight, the refit including it, and the lookahead's
 * predicted against realized kill chance at the spec clicks of the recording and in the sim. The last tape is the new one. */
typedef struct {
    int tick, observed_eat, observed_kill, revealed, above;
    double excess, pred_eat, pred_kill;
} SwingPoint;

static int swing_tick(const Tape* tape, const TapeAnim* an, int horizon, int death) {
    if (an->side != SIDE_US || (an->anim != ANIM_VW_SPEC && an->anim != ANIM_MAUL_SPEC)) return -1;
    int t = an->tick - 1;
    return t >= tape->lo + 1 && (t + horizon <= tape->hi || (death > t && death <= t + horizon)) ? t : -1;
}

/** One point per recorded VW or maul spec swing: eat in the 6 ticks after, kill within the horizon, and the bar band against the next reveal, scored against worlds drawn at the current globals. */
static int swing_points(Tape* tape, const OppModel* model, Sim* sim, Sim* work, SwingPoint* out) {
    enum { WINDOW = 6, WORLDS = 64 };
    const Observer* series = tape->observer_series;
    int death = recorded_death_tick(tape), n = 0;
    g_tape = tape;
    for (int a = 0; a < tape->anim_count; a++) {
        const TapeAnim* an = &tape->anims[a];
        int t = swing_tick(tape, an, g_la_horizon, death);
        if (t < 0) continue;
        if (!clock_eligible(tape, t, CLOCK_SPEC)) continue;
        if (!gear_eligible(tape, t, CLOCK_SPEC)) continue;
        uint32_t rng = osrs_lowbias32((uint32_t)t) | 1;
        RiskfightSnapshot snap;
        if (!tape_snapshot(tape, t, &DEFAULT_PRIORS, &rng, &snap)) continue;
        riskfight_load_state(&sim->state, &sim->context, &snap);
        observer_assign(&sim->obs, &series[t - tape->lo]);
        observer_branch(&sim->state, &sim->obs);
        sim->hist = ((const Hist*)tape->hist_series)[t - tape->lo];
        sim->seen_since_eat = 99;
        sim->tick_base = t;
        LaBelief belief = la_belief_of(sim, t);
        LaScript spec = {0};
        uint32_t press = osrs_lowbias32((uint32_t)t * 2654435761u) | 1;
        const Player* us = &sim->state.env.players[0];
        HumanCommand clicks[2];
        int k = 0;
        if (!us->spec_armed) clicks[k++] = spec_command();
        clicks[k++] = attack_command();
        la_press(&press, &spec, t, clicks, k);
        double pred_eat = 0, pred_kill = 0;
        static RiskfightState world;
        static Hist hist;
        for (int w = 0; w < WORLDS; w++) {
            uint32_t draw = osrs_lowbias32((uint32_t)(t * 40503u + w * 2246822519u)) | 1;
            uint32_t wr = osrs_lowbias32(draw) | 1;
            la_world(&sim->state, &sim->hist, &belief, t, &wr, &world, &hist);
            LaOutcome with = la_rollout(work, &world, &hist, t, model, &spec, osrs_lowbias32(draw + 17));
            pred_eat += with.ate_at >= 0 && with.ate_at < WINDOW;
            pred_kill += with.kill;
        }
        SwingPoint* s = &out[n++];
        *s = (SwingPoint){.tick = t, .pred_eat = pred_eat / WORLDS, .pred_kill = pred_kill / WORLDS};
        for (int x = t + 1; x <= tape->hi && x <= t + WINDOW; x++) s->observed_eat |= tape_his_anim_at(tape, x, ANIM_EAT) != 0;
        s->observed_kill = death > t && death <= t + g_la_horizon;
        int reveal = -1;
        for (int x = t + 1; x <= tape->hi && x <= t + 8; x++) if (tape_tick(tape, x)->last_bar_hit == x && tape_tick(tape, x)->bar_now_ratio > 0) { reveal = x; break; }
        if (reveal >= 0) {
            int damage = 0;
            for (int x = t + 1; x <= reveal; x++) damage += tape_damage_on_him(tape, x);
            const TapeTick* after = tape_tick(tape, reveal);
            int low = warning_bar_low(after->bar_now_ratio, after->bar_now_scale) + damage, high = warning_bar_high(after->bar_now_ratio, after->bar_now_scale) + damage;
            s->revealed = 1;
            s->above = low > belief.hp_high;
            s->excess = (low + high) / 2.0 - (belief.hp_low + belief.hp_high) / 2.0;
        }
    }
    return n;
}

static void swing_summary(const SwingPoint* pts, int n, char* out, size_t size) {
    double eat_o = 0, eat_p = 0, kill_o = 0, kill_p = 0, rev = 0, above = 0, excess = 0;
    for (int i = 0; i < n; i++) {
        eat_o += pts[i].observed_eat; eat_p += pts[i].pred_eat; kill_o += pts[i].observed_kill; kill_p += pts[i].pred_kill;
        rev += pts[i].revealed; above += pts[i].above; excess += pts[i].excess;
    }
    snprintf(out, size, "%d swings eat %.2f/%.2f kill %.3f/%.3f hp above band %.2f excess %+.1f", n, n ? eat_p / n : 0, n ? eat_o / n : 0, n ? kill_p / n : 0, n ? kill_o / n : 0, rev ? above / rev : 0, rev ? excess / rev : 0);
}

static void oppstep(const char* model_path, const char* ledger_path, Tape* tapes, int count) {
    double started = seconds();
    Tape* fresh = &tapes[count - 1];
    g_quiet = 1;
    static OppFit before, after;
    int eats, identified[2];
    memset(&before, 0, sizeof(before));
    fit_default_servings(&before);
    if (!fit_load(model_path, &before)) {
        fit_model(tapes, count - 1, 0, &before);
        infer_servings(tapes, count - 1, 0, &before, &eats, identified);
    }
    g_only_tape = fresh;
    g_tape = fresh;
    teacher_score(tapes, count, &before, 0);
    double loss[ACT_COUNT], constant[ACT_COUNT], events[ACT_COUNT];
    for (int a = 0; a < ACT_COUNT; a++) {
        const Score* s = &g_last_fitted[a];
        double train_rate = (double)before.logit[a].events / before.logit[a].opportunities, rate = s->n ? s->y / s->n : 0;
        events[a] = s->y;
        loss[a] = s->n ? s->logloss / s->n : 0;
        constant[a] = s->n ? -(rate * log(clip(train_rate)) + (1 - rate) * log(1 - clip(train_rate))) : 0;
    }
    g_only_tape = NULL;
    char held[160] = "no swings", corrected[160] = "-";
    {
        static OppFit prior;
        prior = before;
        prior.eat_action = ACT_EAT_WARNING;
        eat_model_setup(tapes, count - 1);
        if (!fit_unfitted(&prior)) {
            OppModel prior_model = {MODEL_FIT, &prior};
            g_la_model = &prior_model;
            g_la_worlds = 64;
            g_la_stage = 64;
            g_la_cache = 0;
            Sim* a = malloc(sizeof(Sim));
            Sim* b = malloc(sizeof(Sim));
            sim_init(a);
            sim_init(b);
            static SwingPoint pts[256];
            int n = swing_points(fresh, &prior_model, a, b, pts);
            swing_summary(pts, n, held, sizeof(held));
            if (getenv("GS_CORR_SHIFT")) {
                double shift = g_la_belief_hp_shift, scale = g_eat_scale;
                g_la_belief_hp_shift = atof(getenv("GS_CORR_SHIFT"));
                g_eat_scale = atof(getenv("GS_CORR_EAT"));
                n = swing_points(fresh, &prior_model, a, b, pts);
                swing_summary(pts, n, corrected, sizeof(corrected));
                g_la_belief_hp_shift = shift;
                g_eat_scale = scale;
            }
            sim_clear(a); free(a);
            sim_clear(b); free(b);
        }
    }
    memset(&after, 0, sizeof(after));
    fit_default_servings(&after);
    fit_model(tapes, count, 0, &after);
    infer_servings(tapes, count, 0, &after, &eats, identified);
    fit_save(model_path, &after);

    OppFit planner = after;
    planner.eat_action = ACT_EAT_WARNING;
    eat_model_setup(tapes, count);
    int cold = fit_unfitted(&planner) != 0;
    if (!cold) assert_fitted(&planner, "oppstep");
    OppModel model = {MODEL_FIT, &planner};
    g_la_model = &model;
    g_la_worlds = 64;
    g_la_stage = 16;
    g_la_cache = 0;
    Sim* sim = malloc(sizeof(Sim));
    sim_init(sim);
    g_tape = fresh;
    int death = recorded_death_tick(fresh);
    double points = 0, predicted = 0, realized = 0, predicted_hold = 0;
    for (int i = 0; !cold && i < fresh->action_count; i++) {
        const TapeAction* a = &fresh->actions[i];
        if (a->kind != ACTION_SPEC) continue;
        int t = a->tick - 1;
        if (t < fresh->lo + 1 || t + g_la_horizon > fresh->hi) continue;
        if (!clock_eligible(fresh, t, CLOCK_OPPSTEP)) continue;
        uint32_t rng = osrs_lowbias32((uint32_t)t) | 1;
        RiskfightSnapshot snap;
        if (!tape_snapshot(fresh, t, &DEFAULT_PRIORS, &rng, &snap)) continue;
        riskfight_load_state(&sim->state, &sim->context, &snap);
        observer_assign(&sim->obs, &fresh->observer_series[t - fresh->lo]);
        ours_reset(&sim->ours, rng, TIMING_MEASURED, STAKES_COMPUTED, 0);
        observer_branch(&sim->state, &sim->obs);
        sim->hist = ((const Hist*)fresh->hist_series)[t - fresh->lo];
        sim->seen_since_eat = 99;
        sim->tick_base = t;
        sim->ours.arena.used = 0;
        memset(g_la_last_feasible, 0, sizeof(g_la_last_feasible));
        la_override(sim, &sim->ours, 0, t);
        if (!g_la_last_feasible[LA_SPEC_NOW]) continue;
        points++;
        predicted += g_la_last_kill[LA_SPEC_NOW];
        predicted_hold += g_la_last_kill[LA_NONE];
        realized += death > t && death <= t + g_la_horizon;
    }
    sim_clear(sim); free(sim);
    int rollouts = 20;
    double hd[2] = {0}, od[2] = {0};
    {
        static Outcome out;
        Sim* work = malloc(sizeof(Sim));
        sim_init(work);
        OursPolicy policies[2] = {{OURS_RULES, 0, TIMING_MEASURED, STAKES_COMPUTED}, {OURS_LOOKAHEAD, 0, TIMING_MEASURED, STAKES_COMPUTED}};
        int t0 = clock_anchor(fresh, fresh->lo + 1);
        int kk = t0 >= 0 ? fresh->hi - t0 : 0;
        if (t0 < 0) rollouts = 0;
        for (int r = 0; !cold && r < rollouts; r++) {
            uint32_t rng = osrs_lowbias32((uint32_t)(t0 * 7919 + r * 104729 + fresh->index * 13 + fresh->lo)) | 1;
            RiskfightSnapshot snap;
            if (!tape_snapshot(fresh, t0, &DEFAULT_PRIORS, &rng, &snap)) continue;
            uint32_t seed = xorshift32(&rng);
            for (int p = 0; p < 2; p++) {
                riskfight_load_state(&work->state, &work->context, &snap);
                memset(g_la_memo, 0, sizeof(g_la_memo));
                g_la_decision_count = 0;
                run_steps(work, fresh, t0, kk, &model, &policies[p], seed, &out);
                hd[p] += out.his_death >= 0;
                od[p] += out.our_death >= 0;
            }
        }
        sim_clear(work); free(work);
    }
    char key[64];
    snprintf(key, sizeof(key), "%s%d", fresh->session, fresh->index);
    FILE* ledger = fopen(ledger_path, "a+");
    assert(ledger);
    fprintf(ledger, "%s %.5f %.5f %.5f %.5f %.5f %.5f %.0f %.0f %.0f %.0f %.0f %.0f %.0f %.0f %.0f %.0f %.0f %.0f %.0f %.0f %.0f\n", key, loss[ACT_EAT_WARNING], constant[ACT_EAT_WARNING],
        loss[ACT_SWING], constant[ACT_SWING], loss[ACT_VW_SPEC], constant[ACT_VW_SPEC], events[ACT_EAT_WARNING], events[ACT_SWING], events[ACT_VW_SPEC], points, predicted, predicted_hold, realized,
        hd[0], hd[1], od[0], od[1], (double)rollouts, 0.0, 0.0, 0.0);
    rewind(ledger);
    double mean_gain[3] = {0}, fights = 0, total_points = 0, total_predicted = 0, total_realized = 0;
    char name[128];
    double v[21];
    while (fscanf(ledger, "%127s", name) == 1) {
        for (int k = 0; k < 21; k++) assert(fscanf(ledger, "%lf", &v[k]) == 1);
        total_points += v[9]; total_predicted += v[10]; total_realized += v[12];
        if (!isfinite(v[0] + v[1] + v[2] + v[3] + v[4] + v[5])) continue;
        mean_gain[0] += v[1] - v[0]; mean_gain[1] += v[3] - v[2]; mean_gain[2] += v[5] - v[4];
        fights++;
    }
    fclose(ledger);
    printf("%s | held-out logloss/tick eat %.4f (const %.4f, running gain %+.4f) swing %.4f (const %.4f, running %+.4f) vwspec %.4f (const %.4f, running %+.4f) | refit on %d fights | recorded spec points %.0f: lookahead spec-now %.3f hold %.3f realized %.3f (running %.0f points: predicted %.3f realized %.3f) | held-out swings (pred/obs) %s | corrected %s | sim kill rules %.2f lookahead %.2f die %.2f/%.2f (n=%d) | %.0f s\n",
        fresh->name, loss[ACT_EAT_WARNING], constant[ACT_EAT_WARNING], mean_gain[0] / fights, loss[ACT_SWING], constant[ACT_SWING], mean_gain[1] / fights, loss[ACT_VW_SPEC], constant[ACT_VW_SPEC],
        mean_gain[2] / fights, count, points, points ? predicted / points : 0, points ? predicted_hold / points : 0, points ? realized / points : 0, total_points, total_points ? total_predicted / total_points : 0, total_points ? total_realized / total_points : 0, held, corrected, hd[0] / rollouts, hd[1] / rollouts,
        od[0] / rollouts, od[1] / rollouts, rollouts, seconds() - started);
}

/** The sampler may read only what the client knows at the decision tick: worlds drawn from a snapshot of the tape cut at that tick must equal those from the whole tape. */
static void leakcheck(Tape* tapes, int count) {
    static OppFit fit;
    memset(&fit, 0, sizeof(fit));
    Sim* a = malloc(sizeof(Sim));
    Sim* b = malloc(sizeof(Sim));
    sim_init(a);
    sim_init(b);
    long checked = 0, leaked = 0;
    for (int i = 0; i < count; i++) {
        Tape* tape = &tapes[i];
        for (int t = tape->lo + 5; t <= tape->hi; t += 7) {
            if (!clock_eligible(tape, t, CLOCK_LEAKCHECK)) continue;
            if (!gear_eligible(tape, t, CLOCK_LEAKCHECK)) continue;
            Tape cut = *tape;
            cut.hi = t;
            cut.anims = malloc((size_t)tape->anim_count * sizeof(TapeAnim));
            cut.hits = malloc((size_t)tape->hit_count * sizeof(TapeHit));
            cut.actions = malloc((size_t)tape->action_count * sizeof(TapeAction));
            cut.anim_count = cut.hit_count = cut.action_count = 0;
            for (int k = 0; k < tape->anim_count; k++) if (tape->anims[k].tick <= t) cut.anims[cut.anim_count++] = tape->anims[k];
            for (int k = 0; k < tape->hit_count; k++) if (tape->hits[k].tick <= t) cut.hits[cut.hit_count++] = tape->hits[k];
            for (int k = 0; k < tape->action_count; k++) if (tape->actions[k].tick <= t) cut.actions[cut.action_count++] = tape->actions[k];
            cut.kit_count = 0;
            for (int k = 0; k < cut.anim_count; k++) {
                if (cut.anims[k].side != SIDE_HIM) continue;
                int at = 0;
                while (at < cut.kit_count && cut.kit_anims[at][0] != cut.anims[k].anim) at++;
                if (at == cut.kit_count) { cut.kit_anims[at][0] = cut.anims[k].anim; cut.kit_anims[at][1] = 0; cut.kit_count++; }
                cut.kit_anims[at][1]++;
            }
            Sim* sims[2] = {a, b};
            const Tape* tapes_of[2] = {tape, &cut};
            LaBelief belief = {.hp_low = 30, .hp_high = 90, .energy_low = 50, .energy_high = 100, .vengeance = GS_VENGEANCE_MAYBE, .pid = GS_PID_LOCAL, .timer_seen = 1, .timer_first = t + 2,
                .timer_last = t + 4, .food_ready = 0, .seen_since_eat = 5, .bar_ratio = 15, .flip = 0.1, .gear = tape->observer_series[t - tape->lo].inf.c.opponent_gear};
            uint64_t print[2][8];
            static RiskfightState kept[2][8];
            for (int s = 0; s < 2; s++) {
                g_tape = tapes_of[s];
                uint32_t rng = osrs_lowbias32((uint32_t)t) | 1;
                RiskfightSnapshot snap;
                if (!tape_snapshot(tapes_of[s], t, &DEFAULT_PRIORS, &rng, &snap)) {
                    fprintf(stderr, "eligible leakcheck branch lost pending source at tick %d\n", t);
                    abort();
                }
                riskfight_load_state(&sims[s]->state, &sims[s]->context, &snap);
                if (s == 1) memcpy(sims[1]->state.env.players[1].equipped, sims[0]->state.env.players[1].equipped, sizeof(sims[1]->state.env.players[1].equipped));
                for (int d = 0; d < 8; d++) {
                    static RiskfightState world;
                    static Hist hist;
                    uint32_t wr = osrs_lowbias32((uint32_t)(t * 31 + d)) | 1;
                    la_world(&sims[s]->state, &((const Hist*)tape->hist_series)[t - tape->lo], &belief, t, &wr, &world, &hist);
                    print[s][d] = la_fingerprint(&world);
                    kept[s][d] = world;
                    if (getenv("GS_LEAK_DEBUG")) { static int shown; const Player* h = &world.env.players[1]; if (d == 0 && shown < 12) { shown += s; fprintf(stderr, "t%d s%d hp %d en %d veng %d timer %d food %d %d %d atk %d pid %d bar %d seed %u inv", t, s, h->current_hitpoints, h->special_energy, h->veng_active, h->attack_timer, h->food_timer, h->potion_timer, h->karambwan_timer, h->current_attack, world.env.pid_holder, world.visible[0].health_bar, world.script_seed[1]); for (int q = 0; q < OSRS_INVENTORY_SIZE; q++) fprintf(stderr, " %d", osrs_inventory_cell_metadata(&h->inventory_cells[q])->item_idx); fprintf(stderr, "\n"); } }
                }
            }
            for (int d = 0; d < 8; d++) { checked++; leaked += print[0][d] != print[1][d]; }
            if (getenv("GS_LEAK_DEBUG")) for (int d = 0; d < 8; d++) if (print[0][d] != print[1][d]) { { fprintf(stderr, "MISMATCH tape %s t %d draw %d\n", tape->name, t, d); for (int s2 = 0; s2 < 2; s2++) { const Player* h = &kept[s2][d].env.players[1]; fprintf(stderr, "  s%d hp %d en %d regen %d/%d veng %d/%d timer %d food %d %d %d atk/str %d %d pid %d bar %d seed %u inv", s2, h->current_hitpoints, h->special_energy, h->spec_regen_active, h->item_effect_state.special_regen_ticks, h->veng_active, h->veng_cooldown, h->attack_timer, h->food_timer, h->potion_timer, h->karambwan_timer, h->current_attack, h->current_strength, kept[s2][d].env.pid_holder, kept[s2][d].visible[0].health_bar, kept[s2][d].script_seed[1]); for (int q = 0; q < OSRS_INVENTORY_SIZE; q++) { const OsrsItemContentMetadata* m = osrs_inventory_cell_metadata(&h->inventory_cells[q]); fprintf(stderr, " %d:%d:%d", m->item_idx, m->consumable_kind, m->dose_count); } fprintf(stderr, "\n"); } break; } }
            free(cut.anims); free(cut.hits); free(cut.actions);
        }
    }
    printf("no-leak check: %ld worlds compared between the whole tape and the tape cut at the decision tick, %ld differ\n", checked, leaked);
    if (leaked) abort();
}

/** The 161-point gap by mechanism: where his hitpoints lie against the belief band at the next bar reveal, and how often he eats in the window a recorded spec lands in, recorded against the worlds. */
static void calibration(Tape* tapes, int count) {
    enum { WINDOW = 6, WORLDS = 64, HP_BANDS = 4, LAGS = 4 };
    static OppFit fit;
    memset(&fit, 0, sizeof(fit));
    fit_default_servings(&fit);
    int eats, identified[2];
    fit_model(tapes, count, 0, &fit);
    infer_servings(tapes, count, 0, &fit, &eats, identified);
    fit.eat_action = ACT_EAT_WARNING;
    eat_model_setup(tapes, count);
    assert_fitted(&fit, "calibration");
    OppModel model = {MODEL_FIT, &fit};
    g_la_model = &model;
    g_la_worlds = WORLDS;
    g_la_stage = WORLDS;
    g_la_cache = 0;
    if (getenv("GS_LA_HP_SHIFT")) g_la_belief_hp_shift = atof(getenv("GS_LA_HP_SHIFT"));
    if (getenv("GS_CAL_EAT")) g_eat_scale = atof(getenv("GS_CAL_EAT"));
    double band_n[LAGS] = {0}, band_above[LAGS] = {0}, band_below[LAGS] = {0}, band_excess[LAGS] = {0}, band_width[LAGS] = {0};
    double pt_n[HP_BANDS] = {0}, pt_obs_eat[HP_BANDS] = {0}, pt_pred_eat[HP_BANDS] = {0}, pt_obs_kill[HP_BANDS] = {0}, pt_pred_kill[HP_BANDS] = {0}, pt_pred_hold[HP_BANDS] = {0};
    double lag_n[LAGS] = {0}, lag_obs_eat[LAGS] = {0}, lag_pred_eat[LAGS] = {0};
    double un_n[5] = {0}, un_excess[5] = {0}, un_above[5] = {0};
    double fb_n[2][2] = {{0}}, fb_above[2][2] = {{0}}, fb_excess[2][2] = {{0}}, fb_low[2][2] = {{0}};
    double rev_n[2] = {0}, rev_above[2] = {0}, rev_below[2] = {0}, rev_excess[2] = {0};
    double quiet_points = 0, quiet_kills = 0, quiet_pred_kill = 0, quiet_pred_weight = 0, busy_pred_kill = 0, busy_pred_weight = 0;
    Sim* sim = malloc(sizeof(Sim));
    sim_init(sim);
    for (int i = 0; i < count; i++) {
        Tape* tape = &tapes[i];
        g_tape = tape;
        const Observer* series = tape->observer_series;
        int death = recorded_death_tick(tape);
        for (int t = tape->lo + 1; t < tape->hi; t++) {
            const TapeTick* now = tape_tick(tape, t);
            GsHealthRange belief = series[t - tape->lo].inf.opponent_hitpoints;
            int reveal = -1;
            for (int x = t + 1; x <= tape->hi && x <= t + 40; x++) if (tape_tick(tape, x)->last_bar_hit == x && tape_tick(tape, x)->bar_now_ratio > 0) { reveal = x; break; }
            if (reveal < 0 || now->bar_ratio < 0) continue;
            int damage = 0, ate = 0;
            for (int x = t + 1; x <= reveal; x++) { damage += tape_damage_on_him(tape, x); ate += tape_his_anim_at(tape, x, ANIM_EAT); }
            const TapeTick* after = tape_tick(tape, reveal);
            int low = warning_bar_low(after->bar_now_ratio, after->bar_now_scale) + damage, high = warning_bar_high(after->bar_now_ratio, after->bar_now_scale) + damage;
            int lag = t - now->last_bar_hit;
            int bin = lag <= 3 ? 0 : lag <= 6 ? 1 : lag <= 9 ? 2 : 3;
            { int u = imin(4, series[t - tape->lo].inf.unseen_count); un_n[u]++; un_excess[u] += ((low + high) / 2.0) - (belief.low + belief.high) / 2.0; un_above[u] += low > belief.high; }
            {
                int full = now->bar_ratio == now->bar_scale, reveal_full = after->bar_ratio == after->bar_scale;
                fb_n[full][reveal_full]++; fb_above[full][reveal_full] += low > belief.high; fb_excess[full][reveal_full] += (low + high) / 2.0 - (belief.low + belief.high) / 2.0; fb_low[full][reveal_full] += low;
            }
            band_n[bin]++;
            band_above[bin] += low > belief.high;
            band_below[bin] += high < belief.low;
            band_excess[bin] += ((low + high) / 2.0) - (belief.low + belief.high) / 2.0;
            band_width[bin] += belief.high - belief.low;
        }
        int swings = getenv("GS_POINTS") != NULL;
        for (int a = 0; a < (swings ? tape->anim_count : tape->action_count); a++) {
            int t;
            if (swings) {
                const TapeAnim* an = &tape->anims[a];
                if (an->side != SIDE_US || (an->anim != ANIM_VW_SPEC && an->anim != ANIM_MAUL_SPEC)) continue;
                t = an->tick - 1;
            } else {
                if (tape->actions[a].kind != ACTION_SPEC) continue;
                t = tape->actions[a].tick - 1;
            }
            if (t < tape->lo + 1 || (t + g_la_horizon > tape->hi && !(death > t && death <= t + g_la_horizon))) continue;
            if (!clock_eligible(tape, t, CLOCK_CALIBRATION)) continue;
            if (!gear_eligible(tape, t, CLOCK_CALIBRATION)) continue;
            uint32_t rng = osrs_lowbias32((uint32_t)t) | 1;
            RiskfightSnapshot snap;
            if (!tape_snapshot(tape, t, &DEFAULT_PRIORS, &rng, &snap)) continue;
            riskfight_load_state(&sim->state, &sim->context, &snap);
            observer_assign(&sim->obs, &series[t - tape->lo]);
            observer_branch(&sim->state, &sim->obs);
            sim->hist = ((const Hist*)tape->hist_series)[t - tape->lo];
            sim->seen_since_eat = 99;
            sim->tick_base = t;
            LaBelief belief = la_belief_of(sim, t);
            int hp_mid = (belief.hp_low + belief.hp_high) / 2;
            int band = hp_mid <= 44 ? 0 : hp_mid <= 60 ? 1 : hp_mid <= 87 ? 2 : 3;
            int lag = t - tape_tick(tape, t)->last_bar_hit;
            int lb = lag <= 3 ? 0 : lag <= 6 ? 1 : lag <= 9 ? 2 : 3;
            static Sim* work;
            if (!work) { work = malloc(sizeof(Sim)); sim_init(work); }
            LaScript none = {0}, spec = {0};
            uint32_t press = osrs_lowbias32((uint32_t)t * 2654435761u) | 1;
            const Player* us = &sim->state.env.players[0];
            HumanCommand clicks[2];
            int n = 0;
            if (!us->spec_armed) clicks[n++] = spec_command();
            clicks[n++] = attack_command();
            la_press(&press, &spec, t, clicks, n);
            double pred_eat = 0, pred_kill = 0, pred_hold = 0, quiet_k = 0, quiet_w = 0, busy_k = 0, busy_w = 0;
            static RiskfightState world;
            static Hist hist;
            for (int w = 0; w < WORLDS; w++) {
                uint32_t draw = osrs_lowbias32((uint32_t)(t * 40503u + w * 2246822519u)) | 1;
                uint32_t wr = osrs_lowbias32(draw) | 1;
                la_world(&sim->state, &sim->hist, &belief, t, &wr, &world, &hist);
                uint32_t seed = osrs_lowbias32(draw + 17);
                LaOutcome with = la_rollout(work, &world, &hist, t, &model, &spec, seed);
                LaOutcome without = la_rollout(work, &world, &hist, t, &model, &none, seed);
                int ate_in = with.ate_at >= 0 && with.ate_at < WINDOW;
                pred_eat += ate_in;
                if (ate_in) { busy_w++; busy_k += with.kill; } else { quiet_w++; quiet_k += with.kill; }
                pred_kill += with.kill;
                pred_hold += without.kill;
            }
            int observed_eat = 0;
            for (int x = t + 1; x <= tape->hi && x <= t + WINDOW; x++) observed_eat |= tape_his_anim_at(tape, x, ANIM_EAT) != 0;
            pt_n[band]++; pt_obs_eat[band] += observed_eat; pt_pred_eat[band] += pred_eat / WORLDS;
            pt_obs_kill[band] += death > t && death <= t + g_la_horizon; pt_pred_kill[band] += pred_kill / WORLDS; pt_pred_hold[band] += pred_hold / WORLDS;
            {
                int reveal = -1;
                for (int x = t + 1; x <= tape->hi && x <= t + 8; x++) if (tape_tick(tape, x)->last_bar_hit == x && tape_tick(tape, x)->bar_now_ratio > 0) { reveal = x; break; }
                if (reveal >= 0) {
                    int damage = 0;
                    for (int x = t + 1; x <= reveal; x++) damage += tape_damage_on_him(tape, x);
                    const TapeTick* after = tape_tick(tape, reveal);
                    int low = warning_bar_low(after->bar_now_ratio, after->bar_now_scale) + damage, high = warning_bar_high(after->bar_now_ratio, after->bar_now_scale) + damage;
                    int k = observed_eat ? 1 : 0;
                    rev_n[k]++; rev_above[k] += low > belief.hp_high; rev_below[k] += high < belief.hp_low; rev_excess[k] += (low + high) / 2.0 - (belief.hp_low + belief.hp_high) / 2.0;
                }
            }
            quiet_pred_kill += quiet_k; quiet_pred_weight += quiet_w; busy_pred_kill += busy_k; busy_pred_weight += busy_w;
            if (!observed_eat) { quiet_points++; quiet_kills += death > t && death <= t + g_la_horizon; }
            lag_n[lb]++; lag_obs_eat[lb] += observed_eat; lag_pred_eat[lb] += pred_eat / WORLDS;
        }
    }
    static const char* const LAG_NAME[LAGS] = {"bar read 0-3 ticks ago", "4-6", "7-9", "10+"};
    static const char* const HP_NAME[HP_BANDS] = {"belief mid <=44", "45-60", "61-87", "88+"};
    printf("bar band against the bar at the next reveal (reveal hp = bar band + damage since; above = he healed past the band top unseen)\n");
    for (int b = 0; b < LAGS; b++)
        if (band_n[b]) printf("  %-24s %6.0f ticks  band width %5.1f  truth above band %.3f  below %.3f  mean excess over band mid %+.1f hp\n", LAG_NAME[b], band_n[b], band_width[b] / band_n[b], band_above[b] / band_n[b], band_below[b] / band_n[b], band_excess[b] / band_n[b]);
    for (int u = 0; u < 5; u++) if (un_n[u]) printf("  by unseen-meal opportunities %d%s: %6.0f ticks, truth above band %.3f, mean excess %+.1f hp\n", u, u == 4 ? "+" : "", un_n[u], un_above[u] / un_n[u], un_excess[u] / un_n[u]);
    for (int f = 0; f < 2; f++) for (int r = 0; r < 2; r++)
        if (fb_n[f][r]) printf("  bar at tick %s, at next reveal %s: %6.0f ticks, truth above band %.3f, mean excess %+.1f hp, mean reveal low %.1f\n", f ? "FULL" : "not full", r ? "FULL" : "not full", fb_n[f][r], fb_above[f][r] / fb_n[f][r], fb_excess[f][r] / fb_n[f][r], fb_low[f][r] / fb_n[f][r]);
    printf("eat in the %d ticks after a recorded spec click (visible eat animation), recorded against the worlds; kill within %d ticks\n", WINDOW, g_la_horizon);
    double tn = 0, to = 0, tp = 0, ko = 0, kp = 0, kh = 0;
    for (int b = 0; b < HP_BANDS; b++) {
        if (!pt_n[b]) continue;
        printf("  %-18s %4.0f points  eat observed %.3f predicted %.3f | kill observed %.3f predicted %.3f (hold %.3f)\n", HP_NAME[b], pt_n[b], pt_obs_eat[b] / pt_n[b], pt_pred_eat[b] / pt_n[b], pt_obs_kill[b] / pt_n[b], pt_pred_kill[b] / pt_n[b], pt_pred_hold[b] / pt_n[b]);
        tn += pt_n[b]; to += pt_obs_eat[b]; tp += pt_pred_eat[b]; ko += pt_obs_kill[b]; kp += pt_pred_kill[b]; kh += pt_pred_hold[b];
    }
    printf("  %-18s %4.0f points  eat observed %.3f predicted %.3f | kill observed %.3f predicted %.3f (hold %.3f)\n", "all", tn, to / tn, tp / tn, ko / tn, kp / tn, kh / tn);
    printf("  kill given no eat in the window: predicted %.3f of %.0f world-points, observed %.0f kills of %.0f points; kill given an eat: predicted %.3f\n", quiet_pred_weight ? quiet_pred_kill / quiet_pred_weight : 0, quiet_pred_weight, quiet_kills, quiet_points, busy_pred_weight ? busy_pred_kill / busy_pred_weight : 0);
    for (int k = 0; k < 2; k++)
        if (rev_n[k]) printf("  hp at the next reveal after the point (bar band + damage since), he %s in the window: %.0f points, truth above the belief band %.3f, below %.3f, mean excess over the band middle %+.1f hp\n", k ? "ate" : "did not eat", rev_n[k], rev_above[k] / rev_n[k], rev_below[k] / rev_n[k], rev_excess[k] / rev_n[k]);
    for (int b = 0; b < LAGS; b++)
        if (lag_n[b]) printf("  by bar age %-24s %4.0f points  eat observed %.3f predicted %.3f\n", LAG_NAME[b], lag_n[b], lag_obs_eat[b] / lag_n[b], lag_pred_eat[b] / lag_n[b]);
    sim_clear(sim); free(sim);
}

static void corrections(Tape* tapes, int count) {
    enum { SHIFTS = 5, SCALES = 5 };
    static const double SHIFT[SHIFTS] = {0, 4, 8, 12, 16}, SCALE[SCALES] = {1, 1.5, 2, 2.5, 3};
    for (int i = 0; i < count; i++) assert(tapes[i].observer_series);
    static OppFit fit;
    static EatMixture mixture;
    static EatRows rows;
    Tape* train = malloc((size_t)count * sizeof(Tape));
    int capacity = 0;
    for (int i = 0; i < count; i++) capacity = imax(capacity, tapes[i].anim_count);
    SwingPoint* base = malloc((size_t)capacity * sizeof(SwingPoint));
    SwingPoint* points = malloc((size_t)capacity * sizeof(SwingPoint));
    Sim* a = malloc(sizeof(Sim));
    Sim* b = malloc(sizeof(Sim));
    sim_init(a);
    sim_init(b);
    int folds = 0, swings = 0, observed_eats = 0, observed_kills = 0, terminal_positives = 0, censored_tails = 0;
    for (int i = 0; i < count; i++) {
        int death = recorded_death_tick(&tapes[i]);
        for (int a = 0; a < tapes[i].anim_count; a++) {
            const TapeAnim* an = &tapes[i].anims[a];
            if (an->side != SIDE_US || (an->anim != ANIM_VW_SPEC && an->anim != ANIM_MAUL_SPEC)) continue;
            int t = an->tick - 1;
            if (t < tapes[i].lo + 1 || t + g_la_horizon <= tapes[i].hi) continue;
            if (death > t && death <= t + g_la_horizon) terminal_positives++;
            else censored_tails++;
        }
    }
    double base_eats = 0, base_kills = 0, corrected_eats = 0, corrected_kills = 0;
    for (int pass = 0; pass < count; pass++) {
        int eligible = 0;
        int death = recorded_death_tick(&tapes[pass]);
        for (int a = 0; a < tapes[pass].anim_count; a++) {
            int t = swing_tick(&tapes[pass], &tapes[pass].anims[a], g_la_horizon, death);
            eligible += t >= 0 && clock_status(&tapes[pass], t) == CLOCK_EXACT;
        }
        if (!eligible) continue;
        g_la_belief_hp_shift = 0;
        g_eat_scale = 1;
        int train_count = 0;
        for (int i = 0; i < count; i++) if (i != pass) train[train_count++] = tapes[i];
        for (int i = 0; i < train_count; i++) assert(train[i].ticks != tapes[pass].ticks);
        memset(&fit, 0, sizeof(fit));
        fit_default_servings(&fit);
        int eats, identified[2];
        fit_model(train, train_count, 0, &fit);
        infer_servings(train, train_count, 0, &fit, &eats, identified);
        fit.eat_action = ACT_EAT_WARNING;
        rows.count = 0;
        eat_rows_add(&rows, train, train_count, 0, CLOCK_IRRELEVANT);
        eat_rows_unmasked(&rows);
        for (int i = 0; i < rows.count; i++) assert(rows.rows[i].fight / FIT_SAMPLES < train_count);
        mix_fit(&mixture, 2, &rows);
        g_eat_model.on = 1;
        g_eat_model.model = EAT_MIXTURE;
        g_eat_model.types = mixture.types;
        memcpy(g_eat_model.pi, mixture.pi, sizeof(mixture.pi));
        g_eat_model.logit = mixture.logit;
        assert_fitted(&fit, "corrections");
        OppModel model = {MODEL_FIT, &fit};
        double best = 1e18;
        int bs = 0, be = 0;
        int training_swings = 0;
        for (int s = 0; s < SHIFTS; s++)
            for (int e = 0; e < SCALES; e++) {
                double loss = 0;
                int n = 0;
                g_la_belief_hp_shift = SHIFT[s];
                g_eat_scale = SCALE[e];
                for (int i = 0; i < train_count; i++) {
                    int got = swing_points(&train[i], &model, a, b, points);
                    n += got;
                    for (int k = 0; k < got; k++) {
                        double pe = fmin(0.98, fmax(0.02, points[k].pred_eat));
                        double pk = fmin(0.98, fmax(0.02, points[k].pred_kill));
                        loss -= points[k].observed_eat ? log(pe) : log(1 - pe);
                        loss -= points[k].observed_kill ? log(pk) : log(1 - pk);
                    }
                }
                if (s == 0 && e == 0) training_swings = n;
                else assert(n == training_swings);
                if (loss < best) { best = loss; bs = s; be = e; }
            }
        assert(training_swings > 0);
        g_la_belief_hp_shift = 0;
        g_eat_scale = 1;
        int held_n = swing_points(&tapes[pass], &model, a, b, base);
        assert(held_n == eligible);
        g_la_belief_hp_shift = SHIFT[bs];
        g_eat_scale = SCALE[be];
        int corrected_n = swing_points(&tapes[pass], &model, a, b, points);
        assert(corrected_n == held_n);
        double held_base_eat = 0, held_base_kill = 0, held_corr_eat = 0, held_corr_kill = 0;
        int held_obs_eat = 0, held_obs_kill = 0;
        for (int k = 0; k < held_n; k++) {
            assert(base[k].tick == points[k].tick);
            assert(base[k].observed_eat == points[k].observed_eat && base[k].observed_kill == points[k].observed_kill);
            held_obs_eat += base[k].observed_eat;
            held_obs_kill += base[k].observed_kill;
            held_base_eat += base[k].pred_eat;
            held_base_kill += base[k].pred_kill;
            held_corr_eat += points[k].pred_eat;
            held_corr_kill += points[k].pred_kill;
        }
        folds++;
        swings += held_n;
        observed_eats += held_obs_eat;
        observed_kills += held_obs_kill;
        base_eats += held_base_eat;
        base_kills += held_base_kill;
        corrected_eats += held_corr_eat;
        corrected_kills += held_corr_kill;
        printf("held-out %-28s train %d fights %d swings, test %d swings, shift %+.0f scale %.1f | eat %.3f/%.3f/%.3f kill %.3f/%.3f/%.3f (observed/base/corrected)\n",
            tapes[pass].name, train_count, training_swings, held_n, SHIFT[bs], SCALE[be],
            (double)held_obs_eat / held_n, held_base_eat / held_n, held_corr_eat / held_n,
            (double)held_obs_kill / held_n, held_base_kill / held_n, held_corr_kill / held_n);
    }
    assert(swings > 0);
    printf("held-out total %d/%d fights, %d swings, %d known terminal positives, %d censored tails | eat observed %d/%d %.6f base %.6f corrected %.6f | kill observed %d/%d %.6f base %.6f corrected %.6f (probability_sum %.3f/%.3f)\n",
        folds, count, swings, terminal_positives, censored_tails, observed_eats, swings, (double)observed_eats / swings, base_eats / swings, corrected_eats / swings,
        observed_kills, swings, (double)observed_kills / swings, base_kills / swings, corrected_kills / swings, base_kills, corrected_kills);
    g_la_belief_hp_shift = 0;
    g_eat_scale = 1;
    free(train);
    free(base);
    free(points);
    free(a);
    free(b);
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
    long source_counts[CLOCK_STATUS_COUNT] = {0};
    for (int i = 0; i < count; i++)
        for (int t = tapes[i].lo; t <= tapes[i].hi; t++) {
            ClockStatus status = clock_status(&tapes[i], t);
            source_counts[status]++;
            if (status != CLOCK_EXACT) printf("clock source %s tick %d status %s\n", tapes[i].name, t, CLOCK_STATUS_NAME[status]);
        }
    printf("clock source frames %ld exact %ld unknown %ld interval %ld retained_tapes %d/%d\n",
        source_counts[CLOCK_EXACT] + source_counts[CLOCK_UNKNOWN] + source_counts[CLOCK_INTERVAL], source_counts[CLOCK_EXACT],
        source_counts[CLOCK_UNKNOWN], source_counts[CLOCK_INTERVAL], count, count);
    if (!strcmp(mode, "spectator")) { spec_load(&corpus, argv[2]); spectator(tapes, count, argv[3]); }
    else if (!strcmp(mode, "teacher")) teacher(tapes, count, atoi(argv[2]), atoi(argv[3]));
    else if (!strcmp(mode, "rollouts")) rollouts(atoi(argv[2]), atoi(argv[3]), tapes, count, atoi(argv[4]), atoi(argv[5]));
    else if (!strcmp(mode, "attribute")) attribute(atoi(argv[2]), atoi(argv[3]), tapes, count, atoi(argv[4]), atoi(argv[5]));
    else if (!strcmp(mode, "ablate")) ablate(atoi(argv[2]), atoi(argv[3]), tapes, count, atoi(argv[4]), atoi(argv[5]));
    else if (!strcmp(mode, "eatmix")) eat_mixture_study(tapes, count, atoi(argv[2]), atoi(argv[3]), strcmp(argv[4], "-") ? argv[4] : NULL, getenv("GS_MIX_SAVE"));
    else if (!strcmp(mode, "eatstudy")) eat_study(tapes, count, atoi(argv[2]), atoi(argv[3]));
    else if (!strcmp(mode, "autopsy")) autopsy(atoi(argv[2]), atoi(argv[3]), tapes, count, atoi(argv[4]));
    else if (!strcmp(mode, "branch")) branch(atoi(argv[2]), atoi(argv[3]), tapes, count);
    else if (!strcmp(mode, "branchpoint")) branchpoint(argv[2], atoi(argv[3]), tapes, count);
    else if (!strcmp(mode, "calibration")) calibration(tapes, count);
    else if (!strcmp(mode, "corrections")) corrections(tapes, count);
    else if (!strcmp(mode, "leakcheck")) leakcheck(tapes, count);
    else if (!strcmp(mode, "oppstep")) oppstep(argv[2], argv[3], tapes, count);
    else if (!strcmp(mode, "fightcmp")) fightcmp(atoi(argv[2]), tapes, count, atoi(argv[3]));
    else abort();
    for (int c = 0; c < CLOCK_COHORT_COUNT; c++) {
        long* gear = gear_counts[c];
        long source = 0;
        for (int status = 0; status < LA_GEAR_STATUS_COUNT; status++) source += gear[status];
        if (source) printf("gear cohort %s source %ld supported %ld unknown %ld unsupported_weapon %ld unsupported_shield %ld unsupported_ammo_prior %ld armor_unknown %ld unsupported_armor %ld\n", CLOCK_COHORT_NAME[c], source,
            gear[LA_GEAR_SUPPORTED], gear[LA_GEAR_UNKNOWN], gear[LA_GEAR_WEAPON_UNSUPPORTED], gear[LA_GEAR_SHIELD_UNSUPPORTED], gear[LA_GEAR_AMMO_UNSUPPORTED], gear[LA_GEAR_ARMOR_UNKNOWN], gear[LA_GEAR_ARMOR_UNSUPPORTED]);
    }
    for (int c = 0; c < CLOCK_COHORT_COUNT; c++) {
        long* n = clock_counts[c];
        long total = n[CLOCK_EXACT] + n[CLOCK_UNKNOWN] + n[CLOCK_INTERVAL] + n[CLOCK_OUTSIDE];
        if (c == CLOCK_HAZARD_INPUTS || c == CLOCK_EAT_HP_INPUTS) {
            if (total) printf("clock independent cohort %s input_frames %ld retained_exact %ld retained_unknown %ld retained_interval %ld\n",
                CLOCK_COHORT_NAME[c], total, n[CLOCK_EXACT], n[CLOCK_UNKNOWN], n[CLOCK_INTERVAL]);
            continue;
        }
        if (total) printf("clock cohort %s candidate_evaluations %ld accepted_exact %ld excluded_unknown %ld excluded_interval %ld excluded_outside %ld\n",
            CLOCK_COHORT_NAME[c], total, n[CLOCK_EXACT], n[CLOCK_UNKNOWN], n[CLOCK_INTERVAL], n[CLOCK_OUTSIDE]);
    }
    return 0;
}
