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

/* best-gear oracle, defined later in encounter_colosseum_obs_mask.inc (it lives with
   the obs DPT machinery, included after this file); the gear-quality reward lever
   needs it, so forward-declare here. */
static const ColoBestGear (*col_get_best_gear_table(ColosseumState* s))[COLO_NUM_NPC_TYPES];
static float col_expected_dpt_for_equipment_vs_npc(
    ColosseumState* s, const uint8_t equipment[NUM_GEAR_SLOTS],
    const ColoNPC* npc, int use_style_matched_prayer);

static float col_terminal_death_reward(const ColosseumState* s, const ColosseumContext* ctx) {
    (void)s;
    return -ctx->config.death_penalty_coeff;
}

static float col_terminal_timeout_reward(const ColosseumState* s, const ColosseumContext* ctx) {
    (void)s;
    return -ctx->config.timeout_penalty;
}

static void col_update_min_sol_hp_seen(ColosseumState* s) {
    if (!s->sol.started) return;
    int idx = col_sol_find_idx(s);
    if (idx < 0) return;
    int hp = s->npcs[idx].hp;
    if (hp < 0) hp = 0;
    if (hp < s->min_sol_hp_seen) s->min_sol_hp_seen = hp;
}

static int col_sol_score_phase_for_min_hp(int hp) {
    if (hp > 1350) return 0;
    if (hp > 1125) return 1;
    if (hp > 750) return 2;
    if (hp > 375) return 3;
    if (hp > 150) return 4;
    return 5;
}

static float col_sol_score_progress(const ColosseumState* s) {
    if (!s->sol.started) return 0.0f;
    int hp = s->min_sol_hp_seen;
    if (hp < 0) hp = 0;
    if (hp > COLO_SOL_HP_MAX) hp = COLO_SOL_HP_MAX;

    static const int phase_hi_hp[COLO_SOL_NUM_PHASES] = {
        1500, 1350, 1125, 750, 375, 150,
    };
    static const int phase_lo_hp[COLO_SOL_NUM_PHASES] = {
        1350, 1125, 750, 375, 150, 0,
    };
    int phase = col_sol_score_phase_for_min_hp(hp);
    float denom = (float)(phase_hi_hp[phase] - phase_lo_hp[phase]);
    float within_phase = ((float)phase_hi_hp[phase] - (float)hp) / denom;
    if (within_phase < 0.0f) within_phase = 0.0f;
    if (within_phase > 1.0f) within_phase = 1.0f;
    return ((float)phase + within_phase) / (float)COLO_SOL_NUM_PHASES;
}

static float col_current_wave_score_progress(const ColosseumState* s) {
    if (s->wave == COLO_WAVE_BOSS || s->sol.started) return 0.0f;
    if (s->current_wave_hp_pool <= 0) return 0.0f;
    float progress = s->current_wave_fresh_damage / (float)s->current_wave_hp_pool;
    if (progress < 0.0f) return 0.0f;
    /* a full clear is credited by the waves_cleared term, not here, so cap the
       partial fraction strictly below 1.0 (fresh==pool resolves to a +1 wave). */
    if (progress >= 1.0f) return 0.0f;
    return progress;
}

static float col_episode_outcome_score(const ColosseumState* s) {
    if (s->episode_over && s->winner == COLO_OUTCOME_PLAYER_WON) return 1.0f;
    int waves_cleared = s->log.waves_cleared;
    if (waves_cleared > COLO_WAVE_BOSS) waves_cleared = COLO_WAVE_BOSS;
    float depth = (float)waves_cleared +
        col_current_wave_score_progress(s) +
        col_sol_score_progress(s);
    float ratio = depth / (float)COLO_NUM_WAVES;
    return 0.99f * ratio * ratio;
}

static void col_record_terminal_outcome_score(ColosseumState* s) {
    s->log.outcome_score = col_episode_outcome_score(s);
}

/** Furthest depth reached this episode, reported RAW (not squared like the
    outcome_score): waves_fully_cleared + current-wave fresh-fraction + Sol
    sub-progress, in [0, COLO_NUM_WAVES]. A full win pins to COLO_NUM_WAVES. The
    binding keeps the per-env running max of this across episodes. */
