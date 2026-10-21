/* Ezekiel. A fixed script measured from 19 recorded fights (fixtures/tfa_ezekiel_events.tsv):
   special, two attacks, a slot the sword volley decides (weakened, heal or nothing), two attacks, special.
   Finale at one third HP. */

#define EZEKIEL_NPC 16652
#define EZEKIEL_NPC_SWORD 16653

#define EZEKIEL_ANIM_ROLL 14684
#define EZEKIEL_ANIM_MELEE 14685
#define EZEKIEL_ANIM_MAGIC 14686
#define EZEKIEL_ANIM_SPECIAL 14687
#define EZEKIEL_ANIM_HEAL 14688
#define EZEKIEL_ANIM_WEAKEN 14691
#define EZEKIEL_ANIM_RECOVER 14693
#define EZEKIEL_ANIM_FINALE 14694
#define EZEKIEL_ANIM_DEATH 14697
#define EZEKIEL_PROJ_MAGIC 4167
#define EZEKIEL_GFX_STAR 4172
#define EZEKIEL_GFX_SWORD_LINE 4181
#define EZEKIEL_GFX_FINALE_IMPACT 4197

#define EZEKIEL_ARENA_LO 24
#define EZEKIEL_ARENA_HI 36
#define EZEKIEL_ARENA_SPAN 15
#define EZEKIEL_STAR_TABLE_SIZE 41
#define EZEKIEL_STAR_ORIGIN 4
#define EZEKIEL_STAR_ROWS 11
#define EZEKIEL_STAR_COLS 11
#define EZEKIEL_FINALE_CELL 30

typedef struct {
    int first_attack;            /* provisional: the first attack waits for engagement (19..372 ticks after spawn) */
    int attack_interval;         /* 14685 -> next attack 5 in 464/626 */
    int special_to_attack;       /* 14687 -> 14685 7 in 139/146 */
    int special_interval;        /* last attack -> 14687 5 in 160/162 */
    int attacks_per_cycle;       /* special, then 4 straight melees when no slot fires: 14/14 */
    int attacks_after_slot;      /* weakened or heal, then 2 more attacks: 85/85 and 25/25 */
    int magic_late;              /* 14686 comes 1 tick after the slot it replaces: 6 after a melee, 8 after a special */
    int magic_pct;               /* 20 magics in 286 attack intervals in which he had moved, none in 340 without */
    int melee_max_hit;           /* unprotected 37, 38 (n=3), provisional */
    int melee_protected_pct;     /* Protect from Melee leaves 0..6 (n=207), so 6 of 40 */
    int magic_max_hit;           /* unprotected 0..21 (n=23) */
    int magic_protected_pct;     /* provisional: no recorded hit with Protect from Magic active */
    int magic_hit;               /* 14686 -> P0 hit at +2: 23/23 */
    int magic_launch;            /* provisional: no magic projectile row was recorded (only 10 unrelated 2727 rows) */
    int cleave_ring;             /* dust 4394 on the 5x5 ring minus 3 tiles opposite the tank: 13 tiles in 100% of 700 casts */
    int star_land;               /* star damage lands at special +3 on the tile occupied at +2: 34/34 */
    int star_min_damage, star_max_damage; /* 20..33 typeless (n=34) */
    int three_sword_hp_permille; /* first 3-sword volley once HP is at or under this, fitted between 4@829 and 3@818 */
    int sword_charge_cap;        /* a 50 hit plus a 30 hit fills the bar: 80 */
    int sword_hit_cap;           /* sword hits are 50 in 20 of 24, 30, 46, 48 otherwise */
    int sword_jump;              /* swords swap walls at special +14 (4-sword) */
    int sword_despawn;           /* 2 ticks after the jump: 404/404 */
    int sword_stagger;           /* 3-sword lines jump 1 tick apart: +14, +15, +16 */
    int sword_base_min, sword_base_max; /* damage per crossing sword on top of its charge: 12..15 (3-sword singles) */
    int sword_boss_delay;        /* boss hitsplat one tick after the weakened anim: 126 at W+1 */
    int sword4_min, sword4_max;  /* players in a 4-sword band: 50..63 (n=26) */
    int sword3_min, sword3_max;  /* players in a 3-sword band: 27..33 (n=33) */
    int heal_anim;               /* 14688 at special +16: 33/33 */
    int heal_land;               /* heal hitsplat 2 ticks after the anim: 33/33 */
    int weaken_recover;          /* 14691 -> 14693 at +8: 106/106 */
    int weaken_to_attack;        /* 14691 -> next attack 12: 100/102 */
    int heal_to_attack;          /* 14688 -> next attack 5: 30/33 */
    int hp_base;                 /* duo 3000 (wiki), trio 4350 (damage sum of 14 kills), a solo is a duo */
    int hp_per_player;
    int heal_pct;                /* heal is a tenth of max HP: 300 duo, 435 trio, capped at the missing HP */
    int finale_hp_permille;      /* 14684 one tick after HP falls to this, fitted on 13 fights */
    int finale_roll_to_cast;     /* 14684 -> 14694 +3: 13/13 */
    int finale_first_wave;       /* first wave 9 ticks after 14694, then every 11: 133 waves */
    int finale_wave_interval;
    int finale_land_min, finale_land_span; /* impacts land 4, 5 or 6 ticks after creation (57, 82, 68) */
    int finale_min_damage, finale_max_damage; /* 5..22 typeless (n=131) */
    int death_to_clear;          /* 14697 -> challenge complete 10 ticks later: 14/14 */
} TfaEzekielTuning;

