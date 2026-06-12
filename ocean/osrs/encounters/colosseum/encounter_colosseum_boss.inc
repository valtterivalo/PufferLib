#line 76 "encounter_colosseum.h"
/**
 * @file encounter_colosseum_boss.inc
 * @brief Sol Heredit (wave 12) boss state machine.
 *
 * Reads top to bottom as the fight is built: the A1 attack engine (per-attack
 * next delays, {Spear x2, Shield x2} pool + gated specials, forced spears),
 * A2 chase movement behind the start-of-tick adjacency gate, the A3+A15 AoE
 * hazard tiles, the A9-A11 arena hazards (crystals, spheres, beams, molten
 * sand), and the player-response layers (A20+B4 triple parry, A12+B7 grapple).
 *
 * All Sol damage is typeless except the triple-parry melee hits: the player
 * survives by dodging hazard tiles (a 1-tick window, A15), flicking Protect
 * from Melee per parry hit (early prayer is punished, B4), and clicking the
 * called slot on the grapple. Damage routes through encounter_damage_player +
 * col_mod_on_player_damaged so the shared HP/splat/Doom bookkeeping holds.
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

/** is any overhead PROTECT prayer up (the B4 lookback + deactivation set)? */
static int col_player_overhead_protect_on(const ColosseumState* s) {
    return s->player.prayer == PRAYER_PROTECT_MELEE ||
           s->player.prayer == PRAYER_PROTECT_RANGED ||
           s->player.prayer == PRAYER_PROTECT_MAGIC;
}

/**
 * Initialise the boss machine for wave 12: set the (9,9)-(24,24) interior bounds
 * (A8+D29; the static-mask pillars clip the corners), drop the player onto the
 * south-centre start tile (D27), and zero the boss sub-state so a fight reached
 * by wave progression starts identically to a direct start_wave=12.
 * A1: the fight always opens with a Spear.
 */
