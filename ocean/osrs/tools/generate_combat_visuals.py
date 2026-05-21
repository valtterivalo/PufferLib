"""Generate osrs_combat_visuals_generated.h from combat visual TSVs."""

import argparse
import csv
from pathlib import Path


RUNEC_TSV_FIELDS = [
    "kind",
    "key",
    "style",
    "attack_anim",
    "launch_spotanim",
    "travel_spotanim",
    "impact_spotanim",
    "projectile_model",
    "projectile_anim",
    "hit_delay",
    "client_delay",
    "proj_start_height",
    "proj_end_height",
    "proj_delay",
    "proj_angle",
    "proj_length_adjustment",
    "proj_progress",
    "proj_step_multiplier",
    "note",
]

PUFFER_TSV_EXTENSION_FIELDS = [
    "projectile_count",
    "alt_proj_start_height",
    "alt_proj_end_height",
    "alt_proj_delay",
    "alt_proj_angle",
    "alt_proj_length_adjustment",
    "alt_proj_progress",
    "alt_proj_step_multiplier",
    "aux_travel_spotanim",
    "aux_impact_spotanim",
    "aux_projectile_model",
    "aux_projectile_anim",
    "impact_on_last_only",
    "double_launch_spotanim",
    "stance_idx",
]

COMBAT_VISUAL_TSV_FIELDS = RUNEC_TSV_FIELDS + PUFFER_TSV_EXTENSION_FIELDS

INTEGER_FIELDS = {
    "attack_anim",
    "launch_spotanim",
    "travel_spotanim",
    "impact_spotanim",
    "projectile_model",
    "projectile_anim",
    "hit_delay",
    "client_delay",
    "proj_start_height",
    "proj_end_height",
    "proj_delay",
    "proj_angle",
    "proj_length_adjustment",
    "proj_progress",
    "proj_step_multiplier",
    "projectile_count",
    "alt_proj_start_height",
    "alt_proj_end_height",
    "alt_proj_delay",
    "alt_proj_angle",
    "alt_proj_length_adjustment",
    "alt_proj_progress",
    "alt_proj_step_multiplier",
    "aux_travel_spotanim",
    "aux_impact_spotanim",
    "aux_projectile_model",
    "aux_projectile_anim",
    "impact_on_last_only",
    "double_launch_spotanim",
    "stance_idx",
}

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

def required_coverage(
    label: str,
    kind: str,
    key: str,
    style: str,
    **fields: str,
) -> dict[str, object]:
    """Build a required coverage check for merged combat visual rows."""
    return {
        "label": label,
        "kind": kind,
        "key": key,
        "style": style,
        "fields": fields,
    }


