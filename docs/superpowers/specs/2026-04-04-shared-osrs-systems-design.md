# shared OSRS systems: combat, consumables, special attacks

date: 2026-04-04
status: approved
branch: inferno-encounter
validation target: zulrah migration

## problem

OSRS combat formulas, consumable logic, and special attack mechanics are reimplemented
per encounter. the same hit chance formula exists in osrs_combat_shared.h, osrs_pvp_combat.h,
and inline in encounter_zulrah.h. gear stats are precomputed as constants in zulrah, derived
from ITEM_DATABASE in inferno, and computed inline in PvP. adding a new encounter means
re-deriving combat math from scratch and hoping it matches the wiki.

the goal: encounter-specific code contains ONLY encounter-specific logic (rotations, phase
machines, spawn patterns, reward functions, observation spaces). all core OSRS mechanics
live in shared modules that any encounter can call.

## approach

bottom-up primitives. build shared modules first, then migrate zulrah as the validation
target. PvP migrates progressively later — shared systems are designed to support PvP
(player-vs-player with 40% prayer reduction, gear switching, all spec weapons) but PvP
code stays functional throughout.

## reference authority chain

when implementing a formula, the authority order is:

1. **osrs-dps-calc** (`.refs/osrs-dps-calc/src/lib/`) — gold standard for player combat
   formulas. TypeScript, well-structured, community-maintained to match the real game.
   also the source for our equipment.json and monsters.json codegen pipelines.
2. **osrs-client-deob** (`.refs/osrs-client-deob/`) — decompiled game client (java).
   ground truth for edge cases where dps-calc and wiki disagree.
3. **osrs-sdk / InfernoTrainer** (`.refs/osrs-sdk/`, `.refs/InfernoTrainer/`) — reference
   for NPC behavior, hit delays, prayer interactions, special attack implementations.
4. **OSRS wiki** — last resort, useful for high-level descriptions but formulas can be
   imprecise or outdated.

## module 1: osrs_combat.h

**file**: `ocean/osrs/osrs_combat.h` (rename from osrs_combat_shared.h)

pure math, zero state, zero encounter dependencies. any C file can include it
and compute any OSRS combat number.

### existing (carried over from osrs_combat_shared.h)

- `osrs_hit_chance(att_roll, def_roll)` — standard accuracy formula
- `osrs_tbow_acc_mult(target_magic)` / `osrs_tbow_dmg_mult(target_magic)`
- `osrs_barrage_resolve(targets, ...)` — 3x3 AoE with independent rolls
- `osrs_npc_melee_max_hit` / `osrs_npc_ranged_max_hit` / `osrs_npc_magic_max_hit`
- `osrs_npc_attack_roll` / `osrs_npc_max_hit` (style dispatch)
- `osrs_player_def_roll_vs_npc` / `encounter_player_def_bonus`
- `encounter_npc_roll_attack` — accuracy+damage in one call
- `encounter_prayer_correct_for_style`
- RNG: `encounter_xorshift`, `encounter_rand_int`, `encounter_rand_float`
- hit delays: `encounter_magic_hit_delay`, `encounter_ranged_hit_delay`, `encounter_blowpipe_hit_delay`
- `encounter_dist_to_npc`, `encounter_shuffle`
- `osrs_blowpipe_spec_resolve` (moves to osrs_special_attacks.h)

### new player-side primitives

