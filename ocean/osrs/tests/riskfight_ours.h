#ifndef RISKFIGHT_OURS_H
#define RISKFIGHT_OURS_H

/* Our side of a rollout driven by the ported Gear Swapper rules. Beliefs come from the sim as the plugin would have them
 * (the health bar and animations through Hist.warning, his arsenal as seen) with PID, his spec energy and Vengeance read
 * from the truth, which the Java inference only approximates. Timing is ideal: a click is processed the next tick. */
enum { OURS_PROCESS = 1, OURS_COMPLETE = 2, OURS_MAUL_PROCESS = 2, OURS_REFRESH = 5, OURS_PLAY_GAP = 6, OURS_HORIZON = 8, SEEN_VW = 1, SEEN_MAUL = 2 };
static const double VOIDWAKER_CARRIED = 41 / 59.0;
static const double MAUL_CARRIED = 23 / 59.0;

typedef struct { int most; double accuracy; } Strike;

static Strike weapon_strike(const Player* owner, Player* target, uint8_t weapon) {
    static Player copy;
    memcpy(&copy, owner, sizeof(copy));
    copy.equipped[GEAR_SLOT_WEAPON] = weapon;
    if (item_is_two_handed(weapon)) copy.equipped[GEAR_SLOT_SHIELD] = ITEM_NONE;
    osrs_refresh_player_equipment(&copy);
    AttackStyle style = get_slot_weapon_attack_style(&copy) == ATTACK_STYLE_RANGED ? ATTACK_STYLE_RANGED : ATTACK_STYLE_MELEE;
    return (Strike){calculate_max_hit(&copy, style, 1.0f, 30), calculate_hit_chance(&copy, target, style, 1.0f)};
}

static GsStyle gs_style_of_weapon(uint8_t weapon) { return get_item_attack_style(weapon) == 2 ? GS_RANGED : GS_MELEE; }
static int weapon_interval(const Player* owner, uint8_t weapon) {
    static Player copy;
    memcpy(&copy, owner, sizeof(copy));
    copy.equipped[GEAR_SLOT_WEAPON] = weapon;
    osrs_refresh_player_equipment(&copy);
    return get_slot_gear_bonuses(&copy)->attack_speed;
}

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

static GsWarning to_gs_warning(const Warning* w) {
    GsWarning out = {warning_since(w), {0}, w->unseen_count};
    assert(w->unseen_count <= 16);
    memcpy(out.unseen, w->unseen, (size_t)w->unseen_count * sizeof(int));
    return out;
}

static GsTimer timer_of(int seen, int tick, int earliest) { return (GsTimer){seen, seen ? imax(tick + 1, earliest) : 0, earliest}; }

typedef struct {
    GsEatSituation eat;
    GsFood foods[3];
    int food_slot[3], food_count;
    GsMeal meals[8];
    int meal_count;
} OursBeliefs;

