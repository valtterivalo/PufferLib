"""Compare boss laws between recorded fixtures and a sim dump in the same TSV schema; exit 1 when a law's mode differs."""

import collections
import csv
import pathlib
import sys

FIXTURES = pathlib.Path(__file__).resolve().parent.parent / "fixtures"
ATTACKS = {
    "guardian": {"14751", "14753", "14754", "14752", "14750"},
    "xarth": {"14983", "14984", "14985", "14992", "14989"},
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


def main():
    boss, sim_path = sys.argv[1], sys.argv[2]
    party = sys.argv[3] if len(sys.argv) > 3 else None
    real = load(FIXTURES / f"tfa_{boss}_events.tsv")
    if party:
        meta = {r["fight_id"]: r for r in csv.DictReader(open(FIXTURES / "tfa_fights.tsv"), delimiter="\t")}
        real = {k: v for k, v in real.items() if meta[k]["party_size"] == party}
    sim = load(sim_path)
    failures = {"guardian": guardian}[boss](real, sim)
    print(f"{len(failures)} laws differ: {failures}" if failures else "all laws agree")
    sys.exit(1 if failures else 0)


main()
