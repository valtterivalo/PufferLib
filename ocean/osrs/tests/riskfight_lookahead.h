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
typedef struct { int kill, death, energy, ate_at; double dealt, taken; } LaOutcome;
typedef struct { double kill, death, tickets, value; int worlds; } LaScore;
typedef struct {
    int hp_low, hp_high, energy_low, energy_high, vengeance, pid, timer_seen, timer_first, timer_last, food_ready, seen_since_eat, seen_vw, seen_maul, bar_ratio;
    double flip;
    GsObservedGear gear;
} LaBelief;

typedef enum { LA_GEAR_SUPPORTED, LA_GEAR_UNKNOWN, LA_GEAR_WEAPON_UNSUPPORTED, LA_GEAR_SHIELD_UNSUPPORTED, LA_GEAR_AMMO_UNSUPPORTED, LA_GEAR_STATUS_COUNT } LaGearStatus;
static const char* const LA_GEAR_STATUS_NAME[] = {"supported", "opponent_gear_unknown", "opponent_weapon_unsupported", "opponent_shield_unsupported", "opponent_ammo_prior_unsupported"};

static int la_item_supported(int raw) {
    if (raw == -1) return 1;
    for (int i = 0; i < NUM_ITEMS; i++) if (ITEM_DATABASE[i].item_id == raw) return 1;
    return 0;
}

static LaGearStatus la_gear_status(GsObservedGear gear) {
    if (gear.source == GS_GEAR_UNKNOWN) return LA_GEAR_UNKNOWN;
    if (!la_item_supported(gear.weapon)) return LA_GEAR_WEAPON_UNSUPPORTED;
    if (!la_item_supported(gear.shield)) return LA_GEAR_SHIELD_UNSUPPORTED;
    if (gear.weapon != -1 && get_item_attack_style(raw_id_to_item(gear.weapon)) == ATTACK_STYLE_RANGED && raw_id_to_item(gear.weapon) != ITEM_MAGIC_SHORTBOW_I) return LA_GEAR_AMMO_UNSUPPORTED;
    return LA_GEAR_SUPPORTED;
}

static int g_la_worlds = 64, g_la_horizon = 15, g_la_stage = 16, g_la_cache = 1;
static double g_la_prune = 0.03, g_la_belief_hp_shift = 0, g_la_eat_scale_belief = 1;
static const OppModel* g_la_model;
static int g_la_boost = 118;
typedef struct { long calls, cache_hits, rollouts; long chosen[LA_MACROS]; double seconds, worst; double* latency; long latency_count, latency_capacity; } LaStats;
static LaStats g_la;
static struct { uint64_t key; int macro; int used; double kill; } g_la_memo[LA_CACHE];
static struct { int step, macro, spec; double kill; } g_la_decisions[512];
static int g_la_decision_count, g_la_report;
static double g_la_last_kill[LA_MACROS], g_la_last_value[LA_MACROS];
static int g_la_last_feasible[LA_MACROS];

static uint32_t la_next(uint32_t* r) { *r ^= *r << 13; *r ^= *r >> 17; *r ^= *r << 5; return *r; }
static double la_uniform(uint32_t* r, double low, double span) { return low + span * (la_next(r) >> 8) / 16777216.0; }
static int la_between(uint32_t* r, int low, int high) { return high <= low ? low : low + (int)(la_next(r) % (uint32_t)(high - low + 1)); }

