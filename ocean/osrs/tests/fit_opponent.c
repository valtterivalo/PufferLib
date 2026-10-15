/* Fits the opponent (riskfight_oppmodel.h) on every own tape and every spectated duel, scored held out by recording.
 *
 *   cc -std=c11 -O2 -I. -o /tmp/fit_opponent ocean/osrs/tests/fit_opponent.c -lm
 *   /tmp/fit_opponent OUT_DIR SPECTATOR_DUELS|- tape...
 *
 * Every hazard is fitted on the rows of the OTHER fight style (DHAROK style rows are scored with the OTHER fit and reported apart), folds are
 * recordings (K folds, a recording never in the fit that scores it), and each hazard is scored against a constant rate and against the structure
 * of the Oct 8 deployment refitted on the same folds. OUT_DIR gets the report (oppfit.md), model.fit and eat_mixture.fit. */
#define _POSIX_C_SOURCE 200809L
#include "riskfight_observe.h"
#include "riskfight_spectator.h"

enum { MAX_ROWS = 500000, FOLDS = 8, NNZ = 10 };

typedef struct { uint8_t n, idx[NNZ]; float val[NNZ]; } SparseX;

static SparseX sparse_of(const double* x) {
    SparseX s = {0};
    for (int i = 0; i < OPP_DIM; i++) if (x[i] != 0) { assert(s.n < NNZ); s.idx[s.n] = (uint8_t)i; s.val[s.n++] = (float)x[i]; }
    return s;
}

static double sparse_z(const Glm* g, const SparseX* s) { double z = 0; for (int i = 0; i < s->n; i++) z += g->w[s->idx[i]] * s->val[i]; return z; }

/* ---------- corpus ---------- */

static FitRow* g_rows;
static int g_count, g_fights;
static int* g_fight;       /* fight (duel and side) of each row */
static int g_fold[SP_MAX_SESSIONS * 2], g_clean[SP_MAX_SESSIONS * 2];
static int g_sessions;
static char g_session_names[SP_MAX_SESSIONS * 2][64];
static int g_session_source[SP_MAX_SESSIONS * 2];

static int add_session(const char* name, int source) {
    for (int i = 0; i < g_sessions; i++) if (!strcmp(g_session_names[i], name) && g_session_source[i] == source) return i;
    assert(g_sessions < SP_MAX_SESSIONS * 2);
    snprintf(g_session_names[g_sessions], 64, "%s", name);
    g_session_source[g_sessions] = source;
    return g_sessions++;
}

static void add_series(const SpecDuel* d, int side, int session) {
    FitRow* rows = malloc((size_t)d->row_count * sizeof(FitRow));
    Hist final;
    fit_series(d, side, rows, &final);
    for (int i = 1; i < d->row_count; i++) {
        assert(g_count < MAX_ROWS);
        g_rows[g_count] = rows[i];
        g_rows[g_count].session = session;
        g_fight[g_count] = g_fights;
        g_count++;
    }
    g_fights++;
    free(rows);
}

/* ---------- hazards, datasets, fits ---------- */

static int sample_hp(const FitRow* r, int j) {
    uint32_t rng = osrs_lowbias32((uint32_t)(r->tick * 7919 + j * 104729 + r->session * 13)) | 1;
    OsrsHealthBarRange bar = osrs_health_bar_range(r->hit_ratio, r->hit_scale, 99, 121);
    if (r->hit_ratio < 0 || bar.kind != OSRS_HEALTH_BAR_KNOWN) return 60 + (int)(xorshift32(&rng) % 62);
    int hi = bar.upper + 24 * r->meals;
    if (hi > 121) hi = 121;
    return bar.lower + (int)(xorshift32(&rng) % (uint32_t)(hi - bar.lower + 1));
}

typedef struct { int elig, y; } Lab;

typedef struct {
    const char* name;
    Lab (*label)(const FitRow*, int arg);
    void (*feat)(const OppIn*, int arg, uint32_t groups, double* x);
    int arg, samples;
    uint32_t base;              /* the groups of the structure of the Oct 8 deployment */
    uint32_t extra[6];          /* groups tried on top of it, greedily */
    int mixture;                /* eater types fitted by EM */
} Hazard;

typedef struct { SparseX x; uint8_t y, fold, style, clean, source; int pf; float weight; } Obs;

static Obs* g_obs;
static int g_obs_count;

