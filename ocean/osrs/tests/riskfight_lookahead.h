#ifndef RISKFIGHT_LOOKAHEAD_H
#define RISKFIGHT_LOOKAHEAD_H

/* Our side decides by simulating the next ticks: every macro plan is rolled out on worlds drawn from the plugin's belief about him, with common
 * random numbers across plans, and scored as kill chance times his stakes less death chance times ours, plus the spec tickets left. */
enum { LA_CACHE = 4096, LA_MARGIN = 10 };
static double la_seconds(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec + t.tv_nsec * 1e-9;
}
typedef enum { LA_RULES, LA_NONE, LA_SPEC_NOW, LA_SPEC_NEXT, LA_BURST, LA_EAT_NOW, LA_VENG_NOW, LA_EAT_TRIPLE, LA_EAT_STAGGER, LA_WAIT_ALIGN,
    LA_VENG_SPEC_NOW, LA_VENG_SPEC_NOW_BURST, LA_WAIT_VENG_SPEC_1, LA_WAIT_VENG_SPEC_2, LA_WAIT_VENG_SPEC_3,
    LA_WAIT_VENG_SPEC_BURST_1, LA_WAIT_VENG_SPEC_BURST_2, LA_WAIT_VENG_SPEC_BURST_3, LA_MACROS } LaMacro;
static const char* const LA_NAME[LA_MACROS] = {"rules", "no new clicks", "spec now", "spec next", "burst", "eat now", "vengeance now", "eat triple", "eat staggered", "stop and align",
    "vengeance spec now", "vengeance spec now burst", "wait vengeance spec 1", "wait vengeance spec 2", "wait vengeance spec 3",
    "wait vengeance spec burst 1", "wait vengeance spec burst 2", "wait vengeance spec burst 3"};

typedef struct { int kill, death, energy, ate_at; double dealt, taken; } LaOutcome;
typedef struct { double kill, death, tickets, value; int worlds; } LaScore;
typedef struct {
    int hp_low, hp_high, energy_low, energy_high, vengeance, pid, timer_seen, timer_first, timer_last, food_ready, seen_since_eat, seen_vw, seen_maul, bar_ratio;
    double flip;
    GsObservedGear gear;
    int combat_level;
    const GsHealthHistory* health; /* Borrowed immutable source support for this decision's world samples. */
} LaBelief;

typedef enum { LA_GEAR_SUPPORTED, LA_GEAR_UNKNOWN, LA_GEAR_WEAPON_UNSUPPORTED, LA_GEAR_SHIELD_UNSUPPORTED, LA_GEAR_AMMO_UNSUPPORTED, LA_GEAR_ARMOR_UNKNOWN, LA_GEAR_ARMOR_UNSUPPORTED, LA_GEAR_STATUS_COUNT } LaGearStatus;
static const char* const LA_GEAR_STATUS_NAME[] = {"supported", "opponent_gear_unknown", "opponent_weapon_unsupported", "opponent_shield_unsupported", "opponent_ammo_prior_unsupported", "opponent_armor_unknown", "opponent_armor_unsupported"};
typedef struct { LaGearStatus status; int slot, raw; } LaGearIssue;

typedef enum { LA_ITEM_EMPTY, LA_ITEM_MAPPED, LA_ITEM_UNSUPPORTED } LaItemKind;
typedef struct { LaItemKind kind; ItemIndex item; } LaItem;

static LaItem la_item_of(int raw) {
    if (raw == -1) return (LaItem){LA_ITEM_EMPTY, ITEM_NONE};
    int item = osrs_item_index_from_raw_id(raw);
    return item < 0 ? (LaItem){.kind = LA_ITEM_UNSUPPORTED}
        : (LaItem){LA_ITEM_MAPPED, (ItemIndex)item};
}

static LaGearIssue la_gear_status(GsObservedGear gear) {
    if (gear.source == GS_GEAR_UNKNOWN) return (LaGearIssue){.status = LA_GEAR_UNKNOWN};
    LaItem weapon = la_item_of(gear.weapon), shield = la_item_of(gear.shield);
    if (weapon.kind == LA_ITEM_UNSUPPORTED) return (LaGearIssue){LA_GEAR_WEAPON_UNSUPPORTED, 0, gear.weapon};
    if (shield.kind == LA_ITEM_UNSUPPORTED) return (LaGearIssue){LA_GEAR_SHIELD_UNSUPPORTED, 0, gear.shield};
    if (weapon.kind == LA_ITEM_MAPPED && get_item_attack_style(weapon.item) == ATTACK_STYLE_RANGED && weapon.item != ITEM_MAGIC_SHORTBOW_I)
        return (LaGearIssue){LA_GEAR_AMMO_UNSUPPORTED, 0, gear.weapon};
    if (gear.source == GS_GEAR_WEAPON_SHIELD_OBSERVED) return (LaGearIssue){.status = LA_GEAR_ARMOR_UNKNOWN};
    for (int slot = 0; slot < GS_ARMOR_SLOTS; slot++)
        if (la_item_of(gear.armor[slot]).kind == LA_ITEM_UNSUPPORTED) return (LaGearIssue){LA_GEAR_ARMOR_UNSUPPORTED, slot, gear.armor[slot]};
    return (LaGearIssue){.status = LA_GEAR_SUPPORTED};
}

