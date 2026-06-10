#line 76 "encounter_colosseum.h"
/**
 * @file encounter_colosseum_movement.inc
 * @brief Arena predicates + per-NPC movement, mirroring inferno movement.
 */

/** player-walkability callback for shared movement helpers: the static mask
    (walls + pillars, A7+B1), the wave-12 boss clamp, and NPC bodies. */
static int col_player_walkable(void* ctx, int x, int y) {
    ColosseumState* s = (ColosseumState*)ctx;
    if (col_static_blocked(x, y)) return 0;
    if (col_sol_clamp_active(s) && !col_in_boss_arena(s, x, y)) return 0;
    int gx, gy;
    if (!col_grid_index(x, y, &gx, &gy)) return 0;
    return s->player_collision_flags[gx][gy] ? 0 : 1;
}

/**
 * B1: centre-to-centre tile line of sight over the static mask. Mirrors the
 * shared has_line_of_sight Q16 ray walk (osrs_collision.h) with an O(1) grid
 * lookup instead of a blocker list; walls and pillars block fully. Checks every
 * tile the ray enters including the destination; the SOURCE tile is exempt so a
 * perimeter emitter (the wave-12 crystal) can fire out of a blocked corner.
 */
static int col_tiles_have_los(const ColosseumState* s, int x0, int y0, int x1, int y1) {
    (void)s;
    int dx = x1 - x0, dy = y1 - y0;
    int adx = dx < 0 ? -dx : dx;
    int ady = dy < 0 ? -dy : dy;
    if (adx == 0 && ady == 0) return 1;
    if (col_static_blocked(x1, y1)) return 0;

    if (adx > ady) {
        int x = x0;
        int y_fp = y0 * LOS_FP_SCALE + LOS_FP_HALF;
        int slope = (dy * LOS_FP_SCALE) / adx;
        int x_inc = dx > 0 ? 1 : -1;
        if (dy < 0) y_fp -= 1;
        while (x != x1) {
            x += x_inc;
            int y = y_fp >> 16;
            if (col_static_blocked(x, y)) return 0;
            y_fp += slope;
            int new_y = y_fp >> 16;
            if (new_y != y && col_static_blocked(x, new_y)) return 0;
        }
    } else {
        int y = y0;
        int x_fp = x0 * LOS_FP_SCALE + LOS_FP_HALF;
        int slope = (dx * LOS_FP_SCALE) / ady;
        int y_inc = dy > 0 ? 1 : -1;
        if (dx < 0) x_fp -= 1;
        while (y != y1) {
            y += y_inc;
            int x = x_fp >> 16;
            if (col_static_blocked(x, y)) return 0;
            x_fp += slope;
            int new_x = x_fp >> 16;
            if (new_x != x && col_static_blocked(new_x, y)) return 0;
        }
    }
    return 1;
}

/** B1: ranged/magic attacks need LoS from the closest tile of the NPC's
    footprint to the player's tile; stepping behind a pillar safespots simple
    chasers (the attack gate in col_npc_attack_ctx consults this). */
static int col_npc_has_los_to_player(const ColosseumState* s, const ColoNPC* npc) {
    int size = npc->size > 0 ? npc->size : COLO_NPC_STATS[npc->type].size;
    int sx = s->player.x, sy = s->player.y;
    if (sx < npc->x) sx = npc->x;
    if (sx > npc->x + size - 1) sx = npc->x + size - 1;
    if (sy < npc->y) sy = npc->y;
    if (sy > npc->y + size - 1) sy = npc->y + size - 1;
    return col_tiles_have_los(s, sx, sy, s->player.x, s->player.y);
}

/** NPC-blocked callback (size-aware) for the shared chase step: the static mask
    (A7+B1), the wave-12 boss clamp, and other NPC footprints. */
