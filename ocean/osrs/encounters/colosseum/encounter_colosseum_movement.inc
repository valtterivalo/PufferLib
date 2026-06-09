#line 76 "encounter_colosseum.h"
/**
 * @file encounter_colosseum_movement.inc
 * @brief Arena predicates + per-NPC movement, mirroring inferno movement.
 */

static int col_in_arena(int x, int y) {
    return x >= COLO_ARENA_MIN_X && x <= COLO_ARENA_MAX_X &&
           y >= COLO_ARENA_MIN_Y && y <= COLO_ARENA_MAX_Y;
}

/** player-walkability callback for shared movement helpers. */
static int col_player_walkable(void* ctx, int x, int y) {
    ColosseumState* s = (ColosseumState*)ctx;
    if (!col_in_arena(x, y)) return 0;
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

/** NPC-blocked callback (size-aware) for the shared chase step. */
static int col_npc_blocked(void* ctx, int x, int y, int size) {
    ColosseumState* s = (ColosseumState*)ctx;
    for (int dx = 0; dx < size; dx++) {
        for (int dy = 0; dy < size; dy++) {
            if (!col_in_arena(x + dx, y + dy)) return 1;
            int gx, gy;
            if (!col_grid_index(x + dx, y + dy, &gx, &gy)) return 1;
            if (s->npc_collision_flags[gx][gy]) return 1;
        }
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
    col_stamp_npc_collision_footprint(s, npc->x, npc->y, size, 1);
    npc->moved_this_tick = moved ? 1 : 0;
}
