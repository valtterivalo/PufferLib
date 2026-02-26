#!/usr/bin/env python3
"""Protein hyperparameter sweep for Metal static-native PVP training.

Uses PufferLib's Protein algorithm (cost-aware Bayesian optimization with
dual GP models) to search over PPO hyperparameters AND training duration.
Protein models a Pareto frontier of score vs cost (wall-clock time), so it
discovers not just good hyperparams but also how long to train them.

Each trial runs train_pvp.py as a subprocess for crash isolation. Metrics are
parsed from stdout lines matching the format:
  [step=  N | SPS=  S | ret=R wins=W len=L | ent=E pg=P vf=V]

Training curves are downsampled to 5 intermediate observations so the GPs
learn about learning dynamics, not just final scores. Win rate uses a logit
(percentile) transform so differences near 99% are much more meaningful than
near 50%.

Observations persist to JSONL for resume across restarts.

Optimizer selection (muon vs adam) is a searchable parameter encoded as a
uniform [0, 1] float: <0.5 = muon, >=0.5 = adam. Protein can discover which
optimizer works best at each cost level and include it in the Pareto front.

Usage:
    python sweep.py                          # run sweep (default: true_random)
    python sweep.py --opponent improved      # harder opponent (OPP_IMPROVED=8)
    python sweep.py --timeout 6              # custom timeout in hours
    python sweep.py --results                # print results and exit
    python sweep.py --max-trials 50          # limit number of trials
"""

from __future__ import annotations

import argparse
import json
import math
import os
import re
import subprocess
import time
from copy import deepcopy
from pathlib import Path

import numpy as np
import pufferlib
from pufferlib.sweep import Protein, pareto_points, prune_pareto_front
from pufferlib.pufferl import downsample

# ---------------------------------------------------------------------------
# sweep configuration
# ---------------------------------------------------------------------------

SWEEP_DIR = Path("runs/sweep_protein")
DOWNSAMPLE_POINTS = 5

# how often to poll stdout log for training progress (seconds)
POLL_INTERVAL = 10

# opponent type name -> enum value (from osrs_pvp_types.h OpponentType)
OPPONENT_TYPES = {
    "true_random": 1,
    "panicking": 2,
    "weak_random": 3,
    "semi_random": 4,
    "sticky_prayer": 5,
    "random_eater": 6,
    "prayer_rookie": 7,
    "improved": 8,
    "novice_nh": 17,
}
DEFAULT_OPPONENT = "true_random"

# regex to parse train_pvp.py stdout metrics
# format: [step=     32,768 | SPS= 271,000 | ret=  0.12 wins=0.56 len=142 | ent=1.234 pg=0.0012 vf=0.3456]
METRIC_PATTERN = re.compile(
    r"\[step=\s*([\d,]+)\s*\|\s*SPS=\s*([\d,]+)\s*\|\s*"
    r"ret=\s*([\d.-]+)\s+wins=([\d.]+)\s+len=(\d+)\s*\|\s*"
    r"ent=([\d.-]+)\s+pg=([\d.-]+)\s+vf=([\d.-]+)\]"
)


def parse_metric_line(line: str) -> dict | None:
    """Parse a single metric line from train_pvp.py stdout.

    Returns dict with step, sps, episode_return, wins, episode_length,
    entropy, pg_loss, vf_loss on match, None otherwise.
    """
    m = METRIC_PATTERN.search(line)
    if not m:
        return None
    return {
        "step": int(m.group(1).replace(",", "")),
        "sps": int(m.group(2).replace(",", "")),
        "episode_return": float(m.group(3)),
        "wins": float(m.group(4)),
        "episode_length": int(m.group(5)),
        "entropy": float(m.group(6)),
        "pg_loss": float(m.group(7)),
        "vf_loss": float(m.group(8)),
    }


# ---------------------------------------------------------------------------
# protein sweep config
#
# nested dict structure for Protein's Hyperparameters class. keys under
# nested dicts become "section/key" via pufferlib.unroll_nested_dict.
# ---------------------------------------------------------------------------