static int col_npc_blocked(void* ctx, int x, int y, int size) {
    ColosseumState* s = (ColosseumState*)ctx;
    for (int dx = 0; dx < size; dx++) {
        for (int dy = 0; dy < size; dy++) {
            if (col_static_blocked(x + dx, y + dy)) return 1;
            if (col_sol_clamp_active(s) && !col_in_boss_arena(s, x + dx, y + dy)) return 1;
            int gx, gy;
            if (!col_grid_index(x + dx, y + dy, &gx, &gy)) return 1;
            if (s->npc_collision_flags[gx][gy]) return 1;
        }
    }
    return 0;
}

typedef struct {
    ColosseumState* s;
    int size;
} ColoNpcPathCtx;

/** BFS blocked predicate for NPC routefinding (B2(e)+A30): a size x size
    footprint at SW (x, y) vs the static mask, the boss clamp, other NPC bodies,
    and the player's tile. Caller lifts the moving NPC's own footprint stamp. */
static int col_npc_path_blocked(void* ctx, int x, int y) {
    ColoNpcPathCtx* pc = (ColoNpcPathCtx*)ctx;
    ColosseumState* s = pc->s;
    if (s->player.x >= x && s->player.x < x + pc->size &&
        s->player.y >= y && s->player.y < y + pc->size) return 1;
    return col_npc_blocked(s, x, y, pc->size);
}

/** one routefinding step (D3) of a size x size NPC toward SW-anchor dest over
    the shared arena BFS; an unreachable dest degrades to the nearest reachable
    tile (shared pathfinder fallback). Returns 1 if the NPC advanced. */
static int col_npc_path_step(ColosseumState* s, int* x, int* y, int size, int tx, int ty) {
    if (tx < COLO_ARENA_MIN_X) tx = COLO_ARENA_MIN_X;
    if (tx > COLO_ARENA_MAX_X) tx = COLO_ARENA_MAX_X;
    if (ty < COLO_ARENA_MIN_Y) ty = COLO_ARENA_MIN_Y;
    if (ty > COLO_ARENA_MAX_Y) ty = COLO_ARENA_MAX_Y;
    if (*x == tx && *y == ty) return 0;
    ColoNpcPathCtx pc = { s, size };
    PathResult pr = pathfind_step_arena(NULL, 0, *x, *y, tx, ty,
        col_npc_path_blocked, &pc,
        COLO_ARENA_MIN_X, COLO_ARENA_MIN_Y, COLO_ARENA_WIDTH, COLO_ARENA_HEIGHT);
    if (!pr.found || (pr.next_dx == 0 && pr.next_dy == 0)) return 0;
    *x += pr.next_dx;
    *y += pr.next_dy;
    return 1;
}

/** B2(c): pick a warbander's movement target — its formation tile when free,
    else the nearest free player-adjacent tile, cardinals before diagonals
    (diagonal contact cannot attack, A5+D33). The member's own footprint stamp
    is lifted, so the tile it already holds counts as free and it stands still
    rather than orbit the player. */
static void col_warband_pick_target(ColosseumState* s, ColoNPC* npc, int* tx, int* ty) {
    int px = s->player.x, py = s->player.y;
    int pref = colo_npc_warband(npc)->formation_dir;
    int fx = px + COLO_WARBAND_FORM_OFFSET[pref][0];
    int fy = py + COLO_WARBAND_FORM_OFFSET[pref][1];
    ColoNpcPathCtx pc = { s, 1 };
    *tx = fx;
    *ty = fy;
    if (!col_npc_path_blocked(&pc, fx, fy)) return;

    static const int ADJ[8][2] = {
        {0, 1}, {1, 0}, {-1, 0}, {0, -1},        /* cardinals */
        {1, 1}, {1, -1}, {-1, 1}, {-1, -1} };    /* diagonals */
    int best_key = 1 << 30;
    for (int a = 0; a < 8; a++) {
        int x = px + ADJ[a][0], y = py + ADJ[a][1];
        if (col_npc_path_blocked(&pc, x, y)) continue;
        int dx = npc->x > x ? npc->x - x : x - npc->x;
        int dy = npc->y > y ? npc->y - y : y - npc->y;
        int cheb = dx > dy ? dx : dy;
        int key = (a >= 4 ? 4096 : 0) + cheb * 16 + a;
        if (key < best_key) { best_key = key; *tx = x; *ty = y; }
    }
}

