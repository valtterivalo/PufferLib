static const uint8_t TFA_LOADOUT_RANGED[NUM_GEAR_SLOTS] = {
    ITEM_MASORI_MASK_F, ITEM_DIZANAS_QUIVER, ITEM_NECKLACE_OF_ANGUISH, ITEM_DRAGON_ARROWS,
    ITEM_TWISTED_BOW, ITEM_NONE, ITEM_MASORI_BODY_F, ITEM_MASORI_CHAPS_F,
    ITEM_ZARYTE_VAMBRACES, ITEM_AVERNIC_TREADS, ITEM_VENATOR_RING,
};

static const uint8_t TFA_LOADOUT_MELEE_SWITCH[NUM_GEAR_SLOTS] = {
    ITEM_NONE, ITEM_NONE, ITEM_NONE, ITEM_NONE,
    ITEM_INQUISITORS_MACE, ITEM_AVERNIC_DEFENDER, ITEM_NONE, ITEM_NONE,
    ITEM_NONE, ITEM_NONE, ITEM_NONE,
};

static const struct { uint16_t raw_osrs_id; int count; } TFA_SUPPLIES[] = {
    { 6685, 8 },
    { 3024, 6 },
    { 13441, 12 },
};

static void tfa_player_reset(TfaState* s, int p) {
    TfaPlayer* tp = &s->players[p];
    memset(tp, 0, sizeof(*tp));
    Player* pl = &tp->player;
    pl->entity_type = ENTITY_PLAYER;
    memset(pl->equipped, ITEM_NONE, NUM_GEAR_SLOTS);
    encounter_init_maxed_player_combat_stats(pl, TFA_PLAYER_PRAYER);
    pl->special_energy = 100;
    osrs_item_effect_state_init(&pl->item_effect_state);
    osrs_interaction_init(&pl->interaction);
    encounter_apply_loadout(pl, TFA_LOADOUT_RANGED, GEAR_RANGED);
    const uint8_t* switches[] = { TFA_LOADOUT_MELEE_SWITCH };
    encounter_populate_inventory(pl, switches, 1, NULL);
    int cell = 0;
    while (cell < OSRS_INVENTORY_SIZE && !osrs_inventory_cell_is_empty(&pl->inventory_cells[cell])) cell++;
    for (size_t k = 0; k < sizeof(TFA_SUPPLIES) / sizeof(*TFA_SUPPLIES); k++) {
        for (int n = 0; n < TFA_SUPPLIES[k].count; n++) {
            assert(cell < OSRS_INVENTORY_SIZE);
            pl->inventory_cells[cell++] = osrs_inventory_cell_from_raw_osrs_id(TFA_SUPPLIES[k].raw_osrs_id);
        }
    }
    pl->offensive_prayer = OFFENSIVE_PRAYER_RIGOUR;
    tp->life = p < s->team_size ? TFA_LIFE_ALIVE : TFA_LIFE_ABSENT;
    tp->dest_x = -1;
    tp->dest_y = -1;
    tp->stats_dirty = 1;
}

static void tfa_boss_enter(TfaState* s);

static void tfa_room_enter(TfaState* s, int room) {
    s->room = room;
    s->room_tick = 0;
    memset(s->npcs, 0, sizeof(s->npcs));
    memset(s->hazards, 0, sizeof(s->hazards));
    memset(&s->boss, 0, sizeof(s->boss));
    const TfaRoomLayout* l = tfa_room_layout(room);
    for (int p = 0; p < s->team_size; p++) {
        TfaPlayer* tp = &s->players[p];
        tp->player.x = l->start_x[p];
        tp->player.y = l->start_y[p];
        tp->player.dest_x = tp->player.x;
        tp->player.dest_y = tp->player.y;
        tp->dest_x = -1;
        tp->dest_y = -1;
        osrs_interaction_clear(&tp->player.interaction);
        osrs_actor_route_cache_clear(&tp->route_cache);
        tp->incoming.count = 0;
    }
    tfa_boss_enter(s);
}

static void tfa_reset_typed(TfaState* s, TfaContext* ctx, uint32_t seed) {
    for (int room = 0; room < TFA_ROOM_COUNT; room++)
        encounter_arena_topology_require_finalized(ctx->topology[room]);
    uint32_t saved_rng = s->rng_state;
    memset(s, 0, sizeof(*s));
    s->rng_state = encounter_resolve_seed(saved_rng, seed);
    s->team_size = ctx->config.team_size;
    s->episode_mode = ctx->config.episode_mode;
    s->path = ctx->config.path;
    for (int p = 0; p < TFA_MAX_PLAYERS; p++) tfa_player_reset(s, p);
    int room = ctx->config.start_room;
    s->raid_index = 0;
    if (s->episode_mode == TFA_EPISODE_RAID) room = tfa_raid_room(0, s->path);
    tfa_room_enter(s, room);
    ctx->log.room_attempts[s->room] += 1.0f;
}

