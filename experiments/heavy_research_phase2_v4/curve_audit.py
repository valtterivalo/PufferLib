"""Stage-A' precheck: learning-curve audit for top-10 v3 + top-10 control.

Computes end-of-training slopes (last bin vs penultimate bin) on:
  score_normal, min_zuk_hp_normal, phase_reached_normal, death_tick_normal

Per heavy agent's gates:
  Still learning:
    score lift >= 0.03 in final bin  OR
    min_zuk_hp drop >= 40-60 HP late  OR
    phase / death_tick still rising without entropy collapse
  Plateau:
    score < 0.02  AND  min_zuk_hp drop < 30  AND  phase flat  AND  death_tick flat/shrinking

Output goes to stdout and is also saved to curve_audit_results.txt.

Run from repo root:
  python experiments/heavy_research_phase2_v4/curve_audit.py
"""

import wandb
from statistics import median


PROJECT = "valtterivalo-clock-cloud/puffer4"
TAGS = {
    "control": "stage-r1-current-widened",
    "v3": "stage-r1-v3-rebalanced",
}
TOP_N = 10
KEYS = [
    "env/score_normal",
    "env/min_zuk_hp_normal",
    "env/phase_reached_normal",
    "env/death_tick_normal",
    "losses/entropy",
    "losses/approx_kl",
    "losses/value_loss",
    "perf/uptime",
]


def safe_get(summary, key):
    try:
        v = summary.get(key)
        return float(v) if v is not None else None
    except (TypeError, ValueError):
        return None


def fetch_history(run, keys, samples=200):
    """Pull downsampled history. Returns list of dicts ordered by step."""
    hist = list(run.scan_history(keys=["_step"] + keys, page_size=500))
    return hist


def end_slope(hist, key):
    """Slope = last_value - prev_value over the last two non-null records.

    Returns (slope, last, prev, n_points) or (None, None, None, 0).
    """
    pts = [(h.get("_step"), h.get(key)) for h in hist if h.get(key) is not None]
    if len(pts) < 2:
        return None, None, None, len(pts)
    pts.sort(key=lambda x: x[0])
    last_step, last_val = pts[-1]
    prev_step, prev_val = pts[-2]
    return last_val - prev_val, last_val, prev_val, len(pts)


def classify(score_slope, hp_slope, phase_slope, death_slope, entropy_slope):
    """Heavy agent's plateau-vs-still-learning rule."""
    still_signals = []
    if score_slope is not None and score_slope >= 0.03:
        still_signals.append(f"score+{score_slope:.3f}>=0.03")
    if hp_slope is not None and hp_slope <= -40:
        still_signals.append(f"min_hp{hp_slope:+.0f}<=-40")
    if phase_slope is not None and phase_slope >= 0.05:
        still_signals.append(f"phase+{phase_slope:+.3f}>=0.05")
    if death_slope is not None and death_slope >= 5:
        still_signals.append(f"death_tick+{death_slope:+.1f}>=5")

    plateau_signals = []
    if score_slope is not None and abs(score_slope) < 0.02:
        plateau_signals.append("score<0.02")
    if hp_slope is not None and abs(hp_slope) < 30:
        plateau_signals.append("min_hp<30")
    if phase_slope is not None and abs(phase_slope) < 0.03:
        plateau_signals.append("phase<0.03")
    if death_slope is not None and abs(death_slope) < 3:
        plateau_signals.append("death<3")

    collapse = entropy_slope is not None and entropy_slope < -0.05

    if collapse:
        return "COLLAPSE", still_signals, plateau_signals
    if len(still_signals) >= 2:
        return "STILL_LEARNING", still_signals, plateau_signals
    if len(plateau_signals) >= 3:
        return "PLATEAU", still_signals, plateau_signals
    return "AMBIGUOUS", still_signals, plateau_signals