static void col_sol_begin_boss_arena(ColosseumState* s) {
    s->sol = (SolHereditState){0};
    s->sol.started = 1;
    s->sol.boss_idx = -1;
    s->sol.force_spear = 1;
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

/* ----- attack engine (A1+D28) ------------------------------------------------ */

/* mechanic launchers, defined in their sections below. */
static void col_sol_cast_aoe(ColosseumState* s, int idx, int kind, int variant);
static void col_sol_start_triple_parry(ColosseumState* s, int idx);
static void col_sol_start_grapple(ColosseumState* s);

/** D28: next-attack delay for the attack just initiated. Spear/shield speed up
    1t below 75% (phase >= 2); the triple is 11t only in the 50-75% band. */
static int col_sol_attack_delay(int kind, int phase) {
    switch (kind) {
        case COLO_SOL_ATTACK_SPEAR:
            return phase < 2 ? COLO_SOL_SPEAR_DELAY : COLO_SOL_SPEAR_DELAY_FAST;
        case COLO_SOL_ATTACK_SHIELD:
            return phase < 2 ? COLO_SOL_SHIELD_DELAY : COLO_SOL_SHIELD_DELAY_FAST;
        case COLO_SOL_ATTACK_TRIPLE:
            return phase == 2 ? COLO_SOL_TRIPLE_DELAY_MID : COLO_SOL_TRIPLE_DELAY;
        case COLO_SOL_ATTACK_GRAPPLE:
            return COLO_SOL_GRAPPLE_DELAY;
        default:
            assert(0 && "next delay queried for a non-attack");
            return COLO_SOL_SPEAR_DELAY;
    }
}

/** D17: animation freeze for the attack just initiated; the triple holds Sol
    through its full hit schedule (modeled — colosim pins only spear/shield/
    grapple/transition). */
static int col_sol_attack_freeze(int kind, int phase) {
    switch (kind) {
        case COLO_SOL_ATTACK_SPEAR:   return COLO_SOL_SPEAR_FREEZE;
        case COLO_SOL_ATTACK_SHIELD:  return COLO_SOL_SHIELD_FREEZE;
        case COLO_SOL_ATTACK_GRAPPLE: return COLO_SOL_GRAPPLE_FREEZE;
        case COLO_SOL_ATTACK_TRIPLE:
            return 2 * COLO_SOL_TP_HIT_GAP +
                   (phase >= 3 ? COLO_SOL_TP_HIT3_GAP_LOW : COLO_SOL_TP_HIT_GAP);
        default:
            assert(0 && "freeze queried for a non-attack");
            return COLO_SOL_SPEAR_FREEZE;
    }
}

/**
 * A1: draw Sol's next attack. A forced spear (fight opener, post-transition)
 * short-circuits the pool. Otherwise uniform over {Spear, Spear, Shield,
 * Shield} plus the phase-gated specials once 2 normals have passed since the
 * last special: the triple from phase 1 (15/25/35 flavor through phase 2,
 * 15/30/45 from phase 3) and the grapple from phase 2. Selection maintains the
 * special cooldown; col_sol_pick_variant maintains the variant alternation.
 */
static int col_sol_select_attack(ColosseumState* s) {
    SolHereditState* sol = &s->sol;
    if (sol->force_spear) {
        sol->force_spear = 0;
        if (sol->special_cooldown > 0) sol->special_cooldown--;
        return COLO_SOL_ATTACK_SPEAR;
    }

    int pool[6];
    int n = 0;
    pool[n++] = COLO_SOL_ATTACK_SPEAR;
    pool[n++] = COLO_SOL_ATTACK_SPEAR;
    pool[n++] = COLO_SOL_ATTACK_SHIELD;
    pool[n++] = COLO_SOL_ATTACK_SHIELD;
    if (sol->special_cooldown <= 0) {
        if (sol->phase >= 1) pool[n++] = COLO_SOL_ATTACK_TRIPLE;
        if (sol->phase >= 2) pool[n++] = COLO_SOL_ATTACK_GRAPPLE;
    }

    int kind = pool[encounter_rand_int(&s->rng_state, n)];
    if (kind == COLO_SOL_ATTACK_TRIPLE || kind == COLO_SOL_ATTACK_GRAPPLE)
        sol->special_cooldown = COLO_SOL_SPECIAL_COOLDOWN;
    else if (sol->special_cooldown > 0)
        sol->special_cooldown--;
    return kind;
}

/** A1 variant alternation: consecutive same-type casts flip 1 <-> 2; any type
    switch (specials included) resets to variant 1. */
static int col_sol_pick_variant(SolHereditState* sol, int kind) {
    int variant = (sol->last_attack_kind == kind && sol->last_variant == 1) ? 2 : 1;
    sol->last_attack_kind = kind;
    sol->last_variant = variant;
    return variant;
}

/** A2: Sol initiates only when his footprint is melee-adjacent to the player
    at the START of the tick (the NPC phase runs before player movement, so
    positions here are exactly start-of-tick). */
static int col_sol_adjacent_to_player(const ColosseumState* s, int idx) {
    const ColoNPC* boss = &s->npcs[idx];
    return encounter_dist_to_npc(s->player.x, s->player.y, boss->x, boss->y,
        col_npc_effective_size(boss)) == 1;
}

/** one attack-engine tick: count down the freeze + attack delay, then initiate
    when unfrozen, off cooldown, and adjacent at tick start (A2 — walking away
    on the cooldown tick delays him). Initiation launches the mechanic, freezes
    Sol (D17), and rolls the per-attack next delay (D28). */
static void col_sol_tick_attack_engine(ColosseumState* s, int idx) {
    SolHereditState* sol = &s->sol;
    if (sol->immobile_ticks > 0) sol->immobile_ticks--;
    if (sol->attack_delay > 0) sol->attack_delay--;
    if (sol->immobile_ticks > 0 || sol->attack_delay > 0) return;
    if (!col_sol_adjacent_to_player(s, idx)) return;

    int kind = col_sol_select_attack(s);
    int variant = col_sol_pick_variant(sol, kind);
    switch (kind) {
        case COLO_SOL_ATTACK_SPEAR:
        case COLO_SOL_ATTACK_SHIELD:
            col_sol_cast_aoe(s, idx, kind, variant);
            break;
        case COLO_SOL_ATTACK_TRIPLE:
            col_sol_start_triple_parry(s, idx);
            break;
        case COLO_SOL_ATTACK_GRAPPLE:
            col_sol_start_grapple(s);
            break;
        default:
            assert(0 && "selected a non-attack");
            break;
    }
    sol->immobile_ticks = col_sol_attack_freeze(kind, sol->phase);
    sol->attack_delay = col_sol_attack_delay(kind, sol->phase);
    s->npcs[idx].attacked_this_tick = 1;
    s->npcs[idx].attack_style_this_tick = ATTACK_STYLE_MELEE;
}

/* ----- movement (A2+D17) ----------------------------------------------------- */

/** Sol's chase: one footprint-aware BFS step toward centring his 5x5 on the
    player. Frozen while attacking or transitioning; holds once adjacent (the
    attack engine, which ran first this tick, owns the adjacency decision). */
static void col_sol_move(ColosseumState* s, int idx) {
    SolHereditState* sol = &s->sol;
    ColoNPC* boss = &s->npcs[idx];
    boss->target_x = s->player.x;
    boss->target_y = s->player.y;
    if (sol->immobile_ticks > 0) return;
    int size = col_npc_effective_size(boss);
    if (encounter_dist_to_npc(s->player.x, s->player.y, boss->x, boss->y, size) <= 1)
        return;
    col_stamp_npc_collision_footprint(s, boss->x, boss->y, size, 0);
    int moved = col_npc_path_step(s, &boss->x, &boss->y, size,
        s->player.x - size / 2, s->player.y - size / 2);
    col_stamp_npc_collision_footprint(s, boss->x, boss->y, size, 1);
    boss->moved_this_tick = moved ? 1 : 0;
}

/* ----- AoE hazard tiles (A3+A15+D13+D14) -------------------------------------- */

/** dominant cardinal from Sol's centre toward the player at cast time; the
    spear front face + lines aim along it (ties resolve to the x axis). */
static void col_sol_aoe_direction(const ColosseumState* s, const ColoNPC* boss,
                                  int* dx, int* dy) {
    int size = col_npc_effective_size(boss);
    int ox = s->player.x - (boss->x + size / 2);
    int oy = s->player.y - (boss->y + size / 2);
    if (abs(ox) >= abs(oy)) { *dx = ox >= 0 ? 1 : -1; *dy = 0; }
    else                    { *dx = 0; *dy = oy >= 0 ? 1 : -1; }
}

/**
 * A3: is (x, y) a hazard tile of the recorded cast? Shapes (W-STRAT verbatim),
 * in (forward, lateral) coordinates along the cast direction with the boss
 * centre at forward 0 (footprint spans forward/lateral -2..+2; the flush
 * melee row is forward 3):
 *   Spear1: 5x6 block (footprint + the front row) + TWO length-4 lines flush
 *           with the edge at lateral +-1 — dodge = 1 back at the centre or
 *           corner columns.
 *   Spear2: 5x5 block (footprint only) + THREE lines at lateral -2/0/+2 —
 *           dodge = 1 back diagonally off the centre column.
 *   Shield1: hazard everywhere EXCEPT the safe ring at Chebyshev 4 from the
 *           boss centre (the 9x9 perimeter); the rest of the arena burns.
 *   Shield2: inner block to Chebyshev 4 (9x9), safe ring at 5, hazard outside.
 */
static int col_sol_aoe_tile_is_hazard(const SolHereditState* sol, int x, int y) {
    int cx = sol->aoe_x + 2, cy = sol->aoe_y + 2;

    if (sol->aoe_attack == COLO_SOL_AOE_SHIELD1 ||
        sol->aoe_attack == COLO_SOL_AOE_SHIELD2) {
        int dx = abs(x - cx), dy = abs(y - cy);
        int cheb = dx > dy ? dx : dy;
        int ring = sol->aoe_attack == COLO_SOL_AOE_SHIELD1
            ? COLO_SOL_SHIELD1_RING : COLO_SOL_SHIELD2_RING;
        return cheb != ring;
    }
    if (sol->aoe_attack != COLO_SOL_AOE_SPEAR1 &&
        sol->aoe_attack != COLO_SOL_AOE_SPEAR2) return 0;

    int forward = sol->aoe_dir_x != 0 ? (x - cx) * sol->aoe_dir_x
                                      : (y - cy) * sol->aoe_dir_y;
    int lateral = sol->aoe_dir_x != 0 ? (y - cy) : (x - cx);
    if (lateral < -2 || lateral > 2 || forward < -2) return 0;

    int block_depth = sol->aoe_attack == COLO_SOL_AOE_SPEAR1 ? 3 : 2;
    if (forward <= block_depth) return 1;
    if (forward > 2 + COLO_SOL_SPEAR_LINE_LEN) return 0;
    if (sol->aoe_attack == COLO_SOL_AOE_SPEAR1)
        return lateral == -1 || lateral == 1;
    return lateral == -2 || lateral == 0 || lateral == 2;
}

/** launch a spear/shield cast: record the hazard frame (boss SW + direction +
    variant) at age 0 = telegraph. */
static void col_sol_cast_aoe(ColosseumState* s, int idx, int kind, int variant) {
    SolHereditState* sol = &s->sol;
    const ColoNPC* boss = &s->npcs[idx];
    if (kind == COLO_SOL_ATTACK_SPEAR)
        sol->aoe_attack = variant == 1 ? COLO_SOL_AOE_SPEAR1 : COLO_SOL_AOE_SPEAR2;
    else
        sol->aoe_attack = variant == 1 ? COLO_SOL_AOE_SHIELD1 : COLO_SOL_AOE_SHIELD2;
    sol->aoe_age = 0;
    sol->aoe_x = boss->x;
    sol->aoe_y = boss->y;
    col_sol_aoe_direction(s, boss, &sol->aoe_dir_x, &sol->aoe_dir_y);
}

/** age the live AoE: at age 1 (A15) a player standing on a hazard tile eats
    the per-tile 20-44 roll (D13); the residue fades after 3 ticks. */
static void col_sol_tick_aoe(ColosseumState* s) {
    SolHereditState* sol = &s->sol;
    if (sol->aoe_attack == COLO_SOL_AOE_NONE) return;
    sol->aoe_age++;
    if (sol->aoe_age == COLO_SOL_AOE_DAMAGE_AGE &&
        col_sol_aoe_tile_is_hazard(sol, s->player.x, s->player.y)) {
        int dmg = COLO_SOL_SLAM_MIN_HIT
            + encounter_rand_int(&s->rng_state, COLO_SOL_SLAM_RAND);
        encounter_damage_player(&s->player, dmg, &s->tick_scratch.damage_received);
        s->last_hit_by_type = COLO_SOL_HEREDIT;
        col_mod_on_player_damaged(s, dmg);
    }
    if (sol->aoe_age >= COLO_SOL_AOE_LIFETIME) sol->aoe_attack = COLO_SOL_AOE_NONE;
}

/* ----- crystals + spheres (A9+A10+D15+D16) ------------------------------------ */

/** ring tile for a perimeter step index, clockwise around the boss-arena edge. */
static void col_sol_crystal_pos(const ColosseumState* s, int step, int* cx, int* cy) {
    int min_x = s->sol.boss_arena_min_x, max_x = s->sol.boss_arena_max_x;
    int min_y = s->sol.boss_arena_min_y, max_y = s->sol.boss_arena_max_y;
    int w = max_x - min_x, h = max_y - min_y;
    int perim = 2 * (w + h);
    int p = perim > 0 ? step % perim : 0;
    if (p < w)            { *cx = min_x + p;     *cy = min_y; return; }
    p -= w;
    if (p < h)            { *cx = max_x;         *cy = min_y + p; return; }
    p -= h;
    if (p < w)            { *cx = max_x - p;     *cy = max_y; return; }
    p -= w;
    *cx = min_x;          *cy = max_y - p;
}

/** D16: spawn the next crystal (one per transition, phases 1-4). Spawn ring
    offsets spread the crystals evenly (modeled; no source pins spawn tiles). */
static void col_sol_spawn_crystal(ColosseumState* s) {
    SolHereditState* sol = &s->sol;
    if (sol->crystal_count >= COLO_SOL_MAX_CRYSTALS) return;
    int w = sol->boss_arena_max_x - sol->boss_arena_min_x;
    int h = sol->boss_arena_max_y - sol->boss_arena_min_y;
    ColoSolCrystal* c = &sol->crystals[sol->crystal_count];
    c->active = 1;
    c->perim_step = (sol->crystal_count * 2 * (w + h)) / COLO_SOL_MAX_CRYSTALS;
    c->move_timer = COLO_SOL_CRYSTAL_MOVE_TICKS;
    c->fire_cooldown = COLO_SOL_CRYSTAL_COOLDOWN_MIN
        + encounter_rand_int(&s->rng_state, COLO_SOL_CRYSTAL_COOLDOWN_RAND);
    c->telegraph_ticks = 0;
    sol->crystal_count++;
}

/** queue a sphere at the player's tile unless a pillar blocks LoS (C 6.25;
    col_tiles_have_los exempts the source tile, so a crystal parked on a
    blocked ring corner still fires). */
static void col_sol_fire_sphere(ColosseumState* s, int cx, int cy) {
    if (!col_tiles_have_los(s, cx, cy, s->player.x, s->player.y)) return;
    SolHereditState* sol = &s->sol;
    for (int i = 0; i < COLO_SOL_SPHERE_QUEUE_MAX; i++) {
        if (sol->spheres[i].active) continue;
        sol->spheres[i].active = 1;
        sol->spheres[i].tile_x = s->player.x;
        sol->spheres[i].tile_y = s->player.y;
        sol->spheres[i].damage = COLO_SOL_SPHERE_MIN_HIT
            + encounter_rand_int(&s->rng_state, COLO_SOL_SPHERE_RAND);
        sol->spheres[i].ticks_remaining = COLO_SOL_SPHERE_DELAY;
        return;
    }
    assert(0 && "sphere queue full (4 crystals cannot keep 8 in flight)");
}

/** A9: per-crystal state machine — walk the edge ring every 4 ticks; when the
    cooldown expires, stop + telegraph 4 ticks, launch the sphere, and roll the
    next cooldown (25-35t, 12t at enrage). Then land due spheres: 60-75
    typeless, unprayable, on the marked tile only (dodge by moving off). */
static void col_sol_tick_crystals(ColosseumState* s) {
    SolHereditState* sol = &s->sol;
    for (int i = 0; i < sol->crystal_count; i++) {
        ColoSolCrystal* c = &sol->crystals[i];
        if (!c->active) continue;
        if (c->telegraph_ticks > 0) {
            c->telegraph_ticks--;
            if (c->telegraph_ticks == 0) {
                int cx, cy;
                col_sol_crystal_pos(s, c->perim_step, &cx, &cy);
                col_sol_fire_sphere(s, cx, cy);
                c->fire_cooldown = sol->phase >= 5
                    ? COLO_SOL_CRYSTAL_COOLDOWN_ENRAGE
                    : COLO_SOL_CRYSTAL_COOLDOWN_MIN
                        + encounter_rand_int(&s->rng_state, COLO_SOL_CRYSTAL_COOLDOWN_RAND);
            }
            continue;
        }
        if (c->fire_cooldown > 0) c->fire_cooldown--;
        if (c->fire_cooldown == 0) {
            c->telegraph_ticks = COLO_SOL_CRYSTAL_TELEGRAPH;
            continue;
        }
        if (c->move_timer > 0) c->move_timer--;
        if (c->move_timer == 0) {
            c->perim_step++;
            c->move_timer = COLO_SOL_CRYSTAL_MOVE_TICKS;
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

/* ----- beams + molten sand (A11+D25) ------------------------------------------ */

/** drop a permanent molten pool; the 32-slot cap recycles a random slot on
    overflow (D35). */
static void col_sol_add_pool(ColosseumState* s, int x, int y) {
    if (col_static_blocked(x, y)) return;
    SolHereditState* sol = &s->sol;
    int slot = sol->hazard_tile_count < COLO_SOL_HAZARD_TILES_MAX
        ? sol->hazard_tile_count++
        : encounter_rand_int(&s->rng_state, COLO_SOL_HAZARD_TILES_MAX);
    sol->hazard_tile_x[slot] = x;
    sol->hazard_tile_y[slot] = y;
}

typedef struct {
    int x, y;
} ColSolMoltenCandidate;

/** Build every valid molten-sand candidate in the 9x9 around the player, then
    sample without replacement. Phase-transition beams force the player tile
    first (E6). Enrage opening/spam pools do not, because the cited enrage
    sources say random tiles within +-4 of the player (web-sol-mods h). */
static int col_sol_sample_molten_candidates(
    ColosseumState* s,
    int spread,
    int count,
    int force_player_tile,
    ColSolMoltenCandidate* out
) {
    ColSolMoltenCandidate pool[81];
    int pool_count = 0;
    int player_idx = -1;
    for (int x = s->player.x - spread; x <= s->player.x + spread; x++) {
        for (int y = s->player.y - spread; y <= s->player.y + spread; y++) {
            if (!col_in_boss_arena(s, x, y)) continue;
            if (col_static_blocked(x, y)) continue;
            if (pool_count >= (int)(sizeof(pool) / sizeof(pool[0]))) break;
            if (x == s->player.x && y == s->player.y) player_idx = pool_count;
            pool[pool_count++] = (ColSolMoltenCandidate){ .x = x, .y = y };
        }
    }

    int out_count = 0;
    if (force_player_tile && player_idx >= 0 && out_count < count) {
        out[out_count++] = pool[player_idx];
        pool[player_idx] = pool[--pool_count];
    }

    while (out_count < count && pool_count > 0) {
        int idx = encounter_rand_int(&s->rng_state, pool_count);
        out[out_count++] = pool[idx];
        pool[idx] = pool[--pool_count];
    }

    return out_count;
}

/** A11+E6: 6 light beams in the 9x9 around the player, with the first on the
    player's current tile; each becomes a permanent molten pool 2 ticks later. */
static void col_sol_drop_beams(ColosseumState* s) {
    SolHereditState* sol = &s->sol;
    ColSolMoltenCandidate tiles[COLO_SOL_BEAM_COUNT];
    int ntiles = col_sol_sample_molten_candidates(
        s, COLO_SOL_BEAM_SPREAD, COLO_SOL_BEAM_COUNT, 1, tiles);
    for (int n = 0; n < ntiles; n++) {
        for (int b = 0; b < COLO_SOL_BEAM_MAX; b++) {
            if (sol->beams[b].active) continue;
            sol->beams[b] = (ColoSolBeam){
                .active = 1, .x = tiles[n].x, .y = tiles[n].y,
                .ticks_to_pool = COLO_SOL_BEAM_TO_POOL_TICKS };
            break;
        }
    }
}

/** tick beams into pools, run the enrage spam (1 pool per 3 ticks within +-4
    of the player, D25), and burn the player standing on any pool for 5-9
    typeless — one burn per tick across overlapping pools (D35). */
static void col_sol_tick_molten(ColosseumState* s) {
    SolHereditState* sol = &s->sol;
    for (int b = 0; b < COLO_SOL_BEAM_MAX; b++) {
        if (!sol->beams[b].active) continue;
        sol->beams[b].ticks_to_pool--;
        if (sol->beams[b].ticks_to_pool > 0) continue;
        col_sol_add_pool(s, sol->beams[b].x, sol->beams[b].y);
        sol->beams[b].active = 0;
    }

    if (sol->phase >= 5) {
        if (sol->enrage_pool_timer > 0) sol->enrage_pool_timer--;
        if (sol->enrage_pool_timer == 0) {
            ColSolMoltenCandidate tile[1];
            int ntiles = col_sol_sample_molten_candidates(
                s, COLO_SOL_ENRAGE_POOL_SPREAD, 1, 0, tile);
            if (ntiles > 0) col_sol_add_pool(s, tile[0].x, tile[0].y);
            sol->enrage_pool_timer = COLO_SOL_MOLTEN_INTERVAL;
        }
    }

    for (int i = 0; i < sol->hazard_tile_count; i++) {
        if (s->player.x != sol->hazard_tile_x[i] || s->player.y != sol->hazard_tile_y[i])
            continue;
        int dmg = COLO_MOLTEN_SAND_MIN_HIT
            + encounter_rand_int(&s->rng_state, COLO_MOLTEN_SAND_RAND);
        encounter_damage_player(&s->player, dmg, &s->tick_scratch.damage_received);
        s->last_hit_by_type = COLO_SOL_HEREDIT;
        col_mod_on_player_damaged(s, dmg);
        break;
    }
}

/* ----- triple parry (A20+B4+D18) ----------------------------------------------- */

/** B4+D18: per-hit lookback windows (ticks before the hit) during which an
    overhead protect prayer must have been OFF, else that hit is unblockable
    (colosim per-hit history pops: 3 for hit 1, 2 for hits 2-3). */
static const int COLO_SOL_PARRY_LOOKBACK[COLO_SOL_PARRY_HITS] = { 3, 2, 2 };

/** was any overhead protect prayer on during the `lookback` ticks before this
    one (exclusive)? Reads the per-tick history the boss tick records. */
static int col_sol_overhead_on_in_window(const ColosseumState* s, int lookback) {
    for (int k = 1; k <= lookback; k++) {
        int t = s->tick - k;
        if (t < 0) continue;
        if (s->sol.overhead_history[t % COLO_SOL_PRAYER_HISTORY]) return 1;
    }
    return 0;
}

/** A20: start the phase-gated combo — 15/25/35 landing at +3/+6/+9 through
    phase 2 (HP 50-90%); 15/30/45 at +3/+6/+10 from phase 3 (below 50%),
    a replacement, never an alternation. */
static void col_sol_start_triple_parry(ColosseumState* s, int idx) {
    (void)idx;
    SolHereditState* sol = &s->sol;
    static const int SHORT_DMG[COLO_SOL_PARRY_HITS] = { 15, 25, 35 };
    static const int LONG_DMG[COLO_SOL_PARRY_HITS]  = { 15, 30, 45 };
    int low = sol->phase >= 3;
    const int* dmg = low ? LONG_DMG : SHORT_DMG;
    int gap3 = low ? COLO_SOL_TP_HIT3_GAP_LOW : COLO_SOL_TP_HIT_GAP;

    sol->parry_land_in[0] = COLO_SOL_TP_HIT_GAP;
    sol->parry_land_in[1] = 2 * COLO_SOL_TP_HIT_GAP;
    sol->parry_land_in[2] = 2 * COLO_SOL_TP_HIT_GAP + gap3;
    for (int h = 0; h < COLO_SOL_PARRY_HITS; h++) sol->parry_damage[h] = dmg[h];
    sol->parry_hits_left = COLO_SOL_PARRY_HITS;
}

/** resolve due parry hits. B4: an overhead on during the hit's lookback window
    makes it UNBLOCKABLE; otherwise Protect from Melee exactly at land blocks
    it (prayer-at-land convention, D18). Every hit — landed or blocked —
    deactivates all overhead protect prayers, forcing a re-flick per hit. */
static void col_sol_tick_triple_parry(ColosseumState* s) {
    SolHereditState* sol = &s->sol;
    if (sol->parry_hits_left <= 0) return;
    for (int h = 0; h < COLO_SOL_PARRY_HITS; h++) {
        if (sol->parry_land_in[h] <= 0) continue;
        sol->parry_land_in[h]--;
        if (sol->parry_land_in[h] > 0) continue;
        sol->parry_hits_left--;

        int early = col_sol_overhead_on_in_window(s, COLO_SOL_PARRY_LOOKBACK[h]);
        int blocked = !early && s->player.prayer == PRAYER_PROTECT_MELEE;
        if (blocked) {
            s->tick_scratch.prayer_correct++;
        } else {
            encounter_damage_player(&s->player, sol->parry_damage[h],
                &s->tick_scratch.damage_received);
            s->last_hit_by_type = COLO_SOL_HEREDIT;
            col_mod_on_player_damaged(s, sol->parry_damage[h]);
        }
        col_log_prayer_event(s, COLO_SOL_HEREDIT, blocked, blocked ? 0 : sol->parry_damage[h]);
        s->player.prayer = PRAYER_NONE;
    }
}

/* ----- grapple (A12+A28+B7) ----------------------------------------------------- */

/** A12: drop the shield and call one of EXACTLY 5 slots, uniform; the player
    has 4 ticks to click that slot via the GRAPPLE_PARRY head. */
static void col_sol_start_grapple(ColosseumState* s) {
    SolHereditState* sol = &s->sol;
    sol->grapple_active = 1;
    sol->grapple_resolved = 0;
    sol->grapple_body_slot = encounter_rand_int(&s->rng_state, COLO_NUM_GRAPPLE_SLOTS);
    sol->grapple_timer = COLO_SOL_GRAPPLE_WINDOW;
}

/**
 * Grapple response hook (player action phase): clicking the called slot
 * parries. Clicking on the last window tick (grapple_timer <= 1, D18) is a
 * PERFECT parry, arming a guaranteed-max window the player-attack path
 * consumes within COLO_SOL_PERFECT_MAX_TICKS (B7). Returns 1 if the click
 * resolved the grapple, else 0.
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

/** count down the window; an unanswered grapple lands 20 + rand(0..24)
    typeless, unblockable (A28+D13). */
static void col_sol_tick_grapple(ColosseumState* s) {
    SolHereditState* sol = &s->sol;
    if (!sol->grapple_active) return;
    if (sol->grapple_timer > 0) sol->grapple_timer--;
    if (sol->grapple_timer == 0 && !sol->grapple_resolved) {
        int dmg = COLO_SOL_SLAM_MIN_HIT
            + encounter_rand_int(&s->rng_state, COLO_SOL_SLAM_RAND);
        encounter_damage_player(&s->player, dmg, &s->tick_scratch.damage_received);
        s->last_hit_by_type = COLO_SOL_HEREDIT;
        col_mod_on_player_damaged(s, dmg);
        sol->grapple_active = 0;
        sol->grapple_resolved = 1;
    }
}

/* ----- phase transitions + main tick -------------------------------------------- */

/** enter ONE phase: 6 beams around the player (A11), the phase's crystal
    (phases 1-4, D16), and at enrage (phase 5) the 5 opening pools + the
    3-tick spam timer (D25). The caller applies the one-off transition freeze
    and the forced spear (A1+D28). */
static void col_sol_enter_phase(ColosseumState* s, int new_phase) {
    SolHereditState* sol = &s->sol;
    sol->phase = new_phase;
    col_sol_drop_beams(s);
    if (new_phase >= 1 && new_phase <= 4) col_sol_spawn_crystal(s);
    if (new_phase >= 5) {
        ColSolMoltenCandidate tiles[COLO_SOL_ENRAGE_OPEN_POOLS];
        int ntiles = col_sol_sample_molten_candidates(
            s, COLO_SOL_ENRAGE_POOL_SPREAD, COLO_SOL_ENRAGE_OPEN_POOLS, 0, tiles);
        for (int p = 0; p < ntiles; p++)
            col_sol_add_pool(s, tiles[p].x, tiles[p].y);
        sol->enrage_pool_timer = COLO_SOL_MOLTEN_INTERVAL;
    }
    if (new_phase > s->log.boss_phase_reached) s->log.boss_phase_reached = new_phase;
}

/**
 * Sol Heredit boss tick. No-op until wave 12 is live and the boss machine has
 * been started by col_sol_begin_boss_arena. Per-tick order: record the prayer
 * history (B4), run phase transitions, age hazards + resolve player-response
 * layers, THEN the attack engine and movement — a fresh cast keeps its full
 * 1-tick telegraph because the player's move phase runs after this, same tick.
 */
static void col_sol_boss_tick(ColosseumState* s, ColosseumContext* ctx) {
    (void)ctx;
    SolHereditState* sol = &s->sol;
    if (s->wave != COLO_WAVE_BOSS || !sol->started) return;

    int idx = col_sol_find_idx(s);
    sol->boss_idx = idx;
    if (idx < 0) return;  /* Sol dead/absent: wave-complete handles the win */

    /* B4: this tick's overhead state (final — prayer actions ran in pretick). */
    sol->overhead_history[s->tick % COLO_SOL_PRAYER_HISTORY] =
        (uint8_t)col_player_overhead_protect_on(s);

    /* phase transitions on downward HP crossings: per-phase hazards, then one
       D28 transition freeze and a forced spear (A1). */
    int target_phase = col_sol_phase_for_hp(s->npcs[idx].hp);
    if (target_phase > sol->phase) {
        while (sol->phase < target_phase) col_sol_enter_phase(s, sol->phase + 1);
        sol->immobile_ticks = COLO_SOL_TRANSITION_FREEZE;
        sol->force_spear = 1;
    }

    if (sol->guaranteed_max_ticks > 0) {
        sol->guaranteed_max_ticks--;
        if (sol->guaranteed_max_ticks == 0) sol->next_attack_guaranteed_max = 0;
    }

    col_sol_tick_aoe(s);
    col_sol_tick_crystals(s);
    col_sol_tick_molten(s);
    col_sol_tick_triple_parry(s);
    col_sol_tick_grapple(s);

    col_sol_tick_attack_engine(s, idx);
    col_sol_move(s, idx);
}