static void tfa_log_episode(TfaState* s, TfaContext* ctx) {
    TfaLog* log = &ctx->log;
    log->episode_return += s->episode_return;
    log->episode_length += (float)s->tick;
    log->rooms_cleared += (float)s->rooms_cleared;
    log->raid_clears += s->outcome == TFA_OUTCOME_CLEARED && s->episode_mode == TFA_EPISODE_RAID;
    log->wipes += s->outcome == TFA_OUTCOME_WIPED;
    log->timeouts += s->outcome == TFA_OUTCOME_TIMEOUT;
    for (int p = 0; p < s->team_size; p++) {
        log->damage_dealt += s->players[p].damage_dealt_total;
        log->damage_taken += s->players[p].damage_taken_total;
    }
    log->n += 1.0f;
}

/* EncounterDef surface: player 0 is the controlled agent, teammates act from ctx->teammate_actions. */

static void tfa_init_context(EncounterContext* context) {
    TfaContext* ctx = (TfaContext*)context;
    memset(ctx, 0, sizeof(*ctx));
    ctx->config = (TfaConfig){
        .team_size = 1,
        .episode_mode = TFA_EPISODE_ROOM,
        .start_room = TFA_ROOM_GUARDIAN,
        .path = 0,
        .room_max_ticks = TFA_ROOM_MAX_TICKS,
        .reward = {
            .room_clear = 1.0f,
            .raid_clear = 2.0f,
            .damage_dealt = 0.05f,
            .damage_taken = 0.05f,
            .death = 0.5f,
            .wipe = 1.0f,
        },
    };
}

static void tfa_destroy_context(EncounterContext* context) {
    (void)context;
}

static void tfa_finalize_context(EncounterState* state, EncounterContext* context) {
    (void)state;
    tfa_build_topologies((TfaContext*)context);
}

static void tfa_init_state(EncounterState* state, EncounterContext* context) {
    (void)context;
    memset(state, 0, sizeof(TfaState));
}

static EncounterState* tfa_create(void) {
    return (EncounterState*)calloc(1, sizeof(TfaState));
}

static void tfa_destroy(EncounterState* state) {
    free(state);
}

static void tfa_reset(EncounterState* state, EncounterContext* context, uint32_t seed) {
    tfa_reset_typed((TfaState*)state, (TfaContext*)context, seed);
}

static void tfa_step(EncounterState* state, EncounterContext* context, const int* actions) {
    TfaState* s = (TfaState*)state;
    TfaContext* ctx = (TfaContext*)context;
    if (s->episode_over) return;
    int all[TFA_MAX_PLAYERS * TFA_NUM_ACTION_HEADS];
    memcpy(all, actions, sizeof(int) * TFA_NUM_ACTION_HEADS);
    for (int p = 1; p < TFA_MAX_PLAYERS; p++) tfa_heuristic_actions(s, p, all + p * TFA_NUM_ACTION_HEADS);
    tfa_step_team(s, ctx, all);
    if (s->episode_over) tfa_log_episode(s, ctx);
}

static void tfa_write_obs(EncounterState* state, EncounterContext* context, float* obs) {
    tfa_write_obs_for((TfaState*)state, (TfaContext*)context, 0, obs);
}

static void tfa_write_mask(EncounterState* state, EncounterContext* context, float* mask) {
    unsigned char bytes[TFA_ACTION_MASK_SIZE];
    tfa_write_mask_for((TfaState*)state, (TfaContext*)context, 0, bytes);
    for (int i = 0; i < TFA_ACTION_MASK_SIZE; i++) mask[i] = (float)bytes[i];
}

static float tfa_get_reward(EncounterState* state, EncounterContext* context) {
    (void)context;
    return ((TfaState*)state)->players[0].reward;
}

static int tfa_is_terminal(EncounterState* state, EncounterContext* context) {
    (void)context;
    return ((TfaState*)state)->episode_over;
}

static int tfa_get_entity_count(EncounterState* state, EncounterContext* context) {
    (void)context;
    return ((TfaState*)state)->team_size;
}

static void* tfa_get_entity(EncounterState* state, EncounterContext* context, int index) {
    (void)context;
    TfaState* s = (TfaState*)state;
    return index < s->team_size ? &s->players[index].player : NULL;
}

