"""Cross-reference osrs_items.h against canonical OSRS wiki equipment.json.

Accounts for known convention differences:
- magic_damage: our code stores whole % (5 = 5%), wiki JSON stores tenths (50 = 5.0%)
- attack_speed for ranged weapons: our code stores rapid-style speed (wiki - 1)
- attack_speed for non-powered staves: our code stores autocast speed (5 ticks)
- attack_speed for ammo: our code stores 0 (speed comes from weapon)
"""

import json
import re
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
ITEMS_H = REPO / "pufferlib" / "ocean" / "osrs" / "osrs_items.h"
EQUIPMENT_JSON = REPO / ".refs" / "osrs-dps-calc" / "cdn" / "json" / "equipment.json"

FIELD_MAP = [
    ("attack_stab",    ("offensive", "stab")),
    ("attack_slash",   ("offensive", "slash")),
    ("attack_crush",   ("offensive", "crush")),
    ("attack_magic",   ("offensive", "magic")),
    ("attack_ranged",  ("offensive", "ranged")),
    ("defence_stab",   ("defensive", "stab")),
    ("defence_slash",  ("defensive", "slash")),
    ("defence_crush",  ("defensive", "crush")),
    ("defence_magic",  ("defensive", "magic")),
    ("defence_ranged", ("defensive", "ranged")),
    ("melee_strength", ("bonuses", "str")),
    ("ranged_strength",("bonuses", "ranged_str")),
    ("magic_damage",   ("bonuses", "magic_str")),
    ("prayer",         ("bonuses", "prayer")),
    ("attack_speed",   ("speed",)),
]

# ammo slots store speed=0 because speed comes from weapon
AMMO_ENUM_NAMES = {
    "ITEM_DIAMOND_BOLTS_E", "ITEM_OPAL_DRAGON_BOLTS", "ITEM_DRAGON_ARROWS",
    "ITEM_AMETHYST_ARROW", "ITEM_DRAGON_DART", "ITEM_DIZANAS_QUIVER",
    "ITEM_GOD_BLESSING",
}

# powered staves use their own speed (no autocast override)
POWERED_STAVES = {
    "ITEM_SANGUINESTI_STAFF", "ITEM_TRIDENT_OF_SWAMP", "ITEM_TOXIC_BLOWPIPE",
}

# non-powered staves that autocast (store 5-tick autocast speed, not wiki melee speed)
AUTOCAST_STAVES = {
    "ITEM_AHRIM_STAFF", "ITEM_STAFF_OF_DEAD", "ITEM_KODAI_WAND",
    "ITEM_VOLATILE_STAFF", "ITEM_ZURIELS_STAFF",
}

# ranged weapons use rapid style (wiki speed - 1)
RANGED_WEAPON_SLOTS = {"ITEM_RUNE_CROSSBOW", "ITEM_ARMADYL_CROSSBOW",
    "ITEM_ZARYTE_CROSSBOW", "ITEM_DARK_BOW", "ITEM_HEAVY_BALLISTA",
    "ITEM_MORRIGANS_JAVELIN", "ITEM_TWISTED_BOW", "ITEM_BOW_OF_FAERDHINEN",
    "ITEM_MAGIC_SHORTBOW_I", "ITEM_TOXIC_BLOWPIPE",
}


def parse_items_h(path):
    """Parse all item entries from osrs_items.h C source."""
    text = Path(path).read_text()
    pattern = re.compile(r"\[(\w+)\]\s*=\s*\{([^}]+)\}", re.DOTALL)

    items = []
    for m in pattern.finditer(text):
        enum_name = m.group(1)
        body = m.group(2)

        # skip non-item entries (SLOT_*, MAX_ITEMS_*, etc)
        if not enum_name.startswith("ITEM_"):
            continue

        fields = {}
        for fm in re.finditer(r"\.(\w+)\s*=\s*(-?\d+)", body):
            fields[fm.group(1)] = int(fm.group(2))

        name_match = re.search(r'\.name\s*=\s*"([^"]+)"', body)
        name = name_match.group(1) if name_match else enum_name

        items.append({"enum_name": enum_name, "name": name, "fields": fields})
    return items


