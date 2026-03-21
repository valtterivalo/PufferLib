#!/usr/bin/env python3
"""pufferl -- unified Metal RL training CLI.

usage:
    python pufferl.py train <env> [args]     single training run
    python pufferl.py sweep <env> [args]     Protein hyperparameter sweep
    python pufferl.py results <env>          print sweep results

requires building the env first: python setup.py build_<env> --force
"""

from __future__ import annotations

import argparse
import ast
import configparser
import glob
import json
import math
import os
import sys
import time
from copy import deepcopy
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))

import pufferlib
from pufferlib import _C
from pufferlib.pufferl import downsample
from pufferlib.sweep import Protein, pareto_points, prune_pareto_front

# keep logs live when piping through tee
if hasattr(sys.stdout, "reconfigure"):
    sys.stdout.reconfigure(line_buffering=True, write_through=True)
if hasattr(sys.stderr, "reconfigure"):
    sys.stderr.reconfigure(line_buffering=True, write_through=True)


# ============================================================================
# config loading (reads .ini files, builds argparse dynamically)
# ============================================================================

METAL_CONFIG_DIR = Path(__file__).parent / "pufferlib" / "config" / "metal"
SWEEP_DIR_BASE = Path("runs/sweep_bench")
LOG_INTERVAL = 5


def _parse_ini_value(raw: str):
    """Parse a single .ini value into its Python type.

    Uses ast.literal_eval (safe: only parses Python literals like strings,
    numbers, booleans, None — no arbitrary code execution). Falls back to
    returning the raw string if literal_eval can't parse it.
    """
    try:
        return ast.literal_eval(raw)
    except (ValueError, SyntaxError):
        return raw


def load_config(env_name: str) -> dict:
    """Load Metal config from default.ini + per-env .ini, merged via configparser.

    Returns a nested dict with sections as top-level keys. Sweep sections
    (sweep.train.*, sweep.policy.*) are restructured into {"sweep": {"train": {...}, ...}}.
    """
    default_ini = METAL_CONFIG_DIR / "default.ini"
    env_ini = METAL_CONFIG_DIR / "ocean" / f"{env_name}.ini"

    if not env_ini.exists():
        raise FileNotFoundError(f"no Metal config for env '{env_name}': {env_ini}")

    p = configparser.ConfigParser()
    p.read([str(default_ini), str(env_ini)])

    config = {}
    for section in p.sections():
        parsed_section = {}
        for key in p[section]:
            parsed_section[key] = _parse_ini_value(p[section][key])
        config[section] = parsed_section

    # restructure sweep.train.X / sweep.policy.X into nested sweep dict
    sweep = config.pop("sweep", {})
    sweep_params = {"train": {}, "policy": {}, "env": {}}
    sweep_sections_to_remove = []
    for section_name, section_data in list(config.items()):
        if section_name.startswith("sweep."):
            parts = section_name.split(".", 2)  # sweep.train.learning_rate
            if len(parts) == 3:
                group, param = parts[1], parts[2]
                sweep_params.setdefault(group, {})[param] = section_data
            sweep_sections_to_remove.append(section_name)
    for key in sweep_sections_to_remove:
        config.pop(key)

    # merge sweep metadata + param ranges
    sweep_params = {k: v for k, v in sweep_params.items() if v}
    config["sweep"] = {**sweep, **sweep_params}

    return config


