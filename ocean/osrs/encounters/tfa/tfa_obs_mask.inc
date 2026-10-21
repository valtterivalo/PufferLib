#define TFA_OBS_RAID 20
#define TFA_OBS_TEAMMATE 10
#define TFA_OBS_TEAM (TFA_OBS_TEAMMATE * (TFA_MAX_PLAYERS - 1))
#define TFA_OBS_NPC_FEATURES 18
#define TFA_OBS_NPCS (TFA_OBS_NPC_FEATURES * TFA_NUM_TARGET_SLOTS)
#define TFA_DANGER_RADIUS 5
#define TFA_DANGER_SIDE (2 * TFA_DANGER_RADIUS + 1)
#define TFA_OBS_DANGER (2 * TFA_DANGER_SIDE * TFA_DANGER_SIDE)
#define TFA_OBS_INCOMING 6
#define TFA_OBS_AFTER_SHARED OSRS_SHARED_OBS_SIZE
#define TFA_NUM_OBS (TFA_OBS_AFTER_SHARED + TFA_OBS_RAID + TFA_OBS_TEAM + TFA_OBS_NPCS + TFA_OBS_DANGER + TFA_OBS_INCOMING)

#define TFA_NUM_ACTION_HEADS OSRS_BASE_NUM_ACTION_HEADS
#define TFA_ACTION_MASK_SIZE OSRS_BASE_ACTION_MASK_SIZE(TFA_NUM_TARGET_SLOTS)
#define TFA_ACTION_DIMS_INIT OSRS_BASE_ACTION_DIMS_INIT(TFA_NUM_TARGET_SLOTS)
static const int TFA_ACTION_DIMS[TFA_NUM_ACTION_HEADS] = OSRS_BASE_ACTION_DIMS_INIT(TFA_NUM_TARGET_SLOTS);

#define TFA_DANGER_HORIZON 6

static float tfa_ratio(int value, int scale) {
    return osrs_policy_ratio(value, scale);
}

static int tfa_npc_targets(const TfaState* s, const TfaNpc* n, int p);
static int tfa_npc_act_ticks(const TfaState* s, const TfaNpc* n);

/* Ticks until the nearest hazard on (x, y) can hit, or -1 when none threatens it within the horizon. */
static int tfa_tile_danger(const TfaState* s, int p, int x, int y) {
    int best = -1;
    for (int i = 0; i < TFA_MAX_HAZARDS; i++) {
        const TfaHazard* h = &s->hazards[i];
        if (h->kind == TFA_HAZ_NONE || (h->owner >= 0 && h->owner != p)) continue;
        int t;
        if (h->kind == TFA_HAZ_MUSHROOM) {
            if (chebyshev_distance(h->x, h->y, x, y) > 1) continue;
            t = h->arm_ticks > 0 ? h->arm_ticks : 1;
        } else {
            if (!tfa_hazard_covers(h, x, y)) continue;
            t = h->start_tick > s->tick ? h->start_tick - s->tick : 0;
        }
        if (t > TFA_DANGER_HORIZON) continue;
        if (best < 0 || t < best) best = t;
    }
    return best;
}

