# OSRS test suite

standalone C test binaries that verify combat math, item interactions, and special
attacks against the osrs-dps-calc TypeScript reference. no test framework — each file
is a self-contained binary with its own `main()`.

## building and running

all commands run from the repo root (`pufferlib-metal/`):

```bash
# build + run all tests (copy-paste block):
cc -std=c11 -O0 -g -I. -o test_combat_math ocean/osrs/tests/test_combat_math.c -lm && ./test_combat_math
cc -std=c11 -O0 -g -I. -o test_item_effects ocean/osrs/tests/test_item_effects.c -lm && ./test_item_effects
cc -std=c11 -O0 -g -I. -o test_special_attacks ocean/osrs/tests/test_special_attacks.c -lm && ./test_special_attacks
cc -std=c11 -O0 -g -I. -o test_player_combat ocean/osrs/tests/test_player_combat.c -lm && ./test_player_combat
cc -std=c11 -O0 -g -I. -o test_consumables ocean/osrs/tests/test_consumables.c -lm && ./test_consumables
cc -std=c11 -O0 -g -I. -o test_bolt_procs ocean/osrs/tests/test_bolt_procs.c -lm && ./test_bolt_procs
cc -std=c11 -O0 -g -I. -o test_damage ocean/osrs/tests/test_damage.c -lm && ./test_damage
cc -std=c11 -O0 -g -I. -o test_inventory ocean/osrs/tests/test_inventory.c -lm && ./test_inventory
cc -std=c11 -O0 -g -I. -o test_interaction ocean/osrs/tests/test_interaction.c -lm && ./test_interaction
```

each binary prints `=== results: N/N passed ===` on the last line. exit code 0 = all passed.

## test files

| file | tests | what it covers |
|---|---|---|
| `test_combat_math.c` | 155 | NPC combat formulas (hit chance, tbow scaling, barrage AoE, NPC max hits, NPC attack rolls), player defence rolls vs NPCs, loadout stat computation from ITEM_DATABASE, prayer drain |
| `test_item_effects.c` | 164 | tbow accuracy/damage edge cases and monotonicity, PvP/PvE prayer reduction, NPC defence rolls for specific monsters, player attack rolls with full gear loadouts, two-handed weapon logic, end-to-end hit chance, defence bonus selection by style |
| `test_special_attacks.c` | 222+ | spec weapon costs, accuracy/strength multipliers, dragon claws cascade, DWH/BGS defence drain, dark bow double-hit clamping, morrigan's bleed, voidwaker magic hit, VLS reduced defence, volatile staff, godsword variants, blowpipe spec |
| `test_player_combat.c` | ~30+ | player effective level (all prayers + style bonuses), player attack roll, player melee/ranged/magic max hit, prayer damage reduction (PvE vs PvP), osmumten's fang double accuracy roll, equipment bonus summation |
| `test_consumables.c` | ~25+ | food healing amounts, eat timing/clamping, anglerfish overheal, potion restore formulas, antivenom immunity, saradomin brew effects, combo eat timing |
| `test_bolt_procs.c` | 131 | diamond/opal/ruby bolt proc chances, effect formulas, ZCB guaranteed procs, miss behavior, caps, edge cases |
| `test_damage.c` | 66 | damage pipeline (prayer reduction, vengeance reflect, recoil, smite drain), full PvP/PvE pipeline, edge cases, osrs_has_recoil_ring helper |
| `test_inventory.c` | 148 | inventory add/remove/find, equip from inventory, equip swap, two-handed weapon logic, unequip, gear slot mapping |
| `test_interaction.c` | — | entity interaction system (attack/follow/spec toggle), shared across encounters |
| `test_collision.c` | — | collision map loading, tile walkability, BFS pathfinding (moved from osrs/ root) |

## reference data

tests are cross-referenced against:

- `.refs/osrs-dps-calc/src/lib/` — authoritative TypeScript reference for all player combat formulas
- `.refs/osrs-dps-calc/src/tests/` — existing reference test values
- `.refs/osrs-sdk/src/weapons/` — special attack implementations
- `.refs/InfernoTrainer/src/` — NPC behavior, hit delays
- OSRS wiki — high-level formula descriptions

## adding tests

each test file uses the same harness pattern:

```c
static int total_tests = 0;
static int passed_tests = 0;

#define ASSERT_EQ(a, b, msg) do { \
    total_tests++; \
    if ((a) != (b)) { \
        printf("FAIL: %s: got %d, expected %d\n", msg, (int)(a), (int)(b)); \
    } else { passed_tests++; } \
} while(0)
```

group related tests in `static void test_*()` functions, call them from `main()`,
print the section name with `printf("--- section name ---\n")`.

## when to run

- after any change to combat math (`osrs_combat.h`)
- after any change to items (`osrs_items.h`, `osrs_items_generated.h`, codegen)
- after any change to monsters (`osrs_monsters_generated.h`, codegen)
- after any change to encounter combat logic
- before committing changes to any of the above
