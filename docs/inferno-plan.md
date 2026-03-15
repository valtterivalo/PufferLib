# inferno encounter implementation plan

> **For agentic workers:** Use superpowers:subagent-driven-development or superpowers:executing-plans.

**Goal:** full 69-wave inferno encounter as encounter_inferno.h, plugging into the existing encounter vtable.

**Architecture:** single header file (encounter_inferno.h) following encounter_zulrah.h patterns. reuses Player struct, AttackStyle, OverheadPrayer from osrs_pvp_types.h. new LOS system goes into osrs_pvp_collision.h (shared across encounters). binding goes into pufferlib/ocean/osrs_inferno/binding.c.

**Spec:** docs/inferno-specs.md

---

## file structure

| file | action | responsibility |
|---|---|---|
| pufferlib/ocean/osrs_pvp/osrs_pvp_collision.h | modify | add LOS ray tracing (shared, used by inferno + future zulrah fix) |
| pufferlib/ocean/osrs_pvp/encounters/encounter_inferno.h | create | all inferno logic: NPCs, waves, arena, AI, obs, reward, vtable |
| pufferlib/ocean/osrs_inferno/binding.c | create | vecenv binding (same pattern as osrs_zulrah) |
| pufferlib/ocean/osrs_inferno/ symlinks | create | symlinks to shared osrs_pvp headers (same as osrs_zulrah) |
| pufferlib/ocean/osrs_pvp/osrs_pvp.c | modify | register inferno encounter in registry |
| docs/inferno-specs.md | delete after impl | specs absorbed into code comments |

## build order (7 tasks)

### task 1: LOS system in osrs_pvp_collision.h (~150 lines)

add to existing collision header:
- directional bitmask constants (FULL_MASK, EAST/WEST/NORTH/SOUTH_MASK)
- `typedef struct { int x, y, size; uint32_t los_mask; } LOSBlocker;`
- `int has_line_of_sight(LOSBlocker* blockers, int count, int x1, int y1, int x2, int y2, int src_size, int range)` — fixed-point Q16 ray tracing from spec
- NPC LOS variant (flip to closest point)
- melee adjacency check (range=1 shortcut)

test: build osrs_pvp, verify existing functionality not broken.

commit: "add LOS ray tracing system to collision header"

### task 2: inferno types and constants (~200 lines)

top of encounter_inferno.h:
- arena dimensions, pillar positions, spawn positions
- NPC type enum (INF_NPC_NIBBLER through INF_NPC_ZUK)
- NPC stats table: hp, attack_style, attack_speed, attack_range, size, max_hit, def_bonuses
- InfernoNPC struct: type, x, y, hp, attack_timer, attack_style, active, target, special state (burrow timer, etc.)
- InfernoState struct: player, npcs array, pillars, wave number, tick, arena LOS blockers
- wave composition table (69 entries, each a list of NPC type IDs)

commit: "add inferno types, NPC stats, wave table"

### task 3: monster AI — movement + attack (~300 lines)

- `inf_npc_move()`: 1 tile toward target, diagonal if clear, corner safespot handling
- `inf_npc_can_attack()`: LOS check through pillars, range check, not-under-target check
- `inf_npc_attack()`: damage roll, prayer check, melee switchover (50% for ranger/mager)
- per-type special behaviors:
  - nibbler: targets nearest pillar, eats HP
  - blob: prayer reading at tick-3, splits into 2 smaller blobs on death
  - meleer: burrow mechanic (12-tick dig, teleport to player)
  - mager: heal other NPCs
  - jad: alternating range/mage, 8-tick cycle
- `inf_tick_all_npcs()`: iterate all active NPCs, move then attack

commit: "implement inferno monster AI: movement, attacks, per-type behaviors"

### task 4: arena, pillars, wave system (~200 lines)

- pillar struct: x, y, hp, active, los_blocker
- `inf_rebuild_los_blockers()`: collect active pillars into LOSBlocker array
- `inf_spawn_wave()`: look up wave table, place NPCs at spawn positions
- `inf_check_wave_complete()`: all NPCs dead -> advance wave
- `inf_player_attack()`: player attacks targeted NPC (tbow/blowpipe/sang/ice barrage)
- nibbler pillar damage per tick

commit: "add arena, pillars, wave spawning, wave transitions"

### task 5: zuk phase (~200 lines)

- zuk state: shield position, shield movement, healers, final phase flag
- shield mechanic: shield NPC moves left-right, blocks zuk's attacks
- healer spawning: jal-mejjak NPCs that heal zuk
- final phase: faster attacks (7-tick), no shield
- jad spawning during zuk fight

commit: "implement zuk phase: shield, healers, final phase"

### task 6: observations, actions, rewards (~250 lines)

observations (estimated ~200 features):
- player: hp, prayer, position, prayer active, spec energy, food/pots remaining
- per-NPC (up to ~12 visible): type, hp, position, attack_timer, attack_style, distance
- pillars: hp, positions
- wave number, tick

actions (6 heads):
- movement (9: idle + 8 directions)
- prayer (4: none/melee/range/mage)
- attack target (max_npcs + 1: none or NPC index)
- eat (3: none/food/karambwan)
- potion (3: none/restore/brew)
- special (2: none/spec)

reward shaping:
- damage dealt (small positive)
- damage taken (small negative)
- correct prayer when attacked (small positive)
- wave completion (medium positive)
- pillar destroyed (medium negative)
- death = -1, zuk kill = +1

commit: "add inferno obs/action/reward, encounter vtable"

### task 7: binding + registration (~100 lines)

- create pufferlib/ocean/osrs_inferno/binding.c (copy zulrah pattern)
- create symlinks to shared headers
- register encounter in osrs_pvp.c encounter registry
- add to Makefile visual target
- build and smoke test

commit: "add osrs_inferno binding, register encounter, verify build"

## notes

- no TDD here — this is a C game simulation, not a library. verification is: does it build, does it train, does the visual look right.
- the LOS system is the highest-value piece — it benefits all encounters including retroactive zulrah pillar fixes.
- NPC stats may need wiki verification for some monsters (bat, blob, meleer HP and max hits not in our reference code).
- zuk shield movement pattern needs investigation (left-right oscillation speed).
