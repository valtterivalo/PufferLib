/* Ocellus, phases 1 and 2. A scripted fight measured from 6 recorded fights (fixtures/tfa_ocellus_events.tsv).
   Every action is an event on a small queue: handlers schedule their successors at a delay of at least one tick. */

#define OC_NPC_P1 16674
#define OC_NPC_P2 16675
#define OC_NPC_BLADE 16676
#define OC_NPC_P3 16677
#define OC_NPC_RIFT 16678
#define OC_NPC_SIPHON 16679
#define OC_NPC_AUGMENTER 16680
#define OC_NPC_VINE 16681

#define OC_ANIM_BARRAGE 14806
#define OC_ANIM_BARRAGE_END 14807
#define OC_ANIM_CHARGE 14809
#define OC_ANIM_EXPEND 14810
#define OC_ANIM_EXPLODE 14811
#define OC_ANIM_TP_OUT 14812
#define OC_ANIM_TP_IN 14813
#define OC_ANIM_P1_TRANSFORM 14814
#define OC_ANIM_P2_TRANSFORM 14837
#define OC_ANIM_MELEE 14842
#define OC_ANIM_SLASH 14846
#define OC_ANIM_INSOLENCE 14848
#define OC_ANIM_MAGIC 14851
#define OC_ANIM_UNTETHER 14857
#define OC_ANIM_UNTETHER_LOOP 14858
#define OC_ANIM_DESTROY_IN 14859
#define OC_ANIM_DESTROY 14860
#define OC_ANIM_SLAM 14861
#define OC_ANIM_DESTROY_END 14862
#define OC_ANIM_DEFLECT_IN 14863
#define OC_ANIM_DEFLECT_OUT 14865
#define OC_ANIM_SOUL_SPLIT 14855
#define OC_ANIM_SPIN 14869
#define OC_ANIM_SPIN_LOOP 14870
#define OC_ANIM_SPIN_END 14871
#define OC_ANIM_P2_END 14878
#define OC_ANIM_P3_START 14881
#define OC_ANIM_SIPHON_SPAWN 14900
#define OC_ANIM_SIPHON_DEATH 14901
#define OC_ANIM_AUGMENTER_SPAWN 14904
#define OC_ANIM_AUGMENTER_DEATH 14905
#define OC_ANIM_AUGMENTER_BLOW 14906

#define OC_GFX_EXPLODE 4262
#define OC_LOC_LANE_PORTAL 62685
#define OC_GFX_DOT 4270
#define OC_GFX_SAFE_PORTAL 4271
#define OC_GFX_BOMB 4299
#define OC_GFX_SLASH 4335
#define OC_GFX_INSOLENCE 4336
#define OC_GFX_SLAM 4320
#define OC_GFX_DEBRIS 4346
#define OC_GFX_SWORD_DROP 4388
#define OC_LOC_BLADE 62706
#define OC_LOC_BLADE_FALLING 62707
#define OC_LOC_CENTRE 62955
#define OC_CENTRE_HIDE_FIRST 1       /* centre piece OBJ- 1 after 14813, 5/5 middles */
#define OC_CENTRE_HIDE_LAST 36       /* OBJ+ 36 after 14813, 5/5 */
#define OC_CENTRE_GONE_P2 9          /* OBJ- 9 after 14814, 2/2 */
#define OC_BLADE_FALL_TICKS 32       /* 62707 replaces 62706 when the blade spawns, OBJ- 32 later, 2/2 */
#define OC_LOC_VOID 62702

typedef enum {
    OCE_NONE = 0,
    OCE_P1_BARRAGE,
    OCE_P1_SHOT,
    OCE_P1_BARRAGE_END,
    OCE_P1_CHARGE,
    OCE_P1_LINE,
    OCE_P1_EXPLODE_TELL,
    OCE_P1_EXPLODE,
    OCE_P1_AFTER_EXPLODE,
    OCE_P1_TP_OUT,
    OCE_P1_TP_IN,
    OCE_P1_MID_CHARGE,
    OCE_P1_MID_EXPLODE,
    OCE_P1_EXPEND,
    OCE_P1_DYING_IN,
    OCE_P1_TRANSFORM,
    OCE_P2_START,
    OCE_P2_VOLLEY,
    OCE_P2_SPECIAL,
    OCE_P2_PORTAL,
    OCE_P2_FIRE_ALL,
    OCE_P2_CONVERT,
    OCE_P2_END,
    OCE_P3_START,
    OCE_P3_ARRIVE,
    OCE_P3_ATTACK,
    OCE_P3_SPECIAL,
    OCE_P3_SPIN_DEBRIS,
    OCE_P3_SPIN_INJURY,
    OCE_P3_UNTETHER_DOTS,
    OCE_P3_SLAM,
    OCE_P3_DESTROY_CHECK,
    OCE_P3_INSOLENCE,
    OCE_P3_DESTROY,
    OCE_P3_DESTROY_MOVE,
    OCE_P3_DESTROY_END,
    OCE_P3_BOMB,
    OCE_P3_VINE_SPAWN,
    OCE_P3_VINE_BREAK,
    OCE_P3_ADD_DUE,
    OCE_P3_SIPHON_PULSE,
    OCE_P3_AUGMENTER_BLOW,
    OCE_P3_WANT,
    OCE_P3_ANIM,
    OCE_P4_START,
    OCE_P4_STEP,
    OCE_P4_EXPLOSION,
    OCE_P4_RIFT_BLOW,
} TfaOcEventKind;

