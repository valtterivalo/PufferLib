/* Archive Guardian. Every number below is measured from 24 recorded fights
   (fixtures/tfa_guardian_events.tsv) unless the comment says otherwise. */

#define GUARDIAN_NPC_INERT 16657
#define GUARDIAN_NPC_FLYING 16659
#define GUARDIAN_NPC_GROUNDED 16660
#define GUARDIAN_NPC_DEAD 16658
#define GUARDIAN_NPC_PYLON 16662
#define GUARDIAN_NPC_PYLON_DEAD 16663
#define GUARDIAN_NPC_VINE 16664

#define GUARDIAN_ANIM_RANGED 14751
#define GUARDIAN_ANIM_SWOOSH 14750
#define GUARDIAN_ANIM_SPECIAL 14753
#define GUARDIAN_ANIM_BREATH 14754
#define GUARDIAN_ANIM_LAND 14747
#define GUARDIAN_ANIM_RISE 14748
#define GUARDIAN_ANIM_DEATH 14749
#define GUARDIAN_ANIM_VINE_ATTACK 14768
#define GUARDIAN_ANIM_PYLON_DESPAWN 15025

#define GUARDIAN_PROJ_RANGED 4201
#define GUARDIAN_LOC_MUSHROOM_SPROUT 62578
#define GUARDIAN_LOC_MUSHROOM_ARMED 62579
#define GUARDIAN_LOC_MUSHROOM_BURST 62580

typedef struct {
    int ranged_interval;          /* 14751 -> next attack: 5 in 780/854 */
    int ranged_launch_ticks;      /* 4201 first seen at anim +2: 1034/1034 */
    int ranged_hit_ticks;         /* P0 hit at anim +4: 388/856 */
    int ranged_max_hit;           /* unprotected hits 20, 33, 14 (n=3): max observed 33 */
    int ranged_protected_pct;     /* protected hits uniform 0..10 (n=380), 33 * 30% rounds up to 10 */
    int ranged_splash;            /* other players within this distance of the primary also get a projectile: 55/75 of those within 4 did, 53/447 beyond; provisional */
    int ranged_keep_target_of_170;
    int special_intervals[2];     /* flying-phase special gaps 25 (38) or 30 (21) */
    int first_special;            /* first special of a flying phase, from fight start or rise */
    int land_delay;               /* shield 0 -> 14747 three ticks later, 24/24 */
    int ground_base;              /* knockdown 1, 2, 3 ground for 35, 41, 47 ticks */
    int ground_step;
    int ground_min;               /* an HP crossing seen at one vine tick rises at the next, never before 14: 51/51 forced-damage replays */
    int enrage_pct;               /* breath loop starts at the rise that follows HP <= 33% */
    int rise_to_attack;           /* 14748 -> next attack 5 in 64/65 */
    int vine_first;               /* first vine at knockdown +5, 65/65 */
    int vine_interval;            /* one vine per player every 3 ticks while grounded */
    int vine_attacks[3];          /* 14768 at spawn +3, +6, +9: 1380/1380 */
    int vine_life;                /* despawn at spawn +13: 1380/1380 */
    int vine_min_damage, vine_max_damage; /* 17..19 under Protect from Missiles */
    int vine_protected_pct;       /* 10..11 under Protect from Melee */
    int mushroom_arm;             /* 62578 -> 62579 after 3 ticks, 1171/1171 */
    int mushroom_life;            /* untriggered armed mushrooms last 5..64 ticks */
    int mushroom_min_damage, mushroom_max_damage; /* rules.txt: 10-24 the tick after the burst */
    int breath_land;              /* players and pylon hit at 14754 +10: 57/58 */
    int breath_total_min, breath_total_max; /* split evenly: 3 x 21..23, 2 x 32..35, 1 x 70..71 */
    int breath_to_special;        /* 14754 -> next special 15, then breath 5 after it: 92/93 */
    int pylon_shield_damage;      /* 320 to the Guardian at breath +14, 17/21 (smaller when the bar runs out) */
    int pylon_shield_delay;
} TfaGuardianTuning;

