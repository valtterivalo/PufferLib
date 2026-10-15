#ifndef RISKFIGHT_OURS_H
#define RISKFIGHT_OURS_H
#include "riskfight_motor.h"

/* Closed-loop C rule-port proxy, not a certified Java production baseline.
 * Opponent beliefs come from observed events; clicks use the measured motor
 * model of riskfight_motor.h (Fact 20). */
enum { OURS_REFRESH = 5, OURS_PLAY_GAP = 6, OURS_HORIZON = 8, TICK_MS = MOTOR_TICK_MS,
    MAUL_GROUP_WIELD = 3, MAUL_GROUP_BAR = 4, ABORT_RESTORE = 8, HANDS_FOOD_SLOTS = 3 };
enum { KIND_HARD, KIND_BREW, KIND_KARAMBWAN };

static uint32_t ours_random(Ours* ours) {
    ours->rng ^= ours->rng << 13; ours->rng ^= ours->rng >> 17; ours->rng ^= ours->rng << 5;
    return ours->rng;
}
static double ours_uniform(Ours* ours, double low, double span) { return low + span * (ours_random(ours) >> 8) / 16777216.0; }

static int slot_with_kind(const Player* p, OsrsConsumableKind kind, int* count) {
    int first = -1;
    *count = 0;
    for (int slot = 0; slot < OSRS_INVENTORY_SIZE; slot++)
        if (osrs_inventory_cell_metadata(&p->inventory_cells[slot])->consumable_kind == kind) {
            if (first < 0) first = slot;
            (*count)++;
        }
    return first;
}

static int slot_with_item(const Player* p, int (*matches)(uint8_t)) {
    for (int slot = 0; slot < OSRS_INVENTORY_SIZE; slot++) {
        uint8_t item = osrs_inventory_cell_metadata(&p->inventory_cells[slot])->item_idx;
        if (item != ITEM_NONE && matches(item)) return slot;
    }
    return -1;
}
static int is_voidwaker(uint8_t item) { return item == ITEM_VOIDWAKER; }

static void command(HumanInput* hi, HumanCommand c) { human_input_queue_command(hi, c); }
static HumanCommand click_command(int slot) { return (HumanCommand){.kind = HUMAN_COMMAND_INVENTORY_PRIMARY_CLICK, .inventory_slot = slot}; }
static HumanCommand attack_command(void) { return (HumanCommand){.kind = HUMAN_COMMAND_ATTACK_NPC, .npc_slot = 1}; }
static HumanCommand spec_command(void) { return (HumanCommand){.kind = HUMAN_COMMAND_SPEC_TOGGLE}; }
static HumanCommand vengeance_command(void) { return (HumanCommand){.kind = HUMAN_COMMAND_VENGEANCE}; }

enum { WIELD_VOIDWAKER = -2 };
static int g_cmd_trace, g_abort_per_8;
static long g_cb_total, g_cb_abort, g_cb_preempt, g_cb_ran;

static void ours_push(Ours* ours, int due, HumanCommand c) {
    assert(ours->pending_count < OURS_PENDING);
    ours->pending[ours->pending_count++] = (OursPending){due, c};
    ours->busy_until = imax(ours->busy_until, due);
}

/* A run of clicks from the decision at `tick`: each click's processing tick follows from when the hand presses it. */
static void ours_run(Ours* ours, int tick, const HumanCommand* clicks, int count) {
    if (ours->ideal_timing) {
        for (int i = 0; i < count; i++) ours_push(ours, tick + 1, clicks[i]);
        return;
    }
    double ms = 0;
    for (int i = 0; i < count; i++) {
        ms += motor_gap_ms(ours_uniform(ours, 0, 1), i);
        int rel = motor_land_tick(0, ms);
        ours_push(ours, tick + rel, clicks[i]);
    }
}

static void ours_pop_due(Ours* ours, const Player* us, HumanInput* hi, int due) {
    int kept = 0;
    for (int i = 0; i < ours->pending_count; i++) {
        if (ours->pending[i].due == due) {
            HumanCommand c = ours->pending[i].command;
            if (c.kind == HUMAN_COMMAND_INVENTORY_PRIMARY_CLICK && c.inventory_slot == WIELD_VOIDWAKER) c.inventory_slot = slot_with_item(us, is_voidwaker);
            if (getenv("GS_COMBO_TRACE") && g_cmd_trace) fprintf(stderr, "   cmd for tick %d kind %d slot %d\n", due, (int)c.kind, c.inventory_slot);
            command(hi, c);
        }
        else ours->pending[kept++] = ours->pending[i];
    }
    ours->pending_count = kept;
}