static void tfa_write_obs_for(TfaState* s, TfaContext* ctx, int p, float* obs) {
    (void)ctx;
    TfaPlayer* tp = &s->players[p];
    const TfaRoomLayout* layout = tfa_room_layout(s->room);
    const EncounterLoadoutStats* ls = tfa_player_stats(tp);
    int i = 0;
    OsrsSharedObservationInput shared = {
        .player = &tp->player,
        .interaction = &tp->player.interaction,
        .arena_min_x = 0,
        .arena_max_x = TFA_ROOM_SIZE,
        .arena_min_y = 0,
        .arena_max_y = TFA_ROOM_SIZE,
        .attack_style = ls->style,
        .attack_range = ls->attack_range,
        .max_hit = ls->max_hit,
        .attack_speed = ls->attack_speed,
        .defence_stab = ls->def_stab,
        .defence_slash = ls->def_slash,
        .defence_crush = ls->def_crush,
        .defence_magic = ls->def_magic,
        .defence_ranged = ls->def_ranged,
        .effective_level = ls->eff_level,
        .attack_bonus = ls->attack_bonus,
        .strength_bonus = ls->strength_bonus,
        .spell_base_damage = ls->spell_base_damage,
        .special_attack_cost = osrs_spec_cost(tp->player.equipped[GEAR_SLOT_WEAPON]),
    };
    i += osrs_write_shared_observations(&obs[i], &shared);
    assert(i == TFA_OBS_AFTER_SHARED);

    for (int r = 0; r < TFA_ROOM_COUNT; r++) obs[i++] = s->room == r ? 1.0f : 0.0f;
    obs[i++] = tfa_ratio(s->raid_index, TFA_RAID_LENGTH - 1);
    obs[i++] = tfa_ratio(s->room_tick, 1000);
    obs[i++] = tfa_ratio(tfa_alive_count(s), s->team_size);
    obs[i++] = tp->life == TFA_LIFE_ALIVE ? 1.0f : 0.0f;
    obs[i++] = tfa_ratio(tp->weary_ticks, 50);
    obs[i++] = tfa_ratio(tp->rooted_ticks, 32);
    obs[i++] = tfa_ratio(tp->stunned_ticks, 8);
    obs[i++] = tfa_ratio(tp->burn_ticks, 10);
    obs[i++] = tp->poison.severity > 0 ? 1.0f : 0.0f;
    obs[i++] = tfa_ratio(tp->incoming.count, 8);
    obs[i++] = tfa_ratio(tp->damage_taken_tick, 50);
    obs[i++] = tfa_ratio(s->team_damage_dealt_tick, 100);

    for (int q = 0; q < TFA_MAX_PLAYERS; q++) {
        if (q == p) continue;
        const TfaPlayer* mate = &s->players[q];
        int present = q < s->team_size;
        int alive = present && mate->life == TFA_LIFE_ALIVE;
        obs[i++] = present ? 1.0f : 0.0f;
        obs[i++] = alive ? 1.0f : 0.0f;
        obs[i++] = alive ? tfa_ratio(mate->player.x - tp->player.x, 16) : 0.0f;
        obs[i++] = alive ? tfa_ratio(mate->player.y - tp->player.y, 16) : 0.0f;
        obs[i++] = alive ? tfa_ratio(mate->player.current_hitpoints, mate->player.base_hitpoints) : 0.0f;
        obs[i++] = alive && mate->player.prayer == PRAYER_PROTECT_MELEE ? 1.0f : 0.0f;
        obs[i++] = alive && mate->player.prayer == PRAYER_PROTECT_RANGED ? 1.0f : 0.0f;
        obs[i++] = alive && mate->player.prayer == PRAYER_PROTECT_MAGIC ? 1.0f : 0.0f;
        obs[i++] = alive ? tfa_ratio(chebyshev_distance(mate->player.x, mate->player.y, tp->player.x, tp->player.y), 16) : 0.0f;
        obs[i++] = alive && osrs_interaction_active(&mate->player.interaction) ? 1.0f : 0.0f;
    }

    for (int k = 0; k < TFA_NUM_TARGET_SLOTS; k++) {
        const TfaNpc* n = &s->npcs[k];
        if (!n->active) {
            for (int f = 0; f < TFA_OBS_NPC_FEATURES; f++) obs[i++] = 0.0f;
            continue;
        }
        obs[i++] = 1.0f;
        obs[i++] = tfa_ratio(n->kind, TFA_NPC_KIND_COUNT);
        obs[i++] = tfa_ratio(n->x - tp->player.x, 32);
        obs[i++] = tfa_ratio(n->y - tp->player.y, 32);
        obs[i++] = tfa_ratio(n->size, 7);
        obs[i++] = tfa_ratio(n->hp, n->max_hp);
        obs[i++] = tfa_ratio(n->shield, n->max_shield);
        obs[i++] = n->attackable ? 1.0f : 0.0f;
        obs[i++] = tfa_ratio(tfa_npc_act_ticks(s, n), 10);
        for (int a = TFA_ACT_MELEE; a < TFA_ACT_COUNT; a++) obs[i++] = n->act == a ? 1.0f : 0.0f;
        obs[i++] = tfa_npc_targets(s, n, p) ? 1.0f : 0.0f;
        obs[i++] = tfa_ratio(n->state, 8);
        obs[i++] = tfa_ratio(tfa_player_npc_distance(tp, n), 32);
        obs[i++] = osrs_interaction_active(&tp->player.interaction) && tp->player.interaction.target_slot == k ? 1.0f : 0.0f;
    }

    for (int dy = TFA_DANGER_RADIUS; dy >= -TFA_DANGER_RADIUS; dy--) {
        for (int dx = -TFA_DANGER_RADIUS; dx <= TFA_DANGER_RADIUS; dx++) {
            int x = tp->player.x + dx, y = tp->player.y + dy;
            int t = tfa_tile_danger(s, p, x, y);
            obs[i++] = t < 0 ? 0.0f : 1.0f - (float)t / (float)(TFA_DANGER_HORIZON + 1);
            obs[i++] = tfa_layout_blocked(layout, x, y) ? 1.0f : 0.0f;
        }
    }

    int next[3] = { -1, -1, -1 };
    int sum = 0;
    for (int h = 0; h < tp->incoming.count; h++) {
        const EncounterPendingHit* hit = &tp->incoming.hits[h];
        int style = hit->attack_style - ATTACK_STYLE_MELEE;
        if (style >= 0 && style < 3 && (next[style] < 0 || hit->ticks_remaining < next[style]))
            next[style] = hit->ticks_remaining;
        sum += hit->damage;
    }
    for (int k = 0; k < 3; k++) obs[i++] = next[k] < 0 ? 0.0f : 1.0f - tfa_ratio(next[k], 8);
    obs[i++] = tfa_ratio(sum, tp->player.base_hitpoints);
    obs[i++] = tfa_ratio(tp->player.current_hitpoints - sum, tp->player.base_hitpoints);
    obs[i++] = sum >= tp->player.current_hitpoints ? 1.0f : 0.0f;

    if (i != TFA_NUM_OBS) {
        fprintf(stderr, "tfa obs wrote %d expected %d\n", i, TFA_NUM_OBS);
        abort();
    }
}