static void ours_beliefs(Sim* sim, Ours* ours, int tick, OursBeliefs* b) {
    RiskfightState* s = &sim->state;
    Player* us = &s->env.players[0];
    Player* him = &s->env.players[1];
    const Hist* h = &sim->hist;
    memset(b, 0, sizeof(*b));
    GsEatSituation* e = &b->eat;
    e->tick = tick;
    e->hitpoints = us->current_hitpoints;
    e->pid = s->env.pid_holder == 0 ? GS_PID_LOCAL : GS_PID_OPPONENT;
    e->distance = chebyshev_distance(us->x, us->y, him->x, him->y);
    const RiskfightVisibleOpponent* his_view = &s->visible[0];
    const RiskfightVisibleOpponent* our_view = &s->visible[1];
    int his_last = sim->tick_base + (his_view->last_attack_tick - RISKFIGHT_LOAD_TICK);
    e->opponent_timer = timer_of(his_view->last_attack_tick >= 0, tick, his_last + his_view->last_attack_speed);
    uint8_t his_weapon = him->equipped[GEAR_SLOT_WEAPON];
    ours->seen_weapons |= (his_weapon == ITEM_VOIDWAKER || h->since_vw_spec < 99 ? SEEN_VW : 0) | (is_maul_item(his_weapon) || h->since_maul_spec < 99 ? SEEN_MAUL : 0);
    Strike normal = weapon_strike(him, us, his_weapon);
    e->attacks[e->attack_count++] = (GsAttack){gs_style_of_weapon(his_weapon), 0, normal.most, 0, weapon_interval(him, his_weapon), normal.accuracy};
    if (ours->seen_weapons & SEEN_VW) {
        Strike vw = weapon_strike(him, us, ITEM_VOIDWAKER);
        e->attacks[e->attack_count++] = (GsAttack){GS_MELEE, osrs_voidwaker_min_hit(vw.most), osrs_voidwaker_max_hit(vw.most), 50, 4, 1.0};
    } else {
        Strike vw = weapon_strike(him, us, ITEM_VOIDWAKER);
        e->hidden[e->hidden_count++] = (GsHidden){0, {GS_MELEE, osrs_voidwaker_min_hit(vw.most), osrs_voidwaker_max_hit(vw.most), 50, 4, 1.0}, VOIDWAKER_CARRIED};
    }
    Strike maul = weapon_strike(him, us, ITEM_GRANITE_MAUL_ORNATE);
    GsAttack maul_attack = {GS_MELEE, 0, maul.most, 50, 1, maul.accuracy};
    if (ours->seen_weapons & SEEN_MAUL) { e->has_maul = 1; e->maul = maul_attack; }
    else e->hidden[e->hidden_count++] = (GsHidden){1, maul_attack, MAUL_CARRIED};
    e->energy_high = him->special_energy;
    e->vengeance = him->veng_active ? GS_VENGEANCE_UP : GS_VENGEANCE_DOWN;
    e->recoil_seen = him->equipped[GEAR_SLOT_RING] == ITEM_RING_OF_RECOIL;
    uint8_t our_weapon = us->equipped[GEAR_SLOT_WEAPON];
    int our_last = sim->tick_base + (our_view->last_attack_tick - RISKFIGHT_LOAD_TICK);
    int stored = our_view->last_attack_tick >= 0 ? our_last + our_view->last_attack_speed + ours->eat_added : tick + 1;
    e->local_timer = (GsTimer){1, tick + 1 + imax(0, us->attack_timer), stored};
    e->local_style = GS_MELEE;
    Strike ours_normal = weapon_strike(us, him, our_weapon);
    int armed = us->spec_armed && osrs_spec_cost(our_weapon) > 0 && us->special_energy >= osrs_spec_cost(our_weapon);
    e->local_normal = ours_normal.most;
    e->local_most = armed && our_weapon == ITEM_VOIDWAKER ? osrs_voidwaker_max_hit(ours_normal.most) : ours_normal.most;
    e->local_interval = weapon_interval(us, our_weapon);
    e->local_accuracy = ours_normal.accuracy;
    e->local_opening_accuracy = armed ? 1.0 : ours_normal.accuracy;
    e->opponent_hp = (GsHealthRange){h->warning.low, h->warning.high, GS_ASSUMED_MAX_HP};
    e->offence = osrs_interaction_active(&us->interaction) ? GS_ENGAGED : GS_IDLE;
    e->tolerance = 0.01;
    e->eat_cost = 0.01;
    e->stakes = 1;
    e->warning = to_gs_warning(&h->warning);
    e->floor = GS_NONE;
    static const OsrsConsumableKind kinds[3] = {OSRS_CONSUMABLE_ANGLERFISH, OSRS_CONSUMABLE_BREW, OSRS_CONSUMABLE_KARAMBWAN};
    static const GsConsumable gs_kinds[3] = {GS_HARD_FOOD, GS_POTION, GS_FAST_FOOD};
    static const int heals[3] = {22, 16, 18};
    for (int k = 0; k < 3; k++) {
        int count;
        int slot = slot_with_kind(us, kinds[k], &count);
        if (slot < 0) continue;
        int timer = k == 0 ? us->food_timer : k == 1 ? us->potion_timer : us->karambwan_timer;
        int total = count;
        if (k == 1) {
            total = 0;
            for (int i = 0; i < OSRS_INVENTORY_SIZE; i++)
                if (osrs_inventory_cell_metadata(&us->inventory_cells[i])->consumable_kind == OSRS_CONSUMABLE_BREW)
                    total += osrs_inventory_cell_metadata(&us->inventory_cells[i])->dose_count;
        }
        b->foods[b->food_count] = (GsFood){gs_kinds[k], heals[k], timer > 0 ? tick + timer + 1 : GS_NONE, total, k == 1};
        b->food_slot[b->food_count++] = slot;
    }
    for (int mask = 1; mask < 1 << b->food_count; mask++) {
        GsMeal* meal = &b->meals[b->meal_count];
        memset(meal, 0, sizeof(*meal));
        int seen[GS_CONSUMABLES] = {0};
        int fine = 1;
        for (int i = 0; i < b->food_count; i++) {
            if (!(mask >> i & 1)) continue;
            fine &= !seen[b->foods[i].kind];
            seen[b->foods[i].kind] = 1;
            meal->foods[meal->n] = i;
            meal->offsets[meal->n] = OURS_PROCESS;
            meal->following[meal->n++] = OURS_PROCESS;
        }
        if (!fine) continue;
        meal->earliest_process = OURS_PROCESS;
        meal->resume = OURS_PROCESS;
        meal->complete = OURS_COMPLETE;
        b->meal_count++;
    }
}

