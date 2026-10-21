#define TFA_PRIMARY_ATTACK_BASE OSRS_PRIMARY_MOVE_ACTIONS
#define TFA_PRIMARY_DIM OSRS_PRIMARY_DIM(TFA_NUM_TARGET_SLOTS)

typedef struct {
    TfaState* s;
    int p;
} TfaTargetLookup;

static int tfa_target_slot_npc(const TfaState* s, int target_slot) {
    if (target_slot < 0 || target_slot >= TFA_NUM_TARGET_SLOTS) return -1;
    const TfaNpc* n = &s->npcs[target_slot];
    return n->active && n->attackable ? target_slot : -1;
}

static int tfa_lookup_attack_target(void* ctx, int target_slot, OsrsAttackTarget* out) {
    TfaTargetLookup* lookup = (TfaTargetLookup*)ctx;
    int i = tfa_target_slot_npc(lookup->s, target_slot);
    if (i < 0) return 0;
    const TfaNpc* n = &lookup->s->npcs[i];
    *out = (OsrsAttackTarget){
        .slot = target_slot,
        .x = n->x,
        .y = n->y,
        .size = n->size,
        .attack_range = tfa_player_stats(&lookup->s->players[lookup->p])->attack_range,
    };
    return 1;
}

/* Prayer clicks of interval L are in effect at the server tick labelled L+1,
   so they apply before this tick's NPC attacks are computed. */
static void tfa_player_apply_prayers(TfaState* s, int p, const int* acts) {
    TfaPlayer* tp = &s->players[p];
    if (tp->life != TFA_LIFE_ALIVE) return;
    if (tp->injured_ticks <= 0 && encounter_apply_overhead_action(&tp->player.prayer, acts[OSRS_HEAD_OVERHEAD]))
        tp->player.prayer_just_activated = 1;
    OffensivePrayer before = tp->player.offensive_prayer;
    if (encounter_apply_offensive_action(&tp->player.offensive_prayer, acts[OSRS_HEAD_OFFENSIVE]))
        tp->player.offensive_prayer_just_activated = 1;
    if (tp->player.offensive_prayer != before) tfa_mark_stats_dirty(tp);
    int spec = acts[OSRS_HEAD_SPECIAL];
    int cost = osrs_spec_cost(tp->player.equipped[GEAR_SLOT_WEAPON]);
    if (spec == 1 && cost > 0 && tp->player.special_energy >= cost) tp->player.spec_armed = 1;
    else if (spec == 2) tp->player.spec_armed = 0;
}

static void tfa_player_apply_inventory(TfaState* s, int p, const int* acts) {
    TfaPlayer* tp = &s->players[p];
    OsrsInventoryClickActions clicks;
    for (int slot = 0; slot < NUM_GEAR_SLOTS; slot++)
        clicks.equip_by_slot[slot] = acts[OSRS_HEAD_EQUIP_SLOT(slot)];
    clicks.eat = acts[OSRS_HEAD_EAT];
    clicks.drink = acts[OSRS_HEAD_DRINK];
    OsrsInventoryTickIntent intent =
        osrs_resolve_inventory_tick_intent(&tp->player, tp->player.inventory_cells, &clicks);
    OsrsInventoryApplyStep step;
    while (osrs_inventory_intent_next(&intent, &step)) {
        OsrsInventoryUseResult r = osrs_player_use_inventory(
            &tp->player, &tp->use, NULL, step.cell_idx, s->tick);
        assert(r != OSRS_INVENTORY_USE_ESCAPED);
        if (r == OSRS_INVENTORY_USE_CONSUMED && step.kind == OSRS_INVENTORY_APPLY_EQUIP)
            tfa_mark_stats_dirty(tp);
    }
}

static void tfa_record_player_attack(TfaState* s, int p, const TfaNpc* n, int damage, AttackStyle style, int is_special) {
    TfaPlayer* tp = &s->players[p];
    int distance = tfa_player_npc_distance(tp, n);
    tp->attacked_this_tick = 1;
    tp->attack_target_slot = tfa_npc_slot(s, n);
    tp->attack_damage = damage;
    tp->attack_style = style;
    tp->attack_is_special = is_special;
    tp->attack_timing = encounter_player_projectile_timing(
        style, tp->player.equipped[GEAR_SLOT_WEAPON], is_special, distance);
    tp->player.just_attacked = 1;
    tp->player.last_attack_style = style;
    tp->player.attack_style_this_tick = style;
}