static int la_food_timer(int ready, int tick) { return ready > tick + 1 ? ready - tick - 1 : 0; }

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
static void la_world(const RiskfightState* base, const Hist* base_hist, const LaBelief* b, int tick, uint32_t* r, RiskfightState* w, Hist* hist) {
    *w = *base;
    Player* him = &w->env.players[1];
    assert(la_gear_status(b->gear) == LA_GEAR_SUPPORTED);
    him->equipped[GEAR_SLOT_WEAPON] = b->gear.weapon == -1 ? ITEM_NONE : raw_id_to_item(b->gear.weapon);
    him->equipped[GEAR_SLOT_SHIELD] = b->gear.shield == -1 ? ITEM_NONE : raw_id_to_item(b->gear.shield);
    assert(!item_is_two_handed(him->equipped[GEAR_SLOT_WEAPON]) || him->equipped[GEAR_SLOT_SHIELD] == ITEM_NONE);
    him->equipped[GEAR_SLOT_AMMO] = him->equipped[GEAR_SLOT_WEAPON] == ITEM_MAGIC_SHORTBOW_I ? ITEM_AMETHYST_ARROW : ITEM_NONE;
    osrs_refresh_player_equipment(him);
    int low = imax(1, b->hp_low + (int)g_la_belief_hp_shift), high = imax(low, b->hp_high + (int)g_la_belief_hp_shift);
    him->current_hitpoints = la_between(r, low, imin(high, 121));
    him->special_energy = b->energy_low + 10 * la_between(r, 0, (b->energy_high - b->energy_low) / 10);
    him->spec_regen_active = him->special_energy < 100;
    him->item_effect_state.special_regen_ticks = la_between(r, 0, 49);
    him->veng_active = b->vengeance == GS_VENGEANCE_UP || ((b->vengeance == GS_VENGEANCE_MAYBE || b->vengeance == GS_VENGEANCE_UNKNOWN) && la_uniform(r, 0, 1) < 0.5);
    him->veng_cooldown = 0;
    w->visible[0].health_bar = b->bar_ratio;
    him->current_attack = him->current_strength = g_la_boost;
    int holder = b->pid == GS_PID_UNKNOWN ? (int)(la_next(r) & 1) : (b->pid == GS_PID_LOCAL ? 0 : 1);
    if (b->pid != GS_PID_UNKNOWN && la_uniform(r, 0, 1) < b->flip) holder = 1 - holder;
    w->env.pid_holder = holder;
    int attack_timer = 0;
    if (b->timer_seen) {
        assert(b->timer_first <= b->timer_last);
        attack_timer = la_between(r, b->timer_first, b->timer_last) - 1 - tick;
    }
    him->attack_timer_uncapped = attack_timer;
    him->attack_timer = imax(-1, attack_timer);
    him->has_attack_timer = 1;
    him->food_timer = la_food_timer(b->food_ready, tick);
    him->potion_timer = him->karambwan_timer = 0;
    la_resupply(him, r);
    int carries_vw = b->seen_vw || la_uniform(r, 0, 1) < 0.65, carries_maul = b->seen_maul || la_uniform(r, 0, 1) < 0.51;
    for (int slot = 0; slot < OSRS_INVENTORY_SIZE; slot++)
        if (osrs_inventory_cell_metadata(&him->inventory_cells[slot])->consumable_kind == OSRS_CONSUMABLE_NONE) him->inventory_cells[slot] = osrs_inventory_cell_empty();
    int wielded_vw = him->equipped[GEAR_SLOT_WEAPON] == ITEM_VOIDWAKER, wielded_maul = is_maul_item(him->equipped[GEAR_SLOT_WEAPON]);
    for (int pass = 0; pass < 2; pass++) {
        int wanted = (pass == 0 ? carries_vw : carries_maul) && !(pass == 0 ? wielded_vw : wielded_maul);
        uint8_t item = pass == 0 ? ITEM_VOIDWAKER : ITEM_GRANITE_MAUL_ORNATE;
        for (int slot = 0; wanted && slot < OSRS_INVENTORY_SIZE; slot++)
            if (osrs_inventory_cell_is_empty(&him->inventory_cells[slot])) { him->inventory_cells[slot] = osrs_inventory_cell_from_content_code(osrs_inventory_content_code_from_item(item)); break; }
    }
    w->script_seed[1] = osrs_lowbias32(la_next(r));
    *hist = *base_hist;
    hist->since_eat = b->seen_since_eat;
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
    work->state.env.pvp_runtime.attack_chance_observer = NULL;
    work->state.env.rng_state = osrs_lowbias32(seed) | 1;
    work->state.script_seed[0] = osrs_lowbias32(seed + 1);
    work->state.script_seed[1] = osrs_lowbias32(seed + 2);
    g_rollout_seed = seed;
    RiskfightState* s = &work->state;
    HumanInput* hi = &work->context.policy_commands[0];
    LaOutcome out = {0};
    out.ate_at = -1;
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
        if (events.ate_visible && out.ate_at < 0) out.ate_at = step;
        if (s->env.players[1].current_hitpoints <= 0) out.kill = 1;
        if (s->env.players[0].current_hitpoints <= 0) out.death = 1;
    }
    out.energy = s->env.players[0].special_energy;
    return out;
}

