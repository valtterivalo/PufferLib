/* Bork and the Smorks. Measured from ONE recorded fight (fixtures/tfa_bork_events.tsv, 2 players, a wipe),
   so every number is a single sample unless the comment says otherwise. Wiki claims are marked [wiki]
   (Strategies page, tfa-research.md) and are provisional. */

#define BORK_NPC 16640
#define BORK_NPC_SMORKS 16641

#define BORK_ANIM_GONG 14542
#define BORK_ANIM_STOMP 14550
#define BORK_ANIM_STOMP_END 14551
#define BORK_ANIM_STUN 14545
#define BORK_ANIM_CANNON 14554
#define BORK_ANIM_BIG_CANNON 14555
#define BORK_PROJ_NAPALM 4045
#define BORK_PROJ_RANGED 4056
#define BORK_PROJ_MAGIC 4057
#define BORK_PROJ_BOMB 4440
#define BORK_OBJ_SCORCH 62544

typedef struct {
    int first_gong;              /* first gong 8 after "Challenge started" (spawn +32), 1/1 */
    int gong_interval;           /* 14542 -> 14542 7 in 17/19 */
    int stomp_freeze_before;     /* gong 3 before the stomp is skipped (42 of 45), Bork still from stomp-3 to stomp+13, 2/2 */
    int stomp_freeze_after;
    int stomp_to_gong;           /* 14550 -> next gong 15 (45 -> 60, 114 -> 129), 2/2 */
    int stomp_end_anim;          /* 14550 -> 14551 6, 2/2 */
    int bomb_to_stomp;           /* 14555 -> 14550 10, 2/2 */
    int stomp_pulse_first;       /* arena dust at stomp +1, +4, +7, +10 (46 49 52 55, 115 118 121 124), 2/2 */
    int stomp_pulse_gap;
    int stomp_pulses;
    int stomp_min_damage, stomp_max_damage; /* unprotected 50 17 on P0, 50 43 on P1, 4 hits */
    int wide_reach_x2;           /* gong dust reaches 6.5 tiles from the centre in 8 of 17 gongs */
    int far_reach_x2;            /* the rest reach 8.5 to 10.5 around a shadow-clipped box: provisional, [wiki] stronger beats when the target is out of melee range */
    int far_target_distance;     /* provisional */
    int wide_min_damage, wide_max_damage; /* P1 hits at the gong tick 16..25 (n=11), P0 23 */
    int ring_delay;              /* 17 dust tiles at gong +2 around the footprint, P0 hit 35 at +2, n=1 */
    int ring_min_damage, ring_max_damage; /* provisional around the single 35 */
    int protected_pct;           /* [wiki] Protect from Melee reduces the damage: provisional */
    int flat_armour;             /* [wiki edit 15:46] 12, 0 while stunned */
    int arena_x0, arena_y0, arena_x1, arena_y1; /* union of the stomp dust over 8 pulses */
    int smork_first;             /* first cannon at gong +5 (spawn +37), 1/1 */
    int smork_short_min, smork_short_max; /* cannon gaps alternate 7..9 and 12..14, 15 gaps */
    int smork_long_min, smork_long_max;
    int bomb_first;              /* first big cannon at 35 after the first gong, 1/1 */
    int bomb_gap_min, bomb_gap_max; /* 35 -> 104 -> 159: 69, 55 */
    int ranged_weight, magic_weight, napalm_weight; /* 7 ranged, 3 magic, 3 napalm among 13 non-bomb cannons */
    int ranged_hit;              /* 4056 flight 5, hit at anim +5, 2/2 */
    int magic_hit;               /* 4057 flight 5, hit at anim +5, 1/1 */
    int ranged_max_hit;          /* 8 seen through Protect from Melee: provisional */
    int magic_max_hit;           /* 13 and 30 seen: provisional */
    int napalm_flight;           /* 4045 flight 4, 3/3 */
    int napalm_scorch_delay;     /* scorch objects 1 after the landing gfx, 3/3 */
    int bomb_proj_delay;         /* 4440 first seen 1 after the anim, 3/3 */
    int bomb_flight;             /* 4440 lands 4 after its first tick (flight 4.5 rounded down), 3/3 */
    int napalm_bork_delay;       /* Bork hit 26 one tick after the napalm landed on him, 1/1 */
    int napalm_bork_damage;
    int bomb_bork_damage;        /* 28 at the landing tick, 1/1 */
    int blast_min_damage, blast_max_damage; /* direct hit near the landing tile: no sample, provisional */
    int scorch_min, scorch_max;  /* oil 6, 6 on P0 (n=2), [wiki] 6-7 */
    int napalm_tiles_min, napalm_tiles_max; /* 7, 10, 8 scorched tiles */
    int bomb_tiles_min, bomb_tiles_max;     /* 3, 5 */
    int burn_min, burn_max;      /* P1 burn splats every 4 ticks: 2 x5, 1 x8, 3 x4 */
    int enraged_smork_pct;       /* [wiki] Bork dead: Smorks attack faster, hit everyone harder: provisional */
} TfaBorkTuning;

