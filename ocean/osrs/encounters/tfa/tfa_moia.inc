/* Moia and her Undead binders. Measured from ONE recorded fight (fixtures/tfa_moia_events.tsv, 3 players,
   a 54 tick wipe), so every number is a single sample unless the comment says otherwise. Not modelled
   because the recording never shows them: chant 14787, shadow shield (varbit 16418), infect 14795/14796,
   the blue special, Moia's later teleport targets. */

#define MOIA_NPC 16667
#define MOIA_NPC_BINDER 16670

#define MOIA_ANIM_DAGGER 14785
#define MOIA_ANIM_TELEPORT 14788
#define MOIA_ANIM_BINDER_ATTACK 14791
#define MOIA_PROJ_DAGGER 4214
#define MOIA_PROJ_MAGIC 4213

typedef struct {
    int first_attack;            /* first dagger 8 after "Challenge started", 1/1 */
    int dagger_interval;         /* 14785 -> 14785 5 in 8/9 */
    int first_loop_daggers;      /* daggers at 0 5 10 15, teleport at 20: 1/1 */
    int teleport_first;
    int loop;                    /* teleport 20 -> 45, 1/1: teleport, magic at +1, daggers at +5 +10 +15 +20 */
    int magic_offset;            /* the dagger anim 1 after the teleport throws 4213: 1/1 */
    int dagger_offset;
    int dagger_hit;              /* 4214 hits 1 after the anim (4 of 4 aimed hits, flight 0.5) */
    int magic_hit;               /* 4213 flight 5.5: hit 6 after its anim, 1/1 */
    int dagger_max_hit;          /* unprotected 15 16 21: provisional */
    int magic_max_hit;           /* unprotected 26 and 35: provisional */
    int projectile_delay;        /* 4214 and 4213 first seen 1 after the anim, 8/8 */
    int dagger_flight;           /* 4214 flight 0.5, 19/19: drawn as 1 tick */
    int magic_flight;            /* 4213 flight 5.5 rounded down, 3/3 */
    int binder_ring_length;      /* all 8 recorded binder tracks lie on one 36 tile loop */
    int binder_move_first;       /* binders step 4 tiles at 5..8, 20..23, 35..38, 50..53: every 15 from 5 */
    int binder_move_ticks;
    int binder_cycle;
    int binder_attack_first;     /* binder 14791 at 12 17 27 32 42 47 (t mod 15 in {12, 2}), first at 12 */
    int binder_attack_phase_a;
    int binder_attack_phase_b;
    int binder_reach;            /* hits players within 2 tiles, 15 hits of 4 binders */
    int binder_min_damage, binder_max_damage; /* 3..22 unprotected over 19 hits, 0 through Protect from Melee (1/1 blocked) */
    int binder_spawn_first;      /* a ninth binder at the centre at 15, 1/1 */
    int binder_respawn;          /* respawn after a death: provisional, no recorded kill */
    int binders;
    int binder_heal_teleport;    /* binders and Moia healed at the second teleport (35 32 57 55 back to full, Moia +30): 1/1 */
    int moia_heal;
    int shield_pct;              /* shadow shield unmodelled: 144 dealt dropped her bar 6% (about 47 of 785), 35 percent through, provisional */
} TfaMoiaTuning;

static const TfaMoiaTuning TFA_MOIA = {
    .first_attack = 8,
    .dagger_interval = 5,
    .first_loop_daggers = 4,
    .teleport_first = 20,
    .loop = 25,
    .magic_offset = 1,
    .dagger_offset = 5,
    .dagger_hit = 1,
    .magic_hit = 6,
    .dagger_max_hit = 22,
    .magic_max_hit = 35,
    .projectile_delay = 1,
    .dagger_flight = 1,
    .magic_flight = 5,
    .binder_ring_length = 36,
    .binder_move_first = 5,
    .binder_move_ticks = 4,
    .binder_cycle = 15,
    .binder_attack_first = 12,
    .binder_attack_phase_a = 12,
    .binder_attack_phase_b = 2,
    .binder_reach = 2,
    .binder_min_damage = 3,
    .binder_max_damage = 22,
    .binder_spawn_first = 15,
    .binder_respawn = 15,
    .binders = 8,
    .binder_heal_teleport = 2,
    .moia_heal = 30,
    .shield_pct = 35,
};

/* The binder loop around the arena centre (31, 31): up the east edge, in along the top, down the west edge,
   through the centre and back out along the bottom. */
static const int8_t TFA_MOIA_LOOP[36][2] = {
    {35, 35}, {35, 34}, {35, 33}, {35, 32}, {35, 31}, {35, 30}, {35, 29}, {35, 28}, {35, 27}, {34, 28}, {33, 29},
    {32, 29}, {31, 29}, {30, 28}, {29, 27}, {28, 27}, {27, 27}, {27, 28}, {27, 29}, {27, 30}, {27, 31}, {28, 31},
    {29, 31}, {30, 31}, {31, 31}, {30, 32}, {29, 33}, {28, 34}, {27, 35}, {28, 34}, {29, 33}, {30, 33}, {31, 33},
    {32, 34}, {33, 35}, {34, 35},
};

