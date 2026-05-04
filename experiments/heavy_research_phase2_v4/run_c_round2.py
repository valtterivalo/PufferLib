"""Stage C round 2: iterated phase 2 from C1 frontier (ggzxso9c).

Round 1 found C1 phase-2-no-BC works (top10_med +0.103 over C0, fr<=300 5x).
Best cell ggzxso9c hit 0.7649 with min_hp 282, the new frontier.
C2 (BC) collapsed in 3 of 4 cells, dropping that arm.

Round 2 design:
  proposal: ggzxso9c final ckpt (50M, score 0.765)
  demos: refreshed from ggzxso9c (q=0.842-0.845)
  arms: 8 cells, all phase 2 no BC
    nsf in {0.65, 0.70, 0.75, 0.80} x 2 seeds = 8
  no BC arm (round 1 collapsed)

Run from repo root:
  python experiments/heavy_research_phase2_v4/run_c_round2.py
"""

import argparse
import os
import subprocess
import sys
import time
from pathlib import Path


REPO = Path(__file__).resolve().parents[2]
PROPOSAL = REPO / "checkpoints/osrs_inferno/ggzxso9c/0000000049971200.bin"
BASE_INI = REPO / "experiments/heavy_research_phase2_v4/sweeps/inferno_c_base.ini"
DEMO_DIR = REPO / "experiments/heavy_research_phase2_v4/demos_c1/seed_42/demos"
LOG_BASE = REPO / "experiments/heavy_research_phase2_v4/c_round2_logs"
TIMESTEPS = 50_000_000

SCHEDULE = [
    {"label": "p2-nsf65", "seed": 1, "nsf": 0.65},
    {"label": "p2-nsf65", "seed": 2, "nsf": 0.65},
    {"label": "p2-nsf70", "seed": 1, "nsf": 0.70},
    {"label": "p2-nsf70", "seed": 2, "nsf": 0.70},
    {"label": "p2-nsf75", "seed": 1, "nsf": 0.75},
    {"label": "p2-nsf75", "seed": 2, "nsf": 0.75},
    {"label": "p2-nsf80", "seed": 1, "nsf": 0.80},
    {"label": "p2-nsf80", "seed": 2, "nsf": 0.80},
]


def cli_for_cell(cell):
    tag = f"stage-r2-c-rd2-{cell['label']}-s{cell['seed']}"
    log_subdir = f"r2_c_rd2/{cell['label']}_s{cell['seed']}"

    cmd = [
        sys.executable, "-m", "pufferlib.pufferl", "train", "osrs_inferno",
        "--load-model-path", str(PROPOSAL),
        "--train.total-timesteps", str(TIMESTEPS),
        "--train.seed", str(cell["seed"]),
        "--log-dir", f"logs/{log_subdir}",
        "--tag", tag,
        "--wandb",
        "--env.phase2-demo-dir", str(DEMO_DIR),
        "--env.phase2-normal-start-frac", str(cell["nsf"]),
        "--env.phase2-bc-coef", "0.0",
        "--env.phase2-bc-demos-per-minibatch", "0",
    ]
    return cmd, tag


def run_cell(cell, dry_run, log_dir):
    cmd, tag = cli_for_cell(cell)
    print(f"\n=== {time.strftime('%Y-%m-%d %H:%M:%S')} {cell['label']} s{cell['seed']} ===",
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
    ap.add_argument("--dry-run", action="store_true")
    args = ap.parse_args()

    if not PROPOSAL.exists():
        print(f"ERROR: proposal {PROPOSAL} not found", file=sys.stderr)
        return 1
    if not DEMO_DIR.exists():
        print(f"ERROR: demos {DEMO_DIR} not found", file=sys.stderr)
        return 1

    print(f"C round 2: {len(SCHEDULE)} cells, ~{len(SCHEDULE) * 4} min total")
    if args.dry_run:
        print("\n--- DRY RUN ---\n")
    failures = []
    for i, cell in enumerate(SCHEDULE, 1):
        print(f"[{i}/{len(SCHEDULE)}] starting...", flush=True)
        rc = run_cell(cell, args.dry_run, LOG_BASE)
        if rc != 0:
            failures.append((cell["label"], cell["seed"], rc))

    print(f"\n=== C round 2 done. {len(SCHEDULE) - len(failures)}/{len(SCHEDULE)} succeeded ===")
    for f in failures:
        print(f"  FAILED: {f}")
    return 0 if not failures else 1


if __name__ == "__main__":
    sys.exit(main())