static const TfaBorkTuning TFA_BORK = {
    .first_gong = 8,
    .gong_interval = 7,
    .stomp_freeze_before = 3,
    .stomp_freeze_after = 13,
    .stomp_to_gong = 15,
    .stomp_end_anim = 6,
    .bomb_to_stomp = 10,
    .stomp_pulse_first = 1,
    .stomp_pulse_gap = 3,
    .stomp_pulses = 4,
    .stomp_min_damage = 17,
    .stomp_max_damage = 50,
    .wide_reach_x2 = 13,
    .far_reach_x2 = 19,
    .far_target_distance = 5,
    .wide_min_damage = 16,
    .wide_max_damage = 25,
    .ring_delay = 2,
    .ring_min_damage = 25,
    .ring_max_damage = 35,
    .protected_pct = 30,
    .flat_armour = 12,
    .arena_x0 = 24, .arena_y0 = 23, .arena_x1 = 38, .arena_y1 = 39,
    .smork_first = 13,
    .smork_short_min = 7, .smork_short_max = 9,
    .smork_long_min = 12, .smork_long_max = 14,
    .bomb_first = 43,
    .bomb_gap_min = 55, .bomb_gap_max = 69,
    .ranged_weight = 7, .magic_weight = 3, .napalm_weight = 3,
    .ranged_hit = 5,
    .magic_hit = 5,
    .ranged_max_hit = 20,
    .magic_max_hit = 30,
    .napalm_flight = 4,
    .napalm_scorch_delay = 1,
    .bomb_proj_delay = 1,
    .bomb_flight = 4,
    .napalm_bork_delay = 1,
    .napalm_bork_damage = 26,
    .bomb_bork_damage = 28,
    .blast_min_damage = 15,
    .blast_max_damage = 30,
    .scorch_min = 6, .scorch_max = 7,
    .napalm_tiles_min = 7, .napalm_tiles_max = 10,
    .bomb_tiles_min = 3, .bomb_tiles_max = 5,
    .burn_min = 1, .burn_max = 3,
    .enraged_smork_pct = 150,
};

static TfaNpc* tfa_bork_npc(TfaState* s) { return &s->npcs[0]; }
static TfaNpc* tfa_bork_smorks(TfaState* s) { return &s->npcs[1]; }

static void tfa_bork_enter(TfaState* s) {
    const TfaRoomLayout* l = tfa_room_layout(TFA_ROOM_BORK);
    TfaNpc* b = tfa_npc_spawn(s, TFA_NPC_BORK, MON_TFA_BORK, BORK_NPC, l->boss_x, l->boss_y);
    TfaNpc* m = tfa_npc_spawn(s, TFA_NPC_SMORKS, MON_TFA_SMORKS, BORK_NPC_SMORKS, 30, 30);
    assert(tfa_npc_slot(s, b) == 0 && tfa_npc_slot(s, m) == 1);
    s->boss.bork = (TfaBorkState){
        .next_gong = TFA_BORK.first_gong,
        .next_smork = TFA_BORK.first_gong + 5,
        .next_bomb = TFA_BORK.bomb_first,
        .stomp_tick = -1,
        .target = -1,
    };
}

