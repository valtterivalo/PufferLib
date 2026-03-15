/**
 * @file encounter_inferno.h
 * @brief The Inferno — 69-wave PvM challenge with prayer switching and pillar safespotting.
 *
 * core mechanic: 3 destructible pillars block NPC projectiles. the player must
 * position behind pillars to limit incoming attacks to one prayer style at a time.
 * nibblers eat pillars, meleer can dig through them. losing all pillars = death spiral.
 *
 * monster types: nibbler (pillar eater), bat (short-range ranger), blob (prayer reader,
 * splits into 3 on death), meleer (burrows to player), ranger, mager (resurrects dead mobs),
 * jad (random 50/50 range/mage), zuk (final boss with shield mechanic).
 *
 * reference: InfernoTrainer TypeScript, runelite inferno plugin
 */

#ifndef ENCOUNTER_INFERNO_H
#define ENCOUNTER_INFERNO_H

#include "../osrs_pvp_types.h"
#include "../osrs_pvp_collision.h"
#include "../osrs_encounter.h"
#include <string.h>
#include <stdio.h>

/* ======================================================================== */
/* arena constants                                                           */
/* ======================================================================== */

#define INF_ARENA_MIN_X    10
#define INF_ARENA_MAX_X    40
#define INF_ARENA_MIN_Y    13
#define INF_ARENA_MAX_Y    44
#define INF_ARENA_WIDTH    (INF_ARENA_MAX_X - INF_ARENA_MIN_X + 1)  /* 31 */
#define INF_ARENA_HEIGHT   (INF_ARENA_MAX_Y - INF_ARENA_MIN_Y + 1)  /* 32 */

#define INF_PLAYER_START_X 28
#define INF_PLAYER_START_Y 17
#define INF_ZUK_PLAYER_START_X 25
#define INF_ZUK_PLAYER_START_Y 15

#define INF_NUM_PILLARS   3
#define INF_PILLAR_SIZE   3
#define INF_PILLAR_HP     255

static const int INF_PILLAR_POS[INF_NUM_PILLARS][2] = {
    { 21, 37 },  /* south pillar */
    { 11, 23 },  /* west pillar */
    { 28, 21 },  /* north pillar */
};

/* 9 mob spawn positions (shuffled per wave) */
#define INF_NUM_SPAWN_POS 9
static const int INF_SPAWN_POS[INF_NUM_SPAWN_POS][2] = {
    {12, 19}, {33, 19}, {14, 25}, {34, 26}, {27, 31},
    {16, 37}, {34, 39}, {12, 42}, {26, 42},
};

/* nibbler spawn position (near pillars) */
#define INF_NIBBLER_SPAWN_X 20
#define INF_NIBBLER_SPAWN_Y 25

#define INF_MAX_TICKS     6000  /* 60 minutes at 0.6s/tick */
#define INF_NUM_WAVES     69

/* ======================================================================== */
/* NPC types                                                                 */
/* ======================================================================== */

typedef enum {
    INF_NPC_NIBBLER = 0,      /* Jal-Nib: melee, eats pillars */
    INF_NPC_BAT,              /* Jal-MejRah: short-range ranged, drains run */
    INF_NPC_BLOB,             /* Jal-Ak: prayer reader, splits into 3 on death */
    INF_NPC_BLOB_MELEE,       /* Jal-Ak-Rek-Ket: melee split from blob */
    INF_NPC_BLOB_RANGE,       /* Jal-Ak-Rek-Xil: range split from blob */
    INF_NPC_BLOB_MAGE,        /* Jal-Ak-Rek-Mej: mage split from blob */
    INF_NPC_MELEER,           /* Jal-ImKot: melee, can dig */
    INF_NPC_RANGER,           /* Jal-Xil: ranged, can melee if close */
    INF_NPC_MAGER,            /* Jal-Zek: magic, resurrects dead mobs, can melee if close */
    INF_NPC_JAD,              /* JalTok-Jad: random 50/50 range/mage */
    INF_NPC_ZUK,              /* TzKal-Zuk: final boss */
    INF_NPC_HEALER_JAD,       /* Yt-HurKot: jad healer */
    INF_NPC_HEALER_ZUK,       /* Jal-MejJak: zuk healer */
    INF_NPC_ZUK_SHIELD,       /* shield NPC during Zuk */
    INF_NUM_NPC_TYPES
} InfNPCType;

typedef struct {
    int hp;
    int max_hit;
    int attack_speed;
    int attack_range;
    int size;
    int default_style;   /* ATTACK_STYLE_* */
    int can_melee;       /* 1 if can switch to melee when close */
    int def_level;
    int att_level;
    int str_level;
    int range_level;
    int magic_level;
    int stun_on_spawn;   /* ticks of stun when first spawned */
    int can_move;        /* 0 = cannot move (zuk, zuk healers) */
} InfNPCStats;

/* exact stats from InfernoTrainer TypeScript reference */
static const InfNPCStats INF_NPC_STATS[INF_NUM_NPC_TYPES] = {
    /* NIBBLER: HP=10, melee crush, size=1, attacks pillars not player, 1 tick stun on spawn */
    [INF_NPC_NIBBLER] = { .hp = 10, .max_hit = 4, .attack_speed = 4,
        .attack_range = 1, .size = 1, .default_style = ATTACK_STYLE_MELEE,
        .can_melee = 0, .def_level = 15, .att_level = 1, .str_level = 1,
        .range_level = 0, .magic_level = 0, .stun_on_spawn = 1, .can_move = 1 },

    /* BAT (JalMejRah): HP=25, ranged (range=4), size=2, drains run energy on hit */
    [INF_NPC_BAT] = { .hp = 25, .max_hit = 30, .attack_speed = 3,
        .attack_range = 4, .size = 2, .default_style = ATTACK_STYLE_RANGED,
        .can_melee = 0, .def_level = 55, .att_level = 0, .str_level = 0,
        .range_level = 120, .magic_level = 120, .stun_on_spawn = 0, .can_move = 1 },

    /* BLOB (JalAk): HP=40, prayer reader, size=3, speed=3 (effective 6 with scan phase) */
    [INF_NPC_BLOB] = { .hp = 40, .max_hit = 29, .attack_speed = 6,
        .attack_range = 15, .size = 3, .default_style = ATTACK_STYLE_MAGIC,
        .can_melee = 1, .def_level = 95, .att_level = 160, .str_level = 160,
        .range_level = 160, .magic_level = 160, .stun_on_spawn = 0, .can_move = 1 },

    /* BLOB_MELEE (JalAkRekKet): HP=15, melee, size=1 */
    [INF_NPC_BLOB_MELEE] = { .hp = 15, .max_hit = 25, .attack_speed = 4,
        .attack_range = 1, .size = 1, .default_style = ATTACK_STYLE_MELEE,
        .can_melee = 0, .def_level = 95, .att_level = 120, .str_level = 120,
        .range_level = 0, .magic_level = 0, .stun_on_spawn = 0, .can_move = 1 },

    /* BLOB_RANGE (JalAkRekXil): HP=15, ranged, size=1 */
    [INF_NPC_BLOB_RANGE] = { .hp = 15, .max_hit = 25, .attack_speed = 4,
        .attack_range = 15, .size = 1, .default_style = ATTACK_STYLE_RANGED,
        .can_melee = 0, .def_level = 95, .att_level = 0, .str_level = 0,
        .range_level = 120, .magic_level = 0, .stun_on_spawn = 0, .can_move = 1 },

    /* BLOB_MAGE (JalAkRekMej): HP=15, magic, size=1, magic_dmg=1.25 */
    [INF_NPC_BLOB_MAGE] = { .hp = 15, .max_hit = 25, .attack_speed = 4,
        .attack_range = 15, .size = 1, .default_style = ATTACK_STYLE_MAGIC,
        .can_melee = 0, .def_level = 95, .att_level = 0, .str_level = 0,
        .range_level = 0, .magic_level = 120, .stun_on_spawn = 0, .can_move = 1 },

    /* MELEER (JalImKot): HP=75, melee slash, size=4, dig mechanic */
    [INF_NPC_MELEER] = { .hp = 75, .max_hit = 40, .attack_speed = 4,
        .attack_range = 1, .size = 4, .default_style = ATTACK_STYLE_MELEE,
        .can_melee = 0, .def_level = 120, .att_level = 210, .str_level = 290,
        .range_level = 0, .magic_level = 0, .stun_on_spawn = 0, .can_move = 1 },

    /* RANGER (JalXil): HP=125, ranged (range=15), size=3, can melee (crush) if close */
    [INF_NPC_RANGER] = { .hp = 125, .max_hit = 50, .attack_speed = 4,
        .attack_range = 15, .size = 3, .default_style = ATTACK_STYLE_RANGED,
        .can_melee = 1, .def_level = 60, .att_level = 140, .str_level = 180,
        .range_level = 250, .magic_level = 0, .stun_on_spawn = 0, .can_move = 1 },

    /* MAGER (JalZek): HP=220, magic (range=15), size=4, resurrects dead mobs, can melee (stab) */
    [INF_NPC_MAGER] = { .hp = 220, .max_hit = 70, .attack_speed = 4,
        .attack_range = 15, .size = 4, .default_style = ATTACK_STYLE_MAGIC,
        .can_melee = 1, .def_level = 260, .att_level = 370, .str_level = 510,
        .range_level = 510, .magic_level = 300, .stun_on_spawn = 0, .can_move = 1 },

    /* JAD (JalTokJad): HP=350, random 50/50 range/mage, size=5, range=50 */
    [INF_NPC_JAD] = { .hp = 350, .max_hit = 113, .attack_speed = 8,
        .attack_range = 50, .size = 5, .default_style = ATTACK_STYLE_RANGED,
        .can_melee = 0, .def_level = 480, .att_level = 750, .str_level = 1020,
        .range_level = 1020, .magic_level = 510, .stun_on_spawn = 0, .can_move = 1 },

    /* ZUK (TzKalZuk): HP=1200, typeless attacks, size=7, cannot move */
    [INF_NPC_ZUK] = { .hp = 1200, .max_hit = 251, .attack_speed = 10,
        .attack_range = 99, .size = 7, .default_style = ATTACK_STYLE_MAGIC,
        .can_melee = 0, .def_level = 234, .att_level = 350, .str_level = 600,
        .range_level = 400, .magic_level = 150, .stun_on_spawn = 8, .can_move = 0 },

    /* HEALER_JAD (YtHurKot): HP=90, melee, size=1 */
    [INF_NPC_HEALER_JAD] = { .hp = 90, .max_hit = 19, .attack_speed = 4,
        .attack_range = 1, .size = 1, .default_style = ATTACK_STYLE_MELEE,
        .can_melee = 0, .def_level = 100, .att_level = 165, .str_level = 125,
        .range_level = 0, .magic_level = 0, .stun_on_spawn = 0, .can_move = 1 },

    /* HEALER_ZUK (JalMejJak): HP=75, AOE sparks, size=1, cannot move */
    [INF_NPC_HEALER_ZUK] = { .hp = 75, .max_hit = 24, .attack_speed = 4,
        .attack_range = 99, .size = 1, .default_style = ATTACK_STYLE_MAGIC,
        .can_melee = 0, .def_level = 100, .att_level = 0, .str_level = 0,
        .range_level = 0, .magic_level = 0, .stun_on_spawn = 0, .can_move = 0 },

    /* ZUK_SHIELD: HP=600, size=5 (width), oscillates left-right */
    [INF_NPC_ZUK_SHIELD] = { .hp = 600, .max_hit = 0, .attack_speed = 0,
        .attack_range = 0, .size = 5, .default_style = ATTACK_STYLE_NONE,
        .can_melee = 0, .def_level = 0, .att_level = 0, .str_level = 0,
        .range_level = 0, .magic_level = 0, .stun_on_spawn = 1, .can_move = 0 },
};

