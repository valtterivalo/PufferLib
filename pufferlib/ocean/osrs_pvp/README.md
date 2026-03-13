# OSRS PvP Environment

High-performance C implementation of Old School RuneScape No-Honor PvP combat
for reinforcement learning. All N environments run in a single `vec_step` call
with zero per-env Python overhead.

Throughput: ~1.1M env steps/sec standalone, ~840K via Python binding (256 envs).
Training SPS depends on the backend — ~150K with PufferLib 4.0 MPS, ~235K+ with
custom Metal kernels.

## Building

The env compiles as a static library linked into PufferLib's `_C.so`. Two
binding flavors exist:

| file | pattern | used by |
|------|---------|---------|
| `binding.c` | metal `vecenv.h` (static-native) | `train_pvp.py`, metal backend |
| `ocean_binding.c` | PufferLib `env_binding.h` (ocean) | PufferLib 3.0/4.0 Python training |

```bash
# metal backend
python setup.py build_osrs_pvp --inplace --force
python train_pvp.py --no-wandb --total-timesteps 50000000

# zulrah encounter (separate build — overwrites _C.so)
python setup.py build_osrs_zulrah --inplace --force
python train_zulrah.py --no-wandb --total-timesteps 500000000
```

## Data assets

Binary data files (collision maps, 3D models, animations, sprites) are not
checked into git. They're exported from the OSRS game cache using the scripts
in `scripts/`.

### Setup from scratch

1. Download a modern OSRS cache from https://archive.openrs2.org/ — pick any
   recent revision, grab the "flat file" export. You'll get a directory with
   numbered subdirs (`0/`, `1/`, `2/`, `7/`, `255/`) and a `keys.json`.

2. Run the export script:
   ```bash
   cd pufferlib/ocean/osrs_pvp
   ./scripts/export_all.sh /path/to/osrs-cache
   ```

3. This populates `data/` with everything needed for training and the visual
   debug viewer. The export scripts are pure Python (stdlib only, no deps).

Item sprites require a separate Java exporter — see notes at the end of
`export_all.sh`.

## Observation space

373 features per agent (334 raw + 39 action mask):

| range | content |
|-------|---------|
| 0-118 | core: loadout, prayers, HP, consumables, timers, combat history, stats |
| 119-132 | gear bonuses (player + target visible defences) |
| 133-149 | game mode flags, ability checks, attack_timer_ready |
| 150-181 | per-slot features (weapon/style/prayer/equipped) |
| 182-325 | dynamic slot item stats (8 slots x 18 stats) |
| 326 | voidwaker magic damage flag |
| 327-333 | reward shaping tracking |
| 334-372 | action mask (39 floats) |

All observations are normalized in C via a static divisor array.

## Action space

MultiDiscrete with 7 heads: `[9, 13, 6, 2, 5, 2, 2]`

| head | name | dim | values |
|------|------|-----|--------|
| 0 | LOADOUT | 9 | KEEP, MELEE, RANGE, MAGE, TANK, SPEC_MELEE, SPEC_RANGE, SPEC_MAGIC, GMAUL |
| 1 | COMBAT | 13 | NONE, ATK, ICE, BLOOD, ADJACENT, UNDER, DIAGONAL, FARCAST_2..7 |
| 2 | OVERHEAD | 6 | NONE, MAGE, RANGED, MELEE, SMITE, REDEMPTION |
| 3 | FOOD | 2 | NONE, EAT |
| 4 | POTION | 5 | NONE, BREW, RESTORE, COMBAT, RANGED |
| 5 | KARAMBWAN | 2 | NONE, EAT |
| 6 | VENG | 2 | NONE, CAST |

SPEC loadouts are style-aware and atomic — SPEC_MELEE equips the best melee
spec weapon + gear + piety in one action.

## Timing model

Actions submitted on tick N take effect at tick N+1. This is OSRS-accurate
async timing:

```
Tick N:
  1. Apply pending actions (from tick N-1)
  2. Process hits, update timers
  3. Generate observations
  4. Agent computes and queues action (applies at tick N+1)
```

You can't react to in-flight attacks — you have to predict. Prayer switching
protects against the next attack, not the current one.

## Opponents

28 scripted opponents ranging from trivial to nightmare difficulty:

