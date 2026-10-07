#ifndef RISKFIGHT_OURS_H
#define RISKFIGHT_OURS_H

/* Our side of a rollout driven by the ported Gear Swapper rules on what the client shows: every belief about him is the plugin's inference
 * from the observed events, ours is read from the sim as the client reads it. Clicks are timed as the hand times them: the first press lands
 * 130 to 460 ms after the decision, the next 80 to 170 ms after the one before, and a press after the 490 ms server cutoff is processed a tick later. */
enum { OURS_REFRESH = 5, OURS_PLAY_GAP = 6, OURS_HORIZON = 8, CUTOFF_MS = 490, TICK_MS = 600, FIRST_PRESS_MS = 130, FIRST_PRESS_SPAN = 330,
    GAP_MS = 80, GAP_SPAN = 90, MAUL_GROUP_WIELD = 3, MAUL_GROUP_BAR = 4, MAUL_GROUP_ABORT_PER_8 = 2, ABORT_RESTORE = 8, HANDS_FOOD_SLOTS = 3 };
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
    double ms = ours_uniform(ours, FIRST_PRESS_MS, FIRST_PRESS_SPAN);
    for (int i = 0; i < count; i++) {
        if (i) ms += ours_uniform(ours, GAP_MS, GAP_SPAN);
        int rel = ms < CUTOFF_MS ? 1 : 2 + (int)((ms - CUTOFF_MS) / TICK_MS);
        ours_push(ours, tick + rel, clicks[i]);
    }
}

static void ours_pop_due(Ours* ours, HumanInput* hi, int due) {
    int kept = 0;
    for (int i = 0; i < ours->pending_count; i++) {
        if (ours->pending[i].due == due) command(hi, ours->pending[i].command);
        else ours->pending[kept++] = ours->pending[i];
    }
    ours->pending_count = kept;
}

