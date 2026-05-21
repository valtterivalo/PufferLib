"""Parse RuneLite gameval Java constant files into Python dicts.

RuneLite's gameval directory contains auto-generated Java files (AnimationID.java,
NpcID.java, SpotanimID.java, etc.) that define integer constants for OSRS game
entities. Each file has lines like:

    public static final int SNAKEBOSS_ATTACK_ACIDX3 = 5068;

This module extracts those constants into {name: int} dicts for use in Python
tools that need to reference OSRS IDs by their gameval names.
"""

import re
from pathlib import Path

_CONST_PATTERN = re.compile(
    r"public\s+static\s+final\s+int\s+(\w+)\s*=\s*(\d+)\s*;"
)

def _default_gameval_dir() -> Path:
    root = Path(__file__).resolve().parents[3]
    suffix = Path(
        "osrs-client-deob/runelite-api/src/main/java/net/runelite/api/gameval",
    )
    refs_path = root / "refs" / suffix
    if refs_path.exists():
        return refs_path
    return root / ".refs" / suffix


DEFAULT_GAMEVAL_DIR = _default_gameval_dir()


def parse_gameval_file(path: Path) -> dict[str, int]:
    """Parse a single gameval Java file into a {CONSTANT_NAME: int_value} dict."""
    text = Path(path).read_text()
    return {name: int(value) for name, value in _CONST_PATTERN.findall(text)}


def load_gameval(
    gameval_dir: Path = DEFAULT_GAMEVAL_DIR,
) -> tuple[dict[str, int], dict[str, int], dict[str, int]]:
    """Load AnimationID, NpcID, and SpotanimID constants.

    Returns:
        (anim_ids, npc_ids, spotanim_ids) -- three dicts mapping constant names
        to their integer values.
    """
    gameval_dir = Path(gameval_dir)
    anim_ids = parse_gameval_file(gameval_dir / "AnimationID.java")
    npc_ids = parse_gameval_file(gameval_dir / "NpcID.java")
    spotanim_ids = parse_gameval_file(gameval_dir / "SpotanimID.java")
    return anim_ids, npc_ids, spotanim_ids


def resolve_names(
    names: list[str], lookup: dict[str, int], context: str = ""
) -> list[int]:
    """Resolve a list of gameval constant names to their integer IDs.

    Raises:
        KeyError: if any name is not found in the lookup dict.
    """
    result = []
    for name in names:
        if name not in lookup:
            prefix = f"[{context}] " if context else ""
            raise KeyError(f"{prefix}gameval constant not found: {name!r}")
        result.append(lookup[name])
    return result


def reverse_lookup(lookup: dict[str, int], value: int) -> str | None:
    """Find the gameval constant name for a given integer ID.

    Returns None if no match is found.
    """
    for name, v in lookup.items():
        if v == value:
            return name
    return None