| opponent | difficulty | notes |
|----------|------------|-------|
| `true_random` | trivial | baseline |
| `improved` | medium | correct prayers, off-prayer attacks, proper eating |
| `onetick` | hard | 1-tick attacks, fake switches, step-under |
| `master_nh` | boss | onetick + 10% action reading |
| `nightmare_nh` | boss | onetick + 50% action reading |
| `veng_fighter` | hard | lunar spellbook, vengeance, no magic |

Plus curriculum mixes (`mixed_easy/medium/hard`), an ascending NH ladder
(`novice_nh` through `expert_nh`), and PFSP (prioritized fictitious self-play)
with dynamic win-rate-weighted sampling.

## Gear system

8 dynamic gear slots resolved from inventory via priority tables. 4 gear tiers
(basic LMS through bloodier key loot) with per-episode randomization and
correlated opponent tiers (80% same, 15% +/-1, 5% +/-2).

## Encounters

The encounter system supports boss PvE alongside PvP. Each encounter implements
a vtable interface (`osrs_encounter.h`) with create/destroy/reset/step/write_obs
etc. Current encounters:

- `encounter_nh_pvp.h` — NH PvP (wraps the core env)
- `encounter_zulrah.h` — Zulrah boss fight (3 forms, venom, snakelings, clouds,
  collision-aware movement, 3 gear tiers)

Zulrah dimensions: 81 obs + 24 mask = 105 total, 6 action heads.

## Collision and pathfinding

`osrs_pvp_collision.h` implements OSRS tile collision flags with 8-directional
traversal checks. `osrs_pvp_pathfinding.h` runs BFS on a 104x104 local grid
(OSRS scene size) with ~86KB stack-allocated working arrays.

Collision maps are loaded from `.cmap` files exported from the game cache.
When no map is loaded, all traversal checks pass (open arena mode).

## Visual debug viewer

The `osrs_pvp.c` standalone builds a raylib-based debug viewer with 3D model
rendering, animations, inventory GUI, and human-playable mode. Requires raylib
and the exported data assets.

```bash
# in the storm fork (has Makefile + raylib setup):
make visual && ./osrs_pvp_visual --visual
make visual && ./osrs_pvp_visual --visual --encounter zulrah
```

## File overview

| file | purpose |
|------|---------|
| `osrs_pvp_types.h` | core types, enums, Player struct, 8 dynamic gear slots |
| `osrs_pvp_items.h` | item database (60 items), int16 stats |
| `osrs_pvp_gear.h` | priority tables, loadout resolution, gear randomization |
| `osrs_pvp_combat.h` | damage formulas, hit delays, special attacks |
| `osrs_pvp_collision.h` | tile collision flags, Region/RegionMap, traversal |
| `osrs_pvp_pathfinding.h` | BFS pathfinder on 104x104 grid |
| `osrs_pvp_movement.h` | pathfinding, position updates |
| `osrs_pvp_observations.h` | observation generation + action masks |
| `osrs_pvp_actions.h` | action processing, style-aware spec execution |
| `osrs_pvp_api.h` | public API: pvp_init, pvp_reset, pvp_step, pvp_seed, pvp_close |
| `osrs_pvp_opponents.h` | 28 scripted C opponent policies |
| `osrs_pvp.h` | convenience header (includes all above) |
| `osrs_encounter.h` | encounter vtable interface |
| `osrs_pvp_render.h` | raylib debug viewer (2D grid + 3D models) |
| `osrs_pvp_gui.h` | OSRS-style inventory/prayer/spellbook panels |
| `osrs_pvp_anim.h` | animation runtime (vertex-group transforms) |
| `osrs_pvp_models.h` | .models binary loader for raylib meshes |
| `osrs_pvp_terrain.h` | .terrain loader for heightmaps |
| `osrs_pvp_objects.h` | .objects loader for placed world objects |
| `osrs_pvp_effects.h` | visual effects (hitsplats, projectiles, GFX) |
| `osrs_pvp_human_input.h` | click-to-play human control in viewer |
| `binding.c` | metal static-native vecenv binding |
| `ocean_binding.c` | PufferLib ocean env_binding.h binding |
| `osrs_pvp.c` | standalone demo + visual viewer entry point |
| `test_collision.c` | collision system unit tests (19 tests) |
| `data/` | exported binary assets (gitignored) + C model headers |
| `scripts/` | cache export tools (Python, stdlib only) |
