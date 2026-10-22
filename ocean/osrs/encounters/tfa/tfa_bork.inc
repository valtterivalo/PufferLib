/* Bork and the Smorks. Measured from two recorded fights (fixtures/tfa_bork_events.tsv): bork_01, 2 players kiting Bork, a wipe,
   and bork_02, 5 players with a Bork tank, every bomb landed on Bork, a kill. Counts are over both unless the comment says otherwise.
   Wiki claims are marked [wiki] (Strategies and Talk:Bork, tfa-research.md) and are provisional. */

#define BORK_NPC 16640
#define BORK_NPC_SMORKS 16641

#define BORK_ANIM_GONG 14542
#define BORK_ANIM_ANGRY 14549
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
#define BORK_GFX_DUST 4395
#define BORK_GFX_FIRE 4046
#define BORK_GFX_SPLAT 4047
#define BORK_SPLAT_VARIANTS 4

typedef struct {
    int first_gong;              /* first gong 8 after "Challenge started" (spawn +32), 1/1 */
    int gong_interval[2];        /* 14542 -> 14542 by pace: 7 (30 gaps), 6 (16 gaps) */
    int miss_stomp;              /* bomb missed Bork: 14550 5 after the landing, Bork still from 3 before it (2/2) */
    int miss_freeze;
    int stun_angry;              /* bomb on Bork: stun 14545 at the landing, 14549 14 later (4/4 stuns, 3 angers), 14550 2 after that (3/3) */
    int angry_stomp;
    int stomp_end_anim;          /* 14550 -> 14551 6, 5/5 */
    int resume_miss, resume_stun;/* 14550 -> next gong 15 after a miss (2/2), 17 after a stun (3/3); Bork walks again 1 before it */
    int retarget;                /* "Bork smash <name> now." 13 after the 14550 (3/3): a new tank, never the Smorks' target or the old tank */
    int pace_hp_pct;             /* "Smorks go faster!" 1 after the first gong after a stomp with Bork at 52.6%; none at 73% or 93%: provisional */
    int stomp_pulse_first;       /* arena dust at stomp +1, +4, +7, +10, 5/5 */
    int stomp_pulse_gap;
    int stomp_pulses;
    int stomp_min_damage, stomp_max_damage; /* unprotected 50 17 on P0, 50 43 on P1, 49, 5 hits */
    int wide_min_damage, wide_max_damage; /* unprayed hits at the gong tick 16..27 (n=20) */
    int ring_delay;              /* ring dust at gong +2 around the footprint, 49/52 (the others cancelled) */
    int ring_min_damage, ring_max_damage; /* unprayed at gong +2: 30, 30, 35 */
    int protected_pct;           /* Protect from Melee: 4-7 at the gong (n=30), 5-8 at +2 (n=30) */
    int flat_armour;             /* wiki rev 15379087: 8 (12 before), 0 while stunned */
    int arena_x0, arena_y0, arena_x1, arena_y1; /* union of the stomp dust over 8 pulses */
    int smork_first;             /* first Smorks attack 5 after the first gong, 2/2 */
    int smork_napalm_recover[2]; /* napalm -> next attack 13 (4/4), 11 fast (2/2); auto -> napalm 1 more than auto -> auto, auto -> bomb 2 more */
    int smork_autos_opening;     /* autos before the napalm: 1 in the opening cycle (2/2), then 2 (1/1), 3 fast (2/2) */
    int smork_autos_before[2];
    int smork_autos_after_min, smork_autos_after_max; /* autos between the napalm and the bomb: 1 (4), 2 (2) */
    int smork_after_miss;        /* next attack 12 after a bomb that missed (2/2); after a stun the Smorks resume one interval after Bork's gong (3/3) */
    int smork_flight[3];         /* 4056/4057 flight by pace 5, 4 and 3 once Bork is dead (every volley) */
    int ranged_weight, magic_weight; /* volleys 15 ranged, 9 magic; enraged 11, 12 */
    int enraged_first;           /* Bork dead: first volley 11 later, then 2 apart in bursts of 5 between napalms 14 apart (5/5 napalms) */
    int enraged_gap;
    int enraged_burst;
    int enraged_to_napalm;
    int bomb_gap_min, bomb_gap_max; /* Smorks dead, Bork stomps on his own: provisional, from the bomb gaps 69 and 55 of bork_01 */
    int smorks_dead_stomp;       /* 10 after the would-be bomb, as after a miss */
    int ranged_max_hit;          /* 8 seen through Protect from Melee: provisional */
    int magic_max_hit;           /* 13, 17, 26 and 30 seen: provisional */
    int napalm_flight;           /* 4045 flight 4, 15/15 */
    int napalm_scorch_delay;     /* scorch objects 1 after the landing gfx, 3/3 */
    int bomb_proj_delay;         /* 4440 first seen 1 after the anim, 7/7 */
    int bomb_flight;             /* 4440 lands 4 after its first tick (flight 4.5 rounded down), 7/7 */
    int napalm_bork_delay;       /* Bork hit 26 one tick after the napalm landed on him, 1/1 */
    int napalm_bork_damage;
    int bomb_bork_damage;        /* 28 at the landing tick, 1/1 */
    int blast_min_damage, blast_max_damage; /* standing on a napalm fire tile at the landing: 27, 26 (n=2, raiders 2 away off the fire unhit); the bomb's: no sample */
    int scorch_min, scorch_max;  /* oil 6, 6 on P0 (n=2), [wiki] 6-7 */
    int napalm_tiles_min, napalm_tiles_max; /* 7, 10, 8 scorched tiles */
    int napalm_delay_step;       /* fire delay 1 + 3 per tile from the impact, 2/2 */
    int stomp_feet[8][2][2];     /* stomp dust source per facing (S, SW, W, NW, N, NE, E, SE toward the tank): the two feet, pulses alternate from a random one (4/5 stomps; the other turned) */
    int scorch_life;             /* every scorch tile lasts 199 ticks (21/21 removed before the fight ended), the rest go with the fight */
    int bomb_tiles_min, bomb_tiles_max;     /* 3, 5 */
    int burn_min, burn_max;      /* P1 burn splats every 4 ticks: 2 x5, 1 x8, 3 x4 */
    int enraged_smork_pct;       /* [wiki] Bork dead: Smorks hit everyone harder: provisional */
} TfaBorkTuning;

