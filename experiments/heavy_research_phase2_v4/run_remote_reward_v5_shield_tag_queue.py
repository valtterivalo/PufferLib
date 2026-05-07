"""Run shield-tag reward probes and a follow-up Protein sweep on pufferbox4."""

import argparse
import configparser
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
DEFAULT_BASE_CONFIG = (
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
    / "experiments/heavy_research_phase2_v4/remote_reward_v5_shield_tag_queue"
)

FIXED_COEFFS = [0.0, 0.02, 0.05, 0.10, 0.20]
METRIC_KEYS = [
    "SPS",
    "env/score_normal",
    "env/score_snapshot",
    "env/wins_normal",
    "env/wins_snapshot",
    "env/frac_min_hp_le_240_normal",
    "env/frac_min_hp_le_240_snapshot",
    "env/frac_min_hp_le_150_normal",
    "env/frac_min_hp_le_150_snapshot",
    "env/shield_tags_normal",
    "env/shield_tags_snapshot",
    "env/frac_shield_tags_ge_1_normal",
    "env/frac_shield_tags_ge_1_snapshot",
    "env/frac_zuk_healers_tagged_ge_1_snapshot",
    "env/frac_zuk_healers_killed_ge_1_snapshot",
    "env/frac_all_zuk_healers_dead_snapshot",
    "env/frac_died_with_zuk_healer_alive_snapshot",
    "env/frac_deaths_with_shield_active_snapshot",
    "env/frac_deaths_behind_shield_snapshot",
    "env/frac_deaths_after_240_snapshot",
    "env/frac_after_240_deaths_with_shield_active_snapshot",
    "env/frac_after_240_deaths_behind_shield_snapshot",
    "env/brews_remaining_after_240_death_snapshot",
    "env/restores_remaining_after_240_death_snapshot",
    "env/prayer_at_death_after_240_snapshot",
]


def last_metric(metrics: dict, key: str):
    value = metrics.get(key)
    if isinstance(value, list):
        numbers = [item for item in value if isinstance(item, int | float)]
        return numbers[-1] if numbers else None
    return value if isinstance(value, int | float) else None


def parse_json_metrics(log_root: Path) -> dict:
    paths = sorted(
        glob.glob(str(log_root / "osrs_inferno" / "*.json")),
        key=lambda path: Path(path).stat().st_mtime,
    )
    if not paths:
        return {}
    with open(paths[-1]) as handle:
        metrics = json.load(handle).get("metrics", {})
    return {key.replace("env/", ""): last_metric(metrics, key) for key in METRIC_KEYS}


def write_jsonl(path: Path, row: dict) -> None:
    with path.open("a") as handle:
        handle.write(json.dumps(row, sort_keys=True) + "\n")


def read_jsonl(path: Path) -> list[dict]:
    if not path.exists():
        return []
    return [json.loads(line) for line in path.read_text().splitlines() if line.strip()]


def completed_stage(results_path: Path, stage: str, name: str) -> bool:
    return any(
        row.get("stage") == stage and
        row.get("name") == name and
        row.get("returncode") == 0
        for row in read_jsonl(results_path)
    )


def prepare_sweep_config(base_config: Path, out_dir: Path) -> Path:
    config = configparser.ConfigParser()
    config.read(base_config)
    config["base"]["score_metric"] = "frac_shield_tags_ge_1_snapshot"
    config["env"]["shield_tag_reward_coeff"] = "0.05"
    config["sweep"]["metric"] = "frac_shield_tags_ge_1_snapshot"
    sweep_only = [
        item.strip()
        for item in config["sweep"]["sweep_only"].split(",")
        if item.strip()
    ]
    if "shield_tag_reward_coeff" not in sweep_only:
        sweep_only.append("shield_tag_reward_coeff")
    config["sweep"]["sweep_only"] = ", ".join(sweep_only)
    if "sweep.env.shield_tag_reward_coeff" not in config:
        config["sweep.env.shield_tag_reward_coeff"] = {}
    config["sweep.env.shield_tag_reward_coeff"]["distribution"] = "uniform"
    config["sweep.env.shield_tag_reward_coeff"]["min"] = "0.0"
    config["sweep.env.shield_tag_reward_coeff"]["max"] = "0.25"
    config["sweep.env.shield_tag_reward_coeff"]["scale"] = "auto"

    path = out_dir / "inferno_reward_v5_shield_tag_sweep.generated.ini"
    with path.open("w") as handle:
        config.write(handle)
    return path


