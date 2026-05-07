"""Run the reward-v4 healer-transition Protein sweep on pufferbox4."""

import argparse
import os
import subprocess
from pathlib import Path


SRC_REPO = Path("/puffertank/docker/goexplore_terminal_reset_test")
MAIN_REPO = Path("/puffertank/docker/goexplore")
DEFAULT_CONFIG = (
    SRC_REPO
    / "experiments/heavy_research_phase2_v4/sweeps/inferno_reward_v4_healer_sweep.ini"
)
DEFAULT_CHECKPOINT = (
    MAIN_REPO / "checkpoints/osrs_inferno/p2k4szzs/0000000049971200.bin"
)
DEFAULT_DEMOS = (
    SRC_REPO
    / "experiments/heavy_research_phase2_v4/demos_quality_v3_attack_timer_key/seed_72/demos"
)
DEFAULT_OUT_DIR = (
    MAIN_REPO
    / "experiments/heavy_research_phase2_v4/remote_reward_v4_healer_sweep"
)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--config", type=Path, default=DEFAULT_CONFIG)
    parser.add_argument("--checkpoint", type=Path, default=DEFAULT_CHECKPOINT)
    parser.add_argument("--demo-dir", type=Path, default=DEFAULT_DEMOS)
    parser.add_argument("--out-dir", type=Path, default=DEFAULT_OUT_DIR)
    parser.add_argument("--tag", default="remote-r21-reward-v4-healer-sweep")
    args = parser.parse_args()

    for path in [args.config, args.checkpoint, args.demo_dir]:
        if not path.exists():
            raise SystemExit(f"missing required path: {path}")

    args.out_dir.mkdir(parents=True, exist_ok=True)
    env = os.environ.copy()
    env["CUDA_VISIBLE_DEVICES"] = "0"
    env["PUFFER_CONFIG_FILE"] = str(args.config)
    env["PUFFER_SWEEP_WORKER_TIMEOUT"] = "900"

    cmd = [
        "/root/.local/bin/uv",
        "run",
        "--project",
        str(SRC_REPO),
        "python",
        "-m",
        "pufferlib.pufferl",
        "sweep",
        "osrs_inferno",
        "--load-model-path",
        str(args.checkpoint),
        "--log-dir",
        str(args.out_dir / "puffer_logs"),
        "--checkpoint-dir",
        str(args.out_dir / "checkpoints"),
        "--tag",
        args.tag,
        "--env.phase2-demo-dir",
        str(args.demo_dir),
    ]
    return subprocess.run(cmd, cwd=SRC_REPO, env=env, check=False).returncode


if __name__ == "__main__":
    raise SystemExit(main())
