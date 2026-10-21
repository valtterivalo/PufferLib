/* Ocellus, phases 3 and 4, and the room dispatch. Phase 3 is measured from 6 fights (the melee rotation, five specials,
   the add timer and the quadrant destroy); phase 4 was never reached and follows the wiki description, provisional. */

typedef struct {
    int first_attack;            /* 14881 -> first 14842 +10 (3/3) */
    int arrive;                  /* the boss reaches its quadrant 6 after 14881 (f3: 502-496) */
    int melee_gap;               /* 14842 -> next +3 (82/82) */
    int slash_gap;               /* 14846 -> next +4 (38/42), +6 before Insolence */
    int magic_gap;               /* 14851 -> next +6 (5/5) */
    int melee_max, melee_protected_max; /* 22 unprayed (21, 22), 0..7 under Protect from Melee (n=41) */
    int slash_min, slash_max;    /* 22..28, no prayer blocks it (n=12) */
    int slash_len;               /* band 3 wide and 5 long from the facing edge (11/11 hits inside) */
    int magic_min, magic_max;    /* 13 22 21 24 18 28 unprayed, 0 under Protect from Magic (3/3) */
    int magic_delay;             /* 14851 -> hit +2 */
    int special_after_slash;     /* spin and Untether 4 after the slash, Insolence text at +4 and 14848 at +6 */
    int spin_hit_min, spin_hit_max; /* 14..26 on 14871, 17 on the first hit */
    int spin_debris_short, spin_debris_long; /* gfx 4346 at +3, +6 after the quadrant destroyed (6/6 spins) */
    int spin_loop;               /* 14870 at +5 in the long variant */
    int spin_to_next;            /* 14871 -> next attack +4 */
    int injury_after, injury_ticks, hit_after; /* injured at G+1, locked for 3 ticks, unprotected hit at G+2 */
    int void_first_min, void_first_max, void_tick_min, void_tick_max, void_tick_count; /* 62702: 14..16 at G+5, 6..7 every 2 for 12 ticks */
    int void_first;
    int unt_loop;                /* 14858 at +2 */
    int unt_dots_first, unt_dots_gap, unt_dots_count; /* gfx 4270 at +6 every 4, four batches */
    int unt_dot_hit, unt_dot_min, unt_dot_max, unt_dot_decoys; /* hits 4 later, 23..27 (n=12), 12 tiles per batch */
    int unt_wave_min, unt_wave_max; /* waves 17..27 under no Magic, 0 under Magic (12/12) at +10 +14 +18 +22 */
    int unt_pray_on, unt_pray_off;
    int unt_slam_tell, unt_slam_anim, unt_slam_hit; /* gfx 4320 +25, 14861 +29, hit +30 */
    int unt_slam_radius, unt_slam_min, unt_slam_max; /* 13/13 hits at Chebyshev 0..4, 34..42 */
    int unt_next;                /* 14857 -> next 14842 +37 */
    int destroy_text, destroy_anim, destroy_end, destroy_move, destroy_boss, destroy_next; /* +31 +33 +42 +44 +49 +52 (f6, run 4) */
    int destroy_min, destroy_max;/* 6..7 crossing */
    int ins_text, ins_first, ins_gap, ins_count, ins_hit, ins_next; /* text -> 14848 +2, 5 apart x3, hit +3, next +8 after the third */
    int ins_min, ins_max;        /* 33 37 42 43 */
    int ins_ray_from_x10, ins_ray_to_x10, ins_perp_x10; /* ray along 1.5..8 from his centre, perpendicular up to 1.1 */
    int bomb_lead;               /* spotanim 4302 3..4 before the tile, impact +2 after: create 5 before the first hit */
    int bomb_min, bomb_max, bomb_second_min, bomb_second_max, bomb_second_after; /* 2..11 then 6..7 two later */
    int bomb_double_pct;
    int vine_every;              /* the 1st and 4th spin: a vine after each */
    int vine_spawn;              /* spawns 3 after the text */
    int vine_root;               /* roots 32 ticks, hits 34..44 unless broken */
    int vine_min, vine_max;
    int add_first;               /* first add due at 14881 +91 (3/3) */
    int add_gaps[3];             /* 48 (4), 55 (2), 57 (1) */
    int siphon_pulse;            /* projectile every 4 */
    int siphon_heal_min, siphon_heal_max, siphon_death_heal; /* heals Ocellus 12..14, the target 22 when it dies */
    int augmenter_blow;          /* detonates 30 after spawn: 37..39 on everyone */
    int augmenter_min, augmenter_max;
    int tile_lo, tile_hi;        /* add tiles 4..10 from the cross */
    int hp_per_player_p3;
    int p4_first_hp_pct;         /* provisional: the boss keeps at least a fifth of its pool for phase 4 */
} TfaOcellusP3Tuning;

