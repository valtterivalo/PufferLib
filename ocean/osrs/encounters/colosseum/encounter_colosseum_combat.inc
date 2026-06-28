#line 76 "encounter_colosseum.h"
/**
 * @file encounter_colosseum_combat.inc
 * @brief Combat resolution + per-NPC AI, mirroring inferno combat.
 *
 * Accuracy comes from levels + bonuses via the shared NPC roll; damage from the
 * handset max_hit. Ranged/magic attacks queue projectile-delayed pending hits with
 * deferred Protect-prayer checks so flicking matters. The Sol Heredit boss tick is
 * a separate phase (col_sol_tick) and stays a no-op here.
 */

static void col_apply_npc_death(ColosseumState* s, int idx) {
    ColoNPC* npc = &s->npcs[idx];
    /* hazard entities (totems/bees) are not enemies: no kill telemetry, no
       Volatility explosion — just their respawn bookkeeping. */
    if (col_type_is_hazard_entity(npc->type)) {
        col_hazard_on_npc_death(s, idx);
        col_deactivate_npc(s, idx);
        return;
    }
    if (npc->death_ticks > 0) return;
    assert(npc->active && npc->hp <= 0);
    s->log.total_npc_kills++;
    /* Death Charge: the first player-credited killing blow inside the armed
       window grants +15% spec energy and starts the 100-tick cooldown. All kills
       reaching this path are player-credited (NPC->NPC damage never routes here),
       and thrall kills proc it too (the thrall queues through the same path).
       Multiple kills in one tick consume the charge once (the window is zeroed). */
    if (s->death_charge_window_left > 0) {
        s->player.special_energy += COLO_DEATH_CHARGE_SPEC_GAIN;
        if (s->player.special_energy > 100) s->player.special_energy = 100;
        s->death_charge_window_left = 0;
        s->death_charge_cd = COLO_DEATH_CHARGE_CD;
    }
    /* Volatility: an explosion beyond the dying NPC's footprint (and a molten pool
       at tier III). Read its position/size before the slot is recycled. */
    col_mod_volatility_on_death(s, npc->x, npc->y, col_npc_effective_size(npc));
    /* A21: the owner's death despawns its totem and cancels a pending respawn. */
    col_mod_on_owner_death(s, idx);
    if (!osrs_npc_death_linger_start(
            npc->hp, npc->active, &npc->death_ticks,
            col_npc_death_linger_ticks(npc->type)))
        return;
    encounter_pending_hit_queue_clear(&npc->pending_hits);
}

static void col_render_hit_events_add(
    int* count,
    int damage[ENCOUNTER_RENDER_HITS_MAX],
    int hit_damage
) {
    if (!count || !damage || *count < 0 || *count >= ENCOUNTER_RENDER_HITS_MAX) {
        fprintf(stderr, "invalid colosseum render hit event\n");
        abort();
    }
    damage[*count] = hit_damage > 0 ? hit_damage : 0;
    *count += 1;
}

/** Q10: reflect echo-boots recoil onto the NPC that just damaged the player.
    Mirrors inferno's inf_apply_echo_boots_recoil: the shared
    osrs_echo_boots_recoil_damage gates on the charge + effect, the reflected hit
    targets the single attacker (any range), consumes one charge, and routes the
    damage through the same kill bookkeeping a player projectile would (damage_dealt,
    render splat, totem trigger, player-credited death). Sol Heredit and the
    hazard entities (totem/bee) are never recoil targets. */
static void col_apply_echo_boots_recoil(
    ColosseumState* s, ColosseumContext* ctx, int attacker_slot, int final_damage
) {
    int recoil_damage = osrs_echo_boots_recoil_damage(
        col_live_effects(s), &s->player.item_effect_state, final_damage);
    if (recoil_damage <= 0) return;
    if (attacker_slot < 0 || attacker_slot >= COLO_MAX_NPCS) return;

    ColoNPC* npc = &s->npcs[attacker_slot];
    if (!col_npc_is_live_enemy(npc)) return;
    if (npc->type == COLO_SOL_HEREDIT) return;

    osrs_consume_echo_boots_charge(&s->player);
    int landed = 0, hit_damage = 0;
    int prev_min_hp = npc->min_hp_seen;
    int applied = encounter_damage_npc(
        &npc->hp, &landed, &hit_damage, recoil_damage);
    if (applied <= 0) return;

    npc->hit_landed_this_tick = landed;
    npc->hit_damage += hit_damage;
    npc->hit_was_successful_this_tick = 1;
    col_render_hit_events_add(
        &ctx->npc_render_hit_count[attacker_slot],
        ctx->npc_render_hit_damage[attacker_slot],
        hit_damage);
    s->tick_scratch.damage_dealt += (float)applied;
    float fresh = (float)(prev_min_hp - npc->hp);
    if (fresh > 0.0f) {
        npc->min_hp_seen = npc->hp;
        s->tick_scratch.fresh_damage_dealt += fresh;
        s->current_wave_fresh_damage += fresh;  /* echo recoil never hits Sol (excluded above) */
    }
    if (npc->hp <= 0) {
        col_apply_npc_death(s, attacker_slot);
    } else {
        col_mod_on_npc_hp_changed(s, attacker_slot);
    }
}

/** resolve player attack projectiles landing on NPCs. A hit that lands this tick
    is removed from the queue (the shared resolver only flags it inactive), so a
    long fight like Sol does not pile up resolved hits until the queue overflows. */