SWEEP_CONFIG = {
    "method": "Protein",
    "metric": "win_rate",
    "metric_distribution": "percentile",
    "goal": "maximize",
    "downsample": DOWNSAMPLE_POINTS,
    "use_gpu": False,  # MPS doesn't support gpytorch CUDA — use CPU for GPs
    "prune_pareto": True,
    "max_suggestion_cost": 3600,  # 1 hour max wall-clock per trial
    "early_stop_quantile": 0.3,

    # searchable hyperparameters
    "train": {
        "total_timesteps": {
            "distribution": "log_normal",
            "min": 5_000_000,
            "max": 200_000_000,
            "scale": "time",
        },
        "horizon": {
            "distribution": "uniform_pow2",
            "min": 16,
            "max": 128,
            "scale": "auto",
        },
        "learning_rate": {
            "distribution": "log_normal",
            "min": 0.0001,
            "max": 0.03,
            "scale": 0.5,
        },
        "ent_coef": {
            "distribution": "log_normal",
            "min": 0.0005,
            "max": 0.05,
            "scale": "auto",
        },
        "gamma": {
            "distribution": "logit_normal",
            "min": 0.98,
            "max": 0.999,
            "scale": "auto",
        },
        "gae_lambda": {
            "distribution": "logit_normal",
            "min": 0.8,
            "max": 0.99,
            "scale": "auto",
        },
        "prio_alpha": {
            "distribution": "logit_normal",
            "min": 0.01,
            "max": 0.95,
            "scale": "auto",
        },
        "prio_beta0": {
            "distribution": "logit_normal",
            "min": 0.1,
            "max": 0.95,
            "scale": "auto",
        },
        "clip_coef": {
            "distribution": "uniform",
            "min": 0.1,
            "max": 0.5,
            "scale": "auto",
        },
        "vf_coef": {
            "distribution": "uniform",
            "min": 0.5,
            "max": 5.0,
            "scale": "auto",
        },
        "vf_clip_coef": {
            "distribution": "uniform",
            "min": 0.1,
            "max": 1.5,
            "scale": "auto",
        },
        "max_grad_norm": {
            "distribution": "uniform",
            "min": 0.3,
            "max": 4.0,
            "scale": "auto",
        },
        "replay_ratio": {
            "distribution": "uniform",
            "min": 0.1,
            "max": 2.0,
            "scale": "auto",
        },
        "minibatch_size": {
            "distribution": "uniform_pow2",
            "min": 1024,
            "max": 16384,
            "scale": "auto",
        },
    },
    "policy": {
        "hidden_size": {
            "distribution": "uniform_pow2",
            "min": 128,
            "max": 1024,
            "scale": "auto",
        },
        "num_layers": {
            "distribution": "uniform",
            "min": 1,
            "max": 5,
            "scale": "auto",
        },
    },
    # optimizer selection: <0.5 = muon, >=0.5 = adam
    "optim": {
        "optimizer_idx": {
            "distribution": "uniform",
            "min": 0.0,
            "max": 1.0,
            "scale": "auto",
        },
    },
}

# default hyperparameters used for the anchor trial (trial 0).
# these come from the current best known metal config.
DEFAULT_PARAMS = {
    "train": {
        "total_timesteps": 50_000_000,
        "horizon": 32,
        "learning_rate": 0.00112,
        "ent_coef": 0.0016,
        "gamma": 0.991,
        "gae_lambda": 0.845,
        "prio_alpha": 0.914,
        "prio_beta0": 0.218,
        "clip_coef": 0.32,
        "vf_coef": 2.5,
        "vf_clip_coef": 0.1,
        "max_grad_norm": 0.5,
        "replay_ratio": 0.25,
        "minibatch_size": 4096,
    },
    "policy": {
        "hidden_size": 512,
        "num_layers": 3,
    },
    "optim": {
        "optimizer_idx": 0.0,  # muon for anchor trial
    },
}


def decode_optimizer(params: dict) -> str:
    """Decode optimizer_idx to optimizer name.

    <0.5 = muon, >=0.5 = adam.
    """
    idx = params.get("optim", {}).get("optimizer_idx", 0.0)
    return "adam" if idx >= 0.5 else "muon"


# ---------------------------------------------------------------------------
# Protein nested keys -> train_pvp.py flat CLI arg mapping
# ---------------------------------------------------------------------------

