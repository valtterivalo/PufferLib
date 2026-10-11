#define main lookahead_server_main
#include "gs_lookahead_server.c"
#undef main

static Sim sim;
static RiskfightState world, branch;

static void reset_plan_state(void) {
    riskfight_reset((EncounterState*)&sim.state, (EncounterContext*)&sim.context, 7);
    for (int a = 0; a < 2; a++) {
        Player* p = &sim.state.env.players[a];
        p->current_hitpoints = 99;
        p->current_prayer = 99;
        p->veng_active = 0;
        p->equipped[GEAR_SLOT_RING] = ITEM_NONE;
        p->equipped[GEAR_SLOT_WEAPON] = ITEM_VOIDWAKER;
        p->equipped[GEAR_SLOT_SHIELD] = ITEM_NONE;
        osrs_refresh_player_equipment(p);
        osrs_interaction_clear(&p->interaction);
        p->attack_timer = p->attack_timer_uncapped = 10;
        p->has_attack_timer = 1;
        p->num_pending_hits = 0;
    }
}

static void apply_script(LaScript* script, int now, int* cursor, int* resume, int hit, int damage) {
    HumanInput* hi = &sim.context.policy_commands[0];
    human_input_clear_pending(hi);
    la_script_step(script, &sim.state, hi, now, cursor, resume, hit, damage);
    HumanCommandQueue empty = {0};
    riskfight_step_queues(&sim.state, &sim.context, &hi->commands, &empty);
}

static void test_commands_and_barriers(void) {
    reset_plan_state();
    Player* us = &sim.state.env.players[0];
    us->spec_armed = 1;
    LaScript script = {.production = 1, .n = 3, .due = {101, 101, 101}, .op = {2, 1, 1}};
    int cursor = 0, resume = 101;
    apply_script(&script, 101, &cursor, &resume, 0, 0);
    assert(us->spec_armed && cursor == 3); /* disarm, enable, idempotent enable */
    osrs_interaction_set(&us->interaction, 1); /* Existing engagement: Ready needs no acquisition click. */
    sim.state.env.players[1].x = us->x + 10;
    sim.state.env.players[1].y = us->y;
    script = (LaScript){.production = 1, .n = 5, .due = {102, 102, 102, 102, 102},
        .op = {3, 1, 7, 5, 2}, .arg = {0, 0, 0, 50, 0}};
    cursor = 0; resume = 102;
    apply_script(&script, 102, &cursor, &resume, 0, 0);
    assert(cursor == 0);
    us->attack_timer = -1;
    apply_script(&script, 103, &cursor, &resume, 0, 0);
    assert(cursor == 3 && resume == 104 && us->spec_armed);
    us->special_energy = 0;
    apply_script(&script, 104, &cursor, &resume, 0, 0);
    assert(cursor == 5 && us->spec_armed); /* energy gate skipped disarm */
    script = (LaScript){.production = 1, .n = 2, .due = {105, 105}, .op = {4, 2}, .arg = {30, 0}, .fixed = {1, 1}};
    cursor = 0; resume = 105;
    apply_script(&script, 105, &cursor, &resume, 0, 0);
    assert(cursor == 0);
    apply_script(&script, 106, &cursor, &resume, 1, 20);
    assert(cursor == 2 && us->spec_armed); /* low first hit aborts continuation */
    cursor = 0; resume = 107;
    apply_script(&script, 107, &cursor, &resume, 1, 35);
    assert(!us->spec_armed);
}

static void test_identity_after_inventory_mutation(void) {
    reset_plan_state();
    Player* us = &sim.state.env.players[0];
    for (int i = 0; i < OSRS_INVENTORY_SIZE; i++) us->inventory_cells[i] = osrs_inventory_cell_empty();
    us->inventory_cells[0] = osrs_inventory_cell_from_item(ITEM_DRAGON_SCIMITAR);
    us->inventory_cells[1] = osrs_inventory_cell_from_content_code(raw_id_to_content(3024));
    LaScript script = {.production = 1, .n = 4, .due = {101, 101, 101, 101}};
    script.command[0] = (HumanCommand){.kind = HUMAN_COMMAND_EQUIP_INVENTORY_ITEM, .item_osrs_id = ITEM_DATABASE[ITEM_DRAGON_SCIMITAR].item_id};
    script.command[1] = (HumanCommand){.kind = HUMAN_COMMAND_EQUIP_INVENTORY_ITEM, .item_osrs_id = ITEM_DATABASE[ITEM_VOIDWAKER].item_id};
    script.command[2] = (HumanCommand){.kind = HUMAN_COMMAND_INVENTORY_PRIMARY_CLICK, .item_osrs_id = 3024};
    script.command[3] = attack_command();
    int cursor = 0, resume = 101;
    apply_script(&script, 101, &cursor, &resume, 0, 0);
    assert(us->equipped[GEAR_SLOT_WEAPON] == ITEM_VOIDWAKER);
    assert(osrs_inventory_cell_metadata(&us->inventory_cells[1])->raw_osrs_id == 3026);
    assert(osrs_interaction_active(&us->interaction));
}

