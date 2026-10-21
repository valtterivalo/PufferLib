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
    "dreadnoth": {"14779", "14777", "14780", "14782"},
    "ezekiel": {"14685", "14686", "14687", "14688", "14691", "14693"},
    "ocellus": {"14806", "14807", "14809", "14810", "14811", "14812", "14813", "14837", "14878", "14842", "14846", "14848", "14851",
                "14857", "14858", "14861", "14869", "14871"},
}
EZEKIEL_SLOT_NAMES = {"14685": "M", "14686": "G", "14687": "S", "14688": "H", "14691": "W", "14693": "R", "14694": "F", "14684": "X"}


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


def projectile_offsets(fights, anim, proj):
    """(projectile tick minus anim tick, flight rounded down) for each projectile an anim fires."""
    out = []
    for ev in fights.values():
        projs = [e for e in ev if e["kind"] == "PROJ" and e["id"] == proj]
        for a in (e for e in ev if e["kind"] == "ANIM" and e["id"] == anim):
            out += [(p["t"] - a["t"], int(float(p["b"]))) for p in projs if 0 <= p["t"] - a["t"] <= 3 and p["actor"] == a["actor"]]
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
    compare("4201 launch, flight", projectile_offsets(real, "14751", "4201"), projectile_offsets(sim, "14751", "4201"), failures)
    rg, sg = ground_durations(real), ground_durations(sim)
    for k in sorted(set(rg) | set(sg)):
        if k == "cross":
            continue
        compare(f"ground {k}", rg.get(k, hist([])), sg.get(k, hist([])), failures)
    on_vine_tick = lambda c: all(v >= 14 and (v - 5) % 3 == 0 for v in c)
    ok = on_vine_tick(rg.get("cross", {})) and on_vine_tick(sg.get("cross", {})) and bool(sg.get("cross"))
    if not ok:
        failures.append("ground cross on vine ticks")
    print(f"{'ok ' if ok else 'BAD'} {'ground cross on vine ticks':28s} real {fmt(rg.get('cross', hist([]))):42s} sim {fmt(sg.get('cross', hist([])))}")
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
    compare("4374 launch, flight", projectile_offsets(real, "14985", "4374"), projectile_offsets(sim, "14985", "4374"), failures)
    compare("4034 launch, flight", projectile_offsets(real, "14984", "4034"), projectile_offsets(sim, "14984", "4034"), failures)
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
    return hist([max(1, int(float(e["b"]))) for ev in fights.values() for e in ev if e["kind"] == "PROJ" and e["id"] == proj])


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


BORK_STOMP_TEMPLATE = set()
BORK_COUNTS = {"napalm": (7, 10), "bomb": (3, 5)}


