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
    SCORE_SAMPLES = 16, PROFILE_SAMPLES = 64, BIG_HIT_EPISODE = 30, REARM_TICKS = 4, MARGIN = 10 };
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

/* The opponent every mode's rollouts run: the committed fit (fixtures/model.fit and eat_mixture.fit; GS_OPP_MODEL and GS_OPP_EAT name others). */
static void load_opponent(OppFit* fit) {
    const char* model = getenv("GS_OPP_MODEL") ? getenv("GS_OPP_MODEL") : "ocean/osrs/tests/fixtures/model.fit";
    const char* eat = getenv("GS_OPP_EAT") ? getenv("GS_OPP_EAT") : "ocean/osrs/tests/fixtures/eat_mixture.fit";
    opp_load(model, eat, fit, &g_eat_model);
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

/* Where the planner's predicted kills go: the played spec decisions of the ported rules against what the sim opponent then did, with one term changed at a time. */

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
    load_opponent(&fit);
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


/* Which belief drives the planner's overconfidence: the ported rules with one belief about him read from the sim at a time. */
static void ablate(int n, int k, Tape* tapes, int count, int train_day, int test_day) {
    static OppFit fit;
    load_opponent(&fit);
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
    load_opponent(&fit);
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
    load_opponent(&fit);
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
    load_opponent(&fit);
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
    int first = !strcmp(mode, "autopsy") ? 5 : !strcmp(mode, "ablate") ? 6 : 4;
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
    if (!strcmp(mode, "ablate")) ablate(atoi(argv[2]), atoi(argv[3]), tapes, count, atoi(argv[4]), atoi(argv[5]));
    else if (!strcmp(mode, "autopsy")) autopsy(atoi(argv[2]), atoi(argv[3]), tapes, count, atoi(argv[4]));
    else if (!strcmp(mode, "branch")) branch(atoi(argv[2]), atoi(argv[3]), tapes, count);
    else if (!strcmp(mode, "branchpoint")) branchpoint(argv[2], atoi(argv[3]), tapes, count);
    else if (!strcmp(mode, "calibration")) calibration(tapes, count);
    else if (!strcmp(mode, "leakcheck")) leakcheck(tapes, count);
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
