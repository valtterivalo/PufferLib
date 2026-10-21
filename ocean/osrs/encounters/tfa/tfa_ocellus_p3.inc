/* Ocellus, phases 3 and 4, and the room dispatch. Phase 3 is measured from 7 fights (the melee rotation, five specials, the add timer and the
   three quadrant destroys); phase 4 from the one fight that reached it (ocellus_08), the rest of it after the wiki, provisional. */

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
    int spin_debris_short, spin_debris_long; /* gfx 4346 at +3, +6 after the quadrant destroyed (6/6 spins); the long spin also checks the short spin's tick */
    int spin_loop;               /* 14870 at +5 in the long variant */
    int spin_to_next;            /* 14871 -> next attack +4 */
    int spin_check, spin_check_gap, injury_ticks; /* a protection prayer at t+4 injures (3 ticks locked) and the t+5 hit lands through it; off at t+4 and
                                                    back by t+5 blocks it (run 5: 6/6 spins, 17363 the one clean flick). One check and hit per destroyed quadrant
                                                    more, 3 apart: t+4/t+5, t+7/t+8 after one destroy (8/8), t+10/t+11 after two (3/3, 14871 at +11) */
    int void_first_min, void_first_max, void_tick_min, void_tick_max, void_tick_count; /* 62702: 14..16 at G+5, 6..7 every 2 for 12 ticks */
    int void_first;
    int unt_loop;                /* 14858 at +2 */
    int unt_dots_first, unt_dots_gap, unt_dots_count; /* gfx 4270 at +6 every 4, four batches */
    int unt_dot_hit, unt_dot_min, unt_dot_max, unt_dot_decoys; /* hits 4 later, 23..27 (n=12), 12 tiles per batch */
    int unt_wave_min, unt_wave_max; /* waves 17..27 under no Magic, 0 under Magic (12/12) at +10 +14 +18 +22 */
    int unt_pray_on, unt_pray_off;
    int unt_slam_tell, unt_slam_anim, unt_slam_hit; /* gfx 4320 on his centre +25, 14861 +29 = S */
    int unt_slam_out_min, unt_slam_out_max; /* at S everyone outside his 3x3 at S-1: 34..42 */
    int unt_slam_in_min, unt_slam_in_max;   /* at S+1 everyone inside it at S: 19..34 (runs 2-5, every player, both slams of run 5) */
    int unt_next;                /* 14857 -> next 14842 +37 */
    int destroy_hp_pct[3];       /* quadrant destroys at the first action slot with hp at or below 75, 50, 25% (bars 73-75, 51, 26 at the 14860 over 5 destroys),
                                    the third leads into phase 4 */
    int destroy_after_slam;      /* after an Untether 14860 comes 4 after the slam (run 4) */
    int destroy_in;              /* elsewhere "Obliteration..." and 14859 2 before the 14860, on the slot the next attack had (run 5, after a spin) */
    int destroy_end, destroy_move, destroy_boss, destroy_next; /* from the 14860: 14862 +9 (2/2), players moved +11, boss +16, first 14842 +19 (2/2) */
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
    .spin_check = 4, .spin_check_gap = 3, .injury_ticks = 3,
    .void_first_min = 14, .void_first_max = 16, .void_tick_min = 6, .void_tick_max = 7, .void_tick_count = 6,
    .void_first = 5,
    .unt_loop = 2,
    .unt_dots_first = 6, .unt_dots_gap = 4, .unt_dots_count = 4,
    .unt_dot_hit = 4, .unt_dot_min = 23, .unt_dot_max = 27, .unt_dot_decoys = 2,
    .unt_wave_min = 17, .unt_wave_max = 27,
    .unt_pray_on = 5, .unt_pray_off = 22,
    .unt_slam_tell = 25, .unt_slam_anim = 29, .unt_slam_hit = 29,
    .unt_slam_out_min = 34, .unt_slam_out_max = 42,
    .unt_slam_in_min = 19, .unt_slam_in_max = 34,
    .unt_next = 37,
    .destroy_hp_pct = { 75, 50, 25 },
    .destroy_after_slam = 4, .destroy_in = 2,
    .destroy_end = 9, .destroy_move = 11, .destroy_boss = 16, .destroy_next = 19,
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
};

