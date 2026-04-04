# Shared OSRS Systems Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Unify duplicated OSRS combat, consumable, and special attack mechanics into shared modules, validated by migrating encounter_zulrah.h to consume them.

**Architecture:** Four parallel workstreams (A: combat primitives, B: consumables + special attacks, C: monster manifest, D: zulrah migration). A/B/C have zero file overlap. D is blocked by A+B+C. All code is pure C in header files, tested with standalone test binaries.

**Tech Stack:** C11, static inline functions in .h files, xorshift32 RNG, wiki-sourced JSON codegen pipelines. Reference: `.refs/osrs-dps-calc/src/lib/` (TypeScript, authoritative for formulas).

**Spec:** `docs/superpowers/specs/2026-04-04-shared-osrs-systems-design.md`

---

## Workstream A: osrs_combat.h — player combat primitives

### Task A1: Rename osrs_combat_shared.h to osrs_combat.h and update all includes

This is a pure mechanical rename — no logic changes. Every file that includes the old name must be updated.

**Files:**
- Rename: `pufferlib/ocean/osrs/osrs_combat_shared.h` → `pufferlib/ocean/osrs/osrs_combat.h`
- Modify: `pufferlib/ocean/osrs/osrs_encounter.h` (line 62: `#include "osrs_combat_shared.h"` → `#include "osrs_combat.h"`)
- Modify: `pufferlib/ocean/osrs/encounters/encounter_inferno.h` (line 23: `#include "../osrs_combat_shared.h"` → `#include "../osrs_combat.h"`)
- Modify: `pufferlib/ocean/osrs/encounters/encounter_zulrah.h` (line 38: `#include "../osrs_combat_shared.h"` → `#include "../osrs_combat.h"`)
- Modify: `pufferlib/ocean/osrs/tests/test_special_attacks.c` (line 29: `#include "pufferlib/ocean/osrs/osrs_combat_shared.h"` → `#include "pufferlib/ocean/osrs/osrs_combat.h"`)
- Modify: all comments referencing the old filename in the above files

- [ ] **Step 1: Rename the file**

```bash
git mv pufferlib/ocean/osrs/osrs_combat_shared.h pufferlib/ocean/osrs/osrs_combat.h
```

- [ ] **Step 2: Update include in osrs_encounter.h**

In `pufferlib/ocean/osrs/osrs_encounter.h`, change line 62:
```c
// old:
#include "osrs_combat_shared.h"
// new:
#include "osrs_combat.h"
```

Also update the comment at line 50:
```c
// old:
 *   osrs_combat_shared.h              hit chance, tbow formula, barrage AoE, delay formulas
// new:
 *   osrs_combat.h                     hit chance, tbow formula, barrage AoE, delay formulas
```

- [ ] **Step 3: Update include in encounter_inferno.h**

In `pufferlib/ocean/osrs/encounters/encounter_inferno.h`, change line 23:
```c
// old:
#include "../osrs_combat_shared.h"
// new:
#include "../osrs_combat.h"
```

Also update any comments referencing the old filename (line 640).

- [ ] **Step 4: Update include in encounter_zulrah.h**

In `pufferlib/ocean/osrs/encounters/encounter_zulrah.h`, change line 38:
```c
// old:
#include "../osrs_combat_shared.h"
// new:
#include "../osrs_combat.h"
```

Also update comments at lines 667 and 778.

- [ ] **Step 5: Update include in test_special_attacks.c**

In `pufferlib/ocean/osrs/tests/test_special_attacks.c`, change line 29:
```c
// old:
#include "pufferlib/ocean/osrs/osrs_combat_shared.h"
// new:
#include "pufferlib/ocean/osrs/osrs_combat.h"
```

Also update the comment at line 20.

- [ ] **Step 6: Update the file's own header**

In `pufferlib/ocean/osrs/osrs_combat.h`, update:
```c
// old:
 * @fileoverview osrs_combat_shared.h — pure combat math shared by all encounters.
// ...
#ifndef OSRS_COMBAT_SHARED_H
#define OSRS_COMBAT_SHARED_H
// ...
#endif /* OSRS_COMBAT_SHARED_H */

// new:
 * @fileoverview osrs_combat.h — pure combat math shared by all encounters.
// ...
#ifndef OSRS_COMBAT_H
#define OSRS_COMBAT_H
// ...
#endif /* OSRS_COMBAT_H */
```

- [ ] **Step 7: Verify all 3 env builds pass**

```bash
python setup.py build_osrs_inferno --force 2>&1 | tail -1
python setup.py build_osrs_zulrah --force 2>&1 | tail -1
python setup.py build_osrs_pvp --force 2>&1 | tail -1
```

Expected: all print `Built: pufferlib/_C.cpython-312-darwin.so`

- [ ] **Step 8: Verify all existing tests pass**

```bash
cc -std=c11 -O0 -g -I. -o test_combat_math pufferlib/ocean/osrs/tests/test_combat_math.c -lm && ./test_combat_math 2>&1 | tail -1
cc -std=c11 -O0 -g -I. -o test_item_effects pufferlib/ocean/osrs/tests/test_item_effects.c -lm && ./test_item_effects 2>&1 | tail -1
cc -std=c11 -O0 -g -I. -o test_special_attacks pufferlib/ocean/osrs/tests/test_special_attacks.c -lm && ./test_special_attacks 2>&1 | tail -1
```

Expected: `155/155 passed`, `164/164 passed`, `222/222 passed`

- [ ] **Step 9: Commit**

```bash
git add -A && git commit -m "rename osrs_combat_shared.h to osrs_combat.h, update all includes"
```

### Task A2: Add player-side combat primitives to osrs_combat.h

Add the core player combat formulas. These are pure math functions with zero state.
Reference: `.refs/osrs-dps-calc/src/lib/BaseCalc.ts:105-110` for max hit formula,
`.refs/osrs-dps-calc/src/lib/PlayerVsNPCCalc.ts:191-212` for effective level + attack roll.

**Files:**
- Modify: `pufferlib/ocean/osrs/osrs_combat.h` (append before `#endif`)
- Create: `pufferlib/ocean/osrs/tests/test_player_combat.c`

- [ ] **Step 1: Write test file with effective level tests**

Create `pufferlib/ocean/osrs/tests/test_player_combat.c`:

