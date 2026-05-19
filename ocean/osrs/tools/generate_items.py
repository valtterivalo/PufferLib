"""Generate osrs_items_generated.h from equipment.json reference data.

Reads the osrs-dps-calc equipment.json (wiki-sourced item stats) and a manifest
of which items to include, then outputs a C header with the full Item struct
database. This makes encounters data-driven: add an OSRS item ID to the manifest
and all stats are auto-populated from wiki data.

Usage:
    cd pufferlib-metal
    python ocean/osrs/tools/generate_items.py

    # with custom paths:
    python ocean/osrs/tools/generate_items.py \
        --json .refs/osrs-dps-calc/cdn/json/equipment.json \
        --manifest ocean/osrs/tools/items_manifest.json \
        --output ocean/osrs/osrs_items_generated.h

Input:
    equipment.json  — wiki-sourced item stats from osrs-dps-calc
    items_manifest.json — our item list: index name, OSRS ID, manual overrides

Output:
    osrs_items_generated.h — drop-in replacement for osrs_items.h

Stat field mapping (equipment.json -> Item struct):
    id              -> item_id
    name            -> name (truncated to 31 chars)
    slot            -> slot (string -> EquipmentSlot enum)
    speed           -> attack_speed (weapons only, 0 for non-weapons)
    (manual)        -> attack_range (not in JSON, must be in manifest)
    offensive.stab  -> attack_stab
    offensive.slash -> attack_slash
    offensive.crush -> attack_crush
    offensive.magic -> attack_magic
    offensive.ranged-> attack_ranged
    defensive.stab  -> defence_stab
    defensive.slash -> defence_slash
    defensive.crush -> defence_crush
    defensive.magic -> defence_magic
    defensive.ranged-> defence_ranged
    bonuses.str     -> melee_strength
    bonuses.ranged_str -> ranged_strength
    bonuses.magic_str  -> magic_damage
    bonuses.prayer  -> prayer
    (manifest)       -> effect_mask
"""

import argparse
import json
import re
import sys
from pathlib import Path

SLOT_MAP = {
    "head": "SLOT_HEAD",
    "cape": "SLOT_CAPE",
    "neck": "SLOT_NECK",
    "weapon": "SLOT_WEAPON",
    "body": "SLOT_BODY",
    "shield": "SLOT_SHIELD",
    "legs": "SLOT_LEGS",
    "hands": "SLOT_HANDS",
    "feet": "SLOT_FEET",
    "ring": "SLOT_RING",
    "ammo": "SLOT_AMMO",
}

# category -> default attack_range for weapons missing manual override.
# these are common OSRS ranges, but the manifest should override when needed.
CATEGORY_RANGE_DEFAULTS = {
    "Bow": 10,
    "Crossbow": 7,
    "Thrown": 4,
    "Chinchompas": 9,
    "Staff": 10,
    "Powered Staff": 10,
    "Bladed Staff": 10,
    "Polestaff": 10,
    "Salamander": 10,
    "Gun": 8,
    "Blaster": 8,
}

EFFECT_TAG_MAP = {
    "TWISTED_BOW": "OSRS_ITEM_EFFECT_TWISTED_BOW",
    "VIRTUS_PIECE": "OSRS_ITEM_EFFECT_VIRTUS_PIECE",
    "CONFLICTION": "OSRS_ITEM_EFFECT_CONFLICTION",
    "SANG_HEAL": "OSRS_ITEM_EFFECT_SANG_HEAL",
    "RECOIL_RING": "OSRS_ITEM_EFFECT_RECOIL_RING",
    "LIGHTBEARER": "OSRS_ITEM_EFFECT_LIGHTBEARER",
    "DHAROK_PIECE": "OSRS_ITEM_EFFECT_DHAROK_PIECE",
    "ELYSIAN": "OSRS_ITEM_EFFECT_ELYSIAN",
    "CRYSTAL_ARMOUR": "OSRS_ITEM_EFFECT_CRYSTAL_ARMOUR",
    "DRAGON_HUNTER_WAND": "OSRS_ITEM_EFFECT_DRAGON_HUNTER_WAND",
    "ECHO_BOOTS": "OSRS_ITEM_EFFECT_ECHO_BOOTS",
}