static const TfaGuardianTuning TFA_GUARDIAN = {
    .ranged_interval = 5,
    .ranged_launch_ticks = 2,
    .ranged_hit_ticks = 4,
    .ranged_max_hit = 33,
    .ranged_protected_pct = 30,
    .ranged_splash = 4,
    .ranged_keep_target_of_170 = 127,
    .special_intervals = { 25, 30 },
    .first_special = 20,
    .land_delay = 3,
    .ground_base = 35,
    .ground_step = 6,
    .ground_min = 14,
    .enrage_pct = 33,
    .rise_to_attack = 5,
    .vine_first = 5,
    .vine_interval = 3,
    .vine_attacks = { 3, 6, 9 },
    .vine_life = 13,
    .vine_min_damage = 17,
    .vine_max_damage = 19,
    .vine_protected_pct = 60,
    .mushroom_arm = 3,
    .mushroom_life = 60,
    .mushroom_min_damage = 10,
    .mushroom_max_damage = 24,
    .breath_land = 10,
    .breath_total_min = 63,
    .breath_total_max = 70,
    .breath_to_special = 15,
    .pylon_shield_damage = 320,
    .pylon_shield_delay = 4,
};

/* Party scaling: shield 550 at 2 players and 800 at 3 (24/24 bars). HP 1100 at 2 and 1575 at 3: the
   values for which forced-damage replays (tfa_replay_guardian.c) reproduce all 51 rises; the 3-player
   window is 1575 alone. A solo is scaled as a duo. Four players extrapolate. */
static int tfa_guardian_shield(const TfaState* s) {
    return tfa_party_hp(s, 550, 800);
}

static int tfa_guardian_hp(const TfaState* s) {
    return tfa_party_hp(s, 1100, 1575);
}

/* Swoosh destinations recorded in the breath loop (31 swooshes): the boss SW tile. */
static const int8_t TFA_GUARDIAN_SWOOSH_TILES[][2] = {
    {32, 38}, {31, 38}, {34, 25}, {33, 20}, {29, 31}, {34, 40}, {28, 38}, {27, 28},
    {35, 20}, {33, 29}, {28, 24}, {26, 33}, {36, 15}, {35, 26}, {30, 31}, {34, 39},
    {29, 30}, {32, 20}, {26, 32}, {27, 22}, {26, 34}, {35, 27}, {33, 38}, {31, 22},
    {34, 24}, {37, 29}, {31, 32}, {26, 24}, {37, 15}, {32, 21},
};

static const int8_t TFA_GUARDIAN_PYLON_TILES[4][2] = { {18, 24}, {18, 37}, {43, 24}, {43, 37} };

static TfaNpc* tfa_guardian_boss(TfaState* s) {
    TfaNpc* n = tfa_npc_first(s, TFA_NPC_GUARDIAN);
    assert(n);
    return n;
}

static void tfa_guardian_enter(TfaState* s) {
    const TfaRoomLayout* l = tfa_room_layout(TFA_ROOM_GUARDIAN);
    TfaNpc* g = tfa_npc_spawn(s, TFA_NPC_GUARDIAN, MON_TFA_ARCHIVE_GUARDIAN, GUARDIAN_NPC_FLYING, l->boss_x, l->boss_y);
    g->max_hp = g->hp = tfa_guardian_hp(s);
    g->max_shield = g->shield = tfa_guardian_shield(s);
    for (int i = 0; i < 4; i++)
        tfa_npc_spawn_inert(s, TFA_NPC_GUARDIAN_PYLON, GUARDIAN_NPC_PYLON, 2,
            TFA_GUARDIAN_PYLON_TILES[i][0], TFA_GUARDIAN_PYLON_TILES[i][1]);
    TfaGuardianState* gs = &s->boss.guardian;
    gs->phase = TFA_GUARDIAN_FLYING;
    gs->ranged_timer = 1;
    gs->special_timer = TFA_GUARDIAN.first_special;
    gs->target = 0;
}

static int tfa_guardian_center2(const TfaNpc* g, int axis) {
    return 2 * (axis == 0 ? g->x : g->y) + g->size - 1;
}

/* Single-projectile ranged attacks keep the previous target 127 of 170 times; the nearest player
   is the target only 82 of 170 times. Keep with that rate, else switch to another living player. */
static int tfa_guardian_pick_target(TfaState* s, int current) {
    if (tfa_alive(s, current) && (tfa_alive_count(s) == 1 || tfa_rand(s, 170) < TFA_GUARDIAN.ranged_keep_target_of_170))
        return current;
    int c[TFA_MAX_PLAYERS], n = 0;
    for (int p = 0; p < s->team_size; p++) if (tfa_alive(s, p) && p != current) c[n++] = p;
    return n ? c[tfa_rand(s, n)] : -1;
}