# maps unrolled Protein key (e.g. "train/learning_rate") to CLI flag name
CLI_MAP = {
    "train/total_timesteps": "total-timesteps",
    "train/horizon": "horizon",
    "train/learning_rate": "learning-rate",
    "train/ent_coef": "ent-coef",
    "train/gamma": "gamma",
    "train/gae_lambda": "gae-lambda",
    "train/prio_alpha": "prio-alpha",
    "train/prio_beta0": "prio-beta0",
    "train/clip_coef": "clip-coef",
    "train/vf_coef": "vf-coef",
    "train/vf_clip_coef": "vf-clip-coef",
    "train/max_grad_norm": "max-grad-norm",
    "train/replay_ratio": "replay-ratio",
    "train/minibatch_size": "minibatch-size",
    "policy/hidden_size": "hidden-size",
    "policy/num_layers": "num-layers",
}

# params passed as integers (everything else is float)
INTEGER_PARAMS = {
    "total_timesteps", "horizon", "minibatch_size",
    "hidden_size", "num_layers",
}


# ---------------------------------------------------------------------------
# stdout metric parsing
# ---------------------------------------------------------------------------

def read_stdout_metrics(path: Path) -> list[dict]:
    """Read all metric entries from a train_pvp.py stdout log."""
    entries = []
    if not path.exists():
        return entries
    with path.open() as f:
        for line in f:
            parsed = parse_metric_line(line)
            if parsed:
                entries.append(parsed)
    return entries


# ---------------------------------------------------------------------------
# persistence: save/load Protein observations to JSONL
# ---------------------------------------------------------------------------

def save_observation(obs: dict, path: Path) -> None:
    """Append a single observation record to the persistence file."""
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("a") as f:
        f.write(json.dumps(obs) + "\n")


def load_observations(path: Path) -> list[dict]:
    """Load all observation records from the persistence file."""
    if not path.exists():
        return []
    records = []
    with path.open() as f:
        for line in f:
            line = line.strip()
            if line:
                records.append(json.loads(line))
    return records


# ---------------------------------------------------------------------------
# trial execution
# ---------------------------------------------------------------------------

def build_command(params: dict, opponent_type: int) -> list[str]:
    """Build the train_pvp.py CLI command for a trial.

    Args:
        params: nested dict from Protein.suggest() with searchable params.
            optimizer_idx is decoded to --optimizer muon/adam.
        opponent_type: integer opponent type enum value from osrs_pvp_types.h.
    """
    optimizer = decode_optimizer(params)
    cmd = [
        "python", "train_pvp.py",
        "--no-wandb",
        f"--optimizer={optimizer}",
        "--save-interval=0",  # no checkpointing during sweep
        "--log-interval=5",   # frequent logging for better curves
        f"--opponent-type={opponent_type}",
    ]

    flat_params = dict(pufferlib.unroll_nested_dict(params))
    for key, value in flat_params.items():
        cli_flag = CLI_MAP.get(key)
        if cli_flag is None:
            continue
        param_name = key.split("/")[-1]
        if param_name in INTEGER_PARAMS:
            cmd.append(f"--{cli_flag}={int(value)}")
        else:
            cmd.append(f"--{cli_flag}={value}")

    return cmd