REQUIRED_COVERAGE_CHECKS = [
    required_coverage(
        "Inferno ranger", "npc", "7698", "ranged",
        attack_anim="7605", travel_spotanim="1377",
        impact_spotanim="1378", projectile_model="33013"),
    required_coverage(
        "Inferno mager", "npc", "7699", "magic",
        attack_anim="7610", travel_spotanim="1376",
        projectile_model="33007", projectile_anim="7571"),
    required_coverage(
        "Inferno Jad magic", "npc", "7700", "magic",
        attack_anim="7592", travel_spotanim="448",
        projectile_model="9337", projectile_anim="2659"),
    required_coverage(
        "Inferno Jad ranged", "npc", "7700", "ranged",
        attack_anim="7593", travel_spotanim="451",
        projectile_model="9342", projectile_anim="2660"),
    required_coverage(
        "Inferno Zuk", "npc", "7706", "magic",
        attack_anim="7566", travel_spotanim="1375",
        projectile_model="33006", projectile_anim="7571"),
    required_coverage(
        "toxic blowpipe attack", "item", "12926", "any",
        attack_anim="5061", hit_delay="2", client_delay="2",
        proj_delay="32"),
    required_coverage(
        "toxic blowpipe special", "special", "12926", "ranged",
        attack_anim="5061", launch_spotanim="1043"),
    required_coverage(
        "twisted bow", "item", "20997", "any",
        attack_anim="426", proj_start_height="163",
        proj_end_height="146", proj_delay="41"),
    required_coverage(
        "bow of faerdhinen", "item", "25865", "any",
        attack_anim="426", proj_start_height="163",
        proj_end_height="146", proj_delay="41"),
    required_coverage(
        "ice barrage", "spell", "Ice Barrage", "magic",
        attack_anim="811", travel_spotanim="368",
        impact_spotanim="369", projectile_model="14215",
        projectile_anim="1964"),
    required_coverage(
        "blood barrage", "spell", "Blood Barrage", "magic",
        attack_anim="811", impact_spotanim="377", proj_delay="51"),
    required_coverage(
        "shadow barrage", "spell", "Shadow Barrage", "magic",
        attack_anim="811", impact_spotanim="383", proj_delay="51"),
    required_coverage(
        "smoke barrage", "spell", "Smoke Barrage", "magic",
        attack_anim="811", travel_spotanim="390",
        impact_spotanim="391", projectile_model="6398",
        projectile_anim="1986"),
    required_coverage(
        "rune crossbow budget weapon", "item", "9185", "any",
        attack_anim="7552", proj_start_height="155", proj_angle="5"),
    required_coverage(
        "magic shortbow i budget weapon", "item", "12788", "any",
        attack_anim="426", proj_start_height="163", proj_angle="15"),
    required_coverage(
        "kodai wand budget weapon", "item", "21006", "any",
        attack_anim="414"),
    required_coverage(
        "eye of ayak normal", "item", "31113", "magic",
        attack_anim="12397", launch_spotanim="3366",
        travel_spotanim="3367", impact_spotanim="3368",
        projectile_model="28450", projectile_anim="12398"),
    required_coverage(
        "eye of ayak special", "special", "31113", "magic",
        attack_anim="12394", launch_spotanim="3364",
        travel_spotanim="3367", impact_spotanim="3365",
        projectile_model="28450", projectile_anim="12398"),
]

OPTIONAL_IF_PRESENT_COVERAGE_CHECKS = [{
    "label": "dragon hunter wand",
    "kind": "item",
    "key": "30070",
    "style": None,
    "non_missing_fields": ["attack_anim"],
}]


def clean_cell(value: str | None) -> str:
    """Normalize a TSV cell to the generator's in-memory contract."""
    if value is None:
        return ""
    return value.strip()


def is_int_token(value: str) -> bool:
    """Return whether a normalized TSV cell is a base-10 integer."""
    if value.startswith("-"):
        return len(value) > 1 and value[1:].isdigit()
    return value.isdigit()


def parse_int(value: str | None, default: str = "OSRS_COMBAT_PROJECTILE_MISSING") -> str:
    value = clean_cell(value)
    if value == "" or value == "-":
        return default
    if not is_int_token(value):
        raise ValueError(f"expected integer cell, got {value!r}")
    return str(int(value))


def parse_key_id(kind: str, key: str) -> str:
    if kind in {"item", "npc", "special"}:
        if not is_int_token(key):
            raise ValueError(f"expected integer key for {kind}, got {key!r}")
        return str(int(key))
    if key in SPELL_ID_MAP:
        return SPELL_ID_MAP[key]
    if is_int_token(key):
        return str(int(key))
    return "OSRS_COMBAT_PROJECTILE_MISSING"


def escape_c_string(value: str) -> str:
    return value.replace("\\", "\\\\").replace('"', '\\"')


def row_initializer(row: dict[str, str]) -> str:
    kind = clean_cell(row["kind"])
    key = clean_cell(row["key"])
    style_key = clean_cell(row["style"])
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


def validate_fieldnames(path: Path, fieldnames: list[str] | None) -> None:
    """Validate the pipe TSV header against RuneC plus local extension fields."""
    if fieldnames is None:
        raise ValueError(f"{path}: missing combat visual TSV header")
    if len(fieldnames) < len(RUNEC_TSV_FIELDS):
        raise ValueError(f"{path}: combat visual TSV header is too short")
    expected = COMBAT_VISUAL_TSV_FIELDS[:len(fieldnames)]
    if fieldnames != expected:
        raise ValueError(
            f"{path}: expected TSV fields {expected}, got {fieldnames}")