static void build(const Hazard* hz, uint32_t groups, int constant) {
    g_obs_count = 0;
    for (int j = 0; j < hz->samples; j++)
        for (int i = 0; i < g_count; i++) {
            const FitRow* r = &g_rows[i];
            Lab lab = hz->label(r, hz->arg);
            if (!lab.elig || r->third) continue;
            OppIn in = r->in;
            if (hz->samples > 1) in.his_hp = sample_hp(r, j);
            double x[OPP_DIM];
            if (constant) { memset(x, 0, sizeof(x)); x[0] = 1; } else hz->feat(&in, hz->arg, groups, x);
            Obs* o = &g_obs[g_obs_count++];
            o->x = sparse_of(x);
            o->y = (uint8_t)lab.y;
            o->fold = (uint8_t)g_fold[r->session];
            o->style = (uint8_t)r->style;
            o->clean = (uint8_t)g_clean[r->session];
            o->source = (uint8_t)r->source;
            o->pf = j * g_fights + g_fight[i];
            o->weight = 1.0f / hz->samples;
        }
}

typedef struct { Glm g; int types; double pi[MAX_TYPES]; } Model;

static double clipp(double p) { return p < 1e-6 ? 1e-6 : p > 1 - 1e-6 ? 1 - 1e-6 : p; }

static double obs_p(const Model* m, const Obs* o, int type) {
    double z = sparse_z(&m->g, &o->x);
    if (type > 0) z += m->g.w[13 + type];
    return sigmoid(z);
}

static void solve(double a[OPP_DIM][OPP_DIM + 1]) {
    for (int c = 0; c < OPP_DIM; c++) {
        int pivot = c;
        for (int r = c + 1; r < OPP_DIM; r++) if (fabs(a[r][c]) > fabs(a[pivot][c])) pivot = r;
        for (int k = 0; k <= OPP_DIM; k++) { double t = a[c][k]; a[c][k] = a[pivot][k]; a[pivot][k] = t; }
        for (int r = 0; r < OPP_DIM; r++) {
            if (r == c) continue;
            double f = a[r][c] / a[c][c];
            for (int k = c; k <= OPP_DIM; k++) a[r][k] -= f * a[c][k];
        }
    }
}

/** Newton fit with a ridge of 0.5 on every weight but the intercept and half an event and half a non-event at the intercept (a hazard with no event stays finite).
 * Observation i of `sel` weighs wt[i] and carries eater type type[i] (NULL: none). */
static void newton(Glm* g, const int* sel, const double* wt, const uint8_t* type, int n, int warm, int iterations) {
    const double ridge = 0.5;
    if (!warm) memset(g, 0, sizeof(*g));
    for (int iteration = 0; iteration < iterations; iteration++) {
        double grad[OPP_DIM] = {0}, h[OPP_DIM][OPP_DIM] = {{0}};
        for (int d = 1; d < OPP_DIM; d++) { grad[d] -= ridge * g->w[d]; h[d][d] += ridge; }
        double p0 = sigmoid(g->w[0]);
        grad[0] += 0.5 * (1 - p0) - 0.5 * p0;
        h[0][0] += p0 * (1 - p0);
        for (int s = 0; s < n; s++) {
            const Obs* o = &g_obs[sel[s]];
            SparseX x = o->x;
            if (type && type[s] > 0) { assert(x.n < NNZ); x.idx[x.n] = (uint8_t)(13 + type[s]); x.val[x.n++] = 1; }
            double z = 0;
            for (int a = 0; a < x.n; a++) z += g->w[x.idx[a]] * x.val[a];
            double p = sigmoid(z), r = wt[s] * (o->y - p), v = wt[s] * p * (1 - p);
            for (int a = 0; a < x.n; a++) {
                grad[x.idx[a]] += r * x.val[a];
                for (int b = 0; b < x.n; b++) h[x.idx[a]][x.idx[b]] += v * x.val[a] * x.val[b];
            }
        }
        double m[OPP_DIM][OPP_DIM + 1];
        for (int a = 0; a < OPP_DIM; a++) { for (int b = 0; b < OPP_DIM; b++) m[a][b] = h[a][b] + (a == b ? 1e-9 : 0); m[a][OPP_DIM] = grad[a]; }
        solve(m);
        double move = 0;
        for (int a = 0; a < OPP_DIM; a++) {
            double step = m[a][OPP_DIM] / m[a][a];
            if (step > 3) step = 3;
            if (step < -3) step = -3;
            g->w[a] += step;
            move = fmax(move, fabs(step));
        }
        if (move < 1e-7) break;
    }
}

