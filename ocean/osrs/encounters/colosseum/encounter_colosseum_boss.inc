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
 * survives by dodging hazard tiles (damage 2 ticks after the cast = one
 * informed reactive move, A15), flicking Protect
 * from Melee per parry hit (early prayer is punished, B4), and clicking the
 * called slot on the grapple. Damage routes through col_damage_player_from +
 * col_mod_on_player_damaged so the shared HP/splat/Doom bookkeeping holds.
 */

/* ----- phase + arena helpers ------------------------------------------------ */

/** boss arena is clamping movement iff wave 12 is live and the machine started. */
static int col_sol_clamp_active(const ColosseumState* s) {
    return s->wave == COLO_WAVE_BOSS && s->sol.started;
}

/* The boss_arena bounds are the shield-wall RING: the gladiators stand on it and
   the crystals roam it. The player fights one tile INSIDE that ring, so the
   walkable interior -- which confines player + NPC movement + hazard placement --
   is the bounds shrunk by one tile on every side. */
static int col_in_boss_arena(const ColosseumState* s, int x, int y) {
    return x > s->sol.boss_arena_min_x && x < s->sol.boss_arena_max_x &&
           y > s->sol.boss_arena_min_y && y < s->sol.boss_arena_max_y;
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
        if (col_npc_is_live_target(npc) && npc->type == COLO_SOL_HEREDIT)
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
    s->sol.laser_cooldown = COLO_SOL_CRYSTAL_COOLDOWN_MIN;
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
static void col_sol_fire_lasers(ColosseumState* s);

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

    /* colosim: every non-transition attack commit triggers the lasers once
       the shared cooldown has run out (transitions instead force a spear via
       force_spear, which commits here too — colosim fires on those as well
       since only Attacks.PHASE_TRANSITION itself is excluded). */
    if (sol->crystal_count > 0 && sol->laser_cooldown < 0)
        col_sol_fire_lasers(s);
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

/** 8-way attack direction from a position vs the recorded 5x5 hazard anchor
    (colosim getAttackDirection): clamp the position into the footprint box,
    take the sign of the remaining offset per axis. A position past a corner
    resolves DIAGONAL. Under the boss (offset 0,0) colosim falls through to
    SW. Colosim calls this inside the DelayedAction(2) stamp — the bite tick,
    NOT the cast — so running away laterally after the cast cannot force a
    cardinal miss (user-verified vs real play). */
static void col_sol_aoe_direction(int px, int py, int anchor_x, int anchor_y,
                                  int* dx, int* dy) {
    int closest_x = px, closest_y = py;
    if (closest_x < anchor_x) closest_x = anchor_x;
    if (closest_x > anchor_x + 4) closest_x = anchor_x + 4;
    if (closest_y < anchor_y) closest_y = anchor_y;
    if (closest_y > anchor_y + 4) closest_y = anchor_y + 4;
    int ox = px - closest_x;
    int oy = py - closest_y;
    *dx = ox > 0 ? 1 : ox < 0 ? -1 : 0;
    *dy = oy > 0 ? 1 : oy < 0 ? -1 : 0;
    if (*dx == 0 && *dy == 0) { *dx = -1; *dy = -1; }
}

/** direction a live spear resolves under for a query at (x, y): the locked
    bite-tick direction once stamped, else the direction (x, y) itself would
    induce. Per-tile induction makes telegraph-phase hazard queries the exact
    rule-based threat field: "if it lands while you stand here, you are hit". */
static void col_sol_aoe_effective_dir(const SolHereditState* sol, int x, int y,
                                      int* dx, int* dy) {
    if (sol->aoe_dir_x != 0 || sol->aoe_dir_y != 0) {
        *dx = sol->aoe_dir_x;
        *dy = sol->aoe_dir_y;
        return;
    }
    col_sol_aoe_direction(x, y, sol->aoe_x, sol->aoe_y, dx, dy);
}

/**
 * A3: is (x, y) a hazard tile of the recorded cast? Shapes ported tile-exact
 * from colosim doFirstSpear/doSecondSpear/doShields (frame map: ours =
 * (x_colosim, -y_colosim), validated per cardinal against the previous
 * known-good shapes). Cardinals in (forward, lateral) coordinates along the
 * cast direction, boss centre at forward 0, footprint -2..+2, flush melee
 * row forward 3; lines run forward 4..4+LINE_LEN (colosim fillLine covers
 * length+1 tiles):
 *   Spear1: footprint + full-width front row + TWO lines at lateral +-1.
 *   Spear2: 7x7 slam (footprint + 1 ring) + THREE lines at lateral -2/0/+2.
 * Diagonals in (u, v) = per-axis distance out from the footprint corner K
 * facing the player (K = centre + 2*(dx, dy)), both axes oriented along the
 * cast so u, v grow outward:
 *   Spear1: footprint + two lines flanking the corner diagonal, anchors
 *           K+(0,dy) and K+(dx,0) — the exact corner diagonal stays safe.
 *   Spear2: 7x7 slam + centre line ON the diagonal from K+2*(dx,dy) + two
 *           flanks from K+(-dx,2dy) and K+(2dx,-dy).
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

    int dx, dy;
    col_sol_aoe_effective_dir(sol, x, y, &dx, &dy);
    if (dx != 0 && dy != 0) {
        int u = (x - (cx + 2 * dx)) * dx;
        int v = (y - (cy + 2 * dy)) * dy;
        if (sol->aoe_attack == COLO_SOL_AOE_SPEAR1) {
            if (abs(x - cx) <= 2 && abs(y - cy) <= 2) return 1;
            if (u == v - 1 && v >= 1 && v <= 1 + COLO_SOL_SPEAR_LINE_LEN) return 1;
            if (v == u - 1 && u >= 1 && u <= 1 + COLO_SOL_SPEAR_LINE_LEN) return 1;
            return 0;
        }
        if (abs(x - cx) <= 3 && abs(y - cy) <= 3) return 1;
        if (u == v && u >= 2 && u <= 2 + COLO_SOL_SPEAR_LINE_LEN) return 1;
        if (v == u + 3 && u >= -1 && u <= -1 + COLO_SOL_SPEAR_LINE_LEN) return 1;
        if (u == v + 3 && v >= -1 && v <= -1 + COLO_SOL_SPEAR_LINE_LEN) return 1;
        return 0;
    }

    int forward = dx != 0 ? (x - cx) * dx : (y - cy) * dy;
    int lateral = dx != 0 ? (y - cy) : (x - cx);
    if (sol->aoe_attack == COLO_SOL_AOE_SPEAR1) {
        if (lateral >= -2 && lateral <= 2 &&
            forward >= -2 && forward <= 3) return 1;
        if (forward >= 4 && forward <= 4 + COLO_SOL_SPEAR_LINE_LEN)
            return lateral == -1 || lateral == 1;
        return 0;
    }
    if (lateral >= -3 && lateral <= 3 &&
        forward >= -3 && forward <= 3) return 1;
    if (forward >= 4 && forward <= 4 + COLO_SOL_SPEAR_LINE_LEN)
        return lateral == -2 || lateral == 0 || lateral == 2;
    return 0;
}

/** launch a spear/shield cast: record the hazard anchor (boss SW + variant)
    at age 0 = telegraph. The spear direction stays UNRESOLVED (0,0) until the
    bite tick — colosim samples it inside the DelayedAction(2) stamp, so the
    telegraph is Sol's animation, not a committed direction. */
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
    sol->aoe_dir_x = 0;
    sol->aoe_dir_y = 0;
}

