# Unified Visual Export Pipeline Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build a manifest-driven visual export pipeline that reads gameval constants + OSRS cache to produce NPC models, animations, and spotanim GFX binaries. Test on zulrah NPCs.

**Architecture:** Extend monsters_manifest.json with visual sections containing gameval animation/spotanim names. New Python script parses gameval Java files for name-to-ID mapping, reads cache for 3D models + animation sequences, outputs .models + .anims binaries + updated npc_models.h. Discovery helper tool assists manifest authors.

**Tech Stack:** Python 3.12+ (uv), existing cache reader infrastructure (modern_cache_reader.py, export_models.py, export_animations.py). C header generation.

**Spec:** docs/superpowers/specs/2026-04-04-unified-visual-export-design.md

---

## Task 1: Gameval parser module

Create a reusable Python module that parses RuneLite gameval Java constant files into Python dicts.

**Files:**
- Create: ocean/osrs/tools/gameval_parser.py

- [ ] **Step 1: Create the parser module**

The module provides:
- parse_gameval_file(path) -- parse one Java file into {CONSTANT_NAME: int_value}
- load_gameval(gameval_dir) -- load all 3 files (AnimationID, NpcID, SpotanimID)
- resolve_names(names, lookup, context) -- resolve list of gameval names to IDs, raise KeyError if missing
- reverse_lookup(lookup, value) -- find name for a given ID

The regex to match is: public static final int NAME = VALUE;

Default gameval_dir: .refs/osrs-client-deob/runelite-api/src/main/java/net/runelite/api/gameval

Read the spec for full context.

- [ ] **Step 2: Verify it parses correctly**

Run from ocean/osrs:
    uv run python -c "from tools.gameval_parser import load_gameval; a,n,s = load_gameval(); print(f'anims:{len(a)} npcs:{len(n)} spots:{len(s)}'); print(a['SNAKEBOSS_ATTACK_ACIDX3'], n['SNAKEBOSS_BOSS_RANGED'], s['SNAKEBOSS_ORB'])"

Expected: prints counts + correct values (5068, 2042, 1044).

- [ ] **Step 3: Commit**

    git add ocean/osrs/tools/gameval_parser.py
    git commit -m "add gameval_parser.py: parse RuneLite gameval Java constants"

---

## Task 2: Discovery helper tool

Create the tool that helps manifest authors find gameval constants for an NPC.

**Files:**
- Create: ocean/osrs/tools/discover_npc_assets.py

- [ ] **Step 1: Create the discovery tool**

The tool:
- takes --npc-id, --npc-ids (comma-separated), or --search (name search)
- uses gameval_parser.load_gameval() to get all constants
- for a given NPC ID: finds its gameval name via reverse_lookup on NpcID dict
- extracts the naming prefix (e.g. SNAKEBOSS_BOSS_RANGED -> SNAKEBOSS)
- scans AnimationID dict for all SNAKEBOSS_* constants, categorizes by suffix:
  _ATTACK* -> attack, _IDLE/_READY -> idle, _WALK* -> walk, _DEATH -> death, _PET_* -> skip
- scans SpotanimID dict for all SNAKEBOSS_* constants
- prints categorized list + suggested manifest visual section as JSON

Read the spec for the full output format.

- [ ] **Step 2: Test the discovery tool**

    cd ocean/osrs
    uv run python tools/discover_npc_assets.py --npc-ids 2042,2043,2044
    uv run python tools/discover_npc_assets.py --search zulrah

Expected: prints NPC names, categorized animations, spotanims, suggested manifest snippets.

- [ ] **Step 3: Commit**

    git add ocean/osrs/tools/discover_npc_assets.py
    git commit -m "add discover_npc_assets.py: gameval-based NPC visual asset discovery tool"

---

## Task 3: Update monster manifest with zulrah visual sections

Add visual sections to the 5 zulrah NPC entries in the manifest.

**Files:**
- Modify: ocean/osrs/tools/monsters_manifest.json

- [ ] **Step 1: Add visual sections to zulrah manifest entries**

Update the 5 zulrah entries. Gameval constant names from the discovery tool output:

MON_ZULRAH_GREEN (2042): group=zulrah, attack_anims=[SNAKEBOSS_ATTACK_ACIDX3, SNAKEBOSS_ATTACK_ACIDX1], extra_anims=[SNAKEBOSS_SPAWN, SNAKEBOSS_SINKFAST, SNAKEBOSS_EMERGEFAST, SNAKEBOSS_DEATH, SNAKEBOSS_DEFEND], spotanims=[SNAKEBOSS_ORB, SNAKEBOSS_DOUBLE_ORB, SNAKEBOSS_FIREBALL, SNAKEBOSS_EGG, SNAKEBOSS_MINION_SPELL]

MON_ZULRAH_RED (2043): group=zulrah, attack_anims=[SNAKEBOSS_ATTACK_TAIL_LEFT, SNAKEBOSS_ATTACK_TAIL_RIGHT], extra_anims=[SNAKEBOSS_DEATH, SNAKEBOSS_DEFEND], spotanims=[]

MON_ZULRAH_BLUE (2044): group=zulrah, attack_anims=[SNAKEBOSS_ATTACK_ACIDX3], extra_anims=[SNAKEBOSS_DEATH, SNAKEBOSS_DEFEND], spotanims=[SNAKEBOSS_ORB, SNAKEBOSS_FIREBALL]

