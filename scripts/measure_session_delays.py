#!/usr/bin/env python3
"""Session delay measurement for the riskfight session simulator, from the Gear Swapper PvP recordings.

Usage: python3 measure_session_delays.py [--list]      (python3 stdlib + the zstd binary; at most 3 processes)
Reads ~/.light2/gameplay-recordings/local-debug/session_202610*.jsonl.zst (read only; the July-September recordings hold no player opponent),
caches one compact JSON per recording in /tmp/dm_cache (cold run about 4-5 minutes with 3 processes, warm run seconds) and prints sections A-I.
--list also prints every delay sample of the main classes. /tmp/dm_samples.tsv gets one row per pair of consecutive fights.

Event definitions (game ticks of 0.6 s; a tick label N is the pvp_fight convention: events between GameTick N-1 and N):
  fight      pvp_fight.fights(): windows where our interaction target is one named player (gap <= 15 ticks) with >= 3 attack splats on us or him; consecutive
             windows of the same opponent <= 15 ticks apart are merged; windows shorter than 15 ticks (rollout_export `short`) are target-switch artefacts and dropped.
             start lo = first tick he is our interaction target, end hi = last such tick.
  our death  our HP == 0 (first tick within 10 ticks before our animation 836) else the tick of animation 836.
  our kill   his animation 836, or his clientDeadFlag, or the 'Target killed' chat line, within [lo, hi + 6].
  our escape tablet: our animation 4069/4071 (Teleport to house break) first tick of the burst; Exit Arena: last 'Exit Arena' click before the one tick
             position jump of >= 4 tiles (the portal); unconfirmed clicks (recording ended first, no 'cannot leave' chat) count too. A blocked attempt
             ('cannot teleport/leave ... special attack', 'cannot leave the crater') plays no animation and does not count; the successful one does.
  his tp     his animation 4069/4071/714/3864 in [hi - 6, hi + 12] when nothing else ended the fight.
  outcome    priority: death, kill, escape in [hi - 25, hi + 12], his tp, other (tracking lost, not classified).
  delay      start lo of the next real fight minus the end event tick (death tick / kill tick / escape tick). Tick labels are used inside a recording and across
             recordings of one client instance whose labels agree with the wall clock within 5 s; otherwise wall seconds / 0.6 (lower fidelity, flagged).
  in-session next fight starts <= 600 s of wall clock after the last tracked tick of this one; longer gaps are reported separately, never as delays.
"""

import bisect
import collections
import datetime
import glob
import json
import math
import multiprocessing
import os
import re
import subprocess
import sys

REC_DIR = os.path.expanduser("~/.light2/gameplay-recordings/local-debug")
CACHE_DIR = "/tmp/dm_cache"
SCHEMA = 2

# ---- copied from tools/pvp_fight.py (light repo, 2026-10-16) ------------------------------------------------------
WANTED = {
    "SESSION_STARTED", "SESSION_ENDED", "GAME_TICK_OBSERVED", "ANIMATION_OBSERVED", "HITSPLAT_APPLIED_OBSERVED",
    "SEMANTIC_MENU_ACTION", "CHAT_MESSAGE_OBSERVED", "GAME_STATE_OBSERVED", "ACTOR_STATE_OBSERVED",
}
SPECS = {11275: "VW spec", 1667: "maul spec", 7644: "AGS spec", 7514: "claws spec", 7515: "VLS spec", 1062: "dds spec",
         1060: "mace spec", 3299: "abyssal dagger spec", 3300: "BH dagger spec"}
HIT_ATTACK_TYPES = {16, 17, 43}
POISON = 65


def tag(s):
    return re.sub(r"<[^>]*>", "", s)


def name(actor):
    return actor.get("cleanName") or actor["kind"]


def load(rec):
    """Wanted events of one recording as (tick, monotonicNanos, kind, payload), streamed from zstd; only the session markers when the recording never
    tracks a player opponent."""
    proc = subprocess.Popen(["zstd", "-dc", rec], stdout=subprocess.PIPE, text=True, bufsize=1 << 20)
    kept, pvp = [], False
    for line in proc.stdout:
        i = line.index('"eventKind":"') + 13
        k = line[i:line.index('"', i)]
        if k in WANTED:
            kept.append((k, line))
            if k == "GAME_TICK_OBSERVED" and not pvp and '"opponent":{"kind":"PLAYER"' in line:
                pvp = True
    if proc.wait() != 0:
        raise RuntimeError(f"zstd failed on {rec}")
    out = []
    for k, line in kept:
        if pvp or k in ("SESSION_STARTED", "SESSION_ENDED"):
            r = json.loads(line)
            out.append((r["lastObservedGameTick"], r["monotonicNanos"], k, r["payload"]))
    return out, pvp


def fights_from(T, me, gap=15, min_hits=3):
    """pvp_fight.fights: windows of ticks with a tracked player opponent that exchanged at least min_hits attack splats."""
    spans = []
    for t in sorted(T):
        o = T[t].get("opp")
        if not o:
            continue
        if spans and spans[-1]["opp"] == o and t - spans[-1]["hi"] <= gap:
            spans[-1]["hi"] = t
        else:
            spans.append(dict(opp=o, lo=t, hi=t))
    return [s for s in spans
            if sum(1 for t in range(s["lo"], s["hi"] + 1) for h in T[t]["hits"] if h[0] in (me, s["opp"]) and h[2] in HIT_ATTACK_TYPES) >= min_hits]
# --------------------------------------------------------------------------------------------------------------------


def cache_path(rec):
    base = os.path.basename(rec)
    return os.path.join(CACHE_DIR, base[8:24] + "_" + base.split("_")[-1][:8] + f".v{SCHEMA}.json")


