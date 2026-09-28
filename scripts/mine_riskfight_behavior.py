#!/usr/bin/env python3
"""Mine RISKFIGHT_HUMANLIKE per-mille rates from SPECTATOR_COMBAT archives.

A bout is a mutual player pair with hitsplats both ways, from its first
interaction to the first round end: a tab 4069, a standard teleport 714,
a death, or either fighter leaving view. Rates come from modeled-setup
fighters only. Own and opponent HP are health-bar ratios on scale 30,
mapped to HP at base 99, and a bar is stale from an eat until its next
hitsplat. Spec energy is estimated from 100 at each presence start.
"""

import argparse
import collections
import glob
import json
import os
import subprocess

TENT, VW, VW_SPEC, DH_CRUSH, DH_SLASH = 1658, 390, 11275, 2067, 2066
MAUL, MAUL_SPEC, DDS, DDS_SPEC, ATLATL = 1665, 1667, 376, 1062, 11057
CONSUME, TAB, TAB_TAIL, TELEPORT, DEATH = 829, 4069, 4071, 714, 836
VENG_CAST_ANIMS = {8316, 8317}
VENG_SPOTANIMS = {726, 2605}
ATTACK_ANIMS = {TENT, VW, VW_SPEC, DH_CRUSH, DH_SLASH, MAUL, MAUL_SPEC, DDS, DDS_SPEC, ATLATL}
MODELED_ATTACKS = {TENT, VW, VW_SPEC, DH_CRUSH, DH_SLASH, MAUL, MAUL_SPEC}
SPEC_COST = {VW_SPEC: 50, MAUL_SPEC: 50, DDS_SPEC: 25}
WEAPON_NAMES = {TENT: "tent", VW: "vw", VW_SPEC: "vw", DH_CRUSH: "dh", DH_SLASH: "dh",
                MAUL: "maul", MAUL_SPEC: "maul", DDS: "dds", DDS_SPEC: "dds", ATLATL: "atlatl"}
FIGHTER_ROLES = {"SELECTED", "PLAYER_FIGHTER"}
BASE_HP = 99
TARGET_MEMORY_TICKS = 20
FOLLOW_TAB_TICKS = 15
EAT_LOCK_TICKS = 3


def hp_of(r30):
    """Midpoint HP of a scale-30 bar at base 99."""
    if r30 == 0:
        return 0
    lower = 1 if r30 == 1 else (BASE_HP * (r30 - 1) + 28) // 29
    upper = BASE_HP if r30 == 30 else (BASE_HP * r30 - 1) // 29
    return (lower + upper) / 2


class Actor:
    def __init__(self):
        self.name = None
        self.fighter = False
        self.anims = []
        self.hits = []
        self.targets = []
        self.bars = []
        self.veng_casts = []
        self.leaves = []
        self.enters = []
        self.deaths = []
        self.spots = set()