```c
/* player effective level: floor(base * prayer_mult) + style_bonus + 8
   ref: osrs-dps-calc src/lib/PlayerVsNpc.ts effectiveLevel */
int osrs_player_eff_level(int base_level, float prayer_mult, int style_bonus);

/* player attack roll: eff_level * (equipment_bonus + 64)
   ref: osrs-dps-calc src/lib/PlayerVsNpc.ts maxAttackRoll */
int osrs_player_att_roll(int eff_level, int equipment_bonus);

/* player melee max hit: floor(0.5 + eff_str * (str_bonus + 64) / 640)
   ref: osrs-dps-calc src/lib/PlayerVsNpc.ts maxMeleeHit */
int osrs_player_melee_max_hit(int eff_str_level, int str_bonus);

/* player ranged max hit: same formula structure as melee
   ref: osrs-dps-calc src/lib/PlayerVsNpc.ts maxRangedHit */
int osrs_player_ranged_max_hit(int eff_range_level, int ranged_str_bonus);

/* player magic max hit: floor(base_spell_dmg * (1 + magic_dmg_pct/100))
   ref: osrs-dps-calc src/lib/PlayerVsNpc.ts maxMagicHit */
int osrs_player_magic_max_hit(int spell_base_dmg, int magic_dmg_pct);

/* prayer damage reduction.
   PvE: correct prayer blocks 100% of damage.
   PvP: correct prayer reduces by 40% (player takes 60%).
   ref: osrs-dps-calc, osrs wiki "protection prayers" */
int osrs_prayer_reduce_damage(int damage, int prayer, int attack_style, int is_pvp);

/* double accuracy roll (osmumten's fang, confliction gauntlets).
   if att >= def: 1 - (def+2)(2*def+3) / (6*(att+1)^2)
   if att < def: att*(4*att+5) / (6*(att+1)*(def+1))
   ref: osrs-dps-calc src/lib/OsmutensFang.ts */
float osrs_hit_chance_double(int att_roll, int def_roll);

/* sum equipment bonuses from a gear loadout using ITEM_DATABASE.
   iterates all slots, sums offensive + defensive bonuses.
   ref: osrs-dps-calc src/lib/Equipment.ts */
typedef struct {
    int attack_stab, attack_slash, attack_crush, attack_magic, attack_ranged;
    int defence_stab, defence_slash, defence_crush, defence_magic, defence_ranged;
    int melee_strength, ranged_strength, magic_damage, prayer;
    int attack_speed, attack_range;  /* from weapon slot */
} EquipmentBonuses;

void osrs_sum_equipment_bonuses(const uint8_t loadout[NUM_GEAR_SLOTS],
                                EquipmentBonuses* out);
```

### what moves out

