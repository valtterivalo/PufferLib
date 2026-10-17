#!/usr/bin/env python3
"""How often the user over-eats in his own recordings, with the sim's definition (riskfight_chance_range, RF_OVEREAT_PATIENCE).

Usage: python3 scripts/measure_overeats.py [--work DIR] [--light DIR] [--traced DIR] [--min-ticks N] [--recordings GLOB]      (repository root or anywhere; python3 + zstd + cc)

Pipeline (everything under --work, default /tmp/overeat_work):
  1. tools/rollout_export.py of the light repo (--light) writes one tape per fight window of every session_202610* recording (the Gear Swapper PvP recordings, read only).
  2. Each tape becomes the request stream the live engine would have sent for that fight (LA/B/K/F/O/Q/W/G/SHIELD/ARMOR/PS/RA/P/R/V/END, ocean/osrs/tests/gs_rl_server.c
     and gs_lookahead_server.c read_request): our exact hitpoints, levels, gear and bag from the T rows; his composition and weapon from the recording's actor states (any
     observation kind, held forward from the last one: the tapes' E rows exist only for the per-tick states of October 10 and later, the composition is in the callback
     states of the older recordings too); his Vengeance casts (graphic, cast sound on his tile) and 'Taste vengeance!' texts from the recording; his swings, specials, meals,
     health bar and hitsplats from the A, H and T rows, as EngineSeen.java builds the V line.
  3. ocean/osrs/tests/overeat_tapes.c (built here) replays the streams through the live server's own path (rt_parse, rt_decide) and calls riskfight_chance_range(&state, 0)
     after every tick, and reads our meals off the bag (a slot that held food, karambwan, pie or brew changed during the tick).
  4. A meal of tick N is an overeat when our hitpoints at the end of tick N - 1 (the start of tick N) are strictly above the range of tick N - 1 (riskfight_execute_command).
--traced DIR: also compares the generated streams tick by tick (hitpoints, range, its three terms, meals) with request streams the plugin itself traced on a replay
(JAVA_TOOL_OPTIONS=-Dlight.engine.trace=DIR/<id> tools/engine-replay --local --policy RL <recording>; files named <session>_<tag>.requests) wherever both cover a tick.

Fights are the tapes' windows (pvp_fight.fights, >= --min-ticks); fights whose opponent's composition was never observed (October 4-6) are reported apart and not used for the
patience: without his weapon the range is only his fists plus the echo.

Report: the user's overeats per fight (mean, percentiles, histogram), what the range is made of, the share of his meals that are overeats, the opponents' meals against our
range as he could see it (his hitpoints only bounded by his health bar), and the patience rule: RF_OVEREAT_PATIENCE = the smallest integer above the user's p90 per fight
(the opponent leaves when overeats > patience, so a fight that matches the p90 count is still tolerated).
"""

import argparse
import collections
import glob
import json
import math
import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEFAULT_LIGHT = os.path.expanduser("~/Projects/storm/light")
REC_GLOB = os.path.expanduser("~/.light2/gameplay-recordings/local-debug/session_202610*.jsonl.zst")

# ---- animations (AttackAnimations.java, tools/pvp_fight.py, ocean/osrs/tests/fidelity_riskfight.c is_attack_anim / is_spec_anim) ----
ATTACK_ANIMS = {390, 1658, 2066, 2067, 10172, 10173, 376, 401, 1062, 7045, 7644, 11057, 426, 11275, 1060, 440, 1667, 7514, 7515, 3297, 3298, 3299, 3300,
                2080, 2081, 5865, 5866, 1665, 8145, 4230, 386, 1378, 6600, 393, 10427}
