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
    s->log.total_npc_kills++;
    col_deactivate_npc(s, idx);
}

/** resolve player attack projectiles landing on NPCs. A hit that lands this tick
    is removed from the queue (the shared resolver only flags it inactive), so a
    long fight like Sol does not pile up resolved hits until the queue overflows. */
static void col_resolve_player_projectiles_on_npcs(ColosseumState* s) {
    for (int i = 0; i < COLO_MAX_NPCS; i++) {
        ColoNPC* npc = &s->npcs[i];
        if (!npc->active) continue;
        for (int h = 0; h < npc->pending_hits.count; h++) {
            EncounterPendingHit* ph = &npc->pending_hits.hits[h];
            int landed = 0, hit_damage = 0;
            float dmg_acc = 0.0f;
            int frozen = npc->frozen_ticks;
            if (encounter_resolve_npc_pending_hit(
                    ph, &npc->hp, &landed, &hit_damage, &frozen, NULL, &dmg_acc)) {
                npc->hit_landed_this_tick = landed;
                npc->hit_damage = hit_damage;
                npc->hit_was_successful_this_tick = (hit_damage > 0);
                s->tick_scratch.damage_dealt += dmg_acc;
                if (npc->type == COLO_SOL_HEREDIT)
                    s->tick_scratch.damage_boss += dmg_acc;
                if (npc->hp <= 0) {
                    col_apply_npc_death(s, i);
                    break;
                }
                encounter_pending_hit_queue_remove(&npc->pending_hits, h, "colosseum-npc");
                h--;
            }
        }
    }
}

/** resolve NPC attack projectiles landing on the player. */
static void col_resolve_player_pending_hits(ColosseumState* s) {
    int prayer_correct = 0;
    int off_prayer = 0;
    float dmg_received = 0.0f;
    encounter_resolve_player_pending_hits(
        &s->player_pending_hits, &s->player, s->player.prayer,
        &dmg_received, &prayer_correct, &off_prayer);
    s->tick_scratch.damage_received += dmg_received;
    s->tick_scratch.prayer_correct += prayer_correct;
}

/** rect distance from the player to an NPC footprint (closest tile). */
static int col_npc_dist_to_player(const ColosseumState* s, const ColoNPC* npc) {
    return encounter_dist_to_npc(
        s->player.x, s->player.y, npc->x, npc->y, col_npc_effective_size(npc));
}

/** player defence roll against an incoming NPC attack of the given style. */
static int col_player_def_roll(const ColosseumState* s, AttackStyle style, MeleeStyle melee_style) {
    const EncounterLoadoutStats* ls = &s->loadout_stats[s->weapon_set];
    int def_bonus = encounter_player_def_bonus(
        ls->def_stab, ls->def_slash, ls->def_crush, ls->def_magic, ls->def_ranged,
        style, melee_style);
    return osrs_player_def_roll_vs_npc(
        s->player.current_defence, s->player.current_magic, def_bonus, style);
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
    int dmg = encounter_rand_int(&s->rng_state, max_hit + 1);
    *hit_success = encounter_roll_hit_chance(&s->rng_state, att_roll, def_roll);
    return *hit_success ? dmg : 0;
}

/** projectile damage delay (ticks) for an NPC ranged/magic attack at a distance. */
static int col_npc_projectile_delay(AttackStyle style, int distance) {
    EncounterProjectileDelayKind kind = (style == ATTACK_STYLE_MAGIC)
        ? ENCOUNTER_PROJECTILE_DELAY_MAGIC
        : ENCOUNTER_PROJECTILE_DELAY_RANGED;
    EncounterProjectileDelayOptions opts = {0};
    EncounterProjectileTiming timing = encounter_projectile_timing(distance, 0, kind, opts);
    return timing.damage_delay_ticks;
}

/** queue a normal NPC ranged/magic attack on the player with deferred prayer check
    so flicking at land time matters. */
