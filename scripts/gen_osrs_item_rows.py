#!/usr/bin/env python3
"""Regenerates ocean/osrs/osrs_items_wiki_rows.h from ocean/osrs/items/wiki_item_rows.tsv.

Adding an item the sim does not know is three steps:
  1. Append one row to ocean/osrs/items/wiki_item_rows.tsv: the stats from the item's OSRS Wiki
     page (Combat stats infobox) and the wiki URL. Ids that share the same stats (charged and
     ornament variants) go in the `aliases` column of the one row; a variant of a HAND row
     (ocean/osrs/osrs_items_generated.h) goes in OSRS_ITEM_RAW_GEAR_ALIAS_ROWS in osrs_items.h.
  2. python3 scripts/gen_osrs_item_rows.py
  3. Regenerate the observation table (the new row gets the next content code, no frozen code moves):
       cc -std=c11 -O2 -I. -o /tmp/gen_osrs_item_obs ocean/osrs/tools/gen_osrs_item_obs_table.c -lm
       /tmp/gen_osrs_item_obs ocean/osrs/osrs_item_obs_generated.h ocean/osrs/osrs_item_obs_table.inc
     and run ocean/osrs/tests/test_wiki_item_rows.c.
Two budgets bound the table, both enforced below or by the observation generator: the item index is a
uint16, but rows, stat slots and the riskfight one-byte code share 0..253: the first BYTE_WIKI_ROWS rows go below the stat slots, the rest
from index 256; the content code space is OSRS_ITEM_OBS_CODE_SCALE
(512, GEN_OBS_CODE_SCALE of the observation generator). A row with a special attack the sim does not model keeps no special: spec costs and
osrs_resolve_spec stay a hand job in osrs_special_attacks.h.

Usage: scripts/gen_osrs_item_rows.py [--check]   (--check fails when the header is stale)
"""
import csv
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TSV = os.path.join(ROOT, "ocean/osrs/items/wiki_item_rows.tsv")
OUT = os.path.join(ROOT, "ocean/osrs/osrs_items_wiki_rows.h")
HAND_ITEMS = 163          # rows of osrs_items_generated.h written by hand; wiki rows are appended after them
STAT_SLOTS_MIN = 40       # stat-priced run-time rows kept free (osrs_items.h OSRS_STAT_ITEM_SLOTS)
BYTE_WIKI_ROWS = 254 - HAND_ITEMS - STAT_SLOTS_MIN  # wiki rows below the stat slots: indices 0..253 keep their one-byte code; later rows start at 256
NAME_MAX = 31             # Item.name is char[32]
SLOTS = {"head", "cape", "neck", "weapon", "body", "shield", "legs", "hands", "feet", "ring", "ammo"}
STYLES = {"none": 0, "melee": 1, "ranged": 2, "magic": 3}     # AttackStyle of get_item_attack_style
STATS = ["attack_stab", "attack_slash", "attack_crush", "attack_magic", "attack_ranged",
         "defence_stab", "defence_slash", "defence_crush", "defence_magic", "defence_ranged",
         "melee_strength", "ranged_strength", "magic_damage", "prayer"]
COLUMNS = ["raw_id", "enum", "name", "slot", "two_handed", "attack_speed", "attack_range", "style"] + STATS + ["aliases", "wiki_url", "notes"]


def fail(line, message):
    sys.exit(f"{TSV}:{line}: {message}")


