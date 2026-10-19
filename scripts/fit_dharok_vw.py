#!/usr/bin/env python3
"""The Dharok script's Voidwaker special per attack, RF_DHAROK_VW_SPEC_PM (riskfight_policy.h), calibrated in the sim against spectated Dharok fighters.

Usage: python3 scripts/fit_dharok_vw.py [--duels FILE] [--policy riskfight_policy.h] [SIM.duels ...]      (python3 standard library only; deterministic)

Read on real and sim duels alike: Dharok v Dharok duels, fighters seen holding a Voidwaker (or speccing with one) in the duel; per tick (third-party rows
skipped), his own Vengeance (spent: his "Taste vengeance!" 1 or 2 ticks before; else up: a cast seen, not yet proc'd, measure_dharok_play.Side.veng_up; else
down) and the other side's bar the tick before (<12, 12-23, 24 up); a tick with his Voidwaker special counts. Rates are per 1,000 such ticks.

With SIM duels (ocean/osrs/tests/probe_script_duels.c, written with the table in --policy), each cell's next value moves the current one by the ratio of the
real rate to the sim's on the odds of the per-attack value: one step of a fixed-point iteration. Without SIM the real rates are printed.
"""
import argparse
import collections
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "scripts"))
from measure_dharok_play import parse, Side, SPEC, VENG_PROC, WEAPON_FIELD, VOIDWAKERS  # noqa: E402
from mine_riskfight_pool import DEFAULT_DUELS  # noqa: E402

DEFAULT_POLICY = os.path.join(ROOT, "ocean/osrs/encounters/riskfight/riskfight_policy.h")
BAR_NAMES = ("bar <12", "bar 12-23", "bar 24+")
VENG_NAMES = ("down", "up", "spent")


def die(message):
    print(f"fit_dharok_vw: {message}", file=sys.stderr)
    sys.exit(2)


def rates(paths):
    """(Vengeance state, his bar band) -> [ticks, Voidwaker specials]."""
    cells = collections.defaultdict(lambda: [0, 0])
    vw = {str(v) for v in VOIDWAKERS}
    for path in paths:
        for d in parse(path):
            sides = (Side(d, 0), Side(d, 1))
            if not (sides[0].is_dharok() and sides[1].is_dharok()): continue
            for v in (0, 1):
                s = sides[v]
                mine = [r[2 + v] for r in d["rows"]]
                if not any(m[WEAPON_FIELD] in vw or m[SPEC] == "VW" for m in mine): continue
                for i in range(2, len(mine)):
                    if s.third[i]: continue
                    bar = s.opp_bar[i - 1]
                    veng = 2 if int(mine[i - 1][VENG_PROC]) or int(mine[i - 2][VENG_PROC]) else int(s.veng_up[i - 1])
                    c = cells[(veng, (bar >= 12) + (bar >= 24))]
                    c[0] += 1
                    c[1] += mine[i][SPEC] == "VW"
    return cells


def current_table(path):
    m = re.search(r"RF_DHAROK_VW_SPEC_PM\[3\]\[3\] = \{(.*?)\};", open(path).read(), re.S)
    if not m: die(f"{path}: no RF_DHAROK_VW_SPEC_PM[3][3]")
    values = [int(v) for v in re.findall(r"-?\d+", m.group(1))]
    if len(values) != 9: die(f"{path}: RF_DHAROK_VW_SPEC_PM holds {len(values)} values, not 9")
    return {(u, q): values[u * 3 + q] for u in range(3) for q in range(3)}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--duels", default=DEFAULT_DUELS)
    ap.add_argument("--policy", default=DEFAULT_POLICY)
    ap.add_argument("sim", nargs="*")
    args = ap.parse_args()
    real = rates([args.duels])
    sim = rates(args.sim) if args.sim else None
    table = current_table(args.policy)
    print("Voidwaker specials per 1,000 ticks, real (ticks)" + (" / sim (ticks) / table now -> next" if sim else ""))
    nxt = {}
    for u in range(3):
        cols = []
        for q in range(3):
            rn, rs = real[(u, q)]
            if not rn: die(f"no real tick at veng {u}, {BAR_NAMES[q]}")
            r = rs / rn
            if sim is None:
                cols.append(f"{1000 * r:.1f} ({rn})")
                continue
            sn, ss = sim[(u, q)]
            t = table[(u, q)]
            if not sn or not ss: nxt[(u, q)] = t
            else:
                o = t / (1000 - t) * r / (ss / sn)
                nxt[(u, q)] = min(999, round(1000 * o / (1 + o)))
            cols.append(f"{1000 * r:.1f} ({rn}) / {1000 * ss / sn:.1f} ({sn}) / {t} -> {nxt[(u, q)]}" if sn else f"{1000 * r:.1f} ({rn}) / - / {t}")
        print(f"  veng {VENG_NAMES[u]}: " + " | ".join(cols))
    if sim is not None:
        print("static const int16_t RF_DHAROK_VW_SPEC_PM[3][3] = {" + ", ".join("{" + ", ".join(str(nxt[(u, q)]) for q in range(3)) + "}" for u in range(3)) + "};")


if __name__ == "__main__":
    main()