typedef struct {
    int first_barrage;           /* npc 16674 -> first 14806: 4/4 */
    int shot_first;              /* 14806 -> first projectile +1 (29/35) */
    int shot_gap;                /* then every 5 (4-6) */
    int window_fresh;            /* 14806 -> 14809 after the start or a middle: 11 in 9/9 */
    int window_corner;           /* 14806 -> 14809 corner to corner: 16 in 22/22 */
    int shot_to_end;             /* a middle check runs 4 after every shot slot (cuts at B+5 f4, B+10 f3); 14807 plays at B+5 (9 of 12) or +10 when the charge follows at +11, +10 or +15 when it follows at +16 (11 and 11); 2 and 3 shots in 55/55 barrages */
    int flight_near, flight_far; /* projectile flight 2 up to 4 tiles, 3 from 5 (n=270) */
    int flight_split;
    int magic_pct;               /* 4229 in 135 of 235 shots */
    int shot_min, shot_max;      /* unprayed hits 12 13 14 15 15 19 (n=6), blocked 0 on 117/117 prayed */
    int corner_charge_to_explode;/* 14809 -> 14811 +18 in 26/27 */
    int middle_charge_to_explode;/* +6 in 28/28 */
    int explode_to_next;         /* 14811 -> next 14806 or 14812 +3 (22, 5) */
    int explode_tell;            /* overhead line 3 before the explode */
    int explode_radius;          /* hits at Chebyshev 2..3 from his centre, safe from 4 (33 hits, 28 safe samples) */
    int explode_min, explode_max;/* 25..35 (n=33) */
    int lines_corner;            /* 7 lines at +0 +2 .. +12 after the corner 14809 (35 corners, 245 lines) */
    int line_gap;
    int corner_line_len;         /* 8 tiles out from the footprint edge (245/245) */
    int corner_line_hit;         /* END gfx and the chip 5 after the crack (245/245 lines, one pulse each) */
    int corner_line_delay;       /* START gfx delay 61 cycles at the far end, bodies +1 per tile toward him (245/245) */
    int mid_line_len;            /* 6 tiles per V line (88/88) */
    int mid_hit[2];              /* line A ENDs 4 after the charge, line B 6 after (44/44 each) */
    int mid_delay[2];            /* START delay 1 cycle (A) and 61 (B), bodies +1 per tile (44/44 each) */
    int tendril_min, tendril_max;/* chip 3..5 */
    int tendril_boss_heal;       /* boss heals 2 per damaged player on a pulse (hitsplat type 6 on the boss, 2/4/6) */
    int tendril_heal_window;     /* the player heals what lines took in the last 6 ticks (13 = 5+4+4) */
    int meter_threshold;         /* provisional: lead note says 22 unblocked heals before Eradicate */
    int middle_pct[2];           /* middle sequences at hp <= 67 and <= 26 percent: triggered at 66.9, 66.9, 25.4, 24.4, 62.6 (B+5), not at 67.3, 68.1, 68.4, 26.5 (checked at 14811 +3 and every 5 ticks of a barrage) */
    int tp_in;                   /* 14812 -> 14813 +4 (11/14) */
    int mid_charges;             /* 4 charges at the centre */
    int mid_gap;                 /* explode -> next charge +2 */
    int mid_first_charge;        /* 14813 -> 14809 +4 (7/7) */
    int expend_text;             /* line at last explode +2, 14810 at +4 */
    int expend_after;
    int expend_hit_min, expend_hit_max; /* 17..19 on 12/12 sequences */
    int eradicate_hits;          /* 6 hits per player after a full meter (fights 1, 2) */
    int eradicate_ticks[6];
    int resume;                  /* 14810 -> 14806 +7 (6/6) */
    int death_out, death_in, death_transform, death_p2, death_centre; /* hit label of hp 0 +3 (so +2 after the tick that applied it), +3, +2, +7 (4/4); a boss already at the centre transforms +1 after (f4, f5) */
    int hp_base_p1, hp_trio_p1;  /* 2500 duo (wiki), 3630 at 3 players (4/4 fights: damage 3630 incl. heals) */
    int hp_base_p3;
} TfaOcellusP1Tuning;

static const TfaOcellusP1Tuning TFA_OC1 = {
    .first_barrage = 4, .shot_first = 1, .shot_gap = 5,
    .window_fresh = 11, .window_corner = 16,
    .shot_to_end = 4,
    .flight_near = 2, .flight_far = 3, .flight_split = 4,
    .magic_pct = 57,
    .shot_min = 12, .shot_max = 19,
    .corner_charge_to_explode = 18, .middle_charge_to_explode = 6,
    .explode_to_next = 3, .explode_tell = 3, .explode_radius = 3,
    .explode_min = 25, .explode_max = 35,
    .lines_corner = 7, .line_gap = 2,
    .corner_line_len = 8, .corner_line_hit = 5, .corner_line_delay = 61,
    .mid_line_len = 6, .mid_hit = { 4, 6 }, .mid_delay = { 1, 61 },
    .tendril_min = 3, .tendril_max = 5, .tendril_boss_heal = 2, .tendril_heal_window = 6,
    .meter_threshold = 22,
    .middle_pct = { 67, 26 },
    .tp_in = 4, .mid_charges = 4, .mid_gap = 2, .mid_first_charge = 4,
    .expend_text = 2, .expend_after = 2,
    .expend_hit_min = 17, .expend_hit_max = 19,
    .eradicate_hits = 6, .eradicate_ticks = { 1, 2, 2, 2, 3, 3 },
    .resume = 7,
    .death_out = 2, .death_in = 3, .death_transform = 2, .death_p2 = 7, .death_centre = 3,
    .hp_base_p1 = 2500, .hp_trio_p1 = 3630,
    .hp_base_p3 = 5250,
};

typedef struct {
    int duration;                /* 14837 -> 14878 +123 in 5/5 */
    int blade_spawn;             /* The Blade appears at +7 (fight 3) */
    int volley_first;            /* first activation +11, then every 5 */
    int volley_gap;
    int volley_count;            /* six normal volleys per 42 tick period */
    int period;                  /* safe portal events at +39 and +81: period 42 */
    int special_lit;             /* all portals lit at period +42 (d407-365) */
    int special_portal;          /* portal gfx 4271 at +44 (d409) */
    int special_fire;            /* all-lit volley at portal +7 (d416, d458) */
    int fire_delay;              /* lit portals fire 4 after the activation */
    int near_len;                /* far tiles are hit 1 tick later: near up to 5 tiles from the wall (provisional split) */
    int lane_len;                /* 14 tiles from the wall */
    int lane_min, lane_max;      /* 19..25 on 13/13 hits, unprayable */
    int convert[3];              /* Ocellus takes half the Blade damage at +46, +88, +123 (389 of 780) */
    int convert_pct;
    int drop_hit;                /* sword drop 14878 -> hit +2 under the boss spot, 19..23 (5 hits) */
    int drop_min, drop_max;
    int to_p3;                   /* 14878 -> 14881 +8 (5/5) */
    int portal_dx, portal_dy;    /* safe portal tiles 24,55 and 37,55 relative to the cross 31,49: 6 or 7 out, 6 along */
} TfaOcellusP2Tuning;

static const TfaOcellusP2Tuning TFA_OC2 = {
    .duration = 123, .blade_spawn = 7,
    .volley_first = 11, .volley_gap = 5, .volley_count = 6, .period = 42,
    .special_lit = 42, .special_portal = 44, .special_fire = 7, .fire_delay = 4,
    .near_len = 5, .lane_len = 14,
    .lane_min = 19, .lane_max = 25,
    .convert = { 46, 88, 123 }, .convert_pct = 50,
    .drop_hit = 2, .drop_min = 19, .drop_max = 23,
    .to_p3 = 8,
    .portal_dx = 6, .portal_dy = 6,
};

#define OC_CROSS_X 31
#define OC_CROSS_Y 49
static const int8_t OC_CORNERS[4][2] = { {24, 54}, {24, 42}, {36, 42}, {36, 54} };
#define OC_CENTRE_X 30
#define OC_CENTRE_Y 48
/* Safe tile of middle explosion k, relative to his centre: 24 of 28 samples on these tiles unhurt, hit on the other three. */
static const int8_t OC_PINWHEEL[4][2] = { {-2, 1}, {1, 2}, {2, -1}, {-1, -2} };