- `osrs_blowpipe_spec_resolve` moves to osrs_special_attacks.h (it's a spec weapon)

### impact on existing code

- `encounter_compute_loadout_stats()` in osrs_encounter.h becomes a thin wrapper
  that calls `osrs_player_eff_level`, `osrs_player_att_roll`, `osrs_sum_equipment_bonuses`, etc.
- all `#include "osrs_combat_shared.h"` become `#include "osrs_combat.h"`
- PvP's `calculate_hit_chance()` is the same formula as `osrs_hit_chance()` —
  PvP migration can swap them later without changing behavior

### test file

`ocean/osrs/tests/test_player_combat.c` — verify every new function against
osrs-dps-calc reference values. test cases:
- effective level with each prayer (none, augury, rigour, piety) + style bonuses
- attack roll computation with known loadouts
- max hit for melee/ranged/magic with known gear
- prayer damage reduction: PvE full block, PvP 40% reduction, wrong prayer pass-through
- double accuracy roll: known att/def pairs, boundary conditions
- equipment bonus summation: empty loadout, single item, full loadout, verify each field

## module 2: osrs_consumables.h

**file**: `ocean/osrs/osrs_consumables.h`

shared food, potion, and brew consumption. pure functions that take player state
and return effects. encounters call these instead of inlining eat/drink logic.

### API

```c
/* food types — extensible enum, not hardcoded per encounter */
typedef enum {
    FOOD_SHARK = 0,       /* heals 20 */
    FOOD_KARAMBWAN,       /* heals 18 (combo food, different timer) */
    FOOD_MANTA_RAY,       /* heals 22 */
    FOOD_ANGLERFISH,      /* heals 22, can overheal */
    FOOD_SARADOMIN_BREW,  /* heals 15% + 2, boosts def, drains att/str/range/magic */
    NUM_FOOD_TYPES
} FoodType;

typedef enum {
    POTION_PRAYER_RESTORE = 0,  /* 7 + floor(prayer_level / 4) */
    POTION_SUPER_RESTORE,       /* 8 + floor(prayer_level / 4), restores stats too */
    POTION_ANTIVENOM_PLUS,      /* cures venom, 300 tick immunity */
    POTION_RANGING,             /* boosts ranged level */
    POTION_SUPER_COMBAT,        /* boosts att/str/def */
    POTION_IMBUED_HEART,        /* boosts magic level */
    NUM_POTION_TYPES
} PotionType;

/* result from eating food */
typedef struct {
    int hp_healed;
    int def_boost;          /* brew: +2 + 15% of base def */
    int att_drain;          /* brew: -2 - 10% of base att */
    int str_drain;          /* brew: -2 - 10% of base str */
    int range_drain;        /* brew: -2 - 10% of base range */
    int magic_drain;        /* brew: -2 - 10% of base magic */
    int consumed;           /* 1 if food was actually eaten (0 if timer blocked etc) */
} EatResult;

/* result from drinking a potion */
typedef struct {
    int prayer_restored;
    int level_boost;        /* ranging pot: +5 + 10%, super combat: +5 + 15% etc */
    int venom_cured;
    int antivenom_ticks;    /* immunity duration */
    int consumed;
} DrinkResult;

/* eat food. checks timer, computes heal, does NOT mutate player state —
   caller applies the result. this keeps the function pure and testable.
   ref: osrs-dps-calc, osrs wiki food article */
EatResult osrs_eat_food(FoodType type, int current_hp, int max_hp,
                        int food_timer, int base_def, int base_att,
                        int base_str, int base_range, int base_magic);

/* drink potion. same pattern — check timer, compute effect, return result.
   ref: osrs-dps-calc, osrs wiki potions article */
DrinkResult osrs_drink_potion(PotionType type, int current_prayer,
                              int prayer_level, int potion_timer);

/* OSRS combo eating rules:
   food and potions have separate 3-tick timers.
   karambwan has its own timer (can combo with food).
   ref: osrs wiki "tick eating" */
int osrs_can_eat(int food_timer);
int osrs_can_drink(int potion_timer);
int osrs_can_combo_eat(int karambwan_timer);  /* karambwan on separate timer */
```

### encounter usage pattern

```c
/* in encounter step function: */
if (actions[FOOD_HEAD] == FOOD_ACTION_SHARK) {
    EatResult r = osrs_eat_food(FOOD_SHARK, hp, max_hp, food_timer, ...);
    if (r.consumed) {
        hp += r.hp_healed;
        if (hp > max_hp) hp = max_hp;
        food_count--;
        food_timer = 3;
    }
}
```

### test file

`ocean/osrs/tests/test_consumables.c`:
- each food type heals correct amount
- food timer blocks eating
- combo eat timing (food + karambwan in same tick)
- brew heal + stat effects (boost def, drain others)
- prayer restore formula: 7 + floor(level/4) for prayer pot, 8 + floor(level/4) for super restore
- antivenom: cure + immunity duration
- edge cases: eating at full HP, drinking at full prayer, 0 doses remaining

## module 3: osrs_special_attacks.h

**file**: `ocean/osrs/osrs_special_attacks.h`

weapon-specific special attack mechanics. dispatch by item index, return a result
struct that the encounter applies to game state.

### API

```c
typedef struct {
    int num_hits;              /* 1 for most, 2 for MSB(i), 4 for dragon claws */
    int damage[4];             /* per-hit damage */
    int total_damage;          /* sum of damage[] */
    int heal;                  /* blowpipe: 50% of dmg, SGS: floor(dmg/2)+floor(dmg/4) */
    int def_drain;             /* DWH: 30% of target current def, BGS: damage dealt */
    int magic_def_drain;       /* eye of ayak soul rend: damage dealt */
    int freeze_ticks;          /* ZGS: 20 ticks on hit */
    int spec_cost;             /* energy consumed (25-100) */
    int attack_speed_override; /* some specs change attack speed (0 = use default) */
} SpecResult;

/* resolve a special attack given pre-computed rolls.
   pure function: stats in, result out. no encounter state touched.

   weapon_item_idx: ITEM_DATABASE index (e.g. ITEM_AGS, ITEM_DRAGON_CLAWS)
   att_roll: base attack roll (spec applies its own multiplier)
   max_hit: base max hit (spec applies its own multiplier)
   def_roll: target defence roll (some specs modify this internally)
   target_current_def: for DWH/BGS drain calculation
   rng_state: xorshift state for damage rolls

   supported weapons (ref: osrs-dps-calc, .refs/osrs-sdk/src/weapons/):
     melee: AGS, dragon claws, DWH, BGS, ZGS, SGS, ancient godsword,
            voidwaker, VLS, granite maul, dragon dagger, elder maul
     ranged: blowpipe, MSB(i), dark bow, ACB, heavy ballista, morrigans javelin
     magic: volatile staff, eye of ayak (soul rend), zuriel's staff */
SpecResult osrs_resolve_spec(
    int weapon_item_idx,
    int att_roll,
    int max_hit,
    int def_roll,
    int target_current_def,
    uint32_t* rng_state
);

/* spec energy cost lookup (useful for masking — can the player spec?) */
int osrs_spec_cost(int weapon_item_idx);
```

### dragon claws cascade

the most complex spec — 4-hit cascade algorithm. currently implemented in
osrs_pvp_combat.h lines 830-900. moves here unchanged, just wrapped in the
dispatch. ref: osrs-dps-calc src/lib/DragonClaws.ts.

### encounter-applied effects

the encounter is responsible for applying SpecResult fields to game state:
- `total_damage` → damage the target
- `heal` → heal the attacker
- `def_drain` → reduce target's defence level (PvP tracks this on Player)
- `magic_def_drain` → zulrah tracks cumulative drain across forms
- `freeze_ticks` → set frozen_ticks on target

this keeps osrs_special_attacks.h stateless. the spec function doesn't need to
know about zulrah forms or PvP veng — it just computes the weapon's effect.

### item effects (future)

non-spec weapon/item effects that are also not encounter-specific:
- recoil/ring of suffering damage reflection
- sang staff passive (1/6 chance, heal 50% of damage)
- crystal armor set bonus (+30% accuracy with bowfa)
- tbow scaling (already in osrs_combat.h)

these can be added to osrs_combat.h or a new osrs_item_effects.h as needed.
not in scope for the initial implementation but the architecture supports it.

## module 4: monster manifest expansion

**file**: `ocean/osrs/tools/monsters_manifest.json`

add 5 new entries:

```json
{"index": "MON_ZULRAH_GREEN", "npc_id": 2042, "version": "Serpentine",
 "comment": "Zulrah green/ranged form"},
{"index": "MON_ZULRAH_RED", "npc_id": 2043, "version": "Magma",
 "comment": "Zulrah red/melee form"},
{"index": "MON_ZULRAH_BLUE", "npc_id": 2044, "version": "Tanzanite",
 "comment": "Zulrah blue/magic form"},
{"index": "MON_ZULRAH_SNAKELING_MELEE", "npc_id": 2045, "version": "Melee",
 "comment": "Snakeling melee variant"},
{"index": "MON_ZULRAH_SNAKELING_MAGIC", "npc_id": 2046, "version": "Magic",
 "comment": "Snakeling magic variant"}
```

regenerate osrs_monsters_generated.h. verify stats match current hardcoded values:
- green: hp=500, def=300, magic_def=-45, ranged_def=50, max_hit=41
- red: hp=500, def=300, magic_def=0, ranged_def=300, max_hit=30
- blue: hp=500, def=300, magic_def=300, ranged_def=0, max_hit=41
- snakeling melee: hp=1, max_hit=15
- snakeling magic: hp=1, max_hit=13

note: red form max_hit corrects from 41 (current hardcode) to 30 (wiki-accurate).

## zulrah migration (workstream D)

**file**: `ocean/osrs/encounters/encounter_zulrah.h`

blocked by modules 1-3. consumes all shared systems.

### what gets deleted from encounter_zulrah.h

- `ZulGearTierStats` struct and `ZUL_GEAR_TIERS[3]` array (~100 lines)
- all `ZUL_*` combat stat #defines (ZUL_BASE_HP, ZUL_DEF_LEVEL, ZUL_MAX_HIT,
  ZUL_GREEN_DEF_MAGIC, etc.)