/* ======================================================================== */
/* wave compositions                                                         */
/* ======================================================================== */

#define INF_MAX_NPCS_PER_WAVE 9  /* wave 62: NNN BB BL M R MA = 9 */

typedef struct {
    uint8_t types[INF_MAX_NPCS_PER_WAVE];
    int count;
} InfWaveDef;

static const InfWaveDef INF_WAVES[INF_NUM_WAVES] = {
    #define N INF_NPC_NIBBLER
    #define B INF_NPC_BAT
    #define BL INF_NPC_BLOB
    #define M INF_NPC_MELEER
    #define R INF_NPC_RANGER
    #define MA INF_NPC_MAGER
    #define J INF_NPC_JAD
    #define Z INF_NPC_ZUK
    #define W(...) { .types = { __VA_ARGS__ }, .count = sizeof((uint8_t[]){__VA_ARGS__}) }

    /* waves 1-8: bats + nibblers introduction */
    [0]  = W(N,N,N, B),
    [1]  = W(N,N,N, B,B),
    [2]  = W(N,N,N, N,N,N),
    [3]  = W(N,N,N, BL),
    [4]  = W(N,N,N, B,BL),
    [5]  = W(N,N,N, B,B,BL),
    [6]  = W(N,N,N, BL,BL),
    [7]  = W(N,N,N, N,N,N),

    /* waves 9-17: meleer introduction */
    [8]  = W(N,N,N, M),
    [9]  = W(N,N,N, B,M),
    [10] = W(N,N,N, B,B,M),
    [11] = W(N,N,N, BL,M),
    [12] = W(N,N,N, B,BL,M),
    [13] = W(N,N,N, B,B,BL,M),
    [14] = W(N,N,N, BL,BL,M),
    [15] = W(N,N,N, M,M),
    [16] = W(N,N,N, N,N,N),

    /* waves 18-34: ranger introduction */
    [17] = W(N,N,N, R),
    [18] = W(N,N,N, B,R),
    [19] = W(N,N,N, B,B,R),
    [20] = W(N,N,N, BL,R),
    [21] = W(N,N,N, B,BL,R),
    [22] = W(N,N,N, B,B,BL,R),
    [23] = W(N,N,N, BL,BL,R),
    [24] = W(N,N,N, M,R),
    [25] = W(N,N,N, B,M,R),
    [26] = W(N,N,N, B,B,M,R),
    [27] = W(N,N,N, BL,M,R),
    [28] = W(N,N,N, B,BL,M,R),
    [29] = W(N,N,N, B,B,BL,M,R),
    [30] = W(N,N,N, BL,BL,M,R),
    [31] = W(N,N,N, M,M,R),
    [32] = W(N,N,N, R,R),
    [33] = W(N,N,N, N,N,N),

    /* waves 35-66: mager introduction (all combinations) */
    [34] = W(N,N,N, MA),
    [35] = W(N,N,N, B,MA),
    [36] = W(N,N,N, B,B,MA),
    [37] = W(N,N,N, BL,MA),
    [38] = W(N,N,N, B,BL,MA),
    [39] = W(N,N,N, B,B,BL,MA),
    [40] = W(N,N,N, BL,BL,MA),
    [41] = W(N,N,N, M,MA),
    [42] = W(N,N,N, B,M,MA),
    [43] = W(N,N,N, B,B,M,MA),
    [44] = W(N,N,N, BL,M,MA),
    [45] = W(N,N,N, B,BL,M,MA),
    [46] = W(N,N,N, B,B,BL,M,MA),
    [47] = W(N,N,N, BL,BL,M,MA),
    [48] = W(N,N,N, M,M,MA),
    [49] = W(N,N,N, R,MA),
    [50] = W(N,N,N, B,R,MA),
    [51] = W(N,N,N, B,B,R,MA),
    [52] = W(N,N,N, BL,R,MA),
    [53] = W(N,N,N, B,BL,R,MA),
    [54] = W(N,N,N, B,B,BL,R,MA),
    [55] = W(N,N,N, BL,BL,R,MA),
    [56] = W(N,N,N, M,R,MA),
    [57] = W(N,N,N, B,M,R,MA),
    [58] = W(N,N,N, B,B,M,R,MA),
    [59] = W(N,N,N, BL,M,R,MA),
    [60] = W(N,N,N, B,BL,M,R,MA),
    [61] = W(N,N,N, B,B,BL,M,R,MA),
    [62] = W(N,N,N, BL,BL,M,R,MA),
    [63] = W(N,N,N, M,M,R,MA),
    [64] = W(N,N,N, R,R,MA),
    [65] = W(N,N,N, MA,MA),

    /* waves 67-69: jads + zuk */
    [66] = W(J),
    [67] = W(J,J,J),
    [68] = W(Z),

    #undef N
    #undef B
    #undef BL
    #undef M
    #undef R
    #undef MA
    #undef J
    #undef Z
    #undef W
};

/* ======================================================================== */
/* NPC state                                                                 */
/* ======================================================================== */

/* max active NPCs: wave 62 has 9 + blob splits (3 per blob, up to 2 blobs = 6) + healers */
#define INF_MAX_NPCS      32

/* dead mob store for mager resurrection */
#define INF_MAX_DEAD_MOBS 16

typedef struct {
    InfNPCType type;
    int x, y;
    int hp, max_hp;
} InfDeadMob;