def apply_cli_overrides(config: dict, env_name: str) -> dict:
    """Build argparse from config keys, parse CLI, apply overrides.

    Mutates and returns config. CLI args like --learning-rate override
    config["train"]["learning_rate"].
    """
    parser = argparse.ArgumentParser(
        description=f"Metal training for {env_name}",
        add_help=True,
    )

    # register all config keys as CLI args (skip sweep sections)
    arg_registry = {}  # maps cli_name -> (section, key)
    for section in ("train", "policy", "vec", "env", "base"):
        section_data = config.get(section, {})
        for key, value in section_data.items():
            cli_name = f"--{key.replace('_', '-')}"
            if cli_name in arg_registry:
                continue
            arg_registry[cli_name] = (section, key)
            if isinstance(value, bool):
                parser.add_argument(cli_name, type=lambda x: x.lower() in ("1", "true", "yes"),
                                    default=value)
            elif isinstance(value, int):
                parser.add_argument(cli_name, type=int, default=value)
            elif isinstance(value, float):
                parser.add_argument(cli_name, type=float, default=value)
            elif isinstance(value, str):
                parser.add_argument(cli_name, type=str, default=value)
            else:
                parser.add_argument(cli_name, type=type(value), default=value)

    # extra CLI-only args
    parser.add_argument("--no-overlap", action="store_true")
    parser.add_argument("--fp16", action="store_true",
                        help="fp16 training activations/grads (rollout stays fp32)")
    parser.add_argument("--log-interval", type=int, default=10)
    parser.add_argument("--checkpoint-interval", type=int, default=200,
                        help="save weights every N iterations")
    parser.add_argument("--checkpoint-dir", type=str, default="",
                        help="checkpoint directory (default: checkpoints/<env>/<run_id>)")
    parser.add_argument("--load-model-path", type=str, default="latest",
                        help="path to checkpoint for eval mode (default: latest)")
    parser.add_argument("--trace-path", type=str, default="")
    parser.add_argument("--trace-every", type=int, default=1)
    parser.add_argument("--timeout", type=float, default=4.0,
                        help="max sweep hours (default: 4)")
    parser.add_argument("--max-trials", type=int, default=None)
    parser.add_argument("--results", action="store_true",
                        help="print sweep results and exit")
    parser.add_argument("--wandb", action="store_true",
                        help="log to wandb")
    parser.add_argument("--wandb-project", type=str, default="pufferlib-metal")
    parser.add_argument("--wandb-group", type=str, default="debug")
    parser.add_argument("--tag", type=str, default=None)

    args = parser.parse_args()

    # apply overrides back into config
    parsed = vars(args)
    for cli_name, (section, key) in arg_registry.items():
        attr = cli_name.lstrip("-").replace("-", "_")
        if attr in parsed:
            config.setdefault(section, {})[key] = parsed[attr]

    # handle special flags
    if args.no_overlap:
        config.setdefault("train", {})["overlap"] = 0
    if args.fp16:
        config.setdefault("train", {})["train_fp16"] = 1

    config["_cli"] = {
        "log_interval": args.log_interval,
        "checkpoint_interval": args.checkpoint_interval,
        "checkpoint_dir": args.checkpoint_dir,
        "load_model_path": args.load_model_path,
        "trace_path": args.trace_path,
        "trace_every": args.trace_every,
        "timeout": args.timeout,
        "max_trials": args.max_trials,
        "results": args.results,
        "wandb": args.wandb,
        "wandb_project": args.wandb_project,
        "wandb_group": args.wandb_group,
        "tag": args.tag,
    }

    return config


# ============================================================================
# training engine
# ============================================================================

def build_configs(env_name: str, config: dict):
    """Convert loaded config dict to the 4 config dicts for _C.create_pufferl.

    Accepts either:
      - full config from load_config: {"train": {...}, "policy": {...}, "vec": {...}, ...}
      - nested Protein format: {"train": {...}, "policy": {...}}
    """
    train = config.get("train", {})
    policy = config.get("policy", {})
    vec = config.get("vec", {})
    env = config.get("env", {})

    horizon = int(train.get("horizon", 64))
    total_agents = int(vec.get("total_agents", train.get("total_agents", 4096)))
    num_buffers = int(vec.get("num_buffers", train.get("num_buffers", 1)))
    minibatch_size = int(train.get("minibatch_size", 8192))

    # structural: minibatch can't exceed batch size
    batch_size = total_agents * horizon
    if minibatch_size > batch_size:
        minibatch_size = batch_size

    c = {
        "horizon": horizon,
        "learning_rate": train.get("learning_rate", 0.1),
        "min_lr_ratio": train.get("min_lr_ratio", 0.0),
        "anneal_lr": 1.0,
        "beta1": train.get("beta1", 0.73),
        "beta2": train.get("beta2", 0.9986),
        "eps": train.get("eps", 8.3e-5),
        "minibatch_size": minibatch_size,
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
        "profile": float(int(train.get("profile", 0))),
        "overlap": float(int(train.get("overlap", 1))),
        "cpu_inference": float(int(train.get("cpu_inference",
            1 if env_name in ("breakout", "g2048") else 0))),
        "train_fp16": float(int(train.get("train_fp16", 0))),
        "ns_iters": float(int(train.get("ns_iters", 5))),
        "seed": float(int(train.get("seed", 42))),
        "env_name": env_name,
    }
    vec_config = {
        "total_agents": float(total_agents),
        "num_buffers": float(num_buffers),
        "num_threads": float(num_buffers),  # structural: must match
    }
    policy_config = {
        "hidden_size": float(int(policy.get("hidden_size", 64))),
        "num_layers": float(int(policy.get("num_layers", 2))),
    }
    env_config = dict(env)

    if "scaffolding_ratio" in train:
        env_config["scaffolding_ratio"] = train["scaffolding_ratio"]

    return c, vec_config, env_config, policy_config


