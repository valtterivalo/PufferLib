"""Stage R1 analysis: 4-arm reward variant screening on zuk_objective_normal.

Pulls finished trials for each tag, reports per-arm summary stats on the
primary metric and reward-hacking diagnostics, then bootstraps each variant
against the current_widened control on zuk_objective_normal.

Per the heavy research agent's v4 brief:
  positive variant requires:
    IQM(zuk_objective_normal) lift >= 20% over control
    PI(variant > control) >= 0.70
    score_normal top-10 median lift >= 0.05 absolute
  strong evidence (any one):
    >= 2 normal-start wins in independent top-10 runs
    normal-start min_zuk_hp p50 < 300
    normal-start score_normal >= 0.75
  clean no-go:
    IQM lift <= 10% AND no normal-start wins AND improvement only in
    reward-hacking diagnostics

Reward-hacking diagnostics:
  higher episode_return_normal but flat score_normal
  earlier death with higher return
  tag reward up without Zuk HP progress
  shield penalty avoidance via suicide
  phase_reached_normal flat

Usage:
  python experiments/heavy_research_phase2_v4/analyze_r1.py
  python experiments/heavy_research_phase2_v4/analyze_r1.py --variants v1
  python experiments/heavy_research_phase2_v4/analyze_r1.py --top-k 20
"""

import argparse
import sys

import numpy as np


CONTROL_TAG = "stage-r1-current-widened"
VARIANT_TAGS = {
    "v1": "stage-r1-v1-progress",
    "v2": "stage-r1-v2-milestones",
    "v3": "stage-r1-v3-rebalanced",
}

PRIMARY_METRIC = "env/zuk_objective_normal"
DIAGNOSTIC_METRICS = [
    "env/zuk_objective_normal",
    "env/score_normal",
    "env/wins_normal",
    "env/min_zuk_hp_normal",
    "env/episode_return_normal",
    "env/phase_reached_normal",
    "env/death_tick_normal",
]


def iqm(x):
    x = np.sort(np.asarray(x, dtype=float))
    n = len(x)
    if n < 4:
        return float(np.mean(x)) if n else float("nan")
    lo = n // 4
    hi = n - lo
    return float(x[lo:hi].mean())


def prob_improvement(a, b):
    a = np.asarray(a, dtype=float)
    b = np.asarray(b, dtype=float)
    return float((a[:, None] > b[None, :]).mean())


def bootstrap(treat, control, n=10_000, seed=0):
    rng = np.random.default_rng(seed)
    p = np.asarray(treat, dtype=float)
    b = np.asarray(control, dtype=float)
    deltas = np.empty(n)
    pis = np.empty(n)
    for i in range(n):
        ps = rng.choice(p, size=len(p), replace=True)
        bs = rng.choice(b, size=len(b), replace=True)
        deltas[i] = iqm(ps) - iqm(bs)
        pis[i] = prob_improvement(ps, bs)
    return {
        "delta_iqm": iqm(p) - iqm(b),
        "delta_iqm_ci90": np.percentile(deltas, [5, 95]).tolist(),
        "delta_iqm_ci95": np.percentile(deltas, [2.5, 97.5]).tolist(),
        "pi": prob_improvement(p, b),
        "pi_ci90": np.percentile(pis, [5, 95]).tolist(),
        "pi_ci95": np.percentile(pis, [2.5, 97.5]).tolist(),
        "n_treat": int(len(p)),
        "n_control": int(len(b)),
        "iqm_treat": iqm(p),
        "iqm_control": iqm(b),
        "rel_iqm_lift": (iqm(p) - iqm(b)) / max(abs(iqm(b)), 1e-9),
    }


def fetch_arm(entity, project, tag, min_steps=20_000_000):
    """Pull all metrics for finished + crashed runs that reached min_steps."""
    import wandb
    api = wandb.Api()
    runs = list(api.runs(
        f"{entity}/{project}",
        filters={"tags": tag, "state": {"$in": ["finished", "crashed"]}}
    ))
    rows = []
    skipped = 0
    states = {}
    for r in runs:
        steps = r.summary.get("_step", 0)
        primary = r.summary.get(PRIMARY_METRIC)
        if primary is None or not np.isfinite(primary) or steps < min_steps:
            skipped += 1
            continue
        states[r.state] = states.get(r.state, 0) + 1
        row = {"id": r.id}
        for m in DIAGNOSTIC_METRICS:
            v = r.summary.get(m)
            row[m] = float(v) if v is not None and np.isfinite(v) else float("nan")
        rows.append(row)
    return rows, skipped, states


def column(rows, key):
    return [r[key] for r in rows if not np.isnan(r[key])]