static double g_la_worth_scale = 1, g_la_w[GS_MOST_TICKETS + 1];
static int g_la_worth_mode = 1, g_la_burst;
static double la_tickets_worth(int energy) { return g_la_worth_scale * (1 - GS_FIGHT_ENDS) * g_la_w[imin(GS_MOST_TICKETS, energy / GS_TICKET)]; }

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

typedef struct { int spec_ok, eat_ok, veng_ok, energy; } LaFeasible;

static void la_food_slots(const Player* us, int slot[3]) {
    slot[GS_HARD_FOOD] = slot_of_kind(us, OSRS_CONSUMABLE_ANGLERFISH);
    slot[GS_POTION] = slot_of_kind(us, OSRS_CONSUMABLE_BREW);
    slot[GS_FAST_FOOD] = slot_of_kind(us, OSRS_CONSUMABLE_KARAMBWAN);
}

/** The macro scripts as clicks timed from the decision, and which of them are possible. The rules' own script is the caller's. */
static void la_scripts(const RiskfightState* base, int tick, const LaFeasible* f, const LaScript* rules, LaScript scripts[LA_MACROS], int feasible[LA_MACROS]) {
    const Player* us = &base->env.players[0];
    memset(scripts, 0, LA_MACROS * sizeof(LaScript));
    memset(feasible, 0, LA_MACROS * sizeof(int));
    uint32_t saved = osrs_lowbias32((uint32_t)tick * 2654435761u) | 1, r;
    scripts[LA_RULES] = *rules;
    feasible[LA_RULES] = feasible[LA_NONE] = 1;
    if (f->spec_ok) {
        feasible[LA_SPEC_NOW] = feasible[LA_SPEC_NEXT] = 1;
        r = saved;
        la_spec_clicks(&scripts[LA_SPEC_NOW], &r, us, tick);
        int swing = tick + 1 + imax(0, us->attack_timer);
        HumanCommand again[2] = {spec_command(), attack_command()};
        r = saved;
        la_press(&r, &scripts[LA_SPEC_NEXT], swing, again, 2);
        if (f->energy >= 2 * GS_TICKET && g_la_burst) {
            feasible[LA_BURST] = 1;
            r = saved;
            la_spec_clicks(&scripts[LA_BURST], &r, us, tick);
            la_press(&r, &scripts[LA_BURST], swing, again, 2);
        }
    }
    int slot[3];
    la_food_slots(us, slot);
    if (f->eat_ok) {
        feasible[LA_EAT_NOW] = 1;
        int pick = slot[GS_HARD_FOOD] >= 0 ? slot[GS_HARD_FOOD] : slot[GS_FAST_FOOD] >= 0 ? slot[GS_FAST_FOOD] : slot[GS_POTION];
        HumanCommand clicks[2] = {click_command(pick), attack_command()};
        r = saved;
        la_press(&r, &scripts[LA_EAT_NOW], tick, clicks, 2);
    }
    if (f->eat_ok && slot[GS_HARD_FOOD] >= 0 && slot[GS_FAST_FOOD] >= 0 && slot[GS_POTION] >= 0) {
        feasible[LA_EAT_TRIPLE] = feasible[LA_EAT_STAGGER] = 1;
        HumanCommand triple[4] = {click_command(slot[GS_HARD_FOOD]), click_command(slot[GS_FAST_FOOD]), click_command(slot[GS_POTION]), attack_command()};
        r = saved;
        la_press(&r, &scripts[LA_EAT_TRIPLE], tick, triple, 4);
        HumanCommand first[2] = {click_command(slot[GS_HARD_FOOD]), attack_command()};
        HumanCommand second[3] = {click_command(slot[GS_POTION]), click_command(slot[GS_FAST_FOOD]), attack_command()};
        r = saved;
        la_press(&r, &scripts[LA_EAT_STAGGER], tick, first, 2);
        la_press(&r, &scripts[LA_EAT_STAGGER], tick + 3, second, 3);
        la_press(&r, &scripts[LA_EAT_STAGGER], tick + 6, first, 2);
    }
    if (f->veng_ok) {
        feasible[LA_VENG_NOW] = 1;
        HumanCommand clicks[2] = {vengeance_command(), attack_command()};
        r = saved;
        la_press(&r, &scripts[LA_VENG_NOW], tick, clicks, 2);
    }
}

