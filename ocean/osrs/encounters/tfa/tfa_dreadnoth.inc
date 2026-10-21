/* Dreadnoth the Jellied. HP driven, measured from 14 recorded fights (fixtures/tfa_dreadnoth_events.tsv):
   slam sequences at 85, 65 and 45 percent HP (1, 2, 3 slams) each ending in a jelly wave, then a jump chain from 26 percent. */

#define DREAD_NPC 16665
#define DREAD_NPC_JELLY 16666

#define DREAD_ANIM_BASIC 14779
#define DREAD_ANIM_SLAM 14777
#define DREAD_ANIM_JUMP 14780
#define DREAD_ANIM_DEATH 14782
#define DREAD_ANIM_JELLY_SPAWN 15162
#define DREAD_ANIM_JELLY_DEATH 1587
#define DREAD_LOC_BLOOD_TILE 62609
#define DREAD_GFX_JUMP 4209
#define DREAD_GFX_MARK 4210
#define DREAD_PROJ_VOLLEY 4211
#define DREAD_PROJ_GLOB 4212
#define DREAD_PROJ_LAUNCH 2

#define DREAD_ARENA_LO 24
#define DREAD_ARENA_HI 38
#define DREAD_ANCHOR_HI 34

typedef struct {
    int hp_base;                 /* wiki 2100 */
    int hp_trio;                 /* 3 players: damage in minus heals is 3050 in 4 of 4 kills */
    int threshold_pct[3];        /* first slam 1 tick after NHP drops below 76/90, 58/90, 40/90 (14/14, 13/13, 9/9 within +-1) */
    int final_pct;               /* *glub glub* when HP falls under 26 percent: replay brackets it to (789, 795] of 3050 over 7 fights, first jump 5 ticks later */
    int slams_per_phase[3];      /* 1, 2, 3 slams: 14777 groups 6 apart */
    int slam_gap;                /* 14777 next 14777: 6 in 36 of 70, group-internal 6/6 */
    int slam_melee_delay;        /* melee-typed hit 1 tick after the anim on every player, any distance (1 of 1 landing prayer, +1 hits 128) */
    int slam_melee_max;          /* unprotected hits 22..44 (n=8 with Protect Missiles up) */
    int volley_delay[3];         /* three ranged shots per player landing at +4, +5, +6 */
    int volley_max;              /* unprotected 4..29, Protect from Missiles blocks 157 of 157 */
    int spawn_delay;             /* jellies emerge 8 ticks after the last slam in the group, 3 of 3 groups per fight */
    int spawn_tiles[3];          /* 9 tiles for one slam, 6 + 6 for two, 4 + 4 + 4 for three (team of 3, 14 fights) */
    int jelly_emerge;            /* first hit on a jelly 5 ticks after NPC+ (spawn anim 15162) */
    int jelly_attack_speed;      /* single-adjacent P0 hit gaps 3:16 of 44, provisional */
    int jelly_min_hit, jelly_max_hit; /* 5..10 unprotected, 0 on Protect from Melee (275 of 277) */
    int jelly_death_ticks;       /* death anim 1587 to NPC- 3 ticks */
    int final_jellies;           /* 7 jellies at the boss death tick + 3 (4 of 4 kills) */
    int death_spawn_delay;
    int death_remove_delay;      /* 14782 to NPC- 4 ticks */
    int walk_period;             /* move bursts start about 22 apart (20..24), 10 ticks after the last slam */
    int walk_window;             /* a burst is about 4 tiles, one tile per 2 ticks (504 of 604 steps) */
    int walk_post_slam;
    int basic_interval;          /* 14779 next 14779: 4 in 25 of 79 */
    int basic_max;               /* 32 unprotected, 29 on Protect from Melee */
    int basic_protected_pct;     /* 29 / 32 */
    int jump_interval;           /* 14780 next 14780: 8 in 45 of 49 */
    int jump_first_delay;        /* first jump 5 ticks after the glub overhead text (7/7) */
    int jump_land_delay;         /* every player takes a hit 3 ticks after the anim */
    int jump_land_inside_min, jump_land_inside_max; /* inside the 5x5 footprint: 28..34 through any prayer */
    int jump_global_max;         /* outside: melee-typed 19..49, blocked by Protect from Melee (21 of 22) */
    int jump_throw_delay;        /* volley shot and glob thrown at every player 4 ticks after the anim */
    int jump_volley_delay;       /* shot lands 2 ticks after the throw */
    int glob_delay;              /* glob lands 5 ticks after the throw on the thrown-at tile (4212 flight 5.5) */
    int glob_min, glob_max;      /* 16..22 on the tile (4 of 5), provisional */
    int jump_heal_pct;           /* boss heals 1.25..1.31 x the inside-footprint hit, 8 of 8 */
    int blood_min, blood_max;    /* standing on object 62609: 7 or 8 (n=19) */
    int blood_per_slam;          /* 3 objects one tick after a slam around the boss, provisional placement */
    int blood_slam_delay;
    int blood_per_jump;          /* 5 draws on the 7x7 around the landing 3 ticks after the anim: best of N=3..7 by likelihood over 52 jumps */
    int blood_land_delay;        /* new tiles 3 ticks after 14780 (174 tiles, 52 jumps) */
    int mark_per_tick;           /* 3 new markers per tick for 3 ticks (14 of 14 nine-tile groups 3,3,3, 28 of 75 slams 3,1) */
    int mark_seed_tick;          /* first round reads the player tile 3 ticks after the slam (39 of 39 P0 seeds), the markers show at +4 */
    int mark_ring1_weight;       /* derived tile: 186 within 1 of the seed, 79 at 2, so 5 per ring-1 tile against 1 per ring-2 tile (8 and 16 tiles) */
    int mark_first_delay_seed;   /* GFX 4210 delay of a seed's first tick */
    int mark_first_delay_derived;/* 265 derived tile starts a=31 */
} TfaDreadnothTuning;