def load(path):
    """Actors keyed by actorTraceId from one archive, or None for a non-spectator archive."""
    proc = subprocess.run(["zstd", "-dc", path], check=True, capture_output=True)
    actors = collections.defaultdict(Actor)

    def seen(ref):
        actor = actors[ref["actorTraceId"]]
        actor.name = ref["normalizedName"]
        actor.fighter |= ref["scopeRole"] in FIGHTER_ROLES and ref["kind"] == "PLAYER"
        return actor

    def observe_state(actor, tick, state):
        bar = state["healthBar"]
        if bar["availability"] == "OBSERVED_RAW_BAR":
            actor.bars.append((tick, round(bar["ratio"] * 30 / bar["scale"])))
        current = {s["id"] for s in state["effects"]["spotAnims"] if s["id"] in VENG_SPOTANIMS}
        if current - actor.spots:
            actor.veng_casts.append(tick)
        actor.spots = current

    for line in proc.stdout.splitlines():
        record = json.loads(line)
        kind, tick, payload = record["eventKind"], record["lastObservedGameTick"], record["payload"]
        if kind == "SESSION_STARTED" and "spectatorPolicy" not in payload:
            return None
        if kind == "ACTOR_VISIBILITY_OBSERVED" and "actorTraceId" in payload:
            actor = seen(payload)
            if payload["visibility"] == "ENTERED":
                actor.enters.append(tick)
                actor.spots = set()
                observe_state(actor, tick, payload["initialState"])
            else:
                actor.leaves.append(tick)
        elif kind in ("ACTOR_STATE_OBSERVED", "ACTOR_EFFECT_OBSERVED", "ANIMATION_OBSERVED"):
            actor = seen(payload["actor"])
            observe_state(actor, tick, payload["state"])
            if kind == "ANIMATION_OBSERVED" and payload["animation"] != -1:
                actor.anims.append((tick, payload["animation"]))
        elif kind == "HITSPLAT_APPLIED_OBSERVED":
            seen(payload["recipient"]).hits.append((tick, payload["hitsplatType"], payload["amount"]))
        elif kind == "INTERACTING_OBSERVED" and "actorTraceId" in payload["source"]:
            target = payload["target"].get("actorTraceId")
            seen(payload["source"]).targets.append((tick, target))
        elif kind == "ACTOR_DEATH_OBSERVED":
            seen(payload["actor"]).deaths.append(tick)
    return {k: v for k, v in actors.items() if v.name}


def target_at(actor, tick):
    """Last non-null interaction target within TARGET_MEMORY_TICKS before tick."""
    best = None
    for t, target in actor.targets:
        if t > tick:
            break
        if target is not None:
            best = (t, target)
    return best[1] if best and tick - best[0] < TARGET_MEMORY_TICKS else None


def round_end(actor, start):
    ends = [t for t, a in actor.anims if a in (TAB, TELEPORT) and t >= start]
    ends += [t for t in actor.deaths + actor.leaves if t >= start]
    return min(ends, default=None)


def bouts(actors):
    """(a, b, start, end, end_kind) for every mutual pair round with hits both ways."""
    fighters = {k for k, v in actors.items() if v.fighter}
    edges = collections.defaultdict(list)
    for a in fighters:
        for t, target in actors[a].targets:
            if target in fighters and target != a:
                edges[tuple(sorted((a, target)))].append(t)
    out = []
    for (a, b), ticks in edges.items():
        ticks.sort()
        cursor = ticks[0]
        while True:
            ends = [e for e in (round_end(actors[a], cursor), round_end(actors[b], cursor)) if e is not None]
            end = min(ends) if ends else max(ticks)
            mutual = {x for x in (a, b) if any(cursor <= t <= end and target_at(actors[x], t) == (b if x == a else a)
                                               for t, _ in actors[x].targets)}
            hit_both = all(any(cursor <= t <= end and kind in (12, 13, 16, 17)
                               for t, kind, _ in actors[x].hits) for x in (a, b))
            if len(mutual) == 2 and hit_both:
                out.append((a, b, cursor, end, end_kind(actors, a, b, end)))
            later = [t for t in ticks if t > end]
            if not later:
                break
            cursor = later[0]
    return out


def end_kind(actors, a, b, end):
    for x in (a, b):
        if end in actors[x].deaths:
            return "death"
    for x in (a, b):
        if any(t == end and anim in (TAB, TELEPORT) for t, anim in actors[x].anims):
            return "teleport"
    return "left_view"


def setup(actor, start, end):
    weapons = {a for t, a in actor.anims if start <= t <= end and a in ATTACK_ANIMS}
    if weapons and weapons <= MODELED_ATTACKS and weapons & {DH_CRUSH, DH_SLASH, VW, VW_SPEC}:
        return "dharok_veng"
    return "+".join(sorted({WEAPON_NAMES[w] for w in weapons})) or "none"