static int tfa_drink_has_effect(const Player* pl, OsrsConsumableKind kind) {
    return kind != OSRS_CONSUMABLE_PRAYER_RESTORE || pl->current_prayer < pl->base_prayer;
}

static void tfa_write_mask_for(TfaState* s, TfaContext* ctx, int p, unsigned char* mask) {
    TfaPlayer* tp = &s->players[p];
    const EncounterArenaTopology* topology = ctx->topology[s->room];
    memset(mask, 0, TFA_ACTION_MASK_SIZE);
    int primary = osrs_base_action_head_mask_offset(TFA_NUM_TARGET_SLOTS, OSRS_HEAD_PRIMARY);
    int overhead = osrs_base_action_head_mask_offset(TFA_NUM_TARGET_SLOTS, OSRS_HEAD_OVERHEAD);
    int eat = osrs_base_action_head_mask_offset(TFA_NUM_TARGET_SLOTS, OSRS_HEAD_EAT);
    int drink = osrs_base_action_head_mask_offset(TFA_NUM_TARGET_SLOTS, OSRS_HEAD_DRINK);
    int spell = osrs_base_action_head_mask_offset(TFA_NUM_TARGET_SLOTS, OSRS_HEAD_SPELL);
    int special = osrs_base_action_head_mask_offset(TFA_NUM_TARGET_SLOTS, OSRS_HEAD_SPECIAL);
    int offensive = osrs_base_action_head_mask_offset(TFA_NUM_TARGET_SLOTS, OSRS_HEAD_OFFENSIVE);
    mask[primary] = 1;
    mask[overhead] = 1;
    mask[eat] = 1;
    mask[drink] = 1;
    mask[spell + OSRS_SPELL_NONE] = 1;
    mask[special] = 1;
    mask[offensive] = 1;
    for (int slot = 0; slot < NUM_GEAR_SLOTS; slot++)
        mask[osrs_base_action_head_mask_offset(TFA_NUM_TARGET_SLOTS, OSRS_HEAD_EQUIP_SLOT(slot))] = 1;
    if (tp->life != TFA_LIFE_ALIVE || s->episode_over) return;

    if (tp->stunned_ticks <= 0 && tp->rooted_ticks <= 0) {
        for (int a = 1; a < OSRS_PRIMARY_MOVE_ACTIONS; a++) {
            int nx = tp->player.x + ENCOUNTER_MOVE_TARGET_DX[a];
            int ny = tp->player.y + ENCOUNTER_MOVE_TARGET_DY[a];
            mask[primary + a] = !encounter_arena_topology_tile_blocked(topology, nx, ny);
        }
    }
    for (int k = 0; k < TFA_NUM_TARGET_SLOTS; k++)
        mask[primary + TFA_PRIMARY_ATTACK_BASE + k] = tfa_target_slot_npc(s, k) >= 0;

    int has_prayer = tp->player.current_prayer > 0;
    mask[overhead + ENCOUNTER_OVERHEAD_OFF] = tp->player.prayer != PRAYER_NONE;
    for (int a = ENCOUNTER_OVERHEAD_SET_REFRESH_MELEE; a <= ENCOUNTER_OVERHEAD_SET_REFRESH_MAGIC; a++)
        mask[overhead + a] = has_prayer;
    mask[offensive + ENCOUNTER_OFFENSIVE_OFF] = tp->player.offensive_prayer != OFFENSIVE_PRAYER_NONE;
    for (int a = ENCOUNTER_OFFENSIVE_SET_REFRESH_PIETY; a <= ENCOUNTER_OFFENSIVE_SET_REFRESH_AUGURY; a++)
        mask[offensive + a] = has_prayer;

    for (int cell = 0; cell < OSRS_INVENTORY_SIZE; cell++) {
        const OsrsInventoryCell* c = &tp->player.inventory_cells[cell];
        OsrsInventoryClickResolution r = osrs_inventory_cell_click_interpret(c, OSRS_CLICK_TICK_FIRST);
        if (r.click_action == OSRS_CLICK_EQUIP && osrs_can_equip_from_cell(&tp->player, tp->player.inventory_cells, cell)) {
            int gear_slot = osrs_inventory_cell_metadata(c)->gear_slot;
            mask[osrs_base_action_head_mask_offset(TFA_NUM_TARGET_SLOTS, OSRS_HEAD_EQUIP_SLOT(gear_slot)) + cell + 1] = 1;
        } else if (r.click_action == OSRS_CLICK_EAT && osrs_can_eat_consumable_kind(&tp->player, r.consumable_kind)) {
            mask[eat + cell + 1] = 1;
        } else if (r.click_action == OSRS_CLICK_DRINK && osrs_inventory_cell_dose_count(c) > 0 &&
                tp->player.potion_timer == 0 && tfa_drink_has_effect(&tp->player, r.consumable_kind)) {
            mask[drink + cell + 1] = 1;
        }
    }

    int cost = osrs_spec_cost(tp->player.equipped[GEAR_SLOT_WEAPON]);
    mask[special + 1] = cost > 0 && tp->player.special_energy >= cost && !tp->player.spec_armed;
    mask[special + 2] = tp->player.spec_armed;
}
