/**
 * @file encounter_inferno.h
 * @brief The Inferno — 69-wave PvM challenge with prayer switching and pillar safespotting.
 *
 * core mechanic: 3 destructible pillars block NPC projectiles. the player must
 * position behind pillars to limit incoming attacks to one prayer style at a time.
 * nibblers eat pillars, meleer can dig through them. losing all pillars = death spiral.
 *
 * monster types: nibbler (pillar eater), bat (short-range ranger), blob (prayer reader,
 * splits on death), meleer (burrows to player), ranger, mager (heals others),
 * jad (alternating range/mage), zuk (final boss with shield mechanic).
 *
 * reference: docs/inferno-specs.md, .refs/osrs-sdk/, runelite inferno plugin
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

#define INF_ARENA_WIDTH   46
#define INF_ARENA_HEIGHT  54
#define INF_PLAYER_START_X 28
#define INF_PLAYER_START_Y 17

#define INF_NUM_PILLARS   3
#define INF_PILLAR_SIZE   3
#define INF_PILLAR_HP     24   /* each pillar can take 24 nibbler bites */

static const int INF_PILLAR_POS[INF_NUM_PILLARS][2] = {
    { 21, 37 },  /* south pillar */
    { 11, 23 },  /* west pillar */
    { 28, 21 },  /* north pillar */
};

#define INF_MAX_TICKS     6000  /* 60 minutes at 0.6s/tick */
#define INF_NUM_WAVES     69

/* ======================================================================== */
/* NPC types                                                                 */
/* ======================================================================== */

typedef enum {
    INF_NPC_NIBBLER = 0,  /* Jal-Nib: melee, eats pillars */
    INF_NPC_BAT,          /* Jal-MejRah: short-range ranged */
    INF_NPC_BLOB,         /* Jal-Ak: prayer reader, splits on death */
    INF_NPC_MELEER,       /* Jal-ImKot: melee, can burrow */
    INF_NPC_RANGER,       /* Jal-Xil: ranged, can melee if close */
    INF_NPC_MAGER,        /* Jal-Zek: magic, heals others, can melee if close */
    INF_NPC_JAD,          /* JalTok-Jad: alternating range/mage */
    INF_NPC_ZUK,          /* TzKal-Zuk: final boss */
    INF_NPC_HEALER_JAD,   /* Yt-HurKot: jad healer */
    INF_NPC_HEALER_ZUK,   /* Jal-MejJak: zuk healer */
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
} InfNPCStats;