typedef struct {
    InfNPCType type;
    int x, y;
    int hp, max_hp;
    int size;
    int attack_timer;      /* ticks until next attack */
    int attack_style;      /* current attack style (may differ from default for blobs) */
    int active;
    int target_x, target_y; /* movement destination */
    int stun_timer;        /* ticks of stun remaining (cannot act) */

    /* type-specific state */
    int no_los_ticks;      /* meleer: consecutive ticks without LOS to player */
    int dig_freeze_timer;  /* meleer: ticks remaining in dig animation */
    int dig_attack_delay;  /* meleer: ticks after emerging before first attack */

    /* blob prayer-reading state */
    int blob_scan_timer;   /* blob: ticks remaining in scan phase (reads prayer) */
    int blob_scanned_prayer; /* blob: prayer read during scan (OverheadPrayer value) */

    /* jad state */
    int jad_attack_style;  /* jad: current attack style (random 50/50) */
    int jad_healer_spawned; /* jad: 1 if healers have been spawned */
    int jad_owner_idx;     /* healer: which jad this healer belongs to (-1 = none) */

    /* mager resurrection state */
    int resurrect_cooldown; /* mager: ticks until next resurrection attempt */

    /* heal state */
    int heal_target;       /* healer: NPC index being healed (-1 = none) */
    int heal_timer;        /* healer: ticks until next heal tick */
} InfNPC;

/* ======================================================================== */
/* pillar state                                                              */
/* ======================================================================== */

typedef struct {
    int x, y;
    int hp;
    int active;
} InfPillar;

/* ======================================================================== */
/* zuk state                                                                 */
/* ======================================================================== */

typedef struct {
    /* shield */
    int shield_idx;        /* NPC index of shield (-1 if dead) */
    int shield_dir;        /* +1 or -1 */
    int shield_freeze;     /* ticks of freeze at boundary */

    /* spawn timers */
    int initial_delay;     /* 14 ticks before first attack */
    int set_timer;         /* ticks until next set spawn (starts at 72) */
    int set_interval;      /* 350 ticks between set spawns */
    int enraged;           /* 1 when HP < 240 */

    int healer_spawned;    /* 1 when healers have been spawned */
    int jad_spawned;       /* 1 when jad has been spawned during shield phase */
} InfZukState;

/* ======================================================================== */
/* encounter state                                                           */
/* ======================================================================== */

typedef struct {
    Player player;

    InfNPC npcs[INF_MAX_NPCS];
    InfPillar pillars[INF_NUM_PILLARS];
    InfZukState zuk;

    /* dead mob store for mager resurrection */
    InfDeadMob dead_mobs[INF_MAX_DEAD_MOBS];
    int dead_mob_count;

    /* LOS blockers (rebuilt when pillars change) */
    LOSBlocker los_blockers[INF_NUM_PILLARS];
    int los_blocker_count;

    /* wave tracking */
    int wave;              /* current wave (0-indexed, 0-68) */
    int tick;
    int episode_over;
    int winner;            /* 0 = player won (zuk dead), 1 = player died */

    /* reward tracking */
    float reward;
    float damage_dealt_this_tick;
    float damage_received_this_tick;
    int prayer_correct_this_tick;
    int wave_completed_this_tick;
    int pillar_lost_this_tick;

    /* player combat state */
    OverheadPrayer active_prayer;
    int player_attack_timer;
    int player_attack_target; /* NPC index or -1 */
    int player_food_count;
    int player_brew_doses;
    int player_restore_doses;
    int player_special_energy;
    int player_food_timer;
    int player_potion_timer;

    /* spawn position shuffle buffer */
    int spawn_order[INF_NUM_SPAWN_POS];

    /* config */
    int start_wave;        /* for curriculum: start from a later wave */
    uint32_t rng_state;

    Log log;
} InfernoState;

/* ======================================================================== */
/* RNG                                                                       */
/* ======================================================================== */

static inline uint32_t inf_xorshift(uint32_t* state) {
    uint32_t x = *state;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    *state = x;
    return x;
}

static inline int inf_rand_int(InfernoState* s, int max) {
    if (max <= 0) return 0;
    return (int)(inf_xorshift(&s->rng_state) % (uint32_t)max);
}

static inline float inf_rand_float(InfernoState* s) {
    return (float)inf_xorshift(&s->rng_state) / (float)UINT32_MAX;
}

/* fisher-yates shuffle for spawn positions */
static void inf_shuffle_spawns(InfernoState* s) {
    for (int i = 0; i < INF_NUM_SPAWN_POS; i++)
        s->spawn_order[i] = i;
    for (int i = INF_NUM_SPAWN_POS - 1; i > 0; i--) {
        int j = inf_rand_int(s, i + 1);
        int tmp = s->spawn_order[i];
        s->spawn_order[i] = s->spawn_order[j];
        s->spawn_order[j] = tmp;
    }
}

/* ======================================================================== */
/* LOS helper: rebuild blocker array from active pillars                     */
/* ======================================================================== */

static void inf_rebuild_los(InfernoState* s) {
    s->los_blocker_count = 0;
    for (int i = 0; i < INF_NUM_PILLARS; i++) {
        if (s->pillars[i].active) {
            LOSBlocker* b = &s->los_blockers[s->los_blocker_count++];
            b->x = s->pillars[i].x;
            b->y = s->pillars[i].y;
            b->size = INF_PILLAR_SIZE;
            b->los_mask = LOS_FULL_MASK;
        }
    }
}

/* check if NPC at index i has LOS to player */
static int inf_npc_has_los(InfernoState* s, int i) {
    InfNPC* npc = &s->npcs[i];
    const InfNPCStats* stats = &INF_NPC_STATS[npc->type];
    return npc_has_line_of_sight(s->los_blockers, s->los_blocker_count,
                                 npc->x, npc->y, npc->size,
                                 s->player.x, s->player.y,
                                 stats->attack_range);
}

/* ======================================================================== */
/* dead mob store for mager resurrection                                     */
/* ======================================================================== */

static void inf_store_dead_mob(InfernoState* s, InfNPC* npc) {
    if (s->dead_mob_count >= INF_MAX_DEAD_MOBS) return;
    /* only store resurrectable types (not healers, not shield, not jad/zuk) */
    if (npc->type == INF_NPC_HEALER_JAD || npc->type == INF_NPC_HEALER_ZUK ||
        npc->type == INF_NPC_ZUK_SHIELD || npc->type == INF_NPC_ZUK ||
        npc->type == INF_NPC_JAD) return;

    InfDeadMob* dm = &s->dead_mobs[s->dead_mob_count++];
    dm->type = npc->type;
    dm->x = npc->x;
    dm->y = npc->y;
    dm->hp = npc->max_hp / 2;  /* resurrect at 50% HP */
    dm->max_hp = npc->max_hp;
}

/* ======================================================================== */
/* forward declarations                                                      */
/* ======================================================================== */

static float inf_compute_reward(InfernoState* s);
static void inf_spawn_wave(InfernoState* s);
static void inf_tick_npcs(InfernoState* s);
static void inf_tick_player(InfernoState* s, const int* actions);

/* ======================================================================== */
/* lifecycle                                                                 */
/* ======================================================================== */

static EncounterState* inf_create(void) {
    InfernoState* s = (InfernoState*)calloc(1, sizeof(InfernoState));
    s->rng_state = 12345;
    return (EncounterState*)s;
}

static void inf_destroy(EncounterState* state) {
    free(state);
}

static void inf_reset(EncounterState* state, uint32_t seed) {
    InfernoState* s = (InfernoState*)state;
    Log saved_log = s->log;
    int saved_start = s->start_wave;
    uint32_t saved_rng = s->rng_state;
    memset(s, 0, sizeof(InfernoState));
    s->log = saved_log;
    s->start_wave = saved_start;
    s->rng_state = (seed != 0) ? seed : (saved_rng != 0 ? saved_rng : 12345);

    /* player */
    s->player.entity_type = ENTITY_PLAYER;
    s->player.base_hitpoints = 99;
    s->player.current_hitpoints = 99;
    s->player.base_prayer = 99;
    s->player.current_prayer = 99;
    s->player_food_count = 8;
    s->player_brew_doses = 12;
    s->player_restore_doses = 16;
    s->player_special_energy = 100;
    s->active_prayer = PRAYER_NONE;
    s->player_attack_target = -1;

    /* spawn position depends on wave */
    int is_zuk_wave = (saved_start >= 68);
    s->player.x = is_zuk_wave ? INF_ZUK_PLAYER_START_X : INF_PLAYER_START_X;
    s->player.y = is_zuk_wave ? INF_ZUK_PLAYER_START_Y : INF_PLAYER_START_Y;

    /* pillars */
    for (int i = 0; i < INF_NUM_PILLARS; i++) {
        s->pillars[i].x = INF_PILLAR_POS[i][0];
        s->pillars[i].y = INF_PILLAR_POS[i][1];
        s->pillars[i].hp = INF_PILLAR_HP;
        s->pillars[i].active = 1;
    }
    inf_rebuild_los(s);

    /* dead mob store */
    s->dead_mob_count = 0;

    /* start at configured wave (for curriculum) */
    s->wave = s->start_wave;
    inf_spawn_wave(s);
}

