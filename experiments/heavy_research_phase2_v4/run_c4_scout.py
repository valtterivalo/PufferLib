"""Stage C4 scout: phase 2 with three demo-generation strategies.

Per heavy agent r3: frontier-biased archive_sample is the next lever, but
the demo q ceiling didn't change much in our G1/G2 generation. Test whether
more demo diversity (wider q range) helps even at lower max q, or whether
old demos (highest single-leaf q) win.

Three arms x 4 seeds = 12 cells x 50M (~42 min total).

  C4-A control: demos_c2/seed_43 (max q 0.868, narrow distribution, original
                round-2 demo set that produced p2k4szzs)
  C4-B G1:     demos_g1/seed_44 (frontier_mode floor=0.80 power=4 200 iter,
                max q 0.822, wider q range)
  C4-C G2:     demos_g2/seed_46 (frontier_mode floor=0.85 power=8 300 iter,
                max q 0.831, widest q range, largest archive)

Same starting checkpoint (p2k4szzs) and hparams as round 2/3.

Run from repo root:
  python experiments/heavy_research_phase2_v4/run_c4_scout.py
"""

import argparse
import os
import subprocess
import sys
import time
from pathlib import Path


REPO = Path(__file__).resolve().parents[2]
PROPOSAL = REPO / "checkpoints/osrs_inferno/p2k4szzs/0000000049971200.bin"
BASE_INI = REPO / "experiments/heavy_research_phase2_v4/sweeps/inferno_c_base.ini"
LOG_BASE = REPO / "experiments/heavy_research_phase2_v4/c4_logs"
TIMESTEPS = 50_000_000

DEMO_A = REPO / "experiments/heavy_research_phase2_v4/demos_baseline/seed_50/demos"
DEMO_B = REPO / "experiments/heavy_research_phase2_v4/demos_g1/seed_44/demos"
DEMO_C = REPO / "experiments/heavy_research_phase2_v4/demos_g2/seed_46/demos"

SCHEDULE = [
    {"label": "A-baseline", "demos": DEMO_A, "seed": 1, "nsf": 0.65},
    {"label": "A-baseline", "demos": DEMO_A, "seed": 2, "nsf": 0.65},
    {"label": "A-baseline", "demos": DEMO_A, "seed": 3, "nsf": 0.65},
    {"label": "A-baseline", "demos": DEMO_A, "seed": 4, "nsf": 0.65},
    {"label": "B-g1", "demos": DEMO_B, "seed": 1, "nsf": 0.65},
    {"label": "B-g1", "demos": DEMO_B, "seed": 2, "nsf": 0.65},
    {"label": "B-g1", "demos": DEMO_B, "seed": 3, "nsf": 0.65},
    {"label": "B-g1", "demos": DEMO_B, "seed": 4, "nsf": 0.65},
    {"label": "C-g2", "demos": DEMO_C, "seed": 1, "nsf": 0.65},
    {"label": "C-g2", "demos": DEMO_C, "seed": 2, "nsf": 0.65},
    {"label": "C-g2", "demos": DEMO_C, "seed": 3, "nsf": 0.65},
    {"label": "C-g2", "demos": DEMO_C, "seed": 4, "nsf": 0.65},
]


def cli_for_cell(cell):
    tag = f"stage-r2-c4-{cell['label']}-s{cell['seed']}"
    log_subdir = f"r2_c4/{cell['label']}_s{cell['seed']}"

    cmd = [
        sys.executable, "-m", "pufferlib.pufferl", "train", "osrs_inferno",
        "--load-model-path", str(PROPOSAL),
        "--train.total-timesteps", str(TIMESTEPS),
        "--train.seed", str(cell["seed"]),
        "--log-dir", f"logs/{log_subdir}",
        "--tag", tag,
        "--wandb",
        "--env.phase2-demo-dir", str(cell["demos"]),
        "--env.phase2-normal-start-frac", str(cell["nsf"]),
        "--env.phase2-bc-coef", "0.0",
        "--env.phase2-bc-demos-per-minibatch", "0",
    ]
    return cmd, tag


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--dry-run", action="store_true")
    args = ap.parse_args()

    for d in [DEMO_A, DEMO_B, DEMO_C]:
        if not d.exists():
            print(f"ERROR: demo dir missing: {d}", file=sys.stderr)
            return 1

    print(f"C4: {len(SCHEDULE)} cells, ~{len(SCHEDULE) * 4} min total")
    LOG_BASE.mkdir(parents=True, exist_ok=True)

    failures = []
    for i, cell in enumerate(SCHEDULE, 1):
        cmd, tag = cli_for_cell(cell)
        print(f"\n[{i}/{len(SCHEDULE)}] starting...", flush=True)
        print(f"=== {time.strftime('%H:%M:%S')} {tag} ===", flush=True)
        if args.dry_run:
            print(" ".join(cmd), flush=True)
            continue

        log_file = LOG_BASE / f"{tag}.log"
        env = os.environ.copy()
        env["PUFFER_CONFIG_FILE"] = str(BASE_INI)

        t0 = time.time()
        with open(log_file, "w") as f:
            result = subprocess.run(cmd, env=env, stdout=f, stderr=subprocess.STDOUT)
        elapsed = time.time() - t0
        print(f"  -> exit={result.returncode}  elapsed={elapsed/60:.1f}m", flush=True)
        if result.returncode != 0:
            failures.append((cell["label"], cell["seed"], result.returncode))

    print(f"\n=== C4 done. {len(SCHEDULE) - len(failures)}/{len(SCHEDULE)} succeeded ===")
    return 0 if not failures else 1


if __name__ == "__main__":
    sys.exit(main())
