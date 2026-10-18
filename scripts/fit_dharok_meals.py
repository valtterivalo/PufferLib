#!/usr/bin/env python3
"""When spectated Dharok fighters eat, on their own hitpoints and the other side's last hit on them: riskfight_policy.h's RF_DHAROK_MEAL_PM.

Usage: python3 scripts/fit_dharok_meals.py [--duels FILE]      (python3 standard library only; deterministic)

Input: spectator.duels (see measure_dharok_play.py). Only Dharok v Dharok duels; both sides count.

The spectated bar moves only with a hitsplat: of the eat animations below 60 hitpoints none shows the bar rising before his next hitsplat (the heal shows on
that hit, bar plus its damage). So the fit reads meals by the heal the next hitsplat shows, on current status, which needs no visibility model:

- A gap: a hit of the other side leaves him on a fresh read a (a hitsplat on him with a bar, the other side's swing animation on that tick or the one before),
  and his next such read is b. L is the other side's clock between the two swings (capped at STATUS_L). He was healed by b when bar_b + damage_b is
  HEALED_MIN or more over bar_a (hitpoints, bar * 99 / 30).
- With a meal per tick at rate h, P(healed by L) = 1 - (1 - h)^L; h is the maximum likelihood fit (a grid of 1/1000) per cell. A constant h fits the seen
  P(healed by L) within its noise at every L (printed), so the table is flat over the other side's attack clock.
- Cells: bands bar 0-5, 6-11, 12-17, 18-20, 21-23, 24 up of 30 on bar_a (riskfight_dharok_meal_band), and whether the hit that opened the gap did damage or 0
  (a miss; RF_SCRIPT_LAST_HIT_ZERO). After a miss he eats much less at 40-79 than after damage. A miss cell read on fewer than ZERO_MIN gaps takes its band's
  damage rate (below 40 hitpoints few misses are read).
- From 80 up the eat animations are nearly all potions: of the gaps between his hitsplats with one animation, 0.95 show no heal.
"""
import argparse
import collections
import math
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "scripts"))
from measure_dharok_play import parse, Side, BAR, DMG, NHIT, ANIMS  # noqa: E402
from mine_riskfight_pool import DEFAULT_DUELS, SWING_ANIMS  # noqa: E402

SWING_SPEED = {1658: 4, 11275: 4, 7045: 6, 426: 4, 10172: 5, 10173: 5, 440: 7, 2066: 7, 2067: 7, 7514: 4, 7644: 6, 376: 4, 1062: 4, 4230: 4, 5865: 4, 5866: 4, 386: 4}
INSTANT_ANIMS = {1667}
BANDS = 6
BAND_BARS = (0, 6, 12, 18, 21, 24)  # the bar each band starts at
STATUS_L, HEALED_MIN = 11, 8  # L cap; heal that counts as a meal
ZERO_MIN = 100  # gaps under which a miss cell takes its band's damage rate


def band_of(hp):
    bar = int(hp * 30 / 99)
    return max(b for b in range(BANDS) if bar >= BAND_BARS[b])


def anims(field):
    return [] if field == "-" else [int(a) for a in field.split("|")]


def status_loglik(rows, h):
    return sum(y * math.log(1 - (1 - h) ** L) + (n - y) * L * math.log(1 - h) for L, (y, n) in rows)


def current_status(duels):
    """(miss, band, L) -> [healed, gaps]: see the docstring. miss: the opening hit did 0."""
    out = collections.defaultdict(lambda: [0, 0])
    for d in duels:
        if not (Side(d, 0).is_dharok() and Side(d, 1).is_dharok()):
            continue
        rows_all = d["rows"]
        for side in (0, 1):
            mine, his = [r[2 + side] for r in rows_all], [r[3 - side] for r in rows_all]
            swing = [any(x in SWING_ANIMS for x in anims(r[ANIMS])) for r in his]
            fresh = [t for t in range(1, len(mine)) if int(mine[t][NHIT]) > 0 and int(mine[t][BAR]) >= 0 and not rows_all[t][1]]
            for a, b in zip(fresh, fresh[1:]):
                if mine[a][11] == "1": continue
                sa = a if swing[a] else a - 1 if swing[a - 1] else None
                sb = b if swing[b] else b - 1 if swing[b - 1] else None
                hp = int(mine[a][BAR]) * 99 / 30
                if sa is None or sb is None or sb <= sa or hp <= 0: continue
                cell = out[(int(int(mine[a][DMG]) <= 0), band_of(hp), min(sb - sa, STATUS_L))]
                cell[0] += int(mine[b][BAR]) * 99 / 30 + int(mine[b][DMG]) - hp >= HEALED_MIN
                cell[1] += 1
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--duels", default=DEFAULT_DUELS)
    args = ap.parse_args()
    status = current_status(parse(args.duels))
    table = [[0, 0] for _ in range(BANDS)]
    for miss in (0, 1):
        for b in range(BANDS):
            rows = [(L, status[(miss, b, L)]) for L in range(1, STATUS_L + 1) if status[(miss, b, L)][1]]
            n = sum(c for _, (_, c) in rows)
            if miss and n < ZERO_MIN:
                table[b][1] = table[b][0]
                print(f"band {b} after a miss: n {n} < {ZERO_MIN}, takes the damage rate {table[b][0]}")
                continue
            h = max(range(1, 1000), key=lambda i: status_loglik(rows, i / 1000)) / 1000
            table[b][miss] = round(1000 * h)
            print(f"band {b} after {'a miss' if miss else 'damage'}: n {n}, h {1000 * h:.0f} per mille; P(healed by L) seen / fitted: " +
                  " ".join(f"{L}: {y / c:.2f} / {1 - (1 - h) ** L:.2f} ({c})" for L, (y, c) in rows if L <= 7))
    print("RF_DHAROK_MEAL_PM ({after damage, after a miss} per band):")
    print("{" + ", ".join("{" + f"{d}, {z}" + "}" for d, z in table) + "}")


if __name__ == "__main__":
    main()
