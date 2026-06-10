#line 76 "encounter_colosseum.h"
/**
 * @file encounter_colosseum_boss.inc
 * @brief Sol Heredit (wave 12) boss state machine.
 *
 * Structured like inferno's inf_zuk_tick: each tick we find the live boss, run
 * HP-threshold phase transitions, then drive Sol's offense. Sol has NO documented
 * auto-attack outside the named specials (mechanics.md 5.6 UNCONFIRMED), so the
 * machine is exactly: the spear/shield AoE rotation, the triple-parry combo, the
 * grapple, the rotating-crystal spheres, and the enrage molten sand.
 *
 * Every damage source here is typeless (prayer never reduces it): the player
 * survives by dodging (AoE, spheres, molten sand), flicking Protect-from-Melee on
 * the triple parry, or clicking the called slot on the grapple. All damage routes
 * through encounter_damage_player so the shared HP/splat/tracker bookkeeping holds.
 */

/* ----- phase + arena helpers ------------------------------------------------ */

/** boss arena is clamping movement iff wave 12 is live and the machine started. */
static int col_sol_clamp_active(const ColosseumState* s) {
    return s->wave == COLO_WAVE_BOSS && s->sol.started;
}

static int col_in_boss_arena(const ColosseumState* s, int x, int y) {
    return x >= s->sol.boss_arena_min_x && x <= s->sol.boss_arena_max_x &&
           y >= s->sol.boss_arena_min_y && y <= s->sol.boss_arena_max_y;
}

/** HP-fraction phase: 0 (>90%), 1 (<90%), 2 (<75%), 3 (<50%), 4 (<25%), 5 (<10%). */
static int col_sol_phase_for_hp(int hp) {
    int max = COLO_SOL_HP_MAX;
    if (hp > (max * 90) / 100) return 0;
    if (hp > (max * 75) / 100) return 1;
    if (hp > (max * 50) / 100) return 2;
    if (hp > (max * 25) / 100) return 3;
    if (hp > (max * 10) / 100) return 4;
    return 5;
}

/** find the live Sol NPC slot, or -1 (mirrors inf_find_live_zuk_idx). */
static int col_sol_find_idx(const ColosseumState* s) {
    for (int i = 0; i < COLO_MAX_NPCS; i++) {
        const ColoNPC* npc = &s->npcs[i];
        if (npc->active && npc->death_ticks == 0 && npc->type == COLO_SOL_HEREDIT)
            return i;
    }
    return -1;
}

/**
 * Initialise the boss machine for wave 12: set the (9,9)-(24,24) interior bounds
 * (A8+D29; the static-mask pillars clip the corners), drop the player onto the
 * south-centre start tile (D27), and zero the boss sub-state so a fight reached
 * by wave progression starts identically to a direct start_wave=12.
 */
static void col_sol_begin_boss_arena(ColosseumState* s) {
    s->sol = (SolHereditState){0};
    s->sol.started = 1;
    s->sol.boss_idx = -1;
    s->sol.phase = 0;
    s->sol.attack_timer = COLO_SOL_ATTACK_INTERVAL;
    s->sol.last_style = COLO_SOL_STYLE_NONE;
    s->sol.last_attack = COLO_SOL_AOE_NONE;
    s->sol.boss_arena_min_x = COLO_BOSS_ARENA_MIN_X;
    s->sol.boss_arena_min_y = COLO_BOSS_ARENA_MIN_Y;
    s->sol.boss_arena_max_x = COLO_BOSS_ARENA_MAX_X;
    s->sol.boss_arena_max_y = COLO_BOSS_ARENA_MAX_Y;

    s->player.x = COLO_BOSS_PLAYER_START_X;
    s->player.y = COLO_BOSS_PLAYER_START_Y;
    s->player_dest_x = -1;
    s->player_dest_y = -1;
    osrs_interaction_clear(&s->interaction);
    col_rebuild_player_collision_flags(s);
}

/* ----- AoE rotation --------------------------------------------------------- */

/** square footprint span (tiles) of an AoE attack's dust hazard. */
static int col_sol_aoe_span(int attack) {
    switch (attack) {
        case COLO_SOL_AOE_SPEAR1:  return 6;
        case COLO_SOL_AOE_SPEAR2:  return 6;
        case COLO_SOL_AOE_SHIELD1: return 7;
        case COLO_SOL_AOE_SHIELD2: return 9;
        default: return 0;
    }
}