/* stats from OSRS wiki (march 2026) */
static const InfNPCStats INF_NPC_STATS[INF_NUM_NPC_TYPES] = {
    /* NIBBLER:  10 HP, melee only, size 1, attacks pillars not player */
    [INF_NPC_NIBBLER] = { .hp = 10, .max_hit = 1, .attack_speed = 4,
        .attack_range = 1, .size = 1, .default_style = ATTACK_STYLE_MELEE,
        .can_melee = 0, .def_level = 1, .att_level = 1 },
    /* BAT: 25 HP, short-range ranged (4 tiles), size 1 */
    [INF_NPC_BAT] = { .hp = 25, .max_hit = 7, .attack_speed = 3,
        .attack_range = 4, .size = 1, .default_style = ATTACK_STYLE_RANGED,
        .can_melee = 0, .def_level = 50, .att_level = 85 },
    /* BLOB: 40 HP, prayer-reads, range 15, size 3, splits on death */
    [INF_NPC_BLOB] = { .hp = 40, .max_hit = 21, .attack_speed = 6,
        .attack_range = 15, .size = 3, .default_style = ATTACK_STYLE_RANGED,
        .can_melee = 1, .def_level = 65, .att_level = 165 },
    /* MELEER: 75 HP, melee, size 4, can burrow */
    [INF_NPC_MELEER] = { .hp = 75, .max_hit = 49, .attack_speed = 4,
        .attack_range = 1, .size = 4, .default_style = ATTACK_STYLE_MELEE,
        .can_melee = 0, .def_level = 160, .att_level = 240 },
    /* RANGER: 125 HP, ranged (98 range), size 3, can melee */
    [INF_NPC_RANGER] = { .hp = 125, .max_hit = 46, .attack_speed = 4,
        .attack_range = 98, .size = 3, .default_style = ATTACK_STYLE_RANGED,
        .can_melee = 1, .def_level = 60, .att_level = 250 },
    /* MAGER: 220 HP, magic (98 range), size 4, heals others, can melee */
    [INF_NPC_MAGER] = { .hp = 220, .max_hit = 70, .attack_speed = 4,
        .attack_range = 98, .size = 4, .default_style = ATTACK_STYLE_MAGIC,
        .can_melee = 1, .def_level = 260, .att_level = 350 },
    /* JAD: 350 HP, alternating range/mage, size 5 */
    [INF_NPC_JAD] = { .hp = 350, .max_hit = 97, .attack_speed = 8,
        .attack_range = 99, .size = 5, .default_style = ATTACK_STYLE_RANGED,
        .can_melee = 0, .def_level = 480, .att_level = 750 },
    /* ZUK: 1200 HP, special attacks, huge */
    [INF_NPC_ZUK] = { .hp = 1200, .max_hit = 251, .attack_speed = 10,
        .attack_range = 99, .size = 7, .default_style = ATTACK_STYLE_MAGIC,
        .can_melee = 0, .def_level = 260, .att_level = 600 },
    /* HEALER_JAD: 12 HP, melee, heals jad */
    [INF_NPC_HEALER_JAD] = { .hp = 12, .max_hit = 3, .attack_speed = 4,
        .attack_range = 1, .size = 1, .default_style = ATTACK_STYLE_MELEE,
        .can_melee = 0, .def_level = 1, .att_level = 1 },
    /* HEALER_ZUK: 35 HP, heals zuk */
    [INF_NPC_HEALER_ZUK] = { .hp = 35, .max_hit = 14, .attack_speed = 4,
        .attack_range = 98, .size = 1, .default_style = ATTACK_STYLE_MAGIC,
        .can_melee = 0, .def_level = 60, .att_level = 70 },
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
    /* abbreviations: N=nibbler B=bat BL=blob M=meleer R=ranger MA=mager J=jad Z=zuk */
    #define N INF_NPC_NIBBLER
    #define B INF_NPC_BAT
    #define BL INF_NPC_BLOB
    #define M INF_NPC_MELEER
    #define R INF_NPC_RANGER
    #define MA INF_NPC_MAGER
    #define J INF_NPC_JAD
    #define Z INF_NPC_ZUK
    #define W(...) { .types = { __VA_ARGS__ }, .count = sizeof((uint8_t[]){__VA_ARGS__}) }

    /* waves 1-8: bats + blobs introduction */
    [0]  = W(N,N,N, B),
    [1]  = W(N,N,N, B,B),
    [2]  = W(N,N,N, N,N,N),          /* cleanup */
    [3]  = W(N,N,N, BL),
    [4]  = W(N,N,N, B,BL),
    [5]  = W(N,N,N, B,B,BL),
    [6]  = W(N,N,N, BL,BL),
    [7]  = W(N,N,N, N,N,N),          /* cleanup */

    /* waves 9-17: meleer introduction */
    [8]  = W(N,N,N, M),
    [9]  = W(N,N,N, B,M),
    [10] = W(N,N,N, B,B,M),
    [11] = W(N,N,N, BL,M),
    [12] = W(N,N,N, B,BL,M),
    [13] = W(N,N,N, B,B,BL,M),
    [14] = W(N,N,N, BL,BL,M),
    [15] = W(N,N,N, M,M),
    [16] = W(N,N,N, N,N,N),          /* cleanup */

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
    [33] = W(N,N,N, N,N,N),          /* cleanup */

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

#define INF_MAX_NPCS      16   /* max active NPCs at once (wave 62 has 9 + blob splits) */

typedef struct {
    InfNPCType type;
    int x, y;
    int hp, max_hp;
    int size;
    int attack_timer;      /* ticks until next attack */
    int attack_style;      /* current attack style (may differ from default for blobs) */
    int active;
    int target_x, target_y; /* movement destination */

    /* type-specific state */
    int burrow_timer;      /* meleer: ticks remaining in burrow (0 = not burrowing) */
    int jad_is_mage_next;  /* jad: 1 if next attack is magic */
    int heal_target;       /* mager/healer: index of NPC to heal (-1 = none) */
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
    int shield_x;          /* shield NPC x position */
    int shield_dir;        /* +1 or -1 */
    int shield_active;
    int final_phase;       /* 1 when zuk HP < threshold */
    int final_phase_ticks;
    int healer_spawned;
    int jad_spawned;       /* jad spawn during zuk fight */
} InfZukState;

/* ======================================================================== */
/* encounter state                                                           */
/* ======================================================================== */

typedef struct {
    Player player;

    InfNPC npcs[INF_MAX_NPCS];
    InfPillar pillars[INF_NUM_PILLARS];
    InfZukState zuk;

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
/* forward declarations for functions defined later                          */
/* ======================================================================== */

static float inf_compute_reward(InfernoState* s);
static void inf_spawn_wave(InfernoState* s);
static void inf_tick_npcs(InfernoState* s);
static void inf_tick_player(InfernoState* s, const int* actions);

/* placeholder: more implementation follows in subsequent commits */

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
    s->player.x = INF_PLAYER_START_X;
    s->player.y = INF_PLAYER_START_Y;
    s->player_food_count = 8;    /* mantas or sharks */
    s->player_brew_doses = 12;   /* 3 brews */
    s->player_restore_doses = 16; /* 4 restores */
    s->player_special_energy = 100;
    s->active_prayer = PRAYER_NONE;
    s->player_attack_target = -1;

    /* pillars */
    for (int i = 0; i < INF_NUM_PILLARS; i++) {
        s->pillars[i].x = INF_PILLAR_POS[i][0];
        s->pillars[i].y = INF_PILLAR_POS[i][1];
        s->pillars[i].hp = INF_PILLAR_HP;
        s->pillars[i].active = 1;
    }
    inf_rebuild_los(s);

    /* start at configured wave (for curriculum) */
    s->wave = s->start_wave;
    inf_spawn_wave(s);
}

/* ======================================================================== */
/* spawn: place NPCs for current wave                                        */
/* ======================================================================== */

/* simple spawn: distribute NPCs around arena edges */
static void inf_spawn_wave(InfernoState* s) {
    if (s->wave >= INF_NUM_WAVES) return;

    const InfWaveDef* w = &INF_WAVES[s->wave];
    /* clear all NPCs */
    for (int i = 0; i < INF_MAX_NPCS; i++) s->npcs[i].active = 0;

    for (int i = 0; i < w->count && i < INF_MAX_NPCS; i++) {
        InfNPC* npc = &s->npcs[i];
        InfNPCType type = (InfNPCType)w->types[i];
        const InfNPCStats* stats = &INF_NPC_STATS[type];

        npc->type = type;
        npc->hp = stats->hp;
        npc->max_hp = stats->hp;
        npc->size = stats->size;
        npc->attack_timer = stats->attack_speed;
        npc->attack_style = stats->default_style;
        npc->active = 1;
        npc->burrow_timer = 0;
        npc->jad_is_mage_next = 0;
        npc->heal_target = -1;

        /* distribute spawn positions around the arena */
        int angle_idx = i * 8 / w->count;  /* spread evenly */
        switch (angle_idx % 4) {
            case 0: npc->x = 5 + inf_rand_int(s, 10);  npc->y = 5; break;   /* south */
            case 1: npc->x = 5;  npc->y = 15 + inf_rand_int(s, 10); break;  /* west */
            case 2: npc->x = 30 + inf_rand_int(s, 10); npc->y = 40; break;  /* north */
            case 3: npc->x = 35; npc->y = 15 + inf_rand_int(s, 10); break;  /* east */
        }
        npc->target_x = npc->x;
        npc->target_y = npc->y;
    }
}

/* ======================================================================== */
/* NPC AI: movement + attacks (stub — will be expanded)                      */
/* ======================================================================== */

static void inf_npc_move(InfernoState* s, int idx) {
    InfNPC* npc = &s->npcs[idx];
    if (!npc->active) return;
    if (npc->burrow_timer > 0) { npc->burrow_timer--; return; }

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
            /* no pillars left, target player */
            tx = s->player.x;
            ty = s->player.y;
        }
    } else {
        /* all other NPCs target the player */
        tx = s->player.x;
        ty = s->player.y;
    }

    /* move 1 tile toward target */
    int dx = 0, dy = 0;
    if (tx > npc->x) dx = 1;
    else if (tx < npc->x) dx = -1;
    if (ty > npc->y) dy = 1;
    else if (ty < npc->y) dy = -1;

    /* simple collision: don't walk into pillars */
    int nx = npc->x + dx;
    int ny = npc->y + dy;
    int blocked = 0;
    for (int p = 0; p < INF_NUM_PILLARS; p++) {
        if (!s->pillars[p].active) continue;
        if (los_aabb_overlap(nx, ny, npc->size,
                             s->pillars[p].x, s->pillars[p].y, INF_PILLAR_SIZE)) {
            blocked = 1;
            break;
        }
    }
    if (!blocked) {
        npc->x = nx;
        npc->y = ny;
    } else {
        /* try axis-only movement */
        nx = npc->x + dx;
        ny = npc->y;
        blocked = 0;
        for (int p = 0; p < INF_NUM_PILLARS; p++) {
            if (!s->pillars[p].active) continue;
            if (los_aabb_overlap(nx, ny, npc->size,
                                 s->pillars[p].x, s->pillars[p].y, INF_PILLAR_SIZE))
                blocked = 1;
        }
        if (!blocked) { npc->x = nx; }
        else {
            nx = npc->x;
            ny = npc->y + dy;
            blocked = 0;
            for (int p = 0; p < INF_NUM_PILLARS; p++) {
                if (!s->pillars[p].active) continue;
                if (los_aabb_overlap(nx, ny, npc->size,
                                     s->pillars[p].x, s->pillars[p].y, INF_PILLAR_SIZE))
                    blocked = 1;
            }
            if (!blocked) { npc->y = ny; }
        }
    }
}