def read_rows():
    with open(TSV, newline="") as fh:
        lines = [(n, l) for n, l in enumerate(fh, 1) if l.strip() and not l.startswith("#")]
    header = lines[0][1].rstrip("\n").split("\t")
    if header != COLUMNS:
        fail(lines[0][0], f"header must be {COLUMNS}")
    rows, raw_seen, enum_seen = [], {}, set()
    for n, line in lines[1:]:
        f = line.rstrip("\n").split("\t")
        if len(f) != len(COLUMNS):
            fail(n, f"{len(f)} fields, expected {len(COLUMNS)}")
        r = dict(zip(COLUMNS, f))
        if not re.fullmatch(r"ITEM_[A-Z0-9_]+", r["enum"]) or r["enum"] in enum_seen:
            fail(n, f"enum {r['enum']!r} is not a fresh ITEM_ identifier")
        enum_seen.add(r["enum"])
        if len(r["name"]) > NAME_MAX or '"' in r["name"] or "\\" in r["name"]:
            fail(n, f"name {r['name']!r}: at most {NAME_MAX} characters, no quote or backslash")
        if r["slot"] not in SLOTS:
            fail(n, f"slot {r['slot']!r} is not one of {sorted(SLOTS)}")
        if r["style"] not in STYLES:
            fail(n, f"style {r['style']!r} is not one of {sorted(STYLES)}")
        if r["two_handed"] not in ("0", "1") or (r["two_handed"] == "1" and r["slot"] != "weapon"):
            fail(n, "two_handed is 0/1 and only a weapon is two-handed")
        ids = [r["raw_id"]] + [a for a in r["aliases"].split(",") if a]
        for i in ids:
            if not i.isdigit() or not 0 < int(i) < 65536:
                fail(n, f"raw id {i!r} is not a uint16")
            if i in raw_seen:
                fail(n, f"raw id {i} already used by row at line {raw_seen[i]}")
            raw_seen[i] = n
        weapon = r["slot"] == "weapon"
        if weapon != (r["style"] != "none") or weapon != (int(r["attack_speed"]) > 0) or (not weapon and int(r["attack_range"]) != 0):
            fail(n, "a weapon has a style, an attack speed and a range; any other slot has none of them")
        if not r["wiki_url"].startswith("https://oldschool.runescape.wiki/w/"):
            fail(n, "wiki_url must be an oldschool.runescape.wiki page")
        for k in ["two_handed", "attack_speed", "attack_range"] + STATS:
            try:
                v = int(r[k])
            except ValueError:
                fail(n, f"{k}={r[k]!r} is not an integer")
            if not -32768 <= v <= 32767:
                fail(n, f"{k}={v} does not fit int16")
        rows.append(r)
    return rows


def render(rows):
    byte_rows, ext_rows = rows[:BYTE_WIKI_ROWS], rows[BYTE_WIKI_ROWS:]
    out = ["/* Generated by scripts/gen_osrs_item_rows.py from ocean/osrs/items/wiki_item_rows.tsv. Do not edit. */",
           "#ifndef OSRS_ITEMS_WIKI_ROWS_H", "#define OSRS_ITEMS_WIKI_ROWS_H", "",
           f"#define OSRS_ITEM_WIKI_ROW_COUNT {len(byte_rows)}",
           f"#define OSRS_ITEM_WIKI_EXT_ROW_COUNT {len(ext_rows)}", "",
           "/* X(enum, raw_id, name, slot, two_handed, speed, range, attack_style, attack stab/slash/crush/magic/ranged, defence stab/slash/crush/magic/ranged,",
           " *   melee_strength, ranged_strength, magic_damage, prayer); attack_style: 0 none, 1 melee, 2 ranged, 3 magic (get_item_attack_style).",
           " * OSRS_ITEM_WIKI_ROWS fill the indices below the stat-priced slots, OSRS_ITEM_WIKI_EXT_ROWS start at OSRS_ITEM_EXT_BASE (256), above the",
           " * one-byte item code of the riskfight observations. */"]
    for macro, chunk in (("OSRS_ITEM_WIKI_ROWS", byte_rows), ("OSRS_ITEM_WIKI_EXT_ROWS", ext_rows)):
        out.append(f"#define {macro}(X) \\")
        for r in chunk:
            stats = ", ".join(r[k] for k in STATS)
            out.append(f'    X({r["enum"]}, {r["raw_id"]}, "{r["name"]}", SLOT_{r["slot"].upper()}, {r["two_handed"]}, {r["attack_speed"]}, '
                       f'{r["attack_range"]}, {STYLES[r["style"]]}, {stats}) \\')
        out.append("")
    out += ["/* X(raw_id, enum): ids that share the stats of a wiki row (charged and ornament variants). */",
            "#define OSRS_ITEM_WIKI_ALIAS_ROWS(X) \\"]
    for r in rows:
        for a in filter(None, r["aliases"].split(",")):
            out.append(f'    X({a}, {r["enum"]}) \\')
    out += ["", "#endif", ""]
    return "\n".join(out)


def main():
    text = render(read_rows())
    if "--check" in sys.argv:
        with open(OUT) as fh:
            if fh.read() != text:
                sys.exit(f"{OUT} is stale: run scripts/gen_osrs_item_rows.py")
        return
    with open(OUT, "w") as fh:
        fh.write(text)
    print(f"{OUT}: {text.count('    X(ITEM_')} rows")


if __name__ == "__main__":
    main()