static void tfa_render_npc(const TfaState* s, const TfaNpc* n, int slot, RenderEntity* out) {
    memset(out, 0, sizeof(*out));
    out->entity_type = ENTITY_NPC;
    out->npc_def_id = n->npc_id;
    out->npc_visible = 1;
    out->npc_size = n->size;
    out->npc_anim_id = n->anim_tick == s->tick ? n->anim_id : -1;
    out->x = n->x;
    out->y = n->y;
    out->dest_x = n->x + n->size / 2;
    out->dest_y = n->y + n->size / 2;
    out->current_hitpoints = n->max_hp > 0 ? n->hp : 1;
    out->base_hitpoints = n->max_hp > 0 ? n->max_hp : 1;
    out->hit_landed_this_tick = n->damage_taken_tick > 0;
    out->hit_damage = n->damage_taken_tick;
    out->npc_slot = slot;
    out->npc_instance_id = n->instance_id;
    out->render_movement_kind = RENDER_MOVEMENT_NORMAL;
    out->attack_target_entity_idx = n->target >= 0 ? n->target : -1;
}

static void tfa_fill_render_entities(
    EncounterState* state, EncounterContext* context, RenderEntity* out, int max_entities, int* count
) {
    (void)context;
    TfaState* s = (TfaState*)state;
    int n = 0;
    for (int p = 0; p < s->team_size && n < max_entities; p++)
        osrs_render_entity_from_player_entity(&s->players[p].player, &out[n++]);
    for (int i = 0; i < TFA_MAX_NPCS && n < max_entities; i++) {
        if (!s->npcs[i].active) continue;
        tfa_render_npc(s, &s->npcs[i], i, &out[n++]);
    }
    for (int p = 0; p < s->team_size; p++) {
        const TfaPlayer* tp = &s->players[p];
        out[p].attack_target_entity_idx = -1;
        if (!osrs_interaction_active(&tp->player.interaction)) continue;
        for (int e = s->team_size; e < n; e++)
            if (out[e].npc_slot == tp->player.interaction.target_slot) out[p].attack_target_entity_idx = e;
    }
    *count = n;
}

static void tfa_put_int(EncounterState* state, EncounterContext* context, const char* key, int value) {
    (void)state;
    TfaContext* ctx = (TfaContext*)context;
    if (strcmp(key, "team_size") == 0)
        ctx->config.team_size = encounter_require_int_range_config("tfa", key, value, 1, TFA_MAX_PLAYERS);
    else if (strcmp(key, "episode_mode") == 0)
        ctx->config.episode_mode = encounter_require_binary_config("tfa", key, value);
    else if (strcmp(key, "start_room") == 0)
        ctx->config.start_room = encounter_require_int_range_config("tfa", key, value, 0, TFA_ROOM_COUNT - 1);
    else if (strcmp(key, "path") == 0)
        ctx->config.path = encounter_require_int_range_config("tfa", key, value, 0, 7);
    else if (strcmp(key, "room_max_ticks") == 0)
        ctx->config.room_max_ticks = encounter_require_int_range_config("tfa", key, value, 1, 100000);
    else if (strcmp(key, "world_offset_x") == 0)
        ctx->world_offset_x = value;
    else if (strcmp(key, "world_offset_y") == 0)
        ctx->world_offset_y = value;
    else
        encounter_abort_unknown_config("tfa", "int", key);
}

static void tfa_put_float(EncounterState* state, EncounterContext* context, const char* key, float value) {
    (void)state;
    TfaRewardConfig* r = &((TfaContext*)context)->config.reward;
    if (value < 0.0f) {
        fprintf(stderr, "tfa %s must be >= 0, got %f\n", key, value);
        abort();
    }
    if (strcmp(key, "reward_room_clear") == 0) r->room_clear = value;
    else if (strcmp(key, "reward_raid_clear") == 0) r->raid_clear = value;
    else if (strcmp(key, "reward_damage_dealt") == 0) r->damage_dealt = value;
    else if (strcmp(key, "reward_damage_taken") == 0) r->damage_taken = value;
    else if (strcmp(key, "reward_death") == 0) r->death = value;
    else if (strcmp(key, "reward_wipe") == 0) r->wipe = value;
    else encounter_abort_unknown_config("tfa", "float", key);
}

static void* tfa_get_log(EncounterState* state, EncounterContext* context) {
    (void)state;
    return &((TfaContext*)context)->log;
}

static int tfa_get_tick(EncounterState* state, EncounterContext* context) {
    (void)context;
    return ((TfaState*)state)->tick;
}

static int tfa_get_winner(EncounterState* state, EncounterContext* context) {
    (void)context;
    TfaState* s = (TfaState*)state;
    return s->outcome == TFA_OUTCOME_CLEARED ? 0 : s->episode_over ? 1 : -1;
}

static void tfa_render_post_tick(EncounterState* state, EncounterContext* context, EncounterOverlay* ov) {
    (void)context;
    TfaState* s = (TfaState*)state;
    for (int i = 0; i < TFA_MAX_HAZARDS && ov->hazard_count < ENCOUNTER_MAX_OVERLAY_TILES; i++) {
        const TfaHazard* h = &s->hazards[i];
        if (h->kind == TFA_HAZ_NONE) continue;
        ov->hazards[ov->hazard_count].x = h->x;
        ov->hazards[ov->hazard_count].y = h->y;
        ov->hazards[ov->hazard_count].active = 1;
        ov->hazard_count++;
    }
}

