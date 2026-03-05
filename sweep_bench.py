#!/usr/bin/env python3
"""Protein hyperparameter sweep for Metal simple envs (breakout, g2048).

Runs training in-process via _C calls (no subprocess overhead). Each trial
gets a fresh pufferl instance with full Metal teardown between trials.

Each env must be built first: python setup.py build_<env> --force

Usage:
    python sweep_bench.py --env breakout
    python sweep_bench.py --env g2048 --timeout 2
    python sweep_bench.py --env breakout --results
"""

from __future__ import annotations

import argparse
import json
import math
import sys
import time
from copy import deepcopy
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))

import pufferlib
from bench import ENV_DEFAULTS
from pufferlib import _C
from pufferlib.pufferl import downsample
from pufferlib.sweep import Protein, pareto_points, prune_pareto_front

# Keep logs live when piping through tee (stdout is block-buffered otherwise).
if hasattr(sys.stdout, "reconfigure"):
    sys.stdout.reconfigure(line_buffering=True, write_through=True)
if hasattr(sys.stderr, "reconfigure"):
    sys.stderr.reconfigure(line_buffering=True, write_through=True)

SWEEP_DIR_BASE = Path("runs/sweep_bench")
DOWNSAMPLE_POINTS = 5
LOG_INTERVAL = 5  # log every N iterations for early stop checks
MIN_SPS = 300_000  # abort trial if SPS below this after warmup (L2 training-dominated configs ~450-550K)


SWEEP_CONFIG = {
    "method": "Protein",
    "metric": "score",
    "metric_distribution": "linear",
    "goal": "maximize",
    "downsample": DOWNSAMPLE_POINTS,
    "use_gpu": False,
    "prune_pareto": True,
    "max_suggestion_cost": 1800,  # keep suggestions in the short-run sweep budget
    "early_stop_quantile": 0.3,

    "train": {
        "total_timesteps": {
            "distribution": "log_normal",
            "min": 60_000_000,
            "max": 200_000_000,
            "scale": "time",
        },
        "horizon": {
            "distribution": "uniform_pow2",
            "min": 16,
            "max": 64,
            "scale": "auto",
        },
        "min_lr_ratio": {
            "distribution": "uniform",
            "min": 0.0,
            "max": 0.25,
            "scale": "auto",
        },
        "learning_rate": {
            "distribution": "log_normal",
            "min": 0.01,
            "max": 0.3,
            "scale": 0.5,
        },
        "beta1": {
            "distribution": "uniform",
            "min": 0.5,
            "max": 0.95,
            "scale": "auto",
        },
        "beta2": {
            "distribution": "logit_normal",
            "min": 0.95,
            "max": 0.99999,
            "scale": "auto",
        },
        "eps": {
            "distribution": "log_normal",
            "min": 1e-6,
            "max": 1e-3,
            "scale": "auto",
        },
        "ent_coef": {
            "distribution": "log_normal",
            "min": 0.0005,
            "max": 0.02,
            "scale": "auto",
        },
        "gamma": {
            "distribution": "logit_normal",
            "min": 0.88,
            "max": 0.998,
            "scale": "auto",
        },
        "gae_lambda": {
            "distribution": "logit_normal",
            "min": 0.8,
            "max": 0.995,
            "scale": "auto",
        },
        "vtrace_rho_clip": {
            "distribution": "uniform",
            "min": 1.0,
            "max": 4.0,
            "scale": "auto",
        },
        "vtrace_c_clip": {
            "distribution": "uniform",
            "min": 1.0,
            "max": 3.0,
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
            "min": 0.5,
            "max": 0.99,
            "scale": "auto",
        },
        "clip_coef": {
            "distribution": "uniform",
            "min": 0.1,
            "max": 1.5,
            "scale": "auto",
        },
        "vf_coef": {
            "distribution": "uniform",
            "min": 0.5,
            "max": 8.0,
            "scale": "auto",
        },
        "vf_clip_coef": {
            "distribution": "uniform",
            "min": 0.3,
            "max": 6.0,
            "scale": "auto",
        },
        "max_grad_norm": {
            "distribution": "uniform",
            "min": 0.5,
            "max": 4.0,
            "scale": "auto",
        },
        "replay_ratio": {
            "distribution": "uniform",
            "min": 0.5,
            "max": 4.0,
            "scale": "auto",
        },
        "minibatch_size": {
            "distribution": "uniform_pow2",
            "min": 16384,
            "max": 131072,
            "scale": "auto",
        },
        "total_agents": {
            "distribution": "uniform_pow2",
            "min": 2048,
            "max": 8192,
            "scale": "auto",
        },
        "num_buffers": {
            "distribution": "uniform_pow2",
            "min": 2,
            "max": 8,
            "scale": "auto",
        },
        # num_threads always set to match num_buffers in clamp_params
    },
    "policy": {
        # hidden_size fixed at 64 in build_configs (128 is numerically unstable on Metal)
        # L1 models consistently score <10 on breakout, so floor at 2
        "num_layers": {
            "distribution": "uniform",
            "min": 2,
            "max": 3.5,
            "scale": "auto",
        },
    },
}