/**
 * Origin of an AoE hazard block: a `span`-wide square centred on the 5x5 boss
 * footprint. The dust set is the whole block; the documented dodge is to step
 * clear of it (spears 6x6, shields 7x7 / 9x9 force a larger step-back). The exact
 * intra-footprint safe tiles for Spear1 vs Spear2 and the Shield1 "1-tile gap"
 * are UNCONFIRMED, so both spears are one 6x6 set and Shield1 is a solid 7x7.
 */
static void col_sol_aoe_block_origin(const ColoNPC* boss, int span, int* ox, int* oy) {
    int boss_size = boss->size > 0 ? boss->size : 5;
    *ox = boss->x + (boss_size - span) / 2;
    *oy = boss->y + (boss_size - span) / 2;
}

/** pick Sol's next AoE under the alternation rule, recording it for next time. */
static int col_sol_next_aoe(ColosseumState* s) {
    SolHereditState* sol = &s->sol;
    if (sol->last_attack == COLO_SOL_AOE_NONE) {
        sol->last_attack = COLO_SOL_AOE_SPEAR1;
        sol->last_style = COLO_SOL_STYLE_SPEAR;
        return COLO_SOL_AOE_SPEAR1;
    }

    /* alternate spear/shield (bias toward flipping); a same-style repeat forces
       the other sub-pattern so all four patterns appear over a fight. */
    int flip = encounter_rand_int(&s->rng_state, 10) < 6;
    int style = flip
        ? (sol->last_style == COLO_SOL_STYLE_SPEAR ? COLO_SOL_STYLE_SHIELD : COLO_SOL_STYLE_SPEAR)
        : sol->last_style;

    int attack;
    if (style == COLO_SOL_STYLE_SPEAR)
        attack = (style == sol->last_style && sol->last_attack == COLO_SOL_AOE_SPEAR1)
            ? COLO_SOL_AOE_SPEAR2 : COLO_SOL_AOE_SPEAR1;
    else
        attack = (style == sol->last_style && sol->last_attack == COLO_SOL_AOE_SHIELD1)
            ? COLO_SOL_AOE_SHIELD2 : COLO_SOL_AOE_SHIELD1;

    sol->last_style = style;
    sol->last_attack = attack;
    return attack;
}

/** resolve a telegraphed AoE: 44 typeless if the player is inside the dust set. */
static void col_sol_resolve_aoe(ColosseumState* s) {
    SolHereditState* sol = &s->sol;
    int idx = sol->boss_idx;
    if (idx < 0) { sol->aoe_attack = COLO_SOL_AOE_NONE; return; }
    const ColoNPC* boss = &s->npcs[idx];
    int span = col_sol_aoe_span(sol->aoe_attack);
    int ox, oy;
    col_sol_aoe_block_origin(boss, span, &ox, &oy);
    if (encounter_entity_footprints_overlap(ox, oy, span, s->player.x, s->player.y, 1)) {
        encounter_damage_player(&s->player, COLO_SOL_AOE_MAX_HIT, &s->tick_scratch.damage_received);
        s->last_hit_by_type = COLO_SOL_HEREDIT;
        col_mod_on_player_damaged(s, COLO_SOL_AOE_MAX_HIT);
    }
    sol->aoe_attack = COLO_SOL_AOE_NONE;
}

/* ----- triple parry --------------------------------------------------------- */

/** queue one triple-parry melee hit landing `land_tick` ticks out; the deferred
    prayer check locks in on the tick before it lands, so Protect-from-Melee on
    that tick blocks the hit (else `misprayed` typeless damage lands). */
static void col_sol_queue_parry_hit(ColosseumState* s, int idx, int land_tick, int misprayed) {
    EncounterPendingHit hit = {
        .active = 1,
        .damage = misprayed,
        .ticks_remaining = land_tick,
        .attack_style = ATTACK_STYLE_MELEE,
        .check_prayer = 1,
        .prayer_check_delay = land_tick - 1,
        .spell_type = ENCOUNTER_SPELL_NONE,
        .source_npc_type = COLO_SOL_HEREDIT,
        .source_npc_slot = idx,
        .hit_success = 1,
        .elysian_reduced = 0,
    };
    encounter_pending_hit_queue_push(
        &s->player_pending_hits, hit, "colosseum-sol-parry", s->tick, idx, COLO_SOL_HEREDIT);
}

/** start a 3-hit triple parry (available <90% HP). Hits land at +3/+6/+9 ticks,
    the 3rd slipping to +10 once Sol is under 50% HP. Combo A = 15/25/35, combo B
    = 15/30/45, alternating each invocation. */
