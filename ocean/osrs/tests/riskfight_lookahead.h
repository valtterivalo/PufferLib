#ifndef RISKFIGHT_LOOKAHEAD_H
#define RISKFIGHT_LOOKAHEAD_H

/* Our side decides by simulating the next ticks: every macro plan is rolled out on worlds drawn from the plugin's belief about him, with common
 * random numbers across plans, and scored as kill chance times his stakes less death chance times ours, plus the spec tickets left. */
enum { LA_SCRIPT = 16, LA_CACHE = 4096, LA_MARGIN = 10 };
static double la_seconds(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec + t.tv_nsec * 1e-9;
}
typedef enum { LA_RULES, LA_NONE, LA_SPEC_NOW, LA_SPEC_NEXT, LA_BURST, LA_EAT_NOW, LA_VENG_NOW, LA_EAT_TRIPLE, LA_EAT_STAGGER, LA_MACROS } LaMacro;
static const char* const LA_NAME[LA_MACROS] = {"rules", "none", "spec now", "spec next", "burst", "eat now", "vengeance now", "eat triple", "eat staggered"};

typedef struct { int n, due[LA_SCRIPT]; HumanCommand command[LA_SCRIPT]; } LaScript;
typedef struct { int kill, death, energy; double dealt, taken; } LaOutcome;
typedef struct { double kill, death, tickets, value; int worlds; } LaScore;

static int g_la_worlds = 64, g_la_horizon = 15, g_la_stage = 16, g_la_cache = 1;
static double g_la_prune = 0.03, g_la_belief_hp_shift = 0, g_la_eat_scale_belief = 1;
static const OppModel* g_la_model;
static int g_la_boost = 118;
typedef struct { long calls, cache_hits, rollouts; long chosen[LA_MACROS]; double seconds, worst; double* latency; long latency_count, latency_capacity; } LaStats;
static LaStats g_la;
static struct { uint64_t key; int macro; int used; double kill; } g_la_memo[LA_CACHE];
static struct { int step, macro, spec; double kill; } g_la_decisions[512];
static int g_la_decision_count, g_la_report;

static uint32_t la_next(uint32_t* r) { *r ^= *r << 13; *r ^= *r >> 17; *r ^= *r << 5; return *r; }
static double la_uniform(uint32_t* r, double low, double span) { return low + span * (la_next(r) >> 8) / 16777216.0; }
static int la_between(uint32_t* r, int low, int high) { return high <= low ? low : low + (int)(la_next(r) % (uint32_t)(high - low + 1)); }

static void la_resupply(Player* him, uint32_t* r) {
    int cells[OSRS_INVENTORY_SIZE], n = 0;
    for (int slot = 0; slot < OSRS_INVENTORY_SIZE; slot++) {
        OsrsConsumableKind k = osrs_inventory_cell_metadata(&him->inventory_cells[slot])->consumable_kind;
        if (k == OSRS_CONSUMABLE_ANGLERFISH || k == OSRS_CONSUMABLE_KARAMBWAN || k == OSRS_CONSUMABLE_BREW) cells[n++] = slot;
    }
    int hard = la_between(r, 8, 14), fast = la_between(r, 2, 6), brew = la_between(r, 2, 4);
    for (int i = 0; i < n; i++) {
        int pick = i < hard ? 0 : i < hard + fast ? 1 : i < hard + fast + brew ? 2 : 3;
        if (pick == 3) { him->inventory_cells[cells[i]] = osrs_inventory_cell_empty(); continue; }
        uint16_t code = pick == 0 ? riskfight_supply_code(OSRS_CONSUMABLE_ANGLERFISH, 0) : pick == 1 ? riskfight_supply_code(OSRS_CONSUMABLE_KARAMBWAN, 0) :
            riskfight_supply_code(OSRS_CONSUMABLE_BREW, 4);
        him->inventory_cells[cells[i]] = osrs_inventory_cell_from_content_code(code);
    }
}

