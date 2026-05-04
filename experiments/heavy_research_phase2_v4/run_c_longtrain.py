"""Stage C long training: 100M continuation from the best phase-2 frontier.

If C rounds 1-3 plateau around score 0.78-0.80, this script tries the next
obvious lever: longer training at the best knob settings. 100M is 2x the
per-round budget but still single-cell ~7 min on M4 Pro.

Edit FRONTIER_CKPT, FRONTIER_DEMOS, and BEST_NSF below before running with
the actual round-3 winner.

Run from repo root:
  python experiments/heavy_research_phase2_v4/run_c_longtrain.py
"""

import argparse
import os
import subprocess
import sys
import time
from pathlib import Path


REPO = Path(__file__).resolve().parents[2]
BASE_INI = REPO / "experiments/heavy_research_phase2_v4/sweeps/inferno_c_base.ini"
LOG_BASE = REPO / "experiments/heavy_research_phase2_v4/c_longtrain_logs"
TIMESTEPS = 100_000_000

# Defaults assume p2k4szzs is still the frontier; override via CLI if round 3
# beats it.
DEFAULT_FRONTIER_CKPT = REPO / "checkpoints/osrs_inferno/p2k4szzs/0000000049971200.bin"
DEFAULT_FRONTIER_DEMOS = REPO / "experiments/heavy_research_phase2_v4/demos_c2/seed_43/demos"
DEFAULT_NSF = 0.65


def cli_for_cell(seed, ckpt, demos, nsf):
    tag = f"stage-r2-c-long-nsf{int(nsf*100):02d}-s{seed}"
    log_subdir = f"r2_c_long/nsf{int(nsf*100):02d}_s{seed}"

    cmd = [
        sys.executable, "-m", "pufferlib.pufferl", "train", "osrs_inferno",
        "--load-model-path", str(ckpt),
        "--train.total-timesteps", str(TIMESTEPS),
        "--train.seed", str(seed),
        "--log-dir", f"logs/{log_subdir}",
        "--tag", tag,
        "--wandb",
        "--env.phase2-demo-dir", str(demos),
        "--env.phase2-normal-start-frac", str(nsf),
        "--env.phase2-bc-coef", "0.0",
        "--env.phase2-bc-demos-per-minibatch", "0",
    ]
    return cmd, tag


def run_cell(seed, ckpt, demos, nsf, dry_run, log_dir):
    cmd, tag = cli_for_cell(seed, ckpt, demos, nsf)
    print(f"\n=== {time.strftime('%Y-%m-%d %H:%M:%S')} long nsf={nsf} s{seed} ===",
          flush=True)
    print(" ".join(cmd), flush=True)

    if dry_run:
        return 0

    log_dir.mkdir(parents=True, exist_ok=True)
    log_file = log_dir / f"{tag}.log"
    env = os.environ.copy()
    env["PUFFER_CONFIG_FILE"] = str(BASE_INI)

    t0 = time.time()
    with open(log_file, "w") as f:
        result = subprocess.run(cmd, env=env, stdout=f, stderr=subprocess.STDOUT)
    elapsed = time.time() - t0
    print(f"  -> exit={result.returncode}  elapsed={elapsed/60:.1f}m  log={log_file}",
          flush=True)
    return result.returncode


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--ckpt", default=str(DEFAULT_FRONTIER_CKPT),
                    help="frontier checkpoint to continue from")
    ap.add_argument("--demos", default=str(DEFAULT_FRONTIER_DEMOS),
                    help="demo dir for phase 2")
    ap.add_argument("--nsf", type=float, default=DEFAULT_NSF,
                    help="phase2_normal_start_frac")
    ap.add_argument("--seeds", default="1,2,3",
                    help="comma list of seeds (default: 1,2,3)")
    ap.add_argument("--dry-run", action="store_true")
    args = ap.parse_args()

    ckpt = Path(args.ckpt)
    demos = Path(args.demos)
    if not ckpt.exists():
        print(f"ERROR: ckpt {ckpt} not found", file=sys.stderr)
        return 1
    if not demos.exists():
        print(f"ERROR: demos {demos} not found", file=sys.stderr)
        return 1

    seeds = [int(s) for s in args.seeds.split(",")]
    print(f"long train: ckpt={ckpt.name}, nsf={args.nsf}, seeds={seeds}, "
          f"~{len(seeds) * 7} min total")
    if args.dry_run:
        print("\n--- DRY RUN ---\n")

    failures = []
    for i, seed in enumerate(seeds, 1):
        print(f"[{i}/{len(seeds)}] starting...", flush=True)
        rc = run_cell(seed, ckpt, demos, args.nsf, args.dry_run, LOG_BASE)
        if rc != 0:
            failures.append((seed, rc))

    print(f"\n=== long train done. {len(seeds) - len(failures)}/{len(seeds)} succeeded ===")
    for f in failures:
        print(f"  FAILED: {f}")
    return 0 if not failures else 1


if __name__ == "__main__":
    sys.exit(main())
