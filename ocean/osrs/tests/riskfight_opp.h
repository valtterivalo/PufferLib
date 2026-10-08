#ifndef RISKFIGHT_OPP_H
#define RISKFIGHT_OPP_H
#include "riskfight_seed.h"
#include "riskfight_warning.h"
#include "../gearswapper/gs_wire.h"
#include <math.h>

/* Fitted opponent policy: six small logistic hazards on aggregate buckets, no per-opponent terms.
 * Every probability is a function of Hist plus the sim state, so one-step probing and rollouts share it. */
typedef enum { ACT_EAT_BIG, ACT_EAT_WARNING, ACT_SWING, ACT_VW_SPEC, ACT_MAUL_SPEC, ACT_VENGEANCE, ACT_COUNT } Action;
enum { MAX_DIM = 10, BIG_HIT_DAMAGE = 20, LAG_NONE = 99, ENERGY_FOR_SPEC = 50, RECENT_EAT = 3, FOLLOW_UP = 5, LOCK_TICKS = 2 };
static const int ACTION_DIM[ACT_COUNT] = {9, 9, 5, 4, 2, 1};
static const char* ACTION_NAME[ACT_COUNT] = {"eat (anchor: big hit)", "eat (anchor: warning)", "swing when ready", "VW spec given swing",
    "maul spec", "Vengeance cast"};

typedef struct {
    int big_hit_tick, since_eat, since_vw_spec, since_maul_spec, ready_age, last_swing_tick;
    Warning warning;
} Hist;

typedef struct { int n, idx[5]; } Sparse;
typedef struct { Sparse x; float y, weight; } Sample;
typedef struct { double w[MAX_DIM]; long events, opportunities; } Logit;
typedef struct { Logit logit[ACT_COUNT]; Action eat_action; float serving[2][4]; int served[2][4]; } OppFit;

static double sigmoid(double z) { return 1.0 / (1.0 + exp(-z)); }

static double logit_p(const Logit* l, const Sparse* x) {
    double z = 0;
    for (int i = 0; i < x->n; i++) z += l->w[x->idx[i]];
    return sigmoid(z);
}

static void logit_fit(Logit* l, int dim, const Sample* samples, int count) {
    memset(l, 0, sizeof(*l));
    double ridge = 0.5;
    for (int i = 0; i < count; i++) { l->events += samples[i].y > 0; l->opportunities++; }
    for (int iteration = 0; iteration < 30; iteration++) {
        double g[MAX_DIM] = {0}, h[MAX_DIM][MAX_DIM] = {{0}};
        for (int d = 1; d < dim; d++) { g[d] -= ridge * l->w[d]; h[d][d] += ridge; }
        for (int s = 0; s < count; s++) {
            double p = logit_p(l, &samples[s].x), wt = samples[s].weight, r = wt * (samples[s].y - p), v = wt * p * (1 - p);
            for (int a = 0; a < samples[s].x.n; a++) {
                g[samples[s].x.idx[a]] += r;
                for (int b = 0; b < samples[s].x.n; b++) h[samples[s].x.idx[a]][samples[s].x.idx[b]] += v;
            }
        }
        double m[MAX_DIM][MAX_DIM + 1];
        for (int a = 0; a < dim; a++) { for (int b = 0; b < dim; b++) m[a][b] = h[a][b] + (a == b ? 1e-6 : 0); m[a][dim] = g[a]; }
        for (int c = 0; c < dim; c++) {
            int pivot = c;
            for (int r = c + 1; r < dim; r++) if (fabs(m[r][c]) > fabs(m[pivot][c])) pivot = r;
            for (int k = 0; k <= dim; k++) { double t = m[c][k]; m[c][k] = m[pivot][k]; m[pivot][k] = t; }
            for (int r = 0; r < dim; r++) {
                if (r == c) continue;
                double f = m[r][c] / m[c][c];
                for (int k = c; k <= dim; k++) m[r][k] -= f * m[c][k];
            }
        }
        for (int a = 0; a < dim; a++) l->w[a] += m[a][dim] / m[a][a];
    }
}

