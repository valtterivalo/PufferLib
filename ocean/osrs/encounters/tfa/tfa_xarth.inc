/* Xarth. A fixed script measured from 4 recorded fights (fixtures/tfa_xarth_events.tsv):
   P1 is three 31-tick pillar cycles, P2 a 21-tick ranged cycle that summons corpses. */

#define XARTH_NPC 16684
#define XARTH_NPC_PILLAR 16685
#define XARTH_NPC_PILLAR_BROKEN 16686
#define XARTH_NPC_CORPSE 16687

#define XARTH_ANIM_MELEE 14983
#define XARTH_ANIM_MAGIC 14984
#define XARTH_ANIM_RANGED 14985
#define XARTH_ANIM_BLAST 14989
#define XARTH_ANIM_AOE 14992
#define XARTH_ANIM_DEATH 14995
#define XARTH_ANIM_PILLAR_BREAK 14997
#define XARTH_GFX_AOE 4379
#define XARTH_PROJ_RANGED 4374
#define XARTH_PROJ_MAGIC 4034

typedef struct {
    int first_attack;            /* first magic 6 ticks after "Challenge started", 4/4 */
    int magic_interval;          /* P1 magic every 4: 44/44 */
    int p1_cycle;                /* AOE at 20, 51, 82: 31-tick cycles, 4/4 fights */
    int p1_cycles;
    int p1_magics;               /* five magics then the AOE */
    int aoe_to_blast;            /* 14992 -> 14989 three ticks later, 12/12 */
    int aoe_land;                /* AOE tiles hit at 14992 +4 for 15..19 */
    int aoe_min_damage, aoe_max_damage;
    int blast_land;              /* blast hits players or pillars at 14989 +3 */
    int blast_min_damage, blast_max_damage; /* 42..57 on every exposed player */
    int pillar_cone_degrees;     /* players within this angle behind a pillar are shielded; fitted on 12 blasts, provisional */
    int p2_start;                /* first ranged 13 ticks after the last AOE, 4/4 */
    int p2_cycle;                /* R R R R M then the next R: 21 ticks */
    int ranged_launch;           /* 4374 first seen at anim +2 in 239/302 */
    int ranged_hit;              /* 4374 at anim +2, flight 2: hit +4 (110/111) */
    int magic_launch;            /* 4034 first seen at anim +1 in 125/207 */
    int magic_hit;               /* 4034 at anim +1, flight 2: hit +3 */
    int ranged_max_hit;          /* unprotected 7..32 on teammates; Protect from Missiles blocks 107/107 */
    int magic_max_hit;           /* one unprotected 34 */
    int magic_protected_pct;     /* Protect from Magic leaves 0..9 (n=19), applied to the landing-tick prayer */
    int magic_two_targets_pct;   /* 126 of 212 magics fire two projectiles */
    int double_magic_pct;        /* a second magic 2 ticks after a P2 magic slot: 12 of about 60 slots, trigger unknown */
    int melee_max_hit;           /* 27, 15, 9 through Protect from Missiles */
    int corpse_offset;           /* corpses spawn 2 and 0 ticks before a cycle's first ranged */
    int heal_blast;              /* wiki: 200 healed starts an arena blast; P2 blasts 42..57 on all, then +10 to the next attack */
    int heal_blast_recover;
} TfaXarthTuning;

static const TfaXarthTuning TFA_XARTH = {
    .first_attack = 6,
    .magic_interval = 4,
    .p1_cycle = 31,
    .p1_cycles = 3,
    .p1_magics = 5,
    .aoe_to_blast = 3,
    .aoe_land = 4,
    .aoe_min_damage = 15,
    .aoe_max_damage = 19,
    .blast_land = 3,
    .blast_min_damage = 42,
    .blast_max_damage = 57,
    .pillar_cone_degrees = 25,
    .p2_start = 13,
    .p2_cycle = 21,
    .ranged_launch = 2,
    .ranged_hit = 4,
    .magic_launch = 1,
    .magic_hit = 3,
    .ranged_max_hit = 32,
    .magic_max_hit = 34,
    .magic_protected_pct = 26,
    .magic_two_targets_pct = 59,
    .double_magic_pct = 20,
    .melee_max_hit = 27,
    .corpse_offset = 2,
    .heal_blast = 200,
    .heal_blast_recover = 10,
};

static const int8_t TFA_XARTH_PILLAR_TILES[3][2] = { {33, 26}, {33, 36}, {36, 32} };

/* Arena interior is x, y in [24, 38]; corpses rise on the ring at 23 and 39. */
#define XARTH_RING_LO 23
#define XARTH_RING_HI 39

static TfaNpc* tfa_xarth_boss(TfaState* s) {
    TfaNpc* n = tfa_npc_first(s, TFA_NPC_XARTH);
    assert(n);
    return n;
}

