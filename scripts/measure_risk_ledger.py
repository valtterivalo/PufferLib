#!/usr/bin/env python3
"""The risk ledger: how much death chance a Dharok fighter sits in, and how much he accepts for a kill chance of his own, read the same way off real and sim duels.

Usage: python3 scripts/measure_risk_ledger.py --laws RISK_LAWS [--duels FILE] [--names FILE] [SIM.duels ...]      (python3 standard library only; deterministic)

RISK_LAWS is ocean/osrs/tests/risk_laws.c built (it answers the law of each attack from the engine's threat math, osrs_pvp_threat.h). --duels defaults to the
spectator corpus, --names to FILE.names. SIM files come with their SIM.names. A net's corpus (ocean/osrs/tests/probe_risk_duels.c, `id us his`) is split into
"sim us" (the net), "sim oppfit v net" (the fitted opponent model) and "sim scripts v net"; a scripts' corpus (probe_script_duels.c, both seats `script<persona>`)
is "sim scripts v scripts", the like of real Dharok fighters against each other. Only Dharok v Dharok duels count; rows marked third-party are skipped.

Per fighter-tick, from what a spectator sees:
- Hitpoints: fit_dharok_tab.gap_hitpoints (the bar at a hitsplat plus the next hitsplat's heal, in step with eat animations); before a side's first hitsplat its
  first bar read. A tick with either side's hitpoints unknown is skipped.
- His options: every weapon he is seen holding in the duel, its normal attack and, while his special energy allows, its special (single-hit specials only: a
  multi-hit special is left out and counted). Energy: 100 at the duel's start, each special seen costs the weapon's cost, +10 per 50 ticks, at most 100.
- An attack's law is the engine's (risk_laws): the attacker in his composition of the tick with the weapon wielded, the defender in his, at the assumed levels and
  prayers of a viewer (a maxed account, boosted, piety); a Dharok's max at the attacker's hitpoints. A landed hit is uniform on [lo, hi]
  (riskfight_attack_tail): the Voidwaker special always lands, from half its max hit to one and a half.
- K, his kill chance: the best one-attack knockout chance among his options against the other's hitpoints. D, his death chance: the other side's K against him.
  Neither counts Vengeance, a recoil, two attacks on one tick or a meal in between: one attack, the other's hitpoints as estimated.

Decision points: a fighter's attack clock frees `speed` ticks after his last swing (the swung weapon's speed). The first tick it is free is a decision point; what
he does first in the next WINDOW ticks is read: a swing, a shown meal (829), a teleport, or none. A meal on his own swing tick never shows (in the sim as in real
data), so "swing" includes a meal eaten with the swing.

Reported per group (real, sim opponents, sim us):
(a) occupancy: fighter-ticks by D band;
(b) at decision points by D band: what comes first (swing / meal / teleport / none), the mean K;
(c) at decision points, by D below or at least 0.25 and by his own K: what comes first;
(d) per fighter (a named real fighter, a sim opponent persona, the net) at decision points with D >= 0.1: his raw share swinging first (fighters with >= MIN_POINTS),
    and the part of the spread that repeats across his duels (split_half: residuals against the group's D x K cell rates, his duels alternated into two halves,
    the covariance of the halves across fighters), with a 90% interval over fighters resampled and the split-half correlation.
(e) mean D and K at a swing; the greataxe's share of his swings without a special at own hitpoints < 20 by a fresh read (fit_dharok_axe.fresh_hitpoints: the
    gap estimate spreads swings over a meal hidden on the swing tick into the low band, and how often differs between real and sim meal timing), with a 90%
    interval over duels resampled.
"""
import argparse
import collections
import random
import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "scripts"))
from measure_dharok_play import parse, Side, BAR, SPEC, ANIMS, WEAPON_FIELD, DHAROK_AXES, table  # noqa: E402
from fit_dharok_tab import gap_hitpoints, TELEPORT_ANIMS  # noqa: E402
from mine_riskfight_pool import DEFAULT_DUELS, SWING_ANIMS  # noqa: E402
from fit_dharok_axe import fresh_hitpoints  # noqa: E402