/* The safe portal carries a player clockwise: (-,+) to (+,+) to (+,-) to (-,-). */
static const int8_t OC_NEXT_QUAD[4] = { 2, 0, 3, 1 };

static TfaOcellusState* oc_state(TfaState* s) { return &s->boss.ocellus; }

static TfaNpc* oc_boss(TfaState* s) {
    TfaNpc* n = &s->npcs[0];
    assert(n->active && n->kind == TFA_NPC_OCELLUS);
    return n;
}

static void oc_at(TfaState* s, int delay, int kind, int arg, int arg2) {
    assert(delay >= 1);
    TfaOcellusState* os = oc_state(s);
    for (int i = 0; i < TFA_OC_MAX_EVENTS; i++) {
        if (os->events[i].kind != OCE_NONE) continue;
        os->events[i] = (TfaOcEvent){ s->tick + delay, (int16_t)kind, (int16_t)arg, arg2 };
        return;
    }
    fprintf(stderr, "ocellus event queue full\n");
    abort();
}

static void oc_cancel(TfaState* s, int lo, int hi) {
    TfaOcellusState* os = oc_state(s);
    for (int i = 0; i < TFA_OC_MAX_EVENTS; i++)
        if (os->events[i].kind >= lo && os->events[i].kind <= hi) os->events[i].kind = OCE_NONE;
}

static int oc_sign(int v) { return (v > 0) - (v < 0); }

static void oc_haz(TfaState* s, int x, int y, int w, int h, int start, int min, int max,
        AttackStyle style, int protected_pct, int gfx, int owner) {
    int loc = gfx >= 60000 ? gfx : 0;
    if (loc) gfx = 0;
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > TFA_ROOM_SIZE) w = TFA_ROOM_SIZE - x;
    if (y + h > TFA_ROOM_SIZE) h = TFA_ROOM_SIZE - y;
    if (w <= 0 || h <= 0) return;
    tfa_hazard_add(s, (TfaHazard){
        .kind = TFA_HAZ_STRIKE, .style = (uint8_t)style, .w = (uint8_t)w, .h = (uint8_t)h,
        .owner = (int8_t)owner, .x = (int16_t)x, .y = (int16_t)y,
        .start_tick = start, .end_tick = start,
        .min_damage = (int16_t)min, .max_damage = (int16_t)max,
        .protected_pct = (int16_t)protected_pct, .gfx_id = gfx, .loc_id = loc,
    });
}

static void oc_haz_tile(TfaState* s, int x, int y, int start, int min, int max, int gfx) {
    if (tfa_layout_blocked(tfa_room_layout(s->room), x, y)) return;
    oc_haz(s, x, y, 1, 1, start, min, max, ATTACK_STYLE_NONE, 100, gfx, -1);
}

static void oc_clear_hazards(TfaState* s) {
    memset(s->hazards, 0, sizeof(s->hazards));
}

static int oc_random_alive(TfaState* s) {
    int c[TFA_MAX_PLAYERS], n = 0;
    for (int p = 0; p < s->team_size; p++) if (tfa_alive(s, p)) c[n++] = p;
    assert(n > 0);
    return c[tfa_rand(s, n)];
}

static int oc_nearest_alive(const TfaState* s, const TfaNpc* n) {
    int best = -1, best_d = 1 << 30;
    for (int p = 0; p < s->team_size; p++) {
        if (!tfa_alive(s, p)) continue;
        int d = tfa_player_npc_distance(&s->players[p], n);
        if (d < best_d) { best_d = d; best = p; }
    }
    return best;
}

static void oc_walk(TfaNpc* n, int tx, int ty) {
    int dx = tx - n->x, dy = ty - n->y;
    int sx = oc_sign(dx), sy = oc_sign(dy);
    if (abs(dx) > abs(dy)) sy = 0;
    else if (abs(dy) > abs(dx)) sx = 0;
    n->x = (int16_t)(n->x + sx);
    n->y = (int16_t)(n->y + sy);
}

static void oc_boss_heal(TfaNpc* n, int amount) {
    n->hp = n->hp + amount > n->max_hp ? n->max_hp : n->hp + amount;
}

static void oc_heal_player(TfaState* s, int p, int amount) {
    Player* pl = &s->players[p].player;
    pl->current_hitpoints = pl->current_hitpoints + amount > pl->base_hitpoints ? pl->base_hitpoints : pl->current_hitpoints + amount;
}

/* A line pulse on a player: chip damage that heals the boss; what lines took heals back after the window. */
static void tfa_ocellus_tendril_hit(TfaState* s, int p) {
    TfaOcellusState* os = oc_state(s);
    if (os->line_start[p] == 0 || s->tick - os->line_last[p] > TFA_OC1.tendril_heal_window) {
        os->line_start[p] = s->tick;
        os->line_acc[p] = 0;
    }
    os->line_last[p] = s->tick;
    int dmg = tfa_rand_range(s, TFA_OC1.tendril_min, TFA_OC1.tendril_max);
    tfa_damage_player_now(s, p, dmg, ATTACK_STYLE_NONE);
    os->line_acc[p] += dmg;
    if (!os->no_line_heal) oc_boss_heal(oc_boss(s), TFA_OC1.tendril_boss_heal);
}

/* Crack gfx ids: family 01 (4236 START, 4240 body, 4241 END) runs straight, family 02 (4248, 4252, 4253) diagonal; the
   variant +1 (START) or +2 (body, END) per 90 degrees turns the model with the line, clockwise from south (cache spotanim rotation). */
static int oc_crack_rot(int dx, int dy) {
    if (dy < 0 && dx <= 0) return 0;
    if (dx < 0) return 1;
    if (dy > 0) return 2;
    return 3;
}

/* A crack line from its tile next to him outward: START on the far tile, bodies toward him with the delay rising 1 cycle per
   tile, END on every tile hit_in ticks later, when it chips anyone standing on it. */
static void oc_crack_line(TfaState* s, const int* xs, const int* ys, int len, int start_delay, int hit_in) {
    TfaOcellusState* os = oc_state(s);
    TfaOcLine* l = NULL;
    for (int i = 0; i < TFA_OC_MAX_LINES && !l; i++) if (!os->lines[i].live) l = &os->lines[i];
    assert(l);
    int dx = xs[1] - xs[0], dy = ys[1] - ys[0], rot = oc_crack_rot(dx, dy), diagonal = dx && dy;
    int start_gfx = (diagonal ? 4248 : 4236) + rot, body_gfx = (diagonal ? 4252 : 4240) + 2 * rot;
    *l = (TfaOcLine){ .live = 1, .len = (int8_t)len, .end_gfx = (int16_t)(body_gfx + 1), .end_tick = s->tick + hit_in };
    for (int k = 0; k < len; k++) {
        l->x[k] = (int8_t)xs[k], l->y[k] = (int8_t)ys[k];
        tfa_fx_ground(s, xs[k], ys[k], k == len - 1 ? start_gfx : body_gfx, start_delay + len - 1 - k);
        if (tfa_layout_blocked(tfa_room_layout(s->room), xs[k], ys[k])) continue;
        tfa_hazard_add(s, (TfaHazard){
            .kind = TFA_HAZ_STRIKE, .style = ATTACK_STYLE_NONE, .w = 1, .h = 1, .owner = -1,
            .effect = TFA_EFFECT_TENDRIL, .x = (int16_t)xs[k], .y = (int16_t)ys[k],
            .start_tick = l->end_tick, .end_tick = l->end_tick, .protected_pct = 100,
        });
    }
}

