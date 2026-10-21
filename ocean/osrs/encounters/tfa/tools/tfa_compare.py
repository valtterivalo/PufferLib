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
            if a["id"] == anim and b["actor"] == a["actor"]:
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


PLAYER_DAMAGE_SPLATS = {"16", "17", "18", "19", "43", "44"}
P0_SPLATS = {"16", "18", "44", "12"}
RATE_WINDOW = 120


def boss_hits(fights, ids):
    """Player hitsplats on the boss: (fight, tick, damage, P0 or not, landed). A splashed spell leaves no splat, live and in the dump."""
    return [(fid, e["t"], int(e["id"]), e["a"] in P0_SPLATS, e["a"] in PLAYER_DAMAGE_SPLATS) for fid, ev in fights.items() for e in ev
            if e["kind"] == "HIT" and base(e["actor"]) in ids and e["a"] in PLAYER_DAMAGE_SPLATS | {"12", "13"}]


def damage_rates(fights, ids):
    """Team damage per tick on the boss over the first RATE_WINDOW ticks from its first player splat, for fights that last that long."""
    hits = collections.defaultdict(list)
    for fid, t, dmg, _, landed in boss_hits(fights, ids):
        hits[fid].append((t, dmg if landed else 0))
    out = []
    for fid, hs in hits.items():
        t0 = min(t for t, _ in hs)
        if max(e["t"] for e in fights[fid]) - t0 >= RATE_WINDOW:
            out.append(sum(d for t, d in hs if t < t0 + RATE_WINDOW) / RATE_WINDOW)
    return sorted(out)