static int ours_landing(int process, GsPid pid) { return gs_lands(process, GS_MELEE, 1, gs_local_lead(pid)); }

typedef struct { GsSpecSituation s; GsSpecPlan plans[4], after[4]; int plan_count, after_count; GsRebound rebound; int energy; GsReflect reflect; } OursSpecBeliefs;

static void ours_spec_beliefs(Sim* sim, Ours* ours, int tick, const GsEatSituation* e, Arena* arena, OursSpecBeliefs* b) {
    RiskfightState* s = &sim->state;
    Player* us = &s->env.players[0];
    Player* him = &s->env.players[1];
    const Hist* h = &sim->hist;
    memset(b, 0, sizeof(*b));
    GsSpecSituation* sp = &b->s;
    sp->hitpoints = e->opponent_hp;
    sp->hard_food_ready = h->since_eat < 99 ? tick - h->since_eat + 3 : GS_NONE;
    sp->vengeance = e->vengeance;
    sp->pid = e->pid;
    sp->recoil_charges = us->equipped[GEAR_SLOT_RING] == ITEM_RING_OF_RECOIL ? RECOIL_MAX_CHARGES - us->item_effect_state.recoil_damage_used : 0;
    sp->tick = tick;
    sp->warning = e->warning;
    int veng_ready = !us->veng_active && us->veng_cooldown <= 1 && us->current_magic >= OSRS_VENGEANCE_MAGIC_LEVEL && s->inventory_use[0].vengeance_sacks > 0;
    b->rebound = us->veng_active ? (GsRebound){GS_REBOUND_UP, GS_NONE} : veng_ready ? (GsRebound){GS_REBOUND_CASTABLE, tick + OURS_PROCESS} : (GsRebound){GS_REBOUND_SPENT, GS_NONE};
    sp->rebound = b->rebound;
    int due = e->opponent_timer.seen ? e->opponent_timer.next : tick + 1;
    if (e->attack_count > 0 && osrs_interaction_active(&him->interaction)) {
        const GsAttack* a = &e->attacks[0];
        sp->has_incoming = 1;
        sp->incoming = (GsIncoming){gs_lands(due, a->style, e->distance, gs_opponent_lead(e->pid)), a->least, a->most, a->accuracy, due};
    }
    b->energy = us->special_energy;
    b->reflect = e->attacks[0].most >= 40 ? GS_REFLECT_WORTH : GS_REFLECT_WEAK;
    (void)ours;
}