class TrainingResult:
    """Return value from run_training."""
    __slots__ = ("score", "episode_return", "sps", "steps", "elapsed",
                 "entries", "profile", "pufferl")

    def __init__(self):
        self.score = 0.0
        self.episode_return = 0.0
        self.sps = 0.0
        self.steps = 0
        self.elapsed = 0.0
        self.entries = []
        self.profile = {}
        self.pufferl = None


def run_training(config, vec_config, env_config, policy_config, *,
                 log_interval=5, score_key="score",
                 on_log=None, should_stop=None, on_iteration=None,
                 keep_alive=False,
                 checkpoint_dir=None, checkpoint_interval=200):
    """Run the Metal training loop.

    on_log(iteration, global_step, sps, losses, env_stats):
        called every log_interval iterations. return value ignored.
    should_stop(score, elapsed):
        called every log_interval iterations. return True to early-stop.
    on_iteration(pufferl, global_step):
        called every iteration (for PFSP weight updates etc.)
    keep_alive:
        if True, don't close pufferl -- caller gets it via result.pufferl.
    checkpoint_dir:
        if set, save weights every checkpoint_interval iterations.
    """
    total_agents = int(vec_config["total_agents"])
    horizon = int(config["horizon"])
    total_steps = int(config["total_timesteps"])
    steps_per_iter = total_agents * horizon
    total_iters = total_steps // steps_per_iter

    result = TrainingResult()
    pufferl = None

    try:
        pufferl = _C.create_pufferl(config, vec_config, env_config, policy_config)
        result.pufferl = pufferl

        # warmup
        _C.rollouts(pufferl)
        _C.train(pufferl)
        _C.log_losses(pufferl)

        global_step = 0
        start_time = time.time()
        t_last_log = start_time

        if checkpoint_dir:
            os.makedirs(checkpoint_dir, exist_ok=True)

        for iteration in range(1, total_iters + 1):
            _C.rollouts(pufferl)
            _C.train(pufferl)
            global_step += steps_per_iter

            if checkpoint_dir and (iteration % checkpoint_interval == 0 or iteration == total_iters):
                path = os.path.join(checkpoint_dir, f"{global_step:016d}.bin")
                _C.save_weights(pufferl, path)

            if on_iteration:
                on_iteration(pufferl, global_step)

            if iteration % log_interval == 0:
                now = time.time()
                elapsed_since_log = now - t_last_log
                sps = (log_interval * steps_per_iter) / elapsed_since_log
                t_last_log = now

                losses = _C.log_losses(pufferl)
                env_stats = _C.log_environments(pufferl)

                # NaN guard -- surfaces data integrity bugs immediately
                for loss_name in ("entropy", "pg_loss", "vf_loss"):
                    v = losses.get(loss_name)
                    if v is None or not math.isfinite(float(v)):
                        raise RuntimeError(
                            f"invalid loss metric {loss_name}={v} "
                            f"at step={global_step}")

                score = env_stats.get(score_key, env_stats.get("episode_return", 0))
                ep_ret = env_stats.get("episode_return", 0)
                ep_len = env_stats.get("episode_length", 0)

                entry = {
                    "step": global_step,
                    "sps": sps,
                    "score": score,
                    "episode_return": ep_ret,
                    "episode_length": ep_len,
                    "entropy": losses.get("entropy", 0),
                    "pg_loss": losses.get("pg_loss", 0),
                    "vf_loss": losses.get("vf_loss", 0),
                }
                result.entries.append(entry)

                if on_log:
                    on_log(iteration, global_step, sps, losses, env_stats)

                elapsed = now - start_time
                if should_stop and should_stop(score, elapsed):
                    break

        result.elapsed = time.time() - start_time
        result.steps = global_step
        result.sps = global_step / result.elapsed if result.elapsed > 0 else 0

        if result.entries:
            result.score = result.entries[-1]["score"]
            result.episode_return = result.entries[-1]["episode_return"]

        result.profile = _C.log_profile(pufferl)

    finally:
        if pufferl is not None and not keep_alive:
            _C.close(pufferl)
            result.pufferl = None

    return result