SPEC_ANIMS = {11275, 7644, 7514, 7515, 1062, 1060, 3299, 3300, 1378, 10173}   # one special each; the maul's instant 1667 is counted from the hits it lands
INSTANT_SPEC_ANIM = 1667
EAT_ANIMS = {829, 5213}
VENG_CAST_ANIMS = {8316, 8317}
OVERHEADS = ["NONE", "SMITE", "PROTECT_FROM_MELEE", "PROTECT_FROM_MISSILES", "PROTECT_FROM_MAGIC", "REDEMPTION"]
POISON_WEAPONS = {27867, 12904}   # gs_lookahead_server.c request_poison_weapon
CANONICAL = {24592: 13441, 24595: 3144, 24598: 3024, 24601: 3026, 24603: 3028, 24605: 3030, 20513: 10551, 28067: 10551, 28070: 10828, 21287: 21295}   # rollout_export.py
VENG_CAST_SOUNDS = {2907, 7569}                 # GearSwapperPlugin.VENGEANCE_CAST_SOUNDS
VENG_CAST_SPOT_ANIMS = {726, 725}               # SpotanimID lunar spellbook vengeance (self, other); the Trailblazer variant is not in the recordings


# ---- what the tapes lack: his composition at every tick, his Vengeance casts and procs ------------------------------------------------------------------------
def scan_recording(rec, opponents):
    """From one recording, per opponent name (spaces as underscores, the tape's): his composition observations (any observation kind: the per-tick GAME_TICK states exist
    only from October 10, the older recordings have the same composition in COMPOSITION_CALLBACK / INTERACTING_CALLBACK states, which rollout_export.py does not read), the
    ticks of his Vengeance casts (cast graphic, or the cast sound on his tile: GearSwapperPlugin.onGraphicChanged / onAreaSoundEffectPlayed) and of his 'Taste vengeance!'."""
    wanted = {n: dict(gear=[], casts=set(), texts=set(), tile=[], cycle=0) for n in opponents}
    sounds = []
    proc = subprocess.Popen(["zstd", "-dc", rec], stdout=subprocess.PIPE, text=True, bufsize=1 << 20)
    for line in proc.stdout:
        if '"eventKind":"ACTOR_STATE_OBSERVED"' in line:
            i = line.index('"cleanName":"') + 13
            if line[i:line.index('"', i)].replace(" ", "_") not in wanted:
                continue
            r = json.loads(line)
            w = wanted[r["payload"]["actor"]["cleanName"].replace(" ", "_")]
            t, st = r["lastObservedGameTick"], r["payload"]["state"]
            lp = st.get("localPoint") or {}
            if lp.get("availability") == "OBSERVED":
                w["tile"].append((t, lp["x"] >> 7, lp["y"] >> 7))
            for spot in (st.get("effects") or {}).get("spotAnims", ()):
                if spot["id"] in VENG_CAST_SPOT_ANIMS and spot["startCycle"] != w["cycle"]:
                    w["cycle"] = spot["startCycle"]
                    w["casts"].add(t)
            eq = st.get("equipment") or {}
            if eq.get("availability") == "OBSERVED_COMPOSITION":
                off = eq["itemOffset"]
                items = [CANONICAL.get(raw - off, raw - off) if raw >= off else -1 for raw in eq["rawEquipmentIds"]]
                if not w["gear"] or w["gear"][-1][1] != items:
                    w["gear"].append((t, items))
        elif '"eventKind":"OVERHEAD_TEXT_OBSERVED"' in line and "engeance" in line:
            r = json.loads(line)
            n = r["payload"]["actor"].get("cleanName", "").replace(" ", "_")
            if n in wanted:
                wanted[n]["texts"].add(r["lastObservedGameTick"])
        elif '"eventKind":"SOUND_EFFECT_OBSERVED"' in line and any('"soundId":%d' % sid in line for sid in VENG_CAST_SOUNDS):
            r = json.loads(line)["payload"]
            if r.get("scope") == "AREA":
                sounds.append((json.loads(line)["lastObservedGameTick"], r["sceneX"], r["sceneY"]))
    if proc.wait() != 0:
        raise RuntimeError("zstd failed on " + rec)
    for t, x, y in sounds:
        for w in wanted.values():
            if any(abs(tt - t) <= 3 and (tx, ty) == (x, y) for tt, tx, ty in w["tile"]):
                w["casts"].add(t)
    for w in wanted.values():
        w["gear"].sort(key=lambda g: g[0])
    return {n: dict(gear=w["gear"], casts=sorted(w["casts"]), texts=sorted(w["texts"])) for n, w in wanted.items()}


