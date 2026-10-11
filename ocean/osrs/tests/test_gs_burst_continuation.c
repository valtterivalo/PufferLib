#define main lookahead_server_main
#include "gs_lookahead_server.c"
#undef main

static Sim sim;
static OsrsInventoryCell healing_inventory[OSRS_INVENTORY_SIZE];
static const char* test_case;

static void test_failure(const char* condition, const char* file, int line, const char* function) {
    fprintf(stderr, "%s:%d: %s [%s]: %s; tick=%d energy=%d pending=%d queued=%d first=%d spent=%d target=%d tracked=%d ratio=%d hp=%d..%d vengeance=%d\n",
        file, line, function, test_case, condition, sim.obs.inf.tick, gs_spec_energy(&sim.obs.inf.c),
        sim.ours.burst_pending, sim.ours.pending_count, sim.ours.burst_first, sim.ours.burst_spent,
        sim.ours.burst_target, sim.obs.inf.timed, sim.obs.inf.c.ratio,
        sim.obs.inf.opponent_hitpoints.low, sim.obs.inf.opponent_hitpoints.high, sim.obs.inf.opponent.vengeance.phase);
    exit(2);
}

#undef assert
#define assert(condition) do { if (!(condition)) test_failure(#condition, __FILE__, __LINE__, __func__); } while (0)

static void fixture(void) {
    riskfight_reset((EncounterState*)&sim.state, (EncounterContext*)&sim.context, 7);
    ours_reset(&sim.ours, 7, 1, 1, 0);
    sim.ours.lookahead = 0;
    sim.tick_base = 100;
    sim.state.env.tick = RISKFIGHT_LOAD_TICK;
    for (int a = 0; a < 2; a++) {
        Player* p = &sim.state.env.players[a];
        p->current_hitpoints = a ? 1 : 121;
        p->current_prayer = 99;
        p->special_energy = a ? 0 : 100;
        p->spec_armed = 0;
        p->veng_active = 0;
        p->equipped[GEAR_SLOT_RING] = ITEM_NONE;
        p->equipped[GEAR_SLOT_WEAPON] = ITEM_VOIDWAKER;
        p->equipped[GEAR_SLOT_SHIELD] = ITEM_NONE;
        osrs_refresh_player_equipment(p);
        p->attack_timer = p->attack_timer_uncapped = a ? 100 : 1;
        p->has_attack_timer = 1;
        p->num_pending_hits = 0;
        if (a) memcpy(healing_inventory, p->inventory_cells, sizeof(healing_inventory));
        for (int i = 0; i < OSRS_INVENTORY_SIZE; i++) p->inventory_cells[i] = osrs_inventory_cell_empty();
    }
    Player* us = &sim.state.env.players[0];
    Player* him = &sim.state.env.players[1];
    him->x = us->x + 1; him->y = us->y;
    osrs_interaction_set(&us->interaction, 1);
    osrs_interaction_set(&him->interaction, 0);
    observer_clear(&sim.obs);
    memset(&sim.obs, 0, sizeof(sim.obs));
    gs_infer_init(&sim.obs.inf);
    sim.obs.inf.tracker_current = 1;
    sim.obs.bar_hit_tick = 97;
    sim.obs.bar_ratio = 1;
    observer_sync(&sim.state, &sim.obs, 97);
    gs_on_animation(&sim.obs.inf, 97, 0, attack_animation(us));
    observer_sync(&sim.state, &sim.obs, 100);
    gs_on_game_tick(&sim.obs.inf, 100);
    observer_after_tick(&sim.obs, 100);
    sim.obs.inf.local_spec_tick = INT_MIN;
    sim.obs.inf.opponent.timer = (GsTimerState){1, 200, 200};
    sim.ours.burst_pending = 1;
    sim.ours.burst_started = 101;
    sim.ours.burst_first = INT_MIN;
    sim.ours.burst_spent = 0;
    sim.ours.burst_energy = 100;
    sim.ours.burst_target = sim.obs.inf.timed;
    sim.ours.burst_bar = 0;
}

static void advance_food_clocks(int tick) {
    int previous = sim.state.env.tick - RISKFIGHT_LOAD_TICK + sim.tick_base;
    assert(tick >= previous);
    int elapsed = tick - previous;
    if (elapsed) {
        Player* him = &sim.state.env.players[1];
        him->food_timer = imax(0, him->food_timer - elapsed);
        him->potion_timer = imax(0, him->potion_timer - elapsed);
        him->karambwan_timer = imax(0, him->karambwan_timer - elapsed);
        him->ate_food_this_tick = him->ate_karambwan_this_tick = him->ate_brew_this_tick = 0;
    }
    sim.state.env.tick = RISKFIGHT_LOAD_TICK + tick - sim.tick_base;
}