# best known Metal breakout config (trial #95, score 836 at 176M, K-split TN GEMM)
DEFAULT_PARAMS = {
    "train": {
        "total_timesteps": 176_000_000,
        "horizon": 32,
        "min_lr_ratio": 0.113,
        "learning_rate": 0.0669,
        "beta1": 0.523,
        "beta2": 0.993,
        "eps": 6e-6,
        "ent_coef": 0.0081,
        "gamma": 0.920,
        "gae_lambda": 0.931,
        "vtrace_rho_clip": 1.828,
        "vtrace_c_clip": 1.000,
        "prio_alpha": 0.010,
        "prio_beta0": 0.976,
        "clip_coef": 0.756,
        "vf_coef": 2.956,
        "vf_clip_coef": 2.824,
        "max_grad_norm": 2.046,
        "replay_ratio": 2.10,
        "minibatch_size": 65536,
        "total_agents": 2048,
        "num_buffers": 8,
        "num_threads": 8,
    },
    "policy": {
        "hidden_size": 64,
        "num_layers": 2,
    },
}


def build_configs(
    env_name: str, params: dict,
) -> tuple[dict, dict, dict, dict]:
    """Convert Protein params dict to _C.create_pufferl config dicts."""
    train = params.get("train", {})
    policy = params.get("policy", {})

    config = {
        "horizon": int(train.get("horizon", 64)),
        "learning_rate": train.get("learning_rate", 0.1),
        "min_lr_ratio": train.get("min_lr_ratio", 0.0),
        "anneal_lr": 1.0,
        "beta1": train.get("beta1", 0.73),
        "beta2": train.get("beta2", 0.9986),
        "eps": train.get("eps", 8.3e-5),
        "minibatch_size": int(train.get("minibatch_size", 8192)),
        "replay_ratio": train.get("replay_ratio", 1.0),
        "total_timesteps": int(train.get("total_timesteps", 100_000_000)),
        "max_grad_norm": train.get("max_grad_norm", 1.5),
        "clip_coef": train.get("clip_coef", 0.2),
        "vf_clip_coef": train.get("vf_clip_coef", 0.2),
        "vf_coef": train.get("vf_coef", 2.0),
        "ent_coef": train.get("ent_coef", 0.001),
        "gamma": train.get("gamma", 0.995),
        "gae_lambda": train.get("gae_lambda", 0.90),
        "vtrace_rho_clip": train.get("vtrace_rho_clip", 2.0),
        "vtrace_c_clip": train.get("vtrace_c_clip", 1.1),
        "prio_alpha": train.get("prio_alpha", 0.8),
        "prio_beta0": train.get("prio_beta0", 0.2),
        "use_rnn": 1.0,
        "cudagraphs": -1.0,
        "kernels": 1.0,
        "profile": 0.0,
        "overlap": 1.0,
        "cpu_inference": 1.0,
        "env_name": env_name,
    }
    vec_config = {
        "total_agents": float(int(train.get("total_agents", 4096))),
        "num_buffers": float(int(train.get("num_buffers", 1))),
        "num_threads": float(int(train.get("num_threads", 1))),
    }
    policy_config = {
        "hidden_size": 64.0,  # fixed: 128 is numerically unstable on Metal
        "num_layers": float(int(policy.get("num_layers", 2))),
        "arch": 1.0,  # always simple for bench sweep
    }
    env_config = ENV_DEFAULTS[env_name]

    return config, vec_config, env_config, policy_config