def run_fixed_probe(args: argparse.Namespace, config_path: Path, coeff: float, index: int) -> dict:
    out_dir = args.out_dir
    name = f"shield_coeff_{coeff:.2f}".replace(".", "p")
    log_dir = out_dir / "puffer_logs" / name
    log_path = out_dir / "logs" / f"{name}.log"
    tag = f"{args.tag_prefix}-{name}"
    cmd = [
        sys.executable,
        "-m",
        "pufferlib.pufferl",
        "train",
        "osrs_inferno",
        "--seed",
        str(args.seed + index),
        "--train.seed",
        str(args.seed + index),
        "--load-model-path",
        str(args.checkpoint),
        "--log-dir",
        str(log_dir),
        "--checkpoint-dir",
        str(out_dir / "checkpoints" / name),
        "--tag",
        tag,
        "--env.phase2-demo-dir",
        str(args.demo_dir),
        "--env.phase2-seed",
        str(args.phase2_seed + index),
        "--env.shield-tag-reward-coeff",
        str(coeff),
    ]
    if args.wandb:
        cmd.extend([
            "--wandb",
            "--wandb-project",
            args.wandb_project,
            "--wandb-group",
            args.wandb_group_probe,
        ])
    env = os.environ.copy()
    env["CUDA_VISIBLE_DEVICES"] = "0"
    env["PUFFER_CONFIG_FILE"] = str(config_path)
    start = time.time()
    with log_path.open("w") as handle:
        proc = subprocess.run(
            cmd,
            cwd=SRC_REPO,
            env=env,
            stdout=handle,
            stderr=subprocess.STDOUT,
        )
    row = {
        "stage": "fixed_probe",
        "name": name,
        "shield_tag_reward_coeff": coeff,
        "returncode": proc.returncode,
        "elapsed_sec": time.time() - start,
        "log_path": str(log_path),
        "metrics": parse_json_metrics(log_dir),
    }
    write_jsonl(out_dir / "results.jsonl", row)
    print(json.dumps(row, sort_keys=True), flush=True)
    if proc.returncode != 0:
        raise SystemExit(proc.returncode)
    return row


def summarize_fixed(rows: list[dict]) -> dict:
    fixed_rows = [
        row for row in rows
        if row.get("stage") == "fixed_probe" and row.get("returncode") == 0
    ]
    summary = {"fixed_probe_count": len(fixed_rows)}
    for key in [
        "score_snapshot",
        "frac_shield_tags_ge_1_snapshot",
        "shield_tags_snapshot",
        "frac_min_hp_le_240_snapshot",
        "frac_zuk_healers_tagged_ge_1_snapshot",
        "frac_zuk_healers_killed_ge_1_snapshot",
        "frac_all_zuk_healers_dead_snapshot",
        "frac_deaths_with_shield_active_snapshot",
        "frac_after_240_deaths_with_shield_active_snapshot",
        "SPS",
    ]:
        values = [
            row["metrics"].get(key)
            for row in fixed_rows
            if isinstance(row.get("metrics", {}).get(key), int | float)
        ]
        if values:
            summary[f"mean_{key}"] = mean(values)
            summary[f"median_{key}"] = median(values)
    ranked = sorted(
        fixed_rows,
        key=lambda row: (
            row["metrics"].get("frac_shield_tags_ge_1_snapshot") or 0.0,
            row["metrics"].get("score_snapshot") or 0.0,
        ),
        reverse=True,
    )
    summary["fixed_probe_ranked"] = [
        {
            "name": row["name"],
            "shield_tag_reward_coeff": row["shield_tag_reward_coeff"],
            "score_snapshot": row["metrics"].get("score_snapshot"),
            "frac_shield_tags_ge_1_snapshot": row["metrics"].get(
                "frac_shield_tags_ge_1_snapshot"
            ),
            "frac_deaths_with_shield_active_snapshot": row["metrics"].get(
                "frac_deaths_with_shield_active_snapshot"
            ),
            "SPS": row["metrics"].get("SPS"),
        }
        for row in ranked
    ]
    return summary