static float col_episode_max_wave_depth(const ColosseumState* s) {
    if (s->episode_over && s->winner == COLO_OUTCOME_PLAYER_WON)
        return (float)COLO_NUM_WAVES;
    int waves_cleared = s->log.waves_cleared;
    if (waves_cleared > COLO_WAVE_BOSS) waves_cleared = COLO_WAVE_BOSS;
    return (float)waves_cleared +
        col_current_wave_score_progress(s) +
        col_sol_score_progress(s);
}

/** Count how many of the three prayer-checkable styles {magic, ranged, melee} have
    at least one live enemy that can hit the player THIS tick: in attack range, and
    with line of sight for the ranged/magic ones. This is the exact gating
    col_write_threat_los_counts uses for the de-confliction obs (manticore counted as
    its next orb's style), reduced to a distinct-style count (0..3). max(0, count-1)
    is the multi-style exposure the agent must position away to clear with one
    overhead prayer. */
static int col_distinct_live_threat_styles(const ColosseumState* s) {
    int magic = 0, ranged = 0, melee = 0;
    for (int n = 0; n < COLO_MAX_NPCS; n++) {
        const ColoNPC* npc = &s->npcs[n];
        if (!col_npc_is_live_enemy(npc)) continue;
        const ColoNpcStats* ns = &COLO_NPC_STATS[npc->type];
        if (ns->attack_range < 1) continue;
        int dist = col_npc_dist_to_player(s, npc);
        if (dist < 1 || dist > ns->attack_range) continue;
        if (ns->attack_range > 1 && !col_npc_has_los_to_player(s, npc)) continue;
        AttackStyle style = ns->default_style;
        if (npc->type == COLO_MANTICORE) {
            const ColoManticoreState* mc = &npc->type_state.manticore;
            int orb = mc->cycle_step >= 0 ? mc->cycle_step : 0;
            if (orb < 3 && mc->orb_style[orb] != ATTACK_STYLE_NONE) style = mc->orb_style[orb];
        }
        if (style == ATTACK_STYLE_MAGIC) magic = 1;
        else if (style == ATTACK_STYLE_RANGED) ranged = 1;
        else if (style == ATTACK_STYLE_MELEE) melee = 1;
    }
    return magic + ranged + melee;
}

static float col_rewardable_avoided_damage(ColosseumState* s) {
    ColoTickScratch* t = &s->tick_scratch;
    if (__builtin_popcount((unsigned)t->styles_faced_mask) != 1) return 0.0f;
    int style = __builtin_ctz((unsigned)t->styles_faced_mask);
    float raw_avoided = t->avoidable_threat_by_style[style] - t->offpray_damage_this_tick;
    if (raw_avoided <= 0.0f) return 0.0f;
    float progress_budget = s->log.total_damage_dealt - s->avoided_damage_already_rewarded;
    if (progress_budget <= 0.0f) return 0.0f;
    return raw_avoided < progress_budget ? raw_avoided : progress_budget;
}

/* A: gear-quality signal for the argmax-gear reward lever. The player's ACTUAL
   equipped DPT vs the attacked target, as a fraction [0,1] of the best-gear DPT for
   the argmax style. 1.0 = wearing the optimal full kit; lower when off-style armour
   is left equipped (the watched policy stripped its own melee gear). Only computed
   when the lever is on. Returns -1 if no live attack target this tick. */