/** B2(c)+(e)+D3: warbander movement — up to COLO_WARBAND_TILES_PER_TICK BFS
    steps ("darts") toward the member's diamond tile, routing around pillars,
    walls, and other NPCs. Standing on the chosen tile is attack position: the
    member holds still so its cycle window can fire (a member that moved this
    tick is ineligible to attack, A5). */
static void col_warband_move(ColosseumState* s, int idx) {
    ColoNPC* npc = &s->npcs[idx];
    npc->target_x = s->player.x;
    npc->target_y = s->player.y;
    col_stamp_npc_collision_footprint(s, npc->x, npc->y, 1, 0);
    int tx, ty;
    col_warband_pick_target(s, npc, &tx, &ty);
    int steps = 0;
    for (int k = 0; k < COLO_WARBAND_TILES_PER_TICK; k++) {
        if (npc->x == tx && npc->y == ty) break;
        if (!col_npc_path_step(s, &npc->x, &npc->y, 1, tx, ty)) break;
        steps++;
    }
    col_stamp_npc_collision_footprint(s, npc->x, npc->y, 1, 1);
    npc->moved_this_tick = steps > 0 ? 1 : 0;
}

/** per-NPC movement. Warbanders route to their formation tiles (B2). The Red
    Flag minotaur is the routefinding variant (npc 12813): full BFS around
    obstacles, "impossible to safespot" (A30). Everything else keeps the simple
    greedy chase and is MEANT to be pillar-safespottable: melee NPCs path into
    melee contact; ranged NPCs (serpent/manticore/javelin/shockwave) hold once
    within attack_range AND in line of sight, else step closer. Multi-tile NPCs
    use the shared size-aware step. */
static void col_npc_move_ctx(ColosseumState* s, ColosseumContext* ctx, int idx) {
    (void)ctx;
    ColoNPC* npc = &s->npcs[idx];
    const ColoNpcStats* st = &COLO_NPC_STATS[npc->type];
    if (!st->can_move) return;
    if (npc->stun_timer > 0 || npc->frozen_ticks > 0) return;

    if (col_type_is_warbander(npc->type)) {
        col_warband_move(s, idx);
        return;
    }

    int size = col_npc_effective_size(npc);
    int tx = s->player.x, ty = s->player.y;
    npc->target_x = tx;
    npc->target_y = ty;

    int is_ranged = st->attack_range > 1;
    if (is_ranged) {
        int dist = encounter_dist_to_npc(s->player.x, s->player.y, npc->x, npc->y, size);
        if (dist >= 1 && dist <= st->attack_range && col_npc_has_los_to_player(s, npc)) {
            npc->moved_this_tick = 0;
            return;  /* in range with LoS: hold position */
        }
    }

    col_stamp_npc_collision_footprint(s, npc->x, npc->y, size, 0);
    int moved;
    if (npc->type == COLO_MINOTAUR && col_mod_active(s, COLO_MOD_RED_FLAG)) {
        /* A30: BFS toward the footprint-on-player anchor; the blocked dest
           degrades to the nearest reachable flush anchor. Stops in melee. */
        moved = 0;
        if (encounter_dist_to_npc(tx, ty, npc->x, npc->y, size) > 1)
            moved = col_npc_path_step(s, &npc->x, &npc->y, size, tx - 1, ty - 1);
    } else {
        moved = encounter_npc_step_toward(
            &npc->x, &npc->y, tx, ty, size, 1, is_ranged ? 0 : 1, col_npc_blocked, s);
    }
    col_stamp_npc_collision_footprint(s, npc->x, npc->y, size, 1);
    npc->moved_this_tick = moved ? 1 : 0;
}