static void tfa_bork_pend(TfaBorkState* bs, int tick, int kind, int x, int y) {
    assert(bs->pending_count < TFA_BORK_MAX_PENDING);
    bs->pending[bs->pending_count++] = (TfaBorkPending){ tick, kind, x, y };
}

/* Tiles hidden from Bork's centre by the Smorks footprint: the plugin's BorkShadow, which the
   recorded gong dust avoids (dust tiles inside it: 0..10 of 62..176 per gong). */
static int tfa_bork_shadowed(const TfaNpc* b, const TfaNpc* m, int x, int y) {
    double px = b->x + 2.0, py = b->y + 2.0;
    double dx = x + 0.5 - px, dy = y + 0.5 - py;
    double lo[4] = { m->x + 0.5, m->y + 0.5 }, hi[4] = { m->x + m->size - 0.5, m->y + m->size - 0.5 };
    double enter = 0.0, exit = 1.0;
    double d[2] = { dx, dy }, p[2] = { px, py };
    for (int axis = 0; axis < 2; axis++) {
        if (d[axis] == 0.0) {
            if (p[axis] < lo[axis] || p[axis] > hi[axis]) return 0;
            continue;
        }
        double r0 = (lo[axis] - p[axis]) / d[axis], r1 = (hi[axis] - p[axis]) / d[axis];
        double near = r0 < r1 ? r0 : r1, far = r0 < r1 ? r1 : r0;
        if (near > enter) enter = near;
        if (far < exit) exit = far;
    }
    return enter <= exit;
}

static int tfa_bork_covers(const TfaNpc* b, int x, int y) {
    return x >= b->x && x < b->x + b->size && y >= b->y && y < b->y + b->size;
}

static int tfa_bork_in_arena(int x, int y) {
    return x >= TFA_BORK.arena_x0 && x <= TFA_BORK.arena_x1 && y >= TFA_BORK.arena_y0 && y <= TFA_BORK.arena_y1;
}

static int tfa_bork_first_alive(const TfaState* s) {
    for (int p = 0; p < s->team_size; p++) if (tfa_alive(s, p)) return p;
    return -1;
}

static int tfa_bork_hit_player(TfaState* s, int p, int min, int max, int protected_pct) {
    int raw = tfa_rand_range(s, min, max);
    int dmg = tfa_protected_damage(raw, s->players[p].player.prayer, ATTACK_STYLE_MELEE, protected_pct);
    tfa_damage_player_now(s, p, dmg, ATTACK_STYLE_MELEE);
    return dmg;
}

/* Wide wave around Bork's centre minus his footprint and the Smorks shadow. */
static void tfa_bork_gong(TfaState* s, TfaNpc* b, TfaNpc* m) {
    TfaBorkState* bs = &s->boss.bork;
    b->act = TFA_ACT_MELEE;
    tfa_npc_anim(s, b, BORK_ANIM_GONG);
    int far = bs->target >= 0 && tfa_player_npc_distance(&s->players[bs->target], b) > TFA_BORK.far_target_distance;
    int reach2 = far ? TFA_BORK.far_reach_x2 : TFA_BORK.wide_reach_x2;
    for (int p = 0; p < s->team_size; p++) {
        if (!tfa_alive(s, p)) continue;
        int x = s->players[p].player.x, y = s->players[p].player.y;
        if (!tfa_bork_in_arena(x, y) || tfa_bork_covers(b, x, y) || tfa_bork_shadowed(b, m, x, y)) continue;
        int ox2 = 2 * x + 1 - (2 * b->x + 4), oy2 = 2 * y + 1 - (2 * b->y + 4);
        if (abs(ox2) > reach2 || abs(oy2) > reach2) continue;
        tfa_bork_hit_player(s, p, TFA_BORK.wide_min_damage, TFA_BORK.wide_max_damage, TFA_BORK.protected_pct);
    }
    tfa_bork_pend(bs, s->tick + TFA_BORK.ring_delay, TFA_BORK_PEND_RING, b->x, b->y);
}

static void tfa_bork_ring(TfaState* s, int bx, int by, const TfaNpc* b, const TfaNpc* m) {
    for (int p = 0; p < s->team_size; p++) {
        if (!tfa_alive(s, p)) continue;
        int x = s->players[p].player.x, y = s->players[p].player.y;
        int d = encounter_rect_distance(x, y, 1, bx, by, 4);
        if (d != 1 || tfa_bork_shadowed(b, m, x, y)) continue;
        tfa_bork_hit_player(s, p, TFA_BORK.ring_min_damage, TFA_BORK.ring_max_damage, TFA_BORK.protected_pct);
    }
}