static void col_resolve_player_projectiles_on_npcs_ctx(
    ColosseumState* s,
    ColosseumContext* ctx
) {
    for (int i = 0; i < COLO_MAX_NPCS; i++) {
        ColoNPC* npc = &s->npcs[i];
        if (!col_npc_is_live_target(npc)) continue;
        for (int h = 0; h < npc->pending_hits.count; h++) {
            EncounterPendingHit* ph = &npc->pending_hits.hits[h];
            int landed = 0, hit_damage = 0;
            float dmg_acc = 0.0f;
            int frozen = npc->frozen_ticks;
            int prev_min_hp = npc->min_hp_seen;
            if (encounter_resolve_npc_pending_hit(
                    ph, &npc->hp, &landed, &hit_damage, &frozen, NULL, &dmg_acc)) {
                npc->hit_landed_this_tick = landed;
                npc->hit_damage = hit_damage;
                npc->hit_was_successful_this_tick = (hit_damage > 0);
                col_render_hit_events_add(
                    &ctx->npc_render_hit_count[i],
                    ctx->npc_render_hit_damage[i],
                    hit_damage);
                s->tick_scratch.damage_dealt += dmg_acc;
                if (npc->type == COLO_SOL_HEREDIT)
                    s->tick_scratch.damage_boss += dmg_acc;
                float fresh = (float)(prev_min_hp - npc->hp);
                if (fresh > 0.0f) {
                    npc->min_hp_seen = npc->hp;
                    s->tick_scratch.fresh_damage_dealt += fresh;
                    if (npc->type == COLO_SOL_HEREDIT)
                        s->tick_scratch.fresh_damage_boss += fresh;
                    else
                        s->current_wave_fresh_damage += fresh;
                }
                if (npc->hp <= 0) {
                    col_apply_npc_death(s, i);
                    break;
                }
                /* Totemic: a healing totem appears once this NPC drops to <=50% HP. */
                col_mod_on_npc_hp_changed(s, i);
                encounter_pending_hit_queue_remove(&npc->pending_hits, h, "colosseum-npc");
                h--;
            }
        }
    }
}

static void col_resolve_player_projectiles_on_npcs(ColosseumState* s) {
    col_resolve_player_projectiles_on_npcs_ctx(s, col_legacy_context());
}

/** advance dying NPC lifetimes even while wave-gap logic pauses active AI. */
static void col_tick_npc_death_lingers(ColosseumState* s) {
    for (int i = 0; i < COLO_MAX_NPCS; i++) {
        ColoNPC* npc = &s->npcs[i];
        if (!npc->active || npc->death_ticks <= 0) continue;
        if (osrs_npc_death_linger_tick(&npc->death_ticks))
            col_deactivate_npc(s, i);
    }
}

/** per-NPC-type prayer outcome tally (the "who is hitting us off-prayer" log).
    prayed hits count toward correct; unprayed hits attribute their landed damage. */
static void col_log_prayer_event(ColosseumState* s, int npc_type, int prayed, int dmg) {
    assert(npc_type >= 0 && npc_type < COLO_NUM_NPC_TYPES);
    s->log.pray_faced_by_type[npc_type] += 1.0f;
    if (prayed) s->log.pray_correct_by_type[npc_type] += 1.0f;
    else s->log.offpray_damage_by_type[npc_type] += (float)dmg;
}

/** Damage the player and attribute positive damage to one NPC type for episode totals. */
static inline void col_damage_player_from(ColosseumState* s, int dmg, int npc_type) {
    dmg = col_scale_incoming_damage(s, dmg);
    encounter_damage_player(&s->player, dmg, &s->tick_scratch.damage_received);
    if (dmg > 0 && npc_type >= 0 && npc_type < COLO_NUM_NPC_TYPES)
        s->log.total_damage_by_type[npc_type] += (float)dmg;
}

typedef struct {
    ColosseumState* state;
    ColosseumContext* context;
} ColPendingHitObserverContext;

static void col_pending_hit_prayer_observer(
    void* user, const EncounterPendingHit* hit, int dmg, int prayed, int checked) {
    ColPendingHitObserverContext* observer_ctx = (ColPendingHitObserverContext*)user;
    ColosseumState* s = observer_ctx->state;
    ColosseumContext* ctx = observer_ctx->context;
    col_render_hit_events_add(
        &ctx->player_render_hit_count, ctx->player_render_hit_damage, dmg);
    if (dmg > 0 && hit->source_npc_type >= 0 && hit->source_npc_type < COLO_NUM_NPC_TYPES) {
        s->log.total_damage_by_type[hit->source_npc_type] += (float)dmg;
        /* death attribution credits the actual killing-blow source. write this for
           EVERY damaging landing (mirrors inferno): the manticore used to be the only
           writer, so serpent/shockwave/javelin-normal kills inherited a stale manticore
           credit and inflated death_by_manticore. */
        s->last_hit_by_type = hit->source_npc_type;
    }
    /* Q10: echo-boots recoil on a connecting ranged/magic projectile reflects to
       its source NPC at land time (post-prayer dmg), mirroring inferno's
       projectile-land recoil. */
    col_apply_echo_boots_recoil(s, ctx, hit->source_npc_slot, dmg);
    if (!checked && hit->source_npc_type == COLO_MANTICORE) {
        if (hit->hit_success) col_mod_manticore_apply_venom(s, hit->hit_success);
    }
    if (!checked) return;   /* ignore-prayer hits are mechanics, not prayer mistakes */
    s->tick_scratch.styles_faced_mask |= (1 << hit->attack_style);
    s->tick_scratch.avoidable_threat_by_style[hit->attack_style] += (float)hit->damage;
    if (!prayed) s->tick_scratch.offpray_damage_this_tick += (float)dmg;
    col_log_prayer_event(s, hit->source_npc_type, prayed, dmg);
}

/** resolve NPC attack projectiles landing on the player. Blasphemy drains prayer
    by a fraction of the damage taken this tick; Doom adds one stack per damaging
    instance (off-prayer hits that dealt damage). */
static void col_resolve_player_pending_hits_ctx(ColosseumState* s, ColosseumContext* ctx) {
    int prayer_correct = 0;
    int off_prayer = 0;
    float dmg_received = 0.0f;
    ColPendingHitObserverContext observer_ctx = {
        .state = s,
        .context = ctx,
    };
    encounter_resolve_player_pending_hits_observed(
        &s->player_pending_hits, &s->player, s->player.prayer,
        &dmg_received, &prayer_correct, &off_prayer,
        col_pending_hit_prayer_observer, &observer_ctx);
    s->tick_scratch.damage_received += dmg_received;
    s->tick_scratch.prayer_correct += prayer_correct;

    int blas = col_mod_tier(s, COLO_MOD_BLASPHEMY);
    if (blas > 0 && dmg_received > 0.0f && s->player.current_prayer > 0) {
        int drain = ((int)dmg_received * COLO_BLASPHEMY_DRAIN_PCT[blas]) / 100;
        s->player.current_prayer -= drain;
        if (s->player.current_prayer < 0) s->player.current_prayer = 0;
    }
    if (col_mod_active(s, COLO_MOD_DOOM)) s->doom_stacks += off_prayer;
}

