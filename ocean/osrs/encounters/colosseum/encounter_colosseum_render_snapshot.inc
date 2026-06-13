#line 76 "encounter_colosseum.h"
/**
 * @file encounter_colosseum_render_snapshot.inc
 * @brief Config kwargs + snapshot/restore + the EncounterDef vtable.
 */

static void col_put_int_ctx(
    EncounterState* state,
    EncounterContext* context,
    const char* key,
    int value
) {
    ColosseumState* s = (ColosseumState*)state;
    ColosseumContext* ctx = context ? (ColosseumContext*)context : col_legacy_context();
    /* wave is 1-indexed externally, 0-indexed internally */
    if (strcmp(key, "start_wave") == 0) {
        int internal = value - 1;
        if (internal < 0) internal = 0;
        if (internal >= COLO_NUM_WAVES) internal = COLO_NUM_WAVES - 1;
        s->start_wave = internal;
        ctx->config.start_wave = internal;
    }
    else if (strcmp(key, "seed") == 0) s->rng_state = (uint32_t)value;
    else if (strcmp(key, "world_offset_x") == 0) ctx->world_offset_x = value;
    else if (strcmp(key, "world_offset_y") == 0) ctx->world_offset_y = value;
    else if (strcmp(key, "player_dest_x") == 0) s->player_dest_x = value;
    else if (strcmp(key, "player_dest_y") == 0) s->player_dest_y = value;
    else if (strcmp(key, "human_command_mode") == 0)
        ctx->human_command_mode = encounter_require_binary_config("colosseum", key, value);
    else if (strcmp(key, "terminal_penalty_enabled") == 0)
        ctx->config.terminal_penalty_enabled =
            encounter_require_binary_config("colosseum", key, value);
    else if (strcmp(key, "curriculum_agent") == 0)
        ctx->config.curriculum_agent =
            encounter_require_binary_config("colosseum", key, value);
    else if (strcmp(key, "step_out_forecast_obs_enabled") == 0)
        ctx->config.step_out_forecast_obs_enabled =
            encounter_require_binary_config("colosseum", key, value);
    else if (strcmp(key, "loadout_profile_mode") == 0) {
        if (value < 0 || value > COLO_LOADOUT_PROFILE_MODE_MIXED) {
            fprintf(stderr, "colosseum: loadout_profile_mode must be 0..2, got %d\n", value);
            abort();
        }
        ctx->config.loadout_profile_mode = value;
    }
    else encounter_abort_unknown_config("colosseum", "int", key);
}

static void col_put_float_ctx(
    EncounterState* state,
    EncounterContext* context,
    const char* key,
    float value
) {
    (void)state;
    ColosseumContext* ctx = context ? (ColosseumContext*)context : col_legacy_context();
    if (strcmp(key, "damage_reward_coeff") == 0) ctx->config.damage_reward_coeff = value;
    else if (strcmp(key, "boss_damage_reward_coeff") == 0)
        ctx->config.boss_damage_reward_coeff = value;
    else if (strcmp(key, "wave_clear_bonus") == 0) ctx->config.wave_clear_bonus = value;
    else if (strcmp(key, "boss_phase_bonus") == 0) ctx->config.boss_phase_bonus = value;
    else if (strcmp(key, "win_bonus") == 0) ctx->config.win_bonus = value;
    else if (strcmp(key, "prayer_correct_reward") == 0)
        ctx->config.prayer_correct_reward = value;
    else if (strcmp(key, "death_penalty_coeff") == 0) ctx->config.death_penalty_coeff = value;
    else if (strcmp(key, "beginner_loadout_fraction") == 0) {
        if (!(value >= 0.0f && value <= 1.0f)) {
            fprintf(stderr, "colosseum: beginner_loadout_fraction must be in [0,1], got %.6f\n",
                    value);
            abort();
        }
        ctx->config.beginner_loadout_fraction = value;
    }
    else encounter_abort_unknown_config("colosseum", "float", key);
}

static void col_put_ptr_ctx(
    EncounterState* state,
    EncounterContext* context,
    const char* key,
    void* value
) {
    (void)state;
    ColosseumContext* ctx = context ? (ColosseumContext*)context : col_legacy_context();
    if (strcmp(key, "collision_map") == 0) ctx->collision_map = (const CollisionMap*)value;
    else encounter_abort_unknown_config("colosseum", "ptr", key);
}