static void tfa_bork_stomp_pulse(TfaState* s, const TfaNpc* b, const TfaNpc* m) {
    const TfaBorkState* bs = &s->boss.bork;
    for (int p = 0; p < s->team_size; p++) {
        if (!tfa_alive(s, p)) continue;
        int x = s->players[p].player.x, y = s->players[p].player.y;
        if (!tfa_bork_in_arena(x, y) || (!bs->smorks_dead && tfa_bork_shadowed(b, m, x, y))) continue;
        tfa_bork_hit_player(s, p, TFA_BORK.stomp_min_damage, TFA_BORK.stomp_max_damage, TFA_BORK.protected_pct);
    }
}

static void tfa_bork_scorch(TfaState* s, int x, int y) {
    const TfaRoomLayout* l = tfa_room_layout(TFA_ROOM_BORK);
    if (!tfa_bork_in_arena(x, y) || tfa_layout_blocked(l, x, y)) return;
    for (int i = 0; i < TFA_MAX_HAZARDS; i++)
        if (s->hazards[i].kind == TFA_HAZ_FLOOR && s->hazards[i].x == x && s->hazards[i].y == y) return;
    tfa_hazard_add(s, (TfaHazard){
        .kind = TFA_HAZ_FLOOR,
        .style = ATTACK_STYLE_NONE,
        .w = 1, .h = 1,
        .owner = -1,
        .effect = TFA_EFFECT_BURN,
        .x = (int16_t)x, .y = (int16_t)y,
        .start_tick = s->tick,
        .end_tick = 1 << 30,
        .min_damage = (int16_t)TFA_BORK.scorch_min,
        .max_damage = (int16_t)TFA_BORK.scorch_max,
        .protected_pct = 100,
        .loc_id = BORK_OBJ_SCORCH,
    });
}

/* A blob of scorch tiles: the landing tile plus distinct random neighbours, offsets seen in 5 clusters
   (napalm dx -3..1, dy -2..3, bomb dx -1..1, dy -1..1). */
static void tfa_bork_scorch_cluster(TfaState* s, int tx, int ty, int count, int dx0, int dx1, int dy0, int dy1) {
    tfa_bork_scorch(s, tx, ty);
    int offsets[64][2], n = 0;
    for (int dx = dx0; dx <= dx1; dx++)
        for (int dy = dy0; dy <= dy1; dy++)
            if (dx != 0 || dy != 0) { offsets[n][0] = dx; offsets[n][1] = dy; n++; }
    for (int k = 1; k < count; k++) {
        int pick = k - 1 + tfa_rand(s, n - (k - 1));
        int sx = offsets[pick][0], sy = offsets[pick][1];
        offsets[pick][0] = offsets[k - 1][0];
        offsets[pick][1] = offsets[k - 1][1];
        offsets[k - 1][0] = sx;
        offsets[k - 1][1] = sy;
        tfa_bork_scorch(s, tx + sx, ty + sy);
    }
}

static void tfa_bork_blast(TfaState* s, int tx, int ty) {
    for (int p = 0; p < s->team_size; p++) {
        const Player* pl = &s->players[p].player;
        if (tfa_alive(s, p) && chebyshev_distance(pl->x, pl->y, tx, ty) <= 1)
            tfa_damage_player_now(s, p, tfa_rand_range(s, TFA_BORK.blast_min_damage, TFA_BORK.blast_max_damage), ATTACK_STYLE_NONE);
    }
}

static void tfa_bork_landing(TfaState* s, TfaNpc* b, int tx, int ty, int bork_damage, int scorch_kind) {
    TfaBorkState* bs = &s->boss.bork;
    tfa_bork_blast(s, tx, ty);
    if (!bs->bork_dead && tfa_bork_covers(b, tx, ty)) tfa_npc_take_damage(s, b, bork_damage, -1, ATTACK_STYLE_NONE);
    tfa_bork_pend(bs, s->tick + TFA_BORK.napalm_scorch_delay, scorch_kind, tx, ty);
}