def save_observation(obs: dict, path: Path) -> None:
    """Append observation to JSONL persistence file."""
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("a") as f:
        f.write(json.dumps(obs) + "\n")


def load_observations(path: Path) -> list[dict]:
    """Load all observations from JSONL persistence file."""
    if not path.exists():
        return []
    records = []
    with path.open() as f:
        for line in f:
            line = line.strip()
            if line:
                records.append(json.loads(line))
    return records


def clamp_params(params: dict) -> None:
    """Enforce cross-parameter constraints."""
    train = params.get("train", {})
    policy = params.get("policy", {})
    hidden = int(policy.get("hidden_size", 128))
    layers = int(policy.get("num_layers", 1))
    total_agents = int(train.get("total_agents", 2048))
    horizon = int(train.get("horizon", 32))
    minibatch = int(train.get("minibatch_size", 4096))

    batch_size = total_agents * horizon

    model_cost = hidden * hidden * layers
    if model_cost > 512 * 512 * 2 and total_agents > 8192:
        train["total_agents"] = 8192

    # prevent expensive models with tiny batches (causes terrible SPS)
    if model_cost > 128 * 128 * 2 and total_agents < 2048:
        train["total_agents"] = 2048

    if minibatch > batch_size:
        train["minibatch_size"] = batch_size

    replay = train.get("replay_ratio", 0.25)
    effective_mb = int(replay * batch_size / max(minibatch, 1))
    if effective_mb > 32:
        train["replay_ratio"] = 32 * minibatch / max(batch_size, 1)

    # Clamp to wider stable regime — must match SWEEP_CONFIG ranges.
    train["learning_rate"] = min(max(float(train.get("learning_rate", 0.1)), 0.005), 0.4)
    train["beta1"] = min(max(float(train.get("beta1", 0.73)), 0.4), 0.98)
    train["beta2"] = min(max(float(train.get("beta2", 0.9986)), 0.98), 0.999995)
    train["eps"] = min(max(float(train.get("eps", 8.3e-5)), 1e-7), 1e-2)
    train["ent_coef"] = min(max(float(train.get("ent_coef", 0.0033)), 1e-4), 0.05)
    train["gamma"] = min(max(float(train.get("gamma", 0.972)), 0.85), 0.999)
    train["gae_lambda"] = min(max(float(train.get("gae_lambda", 0.949)), 0.75), 0.999)
    train["vtrace_rho_clip"] = min(max(float(train.get("vtrace_rho_clip", 2.1)), 1.0), 5.0)
    train["vtrace_c_clip"] = min(max(float(train.get("vtrace_c_clip", 1.08)), 1.0), 4.0)
    train["clip_coef"] = min(max(float(train.get("clip_coef", 0.67)), 0.05), 2.0)
    train["vf_coef"] = min(max(float(train.get("vf_coef", 1.22)), 0.1), 10.0)
    train["vf_clip_coef"] = min(max(float(train.get("vf_clip_coef", 1.23)), 0.1), 8.0)
    train["max_grad_norm"] = min(max(float(train.get("max_grad_norm", 1.81)), 0.3), 6.0)
    train["replay_ratio"] = min(max(float(train.get("replay_ratio", 1.4)), 0.25), 5.0)
    train["min_lr_ratio"] = min(max(float(train.get("min_lr_ratio", 0.0)), 0.0), 0.35)

    # num_threads must match num_buffers (one thread per buffer)
    num_buf = int(train.get("num_buffers", 1))
    train["num_threads"] = num_buf