static int lag_bin(int lag) { return lag < 1 || lag >= LAG_NONE ? 0 : lag == 1 ? 1 : lag == 2 ? 2 : lag == 3 ? 3 : lag <= 8 ? 4 : 0; }
static int hp_band4(int hp_bar_ratio) { return hp_bar_ratio <= 8 ? 0 : hp_bar_ratio <= 13 ? 1 : hp_bar_ratio <= 24 ? 2 : 3; }
static int our_hp_band(int bar_ratio) { int hp = bar_ratio <= 0 ? 0 : (bar_ratio * 99 + 15) / 30; return hp < 45 ? 0 : hp <= 85 ? 1 : 2; }

static Sparse eat_features(int band, int lag, int warned) {
    Sparse x = {1, {0}};
    if (warned) x.idx[x.n++] = 8;
    if (band) x.idx[x.n++] = band;
    int b = lag_bin(lag);
    if (b) x.idx[x.n++] = 3 + b;
    return x;
}

static Sparse swing_features(const Hist* h) {
    Sparse x = {1, {0}};
    int age = h->ready_age;
    int b = age <= 1 ? 0 : age == 2 ? 1 : age <= 5 ? 2 : 3;
    if (b) x.idx[x.n++] = b;
    if (h->since_eat >= 1 && h->since_eat <= RECENT_EAT) x.idx[x.n++] = 4;
    return x;
}

static Sparse vw_features(int our_bar, int follow) {
    Sparse x = {1, {0}};
    int b = our_hp_band(our_bar);
    if (b) x.idx[x.n++] = b;
    if (follow) x.idx[x.n++] = 3;
    return x;
}

static Sparse follow_features(int follow) {
    Sparse x = {1, {0}};
    if (follow) x.idx[x.n++] = 1;
    return x;
}

typedef struct {
    int eligible[ACT_COUNT];
    Sparse x[ACT_COUNT];
} Probe;

static int lag_of(int step_tick, int anchor) { return anchor == WARNING_NONE ? LAG_NONE : step_tick - anchor; }

/** What he knows when he decides, all of it readable from his side of a duel: his own bar, the bar of ours he sees, and the history of
 * what both showed. The fit on spectated duels and the sim build it the same way. */
typedef struct {
    int hp_ratio, our_bar_ratio;
    int vw_ok, maul_ok, veng_ok;
} HimInputs;

/** The features and the legal actions of the tick `tick` from his inputs and the history after the tick before. */
static Probe probe_from_inputs(const HimInputs* in, const Hist* h, int tick) {
    Probe p = {{0}};
    p.eligible[ACT_EAT_BIG] = p.eligible[ACT_EAT_WARNING] = h->since_eat >= LOCK_TICKS;
    int warned = warning_since(&h->warning) != WARNING_NONE;
    p.x[ACT_EAT_BIG] = eat_features(hp_band4(in->hp_ratio), lag_of(tick, h->big_hit_tick), warned);
    p.x[ACT_EAT_WARNING] = eat_features(hp_band4(in->hp_ratio), lag_of(tick, warning_since(&h->warning)), warned);
    p.eligible[ACT_SWING] = h->ready_age > 0;
    p.x[ACT_SWING] = swing_features(h);
    p.eligible[ACT_VW_SPEC] = in->vw_ok;
    int vw_follow = h->since_vw_spec >= 1 && h->since_vw_spec <= FOLLOW_UP;
    p.x[ACT_VW_SPEC] = vw_features(in->our_bar_ratio, vw_follow);
    p.eligible[ACT_MAUL_SPEC] = in->maul_ok;
    p.x[ACT_MAUL_SPEC] = follow_features(h->since_vw_spec <= 1);
    p.eligible[ACT_VENGEANCE] = in->veng_ok;
    p.x[ACT_VENGEANCE] = (Sparse){1, {0}};
    return p;
}

static int raw_of_item_id(uint8_t item);