/** Rolls every feasible macro out on worlds drawn from the belief and picks the best by value, the rules unless another is strictly better. */
/** The worth of holding k spec tickets, from the sim itself: the extra kill chance over the horizon of throwing a spec at every swing against none, on worlds from the belief. */
static void la_measure_worth(const RiskfightState* base, const Hist* base_hist, const LaBelief* belief, int tick, uint32_t salt, Sim* work) {
    enum { WORLDS = 16, SWINGS = 4, SWING_TICKS = 4 };
    LaScript greedy = {0}, none = {0};
    int first = tick + 1 + imax(0, base->env.players[0].attack_timer);
    for (int j = 0; j < SWINGS; j++) {
        greedy.due[greedy.n] = first + j * SWING_TICKS - 1;
        greedy.command[greedy.n++] = spec_command();
    }
    g_la_w[0] = 0;
    static RiskfightState world;
    static Hist hist;
    for (int k = 1; k <= GS_MOST_TICKETS; k++) {
        double gain = 0;
        for (int w = 0; w < WORLDS; w++) {
            uint32_t draw = salt ^ (uint32_t)(w * 2246822519u);
            uint32_t wr = osrs_lowbias32(draw) | 1;
            la_world(base, base_hist, belief, tick, &wr, &world, &hist);
            world.env.players[0].special_energy = GS_TICKET * k;
            world.env.players[0].spec_armed = 0;
            uint32_t seed = osrs_lowbias32(draw + 17);
            gain += la_rollout(work, &world, &hist, tick, g_la_model, &greedy, seed).kill - la_rollout(work, &world, &hist, tick, g_la_model, &none, seed).kill;
        }
        g_la_w[k] = fmax(0, gain / WORLDS);
    }
}

/** Everything of a world the sampler may have drawn or copied that is not visible gear, as one number. */
static uint64_t la_fingerprint(const RiskfightState* w) {
    uint64_t h = 1469598103934665603ull;
    const Player* him = &w->env.players[1];
    int parts[] = {him->current_hitpoints, him->special_energy, him->spec_regen_active, him->item_effect_state.special_regen_ticks, him->veng_active, him->veng_cooldown, him->attack_timer, him->attack_timer_uncapped, him->has_attack_timer,
        him->food_timer, him->potion_timer, him->karambwan_timer, him->current_attack, him->current_strength, w->env.pid_holder, w->visible[0].health_bar, (int)w->script_seed[1], him->equipped[GEAR_SLOT_WEAPON], him->equipped[GEAR_SLOT_SHIELD], him->equipped[GEAR_SLOT_AMMO]};
    for (size_t i = 0; i < sizeof(parts) / sizeof(*parts); i++) { h ^= (uint64_t)(uint32_t)parts[i]; h *= 1099511628211ull; }
    int counts[256][5] = {{0}};
    for (int i = 0; i < OSRS_INVENTORY_SIZE; i++) {
        const OsrsItemContentMetadata* m = osrs_inventory_cell_metadata(&him->inventory_cells[i]);
        counts[m->item_idx][m->dose_count > 4 ? 4 : m->dose_count] += 1 + 16 * (int)m->consumable_kind;
    }
    for (int i = 0; i < 256; i++) for (int d = 0; d < 5; d++) if (counts[i][d]) { h ^= (uint64_t)(i * 5 + d) * 2654435761u + (uint64_t)counts[i][d]; h *= 1099511628211ull; }
    return h;
}