static const TfaOcellusP3Tuning TFA_OC3 = {
    .first_attack = 10, .arrive = 6,
    .melee_gap = 3, .slash_gap = 4, .magic_gap = 6,
    .melee_max = 22, .melee_protected_max = 7,
    .slash_min = 22, .slash_max = 28, .slash_len = 5,
    .magic_min = 13, .magic_max = 28, .magic_delay = 2,
    .special_after_slash = 4,
    .spin_hit_min = 14, .spin_hit_max = 26,
    .spin_debris_short = 3, .spin_debris_long = 6, .spin_loop = 5, .spin_to_next = 4,
    .injury_after = 1, .injury_ticks = 3, .hit_after = 2,
    .void_first_min = 14, .void_first_max = 16, .void_tick_min = 6, .void_tick_max = 7, .void_tick_count = 6,
    .void_first = 5,
    .unt_loop = 2,
    .unt_dots_first = 6, .unt_dots_gap = 4, .unt_dots_count = 4,
    .unt_dot_hit = 4, .unt_dot_min = 23, .unt_dot_max = 27, .unt_dot_decoys = 2,
    .unt_wave_min = 17, .unt_wave_max = 27,
    .unt_pray_on = 5, .unt_pray_off = 22,
    .unt_slam_tell = 25, .unt_slam_anim = 29, .unt_slam_hit = 30,
    .unt_slam_radius = 4, .unt_slam_min = 34, .unt_slam_max = 42,
    .unt_next = 37,
    .destroy_text = 31, .destroy_anim = 33, .destroy_end = 42, .destroy_move = 44, .destroy_boss = 49, .destroy_next = 52,
    .destroy_min = 6, .destroy_max = 7,
    .ins_text = 0, .ins_first = 2, .ins_gap = 5, .ins_count = 3, .ins_hit = 3, .ins_next = 8,
    .ins_min = 33, .ins_max = 43,
    .ins_ray_from_x10 = 15, .ins_ray_to_x10 = 80, .ins_perp_x10 = 11,
    .bomb_lead = 5,
    .bomb_min = 2, .bomb_max = 11, .bomb_second_min = 6, .bomb_second_max = 7, .bomb_second_after = 2,
    .bomb_double_pct = 50,
    .vine_every = 3, .vine_spawn = 3, .vine_root = 32, .vine_min = 34, .vine_max = 44,
    .add_first = 91, .add_gaps = { 48, 55, 57 },
    .siphon_pulse = 4, .siphon_heal_min = 12, .siphon_heal_max = 14, .siphon_death_heal = 22,
    .augmenter_blow = 30, .augmenter_min = 37, .augmenter_max = 39,
    .tile_lo = 4, .tile_hi = 10,
    .hp_per_player_p3 = 1186,
    .p4_first_hp_pct = 20,
};

typedef struct {
    int rise, wind_hit, wind_explosions, wind_gap, wind_len; /* provisional: the wiki gives no ticks */
    int land, soul_len, soul_hit, void_len;
    int steps;
    int ring;
    int ring_min, ring_max;
    int wind_min, wind_max;      /* explosion damage per hit, provisional */
    int soul_min, soul_max;
    int void_min, void_max;
    int rifts, rift_life, rift_radius, rift_min, rift_max;
} TfaOcellusP4Tuning;

/* All provisional: the wiki describes the order of P4 (deflect melee, wind of half current hp, 6 void explosions every 2 ticks,
   soul split with a quadrant-wide attack, void quadrant with deflect range) but publishes no ticks, ids or damage. */
static const TfaOcellusP4Tuning TFA_OC4 = {
    .rise = 4, .wind_hit = 2, .wind_explosions = 6, .wind_gap = 2, .wind_len = 14,
    .land = 3, .soul_len = 8, .soul_hit = 4, .void_len = 14,
    .steps = 8,
    .ring = 2,
    .ring_min = 6, .ring_max = 8,
    .wind_min = 14, .wind_max = 20,
    .soul_min = 25, .soul_max = 40,
    .void_min = 8, .void_max = 12,
    .rifts = 2, .rift_life = 12, .rift_radius = 3, .rift_min = 40, .rift_max = 48,
};

static const int8_t OC_QUAD_ORDER[4] = { 1, 3, 2, 0 };
static const int8_t OC_QUAD_BOSS[4][2] = { {24, 42}, {36, 42}, {24, 53}, {36, 53} };

static int oc_cur_quad(const TfaOcellusState* os) { return OC_QUAD_ORDER[os->quad_index]; }

static int oc_adjacent_target(const TfaState* s, const TfaNpc* n) {
    int best = -1, best_d = 1 << 30;
    for (int p = 0; p < s->team_size; p++) {
        if (!tfa_alive(s, p)) continue;
        const Player* pl = &s->players[p].player;
        if (!encounter_entity_footprint_cardinal_adjacent(pl->x, pl->y, 1, n->x, n->y, n->size)) continue;
        int d = tfa_player_npc_distance(&s->players[p], n);
        if (d < best_d) { best_d = d; best = p; }
    }
    return best;
}

static void oc_add_tile(TfaState* s, int q, int* out_x, int* out_y) {
    int sx = (q & 1) ? 1 : -1, sy = (q & 2) ? 1 : -1;
    for (int tries = 0; tries < 200; tries++) {
        int x = OC_CROSS_X + sx * (TFA_OC3.tile_lo + tfa_rand(s, TFA_OC3.tile_hi - TFA_OC3.tile_lo + 1));
        int y = OC_CROSS_Y + sy * (TFA_OC3.tile_lo + tfa_rand(s, TFA_OC3.tile_hi - TFA_OC3.tile_lo + 1));
        if (tfa_layout_blocked(tfa_room_layout(s->room), x, y)) continue;
        *out_x = x;
        *out_y = y;
        return;
    }
    fprintf(stderr, "ocellus: no free add tile in quadrant %d\n", q);
    abort();
}

static void oc_p3_start(TfaState* s, TfaNpc* n) {
    TfaOcellusState* os = oc_state(s);
    os->phase = TFA_OCELLUS_P3;
    os->phase_start = s->tick;
    n->npc_id = OC_NPC_P3;
    n->monster = MON_TFA_OCELLUS_P3;
    n->attackable = 1;
    n->max_hp = oc_scaled(TFA_OC1.hp_base_p3, TFA_OC3.hp_per_player_p3, s->team_size);
    if (n->hp > n->max_hp) n->hp = n->max_hp;
    tfa_npc_anim(s, n, OC_ANIM_P3_START);
    oc_want(s, PRAYER_PROTECT_MELEE);
    os->quad_index = 0;
    os->quad_first = 1;
    oc_at(s, TFA_OC3.arrive, OCE_P3_ARRIVE, 0, 0);
    oc_at(s, TFA_OC3.first_attack, OCE_P3_ATTACK, 0, 0);
    oc_at(s, TFA_OC3.add_first, OCE_P3_ADD_DUE, 0, 0);
    os->next_attack_tick = s->tick + TFA_OC3.first_attack;
}

static void oc_arrive(TfaState* s, TfaNpc* n) {
    int q = oc_cur_quad(oc_state(s));
    n->x = OC_QUAD_BOSS[q][0];
    n->y = OC_QUAD_BOSS[q][1];
}