/** What the sim shows him of himself and of us before tick `tick`. */
static Probe probe_state(const RiskfightState* s, const Hist* h, int tick, int kit_vw, int kit_maul) {
    const Player* him = &s->env.players[1];
    HimInputs in = {
        .hp_ratio = osrs_health_bar_ratio(him->current_hitpoints, 99, 30),
        .our_bar_ratio = s->visible[1].health_bar,
        .vw_ok = kit_vw && him->special_energy >= ENERGY_FOR_SPEC,
        .maul_ok = kit_maul && him->special_energy >= ENERGY_FOR_SPEC,
        .veng_ok = !him->veng_active && him->veng_cooldown <= 0,
    };
    return probe_from_inputs(&in, h, tick);
}

/** Ticks since his bar last moved on which a meal would have been hidden behind another animation. */
static int unseen_ticks(const Tape* tape, int tick) { return ((const Hist*)tape->hist_series)[tick - tape->lo].warning.unseen_count; }

static int worn_slot_item(const TapeTick* t, int worn) { return t->worn[worn] >= 0 ? raw_id_to_item(t->worn[worn]) : ITEM_NONE; }

/** The kit composition ids (item id plus the client's offset, -1 for none) a viewer reads of a fighter: the nine visible slots, no ring or ammunition. */
enum { KIT_SLOTS = 12 };
static void kit_of_equipped(const uint8_t equipped[NUM_GEAR_SLOTS], int kit[KIT_SLOTS]) {
    static const int GEAR_OF_KIT[KIT_SLOTS] = {GEAR_SLOT_HEAD, GEAR_SLOT_CAPE, GEAR_SLOT_NECK, GEAR_SLOT_WEAPON, GEAR_SLOT_BODY, GEAR_SLOT_SHIELD, -1,
        GEAR_SLOT_LEGS, -1, GEAR_SLOT_HANDS, GEAR_SLOT_FEET, -1};
    for (int k = 0; k < KIT_SLOTS; k++) kit[k] = GEAR_OF_KIT[k] < 0 || equipped[GEAR_OF_KIT[k]] == ITEM_NONE ? -1 : ITEM_DATABASE[equipped[GEAR_OF_KIT[k]]].item_id + GS_ITEM_OFFSET;
}

/** The most the visible weapon can hit for, as a viewer who knows nothing of its wielder's levels or energy reads it: the plugin's assumed
 * levels and gear, the spec if the weapon has one. 0 when the weapon's stats are unknown. */
static int assumed_reach(const int kit[KIT_SLOTS], int weapon, int* has_spec) {
    GsClient c = {0};
    c.equipment_count = KIT_SLOTS;
    memcpy(c.equipment, kit, sizeof(int) * KIT_SLOTS);
    GsArmament a;
    *has_spec = 0;
    if (!gs_armed(&c, GS_NO_BONUSES, weapon, &a)) return 0;
    *has_spec = GS_WEAPON_ROWS[a.weapon].cost > 0;
    return *has_spec && a.has_spec ? a.spec : a.normal;
}

/** One tick of what the other side showed of this fighter. */
typedef struct {
    int ate_visible, ate, animated, hits, damage;
    int bar_ratio, bar_scale;
    int weapon, has_spec, reach;
    int swing, vw, maul;
    int ready;
} HistInput;

static Hist hist_start(void) {
    return (Hist){.big_hit_tick = WARNING_NONE, .since_eat = 99, .since_vw_spec = 99, .since_maul_spec = 99, .warning = warning_start(), .last_swing_tick = -100};
}

/** Moves the history of a fighter one tick on. `ate` is his own knowledge of his meals, `ate_visible` the 829 the viewer saw. */
static void hist_update(Hist* h, int tick, const HistInput* in) {
    h->since_eat = in->ate ? 0 : h->since_eat + 1;
    h->since_vw_spec = in->vw ? 0 : h->since_vw_spec + 1;
    h->since_maul_spec = in->maul ? 0 : h->since_maul_spec + 1;
    if (in->swing) h->last_swing_tick = tick;
    h->ready_age = in->ready ? h->ready_age + 1 : 0;
    if (in->damage >= BIG_HIT_DAMAGE) h->big_hit_tick = tick;
    if (in->ate_visible) warning_ate(&h->warning, tick);
    if (in->animated) warning_animated(&h->warning, tick);
    if (in->hits) warning_hitsplat(&h->warning);
    if (in->damage) warning_struck(&h->warning, tick, in->damage);
    warning_observe(&h->warning, tick, in->bar_ratio, in->bar_scale, in->weapon, in->has_spec, in->reach);
}

