# OSRS PvP Environment

C implementation of Old School RuneScape NH PvP for reinforcement learning.
~1.1M env steps/sec standalone, ~235K+ training SPS on Metal.

## Build and train

```bash
python setup.py build_osrs_pvp --inplace --force
python train_pvp.py --no-wandb --total-timesteps 50000000

# zulrah (separate build, overwrites _C.so)
python setup.py build_osrs_zulrah --inplace --force
python train_zulrah.py --no-wandb --total-timesteps 500000000
```

Two bindings: `binding.c` (metal vecenv.h) and `ocean_binding.c` (PufferLib env_binding.h).

## Data assets

Not in git. Exported from the OSRS game cache:

1. Download a modern cache from https://archive.openrs2.org/ ("flat file" export)
2. `cd pufferlib/ocean/osrs_pvp && ./scripts/export_all.sh /path/to/cache`

Pure Python, no deps.

## Spaces

**Obs:** 373 = 334 features + 39 action mask, normalized in C.

**Actions:** MultiDiscrete `[9, 13, 6, 2, 5, 2, 2]` — loadout, combat, prayer, food, potion, karambwan, veng.

**Timing:** tick N actions apply at tick N+1 (OSRS-accurate async).

## Opponents

28 scripted policies from trivial (`true_random`) to boss (`nightmare_nh` — onetick + 50% action reading). Curriculum mixes and PFSP supported.

## Encounters

Vtable interface (`osrs_encounter.h`). Current: NH PvP, Zulrah (81 obs, 6 heads, 3 forms, venom, clouds, collision).

## Files

Core env: `osrs_pvp_types/items/gear/combat/collision/pathfinding/movement/observations/actions/opponents/api.h`

Visual: `osrs_pvp_render/gui/anim/models/terrain/objects/effects/human_input.h`

Encounters: `encounters/encounter_nh_pvp.h`, `encounters/encounter_zulrah.h`

Data: `data/` (gitignored binaries + C model headers), `scripts/` (cache exporters)
