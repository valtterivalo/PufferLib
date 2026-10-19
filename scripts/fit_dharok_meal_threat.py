#!/usr/bin/env python3
"""A Dharok fighter's low-hitpoint meal against a Voidwaker, RF_DHAROK_MEAL_VW_THREAT (riskfight_policy.h), calibrated in the sim against spectated fighters.

Usage: python3 scripts/fit_dharok_meal_threat.py [--duels FILE] [--policy riskfight_policy.h] [SIM.duels ...]      (python3 standard library only; deterministic)

Read on real and sim duels alike: Dharok v Dharok duels; for each fighter, the first hitsplat that leaves his fresh bar at 1-11 (the script's meal bands 0-1:
bar 1-5, 6-11) on a row without a third party; whether the other side holds a Voidwaker on that row (the weapon in hand); and whether an eat animation of his
shows on the next 2 rows. The script reads the Voidwaker in the other side's hand and raises its band's per-tick meal hazard to RF_DHAROK_MEAL_VW_THREAT[band]
per mille.

With SIM duels (ocean/osrs/tests/probe_script_duels.c, written with the table in --policy), each band's next exponent moves the current one by the ratio of
log(1 - real) to log(1 - sim) of the share shown against a Voidwaker: one step of a fixed-point iteration. The share against other weapons is printed for the
check that it stays where it was.
"""
import argparse
import collections
import math
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "scripts"))
from measure_dharok_play import parse, Side, BAR, NHIT, ANIMS, WEAPON_FIELD, VOIDWAKERS  # noqa: E402
from mine_riskfight_pool import DEFAULT_DUELS  # noqa: E402

DEFAULT_POLICY = os.path.join(ROOT, "ocean/osrs/encounters/riskfight/riskfight_policy.h")
EAT = 2
WINDOW = 2


def die(message):
    print(f"fit_dharok_meal_threat: {message}", file=sys.stderr)
    sys.exit(2)


def anims(field):
    return [] if field == "-" else [int(a) for a in field.split("|")]


def shares(paths):
    """(band, the other side holds a Voidwaker) -> [events, meals shown within WINDOW rows]."""
    vw = {str(v) for v in VOIDWAKERS}
    cells = collections.defaultdict(lambda: [0, 0])
    for path in paths:
        for d in parse(path):
            sides = (Side(d, 0), Side(d, 1))
            if not (sides[0].is_dharok() and sides[1].is_dharok()): continue
            rows = d["rows"]
            n = len(rows)
            for v in (0, 1):
                mine, his = [r[2 + v] for r in rows], [r[3 - v] for r in rows]
                for i in range(n):
                    bar = int(mine[i][BAR])
                    if int(mine[i][NHIT]) <= 0 or not 1 <= bar <= 11 or rows[i][1]: continue
                    c = cells[(bar // 6, his[i][WEAPON_FIELD] in vw)]
                    c[0] += 1
                    c[1] += any(mine[i + k][EAT] == "1" or 829 in anims(mine[i + k][ANIMS]) for k in range(1, WINDOW + 1) if i + k < n)
                    break
    return cells


def current_table(path):
    m = re.search(r"RF_DHAROK_MEAL_VW_THREAT\[2\] = \{(.*?)\};", open(path).read(), re.S)
    if not m: die(f"{path}: no RF_DHAROK_MEAL_VW_THREAT[2]")
    values = [int(v) for v in re.findall(r"\d+", m.group(1))]
    if len(values) != 2: die(f"{path}: RF_DHAROK_MEAL_VW_THREAT holds {len(values)} values, not 2")
    return values


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--duels", default=DEFAULT_DUELS)
    ap.add_argument("--policy", default=DEFAULT_POLICY)
    ap.add_argument("sim", nargs="*")
    args = ap.parse_args()
    real = shares([args.duels])
    sim = shares(args.sim) if args.sim else None
    table = current_table(args.policy)
    nxt = list(table)
    print(f"meal shown within {WINDOW} ticks of a hit to a fresh bar 1-11, real (events)" + (" / sim (events)" if sim else "") + "; against a Voidwaker | another weapon")
    for band in (0, 1):
        cols = []
        for held in (True, False):
            rn, rs = real[(band, held)]
            if not rn: die(f"no real event at band {band}, Voidwaker {held}")
            col = f"{rs / rn:.3f} ({rn})"
            if sim:
                sn, ss = sim[(band, held)]
                col += f" / {ss / sn:.3f} ({sn})" if sn else " / -"
                if held and sn and 0 < ss < sn:
                    nxt[band] = round(table[band] * math.log(1 - rs / rn) / math.log(1 - ss / sn))
            cols.append(col)
        print(f"  band {band} (bar {'1-5' if band == 0 else '6-11'}): " + " | ".join(cols) + (f" | exponent {table[band]} -> {nxt[band]}" if sim else ""))
    if sim:
        print(f"static const uint16_t RF_DHAROK_MEAL_VW_THREAT[2] = {{{nxt[0]}, {nxt[1]}}};")


if __name__ == "__main__":
    main()
