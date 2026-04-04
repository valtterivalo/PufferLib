"""Discover gameval visual asset constants for OSRS NPCs.

Standalone helper for manifest authors. Scans RuneLite gameval Java constants
(AnimationID, SpotanimID) to find all animations and spotanims associated with
an NPC, categorizes them by purpose, and prints a suggested manifest visual
section.

Usage:
    cd ocean/osrs
    uv run python tools/discover_npc_assets.py --npc-id 2042
    uv run python tools/discover_npc_assets.py --npc-ids 2042,2043,2044
    uv run python tools/discover_npc_assets.py --search zulrah
"""

import argparse
import json
import re
import sys
from collections import defaultdict
from pathlib import Path

from gameval_parser import DEFAULT_GAMEVAL_DIR, load_gameval, reverse_lookup

# regex to extract javadoc comment + constant pairs from gameval Java files
# matches: /** In-game Name */\n\tpublic static final int CONST_NAME = 123;
_COMMENTED_CONST_PATTERN = re.compile(
    r"/\*\*\s*\n\s*\*\s*(.+?)\s*\n\s*\*/\s*\n\s*public\s+static\s+final\s+int\s+(\w+)\s*=\s*(\d+)\s*;",
)

# -- animation suffix categorization --

# suffixes that indicate attack animations
_ATTACK_SUFFIXES = ("_ATTACK", "_ACIDX")

# suffixes auto-loaded from cache (idle/walk) -- no need in manifest
_AUTO_SUFFIXES = ("_IDLE", "_READY", "_WALK", "_RUN")

# suffixes to skip entirely (pets, cosmetics, unrelated)
_SKIP_SUFFIXES = (
    "_PET_",
    "_CHATHEAD",
    "_FLETCHING",
    "_ORNAMENT",
    "_SLICEEEL",
)

# known NPC type suffixes to strip when extracting prefix
_NPC_TYPE_SUFFIXES = (
    "_BOSS_RANGED",
    "_BOSS_MELEE",
    "_BOSS_MAGIC",
    "_BOSS_RANGE",
    "_BOSS_MAGE",
    "_BOSS_",
    "_CREATURE_",
    "_MASTER_",
    "_MASTER",
    "_MINION_MELEE",
    "_MINION_MAGIC",
    "_MINION_RANGE",
    "_MINION_DYING",
    "_MINION_",
    "_HIGHPRIEST",
    "_PRIEST_",
    "_PRIEST",
    "_OGRE_",
    "_GNOME_",
    "_GNOME_VICTIM",
    "_FISHERMAN",
    "_TYRASGUARD_",
    "_SPECTATOR",
    "_FISHINGSPOT",
    "_FISHINGSPOT_FAKE",
)


def _extract_prefix(npc_name: str, anim_ids: dict[str, int]) -> str:
    """Extract the naming prefix from a gameval NPC constant name.

    Strategy:
    1. Try stripping known NPC type suffixes.
    2. Fallback: try progressively shorter underscore-delimited prefixes
       until one matches >3 animation constants.
    """
    # strategy 1: strip known type suffixes
    for suffix in _NPC_TYPE_SUFFIXES:
        if npc_name.endswith(suffix) or suffix in npc_name:
            candidate = npc_name.split(suffix)[0]
            if candidate and _count_anim_matches(candidate, anim_ids) > 3:
                return candidate

    # strategy 2: progressively shorter prefixes
    parts = npc_name.split("_")
    for length in range(len(parts) - 1, 0, -1):
        candidate = "_".join(parts[:length])
        if _count_anim_matches(candidate, anim_ids) > 3:
            return candidate

    # last resort: use the full name
    return npc_name


def _count_anim_matches(prefix: str, anim_ids: dict[str, int]) -> int:
    """Count how many animation constants start with prefix_."""
    prefix_with_sep = prefix + "_"
    return sum(1 for name in anim_ids if name.startswith(prefix_with_sep))


def _categorize_suffix(name: str, prefix: str) -> str:
    """Categorize an animation constant by its suffix relative to the prefix."""
    suffix = name[len(prefix):]

    for s in _SKIP_SUFFIXES:
        if s in suffix:
            return "skip"

    for s in _ATTACK_SUFFIXES:
        if s in suffix:
            return "attack"

    for s in _AUTO_SUFFIXES:
        if suffix.startswith(s) or suffix == s:
            return "auto"

    if suffix == "_DEATH":
        return "death"

    return "extra"