static void la_print_base(const RiskfightState* b) {
    for (int a = 0; a < 2; a++) {
        const Player* p = &b->env.players[a];
        fprintf(stderr, "P%d hp %d atk %d str %d def %d mag %d pray %d spec %d armed %d veng %d/%d timer %d food %d %d %d pos %d,%d int %d prayer %d off %d recoil %d eq", a, p->current_hitpoints, p->current_attack, p->current_strength,
            p->current_defence, p->current_magic, p->current_prayer, p->special_energy, p->spec_armed, p->veng_active, p->veng_cooldown, p->attack_timer, p->food_timer, p->potion_timer,
            p->karambwan_timer, p->x, p->y, osrs_interaction_active(&p->interaction), p->prayer, p->offensive_prayer, p->item_effect_state.recoil_damage_used);
        for (int i = 0; i < NUM_GEAR_SLOTS; i++) fprintf(stderr, " %d", p->equipped[i]);
        fprintf(stderr, " inv");
        for (int i = 0; i < OSRS_INVENTORY_SIZE; i++) fprintf(stderr, " %d", osrs_inventory_cell_metadata(&p->inventory_cells[i])->consumable_kind);
        fprintf(stderr, "\n");
    }
    fprintf(stderr, "pid %d visible bars %d %d\n", b->env.pid_holder, b->visible[0].health_bar, b->visible[1].health_bar);
}

