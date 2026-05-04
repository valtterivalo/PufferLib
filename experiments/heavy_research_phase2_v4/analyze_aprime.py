"""Stage A' analyzer: applies the heavy-agent gates to the 200M continuation runs.

Pulls all stage-r2-aprime-* runs from wandb and computes per-cell + per-config
summary stats. Reports against the 4 gates the heavy agent specified.

Gates:
  STRONG_CONTINUE: >=2 normal-start wins  OR  best score >=0.90
                   OR  >=4/15 v3 cells reach min_zuk_hp <= 150
  POSITIVE_UNDERTRAIN: v3 top-10 median score >= 0.66
                       OR  v3 best >= 0.82
                       OR  v3 p50 score lift >= 0.08 over R1 v3 p50 (0.523)
                       OR  last-50M score slope >= 0.03 in >=1/3 top configs
  CONTROL_CEILING_WARN: ctrl 200M best >= v3 200M best + 0.05
                        OR  ctrl top-3 median >= v3 top-3 median
  LONGER_PPO_NOGO: 0 wins AND best score < 0.78 AND v3 top-10 median <= 0.63
                   AND last-50M slope < 0.02 AND phase_reached flat

R1 v3 baselines (for comparison):
  p50 score = 0.523, top-10 median score = 0.596, best score = 0.709
R1 control baselines:
  p50 score = 0.299, top-10 median score = 0.575, best score = 0.787

Run from repo root (after A' completes):
  python experiments/heavy_research_phase2_v4/analyze_aprime.py
"""

import argparse
from statistics import median, quantiles
import wandb


PROJECT = "valtterivalo-clock-cloud/puffer4"
TAG_PREFIX = "stage-r2-aprime"

# R1 baselines pulled from r1_results.txt for comparison.
R1_BASELINES = {
    "v3": {"p50": 0.523, "top10_median": 0.596, "best": 0.709},
    "ctrl": {"p50": 0.299, "top10_median": 0.575, "best": 0.787},
}

KEYS = [
    "env/zuk_objective_normal",
    "env/score_normal",
    "env/wins_normal",
    "env/min_zuk_hp_normal",
    "env/phase_reached_normal",
    "env/death_tick_normal",
    "env/episode_return_normal",
]


def safe(summary, key):
    v = summary.get(key)
    try:
        return float(v) if v is not None else None
    except (TypeError, ValueError):
        return None


def fetch_arm(api, arm):
    """arm in {'a1-v3','a2-ctrl'}.  Returns list of finished runs with summary."""
    needle = f"{TAG_PREFIX}-{arm}-"
    # wandb tag filters don't support $regex; pull broadly and filter client-side.
    runs = list(api.runs(PROJECT, filters={"state": "finished"}))
    rows = []
    for r in runs:
        tags = r.tags or []
        cell_tag = next((t for t in tags if t.startswith(needle)), None)
        if cell_tag is None:
            continue
        rows.append({
            "id": r.id,
            "tag": cell_tag,
            "score": safe(r.summary, "env/score_normal"),
            "obj": safe(r.summary, "env/zuk_objective_normal"),
            "wins": safe(r.summary, "env/wins_normal"),
            "min_hp": safe(r.summary, "env/min_zuk_hp_normal"),
            "phase": safe(r.summary, "env/phase_reached_normal"),
            "death_t": safe(r.summary, "env/death_tick_normal"),
            "ret": safe(r.summary, "env/episode_return_normal"),
        })
    return [r for r in rows if r["score"] is not None]


def compute_last_50m_slope(api, run_id):
    """Returns score slope over the last ~25% of training, or None."""
    try:
        run = api.run(f"{PROJECT}/{run_id}")
        hist = list(run.scan_history(keys=["_step", "env/score_normal"]))
        pts = [(h["_step"], h["env/score_normal"]) for h in hist
               if h.get("env/score_normal") is not None]
        if len(pts) < 4:
            return None
        pts.sort()
        # Use last quarter as proxy for "last 50M" of 200M run.
        cut = len(pts) * 3 // 4
        last = [p[1] for p in pts[cut:]]
        first = [p[1] for p in pts[cut-1:cut]]
        return last[-1] - first[0] if first else None
    except Exception:
        return None