EAT_FIELD = 2
WINDOW = 4
MIN_POINTS = 15
HALF_MIN = 8
ENERGY_REGEN_TICKS = 50
D_BANDS = (0.02, 0.10, 0.25, 0.50)
D_NAMES = ("<.02", ".02-.10", ".10-.25", ".25-.50", ">=.50")
K_BANDS = (0.10, 0.25)
K_NAMES = ("K <.10", "K .10-.25", "K >=.25")
GROUPS = ("real", "sim scripts v scripts", "sim scripts v net", "sim oppfit v net", "sim us")


def die(message):
    print(f"measure_risk_ledger: {message}", file=sys.stderr)
    sys.exit(2)


def band(x, edges):
    return sum(x >= e for e in edges)


def tail(hit, lo, hi, need):
    """riskfight_attack_tail: P(X >= need), 0 on a miss (1 - hit), else uniform on the integers [lo, hi]."""
    if need <= 0: return 1.0
    if need <= lo: return hit
    if need > hi: return 0.0
    return hit * (hi - need + 1) / (hi - lo + 1)


def anims(field):
    return [] if field == "-" else [int(a) for a in field.split("|")]


class Laws:
    """The engine's attack laws (risk_laws), asked once per distinct (weapon, attacker composition, defender composition)."""

    def __init__(self, binary):
        self.binary, self.want, self.law = binary, set(), {}
        self.unknown_weapons, self.unknown_items = collections.Counter(), 0

    def key(self, weapon, att, dfn):
        return (weapon, tuple(att), tuple(dfn))

    def ask(self, key):
        if key not in self.law: self.want.add(key)

    def resolve(self):
        keys = sorted(self.want)
        if not keys: return
        query = "".join(" ".join(str(v) for v in (k[0],) + k[1] + k[2]) + "\n" for k in keys)
        done = subprocess.run([self.binary], input=query, capture_output=True, text=True)
        if done.returncode != 0: die(f"risk_laws failed: {done.stderr.strip()}")
        lines = done.stdout.splitlines()
        if len(lines) != len(keys): die(f"risk_laws answered {len(lines)} of {len(keys)} queries")
        for k, line in zip(keys, lines):
            if line == "unknown_weapon":
                self.law[k] = None
                self.unknown_weapons[k[0]] += 1
                continue
            f = line.split()
            law = dict(hit=float(f[0]), max=int(f[1]), dharok=int(f[2]), speed=int(f[3]), spec_cost=int(f[4]), spec_hits=int(f[5]), spec_hit=float(f[6]),
                       spec_lo=int(f[7]), spec_hi=int(f[8]), unknown=int(f[9]))
            law["max_at"] = [int(v) for v in f[10:]] if law["dharok"] else None
            self.unknown_items += law["unknown"]
            self.law[k] = law
        self.want.clear()


class Fighter:
    """One side of one duel, per row: hitpoints, composition, weapon in hand, the weapons he is seen with, his special energy, what he shows."""

    def __init__(self, duel, side):
        self.s = Side(duel, side)
        rows = duel["rows"]
        mine = [r[2 + side] for r in rows]
        self.mine = mine
        gap = gap_hitpoints(mine)
        first = next((int(m[BAR]) * 99 / 30 for m in mine if int(m[BAR]) >= 0), None)
        first_fresh = min(gap) if gap else len(mine)
        self.hp = [gap.get(t, first if t < first_fresh else None) for t in range(len(mine))]
        self.weapon = [int(m[WEAPON_FIELD]) for m in mine]
        self.carried = sorted({w for w in self.weapon if w >= 0})
        self.anims = [anims(m[ANIMS]) for m in mine]
        self.spec = [m[SPEC] != "-" for m in mine]
        self.shown_meal = [m[EAT_FIELD] == "1" or 829 in a for m, a in zip(mine, self.anims)]
        self.tab = [any(a in TELEPORT_ANIMS for a in an) for an in self.anims]
        self.kit = self.s.kit

    def comp(self, i):
        k = self.kit[i]
        return list(k) if k is not None else None


def options(laws, att, dfn, i, energy):
    """(law, use) per option of `att` against `dfn` at row i: each carried weapon's normal attack, and its single-hit special while energy allows."""
    a, d = att.comp(i), dfn.comp(i)
    out = []
    for w in att.carried:
        law = laws.law.get(laws.key(w, a, d))
        if law is None: continue
        out.append((law, "normal"))
        if law["spec_hits"] == 1 and law["spec_cost"] <= energy: out.append((law, "spec"))
    return out


