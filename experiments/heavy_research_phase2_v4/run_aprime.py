"""Stage A' runner: fixed-hparam continuations for top R1 configs at 200M.

A1: 3 v3 configs x 5 seeds = 15 runs (v3 reward)
A2: 2 control configs x 3 seeds = 6 runs  (control reward)
Optional A3: extra-long tail at 400M, gated on A1 still climbing

Reads aprime_hparams.json (produced by pull_aprime_hparams.py).
Launches sequentially via subprocess; tags every run for downstream filtering.

Run from repo root:
  python experiments/heavy_research_phase2_v4/run_aprime.py            # full A1 + A2
  python experiments/heavy_research_phase2_v4/run_aprime.py --dry-run  # print commands only
  python experiments/heavy_research_phase2_v4/run_aprime.py --only A1  # just v3 cells
  python experiments/heavy_research_phase2_v4/run_aprime.py --only A2  # just control cells
"""

import argparse
import json
import os
import subprocess
import sys
import time
from pathlib import Path


REPO = Path(__file__).resolve().parents[2]
HPARAMS_JSON = REPO / "experiments/heavy_research_phase2_v4/aprime_hparams.json"
PROPOSAL = REPO / "checkpoints/osrs_inferno/cdevk9pk/0000000029982720.bin"
V3_BASE = REPO / "experiments/heavy_research_phase2_v4/sweeps/inferno_aprime_v3_base.ini"
CTRL_BASE = REPO / "experiments/heavy_research_phase2_v4/sweeps/inferno_aprime_ctrl_base.ini"

A1_TOP_K = 3        # top 3 v3 configs
A1_SEEDS = [1, 2, 3, 4, 5]
A2_TOP_K = 2        # top 2 control configs
A2_SEEDS = [1, 2, 3]
TOTAL_TIMESTEPS = 200_000_000


def hparam_cli_args(hparams):
    """train.learning_rate=0.005 -> ['--train.learning-rate', '0.005']."""
    out = []
    for key, value in hparams.items():
        if value is None:
            continue
        cli_key = "--" + key.replace("_", "-")
        out += [cli_key, str(value)]
    return out


def run_cell(arm, run_id, hparams, seed, dry_run, log_dir):
    base_ini = V3_BASE if arm == "A1_v3" else CTRL_BASE
    log_subdir = f"r2_aprime/{arm}/{run_id}_s{seed}"
    tag = f"stage-r2-aprime-{arm.lower().replace('_','-')}-{run_id}-s{seed}"

    env = os.environ.copy()
    env["PUFFER_CONFIG_FILE"] = str(base_ini)

    cmd = [
        sys.executable, "-m", "pufferlib.pufferl", "train", "osrs_inferno",
        "--load-model-path", str(PROPOSAL),
        "--train.total-timesteps", str(TOTAL_TIMESTEPS),
        "--train.seed", str(seed),
        "--log-dir", f"logs/{log_subdir}",
        "--tag", tag,
        "--wandb",
    ]
    cmd += hparam_cli_args(hparams)

    print(f"\n=== {time.strftime('%Y-%m-%d %H:%M:%S')} cell {arm} {run_id} seed={seed} ===", flush=True)
    print(" ".join(cmd), flush=True)

    if dry_run:
        return 0

    log_file = log_dir / f"{tag}.log"
    log_dir.mkdir(parents=True, exist_ok=True)
    t0 = time.time()
    with open(log_file, "w") as f:
        result = subprocess.run(cmd, env=env, stdout=f, stderr=subprocess.STDOUT)
    elapsed = time.time() - t0
    print(f"  -> exit={result.returncode}  elapsed={elapsed/60:.1f}m  log={log_file}", flush=True)
    return result.returncode


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--dry-run", action="store_true")
    ap.add_argument("--only", choices=["A1", "A2"], default=None,
                    help="Run only A1 (v3) or A2 (control). Default: both.")
    args = ap.parse_args()

    if not HPARAMS_JSON.exists():
        print(f"ERROR: {HPARAMS_JSON} not found. Run pull_aprime_hparams.py first.", file=sys.stderr)
        return 1
    if not PROPOSAL.exists():
        print(f"ERROR: proposal checkpoint not found at {PROPOSAL}", file=sys.stderr)
        return 1

    rows = json.load(open(HPARAMS_JSON))
    a1 = [r for r in rows if r["arm"] == "A1_v3"][:A1_TOP_K]
    a2 = [r for r in rows if r["arm"] == "A2_ctrl"][:A2_TOP_K]

    print(f"A1 cells: {len(a1) * len(A1_SEEDS)}  ({A1_TOP_K} configs x {len(A1_SEEDS)} seeds)")
    for r in a1:
        print(f"  v3 {r['run_id']}  prior_obj={r['summary'].get('env/zuk_objective_normal'):.3f}")
    print(f"A2 cells: {len(a2) * len(A2_SEEDS)}  ({A2_TOP_K} configs x {len(A2_SEEDS)} seeds)")
    for r in a2:
        print(f"  ctrl {r['run_id']}  prior_obj={r['summary'].get('env/zuk_objective_normal'):.3f}")

    log_dir = REPO / "experiments/heavy_research_phase2_v4/aprime_logs"
    failures = []

    schedule = []
    if args.only != "A2":
        for r in a1:
            for seed in A1_SEEDS:
                schedule.append((r, seed))
    if args.only != "A1":
        for r in a2:
            for seed in A2_SEEDS:
                schedule.append((r, seed))

    print(f"\nTotal cells to run: {len(schedule)}")
    if args.dry_run:
        print("\n--- DRY RUN ---")
    print()

    for i, (r, seed) in enumerate(schedule, 1):
        print(f"[{i}/{len(schedule)}] starting...", flush=True)
        rc = run_cell(r["arm"], r["run_id"], r["hparams"], seed, args.dry_run, log_dir)
        if rc != 0:
            failures.append((r["arm"], r["run_id"], seed, rc))

    print(f"\n=== A' done. {len(schedule) - len(failures)}/{len(schedule)} succeeded ===")
    for f in failures:
        print(f"  FAILED: {f}")
    return 0 if not failures else 1


if __name__ == "__main__":
    sys.exit(main())
