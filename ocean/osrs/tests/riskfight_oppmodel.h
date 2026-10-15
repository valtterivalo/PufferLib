#ifndef RISKFIGHT_OPPMODEL_H
#define RISKFIGHT_OPPMODEL_H
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The fitted opponent: small logistic hazards on what he can read of the fight, one set of coefficients for every opponent.
 *
 * Every hazard is a sigmoid of a dot product between the weights of a Glm and the features of one OppIn, the inputs of one tick of
 * his as he reads them: his own bar and hitpoints, the bar of ours he sees, our weapon and swing timer and Vengeance, the history
 * of what he did. The fit (fit_opponent.c) builds an OppIn from a recording and the sim builds one from its state, through the
 * same feature functions, so a tick of a recording and a tick of a rollout are scored by the same arithmetic. */

enum { OPP_DIM = 24, MAX_TYPES = 3, OPP_LAG_NONE = 99 };

/** A spec weapon of his arsenal (or the axe he swaps to), by the hazard that models its use. MAUL is the instant spec: no swing. */
typedef enum { WK_VW, WK_AGS, WK_CLAWS, WK_AXE, WK_MAUL, WK_COUNT } WeaponKind;
enum { WK_NONE = -1 };
static const char* const WK_NAME[WK_COUNT] = {"Voidwaker spec", "AGS spec", "dragon claws spec", "Dharok's greataxe swing", "granite maul spec"};
static const int WK_COST[WK_COUNT] = {50, 50, 50, 0, 50};

typedef struct { double w[OPP_DIM]; } Glm;

/** Everything a hazard may read at the start of one of his ticks. `his_hp` is his hitpoints: sampled inside his bar in a fit, the
 * sim's own in a rollout. Ticks are the decision tick; `our_tau` is how many ticks until our next swing may land (0: ready now, -1: not known). */
typedef struct {
    int his_hp, his_bar_ratio, our_bar_ratio, our_hp;
    int lag, warned, since_eat, since_vw_spec;
    int ready, ready_age, on_time_streak, since_swing;
    int our_tau, our_spec, our_veng;
    int seen_mask, wield_class;
    int energy_ok[WK_COUNT], has[WK_COUNT], veng_ok;
} OppIn;

static double sigmoid(double z) { return 1.0 / (1.0 + exp(-z)); }
static double glm_z(const Glm* g, const double* x) { double z = 0; for (int i = 0; i < OPP_DIM; i++) z += g->w[i] * x[i]; return z; }
static double glm_p(const Glm* g, const double* x) { return sigmoid(glm_z(g, x)); }

static int opp_imax(int a, int b) { return a > b ? a : b; }

/* ---------- eat ---------- */

/** Feature groups of the eat hazard beyond its base (his bar band, lag since the warning, warned): the rest of the layout is fixed. */
enum { EG_HP = 1, EG_TYPE = 2, EG_TAU = 4, EG_SPEC = 8, EG_VENG = 16, EG_LOW = 32 };

static int opp_lag_bin(int lag) { return lag < 1 || lag >= OPP_LAG_NONE ? 0 : lag == 1 ? 1 : lag == 2 ? 2 : lag == 3 ? 3 : lag <= 8 ? 4 : 0; }
static int opp_band4(int bar_ratio) { return bar_ratio <= 8 ? 0 : bar_ratio <= 13 ? 1 : bar_ratio <= 24 ? 2 : 3; }
static int opp_our_hp_band(int hp) { return hp < 45 ? 0 : hp <= 85 ? 1 : 2; }

static int opp_hp_bucket(int hp) { return hp <= 30 ? 0 : hp <= 44 ? 1 : hp <= 60 ? 2 : hp <= 87 ? 3 : 4; }

static void eat_x(const OppIn* in, uint32_t groups, int type, double* x) {
    memset(x, 0, OPP_DIM * sizeof(double));
    x[0] = 1;
    int band = opp_band4(in->his_bar_ratio);
    if (band) x[band] = 1;
    int lag = opp_lag_bin(in->lag);
    if (lag) x[3 + lag] = 1;
    if (in->warned) x[8] = 1;
    if (groups & EG_HP) x[9 + opp_hp_bucket(in->his_hp)] = 1;
    if ((groups & EG_TYPE) && type > 0) x[13 + type] = 1;
    int soon = in->our_tau >= 0 && in->our_tau <= 1;
    if ((groups & EG_TAU) && in->our_tau >= 0 && in->our_tau <= 2) x[16 + in->our_tau] = 1;
    if (groups & EG_SPEC) { x[19] = in->our_spec; x[20] = in->our_spec && soon; }
    if (groups & EG_VENG) x[21] = in->our_veng;
    if ((groups & EG_LOW) && soon && in->his_hp <= 60) x[22] = 1;
}

/* ---------- swing cadence ---------- */

enum { SW_STREAK = 1, SW_IDLE = 2 };