def bork_ground(fights, walk):
    """Bork ground gfx per event: dust delay residuals from 1 + Manhattan to the start-of-tick centre, wave tiles breaking the
    footprint, Smorks and walkable rules, ring tiles against the footprint ring one tick after the gong, stomp pulse sources,
    scorch tiles per landing with their fire and splat ids, scorch orientation against the splat, and the clear tick."""
    out = collections.defaultdict(collections.Counter)
    for ev in fights.values():
        npos = sorted((e["t"], (int(e["x"]), int(e["y"]))) for e in ev if e["kind"] == "NPOS" and base(e["actor"]) == "16640")

        def pos(t):
            return [xy for tt, xy in npos if tt <= t][-1]

        dust = collections.defaultdict(dict)
        fire = collections.defaultdict(list)
        splat = collections.defaultdict(dict)
        for e in ev:
            if e["kind"] != "GFX":
                continue
            tile, gid, delay = (int(e["x"]), int(e["y"])), int(e["id"]), int(e["a"])
            if gid == 4395:
                dust[e["t"]][tile] = delay
            elif gid == 4046:
                fire[e["t"]].append((tile, delay))
            elif 4047 <= gid <= 4050:
                splat[e["t"]][tile] = (gid - 4047, delay)
        anims = lambda a: sorted(e["t"] for e in ev if e["kind"] == "ANIM" and e["id"] == a and base(e["actor"]) == "16640")
        gongs, stomps, stuns = anims("14542"), anims("14550"), anims("14545")
        for g in gongs:
            if g not in dust:
                continue
            b = pos(g - 1)
            out["wave delay residual"].update(d - (1 + abs(x - b[0] - 2) + abs(y - b[1] - 2)) for (x, y), d in dust[g].items())
            out["wave tiles breaking the rule"][sum(
                (x, y) not in walk or (b[0] <= x < b[0] + 4 and b[1] <= y < b[1] + 4) or (30 <= x < 33 and 30 <= y < 33)
                for x, y in dust[g])] += 1
            ring_pos = pos(g + 1)
            ring = {(x, y) for x in range(ring_pos[0] - 1, ring_pos[0] + 5) for y in range(ring_pos[1] - 1, ring_pos[1] + 5)
                    if (x, y) in walk and not (ring_pos[0] <= x < ring_pos[0] + 4 and ring_pos[1] <= y < ring_pos[1] + 4)
                    and not (30 <= x < 33 and 30 <= y < 33)}
            got = dust[g + 2]
            out["ring (extra, missing) tiles"][(len(set(got) - ring), len(ring - set(got)))] += 1
            out["ring delay residual"].update(d - (1 + abs(x - ring_pos[0] - 2) + abs(y - ring_pos[1] - 2)) for (x, y), d in got.items())
        for st in stomps:
            for k in range(4):
                got = dust[st + 1 + 3 * k]
                if not got:
                    continue
                b = pos(st)
                if b == (33, 30):
                    out["stomp tiles within 3 of the recorded template"][len(set(got) ^ BORK_STOMP_TEMPLATE) <= 3] += 1
                fits = [(dx, dy) for dx in range(-2, 6) for dy in range(-2, 6)
                        if all(d == 1 + abs(x - b[0] - dx) + abs(y - b[1] - dy) for (x, y), d in got.items())]
                out["stomp source offset"][fits[0] if fits else None] += 1
        objs = {(e["t"], int(e["x"]), int(e["y"])): int(e["b"]) for e in ev if e["kind"] == "OBJ+" and e["id"] == "62544"}
        for t, tiles in splat.items():
            kind = "bomb" if max(d for _, d in tiles.values()) == 1 else "napalm"
            out["landing tile count in the recorded range"][(kind, BORK_COUNTS[kind][0] <= len(tiles) <= BORK_COUNTS[kind][1])] += 1
            for tile, (variant, delay) in tiles.items():
                n_fire = sum(1 for ft, _ in fire[t] if ft == tile)
                out["tile (kind, delay, 4046 count)"][(kind, delay, n_fire)] += 1
                out["scorch orientation = splat rotation"][objs.get((t + 1, *tile)) == 512 * variant] += 1
                out["scorch orientation"][512 * variant] += 1
        removed = [e for e in ev if e["kind"] == "OBJ-" and e["id"] == "62544"]
        for t in sorted({e["t"] for e in removed}):
            out["scorch removal offset from the last stun"][max((t - st for st in stuns if st <= t), default=None)] += 1
        for e in removed:
            out["scorch removed with its placed orientation"][objs.get(
                max((o for o in objs if o[1:] == (int(e["x"]), int(e["y"])) and o[0] < e["t"]), default=None)) == int(e["b"])] += 1
    return out


def bork_ground_laws(real, sim, failures):
    """Wave, ring, stomp and scorch ground gfx must match the recorded rules in both directions, with provisional single-sample
    choices (stomp source, scorch tile counts) compared as 'every common sim value was recorded'."""
    walk = {(int(e["x"]), int(e["y"])) for ev in real.values() for e in ev if e["kind"] == "GFX" and e["id"] == "4395"}
    BORK_STOMP_TEMPLATE.update((int(e["x"]), int(e["y"])) for ev in real.values() for e in ev if e["kind"] == "GFX" and e["id"] == "4395" and e["t"] == 46)
    real_g, sim_g = bork_ground(real, walk), bork_ground(sim, walk)
    for name in real_g:
        shapes_law(name, real_g[name], sim_g[name], failures)


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
    bork_ground_laws(real, sim, failures)
    return failures


def first_anim(ev, anim):
    return min((e["t"] for e in ev if e["kind"] == "ANIM" and e["id"] == anim), default=0)


def moia_hits_at_proj(fights, proj, offset):
    """Whether the aimed player takes a hit exactly offset ticks after each projectile launch."""
    out = []
    for ev in fights.values():
        hits = {(e["t"], e["actor"]) for e in ev if e["kind"] == "HIT" and e["actor"].startswith("P")}
        for e in ev:
            if e["kind"] == "PROJ" and e["id"] == proj and e["a"].startswith("P"):
                out.append((e["t"] + offset, e["a"]) in hits)
    return hist(out)


