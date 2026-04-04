"""Cross-reference inferno NPC stats in encounter_inferno.h against wiki monsters.json.

Compares: HP, attack speed, size, combat levels, offensive bonuses, defensive bonuses,
and wiki max hit against our max_hit_cap where applicable.
"""

import json
import re
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
ENCOUNTER_H = REPO / "pufferlib" / "ocean" / "osrs" / "encounters" / "encounter_inferno.h"
MONSTERS_JSON = REPO / ".refs" / "osrs-dps-calc" / "cdn" / "json" / "monsters.json"

# field mapping: (c_field, json_path, description)
FIELD_MAP = [
    ("hp",              ("skills", "hp"),           "hitpoints"),
    ("attack_speed",    ("speed",),                 "attack speed (ticks)"),
    ("size",            ("size",),                  "NPC size"),
    ("att_level",       ("skills", "atk"),          "attack level"),
    ("str_level",       ("skills", "str"),          "strength level"),
    ("def_level",       ("skills", "def"),          "defence level"),
    ("range_level",     ("skills", "ranged"),       "ranged level"),
    ("magic_level",     ("skills", "magic"),        "magic level"),
    ("melee_att_bonus", ("offensive", "atk"),       "melee attack bonus"),
    ("range_att_bonus", ("offensive", "ranged"),    "ranged attack bonus"),
    ("magic_att_bonus", ("offensive", "magic"),     "magic attack bonus"),
    ("melee_str_bonus", ("offensive", "str"),       "melee strength bonus"),
    ("ranged_str_bonus",("offensive", "ranged_str"),"ranged strength bonus"),
    ("stab_def",        ("defensive", "stab"),      "stab defence"),
    ("slash_def",       ("defensive", "slash"),     "slash defence"),
    ("crush_def",       ("defensive", "crush"),     "crush defence"),
    ("magic_def_bonus", ("defensive", "magic"),     "magic defence"),
]

# new-style ranged defence fields (light/standard/heavy replaced single "ranged")
RANGED_DEF_FIELDS = ["light", "standard", "heavy"]


def parse_npc_def_ids(text: str) -> dict[str, int]:
    """Parse INF_NPC_DEF_IDS array from C source (only that section)."""
    # isolate the DEF_IDS array to avoid matching other arrays with same enum keys
    start = text.find("INF_NPC_DEF_IDS[INF_NUM_NPC_TYPES]")
    if start < 0:
        raise ValueError("could not find INF_NPC_DEF_IDS array")
    end = text.find("};", start)
    section = text[start:end]

    ids = {}
    for m in re.finditer(r"\[(INF_NPC_\w+)\]\s*=\s*(\d+)", section):
        ids[m.group(1)] = int(m.group(2))
    return ids


def parse_npc_stats(text: str) -> dict[str, dict]:
    """Parse INF_NPC_STATS entries from C source."""
    # find the stats array region
    stats_start = text.find("INF_NPC_STATS[INF_NUM_NPC_TYPES]")
    if stats_start < 0:
        raise ValueError("could not find INF_NPC_STATS array")

    stats_text = text[stats_start:]
    npcs = {}

    # match each [INF_NPC_XXX] = { ... } block
    pattern = re.compile(r"\[(INF_NPC_\w+)\]\s*=\s*\{([^}]+)\}", re.DOTALL)
    for m in pattern.finditer(stats_text):
        enum_name = m.group(1)
        body = m.group(2)

        fields = {}
        for fm in re.finditer(r"\.(\w+)\s*=\s*(-?\d+)", body):
            fields[fm.group(1)] = int(fm.group(2))

        npcs[enum_name] = fields

    return npcs


def build_monster_lookup(path: str) -> dict[int, list[dict]]:
    """Build NPC id -> list of monster entries from wiki JSON."""
    with open(path) as f:
        data = json.load(f)
    lookup = {}
    for entry in data:
        lookup.setdefault(entry["id"], []).append(entry)
    return lookup


def get_json_value(entry: dict, json_path: tuple) -> int | None:
    """Navigate nested JSON path to get a value."""
    current = entry
    for key in json_path:
        if isinstance(current, dict) and key in current:
            current = current[key]
        else:
            return None
    if isinstance(current, (int, float)):
        return int(current)
    return None


