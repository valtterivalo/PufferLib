#!/usr/bin/env python3
"""How the user's real fights start, and what the death price was, from his recordings: the riskfight sim draws both from this (riskfight_starts_data.h).

Usage: python3 scripts/measure_fight_starts.py [--work DIR] [--light DIR] [--tapes DIR] [--recordings GLOB] [--no-write]
(python3 stdlib + zstd; at most 3 processes; the recordings, the client logs and the light repository are read only)

Fights are the tapes' windows (tools/rollout_export.py of the light repo over every recording; --tapes reuses a directory of tapes already exported).
A fight's START is the end of the first tick on which either side swings: the first animation of the Gear Swapper's attack table
(AttackAnimations.java, read through tools/attack_anim_audit.py, plus 5866 the exporter's SWING_SPEED lists) of either side. The tape's T row of that tick is the start
state: our exact hitpoints, boosted levels, prayer, special energy, Vengeance, supplies and tile, his health bar, his special energy and Vengeance as the tape's
tracker estimates them (from the animations and texts it saw). The hits that land one tick later (a swing of the first tick lands on the next one when the
recipient was processed first; the Vengeance and recoil echoes of the first hits) are part of the start: the sim carries them as hits in flight.

Supplies are counted as what the user has used up: the tape's inventory at the start against the most of each item that recording ever held at the start of one of
its fights (his kit changed over the weeks: 12 to 15 anglerfish), as deficits. The sim applies deficits to the kit it drew for that fight.

A REMATCH is a fight against the same opponent as the previous fight of the same recording; rematches differ from first meetings (report), so the sim draws them
from their own rows.

Price: the client log of the recording's client run (run-<stamp>.log of the recording's name) has `risk: us X gp ... him Y gp` whenever the stakes inputs change
for the player we target. The price of a fight is the last such line at or before its first swing, no earlier than 30 s before the fight window starts (else the
first line inside the window); a fight with none (before the plugin logged stakes, or no stakes) has no price. The live server prices a fight us / him clamped to
[0.1, 2.5] (gs_rl_server.c): the table holds the clamped value of the first fight of each opponent (a rematch keeps its price, the sim draws one per new opponent).

Writes ocean/osrs/encounters/riskfight/riskfight_starts_data.h (what the sim reads) and fight_starts.tsv (every measured field per fight); prints the report.
"""

import argparse
import collections
import datetime
import glob
import json
import math
import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
HOME = os.path.expanduser("~")
DEFAULT_LIGHT = os.path.join(HOME, "Projects/storm/light")
REC_DIR = os.path.join(HOME, ".light2/gameplay-recordings/local-debug")
REC_GLOB = os.path.join(REC_DIR, "session_202610*.jsonl.zst")   # the Gear Swapper PvP recordings (July to September hold no player opponent)
OUT_DIR = os.path.join(ROOT, "ocean/osrs/encounters/riskfight")
LOG_DIR = os.path.join(DEFAULT_LIGHT, "runtime/client/logs")

SWING_EXTRA = {5866}   # tools/rollout_export.py SWING_SPEED lists it; AttackAnimations.java does not
SPEC_ANIMS = {11275, 7644, 7514, 7515, 1062, 1060, 3299, 3300, 1378, 10173, 1667}   # one special each (1667 the maul's instant one)
BREW_DOSES = {6685: 4, 6687: 3, 6689: 2, 6691: 1}
RESTORE_DOSES = {3024: 4, 3026: 3, 3028: 2, 3030: 1}
COMBAT_DOSES = {12695: 4, 12697: 3, 12699: 2, 12701: 1}
ANGLERFISH, KARAMBWAN, TAB = 13441, 3144, 8013
TZ_HOURS = {"EEST": 3, "EET": 2, "UTC": 0}
PRICE_MIN, PRICE_MAX = 0.1, 2.5   # gs_rl_server.c: the live death price
LEAD = datetime.timedelta(seconds=30)

# Column order of the T row after the "T" token (tools/rollout_export.py): tick, hp, atk, str, def, mag, prayer, x, y, ox, oy, our spec, spec phase, our veng, veng cooldown,
# sacks, recoil used, bar ratio, bar scale, last bar hit, meals since, his spec, his spec phase, his veng, his veng cooldown, his last swing (tick, speed), our last swing
# (tick, speed), recoil evidence, current bar ratio, current bar scale, pid, overhead, piety, 13 worn, 28 inventory
T = dict(tick=0, hp=1, atk=2, str=3, df=4, mag=5, pray=6, x=7, y=8, ox=9, oy=10, spec=11, phase=12, veng=13, vcd=14, sacks=15, bar=17, scale=18,
         hspec=21, hphase=22, hveng=23, cur=30, cur_scale=31, worn=35, inv=48)