typedef struct {
    int step_len[9];             /* one 60-tick cycle from 14863 at C (anims: ocellus_08 3 cycles, ocellus_11 1): rise C, wind C+4, land 14865 C+21,
                                    quadrant attack 14844 C+24, void 14855 C+34, wind C+35, void end 14856 C+50, quadrant attack 14844 C+52, rifts C+59, next
                                    rise C+60; the winds hit at C+4 and C+35 in 5/5 halves */
    int drop_first, drops, drop_gap; /* void drops gfx 4339 at wind +5 +7 ... +15 (6/6, ocellus_11), each on the tile the raider held the tick before;
                                    still there next tick it hits */
    int quad_hit;                /* quadrant attack hit 5 after the 14844 (6/6 across ocellus_08 and 11) */
    int quad_protected_pct;      /* one roll for everyone (equal hits on all raiders, 8/8), 6..10 lands through the right prayer; unprotected is unrecorded */
    int switch_min, switch_max;  /* tells per fight (runs 11-13): 4329 x2; 4329 x5 then 4330 x2; 4328 x2 then 4330. The opening style is melee or magic
                                    (ranged 0/3), held for 2..5 attacks, then ranged for good: provisional */
    int orb_a_from, orb_a_until; /* soul fragment 4311 spawns at wind-1, collectable (idle 4310) wind+12..wind+24, heals that raider's wind (+40 for a 40) */
    int orb_b_from, orb_b_until; /* the 6-tick variant 4309 after the second wind: wind+6..wind+18 */
    int split_from, split_ticks; /* soul split overhead (sprite 11) from 1 after each 14844 for 5 ticks, one 50 heal splat in it (ocellus_11, 2/2) */
    int deflect_range_from;      /* Deflect Range (sprite 16) from 7 after the first 14844 (ocellus_11, 1/1) */
    int ring;
    int wind_min, wind_max;      /* void drop damage: 16 (ocellus_11, under Protect from Melee), 15..16 in ocellus_08: unprayable */
    int quad_min, quad_max;      /* unprotected quadrant attack: provisional, 20% of it is the recorded 6..10 */
    int void_min, void_max;
    int rifts_max;               /* 2, 3, 4 over the three recorded cycles; held at 4 after that (unrecorded, provisional) */
    int rift_life, rift_radius, rift_min, rift_max;
} TfaOcellusP4Tuning;

/* Phase 4 from ocellus_08 (one raider alive through 3 cycles): "End..." 16 after the third destroy, the first rise 3 later. The wind takes half
   of current hp (27, 29, 24, 18, 6 on that raider) and the raider's orb heals exactly that (4314 at pickup, 4/4). Rifts come one tick before
   each rise from the second, one more each cycle (2, 3, 4). Which visual asks for which prayer is still unknown, so the quadrant style is drawn. */