static const TfaEzekielTuning TFA_EZEKIEL = {
    .first_attack = 2,
    .attack_interval = 5,
    .special_to_attack = 7,
    .special_interval = 5,
    .attacks_per_cycle = 4,
    .attacks_after_slot = 2,
    .magic_late = 1,
    .magic_pct = 7,
    .melee_max_hit = 40,
    .melee_protected_pct = 15,
    .magic_max_hit = 21,
    .magic_protected_pct = 15,
    .magic_hit = 2,
    .magic_launch = 1,
    .cleave_ring = 16,
    .star_land = 3,
    .star_min_damage = 20,
    .star_max_damage = 33,
    .three_sword_hp_permille = 825,
    .sword_charge_cap = 80,
    .sword_hit_cap = 50,
    .sword_jump = 14,
    .sword_despawn = 2,
    .sword_stagger = 1,
    .sword_base_min = 12,
    .sword_base_max = 15,
    .sword_boss_delay = 1,
    .sword4_min = 50,
    .sword4_max = 63,
    .sword3_min = 27,
    .sword3_max = 33,
    .heal_anim = 16,
    .heal_land = 2,
    .weaken_recover = 8,
    .weaken_to_attack = 12,
    .heal_to_attack = 5,
    .hp_base = 3000,
    .hp_per_player = 1350,
    .heal_pct = 10,
    .finale_hp_permille = 340,
    .finale_roll_to_cast = 3,
    .finale_first_wave = 9,
    .finale_wave_interval = 11,
    .finale_land_min = 4,
    .finale_land_span = 3,
    .finale_min_damage = 5,
    .finale_max_damage = 22,
    .death_to_clear = 10,
};

/* Star tiles relative to Ezekiel's SW tile as row bitmasks (row = y + 4, bit = x + 4), one row per recorded distinct
   pattern with its count in 162 specials: a ring around him plus arms of random length on each side. */