def moia_binder_phases(fights, kind):
    attack, move = [], []
    for ev in fights.values():
        t0 = first_anim(ev, "14785")
        if kind == "attack":
            attack += [(e["t"] - t0) % 15 for e in ev if e["kind"] == "ANIM" and e["id"] == "14791"]
        else:
            last = {}
            for e in ev:
                if e["kind"] != "NPOS" or base(e["actor"]) != "16670":
                    continue
                pos = (e["x"], e["y"])
                if e["actor"] in last and last[e["actor"]] != pos:
                    move.append((e["t"] - t0) % 15)
                last[e["actor"]] = pos
    return hist(attack if kind == "attack" else move)


def moia_teleports(fights):
    out = []
    for ev in fights.values():
        tps = sorted(e["t"] for e in ev if e["kind"] == "ANIM" and e["id"] == "14788")
        pos = {e["t"]: (e["x"], e["y"]) for e in ev if e["kind"] == "NPOS" and base(e["actor"]) == "16667"}
        out += [pos[t + 1] for t in tps[:2] if t + 1 in pos]
    return hist(out)


def moia_tp_gaps(fights):
    out = []
    for ev in fights.values():
        tps = sorted(e["t"] for e in ev if e["kind"] == "ANIM" and e["id"] == "14788")
        out += [b - a for a, b in zip(tps, tps[1:])]
    return hist(out)


def moia_late_binders(fights):
    out = []
    for ev in fights.values():
        t0 = first_anim(ev, "14785")
        out += [e["t"] - t0 for e in ev if e["kind"] == "NPC+" and e["id"] == "16670" and e["t"] >= t0]
    return hist(out)


def moia(real, sim):
    failures = []
    compare("14785 next attack", next_attack(real, "moia", "14785"), next_attack(sim, "moia", "14785"), failures)
    exact("14788 next attack", next_attack(real, "moia", "14788"), next_attack(sim, "moia", "14788"), failures)
    exact("teleport to teleport", moia_tp_gaps(real), moia_tp_gaps(sim), failures)
    for proj in ("4214", "4213"):
        exact(f"{proj} flight", proj_flights(real, proj), proj_flights(sim, proj), failures)
    exact("4214 hit at launch tick", moia_hits_at_proj(real, "4214", 0), moia_hits_at_proj(sim, "4214", 0), failures)
    exact("4213 hit at launch +5", moia_hits_at_proj(real, "4213", 5), moia_hits_at_proj(sim, "4213", 5), failures)
    exact("binder attack phase", moia_binder_phases(real, "attack"), moia_binder_phases(sim, "attack"), failures)
    rm, sm = moia_binder_phases(real, "move"), moia_binder_phases(sim, "move")
    rs, ss = {k for k, n in rm.items() if n >= 3}, {k for k, n in sm.items() if n >= 3}
    ok = rs == ss
    if not ok:
        failures.append("binder move phases")
    print(f"{'ok ' if ok else 'BAD'} {'binder move phases':28s} real {sorted(rs)} sim {sorted(ss)}")
    exact("late binder spawn", moia_late_binders(real), moia_late_binders(sim), failures)
    exact("Moia tile after teleport", moia_teleports(real), moia_teleports(sim), failures)
def boss_anims(ev, anim):
    return sorted(e["t"] for e in ev if e["kind"] == "ANIM" and base(e["actor"]) == "16665" and e["id"] == anim)


def slam_groups(ev):
    groups = []
    for t in boss_anims(ev, "14777"):
        if groups and t - groups[-1][-1] <= 6:
            groups[-1].append(t)
        else:
            groups.append([t])
    return groups


def slam_group_sizes(fights):
    return hist(len(g) for ev in fights.values() for g in slam_groups(ev))


def slam_gaps(fights):
    return hist(b - a for ev in fights.values() for g in slam_groups(ev) for a, b in zip(g, g[1:]))


def spawn_batches(ev):
    first_jump = min(boss_anims(ev, "14780"), default=10**9)
    per = collections.Counter(e["t"] for e in ev if e["kind"] == "NPC+" and e["id"] == "16666" and e["t"] < first_jump)
    return sorted((t, n) for t, n in per.items() if n >= 7)


def spawn_offset(fights):
    out = []
    for ev in fights.values():
        batches = spawn_batches(ev)
        for g in slam_groups(ev):
            t = next((t for t, n in batches if 0 < t - g[-1] <= 20), None)
            if t is not None:
                out.append(t - g[-1])
    return hist(out)