/* Players cannot leave the quadrant of the boss: the barrier objects are not in the recorded collision. */
static void oc_confine(TfaState* s) {
    TfaOcellusState* os = oc_state(s);
    if (s->tick < os->free_roam_until) return;
    int q = oc_cur_quad(os);
    for (int p = 0; p < s->team_size; p++) {
        if (!tfa_alive(s, p)) continue;
        const Player* pl = &s->players[p].player;
        if (oc_quadrant(pl->x, pl->y) != q) oc_place_player(s, p, q);
    }
}

static int oc_footprint_free(const TfaState* s, int x, int y, int q) {
    for (int dy = 0; dy < 3; dy++)
        for (int dx = 0; dx < 3; dx++)
            if (tfa_layout_blocked(tfa_room_layout(s->room), x + dx, y + dy)) return 0;
    return oc_quadrant(x + 1, y + 1) == q;
}

static void oc_chase(TfaState* s, TfaNpc* n) {
    TfaOcellusState* os = oc_state(s);
    if (s->tick <= os->busy_until || oc_adjacent_target(s, n) >= 0) return;
    int p = oc_nearest_alive(s, n);
    if (p < 0) return;
    const Player* pl = &s->players[p].player;
    int sx = oc_sign(pl->x - (n->x + 1)), sy = oc_sign(pl->y - (n->y + 1));
    int q = oc_cur_quad(os);
    if (oc_footprint_free(s, n->x + sx, n->y + sy, q)) { n->x += sx; n->y += sy; }
    else if (sx && oc_footprint_free(s, n->x + sx, n->y, q)) n->x += sx;
    else if (sy && oc_footprint_free(s, n->x, n->y + sy, q)) n->y += sy;
}

static void oc_whole_room(TfaState* s, int start, int min, int max, AttackStyle style, int gfx) {
    oc_haz(s, 0, 0, TFA_ROOM_SIZE, TFA_ROOM_SIZE, start, min, max, style, 0, gfx, -1);
}

/* 8 facing directions from the boss centre toward a tile. */
static void oc_facing(const TfaNpc* n, int tx, int ty, int* ux, int* uy) {
    double a = atan2((double)(ty - (n->y + 1)), (double)(tx - (n->x + 1)));
    int k = (int)floor(a / (TFA_PI / 4.0) + 0.5);
    k = ((k % 8) + 8) % 8;
    static const int8_t F[8][2] = { {1, 0}, {1, 1}, {0, 1}, {-1, 1}, {-1, 0}, {-1, -1}, {0, -1}, {1, -1} };
    *ux = F[k][0];
    *uy = F[k][1];
}

static void oc_slash_band(TfaState* s, TfaNpc* n, int target) {
    const Player* pl = &s->players[target].player;
    int ux, uy;
    oc_facing(n, pl->x, pl->y, &ux, &uy);
    int len = TFA_OC3.slash_len, start = s->tick + 1;
    int lo = TFA_OC3.slash_min, hi = TFA_OC3.slash_max;
    if (ux == 0 || uy == 0) {
        int x = ux > 0 ? n->x + 3 : ux < 0 ? n->x - len : n->x;
        int y = uy > 0 ? n->y + 3 : uy < 0 ? n->y - len : n->y;
        oc_haz(s, x, y, ux ? len : 3, uy ? len : 3, start, lo, hi, ATTACK_STYLE_NONE, 100, OC_GFX_SLASH, -1);
        return;
    }
    int cx = ux > 0 ? n->x + 2 : n->x, cy = uy > 0 ? n->y + 2 : n->y;
    for (int a = 0; a <= len; a++)
        for (int b = 0; b <= len; b++)
            if (abs(a - b) <= 1 && (a > b ? a : b) > 0) oc_haz_tile(s, cx + ux * a, cy + uy * b, start, lo, hi, OC_GFX_SLASH);
}

static void oc_ray(TfaState* s, TfaNpc* n, int target, int start) {
    const Player* pl = &s->players[target].player;
    double cx = n->x + 1, cy = n->y + 1;
    double vx = pl->x - cx, vy = pl->y - cy;
    double len = sqrt(vx * vx + vy * vy);
    if (len < 0.5) { vx = 1.0; vy = 0.0; len = 1.0; }
    double ux = vx / len, uy = vy / len;
    for (int y = (int)cy - 9; y <= (int)cy + 9; y++) {
        for (int x = (int)cx - 9; x <= (int)cx + 9; x++) {
            double dx = x - cx, dy = y - cy;
            double along = dx * ux + dy * uy, perp = fabs(dx * uy - dy * ux);
            if (along * 10.0 < TFA_OC3.ins_ray_from_x10 || along * 10.0 > TFA_OC3.ins_ray_to_x10 || perp * 10.0 > TFA_OC3.ins_perp_x10) continue;
            oc_haz_tile(s, x, y, start, TFA_OC3.ins_min, TFA_OC3.ins_max, OC_GFX_INSOLENCE);
        }
    }
}

static void oc_p3_melee(TfaState* s, TfaNpc* n, int target) {
    n->act = TFA_ACT_MELEE;
    n->target = (int8_t)target;
    tfa_npc_anim(s, n, OC_ANIM_MELEE);
    int prayed = encounter_prayer_correct_for_style(s->players[target].player.prayer, ATTACK_STYLE_MELEE);
    int max_hit = prayed ? TFA_OC3.melee_protected_max : TFA_OC3.melee_max;
    tfa_queue_npc_hit(s, target, tfa_npc_roll(s, n, target, ATTACK_STYLE_MELEE, max_hit), ATTACK_STYLE_MELEE, 0, 100, n);
}

