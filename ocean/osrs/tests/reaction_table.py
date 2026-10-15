#!/usr/bin/env python3
"""Side-by-side tables of his reaction to damage: the recordings and the sim, on the same windows (reaction_replay.c writes the rows).

  reaction_table.py LABEL=ROWS.tsv [LABEL=ROWS.tsv ...]     markdown on stdout; the first file's D rows are the recordings, every file's S and X rows a sim column

A window is alignment.py's (Light repo, tools/calib/alignment.py). A sim rate is the mean over windows of the share of that window's repetitions that did the thing, and
its interval is Wilson on the number of windows (the sampling noise of the recordings' windows, not of the repetitions).
  A1: isolated windows (nothing else hits him within 8 ticks): a visible meal launched within k ticks of the first damage, by the bar before.
  A4: split windows with both bars: a meal between the two damaging ticks (a visible meal in the gap, or a gross rise of 12 hitpoints or more).
  A3: windows whose total damage reaches the bar before them: his death on the first damage's tick or within 3 after, by what the first tick holds and by the gap.
  X : the same hitpoints and total delivered as one stack or as two halves g ticks apart; the edge is the paired difference of their kill rates.
"""
import collections
import csv
import math
import sys

BAR_HP = 99 / 30
HP_BINS = (("<=40", 0, 12), ("41-60", 13, 18), ("61-80", 19, 24), ("81+", 25, 30))
SOURCE = {0: "OWN", 1: "SPECTATOR"}
STYLE = {0: "OTHER", 1: "DHAROK"}
NO_RISE = -9999
INT_COLUMNS = ("source", "style", "session", "tick", "r0", "d0", "d1", "d2", "d3", "n0", "n1", "n2", "n3", "total", "isolated", "killed", "death", "eatmask", "eattrue", "hp0", "rep", "bar_age", "rise", "lead", "lead_eats")


def wilson(k, n, z=1.96):
    p = k / n
    d = 1 + z * z / n
    c = p + z * z / (2 * n)
    h = z * math.sqrt(p * (1 - p) / n + z * z / (4 * n * n))
    return max(0.0, (c - h) / d), min(1.0, (c + h) / d)


def share(values):
    """`values`: one number in [0, 1] per window."""
    n = len(values)
    if n == 0:
        return "-"
    k = sum(values)
    lo, hi = wilson(k, n)
    return "%.1f%% [%.0f, %.0f]" % (100 * k / n, 100 * lo, 100 * hi)


def load(path):
    rows = []
    with open(path) as handle:
        for r in csv.DictReader(handle, delimiter="\t"):
            for key in INT_COLUMNS:
                r[key] = int(r[key])
            rows.append(r)
    return rows


def hp_bin(r0):
    return next((name for name, lo, hi in HP_BINS if lo <= r0 <= hi), "none")


def eat_within(r, k):
    """A visible meal launched on the tick of the first damage or within k ticks after it (bit o + 2 of the mask)."""
    return any(r["eatmask"] >> (o + 2) & 1 for o in range(0, k + 1))


def meal_between(r, gap, key="eatmask"):
    return any(r[key] >> (o + 2) & 1 for o in range(1, gap + 1))


def reaches(r):
    return r["r0"] >= 0 and r["total"] > r["r0"] * BAR_HP


def first_alone(r):
    return r["d0"] >= (r["r0"] + 1) * BAR_HP


def window_key(r):
    return (r["source"], r["session"], r["tick"], r["r0"], r["d0"], r["d1"], r["d2"], r["d3"])


def by_window(rows, kind):
    groups = collections.defaultdict(list)
    for r in rows:
        if r["kind"] == kind:
            groups[window_key(r)].append(r)
    return list(groups.values())


class Column:
    def __init__(self, label, rows, kind):
        self.label = label
        self.rows = rows
        self.windows = by_window(rows, kind)

    def select(self, source, style, test=lambda r: True):
        return [g for g in self.windows if g[0]["source"] == source and g[0]["style"] == style and test(g[0])]


