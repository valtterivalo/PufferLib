"""Run a restored-start healer training scout on pufferbox4."""

import argparse
import glob
import json
import os
import subprocess
import sys
import time
from pathlib import Path
from statistics import mean, median


SRC_REPO = Path("/puffertank/docker/goexplore_terminal_reset_test")
MAIN_REPO = Path("/puffertank/docker/goexplore")
DEFAULT_CHECKPOINT = MAIN_REPO / "checkpoints/osrs_inferno/p2k4szzs/0000000049971200.bin"
DEFAULT_DEMOS = SRC_REPO / "experiments/heavy_research_phase2_v4/demos_quality_v2_snapshot_v2/seed_63/demos"
DEFAULT_OUT_DIR = MAIN_REPO / "experiments/heavy_research_phase2_v4/remote_restored_healer_training_scout"

DEFAULT_TIMESTEPS = 50_000_000
DEFAULT_SEEDS = "901,902"

METRIC_KEYS = [
    "SPS",
    "env/score_normal",
    "env/wins_normal",
    "env/frac_min_hp_le_240_normal",
    "env/frac_min_hp_le_150_normal",
    "env/frac_zuk_healers_targeted_ge_1_normal",
    "env/frac_zuk_healers_attacked_ge_1_normal",
    "env/frac_zuk_healers_attackable_ge_1_normal",
    "env/frac_zuk_healers_tagged_ge_1_normal",
    "env/frac_zuk_healers_killed_ge_1_normal",
    "env/frac_all_zuk_healers_dead_normal",
    "env/frac_died_with_zuk_healer_alive_normal",
    "env/score_snapshot",
    "env/wins_snapshot",
    "env/frac_min_hp_le_240_snapshot",
    "env/frac_min_hp_le_150_snapshot",
    "env/damage_after_240_snapshot",
    "env/frac_healer_spawned_snapshot",
    "env/frac_zuk_healers_targeted_ge_1_snapshot",
    "env/frac_zuk_healers_attacked_ge_1_snapshot",
    "env/frac_zuk_healers_attackable_ge_1_snapshot",
    "env/ticks_240_to_first_healer_target_snapshot",
    "env/ticks_240_to_first_healer_attack_snapshot",
    "env/zuk_healer_target_cannot_attack_ticks_snapshot",
    "env/zuk_healer_target_cooldown_ticks_snapshot",
    "env/zuk_healer_target_out_of_range_ticks_snapshot",
    "env/zuk_healer_target_attackable_ticks_snapshot",
    "env/frac_zuk_healers_tagged_ge_1_snapshot",
    "env/frac_zuk_healers_tagged_ge_4_snapshot",
    "env/frac_zuk_healers_killed_ge_1_snapshot",
    "env/frac_zuk_healers_killed_ge_4_snapshot",
    "env/frac_all_zuk_healers_dead_snapshot",
    "env/hp_restored_after_240_snapshot",
    "env/frac_died_with_zuk_healer_alive_snapshot",
    "env/frac_died_after_240_never_tagged_healer_snapshot",
    "env/frac_died_after_240_some_healers_tagged_snapshot",
    "env/phase2_cursor_mean_frac",
    "env/phase2_cursor_min_frac",
    "env/phase2_cursor_max_frac",
]

TRAIN_HPARAMS = {
    "learning-rate": 0.0008836829619964984,
    "min-lr-ratio": 0.6733178808180771,
    "gamma": 0.999997223455653,
    "gae-lambda": 0.92,
    "replay-ratio": 0.30461446981116713,
    "clip-coef": 0.0680595985698656,
    "vf-coef": 0.5000000000000001,
    "vf-clip-coef": 1.9766612069408152,
    "max-grad-norm": 0.581115937809795,
    "ent-coef": 0.016636828336430237,
    "vtrace-rho-clip": 1.0,
    "vtrace-c-clip": 1.0,
    "prio-alpha": 0.12916827796176567,
}

ARMS = {
    "pre_healer_only": {
        "phase": 1,
        "phase2_seed_offset": 91_000,
    },
    "immediate_healer_only": {
        "phase": 2,
        "phase2_seed_offset": 92_000,
    },
}