static void tfa_xarth_enter(TfaState* s) {
    const TfaRoomLayout* l = tfa_room_layout(TFA_ROOM_XARTH);
    tfa_npc_spawn(s, TFA_NPC_XARTH, MON_TFA_XARTH, XARTH_NPC, l->boss_x, l->boss_y);
    for (int i = 0; i < 3; i++)
        tfa_npc_spawn_inert(s, TFA_NPC_XARTH_PILLAR, XARTH_NPC_PILLAR, 2,
            TFA_XARTH_PILLAR_TILES[i][0], TFA_XARTH_PILLAR_TILES[i][1]);
    s->boss.xarth = (TfaXarthState){ .next_attack = TFA_XARTH.first_attack, .blast_land_tick = -1 };
}

static int tfa_xarth_random_alive(TfaState* s, int exclude) {
    int c[TFA_MAX_PLAYERS], n = 0;
    for (int p = 0; p < s->team_size; p++) if (tfa_alive(s, p) && p != exclude) c[n++] = p;
    return n ? c[tfa_rand(s, n)] : -1;
}

static void tfa_xarth_magic(TfaState* s, TfaNpc* x) {
    x->act = TFA_ACT_MAGIC;
    tfa_npc_anim(s, x, XARTH_ANIM_MAGIC);
    int first = tfa_xarth_random_alive(s, -1);
    if (first < 0) return;
    int targets[2] = { first, -1 };
    if (tfa_rand(s, 100) < TFA_XARTH.magic_two_targets_pct) targets[1] = tfa_xarth_random_alive(s, first);
    x->target = (int8_t)first;
    for (int k = 0; k < 2; k++) {
        int p = targets[k];
        if (p < 0) continue;
        tfa_queue_npc_hit_land(s, p, tfa_npc_roll(s, x, p, ATTACK_STYLE_MAGIC, TFA_XARTH.magic_max_hit),
            ATTACK_STYLE_MAGIC, TFA_XARTH.magic_hit, TFA_XARTH.magic_protected_pct, x);
        tfa_fx_projectile(s, x, p, XARTH_PROJ_MAGIC, SPOTANIM_TFA_XARTH_TFA_UNNAMED_4034, SPOTANIM_TFA_XARTH_TFA_UNNAMED_4034_ANIM,
            TFA_XARTH.magic_launch, TFA_XARTH.magic_hit - TFA_XARTH.magic_launch);
    }
}

static void tfa_xarth_ranged(TfaState* s, TfaNpc* x) {
    for (int p = 0; p < s->team_size; p++) {
        if (!tfa_alive(s, p)) continue;
        const Player* pl = &s->players[p].player;
        if (encounter_entity_footprint_cardinal_adjacent(pl->x, pl->y, 1, x->x, x->y, x->size)) {
            x->act = TFA_ACT_MELEE;
            x->target = (int8_t)p;
            tfa_npc_anim(s, x, XARTH_ANIM_MELEE);
            tfa_queue_npc_hit(s, p, tfa_npc_roll(s, x, p, ATTACK_STYLE_MELEE, TFA_XARTH.melee_max_hit), ATTACK_STYLE_MELEE, 0, 0, x);
            return;
        }
    }
    int p = tfa_xarth_random_alive(s, -1);
    if (p < 0) return;
    x->act = TFA_ACT_RANGED;
    x->target = (int8_t)p;
    tfa_npc_anim(s, x, XARTH_ANIM_RANGED);
    tfa_queue_npc_hit_land(s, p, tfa_npc_roll(s, x, p, ATTACK_STYLE_RANGED, TFA_XARTH.ranged_max_hit), ATTACK_STYLE_RANGED, TFA_XARTH.ranged_hit, 0, x);
    tfa_fx_projectile(s, x, p, XARTH_PROJ_RANGED, SPOTANIM_TFA_XARTH_VFX_XARTH_ATTACK_RANGED_PROJECTILE_01,
        SPOTANIM_TFA_XARTH_VFX_XARTH_ATTACK_RANGED_PROJECTILE_01_ANIM, TFA_XARTH.ranged_launch, TFA_XARTH.ranged_hit - TFA_XARTH.ranged_launch);
}

