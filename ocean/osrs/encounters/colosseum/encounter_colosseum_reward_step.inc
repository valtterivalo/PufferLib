#line 76 "encounter_colosseum.h"
/**
 * @file encounter_colosseum_reward_step.inc
 * @brief Reward shaping + the per-tick step loop, mirroring inferno reward_step.
 *
 * Reward is sparse-leaning (sparse > dense, anti suicide-perversity): event
 * bonuses (wave clear / boss phase / win) dominate, with small dense damage +
 * prayer-correct shaping. All coeffs default to 0 so the lead opts in per term.
 * Death/terminal penalty defaults off. The step loop keeps inferno's exact
 * ordering so gameplay can be filled in without re-plumbing.
 */

static float col_terminal_loss_reward(const ColosseumState* s, const ColosseumContext* ctx) {
    (void)s;
    if (ctx->config.terminal_penalty_enabled) return -1.0f;
    return -ctx->config.death_penalty_coeff;
}

/**
 * Bank this tick's accumulators into episode totals and return the shaped per-tick
 * reward: small dense damage + prayer-correct terms, plus a boss_phase_bonus for
 * every Sol HP-threshold phase crossed this tick. Event bonuses (wave clear, win)
 * are added on top by col_step_ctx so a clear tick still pays its dense terms.
 */
static float col_compute_reward_ctx(ColosseumState* s, const ColosseumContext* ctx) {
    const ColConfig* cfg = &ctx->config;
    ColoTickScratch* t = &s->tick_scratch;

    s->log.total_damage_dealt += t->damage_dealt;
    s->log.total_boss_damage += t->damage_boss;
    s->log.total_damage_received += t->damage_received;
    s->log.total_prayer_correct += t->prayer_correct;
    s->log.total_npc_attacks += t->attacks_fired;
    if (s->sol.phase > s->log.boss_phase_reached) s->log.boss_phase_reached = s->sol.phase;

    float reward = 0.0f;
    reward += cfg->damage_reward_coeff * t->damage_dealt;
    reward += cfg->boss_damage_reward_coeff * t->damage_boss;
    reward += cfg->prayer_correct_reward * (float)t->prayer_correct;

    if (s->sol.started && s->sol.phase > s->rewarded_boss_phase) {
        reward += cfg->boss_phase_bonus * (float)(s->sol.phase - s->rewarded_boss_phase);
        s->rewarded_boss_phase = s->sol.phase;
    }
    return reward;
}