/* Loop index of each initial binder, binders #1..#8 of the recording. */
static const int8_t TFA_MOIA_BINDER_START[8] = { 0, 28, 32, 24, 12, 16, 4, 8 };
#define MOIA_LOOP_CENTRE 24

/* Moia stands at the west spawn, then teleports north (30, 23) and south (30, 37): 3 of 3 seen, cycled. */
static const int8_t TFA_MOIA_TELEPORTS[3][2] = { {23, 30}, {30, 23}, {30, 37} };

static TfaNpc* tfa_moia_boss(TfaState* s) {
    TfaNpc* n = tfa_npc_first(s, TFA_NPC_MOIA);
    assert(n);
    return n;
}

static TfaNpc* tfa_moia_spawn_binder(TfaState* s, int loop_index) {
    int free_target_slot = 0;
    for (int i = 0; i < TFA_NUM_TARGET_SLOTS; i++) free_target_slot |= !s->npcs[i].active;
    int x = TFA_MOIA_LOOP[loop_index][0], y = TFA_MOIA_LOOP[loop_index][1];
    TfaNpc* b = free_target_slot
        ? tfa_npc_spawn(s, TFA_NPC_BINDER, MON_TFA_BINDER, MOIA_NPC_BINDER, x, y)
        : tfa_npc_spawn_inert(s, TFA_NPC_BINDER, MOIA_NPC_BINDER, 1, x, y);
    if (!free_target_slot) {
        b->hp = b->max_hp = MONSTER_DATABASE[MON_TFA_BINDER].hp;
        b->monster = MON_TFA_BINDER;
    }
    b->counter = (int16_t)loop_index;
    return b;
}

static void tfa_moia_enter(TfaState* s) {
    const TfaRoomLayout* l = tfa_room_layout(TFA_ROOM_MOIA);
    TfaNpc* m = tfa_npc_spawn(s, TFA_NPC_MOIA, MON_TFA_MOIA, MOIA_NPC, l->boss_x, l->boss_y);
    assert(tfa_npc_slot(s, m) == 0);
    for (int i = 0; i < TFA_MOIA.binders; i++) tfa_moia_spawn_binder(s, TFA_MOIA_BINDER_START[i]);
    s->boss.moia = (TfaMoiaState){ .respawn_tick = -1, .magic_land_tick = -1 };
}

/* An attack on every living player, aimed at the throw tick and landing after delay. */
static void tfa_moia_volley(TfaState* s, TfaNpc* m, AttackStyle style, int max_hit, int delay) {
    for (int p = 0; p < s->team_size; p++) {
        if (!tfa_alive(s, p)) continue;
        tfa_queue_npc_hit_land(s, p, tfa_npc_roll(s, m, p, style, max_hit), style, delay, 0, m);
    }
}

static void tfa_moia_fx_volley(TfaState* s, TfaNpc* m, int magic) {
    for (int p = 0; p < s->team_size; p++) {
        if (!tfa_alive(s, p)) continue;
        if (magic)
            tfa_fx_projectile(s, m, p, MOIA_PROJ_MAGIC, SPOTANIM_TFA_MOIA_MOIA_DAGGER_MAGIC, SPOTANIM_TFA_MOIA_MOIA_DAGGER_MAGIC_ANIM,
                TFA_MOIA.projectile_delay, TFA_MOIA.magic_flight);
        else
            tfa_fx_projectile(s, m, p, MOIA_PROJ_DAGGER, SPOTANIM_TFA_MOIA_MOIA_DAGGER, SPOTANIM_TFA_MOIA_MOIA_DAGGER_ANIM,
                TFA_MOIA.projectile_delay, TFA_MOIA.dagger_flight);
    }
}

static void tfa_moia_binders_tick(TfaState* s, int t) {
    int phase = t % TFA_MOIA.binder_cycle;
    int moving = t >= TFA_MOIA.binder_move_first &&
        phase >= TFA_MOIA.binder_move_first && phase < TFA_MOIA.binder_move_first + TFA_MOIA.binder_move_ticks;
    int attacking = t >= TFA_MOIA.binder_attack_first &&
        (phase == TFA_MOIA.binder_attack_phase_a || phase == TFA_MOIA.binder_attack_phase_b);
    for (int i = 0; i < TFA_MAX_NPCS; i++) {
        TfaNpc* b = &s->npcs[i];
        if (!b->active || b->kind != TFA_NPC_BINDER) continue;
        b->act = TFA_ACT_IDLE;
        if (moving) {
            b->counter = (int16_t)((b->counter + 1) % TFA_MOIA.binder_ring_length);
            b->x = TFA_MOIA_LOOP[b->counter][0];
            b->y = TFA_MOIA_LOOP[b->counter][1];
        }
        if (!attacking) continue;
        int target = -1, best = TFA_MOIA.binder_reach + 1;
        for (int p = 0; p < s->team_size; p++) {
            if (!tfa_alive(s, p)) continue;
            int d = chebyshev_distance(b->x, b->y, s->players[p].player.x, s->players[p].player.y);
            if (d < best) { best = d; target = p; }
        }
        if (target < 0) continue;
        b->act = TFA_ACT_MELEE;
        b->target = (int8_t)target;
        tfa_npc_anim(s, b, MOIA_ANIM_BINDER_ATTACK);
        tfa_queue_npc_hit(s, target, tfa_rand_range(s, TFA_MOIA.binder_min_damage, TFA_MOIA.binder_max_damage),
            ATTACK_STYLE_MELEE, 0, 0, b);
    }
}

