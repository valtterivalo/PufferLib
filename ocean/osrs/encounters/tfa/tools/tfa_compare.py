"""Compare boss laws between recorded fixtures and a sim dump in the same TSV schema; exit 1 when a law's mode differs."""

import collections
import csv
import pathlib
import sys

FIXTURES = pathlib.Path(__file__).resolve().parent.parent / "fixtures"
ATTACKS = {
    "guardian": {"14751", "14753", "14754", "14752", "14750"},
    "xarth": {"14983", "14984", "14985", "14992", "14989"},
    "bork": {"14542", "14550", "14551", "14554", "14555"},
    "moia": {"14785", "14788"},
}


def load(path):
    fights = collections.defaultdict(list)
    with open(path) as f:
        for row in csv.DictReader(f, delimiter="\t"):
            row["t"] = int(row["t"])
            fights[row["fight_id"]].append(row)
    return fights


def base(actor):
    return actor.split("#")[0]


def hist(xs):
    return collections.Counter(xs)


def fmt(c, k=4):
    total = sum(c.values())
    return " ".join(f"{v}:{n}" for v, n in c.most_common(k)) + f" (n={total})" if total else "-"


def next_attack(fights, boss, anim):
    out = []
    for ev in fights.values():
        anims = [e for e in ev if e["kind"] == "ANIM" and (e["id"] in ATTACKS[boss] or e["id"] == anim)]
        for a, b in zip(anims, anims[1:]):
            if a["id"] == anim and b["actor"].split("#")[1] == a["actor"].split("#")[1]:
                out.append(b["t"] - a["t"])
    return hist(out)


def anim_to_p0_hit(fights, anim, window=8):
    out = []
    for ev in fights.values():
        hits = [e for e in ev if e["kind"] == "HIT" and e["actor"] == "P0" and e["a"] != "65"]
        for a in (e for e in ev if e["kind"] == "ANIM" and e["id"] == anim):
            h = next((h for h in hits if a["t"] <= h["t"] <= a["t"] + window), None)
            if h:
                out.append(h["t"] - a["t"])
    return hist(out)


def targeted_hit_delay(fights, anim, window=8):
    """Delay from an attack anim to the next P0 hit, for anims whose projectile (real) or target (sim) is P0."""
    out = []
    for ev in fights.values():
        hits = [e for e in ev if e["kind"] == "HIT" and e["actor"] == "P0" and e["a"] in ("16", "12")]
        for a in (e for e in ev if e["kind"] == "ANIM" and e["id"] == anim):
            aimed = any(
                (e["kind"] == "PROJ" and e["actor"] == a["actor"] and 0 <= e["t"] - a["t"] <= 3 and e["a"] == "P0")
                or (e["kind"] == "TGT" and e["actor"] == a["actor"] and e["t"] == a["t"] and e["a"] == "P0")
                for e in ev if abs(e["t"] - a["t"]) <= 3)
            if not aimed:
                continue
            h = next((h for h in hits if a["t"] <= h["t"] <= a["t"] + window), None)
            if h:
                out.append(h["t"] - a["t"])
    return hist(out)