static void col_npc_queue_projectile(
    ColosseumState* s, int idx, const ColoNpcStats* stats, AttackStyle style, int max_hit
) {
    ColoNPC* npc = &s->npcs[idx];
    int dist = col_npc_dist_to_player(s, npc);
    int hit_success = 0;
    int dmg = col_npc_roll_vs_player(s, stats, style, max_hit, &hit_success);
    int delay = col_npc_projectile_delay(style, dist);
    col_queue_player_pending_hit(s, idx, npc->type, dmg, delay, style, 1, hit_success);
    npc->attacked_this_tick = 1;
    npc->attack_style_this_tick = style;
    s->tick_scratch.attacks_fired++;
}

/** queue a melee NPC attack on the player (1-tick deferral, prayer checked at land). */
static void col_npc_queue_melee(
    ColosseumState* s, int idx, const ColoNpcStats* stats, int max_hit
) {
    ColoNPC* npc = &s->npcs[idx];
    int hit_success = 0;
    int dmg = col_npc_roll_vs_player(s, stats, ATTACK_STYLE_MELEE, max_hit, &hit_success);
    col_queue_player_pending_hit(s, idx, npc->type, dmg, 1, ATTACK_STYLE_MELEE, 1, hit_success);
    npc->attacked_this_tick = 1;
    npc->attack_style_this_tick = ATTACK_STYLE_MELEE;
    s->tick_scratch.attacks_fired++;
}


/** Jaguar warrior: 3 independent melee hits per attack, each up to max_hit (47),
    each its own accuracy + damage roll, each checked against Protect-from-Melee. */
static void col_npc_attack_jaguar(ColosseumState* s, int idx, const ColoNpcStats* stats) {
    for (int h = 0; h < 3; h++)
        col_npc_queue_melee(s, idx, stats, stats->max_hit);
}

/** Minotaur: heal every wounded NPC within 6 tiles each attack cycle, unless the
    player is within the minotaur's melee range (heal suppressed). */
static void col_npc_minotaur_heal_allies(ColosseumState* s, int idx) {
    ColoNPC* mino = &s->npcs[idx];
    for (int i = 0; i < COLO_MAX_NPCS; i++) {
        if (i == idx) continue;
        ColoNPC* other = &s->npcs[i];
        if (!other->active || other->death_ticks > 0 || other->hp <= 0) continue;
        if (other->hp >= other->max_hp) continue;
        int d = encounter_rect_distance(
            mino->x, mino->y, col_npc_effective_size(mino),
            other->x, other->y, col_npc_effective_size(other));
        if (d > 6) continue;
        int heal = encounter_rand_int(&s->rng_state, 11);  /* 0..10 per cycle */
        other->hp += heal;
        if (other->hp > other->max_hp) other->hp = other->max_hp;
    }
}

/** Javelin Colossus: normal throw is Protect-from-Missiles blockable; every 5th
    throw is a skyfall that marks the player's current tile and lands 3 ticks later
    ignoring Protect-from-Missiles (dodge by leaving the tile). */
static void col_npc_attack_javelin(ColosseumState* s, int idx, const ColoNpcStats* stats) {
    ColoNPC* npc = &s->npcs[idx];
    ColoJavelinState* jv = colo_npc_javelin(npc);
    jv->attack_count++;
    if (jv->attack_count % 5 == 0) {
        int hit_success = 0;
        int dmg = col_npc_roll_vs_player(s, stats, ATTACK_STYLE_RANGED, stats->max_hit, &hit_success);
        jv->skyfall_pending = 1;
        jv->skyfall_tile_x = s->player.x;
        jv->skyfall_tile_y = s->player.y;
        jv->skyfall_timer = COLO_JAVELIN_SKYFALL_DELAY;
        jv->skyfall_damage = hit_success ? dmg : 0;
        npc->attacked_this_tick = 1;
        npc->attack_style_this_tick = ATTACK_STYLE_RANGED;
        s->tick_scratch.attacks_fired++;
        return;
    }
    col_npc_queue_projectile(s, idx, stats, ATTACK_STYLE_RANGED, stats->max_hit);
}

/** resolve a javelin's in-flight skyfall: land on the marked tile, ignoring
    Protect-from-Missiles. The player dodges by standing off the tile at land time. */