# ============================================================================
# sweep functions
# ============================================================================

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


def _build_sweep_config(config: dict) -> dict:
    """Extract Protein sweep config from loaded config dict."""
    sweep = config.get("sweep", {})
    sweep_config = {
        "method": sweep.get("method", "Protein"),
        "metric": config.get("base", {}).get("score_metric", "score"),
        "metric_distribution": sweep.get("metric_distribution", "linear"),
        "goal": sweep.get("goal", "maximize"),
        "downsample": int(sweep.get("downsample", 5)),
        "use_gpu": False,
        "prune_pareto": sweep.get("prune_pareto", True),
        "early_stop_quantile": float(sweep.get("early_stop_quantile", 0.3)),
        "max_suggestion_cost": int(sweep.get("max_suggestion_cost", 1800)),
    }
    # add sweep param ranges
    for group in ("train", "policy", "env"):
        if group in sweep and isinstance(sweep[group], dict):
            sweep_config[group] = sweep[group]
    return sweep_config


def _build_default_params(config: dict) -> dict:
    """Extract default params (train + policy) from loaded config dict.

    Protein's sweep config nests vec params (total_agents, num_buffers) under
    train/, so we merge vec into train here to match the sweep key paths."""
    train = dict(config.get("train", {}))
    # vec params are sweepable under train/ in the sweep config
    for k, v in config.get("vec", {}).items():
        if k not in train:
            train[k] = v
    return {
        "train": train,
        "policy": dict(config.get("policy", {})),
    }


