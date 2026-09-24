#!/usr/bin/env python3
"""Mine distribution-matched behavior rates from SPECTATOR_COMBAT_V1 archives.

Reads every session_*.zst under --recordings, segments each archive into
fights (maximal tick runs with a stable visible fighter pair), and derives
per-mille sample rolls for the RISKFIGHT_HUMANLIKE script branch.

Spectator data has no inputs by construction
(unknown=EXACT_HP_..._INPUTS_CAUSALITY): drink shares eat anim 829 with no
ID split, so all 829s are joint consumes. Ratio bands avoid any base-HP
assumption; health-bar scale is normalized to 30 per record.

Anim meanings: 1658 tentacle, 2067 axe, 1665 maul ordinary, 1667 maul spec,
390 voidwaker ordinary, 1378 or 11275 voidwaker spec, 829 consume, 4069
teleport (4071 is its tail), 4410 veng, 836 death, 4177/424/1156 blocks,
808/819/824 idle/walk/run, -1 cleared.
"""

import argparse
import collections
import glob
import json
import os
import statistics
import subprocess
import sys

COMBAT_ANIMS = {1658, 2067, 1665, 1667, 390, 1378, 11275, 829}
WEAPON_ANIMS = {1658, 2067, 1665, 1667, 390, 1378, 11275}
CONSUME_ANIM = 829
TELEPORT_ANIM = 4069
TELEPORT_TAIL_ANIM = 4071
VENG_ANIM = 4410
VENG_SPOTANIM = 726
VW_SPEC_ANIMS = {1378, 11275}
MAUL_SPEC_ANIM = 1667
AXE_ANIM = 2067

BANDS = [(0, 10), (11, 15), (16, 19), (20, 24), (25, 30)]
BAND_NAMES = ["0-10", "11-15", "16-19", "20-24", "25-30"]


def band_of(r30):
    for (lo, hi), name in zip(BANDS, BAND_NAMES):
        if lo <= r30 <= hi:
            return name
    return None


def r30_of(health_bar):
    """Normalize an observed health bar to scale-30 ratio; None if unknown."""
    if not isinstance(health_bar, dict):
        return None
    if health_bar.get("availability") != "OBSERVED_RAW_BAR":
        return None
    try:
        ratio = health_bar["ratio"]
        scale = health_bar["scale"]
    except KeyError:
        return None
    if not isinstance(ratio, (int, float)) or not isinstance(scale, (int, float)):
        return None
    if scale <= 0:
        return None
    return int(round(ratio * 30.0 / scale))


def spotanim_ids(state):
    try:
        spots = state["effects"]["spotAnims"]
    except (KeyError, TypeError):
        return []
    if not isinstance(spots, list):
        return []
    return [s.get("id") for s in spots if isinstance(s, dict)]


def overhead_text(state):
    try:
        return state["effects"]["overheadText"].get("text") or ""
    except (KeyError, TypeError, AttributeError):
        return ""


def is_veng(anim, state):
    if anim == VENG_ANIM:
        return True
    if VENG_SPOTANIM in spotanim_ids(state):
        return True
    return "vengeance" in overhead_text(state).lower()


def actor_ref(payload, key):
    ref = payload.get(key)
    return ref if isinstance(ref, dict) else {}


def split_anim_events(seq_events):
    """Split a fighter's combat-anim id sequence into (prev, curr) bigrams."""
    return [(seq_events[i], seq_events[i + 1]) for i in range(len(seq_events) - 1)]