static void oc_p3_magic(TfaState* s, TfaNpc* n) {
    n->act = TFA_ACT_MAGIC;
    tfa_npc_anim(s, n, OC_ANIM_MAGIC);
    oc_want(s, PRAYER_PROTECT_MAGIC);
    oc_at(s, TFA_OC3.magic_delay, OCE_P3_WANT, PRAYER_PROTECT_MELEE, 0);
    for (int p = 0; p < s->team_size; p++) {
        if (!tfa_alive(s, p)) continue;
        tfa_queue_npc_hit_land(s, p, tfa_rand_range(s, TFA_OC3.magic_min, TFA_OC3.magic_max), ATTACK_STYLE_MAGIC, TFA_OC3.magic_delay, 0, n);
        tfa_fx_projectile(s, n, p, 4297, SPOTANIM_TFA_OCELLUS_VFX_OCELLUS_MAGIC_PROJANIM01,
            SPOTANIM_TFA_OCELLUS_VFX_OCELLUS_MAGIC_PROJANIM01_ANIM, TFA_OC3.magic_delay - 1, 1);
    }
}

static void oc_p3_attack(TfaState* s, TfaNpc* n) {
    TfaOcellusState* os = oc_state(s);
    n->act = TFA_ACT_IDLE;
    os->busy_until = s->tick;
    if (os->quad_first) {
        os->quad_first = 0;
        oc_at(s, 1, OCE_P3_BOMB, 0, 0);
    }
    if (os->vine_pending) {
        os->vine_pending = 0;
        oc_at(s, TFA_OC3.vine_spawn, OCE_P3_VINE_SPAWN, 0, 0);
    }
    int gap;
    if (os->slot < 2) {
        int target = oc_adjacent_target(s, n);
        if (target >= 0) {
            oc_p3_melee(s, n, target);
            gap = TFA_OC3.melee_gap;
        } else {
            oc_p3_magic(s, n);
            gap = TFA_OC3.magic_gap;
        }
        os->slot++;
        oc_at(s, gap, OCE_P3_ATTACK, 0, 0);
    } else {
        int target = oc_nearest_alive(s, n);
        n->act = TFA_ACT_MELEE;
        n->target = (int8_t)target;
        tfa_npc_anim(s, n, OC_ANIM_SLASH);
        if (target >= 0) oc_slash_band(s, n, target);
        os->slot = 0;
        os->groups++;
        if (os->groups % 2 == 0) {
            int kind = os->specials++ % 4;
            oc_at(s, TFA_OC3.special_after_slash, OCE_P3_SPECIAL, kind, 0);
            gap = TFA_OC3.special_after_slash;
        } else {
            gap = TFA_OC3.slash_gap;
            oc_at(s, gap, OCE_P3_ATTACK, 0, 0);
        }
    }
    os->next_attack_tick = s->tick + gap;
}

static void oc_spin(TfaState* s, TfaNpc* n) {
    TfaOcellusState* os = oc_state(s);
    int long_variant = os->destroyed >= 1;
    int g = long_variant ? TFA_OC3.spin_debris_long : TFA_OC3.spin_debris_short;
    n->act = TFA_ACT_SPECIAL;
    tfa_npc_anim(s, n, OC_ANIM_SPIN);
    os->spins++;
    if (os->spins % TFA_OC3.vine_every == 1) os->vine_pending = 1;
    oc_whole_room(s, s->tick + 1, TFA_OC3.spin_hit_min, TFA_OC3.spin_hit_max, ATTACK_STYLE_MELEE, 0);
    oc_whole_room(s, s->tick + g + TFA_OC3.hit_after, TFA_OC3.spin_hit_min, TFA_OC3.spin_hit_max, ATTACK_STYLE_MELEE, 0);
    oc_at(s, g, OCE_P3_SPIN_DEBRIS, 0, 0);
    oc_at(s, g + TFA_OC3.injury_after, OCE_P3_SPIN_INJURY, 0, 0);
    oc_at(s, g, OCE_P3_WANT, PRAYER_NONE, 0);
    oc_at(s, g + 1, OCE_P3_WANT, PRAYER_PROTECT_MELEE, 0);
    if (long_variant) oc_at(s, TFA_OC3.spin_loop, OCE_P3_ANIM, OC_ANIM_SPIN_LOOP, 0);
    oc_at(s, g + TFA_OC3.hit_after, OCE_P3_ANIM, OC_ANIM_SPIN_END, 0);
    int next = g + TFA_OC3.hit_after + TFA_OC3.spin_to_next;
    oc_at(s, next, OCE_P3_ATTACK, 0, 0);
    os->next_attack_tick = s->tick + next;
    os->busy_until = s->tick + next;
}

static void oc_untether(TfaState* s, TfaNpc* n) {
    TfaOcellusState* os = oc_state(s);
    int destroy = ++os->untethers % 2 == 0;
    n->act = TFA_ACT_SPECIAL;
    tfa_npc_anim(s, n, OC_ANIM_UNTETHER);
    oc_at(s, TFA_OC3.unt_loop, OCE_P3_ANIM, OC_ANIM_UNTETHER_LOOP, 0);
    oc_at(s, TFA_OC3.unt_dots_first, OCE_P3_UNTETHER_DOTS, 0, 0);
    oc_at(s, TFA_OC3.unt_pray_on, OCE_P3_WANT, PRAYER_PROTECT_MAGIC, 0);
    oc_at(s, TFA_OC3.unt_pray_off, OCE_P3_WANT, PRAYER_PROTECT_MELEE, 0);
    oc_at(s, TFA_OC3.unt_slam_tell, OCE_P3_SLAM, 0, 0);
    oc_at(s, TFA_OC3.unt_slam_anim, OCE_P3_ANIM, OC_ANIM_SLAM, 0);
    int next = TFA_OC3.unt_next;
    os->busy_until = s->tick + TFA_OC3.unt_slam_hit;
    os->pause_until = s->tick + TFA_OC3.unt_slam_hit + 2;
    if (destroy) {
        oc_at(s, TFA_OC3.destroy_anim, OCE_P3_DESTROY, 0, 0);
        oc_at(s, TFA_OC3.destroy_end, OCE_P3_DESTROY_END, 0, 0);
        oc_at(s, TFA_OC3.destroy_move, OCE_P3_DESTROY_MOVE, 0, 0);
        oc_at(s, TFA_OC3.destroy_boss, OCE_P3_DESTROY_MOVE, 1, 0);
        next = TFA_OC3.destroy_next;
        os->pause_until = s->tick + TFA_OC3.destroy_boss + 2;
        os->busy_until = s->tick + next;
    }
    oc_at(s, next, destroy && os->destroyed == 2 ? OCE_P4_START : OCE_P3_ATTACK, 0, 0);
    os->next_attack_tick = s->tick + next;
}

