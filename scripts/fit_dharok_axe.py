#!/usr/bin/env python3
"""The Dharok script's greataxe per swing, RF_AXE_SWING_PM (riskfight_policy.h), calibrated in the sim against spectated Dharok fighters.

Usage: python3 scripts/fit_dharok_axe.py [--duels FILE] [--policy riskfight_policy.h] [SIM.duels ...]      (python3 standard library only; deterministic)

A fighter knows his own hitpoints; he sees the other side's bar. The table is keyed on both: his hitpoints 0-19, 20-39, 40-79, 80 up, his Vengeance up (a cast
seen, not yet proc'd), the other side's bar the tick before (scale 30: <12, 12-23, 24 up, about <40, 40-79, 80 up). The share read is the axe's among his
swings without a special (measure_dharok_play.py's swings and axe animations).

His hitpoints come from a fresh read: his bar at a hitsplat on him in the FRESH_TICKS ticks before the swing, with no meal of his shown since. A gap estimate
(fit_dharok_tab.gap_hitpoints) reads a meal hidden on his swing tick as not eaten and spreads high swings into the low bands; on the sim's duels the fresh read
gives the same table as his true hitpoints at the decision (the probe's truth) within a few hundredths, the gap estimate does not.

With SIM duels (ocean/osrs/tests/probe_script_duels.c, the script on both seats, written with the table in --policy), each cell's next value moves the
current one by the odds ratio of the real share to the sim's: one step of a fixed-point iteration, run until the sim reads as real. A cell with under
POOL_MIN real swings takes its Vengeance pair's pooled share, and if the pair is still under POOL_MIN, the pooled share of both Vengeance cells of it and
of the next own band inward (0-19 with 20-39, 80+ with 40-79); a cell with under SIM_MIN sim swings keeps its value. Without SIM the real table is printed,
the iteration's start.
"""
import argparse
import collections
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "scripts"))
from measure_dharok_play import parse, Side, BAR, NHIT, ANIMS, SPEC, DH_SWINGS  # noqa: E402
from mine_riskfight_pool import DEFAULT_DUELS, SWING_ANIMS  # noqa: E402

FRESH_TICKS = 3
EAT = 2
POOL_MIN = 60
SIM_MIN = 30
OWN_EDGES = (20, 40, 80)
OWN_NAMES = ("hp 0-19", "hp 20-39", "hp 40-79", "hp 80+")
BAR_NAMES = ("bar <12", "bar 12-23", "bar 24+")
DEFAULT_POLICY = os.path.join(ROOT, "ocean/osrs/encounters/riskfight/riskfight_policy.h")


def die(message):
    print(f"fit_dharok_axe: {message}", file=sys.stderr)
    sys.exit(2)


def anims(field):
    return [] if field == "-" else [int(a) for a in field.split("|")]


def fresh_hitpoints(mine, i):
    """His bar at the latest hitsplat on him in the FRESH_TICKS ticks before row i, in hitpoints, if no meal of his showed since; else None."""
    for j in range(i - 1, max(-1, i - 1 - FRESH_TICKS), -1):
        if mine[j][EAT] == "1" or 829 in anims(mine[j][ANIMS]): return None
        if int(mine[j][NHIT]) > 0 and int(mine[j][BAR]) >= 0: return int(mine[j][BAR]) * 99 / 30
    return None


def grid(paths):
    """(own band, veng up, his bar band) -> [swings, axe swings], swings without a special of Dharok fighters of Dharok v Dharok duels at a fresh read."""
    cells = collections.defaultdict(lambda: [0, 0])
    for path in paths:
        for d in parse(path):
            sides = (Side(d, 0), Side(d, 1))
            if not (sides[0].is_dharok() and sides[1].is_dharok()): continue
            for x in (0, 1):
                me, him = sides[x], sides[1 - x]
                mine = [r[2 + x] for r in d["rows"]]
                for i in range(1, len(mine)):
                    a = anims(mine[i][ANIMS])
                    if me.third[i] or mine[i][SPEC] != "-" or not any(v in SWING_ANIMS for v in a): continue
                    hp = fresh_hitpoints(mine, i)
                    if hp is None: continue
                    bar = him.bar[i - 1]
                    c = cells[(sum(hp >= e for e in OWN_EDGES), int(me.veng_up[i - 1]), (bar >= 12) + (bar >= 24))]
                    c[0] += 1
                    c[1] += any(v in DH_SWINGS for v in a)
    return cells


