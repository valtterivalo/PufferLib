#!/usr/bin/env python3
"""When spectated Dharok fighters eat, on their own hitpoints and the other side's attack clock: riskfight_policy.h's RF_DHAROK_MEAL_PM.

Usage: python3 scripts/fit_dharok_meals.py [--duels FILE]      (python3 standard library only; deterministic)

Input: spectator.duels (see measure_dharok_play.py). Only Dharok v Dharok duels; both sides count.

The spectated bar moves only with a hitsplat: of the eat animations below 60 hitpoints none shows the bar rising before his next hitsplat (the heal shows on
that hit, bar plus its damage). So a rise of the bar between reads (measure_dharok_eating.py (b), (c)) times a meal at the next hit, not when it was eaten,
and his hitpoints between hits are the last read plus meals not yet shown. This fit times meals by their eat animation and reads his hitpoints between hits as:

- Fresh reads: a tick with a hitsplat on him (NHIT > 0) and a bar. Between two of them, a and b, he healed H = bar_b + damage_b - bar_a (hitpoints).
- His hitpoints on a tick of (a, b]: bar_a plus H times the share of the gap's visible eat animations before that tick (bar_a for the whole gap when none shows).
  Ticks before his first hitsplat and after his last are not read.
- Visibility: an eat animation shows with p 0 on his own swing tick, 0.49 on a tick a hit lands on him, 0.45 the tick after any animation of his, 0.89
  otherwise (riskfight_step.h's RF_MEAL_VISIBLE_*, fitted on the c-engine tapes). A cell's meals per tick are its visible animations over the sum of p over
  its ticks (each tick's chance to show a meal), so hidden meals count at the cell's rate.
- His clock phase on tick t (RiskfightMealPhase): ticks since the other side's last swing animation before t (instant maul specials, 1667, leave it running),
  as the script reads it from the observation: 1, 2, 3, 4 up while his weapon is not ready (LATE), READY from his weapon's speed on (SWING_SPEED by animation,
  4 for shared animations), NONE before his first swing.
- Bands: bar 0-5, 6-11, 12-17, 18-23, 24-29 of 30 on the estimated hitpoints (the script's band, hitpoints * 30 / 99 / 6).

- Calibration: the eat animation (829) is a potion's too, so animations alone overcount meals that heal (restores and boosts drink on the same animation). The
  rates from the animations give the shape over the phases; each band is scaled so its meals carry the heal the gaps show: a gap's heal H counts H / size meals,
  size the mean of the script's meal sizes at that hitpoints (RF_DHAROK_MEAL_SINGLE_PM / _DOUBLE_PM: single 22, double 40, triple 56), split over the gap's
  ticks in proportion to their animation rates. NONE cells under NONE_MIN ticks take the band's READY rate.

The rates of bands 0-3 (below 79 hitpoints) come from the heal the next hitsplat shows instead, a current-status fit that needs no visibility model: after a hit
of the other side leaves him in a band (a fresh read, the swing animation of the other side on that tick or the one before), his next hit lands L ticks of
the other side's clock later (swing to swing); he was healed by then when bar + damage on that hit is 8 or more over the read. With a meal per tick at rate h,
P(healed by L) = 1 - (1 - h)^L; h is the maximum likelihood fit per band (a grid of 1/1000), the same on every phase. The animation rates put too few meals
in the first ticks after his hit (by his next 4-tick swing 0.65 healed in the sim against 0.92-0.98 spectated), and the constant hazard fits the spectated
P(healed by L) within its noise (printed). Band 4 keeps the animation rates: from 80 up a meal mostly lifts him past 99, which the bar (30 at 99 and up) hides.
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

EAT = 2
SWING_SPEED = {1658: 4, 11275: 4, 7045: 6, 426: 4, 10172: 5, 10173: 5, 440: 7, 2066: 7, 2067: 7, 7514: 4, 7644: 6, 376: 4, 1062: 4, 4230: 4, 5865: 4, 5866: 4, 386: 4}
INSTANT_ANIMS = {1667}
PHASES = ("1", "2", "3", "late", "ready", "none")
BANDS = 5
STATUS_BANDS, STATUS_L, HEALED_MIN = 4, 11, 8  # bands fitted on current status; L cap; heal that counts as a meal
NONE_MIN = 100  # ticks under which the NONE cell takes the band's READY rate
VISIBLE_SWING, VISIBLE_HIT, VISIBLE_CARRY, VISIBLE_QUIET = 0.0, 0.49, 0.45, 0.89
SINGLE_PM = [150, 130, 100, 170, 310, 790, 970, 970, 920, 1000]  # riskfight_policy.h RF_DHAROK_MEAL_SINGLE_PM / _DOUBLE_PM, by hitpoints / 10
DOUBLE_PM = [40, 140, 280, 540, 650, 210, 20, 30, 80, 0]
SIZE_BY_BAND = []
for _b in range(5):  # a band's mean meal: the deciles it covers (bar 6 = hitpoints 19.8)
    _d = [x for x in range(10) if _b * 19.8 <= x * 10 + 5 < (_b + 1) * 19.8]
    SIZE_BY_BAND.append(sum((22 * SINGLE_PM[x] + 40 * DOUBLE_PM[x] + 56 * (1000 - SINGLE_PM[x] - DOUBLE_PM[x])) / 1000 for x in _d) / len(_d))


def anims(field):
    return [] if field == "-" else [int(a) for a in field.split("|")]


def phases(his):
    out, last, speed = [], None, 4
    for t, a in enumerate(his):
        out.append(5 if last is None else min(t - last, 4) - 1 if t - last < speed else 4)
        swings = [x for x in anims(a) if x in SWING_ANIMS and x not in INSTANT_ANIMS]
        if swings: last, speed = t, SWING_SPEED.get(swings[0], 4)
    return out


def fighter_ticks(duels):
    """Per read tick: (band, phase, visibility p, eat animation shown), and per gap: (ticks of the gap, heal H)."""
    for d in duels:
        if not (Side(d, 0).is_dharok() and Side(d, 1).is_dharok()):
            continue
        rows_all = d["rows"]
        for side in (0, 1):
            rows = [r[2 + side] for r in rows_all]
            if any(r[11] == "1" for r in rows): end = next(i for i, r in enumerate(rows) if r[11] == "1")
            else: end = len(rows)
            ph = phases([r[3 - side][ANIMS] for r in rows_all])
            fresh = [t for t in range(end) if int(rows[t][NHIT]) > 0 and int(rows[t][BAR]) >= 0 and not rows_all[t][1]]
            ticks, gaps = [], []
            for a, b in zip(fresh, fresh[1:]):
                if any(rows_all[t][1] for t in range(a, b + 1)): continue
                ha = int(rows[a][BAR]) * 99 / 30
                heal = int(rows[b][BAR]) * 99 / 30 + int(rows[b][DMG]) - ha
                eats = [t for t in range(a + 1, b + 1) if rows[t][EAT] == "1"]
                gap = []
                for t in range(a + 1, b + 1):
                    hp = ha + (max(0.0, heal) * sum(e < t for e in eats) / len(eats) if eats else 0)
                    own = anims(rows[t][ANIMS])
                    p = VISIBLE_SWING if any(x in SWING_ANIMS for x in own) else VISIBLE_HIT if int(rows[t][NHIT]) > 0 else \
                        VISIBLE_CARRY if anims(rows[t - 1][ANIMS]) or rows[t - 1][EAT] == "1" else VISIBLE_QUIET
                    if hp >= 99: continue
                    gap.append((min(BANDS - 1, int(hp * 30 / 99) // 6), ph[t], p, rows[t][EAT] == "1"))
                ticks += gap
                gaps.append((gap, heal))
            yield ticks, gaps


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--duels", default=DEFAULT_DUELS)
    args = ap.parse_args()
    shown, weight, count = collections.Counter(), collections.Counter(), collections.Counter()
    all_gaps = []
    for ticks, gaps in fighter_ticks(parse(args.duels)):
        for band, phase, p, eat in ticks:
            shown[(band, phase)] += eat
            weight[(band, phase)] += p
            count[(band, phase)] += 1
        all_gaps += gaps
    anim_rate = {k: shown[k] / weight[k] if weight[k] else 0.0 for k in count}
    fitted, healed = collections.Counter(), collections.Counter()
    for g, heal in all_gaps:
        w = [anim_rate[(band, phase)] for band, phase, _, _ in g]
        if not sum(w): continue
        for (band, phase, _, _), x in zip(g, w):
            fitted[band] += x
            healed[band] += max(0.0, heal) * x / sum(w) / SIZE_BY_BAND[band]
    scale = [healed[b] / fitted[b] if fitted[b] else 0.0 for b in range(BANDS)]
    print("band scale (heal meals / animation meals): " + " ".join(f"{s:.2f}" for s in scale) + " | mean meal " + " ".join(f"{x:.0f}" for x in SIZE_BY_BAND))
    rate = {k: v * scale[k[0]] for k, v in anim_rate.items()}
    for b in range(BANDS):  # before his first swing: too few ticks to read, the band's ready rate (no swing of his pending either way)
        if count[(b, 5)] < NONE_MIN: rate[(b, 5)] = rate.get((b, 4), 0.0)
    print("meals per mille per tick (visible animations / sum of visibility; ticks in brackets); RF_DHAROK_MEAL_PM rows, phases " + " ".join(PHASES))
    for b in range(BANDS):
        cells = [f"{PHASES[p]} {1000 * rate.get((b, p), 0):.0f} ({shown[(b, p)]}/{count[(b, p)]})" for p in range(len(PHASES))]
        print(f"| bar {6 * b}-{6 * b + 5} | hp {6 * b * 99 / 30:.0f}-{(6 * b + 6) * 99 / 30:.0f} | " + " | ".join(cells) + " |")
    status = current_status(parse(args.duels))
    for b in range(STATUS_BANDS):
        rows = [(L, status[(b, L)]) for L in range(1, STATUS_L + 1) if status[(b, L)][1]]
        h = max(range(1, 1000), key=lambda i: status_loglik(rows, i / 1000)) / 1000
        print(f"band {b} current status n {sum(n for _, (_, n) in rows)}, h {1000 * h:.0f} per mille; P(healed by L) seen / fitted: " +
              " ".join(f"{L}: {y / n:.2f} / {1 - (1 - h) ** L:.2f} ({n})" for L, (y, n) in rows if L <= 7))
        for p in range(len(PHASES)): rate[(b, p)] = h
    print("RF_DHAROK_MEAL_PM:")
    print("{" + ", ".join("{" + ", ".join(f"{round(1000 * rate.get((b, p), 0))}" for p in range(len(PHASES))) + "}" for b in range(BANDS)) + "}")


def status_loglik(rows, h):
    return sum(y * math.log(1 - (1 - h) ** L) + (n - y) * L * math.log(1 - h) for L, (y, n) in rows)


def current_status(duels):
    """(band, L) -> [healed, gaps]: see the docstring. L is capped at STATUS_L."""
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
                if mine[a][11] == "1" or int(mine[a][DMG]) <= 0: continue
                sa = a if swing[a] else a - 1 if swing[a - 1] else None
                sb = b if swing[b] else b - 1 if swing[b - 1] else None
                hp = int(mine[a][BAR]) * 99 / 30
                if sa is None or sb is None or sb <= sa or hp <= 0: continue
                band = min(BANDS - 1, int(hp * 30 / 99) // 6)
                if band >= STATUS_BANDS: continue
                cell = out[(band, min(sb - sa, STATUS_L))]
                cell[0] += int(mine[b][BAR]) * 99 / 30 + int(mine[b][DMG]) - hp >= HEALED_MIN
                cell[1] += 1
    return out


if __name__ == "__main__":
    main()