static void col_npc_resolve_javelin_skyfall(ColosseumState* s, int idx) {
    ColoNPC* npc = &s->npcs[idx];
    ColoJavelinState* jv = colo_npc_javelin(npc);
    if (!jv->skyfall_pending) return;
    jv->skyfall_timer--;
    if (jv->skyfall_timer > 0) return;
    jv->skyfall_pending = 0;
    int on_tile = (s->player.x == jv->skyfall_tile_x && s->player.y == jv->skyfall_tile_y);
    int dmg = on_tile ? jv->skyfall_damage : 0;
    encounter_damage_player(&s->player, dmg, &s->tick_scratch.damage_received);
    if (dmg > 0) s->last_hit_by_type = npc->type;
}

/** Manticore: 10-tick cycle 3-orb barrage. orbs 0&1 are {ranged, magic} in random
    order, orb 2 is always melee. orbs fire 1 tick apart with 0 flight time, each
    prayed individually. A ready manticore staggers 5 ticks if another is mid-barrage. */
static int col_any_other_manticore_mid_barrage(const ColosseumState* s, int idx) {
    for (int i = 0; i < COLO_MAX_NPCS; i++) {
        if (i == idx) continue;
        const ColoNPC* other = &s->npcs[i];
        if (!other->active || other->type != COLO_MANTICORE) continue;
        if (other->type_state.manticore.cycle_step >= 0) return 1;
    }
    return 0;
}

static int col_manticore_orb_max_hit(AttackStyle style) {
    if (style == ATTACK_STYLE_RANGED) return COLO_MANTICORE_MAX_HIT_RANGED;
    if (style == ATTACK_STYLE_MAGIC) return COLO_MANTICORE_MAX_HIT_MAGIC;
    return COLO_MANTICORE_MAX_HIT_MELEE;
}

/** fire one manticore orb (flight time 0): each orb prayed individually. */
static void col_npc_manticore_fire_orb(ColosseumState* s, int idx, const ColoNpcStats* stats) {
    ColoNPC* npc = &s->npcs[idx];
    ColoManticoreState* mc = colo_npc_manticore(npc);
    AttackStyle style = mc->orb_style[mc->cycle_step];
    int max_hit = col_manticore_orb_max_hit(style);
    int hit_success = 0;
    int dmg = col_npc_roll_vs_player(s, stats, style, max_hit, &hit_success);
    /* flight time 0: lands next tick, prayer checked individually at land. */
    col_queue_player_pending_hit(s, idx, npc->type, dmg, 1, style, 1, hit_success);
    npc->attacked_this_tick = 1;
    npc->attack_style_this_tick = style;
    s->tick_scratch.attacks_fired++;
    mc->cycle_step++;
    if (mc->cycle_step >= 3) {
        mc->cycle_step = -1;
        npc->attack_timer = stats->attack_speed;  /* 10-tick cycle restart */
    }
}

/** advance a manticore's in-flight barrage (fires the next queued orb). */
static void col_npc_manticore_advance_barrage(ColosseumState* s, int idx, const ColoNpcStats* stats) {
    ColoNPC* npc = &s->npcs[idx];
    if (npc->type_state.manticore.cycle_step < 0) return;
    if (npc->stun_timer > 0 || npc->frozen_ticks > 0) return;
    col_npc_manticore_fire_orb(s, idx, stats);
}

/** start a manticore barrage: roll the 3 orb styles and fire orb 0 this tick. */
static void col_npc_attack_manticore(ColosseumState* s, int idx, const ColoNpcStats* stats) {
    ColoNPC* npc = &s->npcs[idx];
    ColoManticoreState* mc = colo_npc_manticore(npc);

    if (col_any_other_manticore_mid_barrage(s, idx)) {
        npc->attack_timer = COLO_MANTICORE_STAGGER_TICKS;
        return;
    }

    /* orbs 0 & 1 are {ranged, magic} in random order; orb 2 is always melee. */
    if (encounter_rand_int(&s->rng_state, 2) == 0) {
        mc->orb_style[0] = ATTACK_STYLE_RANGED;
        mc->orb_style[1] = ATTACK_STYLE_MAGIC;
    } else {
        mc->orb_style[0] = ATTACK_STYLE_MAGIC;
        mc->orb_style[1] = ATTACK_STYLE_RANGED;
    }
    mc->orb_style[2] = ATTACK_STYLE_MELEE;
    mc->cycle_step = 0;
    col_npc_manticore_fire_orb(s, idx, stats);
}