/* ======================================================================== */
/* spawn: place NPCs for current wave                                        */
/* ======================================================================== */

/* find a free NPC slot, return index or -1 */
static int inf_find_free_npc(InfernoState* s) {
    for (int i = 0; i < INF_MAX_NPCS; i++) {
        if (!s->npcs[i].active) return i;
    }
    return -1;
}

/* initialize an NPC at a given slot */
static void inf_init_npc(InfernoState* s, int idx, InfNPCType type, int x, int y) {
    InfNPC* npc = &s->npcs[idx];
    const InfNPCStats* stats = &INF_NPC_STATS[type];
    memset(npc, 0, sizeof(InfNPC));

    npc->type = type;
    npc->hp = stats->hp;
    npc->max_hp = stats->hp;
    npc->size = stats->size;
    npc->attack_timer = stats->attack_speed;
    npc->attack_style = stats->default_style;
    npc->active = 1;
    npc->x = x;
    npc->y = y;
    npc->target_x = x;
    npc->target_y = y;
    npc->heal_target = -1;
    npc->jad_owner_idx = -1;
    npc->stun_timer = stats->stun_on_spawn;
}

static void inf_spawn_wave(InfernoState* s) {
    if (s->wave >= INF_NUM_WAVES) return;

    const InfWaveDef* w = &INF_WAVES[s->wave];

    /* clear all NPCs */
    for (int i = 0; i < INF_MAX_NPCS; i++) s->npcs[i].active = 0;

    /* clear dead mob store each wave */
    s->dead_mob_count = 0;

    /* shuffle spawn positions */
    inf_shuffle_spawns(s);

    /* zuk wave (wave 69, index 68) is special */
    if (s->wave == 68) {
        /* spawn Zuk — fixed position, cannot move */
        int zuk_idx = inf_find_free_npc(s);
        if (zuk_idx >= 0) {
            inf_init_npc(s, zuk_idx, INF_NPC_ZUK, 20, 5);
            s->npcs[zuk_idx].stun_timer = 14;  /* initial delay */
        }

        /* spawn shield */
        int shield_idx = inf_find_free_npc(s);
        if (shield_idx >= 0) {
            inf_init_npc(s, shield_idx, INF_NPC_ZUK_SHIELD, 23, 13);
            s->zuk.shield_idx = shield_idx;
            s->zuk.shield_dir = (inf_rand_int(s, 2) == 0) ? 1 : -1;
            s->zuk.shield_freeze = 1;  /* 1-tick freeze on spawn */
        }

        /* zuk state */
        s->zuk.initial_delay = 14;
        s->zuk.set_timer = 72;
        s->zuk.set_interval = 350;
        s->zuk.enraged = 0;
        s->zuk.healer_spawned = 0;
        s->zuk.jad_spawned = 0;

        /* player starts at zuk position */
        s->player.x = INF_ZUK_PLAYER_START_X;
        s->player.y = INF_ZUK_PLAYER_START_Y;
        return;
    }

    /* regular waves: spawn NPCs at shuffled positions */
    int spawn_idx = 0;
    for (int i = 0; i < w->count && i < INF_MAX_NPCS; i++) {
        InfNPCType type = (InfNPCType)w->types[i];
        int slot = inf_find_free_npc(s);
        if (slot < 0) break;

        int sx, sy;
        if (type == INF_NPC_NIBBLER) {
            /* nibblers spawn near pillars with small random offset */
            sx = INF_NIBBLER_SPAWN_X + inf_rand_int(s, 3) - 1;
            sy = INF_NIBBLER_SPAWN_Y + inf_rand_int(s, 3) - 1;
        } else {
            int pi = s->spawn_order[spawn_idx % INF_NUM_SPAWN_POS];
            sx = INF_SPAWN_POS[pi][0];
            sy = INF_SPAWN_POS[pi][1];
            spawn_idx++;
        }

        inf_init_npc(s, slot, type, sx, sy);
    }
}

/* ======================================================================== */
/* NPC AI: movement                                                          */
/* ======================================================================== */

static int inf_in_arena(int x, int y) {
    return x >= INF_ARENA_MIN_X && x <= INF_ARENA_MAX_X &&
           y >= INF_ARENA_MIN_Y && y <= INF_ARENA_MAX_Y;
}

static int inf_blocked_by_pillar(InfernoState* s, int x, int y, int size) {
    for (int p = 0; p < INF_NUM_PILLARS; p++) {
        if (!s->pillars[p].active) continue;
        if (los_aabb_overlap(x, y, size,
                             s->pillars[p].x, s->pillars[p].y, INF_PILLAR_SIZE))
            return 1;
    }
    return 0;
}

static void inf_npc_move(InfernoState* s, int idx) {
    InfNPC* npc = &s->npcs[idx];
    if (!npc->active) return;
    if (npc->stun_timer > 0) return;
    if (npc->dig_freeze_timer > 0) return;

    const InfNPCStats* stats = &INF_NPC_STATS[npc->type];
    if (!stats->can_move) return;

    /* nibblers target nearest active pillar */
    int tx, ty;
    if (npc->type == INF_NPC_NIBBLER) {
        int best = -1, best_dist = 999999;
        for (int p = 0; p < INF_NUM_PILLARS; p++) {
            if (!s->pillars[p].active) continue;
            int dx = s->pillars[p].x - npc->x;
            int dy = s->pillars[p].y - npc->y;
            int dist = dx*dx + dy*dy;
            if (dist < best_dist) { best_dist = dist; best = p; }
        }
        if (best >= 0) {
            tx = s->pillars[best].x;
            ty = s->pillars[best].y;
        } else {
            tx = s->player.x;
            ty = s->player.y;
        }
    } else {
        tx = s->player.x;
        ty = s->player.y;
    }

    /* move 1 tile toward target */
    int dx = 0, dy = 0;
    if (tx > npc->x) dx = 1;
    else if (tx < npc->x) dx = -1;
    if (ty > npc->y) dy = 1;
    else if (ty < npc->y) dy = -1;

    /* try diagonal first, then axis-only */
    int nx = npc->x + dx;
    int ny = npc->y + dy;
    if (inf_in_arena(nx, ny) && !inf_blocked_by_pillar(s, nx, ny, npc->size)) {
        npc->x = nx;
        npc->y = ny;
    } else {
        /* try x-only */
        nx = npc->x + dx;
        ny = npc->y;
        if (dx != 0 && inf_in_arena(nx, ny) && !inf_blocked_by_pillar(s, nx, ny, npc->size)) {
            npc->x = nx;
        } else {
            /* try y-only */
            nx = npc->x;
            ny = npc->y + dy;
            if (dy != 0 && inf_in_arena(nx, ny) && !inf_blocked_by_pillar(s, nx, ny, npc->size))
                npc->y = ny;
        }
    }
}

/* ======================================================================== */
/* NPC AI: meleer dig mechanic                                               */
/* ======================================================================== */

/* meleer digs when no LOS for 38+ ticks, 10% per tick, forced at 50 */
static void inf_meleer_dig_check(InfernoState* s, int idx) {
    InfNPC* npc = &s->npcs[idx];
    if (npc->type != INF_NPC_MELEER || !npc->active) return;
    if (npc->dig_freeze_timer > 0) {
        npc->dig_freeze_timer--;
        if (npc->dig_freeze_timer == 0 && npc->dig_attack_delay == 0) {
            /* emerge: place near player */
            npc->x = s->player.x + (inf_rand_int(s, 3) - 1);
            npc->y = s->player.y + (inf_rand_int(s, 3) - 1);
            npc->stun_timer = 2;  /* 2-tick freeze after emerging */
            npc->dig_attack_delay = 6;  /* 6-tick delay before attacking */
            npc->no_los_ticks = 0;
        }
        return;
    }
    if (npc->dig_attack_delay > 0) {
        npc->dig_attack_delay--;
        return;
    }

    /* track LOS absence */
    if (!inf_npc_has_los(s, idx)) {
        npc->no_los_ticks++;
    } else {
        npc->no_los_ticks = 0;
        return;
    }

    /* check dig trigger */
    if (npc->no_los_ticks >= 50) {
        /* forced dig */
        npc->dig_freeze_timer = 6;
    } else if (npc->no_los_ticks >= 38) {
        /* 10% chance per tick */
        if (inf_rand_int(s, 10) == 0) {
            npc->dig_freeze_timer = 6;
        }
    }
}