static int attack_hitsplat(int type) { return type == 16 || type == 17 || type == 43; }

/** Hist after each tape tick lo..hi, from his animations, the hitsplats on him and the bar as shown. */
static void hist_series(const Tape* tape, Hist* out) {
    Hist h = hist_start();
    int ready_age = 0;
    const TapeTick* initial = tape_tick(tape, tape->lo);
    int clock_observed = initial->swing_him_tick >= 0 && initial->swing_him_speed > 0;
    int attack_clock = clock_observed ? initial->swing_him_speed - 1 - (tape->lo - 1 - initial->swing_him_tick) : -1;
    for (int x = tape->lo; x <= tape->hi; x++) {
        const TapeTick* t = tape_tick(tape, x);
        if (clock_observed) attack_clock--;
        HistInput in = {.bar_ratio = t->bar_now_ratio, .bar_scale = t->bar_now_scale};
        int other_anim = 0;
        for (int i = 0; i < tape->anim_count; i++) {
            const TapeAnim* a = &tape->anims[i];
            if (a->tick != x || a->side != SIDE_HIM) continue;
            in.ate |= a->anim == ANIM_EAT;
            if (clock_observed && a->anim == ANIM_EAT) attack_clock += consume_delay(CONSUME_HARD);
            in.vw |= a->anim == ANIM_VW_SPEC;
            in.maul |= a->anim == ANIM_MAUL_SPEC;
            other_anim |= a->anim != ANIM_EAT;
        }
        in.ate_visible = in.ate;
        in.animated = other_anim;
        for (int i = 0; i < tape->hit_count; i++) {
            const TapeHit* hit = &tape->hits[i];
            if (hit->tick != x || hit->side != SIDE_HIM) continue;
            in.hits++;
            if (attack_hitsplat(hit->type)) in.damage += hit->amount;
        }
        uint8_t equipped[NUM_GEAR_SLOTS];
        for (int i = 0; i < NUM_GEAR_SLOTS; i++) equipped[i] = ITEM_NONE;
        for (int i = 0; i < TAPE_WORN; i++) if (WORN_TO_GEAR[i] >= 0) equipped[WORN_TO_GEAR[i]] = (uint8_t)worn_slot_item(t, i);
        int kit[KIT_SLOTS];
        kit_of_equipped(equipped, kit);
        in.weapon = t->worn[3];
        in.reach = assumed_reach(kit, in.weapon, &in.has_spec);
        if (t->swing_him_tick == x) {
            attack_clock = t->swing_him_speed - 1;
            clock_observed = 1;
        }
        ready_age = attack_clock <= 0 ? ready_age + 1 : 0;
        in.ready = ready_age > 0;
        hist_update(&h, x, &in);
        h.ready_age = ready_age;
        out[x - tape->lo] = h;
    }
}

static int has_item(const RiskfightState* s, int agent, uint8_t item) {
    const Player* p = &s->env.players[agent];
    if (p->equipped[GEAR_SLOT_WEAPON] == item) return 1;
    for (int i = 0; i < OSRS_INVENTORY_SIZE; i++) if (osrs_inventory_cell_metadata(&p->inventory_cells[i])->item_idx == item) return 1;
    return 0;
}

static int is_maul_item(uint8_t item) { return item == ITEM_GRANITE_MAUL || item == ITEM_GRANITE_MAUL_ORNATE; }

static int has_maul(const RiskfightState* s, int agent) {
    return has_item(s, agent, ITEM_GRANITE_MAUL) || has_item(s, agent, ITEM_GRANITE_MAUL_ORNATE);
}

typedef struct { int ate_visible, ate, swung, vw, maul, damage_on_him, max_hit_on_him, hit_on_him; } StepEvents;
static int raw_of_item_id(uint8_t item) { return item == ITEM_NONE ? -1 : ITEM_DATABASE[item].item_id; }