static float col_attacked_gear_quality_ratio(ColosseumState* s) {
    if (!s->tick_scratch.player_attacked) return -1.0f;
    int npc_slot = s->player_attack_npc_idx;
    if (npc_slot < 0 || npc_slot >= COLO_MAX_NPCS) return -1.0f;
    const ColoNPC* npc = &s->npcs[npc_slot];
    if (!col_npc_is_live_target(npc) || col_type_is_hazard_entity(npc->type))
        return -1.0f;
    const ColoBestGear (*best)[COLO_NUM_NPC_TYPES] = col_get_best_gear_table(s);
    float best_dpt = 0.0f;
    for (int set = 0; set < COLO_NUM_WEAPON_SETS; set++) {
        float d = best[set][npc->type].dpt;
        if (d > best_dpt) best_dpt = d;
    }
    if (best_dpt <= 0.0f) return -1.0f;
    /* match the oracle's style-matched-prayer assumption (the best-gear table uses it)
       so the ratio isolates GEAR quality and is not diluted by the live offensive
       prayer. use_style_matched_prayer only selects which prayer the stat math assumes;
       it does not mutate player state. */
    float actual = col_expected_dpt_for_equipment_vs_npc(s, s->player.equipped, npc, 1);
    float ratio = actual / best_dpt;
    if (ratio < 0.0f) ratio = 0.0f;
    if (ratio > 1.0f) ratio = 1.0f;
    return ratio;
}

/* F1: 1 if the style the player just attacked with has its offensive stat boosted
   above base (a combat / ranging / magic potion is active), else 0; -1 if no live
   attack target. For the offensive-boost-usage reward lever. */
static int col_attacked_with_offensive_boost(ColosseumState* s) {
    if (!s->tick_scratch.player_attacked) return -1;
    int npc_slot = s->player_attack_npc_idx;
    if (npc_slot < 0 || npc_slot >= COLO_MAX_NPCS) return -1;
    const ColoNPC* npc = &s->npcs[npc_slot];
    if (!col_npc_is_live_target(npc) || col_type_is_hazard_entity(npc->type))
        return -1;
    AttackStyle style = (AttackStyle)s->player_attack_style_id;
    int cur_att = 0, cur_str = 0;
    col_current_style_levels(&s->player, style, &cur_att, &cur_str);
    int base_att, base_str;
    if (style == ATTACK_STYLE_RANGED) {
        base_att = base_str = s->player.base_ranged;
    } else if (style == ATTACK_STYLE_MAGIC) {
        base_att = base_str = s->player.base_magic;
    } else {
        base_att = s->player.base_attack;
        base_str = s->player.base_strength;
    }
    return (cur_att > base_att || cur_str > base_str) ? 1 : 0;
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

    /* land-time forensics invariant: every unit of received damage is classified
       into exactly one channel. Guarded to scale == 1 because the pending
       pipeline books unscaled damage while the direct sink books scaled. */
    assert(cfg->player_damage_received_scale != 1.0f ||
        fabsf(t->landed_offpray_damage + t->landed_prayed_damage +
              t->landed_unprayable_damage + t->landed_self_damage
              - t->damage_received) < 0.5f);

    s->log.total_damage_dealt += t->damage_dealt;
    s->log.total_boss_damage += t->damage_boss;
    s->log.total_damage_received += t->damage_received;
    s->log.total_prayer_correct += t->prayer_correct;
    s->log.total_npc_attacks += t->attacks_fired;
    col_update_min_sol_hp_seen(s);
    if (s->sol.phase > s->log.boss_phase_reached) s->log.boss_phase_reached = s->sol.phase;

    float reward = 0.0f;
    float rewarded_fresh = t->fresh_damage_dealt;
    if (s->wave < COLO_FARM_CAP_WAVES) {
        s->log.farm_damage += t->fresh_damage_reinforcement;
        if (cfg->farm_safe_damage_cap)
            rewarded_fresh -= t->fresh_damage_reinforcement;
    }
    reward += cfg->damage_reward_coeff * rewarded_fresh;
    reward += cfg->boss_damage_reward_coeff * t->fresh_damage_boss;
    reward += cfg->prayer_correct_reward * (float)t->prayer_correct;
    reward -= cfg->offpray_damage_penalty_coeff * t->offpray_damage_this_tick;
    if (cfg->multistyle_exposure_penalty_coeff != 0.0f) {
        int excess_styles = col_distinct_live_threat_styles(s) - 1;
        if (excess_styles > 0)
            reward -= cfg->multistyle_exposure_penalty_coeff * (float)excess_styles;
    }
    if (cfg->argmax_gear_reward_coeff != 0.0f) {
        float gear_quality = col_attacked_gear_quality_ratio(s);
        if (gear_quality >= 0.0f)
            reward += cfg->argmax_gear_reward_coeff * gear_quality;
    }
    if (cfg->offensive_boost_reward_coeff != 0.0f) {
        if (col_attacked_with_offensive_boost(s) > 0)
            reward += cfg->offensive_boost_reward_coeff;
    }
    float rewardable_avoided = col_rewardable_avoided_damage(s);
    reward += cfg->avoided_damage_coeff * rewardable_avoided;
    s->avoided_damage_already_rewarded += rewardable_avoided;

    if (s->sol.started && s->sol.phase > s->rewarded_boss_phase) {
        reward += cfg->boss_phase_bonus * (float)(s->sol.phase - s->rewarded_boss_phase);
        s->rewarded_boss_phase = s->sol.phase;
    }
    return reward;
}