def kill_chance(opts, att_hp, def_hp):
    best = 0.0
    for law, use in opts:
        if use == "normal":
            hi = law["max_at"][min(121, max(1, att_hp)) - 1] if law["dharok"] else law["max"]
            best = max(best, tail(law["hit"], 0, hi, def_hp))
        else:
            best = max(best, tail(law["spec_hit"], law["spec_lo"], law["spec_hi"], def_hp))
    return best


def energies(f, laws, other):
    out, energy, spent_at = [], 100.0, None
    for i in range(len(f.mine)):
        energy = min(100.0, energy + 10.0 / ENERGY_REGEN_TICKS)
        if f.spec[i]:
            a, d = f.comp(i), other.comp(i)
            law = laws.law.get(laws.key(f.weapon[i], a, d)) if a and d else None
            energy = max(0.0, energy - (law["spec_cost"] if law and law["spec_cost"] > 0 else 50))
        out.append(energy)
    return out


def read_corpus(path, names_path, group_of_name, laws, tally):
    duels = parse(path)
    names = {}
    if names_path and os.path.exists(names_path):
        for line in open(names_path):
            f = line.split()
            names[f[0]] = (f[1], f[2])
    pairs = []
    for d in duels:
        f = (Fighter(d, 0), Fighter(d, 1))
        if not (f[0].s.is_dharok() and f[1].s.is_dharok()): continue
        pairs.append((d, f))
        for x, y in ((0, 1), (1, 0)):
            for i in range(len(d["rows"])):
                a, b = f[x].comp(i), f[y].comp(i)
                if a is None or b is None: continue
                for w in f[x].carried: laws.ask(laws.key(w, a, b))
    laws.resolve()
    for d, f in pairs:
        who = names.get(d["id"])
        e = (energies(f[0], laws, f[1]), energies(f[1], laws, f[0]))
        rows = d["rows"]
        n = len(rows)
        K = [[None] * n, [None] * n]
        for x, y in ((0, 1), (1, 0)):
            for i in range(n):
                hx, hy = f[x].hp[i], f[y].hp[i]
                if hx is None or hy is None or f[x].comp(i) is None or f[y].comp(i) is None: continue
                K[x][i] = kill_chance(options(laws, f[x], f[y], i, e[x][i]), round(hx), max(1, round(hy)))
        for x, y in ((0, 1), (1, 0)):
            name = who[x] if who else None
            group = group_of_name(name)
            fighter_id = (group, name if name else f"{d['id']}:{x}")
            t = tally[group]
            fx = f[x]
            ready_at, last_point = None, None
            swing = [any(a in SWING_ANIMS for a in an) for an in fx.anims]
            for i in range(n):
                counted = not f[x].s.third[i] and K[x][i] is not None and K[y][i] is not None
                # The decision point comes before this tick's own swing moves his clock on: a swing on the very tick his clock frees is read as swinging first.
                if counted and ready_at is not None and i >= ready_at and last_point != ready_at:
                    last_point = ready_at
                    first = "none"
                    for j in range(i, min(n, i + WINDOW)):
                        if fx.tab[j]: first = "tab"; break
                        if swing[j]: first = "swing"; break
                        if fx.shown_meal[j]: first = "meal"; break
                    dd, k = K[y][i], K[x][i]
                    db = band(dd, D_BANDS)
                    t["points"][db][first] += 1
                    t["points_k"][db] += k
                    t["joint"][(dd >= 0.25, band(k, K_BANDS))][first] += 1
                    if dd >= 0.10:
                        t["per_fighter"][fighter_id][first] += 1
                        t["points_list"].append((fighter_id, (path, d["id"]), db, band(k, K_BANDS), first == "swing"))
                if swing[i]:
                    law = laws.law.get(laws.key(fx.weapon[i], fx.comp(i), f[y].comp(i))) if fx.comp(i) and f[y].comp(i) else None
                    ready_at = i + (law["speed"] if law else 4)
                if swing[i] and not fx.spec[i]:
                    fresh = fresh_hitpoints(fx.mine, i) if i > 0 and not f[x].s.third[i] else None
                    if fresh is not None and fresh < 20:
                        low = t["low_by_duel"][(path, d["id"])]
                        low[0] += 1
                        if any(a in (2066, 2067) for a in fx.anims[i]):
                            low[1] += 1
                            if counted: t["low_axe_k"].append(K[x][i])
                if not counted: continue
                k, dd = K[x][i], K[y][i]
                t["ticks"][band(dd, D_BANDS)] += 1
                t["ticks_all"] += 1
                if swing[i]:
                    t["swing_d"] += dd
                    t["swing_k"] += k
                    t["swings"] += 1
    return len(pairs)