/** Moves the history one sim step on. tick is the tape tick the step covered. */
static void hist_step(Hist* h, RiskfightState* s, int tick, const StepEvents* e) {
    const Player* him = &s->env.players[1];
    const Player* us = &s->env.players[0];
    HistInput in = {.ate_visible = e->ate_visible, .ate = e->ate, .animated = e->swung || e->hit_on_him, .hits = e->hit_on_him, .damage = e->damage_on_him,
        .bar_ratio = s->visible[0].health_bar, .bar_scale = OSRS_PLAYER_HEALTH_BAR_SCALE, .swing = e->swung, .vw = e->vw, .maul = e->maul,
        .ready = him->attack_timer <= 0};
    int kit[KIT_SLOTS];
    kit_of_equipped(us->equipped, kit);
    in.weapon = raw_of_item_id(us->equipped[GEAR_SLOT_WEAPON]);
    in.reach = assumed_reach(kit, in.weapon, &in.has_spec);
    hist_update(h, tick, &in);
}

static const float SERVING_SHARES[4] = {0.15f, 0.34f, 0.34f, 0.17f};

static void fit_default_servings(OppFit* fit) {
    for (int w = 0; w < 2; w++) for (int k = 0; k < 4; k++) fit->serving[w][k] = SERVING_SHARES[k];
}

enum { EAT_DIM = 24, EAT_SWING_TIMER = 2, EAT_MIXTURE = 4, MAX_TYPES = 3 };
typedef struct { double w[EAT_DIM]; } EatLogit;
typedef struct { int types; double pi[MAX_TYPES]; EatLogit logit; } EatMixture;
typedef struct { int base_n, base[9], hp, timer, y, masked, type, fight, group; double weight; } EatRow;
typedef struct { EatRow* rows; int count, capacity; } EatRows;

static int eat_features_of(const EatRow* r, int model, int* idx) {
    int n = 0;
    for (int i = 0; i < r->base_n; i++) idx[n++] = r->base[i];
    if (model == EAT_MIXTURE) {
        idx[n++] = r->hp <= 30 ? 9 : r->hp <= 44 ? 10 : r->hp <= 60 ? 11 : r->hp <= 87 ? 12 : 13;
        if (r->type > 0) idx[n++] = 20 + r->type;
        return n;
    }
    if (model >= 1) idx[n++] = r->hp <= 30 ? 9 : r->hp <= 44 ? 10 : r->hp <= 60 ? 11 : r->hp <= 87 ? 12 : 13;
    if (model >= 2) idx[n++] = r->timer == 0 ? 14 : r->timer == 1 ? 15 : r->timer == 2 ? 16 : 17;
    if (model >= 3) {
        int low = r->hp <= 60;
        if (low && r->timer == 0) idx[n++] = 18;
        if (low && r->timer == 1) idx[n++] = 19;
        if (low && r->timer == 2) idx[n++] = 20;
    }
    return n;
}

static double eat_p(const EatLogit* l, const EatRow* r, int model) {
    int idx[16];
    int n = eat_features_of(r, model, idx);
    double z = 0;
    for (int i = 0; i < n; i++) z += l->w[idx[i]];
    return sigmoid(z);
}


/** The eat hazard that replaces the logit when a model is loaded: his bar band, lag and warning plus his hitpoints and our swing timer. */
static struct { int on, model, types; double pi[MAX_TYPES]; EatLogit logit; } g_eat_model;

/** His chance to leave by teleport each tick: 14 teleports in 5094 ticks of the 38 recorded fights, flat across his hitpoints (0.4 percent a tick at every band). */
static const double ESCAPE_RATE = 14.0 / 5094;
static int g_escape_by_hp = 1;
static double g_eat_inert = 0, g_eat_scale = 1;
/** The same by his hitpoints, 44 or less, 45-60, 61-87, 88 and more: 4 of 690, 5 of 729, 5 of 2485 and 0 of 1069 recorded ticks by the health bar. */
static double escape_rate_at(int hitpoints) { return hitpoints <= 44 ? 4.0 / 690 : hitpoints <= 60 ? 5.0 / 729 : hitpoints <= 87 ? 5.0 / 2485 : 0; }