static void decide(int tick, int energy) {
    assert(tick >= sim.obs.inf.tick);
    advance_food_clocks(tick);
    sim.state.env.players[0].special_energy = energy;
    observer_sync(&sim.state, &sim.obs, tick);
    if (tick > sim.obs.inf.tick) gs_on_game_tick(&sim.obs.inf, tick);
    observer_after_tick(&sim.obs, tick);
    ours_decide(&sim, &sim.ours, tick - sim.tick_base, tick);
}

static void launch(int tick) {
    assert(tick >= sim.obs.inf.tick);
    observer_sync(&sim.state, &sim.obs, tick);
    gs_on_animation(&sim.obs.inf, tick, 0, ANIM_VW_SPEC_ID);
    sim.state.env.players[0].attack_timer = sim.state.env.players[0].attack_timer_uncapped = gs_base_speed(GS_ITEM_VOIDWAKER);
}

static void health_bar(int tick) {
    const Player* him = &sim.state.env.players[1];
    sim.obs.bar_hit_tick = tick;
    sim.obs.bar_ratio = osrs_health_bar_ratio(him->current_hitpoints, him->base_hitpoints, SCALE_BAR);
}

static int enables(const HumanCommandQueue* commands) {
    int count = 0;
    for (int i = 0; i < commands->count; i++)
        count += commands->items[i].kind == HUMAN_COMMAND_SPEC_TOGGLE
            && commands->items[i].spec_mode == HUMAN_SPEC_ENABLE;
    return count;
}

static void confirm(int energy_first) {
    fixture();
    if (!energy_first) launch(101);
    decide(101, energy_first ? 50 : 100);
    assert(sim.ours.burst_pending == 1 && sim.ours.pending_count == 0);
    if (energy_first) launch(101);
    decide(102, 50);
    assert(sim.ours.burst_first == 101 && sim.ours.burst_spent);
    decide(103, 60);
    assert(sim.ours.burst_pending == 1 && sim.ours.pending_count == 0);
}

static void observed_healing_meal(int tick, int hard_slot, int fast_slot, int fresh_bar) {
    advance_food_clocks(tick);
    Player* him = &sim.state.env.players[1];
    OsrsInventoryUseState* use = &sim.state.inventory_use[1];
    const OsrsTeleportState* teleport = &sim.state.env.pvp_runtime.teleport[1];
    assert(osrs_player_use_inventory(him, use, teleport, hard_slot, sim.state.env.tick) == OSRS_INVENTORY_USE_CONSUMED);
    assert(osrs_player_use_inventory(him, use, teleport, 7, sim.state.env.tick) == OSRS_INVENTORY_USE_CONSUMED);
    if (fast_slot >= 0)
        assert(osrs_player_use_inventory(him, use, teleport, fast_slot, sim.state.env.tick) == OSRS_INVENTORY_USE_CONSUMED);
    if (fresh_bar) {
        sim.obs.hits[0] = (ObservedHit){.source = 0, .target = 1, .kind = OSRS_HIT_DIRECT, .damage = 0};
        sim.obs.hit_count = 1;
    }
    observer_step(&sim.state, &sim.obs, tick);
}

static void confirm_healing(void) {
    fixture();
    memcpy(sim.state.env.players[1].inventory_cells, healing_inventory, sizeof(healing_inventory));
    launch(101);
    observed_healing_meal(101, 9, 3, 0);
    decide(101, 100);
    assert(sim.ours.burst_pending == 1 && sim.ours.pending_count == 0);
    decide(102, 50);
    assert(sim.ours.burst_first == 101 && sim.ours.burst_spent);
    decide(103, 60);
    assert(sim.ours.burst_pending == 1 && sim.ours.pending_count == 0);
}