/** Fits a model on the observations of `sel`: one logit, or (mixture) a logit and eater types by EM over pseudo-fights (one per fight and hp draw), each type adding an intercept. */
static void fit_model(Model* m, const int* sel, int n, int mixture, int types) {
    memset(m, 0, sizeof(*m));
    m->types = mixture ? types : 1;
    double* wt = malloc((size_t)n * (size_t)m->types * sizeof(double));
    for (int s = 0; s < n; s++) wt[s] = g_obs[sel[s]].weight;
    newton(&m->g, sel, wt, NULL, n, 0, 30);
    if (!mixture) { free(wt); return; }
    for (int k = 0; k < types; k++) m->pi[k] = 1.0 / types;
    if (types > 1) m->g.w[14] = 0.8;
    if (types > 2) m->g.w[15] = -0.8;
    int pfs = 0;
    for (int s = 0; s < n; s++) if (s == 0 || g_obs[sel[s]].pf != g_obs[sel[s - 1]].pf) pfs++;
    double* post = malloc((size_t)pfs * MAX_TYPES * sizeof(double));
    int* esel = malloc((size_t)n * (size_t)types * sizeof(int));
    double* ewt = malloc((size_t)n * (size_t)types * sizeof(double));
    uint8_t* etype = malloc((size_t)n * (size_t)types);
    for (int iteration = 0; iteration < 25; iteration++) {
        int pf = -1;
        double ll[MAX_TYPES];
        for (int s = 0; s <= n; s++) {
            if (s == n || (s && g_obs[sel[s]].pf != g_obs[sel[s - 1]].pf)) {
                double top = -1e300, sum = 0;
                for (int k = 0; k < types; k++) top = fmax(top, log(m->pi[k]) + ll[k]);
                for (int k = 0; k < types; k++) { post[pf * MAX_TYPES + k] = exp(log(m->pi[k]) + ll[k] - top); sum += post[pf * MAX_TYPES + k]; }
                for (int k = 0; k < types; k++) post[pf * MAX_TYPES + k] /= sum;
                if (s == n) break;
            }
            if (s == 0 || g_obs[sel[s]].pf != g_obs[sel[s - 1]].pf) { pf++; for (int k = 0; k < types; k++) ll[k] = 0; }
            const Obs* o = &g_obs[sel[s]];
            for (int k = 0; k < types; k++) { double p = clipp(obs_p(m, o, k)); ll[k] += o->y ? log(p) : log(1 - p); }
        }
        double total[MAX_TYPES] = {0};
        for (int p = 0; p < pfs; p++) for (int k = 0; k < types; k++) total[k] += post[p * MAX_TYPES + k];
        double norm = 0;
        for (int k = 0; k < types; k++) { m->pi[k] = fmax(1e-3, total[k] / pfs); norm += m->pi[k]; }
        for (int k = 0; k < types; k++) m->pi[k] /= norm;
        int e = 0;
        pf = -1;
        for (int s = 0; s < n; s++) {
            if (s == 0 || g_obs[sel[s]].pf != g_obs[sel[s - 1]].pf) pf++;
            for (int k = 0; k < types; k++) { esel[e] = sel[s]; ewt[e] = g_obs[sel[s]].weight * post[pf * MAX_TYPES + k]; etype[e++] = (uint8_t)k; }
        }
        newton(&m->g, esel, ewt, etype, e, 1, 4);
    }
    free(wt); free(post); free(esel); free(ewt); free(etype);
}

/* ---------- held-out scoring ---------- */

typedef struct {
    double loss[2], n[2], events[2];           /* by style: nats, opportunities, events */
    double fold_loss[FOLDS], source_loss[2], source_n[2], clean_loss, clean_n, clean_events;
} Cv;

static void score_obs(const Model* m, int fold, Cv* cv, int only_clean) {
    int pf = -1;
    double post[MAX_TYPES];
    for (int i = 0; i < g_obs_count; i++) {
        const Obs* o = &g_obs[i];
        if (fold >= 0 && o->fold != fold) continue;
        if (only_clean && !(o->clean && o->style == 0)) continue;
        if (o->pf != pf) { pf = o->pf; for (int k = 0; k < m->types; k++) post[k] = m->pi[k]; if (m->types == 1) post[0] = 1; }
        double pk[MAX_TYPES], p = 0;
        for (int k = 0; k < m->types; k++) { pk[k] = clipp(obs_p(m, o, k)); p += post[k] * pk[k]; }
        p = clipp(p);
        double l = -(o->y ? log(p) : log(1 - p)) * o->weight;
        double sum = 0;
        for (int k = 0; k < m->types; k++) { post[k] *= o->y ? pk[k] : 1 - pk[k]; sum += post[k]; }
        for (int k = 0; k < m->types; k++) post[k] /= sum;
        cv->loss[o->style] += l; cv->n[o->style] += o->weight; cv->events[o->style] += o->y * o->weight;
        if (o->style == 0) {
            if (fold >= 0) cv->fold_loss[fold] += l;
            cv->source_loss[o->source] += l; cv->source_n[o->source] += o->weight;
            if (o->clean) { cv->clean_loss += l; cv->clean_n += o->weight; cv->clean_events += o->y * o->weight; }
        }
    }
}