static const TfaDreadnothTuning TFA_DREAD = {
    .hp_base = 2100,
    .hp_trio = 3050,
    .threshold_pct = { 85, 65, 45 },
    .final_pct = 26,
    .slams_per_phase = { 1, 2, 3 },
    .slam_gap = 6,
    .slam_melee_delay = 1,
    .slam_melee_max = 44,
    .volley_delay = { 4, 5, 6 },
    .volley_max = 29,
    .spawn_delay = 8,
    .spawn_tiles = { 9, 6, 4 },
    .jelly_emerge = 5,
    .jelly_attack_speed = 3,
    .jelly_min_hit = 5,
    .jelly_max_hit = 10,
    .jelly_death_ticks = 3,
    .final_jellies = 7,
    .death_spawn_delay = 3,
    .death_remove_delay = 4,
    .walk_period = 22,
    .walk_window = 8,
    .walk_post_slam = 10,
    .basic_interval = 4,
    .basic_max = 32,
    .basic_protected_pct = 91,
    .jump_interval = 8,
    .jump_first_delay = 5,
    .jump_land_delay = 3,
    .jump_land_inside_min = 28,
    .jump_land_inside_max = 34,
    .jump_global_max = 49,
    .jump_throw_delay = 4,
    .jump_volley_delay = 2,
    .glob_delay = 5,
    .glob_min = 16,
    .glob_max = 22,
    .jump_heal_pct = 127,
    .blood_min = 7,
    .blood_max = 8,
    .blood_per_slam = 3,
    .blood_slam_delay = 1,
    .blood_per_jump = 5,
    .blood_land_delay = 3,
    .mark_per_tick = 3,
    .mark_seed_tick = 3,
    .mark_ring1_weight = 5,
    .mark_first_delay_seed = 1,
    .mark_first_delay_derived = 31,
};

static const int8_t DREAD_NEIGHBOUR[8][2] = { {-1,-1}, {0,-1}, {1,-1}, {-1,0}, {1,0}, {-1,1}, {0,1}, {1,1} };

static int tfa_dread_sign(int v) {
    return (v > 0) - (v < 0);
}

static TfaNpc* tfa_dread_boss(TfaState* s) {
    TfaNpc* n = tfa_npc_first(s, TFA_NPC_DREADNOTH);
    assert(n);
    return n;
}

static void tfa_dread_enter(TfaState* s) {
    const TfaRoomLayout* l = tfa_room_layout(TFA_ROOM_DREADNOTH);
    TfaNpc* b = tfa_npc_spawn(s, TFA_NPC_DREADNOTH, MON_TFA_DREADNOTH, DREAD_NPC, l->boss_x, l->boss_y);
    b->hp = b->max_hp = tfa_party_hp(s, TFA_DREAD.hp_base, TFA_DREAD.hp_trio);
    s->boss.dreadnoth = (TfaDreadnothState){ .slam_start = -1, .spawn_tick = -1, .next_jump = -1 };
}