def spawn_sizes(fights):
    return hist(12 if n >= 11 else n for ev in fights.values() for t, n in spawn_batches(ev))


def hp_pct_at_group_start(fights, scale_real):
    out = [[], [], []]
    for ev in fights.values():
        hp = [(e["t"], int(e["id"]) * 100 / int(e["a"])) for e in ev if e["kind"] == "NHP" and base(e["actor"]) == "16665" and int(e["id"]) >= 0]
        for i, g in enumerate(slam_groups(ev)[:3]):
            v = [p for t, p in hp if t <= g[0]]
            if v:
                out[i].append(v[-1])
    return out


def changes(pos):
    return [p for i, p in enumerate(pos) if i == 0 or p[1:] != pos[i - 1][1:]]


def boss_step_gaps(fights):
    out = []
    for ev in fights.values():
        pos = changes([(e["t"], int(e["x"]), int(e["y"])) for e in ev if e["kind"] == "NPOS" and base(e["actor"]) == "16665"])
        jumps = set(boss_anims(ev, "14780"))
        out += [b[0] - a[0] for a, b in zip(pos, pos[1:]) if max(abs(a[1] - b[1]), abs(a[2] - b[2])) == 1 and b[0] not in jumps and b[0] - a[0] <= 4]
    return hist(out)


def jelly_step_gaps(fights):
    out = []
    for ev in fights.values():
        pos = collections.defaultdict(list)
        for e in ev:
            if e["kind"] == "NPOS" and base(e["actor"]) == "16666":
                pos[e["actor"]].append((e["t"], int(e["x"]), int(e["y"])))
        for p in pos.values():
            p = changes(p)
            out += [b[0] - a[0] for a, b in zip(p, p[1:]) if max(abs(a[1] - b[1]), abs(a[2] - b[2])) == 1 and b[0] - a[0] <= 4]
    return hist(out)


def jelly_hp(fights):
    out = []
    for ev in fights.values():
        dmg = collections.Counter()
        for e in ev:
            if e["kind"] == "NPC+" and base(e["actor"]) == "16666":
                dmg[e["actor"]] = 0
            if e["kind"] == "HIT" and base(e["actor"]) == "16666":
                dmg[e["actor"]] += int(e["id"])
            if e["kind"] == "ANIM" and e["id"] == "1587" and dmg[e["actor"]] > 0:
                out.append(dmg[e["actor"]])
    return hist(out)


def blood_after_slam(fights):
    out = []
    for ev in fights.values():
        objs = collections.Counter(e["t"] for e in ev if e["kind"] == "OBJ+" and e["id"] == "62609")
        out += [objs[t + 1] for g in slam_groups(ev) for t in g]
    return hist(out)


def slam_hit_offsets(fights):
    count, slams = collections.Counter(), 0
    for ev in fights.values():
        hits = [e["t"] for e in ev if e["kind"] == "HIT" and e["actor"] == "P0" and e["a"] in ("16", "17", "12", "13")]
        for t in boss_anims(ev, "14777"):
            slams += 1
            for off in {h - t for h in hits if 0 <= h - t <= 7}:
                count[off] += 1
    return {off for off, n in count.items() if n * 10 >= slams * 4}


def set_law(name, real, sim, failures):
    ok = real == sim
    if not ok:
        failures.append(name)
    print(f"{'ok ' if ok else 'BAD'} {name:28s} real {sorted(real)!s:42s} sim {sorted(sim)}")


def dreadnoth(real, sim):
    failures = []
    set_law("slams per group", set(slam_group_sizes(real)), set(slam_group_sizes(sim)), failures)
    compare("slam gap within group", slam_gaps(real), slam_gaps(sim), failures)
    compare("14779 next attack", next_attack(real, "dreadnoth", "14779"), next_attack(sim, "dreadnoth", "14779"), failures)
    compare("jelly spawn after last slam", spawn_offset(real), spawn_offset(sim), failures)
    set_law("jelly wave sizes", {n for n, c in spawn_sizes(real).items() if c >= 4}, {n for n, c in spawn_sizes(sim).items() if c >= 4}, failures)
    for i, (r, m) in enumerate(zip(hp_pct_at_group_start(real, 1), hp_pct_at_group_start(sim, 1))):
        median_law(f"HP pct at slam group {i + 1}", [round(x) for x in r], [round(x) for x in m], 3, failures)
    compare("boss step gap", boss_step_gaps(real), boss_step_gaps(sim), failures)
    compare("jelly step gap", jelly_step_gaps(real), jelly_step_gaps(sim), failures)
    rh, sh = jelly_hp(real), jelly_hp(sim)
    ok = max(sh) <= max(v for v, n in rh.items() if n >= 3)
    if not ok:
        failures.append("jelly hp")
    print(f"{'ok ' if ok else 'BAD'} {'jelly hp (max damage taken)':28s} real {max(v for v, n in rh.items() if n >= 3):<42} sim {max(sh)}")
    compare("blood tiles after slam", blood_after_slam(real), blood_after_slam(sim), failures)
    compare("4211 launch, flight", projectile_offsets(real, "14777", "4211"), projectile_offsets(sim, "14777", "4211"), failures)
    set_law("slam P0 hit offsets", slam_hit_offsets(real), slam_hit_offsets(sim), failures)
    return failures