static int has_events(void) { double y = 0; for (int i = 0; i < g_obs_count; i++) if (g_obs[i].style == 0) y += g_obs[i].y; return y > 0; }

/** Each fold scored under the fit of the others (the rows of the OTHER style only), the DHAROK rows under the fit of their fold. */
static void cv_run(const Hazard* hz, uint32_t groups, int constant, Cv* cv) {
    build(hz, groups, constant);
    memset(cv, 0, sizeof(*cv));
    int* sel = malloc((size_t)g_obs_count * sizeof(int));
    for (int f = 0; f < FOLDS; f++) {
        int n = 0;
        for (int i = 0; i < g_obs_count; i++) if (g_obs[i].fold != f && g_obs[i].style == 0) sel[n++] = i;
        Model m;
        fit_model(&m, sel, n, hz->mixture && !constant && (groups & EG_TYPE), 2);
        score_obs(&m, f, cv, 0);
    }
    free(sel);
}

static void fit_all(const Hazard* hz, uint32_t groups, int constant, Model* m, double* events, double* opportunities) {
    build(hz, groups, constant);
    int* sel = malloc((size_t)g_obs_count * sizeof(int));
    int n = 0;
    *events = *opportunities = 0;
    for (int i = 0; i < g_obs_count; i++) if (g_obs[i].style == 0) { sel[n++] = i; *events += g_obs[i].y * g_obs[i].weight; *opportunities += g_obs[i].weight; }
    fit_model(m, sel, n, hz->mixture && !constant && (groups & EG_TYPE), 2);
    free(sel);
}

/* ---------- the hazards ---------- */

static Lab lab_eat(const FitRow* r, int arg) { (void)arg; return (Lab){r->in.since_eat >= LOCK_TICKS && !r->masked, r->ate}; }
static Lab lab_swing(const FitRow* r, int arg) { (void)arg; return (Lab){r->in.ready && !r->ate && !r->swing_unknown, r->swung}; }
static Lab lab_veng(const FitRow* r, int arg) { (void)arg; return (Lab){r->in.veng_ok, r->veng}; }
/** arg = kind * 2 + seen: the swing is the spec of the weapon (the axe swing), among his swings, for a weapon he has (not) used before in the fight. The maul is not a swing. */
static Lab lab_spec(const FitRow* r, int arg) {
    int k = arg >> 1, seen = arg & 1;
    if ((r->in.seen_mask >> k & 1) != seen) return (Lab){0, 0};
    if (k == WK_MAUL) return (Lab){r->in.energy_ok[k] && !r->ate, r->launch == WK_MAUL};
    return (Lab){r->swung && !r->swing_unknown && r->launch != WK_MAUL && (k == WK_AXE || r->in.energy_ok[k]), r->launch == k};
}

static void feat_eat(const OppIn* in, int arg, uint32_t groups, double* x) { (void)arg; eat_x(in, groups, 0, x); }
static void feat_swing(const OppIn* in, int arg, uint32_t groups, double* x) { (void)arg; swing_x(in, groups, x); }
static void feat_veng(const OppIn* in, int arg, uint32_t groups, double* x) { (void)arg; veng_x(in, groups, x); }
static void feat_spec(const OppIn* in, int arg, uint32_t groups, double* x) { spec_x(in, arg >> 1, arg & 1, groups, x); }

enum { HZ_EAT, HZ_SWING, HZ_VENG, HZ_SPEC0, HZ_COUNT = HZ_SPEC0 + WK_COUNT * 2 };
static Hazard g_hz[HZ_COUNT];
static char g_hz_name[HZ_COUNT][64];
/** Which hazards the Oct 8 deployment had (and so the structure to compare with). */
static int g_has_old[HZ_COUNT];

static void define_hazards(void) {
    g_hz[HZ_EAT] = (Hazard){"eat", lab_eat, feat_eat, 0, FIT_SAMPLES, EG_HP | EG_TYPE, {EG_TAU, EG_SPEC, EG_VENG, EG_LOW}, 1};
    g_hz[HZ_SWING] = (Hazard){"swing when ready", lab_swing, feat_swing, 0, 1, 0, {SW_STREAK, SW_IDLE}, 0};
    g_hz[HZ_VENG] = (Hazard){"Vengeance cast", lab_veng, feat_veng, 0, 1, 0, {VG_OURS}, 0};
    g_has_old[HZ_EAT] = g_has_old[HZ_SWING] = g_has_old[HZ_VENG] = 1;
    for (int k = 0; k < WK_COUNT; k++)
        for (int seen = 0; seen < 2; seen++) {
            Hazard* h = &g_hz[HZ_SPEC0 + k * 2 + seen];
            uint32_t base = !seen ? 0 : k == WK_VW ? SPG_BAND | SPG_FOLLOW : k == WK_MAUL ? SPG_FOLLOW : 0;
            *h = (Hazard){0, lab_spec, feat_spec, k * 2 + seen, 1, base, {SPG_HP, SPG_BAND}, 0};
            int n = 2;
            if (k == WK_VW || k == WK_MAUL) h->extra[n++] = SPG_FOLLOW;
            if (seen) h->extra[n++] = SPG_WIELD;
            snprintf(g_hz_name[HZ_SPEC0 + k * 2 + seen], 64, "%s, %s", WK_NAME[k], seen ? "used before in the fight" : "first use");
            h->name = g_hz_name[HZ_SPEC0 + k * 2 + seen];
            g_has_old[HZ_SPEC0 + k * 2 + seen] = seen && (k == WK_VW || k == WK_MAUL);
        }
}

