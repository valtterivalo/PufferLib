#ifndef RISKFIGHT_OPP_H
#define RISKFIGHT_OPP_H
#include "riskfight_seed.h"
#include "riskfight_warning.h"
#include "riskfight_oppmodel.h"
#include "../gearswapper/gs_wire.h"
#include <math.h>

/* Fitted opponent policy (riskfight_oppmodel.h): small logistic hazards on what he can read of the fight, no per-opponent terms.
 * Every probability is a function of Hist plus the sim state, so one-step probing and rollouts share it. */
enum { BIG_HIT_DAMAGE = 20, LAG_NONE = OPP_LAG_NONE, ENERGY_FOR_SPEC = 50, RECENT_EAT = 3, FOLLOW_UP = 5, LOCK_TICKS = 2 };

typedef struct {
    int big_hit_tick, since_eat, since_vw_spec, since_maul_spec, ready_age, last_swing_tick;
    Warning warning;
    /* Spec weapons he has used in this fight (bit per WeaponKind), the kind of the weapon of his last swing, how many of his last swings came on the first tick he could,
     * and his spec energy as the sim last read it. */
    int seen_mask, wield_class, on_time_streak, last_energy;
    /* The first damage on him that no meal of his has answered since, and the damage taken since (it takes his reaction a few ticks to arrive). */
    int unanswered_since, unanswered_damage;
} Hist;

static int lag_of(int step_tick, int anchor) { return anchor == WARNING_NONE ? LAG_NONE : step_tick - anchor; }

/** The weapon kind of a sim item, WK_NONE for the others. */
static int weapon_kind_of_item(int item) {
    switch (item) {
        case ITEM_VOIDWAKER: return WK_VW;
        case ITEM_AGS: return WK_AGS;
        case ITEM_DRAGON_CLAWS: return WK_CLAWS;
        case ITEM_GRANITE_MAUL: case ITEM_GRANITE_MAUL_ORNATE: return WK_MAUL;
        default: return WK_NONE;
    }
}

static const uint8_t WK_ITEM[WK_COUNT] = {ITEM_VOIDWAKER, ITEM_AGS, ITEM_DRAGON_CLAWS, ITEM_GRANITE_MAUL_ORNATE};

static int raw_of_item_id(uint8_t item);


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

/** One tick of what the other side showed of this fighter. `wield_class` is the kind of the weapon of a swing, `launch_class` the kind of the spec (or axe swing) launched this tick. */
typedef struct {
    int ate_visible, ate, animated, hits, damage;
    int bar_ratio, bar_scale;
    int weapon, has_spec, reach;
    int swing, wield_class, launch_class;
    int ready;
} HistInput;

static Hist hist_start(void) {
    return (Hist){.big_hit_tick = WARNING_NONE, .since_eat = 99, .since_vw_spec = 99, .since_maul_spec = 99, .warning = warning_start(), .last_swing_tick = -100,
        .wield_class = WK_NONE, .last_energy = 100, .unanswered_since = WARNING_NONE};
}

/** Moves the history of a fighter one tick on. `ate` is his own knowledge of his meals, `ate_visible` the 829 the viewer saw. */
static void hist_update(Hist* h, int tick, const HistInput* in) {
    h->since_eat = in->ate ? 0 : h->since_eat + 1;
    h->since_vw_spec = in->launch_class == WK_VW ? 0 : h->since_vw_spec + 1;
    h->since_maul_spec = in->launch_class == WK_MAUL ? 0 : h->since_maul_spec + 1;
    if (in->swing) {
        h->last_swing_tick = tick;
        h->on_time_streak = h->ready_age <= 1 ? h->on_time_streak + 1 : 0;
        h->wield_class = in->wield_class;
    }
    if (in->launch_class != WK_NONE) h->seen_mask |= 1 << in->launch_class;
    h->ready_age = in->ready ? h->ready_age + 1 : 0;
    if (in->damage >= BIG_HIT_DAMAGE) h->big_hit_tick = tick;
    /* A meal heals before the damage landing on its tick (inputs resolve before incoming hits), so the damage of that tick starts a new unanswered run. */
    if (in->ate) { h->unanswered_since = WARNING_NONE; h->unanswered_damage = 0; }
    if (in->damage > 0) {
        if (h->unanswered_since == WARNING_NONE) h->unanswered_since = tick;
        h->unanswered_damage += in->damage;
    }
    if (in->ate_visible) warning_ate(&h->warning, tick);
    if (in->animated) warning_animated(&h->warning, tick);
    if (in->hits) warning_hitsplat(&h->warning);
    if (in->damage) warning_struck(&h->warning, tick, in->damage);
    warning_observe(&h->warning, tick, in->bar_ratio, in->bar_scale, in->weapon, in->has_spec, in->reach);
}