/** age the live AoE: at age 2 (A15: cast tick + telegraph tick + damage tick,
    so the post-telegraph move phase runs before the check) a spear locks its
    direction from the player's post-move position — the game decides cardinal
    vs diagonal on the bite tick, so the safe tiles are a rule the player must
    know relative to their OWN landing tile, not a telegraph to outrun — then
    a player standing on a hazard tile eats the per-tile 20-44 roll (D13);
    residue fades after 3 ticks. */
static void col_sol_tick_aoe(ColosseumState* s) {
    SolHereditState* sol = &s->sol;
    if (sol->aoe_attack == COLO_SOL_AOE_NONE) return;
    sol->aoe_age++;
    if (sol->aoe_age == COLO_SOL_AOE_DAMAGE_AGE &&
        (sol->aoe_attack == COLO_SOL_AOE_SPEAR1 ||
         sol->aoe_attack == COLO_SOL_AOE_SPEAR2))
        col_sol_aoe_direction(s->player.x, s->player.y, sol->aoe_x, sol->aoe_y,
                              &sol->aoe_dir_x, &sol->aoe_dir_y);
    if (sol->aoe_age == COLO_SOL_AOE_DAMAGE_AGE &&
        col_sol_aoe_tile_is_hazard(sol, s->player.x, s->player.y)) {
        int dmg = COLO_SOL_SLAM_MIN_HIT
            + encounter_rand_int(&s->rng_state, COLO_SOL_SLAM_RAND);
        col_damage_player_from(s, dmg, COLO_SOL_HEREDIT, COLO_DMG_UNPRAYABLE);
        s->last_hit_by_type = COLO_SOL_HEREDIT;
        col_mod_on_player_damaged(s, dmg);
    }
    if (sol->aoe_age >= COLO_SOL_AOE_LIFETIME) sol->aoe_attack = COLO_SOL_AOE_NONE;
}