static const TfaOcellusP4Tuning TFA_OC4 = {
    .step_len = { 4, 17, 3, 10, 1, 15, 2, 7, 1 },
    .drop_first = 5, .drops = 6, .drop_gap = 2,
    .quad_hit = 5,
    .quad_protected_pct = 20,
    .switch_min = 2, .switch_max = 5,
    .orb_a_from = 12, .orb_a_until = 24, .orb_b_from = 6, .orb_b_until = 18,
    .split_from = 1, .split_ticks = 5,
    .deflect_range_from = 7,
    .ring = 2,
    .wind_min = 15, .wind_max = 16,
    .quad_min = 30, .quad_max = 50,
    .void_min = 8, .void_max = 12,
    .rifts_max = 4,
    .rift_life = 12, .rift_radius = 3, .rift_min = 40, .rift_max = 48,
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
    n->max_hp = tfa_party_hp(s->team_size, TFA_OC1.hp_base_p3, TFA_OC1.hp_base_p3 * TFA_TRIO_SCALE_PCT / 100);
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

static void oc_p3_event(TfaState* s, TfaNpc* n, const TfaOcEvent* e);

static int oc_destroy_due(const TfaState* s, const TfaNpc* n) {
    const TfaOcellusState* os = &s->boss.ocellus;
    return os->destroyed < 3 && n->hp * 100 <= TFA_OC3.destroy_hp_pct[os->destroyed] * n->max_hp;
}

/* The quadrant destroy from the 14860 at delay 0 (now) or later: it replaces the pending attack, the second one leads into phase 4. */
static void oc_destroy(TfaState* s, TfaNpc* n, int delay) {
    TfaOcellusState* os = oc_state(s);
    oc_cancel(s, OCE_P3_ATTACK, OCE_P3_ATTACK);
    if (delay) oc_at(s, delay, OCE_P3_DESTROY, 0, 0);
    else {
        TfaOcEvent now = { .tick = s->tick, .kind = OCE_P3_DESTROY };
        oc_p3_event(s, n, &now);
    }
    oc_at(s, delay + TFA_OC3.destroy_end, OCE_P3_DESTROY_END, 0, 0);
    oc_at(s, delay + TFA_OC3.destroy_move, OCE_P3_DESTROY_MOVE, 0, 0);
    oc_at(s, delay + TFA_OC3.destroy_boss, OCE_P3_DESTROY_MOVE, 1, 0);
    int next = delay + TFA_OC3.destroy_next;
    oc_at(s, next, os->destroyed + (delay ? 1 : 0) == 3 ? OCE_P4_START : OCE_P3_ATTACK, 0, 0);
    os->pause_until = s->tick + delay + TFA_OC3.destroy_boss + 2;
    os->busy_until = s->tick + next;
    os->next_attack_tick = s->tick + next;
}

static void oc_p3_attack(TfaState* s, TfaNpc* n) {
    TfaOcellusState* os = oc_state(s);
    n->act = TFA_ACT_IDLE;
    os->busy_until = s->tick;
    if (oc_destroy_due(s, n)) {
        tfa_npc_anim(s, n, OC_ANIM_DESTROY_IN);
        oc_destroy(s, n, TFA_OC3.destroy_in);
        return;
    }
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
    int pairs = (os->destroyed < 2 ? os->destroyed : 2) + 1;
    int g = os->destroyed ? TFA_OC3.spin_debris_long : TFA_OC3.spin_debris_short;
    n->act = TFA_ACT_SPECIAL;
    tfa_npc_anim(s, n, OC_ANIM_SPIN);
    os->spins++;
    if (os->spins % TFA_OC3.vine_every == 1) os->vine_pending = 1;
    oc_whole_room(s, s->tick + 1, TFA_OC3.spin_hit_min, TFA_OC3.spin_hit_max, ATTACK_STYLE_MELEE, 0);
    oc_at(s, g, OCE_P3_SPIN_DEBRIS, 0, 0);
    int last_hit = 0;
    for (int k = 0; k < pairs; k++) {
        int check = TFA_OC3.spin_check + TFA_OC3.spin_check_gap * k;
        last_hit = check + 1;
        oc_whole_room(s, s->tick + last_hit, TFA_OC3.spin_hit_min, TFA_OC3.spin_hit_max, ATTACK_STYLE_MELEE, 0);
        oc_at(s, check, OCE_P3_SPIN_INJURY, 0, 0);
        oc_at(s, check - 1, OCE_P3_WANT, PRAYER_NONE, 0);
        oc_at(s, check, OCE_P3_WANT, PRAYER_PROTECT_MELEE, 0);
    }
    if (pairs > 1) oc_at(s, TFA_OC3.spin_loop, OCE_P3_ANIM, OC_ANIM_SPIN_LOOP, 0);
    oc_at(s, last_hit, OCE_P3_ANIM, OC_ANIM_SPIN_END, 0);
    int next = last_hit + TFA_OC3.spin_to_next;
    oc_at(s, next, OCE_P3_ATTACK, 0, 0);
    oc_at(s, next - TFA_OC3.destroy_in, OCE_P3_DESTROY_CHECK, 1, 0);
    os->next_attack_tick = s->tick + next;
    os->busy_until = s->tick + next;
}

static void oc_untether(TfaState* s, TfaNpc* n) {
    TfaOcellusState* os = oc_state(s);
    n->act = TFA_ACT_SPECIAL;
    tfa_npc_anim(s, n, OC_ANIM_UNTETHER);
    oc_at(s, TFA_OC3.unt_loop, OCE_P3_ANIM, OC_ANIM_UNTETHER_LOOP, 0);
    oc_at(s, TFA_OC3.unt_dots_first, OCE_P3_UNTETHER_DOTS, 0, 0);
    oc_at(s, TFA_OC3.unt_pray_on, OCE_P3_WANT, PRAYER_PROTECT_MAGIC, 0);
    oc_at(s, TFA_OC3.unt_pray_off, OCE_P3_WANT, PRAYER_PROTECT_MELEE, 0);
    oc_at(s, TFA_OC3.unt_slam_tell, OCE_P3_SLAM, 0, 0);
    oc_at(s, TFA_OC3.unt_slam_anim, OCE_P3_ANIM, OC_ANIM_SLAM, 0);
    int next = TFA_OC3.unt_next;
    os->busy_until = s->tick + TFA_OC3.unt_slam_hit + 1;
    os->pause_until = s->tick + TFA_OC3.unt_slam_anim + 3;
    oc_at(s, next, OCE_P3_ATTACK, 0, 0);
    oc_at(s, TFA_OC3.unt_slam_hit + TFA_OC3.destroy_after_slam, OCE_P3_DESTROY_CHECK, 0, 0);
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
            if (oc_destroy_due(s, n)) {
                tfa_npc_anim(s, n, OC_ANIM_DESTROY_IN);
                oc_destroy(s, n, TFA_OC3.destroy_in);
            } else if (e->arg % 2 == 0) oc_spin(s, n);
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
                    oc_haz_tile(s, x, y, s->tick + TFA_OC3.void_first + 2 * k, TFA_OC3.void_tick_min, TFA_OC3.void_tick_max, OC_LOC_VOID);
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
            int out = s->tick + TFA_OC3.unt_slam_hit - TFA_OC3.unt_slam_tell, fx = n->x, fy = n->y, w = n->size;
            oc_haz(s, 0, 0, fx, TFA_ROOM_SIZE, out, TFA_OC3.unt_slam_out_min, TFA_OC3.unt_slam_out_max, ATTACK_STYLE_NONE, 100, 0, -1);
            oc_haz(s, fx + w, 0, TFA_ROOM_SIZE - fx - w, TFA_ROOM_SIZE, out, TFA_OC3.unt_slam_out_min, TFA_OC3.unt_slam_out_max, ATTACK_STYLE_NONE, 100, 0, -1);
            oc_haz(s, fx, 0, w, fy, out, TFA_OC3.unt_slam_out_min, TFA_OC3.unt_slam_out_max, ATTACK_STYLE_NONE, 100, 0, -1);
            oc_haz(s, fx, fy + w, w, TFA_ROOM_SIZE - fy - w, out, TFA_OC3.unt_slam_out_min, TFA_OC3.unt_slam_out_max, ATTACK_STYLE_NONE, 100, 0, -1);
            oc_haz(s, fx, fy, w, w, out + 1, TFA_OC3.unt_slam_in_min, TFA_OC3.unt_slam_in_max, ATTACK_STYLE_NONE, 100, 0, -1);
            tfa_fx_ground(s, fx + w / 2, fy + w / 2, OC_GFX_SLAM, 0);
            os->slam_tick = out;
            break;
        }
        case OCE_P3_DESTROY_CHECK:
            if (!oc_destroy_due(s, n)) break;
            if (e->arg) {
                tfa_npc_anim(s, n, OC_ANIM_DESTROY_IN);
                oc_destroy(s, n, TFA_OC3.destroy_in);
            } else {
                oc_destroy(s, n, 0);
            }
            break;
        case OCE_P3_INSOLENCE: {
            int target = oc_random_alive(s);
            tfa_npc_anim(s, n, OC_ANIM_INSOLENCE);
            oc_ray(s, n, target, s->tick + TFA_OC3.ins_hit);
            break;
        }
        case OCE_P3_DESTROY:
            tfa_npc_anim(s, n, OC_ANIM_DESTROY);
            os->free_roam_until = s->tick + TFA_OC3.destroy_move + 1;
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
    OC4_RISE = 0, OC4_WIND_A, OC4_LAND, OC4_QUAD_A, OC4_VOID, OC4_WIND_B, OC4_VOID_END, OC4_QUAD_B, OC4_RIFTS, OC4_STEPS,
} TfaOc4Step;