static int attack_hitsplat(int type) { return type == 16 || type == 17 || type == 43; }

/** Ticks since his bar last moved on which a meal would have been hidden behind another animation. */
static int unseen_ticks(const Tape* tape, int tick) { return ((const Hist*)tape->hist_series)[tick - tape->lo].warning.unseen_count; }

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

static int bar_hp(int ratio) { return ratio <= 0 ? 0 : (ratio * 99 + 15) / 30; }

/** What the sim shows him of himself and of us before tick `tick`: the inputs of every hazard. */
static OppIn opp_inputs(const RiskfightState* s, const Hist* h, int tick) {
    const Player* him = &s->env.players[1];
    const Player* us = &s->env.players[0];
    int warning = warning_since(&h->warning);
    OppIn in = {
        .his_hp = him->current_hitpoints,
        .his_bar_ratio = osrs_health_bar_ratio(him->current_hitpoints, 99, 30),
        .our_bar_ratio = s->visible[1].health_bar,
        .our_hp = bar_hp(s->visible[1].health_bar),
        .lag = lag_of(tick, warning), .warned = warning != WARNING_NONE,
        .since_eat = h->since_eat, .since_vw_spec = h->since_vw_spec,
        .ready = !him->has_attack_timer || him->attack_timer <= 0, .ready_age = h->ready_age, .on_time_streak = h->on_time_streak,
        .since_swing = tick - h->last_swing_tick,
        .our_tau = us->has_attack_timer ? opp_imax(0, us->attack_timer) : -1,
        .our_spec = h->warning.spec_wielded_since != WARNING_NONE, .our_veng = us->veng_active,
        .seen_mask = h->seen_mask, .wield_class = h->wield_class,
        .veng_ok = !him->veng_active && him->veng_cooldown <= 0,
        .dmg_lag = lag_of(tick, h->unanswered_since), .dmg_size = h->unanswered_damage,
    };
    for (int k = 0; k < WK_COUNT; k++) {
        in.energy_ok[k] = him->special_energy >= WK_COST[k];
        in.has[k] = k == WK_MAUL ? has_maul(s, 1) : has_item(s, 1, WK_ITEM[k]);
    }
    return in;
}

typedef struct { int ate_visible, ate, swung, vw, maul, damage_on_him, max_hit_on_him, hit_on_him; } StepEvents;
static int raw_of_item_id(uint8_t item) { return item == ITEM_NONE ? -1 : ITEM_DATABASE[item].item_id; }

/** Moves the history one sim step on. tick is the tape tick the step covered. */
static void hist_step(Hist* h, RiskfightState* s, int tick, const StepEvents* e) {
    const Player* him = &s->env.players[1];
    const Player* us = &s->env.players[0];
    HistInput in = {.ate_visible = e->ate_visible, .ate = e->ate, .animated = e->swung || e->hit_on_him, .hits = e->hit_on_him, .damage = e->damage_on_him,
        .bar_ratio = s->visible[0].health_bar, .bar_scale = OSRS_PLAYER_HEALTH_BAR_SCALE, .swing = e->swung,
        .wield_class = weapon_kind_of_item(him->equipped[GEAR_SLOT_WEAPON]), .launch_class = WK_NONE, .ready = him->attack_timer <= 0};
    /* A spec shows as the drop of his energy (a regeneration tick adds ten, a spec takes at least 25). */
    int spec = (e->swung || e->maul) && him->special_energy < h->last_energy - 14;
    if (e->maul) in.launch_class = WK_MAUL;
    else if (e->vw) in.launch_class = WK_VW;
    else if (spec && in.wield_class != WK_NONE && WK_COST[in.wield_class] > 0) in.launch_class = in.wield_class;
    h->last_energy = him->special_energy;
    int kit[KIT_SLOTS];
    kit_of_equipped(us->equipped, kit);
    in.weapon = raw_of_item_id(us->equipped[GEAR_SLOT_WEAPON]);
    in.reach = assumed_reach(kit, in.weapon, &in.has_spec);
    hist_update(h, tick, &in);
}