class Tape:
    def __init__(self, path):
        self.path = path
        self.name = os.path.basename(path)
        self.anims = collections.defaultdict(list)    # tick -> [(side, animation)]
        self.hits = collections.defaultdict(list)     # tick -> [(side, amount, class)]
        self.rows = {}                                # tick -> ints of the T row
        self.comp = {}                                # tick -> his composition items (E row, observed only)
        with open(path) as fh:
            for line in fh:
                p = line.split()
                if p[0] == "FIGHT":
                    self.session, self.opponent, self.lo, self.hi = p[2], p[3], int(p[4]), int(p[5])
                elif p[0] == "A":
                    self.anims[int(p[1])].append((p[2], int(p[3])))
                elif p[0] == "H":
                    self.hits[int(p[1])].append((p[2], int(p[3]), p[5]))
                elif p[0] == "T":
                    self.rows[int(p[1])] = [int(x) if x.lstrip("-").isdigit() else x for x in p[1:]]
                elif p[0] == "E" and p[2] == "1":
                    self.comp[int(p[1])] = [int(x) for x in p[4:]]


def attack_animations(light):
    sys.dont_write_bytecode = True   # the light repository is read only
    sys.path.insert(0, os.path.join(light, "tools"))
    import attack_anim_audit
    return {a for a, listed in attack_anim_audit.table().items() if listed} | SWING_EXTRA


def export_tapes(light, recs, work):
    out = os.path.join(work, "tapes")
    subprocess.run([sys.executable, os.path.join(light, "tools/rollout_export.py"), out, *recs, "--item-metadata", os.path.join(ROOT, "ocean/osrs/osrs_item_obs_generated.h"),
                    "--jobs", "3", "--manifest", os.path.join(work, "manifest.tsv")], check=True, stdout=subprocess.DEVNULL, env={**os.environ, "PYTHONDONTWRITEBYTECODE": "1"})
    return out


def read_manifest(path):
    """Tape file name -> recording file name, and the recordings the exporter could not read (reported)."""
    recording, excluded = {}, []
    with open(path) as fh:
        for line in fh:
            p = line.rstrip("\n").split("\t")
            if p[0] == "EXCLUDED":
                excluded.append((p[1], p[2]))
            else:
                assert len(p) == 7, "manifest row: " + line
                recording[os.path.basename(p[0])] = p[1]
    return recording, excluded


def cheb(a, b, c, d):
    return max(abs(a - c), abs(b - d))