def bar_timeline(actor, start, end):
    """Per tick own r30 or None when unknown or stale after an eat."""
    eats = {t for t, a in actor.anims if a == CONSUME}
    bar_ticks = dict(actor.bars)
    hit_ticks = {t for t, _, _ in actor.hits}
    current = None
    for t, r in actor.bars:
        if t < start:
            current = r
    out = {}
    for t in range(start, end + 1):
        if t in bar_ticks:
            current = bar_ticks[t]
        if t in eats:
            out[t] = current
            current = None
            continue
        if current is None and t in hit_ticks and t in bar_ticks:
            current = bar_ticks[t]
        out[t] = current
    return out


def spec_energy(actor, start, end):
    """Per tick estimated special energy, 100 at the last presence start."""
    begin = max([t for t in actor.enters if t <= start], default=start)
    costs = collections.Counter()
    for t, a in actor.anims:
        if a in SPEC_COST and begin <= t <= end:
            costs[t] += SPEC_COST[a]
    energy, out = 100.0, {}
    for t in range(begin, end + 1):
        energy = min(100.0, energy + 0.2) - costs[t]
        out[t] = energy
    return out


class Counts:
    def __init__(self):
        self.eat_events = collections.Counter()
        self.eat_ticks = collections.Counter()
        self.vw_specs = self.vw_opportunities = 0
        self.maul_specs = self.maul_ticks = 0
        self.axes = self.axe_opportunities = 0
        self.tabs = self.tab_ticks = 0
        self.first_tab_hp = []
        self.follow_tab_gaps = []
        self.attack_to_tab = []
        self.orb_axes = 0
        self.veng_recast_after_ready = []
        self.bout_ends = collections.Counter()
        self.setups = collections.Counter()


EAT_BANDS = [(0, 40), (40, 65), (65, 73), (73, 90), (90, 200)]


def band(hp):
    return next(f"{lo}-{hi}" for lo, hi in EAT_BANDS if lo <= hp < hi)


def mine(actors, counts):
    for a, b, start, end, kind in bouts(actors):
        counts.bout_ends[kind] += 1
        for me, opp in ((a, b), (b, a)):
            s = setup(actors[me], start, end)
            counts.setups[s] += 1
            if s == "dharok_veng":
                mine_fighter(actors[me], actors[opp], start, end, counts)


def mine_fighter(me, opp, start, end, counts):
    own, other = bar_timeline(me, start, end), bar_timeline(opp, start, end)
    energy = spec_energy(me, start, end)
    anims = collections.defaultdict(set)
    for t, anim in me.anims:
        if start <= t <= end:
            anims[t].add(anim)
    last_eat = -10**9
    for t in range(start, end + 1):
        if CONSUME in anims[t] and own[t] is not None:
            counts.eat_events[band(hp_of(own[t]))] += 1
        if t - last_eat >= EAT_LOCK_TICKS and own[t] is not None:
            counts.eat_ticks[band(hp_of(own[t]))] += 1
        if CONSUME in anims[t]:
            last_eat = t
        attacks = anims[t] & ATTACK_ANIMS
        o = other[t]
        if attacks and o is not None and o <= 19 and energy[t] + SPEC_COST.get(VW_SPEC, 0) * (VW_SPEC in attacks) >= 50:
            counts.vw_opportunities += 1
            counts.vw_specs += VW_SPEC in attacks
        if o is not None and o <= 19 and own[t] is not None and own[t] < 30 and \
                energy[t] + 50 * (MAUL_SPEC in anims[t]) >= 99.5:
            counts.maul_ticks += 1
            counts.maul_specs += MAUL_SPEC in anims[t] and MAUL_SPEC not in anims[t - 1]
        normal = attacks - {VW_SPEC, MAUL_SPEC, DDS_SPEC}
        if normal and own[t] is not None and own[t] < 30 and o is not None and 12 <= o <= 23:
            counts.axe_opportunities += 1
            counts.axes += bool(normal & {DH_CRUSH, DH_SLASH})
        if own[t] is not None and hp_of(own[t]) < 40:
            counts.tab_ticks += 1
    my_tabs = [t for t, anim in me.anims if anim in (TAB, TELEPORT) and start <= t <= end + FOLLOW_TAB_TICKS]
    opp_tabs = [t for t, anim in opp.anims if anim in (TAB, TELEPORT) and start <= t <= end + FOLLOW_TAB_TICKS]
    for t in my_tabs:
        leader = [u for u in opp_tabs if 0 < t - u <= FOLLOW_TAB_TICKS]
        if leader:
            counts.follow_tab_gaps.append(t - leader[0])
            continue
        hp = own.get(t)
        counts.first_tab_hp.append(None if hp is None else round(hp_of(hp)))
        counts.tabs += hp is not None and hp_of(hp) < 40
        last_attack = max([u for u, anim in me.anims if anim in ATTACK_ANIMS and u <= t], default=None)
        if last_attack is not None:
            counts.attack_to_tab.append(t - last_attack)
    orbs = [t for t, kind, amount in me.hits if kind in (16, 17) and amount == 10 and start <= t <= end]
    axes = [t for t, anim in me.anims if anim in (DH_CRUSH, DH_SLASH) and start <= t <= end]
    counts.orb_axes += sum(1 for t in axes if any(0 <= t - o <= 2 for o in orbs))
    casts = sorted(c for c in me.veng_casts if start <= c <= end)
    counts.veng_recast_after_ready.extend(c2 - (c1 + 50) for c1, c2 in zip(casts, casts[1:]))