def run_trial(
    trial_idx: int,
    env_name: str,
    params: dict,
    protein: Protein,
    sweep_dir: Path,
) -> dict | None:
    """Run a single in-process training trial via _C calls."""
    flat = dict(pufferlib.unroll_nested_dict(params))
    total_steps = int(flat.get("train/total_timesteps", 0))
    total_agents = int(flat.get("train/total_agents", 4096))
    horizon = int(flat.get("train/horizon", 64))

    print(f"\n{'='*70}")
    print(f"trial {trial_idx}  ({total_steps/1e6:.1f}M steps, muon)")
    for key, value in sorted(flat.items()):
        short_key = key.split("/")[-1]
        fmt = f"{value:.6f}" if isinstance(value, float) else str(value)
        print(f"  {short_key:20s} = {fmt}")
    print(f"{'='*70}")

    log_dir = sweep_dir / f"trial_{trial_idx}"
    log_dir.mkdir(parents=True, exist_ok=True)

    with (log_dir / "config.json").open("w") as f:
        json.dump({"params": params}, f, indent=2)

    pufferl = None
    try:
        config, vec_config, env_config, policy_config = build_configs(
            env_name, params,
        )
        pufferl = _C.create_pufferl(config, vec_config, env_config, policy_config)
        print(f"  params: {pufferl.num_params():,}")

        # warmup
        _C.rollouts(pufferl)
        _C.train(pufferl)
        _C.log_losses(pufferl)

        steps_per_iter = total_agents * horizon
        total_iters = total_steps // steps_per_iter
        global_step = 0
        start_time = time.time()
        t_last_log = start_time
        last_report_time = start_time
        entries: list[dict] = []
        early_stopped = False

        for iteration in range(1, total_iters + 1):
            _C.rollouts(pufferl)
            _C.train(pufferl)
            global_step += steps_per_iter

            if iteration % LOG_INTERVAL == 0:
                now = time.time()
                elapsed_since_log = now - t_last_log
                sps = (LOG_INTERVAL * steps_per_iter) / elapsed_since_log
                t_last_log = now

                losses = _C.log_losses(pufferl)
                env_stats = _C.log_environments(pufferl)
                for loss_name in ("entropy", "pg_loss", "vf_loss"):
                    loss_value = losses.get(loss_name)
                    if loss_value is None or not math.isfinite(float(loss_value)):
                        raise RuntimeError(
                            f"invalid loss metric {loss_name}={loss_value} "
                            f"at step={global_step}"
                        )

                score = env_stats.get("score", env_stats.get("episode_return", 0))
                ep_ret = env_stats.get("episode_return", 0)
                ep_len = env_stats.get("episode_length", 0)
                ent = losses.get("entropy", 0)

                entries.append({
                    "step": global_step,
                    "sps": sps,
                    "score": score,
                    "episode_return": ep_ret,
                    "episode_length": ep_len,
                    "entropy": ent,
                    "pg_loss": losses.get("pg_loss", 0),
                    "vf_loss": losses.get("vf_loss", 0),
                })

                elapsed = now - start_time
                if now - last_report_time > 30:
                    mean_sps = sum(e["sps"] for e in entries) / len(entries)
                    print(
                        f"  [{global_step/1e6:.1f}M / {total_steps/1e6:.0f}M]  "
                        f"score={score:.2f}  sps={mean_sps:.0f}  "
                        f"elapsed={elapsed:.0f}s"
                    )
                    last_report_time = now

                # abort if SPS is too low (bad param combo for the hardware)
                if len(entries) <= 2 and sps < MIN_SPS:
                    print(
                        f"  ABORT: SPS={sps:.0f} < {MIN_SPS} "
                        f"(bad param combo), skipping trial"
                    )
                    early_stopped = True
                    break

                if protein.should_stop(score, elapsed):
                    print(
                        f"  EARLY STOP at {global_step/1e6:.1f}M steps "
                        f"({elapsed:.0f}s), score={score:.2f}"
                    )
                    early_stopped = True
                    break

        elapsed = time.time() - start_time

    except Exception as e:
        print(f"  FAILED: {e}")
        return None
    finally:
        if pufferl is not None:
            _C.close(pufferl)

    if not entries:
        print(f"  FAILED: no metric entries ({elapsed:.0f}s)")
        return None

    scores = [e["score"] for e in entries]
    steps = [e["step"] for e in entries]

    downsampled_scores = downsample(scores, DOWNSAMPLE_POINTS)
    downsampled_steps = downsample(steps, DOWNSAMPLE_POINTS)

    observations = []
    for score, step in zip(downsampled_scores, downsampled_steps):
        obs_params = deepcopy(params)
        obs_params["train"]["total_timesteps"] = step
        cost = elapsed * (step / max(steps[-1], 1))
        protein.observe(obs_params, score, cost)
        observations.append({
            "params": obs_params,
            "score": score,
            "cost": cost,
            "step": step,
        })

    final_score = scores[-1]
    tail_start = int(len(scores) * 0.75)
    tail_score = sum(scores[tail_start:]) / max(len(scores[tail_start:]), 1)
    mean_sps = sum(e["sps"] for e in entries) / len(entries)
    stop_label = "  (early)" if early_stopped else ""

    print(
        f"  DONE  score={final_score:.2f}  tail={tail_score:.2f}  "
        f"sps={mean_sps:.0f}  steps={steps[-1]/1e6:.1f}M  "
        f"wall={elapsed:.0f}s{stop_label}"
    )

    return {
        "trial": trial_idx,
        "params": params,
        "final_score": final_score,
        "tail_score": tail_score,
        "mean_sps": mean_sps,
        "total_steps": steps[-1],
        "wall_seconds": elapsed,
        "observations": observations,
    }


