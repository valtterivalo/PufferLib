#!/usr/bin/env python3
"""How often a meal's eat animation (829) is visible, from OUR meals in the fight tapes (rollout_export.py): our clicks on food (M lines, landed tick estimated by the
exporter's cutoff, so the meal tick is searched in t-1..t+1 for the animation) against our animations (A U rows) and the hits landed on us (H U rows). The other
side sees our animations exactly as we see his, so these are the probabilities riskfight_meal_visible uses.

Classes by what the eater shows on the meal tick tau (tau = the tick with the animation when there is one, else the landed tick):
  hit    a hitsplat lands on the eater on tau (its block animation competes);
  swing  a swing animation on tau (never visible, Fact 6);
  carry  no hit, no swing, but an animation (swing, block, meal, cast) on tau-1;
  quiet  none of those.

    python3 fit_meal_visibility.py <tape dir>
"""
import collections
import glob
import math
import os
import sys

FOOD = {13441, 3144, 385, 391, 7946}
EAT, VENG_CAST, BLOCK = 829, 8317, 4177


def wilson(k, n, z=1.96):
    if n == 0:
        return (0.0, 1.0)
    p = k / n
    d = 1 + z * z / n
    c = p + z * z / (2 * n)
    h = z * math.sqrt(p * (1 - p) / n + z * z / (4 * n * n))
    return ((c - h) / d, (c + h) / d)


def main(tape_dir):
    cells = collections.Counter()
    for path in sorted(glob.glob(os.path.join(os.path.expanduser(tape_dir), "*.tape"))):
        meals, anims, hits = [], collections.defaultdict(set), collections.defaultdict(list)
        for line in open(path):
            f = line.split()
            if not f:
                continue
            if f[0] == "M" and f[2] == "click" and int(f[4]) in FOOD:
                meals.append(int(f[1]))
            elif f[0] == "A" and f[2] == "U":
                anims[int(f[1])].add(int(f[3]))
            elif f[0] == "H" and f[2] == "U":
                hits[int(f[1])].append(int(f[3]))
        swing = lambda s: any(a not in (EAT, VENG_CAST, BLOCK, -1) for a in s)
        for t in meals:
            seen = [d for d in (0, -1, 1) if EAT in anims.get(t + d, ())]
            tau = t + seen[0] if seen else t
            if swing(anims.get(tau, ())):
                klass = "swing"
            elif hits.get(tau):
                klass = "hit"
            elif anims.get(tau - 1):
                klass = "carry"
            else:
                klass = "quiet"
            cells[(klass, bool(seen))] += 1
    print("class    meals visible  p      95% CI")
    for klass in ("quiet", "carry", "hit", "swing"):
        k, n = cells[(klass, True)], cells[(klass, True)] + cells[(klass, False)]
        lo, hi = wilson(k, n)
        print(f"{klass:8s} {n:5d} {k:7d} {k / n if n else float('nan'):.3f}  [{lo:.3f}, {hi:.3f}]")


if __name__ == "__main__":
    main(sys.argv[1])