/* ======================================================================== */
/* NPC AI: attacks                                                           */
/* ======================================================================== */

static void inf_npc_attack(InfernoState* s, int idx) {
    InfNPC* npc = &s->npcs[idx];
    if (!npc->active) return;
    if (npc->stun_timer > 0) { npc->stun_timer--; return; }
    if (npc->dig_freeze_timer > 0) return;
    if (npc->dig_attack_delay > 0) return;
    if (npc->attack_timer > 0) { npc->attack_timer--; return; }

    const InfNPCStats* stats = &INF_NPC_STATS[npc->type];

    /* shield and zuk healers don't attack normally (handled separately) */
    if (npc->type == INF_NPC_ZUK_SHIELD) return;

    /* nibbler attacks pillars, not player */
    if (npc->type == INF_NPC_NIBBLER) {
        for (int p = 0; p < INF_NUM_PILLARS; p++) {
            if (!s->pillars[p].active) continue;
            int ddx = npc->x - s->pillars[p].x;
            int ddy = npc->y - s->pillars[p].y;
            if (ddx >= -1 && ddx <= INF_PILLAR_SIZE && ddy >= -1 && ddy <= INF_PILLAR_SIZE) {
                s->pillars[p].hp -= stats->max_hit;
                if (s->pillars[p].hp <= 0) {
                    s->pillars[p].active = 0;
                    s->pillar_lost_this_tick = 1;
                    inf_rebuild_los(s);
                }
                npc->attack_timer = stats->attack_speed;
                return;
            }
        }
        return;
    }

    /* zuk healer: heals zuk + AOE sparks on player */
    if (npc->type == INF_NPC_HEALER_ZUK) {
        /* heal Zuk */
        for (int i = 0; i < INF_MAX_NPCS; i++) {
            if (s->npcs[i].active && s->npcs[i].type == INF_NPC_ZUK) {
                int heal = inf_rand_int(s, 25);  /* 0-24 HP */
                s->npcs[i].hp += heal;
                if (s->npcs[i].hp > s->npcs[i].max_hp)
                    s->npcs[i].hp = s->npcs[i].max_hp;
                break;
            }
        }
        /* AOE sparks on player (3 spark projectiles, simplified to single hit) */
        int dmg = inf_rand_int(s, stats->max_hit + 1);
        s->player.current_hitpoints -= dmg;
        if (s->player.current_hitpoints < 0) s->player.current_hitpoints = 0;
        s->damage_received_this_tick += dmg;
        npc->attack_timer = stats->attack_speed;
        return;
    }

    /* jad healer: heals its jad, can be pulled to attack player */
    if (npc->type == INF_NPC_HEALER_JAD) {
        int jad_idx = npc->jad_owner_idx;
        if (jad_idx >= 0 && s->npcs[jad_idx].active &&
            s->npcs[jad_idx].type == INF_NPC_JAD) {
            /* heal jad 0-19 HP with 3 tick delay (simplified: heal every attack tick) */
            int heal = inf_rand_int(s, 20);
            s->npcs[jad_idx].hp += heal;
            if (s->npcs[jad_idx].hp > s->npcs[jad_idx].max_hp)
                s->npcs[jad_idx].hp = s->npcs[jad_idx].max_hp;
        }
        /* if player has targeted this healer, attack player in melee range */
        int px = s->player.x, py = s->player.y;
        int ddx = npc->x - px, ddy = npc->y - py;
        int dist = (ddx < 0 ? -ddx : ddx) > (ddy < 0 ? -ddy : ddy)
                   ? (ddx < 0 ? -ddx : ddx) : (ddy < 0 ? -ddy : ddy);
        if (dist <= 1) {
            int dmg = inf_rand_int(s, stats->max_hit + 1);
            int prayer_matches = (s->active_prayer == PRAYER_PROTECT_MELEE);
            if (prayer_matches) { dmg = 0; s->prayer_correct_this_tick = 1; }
            s->player.current_hitpoints -= dmg;
            if (s->player.current_hitpoints < 0) s->player.current_hitpoints = 0;
            s->damage_received_this_tick += dmg;
        }
        npc->attack_timer = stats->attack_speed;
        return;
    }

    /* check LOS for ranged/magic attackers */
    if (stats->attack_range > 1 && !inf_npc_has_los(s, idx)) return;

    /* check range */
    int ddx = npc->x - s->player.x;
    int ddy = npc->y - s->player.y;
    int dist = (ddx < 0 ? -ddx : ddx) > (ddy < 0 ? -ddy : ddy)
               ? (ddx < 0 ? -ddx : ddx) : (ddy < 0 ? -ddy : ddy);
    if (dist > stats->attack_range) return;

    /* blob prayer reading: 2-phase attack cycle */
    if (npc->type == INF_NPC_BLOB) {
        if (npc->blob_scan_timer > 0) {
            /* still in scan phase, waiting */
            npc->blob_scan_timer--;
            if (npc->blob_scan_timer == 0) {
                /* scan complete, determine attack style based on prayer read */
                OverheadPrayer read_prayer = (OverheadPrayer)npc->blob_scanned_prayer;
                if (read_prayer == PRAYER_PROTECT_MAGIC)
                    npc->attack_style = ATTACK_STYLE_RANGED;
                else if (read_prayer == PRAYER_PROTECT_RANGED)
                    npc->attack_style = ATTACK_STYLE_MAGIC;
                else
                    npc->attack_style = (inf_rand_int(s, 2) == 0)
                        ? ATTACK_STYLE_MAGIC : ATTACK_STYLE_RANGED;
            }
            return;
        }
        /* start scan phase: read current prayer, wait 3 ticks */
        npc->blob_scanned_prayer = (int)s->active_prayer;
        npc->blob_scan_timer = 3;
        return;
    }

    /* determine actual attack style */
    int actual_style = npc->attack_style;

    /* jad: random 50/50 range or magic each attack */
    if (npc->type == INF_NPC_JAD) {
        actual_style = (inf_rand_int(s, 2) == 0) ? ATTACK_STYLE_RANGED : ATTACK_STYLE_MAGIC;
        npc->jad_attack_style = actual_style;
    }

    /* zuk: typeless attack (not blockable by prayer) */
    if (npc->type == INF_NPC_ZUK) {
        /* check if shield blocks the attack */
        int shield_idx = s->zuk.shield_idx;
        if (shield_idx >= 0 && s->npcs[shield_idx].active) {
            InfNPC* shield = &s->npcs[shield_idx];
            int shield_left = shield->x;
            int shield_right = shield->x + shield->size;
            /* shield blocks if player within shield x range AND y <= 16 */
            if (s->player.x >= shield_left && s->player.x < shield_right &&
                s->player.y <= 16) {
                /* shield absorbs the hit */
                int dmg = inf_rand_int(s, stats->max_hit + 1);
                shield->hp -= dmg;
                if (shield->hp <= 0) {
                    shield->active = 0;
                    s->zuk.shield_idx = -1;
                }
                npc->attack_timer = s->zuk.enraged ? 7 : stats->attack_speed;
                return;
            }
        }

        /* typeless hit — not blockable by prayer */
        int dmg = inf_rand_int(s, stats->max_hit + 1);
        s->player.current_hitpoints -= dmg;
        if (s->player.current_hitpoints < 0) s->player.current_hitpoints = 0;
        s->damage_received_this_tick += dmg;
        npc->attack_timer = s->zuk.enraged ? 7 : stats->attack_speed;
        return;
    }

    /* melee switchover for ranger/mager: when close */
    if (stats->can_melee && dist <= 1) {
        actual_style = ATTACK_STYLE_MELEE;
    }

    /* damage calculation */
    int dmg = inf_rand_int(s, stats->max_hit + 1);

    /* prayer reduction */
    int prayer_matches = (actual_style == ATTACK_STYLE_MELEE && s->active_prayer == PRAYER_PROTECT_MELEE) ||
                         (actual_style == ATTACK_STYLE_RANGED && s->active_prayer == PRAYER_PROTECT_RANGED) ||
                         (actual_style == ATTACK_STYLE_MAGIC && s->active_prayer == PRAYER_PROTECT_MAGIC);

    /* jad: prayer checked at projectile hit time (3 ticks after attack animation).
       simplified here to check at attack time. */
    if (prayer_matches) {
        dmg = 0;
        s->prayer_correct_this_tick = 1;
    }

    s->player.current_hitpoints -= dmg;
    if (s->player.current_hitpoints < 0) s->player.current_hitpoints = 0;
    s->damage_received_this_tick += dmg;

    npc->attack_timer = stats->attack_speed;

    /* jad attack speed varies by wave */
    if (npc->type == INF_NPC_JAD) {
        if (s->wave == 66)      npc->attack_timer = 8;  /* wave 67 */
        else if (s->wave == 67) npc->attack_timer = 9;  /* wave 68 */
        else                    npc->attack_timer = 8;  /* zuk wave */
    }
}