def extract(rec):
    """Compact per-recording record, cached as JSON."""
    base = os.path.basename(rec)
    cache = cache_path(rec)
    if os.path.exists(cache):
        with open(cache) as f:
            return json.load(f)
    events, pvp = load(rec)
    started = next((ns, p) for _, ns, k, p in events if k == "SESSION_STARTED")
    ended = next(((ns, p) for _, ns, k, p in events if k == "SESSION_ENDED"), None)
    start_ns = started[0]
    me = next((p["player"]["cleanName"] for _, _, k, p in events if k == "GAME_TICK_OBSERVED"), None)
    T = collections.defaultdict(lambda: dict(hits=[], opp=None))
    ticks = {}
    anims = []       # (tick, actor name, animation, actor world x, y) every player animation start
    chat = []        # (tick, type, message) non-SPAM
    menus = []       # (tick, option, target, origin)
    states = []      # (tick, game state)
    for t, ns, k, p in events:
        if k == "GAME_TICK_OBSERVED":
            pl = p["player"]
            ob = pl.get("opponent")
            opp = name(ob) if ob and ob["kind"] == "PLAYER" else None
            bar = None
            if opp:
                hb = ob["healthBar"]
                bar = [hb["ratio"], hb["scale"]] if hb["availability"] == "OBSERVED_RAW_BAR" else None
            T[t]["opp"] = opp
            wp = pl["worldPoint"]
            ticks[t] = [round((ns - start_ns) / 1e9, 3), pl["combatResources"]["hitpoints"]["boosted"], wp["x"], wp["y"], wp["plane"],
                        opp, bar, pl["animation"], pl.get("poseAnimation", -1), p.get("gameState")]
        elif k == "HITSPLAT_APPLIED_OBSERVED":
            T[t]["hits"].append((name(p["recipient"]), p["amount"], p["hitsplatType"]))
        elif k == "ANIMATION_OBSERVED" and p["animation"] != -1 and p["actor"]["kind"] == "PLAYER":
            wp = p["actor"]["worldPoint"]
            anims.append((t, name(p["actor"]), p["animation"], wp["x"], wp["y"]))
        elif k == "CHAT_MESSAGE_OBSERVED" and p["type"] != "SPAM":
            chat.append((t, p["type"], p["message"][:200]))
        elif k == "SEMANTIC_MENU_ACTION":
            menus.append((t, tag(p["option"]), tag(p["target"])[:40], p["origin"]))
        elif k == "GAME_STATE_OBSERVED":
            states.append((t, p["gameState"]))
    fights = fights_from(T, me)
    opp_names = {f["opp"] for f in fights}
    opp_track = []   # (tick, name, x, y, bar ratio, bar scale, clientDeadFlag, animation) of every fight opponent seen at a GAME_TICK observation
    for t, ns, k, p in events:
        if k == "ACTOR_STATE_OBSERVED" and p.get("observation") == "GAME_TICK" and p["actor"].get("cleanName") in opp_names:
            st = p["state"]
            hb = st.get("healthBar") or {}
            opp_track.append((t, p["actor"]["cleanName"], st["worldPoint"]["x"], st["worldPoint"]["y"], hb.get("ratio", -1), hb.get("scale", -1),
                              st.get("clientDeadFlag"), st.get("animation")))
    out = dict(
        file=base, start_utc=started[1]["startedAt"], end_utc=ended[1]["endedAt"] if ended else None,
        end_rel=round((ended[0] - start_ns) / 1e9, 3) if ended else None,
        pvp=pvp, me=me, first_tick=min(ticks) if ticks else None, last_tick=max(ticks) if ticks else None, n_ticks=len(ticks),
        ticks=[[t] + v for t, v in sorted(ticks.items())], anims=anims, chat=chat, menus=menus, states=states,
        fights=fights, opp_track=opp_track,
        hits=[[t, h[0], h[1], h[2]] for t in sorted(T) for h in T[t]["hits"]],
    )
    os.makedirs(CACHE_DIR, exist_ok=True)
    tmp = cache + f".tmp{os.getpid()}"
    with open(tmp, "w") as f:
        json.dump(out, f)
    os.replace(tmp, cache)
    return out



TICK_S = 0.6
SESSION_GAP_S = 600.0     # a gap between fights longer than this is not an in-session delay
MERGE_GAP = 15            # same opponent spans closer than this are one fight (pvp_fight.fights gap)
ARENA_BOX = (3340, 3460, 4000, 4120)   # x0, x1, y0, y1 of the instanced arena seen in the recordings
JUMP_TILES = 4            # a one tick position change of at least this many tiles is a teleport style move (running is 2)
DEATH_ANIM = 836
TABLET_ANIMS = {4069, 4071}
UNAMBIGUOUS_SPECS = {11275, 1667, 7644, 7514, 1062}
SPEC_COST = {11275: 50, 1667: 50, 7644: 50, 7514: 50, 7515: 50, 1062: 25, 1060: 25, 3300: 25, 3299: 25}
SPEC_REGEN_TICKS, SPEC_REGEN_AMOUNT = 50, 10     # rollout_export REGEN_TICKS / REGEN_AMOUNT
BLOCK_RE = re.compile(r"cannot (leave|teleport)")
END_WINDOW_BEFORE, END_WINDOW_AFTER = 25, 12


def epoch(rec):
    return datetime.datetime.fromisoformat(rec["start_utc"].replace("Z", "+00:00")).timestamp()


def instance(rec):
    return re.match(r"session_\w+\.\d+Z_(\w+\.\d+)_", rec["file"]).group(1)


def merge_fights(fights):
    out = []
    for f in sorted(fights, key=lambda f: f["lo"]):
        if out and out[-1]["opp"] == f["opp"] and f["lo"] - out[-1]["hi"] <= MERGE_GAP:
            out[-1] = dict(opp=f["opp"], lo=out[-1]["lo"], hi=f["hi"])
        else:
            out.append(dict(f))
    return out


