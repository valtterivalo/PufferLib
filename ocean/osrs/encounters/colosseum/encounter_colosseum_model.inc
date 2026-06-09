#line 76 "encounter_colosseum.h"
/**
 * @file encounter_colosseum_model.inc
 * @brief Colosseum data model: arena, NPC roster, wave defs, state structs.
 *
 * NPC combat stats are the canonical COLO_NPC_BASE table (refs/osrs-dps-calc,
 * cross-checked vs wiki) merged with a behavior overlay by col_build_npc_stats().
 * The wave roster, modifiers, and Sol boss sub-state are later phases.
 */

#define COLO_ARENA_MIN_X    0
#define COLO_ARENA_MAX_X    33
#define COLO_ARENA_MIN_Y    0
#define COLO_ARENA_MAX_Y    33
#define COLO_ARENA_WIDTH    (COLO_ARENA_MAX_X - COLO_ARENA_MIN_X + 1)   /* 34 */
#define COLO_ARENA_HEIGHT   (COLO_ARENA_MAX_Y - COLO_ARENA_MIN_Y + 1)  /* 34 */

#define COLO_PLAYER_START_X 17
#define COLO_PLAYER_START_Y 5

#define COLO_MAX_TICKS      9000   /* 90 minutes at 0.6s/tick */
#define COLO_NUM_WAVES      12
#define COLO_WAVE_BOSS      (COLO_NUM_WAVES - 1)  /* wave index 11 = Sol Heredit */
#define COLO_START_READY_TICKS 6
#define COLO_REINFORCEMENT_TICKS 66   /* 40s reinforcement cadence */
#define COLO_NUM_ACTION_HEADS 9

/* max active NPCs at once + obs slot count (one obs slot per active NPC) */
#define COLO_MAX_NPCS 24
#define COLO_OBS_NPCS 12

/* NPC spawn positions around the arena gates (shuffled per wave) */
#define COLO_NUM_SPAWN_POS 8
static const int COLO_SPAWN_POS[COLO_NUM_SPAWN_POS][2] = {
    {4, 4}, {29, 4}, {4, 29}, {29, 29},
    {17, 4}, {4, 17}, {29, 17}, {17, 29},
};

/* north/south gate tiles reinforcements stream in from; the nearer one to the
   player by y is chosen at overrun time (col_spawn_reinforcements). */
#define COLO_GATE_X       17
#define COLO_GATE_NORTH_Y 4
#define COLO_GATE_SOUTH_Y 29

#define COLO_SOL_HEREDIT_X 17
#define COLO_SOL_HEREDIT_Y 17

/* Sol Heredit boss arena: a 16x16 box centred on the 5x5 boss footprint
   ([17,21] x [17,21], centre ~ (19,19)). Player + boss are clamped to this box
   during wave 12 (col_sol_clamp_to_boss_arena). The four pillars sit one tile in
   from each corner and block line of sight for the rotating crystal's spheres.
   Dims are UNCONFIRMED in the wiki; 16x16 is the documented arena size. */
#define COLO_BOSS_ARENA_MIN_X 12
#define COLO_BOSS_ARENA_MIN_Y 12
#define COLO_BOSS_ARENA_MAX_X 27
#define COLO_BOSS_ARENA_MAX_Y 27
#define COLO_BOSS_ARENA_SIZE  (COLO_BOSS_ARENA_MAX_X - COLO_BOSS_ARENA_MIN_X + 1)  /* 16 */

/* player melee-start tile for the boss wave: south-centre of the boss arena,
   in cardinal contact range of Sol's south edge once the player steps up. */
#define COLO_BOSS_PLAYER_START_X 19
#define COLO_BOSS_PLAYER_START_Y 26

/* four pillars, one tile in from each boss-arena corner (1x1 LoS blockers). */
#define COLO_BOSS_NUM_PILLARS 4
static const int COLO_BOSS_PILLARS[COLO_BOSS_NUM_PILLARS][2] = {
    {COLO_BOSS_ARENA_MIN_X + 2, COLO_BOSS_ARENA_MIN_Y + 2},
    {COLO_BOSS_ARENA_MAX_X - 2, COLO_BOSS_ARENA_MIN_Y + 2},
    {COLO_BOSS_ARENA_MIN_X + 2, COLO_BOSS_ARENA_MAX_Y - 2},
    {COLO_BOSS_ARENA_MAX_X - 2, COLO_BOSS_ARENA_MAX_Y - 2},
};


/** OSRS cache NPC definition ids, used by the renderer to look up models.
    Real Fortis Colosseum cache ids (exported via tools/export_colosseum_npcs.py). */
static const int COLO_NPC_DEF_IDS[COLO_NUM_NPC_TYPES] = {
    [COLO_FREMENNIK_BERSERKER] = 12816,
    [COLO_FREMENNIK_ARCHER]    = 12814,
    [COLO_FREMENNIK_SEER]      = 12815,
    [COLO_SERPENT_SHAMAN]      = 12811,
    [COLO_JAGUAR_WARRIOR]      = 12810,
    [COLO_JAVELIN_COLOSSUS]    = 12817,
    [COLO_SHOCKWAVE_COLOSSUS]  = 12819,
    [COLO_MINOTAUR]            = 12812,
    [COLO_MANTICORE]           = 12818,
    [COLO_SOL_HEREDIT]         = 12821,
};

static const char* colo_npc_type_name(int type) {
    switch (type) {
        case COLO_FREMENNIK_BERSERKER: return "Fremennik Berserker";
        case COLO_FREMENNIK_ARCHER:    return "Fremennik Archer";
        case COLO_FREMENNIK_SEER:      return "Fremennik Seer";
        case COLO_SERPENT_SHAMAN:      return "Serpent Shaman";
        case COLO_JAGUAR_WARRIOR:      return "Jaguar Warrior";
        case COLO_JAVELIN_COLOSSUS:    return "Javelin Colossus";
        case COLO_SHOCKWAVE_COLOSSUS:  return "Shockwave Colossus";
        case COLO_MINOTAUR:            return "Minotaur";
        case COLO_MANTICORE:           return "Manticore";
        case COLO_SOL_HEREDIT:         return "Sol Heredit";
        default: return "unknown";
    }
}