def load_equipment_json(path: str) -> dict[int, dict]:
    """Load equipment.json and index by OSRS item ID."""
    json_path = Path(path)
    if not json_path.exists():
        raise FileNotFoundError(
            f"{json_path} not found. pass --json pointing at osrs-dps-calc/cdn/json/equipment.json"
        )
    with open(json_path) as f:
        items = json.load(f)
    by_id: dict[int, list[dict]] = {}
    for item in items:
        item_id = item["id"]
        if item_id not in by_id:
            by_id[item_id] = []
        by_id[item_id].append(item)
    return by_id


def load_manifest(path: str) -> list[dict]:
    """Load the item manifest.

    Each entry: {"index": "ITEM_WHIP", "item_id": 4151, "version": "", "attack_range": 1, "comment": "..."}
    version and attack_range are optional.
    """
    with open(path) as f:
        return json.load(f)


def extract_manifest_from_source(items_h_path: str) -> list[dict]:
    """Extract item manifest from existing osrs_items.h (bootstrap helper).

    Parses the C source to get index names, OSRS IDs, attack_range values,
    and item names. Outputs a manifest list for items_manifest.json.
    """
    with open(items_h_path) as f:
        content = f.read()

    pattern = (
        r"\[(ITEM_\w+)\]\s*=\s*\{"
        r"[^}]*\.item_id\s*=\s*(\d+)"
        r"[^}]*\.name\s*=\s*\"([^\"]+)\""
        r"[^}]*\.attack_speed\s*=\s*(\d+)"
        r"[^}]*\.attack_range\s*=\s*(\d+)"
    )
    entries = []
    for match in re.finditer(pattern, content):
        idx_name, item_id, name, speed, attack_range = match.groups()
        entry = {
            "index": idx_name,
            "item_id": int(item_id),
            "attack_range": int(attack_range),
            "comment": name,
        }
        entries.append(entry)
    return entries


def find_item_in_json(
    by_id: dict[int, list[dict]], item_id: int, version: str = ""
) -> dict | None:
    """Find an item in equipment.json by ID and optional version."""
    candidates = by_id.get(item_id, [])
    if not candidates:
        return None
    if version:
        for c in candidates:
            if c.get("version", "") == version:
                return c
    # default: prefer unversioned, else first
    for c in candidates:
        if not c.get("version", ""):
            return c
    return candidates[0]