- `zul_player_def_roll()` — replaced by `osrs_player_def_roll_vs_npc`
- `zul_hit_chance_double()` — replaced by `osrs_hit_chance_double`
- `zul_player_attack_hits()` — uses osrs_combat.h primitives
- `zul_player_spec()` (~90 lines) — replaced by `osrs_resolve_spec()`
- `zul_player_attack()` — simplified using shared formulas
- `zul_process_food()` / `zul_process_potion()` — use osrs_consumables.h
- `zul_has_recoil_effect()` — duplicated from pvp_combat.h, moves to shared
- `zul_apply_player_damage()` recoil logic — uses shared recoil helper

### what stays in encounter_zulrah.h

- rotation tables (`ZUL_ROT1..4`) and phase machine
- cloud spawn/tick logic (3x3 placement, safe tile avoidance, pending flight)
- snakeling spawn/tick logic (position picking, movement, attack)
- venom mechanics (25% envenom chance, escalating damage, antivenom interaction)
- melee stare-then-whip mechanic (form-specific behavior)
- observation space (`zul_write_obs`)
- action masks (`zul_write_mask`)
- reward function (`zul_compute_reward`)
- heuristic policy (`zul_heuristic_actions`)
- arena geometry, stand positions, render overlay
- EncounterDef vtable + registration