static void col_resolve_player_pending_hits(ColosseumState* s) {
    col_resolve_player_pending_hits_ctx(s, col_legacy_context());
}

/** rect distance from the player to an NPC footprint (closest tile). */
static int col_npc_dist_to_player(const ColosseumState* s, const ColoNPC* npc) {
    return encounter_dist_to_npc(
        s->player.x, s->player.y, npc->x, npc->y, col_npc_effective_size(npc));
}

/** OSRS footprint-edge cardinal melee adjacency (WorldArea.isInMeleeDistance:
    axisX+axisY==1, no diagonal), size-aware. Reuses the range==1 branch of the
    shared collision primitive so the diagonal corner is NOT melee-reachable —
    the diagonal safespot the player relies on actually exists. */
static int col_npc_melee_adjacent_to_player(const ColosseumState* s, const ColoNPC* npc) {
    return entity_has_line_of_sight(
        NULL, 0, npc->x, npc->y, col_npc_effective_size(npc),
        s->player.x, s->player.y, 1, 1);
}

/** player defence roll against an incoming NPC attack of the given style.
    A23: Relentless bypasses 33/66/100% of the Defence LEVEL before the roll —
    the gear defence bonus enters the formula fully intact. */
static int col_player_def_roll(ColosseumState* s, AttackStyle style, MeleeStyle melee_style) {
    const EncounterLoadoutStats* ls = col_live_loadout_stats(s);
    int def_level = col_mod_relentless_def_level(s, s->player.current_defence);
    return encounter_player_def_roll_from_loadout(
        def_level, s->player.current_magic,
        ls->def_stab, ls->def_slash, ls->def_crush, ls->def_magic, ls->def_ranged,
        style, melee_style);
}

/** roll an NPC attack against the player: accuracy from levels + bonuses, damage
    from the handset max_hit. Returns damage (0 on miss); sets *hit_success to the
    accuracy result so a 0-damage splash still renders. */
static int col_npc_roll_vs_player(
    ColosseumState* s, const ColoNpcStats* stats, AttackStyle style, int max_hit,
    int* hit_success
) {
    int att_level, att_bonus;
    if (style == ATTACK_STYLE_MELEE) {
        att_level = stats->att_level; att_bonus = stats->melee_att_bonus;
    } else if (style == ATTACK_STYLE_RANGED) {
        att_level = stats->range_level; att_bonus = stats->range_att_bonus;
    } else {
        att_level = stats->magic_level; att_bonus = stats->magic_att_bonus;
    }
    int att_roll = osrs_npc_attack_roll(att_level, att_bonus);
    int def_roll = col_player_def_roll(s, style, stats->melee_style);

    /* Relentless: +1/+3/+6 max hit, and (T3) ignore accuracy entirely; the
       defence-level bypass is already inside col_player_def_roll (A23). */
    int force_hit = 0;
    int max_hit_bonus = col_mod_relentless_max_hit_bonus(s, &force_hit);
    int effective_max = max_hit + max_hit_bonus;

    return encounter_npc_roll_attack_ex(
        att_roll, def_roll, effective_max, force_hit, &s->rng_state, hit_success);
}

/** projectile damage delay (ticks) for an NPC ranged/magic attack at a distance.
    Q5: routes through the shared distance-based timing with is_player=0, the same
    inferno onTicked convention (an NPC projectile lands one tick earlier than a
    player's at the same range — the +1 is_player term is omitted). Colosseum NPCs
    carry no per-NPC timing tweaks, so the default options mirror inferno's standard
    non-mager/ranger/jad path exactly. */
static int col_npc_projectile_delay(AttackStyle style, int distance) {
    EncounterProjectileDelayOptions opts = {0};
    EncounterProjectileTiming timing = encounter_projectile_timing(
        distance, 0, col_projectile_delay_kind_for_style(style), opts);
    return timing.damage_delay_ticks;
}

/** queue a normal NPC ranged/magic attack on the player. OSRS locks the protect-
    prayer outcome AND the damage on the THROW tick (the overhead up now), then the
    frozen damage flies and lands `delay` ticks later: you cannot react-flick once
    the projectile is in the air. Prayer + attribution therefore happen here at
    throw, mirroring the manticore-orb and instant-melee paths; the queued hit
    carries the already-resolved (frozen) damage with check_prayer=0 so the shared
    resolver just lands it. */
static void col_npc_queue_projectile(
    ColosseumState* s, int idx, const ColoNpcStats* stats, AttackStyle style, int max_hit
) {
    ColoNPC* npc = &s->npcs[idx];
    int dist = col_npc_dist_to_player(s, npc);
    int hit_success = 0;
    int dmg = col_npc_roll_vs_player(s, stats, style, max_hit, &hit_success);
    int rolled_dmg = dmg;
    int delay = col_npc_projectile_delay(style, dist);
    int prayed = 0;
    EncounterPendingHit hit = encounter_pending_hit_resolved_at_throw(
        dmg, delay, style, s->player.prayer, npc->type, idx, hit_success, &prayed);
    dmg = hit.damage;
    if (prayed) s->tick_scratch.prayer_correct++;
    col_log_prayer_event(s, npc->type, prayed, dmg);
    s->tick_scratch.styles_faced_mask |= (1 << style);
    s->tick_scratch.avoidable_threat_by_style[style] += (float)rolled_dmg;
    if (!prayed) s->tick_scratch.offpray_damage_this_tick += (float)dmg;
    col_push_player_pending_hit(s, hit);
    npc->attacked_this_tick = 1;
    npc->attack_style_this_tick = style;
    s->tick_scratch.attacks_fired++;
}

/** Apply one NPC melee hit on the player INSTANTLY on the throw tick. OSRS melee
    rolls AND lands on the same tick the animation plays, prayer-checked against the
    overhead already up that tick — it is NOT reactable (you cannot flick Protect-
    from-Melee after seeing the animation). The old path queued melee with a 1-tick
    deferral + deferred prayer check, giving the agent a free reaction tick. This
    mirrors inferno's hit_delay==0 path and the manticore instant orb, replicating
    the FULL bookkeeping the resolver+observer did for melee (render hitsplat, per-
    NPC off-prayer attribution, conflict-bleed classifier inputs, prayer-correct,
    damage, Blasphemy drain, Doom stacks) plus correct melee kill attribution. */