static const TfaBorkTuning TFA_BORK = {
    .first_gong = 8,
    .gong_interval = { 7, 6 },
    .miss_stomp = 5,
    .miss_freeze = 3,
    .stun_angry = 14,
    .angry_stomp = 2,
    .stomp_end_anim = 6,
    .resume_miss = 15,
    .resume_stun = 17,
    .retarget = 13,
    .pace_hp_pct = 60,
    .stomp_pulse_first = 1,
    .stomp_pulse_gap = 3,
    .stomp_pulses = 4,
    .stomp_min_damage = 17,
    .stomp_max_damage = 50,
    .wide_min_damage = 16,
    .wide_max_damage = 27,
    .ring_delay = 2,
    .ring_min_damage = 25,
    .ring_max_damage = 35,
    .protected_pct = 25,
    .flat_armour = 8,
    .arena_x0 = 24, .arena_y0 = 23, .arena_x1 = 38, .arena_y1 = 39,
    .smork_first = 5,
    .smork_napalm_recover = { 13, 11 },
    .smork_autos_opening = 1,
    .smork_autos_before = { 2, 3 },
    .smork_autos_after_min = 1, .smork_autos_after_max = 2,
    .smork_after_miss = 12,
    .smork_flight = { 5, 4, 3 },
    .ranged_weight = 5, .magic_weight = 3,
    .enraged_first = 11,
    .enraged_gap = 2,
    .enraged_burst = 5,
    .enraged_to_napalm = 3,
    .bomb_gap_min = 55, .bomb_gap_max = 69,
    .smorks_dead_stomp = 10,
    .ranged_max_hit = 20,
    .magic_max_hit = 30,
    .napalm_flight = 4,
    .napalm_scorch_delay = 1,
    .bomb_proj_delay = 1,
    .bomb_flight = 4,
    .napalm_bork_delay = 1,
    .napalm_bork_damage = 26,
    .bomb_bork_damage = 28,
    .blast_min_damage = 26,
    .blast_max_damage = 27,
    .scorch_min = 6, .scorch_max = 7,
    .napalm_tiles_min = 7, .napalm_tiles_max = 10,
    .napalm_delay_step = 3,
    .stomp_feet = { { {1, 0}, {2, 0} }, { {0, 1}, {1, 0} }, { {0, 1}, {0, 2} }, { {0, 2}, {1, 3} },
        { {1, 3}, {2, 3} }, { {2, 3}, {3, 2} }, { {3, 1}, {3, 2} }, { {2, 0}, {3, 1} } },
    .scorch_life = 199,
    .bomb_tiles_min = 3, .bomb_tiles_max = 5,
    .burn_min = 1, .burn_max = 3,
    .enraged_smork_pct = 150,
};

static TfaNpc* tfa_bork_npc(TfaState* s) { return &s->npcs[0]; }
static TfaNpc* tfa_bork_smorks(TfaState* s) { return &s->npcs[1]; }

/* A random living raider other than a and b, else other than a, else any living one; -1 when none is alive. */
static int tfa_bork_pick(TfaState* s, int a, int b) {
    int pool[TFA_MAX_PLAYERS], n = 0;
    for (int pass = 0; pass < 3 && n == 0; pass++)
        for (int p = 0; p < s->team_size; p++)
            if (tfa_alive(s, p) && (pass == 2 || (p != a && (pass == 1 || p != b)))) pool[n++] = p;
    return n ? pool[tfa_rand(s, n)] : -1;
}