def gear_at(gear, t):
    """The last composition observed at or before tick t; before the first observation, the first (the opponent is in the scene, the callback fires when it changes)."""
    if not gear:
        return None
    held = gear[0][1]
    for tt, items in gear:
        if tt > t:
            break
        held = items
    return held


# ---- tape -> request stream ----------------------------------------------------------------------------------------------------------------------------------
class Tape:
    def __init__(self, path):
        self.path = path
        self.anims = collections.defaultdict(list)   # tick -> [(side, anim)]
        self.hits = collections.defaultdict(list)    # tick -> [(side, amount, type, class)]
        self.rows = {}                               # tick -> T fields
        self.menus = []                              # (tick, kind, slot, raw)
        with open(path) as fh:
            for line in fh:
                p = line.split()
                if not p:
                    continue
                if p[0] == "FIGHT":
                    self.index, self.session, self.opponent, self.lo, self.hi = int(p[1]), p[2], p[3], int(p[4]), int(p[5])
                elif p[0] == "A":
                    self.anims[int(p[1])].append((p[2], int(p[3])))
                elif p[0] == "H":
                    self.hits[int(p[1])].append((p[2], int(p[3]), int(p[4]), p[5]))
                elif p[0] == "M":
                    self.menus.append((int(p[1]), p[2], int(p[3]), int(p[4])))
                elif p[0] == "T":
                    self.rows[int(p[1])] = p


def t_field(p):
    """The named fields of a T row (rollout_export.py)."""
    return dict(tick=int(p[1]), hp=int(p[2]), atk=int(p[3]), str=int(p[4]), defence=int(p[5]), mag=int(p[6]), prayer=int(p[7]), ox=int(p[10]) - int(p[8]),
                oy=int(p[11]) - int(p[9]), spec_us=int(p[12]), spec_phase=int(p[13]), veng_us=int(p[14]), veng_cd=int(p[15]), sacks=int(p[16]), recoil=int(p[17]),
                veng_him=int(p[24]), recoil_evidence=int(p[30]), bar=(int(p[31]), int(p[32])), overhead=p[34], piety=int(p[35]), worn=[int(x) for x in p[36:49]],
                bag=[int(x) for x in p[49:77]])