static void test_orderings_and_no_duplicate(void) {
    for (int order = 0; order < 2; order++) {
        test_case = order ? "energy before animation" : "animation before energy";
        confirm(order);
        decide(104, 60);
        assert(sim.ours.burst_pending == 2 && sim.ours.pending_count == 2);
        decide(104, 60);
        assert(sim.ours.pending_count == 2);
        HumanInput hi = {0};
        ours_pop_due(&sim.ours, &sim.state.env.players[0], &hi, 105);
        assert(hi.commands.count == 2 && enables(&hi.commands) == 1);
        launch(105);
        decide(105, 10);
        assert(!sim.ours.burst_pending);
        human_input_clear_pending(&hi);
        ours_pop_due(&sim.ours, &sim.state.env.players[0], &hi, 106);
        assert(enables(&hi.commands) == 0);
        decide(106, 10);
        assert(enables(&hi.commands) == 0 && sim.state.env.players[0].special_energy == 10);
        assert(!sim.ours.burst_pending);
        free(hi.commands.items);
    }
}

static void test_delayed_first_launch(void) {
    for (int energy_first = 0; energy_first < 2; energy_first++) {
        test_case = energy_first ? "delayed spend before animation" : "delayed animation before spend";
        fixture();
        decide(103, 100);
        assert(sim.ours.burst_pending == 1 && sim.ours.burst_first == INT_MIN);
        if (!energy_first) launch(105);
        health_bar(105);
        decide(105, energy_first ? 50 : 100);
        assert(sim.ours.burst_pending == 1 && sim.ours.pending_count == 0);
        if (energy_first) launch(105);
        decide(106, 50);
        assert(sim.ours.burst_first == 105 && sim.ours.burst_spent);
        decide(107, 60);
        assert(sim.ours.burst_pending == 1 && sim.ours.pending_count == 0);
        decide(108, 60);
        assert(sim.ours.burst_pending == 2 && sim.ours.pending_count == 2);
        decide(108, 60);
        assert(sim.ours.pending_count == 2);
        HumanInput hi = {0};
        ours_pop_due(&sim.ours, &sim.state.env.players[0], &hi, 109);
        assert(hi.commands.count == 2 && enables(&hi.commands) == 1);
        free(hi.commands.items);
    }
}

static void assert_cancelled(void) {
    assert(!sim.ours.burst_pending);
    HumanInput hi = {0};
    ours_pop_due(&sim.ours, &sim.state.env.players[0], &hi, 105);
    for (int i = 0; i < hi.commands.count; i++) assert(hi.commands.items[i].kind != HUMAN_COMMAND_ATTACK_NPC);
    assert(enables(&hi.commands) == 0);
    human_input_clear_pending(&hi);
    ours_pop_due(&sim.ours, &sim.state.env.players[0], &hi, 106);
    for (int i = 0; i < hi.commands.count; i++) assert(hi.commands.items[i].kind != HUMAN_COMMAND_ATTACK_NPC);
    assert(enables(&hi.commands) == 0);
    free(hi.commands.items);
}

static void test_cancellation(void) {
    for (int queued = 0; queued < 2; queued++) {
        test_case = queued ? "healed target after queue" : "healed target before queue";
        confirm_healing();
        if (queued) {
            decide(104, 60);
            assert(sim.ours.burst_pending == 2);
        }
        observed_healing_meal(104, 10, -1, 1);
        assert(sim.obs.inf.opponent_hitpoints.low <= sim.state.env.players[1].current_hitpoints
            && sim.obs.inf.opponent_hitpoints.high >= sim.state.env.players[1].current_hitpoints);
        decide(104, 60);
        assert_cancelled();
    }
    test_case = "decrease after regeneration";
    confirm(0);
    decide(104, 50); /* Conflicting decrease after confirmed regeneration. */
    assert_cancelled();
    test_case = "insufficient energy";
    confirm(1);
    decide(104, 40);
    assert_cancelled();
    test_case = "spend without first animation";
    fixture();
    decide(103, 50);
    decide(105, 50);
    assert_cancelled();
    test_case = "lost tracking after queue";
    confirm(0);
    decide(104, 60);
    sim.obs.inf.tracker_current = -1;
    gs_sync_opponent_timer(&sim.obs.inf, 104);
    ours_decide(&sim, &sim.ours, 104 - sim.tick_base, 104);
    assert_cancelled();
    test_case = "invalid bar after queue";
    confirm(1);
    decide(104, 60);
    sim.state.env.players[1].current_hitpoints = 0;
    health_bar(104);
    decide(104, 60);
    assert_cancelled();
    test_case = "unexpected additional launch";
    confirm(0);
    launch(103);
    decide(103, 60); /* Unexpected additional launch consumes the reservation. */
    assert_cancelled();
}