static int g_la_worlds = 64, g_la_horizon = 15, g_la_stage = 16, g_la_cache = 1;
static double g_la_prune = 0.03, g_la_belief_hp_shift = 0, g_la_eat_scale_belief = 1;
static const OppModel* g_la_model;
static int g_la_boost = 118;
typedef struct { long calls, cache_hits, rollouts; long chosen[LA_MACROS]; double seconds, worst; double* latency; long latency_count, latency_capacity; } LaStats;
static int g_la_collect_latency = 1;
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
    /* Hidden bag occupancy is not an observation. Reconstruct the whole bag
     * from the declared supply prior, then add sampled carried weapons. */
    int hard = la_between(r, 8, 14), fast = la_between(r, 2, 6), brew = la_between(r, 2, 4);
    for (int i = 0; i < OSRS_INVENTORY_SIZE; i++) {
        int pick = i < hard ? 0 : i < hard + fast ? 1 : i < hard + fast + brew ? 2 : 3;
        if (pick == 3) { him->inventory_cells[i] = osrs_inventory_cell_empty(); continue; }
        uint16_t code = pick == 0 ? riskfight_supply_code(OSRS_CONSUMABLE_ANGLERFISH, 0) : pick == 1 ? riskfight_supply_code(OSRS_CONSUMABLE_KARAMBWAN, 0) :
            riskfight_supply_code(OSRS_CONSUMABLE_BREW, 4);
        him->inventory_cells[i] = osrs_inventory_cell_from_content_code(code);
    }
}

