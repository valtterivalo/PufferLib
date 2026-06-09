#line 76 "encounter_colosseum.h"
/**
 * @file encounter_colosseum_model.inc
 * @brief Colosseum data model: arena, NPC roster, wave defs, state structs.
 *
 * Scaffold values are placeholders. col_build_npc_stats() returns small constant
 * stats the lead replaces with wiki values. The struct layout, union accessors,
 * and obs/action constants are final.
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
#define COLO_NUM_ACTION_HEADS 8

/* max active NPCs at once + obs slot count (one obs slot per active NPC) */
#define COLO_MAX_NPCS 24
#define COLO_OBS_NPCS 12

/* NPC spawn positions around the arena gates (shuffled per wave) */
#define COLO_NUM_SPAWN_POS 8
static const int COLO_SPAWN_POS[COLO_NUM_SPAWN_POS][2] = {
    {4, 4}, {29, 4}, {4, 29}, {29, 29},
    {17, 4}, {4, 17}, {29, 17}, {17, 29},
};

#define COLO_SOL_HEREDIT_X 17
#define COLO_SOL_HEREDIT_Y 17


/** OSRS cache NPC definition ids, used by the renderer to look up models.
    Placeholder ids reuse inferno def ids until the cache export lands. */
static const int COLO_NPC_DEF_IDS[COLO_NUM_NPC_TYPES] = {
    [COLO_FREMENNIK_BERSERKER] = 7691,
    [COLO_FREMENNIK_ARCHER]    = 7692,
    [COLO_FREMENNIK_SEER]      = 7693,
    [COLO_SERPENT_SHAMAN]      = 7694,
    [COLO_JAGUAR_WARRIOR]      = 7696,
    [COLO_JAVELIN_COLOSSUS]    = 7697,
    [COLO_SHOCKWAVE_COLOSSUS]  = 7698,
    [COLO_MINOTAUR]            = 7699,
    [COLO_MANTICORE]           = 7700,
    [COLO_SOL_HEREDIT]         = 7706,
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

/** resolved per-type combat stats, populated at startup by col_build_npc_stats(). */
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
    int magic_base_dmg, magic_dmg_pct;
    int stab_def, slash_def, crush_def, magic_def_bonus, ranged_def_bonus;

    int max_hit_cap;     /* 0 = no cap, >0 = clamp formula result */
    int stun_on_spawn;
    int can_move;
} ColoNpcStats;

/** encounter-specific overlay merged on top of the generated monster database. */
typedef struct {
    int attack_range;
    AttackStyle default_style;
    MeleeStyle melee_style;
    int can_melee;
    int magic_base_dmg;
    int magic_dmg_pct;
    int max_hit_cap;
    int stun_on_spawn;
    int can_move;
} ColoNpcOverlay;

/** maps ColoNpcType -> MonsterIndex for MONSTER_DATABASE lookup. Placeholder
    rows reuse inferno monster ids until COLO mobs are added to monsters.json. */
static const MonsterIndex COLO_NPC_TO_MON[COLO_NUM_NPC_TYPES] = {
    [COLO_FREMENNIK_BERSERKER] = MON_JAL_IMKOT,
    [COLO_FREMENNIK_ARCHER]    = MON_JAL_XIL,
    [COLO_FREMENNIK_SEER]      = MON_JAL_ZEK,
    [COLO_SERPENT_SHAMAN]      = MON_JAL_ZEK,
    [COLO_JAGUAR_WARRIOR]      = MON_JAL_IMKOT,
    [COLO_JAVELIN_COLOSSUS]    = MON_JAL_XIL,
    [COLO_SHOCKWAVE_COLOSSUS]  = MON_JALTOK_JAD,
    [COLO_MINOTAUR]            = MON_JAL_IMKOT,
    [COLO_MANTICORE]           = MON_JALTOK_JAD,
    [COLO_SOL_HEREDIT]         = MON_TZKAL_ZUK,
};

/** encounter overlay extras. Placeholder values; the lead replaces with wiki data. */
static const ColoNpcOverlay COLO_NPC_OVERLAY[COLO_NUM_NPC_TYPES] = {
    [COLO_FREMENNIK_BERSERKER] = { 1, ATTACK_STYLE_MELEE,  MELEE_STYLE_SLASH, 0,  0,   0, 0, 0, 1 },
    [COLO_FREMENNIK_ARCHER]    = { 7, ATTACK_STYLE_RANGED, MELEE_STYLE_STAB,  0,  0,   0, 0, 0, 1 },
    [COLO_FREMENNIK_SEER]      = { 7, ATTACK_STYLE_MAGIC,  MELEE_STYLE_STAB,  0,  10, 100, 0, 0, 1 },
    [COLO_SERPENT_SHAMAN]      = { 7, ATTACK_STYLE_MAGIC,  MELEE_STYLE_STAB,  0,  10, 100, 0, 0, 1 },
    [COLO_JAGUAR_WARRIOR]      = { 1, ATTACK_STYLE_MELEE,  MELEE_STYLE_SLASH, 0,  0,   0, 0, 0, 1 },
    [COLO_JAVELIN_COLOSSUS]    = { 10, ATTACK_STYLE_RANGED, MELEE_STYLE_STAB, 0,  0,   0, 0, 0, 1 },
    [COLO_SHOCKWAVE_COLOSSUS]  = { 1, ATTACK_STYLE_MELEE,  MELEE_STYLE_CRUSH, 1,  0,   0, 0, 0, 1 },
    [COLO_MINOTAUR]            = { 1, ATTACK_STYLE_MELEE,  MELEE_STYLE_CRUSH, 0,  0,   0, 0, 0, 1 },
    [COLO_MANTICORE]           = { 10, ATTACK_STYLE_MAGIC, MELEE_STYLE_STAB,  0,  10, 100, 0, 0, 1 },
    [COLO_SOL_HEREDIT]         = { 1, ATTACK_STYLE_MELEE,  MELEE_STYLE_STAB,  1,  0,   0, 0, 0, 0 },
};