static void col_apply_instant_melee_hit(
    ColosseumState* s, ColosseumContext* ctx, int attacker_slot, int npc_type,
    int dmg, int hit_success
) {
    (void)hit_success;
    int rolled_dmg = dmg;
    EncounterProtectResolve pr = encounter_resolve_protect_at_throw(dmg, s->player.prayer, ATTACK_STYLE_MELEE);
    int prayed = pr.prayed;
    dmg = pr.frozen_damage;
    if (prayed) s->tick_scratch.prayer_correct++;
    col_render_hit_events_add(
        &ctx->player_render_hit_count, ctx->player_render_hit_damage, dmg);
    s->tick_scratch.styles_faced_mask |= (1 << ATTACK_STYLE_MELEE);
    s->tick_scratch.avoidable_threat_by_style[ATTACK_STYLE_MELEE] += (float)rolled_dmg;
    if (!prayed) s->tick_scratch.offpray_damage_this_tick += (float)dmg;
    col_log_prayer_event(s, npc_type, prayed, dmg);
    /* Q10: echo-boots recoil fires on the damage actually dealt (post-prayer),
       reflected to the melee attacker, before the player damage applies (mirrors
       inferno's instant-melee recoil ordering). */
    col_apply_echo_boots_recoil(s, ctx, attacker_slot, dmg);
    col_damage_player_from(s, dmg, npc_type);
    /* Doom + Blasphemy follow the ACTUALLY-APPLIED (scaled) damage, matching the
       queued + volatility/solarflare paths, so player_damage_received_scale=0 is
       truly Doom-immune like invuln_mode (off-prayer 0-damage hits add no Doom
       stack). At scale 1.0 col_scale_incoming_damage returns dmg unchanged so this
       is bit-identical to gating on dmg. */
    int applied = col_scale_incoming_damage(s, dmg);
    if (applied > 0) {  /* an off-prayer hit that dealt damage (prayed -> dmg 0) */
        int blas = col_mod_tier(s, COLO_MOD_BLASPHEMY);
        if (blas > 0 && s->player.current_prayer > 0) {
            int drain = (applied * COLO_BLASPHEMY_DRAIN_PCT[blas]) / 100;
            s->player.current_prayer -= drain;
            if (s->player.current_prayer < 0) s->player.current_prayer = 0;
        }
        if (col_mod_active(s, COLO_MOD_DOOM)) s->doom_stacks += 1;
        s->last_hit_by_type = npc_type;
    }
}

/** an NPC melee attack on the player: rolls + resolves INSTANTLY on the throw tick
    (not reactable). Replaces the old deferred-queue col_npc_queue_melee. */
static void col_npc_melee_attack(
    ColosseumState* s, ColosseumContext* ctx, int idx, const ColoNpcStats* stats, int max_hit
) {
    ColoNPC* npc = &s->npcs[idx];
    int hit_success = 0;
    int dmg = col_npc_roll_vs_player(s, stats, ATTACK_STYLE_MELEE, max_hit, &hit_success);
    col_apply_instant_melee_hit(s, ctx, idx, npc->type, dmg, hit_success);
    npc->attacked_this_tick = 1;
    npc->attack_style_this_tick = ATTACK_STYLE_MELEE;
    s->tick_scratch.attacks_fired++;
}


/** Jaguar warrior: 3 independent melee hits per attack, each up to max_hit (47),
    each its own accuracy + damage roll, each checked against Protect-from-Melee. */
static void col_npc_attack_jaguar(
    ColosseumState* s, ColosseumContext* ctx, int idx, const ColoNpcStats* stats
) {
    for (int h = 0; h < 3; h++)
        col_npc_melee_attack(s, ctx, idx, stats, stats->max_hit);
}

/** A13: minotaur heal — pick ONE eligible ally per heal action and restore it
    TO FULL. Eligibility: not a minotaur, below 75% of max HP, centre within
    COLO_MINOTAUR_HEAL_RANGE tiles of the minotaur's centre (Chebyshev), and
    centre-to-centre line of sight (pillars block heals). Modeled tiebreak: the
    lowest-HP-fraction candidate (no source ranks multiple eligibles).
    D10: NO passive self-regen — the ~5hp/20t talk-page report is a single
    unresolved anonymous claim and is deliberately not modeled. */
static void col_npc_minotaur_heal_ally(ColosseumState* s, int idx) {
    ColoNPC* mino = &s->npcs[idx];
    int mcx = mino->x + 1, mcy = mino->y + 1;   /* centre of the 3x3 */
    int best = -1;
    for (int i = 0; i < COLO_MAX_NPCS; i++) {
        if (i == idx) continue;
        ColoNPC* other = &s->npcs[i];
        if (!col_npc_is_live_target(other)) continue;
        if (other->type == COLO_MINOTAUR) continue;
        if (other->hp * 4 >= other->max_hp * 3) continue;   /* at/above 75% */
        int osz = col_npc_effective_size(other);
        int ocx = other->x + osz / 2, ocy = other->y + osz / 2;
        int dx = ocx > mcx ? ocx - mcx : mcx - ocx;
        int dy = ocy > mcy ? ocy - mcy : mcy - ocy;
        if ((dx > dy ? dx : dy) > COLO_MINOTAUR_HEAL_RANGE) continue;
        if (!col_tiles_have_los(s, mcx, mcy, ocx, ocy)) continue;
        if (best < 0 ||
            other->hp * s->npcs[best].max_hp < s->npcs[best].hp * other->max_hp)
            best = i;
    }
    if (best >= 0) s->npcs[best].hp = s->npcs[best].max_hp;
}

/** Javelin Colossus normal throw is Protect-from-Missiles blockable. Every 5th
    attack is a skyfall that replaces the normal throw, marks the player's
    current tile, and lands COLO_JAVELIN_SKYFALL_DELAY ticks later. Skyfall has
    no accuracy, defence, prayer, or Relentless modifier gate. */