static void tfa_bork_enter(TfaState* s) {
    const TfaRoomLayout* l = tfa_room_layout(TFA_ROOM_BORK);
    TfaNpc* b = tfa_npc_spawn(s, TFA_NPC_BORK, MON_TFA_BORK, BORK_NPC, l->boss_x, l->boss_y);
    TfaNpc* m = tfa_npc_spawn(s, TFA_NPC_SMORKS, MON_TFA_SMORKS, BORK_NPC_SMORKS, 30, 30);
    b->hp = b->max_hp = tfa_party_hp(s->team_size, b->max_hp, b->max_hp * TFA_TRIO_SCALE_PCT / 100);
    m->hp = m->max_hp = tfa_party_hp(s->team_size, m->max_hp, m->max_hp * TFA_TRIO_SCALE_PCT / 100);
    assert(tfa_npc_slot(s, b) == 0 && tfa_npc_slot(s, m) == 1);
    s->boss.bork = (TfaBorkState){
        .next_gong = TFA_BORK.first_gong,
        .next_smork = TFA_BORK.first_gong + TFA_BORK.smork_first,
        .smork_move = TFA_BORK_SMORK_AUTO,
        .autos_left = TFA_BORK.smork_autos_opening,
        .after_autos = TFA_BORK_SMORK_NAPALM,
        .stomp_tick = -1,
        .angry_tick = -1,
        .freeze_from = -1,
        .resume_tick = -1,
    };
    TfaBorkState* bs = &s->boss.bork;
    bs->tank = tfa_rand(s, s->team_size);
    bs->smork_target = tfa_bork_pick(s, bs->tank, bs->tank);
}

static void tfa_bork_pend(TfaBorkState* bs, int tick, int kind, int x, int y, int arg) {
    assert(bs->pending_count < TFA_BORK_MAX_PENDING);
    bs->pending[bs->pending_count++] = (TfaBorkPending){ tick, kind, x, y, arg };
}

/* Tiles hidden from the wave source at Bork's centre by the Smorks footprint (dust inside it: 0 of 2400 present tiles
   against 30 of ~2400 tile calls disagreeing over 19 gongs). */