def a1(cols, source, style):
    head = ["bar before (HP)", "k", "windows"] + [c.label for c in cols]
    out = ["**A1 (%s, %s): isolated windows, a visible meal launched within k ticks of the first damage**\n" % (SOURCE[source], STYLE[style]), "| " + " | ".join(head) + " |", "|" + "---|" * len(head)]
    for name in [b[0] for b in HP_BINS] + ["none", "all"]:
        for k in (1, 2, 3):
            line = [name, "<=+%d" % k]
            for ci, c in enumerate(cols):
                sel = c.select(source, style, lambda r: r["isolated"] and name in ("all", hp_bin(r["r0"])))
                if ci == 0:
                    line.append(str(len(sel)))
                line.append(share([sum(eat_within(r, k) for r in g) / len(g) for g in sel]))
            out.append("| " + " | ".join(line) + " |")
    return "\n".join(out)


def a0(cols, source, style):
    head = ["bar before (HP)", "windows with a bar read before"] + [c.label for c in cols]
    out = ["**A0 (%s, %s): visible meals per 10 ticks between the last time his bar was read at a hit and the window (the lead-in the sim runs)**\n" % (SOURCE[source], STYLE[style]),
           "| " + " | ".join(head) + " |", "|" + "---|" * len(head)]
    for name in [b[0] for b in HP_BINS] + ["all"]:
        line = [name]
        for ci, c in enumerate(cols):
            sel = c.select(source, style, lambda r: r["lead"] > 0 and name in ("all", hp_bin(r["r0"])))
            if ci == 0:
                line.append(str(len(sel)))
            ticks = sum(g[0]["lead"] for g in sel)
            meals = sum(sum(r["lead_eats"] for r in g) / len(g) for g in sel)
            line.append("%.2f" % (10 * meals / ticks) if ticks else "-")
        out.append("| " + " | ".join(line) + " |")
    return "\n".join(out)


def a4(cols, source, style):
    head = ["gap to the second damage", "split windows with both bars"] + [c.label for c in cols] + ["sim: a meal really eaten in the gap"]
    out = ["**A4 (%s, %s): a meal between the first two damaging ticks of a split window** (a visible meal in the gap, or a rise of 12 hitpoints or more)\n" % (SOURCE[source], STYLE[style]),
           "| " + " | ".join(head) + " |", "|" + "---|" * len(head)]
    for gap in (1, 2, 3):
        line = [str(gap)]
        test = lambda r: r["pattern"] == "split+%d" % gap and r["rise"] != NO_RISE
        for ci, c in enumerate(cols):
            sel = c.select(source, style, test)
            if ci == 0:
                line.append(str(len(sel)))
            line.append(share([sum(1 for r in g if meal_between(r, gap) or r["rise"] >= 120) / len(g) for g in sel]))
        truth = []
        for c in cols[1:]:
            sel = c.select(source, style, test)
            truth.append(share([sum(1 for r in g if meal_between(r, gap, "eattrue")) / len(g) for g in sel]))
        line.append(" / ".join(truth) if truth else "-")
        out.append("| " + " | ".join(line) + " |")
    return "\n".join(out)


def a3(cols, source, style, fresh):
    title = "windows whose bar before was shown on the tick before (bar age <= 1)" if fresh else "windows whose total damage reaches the bar before them"
    head = ["first tick", "pattern", "windows"] + [c.label for c in cols]
    out = ["**A3 (%s, %s%s): kill rate in the window (his death on the first damage's tick or within 3 after) of the %s**\n" % (SOURCE[source], STYLE[style], ", fresh bar" if fresh else "", title),
           "| " + " | ".join(head) + " |", "|" + "---|" * len(head)]
    rows = [("alone reaches it", True, "lone"), ("alone reaches it", True, "stack"), ("needs the later damage", False, "split+1"), ("needs the later damage", False, "split+2"), ("needs the later damage", False, "split+3")]
    for label, first, pattern in rows:
        test = lambda r: reaches(r) and r["pattern"] == pattern and first_alone(r) == first and (not fresh or 0 <= r["bar_age"] <= 1)
        line = [label, pattern]
        for ci, c in enumerate(cols):
            sel = c.select(source, style, test)
            if ci == 0:
                line.append(str(len(sel)))
            line.append(share([sum(1 for r in g if r["killed"]) / len(g) for g in sel]))
        if line[2] != "0":
            out.append("| " + " | ".join(line) + " |")
    for label, patterns in (("one tick (lone + stack)", ("lone", "stack")), ("split +1", ("split+1",)), ("split +2", ("split+2",)), ("split +3", ("split+3",))):
        test = lambda r: reaches(r) and r["pattern"] in patterns and (not fresh or 0 <= r["bar_age"] <= 1)
        line = ["all", label]
        for ci, c in enumerate(cols):
            sel = c.select(source, style, test)
            if ci == 0:
                line.append(str(len(sel)))
            line.append(share([sum(1 for r in g if r["killed"]) / len(g) for g in sel]))
        out.append("| " + " | ".join(line) + " |")
    return "\n".join(out)