/** HP-heal capacity remaining in the inventory: per-dose heal x remaining doses,
    summed over every heal-capable cell. Death forensics: high = a burst death with
    unused supplies (a positioning/decision failure), ~0 = attrition (supplies
    exhausted before the fatal tick). */
static float col_inventory_heal_remaining(const ColosseumState* s) {
    float total = 0.0f;
    for (int i = 0; i < OSRS_INVENTORY_SIZE; i++) {
        const OsrsInventoryCell* cell = &s->inventory_cells[i];
        if (cell->raw_osrs_id == 0) continue;
        OsrsInventoryClickResolution r =
            osrs_inventory_cell_click_interpret(cell, OSRS_CLICK_TICK_FIRST);
        int per_dose = osrs_consumable_hp_heal_amount(
            r.consumable_kind, s->player.base_hitpoints);
        if (per_dose <= 0) continue;
        int doses = r.dose_count > 0 ? (int)r.dose_count : 1;
        total += (float)(per_dose * doses);
    }
    return total;
}

/** Diagnostic death attribution: record which NPC type landed the killing blow
    (s->last_hit_by_type), the fatal tick's total damage_received (a single big
    off-prayer hit vs a same-tick multi-style combo), the fatal tick's land-time
    channel split (what KIND of damage actually killed us), and the heal capacity
    left in the inventory (burst vs attrition). Called only on a real HP/Doom
    death, never on the tick-timeout path (where last_hit_by_type would be stale). */
static void col_record_death_attribution(ColosseumState* s) {
    int kt = s->last_hit_by_type;
    if (kt >= 0 && kt < COLO_NUM_NPC_TYPES) s->log.death_by_type[kt] += 1.0f;
    s->log.death_fatal_damage += s->tick_scratch.damage_received;
    if (__builtin_popcount(s->tick_scratch.styles_faced_mask) >= 2)
        s->log.death_on_conflict_tick += 1.0f;
    s->log.death_dmg_unprayable += s->tick_scratch.landed_unprayable_damage;
    s->log.death_dmg_offpray += s->tick_scratch.landed_offpray_damage;
    s->log.death_dmg_prayed += s->tick_scratch.landed_prayed_damage;
    s->log.death_dmg_self += s->tick_scratch.landed_self_damage;
    s->log.death_heal_remaining += col_inventory_heal_remaining(s);
}

/** Diagnostic bleed split: route this tick's off-prayer HP into the conflict
    bucket (>=2 distinct prayer-checkable styles landed, so a single overhead
    could not block all of them -> unavoidable) or the solo bucket (1 style, a
    wrong-overhead mis-flick -> avoidable). Call once per tick after all incoming
    hits have resolved (pending-hit resolve + manticore orbs), before tick reset. */
static void col_classify_tick_offpray(ColosseumState* s) {
    float d = s->tick_scratch.offpray_damage_this_tick;
    if (d <= 0.0f) return;
    if (__builtin_popcount(s->tick_scratch.styles_faced_mask) >= 2)
        s->log.offpray_damage_conflict += d;
    else
        s->log.offpray_damage_solo += d;
}