static GsSpecHit gs_certain_hit(Arena* arena, int lands, int damage) {
    double* d = arena_alloc(arena, (size_t)(damage + 1) * sizeof(double));
    memset(d, 0, (size_t)(damage + 1) * sizeof(double));
    d[damage] = 1;
    return (GsSpecHit){lands, d, damage + 1, GS_ORIGIN_OURS, imax(1, damage), damage > 0 ? 1 : 0};
}

static int ours_maul_slot(const Player* p) {
    int slot = slot_with_item(p, is_maul_item);
    if (slot < 0) return -1;
    int used = 0;
    for (int i = 0; i < OSRS_INVENTORY_SIZE; i++) used += !osrs_inventory_cell_is_empty(&p->inventory_cells[i]);
    return p->equipped[GEAR_SLOT_SHIELD] == ITEM_NONE || used < OSRS_INVENTORY_SIZE ? slot : -1;
}

typedef struct {
    GsFood foods[3];
    int food_slot[3], food_count;
    GsMeal meals[8];
    int meal_count;
} OursFoods;

/* AutoEater.meals: every set of foods of distinct channels, depth first in offer order, the first food processed next tick and the rest the tick after. */
static void ours_meals(OursFoods* f, int next, int* selected, int depth) {
    if (depth) {
        GsMeal* meal = &f->meals[f->meal_count++];
        memset(meal, 0, sizeof(*meal));
        meal->n = depth;
        for (int j = 0; j < depth; j++) {
            meal->foods[j] = selected[j];
            meal->offsets[j] = j == 0 ? 1 : 2;
            meal->following[j] = j == 0 ? 1 : 2;
        }
        meal->earliest_process = 1;
        meal->complete = 2;
        meal->resume = meal->complete;
    }
    for (int i = next; i < f->food_count; i++) {
        selected[depth] = i;
        ours_meals(f, i + 1, selected, depth + 1);
    }
}

static void ours_foods(const Sim* sim, OursFoods* f) {
    const Player* us = &sim->state.env.players[0];
    const GsInfer* inf = &sim->obs.inf;
    memset(f, 0, sizeof(*f));
    static const OsrsConsumableKind kinds[3] = {OSRS_CONSUMABLE_ANGLERFISH, OSRS_CONSUMABLE_BREW, OSRS_CONSUMABLE_KARAMBWAN};
    static const GsConsumable channels[3] = {GS_HARD_FOOD, GS_POTION, GS_FAST_FOOD};
    static const int heals[3] = {22, 16, 18};
    for (int k = 0; k < 3; k++) {
        int count;
        int slot = slot_with_kind(us, kinds[k], &count);
        if (slot < 0) continue;
        int total = count;
        if (k == KIND_BREW) {
            total = 0;
            for (int i = 0; i < OSRS_INVENTORY_SIZE; i++) {
                const OsrsItemContentMetadata* m = osrs_inventory_cell_metadata(&us->inventory_cells[i]);
                if (m->consumable_kind == OSRS_CONSUMABLE_BREW) total += m->dose_count;
            }
        }
        f->foods[f->food_count] = (GsFood){channels[k], heals[k], inf->ready[channels[k]], total, k == KIND_BREW};
        f->food_slot[f->food_count++] = slot;
    }
    int selected[3];
    ours_meals(f, 0, selected, 0);
}

static GsRebound ours_rebound(const Sim* sim, const Ours* ours, int tick, int servings) {
    const Player* us = &sim->state.env.players[0];
    if (us->veng_active) return (GsRebound){GS_REBOUND_UP, GS_NONE};
    int ready = us->veng_cooldown <= 1 && us->current_magic >= OSRS_VENGEANCE_MAGIC_LEVEL && sim->state.inventory_use[0].vengeance_sacks > 0 &&
        tick > ours->vengeance_clicked + 1;
    return ready ? (GsRebound){GS_REBOUND_CASTABLE, tick + 1 + servings} : (GsRebound){GS_REBOUND_SPENT, GS_NONE};
}

