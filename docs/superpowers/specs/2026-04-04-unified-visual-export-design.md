# unified visual export pipeline

date: 2026-04-04
status: approved
branch: inferno-encounter
test target: zulrah NPCs (2042-2046)

## problem

adding a new NPC to an encounter requires manually looking up attack animation IDs
from wiki/RuneLite, manually querying spotanim GFX from the cache, manually populating
SPOTANIM_TABLE in effects.h, and manually adding GFX defines in npc_models.h. the data
exists in structured form in the RuneLite deob client's gameval Java constants, but our
export pipeline doesn't read it.

## solution

extend the monster manifest with a `visual` section that lists gameval animation and
spotanim names. a new export script reads the manifest, resolves gameval names to
integer IDs, reads models and animations from the cache, and produces binary asset
files + a C header. a separate discovery helper tool makes it easy to find the right
gameval names for any NPC.

## manifest extension

each NPC entry in `tools/monsters_manifest.json` gets an optional `visual` section:

```json
{
    "index": "MON_ZULRAH_GREEN",
    "npc_id": 2042,
    "version": "Serpentine",
    "comment": "Zulrah green/ranged form",
    "visual": {
        "group": "zulrah",
        "attack_anims": ["SNAKEBOSS_ATTACK_ACIDX3", "SNAKEBOSS_ATTACK_ACIDX1"],
        "extra_anims": ["SNAKEBOSS_SPAWN", "SNAKEBOSS_SINKFAST", "SNAKEBOSS_EMERGEFAST",
                        "SNAKEBOSS_DEATH", "SNAKEBOSS_DEFEND"],
        "spotanims": ["SNAKEBOSS_ORB", "SNAKEBOSS_DOUBLE_ORB", "SNAKEBOSS_FIREBALL",
                      "SNAKEBOSS_EGG", "SNAKEBOSS_MINION_SPELL"]
    }
}
```

fields:
- `group`: encounter name for output file grouping (zulrah.models, zulrah.anims)
- `attack_anims`: gameval AnimationID constant names. first is the default attack anim
  used in npc_models.h NPC_MODEL_MAP entry.
- `extra_anims`: non-combat animations to export (death, spawn, dive, defend, etc.)
- `spotanims`: gameval SpotanimID constant names for projectiles/effects.

NPCs without `visual` are stats-only (generate_monsters.py handles them).
idle + walk anims come from the cache NPC config automatically — not in the manifest.

## new file: tools/export_encounter_npcs.py

the main export script. replaces per-encounter scripts for new content.

### inputs

- `tools/monsters_manifest.json` — NPC IDs + visual sections
- `.refs/osrs-client-deob/runelite-api/.../gameval/AnimationID.java`
- `.refs/osrs-client-deob/runelite-api/.../gameval/SpotanimID.java`
- `.refs/osrs-cache-modern/` — 3D models, NPC configs (idle/walk anims), spotanim configs

### outputs (per group)

- `data/<group>.models` — MDL2 binary with NPC meshes + spotanim models
- `data/<group>.anims` — ANIM binary with all referenced animation sequences
- `data/npc_models.h` — regenerated C header (preserves existing inferno entries,
  adds new group entries)

### pipeline steps

1. parse gameval Java files → build `{constant_name: int}` dicts for AnimationID + SpotanimID
2. load manifest, filter entries with `visual` section, group by `group` field
3. for each NPC in group:
   a. read cache NPC config (index 2, group 9) → model IDs + idle_anim + walk_anim + recolors
   b. resolve attack_anims gameval names → anim IDs via gameval dict
   c. resolve extra_anims gameval names → anim IDs
   d. resolve spotanims gameval names → GFX IDs → read cache spotanim config (index 2, group 13)
      → get model_id + anim_seq_id per GFX
4. collect all referenced model IDs → load from cache → decode MDL2 → apply recolors
5. collect all referenced animation sequence IDs → load from cache → decode
6. write `data/<group>.models` (MDL2 binary) + `data/<group>.anims` (ANIM binary)
7. regenerate `data/npc_models.h`:
   - preserve existing NPC_MODEL_MAP_INFERNO array (from export_inferno_npcs.py)
   - add NPC_MODEL_MAP_<GROUP> arrays for each new group
   - add GFX model/anim defines per spotanim

### reuse from existing scripts

the new script imports from the existing export infrastructure:
- `export_models.py` — MDL2 decode, model merge, binary write
- `export_animations.py` — ANIM decode, sequence parse, binary write
- `modern_cache_reader.py` — cache file reading

### usage

```bash
# export zulrah visual assets
uv run python tools/export_encounter_npcs.py --group zulrah

# export all groups that have visual sections
uv run python tools/export_encounter_npcs.py --all
```

## new file: tools/discover_npc_assets.py

standalone helper for manifest authors. NOT part of the build pipeline.

### usage

