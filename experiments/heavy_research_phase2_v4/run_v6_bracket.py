"""v6 reward scout: 3 arms x 4 seeds x 50M (heavy agent r5 step 2).

Arms:
  A v3      = current production reward, no v6 add rewards.
  B v6      = v6-soft-E: jad/healer/set damage rewards + kill bonuses,
              no Zuk multiplier.
  C v6+xfer = B + post_jad_zuk_multiplier=1.5 (Option 2 transition reward
              that incentivises returning to Zuk after Jad dies).

Same proposal (p2k4szzs), same hparams (e3vyhuh9 from inferno_c_base.ini).
Same starting demo set as round-4 baseline (demos_baseline/seed_50).

Runs ~42 min wall on M4 Pro (12 cells x 3.5 min).

Usage:
  python experiments/heavy_research_phase2_v4/run_v6_bracket.py
  python experiments/heavy_research_phase2_v4/run_v6_bracket.py --dry-run
"""

import argparse
import os
import subprocess
import sys
import time
from pathlib import Path


REPO = Path(__file__).resolve().parents[2]
PROPOSAL = REPO / "checkpoints/osrs_inferno/p2k4szzs/0000000049971200.bin"
BASE_INI = REPO / "experiments/heavy_research_phase2_v4/sweeps/inferno_c_base_v6.ini"
LOG_BASE = REPO / "experiments/heavy_research_phase2_v4/v6_logs"
DEMOS = REPO / "experiments/heavy_research_phase2_v4/demos_v6_baseline/seed_50/demos"
TIMESTEPS = 50_000_000

V6_DAMAGE_COEFFS = {
    "jad_damage_reward_coeff": 0.004,
    "zuk_healer_damage_reward_coeff": 0.006,
    "set_damage_reward_coeff": 0.002,
    "jad_kill_bonus": 0.35,
    "zuk_healer_kill_bonus": 0.12,
    "set_kill_bonus": 0.08,
}

ARMS = [
    ("v3-control", {
        "post_jad_zuk_multiplier": 1.0,
        "jad_alive_zuk_multiplier": 1.0,
    }),
    ("v6-soft-e", {
        **V6_DAMAGE_COEFFS,
        "post_jad_zuk_multiplier": 1.0,
        "jad_alive_zuk_multiplier": 1.0,
    }),
    ("v6-xfer", {
        **V6_DAMAGE_COEFFS,
        "post_jad_zuk_multiplier": 1.5,
        "jad_alive_zuk_multiplier": 1.0,
    }),
]
SEEDS = [1, 2, 3, 4]


def cli_for(arm_label, arm_cfg, seed):
    tag = f"stage-r5-v6-{arm_label}-s{seed}"
    log_subdir = f"r5_v6/{arm_label}_s{seed}"
    cmd = [
        sys.executable, "-m", "pufferlib.pufferl", "train", "osrs_inferno",
        "--load-model-path", str(PROPOSAL),
        "--train.total-timesteps", str(TIMESTEPS),
        "--train.seed", str(seed),
        "--log-dir", f"logs/{log_subdir}",
        "--tag", tag,
        "--wandb",
        "--env.phase2-demo-dir", str(DEMOS),
        "--env.phase2-normal-start-frac", "0.65",
        "--env.phase2-bc-coef", "0.0",
        "--env.phase2-bc-demos-per-minibatch", "0",
    ]
    for k, v in arm_cfg.items():
        cmd.append(f"--env.{k.replace('_','-')}")
        cmd.append(str(v))
    return cmd, tag


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--dry-run", action="store_true")
    args = ap.parse_args()

    cells = [(arm_label, arm_cfg, seed) for arm_label, arm_cfg in ARMS for seed in SEEDS]
    print(f"v6 bracket: {len(cells)} cells, ~{len(cells) * 4} min total")
    LOG_BASE.mkdir(parents=True, exist_ok=True)

    failures = []
    for i, (arm_label, arm_cfg, seed) in enumerate(cells, 1):
        cmd, tag = cli_for(arm_label, arm_cfg, seed)
        print(f"\n[{i}/{len(cells)}] {time.strftime('%H:%M:%S')} {tag}", flush=True)
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
            failures.append((arm_label, seed, result.returncode))

    print(f"\n=== v6 bracket done. {len(cells) - len(failures)}/{len(cells)} succeeded ===")
    return 0 if not failures else 1


if __name__ == "__main__":
    sys.exit(main())
