#!/usr/bin/env python3
"""How much spectated Dharok fighters differ from each other, behaviour by behaviour, beyond the noise of their counts (riskfight_policy.h's Dharok personas).

Usage: python3 scripts/fit_dharok_personas.py [--duels FILE] [--names FILE] [--boot N]      (python3 standard library only; deterministic)

Input: spectator.duels (see measure_dharok_play.py), Dharok v Dharok duels only, both sides; rows marked third-party skipped. --names (default FILE.names)
ties a duel side to a fighter across duels (`duel_id hash_a hash_b`); both files come from the light repo's export:
    python3 tools/spectator_export.py ~/.light2/c-engine/spectator.duels --names ~/.light2/c-engine/spectator.duels.names

Behaviours (each an event with a yes/no outcome y in a cell of the state the script's table conditions on; the pooled rate of the cell is p):
- meal_low: after the other side's hit leaves him below 60 hitpoints (bands 0-2 of fit_dharok_meals.py), NOT healed (bar + damage 8 over) by that side's next hit;
  cell (band, ticks between the hits). The miss rate, so a multiplier above 1 is a slow eater.
- meal_mid: the same at 59-79 hitpoints (bands 3-4), healed by the next hit.
- uptime: a tick his weapon is ready (ticks since his swing >= its speed, no eat animation in the 3 ticks up to it), a swing on it; cell (ticks since ready
  0, 1, 2, 3+; his bar band).
- vw_spec: a tick with the other side's bar 25 or under (RF_VW_SPEC_BAR_MAX), for a fighter seen holding a Voidwaker in the duel, a Voidwaker special on it;
  cell the other side's bar 0-9, 10-19, 20-25.
- axe: a swing without a special, the greataxe animation; cell RF_AXE_SWING_PM's (his bar band the tick before, Vengeance up, the other side's bar band).
- orb: a tick the orb can be told from his hit (measure_dharok_play.py (f)), a lone 10; cell RF_DHAROK_ORB_PM's (axe in hand, hitpoints / 20).
- tab: a tick before the fight's end, his teleport animation; cell his hitpoints / 20 (0-19, 20-39, 40-59, 60 up) on the carried bar.
- big_meal: a gap between his hitsplats under 80 hitpoints that healed 8 or more, a heal of 33 or more (a double or triple meal, RF_DHAROK_MEAL_DOUBLE_PM);
  cell hitpoints / 20. (Shares its gaps with meal_low / meal_mid: their correlation with it carries sampling noise of their own.)
Per fighter g: O = sum y, E = sum p, V = sum p (1 - p) over his events. A fighter's multiplier m on the pooled rates has mean 1 and between-fighter variance
tau2 = (sum (O - E)^2 - sum V) / sum E^2 (moments); its empirical-Bayes estimate is 1 + tau2 E (O - E) / (tau2 E^2 + V). Dispersion: Pearson X2 = sum (O - E)^2 / V
against its null by parametric bootstrap (each cell's y redrawn binomial at p). Correlations of two behaviours' multipliers: sum (Oa - Ea)(Ob - Eb) / sum Ea Eb
over tau_a tau_b. Persistence: each fighter's duels split alternately in time order, the cross moment of the two halves over E1 E2 is the variance that persists
across fights; over tau2 of fighter-duels it is the share of a fighter-duel's spread that is his and not the fight's. CIs: bootstrap over fighters.
"""
import argparse
import collections
import math
import os
import random
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "scripts"))
from measure_dharok_play import parse, Side, BAR, DMG, NHIT, ANIMS, DEAD, SPEC, WEAPON_FIELD, VOIDWAKER  # noqa: E402
from mine_riskfight_pool import DEFAULT_DUELS, SWING_ANIMS  # noqa: E402
from fit_dharok_meals import SWING_SPEED, INSTANT_ANIMS, band_of, STATUS_L, HEALED_MIN  # noqa: E402
from fit_dharok_tab import TELEPORT_ANIMS  # noqa: E402