/** Every hazard fitted from events and the eat mixture loaded: a policy left at its defaults must never produce a result. */
static int fit_unfitted(const OppFit* fit) {
    for (int a = 0; a < ACT_COUNT; a++)
        if (fit->logit[a].events <= 0 || fit->logit[a].opportunities <= 0 || fit->logit[a].w[0] == 0) return a + 1;
    return 0;
}

static void assert_fitted(const OppFit* fit, const char* who) {
    int bad = fit_unfitted(fit);
    if (bad) { fprintf(stderr, "%s: hazard %d (%s) sits at its default\n", who, bad - 1, ACTION_NAME[bad - 1]); abort(); }
    double pi = 0, weights = 0;
    for (int k = 0; k < g_eat_model.types; k++) pi += g_eat_model.pi[k];
    for (int i = 0; i < EAT_DIM; i++) weights += fabs(g_eat_model.logit.w[i]);
    if (!g_eat_model.on || g_eat_model.model != EAT_MIXTURE || g_eat_model.types < 1 || fabs(pi - 1) > 1e-6 || weights == 0) { fprintf(stderr, "%s: eat mixture is not loaded\n", who); abort(); }
}

static double action_probability(const OppFit* fit, const Probe* p, Action a) {
    if (!p->eligible[a]) return 0;
    return logit_p(&fit->logit[a], &p->x[a]);
}

static int roll(uint32_t seed, int tick, int salt, double p) {
    return (int)(riskfight_sample_hash(seed, (uint32_t)tick, (uint32_t)salt) % 1000000u) < (int)(p * 1000000);
}

