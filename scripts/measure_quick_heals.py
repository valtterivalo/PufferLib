#!/usr/bin/env python3
"""How hard a low Dharok fighter is to finish, read the same way on spectated and sim duels (riskfight_policy.h's RF_DHAROK_MEAL_LOW_GAIN).

Usage: python3 scripts/measure_quick_heals.py [--duels FILE] [SIM.duels ...]      (python3 standard library only; deterministic)

Dharok v Dharok duels, rows without a third party:
  quick heals  of two consecutive hitsplats on a fighter g = 1..4 ticks apart, the first leaving his bar at 1-16, what he healed in between: (bar after the
               second - bar after the first) x 99 / 30 + the second's damage. Shares of 8, 20 and 40 or more.
  specials     Voidwaker specials by the victim's bar the tick before (carried), and the share followed by his death within 2 ticks.
  knockouts    the share of duels that end in a death.
With SIM files, the same lines for them (a sim file's fighters are all counted: write them with the script on both seats, ocean/osrs/tests/probe_script_duels.c).
"""
import argparse
import collections
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "scripts"))
from measure_dharok_play import parse, Side, NHIT, DMG, BAR, SPEC, DEAD  # noqa: E402
from mine_riskfight_pool import DEFAULT_DUELS  # noqa: E402

SPEC_BARS = [(0, 6), (7, 11), (12, 16), (17, 22), (23, 30)]


def measure(path):
    heals = collections.defaultdict(list)
    specs = collections.defaultdict(lambda: [0, 0])
    duels = kos = 0
    for d in parse(path):
        sides = (Side(d, 0), Side(d, 1))
        if not (sides[0].is_dharok() and sides[1].is_dharok()):
            continue
        rows = d["rows"]
        n = len(rows)
        duels += 1
        kos += any(r[2][DEAD] == "1" or r[3][DEAD] == "1" for r in rows)
        for v in (0, 1):
            hits = [i for i in range(n) if int(rows[i][2 + v][NHIT]) > 0 and int(rows[i][2 + v][BAR]) >= 0 and not rows[i][1]]
            for x, y in zip(hits, hits[1:]):
                gap = y - x
                first = int(rows[x][2 + v][BAR])
                if gap <= 4 and 1 <= first <= 16:
                    heals[gap].append((int(rows[y][2 + v][BAR]) - first) * 99 / 30 + int(rows[y][2 + v][DMG]))
            a = 1 - v
            for i in range(1, n):
                if rows[i][2 + a][SPEC] != "VW" or rows[i][1]:
                    continue
                bar = sides[v].bar[i - 1]
                cell = specs[next(k for k, (lo, hi) in enumerate(SPEC_BARS) if lo <= bar <= hi)]
                cell[0] += 1
                cell[1] += any(rows[t][2 + v][DEAD] == "1" for t in range(i, min(n, i + 3)))
    return heals, specs, duels, kos


def report(name, path):
    heals, specs, duels, kos = measure(path)
    print(f"{name}: {duels} Dharok v Dharok duels, {kos / max(1, duels):.3f} end in a knockout")
    for g in (1, 2, 3, 4):
        xs = heals[g]
        if xs:
            print(f"  quick heals, gap {g}: n {len(xs)}, healed >= 8 {sum(x >= 8 for x in xs) / len(xs):.2f}, >= 20 {sum(x >= 20 for x in xs) / len(xs):.2f}, "
                  f">= 40 {sum(x >= 40 for x in xs) / len(xs):.2f}")
    print("  Voidwaker specials by his bar: " + " | ".join(f"{lo}-{hi}: n {specs[k][0]}, knockout {specs[k][1] / max(1, specs[k][0]):.3f}"
                                                       for k, (lo, hi) in enumerate(SPEC_BARS)))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--duels", default=DEFAULT_DUELS)
    ap.add_argument("sim", nargs="*")
    args = ap.parse_args()
    report("spectated", args.duels)
    for path in args.sim:
        report(os.path.basename(path), path)


if __name__ == "__main__":
    main()