static void oc_p4_ring(TfaState* s, const TfaNpc* n, int inverse, int start, int end, int min, int max) {
    int x = n->x - 1, y = n->y - 1, side = n->size + 2;
    if (!inverse) {
        tfa_hazard_add(s, (TfaHazard){ .kind = TFA_HAZ_FLOOR, .style = ATTACK_STYLE_NONE, .w = (uint8_t)side, .h = (uint8_t)side,
            .owner = -1, .x = (int16_t)x, .y = (int16_t)y, .start_tick = start, .end_tick = end,
            .min_damage = (int16_t)min, .max_damage = (int16_t)max, .protected_pct = 100, .loc_id = OC_LOC_VOID });
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
            .min_damage = (int16_t)min, .max_damage = (int16_t)max, .protected_pct = 100, .loc_id = OC_LOC_VOID });
    }
}

static void oc_p4_wind(TfaState* s, TfaNpc* n, int second) {
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
        os->orb_from[p] = s->tick + (second ? TFA_OC4.orb_b_from : TFA_OC4.orb_a_from);
        os->orb_until[p] = s->tick + (second ? TFA_OC4.orb_b_until : TFA_OC4.orb_a_until);
        tfa_fx_ground(s, x, y, second ? OC_GFX_ORB_SHORT : OC_GFX_ORB, 0);
    }
    for (int k = 0; k < TFA_OC4.drops; k++) oc_at(s, TFA_OC4.drop_first + TFA_OC4.drop_gap * k, OCE_P4_EXPLOSION, 0, 0);
}