static void test_pending_before_actions_and_common_world(void) {
    reset_plan_state();
    Player* us = &sim.state.env.players[0];
    us->current_hitpoints = 10;
    us->inventory_cells[0] = osrs_inventory_cell_from_content_code(raw_id_to_content(13441));
    sim.state.env.players[1].num_pending_hits = 1;
    sim.state.env.players[1].pending_hits[0] = (PendingHit){.damage = 20,
        .hit_success = 1, .attack_type = ATTACK_STYLE_MELEE};
    sim.state.env.pending_hit_metadata[1][0] = (PendingHitMetadata){.damage_mode = 1};
    LaScript meal = {.production = 1, .n = 2, .due = {102, 102}};
    meal.command[0] = click_command(0);
    meal.command[1] = attack_command();
    int cursor = 0, resume = 101;
    apply_script(&meal, 101, &cursor, &resume, 0, 0);
    assert(us->current_hitpoints == 0);
    assert(us->inventory_cells[0].content_code == raw_id_to_content(13441));

    reset_plan_state();
    sim.state.env.players[1].num_pending_hits = 1;
    sim.state.env.players[1].pending_hits[0] = (PendingHit){.ticks_until_hit = 1,
        .attack_type = ATTACK_STYLE_MELEE};
    sim.state.env.pending_hit_metadata[1][0] = (PendingHitMetadata){.damage_mode = 2,
        .damage_min = 20, .damage_max = 40, .accuracy_bp = 10000};
    LaBelief belief = {.hp_low = 99, .hp_high = 99, .energy_low = 0, .energy_high = 0,
        .pid = GS_PID_LOCAL, .gear = {.source = GS_GEAR_COMPOSITION_OBSERVED,
            .weapon = ITEM_DATABASE[ITEM_VOIDWAKER].item_id, .shield = -1}};
    for (int i = 0; i < GS_ARMOR_SLOTS; i++) belief.gear.armor[i] = -1;
    uint32_t rng = 99;
    Hist hist;
    la_world(&sim.state, &sim.hist, &belief, 100, &rng, &world, &hist);
    assert(world.env.pending_hit_metadata[1][0].damage_mode == 1);
    branch = world;
    uint32_t after = rng;
    riskfight_sample_pending(&branch, &rng);
    assert(rng == after);
    assert(!memcmp(world.env.players[1].pending_hits, branch.env.players[1].pending_hits, sizeof(world.env.players[1].pending_hits)));
    HumanCommandQueue empty = {0};
    for (int i = 0; i < 2; i++) {
        riskfight_step_queues(&world, &sim.context, &empty, &empty);
        riskfight_step_queues(&branch, &sim.context, &empty, &empty);
    }
    assert(world.env.players[0].current_hitpoints == branch.env.players[0].current_hitpoints);
    assert(world.env.players[0].current_hitpoints >= 59 && world.env.players[0].current_hitpoints <= 79);
}