/** resolved per-type combat stats, populated at startup by col_build_npc_stats().
    accuracy is driven by levels + bonuses through the shared NPC roll; damage is
    driven by the Jagex-handset max_hit, never recomputed from levels. */
typedef struct {
    int hp;
    int attack_speed;
    int attack_range;
    int size;
    AttackStyle default_style;
    MeleeStyle melee_style;
    int can_melee;

    int att_level, str_level, def_level, range_level, magic_level;
    int melee_att_bonus, range_att_bonus, magic_att_bonus;
    int melee_str_bonus, ranged_str_bonus;
    int stab_def, slash_def, crush_def, magic_def_bonus, ranged_def_bonus;

    int max_hit;         /* Jagex handset max hit for the default attack */
    AttackStyle player_style_that_max_hits;  /* RPS weakness, ATTACK_STYLE_NONE = none */
    int stun_on_spawn;
    int can_move;
} ColoNpcStats;

/** encounter-specific behavior overlay merged on top of COLO_NPC_BASE stats. */
typedef struct {
    int attack_range;
    AttackStyle default_style;
    MeleeStyle melee_style;
    int can_melee;
    AttackStyle player_style_that_max_hits;
    int stun_on_spawn;
    int can_move;
} ColoNpcOverlay;

/** Canonical Fortis Colosseum NPC combat stats (refs/osrs-dps-calc, cross-checked
    vs OSRS wiki). Self-contained: the shared combat fns take plain ints, so
    Colosseum needs no MONSTER_DATABASE entry. Field order follows MonsterStats. */
static const MonsterStats COLO_NPC_BASE[COLO_NUM_NPC_TYPES] = {
    [COLO_FREMENNIK_BERSERKER] = { .npc_id=12816, .name="Fremennik berserker",
        .hp=48, .att_level=110,.str_level=110,.def_level=80,.magic_level=110,.range_level=110,
        .attack_speed=6,.size=1,.max_hit=29,
        .melee_att_bonus=150,.melee_str_bonus=90,.magic_att_bonus=150,.magic_str_bonus=0,.range_att_bonus=0,.ranged_str_bonus=0,
        .stab_def=50,.slash_def=50,.crush_def=50,.magic_def=0,.ranged_def=75 },
    [COLO_FREMENNIK_ARCHER] = { .npc_id=12814, .name="Fremennik archer",
        .hp=50, .att_level=110,.str_level=110,.def_level=80,.magic_level=110,.range_level=110,
        .attack_speed=6,.size=1,.max_hit=14,
        .melee_att_bonus=150,.melee_str_bonus=150,.magic_att_bonus=150,.magic_str_bonus=0,.range_att_bonus=150,.ranged_str_bonus=10,
        .stab_def=0,.slash_def=0,.crush_def=0,.magic_def=75,.ranged_def=50 },
    [COLO_FREMENNIK_SEER] = { .npc_id=12815, .name="Fremennik seer",
        .hp=50, .att_level=110,.str_level=110,.def_level=80,.magic_level=110,.range_level=110,
        .attack_speed=6,.size=1,.max_hit=12,
        .melee_att_bonus=0,.melee_str_bonus=0,.magic_att_bonus=150,.magic_str_bonus=0,.range_att_bonus=0,.ranged_str_bonus=0,
        .stab_def=50,.slash_def=50,.crush_def=50,.magic_def=30,.ranged_def=0 },
    [COLO_SERPENT_SHAMAN] = { .npc_id=12811, .name="Serpent shaman",
        .hp=125, .att_level=100,.str_level=90,.def_level=90,.magic_level=220,.range_level=160,
        .attack_speed=5,.size=1,.max_hit=28,
        .melee_att_bonus=0,.melee_str_bonus=0,.magic_att_bonus=50,.magic_str_bonus=15,.range_att_bonus=0,.ranged_str_bonus=0,
        .stab_def=30,.slash_def=30,.crush_def=30,.magic_def=15,.ranged_def=50 },
    [COLO_JAGUAR_WARRIOR] = { .npc_id=12810, .name="Jaguar warrior",
        .hp=125, .att_level=200,.str_level=330,.def_level=125,.magic_level=100,.range_level=160,
        .attack_speed=5,.size=2,.max_hit=47,
        .melee_att_bonus=0,.melee_str_bonus=25,.magic_att_bonus=0,.magic_str_bonus=0,.range_att_bonus=0,.ranged_str_bonus=0,
        .stab_def=30,.slash_def=30,.crush_def=30,.magic_def=15,.ranged_def=45 },
    [COLO_JAVELIN_COLOSSUS] = { .npc_id=12817, .name="Javelin Colossus",
        .hp=220, .att_level=200,.str_level=300,.def_level=190,.magic_level=225,.range_level=360,
        .attack_speed=5,.size=3,.max_hit=48,
        .melee_att_bonus=0,.melee_str_bonus=0,.magic_att_bonus=0,.magic_str_bonus=0,.range_att_bonus=25,.ranged_str_bonus=20,
        .stab_def=15,.slash_def=15,.crush_def=15,.magic_def=20,.ranged_def=30 },
    [COLO_SHOCKWAVE_COLOSSUS] = { .npc_id=12819, .name="Shockwave Colossus",
        .hp=125, .att_level=120,.str_level=190,.def_level=150,.magic_level=350,.range_level=220,
        .attack_speed=5,.size=3,.max_hit=56,
        .melee_att_bonus=0,.melee_str_bonus=0,.magic_att_bonus=55,.magic_str_bonus=35,.range_att_bonus=0,.ranged_str_bonus=0,
        .stab_def=15,.slash_def=15,.crush_def=15,.magic_def=5,.ranged_def=35 },
    [COLO_MINOTAUR] = { .npc_id=12812, .name="Minotaur",
        .hp=225, .att_level=300,.str_level=360,.def_level=190,.magic_level=250,.range_level=120,
        .attack_speed=5,.size=3,.max_hit=74,
        .melee_att_bonus=15,.melee_str_bonus=64,.magic_att_bonus=0,.magic_str_bonus=0,.range_att_bonus=0,.ranged_str_bonus=0,
        .stab_def=0,.slash_def=0,.crush_def=0,.magic_def=0,.ranged_def=12 },
    [COLO_MANTICORE] = { .npc_id=12818, .name="Manticore",
        .hp=250, .att_level=300,.str_level=300,.def_level=250,.magic_level=300,.range_level=350,
        .attack_speed=10,.size=3,.max_hit=36, /* per-style rng36/mag31/melee31 set in barrage logic */
        .melee_att_bonus=0,.melee_str_bonus=0,.magic_att_bonus=0,.magic_str_bonus=0,.range_att_bonus=0,.ranged_str_bonus=0,
        .stab_def=0,.slash_def=0,.crush_def=0,.magic_def=10,.ranged_def=25 },
    [COLO_SOL_HEREDIT] = { .npc_id=12821, .name="Sol Heredit",
        .hp=1500, .att_level=350,.str_level=400,.def_level=200,.magic_level=300,.range_level=350,
        .attack_speed=5,.size=5,.max_hit=44, /* AoE/grapple 44; TP combos handled in boss logic */
        .melee_att_bonus=250,.melee_str_bonus=5,.magic_att_bonus=0,.magic_str_bonus=0,.range_att_bonus=150,.ranged_str_bonus=5,
        .stab_def=65,.slash_def=5,.crush_def=30,.magic_def=750,.ranged_def=825 },
};

