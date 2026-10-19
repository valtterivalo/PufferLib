#!/usr/bin/env python3
"""How spectated Dharok fighters play: armour strips, greataxe wields and the locator orb (riskfight_script's Dharok rules and the pool's orb share).

Usage: python3 scripts/measure_dharok_play.py [--duels FILE]      (python3 standard library only; deterministic)

Input: spectator.duels (tools/spectator_export.py in the light repo): per duel a DUEL line, E lines (tick, side, the 12 composition slots head cape neck weapon body
shield arms legs hair hands feet jaw, -1 for an empty slot) on every kit change, and R lines per tick (13 fields per side: bar weapon eat att spec dmg nhit
veng_cast veng_proc meal meal_heal dead anims). Rows marked `third` (a third party on either fighter) are skipped.

A Dharok fighter is a duel side that, on some tick, wears Dharok's helm, platebody and platelegs together or wields the greataxe (any degradation). His bar (scale
30) is carried forward over ticks without a read; bands are bar 0-11, 12-23, 24-30 (hitpoints about 0-39, 40-79, 80-99 of 99).

(a) A strip starts on a tick he wears the helm and the full set the tick before, and the platebody and/or platelegs slot is empty; it lasts while the helm stays on
    and a piece is off. A strip of 1 tick, or one starting on a tick his weapon changes, is a switch artefact (the client drops the body on some weapon switches) and
    is not counted. Reported: starts per eligible fighter-tick (helm, body and legs on), the duration, his bar band at the start, the weapon in hand, and what
    follows: both pieces back on (re-equip) and the greataxe wielded, in which order.
(b) Greataxe wields: ticks his weapon becomes the axe, per fighter-tick without the axe in hand, by his own bar band (the profiles' axe_pm bands, their training
    mean in mine_riskfight_behavior.py's AXE_WIELD_PM_OWN_BAND) and by the opponent's bar band.
(c) The orb ("Feel", 10 damage to himself): a single hitsplat of exactly 10 on a tick with no swing of the opponent in the 4 ticks up to it, no Vengeance proc
    from the opponent and no swing of his own in the 2 ticks before (recoil). The same count for every other damage value is the baseline: a spike at 10 is the orb.
    Also reported: axe swings (2066/2067) with a single 10 in the 2 ticks up to them (what mine_riskfight_behavior.py fits orb_axe_pm on), and the share of
    Dharok fighters with >= LONG_WIELDS axe wields carrying an orb, bounded below by an unexplained 10 and above by any single 10: its midpoint is the pool's
    RF_KIT_ORB_PM (riskfight_kits.h). The strip numbers are riskfight_policy.h's RF_STRIP_* constants.
(d) Greataxe swings: of a Dharok fighter's swings without a special, the share with the axe anim, by his own bar band the tick before (his hitpoints at his last
    hit), his Vengeance up (a cast seen and no proc of his since) and the opponent's bar the tick before: the key riskfight_policy.h's RF_AXE_SWING_PM had before
    scripts/fit_dharok_axe.py keyed it on his own hitpoints at a fresh read. Then the knockouts of the Dharok v Dharok duels: the
    killer's state at his axe swing (Vengeance up, an orb drop or a strip in the 5 ticks up to it, the opponent swinging the tick before, both bars) against
    all axe swings and all Dharok ticks, and the axe swings carrying his own reflect on the knockout.
(e) Voidwaker specials of Dharok fighters holding a Voidwaker, by the opponent's bar the tick before: specials per 1,000 ticks and their cumulative share. The
    lowest bar holding 95% of the specials at or below it is riskfight_policy.h's RF_VW_SPEC_BAR_MAX (the script's gate). The rate stays at 6-13 per 1,000
    ticks up to bar 25 and falls to under 1 from bar 27; the gate cuts where the specials end, not at a fixed share of the bar.
(f) The orb per tick for riskfight_policy.h's RF_DHAROK_ORB_PM, on the Dharok v Dharok duels from the first hit to the knockout: a lone 10 on a tick with no
    swing of the opponent on ticks i-1..i+1 and no Vengeance proc of his on i-1, i. On any other tick an orb can't be told from his hit, so those ticks leave both
    the count and the denominator (the tick after his swing always does): no orb seen there is not evidence of none. By his own hitpoints band and the axe in
    hand the tick before, over KIT_ORB_PM; then the visible rate by ticks since the opponent's swing, the ready tick (his weapon frees the next swing) apart.
(g) Setups (riskfight_model.h's spectated Dharok kits): per Dharok fighter of the Dharok v Dharok duels, the main weapon (most ticks in hand, the axe and
    mauls aside), the Voidwaker or Armadyl godsword seen wielded besides it, and a granite maul seen wielded. The bag is not in the data: these are lower bounds.
"""
import argparse
import collections
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "scripts"))
from mine_riskfight_pool import SWING_ANIMS, DEFAULT_DUELS  # noqa: E402