static void test_pending_metadata_compact_and_reuse(void) {
    reset_plan_state();
    OsrsEnv* env = &sim.state.env;
    Player* source = &env->players[1];
    Player* target = &env->players[0];
    target->prayer = PRAYER_PROTECT_MELEE;
    source->prayer = PRAYER_SMITE;
    source->num_pending_hits = 2;
    source->pending_hits[0] = (PendingHit){.damage = 20, .hit_success = 1,
        .attack_type = ATTACK_STYLE_MELEE, .defender_prayer_at_attack = PRAYER_PROTECT_MELEE};
    source->pending_hits[1] = (PendingHit){.ticks_until_hit = 2,
        .attack_type = ATTACK_STYLE_MELEE, .defender_prayer_at_attack = PRAYER_PROTECT_MELEE};
    env->pending_hit_metadata[1][0] = (PendingHitMetadata){.damage_mode = 1, .smite_snapshot = 2};
    env->pending_hit_metadata[1][1] = (PendingHitMetadata){.damage_mode = 2,
        .damage_min = 20, .damage_max = 20, .accuracy_bp = 10000, .smite_snapshot = 1};
    pvp_process_incoming_hits(env, 0);
    assert(target->current_hitpoints == 79 && target->current_prayer == 94);
    assert(source->num_pending_hits == 1 && source->pending_hits[0].ticks_until_hit == 1);
    assert(env->pending_hit_metadata[1][0].damage_mode == 2);
    assert(env->pending_hit_metadata[1][0].damage_min == 20 &&
        env->pending_hit_metadata[1][0].damage_max == 20 &&
        env->pending_hit_metadata[1][0].accuracy_bp == 10000 &&
        env->pending_hit_metadata[1][0].smite_snapshot == 1);
    queue_hit(env, 1, 0, source, target, 20, ATTACK_STYLE_MELEE, 2, 0, 1, 0, 0, 0, 0, 0);
    PendingHitMetadata raw = {0};
    assert(!memcmp(&env->pending_hit_metadata[1][1], &raw, sizeof(raw)));
    uint32_t rng = 99;
    riskfight_sample_pending(&sim.state, &rng);
    assert(env->pending_hit_metadata[1][0].damage_mode == 1);
    assert(source->pending_hits[0].damage == 20);
    pvp_process_incoming_hits(env, 0);
    pvp_process_incoming_hits(env, 0);
    assert(target->current_hitpoints == 59 && target->current_prayer == 94);
    assert(source->num_pending_hits == 1 && source->pending_hits[0].damage == 20);
    assert(!memcmp(&env->pending_hit_metadata[1][0], &raw, sizeof(raw)));
    pvp_process_incoming_hits(env, 0);
    assert(target->current_hitpoints == 47 && target->current_prayer == 91);
    assert(source->num_pending_hits == 0);
    assert(!memcmp(&env->pending_hit_metadata[1][0], &raw, sizeof(raw)));
}

static void test_shared_click_timing(void) {
    HumanCommand clicks[] = {click_command(0), click_command(1), attack_command()};
    LaScript macro = {0}, production = {.production = 1, .n = 3, .due = {101, 101, 101}};
    memcpy(production.command, clicks, sizeof(clicks));
    uint32_t a = 1234, b = a;
    la_press(&a, &macro, 100, clicks, 3);
    la_time_production(&b, 100, &production);
    reset_plan_state();
    int cursor = 0, resume = 101;
    for (int now = 101; cursor < production.n; now++)
        apply_script(&production, now, &cursor, &resume, 0, 0);
    assert(a == production.timing_rng && !memcmp(macro.due, production.due, 3 * sizeof(int)));
    production.fixed[0] = production.fixed[1] = production.fixed[2] = 1;
    int saved[3];
    memcpy(saved, production.due, sizeof(saved));
    uint32_t seed = b;
    la_time_production(&b, 100, &production);
    assert(b == seed && !memcmp(saved, production.due, sizeof(saved)));
}

static void test_conditional_two_ticket_plan(void) {
    reset_plan_state();
    Player* us = &sim.state.env.players[0];
    Player* him = &sim.state.env.players[1];
    us->special_energy = 100;
    us->spec_armed = 0;
    us->attack_timer = us->attack_timer_uncapped = -1;
    him->attack_timer = him->attack_timer_uncapped = 100;
    him->current_hitpoints = 121;
    him->x = us->x + 1;
    him->y = us->y;
    LaScript burst = {.production = 1, .n = 8,
        .due = {101, 101, 101, 101, 101, 101, 101, 101},
        .op = {1, 0, 7, 3, 4, 5, 1, 0},
        .arg = {0, 0, 0, 0, -1, 50, 0, 0}};
    burst.command[1] = burst.command[7] = attack_command();
    int cursor = 0, resume = 101, hit = 0, damage = 0, first_tick = -1, second_tick = -1;
    for (int now = 101; now < 112 && !sim.state.env.episode_over; now++) {
        int before = sim.state.spec_voidwaker[0];
        apply_script(&burst, now, &cursor, &resume, hit, damage);
        if (sim.state.spec_voidwaker[0] > before) {
            if (first_tick < 0) first_tick = now;
            else second_tick = now;
        }
        if (him->hit_landed_this_tick) { hit = 1; damage = him->hit_damage; }
        if (second_tick >= 0) break;
    }
    assert(sim.state.spec_voidwaker[0] == 2 && us->special_energy == 0);
    assert(second_tick - first_tick >= 4);
}