static GsOwn ours_own(const Sim* sim) {
    const Player* us = &sim->state.env.players[0];
    GsOwn own;
    gs_own_of(&sim->obs.inf.c, &own.maul_id, &own.inventory_count, &own.shield_worn);
    (void)us;
    return own;
}

static int ours_venged_reason(const GsInfer* inf, int tick, GsCast cast, const GsRebound* rebound, GsPid pid) {
    GsIncoming attack;
    int has_attack = inf->attack_tick == tick && gs_swing_underway(inf, tick, gs_pid_at(inf, tick), &attack);
    GsReflect weapon = gs_weapon_reflect((GsInfer*)inf, tick);
    GsEnergyCover cover = gs_energy_range(&inf->opponent.spec, tick).low >= gs_opponent_spec_cost(inf) ? GS_ENERGY_COVERS : GS_ENERGY_SHORT;
    return gs_vengeance_reason(cast, rebound, has_attack, has_attack ? attack.lands : 0, pid, weapon, inf->swing_reflect, cover);
}

static void ours_group_two(Ours* ours, const Player* us, int tick, GsChoiceKind choice) {
    int vw_slot = WIELD_VOIDWAKER;
    (void)us;
    g_cb_total++;
    if (ours->ideal_timing) {
        ours_push(ours, tick + 2, click_command(vw_slot));
        if (choice == GS_CHOICE_MAUL_VOIDWAKER) ours_push(ours, tick + 3, spec_command());
        ours_push(ours, tick + 3, attack_command());
        return;
    }
    if (ours_random(ours) % 8 < (uint32_t)g_abort_per_8) {
        g_cb_abort++;
        ours_push(ours, tick + ABORT_RESTORE, click_command(vw_slot));
        ours_push(ours, tick + ABORT_RESTORE, attack_command());
        return;
    }
    ours_push(ours, tick + MAUL_GROUP_WIELD, click_command(vw_slot));
    if (choice == GS_CHOICE_MAUL_VOIDWAKER) ours_push(ours, tick + MAUL_GROUP_BAR, spec_command());
    ours_push(ours, tick + MAUL_GROUP_BAR, attack_command());
}

/* The plugin's state with the beliefs an ablation reads from the sim put in place of its inference. */
static void ours_truth(const Sim* sim, int truth, int tick, GsInfer* view) {
    const RiskfightState* s = &sim->state;
    const Player* him = &s->env.players[1];
    if (truth & TRUTH_PID) { view->pid = gs_pid_unknown(); view->pid.owner = s->env.pid_holder == 0 ? GS_PID_LOCAL : GS_PID_OPPONENT; view->pid.observed_tick = tick; }
    if (truth & TRUTH_ENERGY) view->opponent.spec = (GsSpecEnergy){tick, {him->special_energy, him->special_energy}, {him->special_energy, him->special_energy}, 0};
    if (truth & TRUTH_VENGEANCE) view->opponent.vengeance = him->veng_active ? gs_vengeance_cast(tick) : (GsVengeanceState){GS_VPHASE_DISARMED, 0, 0, 0, 0};
    if (truth & TRUTH_HP) {
        view->opponent_hitpoints = (GsHealthRange){imax(1, him->current_hitpoints), imax(1, him->current_hitpoints), imax(GS_ASSUMED_MAX_HP, him->current_hitpoints)};
        view->health = NULL; /* This ablation deliberately supplies its exact range instead of inferred target histories. */
    }
    if (truth & TRUTH_TIMER) {
        int next = tick + 1 + imax(0, him->attack_timer);
        view->opponent.timer = (GsTimerState){1, next, next};
    }
}

/* Arbiter.value: his loss times the kill chance less ours times the death chance. */
static double ours_value(double kill, double death, GsStakes stakes) { return kill * (double)stakes.him - death * (double)stakes.us; }

