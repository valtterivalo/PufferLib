#!/usr/bin/env python3
"""When spectated Dharok fighters teleport out, on their own hitpoints: riskfight_policy.h's RF_DHAROK_TAB_P10K.

Usage: python3 scripts/fit_dharok_tab.py [--duels FILE]      (python3 standard library only; deterministic)

Input: spectator.duels (see measure_dharok_play.py). Only Dharok v Dharok duels; both sides count, rows marked third-party are skipped.

- The fight ends at the first knockout or the first teleport animation of either side (tablet 4069/4071, spell 714, 3864); the side showing it leaves. A
  knockout is not a teleport.
- His hitpoints on a tick: the spectated bar moves only with a hitsplat, so between two of his hitsplats a and b they are bar_a plus the heal the hitsplat at b
  shows (bar_b + damage_b - bar_a, at least 0), in step with his eat animations in (a, t]; bar_a for the whole gap when none shows. From his last hitsplat on,
  the last read. Ticks before his first hitsplat are not counted (no read).
- Rate: teleports per fighter-tick at hitpoints 0-19, 20-39, 40-59, 60 up, per 10,000; the carried bar (the old fit's reading) is printed alongside.
"""
import argparse
import collections
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "scripts"))
from measure_dharok_play import parse, Side, BAR, DMG, NHIT, ANIMS, DEAD  # noqa: E402
from mine_riskfight_pool import DEFAULT_DUELS  # noqa: E402

EAT = 2
TELEPORT_ANIMS = {4069, 4071, 714, 3864}
BANDS = 4


def anims(field):
    return [] if field == "-" else [int(a) for a in field.split("|")]


def gap_hitpoints(mine):
    """Tick -> his estimated hitpoints (see the docstring)."""
    est, fresh = {}, [t for t in range(len(mine)) if int(mine[t][NHIT]) > 0 and int(mine[t][BAR]) >= 0]
    for a, b in zip(fresh, fresh[1:]):
        ha = int(mine[a][BAR]) * 99 / 30
        heal = max(0.0, int(mine[b][BAR]) * 99 / 30 + int(mine[b][DMG]) - ha)
        eats = [t for t in range(a + 1, b + 1) if mine[t][EAT] == "1"]
        for t in range(a, b):
            est[t] = ha + (heal * sum(e <= t for e in eats) / len(eats) if eats else 0)
    if fresh:
        for t in range(fresh[-1], len(mine)):
            est[t] = int(mine[fresh[-1]][BAR]) * 99 / 30
    return est


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--duels", default=DEFAULT_DUELS)
    args = ap.parse_args()
    ticks = {"gap": collections.Counter(), "bar": collections.Counter()}
    tabs = {"gap": collections.Counter(), "bar": collections.Counter()}
    for d in parse(args.duels):
        sides = (Side(d, 0), Side(d, 1))
        if not (sides[0].is_dharok() and sides[1].is_dharok()):
            continue
        rows = d["rows"]
        n = len(rows)
        dead = next((t for t in range(n) for s in (0, 1) if rows[t][2 + s][DEAD] == "1"), None)
        tele = [next((t for t in range(n) if any(a in TELEPORT_ANIMS for a in anims(rows[t][2 + s][ANIMS]))), None) for s in (0, 1)]
        end = min([x for x in (dead, *tele) if x is not None], default=n)
        for s in (0, 1):
            est = gap_hitpoints([r[2 + s] for r in rows])
            for t in range(min(end + 1, n)):
                if rows[t][1]:
                    continue
                for reading, hp in (("gap", est.get(t)), ("bar", sides[s].bar[t] * 99 / 30)):
                    if hp is None:
                        continue
                    band = min(BANDS - 1, int(min(hp, 99)) // 20)
                    if t < end: ticks[reading][band] += 1
                    elif tele[s] == end and dead is None: tabs[reading][band] += 1
    for reading in ("bar", "gap"):
        print(f"{reading}: teleports per 10,000 fighter-ticks at hitpoints 0-19 / 20-39 / 40-59 / 60+: " +
              " ".join(f"{10000 * tabs[reading][b] / ticks[reading][b]:.0f} ({tabs[reading][b]}/{ticks[reading][b]})" for b in range(BANDS)))
    print("RF_DHAROK_TAB_P10K: {" + ", ".join(str(round(10000 * tabs["gap"][b] / ticks["gap"][b])) for b in range(BANDS)) + "}")


if __name__ == "__main__":
    main()