/** populated at startup by col_build_npc_stats(). */
static ColoNpcStats COLO_NPC_STATS[COLO_NUM_NPC_TYPES];

/**
 * Merge MONSTER_DATABASE + COLO_NPC_OVERLAY into COLO_NPC_STATS.
 *
 * Scaffold stub: produces placeholder stats so combat/obs compile and run. The
 * lead replaces the per-type values with exact OSRS Wiki numbers (HP, levels,
 * bonuses, max-hit caps) when wiring real combat.
 */
static void col_build_npc_stats(void) {
    for (int i = 0; i < COLO_NUM_NPC_TYPES; i++) {
        const MonsterStats* m = &MONSTER_DATABASE[COLO_NPC_TO_MON[i]];
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


#define COLO_MAX_NPCS_PER_WAVE 6

typedef struct {
    uint8_t types[COLO_MAX_NPCS_PER_WAVE];
    int count;
} ColoWaveDef;

/** Placeholder wave script. The lead replaces counts/composition with the real
    12-wave roster and reinforcement structure. */
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
    #define W(...) { .types = { __VA_ARGS__ }, .count = sizeof((uint8_t[]){__VA_ARGS__}) }

    [0]  = W(BZ),
    [1]  = W(BZ, AR),
    [2]  = W(BZ, AR, SE),
    [3]  = W(JG, AR),
    [4]  = W(SH, MT),
    [5]  = W(JV, MC),
    [6]  = W(BZ, AR, SE, JG),
    [7]  = W(SW, MT),
    [8]  = W(MC, SH),
    [9]  = W(JV, SW, MT),
    [10] = W(BZ, AR, SE, JG, MC),
    [11] = W(SOL),

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
};


/** active-modifier set. Colosseum offers 3 modifiers to draft between waves;
    each chosen modifier persists and carries a tier. */
#define COLO_NUM_MODIFIERS 16
#define COLO_MODIFIER_DRAFT_OPTIONS 3

typedef struct {
    uint32_t active_mask;                  /* bit i set = modifier i active */
    uint8_t tier[COLO_NUM_MODIFIERS];      /* per-modifier tier (0 = inactive) */
    int draft_options[COLO_MODIFIER_DRAFT_OPTIONS];  /* modifier ids offered, -1 = none */
    int draft_pending;                     /* 1 while a between-wave draft awaits a pick */
} ColoModifierState;


/** Sol Heredit boss sub-state. Scaffold fields cover the documented phases,
    attack-combo queue, triple-parry, grapple, and molten-sand hazards. */
#define COLO_SOL_COMBO_QUEUE_MAX 8
#define COLO_SOL_HAZARD_TILES_MAX 32

typedef struct {
    int phase;                 /* 0..N HP-threshold phase (100/90/75/50/25/10%) */
    int combo_queue[COLO_SOL_COMBO_QUEUE_MAX];  /* queued attack ids */
    int combo_len;
    int combo_cursor;          /* index into combo_queue of the next attack */
    int parry_sequence_active; /* 1 while the triple-parry sequence runs (<90%) */
    int parry_ticks_remaining;
    int grapple_active;        /* 1 while a grapple is in progress (<75%) */
    int grapple_body_slot;     /* which body-part slot the player must click */
    int grapple_timer;         /* 4-tick grapple resolution window */
    int hazard_tile_x[COLO_SOL_HAZARD_TILES_MAX];  /* molten-sand tiles (<10%) */
    int hazard_tile_y[COLO_SOL_HAZARD_TILES_MAX];
    int hazard_tile_count;
    int hazard_timer;          /* ticks until next molten-sand placement */
} SolHereditState;


/** berserker/jaguar 3-hit combo state. valid iff type is COLO_JAGUAR_WARRIOR;
    reach it through colo_npc_jaguar(). */
typedef struct {
    int combo_step;            /* which hit of the 3-hit combo is next */
} ColoJaguarState;

/** manticore 3-style barrage cycle. valid iff type is COLO_MANTICORE;
    reach it through colo_npc_manticore(). */
typedef struct {
    int cycle_step;            /* 0=magic, 1=ranged, 2=melee in the barrage cycle */
} ColoManticoreState;


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

/** write the spawn-time defaults for npc->type's type_state arm. caller must
    have zeroed the NPC and set npc->type first. */
static void colo_npc_init_type_state(ColoNPC* npc) {
    switch (npc->type) {
        case COLO_JAGUAR_WARRIOR:
            npc->type_state.jaguar = (ColoJaguarState){ .combo_step = 0 };
            break;
        case COLO_MANTICORE:
            npc->type_state.manticore = (ColoManticoreState){ .cycle_step = 0 };
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


/** Colosseum env-knob struct. Scaffold reward terms; the lead tunes these. */
typedef struct {
    int start_wave;        /* internal wave index (0-based) */
    float damage_reward_coeff;
    float boss_damage_reward_coeff;
    float wave_clear_bonus;
    float boss_phase_bonus;
    float win_bonus;
    float death_penalty_coeff;
    int terminal_penalty_enabled;
    int curriculum_agent;
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

    /* reward bookkeeping */
    float reward;
    float episode_return;
    int last_hit_by_type;     /* NPC type that last damaged the player (-1 = none) */

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
    uint32_t rng_state;
} ColosseumState;