static void col_npc_attack_javelin(
    ColosseumState* s,
    ColosseumContext* ctx,
    int idx,
    const ColoNpcStats* stats
) {
    assert(ctx);
    ctx->npc_render_attack_anim[idx] = COLO_NPC_RENDER_ATTACK_NORMAL;
    ColoNPC* npc = &s->npcs[idx];
    ColoJavelinState* jv = colo_npc_javelin(npc);
    jv->attack_count++;
    if (jv->attack_count % 5 == 0) {
        jv->skyfall_pending = 1;
        jv->skyfall_tile_x = s->player.x;
        jv->skyfall_tile_y = s->player.y;
        jv->skyfall_timer = COLO_JAVELIN_SKYFALL_DELAY;
        jv->skyfall_damage = encounter_rand_int(
            &s->rng_state, COLO_JAVELIN_SKYFALL_MAX_HIT + 1);
        npc->attacked_this_tick = 1;
        npc->attack_style_this_tick = ATTACK_STYLE_RANGED;
        ctx->npc_render_attack_anim[idx] = COLO_NPC_RENDER_ATTACK_JAVELIN_SKYFALL;
        s->tick_scratch.attacks_fired++;
        return;
    }
    col_npc_queue_projectile(s, idx, stats, ATTACK_STYLE_RANGED, stats->max_hit);
}

/** resolve a javelin's in-flight skyfall: land on the marked tile, ignoring
    Protect-from-Missiles (D7: no defence gate either — on-tile means hit). The
    player dodges by standing off the tile at land time; resolution runs before
    NPC and player movement of the land tick (D8). */
static void col_npc_resolve_javelin_skyfall(
    ColosseumState* s,
    ColosseumContext* ctx,
    int idx
) {
    ColoNPC* npc = &s->npcs[idx];
    ColoJavelinState* jv = colo_npc_javelin(npc);
    if (!jv->skyfall_pending) return;
    jv->skyfall_timer--;
    if (jv->skyfall_timer > 0) return;
    jv->skyfall_pending = 0;
    int on_tile = (s->player.x == jv->skyfall_tile_x && s->player.y == jv->skyfall_tile_y);
    int dmg = on_tile ? jv->skyfall_damage : 0;
    col_damage_player_from(s, dmg, npc->type);
    if (dmg > 0) {
        s->last_hit_by_type = npc->type;
        col_mod_on_player_damaged(s, dmg);
    }
    /* Reentry: the skyfall leaves molten sand on the marked tile. */
    col_mod_reentry_on_skyfall(s, jv->skyfall_tile_x, jv->skyfall_tile_y);
}

/** Manticore uses a 10-tick barrage-to-barrage cycle: 3 orbs on consecutive
    ticks, then a 7-tick recharge. Orbs 0 and 1 are ranged and magic in random
    order. Orb 2 is always melee. Each orb checks prayer on its launch tick and
    lands one tick later in fire order, independent of distance. A ready
    manticore staggers 5 ticks if another is mid-barrage. Spawns uncharged, so
    the first barrage waits a full cycle. D11: movement stays unrestricted while
    charging. */
static int col_any_other_manticore_mid_barrage(const ColosseumState* s, int idx) {
    for (int i = 0; i < COLO_MAX_NPCS; i++) {
        if (i == idx) continue;
        const ColoNPC* other = &s->npcs[i];
        if (!col_npc_is_live_target(other) || other->type != COLO_MANTICORE) continue;
        if (other->type_state.manticore.cycle_step > 0) return 1;
    }
    return 0;
}

static int col_manticore_orb_max_hit(AttackStyle style) {
    if (style == ATTACK_STYLE_RANGED) return COLO_MANTICORE_MAX_HIT_RANGED;
    if (style == ATTACK_STYLE_MAGIC) return COLO_MANTICORE_MAX_HIT_MAGIC;
    return COLO_MANTICORE_MAX_HIT_MELEE;
}

/** launch one manticore orb. Prayer is checked immediately and the resulting
    damage is queued to land one tick later. Mantimayhem T1 fires a second
    projectile of the same orb; T2 venom applies when that unprotected successful
    projectile lands. */
static void col_npc_manticore_fire_orb(ColosseumState* s, int idx, const ColoNpcStats* stats) {
#ifdef COLO_PROFILE_ENABLED
    int col_prof_enabled = COLO_PROFILE_ENABLED();
    double col_prof_t0 = col_prof_enabled ? COLO_PROFILE_NOW_MS() : 0.0;
#endif
    ColoNPC* npc = &s->npcs[idx];
    ColoManticoreState* mc = colo_npc_manticore(npc);
    AttackStyle style = mc->orb_style[mc->cycle_step];
    int max_hit = col_manticore_orb_max_hit(style);
    int mayhem = col_mod_tier(s, COLO_MOD_MANTIMAYHEM);
    int shots = (mayhem >= 1) ? 2 : 1;   /* T1+: an extra projectile per orb */
    for (int shot = 0; shot < shots; shot++) {
        int hit_success = 0;
        int dmg = col_npc_roll_vs_player(s, stats, style, max_hit, &hit_success);
        int rolled_dmg = dmg;
        int prayed = 0;
        EncounterPendingHit hit = encounter_pending_hit_resolved_at_throw(
            dmg, 1, style, s->player.prayer, npc->type, idx, hit_success, &prayed);
        dmg = hit.damage;
        if (prayed) s->tick_scratch.prayer_correct++;
        col_log_prayer_event(s, npc->type, prayed, dmg);
        s->tick_scratch.styles_faced_mask |= (1 << style);
        s->tick_scratch.avoidable_threat_by_style[style] += (float)rolled_dmg;
        if (!prayed) s->tick_scratch.offpray_damage_this_tick += (float)dmg;
        col_push_player_pending_hit(s, hit);
    }
    npc->attacked_this_tick = 1;
    npc->attack_style_this_tick = style;
    s->tick_scratch.attacks_fired++;
    mc->cycle_step++;
    if (mc->cycle_step >= 3) {
        mc->cycle_step = -1;
        mc->orb_style[0] = ATTACK_STYLE_NONE;
        mc->orb_style[1] = ATTACK_STYLE_NONE;
        mc->orb_style[2] = ATTACK_STYLE_NONE;
        /* A19: 10-tick barrage-to-barrage. The timer is set on the LAST orb's
           tick, two ticks after the barrage started, so attack_speed - 2 makes
           the next orb 0 land exactly attack_speed (10) ticks after this one's
           — the 7-tick recharge + 3 orb ticks of the researched cycle. */
        npc->attack_timer = stats->attack_speed - 2;
    }
#ifdef COLO_PROFILE_ENABLED
    COLO_PROFILE_MARK(COLO_PROF_STEP_MANTICORE_BARRAGE);
#endif
}