#define TFA_ENCOUNTER_FIELDS \
    .obs_size = TFA_NUM_OBS, \
    .num_action_heads = TFA_NUM_ACTION_HEADS, \
    .action_head_dims = TFA_ACTION_DIMS, \
    .mask_size = TFA_ACTION_MASK_SIZE, \
    .state_size = sizeof(TfaState), \
    .context_size = sizeof(TfaContext), \
    .destroy_context = tfa_destroy_context, \
    .init_state = tfa_init_state, \
    .finalize_context = tfa_finalize_context, \
    .create = tfa_create, \
    .destroy = tfa_destroy, \
    .reset = tfa_reset, \
    .step = tfa_step, \
    .write_obs = tfa_write_obs, \
    .write_mask = tfa_write_mask, \
    .get_reward = tfa_get_reward, \
    .is_terminal = tfa_is_terminal, \
    .get_entity_count = tfa_get_entity_count, \
    .get_entity = tfa_get_entity, \
    .fill_render_entities = tfa_fill_render_entities, \
    .put_int = tfa_put_int, \
    .put_float = tfa_put_float, \
    .arena_base_x = 0, \
    .arena_base_y = 0, \
    .arena_width = TFA_ARENA_MAX, \
    .arena_height = TFA_ARENA_MAX, \
    .head_move = OSRS_HEAD_PRIMARY, \
    .head_prayer = OSRS_HEAD_OVERHEAD, \
    .head_target = -1, \
    .render_post_tick = tfa_render_post_tick, \
    .get_log = tfa_get_log, \
    .get_tick = tfa_get_tick, \
    .get_winner = tfa_get_winner

static void tfa_init_context_room(EncounterContext* context, int room) {
    tfa_init_context(context);
    ((TfaContext*)context)->config.start_room = room;
}

#define TFA_ROOM_DEF(ROOM, NAME, DISPLAY, SCENE, OX, OY) \
    static void tfa_init_context_##NAME(EncounterContext* context) { tfa_init_context_room(context, ROOM); } \
    static const EncounterDef ENCOUNTER_TFA_##NAME = { \
        .name = "tfa_" #NAME, .display_name = DISPLAY, TFA_ENCOUNTER_FIELDS, \
        .init_context = tfa_init_context_##NAME, \
        .scene = SCENE, .npc_pack = SCENE, .scene_origin_x = OX, .scene_origin_y = OY, \
    };

TFA_ROOM_DEF(TFA_ROOM_GUARDIAN, guardian, "TFA: Archive Guardian", "tfa_guardian", 4096, 5568)
TFA_ROOM_DEF(TFA_ROOM_BORK, bork, "TFA: Bork and the Smorks", "tfa_bork", 4160, 5568)
TFA_ROOM_DEF(TFA_ROOM_EZEKIEL, ezekiel, "TFA: Ezekiel", "tfa_ezekiel", 4032, 5504)
TFA_ROOM_DEF(TFA_ROOM_DREADNOTH, dreadnoth, "TFA: Dreadnoth the Jellied", "tfa_dreadnoth", 4160, 5632)
TFA_ROOM_DEF(TFA_ROOM_MOIA, moia, "TFA: Moia", "tfa_moia", 4032, 5632)
TFA_ROOM_DEF(TFA_ROOM_XARTH, xarth, "TFA: Xarth", "tfa_xarth", 4160, 5504)
TFA_ROOM_DEF(TFA_ROOM_DAGONHAI, dagonhai, "TFA: Dagon'hai Trio", "tfa_dagonhai", 4032, 5568)
TFA_ROOM_DEF(TFA_ROOM_OCELLUS, ocellus, "TFA: Ocellus", "tfa_ocellus", 4096, 5632)

/* The wrapper's definition: config decides the room, no scene of its own. */
static const EncounterDef ENCOUNTER_TFA = {
    .name = "tfa", .display_name = "The Fractured Archive", TFA_ENCOUNTER_FIELDS,
    .init_context = tfa_init_context,
};


__attribute__((constructor))
static void tfa_register(void) {
    encounter_register(&ENCOUNTER_TFA);
    encounter_register(&ENCOUNTER_TFA_guardian);
    encounter_register(&ENCOUNTER_TFA_bork);
    encounter_register(&ENCOUNTER_TFA_ezekiel);
    encounter_register(&ENCOUNTER_TFA_dreadnoth);
    encounter_register(&ENCOUNTER_TFA_moia);
    encounter_register(&ENCOUNTER_TFA_xarth);
    encounter_register(&ENCOUNTER_TFA_dagonhai);
    encounter_register(&ENCOUNTER_TFA_ocellus);
}