def new_tally():
    return dict(ticks=collections.Counter(), ticks_all=0, points=collections.defaultdict(collections.Counter), points_k=collections.Counter(),
                joint=collections.defaultdict(collections.Counter), per_fighter=collections.defaultdict(collections.Counter), low_axe_k=[],
                low_by_duel=collections.defaultdict(lambda: [0, 0]),
                swings=0, swing_d=0.0, swing_k=0.0, points_list=[])


def split_half(points, boot=1000):
    """The between-fighter spread of swinging first under threat that repeats across a fighter's duels: each point's residual against the group's rate in its
    D x K cell (so a fighter's mix of states does not count), his duels alternated into two halves (a duel's points stay together, so whatever ties the
    decisions of one duel together lands in one half), and the covariance across fighters of his two half means: an estimate of the variance of his own
    tendency, free of the noise of either half. Fighters with >= HALF_MIN points in each half. Returns (fighters, SD, 90% interval over fighters resampled,
    split-half correlation)."""
    cell_n, cell_s = collections.Counter(), collections.Counter()
    for _, _, db, kb, s in points:
        cell_n[(db, kb)] += 1
        cell_s[(db, kb)] += s
    by = collections.defaultdict(lambda: collections.defaultdict(list))
    for fid, duel, db, kb, s in points:
        by[fid][duel].append(s - cell_s[(db, kb)] / cell_n[(db, kb)])
    pairs = []
    for fid, duels in by.items():
        halves = ([], [])
        for j, duel in enumerate(sorted(duels)): halves[j % 2].extend(duels[duel])
        if min(len(halves[0]), len(halves[1])) >= HALF_MIN: pairs.append((sum(halves[0]) / len(halves[0]), sum(halves[1]) / len(halves[1])))
    if len(pairs) < 3: return len(pairs), None, None, None

    def cov(ps):
        ma, mb = sum(a for a, _ in ps) / len(ps), sum(b for _, b in ps) / len(ps)
        return sum((a - ma) * (b - mb) for a, b in ps) / (len(ps) - 1)

    c = cov(pairs)
    va, vb = cov([(a, a) for a, _ in pairs]), cov([(b, b) for _, b in pairs])
    rng = random.Random(1)
    sds = sorted(max(0.0, cov([rng.choice(pairs) for _ in pairs])) ** 0.5 for _ in range(boot))
    return len(pairs), max(0.0, c) ** 0.5, (sds[int(0.05 * boot)], sds[int(0.95 * boot)]), c / (va * vb) ** 0.5 if va > 0 and vb > 0 else None


def share(c, key):
    n = sum(c.values())
    return f"{c[key] / n:.2f}" if n else "-"


def quantiles(xs):
    xs = sorted(xs)
    if not xs: return "-"
    q = lambda p: xs[min(len(xs) - 1, int(p * len(xs)))]
    return f"{q(0.1):.2f} / {q(0.25):.2f} / {q(0.5):.2f} / {q(0.75):.2f} / {q(0.9):.2f}"


