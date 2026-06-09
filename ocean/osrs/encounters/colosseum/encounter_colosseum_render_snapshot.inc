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
    else if (strcmp(key, "death_penalty_coeff") == 0) ctx->config.death_penalty_coeff = value;
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

static void col_write_terminal_status_text(const ColosseumState* s, char* out, size_t cap) {
    if (cap == 0) return;
    out[0] = '\0';
    if (!s->episode_over) return;
    if (s->winner == COLO_OUTCOME_PLAYER_WON) {
        snprintf(out, cap, "Colosseum cleared");
        return;
    }
    snprintf(out, cap, "Killed by %s", colo_npc_type_name(s->last_hit_by_type));
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
    col_write_terminal_status_text(s, ov->status_text, sizeof(ov->status_text));
    ov->status_text_active = ov->status_text[0] != '\0';
}


/* snapshot/restore stores only gameplay state; live process data stays in context. */

#define COLO_SNAPSHOT_MAGIC 0xC010C001u
#define COLO_SNAPSHOT_VERSION 1u

typedef struct {
    uint32_t magic;
    uint32_t version;
    uint32_t state_size;
    uint32_t reserved;
    ColosseumState state;
} ColoSnapshot;

static void col_refresh_after_state_load(ColosseumState* s, ColosseumContext* ctx) {
    col_build_npc_stats();
    encounter_compute_loadout_stats(COLO_LOADOUTS[COLO_GEAR_MELEE], ATTACK_STYLE_MELEE,
        s->player.offensive_prayer, s->player.current_strength,
        FIGHT_STYLE_AGGRESSIVE, 0, &s->loadout_stats[COLO_GEAR_MELEE]);
    encounter_compute_loadout_stats(COLO_LOADOUTS[COLO_GEAR_RANGED], ATTACK_STYLE_RANGED,
        s->player.offensive_prayer, s->player.current_ranged,
        FIGHT_STYLE_RAPID, 0, &s->loadout_stats[COLO_GEAR_RANGED]);
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
    memset(snap, 0, sizeof(*snap));
    snap->magic = COLO_SNAPSHOT_MAGIC;
    snap->version = COLO_SNAPSHOT_VERSION;
    snap->state_size = (uint32_t)sizeof(ColosseumState);
    snap->state = *(ColosseumState*)state;
}

static void col_restore_ctx(
    EncounterState* state,
    EncounterContext* context,
    const void* data,
    size_t n
) {
    ColosseumContext* ctx = context ? (ColosseumContext*)context : col_legacy_context();
    if (n != sizeof(ColoSnapshot)) {
        fprintf(stderr, "col_restore: bad snapshot size %zu (expected %zu)\n",
            n, sizeof(ColoSnapshot));
        abort();
    }
    const ColoSnapshot* snap = (const ColoSnapshot*)data;
    if (snap->magic != COLO_SNAPSHOT_MAGIC || snap->version != COLO_SNAPSHOT_VERSION) {
        fprintf(stderr, "col_restore: bad magic/version (got 0x%08x v%u, want 0x%08x v%u)\n",
            snap->magic, snap->version, COLO_SNAPSHOT_MAGIC, COLO_SNAPSHOT_VERSION);
        abort();
    }
    if (snap->state_size != sizeof(ColosseumState)) {
        fprintf(stderr, "col_restore: state size mismatch (got %u, want %zu)\n",
            snap->state_size, sizeof(ColosseumState));
        abort();
    }
    ColosseumState* dst = (ColosseumState*)state;
    *dst = snap->state;
    col_refresh_after_state_load(dst, ctx);
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
    .step_human_commands = NULL,

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

    .translate_human_input = NULL,
    .is_human_targetable_npc_slot = NULL,
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