/** advance a manticore's in-flight barrage (fires the next queued orb). */
static void col_npc_manticore_advance_barrage(ColosseumState* s, int idx, const ColoNpcStats* stats) {
    ColoNPC* npc = &s->npcs[idx];
    if (npc->type_state.manticore.cycle_step < 0) return;
    if (npc->stun_timer > 0 || npc->frozen_ticks > 0) return;
    col_npc_manticore_fire_orb(s, idx, stats);
}

/** B10: from wave 9, a manticore's fixed cycle is copied onto idle peers within
    range and line of sight. The peer still exposes it only after arming. */
static void col_manticore_copy_pattern_to_peers(ColosseumState* s, int idx) {
    if (s->wave < COLO_MANTICORE_COPY_MIN_WAVE) return;
    ColoNPC* src = &s->npcs[idx];
    ColoManticoreState* smc = colo_npc_manticore(src);
    int scx = src->x + 1, scy = src->y + 1;
    for (int i = 0; i < COLO_MAX_NPCS; i++) {
        if (i == idx) continue;
        ColoNPC* other = &s->npcs[i];
        if (!col_npc_is_live_target(other)) continue;
        if (other->type != COLO_MANTICORE) continue;
        ColoManticoreState* omc = colo_npc_manticore(other);
        if (omc->cycle_step >= 0) continue;
        if (omc->orb_style[0] != ATTACK_STYLE_NONE) continue;
        int ocx = other->x + 1, ocy = other->y + 1;
        int dx = ocx > scx ? ocx - scx : scx - ocx;
        int dy = ocy > scy ? ocy - scy : scy - ocy;
        if ((dx > dy ? dx : dy) > COLO_MANTICORE_COPY_RANGE) continue;
        if (!col_tiles_have_los(s, scx, scy, ocx, ocy)) continue;
        for (int o = 0; o < 3; o++) omc->fixed_orb_style[o] = smc->fixed_orb_style[o];
        omc->pattern_copied = 1;
    }
}

/** Arm the next barrage by copying the fixed cycle into the visible queue. */
static void col_npc_manticore_arm(ColosseumState* s, int idx) {
    ColoManticoreState* mc = colo_npc_manticore(&s->npcs[idx]);
    if (mc->cycle_step >= 0) return;
    if (mc->orb_style[0] != ATTACK_STYLE_NONE) return;
    for (int o = 0; o < 3; o++) mc->orb_style[o] = mc->fixed_orb_style[o];
    mc->cycle_step = 0;
    col_manticore_copy_pattern_to_peers(s, idx);
}

/** start a manticore barrage once its charge completes: a ready manticore
    staggers 5 ticks if a peer is mid-barrage; otherwise it fires orb 0 of the
    already-armed pattern this tick. */
static void col_npc_attack_manticore(ColosseumState* s, int idx, const ColoNpcStats* stats) {
    ColoNPC* npc = &s->npcs[idx];
    ColoManticoreState* mc = colo_npc_manticore(npc);

    if (col_any_other_manticore_mid_barrage(s, idx)) {
        npc->attack_timer = COLO_MANTICORE_STAGGER_TICKS;
        return;
    }
    mc->pattern_copied = 0;
    mc->cycle_step = 0;
    col_npc_manticore_fire_orb(s, idx, stats);
}


/**
 * Per-NPC attack routine: style selection, accuracy + damage rolls, prayer-aware
 * pending-hit queueing. Mirrors inferno's deferred prayer-check pattern. Sol
 * Heredit is driven by col_sol_tick, not here.
 */
static void col_npc_attack_ctx(ColosseumState* s, ColosseumContext* ctx, int idx) {
    assert(ctx);
    ColoNPC* npc = &s->npcs[idx];
    const ColoNpcStats* stats = &COLO_NPC_STATS[npc->type];

    if (npc->type == COLO_SOL_HEREDIT) return;  /* boss handled in col_sol_tick */
    /* B2(a): warbanders attack on the shared wave-anchored cycle, resolved in
       col_warband_attack_phase after the player's move phase — never here. */
    if (col_type_is_warbander(npc->type)) return;

    /* an in-flight manticore barrage fires its remaining orbs 1 tick apart,
       independent of the main attack cadence. */
    if (npc->type == COLO_MANTICORE && npc->type_state.manticore.cycle_step > 0) {
        col_npc_manticore_advance_barrage(s, idx, stats);
        return;
    }

    if (npc->attack_timer > 0) npc->attack_timer--;
    if (npc->stun_timer > 0 || npc->frozen_ticks > 0) return;

    /* manticore: lock + telegraph the next barrage during the charge-up, then
       fire when the charge completes. Arming leaves attack_timer alone, so the
       cadence matches the legacy fire-time roll exactly; only the reveal moves
       earlier. Requires range + LoS to "spot" the player (A19). */
    if (npc->type == COLO_MANTICORE) {
        int mdist = col_npc_dist_to_player(s, npc);
        if (mdist < 1 || mdist > stats->attack_range) return;
        if (!col_npc_has_los_to_player(s, npc)) return;
        int cycle_step_before_arm = npc->type_state.manticore.cycle_step;
        col_npc_manticore_arm(s, idx);
        if (cycle_step_before_arm < 0 &&
                npc->type_state.manticore.cycle_step == 0 &&
                npc->attack_timer > 0) {
            ctx->manticore_charge_started[idx] = 1;
        }
        if (npc->attack_timer > 0) return;          /* still charging */
        col_npc_attack_manticore(s, idx, stats);    /* fire orb 0 (or stagger) */
        return;
    }

    if (npc->attack_timer > 0) return;

    AttackStyle style = stats->default_style;
    int dist = col_npc_dist_to_player(s, npc);

    /* A13+D9: the minotaur runs its 5-tick action timer whether or not it can
       reach the player. In melee distance (incl. diagonal) it crushes — melee
       has priority over healing — with instant same-tick (non-reactable) damage;
       otherwise it heals ONE eligible ally to full. */
    if (npc->type == COLO_MINOTAUR) {
        if (dist == 1) col_npc_melee_attack(s, ctx, idx, stats, stats->max_hit);
        else col_npc_minotaur_heal_ally(s, idx);
        npc->attack_timer = stats->attack_speed;
        return;
    }

    /* ranged/magic NPCs need range + line of sight; melee needs cardinal contact. */
    if (stats->attack_range > 1) {
        if (dist < 1 || dist > stats->attack_range) return;
        if (!col_npc_has_los_to_player(s, npc)) return;
    } else {
        /* melee: OSRS cardinal contact only (no diagonal). The minotaur (above)
           keeps Chebyshev per the documented research decision (divergences #33);
           the jaguar (size 2, the wave 1-2 reinforcement) uses the faithful
           footprint-cardinal gate so the diagonal safespot exists. */
        if (!col_npc_melee_adjacent_to_player(s, npc)) return;
    }

    switch (npc->type) {
        case COLO_JAGUAR_WARRIOR:
            col_npc_attack_jaguar(s, ctx, idx, stats);
            break;
        case COLO_JAVELIN_COLOSSUS:
            col_npc_attack_javelin(s, ctx, idx, stats);
            break;
        case COLO_SERPENT_SHAMAN:
        case COLO_SHOCKWAVE_COLOSSUS:
            col_npc_queue_projectile(s, idx, stats, style, stats->max_hit);
            break;
        default:
            return;
    }
    npc->attack_timer = stats->attack_speed;
}

