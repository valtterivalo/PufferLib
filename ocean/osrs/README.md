# OSRS environments

C implementation of Old School RuneScape encounters for reinforcement learning.
three encounters: 69-wave Inferno (PvE), Zulrah (multi-phase boss), NH PvP (1v1 LMS).

## build

training envs (each overwrites `pufferlib/_C.cpython-312-darwin.so`):
```bash
python setup.py build_osrs_inferno --force
python setup.py build_osrs_zulrah --force
python setup.py build_osrs_pvp --force
```

visual binary (standalone, uses raylib):
```bash
cd ocean/osrs && make visual
./osrs_visual --encounter inferno
./osrs_visual --encounter zulrah
./osrs_visual                          # default: PvP
```

other Makefile targets: `make` (headless benchmark), `make debug` (sanitizers).

## training

```bash
python pufferl.py train osrs_inferno
python pufferl.py train osrs_pvp
python pufferl.py sweep osrs_inferno --timeout 4
python pufferl.py results osrs_inferno
```

configs: `pufferlib/config/metal/ocean/<env>.ini`. any .ini key is a CLI flag
(e.g. `--learning-rate 0.05`). see top-level `CLAUDE.md` for config system details.

## architecture

### shared modules (all encounters)

| file | what it does |
|---|---|
| `osrs_types.h` | core types, enums, structs |
| `osrs_combat.h` | player/NPC combat formulas, hit chance, max hit, prayer |
| `osrs_consumables.h` | food, potions, brews |
| `osrs_special_attacks.h` | 19 weapon spec dispatch |
| `osrs_bolt_procs.h` | diamond, opal, ruby bolt procs |
| `osrs_damage.h` | damage pipeline (veng, recoil, smite) |
| `osrs_inventory.h` | 28-slot inventory + equipment |
| `osrs_items.h` | item definitions |
| `osrs_items_generated.h` | 133 items codegen from wiki |
| `osrs_monsters_generated.h` | 19 NPCs codegen from wiki |
| `osrs_encounter.h` | movement, pathfinding, gear switching, rendering interface |
| `osrs_collision.h` | collision map loading + queries |
| `osrs_pathfinding.h` | BFS pathfinding |

### PvP-specific

`osrs_pvp_combat.h`, `osrs_pvp_gear.h`, `osrs_pvp_actions.h`, `osrs_pvp_movement.h`,
`osrs_pvp_observations.h`, `osrs_pvp_opponents.h`, `osrs_pvp_api.h`, `osrs_pvp_effects.h`

### rendering (shared)

`osrs_render.h`, `osrs_models.h`, `osrs_anim.h`, `osrs_gui.h`, `osrs_terrain.h`,
`osrs_objects.h`, `osrs_human_input.h`

### encounters

| file | encounter |
|---|---|
| `encounters/encounter_inferno.h` | 69-wave Inferno |
| `encounters/encounter_zulrah.h` | Zulrah (3 forms, venom, clouds) |
| `encounters/encounter_nh_pvp.h` | NH PvP (28 scripted opponents, PFSP) |

`osrs_env.h` is the include aggregator — pull it in for full environment access.

## adding a new encounter

1. create `encounters/encounter_<name>.h` implementing the encounter vtable from `osrs_encounter.h`
2. add NPC stats to `tools/monsters_manifest.json` and run `python tools/generate_monsters.py`
3. export visual assets (see `tools/README.md` for the full asset pipeline)
4. add a build target in `setup.py` and a config at `pufferlib/config/metal/ocean/<name>.ini`

## tests

996 tests across 8 suites covering combat math, items, special attacks, bolt procs,
consumables, damage pipeline, and inventory. see `tests/README.md` for build commands
and coverage details.

## asset pipeline

manifest-driven codegen from wiki JSON, OSRS cache, and RuneLite gameval constants.
see `tools/README.md` for the full pipeline and manifest format.

## file organization

```
osrs/
  osrs_env.h              include aggregator
  osrs_*.h                shared modules (combat, items, damage, etc.)
  osrs_pvp_*.h            PvP-specific subsystems
  osrs_visual.c           standalone visual binary entry point
  encounters/             encounter implementations
  data/                   binary assets + codegen headers (gitignored)
  tools/                  asset pipeline scripts + manifests
  tests/                  standalone C test binaries
  scripts/                cache export scripts (models, terrain, sprites)
  Makefile                standalone build targets
```