static const struct { int weight; uint16_t rows[EZEKIEL_STAR_ROWS]; } TFA_EZEKIEL_STARS[EZEKIEL_STAR_TABLE_SIZE] = {
    { 11, { 0x000, 0x020, 0x070, 0x0f8, 0x08c, 0x08f, 0x08c, 0x0f8, 0x070, 0x020, 0x000 }, },
    { 9, { 0x000, 0x020, 0x070, 0x0f8, 0x188, 0x788, 0x188, 0x0f8, 0x070, 0x020, 0x000 }, },
    { 9, { 0x000, 0x000, 0x000, 0x0f8, 0x18c, 0x38e, 0x18c, 0x0f8, 0x070, 0x020, 0x020 }, },
    { 7, { 0x020, 0x020, 0x070, 0x0f8, 0x18c, 0x18f, 0x18c, 0x000, 0x000, 0x000, 0x000 }, },
    { 7, { 0x000, 0x000, 0x070, 0x0f8, 0x18c, 0x78c, 0x18c, 0x0f8, 0x070, 0x020, 0x020 }, },
    { 6, { 0x020, 0x020, 0x070, 0x0f8, 0x18c, 0x38e, 0x18c, 0x000, 0x000, 0x000, 0x000 }, },
    { 6, { 0x020, 0x020, 0x070, 0x0f8, 0x18c, 0x78c, 0x18c, 0x000, 0x000, 0x000, 0x000 }, },
    { 6, { 0x000, 0x000, 0x070, 0x0f8, 0x18c, 0x38e, 0x18c, 0x0f8, 0x070, 0x020, 0x020 }, },
    { 6, { 0x020, 0x020, 0x070, 0x0f8, 0x08c, 0x08f, 0x08c, 0x0f8, 0x070, 0x000, 0x000 }, },
    { 5, { 0x020, 0x020, 0x070, 0x078, 0x00c, 0x00f, 0x00c, 0x078, 0x000, 0x000, 0x000 }, },
    { 5, { 0x020, 0x020, 0x070, 0x0f8, 0x08c, 0x08f, 0x08c, 0x0f8, 0x000, 0x000, 0x000 }, },
    { 5, { 0x020, 0x020, 0x070, 0x0f8, 0x18c, 0x38e, 0x18c, 0x0f8, 0x000, 0x000, 0x000 }, },
    { 5, { 0x000, 0x020, 0x070, 0x0f8, 0x18c, 0x18f, 0x18c, 0x0f8, 0x070, 0x020, 0x000 }, },
    { 5, { 0x000, 0x020, 0x070, 0x0f8, 0x18c, 0x78c, 0x18c, 0x0f8, 0x070, 0x020, 0x000 }, },
    { 5, { 0x020, 0x020, 0x070, 0x0f8, 0x18c, 0x38e, 0x18c, 0x0f8, 0x070, 0x000, 0x000 }, },
    { 5, { 0x000, 0x020, 0x070, 0x078, 0x00c, 0x00f, 0x00c, 0x078, 0x070, 0x020, 0x000 }, },
    { 4, { 0x020, 0x020, 0x070, 0x0f8, 0x18c, 0x18f, 0x18c, 0x0f8, 0x000, 0x000, 0x000 }, },
    { 4, { 0x020, 0x020, 0x070, 0x0f8, 0x18c, 0x18f, 0x18c, 0x0f8, 0x070, 0x000, 0x000 }, },
    { 4, { 0x000, 0x020, 0x070, 0x0f0, 0x180, 0x780, 0x180, 0x0f0, 0x070, 0x020, 0x000 }, },
    { 4, { 0x000, 0x000, 0x070, 0x078, 0x00c, 0x00f, 0x00c, 0x078, 0x070, 0x020, 0x020 }, },
    { 3, { 0x000, 0x000, 0x070, 0x0f0, 0x180, 0x780, 0x180, 0x0f0, 0x070, 0x020, 0x020 }, },
    { 3, { 0x000, 0x000, 0x000, 0x0f8, 0x18c, 0x18f, 0x18c, 0x0f8, 0x070, 0x020, 0x020 }, },
    { 3, { 0x020, 0x020, 0x070, 0x0f8, 0x18c, 0x78c, 0x18c, 0x0f8, 0x000, 0x000, 0x000 }, },
    { 3, { 0x000, 0x020, 0x070, 0x0f8, 0x18c, 0x38e, 0x18c, 0x0f8, 0x070, 0x020, 0x000 }, },
    { 3, { 0x000, 0x000, 0x000, 0x0f8, 0x18c, 0x78c, 0x18c, 0x0f8, 0x070, 0x020, 0x020 }, },
    { 3, { 0x000, 0x000, 0x070, 0x0f8, 0x18c, 0x18f, 0x18c, 0x0f8, 0x070, 0x020, 0x020 }, },
    { 3, { 0x020, 0x020, 0x070, 0x0f8, 0x188, 0x788, 0x188, 0x0f8, 0x070, 0x000, 0x000 }, },
    { 3, { 0x000, 0x000, 0x000, 0x0f8, 0x08c, 0x08f, 0x08c, 0x0f8, 0x070, 0x020, 0x020 }, },
    { 2, { 0x020, 0x020, 0x070, 0x0f8, 0x18c, 0x78c, 0x18c, 0x0f8, 0x070, 0x000, 0x000 }, },
    { 2, { 0x000, 0x000, 0x070, 0x0f8, 0x188, 0x788, 0x188, 0x0f8, 0x070, 0x020, 0x020 }, },
    { 2, { 0x020, 0x020, 0x070, 0x0f8, 0x08c, 0x08f, 0x08c, 0x000, 0x000, 0x000, 0x000 }, },
    { 2, { 0x020, 0x020, 0x070, 0x0f8, 0x188, 0x788, 0x188, 0x0f8, 0x000, 0x000, 0x000 }, },
    { 2, { 0x000, 0x000, 0x000, 0x0f0, 0x180, 0x780, 0x180, 0x0f0, 0x070, 0x020, 0x020 }, },
    { 2, { 0x020, 0x020, 0x070, 0x078, 0x00c, 0x00f, 0x00c, 0x078, 0x070, 0x000, 0x000 }, },
    { 2, { 0x020, 0x020, 0x070, 0x0f0, 0x180, 0x780, 0x180, 0x0f0, 0x070, 0x000, 0x000 }, },
    { 1, { 0x000, 0x000, 0x000, 0x000, 0x08c, 0x08f, 0x08c, 0x0f8, 0x070, 0x020, 0x020 }, },
    { 1, { 0x020, 0x020, 0x070, 0x0f8, 0x188, 0x788, 0x188, 0x000, 0x000, 0x000, 0x000 }, },
    { 1, { 0x000, 0x000, 0x000, 0x000, 0x18c, 0x18f, 0x18c, 0x0f8, 0x070, 0x020, 0x020 }, },
    { 1, { 0x000, 0x000, 0x000, 0x0f8, 0x188, 0x788, 0x188, 0x0f8, 0x070, 0x020, 0x020 }, },
    { 1, { 0x000, 0x000, 0x000, 0x000, 0x18c, 0x78c, 0x18c, 0x0f8, 0x070, 0x020, 0x020 }, },
    { 1, { 0x000, 0x000, 0x000, 0x078, 0x00c, 0x00f, 0x00c, 0x078, 0x070, 0x020, 0x020 }, },
};