def run_trial(
    trial_idx: int,
    params: dict,
    protein: Protein,
    opponent_type: int,
) -> dict | None:
    """Run a single training trial and observe results.

    Returns a summary dict on success, None on failure.
    """
    optimizer = decode_optimizer(params)
    cmd = build_command(params, opponent_type)

    flat = dict(pufferlib.unroll_nested_dict(params))
    total_steps = int(flat.get("train/total_timesteps", 0))

    print(f"\n{'='*70}")
    print(f"trial {trial_idx}  ({total_steps/1e6:.1f}M steps, {optimizer})")
    for key, value in sorted(flat.items()):
        short_key = key.split("/")[-1]
        fmt = f"{value:.6f}" if isinstance(value, float) else str(value)
        print(f"  {short_key:20s} = {fmt}")
    print(f"{'='*70}")

    log_dir = SWEEP_DIR / f"trial_{trial_idx}"
    log_dir.mkdir(parents=True, exist_ok=True)

    # save trial config for reference
    with (log_dir / "config.json").open("w") as f:
        json.dump({"params": params, "optimizer": optimizer, "cmd": cmd}, f, indent=2)

    env = os.environ.copy()

    stdout_path = log_dir / "stdout.log"
    stderr_path = log_dir / "stderr.log"
    stdout_f = open(stdout_path, "w")
    stderr_f = open(stderr_path, "w")
    start_time = time.time()

    process = subprocess.Popen(
        cmd,
        stdout=stdout_f,
        stderr=stderr_f,
        env=env,
        cwd=Path(__file__).parent,
    )

    # poll stdout for intermediate metrics + early stopping
    last_n_entries = 0
    last_report_time = start_time

    while process.poll() is None:
        time.sleep(POLL_INTERVAL)

        entries = read_stdout_metrics(stdout_path)
        if len(entries) <= last_n_entries:
            continue
        last_n_entries = len(entries)

        current = entries[-1]
        current_wr = current["wins"]
        current_step = current["step"]
        elapsed = time.time() - start_time

        # periodic status print
        if time.time() - last_report_time > 60:
            mean_sps = sum(e["sps"] for e in entries) / len(entries) if entries else 0
            print(
                f"  [{current_step/1e6:.1f}M / {total_steps/1e6:.0f}M]  "
                f"wr={current_wr:.3f}  sps={mean_sps:.0f}  "
                f"elapsed={elapsed:.0f}s"
            )
            last_report_time = time.time()

        # early stopping via Protein's learned cost model
        if protein.should_stop(current_wr, elapsed):
            print(f"  EARLY STOP at {current_step/1e6:.1f}M steps ({elapsed:.0f}s), wr={current_wr:.3f}")
            process.terminate()
            process.wait(timeout=10)
            break

    stdout_f.close()
    stderr_f.close()

    elapsed = time.time() - start_time
    returncode = process.returncode

    # 138 = SIGBUS (metal cleanup on macOS), -15 = SIGTERM (Python),
    # 143 = SIGTERM (128+15, shell convention for early stop)
    if returncode not in (None, 0, 138, -15, 143):
        if stderr_path.exists():
            lines = stderr_path.read_text().strip().splitlines()[-5:]
            for line in lines:
                print(f"  stderr: {line}")
        print(f"  FAILED (exit {returncode}, {elapsed:.0f}s)")
        return None

    # read final results
    entries = read_stdout_metrics(stdout_path)
    if not entries:
        print(f"  FAILED: no metric output ({elapsed:.0f}s)")
        return None

    win_rates = [e["wins"] for e in entries]
    steps = [e["step"] for e in entries]

    # downsample the training curve for Protein observations.
    # use ACTUAL step counts as total_timesteps in each observation.
    downsampled_wr = downsample(win_rates, DOWNSAMPLE_POINTS)
    downsampled_steps = downsample(steps, DOWNSAMPLE_POINTS)

    # observe each downsampled point
    observations = []
    for wr, step in zip(downsampled_wr, downsampled_steps):
        obs_params = deepcopy(params)
        obs_params["train"]["total_timesteps"] = step
        cost = elapsed * (step / max(steps[-1], 1))  # pro-rate wall time
        protein.observe(obs_params, wr, cost)
        observations.append({
            "params": obs_params,
            "win_rate": wr,
            "cost": cost,
            "step": step,
        })

    # summary
    final_wr = win_rates[-1]
    tail_start = int(len(win_rates) * 0.75)
    tail_wr = sum(win_rates[tail_start:]) / max(len(win_rates[tail_start:]), 1)
    mean_sps = sum(e["sps"] for e in entries) / len(entries) if entries else 0

    print(
        f"  DONE  final_wr={final_wr:.4f}  tail_wr={tail_wr:.4f}  "
        f"sps={mean_sps:.0f}  steps={steps[-1]/1e6:.1f}M  "
        f"wall={elapsed:.0f}s"
    )

    return {
        "trial": trial_idx,
        "params": params,
        "optimizer": optimizer,
        "final_win_rate": final_wr,
        "tail_win_rate": tail_wr,
        "mean_sps": mean_sps,
        "total_steps": steps[-1],
        "wall_seconds": elapsed,
        "observations": observations,
    }


# ---------------------------------------------------------------------------
# results display
# ---------------------------------------------------------------------------

def logit_inverse(logit_score: float) -> float:
    """Convert logit-transformed score back to [0, 1] win rate."""
    logit_score = max(-5, min(100, logit_score))
    return 1.0 / (1.0 + math.exp(-logit_score))