def pooled_shares(cells, minimum):
    out = {}
    for o in range(4):
        inward = 1 if o == 0 else 2 if o == 3 else o
        for q in range(3):
            pair = [cells[(o, v, q)] for v in (0, 1)]
            for v in (0, 1):
                if min(p[0] for p in pair) >= minimum: n, a = pair[v]
                elif sum(p[0] for p in pair) >= minimum or inward == o: n, a = sum(p[0] for p in pair), sum(p[1] for p in pair)
                else:
                    group = pair + [cells[(inward, w, q)] for w in (0, 1)]
                    n, a = sum(p[0] for p in group), sum(p[1] for p in group)
                out[(o, v, q)] = (a / n if n else None, n)
    return out


def current_table(path):
    text = open(path).read()
    m = re.search(r"RF_AXE_SWING_PM\[4\]\[2\]\[3\] = \{(.*?)\};", text, re.S)
    if not m: die(f"{path}: no RF_AXE_SWING_PM[4][2][3]")
    values = [int(v) for v in re.findall(r"-?\d+", m.group(1))]
    if len(values) != 24: die(f"{path}: RF_AXE_SWING_PM holds {len(values)} values, not 24")
    return {(o, v, q): values[o * 6 + v * 3 + q] for o in range(4) for v in range(2) for q in range(3)}


def odds(p):
    p = min(max(p, 0.001), 0.999)
    return p / (1 - p)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--duels", default=DEFAULT_DUELS)
    ap.add_argument("--policy", default=DEFAULT_POLICY)
    ap.add_argument("sim", nargs="*")
    args = ap.parse_args()
    real = pooled_shares(grid([args.duels]), POOL_MIN)
    sim = pooled_shares(grid(args.sim), SIM_MIN) if args.sim else None
    table = current_table(args.policy) if args.sim else None
    print("axe share of swings without a special at a fresh read, real (swings)" + (" / sim (swings) / table now -> next" if sim else ""))
    nxt = {}
    for o in range(4):
        for v in range(2):
            cols = []
            for q in range(3):
                r, rn = real[(o, v, q)]
                if r is None: die(f"no real swing in {OWN_NAMES[o]}, veng {v}, {BAR_NAMES[q]}")
                if sim is None:
                    nxt[(o, v, q)] = round(1000 * r)
                    cols.append(f"{r:.3f} ({rn})")
                    continue
                s, sn = sim[(o, v, q)]
                t = table[(o, v, q)]
                if s is None or sn < SIM_MIN or t <= 0 or t >= 1000: nxt[(o, v, q)] = t
                else:
                    ot = odds(t / 1000) * odds(r) / odds(s)
                    nxt[(o, v, q)] = round(1000 * ot / (1 + ot))
                cols.append(f"{r:.3f} ({rn}) / {s:.3f} ({sn}) / {t} -> {nxt[(o, v, q)]}" if s is not None else f"{r:.3f} ({rn}) / - / {t}")
            print(f"  {OWN_NAMES[o]:9s} veng {v}: " + " | ".join(cols))
    print("static const int16_t RF_AXE_SWING_PM[4][2][3] = {")
    for o in range(4):
        print("    {" + ", ".join("{" + ", ".join(str(nxt[(o, v, q)]) for q in range(3)) + "}" for v in range(2)) + "},")
    print("};")


if __name__ == "__main__":
    main()
