#!/usr/bin/env python3
"""How spectated Dharok fighters attack after a hit leaves them low, against the sim's Dharok script (both seats on the script).

Usage: python3 scripts/measure_dharok_low_attack.py [--duels FILE] [SIM ...]      (python3 standard library only; deterministic)

Input: spectator.duels (see measure_dharok_play.py), Dharok v Dharok duels only, both sides, up to the first knockout. Optional SIM files are per-tick logs of
both seats on the Dharok script, one line per seat and tick: `T fight tick seat hp_before hp_after attack_timer_before swung weapon_class spec ate eat_actions
stop hit_damage` (weapon_class 1 axe, 2 Voidwaker, 3 maul, 0 other; hit_damage the hitsplat on that seat this tick).

An event is a hitsplat on him with damage and a bar (real: a fresh read, so no stale-bar estimate; sim: true hitpoints after the hit), by the band his
hitpoints fall in (0-19, 20-39, 40-59, 60 up). Over the next W ticks: whether he swings (any swing animation; sim: the step's swing), how soon, his axe swings
and Voidwaker specials, and the damage his swings put on the other side (the other's hitsplat on the swing tick or the next, each counted once) per tick.
HP between hitsplats is not used: an eat animation never shows on the eater's own swing tick, so a gap estimate reads a meal eaten with the swing as not eaten
and puts the swing in too low a band (that made low attackers look 1.8 times as dangerous per tick as the sim's).
"""
import argparse
import collections
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "scripts"))
from measure_dharok_play import parse, Side, BAR, DMG, NHIT, ANIMS, DEAD  # noqa: E402
from mine_riskfight_pool import DEFAULT_DUELS, SWING_ANIMS  # noqa: E402

W = 8
WEAPON = 1
AXES = {4718, 4886, 4887, 4888, 4889}
BANDS = ("0-19", "20-39", "40-59", "60+")


def anims(field):
    return [] if field == "-" else [int(a) for a in field.split("|")]


def band(hp):
    return min(3, int(hp) // 20)


def report(name, tally):
    print(f"{name}: next {W} ticks after a hit leaves him in the band")
    print("  band   events  P(swing)  P(swing by +2)  swings  axe swings  VW specs  damage/tick")
    for b in range(4):
        a = tally[b]
        n = max(1, a["n"])
        print(f"  {BANDS[b]:6s} {a['n']:6d}  {a['sw'] / n:.2f}      {a['early'] / n:.2f}            {a['ns'] / n:.2f}    {a['axe'] / n:.2f}"
              f"        {a['spec'] / n:.3f}     {a['dmg'] / n / W:.2f}")


def real(path):
    tally = collections.defaultdict(collections.Counter)
    for d in parse(path):
        if not (Side(d, 0).is_dharok() and Side(d, 1).is_dharok()):
            continue
        rows = d["rows"]
        n = len(rows)
        end = next((t for t in range(n) for s in (0, 1) if rows[t][2 + s][DEAD] == "1"), n)
        for s in (0, 1):
            mine, his = [r[2 + s] for r in rows], [r[3 - s] for r in rows]
            for t in range(1, end - W):
                if int(mine[t][NHIT]) <= 0 or int(mine[t][BAR]) < 0 or int(mine[t][DMG]) <= 0 or rows[t][1]:
                    continue
                hp = int(mine[t][BAR]) * 99 / 30
                if hp <= 0:
                    continue
                a = tally[band(hp)]
                a["n"] += 1
                used, first = set(), None
                for u in range(t + 1, t + 1 + W):
                    swings = [x for x in anims(mine[u][ANIMS]) if x in SWING_ANIMS]
                    if not swings:
                        continue
                    first = u if first is None else first
                    a["ns"] += 1
                    w, x = int(mine[u][WEAPON]), swings[0]
                    a["axe"] += x in (2066, 2067) or (w in AXES and x not in (11275, 1667))
                    a["spec"] += x == 11275
                    land = next((y for y in (u, u + 1) if y < n and y not in used and int(his[y][NHIT]) > 0), None)
                    if land is not None:
                        used.add(land)
                        a["dmg"] += int(his[land][DMG])
                a["sw"] += first is not None
                a["early"] += first is not None and first <= t + 2
    return tally


def sim(paths):
    tally = collections.defaultdict(collections.Counter)

    def fight(rows):
        for s in (0, 1):
            mine, his = rows[s], rows[1 - s]
            for t in sorted(mine):
                hp_after, dmg = mine[t][1], mine[t][9]
                if dmg <= 0 or hp_after <= 0 or t + W not in mine:
                    continue
                a = tally[band(hp_after)]
                a["n"] += 1
                used, first = set(), None
                for u in range(t + 1, t + 1 + W):
                    _, _, _, swung, wc, spec, _, _, _, _ = mine[u]
                    if not swung:
                        continue
                    first = u if first is None else first
                    a["ns"] += 1
                    a["axe"] += wc == 1
                    a["spec"] += wc == 2 and spec
                    land = next((y for y in (u, u + 1) if y in his and y not in used and his[y][9] > 0), None)
                    if land is not None:
                        used.add(land)
                        a["dmg"] += his[land][9]
                a["sw"] += first is not None
                a["early"] += first is not None and first <= t + 2

    for path in paths:
        rows, current = ({}, {}), None
        for line in open(path):
            x = line.split()
            if x[0] != "T":
                continue
            f, tick, seat = int(x[1]), int(x[2]), int(x[3])
            if f != current:
                fight(rows)
                rows, current = ({}, {}), f
            rows[seat][tick] = tuple(map(int, x[4:]))
        fight(rows)
    return tally


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--duels", default=DEFAULT_DUELS)
    ap.add_argument("sim", nargs="*")
    args = ap.parse_args()
    report("REAL", real(args.duels))
    if args.sim:
        report("SIM", sim(args.sim))


if __name__ == "__main__":
    main()