def measure(tape, attacks):
    """The start record of one tape, or None for a window in which neither side swung (the report lists those)."""
    swings = [(k, s, a) for k in sorted(tape.anims) for s, a in tape.anims[k] if a in attacks]
    if not swings:
        return None
    f = swings[0][0]
    first_tick = {s: next((k for k, ss, _ in swings if ss == s), None) for s in "UO"}
    opener = {s: int(first_tick[s] == f) for s in "UO"}
    other = "O" if opener["U"] else "U"
    gap = 0 if opener["U"] and opener["O"] else (-1 if first_tick[other] is None else first_tick[other] - f)
    row = tape.rows[f]
    swung_f = {s for k, s, _ in swings if k == f}
    spec = {s: int(any(k == f and ss == s and a in SPEC_ANIMS for k, ss, a in swings)) for s in "UO"}
    ratio, scale = (row[T["cur"]], row[T["cur_scale"]]) if row[T["cur"]] >= 0 else (row[T["bar"]], row[T["scale"]])
    assert scale in (-1, 30), (tape.name, scale)
    inv = row[T["inv"]:T["inv"] + 28]
    assert len(inv) == 28 and len(row) == T["inv"] + 28, tape.name
    landed = {s: -1 for s in "UO"}                    # hits that landed on this side at the first tick (-1: none)
    for side, amount, _ in tape.hits.get(f, []):
        landed[side] = max(landed[side], 0) + amount
    flight = {s: dict(direct=-1, veng=-1, recoil=-1) for s in "UO"}   # hits landing the next tick on this side, by cause
    for side, amount, klass in tape.hits.get(f + 1, []):
        attacker = "O" if side == "U" else "U"
        if klass in ("A", "B"):
            if attacker in swung_f:
                flight[side]["direct"] = max(flight[side]["direct"], 0) + amount
        elif klass == "V":
            flight[side]["veng"] = max(flight[side]["veng"], 0) + amount
        elif klass == "R":
            flight[side]["recoil"] = max(flight[side]["recoil"], 0) + amount
        else:
            assert klass == "P", (tape.name, f + 1, klass)   # poison is not a swing's consequence
    weapon = {"O": tape.comp[f][3] if f in tape.comp else None, "U": row[T["worn"] + 3]}   # his composition is exposed from 2026-10-10 only
    return dict(
        tape=tape.name, opponent=tape.opponent, lo=tape.lo, hi=tape.hi, f=f, wait=f - tape.lo, short=int(tape.hi - tape.lo < 15),
        opener_us=opener["U"], opener_him=opener["O"], gap=gap, spec_us=spec["U"], spec_him=spec["O"],
        o_anim=sorted({a for k, s, a in swings if k == f and s == "O"}), u_anim=sorted({a for k, s, a in swings if k == f and s == "U"}), o_weapon=weapon["O"], u_weapon=weapon["U"],
        hp=row[T["hp"]], atk=row[T["atk"]], str=row[T["str"]], df=row[T["df"]], mag=row[T["mag"]], pray=row[T["pray"]],
        e_spec=row[T["spec"]], e_phase=row[T["phase"]], veng=row[T["veng"]], vcd=row[T["vcd"]], sacks=row[T["sacks"]],
        angler=inv.count(ANGLERFISH), karam=inv.count(KARAMBWAN), brew=sum(BREW_DOSES.get(i, 0) for i in inv), restore=sum(RESTORE_DOSES.get(i, 0) for i in inv),
        combat=sum(COMBAT_DOSES.get(i, 0) for i in inv), tab=int(TAB in inv),
        bar=ratio, h_spec=row[T["hspec"]], h_phase=row[T["hphase"]], h_veng=row[T["hveng"]],
        dist=cheb(row[T["x"]], row[T["y"]], row[T["ox"]], row[T["oy"]]),
        landed_us=landed["U"], landed_him=landed["O"],
        fl_us_direct=flight["U"]["direct"], fl_us_veng=flight["U"]["veng"], fl_us_recoil=flight["U"]["recoil"],
        fl_him_direct=flight["O"]["direct"], fl_him_veng=flight["O"]["veng"], fl_him_recoil=flight["O"]["recoil"], opening=opening_hits(tape))


def opening_hits(tape):
    """The opening exchange of hits: the tick each side's first swing hit lands on the other (class A damage or B zero: `landed_*`, the swing's own hit), the first damaging
    one, and what our Vengeance did to his first damaging hit on us: an echo (class V) on him within a tick of it, and the bar his echo left."""
    first_land = {"U": None, "O": None}     # side that RECEIVED the hit
    first_dmg = {"U": None, "O": None}
    for tick in sorted(tape.hits):
        for side, amount, klass in tape.hits[tick]:
            if klass in ("A", "B") and first_land[side] is None:
                first_land[side] = tick
            if klass == "A" and amount > 0 and first_dmg[side] is None:
                first_dmg[side] = tick
    echo = None                             # (tick, damage, bar after) of our first Vengeance echo on him
    for tick in sorted(tape.hits):
        for side, amount, klass in tape.hits[tick]:
            if side == "O" and klass == "V" and echo is None:
                row = tape.rows.get(tick)
                bar = -1 if row is None else (row[T["cur"]] if row[T["cur"]] >= 0 else row[T["bar"]])
                echo = (tick, amount, bar)
    proc = first_dmg["U"] is not None and echo is not None and echo[0] in (first_dmg["U"], first_dmg["U"] + 1)
    return dict(land_us=first_land["U"], land_him=first_land["O"], dmg_us=first_dmg["U"], dmg_him=first_dmg["O"], echo=echo, proc_on_his_opener=int(proc))


