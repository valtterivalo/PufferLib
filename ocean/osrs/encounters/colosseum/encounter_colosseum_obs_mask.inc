#line 76 "encounter_colosseum.h"
/**
 * @file encounter_colosseum_obs_mask.inc
 * @brief Obs writer + action mask + render entities, mirroring inferno obs_mask.
 *
 * Obs layout (flat): player block + per-NPC slots + modifier flags + wave block
 * + boss block. col_write_obs_ctx hard-asserts the running index matches the
 * section constants, exactly like inferno.
 */

/* obs section sizes */
#define COLO_PLAYER_OBS_SIZE     20
#define COLO_FEATURES_PER_NPC    8
#define COLO_NPC_OBS_SIZE        (COLO_OBS_NPCS * COLO_FEATURES_PER_NPC)  /* 96 */
#define COLO_MODIFIER_OBS_SIZE   COLO_NUM_MODIFIERS                       /* 16 */
#define COLO_WAVE_OBS_SIZE       4
#define COLO_BOSS_OBS_SIZE       8
#define COLO_FEATURES_PER_HIT    5
#define COLO_PENDING_HIT_OBS_SIZE (COLO_FEATURES_PER_HIT * ENCOUNTER_MAX_PENDING_HITS)
#define COLO_NUM_OBS (COLO_PLAYER_OBS_SIZE + COLO_NPC_OBS_SIZE + COLO_MODIFIER_OBS_SIZE + \
    COLO_WAVE_OBS_SIZE + COLO_BOSS_OBS_SIZE + COLO_PENDING_HIT_OBS_SIZE)


/** assign each active NPC to a fixed obs slot in array order. */
static void col_refresh_current_obs_slots_ctx(ColosseumState* s, const ColosseumContext* ctx) {
    (void)ctx;
    for (int i = 0; i < COLO_OBS_NPCS; i++) s->current_obs_slots[i] = -1;
    int slot = 0;
    for (int i = 0; i < COLO_MAX_NPCS && slot < COLO_OBS_NPCS; i++) {
        if (s->npcs[i].active) s->current_obs_slots[slot++] = i;
    }
}

static int col_obs_slot_is_targetable(const ColosseumState* s, int obs_slot) {
    int npc_idx = s->current_obs_slots[obs_slot];
    if (npc_idx < 0 || npc_idx >= COLO_MAX_NPCS) return 0;
    const ColoNPC* npc = &s->npcs[npc_idx];
    return npc->active && npc->death_ticks == 0;
}


