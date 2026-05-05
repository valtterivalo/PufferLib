"""Stage B0: horizon=256 probe at phase-2 frontier.

Heavy agent's round-2 plan listed B0 as the cheapest architecture probe:
  'B0: same architecture, horizon 256
   load v3 best proposal
   50M to 100M, 5 seeds'

Horizon doubles rollout depth without changing weight shapes - load-compatible.
Tests whether longer per-update trajectories help long-horizon credit.

3 seeds x 50M from p2k4szzs at nsf=0.65 + horizon=256.

Run from repo root:
  python experiments/heavy_research_phase2_v4/run_b0_horizon.py
"""

import os
import subprocess
import sys
import time
from pathlib import Path


REPO = Path(__file__).resolve().parents[2]
PROPOSAL = REPO / "checkpoints/osrs_inferno/p2k4szzs/0000000049971200.bin"
BASE_INI = REPO / "experiments/heavy_research_phase2_v4/sweeps/inferno_c_base.ini"
DEMO_DIR = REPO / "experiments/heavy_research_phase2_v4/demos_c2/seed_43/demos"
LOG_BASE = REPO / "experiments/heavy_research_phase2_v4/b0_logs"
TIMESTEPS = 50_000_000
HORIZON = 256
NSF = 0.65


def cli_for_cell(seed):
    tag = f"stage-r2-b0-h256-s{seed}"
    log_subdir = f"r2_b0/h256_s{seed}"

    cmd = [
        sys.executable, "-m", "pufferlib.pufferl", "train", "osrs_inferno",
        "--load-model-path", str(PROPOSAL),
        "--train.total-timesteps", str(TIMESTEPS),
        "--train.horizon", str(HORIZON),
        "--train.minibatch-size", "4096",
        "--train.seed", str(seed),
        "--log-dir", f"logs/{log_subdir}",
        "--tag", tag,
        "--wandb",
        "--env.phase2-demo-dir", str(DEMO_DIR),
        "--env.phase2-normal-start-frac", str(NSF),
        "--env.phase2-bc-coef", "0.0",
        "--env.phase2-bc-demos-per-minibatch", "0",
    ]
    return cmd, tag


def main():
    if not PROPOSAL.exists():
        print(f"ERROR: proposal {PROPOSAL} not found", file=sys.stderr)
        return 1
    if not DEMO_DIR.exists():
        print(f"ERROR: demos {DEMO_DIR} not found", file=sys.stderr)
        return 1

    seeds = [1, 2, 3]
    print(f"B0 horizon={HORIZON}: {len(seeds)} seeds")
    LOG_BASE.mkdir(parents=True, exist_ok=True)

    failures = []
    for i, seed in enumerate(seeds, 1):
        print(f"\n[{i}/{len(seeds)}] starting...", flush=True)
        cmd, tag = cli_for_cell(seed)
        print(f"=== {time.strftime('%H:%M:%S')} {tag} ===", flush=True)
        print(" ".join(cmd), flush=True)

        log_file = LOG_BASE / f"{tag}.log"
        env = os.environ.copy()
        env["PUFFER_CONFIG_FILE"] = str(BASE_INI)

        t0 = time.time()
        with open(log_file, "w") as f:
            result = subprocess.run(cmd, env=env, stdout=f, stderr=subprocess.STDOUT)
        elapsed = time.time() - t0
        print(f"  -> exit={result.returncode}  elapsed={elapsed/60:.1f}m  log={log_file}",
              flush=True)
        if result.returncode != 0:
            failures.append((seed, result.returncode))

    print(f"\n=== B0 done. {len(seeds) - len(failures)}/{len(seeds)} succeeded ===")
    return 0 if not failures else 1


if __name__ == "__main__":
    sys.exit(main())