def session_maxima(tapes_of_recording):
    """The most of each supply the recording ever held, over every tick of every tape of it."""
    most = dict(angler=0, karam=0, brew=0, restore=0, combat=0, tab=0)
    for tape in tapes_of_recording:
        for row in tape.rows.values():
            inv = row[T["inv"]:T["inv"] + 28]
            most["angler"] = max(most["angler"], inv.count(ANGLERFISH))
            most["karam"] = max(most["karam"], inv.count(KARAMBWAN))
            most["brew"] = max(most["brew"], sum(BREW_DOSES.get(i, 0) for i in inv))
            most["restore"] = max(most["restore"], sum(RESTORE_DOSES.get(i, 0) for i in inv))
            most["combat"] = max(most["combat"], sum(COMBAT_DOSES.get(i, 0) for i in inv))
            most["tab"] = max(most["tab"], int(TAB in inv))
    return most


def tick_clock(rec):
    """(monotonic ns, UTC start) of the recording and tick -> monotonic ns."""
    ticks, start = {}, None
    out = subprocess.run(["zstd", "-dc", os.path.join(REC_DIR, rec)], capture_output=True, text=True, check=True).stdout
    for line in out.splitlines():
        if '"eventKind":"SESSION_STARTED"' in line:
            r = json.loads(line)
            start = (r["monotonicNanos"], datetime.datetime.fromisoformat(r["payload"]["startedAt"].replace("Z", "+00:00")).replace(tzinfo=None))
        elif '"eventKind":"GAME_TICK_OBSERVED"' in line:
            i = line.index('"lastObservedGameTick":') + 23
            k = line.index('"monotonicNanos":') + 17
            l = k
            while line[l].isdigit():
                l += 1
            ticks[int(line[i:line.index(",", i)])] = int(line[k:l])
    assert start is not None, rec
    return start, ticks


LOG_RE = re.compile(r"^(\d{4}-\d\d-\d\d \d\d:\d\d:\d\d) (\S+) \[[^\]]*\] (\w+)\s+(\S+) - (.*)$")
RISK_RE = re.compile(r"risk: us (\d+) gp.*?him (\d+) gp")


def find_log(rec):
    stamp = re.match(r"session_[^_]+_(\w+\.\d+)_", rec).group(1)
    found = glob.glob(os.path.join(LOG_DIR, "run-%s*.log" % stamp))
    assert len(found) == 1, (stamp, found)
    return found[0]


def risk_lines(rec):
    """The `risk: us X gp ... him Y gp` lines of the recording's client run as (UTC time, us, him); lines naming no stakes ("nothing is lost", unknown skull) are counted apart."""
    gp, equal = [], 0
    with open(find_log(rec), errors="replace") as fh:
        stakes = [line for line in fh if "GearSwapperPlugin - risk:" in line]
    for line in stakes:
        m = LOG_RE.match(line)
        assert m, line
        when = datetime.datetime.strptime(m.group(1), "%Y-%m-%d %H:%M:%S") - datetime.timedelta(hours=TZ_HOURS[m.group(2)])
        g = RISK_RE.search(m.group(5))
        if g:
            gp.append((when, int(g.group(1)), int(g.group(2))))
        else:
            equal += 1
    return gp, equal


def price_of(rec, clock, lines, r):
    """(us, him, where) of the line that stands for the fight, or None when the log has no stakes line for it."""
    (start_ns, start_utc), ticks = clock[rec]
    at = lambda tick: start_utc + datetime.timedelta(seconds=(ticks[tick] - start_ns) / 1e9)
    lo, f, hi = at(r["lo"]), at(r["f"]), at(r["hi"])
    window = [x for x in lines if lo - LEAD <= x[0] <= hi]
    before = [x for x in window if x[0] <= f]
    pick = before[-1] if before else (window[0] if window else None)
    return None if pick is None else (pick[1], pick[2], "before" if before else "during")


def pct(xs, q):
    xs = sorted(xs)
    return xs[min(len(xs) - 1, int(q * len(xs)))]


def summary(xs):
    return "n %d  min %s  p10 %s  p25 %s  p50 %s  p75 %s  p90 %s  max %s  mean %.2f" % (
        len(xs), min(xs), pct(xs, .1), pct(xs, .25), pct(xs, .5), pct(xs, .75), pct(xs, .9), max(xs), sum(xs) / len(xs))


def counts(xs):
    c = collections.Counter(xs)
    return "  ".join("%s:%d" % (k, c[k]) for k in sorted(c, key=str))


def counts_map(c):
    return "  ".join("%s %d" % (k, c[k]) for k in sorted(c))