static void tfa_dread_schedule(TfaState* s, int tick, TfaDreadEventKind kind, int player, int x, int y) {
    TfaDreadEvent* ev = s->boss.dreadnoth.events;
    for (int i = 0; i < TFA_DREAD_MAX_EVENTS; i++) {
        if (ev[i].kind != TFA_DREAD_EV_NONE) continue;
        ev[i] = (TfaDreadEvent){ .tick = tick, .kind = (int8_t)kind, .player = (int8_t)player, .x = (int16_t)x, .y = (int16_t)y };
        return;
    }
    fprintf(stderr, "tfa dreadnoth event queue full\n");
    abort();
}

static int tfa_dread_blood_at(const TfaState* s, int x, int y) {
    for (int i = 0; i < TFA_MAX_HAZARDS; i++) {
        const TfaHazard* h = &s->hazards[i];
        if (h->kind == TFA_HAZ_FLOOR && h->loc_id == DREAD_LOC_BLOOD_TILE && h->x == x && h->y == y) return 1;
    }
    return 0;
}

static int tfa_dread_tile_free(const TfaState* s, int x, int y) {
    return x >= DREAD_ARENA_LO && x <= DREAD_ARENA_HI && y >= DREAD_ARENA_LO && y <= DREAD_ARENA_HI
        && !tfa_layout_blocked(tfa_room_layout(TFA_ROOM_DREADNOTH), x, y);
}

static void tfa_dread_blood(TfaState* s, int x, int y) {
    if (!tfa_dread_tile_free(s, x, y) || tfa_dread_blood_at(s, x, y)) return;
    tfa_hazard_add(s, (TfaHazard){
        .kind = TFA_HAZ_FLOOR,
        .style = ATTACK_STYLE_NONE,
        .w = 1, .h = 1,
        .owner = -1,
        .x = (int16_t)x, .y = (int16_t)y,
        .start_tick = s->tick + 1,
        .end_tick = INT32_MAX,
        .min_damage = (int16_t)TFA_DREAD.blood_min,
        .max_damage = (int16_t)TFA_DREAD.blood_max,
        .protected_pct = 100,
        .loc_id = DREAD_LOC_BLOOD_TILE,
        .orientation = (int16_t)(512 * tfa_rand(s, 4)),
    });
}

static int tfa_dread_random_alive(TfaState* s) {
    int c[TFA_MAX_PLAYERS], n = 0;
    for (int p = 0; p < s->team_size; p++) if (tfa_alive(s, p)) c[n++] = p;
    assert(n > 0);
    return c[tfa_rand(s, n)];
}

static int tfa_dread_nearest_alive(const TfaState* s, int x, int y) {
    int best = -1, best_d = 1 << 30;
    for (int p = 0; p < s->team_size; p++) {
        if (!tfa_alive(s, p)) continue;
        int d = chebyshev_distance(x, y, s->players[p].player.x, s->players[p].player.y);
        if (d < best_d) { best_d = d; best = p; }
    }
    return best;
}

static int tfa_dread_adjacent(const TfaNpc* b, const Player* pl) {
    return encounter_rect_distance(pl->x, pl->y, 1, b->x, b->y, b->size) <= 1;
}

static void tfa_dread_step_toward(TfaNpc* b, int px, int py) {
    int nx = b->x + tfa_dread_sign(px - (b->x + 2));
    int ny = b->y + tfa_dread_sign(py - (b->y + 2));
    b->x = (int16_t)(nx < DREAD_ARENA_LO ? DREAD_ARENA_LO : nx > DREAD_ANCHOR_HI ? DREAD_ANCHOR_HI : nx);
    b->y = (int16_t)(ny < DREAD_ARENA_LO ? DREAD_ARENA_LO : ny > DREAD_ANCHOR_HI ? DREAD_ANCHOR_HI : ny);
}