static void test_strict_hp_equality(void) {
    reset_plan_state();
    Player* us = &sim.state.env.players[0];
    us->current_hitpoints = 60;
    us->inventory_cells[0] = osrs_inventory_cell_from_content_code(raw_id_to_content(13441));
    LaScript script = {.production = 1, .n = 3, .due = {101, 101, 101},
        .op = {6, 0, 1}, .arg = {60, 0, 0}};
    script.command[1] = click_command(0);
    int cursor = 0, resume = 101;
    apply_script(&script, 101, &cursor, &resume, 0, 0);
    assert(cursor == 3 && us->spec_armed); /* Only the meal, not the following step, is skipped. */
    assert(us->current_hitpoints == 60 && us->food_timer == 0);
    assert(us->attack_timer == 9 && us->inventory_cells[0].content_code == raw_id_to_content(13441));
}

static void test_conditioned_meal_clock(void) {
    HumanCommand clicks[] = {click_command(0), click_command(1), click_command(2)};
    LaScript macro = {0}, plan;
    uint32_t seed;
    for (seed = 1; seed < 10000; seed++) {
        macro = (LaScript){0};
        plan = (LaScript){.production = 1, .n = 6, .due = {101, 101, 101, 101, 101, 101},
            .op = {6, 0, 5, 0, 6, 0}, .arg = {121, 0, 50, 0, 121, 0}};
        plan.command[1] = clicks[0]; plan.command[3] = clicks[1]; plan.command[5] = clicks[2];
        uint32_t a = seed, b = seed;
        la_press(&a, &macro, 100, clicks, 3);
        la_time_production(&b, 100, &plan);
        /* Guards are evaluated by the consumer before any motor draw. */
        if (macro.due[2] > macro.due[0]) break;
    }
    assert(seed < 10000);
    reset_plan_state();
    Player* us = &sim.state.env.players[0];
    us->current_hitpoints = 1;
    us->special_energy = 100;
    us->inventory_cells[0] = osrs_inventory_cell_from_content_code(raw_id_to_content(13441));
    us->inventory_cells[1] = osrs_inventory_cell_from_content_code(raw_id_to_content(6685));
    us->inventory_cells[2] = osrs_inventory_cell_from_content_code(raw_id_to_content(3144));
    int cursor = 0, resume = 101, meals = 0;
    for (int now = 101; now <= macro.due[2]; now++) {
        apply_script(&plan, now, &cursor, &resume, 0, 0);
        int expected = 0;
        for (int i = 0; i < 3; i++) expected += macro.due[i] <= now;
        meals = osrs_inventory_cell_is_empty(&us->inventory_cells[0]) +
            (osrs_inventory_cell_metadata(&us->inventory_cells[1])->raw_osrs_id == 6687) +
            osrs_inventory_cell_is_empty(&us->inventory_cells[2]);
        assert(meals == expected);
        assert(us->attack_timer == 10 - (now - 100) +
            (macro.due[0] <= now ? 3 : 0) + (macro.due[2] <= now ? 2 : 0));
    }
    assert(cursor == 6 && meals == 3 && us->food_timer > 0 && us->karambwan_timer > 0 && us->potion_timer > 0);
    assert(!memcmp(&plan.due[1], &macro.due[0], sizeof(int)));
    assert(plan.due[3] == macro.due[1] && plan.due[5] == macro.due[2]);
}