/* Corner line k: one of 7 candidates out of the footprint toward the arena (along x or y at the two offsets off the wall side, or
   the three diagonals through and beside the corner tile; 245/245 recorded lines are among them and the wall-side x and y lines
   never occur in any corner), the one nearest the raider whose turn it is. Turns go
   round the living raiders from a random one: recorded line k repeats line k + 3 in most 3-raider corners, and the centre offsets
   (x1 45, y1 54 of 245) beat the edge ones (8-12). */
static void oc_corner_line(TfaState* s, const TfaNpc* n, int k) {
    int len = TFA_OC1.corner_line_len;
    int sx = n->x + 1 < OC_CROSS_X ? 1 : -1, sy = n->y + 1 < OC_CROSS_Y ? 1 : -1;
    int xs[9][8], ys[9][8];
    for (int c = 0; c < 9; c++)
        for (int i = 0; i < len; i++) {
            int off = c % 3;
            if (c < 3) xs[c][i] = (sx > 0 ? n->x + 3 : n->x - 1) + sx * i, ys[c][i] = n->y + off;
            else if (c < 6) xs[c][i] = n->x + off, ys[c][i] = (sy > 0 ? n->y + 3 : n->y - 1) + sy * i;
            else {
                int ox = off == 1 ? -sx : 0, oy = off == 2 ? -sy : 0;
                xs[c][i] = (sx > 0 ? n->x + 2 : n->x) + sx * (i + 1) + ox, ys[c][i] = (sy > 0 ? n->y + 2 : n->y) + sy * (i + 1) + oy;
            }
        }
    int alive[TFA_MAX_PLAYERS], nalive = 0;
    for (int p = 0; p < s->team_size; p++) if (tfa_alive(s, p)) alive[nalive++] = p;
    assert(nalive > 0);
    const Player* pl = &s->players[alive[(oc_state(s)->line_turn + k) % nalive]].player;
    int best[9], nbest = 0, best_d = 1 << 30;
    for (int c = 0; c < 9; c++) {
        if ((c < 3 && c % 3 == (sy > 0 ? 0 : 2)) || (c >= 3 && c < 6 && c % 3 == (sx > 0 ? 0 : 2))) continue;
        int d = 1 << 30;
        for (int i = 0; i < len; i++) {
            int e = chebyshev_distance(xs[c][i], ys[c][i], pl->x, pl->y);
            if (e < d) d = e;
        }
        if (d < best_d) best_d = d, nbest = 0;
        if (d == best_d) best[nbest++] = c;
    }
    int c = best[tfa_rand(s, nbest)];
    oc_crack_line(s, xs[c], ys[c], len, TFA_OC1.corner_line_delay, TFA_OC1.corner_line_hit);
}

/* The middle V: facing f and its left L, line A from centre + 2f + L along f + L, line B from centre + 2f - L along f - L
   (24/24 recorded V lines, tile table in notes/ocellus-cracks.md). */
static void oc_middle_lines(TfaState* s, const TfaNpc* n, int facing) {
    static const int8_t FACE[4][2] = { {0, -1}, {-1, 0}, {0, 1}, {1, 0} };
    int fx = FACE[facing][0], fy = FACE[facing][1], lx = -fy, ly = fx, len = TFA_OC1.mid_line_len;
    for (int side = 0; side < 2; side++) {
        int px = side ? -lx : lx, py = side ? -ly : ly, xs[8], ys[8];
        for (int k = 0; k < len; k++) xs[k] = n->x + 1 + 2 * fx + px + k * (fx + px), ys[k] = n->y + 1 + 2 * fy + py + k * (fy + py);
        oc_crack_line(s, xs, ys, len, TFA_OC1.mid_delay[side], TFA_OC1.mid_hit[side]);
    }
}

/* Explosion floor 4262-4265 on every arena tile at Chebyshev 1-2 from his footprint: all 40 in the middle, 37 in a corner where
   the arena octagon |dx| + |dy| <= 16 around the cross cuts the outer corner (44 and 34 events). Variant uniform per tile (40/40 tiles
   saw all four), delay 1 cycle on the inner ring and 3 on the outer (40/40). */
static void oc_explosion_floor(TfaState* s, const TfaNpc* n) {
    for (int dx = -2; dx <= 4; dx++)
        for (int dy = -2; dy <= 4; dy++) {
            int x = n->x + dx, y = n->y + dy, ring = dx < -1 || dx > 3 || dy < -1 || dy > 3 ? 2 : dx < 0 || dx > 2 || dy < 0 || dy > 2 ? 1 : 0;
            if (!ring || abs(x - OC_CROSS_X) + abs(y - OC_CROSS_Y) > 16) continue;
            uint32_t h = (uint32_t)s->tick * 73856093u ^ (uint32_t)x * 19349663u ^ (uint32_t)y * 83492791u;
            tfa_fx_ground(s, x, y, OC_GFX_EXPLODE + (int)(h % 4), ring == 1 ? 1 : 3);
        }
}

static void oc_lines_tick(TfaState* s) {
    TfaOcellusState* os = oc_state(s);
    for (int i = 0; i < TFA_OC_MAX_LINES; i++) {
        TfaOcLine* l = &os->lines[i];
        if (!l->live || l->end_tick != s->tick) continue;
        for (int k = 0; k < l->len; k++) tfa_fx_ground(s, l->x[k], l->y[k], l->end_gfx, l->len - k);
        l->live = 0;
    }
    if (os->explode_gfx_tick == s->tick) oc_explosion_floor(s, oc_boss(s));
    for (int p = 0; p < s->team_size; p++) {
        if (os->line_acc[p] <= 0 || s->tick - os->line_start[p] < TFA_OC1.tendril_heal_window) continue;
        if (tfa_alive(s, p)) {
            oc_heal_player(s, p, os->line_acc[p]);
            os->meter++;
        }
        os->line_acc[p] = 0;
        os->line_start[p] = 0;
    }
}

