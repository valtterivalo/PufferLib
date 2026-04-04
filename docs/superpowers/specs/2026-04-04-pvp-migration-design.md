# PvP migration to shared OSRS systems

date: 2026-04-04
status: approved
branch: inferno-encounter

## problem

NH PvP has a completely separate 6200-line codebase (7 files) that reimplements core OSRS
mechanics already available in shared modules. combat formulas, special attacks, hit delays,
prayer checks, food/potions, vengeance, recoil, and pending hit queues are all duplicated.
a second PvP encounter would have to fork 6200 lines instead of writing only encounter-specific
code.

## architecture: two tiers

```
tier 1: OSRS core (shared by all encounters)
  osrs_combat.h              — hit chance, rolls, max hits, prayer reduction
  osrs_damage.h (NEW)        — pending hits, veng, recoil, smite, damage pipeline
  osrs_consumables.h         — food, potions, brews
  osrs_special_attacks.h     — weapon spec dispatch
  osrs_bolt_procs.h          — bolt procs
  osrs_inventory.h (NEW)     — 28-slot inventory + equipment, gear switching
  osrs_items.h               — item database
  osrs_encounter.h           — movement, pathfinding, rendering

tier 2: encounter-specific
  encounter_nh_pvp.h         — NH 1v1 obs, actions, reward, opponent AI, PID, farcast
  encounter_inferno.h        — waves, NPCs, Zuk phase
  encounter_zulrah.h         — rotations, clouds, snakelings
```

## new shared module: osrs_damage.h

extracted from osrs_pvp_combat.h apply_damage() (line 569) + pending hit system.

### pending hit queue

every OSRS attack creates a pending hit with a tick delay (projectile flight time).
when countdown reaches 0, damage lands. this exists in both PvP (osrs_pvp_combat.h:537)
and PvE (EncounterPendingHit in osrs_encounter.h:79). unify into one system.

```c
#define OSRS_MAX_PENDING_HITS 16

typedef struct {
    int active;
    int damage;
    int ticks_remaining;
    int attack_style;       /* for prayer check at land time */
    int source_idx;         /* attacker entity index */
    int target_idx;         /* defender entity index */
    int check_prayer;       /* 1 = re-check prayer when hit lands (jad, delayed spells) */
    int spell_type;         /* ENCOUNTER_SPELL_* for freeze/heal on land */
    int is_pvp;             /* 1 = 40% prayer reduction, 0 = 100% block */
} OsrsPendingHit;
```

### damage application pipeline

when a pending hit lands, the shared pipeline:
1. prayer check: `osrs_prayer_reduce_damage(dmg, prayer, style, is_pvp)`
2. vengeance: if target has veng active, reflect `floor(dmg * 0.75)` back to attacker
3. recoil: if target has recoil ring, reflect `floor(dmg * 0.1) + 1`, track charges
4. smite: if attacker has smite prayer, drain `floor(dmg / 4)` prayer from target
5. apply damage to target HP

```c
typedef struct {
    int final_damage;       /* after prayer reduction */
    int veng_damage;        /* reflected by vengeance (0 if no veng) */
    int recoil_damage;      /* reflected by recoil ring (0 if no ring) */
    int smite_drain;        /* prayer drained by smite (0 if no smite) */
    int prayer_blocked;     /* 1 if prayer blocked/reduced this hit */
} DamageResult;

DamageResult osrs_apply_damage(
    int raw_damage, int attack_style,
    int target_prayer, int is_pvp,
    int target_veng_active, int* target_veng_flag,
    int target_has_recoil, int* recoil_charges,
    int attacker_has_smite, int* target_prayer_points
);
```

all encounters use this. PvE encounters pass is_pvp=0, veng flags from player state.
PvP passes is_pvp=1. the function is pure — modifies only via output pointers.

### what this replaces

- osrs_pvp_combat.h `apply_damage()` (lines 569-711) — the core 142-line function
- osrs_pvp_combat.h `queue_hit()` (line 537) — hit queue with 12 params
- osrs_pvp_combat.h `process_pending_hits()` (line 694) — tick countdown
- osrs_encounter.h `EncounterPendingHit` — simpler version of same thing
- encounter_zulrah.h `zul_apply_player_damage()` recoil logic — uses shared pipeline

## new shared module: osrs_inventory.h

extracted from osrs_pvp_gear.h slot-based inventory system.

real OSRS: every player has 28 inventory slots + 11 equipment slots. gear switching
means moving items between inventory and equipment. 2-handed weapons unequip shields.
this is how every encounter works in the real game.

### core API

```c
#define OSRS_INVENTORY_SIZE 28

typedef struct {
    uint8_t equipment[NUM_GEAR_SLOTS];     /* equipped items */
    uint8_t inventory[OSRS_INVENTORY_SIZE]; /* inventory slots (ITEM_NONE = empty) */
    int inventory_count;                    /* number of occupied slots */
} OsrsEquipment;

/* equip an item: move from inventory to equipment slot, swap if occupied.
   handles 2H weapon auto-unequip of shield. returns 1 if successful. */
int osrs_equip_item(OsrsEquipment* eq, uint8_t item_idx);

/* unequip to inventory. returns 1 if successful (inventory not full). */
int osrs_unequip_item(OsrsEquipment* eq, int gear_slot);

/* check if inventory has space for N items */
int osrs_inventory_has_space(const OsrsEquipment* eq, int count);

/* find item in inventory, returns slot index or -1 */
int osrs_inventory_find(const OsrsEquipment* eq, uint8_t item_idx);
```