def median_law(name, real, sim, tol, failures):
    r = sorted(real)
    m = sorted(sim)
    ok = bool(r) and bool(m) and abs(r[len(r) // 2] - m[len(m) // 2]) <= tol
    if not ok:
        failures.append(name)
    print(f"{'ok ' if ok else 'BAD'} {name:28s} real median {r[len(r) // 2] if r else '-'} (n={len(r)}){'':22s} sim median {m[len(m) // 2] if m else '-'} (n={len(m)})")


def ground_durations(fights):
    out = collections.defaultdict(list)
    for ev in fights.values():
        kd = [e["t"] for e in ev if e["kind"] == "ANIM" and e["id"] == "14747"]
        rise = [e["t"] for e in ev if e["kind"] == "ANIM" and e["id"] == "14748"]
        hp = [(e["t"], int(e["id"])) for e in ev if e["kind"] == "NHP" and base(e["actor"]) == "16660"]
        for i, k in enumerate(kd):
            r = next((x for x in rise if x > k), None)
            if r is None:
                continue
            crossed = any(v <= 33 for t, v in hp if k <= t < r)
            out["cross" if crossed else f"kd{i + 1}"].append(r - k)
    return {k: hist(v) for k, v in out.items()}


def vines(fights):
    first, gap, offs, life = [], [], [], []
    for ev in fights.values():
        kd = [e["t"] for e in ev if e["kind"] == "ANIM" and e["id"] == "14747"]
        sp = [e for e in ev if e["kind"] == "NPC+" and e["id"] == "16664"]
        ts = sorted({e["t"] for e in sp})
        gap += [b - a for a, b in zip(ts, ts[1:]) if b - a < 10]
        for k in kd:
            f = next((t for t in ts if t >= k), None)
            if f is not None and f - k < 20:
                first.append(f - k)
        for s in sp:
            an = tuple(e["t"] - s["t"] for e in ev if e["kind"] == "ANIM" and e["actor"] == s["actor"] and e["id"] == "14768" and e["t"] >= s["t"])
            offs.append(an[:3])
            de = next((e["t"] for e in ev if e["kind"] == "NPC-" and e["actor"] == s["actor"] and e["t"] >= s["t"]), None)
            if de is not None:
                life.append(de - s["t"])
    return hist(first), hist(gap), hist(offs), hist(life)


def mushrooms(fights):
    per, arm = [], []
    for ev in fights.values():
        for t in (e["t"] for e in ev if e["kind"] == "ANIM" and e["id"] == "14753"):
            per.append(sum(1 for e in ev if e["kind"] == "OBJ+" and e["id"] == "62578" and e["t"] == t))
        sprout = collections.defaultdict(list)
        for e in ev:
            if e["kind"] == "OBJ+" and e["id"] in ("62578", "62579"):
                sprout[(e["x"], e["y"])].append((e["t"], e["id"]))
        for lst in sprout.values():
            for (t0, a), (t1, b) in zip(lst, lst[1:]):
                if a == "62578" and b == "62579":
                    arm.append(t1 - t0)
    return hist(per), hist(arm)


def breath(fights):
    split = []
    for ev in fights.values():
        for b in (e["t"] for e in ev if e["kind"] == "ANIM" and e["id"] == "14754"):
            hs = [int(h["id"]) for h in ev if h["kind"] == "HIT" and h["actor"].startswith("P") and h["t"] == b + 10 and h["a"] != "65"]
            if hs:
                split.append(sum(hs) // 10 * 10)
    return hist(split)


def shields(fights):
    out = []
    for ev in fights.values():
        cum, start = 0, None
        for e in ev:
            if e["kind"] == "HIT" and base(e["actor"]) == "16659":
                cum += int(e["id"])
            if e["kind"] == "ANIM" and e["id"] == "14747":
                out.append(cum)
                cum = 0
            if e["kind"] == "ANIM" and e["id"] == "14748":
                cum = 0
    return hist(out)


def compare(name, real, sim, failures):
    if sum(real.values()) < 3:
        print(f"--  {name:28s} real {fmt(real):42s} sim {fmt(sim)}")
        return
    ok = bool(sim) and real.most_common(1)[0][0] == sim.most_common(1)[0][0]
    if not ok:
        failures.append(name)
    print(f"{'ok ' if ok else 'BAD'} {name:28s} real {fmt(real):42s} sim {fmt(sim)}")


def guardian(real, sim):
    failures = []
    for anim in ("14751", "14753", "14754", "14748"):
        compare(f"{anim} next attack", next_attack(real, "guardian", anim), next_attack(sim, "guardian", anim), failures)
    compare("14751 P0 hit delay", anim_to_p0_hit(real, "14751"), anim_to_p0_hit(sim, "14751"), failures)
    rg, sg = ground_durations(real), ground_durations(sim)
    for k in sorted(set(rg) | set(sg)):
        compare(f"ground {k}", rg.get(k, hist([])), sg.get(k, hist([])), failures)
    for label, r, s in zip(("vine first", "vine gap", "vine anims", "vine life"), vines(real), vines(sim)):
        compare(label, r, s, failures)
    for label, r, s in zip(("mushrooms per special", "mushroom arm"), mushrooms(real), mushrooms(sim)):
        compare(label, r, s, failures)
    compare("breath total (/10)", breath(real), breath(sim), failures)
    compare("shield per flying phase", shields(real), shields(sim), failures)
    return failures


def schedule(fights, anim):
    out = []
    for ev in fights.values():
        anims = [e for e in ev if e["kind"] == "ANIM" and base(e["actor"]) == "16684" and e["id"] in ATTACKS["xarth"]]
        if not anims:
            continue
        t0 = anims[0]["t"]
        out += [e["t"] - t0 for e in anims if e["id"] == anim]
    return out


def first_of(fights, anim):
    out = []
    for ev in fights.values():
        anims = [e for e in ev if e["kind"] == "ANIM" and base(e["actor"]) == "16684" and e["id"] in ATTACKS["xarth"]]
        if anims:
            out.append(next((e["t"] - anims[0]["t"] for e in anims if e["id"] == anim), None))
    return hist(out)


def pillar_breaks(fights):
    out = []
    for ev in fights.values():
        aoes = [e["t"] for e in ev if e["kind"] == "ANIM" and e["id"] == "14992"]
        for e in ev:
            if e["kind"] == "NPCID" and e["id"] == "16686" or (e["kind"] == "NPC+" and e["id"] == "16686"):
                a = max((t for t in aoes if t <= e["t"]), default=None)
                if a is not None:
                    out.append(e["t"] - a)
    return hist(out)


def corpses(fights):
    gaps, kills, steps = [], [], []
    for ev in fights.values():
        ts = sorted(e["t"] for e in ev if e["kind"] == "NPC+" and e["id"] == "16687")
        gaps += [b - a for a, b in zip(ts, ts[1:])]
        hp = collections.Counter()
        pos = collections.defaultdict(list)
        life = collections.Counter()
        for e in ev:
            if e["kind"] == "NPC+" and e["id"] == "16687":
                life[e["actor"]] += 1
            if e["kind"] == "HIT" and base(e["actor"]) == "16687":
                hp[(e["actor"], life[e["actor"]])] += int(e["id"])
            if e["kind"] == "NPOS" and base(e["actor"]) == "16687":
                pos[e["actor"]].append((e["t"], int(e["x"]), int(e["y"])))
        kills += [v for v in hp.values() if v > 0]
        for p in pos.values():
            steps += [max(abs(b[1] - a[1]), abs(b[2] - a[2])) for a, b in zip(p, p[1:]) if b[0] == a[0] + 1]
    return hist(gaps), hist(kills), hist(steps)


def blast_hits(fights):
    off, amounts = [], []
    for ev in fights.values():
        for b in (e["t"] for e in ev if e["kind"] == "ANIM" and e["id"] == "14989"):
            hs = [h for h in ev if h["kind"] == "HIT" and h["actor"].startswith("P") and h["t"] == b + 3 and h["a"] in ("16", "17") and int(h["id"]) >= 40]
            amounts += [int(h["id"]) for h in hs]
    return amounts


def xarth(real, sim):
    failures = []
    for anim in ("14984", "14985", "14992", "14989"):
        compare(f"{anim} next attack", next_attack(real, "xarth", anim), next_attack(sim, "xarth", anim), failures)
    compare("AOE schedule", hist(schedule(real, "14992")), hist(schedule(sim, "14992")), failures)
    compare("first ranged", first_of(real, "14985"), first_of(sim, "14985"), failures)
    compare("14985 aimed P0 hit delay", targeted_hit_delay(real, "14985"), targeted_hit_delay(sim, "14985"), failures)
    compare("14984 aimed P0 hit delay", targeted_hit_delay(real, "14984"), targeted_hit_delay(sim, "14984"), failures)
    compare("pillar break after AOE", pillar_breaks(real), pillar_breaks(sim), failures)
    rg, rk, rs = corpses(real)
    sg, sk, ss = corpses(sim)
    compare("corpse spawn gap", rg, sg, failures)
    compare("corpse step", rs, ss, failures)
    ok = max(sk) <= max(v for v, n in rk.items() if n >= 3)
    if not ok:
        failures.append("corpse hp")
    print(f"{'ok ' if ok else 'BAD'} {'corpse hp (max damage taken)':28s} real {max(v for v, n in rk.items() if n >= 3):<42} sim {max(sk)}")
    median_law("blast damage", blast_hits(real), blast_hits(sim), 3, failures)
    return failures


def exact(name, real, sim, failures):
    """A single-sample law: the real mode must be the sim mode whatever n is."""
    if not real:
        return
    ok = bool(sim) and real.most_common(1)[0][0] == sim.most_common(1)[0][0]
    if not ok:
        failures.append(name)
    print(f"{'ok ' if ok else 'BAD'} {name:28s} real {fmt(real):42s} sim {fmt(sim)}")


def chain_gap(fights, a_id, b_id, actor_id):
    out = []
    for ev in fights.values():
        anims = [e for e in ev if e["kind"] == "ANIM" and base(e["actor"]) == actor_id]
        for a, b in zip(anims, anims[1:]):
            if a["id"] == a_id and b["id"] == b_id:
                out.append(b["t"] - a["t"])
    return hist(out)


def stomp_offsets(fights):
    out = []
    for ev in fights.values():
        bombs = [e["t"] for e in ev if e["kind"] == "ANIM" and e["id"] == "14555"]
        stomps = [e["t"] for e in ev if e["kind"] == "ANIM" and e["id"] == "14550"]
        out += [min(t for t in stomps if t > b) - b for b in bombs if any(t > b for t in stomps)]
    return hist(out)


def proj_flights(fights, proj):
    return hist([e["b"] for ev in fights.values() for e in ev if e["kind"] == "PROJ" and e["id"] == proj])


def smork_gaps(fights):
    out = []
    for ev in fights.values():
        ts = [e["t"] for e in ev if e["kind"] == "ANIM" and e["id"] in ("14554", "14555")]
        out += [b - a for a, b in zip(ts, ts[1:])]
    return out


def gong_hit_delays(fights):
    out = []
    for ev in fights.values():
        hits = [e for e in ev if e["kind"] == "HIT" and e["actor"].startswith("P") and e["a"] in ("16", "17") and int(e["id"]) >= 5]
        for g in (e["t"] for e in ev if e["kind"] == "ANIM" and e["id"] == "14542"):
            out += [h["t"] - g for h in hits if g <= h["t"] <= g + 6]
    return hist(out)


def scorch_delay(fights):
    out = []
    for ev in fights.values():
        objs = sorted(e["t"] for e in ev if e["kind"] == "OBJ+" and e["id"] == "62544")
        for p in (e["t"] for e in ev if e["kind"] == "PROJ" and e["id"] in ("4045", "4440")):
            nxt = next((t for t in objs if t >= p), None)
            if nxt is not None:
                out.append(nxt - p)
    return hist(out)


def scorch_counts(fights):
    out = []
    for ev in fights.values():
        c = collections.Counter(e["t"] for e in ev if e["kind"] == "OBJ+" and e["id"] == "62544")
        out += [c[t] for t in sorted(c)[:5]]
    return out


def shadowed(bx, by, x, y):
    px, py = bx + 2.0, by + 2.0
    dx, dy = x + 0.5 - px, y + 0.5 - py
    enter, leave = 0.0, 1.0
    for d, p, lo, hi in ((dx, px, 30.5, 32.5), (dy, py, 30.5, 32.5)):
        if d == 0:
            if p < lo or p > hi:
                return False
            continue
        r0, r1 = (lo - p) / d, (hi - p) / d
        enter, leave = max(enter, min(r0, r1)), min(leave, max(r0, r1))
    return enter <= leave


def gong_hits_in_shadow(fights):
    """Gong-tick hits (>= 10, no projectile landing that tick) taken on a tile hidden from Bork's centre by the Smorks."""
    inside = total = 0
    for ev in fights.values():
        lands = {e["t"] + int(float(e["b"])) for e in ev if e["kind"] == "PROJ"}
        bork = {e["t"]: (int(e["x"]), int(e["y"])) for e in ev if e["kind"] == "NPOS" and base(e["actor"]) == "16640"}
        for g in (e["t"] for e in ev if e["kind"] == "ANIM" and e["id"] == "14542"):
            for h in ev:
                if h["kind"] == "HIT" and h["actor"].startswith("P") and h["t"] == g and h["a"] in ("16", "17") and int(h["id"]) >= 10 and g not in lands:
                    total += 1
                    inside += shadowed(*bork[g], int(h["x"]), int(h["y"]))
    return inside, total


def bork(real, sim):
    failures = []
    compare("14542 next attack", next_attack(real, "bork", "14542"), next_attack(sim, "bork", "14542"), failures)
    exact("14555 -> stomp 14550", stomp_offsets(real), stomp_offsets(sim), failures)
    exact("14550 -> 14551", chain_gap(real, "14550", "14551", "16640"), chain_gap(sim, "14550", "14551", "16640"), failures)
    exact("14551 -> gong", chain_gap(real, "14551", "14542", "16640"), chain_gap(sim, "14551", "14542", "16640"), failures)
    for proj in ("4056", "4057", "4045", "4440"):
        exact(f"{proj} flight", proj_flights(real, proj), proj_flights(sim, proj), failures)
    median_law("smorks cannon gap", smork_gaps(real), smork_gaps(sim), 2, failures)
    exact("gong player hit delay", gong_hit_delays(real), gong_hit_delays(sim), failures)
    exact("scorch after projectile", scorch_delay(real), scorch_delay(sim), failures)
    median_law("scorch tiles per landing", scorch_counts(real), scorch_counts(sim), 2, failures)
    ri, rt = gong_hits_in_shadow(real)
    si, st = gong_hits_in_shadow(sim)
    ok = si == 0
    if not ok:
        failures.append("gong hits in shadow")
    print(f"{('ok ' if st else '-- ') if ok else 'BAD'} {'gong hits in shadow':28s} real {ri}/{rt:<38} sim {si}/{st}")
    return failures


def moia(real, sim):
    return []


def main():
    boss, sim_path = sys.argv[1], sys.argv[2]
    party = sys.argv[3] if len(sys.argv) > 3 else None
    real = load(FIXTURES / f"tfa_{boss}_events.tsv")
    if party:
        meta = {r["fight_id"]: r for r in csv.DictReader(open(FIXTURES / "tfa_fights.tsv"), delimiter="\t")}
        real = {k: v for k, v in real.items() if meta[k]["party_size"] == party}
    sim = load(sim_path)
    failures = {"guardian": guardian, "xarth": xarth, "bork": bork, "moia": moia}[boss](real, sim)
    print(f"{len(failures)} laws differ: {failures}" if failures else "all laws agree")
    sys.exit(1 if failures else 0)


main()