```bash
# discover all visual assets for NPC 2042
uv run python tools/discover_npc_assets.py --npc-id 2042

# discover for multiple NPCs
uv run python tools/discover_npc_assets.py --npc-ids 2042,2043,2044,2045,2046

# search by name
uv run python tools/discover_npc_assets.py --search "zulrah"
```

### what it does

1. parses gameval NpcID.java → finds NPC ID → gameval constant name
2. extracts prefix from constant name (e.g., SNAKEBOSS_BOSS_RANGED → SNAKEBOSS)
3. scans AnimationID.java for all constants with that prefix
4. categorizes by suffix: _ATTACK_* → attack, _IDLE/_READY → idle, _WALK* → walk,
   _DEATH → death, _SPAWN → spawn, _DEFEND → defend, _PET_* → skip
5. scans SpotanimID.java for all constants with that prefix
6. prints categorized list + suggested manifest visual section

no cache access, no binary output, fast. purely informational.

## new file: tools/README.md

documentation for the full asset pipeline. covers:
1. how OSRS assets work (cache models + idle/walk, gameval attack/death/GFX, server-driven)
2. step-by-step: how to add a new NPC visually
3. manifest format reference with examples
4. output file descriptions and how the renderer loads them
5. gameval files: what they are, where they live, naming conventions
6. existing scripts: what each one does, when to use it

## zulrah test manifest entries

```json
{"index": "MON_ZULRAH_GREEN", "npc_id": 2042, "version": "Serpentine",
 "comment": "Zulrah green/ranged form",
 "visual": {
     "group": "zulrah",
     "attack_anims": ["SNAKEBOSS_ATTACK_ACIDX3", "SNAKEBOSS_ATTACK_ACIDX1"],
     "extra_anims": ["SNAKEBOSS_SPAWN", "SNAKEBOSS_SINKFAST", "SNAKEBOSS_EMERGEFAST",
                     "SNAKEBOSS_DEATH", "SNAKEBOSS_DEFEND"],
     "spotanims": ["SNAKEBOSS_ORB", "SNAKEBOSS_DOUBLE_ORB", "SNAKEBOSS_FIREBALL",
                   "SNAKEBOSS_EGG", "SNAKEBOSS_MINION_SPELL"]
 }},
{"index": "MON_ZULRAH_RED", "npc_id": 2043, "version": "Magma",
 "comment": "Zulrah red/melee form",
 "visual": {
     "group": "zulrah",
     "attack_anims": ["SNAKEBOSS_ATTACK_TAIL_LEFT", "SNAKEBOSS_ATTACK_TAIL_RIGHT"],
     "extra_anims": ["SNAKEBOSS_DEATH", "SNAKEBOSS_DEFEND"],
     "spotanims": []
 }},
{"index": "MON_ZULRAH_BLUE", "npc_id": 2044, "version": "Tanzanite",
 "comment": "Zulrah blue/magic form",
 "visual": {
     "group": "zulrah",
     "attack_anims": ["SNAKEBOSS_ATTACK_ACIDX3"],
     "extra_anims": ["SNAKEBOSS_DEATH", "SNAKEBOSS_DEFEND"],
     "spotanims": ["SNAKEBOSS_ORB", "SNAKEBOSS_FIREBALL"]
 }},
{"index": "MON_ZULRAH_SNAKELING_MELEE", "npc_id": 2045, "version": "Melee",
 "comment": "Snakeling melee variant",
 "visual": {
     "group": "zulrah",
     "attack_anims": [],
     "extra_anims": [],
     "spotanims": []
 }},
{"index": "MON_ZULRAH_SNAKELING_MAGIC", "npc_id": 2046, "version": "Magic",
 "comment": "Snakeling magic variant",
 "visual": {
     "group": "zulrah",
     "attack_anims": [],
     "extra_anims": [],
     "spotanims": ["SNAKEBOSS_MINION_SPELL"]
 }}
```

note: all 3 zulrah forms share the same base 3D model with different recolors applied
from the cache NPC config. the export script handles recoloring during model decode
(same as existing export_inferno_npcs.py).

## backward compatibility

- `scripts/export_inferno_npcs.py` stays as-is. inferno assets untouched.
- `data/npc_models.h` regeneration preserves NPC_MODEL_MAP_INFERNO + all INF_GFX_* defines.
- new zulrah entries are added as NPC_MODEL_MAP_ZULRAH + ZUL_GFX_* defines.
- inferno NPCs can migrate to the manifest-driven pipeline later.

## verification

```bash
# export zulrah assets
uv run python tools/export_encounter_npcs.py --group zulrah

# verify outputs exist
ls -la data/zulrah.models data/zulrah.anims

# verify npc_models.h has zulrah entries
grep NPC_MODEL_MAP_ZULRAH data/npc_models.h

# build visual binary
cd ocean/osrs && make visual

# build training env (should not break)
cd ../../.. && python setup.py build_osrs_zulrah --force
python setup.py build_osrs_inferno --force
```