/* 7x7 around his centre minus the footprint; safe_dx/dy mark one unhurt tile or are 99. */
static void oc_zone(TfaState* s, const TfaNpc* n, int safe_dx, int safe_dy, int start) {
    oc_state(s)->explode_gfx_tick = start;
    int cx = n->x + 1, cy = n->y + 1, r = TFA_OC1.explode_radius;
    int x0 = cx - r, y0 = cy - r, side = 2 * r + 1;
    int lo = TFA_OC1.explode_min, hi = TFA_OC1.explode_max;
    if (safe_dx == 99) {
        oc_haz(s, x0, y0, side, side, start, lo, hi, ATTACK_STYLE_NONE, 100, 0, -1);
        return;
    }
    int sx = cx + safe_dx, sy = cy + safe_dy;
    oc_haz(s, x0, y0, side, sy - y0, start, lo, hi, ATTACK_STYLE_NONE, 100, 0, -1);
    oc_haz(s, x0, sy + 1, side, y0 + side - sy - 1, start, lo, hi, ATTACK_STYLE_NONE, 100, 0, -1);
    oc_haz(s, x0, sy, sx - x0, 1, start, lo, hi, ATTACK_STYLE_NONE, 100, 0, -1);
    oc_haz(s, sx + 1, sy, x0 + side - sx - 1, 1, start, lo, hi, ATTACK_STYLE_NONE, 100, 0, -1);
}

static void oc_want(TfaState* s, OverheadPrayer prayer) {
    oc_state(s)->want_prayer = prayer;
}

static int oc_middle_due(const TfaState* s) {
    const TfaOcellusState* os = &s->boss.ocellus;
    const TfaNpc* n = &s->npcs[0];
    return os->middles < 2 && n->hp * 100 <= TFA_OC1.middle_pct[os->middles] * n->max_hp;
}

static void oc_p1_next_corner(TfaState* s) {
    TfaOcellusState* os = oc_state(s);
    os->corner = (os->corner + 1) & 3;
}

static void oc_p1_enter(TfaState* s) {
    TfaOcellusState* os = oc_state(s);
    TfaNpc* n = tfa_npc_spawn(s, TFA_NPC_OCELLUS, MON_TFA_OCELLUS_P1, OC_NPC_P1, 30, 45);
    n->max_hp = n->hp = tfa_party_hp(s->team_size, TFA_OC1.hp_base_p1, TFA_OC1.hp_trio_p1);
    os->phase = TFA_OCELLUS_P1;
    os->corner = tfa_rand(s, 4);
    os->window = TFA_OC1.window_fresh;
    os->zero_tick = -1;
    os->want_prayer = PRAYER_NONE;
    oc_at(s, TFA_OC1.first_barrage, OCE_P1_BARRAGE, 0, 0);
}

static void oc_p1_barrage(TfaState* s, TfaNpc* n) {
    TfaOcellusState* os = oc_state(s);
    n->act = TFA_ACT_RANGED;
    os->centre = 0;
    tfa_npc_anim(s, n, OC_ANIM_BARRAGE);
    int shots = os->window / TFA_OC1.shot_gap;
    int b_at = os->window == TFA_OC1.window_fresh ? (tfa_rand(s, 4) == 0 ? 10 : 5) : (tfa_rand(s, 2) ? 10 : 15);
    for (int k = 0; k < shots; k++) {
        int at = TFA_OC1.shot_first + TFA_OC1.shot_gap * k;
        oc_at(s, at, OCE_P1_SHOT, 0, 0);
        oc_at(s, at + TFA_OC1.shot_to_end, OCE_P1_BARRAGE_END, at + TFA_OC1.shot_to_end == b_at, 0);
    }
    oc_at(s, os->window, OCE_P1_CHARGE, 0, 0);
    os->next_attack_tick = s->tick + TFA_OC1.shot_first;
}

static void oc_p1_shot(TfaState* s, TfaNpc* n) {
    TfaOcellusState* os = oc_state(s);
    AttackStyle style = tfa_rand(s, 100) < TFA_OC1.magic_pct ? ATTACK_STYLE_MAGIC : ATTACK_STYLE_RANGED;
    oc_want(s, style == ATTACK_STYLE_MAGIC ? PRAYER_PROTECT_MAGIC : PRAYER_PROTECT_RANGED);
    for (int p = 0; p < s->team_size; p++) {
        if (!tfa_alive(s, p)) continue;
        const Player* pl = &s->players[p].player;
        int d = chebyshev_distance(pl->x, pl->y, n->x + 1, n->y + 1);
        int flight = d < TFA_OC1.flight_split + 1 ? TFA_OC1.flight_near : TFA_OC1.flight_far;
        tfa_queue_npc_hit_land(s, p, tfa_rand_range(s, TFA_OC1.shot_min, TFA_OC1.shot_max), style, flight, 0, n);
        if (style == ATTACK_STYLE_MAGIC)
            tfa_fx_projectile(s, n, p, 4229, SPOTANIM_TFA_OCELLUS_VFX_OCELLUS_WATER_ATTACK01_PROJANIM,
                SPOTANIM_TFA_OCELLUS_VFX_OCELLUS_WATER_ATTACK01_PROJANIM_ANIM, 0, flight);
        else
            tfa_fx_projectile(s, n, p, 4232, SPOTANIM_TFA_OCELLUS_VFX_OCELLUS_FIRE_ATTACK01_PROJANIM,
                SPOTANIM_TFA_OCELLUS_VFX_OCELLUS_FIRE_ATTACK01_PROJANIM_ANIM, 0, flight);
    }
    os->next_attack_tick = s->tick + TFA_OC1.shot_gap;
}

static void oc_p1_charge(TfaState* s, TfaNpc* n) {
    TfaOcellusState* os = oc_state(s);
    n->act = TFA_ACT_SPECIAL;
    tfa_npc_anim(s, n, OC_ANIM_CHARGE);
    int explode = s->tick + TFA_OC1.corner_charge_to_explode;
    os->line_turn = tfa_rand(s, TFA_MAX_PLAYERS);
    for (int k = 0; k < TFA_OC1.lines_corner; k++) {
        if (k == 0) oc_corner_line(s, n, 0);
        else oc_at(s, TFA_OC1.line_gap * k, OCE_P1_LINE, k, 0);
    }
    oc_at(s, TFA_OC1.corner_charge_to_explode - TFA_OC1.explode_tell, OCE_P1_EXPLODE_TELL, 0, 0);
    oc_at(s, TFA_OC1.corner_charge_to_explode, OCE_P1_EXPLODE, 0, 0);
    os->next_attack_tick = explode;
}

static void oc_p1_zero_cut(TfaState* s) {
    oc_cancel(s, OCE_P1_BARRAGE, OCE_P1_EXPEND);
    oc_clear_hazards(s);
    memset(oc_state(s)->lines, 0, sizeof(oc_state(s)->lines));
    if (oc_state(s)->centre) oc_at(s, TFA_OC1.death_centre, OCE_P1_DYING_IN, 0, 0);
    else oc_at(s, TFA_OC1.death_out, OCE_P1_TP_OUT, 1, 0);
}