EAT = 2
BEHAVIOURS = ("meal_low", "meal_mid", "eat", "uptime", "vw_spec", "axe", "orb", "tab", "big_meal")
OUTCOMES = ("ko_dealt", "ko_taken", "escaped")
EAT_BLOCK = 3      # ticks after an eat animation in which a ready tick is not counted (the meal delays his swing)
BIG_MEAL = 33      # heal of a double meal or more
VW_BAR_MAX = 25    # riskfight_policy.h RF_VW_SPEC_BAR_MAX


def anims(field):
    return [] if field == "-" else [int(a) for a in field.split("|")]


def units(duels):
    """Per Dharok v Dharok fighter-duel: (duel id, side, start tick, {behaviour: {cell: [n, y]}}, {outcome: 0/1}, {behaviour: spec bars})."""
    for d in duels:
        sides = (Side(d, 0), Side(d, 1))
        if not (sides[0].is_dharok() and sides[1].is_dharok()):
            continue
        rows = d["rows"]
        n = len(rows)
        dead = [next((i for i in range(n) if rows[i][2 + v][DEAD] == "1"), None) for v in (0, 1)]
        tele = [next((i for i in range(n) if any(a in TELEPORT_ANIMS for a in anims(rows[i][2 + v][ANIMS]))), None) for v in (0, 1)]
        ends = [x for x in dead + tele if x is not None]
        end = min(ends) if ends else n - 1
        first = next((i for i in range(n) if sides[0].dmg[i] + sides[1].dmg[i] > 0), None)
        for v in (0, 1):
            s, k = sides[v], 1 - v
            mine, his = [r[2 + v] for r in rows], [r[2 + k] for r in rows]
            cells = {b: collections.defaultdict(lambda: [0, 0]) for b in BEHAVIOURS}

            def add(b, cell, y):
                c = cells[b][cell]
                c[0] += 1
                c[1] += int(bool(y))

            # meals (fit_dharok_meals.current_status) and meal size
            swing = [any(x in SWING_ANIMS for x in anims(r[ANIMS])) for r in his]
            fresh = [t for t in range(1, end + 1) if int(mine[t][NHIT]) > 0 and int(mine[t][BAR]) >= 0 and not rows[t][1]]
            for a, b in zip(fresh, fresh[1:]):
                if mine[a][DEAD] == "1" or int(mine[a][DMG]) <= 0: continue
                hp = int(mine[a][BAR]) * 99 / 30
                heal = int(mine[b][BAR]) * 99 / 30 + int(mine[b][DMG]) - hp
                if 0 < hp < 80 and heal >= HEALED_MIN and not any(rows[t][1] for t in range(a, b + 1)):
                    add("big_meal", int(hp // 20), heal >= BIG_MEAL)
                sa = a if swing[a] else a - 1 if swing[a - 1] else None
                sb = b if swing[b] else b - 1 if swing[b - 1] else None
                if sa is None or sb is None or sb <= sa or hp <= 0: continue
                band, L = band_of(hp), min(sb - sa, STATUS_L)
                if band <= 2: add("meal_low", (band, L), heal < HEALED_MIN)
                elif band <= 4: add("meal_mid", (band, L), heal >= HEALED_MIN)
                if band <= 4: add("eat", (band, L), heal >= HEALED_MIN)
            # uptime, specs, axe, tab over the fight (first hit to its end)
            if first is None: continue
            vw = any(r[WEAPON_FIELD] == str(VOIDWAKER) or r[SPEC] == "VW" for r in mine[:end + 1])
            last, speed, ate = None, 4, -99
            spec_bars = []
            for i in range(max(1, first), end + 1):
                third = rows[i][1]
                own = [x for x in anims(mine[i][ANIMS]) if x in SWING_ANIMS]
                if mine[i][EAT] == "1": ate = i
                if not third:
                    if last is not None and i - last >= speed and i - ate > EAT_BLOCK:
                        add("uptime", (min(i - last - speed, 3), (s.bar[i - 1] >= 12) + (s.bar[i - 1] >= 24)), bool(own))
                    if vw and s.opp_bar[i - 1] <= VW_BAR_MAX:
                        add("vw_spec", (s.opp_bar[i - 1] >= 10) + (s.opp_bar[i - 1] >= 20), mine[i][SPEC] == "VW")
                    if mine[i][SPEC] != "-": spec_bars.append(s.opp_bar[i - 1])
                    if s.swing[i] and not s.special[i]:
                        add("axe", ((s.bar[i - 1] >= 12) + (s.bar[i - 1] >= 24), s.veng_up[i], (s.opp_bar[i - 1] >= 10) + (s.opp_bar[i - 1] >= 20)), s.axe_swing[i])
                    add("tab", min(3, int(s.bar[i - 1] * 99 / 30) // 20), tele[v] == i)
                    if i < n - 1 and not (any(s.opp_swing[i - 1:i + 2]) or s.opp_veng_proc[i - 1] or s.opp_veng_proc[i]):
                        add("orb", (s.axe(i - 1), min(4, int(s.bar[i - 1] * 99 / 30) // 20)), s.nhit[i] == 1 and s.dmg[i] == 10)
                swings = [x for x in own if x not in INSTANT_ANIMS]
                if swings: last, speed = i, SWING_SPEED.get(swings[0], 4)
            outcome = {"ko_dealt": int(dead[k] is not None and dead[k] == end), "ko_taken": int(dead[v] is not None and dead[v] == end),
                       "escaped": int(tele[v] is not None and tele[v] == end)}
            yield d["id"], v, rows[0][0], cells, outcome, spec_bars


class Data:
    def __init__(self, fds, names):
        self.fds = fds  # list of (fighter, order key, cells, outcome)
        self.pooled = {}
        for b in BEHAVIOURS:
            tot = collections.defaultdict(lambda: [0, 0])
            for _, _, cells, _ in fds:
                for c, (n, y) in cells[b].items():
                    tot[c][0] += n
                    tot[c][1] += y
            self.pooled[b] = {c: y / n for c, (n, y) in tot.items()}

    def oev(self, b, group_of):
        """{group: [O, E, V, n]} for behaviour b."""
        out = collections.defaultdict(lambda: [0.0, 0.0, 0.0, 0])
        p = self.pooled[b]
        for fd in self.fds:
            g = group_of(fd)
            if g is None: continue
            acc = out[g]
            for c, (n, y) in fd[2][b].items():
                acc[0] += y
                acc[1] += n * p[c]
                acc[2] += n * p[c] * (1 - p[c])
                acc[3] += n
        return out


MEAL_H = (0.589, 0.572, 0.468, 0.241, 0.036)  # riskfight_policy.h RF_DHAROK_MEAL_PM bands 0-4 (fit_dharok_meals.py)
PERSONA_TRAITS = ("eat", "big_meal", "axe")  # riskfight_personas.h columns, in this order
THETA = [i / 50 - 4 for i in range(401)]     # the log-scale grid a fighter's loglik is tabulated on
NODES = [i / 4 - 4 for i in range(33)]
_w = [math.exp(-z * z / 2) for z in NODES]
WEIGHTS = [w / sum(_w) for w in _w]


def prob(trait, cell, p, th):
    """A fighter's event probability at log-scale offset th: the meal hazard's log-survival times e^th (eat), else the odds times e^th."""
    if trait == "eat":
        band, L = cell
        return 1 - (1 - MEAL_H[band]) ** (L * math.exp(th))
    p = min(max(p, 1e-6), 1 - 1e-6)
    odds = p / (1 - p) * math.exp(th)
    return odds / (1 + odds)


def latent(data, trait):
    """Random-effect fit, th ~ N(mu, sigma^2) per fighter: (mu, sigma, {fighter: posterior mean th}) by marginal likelihood on a grid."""
    p = data.pooled[trait]
    per = collections.defaultdict(lambda: collections.defaultdict(lambda: [0, 0]))
    for f, _, cells, _ in data.fds:
        for c, (n, y) in cells[trait].items():
            per[f][c][0] += n
            per[f][c][1] += y
    ll = {}
    for f, cells in per.items():
        row = []
        for th in THETA:
            s = 0.0
            for c, (n, y) in cells.items():
                q = min(max(prob(trait, c, p[c], th), 1e-9), 1 - 1e-9)
                s += y * math.log(q) + (n - y) * math.log(1 - q)
            row.append(s)
        top = max(row)
        ll[f] = (top, [math.exp(x - top) for x in row])

    def at(f, th):
        i = min(400, max(0, round((th + 4) * 50)))
        return ll[f][1][i]

    def marginal(mu, sigma):
        return sum(ll[f][0] + math.log(max(1e-300, sum(w * at(f, mu + sigma * z) for z, w in zip(NODES, WEIGHTS)))) for f in ll)
    best = max(((marginal(mu / 25, sg / 25), mu / 25, sg / 25) for mu in range(-15, 16) for sg in range(0, 41)))
    _, mu, sigma = best
    post = {}
    for f in ll:
        ws = [w * at(f, mu + sigma * z) for z, w in zip(NODES, WEIGHTS)]
        post[f] = sum(x * (mu + sigma * z) for x, z in zip(ws, NODES)) / sum(ws)
    return mu, sigma, post


def personas(data, fights, shares):
    """Per fighter: (fights, {trait: th}). The posterior means are spread back to the persistent variance sigma^2 x min(1, share) about mu (a fighter's
    shrunk estimate alone is narrower than the fighters are), whole vectors kept, so the traits keep their correlations."""
    out = {f: {} for f in fights}
    fitted = {}
    for trait in PERSONA_TRAITS:
        mu, sigma, post = latent(data, trait)
        vals = {f: post.get(f, mu) for f in fights}
        w = sum(fights.values())
        mean = sum(vals[f] * fights[f] for f in fights) / w
        var = sum((vals[f] - mean) ** 2 * fights[f] for f in fights) / w
        target = sigma ** 2 * min(1.0, max(0.0, shares[trait]))
        scale = math.sqrt(target / var) if var > 0 else 0.0
        for f in fights: out[f][trait] = mu + scale * (vals[f] - mean)
        fitted[trait] = (mu, sigma, scale)
    return out, fitted


def wcorr(xs, ys, ws):
    w = sum(ws)
    mx, my = sum(x * v for x, v in zip(xs, ws)) / w, sum(y * v for y, v in zip(ys, ws)) / w
    sxy = sum(v * (x - mx) * (y - my) for x, y, v in zip(xs, ys, ws))
    sxx, syy = sum(v * (x - mx) ** 2 for x, v in zip(xs, ws)), sum(v * (y - my) ** 2 for y, v in zip(ys, ws))
    return sxy / math.sqrt(sxx * syy) if sxx > 0 and syy > 0 else float("nan")


def tau2(groups):
    num = sum((o - e) ** 2 - v for o, e, v, _ in groups)
    den = sum(e * e for _, e, _, _ in groups)
    return num / den if den else 0.0


def pearson_x2(groups):
    return sum((o - e) ** 2 / v for o, e, v, _ in groups if v > 0)


def null_x2(data, b, group_of, rng, reps):
    p = data.pooled[b]
    draws = []
    for _ in range(reps):
        acc = collections.defaultdict(lambda: [0.0, 0.0, 0.0, 0])
        for fd in data.fds:
            g = group_of(fd)
            if g is None: continue
            a = acc[g]
            for c, (n, _) in fd[2][b].items():
                a[0] += rng.binomialvariate(n, p[c])
                a[1] += n * p[c]
                a[2] += n * p[c] * (1 - p[c])
        draws.append(pearson_x2(acc.values()))
    return draws


def eb(o, e, v, t2):
    return 1 + t2 * e * (o - e) / (t2 * e * e + v) if e > 0 and t2 > 0 else 1.0


def quantiles(xs, qs=(0.05, 0.95)):
    xs = sorted(xs)
    return [xs[min(len(xs) - 1, int(q * len(xs)))] for q in qs]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--duels", default=DEFAULT_DUELS)
    ap.add_argument("--names")
    ap.add_argument("--boot", type=int, default=200)
    ap.add_argument("--table", help="write riskfight_personas.h here")
    args = ap.parse_args()
    names_path = args.names or args.duels + ".names"
    names = {}
    with open(names_path) as fh:
        for line in fh:
            k, a, b = line.split()
            names[k] = (a, b)
    rng = random.Random(1)
    fds = []
    for did, v, start, cells, outcome, spec_bars in units(parse(args.duels)):
        rec = did.split(".")[0]
        fds.append((names[did][v], (rec, start), cells, outcome))
    data = Data(fds, names)
    by_fd = {id(fd): i for i, fd in enumerate(fds)}
    group_fd = lambda fd: by_fd[id(fd)]
    group_f = lambda fd: fd[0]
    fights = collections.Counter(fd[0] for fd in fds)
    order = collections.defaultdict(list)
    for fd in fds: order[fd[0]].append(fd)
    half = {}
    for f, lst in order.items():
        for j, fd in enumerate(sorted(lst, key=lambda x: x[1])): half[id(fd)] = j % 2
    print(f"{len(fds)} Dharok v Dharok fighter-duels, {len(fights)} fighters, {sum(c >= 2 for c in fights.values())} with 2+ fights")
    fighters = sorted(fights)
    G = {b: data.oev(b, group_f) for b in BEHAVIOURS}
    T = {b: tau2(G[b].values()) for b in BEHAVIOURS}
    print("| behaviour | events | fighters | pooled rate | X2 / null mean (p) | sd of multiplier, fighters [90% CI] | fighter-duels | persistent share [90% CI] |")
    print("|---|---|---|---|---|---|---|---|")
    boots = [[rng.choice(fighters) for _ in fighters] for _ in range(args.boot)]
    shares = {}
    for b in BEHAVIOURS:
        groups = G[b]
        live = {g: x for g, x in groups.items() if x[3]}
        x2 = pearson_x2(live.values())
        null = null_x2(data, b, group_f, rng, args.boot)
        pval = (1 + sum(z >= x2 for z in null)) / (1 + len(null))
        bt = [tau2([groups[g] for g in s if groups[g][3]]) for s in boots]
        t2_fd = tau2([x for x in data.oev(b, group_fd).values() if x[3]])
        hv = [data.oev(b, lambda fd, h=h: fd[0] if half[id(fd)] == h else None) for h in (0, 1)]

        def persist(sample):
            num = sum((hv[0][g][0] - hv[0][g][1]) * (hv[1][g][0] - hv[1][g][1]) for g in sample if g in hv[0] and g in hv[1])
            den = sum(hv[0][g][1] * hv[1][g][1] for g in sample if g in hv[0] and g in hv[1])
            return num / den if den else 0.0
        share = persist(fighters) / t2_fd if t2_fd > 0 else float("nan")
        shares[b] = share if share == share else 0.0
        bs = [persist(s) / t2_fd if t2_fd > 0 else float("nan") for s in boots]
        events = sum(x[3] for x in groups.values())
        rate = sum(x[0] for x in groups.values()) / events
        sd = lambda t: math.sqrt(max(0.0, t))
        lo, hi = quantiles(bt)
        slo, shi = quantiles(bs)
        print(f"| {b} | {events} | {len(live)} | {rate:.4f} | {x2 / (sum(null) / len(null)):.2f} ({pval:.3f}) | {sd(T[b]):.2f} [{sd(lo):.2f}, {sd(hi):.2f}] | "
              f"{sd(t2_fd):.2f} | {share:.2f} [{slo:.2f}, {shi:.2f}] |")
    hetero = [b for b in BEHAVIOURS if T[b] > 0]
    m = {b: {f: eb(*G[b][f][:3], T[b]) for f in fighters if G[b][f][3]} for b in hetero}
    print("correlation of the fighters' empirical-Bayes multipliers (fights-weighted Pearson r over fighters with both, 90% bootstrap CI over fighters; the "
          "shrinkage pulls it toward 0)")
    for i, a in enumerate(hetero):
        for b in hetero[i + 1:]:
            def corr(sample):
                xs = [f for f in sample if f in m[a] and f in m[b]]
                return wcorr([m[a][f] for f in xs], [m[b][f] for f in xs], [fights[f] for f in xs])
            bs = [x for x in (corr(s) for s in boots) if x == x]
            lo, hi = quantiles(bs)
            print(f"  {a} ~ {b}: {corr(fighters):+.2f} [{lo:+.2f}, {hi:+.2f}]")
    print("outcomes per fight against the fighter's empirical-Bayes multiplier (fights-weighted Pearson r, 90% bootstrap CI)")
    outc = {f: [sum(fd[3][o] for fd in order[f]) for o in OUTCOMES] for f in fighters}
    base = [sum(outc[f][j] for f in fighters) / len(fds) for j in range(len(OUTCOMES))]
    print("  pooled per fight: " + ", ".join(f"{o} {base[j]:.3f}" for j, o in enumerate(OUTCOMES)))
    for b in hetero:
        cells = []
        for j, o in enumerate(OUTCOMES):
            def wr(sample):
                xs = [f for f in sample if f in m[b]]
                return wcorr([m[b][f] for f in xs], [outc[f][j] / fights[f] for f in xs], [fights[f] for f in xs])
            bs = [x for x in (wr(s) for s in boots) if x == x]
            lo, hi = quantiles(bs)
            cells.append(f"{o} {wr(fighters):+.2f} [{lo:+.2f}, {hi:+.2f}]")
        print(f"  {b}: " + "; ".join(cells))
    vec, fitted = personas(data, fights, shares)
    print("personas: th ~ N(mu, sigma^2) per fighter by marginal likelihood; spread back to sigma x sqrt(min(1, persistent share)) (eat: log-survival "
          "exponent of RF_DHAROK_MEAL_PM bands 0-4; big_meal: odds of a double or triple meal; axe: odds of RF_AXE_SWING_PM)")
    for t in PERSONA_TRAITS:
        mu, sigma, scale = fitted[t]
        xs = sorted(math.exp(vec[f][t]) for f in fighters)
        print(f"  {t}: mu {mu:+.2f} sigma {sigma:.2f} spread x{scale:.2f}; multiplier p10 / p50 / p90 {xs[len(xs) // 10]:.2f} / {xs[len(xs) // 2]:.2f} / "
              f"{xs[9 * len(xs) // 10]:.2f}")
    for i, a in enumerate(PERSONA_TRAITS):
        for b in PERSONA_TRAITS[i + 1:]:
            print(f"  persona correlation {a} ~ {b}: {wcorr([vec[f][a] for f in fighters], [vec[f][b] for f in fighters], [fights[f] for f in fighters]):+.2f}")
    if args.table:
        rows, cum = [], 0
        for f in fighters:
            cum += fights[f]
            rows.append("{" + ", ".join(str(x) for x in [cum] + [round(1000 * math.exp(vec[f][t])) for t in PERSONA_TRAITS]) + "}")
        with open(args.table, "w") as fh:
            fh.write("#ifndef OSRS_RISKFIGHT_PERSONAS_H\n#define OSRS_RISKFIGHT_PERSONAS_H\n")
            fh.write(f"/** Generated by scripts/fit_dharok_personas.py from {os.path.basename(args.duels)}: one row per spectated Dharok fighter ({len(fighters)} "
                     f"fighters of {len(fds)} Dharok v Dharok fighter-duels), drawn in proportion to his fights. Columns: cumulative fights, then per mille "
                     "multipliers (RiskfightDharokPersonaTrait). */\n")
            fh.write(f"enum {{ RF_DHAROK_PERSONAS = {len(rows)} }};\n")
            fh.write("static const uint16_t RF_DHAROK_PERSONA[RF_DHAROK_PERSONAS][4] = {\n")
            for i in range(0, len(rows), 6):
                fh.write("    " + ", ".join(rows[i:i + 6]) + ",\n")
            fh.write("};\n#endif\n")


if __name__ == "__main__":
    main()