static void oc_insolence(TfaState* s, TfaNpc* n) {
    TfaOcellusState* os = oc_state(s);
    for (int k = 0; k < TFA_OC3.ins_count; k++) oc_at(s, TFA_OC3.ins_first + TFA_OC3.ins_gap * k, OCE_P3_INSOLENCE, 0, 0);
    int next = TFA_OC3.ins_first + TFA_OC3.ins_gap * (TFA_OC3.ins_count - 1) + TFA_OC3.ins_next;
    oc_at(s, next, OCE_P3_ATTACK, 0, 0);
    os->next_attack_tick = s->tick + next;
    os->busy_until = s->tick + next;
    n->act = TFA_ACT_SPECIAL;
}

static void oc_spawn_add(TfaState* s) {
    TfaOcellusState* os = oc_state(s);
    int x, y;
    oc_add_tile(s, oc_cur_quad(os), &x, &y);
    if (os->destroyed == 0) {
        TfaNpc* a = tfa_npc_spawn(s, TFA_NPC_VOID_SIPHON, MON_TFA_VOID_SIPHON, OC_NPC_SIPHON, x, y);
        a->target = (int8_t)oc_random_alive(s);
        tfa_npc_anim(s, a, OC_ANIM_SIPHON_SPAWN);
        oc_at(s, TFA_OC3.siphon_pulse, OCE_P3_SIPHON_PULSE, tfa_npc_slot(s, a), (int)a->instance_id);
    } else {
        TfaNpc* a = tfa_npc_spawn(s, TFA_NPC_VOID_AUGMENTER, MON_TFA_VOID_AUGMENTER, OC_NPC_AUGMENTER, x, y);
        tfa_npc_anim(s, a, OC_ANIM_AUGMENTER_SPAWN);
        oc_at(s, TFA_OC3.augmenter_blow, OCE_P3_AUGMENTER_BLOW, tfa_npc_slot(s, a), (int)a->instance_id);
    }
}

static TfaNpc* oc_find(TfaState* s, int slot, int instance) {
    TfaNpc* n = &s->npcs[slot];
    return n->active && (int)n->instance_id == instance ? n : NULL;
}

static void oc_bomb(TfaState* s) {
    const Player* pl = &s->players[oc_random_alive(s)].player;
    int start = s->tick + TFA_OC3.bomb_lead;
    oc_haz_tile(s, pl->x, pl->y, start, TFA_OC3.bomb_min, TFA_OC3.bomb_max, OC_GFX_BOMB);
    oc_haz_tile(s, pl->x, pl->y, start + TFA_OC3.bomb_second_after, TFA_OC3.bomb_second_min, TFA_OC3.bomb_second_max, OC_GFX_BOMB);
}