def x_tables(columns):
    """The kill rate of a stack and of the same hitpoints and total split g ticks apart, and their paired difference."""
    sims = columns[1:]
    head = ["population", "pattern", "draws"] + [c.label for c in sims]
    out = ["**X: matched injections, the kill rate of the same hitpoints and total as one stack or two halves g ticks apart**\n", "| " + " | ".join(head) + " |", "|" + "---|" * len(head)]
    edge_head = ["population", "split gap", "paired draws"] + [c.label for c in sims]
    edge = ["**X edge: kill rate of the stack minus kill rate of the same hitpoints and total split g ticks apart** (paired draws; 95% normal interval on the paired difference)\n",
            "| " + " | ".join(edge_head) + " |", "|" + "---|" * len(edge_head)]
    for source in (0, 1):
        for style in (0, 1):
            for band in [b[0] for b in HP_BINS] + ["all"]:
                population = "%s %s %s" % (SOURCE[source], STYLE[style], band)
                pick = lambda c: [r for r in c.rows if r["kind"] == "X" and r["source"] == source and r["style"] == style and band in ("all", hp_bin(r["r0"]))]
                picks = [pick(c) for c in sims]
                if not picks[0]:
                    continue
                for pattern in ("stack", "split+1", "split+2", "split+3"):
                    out.append("| " + " | ".join([population, pattern, str(sum(1 for r in picks[0] if r["pattern"] == pattern))] + [share([r["killed"] for r in p if r["pattern"] == pattern]) for p in picks]) + " |")
                for gap in (1, 2, 3):
                    line = [population, "+%d" % gap]
                    cells = []
                    n = 0
                    for p in picks:
                        paired = collections.defaultdict(dict)
                        for r in p:
                            paired[(r["source"], r["session"], r["tick"], r["r0"], r["total"], r["rep"])][r["pattern"]] = r["killed"]
                        diffs = [q["stack"] - q["split+%d" % gap] for q in paired.values() if "stack" in q and "split+%d" % gap in q]
                        if len(diffs) < 5:
                            cells.append("-")
                            continue
                        m = sum(diffs) / len(diffs)
                        h = 1.96 * math.sqrt(sum((x - m) ** 2 for x in diffs) / (len(diffs) - 1) / len(diffs))
                        cells.append("%+.1f pts [%+.1f, %+.1f]" % (100 * m, 100 * (m - h), 100 * (m + h)))
                        n = max(n, len(diffs))
                    line.insert(2, str(n))
                    edge.append("| " + " | ".join(line + cells) + " |")
    return "\n".join(edge) + "\n\n" + "\n".join(out)


def main():
    if len(sys.argv) < 2:
        raise SystemExit(__doc__)
    loaded = []
    for arg in sys.argv[1:]:
        label, path = arg.split("=", 1)
        loaded.append((label, load(path)))
    data = Column("recordings", loaded[0][1], "D")
    sims = [Column(label, rows, "S") for label, rows in loaded]
    cols = [data] + sims
    for source in (1, 0):
        for style in (0, 1):
            print(a1(cols, source, style) + "\n")
    for source in (1, 0):
        for style in (0, 1):
            print(a0(cols, source, style) + "\n")
    for source in (1, 0):
        for style in (0, 1):
            print(a4(cols, source, style) + "\n")
    for source in (1, 0):
        for style in (0, 1):
            for fresh in (False, True):
                print(a3(cols, source, style, fresh) + "\n")
    if any(r["kind"] == "X" for _, rows in loaded for r in rows):
        print(x_tables([data] + [Column(label, rows, "X") for label, rows in loaded]))


if __name__ == "__main__":
    main()