def validate_integer_field(row: dict[str, str], field: str) -> None:
    """Validate one integer-like field before emitting C initializers."""
    value = clean_cell(row.get(field))
    if value == "" or value == "-":
        return
    if not is_int_token(value):
        raise ValueError(
            f"{row['_source']}:{row['_line']}: {field} must be an integer, got {value!r}")
    parsed = int(value)
    if field == "projectile_count" and not 1 <= parsed <= 4:
        raise ValueError(
            f"{row['_source']}:{row['_line']}: projectile_count must be 1..4")
    if field == "impact_on_last_only" and parsed not in {0, 1}:
        raise ValueError(
            f"{row['_source']}:{row['_line']}: impact_on_last_only must be 0 or 1")


def validate_row(row: dict[str, str]) -> None:
    """Validate one normalized TSV row before generation."""
    kind = clean_cell(row["kind"])
    key = clean_cell(row["key"])
    style = clean_cell(row["style"])
    if kind not in KIND_MAP:
        raise ValueError(f"{row['_source']}:{row['_line']}: unknown kind {kind!r}")
    if key == "":
        raise ValueError(f"{row['_source']}:{row['_line']}: missing key")
    if kind in {"item", "npc", "special"} and not is_int_token(key):
        raise ValueError(
            f"{row['_source']}:{row['_line']}: {kind} key must be an integer")
    if style not in STYLE_MAP and not is_int_token(style):
        raise ValueError(f"{row['_source']}:{row['_line']}: unknown style {style!r}")
    for field in INTEGER_FIELDS:
        validate_integer_field(row, field)


def read_rows(path: Path) -> list[dict[str, str]]:
    with path.open(newline="") as source:
        reader = csv.DictReader(source, delimiter="|")
        validate_fieldnames(path, reader.fieldnames)
        rows = []
        for row in reader:
            if row.get(None):
                raise ValueError(f"{path}:{reader.line_num}: too many TSV fields")
            for field in COMBAT_VISUAL_TSV_FIELDS:
                row.setdefault(field, "")
            row["_source"] = str(path)
            row["_line"] = str(reader.line_num)
            validate_row(row)
            rows.append(row)
        return rows


def coverage_candidates(
    rows: list[dict[str, str]],
    check: dict[str, object],
) -> list[dict[str, str]]:
    """Find rows matching a coverage contract entry."""
    style = check["style"]
    return [
        row for row in rows
        if clean_cell(row["kind"]) == check["kind"]
        and clean_cell(row["key"]) == check["key"]
        and (style is None or clean_cell(row["style"]) == style)
    ]


def validate_coverage_check(
    rows: list[dict[str, str]],
    check: dict[str, object],
    required: bool,
) -> None:
    """Validate one required or if-present coverage entry."""
    matches = coverage_candidates(rows, check)
    label = check["label"]
    if not matches:
        if required:
            raise ValueError(f"missing combat visual coverage row: {label}")
        return
    row = matches[0]
    for field, expected in check.get("fields", {}).items():
        actual = clean_cell(row.get(field))
        if actual != expected:
            raise ValueError(
                f"{row['_source']}:{row['_line']}: {label} expected "
                f"{field}={expected!r}, got {actual!r}")
    for field in check.get("non_missing_fields", []):
        actual = clean_cell(row.get(field))
        if actual == "" or actual == "-":
            raise ValueError(
                f"{row['_source']}:{row['_line']}: {label} missing {field}")


def validate_coverage(rows: list[dict[str, str]]) -> None:
    """Validate the rows that keep generated combat visuals useful."""
    for check in REQUIRED_COVERAGE_CHECKS:
        validate_coverage_check(rows, check, required=True)
    for check in OPTIONAL_IF_PRESENT_COVERAGE_CHECKS:
        validate_coverage_check(rows, check, required=False)


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
        if not path.is_file():
            raise FileNotFoundError(path)
        rows.extend(read_rows(path))
    validate_coverage(rows)
    write_header(rows, args.output)


if __name__ == "__main__":
    main()