static void oc_p3_event(TfaState* s, TfaNpc* n, const TfaOcEvent* e) {
    TfaOcellusState* os = oc_state(s);
    switch (e->kind) {
        case OCE_P3_START: oc_p3_start(s, n); break;
        case OCE_P3_ARRIVE: oc_arrive(s, n); break;
        case OCE_P3_ATTACK: oc_p3_attack(s, n); break;
        case OCE_P3_SPECIAL:
            if (e->arg % 2 == 0) oc_spin(s, n);
            else if (e->arg == 1) oc_untether(s, n);
            else oc_insolence(s, n);
            break;
        case OCE_P3_WANT: oc_want(s, (OverheadPrayer)e->arg); break;
        case OCE_P3_ANIM: tfa_npc_anim(s, n, e->arg); break;
        case OCE_P3_SPIN_DEBRIS:
            for (int p = 0; p < s->team_size; p++) {
                if (!tfa_alive(s, p)) continue;
                int x = s->players[p].player.x, y = s->players[p].player.y;
                oc_haz_tile(s, x, y, s->tick + TFA_OC3.void_first, TFA_OC3.void_first_min, TFA_OC3.void_first_max, OC_GFX_DEBRIS);
                for (int k = 1; k <= TFA_OC3.void_tick_count; k++)
                    oc_haz_tile(s, x, y, s->tick + TFA_OC3.void_first + 2 * k, TFA_OC3.void_tick_min, TFA_OC3.void_tick_max, OC_GFX_VOID);
            }
            break;
        case OCE_P3_SPIN_INJURY:
            for (int p = 0; p < s->team_size; p++) {
                TfaPlayer* tp = &s->players[p];
                if (!tfa_alive(s, p) || tp->player.prayer == PRAYER_NONE) continue;
                tp->player.prayer = PRAYER_NONE;
                tp->injured_ticks = TFA_OC3.injury_ticks;
            }
            break;
        case OCE_P3_UNTETHER_DOTS:
            for (int p = 0; p < s->team_size; p++) {
                if (!tfa_alive(s, p)) continue;
                const Player* pl = &s->players[p].player;
                int start = s->tick + TFA_OC3.unt_dot_hit;
                oc_haz_tile(s, pl->x, pl->y, start, TFA_OC3.unt_dot_min, TFA_OC3.unt_dot_max, OC_GFX_DOT);
                for (int k = 0; k < TFA_OC3.unt_dot_decoys; k++)
                    oc_haz_tile(s, pl->x + tfa_rand(s, 7) - 3, pl->y + tfa_rand(s, 7) - 3, start, TFA_OC3.unt_dot_min, TFA_OC3.unt_dot_max, OC_GFX_DOT);
            }
            oc_whole_room(s, s->tick + TFA_OC3.unt_dot_hit, TFA_OC3.unt_wave_min, TFA_OC3.unt_wave_max, ATTACK_STYLE_MAGIC, 0);
            if (e->arg + 1 < TFA_OC3.unt_dots_count) oc_at(s, TFA_OC3.unt_dots_gap, OCE_P3_UNTETHER_DOTS, e->arg + 1, 0);
            break;
        case OCE_P3_SLAM: {
            int r = TFA_OC3.unt_slam_radius, c = 1;
            oc_haz(s, n->x + c - r, n->y + c - r, 2 * r + 1, 2 * r + 1, s->tick + TFA_OC3.unt_slam_hit - TFA_OC3.unt_slam_tell,
                TFA_OC3.unt_slam_min, TFA_OC3.unt_slam_max, ATTACK_STYLE_NONE, 100, OC_GFX_SLAM, -1);
            break;
        }
        case OCE_P3_INSOLENCE: {
            int target = oc_random_alive(s);
            tfa_npc_anim(s, n, OC_ANIM_INSOLENCE);
            oc_ray(s, n, target, s->tick + TFA_OC3.ins_hit);
            break;
        }
        case OCE_P3_DESTROY:
            tfa_npc_anim(s, n, OC_ANIM_DESTROY);
            os->free_roam_until = s->tick + TFA_OC3.destroy_move - TFA_OC3.destroy_anim + 1;
            os->destroyed++;
            os->quad_index++;
            break;
        case OCE_P3_DESTROY_END: tfa_npc_anim(s, n, OC_ANIM_DESTROY_END); break;
        case OCE_P3_DESTROY_MOVE:
            if (e->arg == 0) {
                for (int p = 0; p < s->team_size; p++) {
                    if (!tfa_alive(s, p)) continue;
                    oc_place_player(s, p, oc_cur_quad(os));
                    tfa_damage_player_now(s, p, tfa_rand_range(s, TFA_OC3.destroy_min, TFA_OC3.destroy_max), ATTACK_STYLE_NONE);
                }
            } else {
                oc_arrive(s, n);
                os->quad_first = os->destroyed < 3;
            }
            break;
        case OCE_P3_BOMB:
            oc_bomb(s);
            if (e->arg == 0 && tfa_rand(s, 100) < TFA_OC3.bomb_double_pct) oc_at(s, 2, OCE_P3_BOMB, 1, 0);
            break;
        case OCE_P3_VINE_SPAWN: {
            int p = oc_random_alive(s);
            TfaPlayer* tp = &s->players[p];
            TfaNpc* v = tfa_npc_spawn(s, TFA_NPC_VOID_VINE, MON_TFA_VOID_VINE, OC_NPC_VINE, tp->player.x, tp->player.y);
            v->target = (int8_t)p;
            tp->rooted_ticks = TFA_OC3.vine_root;
            oc_at(s, TFA_OC3.vine_root, OCE_P3_VINE_BREAK, tfa_npc_slot(s, v), (int)v->instance_id);
            break;
        }
        case OCE_P3_VINE_BREAK: {
            TfaNpc* v = oc_find(s, e->arg, e->arg2);
            if (!v) break;
            if (tfa_alive(s, v->target)) {
                tfa_damage_player_now(s, v->target, tfa_rand_range(s, TFA_OC3.vine_min, TFA_OC3.vine_max), ATTACK_STYLE_NONE);
                s->players[v->target].rooted_ticks = 0;
            }
            v->active = 0;
            break;
        }
        case OCE_P3_ADD_DUE: {
            if (s->tick < os->pause_until) {
                oc_at(s, os->pause_until - s->tick, OCE_P3_ADD_DUE, e->arg, 0);
                break;
            }
            oc_spawn_add(s);
            int r = tfa_rand(s, 7);
            oc_at(s, TFA_OC3.add_gaps[r < 4 ? 0 : r < 6 ? 1 : 2], OCE_P3_ADD_DUE, 0, 0);
            break;
        }
        case OCE_P3_SIPHON_PULSE: {
            TfaNpc* a = oc_find(s, e->arg, e->arg2);
            if (!a) break;
            oc_boss_heal(n, tfa_rand_range(s, TFA_OC3.siphon_heal_min, TFA_OC3.siphon_heal_max));
            oc_at(s, TFA_OC3.siphon_pulse, OCE_P3_SIPHON_PULSE, e->arg, e->arg2);
            break;
        }
        case OCE_P3_AUGMENTER_BLOW: {
            TfaNpc* a = oc_find(s, e->arg, e->arg2);
            if (!a) break;
            tfa_npc_anim(s, a, OC_ANIM_AUGMENTER_BLOW);
            for (int p = 0; p < s->team_size; p++)
                if (tfa_alive(s, p)) tfa_damage_player_now(s, p, tfa_rand_range(s, TFA_OC3.augmenter_min, TFA_OC3.augmenter_max), ATTACK_STYLE_NONE);
            a->active = 0;
            break;
        }
        default: abort();
    }
}

/* Phase 4. */
typedef enum {
    OC4_RISE = 0, OC4_WIND_A, OC4_LAND_A, OC4_SOUL_A, OC4_VOID, OC4_WIND_B, OC4_LAND_B, OC4_SOUL_B,
} TfaOc4Step;

