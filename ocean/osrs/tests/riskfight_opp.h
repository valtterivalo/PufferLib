#ifndef RISKFIGHT_OPP_H
#define RISKFIGHT_OPP_H
#include "riskfight_seed.h"
#include "riskfight_warning.h"
#include <math.h>

/* Fitted opponent policy: six small logistic hazards on aggregate buckets, no per-opponent terms.
 * Every probability is a function of Hist plus the sim state, so one-step probing and rollouts share it. */
typedef enum { ACT_EAT_BIG, ACT_EAT_WARNING, ACT_SWING, ACT_VW_SPEC, ACT_MAUL_SPEC, ACT_VENGEANCE, ACT_COUNT } Action;
enum { MAX_DIM = 8, BIG_HIT_DAMAGE = 20, LAG_NONE = 99, ENERGY_FOR_SPEC = 50, RECENT_EAT = 3, FOLLOW_UP = 5 };
static const int ACTION_DIM[ACT_COUNT] = {8, 8, 5, 4, 2, 1};
static const char* ACTION_NAME[ACT_COUNT] = {"eat (anchor: big hit)", "eat (anchor: warning)", "swing when ready", "VW spec given swing",
    "maul spec", "Vengeance cast"};

typedef struct {
    int big_hit_tick, since_eat, since_vw_spec, since_maul_spec, ready_age, last_swing_tick;
    Warning warning;
} Hist;

typedef struct { int n, idx[4]; } Sparse;
typedef struct { Sparse x; float y, weight; } Sample;
typedef struct { double w[MAX_DIM]; long events, opportunities; } Logit;
typedef struct { Logit logit[ACT_COUNT]; Action eat_action; } OppFit;

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
static int our_hp_band(int hp) { return hp < 45 ? 0 : hp <= 85 ? 1 : 2; }

