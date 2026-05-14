"""Generate osrs_monsters_generated.h from monsters.json reference data.

Reads the osrs-dps-calc monsters.json (wiki-sourced monster stats) and a manifest
of which monsters to include, then outputs a C header with the full monster database.
This makes encounters data-driven: add an OSRS NPC ID to the manifest and all stats
are auto-populated from wiki data.

Usage:
    cd pufferlib-metal
    python ocean/osrs/tools/generate_monsters.py

    # bootstrap manifest from encounter_inferno.h:
    python ocean/osrs/tools/generate_monsters.py --bootstrap

Input:
    monsters.json       — wiki-sourced monster stats from osrs-dps-calc
    monsters_manifest.json — our monster list: index name, NPC ID, version, overrides

Output:
    osrs_monsters_generated.h — generated monster database

Generated types: MonsterIndex enum, MonsterStats struct, MONSTER_DATABASE array.

Stat field mapping (monsters.json -> MonsterStats struct):
    id                  -> npc_id
    name                -> name
    skills.hp           -> hp
    skills.atk          -> att_level
    skills.str          -> str_level
    skills.def          -> def_level
    skills.magic        -> magic_level
    skills.ranged       -> range_level
    speed               -> attack_speed
    size                -> size
    max_hit             -> max_hit (parsed from string, may contain HTML entities)
    offensive.atk       -> melee_att_bonus
    offensive.str       -> melee_str_bonus
    offensive.magic     -> magic_att_bonus
    offensive.magic_str -> magic_str_bonus
    offensive.ranged    -> range_att_bonus
    offensive.ranged_str-> ranged_str_bonus
    defensive.stab      -> stab_def
    defensive.slash     -> slash_def
    defensive.crush     -> crush_def
    defensive.magic     -> magic_def
    defensive.light     -> ranged_def (light/standard/heavy are equal for most NPCs)
    style               -> attack_styles (list of strings)
"""

import argparse
import json
import re
import sys
from pathlib import Path


def load_monsters_json(path: str) -> dict[int, list[dict]]:
    """Load monsters.json and index by NPC ID."""
    with open(path) as f:
        monsters = json.load(f)
    by_id: dict[int, list[dict]] = {}
    for m in monsters:
        npc_id = m["id"]
        if npc_id not in by_id:
            by_id[npc_id] = []
        by_id[npc_id].append(m)
    return by_id


def load_manifest(path: str) -> list[dict]:
    """Load the monster manifest."""
    with open(path) as f:
        return json.load(f)


def find_monster(by_id: dict[int, list[dict]], npc_id: int, version: str = "") -> dict | None:
    """Find a monster in monsters.json by ID and optional version."""
    candidates = by_id.get(npc_id, [])
    if not candidates:
        return None
    if version:
        for c in candidates:
            if c.get("version", "") == version:
                return c
    for c in candidates:
        if not c.get("version", ""):
            return c
    return candidates[0]


def parse_max_hit(raw: str | int) -> int:
    """Parse max_hit from monsters.json (may contain HTML entities or ranges)."""
    if isinstance(raw, int):
        return raw
    cleaned = re.sub(r"&\w+;", " ", str(raw))
    cleaned = re.split(r"\s*\(", cleaned)[0].strip()
    range_matches = [
        int(value)
        for value in re.findall(r"\d+", cleaned.replace("–", "-").replace("—", "-"))
    ]
    if range_matches:
        return max(range_matches)
    return 0