static TfaNpc* tfa_dread_jelly_spawn(TfaState* s, int x, int y) {
    int free_slot = 0;
    for (int i = 0; i < TFA_NUM_TARGET_SLOTS; i++) free_slot |= !s->npcs[i].active;
    TfaNpc* j;
    if (free_slot) {
        j = tfa_npc_spawn(s, TFA_NPC_JELLY, MON_TFA_JELLY, DREAD_NPC_JELLY, x, y);
    } else {
        j = tfa_npc_alloc(s, TFA_NPC_JELLY, DREAD_NPC_JELLY, 1, x, y, 0);
        j->monster = MON_TFA_JELLY;
        j->hp = j->max_hp = MONSTER_DATABASE[MON_TFA_JELLY].hp;
    }
    j->attackable = 0;
    j->state_ticks = (int16_t)TFA_DREAD.jelly_emerge;
    j->attack_timer = (int16_t)TFA_DREAD.jelly_attack_speed;
    tfa_npc_anim(s, j, DREAD_ANIM_JELLY_SPAWN);
    return j;
}

/* Jellies that spawned past the target slots wait inert and move into a slot when one frees. */
static void tfa_dread_promote(TfaState* s) {
    for (int i = TFA_NUM_TARGET_SLOTS; i < TFA_MAX_NPCS; i++) {
        TfaNpc* j = &s->npcs[i];
        if (!j->active || j->kind != TFA_NPC_JELLY) continue;
        for (int k = 0; k < TFA_NUM_TARGET_SLOTS; k++) {
            if (s->npcs[k].active) continue;
            s->npcs[k] = *j;
            memset(j, 0, sizeof(*j));
            break;
        }
    }
}

static int tfa_dread_jelly_count(const TfaState* s) {
    int n = 0;
    for (int i = 0; i < TFA_MAX_NPCS; i++) n += s->npcs[i].active && s->npcs[i].kind == TFA_NPC_JELLY;
    return n;
}

static void tfa_dread_jelly_act(TfaState* s, TfaNpc* j) {
    if (j->counter > 0) {
        if (--j->counter == 0) j->active = 0;
        return;
    }
    if (j->state_ticks > 0) {
        if (--j->state_ticks == 0 && j - s->npcs < TFA_NUM_TARGET_SLOTS) j->attackable = 1;
        return;
    }
    if (!j->attackable && j - s->npcs < TFA_NUM_TARGET_SLOTS) j->attackable = 1;
    if (j->attack_timer > 0) j->attack_timer--;
    int p = tfa_dread_nearest_alive(s, j->x, j->y);
    j->act = TFA_ACT_IDLE;
    if (p < 0) return;
    const Player* pl = &s->players[p].player;
    j->target = (int8_t)p;
    if (encounter_entity_footprint_cardinal_adjacent(pl->x, pl->y, 1, j->x, j->y, 1)) {
        if (j->attack_timer > 0) return;
        j->act = TFA_ACT_MELEE;
        j->attack_timer = (int16_t)TFA_DREAD.jelly_attack_speed;
        tfa_queue_npc_hit(s, p, tfa_rand_range(s, TFA_DREAD.jelly_min_hit, TFA_DREAD.jelly_max_hit), ATTACK_STYLE_MELEE, 0, 0, j);
        return;
    }
    int dx = tfa_dread_sign(pl->x - j->x), dy = tfa_dread_sign(pl->y - j->y);
    if (dx && dy && chebyshev_distance(j->x, j->y, pl->x, pl->y) == 1) dy = 0;
    if (!tfa_layout_blocked(tfa_room_layout(TFA_ROOM_DREADNOTH), j->x + dx, j->y + dy)) {
        j->x = (int16_t)(j->x + dx);
        j->y = (int16_t)(j->y + dy);
    } else if (dx && !tfa_layout_blocked(tfa_room_layout(TFA_ROOM_DREADNOTH), j->x + dx, j->y)) {
        j->x = (int16_t)(j->x + dx);
    } else if (dy && !tfa_layout_blocked(tfa_room_layout(TFA_ROOM_DREADNOTH), j->x, j->y + dy)) {
        j->y = (int16_t)(j->y + dy);
    }
}

/* One round of spawn markers (GFX 4210, 39 of 39 emergences equal the marker set at the last tick before). Round r runs mark_seed_tick + r
   ticks after the slam and takes picks tiles, one per living player in turn: the player's own tile when unmarked (157 starts with
   delay 1, 127 of them at +4), else a tile within 2 of the player's (265 starts with delay 31). The tile first shows next tick. */