static const float SERVING_SHARES[4] = {0.15f, 0.34f, 0.34f, 0.17f};

/** The eat hazard of the loaded fit (a mixture over eater types, one drawn per rollout). */
static EatMixture g_eat_model;

/** His chance to leave by teleport each tick: 14 teleports in 5094 ticks of the 38 recorded fights, flat across his hitpoints (0.4 percent a tick at every band). */
static const double ESCAPE_RATE = 14.0 / 5094;
static int g_escape_by_hp = 1;
static double g_eat_inert = 0, g_eat_scale = 1;
/** The same by his hitpoints, 44 or less, 45-60, 61-87, 88 and more: 4 of 690, 5 of 729, 5 of 2485 and 0 of 1069 recorded ticks by the health bar. */
static double escape_rate_at(int hitpoints) { return hitpoints <= 44 ? 4.0 / 690 : hitpoints <= 60 ? 5.0 / 729 : hitpoints <= 87 ? 5.0 / 2485 : 0; }

static void assert_fitted(const OppFit* fit, const char* who) { opp_check(fit, &g_eat_model, who); }

static int roll(uint32_t seed, int tick, int salt, double p) {
    return (int)(riskfight_sample_hash(seed, (uint32_t)tick, (uint32_t)salt) % 1000000u) < (int)(p * 1000000);
}

/** The chance he swings weapon kind `k`'s spec on a swing, 0 where he cannot (no weapon in his kit, no energy). */
static double spec_probability(const OppFit* fit, const OppIn* in, int k) {
    if (!in->has[k] || !in->energy_ok[k]) return 0;
    int seen = in->seen_mask >> k & 1;
    double x[OPP_DIM];
    spec_x(in, k, seen, fit->spec_groups[k][seen], x);
    return glm_p(&fit->spec[k][seen], x);
}