def generate_header(
    manifest: list[dict],
    by_id: dict[int, list[dict]],
) -> str:
    """Generate the C header content."""
    lines = []
    lines.append("/**")
    lines.append(
        " * @file osrs_items_generated.h"
    )
    lines.append(
        " * @brief AUTO-GENERATED item database from equipment.json"
    )
    lines.append(" *")
    lines.append(
        " * DO NOT EDIT — regenerate with:"
    )
    lines.append(
        " *   python ocean/osrs/tools/generate_items.py"
    )
    lines.append(" */")
    lines.append("")
    lines.append("#ifndef OSRS_ITEMS_GENERATED_H")
    lines.append("#define OSRS_ITEMS_GENERATED_H")
    lines.append("")

    # item index enum (EquipmentSlot enum and Item struct live in osrs_items.h)
    lines.append("typedef enum {")
    for i, entry in enumerate(manifest):
        comment = entry.get("comment", "")
        suffix = f"  /* {comment} */" if comment else ""
        lines.append(f"    {entry['index']} = {i},{suffix}")
    lines.append(f"    NUM_ITEMS = {len(manifest)},")
    lines.append("    ITEM_NONE = 255")
    lines.append("} ItemIndex;")
    lines.append("")

    # item database
    lines.append(
        f"static const Item ITEM_DATABASE[NUM_ITEMS] = {{"
    )
    warnings = []

    for entry in manifest:
        idx_name = entry["index"]
        item_id = entry["item_id"]
        version = entry.get("version", "")
        manual_range = entry.get("attack_range", None)
        effect_tags = entry.get("effect_tags", [])
        effect_mask = " | ".join(EFFECT_TAG_MAP[tag] for tag in effect_tags) if effect_tags else "OSRS_ITEM_EFFECT_NONE"

        json_item = find_item_in_json(by_id, item_id, version)

        if json_item is None:
            # check for manual_stats override in manifest (for LMS-only items etc)
            manual = entry.get("manual_stats")
            if manual:
                comment = entry.get("comment", f"id={item_id}")
                name = manual.get("name", comment)[:31]
                slot_enum = SLOT_MAP.get(manual.get("slot", "weapon"), "0")
                lines.append(f"    [{idx_name}] = {{ /* {comment} (manual) */")
                lines.append(
                    f"        .item_id = {item_id}, "
                    f'.name = "{name}", '
                    f".slot = {slot_enum},"
                )
                lines.append(
                    f"        .attack_speed = {manual.get('attack_speed', 0)}, "
                    f".attack_range = {entry.get('attack_range', 0)},"
                )
                lines.append(
                    f"        .attack_stab = {manual.get('attack_stab', 0)}, "
                    f".attack_slash = {manual.get('attack_slash', 0)}, "
                    f".attack_crush = {manual.get('attack_crush', 0)},"
                )
                lines.append(
                    f"        .attack_magic = {manual.get('attack_magic', 0)}, "
                    f".attack_ranged = {manual.get('attack_ranged', 0)},"
                )
                lines.append(
                    f"        .defence_stab = {manual.get('defence_stab', 0)}, "
                    f".defence_slash = {manual.get('defence_slash', 0)}, "
                    f".defence_crush = {manual.get('defence_crush', 0)},"
                )
                lines.append(
                    f"        .defence_magic = {manual.get('defence_magic', 0)}, "
                    f".defence_ranged = {manual.get('defence_ranged', 0)},"
                )
                lines.append(
                    f"        .melee_strength = {manual.get('melee_strength', 0)}, "
                    f".ranged_strength = {manual.get('ranged_strength', 0)}, "
                    f".magic_damage = {manual.get('magic_damage', 0)}, "
                    f".prayer = {manual.get('prayer', 0)}, "
                    f".effect_mask = {effect_mask}"
                )
                lines.append("    },")
                continue

            warnings.append(
                f"WARNING: {idx_name} (id={item_id}) not found in equipment.json "
                f"and no manual_stats in manifest"
            )
            lines.append(f"    /* WARNING: {idx_name} (id={item_id}) NOT FOUND */")
            lines.append(f"    [{idx_name}] = {{")
            lines.append(f"        .item_id = {item_id}, "
                         f'.name = "MISSING", .slot = 0,')
            lines.append("        .attack_speed = 0, .attack_range = 0,")
            lines.append(
                "        .attack_stab = 0, .attack_slash = 0, .attack_crush = 0,"
            )
            lines.append("        .attack_magic = 0, .attack_ranged = 0,")
            lines.append(
                "        .defence_stab = 0, .defence_slash = 0, .defence_crush = 0,"
            )
            lines.append("        .defence_magic = 0, .defence_ranged = 0,")
            lines.append(
                "        .melee_strength = 0, .ranged_strength = 0, "
                f".magic_damage = 0, .prayer = 0, .effect_mask = {effect_mask}"
            )
            lines.append("    },")
            continue

        name = json_item["name"][:31]
        slot_str = json_item.get("slot", "")
        slot_enum = SLOT_MAP.get(slot_str, "0")

        speed = json_item.get("speed", 0) if slot_str == "weapon" else 0

        # attack_range: prefer manual override, then category default, then 0
        if manual_range is not None:
            attack_range = manual_range
        elif slot_str == "weapon":
            category = json_item.get("category", "")
            attack_range = CATEGORY_RANGE_DEFAULTS.get(category, 1)
        else:
            attack_range = 0

        off = json_item.get("offensive", {})
        defe = json_item.get("defensive", {})
        bon = json_item.get("bonuses", {})

        # magic_str in equipment.json is in tenths of a percent (150 = 15.0%).
        # our Item struct uses whole percent (15 = 15%). convert with rounding.
        raw_magic_str = bon.get("magic_str", 0)
        magic_damage_pct = (raw_magic_str + 5) // 10 if raw_magic_str > 0 else 0

        comment = entry.get("comment", name)
        lines.append(f"    [{idx_name}] = {{ /* {comment} */")
        lines.append(
            f"        .item_id = {item_id}, "
            f'.name = "{name}", '
            f".slot = {slot_enum},"
        )
        lines.append(
            f"        .attack_speed = {speed}, .attack_range = {attack_range},"
        )
        lines.append(
            f"        .attack_stab = {off.get('stab', 0)}, "
            f".attack_slash = {off.get('slash', 0)}, "
            f".attack_crush = {off.get('crush', 0)},"
        )
        lines.append(
            f"        .attack_magic = {off.get('magic', 0)}, "
            f".attack_ranged = {off.get('ranged', 0)},"
        )
        lines.append(
            f"        .defence_stab = {defe.get('stab', 0)}, "
            f".defence_slash = {defe.get('slash', 0)}, "
            f".defence_crush = {defe.get('crush', 0)},"
        )
        lines.append(
            f"        .defence_magic = {defe.get('magic', 0)}, "
            f".defence_ranged = {defe.get('ranged', 0)},"
        )
        lines.append(
            f"        .melee_strength = {bon.get('str', 0)}, "
            f".ranged_strength = {bon.get('ranged_str', 0)}, "
            f".magic_damage = {magic_damage_pct}, "
            f".prayer = {bon.get('prayer', 0)}, "
            f".effect_mask = {effect_mask}"
        )
        lines.append("    },")

    lines.append("};")
    lines.append("")
    lines.append("#endif /* OSRS_ITEMS_GENERATED_H */")
    lines.append("")

    return "\n".join(lines), warnings