def fisher(a, b, c, d):
    """Two-sided Fisher exact p of [[a, b], [c, d]]."""
    n, r1, c1 = a + b + c + d, a + b, a + c
    p = lambda x: math.comb(r1, x) * math.comb(n - r1, c1 - x) / math.comb(n, c1)
    p0 = p(a)
    return sum(p(x) for x in range(max(0, c1 - (n - r1)), min(r1, c1) + 1) if p(x) <= p0 * (1 + 1e-9))


def bar_range(ratio):
    """osrs_health_bar_range(ratio, 30, 99, 121): inclusive hitpoints bounds of a bar, (99, 121) for none or full."""
    if ratio < 0:
        return (99, 121)
    if ratio == 0:
        return (0, 0)
    lower = 1 if ratio == 1 else (99 * (ratio - 1) + 28) // 29
    upper = 121 if ratio == 30 else (99 * ratio - 1) // 29
    return (lower, upper)


def report(rows, priced, unpriced, excluded, swing_free, equal_lines):
    out = []
    p = out.append
    n = len(rows)
    p("FIGHT STARTS: %d fights of %d recordings (%d short: window < 15 ticks), %d rematches, %d first meetings; %d windows without a swing in the tape, %d recordings the exporter could not read"
      % (n, len({r["tape"].split("._")[0] for r in rows}), sum(r["short"] for r in rows), sum(r["rematch"] for r in rows), sum(1 - r["rematch"] for r in rows), len(swing_free), len(excluded)))
    for name in swing_free:
        p("  no swing in the tape: " + name)
    for rec, why in excluded:
        p("  excluded recording %s: %s" % (rec[:40], why[:80]))
    p("")
    p("who swung first (the first swing tick): %s" % counts(["us" if r["opener_us"] and not r["opener_him"] else "him" if r["opener_him"] and not r["opener_us"] else "both" for r in rows]))
    gaps = [r["gap"] for r in rows if r["gap"] >= 0]
    p("gap to the other side's first swing (ticks, 0 = the same tick): n %d, %s ; the other never swung in the window: %d" % (len(gaps), counts(gaps), sum(r["gap"] < 0 for r in rows)))
    p("ticks from the window start (we target him) to the first swing: %s" % summary([r["wait"] for r in rows]))
    p("  waits: %s" % counts([min(r["wait"], 20) for r in rows]))
    p("our hitpoints at the start (after the first tick): %s" % summary([r["hp"] for r in rows]))
    p("  exact: %s" % counts([r["hp"] for r in rows]))
    before = [r["hp"] + max(r["landed_us"], 0) for r in rows]
    p("  before the first tick (hitpoints + what landed on it that tick): %s; at 121: %d, at 120: %d, below 120: %d" % (summary(before), before.count(121), before.count(120), sum(b < 120 for b in before)))
    p("  at 121: %d  at 120: %d  below 120: %d  below 99: %d" % tuple(sum(f(r["hp"]) for r in rows) for f in (lambda h: h == 121, lambda h: h == 120, lambda h: h < 120, lambda h: h < 99)))
    bars = [r["bar"] for r in rows]
    p("his health bar (of 30; -1 = none shown, he had not been hit): %s" % counts(bars))
    p("  his hitpoints range (osrs_health_bar_range): full or none %d, partial %d (bars 1..29)" % (sum(b in (-1, 30) for b in bars), sum(0 <= b < 30 for b in bars)))
    p("our special energy: %s   his (estimated, after his own opening spec): %s" % (counts([r["e_spec"] for r in rows]), counts([r["h_spec"] for r in rows])))
    p("opening swing was a special: ours %d, his %d" % (sum(r["spec_us"] for r in rows), sum(r["spec_him"] for r in rows)))
    p("Vengeance up after the first tick: ours %d of %d (hits of that tick consumed some), his estimated %d of %d" % (sum(r["veng"] for r in rows), n, sum(r["h_veng"] for r in rows), n))
    p("distance (Chebyshev tiles): %s" % counts([min(r["dist"], 9) for r in rows]))
    p("our supplies at the start, as deficits against the most that recording held:")
    for k in ("angler", "karam", "brew", "restore", "combat", "tab"):
        p("  %-8s deficit %s   held %s" % (k, counts([r["d_" + k] for r in rows]), counts([r[k] for r in rows])))
    p("  vengeance sacks left: %s" % summary([r["sacks"] for r in rows]))
    for k, name in (("atk", "attack"), ("str", "strength"), ("df", "defence"), ("mag", "magic"), ("pray", "prayer points")):
        p("our %s: %s" % (name, summary([r[k] for r in rows])))
    p("  attack/strength/defence below 118: %d / %d / %d" % tuple(sum(r[k] < 118 for r in rows) for k in ("atk", "str", "df")))
    p("his weapon on the first swing (composition slot, observed from 2026-10-10): %s" % counts([r["o_weapon"] for r in rows if r["o_weapon"] is not None]))
    p("  unobserved (composition not exposed that day): %d" % sum(r["o_weapon"] is None for r in rows))
    p("his opening animation: %s" % counts([tuple(r["o_anim"]) for r in rows if r["o_anim"]]))
    p("our opening animation: %s" % counts([tuple(r["u_anim"]) for r in rows if r["u_anim"]]))
    p("hits landing the next tick (in flight at the start), on us / on him:  direct %d / %d, Vengeance echo %d / %d, recoil %d / %d" % (
        sum(r["fl_us_direct"] >= 0 for r in rows), sum(r["fl_him_direct"] >= 0 for r in rows), sum(r["fl_us_veng"] >= 0 for r in rows), sum(r["fl_him_veng"] >= 0 for r in rows),
        sum(r["fl_us_recoil"] >= 0 for r in rows), sum(r["fl_him_recoil"] >= 0 for r in rows)))
    p("hits that landed on the first tick, on us / on him: %d / %d" % (sum(r["landed_us"] >= 0 for r in rows), sum(r["landed_him"] >= 0 for r in rows)))
    p("")
    p("OPENING HITS (which side's swing lands first, and our Vengeance against his opener)")
    def order(key_us, key_him):
        c = collections.Counter()
        for r in rows:
            u, h = r["opening"][key_us], r["opening"][key_him]   # tick his hit lands on us, tick ours lands on him
            c["neither lands" if u is None and h is None else "only his lands" if h is None else "only ours lands" if u is None else
              "his lands first" if u < h else "ours lands first" if h < u else "same tick"] += 1
        return counts_map(c)
    p("  first hit landing (a swing's hit, damage or zero): %s" % order("land_us", "land_him"))
    p("  first damaging hit:                               %s" % order("dmg_us", "dmg_him"))
    his_first = [r for r in rows if r["opening"]["dmg_us"] is not None and (r["opening"]["dmg_him"] is None or r["opening"]["dmg_us"] < r["opening"]["dmg_him"])]
    hit_us = [r for r in rows if r["opening"]["dmg_us"] is not None]
    p("  our Vengeance echo on him within a tick of his first damaging hit on us:      %d of %d fights where he hit us (%.2f); %d of %d where his damaging hit was the first of the fight (%.2f)" % (
        sum(r["opening"]["proc_on_his_opener"] for r in hit_us), len(hit_us), sum(r["opening"]["proc_on_his_opener"] for r in hit_us) / len(hit_us),
        sum(r["opening"]["proc_on_his_opener"] for r in his_first), len(his_first), sum(r["opening"]["proc_on_his_opener"] for r in his_first) / len(his_first)))
    echoes = [r["opening"]["echo"] for r in rows if r["opening"]["echo"] is not None]
    p("  our first echo on him (any time): %d fights; his bar right after it: full (30) %d, below full %d, no bar shown %d; echo damage %s" % (
        len(echoes), sum(e[2] == 30 for e in echoes), sum(0 <= e[2] < 30 for e in echoes), sum(e[2] < 0 for e in echoes), summary([e[1] for e in echoes])))
    p("")
    new, rem = [r for r in rows if not r["rematch"]], [r for r in rows if r["rematch"]]
    p("REMATCH vs FIRST MEETING (Fisher exact, two-sided): the sim draws rematches from their own %d rows, first meetings from %d" % (len(rem), len(new)))
    for name, f in (("we swung first (alone)", lambda r: r["opener_us"] and not r["opener_him"]), ("he swung first (alone)", lambda r: r["opener_him"] and not r["opener_us"]),
                    ("both on one tick", lambda r: r["opener_us"] and r["opener_him"]), ("swing on the first tick of the window", lambda r: r["wait"] == 0),
                    ("his opening swing was a special", lambda r: r["spec_him"]), ("our Vengeance up", lambda r: r["veng"]), ("our hitpoints below 120", lambda r: r["hp"] < 120),
                    ("our hitpoints at 121", lambda r: r["hp"] == 121), ("distance 1", lambda r: r["dist"] == 1), ("his bar shown", lambda r: r["bar"] >= 0),
                    ("food below the session maximum", lambda r: r["d_angler"] > 0)):
        a, c = sum(bool(f(r)) for r in new), sum(bool(f(r)) for r in rem)
        p("  %-40s first %3d/%d (%.2f)  rematch %3d/%d (%.2f)  p = %.4f" % (name, a, len(new), a / len(new), c, len(rem), c / len(rem), fisher(a, len(new) - a, c, len(rem) - c)))
    p("")
    p("PRICE: %d of %d fights have a stakes line (%d have none: before the plugin logged stakes, or no stakes line; %d `risk:` lines in these logs name no stakes)" % (len(priced), n, len(unpriced), equal_lines))
    firsts = [r for r in priced if not r["rematch"]]
    ks = sorted(r["K"] for r in firsts)
    p("  table: the clamped price us / him of the %d first meetings with a price (rematches keep theirs); all %d priced fights: %s" % (len(ks), len(priced), summary(sorted(r["K"] for r in priced))))
    p("  %s" % summary(ks))
    p("  at the clamp: 0.1: %d, 2.5: %d; raw us / him outside [0.4, 2.5] (what training draws): %d of %d" % (
        sum(k <= PRICE_MIN + 1e-9 for k in ks), sum(k >= PRICE_MAX - 1e-9 for k in ks), sum(r["K_raw"] < 0.4 or r["K_raw"] > 2.5 for r in firsts), len(firsts)))
    logk = [math.log(k) for k in ks]
    mean = sum(logk) / len(logk)
    p("  log price: mean %.3f (geometric mean %.3f), sd %.3f; log-uniform [0.4, 2.5] has mean %.3f and sd %.3f" % (mean, math.exp(mean), math.sqrt(sum((x - mean) ** 2 for x in logk) / len(logk)),
      (math.log(0.4) + math.log(2.5)) / 2, (math.log(2.5) - math.log(0.4)) / math.sqrt(12)))
    p("  price bins: " + "  ".join("%.2f-%.2f:%d" % (lo, hi, sum(lo <= k < hi or (hi == 2.5 and k == 2.5) for k in ks)) for lo, hi in
                               ((0.1, 0.2), (0.2, 0.4), (0.4, 0.7), (0.7, 1.0), (1.0, 1.4), (1.4, 2.0), (2.0, 2.5), (2.5, 2.5))))
    p("  sorted: " + " ".join("%.2f" % k for k in ks))
    return "\n".join(out)


