#line 76 "encounter_colosseum.h"
/**
 * @file encounter_colosseum_movement.inc
 * @brief Arena predicates + per-NPC movement, mirroring inferno movement.
 */

static int col_in_arena(int x, int y) {
    return x >= COLO_ARENA_MIN_X && x <= COLO_ARENA_MAX_X &&
           y >= COLO_ARENA_MIN_Y && y <= COLO_ARENA_MAX_Y;
}

/** a tile is a Sol pillar (1x1 LoS blocker, also blocks movement) iff the boss
    arena is active and (x, y) matches one of the four pillar positions. */
static int col_is_boss_pillar(const ColosseumState* s, int x, int y) {
    if (!col_sol_clamp_active(s)) return 0;
    for (int p = 0; p < COLO_BOSS_NUM_PILLARS; p++)
        if (COLO_BOSS_PILLARS[p][0] == x && COLO_BOSS_PILLARS[p][1] == y) return 1;
    return 0;
}

/** player-walkability callback for shared movement helpers. During wave 12 the
    player is confined to the boss arena and cannot stand on a pillar tile. */
static int col_player_walkable(void* ctx, int x, int y) {
    ColosseumState* s = (ColosseumState*)ctx;
    if (!col_in_arena(x, y)) return 0;
    if (col_sol_clamp_active(s) && !col_in_boss_arena(s, x, y)) return 0;
    if (col_is_boss_pillar(s, x, y)) return 0;
    int gx, gy;
    if (!col_grid_index(x, y, &gx, &gy)) return 0;
    return s->player_collision_flags[gx][gy] ? 0 : 1;
}

/** open arena: no LoS blockers, so an NPC has line of sight whenever in range.
    kept as a named predicate so adding pillars later is a one-line change. */
static int col_npc_has_los_to_player(const ColosseumState* s, const ColoNPC* npc) {
    (void)s;
    (void)npc;
    return 1;
}

/** NPC-blocked callback (size-aware) for the shared chase step. During wave 12
    NPC movement is confined to the boss arena and blocked by pillar tiles. */
static int col_npc_blocked(void* ctx, int x, int y, int size) {
    ColosseumState* s = (ColosseumState*)ctx;
    for (int dx = 0; dx < size; dx++) {
        for (int dy = 0; dy < size; dy++) {
            if (!col_in_arena(x + dx, y + dy)) return 1;
            if (col_sol_clamp_active(s) && !col_in_boss_arena(s, x + dx, y + dy)) return 1;
            if (col_is_boss_pillar(s, x + dx, y + dy)) return 1;
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