static void inf_npc_attack(InfernoState* s, int idx) {
    InfNPC* npc = &s->npcs[idx];
    if (!npc->active) return;
    if (npc->attack_timer > 0) { npc->attack_timer--; return; }

    const InfNPCStats* stats = &INF_NPC_STATS[npc->type];

    /* nibbler attacks pillars, not player */
    if (npc->type == INF_NPC_NIBBLER) {
        for (int p = 0; p < INF_NUM_PILLARS; p++) {
            if (!s->pillars[p].active) continue;
            int dx = npc->x - s->pillars[p].x;
            int dy = npc->y - s->pillars[p].y;
            if (dx >= -1 && dx <= INF_PILLAR_SIZE && dy >= -1 && dy <= INF_PILLAR_SIZE) {
                s->pillars[p].hp--;
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

    /* check LOS for ranged/magic attackers */
    if (stats->attack_range > 1 && !inf_npc_has_los(s, idx)) return;

    /* check range */
    int dx = npc->x - s->player.x;
    int dy = npc->y - s->player.y;
    int dist = (dx < 0 ? -dx : dx) > (dy < 0 ? -dy : dy)
               ? (dx < 0 ? -dx : dx) : (dy < 0 ? -dy : dy);
    if (dist > stats->attack_range) return;

    /* blob prayer reading: switch style based on player's active prayer */
    if (npc->type == INF_NPC_BLOB) {
        if (s->active_prayer == PRAYER_PROTECT_RANGED)
            npc->attack_style = ATTACK_STYLE_MAGIC;
        else if (s->active_prayer == PRAYER_PROTECT_MAGIC)
            npc->attack_style = ATTACK_STYLE_RANGED;
        else
            npc->attack_style = stats->default_style;
    }

    /* melee switchover for ranger/mager: 50% chance when close */
    int actual_style = npc->attack_style;
    if (stats->can_melee && dist <= 1 && inf_rand_int(s, 2) == 0) {
        actual_style = ATTACK_STYLE_MELEE;
    }

    /* jad: alternate range/mage */
    if (npc->type == INF_NPC_JAD) {
        actual_style = npc->jad_is_mage_next ? ATTACK_STYLE_MAGIC : ATTACK_STYLE_RANGED;
        npc->jad_is_mage_next = !npc->jad_is_mage_next;
    }

    /* damage calculation */
    int dmg = inf_rand_int(s, stats->max_hit + 1);

    /* prayer reduction */
    int prayer_matches = (actual_style == ATTACK_STYLE_MELEE && s->active_prayer == PRAYER_PROTECT_MELEE) ||
                         (actual_style == ATTACK_STYLE_RANGED && s->active_prayer == PRAYER_PROTECT_RANGED) ||
                         (actual_style == ATTACK_STYLE_MAGIC && s->active_prayer == PRAYER_PROTECT_MAGIC);
    if (prayer_matches) {
        dmg = 0;  /* correct prayer blocks all damage in inferno */
        s->prayer_correct_this_tick = 1;
    }

    s->player.current_hitpoints -= dmg;
    if (s->player.current_hitpoints < 0) s->player.current_hitpoints = 0;
    s->damage_received_this_tick += dmg;

    npc->attack_timer = stats->attack_speed;
}

/* mager heal: find lowest HP NPC and heal it */
static void inf_mager_heal(InfernoState* s, int idx) {
    InfNPC* npc = &s->npcs[idx];
    if (npc->type != INF_NPC_MAGER || !npc->active) return;
    /* heal every ~5 ticks */
    if (s->tick % 5 != 0) return;

    int best = -1, lowest_pct = 100;
    for (int i = 0; i < INF_MAX_NPCS; i++) {
        if (!s->npcs[i].active || i == idx) continue;
        int pct = s->npcs[i].hp * 100 / s->npcs[i].max_hp;
        if (pct < lowest_pct && pct < 100) {
            lowest_pct = pct;
            best = i;
        }
    }
    if (best >= 0) {
        int heal = 25;
        s->npcs[best].hp += heal;
        if (s->npcs[best].hp > s->npcs[best].max_hp)
            s->npcs[best].hp = s->npcs[best].max_hp;
    }
}

static void inf_tick_npcs(InfernoState* s) {
    for (int i = 0; i < INF_MAX_NPCS; i++) {
        if (!s->npcs[i].active) continue;
        inf_npc_move(s, i);
        inf_npc_attack(s, i);
        if (s->npcs[i].type == INF_NPC_MAGER)
            inf_mager_heal(s, i);
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
        /* restore prayer */
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
        /* bounds check */
        if (nx >= 0 && nx < INF_ARENA_WIDTH && ny >= 0 && ny < INF_ARENA_HEIGHT) {
            /* pillar collision */
            int blocked = 0;
            for (int p = 0; p < INF_NUM_PILLARS; p++) {
                if (!s->pillars[p].active) continue;
                if (los_aabb_overlap(nx, ny, 1,
                                     s->pillars[p].x, s->pillars[p].y, INF_PILLAR_SIZE))
                    blocked = 1;
            }
            if (!blocked) {
                s->player.x = nx;
                s->player.y = ny;
            }
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
            /* simplified: fixed max hit based on gear, always hits for now */
            int max_hit = 45;  /* tbow against high-magic monsters */
            int dmg = inf_rand_int(s, max_hit + 1);
            target_npc->hp -= dmg;
            s->damage_dealt_this_tick += dmg;
            if (target_npc->hp <= 0) {
                target_npc->active = 0;
                /* blob splits into 2 smaller blobs */
                if (target_npc->type == INF_NPC_BLOB) {
                    for (int sp = 0; sp < 2; sp++) {
                        for (int j = 0; j < INF_MAX_NPCS; j++) {
                            if (!s->npcs[j].active) {
                                s->npcs[j] = *target_npc;
                                s->npcs[j].active = 1;
                                s->npcs[j].hp = 20;
                                s->npcs[j].max_hp = 20;
                                s->npcs[j].size = 1;
                                s->npcs[j].x += (sp == 0) ? -1 : 1;
                                s->npcs[j].attack_timer = 4;
                                break;
                            }
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
    if (s->episode_over)
        return (s->winner == 0) ? 1.0f : -1.0f;

    float r = 0.0f;
    /* damage dealt */
    if (s->damage_dealt_this_tick > 0)
        r += 0.01f * (s->damage_dealt_this_tick / 50.0f);
    /* damage taken penalty */
    if (s->damage_received_this_tick > 0)
        r -= 0.02f * (s->damage_received_this_tick / 50.0f);
    /* correct prayer bonus */
    if (s->prayer_correct_this_tick)
        r += 0.03f;
    /* wave completion bonus */
    if (s->wave_completed_this_tick)
        r += 0.05f;
    /* pillar lost penalty */
    if (s->pillar_lost_this_tick)
        r -= 0.1f;

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
            /* zuk dead = victory */
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

#define INF_NUM_OBS 200  /* approximate, will tune */

static void inf_write_obs(EncounterState* state, float* obs) {
    InfernoState* s = (InfernoState*)state;
    memset(obs, 0, INF_NUM_OBS * sizeof(float));
    int i = 0;

    /* player state */
    obs[i++] = (float)s->player.current_hitpoints / 99.0f;
    obs[i++] = (float)s->player.x / (float)INF_ARENA_WIDTH;
    obs[i++] = (float)s->player.y / (float)INF_ARENA_HEIGHT;
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

    /* NPCs (up to INF_MAX_NPCS) */
    for (int n = 0; n < INF_MAX_NPCS; n++) {
        InfNPC* npc = &s->npcs[n];
        if (npc->active) {
            obs[i++] = 1.0f;  /* active */
            obs[i++] = (float)npc->type / (float)INF_NUM_NPC_TYPES;
            obs[i++] = (float)npc->hp / (float)npc->max_hp;
            obs[i++] = (float)npc->x / (float)INF_ARENA_WIDTH;
            obs[i++] = (float)npc->y / (float)INF_ARENA_HEIGHT;
            obs[i++] = (float)npc->attack_timer / 10.0f;
            obs[i++] = (npc->attack_style == ATTACK_STYLE_MELEE) ? 1.0f : 0.0f;
            obs[i++] = (npc->attack_style == ATTACK_STYLE_RANGED) ? 1.0f : 0.0f;
            obs[i++] = (npc->attack_style == ATTACK_STYLE_MAGIC) ? 1.0f : 0.0f;
            obs[i++] = inf_npc_has_los(s, n) ? 1.0f : 0.0f;
        } else {
            for (int j = 0; j < 10; j++) obs[i++] = 0.0f;
        }
    }

    /* pad to INF_NUM_OBS */
    while (i < INF_NUM_OBS) obs[i++] = 0.0f;
}

static void inf_write_mask(EncounterState* state, float* mask) {
    (void)state;
    /* all actions always valid for now */
    for (int i = 0; i < INF_ACTION_MASK_SIZE; i++) mask[i] = 1.0f;
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
    int count = 1;  /* player */
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
            if (n == index) return &s->npcs[i];  /* TODO: needs Player* wrapper */
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

    .arena_base_x = 0,
    .arena_base_y = 0,
    .arena_width = INF_ARENA_WIDTH,
    .arena_height = INF_ARENA_HEIGHT,

    .render_post_tick = NULL,  /* TODO: visual overlay */
    .get_log = inf_get_log,
    .get_tick = inf_get_tick,
    .get_winner = inf_get_winner,
};

__attribute__((constructor))
static void inf_register(void) {
    encounter_register(&ENCOUNTER_INFERNO);
}

#endif /* ENCOUNTER_INFERNO_H */
