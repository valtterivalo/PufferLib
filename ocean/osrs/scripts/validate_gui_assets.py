#!/usr/bin/env python3
"""Validate required OSRS GUI sprite assets."""

from __future__ import annotations

import argparse
from pathlib import Path
import sys

PNG_SIGNATURE = b"\x89PNG\r\n\x1a\n"

PRAYER_IDS = (
    115, 116, 117, 118, 119, 120, 121, 122, 123, 124, 125, 126,
    127, 128, 129, 130, 131, 132, 133, 134,
    135, 136, 137, 138, 139, 140, 141, 142, 143, 144, 145, 146,
    147, 148, 149, 150, 151, 152, 153, 154,
    502, 503, 504, 505, 506, 507, 508, 509,
    945, 946, 947, 949, 950, 951,
    1420, 1421, 1424, 1425,
)

ANCIENT_COMBAT_SPELL_IDS = tuple(range(325, 341)) + tuple(range(375, 391))
ANCIENT_TELEPORT_SPELL_IDS = tuple(range(341, 349)) + tuple(range(391, 399))

CORE_NAMED_ASSETS = (
    "tradebacking_dark",
    "osrs_stretch_side_topbottom_0",
    "osrs_stretch_side_topbottom_1",
    "osrs_stretch_side_columns_0",
    "osrs_stretch_side_columns_1",
    "osrs_stretch_mapsurround",
    "resize_map_mask",
    "resize_compass_mask",
    "side_icon_combat",
    "side_icon_stats",
    "side_icon_quests",
    "side_icon_inventory",
    "side_icon_equipment",
    "side_icon_prayer",
    "side_icon_magic",
    "side_icon_magic_ancient",
    "combatboxes_0",
    "combatboxes_1",
    "combatboxes_2",
    "combatboxes_3",
    "wornicons_0",
    "wornicons_1",
    "wornicons_2",
    "wornicons_3",
    "wornicons_4",
    "wornicons_5",
    "wornicons_6",
    "wornicons_7",
    "wornicons_8",
    "wornicons_9",
    "wornicons_10",
    "wornicons_11",
    "skill_icon_0",
    "skill_icon_1",
    "skill_icon_2",
    "skill_icon_3",
    "skill_icon_4",
    "skill_icon_5",
    "skill_icon_6",
    "skill_icon_7",
    "skill_icon_8",
    "skill_icon_9",
    "skill_icon_10",
    "skill_icon_11",
    "skill_icon_12",
    "skill_icon_13",
    "skill_icon_14",
    "skill_icon_15",
    "skill_icon_16",
    "skill_icon_17",
    "skill_icon_18",
    "skill_icon_19",
    "skill_icon_20",
    "skill_icon_21",
    "skill_icon_22",
    "skill_icon_23",
    "prayeron_24",
    "prayeroff_24",
    "magicon_47",
    "magicoff_47",
    "standard_spell_on_79",
    "compass",
    "minimap_alpha_mask",
    "minimap_and_compass_frame",
    "rm_minimap_alpha_mask",
    "rm_minimap_and_compass_frame",
    "rm_compass_alpha_mask",
    "orb_empty",
    "orb_hp",
    "orb_prayer",
    "orb_run",
    "orb_run_active",
    "orb_icon_hp",
    "orb_icon_prayer",
    "orb_icon_walk",
    "orb_icon_run",
    "minimap_dot_player",
    "minimap_dot_npc",
    "minimap_dot_friend",
    "minimap_dot_item",
)

TRANSPARENT_ASSETS = (
    "resize_map_mask",
    "resize_compass_mask",
    "minimap_alpha_mask",
    "rm_minimap_alpha_mask",
    "rm_compass_alpha_mask",
)


def is_png(path: Path) -> bool:
    try:
        with path.open("rb") as handle:
            return handle.read(len(PNG_SIGNATURE)) == PNG_SIGNATURE
    except OSError:
        return False


def transparent_pixel_count(path: Path) -> int:
    from PIL import Image

    with Image.open(path) as image:
        rgba = image.convert("RGBA")
        return sum(1 for pixel in rgba.getdata() if pixel[3] == 0)


def required_asset_names(include_full_ancient_spellbook: bool) -> list[str]:
    names = [str(sprite_id) for sprite_id in PRAYER_IDS]
    names.extend(str(sprite_id) for sprite_id in ANCIENT_COMBAT_SPELL_IDS)
    if include_full_ancient_spellbook:
        names.extend(str(sprite_id) for sprite_id in ANCIENT_TELEPORT_SPELL_IDS)
    names.extend(CORE_NAMED_ASSETS)
    return sorted(set(names))


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--assets-dir",
        type=Path,
        default=Path("ocean/osrs/data/sprites/gui"),
    )
    parser.add_argument(
        "--full-ancient-spellbook",
        action="store_true",
        help="require ancient teleport icons in addition to combat spell icons",
    )
    parser.add_argument(
        "--require-transparent",
        action="store_true",
        help="check that known mask assets preserve alpha",
    )
    args = parser.parse_args(argv)

    missing: list[Path] = []
    bad_png: list[Path] = []
    bad_alpha: list[Path] = []

    for name in required_asset_names(args.full_ancient_spellbook):
        path = args.assets_dir / f"{name}.png"
        if not path.exists():
            missing.append(path)
        elif not is_png(path):
            bad_png.append(path)

    if args.require_transparent:
        for name in TRANSPARENT_ASSETS:
            path = args.assets_dir / f"{name}.png"
            if not path.exists():
                missing.append(path)
            elif not is_png(path):
                bad_png.append(path)
            elif transparent_pixel_count(path) <= 0:
                bad_alpha.append(path)

    for path in missing:
        print(f"missing required GUI sprite: {path}", file=sys.stderr)
    for path in bad_png:
        print(f"invalid PNG GUI sprite: {path}", file=sys.stderr)
    for path in bad_alpha:
        print(f"GUI sprite has no transparent pixels: {path}", file=sys.stderr)

    required = required_asset_names(args.full_ancient_spellbook)
    print(f"validated {len(required) - len(missing) - len(bad_png)}/{len(required)} GUI sprites")
    if args.require_transparent:
        print(
            f"validated transparency for {len(TRANSPARENT_ASSETS) - len(bad_alpha)}/"
            f"{len(TRANSPARENT_ASSETS)} selected GUI sprites"
        )
    return 1 if missing or bad_png or bad_alpha else 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