static void tfa_bork_cannon(TfaState* s, TfaNpc* m) {
    TfaBorkState* bs = &s->boss.bork;
    int first = tfa_bork_first_alive(s);
    if (first < 0) return;
    bs->target = bs->target >= 0 && tfa_alive(s, bs->target) ? bs->target : first;
    const Player* tp = &s->players[bs->target].player;
    m->act = TFA_ACT_RANGED;
    m->target = (int8_t)bs->target;
    int heavy = bs->next_bomb <= s->tick && !bs->smorks_dead;
    int pick = tfa_rand(s, TFA_BORK.ranged_weight + TFA_BORK.magic_weight + TFA_BORK.napalm_weight);
    if (heavy) {
        bs->next_bomb = s->tick + tfa_rand_range(s, TFA_BORK.bomb_gap_min, TFA_BORK.bomb_gap_max);
        bs->stomp_tick = s->tick + TFA_BORK.bomb_to_stomp;
        tfa_npc_anim(s, m, BORK_ANIM_BIG_CANNON);
        tfa_fx_projectile(s, m, bs->target, BORK_PROJ_BOMB, SPOTANIM_TFA_BORK_CANNONBALL_TRAVEL_BIG,
            SPOTANIM_TFA_BORK_CANNONBALL_TRAVEL_BIG_ANIM, TFA_BORK.bomb_proj_delay, TFA_BORK.bomb_flight);
        tfa_bork_pend(bs, s->tick + TFA_BORK.bomb_proj_delay + TFA_BORK.bomb_flight, TFA_BORK_PEND_BOMB_LAND, tp->x, tp->y);
        return;
    }
    tfa_npc_anim(s, m, BORK_ANIM_CANNON);
    if (pick < TFA_BORK.napalm_weight) {
        tfa_fx_projectile(s, m, bs->target, BORK_PROJ_NAPALM, SPOTANIM_TFA_BORK_VFX_SMORK_NAPALM_FIRE_PROJANIM01,
            SPOTANIM_TFA_BORK_VFX_SMORK_NAPALM_FIRE_PROJANIM01_ANIM, 0, TFA_BORK.napalm_flight);
        tfa_bork_pend(bs, s->tick + TFA_BORK.napalm_flight, TFA_BORK_PEND_NAPALM_LAND, tp->x, tp->y);
        return;
    }
    int magic = pick < TFA_BORK.napalm_weight + TFA_BORK.magic_weight;
    AttackStyle style = magic ? ATTACK_STYLE_MAGIC : ATTACK_STYLE_RANGED;
    int enraged = bs->bork_dead;
    for (int p = 0; p < s->team_size; p++) {
        if (!tfa_alive(s, p) || (!enraged && p != bs->target)) continue;
        int max_hit = magic ? TFA_BORK.magic_max_hit : TFA_BORK.ranged_max_hit;
        if (enraged) max_hit = max_hit * TFA_BORK.enraged_smork_pct / 100;
        int raw = tfa_npc_roll(s, m, p, style, max_hit);
        tfa_queue_npc_hit_land(s, p, raw, style, TFA_BORK.ranged_hit, 0, m);
        if (magic)
            tfa_fx_projectile(s, m, p, BORK_PROJ_MAGIC, SPOTANIM_TFA_BORK_VFX_SMORK_MAGE_ATTACK_PROJANIM01,
                SPOTANIM_TFA_BORK_VFX_SMORK_MAGE_ATTACK_PROJANIM01_ANIM, 0, TFA_BORK.magic_hit);
        else
            tfa_fx_projectile(s, m, p, BORK_PROJ_RANGED, SPOTANIM_TFA_BORK_VFX_SMORK_RANGE_ATTACK_PROJANIM01,
                SPOTANIM_TFA_BORK_VFX_SMORK_RANGE_ATTACK_PROJANIM01_ANIM, 0, TFA_BORK.ranged_hit);
        m->act = magic ? TFA_ACT_MAGIC : TFA_ACT_RANGED;
    }
}