def ezekiel_cycles(fights):
    """Anim names between consecutive specials, and the special-to-heal and special-to-weaken offsets."""
    patterns, heal_at, weaken_at = [], [], []
    for ev in fights.values():
        anims = [(e["t"], EZEKIEL_SLOT_NAMES[e["id"]]) for e in ev
                 if e["kind"] == "ANIM" and base(e["actor"]) == "16652" and e["id"] in EZEKIEL_SLOT_NAMES]
        for i, (t0, name) in enumerate(anims):
            if name != "S":
                continue
            seg = []
            for t, n in anims[i + 1:]:
                seg.append((t - t0, n))
                if n in "SFX":
                    break
            if not seg or seg[-1][1] != "S":
                continue
            patterns.append(" ".join(n for t, n in seg[:-1]))
            heal_at += [t for t, n in seg if n == "H"]
            weaken_at += [t for t, n in seg if n == "W"]
    return patterns, hist(heal_at), hist(weaken_at)


def ezekiel_volleys(fights):
    """Sword counts per volley in order, and the boss health percent at the first three-sword volley."""
    kinds, first3 = [], []
    for ev in fights.values():
        nhp = [(e["t"], int(e["id"]) * 100 // int(e["a"])) for e in ev if e["kind"] == "NHP" and base(e["actor"]) == "16652" and int(e["id"]) >= 0]
        counts = collections.Counter(e["t"] for e in ev if e["kind"] == "NPC+" and e["id"] == "16653")
        seq = [counts[e["t"]] for e in ev if e["kind"] == "ANIM" and e["id"] == "14687" and base(e["actor"]) == "16652"]
        kinds.append(seq)
        for e in ev:
            if e["kind"] == "ANIM" and e["id"] == "14687" and counts[e["t"]] == 3:
                first3.append(next((v for t, v in reversed(nhp) if t < e["t"]), 100))
                break
    return kinds, first3


def ezekiel_alternation_breaks(kinds):
    """Adjacent equal sword counts after the first three-sword volley; both real and sim never break."""
    return sum(1 for seq in kinds if 3 in seq for a, b in zip(seq[seq.index(3):], seq[seq.index(3) + 1:]) if a == b)


def ezekiel_finale(fights):
    cast, wave_first, wave_gaps = [], [], []
    for ev in fights.values():
        x = [e["t"] for e in ev if e["kind"] == "ANIM" and e["id"] == "14684"]
        f = [e["t"] for e in ev if e["kind"] == "ANIM" and e["id"] == "14694"]
        if not f:
            continue
        if x:
            cast.append(f[0] - x[0])
        waves = sorted({e["t"] for e in ev if e["kind"] in ("GFX", "OBJ+") and e["id"] == "4197"})
        if waves:
            wave_first.append(waves[0] - f[0])
            wave_gaps += [b - a for a, b in zip(waves, waves[1:])]
    return hist(cast), hist(wave_first), hist(wave_gaps)


def ezekiel_player_hits(fights):
    """Player hit amounts at fixed offsets from a special: stars at +3, sword bands at the jump ticks +14..+16."""
    star, band4, band3 = [], [], []
    for ev in fights.values():
        counts = collections.Counter(e["t"] for e in ev if e["kind"] == "NPC+" and e["id"] == "16653")
        for a in (e for e in ev if e["kind"] == "ANIM" and e["id"] == "14687"):
            for h in ev:
                if h["kind"] != "HIT" or not h["actor"].startswith("P") or h["a"] not in ("16", "17") or h["t"] < a["t"]:
                    continue
                amount, d = int(h["id"]), h["t"] - a["t"]
                if d == 3 and 20 <= amount <= 33:
                    star.append(amount)
                if 14 <= d <= 16 and counts[a["t"]] == 4 and 50 <= amount <= 63:
                    band4.append(amount)
                if 14 <= d <= 16 and counts[a["t"]] == 3 and 27 <= amount <= 33:
                    band3.append(amount)
    return star, band4, band3


def ezekiel(real, sim):
    failures = []
    for anim in ("14685", "14686", "14687", "14688", "14691", "14693"):
        compare(f"{anim} next attack", next_attack(real, "ezekiel", anim), next_attack(sim, "ezekiel", anim), failures)
    rp, rh, rw = ezekiel_cycles(real)
    sp, sh, sw = ezekiel_cycles(sim)
    unknown = sorted({p.replace("G", "M") for p in sp} - {p.replace("G", "M") for p in rp})
    ok = not unknown
    if not ok:
        failures.append("sim cycle shapes")
    print(f"{'ok ' if ok else 'BAD'} {'cycle shapes (G as M) sim only':28s} {unknown}")
    compare("heal after special", rh, sh, failures)
    compare("weaken after special", rw, sw, failures)
    rk, r3 = ezekiel_volleys(real)
    sk, s3 = ezekiel_volleys(sim)
    ok = ezekiel_alternation_breaks(rk) == 0 and ezekiel_alternation_breaks(sk) == 0 and max(s3) <= 85 and max(r3) <= 85
    if not ok:
        failures.append("three-sword phase")
    print(f"{'ok ' if ok else 'BAD'} {'three-sword phase':28s} real first-3 hp max {max(r3)} breaks {ezekiel_alternation_breaks(rk)}  sim max {max(s3)} breaks {ezekiel_alternation_breaks(sk)}")
    for label, r, s in zip(("roll to cast", "first wave", "wave gap"), ezekiel_finale(real), ezekiel_finale(sim)):
        compare(f"finale {label}", r, s, failures)
    for label, r, s in zip(("star damage", "4-sword band damage", "3-sword band damage"), ezekiel_player_hits(real), ezekiel_player_hits(sim)):
        median_law(label, r, s, 3, failures)
    return failures


def first_offsets(fights, anchor, kind, npc_id, window=400):
    """Ticks from each anchor anim to the first NPC+ of npc_id after it."""
    out = []
    for ev in fights.values():
        for a in (e for e in ev if e["kind"] == "ANIM" and e["id"] == anchor):
            n = next((e for e in ev if e["kind"] == kind and e["id"] == npc_id and 0 <= e["t"] - a["t"] <= window), None)
            if n:
                out.append(n["t"] - a["t"])
    return hist(out)


def anim_offsets(fights, anchor, anim, window):
    out = []
    for ev in fights.values():
        for a in (e for e in ev if e["kind"] == "ANIM" and e["id"] == anchor):
            out += [e["t"] - a["t"] for e in ev if e["kind"] == "ANIM" and e["id"] == anim and 0 < e["t"] - a["t"] <= window]
    return hist(out)


def gfx_offsets(fights, anchor, gfx, window):
    """Offset of the first gfx row after each anchor anim, split by the anchor's following anim (the spin variant)."""
    out = []
    for ev in fights.values():
        for a in (e for e in ev if e["kind"] == "ANIM" and e["id"] == anchor):
            g = next((e for e in ev if e["kind"] == "GFX" and e["id"] == gfx and 0 <= e["t"] - a["t"] <= window), None)
            if g:
                out.append(g["t"] - a["t"])
    return hist(out)


def big_hits(fights, anim, lo, window):
    out = []
    for ev in fights.values():
        for a in (e for e in ev if e["kind"] == "ANIM" and e["id"] == anim):
            out += [h["t"] - a["t"] for h in ev if h["kind"] == "HIT" and h["actor"].startswith("P") and h["a"] in ("16", "17")
                    and int(h["id"]) >= lo and 0 <= h["t"] - a["t"] <= window]
    return hist(out)


def projectile_launch(fights, anim, proj):
    out = []
    for ev in fights.values():
        projs = [e for e in ev if e["kind"] == "PROJ" and e["id"] == proj]
        for a in (e for e in ev if e["kind"] == "ANIM" and e["id"] == anim):
            out += [p["t"] - a["t"] for p in projs if 0 <= p["t"] - a["t"] <= 3 and p["actor"] == a["actor"]]
    return hist(out)


def explode_next(fights):
    """What follows a 14811: (previous charge 18 ticks back is a corner, 6 a middle charge), ticks to the next anim."""
    out = []
    for ev in fights.values():
        anims = [e for e in ev if e["kind"] == "ANIM" and e["id"] in ATTACKS["ocellus"] and e["actor"].split("#")[0] == "16674"]
        for i, a in enumerate(anims[1:-1], 1):
            if a["id"] == "14811" and anims[i - 1]["id"] == "14809":
                out.append((a["t"] - anims[i - 1]["t"], anims[i + 1]["t"] - a["t"], anims[i + 1]["id"]))
    return hist(out)


def barrage_shapes(fights):
    """(14809 offset, 14807 offset) after each 14806: the charge follows at 11 or 16, the end anim plays at 5, 10 or 15."""
    out = []
    for ev in fights.values():
        anims = [e for e in ev if e["kind"] == "ANIM" and e["id"] in ATTACKS["ocellus"] and e["actor"].split("#")[0] == "16674"]
        for i, a in enumerate(anims):
            if a["id"] != "14806":
                continue
            nxt = anims[i + 1:i + 4]
            charge = next((e["t"] - a["t"] for e in nxt if e["id"] == "14809"), None)
            end = next((e["t"] - a["t"] for e in nxt if e["id"] == "14807"), None)
            if charge in (11, 16):
                out.append((charge, end))
    return hist(out)


def shapes_law(name, real, sim, failures):
    """Every value seen 3 or more times in the recordings appears in the sim, and every common sim value was recorded."""
    missing = [k for k in real if real[k] >= 3 and not sim[k]]
    extra = [k for k in sim if sim[k] * 20 >= sum(sim.values()) and not real[k]]
    if missing or extra:
        failures.append(name)
    print(f"{'BAD' if missing or extra else 'ok '} {name:28s} real {fmt(real):42s} sim {fmt(sim)}")


OC_CRACK_IDS = range(4236, 4260)
OC_EXPLODE_IDS = range(4262, 4266)


def oc_crack_kind(gfx):
    k = (gfx - 4236) % 12
    return "S" if k < 4 else ("E" if (k - 4) % 2 else "B")


def ocellus_ground(fights):
    """Phase 1 ground gfx per tick against Ocellus's footprint: crack line starts (boss tile, id, delay, far tile relative to the
    boss), start offsets from the 14809 charge, END offset from the line's start, and the explosion floor per ring."""
    starts, offsets, ends, pairs, rings, sizes = hist([]), hist([]), hist([]), hist([]), hist([]), hist([])
    for ev in fights.values():
        boss, charge, born, blast = None, None, {}, collections.defaultdict(list)
        for e in sorted(ev, key=lambda e: e["t"]):
            if e["kind"] == "NPOS" and base(e["actor"]) == "16674":
                boss = (int(e["x"]), int(e["y"]))
            elif e["kind"] == "ANIM" and base(e["actor"]) == "16674" and e["id"] == "14809":
                charge = e["t"]
            elif e["kind"] == "GFX" and boss is not None and charge is not None and (int(e["id"]) in OC_CRACK_IDS or int(e["id"]) in OC_EXPLODE_IDS):
                gfx, x, y, delay = int(e["id"]), int(e["x"]), int(e["y"]), int(e["a"])
                where = "middle" if boss == (30, 48) else "corner"
                if gfx in OC_EXPLODE_IDS:
                    cheb = max(max(boss[0] - x, x - boss[0] - 2, 0), max(boss[1] - y, y - boss[1] - 2, 0))
                    rings[(cheb, delay)] += 1
                    blast[(e["t"], where)].append(1)
                if gfx not in OC_CRACK_IDS:
                    continue
                kind = oc_crack_kind(gfx)
                pairs[(kind, delay)] += 1
                if kind == "S":
                    starts[(where, boss, gfx, delay, x - boss[0], y - boss[1])] += 1
                    offsets[(where, e["t"] - charge)] += 1
                    born.setdefault((x, y), []).append((e["t"], delay))
                elif kind == "E" and born.get((x, y)) and delay == 1:
                    t0, d0 = born[(x, y)].pop(0)
                    ends[(where, d0, e["t"] - t0)] += 1
        for (t, where), tiles in blast.items():
            sizes[(where, len(tiles))] += 1
    return starts, offsets, ends, pairs, rings, sizes


def ocellus_ground_laws(real, sim, failures):
    """Crack line choice follows the raiders, so the sim's lines must each be a recorded line; timing, delays and the
    explosion floor must match in both directions."""
    real_g, sim_g = ocellus_ground(real), ocellus_ground(sim)
    unseen = sorted(k for k in sim_g[0] if not real_g[0][k])
    if unseen:
        failures.append("crack lines not in the recordings")
    print(f"{'BAD' if unseen else 'ok '} {'crack lines all recorded':28s} sim {len(sim_g[0])} distinct, {sum(sim_g[0].values())} lines, unseen {unseen[:4]}")
    labels = ("crack start offset from 14809", "crack END after start", "crack (piece, delay)", "explosion floor (ring, delay)", "explosion floor tiles")
    for label, r, s in zip(labels, real_g[1:], sim_g[1:]):
        shapes_law(label, r, s, failures)


def ocellus(real, sim):
    failures = []
    rs, ss = barrage_shapes(real), barrage_shapes(sim)
    for key in sorted(rs, key=str):
        if rs[key] < 3:
            continue
        ok = ss[key] > 0
        if not ok:
            failures.append(f"barrage {key}")
        print(f"{'ok ' if ok else 'BAD'} barrage (charge, end) {key!s:12s} real {rs[key]} sim {ss[key]}")
    extra = [k for k in ss if ss[k] * 20 >= sum(ss.values()) and k not in rs]
    if extra:
        failures.append(f"barrage shapes only in sim {extra}")
        print(f"BAD barrage shapes only in sim {extra}")
    for anim in ("14809", "14810", "14842", "14846", "14851", "14857", "14871", "14837"):
        compare(f"{anim} next attack", next_attack(real, "ocellus", anim), next_attack(sim, "ocellus", anim), failures)
    rx, sx = explode_next(real), explode_next(sim)
    for key in sorted({k for k in rx if rx[k] >= 3}):
        ok = sx[key] > 0
        if not ok:
            failures.append(f"explode {key}")
        print(f"{'ok ' if ok else 'BAD'} explode (charge, next, anim) {key!s:20s} real {rx[key]} sim {sx[key]}")
    compare("4229 launch", projectile_launch(real, "14806", "4229"), projectile_launch(sim, "14806", "4229"), failures)
    compare("4232 launch", projectile_launch(real, "14806", "4232"), projectile_launch(sim, "14806", "4232"), failures)
    compare("14811 big hit offset", big_hits(real, "14811", 25, 3), big_hits(sim, "14811", 25, 3), failures)
    compare("14861 slam hit offset", big_hits(real, "14861", 30, 3), big_hits(sim, "14861", 30, 3), failures)
    compare("14857 -> 14861", anim_offsets(real, "14857", "14861", 40), anim_offsets(sim, "14857", "14861", 40), failures)
    shapes_law("14869 -> 14871 (short, long)", next_attack(real, "ocellus", "14869"), next_attack(sim, "ocellus", "14869"), failures)
    shapes_law("14869 -> 4346 debris", gfx_offsets(real, "14869", "4346", 8), gfx_offsets(sim, "14869", "4346", 8), failures)
    compare("14842 P0 hit delay", anim_to_p0_hit(real, "14842", 1), anim_to_p0_hit(sim, "14842", 1), failures)
    compare("14861 -> siphon spawn", first_offsets(real, "14861", "NPC+", "16679", 10), first_offsets(sim, "14861", "NPC+", "16679", 10), failures)
    compare("14881 -> first 14842", anim_offsets(real, "14881", "14842", 20), anim_offsets(sim, "14881", "14842", 20), failures)
    ocellus_ground_laws(real, sim, failures)
    return failures


def main():
    boss, sim_path = sys.argv[1], sys.argv[2]
    party = sys.argv[3] if len(sys.argv) > 3 else None
    real = load(FIXTURES / f"tfa_{boss}_events.tsv")
    if party:
        meta = {r["fight_id"]: r for r in csv.DictReader(open(FIXTURES / "tfa_fights.tsv"), delimiter="\t")}
        real = {k: v for k, v in real.items() if meta[k]["party_size"] == party}
    sim = load(sim_path)
    failures = {"guardian": guardian, "xarth": xarth, "dreadnoth": dreadnoth, "ezekiel": ezekiel, "bork": bork, "moia": moia, "ocellus": ocellus}[boss](real, sim)
    print(f"{len(failures)} laws differ: {failures}" if failures else "all laws agree")
    sys.exit(1 if failures else 0)


main()
