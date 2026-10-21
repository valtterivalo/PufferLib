/* The Dagon'hai Trio, built from the wiki Strategies write-up (revs 15378581-15378607, 21 Oct 16:21-16:40 UTC) before any recording.
   [wiki] the write-up, [cache] NPC definitions and gameval names, [guess] a stand-in until the first recording. Not modelled: the counter
   element (the shared spell head has no elemental spells, so any damaging magic hit interrupts a channel), the imp kill buff, the censers,
   the monks walking to their desks. */

#define DAGONHAI_NPC_ELDER 16643
#define DAGONHAI_NPC_MONK_A 16645
#define DAGONHAI_NPC_MONK_B 16647
#define DAGONHAI_NPC_CAULDRON 16649
#define DAGONHAI_NPC_IMP 16650
#define DAGONHAI_NPC_RIFT 16651

#define DAGONHAI_ANIM_RANGED 14593
#define DAGONHAI_ANIM_SURGE 14603
#define DAGONHAI_ANIM_BOOK_SEARCH 14630
#define DAGONHAI_ANIM_CAULDRON_ZAMORAK 14657
#define DAGONHAI_ANIM_CAULDRON_SARADOMIN 14659
#define DAGONHAI_ANIM_CAULDRON_BANDOS 14661
#define DAGONHAI_PROJ_RANGED 4071
#define DAGONHAI_PROJ_MAGIC 4078
#define DAGONHAI_GFX_SARADOMIN_TELEGRAPH 4063
#define DAGONHAI_GFX_HEAL 4066
#define DAGONHAI_GFX_LIGHTNING 4068
#define DAGONHAI_GFX_ZAMORAK_AOE 4149
#define DAGONHAI_GFX_BANDOS_AOE 4156
#define DAGONHAI_GFX_DUST_EXPLOSION 4161

/* Per caster, in the gameval suffix order (_01 Elder, _02 and _03 the monks by their idle anims 14581-14583 [cache]): attack, channel idle,
   channel end, channel interrupted, death. */
static const int TFA_DAGONHAI_CASTER_ANIMS[3][5] = {
    { DAGONHAI_ANIM_RANGED, 14607, 14609, 14611, 14652 },
    { 14599, 14612, 14614, 14616, 14653 },
    { 14601, 14617, 14619, 14621, 14654 },
};
/* Book monk throws, the _02 set: Zamorak, Bandos, Saradomin [cache]. */
static const int TFA_DAGONHAI_THROW_ANIMS[4] = { 0, 14633, 14632, 14634 };

typedef struct {
    int pool_duo, pool_trio;     /* [guess] the shared shield, refilled after every break */
    int breaks;                  /* [wiki] the fifth break explodes the cauldron */
    int finale_hp;               /* [guess] the stunned two "die in a few hits" [wiki] */
    int first_attack;            /* [guess] */
    int attack_interval;         /* [guess] */
    int ranged_max_hit;          /* [guess] grey projectile [wiki] */
    int magic_max_hit;           /* [guess] surge [wiki] */
    int splash_radius;           /* [wiki] he focuses one player and splashes those nearby, radius [guess] */
    int empowered_surges;        /* [wiki] 2 surges per attack once the ritual fills */
    int empowered_protected_pct; /* [wiki] heavy damage through prayer, amount [guess] */
    int monk_max_hit;            /* [wiki] all three attack when empowered, max [guess] */
    int ritual_full;             /* meter scale */
    int siphon_interval;         /* [wiki] monks siphon into the cauldron, each idle monk adds 1 per interval [guess] */
    int channel_first, channel_interval, channel_ticks, channel_gain; /* [wiki] channels feed the ritual, numbers [guess] */
    int tome_first, tome_interval;  /* [guess] */
    int tome_warning;            /* [wiki] "What have we here?" precedes the throw, ticks to landing [guess] */
    int tome_unblocked_gain;     /* [guess] */
    int gust_min, gust_max;      /* [wiki] air gusts mark the blocking tile, its distance from the cauldron [guess] */
    int bandos_max_hit;          /* [wiki] the blocker takes up to 55 */
    int shockwave_max_hit;       /* [wiki] an arena-wide shockwave spares the blocker, max [guess] */
    int zamorak_hit;             /* [wiki] the blocker takes about 25 */
    int fireball_size;           /* [wiki] a 7x7 fireball lands around the blocker with a burn */
    int fireball_delay, fireball_min, fireball_max; /* [guess] */
    int saradomin_heal;          /* [wiki] the blocker is healed, amount [guess] */
    int lightning_count, lightning_delay, lightning_min, lightning_max; /* [wiki] lightning across the arena, numbers [guess] */
    int burn_interval, burn_damage; /* [wiki] burn starting at 3, every 4 ticks [guess] */
    int rifts, imps_per_rift;    /* [wiki] 2 rifts each drop 3 imps */
    int imp_drop_delay;          /* [guess] */
    int imp_protected_pct;       /* [wiki] Protect from Melee reduces imp hits, amount [guess] */
    int arena_x0, arena_y0, arena_x1, arena_y1; /* the open floor of the scene's cache collision */
} TfaDagonhaiTuning;