static void tfa_guardian_ranged(TfaState* s, TfaNpc* g) {
    TfaGuardianState* gs = &s->boss.guardian;
    int primary = tfa_guardian_pick_target(s, gs->target);
    if (primary < 0) return;
    gs->target = primary;
    g->target = (int8_t)primary;
    g->act = TFA_ACT_RANGED;
    tfa_npc_anim(s, g, GUARDIAN_ANIM_RANGED);
    const Player* aim = &s->players[primary].player;
    for (int p = 0; p < s->team_size; p++) {
        if (!tfa_alive(s, p)) continue;
        if (p != primary && chebyshev_distance(s->players[p].player.x, s->players[p].player.y, aim->x, aim->y) > TFA_GUARDIAN.ranged_splash)
            continue;
        int prayed = encounter_prayer_correct_for_style(s->players[p].player.prayer, ATTACK_STYLE_RANGED);
        int max_hit = prayed
            ? (TFA_GUARDIAN.ranged_max_hit * TFA_GUARDIAN.ranged_protected_pct + 99) / 100
            : TFA_GUARDIAN.ranged_max_hit;
        int dmg = tfa_npc_roll(s, g, p, ATTACK_STYLE_RANGED, max_hit);
        tfa_queue_npc_hit(s, p, dmg, ATTACK_STYLE_RANGED, TFA_GUARDIAN.ranged_hit_ticks, 100, g);
        tfa_fx_projectile(s, g, p, GUARDIAN_PROJ_RANGED, SPOTANIM_TFA_GUARDIAN_VFX_GUARDIAN_RANGED_PROJANIM01,
            SPOTANIM_TFA_GUARDIAN_VFX_GUARDIAN_RANGED_PROJANIM01_ANIM, TFA_GUARDIAN.ranged_launch_ticks,
            TFA_GUARDIAN.ranged_hit_ticks - TFA_GUARDIAN.ranged_launch_ticks);
    }
}

static int tfa_guardian_tile_walkable(const TfaState* s, int x, int y) {
    return !tfa_layout_blocked(tfa_room_layout(s->room), x, y);
}

static void tfa_guardian_mushroom(TfaState* s, int x, int y) {
    if (!tfa_guardian_tile_walkable(s, x, y)) return;
    tfa_hazard_add(s, (TfaHazard){
        .kind = TFA_HAZ_MUSHROOM,
        .style = ATTACK_STYLE_NONE,
        .w = 1, .h = 1,
        .owner = -1,
        .effect = TFA_EFFECT_POISON,
        .x = (int16_t)x, .y = (int16_t)y,
        .end_tick = s->tick + TFA_GUARDIAN.mushroom_arm + TFA_GUARDIAN.mushroom_life,
        .arm_ticks = (int16_t)TFA_GUARDIAN.mushroom_arm,
        .min_damage = (int16_t)TFA_GUARDIAN.mushroom_min_damage,
        .max_damage = (int16_t)TFA_GUARDIAN.mushroom_max_damage,
        .protected_pct = 100,
        .loc_id = GUARDIAN_LOC_MUSHROOM_SPROUT,
    });
}

/* Two mushrooms per living player: one on the ring at distance 1 and one on the ring at 3 (rel. hist, 2p fights). */
static void tfa_guardian_special(TfaState* s, TfaNpc* g) {
    g->act = TFA_ACT_SPECIAL;
    tfa_npc_anim(s, g, GUARDIAN_ANIM_SPECIAL);
    for (int p = 0; p < s->team_size; p++) {
        if (!tfa_alive(s, p)) continue;
        const Player* pl = &s->players[p].player;
        for (int ring = 1; ring <= 3; ring += 2) {
            int side = tfa_rand(s, 8 * ring);
            int dx, dy;
            if (side < 2 * ring) { dx = -ring + side; dy = ring; }
            else if (side < 4 * ring) { dx = ring; dy = ring - (side - 2 * ring); }
            else if (side < 6 * ring) { dx = ring - (side - 4 * ring); dy = -ring; }
            else { dx = -ring; dy = -ring + (side - 6 * ring); }
            tfa_guardian_mushroom(s, pl->x + dx, pl->y + dy);
        }
    }
}