DHAROK_AXES = {4718, 4886, 4887, 4888, 4889}
DHAROK_HELM = {4716, 4880, 4881, 4882, 4883}
DHAROK_BODY = {4720, 4892, 4893, 4894, 4895}
DHAROK_LEGS = {4722, 4898, 4899, 4900, 4901}
HEAD, WEAPON, BODY, LEGS = 0, 3, 4, 7
BAR, SPEC, DMG, NHIT, VENG_CAST, VENG_PROC, DEAD, ANIMS = 0, 4, 5, 6, 7, 8, 11, 12
WEAPON_FIELD = 1  # the R line's weapon (item id in hand); WEAPON above is the composition slot
VOIDWAKER = 27690
VOIDWAKERS = {VOIDWAKER, 29607}     # (g) the raw ids osrs_items.h maps to the Voidwaker, abyssal tentacle, Armadyl godsword and granite maul
TENTACLES = {12006, 26484}
GODSWORDS = {11802, 20368, 29605}
MAULS = {4153, 12848, 24225, 24227}
VW_GATE_SHARE = 0.95  # (e)
FOLLOW = 20       # ticks after a strip's start in which its outcome is read
OPP_SWING_WINDOW = 4
OWN_SWING_WINDOW = 2
LONG_WIELDS = 8   # axe wields that make a fighter's orb share readable (the share of fighters showing the orb levels off from about here)
DH_SWINGS = {2066, 2067}
AXE_POOL_MIN = 60  # swings under which a (d) cell takes its Vengeance pair's pooled share
KIT_ORB_PM = 600   # riskfight_kits.h's RF_KIT_ORB_PM: (f) divides by it
WEAPON_SPEED = {27690: 4, 12006: 4, 4151: 4, 24225: 7, 4153: 7, 12848: 7, 11802: 6, 13652: 4, 21009: 5, 28338: 5, **{a: 7 for a in DHAROK_AXES}}  # (f) his clock


def die(message):
    print(f"measure_dharok_play: {message}", file=sys.stderr)
    sys.exit(2)


def parse(path):
    duels, duel = [], None
    with open(path) as fh:
        for number, line in enumerate(fh, 1):
            f = line.split()
            if not f:
                continue
            if f[0] == "DUEL":
                if len(f) != 9: die(f"{path}:{number}: bad DUEL line")
                duel = dict(id=f[1], start=int(f[3]), end=int(f[4]), eq=([], []), rows=[])
                duels.append(duel)
            elif f[0] == "E":
                if duel is None or len(f) != 15 or f[2] not in ("a", "b"): die(f"{path}:{number}: bad E line")
                duel["eq"][f[2] == "b"].append((int(f[1]), [int(v) for v in f[3:]]))
            elif f[0] == "R":
                if duel is None or len(f) != 29: die(f"{path}:{number}: bad R line")
                duel["rows"].append((int(f[1]), int(f[2]), f[3:16], f[16:29]))
            else:
                die(f"{path}:{number}: unknown line kind {f[0]!r}")
    return duels


def kits_by_tick(eq, ticks):
    out, current, i = {}, None, 0
    for t in ticks:
        while i < len(eq) and eq[i][0] <= t:
            current = eq[i][1]
            i += 1
        out[t] = current
    return out


