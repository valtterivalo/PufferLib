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

SWEEP_DIR_BASE = Path("runs/sweep_bench")
DOWNSAMPLE_POINTS = 5
LOG_INTERVAL = 5  # log every N iterations for early stop checks
MIN_SPS = 100_000  # abort trial if SPS below this after warmup


SWEEP_CONFIG = {
    "method": "Protein",
    "metric": "score",
    "metric_distribution": "linear",
    "goal": "maximize",
    "downsample": DOWNSAMPLE_POINTS,
    "use_gpu": False,
    "prune_pareto": True,
    "max_suggestion_cost": 3600,  # 1 hour max per trial (aligned with upstream)
    "early_stop_quantile": 0.3,

    "train": {
        "total_timesteps": {
            "distribution": "log_normal",
            "min": 30_000_000,
            "max": 10_000_000_000,
            "scale": "time",
        },
        "horizon": {
            "distribution": "uniform_pow2",
            "min": 32,
            "max": 64,
            "scale": "auto",
        },
        "learning_rate": {
            "distribution": "log_normal",
            "min": 0.00001,
            "max": 0.1,
            "scale": 0.5,
        },
        "ent_coef": {
            "distribution": "log_normal",
            "min": 0.00001,
            "max": 0.2,
            "scale": "auto",
        },
        "gamma": {
            "distribution": "logit_normal",
            "min": 0.8,
            "max": 0.9999,
            "scale": "auto",
        },
        "gae_lambda": {
            "distribution": "logit_normal",
            "min": 0.2,
            "max": 0.995,
            "scale": "auto",
        },
        "prio_alpha": {
            "distribution": "logit_normal",
            "min": 0.1,
            "max": 0.99,
            "scale": "auto",
        },
        "prio_beta0": {
            "distribution": "logit_normal",
            "min": 0.1,
            "max": 0.99,
            "scale": "auto",
        },
        "clip_coef": {
            "distribution": "uniform",
            "min": 0.01,
            "max": 1.0,
            "scale": "auto",
        },
        "vf_coef": {
            "distribution": "uniform",
            "min": 0.1,
            "max": 5.0,
            "scale": "auto",
        },
        "vf_clip_coef": {
            "distribution": "uniform",
            "min": 0.01,
            "max": 5.0,
            "scale": "auto",
        },
        "max_grad_norm": {
            "distribution": "uniform",
            "min": 0.1,
            "max": 5.0,
            "scale": "auto",
        },
        "replay_ratio": {
            "distribution": "uniform",
            "min": 0.25,
            "max": 4.0,
            "scale": "auto",
        },
        "minibatch_size": {
            "distribution": "uniform_pow2",
            "min": 4096,
            "max": 65536,
            "scale": "auto",
        },
        "total_agents": {
            "distribution": "uniform_pow2",
            "min": 256,
            "max": 16384,
            "scale": "auto",
        },
    },
    "policy": {
        "hidden_size": {
            "distribution": "uniform_pow2",
            "min": 32,
            "max": 1024,
            "scale": "auto",
        },
        "num_layers": {
            "distribution": "uniform",
            "min": 1,
            "max": 8,
            "scale": "auto",
        },
    },
    "optim": {
        "optimizer_idx": {
            "distribution": "uniform",
            "min": 0.0,
            "max": 1.0,
            "scale": "auto",
        },
    },
}

# defaults aligned with upstream static-native config/default.ini
DEFAULT_PARAMS = {
    "train": {
        "total_timesteps": 100_000_000,
        "horizon": 64,
        "learning_rate": 0.015,
        "ent_coef": 0.001,
        "gamma": 0.995,
        "gae_lambda": 0.90,
        "prio_alpha": 0.8,
        "prio_beta0": 0.2,
        "clip_coef": 0.2,
        "vf_coef": 2.0,
        "vf_clip_coef": 0.2,
        "max_grad_norm": 1.5,
        "replay_ratio": 1.0,
        "minibatch_size": 8192,
        "total_agents": 4096,
    },
    "policy": {
        "hidden_size": 128,
        "num_layers": 4,
    },
    "optim": {
        "optimizer_idx": 0.0,
    },
}


def decode_optimizer(params: dict) -> str:
    """Decode optimizer_idx: <0.5 = muon, >=0.5 = adam."""
    idx = params.get("optim", {}).get("optimizer_idx", 0.0)
    return "adam" if idx >= 0.5 else "muon"