def parse_int_csv(raw: str) -> list[int]:
    values = [item.strip() for item in raw.split(",") if item.strip()]
    if not values:
        raise ValueError("expected at least one integer")
    return [int(item) for item in values]


def parse_arm_csv(raw: str) -> list[str]:
    names = [item.strip() for item in raw.split(",") if item.strip()]
    if not names:
        raise ValueError("expected at least one arm")
    unknown = [name for name in names if name not in ARMS]
    if unknown:
        raise ValueError(f"unknown arms: {unknown}")
    return names


def last_metric(metrics: dict, key: str):
    value = metrics.get(key)
    if isinstance(value, list):
        numbers = [item for item in value if isinstance(item, (int, float))]
        return numbers[-1] if numbers else None
    return value if isinstance(value, (int, float)) else None


def parse_json_metrics(out_dir: Path, arm: str, seed: int) -> dict:
    pattern = str(out_dir / "puffer_logs" / f"{arm}_s{seed}" / "osrs_inferno" / "*.json")
    paths = sorted(glob.glob(pattern), key=lambda path: Path(path).stat().st_mtime)
    if not paths:
        return {}
    with open(paths[-1]) as handle:
        metrics = json.load(handle).get("metrics", {})
    return {key.replace("env/", ""): last_metric(metrics, key) for key in METRIC_KEYS}


def append_result(results_path: Path, row: dict) -> None:
    with results_path.open("a") as handle:
        handle.write(json.dumps(row, sort_keys=True) + "\n")


def read_results(results_path: Path) -> list[dict]:
    if not results_path.exists():
        return []
    return [json.loads(line) for line in results_path.read_text().splitlines() if line.strip()]


def is_done(results_path: Path, arm: str, seed: int) -> bool:
    return any(
        row["arm"] == arm and row["seed"] == seed and row["returncode"] == 0
        for row in read_results(results_path)
    )


def run_cell(args_cli: argparse.Namespace, arm: str, seed: int, spec: dict) -> dict:
    out_dir = Path(args_cli.out_dir)
    log_dir = out_dir / "logs"
    results_path = out_dir / "results.jsonl"
    tag = f"{args_cli.tag_prefix}-{arm}-s{seed}"
    log_path = log_dir / f"{tag}.log"
    phase2_seed = seed + spec["phase2_seed_offset"]
    cmd = [
        sys.executable,
        "-m",
        "pufferlib.pufferl",
        "train",
        "osrs_inferno",
        "--seed",
        str(seed),
        "--train.seed",
        str(seed),
        "--load-model-path",
        str(args_cli.checkpoint),
        "--train.total-timesteps",
        str(args_cli.timesteps),
        "--checkpoint-interval",
        str(args_cli.timesteps),
        "--log-dir",
        str(out_dir / "puffer_logs" / f"{arm}_s{seed}"),
        "--tag",
        tag,
        "--train.terminal-reset-state",
        "1",
        "--train.minibatch-size",
        "4096",
        "--train.horizon",
        "128",
        "--policy.hidden-size",
        "256",
        "--policy.num-layers",
        "3",
        "--env.phase2-demo-dir",
        str(args_cli.demo_dir),
        "--env.phase2-seed",
        str(phase2_seed),
        "--env.phase2-normal-start-frac",
        "0.0",
        "--env.phase2-randomize-rng-frac",
        "0.0",
        "--env.phase2-diagnostic-phase",
        str(spec["phase"]),
        "--env.phase2-diagnostic-tries",
        str(args_cli.diagnostic_tries),
        "--env.phase2-max-player-attack-timer",
        str(args_cli.max_player_attack_timer),
        "--env.phase2-bc-coef",
        "0.0",
        "--env.phase2-bc-demos-per-minibatch",
        "0",
        "--env.phase2-backstep-ticks",
        "8",
        "--env.phase2-success-q-delta",
        "0.0010560670538554925",
    ]
    for key, value in TRAIN_HPARAMS.items():
        cmd.extend([f"--train.{key}", str(value)])

    print(f"running arm={arm} seed={seed}", flush=True)
    start = time.time()
    env = os.environ.copy()
    env["CUDA_VISIBLE_DEVICES"] = "0"
    with log_path.open("w") as handle:
        proc = subprocess.run(
            cmd,
            cwd=SRC_REPO,
            env=env,
            stdout=handle,
            stderr=subprocess.STDOUT,
        )
    row = {
        "arm": arm,
        "seed": seed,
        "timesteps": args_cli.timesteps,
        "demo_dir": str(args_cli.demo_dir),
        "max_player_attack_timer": args_cli.max_player_attack_timer,
        "returncode": proc.returncode,
        "elapsed_sec": time.time() - start,
        "log_path": str(log_path),
        "metrics": parse_json_metrics(out_dir, arm, seed),
    }
    append_result(results_path, row)
    print(json.dumps(row, sort_keys=True), flush=True)
    if proc.returncode != 0:
        raise SystemExit(proc.returncode)
    return row