static int ours_maul_slot(const Player* p) {
    int slot = slot_with_item(p, is_maul_item);
    if (slot < 0) return -1;
    int used = 0;
    for (int i = 0; i < OSRS_INVENTORY_SIZE; i++) used += !osrs_inventory_cell_is_empty(&p->inventory_cells[i]);
    return p->equipped[GEAR_SLOT_SHIELD] == ITEM_NONE || used < OSRS_INVENTORY_SIZE ? slot : -1;
}

static int ours_build_plans(Arena* arena, Sim* sim, const GsEatSituation* e, int tick, int extra, GsSpecPlan* plans) {
    Player* us = &sim->state.env.players[0];
    Player* him = &sim->state.env.players[1];
    int energy = us->special_energy, count = 0;
    int swing = imax(tick + 1, e->local_timer.next);
    Strike vw = weapon_strike(us, him, ITEM_VOIDWAKER);
    int vw_min = osrs_voidwaker_min_hit(vw.most), vw_max = osrs_voidwaker_max_hit(vw.most);
    if (us->equipped[GEAR_SLOT_WEAPON] == ITEM_VOIDWAKER && energy >= 50) {
        GsSpecHit* hit = arena_alloc(arena, sizeof(GsSpecHit));
        *hit = gs_spec_hit_of(arena, ours_landing(swing, e->pid), vw_min, vw_max, 1.0);
        plans[count++] = (GsSpecPlan){GS_CHOICE_VOIDWAKER, hit, 1};
    }
    if (ours_maul_slot(us) >= 0 && energy >= 50) {
        Strike maul = weapon_strike(us, him, ITEM_GRANITE_MAUL_ORNATE);
        int process = tick + OURS_MAUL_PROCESS + extra;
        GsSpecHit* hits = arena_alloc(arena, 2 * sizeof(GsSpecHit));
        hits[0] = gs_spec_hit_of(arena, ours_landing(process, e->pid), 0, maul.most, maul.accuracy);
        plans[count++] = (GsSpecPlan){GS_CHOICE_MAUL, hits, 1};
        if (energy >= 100) {
            GsSpecHit* both = arena_alloc(arena, 2 * sizeof(GsSpecHit));
            both[0] = hits[0];
            both[1] = gs_spec_hit_of(arena, ours_landing(imax(process + 1, swing), e->pid), vw_min, vw_max, 1.0);
            plans[count++] = (GsSpecPlan){GS_CHOICE_MAUL_VOIDWAKER, both, 2};
        }
    }
    return count;
}

static void command(HumanInput* hi, HumanCommand c) { human_input_queue_command(hi, c); }
static void click_slot(HumanInput* hi, int slot) { command(hi, (HumanCommand){.kind = HUMAN_COMMAND_INVENTORY_PRIMARY_CLICK, .inventory_slot = slot}); }
static void attack(HumanInput* hi) { command(hi, (HumanCommand){.kind = HUMAN_COMMAND_ATTACK_NPC, .npc_slot = 1}); }