static int col_get_tick_ctx(EncounterState* state, EncounterContext* context) {
    (void)context;
    return ((ColosseumState*)state)->tick;
}

static int col_get_winner_ctx(EncounterState* state, EncounterContext* context) {
    (void)context;
    return ((ColosseumState*)state)->winner;
}

static void* col_get_log_ctx(EncounterState* state, EncounterContext* context) {
    ColosseumState* s = (ColosseumState*)state;
    (void)context;
    s->log.episode_return = s->episode_return;
    s->log.episode_length = s->tick;
    s->log.wave_reached = s->wave;
    s->log.win = (s->episode_over && s->winner == COLO_OUTCOME_PLAYER_WON) ? 1 : 0;
    s->log.died = (s->episode_over && s->winner == COLO_OUTCOME_PLAYER_DIED) ? 1 : 0;
    s->log.boss_phase_reached = s->sol.phase;
    s->log.active_tier_sum = col_modifier_active_tier_sum(s);
    return &s->log;
}

static void col_render_post_tick_ctx(
    EncounterState* state,
    EncounterContext* context,
    EncounterOverlay* ov
) {
    (void)context;
    ColosseumState* s = (ColosseumState*)state;
    ov->projectile_count = 0;
    encounter_write_terminal_status_text(
        s->episode_over,
        s->winner,
        COLO_OUTCOME_PLAYER_WON,
        "Colosseum cleared",
        colo_npc_type_name(s->last_hit_by_type),
        ov->status_text,
        sizeof(ov->status_text));
    ov->status_text_active = ov->status_text[0] != '\0';
}


/* ----- human mode --------------------------------------------------------- */
/* Maps semantic HumanInput intents onto the 9 Colosseum action heads. Mirrors
   inferno's translate/step pair: MOVE (25-dir walk) + TARGET (obs slot of the
   clicked NPC) drive attacking, plus prayer/gear/eat/potion/spec. The two
   Colosseum-only heads have no inferno analog: MODIFIER_SELECT picks a draft
   option and GRAPPLE_PARRY clicks a Sol grapple body slot. */

static void* col_get_player_for_input(void* state, int idx) {
    ColosseumState* s = (ColosseumState*)state;
    return (idx == 0) ? (void*)&s->player : NULL;
}

/** obs slot the agent sees for a raw NPC array index, or -1. Inverse of
    current_obs_slots, refreshed by the caller. */
static int col_find_target_obs_slot(const ColosseumState* s, int npc_slot) {
    return encounter_find_observed_target_slot(
        s->current_obs_slots, COLO_OBS_NPCS, npc_slot);
}

static int col_is_human_targetable_npc_slot_ctx(
    EncounterState* state,
    EncounterContext* context,
    int npc_slot
) {
    ColosseumState* s = (ColosseumState*)state;
    ColosseumContext* ctx = context ? (ColosseumContext*)context : col_legacy_context();
    col_refresh_current_obs_slots_ctx(s, ctx);
    int obs_slot = col_find_target_obs_slot(s, npc_slot);
    if (obs_slot < 0) return 0;
    return col_obs_slot_is_targetable(s, obs_slot);
}

