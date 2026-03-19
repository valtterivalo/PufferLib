/**
 * @fileoverview osrs_combat_shared.h — pure combat math shared by all encounters.
 *
 * stateless functions with no dependencies beyond <math.h>. use these instead
 * of reimplementing combat formulas per encounter.
 *
 * SHARED FUNCTIONS:
 *   osrs_hit_chance(att_roll, def_roll)       standard OSRS accuracy formula
 *   osrs_tbow_acc_mult(target_magic)          twisted bow accuracy multiplier
 *   osrs_tbow_dmg_mult(target_magic)          twisted bow damage multiplier
 *   osrs_barrage_resolve(targets, ...)        barrage 3x3 AoE with independent rolls
 *   osrs_npc_melee_max_hit(str, bonus)        NPC melee max hit from stats
 *   osrs_npc_ranged_max_hit(range, bonus)     NPC ranged max hit from stats
 *   osrs_npc_magic_max_hit(base, pct)         NPC magic max hit from stats
 *   osrs_npc_max_hit(style, ...)              dispatches to style-specific formula
 *   osrs_npc_attack_roll(att, bonus)          NPC attack roll
 *   osrs_player_def_roll_vs_npc(def,mag,b,s)  player defence roll vs NPC
 *   encounter_xorshift(state)                 xorshift32 RNG step
 *   encounter_rand_int(state, max)            random int in [0, max)
 *   encounter_rand_float(state)               random float in [0, 1)
 *   encounter_npc_roll_attack(att,def,mh,rng) NPC accuracy+damage in one call
 *   encounter_prayer_correct_for_style(p, s)  prayer blocks attack style check
 *   encounter_magic_hit_delay(dist, is_p)     magic projectile flight delay (ticks)
 *   encounter_ranged_hit_delay(dist, is_p)    ranged projectile flight delay (ticks)
 *   encounter_dist_to_npc(px,py,nx,ny,sz)     chebyshev dist to multi-tile NPC
 *
 * SEE ALSO:
 *   osrs_encounter.h    encounter-level abstractions (damage, movement, gear, etc.)
 *   osrs_pvp_combat.h   PvP-specific combat (prayer, veng, recoil, pending hits)
 */

#ifndef OSRS_COMBAT_SHARED_H
#define OSRS_COMBAT_SHARED_H

#include <math.h>
#include <stdint.h>

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
/* shared encounter RNG (xorshift32)                                         */
/* ======================================================================== */

/* all encounters should use these instead of reimplementing.
   state must be non-zero. */
static inline uint32_t encounter_xorshift(uint32_t* state) {
    *state ^= *state << 13;
    *state ^= *state >> 17;
    *state ^= *state << 5;
    return *state;
}

static inline int encounter_rand_int(uint32_t* rng_state, int max) {
    if (max <= 0) return 0;
    return (int)(encounter_xorshift(rng_state) % (unsigned)max);
}

static inline float encounter_rand_float(uint32_t* rng_state) {
    return (float)(encounter_xorshift(rng_state) & 0xFFFF) / 65536.0f;
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

    if (max_targets < 1 || !targets[0].active) return result;

    /* primary target (index 0) always gets rolled */
    int px = targets[0].x, py = targets[0].y;
    {
        int def_roll = (targets[0].def_level + 8) * (targets[0].magic_def_bonus + 64);
        float chance = osrs_hit_chance(att_roll, def_roll);
        targets[0].hit = encounter_rand_float(rng_state) < chance;
        targets[0].damage = targets[0].hit ? encounter_rand_int(rng_state, max_hit + 1) : 0;
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
        targets[i].hit = encounter_rand_float(rng_state) < chance;
        targets[i].damage = targets[i].hit ? encounter_rand_int(rng_state, max_hit + 1) : 0;
        result.total_damage += targets[i].damage;
        result.num_hits++;
        if (targets[i].hit) result.num_successful++;
    }

    return result;
}

/* ======================================================================== */
/* NPC combat formulas (from InfernoTrainer/osrs-sdk)                        */
/* ======================================================================== */

/* NPC melee max hit: floor((str + 8) * (melee_str_bonus + 64) + 320) / 640) */
static inline int osrs_npc_melee_max_hit(int str_level, int melee_str_bonus) {
    return ((str_level + 8) * (melee_str_bonus + 64) + 320) / 640;
}

