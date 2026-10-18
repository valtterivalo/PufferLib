#!/usr/bin/env python3
"""How often spectated Dharok fighters drink potions that heal nothing: the target of riskfight_policy.h's RF_DHAROK_REPOT_PM and of riskfight_kits.h's
RF_KIT_DHAROK_EXTRA_POTIONS.

Usage: python3 scripts/measure_dharok_potions.py [--duels FILE]      (python3 standard library only; deterministic)

Input: spectator.duels (see measure_dharok_play.py), or the sim's fights written in the same format. Only Dharok v Dharok duels; both sides count.

The eat animation (829) is a meal's and a potion's alike. Between two of his fresh reads a and b (a hitsplat on him with a bar), a gap whose heal
bar_b + damage_b - bar_a is under NO_HEAL hitpoints holds no meal, so its animations are potions (combat, restore, prayer). Reported per 1,000 ticks of
such gaps, by bar_a at 0-11, 12-23, 24 up (hitpoints under 40, 40-79, 80 up), with the share of ticks the no-heal gaps cover. The rate counts only visible
animations; the sim's measure on the same format carries the sim's visibility rule, so the two compare like for like.
"""
import argparse
import collections
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "scripts"))
from measure_dharok_play import parse, Side, BAR, DMG, NHIT  # noqa: E402
from mine_riskfight_pool import DEFAULT_DUELS  # noqa: E402

EAT = 2
NO_HEAL = 4
BANDS = ("hp < 40 (bar 0-11)", "hp 40-79 (bar 12-23)", "hp 80+ (bar 24-30)")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--duels", default=DEFAULT_DUELS)
    args = ap.parse_args()
    shown, no_heal_ticks, ticks = collections.Counter(), collections.Counter(), collections.Counter()
    for d in parse(args.duels):
        if not (Side(d, 0).is_dharok() and Side(d, 1).is_dharok()):
            continue
        rows = d["rows"]
        for side in (0, 1):
            mine = [r[2 + side] for r in rows]
            fresh = [t for t in range(len(rows)) if int(mine[t][NHIT]) > 0 and int(mine[t][BAR]) >= 0 and not rows[t][1]]
            for a, b in zip(fresh, fresh[1:]):
                bar_a = int(mine[a][BAR])
                band = (bar_a >= 12) + (bar_a >= 24)
                ticks[band] += b - a
                if int(mine[b][BAR]) * 99 / 30 + int(mine[b][DMG]) - bar_a * 99 / 30 >= NO_HEAL:
                    continue
                no_heal_ticks[band] += b - a
                shown[band] += sum(mine[t][EAT] == "1" for t in range(a + 1, b + 1))
    for band, name in enumerate(BANDS):
        print(f"{name}: {1000 * shown[band] / no_heal_ticks[band]:.1f} potion animations per 1,000 no-heal ticks "
              f"({shown[band]} / {no_heal_ticks[band]}; no-heal share of ticks {no_heal_ticks[band] / ticks[band]:.2f})")


if __name__ == "__main__":
    main()
