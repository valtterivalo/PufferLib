static void tfa_room_enter(TfaState* s, int room);

static void tfa_boss_tick(TfaState* s, TfaContext* ctx);
static void tfa_boss_npc_died(TfaState* s, TfaNpc* n, int attacker);
static int tfa_boss_npc_damage(TfaState* s, TfaNpc* n, int damage, int attacker, AttackStyle style);

/* Hard cap applied when the hit is rolled: some bosses take no damage in some states. */
static int tfa_npc_damage_cap(const TfaState* s, const TfaNpc* n, int damage) {
    (void)s;
    return n->attackable ? damage : 0;
}

static int tfa_npc_take_damage(TfaState* s, TfaNpc* n, int damage, int attacker, AttackStyle style) {
    int applied = tfa_boss_npc_damage(s, n, damage, attacker, style);
    n->damage_taken_tick += applied;
    if (n->hit_count_tick < OSRS_RENDER_HITS_MAX) {
        n->hits_attacker[n->hit_count_tick] = (int8_t)attacker;
        n->hits_style[n->hit_count_tick] = (int8_t)style;
        n->hits_tick[n->hit_count_tick++] = (int16_t)applied;
    }
    tfa_credit_damage(s, attacker, applied);
    if (n->active && n->hp <= 0 && n->max_hp > 0) tfa_boss_npc_died(s, n, attacker);
    return applied;
}

static void tfa_kill_player(TfaState* s, int p) {
    TfaPlayer* tp = &s->players[p];
    tp->life = TFA_LIFE_DEAD;
    tp->death_tick = (int16_t)s->tick;
    tp->player.prayer = PRAYER_NONE;
    tp->player.offensive_prayer = OFFENSIVE_PRAYER_NONE;
    osrs_interaction_clear(&tp->player.interaction);
    tp->incoming.count = 0;
}

static void tfa_hazards_tick(TfaState* s) {
    for (int i = 0; i < s->hazard_hi; i++) {
        TfaHazard* h = &s->hazards[i];
        if (h->kind != TFA_HAZ_STRIKE && h->kind != TFA_HAZ_FLOOR) continue;
        if (s->tick >= h->start_tick) {
            for (int p = 0; p < s->team_size; p++) {
                if (!tfa_alive(s, p) || (h->owner >= 0 && h->owner != p)) continue;
                if (tfa_hazard_covers(h, s->players[p].player.x, s->players[p].player.y))
                    tfa_hazard_hit_player(s, h, p);
            }
        }
        if (s->tick >= h->end_tick) h->kind = TFA_HAZ_NONE;
    }
}

static float tfa_player_reward(const TfaState* s, const TfaContext* ctx, int p) {
    const TfaRewardConfig* r = &ctx->config.reward;
    const TfaPlayer* tp = &s->players[p];
    float reward = r->damage_dealt * s->team_damage_dealt_tick / 100.0f
        - r->damage_taken * tp->damage_taken_tick / 100.0f;
    if (tp->life == TFA_LIFE_DEAD && tp->death_tick == s->tick) reward -= r->death;
    if (s->room_cleared_this_tick) reward += r->room_clear;
    if (s->outcome == TFA_OUTCOME_CLEARED) reward += r->raid_clear;
    if (s->outcome == TFA_OUTCOME_WIPED) reward -= r->wipe;
    return reward;
}

static void tfa_room_cleared(TfaState* s, TfaContext* ctx) {
    s->rooms_cleared++;
    s->room_cleared_this_tick = 1;
    ctx->log.room_clears[s->room] += 1.0f;
    ctx->log.room_ticks[s->room] += (float)s->room_tick;
    if (s->episode_mode == TFA_EPISODE_ROOM || s->raid_index == TFA_RAID_LENGTH - 1) {
        s->outcome = TFA_OUTCOME_CLEARED;
        s->episode_over = 1;
        return;
    }
    s->raid_index++;
    tfa_room_enter(s, tfa_raid_room(s->raid_index, s->path));
    ctx->log.room_attempts[s->room] += 1.0f;
}

static void tfa_step_team(TfaState* s, TfaContext* ctx, const int* actions) {
    assert(!s->episode_over);
    s->tick++;
    s->room_tick++;
    s->team_damage_dealt_tick = 0.0f;
    s->room_cleared_this_tick = 0;
    s->fx_count = 0;
    for (int p = 0; p < s->team_size; p++) {
        TfaPlayer* tp = &s->players[p];
        tp->reward = 0.0f;
        tp->damage_dealt_tick = 0.0f;
        tp->damage_taken_tick = 0.0f;
        tp->attacked_this_tick = 0;
        tp->player.just_attacked = 0;
        tp->player.prayer_just_activated = 0;
        tp->player.offensive_prayer_just_activated = 0;
        encounter_clear_tick_flags(&tp->player);
    }
    for (int i = 0; i < TFA_MAX_NPCS; i++) {
        s->npcs[i].damage_taken_tick = 0;
        s->npcs[i].hit_count_tick = 0;
        if (s->npcs[i].frozen_ticks > 0) s->npcs[i].frozen_ticks--;
    }

    for (int p = 0; p < s->team_size; p++) {
        if (!tfa_alive(s, p)) continue;
        tfa_player_tick_timers(s, p);
        tfa_resolve_player_hits(s, p);
    }
    for (int i = 0; i < TFA_MAX_NPCS; i++)
        if (s->npcs[i].active) tfa_resolve_npc_hits(s, &s->npcs[i]);

    for (int p = 0; p < s->team_size; p++)
        tfa_player_apply_prayers(s, p, actions + p * OSRS_BASE_NUM_ACTION_HEADS);

    int room_before = s->room;
    if (!s->episode_over) tfa_boss_tick(s, ctx);
    if (!s->episode_over && s->room == room_before) tfa_hazards_tick(s);

    if (!s->episode_over && s->room == room_before) {
        for (int p = 0; p < s->team_size; p++)
            tfa_player_turn(s, ctx, p, actions + p * OSRS_BASE_NUM_ACTION_HEADS);
    }

    for (int p = 0; p < s->team_size; p++) {
        TfaPlayer* tp = &s->players[p];
        if (tp->life != TFA_LIFE_ALIVE) continue;
        OffensivePrayer before = tp->player.offensive_prayer;
        encounter_drain_all_prayers(&tp->player, encounter_player_prayer_bonus(&tp->player));
        if (tp->player.offensive_prayer != before) tfa_mark_stats_dirty(tp);
        if (tp->player.current_hitpoints <= 0) {
            tfa_kill_player(s, p);
            ctx->log.deaths += 1.0f;
        }
    }

    if (!s->episode_over && tfa_alive_count(s) == 0) {
        s->outcome = TFA_OUTCOME_WIPED;
        s->episode_over = 1;
    }
    if (!s->episode_over && s->room_tick >= ctx->config.room_max_ticks) {
        s->outcome = TFA_OUTCOME_TIMEOUT;
        s->episode_over = 1;
    }

    for (int p = 0; p < s->team_size; p++) {
        s->players[p].reward = tfa_player_reward(s, ctx, p);
        s->episode_return += s->players[p].reward;
    }
    tfa_danger_build(s);
}