def run_trial(
    trial_idx: int,
    env_name: str,
    params: dict,
    protein: Protein,
    sweep_dir: Path,
    config: dict,
) -> dict | None:
    """Run a single training trial using the shared training loop."""
    from pufferlib.ocean.osrs_pvp.pfsp import (
        OPP_PFSP, POOL_TYPES, init_pfsp, update_pfsp,
    )

    flat = dict(pufferlib.unroll_nested_dict(params))
    total_steps = int(flat.get("train/total_timesteps", 0))

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

    # merge trial params into full config for build_configs
    trial_config = {
        "train": params.get("train", {}),
        "policy": params.get("policy", {}),
        "vec": {
            "total_agents": params.get("train", {}).get("total_agents",
                config.get("vec", {}).get("total_agents", 2048)),
            "num_buffers": params.get("train", {}).get("num_buffers",
                config.get("vec", {}).get("num_buffers", 1)),
        },
        "env": config.get("env", {}),
    }
    c, vec_config, env_config, policy_config = build_configs(env_name, trial_config)

    total_agents = int(vec_config["total_agents"])

    # PFSP setup for osrs_pvp
    pfsp_state = None
    if env_name == "osrs_pvp" and env_config.get("opponent_type", 0) == float(OPP_PFSP):
        pfsp_state = {"total_agents": total_agents}

    last_report_time = time.time()
    log_count = 0
    score_key = config.get("base", {}).get("score_metric", "score")
    min_sps = int(config.get("sweep", {}).get("min_sps", 100_000))
    downsample_points = int(config.get("sweep", {}).get("downsample", 5))

    def on_log(iteration, global_step, sps, losses, env_stats):
        nonlocal last_report_time, log_count
        log_count += 1
        score = env_stats.get(score_key, env_stats.get("episode_return", 0))
        now = time.time()
        if now - last_report_time > 30:
            print(
                f"  [{global_step/1e6:.1f}M / {total_steps/1e6:.0f}M]  "
                f"score={score:.2f}  sps={sps:.0f}  "
                f"elapsed={now - start_time:.0f}s"
            )
            last_report_time = now

    def should_stop(score, elapsed):
        if log_count <= 2 and result_ref[0] and result_ref[0].entries:
            recent_sps = result_ref[0].entries[-1]["sps"]
            if recent_sps < min_sps:
                print(f"  ABORT: SPS={recent_sps:.0f} < {min_sps}")
                return True
        return protein.should_stop(score, elapsed)

    pfsp_initialized = [False]

    def on_iteration(pufferl, global_step):
        if pfsp_state is not None:
            if not pfsp_initialized[0]:
                init_pfsp(pufferl, total_agents)
                pfsp_state["cum_episodes"] = [0.0] * len(POOL_TYPES)
                pfsp_state["last_update_step"] = 0
                pfsp_initialized[0] = True
                print(f"  PFSP: {len(POOL_TYPES)} opponents, uniform initial weights")
            update_pfsp(pufferl, pfsp_state, global_step)

    result_ref = [None]
    start_time = time.time()

    try:
        result = run_training(
            c, vec_config, env_config, policy_config,
            log_interval=LOG_INTERVAL,
            score_key=score_key,
            on_log=on_log,
            should_stop=should_stop,
            on_iteration=on_iteration,
        )
        result_ref[0] = result
    except Exception as e:
        print(f"  FAILED: {e}")
        return None

    entries = result.entries
    elapsed = result.elapsed

    if not entries:
        print(f"  FAILED: no metric entries ({elapsed:.0f}s)")
        return None

    scores = [e["score"] for e in entries]
    steps = [e["step"] for e in entries]

    downsampled_scores = downsample(scores, downsample_points)
    downsampled_steps = downsample(steps, downsample_points)

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
    early_stopped = result.steps < int(params.get("train", {}).get("total_timesteps", 0))
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


def run_sweep(env_name: str, config: dict, max_trials: int | None, timeout_h: float) -> None:
    """Run the Protein sweep for a Metal env."""
    sweep_dir = SWEEP_DIR_BASE / env_name
    sweep_dir.mkdir(parents=True, exist_ok=True)
    obs_path = sweep_dir / "observations.jsonl"

    sweep_config = _build_sweep_config(config)
    default_params = _build_default_params(config)

    protein = Protein(sweep_config, use_gpu=False, prune_pareto=True)

    existing_records = load_observations(obs_path)
    existing_trials: set[int] = set()
    if existing_records:
        for r in existing_records:
            existing_trials.add(r["trial"])
            if "train" in r["params"]:
                r["params"]["train"].setdefault("ns_iters", 5)
            score = r.get("score", r.get("episode_return", 0))
            protein.observe(r["params"], score, r["cost"])
        print(f"replayed {len(existing_records)} observations from {len(existing_trials)} previous trials")

    trial_idx = max(existing_trials) + 1 if existing_trials else 0
    sweep_start = time.time()
    timeout_s = timeout_h * 3600

    n_params = len(dict(pufferlib.unroll_nested_dict(
        {k: v for k, v in sweep_config.items() if isinstance(v, dict)}
    )))

    score_key = config.get("base", {}).get("score_metric", "score")
    metric_dist = sweep_config.get("metric_distribution", "linear")
    print(f"protein sweep ({env_name}, metal, in-process)")
    print(f"  metric: {score_key} ({metric_dist} distribution)")
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
            params = deepcopy(default_params)
            print("\ntrial 0: using default hyperparameters as anchor")
        else:
            fill = deepcopy(default_params)
            params, info = protein.suggest(fill)
            if info:
                pred_cost = info.get("cost", 0)
                pred_score = info.get("score", 0)
                print(f"\nprotein prediction: score={pred_score:.3f}, cost={pred_cost:.0f}s")

        result = run_trial(trial_idx, env_name, params, protein, sweep_dir, config)

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