static void tfa_guardian_mushrooms_tick(TfaState* s) {
    for (int i = 0; i < TFA_MAX_HAZARDS; i++) {
        TfaHazard* h = &s->hazards[i];
        if (h->kind != TFA_HAZ_MUSHROOM) continue;
        if (h->arm_ticks > 0) {
            if (--h->arm_ticks == 0) h->loc_id = GUARDIAN_LOC_MUSHROOM_ARMED;
            continue;
        }
        if (h->arm_ticks < 0) {
            for (int p = 0; p < s->team_size; p++)
                if (tfa_alive(s, p) && chebyshev_distance(h->x, h->y, s->players[p].player.x, s->players[p].player.y) <= 1)
                    tfa_hazard_hit_player(s, h, p);
            h->kind = TFA_HAZ_NONE;
            continue;
        }
        if (s->tick >= h->end_tick) { h->kind = TFA_HAZ_NONE; continue; }
        for (int p = 0; p < s->team_size; p++) {
            if (tfa_alive(s, p) && chebyshev_distance(h->x, h->y, s->players[p].player.x, s->players[p].player.y) <= 1) {
                h->arm_ticks = -1;
                h->loc_id = GUARDIAN_LOC_MUSHROOM_BURST;
                break;
            }
        }
    }
}

static void tfa_guardian_spawn_vines(TfaState* s, TfaNpc* g) {
    int cx2 = tfa_guardian_center2(g, 0), cy2 = tfa_guardian_center2(g, 1);
    for (int p = 0; p < s->team_size; p++) {
        if (!tfa_alive(s, p)) continue;
        const Player* pl = &s->players[p].player;
        int dx = 2 * pl->x - cx2, dy = 2 * pl->y - cy2;
        int x = pl->x + (dx > 0) - (dx < 0), y = pl->y + (dy > 0) - (dy < 0);
        if (!tfa_guardian_tile_walkable(s, x, y)) { x = pl->x; y = pl->y; }
        TfaNpc* v = tfa_npc_spawn_inert(s, TFA_NPC_GUARDIAN_VINE, GUARDIAN_NPC_VINE, 1, x, y);
        v->target = (int8_t)p;
        v->state_ticks = 0;
        for (int k = 0; k < 3; k++) {
            tfa_hazard_add(s, (TfaHazard){
                .kind = TFA_HAZ_STRIKE,
                .style = ATTACK_STYLE_MELEE,
                .w = 3, .h = 3,
                .owner = -1,
                .x = (int16_t)(x - 1), .y = (int16_t)(y - 1),
                .start_tick = s->tick + TFA_GUARDIAN.vine_attacks[k],
                .end_tick = s->tick + TFA_GUARDIAN.vine_attacks[k],
                .min_damage = (int16_t)TFA_GUARDIAN.vine_min_damage,
                .max_damage = (int16_t)TFA_GUARDIAN.vine_max_damage,
                .protected_pct = (int16_t)TFA_GUARDIAN.vine_protected_pct,
            });
        }
    }
}

/* Vines are scenery that animate on their strike ticks; the strikes themselves are hazards. */
static void tfa_guardian_vines_tick(TfaState* s) {
    for (int i = 0; i < TFA_MAX_NPCS; i++) {
        TfaNpc* v = &s->npcs[i];
        if (!v->active || v->kind != TFA_NPC_GUARDIAN_VINE) continue;
        v->state_ticks++;
        if (v->state_ticks >= TFA_GUARDIAN.vine_life) { v->active = 0; continue; }
        for (int k = 0; k < 3; k++)
            if (v->state_ticks == TFA_GUARDIAN.vine_attacks[k]) tfa_npc_anim(s, v, GUARDIAN_ANIM_VINE_ATTACK);
    }
}

/* Twice the perpendicular distance from tile (x, y) to the beam from the boss centre through the target. */
static double tfa_guardian_beam_offset(const TfaNpc* g, int tx, int ty, int x, int y) {
    double cx = tfa_guardian_center2(g, 0) * 0.5, cy = tfa_guardian_center2(g, 1) * 0.5;
    double ax = tx - cx, ay = ty - cy, bx = x - cx, by = y - cy;
    double len = sqrt(ax * ax + ay * ay);
    if (len == 0.0) return 0.0;
    if (ax * bx + ay * by < 0.0) return 1e9;
    return fabs(ax * by - ay * bx) / len;
}