/** One world: the sim as it stands with everything hidden about him replaced by a draw from what the client infers. */
static void la_world(const Sim* sim, int tick, uint32_t* r, RiskfightState* w, Hist* hist) {
    const GsInfer* inf = &sim->obs.inf;
    *w = sim->state;
    Player* him = &w->env.players[1];
    GsHealthRange bar = inf->opponent_hitpoints;
    int low = imax(1, bar.low + (int)g_la_belief_hp_shift), high = imax(low, bar.high + (int)g_la_belief_hp_shift);
    him->current_hitpoints = la_between(r, low, imin(high, 121));
    GsEnergyRange energy = gs_energy_range(&inf->opponent.spec, tick);
    him->special_energy = energy.low + 10 * la_between(r, 0, (energy.high - energy.low) / 10);
    him->spec_regen_active = him->special_energy < 100;
    him->item_effect_state.special_regen_ticks = la_between(r, 0, 49);
    GsVengeance reading = gs_vengeance_reading(inf->opponent.vengeance, tick);
    him->veng_active = reading == GS_VENGEANCE_UP || ((reading == GS_VENGEANCE_MAYBE || reading == GS_VENGEANCE_UNKNOWN) && la_uniform(r, 0, 1) < 0.5);
    him->veng_cooldown = 0;
    him->current_attack = him->current_strength = g_la_boost;
    GsPid owner = inf->pid.owner;
    double flip = gs_pid_flip_odds(&inf->pid, tick);
    int holder = owner == GS_PID_UNKNOWN ? (int)(la_next(r) & 1) : (owner == GS_PID_LOCAL ? 0 : 1);
    if (owner != GS_PID_UNKNOWN && la_uniform(r, 0, 1) < flip) holder = 1 - holder;
    w->env.pid_holder = holder;
    GsTimerState timer = inf->opponent.timer;
    int attack_timer = 0;
    if (timer.seen) {
        int first = imax(gs_next_attack(timer, tick), tick + 1), last = imax(first, timer.latest);
        attack_timer = la_between(r, first, last) - 1 - tick;
    }
    him->attack_timer = him->attack_timer_uncapped = attack_timer;
    him->food_timer = imax(0, inf->opponent_hard_food_ready - tick);
    him->potion_timer = him->karambwan_timer = 0;
    la_resupply(him, r);
    w->script_seed[1] = osrs_lowbias32(la_next(r));
    *hist = sim->hist;
    hist->since_eat = sim->seen_since_eat;
}

static void la_eat_at_risk(const RiskfightState* s, HumanInput* hi) {
    const Player* p = &s->env.players[0];
    const Player* him = &s->env.players[1];
    AttackStyle style = get_slot_weapon_attack_style((Player*)him) == ATTACK_STYLE_RANGED ? ATTACK_STYLE_RANGED : ATTACK_STYLE_MELEE;
    int normal = calculate_max_hit((Player*)him, style, 1.0f, 30);
    int worst = him->special_energy >= ENERGY_FOR_SPEC && style == ATTACK_STYLE_MELEE ? osrs_voidwaker_max_hit(normal) : normal;
    if (p->current_hitpoints > worst + LA_MARGIN) return;
    int hard = slot_of_kind(p, OSRS_CONSUMABLE_ANGLERFISH), karambwan = slot_of_kind(p, OSRS_CONSUMABLE_KARAMBWAN), brew = slot_of_kind(p, OSRS_CONSUMABLE_BREW);
    if (p->food_timer == 0 && hard >= 0) human_input_queue_command(hi, click_command(hard));
    if (p->karambwan_timer == 0 && karambwan >= 0 && p->current_hitpoints <= worst) human_input_queue_command(hi, click_command(karambwan));
    if (p->potion_timer == 0 && brew >= 0 && p->current_hitpoints <= worst) human_input_queue_command(hi, click_command(brew));
}

static Outcome g_la_scratch;