# ============================================================================
# CLI: train mode
# ============================================================================

def train_cli(env_name: str):
    config = load_config(env_name)
    config = apply_cli_overrides(config, env_name)
    cli = config.pop("_cli", {})

    c, vec_config, env_config, policy_config = build_configs(env_name, config)

    total_agents = int(vec_config["total_agents"])
    hidden_size = int(policy_config["hidden_size"])
    num_layers = int(policy_config["num_layers"])
    horizon = int(c["horizon"])
    overlap = bool(int(c["overlap"]))
    cpu_infer = bool(int(c["cpu_inference"]))
    fp16 = bool(int(c["train_fp16"]))
    seed = int(c["seed"])

    print(f"env={env_name}, agents={total_agents}, hidden={hidden_size}, "
          f"layers={num_layers}, horizon={horizon}, overlap={overlap}, "
          f"cpu_infer={cpu_infer}, fp16={fp16}, seed={seed}")

    # wandb init
    wandb_run = None
    if cli.get("wandb"):
        import wandb
        run_id = wandb.util.generate_id()
        wandb_run = wandb.init(
            id=run_id, config={**c, **vec_config, **env_config, **policy_config},
            project=cli.get("wandb_project", "pufferlib-metal"),
            group=cli.get("wandb_group", "debug"),
            tags=[cli["tag"]] if cli.get("tag") else [env_name],
            settings=wandb.Settings(console="off"),
        )

    # optional trace output
    trace_file = None
    trace_every = max(int(cli.get("trace_every", 1)), 1)
    trace_path = cli.get("trace_path", "")
    if trace_path:
        tp = Path(trace_path).expanduser().resolve()
        tp.parent.mkdir(parents=True, exist_ok=True)
        trace_file = tp.open("w", encoding="utf-8")
        trace_file.write(json.dumps({
            "event": "meta", "env": env_name, "seed": seed,
            "total_agents": total_agents, "hidden_size": hidden_size,
            "num_layers": num_layers, "horizon": horizon,
            "total_timesteps": int(c["total_timesteps"]),
            "learning_rate": c["learning_rate"],
            "optimizer": "muon", "trace_every": trace_every,
        }) + "\n")
        trace_file.flush()

    def on_log(iteration, global_step, sps, losses, env_stats):
        ent = losses.get("entropy", 0)
        pg = losses.get("pg_loss", 0)
        vf = losses.get("vf_loss", 0)
        ep_ret = env_stats.get("episode_return", 0)
        score = env_stats.get("score", ep_ret)
        ep_len = env_stats.get("episode_length", 0)
        wave = env_stats.get("wave", 0)
        prayer = env_stats.get("prayer_correct_rate", 0)
        idle = env_stats.get("idle_ticks", 0)
        print(f"[step={global_step:>10,} | SPS={sps:>10,.0f} | "
              f"ret={ep_ret:>8.2f} score={score:>8.2f} len={ep_len:>6.0f} "
              f"wave={wave:>4.1f} pray={prayer:.0%} idle={idle:>4.0f} | "
              f"ent={ent:.3f} pg={pg:.4f} vf={vf:.4f}]")

        if wandb_run:
            wandb_run.log({
                "sps": sps,
                "score": score,
                "episode_return": ep_ret,
                "episode_length": ep_len,
                "entropy": ent,
                "pg_loss": pg,
                "vf_loss": vf,
                **{k: v for k, v in env_stats.items()
                   if k not in ("episode_return", "episode_length", "score")},
            }, step=global_step)

        if trace_file and iteration % trace_every == 0:
            trace_row = {
                "event": "tick", "iteration": iteration, "step": global_step,
                "sps": sps, "score": score, "episode_return": ep_ret,
                "episode_length": ep_len, "entropy": ent,
                "pg_loss": pg, "vf_loss": vf,
            }
            trace_file.write(json.dumps(trace_row) + "\n")

    log_interval = int(cli.get("log_interval", 10))
    checkpoint_interval = int(cli.get("checkpoint_interval", 200))
    checkpoint_dir = cli.get("checkpoint_dir", "")
    if not checkpoint_dir:
        run_id = str(int(1000 * time.time()))
        checkpoint_dir = os.path.join("checkpoints", env_name, run_id)

    print(f"model params: {int(c.get('total_timesteps', 0)):,} steps target")
    print(f"checkpoints: {checkpoint_dir} (every {checkpoint_interval} iters)")

    result = run_training(
        c, vec_config, env_config, policy_config,
        log_interval=log_interval,
        on_log=on_log,
        checkpoint_dir=checkpoint_dir,
        checkpoint_interval=checkpoint_interval,
    )

    print(f"\ndone. {result.steps:,} steps in {result.elapsed:.1f}s")
    print(f"avg SPS: {result.sps:,.0f}")
    for k, v in sorted(result.profile.items()):
        print(f"  {k}: {v:.3f}")

    if trace_file:
        trace_file.write(json.dumps({
            "event": "final", "step": result.steps,
            "total_time_seconds": result.elapsed, "avg_sps": result.sps,
            "profile": result.profile,
        }) + "\n")
        trace_file.close()

    if wandb_run:
        import wandb
        # upload final checkpoint as artifact
        final_ckpt = os.path.join(checkpoint_dir, "latest.bin")
        if os.path.exists(final_ckpt):
            artifact = wandb.Artifact(f"{env_name}-model", type="model")
            artifact.add_file(final_ckpt)
            wandb_run.log_artifact(artifact)
        wandb_run.finish()


