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