/** per-style barrage max hits for the Manticore (mechanics-to-code.md). */
#define COLO_MANTICORE_MAX_HIT_RANGED 36
#define COLO_MANTICORE_MAX_HIT_MAGIC  31
#define COLO_MANTICORE_MAX_HIT_MELEE  31

/** encounter behavior overlay. attack_range/default_style/RPS weakness per type;
    Sol can_move=0 (boss logic drives it later). magic damage is the handset
    max_hit, so no base/pct fields are needed here. */
static const ColoNpcOverlay COLO_NPC_OVERLAY[COLO_NUM_NPC_TYPES] = {
    [COLO_FREMENNIK_BERSERKER] = { .attack_range=1,  .default_style=ATTACK_STYLE_MELEE,  .melee_style=MELEE_STYLE_STAB,  .can_melee=1, .player_style_that_max_hits=ATTACK_STYLE_MAGIC,  .can_move=1 },
    [COLO_FREMENNIK_ARCHER]    = { .attack_range=10, .default_style=ATTACK_STYLE_RANGED, .melee_style=MELEE_STYLE_STAB,  .can_melee=0, .player_style_that_max_hits=ATTACK_STYLE_MELEE,  .can_move=1 },
    [COLO_FREMENNIK_SEER]      = { .attack_range=10, .default_style=ATTACK_STYLE_MAGIC,  .melee_style=MELEE_STYLE_STAB,  .can_melee=0, .player_style_that_max_hits=ATTACK_STYLE_RANGED, .can_move=1 },
    [COLO_SERPENT_SHAMAN]      = { .attack_range=10, .default_style=ATTACK_STYLE_MAGIC,  .melee_style=MELEE_STYLE_STAB,  .can_melee=0, .player_style_that_max_hits=ATTACK_STYLE_NONE,  .can_move=1 },
    [COLO_JAGUAR_WARRIOR]      = { .attack_range=1,  .default_style=ATTACK_STYLE_MELEE,  .melee_style=MELEE_STYLE_SLASH, .can_melee=1, .player_style_that_max_hits=ATTACK_STYLE_NONE,  .can_move=1 },
    [COLO_JAVELIN_COLOSSUS]    = { .attack_range=15, .default_style=ATTACK_STYLE_RANGED, .melee_style=MELEE_STYLE_STAB,  .can_melee=0, .player_style_that_max_hits=ATTACK_STYLE_NONE,  .can_move=1 },
    [COLO_SHOCKWAVE_COLOSSUS]  = { .attack_range=15, .default_style=ATTACK_STYLE_MAGIC,  .melee_style=MELEE_STYLE_STAB,  .can_melee=0, .player_style_that_max_hits=ATTACK_STYLE_NONE,  .can_move=1 },
    [COLO_MINOTAUR]            = { .attack_range=1,  .default_style=ATTACK_STYLE_MELEE,  .melee_style=MELEE_STYLE_CRUSH, .can_melee=1, .player_style_that_max_hits=ATTACK_STYLE_NONE,  .can_move=1 },
    [COLO_MANTICORE]           = { .attack_range=15, .default_style=ATTACK_STYLE_RANGED, .melee_style=MELEE_STYLE_STAB,  .can_melee=1, .player_style_that_max_hits=ATTACK_STYLE_NONE,  .can_move=1 },
    [COLO_SOL_HEREDIT]         = { .attack_range=1,  .default_style=ATTACK_STYLE_MELEE,  .melee_style=MELEE_STYLE_SLASH, .can_melee=1, .player_style_that_max_hits=ATTACK_STYLE_NONE,  .can_move=0 },
};

/** populated at startup by col_build_npc_stats(). */
static ColoNpcStats COLO_NPC_STATS[COLO_NUM_NPC_TYPES];

/**
 * Merge COLO_NPC_BASE + COLO_NPC_OVERLAY into COLO_NPC_STATS.
 *
 * Accuracy stays level + bonus driven (shared NPC roll); damage uses the handset
 * max_hit. No max-hit formula is run for these NPCs.
 */
static void col_build_npc_stats(void) {
    for (int i = 0; i < COLO_NUM_NPC_TYPES; i++) {
        const MonsterStats* m = &COLO_NPC_BASE[i];
        const ColoNpcOverlay* o = &COLO_NPC_OVERLAY[i];
        ColoNpcStats* s = &COLO_NPC_STATS[i];

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
        s->magic_def_bonus = m->magic_def;
        s->ranged_def_bonus = m->ranged_def;
        s->max_hit         = m->max_hit;

        s->attack_range    = o->attack_range;
        s->default_style   = o->default_style;
        s->melee_style     = o->melee_style;
        s->can_melee       = o->can_melee;
        s->player_style_that_max_hits = o->player_style_that_max_hits;
        s->stun_on_spawn   = o->stun_on_spawn;
        s->can_move        = o->can_move;
    }
}