static void tfa_guardian_breath_land(TfaState* s, TfaNpc* g) {
    TfaGuardianState* gs = &s->boss.guardian;
    int t = gs->breath_target;
    if (!tfa_alive(s, t)) return;
    int tx = s->players[t].player.x, ty = s->players[t].player.y;
    int hit[TFA_MAX_PLAYERS], n = 0;
    for (int p = 0; p < s->team_size; p++) {
        if (tfa_alive(s, p) && tfa_guardian_beam_offset(g, tx, ty, s->players[p].player.x, s->players[p].player.y) <= 1.0)
            hit[n++] = p;
    }
    int total = tfa_rand_range(s, TFA_GUARDIAN.breath_total_min, TFA_GUARDIAN.breath_total_max);
    for (int k = 0; k < n; k++) tfa_damage_player_now(s, hit[k], total / n, ATTACK_STYLE_NONE);
    for (int i = 0; i < TFA_MAX_NPCS; i++) {
        TfaNpc* py = &s->npcs[i];
        if (!py->active || py->kind != TFA_NPC_GUARDIAN_PYLON || py->npc_id != GUARDIAN_NPC_PYLON) continue;
        int on_beam = 0;
        for (int dx = 0; dx < py->size; dx++)
            for (int dy = 0; dy < py->size; dy++)
                on_beam |= tfa_guardian_beam_offset(g, tx, ty, py->x + dx, py->y + dy) <= 1.0;
        if (!on_beam) continue;
        py->npc_id = GUARDIAN_NPC_PYLON_DEAD;
        tfa_npc_anim(s, py, GUARDIAN_ANIM_PYLON_DESPAWN);
        gs->pylon_hit_tick = s->tick + TFA_GUARDIAN.pylon_shield_delay;
        break;
    }
}

static void tfa_guardian_bar_damage(TfaState* s, TfaNpc* g, int damage) {
    if (g->shield > 0) {
        g->shield -= damage < g->shield ? damage : g->shield;
        if (g->shield == 0) s->boss.guardian.shield_broke_tick = s->tick;
        return;
    }
    g->hp -= damage < g->hp ? damage : g->hp;
}

/* Flying, only ranged and magic reach it (wiki Strategies); melee lands on the grounded body.
   Hits after the shield breaks, during the 3-tick landing, already drain HP (guardian_04: 21 damage, 2% of 1100). */
static int tfa_guardian_npc_damage(TfaState* s, TfaNpc* n, int damage, int attacker, AttackStyle style) {
    (void)attacker;
    if (n->kind != TFA_NPC_GUARDIAN || damage <= 0) return 0;
    TfaGuardianState* gs = &s->boss.guardian;
    if (style == ATTACK_STYLE_MELEE && gs->phase != TFA_GUARDIAN_GROUNDED) return 0;
    int before = n->shield + n->hp;
    if (n->shield > 0 && gs->phase != TFA_GUARDIAN_GROUNDED) tfa_guardian_bar_damage(s, n, damage);
    else n->hp -= damage < n->hp ? damage : n->hp;
    return before - (n->shield + n->hp);
}

static void tfa_guardian_npc_died(TfaState* s, TfaNpc* n, int attacker) {
    (void)attacker;
    assert(n->kind == TFA_NPC_GUARDIAN);
    tfa_npc_anim(s, n, GUARDIAN_ANIM_DEATH);
    n->npc_id = GUARDIAN_NPC_DEAD;
    n->attackable = 0;
    n->active = 0;
    s->boss.guardian.phase = TFA_GUARDIAN_DYING;
}

