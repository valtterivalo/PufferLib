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
(b) Greataxe wields: ticks his weapon becomes the axe, per fighter-tick without the axe in hand, by his own bar band and by the opponent's bar band (the profile's
    axe_pm bands).
(c) The orb ("Feel", 10 damage to himself): a single hitsplat of exactly 10 on a tick with no swing of the opponent in the 4 ticks up to it, no Vengeance proc
    from the opponent and no swing of his own in the 2 ticks before (recoil). The same count for every other damage value is the baseline: a spike at 10 is the orb.
    Also reported: axe swings (2066/2067) with a single 10 in the 2 ticks up to them (what mine_riskfight_behavior.py fits orb_axe_pm on), and the share of
    Dharok fighters with >= LONG_WIELDS axe wields carrying an orb, bounded below by an unexplained 10 and above by any single 10: its midpoint is the pool's
    RF_KIT_ORB_PM (riskfight_kits.h). The strip numbers are riskfight_policy.h's RF_STRIP_* constants.
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
BAR, DMG, NHIT, VENG_PROC, ANIMS = 0, 5, 6, 8, 12
FOLLOW = 20       # ticks after a strip's start in which its outcome is read
OPP_SWING_WINDOW = 4
OWN_SWING_WINDOW = 2
LONG_WIELDS = 8   # axe wields that make a fighter's orb share readable (the share of fighters showing the orb levels off from about here)
DH_SWINGS = {2066, 2067}


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
    return "\n".join(out)


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