def build_equipment_lookup(path):
    """Build item_id -> list of equipment entries from wiki JSON."""
    with open(path) as f:
        data = json.load(f)
    lookup = {}
    for entry in data:
        lookup.setdefault(entry["id"], []).append(entry)
    return lookup


def get_json_value(entry, json_path):
    """Navigate nested JSON path to get a stat value."""
    current = entry
    for key in json_path:
        if isinstance(current, dict) and key in current:
            current = current[key]
        else:
            return None
    return int(current) if isinstance(current, (int, float)) else None


def main():
    items = parse_items_h(ITEMS_H)
    wiki = build_equipment_lookup(EQUIPMENT_JSON)

    print(f"parsed {len(items)} items from osrs_items.h")
    print(f"loaded {len(wiki)} unique item IDs from equipment.json\n")

    real_bugs = []
    convention_diffs = []
    missing_items = []

    for item in items:
        fields = item["fields"]
        item_id = fields.get("item_id")
        enum_name = item["enum_name"]

        if item_id is None:
            continue
        if item_id not in wiki:
            missing_items.append(item)
            continue

        wiki_entries = wiki[item_id]
        best = next((e for e in wiki_entries if e.get("version", "") == ""), wiki_entries[0])

        for c_field, json_path in FIELD_MAP:
            our_val = fields.get(c_field)
            if our_val is None:
                continue
            wiki_val = get_json_value(best, json_path)
            if wiki_val is None or our_val == wiki_val:
                continue

            # classify: convention difference or real bug?
            entry = {
                "item": item, "field": c_field, "ours": our_val,
                "wiki": wiki_val, "wiki_name": best.get("name", "???"),
                "wiki_version": best.get("version", ""),
            }

            if c_field == "magic_damage" and our_val * 10 == wiki_val:
                entry["reason"] = "10x units (our % vs wiki tenths-of-%)"
                convention_diffs.append(entry)
            elif c_field == "attack_speed" and enum_name in AMMO_ENUM_NAMES:
                entry["reason"] = "ammo stores 0, speed comes from weapon"
                convention_diffs.append(entry)
            elif c_field == "attack_speed" and enum_name in RANGED_WEAPON_SLOTS and our_val == wiki_val - 1:
                entry["reason"] = "rapid-style speed (wiki - 1)"
                convention_diffs.append(entry)
            elif c_field == "attack_speed" and enum_name in AUTOCAST_STAVES and our_val == 5:
                entry["reason"] = "autocast speed override (5 ticks for non-powered staves)"
                convention_diffs.append(entry)
            else:
                real_bugs.append(entry)

    # === PRINT REPORT ===
    print("=" * 70)
    print("REAL STAT BUGS (need fixing)")
    print("=" * 70)
    if real_bugs:
        for e in real_bugs:
            v = f" (version: '{e['wiki_version']}')" if e["wiki_version"] else ""
            print(f"  {e['item']['enum_name']} ({e['item']['name']}) id={e['item']['fields']['item_id']}")
            print(f"    wiki: {e['wiki_name']}{v}")
            print(f"    {e['field']}: ours={e['ours']}, wiki={e['wiki']}")
            print()
    else:
        print("  (none found)\n")

    print("=" * 70)
    print("MISSING FROM WIKI JSON (may be PvP/deadman items)")
    print("=" * 70)
    for item in missing_items:
        print(f"  {item['enum_name']} ({item['name']}) id={item['fields']['item_id']}")
    print()

    print("=" * 70)
    print(f"CONVENTION DIFFERENCES ({len(convention_diffs)} total, not bugs)")
    print("=" * 70)
    by_reason = {}
    for e in convention_diffs:
        by_reason.setdefault(e["reason"], []).append(e)
    for reason, entries in by_reason.items():
        print(f"\n  [{reason}] ({len(entries)} items)")
        for e in entries:
            print(f"    {e['item']['enum_name']}: {e['field']} ours={e['ours']} wiki={e['wiki']}")

    print("\n" + "=" * 70)
    print("SUMMARY")
    print("=" * 70)
    print(f"  items checked: {len(items)}")
    print(f"  real bugs: {len(real_bugs)}")
    print(f"  missing from wiki: {len(missing_items)}")
    print(f"  convention diffs: {len(convention_diffs)}")

    if real_bugs or missing_items:
        sys.exit(1)


if __name__ == "__main__":
    main()