```c
/**
 * @file test_player_combat.c
 * @brief Tests for player-side combat primitives in osrs_combat.h.
 *
 * Verifies effective level, attack roll, max hit, prayer reduction,
 * double accuracy, and equipment bonus summation against osrs-dps-calc
 * reference values.
 *
 * Build: cc -std=c11 -O0 -g -I. -o test_player_combat pufferlib/ocean/osrs/tests/test_player_combat.c -lm
 */

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include "pufferlib/ocean/osrs/osrs_combat.h"
#include "pufferlib/ocean/osrs/osrs_items.h"

static int total_tests = 0;
static int passed_tests = 0;

#define ASSERT_EQ(a, b, msg) do { \
    total_tests++; \
    if ((a) != (b)) { \
        printf("FAIL: %s: got %d, expected %d\n", msg, (int)(a), (int)(b)); \
    } else { passed_tests++; } \
} while(0)

#define ASSERT_FLOAT_NEAR(a, b, tol, msg) do { \
    total_tests++; \
    if (fabsf((float)(a) - (float)(b)) > (float)(tol)) { \
        printf("FAIL: %s: got %.6f, expected %.6f\n", msg, (float)(a), (float)(b)); \
    } else { passed_tests++; } \
} while(0)

/* --- osrs_player_eff_level --- */
static void test_eff_level(void) {
    printf("--- osrs_player_eff_level ---\n");

    /* no prayer, no style bonus: floor(99 * 1.0) + 0 + 8 = 107 */
    ASSERT_EQ(osrs_player_eff_level(99, 1.0f, 0), 107, "base 99, no prayer, no style");

    /* rigour: floor(99 * 1.20) + 0 + 8 = floor(118.8) + 8 = 118 + 8 = 126 */
    ASSERT_EQ(osrs_player_eff_level(99, 1.20f, 0), 126, "base 99, rigour att mult");

    /* augury: floor(99 * 1.25) + 0 + 8 = floor(123.75) + 8 = 123 + 8 = 131 */
    ASSERT_EQ(osrs_player_eff_level(99, 1.25f, 0), 131, "base 99, augury");

    /* piety: floor(99 * 1.20) + 0 + 8 = 126 */
    ASSERT_EQ(osrs_player_eff_level(99, 1.20f, 0), 126, "base 99, piety att");

    /* piety str: floor(99 * 1.23) + 0 + 8 = floor(121.77) + 8 = 121 + 8 = 129 */
    ASSERT_EQ(osrs_player_eff_level(99, 1.23f, 0), 129, "base 99, piety str");

    /* accurate style (+3): floor(99 * 1.0) + 3 + 8 = 110 */
    ASSERT_EQ(osrs_player_eff_level(99, 1.0f, 3), 110, "base 99, accurate +3");

    /* rigour + rapid (+0): floor(99 * 1.20) + 0 + 8 = 126 */
    ASSERT_EQ(osrs_player_eff_level(99, 1.20f, 0), 126, "rigour, rapid");

    /* level 1, no prayer: floor(1 * 1.0) + 0 + 8 = 9 */
    ASSERT_EQ(osrs_player_eff_level(1, 1.0f, 0), 9, "level 1");

    /* boosted level (99+13 = 112 from imbued heart): floor(112*1.25)+8 = 148 */
    ASSERT_EQ(osrs_player_eff_level(112, 1.25f, 0), 148, "boosted 112, augury");
}

/* --- osrs_player_att_roll --- */
static void test_att_roll(void) {
    printf("--- osrs_player_att_roll ---\n");

    /* eff_level * (bonus + 64) */
    ASSERT_EQ(osrs_player_att_roll(107, 0), 107 * 64, "eff 107, bonus 0");
    ASSERT_EQ(osrs_player_att_roll(126, 100), 126 * 164, "eff 126, bonus 100");
    ASSERT_EQ(osrs_player_att_roll(148, 182), 148 * 246, "eff 148, bonus 182 (BIS mage)");
    ASSERT_EQ(osrs_player_att_roll(9, 0), 9 * 64, "eff 9, bonus 0 (level 1)");
}

/* --- osrs_player_melee_max_hit --- */
static void test_melee_max_hit(void) {
    printf("--- osrs_player_melee_max_hit ---\n");

    /* formula: floor((eff * (str + 64) + 320) / 640)
       ref: BaseCalc.ts:107 — floor((effectiveLevel * gearBonus + 320) / 640)
       where gearBonus = str_bonus + 64 */

    /* whip (str 82) + piety str eff 129: floor((129 * (82+64) + 320) / 640) = floor(18854/640+0.5) */
    /* = floor((129 * 146 + 320) / 640) = floor(18834/640 + 320/640) = floor(29.74+0.5) = 29 */
    int eff = 129, str = 82;
    int expected = (eff * (str + 64) + 320) / 640;
    ASSERT_EQ(osrs_player_melee_max_hit(129, 82), expected, "whip, piety");

    /* level 1, str 0: floor((9*(0+64)+320)/640) = floor(896/640) = 1 */
    ASSERT_EQ(osrs_player_melee_max_hit(9, 0), (9 * 64 + 320) / 640, "level 1, no gear");
}

/* --- osrs_player_ranged_max_hit --- */
static void test_ranged_max_hit(void) {
    printf("--- osrs_player_ranged_max_hit ---\n");

    /* same formula as melee. tbow (ranged_str 20) + dragon arrows (60) + other gear
       for simplicity test with known values. rigour str eff = floor(99*1.23)+8 = 129
       ranged str 98 (BIS from spec): floor((129*(98+64)+320)/640) = floor((129*162+320)/640) */
    int eff = 129, str = 98;
    int expected = (eff * (str + 64) + 320) / 640;
    ASSERT_EQ(osrs_player_ranged_max_hit(129, 98), expected, "rigour, BIS ranged str");
}

/* --- osrs_player_magic_max_hit --- */
static void test_magic_max_hit(void) {
    printf("--- osrs_player_magic_max_hit ---\n");

    /* ice barrage base 30, magic dmg bonus 30%: floor(30 * (100 + 30) / 100) = floor(39) = 39 */
    ASSERT_EQ(osrs_player_magic_max_hit(30, 30), 30 * 130 / 100, "barrage, 30% dmg");

    /* trident base: floor(magic_level / 3) - 6 for seas = floor(99/3)-6 = 27
       with 0% bonus: 27 * 100 / 100 = 27 */
    ASSERT_EQ(osrs_player_magic_max_hit(27, 0), 27, "trident, no bonus");

    /* sang staff base: floor(magic_level / 3) - 1 = 32
       with 15% bonus (occult + tormented): floor(32 * 115 / 100) = 36 */
    ASSERT_EQ(osrs_player_magic_max_hit(32, 15), 32 * 115 / 100, "sang, 15% bonus");
}

/* --- osrs_prayer_reduce_damage --- */
static void test_prayer_reduce(void) {
    printf("--- osrs_prayer_reduce_damage ---\n");

    /* PvE: correct prayer blocks 100% */
    ASSERT_EQ(osrs_prayer_reduce_damage(50, PRAYER_PROTECT_MAGIC, ATTACK_STYLE_MAGIC, 0), 0,
              "PvE magic pray vs magic");
    ASSERT_EQ(osrs_prayer_reduce_damage(50, PRAYER_PROTECT_RANGED, ATTACK_STYLE_RANGED, 0), 0,
              "PvE range pray vs range");
    ASSERT_EQ(osrs_prayer_reduce_damage(50, PRAYER_PROTECT_MELEE, ATTACK_STYLE_MELEE, 0), 0,
              "PvE melee pray vs melee");

    /* PvE: wrong prayer passes through */
    ASSERT_EQ(osrs_prayer_reduce_damage(50, PRAYER_PROTECT_MAGIC, ATTACK_STYLE_MELEE, 0), 50,
              "PvE magic pray vs melee");
    ASSERT_EQ(osrs_prayer_reduce_damage(50, PRAYER_NONE, ATTACK_STYLE_MAGIC, 0), 50,
              "PvE no pray vs magic");

    /* PvP: correct prayer reduces by 40% (player takes 60%) */
    ASSERT_EQ(osrs_prayer_reduce_damage(50, PRAYER_PROTECT_MAGIC, ATTACK_STYLE_MAGIC, 1), 30,
              "PvP magic pray vs magic: 50*0.6=30");
    ASSERT_EQ(osrs_prayer_reduce_damage(41, PRAYER_PROTECT_RANGED, ATTACK_STYLE_RANGED, 1), (int)(41 * 0.6f),
              "PvP range pray vs range: 41*0.6");

    /* PvP: wrong prayer passes through */
    ASSERT_EQ(osrs_prayer_reduce_damage(50, PRAYER_PROTECT_MAGIC, ATTACK_STYLE_MELEE, 1), 50,
              "PvP magic pray vs melee");

    /* zero damage stays zero */
    ASSERT_EQ(osrs_prayer_reduce_damage(0, PRAYER_PROTECT_MAGIC, ATTACK_STYLE_MAGIC, 0), 0,
              "zero damage PvE");
    ASSERT_EQ(osrs_prayer_reduce_damage(0, PRAYER_PROTECT_MAGIC, ATTACK_STYLE_MAGIC, 1), 0,
              "zero damage PvP");
}

/* --- osrs_hit_chance_double --- */
static void test_hit_chance_double(void) {
    printf("--- osrs_hit_chance_double ---\n");

    /* osmumten's fang / confliction gauntlets formula.
       when att >= def: 1 - (def+2)(2*def+3) / (6*(att+1)^2)
       when att < def:  att*(4*att+5) / (6*(att+1)*(def+1))
       ref: zul_hit_chance_double in encounter_zulrah.h:782-789 */

    /* large att vs small def: should be near 1.0 */
    float c1 = osrs_hit_chance_double(50000, 1000);
    ASSERT_FLOAT_NEAR(c1, 1.0f, 0.01f, "large att vs small def");

    /* equal rolls: should be higher than single roll at equal */
    float single = osrs_hit_chance(10000, 10000);
    float double_r = osrs_hit_chance_double(10000, 10000);
    total_tests++;
    if (double_r > single) { passed_tests++; }
    else { printf("FAIL: double roll should exceed single at equal rolls\n"); }

    /* zero att: should be 0 */
    ASSERT_FLOAT_NEAR(osrs_hit_chance_double(0, 10000), 0.0f, 0.001f, "zero att");
}

/* --- osrs_sum_equipment_bonuses --- */
static void test_equipment_bonuses(void) {
    printf("--- osrs_sum_equipment_bonuses ---\n");

    /* empty loadout: all zeros */
    uint8_t empty[NUM_GEAR_SLOTS];
    for (int i = 0; i < NUM_GEAR_SLOTS; i++) empty[i] = ITEM_NONE;
    EquipmentBonuses eb;
    osrs_sum_equipment_bonuses(empty, &eb);
    ASSERT_EQ(eb.attack_stab, 0, "empty: stab 0");
    ASSERT_EQ(eb.attack_magic, 0, "empty: magic 0");
    ASSERT_EQ(eb.melee_strength, 0, "empty: melee str 0");
    ASSERT_EQ(eb.attack_speed, 0, "empty: speed 0");

    /* single item: whip in weapon slot */
    uint8_t whip_only[NUM_GEAR_SLOTS];
    for (int i = 0; i < NUM_GEAR_SLOTS; i++) whip_only[i] = ITEM_NONE;
    whip_only[GEAR_SLOT_WEAPON] = ITEM_WHIP;
    osrs_sum_equipment_bonuses(whip_only, &eb);
    const Item* whip = &ITEM_DATABASE[ITEM_WHIP];
    ASSERT_EQ(eb.attack_slash, whip->attack_slash, "whip slash matches DB");
    ASSERT_EQ(eb.melee_strength, whip->melee_strength, "whip str matches DB");
    ASSERT_EQ(eb.attack_speed, whip->attack_speed, "whip speed matches DB");
    ASSERT_EQ(eb.attack_range, whip->attack_range, "whip range matches DB");

    /* full loadout: ancestral hat + occult + kodai wand — verify they sum */
    uint8_t mage_partial[NUM_GEAR_SLOTS];
    for (int i = 0; i < NUM_GEAR_SLOTS; i++) mage_partial[i] = ITEM_NONE;
    mage_partial[GEAR_SLOT_HEAD] = ITEM_ANCESTRAL_HAT;
    mage_partial[GEAR_SLOT_NECK] = ITEM_OCCULT_NECKLACE;
    mage_partial[GEAR_SLOT_WEAPON] = ITEM_KODAI_WAND;
    osrs_sum_equipment_bonuses(mage_partial, &eb);
    int expected_magic = ITEM_DATABASE[ITEM_ANCESTRAL_HAT].attack_magic
                       + ITEM_DATABASE[ITEM_OCCULT_NECKLACE].attack_magic
                       + ITEM_DATABASE[ITEM_KODAI_WAND].attack_magic;
    ASSERT_EQ(eb.attack_magic, expected_magic, "mage partial: magic att sum");
}

int main(void) {
    test_eff_level();
    test_att_roll();
    test_melee_max_hit();
    test_ranged_max_hit();
    test_magic_max_hit();
    test_prayer_reduce();
    test_hit_chance_double();
    test_equipment_bonuses();

    printf("\n=== results: %d/%d passed ===\n", passed_tests, total_tests);
    return (passed_tests == total_tests) ? 0 : 1;
}
```