static void test_skipped_guards_at_cutoff(void) {
    uint32_t seed;
    LaScript single, charged;
    HumanCommand clicks[] = {click_command(0), attack_command()};
    for (seed = 1; seed < 100000; seed++) {
        single = (LaScript){0}; charged = (LaScript){0};
        uint32_t a = seed, b = seed;
        la_press(&a, &single, 100, &clicks[1], 1);
        la_press(&b, &charged, 100, clicks, 2);
        if (single.due[0] == 101 && charged.due[1] == 102) break;
    }
    assert(seed < 100000);
    for (int energy_group = 0; energy_group < 2; energy_group++) {
        reset_plan_state();
        Player* us = &sim.state.env.players[0];
        us->current_hitpoints = 60;
        us->special_energy = 0;
        us->inventory_cells[0] = osrs_inventory_cell_from_content_code(raw_id_to_content(13441));
        LaScript plan = {.production = 1, .n = energy_group ? 7 : 3,
            .due = {101, 101, 101, 101, 101, 101, 101},
            .op = {6, 0, 0}, .arg = {60}};
        plan.command[1] = clicks[0];
        plan.command[2] = clicks[1];
        int attack_index = 2;
        if (energy_group) {
            plan.op[0] = 5; plan.arg[0] = 50;
            plan.op[2] = 1; plan.op[3] = 7;
            plan.op[4] = 6; plan.arg[4] = 60;
            plan.command[5] = clicks[0];
            plan.command[6] = clicks[1];
            attack_index = 6;
        }
        uint32_t rng = seed;
        la_time_production(&rng, 100, &plan);
        int cursor = 0, resume = 101;
        apply_script(&plan, 101, &cursor, &resume, 0, 0);
        if (energy_group) {
            assert(!osrs_interaction_active(&us->interaction));
            apply_script(&plan, 102, &cursor, &resume, 0, 0);
        }
        assert(cursor == plan.n && osrs_interaction_active(&us->interaction));
        assert(plan.due[attack_index] == single.due[0] + energy_group);
        assert(us->inventory_cells[0].content_code == raw_id_to_content(13441));
        uint32_t expected = seed;
        la_press_cost(&expected, 1);
        assert(plan.timing_rng == expected); /* Skipped tails consumed no RNG or click time. */
    }
}

static void test_dynamic_recipe_transitions(void) {
    reset_plan_state();
    Player* us = &sim.state.env.players[0];
    for (int i = 0; i < OSRS_INVENTORY_SIZE; i++) us->inventory_cells[i] = osrs_inventory_cell_empty();
    us->inventory_cells[0] = osrs_inventory_cell_from_content_code(raw_id_to_content(3024));
    us->inventory_cells[1] = osrs_inventory_cell_from_item(ITEM_DRAGON_SCIMITAR);
    LaScript plan = {.production = 1, .n = 7,
        .due = {101, 101, 101, 101, 101, 101, 101},
        .op = {8, 7, 8, 9, 7, 10, 8},
        .candidate_count = {1, 0, 1, 1, 0, 1, 1}};
    plan.candidates[0][0] = plan.candidates[2][0] = -3024;
    plan.candidates[3][0] = plan.candidates[5][0] = ITEM_DATABASE[ITEM_DRAGON_SCIMITAR].item_id;
    plan.candidates[6][0] = 3024; /* Exact four-dose recipe must not match the remaining doses. */
    int cursor = 0, resume = 101;
    apply_script(&plan, 101, &cursor, &resume, 0, 0);
    assert(osrs_inventory_cell_metadata(&us->inventory_cells[0])->raw_osrs_id == 3026);
    us->potion_timer = 0;
    apply_script(&plan, 102, &cursor, &resume, 0, 0);
    assert(osrs_inventory_cell_metadata(&us->inventory_cells[0])->raw_osrs_id == 3028);
    assert(us->equipped[GEAR_SLOT_WEAPON] == ITEM_DRAGON_SCIMITAR);
    apply_script(&plan, 103, &cursor, &resume, 0, 0);
    assert(cursor == plan.n && us->equipped[GEAR_SLOT_WEAPON] == ITEM_NONE);
    assert(osrs_inventory_cell_metadata(&us->inventory_cells[0])->raw_osrs_id == 3028);
}