def per_mille(numerator, denominator):
    return round(1000 * numerator / denominator)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--recordings", default=os.path.expanduser("~/.light2/gameplay-recordings/spectator-combat"))
    parser.add_argument("--out")
    args = parser.parse_args()
    counts = Counts()
    archives = collections.Counter()
    for path in sorted(glob.glob(os.path.join(args.recordings, "session_*.jsonl.zst"))):
        actors = load(path)
        archives["spectator" if actors is not None else "not_spectator"] += 1
        if actors is not None:
            mine(actors, counts)
    hazard = {k: (counts.eat_events[k], counts.eat_ticks[k]) for k in counts.eat_ticks}
    low_events = counts.eat_events["0-40"] + counts.eat_events["40-65"]
    low_ticks = counts.eat_ticks["0-40"] + counts.eat_ticks["40-65"]
    rolls = {
        "RISKFIGHT_HL_EARLY_EAT_PM": per_mille(counts.eat_events["65-73"], counts.eat_ticks["65-73"]),
        "RISKFIGHT_HL_EAT_DELAY_PM": 1000 - per_mille(low_events, low_ticks),
        "RISKFIGHT_HL_VW_PM": per_mille(counts.vw_specs, counts.vw_opportunities),
        "RISKFIGHT_HL_MAUL_PM": per_mille(counts.maul_specs, counts.maul_ticks),
        "RISKFIGHT_HL_AXE_PM": per_mille(counts.axes, counts.axe_opportunities),
        "RISKFIGHT_HL_TP_PM": per_mille(counts.tabs, counts.tab_ticks),
    }
    report = {
        "archives": dict(archives),
        "bout_ends": dict(counts.bout_ends),
        "fighter_setups": dict(counts.setups),
        "eat_events_over_eligible_ticks_by_hp": hazard,
        "vw_specs_over_attacks": (counts.vw_specs, counts.vw_opportunities),
        "maul_specs_over_ready_ticks": (counts.maul_specs, counts.maul_ticks),
        "axes_over_normal_attacks": (counts.axes, counts.axe_opportunities),
        "leading_tabs_under_40_over_ticks_under_40": (counts.tabs, counts.tab_ticks),
        "leading_tab_hp": sorted(counts.first_tab_hp, key=lambda x: (x is None, x)),
        "follow_tab_gaps": sorted(counts.follow_tab_gaps),
        "attack_to_leading_tab": sorted(counts.attack_to_tab),
        "axes_within_2_ticks_of_orb": counts.orb_axes,
        "veng_recast_ticks_after_cooldown": sorted(counts.veng_recast_after_ready),
        "rolls": rolls,
    }
    if args.out:
        with open(args.out, "w") as handle:
            json.dump(report, handle, indent=1)
    print(json.dumps(report, indent=1))


if __name__ == "__main__":
    main()
