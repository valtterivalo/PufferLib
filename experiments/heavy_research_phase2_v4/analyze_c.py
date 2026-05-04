"""Stage C analyzer: applies heavy-agent gates to the C0/C1/C2 phase-2 scout.

Pulls finished stage-r2-c-* runs from wandb, summarizes per arm, then evaluates:
  POSITIVE_C  -- phase 2 is helping
    >=1 normal-start win
    OR best score >= 0.80
    OR top-10 median lift >= 0.04 over C0
    OR count_min_hp_le_300 doubles over C0
    OR best episode min_hp <= 200 (proxied via frac_min_hp_le_150 > 0 across cells)
  NO_GO_C
    C1/C2 under C0 on top-10 median
    OR snapshot metrics improve but normal-start score does not
    (we surface both signals; humans gate the decision)

R1/R2 baselines for context:
  v3 200M best         0.7375
  v3 200M top10_median 0.5701
  P0 D-audit eval:     count_<=300=9135 / 20k = 45.6%

Run from repo root:
  python experiments/heavy_research_phase2_v4/analyze_c.py
"""

import argparse
import json
from statistics import median
import wandb


PROJECT = "valtterivalo-clock-cloud/puffer4"
TAG_PREFIX = "stage-r2-c"

# v3 baselines from earlier rounds for context lines.
BASELINES = {
    "r1_v3_top10_med": 0.596,
    "r2_v3_200M_best": 0.7375,
    "r2_v3_200M_top10_med": 0.5701,
    "p0_count_le_300_per_20k": 9135,
}


def safe(d, key):
    v = d.get(key)
    try:
        return float(v) if v is not None else None
    except (TypeError, ValueError):
        return None


def fetch_arm(api, arm):
    needle = f"{TAG_PREFIX}-{arm}-"
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
            "frac_le_300": safe(r.summary, "env/frac_min_hp_le_300_normal"),
            "frac_le_240": safe(r.summary, "env/frac_min_hp_le_240_normal"),
            "frac_le_150": safe(r.summary, "env/frac_min_hp_le_150_normal"),
            "snapshot_frac": safe(r.summary, "env/snapshot_frac"),
        })
    return [r for r in rows if r["score"] is not None]


def summarize(rows, label):
    print(f"\n{'='*78}")
    print(f"{label}: n={len(rows)}")
    print(f"{'='*78}")
    if not rows:
        return {}
    scores = sorted([r["score"] for r in rows], reverse=True)
    s_p50 = median(scores)
    s_top10 = scores[:10]
    s_top10_med = median(s_top10) if s_top10 else 0.0
    s_top3 = scores[:3]
    s_top3_med = median(s_top3) if s_top3 else 0.0
    s_best = scores[0] if scores else 0.0
    n_wins_runs = sum(1 for r in rows if (r["wins"] or 0) > 1e-6)
    wins_total = sum(r["wins"] or 0 for r in rows)
    fr_le_300 = [r["frac_le_300"] for r in rows if r["frac_le_300"] is not None]
    fr_le_240 = [r["frac_le_240"] for r in rows if r["frac_le_240"] is not None]
    fr_le_150 = [r["frac_le_150"] for r in rows if r["frac_le_150"] is not None]
    med_fr_300 = median(fr_le_300) if fr_le_300 else 0.0
    med_fr_240 = median(fr_le_240) if fr_le_240 else 0.0
    med_fr_150 = median(fr_le_150) if fr_le_150 else 0.0
    n_with_le_150 = sum(1 for f in fr_le_150 if (f or 0) > 0)

    print(f"  scores: best={s_best:.4f}  top3_med={s_top3_med:.4f}  "
          f"top10_med={s_top10_med:.4f}  p50={s_p50:.4f}")
    print(f"  wins:    runs_with_any_win={n_wins_runs}  total_win_rate={wins_total:.4f}")
    print(f"  tails:   frac_<=300 median={med_fr_300:.3f}  "
          f"<=240 median={med_fr_240:.3f}  <=150 median={med_fr_150:.4f}  "
          f"cells with any <=150: {n_with_le_150}/{len(rows)}")

    print(f"\n  per cell:")
    for r in sorted(rows, key=lambda x: -x["score"]):
        print(f"    {r['id']:<10} score={r['score']:.4f} win={r['wins'] or 0:.3f} "
              f"min_hp={(r['min_hp'] or 0):>6.1f} phase={r['phase'] or 0:.2f} "
              f"<=300={(r['frac_le_300'] or 0):.3f} <=240={(r['frac_le_240'] or 0):.3f} "
              f"<=150={(r['frac_le_150'] or 0):.4f}  tag={r['tag']}")

    return {
        "n": len(rows),
        "best": s_best,
        "top3_med": s_top3_med,
        "top10_med": s_top10_med,
        "p50": s_p50,
        "n_wins_runs": n_wins_runs,
        "wins_total": wins_total,
        "med_fr_300": med_fr_300,
        "med_fr_240": med_fr_240,
        "med_fr_150": med_fr_150,
        "n_with_le_150": n_with_le_150,
    }