static void col_write_obs_ctx(EncounterState* state, EncounterContext* context, float* obs) {
    ColosseumState* s = (ColosseumState*)state;
    ColosseumContext* ctx = context ? (ColosseumContext*)context : col_legacy_context();
    col_refresh_current_obs_slots_ctx(s, ctx);
    memset(obs, 0, COLO_NUM_OBS * sizeof(float));
    int i = 0;
    int px = s->player.x, py = s->player.y;
    const EncounterLoadoutStats* ls = &s->loadout_stats[s->weapon_set];

    /* player block (20) */
    obs[i++] = (float)s->player.current_hitpoints / 99.0f;
    obs[i++] = (float)(px - COLO_ARENA_MIN_X) / (float)COLO_ARENA_WIDTH;
    obs[i++] = (float)(COLO_ARENA_MAX_X - px) / (float)COLO_ARENA_WIDTH;
    obs[i++] = (float)(py - COLO_ARENA_MIN_Y) / (float)COLO_ARENA_HEIGHT;
    obs[i++] = (float)(COLO_ARENA_MAX_Y - py) / (float)COLO_ARENA_HEIGHT;
    obs[i++] = (s->player.prayer == PRAYER_PROTECT_MELEE) ? 1.0f : 0.0f;
    obs[i++] = (s->player.prayer == PRAYER_PROTECT_RANGED) ? 1.0f : 0.0f;
    obs[i++] = (s->player.prayer == PRAYER_PROTECT_MAGIC) ? 1.0f : 0.0f;
    obs[i++] = (float)s->player.current_prayer / 99.0f;
    obs[i++] = (s->weapon_set == COLO_GEAR_MELEE) ? 1.0f : 0.0f;
    obs[i++] = (s->weapon_set == COLO_GEAR_RANGED) ? 1.0f : 0.0f;
    obs[i++] = (float)s->player.attack_timer / 8.0f;
    obs[i++] = (float)s->player.potion_timer / 3.0f;
    obs[i++] = (float)s->player.special_energy / 100.0f;
    obs[i++] = osrs_interaction_active(&s->interaction) ? 1.0f : 0.0f;
    obs[i++] = (float)ls->attack_range / 15.0f;
    obs[i++] = (float)ls->max_hit / 80.0f;
    obs[i++] = (float)ls->attack_speed / 6.0f;
    obs[i++] = (float)s->player.current_defence / 99.0f;
    obs[i++] = (float)s->player.run_energy / 10000.0f;
    assert(i == COLO_PLAYER_OBS_SIZE);

    /* per-NPC slots (COLO_OBS_NPCS * 8) */
    for (int n = 0; n < COLO_OBS_NPCS; n++) {
        int npc_idx = s->current_obs_slots[n];
        if (npc_idx < 0 || npc_idx >= COLO_MAX_NPCS || !s->npcs[npc_idx].active) {
            i += COLO_FEATURES_PER_NPC;
            continue;
        }
        const ColoNPC* npc = &s->npcs[npc_idx];
        obs[i++] = (float)(npc->type + 1) / (float)COLO_NUM_NPC_TYPES;
        obs[i++] = (float)(npc->x - px) / (float)COLO_ARENA_WIDTH;
        obs[i++] = (float)(npc->y - py) / (float)COLO_ARENA_HEIGHT;
        obs[i++] = npc->max_hp > 0 ? (float)npc->hp / (float)npc->max_hp : 0.0f;
        obs[i++] = (npc->attack_style == ATTACK_STYLE_MELEE) ? 1.0f : 0.0f;
        obs[i++] = (npc->attack_style == ATTACK_STYLE_RANGED) ? 1.0f : 0.0f;
        obs[i++] = (npc->attack_style == ATTACK_STYLE_MAGIC) ? 1.0f : 0.0f;
        obs[i++] = (float)npc->attack_timer / 8.0f;
    }
    assert(i == COLO_PLAYER_OBS_SIZE + COLO_NPC_OBS_SIZE);

    /* active modifier flags (COLO_NUM_MODIFIERS) */
    for (int m = 0; m < COLO_NUM_MODIFIERS; m++)
        obs[i++] = (s->modifiers.active_mask & (1u << m)) ? 1.0f : 0.0f;
    assert(i == COLO_PLAYER_OBS_SIZE + COLO_NPC_OBS_SIZE + COLO_MODIFIER_OBS_SIZE);

    /* wave block (4) */
    obs[i++] = (float)s->wave / (float)COLO_NUM_WAVES;
    obs[i++] = (float)s->reinforcement_timer / (float)COLO_REINFORCEMENT_TICKS;
    obs[i++] = s->modifiers.draft_pending ? 1.0f : 0.0f;
    obs[i++] = (s->wave == COLO_WAVE_BOSS) ? 1.0f : 0.0f;
    assert(i == COLO_PLAYER_OBS_SIZE + COLO_NPC_OBS_SIZE + COLO_MODIFIER_OBS_SIZE +
        COLO_WAVE_OBS_SIZE);

    /* boss block (8) */
    obs[i++] = (float)s->sol.phase / 6.0f;
    obs[i++] = (float)s->sol.combo_len / (float)COLO_SOL_COMBO_QUEUE_MAX;
    obs[i++] = (float)s->sol.combo_cursor / (float)COLO_SOL_COMBO_QUEUE_MAX;
    obs[i++] = s->sol.parry_sequence_active ? 1.0f : 0.0f;
    obs[i++] = (float)s->sol.parry_ticks_remaining / 8.0f;
    obs[i++] = s->sol.grapple_active ? 1.0f : 0.0f;
    obs[i++] = (float)s->sol.grapple_timer / 4.0f;
    obs[i++] = (float)s->sol.hazard_tile_count / (float)COLO_SOL_HAZARD_TILES_MAX;
    assert(i == COLO_PLAYER_OBS_SIZE + COLO_NPC_OBS_SIZE + COLO_MODIFIER_OBS_SIZE +
        COLO_WAVE_OBS_SIZE + COLO_BOSS_OBS_SIZE);

    /* pending hits on player (COLO_FEATURES_PER_HIT * ENCOUNTER_MAX_PENDING_HITS) */
    for (int h = 0; h < ENCOUNTER_MAX_PENDING_HITS; h++) {
        if (h < s->player_pending_hits.count) {
            const EncounterPendingHit* ph = &s->player_pending_hits.hits[h];
            obs[i++] = ph->active ? 1.0f : 0.0f;
            obs[i++] = (ph->attack_style == ATTACK_STYLE_RANGED) ? 1.0f : 0.0f;
            obs[i++] = (ph->attack_style == ATTACK_STYLE_MAGIC) ? 1.0f : 0.0f;
            obs[i++] = (float)ph->ticks_remaining / 10.0f;
            obs[i++] = (float)ph->damage / 50.0f;
        } else {
            i += COLO_FEATURES_PER_HIT;
        }
    }
    if (i != COLO_NUM_OBS) {
        fprintf(stderr, "colosseum obs index mismatch: wrote %d, expected %d\n", i, COLO_NUM_OBS);
        abort();
    }
}