def report(tally, counts):
    groups = [g for g in GROUPS if tally[g]["ticks_all"]]
    out = ["duels read (Dharok v Dharok): " + ", ".join(f"{k} {v}" for k, v in counts.items()), ""]
    out.append("(a) fighter-ticks by D, his chance to die to the other's next attack")
    out.append(table(["group", "ticks"] + list(D_NAMES), [[g, tally[g]["ticks_all"]] + [f"{tally[g]['ticks'][b] / tally[g]['ticks_all']:.3f}" for b in range(5)]
                                                          for g in groups]))
    out.append("")
    out.append(f"(b) decision points (his clock free; first action within {WINDOW} ticks): swing / meal / teleport / none, mean K")
    rows = []
    for g in groups:
        for b in range(5):
            c = tally[g]["points"][b]
            n = sum(c.values())
            if not n: continue
            rows.append([g, D_NAMES[b], n, share(c, "swing"), share(c, "meal"), share(c, "tab"), share(c, "none"), f"{tally[g]['points_k'][b] / n:.3f}"])
    out.append(table(["group", "D", "points", "swing", "meal", "tab", "none", "mean K"], rows))
    out.append("")
    out.append("(c) decision points by D (below / at least .25) and his own K: what comes first")
    rows = []
    for g in groups:
        for hi in (False, True):
            for b in range(3):
                c = tally[g]["joint"][(hi, b)]
                n = sum(c.values())
                if n: rows.append([g, "D >= .25" if hi else "D < .25", K_NAMES[b], n, share(c, "swing"), share(c, "meal"), share(c, "tab"), share(c, "none")])
    out.append(table(["group", "D", "K", "points", "swing", "meal", "tab", "none"], rows))
    out.append("")
    out.append(f"(d) per fighter, decision points at D >= .10: raw share swinging first, p10 / p25 / median / p75 / p90 (fighters with >= {MIN_POINTS}); the spread "
               f"that repeats across his duels, state-adjusted (fighters with >= {HALF_MIN} points in each half)")
    rows = []
    for g in groups:
        counts = [c for c in tally[g]["per_fighter"].values() if sum(c.values()) >= MIN_POINTS]
        sh = [c["swing"] / sum(c.values()) for c in counts]
        n, sd, ci, r = split_half(tally[g]["points_list"])
        rows.append([g, len(sh), quantiles(sh), n, f"{sd:.3f} [{ci[0]:.3f}, {ci[1]:.3f}]" if sd is not None else "-", f"{r:.2f}" if r is not None else "-"])
    out.append(table(["group", "fighters", "swing first (raw shares)", "fighters split", "repeatable SD [90%]", "split-half r"], rows))
    out.append("")
    out.append("(e) swings: mean D and K at a swing; at own hitpoints < 20 by a fresh read, the greataxe's share of his swings without a special, 90% interval over "
               "duels resampled")
    rows = []
    for g in groups:
        t = tally[g]
        duels = list(t["low_by_duel"].values())
        share_axe = lambda ds: sum(x[1] for x in ds) / max(1, sum(x[0] for x in ds))
        rng = random.Random(2)
        boots = sorted(share_axe([rng.choice(duels) for _ in duels]) for _ in range(500)) if duels else None
        k = t["low_axe_k"]
        rows.append([g, t["swings"], f"{t['swing_d'] / t['swings']:.3f}" if t["swings"] else "-", f"{t['swing_k'] / t['swings']:.3f}" if t["swings"] else "-",
                     sum(x[0] for x in duels), f"{share_axe(duels):.2f} [{boots[25]:.2f}, {boots[474]:.2f}]" if boots else "-",
                     f"{sum(k) / len(k):.3f}" if k else "-"])
    out.append(table(["group", "swings", "mean D", "mean K", "swings at < 20 hp (fresh)", "axe share", "axe swings' mean K"], rows))
    return "\n".join(out)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--laws", required=True)
    ap.add_argument("--duels", default=DEFAULT_DUELS)
    ap.add_argument("--names")
    ap.add_argument("sim", nargs="*")
    args = ap.parse_args()
    laws = Laws(args.laws)
    tally = collections.defaultdict(new_tally)
    counts = {}
    counts["real"] = read_corpus(args.duels, args.names or args.duels + ".names", lambda name: "real", laws, tally)
    for path in args.sim:
        names = path + ".names"
        if not os.path.exists(names): die(f"{names} missing: a sim corpus needs its names to tell its seats apart")
        net = any(line.split()[1] == "us" for line in open(names))
        group = (lambda name: "sim us" if name == "us" else "sim oppfit v net" if name == "oppfit" else "sim scripts v net") if net else \
            (lambda name: "sim scripts v scripts")
        counts[os.path.basename(path)] = read_corpus(path, names, group, laws, tally)
    print(report(tally, counts))
    if laws.unknown_weapons: print(f"\nweapons with no item row (their options left out): {dict(laws.unknown_weapons)}")
    print(f"composition slots with no item row, worn as empty, over all laws asked: {laws.unknown_items}")


if __name__ == "__main__":
    main()