static void tfa_bork_pending_tick(TfaState* s, TfaNpc* b, TfaNpc* m) {
    TfaBorkState* bs = &s->boss.bork;
    for (int i = 0; i < bs->pending_count; i++) {
        TfaBorkPending e = bs->pending[i];
        if (e.tick != s->tick) continue;
        bs->pending[i--] = bs->pending[--bs->pending_count];
        switch ((TfaBorkPendKind)e.kind) {
            case TFA_BORK_PEND_NAPALM_LAND:
                tfa_bork_landing(s, b, e.x, e.y, TFA_BORK.napalm_bork_damage, TFA_BORK_PEND_SCORCH_NAPALM);
                break;
            case TFA_BORK_PEND_BOMB_LAND:
                tfa_bork_landing(s, b, e.x, e.y, TFA_BORK.bomb_bork_damage, TFA_BORK_PEND_SCORCH_BOMB);
                if (tfa_bork_covers(b, e.x, e.y)) {
                    bs->stunned = 1;
                    b->act = TFA_ACT_STUNNED;
                    if (b->anim_tick != s->tick) tfa_npc_anim(s, b, BORK_ANIM_STUN);
                }
                break;
            case TFA_BORK_PEND_SCORCH_NAPALM:
                tfa_bork_scorch_cluster(s, e.x, e.y, tfa_rand_range(s, TFA_BORK.napalm_tiles_min, TFA_BORK.napalm_tiles_max), -3, 1, -2, 3);
                break;
            case TFA_BORK_PEND_SCORCH_BOMB:
                tfa_bork_scorch_cluster(s, e.x, e.y, tfa_rand_range(s, TFA_BORK.bomb_tiles_min, TFA_BORK.bomb_tiles_max), -1, 1, -1, 1);
                break;
            case TFA_BORK_PEND_RING:
                tfa_bork_ring(s, e.x, e.y, b, m);
                break;
            case TFA_BORK_PEND_PULSE:
                tfa_bork_stomp_pulse(s, b, m);
                break;
        }
    }
}

/* One cardinal step toward the target along the larger axis, the other axis when blocked, else stay. */
static void tfa_bork_chase(TfaState* s, TfaNpc* b, const TfaNpc* m) {
    int t = s->boss.bork.target;
    if (t < 0) return;
    const Player* pl = &s->players[t].player;
    int dx = pl->x - (b->x + 1), dy = pl->y - (b->y + 1);
    int sx = (dx > 0) - (dx < 0), sy = (dy > 0) - (dy < 0);
    int order_x = abs(dx) >= abs(dy);
    for (int k = 0; k < 2; k++) {
        int use_x = (k == 0) == order_x;
        int nx = b->x + (use_x ? sx : 0), ny = b->y + (use_x ? 0 : sy);
        if (nx == b->x && ny == b->y) continue;
        int free = 1;
        for (int ox = 0; ox < b->size && free; ox++)
            for (int oy = 0; oy < b->size && free; oy++) {
                int tx = nx + ox, ty = ny + oy;
                free = !tfa_layout_blocked(tfa_room_layout(TFA_ROOM_BORK), tx, ty) &&
                    !(tx >= m->x && tx < m->x + m->size && ty >= m->y && ty < m->y + m->size);
            }
        if (!free) continue;
        b->x = (int16_t)nx;
        b->y = (int16_t)ny;
        return;
    }
}

static int tfa_bork_npc_damage(TfaState* s, TfaNpc* n, int damage, int attacker, AttackStyle style) {
    (void)attacker;
    if (n->kind != TFA_NPC_BORK && n->kind != TFA_NPC_SMORKS) return 0;
    int armour = n->kind == TFA_NPC_SMORKS ? 2 : s->boss.bork.stunned ? 0 : TFA_BORK.flat_armour;
    if (style != ATTACK_STYLE_NONE) damage = damage > armour ? damage - armour : 0;
    int applied = damage < n->hp ? damage : n->hp;
    n->hp -= applied;
    return applied;
}

static void tfa_bork_npc_died(TfaState* s, TfaNpc* n, int attacker) {
    (void)attacker;
    TfaBorkState* bs = &s->boss.bork;
    n->attackable = 0;
    n->active = 0;
    if (n->kind == TFA_NPC_BORK) bs->bork_dead = 1;
    else bs->smorks_dead = 1;
}