/* Four-sword volleys: two lines, each swept by a sword from either wall. Counts in 105 recorded volleys. */
static const struct { int weight; int8_t row[2]; int8_t coord[2]; } TFA_EZEKIEL_FOUR[] = {
    { 21, { 0, 1 }, { 27, 30 } },
    { 19, { 0, 1 }, { 33, 33 } },
    { 15, { 1, 1 }, { 28, 32 } },
    { 12, { 0, 1 }, { 30, 33 } },
    { 11, { 0, 1 }, { 33, 30 } },
    { 8, { 0, 1 }, { 30, 27 } },
    { 8, { 0, 0 }, { 28, 32 } },
    { 5, { 0, 1 }, { 33, 27 } },
    { 3, { 0, 1 }, { 27, 33 } },
    { 3, { 1, 1 }, { 28, 33 } },
};

/* Finale impacts per tile of the central 3x3, indexed [y - 30][x - 30]: 20 per wave, identical in every recorded wave. */
static const int8_t TFA_EZEKIEL_WAVE_IMPACTS[3][3] = {
    { 3, 2, 1 },
    { 3, 3, 2 },
    { 3, 2, 1 },
};

static const int8_t TFA_EZEKIEL_RING[16][2] = {
    { -2, 2 }, { -1, 2 }, { 0, 2 }, { 1, 2 }, { 2, 2 }, { 2, 1 }, { 2, 0 }, { 2, -1 },
    { 2, -2 }, { 1, -2 }, { 0, -2 }, { -1, -2 }, { -2, -2 }, { -2, -1 }, { -2, 0 }, { -2, 1 },
};

static TfaNpc* tfa_ezekiel_boss(TfaState* s) {
    TfaNpc* n = tfa_npc_first(s, TFA_NPC_EZEKIEL);
    assert(n);
    return n;
}

static int tfa_ezekiel_hp(const TfaState* s) {
    int players = s->team_size < 2 ? 2 : s->team_size;
    return TFA_EZEKIEL.hp_base + TFA_EZEKIEL.hp_per_player * (players - 2);
}

static void tfa_ezekiel_enter(TfaState* s) {
    const TfaRoomLayout* l = tfa_room_layout(TFA_ROOM_EZEKIEL);
    TfaNpc* x = tfa_npc_spawn(s, TFA_NPC_EZEKIEL, MON_TFA_EZEKIEL, EZEKIEL_NPC, l->boss_x, l->boss_y);
    x->max_hp = x->hp = tfa_ezekiel_hp(s);
    s->boss.ezekiel = (TfaEzekielState){
        .next_attack = TFA_EZEKIEL.first_attack,
        .attacks_left = TFA_EZEKIEL.attacks_after_slot,
        .magic_clock = -1,
        .weak_until = -1,
        .sword_damage_tick = -1,
        .heal_anim_clock = -1,
        .heal_land_clock = -1,
        .finale_arm_clock = -1,
        .finale_roll_clock = -1,
        .finale_start_clock = -1,
        .next_wave_clock = -1,
    };
}

static int tfa_ezekiel_tank(const TfaState* s, const TfaNpc* x) {
    int best = -1, best_d = 1 << 30;
    for (int p = 0; p < s->team_size; p++) {
        if (!tfa_alive(s, p)) continue;
        int d = tfa_player_npc_distance(&s->players[p], x);
        if (d < best_d) { best_d = d; best = p; }
    }
    return best;
}

static int tfa_ezekiel_footprint_free(int x, int y) {
    const TfaRoomLayout* l = tfa_room_layout(TFA_ROOM_EZEKIEL);
    for (int dx = 0; dx < 3; dx++)
        for (int dy = 0; dy < 3; dy++)
            if (tfa_layout_blocked(l, x + dx, y + dy)) return 0;
    return 1;
}

/* One tile a tick toward the tank until adjacent; diagonal first, then either axis. */
static void tfa_ezekiel_walk(TfaState* s, TfaNpc* x, int tank) {
    const TfaPlayer* tp = &s->players[tank];
    if (tfa_player_npc_distance(tp, x) <= 1) return;
    int sx = (tp->player.x > x->x + 1) - (tp->player.x < x->x + 1);
    int sy = (tp->player.y > x->y + 1) - (tp->player.y < x->y + 1);
    const int steps[3][2] = { { sx, sy }, { sx, 0 }, { 0, sy } };
    for (int k = 0; k < 3; k++) {
        if (!steps[k][0] && !steps[k][1]) continue;
        if (!tfa_ezekiel_footprint_free(x->x + steps[k][0], x->y + steps[k][1])) continue;
        x->x = (int16_t)(x->x + steps[k][0]);
        x->y = (int16_t)(x->y + steps[k][1]);
        s->boss.ezekiel.moved = 1;
        return;
    }
}