static void oc_p4_orbs(TfaState* s) {
    TfaOcellusState* os = oc_state(s);
    for (int p = 0; p < s->team_size; p++) {
        if (os->orb_heal[p] <= 0 || !tfa_alive(s, p) || s->tick < os->orb_from[p]) continue;
        if (s->tick > os->orb_until[p]) {
            os->orb_heal[p] = 0;
            continue;
        }
        const Player* pl = &s->players[p].player;
        if (pl->x != os->orb_x[p] || pl->y != os->orb_y[p]) continue;
        oc_heal_player(s, p, os->orb_heal[p]);
        os->orb_heal[p] = 0;
    }
}

/* Overheads (ocellus_11): Deflect Melee (sprite 15) from the rise until the first 14844 +1, Soul Split (11) for 5 ticks from each 14844 +1,
   Deflect Range (16) from the first 14844 +7 until the second +1. A deflected style hits the attacker instead ("Ocellus deflects your damage",
   a blowpipe into 16 reflected 9 and 5). */
static void oc_p4_quadrant(TfaState* s, TfaNpc* n, int second) {
    tfa_npc_anim(s, n, OC_ANIM_QUADRANT_ATTACK);
    oc_at(s, TFA_OC4.split_from, OCE_P4_SPLIT, 1, 0);
    oc_at(s, TFA_OC4.split_from + TFA_OC4.split_ticks, OCE_P4_SPLIT, 0, 0);
    oc_at(s, TFA_OC4.split_from, OCE_P4_DEFLECT, ATTACK_STYLE_NONE, 0);
    if (!second) oc_at(s, TFA_OC4.deflect_range_from, OCE_P4_DEFLECT, ATTACK_STYLE_RANGED, 0);
    TfaOcellusState* os = oc_state(s);
    if (os->p4_quads++ == os->p4_switch_at) os->p4_style = ATTACK_STYLE_RANGED;
    AttackStyle style = (AttackStyle)os->p4_style;
    n->act = style == ATTACK_STYLE_MELEE ? TFA_ACT_MELEE : style == ATTACK_STYLE_RANGED ? TFA_ACT_RANGED : TFA_ACT_MAGIC;
    oc_want(s, style == ATTACK_STYLE_MELEE ? PRAYER_PROTECT_MELEE : style == ATTACK_STYLE_RANGED ? PRAYER_PROTECT_RANGED : PRAYER_PROTECT_MAGIC);
    int dmg = tfa_rand_range(s, TFA_OC4.quad_min, TFA_OC4.quad_max);
    for (int p = 0; p < s->team_size; p++)
        if (tfa_alive(s, p)) tfa_queue_npc_hit_land(s, p, dmg, style, TFA_OC4.quad_hit, TFA_OC4.quad_protected_pct, n);
}