def swung(side_row):
    return side_row[ANIMS] != "-" and any(int(a) in SWING_ANIMS for a in side_row[ANIMS].split("|"))


def band(bar):
    return (bar >= 12) + (bar >= 24)


BAND_NAMES = ["bar 0-11 (hp ~0-39)", "bar 12-23 (hp ~40-79)", "bar 24-30 (hp ~80-99)"]


class Side:
    """One fighter of one duel, per tick: kit, bar carried forward, the opponent's bar, swings and hits."""

    def __init__(self, duel, side):
        ticks = [r[0] for r in duel["rows"]]
        self.ticks = ticks
        kit = kits_by_tick(duel["eq"][side], ticks)
        self.kit = [kit[t] for t in ticks]
        self.third = [r[1] for r in duel["rows"]]
        mine = [r[2 + side] for r in duel["rows"]]
        his = [r[3 - side] for r in duel["rows"]]
        self.bar, self.opp_bar = carried([int(r[BAR]) for r in mine]), carried([int(r[BAR]) for r in his])
        self.swing, self.opp_swing = [swung(r) for r in mine], [swung(r) for r in his]
        self.axe_swing = [r[ANIMS] != "-" and any(int(a) in DH_SWINGS for a in r[ANIMS].split("|")) for r in mine]
        self.dmg, self.nhit = [int(r[DMG]) for r in mine], [int(r[NHIT]) for r in mine]
        self.opp_veng_proc = [int(r[VENG_PROC]) for r in his]
        self.veng_proc, self.special = [int(r[VENG_PROC]) for r in mine], [r[SPEC] != "-" for r in mine]
        self.veng_up, up = [], 0  # his Vengeance: a cast seen, no proc of his since
        for r in mine:
            up = up or r[VENG_CAST] != "0"
            self.veng_up.append(up)
            if int(r[VENG_PROC]): up = 0

    def worn(self, i, slot, ids):
        return self.kit[i] is not None and self.kit[i][slot] in ids

    def full_set(self, i):
        return self.worn(i, HEAD, DHAROK_HELM) and self.worn(i, BODY, DHAROK_BODY) and self.worn(i, LEGS, DHAROK_LEGS)

    def stripped(self, i):
        k = self.kit[i]
        return k is not None and k[HEAD] in DHAROK_HELM and (k[BODY] == -1 or k[LEGS] == -1)

    def axe(self, i):
        return self.worn(i, WEAPON, DHAROK_AXES)

    def is_dharok(self):
        return any(self.full_set(i) or self.axe(i) for i in range(len(self.ticks)))


def carried(bars):
    out, last = [], 30
    for b in bars:
        if b >= 0: last = b
        out.append(last)
    return out


def pm(n, d):
    return f"{1000.0 * n / d:.1f}" if d else "-"


def table(header, rows):
    lines = ["| " + " | ".join(header) + " |", "|" + "---|" * len(header)]
    lines += ["| " + " | ".join(str(c) for c in row) + " |" for row in rows]
    return "\n".join(lines)