def request_stream(tape, scan):
    """The request text of every tick of the fight, as the Gear Swapper would have sent it (OBS requests: only the state, no decision is wanted here). `scan`: scan_recording()
    of this opponent (composition timeline, casts, procs)."""
    out = ["FIGHT %d" % tape.lo]
    casts, texts = set(scan["casts"]), set(scan["texts"])
    pending_maul = None              # tick a maul special launched (its hits count on that tick or the next)
    ticks = sorted(tape.rows)
    for n, t in enumerate(ticks):
        f = t_field(tape.rows[t])
        held = gear_at(scan["gear"], t)
        weapon = held[3] if held else -1
        shield = held[5] if held else None
        # V: what the client showed of him this tick
        swing_raw, style, spec, consuming, cast = -1, 0, 0, 0, 0
        maul = False
        for side, anim in tape.anims.get(t, ()):
            if side != "O":
                continue
            if anim in ATTACK_ANIMS or anim == INSTANT_SPEC_ANIM:
                swing_raw, style = weapon, 1
                spec = 1 if anim in SPEC_ANIMS else 0
                maul = anim == INSTANT_SPEC_ANIM
            if anim in EAT_ANIMS:
                consuming = 1
            if anim in VENG_CAST_ANIMS:
                cast = 1
        cast = int(cast or t in casts)
        hits_on_him = [h for h in tape.hits.get(t, ()) if h[0] == "O"]
        attack_hits_on_us = sum(1 for h in tape.hits.get(t, ()) if h[0] == "U" and h[3] in ("A", "B"))   # his attack hits, blocks included, not recoil or echoes
        specs = spec
        if maul:
            pending_maul = t
            if attack_hits_on_us:
                specs, pending_maul = min(2, attack_hits_on_us), None
        elif pending_maul == t - 1:
            pending_maul = None
            if attack_hits_on_us and (swing_raw == -1 or swing_raw == weapon):
                specs = min(2, attack_hits_on_us)
        veng_text = int(t in texts)
        ratio, scale = f["bar"]
        shown = ratio >= 0 and scale > 0
        o = [f["hp"], f["prayer"], f["atk"], f["str"], f["defence"], f["mag"], f["spec_us"], f["spec_phase"], 0, f["veng_us"], f["veng_cd"], f["sacks"], f["recoil"],
             -1, 0, 0, 0, f["piety"], OVERHEADS.index(f["overhead"]), -1, 4, -1, 4, weapon, f["recoil_evidence"], f["ox"], f["oy"]]
        out.append("LA %d %d OBS" % (n + 1, t))
        out.append("B 1 121 100 100 0 2 0.0 0 0 0 %d 99 0 0 -1" % t)
        out.append("K 1000000 1000000")
        out.append("F 1 0 0 100")
        out.append("O " + " ".join(map(str, o)) + " UNKNOWN 0 0")
        out.append("Q none 0")
        out.append("W " + " ".join(map(str, f["worn"])))
        out.append("G " + " ".join(map(str, f["bag"])))
        if held is None:
            out.append("SHIELD UNKNOWN")
            out.append("ARMOR UNKNOWN")
        else:
            out.append("SHIELD OBSERVED %d" % shield)
            out.append("ARMOR OBSERVED " + " ".join(str(held[i]) for i in (0, 1, 2, 4, 7, 9, 10)))
        if POISON_WEAPONS & set(f["worn"] + f["bag"] + [weapon]):
            # A toxic weapon makes the server ask for the poison immunity of both sides, which the recording does not give; the range does not read it
            out.append("PS 0 KNOWN 0 0 100000")
            out.append("PS 1 KNOWN 0 0 100000")
        else:
            out.append("PS 0 KNOWN 0 0 0")
            out.append("PS 1 CLEAR_PRIOR")
        out.append("RA OBSERVED 1")
        out.append("P KNOWN 0")
        out.append("R KNOWN 0")
        out.append("V %d %d %d %d %d %d %d %d %d %d" % (swing_raw, style, int(bool(hits_on_him)), sum(h[1] for h in hits_on_him), cast, consuming,
                                                          ratio if shown else -1, scale if shown else -1, veng_text, specs))
        out.append("END")
    return "\n".join(out) + "\n"


# ---- harness -----------------------------------------------------------------------------------------------------------------------------------------------
COLUMNS = ["file", "fight", "tick", "hp", "range", "normal", "spec", "echo", "veng_up", "veng_strict", "spec_est", "wielded", "his_meal", "eat", "brew",
           "unclassified", "bar_lo", "bar_hi", "our_max", "our_spec_cost", "our_spec_max", "our_energy", "his_max", "our_veng"]


def build_harness(work):
    """Built on every run: the harness includes the whole sim and live server, so a cached binary would measure whatever headers it was built against."""
    exe = os.path.join(work, "overeat_tapes")
    src = os.path.join(ROOT, "ocean/osrs/tests/overeat_tapes.c")
    subprocess.run(["cc", "-std=gnu11", "-O2", "-I.", "-Isrc", "-Wno-comment", "-o", exe, src, "-lm", "-lpthread"], cwd=ROOT, check=True)
    return exe


UNAVAILABLE = []   # (file, tick, reason) of every request the live server would have answered UNAVAILABLE: no row