static void test_ready_resumes_once(void) {
    reset_plan_state();
    Player* us = &sim.state.env.players[0];
    Player* him = &sim.state.env.players[1];
    us->current_hitpoints = 50;
    us->special_energy = 100;
    us->attack_timer = us->attack_timer_uncapped = 2;
    us->inventory_cells[0] = osrs_inventory_cell_from_content_code(raw_id_to_content(13441));
    him->x = us->x + 1; him->y = us->y;
    him->current_hitpoints = 121;
    LaScript plan = {.production = 1, .n = 4, .due = {101, 101, 101, 101},
        .op = {0, 3, 1, 4}, .arg = {0, 0, 0, -1}, .timing_rng = 1234};
    plan.command[0] = click_command(0);
    int cursor = 0, resume = 101, attacks = 0;
    for (int now = 101; now < 112 && cursor < plan.n; now++) {
        apply_script(&plan, now, &cursor, &resume, 0, 0);
        HumanCommandQueue* commands = &sim.context.policy_commands[0].commands;
        for (int i = 0; i < commands->count; i++) attacks += commands->items[i].kind == HUMAN_COMMAND_ATTACK_NPC;
        if (now == 101) assert(!osrs_interaction_active(&us->interaction) && us->food_timer > 0);
    }
    assert(attacks == 1 && cursor == 4);
    assert(sim.state.spec_voidwaker[0] == 1 && us->special_energy == 50);
    assert(osrs_interaction_active(&us->interaction) && us->attack_timer > 0);

    reset_plan_state();
    HumanCommand engage = attack_command();
    riskfight_execute_command(&sim.state, &sim.context, 0, &engage);
    plan = (LaScript){.production = 1, .n = 2, .due = {101, 101}, .op = {3, 1}};
    cursor = 0; resume = 101;
    apply_script(&plan, 101, &cursor, &resume, 0, 0);
    assert(cursor == 0 && sim.context.policy_commands[0].commands.count == 0);
    sim.state.env.players[0].attack_timer = sim.state.env.players[0].attack_timer_uncapped = 0;
    apply_script(&plan, 102, &cursor, &resume, 0, 0);
    assert(cursor == 2 && !plan.ready_sent);
    assert(sim.context.policy_commands[0].commands.count == 1);
    assert(sim.context.policy_commands[0].commands.items[0].kind == HUMAN_COMMAND_SPEC_TOGGLE);

    reset_plan_state();
    plan = (LaScript){.production = 1, .n = 2, .due = {101, 101},
        .op = {3, 1}, .timing_rng = 1};
    cursor = 0; resume = 101;
    apply_script(&plan, 101, &cursor, &resume, 0, 0);
    assert(cursor == 0 && plan.ready_sent);
    HumanCommand stop = {.kind = HUMAN_COMMAND_STOP};
    riskfight_execute_command(&sim.state, &sim.context, 0, &stop);
    apply_script(&plan, 102, &cursor, &resume, 0, 0);
    assert(cursor == 2 && !sim.state.env.players[0].spec_armed);
    assert(sim.context.policy_commands[0].commands.count == 0);
}

static void test_ranged_hit_registration_and_damage(void) {
    for (int minimum = -1; minimum <= 1; minimum++) {
        reset_plan_state();
        Player* us = &sim.state.env.players[0];
        Player* him = &sim.state.env.players[1];
        us->equipped[GEAR_SLOT_WEAPON] = ITEM_MAGIC_SHORTBOW_I;
        us->equipped[GEAR_SLOT_AMMO] = ITEM_AMETHYST_ARROW;
        osrs_refresh_player_equipment(us);
        us->attack_timer = us->attack_timer_uncapped = -1;
        us->inventory_cells[0] = osrs_inventory_cell_from_item(ITEM_VOIDWAKER);
        him->x = us->x + 6; him->y = us->y;
        LaScript plan = {.production = 1, .n = 3, .due = {101, 101, 101},
            .op = {0, 4, 0}, .arg = {0, minimum, 0}};
        plan.command[0] = attack_command();
        plan.command[2] = (HumanCommand){.kind = HUMAN_COMMAND_EQUIP_INVENTORY_ITEM,
            .item_osrs_id = ITEM_DATABASE[ITEM_VOIDWAKER].item_id};
        int cursor = 0, resume = 101;
        apply_script(&plan, 101, &cursor, &resume, 0, 0);
        assert(cursor == 1 && sim.state.visible[1].last_attack_tick == sim.state.env.tick - 1);
        assert(!him->hit_landed_this_tick && us->attack_timer > 0);
        apply_script(&plan, 102, &cursor, &resume, 0, 0);
        if (minimum < 0) {
            assert(cursor == 3 && us->equipped[GEAR_SLOT_WEAPON] == ITEM_VOIDWAKER);
            assert(!him->hit_landed_this_tick);
        } else {
            assert(cursor == 1 && us->equipped[GEAR_SLOT_WEAPON] == ITEM_MAGIC_SHORTBOW_I);
            int now = 103;
            while (!him->hit_landed_this_tick && now < 110)
                apply_script(&plan, now++, &cursor, &resume, 0, 0);
            assert(him->hit_landed_this_tick);
            int damage = him->hit_damage;
            apply_script(&plan, now, &cursor, &resume, 1, damage);
            assert(cursor == 3);
            assert(us->equipped[GEAR_SLOT_WEAPON] == (damage >= minimum ? ITEM_VOIDWAKER : ITEM_MAGIC_SHORTBOW_I));
        }
    }
}

