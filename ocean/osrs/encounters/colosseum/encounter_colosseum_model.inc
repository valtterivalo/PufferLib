#line 76 "encounter_colosseum.h"
/**
 * @file encounter_colosseum_model.inc
 * @brief Colosseum data model: arena, NPC roster, wave defs, state structs.
 *
 * NPC combat stats are the canonical COLO_NPC_BASE table (refs/osrs-dps-calc,
 * cross-checked vs wiki) merged with a behavior overlay by col_build_npc_stats().
 * The wave roster, modifiers, and Sol boss sub-state are later phases.
 */

/* SIM-LOCAL frame: (0,0) = world (1808,3090), the SW corner of the 34x34
   playable square (the los.colosim.com grid); x+ = world east, y+ = world NORTH.
   sim (x,y) <-> world (1808+x, 3090+y); los grid: sim_x = los_col,
   sim_y = 33 - los_row (los y+ = south). */
#define COLO_ARENA_MIN_X    0
#define COLO_ARENA_MAX_X    33
#define COLO_ARENA_MIN_Y    0
#define COLO_ARENA_MAX_Y    33
#define COLO_ARENA_WIDTH    (COLO_ARENA_MAX_X - COLO_ARENA_MIN_X + 1)   /* 34 */
#define COLO_ARENA_HEIGHT   (COLO_ARENA_MAX_Y - COLO_ARENA_MIN_Y + 1)  /* 34 */

/* A18: canonical wave start, the los default player tile = world (1815,3108),
   west side by the west entrance. */
#define COLO_PLAYER_START_X 7
#define COLO_PLAYER_START_Y 18

#define COLO_MAX_TICKS      9000   /* 90 minutes at 0.6s/tick */
#define COLO_SPEEDRUN_MAX_TICKS 4000
#define COLO_BEGINNER_MAX_TICKS 6000
#define COLO_NUM_WAVES      12
#define COLO_WAVE_BOSS      (COLO_NUM_WAVES - 1)  /* wave index 11 = Sol Heredit */
#define COLO_START_READY_TICKS 6
#define COLO_REINFORCEMENT_TICKS 66   /* 40s reinforcement cadence */
#define COLO_NUM_ACTION_HEADS 35

/* max active NPCs at once + obs slot count (one obs slot per active NPC).
   COLO_OBS_NPCS == COLO_MAX_NPCS so no live NPC is ever dropped from the obs
   window or the TARGET action head at busy waves. */
#define COLO_MAX_NPCS 24
#define COLO_OBS_NPCS COLO_MAX_NPCS
/* compile-time MAX rollout depth (ticks[] array size). The live depth is the
   runtime ColConfig.forecast_horizon, clamped to [1, this]; default == this. */
#define COLO_STEP_OUT_FORECAST_HORIZON 4
#define COLO_STEP_OUT_FORECAST_ACTION_FEATURES 8
#define COLO_STEP_OUT_FORECAST_OBS_SIZE \
    (ENCOUNTER_MOVE_ACTIONS * COLO_STEP_OUT_FORECAST_ACTION_FEATURES)

typedef struct {
    int melee_count;
    int ranged_count;
    int magic_count;
    int max_hit;
} ColoStepOutForecastTick;

typedef struct {
    int valid;
    int land_x;
    int land_y;
    ColoStepOutForecastTick ticks[COLO_STEP_OUT_FORECAST_HORIZON];
    int same_tick_mixed_style_conflict;
    int ranged_magic_offtick_opportunity;
    int melee_fallback_exposure;
} ColoStepOutForecastAction;

typedef struct {
    ColoStepOutForecastAction actions[ENCOUNTER_MOVE_ACTIONS];
} ColoStepOutForecast;

typedef enum {
    COLO_FORECAST_BEHAVIOR_STANDARD = 0,
    COLO_FORECAST_BEHAVIOR_WARBAND,
    COLO_FORECAST_BEHAVIOR_MANTICORE,
    COLO_FORECAST_BEHAVIOR_SOL,
} ColoForecastBehaviorKind;

typedef struct {
    int active;
    ColoNpcType type;
    ColoForecastBehaviorKind forecast_behavior;
    int x;
    int y;
    int size;
    int hp;
    int attack_range;
    int attack_speed;
    int can_move;
    AttackStyle default_style;
    int attack_timer;
    int stun_timer;
    int frozen_ticks;
    int death_ticks;
    int cycle_step;
    AttackStyle orb_style[3];
    AttackStyle fixed_orb_style[3];
    int formation_dir;
    int javelin_attack_count;
    int sol_attack_delay;
    int sol_immobile_ticks;
} ColoForecastNpcLocal;

/* A7+B1: static arena terrain. Walls: the los `Lr` per-row blocked x-extents
   (half-open [lo,hi)), row-flipped into sim rows, cross-verified against the wiki
   deadzone outline with 0 mismatches (WSN#GAP1). Pillars: four 3x3 objects
   (obj 52490, cache-verified SW anchors), present on EVERY wave; they block
   movement and line of sight. */
typedef struct { uint8_t lo, hi; } ColoWallSpan;   /* blocked x in [lo, hi) */
#define COLO_WALL_SPANS_PER_ROW 3
static const ColoWallSpan COLO_WALL_SPANS[COLO_ARENA_HEIGHT][COLO_WALL_SPANS_PER_ROW] = {
    [0]  = { {0,13}, {15,19}, {21,34} },   /* los row 33: south inner row, gate doors */
    [1]  = { {0,9},  {25,34} },            /* los row 32 */
    [2]  = { {0,7},  {27,34} },            /* los row 31 */
    [3]  = { {0,5},  {29,34} },            /* los row 30 */
    [4]  = { {0,5},  {29,34} },            /* los row 29 */
    [5]  = { {0,3},  {31,34} },            /* los row 28 */
    [6]  = { {0,3},  {31,34} },            /* los row 27 */
    [7]  = { {0,2},  {32,34} },            /* los row 26 */
    [8]  = { {0,2},  {32,34} },            /* los row 25 */
    [9]  = { {0,1},  {33,34} },            /* los row 24 */
    [10] = { {0,1},  {33,34} },            /* los row 23 */
    [11] = { {0,1},  {32,34} },            /* los row 22 */
    [12] = { {0,1},  {31,34} },            /* los row 21 */
    [13] = { {31,34} },                    /* los row 20: west entrance flank */
    [14] = { {31,34} },                    /* los row 19: west entrance flank */
    [15] = { {0,1},  {31,34} },            /* los row 18 */
    [16] = { {0,1},  {31,34} },            /* los row 17 */
    [17] = { {0,1},  {31,34} },            /* los row 16 */
    [18] = { {0,1},  {31,34} },            /* los row 15 */
    [19] = { {31,34} },                    /* los row 14: west entrance flank */
    [20] = { {31,34} },                    /* los row 13: west entrance flank */
    [21] = { {0,1},  {31,34} },            /* los row 12 */
    [22] = { {0,1},  {32,34} },            /* los row 11 */
    [23] = { {0,1},  {33,34} },            /* los row 10 */
    [24] = { {0,1},  {33,34} },            /* los row 9 */
    [25] = { {0,2},  {32,34} },            /* los row 8 */
    [26] = { {0,2},  {32,34} },            /* los row 7 */
    [27] = { {0,3},  {31,34} },            /* los row 6 */
    [28] = { {0,3},  {31,34} },            /* los row 5 */
    [29] = { {0,5},  {29,34} },            /* los row 4 */
    [30] = { {0,6},  {29,34} },            /* los row 3: asymmetric vs sim row 3 */
    [31] = { {0,7},  {27,34} },            /* los row 2 */
    [32] = { {0,9},  {25,34} },            /* los row 1 */
    [33] = { {0,13}, {15,19}, {21,34} },   /* los row 0: north inner row, gate doors */
};

/* B1: the four arena pillars, SW tiles, world (1816,3098) (1831,3098)
   (1816,3113) (1831,3113); 15-tile spacing both axes. */
#define COLO_NUM_PILLARS  4
#define COLO_PILLAR_SIZE  3
static const int COLO_PILLARS[COLO_NUM_PILLARS][2] = {
    {8, 8}, {23, 8}, {8, 23}, {23, 23},
};

/** blocked-tile grid, COLO_WALL_SPANS + COLO_PILLARS, built by
    col_build_static_arena (called from col_build_npc_stats at every entry point). */
static uint8_t COLO_STATIC_BLOCKED[COLO_ARENA_WIDTH][COLO_ARENA_HEIGHT];

/**
 * Process-local static geometry caches.
 *
 * osrs_colosseum my_vec_init calls my_init sequentially before worker threads
 * are created. The first col_build_static_arena call builds these tables once
 * on that setup path.
 */
#define COLO_ARENA_TILE_COUNT (COLO_ARENA_WIDTH * COLO_ARENA_HEIGHT)
#define COLO_STATIC_LOS_TABLE_BITS (COLO_ARENA_TILE_COUNT * COLO_ARENA_TILE_COUNT)
#define COLO_STATIC_LOS_TABLE_WORDS ((COLO_STATIC_LOS_TABLE_BITS + 63) / 64)
#define COLO_STATIC_FOOTPRINT_MAX_SIZE 5
#define COLO_STATIC_FOOTPRINT_PAD (COLO_STATIC_FOOTPRINT_MAX_SIZE - 1)
#define COLO_STATIC_FOOTPRINT_MIN_X (COLO_ARENA_MIN_X - COLO_STATIC_FOOTPRINT_PAD)
#define COLO_STATIC_FOOTPRINT_MIN_Y (COLO_ARENA_MIN_Y - COLO_STATIC_FOOTPRINT_PAD)
#define COLO_STATIC_FOOTPRINT_MAX_X (COLO_ARENA_MAX_X + 1)
#define COLO_STATIC_FOOTPRINT_MAX_Y (COLO_ARENA_MAX_Y + 1)
#define COLO_STATIC_FOOTPRINT_WIDTH \
    (COLO_STATIC_FOOTPRINT_MAX_X - COLO_STATIC_FOOTPRINT_MIN_X + 1)
#define COLO_STATIC_FOOTPRINT_HEIGHT \
    (COLO_STATIC_FOOTPRINT_MAX_Y - COLO_STATIC_FOOTPRINT_MIN_Y + 1)
static uint64_t COLO_STATIC_LOS_BITS[COLO_STATIC_LOS_TABLE_WORDS];
static uint8_t COLO_STATIC_FOOTPRINT_BLOCKED[COLO_STATIC_FOOTPRINT_MAX_SIZE]
    [COLO_STATIC_FOOTPRINT_WIDTH][COLO_STATIC_FOOTPRINT_HEIGHT];
static int COLO_STATIC_LOOKUP_TABLES_BUILT = 0;

static void col_build_static_lookup_tables_once(void);

static void col_build_static_arena(void) {
    memset(COLO_STATIC_BLOCKED, 0, sizeof(COLO_STATIC_BLOCKED));
    for (int y = 0; y < COLO_ARENA_HEIGHT; y++) {
        for (int sp = 0; sp < COLO_WALL_SPANS_PER_ROW; sp++) {
            ColoWallSpan span = COLO_WALL_SPANS[y][sp];
            for (int x = span.lo; x < span.hi; x++) COLO_STATIC_BLOCKED[x][y] = 1;
        }
    }
    for (int p = 0; p < COLO_NUM_PILLARS; p++)
        for (int dx = 0; dx < COLO_PILLAR_SIZE; dx++)
            for (int dy = 0; dy < COLO_PILLAR_SIZE; dy++)
                COLO_STATIC_BLOCKED[COLO_PILLARS[p][0] + dx][COLO_PILLARS[p][1] + dy] = 1;
    col_build_static_lookup_tables_once();
}

/** wall/pillar lookup; out-of-bounds counts as blocked. */
static inline int col_static_blocked(int x, int y) {
    if (x < COLO_ARENA_MIN_X || x > COLO_ARENA_MAX_X ||
        y < COLO_ARENA_MIN_Y || y > COLO_ARENA_MAX_Y) return 1;
    return COLO_STATIC_BLOCKED[x][y];
}