/* Queues our commands for the step after tick `tick`. previous holds what the last step showed. */
static void ours_act(Sim* sim, Ours* ours, HumanInput* hi, int step, const StepEvents* previous) {
    RiskfightState* s = &sim->state;
    Player* us = &s->env.players[0];
    int tick = sim->tick_base + step;
    human_input_clear_pending(hi);
    ours->ticks++;
    const RiskfightVisibleOpponent* our_view = &s->visible[1];
    if (our_view->last_attack_tick != ours->last_swing_seen) {
        ours->last_swing_seen = our_view->last_attack_tick;
        ours->eat_added = 0;
    }
    if (ours->stage) {
        int slot = slot_with_item(us, is_voidwaker);
        if (slot >= 0) click_slot(hi, slot);
        if (ours->stage_choice == GS_CHOICE_MAUL_VOIDWAKER) command(hi, (HumanCommand){.kind = HUMAN_COMMAND_SPEC_TOGGLE});
        attack(hi);
        ours->stage = 0;
        return;
    }
    int interesting = step == 0 || previous->ate || previous->swung || previous->vw || previous->maul || previous->hit_on_him || us->hit_landed_this_tick ||
        s->env.players[1].cast_veng_this_tick || tick - ours->last_decision >= OURS_REFRESH;
    if (!osrs_interaction_active(&us->interaction) && !interesting) attack(hi);
    if (!interesting) return;
    ours->last_decision = tick;
    ours->decisions++;
    ours->arena.used = 0;
    static OursBeliefs beliefs;
    ours_beliefs(sim, ours, tick, &beliefs);
    GsVerdict verdict = gs_auto_eat_decide(&beliefs.eat, beliefs.foods, beliefs.food_count, beliefs.meals, beliefs.meal_count, &ours->arena);
    int cast = 0;
    if (verdict.decision != GS_WAIT && verdict.food_count > 0) {
        int kinds[GS_CONSUMABLES] = {0};
        for (int i = 0; i < verdict.food_count; i++) {
            click_slot(hi, beliefs.food_slot[verdict.foods[i]]);
            kinds[beliefs.foods[verdict.foods[i]].kind] = 1;
        }
        ours->eat_added += consumption_delay(consumption_of(kinds[GS_POTION], kinds[GS_FAST_FOOD], kinds[GS_HARD_FOOD]));
        attack(hi);
        return;
    }
    static OursSpecBeliefs spec;
    ours_spec_beliefs(sim, ours, tick, &beliefs.eat, &ours->arena, &spec);
    GsSpecPlan plans[4], after[4];
    int plan_count = ours_build_plans(&ours->arena, sim, &beliefs.eat, tick, 0, plans);
    int after_count = spec.rebound.kind == GS_REBOUND_CASTABLE ? ours_build_plans(&ours->arena, sim, &beliefs.eat, tick, 1, after) : 0;
    GsSpecDecision decision = gs_auto_spec_decide(&ours->arena, &spec.s, plans, plan_count, after, after_count, spec.energy);
    if (decision.choice != GS_CHOICE_HOLD && (decision.choice != ours->last_played_choice || step - ours->last_played_step > OURS_PLAY_GAP)) {
        assert(ours->played_count < 64);
        ours->played[ours->played_count].step = step;
        ours->played[ours->played_count++].chance = decision.chance;
    }
    if (decision.choice != GS_CHOICE_HOLD) {
        ours->last_played_choice = decision.choice;
        ours->last_played_step = step;
    }
    GsPid first = beliefs.eat.pid;
    int attack_underway = 0;
    GsCastReason reason = gs_vengeance_reason(decision.cast, &spec.rebound, attack_underway, 0, first, spec.reflect, spec.reflect);
    if (reason != GS_REASON_NONE && spec.rebound.kind == GS_REBOUND_CASTABLE) { command(hi, (HumanCommand){.kind = HUMAN_COMMAND_VENGEANCE}); cast = 1; }
    (void)cast;
    switch (decision.choice) {
        case GS_CHOICE_VOIDWAKER:
            if (!us->spec_armed) command(hi, (HumanCommand){.kind = HUMAN_COMMAND_SPEC_TOGGLE});
            break;
        case GS_CHOICE_MAUL:
        case GS_CHOICE_MAUL_VOIDWAKER:
            click_slot(hi, ours_maul_slot(us));
            command(hi, (HumanCommand){.kind = HUMAN_COMMAND_SPEC_TOGGLE});
            ours->stage = 1;
            ours->stage_choice = decision.choice;
            break;
        case GS_CHOICE_HOLD:
            break;
    }
    attack(hi);
}
#endif