/** The fitted policy as sim actions: eat with a serving, else swing or idle, VW spec on a swing, maul spec, Vengeance. */
static void opp_fit_actions(const OppFit* fit, const RiskfightState* s, const Hist* h, int tick, uint32_t seed, int* actions) {
    float obs[RF_OBS_SIZE];
    riskfight_write_observation(s, 1, obs);
    static const RiskfightProfile quiet = {{0}, 0, 0, {0}, 0, 0, {{0}}};
    riskfight_script(obs, &quiet, s->script_seed[1], s->consume_ticks[1], actions);
    RiskfightObservedSelf self = riskfight_observed_self(obs);
    RiskfightObservedInventory inventory = riskfight_observed_inventory(obs);
    Probe probe = probe_state(s, h, tick, has_item(s, 1, ITEM_VOIDWAKER), has_maul(s, 1));
    int threatened = 0;
    if (g_ablation.threat > 0 && probe.eligible[fit->eat_action]) {
        const Player* us = &s->env.players[0];
        const Player* him = &s->env.players[1];
        int kit[KIT_SLOTS], has_spec;
        kit_of_equipped(us->equipped, kit);
        int reach = assumed_reach(kit, raw_of_item_id(us->equipped[GEAR_SLOT_WEAPON]), &has_spec);
        threatened = us->attack_timer <= g_ablation.threat_timer && him->current_hitpoints <= reach && roll(seed, tick, 47, g_ablation.threat);
    }
    double p_eat = action_probability(fit, &probe, fit->eat_action);
    if (g_eat_model.on && probe.eligible[fit->eat_action]) {
        EatRow row = {.base_n = probe.x[fit->eat_action].n, .hp = s->env.players[1].current_hitpoints, .timer = imax(0, s->env.players[0].attack_timer_uncapped)};
        if (g_eat_model.model == EAT_MIXTURE) {
            double u = (riskfight_sample_hash(g_rollout_seed, 0, 78) % 1000000u) / 1e6, acc = 0;
            for (row.type = 0; row.type < g_eat_model.types - 1; row.type++) { acc += g_eat_model.pi[row.type]; if (u < acc) break; }
        }
        memcpy(row.base, probe.x[fit->eat_action].idx, sizeof(int) * (size_t)row.base_n);
        p_eat = eat_p(&g_eat_model.logit, &row, g_eat_model.model);
    }
    if (g_ablation.appetite > 0 && probe.eligible[fit->eat_action]) {
        int level = g_ablation.level_low + (int)(riskfight_sample_hash(g_rollout_seed, 0, 77) % (uint32_t)(g_ablation.level_high - g_ablation.level_low + 1));
        p_eat = s->env.players[1].current_hitpoints < level ? g_ablation.appetite : 0;
    }
    /* No recorded tick sits below 30 hitpoints long enough to fit a hazard there: panic is the rate that reproduces the share of ticks the recorded fights spend at 0-29 hp (0.8 a tick at 25 or less). */
    if (g_ablation.panic > 0 && probe.eligible[fit->eat_action] && s->env.players[1].current_hitpoints <= g_ablation.panic_hp && p_eat < g_ablation.panic) p_eat = g_ablation.panic;
    p_eat = fmin(1.0, p_eat * g_eat_scale);
    int eating = threatened || (g_ablation.eats ? tape_his_anim_at(g_tape, tick, ANIM_EAT) : roll(seed, tick, 41, p_eat));
    if (g_no_eat) eating = 0;
    if (eating) {
        const float* shares = fit->serving[warning_since(&h->warning) != WARNING_NONE];
        float serving = (float)(riskfight_sample_hash(seed, (uint32_t)tick, 42) % 10000) / 10000.0f;
        int k = serving < shares[0] ? 0 : serving < shares[0] + shares[1] ? 1 : serving < shares[0] + shares[1] + shares[2] ? 2 : 3;
        int inert = (float)(riskfight_sample_hash(seed, (uint32_t)tick, 49) % 10000) / 10000.0f < g_eat_inert && inventory.kind_slot[OSRS_CONSUMABLE_SUPER_RESTORE] > 0;
        if (inert) actions[RF_DRINK] = inventory.kind_slot[OSRS_CONSUMABLE_SUPER_RESTORE];
        else actions[RF_FOOD] = inventory.kind_slot[OSRS_CONSUMABLE_ANGLERFISH];
        if (!inert && (k == 1 || k == 3)) actions[RF_COMBO] = inventory.kind_slot[OSRS_CONSUMABLE_KARAMBWAN];
        if (!inert && (k == 2 || k == 3) && self.potion_timer == 0) actions[RF_DRINK] = inventory.kind_slot[OSRS_CONSUMABLE_BREW];
        actions[RF_PRIMARY] = RF_STOP;
    }
    int maul = roll(seed, tick, 43, action_probability(fit, &probe, ACT_MAUL_SPEC));
    int swing = !eating && probe.eligible[ACT_SWING] && roll(seed, tick, 44, action_probability(fit, &probe, ACT_SWING));
    int vw = swing && !maul && roll(seed, tick, 45, action_probability(fit, &probe, ACT_VW_SPEC));
    if (!eating && probe.eligible[ACT_SWING] && !swing) actions[RF_PRIMARY] = RF_STOP;
    int armed = obs[13] >= 0.5f;
    int holding_maul = pvp_is_maul(self.weapon);
    if (maul) {
        actions[RF_WEAPON] = inventory.item_slot[ITEM_GRANITE_MAUL_ORNATE];
        actions[RF_SHIELD] = 0;
        actions[RF_SPECIAL] = 2;
        actions[RF_PRIMARY] = RF_ATTACK;
    } else if (vw) {
        actions[RF_WEAPON] = inventory.item_slot[ITEM_VOIDWAKER];
        actions[RF_SPECIAL] = !holding_maul && !armed;
    } else if (!eating) {
        actions[RF_SPECIAL] = !holding_maul && armed;
    }
    actions[RF_VENGEANCE] = roll(seed, tick, 46, action_probability(fit, &probe, ACT_VENGEANCE));
    if (g_ablation.escape && tick == tape_escape_tick(g_tape)) actions[RF_PRIMARY] = RF_TELEPORT;
    if (g_ablation.escape_model && roll(seed, tick, 48, g_escape_by_hp ? escape_rate_at(s->env.players[1].current_hitpoints) : ESCAPE_RATE)) actions[RF_PRIMARY] = RF_TELEPORT;
}
#endif