static void tfa_guardian_tick(TfaState* s, TfaContext* ctx) {
    TfaGuardianState* gs = &s->boss.guardian;
    if (gs->phase == TFA_GUARDIAN_DYING) {
        tfa_room_cleared(s, ctx);
        return;
    }
    TfaNpc* g = tfa_guardian_boss(s);
    g->act = TFA_ACT_IDLE;
    tfa_guardian_mushrooms_tick(s);
    tfa_guardian_vines_tick(s);

    if (gs->pylon_hit_tick == s->tick) tfa_guardian_bar_damage(s, g, TFA_GUARDIAN.pylon_shield_damage);
    if (gs->breath_land_tick == s->tick) tfa_guardian_breath_land(s, g);

    int threshold = g->max_hp * TFA_GUARDIAN.enrage_pct / 100;
    gs->phase_ticks++;
    switch ((TfaGuardianPhase)gs->phase) {
        case TFA_GUARDIAN_FLYING:
            if (gs->shield_broke_tick > 0 && !gs->enraged) {
                if (s->tick - gs->shield_broke_tick < TFA_GUARDIAN.land_delay) break;
                gs->shield_broke_tick = 0;
                gs->phase = TFA_GUARDIAN_GROUNDED;
                gs->phase_ticks = 0;
                gs->knockdowns++;
                g->npc_id = GUARDIAN_NPC_GROUNDED;
                tfa_npc_anim(s, g, GUARDIAN_ANIM_LAND);
                gs->vine_timer = TFA_GUARDIAN.vine_first;
                break;
            }
            if (gs->enraged) {
                if (gs->shield_broke_tick > 0) {
                    gs->shield_broke_tick = 0;
                    g->npc_id = GUARDIAN_NPC_GROUNDED;
                }
                if (--gs->special_timer == 0) {
                    tfa_guardian_special(s, g);
                    gs->swoosh_tick = s->tick + 3;
                    gs->breath_tick = s->tick + 5;
                    gs->special_timer = 20;
                }
                if (gs->swoosh_tick == s->tick) {
                    int k = tfa_rand(s, (int)(sizeof(TFA_GUARDIAN_SWOOSH_TILES) / sizeof(*TFA_GUARDIAN_SWOOSH_TILES)));
                    g->x = TFA_GUARDIAN_SWOOSH_TILES[k][0];
                    g->y = TFA_GUARDIAN_SWOOSH_TILES[k][1];
                    tfa_npc_anim(s, g, GUARDIAN_ANIM_SWOOSH);
                }
                if (gs->breath_tick == s->tick) {
                    g->act = TFA_ACT_MAGIC;
                    tfa_npc_anim(s, g, GUARDIAN_ANIM_BREATH);
                    int candidates[TFA_MAX_PLAYERS], n = 0;
                    for (int p = 0; p < s->team_size; p++) if (tfa_alive(s, p)) candidates[n++] = p;
                    if (n > 0) {
                        gs->breath_target = candidates[tfa_rand(s, n)];
                        g->target = (int8_t)gs->breath_target;
                        gs->breath_land_tick = s->tick + TFA_GUARDIAN.breath_land;
                    }
                }
                break;
            }
            if (--gs->ranged_timer > 0) break;
            gs->ranged_timer = TFA_GUARDIAN.ranged_interval;
            gs->special_timer -= TFA_GUARDIAN.ranged_interval;
            if (gs->special_timer <= 0) {
                tfa_guardian_special(s, g);
                gs->special_timer = TFA_GUARDIAN.special_intervals[tfa_rand(s, 2)];
            } else {
                tfa_guardian_ranged(s, g);
            }
            break;
        case TFA_GUARDIAN_GROUNDED: {
            if (--gs->vine_timer != 0) break;
            gs->vine_timer = TFA_GUARDIAN.vine_interval;
            int base = TFA_GUARDIAN.ground_base + TFA_GUARDIAN.ground_step * (gs->knockdowns - 1);
            int rise = gs->phase_ticks >= base || (gs->cross_tick > 0 && gs->phase_ticks >= TFA_GUARDIAN.ground_min);
            if (!rise && g->hp <= threshold && gs->cross_tick == 0) gs->cross_tick = gs->phase_ticks;
            if (!rise) {
                tfa_guardian_spawn_vines(s, g);
                break;
            }
            gs->phase = TFA_GUARDIAN_FLYING;
            gs->phase_ticks = 0;
            g->npc_id = GUARDIAN_NPC_FLYING;
            g->shield = g->max_shield;
            tfa_npc_anim(s, g, GUARDIAN_ANIM_RISE);
            if (gs->cross_tick > 0) {
                gs->enraged = 1;
                gs->special_timer = TFA_GUARDIAN.rise_to_attack;
            } else {
                gs->ranged_timer = TFA_GUARDIAN.rise_to_attack;
                gs->special_timer = TFA_GUARDIAN.first_special;
            }
            break;
        }
        case TFA_GUARDIAN_DYING:
            break;
    }
    g->attack_timer = (int16_t)(gs->enraged ? (gs->breath_tick > s->tick ? gs->breath_tick - s->tick : gs->special_timer) : gs->ranged_timer);
}