typedef struct {
    uint32_t groups;
    int constant, few;
    Model model;
    double events, opportunities;
    Cv cv_const, cv_old, cv_new;
} Choice;

static const char* group_names(int hazard, uint32_t g, char* out) {
    out[0] = 0;
    if (hazard == HZ_EAT) {
        if (g & EG_HP) strcat(out, "hp "); if (g & EG_TYPE) strcat(out, "type "); if (g & EG_TAU) strcat(out, "our-timer ");
        if (g & EG_SPEC) strcat(out, "our-spec "); if (g & EG_VENG) strcat(out, "our-veng "); if (g & EG_LOW) strcat(out, "low-hp*soon ");
    } else if (hazard == HZ_SWING) {
        if (g & SW_STREAK) strcat(out, "streak "); if (g & SW_IDLE) strcat(out, "idle ");
    } else if (hazard == HZ_VENG) {
        if (g & VG_OURS) strcat(out, "our-veng ");
    } else {
        if (g & SPG_HP) strcat(out, "our-hp "); if (g & SPG_BAND) strcat(out, "our-bar-band "); if (g & SPG_FOLLOW) strcat(out, "follow "); if (g & SPG_WIELD) strcat(out, "wielding ");
    }
    if (!out[0]) strcpy(out, "(intercept and base)");
    return out;
}

static double per(const Cv* cv) { return cv->n[0] > 0 ? cv->loss[0] / cv->n[0] : 0; }

static void report_row(FILE* rep, const char* hazard, const char* variant, const Cv* cv, const Cv* against) {
    fprintf(rep, "| %s | %s | %.1f | %.0f | %.5f | %.1f | %+.1f |\n", hazard, variant, cv->events[0], cv->n[0], per(cv), cv->loss[0], against ? against->loss[0] - cv->loss[0] : 0.0);
}

/** Constant rate, the structure of the deployment, then groups added greedily while each buys 2 nats held out; the structure is dropped for a constant that is within a nat of it. */
static void select_hazard(int h, FILE* rep, Choice* c) {
    const Hazard* hz = &g_hz[h];
    char names[160];
    memset(c, 0, sizeof(*c));
    cv_run(hz, 0, 1, &c->cv_const);
    c->events = c->cv_const.events[0];
    fprintf(stderr, "%s: const %.1f nats over %.0f, events %.1f\n", hz->name, c->cv_const.loss[0], c->cv_const.n[0], c->events);
    report_row(rep, hz->name, "constant rate", &c->cv_const, NULL);
    if (c->events < 3) {
        c->few = 1; c->constant = 1; c->cv_new = c->cv_old = c->cv_const;
        fit_all(hz, 0, 1, &c->model, &c->events, &c->opportunities);
        fprintf(rep, "| %s | fewer than 3 events: constant | | | | | |\n", hz->name);
        return;
    }
    uint32_t cur = hz->base;
    cv_run(hz, cur, 0, &c->cv_old);
    report_row(rep, hz->name, g_has_old[h] ? group_names(h, cur, names) : "structure with no hazard of the deployment", &c->cv_old, &c->cv_const);
    c->cv_new = c->cv_old;
    if (hz->mixture) {
        Cv alt;
        cv_run(hz, cur & ~EG_TYPE, 0, &alt);
        report_row(rep, hz->name, "without eater types", &alt, &c->cv_const);
        cv_run(hz, 0, 0, &alt);
        report_row(rep, hz->name, "without hp and types", &alt, &c->cv_const);
    }
    for (;;) {
        int best = -1;
        Cv best_cv = c->cv_new;
        for (int e = 0; e < 6; e++) {
            uint32_t g = hz->extra[e];
            if (!g || (cur & g)) continue;
            Cv trial;
            cv_run(hz, cur | g, 0, &trial);
            group_names(h, cur | g, names);
            report_row(rep, hz->name, names, &trial, &c->cv_const);
            if (trial.loss[0] < best_cv.loss[0] - 2.0) { best = e; best_cv = trial; }
        }
        if (best < 0) break;
        cur |= hz->extra[best];
        c->cv_new = best_cv;
    }
    c->groups = cur;
    if (c->cv_const.loss[0] <= c->cv_new.loss[0] + 1.0) { c->constant = 1; c->cv_new = c->cv_const; }
    fit_all(hz, c->constant ? 0 : c->groups, c->constant, &c->model, &c->events, &c->opportunities);
}