static int tfa_npc_damage_cap(const TfaState* s, const TfaNpc* n, int damage);

static void tfa_player_attack(TfaState* s, int p, TfaNpc* n) {
    TfaPlayer* tp = &s->players[p];
    const EncounterLoadoutStats* ls = tfa_player_stats(tp);
    AttackStyle style = ls->style;
    uint8_t weapon = tp->player.equipped[GEAR_SLOT_WEAPON];
    MeleeStyle melee_style = osrs_item_melee_style(weapon);
    const MonsterStats* m = &MONSTER_DATABASE[n->monster];
    OsrsTargetRef target = { .kind = OSRS_TARGET_NPC, .id = tfa_npc_slot(s, n) };
    int def_roll = tfa_npc_def_roll(n, style, melee_style);
    int distance = tfa_player_npc_distance(tp, n);
    int delay = encounter_player_projectile_timing(style, weapon, 0, distance).damage_delay_ticks;

    if (tp->player.spec_armed && osrs_spec_cost(weapon) > 0 &&
            tp->player.special_energy >= osrs_spec_cost(weapon)) {
        int att_roll = osrs_player_att_roll(ls->eff_level, ls->attack_bonus);
        SpecResult sr = osrs_resolve_spec(weapon, att_roll, ls->max_hit, def_roll, m->def_level, &s->rng_state);
        tp->player.special_energy -= sr.spec_cost;
        tp->player.used_special_this_tick = 1;
        tp->player.attack_timer = sr.attack_speed_override ? sr.attack_speed_override : ls->attack_speed;
        osrs_spec_disarm(&tp->player.spec_armed);
        int total = 0;
        for (int h = 0; h < sr.num_hits; h++) {
            int dmg = tfa_npc_damage_cap(s, n, sr.damage[h]);
            tfa_queue_player_hit(s, n, p, dmg, style, delay);
            total += dmg;
        }
        if (sr.heal > 0) {
            tp->player.current_hitpoints += sr.heal;
            if (tp->player.current_hitpoints > tp->player.base_hitpoints)
                tp->player.current_hitpoints = tp->player.base_hitpoints;
        }
        tfa_record_player_attack(s, p, n, total, style, 1);
        return;
    }

    OsrsMagicAttackKind magic_kind = style == ATTACK_STYLE_MAGIC ? OSRS_MAGIC_ATTACK_POWERED_STAFF : OSRS_MAGIC_ATTACK_NONE;
    OsrsPreparedAttackEffects fx = osrs_prepare_attack_effects_for_melee_style(
        &tp->player.equipment_effect_profile, &tp->player.item_effect_state, weapon, style, melee_style,
        magic_kind, target, 1, osrs_player_att_roll(ls->eff_level, ls->attack_bonus), ls->max_hit,
        osrs_target_effect_context_magic(m->magic_level, m->magic_att_bonus),
        tp->player.current_hitpoints, tp->player.base_hitpoints);
    tp->player.attack_timer = ls->attack_speed;
    int hit = fx.use_double_accuracy
        ? encounter_roll_hit_chance_double(&s->rng_state, fx.attack_roll, def_roll)
        : encounter_roll_hit_chance(&s->rng_state, fx.attack_roll, def_roll);
    int dmg = hit ? tfa_npc_damage_cap(s, n, fx.min_hit + tfa_rand(s, fx.max_hit - fx.min_hit + 1)) : 0;
    tfa_queue_player_hit(s, n, p, dmg, style, delay);
    OsrsPostAttackEffects post = osrs_finalize_attack_effects(
        &tp->player.equipment_effect_profile, &tp->player.item_effect_state, weapon, style,
        magic_kind, target, 1, fx.use_double_accuracy, hit, dmg, &s->rng_state);
    if (post.heal_amount > 0) {
        tp->player.current_hitpoints += post.heal_amount;
        if (tp->player.current_hitpoints > tp->player.base_hitpoints)
            tp->player.current_hitpoints = tp->player.base_hitpoints;
    }
    tfa_record_player_attack(s, p, n, dmg, style, 0);
}