def report_arm(name, rows):
    if not rows:
        print(f"  {name}: NO RUNS")
        return
    print(f"  {name}: n={len(rows)}")
    for m in DIAGNOSTIC_METRICS:
        col = column(rows, m)
        if not col:
            continue
        arr = np.asarray(col)
        label = m.replace("env/", "")
        print(f"    {label:28s}  mean={arr.mean():7.3f}  median={np.median(arr):7.3f}  "
              f"iqm={iqm(arr):7.3f}  p90={np.percentile(arr,90):7.3f}  max={arr.max():7.3f}")


def report_top_k(name, rows, k):
    if not rows:
        return
    sorted_rows = sorted(rows, key=lambda r: -r[PRIMARY_METRIC])[:k]
    print(f"  {name} top-{k} by {PRIMARY_METRIC.split('/')[-1]}:")
    for r in sorted_rows:
        print(f"    {r['id']}  obj={r[PRIMARY_METRIC]:.3f}  score={r['env/score_normal']:.3f}  "
              f"win={r['env/wins_normal']:.3f}  zhp={r['env/min_zuk_hp_normal']:.0f}  "
              f"ret={r['env/episode_return_normal']:.3f}  "
              f"phase={r['env/phase_reached_normal']:.2f}  death_t={r['env/death_tick_normal']:.0f}")


def hacking_diagnostics(name, rows, control_rows):
    """Flag reward-hacking signals: high return but flat score, early death,
    flat phase_reached, etc. Compares variant top-10 to control top-10."""
    if not rows or not control_rows:
        return
    top_v = sorted(rows, key=lambda r: -r[PRIMARY_METRIC])[:10]
    top_c = sorted(control_rows, key=lambda r: -r[PRIMARY_METRIC])[:10]

    def med(xs, key):
        col = [r[key] for r in xs if not np.isnan(r[key])]
        return float(np.median(col)) if col else float("nan")

    score_v = med(top_v, "env/score_normal")
    score_c = med(top_c, "env/score_normal")
    ret_v = med(top_v, "env/episode_return_normal")
    ret_c = med(top_c, "env/episode_return_normal")
    death_v = med(top_v, "env/death_tick_normal")
    death_c = med(top_c, "env/death_tick_normal")
    phase_v = med(top_v, "env/phase_reached_normal")
    phase_c = med(top_c, "env/phase_reached_normal")

    print(f"  {name} top-10 vs control top-10:")
    print(f"    score_normal      median: {score_v:.3f} vs {score_c:.3f}  delta={score_v-score_c:+.3f}")
    print(f"    phase_reached     median: {phase_v:.2f} vs {phase_c:.2f}  delta={phase_v-phase_c:+.2f}")
    print(f"    death_tick_normal median: {death_v:.0f} vs {death_c:.0f}  delta={death_v-death_c:+.0f}")
    print(f"    episode_return    median: {ret_v:.3f} vs {ret_c:.3f}  (return is reward-coupled, not comparable across reward variants — diagnostic only)")
    flags = []
    if score_v - score_c < 0.02 and ret_v - ret_c > 0.5:
        flags.append("possible reward hacking: return up, score flat")
    if death_v < death_c - 5 and score_v < score_c + 0.05:
        flags.append("possible suicide perversity: dying earlier without progress")
    if phase_v < phase_c - 0.05:
        flags.append("phase progression flat or worse than control")
    if flags:
        print(f"    flags: {'; '.join(flags)}")
    else:
        print(f"    flags: none")


def gate_screening(stats, score_lift_top10):
    return (
        stats["rel_iqm_lift"] >= 0.20
        and stats["pi"] >= 0.70
        and score_lift_top10 >= 0.05
    )


def gate_strong_evidence(rows, n_threshold_wins=2, p50_threshold=300, score_threshold=0.75):
    if not rows:
        return False, "no rows"
    top10 = sorted(rows, key=lambda r: -r[PRIMARY_METRIC])[:10]
    win_runs = [r for r in top10 if r["env/wins_normal"] > 1e-6]
    score_arr = np.asarray([r["env/score_normal"] for r in rows if not np.isnan(r["env/score_normal"])])
    zhp_arr = np.asarray([r["env/min_zuk_hp_normal"] for r in rows if not np.isnan(r["env/min_zuk_hp_normal"])])
    reasons = []
    if len(win_runs) >= n_threshold_wins:
        reasons.append(f">= {n_threshold_wins} top-10 runs with normal-start wins")
    if zhp_arr.size and float(np.percentile(zhp_arr, 50)) < p50_threshold:
        reasons.append(f"normal min_zuk_hp p50 < {p50_threshold}")
    if score_arr.size and float(score_arr.max()) >= score_threshold:
        reasons.append(f"some run hit score_normal >= {score_threshold}")
    return bool(reasons), reasons