static void col_sol_start_triple_parry(ColosseumState* s, int idx) {
    SolHereditState* sol = &s->sol;
    int low_hp = s->npcs[idx].hp <= (COLO_SOL_HP_MAX * 50) / 100;
    int third_gap = low_hp ? COLO_SOL_TP_HIT3_GAP_LOW : COLO_SOL_TP_HIT_GAP;
    int land1 = COLO_SOL_TP_HIT_GAP;
    int land2 = land1 + COLO_SOL_TP_HIT_GAP;
    int land3 = land2 + third_gap;

    static const int COMBO_A[3] = {15, 25, 35};
    static const int COMBO_B[3] = {15, 30, 45};
    const int* dmg = sol->parry_combo_b ? COMBO_B : COMBO_A;
    sol->parry_combo_b = !sol->parry_combo_b;

    col_sol_queue_parry_hit(s, idx, land1, dmg[0]);
    col_sol_queue_parry_hit(s, idx, land2, dmg[1]);
    col_sol_queue_parry_hit(s, idx, land3, dmg[2]);

    sol->parry_sequence_active = 1;
    sol->parry_ticks_remaining = land3;
    sol->combo_len = 3;
    sol->combo_cursor = 0;
    sol->combo_queue[0] = dmg[0];
    sol->combo_queue[1] = dmg[1];
    sol->combo_queue[2] = dmg[2];
}

/** advance triple-parry obs telemetry; the pending-hit queue does the real work. */
static void col_sol_tick_triple_parry(ColosseumState* s) {
    SolHereditState* sol = &s->sol;
    if (!sol->parry_sequence_active) return;
    if (sol->parry_ticks_remaining > 0) sol->parry_ticks_remaining--;
    if (sol->parry_ticks_remaining == 0) {
        sol->parry_sequence_active = 0;
        sol->combo_len = 0;
        sol->combo_cursor = 0;
    }
}

/* ----- grapple -------------------------------------------------------------- */

/** start a grapple (available <75% HP): Sol calls a body part; the player has
    COLO_SOL_GRAPPLE_WINDOW ticks to click that equipment slot via the hook. */
static void col_sol_start_grapple(ColosseumState* s) {
    SolHereditState* sol = &s->sol;
    sol->grapple_active = 1;
    sol->grapple_resolved = 0;
    sol->grapple_body_slot = encounter_rand_int(&s->rng_state, NUM_GEAR_SLOTS);
    sol->grapple_timer = COLO_SOL_GRAPPLE_WINDOW;
}

/**
 * Grapple parry hook for the obs/action phase: call when the player clicks an
 * equipment slot during an active grapple. A correct slot parries; clicking on
 * the last tick (grapple_timer == 1) is a perfect parry, arming a guaranteed-max
 * next attack for COLO_SOL_PERFECT_MAX_TICKS ticks. Returns 1 if the click
 * resolved the grapple, else 0. No action head exists yet — this is the seam.
 */
static int col_sol_grapple_parry_attempt(ColosseumState* s, int slot) {
    SolHereditState* sol = &s->sol;
    if (!sol->grapple_active || sol->grapple_resolved) return 0;
    if (slot != sol->grapple_body_slot) return 0;

    int perfect = sol->grapple_timer <= 1;
    sol->grapple_resolved = 1;
    sol->grapple_active = 0;
    sol->grapple_timer = 0;
    if (perfect) {
        sol->next_attack_guaranteed_max = 1;
        sol->guaranteed_max_ticks = COLO_SOL_PERFECT_MAX_TICKS;
    }
    return 1;
}

/** count down an active grapple; an unparried grapple lands 44 typeless. */
static void col_sol_tick_grapple(ColosseumState* s) {
    SolHereditState* sol = &s->sol;
    if (!sol->grapple_active) return;
    if (sol->grapple_timer > 0) sol->grapple_timer--;
    if (sol->grapple_timer == 0 && !sol->grapple_resolved) {
        encounter_damage_player(&s->player, COLO_SOL_GRAPPLE_MAX_HIT, &s->tick_scratch.damage_received);
        s->last_hit_by_type = COLO_SOL_HEREDIT;
        col_mod_on_player_damaged(s, COLO_SOL_GRAPPLE_MAX_HIT);
        sol->grapple_active = 0;
        sol->grapple_resolved = 1;
    }
}

/* ----- rotating crystal + spheres ------------------------------------------ */

