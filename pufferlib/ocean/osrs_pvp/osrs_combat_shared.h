/**
 * @fileoverview osrs_combat_shared.h — pure combat math shared by all encounters.
 *
 * stateless functions with no dependencies beyond <math.h>. use these instead
 * of reimplementing combat formulas per encounter.
 *
 * SHARED FUNCTIONS:
 *   osrs_hit_chance(att_roll, def_roll)    standard OSRS accuracy formula
 *   osrs_tbow_acc_mult(target_magic)       twisted bow accuracy multiplier
 *   osrs_tbow_dmg_mult(target_magic)       twisted bow damage multiplier
 *   osrs_barrage_resolve(targets, ...)     barrage 3x3 AoE with independent rolls
 *   encounter_magic_hit_delay(dist, is_p)  magic projectile flight delay (ticks)
 *   encounter_ranged_hit_delay(dist, is_p) ranged projectile flight delay (ticks)
 *   encounter_dist_to_npc(px,py,nx,ny,sz)  chebyshev dist to multi-tile NPC
 *
 * SEE ALSO:
 *   osrs_encounter.h    encounter-level abstractions (damage, movement, gear, etc.)
 *   osrs_pvp_combat.h   PvP-specific combat (prayer, veng, recoil, pending hits)
 */

#ifndef OSRS_COMBAT_SHARED_H
#define OSRS_COMBAT_SHARED_H

#include <math.h>

/* standard OSRS accuracy formula.
   att_roll and def_roll are pre-computed: eff_level * (bonus + 64).
   returns hit probability in [0, 1]. */
static inline float osrs_hit_chance(int att_roll, int def_roll) {
    if (att_roll > def_roll)
        return 1.0f - (float)(def_roll + 2) / (2.0f * (float)(att_roll + 1));
    else
        return (float)att_roll / (2.0f * (float)(def_roll + 1));
}

/* twisted bow accuracy multiplier.
   target_magic = min(max(npc_magic_level, npc_magic_attack_bonus), 250).
   formula from RuneLite TwistedBow._accuracyMultiplier. */
static inline float osrs_tbow_acc_mult(int target_magic) {
    int m = target_magic < 250 ? target_magic : 250;
    float t = (float)(3 * m) / 10.0f;
    float mult = (140.0f + (t - 10.0f) / 100.0f - (t - 100.0f) * (t - 100.0f) / 100.0f) / 100.0f;
    if (mult > 1.4f) mult = 1.4f;
    if (mult < 0.0f) mult = 0.0f;
    return mult;
}

/* twisted bow damage multiplier.
   same input as accuracy multiplier.
   formula from RuneLite TwistedBow._damageMultiplier. */
static inline float osrs_tbow_dmg_mult(int target_magic) {
    int m = target_magic < 250 ? target_magic : 250;
    float t = (float)(3 * m) / 10.0f;
    float mult = (250.0f + (t - 14.0f) / 100.0f - (t - 140.0f) * (t - 140.0f) / 100.0f) / 100.0f;
    if (mult > 2.5f) mult = 2.5f;
    if (mult < 0.0f) mult = 0.0f;
    return mult;
}

/* ======================================================================== */
/* barrage AoE (3x3)                                                         */
/* ======================================================================== */

#define BARRAGE_MAX_HITS 9
#define BARRAGE_FREEZE_TICKS 32

/* per-target info for barrage AoE. caller fills in the target array,
   osrs_barrage_resolve does accuracy/damage rolls and writes results back. */
typedef struct {
    int active;          /* in: 1 if this target slot is valid */
    int x, y;            /* in: NPC SW corner tile position */
    int def_level;       /* in: NPC defence level */
    int magic_def_bonus; /* in: NPC magic defence bonus */
    int npc_idx;         /* in: index into caller's NPC array (for callbacks) */
    int hit;             /* out: 1 = accuracy passed, 0 = splashed */
    int damage;          /* out: damage rolled (0 if splashed) */
} BarrageTarget;

/* result from a barrage cast */
typedef struct {
    int total_damage;    /* sum of all damage across AoE */
    int num_hits;        /* number of targets that were rolled against */
    int num_successful;  /* number that passed accuracy (hit=1) */
} BarrageResult;