static void tfa_dread_mark_round(TfaState* s, int picks) {
    TfaDreadnothState* d = &s->boss.dreadnoth;
    int alive[TFA_MAX_PLAYERS], n_alive = 0;
    for (int p = 0; p < s->team_size; p++) if (tfa_alive(s, p)) alive[n_alive++] = p;
    assert(n_alive > 0);
    for (int k = 0; k < picks; k++) {
        const Player* pl = &s->players[alive[k % n_alive]].player;
        int x = pl->x, y = pl->y, seed = 1;
        for (int i = 0; i < d->spawn_n && seed; i++) seed = d->spawn_x[i] != x || d->spawn_y[i] != y;
        if (!seed) {
            int cx[225], cy[225], cw[225], total = 0, n = 0;
            for (int reach = 2; n == 0; reach++) {
                for (int dx = -reach; dx <= reach; dx++) {
                    for (int dy = -reach; dy <= reach; dy++) {
                        int tx = pl->x + dx, ty = pl->y + dy, dup = !tfa_dread_tile_free(s, tx, ty);
                        for (int i = 0; i < d->spawn_n && !dup; i++) dup = d->spawn_x[i] == tx && d->spawn_y[i] == ty;
                        if (dup) continue;
                        cx[n] = tx; cy[n] = ty;
                        cw[n] = abs(dx) <= 1 && abs(dy) <= 1 ? TFA_DREAD.mark_ring1_weight : 1;
                        total += cw[n++];
                    }
                }
            }
            int pick = tfa_rand(s, total), i = 0;
            while (pick >= cw[i]) pick -= cw[i++];
            x = cx[i]; y = cy[i];
        }
        assert(d->spawn_n < TFA_DREAD_MAX_SPAWN);
        d->spawn_x[d->spawn_n] = (int8_t)x;
        d->spawn_y[d->spawn_n] = (int8_t)y;
        d->mark_tick[d->spawn_n] = s->tick + 1;
        d->mark_delay[d->spawn_n++] = (int8_t)(seed ? TFA_DREAD.mark_first_delay_seed : TFA_DREAD.mark_first_delay_derived);
    }
}

static void tfa_dread_slam(TfaState* s, TfaNpc* b) {
    TfaDreadnothState* d = &s->boss.dreadnoth;
    b->act = TFA_ACT_SPECIAL;
    tfa_npc_anim(s, b, DREAD_ANIM_SLAM);
    for (int p = 0; p < s->team_size; p++) {
        if (!tfa_alive(s, p)) continue;
        tfa_queue_npc_hit_land(s, p, tfa_npc_roll(s, b, p, ATTACK_STYLE_MELEE, TFA_DREAD.slam_melee_max),
            ATTACK_STYLE_MELEE, TFA_DREAD.slam_melee_delay, 0, b);
        for (int k = 0; k < 3; k++) {
            tfa_queue_npc_hit_land(s, p, tfa_npc_roll(s, b, p, ATTACK_STYLE_RANGED, TFA_DREAD.volley_max),
                ATTACK_STYLE_RANGED, TFA_DREAD.volley_delay[k], 0, b);
            tfa_fx_projectile(s, b, p, DREAD_PROJ_VOLLEY, SPOTANIM_TFA_DREADNOTH_TFA_UNNAMED_4211,
                SPOTANIM_TFA_DREADNOTH_TFA_UNNAMED_4211_ANIM, DREAD_PROJ_LAUNCH, TFA_DREAD.volley_delay[k] - DREAD_PROJ_LAUNCH);
        }
    }
    int tiles = TFA_DREAD.spawn_tiles[d->phase_slams - 1];
    for (int r = 0; r * TFA_DREAD.mark_per_tick < tiles; r++)
        tfa_dread_schedule(s, s->tick + TFA_DREAD.mark_seed_tick + r, TFA_DREAD_EV_MARK, -1,
            tiles - r * TFA_DREAD.mark_per_tick < TFA_DREAD.mark_per_tick ? tiles - r * TFA_DREAD.mark_per_tick : TFA_DREAD.mark_per_tick, 0);
    for (int k = 0; k < TFA_DREAD.blood_per_slam; k++)
        tfa_dread_schedule(s, s->tick + TFA_DREAD.blood_slam_delay, TFA_DREAD_EV_BLOOD, -1,
            b->x - 1 + tfa_rand(s, b->size + 2), b->y - 1 + tfa_rand(s, b->size + 2));
    d->spawn_tick = s->tick + TFA_DREAD.spawn_delay;
}