def mine_archive(path, min_anims):
    """Stream one archive. Returns (status, data) where status selects
    'used' (has >=1 usable fight), 'empty' (spectator, no usable fight),
    or 'skipped' (reason in data)."""
    manifest_path = path + ".manifest.json"
    source_sha256 = None
    try:
        with open(manifest_path) as mf:
            source_sha256 = json.load(mf).get("sourceSha256")
    except (OSError, ValueError):
        pass
    meta = {"archive": os.path.basename(path), "source_sha256": source_sha256}

    proc = subprocess.Popen(["zstd", "-dc", path], stdout=subprocess.PIPE,
                            stderr=subprocess.DEVNULL)
    try:
        selected_names = None
        ended = False
        index = 0
        anim_nostate = 0
        anim_state = 0
        tick_visible = {}  # game_tick -> {traceId: normalizedName}
        roles = collections.defaultdict(set)  # traceId -> set(scopeRole)
        names = {}  # traceId -> normalizedName
        snaps = collections.defaultdict(dict)  # game_tick -> {traceId: r30}
        last_r30 = {}  # traceId -> last known r30
        anim_events = []  # (tick, traceId, anim, own_r30)
        teleports = []  # (tick, traceId)
        vengs = []  # (tick, traceId)
        deaths = []  # (tick, traceId)
        hitsplats = 0

        for raw in proc.stdout:
            try:
                record = json.loads(raw)
            except ValueError:
                proc.kill()
                return "skipped", dict(meta, reason="json_parse_error")
            if record.get("sequence") != index:
                proc.kill()
                return "skipped", dict(meta, reason="sequence_gap")
            index += 1
            kind = record.get("eventKind")
            tick = record.get("lastObservedGameTick")
            payload = record.get("payload") or {}

            if kind == "SESSION_STARTED":
                if not isinstance(payload.get("spectatorPolicy"), dict):
                    # Not a spectator archive; stop early without full decode.
                    proc.kill()
                    return "skipped", dict(meta, reason="not_spectator")
                try:
                    selected_names = list(payload["spectatorPolicy"]["selectedNames"])
                except (KeyError, TypeError):
                    selected_names = []
            elif kind == "SESSION_ENDED":
                ended = True
            elif kind == "GAME_TICK_OBSERVED":
                vis = {}
                for target in payload.get("spectatorTargets") or []:
                    if not isinstance(target, dict):
                        continue
                    if target.get("visibility") == "VISIBLE":
                        tid = target.get("actorTraceId")
                        if tid is not None:
                            vis[tid] = target.get("normalizedName")
                            if target.get("normalizedName"):
                                names.setdefault(tid, target.get("normalizedName"))
                if isinstance(tick, int):
                    tick_visible[tick] = vis
            elif kind == "ACTOR_VISIBILITY_OBSERVED":
                tid = payload.get("actorTraceId")
                if tid is not None:
                    roles[tid].add(str(payload.get("scopeRole")) + ":" +
                                   str(payload.get("visibility")))
                    if payload.get("normalizedName"):
                        names.setdefault(tid, payload.get("normalizedName"))
                    state = payload.get("initialState") or {}
                    r30 = r30_of(state.get("healthBar"))
                    if r30 is not None:
                        last_r30[tid] = r30
                        if isinstance(tick, int):
                            snaps[tick][tid] = r30
            elif kind in ("ACTOR_STATE_OBSERVED", "ACTOR_EFFECT_OBSERVED"):
                actor = actor_ref(payload, "actor")
                tid = actor.get("actorTraceId")
                if tid is None:
                    continue
                if actor.get("scopeRole"):
                    roles[tid].add(actor.get("scopeRole"))
                if actor.get("normalizedName"):
                    names.setdefault(tid, actor.get("normalizedName"))
                state = payload.get("state")
                if not isinstance(state, dict):
                    continue
                r30 = r30_of(state.get("healthBar"))
                if r30 is not None:
                    last_r30[tid] = r30
                    if isinstance(tick, int):
                        snaps[tick][tid] = r30
            elif kind == "ANIMATION_OBSERVED":
                actor = actor_ref(payload, "actor")
                tid = actor.get("actorTraceId")
                if tid is None:
                    continue
                if actor.get("scopeRole"):
                    roles[tid].add(actor.get("scopeRole"))
                if actor.get("normalizedName"):
                    names.setdefault(tid, actor.get("normalizedName"))
                state = payload.get("state")
                if not isinstance(state, dict):
                    anim_nostate += 1
                    continue
                anim_nostate_check = payload.get("animation")
                anim = state.get("animation")
                if anim is None:
                    anim = anim_nostate_check
                anim_state += 1
                r30 = r30_of(state.get("healthBar"))
                if r30 is not None:
                    last_r30[tid] = r30
                    if isinstance(tick, int):
                        snaps[tick][tid] = r30
                if isinstance(anim, int) and isinstance(tick, int):
                    if anim == CONSUME_ANIM:
                        anim_events.append((tick, tid, anim, r30))
                    elif anim in WEAPON_ANIMS:
                        anim_events.append((tick, tid, anim, r30))
                    elif anim == TELEPORT_ANIM:
                        teleports.append((tick, tid))
                    # 4071 tail ignored; -1/blocks/idle carry no signal.
                    if is_veng(anim, state):
                        vengs.append((tick, tid))
            elif kind == "HITSPLAT_APPLIED_OBSERVED":
                hitsplats += 1
            elif kind == "ACTOR_DEATH_OBSERVED":
                actor = actor_ref(payload, "actor")
                tid = actor.get("actorTraceId")
                if tid is not None and isinstance(tick, int):
                    deaths.append((tick, tid))
                    if actor.get("normalizedName"):
                        names.setdefault(tid, actor.get("normalizedName"))
    finally:
        if proc.poll() is None:
            proc.kill()
        proc.wait()
    if not ended:
        return "skipped", dict(meta, reason="no_session_ended")

    def ever_selected(tid):
        return any(r == "SELECTED" or r.startswith("SELECTED:") for r in roles[tid])

    # Segment into fights: maximal tick runs with a stable visible pair,
    # interferers (never SELECTED) excluded.
    ordered_ticks = sorted(tick_visible)
    fights = []
    run_start = None
    run_pair = None
    run_names = None

    def close_run(run_end):
        if run_start is None or run_pair is None:
            return
        a, b = run_pair
        span_events = [e for e in anim_events
                       if run_start <= e[0] <= run_end and e[1] in run_pair]
        n_state = len(span_events)
        combat = [e for e in span_events if e[2] in COMBAT_ANIMS]
        if n_state < min_anims or not combat:
            return
        fights.append({
            "fighters": sorted(run_pair),
            "fighter_names": run_names,
            "tick_start": run_start,
            "tick_end": run_end,
            "anim_events": span_events,
            "teleports": [t for t in teleports
                          if run_start <= t[0] <= run_end and t[1] in run_pair],
            "vengs": [v for v in vengs
                      if run_start <= v[0] <= run_end and v[1] in run_pair],
            "deaths": [d for d in deaths
                       if run_start <= d[0] <= run_end and d[1] in run_pair],
        })

    prev_tick = None
    for tick in ordered_ticks:
        vis = tick_visible[tick]
        pair = tuple(sorted(t for t in vis if ever_selected(t)))
        if len(pair) != 2:
            close_run(prev_tick if prev_tick is not None else tick)
            run_start = run_pair = run_names = None
            prev_tick = tick
            continue
        if pair != run_pair or (prev_tick is not None and tick != prev_tick + 1):
            close_run(prev_tick if prev_tick is not None else tick)
            run_start = tick
            run_pair = pair
            run_names = {str(t): names.get(t) for t in pair}
        prev_tick = tick
    close_run(prev_tick if prev_tick is not None else 0)

    if not fights:
        return "empty", dict(meta, selected_names=selected_names,
                             anim_with_state=anim_state,
                             anim_without_state=anim_nostate,
                             hitsplats=hitsplats)
    return "used", dict(meta, selected_names=selected_names,
                        anim_with_state=anim_state,
                        anim_without_state=anim_nostate,
                        hitsplats=hitsplats, fights=fights, snaps=dict(snaps),
                        names={str(k): v for k, v in names.items()})