def print_results(obs_path: Path) -> None:
    """Print sweep results from persisted observations."""
    records = load_observations(obs_path)
    if not records:
        print("no observations found")
        return

    trials: dict[int, list[dict]] = {}
    for r in records:
        trials.setdefault(r["trial"], []).append(r)

    trial_summaries = []
    for tid, obs_list in sorted(trials.items()):
        best = max(obs_list, key=lambda o: o["step"])
        score = best.get("score", best.get("episode_return", 0))
        trial_summaries.append({
            "trial": tid,
            "score": score,
            "cost": best["cost"],
            "step": best["step"],
            "params": best["params"],
            "mean_sps": best.get("mean_sps", 0),
            "output": score,
        })

    print(f"\n{'='*70}")
    print(f"sweep results: {len(trial_summaries)} trials")
    print(f"{'='*70}")

    valid = [t for t in trial_summaries if t["step"] > 0]

    pareto, _ = pareto_points(valid)
    pruned = prune_pareto_front(pareto)
    pareto_ids = {t["trial"] for t in pruned}

    if pruned:
        best = pruned[-1]
        print(f"\nbest on pareto front: #{best['trial']}")
        print(f"  score: {best['score']:.2f}")
        print(f"  steps: {best['step']/1e6:.1f}M")
        print(f"  wall: {best['cost']:.0f}s")
        flat = dict(pufferlib.unroll_nested_dict(best["params"]))
        print("\n  hyperparameters:")
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
            nl = int(flat.get("policy/num_layers", 0))
            steps_m = t["step"] / 1e6
            sps = t.get("mean_sps", 0)
            sps_str = f"  sps={sps/1e6:.2f}M" if sps > 0 else ""
            print(
                f"  #{t['trial']:3d}  score={t['score']:>8.2f}  "
                f"steps={steps_m:>5.1f}M  wall={t['cost']:>5.0f}s{sps_str}  "
                f"hz={hz:>3}  lr={lr:.4f}  ent={ent:.4f}  hs={hs}  L{nl}"
            )

    by_score = sorted(trial_summaries, key=lambda t: t["score"], reverse=True)
    print("\ntop 15 by score:")
    for t in by_score[:15]:
        flat = dict(pufferlib.unroll_nested_dict(t["params"]))
        hz = int(flat.get("train/horizon", 0))
        lr = flat.get("train/learning_rate", 0)
        ent = flat.get("train/ent_coef", 0)
        hs = int(flat.get("policy/hidden_size", 0))
        nl = int(flat.get("policy/num_layers", 0))
        steps_m = t["step"] / 1e6
        sps = t.get("mean_sps", 0)
        sps_str = f"  sps={sps/1e6:.2f}M" if sps > 0 else ""
        is_pareto = " *" if t["trial"] in pareto_ids else ""
        print(
            f"  #{t['trial']:3d}  score={t['score']:>8.2f}  "
            f"steps={steps_m:>5.1f}M  wall={t['cost']:>5.0f}s{sps_str}  "
            f"hz={hz:>3}  lr={lr:.4f}  ent={ent:.4f}  hs={hs}  L{nl}{is_pareto}"
        )