static void col_translate_human_input_ctx(
    HumanInput* hi,
    int* actions,
    EncounterState* state,
    EncounterContext* context
) {
    for (int h = 0; h < COLO_NUM_ACTION_HEADS; h++) actions[h] = 0;

    encounter_translate_movement(hi, actions, COLO_HEAD_MOVE, col_get_player_for_input, state);
    encounter_translate_prayer(hi, actions, COLO_HEAD_PRAYER);

    ColosseumState* s = (ColosseumState*)state;
    ColosseumContext* ctx = context ? (ColosseumContext*)context : col_legacy_context();
    col_refresh_current_obs_slots_ctx(s, ctx);

    /* map raw pending_target_idx to the observation slot the agent sees */
    if (hi->pending_target_idx >= 0) {
        int found_slot = col_find_target_obs_slot(s, hi->pending_target_idx);
        actions[COLO_HEAD_TARGET] =
            (found_slot >= 0 && col_obs_slot_is_targetable(s, found_slot))
                ? found_slot + 1 : 0;
    } else {
        actions[COLO_HEAD_TARGET] = 0;
    }

    if (hi->pending_gear > 0) actions[COLO_HEAD_GEAR] = hi->pending_gear;
    /* the colosseum EAT head IS the brew (L14); potion intents map onto the
       researched 5-dim potion head; the spec key arms/disarms spec weapon A
       (B is agent/lab-reachable; no second human key yet). */
    if (hi->pending_food || hi->pending_potion == POTION_BREW)
        actions[COLO_HEAD_EAT] = 1;
    if (hi->pending_potion == POTION_RESTORE)
        actions[COLO_HEAD_POTION] = COLO_POTION_RESTORE;
    else if (hi->pending_potion == POTION_COMBAT)
        actions[COLO_HEAD_POTION] = COLO_POTION_COMBAT;
    else if (hi->pending_potion == POTION_RANGED)
        actions[COLO_HEAD_POTION] = COLO_POTION_RANGING;
    else if (hi->pending_potion == POTION_SURGE)
        actions[COLO_HEAD_POTION] = COLO_POTION_SURGE;
    if (hi->pending_spec) actions[COLO_HEAD_SPEC] = 1;
    encounter_translate_offensive_prayer(hi, actions, COLO_HEAD_OFFENSIVE);

    /* Colosseum-only heads: draft pick (0=none, 1..3) + grapple body slot. */
    if (hi->pending_modifier_select > 0 &&
            hi->pending_modifier_select <= COLO_MODIFIER_DRAFT_OPTIONS)
        actions[COLO_HEAD_MODIFIER_SELECT] = hi->pending_modifier_select;
    if (hi->pending_grapple_slot > 0 && hi->pending_grapple_slot <= COLO_NUM_GRAPPLE_SLOTS)
        actions[COLO_HEAD_GRAPPLE_PARRY] = hi->pending_grapple_slot;
}

static void col_translate_human_commands_ctx(
    HumanInput* hi,
    int* actions,
    ColosseumState* s,
    ColosseumContext* ctx
) {
    for (int h = 0; h < COLO_NUM_ACTION_HEADS; h++) actions[h] = 0;
    col_refresh_current_obs_slots_ctx(s, ctx);

    for (int i = 0; i < hi->commands.count; i++) {
        const HumanCommand* cmd = &hi->commands.items[i];
        switch (cmd->kind) {
            case HUMAN_COMMAND_WALK:
                s->player_dest_x = cmd->world_x;
                s->player_dest_y = cmd->world_y;
                actions[COLO_HEAD_TARGET] = 0;
                break;
            case HUMAN_COMMAND_ATTACK_NPC: {
                int found_slot = col_find_target_obs_slot(s, cmd->npc_slot);
                actions[COLO_HEAD_TARGET] =
                    (found_slot >= 0 && col_obs_slot_is_targetable(s, found_slot))
                        ? found_slot + 1 : 0;
                s->player_dest_x = -1;
                s->player_dest_y = -1;
                break;
            }
            case HUMAN_COMMAND_OVERHEAD_PRAYER:
                actions[COLO_HEAD_PRAYER] = cmd->overhead_prayer;
                break;
            case HUMAN_COMMAND_EAT:
                actions[COLO_HEAD_EAT] = 1;
                break;
            case HUMAN_COMMAND_DRINK:
                actions[COLO_HEAD_POTION] = 1;
                break;
            case HUMAN_COMMAND_SPEC_TOGGLE:
                actions[COLO_HEAD_SPEC] = 1;
                break;
            case HUMAN_COMMAND_OFFENSIVE_PRAYER:
            case HUMAN_COMMAND_SPELL_TARGET:
            case HUMAN_COMMAND_EQUIP_INVENTORY_ITEM:
            case HUMAN_COMMAND_FIGHT_STYLE:
            case HUMAN_COMMAND_SET_AUTOCAST:
            case HUMAN_COMMAND_ITEM_ON_ITEM:
            case HUMAN_COMMAND_ITEM_ON_WIDGET:
            case HUMAN_COMMAND_SPELL_ON_WIDGET:
            case HUMAN_COMMAND_NONE:
                break;
        }
    }

    /* gear/draft/grapple intents are staged on pending_* (not the command queue),
       so fold them in here exactly as the per-tick translate path does. */
    if (hi->pending_gear > 0) actions[COLO_HEAD_GEAR] = hi->pending_gear;
    if (hi->pending_modifier_select > 0 &&
            hi->pending_modifier_select <= COLO_MODIFIER_DRAFT_OPTIONS)
        actions[COLO_HEAD_MODIFIER_SELECT] = hi->pending_modifier_select;
    if (hi->pending_grapple_slot > 0 && hi->pending_grapple_slot <= COLO_NUM_GRAPPLE_SLOTS)
        actions[COLO_HEAD_GRAPPLE_PARRY] = hi->pending_grapple_slot;
}