/* ----- laser crystals (A9+A10+D15+D16, colosim LaserOrb) ----------------------- */

/** patrol segment for an edge: the line one tile inside the boundary, with
    endpoints inset 2 from the corners on the perpendicular axis (colosim
    EDGE_BOUNDARIES; the insets clear the corner pillar footprints). N/S
    crystals patrol along x at a fixed y, E/W along y at a fixed x. */
static void col_sol_crystal_patrol(
    const SolHereditState* sol, int edge, int* fixed, int* lo, int* hi
) {
    switch (edge) {
        case COLO_SOL_EDGE_NORTH:
            *fixed = sol->boss_arena_max_y - 1;
            *lo = sol->boss_arena_min_x + 2;
            *hi = sol->boss_arena_max_x - 2;
            return;
        case COLO_SOL_EDGE_SOUTH:
            *fixed = sol->boss_arena_min_y + 1;
            *lo = sol->boss_arena_min_x + 2;
            *hi = sol->boss_arena_max_x - 2;
            return;
        case COLO_SOL_EDGE_EAST:
            *fixed = sol->boss_arena_max_x - 1;
            *lo = sol->boss_arena_min_y + 2;
            *hi = sol->boss_arena_max_y - 2;
            return;
        case COLO_SOL_EDGE_WEST:
            *fixed = sol->boss_arena_min_x + 1;
            *lo = sol->boss_arena_min_y + 2;
            *hi = sol->boss_arena_max_y - 2;
            return;
        default:
            fprintf(stderr, "invalid crystal edge %d\n", edge);
            abort();
    }
}

/** is the player on the crystal's firing line (colosim isInLineWithPlayer)?
    N/S crystals shoot down their column, E/W down their row. */
static int col_sol_crystal_in_line(const ColosseumState* s, const ColoSolCrystal* c) {
    if (c->edge == COLO_SOL_EDGE_NORTH || c->edge == COLO_SOL_EDGE_SOUTH)
        return s->player.x == c->x;
    return s->player.y == c->y;
}

/** D16: spawn the next crystal (one per transition, phases 1-4) on its own
    edge in colosim's order N/E/S/W, at a uniform point on the patrol segment,
    heading toward a random endpoint. */
static void col_sol_spawn_crystal(ColosseumState* s) {
    SolHereditState* sol = &s->sol;
    if (sol->crystal_count >= COLO_SOL_MAX_CRYSTALS) return;
    ColoSolCrystal* c = &sol->crystals[sol->crystal_count];
    c->active = 1;
    c->edge = sol->crystal_count;
    int fixed, lo, hi;
    col_sol_crystal_patrol(sol, c->edge, &fixed, &lo, &hi);
    int along = lo + encounter_rand_int(&s->rng_state, hi - lo);
    if (c->edge == COLO_SOL_EDGE_NORTH || c->edge == COLO_SOL_EDGE_SOUTH) {
        c->x = along;
        c->y = fixed;
    } else {
        c->x = fixed;
        c->y = along;
    }
    c->dir = encounter_rand_int(&s->rng_state, 2) ? 1 : -1;
    if (along == lo) c->dir = 1;
    if (along == hi) c->dir = -1;
    c->move_timer = COLO_SOL_CRYSTAL_MOVE_TICKS_SPAWN;
    c->firing_freeze = 0;
    sol->crystal_count++;
}

/** fire every crystal simultaneously (colosim fireOrbs), called by the attack
    engine on a non-transition attack commit once the shared cooldown ran out. */
static void col_sol_fire_lasers(ColosseumState* s) {
    SolHereditState* sol = &s->sol;
    for (int i = 0; i < sol->crystal_count; i++)
        if (sol->crystals[i].active)
            sol->crystals[i].firing_freeze = COLO_SOL_LASER_FREEZE;
    sol->laser_cooldown = sol->phase >= 5
        ? COLO_SOL_CRYSTAL_COOLDOWN_ENRAGE
        : COLO_SOL_CRYSTAL_COOLDOWN_MIN
            + encounter_rand_int(&s->rng_state, COLO_SOL_CRYSTAL_COOLDOWN_RAND);
}