def measure(duels):
    sides = [Side(d, s) for d in duels for s in (0, 1)]
    dh = [s for s in sides if s.is_dharok()]
    out = [f"duels {len(duels)}, fighters {len(sides)}, Dharok fighters {len(dh)}", ""]

    # (a) strips
    eligible = collections.Counter()      # band -> fighter-ticks wearing the full set, no third party
    starts = collections.Counter()        # band -> strip starts
    artefacts = collections.Counter()     # 'one tick' / 'weapon change'
    durations = collections.Counter()
    pieces = collections.Counter()
    weapon_at = collections.Counter()
    follow = collections.Counter()
    lag = collections.Counter()           # ticks from re-equip to the axe in hand
    strippers = set()
    stripped_past_first = reequips = 0
    for s in dh:
        n = len(s.ticks)
        i = 1
        while i < n:
            if s.third[i - 1]:
                i += 1
                continue
            if s.full_set(i - 1):
                eligible[band(s.bar[i - 1])] += 1
            if not (s.full_set(i - 1) and s.stripped(i)):
                i += 1
                continue
            j = i
            while j < n and s.stripped(j): j += 1
            length = j - i
            weapon_change = s.kit[i][WEAPON] != s.kit[i - 1][WEAPON]
            if length < 2 or weapon_change:
                artefacts["one tick" if length < 2 else "weapon change on the start tick"] += 1
                i = j
                continue
            starts[band(s.bar[i - 1])] += 1
            strippers.add(id(s))
            durations[min(length, 11)] += 1
            k = s.kit[i]
            pieces["body and legs" if k[BODY] == -1 and k[LEGS] == -1 else "body" if k[BODY] == -1 else "legs"] += 1
            weapon_at["axe" if s.axe(i - 1) else "other"] += 1
            reequip = j if j < n and s.full_set(j) else None
            stripped_past_first += length - 1
            reequips += reequip is not None
            axe_tick = next((t for t in range(i, min(n, i + FOLLOW)) if s.axe(t) and not s.axe(t - 1)), None)
            if s.axe(i - 1):
                follow["axe already in hand" + (", re-equipped" if reequip is not None else ", not re-equipped")] += 1
            elif axe_tick is None:
                follow["no axe within 20 ticks" + (", re-equipped" if reequip is not None else ", not re-equipped")] += 1
            elif reequip is not None and reequip <= axe_tick:
                follow["re-equip, then axe"] += 1
                lag[min(axe_tick - reequip, 5)] += 1
            else:
                follow["axe while stripped"] += 1
            i = j
    total_starts, total_eligible = sum(starts.values()), sum(eligible.values())
    out.append("(a) strips: body and/or legs off with the helm on, >= 2 ticks, no weapon change on the start tick")
    out.append(f"strip starts {total_starts} by {len(strippers)} of {len(dh)} Dharok fighters, over {total_eligible} full-set fighter-ticks: "
               f"{pm(total_starts, total_eligible)} per mille per tick; artefacts dropped: {dict(artefacts)}")
    out.append(table(["his bar at the start", "full-set ticks", "strips", "per mille"],
                     [(BAND_NAMES[b], eligible[b], starts[b], pm(starts[b], eligible[b])) for b in range(3)]))
    out.append(table(["duration (ticks)", "strips"], [(f"{d}{'+' if d == 11 else ''}", durations[d]) for d in sorted(durations)]))
    out.append(table(["pieces off", "strips"], sorted(pieces.items())))
    out.append(table(["weapon in hand before", "strips"], sorted(weapon_at.items())))
    out.append(table(["what follows", "strips"], sorted(follow.items(), key=lambda kv: -kv[1])))
    out.append(table(["ticks from re-equip to the axe", "strips"], [(f"{d}{'+' if d == 5 else ''}", lag[d]) for d in sorted(lag)]))
    out.append(f"re-equip hazard past the first stripped tick: {reequips} re-equips over {stripped_past_first} stripped ticks, "
               f"{pm(reequips, stripped_past_first)} per mille per tick")
    out.append("")

    # (b) axe wields
    own_t, own_w, opp_t, opp_w = collections.Counter(), collections.Counter(), collections.Counter(), collections.Counter()
    for s in dh:
        for i in range(1, len(s.ticks)):
            if s.third[i - 1] or s.axe(i - 1) or s.kit[i - 1] is None or s.kit[i] is None: continue
            own_t[band(s.bar[i - 1])] += 1
            opp_t[band(s.opp_bar[i - 1])] += 1
            if s.axe(i):
                own_w[band(s.bar[i - 1])] += 1
                opp_w[band(s.opp_bar[i - 1])] += 1
    out.append("(b) greataxe wields per fighter-tick without the axe in hand")
    out.append(table(["band", "his ticks", "wields", "per mille", "opponent's ticks", "wields", "per mille"],
                     [(BAND_NAMES[b], own_t[b], own_w[b], pm(own_w[b], own_t[b]), opp_t[b], opp_w[b], pm(opp_w[b], opp_t[b])) for b in range(3)]))
    out.append("")

    # (c) the orb
    unexplained, explained = collections.Counter(), collections.Counter()
    before_axe = 0
    for s in dh:
        for i in range(len(s.ticks)):
            if s.third[i] or s.nhit[i] != 1 or s.dmg[i] <= 0: continue
            lo = max(0, i - OPP_SWING_WINDOW)
            opp = any(s.opp_swing[lo:i + 1]) or s.opp_veng_proc[i]
            own = any(s.swing[max(0, i - OWN_SWING_WINDOW):i])
            if opp or own:
                explained[s.dmg[i]] += 1
                continue
            unexplained[s.dmg[i]] += 1
            if s.dmg[i] == 10 and any(s.axe(t) and s.swing[t] for t in range(i + 1, min(len(s.ticks), i + 4))):
                before_axe += 1
    neighbours = [unexplained[d] for d in (8, 9, 11, 12)]
    baseline = sum(neighbours) / len(neighbours)
    out.append("(c) single hitsplats on a Dharok fighter with no swing of the opponent in the 4 ticks up to it, no Vengeance proc, no own swing in 2 ticks")
    out.append(table(["damage", "unexplained", "explained"], [(d, unexplained[d], explained[d]) for d in range(1, 21)]))
    visible = unexplained[10] >= 3 * max(baseline, 1)
    out.append(f"exactly 10: {unexplained[10]} against a neighbour baseline of {baseline:.1f} (8, 9, 11, 12): "
               f"{'a spike, the orb is visible' if visible else 'no spike, the orb is not visible in this data: keep the profiles orb_axe_pm'}; "
               f"{before_axe} of the 10s precede an axe swing within 3 ticks")
    axe_swings = orb_axe_swings = 0
    for s in dh:
        for t in range(len(s.ticks)):
            if not s.axe_swing[t]: continue
            axe_swings += 1
            orb_axe_swings += any(s.nhit[u] == 1 and s.dmg[u] == 10 for u in range(max(0, t - 2), t + 1))
    out.append(f"axe swings (2066/2067) with a single 10 in the 2 ticks up to them: {orb_axe_swings} of {axe_swings} "
               f"({pm(orb_axe_swings, axe_swings)} per mille; the profiles' orb_axe_pm is this count per fighter, mined by mine_riskfight_behavior.py)")
    if visible:
        long_axe_users = seen = any_ten = 0
        for s in dh:
            wields = sum(1 for i in range(1, len(s.ticks)) if s.axe(i) and not s.axe(i - 1))
            if wields < LONG_WIELDS: continue
            long_axe_users += 1
            seen += any(unexplained_ten(s, i) for i in range(len(s.ticks)))
            any_ten += any(not s.third[i] and s.nhit[i] == 1 and s.dmg[i] == 10 for i in range(len(s.ticks)))
        lower, upper = 1000 * seen / long_axe_users, 1000 * any_ten / long_axe_users
        out.append(f"orb carriers among the {long_axe_users} Dharok fighters with >= {LONG_WIELDS} axe wields: {seen} showed an unexplained 10 "
                   f"({lower:.0f} per mille, a lower bound: an orb under the opponent's swing hides), {any_ten} took any single 10 "
                   f"({upper:.0f} per mille, an upper bound: real hits of 10 count); pool orb share, the midpoint to 50: "
                   f"{50 * round((lower + upper) / 2 / 50)} per mille")
    out.append("")
    out.append(measure_axe_swings(duels, dh))
    out.append("")
    out.append(measure_vw_specs(duels))
    out.append("")
    out.append(measure_orb_ticks(duels))
    out.append("")
    out.append(measure_setups(duels))
    return "\n".join(out)