/** A5+D33: warbander melee gate — cardinal-only adjacency (1x1 OSRS melee). */
static int col_warband_melee_adjacent(const ColosseumState* s, const ColoNPC* npc) {
    int dx = npc->x > s->player.x ? npc->x - s->player.x : s->player.x - npc->x;
    int dy = npc->y > s->player.y ? npc->y - s->player.y : s->player.y - npc->y;
    return dx + dy == 1;
}

/**
 * B2: the warband's shared 6-tick attack cycle, anchored to the wave's first
 * actionable tick N (warband_cycle_anchor): berserker window N+1, seer N+2,
 * archer N+3, then 3 silent ticks. A member attacks on its window only when it
 * is cardinally adjacent (A5) and has held its tile for two consecutive ticks
 * (it moved on neither this tick nor last): the first stationary tick is the
 * researched grace that lets the player attack then step before the second.
 * Movement is the dodge — a member forced to chase moves and resets the count.
 * Quartet extras share the window of their species. Runs AFTER the player's
 * move phase of the same tick.
 *
 * attack_timer no longer gates these attacks; it is rewritten every live tick
 * as ticks-until-window so the obs slot stays meaningful.
 */
static void col_warband_attack_phase(ColosseumState* s, ColosseumContext* ctx) {
#ifdef COLO_PROFILE_ENABLED
    int col_prof_enabled = COLO_PROFILE_ENABLED();
    double col_prof_t0 = col_prof_enabled ? COLO_PROFILE_NOW_MS() : 0.0;
#endif
    if (s->warband_cycle_anchor < 0) {
#ifdef COLO_PROFILE_ENABLED
        COLO_PROFILE_MARK(COLO_PROF_STEP_WARBAND_ATTACK);
#endif
        return;
    }
    int phase = (s->tick - s->warband_cycle_anchor) % COLO_WARBAND_CYCLE_TICKS;
    for (int i = 0; i < COLO_MAX_NPCS; i++) {
        ColoNPC* npc = &s->npcs[i];
        if (!col_npc_is_live_target(npc)) continue;
        if (!col_type_is_warbander(npc->type)) continue;
        int wait = (col_warband_window_offset(npc->type) - phase
                    + COLO_WARBAND_CYCLE_TICKS) % COLO_WARBAND_CYCLE_TICKS;
        npc->attack_timer = wait;
        if (wait != 0) continue;
        if (npc->stun_timer > 0 || npc->frozen_ticks > 0) continue;
        if (npc->moved_this_tick || npc->moved_last_tick) continue;
        if (!col_warband_melee_adjacent(s, npc)) continue;
        const ColoNpcStats* stats = &COLO_NPC_STATS[npc->type];
        if (npc->type == COLO_FREMENNIK_BERSERKER)
            col_npc_melee_attack(s, ctx, i, stats, stats->max_hit);
        else
            col_npc_queue_projectile(s, i, stats, stats->default_style, stats->max_hit);
    }
#ifdef COLO_PROFILE_ENABLED
    COLO_PROFILE_MARK(COLO_PROF_STEP_WARBAND_ATTACK);
#endif
}

/**
 * Prayer-oracle scaffold helper: the style an NPC will THROW this tick, or
 * ATTACK_STYLE_NONE. On this branch ranged/magic prayer locks at the throw tick
 * (col_npc_queue_projectile freezes damage vs the live overhead, check_prayer=0),
 * so the oracle must set the overhead BEFORE the throw, keyed on who fires now —
 * not on the already-frozen landing queue. Mirrors the firing gate of
 * col_npc_attack_ctx (attack_timer<=1 pre-decrement + range/LoS or cardinal
 * adjacency) and col_warband_attack_phase (wave-cycle window + adjacency). It may
 * OVER-report a not-quite-firing NPC (e.g. a warband member that will be forced to
 * move this tick, whose stationary gate is unknowable here); by the priority order
 * below that only ever deprioritizes a lower style, never mis-blocks a higher one.
 */
