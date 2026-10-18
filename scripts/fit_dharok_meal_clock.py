#!/usr/bin/env python3
"""Whether a low spectated Dharok fighter eats before or after his next swing, by his own attack clock: riskfight_policy.h's RF_DHAROK_MEAL_CLOCK.

Usage: python3 scripts/fit_dharok_meal_clock.py [--duels FILE]      (python3 standard library only; deterministic)

Input: spectator.duels (see measure_dharok_play.py), Dharok v Dharok duels only, both sides, up to the first knockout; rows marked third-party skipped.

- Event: a hitsplat on him with damage that leaves his bar (a fresh read) at 1-11 of 30, hitpoints under 40.
- His own clock at t+1: ticks until his weapon is ready, counted from his last swing animation at its speed (fit_dharok_meals.py SWING_SPEED; instant maul
  specials leave it running), one tick a tick, plus 3 for every eat animation of his since (a meal adds its delay to a running clock; a meal on the due tick
  delays the swing by the full amount, FINDINGS 2026-10-19). 0: he may swing at t+1.
- Order: over t+1..t+8, his first eat animation before his first swing ("eat first"), against a swing first or no eat seen.

The fit: the script eats with a per-tick hazard h (RF_DHAROK_MEAL_PM, h0 = H_BASE in the bands under 40) on every tick until it swings. With his clock at
r at t+1, eating first means a meal on one of the r + 1 ticks with his clock at r, ..., 1, 0: P(eat first | r) = 1 - prod_{k <= r} (1 - h_k). Solving from
r = 0 up gives h_0, h_1, h_2 per clock tick; each is written as an exponent of the band's hazard, e_k = ln(1 - h_k) / ln(1 - H_BASE), per mille, so the band
and persona rates keep their shape. The exponent of the other ticks (clock 3 up, after his swing) makes up the rest of a 4-tick cycle's 4000: a fighter
on a 4-tick weapon eats as much per cycle as before, so "healed before the other side's next hit" (fit_dharok_meals.py's target) stays.
"""
import argparse
import collections
import math
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "scripts"))
from measure_dharok_play import parse, Side, BAR, DMG, NHIT, ANIMS, DEAD  # noqa: E402
from mine_riskfight_pool import DEFAULT_DUELS, SWING_ANIMS  # noqa: E402
from fit_dharok_meals import SWING_SPEED, INSTANT_ANIMS  # noqa: E402

EAT, W, FOOD_DELAY, LOW_BAR, CLOCKS = 2, 8, 3, 12, 3
H_BASE = 0.58  # RF_DHAROK_MEAL_PM bands 0-1 (589, 572)


def anims(field):
    return [] if field == "-" else [int(a) for a in field.split("|")]


def order_by_clock(path):
    """clock at t+1 (0, 1, 2, 3 up) -> [eat first, events, swing by t+2]"""
    out = collections.defaultdict(lambda: [0, 0, 0])
    for d in parse(path):
        if not (Side(d, 0).is_dharok() and Side(d, 1).is_dharok()):
            continue
        rows = d["rows"]
        n = len(rows)
        end = next((t for t in range(n) for s in (0, 1) if rows[t][2 + s][DEAD] == "1"), n)
        for s in (0, 1):
            mine = [r[2 + s] for r in rows]
            clock, clocks = None, []
            for t in range(n):
                if clock is not None:
                    clock -= 1
                    if mine[t][EAT] == "1": clock += FOOD_DELAY
                swings = [a for a in anims(mine[t][ANIMS]) if a in SWING_ANIMS and a not in INSTANT_ANIMS]
                if swings: clock = SWING_SPEED.get(swings[0], 4) - 1
                clocks.append(clock)
            for t in range(1, end - W):
                if int(mine[t][NHIT]) <= 0 or int(mine[t][DMG]) <= 0 or rows[t][1] or not 0 < int(mine[t][BAR]) < LOW_BAR or clocks[t] is None:
                    continue
                r = min(CLOCKS, max(0, clocks[t]))
                swing = next((u for u in range(t + 1, t + 1 + W) if any(a in SWING_ANIMS for a in anims(mine[u][ANIMS]))), None)
                eat = next((u for u in range(t + 1, t + 1 + W) if mine[u][EAT] == "1"), None)
                cell = out[r]
                cell[1] += 1
                cell[0] += eat is not None and (swing is None or eat < swing)
                cell[2] += swing is not None and swing <= t + 2
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--duels", default=DEFAULT_DUELS)
    args = ap.parse_args()
    cells = order_by_clock(args.duels)
    print("clock at t+1 | events | eat first | swing by t+2")
    for r in range(CLOCKS + 1):
        y, n, s2 = cells[r]
        print(f"{r if r < CLOCKS else str(r) + ' up':>5} | {n} | {y / n:.2f} | {s2 / n:.2f}")
    unfed, h = 1.0, []
    for r in range(CLOCKS):
        y, n, _ = cells[r]
        left = 1 - y / n
        h.append(min(0.999, max(0.0, 1 - left / unfed)))
        unfed *= 1 - h[-1]
    e = [round(1000 * math.log(1 - x) / math.log(1 - H_BASE)) for x in h]
    e.append(4000 - sum(e))
    print("per-tick hazard at clock 0, 1, 2: " + ", ".join(f"{x:.3f}" for x in h))
    print("RF_DHAROK_MEAL_CLOCK: {" + ", ".join(map(str, e)) + "}")


if __name__ == "__main__":
    main()
