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
#include "../osrs_pvp_items.h"
#include "../osrs_pvp_collision.h"
#include "../osrs_combat_shared.h"
#include "../osrs_encounter.h"
#include "../data/npc_models.h"
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

#define INF_PLAYER_START_X 25
#define INF_PLAYER_START_Y 16
#define INF_ZUK_PLAYER_START_X 25
#define INF_ZUK_PLAYER_START_Y 42

#define INF_NUM_PILLARS   3
#define INF_PILLAR_SIZE   3
#define INF_PILLAR_HP     255

static const int INF_PILLAR_POS[INF_NUM_PILLARS][2] = {
    { 21, 20 },  /* south pillar */
    { 11, 34 },  /* west pillar */
    { 28, 36 },  /* north pillar */
};

/* 9 mob spawn positions (shuffled per wave) */
#define INF_NUM_SPAWN_POS 9
static const int INF_SPAWN_POS[INF_NUM_SPAWN_POS][2] = {
    {12, 38}, {33, 38}, {14, 32}, {34, 31}, {27, 26},
    {16, 20}, {34, 18}, {12, 15}, {26, 15},
};

/* nibbler spawn position (near pillars) */
#define INF_NIBBLER_SPAWN_X 20
#define INF_NIBBLER_SPAWN_Y 32

#define INF_MAX_TICKS     18000  /* 3 hours at 0.6s/tick */
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

/* OSRS NPC definition IDs — maps InfNPCType enum to actual cache NPC IDs
 * used by the renderer to look up models/animations in npc_models.h */
static const int INF_NPC_DEF_IDS[INF_NUM_NPC_TYPES] = {
    [INF_NPC_NIBBLER]    = 7691,  /* Jal-Nib */
    [INF_NPC_BAT]        = 7692,  /* Jal-MejRah */
    [INF_NPC_BLOB]       = 7693,  /* Jal-Ak */
    [INF_NPC_BLOB_MELEE] = 7694,  /* Jal-AkRek-Mej */
    [INF_NPC_BLOB_RANGE] = 7695,  /* Jal-AkRek-Xil */
    [INF_NPC_BLOB_MAGE]  = 7696,  /* Jal-AkRek-Ket */
    [INF_NPC_MELEER]     = 7697,  /* Jal-ImKot */
    [INF_NPC_RANGER]     = 7698,  /* Jal-Xil */
    [INF_NPC_MAGER]      = 7699,  /* Jal-Zek */
    [INF_NPC_JAD]        = 7700,  /* JalTok-Jad */
    [INF_NPC_ZUK]        = 7706,  /* TzKal-Zuk */
    [INF_NPC_HEALER_JAD] = 7701,  /* Yt-HurKot */
    [INF_NPC_HEALER_ZUK] = 7708,  /* Jal-MejJak */
    [INF_NPC_ZUK_SHIELD] = 7707,  /* Ancestral Glyph */
};

typedef struct {
    int hp;
    int attack_speed;
    int attack_range;
    int size;
    int default_style;   /* ATTACK_STYLE_* */
    int can_melee;       /* 1 if can switch to melee when close */

    /* combat levels (used for attack rolls and max hit computation) */
    int att_level, str_level, def_level, range_level, magic_level;

    /* attack bonuses (for NPC attack roll: (level + 9) * (bonus + 64)) */
    int melee_att_bonus;   /* best of stab/slash/crush */
    int range_att_bonus;
    int magic_att_bonus;

    /* strength bonuses (for max hit formulas) */
    int melee_str_bonus;   /* bonuses.other.meleeStrength */
    int ranged_str_bonus;  /* bonuses.other.rangedStrength */
    int magic_base_dmg;    /* base spell damage (magicMaxHit() in InfernoTrainer) */
    int magic_dmg_pct;     /* magic damage multiplier as % (100 = 1.0x) */

    /* defence bonuses (for player hit chance against this NPC) */
    int stab_def, slash_def, crush_def;
    int magic_def_bonus;
    int ranged_def_bonus;

    /* wiki max hit cap: 0 = no cap (use formula), >0 = clamp to this value.
       needed for Jad/Zuk where InfernoTrainer multipliers overshoot wiki values. */
    int max_hit_cap;

    int stun_on_spawn;   /* ticks of stun when first spawned */
    int can_move;        /* 0 = cannot move (zuk, zuk healers) */
} InfNPCStats;

/* stats from InfernoTrainer TypeScript reference + OSRS wiki.
   max hits are computed from levels + bonuses via osrs_npc_*_max_hit() formulas. */