static void col_step_human_commands_ctx(
    EncounterState* state,
    EncounterContext* context,
    HumanInput* hi
) {
    ColosseumState* s = (ColosseumState*)state;
    ColosseumContext* ctx = context ? (ColosseumContext*)context : col_legacy_context();
    int actions[COLO_NUM_ACTION_HEADS];
    ctx->human_command_mode = 1;
    ctx->human_commands = hi->commands.items;
    ctx->human_command_count = hi->commands.count;
    col_translate_human_commands_ctx(hi, actions, s, ctx);
    col_step_ctx(state, (EncounterContext*)ctx, actions);
    ctx->human_commands = NULL;
    ctx->human_command_count = 0;
    human_input_clear_pending(hi);
}

/* snapshot/restore stores only gameplay state; live process data stays in context. */

#define COLO_SNAPSHOT_MAGIC 0xC010C001u
/* v2: P4 SolHereditState rewrite (A1-A15, B4, B7).
   v3: P5 draft rework (draft_gates_spawn replaces draft_timer), hazard-entity
   totems/bees (ColoTotem/ColoBeeSwarm relaid), molten_kind, venom fields.
   v5: E8 bee-contact poison state fields.
   v6: E13 stat drift and divine potion timers. */
#define COLO_SNAPSHOT_VERSION 6u

typedef struct {
    uint32_t magic;
    uint32_t version;
    uint32_t state_size;
    uint32_t reserved;
    ColosseumState state;
} ColoSnapshot;

static void col_refresh_after_state_load(ColosseumState* s, ColosseumContext* ctx) {
    col_build_npc_stats();
    const uint8_t* const* loadouts = col_loadouts_for_profile(s->active_loadout_profile);
    const uint8_t* spec_weapons = COLO_SPEC_WEAPONS[s->active_loadout_profile];
    encounter_compute_loadout_stats(loadouts[COLO_GEAR_MELEE], ATTACK_STYLE_MELEE,
        s->player.offensive_prayer, s->player.current_strength,
        FIGHT_STYLE_AGGRESSIVE, 0, &s->loadout_stats[COLO_GEAR_MELEE]);
    encounter_compute_loadout_stats(loadouts[COLO_GEAR_RANGED], ATTACK_STYLE_RANGED,
        s->player.offensive_prayer, s->player.current_ranged,
        FIGHT_STYLE_RAPID, 0, &s->loadout_stats[COLO_GEAR_RANGED]);
    encounter_recompute_loadout_max_hits(s->loadout_stats, COLO_NUM_WEAPON_SETS, &s->player);
    for (int k = 0; k < COLO_NUM_SPEC_WEAPONS; k++) {
        uint8_t spec_loadout[NUM_GEAR_SLOTS];
        memcpy(spec_loadout, loadouts[COLO_GEAR_MELEE], NUM_GEAR_SLOTS);
        spec_loadout[GEAR_SLOT_WEAPON] = spec_weapons[k];
        encounter_compute_loadout_stats(spec_loadout, ATTACK_STYLE_MELEE,
            s->player.offensive_prayer, s->player.current_strength,
            FIGHT_STYLE_AGGRESSIVE, 0, &s->spec_stats[k]);
        osrs_derive_equipment_effect_profile(spec_loadout, &s->spec_effects[k]);
    }
    for (int set = 0; set < COLO_NUM_WEAPON_SETS; set++)
        osrs_derive_equipment_effect_profile(loadouts[set], &s->set_effects[set]);
    col_rebuild_player_collision_flags(s);
    col_refresh_current_obs_slots_ctx(s, ctx);
}