/** wiki-exact Fortis Colosseum roster: W8/W11 start 7 NPCs, so 8 leaves headroom. */
#define COLO_MAX_NPCS_PER_WAVE 8
/** at most Jaguar + Serpent (W4-6) or Minotaur + Serpent (W10-11) reinforce per overrun. */
#define COLO_MAX_REINFORCE_PER_WAVE 2

/** one wave's full starting roster plus its single-overrun reinforcement set.
    types/count list every NPC that spawns at wave start (warband listed
    explicitly for waves 1-11). reinforce/reinforce_count is the species set
    spawned once if the wave is not cleared within COLO_REINFORCEMENT_TICKS. */
typedef struct {
    uint8_t types[COLO_MAX_NPCS_PER_WAVE];
    int count;
    uint8_t reinforce[COLO_MAX_REINFORCE_PER_WAVE];
    int reinforce_count;
} ColoWaveDef;

/** wiki-exact 12-wave script. Waves 1-11 (indices 0-10) open with the Fremennik
    warband (berserker + archer + seer) listed explicitly; wave 12 (index 11) is
    Sol Heredit alone. Reinforcements: W1-3 Jaguar, W4-6 Jaguar + Serpent,
    W7-9 Minotaur, W10-11 Minotaur + Serpent, W12 none. */
static const ColoWaveDef COLO_WAVES[COLO_NUM_WAVES] = {
    #define BZ COLO_FREMENNIK_BERSERKER
    #define AR COLO_FREMENNIK_ARCHER
    #define SE COLO_FREMENNIK_SEER
    #define SH COLO_SERPENT_SHAMAN
    #define JG COLO_JAGUAR_WARRIOR
    #define JV COLO_JAVELIN_COLOSSUS
    #define SW COLO_SHOCKWAVE_COLOSSUS
    #define MT COLO_MINOTAUR
    #define MC COLO_MANTICORE
    #define SOL COLO_SOL_HEREDIT
    #define W(start, reinf) { \
        .types = { COLO_WAVE_UNPACK start }, \
        .count = sizeof((uint8_t[]){ COLO_WAVE_UNPACK start }), \
        .reinforce = { COLO_WAVE_UNPACK reinf }, \
        .reinforce_count = sizeof((uint8_t[]){ COLO_WAVE_UNPACK reinf }) }
    #define COLO_WAVE_UNPACK(...) __VA_ARGS__

    [0]  = W((BZ, AR, SE, SH),              (JG)),
    [1]  = W((BZ, AR, SE, SH, JV),          (JG)),
    [2]  = W((BZ, AR, SE, SH, JV, JV),      (JG)),
    [3]  = W((BZ, AR, SE, SH, MC),          (JG, SH)),
    [4]  = W((BZ, AR, SE, SH, JV, MC),      (JG, SH)),
    [5]  = W((BZ, AR, SE, SH, JV, JV, MC),  (JG, SH)),
    [6]  = W((BZ, AR, SE, JV, MC, SW),      (MT)),
    [7]  = W((BZ, AR, SE, JV, JV, MC, SW),  (MT)),
    [8]  = W((BZ, AR, SE, JV, MC, MC),      (MT)),
    [9]  = W((BZ, AR, SE, JV, JV, MC, MC),  (MT, SH)),
    [10] = W((BZ, AR, SE, JV, MC, MC, SW),  (MT, SH)),
    [11] = W((SOL),                         ()),

    #undef BZ
    #undef AR
    #undef SE
    #undef SH
    #undef JG
    #undef JV
    #undef SW
    #undef MT
    #undef MC
    #undef SOL
    #undef W
    #undef COLO_WAVE_UNPACK
};


/** active-modifier set. Colosseum offers 3 modifiers to draft between waves;
    each chosen modifier persists and carries a tier. */
#define COLO_NUM_MODIFIERS 16
#define COLO_MODIFIER_DRAFT_OPTIONS 3

/** highest wave (0-based) after whose clear a modifier draft is offered. Waves
    1..COLO_MODIFIER_LAST_WAVE clear into a draft; the spec flags four modifiers as
    "not offered after wave 11", and leaves whether ANY choice is offered after
    wave 11 UNCONFIRMED. We offer through wave 11 (index 10) so the last meaningful
    choice lands before the boss; one-line tunable. */
#define COLO_MODIFIER_LAST_WAVE 10

/** per-modifier max tier: single-tier modifiers cap at 1, I/II/III cap at 3. */
static const uint8_t COLO_MODIFIER_MAX_TIER[COLO_NUM_REAL_MODIFIERS] = {
    [COLO_MOD_BEES]        = 3,
    [COLO_MOD_BLASPHEMY]   = 3,
    [COLO_MOD_DOOM]        = 3,
    [COLO_MOD_DYNAMIC_DUO] = 1,
    [COLO_MOD_FRAILTY]     = 3,
    [COLO_MOD_MANTIMAYHEM] = 3,
    [COLO_MOD_MYOPIA]      = 3,
    [COLO_MOD_REENTRY]     = 3,
    [COLO_MOD_RED_FLAG]    = 1,
    [COLO_MOD_RELENTLESS]  = 3,
    [COLO_MOD_SOLARFLARE]  = 3,
    [COLO_MOD_QUARTET]     = 1,
    [COLO_MOD_TOTEMIC]     = 1,
    [COLO_MOD_VOLATILITY]  = 3,
};

/** modifiers whose affected entities only spawn before the boss, so the spec
    flags them "not offered after wave 11". 1 = pre-boss-only. */
static const uint8_t COLO_MODIFIER_PRE_BOSS_ONLY[COLO_NUM_REAL_MODIFIERS] = {
    [COLO_MOD_DYNAMIC_DUO] = 1,
    [COLO_MOD_MANTIMAYHEM] = 1,
    [COLO_MOD_REENTRY]     = 1,
    [COLO_MOD_RED_FLAG]    = 1,
};

typedef struct {
    uint32_t active_mask;                  /* bit i set = modifier i active */
    uint8_t tier[COLO_NUM_MODIFIERS];      /* per-modifier tier (0 = inactive) */
    int draft_options[COLO_MODIFIER_DRAFT_OPTIONS];  /* modifier ids offered, -1 = none */
    int draft_pending;                     /* 1 while a between-wave draft awaits a pick */
    int draft_timer;                       /* ticks left to act on the open draft */
} ColoModifierState;