/* resolve a barrage spell against a primary target + 3x3 AoE.
   - targets[0] is the primary target (always rolled first)
   - targets[1..max_targets-1] are potential AoE targets (only those within
     1 tile of primary are rolled against)
   - att_roll: pre-computed attacker magic roll (eff_level * (bonus + 64))
   - max_hit: barrage spell max hit
   - rng_state: pointer to RNG state for rolls
   - max_targets: size of targets array

   the function sets hit/damage on each target. caller is responsible for
   applying damage, freeze, death effects based on the results.

   returns aggregate result for reward/heal calculations. */
static inline BarrageResult osrs_barrage_resolve(
    BarrageTarget* targets, int max_targets,
    int att_roll, int max_hit, uint32_t* rng_state
) {
    BarrageResult result = { 0, 0, 0 };

    /* simple xorshift for rolls (same as inf_rand) */
    #define BARRAGE_RAND_INT(state, n) ({ \
        *(state) ^= *(state) << 13; \
        *(state) ^= *(state) >> 17; \
        *(state) ^= *(state) << 5; \
        (int)(*(state) % (unsigned)(n)); \
    })
    #define BARRAGE_RAND_FLOAT(state) ({ \
        *(state) ^= *(state) << 13; \
        *(state) ^= *(state) >> 17; \
        *(state) ^= *(state) << 5; \
        (float)(*(state) & 0xFFFF) / 65536.0f; \
    })

    if (max_targets < 1 || !targets[0].active) return result;

    /* primary target (index 0) always gets rolled */
    int px = targets[0].x, py = targets[0].y;
    {
        int def_roll = (targets[0].def_level + 8) * (targets[0].magic_def_bonus + 64);
        float chance = osrs_hit_chance(att_roll, def_roll);
        targets[0].hit = BARRAGE_RAND_FLOAT(rng_state) < chance;
        targets[0].damage = targets[0].hit ? BARRAGE_RAND_INT(rng_state, max_hit + 1) : 0;
        result.total_damage += targets[0].damage;
        result.num_hits++;
        if (targets[0].hit) result.num_successful++;
    }

    /* AoE: roll against all other active targets within 1 tile of primary */
    for (int i = 1; i < max_targets && result.num_hits < BARRAGE_MAX_HITS; i++) {
        if (!targets[i].active) continue;
        int dx = targets[i].x - px;
        int dy = targets[i].y - py;
        if (dx < -1 || dx > 1 || dy < -1 || dy > 1) continue;

        int def_roll = (targets[i].def_level + 8) * (targets[i].magic_def_bonus + 64);
        float chance = osrs_hit_chance(att_roll, def_roll);
        targets[i].hit = BARRAGE_RAND_FLOAT(rng_state) < chance;
        targets[i].damage = targets[i].hit ? BARRAGE_RAND_INT(rng_state, max_hit + 1) : 0;
        result.total_damage += targets[i].damage;
        result.num_hits++;
        if (targets[i].hit) result.num_successful++;
    }

    #undef BARRAGE_RAND_INT
    #undef BARRAGE_RAND_FLOAT

    return result;
}

/* ======================================================================== */
/* hit delay formulas (matching PvP + InfernoTrainer SDK)                    */
/* ======================================================================== */

/* magic hit delay: floor((1 + distance) / 3) + 1, +1 if attacker is player */
static inline int encounter_magic_hit_delay(int distance, int is_player) {
    return (1 + distance) / 3 + 1 + (is_player ? 1 : 0);
}

/* ranged hit delay: floor((3 + distance) / 6) + 1, +1 if attacker is player */
static inline int encounter_ranged_hit_delay(int distance, int is_player) {
    return (3 + distance) / 6 + 1 + (is_player ? 1 : 0);
}

/* chebyshev distance from point (px,py) to nearest tile of NPC footprint
   at (nx,ny) with given npc_size. accounts for multi-tile NPCs. */
static inline int encounter_dist_to_npc(int px, int py, int nx, int ny, int npc_size) {
    int cx = px < nx ? nx : (px > nx + npc_size - 1 ? nx + npc_size - 1 : px);
    int cy = py < ny ? ny : (py > ny + npc_size - 1 ? ny + npc_size - 1 : py);
    int dx = px - cx; if (dx < 0) dx = -dx;
    int dy = py - cy; if (dy < 0) dy = -dy;
    return dx > dy ? dx : dy;
}

#endif /* OSRS_COMBAT_SHARED_H */