static size_t col_snapshot_size_ctx(EncounterState* state, EncounterContext* context) {
    (void)state;
    (void)context;
    return sizeof(ColoSnapshot);
}

static void col_snapshot_ctx(EncounterState* state, EncounterContext* context, void* out) {
    (void)context;
    ColoSnapshot* snap = (ColoSnapshot*)out;
    encounter_snapshot_write_frame(
        snap, sizeof(*snap), COLO_SNAPSHOT_MAGIC, COLO_SNAPSHOT_VERSION,
        sizeof(ColosseumState));
    encounter_snapshot_copy_state_to(
        snap, offsetof(ColoSnapshot, state), state, sizeof(ColosseumState));
}

static void col_restore_ctx(
    EncounterState* state,
    EncounterContext* context,
    const void* data,
    size_t n
) {
    ColosseumContext* ctx = context ? (ColosseumContext*)context : col_legacy_context();
    ColosseumState* dst = (ColosseumState*)state;
    encounter_snapshot_validate_frame(
        "col_restore", data, n, sizeof(ColoSnapshot),
        COLO_SNAPSHOT_MAGIC, COLO_SNAPSHOT_VERSION, sizeof(ColosseumState));
    encounter_snapshot_copy_state_from(
        dst, data, offsetof(ColoSnapshot, state), sizeof(ColosseumState));
    col_refresh_after_state_load(dst, ctx);
}


static int col_lab_apply_script_line_alloc_json_void(
    void* state, const char* line, char** out_json
) {
    return col_lab_apply_script_line_alloc_json((ColosseumState*)state, line, out_json);
}

static int col_apply_lab_command_ctx(
    EncounterState* state,
    EncounterContext* context,
    const char* line
) {
    (void)context;
    return encounter_apply_lab_command_dump_wrapper(
        state, line, COLO_LAB_LINE_DUMP, col_lab_apply_script_line_alloc_json_void);
}


static const EncounterDef ENCOUNTER_COLOSSEUM = {
    .name = "colosseum",
    .obs_size = COLO_NUM_OBS,
    .num_action_heads = COLO_NUM_ACTION_HEADS,
    .action_head_dims = COLO_ACTION_DIMS,
    .mask_size = COLO_ACTION_MASK_SIZE,

    .state_size = sizeof(ColosseumState),
    .context_size = sizeof(ColosseumContext),
    .init_context = col_init_context,
    .destroy_context = col_destroy_context,
    .init_state = col_init_state,

    .create = col_create,
    .destroy = col_destroy,
    .reset = col_reset_ctx,
    .step = col_step_ctx,
    .step_human_commands = col_step_human_commands_ctx,

    .snapshot_size = col_snapshot_size_ctx,
    .snapshot = col_snapshot_ctx,
    .restore = col_restore_ctx,

    .cell_key_size = NULL,
    .write_cell_key = NULL,
    .progress_score = NULL,

    .write_obs = col_write_obs_ctx,
    .write_mask = col_write_mask_ctx,
    .get_reward = col_get_reward_ctx,
    .is_terminal = col_is_terminal_ctx,

    .get_entity_count = col_get_entity_count_ctx,
    .get_entity = col_get_entity_ctx,
    .fill_render_entities = col_fill_render_entities_ctx,

    .put_int = col_put_int_ctx,
    .put_float = col_put_float_ctx,
    .put_ptr = col_put_ptr_ctx,

    .arena_base_x = COLO_ARENA_MIN_X,
    .arena_base_y = COLO_ARENA_MIN_Y,
    .arena_width = COLO_ARENA_WIDTH,
    .arena_height = COLO_ARENA_HEIGHT,

    .translate_human_input = col_translate_human_input_ctx,
    .is_human_targetable_npc_slot = col_is_human_targetable_npc_slot_ctx,
    .apply_lab_command = col_apply_lab_command_ctx,
    .head_move = COLO_HEAD_MOVE,
    .head_prayer = COLO_HEAD_PRAYER,
    .head_target = COLO_HEAD_TARGET,

    .render_post_tick = col_render_post_tick_ctx,
    .get_log = col_get_log_ctx,
    .get_tick = col_get_tick_ctx,
    .get_winner = col_get_winner_ctx,
};

__attribute__((constructor))
static void col_register(void) {
    encounter_register(&ENCOUNTER_COLOSSEUM);
}