/** Bees! hazard: a roaming swarm converging on the player, moving every
    COLO_BEE_MOVE_INTERVAL ticks and dealing up to COLO_BEE_MAX_POISON unblockable
    damage on contact. Tier I/II/III field 1/2/3 swarms. A swarm dies in one hit but
    respawns; we model the kill+respawn as a move-only entity (no separate HP). */
#define COLO_MAX_BEE_SWARMS 3
#define COLO_BEE_MOVE_INTERVAL 12   /* swarm steps one tile every 12 ticks */
#define COLO_BEE_MAX_POISON 10      /* up to 10 unblockable contact damage/tick */

typedef struct {
    int active;
    int x, y;
    int move_timer;   /* ticks until the next step toward the player */
} ColoBeeSwarm;

/** Totemic hazard: when an NPC first drops to <=50% HP a healing totem spawns near
    it, healing that NPC ~COLO_TOTEM_HEAL_PCT% of its max HP every
    COLO_TOTEM_HEAL_INTERVAL ticks until destroyed. The totem is destroyed by
    standing adjacent to it (modelled as the player being within 1 tile), matching
    "until destroyed" without adding a separate combat target. UNCONFIRMED cadence. */
#define COLO_MAX_TOTEMS COLO_MAX_NPCS
#define COLO_TOTEM_HEAL_PCT 30        /* heal ~30% of the NPC's max HP per pulse */
#define COLO_TOTEM_HEAL_INTERVAL 5    /* ticks between heal pulses (UNCONFIRMED) */

typedef struct {
    int active;
    int x, y;
    int npc_slot;     /* the NPC this totem heals (-1 if that NPC has since died) */
    int heal_timer;   /* ticks until the next heal pulse */
} ColoTotem;

/** Solarflare hazard: a damaging orb circling the four boss pillars. Tiers change
    cadence/damage and T3 disables prayer on hit. We reuse the boss pillar ring as
    the orb path on every wave (the spec says "circles the pillars"); pre-boss the
    ring is the four arena spawn corners so the orb still sweeps the play space.
    UNCONFIRMED higher-tier damage; constants below are the modelled values. */
#define COLO_SOLARFLARE_CORNER_PAUSE 7   /* T1 pauses 7 ticks at each corner */
#define COLO_SOLARFLARE_MAX_HIT_T1 12    /* contact damage, tier I */
#define COLO_SOLARFLARE_MAX_HIT_T2 18    /* tier II "more damage" (UNCONFIRMED) */
#define COLO_SOLARFLARE_MAX_HIT_T3 24    /* tier III "increased damage" (UNCONFIRMED) */

typedef struct {
    int active;
    int x, y;
    int step;          /* position index around the orb ring perimeter */
    int move_timer;    /* ticks until the next step (cadence is tier-driven) */
    int pause_timer;   /* T1 corner pause countdown */
} ColoSolarflareOrb;

/** Doom: stacks gained per damage instance; reaching the tier cap (15/10/5) kills
    the player. Stacks reset between waves. */
static const int COLO_DOOM_CAP[4] = { 0, 15, 10, 5 };  /* indexed by tier (1..3) */

/** Volatility death explosion radius (tiles beyond the NPC footprint) by tier, and
    its contact max hit. T3 additionally leaves a temporary molten pool. */
#define COLO_VOLATILITY_MAX_HIT 25       /* explosion contact damage (UNCONFIRMED) */
#define COLO_VOLATILITY_POOL_TICKS 10    /* T3 molten pool lifetime */

/** general-arena molten pool (Reentry / Volatility T3 on waves 1-11): contact
    damage and the sentinel marking a permanent pool (Reentry T2, until wave end). */
#define COLO_MOLTEN_MAX_HIT 12
#define COLO_POOL_PERMANENT (-1)
#define COLO_REENTRY_POOL_TICKS 8        /* Reentry T1 temporary pool lifetime */

/** Relentless: defence-bypass fraction (percent) and flat max-hit bonus by tier. */
static const int COLO_RELENTLESS_DEF_BYPASS_PCT[4] = { 0, 33, 66, 100 };
static const int COLO_RELENTLESS_MAX_HIT_BONUS[4]  = { 0, 1, 3, 6 };

/** Blasphemy: prayer drained as a percent of damage taken, by tier. */
static const int COLO_BLASPHEMY_DRAIN_PCT[4] = { 0, 20, 40, 60 };

/** Frailty: max-HP reduction percent by tier (T1 also disables overheal). */
static const int COLO_FRAILTY_HP_CUT_PCT[4] = { 0, 10, 20, 40 };

/** Myopia: player attack-range reduction (tiles) by tier. */
static const int COLO_MYOPIA_RANGE_CUT[4] = { 0, 2, 4, 6 };

/** Mantimayhem T2 venom: poison applied to the player on an unprotected manticore
    hit, decaying by COLO_VENOM_DECAY each application until it expires. */
#define COLO_VENOM_START 6     /* initial venom damage per tick */
#define COLO_VENOM_DECAY 1     /* venom drops by this each tick it deals damage */
#define COLO_VENOM_INTERVAL 5  /* ticks between venom damage applications */


/** Sol Heredit AoE attack ids. The fight alternates spear/shield; if the same
    style lands twice in a row the second uses the other sub-pattern, and the
    fight always opens with COLO_SOL_AOE_SPEAR1. Footprints + dodge in boss.inc. */
typedef enum {
    COLO_SOL_AOE_NONE = 0,
    COLO_SOL_AOE_SPEAR1,   /* 6x6 dust under boss; dodge = step off centre/edge */
    COLO_SOL_AOE_SPEAR2,   /* 6x6 dust;            dodge = step to off-centre tile */
    COLO_SOL_AOE_SHIELD1,  /* 7x7 with a 1-tile gap; dodge = back 1 */
    COLO_SOL_AOE_SHIELD2,  /* 9x9;                   dodge = back 2 */
} ColoSolAoeAttack;

/** which family the last AoE belonged to, for the spear/shield alternation. */
typedef enum {
    COLO_SOL_STYLE_NONE = 0,
    COLO_SOL_STYLE_SPEAR,
    COLO_SOL_STYLE_SHIELD,
} ColoSolAttackStyle;