def summarize(fight, snaps):
    """Per-fight tables + opportunity denominators (perspective-ticks)."""
    a, b = fight["fighters"]
    opp_of = {a: b, b: a}
    ticks = range(fight["tick_start"], fight["tick_end"] + 1)
    # Forward-filled carry-forward over the integer span.
    last = {}
    bands_eat = collections.Counter()
    bands_vw = collections.Counter()
    weapon_mix = collections.Counter()
    vw_specs = 0
    maul_specs = 0
    axe_mid = 0
    o_le19 = o_le18 = own_le10 = o_mid = 0
    for tick in ticks:
        for tid in (a, b):
            if tid in snaps.get(tick, {}):
                last[tid] = snaps[tick][tid]
        for tid in (a, b):
            own = last.get(tid)
            opp = last.get(opp_of[tid])
            if own is None or opp is None:
                continue
            if opp <= 19:
                o_le19 += 1
            if opp <= 18:
                o_le18 += 1
            if own <= 10:
                own_le10 += 1
            if 10 <= own <= 24 and 12 <= opp <= 24:
                o_mid += 1
    eats = specs_vw = specs_maul = teles = 0
    consume_ticks = collections.defaultdict(list)
    seqs = collections.defaultdict(list)
    bigrams = collections.Counter()
    for (tick, tid, anim, own_r30) in fight["anim_events"]:
        opp = None
        snap = snaps.get(tick, {})
        if opp_of[tid] in snap:
            opp = snap[opp_of[tid]]
        elif last.get(opp_of[tid]) is not None:
            opp = last.get(opp_of[tid])
        if anim == CONSUME_ANIM:
            eats += 1
            consume_ticks[tid].append(tick)
            if own_r30 is not None:
                band = band_of(own_r30)
                if band:
                    bands_eat[band] += 1
        elif anim in VW_SPEC_ANIMS:
            specs_vw += 1
            vw_specs += 1
            weapon_mix["1378+11275"] += 1
            if opp is not None:
                band = band_of(opp)
                if band:
                    bands_vw[band] += 1
        elif anim == MAUL_SPEC_ANIM:
            specs_maul += 1
            maul_specs += 1
            weapon_mix[str(anim)] += 1
        else:
            weapon_mix[str(anim)] += 1
        if anim in WEAPON_ANIMS:
            seqs[tid].append(anim)
            if anim == AXE_ANIM and own_r30 is not None and opp is not None:
                if 10 <= own_r30 <= 24 and 12 <= opp <= 24:
                    axe_mid += 1
    for tid, seq in seqs.items():
        for prev, curr in split_anim_events(seq):
            if prev != curr:
                bigrams["%d>%d" % (prev, curr)] += 1
    for (tick, tid) in fight["teleports"]:
        teles += 1
    gaps = []
    for tid, ticks_c in consume_ticks.items():
        ticks_c.sort()
        gaps.extend(b - a_ for a_, b in zip(ticks_c, ticks_c[1:]))
    swings = sum(1 for e in fight["anim_events"] if e[2] in WEAPON_ANIMS)
    active_ticks = len({e[0] for e in fight["anim_events"] if e[2] in WEAPON_ANIMS})
    return {
        "fighters": fight["fighter_names"],
        "tick_start": fight["tick_start"],
        "tick_end": fight["tick_end"],
        "eats": eats,
        "specs_vw": specs_vw,
        "specs_maul": specs_maul,
        "teleports": teles,
        "vengs": len(fight["vengs"]),
        "deaths": len(fight["deaths"]),
        "bands_eat": dict(bands_eat),
        "bands_vw": dict(bands_vw),
        "weapon_mix": dict(weapon_mix),
        "bigrams": dict(bigrams),
        "axe_mid": axe_mid,
        "ticks_opp_le19": o_le19,
        "ticks_opp_le18": o_le18,
        "ticks_own_le10": own_le10,
        "ticks_midzone": o_mid,
        "consume_gaps": gaps,
        "swings": swings,
        "active_ticks": active_ticks,
    }


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--recordings",
                        default=os.path.expanduser("~/.light2/gameplay-recordings/spectator-combat"))
    parser.add_argument("--out", default=None)
    parser.add_argument("--min-anims", type=int, default=50)
    args = parser.parse_args()

    paths = sorted(glob.glob(os.path.join(args.recordings, "session_*.zst")))
    provenance = {"archives_used": [], "archives_skipped": []}
    fight_rows = []
    tables = {
        "eat_consume_events_by_own_ratio_band": collections.Counter(),
        "weapon_anim_mix": collections.Counter(),
        "switch_bigrams": collections.Counter(),
        "vw_specs_by_opp_ratio_band": collections.Counter(),
        "maul_specs": 0,
        "axe_anims_midzone": 0,
        "teleports": 0,
        "ticks_opp_le19": 0,
        "ticks_opp_le18": 0,
        "ticks_own_le10": 0,
        "ticks_midzone": 0,
        "ticks_between_consumes": {},
        "eats_per_fight": [],
        "teleports_per_fight": [],
        "deaths_per_fight": [],
        "swing_rate_per_100_active_ticks": 0.0,
    }
    gaps_all = []
    swings_all = 0
    active_all = 0
    usable_fights = 0

    for path in paths:
        status, data = mine_archive(path, args.min_anims)
        if status == "skipped":
            provenance["archives_skipped"].append(data)
            continue
        if status == "empty":
            provenance["archives_skipped"].append(
                {"archive": data["archive"],
                 "source_sha256": data["source_sha256"],
                 "reason": "no_usable_fight"})
            continue
        provenance["archives_used"].append(
            {"archive": data["archive"], "source_sha256": data["source_sha256"]})
        snaps = {int(k): v for k, v in data["snaps"].items()}
        for fight in data["fights"]:
            usable_fights += 1
            row = summarize(fight, snaps)
            fight_rows.append({"archive": data["archive"], **row})
            tables["eat_consume_events_by_own_ratio_band"].update(row["bands_eat"])
            tables["weapon_anim_mix"].update(row["weapon_mix"])
            tables["switch_bigrams"].update(row["bigrams"])
            tables["vw_specs_by_opp_ratio_band"].update(row["bands_vw"])
            tables["maul_specs"] += row["specs_maul"]
            tables["axe_anims_midzone"] += row["axe_mid"]
            tables["teleports"] += row["teleports"]
            tables["ticks_opp_le19"] += row["ticks_opp_le19"]
            tables["ticks_opp_le18"] += row["ticks_opp_le18"]
            tables["ticks_own_le10"] += row["ticks_own_le10"]
            tables["ticks_midzone"] += row["ticks_midzone"]
            tables["eats_per_fight"].append(row["eats"])
            tables["teleports_per_fight"].append(row["teleports"])
            tables["deaths_per_fight"].append(row["deaths"])
            gaps_all.extend(row["consume_gaps"])
            swings_all += row["swings"]
            active_all += row["active_ticks"]

    for key in ("eat_consume_events_by_own_ratio_band", "weapon_anim_mix",
                "switch_bigrams", "vw_specs_by_opp_ratio_band"):
        tables[key] = dict(tables[key])
    tables["switch_bigrams"] = dict(
        sorted(tables["switch_bigrams"].items(), key=lambda kv: -kv[1])[:10])
    if gaps_all:
        gaps_all.sort()
        tables["ticks_between_consumes"] = {
            "median": statistics.median(gaps_all),
            "p90": gaps_all[min(len(gaps_all) - 1, int(len(gaps_all) * 0.9))],
        }
    else:
        tables["ticks_between_consumes"] = {"median": None, "p90": None}
    if active_all:
        tables["swing_rate_per_100_active_ticks"] = round(100.0 * swings_all / active_all, 2)

    for row in fight_rows:
        del row["consume_gaps"]
        del row["bands_eat"]
        del row["bands_vw"]
        del row["weapon_mix"]
        del row["bigrams"]
        del row["axe_mid"]
        del row["ticks_opp_le19"]
        del row["ticks_opp_le18"]
        del row["ticks_own_le10"]
        del row["ticks_midzone"]
        del row["swings"]
        del row["active_ticks"]

    if usable_fights < 10:
        rolls = {"early_eat_pm": 250, "eat_delay_pm": 150, "vw_pm": 400,
                 "maul_pm": 200, "axe_pm": 100, "teleport_early_pm": 50,
                 "fallback": True}
    else:
        bands = tables["eat_consume_events_by_own_ratio_band"]
        e_total = sum(bands.get(b, 0) for b in BAND_NAMES)
        e_high = sum(bands.get(b, 0) for b in ("16-19", "20-24", "25-30"))
        s_vw = sum(tables["weapon_anim_mix"].get(k, 0) for k in ("1378+11275",))
        s_vw += tables["weapon_anim_mix"].get("1378", 0)
        m_maul = tables["maul_specs"]
        a_mid = tables["axe_anims_midzone"]
        o_19 = tables["ticks_opp_le19"]
        o_18 = tables["ticks_opp_le18"]
        o_mid = tables["ticks_midzone"]
        t_tp = tables["teleports"]
        t_le10 = tables["ticks_own_le10"]
        rolls = {
            "early_eat_pm": min(600, round(1000 * e_high / max(1, e_total))),
            "eat_delay_pm": 150,
            "vw_pm": (0 if s_vw == 0
                      else min(800, max(20, round(1000 * s_vw / max(1, o_19))))),
            "maul_pm": (0 if m_maul == 0
                        else min(500, round(1000 * m_maul / max(1, o_18)))),
            "axe_pm": (0 if a_mid == 0
                       else min(400, round(1000 * a_mid / max(1, o_mid)))),
            "teleport_early_pm": (0 if t_tp == 0
                                  else min(300, round(1000 * t_tp / max(1, t_le10)))),
            "fallback": False,
        }

    out = {"provenance": provenance, "fights": fight_rows, "tables": tables,
           "rolls": rolls}
    if args.out:
        with open(args.out, "w") as handle:
            json.dump(out, handle, indent=1)
    print("usable_fights=%d" % usable_fights)
    print(json.dumps(rolls, indent=1))
    return 0


if __name__ == "__main__":
    sys.exit(main())