- [ ] **Step 2: Run tests to verify they fail (functions don't exist yet)**

```bash
cc -std=c11 -O0 -g -I. -o test_player_combat pufferlib/ocean/osrs/tests/test_player_combat.c -lm 2>&1 | head -5
```

Expected: compile errors about undefined functions.

- [ ] **Step 3: Implement player combat primitives in osrs_combat.h**

Append before the final `#endif` in `pufferlib/ocean/osrs/osrs_combat.h`:

```c
/* ======================================================================== */
/* player-side combat primitives                                             */
/*                                                                           */
/* pure math for player effective levels, attack rolls, and max hits.        */
/* ref: .refs/osrs-dps-calc/src/lib/PlayerVsNPCCalc.ts                      */
/*      .refs/osrs-dps-calc/src/lib/BaseCalc.ts:105-110                     */
/* ======================================================================== */

/* player effective level: floor(base * prayer_mult) + style_bonus + 8.
   prayer_mult: 1.0 (none), 1.20 (piety/rigour att), 1.23 (piety/rigour str),
   1.25 (augury). style_bonus: 0 (rapid/autocast), +3 (accurate), +1 (controlled).
   ref: PlayerVsNPCCalc.ts lines 191-208 */
static inline int osrs_player_eff_level(int base_level, float prayer_mult, int style_bonus) {
    return (int)(base_level * prayer_mult) + style_bonus + 8;
}

/* player attack roll: eff_level * (equipment_bonus + 64).
   ref: PlayerVsNPCCalc.ts line 212 */
static inline int osrs_player_att_roll(int eff_level, int equipment_bonus) {
    return eff_level * (equipment_bonus + 64);
}

/* player melee max hit: floor((eff_str * (str_bonus + 64) + 320) / 640).
   ref: BaseCalc.ts:107 trackMaxHitFromEffective */
static inline int osrs_player_melee_max_hit(int eff_str_level, int str_bonus) {
    return (eff_str_level * (str_bonus + 64) + 320) / 640;
}

/* player ranged max hit: same formula as melee, different input stats.
   ref: BaseCalc.ts:107 (same formula, ranged strength bonus instead of melee) */
static inline int osrs_player_ranged_max_hit(int eff_range_level, int ranged_str_bonus) {
    return (eff_range_level * (ranged_str_bonus + 64) + 320) / 640;
}

/* player magic max hit: floor(spell_base_dmg * (100 + magic_dmg_pct) / 100).
   magic_dmg_pct is the total % bonus from gear (e.g. 30 = +30%).
   spell_base_dmg: 30 for ice/blood barrage, floor(magic/3)-6 for trident, etc.
   ref: PlayerVsNPCCalc.ts lines 622-667 */
static inline int osrs_player_magic_max_hit(int spell_base_dmg, int magic_dmg_pct) {
    return spell_base_dmg * (100 + magic_dmg_pct) / 100;
}

/* prayer damage reduction.
   PvE (is_pvp=0): correct overhead prayer blocks 100% of damage → returns 0.
   PvP (is_pvp=1): correct overhead prayer reduces by 40% → returns floor(dmg * 0.6).
   wrong prayer or no prayer: returns damage unchanged.
   ref: osrs wiki "protection prayers", osrs-dps-calc */
static inline int osrs_prayer_reduce_damage(int damage, int prayer, int attack_style, int is_pvp) {
    if (damage <= 0) return 0;
    if (!encounter_prayer_correct_for_style(prayer, attack_style)) return damage;
    if (is_pvp) return (int)(damage * 0.6f);
    return 0;  /* PvE: full block */
}

/* double accuracy roll (osmumten's fang, confliction gauntlets).
   rolls accuracy twice — hit if EITHER roll succeeds.
   effective chance: 1 - (1-p)^2 where p = single roll hit chance.
   closed-form from wiki:
     if att >= def: 1 - (def+2)(2*def+3) / (6*(att+1)^2)
     if att < def:  att*(4*att+5) / (6*(att+1)*(def+1))
   ref: osrs wiki "osmumten's fang", encounter_zulrah.h:782-789 */
static inline float osrs_hit_chance_double(int att_roll, int def_roll) {
    float fa = (float)att_roll, fd = (float)def_roll;
    if (att_roll >= def_roll) {
        float num = (fd + 2.0f) * (2.0f * fd + 3.0f);
        float den = 6.0f * (fa + 1.0f) * (fa + 1.0f);
        return 1.0f - num / den;
    }
    return fa * (4.0f * fa + 5.0f) / (6.0f * (fa + 1.0f) * (fd + 1.0f));
}

/* sum equipment bonuses from a gear loadout using ITEM_DATABASE.
   iterates all slots, sums all offensive + defensive bonuses.
   attack_speed and attack_range come from the weapon slot only.
   ITEM_NONE (255) slots are skipped. */
typedef struct {
    int attack_stab, attack_slash, attack_crush, attack_magic, attack_ranged;
    int defence_stab, defence_slash, defence_crush, defence_magic, defence_ranged;
    int melee_strength, ranged_strength, magic_damage, prayer;
    int attack_speed, attack_range;
} EquipmentBonuses;

static inline void osrs_sum_equipment_bonuses(const uint8_t loadout[NUM_GEAR_SLOTS],
                                               EquipmentBonuses* out) {
    memset(out, 0, sizeof(*out));
    for (int slot = 0; slot < NUM_GEAR_SLOTS; slot++) {
        uint8_t idx = loadout[slot];
        if (idx == 255) continue;  /* ITEM_NONE */
        const Item* item = &ITEM_DATABASE[idx];
        out->attack_stab += item->attack_stab;
        out->attack_slash += item->attack_slash;
        out->attack_crush += item->attack_crush;
        out->attack_magic += item->attack_magic;
        out->attack_ranged += item->attack_ranged;
        out->defence_stab += item->defence_stab;
        out->defence_slash += item->defence_slash;
        out->defence_crush += item->defence_crush;
        out->defence_magic += item->defence_magic;
        out->defence_ranged += item->defence_ranged;
        out->melee_strength += item->melee_strength;
        out->ranged_strength += item->ranged_strength;
        out->magic_damage += item->magic_damage;
        out->prayer += item->prayer;
    }
    /* weapon slot determines speed + range */
    uint8_t weapon = loadout[GEAR_SLOT_WEAPON];
    if (weapon != 255) {
        out->attack_speed = ITEM_DATABASE[weapon].attack_speed;
        out->attack_range = ITEM_DATABASE[weapon].attack_range;
    }
}
```

Also add `#include <string.h>` near the top of osrs_combat.h if not already present (needed for `memset`). And add `#include "osrs_items.h"` since `osrs_sum_equipment_bonuses` references `ITEM_DATABASE`. Note: osrs_combat.h currently has zero includes besides `<math.h>` and `<stdint.h>`. The items include is needed ONLY for `osrs_sum_equipment_bonuses`. If this coupling is a concern, this function could live in osrs_encounter.h instead — but since the spec places it here, add the include.

Update the `@fileoverview` to list the new functions.

- [ ] **Step 4: Run tests to verify they pass**

```bash
cc -std=c11 -O0 -g -I. -o test_player_combat pufferlib/ocean/osrs/tests/test_player_combat.c -lm && ./test_player_combat
```

Expected: all tests pass.

- [ ] **Step 5: Run existing test suites to verify no regressions**

```bash
cc -std=c11 -O0 -g -I. -o test_combat_math pufferlib/ocean/osrs/tests/test_combat_math.c -lm && ./test_combat_math 2>&1 | tail -1
cc -std=c11 -O0 -g -I. -o test_item_effects pufferlib/ocean/osrs/tests/test_item_effects.c -lm && ./test_item_effects 2>&1 | tail -1
cc -std=c11 -O0 -g -I. -o test_special_attacks pufferlib/ocean/osrs/tests/test_special_attacks.c -lm && ./test_special_attacks 2>&1 | tail -1
```

Expected: `155/155`, `164/164`, `222/222`

- [ ] **Step 6: Verify all 3 env builds still pass**

```bash
python setup.py build_osrs_inferno --force 2>&1 | tail -1
python setup.py build_osrs_zulrah --force 2>&1 | tail -1
python setup.py build_osrs_pvp --force 2>&1 | tail -1
```

- [ ] **Step 7: Commit**

```bash
git add pufferlib/ocean/osrs/osrs_combat.h pufferlib/ocean/osrs/tests/test_player_combat.c
git commit -m "add player-side combat primitives to osrs_combat.h with tests"
```

---

## Workstream B: osrs_consumables.h + osrs_special_attacks.h

### Task B1: Create osrs_consumables.h with food and potion functions

Pure functions for OSRS food eating and potion drinking. No encounter state — takes
parameters, returns result struct. Encounters apply the result to their state.

Reference: OSRS wiki food/potion articles, `.refs/osrs-dps-calc/` for brew formulas.

**Files:**
- Create: `pufferlib/ocean/osrs/osrs_consumables.h`
- Create: `pufferlib/ocean/osrs/tests/test_consumables.c`

- [ ] **Step 1: Write test file**

Create `pufferlib/ocean/osrs/tests/test_consumables.c`:

```c
/**
 * @file test_consumables.c
 * @brief Tests for shared food/potion/brew functions in osrs_consumables.h.
 *
 * Build: cc -std=c11 -O0 -g -I. -o test_consumables pufferlib/ocean/osrs/tests/test_consumables.c -lm
 */

#include <stdio.h>
#include <stdlib.h>
#include "pufferlib/ocean/osrs/osrs_consumables.h"

static int total_tests = 0;
static int passed_tests = 0;

#define ASSERT_EQ(a, b, msg) do { \
    total_tests++; \
    if ((a) != (b)) { \
        printf("FAIL: %s: got %d, expected %d\n", msg, (int)(a), (int)(b)); \
    } else { passed_tests++; } \
} while(0)

/* --- food healing amounts --- */
static void test_food_heal_amount(void) {
    printf("--- food heal amounts ---\n");
    ASSERT_EQ(osrs_food_heal_amount(FOOD_SHARK), 20, "shark heals 20");
    ASSERT_EQ(osrs_food_heal_amount(FOOD_KARAMBWAN), 18, "karambwan heals 18");
    ASSERT_EQ(osrs_food_heal_amount(FOOD_MANTA_RAY), 22, "manta ray heals 22");
    ASSERT_EQ(osrs_food_heal_amount(FOOD_ANGLERFISH), 22, "anglerfish heals 22");
}

/* --- eating food --- */
static void test_eat_food(void) {
    printf("--- osrs_eat_food ---\n");

    /* shark at 50/99 HP: heals 20 → 70 */
    EatResult r = osrs_eat_food(FOOD_SHARK, 50, 99, 0);
    ASSERT_EQ(r.consumed, 1, "shark consumed");
    ASSERT_EQ(r.hp_healed, 20, "shark heals 20");

    /* shark at 90/99: heals 9 (clamped to max) */
    r = osrs_eat_food(FOOD_SHARK, 90, 99, 0);
    ASSERT_EQ(r.consumed, 1, "shark at 90 consumed");
    ASSERT_EQ(r.hp_healed, 9, "shark at 90 heals 9 (clamped)");

    /* shark at 99/99: can't eat (full HP) */
    r = osrs_eat_food(FOOD_SHARK, 99, 99, 0);
    ASSERT_EQ(r.consumed, 0, "shark at full HP not consumed");

    /* shark with food_timer active: can't eat */
    r = osrs_eat_food(FOOD_SHARK, 50, 99, 2);
    ASSERT_EQ(r.consumed, 0, "shark timer active not consumed");

    /* anglerfish can overheal: at 99/99, heals to 121 */
    r = osrs_eat_food(FOOD_ANGLERFISH, 99, 99, 0);
    ASSERT_EQ(r.consumed, 1, "anglerfish at full HP consumed (overheal)");
    ASSERT_EQ(r.hp_healed, 22, "anglerfish overheals 22");
}

/* --- potions --- */
static void test_drink_potion(void) {
    printf("--- osrs_drink_potion ---\n");

    /* prayer pot: 7 + floor(prayer_level / 4).  at 77: 7 + 19 = 26 */
    DrinkResult dr = osrs_drink_potion(POTION_PRAYER_RESTORE, 30, 77, 0);
    ASSERT_EQ(dr.consumed, 1, "prayer pot consumed");
    ASSERT_EQ(dr.prayer_restored, 26, "prayer pot restores 26 at lvl 77");

    /* super restore: 8 + floor(prayer_level / 4).  at 77: 8 + 19 = 27 */
    dr = osrs_drink_potion(POTION_SUPER_RESTORE, 30, 77, 0);
    ASSERT_EQ(dr.consumed, 1, "super restore consumed");
    ASSERT_EQ(dr.prayer_restored, 27, "super restore restores 27 at lvl 77");

    /* prayer pot at full prayer: can't drink */
    dr = osrs_drink_potion(POTION_PRAYER_RESTORE, 77, 77, 0);
    ASSERT_EQ(dr.consumed, 0, "prayer pot at full prayer not consumed");

    /* potion timer active: can't drink */
    dr = osrs_drink_potion(POTION_PRAYER_RESTORE, 30, 77, 2);
    ASSERT_EQ(dr.consumed, 0, "prayer pot timer active not consumed");

    /* antivenom+: cures venom, grants immunity */
    dr = osrs_drink_potion(POTION_ANTIVENOM_PLUS, 50, 77, 0);
    ASSERT_EQ(dr.consumed, 1, "antivenom consumed");
    ASSERT_EQ(dr.venom_cured, 1, "antivenom cures venom");
    ASSERT_EQ(dr.antivenom_ticks, 300, "antivenom grants 300 tick immunity");
}

/* --- brew --- */
static void test_brew(void) {
    printf("--- osrs_brew ---\n");

    /* sara brew: heals 15% + 2 of max HP = floor(99*0.15)+2 = 16.
       boosts def: floor(99*0.20)+2 = 21.
       drains att/str/range/magic: floor(99*0.10)+2 = 11 each. */
    BrewResult br = osrs_brew_effect(99, 99, 99, 99, 99);
    ASSERT_EQ(br.hp_healed, 16, "brew heals floor(99*0.15)+2=16");
    ASSERT_EQ(br.def_boost, 21, "brew def boost floor(99*0.20)+2=21");
    ASSERT_EQ(br.att_drain, 11, "brew att drain floor(99*0.10)+2=11");
    ASSERT_EQ(br.str_drain, 11, "brew str drain");
    ASSERT_EQ(br.range_drain, 11, "brew range drain");
    ASSERT_EQ(br.magic_drain, 11, "brew magic drain");
}

/* --- combo eat timing --- */
static void test_combo_timing(void) {
    printf("--- combo eat timing ---\n");

    ASSERT_EQ(osrs_can_eat(0), 1, "can eat when timer=0");
    ASSERT_EQ(osrs_can_eat(1), 0, "can't eat when timer=1");
    ASSERT_EQ(osrs_can_eat(3), 0, "can't eat when timer=3");
    ASSERT_EQ(osrs_can_drink(0), 1, "can drink when timer=0");
    ASSERT_EQ(osrs_can_drink(2), 0, "can't drink when timer=2");
}

int main(void) {
    test_food_heal_amount();
    test_eat_food();
    test_drink_potion();
    test_brew();
    test_combo_timing();

    printf("\n=== results: %d/%d passed ===\n", passed_tests, total_tests);
    return (passed_tests == total_tests) ? 0 : 1;
}
```

- [ ] **Step 2: Run tests to verify they fail**

```bash
cc -std=c11 -O0 -g -I. -o test_consumables pufferlib/ocean/osrs/tests/test_consumables.c -lm 2>&1 | head -5
```

Expected: compile errors (header doesn't exist yet).

- [ ] **Step 3: Implement osrs_consumables.h**

Create `pufferlib/ocean/osrs/osrs_consumables.h`:

```c
/**
 * @fileoverview osrs_consumables.h — shared food, potion, and brew consumption.
 *
 * pure functions that compute the effect of consuming food/potions/brews.
 * encounters call these instead of inlining eat/drink logic per encounter.
 * functions do NOT mutate state — they return result structs that the caller
 * applies. this keeps them testable and encounter-agnostic.
 *
 * SHARED FUNCTIONS:
 *   osrs_food_heal_amount(type)       heal amount for a food type
 *   osrs_eat_food(type, hp, max, tmr) compute food eat result
 *   osrs_drink_potion(type, ...)      compute potion drink result
 *   osrs_brew_effect(base levels)     compute saradomin brew effect
 *   osrs_can_eat(timer)               check if food timer allows eating
 *   osrs_can_drink(timer)             check if potion timer allows drinking
 *
 * ref: OSRS wiki food/potion articles, osrs-dps-calc
 */

#ifndef OSRS_CONSUMABLES_H
#define OSRS_CONSUMABLES_H

#include <stdint.h>

/* food types */
typedef enum {
    FOOD_SHARK = 0,
    FOOD_KARAMBWAN,
    FOOD_MANTA_RAY,
    FOOD_ANGLERFISH,
    FOOD_SARADOMIN_BREW,
    NUM_FOOD_TYPES
} FoodType;

/* potion types */
typedef enum {
    POTION_PRAYER_RESTORE = 0,
    POTION_SUPER_RESTORE,
    POTION_ANTIVENOM_PLUS,
    POTION_RANGING,
    POTION_SUPER_COMBAT,
    POTION_IMBUED_HEART,
    NUM_POTION_TYPES
} PotionType;

/* result from eating food */
typedef struct {
    int hp_healed;
    int consumed;       /* 1 if food was actually eaten */
} EatResult;

/* result from drinking a potion */
typedef struct {
    int prayer_restored;
    int level_boost;
    int venom_cured;
    int antivenom_ticks;
    int consumed;
} DrinkResult;

/* result from saradomin brew */
typedef struct {
    int hp_healed;
    int def_boost;
    int att_drain;
    int str_drain;
    int range_drain;
    int magic_drain;
} BrewResult;

/* food heal amounts (wiki-sourced) */
static inline int osrs_food_heal_amount(FoodType type) {
    switch (type) {
        case FOOD_SHARK:       return 20;
        case FOOD_KARAMBWAN:   return 18;
        case FOOD_MANTA_RAY:   return 22;
        case FOOD_ANGLERFISH:  return 22;
        default: return 0;
    }
}

/* timer checks */
static inline int osrs_can_eat(int food_timer) { return food_timer <= 0; }
static inline int osrs_can_drink(int potion_timer) { return potion_timer <= 0; }

/* eat food: compute result. caller applies hp change and timer.
   anglerfish can overheal (eat at full HP). all others require HP < max.
   heal is clamped so HP doesn't exceed max (except anglerfish overheal). */
static inline EatResult osrs_eat_food(FoodType type, int current_hp, int max_hp, int food_timer) {
    EatResult r = {0, 0};
    if (food_timer > 0) return r;

    int heal = osrs_food_heal_amount(type);
    if (heal <= 0) return r;

    /* anglerfish can overheal — always consumable */
    if (type == FOOD_ANGLERFISH) {
        r.consumed = 1;
        /* overheal cap: max_hp + floor(base_hp * 0.1) + 2, but for simplicity
           in our sim we just allow the full heal amount to overheal.
           the encounter clamps to its own overheal cap if desired. */
        r.hp_healed = heal;
        return r;
    }

    /* normal food: can't eat at full HP */
    if (current_hp >= max_hp) return r;

    r.consumed = 1;
    r.hp_healed = heal;
    /* clamp so total doesn't exceed max */
    if (current_hp + heal > max_hp) r.hp_healed = max_hp - current_hp;
    return r;
}

/* drink potion: compute result. caller applies effect and timer.
   prayer pots can't be drunk at full prayer. antivenom always drinkable. */
static inline DrinkResult osrs_drink_potion(PotionType type, int current_prayer,
                                             int prayer_level, int potion_timer) {
    DrinkResult r = {0, 0, 0, 0, 0};
    if (potion_timer > 0) return r;

    switch (type) {
        case POTION_PRAYER_RESTORE:
            if (current_prayer >= prayer_level) return r;
            r.consumed = 1;
            r.prayer_restored = 7 + prayer_level / 4;
            break;
        case POTION_SUPER_RESTORE:
            if (current_prayer >= prayer_level) return r;
            r.consumed = 1;
            r.prayer_restored = 8 + prayer_level / 4;
            break;
        case POTION_ANTIVENOM_PLUS:
            r.consumed = 1;
            r.venom_cured = 1;
            r.antivenom_ticks = 300;
            break;
        case POTION_RANGING:
            r.consumed = 1;
            r.level_boost = 5 + prayer_level / 10;  /* +5 + 10% of level */
            break;
        case POTION_SUPER_COMBAT:
            r.consumed = 1;
            r.level_boost = 5 + prayer_level * 15 / 100;  /* +5 + 15% of level */
            break;
        case POTION_IMBUED_HEART:
            r.consumed = 1;
            r.level_boost = 1 + prayer_level / 10;  /* +1 + 10% of level */
            break;
        default:
            break;
    }
    return r;
}

/* saradomin brew effect: heals HP, boosts def, drains att/str/range/magic.
   all parameters are BASE levels (99 typically).
   ref: osrs wiki "saradomin brew" */
static inline BrewResult osrs_brew_effect(int base_hp, int base_att,
                                           int base_str, int base_range,
                                           int base_magic) {
    BrewResult r;
    r.hp_healed = base_hp * 15 / 100 + 2;  /* floor(base*0.15) + 2 */
    r.def_boost = base_hp * 20 / 100 + 2;  /* floor(base*0.20) + 2 (uses HP base for def) */
    r.att_drain = base_att * 10 / 100 + 2;
    r.str_drain = base_str * 10 / 100 + 2;
    r.range_drain = base_range * 10 / 100 + 2;
    r.magic_drain = base_magic * 10 / 100 + 2;
    return r;
}

#endif /* OSRS_CONSUMABLES_H */
```

- [ ] **Step 4: Run tests to verify they pass**

```bash
cc -std=c11 -O0 -g -I. -o test_consumables pufferlib/ocean/osrs/tests/test_consumables.c -lm && ./test_consumables
```

Expected: all tests pass.

- [ ] **Step 5: Commit**

```bash
git add pufferlib/ocean/osrs/osrs_consumables.h pufferlib/ocean/osrs/tests/test_consumables.c
git commit -m "add osrs_consumables.h: shared food, potion, brew functions with tests"
```

### Task B2: Create osrs_special_attacks.h with spec dispatch

Weapon-specific special attack resolution. dispatch by item index. returns a result
struct — encounters apply damage/heal/drain from the result.

Reference: `.refs/osrs-dps-calc/src/lib/`, `.refs/osrs-sdk/src/weapons/`,
current implementations in `osrs_pvp_combat.h` (lines 830-1000+) and `encounter_zulrah.h` (lines 1031-1130).

**Files:**
- Create: `pufferlib/ocean/osrs/osrs_special_attacks.h`
- Modify: `pufferlib/ocean/osrs/tests/test_special_attacks.c` (extend with new dispatch tests)

- [ ] **Step 1: Write tests for spec dispatch**

Extend `pufferlib/ocean/osrs/tests/test_special_attacks.c` by adding a new test section
at the end (before `main`'s return). The existing 222 tests must remain untouched.
Add tests for the new `osrs_resolve_spec()` function:

```c
/* --- osrs_resolve_spec dispatch tests --- */
static void test_spec_dispatch(void) {
    printf("--- osrs_resolve_spec dispatch ---\n");
    uint32_t rng = 12345;

    /* AGS: 1.25x accuracy, 1.375x max hit (floor(max * 11/8)) */
    ASSERT_EQ(osrs_spec_cost(ITEM_AGS), 50, "AGS costs 50%");

    /* blowpipe: 2x accuracy, 1.5x max hit, heal 50% of damage */
    ASSERT_EQ(osrs_spec_cost(ITEM_BLOWPIPE), 50, "blowpipe costs 50%");

    /* DWH: 1.5x accuracy, same max hit, 30% def drain on hit */
    ASSERT_EQ(osrs_spec_cost(ITEM_STATIUS_WARHAMMER), 35, "DWH costs 35%");

    /* dragon dagger: 1.15x acc, 1.15x max hit, 2 hits */
    ASSERT_EQ(osrs_spec_cost(ITEM_DRAGON_DAGGER), 25, "DDS costs 25%");

    /* unknown weapon: cost 0 (can't spec) */
    ASSERT_EQ(osrs_spec_cost(ITEM_BARROWS_GLOVES), 0, "non-weapon has no spec");

    /* resolve AGS with guaranteed hit (huge att_roll vs tiny def) */
    rng = 42;
    SpecResult sr = osrs_resolve_spec(ITEM_AGS, 100000, 50, 100, 99, &rng);
    ASSERT_EQ(sr.num_hits, 1, "AGS is 1 hit");
    /* with 100000 att_roll * 1.25 vs 100 def: guaranteed hit */
    total_tests++;
    if (sr.total_damage >= 0 && sr.total_damage <= 50 * 11 / 8) { passed_tests++; }
    else { printf("FAIL: AGS damage %d out of range [0, %d]\n", sr.total_damage, 50 * 11 / 8); }

    /* resolve blowpipe spec with guaranteed hit */
    rng = 99;
    sr = osrs_resolve_spec(ITEM_BLOWPIPE, 100000, 30, 100, 99, &rng);
    ASSERT_EQ(sr.num_hits, 1, "blowpipe is 1 hit");
    /* heal should be 50% of damage */
    ASSERT_EQ(sr.heal, sr.total_damage / 2, "blowpipe heals 50% of damage");
}
```

Add a call to `test_spec_dispatch()` in the test file's `main()`.

The test file will need a new include: `#include "pufferlib/ocean/osrs/osrs_special_attacks.h"`.

- [ ] **Step 2: Run tests to verify new tests fail (header doesn't exist)**

```bash
cc -std=c11 -O0 -g -I. -o test_special_attacks pufferlib/ocean/osrs/tests/test_special_attacks.c -lm 2>&1 | head -5
```

Expected: compile error about missing header.

- [ ] **Step 3: Implement osrs_special_attacks.h**

Create `pufferlib/ocean/osrs/osrs_special_attacks.h`. This file implements `osrs_spec_cost()` and `osrs_resolve_spec()` with a switch dispatch over weapon item indices.

The implementation should:
- include `osrs_combat.h` for `osrs_hit_chance`, `encounter_rand_int`, `encounter_rand_float`
- include `osrs_items.h` for `ITEM_*` constants
- implement each weapon's spec multipliers as documented in the osrs-dps-calc
- dragon claws cascade: port from `osrs_pvp_combat.h` lines 830-900
- blowpipe: port from existing `osrs_blowpipe_spec_resolve` in osrs_combat.h
- AGS: 1.25x accuracy, floor(max_hit * 11 / 8) damage
- DWH: 1.5x accuracy, 30% def drain on hit
- BGS: 2x accuracy, drain def by damage dealt
- dragon dagger: 1.15x acc + 1.15x max, 2 hits
- VLS: 20-120% max hit, 25% def roll
- voidwaker: magic-based damage, 50-150% of magic max hit, 25% def
- MSB(i): 10/7 accuracy, 2 arrows
- eye of ayak: 2x accuracy, 1.3x max hit, magic def drain
- granite maul: no acc boost, instant hit
- dark bow: 2 arrows, min 8 each (or 5 in PvP)
- default (unknown weapon): return empty result

```c
/**
 * @fileoverview osrs_special_attacks.h — weapon special attack dispatch.
 *
 * pure function that resolves a special attack given pre-computed combat stats.
 * encounters call osrs_resolve_spec() and apply the returned SpecResult.
 *
 * ref: .refs/osrs-dps-calc/src/lib/ for multipliers,
 *      .refs/osrs-sdk/src/weapons/ for behavior,
 *      osrs_pvp_combat.h for existing claws/VLS implementations.
 */

#ifndef OSRS_SPECIAL_ATTACKS_H
#define OSRS_SPECIAL_ATTACKS_H

#include "osrs_combat.h"
#include "osrs_items.h"

typedef struct {
    int num_hits;
    int damage[4];
    int total_damage;
    int heal;
    int def_drain;
    int magic_def_drain;
    int freeze_ticks;
    int spec_cost;
    int attack_speed_override;
} SpecResult;
```

The full implementation of each weapon's spec mechanics follows — this is the largest
single function in the file (~300 lines of switch cases). reference the existing
implementations in `osrs_pvp_combat.h` and `encounter_zulrah.h` for exact formulas.
consult `.refs/osrs-dps-calc/src/lib/` for any formula you're unsure about.

each weapon case follows this pattern:
```c
case ITEM_AGS: {
    int spec_att = att_roll * 5 / 4;  /* 1.25x accuracy */
    int spec_max = max_hit * 11 / 8;  /* 1.375x max hit */
    r.spec_cost = 50;
    r.num_hits = 1;
    if (encounter_rand_float(rng_state) < osrs_hit_chance(spec_att, def_roll)) {
        r.damage[0] = encounter_rand_int(rng_state, spec_max + 1);
    }
    r.total_damage = r.damage[0];
    break;
}
```

Close with `#endif`.

- [ ] **Step 4: Run tests to verify all pass (old 222 + new dispatch tests)**

```bash
cc -std=c11 -O0 -g -I. -o test_special_attacks pufferlib/ocean/osrs/tests/test_special_attacks.c -lm && ./test_special_attacks
```

Expected: all tests pass (222 + new tests).

- [ ] **Step 5: Move osrs_blowpipe_spec_resolve out of osrs_combat.h**

Now that blowpipe spec lives in osrs_special_attacks.h, remove the standalone
`osrs_blowpipe_spec_resolve()` function and the `BLOWPIPE_SPEC_*` defines from
`osrs_combat.h`. Update any test that references the old function to use
`osrs_resolve_spec(ITEM_BLOWPIPE, ...)` instead.

Check what currently uses it:
- `test_special_attacks.c` — update tests to use new dispatch
- `encounter_inferno.h` — update blowpipe spec call site

- [ ] **Step 6: Verify all builds + all tests pass**

```bash
python setup.py build_osrs_inferno --force 2>&1 | tail -1
python setup.py build_osrs_zulrah --force 2>&1 | tail -1
python setup.py build_osrs_pvp --force 2>&1 | tail -1
cc -std=c11 -O0 -g -I. -o test_combat_math pufferlib/ocean/osrs/tests/test_combat_math.c -lm && ./test_combat_math 2>&1 | tail -1
cc -std=c11 -O0 -g -I. -o test_item_effects pufferlib/ocean/osrs/tests/test_item_effects.c -lm && ./test_item_effects 2>&1 | tail -1
cc -std=c11 -O0 -g -I. -o test_special_attacks pufferlib/ocean/osrs/tests/test_special_attacks.c -lm && ./test_special_attacks 2>&1 | tail -1
```

- [ ] **Step 7: Commit**

```bash
git add pufferlib/ocean/osrs/osrs_special_attacks.h pufferlib/ocean/osrs/tests/test_special_attacks.c pufferlib/ocean/osrs/osrs_combat.h
git commit -m "add osrs_special_attacks.h: weapon spec dispatch with tests, move blowpipe spec from combat.h"
```

---

## Workstream C: Monster manifest expansion

### Task C1: Add zulrah forms and snakelings to monster manifest

Pure data task — add 5 NPC entries to the manifest, regenerate the header.
Zero code logic changes.

**Files:**
- Modify: `pufferlib/ocean/osrs/tools/monsters_manifest.json`
- Regenerate: `pufferlib/ocean/osrs/osrs_monsters_generated.h`

- [ ] **Step 1: Add zulrah entries to manifest**

Append these 5 entries to the end of the JSON array in
`pufferlib/ocean/osrs/tools/monsters_manifest.json` (before the closing `]`):

```json
  {
    "index": "MON_ZULRAH_GREEN",
    "npc_id": 2042,
    "version": "Serpentine",
    "comment": "Zulrah green/ranged form"
  },
  {
    "index": "MON_ZULRAH_RED",
    "npc_id": 2043,
    "version": "Magma",
    "comment": "Zulrah red/melee form"
  },
  {
    "index": "MON_ZULRAH_BLUE",
    "npc_id": 2044,
    "version": "Tanzanite",
    "comment": "Zulrah blue/magic form"
  },
  {
    "index": "MON_ZULRAH_SNAKELING_MELEE",
    "npc_id": 2045,
    "version": "Melee",
    "comment": "Snakeling melee variant"
  },
  {
    "index": "MON_ZULRAH_SNAKELING_MAGIC",
    "npc_id": 2046,
    "version": "Magic",
    "comment": "Snakeling magic variant"
  }
```

- [ ] **Step 2: Regenerate the header**

```bash
python pufferlib/ocean/osrs/tools/generate_monsters.py
```

Expected output includes: `manifest: 19 monsters` (14 existing + 5 new).

- [ ] **Step 3: Verify generated stats match expected values**

Check the generated header for correct zulrah stats:

```bash
grep -A5 "MON_ZULRAH_GREEN" pufferlib/ocean/osrs/osrs_monsters_generated.h | head -8
grep -A5 "MON_ZULRAH_RED" pufferlib/ocean/osrs/osrs_monsters_generated.h | head -8
grep -A5 "MON_ZULRAH_BLUE" pufferlib/ocean/osrs/osrs_monsters_generated.h | head -8
```

Expected values:
- Green: hp=500, def_level=300, magic_def=-45, ranged_def=50, max_hit=41
- Red: hp=500, def_level=300, magic_def=0, ranged_def=300, max_hit=30
- Blue: hp=500, def_level=300, magic_def=300, ranged_def=0, max_hit=41

- [ ] **Step 4: Verify all 3 env builds pass**

```bash
python setup.py build_osrs_inferno --force 2>&1 | tail -1
python setup.py build_osrs_zulrah --force 2>&1 | tail -1
python setup.py build_osrs_pvp --force 2>&1 | tail -1
```

- [ ] **Step 5: Commit**

```bash
git add pufferlib/ocean/osrs/tools/monsters_manifest.json pufferlib/ocean/osrs/osrs_monsters_generated.h
git commit -m "add zulrah forms and snakelings to monster manifest (5 NPCs: 2042-2046)"
```

---

## Workstream D: Zulrah migration (blocked by A + B + C)

### Task D1: Replace ZulGearTierStats with encounter_compute_loadout_stats

The most impactful single change: delete the 100+ lines of precomputed gear tier
constants and derive them from ITEM_DATABASE at reset time.

**Files:**
- Modify: `pufferlib/ocean/osrs/encounters/encounter_zulrah.h`

- [ ] **Step 1: Add EncounterLoadoutStats fields to ZulrahState**

In the `ZulrahState` struct, replace gear tier stat tracking with computed loadout stats:

```c
/* replace: int gear_tier; (keep this — it selects which loadout arrays to use) */
/* add: */
EncounterLoadoutStats mage_stats;   /* computed at reset from ZUL_MAGE_LOADOUT[tier] */
EncounterLoadoutStats range_stats;  /* computed at reset from ZUL_RANGE_LOADOUT[tier] */
```

- [ ] **Step 2: Compute stats at reset instead of using precomputed constants**

In `zul_reset()`, after `encounter_apply_loadout()`, compute stats:

```c
/* compute loadout stats from real item database */
encounter_compute_loadout_stats(
    ZUL_MAGE_LOADOUT[s->gear_tier], ATTACK_STYLE_MAGIC,
    (s->gear_tier >= 1) ? ENCOUNTER_PRAYER_AUGURY : ENCOUNTER_PRAYER_NONE,
    99, 0, 30,  /* base level 99, rapid (+0), barrage base 30 */
    &s->mage_stats);
encounter_compute_loadout_stats(
    ZUL_RANGE_LOADOUT[s->gear_tier], ATTACK_STYLE_RANGED,
    (s->gear_tier >= 1) ? ENCOUNTER_PRAYER_RIGOUR : ENCOUNTER_PRAYER_NONE,
    99, 0, 0,
    &s->range_stats);
```

- [ ] **Step 3: Replace all ZulGearTierStats references in combat code**

Update `zul_player_attack_hits()`, `zul_player_attack()`, `zul_player_spec()`,
`zul_player_def_roll()` to read from `s->mage_stats` / `s->range_stats` instead
of `ZUL_GEAR_TIERS[s->gear_tier]`.

For example in `zul_player_attack_hits()`:
```c
/* old: */
const ZulGearTierStats* t = &ZUL_GEAR_TIERS[s->gear_tier];
int eff_level = is_mage ? t->eff_mage_level : t->eff_range_level;
int att_bonus = is_mage ? t->mage_att_bonus : t->range_att_bonus;
int att_roll = eff_level * (att_bonus + 64);

/* new: */
EncounterLoadoutStats* ls = is_mage ? &s->mage_stats : &s->range_stats;
int att_roll = osrs_player_att_roll(ls->eff_level, ls->attack_bonus);
```

- [ ] **Step 4: Delete ZulGearTierStats struct and ZUL_GEAR_TIERS array**

Remove the `ZulGearTierStats` typedef and the entire `ZUL_GEAR_TIERS[3]` initializer
(approximately lines 385-459 of encounter_zulrah.h).

- [ ] **Step 5: Verify build + heuristic sanity**

```bash
python setup.py build_osrs_zulrah --force 2>&1 | tail -1
```

Expected: builds successfully.

- [ ] **Step 6: Commit**

```bash
git add pufferlib/ocean/osrs/encounters/encounter_zulrah.h
git commit -m "zulrah: replace precomputed gear tier stats with encounter_compute_loadout_stats"
```

### Task D2: Replace hardcoded NPC stats with MONSTER_DATABASE lookups

**Files:**
- Modify: `pufferlib/ocean/osrs/encounters/encounter_zulrah.h`

- [ ] **Step 1: Add monster index includes and form-to-monster mapping**

At the top of encounter_zulrah.h (after existing includes), add:

```c
#include "../osrs_monsters_generated.h"
```

Add a form-to-monster-index mapping:

```c
static const int ZUL_FORM_MONSTER_IDX[] = {
    [ZUL_FORM_GREEN] = MON_ZULRAH_GREEN,
    [ZUL_FORM_RED]   = MON_ZULRAH_RED,
    [ZUL_FORM_BLUE]  = MON_ZULRAH_BLUE,
};
```

- [ ] **Step 2: Replace all ZUL_* stat defines with MONSTER_DATABASE lookups**

Delete these defines:
```c
#define ZUL_BASE_HP        500
#define ZUL_MAX_HIT        41
#define ZUL_ATTACK_SPEED   3
#define ZUL_DEF_LEVEL      300
#define ZUL_GREEN_DEF_MAGIC   (-45)
#define ZUL_GREEN_DEF_RANGED  50
#define ZUL_RED_DEF_MAGIC     0
#define ZUL_RED_DEF_RANGED    300
#define ZUL_BLUE_DEF_MAGIC    300
#define ZUL_BLUE_DEF_RANGED   0
#define ZUL_NPC_RANGED_ATT_ROLL  35112
#define ZUL_SNAKELING_MELEE_MAX_HIT 15
#define ZUL_SNAKELING_MAGIC_MAX_HIT 13
```

Replace usages throughout with `MONSTER_DATABASE[idx]` lookups. For example:

```c
/* old: */
s->zulrah.base_hitpoints = ZUL_BASE_HP;
s->zulrah.current_hitpoints = ZUL_BASE_HP;

/* new: */
const MonsterStats* green = &MONSTER_DATABASE[MON_ZULRAH_GREEN];
s->zulrah.base_hitpoints = green->hp;
s->zulrah.current_hitpoints = green->hp;
```

For per-form defence:
```c
/* old: */
int def_roll = (ZUL_DEF_LEVEL + 8) * (def_bonus + 64);

/* new: */
const MonsterStats* m = &MONSTER_DATABASE[ZUL_FORM_MONSTER_IDX[s->current_form]];
int def_bonus = is_mage ? m->magic_def : m->ranged_def;
int def_roll = (m->def_level + 8) * (def_bonus + 64);
```

For NPC attack rolls, compute from MonsterStats instead of precomputed constant:
```c
/* old: */
float chance = osrs_hit_chance(ZUL_NPC_RANGED_ATT_ROLL, def_roll);

/* new: */
const MonsterStats* m = &MONSTER_DATABASE[MON_ZULRAH_GREEN];
int npc_att_roll = osrs_npc_attack_roll(m->range_level, m->range_att_bonus);
float chance = osrs_hit_chance(npc_att_roll, def_roll);
```

For snakelings:
```c
/* old: */
int sn_max = sn->is_magic ? ZUL_SNAKELING_MAGIC_MAX_HIT : ZUL_SNAKELING_MELEE_MAX_HIT;

/* new: */
const MonsterStats* sn_mon = &MONSTER_DATABASE[
    sn->is_magic ? MON_ZULRAH_SNAKELING_MAGIC : MON_ZULRAH_SNAKELING_MELEE];
int sn_max = sn_mon->max_hit;
```

Note: red form max_hit changes from 41 to 30 (wiki-accurate). This is correct —
red form does melee damage in the 20-30 range, not 41.

- [ ] **Step 3: Verify build passes**

```bash
python setup.py build_osrs_zulrah --force 2>&1 | tail -1
```

- [ ] **Step 4: Commit**

```bash
git add pufferlib/ocean/osrs/encounters/encounter_zulrah.h
git commit -m "zulrah: replace hardcoded NPC stats with MONSTER_DATABASE lookups"
```

### Task D3: Replace duplicated combat functions with osrs_combat.h calls

**Files:**
- Modify: `pufferlib/ocean/osrs/encounters/encounter_zulrah.h`

- [ ] **Step 1: Replace zul_hit_chance_double with osrs_hit_chance_double**

Delete the `zul_hit_chance_double()` function (lines 782-789). Replace all call sites
with `osrs_hit_chance_double()` from osrs_combat.h.

- [ ] **Step 2: Simplify zul_player_def_roll to use shared functions**

Replace the inline player defence roll computation with `osrs_player_def_roll_vs_npc()`
from osrs_combat.h:

```c
/* old: zul_player_def_roll() — 15 lines of inline logic */
/* new: */
static int zul_player_def_roll(ZulrahState* s, int attack_style) {
    EncounterLoadoutStats* ls = (s->player_gear == ZUL_GEAR_MAGE)
        ? &s->mage_stats : &s->range_stats;
    int def_bonus = encounter_player_def_bonus(
        ls->def_stab, ls->def_slash, ls->def_crush,
        ls->def_magic, ls->def_ranged,
        attack_style, 0);
    return osrs_player_def_roll_vs_npc(99, 99, def_bonus, attack_style);
}
```

- [ ] **Step 3: Replace zul_has_recoil_effect with shared version**

Delete `zul_has_recoil_effect()` (duplicated from pvp_combat.h). Either use the
pvp version directly or add a shared `osrs_has_recoil_effect()` to osrs_combat.h.
Simplest approach: inline the check at the call site since it's a 2-line function:

```c
int ring = p->equipped[GEAR_SLOT_RING];
int has_recoil = (ring == ITEM_RING_OF_RECOIL || ring == ITEM_RING_OF_SUFFERING_RI);
```

- [ ] **Step 4: Verify build + all tests pass**

```bash
python setup.py build_osrs_zulrah --force 2>&1 | tail -1
python setup.py build_osrs_inferno --force 2>&1 | tail -1
python setup.py build_osrs_pvp --force 2>&1 | tail -1
cc -std=c11 -O0 -g -I. -o test_combat_math pufferlib/ocean/osrs/tests/test_combat_math.c -lm && ./test_combat_math 2>&1 | tail -1
cc -std=c11 -O0 -g -I. -o test_player_combat pufferlib/ocean/osrs/tests/test_player_combat.c -lm && ./test_player_combat 2>&1 | tail -1
```

- [ ] **Step 5: Commit**

```bash
git add pufferlib/ocean/osrs/encounters/encounter_zulrah.h
git commit -m "zulrah: replace duplicated combat functions with osrs_combat.h calls"
```

### Task D4: Replace zul_player_spec with osrs_resolve_spec

**Files:**
- Modify: `pufferlib/ocean/osrs/encounters/encounter_zulrah.h`

- [ ] **Step 1: Replace the ~90-line zul_player_spec function**

Delete the entire `zul_player_spec()` function. Replace with a version that uses
`osrs_resolve_spec()`:

```c
static void zul_player_spec(ZulrahState* s) {
    if (!s->zulrah_visible || s->is_diving) return;
    if (s->player_attack_timer > 0) return;
    if (s->player_stunned_ticks > 0) return;

    /* determine which weapon to spec with based on gear */
    int weapon_idx = s->player.equipped[GEAR_SLOT_WEAPON];
    int cost = osrs_spec_cost(weapon_idx);
    if (cost <= 0 || s->player_special_energy < cost) return;

    /* compute attack roll and max hit from current loadout stats */
    EncounterLoadoutStats* ls = (s->player_gear == ZUL_GEAR_MAGE)
        ? &s->mage_stats : &s->range_stats;
    int att_roll = osrs_player_att_roll(ls->eff_level, ls->attack_bonus);

    /* NPC defence roll for current form */
    const MonsterStats* m = &MONSTER_DATABASE[ZUL_FORM_MONSTER_IDX[s->current_form]];
    int is_mage = (ls->style == ATTACK_STYLE_MAGIC);
    int npc_def_bonus = is_mage ? m->magic_def - s->magic_def_drain : m->ranged_def;
    if (npc_def_bonus < -64) npc_def_bonus = -64;
    int def_roll = (m->def_level + 8) * (npc_def_bonus + 64);
    if (def_roll < 0) def_roll = 0;

    SpecResult sr = osrs_resolve_spec(weapon_idx, att_roll, ls->max_hit,
                                       def_roll, m->def_level, &s->rng_state);

    s->player_special_energy -= sr.spec_cost;
    s->player.just_attacked = 1;
    s->player.used_special_this_tick = 1;
    s->player.last_attack_style = ls->style;
    s->player.attack_style_this_tick = ls->style;
    s->player_attack_timer = sr.attack_speed_override > 0
        ? sr.attack_speed_override : ls->attack_speed;

    /* apply damage */
    if (sr.total_damage > 0) {
        int capped = zul_cap_damage(s, sr.total_damage);
        encounter_damage_player(&s->zulrah, capped, NULL);
        s->damage_dealt_this_tick += capped;
        s->total_damage_dealt += capped;
    }

    /* apply heal (blowpipe) */
    if (sr.heal > 0) {
        s->player.current_hitpoints += sr.heal;
        if (s->player.current_hitpoints > s->player.base_hitpoints)
            s->player.current_hitpoints = s->player.base_hitpoints;
    }

    /* apply magic def drain (eye of ayak) */
    s->magic_def_drain += sr.magic_def_drain;

    /* visual */
    s->zulrah.hit_landed_this_tick = 1;
    s->zulrah.hit_damage = sr.total_damage;
    s->zulrah.hit_was_successful = (sr.total_damage > 0);
}
```

- [ ] **Step 2: Verify build passes**

```bash
python setup.py build_osrs_zulrah --force 2>&1 | tail -1
```

- [ ] **Step 3: Commit**

```bash
git add pufferlib/ocean/osrs/encounters/encounter_zulrah.h
git commit -m "zulrah: replace per-tier spec logic with osrs_resolve_spec dispatch"
```

### Task D5: Replace food/potion logic with osrs_consumables.h calls

**Files:**
- Modify: `pufferlib/ocean/osrs/encounters/encounter_zulrah.h`

- [ ] **Step 1: Add consumables include**

Add to encounter_zulrah.h includes:
```c
#include "../osrs_consumables.h"
```

- [ ] **Step 2: Replace zul_process_food with shared calls**

```c
static void zul_process_food(ZulrahState* s, int a) {
    if (a == 0) return;
    FoodType type = (a == 1) ? FOOD_SHARK : FOOD_KARAMBWAN;
    int* count = (a == 1) ? &s->player_food_count : &s->player_karambwan_count;
    if (*count <= 0) return;

    EatResult r = osrs_eat_food(type, s->player.current_hitpoints,
                                 s->player.base_hitpoints, s->player_food_timer);
    if (!r.consumed) return;
    (*count)--;
    s->player_food_timer = 3;
    s->player.current_hitpoints += r.hp_healed;
    if (s->player.current_hitpoints > s->player.base_hitpoints)
        s->player.current_hitpoints = s->player.base_hitpoints;
}
```

- [ ] **Step 3: Replace zul_process_potion with shared calls**

```c
static void zul_process_potion(ZulrahState* s, int a) {
    if (a == 0) return;
    if (a == 1) {
        if (s->player_restore_doses <= 0) return;
        DrinkResult r = osrs_drink_potion(POTION_PRAYER_RESTORE,
            s->player.current_prayer, s->player.base_prayer, s->player_potion_timer);
        if (!r.consumed) return;
        s->player_restore_doses--;
        s->player_potion_timer = 3;
        s->player.current_prayer += r.prayer_restored;
        if (s->player.current_prayer > s->player.base_prayer)
            s->player.current_prayer = s->player.base_prayer;
    } else if (a == 2) {
        if (s->antivenom_doses <= 0 || s->player_potion_timer > 0) return;
        DrinkResult r = osrs_drink_potion(POTION_ANTIVENOM_PLUS, 0, 0, 0);
        s->antivenom_doses--;
        s->player_potion_timer = 3;
        s->venom_counter = 0;
        s->venom_timer = 0;
        s->antivenom_timer = r.antivenom_ticks;
    }
}
```

- [ ] **Step 4: Verify build passes**

```bash
python setup.py build_osrs_zulrah --force 2>&1 | tail -1
```

- [ ] **Step 5: Commit**

```bash
git add pufferlib/ocean/osrs/encounters/encounter_zulrah.h
git commit -m "zulrah: replace inline food/potion logic with osrs_consumables.h calls"
```

### Task D6: Final verification — all builds + all tests

**Files:** none (verification only)

- [ ] **Step 1: Regenerate both databases**

```bash
python pufferlib/ocean/osrs/tools/generate_monsters.py
python pufferlib/ocean/osrs/tools/generate_items.py
```

- [ ] **Step 2: Build all 3 environments**

```bash
python setup.py build_osrs_inferno --force 2>&1 | tail -1
python setup.py build_osrs_zulrah --force 2>&1 | tail -1
python setup.py build_osrs_pvp --force 2>&1 | tail -1
```

Expected: all 3 print `Built: pufferlib/_C.cpython-312-darwin.so`

- [ ] **Step 3: Run all test suites**

```bash
cc -std=c11 -O0 -g -I. -o test_combat_math pufferlib/ocean/osrs/tests/test_combat_math.c -lm && ./test_combat_math 2>&1 | tail -1
cc -std=c11 -O0 -g -I. -o test_item_effects pufferlib/ocean/osrs/tests/test_item_effects.c -lm && ./test_item_effects 2>&1 | tail -1
cc -std=c11 -O0 -g -I. -o test_special_attacks pufferlib/ocean/osrs/tests/test_special_attacks.c -lm && ./test_special_attacks 2>&1 | tail -1
cc -std=c11 -O0 -g -I. -o test_consumables pufferlib/ocean/osrs/tests/test_consumables.c -lm && ./test_consumables 2>&1 | tail -1
cc -std=c11 -O0 -g -I. -o test_player_combat pufferlib/ocean/osrs/tests/test_player_combat.c -lm && ./test_player_combat 2>&1 | tail -1
```

Expected: all pass.

- [ ] **Step 4: Verify encounter_zulrah.h line count reduced**

```bash
wc -l pufferlib/ocean/osrs/encounters/encounter_zulrah.h
```

Expected: ~1200-1400 lines (down from 2364).