def generate_header(manifest: list[dict], by_id: dict[int, list[dict]]) -> str:
    """Generate the C header content."""
    lines = []
    lines.append("/**")
    lines.append(" * @file osrs_monsters_generated.h")
    lines.append(" * @brief AUTO-GENERATED monster database from monsters.json")
    lines.append(" *")
    lines.append(" * DO NOT EDIT — regenerate with:")
    lines.append(" *   python ocean/osrs/tools/generate_monsters.py")
    lines.append(" */")
    lines.append("")
    lines.append("#ifndef OSRS_MONSTERS_GENERATED_H")
    lines.append("#define OSRS_MONSTERS_GENERATED_H")
    lines.append("")
    lines.append("#include <stdint.h>")
    lines.append("")

    # monster index enum
    lines.append("typedef enum {")
    for i, entry in enumerate(manifest):
        comment = entry.get("comment", "")
        suffix = f"  /* {comment} */" if comment else ""
        lines.append(f"    {entry['index']} = {i},{suffix}")
    lines.append(f"    NUM_MONSTERS = {len(manifest)}")
    lines.append("} MonsterIndex;")
    lines.append("")

    # monster struct
    lines.append("typedef struct {")
    lines.append("    uint16_t npc_id;")
    lines.append("    char name[32];")
    lines.append("    int16_t hp;")
    lines.append("    int16_t att_level;")
    lines.append("    int16_t str_level;")
    lines.append("    int16_t def_level;")
    lines.append("    int16_t magic_level;")
    lines.append("    int16_t range_level;")
    lines.append("    uint8_t attack_speed;")
    lines.append("    uint8_t size;")
    lines.append("    int16_t max_hit;")
    lines.append("    /* offensive bonuses */")
    lines.append("    int16_t melee_att_bonus;")
    lines.append("    int16_t melee_str_bonus;")
    lines.append("    int16_t magic_att_bonus;")
    lines.append("    int16_t magic_str_bonus;")
    lines.append("    int16_t range_att_bonus;")
    lines.append("    int16_t ranged_str_bonus;")
    lines.append("    /* defensive bonuses */")
    lines.append("    int16_t stab_def;")
    lines.append("    int16_t slash_def;")
    lines.append("    int16_t crush_def;")
    lines.append("    int16_t magic_def;")
    lines.append("    int16_t ranged_def;")
    lines.append("} MonsterStats;")
    lines.append("")

    # monster database
    lines.append(f"static const MonsterStats MONSTER_DATABASE[NUM_MONSTERS] = {{")
    warnings = []

    for entry in manifest:
        idx_name = entry["index"]
        npc_id = entry["npc_id"]
        version = entry.get("version", "")

        m = find_monster(by_id, npc_id, version)

        if m is None:
            manual = entry.get("manual_stats")
            if manual:
                comment = entry.get("comment", f"id={npc_id}")
                name = manual.get("name", comment)[:31]
                lines.append(f"    [{idx_name}] = {{ /* {comment} (manual) */")
                lines.append(f"        .npc_id = {npc_id}, .name = \"{name}\",")
                lines.append(f"        .hp = {manual.get('hp', 0)}, "
                             f".att_level = {manual.get('att_level', 0)}, "
                             f".str_level = {manual.get('str_level', 0)}, "
                             f".def_level = {manual.get('def_level', 0)},")
                lines.append(f"        .magic_level = {manual.get('magic_level', 0)}, "
                             f".range_level = {manual.get('range_level', 0)},")
                lines.append(f"        .attack_speed = {manual.get('attack_speed', 0)}, "
                             f".size = {manual.get('size', 1)}, "
                             f".max_hit = {manual.get('max_hit', 0)},")
                lines.append(f"        .melee_att_bonus = 0, .melee_str_bonus = 0, "
                             f".magic_att_bonus = 0, .magic_str_bonus = 0, "
                             f".range_att_bonus = 0, .ranged_str_bonus = 0,")
                lines.append(f"        .stab_def = 0, .slash_def = 0, .crush_def = 0, "
                             f".magic_def = 0, .ranged_def = 0")
                lines.append("    },")
                continue

            warnings.append(f"WARNING: {idx_name} (id={npc_id}) not found in monsters.json")
            lines.append(f"    /* WARNING: {idx_name} (id={npc_id}) NOT FOUND */")
            lines.append(f"    [{idx_name}] = {{ .npc_id = {npc_id}, .name = \"MISSING\" }},")
            continue

        name = m["name"][:31]
        skills = m.get("skills", {})
        off = m.get("offensive", {})
        defe = m.get("defensive", {})
        max_hit = parse_max_hit(m.get("max_hit", 0))
        comment = entry.get("comment", name)

        lines.append(f"    [{idx_name}] = {{ /* {comment} */")
        lines.append(f"        .npc_id = {npc_id}, .name = \"{name}\",")
        lines.append(f"        .hp = {skills.get('hp', 0)}, "
                     f".att_level = {skills.get('atk', 0)}, "
                     f".str_level = {skills.get('str', 0)}, "
                     f".def_level = {skills.get('def', 0)},")
        lines.append(f"        .magic_level = {skills.get('magic', 0)}, "
                     f".range_level = {skills.get('ranged', 0)},")
        lines.append(f"        .attack_speed = {m.get('speed', 0)}, "
                     f".size = {m.get('size', 1)}, "
                     f".max_hit = {max_hit},")
        lines.append(f"        .melee_att_bonus = {off.get('atk', 0)}, "
                     f".melee_str_bonus = {off.get('str', 0)}, "
                     f".magic_att_bonus = {off.get('magic', 0)}, "
                     f".magic_str_bonus = {off.get('magic_str', 0)},")
        lines.append(f"        .range_att_bonus = {off.get('ranged', 0)}, "
                     f".ranged_str_bonus = {off.get('ranged_str', 0)},")
        lines.append(f"        .stab_def = {defe.get('stab', 0)}, "
                     f".slash_def = {defe.get('slash', 0)}, "
                     f".crush_def = {defe.get('crush', 0)},")
        lines.append(f"        .magic_def = {defe.get('magic', 0)}, "
                     f".ranged_def = {defe.get('light', 0)}")
        lines.append("    },")

    lines.append("};")
    lines.append("")
    lines.append("#endif /* OSRS_MONSTERS_GENERATED_H */")
    lines.append("")

    for w in warnings:
        print(w, file=sys.stderr)

    return "\n".join(lines)