def summarize(arms: list[str], rows: list[dict]) -> dict:
    summary = {}
    for arm in arms:
        arm_rows = [row for row in rows if row["arm"] == arm and row["returncode"] == 0]
        metrics = [row["metrics"] for row in arm_rows]
        arm_summary = {"n": len(arm_rows)}
        for key in [
            "score_snapshot",
            "frac_zuk_healers_targeted_ge_1_snapshot",
            "frac_zuk_healers_attacked_ge_1_snapshot",
            "frac_zuk_healers_attackable_ge_1_snapshot",
            "frac_zuk_healers_tagged_ge_1_snapshot",
            "frac_zuk_healers_killed_ge_1_snapshot",
            "frac_all_zuk_healers_dead_snapshot",
            "frac_died_with_zuk_healer_alive_snapshot",
            "frac_died_after_240_never_tagged_healer_snapshot",
            "frac_min_hp_le_150_snapshot",
            "SPS",
        ]:
            values = [m.get(key) for m in metrics if isinstance(m.get(key), (int, float))]
            if values:
                arm_summary[f"mean_{key}"] = mean(values)
                arm_summary[f"median_{key}"] = median(values)
        summary[arm] = arm_summary
    return summary


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--checkpoint", type=Path, default=DEFAULT_CHECKPOINT)
    parser.add_argument("--demo-dir", type=Path, default=DEFAULT_DEMOS)
    parser.add_argument("--out-dir", type=Path, default=DEFAULT_OUT_DIR)
    parser.add_argument("--timesteps", type=int, default=DEFAULT_TIMESTEPS)
    parser.add_argument("--seeds", default=DEFAULT_SEEDS)
    parser.add_argument("--arms", default=",".join(ARMS))
    parser.add_argument("--diagnostic-tries", type=int, default=512)
    parser.add_argument("--max-player-attack-timer", type=int, default=-1)
    parser.add_argument("--tag-prefix", default="remote-r18-restored-healer")
    args_cli = parser.parse_args()

    seeds = parse_int_csv(args_cli.seeds)
    arms = parse_arm_csv(args_cli.arms)
    args_cli.out_dir.mkdir(parents=True, exist_ok=True)
    (args_cli.out_dir / "logs").mkdir(parents=True, exist_ok=True)
    results_path = args_cli.out_dir / "results.jsonl"
    summary_path = args_cli.out_dir / "summary.json"
    if not args_cli.checkpoint.exists():
        raise SystemExit(f"missing checkpoint: {args_cli.checkpoint}")
    if not args_cli.demo_dir.exists():
        raise SystemExit(f"missing demos: {args_cli.demo_dir}")

    for arm in arms:
        spec = ARMS[arm]
        for seed in seeds:
            if is_done(results_path, arm, seed):
                print(f"skip completed arm={arm} seed={seed}", flush=True)
                continue
            run_cell(args_cli, arm, seed, spec)

    rows = read_results(results_path)
    summary = summarize(arms, rows)
    summary_path.write_text(json.dumps(summary, indent=2, sort_keys=True) + "\n")
    print(json.dumps(summary, indent=2, sort_keys=True), flush=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