def main():
    parser = argparse.ArgumentParser(
        description="Generate C item database from equipment.json"
    )
    parser.add_argument(
        "--json",
        default=".refs/osrs-dps-calc/cdn/json/equipment.json",
        help="path to equipment.json",
    )
    parser.add_argument(
        "--manifest",
        default="ocean/osrs/tools/items_manifest.json",
        help="path to items manifest JSON",
    )
    parser.add_argument(
        "--output",
        default="ocean/osrs/osrs_items_generated.h",
        help="output C header path",
    )
    parser.add_argument(
        "--bootstrap",
        action="store_true",
        help="extract manifest from existing osrs_items.h and write to --manifest",
    )
    parser.add_argument(
        "--items-h",
        default="ocean/osrs/osrs_items.h",
        help="path to existing osrs_items.h (for --bootstrap)",
    )
    args = parser.parse_args()

    if args.bootstrap:
        print(f"bootstrapping manifest from {args.items_h}...")
        manifest = extract_manifest_from_source(args.items_h)
        Path(args.manifest).parent.mkdir(parents=True, exist_ok=True)
        with open(args.manifest, "w") as f:
            json.dump(manifest, f, indent=2)
        print(f"wrote {len(manifest)} items to {args.manifest}")
        return

    print(f"loading equipment.json from {args.json}...")
    by_id = load_equipment_json(args.json)
    print(f"  {sum(len(v) for v in by_id.values())} items indexed by {len(by_id)} unique IDs")

    print(f"loading manifest from {args.manifest}...")
    manifest = load_manifest(args.manifest)
    print(f"  {len(manifest)} items in manifest")

    print("generating header...")
    header_content, warnings = generate_header(manifest, by_id)

    for w in warnings:
        print(f"  {w}", file=sys.stderr)

    Path(args.output).parent.mkdir(parents=True, exist_ok=True)
    with open(args.output, "w") as f:
        f.write(header_content)

    print(f"wrote {args.output}")
    if warnings:
        print(f"  {len(warnings)} warnings — check stderr")


if __name__ == "__main__":
    main()