static const TfaDagonhaiTuning TFA_DAGONHAI = {
    .pool_duo = 415,
    .pool_trio = 600,
    .breaks = 5,
    .finale_hp = 100,
    .first_attack = 6,
    .attack_interval = 5,
    .ranged_max_hit = 30,
    .magic_max_hit = 35,
    .splash_radius = 1,
    .empowered_surges = 2,
    .empowered_protected_pct = 50,
    .monk_max_hit = 25,
    .ritual_full = 100,
    .siphon_interval = 4,
    .channel_first = 15,
    .channel_interval = 25,
    .channel_ticks = 8,
    .channel_gain = 25,
    .tome_first = 20,
    .tome_interval = 30,
    .tome_warning = 5,
    .tome_unblocked_gain = 34,
    .gust_min = 3,
    .gust_max = 6,
    .bandos_max_hit = 55,
    .shockwave_max_hit = 25,
    .zamorak_hit = 25,
    .fireball_size = 7,
    .fireball_delay = 2,
    .fireball_min = 10,
    .fireball_max = 20,
    .saradomin_heal = 20,
    .lightning_count = 8,
    .lightning_delay = 3,
    .lightning_min = 15,
    .lightning_max = 25,
    .burn_interval = 4,
    .burn_damage = 3,
    .rifts = 2,
    .imps_per_rift = 3,
    .imp_drop_delay = 2,
    .imp_protected_pct = 50,
    .arena_x0 = 23,
    .arena_y0 = 24,
    .arena_x1 = 42,
    .arena_y1 = 39,
};

/* Spawn tiles [guess]: the cauldron in the middle of the floor, the Elder north of it, the monks west and east, rifts in opposite corners. */
static const int8_t TFA_DAGONHAI_CASTER_TILES[3][2] = { {31, 34}, {26, 30}, {37, 30} };
static const int8_t TFA_DAGONHAI_RIFT_TILES[2][2] = { {27, 35}, {37, 26} };
#define DAGONHAI_CAULDRON_X 31
#define DAGONHAI_CAULDRON_Y 30

/* The Elder and the monks hold slots 0-2, their shielded ids; unshielded is the next id [cache]. */
static const int TFA_DAGONHAI_IDS[3] = { DAGONHAI_NPC_ELDER, DAGONHAI_NPC_MONK_A, DAGONHAI_NPC_MONK_B };

static TfaNpc* tfa_dagonhai_cauldron(TfaState* s) {
    return &s->npcs[3];
}

static void tfa_dagonhai_set_shielded(TfaState* s, int shielded) {
    for (int i = 0; i < 3; i++) s->npcs[i].npc_id = TFA_DAGONHAI_IDS[i] + !shielded;
}

static void tfa_dagonhai_sync_shield(TfaState* s) {
    TfaDagonhaiState* ds = &s->boss.dagonhai;
    for (int i = 0; i < 3; i++) {
        s->npcs[i].shield = ds->phase == TFA_DAGONHAI_SHIELDED ? ds->pool : 0;
        s->npcs[i].max_shield = ds->pool_max;
    }
    TfaNpc* c = tfa_dagonhai_cauldron(s);
    c->shield = ds->ritual;
    c->max_shield = TFA_DAGONHAI.ritual_full;
    c->state = (int16_t)ds->breaks;
}