static int tfa_moia_npc_damage(TfaState* s, TfaNpc* n, int damage, int attacker, AttackStyle style) {
    (void)s; (void)attacker; (void)style;
    if (n->kind != TFA_NPC_MOIA && n->kind != TFA_NPC_BINDER) return 0;
    if (n->kind == TFA_NPC_MOIA) damage = (damage * TFA_MOIA.shield_pct + 99) / 100;
    int applied = damage < n->hp ? damage : n->hp;
    n->hp -= applied;
    return applied;
}

static void tfa_moia_npc_died(TfaState* s, TfaNpc* n, int attacker) {
    (void)attacker;
    TfaMoiaState* ms = &s->boss.moia;
    n->attackable = 0;
    n->active = 0;
    if (n->kind == TFA_NPC_MOIA) {
        ms->dead = 1;
        return;
    }
    ms->respawn_tick = s->tick + TFA_MOIA.binder_respawn;
}

static void tfa_moia_tick(TfaState* s, TfaContext* ctx) {
    TfaMoiaState* ms = &s->boss.moia;
    if (ms->dead) {
        tfa_room_cleared(s, ctx);
        return;
    }
    TfaNpc* m = tfa_moia_boss(s);
    m->act = TFA_ACT_IDLE;
    ms->clock++;
    int t = ms->clock - TFA_MOIA.first_attack;

    if (t == TFA_MOIA.binder_spawn_first || ms->respawn_tick == s->tick)
        tfa_moia_spawn_binder(s, MOIA_LOOP_CENTRE);
    if (t >= 0) tfa_moia_binders_tick(s, t);

    int dagger = 0, magic = 0, teleport = 0;
    if (t >= 0 && t < TFA_MOIA.teleport_first) {
        dagger = t % TFA_MOIA.dagger_interval == 0;
    } else if (t >= TFA_MOIA.teleport_first) {
        int u = (t - TFA_MOIA.teleport_first) % TFA_MOIA.loop;
        teleport = u == 0;
        magic = u == TFA_MOIA.magic_offset;
        dagger = u >= TFA_MOIA.dagger_offset && u % TFA_MOIA.dagger_interval == 0;
    }
    if (teleport) {
        ms->teleports++;
        m->act = TFA_ACT_SPECIAL;
        tfa_npc_anim(s, m, MOIA_ANIM_TELEPORT);
        if (ms->teleports >= TFA_MOIA.binder_heal_teleport) {
            for (int i = 0; i < TFA_MAX_NPCS; i++)
                if (s->npcs[i].active && s->npcs[i].kind == TFA_NPC_BINDER) s->npcs[i].hp = s->npcs[i].max_hp;
            m->hp = m->hp + TFA_MOIA.moia_heal > m->max_hp ? m->max_hp : m->hp + TFA_MOIA.moia_heal;
        }
    }
    if (ms->teleports > 0 && t == TFA_MOIA.teleport_first + (ms->teleports - 1) * TFA_MOIA.loop + 1) {
        m->x = TFA_MOIA_TELEPORTS[ms->teleports % 3][0];
        m->y = TFA_MOIA_TELEPORTS[ms->teleports % 3][1];
    }
    if (dagger) {
        m->act = TFA_ACT_RANGED;
        tfa_npc_anim(s, m, MOIA_ANIM_DAGGER);
        tfa_moia_volley(s, m, ATTACK_STYLE_RANGED, TFA_MOIA.dagger_max_hit, TFA_MOIA.dagger_hit);
        tfa_moia_fx_volley(s, m, 0);
    }
    if (magic) {
        m->act = TFA_ACT_MAGIC;
        tfa_npc_anim(s, m, MOIA_ANIM_DAGGER);
        tfa_moia_volley(s, m, ATTACK_STYLE_MAGIC, TFA_MOIA.magic_max_hit, TFA_MOIA.magic_hit);
        tfa_moia_fx_volley(s, m, 1);
        ms->magic_land_tick = s->tick + TFA_MOIA.magic_hit;
    }
    m->attack_timer = (int16_t)(t < 0 ? -t : (TFA_MOIA.dagger_interval - (t % TFA_MOIA.dagger_interval)) % TFA_MOIA.dagger_interval);
}