/* The dust ring leaves 3 tiles open, centred on the ring tile opposite the tank: 125 of 125 exact
   for a tank on the ring, with the recording player either opposite the gap or in it. */
static int tfa_ezekiel_gap_centre(const TfaNpc* x, const TfaPlayer* tank) {
    int ux = -(tank->player.x - (x->x + 1)), uy = -(tank->player.y - (x->y + 1));
    int m = abs(ux) > abs(uy) ? abs(ux) : abs(uy);
    if (m == 0) { ux = 0; uy = -1; m = 1; }
    int rx = (int)lround(ux * 2.0 / m), ry = (int)lround(uy * 2.0 / m);
    for (int i = 0; i < 16; i++)
        if (TFA_EZEKIEL_RING[i][0] == rx && TFA_EZEKIEL_RING[i][1] == ry) return i;
    abort();
}

static void tfa_ezekiel_melee(TfaState* s, TfaNpc* x, int tank) {
    x->act = TFA_ACT_MELEE;
    x->target = (int8_t)tank;
    tfa_npc_anim(s, x, EZEKIEL_ANIM_MELEE);
    int gap = tfa_ezekiel_gap_centre(x, &s->players[tank]);
    for (int i = 0; i < TFA_EZEKIEL.cleave_ring; i++) {
        int off = (i - gap + 16) % 16;
        if (off <= 1 || off == 15) continue;
        int tx = x->x + 1 + TFA_EZEKIEL_RING[i][0], ty = x->y + 1 + TFA_EZEKIEL_RING[i][1];
        for (int p = 0; p < s->team_size; p++) {
            if (!tfa_alive(s, p) || s->players[p].player.x != tx || s->players[p].player.y != ty) continue;
            tfa_queue_npc_hit(s, p, tfa_npc_roll(s, x, p, ATTACK_STYLE_MELEE, TFA_EZEKIEL.melee_max_hit),
                ATTACK_STYLE_MELEE, 0, TFA_EZEKIEL.melee_protected_pct, x);
        }
    }
}

static void tfa_ezekiel_magic(TfaState* s, TfaNpc* x, int tank) {
    x->act = TFA_ACT_MAGIC;
    x->target = (int8_t)tank;
    tfa_npc_anim(s, x, EZEKIEL_ANIM_MAGIC);
    tfa_queue_npc_hit_land(s, tank, tfa_npc_roll(s, x, tank, ATTACK_STYLE_MAGIC, TFA_EZEKIEL.magic_max_hit),
        ATTACK_STYLE_MAGIC, TFA_EZEKIEL.magic_hit, TFA_EZEKIEL.magic_protected_pct, x);
    tfa_fx_projectile(s, x, tank, EZEKIEL_PROJ_MAGIC, SPOTANIM_TFA_EZEKIEL_EZEKIEL_MAGIC_ATTACK_VFX01,
        SPOTANIM_TFA_EZEKIEL_EZEKIEL_MAGIC_ATTACK_VFX01_ANIM, TFA_EZEKIEL.magic_launch, TFA_EZEKIEL.magic_hit - TFA_EZEKIEL.magic_launch);
}

static void tfa_ezekiel_strike(TfaState* s, int x, int y, int w, int h, int land, int min_damage, int max_damage, int gfx) {
    tfa_hazard_add(s, (TfaHazard){
        .kind = TFA_HAZ_STRIKE,
        .style = ATTACK_STYLE_NONE,
        .w = (uint8_t)w, .h = (uint8_t)h,
        .owner = -1,
        .x = (int16_t)x, .y = (int16_t)y,
        .start_tick = land,
        .end_tick = land,
        .min_damage = (int16_t)min_damage,
        .max_damage = (int16_t)max_damage,
        .protected_pct = 100,
        .gfx_id = gfx,
    });
}

static void tfa_ezekiel_stars(TfaState* s, const TfaNpc* x) {
    int total = 0;
    for (int i = 0; i < EZEKIEL_STAR_TABLE_SIZE; i++) total += TFA_EZEKIEL_STARS[i].weight;
    int roll = tfa_rand(s, total), pick = 0;
    while (roll >= TFA_EZEKIEL_STARS[pick].weight) roll -= TFA_EZEKIEL_STARS[pick++].weight;
    for (int row = 0; row < EZEKIEL_STAR_ROWS; row++) {
        uint16_t bits = TFA_EZEKIEL_STARS[pick].rows[row];
        for (int col = 0; col < EZEKIEL_STAR_COLS; col++) {
            if (!(bits >> col & 1)) continue;
            int run = 1;
            while (col + run < EZEKIEL_STAR_COLS && (bits >> (col + run) & 1)) run++;
            tfa_ezekiel_strike(s, x->x + col - EZEKIEL_STAR_ORIGIN, x->y + row - EZEKIEL_STAR_ORIGIN, run, 1,
                s->tick + TFA_EZEKIEL.star_land, TFA_EZEKIEL.star_min_damage, TFA_EZEKIEL.star_max_damage, EZEKIEL_GFX_STAR);
            col += run - 1;
        }
    }
}