/** Sol Heredit boss sub-state. Covers the documented phases, AoE rotation,
    triple-parry combo, grapple, rotating-crystal spheres, and enrage molten sand.
    combo_queue/combo_len/combo_cursor + parry_* describe the active triple parry;
    the actual per-hit block runs through the player's deferred prayer check. */
#define COLO_SOL_COMBO_QUEUE_MAX 8
#define COLO_SOL_HAZARD_TILES_MAX 32
#define COLO_SOL_SPHERE_QUEUE_MAX 8
#define COLO_SOL_NUM_PHASES 6  /* >90 / <90 / <75 / <50 / <25 / <10 (enrage) */
#define COLO_SOL_HP_MAX 1500

/** one in-flight light sphere from the rotating crystal: marks a tile and lands
    COLO_SOL_SPHERE_DELAY ticks later for up to COLO_SOL_SPHERE_MAX_HIT typeless. */
typedef struct {
    int active;
    int tile_x, tile_y;
    int damage;
    int ticks_remaining;
} ColoSolSphere;

typedef struct {
    int started;               /* 1 once the boss machine has been initialised */
    int phase;                 /* 0..5 HP-threshold phase (>90/<90/<75/<50/<25/<10) */
    int boss_idx;              /* NPC slot of the live Sol, refreshed each tick (-1) */
    int attack_timer;          /* ticks until Sol's next AoE / triple-parry decision */

    /* AoE rotation */
    int aoe_attack;            /* ColoSolAoeAttack currently telegraphed (or NONE) */
    int aoe_resolve_timer;     /* ticks until the telegraphed dust resolves */
    int last_style;            /* ColoSolAttackStyle of the previous AoE */
    int last_attack;           /* ColoSolAoeAttack of the previous AoE */

    /* triple parry (available <90% HP): obs telemetry; hits run as pending hits */
    int combo_queue[COLO_SOL_COMBO_QUEUE_MAX];  /* per-hit misprayed damage */
    int combo_len;
    int combo_cursor;          /* index of the next un-landed hit */
    int parry_sequence_active; /* 1 while the triple-parry sequence runs */
    int parry_ticks_remaining; /* ticks until the next hit in the combo lands */
    int parry_combo_b;         /* 0 = combo A (15/25/35), 1 = combo B (15/30/45) */

    /* grapple (available <75% HP): clicked-slot parry, hook in boss.inc */
    int grapple_active;        /* 1 while a grapple is in progress */
    int grapple_body_slot;     /* equipment slot the player must click to parry */
    int grapple_timer;         /* 4-tick grapple resolution window (4..1, 0 = lands) */
    int grapple_resolved;      /* 1 once parried/failed this grapple (latches result) */
    int next_attack_guaranteed_max; /* set by a perfect parry: next player hit maxes */
    int guaranteed_max_ticks;  /* ticks left on the guaranteed-max window (<=5) */

    /* phase-transition hazards */
    int crystal_active;        /* 1 once the rotating crystal is up (phase >= 2) */
    int crystal_fire_timer;    /* ticks until the crystal fires its next sphere */
    int crystal_step;          /* crystal position index around the arena edge */
    int beam_ticks_remaining;  /* >0 while the 6 light-beam telegraphs are showing */
    int beam_count;            /* number of active light-beam telegraphs (<= 6) */
    ColoSolSphere spheres[COLO_SOL_SPHERE_QUEUE_MAX];

    /* enrage molten sand (<10% HP) + skyfall reentry pools share this tile set */
    int hazard_tile_x[COLO_SOL_HAZARD_TILES_MAX];
    int hazard_tile_y[COLO_SOL_HAZARD_TILES_MAX];
    int hazard_tile_count;
    int hazard_timer;          /* ticks until the next molten-sand placement */

    /* boss arena bounds, set when wave 12 starts (col_sol_begin_boss_arena). */
    int boss_arena_min_x, boss_arena_min_y;
    int boss_arena_max_x, boss_arena_max_y;
} SolHereditState;

/* Sol timing + damage constants (mechanics-to-code.md "Sol Heredit state machine").
   All damage below is typeless: prayer never reduces it, only dodging/parrying. */
#define COLO_SOL_ATTACK_INTERVAL   5   /* ticks between Sol's AoE / TP decisions */
#define COLO_SOL_AOE_TELEGRAPH     2   /* dodge window before the dust resolves */
#define COLO_SOL_AOE_MAX_HIT       44  /* typeless dust max hit */
#define COLO_SOL_GRAPPLE_MAX_HIT   44  /* typeless grapple-fail max hit */
#define COLO_SOL_GRAPPLE_WINDOW    4   /* ticks to click the called slot */
#define COLO_SOL_PERFECT_MAX_TICKS 5   /* guaranteed-max window after a perfect parry */
#define COLO_SOL_TP_HIT_GAP        3   /* base ticks between triple-parry hits */
#define COLO_SOL_TP_HIT3_GAP_LOW   4   /* 3rd-hit gap once Sol is under 50% HP */
#define COLO_SOL_SPHERE_DELAY      4   /* ticks the crystal sphere takes to land */
#define COLO_SOL_SPHERE_MAX_HIT    75  /* typeless sphere max hit */
#define COLO_SOL_CRYSTAL_INTERVAL  6   /* ticks between crystal sphere fires */
#define COLO_SOL_CRYSTAL_ENRAGE_INTERVAL 2  /* faster sphere cadence at enrage */
#define COLO_SOL_BEAM_COUNT        6   /* light-beam telegraphs per phase transition */
#define COLO_SOL_BEAM_TICKS        4   /* how long the beam telegraphs linger */
#define COLO_SOL_MOLTEN_INTERVAL   3   /* enrage: molten sand spawns every 3 ticks */
#define COLO_SOL_MOLTEN_MAX_HIT    12  /* per-tick contact damage from molten sand */


/** jaguar 3-hit combo state. valid iff type is COLO_JAGUAR_WARRIOR;
    reach it through colo_npc_jaguar(). The 3 hits resolve in one attack cycle, so
    no per-hit cursor persists across ticks. Placeholder field keeps the union
    arm non-empty for future combo extensions. */