/* ======================================================================== */
/* NPC AI: mager resurrection                                                */
/* ======================================================================== */

/* mager resurrects dead mobs: 10% chance per attack, 8-tick cooldown.
   only on waves 1-68 (indices 0-67), NOT during Zuk wave. */
static void inf_mager_resurrect(InfernoState* s, int idx) {
    InfNPC* npc = &s->npcs[idx];
    if (npc->type != INF_NPC_MAGER || !npc->active) return;
    if (s->wave >= 68) return;  /* no resurrection during Zuk wave */
    if (npc->resurrect_cooldown > 0) { npc->resurrect_cooldown--; return; }
    if (s->dead_mob_count == 0) return;

    /* 10% chance per attack tick */
    if (inf_rand_int(s, 10) != 0) return;

    /* pick a random dead mob */
    int di = inf_rand_int(s, s->dead_mob_count);
    InfDeadMob* dm = &s->dead_mobs[di];

    int slot = inf_find_free_npc(s);
    if (slot < 0) return;

    /* spawn near mager */
    int rx = npc->x + inf_rand_int(s, 3) - 1;
    int ry = npc->y + inf_rand_int(s, 3) - 1;
    inf_init_npc(s, slot, dm->type, rx, ry);
    s->npcs[slot].hp = dm->hp;      /* 50% of max HP */
    s->npcs[slot].max_hp = dm->max_hp;

    /* remove from dead store (swap with last) */
    s->dead_mobs[di] = s->dead_mobs[s->dead_mob_count - 1];
    s->dead_mob_count--;

    /* 8-tick cooldown */
    npc->resurrect_cooldown = 8;
}

/* ======================================================================== */
/* NPC AI: jad healer spawning                                               */
/* ======================================================================== */

static void inf_jad_check_healers(InfernoState* s, int idx) {
    InfNPC* npc = &s->npcs[idx];
    if (npc->type != INF_NPC_JAD || !npc->active) return;
    if (npc->jad_healer_spawned) return;

    /* spawn healers when below 50% HP */
    if (npc->hp > npc->max_hp / 2) return;
    npc->jad_healer_spawned = 1;

    int num_healers;
    if (s->wave == 66)      num_healers = 5;  /* wave 67: 5 healers */
    else if (s->wave == 67) num_healers = 3;  /* wave 68: 3 per jad */
    else                    num_healers = 3;  /* zuk wave: 3 */

    for (int h = 0; h < num_healers; h++) {
        int slot = inf_find_free_npc(s);
        if (slot < 0) break;
        int hx = npc->x + inf_rand_int(s, 5) - 2;
        int hy = npc->y + inf_rand_int(s, 5) - 2;
        inf_init_npc(s, slot, INF_NPC_HEALER_JAD, hx, hy);
        s->npcs[slot].jad_owner_idx = idx;
    }
}

/* ======================================================================== */
/* NPC AI: zuk phases                                                        */
/* ======================================================================== */

static void inf_zuk_tick(InfernoState* s) {
    if (s->wave != 68) return;

    /* find zuk NPC */
    int zuk_idx = -1;
    for (int i = 0; i < INF_MAX_NPCS; i++) {
        if (s->npcs[i].active && s->npcs[i].type == INF_NPC_ZUK) {
            zuk_idx = i;
            break;
        }
    }
    if (zuk_idx < 0) return;
    InfNPC* zuk = &s->npcs[zuk_idx];

    /* shield oscillation */
    int si = s->zuk.shield_idx;
    if (si >= 0 && s->npcs[si].active) {
        InfNPC* shield = &s->npcs[si];
        if (s->zuk.shield_freeze > 0) {
            s->zuk.shield_freeze--;
        } else {
            shield->x += s->zuk.shield_dir;
            /* boundary check: 5-tick freeze at edges */
            if (shield->x < 11) {
                shield->x = 11;
                s->zuk.shield_freeze = 5;
                s->zuk.shield_dir = 1;
            } else if (shield->x > 35) {
                shield->x = 35;
                s->zuk.shield_freeze = 5;
                s->zuk.shield_dir = -1;
            }
        }
    }

    /* set timer: spawns JalZek + JalXil periodically */
    if (s->zuk.set_timer > 0) {
        s->zuk.set_timer--;
    } else {
        /* spawn mager at {20,21} and ranger at {29,21} */
        int m_slot = inf_find_free_npc(s);
        if (m_slot >= 0) inf_init_npc(s, m_slot, INF_NPC_MAGER, 20, 21);
        int r_slot = inf_find_free_npc(s);
        if (r_slot >= 0) inf_init_npc(s, r_slot, INF_NPC_RANGER, 29, 21);

        /* when shield dies, these switch aggro to player (default behavior) */
        s->zuk.set_timer = s->zuk.set_interval;
    }

    /* jad spawn at HP < 480 (with shield still alive) */
    if (!s->zuk.jad_spawned && zuk->hp < 480 &&
        si >= 0 && s->npcs[si].active) {
        s->zuk.jad_spawned = 1;
        int j_slot = inf_find_free_npc(s);
        if (j_slot >= 0) {
            inf_init_npc(s, j_slot, INF_NPC_JAD, 24, 25);
        }
    }

    /* healer spawn at HP < 240: 4 JalMejJak, sets enraged */
    if (!s->zuk.healer_spawned && zuk->hp < 240) {
        s->zuk.healer_spawned = 1;
        s->zuk.enraged = 1;
        static const int healer_pos[4][2] = {
            {16, 9}, {20, 9}, {30, 9}, {34, 9}
        };
        for (int h = 0; h < 4; h++) {
            int slot = inf_find_free_npc(s);
            if (slot >= 0) {
                inf_init_npc(s, slot, INF_NPC_HEALER_ZUK,
                             healer_pos[h][0], healer_pos[h][1]);
            }
        }
    }

    /* on zuk death: all other mobs die */
    if (zuk->hp <= 0) {
        for (int i = 0; i < INF_MAX_NPCS; i++) {
            s->npcs[i].active = 0;
        }
    }
}

/* ======================================================================== */
/* NPC AI: tick all NPCs                                                     */
/* ======================================================================== */

static void inf_tick_npcs(InfernoState* s) {
    /* zuk-specific phases first */
    inf_zuk_tick(s);

    for (int i = 0; i < INF_MAX_NPCS; i++) {
        if (!s->npcs[i].active) continue;

        /* meleer dig check */
        if (s->npcs[i].type == INF_NPC_MELEER)
            inf_meleer_dig_check(s, i);

        inf_npc_move(s, i);
        inf_npc_attack(s, i);

        /* mager resurrection */
        if (s->npcs[i].type == INF_NPC_MAGER)
            inf_mager_resurrect(s, i);

        /* jad healer spawning */
        if (s->npcs[i].type == INF_NPC_JAD)
            inf_jad_check_healers(s, i);
    }
}

/* ======================================================================== */
/* player actions                                                            */
/* ======================================================================== */

#define INF_HEAD_MOVE    0   /* 9: idle + 8 directions */
#define INF_HEAD_PRAYER  1   /* 4: none, melee, range, mage */
#define INF_HEAD_TARGET  2   /* INF_MAX_NPCS+1: none or NPC index */
#define INF_HEAD_EAT     3   /* 3: none, food, karambwan */
#define INF_HEAD_POTION  4   /* 3: none, restore, brew */
#define INF_HEAD_SPEC    5   /* 2: none, spec */
#define INF_NUM_ACTION_HEADS 6

static const int INF_ACTION_DIMS[INF_NUM_ACTION_HEADS] = { 9, 4, INF_MAX_NPCS+1, 3, 3, 2 };
#define INF_ACTION_MASK_SIZE (9 + 4 + INF_MAX_NPCS+1 + 3 + 3 + 2)