def _discover_npc(
    npc_id: int,
    anim_ids: dict[str, int],
    npc_ids: dict[str, int],
    spotanim_ids: dict[str, int],
) -> None:
    """Discover and print visual asset info for one NPC ID."""
    npc_name = reverse_lookup(npc_ids, npc_id)
    if npc_name is None:
        print(f"  NPC ID {npc_id}: not found in gameval NpcID constants")
        return

    prefix = _extract_prefix(npc_name, anim_ids)

    print(f"\n  NPC {npc_id}: {npc_name}")
    print(f"  prefix: {prefix}")

    # scan animations
    prefix_with_sep = prefix + "_"
    categorized: dict[str, list[tuple[str, int]]] = defaultdict(list)
    for name, value in sorted(anim_ids.items()):
        if name.startswith(prefix_with_sep):
            category = _categorize_suffix(name, prefix)
            categorized[category].append((name, value))

    # print categorized animations
    category_order = ["attack", "death", "extra", "auto", "skip"]
    for category in category_order:
        entries = categorized.get(category, [])
        if not entries:
            continue
        label = {
            "attack": "ATTACK anims",
            "death": "DEATH anims",
            "extra": "EXTRA anims (spawn, defend, etc.)",
            "auto": "AUTO anims (idle/walk, from cache)",
            "skip": "SKIP (pet, cosmetic, unrelated)",
        }[category]
        print(f"\n    {label}:")
        for name, value in entries:
            print(f"      {name} = {value}")

    # scan spotanims
    spotanims = [
        (name, value)
        for name, value in sorted(spotanim_ids.items())
        if name.startswith(prefix_with_sep)
    ]
    if spotanims:
        print(f"\n    SPOTANIMS:")
        for name, value in spotanims:
            print(f"      {name} = {value}")

    # build suggested manifest visual section
    attack_anim_names = [name for name, _ in categorized.get("attack", [])]
    death_anim_names = [name for name, _ in categorized.get("death", [])]
    extra_anim_names = [name for name, _ in categorized.get("extra", [])]
    spotanim_names = [name for name, _ in spotanims]

    suggested = {
        "group": "REPLACE_ME",
        "attack_anims": attack_anim_names,
        "extra_anims": death_anim_names + extra_anim_names,
        "spotanims": spotanim_names,
    }

    print(f"\n    suggested manifest visual section:")
    print(f"    {json.dumps(suggested, indent=4)}")


def _load_npc_ingame_names(
    gameval_dir: Path = DEFAULT_GAMEVAL_DIR,
) -> dict[str, str]:
    """Parse NpcID.java to extract in-game names from javadoc comments.

    Returns {GAMEVAL_CONST_NAME: "In-Game Name"} for constants that have comments.
    """
    npc_java = (Path(gameval_dir) / "NpcID.java").read_text()
    return {
        const_name: ingame_name
        for ingame_name, const_name, _ in _COMMENTED_CONST_PATTERN.findall(npc_java)
    }


def _search_npcs(
    query: str, npc_ids: dict[str, int], ingame_names: dict[str, str]
) -> list[tuple[str, int, str]]:
    """Search NPC names case-insensitively, matching both gameval and in-game names."""
    query_upper = query.upper()
    results = []
    for name, value in sorted(npc_ids.items(), key=lambda x: x[1]):
        ingame = ingame_names.get(name, "")
        if query_upper in name or query_upper in ingame.upper():
            results.append((name, value, ingame))
    return results


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Discover gameval visual asset constants for OSRS NPCs."
    )
    group = parser.add_mutually_exclusive_group(required=True)
    group.add_argument("--npc-id", type=int, help="single NPC ID to discover")
    group.add_argument(
        "--npc-ids", type=str, help="comma-separated NPC IDs (e.g. 2042,2043,2044)"
    )
    group.add_argument(
        "--search", type=str, help="search NPC names case-insensitively"
    )

    args = parser.parse_args()
    anim_ids, npc_ids, spotanim_ids = load_gameval()

    if args.search is not None:
        ingame_names = _load_npc_ingame_names()
        matches = _search_npcs(args.search, npc_ids, ingame_names)
        if not matches:
            print(f"no NPCs matching '{args.search}'")
            sys.exit(1)
        print(f"NPCs matching '{args.search}' ({len(matches)} results):\n")
        for name, value, ingame in matches:
            label = f" ({ingame})" if ingame else ""
            print(f"  {value:>6}  {name}{label}")
        print(
            f"\nuse --npc-id or --npc-ids to discover visual assets for specific NPCs"
        )
        return

    if args.npc_id is not None:
        npc_id_list = [args.npc_id]
    else:
        npc_id_list = [int(x.strip()) for x in args.npc_ids.split(",")]

    print(f"discovering visual assets for {len(npc_id_list)} NPC(s)...")
    for npc_id in npc_id_list:
        _discover_npc(npc_id, anim_ids, npc_ids, spotanim_ids)


if __name__ == "__main__":
    main()
