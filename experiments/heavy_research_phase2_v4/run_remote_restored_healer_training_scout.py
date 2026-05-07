"""Run a restored-start healer training scout on pufferbox4."""

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
CHECKPOINT = MAIN_REPO / "checkpoints/osrs_inferno/p2k4szzs/0000000049971200.bin"
DEMOS = SRC_REPO / "experiments/heavy_research_phase2_v4/demos_quality_v2_snapshot_v2/seed_63/demos"
OUT_DIR = MAIN_REPO / "experiments/heavy_research_phase2_v4/remote_restored_healer_training_scout"
LOG_DIR = OUT_DIR / "logs"
RESULTS_PATH = OUT_DIR / "results.jsonl"
SUMMARY_PATH = OUT_DIR / "summary.json"

TIMESTEPS = 50_000_000
SEEDS = [901, 902]

METRIC_KEYS = [
    "SPS",
    "env/score_normal",
    "env/wins_normal",
    "env/frac_min_hp_le_240_normal",
    "env/frac_min_hp_le_150_normal",
    "env/frac_zuk_healers_targeted_ge_1_normal",
    "env/frac_zuk_healers_attacked_ge_1_normal",
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
    "env/ticks_240_to_first_healer_target_snapshot",
    "env/ticks_240_to_first_healer_attack_snapshot",
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


def last_metric(metrics: dict, key: str):
    value = metrics.get(key)
    if isinstance(value, list):
        numbers = [item for item in value if isinstance(item, (int, float))]
        return numbers[-1] if numbers else None
    return value if isinstance(value, (int, float)) else None


def parse_json_metrics(arm: str, seed: int) -> dict:
    pattern = str(OUT_DIR / "puffer_logs" / f"{arm}_s{seed}" / "osrs_inferno" / "*.json")
    paths = sorted(glob.glob(pattern), key=lambda path: Path(path).stat().st_mtime)
    if not paths:
        return {}
    with open(paths[-1]) as handle:
        metrics = json.load(handle).get("metrics", {})
    return {key.replace("env/", ""): last_metric(metrics, key) for key in METRIC_KEYS}


def append_result(row: dict) -> None:
    with RESULTS_PATH.open("a") as handle:
        handle.write(json.dumps(row, sort_keys=True) + "\n")


def read_results() -> list[dict]:
    if not RESULTS_PATH.exists():
        return []
    return [json.loads(line) for line in RESULTS_PATH.read_text().splitlines() if line.strip()]


def is_done(arm: str, seed: int) -> bool:
    return any(
        row["arm"] == arm and row["seed"] == seed and row["returncode"] == 0
        for row in read_results()
    )


def run_cell(arm: str, seed: int, spec: dict) -> dict:
    tag = f"remote-r18-restored-healer-{arm}-s{seed}"
    log_path = LOG_DIR / f"{tag}.log"
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
        str(CHECKPOINT),
        "--train.total-timesteps",
        str(TIMESTEPS),
        "--checkpoint-interval",
        str(TIMESTEPS),
        "--log-dir",
        str(OUT_DIR / "puffer_logs" / f"{arm}_s{seed}"),
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
        str(DEMOS),
        "--env.phase2-seed",
        str(phase2_seed),
        "--env.phase2-normal-start-frac",
        "0.0",
        "--env.phase2-randomize-rng-frac",
        "0.0",
        "--env.phase2-diagnostic-phase",
        str(spec["phase"]),
        "--env.phase2-diagnostic-tries",
        "512",
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
        "timesteps": TIMESTEPS,
        "returncode": proc.returncode,
        "elapsed_sec": time.time() - start,
        "log_path": str(log_path),
        "metrics": parse_json_metrics(arm, seed),
    }
    append_result(row)
    print(json.dumps(row, sort_keys=True), flush=True)
    if proc.returncode != 0:
        raise SystemExit(proc.returncode)
    return row


def summarize(rows: list[dict]) -> dict:
    summary = {}
    for arm in ARMS:
        arm_rows = [row for row in rows if row["arm"] == arm and row["returncode"] == 0]
        metrics = [row["metrics"] for row in arm_rows]
        arm_summary = {"n": len(arm_rows)}
        for key in [
            "score_snapshot",
            "frac_zuk_healers_targeted_ge_1_snapshot",
            "frac_zuk_healers_attacked_ge_1_snapshot",
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
    OUT_DIR.mkdir(parents=True, exist_ok=True)
    LOG_DIR.mkdir(parents=True, exist_ok=True)
    if not CHECKPOINT.exists():
        raise SystemExit(f"missing checkpoint: {CHECKPOINT}")
    if not DEMOS.exists():
        raise SystemExit(f"missing demos: {DEMOS}")

    for arm, spec in ARMS.items():
        for seed in SEEDS:
            if is_done(arm, seed):
                print(f"skip completed arm={arm} seed={seed}", flush=True)
                continue
            run_cell(arm, seed, spec)

    rows = read_results()
    summary = summarize(rows)
    SUMMARY_PATH.write_text(json.dumps(summary, indent=2, sort_keys=True) + "\n")
    print(json.dumps(summary, indent=2, sort_keys=True), flush=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