typedef struct {
    int unused;
} ColoJaguarState;

/** manticore 3-orb barrage cycle. valid iff type is COLO_MANTICORE; reach it
    through colo_npc_manticore(). cycle_step counts ticks within a barrage:
    -1 = idle (waiting on attack_timer), 0/1/2 = next orb to fire 1 tick apart.
    orb_style holds the resolved per-orb styles (orbs 0&1 are {ranged,magic} in
    random order, orb 2 is always melee). */
typedef struct {
    int cycle_step;
    AttackStyle orb_style[3];
} ColoManticoreState;

/** javelin skyfall cadence. valid iff type is COLO_JAVELIN_COLOSSUS; reach it
    through colo_npc_javelin(). skyfall fires on every 5th throw; the marked tile
    lands COLO_JAVELIN_SKYFALL_DELAY ticks later, ignoring Protect-from-Missiles. */
typedef struct {
    int attack_count;          /* throws since spawn; every 5th is a skyfall */
    int skyfall_pending;       /* 1 while a marked tile is in flight */
    int skyfall_tile_x, skyfall_tile_y;
    int skyfall_timer;         /* ticks until the marked tile lands */
    int skyfall_damage;        /* damage rolled at mark time */
} ColoJavelinState;


/** single struct for every Colosseum monster; `type` is the discriminant.
    flat fields are read in generic per-tick loops; type-specific state lives in
    a tagged union reached only through the asserting colo_npc_*() accessors. */
typedef struct {
    ColoNpcType type;
    int x, y;
    int hp, max_hp;
    int size;
    uint32_t render_id;
    int attack_timer;
    AttackStyle attack_style;
    int active;
    int target_x, target_y;
    int stun_timer;
    int frozen_ticks;

    /* type-specific state keyed on `type`; reach only via colo_npc_*(). */
    union {
        ColoJaguarState jaguar;       /* COLO_JAGUAR_WARRIOR */
        ColoManticoreState manticore; /* COLO_MANTICORE */
        ColoJavelinState javelin;     /* COLO_JAVELIN_COLOSSUS */
    } type_state;

    EncounterPendingHitQueue pending_hits;

    int death_ticks;       /* death-animation linger, >0 = dying */
    int aggro_target;      /* -1 = player, >= 0 = NPC index */

    /* per-tick render flags (cleared at start of each tick) */
    int attacked_this_tick;
    AttackStyle attack_style_this_tick;
    int attack_visual_target;
    int moved_this_tick;
    int hit_landed_this_tick;
    int hit_damage;
    int hit_was_successful_this_tick;
    int hit_spell_type;
} ColoNPC;

static inline ColoJaguarState* colo_npc_jaguar(ColoNPC* npc) {
    assert(npc->type == COLO_JAGUAR_WARRIOR);
    return &npc->type_state.jaguar;
}

static inline ColoManticoreState* colo_npc_manticore(ColoNPC* npc) {
    assert(npc->type == COLO_MANTICORE);
    return &npc->type_state.manticore;
}

static inline ColoJavelinState* colo_npc_javelin(ColoNPC* npc) {
    assert(npc->type == COLO_JAVELIN_COLOSSUS);
    return &npc->type_state.javelin;
}

/** ticks the marked skyfall tile lands after the javelin throws it. */
#define COLO_JAVELIN_SKYFALL_DELAY 3
/** a ready manticore delays its barrage start while another is mid-barrage. */
#define COLO_MANTICORE_STAGGER_TICKS 5

/** write the spawn-time defaults for npc->type's type_state arm. caller must
    have zeroed the NPC and set npc->type first. */
static void colo_npc_init_type_state(ColoNPC* npc) {
    switch (npc->type) {
        case COLO_JAGUAR_WARRIOR:
            npc->type_state.jaguar = (ColoJaguarState){0};
            break;
        case COLO_MANTICORE:
            npc->type_state.manticore = (ColoManticoreState){
                .cycle_step = -1,
                .orb_style = { ATTACK_STYLE_NONE, ATTACK_STYLE_NONE, ATTACK_STYLE_NONE },
            };
            break;
        case COLO_JAVELIN_COLOSSUS:
            npc->type_state.javelin = (ColoJavelinState){0};
            break;
        default:
            break;
    }
}


typedef enum {
    COLO_GEAR_MELEE = 0,
    COLO_GEAR_RANGED,
    COLO_NUM_WEAPON_SETS
} ColoWeaponSet;

/* melee + ranged loadout arrays (placeholder maxed gear, reused from inferno's items) */
static const uint8_t COLO_MELEE_LOADOUT[NUM_GEAR_SLOTS] = {
    [GEAR_SLOT_HEAD]   = ITEM_MASORI_MASK_F,
    [GEAR_SLOT_CAPE]   = ITEM_DIZANAS_QUIVER,
    [GEAR_SLOT_NECK]   = ITEM_OCCULT_NECKLACE,
    [GEAR_SLOT_WEAPON] = ITEM_KODAI_WAND,
    [GEAR_SLOT_SHIELD] = ITEM_ELYSIAN_SPIRIT_SHIELD,
    [GEAR_SLOT_BODY]   = ITEM_VIRTUS_ROBE_TOP,
    [GEAR_SLOT_LEGS]   = ITEM_VIRTUS_ROBE_BOTTOM,
    [GEAR_SLOT_HANDS]  = ITEM_CONFLICTION_GAUNTLETS,
    [GEAR_SLOT_FEET]   = ITEM_AVERNIC_TREADS,
    [GEAR_SLOT_RING]   = ITEM_VENATOR_RING,
};

static const uint8_t COLO_RANGED_LOADOUT[NUM_GEAR_SLOTS] = {
    [GEAR_SLOT_HEAD]   = ITEM_MASORI_MASK_F,
    [GEAR_SLOT_CAPE]   = ITEM_DIZANAS_QUIVER,
    [GEAR_SLOT_NECK]   = ITEM_NECKLACE_OF_ANGUISH,
    [GEAR_SLOT_AMMO]   = ITEM_DRAGON_ARROWS,
    [GEAR_SLOT_WEAPON] = ITEM_TWISTED_BOW,
    [GEAR_SLOT_SHIELD] = ITEM_NONE,
    [GEAR_SLOT_BODY]   = ITEM_MASORI_BODY_F,
    [GEAR_SLOT_LEGS]   = ITEM_MASORI_CHAPS_F,
    [GEAR_SLOT_HANDS]  = ITEM_ZARYTE_VAMBRACES,
    [GEAR_SLOT_FEET]   = ITEM_AVERNIC_TREADS,
    [GEAR_SLOT_RING]   = ITEM_VENATOR_RING,
};