static void tfa_dagonhai_enter(TfaState* s) {
    for (int i = 0; i < 3; i++) {
        TfaNpc* n = tfa_npc_spawn(s, i ? TFA_NPC_DAGONHAI_MONK : TFA_NPC_DAGONHAI_ELDER, i ? MON_TFA_DAGONHAI_MONK : MON_TFA_DAGONHAI_ELDER,
            TFA_DAGONHAI_IDS[i], TFA_DAGONHAI_CASTER_TILES[i][0], TFA_DAGONHAI_CASTER_TILES[i][1]);
        n->hp = n->max_hp = TFA_DAGONHAI.finale_hp;
        assert(tfa_npc_slot(s, n) == i);
    }
    TfaNpc* c = tfa_npc_alloc(s, TFA_NPC_DAGONHAI_CAULDRON, DAGONHAI_NPC_CAULDRON, 3, DAGONHAI_CAULDRON_X, DAGONHAI_CAULDRON_Y, 1);
    assert(tfa_npc_slot(s, c) == 3);
    int pool = tfa_party_hp(s->team_size, TFA_DAGONHAI.pool_duo, TFA_DAGONHAI.pool_trio);
    s->boss.dagonhai = (TfaDagonhaiState){
        .pool = pool, .pool_max = pool, .focus = -1,
        .next_attack = TFA_DAGONHAI.first_attack, .caster_slot = -1,
        .next_channel = TFA_DAGONHAI.channel_first, .next_tome = TFA_DAGONHAI.tome_first, .gust_slot = -1, .alive = 3,
    };
    tfa_dagonhai_sync_shield(s);
}

static int tfa_dagonhai_random_player(TfaState* s) {
    int alive = tfa_alive_count(s);
    if (alive == 0) return -1;
    int k = tfa_rand(s, alive);
    for (int p = 0; p < s->team_size; p++)
        if (tfa_alive(s, p) && k-- == 0) return p;
    abort();
}

/* One projectile at player p, its damage checked on impact [wiki]. */
static void tfa_dagonhai_shot(TfaState* s, TfaNpc* n, int p, AttackStyle style, int max_hit, int protected_pct) {
    const Player* pl = &s->players[p].player;
    int d = encounter_rect_distance(n->x, n->y, n->size, pl->x, pl->y, 1);
    int delay = encounter_projectile_base_hit_delay(d, 0,
        style == ATTACK_STYLE_MAGIC ? ENCOUNTER_PROJECTILE_DELAY_MAGIC : ENCOUNTER_PROJECTILE_DELAY_RANGED);
    tfa_queue_npc_hit_land(s, p, tfa_npc_roll(s, n, p, style, max_hit), style, delay, protected_pct, n);
    if (style == ATTACK_STYLE_MAGIC)
        tfa_fx_projectile(s, n, p, DAGONHAI_PROJ_MAGIC, SPOTANIM_TFA_DAGONHAI_VFX_DAGONHAI_ATTACK_MAGIC_04_PROJANIM,
            SPOTANIM_TFA_DAGONHAI_VFX_DAGONHAI_ATTACK_MAGIC_04_PROJANIM_ANIM, 0, delay);
    else
        tfa_fx_projectile(s, n, p, DAGONHAI_PROJ_RANGED, SPOTANIM_TFA_DAGONHAI_VFX_PROJANIM_DAGONHAI_ATTACK_RANGED_01,
            SPOTANIM_TFA_DAGONHAI_VFX_PROJANIM_DAGONHAI_ATTACK_RANGED_01_ANIM, 0, delay);
}

/* The Elder's attack on his focus and everyone within the splash radius of it. */
static void tfa_dagonhai_elder_attack(TfaState* s, TfaNpc* e) {
    TfaDagonhaiState* ds = &s->boss.dagonhai;
    if (!tfa_alive(s, ds->focus)) ds->focus = tfa_dagonhai_random_player(s);
    if (ds->focus < 0) return;
    int surges = ds->empowered ? TFA_DAGONHAI.empowered_surges : 0;
    AttackStyle style = surges || tfa_rand(s, 2) ? ATTACK_STYLE_MAGIC : ATTACK_STYLE_RANGED;
    int max_hit = style == ATTACK_STYLE_MAGIC ? TFA_DAGONHAI.magic_max_hit : TFA_DAGONHAI.ranged_max_hit;
    int protected_pct = surges ? TFA_DAGONHAI.empowered_protected_pct : 0;
    e->act = style == ATTACK_STYLE_MAGIC ? TFA_ACT_MAGIC : TFA_ACT_RANGED;
    e->target = (int8_t)ds->focus;
    tfa_npc_anim(s, e, style == ATTACK_STYLE_MAGIC ? DAGONHAI_ANIM_SURGE : DAGONHAI_ANIM_RANGED);
    const Player* f = &s->players[ds->focus].player;
    for (int p = 0; p < s->team_size; p++) {
        if (!tfa_alive(s, p) || chebyshev_distance(s->players[p].player.x, s->players[p].player.y, f->x, f->y) > TFA_DAGONHAI.splash_radius) continue;
        for (int k = 0; k < (surges ? surges : 1); k++) tfa_dagonhai_shot(s, e, p, style, max_hit, protected_pct);
    }
}