/* The spec decision of AutoSpecialAttack on this tick, HOLD where the Arbiter would drop a spec whose own death outweighs its kill. */
static GsSpecDecision ours_spec(Sim* sim, Ours* ours, GsInfer* inf, int tick, const GsRebound* rebound, GsStakes stakes, int* planned) {
    GsSpecDecision decision = {GS_CHOICE_HOLD, GS_CAST_KEEP, GS_CHOICE_HOLD, 0, 0, 0, 0, 0};
    *planned = gs_spec_plannable(inf);
    if (!*planned) {
        ours->burst_pending = 0;
        return decision;
    }
    GsSpecSituation spec;
    GsUnderway underway;
    gs_spec_inputs(inf, tick, &spec, &underway);
    GsSpecHit certain;
    if (underway.certain) { certain = gs_certain_hit(&ours->arena, underway.lands, underway.damage); spec.underway = &certain; spec.underway_count = 1; }
    spec.rebound = *rebound;
    GsOwn own = ours_own(sim);
    GsSpecPlan plans[4], after[4];
    int plan_count = gs_spec_plans(&ours->arena, inf, tick, 0, &own, plans);
    int after_count = !ours->burst_pending && rebound->kind == GS_REBOUND_CASTABLE ? gs_spec_plans(&ours->arena, inf, tick, 1, &own, after) : 0;
    g_gs_expected_hp = -1;
    g_gs_truth_hp = sim->state.env.players[1].current_hitpoints;
    int energy = gs_spec_energy(&inf->c);
    if (ours->burst_pending) {
        const Player* us = &sim->state.env.players[0];
        if ((!ours->burst_spent && energy != 2 * GS_TICKET && energy != GS_TICKET)
                || (ours->burst_spent && (energy < GS_TICKET || energy < ours->burst_energy))
                || us->equipped[GEAR_SLOT_WEAPON] != ITEM_VOIDWAKER || inf->timed != ours->burst_target
                || inf->c.ratio == 0 || us->current_hitpoints <= 0) {
            ours->burst_pending = 0;
            return decision;
        }
        if (!ours->burst_spent && energy == GS_TICKET) {
            ours->burst_spent = 1;
            ours->burst_spend_tick = tick;
        }
        ours->burst_energy = energy;
        if (ours->burst_first == INT_MIN && inf->local_spec_tick >= ours->burst_started)
            ours->burst_first = inf->local_spec_tick;
        int speed = gs_base_speed(GS_ITEM_VOIDWAKER);
        if ((ours->burst_first == INT_MIN && ours->burst_spent && tick > ours->burst_spend_tick + 1)
                || (ours->burst_first != INT_MIN && (tick >= ours->burst_first + speed
                    || inf->local_spec_tick > ours->burst_first))) {
            ours->burst_pending = 0;
            return decision;
        }
        if (!ours->burst_spent || ours->burst_first == INT_MIN || tick != ours->burst_first + speed - 1) return decision;
        const GsSpecPlan* single = NULL;
        for (int i = 0; i < plan_count; i++)
            if (plans[i].choice == GS_CHOICE_VOIDWAKER && plans[i].attack_tick == 0) { single = &plans[i]; break; }
        if (!single) {
            ours->burst_pending = 0;
            return decision;
        }
        int through = 0;
#ifndef GS_HP_BELIEF
        through = gs_spec_horizon(&spec, single->hits, single->hit_count);
#endif
        decision = gs_spec_continue_burst(&ours->arena, &spec, single, ours->burst_bar, gs_stakes_ratio(stakes), through);
    } else {
        decision = gs_auto_spec_decide_priced(&ours->arena, &spec, plans, plan_count, after, after_count, energy, gs_stakes_ratio(stakes));
    }
#ifdef GS_HP_BELIEF
    if (g_gs_expected_hp >= 0) {
        int h = 0, a = 0, q = 0;
        for (int i = 0; i < inf->watched_count; i++) { h += inf->watched[i] == GS_SEEN_HIDDEN; a += inf->watched[i] == GS_SEEN_ATE; q += inf->watched[i] == GS_SEEN_QUIET; }
        int width = inf->opponent_bar.high - inf->opponent_bar.low;
        int keys[4] = {h == 0 ? 0 : h == 1 ? 1 : h <= 3 ? 2 : 3, a == 0 ? 0 : a == 1 ? 1 : 2, q < 3 ? 0 : q < 8 ? 1 : q < 20 ? 2 : 3, width < 3 ? 0 : width < 10 ? 1 : 2};
        double truth = sim->state.env.players[1].current_hitpoints;
        { int d = (int)(g_gs_pit * 10); if (d > 9) d = 9; g_pit_all[d]++; g_pit_n++; if (g_gs_expected_hp <= 60) { g_pit_low[d]++; g_pit_low_n++; } }
        { int eb = g_gs_expected_hp <= 30 ? 0 : g_gs_expected_hp <= 45 ? 1 : g_gs_expected_hp <= 60 ? 2 : g_gs_expected_hp <= 75 ? 3 : g_gs_expected_hp <= 90 ? 4 : 5; g_eb_n[eb]++; g_eb_exp[eb] += g_gs_expected_hp; g_eb_true[eb] += truth; g_eb_low[eb] += truth <= g_gs_expected_hp - 15; g_eb_high[eb] += truth >= g_gs_expected_hp + 15; }
        for (int k = 0; k < 4; k++) { g_diag_n[k][keys[k]]++; g_diag_exp[k][keys[k]] += g_gs_expected_hp; g_diag_true[k][keys[k]] += truth; }
    }
#endif
    if (decision.choice != GS_CHOICE_HOLD && decision.attack_tick != 0) {
        fprintf(stderr, "C proxy has no timed spec actuator for attack tick %d\n", decision.attack_tick);
        abort();
    }
    g_plan_mean = 0; g_plan_land = 0; g_plan_last = 0;
    for (int i = 0; i < plan_count; i++)
        if (plans[i].choice == decision.choice && plans[i].attack_tick == decision.attack_tick) {
            for (int h = 0; h < plans[i].hit_count; h++) {
                for (int r = 0; r < plans[i].hits[h].damages; r++) g_plan_mean += r * plans[i].hits[h].damage[r];
                g_plan_last = plans[i].hits[h].lands - tick;
            }
            g_plan_land = plans[i].hits[0].lands - tick;
        }
    if (decision.choice != GS_CHOICE_HOLD && ours_value(decision.chance, decision.death, stakes) <= 0) decision.choice = GS_CHOICE_HOLD;
    if (ours->burst_pending == 2 && decision.choice == GS_CHOICE_HOLD) ours->burst_pending = 0;
    return decision;
}