/* AOE tiles: each player's tile and, half the time, one neighbour (4-6 tiles for 3 players, 12/12 casts). */
static void tfa_xarth_aoe(TfaState* s, TfaNpc* x) {
    x->act = TFA_ACT_SPECIAL;
    tfa_npc_anim(s, x, XARTH_ANIM_AOE);
    for (int p = 0; p < s->team_size; p++) {
        if (!tfa_alive(s, p)) continue;
        const Player* pl = &s->players[p].player;
        int tiles = 1 + tfa_rand(s, 2);
        for (int k = 0; k < tiles; k++) {
            int dx = 0, dy = 0;
            if (k == 1) {
                int a = 1 + tfa_rand(s, 8);
                dx = ENCOUNTER_MOVE_TARGET_DX[a] > 0 ? 1 : ENCOUNTER_MOVE_TARGET_DX[a] < 0 ? -1 : 0;
                dy = ENCOUNTER_MOVE_TARGET_DY[a] > 0 ? 1 : ENCOUNTER_MOVE_TARGET_DY[a] < 0 ? -1 : 0;
            }
            tfa_hazard_add(s, (TfaHazard){
                .kind = TFA_HAZ_STRIKE,
                .style = ATTACK_STYLE_NONE,
                .w = 1, .h = 1,
                .owner = -1,
                .x = (int16_t)(pl->x + dx), .y = (int16_t)(pl->y + dy),
                .start_tick = s->tick + TFA_XARTH.aoe_land,
                .end_tick = s->tick + TFA_XARTH.aoe_land,
                .min_damage = (int16_t)TFA_XARTH.aoe_min_damage,
                .max_damage = (int16_t)TFA_XARTH.aoe_max_damage,
                .protected_pct = 100,
                .gfx_id = XARTH_GFX_AOE,
            });
        }
    }
}

static void tfa_xarth_blast(TfaState* s, TfaNpc* x) {
    x->act = TFA_ACT_SPECIAL;
    tfa_npc_anim(s, x, XARTH_ANIM_BLAST);
    s->boss.xarth.blast_land_tick = s->tick + TFA_XARTH.blast_land;
}

/* A standing pillar closer than the player and within the cone shields them and breaks. */
static void tfa_xarth_blast_land(TfaState* s, TfaNpc* x) {
    double cx = x->x + (x->size - 1) * 0.5, cy = x->y + (x->size - 1) * 0.5;
    double cone = TFA_XARTH.pillar_cone_degrees * TFA_PI / 180.0;
    int broken[TFA_MAX_NPCS] = {0};
    for (int p = 0; p < s->team_size; p++) {
        if (!tfa_alive(s, p)) continue;
        const Player* pl = &s->players[p].player;
        double px = pl->x - cx, py = pl->y - cy;
        int shield = -1;
        for (int i = 0; i < TFA_MAX_NPCS; i++) {
            const TfaNpc* pi = &s->npcs[i];
            if (!pi->active || pi->kind != TFA_NPC_XARTH_PILLAR || pi->npc_id != XARTH_NPC_PILLAR) continue;
            double qx = pi->x + 0.5 - cx, qy = pi->y + 0.5 - cy;
            double d = fabs(remainder(atan2(py, px) - atan2(qy, qx), 2.0 * TFA_PI));
            if (d <= cone && qx * qx + qy * qy < px * px + py * py) { shield = i; break; }
        }
        if (shield >= 0) { broken[shield] = 1; continue; }
        tfa_damage_player_now(s, p, tfa_rand_range(s, TFA_XARTH.blast_min_damage, TFA_XARTH.blast_max_damage), ATTACK_STYLE_NONE);
    }
    for (int i = 0; i < TFA_MAX_NPCS; i++) {
        if (!broken[i]) continue;
        s->npcs[i].npc_id = XARTH_NPC_PILLAR_BROKEN;
        tfa_npc_anim(s, &s->npcs[i], XARTH_ANIM_PILLAR_BREAK);
        s->npcs[i].state_ticks = 3;
    }
}

static void tfa_xarth_spawn_corpse(TfaState* s) {
    int side = tfa_rand(s, 4), along = XARTH_RING_LO + tfa_rand(s, XARTH_RING_HI - XARTH_RING_LO + 1);
    int x = side == 0 ? XARTH_RING_LO : side == 1 ? XARTH_RING_HI : along;
    int y = side == 2 ? XARTH_RING_LO : side == 3 ? XARTH_RING_HI : along;
    tfa_npc_spawn(s, TFA_NPC_XARTH_CORPSE, MON_TFA_XARTH_CORPSE, XARTH_NPC_CORPSE, x, y);
}

/* Corpses walk one tile a tick straight at Xarth and heal him their remaining HP on contact. */
static void tfa_xarth_corpses_tick(TfaState* s, TfaNpc* x) {
    for (int i = 0; i < TFA_MAX_NPCS; i++) {
        TfaNpc* c = &s->npcs[i];
        if (!c->active || c->kind != TFA_NPC_XARTH_CORPSE) continue;
        if (encounter_rect_distance(c->x, c->y, 1, x->x, x->y, x->size) <= 1) {
            int heal = c->hp;
            x->hp = x->hp + heal > x->max_hp ? x->max_hp : x->hp + heal;
            s->boss.xarth.healed += heal;
            c->active = 0;
            continue;
        }
        int tx = x->x + 1, ty = x->y + 1;
        c->x += (tx > c->x) - (tx < c->x);
        c->y += (ty > c->y) - (ty < c->y);
    }
}