def bootstrap_inferno_manifest() -> list[dict]:
    """Generate manifest from inferno encounter NPC IDs."""
    return [
        {"index": "MON_JAL_NIB", "npc_id": 7691, "comment": "Nibbler"},
        {"index": "MON_JAL_MEJRAH", "npc_id": 7692, "comment": "Bat"},
        {"index": "MON_JAL_AK", "npc_id": 7693, "comment": "Blob"},
        {"index": "MON_JAL_AKREK_MEJ", "npc_id": 7694, "comment": "Blob mage split"},
        {"index": "MON_JAL_AKREK_XIL", "npc_id": 7695, "comment": "Blob range split"},
        {"index": "MON_JAL_AKREK_KET", "npc_id": 7696, "comment": "Blob melee split"},
        {"index": "MON_JAL_IMKOT", "npc_id": 7697, "comment": "Meleer"},
        {"index": "MON_JAL_XIL", "npc_id": 7698, "comment": "Ranger"},
        {"index": "MON_JAL_ZEK", "npc_id": 7699, "comment": "Mager"},
        {"index": "MON_JALTOK_JAD", "npc_id": 7700, "comment": "Jad"},
        {"index": "MON_YT_HURKOT", "npc_id": 7701, "comment": "Jad healer"},
        {"index": "MON_TZKAL_ZUK", "npc_id": 7706, "comment": "Zuk",
         "version": "Normal"},
        {"index": "MON_ZUK_SHIELD", "npc_id": 7707, "comment": "Ancestral Glyph",
         "manual_stats": {"name": "Ancestral Glyph", "hp": 600, "size": 5,
                          "attack_speed": 0, "max_hit": 0}},
        {"index": "MON_JAL_MEJJAK", "npc_id": 7708, "comment": "Zuk healer"},
    ]


def main():
    parser = argparse.ArgumentParser(description="generate monster database from monsters.json")
    parser.add_argument(
        "--json", type=Path,
        default=Path(".refs/osrs-dps-calc/cdn/json/monsters.json"),
        help="path to monsters.json",
    )
    parser.add_argument(
        "--manifest", type=Path,
        default=Path("ocean/osrs/tools/monsters_manifest.json"),
        help="path to monster manifest JSON",
    )
    parser.add_argument(
        "--output", type=Path,
        default=Path("ocean/osrs/osrs_monsters_generated.h"),
        help="output header file",
    )
    parser.add_argument(
        "--bootstrap", action="store_true",
        help="generate initial manifest from inferno NPCs and exit",
    )
    args = parser.parse_args()

    if args.bootstrap:
        manifest = bootstrap_inferno_manifest()
        args.manifest.parent.mkdir(parents=True, exist_ok=True)
        with open(args.manifest, "w") as f:
            json.dump(manifest, f, indent=2)
        print(f"bootstrapped {len(manifest)} monsters to {args.manifest}")
        return

    by_id = load_monsters_json(str(args.json))
    print(f"loaded {sum(len(v) for v in by_id.values())} monsters from {args.json}")

    manifest = load_manifest(str(args.manifest))
    print(f"manifest: {len(manifest)} monsters")

    header = generate_header(manifest, by_id)

    args.output.parent.mkdir(parents=True, exist_ok=True)
    with open(args.output, "w") as f:
        f.write(header)
    print(f"wrote {len(header):,} bytes to {args.output}")


if __name__ == "__main__":
    main()