/* A sword stands on its wall until it jumps to the opposite one; state is 1 for a row line (east-west), 0 for a column. */
static void tfa_ezekiel_sword(TfaState* s, int row, int coord, int from_far_wall, int jump_in) {
    int wall = from_far_wall ? EZEKIEL_ARENA_HI : EZEKIEL_ARENA_LO;
    TfaNpc* w = tfa_npc_spawn(s, TFA_NPC_EZEKIEL_SWORD, MON_TFA_EZEKIEL_SWORD, EZEKIEL_NPC_SWORD,
        row ? wall : coord, row ? coord : wall);
    w->hp = 0;
    w->state = (int16_t)row;
    w->state_ticks = (int16_t)jump_in;
    w->attack_timer = (int16_t)(jump_in + TFA_EZEKIEL.sword_despawn);
}

/* The swept band is 3 wide from the sword's SW tile and hurts on the jump tick: the swords on the walls are its telegraph. */
static void tfa_ezekiel_line(TfaState* s, int row, int coord, int swords, int first_wall, int jump_in, int four) {
    for (int k = 0; k < swords; k++) tfa_ezekiel_sword(s, row, coord, first_wall ^ k, jump_in);
    tfa_ezekiel_strike(s, row ? EZEKIEL_ARENA_LO : coord, row ? coord : EZEKIEL_ARENA_LO,
        row ? EZEKIEL_ARENA_SPAN : 3, row ? 3 : EZEKIEL_ARENA_SPAN, s->tick + jump_in,
        four ? TFA_EZEKIEL.sword4_min : TFA_EZEKIEL.sword3_min,
        four ? TFA_EZEKIEL.sword4_max : TFA_EZEKIEL.sword3_max, EZEKIEL_GFX_SWORD_LINE);
}

static void tfa_ezekiel_volley(TfaState* s, const TfaNpc* x) {
    TfaEzekielState* es = &s->boss.ezekiel;
    int hp_low = x->hp * 1000 <= x->max_hp * TFA_EZEKIEL.three_sword_hp_permille;
    if (!es->phase2 && hp_low) {
        es->phase2 = 1;
        es->three_next = 1;
    }
    int three = es->phase2 && es->three_next;
    if (es->phase2) es->three_next = !es->three_next;
    es->volley_swords = three ? 3 : 4;
    es->volley_start = es->clock;
    es->volleys++;
    if (three) {
        int row = tfa_rand(s, 57) < 31;
        static const int lines[3] = { 27, 30, 33 };
        for (int k = 0; k < 3; k++) {
            int jump = TFA_EZEKIEL.sword_jump + (row ? 2 - k : k) * TFA_EZEKIEL.sword_stagger;
            tfa_ezekiel_line(s, row, lines[k], 1, !row, jump, 0);
        }
        return;
    }
    int total = 0;
    for (size_t i = 0; i < sizeof(TFA_EZEKIEL_FOUR) / sizeof(*TFA_EZEKIEL_FOUR); i++) total += TFA_EZEKIEL_FOUR[i].weight;
    int roll = tfa_rand(s, total), pick = 0;
    while (roll >= TFA_EZEKIEL_FOUR[pick].weight) roll -= TFA_EZEKIEL_FOUR[pick++].weight;
    for (int line = 0; line < 2; line++)
        tfa_ezekiel_line(s, TFA_EZEKIEL_FOUR[pick].row[line], TFA_EZEKIEL_FOUR[pick].coord[line], 2, 0, TFA_EZEKIEL.sword_jump, 1);
}

static void tfa_ezekiel_special(TfaState* s, TfaNpc* x) {
    x->act = TFA_ACT_SPECIAL;
    tfa_npc_anim(s, x, EZEKIEL_ANIM_SPECIAL);
    tfa_ezekiel_stars(s, x);
    tfa_ezekiel_volley(s, x);
}

static void tfa_ezekiel_set_slot(TfaEzekielState* es, int next_attack) {
    es->attacks_left = TFA_EZEKIEL.attacks_after_slot;
    es->next_attack = next_attack;
}

