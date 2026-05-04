"""Stage C scout: phase-2 with v3-best proposal and v3-derived demos.

Launches sequential 50M training runs across three arms, sharing
inferno_c_base.ini and overriding phase-2 knobs + seed via CLI.

Arms (heavy-agent spec, simplified for overnight budget):
  C0: PPO continuation control (no phase 2)              4 seeds
  C1: phase 2, no BC, normal_start_frac in {0.75, 0.85}  4 seeds (2x2)
  C2: phase 2 + BC, bc_coef in {0.001, 0.005}            4 seeds (2x2)
Total: 12 cells x 50M = ~45-60 min wall time.

Run from repo root:
  python experiments/heavy_research_phase2_v4/run_c_scout.py
"""

import argparse
import os
import subprocess
import sys
import time
from pathlib import Path


REPO = Path(__file__).resolve().parents[2]
PROPOSAL = REPO / "checkpoints/osrs_inferno/v3xzk1qs/0000000199983104.bin"
BASE_INI = REPO / "experiments/heavy_research_phase2_v4/sweeps/inferno_c_base.ini"
DEMO_DIR = REPO / "experiments/heavy_research_phase2_v4/demos_p0/seed_42/demos"
LOG_BASE = REPO / "experiments/heavy_research_phase2_v4/c_logs"
TIMESTEPS = 50_000_000

# Heavy agent's full ranges; we sample a subset.
SCHEDULE = [
    # C0: PPO continuation control, no phase 2.
    {"arm": "C0", "label": "ppo-control", "seed": 1, "phase2": False},
    {"arm": "C0", "label": "ppo-control", "seed": 2, "phase2": False},
    {"arm": "C0", "label": "ppo-control", "seed": 3, "phase2": False},
    {"arm": "C0", "label": "ppo-control", "seed": 4, "phase2": False},

    # C1: phase 2 curriculum, no BC. Vary normal_start_frac.
    {"arm": "C1", "label": "p2-nsf75", "seed": 1, "phase2": True, "nsf": 0.75, "bc": 0.0},
    {"arm": "C1", "label": "p2-nsf75", "seed": 2, "phase2": True, "nsf": 0.75, "bc": 0.0},
    {"arm": "C1", "label": "p2-nsf85", "seed": 1, "phase2": True, "nsf": 0.85, "bc": 0.0},
    {"arm": "C1", "label": "p2-nsf85", "seed": 2, "phase2": True, "nsf": 0.85, "bc": 0.0},

    # C2: phase 2 + tiny BC. Vary bc_coef.
    {"arm": "C2", "label": "p2-bc001", "seed": 1, "phase2": True, "nsf": 0.80, "bc": 0.001},
    {"arm": "C2", "label": "p2-bc001", "seed": 2, "phase2": True, "nsf": 0.80, "bc": 0.001},
    {"arm": "C2", "label": "p2-bc005", "seed": 1, "phase2": True, "nsf": 0.80, "bc": 0.005},
    {"arm": "C2", "label": "p2-bc005", "seed": 2, "phase2": True, "nsf": 0.80, "bc": 0.005},
]


def cli_for_cell(cell):
    """Build the train command for one cell."""
    tag = f"stage-r2-c-{cell['arm'].lower()}-{cell['label']}-s{cell['seed']}"
    log_subdir = f"r2_c/{cell['arm']}/{cell['label']}_s{cell['seed']}"

    cmd = [
        sys.executable, "-m", "pufferlib.pufferl", "train", "osrs_inferno",
        "--load-model-path", str(PROPOSAL),
        "--train.total-timesteps", str(TIMESTEPS),
        "--train.seed", str(cell["seed"]),
        "--log-dir", f"logs/{log_subdir}",
        "--tag", tag,
        "--wandb",
    ]
    if cell.get("phase2"):
        cmd += [
            "--env.phase2-demo-dir", str(DEMO_DIR),
            "--env.phase2-normal-start-frac", str(cell["nsf"]),
            "--env.phase2-bc-coef", str(cell["bc"]),
            "--env.phase2-bc-demos-per-minibatch", "2" if cell["bc"] > 0 else "0",
        ]
    return cmd, tag


def run_cell(cell, dry_run, log_dir):
    cmd, tag = cli_for_cell(cell)
    print(f"\n=== {time.strftime('%Y-%m-%d %H:%M:%S')} {cell['arm']} "
          f"{cell['label']} s{cell['seed']} ===", flush=True)
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
    ap.add_argument("--only", default="all", help="comma list of arms (e.g. C0,C1) or 'all'")
    args = ap.parse_args()

    if not PROPOSAL.exists():
        print(f"ERROR: proposal {PROPOSAL} not found", file=sys.stderr)
        return 1
    if not DEMO_DIR.exists():
        print(f"ERROR: demos {DEMO_DIR} not found. "
              f"Run scripts/run_archive_explore.py first.", file=sys.stderr)
        return 1

    arms = SCHEDULE if args.only == "all" else [
        c for c in SCHEDULE if c["arm"] in args.only.split(",")
    ]

    print(f"C scout: {len(arms)} cells, ~{len(arms) * 4} min total")
    if args.dry_run:
        print("\n--- DRY RUN ---\n")
    failures = []
    for i, cell in enumerate(arms, 1):
        print(f"[{i}/{len(arms)}] starting...", flush=True)
        rc = run_cell(cell, args.dry_run, LOG_BASE)
        if rc != 0:
            failures.append((cell["arm"], cell["label"], cell["seed"], rc))

    print(f"\n=== C scout done. {len(arms) - len(failures)}/{len(arms)} succeeded ===")
    for f in failures:
        print(f"  FAILED: {f}")
    return 0 if not failures else 1


if __name__ == "__main__":
    sys.exit(main())
