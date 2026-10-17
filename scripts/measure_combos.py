#!/usr/bin/env python3
"""How often a recorded opponent follows his hit with an instant granite maul special, against our hitpoints and his combo max (riskfight_script's combo rate).

Usage: python3 scripts/measure_combos.py [--work DIR]      (repository root or anywhere; python3 + cc)

Input: the tapes and request streams scripts/measure_overeats.py writes under --work (default /tmp/overeat_work: run it first). ocean/osrs/tests/combo_tapes.c (built
here) replays every stream through the live server's path and writes, per tick, our hitpoints, his estimated special energy and what the threat block knows of his maul
and his opening hit. Fights whose opponent's composition was never observed (October 4-6) are left out, as in measure_overeats.py: without his weapon the opening max is
his fists.

An opportunity is a normal swing of his (an attack animation other than the maul's 1667) on tick t while, at the end of tick t - 1, his estimated energy pays for at least
one maul special and the threat block holds a maul (wielded, swung, or the pool's prior for one hidden in the bag). A combo is his maul special (1667) on tick t + 1: the
recordings show it the tick after the swing (none on the swing's own tick, which the client could not tell apart anyway). His combo max is the opening max plus k maul
special maxes, k = energy // cost; the margin is our hitpoints at the end of tick t - 1 minus that combo max. A carrier fight is one in which he did a maul special.
"""

import argparse
import collections
import glob
import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "scripts"))
from measure_overeats import ATTACK_ANIMS, INSTANT_SPEC_ANIM  # noqa: E402

BUCKETS = [(-999, -60), (-60, -40), (-40, -20), (-20, 0), (0, 20), (20, 999)]


def build_harness(work):
    exe = os.path.join(work, "combo_tapes")
    src = os.path.join(ROOT, "ocean/osrs/tests/combo_tapes.c")
    subprocess.run(["cc", "-std=gnu11", "-O2", "-I.", "-Isrc", "-Wno-comment", "-o", exe, src, "-lm", "-lpthread"], cwd=ROOT, check=True)
    return exe


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--work", default="/tmp/overeat_work")
    args = ap.parse_args()
    streams = sorted(glob.glob(os.path.join(args.work, "requests", "*.requests")))
    if not streams:
        sys.exit("no request streams under %s/requests: run scripts/measure_overeats.py --work %s first" % (args.work, args.work))
    exe = build_harness(args.work)
    out = subprocess.run([exe] + streams, cwd=ROOT, check=True, capture_output=True, text=True).stdout
    state = collections.defaultdict(dict)
    for line in out.splitlines():
        f, _, t, hp, energy, maul_p, cost, maul_max, opening = line.split("\t")
        state[os.path.basename(f)[:-len(".requests")]][int(t)] = (int(hp), int(energy), float(maul_p), int(cost), int(maul_max), int(opening))
    observed = {os.path.basename(f)[:-len(".requests")] for f in streams if "ARMOR OBSERVED" in open(f).read()}

    opportunities = []   # (fight, carrier, margin, k, combo)
    for tape in sorted(glob.glob(os.path.join(args.work, "tapes", "*.tape"))):
        name = os.path.basename(tape)[:-len(".tape")]
        if name not in observed:
            continue
        anims = collections.defaultdict(set)
        with open(tape) as fh:
            for line in fh:
                p = line.split()
                if p and p[0] == "A" and p[2] == "O":
                    anims[int(p[1])].add(int(p[3]))
        carrier = any(INSTANT_SPEC_ANIM in a for a in anims.values())
        rows = state[name]
        for t in sorted(anims):
            if not any(a in ATTACK_ANIMS and a != INSTANT_SPEC_ANIM for a in anims[t]) or t - 1 not in rows:
                continue
            hp, energy, maul_p, cost, maul_max, opening = rows[t - 1]
            if maul_p <= 0 or energy < cost:
                continue
            k = energy // cost
            opportunities.append((name, carrier, hp - (opening + k * maul_max), k, INSTANT_SPEC_ANIM in anims.get(t + 1, ())))

    print("fights with his composition observed: %d; opportunities (his swing, a maul held or plausible, energy >= its cost): %d; combos: %d"
          % (len(observed), len(opportunities), sum(o[4] for o in opportunities)))
    print("margin = our hp before his swing - (his opening max + k x maul spec max)")
    print("%-12s %22s %22s" % ("margin", "carrier fights", "all fights"))
    for lo, hi in BUCKETS:
        cells = []
        for subset in (True, None):
            sel = [o for o in opportunities if lo <= o[2] < hi and (subset is None or o[1] == subset)]
            n, c = len(sel), sum(o[4] for o in sel)
            cells.append("%3d / %4d = %5.1f%%" % (c, n, 100.0 * c / n if n else 0.0))
        label = "< %d" % hi if lo == -999 else ">= %d" % lo if hi == 999 else "[%d, %d)" % (lo, hi)
        print("%-12s %22s %22s" % (label, cells[0], cells[1]))
    print("combos by k: %s" % dict(collections.Counter(o[3] for o in opportunities if o[4])))


if __name__ == "__main__":
    main()