def protein_logit(value: float, epsilon: float = 1e-9) -> float:
    """Logit transform matching Protein.logit_transform."""
    value = max(epsilon, min(1 - epsilon, value))
    return max(-5, min(100, math.log(value / (1 - value))))


def print_results(obs_path: Path) -> None:
    """Print sweep results from persisted observations."""
    records = load_observations(obs_path)
    if not records:
        print("no observations found")
        return

    # group by trial, take the last observation (highest step) as "final"
    trials: dict[int, list[dict]] = {}
    for r in records:
        trials.setdefault(r["trial"], []).append(r)

    trial_summaries = []
    for tid, obs_list in sorted(trials.items()):
        best = max(obs_list, key=lambda o: o["step"])
        trial_summaries.append({
            "trial": tid,
            "win_rate": best["win_rate"],
            "cost": best["cost"],
            "step": best["step"],
            "params": best["params"],
            "optimizer": best.get("optimizer", "muon"),
            "output": protein_logit(best["win_rate"]),
        })

    print(f"\n{'='*70}")
    print(f"protein sweep results: {len(trial_summaries)} trials")
    print(f"{'='*70}")

    valid_summaries = [t for t in trial_summaries if t["step"] > 0 and t["win_rate"] > 0]

    pareto, pareto_idxs = pareto_points(valid_summaries)
    pruned = prune_pareto_front(pareto)
    pareto_trial_ids = {t["trial"] for t in pruned}

    if pruned:
        best = pruned[-1]
        print(f"\nbest on pareto front: #{best['trial']}")
        print(f"  win_rate: {best['win_rate']:.4f}")
        print(f"  steps: {best['step']/1e6:.1f}M")
        print(f"  wall: {best['cost']:.0f}s")
        print(f"  optimizer: {best.get('optimizer', 'muon')}")
        flat = dict(pufferlib.unroll_nested_dict(best["params"]))
        print(f"\n  hyperparameters:")
        for key, value in sorted(flat.items()):
            short_key = key.split("/")[-1]
            fmt = f"{value:.6f}" if isinstance(value, float) else str(value)
            print(f"    {short_key:20s} = {fmt}")

    if len(pruned) > 1:
        print(f"\npareto frontier ({len(pruned)} points, best first):")
        for t in reversed(pruned):
            flat = dict(pufferlib.unroll_nested_dict(t["params"]))
            hz = int(flat.get("train/horizon", 0))
            lr = flat.get("train/learning_rate", 0)
            ent = flat.get("train/ent_coef", 0)
            hs = int(flat.get("policy/hidden_size", 0))
            steps_m = t["step"] / 1e6
            opt = t.get("optimizer", "muon")
            print(
                f"  #{t['trial']:3d}  wr={t['win_rate']:.4f}  "
                f"steps={steps_m:>5.1f}M  wall={t['cost']:>5.0f}s  "
                f"hz={hz:>3}  lr={lr:.4f}  ent={ent:.4f}  hs={hs}  {opt}"
            )

    by_wr = sorted(trial_summaries, key=lambda t: t["win_rate"], reverse=True)
    print(f"\ntop 15 by raw win rate:")
    for t in by_wr[:15]:
        flat = dict(pufferlib.unroll_nested_dict(t["params"]))
        hz = int(flat.get("train/horizon", 0))
        lr = flat.get("train/learning_rate", 0)
        ent = flat.get("train/ent_coef", 0)
        hs = int(flat.get("policy/hidden_size", 0))
        steps_m = t["step"] / 1e6
        opt = t.get("optimizer", "muon")
        is_pareto = " *" if t["trial"] in pareto_trial_ids else ""
        print(
            f"  #{t['trial']:3d}  wr={t['win_rate']:.4f}  "
            f"steps={steps_m:>5.1f}M  wall={t['cost']:>5.0f}s  "
            f"hz={hz:>3}  lr={lr:.4f}  ent={ent:.4f}  hs={hs}  {opt}{is_pareto}"
        )


# ---------------------------------------------------------------------------
# main sweep loop
# ---------------------------------------------------------------------------