static void test_native_reservation_continues_existing_policy(void) {
    reset_plan_state();
    sim.state.env.tick = RISKFIGHT_LOAD_TICK;
    Player* us = &sim.state.env.players[0];
    Player* him = &sim.state.env.players[1];
    us->current_hitpoints = 121;
    us->special_energy = 50;
    us->spec_armed = 0;
    us->attack_timer = us->attack_timer_uncapped = 2;
    him->current_hitpoints = 1;
    him->special_energy = 0;
    him->attack_timer = him->attack_timer_uncapped = 100;
    him->x = us->x + 1; him->y = us->y;
    osrs_interaction_set(&him->interaction, 0);
    for (int a = 0; a < 2; a++)
        for (int i = 0; i < OSRS_INVENTORY_SIZE; i++)
            sim.state.env.players[a].inventory_cells[i] = osrs_inventory_cell_empty();
    observer_clear(&sim.obs);
    memset(&sim.obs, 0, sizeof(sim.obs));
    gs_infer_init(&sim.obs.inf);
    sim.obs.inf.tracker_current = 1;
    sim.obs.bar_hit_tick = 102;
    sim.obs.bar_ratio = 1;
    observer_sync(&sim.state, &sim.obs, 102);
    gs_on_game_tick(&sim.obs.inf, 102);
    observer_after_tick(&sim.obs, 102);
    sim.obs.inf.local_spec_tick = sim.obs.inf.local_attack_tick = 101;
    sim.obs.inf.local_timer = (GsTimerState){1, 105, 105};
    sim.obs.inf.opponent.timer = (GsTimerState){1, 200, 200};
    sim.obs.inf.opponent_hitpoints = sim.obs.inf.opponent_bar = (GsHealthRange){1, 3, 3};
    Ours reserved = sim.ours;
    reserved.lookahead = 0;
    reserved.ideal_timing = 1;
    reserved.equal_stakes = 1;
    reserved.pending_count = 0;
    reserved.burst_pending = 1;
    reserved.burst_started = 101;
    reserved.burst_first = INT_MIN;
    reserved.burst_spent = 0;
    reserved.burst_target = 1;
    reserved.burst_bar = 0;
    Observer observed = {0};
    observer_assign(&observed, &sim.obs);
    world = sim.state;
    LaScript plan = {.native_rules = &reserved, .native_observer = &observed};
    OppModel opponent = {.kind = MODEL_PROFILES};
    int horizon = g_la_horizon;
    g_la_horizon = 4;
    (void)la_rollout(&sim, &world, &sim.hist, 102, &opponent, &plan, 123);
    g_la_horizon = horizon;
    observer_clear(&observed);
    assert(sim.ours.burst_first == 101 && sim.ours.burst_spent);
    assert(sim.state.spec_voidwaker[0] == 1 && sim.state.env.players[0].special_energy == 0);
}