static LaMacro la_evaluate(const RiskfightState* base, const Hist* base_hist, const LaBelief* belief, int tick, GsStakes stakes, uint32_t salt,
    const LaScript scripts[LA_MACROS], const int feasible[LA_MACROS], LaScore score[LA_MACROS], int alive[LA_MACROS]) {
    if (getenv("GS_LA_BASE")) la_print_base(base);
    if (getenv("GS_LA_SAVE")) { FILE* f = fopen(getenv("GS_LA_SAVE"), "wb"); fwrite(base, sizeof(*base), 1, f); fclose(f); }
    static Sim* work;
    if (!work) { work = malloc(sizeof(Sim)); sim_init(work); }
    uint32_t saved_seed = g_rollout_seed;
    double saved_scale = g_eat_scale;
    g_eat_scale = g_la_eat_scale_belief;
    uint32_t world_rng = osrs_lowbias32((uint32_t)tick * 40503u + salt) | 1;
    memset(score, 0, LA_MACROS * sizeof(LaScore));
    if (g_la_worth_mode == 1) la_measure_worth(base, base_hist, belief, tick, world_rng ^ 0x5bd1e995u, work);
    else for (int k = 0; k <= GS_MOST_TICKETS; k++) g_la_w[k] = GS_WORTH[k];
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
            la_world(base, base_hist, belief, tick, &wr, &world, &hist);
            if (getenv("GS_LA_WORLDS")) {
                const Player* h = &world.env.players[1];
                int vw = 0, mm = 0, food = 0;
                for (int i = 0; i < OSRS_INVENTORY_SIZE; i++) { uint8_t it = osrs_inventory_cell_metadata(&h->inventory_cells[i])->item_idx; vw += it == ITEM_VOIDWAKER; mm += is_maul_item(it); food += osrs_inventory_cell_metadata(&h->inventory_cells[i])->consumable_kind == OSRS_CONSUMABLE_ANGLERFISH; }
                fprintf(stderr, "W hp %d en %d timer %d veng %d pid %d vw %d maul %d food %d eqw %d since %d warn %d\n", h->current_hitpoints, h->special_energy, h->attack_timer, h->veng_active, world.env.pid_holder, vw, mm, food, h->equipped[GEAR_SLOT_WEAPON], hist.since_eat, warning_since(&hist.warning));
            }
            uint32_t seed = osrs_lowbias32(draw + 17);
            for (int m = 0; m < LA_MACROS; m++) {
                if (!alive[m]) continue;
                LaOutcome o = la_rollout(work, &world, &hist, tick, g_la_model, &scripts[m], seed);
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
    LaMacro best = LA_RULES;
    double top = score[LA_RULES].value / score[LA_RULES].worlds;
    for (int m = 0; m < LA_MACROS; m++) {
        if (!alive[m] || m == LA_RULES) continue;
        double v = score[m].value / score[m].worlds;
        if (v > top) { top = v; best = (LaMacro)m; }
    }
    g_rollout_seed = saved_seed;
    g_eat_scale = saved_scale;
    return best;
}

static LaBelief la_belief_of(const Sim* sim, int tick) {
    const GsInfer* inf = &sim->obs.inf;
    GsEnergyRange energy = gs_energy_range(&inf->opponent.spec, tick);
    GsTimerState timer = inf->opponent.timer;
    int seen_vw = 0, seen_maul = 0;
    for (int i = 0; i < inf->arsenal_count; i++) {
        seen_vw |= inf->arsenal[i].weapon == GS_W_VOIDWAKER;
        seen_maul |= inf->arsenal[i].weapon == GS_W_GRANITE_MAUL;
    }
    return (LaBelief){.hp_low = inf->opponent_hitpoints.low, .hp_high = inf->opponent_hitpoints.high, .energy_low = energy.low, .energy_high = energy.high,
        .vengeance = (int)gs_vengeance_reading(inf->opponent.vengeance, tick), .pid = (int)inf->pid.owner, .timer_seen = timer.seen,
        .timer_first = timer.seen ? timer.earliest : 0, .timer_last = timer.latest, .food_ready = inf->opponent_hard_food_ready,
        .seen_since_eat = sim->seen_since_eat, .seen_vw = seen_vw, .seen_maul = seen_maul, .bar_ratio = sim->state.visible[0].health_bar, .flip = gs_pid_flip_odds(&inf->pid, tick), .gear = inf->c.opponent_gear};
}

static uint64_t la_key(const LaBelief* b, const Player* us, int tick, int energy) {
    uint64_t h = 1469598103934665603ull;
    int parts[] = {us->current_hitpoints, us->attack_timer, us->attack_timer_uncapped, us->has_attack_timer, us->food_timer, us->potion_timer, us->karambwan_timer, us->spec_armed, energy, us->veng_active, us->veng_cooldown,
        b->hp_low, b->hp_high, b->energy_low, b->energy_high, b->vengeance, b->pid, b->timer_seen ? b->timer_first - tick : -1, b->timer_last - tick, la_food_timer(b->food_ready, tick),
        b->seen_since_eat > 9 ? 9 : b->seen_since_eat, b->seen_vw, b->seen_maul, b->gear.source, b->gear.weapon, b->gear.shield};
    for (size_t i = 0; i < sizeof(parts) / sizeof(*parts); i++) { h ^= (uint64_t)(uint32_t)parts[i]; h *= 1099511628211ull; }
    return h;
}

static void la_print_report(const LaBelief* b, const Player* us, int energy, int tick, int step, double ratio, LaMacro best, const LaScore score[LA_MACROS], const int feasible[LA_MACROS], const int alive[LA_MACROS]) {
    printf("tick %d (step %d) our hp %d energy %d his belief hp %d-%d energy %d-%d ratio %.2f\n", tick, step, us->current_hitpoints, energy, b->hp_low, b->hp_high, b->energy_low, b->energy_high, ratio);
    for (int m = 0; m < LA_MACROS; m++)
        if (feasible[m]) printf("  %-14s %s worlds %3d kill %.3f death %.3f tickets-worth %.3f value %+.4f%s\n", LA_NAME[m], alive[m] ? " " : "x", score[m].worlds, score[m].kill / score[m].worlds,
            score[m].death / score[m].worlds, score[m].tickets / score[m].worlds, score[m].value / score[m].worlds, m == (int)best ? "  <- chosen" : "");
}

/** Plays a macro other than the rules when the rollouts say it beats them. Returns 1 when it queued the clicks. */
static void (*g_la_dump)(const Sim*, const LaBelief*, int, const LaFeasible*, GsStakes, const LaScript*);

static int la_override(Sim* sim, Ours* ours, int step, int tick) {
    double started = la_seconds();
    const GsInfer* inf = &sim->obs.inf;
    const Player* us = &sim->state.env.players[0];
    LaFeasible f;
    f.energy = gs_spec_energy(&inf->c);
    GsStakes stakes = ours->equal_stakes ? (GsStakes){1, 1} : sim->obs.stakes;
    int slot[3];
    la_food_slots(us, slot);
    GsRebound rebound = ours_rebound(sim, ours, tick, 0);
    f.spec_ok = gs_spec_plannable(inf) && f.energy >= GS_TICKET && ours->last_played_step + 2 < step;
    f.eat_ok = (slot[0] >= 0 || slot[1] >= 0 || slot[2] >= 0) && us->current_hitpoints < GS_ASSUMED_MAX_HP;
    f.veng_ok = rebound.kind == GS_REBOUND_CASTABLE;
    if (!f.spec_ok && !(f.eat_ok && us->current_hitpoints <= 75) && !f.veng_ok) return 0;

    LaBelief belief = la_belief_of(sim, tick);
    LaGearStatus gear_status = la_gear_status(belief.gear);
    if (gear_status != LA_GEAR_SUPPORTED) {
        fprintf(stderr, "lookahead unavailable tick %d reason %s weapon %d shield %d\n", tick, LA_GEAR_STATUS_NAME[gear_status], belief.gear.weapon, belief.gear.shield);
        return 0;
    }
    uint64_t key = la_key(&belief, us, tick, f.energy);
    int memo = (int)(key % LA_CACHE);
    int cached = g_la_cache && g_la_memo[memo].used && g_la_memo[memo].key == key;
    g_la.calls++;
    LaScript rules = {0};
    Ours trial = *ours;
    trial.lookahead = 0;
    int before = trial.pending_count;
    ours_decide(sim, &trial, step, tick);
    for (int i = before; i < trial.pending_count; i++) {
        rules.due[rules.n] = trial.pending[i].due;
        rules.command[rules.n++] = trial.pending[i].command;
    }
    if (g_la_dump) g_la_dump(sim, &belief, tick, &f, stakes, &rules);
    LaScript scripts[LA_MACROS];
    int feasible[LA_MACROS];
    la_scripts(&sim->state, tick, &f, &rules, scripts, feasible);
    LaMacro best = LA_RULES;
    double predicted = 0;
    if (cached) {
        best = (LaMacro)g_la_memo[memo].macro;
        predicted = g_la_memo[memo].kill;
        g_la.cache_hits++;
    } else {
        LaScore score[LA_MACROS];
        int alive[LA_MACROS];
        Hist cold = hist_start();
        const Hist* chosen_hist = getenv("GS_LA_COLDHIST") ? &cold : &sim->hist;
        best = la_evaluate(&sim->state, chosen_hist, &belief, tick, stakes, (uint32_t)step * 7919u + ours->rng, scripts, feasible, score, alive);
        predicted = score[best].kill / score[best].worlds;
        for (int m = 0; m < LA_MACROS; m++) {
            g_la_last_feasible[m] = feasible[m] && score[m].worlds > 0;
            g_la_last_kill[m] = score[m].worlds ? score[m].kill / score[m].worlds : 0;
            g_la_last_value[m] = score[m].worlds ? score[m].value / score[m].worlds : 0;
        }
        if (g_la_report) la_print_report(&belief, us, f.energy, tick, step, stakes.us > 0 ? (double)stakes.him / (double)stakes.us : 1, best, score, feasible, alive);
        g_la_memo[memo].used = 1;
        g_la_memo[memo].key = key;
        g_la_memo[memo].macro = best;
        g_la_memo[memo].kill = predicted;
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
    if (best == LA_SPEC_NOW || best == LA_SPEC_NEXT || best == LA_BURST) { ours->last_played_choice = GS_CHOICE_VOIDWAKER; ours->last_played_step = step; }
    return 1;
}
#endif