static void oc_p4_ring(TfaState* s, const TfaNpc* n, int inverse, int start, int end, int min, int max) {
    int x = n->x - 1, y = n->y - 1, side = n->size + 2;
    if (!inverse) {
        tfa_hazard_add(s, (TfaHazard){ .kind = TFA_HAZ_FLOOR, .style = ATTACK_STYLE_NONE, .w = (uint8_t)side, .h = (uint8_t)side,
            .owner = -1, .x = (int16_t)x, .y = (int16_t)y, .start_tick = start, .end_tick = end,
            .min_damage = (int16_t)min, .max_damage = (int16_t)max, .protected_pct = 100, .loc_id = OC_GFX_VOID });
        return;
    }
    int q = oc_quadrant(n->x + 1, n->y + 1);
    int x0 = (q & 1) ? OC_CROSS_X + 1 : OC_CROSS_X - 14, y0 = (q & 2) ? OC_CROSS_Y + 1 : OC_CROSS_Y - 14, w = 14;
    int rects[4][4] = {
        { x0, y0, w, y - y0 },
        { x0, y + side, w, y0 + w - y - side },
        { x0, y, x - x0, side },
        { x + side, y, x0 + w - x - side, side },
    };
    for (int i = 0; i < 4; i++) {
        if (rects[i][2] <= 0 || rects[i][3] <= 0) continue;
        tfa_hazard_add(s, (TfaHazard){ .kind = TFA_HAZ_FLOOR, .style = ATTACK_STYLE_NONE, .w = (uint8_t)rects[i][2], .h = (uint8_t)rects[i][3],
            .owner = -1, .x = (int16_t)rects[i][0], .y = (int16_t)rects[i][1], .start_tick = start, .end_tick = end,
            .min_damage = (int16_t)min, .max_damage = (int16_t)max, .protected_pct = 100, .loc_id = OC_GFX_VOID });
    }
}

static void oc_p4_wind(TfaState* s, TfaNpc* n) {
    TfaOcellusState* os = oc_state(s);
    n->act = TFA_ACT_MAGIC;
    for (int p = 0; p < s->team_size; p++) {
        if (!tfa_alive(s, p)) continue;
        int dmg = s->players[p].player.current_hitpoints / 2;
        tfa_damage_player_now(s, p, dmg, ATTACK_STYLE_NONE);
        int x, y;
        oc_add_tile(s, oc_cur_quad(os), &x, &y);
        os->orb_x[p] = x;
        os->orb_y[p] = y;
        os->orb_heal[p] = dmg;
    }
    for (int k = 0; k < TFA_OC4.wind_explosions; k++) oc_at(s, TFA_OC4.wind_gap * (k + 1), OCE_P4_EXPLOSION, 0, 0);
}

static void oc_p4_orbs(TfaState* s) {
    TfaOcellusState* os = oc_state(s);
    for (int p = 0; p < s->team_size; p++) {
        if (os->orb_heal[p] <= 0 || !tfa_alive(s, p)) continue;
        const Player* pl = &s->players[p].player;
        if (pl->x != os->orb_x[p] || pl->y != os->orb_y[p]) continue;
        oc_heal_player(s, p, os->orb_heal[p]);
        os->orb_heal[p] = 0;
    }
}

static void oc_p4_step(TfaState* s, TfaNpc* n, int step) {
    TfaOcellusState* os = oc_state(s);
    int len;
    n->act = TFA_ACT_IDLE;
    switch (step) {
        case OC4_RISE:
            tfa_npc_anim(s, n, OC_ANIM_DEFLECT_IN);
            os->deflect = ATTACK_STYLE_MELEE;
            os->soul_split = 0;
            oc_p4_ring(s, n, 0, s->tick + 1, s->tick + TFA_OC4.rise, TFA_OC4.ring_min, TFA_OC4.ring_max);
            if (os->p4_cycle > 0) {
                for (int k = 0; k < TFA_OC4.rifts; k++) {
                    int x, y;
                    oc_add_tile(s, oc_cur_quad(os), &x, &y);
                    TfaNpc* r = tfa_npc_spawn(s, TFA_NPC_VOID_RIFT, MON_TFA_VOID_RIFT, OC_NPC_RIFT, x, y);
                    oc_at(s, TFA_OC4.rift_life, OCE_P4_RIFT_BLOW, tfa_npc_slot(s, r), (int)r->instance_id);
                }
            }
            len = TFA_OC4.rise;
            break;
        case OC4_WIND_A:
        case OC4_WIND_B:
            oc_p4_wind(s, n);
            len = TFA_OC4.wind_len;
            break;
        case OC4_LAND_A:
        case OC4_LAND_B:
            tfa_npc_anim(s, n, OC_ANIM_DEFLECT_OUT);
            os->deflect = ATTACK_STYLE_NONE;
            len = TFA_OC4.land;
            break;
        case OC4_SOUL_A:
        case OC4_SOUL_B: {
            tfa_npc_anim(s, n, OC_ANIM_SOUL_SPLIT);
            os->soul_split = 1;
            AttackStyle style = (AttackStyle)(ATTACK_STYLE_MELEE + tfa_rand(s, 3));
            n->act = style == ATTACK_STYLE_MELEE ? TFA_ACT_MELEE : style == ATTACK_STYLE_RANGED ? TFA_ACT_RANGED : TFA_ACT_MAGIC;
            oc_want(s, style == ATTACK_STYLE_MELEE ? PRAYER_PROTECT_MELEE : style == ATTACK_STYLE_RANGED ? PRAYER_PROTECT_RANGED : PRAYER_PROTECT_MAGIC);
            for (int p = 0; p < s->team_size; p++)
                if (tfa_alive(s, p))
                    tfa_queue_npc_hit_land(s, p, tfa_rand_range(s, TFA_OC4.soul_min, TFA_OC4.soul_max), style, TFA_OC4.soul_hit, 0, n);
            len = TFA_OC4.soul_len;
            break;
        }
        default:
            tfa_npc_anim(s, n, OC_ANIM_DEFLECT_IN);
            os->deflect = ATTACK_STYLE_RANGED;
            os->soul_split = 0;
            len = TFA_OC4.void_len;
            oc_p4_ring(s, n, 1, s->tick + 1, s->tick + len, TFA_OC4.void_min, TFA_OC4.void_max);
            break;
    }
    os->p4_step = (step + 1) % TFA_OC4.steps;
    if (os->p4_step == 0) os->p4_cycle++;
    oc_at(s, len, OCE_P4_STEP, os->p4_step, 0);
    os->next_attack_tick = s->tick + len;
}