static void col_write_obs(EncounterState* state, float* obs) {
    col_write_obs_ctx(state, (EncounterContext*)col_legacy_context(), obs);
}


static void col_write_mask_ctx(EncounterState* state, EncounterContext* context, float* mask) {
    ColosseumState* s = (ColosseumState*)state;
    ColosseumContext* ctx = context ? (ColosseumContext*)context : col_legacy_context();
    col_refresh_current_obs_slots_ctx(s, ctx);
    int offset = 0;

    /* HEAD_MOVE (25): idle always valid, walk/run valid iff target in-arena */
    mask[offset++] = 1.0f;
    for (int d = 1; d < ENCOUNTER_MOVE_ACTIONS; d++) {
        int nx = s->player.x + ENCOUNTER_MOVE_TARGET_DX[d];
        int ny = s->player.y + ENCOUNTER_MOVE_TARGET_DY[d];
        mask[offset++] = col_in_arena(nx, ny) ? 1.0f : 0.0f;
    }

    /* HEAD_PRAYER (5): no_change, off, set melee/ranged/magic */
    mask[offset++] = 1.0f;
    mask[offset++] = s->player.prayer != PRAYER_NONE ? 1.0f : 0.0f;
    mask[offset++] = s->player.current_prayer > 0 ? 1.0f : 0.0f;
    mask[offset++] = s->player.current_prayer > 0 ? 1.0f : 0.0f;
    mask[offset++] = s->player.current_prayer > 0 ? 1.0f : 0.0f;

    /* HEAD_TARGET (COLO_OBS_NPCS+1): none valid, slot valid iff targetable */
    mask[offset++] = 1.0f;
    for (int n = 0; n < COLO_OBS_NPCS; n++)
        mask[offset++] = col_obs_slot_is_targetable(s, n) ? 1.0f : 0.0f;

    /* HEAD_GEAR (3): no_switch, melee, ranged */
    mask[offset++] = 1.0f;
    mask[offset++] = (s->weapon_set != COLO_GEAR_MELEE) ? 1.0f : 0.0f;
    mask[offset++] = (s->weapon_set != COLO_GEAR_RANGED) ? 1.0f : 0.0f;

    /* HEAD_EAT (2): none, eat */
    mask[offset++] = 1.0f;
    mask[offset++] = (s->player.food_count > 0 && s->player.food_timer == 0 &&
                      s->player.current_hitpoints < s->player.base_hitpoints) ? 1.0f : 0.0f;

    /* HEAD_POTION (2): none, drink */
    mask[offset++] = 1.0f;
    mask[offset++] = (s->player.restore_doses > 0 && s->player.potion_timer == 0) ? 1.0f : 0.0f;

    /* HEAD_SPEC (2): no_change, toggle */
    mask[offset++] = 1.0f;
    mask[offset++] = (s->player.special_energy >= 25) ? 1.0f : 0.0f;

    /* HEAD_MODIFIER_SELECT (4): none + pick option 0/1/2, valid only during a draft */
    mask[offset++] = 1.0f;
    for (int o = 0; o < COLO_MODIFIER_DRAFT_OPTIONS; o++)
        mask[offset++] = (s->modifiers.draft_pending && s->modifiers.draft_options[o] >= 0)
            ? 1.0f : 0.0f;

    if (offset != COLO_ACTION_MASK_SIZE) {
        fprintf(stderr, "colosseum mask index mismatch: wrote %d, expected %d\n",
            offset, COLO_ACTION_MASK_SIZE);
        abort();
    }
}