def audit_arm(api, label, tag):
    print(f"\n{'='*78}")
    print(f"Arm: {label}  (tag={tag})  top-{TOP_N} by zuk_objective_normal")
    print(f"{'='*78}")
    runs = list(api.runs(PROJECT, filters={"tags": tag, "state": "finished"}))
    keyed = []
    for r in runs:
        obj = safe_get(r.summary, "env/zuk_objective_normal")
        if obj is None:
            continue
        keyed.append((obj, r))
    keyed.sort(key=lambda x: -x[0])
    top = keyed[:TOP_N]

    rows = []
    print(f"{'run_id':<10} {'obj':>5} {'score_slope':>12} {'hp_slope':>9} "
          f"{'phase_slope':>12} {'death_slope':>12} {'ent_slope':>10} verdict")
    for obj, r in top:
        try:
            hist = fetch_history(r, KEYS)
        except Exception as e:
            print(f"{r.id:<10}  HIST_FAIL: {e}")
            continue
        score_slope, score_last, score_prev, n_score = end_slope(hist, "env/score_normal")
        hp_slope, _, _, _ = end_slope(hist, "env/min_zuk_hp_normal")
        phase_slope, _, _, _ = end_slope(hist, "env/phase_reached_normal")
        death_slope, _, _, _ = end_slope(hist, "env/death_tick_normal")
        entropy_slope, _, _, _ = end_slope(hist, "losses/entropy")
        verdict, still, plat = classify(score_slope, hp_slope, phase_slope,
                                         death_slope, entropy_slope)
        print(f"{r.id:<10} {obj:5.3f} "
              f"{(score_slope if score_slope is not None else float('nan')):>12.4f} "
              f"{(hp_slope if hp_slope is not None else float('nan')):>9.1f} "
              f"{(phase_slope if phase_slope is not None else float('nan')):>12.4f} "
              f"{(death_slope if death_slope is not None else float('nan')):>12.2f} "
              f"{(entropy_slope if entropy_slope is not None else float('nan')):>10.4f} "
              f"{verdict}")
        rows.append({
            "run_id": r.id,
            "obj": obj,
            "score_slope": score_slope,
            "hp_slope": hp_slope,
            "phase_slope": phase_slope,
            "death_slope": death_slope,
            "entropy_slope": entropy_slope,
            "score_last": score_last,
            "score_prev": score_prev,
            "n_score_points": n_score,
            "verdict": verdict,
        })
    return rows


def summarize(rows, label):
    if not rows:
        print(f"\n  {label}: no rows")
        return
    counts = {}
    for r in rows:
        counts[r["verdict"]] = counts.get(r["verdict"], 0) + 1
    score_slopes = [r["score_slope"] for r in rows if r["score_slope"] is not None]
    hp_slopes = [r["hp_slope"] for r in rows if r["hp_slope"] is not None]
    phase_slopes = [r["phase_slope"] for r in rows if r["phase_slope"] is not None]
    print(f"\n  {label} top-{len(rows)} verdict counts: {counts}")
    if score_slopes:
        print(f"    median score_slope: {median(score_slopes):+.4f}")
    if hp_slopes:
        print(f"    median hp_slope:    {median(hp_slopes):+.1f}")
    if phase_slopes:
        print(f"    median phase_slope: {median(phase_slopes):+.4f}")


def main():
    api = wandb.Api(timeout=60)
    all_rows = {}
    for label, tag in TAGS.items():
        all_rows[label] = audit_arm(api, label, tag)

    print(f"\n{'='*78}")
    print("Summary")
    print(f"{'='*78}")
    for label in TAGS:
        summarize(all_rows[label], label)

    print("\n  Decision rule:")
    print("    If majority of top-10 v3 are STILL_LEARNING -> A' likely useful (longer = more progress)")
    print("    If majority are PLATEAU                     -> A' anchor only, then pivot (B or C)")
    print("    If COLLAPSE shows up                        -> something is unstable; investigate")


if __name__ == "__main__":
    main()