static const InfNPCStats INF_NPC_STATS[INF_NUM_NPC_TYPES] = {
    /* NIBBLER (JalNib): attacks pillars not player, bypasses combat formula (hardcoded 0-4) */
    [INF_NPC_NIBBLER] = { .hp = 10, .attack_speed = 4, .attack_range = 1, .size = 1,
        .default_style = ATTACK_STYLE_MELEE, .can_melee = 0,
        .att_level = 1, .str_level = 1, .def_level = 15, .range_level = 0, .magic_level = 15,
        .melee_att_bonus = 0, .range_att_bonus = 0, .magic_att_bonus = 0,
        .melee_str_bonus = 0, .ranged_str_bonus = 0, .magic_base_dmg = 0, .magic_dmg_pct = 0,
        .stab_def = -20, .slash_def = -20, .crush_def = -20, .magic_def_bonus = -20, .ranged_def_bonus = -20,
        .stun_on_spawn = 1, .can_move = 1 },

    /* BAT (JalMejRah): ranged, drains run energy on hit. computed max hit = 19 */
    [INF_NPC_BAT] = { .hp = 25, .attack_speed = 3, .attack_range = 4, .size = 2,
        .default_style = ATTACK_STYLE_RANGED, .can_melee = 0,
        .att_level = 0, .str_level = 0, .def_level = 55, .range_level = 120, .magic_level = 120,
        .melee_att_bonus = 0, .range_att_bonus = 25, .magic_att_bonus = 0,
        .melee_str_bonus = 0, .ranged_str_bonus = 30, .magic_base_dmg = 0, .magic_dmg_pct = 0,
        .stab_def = 30, .slash_def = 30, .crush_def = 30, .magic_def_bonus = -20, .ranged_def_bonus = 45,
        .stun_on_spawn = 0, .can_move = 1 },

    /* BLOB (JalAk): prayer reader, can melee if close. computed max hit = 29 */
    [INF_NPC_BLOB] = { .hp = 40, .attack_speed = 6, .attack_range = 15, .size = 3,
        .default_style = ATTACK_STYLE_MAGIC, .can_melee = 1,
        .att_level = 160, .str_level = 160, .def_level = 95, .range_level = 160, .magic_level = 160,
        .melee_att_bonus = 0, .range_att_bonus = 40, .magic_att_bonus = 45,
        .melee_str_bonus = 45, .ranged_str_bonus = 45, .magic_base_dmg = 29, .magic_dmg_pct = 100,
        .stab_def = 25, .slash_def = 25, .crush_def = 25, .magic_def_bonus = 25, .ranged_def_bonus = 25,
        .stun_on_spawn = 0, .can_move = 1 },

    /* BLOB_MELEE (JalAkRekKet): melee split. computed max hit = 18 */
    [INF_NPC_BLOB_MELEE] = { .hp = 15, .attack_speed = 4, .attack_range = 1, .size = 1,
        .default_style = ATTACK_STYLE_MELEE, .can_melee = 0,
        .att_level = 120, .str_level = 120, .def_level = 95, .range_level = 0, .magic_level = 0,
        .melee_att_bonus = 0, .range_att_bonus = 25, .magic_att_bonus = 0,
        .melee_str_bonus = 25, .ranged_str_bonus = 0, .magic_base_dmg = 0, .magic_dmg_pct = 0,
        .stab_def = 25, .slash_def = 25, .crush_def = 25, .magic_def_bonus = 0, .ranged_def_bonus = 0,
        .stun_on_spawn = 0, .can_move = 1 },

    /* BLOB_RANGE (JalAkRekXil): ranged split. computed max hit = 18 */
    [INF_NPC_BLOB_RANGE] = { .hp = 15, .attack_speed = 4, .attack_range = 15, .size = 1,
        .default_style = ATTACK_STYLE_RANGED, .can_melee = 0,
        .att_level = 0, .str_level = 0, .def_level = 95, .range_level = 120, .magic_level = 0,
        .melee_att_bonus = 0, .range_att_bonus = 25, .magic_att_bonus = 0,
        .melee_str_bonus = 0, .ranged_str_bonus = 25, .magic_base_dmg = 0, .magic_dmg_pct = 0,
        .stab_def = 0, .slash_def = 0, .crush_def = 0, .magic_def_bonus = 0, .ranged_def_bonus = 25,
        .stun_on_spawn = 0, .can_move = 1 },

    /* BLOB_MAGE (JalAkRekMej): magic split. wiki max hit = 25. InfernoTrainer has
       magicMaxHit()=0 (base Mob) but wiki clearly shows max hit 25 — use wiki value. */
    [INF_NPC_BLOB_MAGE] = { .hp = 15, .attack_speed = 4, .attack_range = 15, .size = 1,
        .default_style = ATTACK_STYLE_MAGIC, .can_melee = 0,
        .att_level = 0, .str_level = 0, .def_level = 95, .range_level = 0, .magic_level = 120,
        .melee_att_bonus = 0, .range_att_bonus = 0, .magic_att_bonus = 25,
        .melee_str_bonus = 0, .ranged_str_bonus = 0, .magic_base_dmg = 25, .magic_dmg_pct = 100,
        .stab_def = 0, .slash_def = 0, .crush_def = 0, .magic_def_bonus = 25, .ranged_def_bonus = 0,
        .stun_on_spawn = 0, .can_move = 1 },

    /* MELEER (JalImKot): melee slash, dig mechanic. computed max hit = 48 (wiki: 49) */
    [INF_NPC_MELEER] = { .hp = 75, .attack_speed = 4, .attack_range = 1, .size = 4,
        .default_style = ATTACK_STYLE_MELEE, .can_melee = 0,
        .att_level = 210, .str_level = 290, .def_level = 120, .range_level = 220, .magic_level = 120,
        .melee_att_bonus = 0, .range_att_bonus = 0, .magic_att_bonus = 0,
        .melee_str_bonus = 40, .ranged_str_bonus = 0, .magic_base_dmg = 0, .magic_dmg_pct = 0,
        .stab_def = 65, .slash_def = 65, .crush_def = 65, .magic_def_bonus = 30, .ranged_def_bonus = 50,
        .stun_on_spawn = 0, .can_move = 1 },

    /* RANGER (JalXil): ranged, can melee if close. computed max hit = 46 */
    [INF_NPC_RANGER] = { .hp = 125, .attack_speed = 4, .attack_range = 15, .size = 3,
        .default_style = ATTACK_STYLE_RANGED, .can_melee = 1,
        .att_level = 140, .str_level = 180, .def_level = 60, .range_level = 250, .magic_level = 90,
        .melee_att_bonus = 0, .range_att_bonus = 40, .magic_att_bonus = 0,
        .melee_str_bonus = 0, .ranged_str_bonus = 50, .magic_base_dmg = 0, .magic_dmg_pct = 0,
        .stab_def = 0, .slash_def = 0, .crush_def = 0, .magic_def_bonus = 0, .ranged_def_bonus = 0,
        .stun_on_spawn = 0, .can_move = 1 },

    /* MAGER (JalZek): magic, resurrects dead mobs, can melee. computed max hit = 70 */
    [INF_NPC_MAGER] = { .hp = 220, .attack_speed = 4, .attack_range = 15, .size = 4,
        .default_style = ATTACK_STYLE_MAGIC, .can_melee = 1,
        .att_level = 370, .str_level = 510, .def_level = 260, .range_level = 510, .magic_level = 300,
        .melee_att_bonus = 0, .range_att_bonus = 0, .magic_att_bonus = 80,
        .melee_str_bonus = 0, .ranged_str_bonus = 0, .magic_base_dmg = 70, .magic_dmg_pct = 100,
        .stab_def = 0, .slash_def = 0, .crush_def = 0, .magic_def_bonus = 0, .ranged_def_bonus = 0,
        .stun_on_spawn = 0, .can_move = 1 },

    /* JAD (JalTokJad): 50/50 range/mage. wiki max hit = 113. formula gives 231 ranged
       (due to very high range level + bonus), capped to wiki value. */
    [INF_NPC_JAD] = { .hp = 350, .attack_speed = 8, .attack_range = 50, .size = 5,
        .default_style = ATTACK_STYLE_RANGED, .can_melee = 0,
        .att_level = 750, .str_level = 1020, .def_level = 480, .range_level = 1020, .magic_level = 510,
        .melee_att_bonus = 0, .range_att_bonus = 80, .magic_att_bonus = 100,
        .melee_str_bonus = 0, .ranged_str_bonus = 80, .magic_base_dmg = 113, .magic_dmg_pct = 100,
        .stab_def = 0, .slash_def = 0, .crush_def = 0, .magic_def_bonus = 0, .ranged_def_bonus = 0,
        .max_hit_cap = 113,
        .stun_on_spawn = 0, .can_move = 1 },

    /* ZUK (TzKalZuk): typeless attacks, wiki max hit = 148 */
    [INF_NPC_ZUK] = { .hp = 1200, .attack_speed = 10, .attack_range = 99, .size = 7,
        .default_style = ATTACK_STYLE_MAGIC, .can_melee = 0,
        .att_level = 350, .str_level = 600, .def_level = 260, .range_level = 400, .magic_level = 150,
        .melee_att_bonus = 0, .range_att_bonus = 550, .magic_att_bonus = 550,
        .melee_str_bonus = 200, .ranged_str_bonus = 200, .magic_base_dmg = 148, .magic_dmg_pct = 100,
        .stab_def = 0, .slash_def = 0, .crush_def = 0, .magic_def_bonus = 350, .ranged_def_bonus = 100,
        .stun_on_spawn = 8, .can_move = 0 },

    /* HEALER_JAD (YtHurKot): melee, heals its Jad */
    [INF_NPC_HEALER_JAD] = { .hp = 90, .attack_speed = 4, .attack_range = 1, .size = 1,
        .default_style = ATTACK_STYLE_MELEE, .can_melee = 0,
        .att_level = 165, .str_level = 125, .def_level = 100, .range_level = 0, .magic_level = 150,
        .melee_att_bonus = 0, .range_att_bonus = 0, .magic_att_bonus = 0,
        .melee_str_bonus = 0, .ranged_str_bonus = 0, .magic_base_dmg = 0, .magic_dmg_pct = 0,
        .stab_def = 0, .slash_def = 0, .crush_def = 0, .magic_def_bonus = 130, .ranged_def_bonus = 0,
        .stun_on_spawn = 0, .can_move = 1 },

    /* HEALER_ZUK (JalMejJak): AOE magic sparks, cannot move */
    [INF_NPC_HEALER_ZUK] = { .hp = 75, .attack_speed = 4, .attack_range = 99, .size = 1,
        .default_style = ATTACK_STYLE_MAGIC, .can_melee = 0,
        .att_level = 0, .str_level = 0, .def_level = 100, .range_level = 0, .magic_level = 0,
        .melee_att_bonus = 0, .range_att_bonus = 0, .magic_att_bonus = 0,
        .melee_str_bonus = 0, .ranged_str_bonus = 0, .magic_base_dmg = 24, .magic_dmg_pct = 100,
        .stab_def = 0, .slash_def = 0, .crush_def = 0, .magic_def_bonus = 0, .ranged_def_bonus = 0,
        .stun_on_spawn = 0, .can_move = 0 },

    /* ZUK_SHIELD: no attacks, oscillates left-right */
    [INF_NPC_ZUK_SHIELD] = { .hp = 600, .attack_speed = 0, .attack_range = 0, .size = 5,
        .default_style = ATTACK_STYLE_NONE, .can_melee = 0,
        .att_level = 0, .str_level = 0, .def_level = 0, .range_level = 0, .magic_level = 0,
        .melee_att_bonus = 0, .range_att_bonus = 0, .magic_att_bonus = 0,
        .melee_str_bonus = 0, .ranged_str_bonus = 0, .magic_base_dmg = 0, .magic_dmg_pct = 0,
        .stab_def = 0, .slash_def = 0, .crush_def = 0, .magic_def_bonus = 0, .ranged_def_bonus = 0,
        .stun_on_spawn = 1, .can_move = 0 },
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

    /* freeze state (ice barrage) */
    int frozen_ticks;       /* ticks remaining in ice barrage freeze */

    /* heal state */
    int heal_target;       /* healer: NPC index being healed (-1 = none) */
    int heal_timer;        /* healer: ticks until next heal tick */

    /* pending hit from player attack (projectile in flight) */
    EncounterPendingHit pending_hit;

    /* death linger: NPC stays visible for death animation + final hitsplat.
       >0 means dying (decremented each tick), 0 = alive or fully removed. */
    int death_ticks;

    /* per-tick render flags (cleared at start of each tick) */
    int attacked_this_tick;  /* 1 when NPC attacks this tick */
    int moved_this_tick;     /* 1 when NPC moves this tick */
    int hit_landed_this_tick; /* 1 when this NPC was hit by player */
    int hit_damage;          /* damage dealt to this NPC this tick */
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
/* weapon sets and pre-computed stats                                         */
/* ======================================================================== */

typedef enum {
    INF_GEAR_MAGE = 0,
    INF_GEAR_TBOW,
    INF_GEAR_BP,
    INF_NUM_WEAPON_SETS
} InfWeaponSet;

/* gear loadout arrays per weapon set */
static const uint8_t INF_MAGE_LOADOUT[NUM_GEAR_SLOTS] = {
    [GEAR_SLOT_HEAD]   = ITEM_MASORI_MASK_F,
    [GEAR_SLOT_CAPE]   = ITEM_DIZANAS_QUIVER,
    [GEAR_SLOT_NECK]   = ITEM_OCCULT_NECKLACE,
    [GEAR_SLOT_AMMO]   = ITEM_DRAGON_ARROWS,
    [GEAR_SLOT_WEAPON] = ITEM_KODAI_WAND,
    [GEAR_SLOT_SHIELD] = ITEM_CRYSTAL_SHIELD,
    [GEAR_SLOT_BODY]   = ITEM_ANCESTRAL_TOP,
    [GEAR_SLOT_LEGS]   = ITEM_ANCESTRAL_BOTTOM,
    [GEAR_SLOT_HANDS]  = ITEM_ZARYTE_VAMBRACES,
    [GEAR_SLOT_FEET]   = ITEM_PEGASIAN_BOOTS,
    [GEAR_SLOT_RING]   = ITEM_RING_OF_SUFFERING_RI,
};

static const uint8_t INF_RANGE_TBOW_LOADOUT[NUM_GEAR_SLOTS] = {
    [GEAR_SLOT_HEAD]   = ITEM_MASORI_MASK_F,
    [GEAR_SLOT_CAPE]   = ITEM_DIZANAS_QUIVER,
    [GEAR_SLOT_NECK]   = ITEM_NECKLACE_OF_ANGUISH,
    [GEAR_SLOT_AMMO]   = ITEM_DRAGON_ARROWS,
    [GEAR_SLOT_WEAPON] = ITEM_TWISTED_BOW,
    [GEAR_SLOT_SHIELD] = ITEM_NONE,  /* tbow is 2h */
    [GEAR_SLOT_BODY]   = ITEM_MASORI_BODY_F,
    [GEAR_SLOT_LEGS]   = ITEM_MASORI_CHAPS_F,
    [GEAR_SLOT_HANDS]  = ITEM_ZARYTE_VAMBRACES,
    [GEAR_SLOT_FEET]   = ITEM_PEGASIAN_BOOTS,
    [GEAR_SLOT_RING]   = ITEM_RING_OF_SUFFERING_RI,
};

static const uint8_t INF_RANGE_BP_LOADOUT[NUM_GEAR_SLOTS] = {
    [GEAR_SLOT_HEAD]   = ITEM_MASORI_MASK_F,
    [GEAR_SLOT_CAPE]   = ITEM_DIZANAS_QUIVER,
    [GEAR_SLOT_NECK]   = ITEM_NECKLACE_OF_ANGUISH,
    [GEAR_SLOT_AMMO]   = ITEM_DRAGON_ARROWS,  /* arrows persist across all switches */
    [GEAR_SLOT_WEAPON] = ITEM_TOXIC_BLOWPIPE,
    [GEAR_SLOT_SHIELD] = ITEM_NONE,  /* bp is 2h */
    [GEAR_SLOT_BODY]   = ITEM_MASORI_BODY_F,
    [GEAR_SLOT_LEGS]   = ITEM_MASORI_CHAPS_F,
    [GEAR_SLOT_HANDS]  = ITEM_ZARYTE_VAMBRACES,
    [GEAR_SLOT_FEET]   = ITEM_PEGASIAN_BOOTS,
    [GEAR_SLOT_RING]   = ITEM_RING_OF_SUFFERING_RI,
};

/* pointer array for loadout switching */
static const uint8_t* const INF_LOADOUTS[INF_NUM_WEAPON_SETS] = {
    INF_MAGE_LOADOUT,
    INF_RANGE_TBOW_LOADOUT,
    INF_RANGE_BP_LOADOUT,
};

/* tank overlay items (justiciar) */
#define INF_TANK_HEAD ITEM_JUSTICIAR_FACEGUARD
#define INF_TANK_BODY ITEM_JUSTICIAR_CHESTGUARD
#define INF_TANK_LEGS ITEM_JUSTICIAR_LEGGUARDS

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
    int wave_spawn_delay;  /* ticks until first wave spawns (0 = spawn immediately) */
    int episode_over;
    int winner;            /* 0 = player won (zuk dead), 1 = player died */

    /* reward tracking */
    float reward;
    float episode_return;  /* accumulated reward over entire episode */
    float damage_dealt_this_tick;
    float damage_received_this_tick;
    int prayer_correct_this_tick;
    int wave_completed_this_tick;
    int pillar_lost_this_tick;     /* -1 = none, 0-2 = which pillar was destroyed */

    /* cumulative stats for diagnostics */
    float total_damage_dealt;
    float total_damage_received;
    int total_waves_cleared;
    int ticks_without_action;  /* consecutive ticks with no attack or movement */

    /* player combat state */
    OverheadPrayer active_prayer;
    int prayer_drain_counter;  /* shared drain system counter (see encounter_drain_prayer) */
    int player_attack_timer;
    int player_attack_target; /* NPC index or -1 */
    int player_brew_doses;
    int player_restore_doses;
    int player_bastion_doses;
    int player_stamina_doses;
    int player_potion_timer;

    /* gear state */
    InfWeaponSet weapon_set;
    EncounterLoadoutStats loadout_stats[INF_NUM_WEAPON_SETS];
    int armor_tank;            /* 1 = justiciar overlay active */
    int stamina_active_ticks;  /* countdown for stamina effect */
    int spell_choice;          /* 0 = blood barrage, 1 = ice barrage */

    /* per-tick player attack event for renderer projectiles */
    int player_attacked_this_tick;  /* 1 if player fired an attack this tick */
    int player_attack_npc_idx;      /* NPC index targeted by player attack */
    int player_attack_dmg;          /* total damage dealt */
    int player_attack_style_id;     /* ATTACK_STYLE_* of the player attack */

    /* pending hits on player from NPC attacks (projectiles in flight) */
    EncounterPendingHit player_pending_hits[ENCOUNTER_MAX_PENDING_HITS];
    int player_pending_hit_count;

    /* nibbler pillar target: random pillar chosen per wave, all nibblers attack it */
    int nibbler_target_pillar;

    /* spawn position shuffle buffer */
    int spawn_order[INF_NUM_SPAWN_POS];

    /* collision map (loaded from cache, passed via put_ptr) */
    const CollisionMap* collision_map;
    int world_offset_x, world_offset_y;

    /* human click-to-move destination (-1 = no dest) */
    int player_dest_x, player_dest_y;

    /* config */
    int start_wave;        /* for curriculum: start from a later wave */
    uint32_t rng_state;

    Log log;
} InfernoState;