MON_ZULRAH_SNAKELING_MELEE (2045): group=zulrah, attack_anims=[], extra_anims=[], spotanims=[]

MON_ZULRAH_SNAKELING_MAGIC (2046): group=zulrah, attack_anims=[], extra_anims=[], spotanims=[SNAKEBOSS_MINION_SPELL]

- [ ] **Step 2: Verify generate_monsters.py still works (ignores visual section)**

    python ocean/osrs/tools/generate_monsters.py

Expected: manifest: 19 monsters, no errors.

- [ ] **Step 3: Commit**

    git add ocean/osrs/tools/monsters_manifest.json
    git commit -m "add visual sections to zulrah manifest entries (gameval anim + spotanim names)"

---

## Task 4: Build the unified visual export script

The main export script. Reads manifest visual sections, resolves gameval names, reads cache, produces .models + .anims + npc_models header.

**Files:**
- Create: ocean/osrs/tools/export_encounter_npcs.py

- [ ] **Step 1: Create the export script**

The script follows the same patterns as scripts/export_inferno_npcs.py (READ IT THOROUGHLY before implementing). Key differences:
- NPC IDs come from manifest (visual.group filter) instead of hardcoded INFERNO_NPC_IDS
- Attack anims come from gameval_parser.resolve_names() instead of INFERNO_ATTACK_ANIMS dict
- Spotanim GFX IDs come from gameval_parser.resolve_names() instead of INFERNO_SPOTANIM_IDS

Imports from existing infrastructure (these are in ocean/osrs/scripts/):
- modern_cache_reader.py: ModernCacheReader, read_big_smart, read_u8, read_u16, etc.
- export_models.py: decode_model, expand_model, write_models_binary, ModelData, _merge_models
- export_animations.py: write_animations_binary, FrameBaseDef, FrameDef, SequenceDef, load_modern_framebases, _parse_normal_frame
- modern_cache_reader.py: parse_sequence as parse_modern_sequence

Also imports gameval_parser from tools/.

Reuse NpcDef and SpotAnimDef dataclasses and their parse functions from export_inferno_npcs.py (copy them, or refactor into a shared module if cleaner).

Output files per group:
- data/<group>.models -- MDL2 binary
- data/<group>.anims -- ANIM binary
- data/npc_models_<group>.h -- C header with NPC_MODEL_MAP_<GROUP> + GFX defines

The C header is a standalone includeable file (not modifying the main npc_models.h). The main npc_models.h includes it. This avoids any risk to inferno data.

Usage:
    cd ocean/osrs
    uv run python tools/export_encounter_npcs.py --group zulrah --modern-cache ../../../.refs/osrs-cache-modern --output-dir data

- [ ] **Step 2: Test on zulrah group**

Run the export and verify:
- data/zulrah.models exists (should be >10KB)
- data/zulrah.anims exists (should be >5KB)
- data/npc_models_zulrah.h exists and has NPC_MODEL_MAP_ZULRAH entries

- [ ] **Step 3: Verify the C header compiles**

    echo '#include "ocean/osrs/data/npc_models_zulrah.h"' > /tmp/th.c
    echo 'int main(void){return 0;}' >> /tmp/th.c
    cc -std=c11 -I. -o /tmp/th /tmp/th.c

- [ ] **Step 4: Verify existing builds not broken**

    python setup.py build_osrs_inferno --force 2>&1 | tail -1
    python setup.py build_osrs_zulrah --force 2>&1 | tail -1
    python setup.py build_osrs_pvp --force 2>&1 | tail -1

- [ ] **Step 5: Commit**

    git add ocean/osrs/tools/export_encounter_npcs.py ocean/osrs/data/zulrah.models ocean/osrs/data/zulrah.anims ocean/osrs/data/npc_models_zulrah.h
    git commit -m "add export_encounter_npcs.py: manifest-driven visual export, test on zulrah"

---

## Task 5: Documentation

**Files:**
- Create: ocean/osrs/tools/README.md

- [ ] **Step 1: Write the pipeline documentation**

Cover:
1. How OSRS assets work (cache models + idle/walk, gameval attack/death/GFX, server-driven)
2. Step-by-step: adding a new NPC visually (discover -> manifest -> export -> build)
3. Manifest format reference with examples (stats fields + visual fields)
4. Gameval files: what they are, where they live, naming conventions
5. Output files: .models, .anims, npc_models_*.h, how renderer loads them
6. Existing scripts reference (export_inferno_npcs.py, export_models.py, etc.)

- [ ] **Step 2: Commit**

    git add ocean/osrs/tools/README.md
    git commit -m "add tools README: visual asset pipeline documentation"

---

## Task 6: Verification

- [ ] **Step 1: Run all existing tests (no regressions)**

All 8 test suites: combat_math, item_effects, special_attacks, player_combat, consumables, bolt_procs, damage, inventory.

- [ ] **Step 2: Verify all 3 env builds pass**

    python setup.py build_osrs_inferno --force
    python setup.py build_osrs_zulrah --force
    python setup.py build_osrs_pvp --force

- [ ] **Step 3: Verify zulrah assets are valid**

    ls -la ocean/osrs/data/zulrah.models ocean/osrs/data/zulrah.anims

- [ ] **Step 4: Verify inferno assets are untouched**

    git diff ocean/osrs/data/inferno_npcs.models
    git diff ocean/osrs/data/inferno_npcs.anims
    git diff ocean/osrs/data/npc_models.h

Expected: no changes to inferno files.