static void swing_x(const OppIn* in, uint32_t groups, double* x) {
    memset(x, 0, OPP_DIM * sizeof(double));
    x[0] = 1;
    int age = in->ready_age;
    int b = age <= 1 ? 0 : age == 2 ? 1 : age <= 5 ? 2 : 3;
    if (b) x[b] = 1;
    if (in->since_eat >= 1 && in->since_eat <= 3) x[4] = 1;
    if (groups & SW_STREAK) { x[5] = in->on_time_streak >= 1; x[6] = in->on_time_streak >= 3; }
    if (groups & SW_IDLE) x[7] = in->since_swing > 12;
}

/* ---------- spec weapons ---------- */

enum { SPG_HP = 1, SPG_BAND = 2, SPG_FOLLOW = 4, SPG_WIELD = 8 };

/** The hazard that his next swing is weapon `k`'s spec (the axe: his swing with it): `seen` he has used it before in this fight. */
static void spec_x(const OppIn* in, int k, int seen, uint32_t groups, double* x) {
    memset(x, 0, OPP_DIM * sizeof(double));
    x[0] = 1;
    if (groups & SPG_HP) x[1] = opp_imax(35, in->our_hp) / 50.0 - 1.0;
    if (seen) {
        if (groups & SPG_BAND) { int b = opp_our_hp_band(in->our_hp); if (b) x[1 + b] = 1; }
        if ((groups & SPG_FOLLOW) && ((k == WK_VW && in->since_vw_spec >= 1 && in->since_vw_spec <= 5) || (k == WK_MAUL && in->since_vw_spec <= 1))) x[4] = 1;
        if ((groups & SPG_WIELD) && in->wield_class == k) x[5] = 1;
    }
}

/* ---------- Vengeance ---------- */

enum { VG_OURS = 1 };
static void veng_x(const OppIn* in, uint32_t groups, double* x) {
    memset(x, 0, OPP_DIM * sizeof(double));
    x[0] = 1;
    if (groups & VG_OURS) x[1] = in->our_veng;
}

/* ---------- the model ---------- */

enum { OPP_MAGIC = 0x4f505032, OPP_VERSION = 2 };

/** The fitted policy besides the eat hazard (the eat hazard is its own file, a mixture over eater types). */
typedef struct {
    uint32_t magic, version;
    uint32_t swing_groups, veng_groups, spec_groups[WK_COUNT][2];
    Glm swing, veng;
    Glm spec[WK_COUNT][2]; /* [k][seen]; the maul's rows are not conditioned on a swing */
    float serving[2][4];
    long events[WK_COUNT + 2], opportunities[WK_COUNT + 2];
} OppFit;

typedef struct { uint32_t magic, version, groups; int types; double pi[MAX_TYPES]; Glm glm; long events, opportunities; } EatMixture;

static void opp_fail(const char* path, const char* what) { fprintf(stderr, "%s: %s\n", path, what); abort(); }

static void opp_read(const char* path, void* out, size_t size) {
    FILE* f = fopen(path, "rb");
    if (!f) opp_fail(path, "cannot open opponent fit");
    if (fread(out, size, 1, f) != 1) opp_fail(path, "opponent fit has the wrong size");
    char extra;
    if (fread(&extra, 1, 1, f) == 1) opp_fail(path, "opponent fit has the wrong size");
    fclose(f);
}

static void opp_write(const char* path, const void* data, size_t size) {
    FILE* f = fopen(path, "wb");
    if (!f || fwrite(data, size, 1, f) != 1) opp_fail(path, "cannot write opponent fit");
    fclose(f);
}

static void opp_check(const OppFit* fit, const EatMixture* mix, const char* who) {
    double pi = 0;
    for (int k = 0; k < mix->types; k++) pi += mix->pi[k];
    if (fit->magic != OPP_MAGIC || fit->version != OPP_VERSION || mix->magic != OPP_MAGIC || mix->version != OPP_VERSION ||
        mix->types < 1 || mix->types > MAX_TYPES || fabs(pi - 1) > 1e-6) opp_fail(who, "opponent fit files are not of this version");
    for (int k = 0; k < WK_COUNT; k++)
        for (int s = 0; s < 2; s++)
            if (fit->spec[k][s].w[0] == 0) { fprintf(stderr, "%s: hazard %s (seen %d) sits at its default\n", who, WK_NAME[k], s); abort(); }
    if (fit->swing.w[0] == 0 || fit->veng.w[0] == 0 || mix->glm.w[0] == 0) opp_fail(who, "a hazard sits at its default");
}

/** Loads and checks the two fit files. */
static void opp_load(const char* model_path, const char* eat_path, OppFit* fit, EatMixture* mix) {
    opp_read(model_path, fit, sizeof(*fit));
    opp_read(eat_path, mix, sizeof(*mix));
    opp_check(fit, mix, model_path);
}

/** The eater type of a rollout, drawn once from the mixture by `u` in [0, 1). */
static int eat_type_of(const EatMixture* mix, double u) {
    double acc = 0;
    int type = 0;
    for (; type < mix->types - 1; type++) { acc += mix->pi[type]; if (u < acc) break; }
    return type;
}
#endif