/* prayer check and RNG: use shared encounter_prayer_correct_for_style(),
   encounter_rand_int(), encounter_rand_float() from osrs_combat_shared.h */

static void inf_shuffle_spawns(InfernoState* s) {
    for (int i = 0; i < INF_NUM_SPAWN_POS; i++)
        s->spawn_order[i] = i;
    encounter_shuffle(s->spawn_order, INF_NUM_SPAWN_POS, &s->rng_state);
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
static void inf_apply_npc_death(InfernoState* s, int npc_idx);

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
    const CollisionMap* saved_cmap = s->collision_map;
    int saved_wox = s->world_offset_x;
    int saved_woy = s->world_offset_y;
    memset(s, 0, sizeof(InfernoState));
    s->log = saved_log;
    s->start_wave = saved_start;
    s->collision_map = saved_cmap;
    s->world_offset_x = saved_wox;
    s->world_offset_y = saved_woy;
    s->rng_state = encounter_resolve_seed(saved_rng, seed);

    /* human click-to-move: no destination after reset */
    s->player_dest_x = -1;
    s->player_dest_y = -1;

    /* player */
    s->player.entity_type = ENTITY_PLAYER;
    s->player.base_hitpoints = 99;
    s->player.current_hitpoints = 99;
    s->player.base_prayer = 99;
    s->player.current_prayer = 99;
    /* start in mage gear (kodai + crystal shield + ancestral) */
    s->weapon_set = INF_GEAR_MAGE;
    s->armor_tank = 0;
    encounter_apply_loadout(&s->player, INF_MAGE_LOADOUT, GEAR_MAGE);
    {
        uint8_t tank_extra[NUM_GEAR_SLOTS];
        memset(tank_extra, ITEM_NONE, NUM_GEAR_SLOTS);
        tank_extra[GEAR_SLOT_HEAD] = INF_TANK_HEAD;
        tank_extra[GEAR_SLOT_BODY] = INF_TANK_BODY;
        tank_extra[GEAR_SLOT_LEGS] = INF_TANK_LEGS;
        encounter_populate_inventory(&s->player, INF_LOADOUTS, INF_NUM_WEAPON_SETS, tank_extra);
    }
    s->player_brew_doses = 32;     /* 8 pots x 4 doses */
    s->player_restore_doses = 40;  /* 10 pots x 4 doses */
    s->player_bastion_doses = 4;   /* 1 pot x 4 doses */
    s->player_stamina_doses = 4;   /* 1 pot x 4 doses */
    s->stamina_active_ticks = 0;
    s->active_prayer = PRAYER_NONE;
    s->player_attack_target = -1;

    /* compute loadout stats from item database (replaces old hardcoded INF_WEAPON_STATS) */
    encounter_compute_loadout_stats(INF_MAGE_LOADOUT, ATTACK_STYLE_MAGIC,
        ENCOUNTER_PRAYER_AUGURY, 99, 0, 30, &s->loadout_stats[INF_GEAR_MAGE]);
    encounter_compute_loadout_stats(INF_RANGE_TBOW_LOADOUT, ATTACK_STYLE_RANGED,
        ENCOUNTER_PRAYER_RIGOUR, 99, 0, 0, &s->loadout_stats[INF_GEAR_TBOW]);
    encounter_compute_loadout_stats(INF_RANGE_BP_LOADOUT, ATTACK_STYLE_RANGED,
        ENCOUNTER_PRAYER_RIGOUR, 99, 0, 0, &s->loadout_stats[INF_GEAR_BP]);

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

    /* start at configured wave (for curriculum).
       delay first wave spawn by 10 ticks — in-game there's a delay
       after entering the inferno before wave 1 begins. */
    s->wave = s->start_wave;
    s->wave_spawn_delay = 10;
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

    /* clear all NPCs and pending hits */
    for (int i = 0; i < INF_MAX_NPCS; i++) s->npcs[i].active = 0;
    s->player_pending_hit_count = 0;

    /* clear dead mob store each wave */
    s->dead_mob_count = 0;

    /* shuffle spawn positions */
    inf_shuffle_spawns(s);

    /* pick random active pillar for nibblers this wave */
    {
        int active_pillars[INF_NUM_PILLARS];
        int num_active = 0;
        for (int p = 0; p < INF_NUM_PILLARS; p++) {
            if (s->pillars[p].active) active_pillars[num_active++] = p;
        }
        s->nibbler_target_pillar = (num_active > 0)
            ? active_pillars[encounter_rand_int(&s->rng_state, num_active)] : -1;
    }

    /* zuk wave (wave 69, index 68) is special */
    if (s->wave == 68) {
        /* spawn Zuk — fixed position, cannot move */
        int zuk_idx = inf_find_free_npc(s);
        if (zuk_idx >= 0) {
            inf_init_npc(s, zuk_idx, INF_NPC_ZUK, 20, 52);
            s->npcs[zuk_idx].stun_timer = 14;  /* initial delay */
        }

        /* spawn shield */
        int shield_idx = inf_find_free_npc(s);
        if (shield_idx >= 0) {
            inf_init_npc(s, shield_idx, INF_NPC_ZUK_SHIELD, 23, 44);
            s->zuk.shield_idx = shield_idx;
            s->zuk.shield_dir = (encounter_rand_int(&s->rng_state, 2) == 0) ? 1 : -1;
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
            sx = INF_NIBBLER_SPAWN_X + encounter_rand_int(&s->rng_state, 3) - 1;
            sy = INF_NIBBLER_SPAWN_Y + encounter_rand_int(&s->rng_state, 3) - 1;
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

/* BFS dynamic obstacle callback — pillars block pathfinding.
   receives absolute world coords, converts to local for pillar check. */
static int inf_pathfind_blocked(void* ctx, int abs_x, int abs_y) {
    InfernoState* s = (InfernoState*)ctx;
    int lx = abs_x - s->world_offset_x;
    int ly = abs_y - s->world_offset_y;
    return inf_blocked_by_pillar(s, lx, ly, 1);
}

/* NPC movement blocked callback for encounter_npc_step_toward.
   checks arena bounds, pillars, collision map, and NPC-vs-NPC collision.
   ctx is a temporary struct with state + current NPC index. */
typedef struct { InfernoState* s; int self_idx; } InfMoveCtx;

static int inf_npc_blocked(void* ctx, int x, int y, int size) {
    InfMoveCtx* mc = (InfMoveCtx*)ctx;
    InfernoState* s = mc->s;
    if (!inf_in_arena(x, y)) return 1;
    if (inf_blocked_by_pillar(s, x, y, size)) return 1;
    if (s->collision_map &&
        !collision_tile_walkable(s->collision_map, 0,
            x + s->world_offset_x, y + s->world_offset_y))
        return 1;
    /* NPC-vs-NPC collision: check all active NPCs except self and nibblers
       (nibblers don't consume space — other mobs walk through them) */
    for (int i = 0; i < INF_MAX_NPCS; i++) {
        if (i == mc->self_idx) continue;
        InfNPC* other = &s->npcs[i];
        if (!other->active) continue;
        if (other->type == INF_NPC_NIBBLER) continue;  /* nibblers transparent */
        if (los_aabb_overlap(x, y, size, other->x, other->y, other->size))
            return 1;
    }
    return 0;
}

/* forward declaration — defined after potions/food section */
static int inf_tile_walkable(void* ctx, int x, int y);

static void inf_npc_move(InfernoState* s, int idx) {
    InfNPC* npc = &s->npcs[idx];
    if (!npc->active) return;
    if (npc->stun_timer > 0) return;
    if (npc->dig_freeze_timer > 0) return;
    if (npc->frozen_ticks > 0) return;

    const InfNPCStats* stats = &INF_NPC_STATS[npc->type];
    if (!stats->can_move) return;

    /* OSRS: NPC shuffles off player tile when overlapping (Mob.ts:109-153).
       if the NPC steps out, skip further movement this tick. */
    if (npc->type != INF_NPC_NIBBLER) {
        int stepped = encounter_npc_step_out_from_under(
            &npc->x, &npc->y, npc->size,
            s->player.x, s->player.y,
            inf_tile_walkable, s, &s->rng_state);
        if (stepped) {
            npc->moved_this_tick = 1;
            return;
        }
    }

    /* ranged/magic NPCs stop moving when they have LOS to the player.
       this is the core OSRS mechanic: NPCs only walk toward their target
       while they cannot see it. once LOS is established, they attack. */
    if (npc->type != INF_NPC_NIBBLER && stats->attack_range > 1) {
        if (inf_npc_has_los(s, idx)) return;
    }

    /* target selection */
    int tx, ty;
    if (npc->type == INF_NPC_NIBBLER) {
        int p = s->nibbler_target_pillar;
        if (p >= 0 && p < INF_NUM_PILLARS && s->pillars[p].active) {
            tx = s->pillars[p].x;
            ty = s->pillars[p].y;
        } else {
            int found = 0;
            for (int pp = 0; pp < INF_NUM_PILLARS; pp++) {
                if (s->pillars[pp].active) {
                    tx = s->pillars[pp].x;
                    ty = s->pillars[pp].y;
                    found = 1;
                    break;
                }
            }
            if (!found) { tx = s->player.x; ty = s->player.y; }
        }
    } else {
        tx = s->player.x;
        ty = s->player.y;
    }

    /* greedy step toward target using shared helper */
    int ox = npc->x, oy = npc->y;
    InfMoveCtx mc = { s, idx };
    encounter_npc_step_toward(&npc->x, &npc->y, tx, ty, npc->size,
                              inf_npc_blocked, &mc);
    if (npc->x != ox || npc->y != oy)
        npc->moved_this_tick = 1;
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
            npc->x = s->player.x + (encounter_rand_int(&s->rng_state, 3) - 1);
            npc->y = s->player.y + (encounter_rand_int(&s->rng_state, 3) - 1);
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
        if (encounter_rand_int(&s->rng_state, 10) == 0) {
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
                /* nibblers deal 0-4 damage per hit (ref: InfernoTrainer JalNib.ts).
                   bypasses combat formula — custom weapon directly rolls rand(0..4). */
                int dmg = encounter_rand_int(&s->rng_state, 5);
                s->pillars[p].hp -= dmg;
                if (s->pillars[p].hp <= 0) {
                    s->pillars[p].active = 0;
                    s->pillar_lost_this_tick = p;
                    inf_rebuild_los(s);
                    /* pillar death AOE: deals damage to all mobs + player within 1 tile */
                    for (int n = 0; n < INF_MAX_NPCS; n++) {
                        if (!s->npcs[n].active) continue;
                        int ndx = s->npcs[n].x - s->pillars[p].x;
                        int ndy = s->npcs[n].y - s->pillars[p].y;
                        if (ndx >= -1 && ndx <= INF_PILLAR_SIZE && ndy >= -1 && ndy <= INF_PILLAR_SIZE) {
                            encounter_damage_npc(&s->npcs[n].hp, &s->npcs[n].hit_landed_this_tick, &s->npcs[n].hit_damage, 12);
                            inf_apply_npc_death(s, n);
                        }
                    }
                    /* also damages the player if standing next to the pillar */
                    {
                        int pdx = s->player.x - s->pillars[p].x;
                        int pdy = s->player.y - s->pillars[p].y;
                        if (pdx >= -1 && pdx <= INF_PILLAR_SIZE && pdy >= -1 && pdy <= INF_PILLAR_SIZE) {
                            /* TODO: find actual pillar collapse damage formula (server-side).
                               49 observed in-game, likely scales with something. */
                            encounter_damage_player(&s->player, 49, &s->damage_received_this_tick);
                        }
                    }
                }
                npc->attacked_this_tick = 1;
                npc->attack_timer = stats->attack_speed;
                return;
            }
        }
        return;
    }

    /* zuk healer: heals zuk + AOE sparks on player (instant, no projectile delay) */
    if (npc->type == INF_NPC_HEALER_ZUK) {
        /* heal Zuk */
        for (int i = 0; i < INF_MAX_NPCS; i++) {
            if (s->npcs[i].active && s->npcs[i].type == INF_NPC_ZUK) {
                int heal = encounter_rand_int(&s->rng_state, 25);  /* 0-24 HP */
                s->npcs[i].hp += heal;
                if (s->npcs[i].hp > s->npcs[i].max_hp)
                    s->npcs[i].hp = s->npcs[i].max_hp;
                break;
            }
        }
        /* AOE sparks on player — queue as pending hit with magic delay */
        int max_hit = osrs_npc_magic_max_hit(stats->magic_base_dmg, stats->magic_dmg_pct);
        int dmg = encounter_rand_int(&s->rng_state, max_hit + 1);
        if (s->player_pending_hit_count < ENCOUNTER_MAX_PENDING_HITS) {
            int d = encounter_dist_to_npc(npc->x, npc->y, s->player.x, s->player.y, 1);
            EncounterPendingHit* ph = &s->player_pending_hits[s->player_pending_hit_count++];
            ph->active = 1;
            ph->damage = dmg;
            ph->ticks_remaining = encounter_magic_hit_delay(d, 0);
            ph->attack_style = ATTACK_STYLE_MAGIC;
            ph->check_prayer = 0;
        }
        npc->attacked_this_tick = 1;
        npc->attack_timer = stats->attack_speed;
        return;
    }

    /* jad healer: heals its jad, can be pulled to attack player */
    if (npc->type == INF_NPC_HEALER_JAD) {
        int jad_idx = npc->jad_owner_idx;
        if (jad_idx >= 0 && s->npcs[jad_idx].active &&
            s->npcs[jad_idx].type == INF_NPC_JAD) {
            /* heal jad 0-19 HP with 3 tick delay (simplified: heal every attack tick) */
            int heal = encounter_rand_int(&s->rng_state, 20);
            s->npcs[jad_idx].hp += heal;
            if (s->npcs[jad_idx].hp > s->npcs[jad_idx].max_hp)
                s->npcs[jad_idx].hp = s->npcs[jad_idx].max_hp;
        }
        /* if player has targeted this healer, attack player in melee range.
           melee = instant (delay 0), apply immediately. */
        if (encounter_dist_to_npc(s->player.x, s->player.y, npc->x, npc->y, 1) == 1) {
            int max_hit = osrs_npc_melee_max_hit(stats->str_level, stats->melee_str_bonus);
            int dmg = encounter_rand_int(&s->rng_state, max_hit + 1);
            /* accuracy roll */
            int att_roll = osrs_npc_attack_roll(stats->att_level, stats->melee_att_bonus);
            const EncounterLoadoutStats* ls = &s->loadout_stats[s->weapon_set];
            int def_bonus = ls->def_stab;  /* melee: use stab def as approximation */
            int def_roll = osrs_player_def_roll_vs_npc(99, 99, def_bonus, ATTACK_STYLE_MELEE);
            if (encounter_rand_float(&s->rng_state) >= osrs_hit_chance(att_roll, def_roll)) dmg = 0;
            int prayer_matches = (s->active_prayer == PRAYER_PROTECT_MELEE);
            if (prayer_matches) { dmg = 0; s->prayer_correct_this_tick = 1; }
            encounter_damage_player(&s->player, dmg, &s->damage_received_this_tick);
        }
        npc->attacked_this_tick = 1;
        npc->attack_timer = stats->attack_speed;
        return;
    }

    /* check LOS for ranged/magic attackers */
    if (stats->attack_range > 1 && !inf_npc_has_los(s, idx)) return;

    /* compute distance to player */
    int dist = encounter_dist_to_npc(s->player.x, s->player.y,
                                      npc->x, npc->y, npc->size);
    if (dist == 0 || dist > stats->attack_range) return;

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
                    npc->attack_style = (encounter_rand_int(&s->rng_state, 2) == 0)
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
        actual_style = (encounter_rand_int(&s->rng_state, 2) == 0) ? ATTACK_STYLE_RANGED : ATTACK_STYLE_MAGIC;
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
            /* shield blocks if player within shield x range AND y >= 41 */
            if (s->player.x >= shield_left && s->player.x < shield_right &&
                s->player.y >= 41) {
                /* shield absorbs the hit (typeless — no accuracy roll) */
                int max_hit = osrs_npc_magic_max_hit(stats->magic_base_dmg, stats->magic_dmg_pct);
                int dmg = encounter_rand_int(&s->rng_state, max_hit + 1);
                encounter_damage_npc(&shield->hp, &shield->hit_landed_this_tick, &shield->hit_damage, dmg);
                if (shield->hp <= 0) {
                    shield->active = 0;
                    s->zuk.shield_idx = -1;
                }
                npc->attacked_this_tick = 1;
                npc->attack_timer = s->zuk.enraged ? 7 : stats->attack_speed;
                return;
            }
        }

        /* typeless hit — not blockable by prayer, no accuracy roll, instant */
        int max_hit = osrs_npc_magic_max_hit(stats->magic_base_dmg, stats->magic_dmg_pct);
        int dmg = encounter_rand_int(&s->rng_state, max_hit + 1);
        encounter_damage_player(&s->player, dmg, &s->damage_received_this_tick);
        npc->attacked_this_tick = 1;
        npc->attack_timer = s->zuk.enraged ? 7 : stats->attack_speed;
        return;
    }

    /* melee switchover for ranger/mager: when close */
    if (stats->can_melee && dist == 1) {
        actual_style = ATTACK_STYLE_MELEE;
    }

    /* max hit from stats + bonuses via OSRS combat formulas */
    int max_hit = osrs_npc_max_hit(actual_style,
        stats->str_level, stats->range_level,
        stats->melee_str_bonus, stats->ranged_str_bonus,
        stats->magic_base_dmg, stats->magic_dmg_pct);
    if (stats->max_hit_cap > 0 && max_hit > stats->max_hit_cap)
        max_hit = stats->max_hit_cap;
    int dmg = encounter_rand_int(&s->rng_state, max_hit + 1);

    /* accuracy roll: NPC attack roll vs player defence roll */
    {
        int att_lvl, att_bonus;
        if (actual_style == ATTACK_STYLE_MELEE) {
            att_lvl = stats->att_level; att_bonus = stats->melee_att_bonus;
        } else if (actual_style == ATTACK_STYLE_RANGED) {
            att_lvl = stats->range_level; att_bonus = stats->range_att_bonus;
        } else {
            att_lvl = stats->magic_level; att_bonus = stats->magic_att_bonus;
        }
        int att_roll = osrs_npc_attack_roll(att_lvl, att_bonus);
        const EncounterLoadoutStats* ls = &s->loadout_stats[s->weapon_set];
        int def_bonus;
        if (actual_style == ATTACK_STYLE_RANGED) def_bonus = ls->def_ranged;
        else if (actual_style == ATTACK_STYLE_MAGIC) def_bonus = ls->def_magic;
        else def_bonus = ls->def_stab;  /* melee: stab as approximation */
        int def_roll = osrs_player_def_roll_vs_npc(99, 99, def_bonus, actual_style);
        if (encounter_rand_float(&s->rng_state) >= osrs_hit_chance(att_roll, def_roll))
            dmg = 0;  /* missed */
    }

    /* bat special: drain run energy on every hit */
    if (npc->type == INF_NPC_BAT && dmg > 0) {
        s->player.run_energy -= 300;
        if (s->player.run_energy < 0) s->player.run_energy = 0;
    }

    /* compute hit delay based on attack style */
    int hit_delay = 0;
    if (actual_style == ATTACK_STYLE_MAGIC)
        hit_delay = encounter_magic_hit_delay(dist, 0);
    else if (actual_style == ATTACK_STYLE_RANGED)
        hit_delay = encounter_ranged_hit_delay(dist, 0);
    /* melee: delay = 0 */

    if (hit_delay == 0) {
        /* melee: instant damage, check prayer now */
        int prayer_matches = encounter_prayer_correct_for_style(s->active_prayer, actual_style);
        if (prayer_matches) { dmg = 0; s->prayer_correct_this_tick = 1; }
        encounter_damage_player(&s->player, dmg, &s->damage_received_this_tick);
    } else {
        /* ranged/magic: queue pending hit on player */
        if (s->player_pending_hit_count < ENCOUNTER_MAX_PENDING_HITS) {
            /* for non-jad NPCs, check prayer at queue time (standard OSRS behavior).
               for jad, check prayer at hit time (gives 3-tick reaction window). */
            int is_jad = (npc->type == INF_NPC_JAD);
            if (!is_jad) {
                int prayer_matches = encounter_prayer_correct_for_style(s->active_prayer, actual_style);
                if (prayer_matches) { dmg = 0; s->prayer_correct_this_tick = 1; }
            }
            EncounterPendingHit* ph = &s->player_pending_hits[s->player_pending_hit_count++];
            ph->active = 1;
            ph->damage = dmg;
            ph->ticks_remaining = hit_delay;
            ph->attack_style = actual_style;
            ph->check_prayer = is_jad ? 1 : 0;
        }
    }

    npc->attacked_this_tick = 1;
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
    if (encounter_rand_int(&s->rng_state, 10) != 0) return;

    /* pick a random dead mob */
    int di = encounter_rand_int(&s->rng_state, s->dead_mob_count);
    InfDeadMob* dm = &s->dead_mobs[di];

    int slot = inf_find_free_npc(s);
    if (slot < 0) return;

    /* spawn near mager */
    int rx = npc->x + encounter_rand_int(&s->rng_state, 3) - 1;
    int ry = npc->y + encounter_rand_int(&s->rng_state, 3) - 1;
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
        int hx = npc->x + encounter_rand_int(&s->rng_state, 5) - 2;
        int hy = npc->y + encounter_rand_int(&s->rng_state, 5) - 2;
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
        /* spawn mager at {20,36} and ranger at {29,36} */
        int m_slot = inf_find_free_npc(s);
        if (m_slot >= 0) inf_init_npc(s, m_slot, INF_NPC_MAGER, 20, 36);
        int r_slot = inf_find_free_npc(s);
        if (r_slot >= 0) inf_init_npc(s, r_slot, INF_NPC_RANGER, 29, 36);

        /* when shield dies, these switch aggro to player (default behavior) */
        s->zuk.set_timer = s->zuk.set_interval;
    }

    /* jad spawn at HP < 480 (with shield still alive) */
    if (!s->zuk.jad_spawned && zuk->hp < 480 &&
        si >= 0 && s->npcs[si].active) {
        s->zuk.jad_spawned = 1;
        int j_slot = inf_find_free_npc(s);
        if (j_slot >= 0) {
            inf_init_npc(s, j_slot, INF_NPC_JAD, 24, 32);
        }
    }

    /* healer spawn at HP < 240: 4 JalMejJak, sets enraged */
    if (!s->zuk.healer_spawned && zuk->hp < 240) {
        s->zuk.healer_spawned = 1;
        s->zuk.enraged = 1;
        static const int healer_pos[4][2] = {
            {16, 48}, {20, 48}, {30, 48}, {34, 48}
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
    /* NPC per-tick flags are cleared in inf_step BEFORE inf_tick_player,
       so player hit flags survive through both tick functions into render_post_tick. */

    /* zuk-specific phases first */
    inf_zuk_tick(s);

    for (int i = 0; i < INF_MAX_NPCS; i++) {
        if (!s->npcs[i].active) continue;

        /* death linger: decrement and deactivate when done */
        if (s->npcs[i].death_ticks > 0) {
            s->npcs[i].death_ticks--;
            if (s->npcs[i].death_ticks == 0) s->npcs[i].active = 0;
            continue;  /* dying NPCs don't move or attack */
        }

        /* decrement ice barrage freeze timer */
        if (s->npcs[i].frozen_ticks > 0) s->npcs[i].frozen_ticks--;

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

#define INF_HEAD_MOVE    0   /* 25: idle + 8 walk + 16 run */
#define INF_HEAD_PRAYER  1   /* 5: no_change, off, melee, range, mage (ENCOUNTER_PRAYER_DIM) */
#define INF_HEAD_TARGET  2   /* INF_MAX_NPCS+1: none or NPC index */
#define INF_HEAD_GEAR    3   /* 5: no_switch, mage, tbow, bp, tank */
#define INF_HEAD_EAT     4   /* 2: none, brew */
#define INF_HEAD_POTION  5   /* 4: none, restore, bastion, stamina */
#define INF_HEAD_SPELL   6   /* 2: blood_barrage, ice_barrage */
#define INF_NUM_ACTION_HEADS 7

static const int INF_ACTION_DIMS[INF_NUM_ACTION_HEADS] = { ENCOUNTER_MOVE_ACTIONS, 5, INF_MAX_NPCS+1, 5, 2, 4, 2 };
#define INF_ACTION_MASK_SIZE (ENCOUNTER_MOVE_ACTIONS + 5 + INF_MAX_NPCS+1 + 5 + 2 + 4 + 2)

/* movement uses shared encounter_move_to_target from osrs_encounter.h */

/* walkability callback for encounter_move_to_target */
static int inf_tile_walkable(void* ctx, int x, int y) {
    InfernoState* s = (InfernoState*)ctx;
    if (!inf_in_arena(x, y)) return 0;
    if (inf_blocked_by_pillar(s, x, y, 1)) return 0;
    if (s->collision_map)
        return collision_tile_walkable(s->collision_map, 0,
            x + s->world_offset_x, y + s->world_offset_y);
    return 1;
}

#define INF_BREW_HEAL     16   /* sara brew heals 16, can overcap to base+16 */
#define INF_RESTORE_PRAY  (7 + 99/4)  /* 31 points */

/* apply NPC death: blob split, mager resurrection store, jad healer cleanup.
   call after reducing npc->hp. checks if hp <= 0 and handles death effects. */
static void inf_apply_npc_death(InfernoState* s, int npc_idx) {
    InfNPC* npc = &s->npcs[npc_idx];
    if (npc->hp > 0 || !npc->active || npc->death_ticks > 0) return;
    /* keep active=1 for death_ticks so renderer shows final hitsplat + death anim.
       inf_tick_npcs decrements death_ticks and sets active=0 when it reaches 0. */
    npc->death_ticks = 2;

    if (npc->type == INF_NPC_BLOB) {
        InfNPCType split_types[3] = {
            INF_NPC_BLOB_MELEE, INF_NPC_BLOB_RANGE, INF_NPC_BLOB_MAGE
        };
        for (int sp = 0; sp < 3; sp++) {
            int slot = inf_find_free_npc(s);
            if (slot < 0) break;
            inf_init_npc(s, slot, split_types[sp], npc->x + (sp - 1), npc->y);
        }
    } else {
        inf_store_dead_mob(s, npc);
    }

    if (npc->type == INF_NPC_JAD) {
        for (int j = 0; j < INF_MAX_NPCS; j++) {
            if (s->npcs[j].active &&
                s->npcs[j].type == INF_NPC_HEALER_JAD &&
                s->npcs[j].jad_owner_idx == npc_idx) {
                s->npcs[j].active = 0;
            }
        }
    }
}

static void inf_tick_player(InfernoState* s, const int* actions) {
    encounter_clear_tick_flags(&s->player);

    /* prayer: uses shared 5-value encoding from osrs_encounter.h */
    encounter_apply_prayer_action(&s->active_prayer, actions[INF_HEAD_PRAYER]);
    /* drain prayer at OSRS rate (protection prayers: ~1 point per 5 ticks with 0 bonus) */
    encounter_drain_prayer(&s->player.current_prayer, &s->active_prayer, 0, &s->prayer_drain_counter);
    s->player.prayer = s->active_prayer;

    /* gear switching */
    int gear_act = actions[INF_HEAD_GEAR];
    if (gear_act >= 1 && gear_act <= 3) {
        /* 1=mage, 2=tbow, 3=bp */
        InfWeaponSet new_set = (InfWeaponSet)(gear_act - 1);
        s->weapon_set = new_set;
        s->armor_tank = 0;
        GearSet gs = (new_set == INF_GEAR_MAGE) ? GEAR_MAGE : GEAR_RANGED;
        encounter_apply_loadout(&s->player, INF_LOADOUTS[new_set], gs);
    } else if (gear_act == 4) {
        /* tank overlay: justiciar head/body/legs on current loadout */
        s->armor_tank = 1;
        s->player.equipped[GEAR_SLOT_HEAD] = INF_TANK_HEAD;
        s->player.equipped[GEAR_SLOT_BODY] = INF_TANK_BODY;
        s->player.equipped[GEAR_SLOT_LEGS] = INF_TANK_LEGS;
    }

    /* auto-detect gear switch from direct inventory equip (human mode).
       gui_inv_click mutates p->equipped directly, bypassing the action head.
       detect weapon mismatch and sync weapon_set + full loadout. */
    {
        uint8_t current_weapon = s->player.equipped[GEAR_SLOT_WEAPON];
        if (current_weapon != INF_LOADOUTS[s->weapon_set][GEAR_SLOT_WEAPON]) {
            for (int g = 0; g < INF_NUM_WEAPON_SETS; g++) {
                if (INF_LOADOUTS[g][GEAR_SLOT_WEAPON] == current_weapon) {
                    s->weapon_set = (InfWeaponSet)g;
                    GearSet gs = (g == INF_GEAR_MAGE) ? GEAR_MAGE : GEAR_RANGED;
                    encounter_apply_loadout(&s->player, INF_LOADOUTS[g], gs);
                    break;
                }
            }
        }
    }

    /* spell choice for mage attacks — normalize to ENCOUNTER_SPELL_*.
       human sends ATTACK_ICE=2 / ATTACK_BLOOD=3, RL sends 0=blood / 1=ice. */
    int spell_act = actions[INF_HEAD_SPELL];
    if (spell_act == ATTACK_ICE || spell_act == 1)
        s->spell_choice = ENCOUNTER_SPELL_ICE;
    else
        s->spell_choice = ENCOUNTER_SPELL_BLOOD;

    /* consumables — shared 3-tick potion timer */
    if (s->player_potion_timer > 0) s->player_potion_timer--;
    if (s->stamina_active_ticks > 0) s->stamina_active_ticks--;

    /* brew (INF_HEAD_EAT): heals 16 HP, can overcap to base+16 */
    int eat_act = actions[INF_HEAD_EAT];
    if (eat_act == 1 && s->player_brew_doses > 0 && s->player_potion_timer == 0) {
        s->player.current_hitpoints += INF_BREW_HEAL;
        if (s->player.current_hitpoints > s->player.base_hitpoints + 16)
            s->player.current_hitpoints = s->player.base_hitpoints + 16;
        s->player_brew_doses--;
        s->player_potion_timer = 3;
        s->player.ate_food_this_tick = 1;
    }

    /* potions (INF_HEAD_POTION): 1=restore, 2=bastion, 3=stamina */
    int pot_act = actions[INF_HEAD_POTION];
    if (pot_act == 1 && s->player_restore_doses > 0 && s->player_potion_timer == 0) {
        s->player.current_prayer += INF_RESTORE_PRAY;
        if (s->player.current_prayer > s->player.base_prayer)
            s->player.current_prayer = s->player.base_prayer;
        s->player_restore_doses--;
        s->player_potion_timer = 3;
    } else if (pot_act == 2 && s->player_bastion_doses > 0 && s->player_potion_timer == 0) {
        /* bastion potion: simplified as flat +6 effective range level (handled in combat) */
        s->player_bastion_doses--;
        s->player_potion_timer = 3;
    } else if (pot_act == 3 && s->player_stamina_doses > 0 && s->player_potion_timer == 0) {
        s->stamina_active_ticks = 200;
        s->player_stamina_doses--;
        s->player_potion_timer = 3;
    }

    /* attack target selection: persistent until NPC dies or player clicks ground.
       target=0 means "no new target this tick" (preserves existing target). */
    int target = actions[INF_HEAD_TARGET];
    int has_new_target = 0;
    if (target > 0 && target <= INF_MAX_NPCS) {
        int npc_idx = target - 1;
        if (s->npcs[npc_idx].active && s->npcs[npc_idx].death_ticks == 0) {
            s->player_attack_target = npc_idx;
            has_new_target = 1;
        }
    }
    /* explicit movement (ground click or RL move) cancels attack target,
       but only if no new target was set this tick. auto-chase movement
       does NOT cancel — only explicit user actions do. */
    int has_explicit_move = (actions[INF_HEAD_MOVE] > 0 || s->player_dest_x >= 0);
    if (!has_new_target && has_explicit_move) {
        s->player_attack_target = -1;
    }
    /* clear target if NPC died or is dying */
    if (s->player_attack_target >= 0 &&
        (!s->npcs[s->player_attack_target].active ||
         s->npcs[s->player_attack_target].death_ticks > 0)) {
        s->player_attack_target = -1;
    }

    /* movement: explicit move, auto-chase toward target, or idle.
       OSRS order: target selection → movement → attack check. */
    int chasing = 0;
    if (has_explicit_move && s->player_attack_target < 0) {
        /* explicit movement (ground click or RL agent) — no attack target */
        if (s->player_dest_x >= 0) {
            encounter_move_toward_dest(&s->player, &s->player_dest_x, &s->player_dest_y,
                s->collision_map, s->world_offset_x, s->world_offset_y,
                inf_tile_walkable, s, inf_pathfind_blocked, s);
        } else {
            int move_act = actions[INF_HEAD_MOVE];
            s->player.is_running = 0;
            if (move_act > 0 && move_act < ENCOUNTER_MOVE_ACTIONS) {
                encounter_move_to_target(&s->player,
                    ENCOUNTER_MOVE_TARGET_DX[move_act], ENCOUNTER_MOVE_TARGET_DY[move_act],
                    inf_tile_walkable, s);
            }
        }
    } else if (s->player_attack_target >= 0) {
        /* auto-chase: pathfind toward attack target when out of range */
        InfNPC* chase_npc = &s->npcs[s->player_attack_target];
        const EncounterLoadoutStats* ls = &s->loadout_stats[s->weapon_set];
        chasing = encounter_chase_attack_target(&s->player,
            chase_npc->x, chase_npc->y, INF_NPC_STATS[chase_npc->type].size,
            ls->attack_range,
            s->collision_map, s->world_offset_x, s->world_offset_y,
            inf_tile_walkable, s, inf_pathfind_blocked, s);
    }

    /* player attacks targeted NPC */
    if (s->player_attack_timer > 0) s->player_attack_timer--;
    if (s->player_attack_target >= 0 && s->player_attack_timer == 0) {
        InfNPC* target_npc = &s->npcs[s->player_attack_target];
        if (target_npc->active) {
            const EncounterLoadoutStats* ls = &s->loadout_stats[s->weapon_set];

            /* range check: compute distance to target NPC, skip if out of range */
            int target_dist = encounter_dist_to_npc(s->player.x, s->player.y,
                target_npc->x, target_npc->y, target_npc->size);

            if (target_dist >= 1 && target_dist <= ls->attack_range) {
                /* compute hit delay for projectile flight */
                int hit_delay;
                if (ls->style == ATTACK_STYLE_MAGIC)
                    hit_delay = encounter_magic_hit_delay(target_dist, 1);
                else
                    hit_delay = encounter_ranged_hit_delay(target_dist, 1);

                int total_dmg = 0;

                if (s->weapon_set == INF_GEAR_MAGE) {
                    /* barrage spells: 3x3 AoE via shared osrs_barrage_resolve.
                       ice barrage: freeze on hit (including 0 dmg), not on splash.
                       blood barrage: heal 25% of total AoE damage (applied when hits land). */
                    int mage_att_roll = ls->eff_level * (ls->attack_bonus + 64);

                    /* build target array: primary target first, then all other active NPCs */
                    BarrageTarget btargets[INF_MAX_NPCS + 1];
                    int bt_count = 0;
                    {
                        const InfNPCStats* ns = &INF_NPC_STATS[target_npc->type];
                        btargets[bt_count++] = (BarrageTarget){
                            .active = 1, .x = target_npc->x, .y = target_npc->y,
                            .def_level = ns->def_level, .magic_def_bonus = ns->magic_def_bonus,
                            .npc_idx = s->player_attack_target, .hit = 0, .damage = 0
                        };
                    }
                    for (int i = 0; i < INF_MAX_NPCS; i++) {
                        if (i == s->player_attack_target || !s->npcs[i].active) continue;
                        const InfNPCStats* ns2 = &INF_NPC_STATS[s->npcs[i].type];
                        btargets[bt_count++] = (BarrageTarget){
                            .active = 1, .x = s->npcs[i].x, .y = s->npcs[i].y,
                            .def_level = ns2->def_level, .magic_def_bonus = ns2->magic_def_bonus,
                            .npc_idx = i, .hit = 0, .damage = 0
                        };
                    }

                    BarrageResult br = osrs_barrage_resolve(
                        btargets, bt_count, mage_att_roll, ls->max_hit, &s->rng_state);
                    total_dmg = br.total_damage;

                    /* queue pending hits on each AoE target (all get same delay from primary distance) */
                    for (int i = 0; i < bt_count; i++) {
                        if (!btargets[i].active || !btargets[i].hit) continue;
                        int nidx = btargets[i].npc_idx;
                        EncounterPendingHit* ph = &s->npcs[nidx].pending_hit;
                        ph->active = 1;
                        ph->damage = btargets[i].damage;
                        ph->ticks_remaining = hit_delay;
                        ph->attack_style = ATTACK_STYLE_MAGIC;
                        ph->check_prayer = 0;
                        ph->spell_type = s->spell_choice;  /* ENCOUNTER_SPELL_ICE or _BLOOD */
                    }

                } else if (s->weapon_set == INF_GEAR_TBOW) {
                    /* tbow: single target, scale by target magic level.
                       ls->max_hit is BASE max hit before tbow scaling. */
                    const InfNPCStats* ns = &INF_NPC_STATS[target_npc->type];
                    int tbow_m = ns->magic_level > ns->magic_def_bonus
                               ? ns->magic_level : ns->magic_def_bonus;
                    if (tbow_m > 250) tbow_m = 250;
                    float acc_mult = osrs_tbow_acc_mult(tbow_m);
                    float dmg_mult = osrs_tbow_dmg_mult(tbow_m);
                    int att_roll = (int)(ls->eff_level * (ls->attack_bonus + 64) * acc_mult);
                    int def_roll = (ns->def_level + 8) * (ns->ranged_def_bonus + 64);
                    int max_hit = (int)(ls->max_hit * dmg_mult);
                    if (encounter_rand_float(&s->rng_state) < osrs_hit_chance(att_roll, def_roll)) {
                        total_dmg = encounter_rand_int(&s->rng_state, max_hit + 1);
                    }
                    EncounterPendingHit* ph = &target_npc->pending_hit;
                    ph->active = 1;
                    ph->damage = total_dmg;
                    ph->ticks_remaining = hit_delay;
                    ph->attack_style = ATTACK_STYLE_RANGED;
                    ph->check_prayer = 0;

                } else {
                    /* blowpipe: single target */
                    const InfNPCStats* ns = &INF_NPC_STATS[target_npc->type];
                    int att_roll = ls->eff_level * (ls->attack_bonus + 64);
                    int def_roll = (ns->def_level + 8) * (ns->ranged_def_bonus + 64);
                    if (encounter_rand_float(&s->rng_state) < osrs_hit_chance(att_roll, def_roll)) {
                        total_dmg = encounter_rand_int(&s->rng_state, ls->max_hit + 1);
                    }
                    EncounterPendingHit* ph = &target_npc->pending_hit;
                    ph->active = 1;
                    ph->damage = total_dmg;
                    ph->ticks_remaining = hit_delay;
                    ph->attack_style = ATTACK_STYLE_RANGED;
                    ph->check_prayer = 0;
                }

                s->player_attack_timer = ls->attack_speed;

                /* player projectile event for renderer */
                s->player_attacked_this_tick = 1;
                s->player_attack_npc_idx = s->player_attack_target;
                s->player_attack_dmg = total_dmg;
                s->player_attack_style_id = ls->style;

                /* player attack animation + spell type for renderer effect system */
                s->player.attack_style_this_tick = ls->style;
                if (s->weapon_set == INF_GEAR_MAGE) {
                    /* 0=none, 1=ice, 2=blood */
                    s->player.magic_type_this_tick = (s->spell_choice == ENCOUNTER_SPELL_ICE) ? 1 : 2;
                }
            }
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

    float r = 0.0f;

    /* wave completion: scales with wave number.
     * wave 1 = 0.01, wave 69 = 0.69. total ≈ 24 if all cleared. */
    if (s->wave_completed_this_tick) {
        r += 0.01f * (float)(s->wave + 1);
        s->total_waves_cleared = s->wave + 1;
    }

    /* damage dealt: reward for hitting monsters.
     * a 45-damage hit gives 0.045. this is the primary learning signal
     * since the agent needs to discover that targeting + attacking = good. */
    if (s->damage_dealt_this_tick > 0.0f)
        r += 0.02f * (s->damage_dealt_this_tick / 50.0f);

    /* idle penalty: if not attacking anything for 10+ ticks, small penalty.
     * pushes the agent toward combat rather than hiding behind pillars. */
    if (s->ticks_without_action > 10)
        r -= 0.005f;

    /* pillar destroyed: north pillar (idx 2, highest Y) is the most important
     * safespot, south (0) and west (1) are less critical. total = -0.8 for all. */
    if (s->pillar_lost_this_tick >= 0) {
        r -= (s->pillar_lost_this_tick == 2) ? 0.4f : 0.2f;
    }

    /* accumulate diagnostic stats */
    s->total_damage_dealt += s->damage_dealt_this_tick;
    s->total_damage_received += s->damage_received_this_tick;

    return r;
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
    s->pillar_lost_this_tick = -1;
    s->player_attacked_this_tick = 0;
    /* clear NPC per-tick flags BEFORE player actions, so hit flags set by
       inf_tick_player survive through inf_tick_npcs into render_post_tick */
    for (int i = 0; i < INF_MAX_NPCS; i++) {
        s->npcs[i].attacked_this_tick = 0;
        s->npcs[i].moved_this_tick = 0;
        s->npcs[i].hit_landed_this_tick = 0;
        s->npcs[i].hit_damage = 0;
    }
    s->tick++;

    /* initial wave spawn delay */
    if (s->wave_spawn_delay > 0) {
        s->wave_spawn_delay--;
        if (s->wave_spawn_delay == 0) {
            inf_spawn_wave(s);
        }
        /* player can still move/pray during delay */
        inf_tick_player(s, actions);
        s->reward = inf_compute_reward(s);
        return;
    }

    /* player actions */
    inf_tick_player(s, actions);

    /* ------------------------------------------------------------------ */
    /* process pending hits: NPC pending hits (player attacks landing)     */
    /* runs BEFORE inf_tick_npcs so that ice barrage freeze takes effect   */
    /* on the same tick the projectile lands (NPC can't act while frozen). */
    /* ------------------------------------------------------------------ */
    {
        int blood_heal_acc = 0;
        for (int i = 0; i < INF_MAX_NPCS; i++) {
            if (!s->npcs[i].active || s->npcs[i].death_ticks > 0) continue;
            int landed = encounter_resolve_npc_pending_hit(
                &s->npcs[i].pending_hit,
                &s->npcs[i].hp, &s->npcs[i].hit_landed_this_tick, &s->npcs[i].hit_damage,
                &s->npcs[i].frozen_ticks, &blood_heal_acc, &s->damage_dealt_this_tick);
            if (landed) inf_apply_npc_death(s, i);
        }
        if (blood_heal_acc > 0) {
            s->player.current_hitpoints += blood_heal_acc / 4;
            if (s->player.current_hitpoints > s->player.base_hitpoints)
                s->player.current_hitpoints = s->player.base_hitpoints;
        }
    }

    /* NPC AI: runs after pending hits so ice barrage freeze is already active */
    inf_tick_npcs(s);

    /* ------------------------------------------------------------------ */
    /* process pending hits: player pending hits (NPC attacks landing)     */
    /* ------------------------------------------------------------------ */
    encounter_resolve_player_pending_hits(
        s->player_pending_hits, &s->player_pending_hit_count,
        &s->player, s->active_prayer,
        &s->damage_received_this_tick, &s->prayer_correct_this_tick);

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
    s->episode_return += s->reward;
}

/* ======================================================================== */
/* observations                                                              */
/* ======================================================================== */

/* obs layout: 19 player + 6 pillar + 32 NPCs * 11 = 377. round up to 380. */
#define INF_NUM_OBS 380

static void inf_write_obs(EncounterState* state, float* obs) {
    InfernoState* s = (InfernoState*)state;
    memset(obs, 0, INF_NUM_OBS * sizeof(float));
    int i = 0;

    /* player state (19 features) */
    obs[i++] = (float)s->player.current_hitpoints / 99.0f;
    obs[i++] = (float)(s->player.x - INF_ARENA_MIN_X) / (float)INF_ARENA_WIDTH;
    obs[i++] = (float)(s->player.y - INF_ARENA_MIN_Y) / (float)INF_ARENA_HEIGHT;
    obs[i++] = (s->active_prayer == PRAYER_PROTECT_MELEE) ? 1.0f : 0.0f;
    obs[i++] = (s->active_prayer == PRAYER_PROTECT_RANGED) ? 1.0f : 0.0f;
    obs[i++] = (s->active_prayer == PRAYER_PROTECT_MAGIC) ? 1.0f : 0.0f;
    obs[i++] = (float)s->player_brew_doses / 32.0f;
    obs[i++] = (float)s->player_restore_doses / 40.0f;
    obs[i++] = (float)s->player.current_prayer / 99.0f;
    obs[i++] = (float)s->wave / (float)INF_NUM_WAVES;
    obs[i++] = (float)s->tick / (float)INF_MAX_TICKS;
    /* weapon_set one-hot (3 floats) */
    obs[i++] = (s->weapon_set == INF_GEAR_MAGE) ? 1.0f : 0.0f;
    obs[i++] = (s->weapon_set == INF_GEAR_TBOW) ? 1.0f : 0.0f;
    obs[i++] = (s->weapon_set == INF_GEAR_BP) ? 1.0f : 0.0f;
    /* armor_tank */
    obs[i++] = s->armor_tank ? 1.0f : 0.0f;
    /* consumable doses */
    obs[i++] = (float)s->player_bastion_doses / 4.0f;
    obs[i++] = (float)s->player_stamina_doses / 4.0f;
    obs[i++] = (s->stamina_active_ticks > 0) ? 1.0f : 0.0f;
    /* potion cooldown */
    obs[i++] = (float)s->player_potion_timer / 3.0f;

    /* pillars (6 features) */
    for (int p = 0; p < INF_NUM_PILLARS; p++) {
        obs[i++] = s->pillars[p].active ? 1.0f : 0.0f;
        obs[i++] = (float)s->pillars[p].hp / (float)INF_PILLAR_HP;
    }

    /* NPCs: 11 features each, up to INF_MAX_NPCS */
    for (int n = 0; n < INF_MAX_NPCS && (i + 11) <= INF_NUM_OBS; n++) {
        InfNPC* npc = &s->npcs[n];
        if (npc->active) {
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
            obs[i++] = (float)npc->frozen_ticks / 32.0f;
        } else {
            for (int j = 0; j < 11; j++) obs[i++] = 0.0f;
        }
    }

    /* pad to INF_NUM_OBS */
    while (i < INF_NUM_OBS) obs[i++] = 0.0f;
}

static void inf_write_mask(EncounterState* state, float* mask) {
    InfernoState* s = (InfernoState*)state;
    int offset = 0;

    /* HEAD_MOVE (25): idle always valid, walk/run valid if target tile reachable */
    mask[offset++] = 1.0f;  /* idle always valid */
    for (int d = 1; d < ENCOUNTER_MOVE_ACTIONS; d++) {
        int nx = s->player.x + ENCOUNTER_MOVE_TARGET_DX[d];
        int ny = s->player.y + ENCOUNTER_MOVE_TARGET_DY[d];
        mask[offset++] = (inf_in_arena(nx, ny) && !inf_blocked_by_pillar(s, nx, ny, 1))
                         ? 1.0f : 0.0f;
    }

    /* HEAD_PRAYER (5): 0=no change (always valid), 1-4=switch (mask out current) */
    mask[offset++] = 1.0f;  /* no change — always valid */
    mask[offset++] = (s->active_prayer != PRAYER_NONE) ? 1.0f : 0.0f;
    mask[offset++] = (s->active_prayer != PRAYER_PROTECT_MELEE) ? 1.0f : 0.0f;
    mask[offset++] = (s->active_prayer != PRAYER_PROTECT_RANGED) ? 1.0f : 0.0f;
    mask[offset++] = (s->active_prayer != PRAYER_PROTECT_MAGIC) ? 1.0f : 0.0f;

    /* HEAD_TARGET (INF_MAX_NPCS+1): none always valid, NPC valid only if alive (not dying) */
    mask[offset++] = 1.0f;  /* no target */
    for (int n = 0; n < INF_MAX_NPCS; n++) {
        mask[offset++] = (s->npcs[n].active && s->npcs[n].death_ticks == 0) ? 1.0f : 0.0f;
    }

    /* HEAD_GEAR (5): no_switch, mage, tbow, bp, tank */
    mask[offset++] = 1.0f;  /* no_switch always valid */
    mask[offset++] = (s->weapon_set != INF_GEAR_MAGE || s->armor_tank) ? 1.0f : 0.0f;
    mask[offset++] = (s->weapon_set != INF_GEAR_TBOW || s->armor_tank) ? 1.0f : 0.0f;
    mask[offset++] = (s->weapon_set != INF_GEAR_BP || s->armor_tank) ? 1.0f : 0.0f;
    mask[offset++] = 1.0f;  /* tank toggle always allowed */

    /* HEAD_EAT (2): none, brew */
    mask[offset++] = 1.0f;  /* none always valid */
    {
        int brew_hp_room = (s->player.base_hitpoints + 16) - s->player.current_hitpoints;
        mask[offset++] = (s->player_brew_doses > 0 &&
                          s->player_potion_timer == 0 &&
                          brew_hp_room >= INF_BREW_HEAL / 2)
                         ? 1.0f : 0.0f;
    }

    /* HEAD_POTION (4): none, restore, bastion, stamina */
    mask[offset++] = 1.0f;  /* none always valid */
    /* restore: mask if no doses, timer active, or would waste more than half */
    {
        int pray_missing = s->player.base_prayer - s->player.current_prayer;
        mask[offset++] = (s->player_restore_doses > 0 &&
                          s->player_potion_timer == 0 &&
                          pray_missing >= (INF_RESTORE_PRAY + 1) / 2)
                         ? 1.0f : 0.0f;
    }
    /* bastion: mask if no doses or timer active */
    mask[offset++] = (s->player_bastion_doses > 0 && s->player_potion_timer == 0)
                     ? 1.0f : 0.0f;
    /* stamina: mask if no doses, timer active, or already active */
    mask[offset++] = (s->player_stamina_doses > 0 &&
                      s->player_potion_timer == 0 &&
                      s->stamina_active_ticks == 0)
                     ? 1.0f : 0.0f;

    /* HEAD_SPELL (2): blood_barrage, ice_barrage — masked when not in mage gear */
    mask[offset++] = (s->weapon_set == INF_GEAR_MAGE) ? 1.0f : 0.0f;
    mask[offset++] = (s->weapon_set == INF_GEAR_MAGE) ? 1.0f : 0.0f;
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
    /* only index 0 (player) returns a valid Player*.
     * NPC indices can't return Player* since InfNPC is a different struct.
     * GUI/human input code must NULL-check. */
    if (index == 0) return &s->player;
    return NULL;
}

/* render entity population */
static void inf_fill_render_entities(EncounterState* state, RenderEntity* out, int max_entities, int* count) {
    InfernoState* s = (InfernoState*)state;
    int n = 0;

    /* sync all consumable counts to Player struct so GUI inventory can read them */
    s->player.food_count = 0;
    s->player.brew_doses = s->player_brew_doses;
    s->player.restore_doses = s->player_restore_doses;
    s->player.combat_potion_doses = s->player_bastion_doses;
    s->player.ranged_potion_doses = s->player_stamina_doses;

    /* index 0: the player */
    if (n < max_entities) {
        render_entity_from_player(&s->player, &out[n++]);
    }

    /* active NPCs: manually fill since InfNPC is not a Player */
    for (int i = 0; i < INF_MAX_NPCS && n < max_entities; i++) {
        InfNPC* npc = &s->npcs[i];
        if (!npc->active) continue;

        RenderEntity* re = &out[n++];
        memset(re, 0, sizeof(RenderEntity));
        memset(re->equipped, ITEM_NONE, NUM_GEAR_SLOTS);
        re->entity_type = ENTITY_NPC;
        re->npc_def_id = INF_NPC_DEF_IDS[npc->type];
        re->npc_slot = i;
        re->attack_target_entity_idx = -1;
        re->npc_visible = npc->active;
        re->npc_size = npc->size;
        {
            const NpcModelMapping* nm = npc_model_lookup(INF_NPC_DEF_IDS[npc->type]);
            if (npc->death_ticks > 0) {
                /* dying: hold idle pose while hitsplat + health bar display */
                re->npc_anim_id = nm ? (int)nm->idle_anim : -1;
            } else if (npc->attacked_this_tick && nm && nm->attack_anim != 65535) {
                re->npc_anim_id = (int)nm->attack_anim;
            } else if (npc->moved_this_tick && nm && nm->walk_anim != 65535) {
                re->npc_anim_id = (int)nm->walk_anim;
            } else {
                re->npc_anim_id = nm ? (int)nm->idle_anim : -1;
            }
        }
        re->x = npc->x;
        re->y = npc->y;
        re->dest_x = npc->target_x;
        re->dest_y = npc->target_y;
        re->current_hitpoints = npc->hp;
        re->base_hitpoints = npc->max_hp;
        re->attack_style_this_tick = npc->attacked_this_tick
            ? (AttackStyle)npc->attack_style : ATTACK_STYLE_NONE;
        re->hit_landed_this_tick = npc->hit_landed_this_tick;
        re->hit_damage = npc->hit_damage;
        /* barrage hits that pass accuracy are queued; splashes never enter the queue.
           so any NPC with hit_landed_this_tick from a pending hit was a successful hit. */
        re->hit_was_successful = npc->hit_landed_this_tick;
    }

    encounter_resolve_attack_target(out, n, s->player_attack_target);
    *count = n;
}

static void inf_put_int(EncounterState* state, const char* key, int value) {
    InfernoState* s = (InfernoState*)state;
    if (strcmp(key, "start_wave") == 0) s->start_wave = value;
    else if (strcmp(key, "seed") == 0) s->rng_state = (uint32_t)value;
    else if (strcmp(key, "world_offset_x") == 0) s->world_offset_x = value;
    else if (strcmp(key, "world_offset_y") == 0) s->world_offset_y = value;
    else if (strcmp(key, "player_dest_x") == 0) s->player_dest_x = value;
    else if (strcmp(key, "player_dest_y") == 0) s->player_dest_y = value;
}

static void inf_put_float(EncounterState* state, const char* key, float value) {
    (void)state; (void)key; (void)value;
}

static void inf_put_ptr(EncounterState* state, const char* key, void* value) {
    InfernoState* s = (InfernoState*)state;
    if (strcmp(key, "collision_map") == 0) s->collision_map = (const CollisionMap*)value;
}

static int inf_get_tick(EncounterState* state) {
    return ((InfernoState*)state)->tick;
}

static int inf_get_winner(EncounterState* state) {
    return ((InfernoState*)state)->winner;
}

static void* inf_get_log(EncounterState* state) {
    InfernoState* s = (InfernoState*)state;
    if (s->episode_over) {
        s->log.episode_return += s->episode_return;
        s->log.episode_length += (float)s->tick;
        s->log.wins += (s->winner == 0) ? 1.0f : 0.0f;
        s->log.damage_dealt += s->total_damage_dealt;
        s->log.damage_received += s->total_damage_received;
        s->log.n += 1.0f;
    }
    return &s->log;
}

/* ======================================================================== */
/* render post-tick: populate overlay projectiles for renderer               */
/* ======================================================================== */


static void inf_render_post_tick(EncounterState* state, EncounterOverlay* ov) {
    InfernoState* s = (InfernoState*)state;
    ov->projectile_count = 0;

    /* NPC attack projectiles — per-NPC-type flight parameters */
    for (int i = 0; i < INF_MAX_NPCS; i++) {
        InfNPC* npc = &s->npcs[i];
        if (!npc->active || !npc->attacked_this_tick) continue;
        if (ov->projectile_count >= ENCOUNTER_MAX_OVERLAY_PROJECTILES) break;

        /* nibblers attack pillars, not worth showing as projectile */
        if (npc->type == INF_NPC_NIBBLER) continue;

        const InfNPCStats* stats = &INF_NPC_STATS[npc->type];
        int actual_style = stats->default_style;

        /* jad uses its per-attack random style */
        if (npc->type == INF_NPC_JAD)
            actual_style = npc->jad_attack_style;

        /* zuk is typeless — show as magic for visual purposes */
        if (npc->type == INF_NPC_ZUK)
            actual_style = ATTACK_STYLE_MAGIC;

        int proj_style = encounter_attack_style_to_proj_style(actual_style);
        int npc_size = stats->size;
        int start_h = (int)(npc_size * 0.75f * 128);
        int end_h = 64;  /* player: size 1 * 0.5 * 128 */
        int dist = encounter_dist_to_npc(s->player.x, s->player.y,
            npc->x, npc->y, npc_size);
        int hit_delay;
        if (actual_style == ATTACK_STYLE_MAGIC)
            hit_delay = encounter_magic_hit_delay(dist, 0);
        else
            hit_delay = encounter_ranged_hit_delay(dist, 0);
        int duration = hit_delay * 30;
        int curve = 16;
        float arc = 0.0f;
        int tracks = 1;

        /* per-NPC-type projectile GFX model ID */
        uint32_t proj_model_id = 0;
        switch (npc->type) {
            case INF_NPC_BAT:        proj_model_id = INF_GFX_1374_MODEL; break;
            case INF_NPC_BLOB:       proj_model_id = (actual_style == ATTACK_STYLE_RANGED) ? INF_GFX_1383_MODEL : INF_GFX_1384_MODEL; break;
            case INF_NPC_BLOB_RANGE: proj_model_id = INF_GFX_1383_MODEL; break;
            case INF_NPC_BLOB_MAGE:  proj_model_id = INF_GFX_1384_MODEL; break;
            case INF_NPC_BLOB_MELEE: proj_model_id = INF_GFX_1382_MODEL; break;
            case INF_NPC_RANGER:     proj_model_id = INF_GFX_1377_MODEL; break;
            case INF_NPC_MAGER:      proj_model_id = INF_GFX_1379_MODEL; break;
            case INF_NPC_JAD:
                proj_model_id = (actual_style == ATTACK_STYLE_MAGIC) ? INF_GFX_448_MODEL : INF_GFX_447_MODEL;
                break;
            case INF_NPC_ZUK:        proj_model_id = INF_GFX_1375_MODEL; break;
            case INF_NPC_HEALER_ZUK: proj_model_id = INF_GFX_1385_MODEL; break;
            default: break;
        }

        /* NPC-specific flight overrides */
        switch (npc->type) {
            case INF_NPC_MAGER:
                duration += 60;  /* visual delay ~2 ticks */
                break;
            case INF_NPC_RANGER:
                duration += 60;  /* visual delay ~2 ticks + slower travel */
                break;
            case INF_NPC_JAD:
                if (actual_style == ATTACK_STYLE_MAGIC) {
                    arc = 1.0f;  /* arcing magic projectile */
                }
                break;
            case INF_NPC_HEALER_ZUK:
                arc = 3.0f;      /* high arcing spark */
                duration = 4 * 30;  /* fixed 4 tick delay */
                break;
            case INF_NPC_ZUK:
                duration = 4 * 30;  /* fixed 4 tick delay */
                break;
            default: break;
        }

        encounter_emit_projectile(ov,
            npc->x, npc->y, s->player.x, s->player.y,
            proj_style, (int)s->damage_received_this_tick,
            duration, start_h, end_h, curve, arc, tracks, npc_size, proj_model_id);
    }

    /* player attack projectile (ranged/magic only — melee has no projectile) */
    if (s->player_attacked_this_tick &&
        s->player_attack_style_id != ATTACK_STYLE_MELEE &&
        ov->projectile_count < ENCOUNTER_MAX_OVERLAY_PROJECTILES) {
        int target_idx = s->player_attack_npc_idx;
        if (target_idx >= 0 && target_idx < INF_MAX_NPCS) {
            InfNPC* target = &s->npcs[target_idx];
            int target_size = INF_NPC_STATS[target->type].size;
            int p_start_h = 64;   /* player: size 1 * 0.5 * 128 */
            int p_end_h = (int)(target_size * 0.5f * 128);
            int p_dist = encounter_dist_to_npc(s->player.x, s->player.y,
                target->x, target->y, target_size);
            int p_style = encounter_attack_style_to_proj_style(s->player_attack_style_id);
            float p_arc = 0.0f;
            int p_tracks = 0;  /* don't track — tracking loop targets entity 0 (player) */
            int p_duration;

            uint32_t player_proj_model = 0;
            if (s->weapon_set == INF_GEAR_MAGE) {
                p_duration = encounter_magic_hit_delay(p_dist, 1) * 30;
                p_arc = 0.0f;
                /* barrage: no projectile model (effect system handles it) */
            } else if (s->weapon_set == INF_GEAR_TBOW) {
                p_duration = encounter_ranged_hit_delay(p_dist, 1) * 30;
                p_arc = 1.0f;
                player_proj_model = 3136;  /* rune arrow (GFX 15) — dragon arrow visually similar */
            } else {
                /* blowpipe */
                p_duration = encounter_ranged_hit_delay(p_dist, 1) * 30;
                p_arc = 0.5f;
                player_proj_model = 26379;  /* dragon dart */
            }

            /* barrage has no in-flight projectile in OSRS (only hit splash) */
            if (player_proj_model > 0) {
                encounter_emit_projectile(ov,
                    s->player.x, s->player.y, target->x, target->y,
                    p_style, s->player_attack_dmg,
                    p_duration, p_start_h, p_end_h, 16, p_arc, p_tracks, 1, player_proj_model);
            }
        }
    }
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
    .fill_render_entities = inf_fill_render_entities,

    .put_int = inf_put_int,
    .put_float = inf_put_float,
    .put_ptr = inf_put_ptr,

    .arena_base_x = INF_ARENA_MIN_X,
    .arena_base_y = INF_ARENA_MIN_Y,
    .arena_width = INF_ARENA_WIDTH,
    .arena_height = INF_ARENA_HEIGHT,

    .render_post_tick = inf_render_post_tick,
    .get_log = inf_get_log,
    .get_tick = inf_get_tick,
    .get_winner = inf_get_winner,

    .head_move = INF_HEAD_MOVE,
    .head_prayer = INF_HEAD_PRAYER,
    .head_target = INF_HEAD_TARGET,
    .head_gear = INF_HEAD_GEAR,
    .head_eat = INF_HEAD_EAT,
    .head_potion = INF_HEAD_POTION,
    .head_spell = INF_HEAD_SPELL,
    .head_spec = -1,
    .head_attack = -1,
};

__attribute__((constructor))
static void inf_register(void) {
    encounter_register(&ENCOUNTER_INFERNO);
}

#endif /* ENCOUNTER_INFERNO_H */