def evaluate_gates(c0, c1, c2):
    print(f"\n{'='*78}")
    print("Gates")
    print(f"{'='*78}")

    def positive_for(arm_label, arm):
        signals = []
        if not arm:
            return [f"{arm_label}: no rows"]
        if arm["n_wins_runs"] >= 1:
            signals.append(f"  + {arm['n_wins_runs']} {arm_label} cells produced normal-start wins")
        if arm["best"] >= 0.80:
            signals.append(f"  + {arm_label} best score {arm['best']:.3f} >= 0.80")
        if c0 and arm["top10_med"] >= c0["top10_med"] + 0.04:
            signals.append(f"  + {arm_label} top-10 median {arm['top10_med']:.3f} "
                           f">= C0 + 0.04 (C0 top10_med {c0['top10_med']:.3f})")
        if c0 and arm["med_fr_300"] >= 2 * c0["med_fr_300"] and arm["med_fr_300"] > 0:
            signals.append(f"  + {arm_label} median frac_<=300 {arm['med_fr_300']:.3f} "
                           f">= 2x C0 ({c0['med_fr_300']:.3f})")
        if arm["n_with_le_150"] >= 1:
            signals.append(f"  + {arm_label} {arm['n_with_le_150']} cells reached <=150 HP")
        return signals

    print("\nPOSITIVE_C signals:")
    for arm_label, arm in [("C1", c1), ("C2", c2)]:
        sigs = positive_for(arm_label, arm)
        if sigs:
            for s in sigs:
                print(s)
        else:
            print(f"  {arm_label}: no positive signals")

    print("\nNO_GO signals (humans interpret):")
    if c0 and c1 and c1["top10_med"] < c0["top10_med"] - 0.02:
        print(f"  - C1 top10 median {c1['top10_med']:.3f} below C0 ({c0['top10_med']:.3f})")
    if c0 and c2 and c2["top10_med"] < c0["top10_med"] - 0.02:
        print(f"  - C2 top10 median {c2['top10_med']:.3f} below C0 ({c0['top10_med']:.3f})")
    if c0 and c1 and c2:
        if max(c1["best"], c2["best"]) < c0["best"]:
            print(f"  - C0 best {c0['best']:.3f} > both C1 best {c1['best']:.3f} "
                  f"and C2 best {c2['best']:.3f}")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default="experiments/heavy_research_phase2_v4/c_results.txt")
    args = ap.parse_args()

    api = wandb.Api(timeout=60)
    print("Pulling C0 (ppo-control)...")
    c0 = fetch_arm(api, "c0")
    print(f"  {len(c0)} finished")
    print("Pulling C1 (phase2-no-bc)...")
    c1 = fetch_arm(api, "c1")
    print(f"  {len(c1)} finished")
    print("Pulling C2 (phase2-bc)...")
    c2 = fetch_arm(api, "c2")
    print(f"  {len(c2)} finished")

    s_c0 = summarize(c0, "C0: PPO continuation control (50M)")
    s_c1 = summarize(c1, "C1: phase 2, no BC (50M)")
    s_c2 = summarize(c2, "C2: phase 2 + tiny BC (50M)")

    evaluate_gates(s_c0, s_c1, s_c2)

    print(f"\n  context: A' v3 200M best={BASELINES['r2_v3_200M_best']:.3f}, "
          f"top10_med={BASELINES['r2_v3_200M_top10_med']:.3f}")
    print(f"           P0 D-audit had {BASELINES['p0_count_le_300_per_20k']}/20k "
          f"({BASELINES['p0_count_le_300_per_20k']/20000:.1%}) eps <=300")


if __name__ == "__main__":
    main()