def measure_vw_specs(duels):
    ticks, specs = collections.Counter(), collections.Counter()
    for d in duels:
        pair = [Side(d, 0), Side(d, 1)]
        if not all(s.is_dharok() for s in pair): continue
        for side, s in enumerate(pair):
            rows = [r[2 + side] for r in d["rows"]]
            if not any(r[WEAPON_FIELD] == str(VOIDWAKER) or r[SPEC] == "VW" for r in rows): continue
            for i in range(1, len(rows)):
                if s.third[i]: continue
                ticks[s.opp_bar[i - 1]] += 1
                specs[s.opp_bar[i - 1]] += rows[i][SPEC] == "VW"
    total, cum, gate = sum(specs.values()), 0, None
    out = [f"(e) Voidwaker specials by the opponent's bar the tick before ({total} specials of fighters holding a Voidwaker)",
           "| bar | ticks | specials | per 1,000 ticks | cumulative share |", "|---|---|---|---|---|"]
    for bar in range(31):
        cum += specs[bar]
        rate = specs[bar] / max(1, ticks[bar])
        if gate is None and cum >= VW_GATE_SHARE * total: gate = bar
        out.append(f"| {bar} | {ticks[bar]} | {specs[bar]} | {1000 * rate:.1f} | {cum / max(1, total):.3f} |")
    out.append(f"RF_VW_SPEC_BAR_MAX (the lowest bar with {VW_GATE_SHARE:.0%} of the specials at or below it): {gate}")
    return "\n".join(out)