static void tfa_dread_jump(TfaState* s, TfaNpc* b) {
    int target = tfa_dread_random_alive(s);
    const Player* tp = &s->players[target].player;
    int best_ax = b->x, best_ay = b->y, best_score = 1 << 30, best_n = 0;
    for (int dist = 5; dist >= 3 && best_n == 0; dist--) {
        for (int ax = DREAD_ARENA_LO; ax <= DREAD_ANCHOR_HI; ax++) {
            for (int ay = DREAD_ARENA_LO; ay <= DREAD_ANCHOR_HI; ay++) {
                if (chebyshev_distance(ax, ay, b->x, b->y) != dist) continue;
                int score = encounter_rect_distance(tp->x, tp->y, 1, ax, ay, b->size);
                if (score > best_score) continue;
                if (score < best_score) { best_score = score; best_n = 0; }
                if (tfa_rand(s, ++best_n) == 0) { best_ax = ax; best_ay = ay; }
            }
        }
    }
    b->x = (int16_t)best_ax;
    b->y = (int16_t)best_ay;
    b->act = TFA_ACT_SPECIAL;
    tfa_npc_anim(s, b, DREAD_ANIM_JUMP);
    tfa_fx_ground(s, b->x + b->size / 2, b->y + b->size / 2, DREAD_GFX_JUMP, 1);
    for (int k = 0; k < TFA_DREAD.blood_per_jump; k++)
        tfa_dread_schedule(s, s->tick + TFA_DREAD.blood_land_delay, TFA_DREAD_EV_BLOOD, -1,
            b->x - 1 + tfa_rand(s, b->size + 2), b->y - 1 + tfa_rand(s, b->size + 2));
    for (int p = 0; p < s->team_size; p++) {
        if (!tfa_alive(s, p)) continue;
        tfa_dread_schedule(s, s->tick + TFA_DREAD.jump_land_delay, TFA_DREAD_EV_JUMP_LAND, p, b->x, b->y);
        tfa_dread_schedule(s, s->tick + TFA_DREAD.jump_throw_delay, TFA_DREAD_EV_JUMP_THROW, p, 0, 0);
    }
}

static void tfa_dread_heal(TfaNpc* b, int damage, int pct) {
    int heal = damage * pct / 100;
    b->hp = b->hp + heal > b->max_hp ? b->max_hp : b->hp + heal;
}

static void tfa_dread_fire_event(TfaState* s, TfaNpc* b, const TfaDreadEvent* ev) {
    TfaDreadnothState* d = &s->boss.dreadnoth;
    int p = ev->player;
    switch ((TfaDreadEventKind)ev->kind) {
        case TFA_DREAD_EV_BLOOD:
            tfa_dread_blood(s, ev->x, ev->y);
            return;
        case TFA_DREAD_EV_MARK:
            tfa_dread_mark_round(s, ev->x);
            return;
        case TFA_DREAD_EV_JUMP_LAND: {
            if (!tfa_alive(s, p)) return;
            const Player* pl = &s->players[p].player;
            if (pl->x >= ev->x && pl->x < ev->x + b->size && pl->y >= ev->y && pl->y < ev->y + b->size) {
                int dmg = tfa_rand_range(s, TFA_DREAD.jump_land_inside_min, TFA_DREAD.jump_land_inside_max);
                tfa_damage_player_now(s, p, dmg, ATTACK_STYLE_NONE);
                if (!d->no_heal) tfa_dread_heal(b, dmg, TFA_DREAD.jump_heal_pct);
                return;
            }
            tfa_damage_player_now(s, p, tfa_protected_damage(
                tfa_npc_roll(s, b, p, ATTACK_STYLE_MELEE, TFA_DREAD.jump_global_max),
                s->players[p].player.prayer, ATTACK_STYLE_MELEE, 0), ATTACK_STYLE_MELEE);
            return;
        }
        case TFA_DREAD_EV_JUMP_THROW: {
            if (!tfa_alive(s, p)) return;
            tfa_queue_npc_hit_land(s, p, tfa_npc_roll(s, b, p, ATTACK_STYLE_RANGED, TFA_DREAD.volley_max),
                ATTACK_STYLE_RANGED, TFA_DREAD.jump_volley_delay, 0, b);
            tfa_fx_projectile(s, b, p, DREAD_PROJ_VOLLEY, SPOTANIM_TFA_DREADNOTH_TFA_UNNAMED_4211,
                SPOTANIM_TFA_DREADNOTH_TFA_UNNAMED_4211_ANIM, 0, TFA_DREAD.jump_volley_delay);
            tfa_fx_projectile(s, b, p, DREAD_PROJ_GLOB, SPOTANIM_TFA_DREADNOTH_TFA_UNNAMED_4212,
                SPOTANIM_TFA_DREADNOTH_TFA_UNNAMED_4212_ANIM, 0, TFA_DREAD.glob_delay);
            tfa_dread_schedule(s, s->tick + TFA_DREAD.glob_delay, TFA_DREAD_EV_GLOB, p, s->players[p].player.x, s->players[p].player.y);
            return;
        }
        case TFA_DREAD_EV_GLOB: {
            if (tfa_alive(s, p) && s->players[p].player.x == ev->x && s->players[p].player.y == ev->y)
                tfa_damage_player_now(s, p, tfa_rand_range(s, TFA_DREAD.glob_min, TFA_DREAD.glob_max), ATTACK_STYLE_NONE);
            tfa_dread_blood(s, ev->x, ev->y);
            return;
        }
        case TFA_DREAD_EV_NONE:
            break;
    }
    abort();
}