# ============================================================================
# CLI: sweep mode
# ============================================================================

def sweep_cli(env_name: str):
    config = load_config(env_name)
    config = apply_cli_overrides(config, env_name)
    cli = config.pop("_cli", {})

    if cli.get("results"):
        print_results(SWEEP_DIR_BASE / env_name / "observations.jsonl")
        return

    run_sweep(
        env_name, config,
        max_trials=cli.get("max_trials"),
        timeout_h=cli.get("timeout", 4.0),
    )


# ============================================================================
# CLI: eval mode (load checkpoint, render env 0)
# ============================================================================

def eval_cli(env_name: str):
    """Load a trained checkpoint and render the agent. Follows upstream pufferl.eval."""
    config = load_config(env_name)
    config = apply_cli_overrides(config, env_name)
    cli = config.pop("_cli", {})

    c, vec_config, env_config, policy_config = build_configs(env_name, config)

    pufferl = _C.create_pufferl(c, vec_config, env_config, policy_config)

    cli = config.pop("_cli", {})

    # resolve load path: explicit path, "latest", or search
    load_path = cli.get("load_model_path", "latest")
    if load_path == "latest":
        pattern = os.path.join("checkpoints", env_name, "**", "*.bin")
        candidates = glob.glob(pattern, recursive=True)
        if not candidates:
            print(f"no checkpoints found in checkpoints/{env_name}/")
            _C.close(pufferl)
            return
        load_path = max(candidates, key=os.path.getctime)

    _C.load_weights(pufferl, load_path)
    print(f"loaded weights from {load_path}")
    print(f"rendering env 0. ctrl+c to stop.")

    try:
        while True:
            _C.render(pufferl, 0)
            _C.rollouts(pufferl)
    except KeyboardInterrupt:
        print("\nstopped.")
    finally:
        _C.close(pufferl)


# ============================================================================
# CLI dispatcher
# ============================================================================

def main():
    if len(sys.argv) < 3:
        print("usage: python pufferl.py [train|sweep|eval|results] <env> [args]")
        sys.exit(1)

    mode = sys.argv.pop(1)
    env_name = sys.argv.pop(1)

    if mode == "train":
        train_cli(env_name)
    elif mode == "sweep":
        sweep_cli(env_name)
    elif mode == "eval":
        eval_cli(env_name)
    elif mode == "results":
        print_results(SWEEP_DIR_BASE / env_name / "observations.jsonl")
    else:
        print(f"unknown mode: {mode}. use train, sweep, eval, or results.")
        sys.exit(1)


if __name__ == "__main__":
    main()