static void tfa_dagonhai_channel_end(TfaState* s, int completed) {
    TfaDagonhaiState* ds = &s->boss.dagonhai;
    TfaNpc* c = &s->npcs[ds->caster_slot];
    c->state = 0;
    tfa_npc_anim(s, c, TFA_DAGONHAI_CASTER_ANIMS[ds->caster_slot][completed ? 2 : 3]);
    if (completed) ds->ritual += TFA_DAGONHAI.channel_gain;
    ds->caster_slot = -1;
}

static int tfa_dagonhai_floor(TfaState* s, int x, int y) {
    const TfaDagonhaiTuning* t = &TFA_DAGONHAI;
    return x >= t->arena_x0 && x <= t->arena_x1 && y >= t->arena_y0 && y <= t->arena_y1 &&
        !tfa_layout_blocked(tfa_room_layout(s->room), x, y);
}

static void tfa_dagonhai_random_floor(TfaState* s, int cx, int cy, int lo, int hi, int* x, int* y) {
    do {
        *x = cx + tfa_rand_range(s, -hi, hi);
        *y = cy + tfa_rand_range(s, -hi, hi);
    } while (!tfa_dagonhai_floor(s, *x, *y) || chebyshev_distance(*x, *y, cx, cy) < lo);
}

static void tfa_dagonhai_tome_start(TfaState* s) {
    TfaDagonhaiState* ds = &s->boss.dagonhai;
    int gx, gy;
    tfa_dagonhai_random_floor(s, DAGONHAI_CAULDRON_X + 1, DAGONHAI_CAULDRON_Y + 1, TFA_DAGONHAI.gust_min, TFA_DAGONHAI.gust_max, &gx, &gy);
    TfaNpc* g = tfa_npc_alloc(s, TFA_NPC_DAGONHAI_GUST, 0, 1, gx, gy, 1);
    ds->gust_slot = tfa_npc_slot(s, g);
    ds->tome = TFA_TOME_BANDOS + tfa_rand(s, 3);
    ds->tome_land = ds->clock + TFA_DAGONHAI.tome_warning;
    g->state = (int16_t)ds->tome;
    if (s->npcs[1].active) tfa_npc_anim(s, &s->npcs[1], DAGONHAI_ANIM_BOOK_SEARCH);
}