/** The fitted policy as sim actions: eat with a serving, else swing or idle, the weapon of the swing and its spec, maul spec, Vengeance. */
static void opp_fit_actions(const OppFit* fit, const RiskfightState* s, const Hist* h, int tick, uint32_t seed, int* actions) {
    float obs[RF_OBS_SIZE];
    riskfight_write_script_observation(s, 1, obs);
    static const RiskfightProfile quiet = {{0}, 0, 0, {0}, 0, 0, {{0}}};
    riskfight_script(obs, &quiet, s->script_seed[1], s->consume_ticks[1], actions);
    RiskfightObservedSelf self = riskfight_observed_self(obs);
    RiskfightObservedInventory inventory = riskfight_observed_inventory(obs);
    OppIn in = opp_inputs(s, h, tick);
    int can_eat = h->since_eat >= LOCK_TICKS;
    int threatened = 0;
    if (g_ablation.threat > 0 && can_eat) {
        const Player* us = &s->env.players[0];
        const Player* him = &s->env.players[1];
        int kit[KIT_SLOTS], has_spec;
        kit_of_equipped(us->equipped, kit);
        int reach = assumed_reach(kit, raw_of_item_id(us->equipped[GEAR_SLOT_WEAPON]), &has_spec);
        threatened = us->attack_timer <= g_ablation.threat_timer && him->current_hitpoints <= reach && roll(seed, tick, 47, g_ablation.threat);
    }
    double p_eat = 0;
    if (can_eat) {
        double x[OPP_DIM];
        eat_x(&in, g_eat_model.groups, eat_type_of(&g_eat_model, (riskfight_sample_hash(g_rollout_seed, 0, 78) % 1000000u) / 1e6), x);
        p_eat = glm_p(&g_eat_model.glm, x);
    }
    if (g_ablation.appetite > 0 && can_eat) {
        int level = g_ablation.level_low + (int)(riskfight_sample_hash(g_rollout_seed, 0, 77) % (uint32_t)(g_ablation.level_high - g_ablation.level_low + 1));
        p_eat = s->env.players[1].current_hitpoints < level ? g_ablation.appetite : 0;
    }
    /* No recorded tick sits below 30 hitpoints long enough to fit a hazard there: panic is the rate that reproduces the share of ticks the recorded fights spend at 0-29 hp (0.8 a tick at 25 or less). */
    if (g_ablation.panic > 0 && can_eat && s->env.players[1].current_hitpoints <= g_ablation.panic_hp && p_eat < g_ablation.panic) p_eat = g_ablation.panic;
    p_eat = fmin(1.0, p_eat * g_eat_scale);
    int eating = threatened || (g_ablation.eats ? tape_his_anim_at(g_tape, tick, ANIM_EAT) : roll(seed, tick, 41, p_eat));
    if (g_no_eat) eating = 0;
    if (eating) {
        const float* shares = fit->serving[in.warned];
        float serving = (float)(riskfight_sample_hash(seed, (uint32_t)tick, 42) % 10000) / 10000.0f;
        int k = serving < shares[0] ? 0 : serving < shares[0] + shares[1] ? 1 : serving < shares[0] + shares[1] + shares[2] ? 2 : 3;
        int inert = (float)(riskfight_sample_hash(seed, (uint32_t)tick, 49) % 10000) / 10000.0f < g_eat_inert && inventory.kind_slot[OSRS_CONSUMABLE_SUPER_RESTORE] > 0;
        if (inert) actions[RF_DRINK] = inventory.kind_slot[OSRS_CONSUMABLE_SUPER_RESTORE];
        else actions[RF_FOOD] = inventory.kind_slot[OSRS_CONSUMABLE_ANGLERFISH];
        if (!inert && (k == 1 || k == 3)) actions[RF_COMBO] = inventory.kind_slot[OSRS_CONSUMABLE_KARAMBWAN];
        if (!inert && (k == 2 || k == 3) && self.potion_timer == 0) actions[RF_DRINK] = inventory.kind_slot[OSRS_CONSUMABLE_BREW];
        actions[RF_PRIMARY] = RF_STOP;
    }
    double x[OPP_DIM];
    swing_x(&in, fit->swing_groups, x);
    int swing = !eating && in.ready && roll(seed, tick, 44, glm_p(&fit->swing, x));
    int maul = roll(seed, tick, 43, spec_probability(fit, &in, WK_MAUL));
    /* One draw picks the weapon of the swing among the arsenal: each kind's chance is a share of it. */
    int kind = WK_NONE;
    if (swing && !maul) {
        double u = (riskfight_sample_hash(seed, (uint32_t)tick, 45) % 1000000u) / 1e6, acc = 0;
        for (int k = 0; k < WK_MAUL && kind == WK_NONE; k++) {
            acc += spec_probability(fit, &in, k);
            if (u < acc) kind = k;
        }
    }
    if (!eating && in.ready && !swing) actions[RF_PRIMARY] = RF_STOP;
    int armed = obs[13] >= 0.5f;
    int holding_maul = pvp_is_maul(self.weapon);
    if (maul) {
        actions[RF_WEAPON] = inventory.item_slot[ITEM_GRANITE_MAUL_ORNATE];
        actions[RF_SHIELD] = 0;
        actions[RF_SPECIAL] = 2;
        actions[RF_PRIMARY] = RF_ATTACK;
    } else if (kind != WK_NONE) {
        actions[RF_WEAPON] = inventory.item_slot[WK_ITEM[kind]];
        if (item_is_two_handed(WK_ITEM[kind])) actions[RF_SHIELD] = 0;
        actions[RF_SPECIAL] = !holding_maul && (WK_COST[kind] > 0 ? !armed : armed);
    } else if (!eating) {
        actions[RF_SPECIAL] = !holding_maul && armed;
    }
    veng_x(&in, fit->veng_groups, x);
    actions[RF_VENGEANCE] = in.veng_ok && roll(seed, tick, 46, glm_p(&fit->veng, x));
    if (g_ablation.escape && tick == tape_escape_tick(g_tape)) actions[RF_PRIMARY] = RF_TELEPORT;
    if (g_ablation.escape_model && roll(seed, tick, 48, g_escape_by_hp ? escape_rate_at(s->env.players[1].current_hitpoints) : ESCAPE_RATE)) actions[RF_PRIMARY] = RF_TELEPORT;
}
#endif