static void oc_p4_event(TfaState* s, TfaNpc* n, const TfaOcEvent* e) {
    TfaOcellusState* os = oc_state(s);
    switch (e->kind) {
        case OCE_P4_START:
            os->phase = TFA_OCELLUS_P4;
            os->phase_start = s->tick;
            n->attackable = 1;
            if (n->hp * 100 < n->max_hp * TFA_OC3.p4_first_hp_pct) n->hp = n->max_hp * TFA_OC3.p4_first_hp_pct / 100;
            oc_want(s, PRAYER_NONE);
            oc_p4_step(s, n, OC4_RISE);
            break;
        case OCE_P4_STEP: oc_p4_step(s, n, e->arg); break;
        case OCE_P4_EXPLOSION:
            for (int p = 0; p < s->team_size; p++) {
                if (!tfa_alive(s, p)) continue;
                const Player* pl = &s->players[p].player;
                oc_haz(s, pl->x, pl->y, 1, 1, s->tick + 1, TFA_OC4.wind_min, TFA_OC4.wind_max, ATTACK_STYLE_NONE, 100, OC_GFX_VOID, p);
            }
            break;
        case OCE_P4_RIFT_BLOW: {
            TfaNpc* r = oc_find(s, e->arg, e->arg2);
            if (!r) break;
            for (int p = 0; p < s->team_size; p++) {
                const Player* pl = &s->players[p].player;
                if (tfa_alive(s, p) && chebyshev_distance(pl->x, pl->y, r->x, r->y) <= TFA_OC4.rift_radius)
                    tfa_damage_player_now(s, p, tfa_rand_range(s, TFA_OC4.rift_min, TFA_OC4.rift_max), ATTACK_STYLE_NONE);
            }
            r->active = 0;
            break;
        }
        default: abort();
    }
}

static void tfa_ocellus_enter(TfaState* s) {
    oc_p1_enter(s);
}

static void oc_dispatch(TfaState* s, TfaNpc* n, const TfaOcEvent* e) {
    if (e->kind <= OCE_P1_TRANSFORM) oc_p1_event(s, n, e);
    else if (e->kind == OCE_P2_START) oc_p2_enter(s, n);
    else if (e->kind <= OCE_P2_END) oc_p2_event(s, n, e);
    else if (e->kind <= OCE_P3_ANIM) oc_p3_event(s, n, e);
    else oc_p4_event(s, n, e);
}

static void tfa_ocellus_tick(TfaState* s, TfaContext* ctx) {
    TfaOcellusState* os = oc_state(s);
    if (os->dead) {
        tfa_room_cleared(s, ctx);
        return;
    }
    TfaNpc* n = oc_boss(s);
    if (os->phase == TFA_OCELLUS_P1) {
        if (os->barrage) oc_walk(n, OC_CORNERS[os->corner][0], OC_CORNERS[os->corner][1]);
        if (os->zero_tick >= 0 && os->zero_tick != -3) {
            os->zero_tick = -3;
            os->barrage = 0;
            oc_p1_zero_cut(s);
        }
        oc_lines_tick(s);
    }
    if (os->phase == TFA_OCELLUS_P2) oc_portals_tick(s);
    if (os->phase >= TFA_OCELLUS_P3) {
        oc_confine(s);
        oc_chase(s, n);
    }
    if (os->phase == TFA_OCELLUS_P4) oc_p4_orbs(s);
    for (int i = 0; i < TFA_OC_MAX_EVENTS; i++) {
        if (os->events[i].kind == OCE_NONE || os->events[i].tick != s->tick) continue;
        TfaOcEvent e = os->events[i];
        os->events[i].kind = OCE_NONE;
        if (e.kind == OCE_P1_BARRAGE) os->barrage = 1;
        if (e.kind == OCE_P1_CHARGE) os->barrage = 0;
        oc_dispatch(s, n, &e);
    }
    n->attack_timer = (int16_t)(os->next_attack_tick > s->tick ? os->next_attack_tick - s->tick : 0);
}

static int tfa_ocellus_npc_damage(TfaState* s, TfaNpc* n, int damage, int attacker, AttackStyle style) {
    TfaOcellusState* os = oc_state(s);
    if (n->kind == TFA_NPC_OCELLUS_BLADE) {
        os->blade_acc += damage;
        return damage;
    }
    if (n->kind == TFA_NPC_OCELLUS) {
        if (os->deflect == (int)style && style != ATTACK_STYLE_NONE) {
            if (attacker >= 0) tfa_damage_player_now(s, attacker, damage, ATTACK_STYLE_NONE);
            return 0;
        }
        if (os->soul_split) {
            oc_boss_heal(n, damage);
            return 0;
        }
        int floor_hp = os->phase == TFA_OCELLUS_P3 ? 1 : 0;
        int applied = damage < n->hp - floor_hp ? damage : n->hp - floor_hp;
        n->hp -= applied;
        return applied;
    }
    int applied = damage < n->hp ? damage : n->hp;
    n->hp -= applied;
    return applied;
}

static void tfa_ocellus_npc_died(TfaState* s, TfaNpc* n, int attacker) {
    (void)attacker;
    TfaOcellusState* os = oc_state(s);
    switch ((TfaNpcKind)n->kind) {
        case TFA_NPC_OCELLUS:
            n->attackable = 0;
            if (os->phase == TFA_OCELLUS_P1) os->zero_tick = os->zero_tick == -3 ? -3 : s->tick;
            else os->dead = 1;
            break;
        case TFA_NPC_VOID_SIPHON:
            tfa_npc_anim(s, n, OC_ANIM_SIPHON_DEATH);
            if (tfa_alive(s, n->target)) oc_heal_player(s, n->target, TFA_OC3.siphon_death_heal);
            n->active = 0;
            break;
        case TFA_NPC_VOID_AUGMENTER:
            tfa_npc_anim(s, n, OC_ANIM_AUGMENTER_DEATH);
            n->active = 0;
            break;
        case TFA_NPC_VOID_VINE:
            if (tfa_alive(s, n->target)) s->players[n->target].rooted_ticks = 0;
            n->active = 0;
            break;
        default:
            n->active = 0;
            break;
    }
}