static inline int col_static_los_endpoint_in_bounds(int x, int y) {
    return x >= COLO_ARENA_MIN_X && x <= COLO_ARENA_MAX_X &&
           y >= COLO_ARENA_MIN_Y && y <= COLO_ARENA_MAX_Y;
}

static inline uint32_t col_static_los_index(int x0, int y0, int x1, int y1) {
    uint32_t a = (uint32_t)((x0 - COLO_ARENA_MIN_X) * COLO_ARENA_HEIGHT +
        (y0 - COLO_ARENA_MIN_Y));
    uint32_t b = (uint32_t)((x1 - COLO_ARENA_MIN_X) * COLO_ARENA_HEIGHT +
        (y1 - COLO_ARENA_MIN_Y));
    return a * COLO_ARENA_TILE_COUNT + b;
}

static inline void col_static_los_set(uint32_t idx) {
    COLO_STATIC_LOS_BITS[idx >> 6] |= 1ULL << (idx & 63);
}

static inline int col_static_los_get(int x0, int y0, int x1, int y1) {
    uint32_t idx = col_static_los_index(x0, y0, x1, y1);
    return (int)((COLO_STATIC_LOS_BITS[idx >> 6] >> (idx & 63)) & 1ULL);
}

static inline int col_static_footprint_anchor_in_table(int x, int y) {
    return x >= COLO_STATIC_FOOTPRINT_MIN_X && x <= COLO_STATIC_FOOTPRINT_MAX_X &&
           y >= COLO_STATIC_FOOTPRINT_MIN_Y && y <= COLO_STATIC_FOOTPRINT_MAX_Y;
}

static int col_static_footprint_blocked_reference_slowpath(int x, int y, int size) {
    for (int dx = 0; dx < size; dx++) {
        for (int dy = 0; dy < size; dy++) {
            if (col_static_blocked(x + dx, y + dy)) return 1;
        }
    }
    return 0;
}

static inline int col_static_footprint_blocked_lookup(int x, int y, int size) {
    if (size < 1 || size > COLO_STATIC_FOOTPRINT_MAX_SIZE)
        return col_static_footprint_blocked_reference_slowpath(x, y, size);
    if (!COLO_STATIC_LOOKUP_TABLES_BUILT) col_build_static_arena();
    if (!col_static_footprint_anchor_in_table(x, y)) return 1;
    return COLO_STATIC_FOOTPRINT_BLOCKED[size - 1]
        [x - COLO_STATIC_FOOTPRINT_MIN_X]
        [y - COLO_STATIC_FOOTPRINT_MIN_Y];
}

static int col_los_tile_blocked_reference(void* ctx, int x, int y) {
    (void)ctx;
    return col_static_blocked(x, y);
}

static int col_tiles_have_los_reference_slowpath(int x0, int y0, int x1, int y1) {
    OsrsLosQuery query = osrs_los_tile(col_los_tile_blocked_reference, NULL);
    return osrs_los_tile_ray_clear(&query, x0, y0, x1, y1);
}

static void col_build_static_lookup_tables_once(void) {
    if (COLO_STATIC_LOOKUP_TABLES_BUILT) return;
    for (int x0 = COLO_ARENA_MIN_X; x0 <= COLO_ARENA_MAX_X; x0++) {
        for (int y0 = COLO_ARENA_MIN_Y; y0 <= COLO_ARENA_MAX_Y; y0++) {
            for (int x1 = COLO_ARENA_MIN_X; x1 <= COLO_ARENA_MAX_X; x1++) {
                for (int y1 = COLO_ARENA_MIN_Y; y1 <= COLO_ARENA_MAX_Y; y1++) {
                    if (col_tiles_have_los_reference_slowpath(x0, y0, x1, y1))
                        col_static_los_set(col_static_los_index(x0, y0, x1, y1));
                }
            }
        }
    }
    for (int size = 1; size <= COLO_STATIC_FOOTPRINT_MAX_SIZE; size++) {
        for (int x = COLO_STATIC_FOOTPRINT_MIN_X; x <= COLO_STATIC_FOOTPRINT_MAX_X; x++) {
            for (int y = COLO_STATIC_FOOTPRINT_MIN_Y; y <= COLO_STATIC_FOOTPRINT_MAX_Y; y++) {
                COLO_STATIC_FOOTPRINT_BLOCKED[size - 1]
                    [x - COLO_STATIC_FOOTPRINT_MIN_X]
                    [y - COLO_STATIC_FOOTPRINT_MIN_Y] =
                    (uint8_t)col_static_footprint_blocked_reference_slowpath(x, y, size);
            }
        }
    }
    COLO_STATIC_LOOKUP_TABLES_BUILT = 1;
}

static void col_static_los_table_selftest(void) {
    col_build_static_arena();
    for (int x0 = COLO_ARENA_MIN_X; x0 <= COLO_ARENA_MAX_X; x0++) {
        for (int y0 = COLO_ARENA_MIN_Y; y0 <= COLO_ARENA_MAX_Y; y0++) {
            for (int x1 = COLO_ARENA_MIN_X; x1 <= COLO_ARENA_MAX_X; x1++) {
                for (int y1 = COLO_ARENA_MIN_Y; y1 <= COLO_ARENA_MAX_Y; y1++) {
                    int table = col_static_los_get(x0, y0, x1, y1);
                    int slow = col_tiles_have_los_reference_slowpath(x0, y0, x1, y1);
                    if (table != slow) {
                        fprintf(stderr,
                            "colosseum LoS table mismatch (%d,%d)->(%d,%d): table=%d slow=%d\n",
                            x0, y0, x1, y1, table, slow);
                        abort();
                    }
                }
            }
        }
    }
    printf("colosseum LoS table selftest PASS: %u pairs\n",
        (unsigned)COLO_STATIC_LOS_TABLE_BITS);
}

static void col_static_footprint_table_selftest(void) {
    col_build_static_arena();
    int checks = 0;
    int min_x = COLO_STATIC_FOOTPRINT_MIN_X - COLO_STATIC_FOOTPRINT_MAX_SIZE;
    int min_y = COLO_STATIC_FOOTPRINT_MIN_Y - COLO_STATIC_FOOTPRINT_MAX_SIZE;
    int max_x = COLO_STATIC_FOOTPRINT_MAX_X + COLO_STATIC_FOOTPRINT_MAX_SIZE;
    int max_y = COLO_STATIC_FOOTPRINT_MAX_Y + COLO_STATIC_FOOTPRINT_MAX_SIZE;
    for (int size = 1; size <= COLO_STATIC_FOOTPRINT_MAX_SIZE; size++) {
        for (int x = min_x; x <= max_x; x++) {
            for (int y = min_y; y <= max_y; y++) {
                int table = col_static_footprint_blocked_lookup(x, y, size);
                int slow = col_static_footprint_blocked_reference_slowpath(x, y, size);
                if (table != slow) {
                    fprintf(stderr,
                        "colosseum footprint table mismatch (%d,%d) size=%d: table=%d slow=%d\n",
                        x, y, size, table, slow);
                    abort();
                }
                checks++;
            }
        }
    }
    printf("colosseum footprint table selftest PASS: %d checks\n", checks);
}

/* A4: the 12 canonical primary-spawn anchors, SW tile of each 3x3 zone; the NPC's
   SW tile is placed ON the anchor. Three-way cross-verified (los `ao` array + b5
   pastebin + wiki "12 default spawns", WSN#GAP1). D5: the b5 13th tile
   (1824,3110) and cyan tile (1824,3106) are excluded — semantics unstated. */
#define COLO_NUM_SPAWN_ANCHORS 12
#define COLO_SPAWN_ZONE_SIZE   3
static const int COLO_SPAWN_ANCHORS[COLO_NUM_SPAWN_ANCHORS][2] = {
    {3, 14}, {9, 16}, {3, 19}, {13, 19}, {19, 19}, {17, 24},
    {13, 13}, {19, 13}, {16, 9}, {24, 17}, {28, 19}, {28, 14},
};

/** B3: primary spawns cannot land within Chebyshev 4 of the player at spawn
    resolution (the b5 9x9 red box). */
#define COLO_SPAWN_EXCLUSION_CHEB 4

/* B2(d) geometry: the warband does NOT use the anchors. The ranger spawns
   uniformly inside the b5 centre box, world (1820,3105)-(1826,3111); berserker +
   seer take adjacent tiles. */
#define COLO_WARBAND_BOX_MIN_X 12
#define COLO_WARBAND_BOX_MIN_Y 15
#define COLO_WARBAND_BOX_MAX_X 18
#define COLO_WARBAND_BOX_MAX_Y 21

/* B2(a): the trio attacks on ONE shared 6-tick cycle anchored to the wave's
   first actionable tick N (the COLO_START_READY_TICKS ready delay = the
   researched click+6): berserker window N+1, seer N+2, archer N+3, then 3
   silent ticks. D30: assume the wave-anchored phase survived the April 2024
   rebalance. */
#define COLO_WARBAND_CYCLE_TICKS 6

/* B2(e)+D3: warbanders "dart" — 2 path steps per tick (OSRS run pace; no
   tiles/tick figure exists in any source). Everything else stays 1. */
#define COLO_WARBAND_TILES_PER_TICK 2

/** B2(c): the diamond slot a warbander holds around the player. Berserker
    prioritises NORTH, seer EAST, archer WEST; the Quartet extra takes SOUTH.
    World north = sim +y. */
typedef enum {
    COLO_WARBAND_FORM_NORTH = 0,
    COLO_WARBAND_FORM_EAST,
    COLO_WARBAND_FORM_WEST,
    COLO_WARBAND_FORM_SOUTH,
} ColoWarbandFormation;

/** (dx, dy) from the player's tile to each formation slot's preferred tile. */
static const int COLO_WARBAND_FORM_OFFSET[4][2] = {
    [COLO_WARBAND_FORM_NORTH] = { 0, 1 },
    [COLO_WARBAND_FORM_EAST]  = { 1, 0 },
    [COLO_WARBAND_FORM_WEST]  = { -1, 0 },
    [COLO_WARBAND_FORM_SOUTH] = { 0, -1 },
};

/* A17+B8+D2: gates are wall GAPS spanning sim x 15-18 in the north (sim row 33)
   and south (sim row 0) inner wall rows (doors blocked, flanks walkable).
   Reinforcements materialize side-by-side inside the gap on the innermost
   walkable row of the chosen wall. Side selection: the b5 yellow line at the
   world y 3105/3106 boundary — player y <= 15 -> SOUTH gate, y >= 16 -> NORTH —
   a fixed threshold, NOT nearest-distance. WORLD north = sim +y. */
#define COLO_GATE_MIN_X            15
#define COLO_GATE_MAX_X            18
#define COLO_GATE_LINE_NORTH_MIN_Y 16
#define COLO_GATE_NORTH_SPAWN_ROW  32
#define COLO_GATE_SOUTH_SPAWN_ROW  1

/* A8: Sol Heredit SW spawn tile, world (1824,3109) (npc 12821, RuneC
   test-asserted; private-server verified). */
#define COLO_SOL_SPAWN_X 16
#define COLO_SOL_SPAWN_Y 19

/* A8+D29: wave-12 outer box world (1816,3098)-(1833,3115) = sim (8,8)-(25,25);
   the usable interior is (9,9)-(24,24). The four 3x3 pillars sit at the outer
   box's corners, so the static mask clips a 2x2 notch out of each interior
   corner — the clamp + mask together produce the researched usable area. */
#define COLO_BOSS_ARENA_MIN_X 9
#define COLO_BOSS_ARENA_MIN_Y 9
#define COLO_BOSS_ARENA_MAX_X 24
#define COLO_BOSS_ARENA_MAX_Y 24
#define COLO_BOSS_ARENA_SIZE  (COLO_BOSS_ARENA_MAX_X - COLO_BOSS_ARENA_MIN_X + 1)  /* 16 */

/* D27: boss-wave start tile, south-centre interior, world (1824,3100). No
   teleport exists in any source; the teleport is a training affordance. */