static void tfa_player_turn(TfaState* s, TfaContext* ctx, int p, const int* acts) {
    TfaPlayer* tp = &s->players[p];
    if (tp->life != TFA_LIFE_ALIVE) return;
    tfa_player_apply_inventory(s, p, acts);

    int primary = acts[OSRS_HEAD_PRIMARY];
    OsrsPlayerCommand command = { .kind = OSRS_PLAYER_CMD_NONE };
    if (primary >= TFA_PRIMARY_ATTACK_BASE && primary < TFA_PRIMARY_DIM) {
        command.kind = OSRS_PLAYER_CMD_TARGET;
        command.target_slot = primary - TFA_PRIMARY_ATTACK_BASE;
        tp->dest_x = -1;
        tp->dest_y = -1;
    } else if (primary > 0 && primary < OSRS_PRIMARY_MOVE_ACTIONS) {
        tp->dest_x = tp->player.x + ENCOUNTER_MOVE_TARGET_DX[primary];
        tp->dest_y = tp->player.y + ENCOUNTER_MOVE_TARGET_DY[primary];
        command.kind = OSRS_PLAYER_CMD_MOVE;
        command.move_kind = OSRS_PLAYER_MOVE_DESTINATION;
    }

    TfaTargetLookup lookup = { .s = s, .p = p };
    OsrsPlayerStepInput input = {
        .player = &tp->player,
        .interaction = &tp->player.interaction,
        .route_cache = &tp->route_cache,
        .target_lookup = tfa_lookup_attack_target,
        .target_ctx = &lookup,
        .command = command,
        .dest_x = &tp->dest_x,
        .dest_y = &tp->dest_y,
        .blocked_ticks = tp->stunned_ticks,
        .arena = {
            .topology = ctx->topology[s->room],
            .movement_mode = tp->weary_ticks > 0 ? ENCOUNTER_ROUTE_MOVEMENT_WALK : ENCOUNTER_ROUTE_MOVEMENT_RUN,
            .cost_policy = ENCOUNTER_ROUTE_COST_OSRS,
            .destination_cost_policy = ENCOUNTER_ROUTE_COST_SOUTH_FIRST_BFS,
            .attack_geometry = ENCOUNTER_ROUTE_ATTACK_GEOMETRY_TOPOLOGY,
        },
    };
    int held_x = tp->player.x, held_y = tp->player.y;
    OsrsPlayerStepResult step = osrs_encounter_player_step(&input);
    if (tp->rooted_ticks > 0) {
        tp->player.x = held_x;
        tp->player.y = held_y;
    }

    if (!step.can_attack || tp->player.attack_timer > 0 || tp->stunned_ticks > 0) return;
    int i = tfa_target_slot_npc(s, step.target_slot);
    if (i < 0) return;
    tfa_player_attack(s, p, &s->npcs[i]);
}

static void tfa_player_tick_timers(TfaState* s, int p) {
    TfaPlayer* tp = &s->players[p];
    Player* pl = &tp->player;
    if (pl->attack_timer > 0) pl->attack_timer--;
    if (pl->food_timer > 0) pl->food_timer--;
    if (pl->karambwan_timer > 0) pl->karambwan_timer--;
    if (pl->potion_timer > 0) pl->potion_timer--;
    if (tp->weary_ticks > 0) tp->weary_ticks--;
    if (tp->rooted_ticks > 0) tp->rooted_ticks--;
    if (tp->stunned_ticks > 0) tp->stunned_ticks--;
    if (tp->injured_ticks > 0) tp->injured_ticks--;
    osrs_poison_tick_immunity(&tp->poison);
    osrs_player_inventory_tick(pl, &tp->use);
    int poison = osrs_poison_tick(&tp->poison);
    if (poison > 0) tfa_damage_player_now(s, p, poison, ATTACK_STYLE_NONE);
    if (tp->burn_ticks > 0) tp->burn_ticks--;
}