def measure_setups(duels):
    """(g), see the module docstring."""
    setups = collections.Counter()
    for d in duels:
        sides = (Side(d, 0), Side(d, 1))
        if not (sides[0].is_dharok() and sides[1].is_dharok()): continue
        for s in sides:
            held = collections.Counter(k[WEAPON] for k in s.kit if k is not None)
            mains = {w: c for w, c in held.items() if w != -1 and w not in DHAROK_AXES and w not in MAULS}
            main = max(mains, key=mains.get) if mains else None
            name = "tentacle" if main in TENTACLES else "Voidwaker" if main in VOIDWAKERS else "other" if main else "axe only"
            specs = [n for n, ids in (("Voidwaker", VOIDWAKERS), ("AGS", GODSWORDS)) if n != name and any(w in held for w in ids)]
            setups[(name, " + ".join(specs) or "-", any(w in held for w in MAULS))] += 1
    total = sum(setups.values())
    out = [f"(g) Dharok setups of the {total} fighters of the Dharok v Dharok duels, by the main weapon (most ticks in hand, not the axe or a maul), the spec "
           "weapons seen wielded besides it, and a granite maul seen wielded (lower bounds on what the bag carries)",
           "| main | spec weapons | maul | fighters | share |", "|---|---|---|---|---|"]
    for (main, specs, maul), c in setups.most_common():
        out.append(f"| {main} | {specs} | {'yes' if maul else 'no'} | {c} | {c / total:.3f} |")
    return "\n".join(out)