/* NPC ranged max hit: floor(0.5 + (range + 8) * (ranged_str_bonus + 64) / 640) */
static inline int osrs_npc_ranged_max_hit(int range_level, int ranged_str_bonus) {
    return (int)(0.5 + (double)(range_level + 8) * (ranged_str_bonus + 64) / 640.0);
}

/* NPC magic max hit: floor(base_spell_dmg * magic_dmg_pct / 100).
   magic_dmg_pct=100 means 1.0x multiplier, 175 means 1.75x. */
static inline int osrs_npc_magic_max_hit(int base_spell_dmg, int magic_dmg_pct) {
    return base_spell_dmg * magic_dmg_pct / 100;
}

/* NPC attack roll: (att_level + 9) * (att_bonus + 64).
   NPCs don't have prayer or void bonuses — just level + invisible +9. */
static inline int osrs_npc_attack_roll(int att_level, int att_bonus) {
    return (att_level + 9) * (att_bonus + 64);
}

/* player defence roll against NPC attack.
   vs melee/ranged: (def_level + 9) * (def_bonus + 64).
   vs magic: (floor(magic_level * 0.7 + def_level * 0.3) + 9) * (def_bonus + 64).
   the magic formula uses 70% magic + 30% defence per OSRS standard. */
static inline int osrs_player_def_roll_vs_npc(
    int def_level, int magic_level, int def_bonus, int attack_style
) {
    int eff_def;
    if (attack_style == 3) {  /* ATTACK_STYLE_MAGIC = 3 */
        eff_def = (int)(magic_level * 0.7 + def_level * 0.3) + 9;
    } else {
        eff_def = def_level + 9;
    }
    return eff_def * (def_bonus + 64);
}

/* NPC max hit by style: dispatches to melee/ranged/magic formula.
   for magic, uses magic_base_dmg * magic_dmg_pct / 100. */
static inline int osrs_npc_max_hit(
    int attack_style,
    int str_level, int range_level,
    int melee_str_bonus, int ranged_str_bonus,
    int magic_base_dmg, int magic_dmg_pct
) {
    if (attack_style == 1) /* ATTACK_STYLE_MELEE = 1 */
        return osrs_npc_melee_max_hit(str_level, melee_str_bonus);
    if (attack_style == 2) /* ATTACK_STYLE_RANGED = 2 */
        return osrs_npc_ranged_max_hit(range_level, ranged_str_bonus);
    if (attack_style == 3) /* ATTACK_STYLE_MAGIC = 3 */
        return osrs_npc_magic_max_hit(magic_base_dmg, magic_dmg_pct);
    return 0;
}

/* NPC attack roll: accuracy check + damage roll in one call.
   returns damage (0 on miss). caller handles prayer separately. */
static inline int encounter_npc_roll_attack(
    int att_roll, int def_roll, int max_hit, uint32_t* rng_state
) {
    int dmg = encounter_rand_int(rng_state, max_hit + 1);
    if (encounter_rand_float(rng_state) >= osrs_hit_chance(att_roll, def_roll))
        dmg = 0;
    return dmg;
}

/* check if overhead prayer blocks the given attack style.
   uses int values directly: ATTACK_STYLE_MELEE(1) matches PRAYER_PROTECT_MELEE(3), etc.
   prayer enum: NONE=0, MAGIC=1, RANGED=2, MELEE=3.
   attack style enum: NONE=0, MELEE=1, RANGED=2, MAGIC=3.
   mapping: melee attack(1)->protect melee(3), ranged(2)->protect ranged(2), magic(3)->protect magic(1). */
static inline int encounter_prayer_correct_for_style(int prayer, int attack_style) {
    return (attack_style == 1 /* ATTACK_STYLE_MELEE */  && prayer == 3 /* PRAYER_PROTECT_MELEE */)  ||
           (attack_style == 2 /* ATTACK_STYLE_RANGED */ && prayer == 2 /* PRAYER_PROTECT_RANGED */) ||
           (attack_style == 3 /* ATTACK_STYLE_MAGIC */  && prayer == 1 /* PRAYER_PROTECT_MAGIC */);
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

/* fisher-yates shuffle for int arrays. used for spawn position randomization,
   snakeling placement, etc. encounters should use this instead of inlining. */
static inline void encounter_shuffle(int* arr, int n, uint32_t* rng) {
    for (int i = n - 1; i > 0; i--) {
        int j = encounter_rand_int(rng, i + 1);
        int tmp = arr[i]; arr[i] = arr[j]; arr[j] = tmp;
    }
}

#endif /* OSRS_COMBAT_SHARED_H */