static void oc_p4_step(TfaState* s, TfaNpc* n, int step) {
    TfaOcellusState* os = oc_state(s);
    n->act = TFA_ACT_IDLE;
    switch ((TfaOc4Step)step) {
        case OC4_RISE:
            tfa_npc_anim(s, n, OC_ANIM_DEFLECT_IN);
            os->deflect = ATTACK_STYLE_MELEE;
            oc_p4_ring(s, n, 0, s->tick + 1, s->tick + TFA_OC4.step_len[OC4_RISE] + TFA_OC4.step_len[OC4_WIND_A], TFA_OC4.void_min, TFA_OC4.void_max);
            break;
        case OC4_WIND_A:
        case OC4_WIND_B:
            oc_p4_wind(s, n, step == OC4_WIND_B);
            break;
        case OC4_LAND:
            tfa_npc_anim(s, n, OC_ANIM_DEFLECT_OUT);
            break;
        case OC4_QUAD_A:
        case OC4_QUAD_B:
            oc_p4_quadrant(s, n, step == OC4_QUAD_B);
            break;
        case OC4_VOID:
            tfa_npc_anim(s, n, OC_ANIM_VOID_IN);
            oc_p4_ring(s, n, 1, s->tick + 1, s->tick + TFA_OC4.step_len[OC4_VOID] + TFA_OC4.step_len[OC4_WIND_B], TFA_OC4.void_min, TFA_OC4.void_max);
            break;
        case OC4_VOID_END:
            tfa_npc_anim(s, n, OC_ANIM_VOID_OUT);
            break;
        case OC4_RIFTS:
            for (int k = 0; k < os->p4_cycle + 2 && k < TFA_OC4.rifts_max; k++) {
                int x, y;
                oc_add_tile(s, oc_cur_quad(os), &x, &y);
                TfaNpc* r = tfa_npc_spawn(s, TFA_NPC_VOID_RIFT, MON_TFA_VOID_RIFT, OC_NPC_RIFT, x, y);
                oc_at(s, TFA_OC4.rift_life, OCE_P4_RIFT_BLOW, tfa_npc_slot(s, r), (int)r->instance_id);
            }
            break;
        case OC4_STEPS: abort();
    }
    int len = TFA_OC4.step_len[step];
    os->p4_step = (step + 1) % OC4_STEPS;
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
            os->p4_style = tfa_rand(s, 2) ? ATTACK_STYLE_MAGIC : ATTACK_STYLE_MELEE;
            os->p4_switch_at = tfa_rand_range(s, TFA_OC4.switch_min, TFA_OC4.switch_max);
            oc_cancel(s, OCE_P3_ADD_DUE, OCE_P3_ADD_DUE);
            oc_cancel(s, OCE_P3_SIPHON_PULSE, OCE_P3_AUGMENTER_BLOW);
            n->attackable = 1;
            oc_want(s, PRAYER_NONE);
            oc_p4_step(s, n, OC4_RISE);
            break;
        case OCE_P4_STEP: oc_p4_step(s, n, e->arg); break;
        case OCE_P4_SPLIT: os->soul_split = e->arg; break;
        case OCE_P4_DEFLECT: os->deflect = e->arg; break;
        case OCE_P4_EXPLOSION:
            for (int p = 0; p < s->team_size; p++) {
                if (!tfa_alive(s, p)) continue;
                const Player* pl = &s->players[p].player;
                oc_haz(s, pl->x, pl->y, 1, 1, s->tick + 1, TFA_OC4.wind_min, TFA_OC4.wind_max, ATTACK_STYLE_NONE, 100, OC_GFX_VOID_DROP, p);
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

static int oc_due(const TfaState* s, int kind, int tick) {
    const TfaOcellusState* os = &s->boss.ocellus;
    for (int i = 0; i < TFA_OC_MAX_EVENTS; i++)
        if (os->events[i].kind == kind && os->events[i].tick == tick) return 1;
    return 0;
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
        if (e.kind == OCE_P1_BARRAGE) os->barrage = 1, os->guard = 1;
        if (e.kind == OCE_P1_CHARGE) os->barrage = 0;
        if (e.kind == OCE_P1_EXPEND) os->guard_from = s->tick + 1;
        oc_dispatch(s, n, &e);
    }
    /* P1 prays Melee from each barrage (or the tick after an expend) and drops it the tick before the charge or teleport that ends it, or on a
       teleport straight out of the barrage: ocellus_10's overheads, 13 of 13 changes. */
    if (os->guard_from == s->tick) os->guard = 1;
    if (oc_due(s, OCE_P1_CHARGE, s->tick + 1) || oc_due(s, OCE_P1_TP_OUT, s->tick + 1) || (n->anim_id == OC_ANIM_TP_OUT && n->anim_tick == s->tick))
        os->guard = 0;
    n->overhead = (int8_t)(os->phase == TFA_OCELLUS_P1 && os->guard ? PRAYER_PROTECT_MELEE
        : os->phase == TFA_OCELLUS_P4 && os->deflect == ATTACK_STYLE_MELEE ? PRAYER_PROTECT_MELEE
        : os->phase == TFA_OCELLUS_P4 && os->deflect == ATTACK_STYLE_RANGED ? PRAYER_PROTECT_RANGED : PRAYER_NONE);
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
        if (n->overhead != PRAYER_NONE && encounter_prayer_correct_for_style(n->overhead, style)) return 0;
        int floor_hp = os->phase == TFA_OCELLUS_P3 ? 1 : 0;
        int applied = damage < n->hp - floor_hp ? damage : n->hp - floor_hp;
        n->hp -= applied;
        return applied;
    }
    int applied = damage < n->hp ? damage : n->hp;
    n->hp -= applied;
    return applied;
}

/* Soul Split heals Ocellus by the damage of his own hits that land on an unprotected raider while it is up: his P4 heals in run 13 were 44
   and 44 against one unprayed 44, none when every raider prayed (5 attacks), and raiders' hits on him in the window land as usual (10 of 23). */
static void tfa_ocellus_npc_hit_player(TfaState* s, TfaNpc* src, int raw, int dealt) {
    if (s->boss.ocellus.soul_split && src->kind == TFA_NPC_OCELLUS && dealt == raw && dealt > 0) oc_boss_heal(src, dealt);
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