FIELDS = ["rematch", "opener_us", "opener_him", "spec_us", "spec_him", "hp", "atk", "str", "df", "mag", "pray", "e_spec", "e_phase", "veng", "vcd", "sacks",
          "d_angler", "d_karam", "d_brew", "d_restore", "d_combat", "d_tab", "bar", "h_spec", "h_phase", "h_veng", "dist", "landed_us", "landed_him",
          "fl_us_direct", "fl_us_veng", "fl_us_recoil", "fl_him_direct", "fl_him_veng", "fl_him_recoil"]
TSV_FIELDS = ["tape", "opponent", "lo", "hi", "f", "wait", "short", "gap", "o_weapon", "u_weapon", "o_anim", "u_anim", "K", "K_raw", "price_where", "land_us", "land_him", "dmg_us", "dmg_him",
              "proc_on_his_opener", *FIELDS]


def write_header(path, rows, prices, nfights):
    with open(path, "w") as fh:
        w = fh.write
        w("/* Generated by scripts/measure_fight_starts.py from %d recorded fights (%d first meetings, %d rematches) and the death prices of %d first meetings. Do not edit. */\n" % (
            nfights, sum(1 - r["rematch"] for r in rows), sum(r["rematch"] for r in rows), len(prices)))
        w("/* One row per fight: the state at the end of the first tick on which either side swung (the report of the script explains every column).\n")
        w(" * hp ... mag, pray: ours, exact. spec/phase: special energy and its regeneration phase. d_*: what the user had used up of each supply (doses for brew, restore, combat).\n")
        w(" * bar: his health bar of 30, -1 none shown. h_*: his special energy, phase and Vengeance as the tape's tracker estimated them. dist: tiles between us.\n")
        w(" * landed_*: damage that landed on that side on the first tick, -1 none. fl_*: damage landing on that side the next tick, by cause, -1 none. */\n")
        w("typedef struct {\n")
        for f in FIELDS:
            w("    int16_t %s;\n" % f)
        w("} RiskfightStartRecord;\n")
        w("static const RiskfightStartRecord RF_START_RECORDS[] = {\n")
        for r in rows:
            w("    {%s},\n" % ", ".join(str(r[f]) for f in FIELDS))
        w("};\n")
        w("enum { RF_START_RECORD_COUNT = sizeof(RF_START_RECORDS) / sizeof(RF_START_RECORDS[0]) };\n")
        w("/* The death price (us / him gp, clamped to [0.1, 2.5] as the live server does) of the first meeting with each priced opponent, sorted. */\n")
        w("static const float RF_PRICE_TABLE[] = {%s};\n" % ", ".join("%.4ff" % k for k in prices))
        w("enum { RF_PRICE_TABLE_COUNT = sizeof(RF_PRICE_TABLE) / sizeof(RF_PRICE_TABLE[0]) };\n")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--work", default="/tmp/fight_starts_work")
    ap.add_argument("--light", default=DEFAULT_LIGHT)
    ap.add_argument("--tapes", help="a directory of tapes (with manifest.tsv) exported already")
    ap.add_argument("--recordings", default=REC_GLOB)
    ap.add_argument("--no-write", action="store_true", help="print the report only")
    args = ap.parse_args()
    os.makedirs(args.work, exist_ok=True)
    attacks = attack_animations(args.light)
    if args.tapes:
        tape_dir, manifest = args.tapes, os.path.join(args.tapes, "manifest.tsv")
    else:
        recs = sorted(glob.glob(os.path.expanduser(args.recordings)))
        assert recs, "no recordings match " + args.recordings
        tape_dir = export_tapes(args.light, recs, args.work)
        manifest = os.path.join(args.work, "manifest.tsv")
    recording_of, excluded = read_manifest(manifest)
    tapes = [Tape(os.path.join(tape_dir, name)) for name in sorted(recording_of)]
    by_recording = collections.defaultdict(list)
    for t in tapes:
        by_recording[recording_of[t.name]].append(t)
    rows, swing_free = [], []
    for t in tapes:
        r = measure(t, attacks)
        (swing_free.append(t.name) if r is None else rows.append(r))
    most = {rec: session_maxima(ts) for rec, ts in by_recording.items()}
    for r in rows:
        rec = recording_of[r["tape"]]
        for k in ("angler", "karam", "brew", "restore", "combat", "tab"):
            r["d_" + k] = most[rec][k] - r[k]
            assert r["d_" + k] >= 0, (r["tape"], k)
    previous = {}
    for r in sorted(rows, key=lambda r: (recording_of[r["tape"]], r["lo"])):
        rec = recording_of[r["tape"]]
        r["rematch"] = int(rec in previous and previous[rec] == r["opponent"])
        previous[rec] = r["opponent"]
    clocks = {rec: tick_clock(rec) for rec in by_recording}
    risk = {rec: risk_lines(rec) for rec in by_recording}
    priced, unpriced = [], []
    for r in rows:
        rec = recording_of[r["tape"]]
        found = price_of(rec, clocks, risk[rec][0], r)
        r["K"], r["K_raw"], r["price_where"] = "", "", ""
        if found is None:
            unpriced.append(r)
        else:
            us, him, where = found
            r["K_raw"], r["K"], r["price_where"] = us / him, min(PRICE_MAX, max(PRICE_MIN, us / him)), where
            priced.append(r)
    prices = sorted(round(r["K"], 4) for r in priced if not r["rematch"])
    text = report(rows, priced, unpriced, excluded, swing_free, sum(v[1] for v in risk.values()))
    print(text)
    if args.no_write:
        return
    rows.sort(key=lambda r: (r["rematch"], r["tape"]))
    for r in rows:
        r.update({k: ("" if v is None else v) for k, v in r["opening"].items() if k in ("land_us", "land_him", "dmg_us", "dmg_him", "proc_on_his_opener")})
    write_header(os.path.join(OUT_DIR, "riskfight_starts_data.h"), rows, prices, len(rows))
    with open(os.path.join(OUT_DIR, "fight_starts.tsv"), "w") as fh:
        fh.write("\t".join(TSV_FIELDS) + "\n")
        for r in rows:
            fh.write("\t".join(str(r[f]) if not isinstance(r[f], list) else "+".join(map(str, r[f])) for f in TSV_FIELDS) + "\n")
    with open(os.path.join(args.work, "report.txt"), "w") as fh:
        fh.write(text + "\n")


if __name__ == "__main__":
    main()