static GsSpecHit gs_certain_hit(Arena* arena, int lands, int damage) {
    double* d = arena_alloc(arena, (size_t)(damage + 1) * sizeof(double));
    memset(d, 0, (size_t)(damage + 1) * sizeof(double));
    d[damage] = 1;
    return (GsSpecHit){lands, d, damage + 1, GS_ORIGIN_OURS};
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
        meal->resume = 1;
        meal->complete = 2;
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
    int vw_slot = slot_with_item(us, is_voidwaker);
    if (vw_slot < 0) return;
    if (ours->ideal_timing) {
        ours_push(ours, tick + 2, click_command(vw_slot));
        if (choice == GS_CHOICE_MAUL_VOIDWAKER) ours_push(ours, tick + 2, spec_command());
        ours_push(ours, tick + 2, attack_command());
        return;
    }
    if (ours_random(ours) % 8 < MAUL_GROUP_ABORT_PER_8) {
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
    if (truth & TRUTH_HP) view->opponent_hitpoints = (GsHealthRange){imax(1, him->current_hitpoints), imax(1, him->current_hitpoints), imax(GS_ASSUMED_MAX_HP, him->current_hitpoints)};
    if (truth & TRUTH_TIMER) {
        const RiskfightVisibleOpponent* seen = &s->visible[0];
        int next = sim->tick_base + (seen->last_attack_tick - RISKFIGHT_LOAD_TICK) + seen->last_attack_speed;
        view->opponent.timer = seen->last_attack_tick >= 0 ? (GsTimerState){1, next, next} : (GsTimerState){0, 0, 0};
    }
}

/* Arbiter.value: his loss times the kill chance less ours times the death chance. */
static double ours_value(double kill, double death, GsStakes stakes) { return kill * (double)stakes.him - death * (double)stakes.us; }

/* The spec decision of AutoSpecialAttack on this tick, HOLD where the Arbiter would drop a spec whose own death outweighs its kill. */
static GsSpecDecision ours_spec(Sim* sim, Ours* ours, GsInfer* inf, int tick, const GsRebound* rebound, GsStakes stakes, int* planned) {
    GsSpecDecision decision = {GS_CHOICE_HOLD, GS_CAST_KEEP, GS_CHOICE_HOLD, 0, 0, 0, 0, 0};
    *planned = gs_spec_plannable(inf);
    if (!*planned) return decision;
    GsSpecSituation spec;
    GsUnderway underway;
    gs_spec_inputs(inf, tick, &spec, &underway);
    GsSpecHit certain;
    if (underway.certain) { certain = gs_certain_hit(&ours->arena, underway.lands, underway.damage); spec.underway = &certain; spec.underway_count = 1; }
    spec.rebound = *rebound;
    GsOwn own = ours_own(sim);
    GsSpecPlan plans[4], after[4];
    int plan_count = gs_spec_plans(&ours->arena, inf, tick, 0, &own, plans);
    int after_count = rebound->kind == GS_REBOUND_CASTABLE ? gs_spec_plans(&ours->arena, inf, tick, 1, &own, after) : 0;
    decision = gs_auto_spec_decide(&ours->arena, &spec, plans, plan_count, after, after_count, gs_spec_energy(&inf->c));
    if (decision.choice != GS_CHOICE_HOLD && ours_value(decision.chance, decision.death, stakes) <= 0) decision.choice = GS_CHOICE_HOLD;
    return decision;
}

/* Plans what the plugin would on this tick and queues the clicks. */
static void ours_decide(Sim* sim, Ours* ours, int step, int tick) {
    RiskfightState* s = &sim->state;
    Player* us = &s->env.players[0];
    static GsInfer view;
    view = sim->obs.inf;
    ours_truth(sim, ours->truth, tick, &view);
    GsInfer* inf = &view;
    ours->last_decision = tick;
    ours->decisions++;
    ours->arena.used = 0;
    GsPid pid = gs_pid_at(inf, tick);
    GsEatSituation e;
    gs_eat_situation(inf, tick, osrs_interaction_active(&us->interaction) ? GS_ENGAGED : GS_IDLE, 0.01, ours->equal_stakes ? 1.0 : gs_stakes_ratio(sim->obs.stakes), &e);
    OursFoods foods;
    ours_foods(sim, &foods);
    GsVerdict verdict = gs_auto_eat_decide(&e, foods.foods, foods.food_count, foods.meals, foods.meal_count, &ours->arena);
    GsStakes stakes = ours->equal_stakes ? (GsStakes){1, 1} : sim->obs.stakes;
    GsRebound rebound = ours_rebound(sim, ours, tick, 0);
    int planned;
    GsSpecDecision decision = verdict.food_count > 0 || ours->pending_count == 0 ? ours_spec(sim, ours, inf, tick, &rebound, stakes, &planned) : (GsSpecDecision){GS_CHOICE_HOLD, GS_CAST_KEEP, GS_CHOICE_HOLD, 0, 0, 0, 0, 0};
    int meal = verdict.food_count > 0 && !(decision.choice != GS_CHOICE_HOLD && ours_value(decision.chance, verdict.risk + decision.death, stakes) + (1 - decision.chance) * decision.kept * (double)stakes.him > ours_value(0, verdict.meal_risk, stakes) + decision.held * (double)stakes.him);
    if (meal) {
        ours->pending_count = 0;
        HumanCommand clicks[8];
        int n = 0;
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
    if (ours->pending_count > 0) return;
    if (decision.choice != GS_CHOICE_HOLD && (decision.choice != ours->last_played_choice || step - ours->last_played_step > OURS_PLAY_GAP)) {
        assert(ours->played_count < 64);
        ours->played[ours->played_count].step = step;
        ours->played[ours->played_count++].chance = decision.chance;
    }
    if (decision.choice != GS_CHOICE_HOLD) {
        ours->last_played_choice = decision.choice;
        ours->last_played_step = step;
    }
    HumanCommand clicks[10];
    int n = 0;
    GsCastReason reason = planned || inf->timed >= 0 ? ours_venged_reason(inf, tick, decision.cast, &rebound, pid) : GS_REASON_NONE;
    if (reason != GS_REASON_NONE && rebound.kind == GS_REBOUND_CASTABLE) {
        clicks[n++] = vengeance_command();
        ours->vengeance_clicked = tick + 1;
    }
    int maul_slot = ours_maul_slot(us);
    switch (decision.choice) {
        case GS_CHOICE_VOIDWAKER:
            if (!us->spec_armed) clicks[n++] = spec_command();
            clicks[n++] = attack_command();
            break;
        case GS_CHOICE_MAUL:
        case GS_CHOICE_MAUL_VOIDWAKER:
            clicks[n++] = click_command(maul_slot);
            clicks[n++] = spec_command();
            if (!osrs_interaction_active(&us->interaction)) clicks[n++] = attack_command();
            break;
        case GS_CHOICE_HOLD:
            break;
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
    int rival = inf->timed >= 0 && inf->c.ratio != 0;
    if (rival && (step == 0 || sim->obs.events > 0 || tick - ours->last_decision >= OURS_REFRESH)) ours_decide(sim, ours, step, tick);
    ours_pop_due(ours, hi, tick + 1);
    if (!osrs_interaction_active(&us->interaction) && ours->pending_count == 0 && hi->commands.count == 0) command(hi, attack_command());
}
#endif