/**
 * Per-NPC attack routine: style selection, accuracy + damage rolls, prayer-aware
 * pending-hit queueing. Mirrors inferno's deferred prayer-check pattern. Sol
 * Heredit is driven by col_sol_tick, not here.
 */
static void col_npc_attack_ctx(ColosseumState* s, ColosseumContext* ctx, int idx) {
    (void)ctx;
    ColoNPC* npc = &s->npcs[idx];
    const ColoNpcStats* stats = &COLO_NPC_STATS[npc->type];

    if (npc->type == COLO_SOL_HEREDIT) return;  /* boss handled in col_sol_tick */

    /* an in-flight manticore barrage fires its remaining orbs 1 tick apart,
       independent of the main attack cadence. */
    if (npc->type == COLO_MANTICORE && npc->type_state.manticore.cycle_step >= 0) {
        col_npc_manticore_advance_barrage(s, idx, stats);
        return;
    }

    if (npc->attack_timer > 0) npc->attack_timer--;
    if (npc->stun_timer > 0 || npc->frozen_ticks > 0) return;
    if (npc->attack_timer > 0) return;

    AttackStyle style = stats->default_style;
    int dist = col_npc_dist_to_player(s, npc);

    /* Minotaur runs its cycle whether or not it can reach the player: in melee
       contact it crushes, otherwise it heals wounded allies within 6 tiles. */
    if (npc->type == COLO_MINOTAUR) {
        if (dist == 1) col_npc_queue_melee(s, idx, stats, stats->max_hit);
        else col_npc_minotaur_heal_allies(s, idx);
        npc->attack_timer = stats->attack_speed;
        return;
    }

    /* ranged/magic NPCs need range + line of sight; melee needs cardinal contact. */
    if (stats->attack_range > 1) {
        if (dist < 1 || dist > stats->attack_range) return;
        if (!col_npc_has_los_to_player(s, npc)) return;
    } else {
        if (dist != 1) return;  /* melee: cardinal contact only */
    }

    switch (npc->type) {
        case COLO_JAGUAR_WARRIOR:
            col_npc_attack_jaguar(s, idx, stats);
            break;
        case COLO_JAVELIN_COLOSSUS:
            col_npc_attack_javelin(s, idx, stats);
            break;
        case COLO_MANTICORE:
            col_npc_attack_manticore(s, idx, stats);
            return;  /* manticore sets its own timer at barrage end */
        case COLO_FREMENNIK_BERSERKER:
            col_npc_queue_melee(s, idx, stats, stats->max_hit);
            break;
        case COLO_FREMENNIK_ARCHER:
        case COLO_FREMENNIK_SEER:
        case COLO_SERPENT_SHAMAN:
        case COLO_SHOCKWAVE_COLOSSUS:
            col_npc_queue_projectile(s, idx, stats, style, stats->max_hit);
            break;
        default:
            return;
    }
    npc->attack_timer = stats->attack_speed;
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
    col_sol_tick(s, ctx);

    /* resolve in-flight javelin skyfalls before movement: the marked tile lands on
       whatever tile the player occupied this tick (dodged if they have stepped off). */
    for (int i = 0; i < COLO_MAX_NPCS; i++) {
        ColoNPC* npc = &s->npcs[i];
        if (npc->active && npc->type == COLO_JAVELIN_COLOSSUS)
            col_npc_resolve_javelin_skyfall(s, i);
    }

    for (int i = 0; i < COLO_MAX_NPCS; i++) {
        ColoNPC* npc = &s->npcs[i];
        if (!npc->active) continue;

        if (npc->death_ticks > 0) {
            npc->death_ticks--;
            if (npc->death_ticks == 0) col_deactivate_npc(s, i);
            continue;
        }
        if (npc->stun_timer > 0) npc->stun_timer--;
        if (npc->frozen_ticks > 0) npc->frozen_ticks--;

        col_npc_move_ctx(s, ctx, i);
        col_npc_attack_ctx(s, ctx, i);
    }
}