#define COLO_BOSS_PLAYER_START_X 16
#define COLO_BOSS_PLAYER_START_Y 10


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
    [COLO_HEALING_TOTEM]       = 12825,
    [COLO_BEE_SWARM]           = 12823,
};

/** A30: the routefinding "Red Flag" minotaur is cache npc 12813 (same model set
    and anims as 12812). The sim type stays COLO_MINOTAUR — only the def id
    reported to the renderer switches while COLO_MOD_RED_FLAG is active. */
#define COLO_NPC_DEF_ID_MINOTAUR_ROUTEFIND 12813

/** Arceuus greater ghost thrall (cache npc 10880). Render-only: the thrall is a
    sim allied auto-attacker with no ColoNPC slot, so it borrows this def id for
    its model + attack/death anims when surfaced as a render entity. */
#define COLO_NPC_DEF_ID_THRALL 10880

/** Render-only thrall identity: npc_slot one past the real slots (never collides
    with a ColoNPC index) and a fixed nonzero instance id, so the renderer matches
    the thrall entity frame-to-frame for animation + movement continuity. */
#define COLO_NPC_SLOT_THRALL    COLO_MAX_NPCS
#define COLO_THRALL_RENDER_INSTANCE_ID 0xC01055EEu

/** Solarflare orbiting sun (cache npc 12826, idle anim 10817). Render only.
    The sim models four slotless per-pillar orb hazards, so the viewer surfaces
    them on synthetic slots after the thrall slot. */
#define COLO_NPC_DEF_ID_SOLARFLARE 12826
#define COLO_SOLARFLARE_IDLE_ANIM_ID 10817
#define COLO_NPC_SLOT_SOLARFLARE    (COLO_MAX_NPCS + 1)
#define COLO_SOLARFLARE_RENDER_INSTANCE_ID 0xC0105F1Au

/** A21+B9: hazard entities — attackable 1-HP modifier spawns. Excluded from
    wave-clear/reinforcement predicates, collision stamps, and the death hooks
    real NPCs trigger; they render and are clickable like any other NPC. */
static int col_type_is_hazard_entity(ColoNpcType type) {
    return type == COLO_HEALING_TOTEM || type == COLO_BEE_SWARM;
}

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
        case COLO_HEALING_TOTEM:       return "Healing totem";
        case COLO_BEE_SWARM:           return "Bee swarm";
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
    vs OSRS wiki; warband offensive bonuses aligned to the decoded cache params,
    A31). Self-contained: the shared combat fns take plain ints, so Colosseum
    needs no MONSTER_DATABASE entry. Field order follows MonsterStats.
    D30: warband attack_speed is the scripted 6t shared cycle; cache param 14=5
    is treated as engine-internal. */