def own_events(rec):
    """Our deaths, escapes (teleport tablet break or Exit Arena) and Enter Arena clicks of one recording, as tick-stamped dicts."""
    me = rec["me"]
    tk = {row[0]: row for row in rec["ticks"]}
    ts = sorted(tk)
    jumps = {}
    for a, b in zip(ts, ts[1:]):
        if b - a <= 3:
            d = max(abs(tk[b][3] - tk[a][3]), abs(tk[b][4] - tk[a][4]))
            if d >= JUMP_TILES:
                jumps[b] = d
    states = collections.defaultdict(list)
    for t, g in rec["states"]:
        states[t].append(g)
    # deaths
    d836 = sorted({t for t, n, a, x, y in rec["anims"] if n == me and a == DEATH_ANIM})
    deaths = []
    for t in d836:
        if deaths and t - deaths[-1]["anim"] <= 15:
            continue
        zero = [u for u in range(t - 10, t + 1) if u in tk and tk[u][2] == 0]
        deaths.append(dict(anim=t, tick=min(zero) if zero else t))
    # escapes
    esc = []
    exits = sorted(m[0] for m in rec["menus"] if m[1] == "Exit" and m[2] == "Arena")
    bursts = []
    for c in exits:
        if bursts and c - bursts[-1][-1] <= 15:
            bursts[-1].append(c)
        else:
            bursts.append([c])
    for b in bursts:
        hit = [j for j in sorted(jumps) if b[0] <= j <= b[-1] + 8]
        blocked = [c for c in rec["chat"] if b[0] - 1 <= c[0] <= b[-1] + 8 and BLOCK_RE.search(c[2])]
        if hit:
            commit = max(c for c in b if c <= hit[0])
            esc.append(dict(kind="exit", tick=commit, first=b[0], landing=hit[0], blocked=len(blocked), jump=jumps[hit[0]]))
        elif not blocked:
            esc.append(dict(kind="exit", tick=b[-1], first=b[0], landing=None, blocked=0, jump=None, unconfirmed=True))
    anim_t = sorted({t for t, n, a, x, y in rec["anims"] if n == me and a in TABLET_ANIMS})
    groups = []
    for t in anim_t:
        if groups and t - groups[-1][-1] <= 6:
            groups[-1].append(t)
        else:
            groups.append([t])
    breaks = sorted(m[0] for m in rec["menus"] if m[1] == "Break" and m[2] == "Teleport to house")
    prev_end = -10**9
    for g in groups:
        # a blocked break plays no animation, so the animation itself is the success evidence; the landing is a bonus
        land = [u for u in range(g[0], g[0] + 9) if jumps.get(u, 0) >= 30 or "LOADING" in states.get(u, [])]
        first = min([c for c in breaks if max(prev_end, g[0] - 30) < c <= g[0]], default=g[0])
        blocked = [c for c in rec["chat"] if first - 1 <= c[0] <= g[0] and BLOCK_RE.search(c[2])]
        esc.append(dict(kind="tablet", tick=g[0], first=first, landing=land[0] if land else None, blocked=len(blocked), jump=None))
        prev_end = g[0]
    esc.sort(key=lambda e: e["tick"])
    enters = []
    for c in sorted(m[0] for m in rec["menus"] if m[1] == "Enter" and m[2] == "Arena"):
        if not enters or c - enters[-1] > 15:
            enters.append(c)
    return dict(deaths=deaths, esc=esc, enters=enters, jumps=jumps)


def classify(rec, ev, f):
    """Outcome of one merged fight with the evidence ticks. Priority: death, kill, our escape, his teleport, other."""
    me, opp, lo, hi = rec["me"], f["opp"], f["lo"], f["hi"]
    last = rec["last_tick"]
    lim = min(hi + END_WINDOW_AFTER, last)
    out = dict(outcome="other", end_tick=hi, evidence="")
    death = next((d for d in ev["deaths"] if lo <= d["tick"] <= lim), None)
    his = [(t, a) for t, n, a, x, y in rec["anims"] if n == opp and lo <= t <= lim]
    dead_flag = [t for (t, n, x, y, r, s, flag, an) in rec["opp_track"] if n == opp and lo <= t <= hi + 6 and (flag or an == DEATH_ANIM)]
    kill_chat = [c for c in rec["chat"] if lo <= c[0] <= hi + 40 and "Target killed" in c[2] and opp in c[2].replace("\xa0", " ")]
    he836 = [t for t, a in his if a == DEATH_ANIM and t <= hi + 6]
    esc = [e for e in ev["esc"] if hi - END_WINDOW_BEFORE <= e["tick"] <= lim and e["tick"] >= lo]
    if death:
        out.update(outcome="death", end_tick=death["tick"], evidence="our hp0/anim836")
    elif he836 or dead_flag or kill_chat:
        out.update(outcome="kill", end_tick=min(he836 + dead_flag) if (he836 or dead_flag) else hi,
                   evidence="opp anim836" if he836 else "opp deadflag" if dead_flag else "kill chat")
    elif esc:
        e = esc[-1]
        out.update(outcome="our_tp" if e["kind"] == "tablet" else "our_exit", end_tick=e["tick"], evidence=e["kind"], esc=e)
    else:
        tele = [(t, a) for t, a in his if a in (4069, 4071, 714, 3864) and hi - 6 <= t <= lim]
        if tele:
            out.update(outcome="his_tp", end_tick=tele[0][0], evidence=f"opp anim {tele[0][1]}")
    return out


def list_recordings():
    return sorted(glob.glob(os.path.join(REC_DIR, "session_202610*.jsonl.zst")))


def extract_all(jobs=3):
    recs = list_recordings()
    if all(os.path.exists(cache_path(r)) for r in recs):
        return [extract(r) for r in recs]
    with multiprocessing.Pool(jobs) as pool:
        return pool.map(extract, recs, chunksize=1)


MIN_REAL_TICKS = 15       # rollout_export marks fights shorter than this as `short`; they are target switch artefacts in crowds
OUR_SPECS = {11275, 1667, 7644, 7514, 1062, 7515, 1060, 3300, 3299}
CONSIST_S = 5.0


def rel_time(tk_rel, ks, t):
    """Seconds since SESSION_STARTED of tick t; a tick without a GAME_TICK row is placed 0.6 s per tick from the nearest row."""
    if t in tk_rel:
        return tk_rel[t]
    i = bisect.bisect_left(ks, t)
    cands = ks[max(i - 1, 0):i + 1]
    k = min(cands, key=lambda c: abs(c - t))
    return tk_rel[k] + (t - k) * TICK_S