/* movement directions: 0=idle, 1-8 = N,NE,E,SE,S,SW,W,NW */
static const int INF_MOVE_DX[9] = { 0, 0, 1, 1, 0, -1, -1, -1, 1 };
static const int INF_MOVE_DY[9] = { 0, 1, 1, 0, -1, -1, 0, 1, -1 };

#define INF_FOOD_HEAL     22   /* manta ray */
#define INF_BREW_HEAL     16   /* sara brew heals 16, boosts def */
#define INF_RESTORE_PRAY  (7 + 99/4)  /* 31 points */

static void inf_tick_player(InfernoState* s, const int* actions) {
    /* prayer */
    int prayer_act = actions[INF_HEAD_PRAYER];
    switch (prayer_act) {
        case 0: s->active_prayer = PRAYER_NONE; break;
        case 1: s->active_prayer = PRAYER_PROTECT_MELEE; break;
        case 2: s->active_prayer = PRAYER_PROTECT_RANGED; break;
        case 3: s->active_prayer = PRAYER_PROTECT_MAGIC; break;
    }

    /* eating */
    if (s->player_food_timer > 0) s->player_food_timer--;
    if (s->player_potion_timer > 0) s->player_potion_timer--;

    int eat_act = actions[INF_HEAD_EAT];
    if (eat_act == 1 && s->player_food_count > 0 && s->player_food_timer == 0) {
        s->player.current_hitpoints += INF_FOOD_HEAL;
        if (s->player.current_hitpoints > s->player.base_hitpoints)
            s->player.current_hitpoints = s->player.base_hitpoints;
        s->player_food_count--;
        s->player_food_timer = 3;
    }

    int pot_act = actions[INF_HEAD_POTION];
    if (pot_act == 1 && s->player_restore_doses > 0 && s->player_potion_timer == 0) {
        s->player.current_prayer += INF_RESTORE_PRAY;
        if (s->player.current_prayer > s->player.base_prayer)
            s->player.current_prayer = s->player.base_prayer;
        s->player_restore_doses--;
        s->player_potion_timer = 3;
    } else if (pot_act == 2 && s->player_brew_doses > 0 && s->player_potion_timer == 0) {
        s->player.current_hitpoints += INF_BREW_HEAL;
        if (s->player.current_hitpoints > s->player.base_hitpoints + 16)
            s->player.current_hitpoints = s->player.base_hitpoints + 16;
        s->player_brew_doses--;
        s->player_potion_timer = 3;
    }

    /* movement */
    int move_act = actions[INF_HEAD_MOVE];
    if (move_act > 0 && move_act < 9) {
        int nx = s->player.x + INF_MOVE_DX[move_act];
        int ny = s->player.y + INF_MOVE_DY[move_act];
        if (inf_in_arena(nx, ny) && !inf_blocked_by_pillar(s, nx, ny, 1)) {
            s->player.x = nx;
            s->player.y = ny;
        }
    }

    /* attack target */
    int target = actions[INF_HEAD_TARGET];
    if (target > 0 && target <= INF_MAX_NPCS) {
        int npc_idx = target - 1;
        if (s->npcs[npc_idx].active) {
            s->player_attack_target = npc_idx;
        }
    } else {
        s->player_attack_target = -1;
    }

    /* player attacks targeted NPC */
    if (s->player_attack_timer > 0) s->player_attack_timer--;
    if (s->player_attack_target >= 0 && s->player_attack_timer == 0) {
        InfNPC* target_npc = &s->npcs[s->player_attack_target];
        if (target_npc->active) {
            int max_hit = 45;  /* tbow against high-magic monsters */
            int dmg = inf_rand_int(s, max_hit + 1);
            target_npc->hp -= dmg;
            s->damage_dealt_this_tick += dmg;
            if (target_npc->hp <= 0) {
                target_npc->active = 0;

                /* blob splits into THREE mobs on death */
                if (target_npc->type == INF_NPC_BLOB) {
                    InfNPCType split_types[3] = {
                        INF_NPC_BLOB_MELEE, INF_NPC_BLOB_RANGE, INF_NPC_BLOB_MAGE
                    };
                    for (int sp = 0; sp < 3; sp++) {
                        int slot = inf_find_free_npc(s);
                        if (slot < 0) break;
                        int sx = target_npc->x + (sp - 1);
                        int sy = target_npc->y;
                        inf_init_npc(s, slot, split_types[sp], sx, sy);
                    }
                } else {
                    /* store for mager resurrection */
                    inf_store_dead_mob(s, target_npc);
                }

                /* jad healer dies when its jad dies */
                if (target_npc->type == INF_NPC_JAD) {
                    for (int j = 0; j < INF_MAX_NPCS; j++) {
                        if (s->npcs[j].active &&
                            s->npcs[j].type == INF_NPC_HEALER_JAD &&
                            s->npcs[j].jad_owner_idx == s->player_attack_target) {
                            s->npcs[j].active = 0;
                        }
                    }
                }
            }
            s->player_attack_timer = 4;  /* tbow speed on rapid */
        }
    }
}

/* ======================================================================== */
/* reward                                                                    */
/* ======================================================================== */

static float inf_compute_reward(InfernoState* s) {
    /* terminal: +1 zuk kill (win), -1 death (fail) */
    if (s->episode_over)
        return (s->winner == 0) ? 1.0f : -1.0f;

    /* wave completion: small reward that scales with wave number.
     * early waves are easy and worth less, later waves worth more.
     * wave 1 = 0.001, wave 69 = 0.069. total if all waves cleared ≈ 2.4.
     * kept small relative to terminal ±1.0 so the agent doesn't
     * farm early waves instead of pushing to zuk. */
    if (s->wave_completed_this_tick)
        return 0.001f * (float)(s->wave + 1);

    return 0.0f;
}

/* ======================================================================== */
/* step                                                                      */
/* ======================================================================== */

static void inf_step(EncounterState* state, const int* actions) {
    InfernoState* s = (InfernoState*)state;
    if (s->episode_over) return;

    /* clear per-tick state */
    s->reward = 0.0f;
    s->damage_dealt_this_tick = 0.0f;
    s->damage_received_this_tick = 0.0f;
    s->prayer_correct_this_tick = 0;
    s->wave_completed_this_tick = 0;
    s->pillar_lost_this_tick = 0;
    s->tick++;

    /* player actions */
    inf_tick_player(s, actions);

    /* NPC AI */
    inf_tick_npcs(s);

    /* check player death */
    if (s->player.current_hitpoints <= 0) {
        s->episode_over = 1;
        s->winner = 1;
        s->reward = inf_compute_reward(s);
        return;
    }

    /* check wave completion */
    int all_dead = 1;
    for (int i = 0; i < INF_MAX_NPCS; i++) {
        if (s->npcs[i].active) { all_dead = 0; break; }
    }
    if (all_dead) {
        s->wave++;
        s->wave_completed_this_tick = 1;
        if (s->wave >= INF_NUM_WAVES) {
            s->episode_over = 1;
            s->winner = 0;
        } else {
            inf_spawn_wave(s);
        }
    }

    /* timeout */
    if (s->tick >= INF_MAX_TICKS) {
        s->episode_over = 1;
        s->winner = 1;
    }

    s->reward = inf_compute_reward(s);
}

/* ======================================================================== */
/* observations                                                              */
/* ======================================================================== */

#define INF_NUM_OBS 200

