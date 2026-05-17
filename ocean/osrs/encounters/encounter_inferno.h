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

#include "../osrs_types.h"
#include "../osrs_items.h"
#include "../osrs_monsters_generated.h"
#include "../osrs_collision.h"
#include "../osrs_combat.h"
#include "../osrs_special_attacks.h"
#include "../osrs_pvp_gear.h"
#include "../osrs_encounter.h"
#include "../osrs_interaction.h"
#include "../data/npc_models.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdarg.h>
#include <ctype.h>
#include <errno.h>


#define INF_ARENA_MIN_X    11
#define INF_ARENA_MAX_X    39
#define INF_ARENA_MIN_Y    14
#define INF_ARENA_MAX_Y    43
#define INF_ARENA_WIDTH    (INF_ARENA_MAX_X - INF_ARENA_MIN_X + 1)  /* 29 */
#define INF_ARENA_HEIGHT   (INF_ARENA_MAX_Y - INF_ARENA_MIN_Y + 1)  /* 30 */

#define INF_PLAYER_START_X 25
#define INF_PLAYER_START_Y 16
#define INF_ZUK_PLAYER_START_X 25
#define INF_ZUK_PLAYER_START_Y 42
#define INF_ZUK_X 22
#define INF_ZUK_Y 49
#define INF_ZUK_SHIELD_X 23
#define INF_ZUK_SHIELD_Y 44

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
#define INF_WAVE_ZUK      (INF_NUM_WAVES - 1)
#define INF_START_WAVE_ZUK_JAD      (INF_WAVE_ZUK + 1)
#define INF_START_WAVE_ZUK_HEALERS  (INF_WAVE_ZUK + 2)
#define INF_MAX_PUBLIC_START_WAVE   (INF_NUM_WAVES + 2)
#define INF_NUM_ACTION_HEADS 9


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
    [INF_NPC_BLOB_MELEE] = 7696,  /* Jal-AkRek-Ket (melee split) */
    [INF_NPC_BLOB_RANGE] = 7695,  /* Jal-AkRek-Xil (range split) */
    [INF_NPC_BLOB_MAGE]  = 7694,  /* Jal-AkRek-Mej (mage split) */
    [INF_NPC_MELEER]     = 7697,  /* Jal-ImKot */
    [INF_NPC_RANGER]     = 7698,  /* Jal-Xil */
    [INF_NPC_MAGER]      = 7699,  /* Jal-Zek */
    [INF_NPC_JAD]        = 7700,  /* JalTok-Jad */
    [INF_NPC_ZUK]        = 7706,  /* TzKal-Zuk */
    [INF_NPC_HEALER_JAD] = 7701,  /* Yt-HurKot */
    [INF_NPC_HEALER_ZUK] = 7708,  /* Jal-MejJak */
    [INF_NPC_ZUK_SHIELD] = 7707,  /* Ancestral Glyph */
};

static const char* inf_npc_type_name(int type) {
    switch (type) {
        case INF_NPC_NIBBLER: return "Jal-Nib";
        case INF_NPC_BAT: return "Jal-MejRah";
        case INF_NPC_BLOB: return "Jal-Ak";
        case INF_NPC_BLOB_MELEE: return "Jal-AkRek-Ket";
        case INF_NPC_BLOB_RANGE: return "Jal-AkRek-Xil";
        case INF_NPC_BLOB_MAGE: return "Jal-AkRek-Mej";
        case INF_NPC_MELEER: return "Jal-ImKot";
        case INF_NPC_RANGER: return "Jal-Xil";
        case INF_NPC_MAGER: return "Jal-Zek";
        case INF_NPC_JAD: return "JalTok-Jad";
        case INF_NPC_ZUK: return "TzKal-Zuk";
        case INF_NPC_HEALER_JAD: return "Yt-HurKot";
        case INF_NPC_HEALER_ZUK: return "Jal-MejJak";
        case INF_NPC_ZUK_SHIELD: return "Ancestral Glyph";
        default: return "unknown";
    }
}

typedef struct {
    int hp;
    int attack_speed;
    int attack_range;
    int size;
    int default_style;   /* ATTACK_STYLE_* */
    int melee_style;     /* MELEE_STYLE_STAB/SLASH/CRUSH for incoming defence bonus selection */
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

/* encounter-specific fields not in the generated monster database */
typedef struct {
    int attack_range;
    int default_style;   /* ATTACK_STYLE_* */
    int melee_style;     /* MELEE_STYLE_STAB/SLASH/CRUSH for incoming defence bonus selection */
    int can_melee;       /* 1 if can switch to melee when close */
    int magic_base_dmg;  /* base spell damage (magicMaxHit() in InfernoTrainer) */
    int magic_dmg_pct;   /* magic damage multiplier as % (100 = 1.0x) */
    int max_hit_cap;     /* 0 = no cap, >0 = clamp formula result to this */
    int stun_on_spawn;   /* ticks of stun when first spawned */
    int can_move;        /* 0 = cannot move (zuk, zuk healers) */
} InfNPCOverlay;

/* maps InfNPCType -> MonsterIndex for MONSTER_DATABASE lookup */
static const MonsterIndex INF_NPC_TO_MON[INF_NUM_NPC_TYPES] = {
    [INF_NPC_NIBBLER]    = MON_JAL_NIB,
    [INF_NPC_BAT]        = MON_JAL_MEJRAH,
    [INF_NPC_BLOB]       = MON_JAL_AK,
    [INF_NPC_BLOB_MELEE] = MON_JAL_AKREK_KET,
    [INF_NPC_BLOB_RANGE] = MON_JAL_AKREK_XIL,
    [INF_NPC_BLOB_MAGE]  = MON_JAL_AKREK_MEJ,
    [INF_NPC_MELEER]     = MON_JAL_IMKOT,
    [INF_NPC_RANGER]     = MON_JAL_XIL,
    [INF_NPC_MAGER]      = MON_JAL_ZEK,
    [INF_NPC_JAD]        = MON_JALTOK_JAD,
    [INF_NPC_ZUK]        = MON_TZKAL_ZUK,
    [INF_NPC_HEALER_JAD] = MON_YT_HURKOT,
    [INF_NPC_HEALER_ZUK] = MON_JAL_MEJJAK,
    [INF_NPC_ZUK_SHIELD] = MON_ZUK_SHIELD,
};

/* encounter-specific overlay: fields the generated DB doesn't cover */
static const InfNPCOverlay INF_NPC_OVERLAY[INF_NUM_NPC_TYPES] = {
    [INF_NPC_NIBBLER]    = { 1,  ATTACK_STYLE_MELEE,  MELEE_STYLE_CRUSH, 0,   0,   0, 0, 1, 1 },
    [INF_NPC_BAT]        = { 4,  ATTACK_STYLE_RANGED, MELEE_STYLE_STAB,  0,   0,   0, 0, 0, 1 },
    [INF_NPC_BLOB]       = { 15, ATTACK_STYLE_MAGIC,  MELEE_STYLE_CRUSH, 1,  29, 100, 0, 0, 1 },
    [INF_NPC_BLOB_MELEE] = { 1,  ATTACK_STYLE_MELEE,  MELEE_STYLE_CRUSH, 0,   0,   0, 0, 0, 1 },
    [INF_NPC_BLOB_RANGE] = { 15, ATTACK_STYLE_RANGED, MELEE_STYLE_STAB,  0,   0,   0, 0, 0, 1 },
    [INF_NPC_BLOB_MAGE]  = { 15, ATTACK_STYLE_MAGIC,  MELEE_STYLE_STAB,  0,  18, 100, 0, 0, 1 },
    [INF_NPC_MELEER]     = { 1,  ATTACK_STYLE_MELEE,  MELEE_STYLE_SLASH, 0,   0,   0, 0, 0, 1 },
    [INF_NPC_RANGER]     = { 15, ATTACK_STYLE_RANGED, MELEE_STYLE_CRUSH, 1,   0,   0, 0, 0, 1 },
    [INF_NPC_MAGER]      = { 15, ATTACK_STYLE_MAGIC,  MELEE_STYLE_STAB,  1,  70, 100, 0, 0, 1 },
    [INF_NPC_JAD]        = { 50, ATTACK_STYLE_RANGED, MELEE_STYLE_STAB,  1, 113, 100, 113, 0, 1 },
    [INF_NPC_ZUK]        = { 99, ATTACK_STYLE_MAGIC,  MELEE_STYLE_STAB,  0, 148, 100, 0, 8, 0 },
    [INF_NPC_HEALER_JAD] = { 1,  ATTACK_STYLE_MELEE,  MELEE_STYLE_CRUSH, 0,   0,   0, 0, 1, 1 },  /* stun_on_spawn=1 per YtHurKot.ts:50 */
    [INF_NPC_HEALER_ZUK] = { 99, ATTACK_STYLE_MAGIC,  MELEE_STYLE_STAB,  0,  10, 100, 0, 1, 0 },  /* stun_on_spawn=1 per InfernoTrainer JalMejJak.ts SPAWN_DELAY */
    [INF_NPC_ZUK_SHIELD] = { 0,  ATTACK_STYLE_NONE,   MELEE_STYLE_STAB,  0,   0,   0, 0, 1, 0 },
};

/* populated at startup by inf_build_npc_stats() */
static InfNPCStats INF_NPC_STATS[INF_NUM_NPC_TYPES];

/* merge MONSTER_DATABASE + INF_NPC_OVERLAY into INF_NPC_STATS */
static void inf_build_npc_stats(void) {
    for (int i = 0; i < INF_NUM_NPC_TYPES; i++) {
        const MonsterStats* m = &MONSTER_DATABASE[INF_NPC_TO_MON[i]];
        const InfNPCOverlay* o = &INF_NPC_OVERLAY[i];
        InfNPCStats* s = &INF_NPC_STATS[i];

        /* from generated monster DB */
        s->hp              = m->hp;
        s->attack_speed    = m->attack_speed;
        s->size            = m->size;
        s->att_level       = m->att_level;
        s->str_level       = m->str_level;
        s->def_level       = m->def_level;
        s->range_level     = m->range_level;
        s->magic_level     = m->magic_level;
        s->melee_att_bonus = m->melee_att_bonus;
        s->range_att_bonus = m->range_att_bonus;
        s->magic_att_bonus = m->magic_att_bonus;
        s->melee_str_bonus = m->melee_str_bonus;
        s->ranged_str_bonus = m->ranged_str_bonus;
        s->stab_def        = m->stab_def;
        s->slash_def       = m->slash_def;
        s->crush_def       = m->crush_def;
        s->magic_def_bonus = m->magic_def;    /* name mapping */
        s->ranged_def_bonus = m->ranged_def;  /* name mapping */

        /* from encounter overlay */
        s->attack_range    = o->attack_range;
        s->default_style   = o->default_style;
        s->melee_style     = o->melee_style;
        s->can_melee       = o->can_melee;
        s->magic_base_dmg  = o->magic_base_dmg;
        s->magic_dmg_pct   = o->magic_dmg_pct;
        s->max_hit_cap     = o->max_hit_cap;
        s->stun_on_spawn   = o->stun_on_spawn;
        s->can_move        = o->can_move;
    }
}


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


/* max active NPCs: wave 62 has 9 + blob splits (3 per blob, up to 2 blobs = 6) + healers */
#define INF_MAX_NPCS      32
#define INF_OBS_NPCS      37
#define INF_START_READY_TICKS 6

/* dead mob store for mager resurrection */
#define INF_MAX_DEAD_MOBS 16

typedef struct {
    InfNPCType type;
    int x, y;
    int hp, max_hp;
} InfDeadMob;

/* 4 zuk healers × 3 sparks per volley × 2 overlapping volleys = 24.
   cap is 32 for headroom so volleys don't silently drop sparks. */
#define INF_MAX_PENDING_SPARKS 32

typedef struct {
    int active;
    int src_x, src_y;
    int x, y;
    int damage;
    int ticks_remaining;
    int visual_emitted;
} InfPendingSpark;

#define INF_MAX_NPC_TARGET_HITS 16

typedef struct {
    int active;
    int target_idx;
    int damage;
    int ticks_remaining;
} InfNpcTargetHit;

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
    int had_los_last_tick; /* blob: previous-tick LOS latch for immediate scans on LOS gain */

    /* jad state */
    int jad_attack_style;  /* jad: committed next ranged/magic style preview, or NONE if unknown */
    int jad_healer_spawned; /* jad: 1 if healers have been spawned */
    int jad_owner_idx;     /* healer: which jad this healer belongs to (-1 = none) */

    /* mager resurrection state */
    int resurrect_cooldown; /* mager: ticks until next resurrection attempt */
    int resurrection_count; /* number of times this original mob has been resurrected */
    int resurrecting_this_tick;
    int resurrection_visual_target;

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

    /* aggro target: -1 = player (default), >= 0 = NPC index.
       used for set→shield targeting and healer→zuk healing. */
    int aggro_target;

    /* per-tick render flags (cleared at start of each tick) */
    int attacked_this_tick;     /* 1 when NPC attacks this tick */
    int attack_style_this_tick; /* actual style that fired this tick */
    int attack_visual_target;   /* NPC index this attack visually targets (-1 = player) */
    int moved_this_tick;        /* 1 when NPC moves this tick */
    int hit_landed_this_tick; /* 1 when this NPC was hit by player */
    int hit_damage;          /* damage dealt to this NPC this tick */
    int hit_was_successful_this_tick;
    int hit_spell_type;      /* ENCOUNTER_SPELL_* from the pending hit that just landed */
    int spawn_blob_splits_on_removal;
} InfNPC;


typedef struct {
    int x, y;
    int hp;
    int active;
} InfPillar;


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

    /* set timer pause: pauses between HP 600→480, resumes with +175 when jad spawns */
    int timer_paused;
    int has_paused;
} InfZukState;


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
    [GEAR_SLOT_SHIELD] = ITEM_ELYSIAN_SPIRIT_SHIELD,
    [GEAR_SLOT_BODY]   = ITEM_VIRTUS_ROBE_TOP,
    [GEAR_SLOT_LEGS]   = ITEM_VIRTUS_ROBE_BOTTOM,
    [GEAR_SLOT_HANDS]  = ITEM_CONFLICTION_GAUNTLETS,
    [GEAR_SLOT_FEET]   = ITEM_AVERNIC_TREADS,
    [GEAR_SLOT_RING]   = ITEM_VENATOR_RING,
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
    [GEAR_SLOT_FEET]   = ITEM_AVERNIC_TREADS,
    [GEAR_SLOT_RING]   = ITEM_VENATOR_RING,
};

static const uint8_t INF_RANGE_BP_LOADOUT[NUM_GEAR_SLOTS] = {
    [GEAR_SLOT_HEAD]   = ITEM_MASORI_MASK_F,
    [GEAR_SLOT_CAPE]   = ITEM_DIZANAS_QUIVER,
    [GEAR_SLOT_NECK]   = ITEM_NECKLACE_OF_ANGUISH,
    [GEAR_SLOT_AMMO]   = ITEM_DRAGON_DART,
    [GEAR_SLOT_WEAPON] = ITEM_TOXIC_BLOWPIPE,
    [GEAR_SLOT_SHIELD] = ITEM_NONE,  /* bp is 2h */
    [GEAR_SLOT_BODY]   = ITEM_MASORI_BODY_F,
    [GEAR_SLOT_LEGS]   = ITEM_MASORI_CHAPS_F,
    [GEAR_SLOT_HANDS]  = ITEM_ZARYTE_VAMBRACES,
    [GEAR_SLOT_FEET]   = ITEM_AVERNIC_TREADS,
    [GEAR_SLOT_RING]   = ITEM_VENATOR_RING,
};

/* pointer array for loadout switching */
static const uint8_t* const INF_LOADOUTS[INF_NUM_WEAPON_SETS] = {
    INF_MAGE_LOADOUT,
    INF_RANGE_TBOW_LOADOUT,
    INF_RANGE_BP_LOADOUT,
};

enum {
    INF_ZUK_HEALER_REWARD_MODE_BASELINE = 0,
    INF_ZUK_HEALER_REWARD_MODE_TAGS_FIRST = 1,
};

enum {
    INF_JOSEPH_REWARD_MODE_OFF = 0,
    INF_JOSEPH_REWARD_MODE_ON = 1,
};

typedef struct {
    int brew_doses;
    int restore_doses;
    int bastion_doses;
    int stamina_doses;
} InfSupplyDoses;

typedef struct {
    float brew_fraction;
    float restore_fraction;
    float bastion_fraction;
    float stamina_fraction;
} InfSupplyFractions;

typedef struct {
    int public_wave;
    InfSupplyFractions fractions;
} InfSupplyProfileAnchor;

typedef struct {
    Player player;

    InfNPC npcs[INF_MAX_NPCS];
    int current_obs_slots[INF_OBS_NPCS];
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
    int wave_spawn_target; /* wave index queued to spawn when the inter-wave delay ends */
    int tick;
    int wave_spawn_delay;  /* ticks until first wave spawns (0 = spawn immediately) */
    int wave_ready_delay;
    int episode_over;
    int winner;            /* 0 = player won (zuk dead), 1 = player died */

    /* reward tracking */
    float reward;
    float episode_return;  /* accumulated reward over entire episode */
    float min_zuk_hp_seen; /* final-wave low watermark for irreversible Zuk progress */
    /* -1 means the Zuk HP boundary has not been crossed this episode. */
    int tick_at_le_300;
    int tick_at_le_240;
    int tick_at_le_150;
    int tick_at_zuk_healer_spawn;
    int tick_at_first_zuk_healer_tag;
    int tick_at_all_zuk_healers_tagged;
    int tick_at_all_zuk_healers_dead;
    int tick_at_first_zuk_hit_after_all_healers_dead;
    /* Damage accumulated after crossing each Zuk HP boundary. */
    float damage_after_300;
    float damage_after_240;
    float damage_after_150;
    float damage_after_all_zuk_healers_dead;
    float post_healer_set_alive_penalty_total;
    float post_healer_set_damage_reward_total;
    float post_healer_set_kill_bonus_total;
    float post_healer_set_pressure_total;
    float zuk_hp_at_all_zuk_healers_dead;
    float hp_restored_after_240;
    float spark_damage_after_240;
    float zuk_hp_max_after_healer_spawn;
    float damage_dealt_this_tick;
    float damage_resurrected_this_tick;
    float damage_zuk_this_tick;
    float damage_zuk_healers_this_tick;
    float damage_jad_this_tick;
    float damage_set_this_tick;
    int kill_jad_this_tick;
    int kill_zuk_healer_this_tick;
    int kill_set_this_tick;
    float shield_damage_this_tick;
    int healer_tags_this_tick;
    int zuk_healer_tags_this_tick;
    int zuk_untagged_healer_targets_this_tick;
    int zuk_safe_untagged_healer_targets_this_tick;
    float spark_damage_this_tick;
    float damage_received_this_tick;
    float hp_restored_this_tick;
    float hp_restored_zuk_this_tick;
    int prayer_correct_this_tick;  /* count of NPC attacks blocked by prayer this tick */
    int wave_completed_this_tick;
    int pillar_lost_this_tick;     /* -1 = none, 0-2 = which pillar was destroyed */
    int player_moved_this_tick;
    int player_moved_last_tick;

    /* cumulative stats for diagnostics */
    float total_damage_dealt;
    float total_zuk_healer_damage;
    float total_damage_received;
    float total_hp_restored;   /* cumulative HP restored to enemies this episode */
    int total_zuk_healer_tags;
    int total_zuk_healer_kills;
    int total_zuk_untagged_healer_targets;
    int total_zuk_safe_untagged_healer_targets;
    int total_zuk_unsafe_untagged_healer_targets;
    int total_zuk_untagged_healer_target_rewards;
    int total_zuk_safe_untagged_healer_target_rewards;
    int total_waves_cleared;
    int ticks_without_action;  /* consecutive ticks with no attack or movement */
    int total_prayer_correct;  /* times prayer blocked an NPC attack */
    int total_npc_attacks;     /* total NPC attacks on player (for prayer_correct_rate) */
    int total_unavoidable_off; /* off-prayer hits where a different style was correctly prayed */
    int off_prayer_hits_this_tick;
    /* per-tick tracking for multi-style analysis */
    int tick_styles_fired;     /* bitmask of styles that fired this tick (bit0=mel,1=rng,2=mag) */
    int tick_attacks_fired;    /* count of NPC attacks that fired this tick */
    int total_ranger_mager_same_tick_attacks;
    int total_step_out_ranger_mager_same_tick_attacks;
    /* per-NPC-type prayer and damage tracking (for wandb, not dashboard) */
    int prayer_correct_by_type[INF_NUM_NPC_TYPES];
    int attacks_by_type[INF_NUM_NPC_TYPES];
    float dmg_from_type[INF_NUM_NPC_TYPES];
    int last_hit_by_type;      /* NPC type that last dealt damage to player (-1=none) */
    int killed_by_type[INF_NUM_NPC_TYPES];  /* count of deaths caused by each NPC type */
    int total_idle_ticks;      /* cumulative ticks of ticks_without_action > 0 */
    int total_brews_used;      /* brew doses consumed this episode */
    int total_blood_healed;    /* HP healed via blood barrage this episode */
    int total_npc_kills;       /* NPCs killed this episode */
    int total_gear_switches;   /* gear switch actions this episode */

    /* Zuk-specific diagnostics */
    int behind_shield_ticks;       /* ticks spent behind shield during Zuk wave */
    int behind_shield_this_tick;   /* 1 if behind shield this tick (for reward) */
    int total_zuk_ticks;           /* total ticks during Zuk wave (for behind_shield_pct) */
    int offshield_ticks_after_240;
    int offshield_ticks_after_all_zuk_healers_dead;

    /* per-tick reward event flags (cleared each tick) */
    int brewed_this_tick;      /* 1 if player drank a brew this tick */
    int blood_heal_this_tick;  /* HP healed from blood barrage this tick */

    /* player combat state */
    OsrsInteraction interaction;  /* shared interaction state */
    int player_last_interaction_target_slot;
    int player_last_interaction_age;

    /* gear state */
    InfWeaponSet weapon_set;
    EncounterLoadoutStats loadout_stats[INF_NUM_WEAPON_SETS];
    int human_command_mode;
    EncounterLoadoutStats human_loadout_stats;
    const HumanCommand* human_commands;
    int human_command_count;
    int stamina_active_ticks;  /* countdown for stamina effect */

    /* per-tick player attack event for renderer projectiles */
    int player_attacked_this_tick;  /* 1 if player fired an attack this tick */
    int player_attack_npc_idx;      /* NPC index targeted by player attack */
    int player_attack_dmg;          /* total damage dealt */
    int player_attack_style_id;     /* ATTACK_STYLE_* of the player attack */
    EncounterProjectileTiming player_attack_timing;

    /* pending hits on player from NPC attacks (projectiles in flight) */
    EncounterPendingHit player_pending_hits[ENCOUNTER_MAX_PENDING_HITS];
    int player_pending_hit_count;
    InfPendingSpark pending_sparks[INF_MAX_PENDING_SPARKS];
    InfNpcTargetHit npc_target_hits[INF_MAX_NPC_TARGET_HITS];

    /* nibbler pillar target: random pillar chosen per wave, all nibblers attack it */
    int nibbler_target_pillar;

    /* spawn position shuffle buffer */
    int spawn_order[INF_NUM_SPAWN_POS];

    /* collision map (loaded from cache, passed via put_ptr) */
    const CollisionMap* collision_map;
    int world_offset_x, world_offset_y;

    /* human click-to-move destination (-1 = no dest) */
    int player_dest_x, player_dest_y;

    /* per-tick LOS cache: lazy — computed on first access, reused for rest of tick.
       -1 = not yet computed, 0 = no LOS, 1 = has LOS. invalidated at tick start
       and on pillar collapse. */
    int8_t npc_los_cache[INF_MAX_NPCS];

    /* OSRS entity collision flags. pathfinding ignores these, movement application checks them. */
    uint8_t npc_collision_flags[INF_ARENA_WIDTH][INF_ARENA_HEIGHT];
    uint8_t player_collision_flags[INF_ARENA_WIDTH][INF_ARENA_HEIGHT];

    /* config */
    int start_wave;        /* for curriculum: start from a later wave */
    uint32_t rng_state;
    float damage_reward_coeff;
    float shield_penalty_coeff;
    float tag_reward_coeff;
    float late_start_supply_profile_scale;
    float supply_milestone_brew_reward_coeff;
    float supply_milestone_restore_reward_coeff;
    uint32_t supply_milestone_rewarded_mask;
    float death_penalty_coeff;
    int terminal_penalty_enabled;
    int step_out_forecast_obs_enabled;
    float phase_900_bonus;
    float phase_600_bonus;
    float phase_300_bonus;
    float shield_penalty_episode_cap;
    /* per-episode milestone-fired flags (cleared at reset) */
    uint8_t phase_900_fired;
    uint8_t phase_600_fired;
    uint8_t phase_300_fired;
    float shield_penalty_episode_total;

    /* late-game boss reward shaping. */
    float jad_damage_reward_coeff;
    float zuk_healer_damage_reward_coeff;
    float set_damage_reward_coeff;
    float jad_kill_bonus;
    float zuk_healer_kill_bonus;
    float set_kill_bonus;
    float post_healer_zuk_damage_coeff;
    float post_healer_set_damage_reward_coeff;
    float post_healer_set_kill_bonus;
    float post_healer_set_alive_tick_penalty_coeff;
    float post_healer_set_alive_penalty_cap;
    float zuk_healer_phase_hp_delta_coeff;
    float zuk_untagged_healer_tick_penalty_coeff;
    float zuk_untagged_healer_target_bonus_coeff;
    float zuk_safe_untagged_healer_target_bonus_coeff;
    float zuk_untagged_healer_nonmagic_attack_bonus_coeff;
    float zuk_healer_mage_attack_penalty_coeff;
    int zuk_safe_untagged_healer_target_mask;
    int zuk_force_safe_untagged_healer_target_mask;
    int zuk_healer_reward_mode;
    /* Multiplies low-watermark Zuk reward once jad_killed_this_episode is set.
       1.0 = unchanged (default). >1.0 incentivises returning to Zuk after Jad kill. */
    float post_jad_zuk_multiplier;
    /* Multiplies low-watermark Zuk reward while a Jad is currently alive.
       1.0 = unchanged (default). <1.0 softly discourages tunneling Zuk while Jad up. */
    float jad_alive_zuk_multiplier;

    /* per-episode kill bonus accumulators - emitted gradually to avoid PPO
       reward clamp [-1, 1] truncating large kill bonuses. */
    float pending_jad_kill_bonus;
    float pending_zuk_healer_kill_bonus;
    float pending_set_kill_bonus;
    uint32_t rewarded_zuk_healer_target_mask;
    /* set once any Jad transitions from alive to dead (or all jads dead after
       jad_spawned). enables post_jad_zuk_multiplier. */
    uint8_t jad_killed_this_episode;

    /* eval-time oracle target-priority wrapper. 0=off (default).
       1=Jad-only override when zuk_hp <= 300.
       2=full priority (Jad > zuk-healer > set) when zuk_hp <= 300.
       3=full priority when zuk_hp <= 240.
       4-7=full priority overrides at jad_spawn (target/+overhead/+gear/+all).
       8=full priority @300 (timing comparison).
       9=target only, untagged Zuk healers until all four are tagged.
       10=target only, currently attackable untagged Zuk healers while behind shield.
       11=mode 10, but only when the player can fire this tick. */
    int oracle_mode;

    Log log;
    int tick_at_first_zuk_healer_target;
    int tick_at_first_zuk_healer_attack;
    int total_zuk_healer_target_ticks;
    int total_zuk_healer_attack_fires;
    int total_zuk_healer_cannot_attack_ticks;
    int total_zuk_healer_cooldown_ticks;
    int total_zuk_healer_out_of_range_ticks;
    int total_zuk_healer_attackable_ticks;
    int zuk_untagged_healer_nonmagic_attacks_this_tick;
    int zuk_healer_mage_attack_fires_this_tick;
    int total_action_mask_checks;
    int zero_valid_action_head_count[9];
    int min_valid_action_count_by_head[9];
    int target_head_valid_healer_count;
    int target_head_valid_zuk_count;
    int target_head_valid_set_count;
    float damage_jad_healers_this_tick;
    int shield_tags_this_tick;
    int total_shield_tags;
    float shield_tag_reward_coeff;
    float hp_restored_jad_this_tick;
    float total_hp_restored_jad;
    float total_hp_restored_zuk;
    int joseph_reward_mode;
} InfernoState;

/* prayer check and RNG: use shared encounter_prayer_correct_for_style(),
   encounter_rand_int(), encounter_rand_float() from osrs_combat.h */

static void inf_shuffle_spawns(InfernoState* s) {
    for (int i = 0; i < INF_NUM_SPAWN_POS; i++)
        s->spawn_order[i] = i;
    encounter_shuffle(s->spawn_order, INF_NUM_SPAWN_POS, &s->rng_state);
}


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

typedef struct {
    int x;
    int y;
    int size;
    int is_player;
} InfTargetArea;

static InfTargetArea inf_npc_current_target_area(const InfernoState* s, const InfNPC* npc) {
    if (npc->type == INF_NPC_NIBBLER) {
        int pillar_idx = s->nibbler_target_pillar;
        if (pillar_idx >= 0 && pillar_idx < INF_NUM_PILLARS &&
            s->pillars[pillar_idx].active) {
            return (InfTargetArea){
                .x = s->pillars[pillar_idx].x,
                .y = s->pillars[pillar_idx].y,
                .size = INF_PILLAR_SIZE,
                .is_player = 0,
            };
        }
        for (int i = 0; i < INF_NUM_PILLARS; i++) {
            if (s->pillars[i].active) {
                return (InfTargetArea){
                    .x = s->pillars[i].x,
                    .y = s->pillars[i].y,
                    .size = INF_PILLAR_SIZE,
                    .is_player = 0,
                };
            }
        }
    }

    if (npc->aggro_target >= 0 && npc->aggro_target < INF_MAX_NPCS &&
        s->npcs[npc->aggro_target].active) {
        const InfNPC* target = &s->npcs[npc->aggro_target];
        return (InfTargetArea){
            .x = target->x,
            .y = target->y,
            .size = target->size,
            .is_player = 0,
        };
    }

    return (InfTargetArea){
        .x = s->player.x,
        .y = s->player.y,
        .size = 1,
        .is_player = 1,
    };
}

/* check if NPC at index i has LOS to its current target */
static int inf_npc_has_los_direct(InfernoState* s, int i) {
    InfNPC* npc = &s->npcs[i];
    const InfNPCStats* stats = &INF_NPC_STATS[npc->type];
    InfTargetArea target = inf_npc_current_target_area(s, npc);
    return entity_has_line_of_sight(s->los_blockers, s->los_blocker_count,
                                    npc->x, npc->y, npc->size,
                                    target.x, target.y, target.size,
                                    stats->attack_range);
}

/* cached LOS check — lazy: computes on first access per tick, caches for reuse.
   cache entries: -1 = not yet computed, 0 = no LOS, 1 = has LOS. */
static int inf_npc_has_los(InfernoState* s, int i) {
    if (s->npc_los_cache[i] >= 0)
        return s->npc_los_cache[i];
    int result = inf_npc_has_los_direct(s, i);
    s->npc_los_cache[i] = (int8_t)result;
    return result;
}

/* invalidate entire LOS cache (call at start of tick and on pillar collapse) */
static inline void inf_invalidate_los_cache(InfernoState* s) {
    memset(s->npc_los_cache, -1, sizeof(s->npc_los_cache));
}

static inline void inf_invalidate_npc_los_cache(InfernoState* s, int i) {
    s->npc_los_cache[i] = -1;
}

static inline int inf_is_final_wave(const InfernoState* s) {
    return s->wave == INF_NUM_WAVES - 1;
}

static inline int inf_zuk_healer_tags_first_reward_gate_active(const InfernoState* s) {
    return s->zuk_healer_reward_mode == INF_ZUK_HEALER_REWARD_MODE_TAGS_FIRST &&
        s->zuk.healer_spawned && s->total_zuk_healer_tags < 4;
}

static int inf_find_live_zuk_idx(const InfernoState* s) {
    for (int i = 0; i < INF_MAX_NPCS; i++) {
        if (s->npcs[i].active && s->npcs[i].type == INF_NPC_ZUK) return i;
    }
    return -1;
}

static int inf_weapon_set_is_valid(const InfernoState* s) {
    return s->weapon_set >= 0 && s->weapon_set < INF_NUM_WEAPON_SETS;
}

typedef enum {
    INF_HEALER_DIAG_OFF = 0,
    INF_HEALER_DIAG_PRE_HEALER = 1,
    INF_HEALER_DIAG_IMMEDIATE_HEALER = 2,
    INF_HEALER_DIAG_PARTIAL_HEALER = 3,
    INF_HEALER_DIAG_POST_HEALER = 4,
    INF_HEALER_DIAG_POST_150 = 5,
    INF_HEALER_DIAG_H0_PRE_HEALER_TIGHT = 6,
    INF_HEALER_DIAG_H4_HEALER_OVERLAP = 7,
    INF_HEALER_DIAG_POST_HEALER_SET_ALIVE = 8,
    INF_HEALER_DIAG_COUNT = 9,
} InfHealerDiagnosticPhase;

typedef struct {
    int live_count;
    int tagged_count;
    int healing_count;
} InfZukHealerCounts;

static InfZukHealerCounts inf_zuk_healer_counts(const InfernoState* s) {
    InfZukHealerCounts counts;
    memset(&counts, 0, sizeof(counts));

    for (int i = 0; i < INF_MAX_NPCS; i++) {
        const InfNPC* npc = &s->npcs[i];
        if (!npc->active || npc->hp <= 0 || npc->type != INF_NPC_HEALER_ZUK)
            continue;
        counts.live_count++;
        if (npc->aggro_target >= 0) counts.healing_count++;
        else counts.tagged_count++;
    }

    return counts;
}

static int inf_is_live_zuk_healer_slot(const InfernoState* s, int npc_slot) {
    if (npc_slot < 0 || npc_slot >= INF_MAX_NPCS) return 0;
    const InfNPC* npc = &s->npcs[npc_slot];
    return npc->active &&
        npc->hp > 0 &&
        npc->death_ticks == 0 &&
        npc->type == INF_NPC_HEALER_ZUK;
}

static int inf_has_live_set_or_jad(const InfernoState* s) {
    for (int i = 0; i < INF_MAX_NPCS; i++) {
        const InfNPC* npc = &s->npcs[i];
        if (!npc->active || npc->hp <= 0) continue;
        if (npc->type == INF_NPC_JAD ||
                npc->type == INF_NPC_MAGER ||
                npc->type == INF_NPC_MELEER ||
                npc->type == INF_NPC_RANGER) {
            return 1;
        }
    }
    return 0;
}

static int inf_npc_type_is_set_pressure(int type) {
    return type == INF_NPC_MAGER ||
        type == INF_NPC_MELEER ||
        type == INF_NPC_RANGER;
}

static int inf_has_live_set_pressure(const InfernoState* s) {
    for (int i = 0; i < INF_MAX_NPCS; i++) {
        const InfNPC* npc = &s->npcs[i];
        if (!npc->active || npc->death_ticks != 0 || npc->hp <= 0)
            continue;
        if (inf_npc_type_is_set_pressure(npc->type)) {
            return 1;
        }
    }
    return 0;
}

static int inf_healer_diagnostic_phase_matches(
    const InfernoState* s,
    int diagnostic_phase
) {
    if (diagnostic_phase == INF_HEALER_DIAG_OFF) return 1;
    if (s->episode_over) return 0;
    if (!inf_weapon_set_is_valid(s)) return 0;

    int zuk_idx = inf_find_live_zuk_idx(s);
    if (zuk_idx < 0 || !inf_is_final_wave(s)) return 0;

    int zuk_hp = s->npcs[zuk_idx].hp;
    InfZukHealerCounts healers = inf_zuk_healer_counts(s);

    switch ((InfHealerDiagnosticPhase)diagnostic_phase) {
        case INF_HEALER_DIAG_PRE_HEALER:
            return !s->zuk.healer_spawned && zuk_hp > 240 && zuk_hp <= 300;
        case INF_HEALER_DIAG_IMMEDIATE_HEALER:
            return s->zuk.healer_spawned &&
                healers.live_count == 4 &&
                healers.healing_count == 4;
        case INF_HEALER_DIAG_PARTIAL_HEALER:
            return s->zuk.healer_spawned &&
                healers.live_count > 0 &&
                (healers.live_count < 4 || healers.tagged_count > 0);
        case INF_HEALER_DIAG_POST_HEALER:
            return s->zuk.healer_spawned && healers.live_count == 0;
        case INF_HEALER_DIAG_POST_150:
            return s->zuk.healer_spawned &&
                (zuk_hp <= 150 ||
                 (s->min_zuk_hp_seen > 0.0f && s->min_zuk_hp_seen <= 150.0f));
        case INF_HEALER_DIAG_H0_PRE_HEALER_TIGHT:
            return !s->zuk.healer_spawned && zuk_hp > 240 && zuk_hp <= 260;
        case INF_HEALER_DIAG_H4_HEALER_OVERLAP:
            return s->zuk.healer_spawned &&
                healers.live_count > 0 &&
                inf_has_live_set_or_jad(s);
        case INF_HEALER_DIAG_POST_HEALER_SET_ALIVE:
            return s->zuk.healer_spawned &&
                healers.live_count == 0 &&
                inf_has_live_set_pressure(s);
        case INF_HEALER_DIAG_OFF:
            return 1;
        case INF_HEALER_DIAG_COUNT:
            return 0;
    }

    return 0;
}

static int inf_healer_diagnostic_attack_timer_matches(
    const InfernoState* s,
    int max_player_attack_timer
) {
    return max_player_attack_timer < 0 ||
        s->player.attack_timer <= max_player_attack_timer;
}

static void inf_reset_transition_diagnostics_for_restored_start(InfernoState* s) {
    int zuk_idx = inf_find_live_zuk_idx(s);
    int has_live_zuk = zuk_idx >= 0;
    float zuk_hp = has_live_zuk ? (float)s->npcs[zuk_idx].hp : 1200.0f;

    s->tick_at_le_300 = has_live_zuk && zuk_hp <= 300.0f ? s->tick : -1;
    s->tick_at_le_240 = has_live_zuk && zuk_hp <= 240.0f ? s->tick : -1;
    s->tick_at_le_150 = has_live_zuk && zuk_hp <= 150.0f ? s->tick : -1;

    s->damage_after_300 = 0.0f;
    s->damage_after_240 = 0.0f;
    s->damage_after_150 = 0.0f;
    s->damage_after_all_zuk_healers_dead = 0.0f;
    s->zuk_hp_at_all_zuk_healers_dead = 0.0f;
    s->hp_restored_after_240 = 0.0f;
    s->spark_damage_after_240 = 0.0f;
    s->zuk_hp_max_after_healer_spawn =
        s->zuk.healer_spawned && has_live_zuk ? zuk_hp : 0.0f;

    InfZukHealerCounts healers = inf_zuk_healer_counts(s);
    s->total_zuk_healer_tags = healers.tagged_count;
    if (s->total_zuk_healer_tags > 4) s->total_zuk_healer_tags = 4;
    s->total_shield_tags = 0;
    s->total_zuk_healer_kills = 0;
    s->total_zuk_healer_target_ticks = 0;
    s->total_zuk_healer_attack_fires = 0;
    s->total_zuk_healer_cannot_attack_ticks = 0;
    s->total_zuk_healer_cooldown_ticks = 0;
    s->total_zuk_healer_out_of_range_ticks = 0;
    s->total_zuk_healer_attackable_ticks = 0;

    s->tick_at_zuk_healer_spawn = s->zuk.healer_spawned ? s->tick : -1;
    s->tick_at_first_zuk_healer_tag =
        s->zuk.healer_spawned && healers.tagged_count > 0 ? s->tick : -1;
    s->tick_at_all_zuk_healers_tagged =
        s->zuk.healer_spawned &&
        healers.live_count > 0 &&
        healers.healing_count == 0 ? s->tick : -1;
    s->tick_at_all_zuk_healers_dead =
        s->zuk.healer_spawned && healers.live_count == 0 ? s->tick : -1;
    if (s->tick_at_all_zuk_healers_dead >= 0)
        s->zuk_hp_at_all_zuk_healers_dead = has_live_zuk ? zuk_hp : 0.0f;
    s->tick_at_first_zuk_hit_after_all_healers_dead = -1;
    s->tick_at_first_zuk_healer_target = -1;
    s->tick_at_first_zuk_healer_attack = -1;
    s->offshield_ticks_after_240 = 0;
    s->offshield_ticks_after_all_zuk_healers_dead = 0;
}

enum {
    INF_STYLE_MASK_MELEE = 1 << 0,
    INF_STYLE_MASK_RANGED = 1 << 1,
    INF_STYLE_MASK_MAGIC = 1 << 2,
};

static inline int inf_attack_style_mask_bit(int style) {
    if (style == ATTACK_STYLE_MELEE) return INF_STYLE_MASK_MELEE;
    if (style == ATTACK_STYLE_RANGED) return INF_STYLE_MASK_RANGED;
    if (style == ATTACK_STYLE_MAGIC) return INF_STYLE_MASK_MAGIC;
    return 0;
}

static inline int inf_cardinal_contact_with_npc(int px, int py, int nx, int ny, int npc_size) {
    int cx = px < nx ? nx : (px > nx + npc_size - 1 ? nx + npc_size - 1 : px);
    int cy = py < ny ? ny : (py > ny + npc_size - 1 ? ny + npc_size - 1 : py);
    int dx = px - cx; if (dx < 0) dx = -dx;
    int dy = py - cy; if (dy < 0) dy = -dy;
    return dx + dy == 1;
}

static inline int inf_melee_fallback_possible(
    const InfernoState* s, const InfNPC* npc, const InfNPCStats* stats,
    int planned_style, int dist
) {
    if (!stats->can_melee || planned_style == ATTACK_STYLE_MELEE || dist != 1)
        return 0;

    switch (npc->type) {
        case INF_NPC_RANGER:
        case INF_NPC_MAGER:
            return 1;
        case INF_NPC_BLOB:
        case INF_NPC_JAD:
            return inf_cardinal_contact_with_npc(
                s->player.x, s->player.y, npc->x, npc->y, npc->size);
        default:
            return 0;
    }
}

static inline int inf_attack_style_options_mask(
    const InfernoState* s, const InfNPC* npc, const InfNPCStats* stats,
    int planned_style, int dist
) {
    int mask = inf_attack_style_mask_bit(planned_style);
    if (inf_melee_fallback_possible(s, npc, stats, planned_style, dist))
        mask |= INF_STYLE_MASK_MELEE;
    return mask;
}

static inline int inf_attack_style_from_mask(int style_mask) {
    if (style_mask == INF_STYLE_MASK_MELEE) return ATTACK_STYLE_MELEE;
    if (style_mask == INF_STYLE_MASK_RANGED) return ATTACK_STYLE_RANGED;
    if (style_mask == INF_STYLE_MASK_MAGIC) return ATTACK_STYLE_MAGIC;
    return ATTACK_STYLE_NONE;
}

static inline int inf_attack_style_obs_preview(int style_mask) {
    int primary_mask = style_mask & (INF_STYLE_MASK_RANGED | INF_STYLE_MASK_MAGIC);
    int primary_style = inf_attack_style_from_mask(primary_mask);
    if (primary_style != ATTACK_STYLE_NONE)
        return primary_style;
    return inf_attack_style_from_mask(style_mask);
}

static inline int inf_attack_style_telegraph_mask(
    const InfernoState* s, const InfNPC* npc, const InfNPCStats* stats,
    int planned_style, int dist
) {
    /* Jad only telegraphs its ranged/magic branch. Melee is an immediate
       fallback choice at fire time, not a prayer-switch cue. */
    if (npc->type == INF_NPC_JAD)
        return inf_attack_style_mask_bit(planned_style);
    return inf_attack_style_options_mask(s, npc, stats, planned_style, dist);
}

static inline int inf_pending_hit_obs_timer(const EncounterPendingHit* ph) {
    if (ph->check_prayer && ph->prayer_check_delay > 0)
        return ph->prayer_check_delay;
    return ph->ticks_remaining;
}

#define INF_JAD_PROJECTILE_DELAY 3
#define INF_ANIM_JALTOK_JAD_MAGIC_ATTACK INF_GEN_ANIM_JALTOK_JAD_ATTACK_MAGIC
#define INF_ANIM_JALTOK_JAD_RANGED_ATTACK INF_GEN_ANIM_JALTOK_JAD_ATTACK_RANGED
#define INF_ANIM_JALTOK_JAD_MELEE_ATTACK INF_GEN_ANIM_JALTOK_JAD_ATTACK_MELEE

static inline EncounterProjectileDelayKind inf_projectile_delay_kind_for_style(int style) {
    if (style == ATTACK_STYLE_MAGIC) return ENCOUNTER_PROJECTILE_DELAY_MAGIC;
    if (style == ATTACK_STYLE_RANGED) return ENCOUNTER_PROJECTILE_DELAY_RANGED;
    return ENCOUNTER_PROJECTILE_DELAY_MELEE;
}

static inline EncounterProjectileDelayOptions inf_npc_projectile_options(
    InfNPCType type, int style
) {
    EncounterProjectileDelayOptions options = {0};
    if (type == INF_NPC_MAGER && style == ATTACK_STYLE_MAGIC) {
        options.visual_delay_ticks = 2;
        options.visual_hit_early_ticks = -1;
    } else if (type == INF_NPC_RANGER && style == ATTACK_STYLE_RANGED) {
        options.reduce_delay = -2;
        options.visual_delay_ticks = 3;
    } else if (type == INF_NPC_JAD && style != ATTACK_STYLE_MELEE) {
        options.reduce_delay = INF_JAD_PROJECTILE_DELAY;
        options.visual_hit_early_ticks = -1;
    } else if (type == INF_NPC_ZUK) {
        options.set_delay = 4;
        options.visual_delay_ticks = 2;
    }
    return options;
}

static inline EncounterProjectileTiming inf_npc_projectile_timing(
    InfNPCType type, int style, int distance
) {
    return encounter_projectile_timing(
        distance, 0, inf_projectile_delay_kind_for_style(style),
        inf_npc_projectile_options(type, style));
}

static inline EncounterProjectileTiming inf_player_projectile_timing(
    int style, uint8_t weapon, int is_special, int distance
) {
    EncounterProjectileDelayKind kind = inf_projectile_delay_kind_for_style(style);
    EncounterProjectileDelayOptions options = {0};
    if (style == ATTACK_STYLE_RANGED) {
        if (weapon == ITEM_TOXIC_BLOWPIPE) {
            kind = ENCOUNTER_PROJECTILE_DELAY_THROWN;
            options.visual_delay_ticks = 1;
            if (is_special) {
                options.reduce_delay = -1;
                options.visual_hit_early_ticks = 1;
            }
        } else if (weapon == ITEM_TWISTED_BOW) {
            options.visual_delay_ticks = 1;
        }
    }
    return encounter_projectile_timing(distance, 1, kind, options);
}

static inline int inf_jad_roll_primary_style(uint32_t* rng_state) {
    return (encounter_rand_int(rng_state, 2) == 0)
        ? ATTACK_STYLE_RANGED
        : ATTACK_STYLE_MAGIC;
}

static inline int inf_npc_attack_anim_id(const InfNPC* npc, const NpcModelMapping* nm) {
    if (npc->resurrecting_this_tick)
        return INF_GEN_ANIM_MAGER_RESURRECT;
    if (npc->type == INF_NPC_RANGER &&
        npc->attack_style_this_tick == ATTACK_STYLE_MELEE)
        return INF_GEN_ANIM_RANGER_ATTACK_MELEE;
    if (npc->type == INF_NPC_MAGER &&
        npc->attack_style_this_tick == ATTACK_STYLE_MELEE)
        return INF_GEN_ANIM_MAGER_ATTACK_MELEE;

    if (npc->type == INF_NPC_JAD) {
        switch (npc->attack_style_this_tick) {
            case ATTACK_STYLE_MAGIC:
                return INF_ANIM_JALTOK_JAD_MAGIC_ATTACK;
            case ATTACK_STYLE_RANGED:
                return INF_ANIM_JALTOK_JAD_RANGED_ATTACK;
            case ATTACK_STYLE_MELEE:
                return INF_ANIM_JALTOK_JAD_MELEE_ATTACK;
            default:
                return -1;
        }
    }

    if (!nm || nm->attack_anim == 65535)
        return -1;
    return (int)nm->attack_anim;
}

static inline int inf_npc_death_anim_id(const InfNPC* npc, const NpcModelMapping* nm) {
    switch (npc->type) {
        case INF_NPC_NIBBLER:
            return INF_GEN_ANIM_NIBBLER_DEATH;
        case INF_NPC_BAT:
            return INF_GEN_ANIM_BAT_DEATH;
        case INF_NPC_BLOB:
        case INF_NPC_BLOB_MELEE:
        case INF_NPC_BLOB_RANGE:
        case INF_NPC_BLOB_MAGE:
            return INF_GEN_ANIM_BLOB_DEATH;
        case INF_NPC_MELEER:
            return INF_GEN_ANIM_MELEER_DEATH;
        case INF_NPC_RANGER:
            return INF_GEN_ANIM_RANGER_DEATH;
        case INF_NPC_MAGER:
            return INF_GEN_ANIM_MAGER_DEATH;
        case INF_NPC_JAD:
            return INF_GEN_ANIM_JALTOK_JAD_DEATH;
        case INF_NPC_ZUK:
            return INF_GEN_ANIM_TZKAL_ZUK_DEATH;
        case INF_NPC_ZUK_SHIELD:
            return INF_GEN_ANIM_ZUK_SHIELD_DEATH;
        default:
            return nm ? (int)nm->idle_anim : -1;
    }
}

static inline int inf_choose_attack_style_for_tick(
    uint32_t* rng_state, int style_mask
) {
    int primary_style = inf_attack_style_from_mask(style_mask & ~INF_STYLE_MASK_MELEE);
    if ((style_mask & INF_STYLE_MASK_MELEE) && primary_style != ATTACK_STYLE_NONE)
        return (encounter_rand_int(rng_state, 2) == 0) ? ATTACK_STYLE_MELEE : primary_style;
    return inf_attack_style_from_mask(style_mask);
}


static inline int inf_dead_mob_is_resurrectable(InfNPCType type) {
    switch (type) {
        case INF_NPC_BAT:
        case INF_NPC_BLOB:
        case INF_NPC_MELEER:
        case INF_NPC_RANGER:
        case INF_NPC_MAGER:
            return 1;
        default:
            return 0;
    }
}

static void inf_store_dead_mob(InfernoState* s, InfNPC* npc) {
    if (s->dead_mob_count >= INF_MAX_DEAD_MOBS) return;
    /* only store the exact types that register with InfernoMobDeathStore in
       the reference: bat, blob parent, meleer, ranger, and mager. */
    if (!inf_dead_mob_is_resurrectable(npc->type)) return;
    if (npc->resurrection_count != 0) return;

    InfDeadMob* dm = &s->dead_mobs[s->dead_mob_count++];
    dm->type = npc->type;
    dm->x = npc->x;
    dm->y = npc->y;
    dm->hp = npc->max_hp / 2;  /* resurrect at 50% HP */
    dm->max_hp = npc->max_hp;
}


static float inf_compute_reward(InfernoState* s);
static void inf_spawn_wave(InfernoState* s);
static void inf_tick_npcs(InfernoState* s);
static void inf_tick_player(InfernoState* s, const int* actions, int can_attack);
static void inf_apply_npc_death(InfernoState* s, int npc_idx);
static int inf_mager_resurrect(InfernoState* s, int idx);
static void inf_queue_zuk_healer_sparks(InfernoState* s, const InfNPC* npc);
static void inf_resolve_pending_sparks(InfernoState* s);
static void inf_rebuild_player_collision_flags(InfernoState* s);
static void inf_refresh_current_obs_slots(InfernoState* s);


static EncounterState* inf_create(void) {
    inf_build_npc_stats();
    InfernoState* s = (InfernoState*)calloc(1, sizeof(InfernoState));
    s->rng_state = 12345;
    s->late_start_supply_profile_scale = 1.0f;
    s->step_out_forecast_obs_enabled = 1;
    return (EncounterState*)s;
}

static void inf_destroy(EncounterState* state) {
    free(state);
}

static InfSupplyDoses inf_full_starting_supplies(void) {
    return (InfSupplyDoses){
        .brew_doses = 24,
        .restore_doses = 40,
        .bastion_doses = 8,
        .stamina_doses = 4,
    };
}

static const InfSupplyProfileAnchor INF_SUPPLY_PROFILE_ANCHORS[] = {
    { 1,  { 1.0000f, 1.0000f, 1.0000f, 1.0000f } },
    { 20, { 1.0000f, 0.9500f, 1.0000f, 1.0000f } },
    { 40, { 0.9167f, 0.8750f, 1.0000f, 1.0000f } },
    { 61, { 0.8333f, 0.7500f, 1.0000f, 1.0000f } },
    { 64, { 0.5833f, 0.5000f, 0.7500f, 1.0000f } },
    { 68, { 0.5833f, 0.4250f, 0.6250f, 1.0000f } },
    { 69, { 0.5000f, 0.3000f, 0.3750f, 1.0000f } },
};

static float inf_lerp_float(float a, float b, float t) {
    return a + (b - a) * t;
}

static void inf_require_valid_supply_scale(float scale) {
    if (scale < 0.0f || scale > 1.0f) {
        fprintf(stderr, "inferno late_start_supply_profile_scale must be in [0, 1], got %.6f\n",
            scale);
        abort();
    }
}

static void inf_require_nonnegative_float_config(const char* key, float value) {
    if (!(value >= 0.0f)) {
        fprintf(stderr, "inferno %s must be >= 0, got %.6f\n", key, value);
        abort();
    }
}

static void inf_require_valid_public_wave(int public_wave) {
    if (public_wave < 1 || public_wave > INF_MAX_PUBLIC_START_WAVE) {
        fprintf(stderr, "inferno start_wave must be in [1, %d], got %d\n",
            INF_MAX_PUBLIC_START_WAVE, public_wave);
        abort();
    }
}

static int inf_runtime_wave_from_start_wave(int internal_start_wave) {
    return internal_start_wave >= INF_WAVE_ZUK ? INF_WAVE_ZUK : internal_start_wave;
}

static int inf_initial_attack_timer_after_stun(int attack_speed, int stun_timer) {
    return attack_speed + stun_timer;
}

static InfSupplyFractions inf_supply_profile_fractions(int public_wave) {
    inf_require_valid_public_wave(public_wave);
    if (public_wave > INF_NUM_WAVES)
        public_wave = INF_NUM_WAVES;

    const int n = (int)(sizeof(INF_SUPPLY_PROFILE_ANCHORS) /
        sizeof(INF_SUPPLY_PROFILE_ANCHORS[0]));
    for (int i = 1; i < n; i++) {
        const InfSupplyProfileAnchor* lo = &INF_SUPPLY_PROFILE_ANCHORS[i - 1];
        const InfSupplyProfileAnchor* hi = &INF_SUPPLY_PROFILE_ANCHORS[i];
        if (public_wave <= hi->public_wave) {
            float t = (float)(public_wave - lo->public_wave) /
                (float)(hi->public_wave - lo->public_wave);
            return (InfSupplyFractions){
                .brew_fraction = inf_lerp_float(lo->fractions.brew_fraction,
                    hi->fractions.brew_fraction, t),
                .restore_fraction = inf_lerp_float(lo->fractions.restore_fraction,
                    hi->fractions.restore_fraction, t),
                .bastion_fraction = inf_lerp_float(lo->fractions.bastion_fraction,
                    hi->fractions.bastion_fraction, t),
                .stamina_fraction = inf_lerp_float(lo->fractions.stamina_fraction,
                    hi->fractions.stamina_fraction, t),
            };
        }
    }

    fprintf(stderr, "inferno supply profile has no anchor for wave %d\n", public_wave);
    abort();
}

static int inf_supply_profile_anchor_index(int public_wave) {
    int n = (int)(sizeof(INF_SUPPLY_PROFILE_ANCHORS) /
        sizeof(INF_SUPPLY_PROFILE_ANCHORS[0]));
    for (int i = 0; i < n; i++) {
        if (INF_SUPPLY_PROFILE_ANCHORS[i].public_wave == public_wave)
            return i;
    }
    return -1;
}

static int inf_profiled_supply_count(int full_doses, float profile_fraction, float scale) {
    assert(full_doses >= 0);

    float effective_fraction = 1.0f - scale * (1.0f - profile_fraction);
    int doses = (int)((float)full_doses * effective_fraction + 0.5f);
    if (doses < 0) doses = 0;
    if (doses > full_doses) doses = full_doses;
    return doses;
}

static InfSupplyDoses inf_supplies_for_start_wave(InfSupplyDoses full,
                                                  int internal_start_wave,
                                                  float scale) {
    inf_require_valid_supply_scale(scale);

    int public_wave = internal_start_wave + 1;

    InfSupplyFractions fractions = inf_supply_profile_fractions(public_wave);
    return (InfSupplyDoses){
        .brew_doses = inf_profiled_supply_count(full.brew_doses,
            fractions.brew_fraction, scale),
        .restore_doses = inf_profiled_supply_count(full.restore_doses,
            fractions.restore_fraction, scale),
        .bastion_doses = inf_profiled_supply_count(full.bastion_doses,
            fractions.bastion_fraction, scale),
        .stamina_doses = inf_profiled_supply_count(full.stamina_doses,
            fractions.stamina_fraction, scale),
    };
}

static float inf_supply_milestone_surplus_reward(InfernoState* s, int public_wave) {
    if (s->supply_milestone_brew_reward_coeff <= 0.0f &&
            s->supply_milestone_restore_reward_coeff <= 0.0f) {
        return 0.0f;
    }

    int anchor_idx = inf_supply_profile_anchor_index(public_wave);
    if (anchor_idx <= 0) return 0.0f;

    uint32_t anchor_bit = 1u << (uint32_t)anchor_idx;
    if ((s->supply_milestone_rewarded_mask & anchor_bit) != 0)
        return 0.0f;
    s->supply_milestone_rewarded_mask |= anchor_bit;

    InfSupplyDoses full = inf_full_starting_supplies();
    InfSupplyDoses expected = inf_supplies_for_start_wave(
        full, public_wave - 1, s->late_start_supply_profile_scale);

    int brew_surplus = s->player.brew_doses - expected.brew_doses;
    int restore_surplus = s->player.restore_doses - expected.restore_doses;
    if (brew_surplus < 0) brew_surplus = 0;
    if (restore_surplus < 0) restore_surplus = 0;

    return
        s->supply_milestone_brew_reward_coeff *
            (float)brew_surplus / (float)full.brew_doses +
        s->supply_milestone_restore_reward_coeff *
            (float)restore_surplus / (float)full.restore_doses;
}

static void inf_reset(EncounterState* state, uint32_t seed) {
    inf_build_npc_stats();
    InfernoState* s = (InfernoState*)state;
    Log saved_log = s->log;
    int saved_start = s->start_wave;
    uint32_t saved_rng = s->rng_state;
    const CollisionMap* saved_cmap = s->collision_map;
    int saved_wox = s->world_offset_x;
    int saved_woy = s->world_offset_y;
    float saved_damage_reward_coeff = s->damage_reward_coeff;
    float saved_shield_penalty_coeff = s->shield_penalty_coeff;
    float saved_tag_reward_coeff = s->tag_reward_coeff;
    float saved_shield_tag_reward_coeff = s->shield_tag_reward_coeff;
    float saved_late_start_supply_profile_scale = s->late_start_supply_profile_scale;
    float saved_supply_milestone_brew_reward_coeff =
        s->supply_milestone_brew_reward_coeff;
    float saved_supply_milestone_restore_reward_coeff =
        s->supply_milestone_restore_reward_coeff;
    float saved_death_penalty_coeff = s->death_penalty_coeff;
    int saved_terminal_penalty_enabled = s->terminal_penalty_enabled;
    int saved_step_out_forecast_obs_enabled = s->step_out_forecast_obs_enabled;
    float saved_phase_900_bonus = s->phase_900_bonus;
    float saved_phase_600_bonus = s->phase_600_bonus;
    float saved_phase_300_bonus = s->phase_300_bonus;
    float saved_shield_penalty_episode_cap = s->shield_penalty_episode_cap;
    int saved_oracle_mode = s->oracle_mode;
    float saved_jad_damage_reward_coeff = s->jad_damage_reward_coeff;
    float saved_zuk_healer_damage_reward_coeff = s->zuk_healer_damage_reward_coeff;
    float saved_set_damage_reward_coeff = s->set_damage_reward_coeff;
    float saved_jad_kill_bonus = s->jad_kill_bonus;
    float saved_zuk_healer_kill_bonus = s->zuk_healer_kill_bonus;
    float saved_set_kill_bonus = s->set_kill_bonus;
    float saved_post_healer_zuk_damage_coeff = s->post_healer_zuk_damage_coeff;
    float saved_post_healer_set_damage_reward_coeff =
        s->post_healer_set_damage_reward_coeff;
    float saved_post_healer_set_kill_bonus = s->post_healer_set_kill_bonus;
    float saved_post_healer_set_alive_tick_penalty_coeff =
        s->post_healer_set_alive_tick_penalty_coeff;
    float saved_post_healer_set_alive_penalty_cap =
        s->post_healer_set_alive_penalty_cap;
    float saved_zuk_healer_phase_hp_delta_coeff = s->zuk_healer_phase_hp_delta_coeff;
    float saved_zuk_untagged_healer_tick_penalty_coeff =
        s->zuk_untagged_healer_tick_penalty_coeff;
    float saved_zuk_untagged_healer_target_bonus_coeff =
        s->zuk_untagged_healer_target_bonus_coeff;
    float saved_zuk_safe_untagged_healer_target_bonus_coeff =
        s->zuk_safe_untagged_healer_target_bonus_coeff;
    float saved_zuk_untagged_healer_nonmagic_attack_bonus_coeff =
        s->zuk_untagged_healer_nonmagic_attack_bonus_coeff;
    float saved_zuk_healer_mage_attack_penalty_coeff =
        s->zuk_healer_mage_attack_penalty_coeff;
    int saved_zuk_safe_untagged_healer_target_mask =
        s->zuk_safe_untagged_healer_target_mask;
    int saved_zuk_force_safe_untagged_healer_target_mask =
        s->zuk_force_safe_untagged_healer_target_mask;
    int saved_zuk_healer_reward_mode = s->zuk_healer_reward_mode;
    float saved_post_jad_zuk_multiplier = s->post_jad_zuk_multiplier;
    float saved_jad_alive_zuk_multiplier = s->jad_alive_zuk_multiplier;
    int saved_joseph_reward_mode = s->joseph_reward_mode;
    memset(s, 0, sizeof(InfernoState));
    s->log = saved_log;
    s->start_wave = saved_start;
    s->collision_map = saved_cmap;
    s->world_offset_x = saved_wox;
    s->world_offset_y = saved_woy;
    s->rng_state = encounter_resolve_seed(saved_rng, seed);
    s->damage_reward_coeff = saved_damage_reward_coeff;
    s->shield_penalty_coeff = saved_shield_penalty_coeff;
    s->tag_reward_coeff = saved_tag_reward_coeff;
    s->shield_tag_reward_coeff = saved_shield_tag_reward_coeff;
    s->late_start_supply_profile_scale = saved_late_start_supply_profile_scale;
    s->supply_milestone_brew_reward_coeff =
        saved_supply_milestone_brew_reward_coeff;
    s->supply_milestone_restore_reward_coeff =
        saved_supply_milestone_restore_reward_coeff;
    s->death_penalty_coeff = saved_death_penalty_coeff;
    s->terminal_penalty_enabled = saved_terminal_penalty_enabled;
    s->step_out_forecast_obs_enabled = saved_step_out_forecast_obs_enabled;
    s->phase_900_bonus = saved_phase_900_bonus;
    s->phase_600_bonus = saved_phase_600_bonus;
    s->phase_300_bonus = saved_phase_300_bonus;
    s->shield_penalty_episode_cap = saved_shield_penalty_episode_cap;
    s->oracle_mode = saved_oracle_mode;
    s->jad_damage_reward_coeff = saved_jad_damage_reward_coeff;
    s->zuk_healer_damage_reward_coeff = saved_zuk_healer_damage_reward_coeff;
    s->set_damage_reward_coeff = saved_set_damage_reward_coeff;
    s->jad_kill_bonus = saved_jad_kill_bonus;
    s->zuk_healer_kill_bonus = saved_zuk_healer_kill_bonus;
    s->set_kill_bonus = saved_set_kill_bonus;
    s->post_healer_zuk_damage_coeff = saved_post_healer_zuk_damage_coeff;
    s->post_healer_set_damage_reward_coeff =
        saved_post_healer_set_damage_reward_coeff;
    s->post_healer_set_kill_bonus = saved_post_healer_set_kill_bonus;
    s->post_healer_set_alive_tick_penalty_coeff =
        saved_post_healer_set_alive_tick_penalty_coeff;
    s->post_healer_set_alive_penalty_cap =
        saved_post_healer_set_alive_penalty_cap;
    s->zuk_healer_phase_hp_delta_coeff = saved_zuk_healer_phase_hp_delta_coeff;
    s->zuk_untagged_healer_tick_penalty_coeff =
        saved_zuk_untagged_healer_tick_penalty_coeff;
    s->zuk_untagged_healer_target_bonus_coeff =
        saved_zuk_untagged_healer_target_bonus_coeff;
    s->zuk_safe_untagged_healer_target_bonus_coeff =
        saved_zuk_safe_untagged_healer_target_bonus_coeff;
    s->zuk_untagged_healer_nonmagic_attack_bonus_coeff =
        saved_zuk_untagged_healer_nonmagic_attack_bonus_coeff;
    s->zuk_healer_mage_attack_penalty_coeff =
        saved_zuk_healer_mage_attack_penalty_coeff;
    s->zuk_safe_untagged_healer_target_mask =
        saved_zuk_safe_untagged_healer_target_mask;
    s->zuk_force_safe_untagged_healer_target_mask =
        saved_zuk_force_safe_untagged_healer_target_mask;
    s->zuk_healer_reward_mode = saved_zuk_healer_reward_mode;
    s->post_jad_zuk_multiplier = saved_post_jad_zuk_multiplier;
    s->jad_alive_zuk_multiplier = saved_jad_alive_zuk_multiplier;
    s->joseph_reward_mode = saved_joseph_reward_mode;
    if (s->post_jad_zuk_multiplier <= 0.0f) s->post_jad_zuk_multiplier = 1.0f;
    if (s->jad_alive_zuk_multiplier <= 0.0f) s->jad_alive_zuk_multiplier = 1.0f;

    /* human click-to-move: no destination after reset */
    s->player_dest_x = -1;
    s->player_dest_y = -1;
    s->player_last_interaction_target_slot = -1;
    s->player_last_interaction_age = 1;

    s->tick_at_le_300 = -1;
    s->tick_at_le_240 = -1;
    s->tick_at_le_150 = -1;
    s->tick_at_zuk_healer_spawn = -1;
    s->tick_at_first_zuk_healer_tag = -1;
    s->tick_at_all_zuk_healers_tagged = -1;
    s->tick_at_all_zuk_healers_dead = -1;
    s->tick_at_first_zuk_hit_after_all_healers_dead = -1;
    s->tick_at_first_zuk_healer_target = -1;
    s->tick_at_first_zuk_healer_attack = -1;
    s->total_zuk_healer_target_ticks = 0;
    s->total_zuk_healer_attack_fires = 0;
    s->total_zuk_healer_cannot_attack_ticks = 0;
    s->total_zuk_healer_cooldown_ticks = 0;
    s->total_zuk_healer_out_of_range_ticks = 0;
    s->total_zuk_healer_attackable_ticks = 0;
    s->total_action_mask_checks = 0;
    for (int h = 0; h < 9; h++) {
        s->zero_valid_action_head_count[h] = 0;
        s->min_valid_action_count_by_head[h] = 1000000;
    }
    s->target_head_valid_healer_count = 0;
    s->target_head_valid_zuk_count = 0;
    s->target_head_valid_set_count = 0;

    /* player */
    s->player.entity_type = ENTITY_PLAYER;
    s->player.base_hitpoints = 99;
    s->player.current_hitpoints = 99;
    s->player.base_prayer = 99;
    s->player.current_prayer = 99;
    s->player.base_attack = 99;
    s->player.base_strength = 99;
    s->player.base_defence = 99;
    s->player.base_ranged = 99;
    s->player.base_magic = 99;
    s->player.current_ranged = 99;
    s->player.current_magic = 99;
    s->player.current_attack = 99;
    s->player.current_strength = 99;
    s->player.current_defence = 99;
    osrs_item_effect_state_init(&s->player.item_effect_state);
    /* start in mage gear */
    s->weapon_set = INF_GEAR_MAGE;
    encounter_apply_loadout(&s->player, INF_MAGE_LOADOUT, GEAR_MAGE);
    {
        encounter_populate_inventory(&s->player, INF_LOADOUTS, INF_NUM_WEAPON_SETS, NULL);

        /* Ammo slot items (dragon darts, dragon arrows) should NOT appear as
           swappable inventory items — in real OSRS darts live inside the
           blowpipe and arrows inside dizana's quiver. Clear them here; the
           equipment panel still shows the correct ammo for the active weapon. */
        for (int i = 0; i < MAX_ITEMS_PER_SLOT; i++) {
            s->player.inventory[GEAR_SLOT_AMMO][i] = ITEM_NONE;
        }
        s->player.num_items_in_slot[GEAR_SLOT_AMMO] = 0;
    }
    InfSupplyDoses full_supplies = inf_full_starting_supplies();
    InfSupplyDoses start_supplies = inf_supplies_for_start_wave(
        full_supplies, s->start_wave, s->late_start_supply_profile_scale);
    s->player.brew_doses = start_supplies.brew_doses;
    s->player.restore_doses = start_supplies.restore_doses;
    s->player.bastion_doses = start_supplies.bastion_doses;
    s->player.stamina_doses = start_supplies.stamina_doses;
    s->stamina_active_ticks = 0;
    s->player.prayer = PRAYER_NONE;
    s->player.autocast_enabled = 1;
    s->player.autocast_defensive = 0;
    s->player.autocast_spell = ENCOUNTER_SPELL_BLOOD;
    osrs_interaction_init(&s->interaction);
    s->player.spec_armed = 0;
    s->player.special_energy = 100;
    s->player.run_energy = 10000;  /* full run energy (OSRS stores as 0-10000) */
    s->last_hit_by_type = -1;

    /* compute loadout stats from item database (replaces old hardcoded INF_WEAPON_STATS).
       mage is kodai + barrage — pure autocast, no invisible bonus.
       ranged loadouts run in rapid stance: -1 to attack_speed (BP 3→2, tbow 6→5). */
    /* offensive prayer is now agent-controlled runtime state (Player.offensive_prayer),
       not baked into the loadout. pass NONE at reset; inf_player_pretick() calls
       encounter_update_loadout_level() whenever the agent toggles a prayer. */
    encounter_compute_loadout_stats(INF_MAGE_LOADOUT, ATTACK_STYLE_MAGIC,
        OFFENSIVE_PRAYER_NONE, 99, FIGHT_STYLE_AUTOCAST, 30, &s->loadout_stats[INF_GEAR_MAGE]);
    encounter_compute_loadout_stats(INF_RANGE_TBOW_LOADOUT, ATTACK_STYLE_RANGED,
        OFFENSIVE_PRAYER_NONE, 99, FIGHT_STYLE_RAPID, 0, &s->loadout_stats[INF_GEAR_TBOW]);
    encounter_compute_loadout_stats(INF_RANGE_BP_LOADOUT, ATTACK_STYLE_RANGED,
        OFFENSIVE_PRAYER_NONE, 99, FIGHT_STYLE_RAPID, 0, &s->loadout_stats[INF_GEAR_BP]);

    /* spawn position depends on wave */
    int effective_start = inf_runtime_wave_from_start_wave(s->start_wave);
    int is_zuk_wave = (effective_start >= INF_WAVE_ZUK);
    s->player.x = is_zuk_wave ? INF_ZUK_PLAYER_START_X : INF_PLAYER_START_X;
    s->player.y = is_zuk_wave ? INF_ZUK_PLAYER_START_Y : INF_PLAYER_START_Y;
    inf_rebuild_player_collision_flags(s);

    /* pillars: all destroyed at end of wave 66 (index 65), so waves 66+ have none */
    for (int i = 0; i < INF_NUM_PILLARS; i++) {
        s->pillars[i].x = INF_PILLAR_POS[i][0];
        s->pillars[i].y = INF_PILLAR_POS[i][1];
        if (effective_start >= 66) {
            s->pillars[i].hp = 0;
            s->pillars[i].active = 0;
        } else {
            s->pillars[i].hp = INF_PILLAR_HP;
            s->pillars[i].active = 1;
        }
    }
    inf_rebuild_los(s);

    /* dead mob store */
    s->dead_mob_count = 0;

    s->wave = effective_start;
    s->wave_spawn_target = effective_start;
    s->wave_spawn_delay = 0;
    s->wave_ready_delay = INF_START_READY_TICKS;
    inf_spawn_wave(s);
    inf_invalidate_los_cache(s);
}


/* find a free NPC slot, return index or -1 */
static int inf_find_free_npc(InfernoState* s) {
    for (int i = 0; i < INF_MAX_NPCS; i++) {
        if (!s->npcs[i].active) return i;
    }
    return -1;
}

static int inf_grid_index(int x, int y, int* gx, int* gy) {
    *gx = x - INF_ARENA_MIN_X;
    *gy = y - INF_ARENA_MIN_Y;
    return *gx >= 0 && *gx < INF_ARENA_WIDTH && *gy >= 0 && *gy < INF_ARENA_HEIGHT;
}

static int inf_npc_sets_collision_flag(InfNPCType type) {
    return type != INF_NPC_NIBBLER && type != INF_NPC_ZUK_SHIELD;
}

static int inf_npc_effective_size(const InfNPC* npc) {
    return npc->size > 0 ? npc->size : INF_NPC_STATS[npc->type].size;
}

static void inf_unstamp_npc_collision_footprint(InfernoState* s, int x, int y, int size) {
    for (int dx = 0; dx < size; dx++) {
        for (int dy = 0; dy < size; dy++) {
            int gx, gy;
            if (inf_grid_index(x + dx, y + dy, &gx, &gy)) {
                if (s->npc_collision_flags[gx][gy] > 0)
                    s->npc_collision_flags[gx][gy]--;
            }
        }
    }
}

static void inf_stamp_npc_collision_footprint(InfernoState* s, int x, int y, int size) {
    for (int dx = 0; dx < size; dx++) {
        for (int dy = 0; dy < size; dy++) {
            int gx, gy;
            if (inf_grid_index(x + dx, y + dy, &gx, &gy)) {
                assert(s->npc_collision_flags[gx][gy] < UINT8_MAX);
                s->npc_collision_flags[gx][gy]++;
            }
        }
    }
}

static void inf_clear_player_collision_flags(InfernoState* s) {
    memset(s->player_collision_flags, 0, sizeof(s->player_collision_flags));
}

static void inf_stamp_player_collision_flags(InfernoState* s) {
    int gx, gy;
    if (inf_grid_index(s->player.x, s->player.y, &gx, &gy))
        s->player_collision_flags[gx][gy] = 1;
}

static void inf_rebuild_player_collision_flags(InfernoState* s) {
    inf_clear_player_collision_flags(s);
    inf_stamp_player_collision_flags(s);
}

static void inf_rebuild_entity_collision_flags(InfernoState* s) {
    memset(s->npc_collision_flags, 0, sizeof(s->npc_collision_flags));
    inf_rebuild_player_collision_flags(s);
    for (int i = 0; i < INF_MAX_NPCS; i++) {
        InfNPC* npc = &s->npcs[i];
        if (!npc->active) continue;
        if (!inf_npc_sets_collision_flag(npc->type)) continue;
        inf_stamp_npc_collision_footprint(s, npc->x, npc->y, inf_npc_effective_size(npc));
    }
}

static void inf_update_npc_collision_flags(
    InfernoState* s, int idx, int ox, int oy, int nx, int ny, int sz
) {
    if (idx >= 0 && idx < INF_MAX_NPCS &&
        !inf_npc_sets_collision_flag(s->npcs[idx].type))
        return;
    inf_unstamp_npc_collision_footprint(s, ox, oy, sz);
    inf_stamp_npc_collision_footprint(s, nx, ny, sz);
}

static void inf_deactivate_npc(InfernoState* s, int idx) {
    if (idx < 0 || idx >= INF_MAX_NPCS) return;
    InfNPC* npc = &s->npcs[idx];
    if (npc->active && inf_npc_sets_collision_flag(npc->type))
        inf_unstamp_npc_collision_footprint(s, npc->x, npc->y, inf_npc_effective_size(npc));
    npc->active = 0;
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
    npc->jad_attack_style = ATTACK_STYLE_NONE;
    npc->attack_style_this_tick = ATTACK_STYLE_NONE;
    npc->active = 1;
    npc->x = x;
    npc->y = y;
    npc->target_x = x;
    npc->target_y = y;
    npc->attack_visual_target = -1;
    npc->resurrection_visual_target = -1;
    npc->heal_target = -1;
    npc->jad_owner_idx = -1;
    npc->aggro_target = -1;
    npc->blob_scanned_prayer = -1;
    npc->had_los_last_tick = 0;
    npc->stun_timer = stats->stun_on_spawn;

    if (inf_npc_sets_collision_flag(type))
        inf_stamp_npc_collision_footprint(s, x, y, stats->size);
}

static int inf_find_first_active_npc_of_type(const InfernoState* s, InfNPCType type) {
    for (int i = 0; i < INF_MAX_NPCS; i++) {
        if (s->npcs[i].active && s->npcs[i].type == type) return i;
    }
    return -1;
}

static void inf_seed_joseph_zuk_checkpoint(InfernoState* s) {
    if (s->start_wave < INF_START_WAVE_ZUK_JAD) return;

    int zuk_idx = inf_find_first_active_npc_of_type(s, INF_NPC_ZUK);
    int shield_idx = s->zuk.shield_idx;
    if (zuk_idx < 0 || shield_idx < 0 || shield_idx >= INF_MAX_NPCS ||
            !s->npcs[shield_idx].active)
        return;

    InfNPC* zuk = &s->npcs[zuk_idx];
    zuk->stun_timer = 0;
    s->zuk.initial_delay = 0;
    s->zuk.has_paused = 1;
    s->zuk.timer_paused = 0;
    s->zuk.jad_spawned = 1;
    s->zuk.shield_freeze = 0;

    if (s->start_wave == INF_START_WAVE_ZUK_JAD) {
        s->zuk.set_timer = 247;
        s->zuk.enraged = 0;
        s->zuk.healer_spawned = 0;
        zuk->hp = 479;
        zuk->attack_timer = INF_NPC_STATS[INF_NPC_ZUK].attack_speed;
        s->min_zuk_hp_seen = (float)zuk->hp;

        int j_slot = inf_find_free_npc(s);
        if (j_slot >= 0) {
            inf_init_npc(s, j_slot, INF_NPC_JAD, 24, 32);
            s->npcs[j_slot].aggro_target = shield_idx;
            s->npcs[j_slot].stun_timer = 7;
        }
        return;
    }

    s->zuk.set_timer = s->zuk.set_interval;
    s->zuk.enraged = 1;
    s->zuk.healer_spawned = 1;
    zuk->hp = 239;
    zuk->attack_timer = 7;
    s->min_zuk_hp_seen = (float)zuk->hp;

    static const int healer_pos[4][2] = {
        {16, 48}, {20, 48}, {30, 48}, {34, 48}
    };
    for (int h = 0; h < 4; h++) {
        int slot = inf_find_free_npc(s);
        if (slot >= 0) {
            inf_init_npc(s, slot, INF_NPC_HEALER_ZUK,
                healer_pos[h][0], healer_pos[h][1]);
            s->npcs[slot].aggro_target = zuk_idx;
        }
    }
}

static void inf_spawn_wave(InfernoState* s) {
    if (s->wave >= INF_NUM_WAVES) return;

    const InfWaveDef* w = &INF_WAVES[s->wave];

    /* clear all NPCs and pending hits */
    for (int i = 0; i < INF_MAX_NPCS; i++) s->npcs[i].active = 0;
    s->player_pending_hit_count = 0;
    memset(s->npc_collision_flags, 0, sizeof(s->npc_collision_flags));
    inf_rebuild_player_collision_flags(s);

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

    /* waves 67-69 have no pillars — ref InfernoRegion.ts:359
       (`this.wave < 67 || this.wave >= 70` means pillars exist outside this range).
       clear on spawn so mid-episode transitions also collapse any survivors. */
    if (s->wave >= 66) {
        int pillars_changed = 0;
        for (int p = 0; p < INF_NUM_PILLARS; p++) {
            if (s->pillars[p].active) {
                s->pillars[p].active = 0;
                s->pillars[p].hp = 0;
                pillars_changed = 1;
            }
        }
        if (pillars_changed) inf_rebuild_los(s);
    }

    /* wave 67 (index 66): single jad at fixed position. ref InfernoRegion.ts:441-451.
       our coord system is Y-flipped vs reference (pillars confirm ours_y = 57 - ref_y).
       ref: player (18, 25), jad (23, 27), stun=1, attackSpeed=8, healers=5. */
    if (s->wave == 66) {
        s->player.x = 18;
        s->player.y = 32;  /* 57 - 25 */
        inf_rebuild_player_collision_flags(s);
        int slot = inf_find_free_npc(s);
        if (slot >= 0) {
            inf_init_npc(s, slot, INF_NPC_JAD, 23, 30);  /* 57 - 27 = 30 */
            s->npcs[slot].stun_timer = 1;
            s->npcs[slot].attack_timer = 8;
        }
        return;
    }

    /* wave 68 (index 67): three jads at fixed positions with staggered first attacks.
       ref InfernoRegion.ts:452-479. stunTimers [1,4,7] shuffled across the 3 jads.
       This sim decrements attack_timer during stun, so the initial attack timer
       includes the stun offset to preserve the reference first-attack cadence.
       ref: player (25, 27), jads (18, 24), (28, 24), (23, 35). attackSpeed=9, healers=3. */
    if (s->wave == 67) {
        s->player.x = 25;
        s->player.y = 30;  /* 57 - 27 */
        inf_rebuild_player_collision_flags(s);
        /* shuffle [1, 4, 7] via Fisher-Yates */
        int stuns[3] = { 1, 4, 7 };
        for (int i = 2; i > 0; i--) {
            int j = encounter_rand_int(&s->rng_state, i + 1);
            int tmp = stuns[i]; stuns[i] = stuns[j]; stuns[j] = tmp;
        }
        static const int JAD_POS[3][2] = {
            {18, 33},  /* 57 - 24 */
            {28, 33},  /* 57 - 24 */
            {23, 22},  /* 57 - 35 */
        };
        for (int i = 0; i < 3; i++) {
            int slot = inf_find_free_npc(s);
            if (slot < 0) break;
            inf_init_npc(s, slot, INF_NPC_JAD, JAD_POS[i][0], JAD_POS[i][1]);
            s->npcs[slot].stun_timer = stuns[i];
            s->npcs[slot].attack_timer =
                inf_initial_attack_timer_after_stun(9, stuns[i]);
        }
        return;
    }

    /* zuk wave (wave 69, index 68) is special */
    if (s->wave == 68) {
        /* spawn Zuk — fixed position, cannot move */
        int zuk_idx = inf_find_free_npc(s);
        if (zuk_idx >= 0) {
            inf_init_npc(s, zuk_idx, INF_NPC_ZUK, INF_ZUK_X, INF_ZUK_Y);
            s->min_zuk_hp_seen = (float)s->npcs[zuk_idx].hp;
            /* InfernoTrainer: stunned=8, attackDelay=14. stun counts down first,
               then attackDelay ticks down to 0 before first attack fires. */
            s->npcs[zuk_idx].stun_timer = 8;
            s->npcs[zuk_idx].attack_timer = 14;
        }

        /* spawn shield */
        int shield_idx = inf_find_free_npc(s);
        if (shield_idx >= 0) {
            inf_init_npc(s, shield_idx, INF_NPC_ZUK_SHIELD, INF_ZUK_SHIELD_X, INF_ZUK_SHIELD_Y);
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
        s->zuk.timer_paused = 0;
        s->zuk.has_paused = 0;

        /* player starts at zuk position */
        s->player.x = INF_ZUK_PLAYER_START_X;
        s->player.y = INF_ZUK_PLAYER_START_Y;
        inf_rebuild_player_collision_flags(s);
        inf_seed_joseph_zuk_checkpoint(s);
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

static int inf_npc_collision_flag_blocked(InfernoState* s, int x, int y, int size) {
    for (int dx = 0; dx < size; dx++) {
        for (int dy = 0; dy < size; dy++) {
            int gx, gy;
            if (inf_grid_index(x + dx, y + dy, &gx, &gy) &&
                s->npc_collision_flags[gx][gy])
                return 1;
        }
    }
    return 0;
}

static int inf_player_collision_flag_blocked(InfernoState* s, int x, int y, int size) {
    for (int dx = 0; dx < size; dx++) {
        for (int dy = 0; dy < size; dy++) {
            int gx, gy;
            if (inf_grid_index(x + dx, y + dy, &gx, &gy) &&
                s->player_collision_flags[gx][gy])
                return 1;
        }
    }
    return 0;
}

/* NPC movement blocked callback for encounter_npc_step_toward. */
typedef struct { InfernoState* s; int self_idx; } InfMoveCtx;

static int inf_npc_environment_blocked(InfernoState* s, int x, int y, int size) {
    if (!inf_in_arena(x, y)) return 1;
    if (inf_blocked_by_pillar(s, x, y, size)) return 1;
    if (s->collision_map &&
        !collision_tile_walkable(s->collision_map, 0,
            x + s->world_offset_x, y + s->world_offset_y))
        return 1;
    return 0;
}

static int inf_npc_blocked(void* ctx, int x, int y, int size) {
    InfMoveCtx* mc = (InfMoveCtx*)ctx;
    InfernoState* s = mc->s;
    (void)mc->self_idx;
    if (inf_npc_environment_blocked(s, x, y, size)) return 1;
    if (inf_player_collision_flag_blocked(s, x, y, size)) return 1;
    return inf_npc_collision_flag_blocked(s, x, y, size);
}

static int inf_npc_blocked_ignore_player(void* ctx, int x, int y, int size) {
    InfMoveCtx* mc = (InfMoveCtx*)ctx;
    InfernoState* s = mc->s;
    (void)mc->self_idx;
    if (inf_npc_environment_blocked(s, x, y, size)) return 1;
    return inf_npc_collision_flag_blocked(s, x, y, size);
}

static int inf_npc_overlap_hold(void* ctx) {
    const InfMoveCtx* mc = (const InfMoveCtx*)ctx;
    const InfernoState* s = mc->s;
    return s->player_last_interaction_age == 0 &&
           s->player_last_interaction_target_slot == mc->self_idx;
}

/* forward declaration — defined after potions/food section */
static int inf_tile_walkable(void* ctx, int x, int y);

static int inf_npc_terrain_blocked(InfernoState* s, int x, int y, int size) {
    if (!inf_in_arena(x, y)) return 1;
    if (inf_blocked_by_pillar(s, x, y, size)) return 1;
    if (!s->collision_map) return 0;
    for (int dx = 0; dx < size; dx++) {
        for (int dy = 0; dy < size; dy++) {
            if (!collision_tile_walkable(
                    s->collision_map, 0,
                    x + dx + s->world_offset_x,
                    y + dy + s->world_offset_y)) {
                return 1;
            }
        }
    }
    return 0;
}

static void inf_npc_move(InfernoState* s, int idx) {
    InfNPC* npc = &s->npcs[idx];
    if (!npc->active) return;
    if (npc->stun_timer > 0) return;
    if (npc->dig_freeze_timer > 0) return;
    if (npc->frozen_ticks > 0) return;

    const InfNPCStats* stats = &INF_NPC_STATS[npc->type];
    if (!stats->can_move) return;
    int uses_collision_flag = inf_npc_sets_collision_flag(npc->type);
    if (uses_collision_flag)
        inf_unstamp_npc_collision_footprint(s, npc->x, npc->y, npc->size);

    /* OSRS: NPC shuffles off player tile when overlapping (Mob.ts:109-153).
       if the NPC steps out, skip further movement this tick. */
    if (npc->type != INF_NPC_NIBBLER) {
        InfMoveCtx mc = { s, idx };
        int stepped = encounter_npc_step_out_from_under(
            &npc->x, &npc->y, npc->size,
            s->player.x, s->player.y,
            inf_npc_blocked_ignore_player, &mc, inf_npc_overlap_hold, &s->rng_state);
        if (stepped == ENCOUNTER_NPC_UNDER_PLAYER_MOVED) {
            npc->moved_this_tick = 1;
            inf_invalidate_npc_los_cache(s, idx);
            if (uses_collision_flag)
                inf_stamp_npc_collision_footprint(s, npc->x, npc->y, npc->size);
            return;
        }
        if (stepped == ENCOUNTER_NPC_UNDER_PLAYER_HELD) {
            if (uses_collision_flag)
                inf_stamp_npc_collision_footprint(s, npc->x, npc->y, npc->size);
            return;
        }
    }

    InfTargetArea target = inf_npc_current_target_area(s, npc);
    if (target.is_player && npc->aggro_target >= 0) npc->aggro_target = -1;
    int tx = target.x;
    int ty = target.y;
    int target_size = target.size;
    npc->target_x = tx;
    npc->target_y = ty;

    /* NPCs stop moving once they can attack their current target.
       reference: InfernoTrainer Unit.ts:383 canMove = !hasLOS (where
       hasLOS is relative to the NPC's current aggro target). */
    if (npc->type != INF_NPC_NIBBLER) {
        int has_los = inf_npc_has_los_direct(s, idx);
        s->npc_los_cache[idx] = (int8_t)has_los;
        if (has_los) {
            if (uses_collision_flag)
                inf_stamp_npc_collision_footprint(s, npc->x, npc->y, npc->size);
            return;
        }
    }

    int ox = npc->x, oy = npc->y;
    InfMoveCtx mc = { s, idx };
    encounter_npc_step_toward(&npc->x, &npc->y, tx, ty, npc->size,
                              target_size, stats->attack_range == 1,
                              inf_npc_blocked, &mc);
    if (npc->x != ox || npc->y != oy) {
        npc->moved_this_tick = 1;
        inf_invalidate_npc_los_cache(s, idx);
    }
    if (uses_collision_flag)
        inf_stamp_npc_collision_footprint(s, npc->x, npc->y, npc->size);
}


/* meleer digs when no LOS for 38+ ticks, 10% per tick, forced at 50 */
static void inf_meleer_dig_check(InfernoState* s, int idx) {
    InfNPC* npc = &s->npcs[idx];
    if (npc->type != INF_NPC_MELEER || !npc->active) return;
    if (npc->dig_freeze_timer > 0) {
        npc->dig_freeze_timer--;
        if (npc->dig_freeze_timer == 0 && npc->dig_attack_delay == 0) {
            /* emerge: use the reference ordered landing candidates around the
               player, then fall back to the default NW corner if all preferred
               tiles are blocked by arena terrain/entities. */
            int ox = npc->x, oy = npc->y;
            if (inf_npc_sets_collision_flag(npc->type))
                inf_unstamp_npc_collision_footprint(s, ox, oy, npc->size);
            int candidates[5][2] = {
                { s->player.x - npc->size + 1, s->player.y - npc->size + 1 },
                { s->player.x,                 s->player.y                 },
                { s->player.x - npc->size + 1, s->player.y                 },
                { s->player.x,                 s->player.y - npc->size + 1 },
                { s->player.x - 1,             s->player.y - 1             },
            };
            int landing_x = candidates[4][0];
            int landing_y = candidates[4][1];
            for (int i = 0; i < 4; i++) {
                if (inf_npc_terrain_blocked(s, candidates[i][0], candidates[i][1], npc->size))
                    continue;
                landing_x = candidates[i][0];
                landing_y = candidates[i][1];
                break;
            }
            npc->x = landing_x;
            npc->y = landing_y;
            if (inf_npc_sets_collision_flag(npc->type))
                inf_stamp_npc_collision_footprint(s, npc->x, npc->y, npc->size);
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

static AttackStyle inf_player_equipped_attack_style(const InfernoState* s) {
    uint8_t weapon = s->player.equipped[GEAR_SLOT_WEAPON];
    AttackStyle style = (AttackStyle)get_item_attack_style(weapon);
    if (style == ATTACK_STYLE_MAGIC ||
        style == ATTACK_STYLE_RANGED ||
        style == ATTACK_STYLE_MELEE) {
        return style;
    }
    return ATTACK_STYLE_RANGED;
}

static void inf_refresh_human_loadout_stats(InfernoState* s) {
    AttackStyle style = inf_player_equipped_attack_style(s);
    int spell_base_damage = (style == ATTACK_STYLE_MAGIC) ? 30 : 0;
    encounter_compute_player_equipped_stats(
        &s->player, style, s->player.fight_style, spell_base_damage,
        &s->human_loadout_stats);
}

static const EncounterLoadoutStats* inf_current_loadout_stats(InfernoState* s) {
    if (s->human_command_mode) {
        inf_refresh_human_loadout_stats(s);
        return &s->human_loadout_stats;
    }
    return &s->loadout_stats[s->weapon_set];
}

static int inf_player_weapon_is(const InfernoState* s, uint8_t item) {
    return s->player.equipped[GEAR_SLOT_WEAPON] == item;
}

typedef struct {
    EncounterLoadoutStats stats;
    int spell;
    int is_barrage;
    uint8_t weapon;
} InfPlayerAttack;

static int inf_is_barrage_spell(int spell) {
    return spell == ENCOUNTER_SPELL_BLOOD || spell == ENCOUNTER_SPELL_ICE;
}

static int inf_spell_magic_level_requirement(int spell) {
    if (spell == ENCOUNTER_SPELL_ICE) return ICE_BARRAGE_LEVEL;
    if (spell == ENCOUNTER_SPELL_BLOOD) return BLOOD_BARRAGE_LEVEL;
    fprintf(stderr, "BUG: invalid Inferno spell %d\n", spell);
    abort();
}

static int inf_spell_action_to_spell(int action_spell) {
    if (action_spell == 1) return ENCOUNTER_SPELL_BLOOD;
    if (action_spell == 2) return ENCOUNTER_SPELL_ICE;
    return ENCOUNTER_SPELL_NONE;
}

static int inf_spell_base_damage(int spell) {
    if (!inf_is_barrage_spell(spell)) {
        fprintf(stderr, "BUG: invalid Inferno spell %d\n", spell);
        abort();
    }
    return 30;
}

static int inf_item_supports_autocast(uint8_t item) {
    return item == ITEM_KODAI_WAND;
}

static int inf_player_autocast_spell(const Player* p) {
    return p->autocast_spell == ENCOUNTER_SPELL_ICE
        ? ENCOUNTER_SPELL_ICE
        : ENCOUNTER_SPELL_BLOOD;
}

static FightStyle inf_player_spell_fight_style(const Player* p) {
    return (p->autocast_defensive || p->fight_style == FIGHT_STYLE_DEFENSIVE_AUTOCAST)
        ? FIGHT_STYLE_DEFENSIVE_AUTOCAST
        : FIGHT_STYLE_AUTOCAST;
}

static int inf_autocast_is_active(const InfernoState* s) {
    return s->player.autocast_enabled &&
        inf_item_supports_autocast(s->player.equipped[GEAR_SLOT_WEAPON]);
}

static void inf_compute_manual_spell_stats(
    InfernoState* s,
    int spell,
    EncounterLoadoutStats* out
) {
    encounter_compute_player_equipped_stats(
        &s->player,
        ATTACK_STYLE_MAGIC,
        inf_player_spell_fight_style(&s->player),
        inf_spell_base_damage(spell),
        out);
    out->attack_speed = 5;
    out->attack_range = 10;
}

static int inf_resolve_player_attack(
    InfernoState* s,
    int manual_spell,
    InfPlayerAttack* out
) {
    memset(out, 0, sizeof(*out));
    out->spell = ENCOUNTER_SPELL_NONE;
    out->weapon = s->player.equipped[GEAR_SLOT_WEAPON];

    if (inf_is_barrage_spell(manual_spell)) {
        if (s->player.current_magic < inf_spell_magic_level_requirement(manual_spell))
            return 0;
        inf_compute_manual_spell_stats(s, manual_spell, &out->stats);
        out->spell = manual_spell;
        out->is_barrage = 1;
        return 1;
    }

    const EncounterLoadoutStats* ls = inf_current_loadout_stats(s);
    out->stats = *ls;

    if (ls->style != ATTACK_STYLE_MAGIC)
        return 1;

    if (!inf_autocast_is_active(s))
        return 0;

    int autocast_spell = inf_player_autocast_spell(&s->player);
    if (s->player.current_magic < inf_spell_magic_level_requirement(autocast_spell))
        return 0;

    out->spell = autocast_spell;
    out->is_barrage = 1;
    out->stats.attack_speed = 5;
    out->stats.attack_range = 10;
    return 1;
}

static int inf_player_has_any_barrage_spell_available(const InfernoState* s) {
    return s->player.current_magic >= BLOOD_BARRAGE_LEVEL;
}

static int inf_npc_type_can_be_tagged_off_shield(int type) {
    return type == INF_NPC_JAD ||
        type == INF_NPC_RANGER ||
        type == INF_NPC_MAGER;
}

static int inf_is_live_shield_slot(const InfernoState* s, int npc_idx) {
    if (npc_idx < 0 || npc_idx >= INF_MAX_NPCS) return 0;
    const InfNPC* npc = &s->npcs[npc_idx];
    return npc->active &&
        npc->death_ticks == 0 &&
        npc->hp > 0 &&
        npc->type == INF_NPC_ZUK_SHIELD;
}

static int inf_is_shield_taggable_slot(const InfernoState* s, int npc_idx) {
    if (npc_idx < 0 || npc_idx >= INF_MAX_NPCS) return 0;
    const InfNPC* npc = &s->npcs[npc_idx];
    return npc->active &&
        npc->death_ticks == 0 &&
        npc->hp > 0 &&
        inf_npc_type_can_be_tagged_off_shield(npc->type) &&
        inf_is_live_shield_slot(s, npc->aggro_target);
}

static int inf_player_behind_zuk_shield_now(const InfernoState* s) {
    int shield_idx = s->zuk.shield_idx;
    if (shield_idx < 0 || shield_idx >= INF_MAX_NPCS) return 0;

    const InfNPC* shield = &s->npcs[shield_idx];
    if (!shield->active || shield->death_ticks != 0 || shield->hp <= 0)
        return 0;

    int shield_size = INF_NPC_STATS[INF_NPC_ZUK_SHIELD].size;
    return s->player.x >= shield->x &&
        s->player.x < shield->x + shield_size &&
        s->player.y >= 41;
}

static int inf_is_untagged_live_zuk_healer_slot(
    const InfernoState* s,
    int npc_idx
) {
    if (npc_idx < 0 || npc_idx >= INF_MAX_NPCS) return 0;

    const InfNPC* npc = &s->npcs[npc_idx];
    if (!npc->active || npc->death_ticks != 0 || npc->hp <= 0 ||
            npc->type != INF_NPC_HEALER_ZUK)
        return 0;

    if (npc->aggro_target < 0 || npc->aggro_target >= INF_MAX_NPCS)
        return 0;

    const InfNPC* aggro = &s->npcs[npc->aggro_target];
    return aggro->active && aggro->type == INF_NPC_ZUK;
}

static int inf_player_can_attack_npc_from_current_tile(
    const InfernoState* s,
    int npc_idx
) {
    if (npc_idx < 0 || npc_idx >= INF_MAX_NPCS) return 0;

    const InfNPC* npc = &s->npcs[npc_idx];
    if (!npc->active || npc->death_ticks != 0 || npc->hp <= 0) return 0;

    const EncounterLoadoutStats* ls = &s->loadout_stats[s->weapon_set];
    return encounter_player_can_attack(
        s->player.x,
        s->player.y,
        npc->x,
        npc->y,
        npc->size,
        ls->attack_range,
        s->los_blockers,
        s->los_blocker_count);
}

static int inf_player_can_phantom_barrage_npc(InfernoState* s, int npc_idx) {
    if (npc_idx < 0 || npc_idx >= INF_MAX_NPCS) return 0;

    const InfNPC* npc = &s->npcs[npc_idx];
    if (!npc->active || npc->death_ticks <= 0 || npc->hp > 0 ||
            npc->type == INF_NPC_ZUK_SHIELD)
        return 0;

    InfPlayerAttack attack;
    int has_barrage = inf_resolve_player_attack(s, ENCOUNTER_SPELL_NONE, &attack) &&
        attack.is_barrage;
    if (!has_barrage)
        has_barrage = inf_player_has_any_barrage_spell_available(s);
    if (!has_barrage) return 0;

    return encounter_player_can_attack(
        s->player.x,
        s->player.y,
        npc->x,
        npc->y,
        npc->size,
        10,
        s->los_blockers,
        s->los_blocker_count);
}

static int inf_npc_is_phantom_barrage_obs_candidate(
    InfernoState* s,
    int npc_idx
) {
    if (npc_idx < 0 || npc_idx >= INF_MAX_NPCS) return 0;
    return s->npcs[npc_idx].death_ticks <= 2 &&
        inf_player_can_phantom_barrage_npc(s, npc_idx);
}

static int inf_npc_is_phantom_barrage_cast_window(
    InfernoState* s,
    int npc_idx
) {
    if (npc_idx < 0 || npc_idx >= INF_MAX_NPCS) return 0;
    return s->player.attack_timer <= 1 &&
        s->npcs[npc_idx].death_ticks == 1 &&
        inf_player_can_phantom_barrage_npc(s, npc_idx);
}

static int inf_untagged_zuk_healer_target_is_safe_now(
    const InfernoState* s,
    int npc_idx
) {
    if (!inf_is_untagged_live_zuk_healer_slot(s, npc_idx)) return 1;
    return inf_player_behind_zuk_shield_now(s) &&
        s->player.attack_timer == 0 &&
        inf_player_can_attack_npc_from_current_tile(s, npc_idx);
}

static int inf_is_safe_untagged_zuk_healer_target_now(
    const InfernoState* s,
    int npc_idx
) {
    return inf_is_untagged_live_zuk_healer_slot(s, npc_idx) &&
        inf_untagged_zuk_healer_target_is_safe_now(s, npc_idx);
}

static int inf_has_safe_untagged_zuk_healer_target_now(const InfernoState* s) {
    for (int obs_slot = 0; obs_slot < INF_OBS_NPCS; obs_slot++) {
        int npc_idx = s->current_obs_slots[obs_slot];
        if (npc_idx >= 0 && npc_idx < INF_MAX_NPCS &&
                s->npcs[npc_idx].active &&
                s->npcs[npc_idx].death_ticks == 0 &&
                s->npcs[npc_idx].type != INF_NPC_ZUK_SHIELD &&
                inf_is_safe_untagged_zuk_healer_target_now(s, npc_idx)) {
            return 1;
        }
    }
    return 0;
}

static int inf_npc_target_hit_delay(
    const InfNPC* npc, const InfNPC* target, int style
) {
    int dist = encounter_projectile_distance(
        npc->x, npc->y, npc->size, target->x, target->y, target->size,
        ENCOUNTER_PROJECTILE_DISTANCE_CLOSEST_TILE);
    EncounterProjectileTiming timing = inf_npc_projectile_timing(npc->type, style, dist);
    if (npc->type == INF_NPC_JAD && timing.damage_delay_ticks > 0)
        return INF_JAD_PROJECTILE_DELAY + timing.damage_delay_ticks;
    return timing.damage_delay_ticks;
}

static void inf_apply_npc_target_damage(InfernoState* s, int target_idx, int damage) {
    if (target_idx < 0 || target_idx >= INF_MAX_NPCS) return;
    InfNPC* target = &s->npcs[target_idx];
    if (!target->active) return;

    int target_hp_before = target->hp;
    encounter_damage_npc(&target->hp, &target->hit_landed_this_tick,
                         &target->hit_damage, damage);
    if (target->type == INF_NPC_ZUK_SHIELD)
        s->shield_damage_this_tick += (float)(target_hp_before - target->hp);

    if (target->hp <= 0 && target->type == INF_NPC_ZUK_SHIELD) {
        inf_deactivate_npc(s, target_idx);
        s->zuk.shield_idx = -1;
        for (int i = 0; i < INF_MAX_NPCS; i++) {
            if (s->npcs[i].aggro_target == target_idx)
                s->npcs[i].aggro_target = -1;
        }
    }
}

static void inf_queue_npc_target_hit(
    InfernoState* s, int target_idx, int damage, int ticks_remaining
) {
    if (ticks_remaining <= 0) {
        inf_apply_npc_target_damage(s, target_idx, damage);
        return;
    }
    for (int i = 0; i < INF_MAX_NPC_TARGET_HITS; i++) {
        InfNpcTargetHit* hit = &s->npc_target_hits[i];
        if (hit->active) continue;
        hit->active = 1;
        hit->target_idx = target_idx;
        hit->damage = damage;
        hit->ticks_remaining = ticks_remaining;
        return;
    }
    fprintf(stderr, "FATAL: Inferno NPC target hit queue overflow\n");
    abort();
}

static void inf_resolve_npc_target_hits(InfernoState* s) {
    for (int i = 0; i < INF_MAX_NPC_TARGET_HITS; i++) {
        InfNpcTargetHit* hit = &s->npc_target_hits[i];
        if (!hit->active) continue;
        hit->ticks_remaining--;
        if (hit->ticks_remaining > 0) continue;
        inf_apply_npc_target_damage(s, hit->target_idx, hit->damage);
        hit->active = 0;
    }
}

static int inf_apply_elysian_to_player_hit(InfernoState* s, int damage, int* reduced) {
    *reduced = 0;
    if (damage <= 0) return damage;
    osrs_ensure_player_equipment(&s->player);
    if (!osrs_effect_profile_has(
            &s->player.equipment_effect_profile, OSRS_ITEM_EFFECT_ELYSIAN))
        return damage;
    if (encounter_rand_int(&s->rng_state, 10) >= 7)
        return damage;
    *reduced = 1;
    return damage * 75 / 100;
}


static void inf_npc_attack(InfernoState* s, int idx) {
    InfNPC* npc = &s->npcs[idx];
    if (!npc->active) return;
    const InfNPCStats* stats = &INF_NPC_STATS[npc->type];
    if (npc->attack_timer > 0) npc->attack_timer--;
    if (npc->stun_timer > 0) { npc->stun_timer--; return; }
    if (npc->dig_freeze_timer > 0) return;
    if (npc->dig_attack_delay > 0) return;

    int has_los_now = 0;
    if (stats->attack_range > 1) {
        has_los_now = inf_npc_has_los(s, idx);
    }

    if (npc->type == INF_NPC_BLOB &&
        npc->blob_scanned_prayer < 0 &&
        has_los_now &&
        !npc->had_los_last_tick) {
        npc->blob_scanned_prayer = (int)s->player.prayer;
        /* commit to opposite of scanned prayer; random if scanned prayer is neither */
        if (s->player.prayer == PRAYER_PROTECT_MAGIC) npc->attack_style = ATTACK_STYLE_RANGED;
        else if (s->player.prayer == PRAYER_PROTECT_RANGED) npc->attack_style = ATTACK_STYLE_MAGIC;
        else npc->attack_style = (encounter_rand_int(&s->rng_state, 2) == 0) ? ATTACK_STYLE_MAGIC : ATTACK_STYLE_RANGED;
        npc->had_los_last_tick = has_los_now;
        npc->attacked_this_tick = 1;
        npc->attack_timer = stats->attack_speed;
        return;
    }

    npc->had_los_last_tick = has_los_now;

    if (npc->attack_timer > 0) return;

    /* shield doesn't attack */
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
                    inf_invalidate_los_cache(s);
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
                            /* pillar collapse: 49 damage (observed in-game, server-side formula unknown) */
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

    /* zuk healer: heal Zuk while it is untapped, switch to player-facing
       sparks after tag. */
    if (npc->type == INF_NPC_HEALER_ZUK) {
        if (npc->aggro_target >= 0) {
            /* heal Zuk with a 3-tick projectile */
            int ti = npc->aggro_target;
            if (ti >= 0 && ti < INF_MAX_NPCS && s->npcs[ti].active && s->npcs[ti].type == INF_NPC_ZUK) {
                npc->heal_target = ti;
                npc->heal_timer = 3;
                npc->attack_visual_target = ti;
            }
        } else {
            /* tagged healer: stop healing and fire the 3-spark ground pattern. */
            npc->heal_target = -1;
            npc->heal_timer = 0;
            npc->attack_visual_target = -1;
            inf_queue_zuk_healer_sparks(s, npc);
        }
        npc->attacked_this_tick = 1;
        npc->attack_style_this_tick = ATTACK_STYLE_MAGIC;
        npc->attack_timer = stats->attack_speed;
        return;
    }

    /* jad healer: heals its Jad while aggro_target points at the boss, then
       switches to crush melee after a player tag. */
    if (npc->type == INF_NPC_HEALER_JAD) {
        int jad_idx = npc->jad_owner_idx;
        if (npc->aggro_target >= 0) {
            if (jad_idx >= 0 && s->npcs[jad_idx].active &&
                s->npcs[jad_idx].type == INF_NPC_JAD) {
                /* heal Jad with the reference 3-tick delay. */
                npc->heal_target = jad_idx;
                npc->heal_timer = 3;
                npc->attack_visual_target = jad_idx;
            }
            npc->attacked_this_tick = 1;
            npc->attack_timer = stats->attack_speed;
            return;
        }
        /* if player has tagged this healer, it only attacks on cardinal melee
           contact. diagonal corners do not count. */
        if (entity_has_line_of_sight(
                s->los_blockers, s->los_blocker_count,
                npc->x, npc->y, 1,
                s->player.x, s->player.y, 1,
                1)) {
            int max_hit = osrs_npc_melee_max_hit(stats->str_level, stats->melee_str_bonus);
            int dmg = encounter_rand_int(&s->rng_state, max_hit + 1);
            /* accuracy roll */
            int att_roll = osrs_npc_attack_roll(stats->att_level, stats->melee_att_bonus);
            const EncounterLoadoutStats* ls = inf_current_loadout_stats(s);
            int def_bonus = ls->def_crush;
            int def_roll = osrs_player_def_roll_vs_npc(s->player.current_defence, s->player.current_magic, def_bonus, ATTACK_STYLE_MELEE);
            if (encounter_rand_float(&s->rng_state) >= osrs_hit_chance(att_roll, def_roll)) dmg = 0;
            int prayer_matches = (s->player.prayer == PRAYER_PROTECT_MELEE);
              if (prayer_matches) { dmg = 0; s->prayer_correct_this_tick = 1; }
              else if (dmg > 0) { s->off_prayer_hits_this_tick++; }
            encounter_damage_player(&s->player, dmg, &s->damage_received_this_tick);
            npc->attack_style_this_tick = ATTACK_STYLE_MELEE;
            npc->attacked_this_tick = 1;
            npc->attack_visual_target = -1;
            npc->attack_timer = stats->attack_speed;
        }
        return;
    }

    if (npc->aggro_target >= 0 && npc->aggro_target < INF_MAX_NPCS) {
        InfNPC* target = &s->npcs[npc->aggro_target];
        if (target->active) {
            int actual_style = npc->attack_style;
            if (npc->type == INF_NPC_JAD) {
                actual_style = npc->jad_attack_style;
                if (actual_style == ATTACK_STYLE_NONE)
                    actual_style = inf_jad_roll_primary_style(&s->rng_state);
            }
            int max_hit = osrs_npc_max_hit(actual_style,
                stats->str_level, stats->range_level,
                stats->melee_str_bonus, stats->ranged_str_bonus,
                stats->magic_base_dmg, stats->magic_dmg_pct);
            if (stats->max_hit_cap > 0 && max_hit > stats->max_hit_cap)
                max_hit = stats->max_hit_cap;
            int dmg = encounter_rand_int(&s->rng_state, max_hit + 1);
            int hit_delay = inf_npc_target_hit_delay(npc, target, actual_style);
            inf_queue_npc_target_hit(s, npc->aggro_target, dmg, hit_delay);
            npc->attacked_this_tick = 1;
            npc->attack_style_this_tick = actual_style;
            npc->attack_visual_target = npc->aggro_target;
            npc->attack_timer = stats->attack_speed;
            if (npc->type == INF_NPC_JAD) {
                npc->jad_attack_style = ATTACK_STYLE_NONE;
                if (s->wave == 66)      npc->attack_timer = 8;
                else if (s->wave == 67) npc->attack_timer = 9;
                else                    npc->attack_timer = 8;
            }
            return;
        }
        npc->aggro_target = -1;
    }

    /* ranged/magic NPCs need LOS, except blobs once they have already stored
       a prayer scan. ref: InfernoTrainer JalAk.ts attackIfPossible(). */
    if (stats->attack_range > 1 &&
        !(npc->type == INF_NPC_BLOB && npc->blob_scanned_prayer >= 0) &&
        !has_los_now) return;

    /* compute distance to player */
    int dist = encounter_dist_to_npc(s->player.x, s->player.y,
                                      npc->x, npc->y, npc->size);
    if (dist == 0 || dist > stats->attack_range) return;

    /* blob prayer reading: 2-phase attack with attack_speed = 3.
       scan tick: read player prayer, set timer, return (shows scan animation).
       fire tick: determine style from scanned prayer, fall through to common attack.
       total cycle = 6 ticks (3 scan + 3 cooldown).
       ref: InfernoTrainer JalAk.ts attackIfPossible() */
    if (npc->type == INF_NPC_BLOB) {
        if (npc->blob_scanned_prayer < 0) {
            /* no pending scan → start scan phase: read prayer, commit to opposite style */
            npc->blob_scanned_prayer = (int)s->player.prayer;
            if (s->player.prayer == PRAYER_PROTECT_MAGIC) npc->attack_style = ATTACK_STYLE_RANGED;
            else if (s->player.prayer == PRAYER_PROTECT_RANGED) npc->attack_style = ATTACK_STYLE_MAGIC;
            else npc->attack_style = (encounter_rand_int(&s->rng_state, 2) == 0) ? ATTACK_STYLE_MAGIC : ATTACK_STYLE_RANGED;
            npc->attacked_this_tick = 1;  /* triggers scan animation */
            npc->attack_timer = stats->attack_speed;  /* 3 */
            return;
        }
        /* has pending scan → determine style and fall through to fire */
        npc->blob_scanned_prayer = -1;
        /* fall through to common attack code */
    }

    /* determine actual attack style */
    int actual_style = npc->attack_style;

    /* jad: use the committed preview style if present. if the preview was not
       seeded, fall back to the 50/50 primary-style roll. */
    if (npc->type == INF_NPC_JAD) {
        actual_style = npc->jad_attack_style;
        if (actual_style == ATTACK_STYLE_NONE)
            actual_style = inf_jad_roll_primary_style(&s->rng_state);
    }

    /* zuk: typeless attack (not blockable by prayer).
       InfernoTrainer TzKalZuk.ts: Zuk always fires at the shield if the player is
       behind it (shield absorbs, 0 damage). otherwise fires at the player.
       hit delay = 4 ticks (ZukWeapon: setDelay=4). */
    if (npc->type == INF_NPC_ZUK) {
        int si = s->zuk.shield_idx;
        int player_behind_shield = 0;
        if (si >= 0 && s->npcs[si].active) {
            InfNPC* shield = &s->npcs[si];
            player_behind_shield = (s->player.x >= shield->x &&
                                    s->player.x < shield->x + shield->size &&
                                    s->player.y >= 41);
        }

        if (player_behind_shield) {
            /* shield absorbs — Zuk fires at shield, 0 damage */
            npc->attacked_this_tick = 1;
            npc->attack_style_this_tick = ATTACK_STYLE_MAGIC;
            npc->attack_visual_target = si;
        } else {
            /* typeless hit on player — not blockable by prayer, no accuracy roll.
               rolled 0..max_hit, queued at T and lands at T+4 unchanged. */
            int max_hit = osrs_npc_magic_max_hit(stats->magic_base_dmg, stats->magic_dmg_pct);
            int dmg = encounter_rand_int(&s->rng_state, max_hit + 1);
            if (s->player_pending_hit_count < ENCOUNTER_MAX_PENDING_HITS) {
                EncounterPendingHit* ph = &s->player_pending_hits[s->player_pending_hit_count++];
                ph->active = 1;
                ph->damage = dmg;
                ph->ticks_remaining = 4;
                ph->attack_style = ATTACK_STYLE_NONE;  /* typeless — not blockable */
                ph->check_prayer = 0;
                ph->prayer_check_delay = 0;
                ph->spell_type = 0;
                ph->source_npc_type = npc->type;
                ph->hit_success = 1;
                ph->elysian_reduced = 0;
            }
            s->last_hit_by_type = INF_NPC_ZUK;
            npc->attacked_this_tick = 1;
            npc->attack_style_this_tick = ATTACK_STYLE_MAGIC;
            /* attack_visual_target = -1 (player), already default */
        }
        npc->attack_timer = s->zuk.enraged ? 7 : stats->attack_speed;
        return;
    }

    {
        int style_mask = inf_attack_style_options_mask(
            s, npc, stats, actual_style, dist);
        actual_style = inf_choose_attack_style_for_tick(
            &s->rng_state, style_mask);
    }

    if (npc->type == INF_NPC_MAGER &&
        actual_style == ATTACK_STYLE_MAGIC &&
        inf_mager_resurrect(s, idx)) {
        npc->attacked_this_tick = 1;
        npc->attack_style_this_tick = ATTACK_STYLE_NONE;
        npc->attack_timer = stats->attack_speed;
        return;
    }

    /* max hit from stats + bonuses via OSRS combat formulas */
    int max_hit = osrs_npc_max_hit(actual_style,
        stats->str_level, stats->range_level,
        stats->melee_str_bonus, stats->ranged_str_bonus,
        stats->magic_base_dmg, stats->magic_dmg_pct);
    if (stats->max_hit_cap > 0 && max_hit > stats->max_hit_cap)
        max_hit = stats->max_hit_cap;
    int is_delayed_jad = (npc->type == INF_NPC_JAD &&
                          actual_style != ATTACK_STYLE_MELEE);
    int dmg = is_delayed_jad ? max_hit : encounter_rand_int(&s->rng_state, max_hit + 1);
    int accuracy_hit = 1;

    /* accuracy roll: NPC attack roll vs player defence roll */
    if (!is_delayed_jad) {
        int att_lvl, att_bonus;
        if (actual_style == ATTACK_STYLE_MELEE) {
            att_lvl = stats->att_level; att_bonus = stats->melee_att_bonus;
        } else if (actual_style == ATTACK_STYLE_RANGED) {
            att_lvl = stats->range_level; att_bonus = stats->range_att_bonus;
        } else {
            att_lvl = stats->magic_level; att_bonus = stats->magic_att_bonus;
        }
        int att_roll = osrs_npc_attack_roll(att_lvl, att_bonus);
        const EncounterLoadoutStats* ls = inf_current_loadout_stats(s);
        int def_bonus = encounter_player_def_bonus(
            ls->def_stab, ls->def_slash, ls->def_crush, ls->def_magic, ls->def_ranged,
            actual_style, stats->melee_style);
        int def_roll = osrs_player_def_roll_vs_npc(s->player.current_defence, s->player.current_magic, def_bonus, actual_style);
        if (encounter_rand_float(&s->rng_state) >= osrs_hit_chance(att_roll, def_roll)) {
            dmg = 0;  /* missed */
            accuracy_hit = 0;
        }
    }

    EncounterProjectileTiming hit_timing =
        inf_npc_projectile_timing(npc->type, actual_style, dist);
    int hit_delay = hit_timing.damage_delay_ticks;
    if (is_delayed_jad)
        hit_delay += INF_JAD_PROJECTILE_DELAY;

    /* track which styles fired this tick for multi-style analysis */
    if (actual_style == ATTACK_STYLE_MELEE) s->tick_styles_fired |= 1;
    else if (actual_style == ATTACK_STYLE_RANGED) s->tick_styles_fired |= 2;
    else if (actual_style == ATTACK_STYLE_MAGIC) s->tick_styles_fired |= 4;
    s->tick_attacks_fired++;
    s->attacks_by_type[npc->type]++;

    /* bat (JalMejRah): drain 3 run energy (300 internal) on every attack.
       ref: OSRS wiki Jal-MejRah, InfernoTrainer JalMejRah.ts */
    if (npc->type == INF_NPC_BAT) {
        s->player.run_energy -= 300;
        if (s->player.run_energy < 0) s->player.run_energy = 0;
    }

    if (hit_delay == 0) {
        /* melee: instant damage, check prayer now */
        int prayer_matches = encounter_prayer_correct_for_style(s->player.prayer, actual_style);
          if (prayer_matches) { dmg = 0; s->prayer_correct_this_tick++; s->prayer_correct_by_type[npc->type]++; }
          else if (dmg > 0) { s->off_prayer_hits_this_tick++; }
        int elysian_reduced = 0;
        dmg = inf_apply_elysian_to_player_hit(s, dmg, &elysian_reduced);
        if (elysian_reduced) s->player.elysian_proc_this_tick = 1;
        s->dmg_from_type[npc->type] += (float)dmg;
        if (dmg > 0) s->last_hit_by_type = npc->type;
        encounter_damage_player(&s->player, dmg, &s->damage_received_this_tick);
    } else {
            /* ranged/magic: queue pending hit on player */
        if (s->player_pending_hit_count < ENCOUNTER_MAX_PENDING_HITS) {
            int is_jad = (npc->type == INF_NPC_JAD);
            if (!is_jad) {
                int prayer_matches = encounter_prayer_correct_for_style(s->player.prayer, actual_style);
          if (prayer_matches) { dmg = 0; s->prayer_correct_this_tick++; s->prayer_correct_by_type[npc->type]++; }
          else if (dmg > 0) { s->off_prayer_hits_this_tick++; }
            }
            if (!is_delayed_jad) {
                s->dmg_from_type[npc->type] += (float)dmg;
                if (dmg > 0) s->last_hit_by_type = npc->type;
            }
            /* bat stat drain: 50% chance on successful hit when not praying protect
               from missiles, drain all combat stats by 1. ref: OSRS wiki Jal-MejRah */
            if (npc->type == INF_NPC_BAT && dmg > 0 &&
                encounter_rand_int(&s->rng_state, 2) == 0) {
                if (s->player.current_attack > 0) s->player.current_attack--;
                if (s->player.current_strength > 0) s->player.current_strength--;
                if (s->player.current_defence > 0) s->player.current_defence--;
                if (s->player.current_ranged > 0) s->player.current_ranged--;
                if (s->player.current_magic > 0) s->player.current_magic--;
            }
            EncounterPendingHit* ph = &s->player_pending_hits[s->player_pending_hit_count++];
            ph->active = 1;
            ph->damage = dmg;
            ph->ticks_remaining = hit_delay;
            ph->attack_style = actual_style;
            ph->check_prayer = is_jad ? 1 : 0;
            ph->prayer_check_delay = is_jad ? INF_JAD_PROJECTILE_DELAY + 1 : 0;
            ph->spell_type = 0;
            ph->source_npc_type = npc->type;
            ph->hit_success = accuracy_hit;
            ph->elysian_reduced = 0;
        }
    }

    npc->attacked_this_tick = 1;
    npc->attack_style_this_tick = actual_style;
    if (npc->type == INF_NPC_JAD)
        npc->jad_attack_style = ATTACK_STYLE_NONE;
    npc->attack_timer = stats->attack_speed;

    /* jad attack speed varies by wave */
    if (npc->type == INF_NPC_JAD) {
        if (s->wave == 66)      npc->attack_timer = 8;  /* wave 67 */
        else if (s->wave == 67) npc->attack_timer = 9;  /* wave 68 */
        else                    npc->attack_timer = 8;  /* zuk wave */
    }
}


static int inf_find_mager_respawn_tile(
    InfernoState* s, int size, int* out_x, int* out_y
) {
    InfMoveCtx mc = { s, -1 };
    for (int x = 26; x < 33; x++) {
        for (int y = 24; y < 37; y++) {
            if (inf_npc_blocked(&mc, x, y, size)) continue;
            if (encounter_entity_footprints_overlap(x, y, size,
                                                    s->player.x, s->player.y, 1))
                continue;
            *out_x = x;
            *out_y = y;
            return 1;
        }
    }

    *out_x = 21;
    *out_y = 22;
    return !inf_npc_blocked(&mc, *out_x, *out_y, size);
}

/* mager resurrection is only evaluated on a real magic attack opportunity,
   not every NPC tick. */
static int inf_mager_resurrect(InfernoState* s, int idx) {
    InfNPC* npc = &s->npcs[idx];
    if (npc->type != INF_NPC_MAGER || !npc->active) return 0;
    if (s->wave >= 68) return 0;  /* no resurrection during Zuk wave */
    if (npc->resurrect_cooldown > 0) return 0;
    if (s->dead_mob_count == 0) return 0;

    /* 10% chance when the mager converts a ready magic attack into a respawn. */
    if (encounter_rand_int(&s->rng_state, 10) != 0) return 0;

    /* pick a random dead mob */
    int di = encounter_rand_int(&s->rng_state, s->dead_mob_count);
    InfDeadMob* dm = &s->dead_mobs[di];

    int slot = inf_find_free_npc(s);
    if (slot < 0) return 0;

    int rx = 21;
    int ry = 22;
    if (!inf_find_mager_respawn_tile(s, INF_NPC_STATS[dm->type].size, &rx, &ry))
        return 0;

    inf_init_npc(s, slot, dm->type, rx, ry);
    s->npcs[slot].hp = dm->hp;      /* 50% of max HP */
    s->npcs[slot].max_hp = dm->max_hp;
    s->npcs[slot].resurrection_count = 1;
    s->npcs[slot].attack_timer = INF_NPC_STATS[dm->type].attack_speed;

    /* remove from dead store (swap with last) */
    s->dead_mobs[di] = s->dead_mobs[s->dead_mob_count - 1];
    s->dead_mob_count--;

    /* 8-tick cooldown */
    npc->resurrect_cooldown = 8;
    npc->resurrecting_this_tick = 1;
    npc->resurrection_visual_target = slot;
    npc->attack_visual_target = slot;
    return 1;
}


#define INF_JAD_HEALER_MAX_SPAWN_CANDIDATES 165

static void inf_sample_jad_healer_spawn(InfernoState* s, const InfNPC* jad, int* out_x, int* out_y) {
    int min_dx = -5;
    int max_dx = 5;
    int min_dy = -4;
    int max_dy = 10;
    if (s->wave == 68) {
        min_dx = 0;
        max_dx = 5;
        min_dy = 5;
        max_dy = 8;
    }

    int offsets[INF_JAD_HEALER_MAX_SPAWN_CANDIDATES][2];
    int order[INF_JAD_HEALER_MAX_SPAWN_CANDIDATES];
    int count = 0;
    for (int dx = min_dx; dx <= max_dx; dx++) {
        for (int dy = min_dy; dy <= max_dy; dy++) {
            offsets[count][0] = dx;
            offsets[count][1] = dy;
            order[count] = count;
            count++;
        }
    }
    encounter_shuffle(order, count, &s->rng_state);

    for (int i = 0; i < count; i++) {
        int hx = jad->x + offsets[order[i]][0];
        int hy = jad->y + offsets[order[i]][1];
        if (encounter_entity_footprints_overlap(hx, hy, 1, jad->x, jad->y, jad->size))
            continue;
        if (inf_npc_terrain_blocked(s, hx, hy, 1))
            continue;
        *out_x = hx;
        *out_y = hy;
        return;
    }

    fprintf(stderr, "FATAL: no valid Jad healer spawn tile for Jad at (%d,%d) on wave %d\n",
            jad->x, jad->y, s->wave + 1);
    abort();
}

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
        int hx = npc->x;
        int hy = npc->y;
        inf_sample_jad_healer_spawn(s, npc, &hx, &hy);
        inf_init_npc(s, slot, INF_NPC_HEALER_JAD, hx, hy);
        s->npcs[slot].jad_owner_idx = idx;
        s->npcs[slot].aggro_target = idx;
    }
}


static void inf_zuk_tick(InfernoState* s) {
    if (!inf_is_final_wave(s)) return;

    /* find zuk NPC */
    int zuk_idx = inf_find_live_zuk_idx(s);
    if (zuk_idx < 0) return;
    InfNPC* zuk = &s->npcs[zuk_idx];

    /* shield oscillation */
    int si = s->zuk.shield_idx;
    if (si >= 0 && s->npcs[si].active) {
        InfNPC* shield = &s->npcs[si];
        if (s->zuk.shield_freeze > 0) {
            s->zuk.shield_freeze--;
        } else {
            int ox = shield->x;
            int oy = shield->y;
            if (inf_npc_sets_collision_flag(shield->type))
                inf_unstamp_npc_collision_footprint(s, ox, oy, shield->size);
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
            if (inf_npc_sets_collision_flag(shield->type))
                inf_stamp_npc_collision_footprint(s, shield->x, shield->y, shield->size);
        }
    }

    /* set timer pause: freeze at HP < 600, resume at HP < 480 with +175 ticks */
    if (!s->zuk.has_paused && zuk->hp < 600) {
        s->zuk.timer_paused = 1;
        s->zuk.has_paused = 1;
    }

    /* jad spawn at HP < 480 (with shield alive, timer paused) */
    if (!s->zuk.jad_spawned && s->zuk.timer_paused && zuk->hp < 480 &&
        si >= 0 && s->npcs[si].active) {
        s->zuk.jad_spawned = 1;
        s->zuk.timer_paused = 0;
        s->zuk.set_timer += 175;
        int j_slot = inf_find_free_npc(s);
        if (j_slot >= 0) {
            inf_init_npc(s, j_slot, INF_NPC_JAD, 24, 32);
            s->npcs[j_slot].aggro_target = si;  /* target shield */
            s->npcs[j_slot].stun_timer = 7;     /* spawn delay */
        }
    }

    /* set timer: spawns JalZek + JalXil targeting the shield */
    if (!s->zuk.timer_paused) {
        if (s->zuk.set_timer > 0)
            s->zuk.set_timer--;
        if (s->zuk.set_timer == 0) {
            int m_slot = inf_find_free_npc(s);
            if (m_slot >= 0) {
                inf_init_npc(s, m_slot, INF_NPC_MAGER, 20, 36);
                if (si >= 0) s->npcs[m_slot].aggro_target = si;
                s->npcs[m_slot].stun_timer = 7;  /* spawn delay */
            }
            int r_slot = inf_find_free_npc(s);
            if (r_slot >= 0) {
                inf_init_npc(s, r_slot, INF_NPC_RANGER, 29, 36);
                if (si >= 0) s->npcs[r_slot].aggro_target = si;
                s->npcs[r_slot].stun_timer = 9;  /* spawn delay */
            }
            s->zuk.set_timer = s->zuk.set_interval;
        }
    }

    /* healer spawn at HP < 240: 4 JalMejJak targeting Zuk, sets enraged */
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
                s->npcs[slot].aggro_target = zuk_idx;  /* heal Zuk until tagged */
            }
        }
    }

    /* on zuk death: all other mobs die */
    if (zuk->hp <= 0) {
        for (int i = 0; i < INF_MAX_NPCS; i++) {
            inf_deactivate_npc(s, i);
        }
    }
}

static void inf_healer_apply_landed_heal(InfernoState* s, int idx) {
    InfNPC* npc = &s->npcs[idx];
    int target_idx = npc->heal_target;
    int heal_cap = (npc->type == INF_NPC_HEALER_ZUK) ? 25 : 20;

    npc->heal_target = -1;
    if (target_idx < 0 || target_idx >= INF_MAX_NPCS) return;
    if (!s->npcs[target_idx].active) return;

    int heal = encounter_rand_int(&s->rng_state, heal_cap);
    int before = s->npcs[target_idx].hp;
    s->npcs[target_idx].hp += heal;
    if (s->npcs[target_idx].hp > s->npcs[target_idx].max_hp)
        s->npcs[target_idx].hp = s->npcs[target_idx].max_hp;
    float effective_heal = (float)(s->npcs[target_idx].hp - before);
    s->hp_restored_this_tick += effective_heal;
    if (s->npcs[target_idx].type == INF_NPC_JAD)
        s->hp_restored_jad_this_tick += effective_heal;
    if (s->npcs[target_idx].type == INF_NPC_ZUK)
        s->hp_restored_zuk_this_tick += effective_heal;
}

static void inf_queue_pending_spark(
    InfernoState* s, int src_x, int src_y, int x, int y, int damage, int ticks_remaining
) {
    for (int i = 0; i < INF_MAX_PENDING_SPARKS; i++) {
        if (s->pending_sparks[i].active) continue;
        s->pending_sparks[i].active = 1;
        s->pending_sparks[i].src_x = src_x;
        s->pending_sparks[i].src_y = src_y;
        s->pending_sparks[i].x = x;
        s->pending_sparks[i].y = y;
        s->pending_sparks[i].damage = damage;
        s->pending_sparks[i].ticks_remaining = ticks_remaining;
        s->pending_sparks[i].visual_emitted = 0;
        return;
    }
    fprintf(stderr, "inferno: pending spark queue overflow at %d slots\n",
        INF_MAX_PENDING_SPARKS);
    abort();
}

static void inf_queue_zuk_healer_sparks(InfernoState* s, const InfNPC* npc) {
    int clamped_x = s->player.x;
    if (clamped_x < npc->x - 5) clamped_x = npc->x - 5;
    if (clamped_x > npc->x + 4) clamped_x = npc->x + 4;

    /* InfernoTrainer JalMejJak AoeWeapon: 2 random sparks target y=14+rand(0..3)
       in reference coords. our coord system is Y-flipped (ours_y = 57 - ref_y),
       so ref y=14..17 maps to ours y=40..43 (near player/zuk band). */
    inf_queue_pending_spark(s, npc->x, npc->y, clamped_x, s->player.y,
                            5 + encounter_rand_int(&s->rng_state, 6), 4);
    inf_queue_pending_spark(s,
                            npc->x, npc->y,
                            npc->x + encounter_rand_int(&s->rng_state, 11) - 5,
                            40 + encounter_rand_int(&s->rng_state, 4),
                            5 + encounter_rand_int(&s->rng_state, 6), 4);
    inf_queue_pending_spark(s,
                            npc->x, npc->y,
                            npc->x + encounter_rand_int(&s->rng_state, 11) - 5,
                            40 + encounter_rand_int(&s->rng_state, 4),
                            5 + encounter_rand_int(&s->rng_state, 6), 4);
}

static void inf_resolve_pending_sparks(InfernoState* s) {
    for (int i = 0; i < INF_MAX_PENDING_SPARKS; i++) {
        if (!s->pending_sparks[i].active) continue;
        s->pending_sparks[i].ticks_remaining--;
        if (s->pending_sparks[i].ticks_remaining > 0) continue;

        if (encounter_entity_footprints_overlap(
                s->pending_sparks[i].x - 1, s->pending_sparks[i].y - 1, 3,
                s->player.x, s->player.y, 1)) {
            encounter_damage_player(&s->player, s->pending_sparks[i].damage,
                                    &s->damage_received_this_tick);
            s->spark_damage_this_tick += (float)s->pending_sparks[i].damage;
            s->last_hit_by_type = INF_NPC_HEALER_ZUK;
        }
        s->pending_sparks[i].active = 0;
    }
}


static void inf_tick_npcs(InfernoState* s) {
    /* NPC per-tick flags are cleared in inf_step BEFORE inf_tick_player,
       so player hit flags survive through both tick functions into render_post_tick. */

    inf_resolve_npc_target_hits(s);
    inf_zuk_tick(s);

    int blob_split_x[INF_MAX_NPCS];
    int blob_split_y[INF_MAX_NPCS];
    int blob_split_parent[INF_MAX_NPCS];
    int blob_split_count = 0;

    for (int i = 0; i < INF_MAX_NPCS; i++) {
        if (!s->npcs[i].active) continue;

        if ((s->npcs[i].type == INF_NPC_HEALER_JAD || s->npcs[i].type == INF_NPC_HEALER_ZUK) &&
            s->npcs[i].death_ticks == 0 && s->npcs[i].heal_timer > 0) {
            s->npcs[i].heal_timer--;
            if (s->npcs[i].heal_timer == 0)
                inf_healer_apply_landed_heal(s, i);
        }

        /* death linger: decrement and deactivate when done */
        if (s->npcs[i].death_ticks > 0) {
            s->npcs[i].death_ticks--;
            if (s->npcs[i].death_ticks == 0) {
                if (s->npcs[i].spawn_blob_splits_on_removal &&
                    blob_split_count < INF_MAX_NPCS) {
                    blob_split_x[blob_split_count] = s->npcs[i].x;
                    blob_split_y[blob_split_count] = s->npcs[i].y;
                    blob_split_parent[blob_split_count] = i;
                    blob_split_count++;
                }
                inf_deactivate_npc(s, i);
            }
            continue;  /* dying NPCs don't move or attack */
        }

        /* decrement ice barrage freeze timer */
        if (s->npcs[i].frozen_ticks > 0) s->npcs[i].frozen_ticks--;

        if (s->npcs[i].type == INF_NPC_MAGER && s->npcs[i].resurrect_cooldown > 0)
            s->npcs[i].resurrect_cooldown--;

        /* meleer dig check */
        if (s->npcs[i].type == INF_NPC_MELEER)
            inf_meleer_dig_check(s, i);

        inf_npc_move(s, i);
        inf_npc_attack(s, i);

        /* jad healer spawning */
        if (s->npcs[i].type == INF_NPC_JAD)
            inf_jad_check_healers(s, i);
    }

    for (int i = 0; i < blob_split_count; i++) {
        InfNPCType split_types[3] = {
            INF_NPC_BLOB_RANGE, INF_NPC_BLOB_MELEE, INF_NPC_BLOB_MAGE
        };
        int split_offsets[3][2] = {
            {1, 1}, {0, 0}, {2, 2}
        };
        for (int sp = 0; sp < 3; sp++) {
            int slot = -1;
            for (int n = 0; n < INF_MAX_NPCS; n++) {
                if (n == blob_split_parent[i] || s->npcs[n].active) continue;
                slot = n;
                break;
            }
            if (slot < 0) break;
            inf_init_npc(
                s, slot, split_types[sp],
                blob_split_x[i] + split_offsets[sp][0],
                blob_split_y[i] + split_offsets[sp][1]);
            s->npcs[slot].attack_timer = 4;
        }
    }
}


#define INF_HEAD_MOVE      0   /* 25: idle + 8 walk + 16 run */
#define INF_HEAD_PRAYER    1   /* no_change, off, set_refresh_melee/ranged/magic */
#define INF_HEAD_TARGET    2   /* INF_OBS_NPCS+1: none or observation slot */
#define INF_HEAD_GEAR      3   /* 4: no_switch, mage, tbow, bp */
#define INF_HEAD_EAT       4   /* 2: none, brew */
#define INF_HEAD_POTION    5   /* 4: none, restore, bastion, stamina */
#define INF_HEAD_SPELL     6   /* 3: no_change, blood_barrage, ice_barrage */
#define INF_HEAD_SPEC      7   /* 2: no_change, toggle (arm/disarm blowpipe spec) */
#define INF_HEAD_OFFENSIVE 8   /* no_change, off, set_refresh_piety/rigour/augury */
static const int INF_ACTION_DIMS[INF_NUM_ACTION_HEADS] = {
    ENCOUNTER_MOVE_ACTIONS, ENCOUNTER_OVERHEAD_DIM_PVE, INF_OBS_NPCS+1, 4, 2, 4, 3, 2, ENCOUNTER_OFFENSIVE_DIM
};
#define INF_ACTION_MASK_SIZE (ENCOUNTER_MOVE_ACTIONS + ENCOUNTER_OVERHEAD_DIM_PVE + INF_OBS_NPCS+1 + 4 + 2 + 4 + 3 + 2 + ENCOUNTER_OFFENSIVE_DIM)

/* movement uses shared encounter_move_to_target from osrs_encounter.h */

/* walkability callback for encounter_move_to_target */
static int inf_tile_walkable(void* ctx, int x, int y) {
    InfernoState* s = (InfernoState*)ctx;
    if (!inf_in_arena(x, y)) return 0;
    if (inf_blocked_by_pillar(s, x, y, 1)) return 0;
    if (s->collision_map)
        if (!collision_tile_walkable(s->collision_map, 0,
                x + s->world_offset_x, y + s->world_offset_y))
            return 0;
    return 1;
}

/* sara brew heal at base HP 99: floor(99*0.15)+2 = 16. ref: osrs_consumables.h osrs_brew_effect */
#define INF_BREW_HEAL       (99 * 15 / 100 + 2)
/* super restore at base prayer 99: 8 + floor(99/4) = 32. ref: osrs_consumables.h osrs_drink_potion */
#define INF_RESTORE_AMOUNT  (8 + 99 / 4)
#define INF_NPC_DEATH_LINGER_TICKS 3

/* apply NPC death: blob split, mager resurrection store, jad healer cleanup.
   call after reducing npc->hp. checks if hp <= 0 and handles death effects. */
static void inf_apply_npc_death(InfernoState* s, int npc_idx) {
    InfNPC* npc = &s->npcs[npc_idx];
    if (npc->hp > 0 || !npc->active || npc->death_ticks > 0) return;
    /* keep active=1 for death_ticks so renderer shows final hitsplat + death anim.
       inf_tick_npcs decrements death_ticks and sets active=0 when it reaches 0. */
    npc->death_ticks = INF_NPC_DEATH_LINGER_TICKS;
    s->total_npc_kills++;

    if (npc->type == INF_NPC_BLOB)
        npc->spawn_blob_splits_on_removal = 1;
    inf_store_dead_mob(s, npc);

    if (npc->type == INF_NPC_JAD) {
        for (int j = 0; j < INF_MAX_NPCS; j++) {
            if (s->npcs[j].active &&
                s->npcs[j].type == INF_NPC_HEALER_JAD &&
                s->npcs[j].jad_owner_idx == npc_idx) {
                inf_deactivate_npc(s, j);
            }
        }
    }
}

typedef struct {
    int target_slot;
    int gear;
    int overhead_style;
    int offensive;
} InfOraclePick;

static int inf_oracle_trigger_active(const InfernoState* s, int mode) {
    int zuk_idx = inf_find_live_zuk_idx(s);
    if (zuk_idx < 0) return 0;
    int zuk_hp = s->npcs[zuk_idx].hp;
    if (mode == 1 || mode == 2 || mode == 8) return zuk_hp <= 300;
    if (mode == 3) return zuk_hp <= 240;
    if (mode >= 4 && mode <= 7) return s->zuk.jad_spawned || zuk_hp <= 600;
    if (mode == 9 || mode == 10 || mode == 11)
        return s->zuk.healer_spawned && s->total_zuk_healer_tags < 4;
    return 0;
}

static InfOraclePick inf_oracle_pick_full(const InfernoState* s, int mode) {
    InfOraclePick pick = { -1, -1, ATTACK_STYLE_NONE, OFFENSIVE_PRAYER_NONE };
    if (mode <= 0 || !inf_oracle_trigger_active(s, mode)) return pick;

    if (mode == 9) {
        for (int o = 33; o < 37; o++) {
            int n = s->current_obs_slots[o];
            if (n < 0 || n >= INF_MAX_NPCS) continue;
            const InfNPC* npc = &s->npcs[n];
            if (!npc->active || npc->death_ticks != 0 ||
                    npc->type != INF_NPC_HEALER_ZUK)
                continue;
            if (npc->aggro_target >= 0 && npc->aggro_target < INF_MAX_NPCS &&
                    s->npcs[npc->aggro_target].active &&
                    s->npcs[npc->aggro_target].type == INF_NPC_ZUK) {
                pick.target_slot = o;
                return pick;
            }
        }
        return pick;
    }

    if (mode == 10 || mode == 11) {
        if (!inf_player_behind_zuk_shield_now(s)) return pick;
        if (mode == 11 && s->player.attack_timer != 0) return pick;

        for (int o = 33; o < 37; o++) {
            int n = s->current_obs_slots[o];
            if (!inf_is_untagged_live_zuk_healer_slot(s, n))
                continue;
            if (inf_player_can_attack_npc_from_current_tile(s, n)) {
                pick.target_slot = o;
                return pick;
            }
        }
        return pick;
    }

    for (int o = 22; o < 25; o++) {
        int n = s->current_obs_slots[o];
        if (n >= 0 && n < INF_MAX_NPCS &&
            s->npcs[n].active && s->npcs[n].death_ticks == 0 &&
            s->npcs[n].type == INF_NPC_JAD) {
            pick.target_slot = o;
            pick.gear = INF_GEAR_TBOW;
            pick.offensive = OFFENSIVE_PRAYER_RIGOUR;
            int jas = s->npcs[n].jad_attack_style;
            if (jas == ATTACK_STYLE_RANGED) pick.overhead_style = ATTACK_STYLE_RANGED;
            else if (jas == ATTACK_STYLE_MAGIC) pick.overhead_style = ATTACK_STYLE_MAGIC;
            return pick;
        }
    }
    if (mode == 1) return pick;

    for (int o = 33; o < 37; o++) {
        int n = s->current_obs_slots[o];
        if (n >= 0 && n < INF_MAX_NPCS &&
            s->npcs[n].active && s->npcs[n].death_ticks == 0 &&
            s->npcs[n].type == INF_NPC_HEALER_ZUK) {
            pick.target_slot = o;
            pick.gear = INF_GEAR_BP;
            pick.offensive = OFFENSIVE_PRAYER_RIGOUR;
            pick.overhead_style = ATTACK_STYLE_MAGIC;
            return pick;
        }
    }

    for (int o = 0; o < 22; o++) {
        int n = s->current_obs_slots[o];
        if (n < 0 || n >= INF_MAX_NPCS) continue;
        if (!s->npcs[n].active || s->npcs[n].death_ticks != 0) continue;
        int t = s->npcs[n].type;
        if (t == INF_NPC_ZUK || t == INF_NPC_ZUK_SHIELD ||
            t == INF_NPC_JAD ||
            t == INF_NPC_HEALER_ZUK || t == INF_NPC_HEALER_JAD)
            continue;
        pick.target_slot = o;
        pick.offensive = OFFENSIVE_PRAYER_RIGOUR;
        if (t == INF_NPC_MAGER) {
            pick.gear = INF_GEAR_TBOW;
            pick.overhead_style = ATTACK_STYLE_MAGIC;
        } else if (t == INF_NPC_RANGER) {
            pick.gear = INF_GEAR_TBOW;
            pick.overhead_style = ATTACK_STYLE_RANGED;
        } else if (t == INF_NPC_MELEER) {
            pick.gear = INF_GEAR_BP;
            pick.overhead_style = ATTACK_STYLE_MELEE;
        } else {
            pick.gear = INF_GEAR_BP;
        }
        return pick;
    }

    return pick;
}

static int inf_oracle_overrides_target(int mode) {
    return mode >= 1 && mode <= 11;
}

static int inf_oracle_overrides_gear_offensive(int mode) {
    return mode == 6 || mode == 7 || mode == 8;
}

static int inf_oracle_overrides_overhead(int mode) {
    return mode == 5 || mode == 7 || mode == 8;
}

static int inf_oracle_overhead_action_for(const InfernoState* s, int wanted_style) {
    (void)s;
    int action = ENCOUNTER_OVERHEAD_NO_CHANGE;
    if (wanted_style == ATTACK_STYLE_RANGED) {
        action = ENCOUNTER_OVERHEAD_SET_REFRESH_RANGED;
    } else if (wanted_style == ATTACK_STYLE_MAGIC) {
        action = ENCOUNTER_OVERHEAD_SET_REFRESH_MAGIC;
    } else if (wanted_style == ATTACK_STYLE_MELEE) {
        action = ENCOUNTER_OVERHEAD_SET_REFRESH_MELEE;
    } else {
        return ENCOUNTER_OVERHEAD_NO_CHANGE;
    }
    return action;
}

static int inf_oracle_offensive_action_for(const InfernoState* s, int wanted_offensive) {
    (void)s;
    if (wanted_offensive == OFFENSIVE_PRAYER_RIGOUR) {
        return ENCOUNTER_OFFENSIVE_SET_REFRESH_RIGOUR;
    }
    if (wanted_offensive == OFFENSIVE_PRAYER_PIETY) {
        return ENCOUNTER_OFFENSIVE_SET_REFRESH_PIETY;
    }
    if (wanted_offensive == OFFENSIVE_PRAYER_AUGURY) {
        return ENCOUNTER_OFFENSIVE_SET_REFRESH_AUGURY;
    }
    return ENCOUNTER_OFFENSIVE_NO_CHANGE;
}

static int inf_oracle_gear_action_for(const InfernoState* s, int wanted_gear) {
    if (wanted_gear < 0) return 0;
    if ((int)s->weapon_set == wanted_gear) return 0;
    return wanted_gear + 1;
}

static void inf_player_pretick(InfernoState* s, const int* actions) {
    /* apply prayer actions. each helper returns 1 when the final active prayer
       was activated this tick, so that slot skips drain. */
    OffensivePrayer prev_offensive = s->player.offensive_prayer;
    int prayer_act = actions[INF_HEAD_PRAYER];
    int offensive_act = actions[INF_HEAD_OFFENSIVE];
    if (s->oracle_mode > 0) {
        InfOraclePick pick = inf_oracle_pick_full(s, s->oracle_mode);
        if (pick.target_slot >= 0) {
            if (inf_oracle_overrides_overhead(s->oracle_mode) &&
                pick.overhead_style != ATTACK_STYLE_NONE) {
                prayer_act = inf_oracle_overhead_action_for(s, pick.overhead_style);
            }
            if (inf_oracle_overrides_gear_offensive(s->oracle_mode) &&
                pick.offensive != OFFENSIVE_PRAYER_NONE) {
                offensive_act = inf_oracle_offensive_action_for(s, pick.offensive);
            }
        }
    }
    if (encounter_apply_overhead_action(&s->player.prayer, prayer_act)) {
        s->player.prayer_just_activated = 1;
    }
    if (encounter_apply_offensive_action(&s->player.offensive_prayer, offensive_act)) {
        s->player.offensive_prayer_just_activated = 1;
    }
    /* drain can also clear offensive_prayer (pp<=0 auto-clear), so recompute
       AFTER drain to catch both the apply-side change and the drain-side clear. */
    encounter_drain_all_prayers(
        &s->player, encounter_player_prayer_bonus(&s->player));
    /* offensive prayer is baked into eff_level/max_hit via the loadout cache.
       recompute all loadouts on any change so combat math reflects current state. */
    if (s->player.offensive_prayer != prev_offensive) {
        encounter_recompute_loadout_max_hits(s->loadout_stats, INF_NUM_WEAPON_SETS, &s->player);
        if (s->human_command_mode)
            inf_refresh_human_loadout_stats(s);
    }
}

static FightStyle inf_default_fight_style_for_style(AttackStyle style) {
    if (style == ATTACK_STYLE_MAGIC) return FIGHT_STYLE_AUTOCAST;
    if (style == ATTACK_STYLE_RANGED) return FIGHT_STYLE_RAPID;
    return FIGHT_STYLE_ACCURATE;
}

static void inf_note_human_weapon_set(InfernoState* s) {
    uint8_t weapon = s->player.equipped[GEAR_SLOT_WEAPON];
    for (int g = 0; g < INF_NUM_WEAPON_SETS; g++) {
        if (INF_LOADOUTS[g][GEAR_SLOT_WEAPON] == weapon) {
            s->weapon_set = (InfWeaponSet)g;
            return;
        }
    }
}

static void inf_apply_human_player_commands(InfernoState* s) {
    int did_change_equipment = 0;
    for (int i = 0; i < s->human_command_count; i++) {
        const HumanCommand* cmd = &s->human_commands[i];
        if (cmd->kind == HUMAN_COMMAND_EQUIP_INVENTORY_ITEM) {
            if (cmd->gear_slot >= 0 && cmd->gear_slot < NUM_GEAR_SLOTS &&
                cmd->item_db_idx >= 0 && cmd->item_db_idx < NUM_ITEMS) {
                int changed = slot_equip_item(&s->player, cmd->gear_slot, (uint8_t)cmd->item_db_idx);
                if (changed) {
                    s->total_gear_switches++;
                    did_change_equipment = 1;
                    if (cmd->gear_slot == GEAR_SLOT_WEAPON) {
                        AttackStyle style = inf_player_equipped_attack_style(s);
                        if (inf_item_supports_autocast(s->player.equipped[GEAR_SLOT_WEAPON])) {
                            s->player.fight_style = s->player.autocast_defensive
                                ? FIGHT_STYLE_DEFENSIVE_AUTOCAST
                                : FIGHT_STYLE_AUTOCAST;
                        } else {
                            s->player.fight_style = inf_default_fight_style_for_style(style);
                        }
                        inf_note_human_weapon_set(s);
                    }
                }
            }
        } else if (cmd->kind == HUMAN_COMMAND_FIGHT_STYLE) {
            if (cmd->fight_style >= FIGHT_STYLE_ACCURATE &&
                cmd->fight_style <= FIGHT_STYLE_DEFENSIVE_AUTOCAST) {
                s->player.fight_style = (FightStyle)cmd->fight_style;
                if (cmd->fight_style == FIGHT_STYLE_AUTOCAST ||
                        cmd->fight_style == FIGHT_STYLE_DEFENSIVE_AUTOCAST) {
                    s->player.autocast_enabled = 1;
                    s->player.autocast_defensive =
                        cmd->fight_style == FIGHT_STYLE_DEFENSIVE_AUTOCAST;
                }
                did_change_equipment = 1;
            }
        } else if (cmd->kind == HUMAN_COMMAND_SET_AUTOCAST) {
            if (inf_is_barrage_spell(cmd->autocast_spell)) {
                s->player.autocast_enabled = 1;
                s->player.autocast_defensive = cmd->autocast_defensive != 0;
                s->player.autocast_spell = cmd->autocast_spell;
                if (inf_item_supports_autocast(s->player.equipped[GEAR_SLOT_WEAPON])) {
                    s->player.fight_style = s->player.autocast_defensive
                        ? FIGHT_STYLE_DEFENSIVE_AUTOCAST
                        : FIGHT_STYLE_AUTOCAST;
                }
                did_change_equipment = 1;
            }
        }
    }
    if (did_change_equipment)
        inf_refresh_human_loadout_stats(s);
}

static void inf_tick_player(InfernoState* s, const int* actions, int can_attack) {
    if (s->player_last_interaction_age == 0)
        s->player_last_interaction_age = 1;

    if (s->human_command_mode) {
        inf_apply_human_player_commands(s);
    } else {
        int gear_act = actions[INF_HEAD_GEAR];
        if (inf_oracle_overrides_gear_offensive(s->oracle_mode)) {
            InfOraclePick pick = inf_oracle_pick_full(s, s->oracle_mode);
            if (pick.target_slot >= 0 && pick.gear >= 0) {
                gear_act = inf_oracle_gear_action_for(s, pick.gear);
            }
        }
        if (gear_act >= 1) s->total_gear_switches++;
        if (gear_act >= 1 && gear_act <= 3) {
            InfWeaponSet new_set = (InfWeaponSet)(gear_act - 1);
            s->weapon_set = new_set;
            GearSet gs = (new_set == INF_GEAR_MAGE) ? GEAR_MAGE : GEAR_RANGED;
            encounter_apply_loadout(&s->player, INF_LOADOUTS[new_set], gs);
        }
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
    }

    int manual_spell = inf_spell_action_to_spell(actions[INF_HEAD_SPELL]);

    /* special energy regen: 10 energy every 50 ticks (30 seconds) */
    encounter_tick_spec_regen(&s->player);

    /* spec toggle: arm/disarm (does NOT interrupt interaction) */
    if (actions[INF_HEAD_SPEC] == 1)
        osrs_spec_toggle(&s->player.spec_armed);

    /* stat decay: every 60 ticks, boosted/drained stats move 1 toward base */
    if (s->tick > 0 && s->tick % 60 == 0) {
        int* stats[] = { &s->player.current_ranged, &s->player.current_magic,
                         &s->player.current_attack, &s->player.current_strength,
                         &s->player.current_defence };
        for (int si = 0; si < 5; si++) {
            if (*stats[si] > 99) (*stats[si])--;
            else if (*stats[si] < 99) (*stats[si])++;
        }
        encounter_recompute_loadout_max_hits(s->loadout_stats, INF_NUM_WEAPON_SETS, &s->player);
        if (s->human_command_mode)
            inf_refresh_human_loadout_stats(s);
    }

    /* consumables — shared 3-tick potion timer */
    if (s->player.potion_timer > 0) s->player.potion_timer--;
    if (s->stamina_active_ticks > 0) s->stamina_active_ticks--;

    /* brew (INF_HEAD_EAT): heals 16 HP, can overcap to base+16 */
    int eat_act = actions[INF_HEAD_EAT];
    if (eat_act == 1 && s->player.brew_doses > 0 && s->player.potion_timer == 0
        && s->player.current_hitpoints < s->player.base_hitpoints) {
        s->player.current_hitpoints += INF_BREW_HEAL;
        if (s->player.current_hitpoints > s->player.base_hitpoints + INF_BREW_HEAL)
            s->player.current_hitpoints = s->player.base_hitpoints + INF_BREW_HEAL;
        s->player.brew_doses--;
        s->player.potion_timer = 3;
        s->player.ate_food_this_tick = 1;
        s->brewed_this_tick = 1;
        encounter_brew_drain_stats(&s->player);
        encounter_recompute_loadout_max_hits(s->loadout_stats, INF_NUM_WEAPON_SETS, &s->player);
        if (s->human_command_mode)
            inf_refresh_human_loadout_stats(s);
    }

    /* potions (INF_HEAD_POTION): 1=restore, 2=bastion, 3=stamina */
    int pot_act = actions[INF_HEAD_POTION];
    if (pot_act == 1 && s->player.restore_doses > 0 && s->player.potion_timer == 0) {
        /* super restore: restores prayer + all combat stats */
        s->player.current_prayer += INF_RESTORE_AMOUNT;
        if (s->player.current_prayer > s->player.base_prayer)
            s->player.current_prayer = s->player.base_prayer;
        encounter_restore_stats(&s->player);
        s->player.restore_doses--;
        s->player.potion_timer = 3;
        encounter_recompute_loadout_max_hits(s->loadout_stats, INF_NUM_WEAPON_SETS, &s->player);
        if (s->human_command_mode)
            inf_refresh_human_loadout_stats(s);
    } else if (pot_act == 2 && s->player.bastion_doses > 0 && s->player.potion_timer == 0) {
        encounter_bastion_boost(&s->player);
        s->player.bastion_doses--;
        s->player.potion_timer = 3;
        encounter_recompute_loadout_max_hits(s->loadout_stats, INF_NUM_WEAPON_SETS, &s->player);
        if (s->human_command_mode)
            inf_refresh_human_loadout_stats(s);
    } else if (pot_act == 3 && s->player.stamina_doses > 0 && s->player.potion_timer == 0) {
        s->stamina_active_ticks = 200;
        s->player.stamina_doses--;
        s->player.potion_timer = 3;
    }

    /* inventory actions interrupt interaction (per OSRS entity interaction rules) */
    if (eat_act > 0)
        osrs_interaction_check_interrupt(&s->interaction, OSRS_IACT_EAT);
    if (pot_act > 0)
        osrs_interaction_check_interrupt(&s->interaction, OSRS_IACT_DRINK);

    inf_refresh_current_obs_slots(s);

    /* attack target selection: persistent until NPC dies or player clicks ground.
       target=0 means "no new target this tick" (preserves existing target). */
    int target = actions[INF_HEAD_TARGET];
    if (inf_oracle_overrides_target(s->oracle_mode)) {
        InfOraclePick pick = inf_oracle_pick_full(s, s->oracle_mode);
        if (pick.target_slot >= 0) target = pick.target_slot + 1;
    }
    int has_new_target = 0;
    if (target > 0 && target <= INF_OBS_NPCS) {
        int obs_idx = target - 1;
        int npc_idx = s->current_obs_slots[obs_idx];
        int is_live_target =
            npc_idx >= 0 && npc_idx < INF_MAX_NPCS &&
            s->npcs[npc_idx].active &&
            s->npcs[npc_idx].death_ticks == 0 &&
            s->npcs[npc_idx].type != INF_NPC_ZUK_SHIELD;
        int is_phantom_target =
            npc_idx >= 0 && npc_idx < INF_MAX_NPCS &&
            inf_npc_is_phantom_barrage_obs_candidate(s, npc_idx);
        if (is_live_target || is_phantom_target) {
            if (npc_idx < 32 && inf_is_untagged_live_zuk_healer_slot(s, npc_idx)) {
                int is_safe_target =
                    inf_player_behind_zuk_shield_now(s) &&
                    s->player.attack_timer == 0 &&
                    inf_player_can_attack_npc_from_current_tile(s, npc_idx);
                s->total_zuk_untagged_healer_targets++;
                if (is_safe_target)
                    s->total_zuk_safe_untagged_healer_targets++;
                else
                    s->total_zuk_unsafe_untagged_healer_targets++;
                if ((s->rewarded_zuk_healer_target_mask & (1u << npc_idx)) == 0) {
                    if (s->zuk_untagged_healer_target_bonus_coeff > 0.0f) {
                        s->zuk_untagged_healer_targets_this_tick++;
                        s->total_zuk_untagged_healer_target_rewards++;
                        s->rewarded_zuk_healer_target_mask |= 1u << npc_idx;
                    } else if (
                            s->zuk_safe_untagged_healer_target_bonus_coeff > 0.0f &&
                            is_safe_target) {
                        s->zuk_safe_untagged_healer_targets_this_tick++;
                        s->total_zuk_safe_untagged_healer_target_rewards++;
                        s->rewarded_zuk_healer_target_mask |= 1u << npc_idx;
                    }
                }
            }
            osrs_interaction_set(&s->interaction, npc_idx);
            s->player_last_interaction_target_slot = npc_idx;
            s->player_last_interaction_age = 0;
            has_new_target = 1;
        }
    }
    if ((s->oracle_mode == 10 || s->oracle_mode == 11) &&
            osrs_interaction_active(&s->interaction) &&
            inf_is_untagged_live_zuk_healer_slot(s, s->interaction.target_slot) &&
            (!inf_player_behind_zuk_shield_now(s) ||
                !inf_player_can_attack_npc_from_current_tile(
                    s, s->interaction.target_slot) ||
                (s->oracle_mode == 11 && s->player.attack_timer != 0))) {
        osrs_interaction_clear(&s->interaction);
    }
    if (s->zuk_safe_untagged_healer_target_mask &&
            osrs_interaction_active(&s->interaction) &&
            !inf_untagged_zuk_healer_target_is_safe_now(
                s,
                s->interaction.target_slot)) {
        osrs_interaction_clear(&s->interaction);
    }
    if (s->zuk_force_safe_untagged_healer_target_mask &&
            osrs_interaction_active(&s->interaction) &&
            inf_has_safe_untagged_zuk_healer_target_now(s) &&
            !inf_is_safe_untagged_zuk_healer_target_now(
                s,
                s->interaction.target_slot)) {
        osrs_interaction_clear(&s->interaction);
    }
    /* explicit movement (ground click or RL move) cancels attack target,
       but only if no new target was set this tick. auto-chase movement
       does NOT cancel — only explicit user actions do. */
    int has_explicit_move = (actions[INF_HEAD_MOVE] > 0 || s->player_dest_x >= 0);
    if (!has_new_target && has_explicit_move)
        osrs_interaction_check_interrupt(&s->interaction, OSRS_IACT_MOVE);
    /* clear target if NPC died unless the current tick can still phantom barrage it. */
    if (osrs_interaction_active(&s->interaction) &&
        (!s->npcs[s->interaction.target_slot].active ||
         (s->npcs[s->interaction.target_slot].death_ticks > 0 &&
             !inf_npc_is_phantom_barrage_cast_window(s, s->interaction.target_slot)))) {
        osrs_interaction_clear(&s->interaction);
    }

    if (osrs_interaction_active(&s->interaction) &&
            inf_is_live_zuk_healer_slot(s, s->interaction.target_slot)) {
        s->total_zuk_healer_target_ticks++;
        if (s->tick_at_first_zuk_healer_target < 0)
            s->tick_at_first_zuk_healer_target = s->tick;
    }

    /* movement: explicit move, auto-chase toward target, or idle.
       OSRS order: target selection → movement → attack check. */
    if (has_explicit_move && !osrs_interaction_active(&s->interaction)) {
        /* explicit movement (ground click or RL agent) — no attack target */
        if (s->player_dest_x >= 0) {
            encounter_move_toward_dest(&s->player, &s->player_dest_x, &s->player_dest_y,
                s->collision_map, s->world_offset_x, s->world_offset_y,
                inf_tile_walkable, s, inf_pathfind_blocked, s,
                INF_ARENA_MIN_X, INF_ARENA_MIN_Y, INF_ARENA_WIDTH, INF_ARENA_HEIGHT);
        } else {
            int move_act = actions[INF_HEAD_MOVE];
            s->player.is_running = 0;
            if (move_act > 0 && move_act < ENCOUNTER_MOVE_ACTIONS) {
                encounter_move_to_target(&s->player,
                    ENCOUNTER_MOVE_TARGET_DX[move_act], ENCOUNTER_MOVE_TARGET_DY[move_act],
                    inf_tile_walkable, s);
            }
        }
    } else if (osrs_interaction_active(&s->interaction)) {
        /* auto-chase: pathfind toward attack target when out of range */
        InfNPC* chase_npc = &s->npcs[s->interaction.target_slot];
        InfPlayerAttack chase_attack;
        int has_chase_attack = inf_resolve_player_attack(s, manual_spell, &chase_attack);
        const EncounterLoadoutStats* ls = has_chase_attack
            ? &chase_attack.stats
            : inf_current_loadout_stats(s);
        encounter_chase_attack_target(&s->player,
            chase_npc->x, chase_npc->y, INF_NPC_STATS[chase_npc->type].size,
            ls->attack_range,
            s->collision_map, s->world_offset_x, s->world_offset_y,
            inf_tile_walkable, s, inf_pathfind_blocked, s,
            s->los_blockers, s->los_blocker_count,
            INF_ARENA_MIN_X, INF_ARENA_MIN_Y, INF_ARENA_WIDTH, INF_ARENA_HEIGHT);
    }
    inf_rebuild_player_collision_flags(s);

    /* player attacks targeted NPC */
    if (can_attack && s->player.attack_timer > 0) s->player.attack_timer--;
    int has_zuk_healer_target = osrs_interaction_active(&s->interaction) &&
        inf_is_live_zuk_healer_slot(s, s->interaction.target_slot);
    if (has_zuk_healer_target) {
        if (!can_attack) {
            s->total_zuk_healer_cannot_attack_ticks++;
        } else if (s->player.attack_timer > 0) {
            s->total_zuk_healer_cooldown_ticks++;
        } else {
            InfNPC* target_npc = &s->npcs[s->interaction.target_slot];
            InfPlayerAttack healer_attack;
            int has_healer_attack = inf_resolve_player_attack(s, manual_spell, &healer_attack);
            if (has_healer_attack && encounter_player_can_attack(s->player.x, s->player.y,
                    target_npc->x, target_npc->y, target_npc->size,
                    healer_attack.stats.attack_range, s->los_blockers, s->los_blocker_count)) {
                s->total_zuk_healer_attackable_ticks++;
            } else {
                s->total_zuk_healer_out_of_range_ticks++;
            }
        }
    }
    if (can_attack && osrs_interaction_active(&s->interaction) && s->player.attack_timer == 0) {
        InfNPC* target_npc = &s->npcs[s->interaction.target_slot];
        if (target_npc->active) {
            InfPlayerAttack attack;
            int has_attack = inf_resolve_player_attack(s, manual_spell, &attack);
            const EncounterLoadoutStats* ls = &attack.stats;
            int is_magic_attack = attack.is_barrage;
            int weapon_is_blowpipe = inf_player_weapon_is(s, ITEM_TOXIC_BLOWPIPE);
            int weapon_is_tbow = inf_player_weapon_is(s, ITEM_TWISTED_BOW);
            uint8_t player_weapon = attack.weapon;
            int active_spell = attack.spell;

            if (has_attack && encounter_player_can_attack(s->player.x, s->player.y,
                    target_npc->x, target_npc->y, target_npc->size,
                    ls->attack_range, s->los_blockers, s->los_blocker_count)) {
                int is_blowpipe_spec_attack = weapon_is_blowpipe &&
                    s->player.spec_armed && s->player.special_energy >= BLOWPIPE_SPEC_COST;
                EncounterProjectileDistanceMode distance_mode = is_magic_attack
                    ? ENCOUNTER_PROJECTILE_DISTANCE_TARGET_SW_TILE
                    : ENCOUNTER_PROJECTILE_DISTANCE_CLOSEST_TILE;
                int projectile_dist = encounter_projectile_distance(
                    s->player.x, s->player.y, 1,
                    target_npc->x, target_npc->y, target_npc->size,
                    distance_mode);
                EncounterProjectileTiming hit_timing = inf_player_projectile_timing(
                    ls->style, player_weapon, is_blowpipe_spec_attack, projectile_dist);
                int hit_delay = hit_timing.damage_delay_ticks;

                int total_dmg = 0;

                if (is_magic_attack) {
                    /* barrage spells: 3x3 AoE via shared osrs_barrage_resolve.
                       ice barrage: freeze on hit (including 0 dmg), not on splash.
                       blood barrage: heal 25% of total AoE damage (applied when hits land). */
                    OsrsTargetRef target_ref = {
                        .kind = OSRS_TARGET_NPC,
                        .id = s->interaction.target_slot,
                    };
                    OsrsMagicAttackKind magic_kind = (active_spell == ENCOUNTER_SPELL_ICE)
                        ? OSRS_MAGIC_ATTACK_ANCIENT_ICE
                        : OSRS_MAGIC_ATTACK_ANCIENT_BLOOD;
                    OsrsPreparedAttackEffects attack_effects = osrs_prepare_attack_effects(
                        &s->player.equipment_effect_profile,
                        &s->player.item_effect_state,
                        s->player.equipped[GEAR_SLOT_WEAPON],
                        ATTACK_STYLE_MAGIC,
                        magic_kind,
                        target_ref,
                        1,
                        ls->eff_level * (ls->attack_bonus + 64),
                        ls->max_hit,
                        0,
                        0,
                        s->player.current_hitpoints,
                        s->player.base_hitpoints
                    );

                    /* build target array: primary target first, then all other active NPCs */
                    BarrageTarget btargets[INF_MAX_NPCS + 1];
                    int bt_count = 0;
                    {
                        const InfNPCStats* ns = &INF_NPC_STATS[target_npc->type];
                        btargets[bt_count++] = (BarrageTarget){
                            .active = 1, .x = target_npc->x, .y = target_npc->y,
                            .def_level = ns->def_level, .magic_def_bonus = ns->magic_def_bonus,
                            .npc_idx = s->interaction.target_slot,
                            .frozen_ticks = &s->npcs[s->interaction.target_slot].frozen_ticks,
                            .hit = 0, .damage = 0
                        };
                    }
                    for (int i = 0; i < INF_MAX_NPCS; i++) {
                        if (i == s->interaction.target_slot || !s->npcs[i].active) continue;
                        /* shield is invulnerable — no damage, no freeze from AoE */
                        if (s->npcs[i].type == INF_NPC_ZUK_SHIELD) continue;
                        /* skip dying NPCs — already logged as killed */
                        if (s->npcs[i].death_ticks > 0) continue;
                        const InfNPCStats* ns2 = &INF_NPC_STATS[s->npcs[i].type];
                        btargets[bt_count++] = (BarrageTarget){
                            .active = 1, .x = s->npcs[i].x, .y = s->npcs[i].y,
                            .def_level = ns2->def_level, .magic_def_bonus = ns2->magic_def_bonus,
                            .npc_idx = i,
                            .frozen_ticks = &s->npcs[i].frozen_ticks,
                            .hit = 0, .damage = 0
                        };
                    }

                    /* resolve barrage: accuracy/damage rolls + instant freeze for ice.
                       freeze is applied by the shared function at cast time. */
                    BarrageResult br = osrs_barrage_resolve(
                        btargets, bt_count, attack_effects.attack_roll, attack_effects.max_hit,
                        &s->rng_state, active_spell, attack_effects.use_double_accuracy);
                    total_dmg = br.total_damage;
                    if (target_npc->death_ticks > 0) {
                        total_dmg -= btargets[0].damage;
                        btargets[0].hit = 0;
                        btargets[0].damage = 0;
                    }
                    osrs_finalize_attack_effects(
                        &s->player.equipment_effect_profile,
                        &s->player.item_effect_state,
                        s->player.equipped[GEAR_SLOT_WEAPON],
                        ATTACK_STYLE_MAGIC,
                        magic_kind,
                        target_ref,
                        1,
                        attack_effects.use_double_accuracy,
                        btargets[0].hit,
                        btargets[0].damage,
                        &s->rng_state
                    );

                    /* queue pending hits for delayed damage */
                    for (int i = 0; i < bt_count; i++) {
                        if (!btargets[i].active || !btargets[i].rolled) continue;
                        int nidx = btargets[i].npc_idx;
                        if (s->npcs[nidx].death_ticks > 0) continue;
                        EncounterPendingHit* ph = &s->npcs[nidx].pending_hit;
                        ph->active = 1;
                        ph->damage = btargets[i].damage;
                        ph->ticks_remaining = hit_delay;
                        ph->attack_style = ATTACK_STYLE_MAGIC;
                        ph->check_prayer = 0;
                        ph->spell_type = active_spell;
                        ph->hit_success = btargets[i].hit;
                        ph->elysian_reduced = 0;
                    }

                } else if (weapon_is_tbow) {
                    const InfNPCStats* ns = &INF_NPC_STATS[target_npc->type];
                    OsrsPreparedAttackEffects attack_effects = osrs_prepare_attack_effects(
                        &s->player.equipment_effect_profile,
                        &s->player.item_effect_state,
                        s->player.equipped[GEAR_SLOT_WEAPON],
                        ATTACK_STYLE_RANGED,
                        OSRS_MAGIC_ATTACK_NONE,
                        (OsrsTargetRef){ .kind = OSRS_TARGET_NPC, .id = s->interaction.target_slot },
                        1,
                        ls->eff_level * (ls->attack_bonus + 64),
                        ls->max_hit,
                        ns->magic_level,
                        ns->magic_att_bonus,
                        s->player.current_hitpoints,
                        s->player.base_hitpoints
                    );
                    int def_roll = (ns->def_level + 8) * (ns->ranged_def_bonus + 64);
                    int hit_success = encounter_rand_float(&s->rng_state) <
                        osrs_hit_chance(attack_effects.attack_roll, def_roll);
                    if (hit_success) {
                        total_dmg = encounter_rand_int(&s->rng_state, attack_effects.max_hit + 1);
                    }
                    EncounterPendingHit* ph = &target_npc->pending_hit;
                    ph->active = 1;
                    ph->damage = total_dmg;
                    ph->ticks_remaining = hit_delay;
                    ph->attack_style = ATTACK_STYLE_RANGED;
                    ph->check_prayer = 0;
                    ph->spell_type = 0;
                    ph->hit_success = hit_success;
                    ph->elysian_reduced = 0;

                } else if (is_blowpipe_spec_attack) {
                    /* blowpipe spec: 2x accuracy, 1.5x max hit, heal 50% of damage */
                    if (!encounter_use_spec(&s->player, BLOWPIPE_SPEC_COST)) abort();
                    osrs_spec_disarm(&s->player.spec_armed);
                    const InfNPCStats* ns = &INF_NPC_STATS[target_npc->type];
                    int base_att_roll = ls->eff_level * (ls->attack_bonus + 64);
                    total_dmg = osrs_blowpipe_spec_resolve(
                        base_att_roll, ls->max_hit,
                        ns->def_level, ns->ranged_def_bonus, &s->rng_state);
                    int heal = total_dmg * BLOWPIPE_SPEC_HEAL_PCT / 100;
                    s->player.current_hitpoints += heal;
                    if (s->player.current_hitpoints > s->player.base_hitpoints)
                        s->player.current_hitpoints = s->player.base_hitpoints;
                    EncounterPendingHit* ph = &target_npc->pending_hit;
                    ph->active = 1;
                    ph->damage = total_dmg;
                    ph->ticks_remaining = hit_delay;
                    ph->attack_style = ATTACK_STYLE_RANGED;
                    ph->check_prayer = 0;
                    ph->spell_type = 0;
                    ph->hit_success = total_dmg > 0;
                    ph->elysian_reduced = 0;

                } else {
                    /* blowpipe: single target, normal attack */
                    const InfNPCStats* ns = &INF_NPC_STATS[target_npc->type];
                    int att_roll = ls->eff_level * (ls->attack_bonus + 64);
                    int def_roll = (ns->def_level + 8) * (ns->ranged_def_bonus + 64);
                    int hit_success = encounter_rand_float(&s->rng_state) <
                        osrs_hit_chance(att_roll, def_roll);
                    if (hit_success) {
                        total_dmg = encounter_rand_int(&s->rng_state, ls->max_hit + 1);
                    }
                    EncounterPendingHit* ph = &target_npc->pending_hit;
                    ph->active = 1;
                    ph->damage = total_dmg;
                    ph->ticks_remaining = hit_delay;
                    ph->attack_style = ATTACK_STYLE_RANGED;
                    ph->check_prayer = 0;
                    ph->spell_type = 0;
                    ph->hit_success = hit_success;
                    ph->elysian_reduced = 0;
                }

                s->player.attack_timer = ls->attack_speed;
                if (target_npc->type == INF_NPC_HEALER_ZUK) {
                    int target_was_untagged = target_npc->aggro_target >= 0;
                    s->total_zuk_healer_attack_fires++;
                    if (target_was_untagged && !is_magic_attack)
                        s->zuk_untagged_healer_nonmagic_attacks_this_tick++;
                    if (is_magic_attack)
                        s->zuk_healer_mage_attack_fires_this_tick++;
                    if (s->tick_at_first_zuk_healer_attack < 0)
                        s->tick_at_first_zuk_healer_attack = s->tick;
                }

                /* player projectile event for renderer */
                s->player_attacked_this_tick = 1;
                s->player_attack_npc_idx = s->interaction.target_slot;
                s->player_attack_dmg = total_dmg;
                s->player_attack_style_id = ls->style;
                s->player_attack_timing = hit_timing;

                /* player attack animation + spell type for renderer effect system */
                s->player.attack_style_this_tick = ls->style;
                if (ls->style == ATTACK_STYLE_MAGIC) {
                    /* 0=none, 1=ice, 2=blood */
                    s->player.magic_type_this_tick = (active_spell == ENCOUNTER_SPELL_ICE) ? 1 : 2;
                }
                if (target_npc->death_ticks > 0)
                    osrs_interaction_clear(&s->interaction);
            }
        }
    }
}

static int inf_roll_delayed_jad_damage(InfernoState* s, int attack_style) {
    const InfNPCStats* stats = &INF_NPC_STATS[INF_NPC_JAD];
    int max_hit = osrs_npc_max_hit(attack_style,
        stats->str_level, stats->range_level,
        stats->melee_str_bonus, stats->ranged_str_bonus,
        stats->magic_base_dmg, stats->magic_dmg_pct);
    if (stats->max_hit_cap > 0 && max_hit > stats->max_hit_cap)
        max_hit = stats->max_hit_cap;

    int dmg = encounter_rand_int(&s->rng_state, max_hit + 1);
    int att_lvl, att_bonus;
    if (attack_style == ATTACK_STYLE_RANGED) {
        att_lvl = stats->range_level;
        att_bonus = stats->range_att_bonus;
    } else {
        att_lvl = stats->magic_level;
        att_bonus = stats->magic_att_bonus;
    }

    int att_roll = osrs_npc_attack_roll(att_lvl, att_bonus);
    const EncounterLoadoutStats* ls = inf_current_loadout_stats(s);
    int def_bonus = encounter_player_def_bonus(
        ls->def_stab, ls->def_slash, ls->def_crush, ls->def_magic, ls->def_ranged,
        attack_style, stats->melee_style);
    int def_roll = osrs_player_def_roll_vs_npc(
        s->player.current_defence, s->player.current_magic, def_bonus, attack_style);
    if (encounter_rand_float(&s->rng_state) >= osrs_hit_chance(att_roll, def_roll))
        dmg = 0;
    return dmg;
}

static void inf_apply_delayed_prayer_check(InfernoState* s, EncounterPendingHit* hit) {
    if (encounter_prayer_correct_for_style(s->player.prayer, hit->attack_style)) {
        hit->damage = 0;
        s->prayer_correct_this_tick++;
        if (hit->source_npc_type >= 0 && hit->source_npc_type < INF_NUM_NPC_TYPES)
            s->prayer_correct_by_type[hit->source_npc_type]++;
    } else if (hit->source_npc_type == INF_NPC_JAD) {
        hit->damage = inf_roll_delayed_jad_damage(s, hit->attack_style);
        s->dmg_from_type[INF_NPC_JAD] += (float)hit->damage;
        if (hit->damage > 0) {
            s->last_hit_by_type = INF_NPC_JAD;
            s->off_prayer_hits_this_tick++;
        }
    } else if (hit->damage > 0 && hit->attack_style != ATTACK_STYLE_NONE) {
        s->off_prayer_hits_this_tick++;
    }
    hit->check_prayer = 0;
}

static void inf_resolve_player_projectiles_on_npcs(InfernoState* s) {
    int blood_heal_acc = 0;
    for (int i = 0; i < INF_MAX_NPCS; i++) {
        if (!s->npcs[i].active || s->npcs[i].death_ticks > 0) continue;
        int spell = s->npcs[i].pending_hit.spell_type;
        int hit_success = s->npcs[i].pending_hit.hit_success;
        float dmg_before = s->damage_dealt_this_tick;
        int hp_before = s->npcs[i].hp;
        int landed = encounter_resolve_npc_pending_hit(
            &s->npcs[i].pending_hit,
            &s->npcs[i].hp, &s->npcs[i].hit_landed_this_tick, &s->npcs[i].hit_damage,
            &s->npcs[i].frozen_ticks, &blood_heal_acc, &s->damage_dealt_this_tick);
        if (landed) {
            float landed_damage = s->damage_dealt_this_tick - dmg_before;
            int t = s->npcs[i].type;
            int hp_after = s->npcs[i].hp;
            int is_resurrected = s->npcs[i].resurrection_count != 0;
            if (is_resurrected) {
                s->damage_resurrected_this_tick += landed_damage;
            } else {
                if (t == INF_NPC_ZUK) {
                    s->damage_zuk_this_tick += landed_damage;
                } else if (t == INF_NPC_HEALER_ZUK) {
                    s->damage_zuk_healers_this_tick += landed_damage;
                } else if (t == INF_NPC_HEALER_JAD) {
                    s->damage_jad_healers_this_tick += landed_damage;
                } else if (t == INF_NPC_JAD) {
                    s->damage_jad_this_tick += landed_damage;
                } else if (t != INF_NPC_ZUK && t != INF_NPC_ZUK_SHIELD &&
                           t != INF_NPC_HEALER_JAD) {
                    s->damage_set_this_tick += landed_damage;
                }
                if (hp_before > 0 && hp_after <= 0) {
                    if (t == INF_NPC_JAD) s->kill_jad_this_tick++;
                    else if (t == INF_NPC_HEALER_ZUK) s->kill_zuk_healer_this_tick++;
                    else if (t != INF_NPC_ZUK && t != INF_NPC_ZUK_SHIELD &&
                             t != INF_NPC_HEALER_JAD) s->kill_set_this_tick++;
                }
            }
            s->npcs[i].hit_spell_type = spell;
            s->npcs[i].hit_was_successful_this_tick = hit_success;
            int shield_taggable = inf_is_shield_taggable_slot(s, i);
            if (s->npcs[i].aggro_target != -1) {
                if (s->npcs[i].type == INF_NPC_HEALER_ZUK ||
                    s->npcs[i].type == INF_NPC_HEALER_JAD) {
                    s->healer_tags_this_tick++;
                    if (s->npcs[i].type == INF_NPC_HEALER_ZUK)
                        s->zuk_healer_tags_this_tick++;
                }
                if (shield_taggable)
                    s->shield_tags_this_tick++;
                s->npcs[i].aggro_target = -1;
                s->npcs[i].stun_timer = 2;
            }
            inf_apply_npc_death(s, i);
        }
    }
    if (blood_heal_acc > 0) {
        int healed = blood_heal_acc / 4;
        int hp_before = s->player.current_hitpoints;
        s->player.current_hitpoints += healed;
        if (s->player.current_hitpoints > s->player.base_hitpoints)
            s->player.current_hitpoints = s->player.base_hitpoints;
        s->blood_heal_this_tick = s->player.current_hitpoints - hp_before;
    }
}

static void inf_resolve_player_pending_hits(InfernoState* s) {
    for (int i = 0; i < s->player_pending_hit_count; i++) {
        EncounterPendingHit* hit = &s->player_pending_hits[i];

        if (hit->check_prayer && hit->prayer_check_delay > 0 &&
            hit->source_npc_type != INF_NPC_JAD) {
            hit->prayer_check_delay--;
            if (hit->prayer_check_delay == 0) {
                inf_apply_delayed_prayer_check(s, hit);
            }
        }

        hit->ticks_remaining--;
        if (hit->ticks_remaining <= 0) {
            int dmg = hit->damage;
            if (hit->check_prayer) {
                if (encounter_prayer_correct_for_style(s->player.prayer, hit->attack_style)) {
                    dmg = 0;
                    s->prayer_correct_this_tick++;
                    if (hit->source_npc_type >= 0 && hit->source_npc_type < INF_NUM_NPC_TYPES)
                        s->prayer_correct_by_type[hit->source_npc_type]++;
                } else if (dmg > 0 && hit->attack_style != ATTACK_STYLE_NONE) {
                    s->off_prayer_hits_this_tick++;
                }
            } else if (dmg > 0 && hit->attack_style != ATTACK_STYLE_NONE &&
                       hit->source_npc_type != INF_NPC_JAD) {
                s->off_prayer_hits_this_tick++;
            }

            if (dmg > 0 &&
                hit->source_npc_type >= 0 &&
                hit->source_npc_type < INF_NUM_NPC_TYPES) {
                s->last_hit_by_type = hit->source_npc_type;
            }
            if (hit->elysian_reduced) {
                s->player.elysian_proc_this_tick = 1;
            } else {
                int elysian_reduced = 0;
                dmg = inf_apply_elysian_to_player_hit(s, dmg, &elysian_reduced);
                if (elysian_reduced) s->player.elysian_proc_this_tick = 1;
            }
            encounter_damage_player(
                &s->player, dmg, &s->damage_received_this_tick);
            s->player_pending_hits[i] =
                s->player_pending_hits[--s->player_pending_hit_count];
            i--;
        }
    }
}

static void inf_resolve_jad_prayer_checks_after_player(InfernoState* s) {
    for (int i = 0; i < s->player_pending_hit_count; i++) {
        EncounterPendingHit* hit = &s->player_pending_hits[i];
        if (!hit->check_prayer ||
            hit->source_npc_type != INF_NPC_JAD ||
            hit->prayer_check_delay <= 0) {
            continue;
        }
        hit->prayer_check_delay--;
        if (hit->prayer_check_delay == 0)
            inf_apply_delayed_prayer_check(s, hit);
    }
}


static int inf_healer_is_actively_healing(const InfernoState* s, const InfNPC* npc) {
    if (!npc->active || npc->death_ticks > 0) return 0;
    if (npc->type != INF_NPC_HEALER_JAD && npc->type != INF_NPC_HEALER_ZUK) return 0;
    if (npc->aggro_target < 0 || npc->aggro_target >= INF_MAX_NPCS) return 0;
    return s->npcs[npc->aggro_target].active;
}

static int inf_npc_type_is_actively_healed(
    const InfernoState* s,
    InfNPCType target_type
) {
    for (int i = 0; i < INF_MAX_NPCS; i++) {
        const InfNPC* healer = &s->npcs[i];
        if (!inf_healer_is_actively_healing(s, healer)) continue;
        const InfNPC* target = &s->npcs[healer->aggro_target];
        if (target->type == target_type) return 1;
    }
    return 0;
}

static int inf_count_untagged_zuk_healers(const InfernoState* s) {
    int count = 0;
    for (int i = 0; i < INF_MAX_NPCS; i++) {
        const InfNPC* healer = &s->npcs[i];
        if (!inf_healer_is_actively_healing(s, healer)) continue;
        if (healer->type != INF_NPC_HEALER_ZUK) continue;

        const InfNPC* target = &s->npcs[healer->aggro_target];
        if (target->type == INF_NPC_ZUK) count++;
    }
    return count;
}

static int inf_count_post_healer_set_pressure(const InfernoState* s) {
    int count = 0;
    for (int i = 0; i < INF_MAX_NPCS; i++) {
        const InfNPC* npc = &s->npcs[i];
        if (!npc->active || npc->death_ticks != 0 || npc->hp <= 0)
            continue;
        if (inf_npc_type_is_set_pressure(npc->type))
            count++;
    }
    return count;
}

static inline float inf_dmg_below_threshold(float old_hp, float new_hp, float t) {
    if (new_hp >= old_hp) return 0.0f;
    float a = old_hp < t ? old_hp : t;
    float b = new_hp < t ? new_hp : t;
    float d = a - b;
    return d > 0.0f ? d : 0.0f;
}

typedef enum {
    INF_ZUK_LOWWATERMARK_TRACK_ONLY,
    INF_ZUK_LOWWATERMARK_REWARD_ALLOWED,
    INF_ZUK_LOWWATERMARK_REWARD_BLOCKED_EXCEPT_THRESHOLD,
} InfZukLowWatermarkMode;

static float inf_zuk_low_watermark_reward(
    InfernoState* s,
    InfZukLowWatermarkMode mode
) {
    int zuk_idx = inf_find_live_zuk_idx(s);
    if (zuk_idx < 0) return 0.0f;

    float zuk_hp = (float)s->npcs[zuk_idx].hp;
    if (zuk_hp >= s->min_zuk_hp_seen) return 0.0f;

    float old_min = s->min_zuk_hp_seen;
    float dmg = old_min - zuk_hp;
    int crosses_zuk_healer_threshold = old_min > 240.0f && zuk_hp <= 240.0f;
    int should_pay_zuk_damage =
        mode == INF_ZUK_LOWWATERMARK_REWARD_ALLOWED ||
        (mode == INF_ZUK_LOWWATERMARK_REWARD_BLOCKED_EXCEPT_THRESHOLD &&
            crosses_zuk_healer_threshold);
    float reward = should_pay_zuk_damage ? s->damage_reward_coeff * dmg : 0.0f;
    if (s->jad_killed_this_episode) {
        reward *= s->post_jad_zuk_multiplier;
    } else if (s->zuk.jad_spawned) {
        reward *= s->jad_alive_zuk_multiplier;
    }
    s->min_zuk_hp_seen = zuk_hp;

    if (s->tick_at_le_300 < 0 && zuk_hp <= 300.0f) s->tick_at_le_300 = s->tick;
    if (s->tick_at_le_240 < 0 && zuk_hp <= 240.0f) s->tick_at_le_240 = s->tick;
    if (s->tick_at_le_150 < 0 && zuk_hp <= 150.0f) s->tick_at_le_150 = s->tick;
    s->damage_after_300 += inf_dmg_below_threshold(old_min, zuk_hp, 300.0f);
    s->damage_after_240 += inf_dmg_below_threshold(old_min, zuk_hp, 240.0f);
    s->damage_after_150 += inf_dmg_below_threshold(old_min, zuk_hp, 150.0f);

    return reward;
}

static float inf_joseph_heal_damped_damage(float damage, float max_hp, float total_healed) {
    if (damage <= 0.0f) return 0.0f;
    float denom = max_hp + 4.0f * total_healed;
    if (denom <= 0.0f) return damage;
    return damage * max_hp / denom;
}

static float inf_zuk_healer_attack_shape_reward(const InfernoState* s) {
    return
        s->zuk_untagged_healer_nonmagic_attack_bonus_coeff *
            (float)s->zuk_untagged_healer_nonmagic_attacks_this_tick -
        s->zuk_healer_mage_attack_penalty_coeff *
            (float)s->zuk_healer_mage_attack_fires_this_tick;
}

static float inf_compute_joseph_reward(
    const InfernoState* s,
    int healer_is_actively_healing
) {
    float reward = 0.0f;
    if (healer_is_actively_healing) {
        reward = s->tag_reward_coeff * (float)s->healer_tags_this_tick;
    } else {
        float rewarded_other_damage = fmaxf(
            0.0f,
            s->damage_dealt_this_tick -
                s->damage_jad_this_tick -
                s->damage_zuk_this_tick -
                s->damage_jad_healers_this_tick);
        reward = s->damage_reward_coeff * (
            rewarded_other_damage +
            inf_joseph_heal_damped_damage(
                s->damage_jad_this_tick,
                (float)INF_NPC_STATS[INF_NPC_JAD].hp,
                s->total_hp_restored_jad) +
            inf_joseph_heal_damped_damage(
                s->damage_zuk_this_tick,
                (float)INF_NPC_STATS[INF_NPC_ZUK].hp,
                s->total_hp_restored_zuk));
    }
    reward -= s->shield_penalty_coeff * s->shield_damage_this_tick;
    reward += inf_zuk_healer_attack_shape_reward(s);
    return reward;
}

static float inf_compute_reward(InfernoState* s) {
    s->total_damage_dealt += s->damage_dealt_this_tick;
    s->total_zuk_healer_damage += s->damage_zuk_healers_this_tick;
    s->total_damage_received += s->damage_received_this_tick;
    s->total_hp_restored += s->hp_restored_this_tick;
    s->total_hp_restored_jad += s->hp_restored_jad_this_tick;
    s->total_hp_restored_zuk += s->hp_restored_zuk_this_tick;

    if (s->kill_jad_this_tick > 0) s->jad_killed_this_episode = 1;

    if (s->episode_over) {
        if (s->winner == 0) return 1.0f;
        if (s->terminal_penalty_enabled) return -1.0f;
        return -s->death_penalty_coeff;
    }

    int jad_is_actively_healed =
        inf_npc_type_is_actively_healed(s, INF_NPC_JAD);
    int zuk_is_actively_healed =
        inf_npc_type_is_actively_healed(s, INF_NPC_ZUK);
    int healer_is_actively_healing =
        jad_is_actively_healed || zuk_is_actively_healed;
    int tags_first_gate = inf_zuk_healer_tags_first_reward_gate_active(s);

    if (s->joseph_reward_mode == INF_JOSEPH_REWARD_MODE_ON) {
        if (inf_is_final_wave(s))
            inf_zuk_low_watermark_reward(s, INF_ZUK_LOWWATERMARK_TRACK_ONLY);
        return inf_compute_joseph_reward(s, healer_is_actively_healing);
    }

    float reward = 0.0f;
    if (inf_is_final_wave(s)) {
        int use_zuk_healer_phase_hp_delta =
            !tags_first_gate && s->zuk.healer_spawned &&
            s->zuk_healer_phase_hp_delta_coeff > 0.0f;
        InfZukLowWatermarkMode watermark_mode =
            INF_ZUK_LOWWATERMARK_REWARD_ALLOWED;
        if (tags_first_gate || use_zuk_healer_phase_hp_delta) {
            watermark_mode = INF_ZUK_LOWWATERMARK_TRACK_ONLY;
        } else if (zuk_is_actively_healed) {
            watermark_mode = INF_ZUK_LOWWATERMARK_REWARD_BLOCKED_EXCEPT_THRESHOLD;
        }
        reward = inf_zuk_low_watermark_reward(s, watermark_mode);
        if (use_zuk_healer_phase_hp_delta) {
            reward += s->zuk_healer_phase_hp_delta_coeff *
                (s->damage_zuk_this_tick - s->hp_restored_zuk_this_tick);
        }
        if (!tags_first_gate)
            reward += s->damage_reward_coeff * s->damage_zuk_healers_this_tick;
        if (!tags_first_gate && healer_is_actively_healing) {
            float generic_heal_cost = s->hp_restored_this_tick;
            if (use_zuk_healer_phase_hp_delta)
                generic_heal_cost -= s->hp_restored_zuk_this_tick;
            if (generic_heal_cost < 0.0f) generic_heal_cost = 0.0f;
            reward -= s->damage_reward_coeff * generic_heal_cost;
        }
        if (!tags_first_gate && s->zuk.healer_spawned &&
                s->zuk_untagged_healer_tick_penalty_coeff > 0.0f) {
            reward -= s->zuk_untagged_healer_tick_penalty_coeff *
                (float)inf_count_untagged_zuk_healers(s);
        }
    } else {
        float rewardable_damage = s->damage_dealt_this_tick -
            s->damage_jad_healers_this_tick -
            s->damage_resurrected_this_tick;
        if (jad_is_actively_healed)
            rewardable_damage -= s->damage_jad_this_tick;
        reward = s->damage_reward_coeff *
            fmaxf(0.0f, rewardable_damage - s->hp_restored_this_tick);
    }
    reward += s->tag_reward_coeff * (float)s->healer_tags_this_tick;
    reward += inf_zuk_healer_attack_shape_reward(s);
    if (!tags_first_gate) {
        reward += s->shield_tag_reward_coeff * (float)s->shield_tags_this_tick;
        reward += s->zuk_untagged_healer_target_bonus_coeff *
            (float)s->zuk_untagged_healer_targets_this_tick;
        reward += s->zuk_safe_untagged_healer_target_bonus_coeff *
            (float)s->zuk_safe_untagged_healer_targets_this_tick;
    }

    /* Late-game reward shape. */
    int zuk_idx = inf_find_live_zuk_idx(s);
    int zuk_hp_now = (zuk_idx >= 0) ? s->npcs[zuk_idx].hp : 1200;
    int late_add_reward_active = s->zuk.jad_spawned || zuk_hp_now <= 600;
    if (late_add_reward_active && !tags_first_gate) {
        float rewardable_jad_damage =
            jad_is_actively_healed ? 0.0f : s->damage_jad_this_tick;
        reward += s->jad_damage_reward_coeff * rewardable_jad_damage;
        reward += s->zuk_healer_damage_reward_coeff * s->damage_zuk_healers_this_tick;
        reward += s->set_damage_reward_coeff * s->damage_set_this_tick;
        if (s->tick_at_all_zuk_healers_dead >= 0) {
            reward += s->post_healer_zuk_damage_coeff * s->damage_zuk_this_tick;
            float post_healer_set_damage_reward =
                s->post_healer_set_damage_reward_coeff * s->damage_set_this_tick;
            float post_healer_set_kill_bonus =
                (float)s->kill_set_this_tick * s->post_healer_set_kill_bonus;
            reward += post_healer_set_damage_reward;
            s->post_healer_set_damage_reward_total +=
                post_healer_set_damage_reward;
            s->post_healer_set_kill_bonus_total += post_healer_set_kill_bonus;
            s->pending_set_kill_bonus += post_healer_set_kill_bonus;
            if (s->post_healer_set_alive_tick_penalty_coeff > 0.0f) {
                int set_pressure = inf_count_post_healer_set_pressure(s);
                s->post_healer_set_pressure_total += (float)set_pressure;
                float penalty = s->post_healer_set_alive_tick_penalty_coeff *
                    (float)set_pressure;
                if (s->post_healer_set_alive_penalty_cap > 0.0f) {
                    float remaining = s->post_healer_set_alive_penalty_cap -
                        s->post_healer_set_alive_penalty_total;
                    if (remaining < 0.0f) remaining = 0.0f;
                    if (penalty > remaining) penalty = remaining;
                }
                s->post_healer_set_alive_penalty_total += penalty;
                reward -= penalty;
            }
        }
        s->pending_jad_kill_bonus +=
            (float)s->kill_jad_this_tick * s->jad_kill_bonus;
        s->pending_zuk_healer_kill_bonus +=
            (float)s->kill_zuk_healer_this_tick * s->zuk_healer_kill_bonus;
        s->pending_set_kill_bonus +=
            (float)s->kill_set_this_tick * s->set_kill_bonus;
    }
    /* emit pending kill bonuses gradually so a single-tick kill doesn't
       saturate the [-1, 1] PPO reward clamp. */
    if (!tags_first_gate && s->pending_jad_kill_bonus > 0.0f) {
        float emit = fminf(0.07f, s->pending_jad_kill_bonus);
        reward += emit;
        s->pending_jad_kill_bonus -= emit;
    }
    if (!tags_first_gate && s->pending_zuk_healer_kill_bonus > 0.0f) {
        float emit = fminf(0.05f, s->pending_zuk_healer_kill_bonus);
        reward += emit;
        s->pending_zuk_healer_kill_bonus -= emit;
    }
    if (!tags_first_gate && s->pending_set_kill_bonus > 0.0f) {
        float emit = fminf(0.03f, s->pending_set_kill_bonus);
        reward += emit;
        s->pending_set_kill_bonus -= emit;
    }

    if (!tags_first_gate) {
        float shield_penalty = s->shield_penalty_coeff * s->shield_damage_this_tick;
        if (s->shield_penalty_episode_cap > 0.0f) {
            float remaining = s->shield_penalty_episode_cap -
                s->shield_penalty_episode_total;
            if (remaining < 0.0f) remaining = 0.0f;
            if (shield_penalty > remaining) shield_penalty = remaining;
        }
        s->shield_penalty_episode_total += shield_penalty;
        reward -= shield_penalty;
    }

    if (!tags_first_gate && s->phase_900_bonus > 0.0f && !s->phase_900_fired
            && s->min_zuk_hp_seen <= 900.0f && s->min_zuk_hp_seen > 0.0f) {
        reward += s->phase_900_bonus;
        s->phase_900_fired = 1;
    }
    if (!tags_first_gate && s->phase_600_bonus > 0.0f && !s->phase_600_fired
            && s->min_zuk_hp_seen <= 600.0f && s->min_zuk_hp_seen > 0.0f) {
        reward += s->phase_600_bonus;
        s->phase_600_fired = 1;
    }
    if (!tags_first_gate && s->phase_300_bonus > 0.0f && !s->phase_300_fired
            && s->min_zuk_hp_seen <= 300.0f && s->min_zuk_hp_seen > 0.0f) {
        reward += s->phase_300_bonus;
        s->phase_300_fired = 1;
    }
    return reward;
}

static float inf_terminal_loss_reward(const InfernoState* s) {
    return s->terminal_penalty_enabled ? -1.0f : 0.0f;
}

static void inf_update_healer_transition_stats(InfernoState* s) {
    s->total_shield_tags += s->shield_tags_this_tick;

    if (!s->zuk.healer_spawned) return;

    if (s->tick_at_zuk_healer_spawn < 0)
        s->tick_at_zuk_healer_spawn = s->tick;

    int zuk_idx = inf_find_live_zuk_idx(s);
    if (zuk_idx >= 0) {
        float hp = (float)s->npcs[zuk_idx].hp;
        if (s->tick_at_le_240 < 0 && hp <= 240.0f)
            s->tick_at_le_240 = s->tick;
        if (hp > s->zuk_hp_max_after_healer_spawn)
            s->zuk_hp_max_after_healer_spawn = hp;
    }

    s->total_zuk_healer_tags += s->zuk_healer_tags_this_tick;
    if (s->total_zuk_healer_tags > 4) s->total_zuk_healer_tags = 4;
    s->total_zuk_healer_kills += s->kill_zuk_healer_this_tick;
    if (s->total_zuk_healer_kills > 4) s->total_zuk_healer_kills = 4;

    if (s->total_zuk_healer_tags > 0 && s->tick_at_first_zuk_healer_tag < 0)
        s->tick_at_first_zuk_healer_tag = s->tick;
    if (s->total_zuk_healer_tags >= 4 && s->tick_at_all_zuk_healers_tagged < 0)
        s->tick_at_all_zuk_healers_tagged = s->tick;

    int live_zuk_healers = 0;
    for (int i = 0; i < INF_MAX_NPCS; i++) {
        if (!s->npcs[i].active || s->npcs[i].hp <= 0) continue;
        if (s->npcs[i].type == INF_NPC_HEALER_ZUK)
            live_zuk_healers++;
    }
    if (live_zuk_healers == 0 && s->tick_at_all_zuk_healers_dead < 0) {
        s->tick_at_all_zuk_healers_dead = s->tick;
        s->zuk_hp_at_all_zuk_healers_dead =
            zuk_idx >= 0 ? (float)s->npcs[zuk_idx].hp : 0.0f;
    }

    if (s->tick_at_all_zuk_healers_dead >= 0 && s->damage_zuk_this_tick > 0.0f) {
        s->damage_after_all_zuk_healers_dead += s->damage_zuk_this_tick;
        if (s->tick_at_first_zuk_hit_after_all_healers_dead < 0)
            s->tick_at_first_zuk_hit_after_all_healers_dead = s->tick;
    }

    if (s->tick_at_le_240 >= 0) {
        s->hp_restored_after_240 += s->hp_restored_this_tick;
        s->spark_damage_after_240 += s->spark_damage_this_tick;
    }
}


static void inf_step(EncounterState* state, const int* actions) {
    InfernoState* s = (InfernoState*)state;
    if (s->episode_over) return;

    /* clear per-tick state */
    s->reward = 0.0f;
    s->damage_dealt_this_tick = 0.0f;
    s->damage_resurrected_this_tick = 0.0f;
    s->damage_zuk_this_tick = 0.0f;
    s->damage_zuk_healers_this_tick = 0.0f;
    s->damage_jad_healers_this_tick = 0.0f;
    s->damage_jad_this_tick = 0.0f;
    s->damage_set_this_tick = 0.0f;
    s->kill_jad_this_tick = 0;
    s->kill_zuk_healer_this_tick = 0;
    s->kill_set_this_tick = 0;
    s->shield_damage_this_tick = 0.0f;
    s->healer_tags_this_tick = 0;
    s->zuk_healer_tags_this_tick = 0;
    s->zuk_untagged_healer_targets_this_tick = 0;
    s->zuk_safe_untagged_healer_targets_this_tick = 0;
    s->zuk_untagged_healer_nonmagic_attacks_this_tick = 0;
    s->zuk_healer_mage_attack_fires_this_tick = 0;
    s->shield_tags_this_tick = 0;
    s->spark_damage_this_tick = 0.0f;
    s->damage_received_this_tick = 0.0f;
    s->hp_restored_this_tick = 0.0f;
    s->hp_restored_jad_this_tick = 0.0f;
    s->hp_restored_zuk_this_tick = 0.0f;
    s->prayer_correct_this_tick = 0;
    s->off_prayer_hits_this_tick = 0;
    s->tick_styles_fired = 0;
    s->tick_attacks_fired = 0;
    s->wave_completed_this_tick = 0;
    s->pillar_lost_this_tick = -1;
    s->player_moved_last_tick = s->player_moved_this_tick;
    s->player_moved_this_tick = 0;
    s->player_attacked_this_tick = 0;
    s->player_attack_timing = (EncounterProjectileTiming){0};
    s->brewed_this_tick = 0;
    s->blood_heal_this_tick = 0;
    s->behind_shield_this_tick = 0;
    encounter_clear_tick_flags(&s->player);
    /* clear NPC per-tick flags BEFORE player actions, so hit flags set by
       inf_tick_player survive through inf_tick_npcs into render_post_tick */
    for (int i = 0; i < INF_MAX_NPCS; i++) {
        s->npcs[i].attacked_this_tick = 0;
        s->npcs[i].attack_style_this_tick = ATTACK_STYLE_NONE;
        s->npcs[i].attack_visual_target = -1;
        s->npcs[i].resurrecting_this_tick = 0;
        s->npcs[i].resurrection_visual_target = -1;
        s->npcs[i].moved_this_tick = 0;
        s->npcs[i].hit_landed_this_tick = 0;
        s->npcs[i].hit_damage = 0;
        s->npcs[i].hit_was_successful_this_tick = 0;
        s->npcs[i].hit_spell_type = 0;
    }
    s->tick++;
    inf_player_pretick(s, actions);

    int spawn_wave_now = 0;
    if (s->wave_spawn_delay > 0) {
        s->wave_spawn_delay--;
        if (s->wave_spawn_delay == 0)
            spawn_wave_now = 1;
    }
    int in_wave_gap = (s->wave_spawn_delay > 0 || spawn_wave_now);
    if (s->wave_ready_delay > 0)
        s->wave_ready_delay--;
    int in_ready_gap = s->wave_ready_delay > 0;

    inf_resolve_player_projectiles_on_npcs(s);
    inf_resolve_player_pending_hits(s);
    inf_resolve_pending_sparks(s);

    if (!in_wave_gap && !in_ready_gap) {
        inf_rebuild_player_collision_flags(s);
        inf_invalidate_los_cache(s);
        inf_tick_npcs(s);
    }
    inf_update_healer_transition_stats(s);

    /* Stop before player actions so lethal hits cannot be brewed back from 0 HP. */
    if (s->player.current_hitpoints <= 0) {
        (void)inf_compute_reward(s);
        if (s->last_hit_by_type >= 0 && s->last_hit_by_type < INF_NUM_NPC_TYPES)
            s->killed_by_type[s->last_hit_by_type]++;
        s->episode_over = 1;
        s->winner = 1;
        s->reward = inf_terminal_loss_reward(s);
        s->episode_return += s->reward;
        return;
    }

    /* player actions */
    int can_player_attack = !in_wave_gap && !in_ready_gap;
    int player_x_before_tick_player = s->player.x;
    int player_y_before_tick_player = s->player.y;
    inf_tick_player(s, actions, can_player_attack);
    s->player_moved_this_tick =
        (s->player.x != player_x_before_tick_player ||
         s->player.y != player_y_before_tick_player) ? 1 : 0;
    if (s->player_moved_this_tick)
        inf_invalidate_los_cache(s);
    inf_resolve_jad_prayer_checks_after_player(s);

    /* idle penalty counter: consecutive ticks where player could attack but didn't */
    {
        int has_alive_npc = 0;
        if (can_player_attack) {
            for (int i = 0; i < INF_MAX_NPCS; i++) {
                if (s->npcs[i].active && s->npcs[i].death_ticks == 0) {
                    has_alive_npc = 1; break;
                }
            }
        }
        if (has_alive_npc && s->player.attack_timer == 0 && !s->player_attacked_this_tick)
            s->ticks_without_action++;
        else
            s->ticks_without_action = 0;
    }

    /* accumulate diagnostic counters.
       prayer_correct_this_tick is a count (multiple NPCs can attack same tick).
       total_npc_attacks counts attacks directed at the player (not nibbler→pillar). */
    s->total_prayer_correct += s->prayer_correct_this_tick;
    int ranger_attacked_this_tick = 0;
    int mager_attacked_this_tick = 0;
    for (int i = 0; i < INF_MAX_NPCS; i++) {
        if (s->npcs[i].attacked_this_tick &&
                s->npcs[i].aggro_target < 0 &&
                s->npcs[i].type != INF_NPC_NIBBLER &&
                !(s->npcs[i].type == INF_NPC_BLOB &&
                    s->npcs[i].blob_scanned_prayer >= 0)) {
            s->total_npc_attacks++;
            if (s->npcs[i].type == INF_NPC_RANGER)
                ranger_attacked_this_tick = 1;
            if (s->npcs[i].type == INF_NPC_MAGER)
                mager_attacked_this_tick = 1;
        }
    }
    if (ranger_attacked_this_tick && mager_attacked_this_tick) {
        s->total_ranger_mager_same_tick_attacks++;
        if (s->player_moved_last_tick)
            s->total_step_out_ranger_mager_same_tick_attacks++;
    }
    /* multi-style analysis: count off-prayer hits that were unavoidable because
       a different style was correctly prayed on the same tick. popcount of
       tick_styles_fired tells us how many distinct styles fired. if 2+, the
       off-prayer hits from non-prayed styles are "unavoidable" (can only pray one). */
    if (s->tick_attacks_fired > 0) {
        int styles = s->tick_styles_fired;
        int n_styles = ((styles >> 0) & 1) + ((styles >> 1) & 1) + ((styles >> 2) & 1);
        if (n_styles >= 2 && s->prayer_correct_this_tick > 0) {
            /* we prayed correctly against at least one style, but other styles
               also fired — those off-prayer hits were unavoidable */
            int off_prayer = s->tick_attacks_fired - s->prayer_correct_this_tick;
            s->total_unavoidable_off += off_prayer;
        }
    }
    if (s->ticks_without_action > 0) s->total_idle_ticks++;
    s->total_brews_used += s->brewed_this_tick;
    s->total_blood_healed += s->blood_heal_this_tick;

    if (can_player_attack && s->wave == 68) {
        s->total_zuk_ticks++;
        int is_behind_shield = inf_player_behind_zuk_shield_now(s);
        if (is_behind_shield) {
            s->behind_shield_ticks++;
            s->behind_shield_this_tick = 1;
        }
        if (!is_behind_shield && s->tick_at_le_240 >= 0)
            s->offshield_ticks_after_240++;
        if (!is_behind_shield && s->tick_at_all_zuk_healers_dead >= 0)
            s->offshield_ticks_after_all_zuk_healers_dead++;
    }

    /* bank the tick's irreversible HP progress before computing reward. all
       damage landings and healer applies have resolved by this point. */
    s->reward = inf_compute_reward(s);

    /* check player death */
    if (s->player.current_hitpoints <= 0) {
        if (s->last_hit_by_type >= 0 && s->last_hit_by_type < INF_NUM_NPC_TYPES)
            s->killed_by_type[s->last_hit_by_type]++;
        s->episode_over = 1;
        s->winner = 1;
        s->reward = inf_terminal_loss_reward(s);
        goto finish_step;
    }

    if (spawn_wave_now) {
        s->wave = s->wave_spawn_target;
        inf_spawn_wave(s);
        inf_invalidate_los_cache(s);
        goto finish_step;
    }
    if (s->wave_spawn_delay > 0) goto finish_step;

    /* check wave completion */
    int all_dead = 1;
    for (int i = 0; i < INF_MAX_NPCS; i++) {
        if (s->npcs[i].active) { all_dead = 0; break; }
    }
    if (all_dead) {
        s->wave_completed_this_tick = 1;
        s->total_waves_cleared++;
        int next_public_wave = s->wave + 2;
        float wave_clear_reward = 1.0f +
            inf_supply_milestone_surplus_reward(s, next_public_wave);
        s->reward = wave_clear_reward;
        if (s->wave + 1 >= INF_NUM_WAVES) {
            s->episode_over = 1;
            s->winner = 0;
        } else {
            s->wave_spawn_target = s->wave + 1;
            s->wave_spawn_delay = 9;
        }
    }

    if (s->tick >= INF_MAX_TICKS) {
        s->episode_over = 1;
        s->winner = 1;
        s->reward = inf_terminal_loss_reward(s);
    }

finish_step:
    s->episode_return += s->reward;
}


/* obs layout: player + Zuk phase + pillars + NPC slots + step-out forecast + pending hits + pending Zuk healer sparks */
#define INF_PLAYER_OBS_SIZE 57
#define INF_BASE_NPC_OBS_SIZE 415
#define INF_TOTAL_NPC_OBS_SIZE INF_BASE_NPC_OBS_SIZE
#define INF_STEP_OUT_FORECAST_HORIZON 4
#define INF_STEP_OUT_FORECAST_TICK_FEATURES 7
#define INF_STEP_OUT_FORECAST_ACTION_FEATURES 8
#define INF_STEP_OUT_FORECAST_OBS_SIZE (ENCOUNTER_MOVE_ACTIONS * INF_STEP_OUT_FORECAST_ACTION_FEATURES)
#define INF_FEATURES_PER_HIT 5
#define INF_SPARK_OBS_SLOTS 4
#define INF_FEATURES_PER_SPARK 5
#define INF_PENDING_HIT_OBS_SIZE (INF_FEATURES_PER_HIT * ENCOUNTER_MAX_PENDING_HITS)
#define INF_PENDING_SPARK_OBS_SIZE (INF_FEATURES_PER_SPARK * INF_SPARK_OBS_SLOTS)
#define INF_NUM_OBS (INF_PLAYER_OBS_SIZE + 12 + INF_TOTAL_NPC_OBS_SIZE + INF_STEP_OUT_FORECAST_OBS_SIZE + INF_PENDING_HIT_OBS_SIZE + INF_PENDING_SPARK_OBS_SIZE)

typedef struct {
    int melee_count;
    int ranged_count;
    int magic_count;
    int blob_scan_count;
    int ranger_count;
    int mager_count;
    int max_hit;
} InfStepOutForecastTick;

typedef struct {
    int valid;
    int land_x;
    int land_y;
    InfStepOutForecastTick ticks[INF_STEP_OUT_FORECAST_HORIZON];
    int same_tick_mixed_style_conflict;
    int ranger_mager_offtick_opportunity;
    int melee_fallback_exposure;
} InfStepOutForecastAction;

typedef struct {
    InfStepOutForecastAction actions[ENCOUNTER_MOVE_ACTIONS];
} InfStepOutForecast;

/* max hit per NPC type, normalized by mager max (70). for prayer priority obs. */
static const float INF_NPC_MAX_HIT_NORM[INF_NUM_NPC_TYPES] = {
    [INF_NPC_NIBBLER]    = 0.0f,
    [INF_NPC_BAT]        = 19.0f / 70.0f,
    [INF_NPC_BLOB]       = 29.0f / 70.0f,
    [INF_NPC_BLOB_MELEE] = 18.0f / 70.0f,
    [INF_NPC_BLOB_RANGE] = 18.0f / 70.0f,
    [INF_NPC_BLOB_MAGE]  = 25.0f / 70.0f,
    [INF_NPC_MELEER]     = 49.0f / 70.0f,
    [INF_NPC_RANGER]     = 46.0f / 70.0f,
    [INF_NPC_MAGER]      = 70.0f / 70.0f,
    [INF_NPC_JAD]        = 113.0f / 70.0f,
    [INF_NPC_ZUK]        = 148.0f / 70.0f,
    [INF_NPC_HEALER_JAD] = 13.0f / 70.0f,
    [INF_NPC_HEALER_ZUK] = 24.0f / 70.0f,
    [INF_NPC_ZUK_SHIELD] = 0.0f,
};

static float inf_zuk_attack_timer_obs(const InfernoState* s) {
    if (!inf_is_final_wave(s)) return 0.0f;

    int min_timer = 999;
    int zuk_idx = inf_find_live_zuk_idx(s);
    if (zuk_idx >= 0) {
        int timer = s->npcs[zuk_idx].attack_timer;
        if (timer < 0) timer = 0;
        min_timer = timer;
    }

    for (int h = 0; h < s->player_pending_hit_count; h++) {
        const EncounterPendingHit* hit = &s->player_pending_hits[h];
        if (hit->source_npc_type != INF_NPC_ZUK) continue;
        if (hit->ticks_remaining < min_timer)
            min_timer = hit->ticks_remaining;
    }

    return (min_timer < 999) ? (float)min_timer / 10.0f : 0.0f;
}

static int inf_npc_targets_player_for_obs(const InfernoState* s, const InfNPC* npc) {
    InfTargetArea target = inf_npc_current_target_area(s, npc);
    return target.is_player;
}

static int inf_npc_planned_style_for_obs(const InfNPC* npc) {
    if (npc->type == INF_NPC_BLOB && npc->blob_scanned_prayer >= 0) {
        OverheadPrayer scanned = (OverheadPrayer)npc->blob_scanned_prayer;
        if (scanned == PRAYER_PROTECT_MAGIC) return ATTACK_STYLE_RANGED;
        if (scanned == PRAYER_PROTECT_RANGED) return ATTACK_STYLE_MAGIC;
    }
    if (npc->type == INF_NPC_JAD)
        return npc->jad_attack_style;
    return npc->attack_style;
}

static int inf_step_out_forecast_action_valid(InfernoState* s, int action) {
    if (action == 0) return 1;
    int nx = s->player.x + ENCOUNTER_MOVE_TARGET_DX[action];
    int ny = s->player.y + ENCOUNTER_MOVE_TARGET_DY[action];
    return inf_in_arena(nx, ny) && !inf_blocked_by_pillar(s, nx, ny, 1);
}

static int inf_forecast_style_max_hit(
    const InfNPCStats* stats, int style
) {
    if (style == ATTACK_STYLE_NONE) return 0;
    int max_hit = osrs_npc_max_hit(style,
        stats->str_level, stats->range_level,
        stats->melee_str_bonus, stats->ranged_str_bonus,
        stats->magic_base_dmg, stats->magic_dmg_pct);
    if (stats->max_hit_cap > 0 && max_hit > stats->max_hit_cap)
        max_hit = stats->max_hit_cap;
    return max_hit;
}

static void inf_step_out_forecast_record_style_mask(
    InfStepOutForecastAction* action,
    int tick_idx,
    InfNPCType type,
    const InfNPCStats* stats,
    int style_mask
) {
    if (tick_idx < 0 || tick_idx >= INF_STEP_OUT_FORECAST_HORIZON) return;
    InfStepOutForecastTick* tick = &action->ticks[tick_idx];
    int styles = 0;
    if (style_mask & INF_STYLE_MASK_MELEE) {
        tick->melee_count++;
        int max_hit = inf_forecast_style_max_hit(stats, ATTACK_STYLE_MELEE);
        if (max_hit > tick->max_hit) tick->max_hit = max_hit;
        styles++;
    }
    if (style_mask & INF_STYLE_MASK_RANGED) {
        tick->ranged_count++;
        int max_hit = inf_forecast_style_max_hit(stats, ATTACK_STYLE_RANGED);
        if (max_hit > tick->max_hit) tick->max_hit = max_hit;
        styles++;
    }
    if (style_mask & INF_STYLE_MASK_MAGIC) {
        tick->magic_count++;
        int max_hit = inf_forecast_style_max_hit(stats, ATTACK_STYLE_MAGIC);
        if (max_hit > tick->max_hit) tick->max_hit = max_hit;
        styles++;
    }
    if (type == INF_NPC_RANGER) tick->ranger_count++;
    if (type == INF_NPC_MAGER) tick->mager_count++;
    if (styles >= 2 || ((tick->melee_count > 0) +
            (tick->ranged_count > 0) + (tick->magic_count > 0)) >= 2) {
        action->same_tick_mixed_style_conflict = 1;
    }
    if ((style_mask & INF_STYLE_MASK_MELEE) &&
            (type == INF_NPC_RANGER || type == INF_NPC_MAGER)) {
        action->melee_fallback_exposure = 1;
    }
}

static void inf_step_out_forecast_record_blob_scan(
    InfStepOutForecastAction* action, int tick_idx
) {
    if (tick_idx < 0 || tick_idx >= INF_STEP_OUT_FORECAST_HORIZON) return;
    action->ticks[tick_idx].blob_scan_count++;
}

static int inf_forecast_jad_unknown_style_mask(const InfNPC* npc) {
    if (npc->type != INF_NPC_JAD) return 0;
    if (npc->jad_attack_style != ATTACK_STYLE_NONE) return 0;
    return INF_STYLE_MASK_RANGED | INF_STYLE_MASK_MAGIC;
}

static void inf_step_out_forecast_npc_attack(
    InfernoState* s,
    int idx,
    InfStepOutForecastAction* action,
    int tick_idx
) {
    InfNPC* npc = &s->npcs[idx];
    if (!npc->active || npc->death_ticks > 0 || npc->hp <= 0) return;
    const InfNPCStats* stats = &INF_NPC_STATS[npc->type];
    if (npc->attack_timer > 0) npc->attack_timer--;
    if (npc->stun_timer > 0) { npc->stun_timer--; return; }
    if (npc->dig_freeze_timer > 0 || npc->dig_attack_delay > 0) return;
    if (npc->type == INF_NPC_ZUK_SHIELD || npc->type == INF_NPC_NIBBLER)
        return;
    if (npc->aggro_target >= 0) return;
    if (!inf_npc_targets_player_for_obs(s, npc)) return;

    int has_los_now = 0;
    if (npc->type == INF_NPC_BLOB && stats->attack_range > 1) {
        has_los_now = inf_npc_has_los(s, idx);
        if (npc->blob_scanned_prayer < 0 &&
            has_los_now &&
            !npc->had_los_last_tick) {
            npc->blob_scanned_prayer = (int)s->player.prayer;
            if (s->player.prayer == PRAYER_PROTECT_MAGIC) npc->attack_style = ATTACK_STYLE_RANGED;
            else if (s->player.prayer == PRAYER_PROTECT_RANGED) npc->attack_style = ATTACK_STYLE_MAGIC;
            else npc->attack_style = ATTACK_STYLE_RANGED;
            npc->had_los_last_tick = has_los_now;
            npc->attack_timer = stats->attack_speed;
            inf_step_out_forecast_record_blob_scan(action, tick_idx);
            return;
        }
        npc->had_los_last_tick = has_los_now;
    }
    if (npc->attack_timer > 0) return;

    if (npc->type != INF_NPC_BLOB && stats->attack_range > 1)
        has_los_now = inf_npc_has_los(s, idx);

    if (stats->attack_range > 1 &&
        !(npc->type == INF_NPC_BLOB && npc->blob_scanned_prayer >= 0) &&
        !has_los_now) return;

    int dist = encounter_dist_to_npc(
        s->player.x, s->player.y, npc->x, npc->y, npc->size);
    if (dist == 0 || dist > stats->attack_range) return;

    if (npc->type == INF_NPC_BLOB && npc->blob_scanned_prayer < 0) {
        npc->blob_scanned_prayer = (int)s->player.prayer;
        if (s->player.prayer == PRAYER_PROTECT_MAGIC) npc->attack_style = ATTACK_STYLE_RANGED;
        else if (s->player.prayer == PRAYER_PROTECT_RANGED) npc->attack_style = ATTACK_STYLE_MAGIC;
        else npc->attack_style = ATTACK_STYLE_RANGED;
        npc->attack_timer = stats->attack_speed;
        inf_step_out_forecast_record_blob_scan(action, tick_idx);
        return;
    }

    int style_mask = inf_forecast_jad_unknown_style_mask(npc);
    if (style_mask == 0) {
        int planned_style = inf_npc_planned_style_for_obs(npc);
        if (planned_style == ATTACK_STYLE_NONE) return;
        style_mask = inf_attack_style_options_mask(
            s, npc, stats, planned_style, dist);
    }
    if (style_mask == 0) return;

    inf_step_out_forecast_record_style_mask(
        action, tick_idx, npc->type, stats, style_mask);

    if (npc->type == INF_NPC_BLOB)
        npc->blob_scanned_prayer = -1;
    if (npc->type == INF_NPC_JAD)
        npc->jad_attack_style = ATTACK_STYLE_NONE;
    npc->attack_timer = stats->attack_speed;
    if (npc->type == INF_NPC_JAD) {
        if (s->wave == 66)      npc->attack_timer = 8;
        else if (s->wave == 67) npc->attack_timer = 9;
        else                    npc->attack_timer = 8;
    }
}

static int inf_collect_step_out_forecast_slots(
    const InfernoState* s,
    int slots[INF_MAX_NPCS]
) {
    int count = 0;
    for (int i = 0; i < INF_MAX_NPCS; i++) {
        const InfNPC* npc = &s->npcs[i];
        if (!npc->active || npc->death_ticks > 0) continue;
        slots[count++] = i;
    }
    return count;
}

static void inf_step_out_forecast_tick(
    InfernoState* sim,
    InfStepOutForecastAction* action,
    int tick_idx,
    const int slots[INF_MAX_NPCS],
    int slot_count
) {
    for (int slot_idx = 0; slot_idx < slot_count; slot_idx++) {
        int i = slots[slot_idx];
        InfNPC* npc = &sim->npcs[i];
        if (npc->frozen_ticks > 0) npc->frozen_ticks--;
        if (npc->type == INF_NPC_MAGER && npc->resurrect_cooldown > 0)
            npc->resurrect_cooldown--;
        inf_npc_move(sim, i);
        inf_step_out_forecast_npc_attack(sim, i, action, tick_idx);
    }
}

static void inf_step_out_forecast_finalize_action(
    InfStepOutForecastAction* action
) {
    for (int t = 0; t < INF_STEP_OUT_FORECAST_HORIZON - 1; t++) {
        int ranger_then_mager = action->ticks[t].ranger_count > 0 &&
            action->ticks[t + 1].mager_count > 0;
        int mager_then_ranger = action->ticks[t].mager_count > 0 &&
            action->ticks[t + 1].ranger_count > 0;
        if (ranger_then_mager || mager_then_ranger)
            action->ranger_mager_offtick_opportunity = 1;
    }
}

static int inf_step_out_forecast_tick_style_mask(
    const InfStepOutForecastTick* tick
) {
    int mask = 0;
    if (tick->melee_count > 0) mask |= INF_STYLE_MASK_MELEE;
    if (tick->ranged_count > 0) mask |= INF_STYLE_MASK_RANGED;
    if (tick->magic_count > 0) mask |= INF_STYLE_MASK_MAGIC;
    return mask;
}

static int inf_step_out_forecast_tick_has_event(
    const InfStepOutForecastTick* tick
) {
    return tick->melee_count > 0 ||
        tick->ranged_count > 0 ||
        tick->magic_count > 0 ||
        tick->blob_scan_count > 0;
}

static void inf_build_step_out_forecast(
    const InfernoState* s,
    InfStepOutForecast* out
) {
    memset(out, 0, sizeof(*out));
    int forecast_slots[INF_MAX_NPCS];
    int forecast_slot_count = inf_collect_step_out_forecast_slots(
        s, forecast_slots);
    for (int action_idx = 0; action_idx < ENCOUNTER_MOVE_ACTIONS; action_idx++) {
        InfStepOutForecastAction* action = &out->actions[action_idx];
        Player moved = s->player;
        action->valid = inf_step_out_forecast_action_valid(
            (InfernoState*)s, action_idx);
        if (action_idx > 0) {
            encounter_move_to_target(
                &moved,
                ENCOUNTER_MOVE_TARGET_DX[action_idx],
                ENCOUNTER_MOVE_TARGET_DY[action_idx],
                inf_tile_walkable,
                (void*)s);
        }
        action->land_x = moved.x;
        action->land_y = moved.y;
        if (!action->valid || forecast_slot_count == 0) continue;

        InfernoState sim = *s;
        sim.player.x = moved.x;
        sim.player.y = moved.y;
        sim.player.is_running = moved.is_running;
        inf_invalidate_los_cache(&sim);
        inf_rebuild_player_collision_flags(&sim);

        for (int tick_idx = 0; tick_idx < INF_STEP_OUT_FORECAST_HORIZON; tick_idx++) {
            inf_step_out_forecast_tick(
                &sim, action, tick_idx, forecast_slots, forecast_slot_count);
        }
        inf_step_out_forecast_finalize_action(action);
    }
}

#define INF_LAB_PILLAR_CONTEXT_RADIUS (INF_PILLAR_SIZE + 2)

typedef enum {
    INF_LAB_COMMAND_NONE = 0,
    INF_LAB_COMMAND_RESET,
    INF_LAB_COMMAND_SET_PLAYER,
    INF_LAB_COMMAND_SPAWN_NPC,
    INF_LAB_COMMAND_MOVE_NPC,
    INF_LAB_COMMAND_DELETE_NPC,
    INF_LAB_COMMAND_KILL_NPC,
    INF_LAB_COMMAND_SET_NPC_HP,
    INF_LAB_COMMAND_SET_NPC_TIMER,
    INF_LAB_COMMAND_SET_PILLAR,
    INF_LAB_COMMAND_SPAWN_WAVE,
    INF_LAB_COMMAND_CLEAR_NPCS,
    INF_LAB_COMMAND_STEP_TICKS,
} InfLabCommandKind;

typedef enum {
    INF_LAB_LINE_NONE = 0,
    INF_LAB_LINE_FORECAST,
    INF_LAB_LINE_DUMP,
} InfLabLineResult;

typedef enum {
    INF_LAB_PILLAR_REMOVED = 0,
    INF_LAB_PILLAR_ACTIVE,
} InfLabPillarState;

typedef enum {
    INF_LAB_OPTIONAL_INT_UNSET = 0,
    INF_LAB_OPTIONAL_INT_SET,
} InfLabOptionalIntKind;

typedef struct {
    InfLabOptionalIntKind kind;
    int value;
} InfLabOptionalInt;

static inline InfLabOptionalInt inf_lab_optional_int_unset(void) {
    return (InfLabOptionalInt){ .kind = INF_LAB_OPTIONAL_INT_UNSET };
}

static inline InfLabOptionalInt inf_lab_optional_int_set(int value) {
    return (InfLabOptionalInt){
        .kind = INF_LAB_OPTIONAL_INT_SET,
        .value = value,
    };
}

typedef struct {
    int x;
    int y;
} InfLabTileCommand;

typedef struct {
    int slot;
    InfNPCType type;
    int x;
    int y;
    InfLabOptionalInt hp;
    InfLabOptionalInt timer;
} InfLabSpawnNpcCommand;

typedef struct {
    int slot;
    int x;
    int y;
} InfLabMoveNpcCommand;

typedef struct {
    int slot;
} InfLabNpcSlotCommand;

typedef struct {
    int slot;
    int hp;
} InfLabNpcHpCommand;

typedef struct {
    int slot;
    int timer;
} InfLabNpcTimerCommand;

typedef struct {
    int pillar_idx;
    InfLabPillarState state;
    InfLabOptionalInt hp;
} InfLabPillarCommand;

typedef struct {
    int wave;
} InfLabWaveCommand;

typedef struct {
    int ticks;
} InfLabStepTicksCommand;

typedef struct {
    uint32_t seed;
} InfLabResetCommand;

typedef struct {
    InfLabCommandKind kind;
    union {
        InfLabResetCommand reset;
        InfLabTileCommand tile;
        InfLabSpawnNpcCommand spawn_npc;
        InfLabMoveNpcCommand move_npc;
        InfLabNpcSlotCommand npc_slot;
        InfLabNpcHpCommand npc_hp;
        InfLabNpcTimerCommand npc_timer;
        InfLabPillarCommand pillar;
        InfLabWaveCommand wave;
        InfLabStepTicksCommand step_ticks;
    } as;
} InfernoLabCommand;

typedef struct {
    char* data;
    size_t len;
    size_t cap;
} InfLabString;

static void inf_lab_abort(const char* fmt, ...) {
    va_list args;
    fprintf(stderr, "inferno lab: ");
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
    fprintf(stderr, "\n");
    abort();
}

static void inf_lab_require_slot(int slot) {
    if (slot < 0 || slot >= INF_MAX_NPCS)
        inf_lab_abort("npc slot must be in [0,%d], got %d", INF_MAX_NPCS - 1, slot);
}

static void inf_lab_require_type(int type) {
    if (type < 0 || type >= INF_NUM_NPC_TYPES)
        inf_lab_abort("npc type must be in [0,%d], got %d", INF_NUM_NPC_TYPES - 1, type);
}

static void inf_lab_require_player_tile(InfernoState* s, int x, int y) {
    if (!inf_in_arena(x, y))
        inf_lab_abort("player tile out of arena: (%d,%d)", x, y);
    if (inf_blocked_by_pillar(s, x, y, 1))
        inf_lab_abort("player tile is blocked by a pillar: (%d,%d)", x, y);
}

static void inf_lab_clear_transient(InfernoState* s) {
    osrs_interaction_init(&s->interaction);
    s->player_last_interaction_target_slot = -1;
    s->player_last_interaction_age = 1;
    s->player_dest_x = -1;
    s->player_dest_y = -1;
    s->player_pending_hit_count = 0;
    memset(s->pending_sparks, 0, sizeof(s->pending_sparks));
    memset(s->npc_target_hits, 0, sizeof(s->npc_target_hits));
    s->player_attacked_this_tick = 0;
    s->player_attack_npc_idx = -1;
    s->player_attack_dmg = 0;
    s->player_attack_style_id = ATTACK_STYLE_NONE;
    for (int i = 0; i < INF_MAX_NPCS; i++) {
        InfNPC* npc = &s->npcs[i];
        npc->attacked_this_tick = 0;
        npc->attack_style_this_tick = ATTACK_STYLE_NONE;
        npc->attack_visual_target = -1;
        npc->moved_this_tick = 0;
        npc->hit_landed_this_tick = 0;
        npc->hit_damage = 0;
        npc->hit_was_successful_this_tick = 0;
        npc->hit_spell_type = ENCOUNTER_SPELL_NONE;
        memset(&npc->pending_hit, 0, sizeof(npc->pending_hit));
    }
}

static void inf_lab_refresh_geometry(InfernoState* s) {
    inf_rebuild_los(s);
    inf_invalidate_los_cache(s);
    inf_rebuild_entity_collision_flags(s);
    inf_refresh_current_obs_slots(s);
}

static void inf_lab_remove_all_npcs(InfernoState* s) {
    memset(s->npcs, 0, sizeof(s->npcs));
    s->dead_mob_count = 0;
}

static void inf_lab_clear_npcs(InfernoState* s) {
    inf_lab_remove_all_npcs(s);
    inf_lab_clear_transient(s);
    inf_lab_refresh_geometry(s);
}

static void inf_lab_apply_command(InfernoState* s, const InfernoLabCommand* cmd) {
    if (!s || !cmd) inf_lab_abort("null command");

    switch (cmd->kind) {
        case INF_LAB_COMMAND_NONE:
            return;

        case INF_LAB_COMMAND_RESET:
            inf_reset((EncounterState*)s, cmd->as.reset.seed);
            return;

        case INF_LAB_COMMAND_SET_PLAYER:
            inf_lab_require_player_tile(s, cmd->as.tile.x, cmd->as.tile.y);
            s->player.x = cmd->as.tile.x;
            s->player.y = cmd->as.tile.y;
            inf_lab_clear_transient(s);
            inf_lab_refresh_geometry(s);
            return;

        case INF_LAB_COMMAND_SPAWN_NPC: {
            const InfLabSpawnNpcCommand* spawn = &cmd->as.spawn_npc;
            inf_lab_require_slot(spawn->slot);
            inf_lab_require_type(spawn->type);
            if (s->npcs[spawn->slot].active)
                inf_deactivate_npc(s, spawn->slot);
            inf_init_npc(s, spawn->slot, spawn->type, spawn->x, spawn->y);
            if (spawn->hp.kind == INF_LAB_OPTIONAL_INT_SET) {
                int hp = spawn->hp.value;
                if (hp < 0) inf_lab_abort("npc hp must be nonnegative");
                if (hp > s->npcs[spawn->slot].max_hp)
                    inf_lab_abort("npc hp %d exceeds max hp %d",
                        hp, s->npcs[spawn->slot].max_hp);
                s->npcs[spawn->slot].hp = hp;
            }
            if (spawn->timer.kind == INF_LAB_OPTIONAL_INT_SET) {
                if (spawn->timer.value < 0)
                    inf_lab_abort("npc timer must be nonnegative");
                s->npcs[spawn->slot].attack_timer = spawn->timer.value;
            }
            inf_lab_clear_transient(s);
            inf_lab_refresh_geometry(s);
            return;
        }

        case INF_LAB_COMMAND_MOVE_NPC: {
            const InfLabMoveNpcCommand* move = &cmd->as.move_npc;
            inf_lab_require_slot(move->slot);
            if (!s->npcs[move->slot].active)
                inf_lab_abort("cannot move inactive npc slot %d", move->slot);
            s->npcs[move->slot].x = move->x;
            s->npcs[move->slot].y = move->y;
            s->npcs[move->slot].target_x = move->x;
            s->npcs[move->slot].target_y = move->y;
            inf_lab_clear_transient(s);
            inf_lab_refresh_geometry(s);
            return;
        }

        case INF_LAB_COMMAND_DELETE_NPC: {
            int slot = cmd->as.npc_slot.slot;
            inf_lab_require_slot(slot);
            inf_deactivate_npc(s, slot);
            inf_lab_clear_transient(s);
            inf_lab_refresh_geometry(s);
            return;
        }

        case INF_LAB_COMMAND_KILL_NPC: {
            int slot = cmd->as.npc_slot.slot;
            inf_lab_require_slot(slot);
            if (!s->npcs[slot].active)
                inf_lab_abort("cannot kill inactive npc slot %d", slot);
            s->npcs[slot].hp = 0;
            inf_apply_npc_death(s, slot);
            inf_lab_refresh_geometry(s);
            return;
        }

        case INF_LAB_COMMAND_SET_NPC_HP: {
            int slot = cmd->as.npc_hp.slot;
            int hp = cmd->as.npc_hp.hp;
            inf_lab_require_slot(slot);
            if (!s->npcs[slot].active)
                inf_lab_abort("cannot set hp for inactive npc slot %d", slot);
            if (hp < 0) inf_lab_abort("npc hp must be nonnegative");
            if (hp > s->npcs[slot].max_hp)
                inf_lab_abort("npc hp %d exceeds max hp %d",
                    hp, s->npcs[slot].max_hp);
            s->npcs[slot].hp = hp;
            inf_lab_refresh_geometry(s);
            return;
        }

        case INF_LAB_COMMAND_SET_NPC_TIMER: {
            int slot = cmd->as.npc_timer.slot;
            int timer = cmd->as.npc_timer.timer;
            inf_lab_require_slot(slot);
            if (!s->npcs[slot].active)
                inf_lab_abort("cannot set timer for inactive npc slot %d", slot);
            if (timer < 0) inf_lab_abort("npc timer must be nonnegative");
            s->npcs[slot].attack_timer = timer;
            inf_lab_refresh_geometry(s);
            return;
        }

        case INF_LAB_COMMAND_SET_PILLAR: {
            const InfLabPillarCommand* pillar = &cmd->as.pillar;
            if (pillar->pillar_idx < 0 || pillar->pillar_idx >= INF_NUM_PILLARS)
                inf_lab_abort("pillar idx must be in [0,%d], got %d",
                    INF_NUM_PILLARS - 1, pillar->pillar_idx);
            s->pillars[pillar->pillar_idx].active =
                pillar->state == INF_LAB_PILLAR_ACTIVE;
            if (pillar->state == INF_LAB_PILLAR_ACTIVE) {
                if (pillar->hp.kind == INF_LAB_OPTIONAL_INT_UNSET) {
                    s->pillars[pillar->pillar_idx].hp = INF_PILLAR_HP;
                } else if (pillar->hp.value <= 0 ||
                        pillar->hp.value > INF_PILLAR_HP) {
                    inf_lab_abort("pillar hp must be in [1,%d], got %d",
                        INF_PILLAR_HP, pillar->hp.value);
                } else {
                    s->pillars[pillar->pillar_idx].hp = pillar->hp.value;
                }
            } else {
                s->pillars[pillar->pillar_idx].hp = 0;
            }
            inf_lab_clear_transient(s);
            inf_lab_refresh_geometry(s);
            return;
        }

        case INF_LAB_COMMAND_SPAWN_WAVE:
            inf_require_valid_public_wave(cmd->as.wave.wave);
            inf_lab_remove_all_npcs(s);
            s->wave = cmd->as.wave.wave - 1;
            s->wave_spawn_target = s->wave;
            s->wave_spawn_delay = 0;
            s->wave_ready_delay = INF_START_READY_TICKS;
            inf_spawn_wave(s);
            inf_lab_clear_transient(s);
            inf_lab_refresh_geometry(s);
            return;

        case INF_LAB_COMMAND_CLEAR_NPCS:
            inf_lab_clear_npcs(s);
            return;

        case INF_LAB_COMMAND_STEP_TICKS: {
            int ticks = cmd->as.step_ticks.ticks;
            if (ticks < 0) inf_lab_abort("step_ticks must be nonnegative");
            int actions[INF_NUM_ACTION_HEADS] = {0};
            for (int t = 0; t < ticks; t++)
                inf_step((EncounterState*)s, actions);
            inf_lab_refresh_geometry(s);
            return;
        }
    }

    inf_lab_abort("unknown command kind %d", cmd->kind);
}

static int inf_lab_nearest_pillar_idx(const InfernoState* s, int x, int y) {
    int best_idx = -1;
    int best_dist = INT32_MAX;
    for (int p = 0; p < INF_NUM_PILLARS; p++) {
        int cx = s->pillars[p].x + INF_PILLAR_SIZE / 2;
        int cy = s->pillars[p].y + INF_PILLAR_SIZE / 2;
        int dist = abs(cx - x) + abs(cy - y);
        if (dist < best_dist) {
            best_dist = dist;
            best_idx = p;
        }
    }
    return best_dist <= INF_LAB_PILLAR_CONTEXT_RADIUS ? best_idx : -1;
}

static const char* inf_lab_npc_type_name(int type) {
    switch (type) {
        case INF_NPC_NIBBLER: return "nibbler";
        case INF_NPC_BAT: return "bat";
        case INF_NPC_BLOB: return "blob";
        case INF_NPC_BLOB_MELEE: return "blob_melee";
        case INF_NPC_BLOB_RANGE: return "blob_range";
        case INF_NPC_BLOB_MAGE: return "blob_mage";
        case INF_NPC_MELEER: return "meleer";
        case INF_NPC_RANGER: return "ranger";
        case INF_NPC_MAGER: return "mager";
        case INF_NPC_JAD: return "jad";
        case INF_NPC_ZUK: return "zuk";
        case INF_NPC_HEALER_JAD: return "healer_jad";
        case INF_NPC_HEALER_ZUK: return "healer_zuk";
        case INF_NPC_ZUK_SHIELD: return "zuk_shield";
        default: return "unknown";
    }
}

static int inf_lab_parse_npc_type(const char* value) {
    if (strcmp(value, "nibbler") == 0) return INF_NPC_NIBBLER;
    if (strcmp(value, "bat") == 0) return INF_NPC_BAT;
    if (strcmp(value, "blob") == 0) return INF_NPC_BLOB;
    if (strcmp(value, "blob_melee") == 0) return INF_NPC_BLOB_MELEE;
    if (strcmp(value, "blob_range") == 0) return INF_NPC_BLOB_RANGE;
    if (strcmp(value, "blob_mage") == 0) return INF_NPC_BLOB_MAGE;
    if (strcmp(value, "meleer") == 0 || strcmp(value, "melee") == 0)
        return INF_NPC_MELEER;
    if (strcmp(value, "ranger") == 0 || strcmp(value, "range") == 0)
        return INF_NPC_RANGER;
    if (strcmp(value, "mager") == 0 || strcmp(value, "mage") == 0)
        return INF_NPC_MAGER;
    if (strcmp(value, "jad") == 0) return INF_NPC_JAD;
    if (strcmp(value, "zuk") == 0) return INF_NPC_ZUK;
    if (strcmp(value, "healer_jad") == 0 || strcmp(value, "jad_healer") == 0)
        return INF_NPC_HEALER_JAD;
    if (strcmp(value, "healer_zuk") == 0 || strcmp(value, "zuk_healer") == 0)
        return INF_NPC_HEALER_ZUK;
    if (strcmp(value, "zuk_shield") == 0 || strcmp(value, "shield") == 0)
        return INF_NPC_ZUK_SHIELD;

    char* end = NULL;
    errno = 0;
    long parsed = strtol(value, &end, 10);
    if (errno == 0 && end && *end == '\0') {
        inf_lab_require_type((int)parsed);
        return (int)parsed;
    }
    inf_lab_abort("unknown npc type %s", value);
    return 0;
}

static int inf_lab_parse_int_value(const char* value) {
    char* end = NULL;
    errno = 0;
    long parsed = strtol(value, &end, 10);
    if (errno != 0 || !end || *end != '\0' ||
            parsed < INT32_MIN || parsed > INT32_MAX) {
        inf_lab_abort("invalid integer %s", value);
    }
    return (int)parsed;
}

static uint32_t inf_lab_parse_seed_value(const char* value) {
    char* end = NULL;
    errno = 0;
    unsigned long parsed = strtoul(value, &end, 10);
    if (errno != 0 || !end || *end != '\0' || parsed > UINT32_MAX)
        inf_lab_abort("invalid seed %s", value);
    return (uint32_t)parsed;
}

static InfLabOptionalInt inf_lab_parse_optional_hp_value(const char* value) {
    if (strcmp(value, "full") == 0) return inf_lab_optional_int_unset();
    return inf_lab_optional_int_set(inf_lab_parse_int_value(value));
}

static char* inf_lab_trim(char* s) {
    while (*s && isspace((unsigned char)*s)) s++;
    char* end = s + strlen(s);
    while (end > s && isspace((unsigned char)end[-1])) end--;
    *end = '\0';
    return s;
}

static void inf_lab_parse_key_value(
    char* token, const char** key, const char** value
) {
    char* eq = strchr(token, '=');
    if (!eq || eq == token || eq[1] == '\0')
        inf_lab_abort("expected key=value token, got %s", token);
    *eq = '\0';
    *key = token;
    *value = eq + 1;
}

typedef struct {
    int slot;
    int type;
    int x;
    int y;
    InfLabOptionalInt hp;
    InfLabOptionalInt timer;
    int pillar_idx;
    InfLabPillarState pillar_state;
    int wave;
    int ticks;
    uint32_t seed;
} InfLabParsedArgs;

static InfLabParsedArgs inf_lab_parsed_args_default(void) {
    return (InfLabParsedArgs){
        .slot = -1,
        .type = -1,
        .x = INT32_MIN,
        .y = INT32_MIN,
        .hp = { .kind = INF_LAB_OPTIONAL_INT_UNSET },
        .timer = { .kind = INF_LAB_OPTIONAL_INT_UNSET },
        .pillar_idx = -1,
        .pillar_state = INF_LAB_PILLAR_ACTIVE,
        .wave = -1,
        .ticks = 0,
        .seed = 0,
    };
}

static void inf_lab_apply_script_arg(
    InfLabParsedArgs* args,
    InfLabCommandKind kind,
    const char* command,
    const char* key,
    const char* value
) {
    switch (kind) {
        case INF_LAB_COMMAND_RESET:
            if (strcmp(key, "seed") == 0) {
                args->seed = inf_lab_parse_seed_value(value);
                return;
            }
            break;

        case INF_LAB_COMMAND_SET_PLAYER:
            if (strcmp(key, "x") == 0) {
                args->x = inf_lab_parse_int_value(value);
                return;
            }
            if (strcmp(key, "y") == 0) {
                args->y = inf_lab_parse_int_value(value);
                return;
            }
            break;

        case INF_LAB_COMMAND_SPAWN_NPC:
            if (strcmp(key, "slot") == 0) {
                args->slot = inf_lab_parse_int_value(value);
                return;
            }
            if (strcmp(key, "type") == 0) {
                args->type = inf_lab_parse_npc_type(value);
                return;
            }
            if (strcmp(key, "x") == 0) {
                args->x = inf_lab_parse_int_value(value);
                return;
            }
            if (strcmp(key, "y") == 0) {
                args->y = inf_lab_parse_int_value(value);
                return;
            }
            if (strcmp(key, "hp") == 0) {
                args->hp = inf_lab_parse_optional_hp_value(value);
                return;
            }
            if (strcmp(key, "timer") == 0) {
                args->timer = inf_lab_optional_int_set(
                    inf_lab_parse_int_value(value));
                return;
            }
            break;

        case INF_LAB_COMMAND_MOVE_NPC:
            if (strcmp(key, "slot") == 0) {
                args->slot = inf_lab_parse_int_value(value);
                return;
            }
            if (strcmp(key, "x") == 0) {
                args->x = inf_lab_parse_int_value(value);
                return;
            }
            if (strcmp(key, "y") == 0) {
                args->y = inf_lab_parse_int_value(value);
                return;
            }
            break;

        case INF_LAB_COMMAND_DELETE_NPC:
        case INF_LAB_COMMAND_KILL_NPC:
            if (strcmp(key, "slot") == 0) {
                args->slot = inf_lab_parse_int_value(value);
                return;
            }
            break;

        case INF_LAB_COMMAND_SET_NPC_HP:
            if (strcmp(key, "slot") == 0) {
                args->slot = inf_lab_parse_int_value(value);
                return;
            }
            if (strcmp(key, "hp") == 0) {
                args->hp = inf_lab_optional_int_set(
                    inf_lab_parse_int_value(value));
                return;
            }
            break;

        case INF_LAB_COMMAND_SET_NPC_TIMER:
            if (strcmp(key, "slot") == 0) {
                args->slot = inf_lab_parse_int_value(value);
                return;
            }
            if (strcmp(key, "timer") == 0) {
                args->timer = inf_lab_optional_int_set(
                    inf_lab_parse_int_value(value));
                return;
            }
            break;

        case INF_LAB_COMMAND_SET_PILLAR:
            if (strcmp(key, "idx") == 0 || strcmp(key, "pillar_idx") == 0) {
                args->pillar_idx = inf_lab_parse_int_value(value);
                return;
            }
            if (strcmp(key, "active") == 0) {
                int active = inf_lab_parse_int_value(value);
                if (active != 0 && active != 1)
                    inf_lab_abort("pillar active must be 0 or 1, got %d", active);
                args->pillar_state = active
                    ? INF_LAB_PILLAR_ACTIVE
                    : INF_LAB_PILLAR_REMOVED;
                return;
            }
            if (strcmp(key, "hp") == 0) {
                args->hp = inf_lab_parse_optional_hp_value(value);
                return;
            }
            break;

        case INF_LAB_COMMAND_SPAWN_WAVE:
            if (strcmp(key, "wave") == 0 || strcmp(key, "public") == 0) {
                args->wave = inf_lab_parse_int_value(value);
                return;
            }
            break;

        case INF_LAB_COMMAND_STEP_TICKS:
            if (strcmp(key, "ticks") == 0) {
                args->ticks = inf_lab_parse_int_value(value);
                return;
            }
            break;

        case INF_LAB_COMMAND_NONE:
        case INF_LAB_COMMAND_CLEAR_NPCS:
            break;
    }

    inf_lab_abort("unknown key %s for command %s", key, command);
}

static InfernoLabCommand inf_lab_build_script_command(
    InfLabCommandKind kind, const InfLabParsedArgs* args, const char* command
) {
    switch (kind) {
        case INF_LAB_COMMAND_NONE:
            return (InfernoLabCommand){ .kind = INF_LAB_COMMAND_NONE };

        case INF_LAB_COMMAND_RESET:
            return (InfernoLabCommand){
                .kind = INF_LAB_COMMAND_RESET,
                .as.reset = { .seed = args->seed },
            };

        case INF_LAB_COMMAND_SET_PLAYER:
            if (args->x == INT32_MIN || args->y == INT32_MIN)
                inf_lab_abort("command %s requires x and y", command);
            return (InfernoLabCommand){
                .kind = INF_LAB_COMMAND_SET_PLAYER,
                .as.tile = { .x = args->x, .y = args->y },
            };

        case INF_LAB_COMMAND_SPAWN_NPC:
            if (args->x == INT32_MIN || args->y == INT32_MIN)
                inf_lab_abort("command %s requires x and y", command);
            if (args->slot < 0 || args->type < 0)
                inf_lab_abort("npc command requires slot and type");
            return (InfernoLabCommand){
                .kind = INF_LAB_COMMAND_SPAWN_NPC,
                .as.spawn_npc = {
                    .slot = args->slot,
                    .type = (InfNPCType)args->type,
                    .x = args->x,
                    .y = args->y,
                    .hp = args->hp,
                    .timer = args->timer,
                },
            };

        case INF_LAB_COMMAND_MOVE_NPC:
            if (args->x == INT32_MIN || args->y == INT32_MIN)
                inf_lab_abort("command %s requires x and y", command);
            if (args->slot < 0)
                inf_lab_abort("command %s requires slot", command);
            return (InfernoLabCommand){
                .kind = INF_LAB_COMMAND_MOVE_NPC,
                .as.move_npc = {
                    .slot = args->slot,
                    .x = args->x,
                    .y = args->y,
                },
            };

        case INF_LAB_COMMAND_DELETE_NPC:
        case INF_LAB_COMMAND_KILL_NPC:
            if (args->slot < 0)
                inf_lab_abort("command %s requires slot", command);
            return (InfernoLabCommand){
                .kind = kind,
                .as.npc_slot = { .slot = args->slot },
            };

        case INF_LAB_COMMAND_SET_NPC_HP:
            if (args->slot < 0)
                inf_lab_abort("command %s requires slot", command);
            if (args->hp.kind != INF_LAB_OPTIONAL_INT_SET)
                inf_lab_abort("set_npc_hp requires hp");
            return (InfernoLabCommand){
                .kind = INF_LAB_COMMAND_SET_NPC_HP,
                .as.npc_hp = {
                    .slot = args->slot,
                    .hp = args->hp.value,
                },
            };

        case INF_LAB_COMMAND_SET_NPC_TIMER:
            if (args->slot < 0)
                inf_lab_abort("command %s requires slot", command);
            if (args->timer.kind != INF_LAB_OPTIONAL_INT_SET)
                inf_lab_abort("set_npc_timer requires timer");
            return (InfernoLabCommand){
                .kind = INF_LAB_COMMAND_SET_NPC_TIMER,
                .as.npc_timer = {
                    .slot = args->slot,
                    .timer = args->timer.value,
                },
            };

        case INF_LAB_COMMAND_SET_PILLAR:
            if (args->pillar_idx < 0)
                inf_lab_abort("set_pillar requires idx");
            return (InfernoLabCommand){
                .kind = INF_LAB_COMMAND_SET_PILLAR,
                .as.pillar = {
                    .pillar_idx = args->pillar_idx,
                    .state = args->pillar_state,
                    .hp = args->hp,
                },
            };

        case INF_LAB_COMMAND_SPAWN_WAVE:
            if (args->wave < 0)
                inf_lab_abort("spawn_wave requires wave");
            return (InfernoLabCommand){
                .kind = INF_LAB_COMMAND_SPAWN_WAVE,
                .as.wave = { .wave = args->wave },
            };

        case INF_LAB_COMMAND_CLEAR_NPCS:
            return (InfernoLabCommand){ .kind = INF_LAB_COMMAND_CLEAR_NPCS };

        case INF_LAB_COMMAND_STEP_TICKS:
            return (InfernoLabCommand){
                .kind = INF_LAB_COMMAND_STEP_TICKS,
                .as.step_ticks = { .ticks = args->ticks },
            };
    }

    inf_lab_abort("unknown command kind %d", kind);
}

static void inf_lab_string_init(InfLabString* out) {
    out->len = 0;
    out->cap = 4096;
    out->data = (char*)malloc(out->cap);
    if (!out->data) inf_lab_abort("out of memory");
    out->data[0] = '\0';
}

static void inf_lab_string_reserve(InfLabString* out, size_t need) {
    if (need <= out->cap) return;
    size_t next = out->cap;
    while (next < need) {
        if (next > SIZE_MAX / 2) inf_lab_abort("json output too large");
        next *= 2;
    }
    char* data = (char*)realloc(out->data, next);
    if (!data) inf_lab_abort("out of memory");
    out->data = data;
    out->cap = next;
}

static void inf_lab_string_append(InfLabString* out, const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    va_list copy;
    va_copy(copy, args);
    int needed = vsnprintf(NULL, 0, fmt, copy);
    va_end(copy);
    if (needed < 0) inf_lab_abort("json formatting failed");
    inf_lab_string_reserve(out, out->len + (size_t)needed + 1);
    int written = vsnprintf(out->data + out->len, out->cap - out->len, fmt, args);
    va_end(args);
    if (written != needed) inf_lab_abort("json formatting length mismatch");
    out->len += (size_t)written;
}

static void inf_lab_append_forecast_tick_json(
    InfLabString* out, const InfStepOutForecastTick* tick, int tick_idx
) {
    int style_mask = inf_step_out_forecast_tick_style_mask(tick);
    inf_lab_string_append(out,
        "{\"t\":%d,\"style_mask\":%d,\"melee\":%d,\"ranged\":%d,"
        "\"magic\":%d,\"blob_scan\":%d,\"ranger\":%d,\"mager\":%d,"
        "\"max_hit\":%d}",
        tick_idx + 1, style_mask, tick->melee_count, tick->ranged_count,
        tick->magic_count, tick->blob_scan_count, tick->ranger_count,
        tick->mager_count, tick->max_hit);
}

static int inf_lab_forecast_action_has_ranger_mager_same_tick(
    const InfStepOutForecastAction* action
) {
    for (int tick_idx = 0; tick_idx < INF_STEP_OUT_FORECAST_HORIZON; tick_idx++) {
        if (action->ticks[tick_idx].ranger_count > 0 &&
                action->ticks[tick_idx].mager_count > 0) {
            return 1;
        }
    }
    return 0;
}

static void inf_lab_append_forecast_json(InfernoState* s, InfLabString* out) {
    InfStepOutForecast forecast;
    inf_build_step_out_forecast(s, &forecast);
    inf_lab_string_append(out, "\"forecast\":{\"horizon\":%d,\"actions\":[",
        INF_STEP_OUT_FORECAST_HORIZON);
    for (int action_idx = 0; action_idx < ENCOUNTER_MOVE_ACTIONS; action_idx++) {
        const InfStepOutForecastAction* action = &forecast.actions[action_idx];
        if (action_idx > 0) inf_lab_string_append(out, ",");
        inf_lab_string_append(out,
            "{\"action\":%d,\"dx\":%d,\"dy\":%d,\"valid\":%d,"
            "\"land_x\":%d,\"land_y\":%d,\"same_tick_mixed\":%d,"
            "\"ranger_mager_same_tick\":%d,\"ranger_mager_offtick\":%d,"
            "\"melee_fallback\":%d,\"ticks\":[",
            action_idx,
            ENCOUNTER_MOVE_TARGET_DX[action_idx],
            ENCOUNTER_MOVE_TARGET_DY[action_idx],
            action->valid, action->land_x, action->land_y,
            action->same_tick_mixed_style_conflict,
            inf_lab_forecast_action_has_ranger_mager_same_tick(action),
            action->ranger_mager_offtick_opportunity,
            action->melee_fallback_exposure);
        for (int tick_idx = 0; tick_idx < INF_STEP_OUT_FORECAST_HORIZON; tick_idx++) {
            if (tick_idx > 0) inf_lab_string_append(out, ",");
            inf_lab_append_forecast_tick_json(out, &action->ticks[tick_idx], tick_idx);
        }
        inf_lab_string_append(out, "]}");
    }
    inf_lab_string_append(out, "]}");
}

static void inf_lab_append_npcs_json(InfernoState* s, InfLabString* out) {
    inf_lab_string_append(out, "\"npcs\":[");
    int emitted = 0;
    for (int i = 0; i < INF_MAX_NPCS; i++) {
        InfNPC* npc = &s->npcs[i];
        if (!npc->active) continue;
        if (emitted > 0) inf_lab_string_append(out, ",");
        int los = npc->death_ticks > 0 ? 0 : inf_npc_has_los(s, i);
        inf_lab_string_append(out,
            "{\"slot\":%d,\"type\":\"%s\",\"type_id\":%d,\"def_id\":%d,"
            "\"x\":%d,\"y\":%d,\"size\":%d,\"hp\":%d,\"max_hp\":%d,"
            "\"timer\":%d,\"stun\":%d,\"frozen\":%d,\"death_ticks\":%d,"
            "\"los_to_player\":%d}",
            i, inf_lab_npc_type_name(npc->type), npc->type,
            INF_NPC_DEF_IDS[npc->type], npc->x, npc->y,
            inf_npc_effective_size(npc), npc->hp, npc->max_hp,
            npc->attack_timer, npc->stun_timer, npc->frozen_ticks,
            npc->death_ticks, los);
        emitted++;
    }
    inf_lab_string_append(out, "]");
}

static void inf_lab_append_pillars_json(const InfernoState* s, InfLabString* out) {
    inf_lab_string_append(out, "\"pillars\":[");
    for (int p = 0; p < INF_NUM_PILLARS; p++) {
        if (p > 0) inf_lab_string_append(out, ",");
        inf_lab_string_append(out,
            "{\"idx\":%d,\"x\":%d,\"y\":%d,\"size\":%d,"
            "\"active\":%d,\"hp\":%d}",
            p, s->pillars[p].x, s->pillars[p].y, INF_PILLAR_SIZE,
            s->pillars[p].active, s->pillars[p].hp);
    }
    inf_lab_string_append(out, "]");
}

static char* inf_lab_alloc_json(InfernoState* s) {
    InfLabString out;
    inf_lab_string_init(&out);
    inf_invalidate_los_cache(s);
    inf_lab_string_append(&out,
        "{\"tick\":%d,\"wave\":%d,"
        "\"player\":{\"x\":%d,\"y\":%d,\"hp\":%d,\"prayer\":%d},",
        s->tick, s->wave + 1,
        s->player.x, s->player.y,
        s->player.current_hitpoints, s->player.current_prayer);
    inf_lab_append_pillars_json(s, &out);
    inf_lab_string_append(&out, ",");
    inf_lab_append_npcs_json(s, &out);
    inf_lab_string_append(&out, ",");
    inf_lab_append_forecast_json(s, &out);
    inf_lab_string_append(&out, "}");
    return out.data;
}

static char* inf_lab_next_token(char** cursor) {
    if (!cursor || !*cursor) inf_lab_abort("null token cursor");
    char* start = *cursor + strspn(*cursor, " \t\r\n");
    if (*start == '\0') {
        *cursor = start;
        return NULL;
    }
    char* end = start + strcspn(start, " \t\r\n");
    if (*end != '\0') {
        *end = '\0';
        *cursor = end + 1;
    } else {
        *cursor = end;
    }
    return start;
}

static InfLabLineResult inf_lab_apply_script_line_impl(
    InfernoState* s, const char* line, char** out_json
) {
    if (!line) inf_lab_abort("null script line");
    size_t len = strlen(line);
    char* copy = (char*)malloc(len + 1);
    if (!copy) inf_lab_abort("out of memory");
    memcpy(copy, line, len + 1);

    char* text = inf_lab_trim(copy);
    if (*text == '\0' || *text == '#') {
        free(copy);
        return INF_LAB_LINE_NONE;
    }

    char* cursor = text;
    char* command = inf_lab_next_token(&cursor);
    if (!command) {
        free(copy);
        return INF_LAB_LINE_NONE;
    }

    if (strcmp(command, "forecast") == 0) {
        InfStepOutForecast forecast;
        inf_build_step_out_forecast(s, &forecast);
        (void)forecast;
        free(copy);
        return INF_LAB_LINE_FORECAST;
    }
    if (strcmp(command, "dump") == 0 || strcmp(command, "dump_json") == 0) {
        if (out_json) *out_json = inf_lab_alloc_json(s);
        free(copy);
        return INF_LAB_LINE_DUMP;
    }

    InfLabCommandKind kind = INF_LAB_COMMAND_NONE;
    InfLabParsedArgs args = inf_lab_parsed_args_default();

    if (strcmp(command, "reset") == 0) {
        kind = INF_LAB_COMMAND_RESET;
    } else if (strcmp(command, "player") == 0 ||
            strcmp(command, "set_player") == 0) {
        kind = INF_LAB_COMMAND_SET_PLAYER;
    } else if (strcmp(command, "npc") == 0 ||
            strcmp(command, "spawn_npc") == 0) {
        kind = INF_LAB_COMMAND_SPAWN_NPC;
    } else if (strcmp(command, "move_npc") == 0) {
        kind = INF_LAB_COMMAND_MOVE_NPC;
    } else if (strcmp(command, "delete_npc") == 0) {
        kind = INF_LAB_COMMAND_DELETE_NPC;
    } else if (strcmp(command, "kill_npc") == 0) {
        kind = INF_LAB_COMMAND_KILL_NPC;
    } else if (strcmp(command, "set_npc_hp") == 0) {
        kind = INF_LAB_COMMAND_SET_NPC_HP;
    } else if (strcmp(command, "set_npc_timer") == 0) {
        kind = INF_LAB_COMMAND_SET_NPC_TIMER;
    } else if (strcmp(command, "pillar") == 0 ||
            strcmp(command, "set_pillar") == 0) {
        kind = INF_LAB_COMMAND_SET_PILLAR;
    } else if (strcmp(command, "wave") == 0 ||
            strcmp(command, "spawn_wave") == 0) {
        kind = INF_LAB_COMMAND_SPAWN_WAVE;
    } else if (strcmp(command, "clear_npcs") == 0) {
        kind = INF_LAB_COMMAND_CLEAR_NPCS;
    } else if (strcmp(command, "step") == 0 ||
            strcmp(command, "step_ticks") == 0) {
        kind = INF_LAB_COMMAND_STEP_TICKS;
    } else {
        inf_lab_abort("unknown script command %s", command);
    }

    for (char* token = inf_lab_next_token(&cursor);
            token != NULL;
            token = inf_lab_next_token(&cursor)) {
        const char* key = NULL;
        const char* value = NULL;
        inf_lab_parse_key_value(token, &key, &value);
        inf_lab_apply_script_arg(&args, kind, command, key, value);
    }

    InfernoLabCommand cmd = inf_lab_build_script_command(kind, &args, command);
    inf_lab_apply_command(s, &cmd);
    free(copy);
    return INF_LAB_LINE_NONE;
}

static InfLabLineResult inf_lab_apply_script_line(
    InfernoState* s, const char* line
) {
    return inf_lab_apply_script_line_impl(s, line, NULL);
}

static InfLabLineResult inf_lab_apply_script_line_alloc_json(
    InfernoState* s, const char* line, char** out_json
) {
    if (!out_json) inf_lab_abort("json output pointer is required");
    *out_json = NULL;
    return inf_lab_apply_script_line_impl(s, line, out_json);
}

typedef enum {
    INF_TARGET_CATEGORY_NONE = 0,
    INF_TARGET_CATEGORY_PLAYER,
    INF_TARGET_CATEGORY_ZUK,
    INF_TARGET_CATEGORY_SHIELD,
    INF_TARGET_CATEGORY_OTHER_NPC,
} InfTargetCategory;

static InfTargetCategory inf_npc_target_category(
    const InfernoState* s,
    const InfNPC* npc
) {
    if (!npc || !npc->active) return INF_TARGET_CATEGORY_NONE;
    if (npc->aggro_target < 0) return INF_TARGET_CATEGORY_PLAYER;
    if (npc->aggro_target >= INF_MAX_NPCS) return INF_TARGET_CATEGORY_NONE;

    const InfNPC* target = &s->npcs[npc->aggro_target];
    if (!target->active) return INF_TARGET_CATEGORY_NONE;
    if (target->type == INF_NPC_ZUK) return INF_TARGET_CATEGORY_ZUK;
    if (target->type == INF_NPC_ZUK_SHIELD) return INF_TARGET_CATEGORY_SHIELD;
    return INF_TARGET_CATEGORY_OTHER_NPC;
}

static int inf_wave_phase_index(int wave_idx) {
    int wave = wave_idx + 1;
    if (wave <= 17) return 0;
    if (wave <= 34) return 1;
    if (wave <= 49) return 2;
    if (wave <= 66) return 3;
    if (wave <= 68) return 4;
    return 5;
}

static int inf_npc_is_phantom_barrage_targetable_now(
    InfernoState* s,
    int npc_idx
) {
    if (s->player.attack_timer != 0) return 0;
    if (npc_idx < 0 || npc_idx >= INF_MAX_NPCS) return 0;
    return s->npcs[npc_idx].death_ticks == 1 &&
        inf_player_can_phantom_barrage_npc(s, npc_idx);
}

typedef struct {
    int active;
    int src_x;
    int src_y;
    int earliest_ticks_remaining;
    int total_damage;
} InfSparkObsBucket;

static int inf_spark_bucket_obs_less(
    const InfernoState* s,
    const InfSparkObsBucket* a,
    const InfSparkObsBucket* b
) {
    if (a->earliest_ticks_remaining != b->earliest_ticks_remaining)
        return a->earliest_ticks_remaining < b->earliest_ticks_remaining;
    int adx = abs(a->src_x - s->player.x);
    int ady = abs(a->src_y - s->player.y);
    int bdx = abs(b->src_x - s->player.x);
    int bdy = abs(b->src_y - s->player.y);
    int ad = adx > ady ? adx : ady;
    int bd = bdx > bdy ? bdx : bdy;
    if (ad != bd) return ad < bd;
    if (a->src_x != b->src_x) return a->src_x < b->src_x;
    return a->src_y < b->src_y;
}

static int inf_build_spark_obs_buckets(
    const InfernoState* s,
    InfSparkObsBucket* buckets,
    int capacity
) {
    int count = 0;
    for (int i = 0; i < INF_MAX_PENDING_SPARKS; i++) {
        const InfPendingSpark* spark = &s->pending_sparks[i];
        if (!spark->active) continue;

        int bucket_idx = -1;
        for (int b = 0; b < count; b++) {
            if (buckets[b].src_x == spark->src_x &&
                    buckets[b].src_y == spark->src_y) {
                bucket_idx = b;
                break;
            }
        }

        if (bucket_idx < 0) {
            if (count >= capacity) {
                fprintf(stderr, "BUG: spark obs bucket overflow\n");
                abort();
            }
            bucket_idx = count++;
            buckets[bucket_idx] = (InfSparkObsBucket){
                .active = 1,
                .src_x = spark->src_x,
                .src_y = spark->src_y,
                .earliest_ticks_remaining = spark->ticks_remaining,
                .total_damage = 0,
            };
        }

        if (spark->ticks_remaining < buckets[bucket_idx].earliest_ticks_remaining)
            buckets[bucket_idx].earliest_ticks_remaining = spark->ticks_remaining;
        buckets[bucket_idx].total_damage += spark->damage;
    }

    for (int i = 0; i < count; i++) {
        for (int j = i + 1; j < count; j++) {
            if (inf_spark_bucket_obs_less(s, &buckets[j], &buckets[i])) {
                InfSparkObsBucket tmp = buckets[i];
                buckets[i] = buckets[j];
                buckets[j] = tmp;
            }
        }
    }

    return count;
}

static void inf_refresh_current_obs_slots(InfernoState* s) {
    int obs_slots[INF_OBS_NPCS];
    for (int j = 0; j < INF_OBS_NPCS; j++) obs_slots[j] = -1;

    int slot_counts[INF_NUM_NPC_TYPES] = {0};
    int slot_offsets[INF_NUM_NPC_TYPES];
    int slot_max[INF_NUM_NPC_TYPES];

    slot_offsets[INF_NPC_MAGER] = 0; slot_max[INF_NPC_MAGER] = 2;
    slot_offsets[INF_NPC_RANGER] = 2; slot_max[INF_NPC_RANGER] = 2;
    slot_offsets[INF_NPC_MELEER] = 4; slot_max[INF_NPC_MELEER] = 2;
    slot_offsets[INF_NPC_BLOB] = 6; slot_max[INF_NPC_BLOB] = 2;
    slot_offsets[INF_NPC_BAT] = 8; slot_max[INF_NPC_BAT] = 2;
    slot_offsets[INF_NPC_BLOB_MAGE] = 10; slot_max[INF_NPC_BLOB_MAGE] = 2;
    slot_offsets[INF_NPC_BLOB_RANGE] = 12; slot_max[INF_NPC_BLOB_RANGE] = 2;
    slot_offsets[INF_NPC_BLOB_MELEE] = 14; slot_max[INF_NPC_BLOB_MELEE] = 2;
    slot_offsets[INF_NPC_NIBBLER] = 16; slot_max[INF_NPC_NIBBLER] = 6;
    slot_offsets[INF_NPC_JAD] = 22; slot_max[INF_NPC_JAD] = 3;
    slot_offsets[INF_NPC_ZUK] = 25; slot_max[INF_NPC_ZUK] = 1;
    slot_offsets[INF_NPC_ZUK_SHIELD] = 26; slot_max[INF_NPC_ZUK_SHIELD] = 1;
    slot_offsets[INF_NPC_HEALER_JAD] = 27; slot_max[INF_NPC_HEALER_JAD] = 6;
    slot_offsets[INF_NPC_HEALER_ZUK] = 33; slot_max[INF_NPC_HEALER_ZUK] = 4;

    for (int n = 0; n < INF_MAX_NPCS; n++) {
        InfNPC* npc = &s->npcs[n];
        if (npc->active && npc->death_ticks == 0) {
            int t = npc->type;
            if (slot_counts[t] < slot_max[t]) {
                obs_slots[slot_offsets[t] + slot_counts[t]] = n;
                slot_counts[t]++;
            }
        }
    }

    for (int n = 0; n < INF_MAX_NPCS; n++) {
        InfNPC* npc = &s->npcs[n];
        if (npc->active && npc->death_ticks > 0 &&
                inf_npc_is_phantom_barrage_obs_candidate(s, n)) {
            int t = npc->type;
            if (slot_counts[t] < slot_max[t]) {
                obs_slots[slot_offsets[t] + slot_counts[t]] = n;
                slot_counts[t]++;
            }
        }
    }

    for (int j = 0; j < INF_OBS_NPCS; j++) {
        s->current_obs_slots[j] = obs_slots[j];
    }
}

static void inf_write_obs(EncounterState* state, float* obs) {
    InfernoState* s = (InfernoState*)state;
#ifdef INF_PROFILE_ENABLED
    int inf_prof_enabled = INF_PROFILE_ENABLED();
    double inf_prof_t0 = inf_prof_enabled ? INF_PROFILE_NOW_MS() : 0.0;
#endif
    memset(obs, 0, INF_NUM_OBS * sizeof(float));
    int i = 0;
    int px = s->player.x, py = s->player.y;
    const EncounterLoadoutStats* ls = inf_current_loadout_stats(s);
    InfSupplyDoses full_supplies = inf_full_starting_supplies();

    /* player state (26 features) */
    obs[i++] = (float)s->player.current_hitpoints / 99.0f;
    obs[i++] = (float)(px - INF_ARENA_MIN_X) / (float)INF_ARENA_WIDTH;   /* dist to west wall */
    obs[i++] = (float)(INF_ARENA_MAX_X - px) / (float)INF_ARENA_WIDTH;  /* dist to east wall */
    obs[i++] = (float)(py - INF_ARENA_MIN_Y) / (float)INF_ARENA_HEIGHT; /* dist to south wall */
    obs[i++] = (float)(INF_ARENA_MAX_Y - py) / (float)INF_ARENA_HEIGHT; /* dist to north wall */
    obs[i++] = (s->player.prayer == PRAYER_PROTECT_MELEE) ? 1.0f : 0.0f;
    obs[i++] = (s->player.prayer == PRAYER_PROTECT_RANGED) ? 1.0f : 0.0f;
    obs[i++] = (s->player.prayer == PRAYER_PROTECT_MAGIC) ? 1.0f : 0.0f;
    /* offensive prayer one-hot (none implied by all-three-zero). */
    obs[i++] = (s->player.offensive_prayer == OFFENSIVE_PRAYER_PIETY) ? 1.0f : 0.0f;
    obs[i++] = (s->player.offensive_prayer == OFFENSIVE_PRAYER_RIGOUR) ? 1.0f : 0.0f;
    obs[i++] = (s->player.offensive_prayer == OFFENSIVE_PRAYER_AUGURY) ? 1.0f : 0.0f;
    obs[i++] = (float)s->player.brew_doses / (float)full_supplies.brew_doses;
    obs[i++] = (float)s->player.restore_doses / (float)full_supplies.restore_doses;
    obs[i++] = (float)s->player.current_prayer / 99.0f;
    obs[i++] = (float)s->wave / (float)INF_NUM_WAVES;
    {
        int phase = inf_wave_phase_index(s->wave);
        for (int p = 0; p < 6; p++)
            obs[i++] = (p == phase) ? 1.0f : 0.0f;
    }
    obs[i++] = inf_zuk_attack_timer_obs(s);
    obs[i++] = (s->weapon_set == INF_GEAR_MAGE) ? 1.0f : 0.0f;
    obs[i++] = (s->weapon_set == INF_GEAR_TBOW) ? 1.0f : 0.0f;
    obs[i++] = (s->weapon_set == INF_GEAR_BP) ? 1.0f : 0.0f;
    obs[i++] = (float)s->player.bastion_doses / (float)full_supplies.bastion_doses;
    obs[i++] = (float)s->player.stamina_doses / (float)full_supplies.stamina_doses;
    obs[i++] = (s->stamina_active_ticks > 0) ? 1.0f : 0.0f;
    obs[i++] = (float)s->player.potion_timer / 3.0f;
    obs[i++] = (float)s->player.attack_timer / 8.0f;
    /* combat stats, target, weapon range, dead mob pool */
    obs[i++] = (float)s->player.current_defence / 99.0f;
    obs[i++] = (float)s->player.current_ranged / 99.0f;
    obs[i++] = (float)s->player.current_magic / 99.0f;
    obs[i++] = osrs_interaction_active(&s->interaction) ? 1.0f : 0.0f;
    obs[i++] = (float)ls->attack_range / 15.0f;
    obs[i++] = (float)s->dead_mob_count / (float)INF_MAX_DEAD_MOBS;
    /* gear stats: current loadout combat performance */
    obs[i++] = (float)ls->max_hit / 80.0f;
    obs[i++] = (float)ls->attack_speed / 6.0f;
    obs[i++] = (float)ls->def_stab / 300.0f;
    obs[i++] = (float)ls->def_magic / 300.0f;
    obs[i++] = (float)ls->def_ranged / 300.0f;
    obs[i++] = (float)s->player.special_energy / 100.0f;

    /* prayer-critical: distilled from NPC array and pending hits */
    {
        int min_timer = 999;
        int min_style = 0;
        int has_melee_2 = 0, has_ranged_2 = 0, has_magic_2 = 0;

        /* 1. Pending hits (handles Jad, which checks prayer on impact) */
        for (int h = 0; h < s->player_pending_hit_count; h++) {
            EncounterPendingHit* ph = &s->player_pending_hits[h];
            if (ph->check_prayer) {
                int t = inf_pending_hit_obs_timer(ph);
                if (t < min_timer) {
                    min_timer = t;
                    min_style = ph->attack_style;
                }
                if (t <= 2) {
                    if (ph->attack_style == ATTACK_STYLE_MELEE) has_melee_2 = 1;
                    if (ph->attack_style == ATTACK_STYLE_RANGED) has_ranged_2 = 1;
                    if (ph->attack_style == ATTACK_STYLE_MAGIC) has_magic_2 = 1;
                }
            }
        }

        /* 2. NPCs firing or telegraphing (non-Jad checks prayer on launch;
           Jad enters here only once its committed preview style is visible). */
        for (int n = 0; n < INF_MAX_NPCS; n++) {
            InfNPC* npc = &s->npcs[n];
            if (!npc->active || npc->death_ticks > 0) continue;
            if (npc->type == INF_NPC_ZUK ||
                npc->type == INF_NPC_ZUK_SHIELD || npc->type == INF_NPC_NIBBLER ||
                npc->type == INF_NPC_HEALER_ZUK) continue;

            const InfNPCStats* st = &INF_NPC_STATS[npc->type];
            if (npc->frozen_ticks > 0 || npc->stun_timer > 0) continue;

            InfTargetArea target = inf_npc_current_target_area(s, npc);
            if (!target.is_player) continue;

            int dist = encounter_dist_to_npc(s->player.x, s->player.y, npc->x, npc->y, npc->size);
            if (dist == 0) continue;

            int style = npc->attack_style;
            if (npc->type == INF_NPC_BLOB && npc->blob_scanned_prayer >= 0) {
                OverheadPrayer scanned = (OverheadPrayer)npc->blob_scanned_prayer;
                if (scanned == PRAYER_PROTECT_MAGIC) style = ATTACK_STYLE_RANGED;
                else if (scanned == PRAYER_PROTECT_RANGED) style = ATTACK_STYLE_MAGIC;
            } else if (npc->type == INF_NPC_JAD) {
                style = npc->jad_attack_style;
                if (style == ATTACK_STYLE_NONE) continue;
            }
            int style_mask = inf_attack_style_telegraph_mask(
                s, npc, st, style, dist);
            int preview_style = inf_attack_style_obs_preview(style_mask);

            int t = npc->attack_timer;
            if (t == 0) t = 1;

            if (t < min_timer) {
                min_timer = t;
                min_style = preview_style;
            }
            if (t <= 2) {
                if (style_mask & INF_STYLE_MASK_MELEE) has_melee_2 = 1;
                if (style_mask & INF_STYLE_MASK_RANGED) has_ranged_2 = 1;
                if (style_mask & INF_STYLE_MASK_MAGIC) has_magic_2 = 1;
            }
        }

        int conflict_count = has_melee_2 + has_ranged_2 + has_magic_2;
        obs[i++] = (min_timer < 999) ? (float)min_timer / 10.0f : 1.0f;
        obs[i++] = (min_style == ATTACK_STYLE_MELEE) ? 1.0f : 0.0f;
        obs[i++] = (min_style == ATTACK_STYLE_RANGED) ? 1.0f : 0.0f;
        obs[i++] = (min_style == ATTACK_STYLE_MAGIC) ? 1.0f : 0.0f;
        obs[i++] = (float)conflict_count / 3.0f;
    }

    /* Zuk-phase features (10 features: 1 flag + 9 Zuk-specific) */
    {
        int is_zuk = (s->wave == 68);
        obs[i++] = is_zuk ? 1.0f : 0.0f;

        if (is_zuk) {
            int si = s->zuk.shield_idx;
            int shield_active = (si >= 0 && s->npcs[si].active);

            /* shield direction/freeze are only meaningful while the shield exists.
               once the shield dies, zero these instead of leaking stale state. */
            obs[i++] = shield_active
                ? ((s->zuk.shield_freeze > 0) ? 0.0f : (float)s->zuk.shield_dir)
                : 0.0f;
            obs[i++] = shield_active ? (float)s->zuk.shield_freeze / 5.0f : 0.0f;
            /* am I behind the shield right now? + signed distance to shield center.
               the binary tells the agent if it's safe. the signed distance gives a
               gradient: negative = move east, positive = move west, 0 = centered. */
            int behind = 0;
            float shield_offset = 0.0f;
            if (shield_active) {
                int sx = s->npcs[si].x;
                int sz = INF_NPC_STATS[INF_NPC_ZUK_SHIELD].size;
                int shield_center = sx + sz / 2;
                shield_offset = (float)(px - shield_center) / 15.0f;  /* normalized, ~[-1,1] range */
                behind = (px >= sx && px < sx + sz && py >= 41);
            }
            obs[i++] = behind ? 1.0f : 0.0f;
            obs[i++] = shield_offset;
            /* Zuk enraged (attack speed 7 instead of 8) */
            obs[i++] = s->zuk.enraged ? 1.0f : 0.0f;
            /* set spawn timer / 350 */
            obs[i++] = (float)s->zuk.set_timer / 350.0f;
            /* set timer paused (Jad spawn phase) */
            obs[i++] = s->zuk.timer_paused ? 1.0f : 0.0f;
            /* Jad has spawned during Zuk fight */
            obs[i++] = s->zuk.jad_spawned ? 1.0f : 0.0f;
            /* Zuk healers have spawned */
            obs[i++] = s->zuk.healer_spawned ? 1.0f : 0.0f;
        } else {
            for (int z = 0; z < 9; z++) obs[i++] = 0.0f;
        }
    }

    /* pillars (12 features: active, hp, relative dx, relative dy per pillar) */
    for (int p = 0; p < INF_NUM_PILLARS; p++) {
        obs[i++] = s->pillars[p].active ? 1.0f : 0.0f;
        obs[i++] = (float)s->pillars[p].hp / (float)INF_PILLAR_HP;
        obs[i++] = (float)(s->pillars[p].x - px) / (float)INF_ARENA_WIDTH;
        obs[i++] = (float)(s->pillars[p].y - py) / (float)INF_ARENA_HEIGHT;
    }

#ifdef INF_PROFILE_ENABLED
    INF_PROFILE_MARK(INF_PROF_OBS_PREFIX);
#endif
    inf_refresh_current_obs_slots(s);
#ifdef INF_PROFILE_ENABLED
    INF_PROFILE_MARK(INF_PROF_OBS_REFRESH_SLOTS);
#endif

    /* NPCs: variable features per slot, fixed order */
    int slot_types[INF_OBS_NPCS];
    int st_idx = 0;
    for (int j = 0; j < 2; j++) slot_types[st_idx++] = INF_NPC_MAGER;
    for (int j = 0; j < 2; j++) slot_types[st_idx++] = INF_NPC_RANGER;
    for (int j = 0; j < 2; j++) slot_types[st_idx++] = INF_NPC_MELEER;
    for (int j = 0; j < 2; j++) slot_types[st_idx++] = INF_NPC_BLOB;
    for (int j = 0; j < 2; j++) slot_types[st_idx++] = INF_NPC_BAT;
    for (int j = 0; j < 2; j++) slot_types[st_idx++] = INF_NPC_BLOB_MAGE;
    for (int j = 0; j < 2; j++) slot_types[st_idx++] = INF_NPC_BLOB_RANGE;
    for (int j = 0; j < 2; j++) slot_types[st_idx++] = INF_NPC_BLOB_MELEE;
    for (int j = 0; j < 6; j++) slot_types[st_idx++] = INF_NPC_NIBBLER;
    for (int j = 0; j < 3; j++) slot_types[st_idx++] = INF_NPC_JAD;
    for (int j = 0; j < 1; j++) slot_types[st_idx++] = INF_NPC_ZUK;
    for (int j = 0; j < 1; j++) slot_types[st_idx++] = INF_NPC_ZUK_SHIELD;
    for (int j = 0; j < 6; j++) slot_types[st_idx++] = INF_NPC_HEALER_JAD;
    for (int j = 0; j < 4; j++) slot_types[st_idx++] = INF_NPC_HEALER_ZUK;

    for (int k = 0; k < INF_OBS_NPCS; k++) {
        int n = s->current_obs_slots[k];
        int type = slot_types[k];

        int has_style = (type == INF_NPC_BLOB || type == INF_NPC_JAD);
        int has_scan = (type == INF_NPC_BLOB);
        int has_los = (type != INF_NPC_NIBBLER && type != INF_NPC_MELEER && type != INF_NPC_HEALER_JAD && type != INF_NPC_ZUK_SHIELD);
        int has_target_category = (type != INF_NPC_NIBBLER && type != INF_NPC_ZUK_SHIELD);
        int has_timer = (type != INF_NPC_NIBBLER && type != INF_NPC_HEALER_JAD && type != INF_NPC_ZUK_SHIELD);
        int has_targeted = 1;
        int has_meleer_dig = (type == INF_NPC_MELEER);

        int num_features = 4; // HP, RelX, RelY, AoE
        if (has_timer) num_features += 1;
        if (has_style) num_features += 3;
        if (has_los) num_features += 1;
        if (has_scan) num_features += 3;
        if (has_target_category) num_features += 4;
        if (has_targeted) num_features += 1;
        num_features += 1;
        if (has_meleer_dig) num_features += 3;

        if (n >= 0) {
            InfNPC* npc = &s->npcs[n];
            obs[i++] = (float)npc->hp / (float)npc->max_hp;
            obs[i++] = (float)(npc->x - px) / (float)INF_ARENA_WIDTH;
            obs[i++] = (float)(npc->y - py) / (float)INF_ARENA_HEIGHT;
            if (has_timer) obs[i++] = (float)npc->attack_timer / 10.0f;

            if (has_style) {
                int style = (npc->type == INF_NPC_JAD) ? npc->jad_attack_style : npc->attack_style;
                obs[i++] = (style == ATTACK_STYLE_MELEE) ? 1.0f : 0.0f;
                obs[i++] = (style == ATTACK_STYLE_RANGED) ? 1.0f : 0.0f;
                obs[i++] = (style == ATTACK_STYLE_MAGIC) ? 1.0f : 0.0f;
            }

            if (has_los) obs[i++] = inf_npc_has_los(s, n) ? 1.0f : 0.0f;

            if (has_scan) {
                if (npc->blob_scanned_prayer >= 0) {
                    OverheadPrayer scanned = (OverheadPrayer)npc->blob_scanned_prayer;
                    obs[i++] = (scanned == PRAYER_PROTECT_MAGIC) ? 1.0f : 0.0f;
                    obs[i++] = (scanned == PRAYER_PROTECT_RANGED) ? 1.0f : 0.0f;
                    obs[i++] = (scanned != PRAYER_PROTECT_MAGIC && scanned != PRAYER_PROTECT_RANGED) ? 1.0f : 0.0f;
                } else {
                    obs[i++] = 0.0f; obs[i++] = 0.0f; obs[i++] = 0.0f;
                }
            }

            /* barrage AoE count: unique blocking NPCs in the 3x3 area */
            {
                int aoe_count = 0;
                for (int oidx = 0; oidx < INF_MAX_NPCS; oidx++) {
                    if (oidx == n) continue;
                    InfNPC* other = &s->npcs[oidx];
                    if (!other->active) continue;
                    if (!inf_npc_sets_collision_flag(other->type)) continue;
                    if (encounter_entity_footprints_overlap(
                            other->x, other->y, inf_npc_effective_size(other),
                            npc->x - 1, npc->y - 1, 3)) {
                        aoe_count++;
                    }
                }
                obs[i++] = (float)aoe_count / 8.0f;
            }

            if (has_target_category) {
                InfTargetCategory category = inf_npc_target_category(s, npc);
                obs[i++] = (category == INF_TARGET_CATEGORY_PLAYER) ? 1.0f : 0.0f;
                obs[i++] = (category == INF_TARGET_CATEGORY_ZUK) ? 1.0f : 0.0f;
                obs[i++] = (category == INF_TARGET_CATEGORY_SHIELD) ? 1.0f : 0.0f;
                obs[i++] = (category == INF_TARGET_CATEGORY_OTHER_NPC) ? 1.0f : 0.0f;
            }
            if (has_targeted) obs[i++] = (osrs_interaction_active(&s->interaction) && s->interaction.target_slot == n) ? 1.0f : 0.0f;
            obs[i++] = inf_npc_is_phantom_barrage_targetable_now(s, n) ? 1.0f : 0.0f;
            if (has_meleer_dig) {
                float no_los = (float)npc->no_los_ticks / 50.0f;
                if (no_los > 1.0f) no_los = 1.0f;
                obs[i++] = no_los;
                obs[i++] = (float)npc->dig_freeze_timer / 6.0f;
                obs[i++] = (float)npc->dig_attack_delay / 6.0f;
            }
        } else {
            for (int j = 0; j < num_features; j++) obs[i++] = 0.0f;
        }
    }
#ifdef INF_PROFILE_ENABLED
    INF_PROFILE_MARK(INF_PROF_OBS_NPC_SLOTS);
#endif

    /* assert NPC section wrote exactly the right number of features.
       if this fires, INF_FEATURES_PER_NPC doesn't match the actual feature count. */
    {
        int expected_npc_end = INF_PLAYER_OBS_SIZE + 12 + INF_TOTAL_NPC_OBS_SIZE;
        if (i != expected_npc_end) {
            fprintf(stderr, "FATAL: obs misaligned after NPC section: i=%d expected=%d\n",
                    i, expected_npc_end);
            abort();
        }
    }

    {
        if (s->step_out_forecast_obs_enabled) {
            InfStepOutForecast forecast;
            inf_build_step_out_forecast(s, &forecast);
            for (int action_idx = 0; action_idx < ENCOUNTER_MOVE_ACTIONS; action_idx++) {
                const InfStepOutForecastAction* action = &forecast.actions[action_idx];
                int first_attack_tick = 0;
                int first_style_mask = 0;
                int max_hit = 0;
                int ranger_mager_same_tick = 0;
                for (int tick_idx = 0; tick_idx < INF_STEP_OUT_FORECAST_HORIZON; tick_idx++) {
                    const InfStepOutForecastTick* tick = &action->ticks[tick_idx];
                    int style_mask = inf_step_out_forecast_tick_style_mask(tick);
                    if (first_attack_tick == 0 &&
                            inf_step_out_forecast_tick_has_event(tick)) {
                        first_attack_tick = tick_idx + 1;
                        first_style_mask = style_mask;
                    }
                    if (tick->max_hit > max_hit) max_hit = tick->max_hit;
                    if (tick->ranger_count > 0 && tick->mager_count > 0)
                        ranger_mager_same_tick = 1;
                }
                obs[i++] = action->valid ? 1.0f : 0.0f;
                obs[i++] = (float)first_attack_tick / (float)INF_STEP_OUT_FORECAST_HORIZON;
                obs[i++] = (float)first_style_mask / 7.0f;
                obs[i++] = (float)max_hit / 150.0f;
                obs[i++] = action->same_tick_mixed_style_conflict ? 1.0f : 0.0f;
                obs[i++] = ranger_mager_same_tick ? 1.0f : 0.0f;
                obs[i++] = action->ranger_mager_offtick_opportunity ? 1.0f : 0.0f;
                obs[i++] = action->melee_fallback_exposure ? 1.0f : 0.0f;
            }
        } else {
            i += INF_STEP_OUT_FORECAST_OBS_SIZE;
        }
    }
#ifdef INF_PROFILE_ENABLED
    INF_PROFILE_MARK(INF_PROF_OBS_FORECAST);
#endif

    {
        int expected_forecast_end = INF_PLAYER_OBS_SIZE + 12 +
            INF_TOTAL_NPC_OBS_SIZE + INF_STEP_OUT_FORECAST_OBS_SIZE;
        if (i != expected_forecast_end) {
            fprintf(stderr, "FATAL: obs misaligned after step-out forecast: i=%d expected=%d\n",
                    i, expected_forecast_end);
            abort();
        }
    }

    /* pending hits on player (INF_FEATURES_PER_HIT * ENCOUNTER_MAX_PENDING_HITS) */
    for (int h = 0; h < ENCOUNTER_MAX_PENDING_HITS; h++) {
        if (h < s->player_pending_hit_count) {
            EncounterPendingHit* ph = &s->player_pending_hits[h];
            obs[i++] = 1.0f;  /* active */
            obs[i++] = (ph->attack_style == ATTACK_STYLE_RANGED) ? 1.0f : 0.0f;
            obs[i++] = (ph->attack_style == ATTACK_STYLE_MAGIC) ? 1.0f : 0.0f;
            obs[i++] = (float)inf_pending_hit_obs_timer(ph) / 10.0f;
            obs[i++] = (float)ph->damage / 150.0f;  /* normalized damage magnitude (Zuk max ~148) */
        } else {
            for (int j = 0; j < INF_FEATURES_PER_HIT; j++) obs[i++] = 0.0f;
        }
    }
#ifdef INF_PROFILE_ENABLED
    INF_PROFILE_MARK(INF_PROF_OBS_PENDING_HITS);
#endif

    {
        InfSparkObsBucket buckets[INF_MAX_PENDING_SPARKS];
        memset(buckets, 0, sizeof(buckets));
        int bucket_count = inf_build_spark_obs_buckets(s, buckets, INF_MAX_PENDING_SPARKS);
        for (int slot = 0; slot < INF_SPARK_OBS_SLOTS; slot++) {
            if (slot < bucket_count) {
                const InfSparkObsBucket* bucket = &buckets[slot];
                obs[i++] = 1.0f;
                obs[i++] = (float)(bucket->src_x - px) / (float)INF_ARENA_WIDTH;
                obs[i++] = (float)(bucket->src_y - py) / (float)INF_ARENA_HEIGHT;
                obs[i++] = (float)bucket->earliest_ticks_remaining / 10.0f;
                obs[i++] = (float)bucket->total_damage / 10.0f;
            } else {
                for (int j = 0; j < INF_FEATURES_PER_SPARK; j++) obs[i++] = 0.0f;
            }
        }
    }
#ifdef INF_PROFILE_ENABLED
    INF_PROFILE_MARK(INF_PROF_OBS_SPARKS);
#endif

    if (i != INF_NUM_OBS) {
        fprintf(stderr, "BUG: inf_write_obs wrote %d features, expected %d\n", i, INF_NUM_OBS);
        abort();
    }
}

static int inf_find_target_obs_slot(const InfernoState* s, int npc_slot) {
    if (npc_slot < 0 || npc_slot >= INF_MAX_NPCS) return -1;
    for (int j = 0; j < INF_OBS_NPCS; j++) {
        if (s->current_obs_slots[j] == npc_slot) return j;
    }
    return -1;
}

static int inf_obs_slot_is_targetable(InfernoState* s, int obs_slot) {
    if (obs_slot < 0 || obs_slot >= INF_OBS_NPCS) return 0;
    int npc_slot = s->current_obs_slots[obs_slot];
    if (npc_slot < 0 || npc_slot >= INF_MAX_NPCS) return 0;
    if (s->npcs[npc_slot].active &&
            s->npcs[npc_slot].death_ticks == 0 &&
            s->npcs[npc_slot].type != INF_NPC_ZUK_SHIELD)
        return 1;
    return s->player.attack_timer <= 1 &&
        inf_npc_is_phantom_barrage_obs_candidate(s, npc_slot);
}

static int inf_is_human_targetable_npc_slot(EncounterState* state, int npc_slot) {
    InfernoState* s = (InfernoState*)state;
    inf_refresh_current_obs_slots(s);
    return inf_obs_slot_is_targetable(s, inf_find_target_obs_slot(s, npc_slot));
}

static void inf_write_mask(EncounterState* state, float* mask) {
    InfernoState* s = (InfernoState*)state;
    inf_refresh_current_obs_slots(s);
    int offset = 0;
    if (s->total_action_mask_checks == 0) {
        for (int h = 0; h < 9; h++) {
            if (s->min_valid_action_count_by_head[h] == 0)
                s->min_valid_action_count_by_head[h] = 1000000;
        }
    }

    /* HEAD_MOVE (25): idle always valid, walk/run valid if target tile reachable */
    mask[offset++] = 1.0f;  /* idle always valid */
    for (int d = 1; d < ENCOUNTER_MOVE_ACTIONS; d++) {
        int nx = s->player.x + ENCOUNTER_MOVE_TARGET_DX[d];
        int ny = s->player.y + ENCOUNTER_MOVE_TARGET_DY[d];
        mask[offset++] = (inf_in_arena(nx, ny) && !inf_blocked_by_pillar(s, nx, ny, 1))
                         ? 1.0f : 0.0f;
    }

    /* HEAD_PRAYER: no_change, off, set_refresh_melee/ranged/magic. */
    mask[offset++] = 1.0f;  /* no_change always valid */
    mask[offset++] = s->player.prayer != PRAYER_NONE ? 1.0f : 0.0f;
    mask[offset++] = s->player.current_prayer > 0 ? 1.0f : 0.0f;
    mask[offset++] = s->player.current_prayer > 0 ? 1.0f : 0.0f;
    mask[offset++] = s->player.current_prayer > 0 ? 1.0f : 0.0f;

    int has_forced_safe_healer_target =
        s->zuk_force_safe_untagged_healer_target_mask &&
        inf_has_safe_untagged_zuk_healer_target_now(s);

    /* HEAD_TARGET (INF_OBS_NPCS+1): slot actions require a valid attack target. */
    mask[offset++] = has_forced_safe_healer_target ? 0.0f : 1.0f;
    for (int n = 0; n < INF_OBS_NPCS; n++) {
        int npc_idx = s->current_obs_slots[n];
        int is_targetable = inf_obs_slot_is_targetable(s, n);
        if (is_targetable &&
                (s->zuk_safe_untagged_healer_target_mask ||
                    s->zuk_force_safe_untagged_healer_target_mask) &&
                !inf_untagged_zuk_healer_target_is_safe_now(s, npc_idx)) {
            is_targetable = 0;
        }
        if (is_targetable && has_forced_safe_healer_target &&
                !inf_is_safe_untagged_zuk_healer_target_now(s, npc_idx)) {
            is_targetable = 0;
        }
        mask[offset++] = is_targetable ? 1.0f : 0.0f;
    }

    /* HEAD_GEAR (4): no_switch, mage, tbow, bp */
    mask[offset++] = 1.0f;  /* no_switch always valid */
    mask[offset++] = (s->weapon_set != INF_GEAR_MAGE) ? 1.0f : 0.0f;
    mask[offset++] = (s->weapon_set != INF_GEAR_TBOW) ? 1.0f : 0.0f;
    mask[offset++] = (s->weapon_set != INF_GEAR_BP) ? 1.0f : 0.0f;

    /* HEAD_EAT (2): none, brew */
    mask[offset++] = 1.0f;  /* none always valid */
    mask[offset++] = (s->player.brew_doses > 0 &&
                      s->player.potion_timer == 0 &&
                      s->player.current_hitpoints < s->player.base_hitpoints)
                     ? 1.0f : 0.0f;

    /* HEAD_POTION (4): none, restore, bastion, stamina */
    mask[offset++] = 1.0f;  /* none always valid */
    /* restore: unmask if any stat is drained or prayer is low enough to not waste.
       "stats drained" = any combat stat below base 99. */
    {
        int pray_missing = s->player.base_prayer - s->player.current_prayer;
        int stats_drained = s->player.current_attack < 99 || s->player.current_strength < 99 ||
                            s->player.current_defence < 99 || s->player.current_ranged < 99 ||
                            s->player.current_magic < 99;
        int pray_worth = pray_missing >= (INF_RESTORE_AMOUNT + 1) / 2;
        mask[offset++] = (s->player.restore_doses > 0 &&
                          s->player.potion_timer == 0 &&
                          (stats_drained || pray_worth))
                         ? 1.0f : 0.0f;
    }
    /* bastion: only worth drinking at 99-105 ranged (drained = restore first, >105 = still boosted) */
    mask[offset++] = (s->player.bastion_doses > 0 && s->player.potion_timer == 0 &&
                      s->player.current_ranged >= 99 && s->player.current_ranged <= 105)
                     ? 1.0f : 0.0f;
    /* stamina: mask if no doses, timer active, or already active */
    mask[offset++] = (s->player.stamina_doses > 0 &&
                      s->player.potion_timer == 0 &&
                      s->stamina_active_ticks == 0)
                     ? 1.0f : 0.0f;

    mask[offset++] = 1.0f;
    mask[offset++] = s->player.current_magic >= BLOOD_BARRAGE_LEVEL ? 1.0f : 0.0f;
    mask[offset++] = s->player.current_magic >= ICE_BARRAGE_LEVEL ? 1.0f : 0.0f;

    /* HEAD_SPEC (2): no_change, toggle. allow when blowpipe equipped + enough energy. */
    mask[offset++] = 1.0f;  /* no_change always valid */
    mask[offset++] = (s->weapon_set == INF_GEAR_BP &&
                      s->player.special_energy >= BLOWPIPE_SPEC_COST)
                     ? 1.0f : 0.0f;

    /* HEAD_OFFENSIVE: no_change, off, set_refresh_piety/rigour/augury. */
    mask[offset++] = 1.0f;  /* no_change always valid */
    mask[offset++] = s->player.offensive_prayer != OFFENSIVE_PRAYER_NONE ? 1.0f : 0.0f;
    mask[offset++] = s->player.current_prayer > 0 ? 1.0f : 0.0f;
    mask[offset++] = s->player.current_prayer > 0 ? 1.0f : 0.0f;
    mask[offset++] = s->player.current_prayer > 0 ? 1.0f : 0.0f;

    int mask_offset = 0;
    for (int h = 0; h < INF_NUM_ACTION_HEADS; h++) {
        int valid = 0;
        for (int a = 0; a < INF_ACTION_DIMS[h]; a++) {
            if (mask[mask_offset + a] > 0.0f)
                valid++;
        }
        if (valid == 0)
            s->zero_valid_action_head_count[h]++;
        if (valid < s->min_valid_action_count_by_head[h])
            s->min_valid_action_count_by_head[h] = valid;
        mask_offset += INF_ACTION_DIMS[h];
    }
    int target_offset = ENCOUNTER_MOVE_ACTIONS + ENCOUNTER_OVERHEAD_DIM_PVE + 1;
    for (int n = 0; n < INF_OBS_NPCS; n++) {
        if (mask[target_offset + n] <= 0.0f)
            continue;
        int npc_idx = s->current_obs_slots[n];
        if (npc_idx < 0 || npc_idx >= INF_MAX_NPCS)
            continue;
        int type = s->npcs[npc_idx].type;
        if (type == INF_NPC_HEALER_ZUK)
            s->target_head_valid_healer_count++;
        else if (type == INF_NPC_ZUK)
            s->target_head_valid_zuk_count++;
        else if (type != INF_NPC_ZUK_SHIELD)
            s->target_head_valid_set_count++;
    }
    s->total_action_mask_checks++;
}


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

    {
        const EncounterLoadoutStats* ls = inf_current_loadout_stats(s);
        s->player.gui_max_hit = ls->max_hit;
        s->player.gui_attack_speed = ls->attack_speed;
        s->player.gui_attack_range = ls->attack_range;
        s->player.gui_strength_bonus = ls->strength_bonus;
    }

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
        /* facing: nibblers → pillar (dest-based), NPCs attacking shield → shield
           (dest-based), all others → player (entity 0). */
        if (npc->type == INF_NPC_NIBBLER || npc->resurrecting_this_tick) {
            re->attack_target_entity_idx = -1;
        } else if (npc->aggro_target >= 0 && npc->aggro_target < INF_MAX_NPCS &&
                   s->npcs[npc->aggro_target].active) {
            /* attacking another NPC (e.g. shield) — use dest-based facing */
            re->attack_target_entity_idx = -1;
        } else {
            re->attack_target_entity_idx = 0;  /* player */
        }
        re->npc_visible = npc->active;
        re->npc_size = npc->size;
        {
            const NpcModelMapping* nm = npc_model_lookup(INF_NPC_DEF_IDS[npc->type]);
            if (npc->death_ticks > 0) {
                re->npc_anim_id = inf_npc_death_anim_id(npc, nm);
            } else if (npc->type == INF_NPC_MELEER &&
                       npc->dig_freeze_timer == 6) {
                re->npc_anim_id = INF_GEN_ANIM_MELEER_DIG_DOWN;
            } else if (npc->type == INF_NPC_MELEER &&
                       npc->dig_attack_delay == 6) {
                re->npc_anim_id = INF_GEN_ANIM_MELEER_DIG_UP;
            } else if (npc->attacked_this_tick) {
                re->npc_anim_id = inf_npc_attack_anim_id(npc, nm);
            } else {
                /* walk/idle handled by secondary track in render_client_tick.
                   setting walk as primary causes stall (interleave_count==0)
                   which freezes movement and creates tile-to-tile teleporting. */
                re->npc_anim_id = -1;
            }
        }
        re->x = npc->x;
        re->y = npc->y;
        /* nibblers: set dest to pillar center so renderer faces them toward
           the pillar instead of the player when idle/attacking */
        if (npc->type == INF_NPC_NIBBLER) {
            int tp = s->nibbler_target_pillar;
            if (tp >= 0 && tp < INF_NUM_PILLARS && s->pillars[tp].active) {
                re->dest_x = s->pillars[tp].x + INF_PILLAR_SIZE / 2;
                re->dest_y = s->pillars[tp].y + INF_PILLAR_SIZE / 2;
            } else {
                re->dest_x = npc->x;
                re->dest_y = npc->y;
            }
        } else if (npc->resurrection_visual_target >= 0 &&
                   npc->resurrection_visual_target < INF_MAX_NPCS &&
                   s->npcs[npc->resurrection_visual_target].active) {
            InfNPC* at = &s->npcs[npc->resurrection_visual_target];
            re->dest_x = at->x + at->size / 2;
            re->dest_y = at->y + at->size / 2;
        } else if (npc->aggro_target >= 0 && npc->aggro_target < INF_MAX_NPCS &&
                   s->npcs[npc->aggro_target].active) {
            /* attacking shield/other NPC — face toward target NPC center */
            InfNPC* at = &s->npcs[npc->aggro_target];
            re->dest_x = at->x + at->size / 2;
            re->dest_y = at->y + at->size / 2;
        } else {
            re->dest_x = npc->target_x;
            re->dest_y = npc->target_y;
        }
        re->current_hitpoints = npc->hp;
        re->base_hitpoints = npc->max_hp;
        re->attack_style_this_tick = npc->attacked_this_tick
            ? (AttackStyle)npc->attack_style_this_tick : ATTACK_STYLE_NONE;
        re->hit_landed_this_tick = npc->hit_landed_this_tick;
        re->hit_damage = npc->hit_damage;
        re->hit_was_successful = npc->hit_was_successful_this_tick;
        re->hit_spell_type = npc->hit_spell_type;
    }

    encounter_resolve_attack_target(out, n, s->interaction.target_slot);
    *count = n;
}

static void inf_put_int(EncounterState* state, const char* key, int value) {
    InfernoState* s = (InfernoState*)state;
    /* wave is 1-indexed externally (wave 1 = first, wave 69 = Zuk), 0-indexed internally */
    if (strcmp(key, "start_wave") == 0) {
        inf_require_valid_public_wave(value);
        s->start_wave = value - 1;
    }
    else if (strcmp(key, "seed") == 0) s->rng_state = (uint32_t)value;
    else if (strcmp(key, "world_offset_x") == 0) s->world_offset_x = value;
    else if (strcmp(key, "world_offset_y") == 0) s->world_offset_y = value;
    else if (strcmp(key, "player_dest_x") == 0) s->player_dest_x = value;
    else if (strcmp(key, "player_dest_y") == 0) s->player_dest_y = value;
    else if (strcmp(key, "human_command_mode") == 0)
        s->human_command_mode = encounter_require_binary_config("inferno", key, value);
    else if (strcmp(key, "terminal_penalty_enabled") == 0)
        s->terminal_penalty_enabled =
            encounter_require_binary_config("inferno", key, value);
    else if (strcmp(key, "step_out_forecast_obs_enabled") == 0)
        s->step_out_forecast_obs_enabled =
            encounter_require_binary_config("inferno", key, value);
    else if (strcmp(key, "oracle_mode") == 0) {
        if (value < 0 || value > 11) {
            fprintf(stderr, "inferno: oracle_mode must be in [0,11], got %d\n", value);
            abort();
        }
        s->oracle_mode = value;
    }
    else if (strcmp(key, "zuk_healer_reward_mode") == 0) {
        if (value < INF_ZUK_HEALER_REWARD_MODE_BASELINE ||
                value > INF_ZUK_HEALER_REWARD_MODE_TAGS_FIRST) {
            fprintf(stderr,
                "inferno: zuk_healer_reward_mode must be in [0,1], got %d\n",
                value);
            abort();
        }
        s->zuk_healer_reward_mode = value;
    }
    else if (strcmp(key, "joseph_reward_mode") == 0) {
        if (value < INF_JOSEPH_REWARD_MODE_OFF ||
                value > INF_JOSEPH_REWARD_MODE_ON) {
            fprintf(stderr,
                "inferno: joseph_reward_mode must be in [0,1], got %d\n",
                value);
            abort();
        }
        s->joseph_reward_mode = value;
    }
    else if (strcmp(key, "zuk_safe_untagged_healer_target_mask") == 0) {
        s->zuk_safe_untagged_healer_target_mask =
            encounter_require_binary_config("inferno", key, value);
    }
    else if (strcmp(key, "zuk_force_safe_untagged_healer_target_mask") == 0) {
        s->zuk_force_safe_untagged_healer_target_mask =
            encounter_require_binary_config("inferno", key, value);
    }
    else encounter_abort_unknown_config("inferno", "int", key);
}

static void inf_put_float(EncounterState* state, const char* key, float value) {
    InfernoState* s = (InfernoState*)state;
    if (strcmp(key, "damage_reward_coeff") == 0) s->damage_reward_coeff = value;
    else if (strcmp(key, "shield_penalty_coeff") == 0) s->shield_penalty_coeff = value;
    else if (strcmp(key, "tag_reward_coeff") == 0) s->tag_reward_coeff = value;
    else if (strcmp(key, "shield_tag_reward_coeff") == 0) s->shield_tag_reward_coeff = value;
    else if (strcmp(key, "death_penalty_coeff") == 0) {
        inf_require_nonnegative_float_config(key, value);
        s->death_penalty_coeff = value;
    }
    else if (strcmp(key, "phase_900_bonus") == 0) s->phase_900_bonus = value;
    else if (strcmp(key, "phase_600_bonus") == 0) s->phase_600_bonus = value;
    else if (strcmp(key, "phase_300_bonus") == 0) s->phase_300_bonus = value;
    else if (strcmp(key, "shield_penalty_episode_cap") == 0) s->shield_penalty_episode_cap = value;
    else if (strcmp(key, "late_start_supply_profile_scale") == 0) {
        inf_require_valid_supply_scale(value);
        s->late_start_supply_profile_scale = value;
    }
    else if (strcmp(key, "supply_milestone_brew_reward_coeff") == 0) {
        inf_require_nonnegative_float_config(key, value);
        s->supply_milestone_brew_reward_coeff = value;
    }
    else if (strcmp(key, "supply_milestone_restore_reward_coeff") == 0) {
        inf_require_nonnegative_float_config(key, value);
        s->supply_milestone_restore_reward_coeff = value;
    }
    else if (strcmp(key, "jad_damage_reward_coeff") == 0) s->jad_damage_reward_coeff = value;
    else if (strcmp(key, "zuk_healer_damage_reward_coeff") == 0) s->zuk_healer_damage_reward_coeff = value;
    else if (strcmp(key, "set_damage_reward_coeff") == 0) s->set_damage_reward_coeff = value;
    else if (strcmp(key, "jad_kill_bonus") == 0) s->jad_kill_bonus = value;
    else if (strcmp(key, "zuk_healer_kill_bonus") == 0) s->zuk_healer_kill_bonus = value;
    else if (strcmp(key, "set_kill_bonus") == 0) s->set_kill_bonus = value;
    else if (strcmp(key, "post_healer_zuk_damage_coeff") == 0) s->post_healer_zuk_damage_coeff = value;
    else if (strcmp(key, "post_healer_set_damage_reward_coeff") == 0) {
        s->post_healer_set_damage_reward_coeff = value;
    }
    else if (strcmp(key, "post_healer_set_kill_bonus") == 0) {
        s->post_healer_set_kill_bonus = value;
    }
    else if (strcmp(key, "post_healer_set_alive_tick_penalty_coeff") == 0) {
        s->post_healer_set_alive_tick_penalty_coeff = value;
    }
    else if (strcmp(key, "post_healer_set_alive_penalty_cap") == 0) {
        s->post_healer_set_alive_penalty_cap = value;
    }
    else if (strcmp(key, "zuk_healer_phase_hp_delta_coeff") == 0) s->zuk_healer_phase_hp_delta_coeff = value;
    else if (strcmp(key, "zuk_untagged_healer_tick_penalty_coeff") == 0) {
        s->zuk_untagged_healer_tick_penalty_coeff = value;
    }
    else if (strcmp(key, "zuk_untagged_healer_target_bonus_coeff") == 0) {
        s->zuk_untagged_healer_target_bonus_coeff = value;
    }
    else if (strcmp(key, "zuk_safe_untagged_healer_target_bonus_coeff") == 0) {
        s->zuk_safe_untagged_healer_target_bonus_coeff = value;
    }
    else if (strcmp(key, "zuk_untagged_healer_nonmagic_attack_bonus_coeff") == 0) {
        s->zuk_untagged_healer_nonmagic_attack_bonus_coeff = value;
    }
    else if (strcmp(key, "zuk_healer_mage_attack_penalty_coeff") == 0) {
        s->zuk_healer_mage_attack_penalty_coeff = value;
    }
    else if (strcmp(key, "post_jad_zuk_multiplier") == 0) s->post_jad_zuk_multiplier = value;
    else if (strcmp(key, "jad_alive_zuk_multiplier") == 0) s->jad_alive_zuk_multiplier = value;
    else encounter_abort_unknown_config("inferno", "float", key);
}

static void inf_put_ptr(EncounterState* state, const char* key, void* value) {
    InfernoState* s = (InfernoState*)state;
    if (strcmp(key, "collision_map") == 0) s->collision_map = (const CollisionMap*)value;
    else encounter_abort_unknown_config("inferno", "ptr", key);
}

static int inf_get_tick(EncounterState* state) {
    return ((InfernoState*)state)->tick;
}

static int inf_get_winner(EncounterState* state) {
    return ((InfernoState*)state)->winner;
}

static void inf_write_terminal_status_text(const InfernoState* s, char* out, size_t cap) {
    if (cap == 0) return;
    out[0] = '\0';
    if (!s->episode_over) return;
    if (s->winner == 0) {
        snprintf(out, cap, "Inferno cleared");
        return;
    }
    snprintf(out, cap, "Killed by %s", inf_npc_type_name(s->last_hit_by_type));
}

static void* inf_get_log(EncounterState* state) {
    InfernoState* s = (InfernoState*)state;
    if (s->episode_over) {
        s->log.episode_return += s->episode_return;
        s->log.episode_length += (float)s->tick;
        s->log.wins += (s->winner == 0) ? 1.0f : 0.0f;
        s->log.damage_dealt += s->total_damage_dealt;
        s->log.zuk_healer_damage += s->total_zuk_healer_damage;
        s->log.damage_received += s->total_damage_received;
        s->log.wave += (float)s->wave;
        s->log.prayer_correct += (float)s->total_prayer_correct;
        s->log.prayer_total += (float)s->total_npc_attacks;
        s->log.idle_ticks += (float)s->total_idle_ticks;
        s->log.brews_used += (float)s->total_brews_used;
        s->log.blood_healed += (float)s->total_blood_healed;
        s->log.ranger_mager_same_tick_attacks +=
            (float)s->total_ranger_mager_same_tick_attacks;
        s->log.step_out_ranger_mager_same_tick_attacks +=
            (float)s->total_step_out_ranger_mager_same_tick_attacks;
        s->log.n += 1.0f;
        s->log.npc_kills += (float)s->total_npc_kills;
        s->log.gear_switches += (float)s->total_gear_switches;
        s->log.current_ranged += (float)s->player.current_ranged;
        s->log.current_magic += (float)s->player.current_magic;
        s->log.min_zuk_hp_seen += (s->winner == 0)
            ? 0.0f
            : (s->min_zuk_hp_seen > 0.0f ? s->min_zuk_hp_seen : 1200.0f);
    }
    return &s->log;
}



static void inf_render_post_tick(EncounterState* state, EncounterOverlay* ov) {
    InfernoState* s = (InfernoState*)state;
    ov->projectile_count = 0;
    inf_write_terminal_status_text(s, ov->status_text, sizeof(ov->status_text));
    ov->status_text_active = ov->status_text[0] != '\0';

    /* NPC attack projectiles — per-NPC-type flight parameters */
    for (int i = 0; i < INF_MAX_NPCS; i++) {
        InfNPC* npc = &s->npcs[i];
        if (!npc->active || !npc->attacked_this_tick) continue;

        /* nibblers attack pillars, not worth showing as projectile */
        if (npc->type == INF_NPC_NIBBLER) continue;
        if (npc->resurrecting_this_tick) continue;

        /* blob scan animation (no projectile) — only emit on the actual fire tick.
           blob_scanned_prayer >= 0 means scan just happened, -1 means fire. */
        if (npc->type == INF_NPC_BLOB && npc->blob_scanned_prayer >= 0) continue;

        const InfNPCStats* stats = &INF_NPC_STATS[npc->type];
        int actual_style = npc->attack_style_this_tick;

        /* Zuk is typeless — show as magic for visual purposes. */
        if (actual_style == ATTACK_STYLE_NONE && npc->type == INF_NPC_ZUK)
            actual_style = ATTACK_STYLE_MAGIC;

        /* tagged Zuk healers spawn their own 3-spark visuals from pending_sparks. */
        if (npc->type == INF_NPC_HEALER_ZUK && npc->attack_visual_target < 0)
            continue;

        if (actual_style == ATTACK_STYLE_NONE) continue;

        /* melee attacks are instant — no in-flight projectile */
        if (actual_style == ATTACK_STYLE_MELEE) continue;

        int proj_style = encounter_attack_style_to_proj_style(actual_style);
        int npc_size = stats->size;
        int start_h = (int)(npc_size * 0.75f * 128);
        int end_h = 64;  /* default: player size 1 * 0.5 * 128 */
        int curve = 16;
        float arc = 0.0f;
        int tracks = 1;

        int target_tile_x = s->player.x;
        int target_tile_y = s->player.y;
        int target_size = 1;
        int target_x = s->player.x, target_y = s->player.y;
        if (npc->attack_visual_target >= 0 && npc->attack_visual_target < INF_MAX_NPCS) {
            InfNPC* vt = &s->npcs[npc->attack_visual_target];
            target_tile_x = vt->x;
            target_tile_y = vt->y;
            target_size = vt->size;
            target_x = vt->x + vt->size / 2;
            target_y = vt->y + vt->size / 2;
            end_h = (int)(vt->size * 0.5f * 128);
            tracks = 0;  /* don't track player — projectile targets shield/NPC */
        }
        int dist = encounter_projectile_distance(
            npc->x, npc->y, npc_size, target_tile_x, target_tile_y, target_size,
            ENCOUNTER_PROJECTILE_DISTANCE_CLOSEST_TILE);
        EncounterProjectileTiming timing =
            inf_npc_projectile_timing(npc->type, actual_style, dist);
        int duration = timing.visual_duration_ticks * 30;
        int start_delay = timing.visual_start_delay_ticks * 30;

        /* per-NPC-type projectile GFX model ID */
        uint32_t proj_model_id = 0;
        switch (npc->type) {
            case INF_NPC_BAT:        proj_model_id = INF_GFX_1374_MODEL; break;
            case INF_NPC_BLOB:       proj_model_id = (actual_style == ATTACK_STYLE_RANGED) ? INF_GFX_1378_MODEL : INF_GFX_1380_MODEL; break;
            case INF_NPC_BLOB_RANGE: proj_model_id = INF_GFX_1379_MODEL; break;
            case INF_NPC_BLOB_MAGE:  proj_model_id = INF_GFX_1381_MODEL; break;
            case INF_NPC_BLOB_MELEE: proj_model_id = INF_GFX_1382_MODEL; break;
            case INF_NPC_RANGER:     proj_model_id = INF_GFX_1377_MODEL; break;
            case INF_NPC_MAGER:      proj_model_id = INF_GFX_1376_MODEL; break;
            case INF_NPC_JAD:
                proj_model_id = (actual_style == ATTACK_STYLE_MAGIC)
                    ? INF_GFX_448_MODEL
                    : INF_GFX_451_MODEL;
                break;
            case INF_NPC_ZUK:        proj_model_id = INF_GFX_1375_MODEL; break;
            case INF_NPC_HEALER_ZUK: proj_model_id = INF_GFX_660_MODEL; break;
            default: break;
        }

        /* NPC-specific flight overrides */
        switch (npc->type) {
            case INF_NPC_JAD:
                if (actual_style == ATTACK_STYLE_MAGIC) {
                    arc = 1.0f;  /* arcing magic projectile */
                } else {
                    start_h = end_h;
                }
                start_delay = INF_JAD_PROJECTILE_DELAY * 30;
                break;
            case INF_NPC_HEALER_ZUK:
                arc = 3.0f;      /* high arcing spark */
                duration = (npc->attack_visual_target >= 0) ? 3 * 30 : 4 * 30;
                start_delay = 0;
                break;
            default: break;
        }

        if (npc->type == INF_NPC_JAD && actual_style == ATTACK_STYLE_MAGIC) {
            encounter_require_projectile_slots(ov, 3);
            uint32_t model_ids[3] = {
                INF_GFX_448_MODEL, INF_GFX_449_MODEL, INF_GFX_450_MODEL
            };
            int anim_ids[3] = {
                INF_GFX_448_ANIM, INF_GFX_449_ANIM, INF_GFX_450_ANIM
            };
            float offsets[3] = {1.0f, 0.5f, 0.0f};
            for (int j = 0; j < 3; j++) {
                int pi = encounter_emit_projectile(ov,
                    npc->x, npc->y, target_x, target_y,
                    proj_style, (int)s->damage_received_this_tick,
                    duration, start_h, end_h, curve, arc, tracks, npc_size, 1,
                    model_ids[j], 0);
                ov->projectiles[pi].start_delay = start_delay;
                encounter_set_projectile_animation(ov, pi, anim_ids[j]);
                encounter_set_projectile_offset(ov, pi, 0.0f, offsets[j], 0.0f);
            }
            continue;
        }

        int impact_gfx_id = (npc->type == INF_NPC_HEALER_ZUK) ? INF_GFX_659_ID : 0;
        int pi = encounter_emit_projectile(ov,
            npc->x, npc->y, target_x, target_y,
            proj_style, (int)s->damage_received_this_tick,
            duration, start_h, end_h, curve, arc, tracks, npc_size, 1,
            proj_model_id, impact_gfx_id);

        ov->projectiles[pi].start_delay = start_delay;

        if (npc->type == INF_NPC_JAD && actual_style == ATTACK_STYLE_RANGED)
            encounter_set_projectile_motion_mode(
                ov, pi, ENCOUNTER_PROJECTILE_MOTION_TARGET_ANCHORED);

        if (npc->type == INF_NPC_JAD && actual_style == ATTACK_STYLE_RANGED)
            encounter_set_projectile_animation(ov, pi, INF_GFX_451_ANIM);

    }

    for (int i = 0; i < INF_MAX_PENDING_SPARKS; i++) {
        InfPendingSpark* spark = &s->pending_sparks[i];
        if (!spark->active || spark->visual_emitted) continue;

        encounter_emit_projectile(
            ov,
            spark->src_x, spark->src_y, spark->x, spark->y,
            encounter_attack_style_to_proj_style(ATTACK_STYLE_MAGIC),
            spark->damage,
            4 * 30, 96, 64, 16, 3.0f, 0, 1, 1,
            INF_GFX_660_MODEL, INF_GFX_659_ID);
        spark->visual_emitted = 1;
    }

    /* player attack projectile (ranged/magic only — melee has no projectile) */
    if (s->player_attacked_this_tick &&
        s->player_attack_style_id != ATTACK_STYLE_MELEE) {
        int target_idx = s->player_attack_npc_idx;
        if (target_idx >= 0 && target_idx < INF_MAX_NPCS) {
            InfNPC* target = &s->npcs[target_idx];
            int target_size = INF_NPC_STATS[target->type].size;
            int p_start_h = 64;   /* player: size 1 * 0.5 * 128 */
            int p_end_h = (int)(target_size * 0.5f * 128);
            int p_style = encounter_attack_style_to_proj_style(s->player_attack_style_id);
            float p_arc = 0.0f;
            int p_tracks = 0;  /* don't track — tracking loop targets entity 0 (player) */
            int p_duration = s->player_attack_timing.visual_duration_ticks * 30;
            int p_start_delay = s->player_attack_timing.visual_start_delay_ticks * 30;
            uint8_t weapon = s->player.equipped[GEAR_SLOT_WEAPON];

            uint32_t player_proj_model = 0;
            if (s->player_attack_style_id == ATTACK_STYLE_MAGIC) {
                p_arc = 0.0f;
                /* barrage: no projectile model (effect system handles it) */
            } else if (weapon == ITEM_TWISTED_BOW) {
                p_arc = 1.0f;
                player_proj_model = INF_GFX_1120_MODEL;
            } else {
                /* blowpipe */
                p_arc = 0.5f;
                player_proj_model = 26379;  /* dragon dart */
            }

            /* barrage has no in-flight projectile in OSRS (only hit splash) */
            if (player_proj_model > 0) {
                int pi = encounter_emit_projectile(ov,
                    s->player.x, s->player.y, target->x, target->y,
                    p_style, s->player_attack_dmg,
                    p_duration, p_start_h, p_end_h, 16, p_arc, p_tracks,
                    1, target_size, player_proj_model, 0);
                ov->projectiles[pi].start_delay = p_start_delay;
            }
        }
    }
}


static void* inf_get_player_for_input(void* state, int idx) {
    InfernoState* s = (InfernoState*)state;
    return (idx == 0) ? (void*)&s->player : NULL;
}

static void inf_translate_human_input(HumanInput* hi, int* actions, EncounterState* state) {
    for (int h = 0; h < INF_NUM_ACTION_HEADS; h++) actions[h] = 0;

    encounter_translate_movement(hi, actions, INF_HEAD_MOVE, inf_get_player_for_input, state);
    encounter_translate_prayer(hi, actions, INF_HEAD_PRAYER);
    encounter_translate_offensive_prayer(hi, actions, INF_HEAD_OFFENSIVE);
    InfernoState* s = (InfernoState*)state;
    inf_refresh_current_obs_slots(s);
    /* map raw pending_target_idx to the observation slot the agent sees */
    if (hi->pending_target_idx >= 0) {
        int found_slot = inf_find_target_obs_slot(s, hi->pending_target_idx);
        if (inf_obs_slot_is_targetable(s, found_slot)) {
            actions[INF_HEAD_TARGET] = found_slot + 1;
        } else {
            actions[INF_HEAD_TARGET] = 0;
        }
    } else {
        actions[INF_HEAD_TARGET] = 0;
    }

    /* gear switch */
    if (hi->pending_gear > 0) actions[INF_HEAD_GEAR] = hi->pending_gear;

    /* eat: brew */
    if (hi->pending_food || hi->pending_potion == POTION_BREW)
        actions[INF_HEAD_EAT] = 1;

    /* potions: restore=1, bastion=2, stamina=3 */
    if (hi->pending_potion == POTION_RESTORE) actions[INF_HEAD_POTION] = 1;
    else if (hi->pending_potion == POTION_BASTION) actions[INF_HEAD_POTION] = 2;
    else if (hi->pending_potion == POTION_STAMINA) actions[INF_HEAD_POTION] = 3;

    /* spell: 0=no change, 1=blood, 2=ice */
    if (hi->pending_spell == ATTACK_BLOOD) actions[INF_HEAD_SPELL] = 1;
    else if (hi->pending_spell == ATTACK_ICE) actions[INF_HEAD_SPELL] = 2;

    /* spec */
    if (hi->pending_spec) actions[INF_HEAD_SPEC] = 1;
}

static void inf_translate_human_commands(HumanInput* hi, int* actions, InfernoState* s) {
    for (int h = 0; h < INF_NUM_ACTION_HEADS; h++) actions[h] = 0;
    inf_refresh_current_obs_slots(s);

    for (int i = 0; i < hi->commands.count; i++) {
        const HumanCommand* cmd = &hi->commands.items[i];
        switch (cmd->kind) {
            case HUMAN_COMMAND_WALK:
                s->player_dest_x = cmd->world_x;
                s->player_dest_y = cmd->world_y;
                actions[INF_HEAD_TARGET] = 0;
                actions[INF_HEAD_SPELL] = 0;
                break;
            case HUMAN_COMMAND_ATTACK_NPC: {
                int found_slot = inf_find_target_obs_slot(s, cmd->npc_slot);
                actions[INF_HEAD_TARGET] = inf_obs_slot_is_targetable(s, found_slot)
                    ? found_slot + 1 : 0;
                s->player_dest_x = -1;
                s->player_dest_y = -1;
                break;
            }
            case HUMAN_COMMAND_SPELL_TARGET: {
                int found_slot = inf_find_target_obs_slot(s, cmd->npc_slot);
                actions[INF_HEAD_TARGET] = inf_obs_slot_is_targetable(s, found_slot)
                    ? found_slot + 1 : 0;
                if (cmd->spell == ATTACK_BLOOD) actions[INF_HEAD_SPELL] = 1;
                else if (cmd->spell == ATTACK_ICE) actions[INF_HEAD_SPELL] = 2;
                s->player_dest_x = -1;
                s->player_dest_y = -1;
                break;
            }
            case HUMAN_COMMAND_OVERHEAD_PRAYER:
                actions[INF_HEAD_PRAYER] = cmd->overhead_prayer;
                break;
            case HUMAN_COMMAND_OFFENSIVE_PRAYER:
                actions[INF_HEAD_OFFENSIVE] = cmd->offensive_prayer;
                break;
            case HUMAN_COMMAND_EAT:
                actions[INF_HEAD_EAT] = 1;
                break;
            case HUMAN_COMMAND_DRINK:
                if (cmd->potion == POTION_BREW) actions[INF_HEAD_EAT] = 1;
                else if (cmd->potion == POTION_RESTORE) actions[INF_HEAD_POTION] = 1;
                else if (cmd->potion == POTION_BASTION) actions[INF_HEAD_POTION] = 2;
                else if (cmd->potion == POTION_STAMINA) actions[INF_HEAD_POTION] = 3;
                break;
            case HUMAN_COMMAND_SPEC_TOGGLE:
                actions[INF_HEAD_SPEC] = 1;
                break;
            case HUMAN_COMMAND_EQUIP_INVENTORY_ITEM:
            case HUMAN_COMMAND_FIGHT_STYLE:
            case HUMAN_COMMAND_SET_AUTOCAST:
            case HUMAN_COMMAND_NONE:
                break;
        }
    }
}

static void inf_step_human_commands(EncounterState* state, HumanInput* hi) {
    InfernoState* s = (InfernoState*)state;
    int actions[INF_NUM_ACTION_HEADS];
    s->human_command_mode = 1;
    s->human_commands = hi->commands.items;
    s->human_command_count = hi->commands.count;
    inf_refresh_human_loadout_stats(s);
    inf_translate_human_commands(hi, actions, s);
    inf_step(state, actions);
    s->human_commands = NULL;
    s->human_command_count = 0;
    human_input_clear_pending(hi);
}


/* archive cell key for Go-Explore-style exploration. fixed 16-byte struct
   that discretizes the env state. byte-equal keys = same cell. lossy on
   continuous fields (player position quantized to 2-tile bins, HP/prayer to
   10-unit bins, Zuk and Jad HP to 50-HP bins). */

typedef struct {
    uint8_t wave;                       /* 0..68 */
    uint8_t weapon_set;                 /* INF_GEAR_* enum */
    uint8_t player_hp_bin;              /* current_hitpoints / 10 */
    uint8_t player_prayer_bin;          /* current_prayer / 10 */
    uint8_t brew_doses;                 /* exact dose count */
    uint8_t restore_doses;              /* exact dose count */
    uint8_t overhead_prayer;            /* PRAYER_PROTECT_* enum */
    uint8_t offensive_prayer_attack_timer; /* low nibble offensive, high nibble player attack_timer */
    uint8_t player_x_quant;             /* (player.x - INF_ARENA_MIN_X) / 2 */
    uint8_t player_y_quant;             /* (player.y - INF_ARENA_MIN_Y) / 2 */
    uint8_t zuk_hp_bin;                 /* live Zuk HP / 50, 0 if no Zuk alive */
    uint8_t zuk_phase_flags;            /* bit0=healer_spawned, 1=jad_spawned, 2=enraged, 3=timer_paused */
    uint8_t active_jad_count;           /* live Jads */
    uint8_t active_zuk_healer_count;    /* live Zuk healers */
    uint8_t active_set_count;           /* live magers + rangers + meleers */
    uint8_t jad_hp_bin;                 /* total live Jad HP / 50 */
} InfCellKey;

typedef struct {
    int live_jad_count;
    int live_zuk_healer_count;
    int live_set_count;
    int live_jad_hp;
    int live_jad_max_hp;
} InfLateAddCounts;

static InfLateAddCounts inf_late_add_counts(const InfernoState* s) {
    InfLateAddCounts counts;
    memset(&counts, 0, sizeof(counts));

    for (int i = 0; i < INF_MAX_NPCS; i++) {
        const InfNPC* npc = &s->npcs[i];
        if (!npc->active || npc->hp <= 0) continue;
        switch (npc->type) {
            case INF_NPC_JAD:
                counts.live_jad_count++;
                counts.live_jad_hp += npc->hp;
                counts.live_jad_max_hp += npc->max_hp > 0 ? npc->max_hp : npc->hp;
                break;
            case INF_NPC_HEALER_ZUK:
                counts.live_zuk_healer_count++;
                break;
            case INF_NPC_MAGER:
            case INF_NPC_MELEER:
            case INF_NPC_RANGER:
                counts.live_set_count++;
                break;
            default:
                break;
        }
    }

    return counts;
}

static size_t inf_cell_key_size(EncounterState* state) {
    (void)state;
    return sizeof(InfCellKey);
}

static uint8_t inf_cell_attack_timer_bucket(const InfernoState* s) {
    int timer = s->player.attack_timer;
    if (timer < 0) timer = 0;
    if (timer > 15) timer = 15;
    return (uint8_t)timer;
}

static void inf_write_cell_key(EncounterState* state, void* out) {
    const InfernoState* s = (const InfernoState*)state;
    InfCellKey* k = (InfCellKey*)out;
    memset(k, 0, sizeof(InfCellKey));

    k->wave = (uint8_t)(s->wave & 0xff);
    k->weapon_set = (uint8_t)s->weapon_set;

    k->player_hp_bin = (uint8_t)(s->player.current_hitpoints / 10);
    k->player_prayer_bin = (uint8_t)(s->player.current_prayer / 10);
    k->brew_doses = (uint8_t)s->player.brew_doses;
    k->restore_doses = (uint8_t)s->player.restore_doses;
    k->overhead_prayer = (uint8_t)s->player.prayer;
    k->offensive_prayer_attack_timer = (uint8_t)(
        ((uint8_t)s->player.offensive_prayer & 0x0fu) |
        (inf_cell_attack_timer_bucket(s) << 4));

    int dx = s->player.x - INF_ARENA_MIN_X;
    int dy = s->player.y - INF_ARENA_MIN_Y;
    if (dx < 0) dx = 0;
    if (dy < 0) dy = 0;
    k->player_x_quant = (uint8_t)((dx / 2) & 0xff);
    k->player_y_quant = (uint8_t)((dy / 2) & 0xff);

    int zuk_idx = inf_find_live_zuk_idx(s);
    int zuk_hp = (zuk_idx >= 0) ? s->npcs[zuk_idx].hp : 0;
    k->zuk_hp_bin = (uint8_t)((zuk_hp / 50) & 0xff);

    k->zuk_phase_flags = (uint8_t)(
        (s->zuk.healer_spawned ? 0x01u : 0u) |
        (s->zuk.jad_spawned    ? 0x02u : 0u) |
        (s->zuk.enraged        ? 0x04u : 0u) |
        (s->zuk.timer_paused   ? 0x08u : 0u)
    );

    InfLateAddCounts counts = inf_late_add_counts(s);
    k->active_jad_count = (uint8_t)(
        counts.live_jad_count > 255 ? 255 : counts.live_jad_count);
    k->active_zuk_healer_count = (uint8_t)(
        counts.live_zuk_healer_count > 255 ? 255 : counts.live_zuk_healer_count);
    k->active_set_count = (uint8_t)(
        counts.live_set_count > 255 ? 255 : counts.live_set_count);
    int jad_hp_bin = counts.live_jad_hp / 50;
    k->jad_hp_bin = (uint8_t)(jad_hp_bin > 255 ? 255 : jad_hp_bin);
}

/* progress_score: archive quality for Zuk transitions. */
static float inf_progress_score(EncounterState* state) {
    const InfernoState* s = (const InfernoState*)state;
    if (s->episode_over && s->winner == 0) return 2.0f;

    float min_zhp = (s->min_zuk_hp_seen > 0.0f) ? s->min_zuk_hp_seen : 1200.0f;
    if (min_zhp < 0.0f) min_zhp = 0.0f;
    if (min_zhp > 1200.0f) min_zhp = 1200.0f;

    float q = (1200.0f - min_zhp) / 1200.0f;
    InfLateAddCounts counts = inf_late_add_counts(s);

    if (s->zuk.jad_spawned && counts.live_jad_count == 0) {
        q += 0.10f;
        if (min_zhp < 600.0f) {
            q += 0.06f * ((600.0f - min_zhp) / 600.0f);
        }
    } else if (s->zuk.jad_spawned && counts.live_jad_max_hp > 0) {
        float jad_damage_frac =
            (float)(counts.live_jad_max_hp - counts.live_jad_hp) /
            (float)counts.live_jad_max_hp;
        if (jad_damage_frac < 0.0f) jad_damage_frac = 0.0f;
        if (jad_damage_frac > 1.0f) jad_damage_frac = 1.0f;
        q += 0.09f * jad_damage_frac;
    }

    if (s->zuk.healer_spawned && counts.live_zuk_healer_count == 0) {
        q += 0.08f;
    }

    if (min_zhp <= 900.0f && counts.live_set_count == 0) {
        q += 0.04f;
    }

    return q;
}


/* snapshot/restore: archive-based exploration captures the full encounter state
   so we can branch from it later. process-local pointers (collision_map,
   human_commands) and the per-env episode Log accumulator are preserved across
   restore — they belong to the live env, not the recorded snapshot. */

#define INF_SNAPSHOT_MAGIC 0x1FE00001u
#define INF_SNAPSHOT_VERSION 13u

typedef struct {
    uint32_t magic;
    uint32_t version;
    InfernoState state;
} InfSnapshot;

typedef struct {
    const CollisionMap* collision_map;
    int world_offset_x;
    int world_offset_y;
    Log log;
    const HumanCommand* human_commands;
    int human_command_count;
    int human_command_mode;
    int start_wave;
    float damage_reward_coeff;
    float shield_penalty_coeff;
    float tag_reward_coeff;
    float shield_tag_reward_coeff;
    float late_start_supply_profile_scale;
    float supply_milestone_brew_reward_coeff;
    float supply_milestone_restore_reward_coeff;
    float death_penalty_coeff;
    int terminal_penalty_enabled;
    int step_out_forecast_obs_enabled;
    float phase_900_bonus;
    float phase_600_bonus;
    float phase_300_bonus;
    float shield_penalty_episode_cap;
    int oracle_mode;
    float jad_damage_reward_coeff;
    float zuk_healer_damage_reward_coeff;
    float set_damage_reward_coeff;
    float jad_kill_bonus;
    float zuk_healer_kill_bonus;
    float set_kill_bonus;
    float post_healer_zuk_damage_coeff;
    float post_healer_set_damage_reward_coeff;
    float post_healer_set_kill_bonus;
    float post_healer_set_alive_tick_penalty_coeff;
    float post_healer_set_alive_penalty_cap;
    float zuk_healer_phase_hp_delta_coeff;
    float zuk_untagged_healer_tick_penalty_coeff;
    float zuk_untagged_healer_target_bonus_coeff;
    float zuk_safe_untagged_healer_target_bonus_coeff;
    float zuk_untagged_healer_nonmagic_attack_bonus_coeff;
    float zuk_healer_mage_attack_penalty_coeff;
    int zuk_safe_untagged_healer_target_mask;
    int zuk_force_safe_untagged_healer_target_mask;
    int zuk_healer_reward_mode;
    int joseph_reward_mode;
    float post_jad_zuk_multiplier;
    float jad_alive_zuk_multiplier;
} InfLiveRestoreFields;

static InfLiveRestoreFields inf_capture_live_restore_fields(const InfernoState* s) {
    return (InfLiveRestoreFields){
        .collision_map = s->collision_map,
        .world_offset_x = s->world_offset_x,
        .world_offset_y = s->world_offset_y,
        .log = s->log,
        .human_commands = s->human_commands,
        .human_command_count = s->human_command_count,
        .human_command_mode = s->human_command_mode,
        .start_wave = s->start_wave,
        .damage_reward_coeff = s->damage_reward_coeff,
        .shield_penalty_coeff = s->shield_penalty_coeff,
        .tag_reward_coeff = s->tag_reward_coeff,
        .shield_tag_reward_coeff = s->shield_tag_reward_coeff,
        .late_start_supply_profile_scale = s->late_start_supply_profile_scale,
        .supply_milestone_brew_reward_coeff =
            s->supply_milestone_brew_reward_coeff,
        .supply_milestone_restore_reward_coeff =
            s->supply_milestone_restore_reward_coeff,
        .death_penalty_coeff = s->death_penalty_coeff,
        .terminal_penalty_enabled = s->terminal_penalty_enabled,
        .step_out_forecast_obs_enabled = s->step_out_forecast_obs_enabled,
        .phase_900_bonus = s->phase_900_bonus,
        .phase_600_bonus = s->phase_600_bonus,
        .phase_300_bonus = s->phase_300_bonus,
        .shield_penalty_episode_cap = s->shield_penalty_episode_cap,
        .oracle_mode = s->oracle_mode,
        .jad_damage_reward_coeff = s->jad_damage_reward_coeff,
        .zuk_healer_damage_reward_coeff = s->zuk_healer_damage_reward_coeff,
        .set_damage_reward_coeff = s->set_damage_reward_coeff,
        .jad_kill_bonus = s->jad_kill_bonus,
        .zuk_healer_kill_bonus = s->zuk_healer_kill_bonus,
        .set_kill_bonus = s->set_kill_bonus,
        .post_healer_zuk_damage_coeff = s->post_healer_zuk_damage_coeff,
        .post_healer_set_damage_reward_coeff =
            s->post_healer_set_damage_reward_coeff,
        .post_healer_set_kill_bonus = s->post_healer_set_kill_bonus,
        .post_healer_set_alive_tick_penalty_coeff =
            s->post_healer_set_alive_tick_penalty_coeff,
        .post_healer_set_alive_penalty_cap =
            s->post_healer_set_alive_penalty_cap,
        .zuk_healer_phase_hp_delta_coeff = s->zuk_healer_phase_hp_delta_coeff,
        .zuk_untagged_healer_tick_penalty_coeff =
            s->zuk_untagged_healer_tick_penalty_coeff,
        .zuk_untagged_healer_target_bonus_coeff =
            s->zuk_untagged_healer_target_bonus_coeff,
        .zuk_safe_untagged_healer_target_bonus_coeff =
            s->zuk_safe_untagged_healer_target_bonus_coeff,
        .zuk_untagged_healer_nonmagic_attack_bonus_coeff =
            s->zuk_untagged_healer_nonmagic_attack_bonus_coeff,
        .zuk_healer_mage_attack_penalty_coeff =
            s->zuk_healer_mage_attack_penalty_coeff,
        .zuk_safe_untagged_healer_target_mask =
            s->zuk_safe_untagged_healer_target_mask,
        .zuk_force_safe_untagged_healer_target_mask =
            s->zuk_force_safe_untagged_healer_target_mask,
        .zuk_healer_reward_mode = s->zuk_healer_reward_mode,
        .joseph_reward_mode = s->joseph_reward_mode,
        .post_jad_zuk_multiplier = s->post_jad_zuk_multiplier,
        .jad_alive_zuk_multiplier = s->jad_alive_zuk_multiplier,
    };
}

static void inf_apply_live_restore_fields(
    InfernoState* s,
    InfLiveRestoreFields fields
) {
    s->collision_map = fields.collision_map;
    s->world_offset_x = fields.world_offset_x;
    s->world_offset_y = fields.world_offset_y;
    s->log = fields.log;
    s->human_commands = fields.human_commands;
    s->human_command_count = fields.human_command_count;
    s->human_command_mode = fields.human_command_mode;
    s->start_wave = fields.start_wave;
    s->damage_reward_coeff = fields.damage_reward_coeff;
    s->shield_penalty_coeff = fields.shield_penalty_coeff;
    s->tag_reward_coeff = fields.tag_reward_coeff;
    s->shield_tag_reward_coeff = fields.shield_tag_reward_coeff;
    s->late_start_supply_profile_scale = fields.late_start_supply_profile_scale;
    s->supply_milestone_brew_reward_coeff =
        fields.supply_milestone_brew_reward_coeff;
    s->supply_milestone_restore_reward_coeff =
        fields.supply_milestone_restore_reward_coeff;
    s->death_penalty_coeff = fields.death_penalty_coeff;
    s->terminal_penalty_enabled = fields.terminal_penalty_enabled;
    s->step_out_forecast_obs_enabled = fields.step_out_forecast_obs_enabled;
    s->phase_900_bonus = fields.phase_900_bonus;
    s->phase_600_bonus = fields.phase_600_bonus;
    s->phase_300_bonus = fields.phase_300_bonus;
    s->shield_penalty_episode_cap = fields.shield_penalty_episode_cap;
    s->oracle_mode = fields.oracle_mode;
    s->jad_damage_reward_coeff = fields.jad_damage_reward_coeff;
    s->zuk_healer_damage_reward_coeff = fields.zuk_healer_damage_reward_coeff;
    s->set_damage_reward_coeff = fields.set_damage_reward_coeff;
    s->jad_kill_bonus = fields.jad_kill_bonus;
    s->zuk_healer_kill_bonus = fields.zuk_healer_kill_bonus;
    s->set_kill_bonus = fields.set_kill_bonus;
    s->post_healer_zuk_damage_coeff = fields.post_healer_zuk_damage_coeff;
    s->post_healer_set_damage_reward_coeff =
        fields.post_healer_set_damage_reward_coeff;
    s->post_healer_set_kill_bonus = fields.post_healer_set_kill_bonus;
    s->post_healer_set_alive_tick_penalty_coeff =
        fields.post_healer_set_alive_tick_penalty_coeff;
    s->post_healer_set_alive_penalty_cap =
        fields.post_healer_set_alive_penalty_cap;
    s->zuk_healer_phase_hp_delta_coeff = fields.zuk_healer_phase_hp_delta_coeff;
    s->zuk_untagged_healer_tick_penalty_coeff =
        fields.zuk_untagged_healer_tick_penalty_coeff;
    s->zuk_untagged_healer_target_bonus_coeff =
        fields.zuk_untagged_healer_target_bonus_coeff;
    s->zuk_safe_untagged_healer_target_bonus_coeff =
        fields.zuk_safe_untagged_healer_target_bonus_coeff;
    s->zuk_untagged_healer_nonmagic_attack_bonus_coeff =
        fields.zuk_untagged_healer_nonmagic_attack_bonus_coeff;
    s->zuk_healer_mage_attack_penalty_coeff =
        fields.zuk_healer_mage_attack_penalty_coeff;
    s->zuk_safe_untagged_healer_target_mask =
        fields.zuk_safe_untagged_healer_target_mask;
    s->zuk_force_safe_untagged_healer_target_mask =
        fields.zuk_force_safe_untagged_healer_target_mask;
    s->zuk_healer_reward_mode = fields.zuk_healer_reward_mode;
    s->joseph_reward_mode = fields.joseph_reward_mode;
    s->post_jad_zuk_multiplier = fields.post_jad_zuk_multiplier;
    s->jad_alive_zuk_multiplier = fields.jad_alive_zuk_multiplier;
}

static size_t inf_snapshot_size(EncounterState* state) {
    (void)state;
    return sizeof(InfSnapshot);
}

static void inf_snapshot(EncounterState* state, void* out) {
    InfSnapshot* snap = (InfSnapshot*)out;
    snap->magic = INF_SNAPSHOT_MAGIC;
    snap->version = INF_SNAPSHOT_VERSION;
    snap->state = *(InfernoState*)state;
}

static void inf_restore(EncounterState* state, const void* data, size_t n) {
    inf_build_npc_stats();
    if (n < 2 * sizeof(uint32_t) || n > sizeof(InfSnapshot)) {
        fprintf(stderr,
                "inf_restore: bad snapshot size %zu (expected <= %zu)\n",
                n, sizeof(InfSnapshot));
        abort();
    }
    InfSnapshot snap;
    memset(&snap, 0, sizeof(snap));
    memcpy(&snap, data, n);
    if (snap.magic != INF_SNAPSHOT_MAGIC || snap.version != INF_SNAPSHOT_VERSION) {
        fprintf(stderr,
                "inf_restore: bad magic/version (got 0x%08x v%u, want 0x%08x v%u)\n",
                snap.magic, snap.version,
                INF_SNAPSHOT_MAGIC, INF_SNAPSHOT_VERSION);
        abort();
    }

    InfernoState* dst = (InfernoState*)state;

    InfLiveRestoreFields live_fields = inf_capture_live_restore_fields(dst);

    *dst = snap.state;

    inf_apply_live_restore_fields(dst, live_fields);
}


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
    .step_human_commands = inf_step_human_commands,

    .snapshot_size = inf_snapshot_size,
    .snapshot = inf_snapshot,
    .restore = inf_restore,

    .cell_key_size = inf_cell_key_size,
    .write_cell_key = inf_write_cell_key,
    .progress_score = inf_progress_score,

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

    .translate_human_input = inf_translate_human_input,
    .is_human_targetable_npc_slot = inf_is_human_targetable_npc_slot,
    .head_move = INF_HEAD_MOVE,
    .head_prayer = INF_HEAD_PRAYER,
    .head_target = INF_HEAD_TARGET,
};

__attribute__((constructor))
static void inf_register(void) {
    encounter_register(&ENCOUNTER_INFERNO);
}

#endif /* ENCOUNTER_INFERNO_H */