/* Plans what the plugin would on this tick and queues the clicks. */
static int la_override(Sim* sim, Ours* ours, int step, int tick);
static void la_script_step(LaScript* script, RiskfightState* s, HumanInput* hi,
    int now, int* cursor, int* resume, int hit, int damage);

static void ours_decide(Sim* sim, Ours* ours, int step, int tick) {
    /* The rules proxy only: the deployed lookahead policy never calls it (la_override decides in ours_act). */
    RiskfightState* s = &sim->state;
    Player* us = &s->env.players[0];
    static GsInfer view;
    /* Borrowed immutable health owner; this decision mutates only the scalar view. */
    view = sim->obs.inf;
    ours_truth(sim, ours->truth, tick, &view);
    GsInfer* inf = &view;
    ours->last_decision = tick;
    ours->decisions++;
    ours->arena.used = 0;
    GsPid pid = gs_pid_at(inf, tick);
    GsEatSituation e;
    gs_eat_situation(inf, tick, osrs_interaction_active(&us->interaction) ? GS_ENGAGED : GS_IDLE, g_eat_risk, ours->equal_stakes ? 1.0 : gs_stakes_ratio(sim->obs.stakes), &e);
    OursFoods foods;
    ours_foods(sim, &foods);
    GsVerdict verdict = gs_auto_eat_decide(&e, foods.foods, foods.food_count, foods.meals, foods.meal_count, &ours->arena);
    GsStakes stakes = ours->equal_stakes ? (GsStakes){1, 1} : sim->obs.stakes;
    GsRebound rebound = ours_rebound(sim, ours, tick, 0);
    int planned;
    int had_burst = ours->burst_pending;
    GsSpecDecision decision = !ours->native_active && (ours->burst_pending || verdict.food_count > 0 || ours->pending_count == 0) ? ours_spec(sim, ours, inf, tick, &rebound, stakes, &planned) : (GsSpecDecision){GS_CHOICE_HOLD, GS_CAST_KEEP, GS_CHOICE_HOLD, 0, 0, 0, 0, 0, 0};
    int meal = verdict.food_count > 0 && !(decision.choice != GS_CHOICE_HOLD && ours_value(decision.chance, verdict.risk + decision.death, stakes) + (1 - decision.chance) * decision.kept * (double)stakes.him > ours_value(0, verdict.meal_risk, stakes) + decision.held * (double)stakes.him);
    if (meal) {
        for (int i = 0; i < ours->pending_count; i++) if (ours->pending[i].command.inventory_slot == WIELD_VOIDWAKER) { g_cb_preempt++; if (ours->played_count) ours->played[ours->played_count - 1].preempted = 1; break; }
        ours->pending_count = 0;
        ours->burst_pending = 0;
        HumanCommand clicks[8];
        int n = 0;
        if (had_burst || ours->native_active) clicks[n++] = (HumanCommand){.kind = HUMAN_COMMAND_SPEC_TOGGLE, .spec_mode = HUMAN_SPEC_DISARM};
        ours->native_active = 0;
        ours->native_cursor = ours->native_program.n;
        for (int i = 0; i < verdict.food_count; i++) clicks[n++] = click_command(foods.food_slot[verdict.foods[i]]);
        GsRebound rebound = ours_rebound(sim, ours, tick, verdict.food_count);
        int drain = 0;
        for (int i = 0; i < verdict.food_count; i++) drain |= foods.foods[verdict.foods[i]].drained;
        if (!drain && rebound.kind == GS_REBOUND_CASTABLE && ours_venged_reason(inf, tick, GS_CAST_KEEP, &rebound, pid) != GS_REASON_NONE) {
            clicks[n++] = vengeance_command();
            ours->vengeance_clicked = tick + 1;
        }
        clicks[n++] = attack_command();
        ours_run(ours, tick, clicks, n);
        return;
    }
    if (ours->native_active) return;
    if (had_burst && !ours->burst_pending) ours->pending_count = 0;
    if (ours->pending_count > 0) return;
    if (had_burst) {
        if (ours->burst_pending && decision.aim == GS_CHOICE_HOLD) return;
        ours->burst_pending = 0;
        if (decision.choice == GS_CHOICE_HOLD) {
            HumanCommand disarm = {.kind = HUMAN_COMMAND_SPEC_TOGGLE, .spec_mode = HUMAN_SPEC_DISARM};
            ours_run(ours, tick, &disarm, 1);
            return;
        }
        ours->burst_pending = 2;
    }
    if (getenv("GS_COMBO_TRACE") && decision.choice == GS_CHOICE_MAUL_VOIDWAKER) {
        static int shown;
        if (shown++ < 30) fprintf(stderr, "maul slot %d shield %d | combo decision step %d: inferred energy %d actual %d weapon vw %d maul %d armed %d\n", ours_maul_slot(us), us->equipped[GEAR_SLOT_SHIELD], step, gs_spec_energy(&inf->c), us->special_energy, us->equipped[GEAR_SLOT_WEAPON] == ITEM_VOIDWAKER, is_maul_item(us->equipped[GEAR_SLOT_WEAPON]), us->spec_armed);
    }
    if (!had_burst && decision.choice != GS_CHOICE_HOLD && (decision.choice != ours->last_played_choice || step - ours->last_played_step > OURS_PLAY_GAP)) {
        assert(ours->played_count < 64);
        ours->played[ours->played_count].step = step;
        ours->played[ours->played_count].his_hitpoints = s->env.players[1].current_hitpoints;
        ours->played[ours->played_count].expected = g_gs_expected_hp;
        ours->played[ours->played_count].pit = g_gs_pit;
        ours->played[ours->played_count].plan_mean = g_plan_mean;
        ours->played[ours->played_count].preempted = 0;
        ours->played[ours->played_count].bar_mid = (inf->opponent_hitpoints.low + inf->opponent_hitpoints.high) / 2.0;
        ours->played[ours->played_count].plan_land = g_plan_land;
        ours->played[ours->played_count].plan_last = g_plan_last;
        ours->played[ours->played_count].choice = (int)decision.choice;
        ours->played[ours->played_count++].chance = decision.chance;
    }
    if (decision.choice != GS_CHOICE_HOLD) {
        ours->last_played_choice = decision.choice;
        ours->last_played_step = step;
    }
    HumanCommand clicks[10];
    int n = 0;
    GsCastReason reason = !had_burst && (planned || inf->timed >= 0) ? ours_venged_reason(inf, tick, decision.cast, &rebound, pid) : GS_REASON_NONE;
    if (reason != GS_REASON_NONE && rebound.kind == GS_REBOUND_CASTABLE) {
        clicks[n++] = vengeance_command();
        ours->vengeance_clicked = tick + 1;
    }
    int maul_slot = ours_maul_slot(us);
    switch (decision.choice) {
        case GS_CHOICE_VOIDWAKER_BURST:
            ours->burst_pending = 1;
            if (!gs_next_swing(inf->local_timer, tick, &ours->burst_started)) abort();
            ours->burst_first = INT_MIN;
            ours->burst_spent = 0;
            ours->burst_energy = gs_spec_energy(&inf->c);
            ours->burst_target = inf->timed;
            ours->burst_bar = decision.burst_bar;
            clicks[n++] = (HumanCommand){.kind = HUMAN_COMMAND_SPEC_TOGGLE, .spec_mode = HUMAN_SPEC_ENABLE};
            clicks[n++] = attack_command();
            break;
        case GS_CHOICE_VOIDWAKER:
            clicks[n++] = (HumanCommand){.kind = HUMAN_COMMAND_SPEC_TOGGLE, .spec_mode = HUMAN_SPEC_ENABLE};
            clicks[n++] = attack_command();
            break;
        case GS_CHOICE_MAUL:
        case GS_CHOICE_MAUL_VOIDWAKER:
            clicks[n++] = click_command(maul_slot);
            clicks[n++] = spec_command();
            clicks[n++] = attack_command();
            break;
        case GS_CHOICE_HOLD:
            break;
        case GS_CHOICE_COUNT:
            fprintf(stderr, "Invalid spec choice sentinel\n");
            abort();
    }
    if (n) ours_run(ours, tick, clicks, n);
    if (decision.choice == GS_CHOICE_MAUL || decision.choice == GS_CHOICE_MAUL_VOIDWAKER) ours_group_two(ours, us, tick, decision.choice);
}