static void col_write_mask(EncounterState* state, float* mask) {
    col_write_mask_ctx(state, (EncounterContext*)col_legacy_context(), mask);
}


static float col_get_reward_ctx(EncounterState* state, EncounterContext* context) {
    (void)context;
    return ((ColosseumState*)state)->reward;
}

static int col_is_terminal_ctx(EncounterState* state, EncounterContext* context) {
    (void)context;
    return ((ColosseumState*)state)->episode_over;
}

static int col_get_entity_count_ctx(EncounterState* state, EncounterContext* context) {
    (void)context;
    ColosseumState* s = (ColosseumState*)state;
    int count = 1;
    for (int i = 0; i < COLO_MAX_NPCS; i++)
        if (s->npcs[i].active) count++;
    return count;
}

static void* col_get_entity_ctx(EncounterState* state, EncounterContext* context, int index) {
    (void)context;
    ColosseumState* s = (ColosseumState*)state;
    if (index == 0) return &s->player;
    return NULL;
}

/** populate render entities (player + active NPCs). */
static void col_fill_render_entities_ctx(
    EncounterState* state,
    EncounterContext* context,
    RenderEntity* out,
    int max_entities,
    int* count
) {
    (void)context;
    ColosseumState* s = (ColosseumState*)state;
    const EncounterLoadoutStats* ls = &s->loadout_stats[s->weapon_set];
    s->player.gui_max_hit = ls->max_hit;
    s->player.gui_attack_speed = ls->attack_speed;
    s->player.gui_attack_range = ls->attack_range;
    s->player.gui_strength_bonus = ls->strength_bonus;

    int n = 0;
    if (n < max_entities)
        osrs_render_entity_from_player_entity(&s->player, &out[n++]);

    for (int i = 0; i < COLO_MAX_NPCS && n < max_entities; i++) {
        ColoNPC* npc = &s->npcs[i];
        if (!npc->active) continue;
        RenderEntity* re = &out[n++];
        osrs_render_entity_from_npc_spec(&(OsrsNpcRenderEntitySpec){
            .npc_def_id = COLO_NPC_DEF_IDS[npc->type],
            .npc_slot = i,
            .npc_instance_id = npc->render_id,
            .npc_visible = npc->active,
            .npc_size = npc->size,
            .npc_anim_id = -1,
            .x = npc->x,
            .y = npc->y,
            .dest_x = npc->target_x,
            .dest_y = npc->target_y,
            .current_hitpoints = npc->hp,
            .base_hitpoints = npc->max_hp,
            .attack_style_this_tick = npc->attacked_this_tick
                ? npc->attack_style_this_tick : ATTACK_STYLE_NONE,
            .hit_landed_this_tick = npc->hit_landed_this_tick,
            .hit_damage = npc->hit_damage,
            .hit_was_successful = npc->hit_was_successful_this_tick,
            .hit_spell_type = npc->hit_spell_type,
            .attack_target_entity_idx = 0,
        }, re);
    }

    encounter_resolve_attack_target(out, n, s->interaction.target_slot);
    *count = n;
}
