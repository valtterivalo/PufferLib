"""Generate osrs_combat_visuals_generated.h from combat visual TSVs."""

import argparse
import csv
from pathlib import Path


STYLE_MAP = {
    "-": "OSRS_COMBAT_VISUAL_STYLE_ANY",
    "any": "OSRS_COMBAT_VISUAL_STYLE_ANY",
    "melee": "ATTACK_STYLE_MELEE",
    "stab": "ATTACK_STYLE_MELEE",
    "slash": "ATTACK_STYLE_MELEE",
    "crush": "ATTACK_STYLE_MELEE",
    "ranged": "ATTACK_STYLE_RANGED",
    "magic": "ATTACK_STYLE_MAGIC",
}

KIND_MAP = {
    "item": "OSRS_COMBAT_VISUAL_KIND_ITEM",
    "spell": "OSRS_COMBAT_VISUAL_KIND_SPELL",
    "npc": "OSRS_COMBAT_VISUAL_KIND_NPC",
    "special": "OSRS_COMBAT_VISUAL_KIND_SPECIAL",
}

SPELL_ID_MAP = {
    "Ice Barrage": "OSRS_COMBAT_VISUAL_SPELL_ICE_BARRAGE",
    "Blood Barrage": "OSRS_COMBAT_VISUAL_SPELL_BLOOD_BARRAGE",
}


def parse_int(value: str | None, default: str = "OSRS_COMBAT_PROJECTILE_MISSING") -> str:
    if value is None:
        return default
    value = value.strip()
    if value == "" or value == "-":
        return default
    return str(int(value))


def parse_key_id(kind: str, key: str) -> str:
    if kind in {"item", "npc", "special"}:
        return str(int(key))
    if key in SPELL_ID_MAP:
        return SPELL_ID_MAP[key]
    try:
        return str(int(key))
    except ValueError:
        return "OSRS_COMBAT_PROJECTILE_MISSING"


def escape_c_string(value: str) -> str:
    return value.replace("\\", "\\\\").replace('"', '\\"')


def row_initializer(row: dict[str, str]) -> str:
    kind = row["kind"].strip()
    key = row["key"].strip()
    style_key = row["style"].strip()
    style = STYLE_MAP[style_key] if style_key in STYLE_MAP else parse_int(style_key)
    key_name = escape_c_string(key if kind == "spell" else "")
    fields = [
        parse_int(row["launch_spotanim"]),
        parse_int(row["travel_spotanim"]),
        parse_int(row["impact_spotanim"]),
        parse_int(row["projectile_model"]),
        parse_int(row["projectile_anim"]),
        parse_int(row["hit_delay"]),
        parse_int(row["client_delay"]),
        parse_int(row["proj_start_height"]),
        parse_int(row["proj_end_height"]),
        parse_int(row["proj_delay"]),
        parse_int(row["proj_angle"]),
        parse_int(row["proj_length_adjustment"]),
        parse_int(row["proj_progress"]),
        parse_int(row["proj_step_multiplier"]),
        parse_int(row.get("projectile_count", ""), "1"),
    ]
    alt_fields = [
        parse_int(row.get("alt_proj_start_height", "")),
        parse_int(row.get("alt_proj_end_height", "")),
        parse_int(row.get("alt_proj_delay", "")),
        parse_int(row.get("alt_proj_angle", "")),
        parse_int(row.get("alt_proj_length_adjustment", "")),
        parse_int(row.get("alt_proj_progress", "")),
        parse_int(row.get("alt_proj_step_multiplier", "")),
    ]
    aux_fields = [
        parse_int(row.get("aux_travel_spotanim", "")),
        parse_int(row.get("aux_impact_spotanim", "")),
        parse_int(row.get("aux_projectile_model", "")),
        parse_int(row.get("aux_projectile_anim", "")),
        parse_int(row.get("impact_on_last_only", ""), "0"),
        parse_int(row.get("double_launch_spotanim", "")),
    ]
    return (
        "    {"
        f"{KIND_MAP[kind]}, {parse_key_id(kind, key)}, \"{key_name}\", {style}, "
        f"{parse_int(row.get('stance_idx', ''))}, {parse_int(row['attack_anim'])}, "
        "{" + ", ".join(fields) + "}, "
        "{" + ", ".join(alt_fields) + "}, "
        + ", ".join(aux_fields)
        + "}"
    )


def read_rows(path: Path) -> list[dict[str, str]]:
    with path.open(newline="") as source:
        reader = csv.DictReader(source, delimiter="|")
        return [
            row for row in reader
            if row["kind"].strip() in KIND_MAP
        ]


def write_header(rows: list[dict[str, str]], output: Path) -> None:
    output.write_text(
        "\n".join([
            "/**",
            " * @file osrs_combat_visuals_generated.h",
            " * @brief AUTO-GENERATED combat visuals from RuneC plus local overlays",
            " *",
            " * DO NOT EDIT. Regenerate with:",
            " *   python3 ocean/osrs/tools/generate_combat_visuals.py",
            " */",
            "",
            "#ifndef OSRS_COMBAT_VISUALS_GENERATED_H",
            "#define OSRS_COMBAT_VISUALS_GENERATED_H",
            "",
            "static const OsrsCombatVisualRow OSRS_COMBAT_VISUAL_ROWS[] = {",
            ",\n".join(row_initializer(row) for row in rows),
            "};",
            "",
            "static const size_t OSRS_COMBAT_VISUAL_ROW_COUNT =",
            "    sizeof(OSRS_COMBAT_VISUAL_ROWS) / sizeof(OSRS_COMBAT_VISUAL_ROWS[0]);",
            "",
            "#endif",
            "",
        ])
    )


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--tsv",
        default="refs/RuneC/data/defs/combat_visuals.tsv",
        type=Path,
    )
    parser.add_argument(
        "--extra-tsv",
        action="append",
        default=[Path("ocean/osrs/tools/combat_visuals_extra.tsv")],
        type=Path,
    )
    parser.add_argument(
        "--output",
        default="ocean/osrs/osrs_combat_visuals_generated.h",
        type=Path,
    )
    args = parser.parse_args()
    rows = read_rows(args.tsv)
    for path in args.extra_tsv:
        if path.is_file():
            rows.extend(read_rows(path))
    write_header(rows, args.output)


if __name__ == "__main__":
    main()