static void tfa_dagonhai_tome_land(TfaState* s) {
    TfaDagonhaiState* ds = &s->boss.dagonhai;
    const TfaDagonhaiTuning* t = &TFA_DAGONHAI;
    TfaNpc* g = &s->npcs[ds->gust_slot];
    int blocker = -1;
    for (int p = 0; p < s->team_size && blocker < 0; p++)
        if (tfa_alive(s, p) && s->players[p].player.x == g->x && s->players[p].player.y == g->y) blocker = p;
    int tome = ds->tome;
    if (s->npcs[1].active) tfa_npc_anim(s, &s->npcs[1], TFA_DAGONHAI_THROW_ANIMS[tome]);
    g->active = 0;
    ds->gust_slot = -1;
    ds->tome = TFA_TOME_NONE;
    TfaNpc* c = tfa_dagonhai_cauldron(s);
    if (blocker < 0) {
        ds->ritual += t->tome_unblocked_gain;
        return;
    }
    const Player* b = &s->players[blocker].player;
    if (tome == TFA_TOME_BANDOS) {
        tfa_npc_anim(s, c, DAGONHAI_ANIM_CAULDRON_BANDOS);
        tfa_damage_player_now(s, blocker, tfa_rand_range(s, 0, t->bandos_max_hit), ATTACK_STYLE_NONE);
        for (int p = 0; p < s->team_size; p++) {
            if (p == blocker || !tfa_alive(s, p)) continue;
            tfa_damage_player_now(s, p, tfa_rand_range(s, 0, t->shockwave_max_hit), ATTACK_STYLE_NONE);
            tfa_fx_ground(s, s->players[p].player.x, s->players[p].player.y, DAGONHAI_GFX_BANDOS_AOE, 0);
        }
    } else if (tome == TFA_TOME_ZAMORAK) {
        tfa_npc_anim(s, c, DAGONHAI_ANIM_CAULDRON_ZAMORAK);
        tfa_damage_player_now(s, blocker, t->zamorak_hit, ATTACK_STYLE_NONE);
        int half = t->fireball_size / 2;
        tfa_hazard_add(s, (TfaHazard){
            .kind = TFA_HAZ_STRIKE, .style = ATTACK_STYLE_NONE, .w = (uint8_t)t->fireball_size, .h = (uint8_t)t->fireball_size, .owner = -1,
            .effect = TFA_EFFECT_BURN, .x = (int16_t)(b->x - half), .y = (int16_t)(b->y - half),
            .start_tick = s->tick + t->fireball_delay, .end_tick = s->tick + t->fireball_delay,
            .min_damage = (int16_t)t->fireball_min, .max_damage = (int16_t)t->fireball_max, .protected_pct = 100,
        });
        tfa_fx_ground(s, b->x, b->y, DAGONHAI_GFX_ZAMORAK_AOE, t->fireball_delay * 30);
    } else {
        tfa_npc_anim(s, c, DAGONHAI_ANIM_CAULDRON_SARADOMIN);
        tfa_player_heal(&s->players[blocker], t->saradomin_heal);
        tfa_fx_ground(s, b->x, b->y, DAGONHAI_GFX_HEAL, 0);
        for (int k = 0; k < t->lightning_count; k++) {
            int x, y;
            tfa_dagonhai_random_floor(s, (t->arena_x0 + t->arena_x1) / 2, (t->arena_y0 + t->arena_y1) / 2, 0, (t->arena_x1 - t->arena_x0) / 2, &x, &y);
            tfa_hazard_add(s, (TfaHazard){
                .kind = TFA_HAZ_STRIKE, .style = ATTACK_STYLE_NONE, .w = 1, .h = 1, .owner = -1, .x = (int16_t)x, .y = (int16_t)y,
                .start_tick = s->tick + t->lightning_delay, .end_tick = s->tick + t->lightning_delay,
                .min_damage = (int16_t)t->lightning_min, .max_damage = (int16_t)t->lightning_max, .protected_pct = 100,
            });
            tfa_fx_ground(s, x, y, DAGONHAI_GFX_SARADOMIN_TELEGRAPH, 0);
            tfa_fx_ground(s, x, y, DAGONHAI_GFX_LIGHTNING, t->lightning_delay * 30);
        }
    }
}

/* A break: the stability drops a fifth, the ritual empties [guess], the trio channels protection while two rifts drop imps [wiki]. */
static void tfa_dagonhai_break(TfaState* s) {
    TfaDagonhaiState* ds = &s->boss.dagonhai;
    ds->breaks++;
    ds->ritual = 0;
    ds->empowered = 0;
    if (ds->caster_slot >= 0) tfa_dagonhai_channel_end(s, 0);
    if (ds->tome != TFA_TOME_NONE) {
        s->npcs[ds->gust_slot].active = 0;
        ds->gust_slot = -1;
        ds->tome = TFA_TOME_NONE;
    }
    tfa_dagonhai_set_shielded(s, 0);
    if (ds->breaks == TFA_DAGONHAI.breaks) {
        ds->phase = TFA_DAGONHAI_FINALE;
        TfaNpc* c = tfa_dagonhai_cauldron(s);
        tfa_fx_ground(s, c->x + 1, c->y + 1, DAGONHAI_GFX_DUST_EXPLOSION, 0);
        TfaNpc* dies = &s->npcs[1 + tfa_rand(s, 2)];
        tfa_npc_anim(s, dies, TFA_DAGONHAI_CASTER_ANIMS[tfa_npc_slot(s, dies)][4]);
        dies->active = 0;
        ds->alive--;
        for (int i = 0; i < 3; i++) {
            if (!s->npcs[i].active) continue;
            s->npcs[i].act = TFA_ACT_STUNNED;
            s->npcs[i].attackable = 1;
        }
        return;
    }
    ds->phase = TFA_DAGONHAI_IMPS;
    for (int i = 0; i < 3; i++) s->npcs[i].attackable = 0;
    for (int r = 0; r < TFA_DAGONHAI.rifts; r++) {
        TfaNpc* rift = tfa_npc_spawn_inert(s, TFA_NPC_DEMONIC_RIFT, DAGONHAI_NPC_RIFT, 1, TFA_DAGONHAI_RIFT_TILES[r][0], TFA_DAGONHAI_RIFT_TILES[r][1]);
        rift->counter = (int16_t)TFA_DAGONHAI.imps_per_rift;
        rift->state_ticks = (int16_t)TFA_DAGONHAI.imp_drop_delay;
    }
    ds->imps_left = TFA_DAGONHAI.rifts * TFA_DAGONHAI.imps_per_rift;
}