static void test_initial_commands_once(void) {
    fixture();
    test_case = "initial enable once";
    HumanCommand initial[] = {{.kind = HUMAN_COMMAND_SPEC_TOGGLE, .spec_mode = HUMAN_SPEC_ENABLE}, attack_command()};
    ours_run(&sim.ours, 100, initial, 2);
    decide(100, 100);
    assert(sim.ours.pending_count == 2);
    HumanInput hi = {0};
    ours_pop_due(&sim.ours, &sim.state.env.players[0], &hi, 101);
    assert(hi.commands.count == 2 && enables(&hi.commands) == 1);
    decide(101, 100);
    assert(sim.ours.pending_count == 0);
    free(hi.commands.items);
}

static void test_food_cancels_queued_continuation(void) {
    test_case = "meal preempts queued continuation";
    confirm_healing();
    decide(104, 60);
    assert(sim.ours.burst_pending == 2);
    sim.state.env.players[0].current_hitpoints = 1;
    /* Keep a following meal available: exhausting the last food uses AutoEat's terminal danger horizon. */
    for (int slot = 0; slot < 2; slot++)
        sim.state.env.players[0].inventory_cells[slot] = osrs_inventory_cell_from_content_code(raw_id_to_content(13441));
    observed_healing_meal(104, 10, -1, 1);
    sim.obs.inf.pid.owner = GS_PID_LOCAL;
    sim.obs.inf.pid.observed_tick = 104;
    observer_sync(&sim.state, &sim.obs, 104);
    sim.state.env.players[1].attack_style_this_tick = ATTACK_STYLE_MELEE;
    gs_on_animation(&sim.obs.inf, 104, 1, attack_animation(&sim.state.env.players[1]));
    sim.ours.ideal_timing = 0;
    decide(104, 60);
    assert(!sim.ours.burst_pending);
    HumanInput hi = {0};
    int food = 0, resumed = 0, disarmed = 0;
    int through = sim.ours.busy_until;
    for (int due = 105; due <= through + 1; due++) {
        human_input_clear_pending(&hi);
        ours_pop_due(&sim.ours, &sim.state.env.players[0], &hi, due);
        assert(enables(&hi.commands) == 0);
        for (int i = 0; i < hi.commands.count; i++) {
            const HumanCommand* c = &hi.commands.items[i];
            if (c->kind == HUMAN_COMMAND_SPEC_TOGGLE && c->spec_mode == HUMAN_SPEC_DISARM) {
                assert(food == 0 && resumed == 0);
                disarmed++;
            } else if (c->kind == HUMAN_COMMAND_INVENTORY_PRIMARY_CLICK && c->inventory_slot == 0) {
                assert(disarmed == 1 && resumed == 0);
                food++;
            } else if (c->kind == HUMAN_COMMAND_ATTACK_NPC) {
                assert(food == 1 && c->npc_slot == 1);
                resumed++;
            }
        }
    }
    assert(food == 1 && disarmed == 1 && resumed == 1);
    assert(sim.ours.pending_count == 0);
    free(hi.commands.items);
}

static void test_reflection_cancels_continuation(void) {
    for (int order = 0; order < 2; order++) for (int queued = 0; queued < 2; queued++) {
        test_case = queued ? "lethal reflection after queue" : "lethal reflection before queue";
        confirm(order);
        if (queued) {
            decide(104, 60);
            assert(sim.ours.burst_pending == 2);
        }
        sim.state.env.players[0].current_hitpoints = 1;
        sim.state.env.players[1].veng_active = 1;
        gs_on_graphic(&sim.obs.inf, 104, 1);
        decide(104, 60);
        assert_cancelled();
        decide(105, 60);
        assert(!sim.ours.burst_pending);
        HumanInput hi = {0};
        ours_pop_due(&sim.ours, &sim.state.env.players[0], &hi, 106);
        assert(enables(&hi.commands) == 0);
        free(hi.commands.items);
    }
}

int main(void) {
    observer_register_items();
    sim_init(&sim);
    test_orderings_and_no_duplicate();
    test_delayed_first_launch();
    test_cancellation();
    test_initial_commands_once();
    test_food_cancels_queued_continuation();
    test_reflection_cancels_continuation();
    riskfight_destroy_context((EncounterContext*)&sim.context);
    puts("Native burst confirmation and cancellation contracts passed");
    return 0;
}