/** crystal position walking clockwise around the boss-arena perimeter. */
static void col_sol_crystal_pos(const ColosseumState* s, int* cx, int* cy) {
    int min_x = s->sol.boss_arena_min_x, max_x = s->sol.boss_arena_max_x;
    int min_y = s->sol.boss_arena_min_y, max_y = s->sol.boss_arena_max_y;
    int w = max_x - min_x, h = max_y - min_y;
    int perim = 2 * (w + h);
    int step = perim > 0 ? s->sol.crystal_step % perim : 0;
    if (step < w)            { *cx = min_x + step;        *cy = min_y; return; }
    step -= w;
    if (step < h)            { *cx = max_x;               *cy = min_y + step; return; }
    step -= h;
    if (step < w)            { *cx = max_x - step;        *cy = max_y; return; }
    step -= w;
    *cx = min_x;             *cy = max_y - step;
}

/** queue a crystal sphere at the player's tile, unless a pillar blocks LoS
    (C 6.25: pillar LoS blocks spheres; col_tiles_have_los exempts the source
    tile so a crystal parked on a blocked ring corner still fires). */
static void col_sol_fire_sphere(ColosseumState* s, int cx, int cy) {
    if (!col_tiles_have_los(s, cx, cy, s->player.x, s->player.y)) return;
    SolHereditState* sol = &s->sol;
    for (int i = 0; i < COLO_SOL_SPHERE_QUEUE_MAX; i++) {
        if (sol->spheres[i].active) continue;
        sol->spheres[i].active = 1;
        sol->spheres[i].tile_x = s->player.x;
        sol->spheres[i].tile_y = s->player.y;
        sol->spheres[i].damage = encounter_rand_int(&s->rng_state, COLO_SOL_SPHERE_MAX_HIT + 1);
        sol->spheres[i].ticks_remaining = COLO_SOL_SPHERE_DELAY;
        return;
    }
}

/** advance the crystal and resolve in-flight spheres (75 typeless on contact). */
static void col_sol_tick_crystal(ColosseumState* s) {
    SolHereditState* sol = &s->sol;
    if (sol->crystal_active) {
        sol->crystal_step++;
        if (sol->crystal_fire_timer > 0) sol->crystal_fire_timer--;
        if (sol->crystal_fire_timer == 0) {
            int cx, cy;
            col_sol_crystal_pos(s, &cx, &cy);
            col_sol_fire_sphere(s, cx, cy);
            sol->crystal_fire_timer = (sol->phase >= 5)
                ? COLO_SOL_CRYSTAL_ENRAGE_INTERVAL : COLO_SOL_CRYSTAL_INTERVAL;
        }
    }

    for (int i = 0; i < COLO_SOL_SPHERE_QUEUE_MAX; i++) {
        ColoSolSphere* sp = &sol->spheres[i];
        if (!sp->active) continue;
        sp->ticks_remaining--;
        if (sp->ticks_remaining > 0) continue;
        if (s->player.x == sp->tile_x && s->player.y == sp->tile_y) {
            encounter_damage_player(&s->player, sp->damage, &s->tick_scratch.damage_received);
            s->last_hit_by_type = COLO_SOL_HEREDIT;
            col_mod_on_player_damaged(s, sp->damage);
        }
        sp->active = 0;
    }
}

/* ----- enrage molten sand --------------------------------------------------- */

/** enrage (<10% HP): drop molten sand on a random arena tile every 3 ticks and
    burn the player for COLO_SOL_MOLTEN_MAX_HIT typeless while they stand on it. */
static void col_sol_tick_molten_sand(ColosseumState* s) {
    SolHereditState* sol = &s->sol;
    if (sol->phase < 5) return;

    if (sol->hazard_timer > 0) sol->hazard_timer--;
    if (sol->hazard_timer == 0) {
        int span = COLO_BOSS_ARENA_SIZE;
        int tx = sol->boss_arena_min_x + encounter_rand_int(&s->rng_state, span);
        int ty = sol->boss_arena_min_y + encounter_rand_int(&s->rng_state, span);
        int slot = sol->hazard_tile_count < COLO_SOL_HAZARD_TILES_MAX
            ? sol->hazard_tile_count++
            : encounter_rand_int(&s->rng_state, COLO_SOL_HAZARD_TILES_MAX);
        sol->hazard_tile_x[slot] = tx;
        sol->hazard_tile_y[slot] = ty;
        sol->hazard_timer = COLO_SOL_MOLTEN_INTERVAL;
    }

    for (int i = 0; i < sol->hazard_tile_count; i++) {
        if (s->player.x == sol->hazard_tile_x[i] && s->player.y == sol->hazard_tile_y[i]) {
            encounter_damage_player(&s->player, COLO_SOL_MOLTEN_MAX_HIT, &s->tick_scratch.damage_received);
            s->last_hit_by_type = COLO_SOL_HEREDIT;
            col_mod_on_player_damaged(s, COLO_SOL_MOLTEN_MAX_HIT);
            break;  /* one burn per tick regardless of overlapping pools */
        }
    }
}