/* ---------- the Oct 8 deployment, read and scored where it never saw the recording ---------- */

typedef struct { double w[10]; long events, opportunities; } OldLogit;
typedef struct { OldLogit logit[6]; int eat_action; float serving[2][4]; int served[2][4]; } OldFit;
typedef struct { double w[24]; } OldEatLogit;
typedef struct { int types; double pi[3]; OldEatLogit logit; } OldMixture;

static int g_have_old;
static Model g_old_model[HZ_COUNT];

static void load_old(const char* model_path, const char* mixture_path) {
    static OldFit fit;
    static OldMixture mix;
    FILE* f = fopen(model_path, "rb");
    if (!f) return;
    if (fread(&fit, sizeof(fit), 1, f) != 1) { fprintf(stderr, "%s: not a deployed fit\n", model_path); abort(); }
    fclose(f);
    f = fopen(mixture_path, "rb");
    if (!f || fread(&mix, sizeof(mix), 1, f) != 1) { fprintf(stderr, "%s: not a deployed mixture\n", mixture_path); abort(); }
    fclose(f);
    Model* e = &g_old_model[HZ_EAT];
    for (int i = 0; i < 14; i++) e->g.w[i] = mix.logit.w[i];
    e->g.w[14] = mix.logit.w[21]; e->g.w[15] = mix.logit.w[22];
    e->types = mix.types;
    for (int k = 0; k < mix.types; k++) e->pi[k] = mix.pi[k];
    Model* s = &g_old_model[HZ_SWING];
    s->types = 1;
    for (int i = 0; i < 5; i++) s->g.w[i] = fit.logit[2].w[i];
    Model* vw = &g_old_model[HZ_SPEC0 + WK_VW * 2 + 1];
    vw->types = 1;
    vw->g.w[0] = fit.logit[3].w[0]; vw->g.w[2] = fit.logit[3].w[1]; vw->g.w[3] = fit.logit[3].w[2]; vw->g.w[4] = fit.logit[3].w[3];
    Model* mm = &g_old_model[HZ_SPEC0 + WK_MAUL * 2 + 1];
    mm->types = 1;
    mm->g.w[0] = fit.logit[4].w[0]; mm->g.w[4] = fit.logit[4].w[1];
    Model* v = &g_old_model[HZ_VENG];
    v->types = 1;
    v->g.w[0] = fit.logit[5].w[0];
    g_have_old = 1;
}

/* ---------- servings: the heal of a meal from the bar on both sides of it ---------- */

enum { SERVING_MIN = 6, SERVING_SINGLE_MAX = 30, SERVING_DOUBLE_MAX = 48, SERVING_WINDOW = 10 };

static int bar_mid(int ratio) { return (warning_bar_low(ratio, 30) + warning_bar_high(ratio, 30)) / 2; }

static void infer_servings(OppFit* fit, int counts_out[2][3]) {
    int counts[2][3] = {{0}};
    int fight = -1, a_tick = 0, a_bar = -1, dmg = 0, eats = 0, warned = 0;
    for (int i = 0; i < g_count; i++) {
        const FitRow* r = &g_rows[i];
        if (g_fight[i] != fight) { fight = g_fight[i]; a_bar = -1; dmg = eats = 0; }
        if (r->style != 0) continue;
        if (r->ate) { eats++; warned = r->in.warned; }
        dmg += r->dmg;
        if (r->nhit > 0 && r->bar > 0) {
            if (a_bar > 0 && eats == 1 && r->tick - a_tick <= SERVING_WINDOW) {
                int heal = bar_mid(r->bar) + dmg - bar_mid(a_bar);
                if (heal >= SERVING_MIN) counts[warned][heal < SERVING_SINGLE_MAX ? 0 : heal < SERVING_DOUBLE_MAX ? 1 : 2]++;
            }
            a_bar = r->bar; a_tick = r->tick; dmg = eats = 0;
        }
    }
    for (int w = 0; w < 2; w++) {
        int total = counts[w][0] + counts[w][1] + counts[w][2];
        for (int k = 0; k < 3; k++) counts_out[w][k] = counts[w][k];
        if (!total) { for (int k = 0; k < 4; k++) fit->serving[w][k] = SERVING_SHARES[k]; continue; }
        fit->serving[w][0] = (float)counts[w][0] / total;
        fit->serving[w][1] = fit->serving[w][2] = (float)counts[w][1] / total / 2;
        fit->serving[w][3] = (float)counts[w][2] / total;
    }
}