def measure_orb_ticks(duels):
    """(f), see the module docstring."""
    seen, orbs = collections.Counter(), collections.Counter()     # (axe in hand, hp band) on ticks the detector can see
    since_seen, since_orbs, since_all = collections.Counter(), collections.Counter(), collections.Counter()
    for d in duels:
        sides = (Side(d, 0), Side(d, 1))
        if not (sides[0].is_dharok() and sides[1].is_dharok()): continue
        rows = d["rows"]
        n = len(rows)
        first = next((i for i in range(n) if sides[0].dmg[i] + sides[1].dmg[i] > 0), None)
        if first is None: continue
        dead = next((i for i in range(n) if rows[i][2][DEAD] == "1" or rows[i][3][DEAD] == "1"), n - 1)
        for v in (0, 1):
            s, k = sides[v], 1 - v
            last, speed = None, None
            for i in range(max(1, first), min(dead + 1, n - 1)):
                if s.third[i]: continue
                if s.opp_swing[i - 1]:
                    last, speed = i - 1, WEAPON_SPEED.get(int(rows[i - 1][2 + k][WEAPON_FIELD]), 4)
                since = None if last is None else i - last
                timing = "before his first swing" if since is None else "ready" if since == speed - 1 else min(since, 6)
                since_all[timing] += 1
                if any(s.opp_swing[i - 1:i + 2]) or s.opp_veng_proc[i - 1] or s.opp_veng_proc[i]:
                    continue
                key = (s.axe(i - 1), min(4, int(s.bar[i - 1] * 99 / 30) // 20))
                orb = s.nhit[i] == 1 and s.dmg[i] == 10
                seen[key] += 1
                orbs[key] += orb
                since_seen[timing] += 1
                since_orbs[timing] += orb
    out = ["(f) orb clicks (a lone 10) per Dharok fighter-tick of the Dharok v Dharok duels, counted only on ticks the orb can be told from his hit: no swing of "
           "the opponent on ticks i-1..i+1 and no Vengeance proc of his on i-1, i. Those ticks leave both the count and the denominator (the tick after his "
           f"swing is always one of them). Script per mille over the RF_KIT_ORB_PM ({KIT_ORB_PM}) share of fighters carrying an orb (RF_DHAROK_ORB_PM)"]
    rows = []
    for axe in (False, True):
        for b in range(5):
            rows.append((("axe" if axe else "no axe") + f" hp {20 * b}-{20 * b + 19 if b < 4 else 99}", seen[(axe, b)], orbs[(axe, b)],
                         pm(orbs[(axe, b)], seen[(axe, b)]), round(1000 * 1000 * orbs[(axe, b)] / seen[(axe, b)] / KIT_ORB_PM) if seen[(axe, b)] else "-"))
    out.append(table(["his state the tick before", "visible ticks", "orbs", "per mille", "script per mille"], rows))
    out.append("by ticks since the opponent's swing (ready: the tick before his weapon frees the next one): ticks, visible ticks, orbs, per mille of visible")
    order = ["before his first swing", 1, 2, 3, 4, 5, 6, "ready"]
    out.append(table(["since his swing", "ticks", "visible", "orbs", "per mille"],
                     [("6+" if t == 6 else t, since_all[t], since_seen[t], since_orbs[t], pm(since_orbs[t], since_seen[t])) for t in order]))
    return "\n".join(out)


def opp_band(bar):
    return (bar >= 10) + (bar >= 20)


def measure_axe_swings(duels, dh):
    """(d), see the module docstring."""
    out = []
    swings, axes = collections.Counter(), collections.Counter()
    for s in dh:
        for i in range(1, len(s.ticks)):
            if s.third[i] or not s.swing[i] or s.special[i]: continue
            key = (band(s.bar[i - 1]), s.veng_up[i], opp_band(s.opp_bar[i - 1]))
            swings[key] += 1
            axes[key] += s.axe_swing[i]
    out.append(f"(d) greataxe share of a Dharok fighter's swings without a special ({sum(swings.values())} swings), per mille (swings), by his own bar band, "
               f"his Vengeance up (cast seen, not yet proc'd) and the opponent's bar the tick before (<10, 10-19, 20+); a cell under {AXE_POOL_MIN} swings "
               "pools with its Vengeance pair (the old key of riskfight_policy.h's RF_AXE_SWING_PM, now fitted by fit_dharok_axe.py)")
    rows = []
    for b in range(3):
        for up in (0, 1):
            cells = []
            for o in range(3):
                n, a = swings[(b, up, o)], axes[(b, up, o)]
                if n < AXE_POOL_MIN or swings[(b, 1 - up, o)] < AXE_POOL_MIN:
                    n, a = swings[(b, 0, o)] + swings[(b, 1, o)], axes[(b, 0, o)] + axes[(b, 1, o)]
                cells.append(f"{pm(a, n)} ({swings[(b, up, o)]})")
            rows.append([BAND_NAMES[b], up] + cells)
    out.append(table(["his bar", "veng up", "opp bar <10", "opp bar 10-19", "opp bar 20+"], rows))
    for name, axis in (("his bar", 0), ("veng up", 1), ("opp bar", 2)):
        marg = [(sum(axes[k] for k in swings if k[axis] == v), sum(swings[k] for k in swings if k[axis] == v)) for v in range(3 if axis != 1 else 2)]
        out.append(f"by {name} alone: " + ", ".join(f"{pm(a, n)} ({n})" for a, n in marg))
    # The knockouts of the Dharok v Dharok duels: the killer's state at his axe swing against all axe swings and all Dharok ticks.
    ko, stack, axe_all, base = collections.Counter(), 0, collections.Counter(), collections.Counter()
    kos = 0
    for d in duels:
        sides = [Side(d, 0), Side(d, 1)]
        if not (sides[0].is_dharok() and sides[1].is_dharok()): continue
        rows_ = d["rows"]
        for s in sides:
            for i in range(1, len(s.ticks)):
                if s.third[i]: continue
                f = setup(s, i)
                base["ticks"] += 1
                for k in ("veng", "orb", "stripped", "opp swung"): base[k] += f[k]
                if s.axe_swing[i]:
                    axe_all["n"] += 1
                    for k in f: axe_all[k] += f[k]
        dead = [(i, v) for i in range(len(rows_)) for v in (0, 1) if rows_[i][2 + v][DEAD] == "1"]
        if not dead: continue
        i, v = dead[0]
        killer, victim = sides[1 - v], sides[v]
        t = next((j for j in range(i, max(-1, i - 3), -1) if victim.dmg[j] > 0), None)
        if t is None: continue
        kos += 1
        reflect = killer.veng_proc[t] or (t > 0 and killer.veng_proc[t - 1])
        if reflect:
            stack += any(killer.axe_swing[j] for j in (t - 2, t - 1, t) if j >= 0)
            continue
        sw = next((j for j in (t - 1, t) if j >= 0 and killer.swing[j] and (j == t or victim.nhit[t - 1] == 0)), None)
        if sw is None: sw = next((j for j in (t, t - 1) if j >= 0 and killer.swing[j]), None)
        if sw is None or not killer.axe_swing[sw]: continue
        ko["n"] += 1
        for k, val in setup(killer, sw).items(): ko[k] += val
    def share(c, k, n): return f"{c[k] / c[n]:.2f}" if c[n] else "-"
    out.append(f"axe knockouts (no reflect) of {kos} Dharok v Dharok knockouts: {ko['n']}; an axe swing in the 3 ticks to a knockout carrying his reflect: {stack}")
    out.append(table(["at his axe swing", "axe knockouts", "all axe swings", "all Dharok ticks"],
                     [("Vengeance up", share(ko, "veng", "n"), share(axe_all, "veng", "n"), share(base, "veng", "ticks")),
                      ("orb drop in swing-5..swing", share(ko, "orb5", "n"), share(axe_all, "orb5", "n"), f"{5 * base['orb'] / base['ticks']:.2f} (5 x per tick)"),
                      ("stripped in swing-5..swing", share(ko, "strip5", "n"), share(axe_all, "strip5", "n"), share(base, "stripped", "ticks")),
                      ("opponent swung the tick before", share(ko, "opp swung", "n"), share(axe_all, "opp swung", "n"), share(base, "opp swung", "ticks")),
                      ("his hitpoints, mean", f"{ko['hp'] / max(1, ko['n']):.0f}", f"{axe_all['hp'] / max(1, axe_all['n']):.0f}", "-"),
                      ("opponent's bar the tick before, mean", f"{ko['opp bar'] / max(1, ko['n']):.1f}", f"{axe_all['opp bar'] / max(1, axe_all['n']):.1f}", "-")]))
    return "\n".join(out)


def setup(s, i):
    window = range(max(0, i - 5), i + 1)
    return {"veng": s.veng_up[i - 1], "orb": unexplained_ten(s, i), "orb5": any(unexplained_ten(s, j) for j in window), "stripped": s.stripped(i),
            "strip5": any(s.stripped(j) for j in window), "opp swung": s.opp_swing[i - 1], "hp": s.bar[i] * 99 / 30, "opp bar": s.opp_bar[i - 1]}


def unexplained_ten(s, i):
    if s.third[i] or s.nhit[i] != 1 or s.dmg[i] != 10: return False
    return not (any(s.opp_swing[max(0, i - OPP_SWING_WINDOW):i + 1]) or s.opp_veng_proc[i] or any(s.swing[max(0, i - OWN_SWING_WINDOW):i]))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--duels", default=DEFAULT_DUELS)
    args = ap.parse_args()
    print(measure(parse(args.duels)))


if __name__ == "__main__":
    main()