static void tfa_dread_basic(TfaState* s, TfaNpc* b) {
    int hit = 0;
    for (int p = 0; p < s->team_size; p++) {
        if (!tfa_alive(s, p) || !tfa_dread_adjacent(b, &s->players[p].player)) continue;
        hit = 1;
        b->target = (int8_t)p;
        tfa_queue_npc_hit(s, p, tfa_npc_roll(s, b, p, ATTACK_STYLE_MELEE, TFA_DREAD.basic_max),
            ATTACK_STYLE_MELEE, 0, TFA_DREAD.basic_protected_pct, b);
    }
    if (!hit) return;
    b->act = TFA_ACT_MELEE;
    tfa_npc_anim(s, b, DREAD_ANIM_BASIC);
    b->attack_timer = (int16_t)TFA_DREAD.basic_interval;
}

static int tfa_dread_npc_damage(TfaState* s, TfaNpc* n, int damage, int attacker, AttackStyle style) {
    (void)attacker; (void)style;
    TfaDreadnothState* d = &s->boss.dreadnoth;
    if (n->counter > 0 || (n->kind == TFA_NPC_DREADNOTH && d->dying)) return 0;
    int applied = damage < n->hp ? damage : n->hp;
    n->hp -= applied;
    if (n->kind != TFA_NPC_DREADNOTH) return applied;
    if (d->phase < 3 && n->hp * 100 < TFA_DREAD.threshold_pct[d->phase] * n->max_hp) {
        d->phase_slams = TFA_DREAD.slams_per_phase[d->phase];
        d->slam_start = s->tick + 1;
        d->phase++;
    } else if (d->phase == 3 && n->hp * 100 < TFA_DREAD.final_pct * n->max_hp) {
        d->phase = 4;
        d->final_phase = 1;
        d->next_jump = s->tick + TFA_DREAD.jump_first_delay;
    }
    return applied;
}

static void tfa_dread_npc_died(TfaState* s, TfaNpc* n, int attacker) {
    (void)attacker;
    TfaDreadnothState* d = &s->boss.dreadnoth;
    if (n->kind == TFA_NPC_JELLY ? n->counter > 0 : d->dying) return;
    n->attackable = 0;
    if (n->kind == TFA_NPC_JELLY) {
        tfa_npc_anim(s, n, DREAD_ANIM_JELLY_DEATH);
        n->counter = (int16_t)TFA_DREAD.jelly_death_ticks;
        tfa_dread_blood(s, n->x, n->y);
        int dx = tfa_rand(s, 3) - 1, dy = tfa_rand(s, 3) - 1;
        tfa_dread_blood(s, n->x + dx, n->y + dy);
        return;
    }
    tfa_npc_anim(s, n, DREAD_ANIM_DEATH);
    memset(d->events, 0, sizeof(d->events));
    d->dying = 1;
    d->death_tick = s->tick;
}