static const uint8_t* const COLO_LOADOUTS[COLO_NUM_WEAPON_SETS] = {
    COLO_MELEE_LOADOUT,
    COLO_RANGED_LOADOUT,
};


/** Colosseum env-knob struct. Sparse-leaning reward terms; the lead tunes these.
    Dense damage terms stay small (PPO clamps per-step rewards). Death/terminal
    penalty defaults off to avoid suicide-perversity (sparse > dense). */
typedef struct {
    int start_wave;        /* internal wave index (0-based) */
    float damage_reward_coeff;       /* per-point reward for damage dealt to NPCs */
    float boss_damage_reward_coeff;  /* per-point reward for damage dealt to Sol */
    float wave_clear_bonus;          /* added on top of the +1 each wave cleared */
    float boss_phase_bonus;          /* per Sol HP-threshold phase crossed */
    float win_bonus;                 /* on Sol death (wave 12 cleared) */
    float prayer_correct_reward;     /* per correctly-prayed incoming hit */
    float death_penalty_coeff;       /* magnitude of the death reward when enabled */
    int terminal_penalty_enabled;
    int curriculum_agent;
    int modifier_draft_window_ticks; /* ticks a between-wave draft stays open (<=0 = stays until picked) */
} ColConfig;

typedef struct {
    ColConfig config;
    const CollisionMap* collision_map;
    int world_offset_x;
    int world_offset_y;
    Log* log;
    const HumanCommand* human_commands;
    int human_command_count;
    int human_command_mode;
} ColosseumContext;

/** per-tick scratch: reward/event accumulators, fully reset each step by a single
    assignment so a new field is reset for free. */
typedef struct {
    float damage_dealt;
    float damage_boss;
    float damage_received;
    int prayer_correct;
    int attacks_fired;
    int wave_completed;
    int player_moved;
    int player_attacked;
} ColoTickScratch;

/** Colosseum telemetry surfaced through the get_log hook (NOT the shared Log). */
typedef struct {
    float episode_return;
    int episode_length;
    int wave_reached;
    int waves_cleared;
    int win;
    int died;
    float total_damage_dealt;
    float total_damage_received;
    float total_boss_damage;
    int boss_phase_reached;
    int total_npc_kills;
    int total_prayer_correct;
    int total_npc_attacks;
    int modifiers_picked;      /* draft selections made this run (skips excluded) */
    int active_tier_sum;       /* sum of active modifier tiers (run difficulty proxy) */
} ColosseumLog;


typedef struct {
    Player player;

    ColoTickScratch tick_scratch;

    ColoNPC npcs[COLO_MAX_NPCS];
    uint32_t next_npc_render_id;
    int current_obs_slots[COLO_OBS_NPCS];

    /* wave / spawn state */
    int wave;              /* current wave (0-based, 0-11) */
    int wave_spawn_target; /* wave queued to spawn when the inter-wave delay ends */
    int tick;
    int wave_spawn_delay;
    int wave_ready_delay;
    int reinforcement_timer;  /* 40s (66-tick) reinforcement countdown */
    int episode_over;
    ColoOutcome winner;       /* valid only when episode_over is set */

    ColoModifierState modifiers;
    SolHereditState sol;

    /* modifier runtime state (zeroed each reset; a fresh run carries none). */
    int doom_stacks;                       /* Doom: reset to 0 at each wave spawn */
    ColoBeeSwarm bees[COLO_MAX_BEE_SWARMS];
    ColoTotem totems[COLO_MAX_TOTEMS];
    ColoSolarflareOrb solarflare;
    int player_venom;                      /* Mantimayhem T2: current venom damage (0 = none) */
    int player_venom_timer;                /* ticks until the next venom application */
    /* general-arena molten pools (Reentry / Volatility T3) for waves 1-11. Boss
       wave reuses SolHereditState.hazard_tile_*; this list is the non-boss analog.
       permanent pools (Reentry T2) use ticks_remaining = COLO_POOL_PERMANENT. */
    int molten_x[COLO_SOL_HAZARD_TILES_MAX];
    int molten_y[COLO_SOL_HAZARD_TILES_MAX];
    int molten_ticks[COLO_SOL_HAZARD_TILES_MAX];
    int molten_count;

    /* reward bookkeeping */
    float reward;
    float episode_return;
    int last_hit_by_type;     /* NPC type that last damaged the player (-1 = none) */
    int rewarded_boss_phase;  /* highest Sol phase already paid a boss_phase_bonus */

    /* player combat state */
    OsrsInteraction interaction;
    ColoWeaponSet weapon_set;
    EncounterLoadoutStats loadout_stats[COLO_NUM_WEAPON_SETS];

    /* per-tick player attack event for the renderer */
    int player_attack_npc_idx;
    int player_attack_dmg;
    AttackStyle player_attack_style_id;
    EncounterProjectileTiming player_attack_timing;

    /* pending hits on the player from NPC attacks (projectiles in flight) */
    EncounterPendingHitQueue player_pending_hits;

    /* spawn position shuffle buffer */
    int spawn_order[COLO_NUM_SPAWN_POS];

    /* human click-to-move destination (-1 = no dest) */
    int player_dest_x, player_dest_y;

    /* OSRS entity collision flags. pathfinding ignores these; movement checks them. */
    uint8_t npc_collision_flags[COLO_ARENA_WIDTH][COLO_ARENA_HEIGHT];
    uint8_t player_collision_flags[COLO_ARENA_WIDTH][COLO_ARENA_HEIGHT];

    ColosseumLog log;

    /* config copy / RNG */
    int start_wave;
    int config_modifier_draft_window;  /* resolved from ColConfig at reset */
    uint32_t rng_state;
} ColosseumState;