def run_sweep(env_name: str, max_trials: int | None, timeout_h: float) -> None:
    """Run the Protein sweep for a simple env."""
    sweep_dir = SWEEP_DIR_BASE / env_name
    sweep_dir.mkdir(parents=True, exist_ok=True)
    obs_path = sweep_dir / "observations.jsonl"

    protein = Protein(SWEEP_CONFIG, use_gpu=False, prune_pareto=True)

    existing_records = load_observations(obs_path)
    existing_trials: set[int] = set()
    if existing_records:
        for r in existing_records:
            existing_trials.add(r["trial"])
            score = r.get("score", r.get("episode_return", 0))
            protein.observe(r["params"], score, r["cost"])
        print(f"replayed {len(existing_records)} observations from {len(existing_trials)} previous trials")

    trial_idx = max(existing_trials) + 1 if existing_trials else 0
    sweep_start = time.time()
    timeout_s = timeout_h * 3600

    n_params = len(dict(pufferlib.unroll_nested_dict(
        {k: v for k, v in SWEEP_CONFIG.items() if isinstance(v, dict)}
    )))

    print(f"protein sweep ({env_name}, metal, in-process)")
    print("  metric: score (linear distribution)")
    print(f"  {n_params} searchable hyperparameters")
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

        if trial_idx == 0:
            params = deepcopy(DEFAULT_PARAMS)
            print("\ntrial 0: using default hyperparameters as anchor")
        else:
            fill = deepcopy(DEFAULT_PARAMS)
            params, info = protein.suggest(fill)
            clamp_params(params)
            if info:
                pred_cost = info.get("cost", 0)
                pred_score = info.get("score", 0)
                print(f"\nprotein prediction: score={pred_score:.3f}, cost={pred_cost:.0f}s")

        result = run_trial(trial_idx, env_name, params, protein, sweep_dir)

        if result is not None:
            for obs in result["observations"]:
                save_observation({
                    "trial": trial_idx,
                    "params": obs["params"],
                    "score": obs["score"],
                    "cost": obs["cost"],
                    "step": obs["step"],
                    "mean_sps": result["mean_sps"],
                }, obs_path)
        else:
            protein.observe(params, 0.0, 1.0, is_failure=True)
            save_observation({
                "trial": trial_idx,
                "params": params,
                "score": 0.0,
                "cost": 1.0,
                "step": 0,
                "is_failure": True,
                "mean_sps": 0,
            }, obs_path)

        trial_idx += 1
        trials_run += 1

    print_results(obs_path)


def main() -> None:
    parser = argparse.ArgumentParser(description="protein sweep for Metal simple envs")
    parser.add_argument("--env", type=str, required=True, choices=["breakout", "g2048"])
    parser.add_argument("--timeout", type=float, default=4.0,
                        help="max hours (default: 4)")
    parser.add_argument("--max-trials", type=int, default=None)
    parser.add_argument("--results", action="store_true",
                        help="print results and exit")
    args = parser.parse_args()

    if args.results:
        print_results(SWEEP_DIR_BASE / args.env / "observations.jsonl")
        return

    run_sweep(args.env, max_trials=args.max_trials, timeout_h=args.timeout)


if __name__ == "__main__":
    main()
