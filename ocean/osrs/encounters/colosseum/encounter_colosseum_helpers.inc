#line 76 "encounter_colosseum.h"
/**
 * @file encounter_colosseum_helpers.inc
 * @brief Context lifecycle + shared primitives, mirroring inferno helpers.
 *
 * Every function is static. `_ctx`-suffixed variants take an explicit
 * ColosseumContext*; the bare variant forwards to col_legacy_context(). The
 * binding always calls the `_ctx` forms.
 */

/* forward declarations of cross-inc functions */
static float col_compute_reward_ctx(ColosseumState* s, const ColosseumContext* ctx);
static void col_spawn_wave(ColosseumState* s);
static void col_tick_npcs_ctx(ColosseumState* s, ColosseumContext* ctx);
static void col_tick_player_ctx(
    ColosseumState* s, ColosseumContext* ctx, const int* actions, int can_attack);
static void col_player_pretick(ColosseumState* s, ColosseumContext* ctx, const int* actions);
static void col_resolve_player_pending_hits(ColosseumState* s);
static void col_rebuild_player_collision_flags(ColosseumState* s);
static void col_refresh_current_obs_slots_ctx(ColosseumState* s, const ColosseumContext* ctx);
static float col_terminal_loss_reward(const ColosseumState* s, const ColosseumContext* ctx);

/* Sol Heredit boss machine (encounter_colosseum_boss.inc, included after combat). */
static void col_sol_boss_tick(ColosseumState* s, ColosseumContext* ctx);
static void col_sol_begin_boss_arena(ColosseumState* s);
static int col_sol_clamp_active(const ColosseumState* s);
static int col_in_boss_arena(const ColosseumState* s, int x, int y);

/* modifier hooks (encounter_colosseum_modifiers.inc) referenced by reset_spawn,
   which is included before it. */
static inline int col_mod_active(const ColosseumState* s, ColoModifier mod);
static void col_modifiers_on_wave_spawn(ColosseumState* s);
static void col_modifier_open_draft(ColosseumState* s, int next_wave);

static inline EncounterProjectileDelayKind col_projectile_delay_kind_for_style(
    AttackStyle style
) {
    switch (style) {
        case ATTACK_STYLE_MELEE:
            return ENCOUNTER_PROJECTILE_DELAY_MELEE;
        case ATTACK_STYLE_MAGIC:
            return ENCOUNTER_PROJECTILE_DELAY_MAGIC;
        case ATTACK_STYLE_RANGED:
            return ENCOUNTER_PROJECTILE_DELAY_RANGED;
        case ATTACK_STYLE_NONE:
            return ENCOUNTER_PROJECTILE_DELAY_MELEE;
    }
    abort();
}

static inline EncounterProjectileTiming col_player_projectile_timing(
    AttackStyle style,
    uint8_t weapon,
    int distance
) {
    EncounterProjectileDelayKind kind = col_projectile_delay_kind_for_style(style);
    EncounterProjectileDelayOptions options = {0};
    if (style == ATTACK_STYLE_RANGED && weapon == ITEM_TWISTED_BOW)
        options.visual_delay_ticks = 1;
    return encounter_projectile_timing(distance, 1, kind, options);
}

static inline int col_npc_attack_anim_id(int npc_def_id) {
    const NpcModelMapping* nm = npc_model_lookup((uint16_t)npc_def_id);
    if (!nm)
        return -1;
    if (nm->attack_anim != 65535)
        return (int)nm->attack_anim;
    if (nm->walk_anim != 65535 && nm->walk_anim != nm->idle_anim)
        return (int)nm->walk_anim;
    return -1;
}

typedef struct {
    int anim_id;
    int client_ticks;
    int linger_ticks;
} ColoNpcDeathVisual;

/**
 * Cache-derived Colosseum death sequence metadata.
 *
 * client_ticks is the summed modern sequence frame delay. linger_ticks is the
 * same duration converted to simulator ticks with ceil(client_ticks / 30).
 * Hazard entities keep instant despawn and never consult this table.
 */
static const ColoNpcDeathVisual COLO_NPC_DEATH_VISUALS[COLO_NUM_NPC_TYPES] = {
    [COLO_FREMENNIK_BERSERKER] = { .anim_id = 10857, .client_ticks = 63,  .linger_ticks = 3 },
    [COLO_FREMENNIK_ARCHER]    = { .anim_id = 10851, .client_ticks = 63,  .linger_ticks = 3 },
    [COLO_FREMENNIK_SEER]      = { .anim_id = 10854, .client_ticks = 63,  .linger_ticks = 3 },
    [COLO_SERPENT_SHAMAN]      = { .anim_id = 10859, .client_ticks = 100, .linger_ticks = 4 },
    [COLO_JAGUAR_WARRIOR]      = { .anim_id = 10848, .client_ticks = 63,  .linger_ticks = 3 },
    [COLO_JAVELIN_COLOSSUS]    = { .anim_id = 10897, .client_ticks = 60,  .linger_ticks = 2 },
    [COLO_SHOCKWAVE_COLOSSUS]  = { .anim_id = 10904, .client_ticks = 60,  .linger_ticks = 2 },
    [COLO_MINOTAUR]            = { .anim_id = 10845, .client_ticks = 150, .linger_ticks = 5 },
    [COLO_MANTICORE]           = { .anim_id = 10872, .client_ticks = 80,  .linger_ticks = 3 },
    [COLO_SOL_HEREDIT]         = { .anim_id = 10873, .client_ticks = 230, .linger_ticks = 8 },
    [COLO_HEALING_TOTEM]       = { .anim_id = -1,    .client_ticks = 0,   .linger_ticks = 0 },
    [COLO_BEE_SWARM]           = { .anim_id = -1,    .client_ticks = 0,   .linger_ticks = 0 },
};