/* ----- phase transitions + decision ---------------------------------------- */

/** AoE / triple-parry / grapple insertion weights once a response is unlocked. */
#define COLO_SOL_TRIPLE_PARRY_PCT 35  /* of 100, when <90% HP */
#define COLO_SOL_GRAPPLE_PCT      25  /* of 100, when <75% HP */

/** run a phase transition: 6 light-beam telegraphs and (phase >= 2) the crystal. */
static void col_sol_enter_phase(ColosseumState* s, int new_phase) {
    SolHereditState* sol = &s->sol;
    sol->phase = new_phase;
    sol->beam_count = COLO_SOL_BEAM_COUNT;
    sol->beam_ticks_remaining = COLO_SOL_BEAM_TICKS;
    if (new_phase >= 2 && !sol->crystal_active) {
        sol->crystal_active = 1;
        sol->crystal_fire_timer = COLO_SOL_CRYSTAL_INTERVAL;
    }
    if (new_phase > s->log.boss_phase_reached) s->log.boss_phase_reached = new_phase;
}

/** is Sol busy resolving a prior special this tick (no new decision)? */
static int col_sol_busy(const SolHereditState* sol) {
    return sol->aoe_attack != COLO_SOL_AOE_NONE ||
           sol->parry_sequence_active || sol->grapple_active;
}

/** pick and launch Sol's next offensive action. */
static void col_sol_decide(ColosseumState* s, int idx) {
    int hp = s->npcs[idx].hp;
    int tp_unlocked = hp < (COLO_SOL_HP_MAX * 90) / 100;
    int grapple_unlocked = hp < (COLO_SOL_HP_MAX * 75) / 100;

    int roll = encounter_rand_int(&s->rng_state, 100);
    if (tp_unlocked && roll < COLO_SOL_TRIPLE_PARRY_PCT) {
        col_sol_start_triple_parry(s, idx);
        return;
    }
    if (grapple_unlocked && roll < COLO_SOL_TRIPLE_PARRY_PCT + COLO_SOL_GRAPPLE_PCT) {
        col_sol_start_grapple(s);
        return;
    }
    s->sol.aoe_attack = col_sol_next_aoe(s);
    s->sol.aoe_resolve_timer = COLO_SOL_AOE_TELEGRAPH;
}

/**
 * Sol Heredit boss tick. No-op until wave 12 is live and the boss machine has
 * been started by col_sol_begin_boss_arena.
 */
static void col_sol_boss_tick(ColosseumState* s, ColosseumContext* ctx) {
    (void)ctx;
    SolHereditState* sol = &s->sol;
    if (s->wave != COLO_WAVE_BOSS || !sol->started) return;

    int idx = col_sol_find_idx(s);
    sol->boss_idx = idx;
    if (idx < 0) return;  /* Sol dead/absent: wave-complete handles the win */

    /* phase transitions on downward HP-fraction crossings. */
    int target_phase = col_sol_phase_for_hp(s->npcs[idx].hp);
    while (sol->phase < target_phase) col_sol_enter_phase(s, sol->phase + 1);

    if (sol->beam_ticks_remaining > 0) {
        sol->beam_ticks_remaining--;
        if (sol->beam_ticks_remaining == 0) sol->beam_count = 0;
    }
    if (sol->guaranteed_max_ticks > 0) {
        sol->guaranteed_max_ticks--;
        if (sol->guaranteed_max_ticks == 0) sol->next_attack_guaranteed_max = 0;
    }

    /* resolve any telegraphed AoE whose dodge window has elapsed. */
    if (sol->aoe_attack != COLO_SOL_AOE_NONE) {
        if (sol->aoe_resolve_timer > 0) sol->aoe_resolve_timer--;
        if (sol->aoe_resolve_timer == 0) col_sol_resolve_aoe(s);
    }

    col_sol_tick_triple_parry(s);
    col_sol_tick_grapple(s);
    col_sol_tick_crystal(s);
    col_sol_tick_molten_sand(s);

    /* main cadence: pick a new special only when not mid-resolution. */
    if (sol->attack_timer > 0) sol->attack_timer--;
    if (sol->attack_timer == 0 && !col_sol_busy(sol)) {
        col_sol_decide(s, idx);
        sol->attack_timer = COLO_SOL_ATTACK_INTERVAL;
    }
}
