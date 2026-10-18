#!/usr/bin/env python3
"""How spectated Dharok fighters eat and what hitpoints they start a fight at (riskfight_script's Dharok eating and riskfight_start.h's Dharok start).

Usage: python3 scripts/measure_dharok_eating.py [--duels FILE]      (python3 standard library only; deterministic)

Input: spectator.duels (see measure_dharok_play.py). Only Dharok v Dharok duels (both sides Dharok fighters, measure_dharok_play.Side.is_dharok); both sides count.
His bar (scale 30 of 99) is read on nearly every tick of a fight (the mean gap between reads is 1.02 ticks); bar 30 is 99 or more.

(a) Start: his bar on its first read, plus the damage of that tick. A full bar (30) is a regear start (the hits he then takes before the bar first drops put most
    of those at 115-121, an anglerfish overheal); below it his hitpoints are bar * 99 / 30 + that damage, in 10-hitpoint bins.
(b) Meals: the eat animation is drawn over by swings (it is on only about one rise in nine), so a meal is a rise of his bar between two consecutive reads, plus
    the damage he took in between, of at least MEAL_MIN hitpoints, from a bar below 30. Per bin of three bar points (about 10 hitpoints) of the earlier read:
    meals per tick in the bin, and the meal's size where the later read is below 30: single < 30 (an anglerfish 22 or a brew dose 16), double < 48 (anglerfish and
    karambwan 40), triple (anglerfish, brew and karambwan 56).
"""
import argparse
import collections
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "scripts"))
from measure_dharok_play import parse, Side, BAR, DMG  # noqa: E402
from mine_riskfight_pool import DEFAULT_DUELS  # noqa: E402

MEAL_MIN = 8  # hitpoints: above the bar's 3.3-hitpoint step and regeneration
BINS = 10     # bar bins of 3 points, bar 0-29; bar 30 is full


def fighters(duels):
    for d in duels:
        if not (Side(d, 0).is_dharok() and Side(d, 1).is_dharok()):
            continue
        for side in (0, 1):
            rows = [r[2 + side] for r in d["rows"]]
            bars, dmg = [int(r[BAR]) for r in rows], [int(r[DMG]) for r in rows]
            reads = [i for i in range(len(rows)) if bars[i] >= 0]
            if reads:
                yield bars, dmg, reads


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--duels", default=DEFAULT_DUELS)
    args = ap.parse_args()
    starts, full = collections.Counter(), 0
    ticks, meals = [0] * (BINS + 1), [0] * (BINS + 1)
    sizes = [collections.Counter() for _ in range(BINS)]
    n = 0
    for bars, dmg, reads in fighters(parse(args.duels)):
        n += 1
        first = reads[0]
        if bars[first] == 30: full += 1
        else: starts[min(9, int((bars[first] * 99 / 30 + dmg[first]) // 10))] += 1
        for j, k in zip(reads, reads[1:]):
            b = min(BINS, bars[j] // 3)
            ticks[b] += k - j
            rise = (bars[k] - bars[j]) * 99 / 30 + sum(dmg[j + 1:k + 1])
            if bars[j] < 30 and rise >= MEAL_MIN:
                meals[b] += 1
                if bars[k] < 30: sizes[b]["single" if rise < 30 else "double" if rise < 48 else "triple"] += 1
    print(f"Dharok fighters (Dharok v Dharok duels): {n}")
    print(f"(a) start: full bar {full} ({1000 * full / n:.0f} per mille); below full by 10 hitpoints: " + " ".join(f"{10 * b}-{10 * b + 9}: {starts[b]}" for b in range(10)))
    total = sum(ticks)
    print("(b) | bar | hp | ticks | share | meals | per mille per tick | single | double | triple |")
    for b in range(BINS + 1):
        s = sizes[b] if b < BINS else collections.Counter()
        m = sum(s.values()) or 1
        print(f"| {3 * b}-{min(30, 3 * b + 2)} | {3 * b * 99 / 30:.0f}-{min(99, (3 * b + 3) * 99 / 30):.0f} | {ticks[b]} | {ticks[b] / total:.3f} | {meals[b]} | "
              f"{1000 * meals[b] / ticks[b]:.1f} | {s['single'] / m:.2f} | {s['double'] / m:.2f} | {s['triple'] / m:.2f} |")
    for name, lo, hi in (("0-39", 0, 4), ("40-79", 4, 8), ("80+", 8, BINS + 1)):  # bar 0-11, 12-23, 24-30 (measure_dharok_play's bands)
        print(f"ticks at hp {name}: {sum(ticks[lo:hi]) / total:.3f}")