static void oc_p1_event(TfaState* s, TfaNpc* n, const TfaOcEvent* e) {
    TfaOcellusState* os = oc_state(s);
    switch (e->kind) {
        case OCE_P1_BARRAGE: oc_p1_barrage(s, n); break;
        case OCE_P1_SHOT: oc_p1_shot(s, n); break;
        case OCE_P1_BARRAGE_END:
            if (oc_middle_due(s)) {
                os->middles++;
                os->barrage = 0;
                oc_cancel(s, OCE_P1_SHOT, OCE_P1_CHARGE);
                TfaOcEvent tp_out = { .kind = OCE_P1_TP_OUT };
                oc_p1_event(s, n, &tp_out);
            } else if (e->arg) {
                tfa_npc_anim(s, n, OC_ANIM_BARRAGE_END);
            }
            break;
        case OCE_P1_CHARGE: oc_p1_charge(s, n); break;
        case OCE_P1_LINE: oc_corner_line(s, n, e->arg); break;
        case OCE_P1_EXPLODE_TELL: oc_zone(s, n, 99, 99, s->tick + TFA_OC1.explode_tell + 1); break;
        case OCE_P1_EXPLODE:
            tfa_npc_anim(s, n, OC_ANIM_EXPLODE);
            oc_at(s, TFA_OC1.explode_to_next - 1, OCE_P1_AFTER_EXPLODE, 0, 0);
            os->next_attack_tick = s->tick + TFA_OC1.explode_to_next;
            break;
        case OCE_P1_AFTER_EXPLODE:
            n->act = TFA_ACT_IDLE;
            oc_p1_next_corner(s);
            if (oc_middle_due(s)) {
                os->middles++;
                oc_at(s, 1, OCE_P1_TP_OUT, 0, 0);
            } else {
                os->window = TFA_OC1.window_corner;
                oc_at(s, 1, OCE_P1_BARRAGE, 0, 0);
            }
            break;
        case OCE_P1_TP_OUT:
            n->act = TFA_ACT_SPECIAL;
            tfa_npc_anim(s, n, OC_ANIM_TP_OUT);
            oc_at(s, e->arg ? TFA_OC1.death_in : TFA_OC1.tp_in, OCE_P1_TP_IN, e->arg, 0);
            break;
        case OCE_P1_TP_IN:
            n->x = OC_CENTRE_X;
            n->y = OC_CENTRE_Y;
            os->centre = 1;
            if (!e->arg) os->centre_gone = s->tick + OC_CENTRE_HIDE_FIRST, os->centre_back = s->tick + OC_CENTRE_HIDE_LAST;
            {
                /* The first V faces a raider along the dominant axis (P0 in 8 of 10 recorded middles, x on ties), then turns +512 per charge. */
                const Player* pl = &s->players[oc_random_alive(s)].player;
                int dx = pl->x - OC_CROSS_X, dy = pl->y - OC_CROSS_Y;
                os->facing = abs(dx) >= abs(dy) ? (dx < 0 ? 1 : 3) : (dy < 0 ? 0 : 2);
            }
            tfa_npc_anim(s, n, OC_ANIM_TP_IN);
            if (e->arg) {
                oc_at(s, TFA_OC1.death_transform, OCE_P1_DYING_IN, 0, 0);
            } else {
                oc_at(s, TFA_OC1.mid_first_charge, OCE_P1_MID_CHARGE, 0, 0);
            }
            break;
        case OCE_P1_MID_CHARGE:
            tfa_npc_anim(s, n, OC_ANIM_CHARGE);
            oc_middle_lines(s, n, (os->facing + e->arg) & 3);
            oc_at(s, TFA_OC1.middle_charge_to_explode, OCE_P1_MID_EXPLODE, e->arg, 0);
            os->next_attack_tick = s->tick + TFA_OC1.middle_charge_to_explode;
            break;
        case OCE_P1_MID_EXPLODE:
            tfa_npc_anim(s, n, OC_ANIM_EXPLODE);
            oc_zone(s, n, OC_PINWHEEL[e->arg][0], OC_PINWHEEL[e->arg][1], s->tick + 1);
            if (e->arg + 1 < TFA_OC1.mid_charges) {
                oc_at(s, TFA_OC1.mid_gap, OCE_P1_MID_CHARGE, e->arg + 1, 0);
                os->next_attack_tick = s->tick + TFA_OC1.mid_gap;
            } else {
                oc_at(s, TFA_OC1.expend_text + TFA_OC1.expend_after, OCE_P1_EXPEND, 0, 0);
                os->next_attack_tick = s->tick + TFA_OC1.expend_text + TFA_OC1.expend_after;
            }
            break;
        case OCE_P1_EXPEND: {
            n->act = TFA_ACT_SPECIAL;
            tfa_npc_anim(s, n, OC_ANIM_EXPEND);
            int full = os->meter >= TFA_OC1.meter_threshold;
            os->meter = 0;
            for (int p = 0; p < s->team_size; p++) {
                if (!tfa_alive(s, p)) continue;
                for (int k = 0; k < (full ? TFA_OC1.eradicate_hits : 1); k++)
                    tfa_queue_npc_hit(s, p, tfa_rand_range(s, TFA_OC1.expend_hit_min, TFA_OC1.expend_hit_max),
                        ATTACK_STYLE_NONE, full ? TFA_OC1.eradicate_ticks[k] : 1, 100, n);
            }
            os->window = TFA_OC1.window_fresh;
            oc_at(s, TFA_OC1.resume, OCE_P1_BARRAGE, 0, 0);
            os->next_attack_tick = s->tick + TFA_OC1.resume;
            break;
        }
        case OCE_P1_DYING_IN:
            tfa_npc_anim(s, n, OC_ANIM_P1_TRANSFORM);
            os->centre_gone = s->tick + OC_CENTRE_GONE_P2, os->centre_back = INT32_MAX;
            oc_at(s, TFA_OC1.death_p2, OCE_P2_START, 0, 0);
            break;
        default: abort();
    }
}

/* The quadrant of a tile: bit 0 east of the cross, bit 1 north of it. */
static int oc_quadrant(int x, int y) {
    return (x > OC_CROSS_X) | ((y > OC_CROSS_Y) << 1);
}

/* A random walkable tile 4..10 out from the cross in quadrant q. */
static void oc_place_player(TfaState* s, int p, int q) {
    int sx = (q & 1) ? 1 : -1, sy = (q & 2) ? 1 : -1;
    for (int tries = 0; tries < 200; tries++) {
        int x = OC_CROSS_X + sx * (4 + tfa_rand(s, 7)), y = OC_CROSS_Y + sy * (4 + tfa_rand(s, 7));
        if (tfa_layout_blocked(tfa_room_layout(s->room), x, y)) continue;
        TfaPlayer* tp = &s->players[p];
        tp->player.x = (int16_t)x;
        tp->player.y = (int16_t)y;
        tp->player.dest_x = x;
        tp->player.dest_y = y;
        tp->dest_x = -1;
        tp->dest_y = -1;
        osrs_actor_route_cache_clear(&tp->route_cache);
        return;
    }
    fprintf(stderr, "ocellus: no free tile in quadrant %d\n", q);
    abort();
}