static LaOutcome la_rollout(Sim* work, const RiskfightState* world, const Hist* hist, int tick, const OppModel* model, const LaScript* script, uint32_t seed) {
    work->state = *world;
    work->hist = *hist;
    work->tick_base = tick;
    work->outcome = &g_la_scratch;
    work->state.env.pvp_runtime.hit_observer = sim_observe_hit;
    work->state.env.pvp_runtime.hit_observer_context = work;
    work->state.env.rng_state = osrs_lowbias32(seed) | 1;
    work->state.script_seed[0] = osrs_lowbias32(seed + 1);
    work->state.script_seed[1] = osrs_lowbias32(seed + 2);
    g_rollout_seed = seed;
    RiskfightState* s = &work->state;
    HumanInput* hi = &work->context.policy_commands[0];
    LaOutcome out = {0};
    for (int step = 0; step < g_la_horizon && !s->env.episode_over; step++) {
        int actions[RF_HEADS];
        opponent_actions(model, work, step, seed + (uint32_t)step * 2654435761u, actions);
        riskfight_policy_commands(s, 1, actions, &work->context.policy_commands[1]);
        human_input_clear_pending(hi);
        for (int i = 0; i < script->n; i++) if (script->due[i] == tick + 1 + step) human_input_queue_command(hi, script->command[i]);
        la_eat_at_risk(s, hi);
        if (!osrs_interaction_active(&s->env.players[0].interaction) && hi->commands.count == 0) human_input_queue_command(hi, attack_command());
        int vw = s->spec_voidwaker[1], maul = s->spec_maul[1];
        work->step_damage = work->step_max_hit = work->step_direct = 0;
        work->obs.hit_count = 0;
        riskfight_step_queues(s, &work->context, &hi->commands, &work->context.policy_commands[1].commands);
        const Player* him = &s->env.players[1];
        int ate_true = him->ate_food_this_tick || him->ate_karambwan_this_tick || s->inventory_use[1].potion_animation_tick_plus_one == s->env.tick;
        StepEvents events = {.ate_visible = consumption_visible(s, 1), .ate = ate_true, .swung = s->visible[0].last_attack_tick == s->env.tick - 1,
            .vw = s->spec_voidwaker[1] - vw, .maul = s->spec_maul[1] - maul, .damage_on_him = work->step_damage, .max_hit_on_him = work->step_max_hit,
            .hit_on_him = him->hit_landed_this_tick};
        hist_step(&work->hist, s, tick + 1 + step, &events);
        out.dealt += work->step_damage;
        if (s->env.players[1].current_hitpoints <= 0) out.kill = 1;
        if (s->env.players[0].current_hitpoints <= 0) out.death = 1;
    }
    out.energy = s->env.players[0].special_energy;
    return out;
}

static double la_tickets_worth(int energy) { return (1 - GS_FIGHT_ENDS) * GS_WORTH[imin(GS_MOST_TICKETS, energy / GS_TICKET)]; }

static void la_press(uint32_t* r, LaScript* script, int tick, const HumanCommand* clicks, int count) {
    double ms = la_uniform(r, FIRST_PRESS_MS, FIRST_PRESS_SPAN);
    for (int i = 0; i < count; i++) {
        if (i) ms += la_uniform(r, GAP_MS, GAP_SPAN);
        int rel = ms < CUTOFF_MS ? 1 : 2 + (int)((ms - CUTOFF_MS) / TICK_MS);
        assert(script->n < LA_SCRIPT);
        script->due[script->n] = tick + rel;
        script->command[script->n++] = clicks[i];
    }
}

static void la_spec_clicks(LaScript* script, uint32_t* r, const Player* us, int due_tick) {
    HumanCommand clicks[2];
    int n = 0;
    if (!us->spec_armed) clicks[n++] = spec_command();
    clicks[n++] = attack_command();
    la_press(r, script, due_tick, clicks, n);
}

static int la_hard_slot(const OursFoods* foods) {
    for (int i = 0; i < foods->food_count; i++) if (foods->foods[i].kind == GS_HARD_FOOD) return i;
    return foods->food_count > 0 ? 0 : -1;
}