/* Queues our commands for the step after tick `tick`: the clicks due then, and a decision when the client showed something new. */
static void ours_act(Sim* sim, Ours* ours, HumanInput* hi, int step) {
    RiskfightState* s = &sim->state;
    Player* us = &s->env.players[0];
    int tick = sim->tick_base + step;
    human_input_clear_pending(hi);
    ours->ticks++;
    const GsInfer* inf = &sim->obs.inf;
    if (s->env.episode_over || us->current_hitpoints <= 0 ||
        s->env.players[1].current_hitpoints <= 0) {
        ours->native_active = ours->burst_pending = ours->pending_count = 0;
        ours->native_cursor = ours->native_program.n;
        return;
    }
    int rival = inf->timed >= 0 && inf->c.ratio != 0;
    if (ours->lookahead) {
        /* Deployed policy: la_decide every tick around the committed remainder; no click without a macro, no rules proxy, no re-acquisition. */
        if (rival && inf->c.forecast_available) la_override(sim, ours, step, tick);
        if (ours->native_active) {
            la_script_step(&ours->native_program, s, hi, tick + 1, &ours->native_cursor, &ours->native_resume, 0, 0);
            ours->native_active = ours->native_cursor < ours->native_program.n;
        }
        return;
    }
    if (!rival && (ours->burst_pending || ours->native_active)) {
        ours->native_active = 0;
        ours->native_cursor = ours->native_program.n;
        ours->burst_pending = 0;
        ours->pending_count = 0;
        command(hi, (HumanCommand){.kind = HUMAN_COMMAND_SPEC_TOGGLE, .spec_mode = HUMAN_SPEC_DISARM});
    }
    if (rival && inf->c.forecast_available && (ours->burst_pending || step == 0 || sim->obs.events > 0 || tick - ours->last_decision >= OURS_REFRESH)) ours_decide(sim, ours, step, tick);
    if (ours->native_active) {
        la_script_step(&ours->native_program, s, hi, tick + 1,
            &ours->native_cursor, &ours->native_resume, 0, 0);
        ours->native_active = ours->native_cursor < ours->native_program.n;
        return; /* STOP owns engagement; no automatic re-acquisition while paused. */
    }
    ours_pop_due(ours, us, hi, tick + 1);
    if (rival && !osrs_interaction_active(&us->interaction) && ours->pending_count == 0 && hi->commands.count == 0) {
        HumanCommand attack = attack_command();
        ours_run(ours, tick, &attack, 1);
        ours_pop_due(ours, us, hi, tick + 1);
    }
}
#endif