/* The sword jump: a line through Ezekiel's SW tile weakens him (1-wide line, 67/67 and 33/33). */
static void tfa_ezekiel_jump(TfaState* s, const TfaNpc* x, TfaNpc* w, int* crossed, int* charge) {
    int row = w->state;
    int line = row ? w->y : w->x;
    if (row) w->x = (int16_t)(EZEKIEL_ARENA_LO + EZEKIEL_ARENA_HI - w->x);
    else w->y = (int16_t)(EZEKIEL_ARENA_LO + EZEKIEL_ARENA_HI - w->y);
    if ((row ? x->y : x->x) == line) {
        (*crossed)++;
        *charge += tfa_rand_range(s, TFA_EZEKIEL.sword_base_min, TFA_EZEKIEL.sword_base_max) + w->hp;
    }
}

static void tfa_ezekiel_swords_tick(TfaState* s, TfaNpc* x) {
    TfaEzekielState* es = &s->boss.ezekiel;
    int crossed = 0, damage = 0, jumped = 0;
    for (int i = 0; i < TFA_NUM_TARGET_SLOTS; i++) {
        TfaNpc* w = &s->npcs[i];
        if (!w->active || w->kind != TFA_NPC_EZEKIEL_SWORD) continue;
        if (--w->state_ticks == 0) {
            tfa_ezekiel_jump(s, x, w, &crossed, &damage);
            jumped++;
        }
        if (--w->attack_timer == 0) w->active = 0;
    }
    if (!jumped) return;
    int volley = es->volleys - 1;
    int heal = es->volley_swords == 4 && !crossed;
    if (es->damage_source == TFA_EZEKIEL_DAMAGE_FORCED) {
        crossed = es->forced_slot[volley] == TFA_EZEKIEL_SLOT_WEAKEN && es->clock == es->volley_start + es->forced_jump[volley];
        heal = es->forced_slot[volley] == TFA_EZEKIEL_SLOT_HEAL;
    }
    if (crossed) {
        es->sword_damage_tick = es->clock + TFA_EZEKIEL.sword_boss_delay;
        es->sword_damage = damage;
        x->act = TFA_ACT_STUNNED;
        tfa_npc_anim(s, x, EZEKIEL_ANIM_WEAKEN);
        es->weak_until = es->clock + TFA_EZEKIEL.weaken_recover;
        tfa_ezekiel_set_slot(es, es->clock + TFA_EZEKIEL.weaken_to_attack);
    } else if (heal) {
        es->heal_anim_clock = es->clock + TFA_EZEKIEL.heal_anim - TFA_EZEKIEL.sword_jump;
    }
}

static void tfa_ezekiel_delayed(TfaState* s, TfaNpc* x) {
    TfaEzekielState* es = &s->boss.ezekiel;
    if (es->clock == es->sword_damage_tick && es->damage_source == TFA_EZEKIEL_DAMAGE_SIM)
        tfa_npc_take_damage(s, x, es->sword_damage, -1, ATTACK_STYLE_NONE);
    if (es->clock == es->weak_until) tfa_npc_anim(s, x, EZEKIEL_ANIM_RECOVER);
    if (es->clock == es->heal_anim_clock) {
        tfa_npc_anim(s, x, EZEKIEL_ANIM_HEAL);
        es->heal_land_clock = es->clock + TFA_EZEKIEL.heal_land;
        tfa_ezekiel_set_slot(es, es->clock + TFA_EZEKIEL.heal_to_attack);
    }
    if (es->clock == es->heal_land_clock && es->damage_source == TFA_EZEKIEL_DAMAGE_SIM) {
        int missing = x->max_hp - x->hp;
        int heal = x->max_hp * TFA_EZEKIEL.heal_pct / 100;
        x->hp += heal < missing ? heal : missing;
    }
}

static void tfa_ezekiel_wave(TfaState* s) {
    for (int dx = 0; dx < 3; dx++)
        for (int dy = 0; dy < 3; dy++)
            for (int k = 0; k < TFA_EZEKIEL_WAVE_IMPACTS[dy][dx]; k++)
                tfa_ezekiel_strike(s, EZEKIEL_FINALE_CELL + dx, EZEKIEL_FINALE_CELL + dy,
                    1, 1, s->tick + TFA_EZEKIEL.finale_land_min + tfa_rand(s, TFA_EZEKIEL.finale_land_span),
                    TFA_EZEKIEL.finale_min_damage, TFA_EZEKIEL.finale_max_damage, EZEKIEL_GFX_FINALE_IMPACT);
}

static void tfa_ezekiel_finale_tick(TfaState* s, TfaNpc* x) {
    TfaEzekielState* es = &s->boss.ezekiel;
    if (es->clock == es->finale_roll_clock) {
        static const int8_t CELLS[4][2] = { { 27, 30 }, { 33, 30 }, { 30, 27 }, { 30, 33 } };
        int c = tfa_rand(s, 4);
        x->x = CELLS[c][0];
        x->y = CELLS[c][1];
        tfa_npc_anim(s, x, EZEKIEL_ANIM_ROLL);
        es->finale_start_clock = es->clock + TFA_EZEKIEL.finale_roll_to_cast;
    }
    if (es->clock == es->finale_start_clock) {
        tfa_npc_anim(s, x, EZEKIEL_ANIM_FINALE);
        es->next_wave_clock = es->clock + TFA_EZEKIEL.finale_first_wave;
    }
    if (es->clock == es->next_wave_clock) {
        tfa_ezekiel_wave(s);
        es->next_wave_clock += TFA_EZEKIEL.finale_wave_interval;
    }
}