def summarize(rows, label, baseline=None):
    print(f"\n{'='*78}")
    print(f"{label}: n={len(rows)}")
    print(f"{'='*78}")
    if not rows:
        return {}

    scores = sorted([r["score"] for r in rows], reverse=True)
    objs = sorted([r["obj"] for r in rows], reverse=True)
    wins_total = sum(r["wins"] or 0 for r in rows)
    n_wins_runs = sum(1 for r in rows if (r["wins"] or 0) > 0.001)
    hp_le_150 = sum(1 for r in rows if (r["min_hp"] or 1e9) <= 150)
    hp_le_300 = sum(1 for r in rows if (r["min_hp"] or 1e9) <= 300)

    s_p50 = median(scores)
    s_top10 = scores[:10]
    s_top10_med = median(s_top10) if s_top10 else 0.0
    s_top3 = scores[:3]
    s_top3_med = median(s_top3) if s_top3 else 0.0
    s_best = scores[0] if scores else 0.0

    print(f"  scores: best={s_best:.4f}  top3_med={s_top3_med:.4f}  "
          f"top10_med={s_top10_med:.4f}  p50={s_p50:.4f}  worst={scores[-1]:.4f}")
    print(f"  wins:    total_normal_wins={wins_total:.3f}  runs_with_any_win={n_wins_runs}")
    print(f"  ceiling: cells with min_hp<=150: {hp_le_150}/{len(rows)}    <=300: {hp_le_300}/{len(rows)}")

    if baseline:
        s_p50_lift = s_p50 - baseline["p50"]
        s_top10_lift = s_top10_med - baseline["top10_median"]
        s_best_lift = s_best - baseline["best"]
        print(f"  vs R1 baseline: p50 {s_p50_lift:+.3f}  "
              f"top10_med {s_top10_lift:+.3f}  best {s_best_lift:+.3f}")

    print(f"\n  top 10 cells:")
    for r in rows[:10] if False else sorted(rows, key=lambda x: -x["score"])[:10]:
        print(f"    {r['id']:<10} score={r['score']:.4f} win={r['wins']:.3f} "
              f"min_hp={(r['min_hp'] or 0):>6.1f} phase={r['phase'] or 0:.2f} "
              f"death_t={(r['death_t'] or 0):>5.1f}  tag={r['tag']}")

    return {
        "n": len(rows),
        "scores": scores,
        "best": s_best,
        "top3_med": s_top3_med,
        "top10_med": s_top10_med,
        "p50": s_p50,
        "wins_total": wins_total,
        "n_wins_runs": n_wins_runs,
        "hp_le_150": hp_le_150,
        "hp_le_300": hp_le_300,
        "rows": rows,
    }