/** per-crystal tick, mirroring colosim LaserOrb.tick order exactly: patrol
    step (skipped while firing), then the damage check at firing_freeze == 3,
    then the timer decrements. The beam crosses the whole arena — pillars do
    not block it (colosim LineOfSightMask.NONE). */
static void col_sol_tick_crystals(ColosseumState* s) {
    SolHereditState* sol = &s->sol;
    for (int i = 0; i < sol->crystal_count; i++) {
        ColoSolCrystal* c = &sol->crystals[i];
        if (!c->active) continue;

        if (c->firing_freeze <= 0 && c->move_timer <= 0) {
            int fixed, lo, hi;
            col_sol_crystal_patrol(sol, c->edge, &fixed, &lo, &hi);
            int* along = (c->edge == COLO_SOL_EDGE_NORTH ||
                          c->edge == COLO_SOL_EDGE_SOUTH) ? &c->x : &c->y;
            *along += c->dir;
            if (*along <= lo || *along >= hi) c->dir = -c->dir;
            c->move_timer = COLO_SOL_CRYSTAL_MOVE_TICKS;
        }

        if (c->firing_freeze == COLO_SOL_LASER_DAMAGE_FREEZE &&
                col_sol_crystal_in_line(s, c)) {
            int dmg = COLO_SOL_LASER_MIN_HIT
                + encounter_rand_int(&s->rng_state, COLO_SOL_LASER_RAND);
            col_damage_player_from(s, dmg, COLO_SOL_HEREDIT, COLO_DMG_UNPRAYABLE);
            s->last_hit_by_type = COLO_SOL_HEREDIT;
            col_mod_on_player_damaged(s, dmg);
        }

        c->move_timer--;
        c->firing_freeze--;
    }
}

/* ----- beams + molten sand (A11+D25) ------------------------------------------ */

/** drop a permanent molten pool; re-covering an existing pool has no
    additional effect (W-STRAT), and the 32-slot cap recycles a random slot
    on overflow (D35). */