### what this replaces

- osrs_pvp_gear.h `slot_equip_item()` (line 283) — with dirty flag + spec weapon update
- osrs_pvp_gear.h `has_free_inventory_slot()` (line 1129)
- the per-slot inventory arrays in the Player struct

### what stays in PvP

- LMS loot system (add_loot_item, upgrade tables, tier randomization)
- loadout resolution (priority tables for best available gear)
- these USE osrs_inventory.h but are PvP/LMS-specific logic

## osrs_pvp_combat.h migration (1605 → ~400 lines)

### what gets deleted (replaced by shared calls)

| lines | what | replacement |
|---|---|---|
| 37-154 | spec cost/multiplier tables | osrs_spec_cost() + osrs_resolve_spec() |
| 179-303 | effective level calcs | osrs_player_eff_level() adapters |
| 388-438 | calculate_hit_chance + max_hit | osrs_hit_chance() + osrs_player_*_max_hit() |
| 444-470 | magic spell helpers | move to osrs_combat.h |
| 476-514 | hit delay functions | encounter_*_hit_delay() |
| 520 | is_prayer_correct | encounter_prayer_correct_for_style() |
| 537-711 | queue_hit + apply_damage + process_pending | osrs_damage.h |
| 852-1031 | perform_*_spec functions (5 specs) | osrs_resolve_spec() |

### what stays (PvP-specific or thin adapters)

| lines | what | why it stays |
|---|---|---|
| 160-175 | get_defence_prayer_mult | PvP offensive prayer → defence; thin adapter over shared |
| 309-383 | bonus lookups from Player struct | thin adapters reading from Player.equipped |
| 717-846 | combat history tracking | RL observation specific |
| 1038-1260 | attack availability checks | PvP action masking |
| 1262-1605 | perform_attack (rewritten) | orchestration: calls shared spec + bolt + damage |

### Dharok effect

PvP calculate_max_hit has Dharok's quadratic scaling (missing HP → extra damage).
this is a set effect, not PvP-specific — any encounter with Dharok's gear needs it.
add to osrs_combat.h as `osrs_dharok_max_hit_bonus(current_hp, max_hp, base_max)`.

## osrs_pvp_gear.h migration (1123 → ~800 lines)

### what changes

- `compute_slot_gear_bonuses()` → calls `osrs_sum_equipment_bonuses()` internally
- equip/unequip → calls `osrs_equip_item()` / `osrs_unequip_item()` from osrs_inventory.h
- item_to_gear_slot → moves to osrs_inventory.h (shared)

### what stays

- LMS loot tables, tier randomization, priority-based loadout resolution
- these are LMS-encounter-specific, not core OSRS

## osrs_pvp_actions.h migration (1002 → ~900 lines)

### what changes

- eat_food/drink_potion → use osrs_consumables.h functions for heal/restore computation
- attack delay on eating stays (PvP-specific timing, but could become shared later)

### what stays

- action dispatch (execute_switches, execute_attacks)
- reward calculation
- timer management

## parallel workstreams

```
A: osrs_damage.h (NEW)           — pending hits, veng, recoil, smite pipeline
B: osrs_inventory.h (NEW)        — slot inventory, equip/unequip
C: osrs_pvp_combat.h migration   — replace formulas, specs, delays with shared calls
D: osrs_pvp_gear.h migration     — use shared inventory + bonus computation
E: osrs_pvp_actions.h migration  — use shared consumables
F: final verification             — all builds + all tests
```

dependency graph:
```
A (damage) ────┐
B (inventory) ──┼──→ C (pvp_combat) ──→ D (pvp_gear) ──→ E (pvp_actions) ──→ F
               │
               └──→ D (pvp_gear uses inventory)
```

A and B are independent (new files, zero overlap). C depends on A. D depends on B and C.
E depends on C. F depends on everything.

## verification

```bash
python setup.py build_osrs_pvp --force
python setup.py build_osrs_inferno --force
python setup.py build_osrs_zulrah --force
# all test suites:
cc -std=c11 -O0 -g -I. -o test_combat_math pufferlib/ocean/osrs/tests/test_combat_math.c -lm && ./test_combat_math
cc -std=c11 -O0 -g -I. -o test_item_effects pufferlib/ocean/osrs/tests/test_item_effects.c -lm && ./test_item_effects
cc -std=c11 -O0 -g -I. -o test_special_attacks pufferlib/ocean/osrs/tests/test_special_attacks.c -lm && ./test_special_attacks
cc -std=c11 -O0 -g -I. -o test_player_combat pufferlib/ocean/osrs/tests/test_player_combat.c -lm && ./test_player_combat
cc -std=c11 -O0 -g -I. -o test_consumables pufferlib/ocean/osrs/tests/test_consumables.c -lm && ./test_consumables
cc -std=c11 -O0 -g -I. -o test_bolt_procs pufferlib/ocean/osrs/tests/test_bolt_procs.c -lm && ./test_bolt_procs
```

all 3 builds + 6 test suites must pass. PvP training should produce same reward curves.