def gate_clean_nogo(stats, rows):
    if not rows:
        return True, ["no rows"]
    win_arr = np.asarray([r["env/wins_normal"] for r in rows if not np.isnan(r["env/wins_normal"])])
    iqm_lift_low = stats["rel_iqm_lift"] <= 0.10
    no_wins = win_arr.size == 0 or float(win_arr.max()) <= 1e-6
    if iqm_lift_low and no_wins:
        return True, [f"IQM lift {stats['rel_iqm_lift']:+.1%} <= 10%", "no normal-start wins"]
    return False, []


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--entity", default="valtterivalo-clock-cloud")
    ap.add_argument("--project", default="puffer4")
    ap.add_argument("--top-k", type=int, default=10)
    ap.add_argument("--variants", nargs="+", default=list(VARIANT_TAGS.keys()))
    args = ap.parse_args()

    print(f"Fetching control ({CONTROL_TAG}) ...")
    control_rows, c_skip, c_states = fetch_arm(args.entity, args.project, CONTROL_TAG)
    print(f"  control: {c_states}  skipped={c_skip}\n")

    variant_rows = {}
    for v in args.variants:
        tag = VARIANT_TAGS[v]
        print(f"Fetching {v} ({tag}) ...")
        rows, skip, states = fetch_arm(args.entity, args.project, tag)
        print(f"  {v}: {states}  skipped={skip}")
        variant_rows[v] = rows
    print()

    if not control_rows:
        print("ERROR: control has no usable runs")
        sys.exit(1)

    print("=" * 80)
    print("Per-arm summary stats:")
    print("=" * 80)
    report_arm("control (current_widened)", control_rows)
    for v in args.variants:
        report_arm(f"{v} ({VARIANT_TAGS[v]})", variant_rows.get(v, []))
    print()

    print("=" * 80)
    print(f"Top-{args.top_k} per arm:")
    print("=" * 80)
    report_top_k("control", control_rows, args.top_k)
    for v in args.variants:
        report_top_k(v, variant_rows.get(v, []), args.top_k)
    print()

    control_primary = column(control_rows, PRIMARY_METRIC)

    for v in args.variants:
        rows = variant_rows.get(v, [])
        if not rows:
            print(f"{v}: NO DATA, skipping")
            continue
        print("=" * 80)
        print(f"Variant {v} vs control:")
        print("=" * 80)
        treat_primary = column(rows, PRIMARY_METRIC)

        hacking_diagnostics(v, rows, control_rows)
        print()

        s = bootstrap(treat_primary, control_primary)
        print(f"  Bootstrap (n_treat={s['n_treat']}, n_control={s['n_control']}, 10000 resamples):")
        print(f"    iqm(control) = {s['iqm_control']:.4f}")
        print(f"    iqm({v})     = {s['iqm_treat']:.4f}")
        print(f"    delta_iqm    = {s['delta_iqm']:+.4f}")
        print(f"    95% CI       = [{s['delta_iqm_ci95'][0]:+.4f}, {s['delta_iqm_ci95'][1]:+.4f}]")
        print(f"    PI({v}>ctrl) = {s['pi']:.3f}  (95% CI [{s['pi_ci95'][0]:.3f}, {s['pi_ci95'][1]:.3f}])")
        print(f"    rel lift     = {s['rel_iqm_lift']:+.1%}")

        top_v = sorted(rows, key=lambda r: -r[PRIMARY_METRIC])[:10]
        top_c = sorted(control_rows, key=lambda r: -r[PRIMARY_METRIC])[:10]
        score_v_med = float(np.median([r["env/score_normal"] for r in top_v if not np.isnan(r["env/score_normal"])]))
        score_c_med = float(np.median([r["env/score_normal"] for r in top_c if not np.isnan(r["env/score_normal"])]))
        score_lift = score_v_med - score_c_med

        screen_pass = gate_screening(s, score_lift)
        strong, reasons = gate_strong_evidence(rows)
        nogo, nogo_reasons = gate_clean_nogo(s, rows)
        print()
        print(f"  Gates:")
        print(f"    screening (lift>=20% AND PI>=0.70 AND top10 score lift>=0.05): {'PASS' if screen_pass else 'FAIL'}")
        print(f"      top10 score_normal lift = {score_lift:+.3f}")
        print(f"    strong evidence: {'YES' if strong else 'no'}")
        for r in reasons: print(f"      + {r}")
        print(f"    clean no-go: {'YES' if nogo else 'no'}")
        for r in nogo_reasons: print(f"      + {r}")
        print()


if __name__ == "__main__":
    main()