static int tfa_dagonhai_blocked(void* ctx, int x, int y, int size) {
    (void)size;
    return !tfa_dagonhai_floor((TfaState*)ctx, x, y);
}

static void tfa_dagonhai_imps_tick(TfaState* s) {
    for (int i = 0; i < TFA_MAX_NPCS; i++) {
        TfaNpc* r = &s->npcs[i];
        if (!r->active || r->kind != TFA_NPC_DEMONIC_RIFT || --r->state_ticks > 0) continue;
        TfaNpc* imp = tfa_npc_spawn(s, TFA_NPC_IMP, MON_TFA_DAGONHAI_IMP, DAGONHAI_NPC_IMP, r->x, r->y);
        imp->attack_timer = (int16_t)MONSTER_DATABASE[MON_TFA_DAGONHAI_IMP].attack_speed;
        r->state_ticks = 1;
        if (--r->counter == 0) r->active = 0;
    }
    for (int i = 0; i < TFA_NUM_TARGET_SLOTS; i++) {
        TfaNpc* imp = &s->npcs[i];
        if (!imp->active || imp->kind != TFA_NPC_IMP) continue;
        imp->act = TFA_ACT_IDLE;
        int best = 1 << 30;
        imp->target = -1;
        for (int p = 0; p < s->team_size; p++) {
            if (!tfa_alive(s, p)) continue;
            int d = chebyshev_distance(imp->x, imp->y, s->players[p].player.x, s->players[p].player.y);
            if (d < best) { best = d; imp->target = (int8_t)p; }
        }
        if (imp->attack_timer > 0) imp->attack_timer--;
        if (imp->target < 0) continue;
        const Player* pl = &s->players[imp->target].player;
        if (encounter_entity_footprint_cardinal_adjacent(imp->x, imp->y, 1, pl->x, pl->y, 1)) {
            if (imp->attack_timer > 0) continue;
            imp->act = TFA_ACT_MELEE;
            imp->attack_timer = (int16_t)MONSTER_DATABASE[MON_TFA_DAGONHAI_IMP].attack_speed;
            tfa_queue_npc_hit(s, imp->target, tfa_npc_roll(s, imp, imp->target, ATTACK_STYLE_MELEE, MONSTER_DATABASE[MON_TFA_DAGONHAI_IMP].max_hit),
                ATTACK_STYLE_MELEE, 0, TFA_DAGONHAI.imp_protected_pct, imp);
            continue;
        }
        int x = imp->x, y = imp->y;
        encounter_npc_step_toward_policy(&x, &y, pl->x, pl->y, 1, 1, ENCOUNTER_NPC_STEP_OSRS_AGGRO_STOP_AT_MELEE,
            tfa_dagonhai_blocked, s, NULL, &s->rng_state);
        imp->x = (int16_t)x;
        imp->y = (int16_t)y;
    }
}

static int tfa_dagonhai_npc_damage(TfaState* s, TfaNpc* n, int damage, int attacker, AttackStyle style) {
    (void)attacker;
    TfaDagonhaiState* ds = &s->boss.dagonhai;
    if (n->kind == TFA_NPC_IMP || ds->phase == TFA_DAGONHAI_FINALE) {
        int applied = damage < n->hp ? damage : n->hp;
        n->hp -= applied;
        return applied;
    }
    if (ds->phase != TFA_DAGONHAI_SHIELDED) return 0;
    if (style == ATTACK_STYLE_MAGIC && damage > 0 && tfa_npc_slot(s, n) == ds->caster_slot) tfa_dagonhai_channel_end(s, 0);
    int applied = damage < ds->pool ? damage : ds->pool;
    ds->pool -= applied;
    if (ds->pool == 0) tfa_dagonhai_break(s);
    tfa_dagonhai_sync_shield(s);
    return applied;
}