def damage_laws(real, sim, ids, failures, rate, floored=True):
    """Block share of P0's and the other raiders' splats on the boss within 0.10, and when `rate`, the median team damage per tick within 25%.
    For a boss with a defence floor (`floored`) only fights after the 21 Oct drain hotfix count: before it the drains went past the floor."""
    if floored:
        real = {k: v for k, v in real.items() if FIGHT_META[k]["drain"] == "floored"}
        if not real:
            print("--  boss misses and damage rate: no fight recorded after the drain hotfix")
            return
    for name, mine in (("boss miss P0", True), ("boss miss others", False)):
        r = [landed for _, _, _, p0, landed in boss_hits(real, ids) if p0 == mine]
        m = [landed for _, _, _, p0, landed in boss_hits(sim, ids) if p0 == mine]
        rm, sm = 1 - sum(r) / len(r), 1 - sum(m) / max(len(m), 1)
        ok = bool(m) and abs(rm - sm) <= 0.10
        if not ok:
            failures.append(name)
        print(f"{'ok ' if ok else 'BAD'} {name:28s} real {rm:.2f} (n={len(r)}){'':30s} sim {sm:.2f} (n={len(m)})")
    if not rate:
        return
    r, m = damage_rates(real, ids), damage_rates(sim, ids)
    rmed, smed = r[len(r) // 2], m[len(m) // 2] if m else 0
    ok = bool(m) and abs(smed - rmed) <= 0.25 * rmed
    if not ok:
        failures.append("damage per tick")
    print(f"{'ok ' if ok else 'BAD'} {'damage per tick':28s} real median {rmed:.1f} (n={len(r)}){'':22s} sim median {smed:.1f} (n={len(m)})")


def guardian_ground(fights):
    """Mushroom loc lifetimes and the 4389 land flash: burst life, armed to burst gaps, armed removals without a burst, and per 14747 anim the
    4389 (tick offset, tile relative to the boss tile, delay, count)."""
    burst_life, gaps, unbursted, land = hist([]), hist([]), 0, hist([])
    for ev in fights.values():
        armed, burst = {}, {}
        bursts_at = {(e["t"], e["x"], e["y"]) for e in ev if e["kind"] == "OBJ+" and e["id"] == "62580"}
        cleared = max((e["t"] for e in ev if e["kind"] == "OBJ-" and e["id"] == "62579"), default=-1)
        for e in sorted(ev, key=lambda e: e["t"]):
            tile = (e["x"], e["y"])
            if e["kind"] == "OBJ+" and e["id"] == "62579":
                armed[tile] = e["t"]
            elif e["kind"] == "OBJ+" and e["id"] == "62580":
                if tile in armed:
                    gaps[e["t"] - armed.pop(tile)] += 1
                burst[tile] = e["t"]
            elif e["kind"] == "OBJ-" and e["id"] == "62580" and tile in burst:
                burst_life[e["t"] - burst.pop(tile)] += 1
            elif e["kind"] == "OBJ-" and e["id"] == "62579" and e["t"] != cleared and (e["t"], e["x"], e["y"]) not in bursts_at:
                unbursted += 1
        for a in (e for e in ev if e["kind"] == "ANIM" and base(e["actor"]) == "16660" and e["id"] == "14747"):
            flashes = [g for g in ev if g["kind"] == "GFX" and g["id"] == "4389" and g["t"] == a["t"]]
            land[(len(flashes),) + tuple((int(g["x"]) - int(a["x"]), int(g["y"]) - int(a["y"]), int(g["a"])) for g in flashes)] += 1
        stray = [g for g in ev if g["kind"] == "GFX" and g["id"] == "4389" and not any(a["t"] == g["t"] and a["id"] == "14747" for a in ev if a["kind"] == "ANIM")]
        land["stray"] += len(stray)
    return burst_life, gaps, unbursted, land


def guardian_ground_laws(real, sim, failures):
    """Burst loc lives 2 ticks, armed to burst is never under 2 ticks and armed mushrooms outlive 60 ticks, and every 14747 draws exactly one
    4389 at the boss tile + (2, 2) with delay 21 on the same tick."""
    (rl, rg, ru, rland), (sl, sg, su, sland) = guardian_ground(real), guardian_ground(sim)
    shapes_law("mushroom burst loc life", rl, sl, failures)
    ok = min(rg) == min(sg) == 2
    if not ok:
        failures.append("armed to burst minimum")
    print(f"{'ok ' if ok else 'BAD'} {'armed to burst minimum':28s} real {min(rg)} sim {min(sg)}")
    ok = any(g > 60 for g in sg)
    if not ok:
        failures.append("armed outlives 60 ticks")
    print(f"{'ok ' if ok else 'BAD'} {'armed outlives 60 ticks':28s} real {sum(n for g, n in rg.items() if g > 60)} gaps, sim {sum(n for g, n in sg.items() if g > 60)}")
    print(f"--  {'armed removed before the clear':28s} real {ru} (cause not recorded), sim {su}")
    shapes_law("4389 per 14747 (count, tile, delay)", rland, sland, failures)


def guardian_slam(fights):
    """Per ranged (14751) or slam (14752) anim of the flying Guardian 16659: (P0 under its 5x5 at t-2, t-1, t, anim), and for each slam whether
    a P0 hitsplat landed at t+1 while P0 was out of the footprint at t."""
    patterns, dodged = hist([]), hist([])
    for ev in fights.values():
        pos = {e["t"]: (int(e["x"]), int(e["y"])) for e in ev if e["kind"] == "POS" and e["actor"] == "P0" and e["x"] != "-"}
        boss = sorted((e["t"], int(e["x"]), int(e["y"])) for e in ev if e["kind"] == "NPOS" and base(e["actor"]) == "16659")
        hits = {e["t"] for e in ev if e["kind"] == "HIT" and e["actor"] == "P0" and e["a"] in ("16", "12")}

        def under(t):
            sw = [(x, y) for tt, x, y in boss if tt <= t]
            if t not in pos or not sw:
                return None
            return sw[-1][0] <= pos[t][0] < sw[-1][0] + 5 and sw[-1][1] <= pos[t][1] < sw[-1][1] + 5

        for e in ev:
            if e["kind"] != "ANIM" or base(e["actor"]) != "16659" or e["id"] not in ("14751", "14752"):
                continue
            pattern = tuple(under(e["t"] - d) for d in (2, 1, 0))
            if None in pattern:
                continue
            patterns[(pattern, e["id"])] += 1
            if e["id"] == "14752" and pattern[2] is False:
                dodged[(e["t"] + 1) not in hits] += 1
    return patterns, dodged


def guardian_slam_laws(real, sim):
    """Run on a solo dump against every recorded fight (P0 under the footprint at t-1 is the slam's target in both). Each pattern seen 3 times or
    more on both sides must pick the same anim, and a P0 who stepped out on the anim tick takes no hit the tick after (2% slack for the vines,
    mushrooms and projectiles that land on that tick anyway; the recordings: 5 of 5 clean)."""
    failures = []
    real = load(FIXTURES / "tfa_guardian_events.tsv")
    rp, rd = guardian_slam(real)
    sp, sd = guardian_slam(sim)
    for pattern in sorted({p for p, _ in rp} | {p for p, _ in sp}):
        r = hist({anim: n for (p, anim), n in rp.items() if p == pattern})
        m = hist({anim: n for (p, anim), n in sp.items() if p == pattern})
        name = f"slam pattern {''.join('I' if u else 'o' for u in pattern)}"
        if sum(r.values()) < 3 or sum(m.values()) < 3:
            print(f"--  {name:28s} real {fmt(r):42s} sim {fmt(m)}")
            continue
        compare(name, r, m, failures)
    ok = bool(sd) and sd[False] <= 0.02 * sum(sd.values())
    if not ok:
        failures.append("slam dodged by stepping out")
    print(f"{'ok ' if ok else 'BAD'} {'slam dodged by stepping out':28s} real {fmt(rd):42s} sim {fmt(sd)}")
    return failures


def guardian_intro(fights):
    """Ticks from the inert Guardian's 14755 to its first attack, and from the 16657 -> 16659 change to it."""
    spawn, change = hist([]), hist([])
    for ev in fights.values():
        attacks = sorted(e["t"] for e in ev if e["kind"] == "ANIM" and e["id"] in ("14751", "14752", "14753"))
        for e in ev:
            later = [a for a in attacks if a > e["t"]]
            if e["kind"] == "ANIM" and e["id"] == "14755" and later:
                spawn[later[0] - e["t"]] += 1
            if e["kind"] == "NPCID" and e["id"] == "16659" and e["a"] == "16657" and later:
                change[later[0] - e["t"]] += 1
    return spawn, change


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
    guardian_ground_laws(real, sim, failures)
    damage_laws(real, sim, {"16659", "16660"}, failures, rate=False)
    for name, r, m in zip(("14755 -> first attack", "16659 change -> first attack"), guardian_intro(real), guardian_intro(sim)):
        compare(name, r, m, failures)
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


XARTH_PILE_IDS = {"62748", "62750", "62752", "62754", "62756", "62758", "62760", "62762"}


def xarth_ground(fights):
    """4379 per event (distinct tiles, duplicate rows, delay, tick offset from 14992); the first arena fire batch against the third 14989;
    later batches against the cycle's first 14985; fire under P0 and orientation; the corpse pile fires against the third 14989; the rubble
    removals against 14997."""
    out = {k: hist([]) for k in ("aoe_tiles", "aoe_dups", "aoe_delay", "aoe_offset", "fire_first", "fire_later", "fire_under", "fire_orient", "piles", "rubble")}
    for ev in fights.values():
        boss = lambda a: [e["t"] for e in ev if e["kind"] == "ANIM" and base(e["actor"]) == "16684" and e["id"] == a]
        aoes, blasts = boss("14992"), boss("14989")
        by_tick = collections.defaultdict(list)
        for e in ev:
            if e["kind"] == "GFX" and e["id"] == "4379":
                by_tick[e["t"]].append((e["x"], e["y"]))
                out["aoe_delay"][e["a"]] += 1
        for t, tiles in by_tick.items():
            out["aoe_tiles"][len(set(tiles))] += 1
            out["aoe_dups"][len(tiles) - len(set(tiles))] += 1
            out["aoe_offset"][t - max(a for a in aoes if a <= t)] += 1
        if len(blasts) < 3:
            continue
        land = blasts[2] + 3
        ranged = sorted(boss("14985"))
        starts = [x for i, x in enumerate(ranged) if i == 0 or x - ranged[i - 1] >= 9]
        fires = collections.defaultdict(list)
        for e in ev:
            if e["kind"] == "OBJ+" and e["id"] == "62713":
                fires[e["t"]].append((e["x"], e["y"]))
                out["fire_orient"][e["b"]] += 1
            elif e["kind"] == "OBJ+" and e["id"] in XARTH_PILE_IDS:
                out["piles"][(e["t"] - land, e["id"], e["x"], e["y"], e["b"])] += 1
            elif e["kind"] == "OBJ-" and e["id"] in ("62711", "62712"):
                broke = [b["t"] for b in ev if b["kind"] == "ANIM" and b["id"] == "14997" and (b["x"], b["y"]) == (e["x"], e["y"])]
                out["rubble"][(e["id"], e["x"], e["y"], min((e["t"] - b for b in broke), key=abs, default="no 14997"))] += 1
        pos = {e["t"]: (e["x"], e["y"]) for e in ev if e["kind"] == "POS" and e["actor"] == "P0"}
        for t, tiles in fires.items():
            out["fire_under"][pos.get(t) in tiles] += 1
            if t == land:
                out["fire_first"][len(tiles)] += 1
            elif t > land:
                before = [x for x in starts if x <= t]
                if before and t - before[-1] <= 12:
                    out["fire_later"][t - before[-1]] += 1
    return out


def xarth_ground_laws(real, sim, failures):
    """4379 is drawn once per tile with delay 121 on the 14992 tick, the arena fire starts as 7 tiles on the third blast's landing and then 6 to 8
    ticks into each ranged cycle off the player, orientation 0, and the pile fires and rubble removals are the recorded sets."""
    r, s = xarth_ground(real), xarth_ground(sim)
    labels = (("aoe_delay", "4379 delay"), ("aoe_offset", "4379 offset from 14992"), ("aoe_dups", "4379 duplicate tiles"), ("fire_first", "first fire batch size"),
              ("fire_later", "fire offset from cycle start"), ("fire_under", "fire spawn under P0"), ("fire_orient", "fire orientation"),
              ("piles", "corpse pile fires"), ("rubble", "rubble removals"))
    for key, label in labels:
        shapes_law(label, r[key], s[key], failures)
    ok = max(s["aoe_tiles"]) <= max(r["aoe_tiles"]) and min(s["aoe_tiles"]) >= 1
    if not ok:
        failures.append("4379 tiles per event")
    print(f"{'ok ' if ok else 'BAD'} {'4379 tiles per event':28s} real {fmt(r['aoe_tiles']):42s} sim {fmt(s['aoe_tiles'])}")


def xarth_phase_split(fights):
    """Each fight cut at its first ranged attack (14985), which only phase 2 throws: the magic gap rule differs by phase, so a pooled
    histogram would follow how long the raiders take in each phase."""
    p1, p2 = {}, {}
    for fid, ev in fights.items():
        start = next((e["t"] for e in ev if e["kind"] == "ANIM" and e["id"] == "14985"), 1 << 30)
        p1[fid] = [e for e in ev if e["t"] < start]
        p2[fid] = [e for e in ev if e["t"] >= start]
    return p1, p2


FIGHT_META = {r["fight_id"]: r for r in csv.DictReader(open(FIXTURES / "tfa_fights.tsv"), delimiter="\t")}


def drained_misses(fights, boss_ids, sim):
    """Zero share of P0's and the others' splats (1-3 left out: thralls) on the boss from 15 ticks after the first Tonalztics throw
    (projectile 2727; the sim dump has no player projectiles and every sim raid opens with them, so its window starts at tick 15).
    Recorded fights before the 21 Oct drain hotfix (tfa_fights.tsv drain=uncapped) drained past the floors and are left out."""
    out = {"P0": [0, 0], "others": [0, 0]}
    for fid, ev in fights.items():
        throws = [e["t"] for e in ev if e["kind"] == "PROJ" and e["id"] == "2727"]
        if not sim and (not throws or FIGHT_META[fid]["drain"] != "floored"):
            continue
        start = (0 if sim else min(throws)) + 15
        for e in ev:
            if e["kind"] != "HIT" or base(e["actor"]) not in boss_ids or e["t"] < start or 1 <= int(e["id"]) <= 3:
                continue
            who = "P0" if e["a"] in P0_SPLATS | {"18", "44"} else "others" if e["a"] in ("13", "17", "19", "43") else None
            if who:
                out[who][0] += 1
                out[who][1] += e["id"] == "0"
    return out


def drained_miss_law(name, boss_ids, real, sim, failures, tol=0.15):
    """A defence floor moves the drained misses by about 0.25 (Xarth: 0.03 with the drain to 0, 0.30 at the floor of 102), so a gap over 0.15 means
    one side has a floor the other lacks."""
    r, s = drained_misses(real, boss_ids, False), drained_misses(sim, boss_ids, True)
    for who in ("P0", "others"):
        if r[who][0] < 20:
            continue
        rv, sv = r[who][1] / r[who][0], s[who][1] / max(s[who][0], 1)
        ok = abs(rv - sv) <= tol
        if not ok:
            failures.append(f"{name} {who}")
        print(f"{'ok ' if ok else 'BAD'} {name + ' ' + who:28s} real {rv:.2f} (n={r[who][0]}){'':22s} sim {sv:.2f} (n={s[who][0]})")


def xarth(real, sim):
    failures = []
    drained_miss_law("miss after the drain", {"16684"}, real, sim, failures)
    for phase, r, s in zip(("P1", "P2"), xarth_phase_split(real), xarth_phase_split(sim)):
        compare(f"14984 next attack {phase}", next_attack(r, "xarth", "14984"), next_attack(s, "xarth", "14984"), failures)
    for anim in ("14985", "14992", "14989"):
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
    xarth_ground_laws(real, sim, failures)
    damage_laws(real, sim, {"16684"}, failures, rate=True)
    return failures


def exact(name, real, sim, failures):
    """A single-sample law: the real mode must be the sim mode whatever n is."""
    if not real:
        return
    ok = bool(sim) and real.most_common(1)[0][0] == sim.most_common(1)[0][0]
    if not ok:
        failures.append(name)
    print(f"{'ok ' if ok else 'BAD'} {name:28s} real {fmt(real):42s} sim {fmt(sim)}")


def proj_flights(fights, proj):
    return hist([max(1, int(float(e["b"]))) for ev in fights.values() for e in ev if e["kind"] == "PROJ" and e["id"] == proj])


def support_reached(name, real, sim, failures):
    """support() for a stage the scripted sim team may not survive to: skipped when the sim never gets there (test_tfa_ocellus_p4 covers reaching it)."""
    if not sim:
        print(f"--  {name:28s} real {fmt(real):42s} sim never reached it")
        return
    support(name, real, sim, failures)


def support(name, real, sim, failures):
    """Every value the sim produces was recorded (and the sim produces some when the recording has some)."""
    if not real:
        return
    extra = sorted(set(sim) - set(real), key=str)
    ok = bool(sim) and not extra
    if not ok:
        failures.append(name)
    print(f"{'ok ' if ok else 'BAD'} {name:28s} real {fmt(real):42s} sim {fmt(sim)}" + (f"  unrecorded {extra[:4]}" if extra else ""))


def bork_next_anim(fights):
    """Gong to gong with no other Bork attack anim between: 7, 6 after the speed step."""
    out = []
    for ev in fights.values():
        ts = [(e["t"], e["id"]) for e in ev if e["kind"] == "ANIM" and base(e["actor"]) == "16640" and e["id"] in ATTACKS["bork"]]
        out += [b - a for (a, ia), (b, ib) in zip(ts, ts[1:]) if ia == ib == "14542"]
    return hist(out)


def bork_gong_hit_offsets(fights):
    """Offsets 0..3 from a gong of the hits (>= 5, no projectile landing, napalm or bomb fire, or scorch under them) on raiders next to Bork at
    the gong: the wave at +0, the ring at +2."""
    out = []
    for ev in fights.values():
        pos = collections.defaultdict(dict)
        for e in ev:
            if e["kind"] == "POS" and e["x"] != "-":
                pos[e["t"]][e["actor"]] = (int(e["x"]), int(e["y"]))
        moves = sorted((e["t"], (int(e["x"]), int(e["y"]))) for e in ev if e["kind"] == "NPOS" and base(e["actor"]) == "16640")
        lands = {(e["t"] + int(float(e["b"])), e["a"]) for e in ev if e["kind"] == "PROJ"}
        fire = {e["t"] + int(float(e["b"])) for e in ev if e["kind"] == "PROJ" and e["id"] in ("4045", "4440")}
        scorch, live = {}, set()
        for e in sorted((e for e in ev if e["kind"] in ("OBJ+", "OBJ-") and e["id"] == "62544"), key=lambda e: e["t"]):
            (live.add if e["kind"] == "OBJ+" else live.discard)((int(e["x"]), int(e["y"])))
            scorch[e["t"]] = set(live)
        def burning(t, x, y):
            return any(k <= t for k in scorch) and (x, y) in scorch[max(k for k in scorch if k <= t)]
        hits = collections.defaultdict(list)
        for e in ev:
            if e["kind"] == "HIT" and e["actor"].startswith("P") and e["a"] in ("16", "17") and int(e["id"]) >= 5 and (e["t"], e["actor"]) not in lands \
                    and e["t"] not in fire and not burning(e["t"], *pos.get(e["t"] - 1, {}).get(e["actor"], (int(e["x"]), int(e["y"])))):
                hits[e["t"]].append(e["actor"])
        for g in (e["t"] for e in ev if e["kind"] == "ANIM" and e["id"] == "14542"):
            b = max((m for m in moves if m[0] < g), default=None)
            if b is None:
                continue
            bx, by = b[1]
            beside = {p for p, (x, y) in pos.get(g - 1, {}).items() if max(max(bx - x, 0, x - (bx + 3)), max(by - y, 0, y - (by + 3))) == 1}
            out += [k for k in range(4) for p in hits.get(g + k, []) if p in beside]
    return hist(out)


def bork_death(ev):
    return min([e["t"] for e in ev if (e["kind"] == "ANIM" and e["id"] == "14541") or (e["kind"] == "NPC-" and base(e["actor"]) == "16640")], default=1 << 30)


def smork_moves(ev):
    """(tick, move) of every Smorks attack: A volley, N napalm, B bomb (the anim, one tick before its projectile)."""
    moves = {}
    for e in ev:
        if e["kind"] == "PROJ" and e["id"] in ("4056", "4057"):
            moves.setdefault(e["t"], "A")
        elif e["kind"] == "PROJ" and e["id"] == "4045":
            moves[e["t"]] = "N"
        elif e["kind"] == "PROJ" and e["id"] == "4440":
            moves[e["t"] - 1] = "B"
    return sorted(moves.items())


def bork_chains(fights):
    """Per bomb: ('stun', 14545 - bomb, 14549 - 14545, 14550 - 14549, 14551 - 14550, gong - 14550) or ('miss', 14550 - bomb, 14551 - 14550, gong - 14550)."""
    out = []
    for ev in fights.values():
        anims = collections.defaultdict(list)
        for e in ev:
            if e["kind"] == "ANIM" and base(e["actor"]) == "16640":
                anims[e["id"]].append(e["t"])
        def after(anim, t):
            return min((x for x in anims[anim] if x > t), default=None)
        for b in (e["t"] for e in ev if e["kind"] == "ANIM" and e["id"] == "14555"):
            stomp = after("14550", b)
            if stomp is None or stomp > bork_death(ev):
                continue
            stun = after("14545", b)
            end, gong = after("14551", stomp), after("14542", stomp)
            if end is None or gong is None:
                continue
            if stun is not None and stun < stomp:
                angry = after("14549", stun)
                out.append(("stun", stun - b, angry - stun, stomp - angry, end - stomp, gong - stomp))
            else:
                out.append(("miss", stomp - b, end - stomp, gong - stomp))
    return hist(out)


def smork_move_gaps(fights):
    """(move, next move, gap) between consecutive Smorks attacks while Bork lives, the gap after a bomb excluded."""
    out = []
    for ev in fights.values():
        dead = bork_death(ev)
        ms = [m for m in smork_moves(ev) if m[0] < dead]
        out += [(a, b, tb - ta) for (ta, a), (tb, b) in zip(ms, ms[1:]) if a != "B"]
    return hist(out)


def smork_after_bomb(fights):
    """Bomb to the next Smorks attack, by whether Bork was stunned (14545 within 6 of the bomb)."""
    out = []
    for ev in fights.values():
        stuns = [e["t"] for e in ev if e["kind"] == "ANIM" and e["id"] == "14545"]
        ms = smork_moves(ev)
        out += [("stun" if any(0 < x - ta <= 6 for x in stuns) else "miss", tb - ta) for (ta, a), (tb, b) in zip(ms, ms[1:]) if a == "B" and tb < bork_death(ev)]
    return hist(out)


def smork_enraged_gaps(fights):
    out = []
    for ev in fights.values():
        dead = bork_death(ev)
        ms = [m for m in smork_moves(ev) if m[0] > dead]
        out += [(a, b, tb - ta) for (ta, a), (tb, b) in zip(ms, ms[1:])]
        if ms:
            out.append(("death", ms[0][1], ms[0][0] - dead))
    return hist(out)


def smork_flights(fights):
    """(Bork alive, flight) of every volley projectile."""
    out = []
    for ev in fights.values():
        dead = bork_death(ev)
        out += [(e["t"] < dead, int(float(e["b"]))) for e in ev if e["kind"] == "PROJ" and e["id"] in ("4056", "4057")]
    return hist(out)


def volley_spares_nearest(fights):
    """Whether the one raider next to Bork for the 5 ticks before a volley (his tank) is among its targets, while Bork lives and two or more raiders
    are placed, outside the 20 ticks after a stomp (the tank changes there)."""
    out = []
    for ev in fights.values():
        dead = bork_death(ev)
        pos = collections.defaultdict(dict)
        for e in ev:
            if e["kind"] == "POS" and e["x"] != "-":
                pos[e["t"]][e["actor"]] = (int(e["x"]), int(e["y"]))
        bork, cur = {}, None
        for e in ev:
            if e["kind"] == "NPOS" and base(e["actor"]) == "16640":
                cur = (int(e["x"]), int(e["y"]))
            bork[e["t"]] = cur
        volleys = collections.defaultdict(set)
        for e in ev:
            if e["kind"] == "PROJ" and e["id"] in ("4056", "4057") and e["t"] < dead:
                volleys[e["t"]].add(e["a"])
        def beside(t):
            placed, b = pos.get(t, {}), bork.get(t)
            if len(placed) < 2 or b is None:
                return None
            near = [p for p, (x, y) in placed.items() if max(max(b[0] - x, 0, x - (b[0] + 3)), max(b[1] - y, 0, y - (b[1] + 3))) <= 1]
            return near[0] if len(near) == 1 else None
        stomps = [e["t"] for e in ev if e["kind"] == "ANIM" and e["id"] == "14550"]
        for t, targets in volleys.items():
            if any(0 <= t - st <= 20 for st in stomps):
                continue
            tanks = {beside(t - k) for k in range(1, 6)}
            if len(tanks) == 1 and None not in tanks:
                out.append(tanks.pop() in targets)
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
        moves = sorted((e["t"], (int(e["x"]), int(e["y"]))) for e in ev if e["kind"] == "NPOS" and base(e["actor"]) == "16640")
        smorks_gone = min((e["t"] for e in ev if e["kind"] == "NPC-" and base(e["actor"]) == "16641"), default=1 << 30)
        bork = {g: max(m for m in moves if m[0] < g)[1] for g in (e["t"] for e in ev if e["kind"] == "ANIM" and e["id"] == "14542") if g < smorks_gone}
        before = {(e["t"], e["actor"]): (int(e["x"]), int(e["y"])) for e in ev if e["kind"] == "POS" and e["x"] != "-"}
        for g in bork:
            for h in ev:
                if h["kind"] == "HIT" and h["actor"].startswith("P") and h["t"] == g and h["a"] in ("16", "17") and int(h["id"]) >= 10 and g not in lands:
                    total += 1
                    inside += shadowed(*bork[g], *before.get((g - 1, h["actor"]), (int(h["x"]), int(h["y"]))))
    return inside, total


BORK_FEET = [{(1, 0), (2, 0)}, {(0, 1), (1, 0)}, {(0, 1), (0, 2)}, {(0, 2), (1, 3)}, {(1, 3), (2, 3)}, {(2, 3), (3, 2)}, {(3, 1), (3, 2)}, {(2, 0), (3, 1)}]
BORK_STOMP_TEMPLATE = set()
BORK_COUNTS = {"napalm": (7, 10), "bomb": (3, 5)}


def bork_ground(fights, walk):
    """Bork ground gfx per event: dust delay residuals from 1 + Manhattan to the start-of-tick centre, wave tiles breaking the
    footprint, Smorks and walkable rules, ring tiles against the footprint ring one tick after the gong, stomp pulse sources,
    scorch tiles per landing with their fire and splat ids (the count only where no live scorch already took a tile of the window),
    scorch orientation against the splat, and the clear tick."""
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
            sources = []
            for k in range(4):
                got = dust[st + 1 + 3 * k]
                if not got:
                    continue
                b = pos(st)
                if b == (33, 30):
                    out["stomp tiles within 3 of the recorded template"][len(set(got) ^ BORK_STOMP_TEMPLATE) <= 3] += 1
                fits = [(dx, dy) for dx in range(-2, 6) for dy in range(-2, 6)
                        if all(d == 1 + abs(x - b[0] - dx) + abs(y - b[1] - dy) for (x, y), d in got.items())]
                sources.append(fits[0] if fits else None)
            if len(sources) == 4:
                out["stomp feet alternate on a facing pair"][sources[0] == sources[2] and sources[1] == sources[3] and
                    {sources[0], sources[1]} in BORK_FEET] += 1
        objs = {(e["t"], int(e["x"]), int(e["y"])): int(e["b"]) for e in ev if e["kind"] == "OBJ+" and e["id"] == "62544"}
        scorch_spans = [(e["t"], (int(e["x"]), int(e["y"]))) for e in ev if e["kind"] in ("OBJ+", "OBJ-") and e["id"] == "62544"]
        for t, tiles in splat.items():
            kind = "bomb" if max(d for _, d in tiles.values()) == 1 else "napalm"
            live = set()
            for tt, tile in sorted(scorch_spans):
                if tt <= t:
                    live ^= {tile}
            impact = next((tile for tile in tiles if sum(1 for ft, _ in fire[t] if ft == tile) == 2), None) if kind == "bomb" else \
                next((tile for tile, (_, d) in tiles.items() if d == 1), None)
            dx, dy = ((-1, 1), (-1, 1)) if kind == "bomb" else ((-3, 1), (-2, 3))
            clipped = impact is None or any((impact[0] + x, impact[1] + y) in live for x in range(dx[0], dx[1] + 1) for y in range(dy[0], dy[1] + 1))
            if not clipped:
                out["landing tile count in the recorded range"][(kind, BORK_COUNTS[kind][0] <= len(tiles) <= BORK_COUNTS[kind][1])] += 1
            for tile, (variant, delay) in tiles.items():
                n_fire = sum(1 for ft, _ in fire[t] if ft == tile)
                out["tile (kind, delay, 4046 count)"][(kind, delay, n_fire)] += 1
                out["scorch orientation = splat rotation"][objs.get((t + 1, *tile)) == 512 * variant] += 1
                out["scorch orientation"][512 * variant] += 1
        removed = [e for e in ev if e["kind"] == "OBJ-" and e["id"] == "62544"]
        cleanup = max((e["t"] for e in removed), default=None)
        for e in removed:
            placed = max((o[0] for o in objs if o[1:] == (int(e["x"]), int(e["y"])) and o[0] < e["t"]), default=None)
            if placed is not None and e["t"] != cleanup:
                out["scorch lifetime"][e["t"] - placed] += 1
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
    support("14542 next Bork anim", bork_next_anim(real), bork_next_anim(sim), failures)
    support("bomb -> stomp -> gong chain", bork_chains(real), bork_chains(sim), failures)
    for proj in ("4045", "4440"):
        exact(f"{proj} flight", proj_flights(real, proj), proj_flights(sim, proj), failures)
    support("volley flight (Bork alive)", smork_flights(real), smork_flights(sim), failures)
    support("smorks move gaps", smork_move_gaps(real), smork_move_gaps(sim), failures)
    support("bomb -> next smorks attack", smork_after_bomb(real), smork_after_bomb(sim), failures)
    support("smorks gaps after Bork died", smork_enraged_gaps(real), smork_enraged_gaps(sim), failures)
    support("volley hits the tank", volley_spares_nearest(real), volley_spares_nearest(sim), failures)
    support("gong hits beside Bork", bork_gong_hit_offsets(real), bork_gong_hit_offsets(sim), failures)
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


DREAD_ARENA = (24, 38)


def blood_after_slam(fights):
    """New blood tiles the tick after each slam whose blood ring (the 7x7 around the 5x5) lies inside the arena: at the edge the ring is clipped
    wherever the boss walked after the raiders."""
    out = []
    for ev in fights.values():
        objs = collections.Counter(e["t"] for e in ev if e["kind"] == "OBJ+" and e["id"] == "62609")
        track = sorted((e["t"], int(e["x"]), int(e["y"])) for e in ev if e["kind"] == "NPOS" and base(e["actor"]) == "16665")
        for g in slam_groups(ev):
            for t in g:
                at = [(x, y) for tt, x, y in track if tt <= t]
                if at and DREAD_ARENA[0] <= at[-1][0] - 1 and at[-1][0] + 5 <= DREAD_ARENA[1] and DREAD_ARENA[0] <= at[-1][1] - 1 and at[-1][1] + 5 <= DREAD_ARENA[1]:
                    out.append(objs[t + 1])
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


def dreadnoth_ground(fights):
    """Blood tiles and boss GFX per cause: blood orientation, tiles per event (slam +1, jump landing +3, jelly death 0), tile
    offsets from the boss SW tile, GFX 4209 (offset, delay, centre), and the 4210 spawn markers of every slam group: start tick and
    delay per tile, tiles per tick, distance of each derived tile from the nearest seed, last tick before the jellies emerge, and
    whether the markers equal the emerged jelly tiles."""
    orient, counts, offsets, jump_gfx = hist([]), hist([]), hist([]), hist([])
    starts, per_tick, derived, ends, equal = hist([]), hist([]), hist([]), hist([]), hist([])
    for ev in fights.values():
        blood, marks = collections.defaultdict(list), collections.defaultdict(dict)
        track0 = sorted((e["t"], int(e["x"]), int(e["y"])) for e in ev if e["kind"] == "NPOS" and base(e["actor"]) == "16665")
        for e in sorted(ev, key=lambda e: e["t"]):
            if e["kind"] == "OBJ+" and e["id"] == "62609":
                orient[e["b"]] += 1
                blood[e["t"]].append((int(e["x"]), int(e["y"])))
            elif e["kind"] == "GFX" and e["id"] == "4209":
                bx, by = next((x, y) for tt, x, y in reversed(track0) if tt <= e["t"])
                jump_gfx[(e["a"], int(e["x"]) - bx, int(e["y"]) - by)] += 1
            elif e["kind"] == "GFX" and e["id"] == "4210":
                marks[e["t"]][(int(e["x"]), int(e["y"]))] = int(e["a"])
        track = [(e["t"], int(e["x"]), int(e["y"])) for e in ev if e["kind"] == "NPOS" and base(e["actor"]) == "16665"]
        boss_at = lambda t: next((x, y) for tt, x, y in reversed(track) if tt <= t)
        for e in ev:
            if e["kind"] != "ANIM":
                continue
            cause = {"14777": ("slam", 1), "14780": ("jump", 3), "1587": ("death", 0)}.get(e["id"])
            if cause is None:
                continue
            counts[(cause[0], len(blood[e["t"] + cause[1]]))] += 1
            if e["id"] != "1587":
                for x, y in blood[e["t"] + cause[1]]:
                    offsets[(cause[0], x - boss_at(e["t"])[0], y - boss_at(e["t"])[1])] += 1
        spawns = {e["t"]: {(int(f["x"]), int(f["y"])) for f in ev if f["kind"] == "NPC+" and f["id"] == "16666" and f["t"] == e["t"]}
                  for e in ev if e["kind"] == "NPC+" and e["id"] == "16666"}
        groups = slam_groups(ev)
        for g in groups:
            emerge = next((t for t in spawns if 0 < t - g[-1] <= 20), None)
            if emerge is None:
                continue
            ends[emerge - max(t for t in marks if t < emerge and marks[t])] += 1
            equal[spawns[emerge] == set(marks[emerge - 1])] += 1
            for t0 in g:
                new = {tile: marks[t][tile] for t in range(t0 + 4, t0 + 7) for tile in marks[t] if tile not in marks[t - 1] and t - t0 <= 6}
                per_tick[tuple(sum(1 for tile in new if tile in marks[t0 + k] and tile not in marks[t0 + k - 1]) for k in (4, 5, 6))] += 1
                seeds = [tile for tile, a in new.items() if a == 1]
                for t in range(t0 + 4, t0 + 7):
                    for tile in marks[t]:
                        if tile in marks[t - 1]:
                            continue
                        starts[(t - t0, marks[t][tile])] += 1
                        if marks[t][tile] != 1 and seeds:
                            derived[min(max(abs(tile[0] - sx), abs(tile[1] - sy)) for sx, sy in seeds)] += 1
    return orient, counts, offsets, jump_gfx, starts, per_tick, derived, ends, equal


def dreadnoth_ground_laws(real, sim, failures):
    """Blood counts, marker starts and marker spread follow where the raiders stand and die (scripted raiders stack and rarely move),
    and the arena edge clips the blood ring wherever the boss walked after them, so their sim values must be recorded ones. The rest are two-way histograms, and the markers must equal the emerged jelly tiles."""
    labels = ("blood orientation", "blood tiles per event (cause, n)", "blood tile (cause, dx, dy) from boss SW", "4209 (delay, dx, dy)",
              "4210 tile start (tick after slam, delay)", "4210 new tiles per tick +4,+5,+6", "4210 derived distance from seed",
              "4210 last tick before emerge", "4210 markers == emerged tiles")
    for i, (label, r, s) in enumerate(zip(labels, dreadnoth_ground(real), dreadnoth_ground(sim))):
        (recorded_law if i in (1, 2, 4, 6) else shapes_law)(label, r, s, failures)


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
    dreadnoth_ground_laws(real, sim, failures)
    damage_laws(real, sim, {"16665"}, failures, rate=True, floored=False)
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


EZ_RING = [(-2, 2), (-1, 2), (0, 2), (1, 2), (2, 2), (2, 1), (2, 0), (2, -1), (2, -2), (1, -2), (0, -2), (-1, -2), (-2, -2), (-2, -1), (-2, 0), (-2, 1)]
EZ_STAR_IDS = {"4170": 35, "4171": 65, "4172": 95}
EZ_CORNERS = {0, 4, 8, 12}


def ez_star_template(bx, by):
    """The 5x5 ring around the 3x3 body (level 0) plus four arms 3 wide, 1, 1 (levels 1-3), clipped to the arena x27..35 y27..35."""
    tiles = {}
    for dx in range(-1, 4):
        for dy in range(-1, 4):
            if not (0 <= dx <= 2 and 0 <= dy <= 2):
                tiles[(bx + dx, by + dy)] = 0
    for level in (1, 2, 3):
        for o in (0, 1, 2) if level == 1 else (1,):
            for tile in ((bx + o, by - 1 - level), (bx + o, by + 3 + level), (bx - 1 - level, by + o), (bx + 3 + level, by + o)):
                tiles[tile] = level
    return {k: v for k, v in tiles.items() if 27 <= k[0] <= 35 and 27 <= k[1] <= 35}


def ezekiel_ground(fights):
    """Star, cleave dust and finale ground gfx per event: exact star template at his tile, (id, delay) pairs, tiles per event, gfx
    on ticks without the causing anim, cleave run centre on the 16-tile ring, finale layer shapes per boss side."""
    out = {k: hist([]) for k in ("star exact", "star (id, delay)", "star tiles", "cleave run centre", "cleave tiles", "cleave delay",
                                 "finale (id, delay)", "finale (u, entries)", "finale tiles", "finale layer sizes", "stray", "sword gfx")}
    sigs = hist([])
    for ev in fights.values():
        npos = [(e["t"], int(e["x"]), int(e["y"])) for e in ev if e["kind"] == "NPOS" and base(e["actor"]) == "16652"]
        gfx = collections.defaultdict(list)
        for e in ev:
            if e["kind"] == "GFX":
                gfx[e["t"]].append(e)
        anim_ticks = {k: {e["t"] for e in ev if e["kind"] == "ANIM" and base(e["actor"]) == "16652" and e["id"] == k} for k in ("14685", "14687")}
        for t, rows in gfx.items():
            out["sword gfx"][sum(e["id"] == "4181" for e in rows)] += 1
            for e in rows:
                if e["id"] in EZ_STAR_IDS and t not in anim_ticks["14687"] or e["id"] == "4394" and t not in anim_ticks["14685"]:
                    out["stray"][e["id"]] += 1
        for t in sorted(anim_ticks["14687"]):
            bx, by = next((x, y) for tt, x, y in reversed(npos) if tt <= t)
            rows = [e for e in gfx[t] if e["id"] in EZ_STAR_IDS]
            expected = ez_star_template(bx, by)
            for gid, base_delay in EZ_STAR_IDS.items():
                got = {(int(e["x"]), int(e["y"])): int(e["a"]) for e in rows if e["id"] == gid}
                out["star exact"][got == {k: base_delay + 2 * v for k, v in expected.items()} and len(got) == sum(e["id"] == gid for e in rows)] += 1
            out["star tiles"][len({(e["x"], e["y"]) for e in rows})] += 1
            for e in rows:
                out["star (id, delay)"][(e["id"], int(e["a"]))] += 1
        for t in sorted(anim_ticks["14685"]):
            rows = [e for e in gfx[t] if e["id"] == "4394"]
            xs, ys = [int(e["x"]) for e in rows], [int(e["y"]) for e in rows]
            cx, cy = (min(xs) + max(xs)) / 2, (min(ys) + max(ys)) / 2
            rel = {(int(e["x"]) - cx, int(e["y"]) - cy) for e in rows}
            missing = [i for i, r in enumerate(EZ_RING) if r not in rel]
            centre = next((m for m in missing if (m - 1) % 16 in missing and (m + 1) % 16 in missing), None)
            run_ok = rel <= set(EZ_RING) and len(rows) == len(rel) and len(missing) == 3 and centre is not None
            bx, by = next((x, y) for tt, x, y in reversed(npos) if tt <= t)
            beside = (cx, cy) != (bx + 1, by + 1)
            out["cleave run centre"]["bad" if not run_ok else "off his tile" if beside else "corner" if centre in EZ_CORNERS else "edge"] += 1
            out["cleave tiles"][len(rows)] += 1
            for e in rows:
                out["cleave delay"][int(e["a"])] += 1
        for t, rows in gfx.items():
            rows = [e for e in rows if e["id"] in ("4197", "4198")]
            if not rows:
                continue
            bx, by = next((x, y) for tt, x, y in reversed(npos) if tt <= t)
            layers = collections.defaultdict(dict)
            entries = collections.defaultdict(list)
            for e in rows:
                d = int(e["a"]) - 125
                x, y = int(e["x"]) - 30, int(e["y"]) - 30
                layers[d // 30][(x, y)] = d % 30 // 2
                entries[(x, y)].append(d % 30 // 2)
                out["finale (id, delay)"][(e["id"], int(e["a"]))] += 1
            for (x, y), us in entries.items():
                out["finale (u, entries)"][(us[0], len(us))] += 1
            out["finale tiles"][len(rows)] += 1
            out["finale layer sizes"][tuple(len(layers[l]) for l in range(3))] += 1
            sigs[((bx, by), tuple(tuple(sorted(layers[l].items())) for l in range(3)))] += 1
    return out, sigs


def ezekiel_ground_laws(real, sim, failures):
    """The star is the exact template at his tile and the dust a 13-tile ring with a straight run left out whose centre is never a corner
    unless he moved that tick and the ring sits on his old tile (8/628 recorded, 5 of them corner runs), both checked on every sim event. Delays, tile counts, gfx on non-anim ticks and finale layer shapes match in both directions,
    and every sim finale layer signature was recorded."""
    (rg, rs), (sg, ss) = ezekiel_ground(real), ezekiel_ground(sim)
    for key in ("star exact", "cleave run centre"):
        bad = {k: n for k, n in sg[key].items() if k in (False, "bad", "corner")}
        if bad or not sg[key]:
            failures.append(key)
        print(f"{'BAD' if bad or not sg[key] else 'ok '} {key:28s} real {fmt(rg[key]):42s} sim {fmt(sg[key])}")
    for key in ("star (id, delay)", "star tiles", "cleave tiles", "cleave delay", "finale (id, delay)", "finale (u, entries)", "finale tiles", "finale layer sizes", "stray", "sword gfx"):
        shapes_law(key, rg[key], sg[key], failures)
    unseen = sorted(k for k in ss if not rs[k])
    if unseen or not ss:
        failures.append("finale layer signatures not in the recordings")
    print(f"{'BAD' if unseen or not ss else 'ok '} {'finale layer signatures':28s} sim {len(ss)} distinct, {sum(ss.values())} waves, unseen {len(unseen)}")


def ezekiel(real, sim):
    failures = []
    for anim in ("14685", "14686", "14687", "14688", "14691", "14693"):
        compare(f"{anim} next attack", next_attack(real, "ezekiel", anim), next_attack(sim, "ezekiel", anim), failures)
    rp, rh, rw = ezekiel_cycles(real)
    sp, sh, sw = ezekiel_cycles(sim)
    # Shapes under 1% of the sim's cycles are scripted-team artefacts (a first melee delayed by positioning), not the rotation.
    shapes = collections.Counter(p.replace("G", "M") for p in sp)
    unknown = sorted(k for k, n in shapes.items() if n * 100 >= len(sp) and k not in {p.replace("G", "M") for p in rp})
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
    ezekiel_ground_laws(real, sim, failures)
    damage_laws(real, sim, {"16652"}, failures, rate=True)
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


def recorded_law(name, real, sim, failures):
    """Every common sim value (5 percent or more) appears in the recordings, for effects whose mix follows where the raiders stand."""
    extra = [k for k in sim if sim[k] * 20 >= sum(sim.values()) and not real[k]]
    if extra:
        failures.append(name)
    print(f"{'BAD' if extra else 'ok '} {name:28s} real {fmt(real):42s} sim {fmt(sim)}")


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


def ocellus_slam_rule(fights):
    """Per placed raider and Untether slam S (14861): ('out', outside his 3x3 at S-1, hit >= 15 at S) and ('in', inside it at S, hit >= 15 at S+1)."""
    out = []
    for ev in fights.values():
        pos = {(e["t"], e["actor"]): (int(e["x"]), int(e["y"])) for e in ev if e["kind"] == "POS" and e["x"] != "-"}
        boss = sorted((e["t"], (int(e["x"]), int(e["y"]))) for e in ev if e["kind"] == "NPOS" and base(e["actor"]) == "16677")
        hits = {(e["t"], e["actor"]) for e in ev if e["kind"] == "HIT" and e["actor"].startswith("P") and int(e["id"]) >= 15}
        for S in (e["t"] for e in ev if e["kind"] == "ANIM" and e["id"] == "14861"):
            b = max((m for m in boss if m[0] < S), default=None)
            if b is None:
                continue
            under = lambda xy: b[1][0] <= xy[0] <= b[1][0] + 2 and b[1][1] <= xy[1] <= b[1][1] + 2
            for p in {a for (t, a) in pos if t == S - 1}:
                if (S - 1, p) in pos:
                    out.append(("out", not under(pos[(S - 1, p)]), (S, p) in hits))
                if (S, p) in pos:
                    out.append(("in", under(pos[(S, p)]), (S + 1, p) in hits))
    return hist(out)


def ocellus_destroys(fights):
    """Per quadrant destroy (14860 at D): (form, 14862 - D, first attack after the 14862 - 14862): form 'spin' with 14859 at D-2, 'slam' with 14861 at D-4.
    Destroys whose fight ends before the next attack are left out."""
    out = []
    for ev in fights.values():
        anims = collections.defaultdict(list)
        for e in ev:
            if e["kind"] == "ANIM" and base(e["actor"]) == "16677":
                anims[e["id"]].append(e["t"])
        for d in anims["14860"]:
            form = "spin" if d - 2 in anims["14859"] else "slam" if d - 4 in anims["14861"] else "other"
            end = min((t for t in anims["14862"] if t > d), default=None)
            nxt = min((t for a in ("14842", "14846", "14851") for t in anims[a] if end is not None and t > end), default=None)
            if nxt is not None:
                out.append((form, end - d, nxt - end))
    return hist(out)


def ocellus_destroy_hp(fights):
    """Per quadrant destroy k (thresholds 75, 50, 25): (k, bar at the 14860 at most one step above the threshold, bar at least the threshold the tick
    before the special before it began). The client's bar rounds up, the sim's down."""
    out = []
    for ev in fights.values():
        bars = sorted((e["t"], int(e["id"])) for e in ev if e["kind"] == "NHP" and base(e["actor"]) == "16677" and e["id"] != "-1")
        rays = sorted(e["t"] for e in ev if e["kind"] == "ANIM" and e["id"] == "14848")
        specials = sorted([e["t"] for e in ev if e["kind"] == "ANIM" and e["id"] in ("14869", "14857")] +
            [t - 2 for i, t in enumerate(rays) if i == 0 or t - rays[i - 1] > 12])
        destroys = sorted(e["t"] for e in ev if e["kind"] == "ANIM" and e["id"] == "14860")
        bar = lambda t: max((b for b in bars if b[0] <= t), default=(0, 100))[1]
        for k, d in enumerate(destroys[:3]):
            thr = (75, 50, 25)[k]
            before = max((t for t in specials if t < d - 4), default=None)
            out.append((k + 1, bar(d) <= thr + 1, before is not None and bar(before - 1) >= thr))
    return hist(out)


def ocellus_p4(fights, max_rises=None):
    """Phase 4: ('start', first 14863 - third 14860), ('cycle', offsets of 14865, 14844, 14855, 14856, 14844 and the next 14863 from each 14863),
    ('rifts', spawn tick - next 14863, count); with max_rises only the first max_rises rises count."""
    out = []
    for ev in fights.values():
        anims = collections.defaultdict(list)
        for e in ev:
            if e["kind"] == "ANIM" and base(e["actor"]) == "16677":
                anims[e["id"]].append(e["t"])
        destroys = sorted(anims["14860"])
        rises = sorted(anims["14863"])[:max_rises]
        if len(destroys) >= 3 and rises:
            out.append(("start", rises[0] - destroys[2]))
        for c, nxt in zip(rises, rises[1:]):
            after = lambda a: sorted(t - c for t in anims[a] if c < t < nxt)
            out.append(("cycle", tuple(after("14865")), tuple(after("14844")), tuple(after("14855")), tuple(after("14856")), nxt - c))
        spawns = collections.Counter(e["t"] for e in ev if e["kind"] == "NPC+" and base(e["actor"]) == "16678" and rises and e["t"] >= rises[0])
        for t, n in sorted(spawns.items()):
            nxt = min((r for r in rises if r >= t), default=None)
            if nxt is not None:
                out.append(("rifts", t - nxt, n))
    return hist(out)


def ocellus_p1_guard(fights):
    """P1 Protect from Melee (overhead sprite 0): when it goes up, the latest barrage 14806 or expend 14810 and the ticks since; when it drops, the
    next charge 14809 or teleport 14812 and the ticks to it. The death teleport (14814 within 6 ticks of it) has no recorded overhead yet and is left out."""
    out = collections.Counter()
    for ev in fights.values():
        anims = [(e["t"], e["id"]) for e in ev if e["kind"] == "ANIM" and base(e["actor"]) == "16674"]
        for e in ev:
            if e["kind"] != "OVH" or base(e["actor"]) != "16674":
                continue
            t = e["t"]
            if e["id"] != "-":
                prev = [(a, i) for a, i in anims if a <= t and i in ("14806", "14810")]
                if prev:
                    out[("on", prev[-1][1], t - prev[-1][0])] += 1
            else:
                nxt = [(a, i) for a, i in anims if a >= t and i in ("14809", "14812")]
                if nxt and not (nxt[0][1] == "14812" and any(i == "14814" and 0 < a - nxt[0][0] <= 6 for a, i in anims)):
                    out[("off", nxt[0][1], nxt[0][0] - t)] += 1
    return out


def ocellus_p4_overheads(fights, horizon=56):
    """Every P4 overhead change on 16677 as (sprite, ticks since the latest rise 14863): 15 Deflect Melee, 11 Soul Split, 16 Deflect Range. Only the
    first horizon ticks of a cycle count: ocellus_11's recording ends 56 into its only cycle."""
    out = collections.Counter()
    for ev in fights.values():
        rises = [e["t"] for e in ev if e["kind"] == "ANIM" and e["id"] == "14863"]
        for e in ev:
            if e["kind"] != "OVH" or base(e["actor"]) != "16677":
                continue
            prev = [r for r in rises if r <= e["t"]]
            if prev and e["t"] - prev[-1] < horizon:
                out[(e["id"].split("+")[0], e["t"] - prev[-1])] += 1
    return out


def ocellus_quad_tells(fights, max_quads=None):
    """The tell on Ocellus at each quadrant attack 14844 (4328 melee, 4329 magic, 4330 ranged): ('first', tell) and (previous, tell) for each
    consecutive pair, over the first max_quads attacks of a fight."""
    out = collections.Counter()
    for ev in fights.values():
        quads = sorted(e["t"] for e in ev if e["kind"] == "ANIM" and e["id"] == "14844")[:max_quads]
        tells = []
        for q in quads:
            ts = [i for e in ev if e["kind"] == "SPOT" and base(e["actor"]) == "16677" and e["t"] == q for i in e["id"].split("+") if i in ("4328", "4329", "4330")]
            if ts:
                tells.append(ts[0])
        if tells:
            out[("first", tells[0])] += 1
        for a, b in zip(tells, tells[1:]):
            out[(a, b)] += 1
    return out


def ocellus_void_drops(fights, horizon=40):
    """Void drop gfx 4339 as ticks since the latest rise; ocellus_11's P0 died 40 into the cycle."""
    out = collections.Counter()
    for ev in fights.values():
        rises = [e["t"] for e in ev if e["kind"] == "ANIM" and e["id"] == "14863"]
        for e in ev:
            if e["kind"] == "GFX" and e["id"] == "4339":
                prev = [r for r in rises if r <= e["t"]]
                if prev and e["t"] - prev[-1] < horizon:
                    out[e["t"] - prev[-1]] += 1
    return out


def ocellus_p4_laws(real, sim):
    """Phase 4 and the spin (one check per destroy so far: 5, 8, 11) against an invulnerable sim team (the dump's invulnerable mode), so every sim
    fight goes through all three destroys."""
    failures = []
    shapes_law("14869 -> 14871 (short, long)", next_attack(real, "ocellus", "14869"), next_attack(sim, "ocellus", "14869"), failures)
    shapes_law("14869 -> 4346 debris", gfx_offsets(real, "14869", "4346", 8), gfx_offsets(sim, "14869", "4346", 8), failures)
    rises = max(len([e for e in ev if e["kind"] == "ANIM" and e["id"] == "14863"]) for ev in real.values())
    support("phase 4", ocellus_p4(real), ocellus_p4(sim, rises), failures)
    support("P4 overheads", ocellus_p4_overheads(real), ocellus_p4_overheads(sim), failures)
    support("P4 void drops", ocellus_void_drops(real), ocellus_void_drops(sim), failures)
    quads = max(len([e for e in ev if e["kind"] == "ANIM" and e["id"] == "14844"]) for ev in real.values())
    support("P4 quadrant tells", ocellus_quad_tells(real), ocellus_quad_tells(sim, quads), failures)
    return failures


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
    compare("14842 P0 hit delay", anim_to_p0_hit(real, "14842", 1), anim_to_p0_hit(sim, "14842", 1), failures)
    compare("14861 -> siphon spawn", first_offsets(real, "14861", "NPC+", "16679", 10), first_offsets(sim, "14861", "NPC+", "16679", 10), failures)
    compare("14881 -> first 14842", anim_offsets(real, "14881", "14842", 20), anim_offsets(sim, "14881", "14842", 20), failures)
    support("slam: (check, where, hit)", ocellus_slam_rule(real), ocellus_slam_rule(sim), failures)
    support("destroy (form, end, next)", ocellus_destroys(real), ocellus_destroys(sim), failures)
    support("destroys at 75/50/25%", ocellus_destroy_hp(real), ocellus_destroy_hp(sim), failures)
    drained_miss_law("P1 miss after the drain", {"16674"}, real, sim, failures)
    drained_miss_law("P3 miss after the drain", {"16677"}, real, sim, failures)
    drained_miss_law("Blade miss", {"16676"}, real, sim, failures)
    support("P1 melee prayer window", ocellus_p1_guard(real), ocellus_p1_guard(sim), failures)
    support_reached("phase 4", ocellus_p4(real), ocellus_p4(sim), failures)
    ocellus_ground_laws(real, sim, failures)
    # Phase 1 only: phase 3's misses follow the team's Division throughput (run 6, 6 raiders, seven throws in phase 3: 0.43-0.48) that a 3-raider
    # scripted team does not reach (0.55-0.60), open until more post-hotfix fights exist.
    damage_laws(real, sim, {"16673", "16674"}, failures, rate=False)
    return failures


def main():
    boss, sim_path = sys.argv[1], sys.argv[2]
    party = sys.argv[3] if len(sys.argv) > 3 else None
    real = load(FIXTURES / f"tfa_{boss.split('_')[0]}_events.tsv")
    if party:
        meta = {r["fight_id"]: r for r in csv.DictReader(open(FIXTURES / "tfa_fights.tsv"), delimiter="\t")}
        keep = (lambda n: int(n) >= int(party[:-1])) if party.endswith("+") else (lambda n: n == party)
        real = {k: v for k, v in real.items() if keep(meta[k]["party_size"])}
    sim = load(sim_path)
    failures = {"guardian_slam": guardian_slam_laws, "guardian": guardian, "xarth": xarth, "dreadnoth": dreadnoth, "ezekiel": ezekiel, "bork": bork, "moia": moia, "ocellus": ocellus, "ocellus_p4": ocellus_p4_laws}[boss](real, sim)
    print(f"{len(failures)} laws differ: {failures}" if failures else "all laws agree")
    sys.exit(1 if failures else 0)


main()