static int tfa_xarth_npc_damage(TfaState* s, TfaNpc* n, int damage, int attacker, AttackStyle style) {
    (void)s; (void)attacker; (void)style;
    if (n->kind != TFA_NPC_XARTH && n->kind != TFA_NPC_XARTH_CORPSE) return 0;
    int applied = damage < n->hp ? damage : n->hp;
    n->hp -= applied;
    return applied;
}

static void tfa_xarth_npc_died(TfaState* s, TfaNpc* n, int attacker) {
    (void)attacker;
    if (n->kind == TFA_NPC_XARTH_CORPSE) { n->active = 0; return; }
    tfa_npc_anim(s, n, XARTH_ANIM_DEATH);
    n->attackable = 0;
    n->active = 0;
    s->boss.xarth.dead = 1;
}

static void tfa_xarth_tick(TfaState* s, TfaContext* ctx) {
    TfaXarthState* xs = &s->boss.xarth;
    if (xs->dead) {
        tfa_room_cleared(s, ctx);
        return;
    }
    TfaNpc* x = tfa_xarth_boss(s);
    x->act = TFA_ACT_IDLE;
    xs->clock++;
    for (int i = 0; i < TFA_MAX_NPCS; i++) {
        TfaNpc* p = &s->npcs[i];
        if (p->active && p->npc_id == XARTH_NPC_PILLAR_BROKEN && --p->state_ticks <= 0) p->active = 0;
    }
    if (xs->blast_land_tick == s->tick) tfa_xarth_blast_land(s, x);
    tfa_xarth_corpses_tick(s, x);

    int p1_end = TFA_XARTH.p1_cycle * (TFA_XARTH.p1_cycles - 1) + TFA_XARTH.first_attack + TFA_XARTH.magic_interval * TFA_XARTH.p1_magics;
    if (xs->clock <= p1_end + TFA_XARTH.aoe_to_blast) {
        int k = (xs->clock - TFA_XARTH.first_attack) % TFA_XARTH.p1_cycle;
        if (xs->clock < TFA_XARTH.first_attack || k < 0) return;
        if (k < TFA_XARTH.magic_interval * TFA_XARTH.p1_magics && k % TFA_XARTH.magic_interval == 0) tfa_xarth_magic(s, x);
        else if (k == TFA_XARTH.magic_interval * TFA_XARTH.p1_magics) tfa_xarth_aoe(s, x);
        else if (k == TFA_XARTH.magic_interval * TFA_XARTH.p1_magics + TFA_XARTH.aoe_to_blast) tfa_xarth_blast(s, x);
        if (xs->clock == p1_end) xs->next_attack = xs->clock + TFA_XARTH.p2_start;
        x->attack_timer = (int16_t)(TFA_XARTH.magic_interval - k % TFA_XARTH.magic_interval);
        return;
    }

    if (!xs->p2_started && xs->clock == xs->next_attack) {
        xs->p2_started = 1;
        xs->slot = 2;
        xs->cycle_start = xs->clock + 2 * TFA_XARTH.magic_interval + TFA_XARTH.p2_cycle - 4 * TFA_XARTH.magic_interval;
    }
    if (xs->clock == xs->cycle_start - TFA_XARTH.corpse_offset || xs->clock == xs->cycle_start) tfa_xarth_spawn_corpse(s);
    if (xs->clock == xs->double_magic_tick) tfa_xarth_magic(s, x);
    if (xs->clock == xs->next_attack) {
        if (xs->healed >= TFA_XARTH.heal_blast) {
            xs->healed = 0;
            tfa_xarth_blast(s, x);
            xs->next_attack = xs->clock + TFA_XARTH.heal_blast_recover;
            xs->cycle_start = xs->next_attack;
            xs->slot = 0;
        } else {
            int magic = xs->slot == 4;
            if (magic) tfa_xarth_magic(s, x);
            else tfa_xarth_ranged(s, x);
            if (magic && tfa_rand(s, 100) < TFA_XARTH.double_magic_pct) xs->double_magic_tick = xs->clock + 2;
            xs->slot = (xs->slot + 1) % 5;
            if (xs->slot == 0) {
                xs->cycle_start = xs->clock + TFA_XARTH.p2_cycle - 4 * TFA_XARTH.magic_interval;
                xs->next_attack = xs->cycle_start;
            } else {
                xs->next_attack = xs->clock + TFA_XARTH.magic_interval;
            }
        }
    }
    x->attack_timer = (int16_t)(xs->next_attack - xs->clock);
}