static void col_step_ctx(EncounterState* state, EncounterContext* context, const int* actions) {
    ColosseumState* s = (ColosseumState*)state;
    ColosseumContext* ctx = context ? (ColosseumContext*)context : col_legacy_context();
    if (s->episode_over) return;

    /* clear per-tick state. one assignment resets every accumulator/flag. */
    s->reward = 0.0f;
    s->tick_scratch = (ColoTickScratch){0};
    /* scaffold: mirror the (possibly binding-annealed) damage scale onto state so
       the state-only HP-apply sinks can read it without threading the context
       through the modifier-tick chain. 1.0 default => bit-identical. */
    s->active_player_damage_received_scale = ctx->config.player_damage_received_scale;
    col_context_clear_render_events(ctx);
    s->player_attack_timing = (EncounterProjectileTiming){0};
    encounter_clear_tick_flags(&s->player);
    for (int i = 0; i < COLO_MAX_NPCS; i++) {
        s->npcs[i].attacked_this_tick = 0;
        s->npcs[i].attack_style_this_tick = ATTACK_STYLE_NONE;
        s->npcs[i].attack_visual_target = -1;
        s->npcs[i].moved_last_tick = s->npcs[i].moved_this_tick;
        s->npcs[i].moved_this_tick = 0;
        s->npcs[i].hit_landed_this_tick = 0;
        s->npcs[i].hit_damage = 0;
        s->npcs[i].hit_was_successful_this_tick = 0;
        s->npcs[i].hit_spell_type = 0;
    }
    s->tick++;
    /* unconditional timeout: checked before the gap/draft gotos so a frozen
       draft can never produce an unterminating episode (training-iter-1 bug). */
    if (s->tick >= s->episode_max_ticks) {
        (void)col_compute_reward_ctx(s, ctx);
        s->episode_over = 1;
        s->time_limit_truncated = 1;
        s->winner = COLO_OUTCOME_PLAYER_DIED;
        s->reward = col_terminal_timeout_reward(s, ctx);
        col_record_terminal_outcome_score(s);
        s->log.max_wave_depth = col_episode_max_wave_depth(s);
        s->episode_return += s->reward;
        return;
    }
    col_player_pretick(s, ctx, actions);
    if (ctx && ctx->config.prayer_oracle_mode) col_apply_prayer_oracle(s);

    /* gate decrements run BEFORE any spawn this tick, so gates armed by a
       fresh spawn survive the spawn tick intact (movement opens spawn+2 = t7,
       attacks spawn+4 = t9). */
    if (s->wave_ready_delay > 0) s->wave_ready_delay--;
    if (s->wave_attack_delay > 0) s->wave_attack_delay--;

    int spawn_wave_now = 0;
    if (s->wave_spawn_delay > 0) {
        s->wave_spawn_delay--;
        if (s->wave_spawn_delay == 0) spawn_wave_now = 1;
    }
    /* Researched t5 spawn resolution: the wave spawns BEFORE this tick's player
       move phase, so anchor exclusion reads the player's PRE-move t5 tile (the
       speedrunner trick: a move queued on t5 lands t6 and costs nothing). NPCs
       created here first appear in the obs written at this tick's end = t6. */
    if (spawn_wave_now) {
        s->wave = s->wave_spawn_target;
        col_spawn_wave(s);
    }
    /* B6+D26: an open draft IS the inter-wave gap — the world pauses and the
       player is frozen until the mandatory pick arms the next spawn. */
    int in_wave_gap = (s->wave_spawn_delay > 0 || spawn_wave_now ||
                       s->modifiers.draft_pending);
    int in_ready_gap = s->wave_ready_delay > 0;

    /* resolve in-flight projectiles (always, even during gaps) */
    col_resolve_player_projectiles_on_npcs_ctx(s, ctx);
    col_resolve_player_pending_hits_ctx(s, ctx);
    col_tick_npc_death_lingers(s);

    if (!in_wave_gap && !in_ready_gap) {
        /* B2(a): anchor the shared warband cycle so the berserker window lands
           on the first attack-legal tick (t9): anchor N = t8, windows N+1/N+2/
           N+3 = berserker t9, seer t10, archer t11. */
        if (s->wave_attack_delay == 0 && s->warband_cycle_anchor < 0)
            s->warband_cycle_anchor = s->tick - 1;
        col_tick_live_stat_drift_and_divines(s);
        col_rebuild_player_collision_flags(s);
        col_tick_npcs_ctx(s, ctx);
        col_modifiers_tick(s);
        /* L13: the surge cooldown pauses between waves (wiki), so it only
           counts down while gameplay is live. */
        if (s->surge_cooldown > 0) s->surge_cooldown--;
    }

    /* classify this tick's off-prayer bleed (all incoming prayer-checkable hits
       have now resolved) before the death check so the fatal tick is bucketed. */
    col_classify_tick_offpray(s);

    /* early death check before player actions: a lethal hit can't be brewed back.
       Doom reaching its tier stack cap is also lethal. */
    if (ctx && ctx->config.invuln_mode) {
        s->player.current_hitpoints = s->player.base_hitpoints;  /* scaffold: unkillable */
    } else if (s->player.current_hitpoints <= 0 || col_mod_doom_lethal(s)) {
        (void)col_compute_reward_ctx(s, ctx);
        col_record_death_attribution(s);
        s->episode_over = 1;
        s->winner = COLO_OUTCOME_PLAYER_DIED;
        s->reward = col_terminal_death_reward(s, ctx);
        col_record_terminal_outcome_score(s);
        s->log.max_wave_depth = col_episode_max_wave_depth(s);
        s->episode_return += s->reward;
        return;
    }

    /* player actions: attacks open the tick NPCs appear (t6); NPCs can neither
       move (until t7) nor hit back (until t9) yet. */
    int can_player_attack = !in_wave_gap;
    int px = s->player.x, py = s->player.y;
    col_tick_player_ctx(s, ctx, actions, can_player_attack);
    s->tick_scratch.player_moved = (s->player.x != px || s->player.y != py) ? 1 : 0;

    /* B2: warband windows resolve after the player's move phase, so a member's
       two-tick stationary state already reflects a player who fled this tick;
       the wave-start attack gate (no NPC attacks before t9) holds them shut. */
    if (!in_wave_gap && !in_ready_gap && s->wave_attack_delay == 0)
        col_warband_attack_phase(s, ctx);

    s->reward = col_compute_reward_ctx(s, ctx);
    /* anti-stall: a per-live-combat-tick cost so a fast wave clear strictly dominates
       letting a wave overrun to farm reinforcement damage (default 0, sweepable). */
    if (can_player_attack && ctx->config.stall_penalty_coeff != 0.0f)
        s->reward -= ctx->config.stall_penalty_coeff;
    int all_dead = 1;
    int wave_cleared = 0;

    /* post-action death check (HP or a lethal Doom stack). */
    if (ctx && ctx->config.invuln_mode) {
        s->player.current_hitpoints = s->player.base_hitpoints;  /* scaffold: unkillable */
    } else if (s->player.current_hitpoints <= 0 || col_mod_doom_lethal(s)) {
        col_record_death_attribution(s);
        s->episode_over = 1;
        s->winner = COLO_OUTCOME_PLAYER_DIED;
        s->reward = col_terminal_death_reward(s, ctx);
        goto finish_step;
    }

    /* a tick that spawned or still awaits a wave never re-evaluates wave-clear */
    if (spawn_wave_now || s->wave_spawn_delay > 0 || s->modifiers.draft_pending)
        goto finish_step;

    for (int i = 0; i < COLO_MAX_NPCS; i++) {
        if (col_npc_is_live_enemy(&s->npcs[i])) {
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
        s->reward += ctx->config.wave_clear_bonus;
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
            if (!s->modifiers.draft_pending)
                s->wave_spawn_delay = col_wave_entry_delay_ticks(s->wave_spawn_target);
            s->log.wave_reached = s->wave + 1;
        }
    }

finish_step:
    if (s->episode_over) {
        col_record_terminal_outcome_score(s);
        s->log.max_wave_depth = col_episode_max_wave_depth(s);
    }
    s->episode_return += s->reward;
}

static void col_step(EncounterState* state, const int* actions) {
    col_step_ctx(state, (EncounterContext*)col_legacy_context(), actions);
}