static uint64_t la_key(const Sim* sim, const GsInfer* inf, const Player* us, int tick, int energy) {
    const Player* me = us;
    GsHealthRange bar = inf->opponent_hitpoints;
    GsEnergyRange range = gs_energy_range(&inf->opponent.spec, tick);
    uint64_t h = 1469598103934665603ull;
    int parts[] = {me->current_hitpoints, me->attack_timer, me->food_timer, me->potion_timer, me->karambwan_timer, me->spec_armed, energy, me->veng_active, me->veng_cooldown,
        bar.low, bar.high, range.low, range.high, (int)gs_vengeance_reading(inf->opponent.vengeance, tick), (int)inf->pid.owner,
        inf->opponent.timer.seen ? gs_next_attack(inf->opponent.timer, tick) - tick : -1, inf->opponent.timer.latest - tick, imax(0, inf->opponent_hard_food_ready - tick),
        sim->seen_since_eat > 9 ? 9 : sim->seen_since_eat};
    for (size_t i = 0; i < sizeof(parts) / sizeof(*parts); i++) { h ^= (uint64_t)(uint32_t)parts[i]; h *= 1099511628211ull; }
    return h;
}

/** Plays a macro other than the rules when the rollouts say it beats them. Returns 1 when it queued the clicks. */
static int la_override(Sim* sim, Ours* ours, int step, int tick) {
    double started = la_seconds();
    const GsInfer* inf = &sim->obs.inf;
    const Player* us = &sim->state.env.players[0];
    int energy = gs_spec_energy(&inf->c);
    GsStakes stakes = ours->equal_stakes ? (GsStakes){1, 1} : sim->obs.stakes;
    OursFoods foods;
    ours_foods(sim, &foods);
    GsRebound rebound = ours_rebound(sim, ours, tick, 0);
    int spec_ok = gs_spec_plannable(inf) && energy >= GS_TICKET && ours->last_played_step + 2 < step;
    int eat_ok = foods.food_count > 0 && us->current_hitpoints < GS_ASSUMED_MAX_HP;
    int veng_ok = rebound.kind == GS_REBOUND_CASTABLE;
    if (!spec_ok && !(eat_ok && us->current_hitpoints <= 75) && !veng_ok) return 0;

    uint64_t key = la_key(sim, inf, us, tick, energy);
    int slot = (int)(key % LA_CACHE);
    int cached = g_la_cache && g_la_memo[slot].used && g_la_memo[slot].key == key;
    g_la.calls++;
    LaMacro best = LA_RULES;
    double predicted = 0;
    LaScript scripts[LA_MACROS];
    int feasible[LA_MACROS] = {0};
    memset(scripts, 0, sizeof(scripts));
    uint32_t press = osrs_lowbias32((uint32_t)tick * 2654435761u + (uint32_t)step) | 1, saved_press = press;

    Ours trial = *ours;
    trial.lookahead = 0;
    int before = trial.pending_count;
    ours_decide(sim, &trial, step, tick);
    for (int i = before; i < trial.pending_count; i++) {
        scripts[LA_RULES].due[scripts[LA_RULES].n] = trial.pending[i].due;
        scripts[LA_RULES].command[scripts[LA_RULES].n++] = trial.pending[i].command;
    }
    feasible[LA_RULES] = feasible[LA_NONE] = 1;
    uint32_t r = saved_press;
    if (spec_ok) {
        feasible[LA_SPEC_NOW] = 1;
        la_spec_clicks(&scripts[LA_SPEC_NOW], &r, us, tick);
        int swing = tick + 1 + imax(0, us->attack_timer);
        feasible[LA_SPEC_NEXT] = 1;
        r = saved_press;
        HumanCommand again[2] = {spec_command(), attack_command()};
        la_press(&r, &scripts[LA_SPEC_NEXT], swing, again, 2);
        if (energy >= 2 * GS_TICKET) {
            feasible[LA_BURST] = 1;
            r = saved_press;
            la_spec_clicks(&scripts[LA_BURST], &r, us, tick);
            la_press(&r, &scripts[LA_BURST], swing, again, 2);
        }
    }
    if (eat_ok) {
        feasible[LA_EAT_NOW] = 1;
        int choice = la_hard_slot(&foods);
        HumanCommand clicks[2] = {click_command(foods.food_slot[choice]), attack_command()};
        r = saved_press;
        la_press(&r, &scripts[LA_EAT_NOW], tick, clicks, 2);
    }
    int kind_slot[3] = {-1, -1, -1};
    for (int i = 0; i < foods.food_count; i++) kind_slot[foods.foods[i].kind] = foods.food_slot[i];
    if (eat_ok && kind_slot[GS_HARD_FOOD] >= 0 && kind_slot[GS_FAST_FOOD] >= 0 && kind_slot[GS_POTION] >= 0) {
        feasible[LA_EAT_TRIPLE] = feasible[LA_EAT_STAGGER] = 1;
        HumanCommand triple[4] = {click_command(kind_slot[GS_HARD_FOOD]), click_command(kind_slot[GS_FAST_FOOD]), click_command(kind_slot[GS_POTION]), attack_command()};
        r = saved_press;
        la_press(&r, &scripts[LA_EAT_TRIPLE], tick, triple, 4);
        HumanCommand first[2] = {click_command(kind_slot[GS_HARD_FOOD]), attack_command()};
        HumanCommand second[3] = {click_command(kind_slot[GS_POTION]), click_command(kind_slot[GS_FAST_FOOD]), attack_command()};
        r = saved_press;
        la_press(&r, &scripts[LA_EAT_STAGGER], tick, first, 2);
        la_press(&r, &scripts[LA_EAT_STAGGER], tick + 3, second, 3);
        la_press(&r, &scripts[LA_EAT_STAGGER], tick + 6, first, 2);
    }
    if (veng_ok) {
        feasible[LA_VENG_NOW] = 1;
        HumanCommand clicks[2] = {vengeance_command(), attack_command()};
        r = saved_press;
        la_press(&r, &scripts[LA_VENG_NOW], tick, clicks, 2);
    }

    if (cached) {
        best = (LaMacro)g_la_memo[slot].macro;
        predicted = g_la_memo[slot].kill;
        g_la.cache_hits++;
    } else {
        static Sim* work;
        if (!work) { work = malloc(sizeof(Sim)); sim_init(work); }
        const OppModel* model = g_la_model;
        uint32_t saved_seed = g_rollout_seed;
        double saved_scale = g_eat_scale;
        g_eat_scale = g_la_eat_scale_belief;
        uint32_t world_rng = osrs_lowbias32((uint32_t)tick * 40503u + (uint32_t)step * 7919u + ours->rng) | 1;
        LaScore score[LA_MACROS];
        memset(score, 0, sizeof(score));
        int alive[LA_MACROS];
        for (int m = 0; m < LA_MACROS; m++) alive[m] = feasible[m];
        double ratio = stakes.us > 0 ? (double)stakes.him / (double)stakes.us : 1;
        static RiskfightState world;
        static Hist hist;
        int done = 0;
        while (done < g_la_worlds) {
            int upto = done < g_la_stage && g_la_worlds > g_la_stage ? g_la_stage : g_la_worlds;
            for (int w = done; w < upto; w++) {
                uint32_t draw = world_rng ^ (uint32_t)(w * 2246822519u);
                uint32_t wr = osrs_lowbias32(draw) | 1;
                la_world(sim, tick, &wr, &world, &hist);
                uint32_t seed = osrs_lowbias32(draw + 17);
                for (int m = 0; m < LA_MACROS; m++) {
                    if (!alive[m]) continue;
                    LaOutcome o = la_rollout(work, &world, &hist, tick, model, &scripts[m], seed);
                    score[m].kill += o.kill;
                    score[m].death += o.death;
                    score[m].tickets += (1 - o.kill) * la_tickets_worth(o.energy) / (1 - GS_FIGHT_ENDS);
                    score[m].value += o.kill * ratio - o.death + (1 - o.kill) * la_tickets_worth(o.energy) * ratio;
                    score[m].worlds++;
                    g_la.rollouts++;
                }
            }
            done = upto;
            if (done < g_la_worlds) {
                double top = -1e300;
                for (int m = 0; m < LA_MACROS; m++) if (alive[m]) top = fmax(top, score[m].value / score[m].worlds);
                for (int m = 0; m < LA_MACROS; m++)
                    if (alive[m] && m != LA_RULES && score[m].value / score[m].worlds < top - g_la_prune * (ratio + 1)) alive[m] = 0;
            }
        }
        double top = score[LA_RULES].value / score[LA_RULES].worlds;
        for (int m = 0; m < LA_MACROS; m++) {
            if (!alive[m] || m == LA_RULES) continue;
            double v = score[m].value / score[m].worlds;
            if (v > top) { top = v; best = (LaMacro)m; }
        }
        predicted = score[best].kill / score[best].worlds;
        if (g_la_report) {
            printf("tick %d (step %d) our hp %d energy %d his belief hp %d-%d energy %d-%d ratio %.2f\n", tick, step, us->current_hitpoints, energy, inf->opponent_hitpoints.low, inf->opponent_hitpoints.high,
                gs_energy_range(&inf->opponent.spec, tick).low, gs_energy_range(&inf->opponent.spec, tick).high, ratio);
            for (int m = 0; m < LA_MACROS; m++)
                if (feasible[m]) printf("  %-14s %s worlds %3d kill %.3f death %.3f tickets-worth %.3f value %+.4f%s\n", LA_NAME[m], alive[m] ? " " : "x", score[m].worlds, score[m].kill / score[m].worlds,
                    score[m].death / score[m].worlds, score[m].tickets / score[m].worlds, score[m].value / score[m].worlds, m == (int)best ? "  <- chosen" : "");
        }
        g_rollout_seed = saved_seed;
        g_eat_scale = saved_scale;
        g_la_memo[slot].used = 1;
        g_la_memo[slot].key = key;
        g_la_memo[slot].macro = best;
        g_la_memo[slot].kill = predicted;
    }
    g_la.chosen[best]++;
    int spec = best == LA_SPEC_NOW || best == LA_BURST || best == LA_SPEC_NEXT;
    for (int i = 0; best == LA_RULES && i < scripts[LA_RULES].n; i++) spec |= scripts[LA_RULES].command[i].kind == HUMAN_COMMAND_SPEC_TOGGLE;
    if (g_la_decision_count < 512) {
        g_la_decisions[g_la_decision_count].step = step;
        g_la_decisions[g_la_decision_count].macro = best;
        g_la_decisions[g_la_decision_count].spec = spec;
        g_la_decisions[g_la_decision_count++].kill = predicted;
    }
    double elapsed = la_seconds() - started;
    g_la.seconds += elapsed;
    if (elapsed > g_la.worst) g_la.worst = elapsed;
    if (g_la.latency_count == g_la.latency_capacity) {
        g_la.latency_capacity = g_la.latency_capacity ? 2 * g_la.latency_capacity : 1 << 16;
        g_la.latency = realloc(g_la.latency, (size_t)g_la.latency_capacity * sizeof(double));
    }
    g_la.latency[g_la.latency_count++] = elapsed;
    if (best == LA_RULES) return 0;
    ours->last_decision = tick;
    ours->decisions++;
    for (int i = 0; i < scripts[best].n; i++) ours_push(ours, scripts[best].due[i], scripts[best].command[i]);
    if (best == LA_VENG_NOW) ours->vengeance_clicked = tick + 1;
    if (best == LA_SPEC_NOW || best == LA_BURST) { ours->last_played_choice = GS_CHOICE_VOIDWAKER; ours->last_played_step = step; }
    return 1;
}
#endif