static void tfa_dread_tick(TfaState* s, TfaContext* ctx) {
    TfaDreadnothState* d = &s->boss.dreadnoth;
    if (d->dead && tfa_dread_jelly_count(s) == 0) {
        tfa_room_cleared(s, ctx);
        return;
    }
    for (int i = 0; i < TFA_DREAD_MAX_EVENTS; i++) {
        TfaDreadEvent ev = d->events[i];
        if (ev.kind == TFA_DREAD_EV_NONE || ev.tick != s->tick) continue;
        d->events[i].kind = TFA_DREAD_EV_NONE;
        tfa_dread_fire_event(s, tfa_dread_boss(s), &ev);
    }
    if (d->spawn_tick == s->tick) {
        for (int i = 0; i < d->spawn_n; i++) tfa_dread_jelly_spawn(s, d->spawn_x[i], d->spawn_y[i]);
        d->spawn_n = 0;
        d->spawn_tick = -1;
    }
    for (int i = 0; i < d->spawn_n; i++)
        if (s->tick >= d->mark_tick[i])
            tfa_fx_ground(s, d->spawn_x[i], d->spawn_y[i], DREAD_GFX_MARK, s->tick == d->mark_tick[i] ? d->mark_delay[i] : 1);
    for (int i = 0; i < TFA_MAX_NPCS; i++) {
        TfaNpc* j = &s->npcs[i];
        if (j->active && j->kind == TFA_NPC_JELLY && j->anim_tick != s->tick) {
            j->act = TFA_ACT_IDLE;
            tfa_dread_jelly_act(s, j);
        }
    }
    tfa_dread_promote(s);
    if (d->dead) return;

    TfaNpc* b = tfa_dread_boss(s);
    b->act = TFA_ACT_IDLE;
    if (d->dying) {
        int since = s->tick - d->death_tick;
        if (since == TFA_DREAD.death_spawn_delay) {
            for (int k = 0; k < TFA_DREAD.final_jellies; k++) {
                int x, y, dup;
                do {
                    x = b->x + tfa_rand(s, b->size);
                    y = b->y + tfa_rand(s, b->size);
                    dup = 0;
                    for (int i = 0; i < TFA_MAX_NPCS; i++)
                        dup |= s->npcs[i].active && s->npcs[i].kind == TFA_NPC_JELLY && s->npcs[i].anim_tick == s->tick
                            && s->npcs[i].x == x && s->npcs[i].y == y;
                } while (dup);
                tfa_dread_jelly_spawn(s, x, y);
            }
        }
        if (since == TFA_DREAD.death_remove_delay) {
            b->active = 0;
            d->dead = 1;
        }
        return;
    }
    if (b->attack_timer > 0) b->attack_timer--;

    if (d->slam_start == s->tick) {
        d->slams_left = d->phase_slams;
        d->next_slam = s->tick;
        d->slam_start = -1;
        d->walk_window = 0;
    }
    if (d->slams_left > 0) {
        if (s->tick != d->next_slam) return;
        tfa_dread_slam(s, b);
        d->next_slam += TFA_DREAD.slam_gap;
        if (--d->slams_left == 0) {
            b->attack_timer = (int16_t)TFA_DREAD.basic_interval;
            d->walk_timer = TFA_DREAD.walk_post_slam;
        }
        return;
    }
    if (d->final_phase) {
        if (s->tick == d->next_jump) {
            tfa_dread_jump(s, b);
            d->next_jump += TFA_DREAD.jump_interval;
        }
        return;
    }

    if (!d->engaged) {
        int p = tfa_dread_nearest_alive(s, b->x + 2, b->y + 2);
        if (p < 0) return;
        const Player* pl = &s->players[p].player;
        if (tfa_dread_adjacent(b, pl)) {
            d->engaged = 1;
            d->walk_timer = TFA_DREAD.walk_period;
            b->attack_timer = 0;
        } else if (s->room_tick % 2 == 1) {
            tfa_dread_step_toward(b, pl->x, pl->y);
            return;
        } else {
            return;
        }
    }
    if (d->walk_window > 0) {
        if (d->walk_window % 2 == 0) {
            const Player* pl = &s->players[d->walk_target].player;
            if (tfa_alive(s, d->walk_target) && !tfa_dread_adjacent(b, pl)) tfa_dread_step_toward(b, pl->x, pl->y);
        }
        d->walk_window--;
    } else if (--d->walk_timer <= 0) {
        d->walk_window = TFA_DREAD.walk_window;
        d->walk_timer = TFA_DREAD.walk_period;
        d->walk_target = tfa_dread_random_alive(s);
    }
    if (b->attack_timer == 0) tfa_dread_basic(s, b);
}