def run_sweep(args: argparse.Namespace, config_path: Path) -> dict:
    log_path = args.out_dir / "logs" / "protein_sweep.log"
    cmd = [
        sys.executable,
        "-m",
        "pufferlib.pufferl",
        "sweep",
        "osrs_inferno",
        "--load-model-path",
        str(args.checkpoint),
        "--log-dir",
        str(args.out_dir / "sweep_puffer_logs"),
        "--checkpoint-dir",
        str(args.out_dir / "sweep_checkpoints"),
        "--tag",
        f"{args.tag_prefix}-protein",
        "--env.phase2-demo-dir",
        str(args.demo_dir),
    ]
    if args.wandb:
        cmd.extend([
            "--wandb",
            "--wandb-project",
            args.wandb_project,
            "--wandb-group",
            args.wandb_group_sweep,
        ])
    env = os.environ.copy()
    env["CUDA_VISIBLE_DEVICES"] = "0"
    env["PUFFER_CONFIG_FILE"] = str(config_path)
    start = time.time()
    with log_path.open("w") as handle:
        proc = subprocess.run(
            cmd,
            cwd=SRC_REPO,
            env=env,
            stdout=handle,
            stderr=subprocess.STDOUT,
        )
    row = {
        "stage": "protein_sweep",
        "name": "shield_tag_sweep",
        "returncode": proc.returncode,
        "elapsed_sec": time.time() - start,
        "log_path": str(log_path),
        "config_path": str(config_path),
    }
    write_jsonl(args.out_dir / "results.jsonl", row)
    print(json.dumps(row, sort_keys=True), flush=True)
    if proc.returncode != 0:
        raise SystemExit(proc.returncode)
    return row


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--base-config", type=Path, default=DEFAULT_BASE_CONFIG)
    parser.add_argument("--checkpoint", type=Path, default=DEFAULT_CHECKPOINT)
    parser.add_argument("--demo-dir", type=Path, default=DEFAULT_DEMOS)
    parser.add_argument("--out-dir", type=Path, default=DEFAULT_OUT_DIR)
    parser.add_argument("--seed", type=int, default=1501)
    parser.add_argument("--phase2-seed", type=int, default=11501)
    parser.add_argument("--tag-prefix", default="remote-r22-shield-tag")
    parser.add_argument("--wandb-project", default="puffer4")
    parser.add_argument("--wandb-group-probe", default="remote-r22-shield-tag-probe")
    parser.add_argument("--wandb-group-sweep", default="remote-r22-shield-tag-sweep")
    parser.add_argument("--wandb", action="store_true")
    parser.add_argument("--skip-sweep", action="store_true")
    args = parser.parse_args()

    for path in [args.base_config, args.checkpoint, args.demo_dir]:
        if not path.exists():
            raise SystemExit(f"missing required path: {path}")

    args.out_dir.mkdir(parents=True, exist_ok=True)
    (args.out_dir / "logs").mkdir(parents=True, exist_ok=True)
    config_path = prepare_sweep_config(args.base_config, args.out_dir)
    results_path = args.out_dir / "results.jsonl"

    for index, coeff in enumerate(FIXED_COEFFS):
        name = f"shield_coeff_{coeff:.2f}".replace(".", "p")
        if completed_stage(results_path, "fixed_probe", name):
            print(f"skip completed fixed probe {name}", flush=True)
            continue
        run_fixed_probe(args, config_path, coeff, index)

    summary = summarize_fixed(read_jsonl(results_path))
    (args.out_dir / "summary.json").write_text(
        json.dumps(summary, indent=2, sort_keys=True) + "\n"
    )
    print(json.dumps(summary, indent=2, sort_keys=True), flush=True)

    if not args.skip_sweep and not completed_stage(
        results_path, "protein_sweep", "shield_tag_sweep"
    ):
        run_sweep(args, config_path)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