### ZulrahState changes

- remove: `player_food_timer`, `player_potion_timer` — use shared consumable state
- add: `EncounterLoadoutStats mage_stats, range_stats` — computed at reset
- change: gear tier still selects loadout arrays, but stats are derived not precomputed

estimated reduction: ~2365 lines → ~1200-1400 lines.

## parallel workstream decomposition

```
workstream A: osrs_combat.h
  owner: teammate-alpha
  files touched: osrs_combat.h (new, from osrs_combat_shared.h rename),
                 tests/test_player_combat.c (new)
  duration: medium
  no dependencies

workstream B: osrs_consumables.h + osrs_special_attacks.h
  owner: teammate-beta
  files touched: osrs_consumables.h (new), osrs_special_attacks.h (new),
                 tests/test_consumables.c (new),
                 tests/test_special_attacks.c (extend)
  duration: medium-large (spec dispatch has many weapons)
  no dependencies

workstream C: monster manifest expansion
  owner: teammate-gamma (or team-lead, it's small)
  files touched: tools/monsters_manifest.json, osrs_monsters_generated.h
  duration: small
  no dependencies

workstream D: zulrah migration
  owner: (assigned after A+B+C complete)
  files touched: encounter_zulrah.h
  blocked by: A, B, C
  duration: medium
```

A, B, C have zero file overlap and can run fully in parallel.

## verification gate

after all workstreams complete:

```bash
python ocean/osrs/tools/generate_monsters.py
python ocean/osrs/tools/generate_items.py
python setup.py build_osrs_inferno --force
python setup.py build_osrs_zulrah --force
python setup.py build_osrs_pvp --force
# all test suites:
cc -std=c11 -O0 -g -I. -o test_combat_math ocean/osrs/tests/test_combat_math.c -lm && ./test_combat_math
cc -std=c11 -O0 -g -I. -o test_item_effects ocean/osrs/tests/test_item_effects.c -lm && ./test_item_effects
cc -std=c11 -O0 -g -I. -o test_special_attacks ocean/osrs/tests/test_special_attacks.c -lm && ./test_special_attacks
cc -std=c11 -O0 -g -I. -o test_consumables ocean/osrs/tests/test_consumables.c -lm && ./test_consumables
cc -std=c11 -O0 -g -I. -o test_player_combat ocean/osrs/tests/test_player_combat.c -lm && ./test_player_combat
```

all 3 env builds + all test suites must pass.

## future work (out of scope for this spec)

- PvP progressive migration to shared combat/consumable systems
- osrs_item_effects.h for non-spec item passives (sang, crystal set, recoil)
- new encounters consuming the shared systems (fight caves, ToB, gauntlet)
- visual binary integration testing