static AttackStyle col_oracle_incoming_style(const ColosseumState* s, const ColoNPC* npc) {
    if (!col_npc_is_live_enemy(npc)) return ATTACK_STYLE_NONE;
    if (npc->stun_timer > 0 || npc->frozen_ticks > 0) return ATTACK_STYLE_NONE;
    const ColoNpcStats* st = &COLO_NPC_STATS[npc->type];

    /* warband: fires on its wave-anchored window when cardinally adjacent (A5);
       attack_timer is a display countdown here, so gate on the cycle phase. */
    if (col_type_is_warbander(npc->type)) {
        if (s->warband_cycle_anchor < 0) return ATTACK_STYLE_NONE;
        int phase = (s->tick - s->warband_cycle_anchor + COLO_WARBAND_CYCLE_TICKS)
                    % COLO_WARBAND_CYCLE_TICKS;
        if (phase != col_warband_window_offset(npc->type)) return ATTACK_STYLE_NONE;
        if (!col_warband_melee_adjacent(s, npc)) return ATTACK_STYLE_NONE;
        return st->default_style;
    }

    /* manticore: an in-flight orb barrage fires one orb per tick; otherwise it
       fires when its charge completes (attack_timer reaches 0) with range + LoS. */
    if (npc->type == COLO_MANTICORE) {
        if (npc->type_state.manticore.cycle_step > 0) return st->default_style;
        if (npc->attack_timer > 1) return ATTACK_STYLE_NONE;
        int d = col_npc_dist_to_player(s, npc);
        if (d < 1 || d > st->attack_range) return ATTACK_STYLE_NONE;
        if (!col_npc_has_los_to_player(s, npc)) return ATTACK_STYLE_NONE;
        return st->default_style;
    }

    /* minotaur: melee crush only when Chebyshev-adjacent (else it heals = no threat). */
    if (npc->type == COLO_MINOTAUR) {
        if (npc->attack_timer > 1) return ATTACK_STYLE_NONE;
        return (col_npc_dist_to_player(s, npc) == 1) ? ATTACK_STYLE_MELEE : ATTACK_STYLE_NONE;
    }

    /* standard NPCs (shaman, shockwave, javelin = ranged/magic; jaguar = melee). */
    if (npc->attack_timer > 1) return ATTACK_STYLE_NONE;
    if (st->attack_range > 1) {
        int d = col_npc_dist_to_player(s, npc);
        if (d < 1 || d > st->attack_range) return ATTACK_STYLE_NONE;
        if (!col_npc_has_los_to_player(s, npc)) return ATTACK_STYLE_NONE;
        return st->default_style;
    }
    return col_npc_melee_adjacent_to_player(s, npc) ? ATTACK_STYLE_MELEE : ATTACK_STYLE_NONE;
}

/**
 * Prayer oracle (scaffold, gated by ColConfig.prayer_oracle_mode; default off).
 * Overrides the player's overhead each tick with the perfect single protect-prayer
 * against this tick's throwers, priority MAGIC > MELEE > RANGED (caller's spec).
 * One overhead per tick, so multi-style-conflict ticks and all typeless/AoE still
 * bleed by design — this removes only the prayer-SELECTION burden, isolating it in
 * the solve-by-cheating ablation. Runs after col_player_pretick and before
 * col_tick_npcs so the chosen overhead is live when each NPC's throw checks it.
 */
static void col_apply_prayer_oracle(ColosseumState* s) {
    int magic = 0, melee = 0, ranged = 0;
    for (int i = 0; i < COLO_MAX_NPCS; i++) {
        AttackStyle style = col_oracle_incoming_style(s, &s->npcs[i]);
        if (style == ATTACK_STYLE_MAGIC) magic = 1;
        else if (style == ATTACK_STYLE_MELEE) melee = 1;
        else if (style == ATTACK_STYLE_RANGED) ranged = 1;
    }
    if (magic) s->player.prayer = PRAYER_PROTECT_MAGIC;
    else if (melee) s->player.prayer = PRAYER_PROTECT_MELEE;
    else if (ranged) s->player.prayer = PRAYER_PROTECT_RANGED;
    /* no incoming this tick: leave whatever the agent set. */
}

/**
 * Sol Heredit boss tick. Drives the wave-12 boss: phase transitions, the AoE
 * spear/shield rotation, the triple-parry combo, the grapple, and the rotating
 * crystal + enrage hazards. The machine lives in encounter_colosseum_boss.inc
 * (included after this file) to keep combat.inc under ~500 lines.
 */
static void col_sol_tick(ColosseumState* s, ColosseumContext* ctx) {
    col_sol_boss_tick(s, ctx);
}

/** the NPC update loop, mirroring inferno's tick_npcs. */
static void col_tick_npcs_ctx(ColosseumState* s, ColosseumContext* ctx) {
#ifdef COLO_PROFILE_ENABLED
    int col_prof_enabled = COLO_PROFILE_ENABLED();
    double col_prof_total_t0 = col_prof_enabled ? COLO_PROFILE_NOW_MS() : 0.0;
    double col_prof_t0 = col_prof_total_t0;
#endif
    col_sol_tick(s, ctx);
#ifdef COLO_PROFILE_ENABLED
    COLO_PROFILE_MARK(COLO_PROF_STEP_SOL_BOSS);
#endif

    /* resolve in-flight javelin skyfalls before movement: the marked tile lands on
       whatever tile the player occupied this tick (dodged if they have stepped off). */
    for (int i = 0; i < COLO_MAX_NPCS; i++) {
        ColoNPC* npc = &s->npcs[i];
        if (col_npc_is_live_target(npc) && npc->type == COLO_JAVELIN_COLOSSUS)
            col_npc_resolve_javelin_skyfall(s, ctx, i);
    }
#ifdef COLO_PROFILE_ENABLED
    COLO_PROFILE_MARK(COLO_PROF_STEP_JAVELIN_SKYFALL);
#endif

    for (int i = 0; i < COLO_MAX_NPCS; i++) {
        ColoNPC* npc = &s->npcs[i];
        if (!npc->active) continue;

        if (npc->death_ticks > 0) continue;
        if (npc->stun_timer > 0) npc->stun_timer--;
        if (npc->frozen_ticks > 0) npc->frozen_ticks--;

        /* hazard entities are driven by the modifier bookkeeping, not the AI. */
        if (col_type_is_hazard_entity(npc->type)) continue;

#ifdef COLO_PROFILE_ENABLED
        double col_prof_call_t0 = col_prof_enabled ? COLO_PROFILE_NOW_MS() : 0.0;
#endif
        col_npc_move_ctx(s, ctx, i);
#ifdef COLO_PROFILE_ENABLED
        if (col_prof_enabled)
            COLO_PROFILE_ADD(
                COLO_PROF_STEP_NPC_MOVEMENT,
                COLO_PROFILE_NOW_MS() - col_prof_call_t0);
        col_prof_call_t0 = col_prof_enabled ? COLO_PROFILE_NOW_MS() : 0.0;
#endif
        col_npc_attack_ctx(s, ctx, i);
#ifdef COLO_PROFILE_ENABLED
        if (col_prof_enabled)
            COLO_PROFILE_ADD(
                COLO_PROF_STEP_NPC_ATTACK,
                COLO_PROFILE_NOW_MS() - col_prof_call_t0);
#endif
    }
#ifdef COLO_PROFILE_ENABLED
    if (col_prof_enabled)
        COLO_PROFILE_ADD(COLO_PROF_STEP_NPC_TOTAL, COLO_PROFILE_NOW_MS() - col_prof_total_t0);
#endif
}