/* The lit portal on the cross that fires a lane's near segment: 2x1 on the y = 49 wall, turned to 1x2 on x = 31 (recorded OBJ+ rotation 512). */
static int oc_lane_portal(const TfaHazard* h, int* x, int* y, int* turned) {
    if (h->loc_id != OC_LOC_LANE_PORTAL) return 0;
    if (h->w == 2) {
        *x = h->x, *y = OC_CROSS_Y, *turned = 0;
        return h->y + h->h == OC_CROSS_Y || h->y == OC_CROSS_Y + 1;
    }
    *x = OC_CROSS_X, *y = h->y, *turned = 1;
    return h->x + h->w == OC_CROSS_X || h->x == OC_CROSS_X + 1;
}

/* Lane projectile end: the far tile inside the arena octagon |dx| + |dy| <= 21, dx -14..14, dy -15..14 around the cross (all 92 recorded 4269 targets). */
static int oc_lane_inside(int x, int y) {
    int dx = x - OC_CROSS_X, dy = y - OC_CROSS_Y;
    return dx >= -14 && dx <= 14 && dy >= -15 && dy <= 14 && abs(dx) + abs(dy) <= 21;
}

/* Projectile 4269 of a firing lane: from the hidden shooter at x = 32 (row lanes, row + 1) or y = 50 (column lanes, column + 1) in every quadrant,
   flight 2 ticks to 12 tiles and 3 at 15-16 (recorded PROJ). Takes a near segment from oc_lane_portal. */
static void oc_lane_shot(const TfaHazard* h, int* sx, int* sy, int* dx, int* dy, int* cycles) {
    if (h->w == 2) {
        int dir = h->y == OC_CROSS_Y + 1 ? 1 : -1;
        *sx = h->x + 1, *sy = OC_CROSS_Y + 1, *dx = h->x, *dy = OC_CROSS_Y + dir;
        while (oc_lane_inside(*dx, *dy + dir)) *dy += dir;
    } else {
        int dir = h->x == OC_CROSS_X + 1 ? 1 : -1;
        *sx = OC_CROSS_X + 1, *sy = h->y + 1, *dx = OC_CROSS_X + dir, *dy = h->y;
        while (oc_lane_inside(*dx + dir, *dy)) *dx += dir;
    }
    int d = chebyshev_distance(*sx, *sy, *dx, *dy);
    *cycles = 60 + 8 * (d > 12 ? d - 12 : 0);
}

/* Scenery the live room changes during the fight: the centre piece (3x3 at 30,48, rotation 2) leaves for each middle sequence and for good
   at the transform, the Blade at the cross (31,49) gives way to the falling blade when the blade NPC spawns. */
static void oc_scenery(const TfaState* s, EncounterOverlay* ov) {
    const TfaOcellusState* os = &s->boss.ocellus;
    if (s->tick < os->centre_gone || s->tick >= os->centre_back) {
        encounter_emit_floating_model(ov, ENCOUNTER_PROJECTILE_TARGET_FIXED, -1, OC_CENTRE_X + 1, OC_CENTRE_Y + 1,
            OSRS_LOC_MODEL_BASE | OC_LOC_CENTRE, -1, 0.0f, 0.0f, 1.0f);
        ov->floating_models[ov->floating_model_count - 1].yaw = (float)TFA_PI;
    }
    int loc = os->blade_tick == 0 ? OC_LOC_BLADE : s->tick < os->blade_tick + OC_BLADE_FALL_TICKS ? OC_LOC_BLADE_FALLING : 0;
    if (loc) encounter_emit_floating_model(ov, ENCOUNTER_PROJECTILE_TARGET_FIXED, -1, OC_CROSS_X, OC_CROSS_Y,
        OSRS_LOC_MODEL_BASE | (uint32_t)loc, SPOTANIM_TFA_OCELLUS_VFX_OCELLUS_SWORD_IDLE_STATIC_ANIM, 0.0f, 0.0f, 1.0f);
}

/* The safe portal's spotanim: spawn 4271 on its first tick, despawn 4273 on the fire tick, idle 4272 between (recorded GFX, 12 portals). */
static void oc_portal_visual(const TfaState* s, const TfaNpc* portal, uint32_t* model, int* anim) {
    int age = s->tick - portal->anim_tick;
    if (age == 0) *model = SPOTANIM_TFA_OCELLUS_VFX_OCELLUS_P2_SAFE_PORTAL_SPAWN, *anim = SPOTANIM_TFA_OCELLUS_VFX_OCELLUS_P2_SAFE_PORTAL_SPAWN_ANIM;
    else if (age == TFA_OC2.special_fire) *model = SPOTANIM_TFA_OCELLUS_VFX_OCELLUS_P2_SAFE_PORTAL_DESPAWN, *anim = SPOTANIM_TFA_OCELLUS_VFX_OCELLUS_P2_SAFE_PORTAL_DESPAWN_ANIM;
    else *model = SPOTANIM_TFA_OCELLUS_VFX_OCELLUS_P2_SAFE_PORTAL_IDLE, *anim = SPOTANIM_TFA_OCELLUS_VFX_OCELLUS_P2_SAFE_PORTAL_IDLE_ANIM;
}

/* Lanes of one quadrant: wall portals light all but one per wall, and fire along 2-wide lanes 4 ticks later. */
static void oc_lanes(TfaState* s, int q, int all_lit, int start) {
    int sx = (q & 1) ? 1 : -1, sy = (q & 2) ? 1 : -1;
    int dark_row = all_lit ? -1 : tfa_rand(s, 6), dark_col = all_lit ? -1 : tfa_rand(s, 6);
    int lo = TFA_OC2.lane_min, hi = TFA_OC2.lane_max, len = TFA_OC2.lane_len, near = TFA_OC2.near_len;
    for (int k = 0; k < 6; k++) {
        int col = sx < 0 ? 19 + 2 * k : 32 + 2 * k;
        int row = sy < 0 ? 37 + 2 * k : 50 + 2 * k;
        if (k != dark_col) {
            int y_near = sy < 0 ? OC_CROSS_Y - near : OC_CROSS_Y + 1, y_far = sy < 0 ? OC_CROSS_Y - len : OC_CROSS_Y + 1 + near;
            oc_haz(s, col, y_near, 2, near, start, lo, hi, ATTACK_STYLE_NONE, 100, OC_LOC_LANE_PORTAL, -1);
            oc_haz(s, col, y_far, 2, len - near, start + 1, lo, hi, ATTACK_STYLE_NONE, 100, OC_LOC_LANE_PORTAL, -1);
        }
        if (k != dark_row) {
            int x_near = sx < 0 ? OC_CROSS_X - near : OC_CROSS_X + 1, x_far = sx < 0 ? OC_CROSS_X - len : OC_CROSS_X + 1 + near;
            oc_haz(s, x_near, row, near, 2, start, lo, hi, ATTACK_STYLE_NONE, 100, OC_LOC_LANE_PORTAL, -1);
            oc_haz(s, x_far, row, len - near, 2, start + 1, lo, hi, ATTACK_STYLE_NONE, 100, OC_LOC_LANE_PORTAL, -1);
        }
    }
}