class Rec:
    """One PvP recording with its tick clock, own events and merged fights."""

    def __init__(self, raw):
        self.raw = raw
        self.file = raw["file"]
        self.key = raw["file"][8:23]
        self.inst = instance(raw)
        self.epoch = epoch(raw)
        self.me = raw["me"]
        self.tk = {row[0]: row for row in raw["ticks"]}
        self.tk_rel = {t: row[1] for t, row in self.tk.items()}
        self.ks = sorted(self.tk)
        self.first, self.last = self.ks[0], self.ks[-1]
        self.ev = own_events(raw)

    def wall(self, t):
        return self.epoch + rel_time(self.tk_rel, self.ks, t)


def venue(rec, t):
    x, y = rec.tk[t][3], rec.tk[t][4]
    return "arena" if ARENA_BOX[0] <= x <= ARENA_BOX[1] and ARENA_BOX[2] <= y <= ARENA_BOX[3] else "open"


def spec_stats(rec, f, end):
    """Spec anims of both sides from the fight start to `end`: ours, his, and his estimated energy at `end` (100 at the start, cost per spec, +10 per 50 ticks)."""
    raw = rec.raw
    ours = [(t, a) for t, n, a, x, y in raw["anims"] if n == rec.me and a in OUR_SPECS and f["lo"] <= t <= end]
    his = [(t, a) for t, n, a, x, y in raw["anims"] if n == f["opp"] and a in SPEC_COST and f["lo"] <= t <= end]
    energy = 100.0
    last = f["lo"]
    for t, a in sorted(his):
        energy = min(100.0, energy + SPEC_REGEN_AMOUNT * ((t - last) // SPEC_REGEN_TICKS))
        last = t
        energy -= SPEC_COST[a]
    energy = min(100.0, energy + SPEC_REGEN_AMOUNT * ((end - last) // SPEC_REGEN_TICKS))
    return ours, his, energy


def build_fights(recs):
    out = []
    for rec in recs:
        for f in merge_fights(rec.raw["fights"]):
            c = classify(rec.raw, rec.ev, f)
            e = c.get("esc")
            end = c["end_tick"]
            fr = dict(rec=rec, opp=f["opp"], lo=f["lo"], hi=f["hi"], ticks=f["hi"] - f["lo"] + 1, outcome=c["outcome"], end_tick=end,
                      evidence=c["evidence"], esc=e, venue=venue(rec, f["lo"]))
            fr["t_lo"] = rec.wall(f["lo"])
            fr["t_hi"] = rec.wall(f["hi"])
            fr["t_end"] = rec.wall(end)
            fr["real"] = fr["ticks"] >= MIN_REAL_TICKS
            ours, his, energy = spec_stats(rec, f, end)
            fr.update(our_specs=len(ours), his_specs=len(his), his_energy=energy, last_our_spec=max([t for t, a in ours], default=None))
            out.append(fr)
    out.sort(key=lambda f: f["t_lo"])
    return out


def ticks_between(r1, t1, r2, t2):
    """Game ticks from (recording 1, tick t1) to (recording 2, tick t2) and the clock used: tick labels when both recordings come from one client
    instance and the labels agree with the wall clock within CONSIST_S, else wall seconds / 0.6."""
    wall = r2.wall(t2) - r1.wall(t1)
    if r1 is r2:
        return t2 - t1, "tick"
    if r1.inst == r2.inst and abs((t2 - t1) * TICK_S - wall) <= CONSIST_S:
        return t2 - t1, "tick"
    return wall / TICK_S, "wall"


def pct(vals, q):
    v = sorted(vals)
    if not v:
        return float("nan")
    k = (len(v) - 1) * q
    lo = int(k)
    hi = min(lo + 1, len(v) - 1)
    return v[lo] + (v[hi] - v[lo]) * (k - lo)


def row(label, vals):
    if not vals:
        return f"{label:<44} n=0"
    return (f"{label:<44} n={len(vals):<4} min={min(vals):>7.1f} p10={pct(vals, .1):>7.1f} p25={pct(vals, .25):>7.1f} med={pct(vals, .5):>7.1f} "
            f"p75={pct(vals, .75):>7.1f} p90={pct(vals, .9):>7.1f} max={max(vals):>8.1f} mean={sum(vals) / len(vals):>7.1f}")


def fisher_two_sided(a, b, c, d):
    """Exact two sided Fisher test for the table [[a, b], [c, d]]."""
    n1, n2, k = a + b, c + d, a + c
    n = n1 + n2

    def p(x):
        return math.comb(n1, x) * math.comb(n2, k - x) / math.comb(n, k)
    p0 = p(a)
    return sum(p(x) for x in range(max(0, k - n2), min(n1, k) + 1) if p(x) <= p0 * (1 + 1e-9))


def link(fights):
    """Pair every real fight with the next real fight and the delay from its end event and from its last tracked tick."""
    real = [f for f in fights if f["real"]]
    pairs = []
    for a, b in zip(real, real[1:]):
        d_evt, clk = ticks_between(a["rec"], a["end_tick"], b["rec"], b["lo"])
        d_hi, _ = ticks_between(a["rec"], a["hi"], b["rec"], b["lo"])
        wall_gap = b["t_lo"] - a["t_hi"]
        pairs.append(dict(a=a, b=b, d_evt=d_evt, d_hi=d_hi, clock=clk, wall_gap=wall_gap, in_session=wall_gap <= SESSION_GAP_S and b["t_lo"] >= a["t_hi"]))
    return real, pairs


def sessions_of(real, pairs):
    sess, cur = [], [real[0]]
    for p in pairs:
        if p["in_session"]:
            cur.append(p["b"])
        else:
            sess.append(cur)
            cur = [p["b"]]
    sess.append(cur)
    return sess


def stamp(f):
    return f["rec"].key


def list_samples(pairs):
    esc_out = ("our_tp", "our_exit")
    groups = [("R tablet", lambda p: p["a"]["outcome"] == "our_tp"), ("R Exit Arena", lambda p: p["a"]["outcome"] == "our_exit"),
              ("M tablet same", lambda p: p["a"]["outcome"] == "our_tp" and p["b"]["opp"] == p["a"]["opp"]),
              ("M exit same", lambda p: p["a"]["outcome"] == "our_exit" and p["b"]["opp"] == p["a"]["opp"]),
              ("R' new opponent", lambda p: p["a"]["outcome"] in esc_out and p["b"]["opp"] != p["a"]["opp"]),
              ("D death", lambda p: p["a"]["outcome"] == "death"), ("S kill", lambda p: p["a"]["outcome"] == "kill"), ("S his_tp", lambda p: p["a"]["outcome"] == "his_tp")]
    print("=" * 132)
    print("SAMPLES (in-session, ticks, sorted)")
    for nm, pred in groups:
        print(f"{nm:<16}", " ".join(f"{p['d_evt']:.0f}" for p in sorted((p for p in pairs if p["in_session"] and pred(p)), key=lambda p: p["d_evt"])))


def main():
    t0 = datetime.datetime.now()
    raws = extract_all()
    print(f"# extracted {len(raws)} recordings ({sum(1 for r in raws if r['fights'])} with a tracked player fight) in {datetime.datetime.now() - t0}")
    recs = sorted((Rec(r) for r in raws if r["fights"]), key=lambda r: r.epoch)
    fights = build_fights(recs)
    real, pairs = link(fights)
    report(recs, fights, real, pairs)
    if "--list" in sys.argv:
        list_samples(pairs)
    print(f"# total {datetime.datetime.now() - t0}")


def utc(ts):
    return datetime.datetime.fromtimestamp(ts, datetime.timezone.utc)


BUCKETS = [(1, "<=1m"), (2, "<=2m"), (5, "<=5m"), (10, "<=10m"), (30, "<=30m"), (60, "<=1h"), (240, "<=4h"), (10**9, ">4h")]


def bucket_row(label, gaps_s):
    c = collections.Counter()
    for g in gaps_s:
        c[next(name_ for lim, name_ in BUCKETS if g <= lim * 60)] += 1
    return f"{label:<30} n={len(gaps_s):<4} " + " ".join(f"{name_}:{c[name_]}" for _, name_ in BUCKETS)


def report(recs, fights, real, pairs):
    P = print
    nxt = {id(p["a"]): p for p in pairs}
    esc_out = ("our_tp", "our_exit")
    P("=" * 132)
    P("A. CORPUS")
    span = (min(r.epoch for r in recs), max(r.epoch for r in recs))
    P(f"recordings with a tracked player fight: {len(recs)} ({utc(span[0]):%Y-%m-%d} .. {utc(span[1]):%Y-%m-%d} UTC), client instances {len({r.inst for r in recs})}; "
      f"recorded time of these recordings {sum(r.last - r.first + 1 for r in recs) * TICK_S / 3600:.2f} h ({sum(r.last - r.first + 1 for r in recs)} ticks)")
    P(f"merged fights {len(fights)}, real (>= {MIN_REAL_TICKS} ticks) {len(real)}, short artefacts dropped {len(fights) - len(real)}; venue by our position at fight start: "
      + ", ".join(f"{v} {sum(1 for f in real if f['venue'] == v)}" for v in ("arena", "open")) + f"; distinct opponents {len({f['opp'] for f in real})}")
    P(f"consecutive real-fight pairs {len(pairs)}: in-session (wall gap <= {SESSION_GAP_S:.0f} s) {sum(p['in_session'] for p in pairs)} "
      f"(same recording {sum(p['a']['rec'] is p['b']['rec'] for p in pairs if p['in_session'])}, across recordings {sum(p['a']['rec'] is not p['b']['rec'] for p in pairs if p['in_session'])}), longer gaps {sum(not p['in_session'] for p in pairs)}; "
      f"delay clock: tick labels {sum(p['clock'] == 'tick' for p in pairs)} (same recording, or same client instance with labels within {CONSIST_S:.0f} s of the wall clock), wall seconds/0.6 {sum(p['clock'] == 'wall' for p in pairs)}")

    P("=" * 132)
    P("B. OUTCOME MIX of real fights (priority: our death > his death > our escape > his teleport anim > other)")
    for v in (None, "arena", "open"):
        sub = [f for f in real if v is None or f["venue"] == v]
        c = collections.Counter(f["outcome"] for f in sub)
        P(f"  {v or 'all':<6} n={len(sub):<4} " + "  ".join(f"{k}={c[k]} ({100 * c[k] / len(sub):.0f}%)" for k in ("kill", "death", "our_tp", "our_exit", "his_tp", "other")))
    ev = collections.Counter((f["outcome"], f["evidence"]) for f in real)
    P("  evidence: " + "; ".join(f"{k[0]}/{k[1] or '-'}={v}" for k, v in sorted(ev.items())))
    P(f"  our escapes that were blocked at least once before they worked: {sum(1 for f in real if f['esc'] and f['esc']['blocked'])} of {sum(f['outcome'] in esc_out for f in real)}; "
      f"'cannot teleport/leave ... special attack' messages in these recordings: {sum(1 for r in recs for c in r.raw['chat'] if 'special attack' in c[2] and BLOCK_RE.search(c[2]))}; "
      f"'cannot leave the crater for N seconds after engaging in combat': {sum(1 for r in recs for c in r.raw['chat'] if 'crater' in c[2])}")
    P(f"  unconfirmed Exit Arena clicks (no jump seen, recording ended or tick row missing) counted as escapes: {sum(1 for f in real if f['esc'] and f['esc'].get('unconfirmed'))}")
    P(f"  deaths seen in these recordings outside a real fight: {sum(len(r.ev['deaths']) for r in recs) - sum(f['outcome'] == 'death' for f in real)} "
      f"(of {sum(len(r.ev['deaths']) for r in recs)})")

    P("=" * 132)
    P("C. FIGHT LENGTH in game ticks (start lo = first tick the opponent is our interaction target in a window with >= 3 attack splats; ticks = hi - lo + 1, hi = last tracked tick)")
    P(row("all real fights", [f["ticks"] for f in real]))
    for v in ("arena", "open"):
        P(row(f"  venue {v}", [f["ticks"] for f in real if f["venue"] == v]))
    for o in ("kill", "death", "our_tp", "our_exit", "his_tp", "other"):
        P(row(f"  outcome {o}", [f["ticks"] for f in real if f["outcome"] == o]))
    P(row("end = outcome event tick (end_tick - lo + 1)", [f["end_tick"] - f["lo"] + 1 for f in real]))
    P(row("  same, only classified ends (not 'other')", [f["end_tick"] - f["lo"] + 1 for f in real if f["outcome"] != "other"]))
    P(row("fights incl. short artefacts (ticks)", [f["ticks"] for f in fights]))
    P(f"  contributing recordings: {len({stamp(f) for f in real})}; total fight ticks {sum(f['ticks'] for f in real)}")

    P("=" * 132)
    P(f"D. DELAYS in game ticks = start (lo) of the next real fight minus the end event of this one; samples are pairs with wall gap <= {SESSION_GAP_S:.0f} s")

    def show(label, pred, key="d_evt", extra=True):
        sel = [p for p in pairs if p["in_session"] and pred(p)]
        vals = [p[key] for p in sel]
        long_ = [p for p in pairs if not p["in_session"] and pred(p)]
        P(row(label, vals))
        if extra and (sel or long_):
            P(f"{'':<44} recordings {len({stamp(p['a']) for p in sel} | {stamp(p['b']) for p in sel})}; clock tick={sum(p['clock'] == 'tick' for p in sel)} wall={sum(p['clock'] == 'wall' for p in sel)}; "
              f"excluded (> 10 min): {len(long_)}")
        return vals
    esc = lambda p: p["a"]["outcome"] in esc_out
    show("R  our escape (tablet or Exit Arena) -> next fight", esc)
    show("   tablet teleport", lambda p: p["a"]["outcome"] == "our_tp")
    show("   Exit Arena", lambda p: p["a"]["outcome"] == "our_exit")
    show("   venue arena", lambda p: esc(p) and p["a"]["venue"] == "arena", extra=False)
    show("   venue open", lambda p: esc(p) and p["a"]["venue"] == "open", extra=False)
    show("   same recording (recorder kept running)", lambda p: esc(p) and p["a"]["rec"] is p["b"]["rec"], extra=False)
    show("   across recordings", lambda p: esc(p) and p["a"]["rec"] is not p["b"]["rec"], extra=False)
    show("M  escape -> SAME opponent fights us again", lambda p: esc(p) and p["b"]["opp"] == p["a"]["opp"])
    show("   tablet", lambda p: p["a"]["outcome"] == "our_tp" and p["b"]["opp"] == p["a"]["opp"], extra=False)
    show("   Exit Arena", lambda p: p["a"]["outcome"] == "our_exit" and p["b"]["opp"] == p["a"]["opp"], extra=False)
    show("R' escape -> NEW opponent", lambda p: esc(p) and p["b"]["opp"] != p["a"]["opp"])
    show("   tablet", lambda p: p["a"]["outcome"] == "our_tp" and p["b"]["opp"] != p["a"]["opp"], extra=False)
    show("   Exit Arena", lambda p: p["a"]["outcome"] == "our_exit" and p["b"]["opp"] != p["a"]["opp"], extra=False)
    show("D  our death -> next fight", lambda p: p["a"]["outcome"] == "death")
    show("S  our kill -> next fight (any opponent)", lambda p: p["a"]["outcome"] == "kill")
    show("S  his teleport anim -> next fight", lambda p: p["a"]["outcome"] == "his_tp")
    show("S* kill or his teleport -> NEW opponent", lambda p: p["a"]["outcome"] in ("kill", "his_tp") and p["b"]["opp"] != p["a"]["opp"], extra=False)
    show("O  unclassified end -> next fight", lambda p: p["a"]["outcome"] == "other")
    show("   any end: idle gap hi -> next lo", lambda p: True, key="d_hi", extra=False)
    P("  right tail: all pairs by wall gap between the end of the fight and the next fight start (no 10 min cut), minutes:")
    for nm, pred in (("escape", esc), ("  tablet", lambda p: p["a"]["outcome"] == "our_tp"), ("  exit", lambda p: p["a"]["outcome"] == "our_exit"),
                     ("death", lambda p: p["a"]["outcome"] == "death"), ("kill", lambda p: p["a"]["outcome"] == "kill"),
                     ("his_tp", lambda p: p["a"]["outcome"] == "his_tp"), ("other", lambda p: p["a"]["outcome"] == "other")):
        P("   " + bucket_row(nm, [p["wall_gap"] for p in pairs if pred(p)]))
    for nm, o in (("D  death", "death"), ("S  kill", "kill")):
        allv = sorted((p["d_evt"], p["clock"]) for p in pairs if p["a"]["outcome"] == o)
        P(f"  {nm}: every observed next-fight delay in ticks (incl. > 10 min; wall-clock ones marked w): " + " ".join(f"{v:.0f}{'w' if c == 'wall' else ''}" for v, c in allv))
    P(f"  pairs whose next fight is in a LATER client instance (wall clock only): {sum(p['a']['rec'].inst != p['b']['rec'].inst for p in pairs)}")

    P("=" * 132)
    P("E. ARENA DECOMPOSITION and arrival, ticks")
    lobby, queue = [], []
    for p in pairs:
        a, b = p["a"], p["b"]
        if not p["in_session"] or a["outcome"] != "our_exit":
            continue
        ent = [e for e in b["rec"].ev["enters"] if e < b["lo"] and (b["rec"] is not a["rec"] or e > a["end_tick"])]
        if ent:
            e = max(ent)
            queue.append(b["lo"] - e)
            if b["rec"] is a["rec"]:
                lobby.append(e - a["end_tick"])
    P(row("Enter Arena click -> next fight start (queue, walk to target)", queue))
    P(row("Exit Arena -> Enter Arena click (lobby: pool, bank, gear), same recording", lobby))
    first = {}
    for f in real:
        first.setdefault(id(f["rec"]), f)
    P(row("recording start -> first real fight lo", [f["lo"] - f["rec"].first for f in first.values()]))

    P("=" * 132)
    P(f"F. SESSIONS = chains of real fights whose consecutive wall gaps are <= {SESSION_GAP_S:.0f} s. Inner span = first fight start to last fight end; time before the first and after the last fight is censored")
    sess = sessions_of(real, pairs)
    spans = [(s[-1]["t_hi"] - s[0]["t_lo"]) / TICK_S for s in sess]
    P(row("fights per session", [len(s) for s in sess]))
    P(row("  sessions with >= 2 fights", [len(s) for s in sess if len(s) >= 2]))
    P(row("ticks per session (inner span)", spans))
    P(row("  sessions with >= 2 fights", [sp for s, sp in zip(sess, spans) if len(s) >= 2]))
    P(row("  sessions with >= 3 fights", [sp for s, sp in zip(sess, spans) if len(s) >= 3]))
    P(row("fights per hour, sessions with >= 3 fights", [len(s) / (sp * TICK_S / 3600) for s, sp in zip(sess, spans) if len(s) >= 3]))
    P(row("kills per session", [sum(f["outcome"] == "kill" for f in s) for s in sess]))
    P(row("deaths per session", [sum(f["outcome"] == "death" for f in s) for s in sess]))
    P(row("escapes per session", [sum(f["outcome"] in esc_out for f in s) for s in sess]))
    tot = sum(spans)
    in_fight = sum(f["ticks"] for f in real)
    P(f"  sessions {len(sess)} ({sum(len(s) >= 2 for s in sess)} with >= 2 fights), fights {len(real)}, total inner-span ticks {tot:.0f} ({tot * TICK_S / 3600:.2f} h); pooled fights/hour over inner spans = {len(real) / (tot * TICK_S / 3600):.1f}; "
      f"pooled fights/hour over sessions with >= 2 fights = {sum(len(s) for s in sess if len(s) >= 2) / (sum(sp for s, sp in zip(sess, spans) if len(s) >= 2) * TICK_S / 3600):.1f}")
    P(f"  kills {sum(f['outcome'] == 'kill' for f in real)}, deaths {sum(f['outcome'] == 'death' for f in real)}, escapes {sum(f['outcome'] in esc_out for f in real)}, his teleport {sum(f['outcome'] == 'his_tp' for f in real)}, other {sum(f['outcome'] == 'other' for f in real)}; "
      f"time in fight / inner-span time = {in_fight / tot:.2f}")
    P(row("idle gap between consecutive fights, in-session (hi -> lo)", [p["d_hi"] for p in pairs if p["in_session"]]))
    P(row("idle gap between consecutive fights, > 10 min (wall ticks)", [p["wall_gap"] / TICK_S for p in pairs if not p["in_session"]]))
    P("   " + bucket_row("all consecutive pairs", [p["wall_gap"] for p in pairs]))
    for lim in (300, 600, 1200, 1800, 3600):
        chains, cur = [], [real[0]]
        for p in pairs:
            if p["wall_gap"] <= lim and p["b"]["t_lo"] >= p["a"]["t_hi"]:
                cur.append(p["b"])
            else:
                chains.append(cur)
                cur = [p["b"]]
        chains.append(cur)
        sz = [len(c) for c in chains]
        P(f"  sensitivity to the session gap threshold {lim // 60:>2} min: sessions {len(chains):<3} fights/session median {pct(sz, .5):.1f} mean {sum(sz) / len(sz):.2f} max {max(sz)}; "
          f"sessions with >= 2 fights {sum(z >= 2 for z in sz)}")
    by_day = collections.defaultdict(list)
    for s in sess:
        by_day[utc(s[0]["t_lo"]).strftime("%m-%d")].append(s)
    P("  per UTC day: " + "; ".join(f"{d}: {len(v)} sess/{sum(len(s) for s in v)} fights" for d, v in sorted(by_day.items())))
    P("  fights per session, in time order: " + " ".join(str(len(s)) for s in sess))
    P("  note: the Oct 4-9 recordings are one arena stay or one wilderness bout each (the recorder restarts), so a session there is a chain of recordings; Oct 10-13 recordings are continuous (many fights each).")

    P("=" * 132)
    P("G. REMATCH after our escape (tablet or Exit Arena); denominators use escapes whose next real fight is within 10 min")
    tot_esc = [f for f in real if f["outcome"] in esc_out]
    idx = {id(f): i for i, f in enumerate(real)}

    def tally(sub):
        s_ = n_ = c_ = 0
        for f in sub:
            p = nxt.get(id(f))
            if p is None or not p["in_session"]:
                c_ += 1
            elif p["b"]["opp"] == f["opp"]:
                s_ += 1
            else:
                n_ += 1
        return s_, n_, c_

    def line(label, sub):
        s_, n_, c_ = tally(sub)
        P(f"  {label:<34} escapes={len(sub):<4} next fight same={s_:<3} new={n_:<3} none within 10 min={c_:<3}" + (f" rematch rate {100 * s_ / (s_ + n_):.1f}%" if s_ + n_ else ""))
    line("all escapes", tot_esc)
    line("  tablet", [f for f in tot_esc if f["outcome"] == "our_tp"])
    line("  Exit Arena", [f for f in tot_esc if f["outcome"] == "our_exit"])
    line("  venue arena", [f for f in tot_esc if f["venue"] == "arena"])
    line("  venue open", [f for f in tot_esc if f["venue"] == "open"])
    later = 0
    for f in tot_esc:
        p = nxt.get(id(f))
        if p and p["in_session"] and p["b"]["opp"] != f["opp"]:
            j = idx[id(p["b"])]
            k = j
            while k < len(real) and (k == j or nxt[id(real[k - 1])]["in_session"]):
                if real[k]["opp"] == f["opp"]:
                    later += 1
                    break
                k += 1
    P(f"  same opponent comes back later in the same session although the immediate next fight was a new opponent: {later}")
    sr = [p for p in pairs if esc(p) and p["in_session"] and p["b"]["opp"] == p["a"]["opp"]]
    P(f"  rematches in the same recording {sum(p['a']['rec'] is p['b']['rec'] for p in sr)}, in the next recording {sum(p['a']['rec'] is not p['b']['rec'] for p in sr)}")
    order = sorted(real, key=lambda f: f["t_lo"])
    horizon = {"1h": 3600, "24h": 86400, "ever": 10**12}
    for h, sec in horizon.items():
        P(f"  same opponent fights us again within {h} of the escape (any later fight, session or not): " + ", ".join(
            f"{lab} {sum(any(g['opp'] == f['opp'] and 0 < g['t_lo'] - f['t_end'] <= sec for g in order) for f in sub)}/{len(sub)}"
            for lab, sub in (("all", tot_esc), ("tablet", [f for f in tot_esc if f["outcome"] == "our_tp"]), ("exit", [f for f in tot_esc if f["outcome"] == "our_exit"]))))
    runs, cur = [], 1
    for p in pairs:
        if p["in_session"] and p["b"]["opp"] == p["a"]["opp"]:
            cur += 1
        else:
            runs.append(cur)
            cur = 1
    runs.append(cur)
    P(row("consecutive in-session fights vs the same opponent (run length)", runs))

    P("=" * 132)
    P("H. RUDE TELEPORTS: our escape after we used our special attack while he still had spec energy for his own")
    P("  our spec = a SPECS animation by us (11275 VW, 1667 maul observed) between fight start and our escape tick. His energy is approximated: 100 at the fight start (his real start energy is unobserved),")
    P("  minus 50 per spec animation of his seen (25 for the dds/mace/dagger animations), +10 per 50 ticks, evaluated at our escape tick. He 'still has energy' = estimate >= 50.")
    cats = [("rude_strict: we specced, he never did", lambda f: f["our_specs"] >= 1 and f["his_specs"] == 0),
            ("rude_loose: we specced, his est. energy >= 50", lambda f: f["our_specs"] >= 1 and f["his_energy"] >= 50),
            ("rude_recent: loose and our last spec <= 25 t before", lambda f: f["our_specs"] >= 1 and f["his_energy"] >= 50 and f["end_tick"] - f["last_our_spec"] <= 25),
            ("rude_instant: loose and our last spec <= 12 t before", lambda f: f["our_specs"] >= 1 and f["his_energy"] >= 50 and f["end_tick"] - f["last_our_spec"] <= 12),
            ("we specced, his energy spent (< 50)", lambda f: f["our_specs"] >= 1 and f["his_energy"] < 50),
            ("we never specced", lambda f: f["our_specs"] == 0)]
    for nm, pred in cats:
        sub = [f for f in tot_esc if pred(f)]
        line(nm, sub)
        dl = [nxt[id(f)]["d_evt"] for f in sub if id(f) in nxt and nxt[id(f)]["in_session"]]
        if dl:
            P(row("      R after these escapes", dl))
    for nm, rude in (("strict", cats[0][1]), ("loose", cats[1][1]), ("recent", cats[2][1]), ("instant", cats[3][1])):
        a1, b1, _ = tally([f for f in tot_esc if rude(f)])
        a2, b2, _ = tally([f for f in tot_esc if not rude(f)])
        if a1 + b1 and a2 + b2:
            P(f"  rematch after rude_{nm}: {a1}/{a1 + b1} = {100 * a1 / (a1 + b1):.0f}%   vs all other escapes: {a2}/{a2 + b2} = {100 * a2 / (a2 + b2):.0f}%   Fisher exact two-sided p = {fisher_two_sided(a1, b1, a2, b2):.3f}")
    for h, sec in horizon.items():
        def again(sub):
            return sum(any(g["opp"] == f["opp"] and 0 < g["t_lo"] - f["t_end"] <= sec for g in order) for f in sub)
        rs = [f for f in tot_esc if cats[1][1](f)]
        ot = [f for f in tot_esc if not cats[1][1](f)]
        P(f"  any later fight vs him within {h}: rude_loose {again(rs)}/{len(rs)}, other escapes {again(ot)}/{len(ot)}")
    P(row("ticks from our last spec anim to our escape, escapes after a spec", [f["end_tick"] - f["last_our_spec"] for f in tot_esc if f["our_specs"] >= 1]))
    P(f"  our spec count per escape: " + ", ".join(f"{k} specs: {sum(f['our_specs'] == k for f in tot_esc)}" for k in range(0, 4)) + f", >=4: {sum(f['our_specs'] >= 4 for f in tot_esc)}")

    P("=" * 132)
    P("I. CONTRIBUTING RECORDINGS (stamp = recording start UTC, [real fights]); every recording below contributes to C and F; R/M/D/S use the recordings of both fights of each pair (see /tmp/dm_samples.tsv)")
    per = collections.Counter(stamp(f) for f in real)
    P(" ".join(f"{k}[{v}]" for k, v in sorted(per.items())))
    P(f"  recordings of October without a tracked player opponent (not PvP): {168 - len(recs)}; the 1177 July-September recordings: 0 with a player opponent (grep prefilter).")
    dump_samples(pairs)


def dump_samples(pairs, path="/tmp/dm_samples.tsv"):
    with open(path, "w") as fh:
        fh.write("a_rec\ta_opp\ta_outcome\ta_end_tick\tb_rec\tb_opp\tb_lo\tdelay_ticks\tdelay_hi\tclock\tin_session\n")
        for p in pairs:
            a, b = p["a"], p["b"]
            fh.write("\t".join(map(str, [a["rec"].key, a["opp"], a["outcome"], a["end_tick"], b["rec"].key, b["opp"], b["lo"], round(p["d_evt"], 1), round(p["d_hi"], 1), p["clock"], p["in_session"]])) + "\n")
    print(f"# samples written to {path}")


if __name__ == "__main__":
    main()