static int tfa_bork_shadowed(int sx, int sy, const TfaNpc* m, int x, int y) {
    double px = sx + 2.0, py = sy + 2.0;
    double dx = x + 0.5 - px, dy = y + 0.5 - py;
    double lo[2] = { m->x + 0.5, m->y + 0.5 }, hi[2] = { m->x + m->size - 0.5, m->y + m->size - 0.5 };
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

static int tfa_bork_walkable(int x, int y) {
    return tfa_bork_in_arena(x, y) && !tfa_layout_blocked(tfa_room_layout(TFA_ROOM_BORK), x, y);
}

static int tfa_bork_hit_player(TfaState* s, int p, int min, int max, int protected_pct) {
    int raw = tfa_rand_range(s, min, max);
    int dmg = tfa_protected_damage(raw, s->players[p].player.prayer, ATTACK_STYLE_MELEE, protected_pct);
    tfa_damage_player_now(s, p, dmg, ATTACK_STYLE_MELEE);
    return dmg;
}

/* Dust 1 + Manhattan distance cycles from the source tile (sx + 2, sy + 2) over every wave, ring and stomp tile (19 gongs, 18 rings, 8 pulses, exact). */
static void tfa_bork_dust(TfaState* s, int sx, int sy, int x, int y) {
    tfa_fx_ground(s, x, y, BORK_GFX_DUST, 1 + abs(x - sx - 2) + abs(y - sy - 2));
}

static int tfa_bork_unshadowed(const TfaNpc* m, int smorks_dead, int sx, int sy, int x, int y) {
    return smorks_dead || (!tfa_bork_covers(m, x, y) && !tfa_bork_shadowed(sx, sy, m, x, y));
}

/* Gong wave tiles: the arena within the Chebyshev reach of the target from Bork's centre (target on the rim in 11/11 hit
   gongs, which makes the target the second player, inferred), minus the footprint and the Smorks and its shadow. */
static int tfa_bork_wave_covers(const TfaNpc* m, int smorks_dead, int sx, int sy, int reach2, int x, int y) {
    int ox2 = 2 * x + 1 - (2 * sx + 4), oy2 = 2 * y + 1 - (2 * sy + 4);
    if (!tfa_bork_walkable(x, y) || abs(ox2) > reach2 || abs(oy2) > reach2) return 0;
    if (x >= sx && x < sx + 4 && y >= sy && y < sy + 4) return 0;
    return tfa_bork_unshadowed(m, smorks_dead, sx, sy, x, y);
}

/* Wide wave around Bork's start-of-tick centre. */
static void tfa_bork_gong(TfaState* s, TfaNpc* b, TfaNpc* m) {
    TfaBorkState* bs = &s->boss.bork;
    b->act = TFA_ACT_MELEE;
    tfa_npc_anim(s, b, BORK_ANIM_GONG);
    int sx = bs->start_x, sy = bs->start_y;
    const Player* tp = &s->players[bs->tank].player;
    int rx2 = abs(2 * tp->x + 1 - (2 * sx + 4)), ry2 = abs(2 * tp->y + 1 - (2 * sy + 4));
    int reach2 = rx2 > ry2 ? rx2 : ry2;
    for (int x = TFA_BORK.arena_x0; x <= TFA_BORK.arena_x1; x++)
        for (int y = TFA_BORK.arena_y0; y <= TFA_BORK.arena_y1; y++)
            if (tfa_bork_wave_covers(m, bs->smorks_dead, sx, sy, reach2, x, y)) tfa_bork_dust(s, sx, sy, x, y);
    for (int p = 0; p < s->team_size; p++) {
        if (!tfa_alive(s, p)) continue;
        if (tfa_bork_wave_covers(m, bs->smorks_dead, sx, sy, reach2, s->players[p].player.x, s->players[p].player.y))
            tfa_bork_hit_player(s, p, TFA_BORK.wide_min_damage, TFA_BORK.wide_max_damage, TFA_BORK.protected_pct);
    }
    tfa_bork_pend(bs, s->tick + TFA_BORK.ring_delay, TFA_BORK_PEND_RING, 0, 0, 0);
}

/* Ring around the footprint at the start of the gong +2 tick: every walkable tile but the Smorks footprint, shadow ignored (18/18 exact
   against the position one tick after the gong). */
static int tfa_bork_ring_covers(const TfaNpc* m, int smorks_dead, int sx, int sy, int x, int y) {
    return tfa_bork_walkable(x, y) && x >= sx - 1 && x <= sx + 4 && y >= sy - 1 && y <= sy + 4 &&
        (x < sx || x >= sx + 4 || y < sy || y >= sy + 4) && (smorks_dead || !tfa_bork_covers(m, x, y));
}

static void tfa_bork_ring(TfaState* s, const TfaNpc* b, const TfaNpc* m) {
    int sx = b->x, sy = b->y, dead = s->boss.bork.smorks_dead;
    for (int x = sx - 1; x <= sx + 4; x++)
        for (int y = sy - 1; y <= sy + 4; y++)
            if (tfa_bork_ring_covers(m, dead, sx, sy, x, y)) tfa_bork_dust(s, sx, sy, x, y);
    for (int p = 0; p < s->team_size; p++) {
        if (tfa_alive(s, p) && tfa_bork_ring_covers(m, dead, sx, sy, s->players[p].player.x, s->players[p].player.y))
            tfa_bork_hit_player(s, p, TFA_BORK.ring_min_damage, TFA_BORK.ring_max_damage, TFA_BORK.protected_pct);
    }
}

/* Facing toward the tank in 8 directions, JAU order (0 south, 2 west, 4 north, 6 east): Bork's recorded orientation at the stomp is the
   direction to the tank (1253 vs 1253, 1864 vs 1863, 1412 vs 1462). */
static int tfa_bork_facing(const TfaNpc* b, const Player* tank) {
    double dx = tank->x + 0.5 - (b->x + b->size * 0.5), dy = tank->y + 0.5 - (b->y + b->size * 0.5);
    double jau = atan2(-dx, -dy) * 1024.0 / M_PI;
    if (jau < 0) jau += 2048.0;
    return ((int)(jau + 128.0) / 256) & 7;
}

/* Stomp pulse over the whole walkable arena but the Smorks and its shadow (190-199 tiles, 20/20 pulses). */
static void tfa_bork_stomp_pulse(TfaState* s, const TfaNpc* b, const TfaNpc* m, int pulse) {
    const TfaBorkState* bs = &s->boss.bork;
    int dead = bs->smorks_dead;
    const int* src = TFA_BORK.stomp_feet[bs->stomp_facing][(bs->stomp_foot + pulse) & 1];
    for (int x = TFA_BORK.arena_x0; x <= TFA_BORK.arena_x1; x++)
        for (int y = TFA_BORK.arena_y0; y <= TFA_BORK.arena_y1; y++)
            if (tfa_bork_walkable(x, y) && tfa_bork_unshadowed(m, dead, b->x, b->y, x, y))
                tfa_fx_ground(s, x, y, BORK_GFX_DUST, 1 + abs(x - b->x - src[0]) + abs(y - b->y - src[1]));
    for (int p = 0; p < s->team_size; p++) {
        if (!tfa_alive(s, p)) continue;
        int x = s->players[p].player.x, y = s->players[p].player.y;
        if (tfa_bork_walkable(x, y) && tfa_bork_unshadowed(m, dead, b->x, b->y, x, y))
            tfa_bork_hit_player(s, p, TFA_BORK.stomp_min_damage, TFA_BORK.stomp_max_damage, TFA_BORK.protected_pct);
    }
}

static void tfa_bork_scorch(TfaState* s, int x, int y, int orientation) {
    tfa_hazard_add(s, (TfaHazard){
        .kind = TFA_HAZ_FLOOR,
        .style = ATTACK_STYLE_NONE,
        .w = 1, .h = 1,
        .owner = -1,
        .effect = TFA_EFFECT_BURN,
        .x = (int16_t)x, .y = (int16_t)y,
        .start_tick = s->tick,
        .end_tick = s->tick + TFA_BORK.scorch_life,
        .min_damage = (int16_t)TFA_BORK.scorch_min,
        .max_damage = (int16_t)TFA_BORK.scorch_max,
        .protected_pct = 100,
        .loc_id = BORK_OBJ_SCORCH,
        .orientation = (int16_t)orientation,
    });
}

static int tfa_bork_scorched(const TfaState* s, int x, int y) {
    for (int i = 0; i < s->hazard_hi; i++)
        if (s->hazards[i].kind == TFA_HAZ_FLOOR && s->hazards[i].loc_id == BORK_OBJ_SCORCH && s->hazards[i].x == x && s->hazards[i].y == y) return 1;
    return 0;
}

/* Fire and a splat variant per scorch tile at the landing (4046 plus 4047 + k, 33/33), the scorch object one tick later turned
   by the splat's rotation 512 * k (33/33). The tiles are the impact tile plus distinct random ones from the window
   (napalm dx -3..1, dy -2..3 over 3 clusters, bomb dx -1..1, dy -1..1 over 2), filtered to free walkable floor. The bomb impact
   tile carries a second 4046 (2/2). Fire delay 1 + step per Chebyshev tile from the impact (napalm 3, 2/2, bomb 0). */
static void tfa_bork_scorch_cluster(TfaState* s, int tx, int ty, int count, int dx0, int dx1, int dy0, int dy1, int delay_step) {
    int tiles[64][2], n = 0;
    if (tfa_bork_walkable(tx, ty) && !tfa_bork_scorched(s, tx, ty)) {
        tiles[n][0] = tx;
        tiles[n][1] = ty;
        n++;
    }
    int first = n;
    for (int dx = dx0; dx <= dx1; dx++)
        for (int dy = dy0; dy <= dy1; dy++)
            if ((dx != 0 || dy != 0) && tfa_bork_walkable(tx + dx, ty + dy) && !tfa_bork_scorched(s, tx + dx, ty + dy)) {
                tiles[n][0] = tx + dx;
                tiles[n][1] = ty + dy;
                n++;
            }
    int total = count < n ? count : n;
    for (int k = first; k < total; k++) {
        int j = k + tfa_rand(s, n - k);
        int x = tiles[j][0], y = tiles[j][1];
        tiles[j][0] = tiles[k][0];
        tiles[j][1] = tiles[k][1];
        tiles[k][0] = x;
        tiles[k][1] = y;
    }
    for (int p = 0; p < s->team_size; p++) {
        const Player* pl = &s->players[p].player;
        int on_fire = 0;
        for (int k = 0; k < total && !on_fire; k++) on_fire = pl->x == tiles[k][0] && pl->y == tiles[k][1];
        if (tfa_alive(s, p) && on_fire)
            tfa_damage_player_now(s, p, tfa_rand_range(s, TFA_BORK.blast_min_damage, TFA_BORK.blast_max_damage), ATTACK_STYLE_NONE);
    }
    for (int k = 0; k < total; k++) {
        int x = tiles[k][0], y = tiles[k][1];
        int cheb = abs(x - tx) > abs(y - ty) ? abs(x - tx) : abs(y - ty);
        int delay = 1 + delay_step * cheb;
        int variant = tfa_rand(s, BORK_SPLAT_VARIANTS);
        tfa_fx_ground(s, x, y, BORK_GFX_FIRE, delay);
        if (delay_step == 0 && cheb == 0) tfa_fx_ground(s, x, y, BORK_GFX_FIRE, delay);
        tfa_fx_ground(s, x, y, BORK_GFX_SPLAT + variant, delay);
        tfa_bork_pend(&s->boss.bork, s->tick + TFA_BORK.napalm_scorch_delay, TFA_BORK_PEND_SCORCH, x, y, 512 * variant);
    }
}

static void tfa_bork_landing(TfaState* s, TfaNpc* b, int tx, int ty, int bork_damage) {
    TfaBorkState* bs = &s->boss.bork;
    if (!bs->bork_dead && tfa_bork_covers(b, tx, ty)) tfa_npc_take_damage(s, b, bork_damage, -1, ATTACK_STYLE_NONE);
}

/* One volley of one style at every living raider but Bork's tank ([wiki] Talk:Bork, 4 of 5 targets in every bork_02 volley), at everyone
   once Bork is dead or when the tank stands alone. */
static void tfa_bork_volley(TfaState* s, TfaNpc* m) {
    TfaBorkState* bs = &s->boss.bork;
    int ranged = bs->bork_dead ? 1 : TFA_BORK.ranged_weight, magic_w = bs->bork_dead ? 1 : TFA_BORK.magic_weight;
    int magic = tfa_rand(s, ranged + magic_w) >= ranged;
    AttackStyle style = magic ? ATTACK_STYLE_MAGIC : ATTACK_STYLE_RANGED;
    int max_hit = magic ? TFA_BORK.magic_max_hit : TFA_BORK.ranged_max_hit;
    if (bs->bork_dead) max_hit = max_hit * TFA_BORK.enraged_smork_pct / 100;
    int flight = TFA_BORK.smork_flight[bs->bork_dead ? 2 : bs->pace];
    int spare_tank = !bs->bork_dead && tfa_alive_count(s) > 1;
    tfa_npc_anim(s, m, BORK_ANIM_CANNON);
    m->act = magic ? TFA_ACT_MAGIC : TFA_ACT_RANGED;
    for (int p = 0; p < s->team_size; p++) {
        if (!tfa_alive(s, p) || (spare_tank && p == bs->tank)) continue;
        m->target = (int8_t)p;
        tfa_queue_npc_hit_land(s, p, tfa_npc_roll(s, m, p, style, max_hit), style, flight, 0, m);
        if (magic)
            tfa_fx_projectile(s, m, p, BORK_PROJ_MAGIC, SPOTANIM_TFA_BORK_VFX_SMORK_MAGE_ATTACK_PROJANIM01,
                SPOTANIM_TFA_BORK_VFX_SMORK_MAGE_ATTACK_PROJANIM01_ANIM, 0, flight);
        else
            tfa_fx_projectile(s, m, p, BORK_PROJ_RANGED, SPOTANIM_TFA_BORK_VFX_SMORK_RANGE_ATTACK_PROJANIM01,
                SPOTANIM_TFA_BORK_VFX_SMORK_RANGE_ATTACK_PROJANIM01_ANIM, 0, flight);
    }
}

/* Napalm on a random living raider's tile, never the tank while Bork lives (P1, P3, P3, P0 in bork_02, never the tank). */
static void tfa_bork_napalm(TfaState* s, TfaNpc* m) {
    TfaBorkState* bs = &s->boss.bork;
    int p = bs->bork_dead ? tfa_bork_pick(s, -1, -1) : tfa_bork_pick(s, bs->tank, bs->tank);
    const Player* tp = &s->players[p].player;
    tfa_npc_anim(s, m, BORK_ANIM_CANNON);
    m->act = TFA_ACT_RANGED;
    m->target = (int8_t)p;
    tfa_fx_projectile(s, m, p, BORK_PROJ_NAPALM, SPOTANIM_TFA_BORK_VFX_SMORK_NAPALM_FIRE_PROJANIM01,
        SPOTANIM_TFA_BORK_VFX_SMORK_NAPALM_FIRE_PROJANIM01_ANIM, 0, TFA_BORK.napalm_flight);
    tfa_bork_pend(bs, s->tick + TFA_BORK.napalm_flight, TFA_BORK_PEND_NAPALM_LAND, tp->x, tp->y, 0);
}

/* The bomb lands on the Smorks' target's tile at the anim (4/4 the raider the team stacks on, gap 2 from Bork). */
static void tfa_bork_bomb(TfaState* s, TfaNpc* m) {
    TfaBorkState* bs = &s->boss.bork;
    if (!tfa_alive(s, bs->smork_target)) bs->smork_target = tfa_bork_pick(s, bs->tank, bs->tank);
    int p = bs->smork_target;
    const Player* tp = &s->players[p].player;
    tfa_npc_anim(s, m, BORK_ANIM_BIG_CANNON);
    m->act = TFA_ACT_RANGED;
    m->target = (int8_t)p;
    tfa_fx_projectile(s, m, p, BORK_PROJ_BOMB, SPOTANIM_TFA_BORK_CANNONBALL_TRAVEL_BIG,
        SPOTANIM_TFA_BORK_CANNONBALL_TRAVEL_BIG_ANIM, TFA_BORK.bomb_proj_delay, TFA_BORK.bomb_flight);
    tfa_bork_pend(bs, s->tick + TFA_BORK.bomb_proj_delay + TFA_BORK.bomb_flight, TFA_BORK_PEND_BOMB_LAND, tp->x, tp->y, 0);
}

/* The Smorks' cycle: autos, napalm, 1-2 autos, bomb. Auto to auto is Bork's gong interval, auto to napalm 1 more, auto to bomb 2 more.
   The autos before the napalm are counted at the first auto after the bomb, with the pace of that tick. Bork dead: bursts between napalms. */
static void tfa_bork_smork_move(TfaState* s, TfaNpc* m) {
    TfaBorkState* bs = &s->boss.bork;
    int v = TFA_BORK.gong_interval[bs->pace];
    if (bs->bork_dead) {
        if (bs->smork_move == TFA_BORK_SMORK_NAPALM) {
            tfa_bork_napalm(s, m);
            bs->smork_move = TFA_BORK_SMORK_AUTO;
            bs->autos_left = TFA_BORK.enraged_burst;
            bs->next_smork = s->tick + TFA_BORK.enraged_to_napalm;
            return;
        }
        tfa_bork_volley(s, m);
        if (--bs->autos_left > 0) bs->next_smork = s->tick + TFA_BORK.enraged_gap;
        else {
            bs->smork_move = TFA_BORK_SMORK_NAPALM;
            bs->next_smork = s->tick + TFA_BORK.enraged_to_napalm;
        }
        return;
    }
    switch (bs->smork_move) {
        case TFA_BORK_SMORK_AUTO:
            if (bs->autos_left == 0) bs->autos_left = TFA_BORK.smork_autos_before[bs->pace];
            tfa_bork_volley(s, m);
            if (--bs->autos_left > 0) bs->next_smork = s->tick + v;
            else {
                bs->smork_move = bs->after_autos;
                bs->next_smork = s->tick + v + (bs->after_autos == TFA_BORK_SMORK_NAPALM ? 1 : 2);
            }
            break;
        case TFA_BORK_SMORK_NAPALM:
            tfa_bork_napalm(s, m);
            bs->smork_move = TFA_BORK_SMORK_AUTO;
            bs->autos_left = tfa_rand_range(s, TFA_BORK.smork_autos_after_min, TFA_BORK.smork_autos_after_max);
            bs->after_autos = TFA_BORK_SMORK_BOMB;
            bs->next_smork = s->tick + TFA_BORK.smork_napalm_recover[bs->pace];
            break;
        case TFA_BORK_SMORK_BOMB:
            tfa_bork_bomb(s, m);
            bs->smork_move = TFA_BORK_SMORK_AUTO;
            bs->autos_left = 0;
            bs->after_autos = TFA_BORK_SMORK_NAPALM;
            bs->next_smork = s->tick + TFA_BORK.smork_after_miss;
            break;
    }
}

/* Bork's stomp chain from the bomb landing: on him a 14-tick stun, 14549, stomp 2 later; a miss stomps 5 later. Gongs pause from the
   stun or 3 before the stomp and resume 17 (stun) or 15 (miss) after the stomp, Bork walking again 1 before that. */
static void tfa_bork_schedule_stomp(TfaBorkState* bs, int stomp_tick, int freeze_from, int resume) {
    bs->stomp_tick = stomp_tick;
    bs->freeze_from = freeze_from;
    bs->resume_tick = stomp_tick + resume;
}

static void tfa_bork_pending_tick(TfaState* s, TfaNpc* b, TfaNpc* m) {
    TfaBorkState* bs = &s->boss.bork;
    for (int i = 0; i < bs->pending_count; i++) {
        TfaBorkPending e = bs->pending[i];
        if (e.tick != s->tick) continue;
        bs->pending[i--] = bs->pending[--bs->pending_count];
        switch ((TfaBorkPendKind)e.kind) {
            case TFA_BORK_PEND_NAPALM_LAND:
                tfa_bork_landing(s, b, e.x, e.y, TFA_BORK.napalm_bork_damage);
                tfa_bork_scorch_cluster(s, e.x, e.y, tfa_rand_range(s, TFA_BORK.napalm_tiles_min, TFA_BORK.napalm_tiles_max), -3, 1, -2, 3, TFA_BORK.napalm_delay_step);
                break;
            case TFA_BORK_PEND_BOMB_LAND:
                tfa_bork_landing(s, b, e.x, e.y, TFA_BORK.bomb_bork_damage);
                tfa_bork_scorch_cluster(s, e.x, e.y, tfa_rand_range(s, TFA_BORK.bomb_tiles_min, TFA_BORK.bomb_tiles_max), -1, 1, -1, 1, 0);
                if (bs->bork_dead) break;
                if (tfa_bork_covers(b, e.x, e.y)) {
                    bs->stunned = 1;
                    b->act = TFA_ACT_STUNNED;
                    tfa_npc_anim(s, b, BORK_ANIM_STUN);
                    bs->angry_tick = s->tick + TFA_BORK.stun_angry;
                    tfa_bork_schedule_stomp(bs, bs->angry_tick + TFA_BORK.angry_stomp, s->tick, TFA_BORK.resume_stun);
                    bs->next_smork = bs->resume_tick + TFA_BORK.gong_interval[bs->pace];
                } else {
                    tfa_bork_schedule_stomp(bs, s->tick + TFA_BORK.miss_stomp, s->tick + TFA_BORK.miss_stomp - TFA_BORK.miss_freeze, TFA_BORK.resume_miss);
                }
                break;
            case TFA_BORK_PEND_SCORCH:
                tfa_bork_scorch(s, e.x, e.y, e.arg);
                break;
            case TFA_BORK_PEND_RING:
                /* a stun or the stomp freeze cancels the ring (3/3 missing rings: stun at gong +1 twice, freeze from gong +2) */
                if (!bs->bork_dead && !bs->stunned && !(bs->freeze_from >= 0 && s->tick >= bs->freeze_from)) tfa_bork_ring(s, b, m);
                break;
            case TFA_BORK_PEND_PULSE:
                tfa_bork_stomp_pulse(s, b, m, e.arg);
                break;
        }
    }
}

/* One cardinal step toward the tank along the larger axis, the other axis when blocked, else stay; none while the tank is next to him
   (bork_02: Bork stands still beside the tank and walks only when the tank steps away). */
static void tfa_bork_chase(TfaState* s, TfaNpc* b, const TfaNpc* m) {
    const Player* pl = &s->players[s->boss.bork.tank].player;
    if (encounter_rect_distance(pl->x, pl->y, 1, b->x, b->y, b->size) <= 1) return;
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
    if (n->kind == TFA_NPC_BORK) {
        bs->bork_dead = 1;
        bs->stunned = 0;
        bs->smork_move = TFA_BORK_SMORK_AUTO;
        bs->autos_left = 2;
        bs->next_smork = s->tick + TFA_BORK.enraged_first;
    } else {
        bs->smorks_dead = 1;
        bs->next_bomb = s->tick + tfa_rand_range(s, TFA_BORK.bomb_gap_min, TFA_BORK.bomb_gap_max);
    }
}

static void tfa_bork_tick(TfaState* s, TfaContext* ctx) {
    TfaBorkState* bs = &s->boss.bork;
    if (bs->bork_dead && bs->smorks_dead) {
        tfa_room_cleared(s, ctx);
        return;
    }
    TfaNpc* b = tfa_bork_npc(s);
    TfaNpc* m = tfa_bork_smorks(s);
    /* A stunned Bork has no defence: other raiders' hits were 0 on 5 of 76 during the stun against 83 of 154 outside it (bork_03, after the hotfix). */
    b->act = bs->stunned ? TFA_ACT_STUNNED : TFA_ACT_IDLE;
    m->act = TFA_ACT_IDLE;
    bs->clock++;
    bs->start_x = b->x;
    bs->start_y = b->y;
    if (tfa_alive_count(s) == 0) return;
    if (!tfa_alive(s, bs->tank)) bs->tank = tfa_bork_pick(s, bs->smork_target, bs->smork_target);
    if (!tfa_alive(s, bs->smork_target)) bs->smork_target = tfa_bork_pick(s, bs->tank, bs->tank);
    tfa_bork_pending_tick(s, b, m);

    for (int p = 0; p < s->team_size; p++) {
        TfaPlayer* tp = &s->players[p];
        if (tfa_alive(s, p) && tp->burn_ticks % 4 == 2)
            tfa_damage_player_now(s, p, tfa_rand_range(s, TFA_BORK.burn_min, TFA_BORK.burn_max), ATTACK_STYLE_NONE);
    }

    if (!bs->bork_dead) {
        int paused = bs->freeze_from >= 0 && s->tick >= bs->freeze_from && s->tick < bs->resume_tick;
        if (!(paused && s->tick < bs->resume_tick - 1)) tfa_bork_chase(s, b, m);
        if (s->tick == bs->angry_tick) {
            bs->stunned = 0;
            tfa_npc_anim(s, b, BORK_ANIM_ANGRY);
        }
        if (s->tick == bs->stomp_tick) {
            b->act = TFA_ACT_SPECIAL;
            tfa_npc_anim(s, b, BORK_ANIM_STOMP);
            bs->stomp_facing = tfa_bork_facing(b, &s->players[bs->tank].player);
            bs->stomp_foot = tfa_rand(s, 2);
            for (int k = 0; k < TFA_BORK.stomp_pulses; k++)
                tfa_bork_pend(bs, s->tick + TFA_BORK.stomp_pulse_first + k * TFA_BORK.stomp_pulse_gap, TFA_BORK_PEND_PULSE, 0, 0, k);
        }
        if (bs->stomp_tick >= 0 && s->tick == bs->stomp_tick + TFA_BORK.stomp_end_anim) tfa_npc_anim(s, b, BORK_ANIM_STOMP_END);
        if (bs->stomp_tick >= 0 && s->tick == bs->stomp_tick + TFA_BORK.retarget) bs->tank = tfa_bork_pick(s, bs->smork_target, bs->tank);
        if (s->tick == bs->resume_tick) bs->next_gong = s->tick;
        if (bs->resume_tick >= 0 && s->tick == bs->resume_tick + 1) {
            if (!bs->pace && b->hp * 100 <= TFA_BORK.pace_hp_pct * b->max_hp) bs->pace = 1;
            bs->stomp_tick = bs->angry_tick = bs->freeze_from = bs->resume_tick = -1;
        }
        if (bs->smorks_dead && bs->resume_tick < 0 && s->tick >= bs->next_bomb) {
            int stomp = s->tick + TFA_BORK.smorks_dead_stomp;
            tfa_bork_schedule_stomp(bs, stomp, stomp - TFA_BORK.miss_freeze, TFA_BORK.resume_miss);
            bs->next_bomb = s->tick + tfa_rand_range(s, TFA_BORK.bomb_gap_min, TFA_BORK.bomb_gap_max);
        }
        if (s->tick == bs->next_gong) {
            if (!paused) tfa_bork_gong(s, b, m);
            bs->next_gong += TFA_BORK.gong_interval[bs->pace];
        }
        b->attack_timer = (int16_t)(bs->next_gong > s->tick ? bs->next_gong - s->tick : 0);
    }

    if (!bs->smorks_dead) {
        if (s->tick == bs->next_smork) tfa_bork_smork_move(s, m);
        m->attack_timer = (int16_t)(bs->next_smork > s->tick ? bs->next_smork - s->tick : 0);
    }
}