def main():
    text = Path(ENCOUNTER_H).read_text()
    def_ids = parse_npc_def_ids(text)
    npc_stats = parse_npc_stats(text)
    wiki = build_monster_lookup(MONSTERS_JSON)

    # only compare NPCs that appear in both DEF_IDS and STATS
    npc_names = sorted(set(def_ids.keys()) & set(npc_stats.keys()))

    print(f"parsed {len(npc_stats)} NPCs from encounter_inferno.h")
    print(f"loaded {len(wiki)} unique NPC IDs from monsters.json")
    print(f"cross-referencing {len(npc_names)} inferno NPCs\n")

    real_bugs = []
    missing_npcs = []
    info_notes = []

    for npc_name in npc_names:
        npc_id = def_ids[npc_name]
        fields = npc_stats[npc_name]

        if npc_id not in wiki:
            missing_npcs.append((npc_name, npc_id))
            continue

        wiki_entries = wiki[npc_id]
        # prefer "Normal" version, then empty version, then first entry
        best = next(
            (e for e in wiki_entries if e.get("version", "") == "Normal"),
            next((e for e in wiki_entries if e.get("version", "") == ""), wiki_entries[0])
        )

        wiki_name = best.get("name", "???")
        wiki_version = best.get("version", "")

        # compare standard fields
        for c_field, json_path, desc in FIELD_MAP:
            our_val = fields.get(c_field)
            if our_val is None:
                continue
            wiki_val = get_json_value(best, json_path)
            if wiki_val is None:
                info_notes.append(
                    f"  {npc_name}: {c_field} ({desc}): ours={our_val}, wiki=MISSING"
                )
                continue
            if our_val != wiki_val:
                real_bugs.append({
                    "npc": npc_name, "npc_id": npc_id, "wiki_name": wiki_name,
                    "wiki_version": wiki_version,
                    "field": c_field, "desc": desc,
                    "ours": our_val, "wiki": wiki_val,
                })

        # ranged defence: compare against light/standard/heavy
        our_ranged_def = fields.get("ranged_def_bonus")
        if our_ranged_def is not None:
            ranged_vals = {}
            for rf in RANGED_DEF_FIELDS:
                v = get_json_value(best, ("defensive", rf))
                if v is not None:
                    ranged_vals[rf] = v

            if ranged_vals:
                # check if our single value matches any of them
                matches = [k for k, v in ranged_vals.items() if v == our_ranged_def]
                if not matches:
                    real_bugs.append({
                        "npc": npc_name, "npc_id": npc_id, "wiki_name": wiki_name,
                        "wiki_version": wiki_version,
                        "field": "ranged_def_bonus", "desc": "ranged defence",
                        "ours": our_ranged_def,
                        "wiki": f"light={ranged_vals.get('light','?')} standard={ranged_vals.get('standard','?')} heavy={ranged_vals.get('heavy','?')}",
                    })
                else:
                    info_notes.append(
                        f"  {npc_name}: ranged_def_bonus={our_ranged_def} matches wiki {matches}"
                        f" (light={ranged_vals.get('light','?')} standard={ranged_vals.get('standard','?')} heavy={ranged_vals.get('heavy','?')})"
                    )

        # max hit comparison
        wiki_max_hit_str = best.get("max_hit", "")
        if wiki_max_hit_str:
            try:
                # wiki max_hit can be "10" or "10+8" or "113" etc
                # take the first number for comparison
                wiki_max = int(re.match(r"(\d+)", str(wiki_max_hit_str)).group(1))
                our_cap = fields.get("max_hit_cap", 0)
                if our_cap > 0 and our_cap != wiki_max:
                    real_bugs.append({
                        "npc": npc_name, "npc_id": npc_id, "wiki_name": wiki_name,
                        "wiki_version": wiki_version,
                        "field": "max_hit_cap", "desc": "max hit cap vs wiki max hit",
                        "ours": our_cap, "wiki": wiki_max,
                    })
                info_notes.append(
                    f"  {npc_name}: wiki max_hit=\"{wiki_max_hit_str}\" "
                    f"(our max_hit_cap={our_cap if our_cap > 0 else 'none, computed from formula'})"
                )
            except (ValueError, AttributeError):
                info_notes.append(
                    f"  {npc_name}: wiki max_hit=\"{wiki_max_hit_str}\" (could not parse)"
                )

    # === PRINT REPORT ===
    print("=" * 70)
    print("REAL STAT DISCREPANCIES")
    print("=" * 70)
    if real_bugs:
        for e in real_bugs:
            v = f" (version: '{e['wiki_version']}')" if e["wiki_version"] else ""
            print(f"  {e['npc']} (id={e['npc_id']}, wiki: {e['wiki_name']}{v})")
            print(f"    {e['field']} ({e['desc']}): ours={e['ours']}, wiki={e['wiki']}")
            print()
    else:
        print("  (none found)\n")

    print("=" * 70)
    print("MISSING FROM WIKI JSON")
    print("=" * 70)
    if missing_npcs:
        for name, npc_id in missing_npcs:
            print(f"  {name} id={npc_id}")
    else:
        print("  (none)")
    print()

    print("=" * 70)
    print("INFO / NOTES")
    print("=" * 70)
    for note in info_notes:
        print(note)
    print()

    print("=" * 70)
    print("SUMMARY")
    print("=" * 70)
    print(f"  NPCs checked: {len(npc_names)}")
    print(f"  stat discrepancies: {len(real_bugs)}")
    print(f"  missing from wiki: {len(missing_npcs)}")

    if real_bugs or missing_npcs:
        sys.exit(1)


if __name__ == "__main__":
    main()
