# OSRS asset pipeline

tools for generating game data (stats, 3D models, animations, effects) from the
OSRS cache and RuneLite gameval constants.

## how OSRS assets work

- **cache**: contains 3D models, textures, NPC definitions (with idle/walk anims), item
  definitions, spotanim/GFX definitions (projectile models + animations). the cache does
  NOT contain attack/death animations — those are server-driven.
- **gameval**: RuneLite's deob client has Java constant files that name every animation,
  NPC, spotanim, and item ID in the game. these are at
  `.refs/osrs-client-deob/runelite-api/src/main/java/net/runelite/api/gameval/`.
  attack anims and projectile GFX IDs come from here.
- **monsters.json / equipment.json**: wiki-sourced stat data from osrs-dps-calc at
  `.refs/osrs-dps-calc/cdn/json/`. used for combat stats, not visuals.

## adding a new NPC — full pipeline

### 1. stats (combat data)

add the NPC ID to `tools/monsters_manifest.json`:
```json
{"index": "MON_MY_NPC", "npc_id": 12345, "comment": "My NPC"}
```

regenerate:
```bash
python tools/generate_monsters.py
```

this produces `osrs_monsters_generated.h` with hp, def, att, max_hit, etc.

### 2. discover visual assets

find the gameval constant names for the NPC's animations and projectiles:
```bash
uv run python tools/discover_npc_assets.py --npc-id 12345
```

this outputs categorized animations (attack, death, idle, walk) and spotanims,
plus a suggested manifest visual section you can copy-paste.

if the NPC name in gameval is non-obvious (e.g. "SNAKEBOSS" for Zulrah), try:
```bash
uv run python tools/discover_npc_assets.py --search "zulrah"
```

this searches both gameval constant names and javadoc comments for in-game names.

### 3. add visual section to manifest

add the `visual` section to the manifest entry with the gameval names you picked:
```json
{
    "index": "MON_MY_NPC", "npc_id": 12345, "comment": "My NPC",
    "visual": {
        "group": "my_encounter",
        "attack_anims": ["MYNPC_ATTACK_MAGIC", "MYNPC_ATTACK_MELEE"],
        "extra_anims": ["MYNPC_DEATH", "MYNPC_SPAWN"],
        "spotanims": ["MYNPC_PROJECTILE", "MYNPC_IMPACT"]
    }
}
```

fields:
- `group`: encounter name. NPCs in the same group share .models + .anims binaries.
- `attack_anims`: gameval AnimationID names for attack animations. first is the default.
- `extra_anims`: other animations to export (death, spawn, defend, etc.).
  idle + walk come from the cache NPC config automatically — don't list them here.
- `spotanims`: gameval SpotanimID names for projectile/effect GFX.

### 4. export visual assets

```bash
cd ocean/osrs
uv run python tools/export_encounter_npcs.py \
    --group my_encounter \
    --modern-cache ../../../.refs/osrs-cache-modern \
    --output-dir data
```

this produces:
- `data/my_encounter.models` — MDL2 binary with all NPC + spotanim 3D meshes
- `data/my_encounter.anims` — ANIM binary with all animation sequences
- `data/npc_models_my_encounter.h` — C header with NPC model mappings + anim/GFX defines

### 5. include in your encounter

in your encounter header:
```c
#include "../data/npc_models_my_encounter.h"
```

the header provides:
- `NPC_MODEL_MAP_<GROUP>_GEN[]` — NPC ID to model/anim mapping array
- `<GROUP>_GEN_ANIM_*` — animation ID defines
- `<GROUP>_GEN_GFX_*_MODEL` / `<GROUP>_GEN_GFX_*_ANIM` — spotanim model/anim defines

### 6. build

```bash
cd ocean/osrs && make visual    # visual binary
cd ../../.. && python setup.py build_osrs_my_encounter --force  # training env
```

## manifest format reference

each entry in `tools/monsters_manifest.json`:

| field | required | description |
|---|---|---|
| `index` | yes | C enum name (e.g. MON_MY_NPC) |
| `npc_id` | yes | OSRS NPC ID |
| `version` | no | NPC version string for monsters.json lookup |
| `comment` | no | human-readable label |
| `manual_stats` | no | override stats when NPC not in monsters.json |
| `visual` | no | visual asset section (see below) |

visual section fields:

| field | description |
|---|---|
| `group` | encounter group name for output files |
| `attack_anims` | list of gameval AnimationID constant names |
| `extra_anims` | list of gameval AnimationID names (death, spawn, etc.) |
| `spotanims` | list of gameval SpotanimID constant names |

## gameval files

location: `.refs/osrs-client-deob/runelite-api/src/main/java/net/runelite/api/gameval/`

| file | what it contains |
|---|---|
| `NpcID.java` | NPC ID constants (e.g. SNAKEBOSS_BOSS_RANGED = 2042) |
| `AnimationID.java` | animation sequence IDs (e.g. SNAKEBOSS_ATTACK_ACIDX3 = 5068) |
| `SpotanimID.java` | GFX/spotanim IDs (e.g. SNAKEBOSS_ORB = 1044) |
| `ItemID.java` | item IDs |
| `ObjectID.java` | game object IDs |

naming conventions vary by content area:
- inferno: `INFERNO_*`, `JAL*`, `JALTOKJAD_*`, `ZUK_*`
- zulrah: `SNAKEBOSS_*`
- fight caves: `FIGHT_CAVE_*`, `TZHAAR_*`

use `discover_npc_assets.py --search <name>` to find the right prefix.

## output files

| file | format | description |
|---|---|---|
| `data/<group>.models` | MDL2 binary | 3D meshes for all NPCs + spotanims in group |
| `data/<group>.anims` | ANIM binary | animation sequences referenced by the group |
| `data/npc_models_<group>.h` | C header | NPC model map + anim/GFX defines |
| `osrs_monsters_generated.h` | C header | NPC combat stats (from generate_monsters.py) |
| `osrs_items_generated.h` | C header | item stats (from generate_items.py) |

the renderer loads .models + .anims at startup and uses the C header mappings
to look up which model/anim to use for a given NPC ID.

## script reference

| script | what it does |
|---|---|
| `tools/generate_monsters.py` | monsters.json → osrs_monsters_generated.h (stats) |
| `tools/generate_items.py` | equipment.json → osrs_items_generated.h (stats) |
| `tools/export_encounter_npcs.py` | manifest + gameval + cache → .models + .anims + header |
| `tools/discover_npc_assets.py` | gameval lookup helper for manifest authors |
| `tools/gameval_parser.py` | shared module: parse gameval Java constants |
| `scripts/export_inferno_npcs.py` | inferno-specific export (legacy, predates manifest system) |
| `scripts/export_models.py` | equipment model export (player body + worn gear) |
| `scripts/export_animations.py` | shared animation export library |
| `scripts/export_sprites.py` | GUI sprite export (hit splats, prayer icons) |
| `scripts/export_collision_map.py` | collision data export |
| `scripts/export_terrain.py` | terrain geometry export |
| `scripts/export_spotanims.py` | spotanim query tool (cache lookup, no auto-output) |