static int oc_occupied(const TfaState* s) {
    int mask = 0;
    for (int p = 0; p < s->team_size; p++)
        if (tfa_alive(s, p)) mask |= 1 << oc_quadrant(s->players[p].player.x, s->players[p].player.y);
    return mask;
}

static void oc_p2_enter(TfaState* s, TfaNpc* n) {
    TfaOcellusState* os = oc_state(s);
    os->phase = TFA_OCELLUS_P2;
    os->phase_start = s->tick;
    n->npc_id = OC_NPC_P2;
    n->monster = MON_TFA_OCELLUS_P2;
    n->def_drain = 0;
    n->attackable = 0;
    n->act = TFA_ACT_IDLE;
    n->hp = n->max_hp = tfa_party_hp(s->team_size, TFA_OC1.hp_base_p3, TFA_OC1.hp_base_p3 * TFA_TRIO_SCALE_PCT / 100);
    n->x = OC_CENTRE_X;
    n->y = OC_CENTRE_Y;
    tfa_npc_anim(s, n, OC_ANIM_P2_TRANSFORM);
    oc_want(s, PRAYER_NONE);
    int blade_at = TFA_OC2.blade_spawn;
    oc_at(s, blade_at, OCE_P2_VOLLEY, -1, 0);
    for (int k = 0; k < 3; k++) {
        int base = TFA_OC2.period * k;
        for (int j = 0; j < TFA_OC2.volley_count; j++)
            if (base + TFA_OC2.volley_first + TFA_OC2.volley_gap * j < TFA_OC2.duration - TFA_OC2.fire_delay)
                oc_at(s, base + TFA_OC2.volley_first + TFA_OC2.volley_gap * j, OCE_P2_VOLLEY, 0, 0);
        if (k < 2) oc_at(s, base + TFA_OC2.special_lit, OCE_P2_SPECIAL, 0, 0);
    }
    for (int k = 0; k < 3; k++) oc_at(s, TFA_OC2.convert[k], OCE_P2_CONVERT, k, 0);
    oc_at(s, TFA_OC2.duration, OCE_P2_END, 0, 0);
}

static void oc_p2_event(TfaState* s, TfaNpc* n, const TfaOcEvent* e) {
    TfaOcellusState* os = oc_state(s);
    switch (e->kind) {
        case OCE_P2_VOLLEY:
            if (e->arg == -1) {
                TfaNpc* b = tfa_npc_spawn(s, TFA_NPC_OCELLUS_BLADE, MON_TFA_OCELLUS_BLADE, OC_NPC_BLADE, OC_CENTRE_X, OC_CENTRE_Y);
                b->hp = b->max_hp = 1;
                os->blade_tick = s->tick;
                break;
            }
            for (int q = 0; q < 4; q++)
                if (oc_occupied(s) & (1 << q)) oc_lanes(s, q, 0, s->tick + TFA_OC2.fire_delay);
            break;
        case OCE_P2_SPECIAL: {
            int occupied = oc_occupied(s);
            for (int q = 0; q < 4; q++) {
                if (!(occupied & (1 << q))) continue;
                int sx = (q & 1) ? 1 : -1, sy = (q & 2) ? 1 : -1;
                os->portal_x[q] = OC_CROSS_X + sx * (TFA_OC2.portal_dx + tfa_rand(s, 2));
                os->portal_y[q] = OC_CROSS_Y + sy * TFA_OC2.portal_dy;
            }
            oc_at(s, TFA_OC2.special_portal - TFA_OC2.special_lit, OCE_P2_PORTAL, occupied, 0);
            break;
        }
        case OCE_P2_PORTAL:
            for (int q = 0; q < 4; q++) {
                if (!(e->arg & (1 << q))) continue;
                TfaNpc* portal = tfa_npc_alloc(s, TFA_NPC_OCELLUS_PORTAL, OC_GFX_SAFE_PORTAL, 1, os->portal_x[q], os->portal_y[q], 1);
                portal->state = (int16_t)q;
                tfa_npc_anim(s, portal, SPOTANIM_TFA_OCELLUS_VFX_OCELLUS_P2_SAFE_PORTAL_SPAWN_ANIM);
                oc_lanes(s, q, 1, s->tick + TFA_OC2.special_fire);
            }
            oc_at(s, TFA_OC2.special_fire + 1, OCE_P2_FIRE_ALL, 0, 0);
            break;
        case OCE_P2_FIRE_ALL:
            for (int i = 0; i < TFA_MAX_NPCS; i++)
                if (s->npcs[i].active && s->npcs[i].kind == TFA_NPC_OCELLUS_PORTAL) s->npcs[i].active = 0;
            break;
        case OCE_P2_CONVERT: {
            int moved = os->blade_acc * TFA_OC2.convert_pct / 100;
            os->blade_acc = 0;
            os->carry_damage += moved;
            n->hp -= moved;
            if (n->hp < 1) n->hp = 1;
            break;
        }
        case OCE_P2_END: {
            tfa_npc_anim(s, n, OC_ANIM_P2_END);
            for (int i = 0; i < TFA_MAX_NPCS; i++)
                if (s->npcs[i].active && (s->npcs[i].kind == TFA_NPC_OCELLUS_BLADE || s->npcs[i].kind == TFA_NPC_OCELLUS_PORTAL)) s->npcs[i].active = 0;
            for (int p = 0; p < s->team_size; p++)
                if (tfa_alive(s, p) && oc_quadrant(s->players[p].player.x, s->players[p].player.y) != 1) oc_place_player(s, p, 1);
            oc_haz(s, 36, 42, 3, 3, s->tick + TFA_OC2.drop_hit, TFA_OC2.drop_min, TFA_OC2.drop_max, ATTACK_STYLE_NONE, 100, OC_GFX_SWORD_DROP, -1);
            oc_at(s, TFA_OC2.to_p3, OCE_P3_START, 0, 0);
            break;
        }
        default: abort();
    }
}

/* Players standing on a safe portal are carried to the next quadrant one tick later. */
static void oc_portals_tick(TfaState* s) {
    for (int i = 0; i < TFA_MAX_NPCS; i++) {
        const TfaNpc* portal = &s->npcs[i];
        if (!portal->active || portal->kind != TFA_NPC_OCELLUS_PORTAL) continue;
        for (int p = 0; p < s->team_size; p++) {
            if (!tfa_alive(s, p) || s->players[p].player.x != portal->x || s->players[p].player.y != portal->y) continue;
            oc_place_player(s, p, OC_NEXT_QUAD[portal->state]);
        }
    }
}