static const MonsterStats COLO_NPC_BASE[COLO_NUM_NPC_TYPES] = {
    [COLO_FREMENNIK_BERSERKER] = { .npc_id=12816, .name="Fremennik berserker",
        .hp=48, .att_level=110,.str_level=110,.def_level=80,.magic_level=110,.range_level=110,
        .attack_speed=6,.size=1,.max_hit=29,
        .melee_att_bonus=150,.melee_str_bonus=90,.magic_att_bonus=0,.magic_str_bonus=0,.range_att_bonus=0,.ranged_str_bonus=0,
        .stab_def=50,.slash_def=50,.crush_def=50,.magic_def=0,.ranged_def=75 },
    [COLO_FREMENNIK_ARCHER] = { .npc_id=12814, .name="Fremennik archer",
        .hp=50, .att_level=110,.str_level=110,.def_level=80,.magic_level=110,.range_level=110,
        .attack_speed=6,.size=1,.max_hit=14,
        .melee_att_bonus=150,.melee_str_bonus=0,.magic_att_bonus=0,.magic_str_bonus=0,.range_att_bonus=150,.ranged_str_bonus=10,
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
        .hp=225, .att_level=200,.str_level=300,.def_level=190,.magic_level=225,.range_level=360,
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
    /* A21+B9: hazard entities. 1 HP (cache-verified for both); any landed player
       attack destroys them (col_player_attack_target bypasses the rolls). */
    [COLO_HEALING_TOTEM] = { .npc_id=12825, .name="Healing totem",
        .hp=1, .att_level=1,.str_level=1,.def_level=1,.magic_level=1,.range_level=1,
        .attack_speed=0,.size=1,.max_hit=0 },
    [COLO_BEE_SWARM] = { .npc_id=12823, .name="Bee swarm",
        .hp=1, .att_level=1,.str_level=1,.def_level=1,.magic_level=1,.range_level=1,
        .attack_speed=0,.size=2,.max_hit=0 },
};

/** per-style barrage max hits for the Manticore (mechanics-to-code.md). */
#define COLO_MANTICORE_MAX_HIT_RANGED 36
#define COLO_MANTICORE_MAX_HIT_MAGIC  31
#define COLO_MANTICORE_MAX_HIT_MELEE  31

/** encounter behavior overlay. attack_range/default_style/RPS weakness per type;
    Sol can_move=1 but the boss machine owns his chase + attack freezes (A2);
    the generic NPC mover skips him. magic damage is the handset max_hit, so no
    base/pct fields are needed here. A5: ALL THREE warbanders attack only at
    melee distance while standing still — the archer/seer fire their style
    point-blank, never at range. */
static const ColoNpcOverlay COLO_NPC_OVERLAY[COLO_NUM_NPC_TYPES] = {
    [COLO_FREMENNIK_BERSERKER] = { .attack_range=1,  .default_style=ATTACK_STYLE_MELEE,  .melee_style=MELEE_STYLE_STAB,  .can_melee=1, .player_style_that_max_hits=ATTACK_STYLE_MAGIC,  .can_move=1 },
    [COLO_FREMENNIK_ARCHER]    = { .attack_range=1,  .default_style=ATTACK_STYLE_RANGED, .melee_style=MELEE_STYLE_STAB,  .can_melee=0, .player_style_that_max_hits=ATTACK_STYLE_MELEE,  .can_move=1 },
    [COLO_FREMENNIK_SEER]      = { .attack_range=1,  .default_style=ATTACK_STYLE_MAGIC,  .melee_style=MELEE_STYLE_STAB,  .can_melee=0, .player_style_that_max_hits=ATTACK_STYLE_RANGED, .can_move=1 },
    [COLO_SERPENT_SHAMAN]      = { .attack_range=10, .default_style=ATTACK_STYLE_MAGIC,  .melee_style=MELEE_STYLE_STAB,  .can_melee=0, .player_style_that_max_hits=ATTACK_STYLE_NONE,  .can_move=1 },
    [COLO_JAGUAR_WARRIOR]      = { .attack_range=1,  .default_style=ATTACK_STYLE_MELEE,  .melee_style=MELEE_STYLE_SLASH, .can_melee=1, .player_style_that_max_hits=ATTACK_STYLE_NONE,  .can_move=1 },
    [COLO_JAVELIN_COLOSSUS]    = { .attack_range=15, .default_style=ATTACK_STYLE_RANGED, .melee_style=MELEE_STYLE_STAB,  .can_melee=0, .player_style_that_max_hits=ATTACK_STYLE_NONE,  .can_move=1 },
    [COLO_SHOCKWAVE_COLOSSUS]  = { .attack_range=15, .default_style=ATTACK_STYLE_MAGIC,  .melee_style=MELEE_STYLE_STAB,  .can_melee=0, .player_style_that_max_hits=ATTACK_STYLE_NONE,  .can_move=1 },
    [COLO_MINOTAUR]            = { .attack_range=1,  .default_style=ATTACK_STYLE_MELEE,  .melee_style=MELEE_STYLE_CRUSH, .can_melee=1, .player_style_that_max_hits=ATTACK_STYLE_NONE,  .can_move=1 },
    [COLO_MANTICORE]           = { .attack_range=15, .default_style=ATTACK_STYLE_RANGED, .melee_style=MELEE_STYLE_STAB,  .can_melee=1, .player_style_that_max_hits=ATTACK_STYLE_NONE,  .can_move=1 },
    [COLO_SOL_HEREDIT]         = { .attack_range=1,  .default_style=ATTACK_STYLE_MELEE,  .melee_style=MELEE_STYLE_SLASH, .can_melee=1, .player_style_that_max_hits=ATTACK_STYLE_NONE,  .can_move=1 },
    [COLO_HEALING_TOTEM]       = { .attack_range=0,  .default_style=ATTACK_STYLE_NONE,   .melee_style=MELEE_STYLE_STAB,  .can_melee=0, .player_style_that_max_hits=ATTACK_STYLE_NONE,  .can_move=0 },
    [COLO_BEE_SWARM]           = { .attack_range=0,  .default_style=ATTACK_STYLE_NONE,   .melee_style=MELEE_STYLE_STAB,  .can_melee=0, .player_style_that_max_hits=ATTACK_STYLE_NONE,  .can_move=0 },
};

/** populated at startup by col_build_npc_stats(). */
static ColoNpcStats COLO_NPC_STATS[COLO_NUM_NPC_TYPES];

/**
 * Merge COLO_NPC_BASE + COLO_NPC_OVERLAY into COLO_NPC_STATS, and (re)build the
 * static arena mask. Idempotent; every entry point (reset, lab, snapshot) runs it.
 *
 * Accuracy stays level + bonus driven (shared NPC roll); damage uses the handset
 * max_hit. No max-hit formula is run for these NPCs.
 */
static void col_build_npc_stats(void) {
    col_build_static_arena();
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


/** A29: the roster cap must hold the largest initial spawn set — 7 scripted
    (W8/W11) + 1 Quartet warbander + 1 Dynamic Duo pair = 9. Spawn paths assert
    a free NPC slot instead of silently dropping. */
#define COLO_MAX_NPCS_PER_WAVE 10
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


/** active-modifier set. A MANDATORY draft of 3 modifiers gates every wave start
    from wave 2 through wave 12 — 11 picks per full run (wave 1 carries no
    modifier); each chosen modifier persists and carries a tier. */
#define COLO_NUM_MODIFIERS 16
#define COLO_MODIFIER_DRAFT_OPTIONS 3

/** A16: Red Flag and Dynamic Duo enter the pool only for drafts that occur
    before wave 7 starts — eligible iff the gated wave index (0-based) is at
    most this; absent from every draft into wave 8+. */
#define COLO_RFDD_LAST_DRAFT_WAVE 6

/** A16 MODELED: offers bias toward upgrading previously picked tier-1/2
    modifiers — an owned upgradable modifier carries this weight vs 1 for the
    rest when filling the 3 slots (the multi-source "biased toward upgrades"
    claim gives no number; ~2x is the modeled interpretation). */
#define COLO_DRAFT_UPGRADE_WEIGHT 2

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
    0, 0, 0, 1,
    0, 1, 0, 1,
    1, 0, 0, 0,
    0, 0,
};

/** B6: while draft_pending the player is FROZEN — movement/attack actions are
    ignored, NPC + hazard ticking pauses, and only a MODIFIER_SELECT pick (no
    skip exists) resumes play. draft_gates_spawn marks the wave-gating drafts
    (reset + wave clear): their pick arms the next wave spawn; lab-opened
    drafts leave it 0. */
typedef struct {
    uint32_t active_mask;                  /* bit i set = modifier i active */
    uint8_t tier[COLO_NUM_MODIFIERS];      /* per-modifier tier (0 = inactive) */
    int draft_options[COLO_MODIFIER_DRAFT_OPTIONS];  /* modifier ids offered, -1 = none */
    int draft_pending;                     /* 1 while a mandatory draft awaits the pick */
    int draft_gates_spawn;                 /* 1 = the pick triggers the queued wave spawn */
} ColoModifierState;


/** lifecycle phase of a managed hazard entity (bee swarm / healing totem). */
typedef enum {
    COLO_HAZARD_NONE = 0,    /* not fielded */
    COLO_HAZARD_ALIVE,       /* live NPC at npc_slot */
    COLO_HAZARD_RESPAWNING,  /* killed; respawn_timer counts down */
} ColoHazardPhase;

/** Bees! hazard (B9): each swarm is an attackable 1-HP NPC (COLO_BEE_SWARM)
    converging on the player every COLO_BEE_MOVE_INTERVAL ticks and dealing
    COLO_BEE_MIN_DAMAGE..COLO_BEE_MAX_DAMAGE unblockable damage beneath the
    player; killed swarms respawn COLO_BEE_RESPAWN_TICKS later (UPD-0410 values).
    Tier I/II/III field 1/2/3 swarms. D34: random-anchor spawn, diagonal steps,
    clamp immunity kept. */
#define COLO_MAX_BEE_SWARMS 3
#define COLO_BEE_MOVE_INTERVAL 12   /* swarm steps one tile every 12 ticks */
/* unblockable contact damage band, dedicated Bee_Swarm wiki page (15-20/tick).
   Halved with an active antipoison; serpentine helm grants full immunity. */
#define COLO_BEE_MIN_DAMAGE 15
#define COLO_BEE_MAX_DAMAGE 20
#define COLO_BEE_RESPAWN_TICKS 50   /* 30s respawn after a swarm is killed */

typedef struct {
    ColoHazardPhase phase;
    int npc_slot;       /* live bee NPC slot (valid iff phase == ALIVE) */
    int respawn_timer;  /* ticks until respawn (valid iff phase == RESPAWNING) */
    int move_timer;     /* ticks until the next step toward the player */
} ColoBeeSwarm;

/** Totemic hazard (A21+B5+D22): when an NPC drops to <=50% HP a healing totem
    (attackable 1-HP NPC, cache id 12825) spawns beside it. Every
    COLO_TOTEM_HEAL_INTERVAL ticks it heals its owner COLO_TOTEM_HEAL_PCT% of
    max HP while the owner sits at or below 50% (the confirmed trigger); on
    wave 12 it heals SOL a flat COLO_TOTEM_SOL_HEAL until destroyed (B5). A
    killed totem respawns COLO_TOTEM_RESPAWN_TICKS later; the owner's death
    ends it. Indexed by OWNER NPC slot. */
#define COLO_MAX_TOTEMS COLO_MAX_NPCS
#define COLO_TOTEM_HEAL_PCT 30        /* heal 30% of the owner's max HP per pulse */
#define COLO_TOTEM_HEAL_INTERVAL 7    /* D22: 7 ticks, mirroring the pinned wave-12 cadence */
#define COLO_TOTEM_RESPAWN_TICKS 200  /* 2 minutes after destruction (UPD-0410) */
#define COLO_TOTEM_SOL_HEAL 75        /* B5: flat heal to Sol per pulse on wave 12 */

typedef struct {
    ColoHazardPhase phase;
    int npc_slot;       /* live totem NPC slot (valid iff phase == ALIVE) */
    int respawn_timer;  /* ticks until respawn (valid iff phase == RESPAWNING) */
    int heal_timer;     /* ticks until the next heal pulse */
} ColoTotem;

/** Solarflare hazard: one damaging orb circles the distance-1 ring around each
    pillar. The four orbs share cadence state and exist on every wave, B1.
    A27 cadence: T1 moves every 2 ticks pausing 7 at
    corners, T2 every 2 ticks continuous, T3 every tick pausing 2 at corners.
    T3 also disables prayer on hit. D21: no damage numbers exist in any source,
    12/18/24 are the modeled tier values. */
#define COLO_SOLARFLARE_RING_STEPS       16
#define COLO_SOLARFLARE_MOVE_INTERVAL    2
#define COLO_SOLARFLARE_MOVE_INTERVAL_T3 1
#define COLO_SOLARFLARE_CORNER_PAUSE     7
#define COLO_SOLARFLARE_CORNER_PAUSE_T3  2
#define COLO_SOLARFLARE_MAX_HIT_T1 12
#define COLO_SOLARFLARE_MAX_HIT_T2 18
#define COLO_SOLARFLARE_MAX_HIT_T3 24

typedef struct {
    int active;
    int step;
    int move_timer;
    int pause_timer;
} ColoSolarflareOrb;

/** Doom: stacks gained per damage instance; reaching the tier cap (15/10/5) kills
    the player. Stacks reset between waves. */
static const int COLO_DOOM_CAP[4] = { 0, 15, 10, 5 };  /* indexed by tier (1..3) */

/** Volatility death explosion radius (tiles beyond the NPC footprint) by tier, and
    its contact max hit. T3 additionally leaves a until-wave-end molten pool.
    D20: no damage number or trigger timing exists in any source — flat 25 on
    the death tick is the modeled value. */
#define COLO_VOLATILITY_MAX_HIT 25       /* explosion contact damage (D20 modeled) */

/** general-arena molten pool mechanics are kinded because Reentry's pool is the
    stronger 1-15 every-other-tick variant, while Volatility keeps Sol's weaker
    per-tick molten sand damage. Lifetime is stored per pool so upgraded Reentry
    pools can persist across waves. */
typedef enum {
    COLO_POOL_VOLATILITY = 0,
    COLO_POOL_REENTRY,
} ColoMoltenPoolKind;

typedef enum {
    COLO_POOL_TEMPORARY = 0,
    COLO_POOL_PERMANENT,
} ColoMoltenPoolLifetime;

/** Relentless: defence-bypass fraction (percent) and flat max-hit bonus by tier. */
static const int COLO_RELENTLESS_DEF_BYPASS_PCT[4] = { 0, 33, 66, 100 };
static const int COLO_RELENTLESS_MAX_HIT_BONUS[4]  = { 0, 1, 3, 6 };

/** Blasphemy: prayer drained as a percent of damage taken, by tier. */
static const int COLO_BLASPHEMY_DRAIN_PCT[4] = { 0, 20, 40, 60 };

/** Frailty: max-HP reduction percent by tier (T1 also disables overheal). */
static const int COLO_FRAILTY_HP_CUT_PCT[4] = { 0, 10, 20, 40 };

/** Myopia: player attack-range reduction (tiles) by tier. */
static const int COLO_MYOPIA_RANGE_CUT[4] = { 0, 2, 4, 6 };

/** A25+E7: Mantimayhem T2 venom is STANDARD OSRS venom — first proc 6, each
    damage tick escalates +2 toward the 20 cap, damage every 30 ticks (18s);
    a reapplication while envenomed bumps the next damage +2 instead of
    resetting. The speedrun kit's sanfew cures it (L5), the beginner's serp
    helm blocks new applications while the melee set is worn (active-set
    check). Venom persists across waves and the tick timer freezes during wave
    gaps because the simulator abstracts gap time. */
#define COLO_VENOM_START 6      /* first venom hit */
#define COLO_VENOM_STEP 2       /* escalation per damage tick / reapplication */
#define COLO_VENOM_CAP 20       /* venom damage ceiling */
#define COLO_VENOM_INTERVAL 30  /* ticks between venom damage instances */

/** E8: bee contact applies standard poison starting at 1 damage. Poison ticks
    every 30 game ticks, damage is floor((severity + 4) / 5), each hit reduces
    severity by 1, and severity 0 expires. */
#define COLO_POISON_BEE_CONTACT_SEVERITY 5
#define COLO_POISON_INTERVAL 30


/** Sol Heredit AoE variants (A3, W-STRAT verbatim shapes; geometry lives in
    col_sol_aoe_tile_is_hazard). Consecutive same-type casts alternate variant
    1 -> 2; a type switch resets to variant 1 (A1). */
typedef enum {
    COLO_SOL_AOE_NONE = 0,
    COLO_SOL_AOE_SPEAR1,   /* 5x6 under + in front, 2 lines at the off-centre cols */
    COLO_SOL_AOE_SPEAR2,   /* 5x5 under him, 3 lines at the corner + centre cols */
    COLO_SOL_AOE_SHIELD1,  /* 7x7 block, safe ring at Chebyshev 4, hazard outside */
    COLO_SOL_AOE_SHIELD2,  /* 9x9 block, safe ring at Chebyshev 5, hazard outside */
} ColoSolAoeAttack;

/** Sol attack kinds for the A1 selection engine: the pool is {Spear, Spear,
    Shield, Shield} plus the phase-gated specials behind a 2-normal cooldown. */
typedef enum {
    COLO_SOL_ATTACK_NONE = 0,
    COLO_SOL_ATTACK_SPEAR,
    COLO_SOL_ATTACK_SHIELD,
    COLO_SOL_ATTACK_TRIPLE,
    COLO_SOL_ATTACK_GRAPPLE,
} ColoSolAttackKind;

/** A12: the grapple callout domain is EXACTLY these 5 equipment slots, drawn
    uniformly (colosim GRAPPLE_SLOTS + the W-STRAT callout list). */
typedef enum {
    COLO_GRAPPLE_SLOT_BODY = 0,   /* "I'LL CRUSH YOUR BODY"  -> platebody */
    COLO_GRAPPLE_SLOT_BACK,       /* "I'LL BREAK YOUR BACK"  -> cape */
    COLO_GRAPPLE_SLOT_HANDS,      /* "I'LL TWIST YOUR HANDS" -> gloves */
    COLO_GRAPPLE_SLOT_LEGS,       /* "I'LL BREAK YOUR LEGS"  -> platelegs */
    COLO_GRAPPLE_SLOT_FEET,       /* "I'LL CUT YOUR FEET"    -> boots */
    COLO_NUM_GRAPPLE_SLOTS
} ColoGrappleSlot;

#define COLO_SOL_HAZARD_TILES_MAX 32
#define COLO_SOL_SPHERE_QUEUE_MAX 8
#define COLO_SOL_MAX_CRYSTALS 4    /* D16: one per transition, phases 1-4 */
#define COLO_SOL_BEAM_MAX 32       /* >= 5 transitions x 6 beams */
#define COLO_SOL_PARRY_HITS 3
#define COLO_SOL_PRAYER_HISTORY 8  /* per-tick overhead ring, > max B4 lookback */
#define COLO_SOL_NUM_PHASES 6      /* >90 / <90 / <75 / <50 / <25 / <10 (enrage) */
#define COLO_SOL_HP_MAX 1500

/** one in-flight light sphere: marks the player's tile at launch and lands
    COLO_SOL_SPHERE_DELAY ticks later for 60-75 typeless, unprayable (A10+D15). */
typedef struct {
    int active;
    int tile_x, tile_y;
    int damage;
    int ticks_remaining;
} ColoSolSphere;

/** A9: one rotating crystal. Walks the boss-arena edge ring every
    COLO_SOL_CRYSTAL_MOVE_TICKS; when its cooldown expires it stops, telegraphs
    a beam for COLO_SOL_CRYSTAL_TELEGRAPH ticks, launches a sphere at the
    player's tile (pillar LoS gates the shot), then rolls a fresh 25-35t
    cooldown (12t at enrage). */
typedef struct {
    int active;
    int perim_step;       /* position index around the boss-arena edge ring */
    int move_timer;       /* ticks until the next ring step */
    int fire_cooldown;    /* ticks until the next telegraph starts */
    int telegraph_ticks;  /* >0 = stopped + telegraphing; fires at 0 */
} ColoSolCrystal;

/** A11: a phase-transition light beam; becomes a permanent molten pool after
    COLO_SOL_BEAM_TO_POOL_TICKS ticks. */
typedef struct {
    int active;
    int x, y;
    int ticks_to_pool;
} ColoSolBeam;

/** Sol Heredit boss sub-state: the A1 attack engine, A3/A15 AoE hazard frame,
    A20+B4 triple parry, A12+B7 grapple, A9-A11 arena hazards.
    grapple_body_slot holds a ColoGrappleSlot (5-slot domain, A12); the field
    name is load-bearing — osrs_render.h's KEY_B debug parry reads it. */
typedef struct {
    int started;               /* 1 once the boss machine has been initialised */
    int phase;                 /* 0..5 HP-threshold phase (>90/<90/<75/<50/<25/<10) */
    int boss_idx;              /* NPC slot of the live Sol, refreshed each tick (-1) */

    /* attack engine (A1+D28) */
    int attack_delay;          /* ticks until Sol may initiate his next attack */
    int immobile_ticks;        /* attack/transition animation freeze (A2+D17) */
    int special_cooldown;      /* normal attacks still owed before a special (A1) */
    int force_spear;           /* fight opener + after every phase transition (A1) */
    int last_attack_kind;      /* ColoSolAttackKind, drives variant alternation */
    int last_variant;          /* 1 or 2 within last_attack_kind */

    /* AoE hazard frame (A3+A15): analytic tile set anchored at cast time */
    int aoe_attack;            /* ColoSolAoeAttack live (or NONE) */
    int aoe_age;               /* ticks since the tiles appeared; bites at age 1 */
    int aoe_x, aoe_y;          /* boss SW tile at cast (he is frozen anyway) */
    int aoe_dir_x, aoe_dir_y;  /* spear-line cardinal, toward the player at cast */

    /* triple parry (A20+B4+D18): resolved here, NOT via the shared pending
       queue — the early-prayer punish needs the per-tick overhead history and
       each hit force-deactivates the overhead prayers. */
    int parry_land_in[COLO_SOL_PARRY_HITS];  /* ticks until each hit lands (0 = spent) */
    int parry_damage[COLO_SOL_PARRY_HITS];
    int parry_hits_left;
    uint8_t overhead_history[COLO_SOL_PRAYER_HISTORY];  /* tick % N -> protect overhead on */

    /* grapple (A12+A28+B7) */
    int grapple_active;        /* 1 while a grapple is in progress */
    int grapple_body_slot;     /* ColoGrappleSlot the player must click to parry */
    int grapple_timer;         /* 4-tick response window (4..1, 0 = lands) */
    int grapple_resolved;      /* 1 once parried/failed this grapple (latches result) */
    int next_attack_guaranteed_max; /* perfect parry: next player attack maxes (B7) */
    int guaranteed_max_ticks;  /* ticks left on the guaranteed-max window (<=5) */

    /* crystals + spheres (A9+A10) */
    ColoSolCrystal crystals[COLO_SOL_MAX_CRYSTALS];
    int crystal_count;         /* crystals spawned so far (1 per transition, D16) */
    ColoSolSphere spheres[COLO_SOL_SPHERE_QUEUE_MAX];

    /* beams + molten pools (A11+D25); pools are permanent for the fight */
    ColoSolBeam beams[COLO_SOL_BEAM_MAX];
    int hazard_tile_x[COLO_SOL_HAZARD_TILES_MAX];
    int hazard_tile_y[COLO_SOL_HAZARD_TILES_MAX];
    int hazard_tile_count;
    int enrage_pool_timer;     /* enrage: ticks until the next 1-per-3t pool */

    /* boss arena bounds, set when wave 12 starts (col_sol_begin_boss_arena). */
    int boss_arena_min_x, boss_arena_min_y;
    int boss_arena_max_x, boss_arena_max_y;
} SolHereditState;

/* A1+D28: per-attack next-attack delays (colosim SolHeredit.ts + tests). The
   fight speeds up below 75% (phase >= 2): spear 7->6, shield 6->5; the triple
   is 12, except 11 in the 50-75% band; grapple 7. */
#define COLO_SOL_SPEAR_DELAY        7
#define COLO_SOL_SPEAR_DELAY_FAST   6
#define COLO_SOL_SHIELD_DELAY       6
#define COLO_SOL_SHIELD_DELAY_FAST  5
#define COLO_SOL_TRIPLE_DELAY       12
#define COLO_SOL_TRIPLE_DELAY_MID   11
#define COLO_SOL_GRAPPLE_DELAY      7
/* A1: specials require 2 normal attacks since the last special. */
#define COLO_SOL_SPECIAL_COOLDOWN   2

/* A2+D17: Sol is frozen in place for the attack animation (colosim per-attack
   freezes; the triple holds him through its full hit schedule, modeled). */
#define COLO_SOL_SPEAR_FREEZE       6
#define COLO_SOL_SHIELD_FREEZE      4
#define COLO_SOL_GRAPPLE_FREEZE     5
#define COLO_SOL_TRANSITION_FREEZE  5   /* D28: stationary through the transition */

/* A3+D13: per-tile slam damage = 20 + rand(0..24) = 20-44 typeless. The same
   roll is the A28 grapple-fail hit. */
#define COLO_SOL_SLAM_MIN_HIT 20
#define COLO_SOL_SLAM_RAND    25
/* A15: hazard tiles bite exactly 1 tick after they appear and linger ~3 ticks
   as residue (colosim SolGroundSlam age==1, dying at 2). */
#define COLO_SOL_AOE_DAMAGE_AGE 1
#define COLO_SOL_AOE_LIFETIME   3
/* A3+D14: spear lines are 1 wide x 4 long, flush with the boss edge. */
#define COLO_SOL_SPEAR_LINE_LEN 4
/* A3: shield safe-ring Chebyshev radii from the boss centre. */
#define COLO_SOL_SHIELD1_RING 4
#define COLO_SOL_SHIELD2_RING 5

/* grapple (A12+A28+B7+D18): 4-tick response window; defending on the last
   window tick (grapple_timer <= 1) is a perfect parry arming a 5-tick
   guaranteed-max window the player-attack path consumes. */
#define COLO_SOL_GRAPPLE_WINDOW    4
#define COLO_SOL_PERFECT_MAX_TICKS 5

/* triple parry (A20 + C 6.13): hits at +3/+6/+9; the third slips to +4 gap
   below 50% HP, where 15/30/45 replaces 15/25/35. */
#define COLO_SOL_TP_HIT_GAP        3
#define COLO_SOL_TP_HIT3_GAP_LOW   4

/* crystals (A9+D16): move every 4 ticks, stop + telegraph 4 ticks before the
   sphere launches; per-crystal cooldown uniform 25-35t, 12t at enrage. */
#define COLO_SOL_CRYSTAL_MOVE_TICKS      4
#define COLO_SOL_CRYSTAL_TELEGRAPH       4
#define COLO_SOL_CRYSTAL_COOLDOWN_MIN    25
#define COLO_SOL_CRYSTAL_COOLDOWN_RAND   11
#define COLO_SOL_CRYSTAL_COOLDOWN_ENRAGE 12
/* sphere (A10+D15): flight 4 ticks, damage 60 + rand(0..15) = 60-75. */
#define COLO_SOL_SPHERE_DELAY    4
#define COLO_SOL_SPHERE_MIN_HIT  60
#define COLO_SOL_SPHERE_RAND     16

/* beams + molten sand (A11+D25): 6 beams per transition uniformly in the 9x9
   around the player, each a permanent pool after 2 ticks; enrage opens with 5
   pools then 1 per 3 ticks within +-4 of the player. */
#define COLO_SOL_BEAM_COUNT         6
#define COLO_SOL_BEAM_TO_POOL_TICKS 2
#define COLO_SOL_BEAM_SPREAD        4
#define COLO_SOL_ENRAGE_OPEN_POOLS  5
#define COLO_SOL_ENRAGE_POOL_SPREAD 4
#define COLO_SOL_MOLTEN_INTERVAL    3

/* A22 Sol molten sand burns 6 + rand(0..2) = 6-8 per tick stood on. Reentry
   molten is stronger and lives in the modifier pool constants below. */
#define COLO_MOLTEN_SAND_MIN_HIT 6
#define COLO_MOLTEN_SAND_RAND    3


static int col_type_is_warbander(ColoNpcType type) {
    return type == COLO_FREMENNIK_BERSERKER || type == COLO_FREMENNIK_ARCHER ||
           type == COLO_FREMENNIK_SEER;
}

/** Fremennik warbander state. valid iff col_type_is_warbander(type); reach it
    through colo_npc_warband(). formation_dir is the diamond slot the member
    holds around the player (B2(c)): species default N/E/W, the Quartet extra
    overridden to SOUTH at spawn. */
typedef struct {
    int formation_dir;   /* ColoWarbandFormation */
} ColoWarbandState;

/** manticore 3-orb barrage cycle. valid iff type is COLO_MANTICORE; reach it
    through colo_npc_manticore(). fixed_orb_style is the persistent spawn-time
    cycle. orb_style is the visible active telegraph and firing queue only.
    cycle_step counts ticks within a barrage: -1 = idle, 0/1/2 = next orb.
    D11: movement is NOT restricted while charging. */
typedef struct {
    int cycle_step;
    AttackStyle orb_style[3];
    AttackStyle fixed_orb_style[3];
    int pattern_copied;
} ColoManticoreState;

/** javelin skyfall cadence. Valid iff type is COLO_JAVELIN_COLOSSUS. Reach it
    through colo_npc_javelin(). Every 5th attack is a skyfall instead of a
    normal throw. The marked tile lands COLO_JAVELIN_SKYFALL_DELAY ticks later,
    ignoring Protect-from-Missiles. */
typedef struct {
    int attack_count;          /* attacks since spawn, every 5th is skyfall */
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
    int def_drained;       /* defence levels drained by player specs (elder maul) */

    /* type-specific state keyed on `type`; reach only via colo_npc_*(). */
    union {
        ColoManticoreState manticore; /* COLO_MANTICORE */
        ColoJavelinState javelin;     /* COLO_JAVELIN_COLOSSUS */
        ColoWarbandState warband;     /* col_type_is_warbander(type) */
    } type_state;

    EncounterPendingHitQueue pending_hits;

    int death_ticks;       /* death-animation linger, >0 = dying */
    int aggro_target;      /* -1 = player, >= 0 = NPC index */

    /* per-tick render flags (cleared at start of each tick) */
    int attacked_this_tick;
    AttackStyle attack_style_this_tick;
    int attack_visual_target;
    int moved_this_tick;
    int moved_last_tick;   /* prior tick's moved_this_tick; warband members attack
                              only after two consecutive stationary ticks (B2) */
    int hit_landed_this_tick;
    int hit_damage;
    int hit_was_successful_this_tick;
    int hit_spell_type;
} ColoNPC;

static inline ColoManticoreState* colo_npc_manticore(ColoNPC* npc) {
    assert(npc->type == COLO_MANTICORE);
    return &npc->type_state.manticore;
}

static inline ColoJavelinState* colo_npc_javelin(ColoNPC* npc) {
    assert(npc->type == COLO_JAVELIN_COLOSSUS);
    return &npc->type_state.javelin;
}

static inline ColoWarbandState* colo_npc_warband(ColoNPC* npc) {
    assert(col_type_is_warbander(npc->type));
    return &npc->type_state.warband;
}

/** B2(a): a species' attack-window offset inside the shared wave-anchored cycle. */
static inline int col_warband_window_offset(ColoNpcType type) {
    switch (type) {
        case COLO_FREMENNIK_BERSERKER: return 1;
        case COLO_FREMENNIK_SEER:      return 2;
        case COLO_FREMENNIK_ARCHER:    return 3;
        default:
            assert(0 && "warband window offset queried for a non-warbander");
            return 0;
    }
}

/** javelin skyfall lands 6 ticks after the 5th attack replaces the normal throw. */
#define COLO_JAVELIN_SKYFALL_DELAY 6
#define COLO_JAVELIN_SKYFALL_MAX_HIT 40
#define COLO_JAVELIN_SKYFALL_ANIM_ID 10893
/** a ready manticore delays its barrage start while another is mid-barrage. */
#define COLO_MANTICORE_STAGGER_TICKS 5
/** B10: from this wave (0-based; wave 9), a manticore's freshly-drawn orb
    pattern is copied by peers within COLO_MANTICORE_COPY_RANGE tiles
    (centre-to-centre) with line of sight. */
#define COLO_MANTICORE_COPY_MIN_WAVE 8
#define COLO_MANTICORE_COPY_RANGE 15
/** A13: minotaur heal reach, centre-to-centre tiles (the wiki's 6-from-edge
    phrasing reconciles to 7-from-centre for the 3x3 minotaur). */
#define COLO_MINOTAUR_HEAL_RANGE 7

/** write the spawn-time defaults for npc->type's type_state arm. caller must
    have zeroed the NPC and set npc->type first. */
static void colo_npc_init_type_state(ColoNPC* npc) {
    switch (npc->type) {
        case COLO_FREMENNIK_BERSERKER:
            npc->type_state.warband = (ColoWarbandState){ .formation_dir = COLO_WARBAND_FORM_NORTH };
            break;
        case COLO_FREMENNIK_SEER:
            npc->type_state.warband = (ColoWarbandState){ .formation_dir = COLO_WARBAND_FORM_EAST };
            break;
        case COLO_FREMENNIK_ARCHER:
            npc->type_state.warband = (ColoWarbandState){ .formation_dir = COLO_WARBAND_FORM_WEST };
            break;
        case COLO_MANTICORE:
            npc->type_state.manticore = (ColoManticoreState){
                .cycle_step = -1,
                .orb_style = { ATTACK_STYLE_NONE, ATTACK_STYLE_NONE, ATTACK_STYLE_NONE },
                .fixed_orb_style = { ATTACK_STYLE_NONE, ATTACK_STYLE_NONE, ATTACK_STYLE_NONE },
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
    COLO_GEAR_MAGIC,
    COLO_NUM_WEAPON_SETS
} ColoWeaponSet;

/** researched loadout profiles (.colosseum-notes/research/loadout-*.md):
    BEGINNER = wiki Budget tab tier-1 + budget example inventory (first-quiver
    floor), SPEEDRUN = wiki High efficiency tab (sub-20 kit). Spec weapons swap
    onto the melee gear base (L2); modeled decisions L1-L16 live in
    loadout-implementation-spec.md. A14 still holds: every melee weapon here is
    a real SLASH weapon (fang/scythe) so Sol's slash-5 weakness is reachable. */
typedef enum {
    COLO_LOADOUT_PROFILE_SPEEDRUN = 0,
    COLO_LOADOUT_PROFILE_BEGINNER,
    COLO_NUM_LOADOUT_PROFILES
} ColoLoadoutProfile;

typedef enum {
    COLO_LOADOUT_PROFILE_MODE_SPEEDRUN_ONLY = 0,
    COLO_LOADOUT_PROFILE_MODE_BEGINNER_ONLY,
    COLO_LOADOUT_PROFILE_MODE_MIXED,
} ColoLoadoutProfileMode;

typedef enum {
    COLO_RESTORE_SUPER_RESTORE = 0,
    COLO_RESTORE_SANFEW,        /* L5: sanfew serum — weaker formula, cures venom */
} ColoRestoreKind;

static const uint8_t COLO_BEGINNER_MELEE_LOADOUT[NUM_GEAR_SLOTS] = {
    [GEAR_SLOT_HEAD]   = ITEM_SERPENTINE_HELM,
    [GEAR_SLOT_CAPE]   = ITEM_INFERNAL_CAPE,
    [GEAR_SLOT_NECK]   = ITEM_AMULET_OF_BLOOD_FURY,
    [GEAR_SLOT_AMMO]   = ITEM_GOD_BLESSING,   /* L7: no phantom arrow str */
    [GEAR_SLOT_WEAPON] = ITEM_OSMUMTENS_FANG,
    [GEAR_SLOT_SHIELD] = ITEM_DRAGON_DEFENDER,
    [GEAR_SLOT_BODY]   = ITEM_FIGHTER_TORSO,
    [GEAR_SLOT_LEGS]   = ITEM_CRYSTAL_LEGS,
    [GEAR_SLOT_HANDS]  = ITEM_BARROWS_GLOVES,
    [GEAR_SLOT_FEET]   = ITEM_ECHO_BOOTS,
    [GEAR_SLOT_RING]   = ITEM_BERSERKER_RING_I,
};

static const uint8_t COLO_BEGINNER_RANGED_LOADOUT[NUM_GEAR_SLOTS] = {
    [GEAR_SLOT_HEAD]   = ITEM_CRYSTAL_HELM,
    [GEAR_SLOT_CAPE]   = ITEM_INFERNAL_CAPE,
    [GEAR_SLOT_NECK]   = ITEM_NECKLACE_OF_ANGUISH,
    [GEAR_SLOT_AMMO]   = ITEM_GOD_BLESSING,
    [GEAR_SLOT_WEAPON] = ITEM_BOW_OF_FAERDHINEN,
    [GEAR_SLOT_SHIELD] = ITEM_NONE,  /* bowfa is 2h */
    [GEAR_SLOT_BODY]   = ITEM_CRYSTAL_BODY,
    [GEAR_SLOT_LEGS]   = ITEM_CRYSTAL_LEGS,
    [GEAR_SLOT_HANDS]  = ITEM_BARROWS_GLOVES,
    [GEAR_SLOT_FEET]   = ITEM_ECHO_BOOTS,
    [GEAR_SLOT_RING]   = ITEM_BERSERKER_RING_I,
};

/** budget magic set = the budget general/melee armor with the weapon swapped to
    the Trident of the swamp (1h, so the Dragon defender shield is kept). The
    budget kit carries NO Occult, so this set's gear magic_damage% is 0 and the
    Trident two-shots the melee frem off its 31 base. Faithful to the kit (the
    wiki budget magic example is literally Trident-on-the-melee-base). */
static const uint8_t COLO_BEGINNER_MAGIC_LOADOUT[NUM_GEAR_SLOTS] = {
    [GEAR_SLOT_HEAD]   = ITEM_SERPENTINE_HELM,
    [GEAR_SLOT_CAPE]   = ITEM_INFERNAL_CAPE,
    [GEAR_SLOT_NECK]   = ITEM_AMULET_OF_BLOOD_FURY,
    [GEAR_SLOT_AMMO]   = ITEM_GOD_BLESSING,
    [GEAR_SLOT_WEAPON] = ITEM_TRIDENT_OF_SWAMP,
    [GEAR_SLOT_SHIELD] = ITEM_DRAGON_DEFENDER,  /* Trident is 1h */
    [GEAR_SLOT_BODY]   = ITEM_FIGHTER_TORSO,
    [GEAR_SLOT_LEGS]   = ITEM_CRYSTAL_LEGS,
    [GEAR_SLOT_HANDS]  = ITEM_BARROWS_GLOVES,
    [GEAR_SLOT_FEET]   = ITEM_ECHO_BOOTS,
    [GEAR_SLOT_RING]   = ITEM_BERSERKER_RING_I,
};

static const uint8_t COLO_SPEEDRUN_MELEE_LOADOUT[NUM_GEAR_SLOTS] = {
    [GEAR_SLOT_HEAD]   = ITEM_TORVA_FULL_HELM,
    [GEAR_SLOT_CAPE]   = ITEM_INFERNAL_CAPE,
    [GEAR_SLOT_NECK]   = ITEM_AMULET_OF_RANCOUR,
    [GEAR_SLOT_AMMO]   = ITEM_DRAGON_ARROWS,
    [GEAR_SLOT_WEAPON] = ITEM_SCYTHE_OF_VITUR,
    [GEAR_SLOT_SHIELD] = ITEM_NONE,  /* scythe is 2h */
    [GEAR_SLOT_BODY]   = ITEM_OATHPLATE_CHEST,
    [GEAR_SLOT_LEGS]   = ITEM_OATHPLATE_LEGS,
    [GEAR_SLOT_HANDS]  = ITEM_FEROCIOUS_GLOVES,
    [GEAR_SLOT_FEET]   = ITEM_AVERNIC_TREADS,
    [GEAR_SLOT_RING]   = ITEM_ULTOR_RING,
};

static const uint8_t COLO_SPEEDRUN_RANGED_LOADOUT[NUM_GEAR_SLOTS] = {
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
    [GEAR_SLOT_RING]   = ITEM_ULTOR_RING,
};

/** high-efficiency magic set = the speedrun general/melee armor with the weapon
    swapped to Tumeken's shadow (2h, so no shield), the Occult necklace, and the
    Confliction gauntlets the kit carries as its mage hand switch. Shadow triples
    this set's gear magic_damage% (Occult 5 + Confliction 7 + Avernic treads 2 =
    14 -> 42). */
static const uint8_t COLO_SPEEDRUN_MAGIC_LOADOUT[NUM_GEAR_SLOTS] = {
    [GEAR_SLOT_HEAD]   = ITEM_TORVA_FULL_HELM,
    [GEAR_SLOT_CAPE]   = ITEM_INFERNAL_CAPE,
    [GEAR_SLOT_NECK]   = ITEM_OCCULT_NECKLACE,
    [GEAR_SLOT_AMMO]   = ITEM_DRAGON_ARROWS,
    [GEAR_SLOT_WEAPON] = ITEM_TUMEKENS_SHADOW,
    [GEAR_SLOT_SHIELD] = ITEM_NONE,  /* shadow is 2h */
    [GEAR_SLOT_BODY]   = ITEM_OATHPLATE_CHEST,
    [GEAR_SLOT_LEGS]   = ITEM_OATHPLATE_LEGS,
    [GEAR_SLOT_HANDS]  = ITEM_CONFLICTION_GAUNTLETS,
    [GEAR_SLOT_FEET]   = ITEM_AVERNIC_TREADS,
    [GEAR_SLOT_RING]   = ITEM_ULTOR_RING,
};

static const uint8_t* const COLO_SPEEDRUN_LOADOUTS[COLO_NUM_WEAPON_SETS] = {
    COLO_SPEEDRUN_MELEE_LOADOUT,
    COLO_SPEEDRUN_RANGED_LOADOUT,
    COLO_SPEEDRUN_MAGIC_LOADOUT,
};

static const uint8_t* const COLO_BEGINNER_LOADOUTS[COLO_NUM_WEAPON_SETS] = {
    COLO_BEGINNER_MELEE_LOADOUT,
    COLO_BEGINNER_RANGED_LOADOUT,
    COLO_BEGINNER_MAGIC_LOADOUT,
};

/** powered-staff base max hit fed as spell_base_damage at the magic compute site
    (the magic max-hit formula is base*(1+magic_damage%), magic LEVEL only feeds
    accuracy). Trident of the swamp = floor(99/3)-2 = 31; Tumeken's shadow =
    floor(99/3)+1 = 34, both at the env's hardcoded base_level 99. */
static const int COLO_MAGIC_SPELL_BASE_DAMAGE[COLO_NUM_LOADOUT_PROFILES] = {
    [COLO_LOADOUT_PROFILE_SPEEDRUN] = 34,
    [COLO_LOADOUT_PROFILE_BEGINNER] = 31,
};

static const uint8_t* const* const COLO_LOADOUT_PROFILES[COLO_NUM_LOADOUT_PROFILES] = {
    [COLO_LOADOUT_PROFILE_SPEEDRUN] = COLO_SPEEDRUN_LOADOUTS,
    [COLO_LOADOUT_PROFILE_BEGINNER] = COLO_BEGINNER_LOADOUTS,
};

/** full starting supplies per profile, in DOSES (4 per potion). Beginner = the
    wiki budget example (6 brews, 8 super restores, regular combat, regular
    ranging). Speedrun = High efficiency (1 brew, 7 sanfews, 1 divine super
    combat, 1 divine ranging, 1 surge potion). E13 supersedes the old L4
    base-boost shortcut for the speedrun kit's divine doses.
    No solid food and no staminas exist in ANY wiki colosseum inventory (L8/L14). */
typedef enum {
    COLO_BOOST_POTION_REGULAR = 0,
    COLO_BOOST_POTION_DIVINE,
} ColoBoostPotionKind;

typedef struct {
    int brew_doses;
    int restore_doses;
    int combat_doses;
    int ranged_doses;
    int surge_doses;
    ColoBoostPotionKind combat_boost_kind;
    ColoBoostPotionKind ranged_boost_kind;
    ColoRestoreKind restore_kind;
} ColoSupplyLoadout;

static const ColoSupplyLoadout COLO_SUPPLY_LOADOUTS[COLO_NUM_LOADOUT_PROFILES] = {
    [COLO_LOADOUT_PROFILE_SPEEDRUN] = {
        .brew_doses = 4, .restore_doses = 28, .combat_doses = 4,
        .ranged_doses = 4, .surge_doses = 4,
        .combat_boost_kind = COLO_BOOST_POTION_DIVINE,
        .ranged_boost_kind = COLO_BOOST_POTION_DIVINE,
        .restore_kind = COLO_RESTORE_SANFEW,
    },
    [COLO_LOADOUT_PROFILE_BEGINNER] = {
        .brew_doses = 24, .restore_doses = 32, .combat_doses = 8,
        .ranged_doses = 8, .surge_doses = 0,
        .combat_boost_kind = COLO_BOOST_POTION_REGULAR,
        .ranged_boost_kind = COLO_BOOST_POTION_REGULAR,
        .restore_kind = COLO_RESTORE_SUPER_RESTORE,
    },
};

static const uint8_t* const* col_loadouts_for_profile(int profile) {
    if (profile < 0 || profile >= COLO_NUM_LOADOUT_PROFILES) {
        fprintf(stderr, "colosseum: invalid loadout profile %d\n", profile);
        abort();
    }
    return COLO_LOADOUT_PROFILES[profile];
}

#define COLO_INVENTORY_DISPLAY_SLOTS 28
#if COLO_INVENTORY_DISPLAY_SLOTS != OSRS_INVENTORY_SIZE
#error "Colosseum inventory size must match shared OSRS inventory size"
#endif

typedef OsrsInventoryCell ColoInvCell;

/** Canonical gameplay inventory per kit, as raw OSRS item ids.
    This 28-slot bag seeds the shared slot model at reset and the renderer reads
    live cells back from that model. Doses use the GUI's dose-specific ids. */
static const int COLO_GAMEPLAY_INVENTORY_LOADOUT[COLO_NUM_LOADOUT_PROFILES][COLO_INVENTORY_DISPLAY_SLOTS] = {
    [COLO_LOADOUT_PROFILE_SPEEDRUN] = {
        20997, 27238, 27235, 28947,   /* Twisted bow, Masori body(f), mask(f), Dizana's quiver */
        26235, 27241, 19547, 13652,   /* Zaryte vambraces, Masori chaps(f), Anguish, Dragon claws */
        27275, 31106, 27610, 24780,   /* Tumeken's shadow, Confliction gauntlets, Venator bow,
                                         Amulet of blood fury */
        30875, 12002, 12006, 21003,   /* Surge potion(4), Occult necklace, Abyssal tentacle, Elder maul */
        23685, 23733, 6685, 4417,     /* Divine super combat(4), Divine ranging(4), Sara brew(4),
                                         Guthix rest(4) */
        10925, 10925, 10925, 10925,   /* Sanfew serum(4) x7 */
        10925, 10925, 10925, 27281,   /* ...continued, Divine rune pouch */
    },
    [COLO_LOADOUT_PROFILE_BEGINNER] = {
        27610, 26219, 25865, 19547,   /* Venator bow, Fang, Bowfa, Anguish */
        11806, 13652, 23975, 23971,   /* SGS, Dragon claws, Crystal body, Crystal helm */
        6685, 6685, 3024, 3024,       /* Sara brew(4) x2, Super restore(4) x2 */
        6685, 6685, 3024, 3024,
        6685, 6685, 3024, 3024,
        12695, 2444, 3024, 3024,      /* Super combat(4), Ranging(4), Super restore(4) x2 */
        23685, 23733, 12006, 27281,   /* Divine super combat(4), Divine ranging(4),
                                         Abyssal tentacle, Divine rune pouch */
    },
};

typedef void (*ColRawOsrsIdVisitor)(int raw_osrs_id, void* ctx);

static const int COLO_NON_CONSUMABLE_DISPLAY_RAW_OSRS_IDS[] = {
    27610,
    12006,
    27281,
};

static int col_raw_osrs_id_seen(const int* seen, int seen_count, int raw_osrs_id) {
    for (int i = 0; i < seen_count; i++) {
        if (seen[i] == raw_osrs_id) return 1;
    }
    return 0;
}

static void col_visit_raw_osrs_id_once(
    int raw_osrs_id,
    int* seen,
    int* seen_count,
    int seen_capacity,
    ColRawOsrsIdVisitor visit,
    void* ctx
) {
    if (raw_osrs_id <= 0 || col_raw_osrs_id_seen(seen, *seen_count, raw_osrs_id)) {
        return;
    }
    if (*seen_count >= seen_capacity) {
        fprintf(stderr, "colosseum sprite id visitor capacity exceeded\n");
        abort();
    }
    seen[(*seen_count)++] = raw_osrs_id;
    visit(raw_osrs_id, ctx);
}

/**
 * Visits every dose-specific raw id reachable from colosseum gameplay loadouts.
 */
static void col_for_each_reachable_consumable_dose_raw_osrs_id(
    ColRawOsrsIdVisitor visit,
    void* ctx
) {
    if (!visit) {
        fprintf(stderr, "colosseum consumable dose visitor is null\n");
        abort();
    }

    enum {
        REGISTRY_COUNT = (int)(
            sizeof(OSRS_CONSUMABLE_CLICK_REGISTRY) /
            sizeof(OSRS_CONSUMABLE_CLICK_REGISTRY[0])
        )
    };
    int seen[REGISTRY_COUNT];
    int seen_count = 0;

    for (int p = 0; p < COLO_NUM_LOADOUT_PROFILES; p++) {
        for (int slot = 0; slot < COLO_INVENTORY_DISPLAY_SLOTS; slot++) {
            uint16_t raw_osrs_id =
                (uint16_t)COLO_GAMEPLAY_INVENTORY_LOADOUT[p][slot];
            OsrsConsumableClick root =
                osrs_consumable_click_lookup_raw_osrs_id(raw_osrs_id);
            if (root.click_action != OSRS_CLICK_DRINK || root.dose_count == 0) {
                continue;
            }

            uint16_t current_raw_osrs_id = raw_osrs_id;
            for (int step = 0; current_raw_osrs_id != 0; step++) {
                if (step >= REGISTRY_COUNT) {
                    fprintf(stderr, "colosseum consumable dose chain cycles at raw id %u\n",
                        current_raw_osrs_id);
                    abort();
                }
                OsrsConsumableClick current =
                    osrs_consumable_click_lookup_raw_osrs_id(current_raw_osrs_id);
                if (current.click_action != OSRS_CLICK_DRINK ||
                        current.dose_count == 0 ||
                        current.consumable_kind != root.consumable_kind) {
                    fprintf(stderr, "colosseum consumable dose chain broke at raw id %u\n",
                        current_raw_osrs_id);
                    abort();
                }
                col_visit_raw_osrs_id_once(
                    current_raw_osrs_id,
                    seen,
                    &seen_count,
                    REGISTRY_COUNT,
                    visit,
                    ctx);
                current_raw_osrs_id =
                    osrs_consumable_raw_osrs_id_after_drink(current_raw_osrs_id);
            }
        }
    }
}

/**
 * Visits every raw item id that the colosseum display inventory must sprite-load.
 */
static void col_for_each_display_inventory_sprite_raw_osrs_id(
    ColRawOsrsIdVisitor visit,
    void* ctx
) {
    if (!visit) {
        fprintf(stderr, "colosseum display sprite visitor is null\n");
        abort();
    }

    enum {
        DISPLAY_CAPACITY =
            (int)(sizeof(COLO_NON_CONSUMABLE_DISPLAY_RAW_OSRS_IDS) /
                  sizeof(COLO_NON_CONSUMABLE_DISPLAY_RAW_OSRS_IDS[0])) +
            (int)(sizeof(OSRS_CONSUMABLE_CLICK_REGISTRY) /
                  sizeof(OSRS_CONSUMABLE_CLICK_REGISTRY[0]))
    };
    int seen[DISPLAY_CAPACITY];
    int seen_count = 0;

    for (int i = 0; i < (int)(sizeof(COLO_NON_CONSUMABLE_DISPLAY_RAW_OSRS_IDS) /
            sizeof(COLO_NON_CONSUMABLE_DISPLAY_RAW_OSRS_IDS[0])); i++) {
        col_visit_raw_osrs_id_once(
            COLO_NON_CONSUMABLE_DISPLAY_RAW_OSRS_IDS[i],
            seen,
            &seen_count,
            DISPLAY_CAPACITY,
            visit,
            ctx);
    }
    col_for_each_reachable_consumable_dose_raw_osrs_id(visit, ctx);
}

/** Arceuus thrall (one allied auto-attacker, full fidelity): greater-thrall max
    hit 3 both kits, uniform 0..3 (accuracy ignored), 4-tick cadence, 17-tick
    recast gate. Damage routes through col_queue_npc_pending_hit (source -1) so
    kills + damage credit the player. Per-kit lifetime: high-eff carries the
    Master combat-achievement x2 (198 ticks); budget runs the base 99. Sol
    Heredit is immune (never summon-target, never damaged). */
#define COLO_THRALL_MAX_HIT  3
#define COLO_THRALL_TICK     4
#define COLO_THRALL_RECAST_CD 17
static const int COLO_THRALL_LIFETIME_TICKS[COLO_NUM_LOADOUT_PROFILES] = {
    [COLO_LOADOUT_PROFILE_SPEEDRUN] = 198,
    [COLO_LOADOUT_PROFILE_BEGINNER] = 99,
};

/** Death Charge (Arceuus): cast arms a 100-tick window; the first player-credited
    killing blow inside it grants +15% spec energy and starts the 100-tick
    cooldown. An unused window closes silently WITHOUT starting the cooldown. */
#define COLO_DEATH_CHARGE_WINDOW   100
#define COLO_DEATH_CHARGE_CD       100
#define COLO_DEATH_CHARGE_SPEC_GAIN 15

/** sample the episode's loadout profile from the config mode (L9). */
static ColoLoadoutProfile col_sample_loadout_profile(
    int mode, float beginner_fraction, uint32_t* rng_state
) {
    if (mode == COLO_LOADOUT_PROFILE_MODE_SPEEDRUN_ONLY)
        return COLO_LOADOUT_PROFILE_SPEEDRUN;
    if (mode == COLO_LOADOUT_PROFILE_MODE_BEGINNER_ONLY)
        return COLO_LOADOUT_PROFILE_BEGINNER;
    if (mode != COLO_LOADOUT_PROFILE_MODE_MIXED) {
        fprintf(stderr, "colosseum: invalid loadout_profile_mode %d\n", mode);
        abort();
    }
    if (!(beginner_fraction >= 0.0f && beginner_fraction <= 1.0f)) {
        fprintf(stderr, "colosseum: beginner_loadout_fraction must be in [0,1], got %.6f\n",
                beginner_fraction);
        abort();
    }
    return encounter_rand_float(rng_state) < beginner_fraction
        ? COLO_LOADOUT_PROFILE_BEGINNER
        : COLO_LOADOUT_PROFILE_SPEEDRUN;
}

/* L13: surge potion — +25 spec energy per dose, one dose per 500 ticks (300s);
   the cooldown is frozen during wave gaps (wiki: "timer pauses between waves
   in the Fortis Colosseum"). */
#define COLO_SURGE_RESTORE_AMOUNT 25
#define COLO_SURGE_COOLDOWN_TICKS 500


/** Colosseum env-knob struct. Sparse-leaning reward terms; the lead tunes these.
    Dense damage terms stay small (PPO clamps per-step rewards). Death/terminal
    penalty defaults off to avoid suicide-perversity (sparse > dense). */
typedef struct {
    int start_wave;        /* internal wave index (0-based) */
    float damage_reward_coeff;       /* per-point reward for damage dealt to NPCs */
    float boss_damage_reward_coeff;  /* per-point reward for damage dealt to Sol */
    float wave_clear_bonus;          /* per-wave clear reward */
    float boss_phase_bonus;          /* per Sol HP-threshold phase crossed */
    float win_bonus;                 /* on Sol death (wave 12 cleared) */
    float prayer_correct_reward;     /* per correctly-prayed incoming hit */
    float offpray_damage_penalty_coeff; /* per-HP penalty for off-prayer damage taken
                                           this tick: immediate credit for praying/
                                           positioning to avoid hits (default 0). */
    float avoided_damage_coeff;
    float death_penalty_coeff;       /* magnitude of the terminal death reward */
    float timeout_penalty;
    int curriculum_agent;
    int loadout_profile_mode;          /* ColoLoadoutProfileMode (L9) */
    float beginner_loadout_fraction;   /* P(beginner) in MIXED mode */
    int step_out_forecast_obs_enabled;
    int forecast_horizon;              /* B1: step-out rollout depth, 1..MAX (default MAX) */
    int forecast_run_tile_mode;        /* ColoForecastRunTileMode (B3, default FULL) */
    int mask_inventory_heads;
    int action_debug_log;
    int prayer_oracle_mode;            /* scaffold: 1 = override the agent's overhead
                                          each tick with the perfect protect-prayer vs
                                          this tick's throwers (magic>melee>range).
                                          Default 0. Solve-by-cheating ablation knob. */
    int bis_gear_oracle_mode;          /* scaffold: 1 = force-equip the max-DPT
                                          weapon set vs the current target each tick.
                                          Default 0. Solve-by-cheating ablation knob. */
    int invuln_mode;                   /* scaffold: 1 = the player cannot die (HP
                                          restored to full + doom-lethal suppressed
                                          before each death check; timeout still
                                          applies). Default 0. Isolates whether the
                                          wall is survival vs damage/progression. */
    int episode_max_ticks_override;    /* scaffold: >0 forces the per-episode tick
                                          budget to this value for ALL loadouts,
                                          overriding the speedrun/beginner defaults.
                                          0 = keep the loadout default (bit-identical).
                                          Probes whether the timeout is the wall. */
    int remove_brews;                  /* scaffold: 1 = strip all Saradomin-brew
                                          doses from the seeded inventory (brews are
                                          the only HP-healing consumable). 0 =
                                          bit-identical. Probes the healing-crutch
                                          plateau hypothesis. */
} ColConfig;

/** B3: how the step-out forecast evaluates the 16 run-tile actions (9-24).
    FULL runs the same multi-tick rollout as walk tiles. STATIC_THREAT skips NPC
    movement and records a single static range/LoS attack gate at the true run
    landing, trading horizon fidelity for the cost of the run-tile rollouts. */
typedef enum {
    COLO_FORECAST_RUN_TILE_FULL = 0,
    COLO_FORECAST_RUN_TILE_STATIC_THREAT = 1,
} ColoForecastRunTileMode;

typedef enum {
    COLO_NPC_RENDER_ATTACK_NORMAL = 0,
    COLO_NPC_RENDER_ATTACK_JAVELIN_SKYFALL,
} ColoNpcRenderAttackAnim;

typedef struct {
    int active;
    int npc_slot;
    int tile_x;
    int tile_y;
    int damage;
} ColoJavelinSkyfallLandingRenderEvent;

typedef struct {
    ColConfig config;
    const CollisionMap* collision_map;
    int world_offset_x;
    int world_offset_y;
    Log* log;
    const HumanCommand* human_commands;
    int human_command_count;
    int human_command_mode;
    ColoNpcRenderAttackAnim npc_render_attack_anim[COLO_MAX_NPCS];
    int manticore_charge_started[COLO_MAX_NPCS];
    ColoJavelinSkyfallLandingRenderEvent javelin_skyfall_landing[COLO_MAX_NPCS];
    int player_render_hit_count;
    int player_render_hit_damage[ENCOUNTER_RENDER_HITS_MAX];
    int npc_render_hit_count[COLO_MAX_NPCS];
    int npc_render_hit_damage[COLO_MAX_NPCS][ENCOUNTER_RENDER_HITS_MAX];
    /* render-only: set on the tick the allied thrall queues its magic hit, so the
       render-entity emitter plays its attack anim and the projectile emitter fires
       a tracking magic bolt. Not serialized; the sim never reads it. */
    int thrall_attacked_this_tick;
    /* render-only: the equipped spec weapon item index fired this tick. */
    int player_spec_fired_kind;
    int player_venator_chain_count;
    int player_venator_chain_slots[OSRS_VENATOR_MAX_CHAIN_HITS];
} ColosseumContext;

static inline void col_context_clear_render_events(ColosseumContext* ctx) {
    if (!ctx) return;
    memset(ctx->npc_render_attack_anim, 0, sizeof(ctx->npc_render_attack_anim));
    memset(ctx->manticore_charge_started, 0, sizeof(ctx->manticore_charge_started));
    memset(ctx->javelin_skyfall_landing, 0, sizeof(ctx->javelin_skyfall_landing));
    ctx->player_render_hit_count = 0;
    memset(ctx->player_render_hit_damage, 0, sizeof(ctx->player_render_hit_damage));
    memset(ctx->npc_render_hit_count, 0, sizeof(ctx->npc_render_hit_count));
    memset(ctx->npc_render_hit_damage, 0, sizeof(ctx->npc_render_hit_damage));
    ctx->thrall_attacked_this_tick = 0;
    ctx->player_spec_fired_kind = 0;
    ctx->player_venator_chain_count = 0;
    for (int i = 0; i < OSRS_VENATOR_MAX_CHAIN_HITS; i++)
        ctx->player_venator_chain_slots[i] = -1;
}

/** Record the Venator chain for same-tick projectile emission only. */
static inline void col_context_record_venator_chain(
    ColosseumContext* ctx,
    const OsrsVenatorChain* chain
) {
    if (!ctx) abort();
    if (!chain) abort();
    osrs_venator_validate_chain_length(chain->length);
    ctx->player_venator_chain_count = (int)chain->length;
    for (int i = 0; i < OSRS_VENATOR_MAX_CHAIN_HITS; i++)
        ctx->player_venator_chain_slots[i] = -1;
    for (int i = 0; i < (int)chain->length; i++)
        ctx->player_venator_chain_slots[i] = chain->hits[i].slot;
}

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
    /* diagnostic: which prayer-checkable styles landed on the player THIS tick
       (bitmask over ATTACK_STYLE_*) and the off-prayer HP taken this tick. >=2
       distinct styles = a same-tick conflict a single overhead cannot fully
       block (unavoidable bleed); 1 style = an avoidable mis-flick. */
    int styles_faced_mask;
    float offpray_damage_this_tick;
    float avoidable_threat_by_style[4];
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
    int modifiers_picked;      /* mandatory draft selections made this run */
    int active_tier_sum;       /* sum of active modifier tiers (run difficulty proxy) */

    /* per-NPC-type prayer outcomes ("who is hitting us off-prayer"): faced =
       prayer-checkable hits that landed, correct = overhead matched at land
       time, offpray_damage = HP lost to mismatched-overhead hits. Floats so
       the binding can aggregate per-episode means directly. */
    float pray_faced_by_type[COLO_NUM_NPC_TYPES];
    float pray_correct_by_type[COLO_NUM_NPC_TYPES];
    float offpray_damage_by_type[COLO_NUM_NPC_TYPES];
    /** Total damage taken from each NPC type this episode for all hits: prayed, off-prayer, and ignore-prayer or typeless, unlike offpray_damage_by_type. */
    float total_damage_by_type[COLO_NUM_NPC_TYPES];

    /* death attribution (diagnostic): death_by_type[killer] = 1 on the episode's
       fatal tick (which NPC type dealt the killing blow), death_fatal_damage =
       that tick's total damage_received (single big hit vs same-tick combo). */
    float death_by_type[COLO_NUM_NPC_TYPES];
    float death_fatal_damage;

    /* bleed avoidability (diagnostic): off-prayer HP taken on ticks with >=2
       distinct prayer-checkable styles landing (conflict = unavoidable, single
       overhead cannot block both) vs ticks with 1 style (solo = an avoidable
       mis-flick). death_on_conflict_tick = 1 if the fatal tick was a conflict. */
    float offpray_damage_conflict;
    float offpray_damage_solo;
    float death_on_conflict_tick;
    float outcome_score;
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
    int episode_max_ticks;
    int reinforcement_timer;  /* 40s (66-tick) reinforcement countdown */
    int warband_cycle_anchor; /* B2(a): the wave's first actionable tick N (-1 until live) */
    int current_wave_kills;
    int current_wave_total_killable;
    float current_wave_kill_progress;
    int episode_over;
    int time_limit_truncated;
    ColoOutcome winner;       /* valid only when episode_over is set */

    ColoModifierState modifiers;
    SolHereditState sol;

    /* modifier runtime state (zeroed each reset; a fresh run carries none). */
    int doom_stacks;                       /* Doom: reset to 0 at each wave spawn */
    ColoBeeSwarm bees[COLO_MAX_BEE_SWARMS];
    ColoTotem totems[COLO_MAX_TOTEMS];     /* indexed by OWNER NPC slot */
    ColoSolarflareOrb solarflare;
    int player_venom;                      /* Mantimayhem T2: next venom damage (0 = none) */
    int player_venom_timer;                /* ticks until the next venom instance */
    int player_poison;                     /* E8: standard poison severity (0 = none) */
    int player_poison_timer;               /* ticks until the next poison instance */
    /* general-arena molten pools (Reentry / Volatility T3) for waves 1-11. Boss
       wave reuses SolHereditState.hazard_tile_*; this list is the non-boss analog. */
    int molten_x[COLO_SOL_HAZARD_TILES_MAX];
    int molten_y[COLO_SOL_HAZARD_TILES_MAX];
    int molten_kind[COLO_SOL_HAZARD_TILES_MAX];
    int molten_lifetime[COLO_SOL_HAZARD_TILES_MAX];
    int molten_burn_timer[COLO_SOL_HAZARD_TILES_MAX];
    int molten_count;

    /* reward bookkeeping */
    float reward;
    float episode_return;
    int last_hit_by_type;     /* NPC type that last damaged the player (-1 = none) */
    int rewarded_boss_phase;  /* highest Sol phase already paid a boss_phase_bonus */
    float avoided_damage_already_rewarded;
    int min_sol_hp_seen;

    /* player combat state */
    OsrsInteraction interaction;
    int player_last_interaction_target_slot;
    int player_last_interaction_age;
    ColoWeaponSet weapon_set;
    EncounterLoadoutStats loadout_stats_live;
    OsrsEquipmentEffectProfile effects_live;
    int loadout_stats_dirty;

    /* loadout profile + inventory state (researched setups, L1-L16) */
    ColoLoadoutProfile active_loadout_profile;
    ColoSupplyLoadout full_supplies;   /* obs normalization denominators */
    /** E13: Colosseum-owned live-gameplay timers for 100-tick stat drift and
        divine boost holds. The obs vector intentionally stays unchanged. */
    int stat_drift_timer;
    int divine_combat_timer;
    int divine_ranged_timer;
    int surge_doses;
    int surge_cooldown;                /* ticks until the next surge dose (L13) */
    OsrsInventoryCell inventory_cells[OSRS_INVENTORY_SIZE];

    /* Arceuus thrall (one allied auto-attacker; not a ColoNPC slot). */
    int thrall_active;          /* 0/1 */
    int thrall_target_slot;     /* NPC array index it auto-attacks, -1 = none */
    int thrall_lifetime_left;   /* ticks until despawn (set per profile on summon) */
    int thrall_attack_timer;    /* ticks until the next thrall hit (4-tick cadence) */
    int thrall_recast_cd;       /* ticks until re-summon allowed (17) */

    /* Death Charge armed window + cooldown. */
    int death_charge_window_left; /* ticks left in the armed window, 0 = not armed */
    int death_charge_cd;          /* ticks until Death Charge can re-arm, 0 = ready */

    /* per-tick player attack event for the renderer */
    int player_attack_npc_idx;
    int player_attack_dmg;
    AttackStyle player_attack_style_id;
    EncounterProjectileTiming player_attack_timing;

    /* pending hits on the player from NPC attacks (projectiles in flight) */
    EncounterPendingHitQueue player_pending_hits;

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

static inline void col_damage_player_from(ColosseumState* s, int dmg, int npc_type);
static void col_apply_bis_gear_oracle(ColosseumState* s); /* scaffold; defined in obs_mask.inc */

static void col_build_live_inventory_display(
    const ColosseumState* s,
    int out[COLO_INVENTORY_DISPLAY_SLOTS]
) {
    for (int i = 0; i < COLO_INVENTORY_DISPLAY_SLOTS; i++)
        out[i] = s->inventory_cells[i].raw_osrs_id;
}