static void tfa_bork_tick(TfaState* s, TfaContext* ctx) {
    TfaBorkState* bs = &s->boss.bork;
    if (bs->bork_dead && bs->smorks_dead) {
        tfa_room_cleared(s, ctx);
        return;
    }
    TfaNpc* b = tfa_bork_npc(s);
    TfaNpc* m = tfa_bork_smorks(s);
    b->act = TFA_ACT_IDLE;
    m->act = TFA_ACT_IDLE;
    bs->clock++;
    int first = tfa_bork_first_alive(s);
    if (bs->target < 0 || !tfa_alive(s, bs->target)) bs->target = first;
    tfa_bork_pending_tick(s, b, m);

    for (int p = 0; p < s->team_size; p++) {
        TfaPlayer* tp = &s->players[p];
        if (tfa_alive(s, p) && tp->burn_ticks % 4 == 2)
            tfa_damage_player_now(s, p, tfa_rand_range(s, TFA_BORK.burn_min, TFA_BORK.burn_max), ATTACK_STYLE_NONE);
    }

    if (!bs->bork_dead) {
        int frozen = bs->stomp_tick >= 0 &&
            s->tick >= bs->stomp_tick - TFA_BORK.stomp_freeze_before && s->tick <= bs->stomp_tick + TFA_BORK.stomp_freeze_after;
        if (!frozen) tfa_bork_chase(s, b, m);
        if (bs->stomp_tick >= 0 && s->tick == bs->stomp_tick) {
            b->act = TFA_ACT_SPECIAL;
            tfa_npc_anim(s, b, BORK_ANIM_STOMP);
            for (int k = 0; k < TFA_BORK.stomp_pulses; k++)
                tfa_bork_pend(bs, s->tick + TFA_BORK.stomp_pulse_first + k * TFA_BORK.stomp_pulse_gap, TFA_BORK_PEND_PULSE, 0, 0);
            bs->stunned = 0;
            bs->next_gong = s->tick + TFA_BORK.stomp_to_gong;
        }
        if (bs->stomp_tick >= 0 && s->tick == bs->stomp_tick + TFA_BORK.stomp_end_anim) tfa_npc_anim(s, b, BORK_ANIM_STOMP_END);
        if (bs->stomp_tick >= 0 && s->tick > bs->stomp_tick + TFA_BORK.stomp_freeze_after) bs->stomp_tick = -1;
        if (bs->stomp_tick < 0 && bs->smorks_dead && s->tick >= bs->next_bomb) {
            bs->stomp_tick = s->tick + TFA_BORK.bomb_to_stomp;
            bs->next_bomb = s->tick + tfa_rand_range(s, TFA_BORK.bomb_gap_min, TFA_BORK.bomb_gap_max);
        }
        if (s->tick == bs->next_gong) {
            int skipped = bs->stomp_tick >= 0 &&
                s->tick >= bs->stomp_tick - TFA_BORK.stomp_freeze_before && s->tick < bs->stomp_tick + TFA_BORK.stomp_to_gong;
            if (!skipped && first >= 0) tfa_bork_gong(s, b, m);
            if (!(bs->stomp_tick >= 0 && s->tick >= bs->stomp_tick)) bs->next_gong += TFA_BORK.gong_interval;
        }
        b->attack_timer = (int16_t)(bs->next_gong > s->tick ? bs->next_gong - s->tick : 0);
    }

    if (!bs->smorks_dead) {
        if (s->tick == bs->next_smork && first >= 0) {
            tfa_bork_cannon(s, m);
            int lo = bs->smork_long ? TFA_BORK.smork_long_min : TFA_BORK.smork_short_min;
            int hi = bs->smork_long ? TFA_BORK.smork_long_max : TFA_BORK.smork_short_max;
            int gap = tfa_rand_range(s, lo, hi);
            if (bs->bork_dead) gap = gap * 100 / TFA_BORK.enraged_smork_pct;
            bs->next_smork = s->tick + gap;
            bs->smork_long = !bs->smork_long;
        }
        m->attack_timer = (int16_t)(bs->next_smork > s->tick ? bs->next_smork - s->tick : 0);
    }
}
