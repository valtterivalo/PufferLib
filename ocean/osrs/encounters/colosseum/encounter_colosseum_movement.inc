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

/** Red Flag minotaur sidestep: when the greedy step toward the player is blocked,
    try a one-tile step along whichever axis still has distance to close, in both
    directions, taking the first that is not blocked. Returns 1 if it moved. */
static int col_npc_red_flag_sidestep(
    ColosseumState* s, int* x, int* y, int tx, int ty, int size
) {
    int dx = (tx > *x) - (tx < *x);
    int dy = (ty > *y) - (ty < *y);
    /* try the axis the greedy step could not advance: if dx was the main pull, try
       vertical detours first, and vice versa. */
    int try_order[4][2];
    int n = 0;
    if (dy != 0) { try_order[n][0] = 0; try_order[n][1] = dy; n++; }
    if (dx != 0) { try_order[n][0] = dx; try_order[n][1] = 0; n++; }
    try_order[n][0] = 0; try_order[n][1] = (dy != 0) ? -dy : 1; n++;
    try_order[n][0] = (dx != 0) ? -dx : 1; try_order[n][1] = 0; n++;
    for (int i = 0; i < n; i++) {
        int sdx = try_order[i][0], sdy = try_order[i][1];
        if (sdx == 0 && sdy == 0) continue;
        if (encounter_npc_try_step(x, y, size, sdx, sdy, col_npc_blocked, s)) return 1;
    }
    return 0;
}

/** per-NPC movement: simple chase toward the player (no obstacle routefinding,
    matching the wiki default). Melee NPCs path into cardinal melee contact; ranged
    NPCs (serpent/archer/seer/manticore/javelin/shockwave) hold once within
    attack_range AND in line of sight, else step closer. Multi-tile NPCs use the
    shared size-aware step. */
static void col_npc_move_ctx(ColosseumState* s, ColosseumContext* ctx, int idx) {
    (void)ctx;
    ColoNPC* npc = &s->npcs[idx];
    const ColoNpcStats* st = &COLO_NPC_STATS[npc->type];
    if (!st->can_move) return;
    if (npc->stun_timer > 0 || npc->frozen_ticks > 0) return;

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
    int moved = encounter_npc_step_toward(
        &npc->x, &npc->y, tx, ty, size, 1, is_ranged ? 0 : 1, col_npc_blocked, s);
    /* Red Flag: a stuck minotaur routes around the blocker by sidestepping along
       the orthogonal axis (a lightweight stand-in for full obstacle pathing; a
       3x3 BFS each tick is overkill for one reinforcement species). */
    if (!moved && npc->type == COLO_MINOTAUR && col_mod_active(s, COLO_MOD_RED_FLAG))
        moved = col_npc_red_flag_sidestep(s, &npc->x, &npc->y, tx, ty, size);
    col_stamp_npc_collision_footprint(s, npc->x, npc->y, size, 1);
    npc->moved_this_tick = moved ? 1 : 0;
}