def build_configs(
    env_name: str, params: dict,
) -> tuple[dict, dict, dict, dict]:
    """Convert Protein params dict to _C.create_pufferl config dicts."""
    train = params.get("train", {})
    policy = params.get("policy", {})
    optimizer = decode_optimizer(params)

    config = {
        "horizon": int(train.get("horizon", 64)),
        "learning_rate": train.get("learning_rate", 0.015),
        "min_lr_ratio": 0.1,
        "anneal_lr": 1.0,
        "beta1": 0.95,
        "beta2": 0.999,
        "eps": 1e-12,
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
        "vtrace_rho_clip": 1.0,
        "vtrace_c_clip": 1.0,
        "prio_alpha": train.get("prio_alpha", 0.8),
        "prio_beta0": train.get("prio_beta0", 0.2),
        "use_rnn": 1.0,
        "cudagraphs": -1.0,
        "kernels": 1.0,
        "profile": 0.0,
        "overlap": 1.0,
        "use_adam": 1.0 if optimizer == "adam" else 0.0,
        "env_name": env_name,
    }
    vec_config = {
        "total_agents": float(int(train.get("total_agents", 4096))),
        "num_buffers": 2.0,
        "num_threads": 4.0,
    }
    policy_config = {
        "hidden_size": float(int(policy.get("hidden_size", 128))),
        "num_layers": float(int(policy.get("num_layers", 4))),
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
    if model_cost > 512 * 512 * 2 and total_agents > 4096:
        train["total_agents"] = 4096

    # prevent expensive models with tiny batches (causes terrible SPS)
    if model_cost > 128 * 128 * 2 and total_agents < 2048:
        train["total_agents"] = 2048
    if model_cost > 256 * 256 * 4 and total_agents < 4096:
        train["total_agents"] = 4096

    if minibatch > batch_size:
        train["minibatch_size"] = batch_size

    replay = train.get("replay_ratio", 0.25)
    effective_mb = int(replay * batch_size / max(minibatch, 1))
    if effective_mb > 32:
        train["replay_ratio"] = 32 * minibatch / max(batch_size, 1)


def run_trial(
    trial_idx: int,
    env_name: str,
    params: dict,
    protein: Protein,
    sweep_dir: Path,
) -> dict | None:
    """Run a single in-process training trial via _C calls."""
    optimizer = decode_optimizer(params)
    flat = dict(pufferlib.unroll_nested_dict(params))
    total_steps = int(flat.get("train/total_timesteps", 0))
    total_agents = int(flat.get("train/total_agents", 4096))
    horizon = int(flat.get("train/horizon", 64))

    print(f"\n{'='*70}")
    print(f"trial {trial_idx}  ({total_steps/1e6:.1f}M steps, {optimizer})")
    for key, value in sorted(flat.items()):
        short_key = key.split("/")[-1]
        fmt = f"{value:.6f}" if isinstance(value, float) else str(value)
        print(f"  {short_key:20s} = {fmt}")
    print(f"{'='*70}")

    log_dir = sweep_dir / f"trial_{trial_idx}"
    log_dir.mkdir(parents=True, exist_ok=True)

    with (log_dir / "config.json").open("w") as f:
        json.dump({"params": params, "optimizer": optimizer}, f, indent=2)

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
        "optimizer": optimizer,
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
            "optimizer": best.get("optimizer", "muon"),
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
            nl = int(flat.get("policy/num_layers", 0))
            steps_m = t["step"] / 1e6
            opt = t.get("optimizer", "muon")
            sps = t.get("mean_sps", 0)
            sps_str = f"  sps={sps/1e6:.2f}M" if sps > 0 else ""
            print(
                f"  #{t['trial']:3d}  score={t['score']:>8.2f}  "
                f"steps={steps_m:>5.1f}M  wall={t['cost']:>5.0f}s{sps_str}  "
                f"hz={hz:>3}  lr={lr:.4f}  ent={ent:.4f}  hs={hs}  L{nl}  {opt}"
            )

    by_score = sorted(trial_summaries, key=lambda t: t["score"], reverse=True)
    print(f"\ntop 15 by score:")
    for t in by_score[:15]:
        flat = dict(pufferlib.unroll_nested_dict(t["params"]))
        hz = int(flat.get("train/horizon", 0))
        lr = flat.get("train/learning_rate", 0)
        ent = flat.get("train/ent_coef", 0)
        hs = int(flat.get("policy/hidden_size", 0))
        nl = int(flat.get("policy/num_layers", 0))
        steps_m = t["step"] / 1e6
        opt = t.get("optimizer", "muon")
        sps = t.get("mean_sps", 0)
        sps_str = f"  sps={sps/1e6:.2f}M" if sps > 0 else ""
        is_pareto = " *" if t["trial"] in pareto_ids else ""
        print(
            f"  #{t['trial']:3d}  score={t['score']:>8.2f}  "
            f"steps={steps_m:>5.1f}M  wall={t['cost']:>5.0f}s{sps_str}  "
            f"hz={hz:>3}  lr={lr:.4f}  ent={ent:.4f}  hs={hs}  L{nl}  {opt}{is_pareto}"
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
    print(f"  metric: score (linear distribution)")
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

        optimizer = decode_optimizer(params)
        result = run_trial(trial_idx, env_name, params, protein, sweep_dir)

        if result is not None:
            for obs in result["observations"]:
                save_observation({
                    "trial": trial_idx,
                    "params": obs["params"],
                    "score": obs["score"],
                    "cost": obs["cost"],
                    "step": obs["step"],
                    "optimizer": optimizer,
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
                "optimizer": optimizer,
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