static void tfa_dagonhai_npc_died(TfaState* s, TfaNpc* n, int attacker) {
    (void)attacker;
    TfaDagonhaiState* ds = &s->boss.dagonhai;
    n->active = 0;
    n->attackable = 0;
    if (n->kind == TFA_NPC_IMP) {
        if (--ds->imps_left > 0) return;
        ds->phase = TFA_DAGONHAI_SHIELDED;
        ds->pool = ds->pool_max;
        tfa_dagonhai_set_shielded(s, 1);
        for (int i = 0; i < 3; i++) s->npcs[i].attackable = 1;
        tfa_dagonhai_sync_shield(s);
        return;
    }
    tfa_npc_anim(s, n, TFA_DAGONHAI_CASTER_ANIMS[tfa_npc_slot(s, n)][4]);
    ds->alive--;
}

static void tfa_dagonhai_tick(TfaState* s, TfaContext* ctx) {
    TfaDagonhaiState* ds = &s->boss.dagonhai;
    const TfaDagonhaiTuning* t = &TFA_DAGONHAI;
    if (ds->alive == 0) {
        tfa_room_cleared(s, ctx);
        return;
    }
    ds->clock++;
    for (int p = 0; p < s->team_size; p++)
        if (tfa_alive(s, p) && s->players[p].burn_ticks > 0 && s->players[p].burn_ticks % t->burn_interval == 0)
            tfa_damage_player_now(s, p, t->burn_damage, ATTACK_STYLE_NONE);
    for (int i = 0; i < 3; i++)
        if (s->npcs[i].active && ds->phase != TFA_DAGONHAI_FINALE) s->npcs[i].act = TFA_ACT_IDLE;
    if (ds->phase == TFA_DAGONHAI_FINALE) return;
    if (ds->phase == TFA_DAGONHAI_IMPS) {
        tfa_dagonhai_imps_tick(s);
        return;
    }

    if (ds->caster_slot >= 0 && ds->clock == ds->channel_end) tfa_dagonhai_channel_end(s, 1);
    if (ds->caster_slot < 0 && ds->clock >= ds->next_channel) {
        ds->caster_slot = tfa_rand(s, 3);
        ds->channel_end = ds->clock + t->channel_ticks;
        ds->next_channel = ds->clock + t->channel_interval;
        TfaNpc* c = &s->npcs[ds->caster_slot];
        c->state = (int16_t)(1 + tfa_rand(s, 4));
        tfa_npc_anim(s, c, TFA_DAGONHAI_CASTER_ANIMS[ds->caster_slot][1]);
    }
    if (ds->clock % t->siphon_interval == 0)
        for (int i = 1; i < 3; i++) ds->ritual += i != ds->caster_slot;
    if (ds->tome == TFA_TOME_NONE && ds->clock >= ds->next_tome) {
        tfa_dagonhai_tome_start(s);
        ds->next_tome = ds->clock + t->tome_interval;
    } else if (ds->tome != TFA_TOME_NONE && ds->clock == ds->tome_land) {
        tfa_dagonhai_tome_land(s);
    }
    if (ds->ritual >= t->ritual_full) {
        ds->ritual = t->ritual_full;
        ds->empowered = 1;
    }

    TfaNpc* e = &s->npcs[0];
    if (ds->clock >= ds->next_attack && ds->caster_slot != 0) {
        tfa_dagonhai_elder_attack(s, e);
        ds->next_attack = ds->clock + t->attack_interval;
        for (int i = 1; i < 3 && ds->empowered; i++) {
            TfaNpc* m = &s->npcs[i];
            int p = tfa_dagonhai_random_player(s);
            if (i == ds->caster_slot || p < 0) continue;
            m->act = TFA_ACT_MAGIC;
            m->target = (int8_t)p;
            tfa_npc_anim(s, m, TFA_DAGONHAI_CASTER_ANIMS[i][0]);
            tfa_dagonhai_shot(s, m, p, ATTACK_STYLE_MAGIC, t->monk_max_hit, 0);
        }
    }
    e->attack_timer = (int16_t)(ds->next_attack - ds->clock);
    tfa_dagonhai_sync_shield(s);
}