/** One world: the sim as it stands with everything hidden about him replaced by a draw from what the client infers. */
static void la_world(const RiskfightState* base, const Hist* base_hist, const LaBelief* b, int tick, uint32_t* r, RiskfightState* w, Hist* hist) {
    if (g_la_horizon <= 0 || g_la_horizon > 32) {
        fprintf(stderr, "lookahead horizon is outside the supported bound\n");
        abort();
    }
    for (int side = 0; side < 2; side++) {
        OsrsPoisonSource source = base->poison_source[side];
        if (source != OSRS_POISON_OBSERVED_STATE &&
            source != OSRS_POISON_HORIZON_IMMUNITY &&
            source != OSRS_POISON_CLEAR_PRIOR &&
            !(side == 0 && source == OSRS_POISON_CAUSAL_STATE)) {
            fprintf(stderr, "lookahead poison status is not admitted side=%d source=%d\n", side, (int)source);
            abort();
        }
        if (source == OSRS_POISON_HORIZON_IMMUNITY &&
            base->poison[side].immunity_ticks <= g_la_horizon) {
            fprintf(stderr, "lookahead poison immunity does not cover horizon side=%d\n", side);
            abort();
        }
        uint8_t weapon = side == 1 ? la_item_of(b->gear.weapon).item :
            base->env.players[side].equipped[GEAR_SLOT_WEAPON];
        if (weapon == ITEM_ABYSSAL_DAGGER_BH_P_PLUS_PLUS &&
            base->poison[1 - side].immunity_ticks <= g_la_horizon) {
            fprintf(stderr, "lookahead weapon-poison application is unverified side=%d\n", side);
            abort();
        }
    }
    *w = *base;
    Player* him = &w->env.players[1];
    assert(la_gear_status(b->gear).status == LA_GEAR_SUPPORTED);
    him->equipped[GEAR_SLOT_WEAPON] = la_item_of(b->gear.weapon).item;
    him->equipped[GEAR_SLOT_SHIELD] = la_item_of(b->gear.shield).item;
    assert(!item_is_two_handed(him->equipped[GEAR_SLOT_WEAPON]) || him->equipped[GEAR_SLOT_SHIELD] == ITEM_NONE);
    him->equipped[GEAR_SLOT_AMMO] = him->equipped[GEAR_SLOT_WEAPON] == ITEM_MAGIC_SHORTBOW_I ? ITEM_AMETHYST_ARROW : ITEM_NONE;
    for (int slot = 0; slot < GS_ARMOR_SLOTS; slot++)
        him->equipped[WORN_TO_GEAR[GS_ARMOR_KIT_SLOT[slot]]] = la_item_of(b->gear.armor[slot]).item;
    osrs_refresh_player_equipment(him);
    if (b->health && b->health->has_started) {
        if (g_la_belief_hp_shift != 0) { fprintf(stderr, "source-conditioned actor does not permit unsupported HP shifts\n"); abort(); }
        RiskfightActorStats stats;
        int hitpoints;
        source_actor_snapshot(b->health, b->combat_level, b->bar_ratio == 0, r, &stats, &hitpoints);
        riskfight_apply_actor_stats(him, &stats);
        him->current_hitpoints = hitpoints;
    } else {
        RiskfightActorStats stats;
        if (!riskfight_model_actor_stats(b->combat_level, him->base_hitpoints, &stats)) {
            fprintf(stderr, "actor profile contradicts observed combat level %d\n", b->combat_level); abort();
        }
        riskfight_apply_actor_stats(him, &stats);
        int low = imax(1, b->hp_low + (int)g_la_belief_hp_shift);
        int high = imin(imax(low, b->hp_high + (int)g_la_belief_hp_shift), gs_health_cap(him->base_hitpoints));
        if (high < low) { fprintf(stderr, "actor base/health prior have no compatible HP support\n"); abort(); }
        him->current_hitpoints = la_between(r, low, high);
    }
    int defence_boost = imax(osrs_super_combat_boost_amount(him->base_defence), osrs_brew_defence_boost_amount(him->base_defence));
    him->current_defence = imin(him->current_defence, him->base_defence + defence_boost);
    him->current_magic = imin(him->current_magic, him->base_magic + osrs_saturated_heart_magic_boost(him->base_magic));
    him->current_prayer = imin(him->current_prayer, him->base_prayer);
    him->special_energy = b->energy_low + 10 * la_between(r, 0, (b->energy_high - b->energy_low) / 10);
    him->spec_regen_active = him->special_energy < 100;
    him->item_effect_state.special_regen_ticks = la_between(r, 0, 49);
    him->veng_active = b->vengeance == GS_VENGEANCE_UP || ((b->vengeance == GS_VENGEANCE_MAYBE || b->vengeance == GS_VENGEANCE_UNKNOWN) && la_uniform(r, 0, 1) < 0.5);
    him->veng_cooldown = 0;
    w->visible[0].health_bar = b->bar_ratio;
    him->current_attack = actor_profile_boost(g_la_boost, him->base_attack);
    him->current_strength = actor_profile_boost(g_la_boost, him->base_strength);
    int holder = b->pid == GS_PID_UNKNOWN ? (int)(la_next(r) & 1) : (b->pid == GS_PID_LOCAL ? 0 : 1);
    if (b->pid != GS_PID_UNKNOWN && la_uniform(r, 0, 1) < b->flip) holder = 1 - holder;
    if (w->env.pid_holder != holder) {
        uint32_t rank = w->env.priority[0].rank;
        w->env.priority[0].rank = w->env.priority[1].rank;
        w->env.priority[1].rank = rank;
    }
    w->env.pid_holder = holder;
    riskfight_sample_pending(w, r);
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


static Outcome g_la_scratch;

/* Production guards cost no click: keep the motor clock, resume Ready through
 * the human executor once, and distinguish attack registration from damage. */
static double la_press_cost(uint32_t* rng, int first);
static int la_press_tick(int tick, double ms);

static int la_motor_due(LaScript* script, int earliest) {
    double start = (earliest - script->motor_tick - 1) * (double)TICK_MS;
    int first = !script->motor_chain || start > script->motor_ms;
    script->motor_ms = fmax(start, script->motor_ms) +
        la_press_cost(&script->timing_rng, first);
    script->motor_chain = 1;
    return la_press_tick(script->motor_tick, script->motor_ms);
}

/* Bind recipes against observed state, not the inventory at request time. */
static int la_recipe(const LaScript* script, int i, const Player* us, HumanCommand* command) {
    int op = script->op[i];
    for (int c = 0; c < script->candidate_count[i]; c++) {
        int raw = script->candidates[i][c];
        if (op == 9 || op == 10) {
            for (int slot = 0; slot < NUM_GEAR_SLOTS; slot++) {
                ItemIndex item = us->equipped[slot];
                if (item != ITEM_NONE && ITEM_DATABASE[item].item_id == raw) {
                    if (op == 9) return 0;
                    *command = (HumanCommand){.kind = HUMAN_COMMAND_UNEQUIP, .gear_slot = slot};
                    return 1;
                }
            }
        }
    }
    if (op == 10) return 0;
    for (int c = 0; c < script->candidate_count[i]; c++) {
        int raw = script->candidates[i][c];
        OsrsConsumableKind family = OSRS_CONSUMABLE_NONE;
        if (raw < 0) {
            uint16_t code = osrs_inventory_content_code_from_raw_osrs_id(-raw);
            family = osrs_item_content_metadata(code)->consumable_kind;
        }
        for (int slot = 0; slot < OSRS_INVENTORY_SIZE; slot++) {
            const OsrsItemContentMetadata* item = osrs_inventory_cell_metadata(&us->inventory_cells[slot]);
            if ((raw > 0 && item->raw_osrs_id == raw) ||
                (raw < 0 && item->consumable_kind == family)) {
                *command = (HumanCommand){.kind = op == 8 ? HUMAN_COMMAND_INVENTORY_PRIMARY_CLICK :
                    HUMAN_COMMAND_EQUIP_INVENTORY_ITEM, .item_osrs_id = item->raw_osrs_id};
                return 1;
            }
        }
    }
    return 0;
}

static void la_script_step(LaScript* script, RiskfightState* s, HumanInput* hi,
    int now, int* cursor, int* resume, int hit, int damage) {
    if (s->env.episode_over || s->env.players[0].current_hitpoints <= 0 ||
        s->env.players[1].current_hitpoints <= 0) { *cursor = script->n; return; }
    if (!script->attack_tracking) {
        script->attack_after = s->env.tick;
        script->attack_tracking = 1;
    }
    int previous = s->env.tick - 1;
    if (s->visible[1].last_attack_tick >= script->attack_after ||
        (previous >= script->attack_after &&
         s->visible[1].events[previous % RF_HISTORY_TICKS][0] > 0)) script->registered = 1;
    if (now < *resume) return;
    if (script->aligned_active && now > script->aligned_tick) {
        *cursor = script->n;
        return;
    }
    while (*cursor < script->n && script->due[*cursor] <= now) {
        int i = *cursor, op = script->op[i], arg = script->arg[i];
        Player* us = &s->env.players[0];
        /* now is the upcoming processing tick, not the observed GameTick.
         * Alignment costs no click; the following attack pays motor timing. */
        if (op == 11) {
            if (now > arg) { *cursor = script->n; return; }
            if (now < arg || us->attack_timer > 1) return;
            script->aligned_active = 1;
            script->aligned_tick = arg;
        }
        /* VW launch counter certifies the ticket spend; the energy drop may
         * include regeneration on that same tick, so do not demand exactly50. */
        if (op == 12 && (!script->spec_tracking ||
            s->spec_voidwaker[0] <= script->spec_before ||
            us->special_energy >= script->energy_before)) return;
        if (op == 3) {
            /* Observe queued acquisitions/cancellations, not unrelated interface clicks. */
            for (int j = 0; j < hi->commands.count; j++) {
                HumanCommandKind kind = hi->commands.items[j].kind;
                if (kind == HUMAN_COMMAND_ATTACK_NPC || kind == HUMAN_COMMAND_INVENTORY_PRIMARY_CLICK ||
                    kind == HUMAN_COMMAND_EQUIP_INVENTORY_ITEM || kind == HUMAN_COMMAND_UNEQUIP ||
                    kind == HUMAN_COMMAND_STOP || kind == HUMAN_COMMAND_WALK) return;
            }
            if (!osrs_interaction_active(&us->interaction)) {
                if (script->ready_index != i + 1) {
                    script->ready_index = i + 1;
                    script->ready_due = script->motor_active ?
                        la_motor_due(script, now) : la_press_tick(now - 1,
                            la_press_cost(&script->timing_rng, i == 0));
                    script->ready_sent = 0;
                }
                if (script->ready_sent) { *cursor = script->n; return; }
                if (now < script->ready_due) return;
                human_input_queue_command(hi, attack_command());
                script->attack_after = s->env.tick;
                script->registered = 0;
                script->ready_sent = 1;
                return; /* Observe acquisition, including rejection, next tick. */
            }
            if (us->attack_timer > arg) return;
        }
        if (op == 4) {
            if (!script->registered && !hit) {
                if (!osrs_interaction_active(&us->interaction) && !hi->commands.count)
                    *cursor = script->n;
                return;
            }
            if (arg >= 0 && !hit) return;
        }
        if (op < 3 || (op >= 8 && op <= 10)) {
            if (op >= 8 && op <= 10 && script->motor_bound != i + 1 &&
                !la_recipe(script, i, us, &script->command[i])) {
                (*cursor)++;
                continue; /* Established recipe satisfaction/absence skips without a click. */
            }
            if (script->motor_active) {
                if (script->fixed[i]) {
                    script->motor_ms = fmax(script->motor_ms,
                        (script->due[i] - script->motor_tick - 1) * (double)TICK_MS);
                    script->motor_chain = 1;
                } else if (script->motor_bound != i + 1) {
                    script->due[i] = la_motor_due(script, script->due[i]);
                    script->motor_bound = i + 1;
                }
                if (script->due[i] > now) return;
            }
        }
        (*cursor)++;
        if (script->production && (op == 3 || op == 4 || op == 7 || op == 11 || op == 12)) {
            int lag = now - script->due[i] + (op == 7);
            for (int j = i + 1; lag > 0 && j < script->n; j++)
                if (!script->fixed[j]) script->due[j] += lag;
        }
        if (op == 3 || op == 11 || op == 12) continue;
        if (op == 4) {
            if (arg >= 0 && damage < arg) *cursor = script->n;
            continue;
        }
        if (op == 5) {
            if (us->special_energy < arg) {
                while (*cursor < script->n && script->op[*cursor] != 7) (*cursor)++;
            }
            continue;
        }
        if (op == 6) {
            if (us->current_hitpoints >= arg && *cursor < script->n) (*cursor)++;
            continue;
        }
        if (op == 7) {
            script->motor_chain = 0;
            *resume = now + 1;
            return;
        }
        if (op == 1 || op == 2) {
            if (op == 1) {
                script->spec_before = s->spec_voidwaker[0];
                script->energy_before = us->special_energy;
                script->spec_tracking = 1;
                script->attack_after = s->env.tick;
                script->registered = 0;
                hit = 0;
            }
            human_input_queue_command(hi, (HumanCommand){.kind = HUMAN_COMMAND_SPEC_TOGGLE,
                .spec_mode = op == 1 ? HUMAN_SPEC_ENABLE : HUMAN_SPEC_DISARM});
            continue;
        }
        if (script->command[i].kind == HUMAN_COMMAND_ATTACK_NPC) {
            script->aligned_active = 0;
            script->attack_after = s->env.tick;
            script->registered = 0;
            hit = 0;
        }
        human_input_queue_command(hi, script->command[i]);
        if (script->command[i].kind == HUMAN_COMMAND_STOP) {
            script->motor_chain = 0;
            *resume = now + 2;
            if (script->motor_active)
                script->motor_ms = fmax(script->motor_ms,
                    (*resume - script->motor_tick - 1) * (double)TICK_MS);
            return;
        }
    }
}

static LaOutcome la_rollout(Sim* work, const RiskfightState* world, const Hist* hist, int tick, const OppModel* model, const LaScript* plan, uint32_t seed) {
    LaScript running = *plan;
    LaScript* script = &running;
    work->state = *world;
    work->hist = *hist;
    work->tick_base = tick;
    work->outcome = &g_la_scratch;
    memset(&g_la_scratch, 0, sizeof(g_la_scratch));
    work->state.env.pvp_runtime.hit_observer = sim_observe_hit;
    work->state.env.pvp_runtime.hit_observer_context = work;
    work->state.env.pvp_runtime.attack_chance_observer = NULL;
    work->state.env.rng_state = osrs_lowbias32(seed) | 1;
    work->state.script_seed[0] = osrs_lowbias32(seed + 1);
    work->state.script_seed[1] = osrs_lowbias32(seed + 2);
    g_rollout_seed = seed;
    RiskfightState* s = &work->state;
    HumanInput* hi = &work->context.policy_commands[0];
    if (script->native_rules) {
        Arena arena = work->ours.arena;
        work->ours = *script->native_rules;
        work->ours.arena = arena;
        work->ours.lookahead = 0;
        observer_assign(&work->obs, script->native_observer);
        observer_branch(s, &work->obs);
    } else {
        work->ours.burst_pending = 0;
    }
    LaOutcome out = {0};
    out.ate_at = -1;
    int cursor = 0, resume = tick + 1, direct_hit = 0, direct_damage = 0;
    for (int step = 0; step < g_la_horizon && !s->env.episode_over; step++) {
        int actions[RF_HEADS];
        opponent_actions(model, work, step, seed + (uint32_t)step * 2654435761u, actions);
        riskfight_policy_commands(s, 1, actions, &work->context.policy_commands[1]);
        human_input_clear_pending(hi);
        if (script->native_rules) {
            if (step == 0) {
                ours_pop_due(&work->ours, &s->env.players[0], hi, tick + 1);
            } else {
                work->tick_base = tick - script->native_step;
                ours_act(work, &work->ours, hi, script->native_step + step);
                work->tick_base = tick;
            }
        } else {
            la_script_step(script, s, hi, tick + 1 + step, &cursor, &resume, direct_hit, direct_damage);
        }
        for (int i = 0; i < hi->commands.count; i++)
            if (hi->commands.items[i].kind == HUMAN_COMMAND_ATTACK_NPC ||
                (hi->commands.items[i].kind == HUMAN_COMMAND_SPEC_TOGGLE &&
                 hi->commands.items[i].spec_mode == HUMAN_SPEC_ENABLE)) direct_hit = 0;
        int vw = s->spec_voidwaker[1], maul = s->spec_maul[1];
        work->step_damage = work->step_max_hit = work->step_direct = 0;
        work->obs.hit_count = 0;
        riskfight_step_queues(s, &work->context, &hi->commands, &work->context.policy_commands[1].commands);
        if (script->native_rules) observer_step(s, &work->obs, tick + 1 + step);
        int landed_direct = 0;
        for (int i = 0; i < work->obs.hit_count; i++)
            landed_direct |= work->obs.hits[i].source == 0 && work->obs.hits[i].kind == OSRS_HIT_DIRECT;
        if (landed_direct) { direct_hit = 1; direct_damage = work->step_direct; }
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
    observer_clear(&work->obs);
    return out;
}

static double g_la_worth_scale = 1, g_la_w[GS_MOST_TICKETS + 1];
static int g_la_worth_mode = 1, g_la_burst = 1;
static double la_tickets_worth(int energy) { return g_la_worth_scale * g_la_w[imin(GS_MOST_TICKETS, energy / GS_TICKET)]; }

static double la_press_cost(uint32_t* rng, int first) {
    return first ? la_uniform(rng, FIRST_PRESS_MS, FIRST_PRESS_SPAN) : la_uniform(rng, GAP_MS, GAP_SPAN);
}
static int la_press_tick(int tick, double ms) {
    return tick + (ms < CUTOFF_MS ? 1 : 2 + (int)((ms - CUTOFF_MS) / TICK_MS));
}

static void la_press(uint32_t* r, LaScript* script, int tick, const HumanCommand* clicks, int count) {
    double ms = la_press_cost(r, 1);
    for (int i = 0; i < count; i++) {
        if (i) ms += la_press_cost(r, 0);
        assert(script->n < LA_SCRIPT);
        script->due[script->n] = la_press_tick(tick, ms);
        script->command[script->n++] = clicks[i];
    }
}

static void la_time_production(uint32_t* rng, int tick, LaScript* script) {
    script->motor_active = 1;
    script->motor_tick = tick;
    script->motor_ms = 0;
    script->motor_chain = script->motor_bound = 0;
    script->timing_rng = *rng;
}



/* pause_ok is an explicit observed-OFF or declared simulation OFF prior.
 * Missing/UNKNOWN/ON producers leave it false, never silently become OFF. */
typedef struct { int spec_ok, eat_ok, veng_ok, energy, pause_ok, incoming_count, incoming[3], pid; } LaFeasible;

/* First three distinct source-visible windows, never rollout future truth. */
static void la_incoming_window(LaFeasible* f, int decision, int tick) {
    if (tick <= decision || tick - decision > g_la_horizon) return;
    int at = 0;
    while (at < f->incoming_count && f->incoming[at] < tick) at++;
    if ((at < f->incoming_count && f->incoming[at] == tick) || at == 3) return;
    if (f->incoming_count < 3) f->incoming_count++;
    for (int i = f->incoming_count - 1; i > at; i--) f->incoming[i] = f->incoming[i - 1];
    f->incoming[at] = tick;
}

static void la_alignment_windows(const PendingHit* incoming, int count,
    const LaBelief* belief, int tick, LaFeasible* f) {
    f->incoming_count = 0;
    f->pid = belief->pid;
    if (f->pid == GS_PID_UNKNOWN) return;
    for (int i = 0; i < count; i++)
        if (incoming[i].kind == OSRS_HIT_DIRECT)
            la_incoming_window(f, tick, tick + 1 + incoming[i].ticks_until_hit);
    LaItem weapon = la_item_of(belief->gear.weapon);
    if (belief->gear.source == GS_GEAR_COMPOSITION_OBSERVED && belief->timer_seen &&
        weapon.kind == LA_ITEM_MAPPED && get_item_attack_style(weapon.item) == ATTACK_STYLE_MELEE) {
        la_incoming_window(f, tick, belief->timer_first);
        la_incoming_window(f, tick, belief->timer_first + (belief->timer_last - belief->timer_first) / 2);
        la_incoming_window(f, tick, belief->timer_last);
    }
}

static void la_op(LaScript* s, int tick, int op, int arg, HumanCommand command) {
    assert(s->n < LA_SCRIPT);
    int i = s->n++;
    s->due[i] = tick + 1;
    s->op[i] = op;
    s->arg[i] = arg;
    s->command[i] = command;
}

/* Current/carried supported weapon only; never a sampled future inventory. */
static int la_has_voidwaker(const Player* us) {
    if (us->equipped[GEAR_SLOT_WEAPON] == ITEM_VOIDWAKER) return 1;
    if (slot_with_item(us, is_voidwaker) < 0) return 0;
    if (us->equipped[GEAR_SLOT_SHIELD] == ITEM_NONE) return 1;
    for (int i = 0; i < OSRS_INVENTORY_SIZE; i++)
        if (osrs_inventory_cell_is_empty(&us->inventory_cells[i])) return 1;
    return 0;
}

static void la_spec_program(LaScript* s, int tick, int ready, int stop,
    int cast, int burst, uint32_t seed) {
    la_op(s, tick, 5, GS_TICKET, (HumanCommand){0});
    if (stop) la_op(s, tick, 0, 0, (HumanCommand){.kind = HUMAN_COMMAND_STOP});
    if (cast) la_op(s, tick, 0, 0, vengeance_command());
    la_op(s, tick, 9, 0, (HumanCommand){0});
    s->candidate_count[s->n - 1] = 1;
    s->candidates[s->n - 1][0] = ITEM_DATABASE[ITEM_VOIDWAKER].item_id;
    la_op(s, tick, 1, 0, (HumanCommand){0});
    la_op(s, tick, stop ? 11 : 3, stop ? ready : 1, (HumanCommand){0});
    s->fixed[s->n - 1] = stop;
    la_op(s, tick, 0, 0, attack_command());
    if (burst) {
        la_op(s, tick, 12, 0, (HumanCommand){0});
        la_op(s, tick, 5, GS_TICKET, (HumanCommand){0});
        /* Arm after observed spend, before actual cooldown can auto-launch.
         * Ready resumes acquisition once; ENABLE never toggles an armed bar off. */
        la_op(s, tick, 1, 0, (HumanCommand){0});
        la_op(s, tick, 3, 1, (HumanCommand){0});
        la_op(s, tick, 0, 0, attack_command());
    }
    la_time_production(&seed, tick, s);
}

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
    /* Every alternative is an admitted timed command sequence, not a policy
     * secretly rescued by exact opponent HP/energy-dependent meals. */
    for (int m = 0; m < LA_MACROS; m++) scripts[m].production = 1;
    scripts[LA_RULES] = *rules;
    if (scripts[LA_RULES].production) {
        r = saved;
        la_time_production(&r, tick, &scripts[LA_RULES]);
    }
    feasible[LA_RULES] = feasible[LA_NONE] = 1;
    feasible[LA_WAIT_ALIGN] = f->pause_ok && us->attack_timer >= 3;
    if (feasible[LA_WAIT_ALIGN]) {
        LaScript* wait = &scripts[LA_WAIT_ALIGN];
        la_op(wait, tick, 0, 0, (HumanCommand){.kind = HUMAN_COMMAND_STOP});
        la_op(wait, tick, 11, tick + imax(1, us->attack_timer), (HumanCommand){0});
        wait->fixed[wait->n - 1] = 1;
        la_op(wait, tick, 0, 0, attack_command());
        r = saved;
        la_time_production(&r, tick, wait);
    }
    if (f->spec_ok && la_has_voidwaker(us)) {
        feasible[LA_SPEC_NOW] = feasible[LA_SPEC_NEXT] = 1;
        la_spec_program(&scripts[LA_SPEC_NOW], tick, tick + 1, 0, 0, 0, saved);
        la_spec_program(&scripts[LA_SPEC_NEXT], tick, tick + imax(1, us->attack_timer), 0, 0, 0, saved);
        if (f->energy >= 2 * GS_TICKET && g_la_burst) {
            feasible[LA_BURST] = 1;
            la_spec_program(&scripts[LA_BURST], tick, tick + 1, 0, 0, 1, saved);
        }
        if (f->veng_ok) {
            feasible[LA_VENG_SPEC_NOW] = 1;
            la_spec_program(&scripts[LA_VENG_SPEC_NOW], tick, tick + 1, 0, 1, 0, saved);
            if (f->energy >= 2 * GS_TICKET && g_la_burst) {
                feasible[LA_VENG_SPEC_NOW_BURST] = 1;
                la_spec_program(&scripts[LA_VENG_SPEC_NOW_BURST], tick, tick + 1, 0, 1, 1, saved);
            }
            assert(f->incoming_count >= 0 && f->incoming_count <= 3);
            if (f->pause_ok && (f->pid == GS_PID_LOCAL || f->pid == GS_PID_OPPONENT)) {
                for (int window = 0; window < f->incoming_count; window++) {
                    /* First actor can cast before this tick's incoming; second
                     * actor needs the next processing tick. Motor lag still pays. */
                    int ready = f->incoming[window] + (f->pid == GS_PID_OPPONENT);
                    if (ready < tick + 3 || ready > tick + g_la_horizon) continue;
                    int single = LA_WAIT_VENG_SPEC_1 + window;
                    int burst = LA_WAIT_VENG_SPEC_BURST_1 + window;
                    feasible[single] = 1;
                    la_spec_program(&scripts[single], tick, ready, 1, 1, 0, saved);
                    if (f->energy >= 2 * GS_TICKET && g_la_burst) {
                        feasible[burst] = 1;
                        la_spec_program(&scripts[burst], tick, ready, 1, 1, 1, saved);
                    }
                }
            }
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
/** Finite executable one/two-ticket continuation prior; not global policy value. */
static void la_measure_worth(const RiskfightState* base, const Hist* base_hist, const LaBelief* belief, int tick, uint32_t salt, Sim* work) {
    enum { WORLDS = 16 };
    LaScript none = {0};
    g_la_w[0] = 0;
    static RiskfightState world;
    static Hist hist;
    for (int k = 1; k <= GS_MOST_TICKETS; k++) {
        LaScript greedy = {.production = 1};
        if (la_has_voidwaker(&base->env.players[0]))
            la_spec_program(&greedy, tick, tick + 1, 0, 0, k > 1, salt | 1);
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
        him->food_timer, him->potion_timer, him->karambwan_timer, him->current_attack, him->current_strength, w->env.pid_holder, w->visible[0].health_bar, (int)w->script_seed[1]};
    for (size_t i = 0; i < sizeof(parts) / sizeof(*parts); i++) { h ^= (uint64_t)(uint32_t)parts[i]; h *= 1099511628211ull; }
    for (int slot = 0; slot < NUM_GEAR_SLOTS; slot++) { h ^= him->equipped[slot]; h *= 1099511628211ull; }
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
    else for (int k = 0; k <= GS_MOST_TICKETS; k++) g_la_w[k] = GS_CONTINUATION[k];
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
                double continuation = !o.kill && !o.death ? la_tickets_worth(o.energy) : 0;
                score[m].tickets += continuation;
                score[m].value += o.kill * ratio - o.death + continuation * ratio;
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
        .seen_since_eat = sim->seen_since_eat, .seen_vw = seen_vw, .seen_maul = seen_maul, .bar_ratio = sim->state.visible[0].health_bar, .flip = gs_pid_flip_odds(&inf->pid, tick), .gear = inf->c.opponent_gear,
        .combat_level = inf->c.opponent_combat_level, .health = inf->health};
}

static uint64_t la_key(const LaBelief* b, const Player* us, int tick, int energy) {
    uint64_t h = 1469598103934665603ull;
    int parts[] = {us->current_hitpoints, us->attack_timer, us->attack_timer_uncapped, us->has_attack_timer, us->food_timer, us->potion_timer, us->karambwan_timer, us->spec_armed, energy, us->veng_active, us->veng_cooldown,
        b->hp_low, b->hp_high, b->energy_low, b->energy_high, b->vengeance, b->pid, b->timer_seen ? b->timer_first - tick : -1, b->timer_last - tick, la_food_timer(b->food_ready, tick),
        b->seen_since_eat > 9 ? 9 : b->seen_since_eat, b->seen_vw, b->seen_maul, b->gear.source, b->gear.weapon, b->gear.shield, b->combat_level};
    for (size_t i = 0; i < sizeof(parts) / sizeof(*parts); i++) { h ^= (uint64_t)(uint32_t)parts[i]; h *= 1099511628211ull; }
    for (int slot = 0; slot < GS_ARMOR_SLOTS; slot++) { h ^= (uint64_t)(uint32_t)b->gear.armor[slot]; h *= 1099511628211ull; }
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
    LaFeasible f = {0};
    f.energy = gs_spec_energy(&inf->c);
    GsStakes stakes = ours->equal_stakes ? (GsStakes){1, 1} : sim->obs.stakes;
    int slot[3];
    la_food_slots(us, slot);
    GsRebound rebound = ours_rebound(sim, ours, tick, 0);
    f.spec_ok = gs_spec_plannable(inf) && f.energy >= GS_TICKET && ours->last_played_step + 2 < step;
    f.eat_ok = (slot[0] >= 0 || slot[1] >= 0 || slot[2] >= 0) && us->current_hitpoints < GS_ASSUMED_MAX_HP;
    f.veng_ok = rebound.kind == GS_REBOUND_CASTABLE;
    f.pause_ok = 1; /* Explicit synthetic simulation OFF_PRIOR, not recorded OFF. */
    if (!f.spec_ok && !(f.eat_ok && us->current_hitpoints <= 75) && !f.veng_ok) return 0;

    LaBelief belief = la_belief_of(sim, tick);
    int holder = belief.pid == GS_PID_LOCAL ? 0 : 1;
    const RiskfightState* observed = &sim->state;
    const PendingHit* incoming = observed->pending_pid_active ?
        observed->pending_pid[holder][1] : observed->env.players[1].pending_hits;
    int incoming_count = observed->pending_pid_active ?
        observed->pending_pid_count[holder][1] : observed->env.players[1].num_pending_hits;
    la_alignment_windows(incoming, incoming_count, &belief, tick, &f);
    LaGearIssue issue = la_gear_status(belief.gear);
    LaGearStatus gear_status = issue.status;
    if (gear_status != LA_GEAR_SUPPORTED) {
        fprintf(stderr, "lookahead unavailable tick %d reason %s weapon %d shield %d\n", tick, LA_GEAR_STATUS_NAME[gear_status], belief.gear.weapon, belief.gear.shield);
        if (gear_status == LA_GEAR_ARMOR_UNSUPPORTED) fprintf(stderr, "  slot %s raw %d\n", GS_ARMOR_SLOT_NAME[issue.slot], issue.raw);
        return 0;
    }
    uint64_t key = la_key(&belief, us, tick, f.energy);
    int memo = (int)(key % LA_CACHE);
    int cacheable = g_la_cache && !belief.health && !sim->state.pending_pid_active && !sim->state.env.players[0].num_pending_hits &&
        !sim->state.env.players[1].num_pending_hits;
    int cached = cacheable && g_la_memo[memo].used && g_la_memo[memo].key == key;
    g_la.calls++;
    LaScript rules = {0};
    Ours trial = *ours;
    trial.lookahead = 0;
    ours_decide(sim, &trial, step, tick);
    rules.native_rules = &trial;
    rules.native_observer = &sim->obs;
    rules.native_step = step;
    cached = cacheable = 0;
    if (g_la_report) printf("rules baseline: closed-loop C proxy; motor timing historical; auto-retaliate OFF_PRIOR simulation-conditioned, not observed; Java parity is not certified\n");
    for (int i = 0; i < trial.pending_count; i++) {
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
        if (cacheable) {
            g_la_memo[memo].used = 1;
            g_la_memo[memo].key = key;
            g_la_memo[memo].macro = best;
            g_la_memo[memo].kill = predicted;
        }
    }
    g_la.chosen[best]++;
    int spec = best == LA_SPEC_NOW || best == LA_BURST || best == LA_SPEC_NEXT || best >= LA_VENG_SPEC_NOW;
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
    if (g_la_collect_latency) {
        if (g_la.latency_count == g_la.latency_capacity) {
            long capacity = g_la.latency_capacity ? 2 * g_la.latency_capacity : 1 << 16;
            double* latency = realloc(g_la.latency, (size_t)capacity * sizeof(double));
            if (!latency) { fprintf(stderr, "lookahead latency allocation failed\n"); abort(); }
            g_la.latency = latency;
            g_la.latency_capacity = capacity;
        }
        g_la.latency[g_la.latency_count++] = elapsed;
    }
    if (best == LA_RULES) return 0;
    ours->burst_pending = 0;
    ours->last_decision = tick;
    ours->decisions++;
    ours->native_program = scripts[best];
    ours->native_cursor = 0;
    ours->native_resume = tick + 1;
    ours->native_active = 1; /* Even NONE owns this processing phase: no fallback click. */
    if (best == LA_VENG_NOW || best >= LA_VENG_SPEC_NOW) ours->vengeance_clicked = tick + 1;
    if (spec) { ours->last_played_choice = GS_CHOICE_VOIDWAKER; ours->last_played_step = step; }
    return 1;
}
#endif