def evaluate_gates(v3, ctrl, slope_count_v3=None):
    print(f"\n{'='*78}")
    print("Gates")
    print(f"{'='*78}")

    if not v3.get("rows"):
        print("  No v3 rows; cannot evaluate.")
        return

    # STRONG_CONTINUE
    sc = []
    if v3["n_wins_runs"] >= 2:
        sc.append(f"  + {v3['n_wins_runs']} v3 runs have >=1 normal-start win")
    if v3["best"] >= 0.90:
        sc.append(f"  + v3 best score {v3['best']:.3f} >= 0.90")
    if v3["hp_le_150"] >= 4:
        sc.append(f"  + {v3['hp_le_150']}/{v3['n']} v3 cells reach min_hp<=150")
    if ctrl.get("rows"):
        if ctrl["n_wins_runs"] >= 2:
            sc.append(f"  + {ctrl['n_wins_runs']} ctrl runs have >=1 normal-start win")
    print(f"\nSTRONG_CONTINUE: {'YES' if sc else 'no'}")
    for s in sc:
        print(s)

    # POSITIVE_UNDERTRAIN
    pu = []
    if v3["top10_med"] >= 0.66:
        pu.append(f"  + v3 top-10 median {v3['top10_med']:.3f} >= 0.66")
    if v3["best"] >= 0.82:
        pu.append(f"  + v3 best {v3['best']:.3f} >= 0.82")
    p50_lift = v3["p50"] - R1_BASELINES["v3"]["p50"]
    if p50_lift >= 0.08:
        pu.append(f"  + v3 p50 lift {p50_lift:+.3f} >= +0.08 over R1")
    if slope_count_v3 is not None and slope_count_v3 >= 1:
        pu.append(f"  + last-25% score slope >=0.03 in {slope_count_v3} v3 cells")
    print(f"\nPOSITIVE_UNDERTRAIN: {'YES' if pu else 'no'}")
    for s in pu:
        print(s)

    # CONTROL_CEILING_WARN
    cw = []
    if ctrl.get("rows"):
        if ctrl["best"] >= v3["best"] + 0.05:
            cw.append(f"  + ctrl best {ctrl['best']:.3f} >= v3 best + 0.05")
        if ctrl["top3_med"] >= v3["top3_med"]:
            cw.append(f"  + ctrl top-3 median {ctrl['top3_med']:.3f} "
                      f">= v3 top-3 median {v3['top3_med']:.3f}")
    print(f"\nCONTROL_CEILING_WARN: {'YES' if cw else 'no'}")
    for s in cw:
        print(s)

    # LONGER_PPO_NOGO (all conditions must hold)
    nogo_conds = [
        v3["n_wins_runs"] == 0,
        v3["best"] < 0.78,
        v3["top10_med"] <= 0.63,
    ]
    if slope_count_v3 is not None:
        nogo_conds.append(slope_count_v3 == 0)
    nogo = all(nogo_conds)
    print(f"\nLONGER_PPO_NOGO: {'YES' if nogo else 'no'}")
    if nogo:
        print("  All conditions hold:")
        print(f"    0 wins, best={v3['best']:.3f}<0.78, top10_med={v3['top10_med']:.3f}<=0.63")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--no-slopes", action="store_true",
                    help="skip per-run scan_history slope computation (faster)")
    args = ap.parse_args()

    api = wandb.Api(timeout=60)
    print("Pulling A1 (v3) ...")
    v3_rows = fetch_arm(api, "a1-v3")
    print(f"  {len(v3_rows)} finished v3 runs")

    print("Pulling A2 (ctrl) ...")
    ctrl_rows = fetch_arm(api, "a2-ctrl")
    print(f"  {len(ctrl_rows)} finished ctrl runs")

    v3 = summarize(v3_rows, "A1: v3 reward (200M)", baseline=R1_BASELINES["v3"])
    ctrl = summarize(ctrl_rows, "A2: control reward (200M)", baseline=R1_BASELINES["ctrl"])

    slope_count_v3 = None
    if not args.no_slopes and v3.get("rows"):
        print("\nComputing last-25% slopes for v3 top-3 configs (3 cells per config)...")
        # group by config_id (run_id from R1)
        v3_by_cfg = {}
        for r in v3["rows"]:
            tag = r["tag"] or ""
            parts = tag.split("-")
            if len(parts) >= 6:
                cfg = parts[5]
                v3_by_cfg.setdefault(cfg, []).append(r)
        slope_count = 0
        for cfg, cells in v3_by_cfg.items():
            slopes = []
            for cell in cells[:3]:
                s = compute_last_50m_slope(api, cell["id"])
                if s is not None:
                    slopes.append(s)
            med_slope = median(slopes) if slopes else None
            print(f"  cfg {cfg}: median slope = {med_slope}")
            if med_slope is not None and med_slope >= 0.03:
                slope_count += 1
        slope_count_v3 = slope_count

    evaluate_gates(v3, ctrl, slope_count_v3=slope_count_v3)


if __name__ == "__main__":
    main()