def run_sweep(
    max_trials: int | None,
    timeout_h: float,
    opponent_name: str,
) -> None:
    """Run the Protein sweep."""
    opponent_type = OPPONENT_TYPES[opponent_name]
    SWEEP_DIR.mkdir(parents=True, exist_ok=True)
    obs_path = SWEEP_DIR / "observations.jsonl"

    protein = Protein(
        SWEEP_CONFIG,
        use_gpu=False,
        prune_pareto=True,
    )

    # replay persisted observations to rebuild Protein's internal state
    existing_records = load_observations(obs_path)
    existing_trials: set[int] = set()
    if existing_records:
        for r in existing_records:
            existing_trials.add(r["trial"])
            protein.observe(r["params"], r["win_rate"], r["cost"])
        print(f"replayed {len(existing_records)} observations from {len(existing_trials)} previous trials")

    trial_idx = max(existing_trials) + 1 if existing_trials else 0
    sweep_start = time.time()
    timeout_s = timeout_h * 3600

    n_params = len(dict(pufferlib.unroll_nested_dict(
        {k: v for k, v in SWEEP_CONFIG.items()
         if isinstance(v, dict)}
    )))

    print(f"protein sweep (metal static-native)")
    print(f"  optimizer: searchable (muon vs adam via optimizer_idx)")
    print(f"  metric: win_rate (percentile/logit transform)")
    print(f"  {n_params} searchable hyperparameters (incl. optimizer)")
    print(f"  opponent: {opponent_name} ({opponent_type})")
    print(f"  downsample: {DOWNSAMPLE_POINTS} points per training curve")
    print(f"  timeout: {timeout_h:.1f}h")
    print(f"  max trials: {max_trials or 'unlimited'}")
    print(f"  existing trials: {len(existing_trials)}")

    trials_run = 0
    while True:
        if max_trials is not None and trials_run >= max_trials:
            print(f"\nreached max trials ({max_trials})")
            break
        if (time.time() - sweep_start) > timeout_s:
            print(f"\ntimeout reached ({timeout_h:.1f}h)")
            break

        # first trial uses defaults as anchor, rest use Protein suggestions
        if trial_idx == 0:
            params = deepcopy(DEFAULT_PARAMS)
            print("\ntrial 0: using default hyperparameters as anchor (muon)")
        else:
            fill = deepcopy(DEFAULT_PARAMS)
            params, info = protein.suggest(fill)
            if info:
                pred_cost = info.get("cost", 0)
                pred_score = info.get("score", 0)
                pred_wr = logit_inverse(pred_score) if pred_score != 0 else 0
                print(f"\nprotein prediction: score={pred_score:.3f} (wr~{pred_wr:.3f}), cost={pred_cost:.0f}s")

        optimizer = decode_optimizer(params)
        result = run_trial(trial_idx, params, protein, opponent_type)

        if result is not None:
            for obs in result["observations"]:
                save_observation({
                    "trial": trial_idx,
                    "params": obs["params"],
                    "win_rate": obs["win_rate"],
                    "cost": obs["cost"],
                    "step": obs["step"],
                    "optimizer": optimizer,
                }, obs_path)
        else:
            protein.observe(params, 0.0, 1.0, is_failure=True)
            save_observation({
                "trial": trial_idx,
                "params": params,
                "win_rate": 0.0,
                "cost": 1.0,
                "step": 0,
                "is_failure": True,
                "optimizer": optimizer,
            }, obs_path)

        trial_idx += 1
        trials_run += 1

    print_results(obs_path)


# ---------------------------------------------------------------------------
# main
# ---------------------------------------------------------------------------

def main() -> None:
    parser = argparse.ArgumentParser(
        description="protein hyperparameter sweep for metal static-native PVP training"
    )
    parser.add_argument("--timeout", type=float, default=12.0,
                        help="max hours to run (default: 12)")
    parser.add_argument("--max-trials", type=int, default=None,
                        help="max number of trials (default: unlimited)")
    parser.add_argument("--opponent", type=str, default=DEFAULT_OPPONENT,
                        choices=list(OPPONENT_TYPES.keys()),
                        help=f"opponent type (default: {DEFAULT_OPPONENT})")
    parser.add_argument("--results", action="store_true",
                        help="print results and exit")
    args = parser.parse_args()

    if args.results:
        print_results(SWEEP_DIR / "observations.jsonl")
        return

    run_sweep(
        max_trials=args.max_trials,
        timeout_h=args.timeout,
        opponent_name=args.opponent,
    )


if __name__ == "__main__":
    main()