def run_harness(exe, files):
    """Rows of the harness as dicts. The harness prints the file name first; stderr carries the server's own [prior] notes and the UNAVAILABLE requests."""
    rows = []
    for i in range(0, len(files), 40):
        proc = subprocess.run([exe] + files[i:i + 40], cwd=ROOT, check=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        UNAVAILABLE.extend(tuple(line.split()[1:4]) for line in proc.stderr.splitlines() if line.startswith("UNAVAILABLE "))
        for line in proc.stdout.splitlines():
            p = line.split("\t")
            rows.append(dict(zip(COLUMNS, [p[0]] + [int(x) for x in p[1:]])))
    return rows


# ---- statistics ----------------------------------------------------------------------------------------------------------------------------------------------
def percentile(xs, q):
    """Linear interpolation between order statistics (numpy default)."""
    xs = sorted(xs)
    if not xs:
        return float("nan")
    k = (len(xs) - 1) * q
    f = math.floor(k)
    c = min(f + 1, len(xs) - 1)
    return xs[f] + (xs[c] - xs[f]) * (k - f)


def summary(xs):
    if not xs:
        return "no fights"
    return "n=%d mean %.2f p50 %.1f p75 %.1f p90 %.1f p95 %.1f max %d" % (len(xs), sum(xs) / len(xs), percentile(xs, .5), percentile(xs, .75), percentile(xs, .9),
                                                                          percentile(xs, .95), max(xs))


def histogram(xs):
    c = collections.Counter(xs)
    return "  ".join("%d:%d" % (k, c[k]) for k in range(max(xs) + 1)) if xs else ""


def range_strict(r):
    """The earlier range: Vengeance counted up only after a seen cast that has not proc'd, never at the start of a fight (riskfight_policy.h now counts it up from the
    start until a proc or a damaging hit on him without one)."""
    return r["normal"] + r["spec"] + (r["echo"] if r["veng_strict"] else 0)


def meals_of(fight_rows):
    """(tick, row before, kind) of every meal of ours in the fight, judged against the row of the tick before; meals on the first tick have no row before."""
    by_tick = {r["tick"]: r for r in fight_rows}
    out, unjudged = [], 0
    for r in fight_rows:
        for kind in ["eat"] * r["eat"] + ["brew"] * r["brew"]:
            before = by_tick.get(r["tick"] - 1)
            if before is None:
                unjudged += 1
            else:
                out.append((r["tick"], before, kind))
    return out, unjudged


def opponent_meals(fight_rows):
    """His meals (V consuming) against the most we could deal before his next meal resolves, with his hitpoints bounded by the bar: (definite, possible) overeat flags."""
    by_tick = {r["tick"]: r for r in fight_rows}
    out = []
    for r in fight_rows:
        before = by_tick.get(r["tick"] - 1)
        if not r["his_meal"] or before is None:
            continue
        hit = max(before["our_max"], before["our_spec_max"] if before["our_energy"] >= before["our_spec_cost"] > 0 else 0)
        echo = before["his_max"] * 3 // 4 if before["our_veng"] else 0
        reach = hit + echo
        out.append((before["bar_lo"] > reach, before["bar_hi"] > reach, reach))
    return out


def report(rows, tapes, scans, min_ticks):
    by_fight = collections.defaultdict(list)
    for r in rows:
        by_fight[(r["file"], r["fight"])].append(r)
    fights = []
    for (path, _), frows in sorted(by_fight.items()):
        tape = tapes[path]
        frows.sort(key=lambda r: r["tick"])
        meals, unjudged = meals_of(frows)
        fights.append(dict(tape=tape, rows=frows, meals=meals, unjudged=unjudged, known=bool(scans[tape.session[:15]][tape.opponent]["gear"])))
    fights = [f for f in fights if f["tape"].hi - f["tape"].lo >= min_ticks]
    lines = []
    emit = lines.append
    known = [f for f in fights if f["known"]]
    unknown = [f for f in fights if not f["known"]]
    emit("fights: %d with a tape of >= %d ticks; his composition (so his weapon) observed in %d (%d ticks), never observed in %d (%d ticks)" % (
        len(fights), min_ticks, len(known), sum(len(f["rows"]) for f in known), len(unknown), sum(len(f["rows"]) for f in unknown)))
    for title, group in (("his composition observed", known), ("his composition never observed (recordings of October 4-6: his weapon is unknown, the range is his fists plus the echo)", unknown)):
        emit("")
        emit("== %s: %d fights" % (title, len(group)))
        for label, rng in (("as written (Vengeance up from the fight's start until a proc or a damaging hit without one, then after a seen cast)", lambda r: r["range"]),
                           ("Vengeance up only after a seen cast (the earlier strict definition)", range_strict)):
            counts = [sum(1 for _, b, _ in f["meals"] if b["hp"] > rng(b)) for f in group]
            meals = sum(len(f["meals"]) for f in group)
            over = sum(counts)
            emit("  user's overeats per fight, %s: %s" % (label, summary(counts)))
            emit("    histogram (overeats:fights) %s" % histogram(counts))
            emit("    meals %d (judged; %d on a fight's first tick unjudged), overeats %d = %.1f%% of his meals" % (
                meals, sum(f["unjudged"] for f in group), over, 100.0 * over / meals if meals else float("nan")))
            if label.startswith("as written"):
                meals_per_fight = [len(f["meals"]) for f in group]
                emit("    meals per fight: %s" % summary(meals_per_fight))
                ticks = [len({t for t, b, _ in f["meals"] if b["hp"] > rng(b)}) for f in group]
                emit("    overeating ticks per fight (a double or triple eat in one tick counted once): %s" % summary(ticks))
        # what the range is made of
        allrows = [r for f in group for r in f["rows"]]
        at_meal = [b for f in group for _, b, _ in f["meals"]]
        for name, rs in (("every tick", allrows), ("ticks before a meal", at_meal)):
            if rs:
                n = len(rs)
                emit("  range at %s (n=%d): mean %.1f = normal %.1f + special %.1f + echo %.1f (echo counted while up); special on in %.0f%% of ticks, echo on in %.0f%% (strict: %.0f%%); his hp bar to us: mean our hp %.1f" % (
                    name, n, sum(r["range"] for r in rs) / n, sum(r["normal"] for r in rs) / n, sum(r["spec"] for r in rs) / n, sum(r["echo"] for r in rs if r["veng_up"]) / n,
                    100.0 * sum(r["spec"] > 0 for r in rs) / n, 100.0 * sum(r["echo"] > 0 and r["veng_up"] for r in rs) / n, 100.0 * sum(r["echo"] > 0 and r["veng_strict"] for r in rs) / n,
                    sum(r["hp"] for r in rs) / n))
        if at_meal:
            n = len(at_meal)
            emit("  meals judged: hp at the start of the tick (mean %.1f, p50 %.0f, p90 %.0f); range (mean %.1f); hp - range (p10 %.0f, p50 %.0f, p90 %.0f)" % (
                sum(b["hp"] for b in at_meal) / n, percentile([b["hp"] for b in at_meal], .5), percentile([b["hp"] for b in at_meal], .9), sum(b["range"] for b in at_meal) / n,
                percentile([b["hp"] - b["range"] for b in at_meal], .1), percentile([b["hp"] - b["range"] for b in at_meal], .5), percentile([b["hp"] - b["range"] for b in at_meal], .9)))
            by_kind = collections.Counter(k for f in group for _, _, k in f["meals"])
            emit("  meals by kind: %s" % dict(by_kind))
            # which terms keep a meal from being an overeat: the overeat count if a term were dropped from the range
            for name, fn in (("without the special", lambda b: b["normal"] + (b["echo"] if b["veng_up"] else 0)), ("without the echo", lambda b: b["normal"] + b["spec"]),
                             ("normal max hit alone", lambda b: b["normal"])):
                over = sum(1 for b in at_meal if b["hp"] > fn(b))
                emit("    overeats if the range were %s: %d of %d meals (%.1f%%)" % (name, over, n, 100.0 * over / n))
        # opponents
        opp = [(f, opponent_meals(f["rows"])) for f in group]
        meals = sum(len(o) for _, o in opp)
        if meals:
            definite = [sum(d for d, _, _ in o) for _, o in opp]
            possible = [sum(p for _, p, _ in o) for _, o in opp]
            emit("  opponents' meals (eat/drink animation): %d in %d fights; overeats against our range as he could see it (our wielded weapon and special, our Vengeance echo of his best hit)" % (meals, len(group)))
            emit("    definite (his bar's lower bound above the range) per fight: %s" % summary(definite))
            emit("    histogram %s" % histogram(definite))
            emit("    possible (the bar's upper bound above the range) per fight: %s" % summary(possible))
            emit("    histogram %s" % histogram(possible))
            emit("    definite %.1f%% / possible %.1f%% of his meals; meals per fight %s" % (100.0 * sum(definite) / meals, 100.0 * sum(possible) / meals, summary([len(o) for _, o in opp])))
    # patience
    emit("")
    if known:
        counts = sorted(sum(1 for _, b, _ in f["meals"] if b["hp"] > b["range"]) for f in known)
        p90 = percentile(counts, .9)
        patience = math.floor(p90) + 1
        emit("patience rule: the smallest integer above the user's p90 overeats per fight (as written, composition observed): p90 = %.2f -> RF_OVEREAT_PATIENCE = %d" % (p90, patience))
        emit("  share of the user's own fights that would annoy an opponent (overeats > patience): patience 6 (provisional) %.1f%%, patience %d %.1f%%, patience %d %.1f%%" % (
            100.0 * sum(c > 6 for c in counts) / len(counts), patience, 100.0 * sum(c > patience for c in counts) / len(counts), patience - 1,
            100.0 * sum(c > patience - 1 for c in counts) / len(counts)))
        counts_s = sorted(sum(1 for _, b, _ in f["meals"] if b["hp"] > range_strict(b)) for f in known)
        p90s = percentile(counts_s, .9)
        emit("  with Vengeance counted up only after a seen cast (the earlier strict definition): p90 = %.2f -> %d" % (p90s, math.floor(p90s) + 1))
        first_proc = collections.Counter()
        for f in known:
            t = f["tape"]
            sc = scans[t.session[:15]][t.opponent]
            casts = [x for x in sc["casts"] if t.lo <= x <= t.hi] + [x for x, a in t.anims.items() if t.lo <= x <= t.hi and any(side == "O" and an in VENG_CAST_ANIMS for side, an in a)]
            texts = [x for x in sc["texts"] if t.lo <= x <= t.hi]
            first_proc["no proc seen" if not texts else "proc before any cast seen" if not casts or min(texts) < min(casts) else "cast, then proc"] += 1
        emit("  his Vengeance at the start of a fight (why the sim counts it up from the start): %s" % dict(first_proc))
    ticks, anims, both = cross_check(fights)
    emit("cross-check: %d ticks with a meal read off the bag, %d ticks with our eat animation (829) in the tape, %d with both" % (ticks, anims, both))
    emit("requests the live server would answer UNAVAILABLE (no row): %d" % len(UNAVAILABLE))
    unclassified = sum(r["unclassified"] for r in rows)
    emit("bag slots that changed from an item the content table has no row for: %d (possible meals of an unknown food)" % unclassified)
    return "\n".join(lines), fights


def cross_check(fights):
    """Meal ticks read off the bag against the eat animations (829) of ours in the tape: every meal tick plays one (a double or triple eat in one tick plays it once)."""
    anims = ticks = both = 0
    for f in fights:
        rows = {r["tick"]: r for r in f["rows"]}
        for t, r in rows.items():
            meal = r["eat"] + r["brew"] > 0
            anim = any(side == "U" and a == 829 for side, a in f["tape"].anims.get(t, ()))
            ticks += meal
            anims += anim
            both += meal and anim
    return ticks, anims, both


# ---- comparison with the plugin's own traced streams ------------------------------------------------------------------------------------------------------------
def compare(rows, tapes, traced_dir, exe):
    """Per tick: hitpoints, range and its terms of the generated stream against a stream the plugin itself traced. Only the traced fights that start on the tick the tape's
    fight starts are compared (a replay that cut a fight into pieces restarts the visible-opponent history, so its ranges are not the whole fight's)."""
    gen, starts = {}, {}
    for r in rows:
        key = (tapes[r["file"]].session[:15], r["tick"])
        gen[key] = r
        starts[(tapes[r["file"]].session[:15], r["file"], r["fight"])] = min(starts.get((tapes[r["file"]].session[:15], r["file"], r["fight"]), 1 << 30), r["tick"])
    gen_starts = {(k[0], v) for k, v in starts.items()}
    traced_files = sorted(glob.glob(os.path.join(traced_dir, "*.requests")))
    traced_rows = run_harness(exe, [f for f in traced_files if os.path.getsize(f) > 0])
    first = {}
    for r in traced_rows:
        k = (os.path.basename(r["file"])[:15], r["file"], r["fight"])
        first[k] = min(first.get(k, 1 << 30), r["tick"])
    n = bad = 0
    fields = ["hp", "range", "normal", "spec", "echo", "wielded", "eat", "brew"]
    diffs = collections.Counter()
    examples = []
    for r in traced_rows:
        k = (os.path.basename(r["file"])[:15], r["file"], r["fight"])
        g = gen.get((k[0], r["tick"]))
        if g is None or (k[0], first[k]) not in gen_starts:
            continue
        n += 1
        wrong = [f for f in fields if g[f] != r[f]]
        if wrong:
            bad += 1
            diffs.update(wrong)
            if len(examples) < 12:
                examples.append(((k[0], r["tick"]), {f: (g[f], r[f]) for f in wrong}))
    return n, bad, diffs, examples


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--work", default="/tmp/overeat_work")
    ap.add_argument("--light", default=DEFAULT_LIGHT, help="the light repository (tools/rollout_export.py)")
    ap.add_argument("--recordings", default=REC_GLOB)
    ap.add_argument("--traced", help="directory of request streams the plugin traced, to compare with")
    ap.add_argument("--min-ticks", type=int, default=15, help="fights shorter than this (hi - lo) are target-switch artefacts (rollout_export short)")
    ap.add_argument("--rows", help="write every harness row (TSV) here")
    args = ap.parse_args()
    os.makedirs(args.work, exist_ok=True)
    tape_dir = os.path.join(args.work, "tapes")
    req_dir = os.path.join(args.work, "requests")
    os.makedirs(req_dir, exist_ok=True)
    recs = sorted(glob.glob(os.path.expanduser(args.recordings)))
    if not recs:
        sys.exit("no recordings match " + args.recordings)
    if not glob.glob(os.path.join(tape_dir, "*.tape")):
        subprocess.run([sys.executable, os.path.join(args.light, "tools/rollout_export.py"), tape_dir, *recs, "--item-metadata", os.path.join(ROOT, "ocean/osrs/osrs_item_obs_generated.h"),
                        "--jobs", "3"], check=True, stdout=subprocess.DEVNULL)
    by_session = {os.path.basename(r)[8:23]: r for r in recs}
    loaded = []
    for path in sorted(glob.glob(os.path.join(tape_dir, "*.tape"))):
        tape = Tape(path)
        if tape.hi - tape.lo >= args.min_ticks:
            loaded.append(tape)
    scans = {}
    for session in sorted({t.session[:15] for t in loaded}):
        opponents = sorted({t.opponent for t in loaded if t.session[:15] == session})
        cache = os.path.join(args.work, "scan_%s.json" % session)
        if os.path.exists(cache):
            with open(cache) as fh:
                scans[session] = json.load(fh)
        else:
            scans[session] = scan_recording(by_session[session], opponents)
            with open(cache, "w") as fh:
                json.dump(scans[session], fh)
    tapes = {}
    for tape in loaded:
        req = os.path.join(req_dir, os.path.basename(tape.path)[:-5] + ".requests")
        with open(req, "w") as fh:
            fh.write(request_stream(tape, scans[tape.session[:15]][tape.opponent]))
        tapes[req] = tape
    exe = build_harness(args.work)
    rows = run_harness(exe, sorted(tapes))
    if args.rows:
        with open(args.rows, "w") as fh:
            fh.write("\t".join(COLUMNS) + "\n")
            for r in rows:
                fh.write("\t".join(str(r[c]) for c in COLUMNS) + "\n")
    text, fights = report(rows, tapes, scans, args.min_ticks)
    print(text)
    if args.traced:
        n, bad, diffs, examples = compare(rows, tapes, args.traced, exe)
        print("\ncomparison with the plugin's traced streams: %d ticks covered by both, %d differ (%s)" % (n, bad, dict(diffs)))
        for key, d in examples:
            print("  ", key, d)


if __name__ == "__main__":
    main()