static void test_source_actor_constraints_and_clone(void) {
    Observer source = {0};
    gs_infer_init(&source.inf);
    GsInfer* inf = &source.inf;
    inf->timed = inf->tracker_current = 1;
    inf->c.worn[GS_WORN_WEAPON] = GS_ITEM_VOIDWAKER;
    inf->c.strength = inf->c.attack = inf->c.ranged = inf->c.magic = 99;
    inf->c.opponent_combat_level = 121;
    inf->c.ratio = inf->c.scale = 30;
    gs_eater_observe(inf, 100);
    gs_eater_opponent_hitsplat(inf, 101);
    gs_eater_opponent_struck(inf, 101, 40);
    inf->c.ratio = 15;
    gs_eater_observe(inf, 101);
    assert(inf->health->base == 82 && inf->health->target.base_hp == 82);
    const GsTargetBranch* original = inf->health->target.history;

    static RiskfightSnapshot snapshot;
    memset(&snapshot, 0, sizeof(snapshot));
    snapshot.pending_known = 1;
    snapshot.seed = 7;
    snapshot.loadout[0] = snapshot.loadout[1] = RISKFIGHT_DEFAULT_LOADOUT;
    uint32_t rng = 11;
    int hitpoints;
    source_actor_snapshot(inf->health, 121, 0, &rng, &snapshot.actor[1], &hitpoints);
    assert(snapshot.actor[1].hitpoints == 82 && snapshot.actor[1].attack == 99
        && snapshot.actor[1].strength == 99 && snapshot.actor[1].defence == 99);
    assert(riskfight_actor_combat_level(&snapshot.actor[1]) == 121);
    assert(inf->health->base_hp_support[82][hitpoints / 64] >> (hitpoints % 64) & 1);
    snapshot.side[0] = (RiskfightSideSnapshot){.attack_history = RISKFIGHT_ATTACK_ABSENT,
        .hitpoints = 121, .prayer = 99, .attack = 118, .strength = 118, .defence = 118, .magic = 99, .special_energy = 100};
    snapshot.side[1] = (RiskfightSideSnapshot){.attack_history = RISKFIGHT_ATTACK_ABSENT,
        .hitpoints = hitpoints, .prayer = 90, .attack = 118, .strength = 118, .defence = 118, .magic = 99, .seen_health_bar = 15};
    static Sim clone;
    sim_init(&clone);
    riskfight_load_state(&clone.state, &clone.context, &snapshot);
    observer_assign(&clone.obs, &source);
    clone.obs.bar_ratio = 15;
    observer_branch(&clone.state, &clone.obs);
    observer_sync(&clone.state, &clone.obs, 102);
    observer_after_tick(&clone.obs, 102);
    assert(clone.state.env.players[1].base_hitpoints == 82);
    assert(observer_combat_level(&clone.state.env.players[1]) == 121);
    assert(clone.obs.inf.c.opponent_combat_level == 121 && clone.obs.inf.c.forecast_available);
    assert(clone.obs.inf.health != inf->health && inf->health->target.history == original);
    GsSpecSituation situation;
    GsUnderway underway;
    gs_spec_inputs(&clone.obs.inf, 102, &situation, &underway);
    assert(situation.target.has_history && situation.target.has_belief && situation.target.base_hp == 82);
    GsSpecHit hit = gs_certain_hit(&clone.ours.arena, 103, 10);
#ifndef GS_HP_BELIEF
    double forecast = gs_spec_knockout(&clone.ours.arena, &situation, &hit, 1, GS_ECHO_RECOIL, gs_spec_horizon(&situation, &hit, 1));
#else
    double forecast = gs_spec_knockout(&clone.ours.arena, &situation, &hit, 1, GS_ECHO_RECOIL);
#endif
    assert(isfinite(forecast) && forecast >= 0 && forecast <= 1);
    sim_clear(&clone);
    assert(inf->health->target.history == original && inf->health->references == 1);
    observer_clear(&source);
}

int main(void) {
    observer_register_items();
    sim_init(&sim);
    test_source_actor_constraints_and_clone();
    test_commands_and_barriers();
    test_shared_click_timing();
    test_identity_after_inventory_mutation();
    test_pending_before_actions_and_common_world();
    test_pending_metadata_compact_and_reuse();
    test_conditional_two_ticket_plan();
    test_strict_hp_equality();
    test_conditioned_meal_clock();
    test_skipped_guards_at_cutoff();
    test_dynamic_recipe_transitions();
    test_ready_resumes_once();
    test_ranged_hit_registration_and_damage();
    test_native_reservation_continues_existing_policy();
    riskfight_destroy_context((EncounterContext*)&sim.context);
    puts("Lookahead pending-world and production-plan contracts passed");
    return 0;
}