/* ---------- main ---------- */

static int cmp_session(const void* a, const void* b) { return strcmp(g_session_names[*(const int*)a], g_session_names[*(const int*)b]); }

static void assign_folds(void) {
    for (int source = 0; source < 2; source++) {
        int ids[SP_MAX_SESSIONS * 2], n = 0;
        for (int s = 0; s < g_sessions; s++) if (g_session_source[s] == source) ids[n++] = s;
        qsort(ids, (size_t)n, sizeof(int), cmp_session);
        for (int i = 0; i < n; i++) {
            g_fold[ids[i]] = i % FOLDS;
            g_clean[ids[i]] = source == 0 && strncmp(g_session_names[ids[i]], "20261009", 8) >= 0;
        }
    }
}

int main(int argc, char** argv) {
    if (argc < 3) { fprintf(stderr, "usage: fit_opponent OUT_DIR SPECTATOR_DUELS|- tape...\n"); return 2; }
    const char* out_dir = argv[1];
    observer_register_items();
    g_rows = malloc((size_t)MAX_ROWS * sizeof(FitRow));
    g_fight = malloc((size_t)MAX_ROWS * sizeof(int));
    g_obs = malloc((size_t)MAX_ROWS * FIT_SAMPLES * sizeof(Obs));
    assert(g_rows && g_fight && g_obs);
    int tapes = 0, own_rows = 0;
    for (int i = 3; i < argc; i++) {
        static Tape tape;
        static SpecDuel duel;
        tape_load(&tape, argv[i]);
        duel_from_tape(&tape, &duel);
        add_series(&duel, 0, add_session(tape.session, 0));
        free(duel.rows); free(duel.eq);
        tape_free(&tape);
        tapes++;
    }
    own_rows = g_count;
    int duels = 0;
    if (strcmp(argv[2], "-")) {
        static SpecCorpus corpus;
        spec_load(&corpus, argv[2]);
        for (int i = 0; i < corpus.count; i++) {
            int s = add_session(corpus.sessions[corpus.duels[i].session], 1);
            add_series(&corpus.duels[i], 0, s);
            add_series(&corpus.duels[i], 1, s);
            duels++;
        }
    }
    assign_folds();
    define_hazards();
    load_old("/Users/valtterivalo/.light2/opponent-loop/model.fit", "/Users/valtterivalo/.light2/opponent-loop/eat_mixture.fit");
    char path[512];
    snprintf(path, sizeof(path), "%s/oppfit.md", out_dir);
    FILE* rep = fopen(path, "w");
    assert(rep);
    int rows_by[2][2] = {{0}}, own_sessions = 0, spec_sessions = 0;
    for (int i = 0; i < g_count; i++) rows_by[g_rows[i].source][g_rows[i].style]++;
    for (int s = 0; s < g_sessions; s++) { own_sessions += g_session_source[s] == 0; spec_sessions += g_session_source[s] == 1; }
    fprintf(rep, "# Opponent fit: held out by recording\n\nCorpus: %d own tapes (%d recordings), %d spectated duels (%d recordings); rows own OTHER %d DHAROK %d, spectator OTHER %d DHAROK %d "
        "(a row is one tick of one fighter; spectator duels give both fighters). %d folds by recording; every score below is the log loss in nats of a fold under the fit of the other folds, "
        "fitted on OTHER-style rows only.\n\n", tapes, own_sessions, duels, spec_sessions, rows_by[0][0], rows_by[0][1], rows_by[1][0], rows_by[1][1], FOLDS);
    (void)own_rows;
    fprintf(rep, "| hazard | structure | events | opportunities | nats per opportunity | total nats | gain over constant |\n|---|---|---|---|---|---|---|\n");
    static Choice choice[HZ_COUNT];
    for (int h = 0; h < HZ_COUNT; h++) { select_hazard(h, rep, &choice[h]); fflush(rep); }

    /* the summary table */
    fprintf(rep, "\n## Summary: new fit against the structure of the Oct 8 deployment refitted on the same folds and against a constant rate\n\n"
        "| hazard | events | opportunities | constant | old structure (refit) | new | new - old (nats, total) | kept features | DHAROK-style rows: constant / new |\n|---|---|---|---|---|---|---|---|---|\n");
    char names[160];
    for (int h = 0; h < HZ_COUNT; h++) {
        const Choice* c = &choice[h];
        fprintf(rep, "| %s | %.1f | %.0f | %.5f | ", g_hz[h].name, c->cv_const.events[0], c->cv_const.n[0], per(&c->cv_const));
        if (g_has_old[h] && !c->few) fprintf(rep, "%.5f | ", per(&c->cv_old)); else fprintf(rep, "n/a | ");
        fprintf(rep, "%.5f | ", per(&c->cv_new));
        if (g_has_old[h] && !c->few) fprintf(rep, "%+.1f | ", c->cv_old.loss[0] - c->cv_new.loss[0]); else fprintf(rep, "n/a | ");
        fprintf(rep, "%s | ", c->few ? "too few events, constant" : c->constant ? "constant rate (the structure did not beat it by a nat)" : group_names(h, c->groups, names));
        if (c->cv_const.n[1] > 0) fprintf(rep, "%.5f (%.0f ev / %.0f) / %.5f |\n", c->cv_const.loss[1] / c->cv_const.n[1], c->cv_const.events[1], c->cv_const.n[1], c->cv_new.loss[1] / c->cv_new.n[1]);
        else fprintf(rep, "none |\n");
    }

    /* the deployed files, on the recordings they never saw */
    if (g_have_old) {
        fprintf(rep, "\n## Deployed Oct 8 files on the own recordings dated Oct 9 or later (they were fitted on tapes of Oct 5 to 8)\n\n"
            "| hazard | events | opportunities | constant | deployed Oct 8 | old structure (refit, held out) | new (held out) |\n|---|---|---|---|---|---|---|\n");
        for (int h = 0; h < HZ_COUNT; h++) {
            if (!g_has_old[h] || choice[h].few) continue;
            const Choice* c = &choice[h];
            build(&g_hz[h], g_hz[h].base, 0);
            Cv cv;
            memset(&cv, 0, sizeof(cv));
            score_obs(&g_old_model[h], -1, &cv, 1);
            double n = c->cv_const.clean_n;
            if (n <= 0) continue;
            fprintf(rep, "| %s | %.1f | %.0f | %.5f | %.5f | %.5f | %.5f |\n", g_hz[h].name, c->cv_const.clean_events, n, c->cv_const.clean_loss / n, cv.loss[0] / cv.n[0],
                c->cv_old.clean_loss / n, c->cv_new.clean_loss / n);
        }
    }

    /* the files */
    OppFit fit;
    memset(&fit, 0, sizeof(fit));
    fit.magic = OPP_MAGIC; fit.version = OPP_VERSION;
    fit.swing = choice[HZ_SWING].model.g; fit.swing_groups = choice[HZ_SWING].constant ? 0 : choice[HZ_SWING].groups;
    fit.veng = choice[HZ_VENG].model.g; fit.veng_groups = choice[HZ_VENG].constant ? 0 : choice[HZ_VENG].groups;
    for (int k = 0; k < WK_COUNT; k++)
        for (int seen = 0; seen < 2; seen++) {
            const Choice* c = &choice[HZ_SPEC0 + k * 2 + seen];
            fit.spec[k][seen] = c->model.g;
            fit.spec_groups[k][seen] = c->constant ? 0 : c->groups;
            fit.events[k] += c->events; fit.opportunities[k] += c->opportunities;
        }
    int served[2][3];
    infer_servings(&fit, served);
    fprintf(rep, "\n## Servings (meal size from the bar on both sides of a meal): unwarned single/double/triple %d/%d/%d, warned %d/%d/%d; shares unwarned %.3f %.3f %.3f %.3f, warned %.3f %.3f %.3f %.3f\n",
        served[0][0], served[0][1], served[0][2], served[1][0], served[1][1], served[1][2], fit.serving[0][0], fit.serving[0][1], fit.serving[0][2], fit.serving[0][3],
        fit.serving[1][0], fit.serving[1][1], fit.serving[1][2], fit.serving[1][3]);
    EatMixture mix;
    memset(&mix, 0, sizeof(mix));
    mix.magic = OPP_MAGIC; mix.version = OPP_VERSION;
    const Choice* eat = &choice[HZ_EAT];
    mix.glm = eat->model.g;
    mix.groups = eat->constant ? 0 : eat->groups;
    mix.types = eat->model.types;
    for (int k = 0; k < mix.types; k++) mix.pi[k] = mix.types == 1 ? 1 : eat->model.pi[k];
    mix.events = (long)eat->events; mix.opportunities = (long)eat->opportunities;
    fprintf(rep, "\nEater types: %d, shares", mix.types);
    for (int k = 0; k < mix.types; k++) fprintf(rep, " %.3f", mix.pi[k]);
    fprintf(rep, "\n");
    snprintf(path, sizeof(path), "%s/model.fit", out_dir);
    opp_write(path, &fit, sizeof(fit));
    snprintf(path, sizeof(path), "%s/eat_mixture.fit", out_dir);
    opp_write(path, &mix, sizeof(mix));
    fclose(rep);
    return 0;
}