static void col_step_ctx(EncounterState* state, EncounterContext* context, const int* actions) {
    ColosseumState* s = (ColosseumState*)state;
    ColosseumContext* ctx = context ? (ColosseumContext*)context : col_legacy_context();
    if (s->episode_over) return;

    /* clear per-tick state. one assignment resets every accumulator/flag. */
    s->reward = 0.0f;
    s->tick_scratch = (ColoTickScratch){0};
    s->player_attack_timing = (EncounterProjectileTiming){0};
    encounter_clear_tick_flags(&s->player);
    for (int i = 0; i < COLO_MAX_NPCS; i++) {
        s->npcs[i].attacked_this_tick = 0;
        s->npcs[i].attack_style_this_tick = ATTACK_STYLE_NONE;
        s->npcs[i].attack_visual_target = -1;
        s->npcs[i].moved_this_tick = 0;
        s->npcs[i].hit_landed_this_tick = 0;
        s->npcs[i].hit_damage = 0;
        s->npcs[i].hit_was_successful_this_tick = 0;
        s->npcs[i].hit_spell_type = 0;
    }
    s->tick++;
    /* unconditional timeout: checked before the gap/draft gotos so a frozen
       draft can never produce an unterminating episode (training-iter-1 bug). */
    if (s->tick >= COLO_MAX_TICKS) {
        (void)col_compute_reward_ctx(s, ctx);
        s->episode_over = 1;
        s->winner = COLO_OUTCOME_PLAYER_DIED;
        s->reward = col_terminal_loss_reward(s, ctx);
        s->episode_return += s->reward;
        return;
    }
    col_player_pretick(s, ctx, actions);

    int spawn_wave_now = 0;
    if (s->wave_spawn_delay > 0) {
        s->wave_spawn_delay--;
        if (s->wave_spawn_delay == 0) spawn_wave_now = 1;
    }
    /* B6+D26: an open draft IS the inter-wave gap — the world pauses and the
       player is frozen until the mandatory pick arms the next spawn. */
    int in_wave_gap = (s->wave_spawn_delay > 0 || spawn_wave_now ||
                       s->modifiers.draft_pending);
    if (s->wave_ready_delay > 0) s->wave_ready_delay--;
    int in_ready_gap = s->wave_ready_delay > 0;

    /* resolve in-flight projectiles (always, even during gaps) */
    col_resolve_player_projectiles_on_npcs(s);
    col_resolve_player_pending_hits(s);

    if (!in_wave_gap && !in_ready_gap) {
        /* B2(a): anchor the shared warband cycle to the wave's first actionable
           tick N (the ready delay models the researched click+6). */
        if (s->warband_cycle_anchor < 0) s->warband_cycle_anchor = s->tick;
        col_tick_live_stat_drift_and_divines(s);
        col_rebuild_player_collision_flags(s);
        col_tick_npcs_ctx(s, ctx);
        col_modifiers_tick(s);
        /* L13: the surge cooldown pauses between waves (wiki), so it only
           counts down while gameplay is live. */
        if (s->surge_cooldown > 0) s->surge_cooldown--;
    }

    /* early death check before player actions: a lethal hit can't be brewed back.
       Doom reaching its tier stack cap is also lethal. */
    if (s->player.current_hitpoints <= 0 || col_mod_doom_lethal(s)) {
        (void)col_compute_reward_ctx(s, ctx);
        s->episode_over = 1;
        s->winner = COLO_OUTCOME_PLAYER_DIED;
        s->reward = col_terminal_loss_reward(s, ctx);
        s->episode_return += s->reward;
        return;
    }

    /* player actions (gated out during wave/ready gaps) */
    int can_player_attack = !in_wave_gap && !in_ready_gap;
    int px = s->player.x, py = s->player.y;
    col_tick_player_ctx(s, ctx, actions, can_player_attack);
    s->tick_scratch.player_moved = (s->player.x != px || s->player.y != py) ? 1 : 0;

    /* B2(b): warband windows resolve after the player's move phase of the same
       tick, so "the player is moving" is exact on the window tick itself. */
    if (!in_wave_gap && !in_ready_gap) col_warband_attack_phase(s);

    s->reward = col_compute_reward_ctx(s, ctx);
    int all_dead = 1;
    int wave_cleared = 0;

    /* post-action death check (HP or a lethal Doom stack). */
    if (s->player.current_hitpoints <= 0 || col_mod_doom_lethal(s)) {
        s->episode_over = 1;
        s->winner = COLO_OUTCOME_PLAYER_DIED;
        s->reward = col_terminal_loss_reward(s, ctx);
        goto finish_step;
    }

    if (spawn_wave_now) {
        s->wave = s->wave_spawn_target;
        col_spawn_wave(s);
        /* D26: every draft-gated wave start runs the 6-tick ready delay (the
           researched click+6; the pick is the click). */
        s->wave_ready_delay = COLO_START_READY_TICKS;
        goto finish_step;
    }
    if (s->wave_spawn_delay > 0 || s->modifiers.draft_pending) goto finish_step;

    /* wave completion: all non-hazard NPC slots inactive — a live totem or bee
       swarm never blocks the clear (A21+B9). A6: on the boss wave Sol's death
       alone ends the run in victory, even with a Quartet warbander alive. */
    for (int i = 0; i < COLO_MAX_NPCS; i++) {
        if (s->npcs[i].active && !col_type_is_hazard_entity(s->npcs[i].type)) {
            all_dead = 0;
            break;
        }
    }
    wave_cleared = all_dead;
    if (s->wave == COLO_WAVE_BOSS && s->sol.started)
        wave_cleared = col_sol_find_idx(s) < 0;

    /* reinforcement: an uncleared wave that overruns COLO_REINFORCEMENT_TICKS
       streams its reinforce species in once, gate side by the yellow-line rule.
       reinforcement_timer counts down only while NPCs remain and gameplay is
       live; col_spawn_reinforcements sets it to COLO_REINFORCE_FIRED so a wave
       reinforces at most once. */
    if (!all_dead && s->reinforcement_timer > 0) {
        s->reinforcement_timer--;
        if (s->reinforcement_timer == 0) col_spawn_reinforcements(s);
    }

    if (wave_cleared) {
        s->tick_scratch.wave_completed = 1;
        s->log.waves_cleared++;
        s->reward += 1.0f + ctx->config.wave_clear_bonus;
        if (s->wave + 1 >= COLO_NUM_WAVES) {
            s->episode_over = 1;
            s->winner = COLO_OUTCOME_PLAYER_WON;
            s->reward += ctx->config.win_bonus;
        } else {
            /* A16+B6+D26: the mandatory draft gates EVERY next wave including
               wave 12 — the pick (no skip exists) arms the spawn. The empty-
               pool fallback spawns ungated (unreachable in real runs). */
            s->wave_spawn_target = s->wave + 1;
            col_modifier_open_draft(s, s->wave + 1);
            if (!s->modifiers.draft_pending) s->wave_spawn_delay = 1;
            s->log.wave_reached = s->wave + 1;
        }
    }

finish_step:
    s->episode_return += s->reward;
}

static void col_step(EncounterState* state, const int* actions) {
    col_step_ctx(state, (EncounterContext*)col_legacy_context(), actions);
}