static void col_sol_add_pool(ColosseumState* s, int x, int y) {
    if (col_static_blocked(x, y)) return;
    SolHereditState* sol = &s->sol;
    for (int i = 0; i < sol->hazard_tile_count; i++)
        if (sol->hazard_tile_x[i] == x && sol->hazard_tile_y[i] == y) return;
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

/** telegraph one sand pillar: the white spot appears now, the pillar strikes
    2 ticks later (the counter decrements on the drop tick). Silently skips
    when all slots are in flight (BEAM_MAX covers every legit burst). */
static void col_sol_drop_beam_at(ColosseumState* s, int x, int y) {
    SolHereditState* sol = &s->sol;
    for (int b = 0; b < COLO_SOL_BEAM_MAX; b++) {
        if (sol->beams[b].active) continue;
        sol->beams[b] = (ColoSolBeam){
            .active = 1, .x = x, .y = y,
            .ticks_to_pool = COLO_SOL_BEAM_TO_POOL_TICKS };
        return;
    }
}

/** A11+E6: 6 light beams in the 9x9 around the player, with the first on the
    player's current tile; each pillar strikes 2 ticks after its telegraph
    appears, leaving a permanent molten pool. */
static void col_sol_drop_beams(ColosseumState* s) {
    ColSolMoltenCandidate tiles[COLO_SOL_BEAM_COUNT];
    int ntiles = col_sol_sample_molten_candidates(
        s, COLO_SOL_BEAM_SPREAD, COLO_SOL_BEAM_COUNT, 1, tiles);
    for (int n = 0; n < ntiles; n++)
        col_sol_drop_beam_at(s, tiles[n].x, tiles[n].y);
}

/** tick beams into pools, run the enrage spam (1 sand per 3 ticks within +-4
    of the player, D25 — enrage changes only the FREQUENCY, every sand still
    telegraphs 2 ticks like transition sand), and burn the player standing on
    any pool for 5-9 typeless — one burn per tick across overlapping pools
    (D35). The spam telegraphs before the aging loop so its pillar strikes on
    the same same-tick-decrement schedule as transition beams. */
static void col_sol_tick_molten(ColosseumState* s) {
    SolHereditState* sol = &s->sol;
    if (sol->phase >= 5) {
        if (sol->enrage_pool_timer > 0) sol->enrage_pool_timer--;
        if (sol->enrage_pool_timer == 0) {
            ColSolMoltenCandidate tile[1];
            int ntiles = col_sol_sample_molten_candidates(
                s, COLO_SOL_ENRAGE_POOL_SPREAD, 1, 0, tile);
            if (ntiles > 0) col_sol_drop_beam_at(s, tile[0].x, tile[0].y);
            sol->enrage_pool_timer = COLO_SOL_MOLTEN_INTERVAL;
        }
    }

    for (int b = 0; b < COLO_SOL_BEAM_MAX; b++) {
        if (!sol->beams[b].active) continue;
        sol->beams[b].ticks_to_pool--;
        if (sol->beams[b].ticks_to_pool > 0) continue;
        col_sol_add_pool(s, sol->beams[b].x, sol->beams[b].y);
        sol->beams[b].active = 0;
    }

    for (int i = 0; i < sol->hazard_tile_count; i++) {
        if (s->player.x != sol->hazard_tile_x[i] || s->player.y != sol->hazard_tile_y[i])
            continue;
        int dmg = COLO_MOLTEN_SAND_MIN_HIT
            + encounter_rand_int(&s->rng_state, COLO_MOLTEN_SAND_RAND);
        col_damage_player_from(s, dmg, COLO_SOL_HEREDIT, COLO_DMG_UNPRAYABLE);
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
        s->tick_scratch.styles_faced_mask |= (1 << ATTACK_STYLE_MELEE);
        s->tick_scratch.avoidable_threat_by_style[ATTACK_STYLE_MELEE] +=
            (float)sol->parry_damage[h];
        if (blocked) {
            s->tick_scratch.prayer_correct++;
        } else {
            s->tick_scratch.offpray_damage_this_tick += (float)sol->parry_damage[h];
            col_damage_player_from(s, sol->parry_damage[h], COLO_SOL_HEREDIT, COLO_DMG_OFFPRAY);
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
    sol->grapple_outcome = perfect
        ? COLO_GRAPPLE_OUTCOME_PERFECT : COLO_GRAPPLE_OUTCOME_DEFENDED;
    sol->grapple_outcome_tick = s->tick;
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
        col_damage_player_from(s, dmg, COLO_SOL_HEREDIT, COLO_DMG_UNPRAYABLE);
        s->last_hit_by_type = COLO_SOL_HEREDIT;
        col_mod_on_player_damaged(s, dmg);
        sol->grapple_active = 0;
        sol->grapple_resolved = 1;
        sol->grapple_outcome = COLO_GRAPPLE_OUTCOME_FAILED;
        sol->grapple_outcome_tick = s->tick;
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
    if (new_phase >= 5) sol->laser_cooldown = COLO_SOL_CRYSTAL_COOLDOWN_ENRAGE;
    if (new_phase >= 5) {
        /* the 5 enrage opening sands telegraph like every other sand — only
           the spawn frequency is special at enrage. */
        ColSolMoltenCandidate tiles[COLO_SOL_ENRAGE_OPEN_POOLS];
        int ntiles = col_sol_sample_molten_candidates(
            s, COLO_SOL_ENRAGE_POOL_SPREAD, COLO_SOL_ENRAGE_OPEN_POOLS, 0, tiles);
        for (int p = 0; p < ntiles; p++)
            col_sol_drop_beam_at(s, tiles[p].x, tiles[p].y);
        sol->enrage_pool_timer = COLO_SOL_MOLTEN_INTERVAL;
    }
    if (new_phase > s->log.boss_phase_reached) s->log.boss_phase_reached = new_phase;
}

/**
 * Sol Heredit boss tick. No-op until wave 12 is live and the boss machine has
 * been started by col_sol_begin_boss_arena. Per-tick order: record the prayer
 * history (B4), run phase transitions, age hazards + resolve player-response
 * layers, THEN the attack engine and movement. A cast on tick T telegraphs in
 * the obs at end of T, the player's informed move runs during T+1, and the
 * age-2 damage check at the top of T+2 sees that moved position (A15).
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
    sol->laser_cooldown--;

    col_sol_tick_aoe(s);
    col_sol_tick_crystals(s);
    col_sol_tick_molten(s);
    col_sol_tick_triple_parry(s);
    col_sol_tick_grapple(s);

    col_sol_tick_attack_engine(s, idx);
    col_sol_move(s, idx);
}
