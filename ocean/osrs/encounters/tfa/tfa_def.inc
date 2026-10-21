/* P0's supplies at the first room: Saradomin brews 12, super restores 6 (21 of 24 Guardian starts), one anglerfish. One brew fewer here keeps
   a cell free for the shield a two-handed switch unequips. */
static const struct { uint16_t raw_osrs_id; int count; } TFA_SUPPLIES[] = {
    { 6685, 11 },
    { 3024, 6 },
    { 13441, 1 },
};

/* Boosted levels on the recorded menu actions: Defence 120 and 119 most often, Attack and Strength 118 (super combat), Ranged 112 (divine
   ranging), Magic unboosted (14054 of 18980). */
#define TFA_BOOSTED_MELEE 118
#define TFA_BOOSTED_DEFENCE 120
#define TFA_BOOSTED_RANGED 112

static void tfa_player_reset(TfaState* s, int p) {
    TfaPlayer* tp = &s->players[p];
    memset(tp, 0, sizeof(*tp));
    Player* pl = &tp->player;
    pl->entity_type = ENTITY_PLAYER;
    memset(pl->equipped, ITEM_NONE, NUM_GEAR_SLOTS * sizeof(uint16_t));
    encounter_init_maxed_player_combat_stats(pl, TFA_PLAYER_PRAYER);
    pl->special_energy = 100;
    osrs_item_effect_state_init(&pl->item_effect_state);
    osrs_interaction_init(&pl->interaction);
    tp->kit = p == 0 ? TFA_KIT_CROSSBOW : TFA_KIT_BOW;
    pl->current_attack = pl->current_strength = TFA_BOOSTED_MELEE;
    pl->current_defence = TFA_BOOSTED_DEFENCE;
    pl->current_ranged = TFA_BOOSTED_RANGED;
    encounter_apply_loadout(pl, TFA_KIT_WORN[tp->kit], GEAR_RANGED);
    int cell = 0;
    for (size_t k = 0; k < sizeof(TFA_KIT_MELEE) / sizeof(*TFA_KIT_MELEE); k++) pl->inventory_cells[cell++] = osrs_inventory_cell_from_item(TFA_KIT_MELEE[k]);
    for (size_t k = 0; k < sizeof(TFA_KIT_MAGIC) / sizeof(*TFA_KIT_MAGIC); k++) pl->inventory_cells[cell++] = osrs_inventory_cell_from_item(TFA_KIT_MAGIC[k]);
    for (int k = 0; k < 6 && TFA_KIT_SWITCHES[tp->kit][k] != ITEM_NONE; k++)
        pl->inventory_cells[cell++] = osrs_inventory_cell_from_item(TFA_KIT_SWITCHES[tp->kit][k]);
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
    int room = ctx->config.start_room >= 0
        ? ctx->config.start_room
        : TFA_IMPLEMENTED_ROOMS[tfa_rand(s, TFA_NUM_IMPLEMENTED_ROOMS)];
    s->raid_index = 0;
    if (s->episode_mode == TFA_EPISODE_RAID) room = tfa_raid_room(0, s->path);
    tfa_room_enter(s, room);
    memset(s->danger, -1, sizeof(s->danger));
    tfa_danger_build(s);
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

/* Raider 0 follows the viewer's clicks: a walk heads for the shared route's next run step to the clicked tile. */
static void tfa_step_human_commands(EncounterState* state, EncounterContext* context, HumanInput* hi) {
    TfaState* s = (TfaState*)state;
    TfaPlayer* tp = &s->players[0];
    int acts[TFA_NUM_ACTION_HEADS] = {0};
    if (hi->pending_move_x >= 0) {
        EncounterRouteInput walk = {
            .topology = ((TfaContext*)context)->topology[s->room],
            .source_x = tp->player.x, .source_y = tp->player.y, .actor_size = 1,
            .target_x = hi->pending_move_x, .target_y = hi->pending_move_y, .target_size = 1,
            .target_kind = ENCOUNTER_ROUTE_TARGET_TILE,
            .movement_mode = tp->weary_ticks > 0 ? ENCOUNTER_ROUTE_MOVEMENT_WALK : ENCOUNTER_ROUTE_MOVEMENT_RUN,
            .cost_policy = ENCOUNTER_ROUTE_COST_SOUTH_FIRST_BFS,
        };
        EncounterRouteResult route = encounter_route_solve(&walk);
        int dx = route.first_dx + route.run_dx, dy = route.first_dy + route.run_dy;
        for (int a = 1; a < OSRS_PRIMARY_MOVE_ACTIONS; a++)
            if (ENCOUNTER_MOVE_TARGET_DX[a] == dx && ENCOUNTER_MOVE_TARGET_DY[a] == dy) acts[OSRS_HEAD_PRIMARY] = a;
        if (route.outcome == ROUTE_UNREACHABLE || !acts[OSRS_HEAD_PRIMARY]) human_input_clear_move(hi);
    }
    for (int i = 0; i < hi->commands.count; i++) {
        const HumanCommand* c = &hi->commands.items[i];
        if (c->kind == HUMAN_COMMAND_ATTACK_NPC || c->kind == HUMAN_COMMAND_SPELL_TARGET) {
            acts[OSRS_HEAD_PRIMARY] = TFA_PRIMARY_ATTACK_BASE + c->npc_slot;
            if (c->kind == HUMAN_COMMAND_SPELL_TARGET && c->spell == OSRS_SPELL_ICE_BARRAGE) acts[OSRS_HEAD_SPELL] = OSRS_SPELL_ICE_BARRAGE;
        } else if (c->kind == HUMAN_COMMAND_EQUIP_INVENTORY_ITEM || c->kind == HUMAN_COMMAND_INVENTORY_PRIMARY_CLICK ||
                c->kind == HUMAN_COMMAND_EAT || c->kind == HUMAN_COMMAND_DRINK) {
            osrs_base_action_inventory_click(acts, &tp->player, c->inventory_slot);
        }
    }
    encounter_translate_prayer(hi, acts, OSRS_HEAD_OVERHEAD);
    encounter_translate_offensive_prayer(hi, acts, OSRS_HEAD_OFFENSIVE);
    if (hi->pending_spec) acts[OSRS_HEAD_SPECIAL] = tp->player.spec_armed ? 2 : 1;
    tfa_step(state, context, acts);
    if (tp->player.x == hi->pending_move_x && tp->player.y == hi->pending_move_y) human_input_clear_move(hi);
    human_input_clear_pending(hi);
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
        if (!s->npcs[i].active || s->npcs[i].kind == TFA_NPC_OCELLUS_PORTAL) continue;
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
        ctx->config.start_room = encounter_require_int_range_config("tfa", key, value, -1, TFA_ROOM_COUNT - 1);
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

static const struct { int gfx; int anim; } TFA_HAZARD_SPOTANIMS[] = {
    { XARTH_GFX_AOE, SPOTANIM_TFA_XARTH_VFX_XARTH_AOE_HITANIM_01_ANIM },
    { OC_GFX_DOT, SPOTANIM_TFA_OCELLUS_VFX_OCELLUS_P2_AOE_DODGE_ANIM },
    { OC_GFX_BOMB, SPOTANIM_TFA_OCELLUS_VFX_OCELLUS_QUADRANT_BOMB_IDLE_01_ANIM },
    { OC_GFX_SLASH, SPOTANIM_TFA_OCELLUS_VFX_OCELLUS_P3_SHADOW_SLASH_SPOTANIM01_ANIM },
    { OC_GFX_INSOLENCE, SPOTANIM_TFA_OCELLUS_VFX_OCELLUS_P3_SHADOW_SLASH_SUPERIOR_SPOTANIM01_ANIM },
    { OC_GFX_SLAM, SPOTANIM_TFA_OCELLUS_VFX_OCELLUS_UNLEASH_ARENA_270_ANIM },
    { OC_GFX_DEBRIS, SPOTANIM_TFA_OCELLUS_VFX_OCELLUS_FALLING_DEBRIS_ANIM },
    { OC_GFX_SWORD_DROP, SPOTANIM_TFA_OCELLUS_TFA_ARENA_SWORD_DROP_EXPLOSION_ANIM },
};

static int tfa_hazard_spotanim_anim(int gfx) {
    for (size_t i = 0; i < sizeof(TFA_HAZARD_SPOTANIMS) / sizeof(*TFA_HAZARD_SPOTANIMS); i++)
        if (TFA_HAZARD_SPOTANIMS[i].gfx == gfx) return TFA_HAZARD_SPOTANIMS[i].anim;
    fprintf(stderr, "tfa hazard gfx %d has no spotanim\n", gfx);
    abort();
}

/* Yaw of an OSRS orientation (0, 512, 1024, 1536 for locs; spotanim rotation degrees * 2048 / 360), turned the same way as the
   renderer turns spotanim rotation. */
static float tfa_orientation_yaw(int orientation) {
    return (float)(orientation * 2.0 * TFA_PI / 2048.0);
}

/* One model at the centre of a hazard's rect: the live game draws a few spotanims per area (4262 ~10 per explosion, 4320 one per slam), never one per tile. */
static void tfa_emit_hazard_model(EncounterOverlay* ov, const TfaHazard* h, uint32_t model, int anim) {
    int ax = h->x + (h->w - 1) / 2, ay = h->y + (h->h - 1) / 2;
    encounter_emit_floating_model(ov, ENCOUNTER_PROJECTILE_TARGET_FIXED, -1, ax, ay, model, anim, 0.0f,
        (float)h->x + h->w * 0.5f - (ax + 0.5f), 1.0f);
    ov->floating_models[ov->floating_model_count - 1].depth_offset = (float)h->y + h->h * 0.5f - (ay + 0.5f);
    ov->floating_models[ov->floating_model_count - 1].yaw = tfa_orientation_yaw(h->orientation);
}

/* Hazard spotanims and locs, Ocellus's safe portals, lit lane portals and lane fire, and this tick's NPC projectiles. Tile-only hazards draw nothing: their telegraph is an NPC (Guardian vines). Durations are client cycles. */
static void tfa_render_post_tick(EncounterState* state, EncounterContext* context, EncounterOverlay* ov) {
    (void)context;
    TfaState* s = (TfaState*)state;
    ov->floating_model_count = 0;
    ov->projectile_count = 0;
    ov->ground_spotanim_count = 0;
    if (s->room == TFA_ROOM_OCELLUS) oc_scenery(s, ov);
    for (int i = 0; i < TFA_MAX_NPCS; i++) {
        if (!s->npcs[i].active || s->npcs[i].kind != TFA_NPC_OCELLUS_PORTAL) continue;
        uint32_t model;
        int anim;
        oc_portal_visual(s, &s->npcs[i], &model, &anim);
        encounter_emit_floating_model(ov, ENCOUNTER_PROJECTILE_TARGET_FIXED, -1, s->npcs[i].x, s->npcs[i].y, model, anim, 0.0f, 0.0f, 1.0f);
    }
    for (int i = 0; i < s->hazard_hi; i++) {
        const TfaHazard* h = &s->hazards[i];
        if (h->kind == TFA_HAZ_NONE) continue;
        int px, py, turned;
        if (h->loc_id == OC_LOC_LANE_PORTAL) {
            if (!oc_lane_portal(h, &px, &py, &turned)) continue;
            encounter_emit_floating_model(ov, ENCOUNTER_PROJECTILE_TARGET_FIXED, -1, px, py,
                OSRS_LOC_MODEL_BASE | OC_LOC_LANE_PORTAL, -1, 0.0f, turned ? 0.0f : 0.5f, 1.0f);
            ov->floating_models[ov->floating_model_count - 1].depth_offset = turned ? 0.5f : 0.0f;
            ov->floating_models[ov->floating_model_count - 1].yaw = tfa_orientation_yaw(turned ? 512 : 0);
            if (h->start_tick != s->tick + 1) continue;
            OsrsProjectileEventSpec shot = { .start_h = 40, .end_h = 40, .src_size = 1, .dst_size = 1, .start_delay = 30,
                .model_id = SPOTANIM_TFA_OCELLUS_VFX_OCELLUS_PORTAL_PROJECTILE, .anim_id = SPOTANIM_TFA_OCELLUS_VFX_OCELLUS_PORTAL_PROJECTILE_ANIM };
            oc_lane_shot(h, &shot.src_x, &shot.src_y, &shot.dst_x, &shot.dst_y, &shot.duration_ticks);
            osrs_emit_projectile_with_spec(ov, &shot, 0);
        } else if (h->loc_id) {
            tfa_emit_hazard_model(ov, h, OSRS_LOC_MODEL_BASE | (uint32_t)h->loc_id, -1);
        } else if (h->gfx_id) {
            tfa_emit_hazard_model(ov, h, OSRS_SPOTANIM_MODEL_BASE | (uint32_t)h->gfx_id, tfa_hazard_spotanim_anim(h->gfx_id));
        }
    }
    for (int i = 0; i < s->fx_count; i++) {
        const TfaFx* fx = &s->fx[i];
        if (fx->kind == TFA_FX_GROUND) {
            encounter_emit_ground_spotanim(ov, fx->x, fx->y, fx->gfx_id, fx->delay);
            continue;
        }
        const TfaNpc* src = &s->npcs[fx->src_slot];
        const Player* dst = &s->players[fx->dst_player].player;
        OsrsProjectileEventSpec spec = {
            .src_x = src->x, .src_y = src->y, .dst_x = dst->x, .dst_y = dst->y,
            .duration_ticks = fx->duration * 30,
            .start_delay = fx->delay * 30,
            .start_h = 100, .end_h = 40, .curve = 16,
            .src_size = src->size, .dst_size = 1,
            .model_id = fx->model,
            .anim_id = fx->anim,
        };
        int idx = osrs_emit_projectile_with_spec(ov, &spec, fx->dst_player == 0);
        encounter_set_projectile_source_npc_slot(ov, idx, fx->src_slot);
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
    .step_human_commands = tfa_step_human_commands, \
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
    ((TfaContext*)context)->config.team_size = 3; /* 58 of 69 recorded fights are trios */
}

#define TFA_ROOM_DEF(ROOM, NAME, DISPLAY, SCENE, OX, OY) \
    static void tfa_init_context_##NAME(EncounterContext* context) { tfa_init_context_room(context, ROOM); } \
    static const EncounterDef ENCOUNTER_TFA_##NAME = { \
        .name = "tfa_" #NAME, .display_name = DISPLAY, TFA_ENCOUNTER_FIELDS, \
        .init_context = tfa_init_context_##NAME, \
        .scene = SCENE, .npc_pack = SCENE, .scene_origin_x = OX, .scene_origin_y = OY, \
    };

TFA_ROOM_DEF(TFA_ROOM_GUARDIAN, guardian, "TFA: Archive Guardian", "tfa_guardian", TFA_SCENE_ORIGIN_X_GUARDIAN, TFA_SCENE_ORIGIN_Y_GUARDIAN)
TFA_ROOM_DEF(TFA_ROOM_BORK, bork, "TFA: Bork and the Smorks", "tfa_bork", TFA_SCENE_ORIGIN_X_BORK, TFA_SCENE_ORIGIN_Y_BORK)
TFA_ROOM_DEF(TFA_ROOM_EZEKIEL, ezekiel, "TFA: Ezekiel", "tfa_ezekiel", TFA_SCENE_ORIGIN_X_EZEKIEL, TFA_SCENE_ORIGIN_Y_EZEKIEL)
TFA_ROOM_DEF(TFA_ROOM_DREADNOTH, dreadnoth, "TFA: Dreadnoth the Jellied", "tfa_dreadnoth", TFA_SCENE_ORIGIN_X_DREADNOTH, TFA_SCENE_ORIGIN_Y_DREADNOTH)
TFA_ROOM_DEF(TFA_ROOM_MOIA, moia, "TFA: Moia", "tfa_moia", TFA_SCENE_ORIGIN_X_MOIA, TFA_SCENE_ORIGIN_Y_MOIA)
TFA_ROOM_DEF(TFA_ROOM_XARTH, xarth, "TFA: Xarth", "tfa_xarth", TFA_SCENE_ORIGIN_X_XARTH, TFA_SCENE_ORIGIN_Y_XARTH)
TFA_ROOM_DEF(TFA_ROOM_DAGONHAI, dagonhai, "TFA: Dagon'hai Trio", "tfa_dagonhai", TFA_SCENE_ORIGIN_X_DAGONHAI, TFA_SCENE_ORIGIN_Y_DAGONHAI)
TFA_ROOM_DEF(TFA_ROOM_OCELLUS, ocellus, "TFA: Ocellus", "tfa_ocellus", TFA_SCENE_ORIGIN_X_OCELLUS, TFA_SCENE_ORIGIN_Y_OCELLUS)

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