static inline const ColoNpcDeathVisual* col_npc_death_visual(ColoNpcType type) {
    assert(type >= 0 && type < COLO_NUM_NPC_TYPES);
    return &COLO_NPC_DEATH_VISUALS[type];
}

static inline int col_npc_death_anim_id(ColoNpcType type) {
    return col_npc_death_visual(type)->anim_id;
}

static inline int col_npc_death_linger_ticks(ColoNpcType type) {
    return col_npc_death_visual(type)->linger_ticks;
}

static inline int col_npc_is_live_target(const ColoNPC* npc) {
    return npc && npc->active && npc->death_ticks == 0 && npc->hp > 0;
}

static inline int col_npc_is_live_enemy(const ColoNPC* npc) {
    return col_npc_is_live_target(npc) && !col_type_is_hazard_entity(npc->type);
}


/** queue a player-attack hit on an NPC's pending-hit queue. */
static void col_queue_npc_pending_hit(
    ColosseumState* s,
    int npc_slot,
    int damage,
    int ticks,
    AttackStyle style,
    int spell_type
) {
    ColoNPC* npc = &s->npcs[npc_slot];
    EncounterPendingHit hit = {
        .active = 1,
        .damage = damage,
        .ticks_remaining = ticks,
        .attack_style = style,
        .spell_type = spell_type,
        .check_prayer = 0,
        .source_npc_slot = -1,
    };
    encounter_pending_hit_queue_push(
        &npc->pending_hits, hit, "colosseum-npc", s->tick, npc_slot, npc->type);
}

/** queue an NPC-attack hit on the player's pending-hit queue.

    check_prayer=1 + prayer_check_delay defers the Protect-from-X test to land time
    (the shared resolver re-checks at T+prayer_check_delay, or at landing when the
    delay is 0), so prayer flicking matters for projectile attacks. Set
    ignore_prayer for hits that bypass Protect (e.g. javelin skyfall) by passing
    check_prayer=0 with a non-prayer style. */
static void col_queue_player_pending_hit(
    ColosseumState* s,
    int source_npc_slot,
    int source_npc_type,
    int damage,
    int ticks,
    AttackStyle style,
    int check_prayer,
    int hit_success
) {
    EncounterPendingHit hit = {
        .active = 1,
        .damage = damage,
        .ticks_remaining = ticks,
        .attack_style = style,
        .check_prayer = check_prayer,
        .prayer_check_delay = 0,
        .spell_type = ENCOUNTER_SPELL_NONE,
        .source_npc_type = source_npc_type,
        .source_npc_slot = source_npc_slot,
        .hit_success = hit_success,
        .elysian_reduced = 0,
    };
    encounter_pending_hit_queue_push(
        &s->player_pending_hits, hit, "colosseum-player", s->tick, source_npc_slot,
        source_npc_type);
}


static ColConfig col_default_config(void) {
    return (ColConfig){
        .start_wave = 0,
        .damage_reward_coeff = 0.0f,
        .boss_damage_reward_coeff = 0.0f,
        .wave_clear_bonus = 0.0f,
        .boss_phase_bonus = 0.0f,
        .win_bonus = 0.0f,
        .prayer_correct_reward = 0.0f,
        .death_penalty_coeff = 0.0f,
        .terminal_penalty_enabled = 0,
        .curriculum_agent = 0,
        .step_out_forecast_obs_enabled = 1,
    };
}

static void col_init_context_typed(ColosseumContext* ctx) {
    memset(ctx, 0, sizeof(*ctx));
    ctx->config = col_default_config();
}

static void col_init_context(EncounterContext* context) {
    col_init_context_typed((ColosseumContext*)context);
}

static void col_destroy_context(EncounterContext* context) {
    (void)context;
}

static ColosseumContext* col_legacy_context(void) {
    static ColosseumContext ctx;
    static Log log;
    static int initialized = 0;
    if (!initialized) {
        col_init_context_typed(&ctx);
        ctx.log = &log;
        initialized = 1;
    }
    return &ctx;
}

static void col_init_state_typed(ColosseumState* s, ColosseumContext* ctx) {
    col_build_npc_stats();
    memset(s, 0, sizeof(*s));
    s->rng_state = 12345;
    s->start_wave = ctx ? ctx->config.start_wave : 0;
}

static void col_init_state(EncounterState* state, EncounterContext* context) {
    col_init_state_typed((ColosseumState*)state, (ColosseumContext*)context);
}

static EncounterState* col_create(void) {
    ColosseumState* s = (ColosseumState*)calloc(1, sizeof(ColosseumState));
    col_init_state_typed(s, col_legacy_context());
    return (EncounterState*)s;
}

static void col_destroy(EncounterState* state) {
    free(state);
}