static void tfa_ezekiel_begin_finale(TfaState* s, TfaEzekielState* es) {
    es->phase = TFA_EZEKIEL_FINALE;
    es->finale_roll_clock = es->clock + 1;
    for (int i = 0; i < TFA_MAX_NPCS; i++)
        if (s->npcs[i].active && s->npcs[i].kind == TFA_NPC_EZEKIEL_SWORD) s->npcs[i].active = 0;
    for (int i = 0; i < TFA_MAX_HAZARDS; i++) s->hazards[i].kind = TFA_HAZ_NONE;
}

static int tfa_ezekiel_npc_damage(TfaState* s, TfaNpc* n, int damage, int attacker, AttackStyle style) {
    (void)s; (void)attacker; (void)style;
    if (!n->attackable) return 0;
    if (n->kind == TFA_NPC_EZEKIEL_SWORD) {
        int room = TFA_EZEKIEL.sword_charge_cap - n->hp;
        int hit = damage < TFA_EZEKIEL.sword_hit_cap ? damage : TFA_EZEKIEL.sword_hit_cap;
        int applied = hit < room ? hit : room;
        n->hp += applied;
        return applied;
    }
    int applied = damage < n->hp ? damage : n->hp;
    n->hp -= applied;
    return applied;
}

static void tfa_ezekiel_npc_died(TfaState* s, TfaNpc* n, int attacker) {
    (void)attacker;
    if (n->kind != TFA_NPC_EZEKIEL) return;
    TfaEzekielState* es = &s->boss.ezekiel;
    tfa_npc_anim(s, n, EZEKIEL_ANIM_DEATH);
    n->attackable = 0;
    n->act = TFA_ACT_IDLE;
    es->phase = TFA_EZEKIEL_DYING;
    es->dying_ticks = TFA_EZEKIEL.death_to_clear;
    for (int i = 0; i < TFA_MAX_NPCS; i++)
        if (s->npcs[i].active && s->npcs[i].kind == TFA_NPC_EZEKIEL_SWORD) s->npcs[i].active = 0;
    for (int i = 0; i < TFA_MAX_HAZARDS; i++) s->hazards[i].kind = TFA_HAZ_NONE;
}

static void tfa_ezekiel_tick(TfaState* s, TfaContext* ctx) {
    TfaEzekielState* es = &s->boss.ezekiel;
    TfaNpc* x = tfa_ezekiel_boss(s);
    if (es->phase == TFA_EZEKIEL_DYING) {
        if (--es->dying_ticks <= 0) tfa_room_cleared(s, ctx);
        return;
    }
    es->clock++;
    x->act = es->clock < es->weak_until ? TFA_ACT_STUNNED : TFA_ACT_IDLE;
    if (es->phase == TFA_EZEKIEL_FINALE) {
        tfa_ezekiel_finale_tick(s, x);
        return;
    }
    if (x->hp * 1000 <= x->max_hp * TFA_EZEKIEL.finale_hp_permille) {
        tfa_ezekiel_begin_finale(s, es);
        return;
    }
    tfa_ezekiel_swords_tick(s, x);
    tfa_ezekiel_delayed(s, x);
    int tank = tfa_ezekiel_tank(s, x);
    if (tank < 0) return;
    x->target = (int8_t)tank;
    if (x->act != TFA_ACT_STUNNED) tfa_ezekiel_walk(s, x, tank);
    if (es->clock != es->next_attack || x->act == TFA_ACT_STUNNED) return;
    if (es->clock == es->magic_clock) {
        tfa_ezekiel_magic(s, x, tank);
    } else if (es->attacks_left == 0) {
        tfa_ezekiel_special(s, x);
        es->attacks_left = TFA_EZEKIEL.attacks_per_cycle;
        es->next_attack = es->clock + TFA_EZEKIEL.special_to_attack;
        es->moved = 0;
        return;
    } else if (es->damage_source == TFA_EZEKIEL_DAMAGE_SIM && es->moved && tfa_rand(s, 100) < TFA_EZEKIEL.magic_pct) {
        es->magic_clock = es->clock + TFA_EZEKIEL.magic_late;
        es->next_attack = es->magic_clock;
        return;
    } else {
        tfa_ezekiel_melee(s, x, tank);
    }
    es->attacks_left--;
    es->moved = 0;
    es->next_attack = es->clock + (es->attacks_left == 0 ? TFA_EZEKIEL.special_interval : TFA_EZEKIEL.attack_interval);
}