static Sparse eat_features(int band, int lag) {
    Sparse x = {1, {0}};
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

static Sparse vw_features(int our_hp, int follow) {
    Sparse x = {1, {0}};
    int b = our_hp_band(our_hp);
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

/** What the model would see before the tick `tick` of his decisions, from the sim state s and the history h. */
static Probe probe_state(const RiskfightState* s, const Hist* h, int tick, int kit_vw, int kit_maul) {
    const Player* him = &s->env.players[1];
    const Player* us = &s->env.players[0];
    Probe p = {{0}};
    int ratio = osrs_health_bar_ratio(him->current_hitpoints, 99, 30);
    int lockfree = him->food_timer == 0;
    p.eligible[ACT_EAT_BIG] = p.eligible[ACT_EAT_WARNING] = lockfree;
    p.x[ACT_EAT_BIG] = eat_features(hp_band4(ratio), lag_of(tick, h->big_hit_tick));
    p.x[ACT_EAT_WARNING] = eat_features(hp_band4(ratio), lag_of(tick, warning_since(&h->warning)));
    p.eligible[ACT_SWING] = him->attack_timer <= 0;
    p.x[ACT_SWING] = swing_features(h);
    p.eligible[ACT_VW_SPEC] = kit_vw && him->special_energy >= ENERGY_FOR_SPEC;
    int vw_follow = h->since_vw_spec >= 1 && h->since_vw_spec <= FOLLOW_UP;
    p.x[ACT_VW_SPEC] = vw_features(us->current_hitpoints, vw_follow);
    p.eligible[ACT_MAUL_SPEC] = kit_maul && him->special_energy >= ENERGY_FOR_SPEC;
    p.x[ACT_MAUL_SPEC] = follow_features(h->since_vw_spec <= 1);
    p.eligible[ACT_VENGEANCE] = !him->veng_active && him->veng_cooldown <= 0;
    p.x[ACT_VENGEANCE] = (Sparse){1, {0}};
    return p;
}

typedef struct { int normal, spec; } Reach;

static int worn_slot_item(const TapeTick* t, int worn) { return t->worn[worn] >= 0 ? raw_id_to_item(t->worn[worn]) : ITEM_NONE; }

/** Max hit of our next swing and of the spec of the weapon, as the sim computes them for the tape's gear and levels. */
static Reach our_reach(const TapeTick* t) {
    static Player p;
    init_player(&p);
    encounter_init_maxed_player_combat_stats(&p, 99);
    p.current_attack = t->attack; p.current_strength = t->strength; p.current_defence = t->defence;
    p.offensive_prayer = t->piety ? OFFENSIVE_PRAYER_PIETY : OFFENSIVE_PRAYER_NONE;
    for (int i = 0; i < NUM_GEAR_SLOTS; i++) p.equipped[i] = ITEM_NONE;
    for (int i = 0; i < TAPE_WORN; i++) if (WORN_TO_GEAR[i] >= 0) p.equipped[WORN_TO_GEAR[i]] = (uint8_t)worn_slot_item(t, i);
    osrs_refresh_player_equipment(&p);
    int normal = calculate_max_hit(&p, ATTACK_STYLE_MELEE, 1.0f, 30);
    int weapon = p.equipped[GEAR_SLOT_WEAPON];
    return (Reach){normal, weapon == ITEM_VOIDWAKER ? osrs_voidwaker_max_hit(normal) : normal};
}

static int attack_hitsplat(int type) { return type == 16 || type == 17 || type == 43; }

/** Hist after each tape tick lo..hi, from his animations, the hitsplats on him and the bar as shown. */
static void hist_series(const Tape* tape, Hist* out) {
    Hist h = {.big_hit_tick = WARNING_NONE, .since_eat = 99, .since_vw_spec = 99, .since_maul_spec = 99, .warning = warning_start(), .last_swing_tick = -100};
    int ready_age = 0;
    for (int x = tape->lo; x <= tape->hi; x++) {
        const TapeTick* t = tape_tick(tape, x);
        int ate = 0, vw = 0, maul = 0;
        for (int i = 0; i < tape->anim_count; i++) {
            const TapeAnim* a = &tape->anims[i];
            if (a->tick != x || a->side != SIDE_HIM) continue;
            ate |= a->anim == ANIM_EAT;
            vw |= a->anim == ANIM_VW_SPEC;
            maul |= a->anim == ANIM_MAUL_SPEC;
        }
        h.since_eat = ate ? 0 : h.since_eat + 1;
        h.since_vw_spec = vw ? 0 : h.since_vw_spec + 1;
        h.since_maul_spec = maul ? 0 : h.since_maul_spec + 1;
        if (ate) warning_ate(&h.warning, x);
        for (int i = 0; i < tape->hit_count; i++) {
            const TapeHit* hit = &tape->hits[i];
            if (hit->tick != x || hit->side != SIDE_HIM) continue;
            warning_hitsplat(&h.warning);
            if (attack_hitsplat(hit->type)) warning_struck(&h.warning, x, hit->amount);
            if (hit->amount >= BIG_HIT_DAMAGE) h.big_hit_tick = x;
        }
        int weapon = worn_slot_item(t, 3);
        int has_spec = weapon == ITEM_VOIDWAKER || pvp_is_maul(weapon);
        Reach reach = our_reach(t);
        int energy_ok = t->spec_us >= ENERGY_FOR_SPEC;
        warning_observe(&h.warning, x, t->bar_now_ratio, t->bar_now_scale, weapon, has_spec, has_spec && energy_ok ? reach.spec : reach.normal);
        ready_age = replay_attack_timer(tape, SIDE_HIM, x) <= 0 ? ready_age + 1 : 0;
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

/** Moves the history one sim step on. tick is the tape tick the step covered. */
static void hist_step(Hist* h, RiskfightState* s, int tick, const StepEvents* e) {
    const Player* him = &s->env.players[1];
    h->since_eat = e->ate ? 0 : h->since_eat + 1;
    h->since_vw_spec = e->vw ? 0 : h->since_vw_spec + 1;
    h->since_maul_spec = e->maul ? 0 : h->since_maul_spec + 1;
    if (e->swung) h->last_swing_tick = tick;
    h->ready_age = him->attack_timer <= 0 ? h->ready_age + 1 : 0;
    if (e->max_hit_on_him >= BIG_HIT_DAMAGE) h->big_hit_tick = tick;
    if (e->ate_visible) warning_ate(&h->warning, tick);
    if (e->hit_on_him) warning_hitsplat(&h->warning);
    if (e->damage_on_him) warning_struck(&h->warning, tick, e->damage_on_him);
    Player* us = &s->env.players[0];
    int weapon = us->equipped[GEAR_SLOT_WEAPON];
    int has_spec = weapon == ITEM_VOIDWAKER || pvp_is_maul(weapon);
    int normal = calculate_max_hit(us, ATTACK_STYLE_MELEE, 1.0f, 30);
    int spec = weapon == ITEM_VOIDWAKER ? osrs_voidwaker_max_hit(normal) : normal;
    int ratio = s->visible[0].health_bar;
    warning_observe(&h->warning, tick, ratio, OSRS_PLAYER_HEALTH_BAR_SCALE, weapon, has_spec,
        has_spec && us->special_energy >= ENERGY_FOR_SPEC ? spec : normal);
}

static const float SERVING_SHARES[4] = {0.15f, 0.34f, 0.34f, 0.17f};

static double action_probability(const OppFit* fit, const Probe* p, Action a) {
    return p->eligible[a] ? logit_p(&fit->logit[a], &p->x[a]) : 0;
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
    int eating = roll(seed, tick, 41, action_probability(fit, &probe, fit->eat_action));
    if (eating) {
        float serving = (float)(riskfight_sample_hash(seed, (uint32_t)tick, 42) % 10000) / 10000.0f;
        int k = serving < SERVING_SHARES[0] ? 0 : serving < SERVING_SHARES[0] + SERVING_SHARES[1] ? 1 :
            serving < SERVING_SHARES[0] + SERVING_SHARES[1] + SERVING_SHARES[2] ? 2 : 3;
        actions[RF_FOOD] = inventory.kind_slot[OSRS_CONSUMABLE_ANGLERFISH];
        if (k == 1 || k == 3) actions[RF_COMBO] = inventory.kind_slot[OSRS_CONSUMABLE_KARAMBWAN];
        if ((k == 2 || k == 3) && self.potion_timer == 0) actions[RF_DRINK] = inventory.kind_slot[OSRS_CONSUMABLE_BREW];
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
}
#endif