static void inf_write_obs(EncounterState* state, float* obs) {
    InfernoState* s = (InfernoState*)state;
    memset(obs, 0, INF_NUM_OBS * sizeof(float));
    int i = 0;

    /* player state */
    obs[i++] = (float)s->player.current_hitpoints / 99.0f;
    obs[i++] = (float)(s->player.x - INF_ARENA_MIN_X) / (float)INF_ARENA_WIDTH;
    obs[i++] = (float)(s->player.y - INF_ARENA_MIN_Y) / (float)INF_ARENA_HEIGHT;
    obs[i++] = (s->active_prayer == PRAYER_PROTECT_MELEE) ? 1.0f : 0.0f;
    obs[i++] = (s->active_prayer == PRAYER_PROTECT_RANGED) ? 1.0f : 0.0f;
    obs[i++] = (s->active_prayer == PRAYER_PROTECT_MAGIC) ? 1.0f : 0.0f;
    obs[i++] = (float)s->player_food_count / 8.0f;
    obs[i++] = (float)s->player_brew_doses / 12.0f;
    obs[i++] = (float)s->player_restore_doses / 16.0f;
    obs[i++] = (float)s->player_special_energy / 100.0f;
    obs[i++] = (float)s->wave / (float)INF_NUM_WAVES;
    obs[i++] = (float)s->tick / (float)INF_MAX_TICKS;

    /* pillars */
    for (int p = 0; p < INF_NUM_PILLARS; p++) {
        obs[i++] = s->pillars[p].active ? 1.0f : 0.0f;
        obs[i++] = (float)s->pillars[p].hp / (float)INF_PILLAR_HP;
    }

    /* NPCs (up to INF_MAX_NPCS, but cap obs to avoid overflow) */
    int max_npc_obs = (INF_NUM_OBS - i) / 10;
    if (max_npc_obs > INF_MAX_NPCS) max_npc_obs = INF_MAX_NPCS;
    for (int n = 0; n < max_npc_obs; n++) {
        InfNPC* npc = &s->npcs[n];
        if (npc->active && (i + 10) <= INF_NUM_OBS) {
            obs[i++] = 1.0f;
            obs[i++] = (float)npc->type / (float)INF_NUM_NPC_TYPES;
            obs[i++] = (float)npc->hp / (float)npc->max_hp;
            obs[i++] = (float)(npc->x - INF_ARENA_MIN_X) / (float)INF_ARENA_WIDTH;
            obs[i++] = (float)(npc->y - INF_ARENA_MIN_Y) / (float)INF_ARENA_HEIGHT;
            obs[i++] = (float)npc->attack_timer / 10.0f;
            obs[i++] = (npc->attack_style == ATTACK_STYLE_MELEE) ? 1.0f : 0.0f;
            obs[i++] = (npc->attack_style == ATTACK_STYLE_RANGED) ? 1.0f : 0.0f;
            obs[i++] = (npc->attack_style == ATTACK_STYLE_MAGIC) ? 1.0f : 0.0f;
            obs[i++] = inf_npc_has_los(s, n) ? 1.0f : 0.0f;
        } else {
            int remaining = INF_NUM_OBS - i;
            int to_write = remaining < 10 ? remaining : 10;
            for (int j = 0; j < to_write; j++) obs[i++] = 0.0f;
        }
    }

    /* pad to INF_NUM_OBS */
    while (i < INF_NUM_OBS) obs[i++] = 0.0f;
}

static void inf_write_mask(EncounterState* state, float* mask) {
    InfernoState* s = (InfernoState*)state;
    int offset = 0;

    /* HEAD_MOVE (9): idle always valid, directions valid if in arena + not blocked */
    mask[offset++] = 1.0f;  /* idle always valid */
    for (int d = 1; d < 9; d++) {
        int nx = s->player.x + INF_MOVE_DX[d];
        int ny = s->player.y + INF_MOVE_DY[d];
        mask[offset++] = (inf_in_arena(nx, ny) && !inf_blocked_by_pillar(s, nx, ny, 1))
                         ? 1.0f : 0.0f;
    }

    /* HEAD_PRAYER (4): mask out the prayer that's already active */
    mask[offset++] = (s->active_prayer != PRAYER_NONE) ? 1.0f : 0.0f;         /* none: only if something is on */
    mask[offset++] = (s->active_prayer != PRAYER_PROTECT_MELEE) ? 1.0f : 0.0f;
    mask[offset++] = (s->active_prayer != PRAYER_PROTECT_RANGED) ? 1.0f : 0.0f;
    mask[offset++] = (s->active_prayer != PRAYER_PROTECT_MAGIC) ? 1.0f : 0.0f;

    /* HEAD_TARGET (INF_MAX_NPCS+1): none always valid, NPC valid only if active */
    mask[offset++] = 1.0f;  /* no target */
    for (int n = 0; n < INF_MAX_NPCS; n++) {
        mask[offset++] = s->npcs[n].active ? 1.0f : 0.0f;
    }

    /* HEAD_EAT (3): none, food, karambwan */
    mask[offset++] = 1.0f;  /* none always valid */
    /* food: mask if no food, eat timer active, or would waste more than half the heal */
    {
        int hp_missing = s->player.base_hitpoints - s->player.current_hitpoints;
        mask[offset++] = (s->player_food_count > 0 &&
                          s->player_food_timer == 0 &&
                          hp_missing >= INF_FOOD_HEAL / 2)
                         ? 1.0f : 0.0f;
    }
    /* karambwan not implemented separately, always masked */
    mask[offset++] = 0.0f;

    /* HEAD_POTION (3): none, restore, brew */
    mask[offset++] = 1.0f;  /* none always valid */
    /* restore: mask if no doses, timer active, or would waste more than half */
    {
        int pray_missing = s->player.base_prayer - s->player.current_prayer;
        mask[offset++] = (s->player_restore_doses > 0 &&
                          s->player_potion_timer == 0 &&
                          pray_missing >= INF_RESTORE_PRAY / 2)
                         ? 1.0f : 0.0f;
    }
    /* brew: mask if no doses, timer active, or would waste more than half.
       brew cap is base+16, so missing = (base+16) - current */
    {
        int brew_hp_room = (s->player.base_hitpoints + 16) - s->player.current_hitpoints;
        mask[offset++] = (s->player_brew_doses > 0 &&
                          s->player_potion_timer == 0 &&
                          brew_hp_room >= INF_BREW_HEAL / 2)
                         ? 1.0f : 0.0f;
    }

    /* HEAD_SPEC (2): none, spec */
    mask[offset++] = 1.0f;  /* none always valid */
    mask[offset++] = (s->player_special_energy >= 50) ? 1.0f : 0.0f;
}

/* ======================================================================== */
/* query functions                                                           */
/* ======================================================================== */

static float inf_get_reward(EncounterState* state) {
    return ((InfernoState*)state)->reward;
}

static int inf_is_terminal(EncounterState* state) {
    return ((InfernoState*)state)->episode_over;
}

static int inf_get_entity_count(EncounterState* state) {
    InfernoState* s = (InfernoState*)state;
    int count = 1;
    for (int i = 0; i < INF_MAX_NPCS; i++)
        if (s->npcs[i].active) count++;
    return count;
}

static void* inf_get_entity(EncounterState* state, int index) {
    InfernoState* s = (InfernoState*)state;
    if (index == 0) return &s->player;
    int n = 0;
    for (int i = 0; i < INF_MAX_NPCS; i++) {
        if (s->npcs[i].active) {
            n++;
            if (n == index) return &s->npcs[i];
        }
    }
    return NULL;
}

static void inf_put_int(EncounterState* state, const char* key, int value) {
    InfernoState* s = (InfernoState*)state;
    if (strcmp(key, "start_wave") == 0) s->start_wave = value;
    else if (strcmp(key, "seed") == 0) s->rng_state = (uint32_t)value;
}

static void inf_put_float(EncounterState* state, const char* key, float value) {
    (void)state; (void)key; (void)value;
}

static void inf_put_ptr(EncounterState* state, const char* key, void* value) {
    (void)state; (void)key; (void)value;
}

static int inf_get_tick(EncounterState* state) {
    return ((InfernoState*)state)->tick;
}

static int inf_get_winner(EncounterState* state) {
    return ((InfernoState*)state)->winner;
}

static void* inf_get_log(EncounterState* state) {
    return &((InfernoState*)state)->log;
}

/* ======================================================================== */
/* encounter definition                                                      */
/* ======================================================================== */

static const EncounterDef ENCOUNTER_INFERNO = {
    .name = "inferno",
    .obs_size = INF_NUM_OBS,
    .num_action_heads = INF_NUM_ACTION_HEADS,
    .action_head_dims = INF_ACTION_DIMS,
    .mask_size = INF_ACTION_MASK_SIZE,

    .create = inf_create,
    .destroy = inf_destroy,
    .reset = inf_reset,
    .step = inf_step,

    .write_obs = inf_write_obs,
    .write_mask = inf_write_mask,
    .get_reward = inf_get_reward,
    .is_terminal = inf_is_terminal,

    .get_entity_count = inf_get_entity_count,
    .get_entity = inf_get_entity,

    .put_int = inf_put_int,
    .put_float = inf_put_float,
    .put_ptr = inf_put_ptr,

    .arena_base_x = INF_ARENA_MIN_X,
    .arena_base_y = INF_ARENA_MIN_Y,
    .arena_width = INF_ARENA_WIDTH,
    .arena_height = INF_ARENA_HEIGHT,

    .render_post_tick = NULL,
    .get_log = inf_get_log,
    .get_tick = inf_get_tick,
    .get_winner = inf_get_winner,
};

__attribute__((constructor))
static void inf_register(void) {
    encounter_register(&ENCOUNTER_INFERNO);
}

#endif /* ENCOUNTER_INFERNO_H */
