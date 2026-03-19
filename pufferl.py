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
import json
import math
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
# training engine
# ============================================================================

# env configs (kwargs passed to my_init via Dict)
ENV_DEFAULTS = {
    "breakout": {
        "frameskip": 4.0,
        "width": 576.0,
        "height": 330.0,
        "paddle_width": 62.0,
        "paddle_height": 8.0,
        "ball_width": 32.0,
        "ball_height": 32.0,
        "brick_width": 32.0,
        "brick_height": 12.0,
        "brick_rows": 6.0,
        "brick_cols": 18.0,
        "initial_ball_speed": 256.0,
        "max_ball_speed": 448.0,
        "paddle_speed": 620.0,
        "continuous": 0.0,
    },
    "g2048": {
        "scaffolding_ratio": 0.0,
    },
    "osrs_pvp": {
        "opponent_type": 16.0,  # OPP_PFSP
        "shaping_scale": 0.0,
        "shaping_enabled": 0.0,
        "mask_in_obs": 1.0,
    },
    "osrs_zulrah": {
        "gear_tier": 0.0,
        "mask_in_obs": 1.0,
    },
    "osrs_inferno": {
        "start_wave": 0.0,
        "mask_in_obs": 1.0,
    },
}


def build_configs(env_name, params):
    """Convert a flat or nested params dict to the 4 config dicts for _C.create_pufferl.

    Accepts either:
      - nested Protein format: {"train": {...}, "policy": {...}}
      - flat CLI format: {"horizon": 32, "learning_rate": 0.001, ...}
        (wrapped into {"train": flat, "policy": flat} internally)
    """
    # normalize: if there's no "train" key, treat the whole dict as flat
    if "train" not in params:
        params = {"train": params, "policy": params}

    train = params.get("train", {})
    policy = params.get("policy", {})

    horizon = int(train.get("horizon", 64))
    total_agents = int(train.get("total_agents", 4096))
    num_buffers = int(train.get("num_buffers", 1))
    minibatch_size = int(train.get("minibatch_size", 8192))

    # structural: minibatch can't exceed batch size
    batch_size = total_agents * horizon
    if minibatch_size > batch_size:
        minibatch_size = batch_size

    config = {
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
        "use_rnn": 1.0,
        "cudagraphs": -1.0,
        "kernels": 1.0,
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
    env_config = ENV_DEFAULTS[env_name].copy()

    if "scaffolding_ratio" in train:
        env_config["scaffolding_ratio"] = train["scaffolding_ratio"]

    return config, vec_config, env_config, policy_config


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
                 keep_alive=False):
    """Run the Metal training loop. Single entry point for bench.py and sweep_bench.py.

    on_log(iteration, global_step, sps, losses, env_stats):
        called every log_interval iterations. return value ignored.
    should_stop(score, elapsed):
        called every log_interval iterations. return True to early-stop.
    on_iteration(pufferl, global_step):
        called every iteration (for PFSP weight updates etc.)
    keep_alive:
        if True, don't close pufferl — caller gets it via result.pufferl.
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

        for iteration in range(1, total_iters + 1):
            _C.rollouts(pufferl)
            _C.train(pufferl)
            global_step += steps_per_iter

            if on_iteration:
                on_iteration(pufferl, global_step)

            if iteration % log_interval == 0:
                now = time.time()
                elapsed_since_log = now - t_last_log
                sps = (log_interval * steps_per_iter) / elapsed_since_log
                t_last_log = now

                losses = _C.log_losses(pufferl)
                env_stats = _C.log_environments(pufferl)

                # NaN guard — surfaces data integrity bugs immediately
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
# sweep configuration
# ============================================================================

SWEEP_DIR_BASE = Path("runs/sweep_bench")
DOWNSAMPLE_POINTS = 5
LOG_INTERVAL = 5  # log every N iterations for early stop checks
MIN_SPS_PER_ENV = {
    "breakout": 300_000,
    "g2048": 100_000,  # larger models (hs=256, L=5) can be slow
    "osrs_pvp": 50_000,  # 373 obs, 7 heads — much heavier than breakout
    "osrs_zulrah": 50_000,  # 105 obs, 6 heads
    "osrs_inferno": 50_000,  # 200 obs, 6 heads, long episodes
}

# per-env score metric: which env_stats key to use as the optimization target.
# defaults to "score" (which falls back to "episode_return") for most envs.
SCORE_METRIC_PER_ENV = {
    "osrs_pvp": "episode_return",  # denser signal than wins (which averages across PFSP pool)
    "osrs_zulrah": "score",
    "osrs_inferno": "episode_return",  # wave completion shaping + terminal ±1
}

# per-env metric distribution for Protein (how scores are transformed).
# "linear" for unbounded scores, "percentile" for [0,1] rates.
METRIC_DIST_PER_ENV = {
    "osrs_pvp": "linear",
    "osrs_inferno": "linear",
    "osrs_zulrah": "linear",
}

# PFSP (prioritized fictitious self-play) config for osrs_pvp.
# opponent name -> enum value (from osrs_pvp_types.h OpponentType)
OPP_PFSP = 16  # special opponent type that samples from the pool
PFSP_POOL = {
    "true_random": 1, "panicking": 2, "weak_random": 3, "semi_random": 4,
    "sticky_prayer": 5, "random_eater": 6, "prayer_rookie": 7, "improved": 8,
    "onetick": 11, "unpredictable_improved": 12, "unpredictable_onetick": 13,
    "novice_nh": 17, "apprentice_nh": 18, "competent_nh": 19,
    "intermediate_nh": 20, "advanced_nh": 21, "proficient_nh": 22,
    "expert_nh": 23, "master_nh": 24, "savant_nh": 25,
    "nightmare_nh": 26, "veng_fighter": 27, "blood_healer": 28,
    "gmaul_combo": 29,
}
PFSP_POOL_NAMES = list(PFSP_POOL.keys())
PFSP_POOL_TYPES = list(PFSP_POOL.values())
PFSP_P = 1.5  # weight exponent: (1-winrate)^p
PFSP_WEIGHT_FLOOR = 0.02
PFSP_UPDATE_INTERVAL = 2_000_000  # steps between weight recomputation
PFSP_WARMUP_EPISODES = 50  # min episodes per opponent before reweighting



# ============================================================================
# per-env sweep configs and defaults
# ============================================================================

# shared sweep config skeleton (overridden per env)
_SWEEP_BASE = {
    "method": "Protein",
    "metric": "score",
    "metric_distribution": "linear",
    "goal": "maximize",
    "downsample": DOWNSAMPLE_POINTS,
    "use_gpu": False,
    "prune_pareto": True,
    "early_stop_quantile": 0.3,
}

SWEEP_CONFIGS = {
    "breakout": {
        **_SWEEP_BASE,
        "max_suggestion_cost": 1800,
        "train": {
            "total_timesteps": {"distribution": "log_normal", "min": 60_000_000, "max": 200_000_000, "scale": "time"},
            "horizon": {"distribution": "uniform_pow2", "min": 16, "max": 64, "scale": "auto"},
            "min_lr_ratio": {"distribution": "uniform", "min": 0.0, "max": 0.25, "scale": "auto"},
            "learning_rate": {"distribution": "log_normal", "min": 0.01, "max": 0.3, "scale": 0.5},
            "beta1": {"distribution": "uniform", "min": 0.5, "max": 0.95, "scale": "auto"},
            "beta2": {"distribution": "logit_normal", "min": 0.95, "max": 0.99999, "scale": "auto"},
            "eps": {"distribution": "log_normal", "min": 1e-6, "max": 1e-3, "scale": "auto"},
            "ent_coef": {"distribution": "log_normal", "min": 0.0005, "max": 0.02, "scale": "auto"},
            "gamma": {"distribution": "logit_normal", "min": 0.88, "max": 0.998, "scale": "auto"},
            "gae_lambda": {"distribution": "logit_normal", "min": 0.8, "max": 0.995, "scale": "auto"},
            "vtrace_rho_clip": {"distribution": "uniform", "min": 1.0, "max": 4.0, "scale": "auto"},
            "vtrace_c_clip": {"distribution": "uniform", "min": 1.0, "max": 3.0, "scale": "auto"},
            "prio_alpha": {"distribution": "logit_normal", "min": 0.01, "max": 0.95, "scale": "auto"},
            "prio_beta0": {"distribution": "logit_normal", "min": 0.5, "max": 0.99, "scale": "auto"},
            "clip_coef": {"distribution": "uniform", "min": 0.1, "max": 1.5, "scale": "auto"},
            "vf_coef": {"distribution": "uniform", "min": 0.5, "max": 8.0, "scale": "auto"},
            "vf_clip_coef": {"distribution": "uniform", "min": 0.3, "max": 6.0, "scale": "auto"},
            "max_grad_norm": {"distribution": "uniform", "min": 0.5, "max": 4.0, "scale": "auto"},
            "replay_ratio": {"distribution": "uniform", "min": 0.5, "max": 4.0, "scale": "auto"},
            "minibatch_size": {"distribution": "uniform_pow2", "min": 16384, "max": 131072, "scale": "auto"},
            "total_agents": {"distribution": "uniform_pow2", "min": 2048, "max": 8192, "scale": "auto"},
            "num_buffers": {"distribution": "uniform_pow2", "min": 2, "max": 8, "scale": "auto"},
            "ns_iters": {"distribution": "uniform", "min": 3, "max": 5.99, "scale": "auto"},
        },
        "policy": {
            "num_layers": {"distribution": "uniform", "min": 2, "max": 3.5, "scale": "auto"},
        },
    },
    "g2048": {
        **_SWEEP_BASE,
        "max_suggestion_cost": 7200,  # g2048 needs longer runs
        "train": {
            "total_timesteps": {"distribution": "log_normal", "min": 300_000_000, "max": 1_000_000_000, "scale": "time"},
            "horizon": {"distribution": "uniform_pow2", "min": 16, "max": 64, "scale": "auto"},
            "min_lr_ratio": {"distribution": "uniform", "min": 0.0, "max": 0.25, "scale": "auto"},
            "learning_rate": {"distribution": "log_normal", "min": 0.0005, "max": 0.02, "scale": 0.5},
            "beta1": {"distribution": "uniform", "min": 0.5, "max": 0.95, "scale": "auto"},
            "beta2": {"distribution": "logit_normal", "min": 0.95, "max": 0.99999, "scale": "auto"},
            "eps": {"distribution": "log_normal", "min": 1e-6, "max": 1e-3, "scale": "auto"},
            "ent_coef": {"distribution": "log_normal", "min": 0.0005, "max": 0.05, "scale": "auto"},
            "gamma": {"distribution": "logit_normal", "min": 0.97, "max": 0.9999, "scale": "auto"},
            "gae_lambda": {"distribution": "logit_normal", "min": 0.5, "max": 0.995, "scale": "auto"},
            "vtrace_rho_clip": {"distribution": "uniform", "min": 1.0, "max": 4.0, "scale": "auto"},
            "vtrace_c_clip": {"distribution": "uniform", "min": 1.0, "max": 3.0, "scale": "auto"},
            "prio_alpha": {"distribution": "logit_normal", "min": 0.01, "max": 0.95, "scale": "auto"},
            "prio_beta0": {"distribution": "logit_normal", "min": 0.5, "max": 0.99, "scale": "auto"},
            "clip_coef": {"distribution": "uniform", "min": 0.05, "max": 0.5, "scale": "auto"},
            "vf_coef": {"distribution": "uniform", "min": 0.1, "max": 8.0, "scale": "auto"},
            "vf_clip_coef": {"distribution": "uniform", "min": 0.05, "max": 6.0, "scale": "auto"},
            "max_grad_norm": {"distribution": "uniform", "min": 0.2, "max": 6.0, "scale": "auto"},
            "replay_ratio": {"distribution": "uniform", "min": 0.25, "max": 4.0, "scale": "auto"},
            "minibatch_size": {"distribution": "uniform_pow2", "min": 4096, "max": 65536, "scale": "auto"},
            "total_agents": {"distribution": "uniform_pow2", "min": 2048, "max": 16384, "scale": "auto"},
            "num_buffers": {"distribution": "uniform_pow2", "min": 2, "max": 8, "scale": "auto"},
            "ns_iters": {"distribution": "uniform", "min": 3, "max": 5.99, "scale": "auto"},
            "scaffolding_ratio": {"distribution": "uniform", "min": 0.0, "max": 0.9, "scale": "auto"},
        },
        "policy": {
            "hidden_size": {"distribution": "uniform_pow2", "min": 64, "max": 256, "scale": "auto"},
            "num_layers": {"distribution": "uniform", "min": 2, "max": 5.5, "scale": "auto"},
        },
    },
    "osrs_pvp": {
        **_SWEEP_BASE,
        "metric": "episode_return",
        "metric_distribution": "linear",
        "max_suggestion_cost": 3600,  # PFSP needs longer trials, 100M+ steps
        "train": {
            # ranges centered on storm proven config with generous room
            "total_timesteps": {"distribution": "log_normal", "min": 50_000_000, "max": 200_000_000, "scale": "time"},
            "horizon": {"distribution": "uniform_pow2", "min": 16, "max": 256, "scale": "auto"},
            "learning_rate": {"distribution": "log_normal", "min": 0.0003, "max": 0.03, "scale": 0.5},
            "ent_coef": {"distribution": "log_normal", "min": 0.00005, "max": 0.01, "scale": "auto"},
            "gamma": {"distribution": "logit_normal", "min": 0.98, "max": 0.9999, "scale": "auto"},
            "min_lr_ratio": {"distribution": "uniform", "min": 0.02, "max": 0.3, "scale": "auto"},
            "beta2": {"distribution": "logit_normal", "min": 0.99, "max": 0.9999, "scale": "auto"},
            "gae_lambda": {"distribution": "logit_normal", "min": 0.1, "max": 0.99, "scale": "auto"},
            "beta1": {"distribution": "uniform", "min": 0.8, "max": 0.99, "scale": "auto"},
            "eps": {"distribution": "log_normal", "min": 1e-6, "max": 1e-3, "scale": "auto"},
            "vtrace_c_clip": {"distribution": "uniform", "min": 1.0, "max": 3.0, "scale": "auto"},
            "prio_alpha": {"distribution": "logit_normal", "min": 0.5, "max": 0.99, "scale": "auto"},
            "prio_beta0": {"distribution": "logit_normal", "min": 0.05, "max": 0.8, "scale": "auto"},
            "clip_coef": {"distribution": "uniform", "min": 0.1, "max": 0.6, "scale": "auto"},
            "vf_coef": {"distribution": "log_normal", "min": 0.5, "max": 10.0, "scale": "auto"},
            "vf_clip_coef": {"distribution": "uniform", "min": 0.1, "max": 2.0, "scale": "auto"},
            "max_grad_norm": {"distribution": "uniform", "min": 0.5, "max": 5.0, "scale": "auto"},
            "replay_ratio": {"distribution": "uniform", "min": 0.1, "max": 2.0, "scale": "auto"},
            "minibatch_size": {"distribution": "uniform_pow2", "min": 2048, "max": 8192, "scale": "auto"},
            "num_buffers": {"distribution": "uniform_pow2", "min": 1, "max": 4, "scale": "auto"},
        },
        "policy": {
            "hidden_size": {"distribution": "uniform_pow2", "min": 256, "max": 1024, "scale": "auto"},
            "num_layers": {"distribution": "uniform", "min": 2.0, "max": 6.0, "scale": "auto"},
        },
    },
    "osrs_zulrah": {
        **_SWEEP_BASE,
        "metric": "score",
        "metric_distribution": "linear",
        "max_suggestion_cost": 1800,
        "train": {
            # wide ranges — we have no idea what works for zulrah with reward shaping
            "total_timesteps": {"distribution": "log_normal", "min": 20_000_000, "max": 200_000_000, "scale": "time"},
            "horizon": {"distribution": "uniform_pow2", "min": 16, "max": 256, "scale": "auto"},
            "learning_rate": {"distribution": "log_normal", "min": 0.0005, "max": 0.05, "scale": 0.5},
            "ent_coef": {"distribution": "log_normal", "min": 0.0001, "max": 0.01, "scale": "auto"},
            "gamma": {"distribution": "logit_normal", "min": 0.98, "max": 0.9999, "scale": "auto"},
            "min_lr_ratio": {"distribution": "uniform", "min": 0.0, "max": 0.5, "scale": "auto"},
            "beta1": {"distribution": "uniform", "min": 0.7, "max": 0.99, "scale": "auto"},
            "beta2": {"distribution": "logit_normal", "min": 0.99, "max": 0.9999, "scale": "auto"},
            "eps": {"distribution": "log_normal", "min": 1e-6, "max": 1e-3, "scale": "auto"},
            "gae_lambda": {"distribution": "logit_normal", "min": 0.1, "max": 0.999, "scale": "auto"},
            "vtrace_rho_clip": {"distribution": "uniform", "min": 1.0, "max": 4.0, "scale": "auto"},
            "vtrace_c_clip": {"distribution": "uniform", "min": 1.0, "max": 3.0, "scale": "auto"},
            "prio_alpha": {"distribution": "logit_normal", "min": 0.0, "max": 0.99, "scale": "auto"},
            "prio_beta0": {"distribution": "logit_normal", "min": 0.01, "max": 0.95, "scale": "auto"},
            "clip_coef": {"distribution": "uniform", "min": 0.05, "max": 0.6, "scale": "auto"},
            "vf_coef": {"distribution": "log_normal", "min": 0.1, "max": 10.0, "scale": "auto"},
            "vf_clip_coef": {"distribution": "uniform", "min": 0.05, "max": 4.0, "scale": "auto"},
            "max_grad_norm": {"distribution": "uniform", "min": 0.3, "max": 8.0, "scale": "auto"},
            "replay_ratio": {"distribution": "uniform", "min": 0.1, "max": 4.0, "scale": "auto"},
            "minibatch_size": {"distribution": "uniform_pow2", "min": 2048, "max": 16384, "scale": "auto"},
            "num_buffers": {"distribution": "uniform_pow2", "min": 1, "max": 4, "scale": "auto"},
        },
        "policy": {
            "hidden_size": {"distribution": "uniform_pow2", "min": 128, "max": 1024, "scale": "auto"},
            "num_layers": {"distribution": "uniform", "min": 1, "max": 6.0, "scale": "auto"},
        },
    },
    "osrs_inferno": {
        **_SWEEP_BASE,
        "metric": "episode_return",
        "metric_distribution": "linear",
        "max_suggestion_cost": 1800,
        "train": {
            # wide ranges — inferno is very different from pvp/zulrah (long episodes,
            # large action space, needs high entropy to explore, long horizon for credit
            # assignment through multi-tick combat sequences)
            "total_timesteps": {"distribution": "log_normal", "min": 50_000_000, "max": 500_000_000, "scale": "time"},
            "horizon": {"distribution": "uniform_pow2", "min": 16, "max": 256, "scale": "auto"},
            "learning_rate": {"distribution": "log_normal", "min": 0.0003, "max": 0.02, "scale": 0.5},
            "ent_coef": {"distribution": "log_normal", "min": 0.001, "max": 0.05, "scale": "auto"},
            "gamma": {"distribution": "logit_normal", "min": 0.99, "max": 0.9999, "scale": "auto"},
            "min_lr_ratio": {"distribution": "uniform", "min": 0.0, "max": 0.3, "scale": "auto"},
            "beta1": {"distribution": "uniform", "min": 0.7, "max": 0.99, "scale": "auto"},
            "beta2": {"distribution": "logit_normal", "min": 0.99, "max": 0.9999, "scale": "auto"},
            "eps": {"distribution": "log_normal", "min": 1e-6, "max": 1e-3, "scale": "auto"},
            "gae_lambda": {"distribution": "logit_normal", "min": 0.5, "max": 0.999, "scale": "auto"},
            "vtrace_rho_clip": {"distribution": "uniform", "min": 1.0, "max": 4.0, "scale": "auto"},
            "vtrace_c_clip": {"distribution": "uniform", "min": 1.0, "max": 3.0, "scale": "auto"},
            "prio_alpha": {"distribution": "logit_normal", "min": 0.0, "max": 0.99, "scale": "auto"},
            "prio_beta0": {"distribution": "logit_normal", "min": 0.01, "max": 0.95, "scale": "auto"},
            "clip_coef": {"distribution": "uniform", "min": 0.05, "max": 0.5, "scale": "auto"},
            "vf_coef": {"distribution": "log_normal", "min": 0.1, "max": 5.0, "scale": "auto"},
            "vf_clip_coef": {"distribution": "uniform", "min": 0.05, "max": 4.0, "scale": "auto"},
            "max_grad_norm": {"distribution": "uniform", "min": 0.3, "max": 5.0, "scale": "auto"},
            "replay_ratio": {"distribution": "uniform", "min": 0.1, "max": 3.0, "scale": "auto"},
            "minibatch_size": {"distribution": "uniform_pow2", "min": 2048, "max": 16384, "scale": "auto"},
            "num_buffers": {"distribution": "uniform_pow2", "min": 1, "max": 4, "scale": "auto"},
        },
        "policy": {
            "hidden_size": {"distribution": "uniform_pow2", "min": 128, "max": 512, "scale": "auto"},
            "num_layers": {"distribution": "uniform", "min": 1, "max": 5.0, "scale": "auto"},
        },
    },
}

DEFAULT_PARAMS_PER_ENV = {
    # best known Metal breakout config (trial #203, score 854, tensor_ops NT)
    "breakout": {
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
            "ns_iters": 5,
        },
        "policy": {
            "hidden_size": 64,
            "num_layers": 2,
        },
    },
    # g2048 anchor: upstream-ish config adapted for Metal (hs=128 L=3 as baseline)
    "g2048": {
        "train": {
            "total_timesteps": 200_000_000,
            "horizon": 32,
            "min_lr_ratio": 0.0,
            "learning_rate": 0.003,
            "beta1": 0.9,
            "beta2": 0.999,
            "eps": 1e-5,
            "ent_coef": 0.01,
            "gamma": 0.998,
            "gae_lambda": 0.95,
            "vtrace_rho_clip": 1.0,
            "vtrace_c_clip": 1.0,
            "prio_alpha": 0.01,
            "prio_beta0": 0.4,
            "clip_coef": 0.2,
            "vf_coef": 1.0,
            "vf_clip_coef": 0.1,
            "max_grad_norm": 0.5,
            "replay_ratio": 1.5,
            "minibatch_size": 8192,
            "total_agents": 4096,
            "num_buffers": 4,
            "num_threads": 4,
            "ns_iters": 5,
            "scaffolding_ratio": 0.5,
        },
        "policy": {
            "hidden_size": 128,
            "num_layers": 3,
        },
    },
    # osrs_pvp anchor: from storm PufferLib 3.0+MPS proven config (95.5% winrate)
    "osrs_pvp": {
        "train": {
            "total_timesteps": 100_000_000,
            "horizon": 128,
            "min_lr_ratio": 0.1,
            "learning_rate": 0.00112,
            "beta1": 0.95,
            "beta2": 0.999,
            "eps": 1e-5,
            "ent_coef": 0.0016,
            "gamma": 0.991,
            "gae_lambda": 0.845,
            "vtrace_rho_clip": 1.0,
            "vtrace_c_clip": 1.5,
            "prio_alpha": 0.914,
            "prio_beta0": 0.218,
            "clip_coef": 0.32,
            "vf_coef": 2.5,
            "vf_clip_coef": 0.5,
            "max_grad_norm": 2.0,
            "replay_ratio": 0.25,
            "minibatch_size": 4096,
            "total_agents": 2048,
            "num_buffers": 2,
            "num_threads": 2,
            "ns_iters": 5,
        },
        "policy": {
            "hidden_size": 512,
            "num_layers": 3,
        },
    },
    # osrs_inferno anchor: lr=0.002 confirmed stable at 10M steps (ret 0.76→1.57)
    "osrs_inferno": {
        "train": {
            "total_timesteps": 100_000_000,
            "horizon": 32,
            "min_lr_ratio": 0.1,
            "learning_rate": 0.002,
            "beta1": 0.9,
            "beta2": 0.999,
            "eps": 1e-5,
            "ent_coef": 0.005,
            "gamma": 0.997,
            "gae_lambda": 0.85,
            "vtrace_rho_clip": 1.0,
            "vtrace_c_clip": 1.5,
            "prio_alpha": 0.5,
            "prio_beta0": 0.3,
            "clip_coef": 0.2,
            "vf_coef": 0.5,
            "vf_clip_coef": 0.5,
            "max_grad_norm": 2.0,
            "replay_ratio": 0.25,
            "minibatch_size": 2048,
            "total_agents": 2048,
            "num_buffers": 2,
            "num_threads": 2,
            "ns_iters": 5,
        },
        "policy": {
            "hidden_size": 256,
            "num_layers": 2,
        },
    },
    # osrs_zulrah anchor: sweep trial 601, fastest positive (score=0.88, 702K SPS)
    "osrs_zulrah": {
        "train": {
            "total_timesteps": 26_000_000,
            "horizon": 16,
            "min_lr_ratio": 0.35,
            "learning_rate": 0.015,
            "beta1": 0.962,
            "beta2": 0.999,
            "eps": 6.3e-4,
            "ent_coef": 0.0056,
            "gamma": 0.997,
            "gae_lambda": 0.824,
            "vtrace_rho_clip": 1.25,
            "vtrace_c_clip": 2.69,
            "prio_alpha": 0.93,
            "prio_beta0": 0.01,
            "clip_coef": 0.256,
            "vf_coef": 0.418,
            "vf_clip_coef": 0.1,
            "max_grad_norm": 4.64,
            "replay_ratio": 0.25,
            "minibatch_size": 2048,
            "total_agents": 2048,
            "num_buffers": 4,
            "num_threads": 4,
            "ns_iters": 5,
        },
        "policy": {
            "hidden_size": 512,
            "num_layers": 1,
        },
    },
}




# per-env score metric: which env_stats key to use as the optimization target.
# defaults to "score" (which falls back to "episode_return") for most envs.
SCORE_METRIC_PER_ENV = {
    "osrs_pvp": "episode_return",  # denser signal than wins (which averages across PFSP pool)
    "osrs_zulrah": "score",
    "osrs_inferno": "episode_return",  # wave completion shaping + terminal ±1
}

# per-env metric distribution for Protein (how scores are transformed).
# "linear" for unbounded scores, "percentile" for [0,1] rates.
METRIC_DIST_PER_ENV = {
    "osrs_pvp": "linear",
    "osrs_inferno": "linear",
    "osrs_zulrah": "linear",
}


# PFSP (prioritized fictitious self-play) config for osrs_pvp.
# opponent name -> enum value (from osrs_pvp_types.h OpponentType)
OPP_PFSP = 16  # special opponent type that samples from the pool
PFSP_POOL = {
    "true_random": 1, "panicking": 2, "weak_random": 3, "semi_random": 4,
    "sticky_prayer": 5, "random_eater": 6, "prayer_rookie": 7, "improved": 8,
    "onetick": 11, "unpredictable_improved": 12, "unpredictable_onetick": 13,
    "novice_nh": 17, "apprentice_nh": 18, "competent_nh": 19,
    "intermediate_nh": 20, "advanced_nh": 21, "proficient_nh": 22,
    "expert_nh": 23, "master_nh": 24, "savant_nh": 25,
    "nightmare_nh": 26, "veng_fighter": 27, "blood_healer": 28,
    "gmaul_combo": 29,
}
PFSP_POOL_NAMES = list(PFSP_POOL.keys())
PFSP_POOL_TYPES = list(PFSP_POOL.values())
PFSP_P = 1.5  # weight exponent: (1-winrate)^p
PFSP_WEIGHT_FLOOR = 0.02
PFSP_UPDATE_INTERVAL = 2_000_000  # steps between weight recomputation
PFSP_WARMUP_EPISODES = 50  # min episodes per opponent before reweighting


# shared sweep config skeleton (overridden per env)
_SWEEP_BASE = {
    "method": "Protein",
    "metric": "score",
    "metric_distribution": "linear",
    "goal": "maximize",
    "downsample": DOWNSAMPLE_POINTS,
    "use_gpu": False,
    "prune_pareto": True,
    "early_stop_quantile": 0.3,
}

SWEEP_CONFIGS = {
    "breakout": {
        **_SWEEP_BASE,
        "max_suggestion_cost": 1800,
        "train": {
            "total_timesteps": {"distribution": "log_normal", "min": 60_000_000, "max": 200_000_000, "scale": "time"},
            "horizon": {"distribution": "uniform_pow2", "min": 16, "max": 64, "scale": "auto"},
            "min_lr_ratio": {"distribution": "uniform", "min": 0.0, "max": 0.25, "scale": "auto"},
            "learning_rate": {"distribution": "log_normal", "min": 0.01, "max": 0.3, "scale": 0.5},
            "beta1": {"distribution": "uniform", "min": 0.5, "max": 0.95, "scale": "auto"},
            "beta2": {"distribution": "logit_normal", "min": 0.95, "max": 0.99999, "scale": "auto"},
            "eps": {"distribution": "log_normal", "min": 1e-6, "max": 1e-3, "scale": "auto"},
            "ent_coef": {"distribution": "log_normal", "min": 0.0005, "max": 0.02, "scale": "auto"},
            "gamma": {"distribution": "logit_normal", "min": 0.88, "max": 0.998, "scale": "auto"},
            "gae_lambda": {"distribution": "logit_normal", "min": 0.8, "max": 0.995, "scale": "auto"},
            "vtrace_rho_clip": {"distribution": "uniform", "min": 1.0, "max": 4.0, "scale": "auto"},
            "vtrace_c_clip": {"distribution": "uniform", "min": 1.0, "max": 3.0, "scale": "auto"},
            "prio_alpha": {"distribution": "logit_normal", "min": 0.01, "max": 0.95, "scale": "auto"},
            "prio_beta0": {"distribution": "logit_normal", "min": 0.5, "max": 0.99, "scale": "auto"},
            "clip_coef": {"distribution": "uniform", "min": 0.1, "max": 1.5, "scale": "auto"},
            "vf_coef": {"distribution": "uniform", "min": 0.5, "max": 8.0, "scale": "auto"},
            "vf_clip_coef": {"distribution": "uniform", "min": 0.3, "max": 6.0, "scale": "auto"},
            "max_grad_norm": {"distribution": "uniform", "min": 0.5, "max": 4.0, "scale": "auto"},
            "replay_ratio": {"distribution": "uniform", "min": 0.5, "max": 4.0, "scale": "auto"},
            "minibatch_size": {"distribution": "uniform_pow2", "min": 16384, "max": 131072, "scale": "auto"},
            "total_agents": {"distribution": "uniform_pow2", "min": 2048, "max": 8192, "scale": "auto"},
            "num_buffers": {"distribution": "uniform_pow2", "min": 2, "max": 8, "scale": "auto"},
            "ns_iters": {"distribution": "uniform", "min": 3, "max": 5.99, "scale": "auto"},
        },
        "policy": {
            "num_layers": {"distribution": "uniform", "min": 2, "max": 3.5, "scale": "auto"},
        },
    },
    "g2048": {
        **_SWEEP_BASE,
        "max_suggestion_cost": 7200,  # g2048 needs longer runs
        "train": {
            "total_timesteps": {"distribution": "log_normal", "min": 300_000_000, "max": 1_000_000_000, "scale": "time"},
            "horizon": {"distribution": "uniform_pow2", "min": 16, "max": 64, "scale": "auto"},
            "min_lr_ratio": {"distribution": "uniform", "min": 0.0, "max": 0.25, "scale": "auto"},
            "learning_rate": {"distribution": "log_normal", "min": 0.0005, "max": 0.02, "scale": 0.5},
            "beta1": {"distribution": "uniform", "min": 0.5, "max": 0.95, "scale": "auto"},
            "beta2": {"distribution": "logit_normal", "min": 0.95, "max": 0.99999, "scale": "auto"},
            "eps": {"distribution": "log_normal", "min": 1e-6, "max": 1e-3, "scale": "auto"},
            "ent_coef": {"distribution": "log_normal", "min": 0.0005, "max": 0.05, "scale": "auto"},
            "gamma": {"distribution": "logit_normal", "min": 0.97, "max": 0.9999, "scale": "auto"},
            "gae_lambda": {"distribution": "logit_normal", "min": 0.5, "max": 0.995, "scale": "auto"},
            "vtrace_rho_clip": {"distribution": "uniform", "min": 1.0, "max": 4.0, "scale": "auto"},
            "vtrace_c_clip": {"distribution": "uniform", "min": 1.0, "max": 3.0, "scale": "auto"},
            "prio_alpha": {"distribution": "logit_normal", "min": 0.01, "max": 0.95, "scale": "auto"},
            "prio_beta0": {"distribution": "logit_normal", "min": 0.5, "max": 0.99, "scale": "auto"},
            "clip_coef": {"distribution": "uniform", "min": 0.05, "max": 0.5, "scale": "auto"},
            "vf_coef": {"distribution": "uniform", "min": 0.1, "max": 8.0, "scale": "auto"},
            "vf_clip_coef": {"distribution": "uniform", "min": 0.05, "max": 6.0, "scale": "auto"},
            "max_grad_norm": {"distribution": "uniform", "min": 0.2, "max": 6.0, "scale": "auto"},
            "replay_ratio": {"distribution": "uniform", "min": 0.25, "max": 4.0, "scale": "auto"},
            "minibatch_size": {"distribution": "uniform_pow2", "min": 4096, "max": 65536, "scale": "auto"},
            "total_agents": {"distribution": "uniform_pow2", "min": 2048, "max": 16384, "scale": "auto"},
            "num_buffers": {"distribution": "uniform_pow2", "min": 2, "max": 8, "scale": "auto"},
            "ns_iters": {"distribution": "uniform", "min": 3, "max": 5.99, "scale": "auto"},
            "scaffolding_ratio": {"distribution": "uniform", "min": 0.0, "max": 0.9, "scale": "auto"},
        },
        "policy": {
            "hidden_size": {"distribution": "uniform_pow2", "min": 64, "max": 256, "scale": "auto"},
            "num_layers": {"distribution": "uniform", "min": 2, "max": 5.5, "scale": "auto"},
        },
    },
    "osrs_pvp": {
        **_SWEEP_BASE,
        "metric": "episode_return",
        "metric_distribution": "linear",
        "max_suggestion_cost": 3600,  # PFSP needs longer trials, 100M+ steps
        "train": {
            # ranges centered on storm proven config with generous room
            "total_timesteps": {"distribution": "log_normal", "min": 50_000_000, "max": 200_000_000, "scale": "time"},
            "horizon": {"distribution": "uniform_pow2", "min": 16, "max": 256, "scale": "auto"},
            "learning_rate": {"distribution": "log_normal", "min": 0.0003, "max": 0.03, "scale": 0.5},
            "ent_coef": {"distribution": "log_normal", "min": 0.00005, "max": 0.01, "scale": "auto"},
            "gamma": {"distribution": "logit_normal", "min": 0.98, "max": 0.9999, "scale": "auto"},
            "min_lr_ratio": {"distribution": "uniform", "min": 0.02, "max": 0.3, "scale": "auto"},
            "beta2": {"distribution": "logit_normal", "min": 0.99, "max": 0.9999, "scale": "auto"},
            "gae_lambda": {"distribution": "logit_normal", "min": 0.1, "max": 0.99, "scale": "auto"},
            "beta1": {"distribution": "uniform", "min": 0.8, "max": 0.99, "scale": "auto"},
            "eps": {"distribution": "log_normal", "min": 1e-6, "max": 1e-3, "scale": "auto"},
            "vtrace_c_clip": {"distribution": "uniform", "min": 1.0, "max": 3.0, "scale": "auto"},
            "prio_alpha": {"distribution": "logit_normal", "min": 0.5, "max": 0.99, "scale": "auto"},
            "prio_beta0": {"distribution": "logit_normal", "min": 0.05, "max": 0.8, "scale": "auto"},
            "clip_coef": {"distribution": "uniform", "min": 0.1, "max": 0.6, "scale": "auto"},
            "vf_coef": {"distribution": "log_normal", "min": 0.5, "max": 10.0, "scale": "auto"},
            "vf_clip_coef": {"distribution": "uniform", "min": 0.1, "max": 2.0, "scale": "auto"},
            "max_grad_norm": {"distribution": "uniform", "min": 0.5, "max": 5.0, "scale": "auto"},
            "replay_ratio": {"distribution": "uniform", "min": 0.1, "max": 2.0, "scale": "auto"},
            "minibatch_size": {"distribution": "uniform_pow2", "min": 2048, "max": 8192, "scale": "auto"},
            "num_buffers": {"distribution": "uniform_pow2", "min": 1, "max": 4, "scale": "auto"},
        },
        "policy": {
            "hidden_size": {"distribution": "uniform_pow2", "min": 256, "max": 1024, "scale": "auto"},
            "num_layers": {"distribution": "uniform", "min": 2.0, "max": 6.0, "scale": "auto"},
        },
    },
    "osrs_zulrah": {
        **_SWEEP_BASE,
        "metric": "score",
        "metric_distribution": "linear",
        "max_suggestion_cost": 1800,
        "train": {
            # wide ranges — we have no idea what works for zulrah with reward shaping
            "total_timesteps": {"distribution": "log_normal", "min": 20_000_000, "max": 200_000_000, "scale": "time"},
            "horizon": {"distribution": "uniform_pow2", "min": 16, "max": 256, "scale": "auto"},
            "learning_rate": {"distribution": "log_normal", "min": 0.0005, "max": 0.05, "scale": 0.5},
            "ent_coef": {"distribution": "log_normal", "min": 0.0001, "max": 0.01, "scale": "auto"},
            "gamma": {"distribution": "logit_normal", "min": 0.98, "max": 0.9999, "scale": "auto"},
            "min_lr_ratio": {"distribution": "uniform", "min": 0.0, "max": 0.5, "scale": "auto"},
            "beta1": {"distribution": "uniform", "min": 0.7, "max": 0.99, "scale": "auto"},
            "beta2": {"distribution": "logit_normal", "min": 0.99, "max": 0.9999, "scale": "auto"},
            "eps": {"distribution": "log_normal", "min": 1e-6, "max": 1e-3, "scale": "auto"},
            "gae_lambda": {"distribution": "logit_normal", "min": 0.1, "max": 0.999, "scale": "auto"},
            "vtrace_rho_clip": {"distribution": "uniform", "min": 1.0, "max": 4.0, "scale": "auto"},
            "vtrace_c_clip": {"distribution": "uniform", "min": 1.0, "max": 3.0, "scale": "auto"},
            "prio_alpha": {"distribution": "logit_normal", "min": 0.0, "max": 0.99, "scale": "auto"},
            "prio_beta0": {"distribution": "logit_normal", "min": 0.01, "max": 0.95, "scale": "auto"},
            "clip_coef": {"distribution": "uniform", "min": 0.05, "max": 0.6, "scale": "auto"},
            "vf_coef": {"distribution": "log_normal", "min": 0.1, "max": 10.0, "scale": "auto"},
            "vf_clip_coef": {"distribution": "uniform", "min": 0.05, "max": 4.0, "scale": "auto"},
            "max_grad_norm": {"distribution": "uniform", "min": 0.3, "max": 8.0, "scale": "auto"},
            "replay_ratio": {"distribution": "uniform", "min": 0.1, "max": 4.0, "scale": "auto"},
            "minibatch_size": {"distribution": "uniform_pow2", "min": 2048, "max": 16384, "scale": "auto"},
            "num_buffers": {"distribution": "uniform_pow2", "min": 1, "max": 4, "scale": "auto"},
        },
        "policy": {
            "hidden_size": {"distribution": "uniform_pow2", "min": 128, "max": 1024, "scale": "auto"},
            "num_layers": {"distribution": "uniform", "min": 1, "max": 6.0, "scale": "auto"},
        },
    },
    "osrs_inferno": {
        **_SWEEP_BASE,
        "metric": "episode_return",
        "metric_distribution": "linear",
        "max_suggestion_cost": 1800,
        "train": {
            # wide ranges — inferno is very different from pvp/zulrah (long episodes,
            # large action space, needs high entropy to explore, long horizon for credit
            # assignment through multi-tick combat sequences)
            "total_timesteps": {"distribution": "log_normal", "min": 50_000_000, "max": 500_000_000, "scale": "time"},
            "horizon": {"distribution": "uniform_pow2", "min": 16, "max": 256, "scale": "auto"},
            "learning_rate": {"distribution": "log_normal", "min": 0.0003, "max": 0.02, "scale": 0.5},
            "ent_coef": {"distribution": "log_normal", "min": 0.001, "max": 0.05, "scale": "auto"},
            "gamma": {"distribution": "logit_normal", "min": 0.99, "max": 0.9999, "scale": "auto"},
            "min_lr_ratio": {"distribution": "uniform", "min": 0.0, "max": 0.3, "scale": "auto"},
            "beta1": {"distribution": "uniform", "min": 0.7, "max": 0.99, "scale": "auto"},
            "beta2": {"distribution": "logit_normal", "min": 0.99, "max": 0.9999, "scale": "auto"},
            "eps": {"distribution": "log_normal", "min": 1e-6, "max": 1e-3, "scale": "auto"},
            "gae_lambda": {"distribution": "logit_normal", "min": 0.5, "max": 0.999, "scale": "auto"},
            "vtrace_rho_clip": {"distribution": "uniform", "min": 1.0, "max": 4.0, "scale": "auto"},
            "vtrace_c_clip": {"distribution": "uniform", "min": 1.0, "max": 3.0, "scale": "auto"},
            "prio_alpha": {"distribution": "logit_normal", "min": 0.0, "max": 0.99, "scale": "auto"},
            "prio_beta0": {"distribution": "logit_normal", "min": 0.01, "max": 0.95, "scale": "auto"},
            "clip_coef": {"distribution": "uniform", "min": 0.05, "max": 0.5, "scale": "auto"},
            "vf_coef": {"distribution": "log_normal", "min": 0.1, "max": 5.0, "scale": "auto"},
            "vf_clip_coef": {"distribution": "uniform", "min": 0.05, "max": 4.0, "scale": "auto"},
            "max_grad_norm": {"distribution": "uniform", "min": 0.3, "max": 5.0, "scale": "auto"},
            "replay_ratio": {"distribution": "uniform", "min": 0.1, "max": 3.0, "scale": "auto"},
            "minibatch_size": {"distribution": "uniform_pow2", "min": 2048, "max": 16384, "scale": "auto"},
            "num_buffers": {"distribution": "uniform_pow2", "min": 1, "max": 4, "scale": "auto"},
        },
        "policy": {
            "hidden_size": {"distribution": "uniform_pow2", "min": 128, "max": 512, "scale": "auto"},
            "num_layers": {"distribution": "uniform", "min": 1, "max": 5.0, "scale": "auto"},
        },
    },
}

DEFAULT_PARAMS_PER_ENV = {
    # best known Metal breakout config (trial #203, score 854, tensor_ops NT)
    "breakout": {
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
            "ns_iters": 5,
        },
        "policy": {
            "hidden_size": 64,
            "num_layers": 2,
        },
    },
    # g2048 anchor: upstream-ish config adapted for Metal (hs=128 L=3 as baseline)
    "g2048": {
        "train": {
            "total_timesteps": 200_000_000,
            "horizon": 32,
            "min_lr_ratio": 0.0,
            "learning_rate": 0.003,
            "beta1": 0.9,
            "beta2": 0.999,
            "eps": 1e-5,
            "ent_coef": 0.01,
            "gamma": 0.998,
            "gae_lambda": 0.95,
            "vtrace_rho_clip": 1.0,
            "vtrace_c_clip": 1.0,
            "prio_alpha": 0.01,
            "prio_beta0": 0.4,
            "clip_coef": 0.2,
            "vf_coef": 1.0,
            "vf_clip_coef": 0.1,
            "max_grad_norm": 0.5,
            "replay_ratio": 1.5,
            "minibatch_size": 8192,
            "total_agents": 4096,
            "num_buffers": 4,
            "num_threads": 4,
            "ns_iters": 5,
            "scaffolding_ratio": 0.5,
        },
        "policy": {
            "hidden_size": 128,
            "num_layers": 3,
        },
    },
    # osrs_pvp anchor: from storm PufferLib 3.0+MPS proven config (95.5% winrate)
    "osrs_pvp": {
        "train": {
            "total_timesteps": 100_000_000,
            "horizon": 128,
            "min_lr_ratio": 0.1,
            "learning_rate": 0.00112,
            "beta1": 0.95,
            "beta2": 0.999,
            "eps": 1e-5,
            "ent_coef": 0.0016,
            "gamma": 0.991,
            "gae_lambda": 0.845,
            "vtrace_rho_clip": 1.0,
            "vtrace_c_clip": 1.5,
            "prio_alpha": 0.914,
            "prio_beta0": 0.218,
            "clip_coef": 0.32,
            "vf_coef": 2.5,
            "vf_clip_coef": 0.5,
            "max_grad_norm": 2.0,
            "replay_ratio": 0.25,
            "minibatch_size": 4096,
            "total_agents": 2048,
            "num_buffers": 2,
            "num_threads": 2,
            "ns_iters": 5,
        },
        "policy": {
            "hidden_size": 512,
            "num_layers": 3,
        },
    },
    # osrs_inferno anchor: lr=0.002 confirmed stable at 10M steps (ret 0.76→1.57)
    "osrs_inferno": {
        "train": {
            "total_timesteps": 100_000_000,
            "horizon": 32,
            "min_lr_ratio": 0.1,
            "learning_rate": 0.002,
            "beta1": 0.9,
            "beta2": 0.999,
            "eps": 1e-5,
            "ent_coef": 0.005,
            "gamma": 0.997,
            "gae_lambda": 0.85,
            "vtrace_rho_clip": 1.0,
            "vtrace_c_clip": 1.5,
            "prio_alpha": 0.5,
            "prio_beta0": 0.3,
            "clip_coef": 0.2,
            "vf_coef": 0.5,
            "vf_clip_coef": 0.5,
            "max_grad_norm": 2.0,
            "replay_ratio": 0.25,
            "minibatch_size": 2048,
            "total_agents": 2048,
            "num_buffers": 2,
            "num_threads": 2,
            "ns_iters": 5,
        },
        "policy": {
            "hidden_size": 256,
            "num_layers": 2,
        },
    },
    # osrs_zulrah anchor: sweep trial 601, fastest positive (score=0.88, 702K SPS)
    "osrs_zulrah": {
        "train": {
            "total_timesteps": 26_000_000,
            "horizon": 16,
            "min_lr_ratio": 0.35,
            "learning_rate": 0.015,
            "beta1": 0.962,
            "beta2": 0.999,
            "eps": 6.3e-4,
            "ent_coef": 0.0056,
            "gamma": 0.997,
            "gae_lambda": 0.824,
            "vtrace_rho_clip": 1.25,
            "vtrace_c_clip": 2.69,
            "prio_alpha": 0.93,
            "prio_beta0": 0.01,
            "clip_coef": 0.256,
            "vf_coef": 0.418,
            "vf_clip_coef": 0.1,
            "max_grad_norm": 4.64,
            "replay_ratio": 0.25,
            "minibatch_size": 2048,
            "total_agents": 2048,
            "num_buffers": 4,
            "num_threads": 4,
            "ns_iters": 5,
        },
        "policy": {
            "hidden_size": 512,
            "num_layers": 1,
        },
    },
}



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





def _init_pfsp(pufferl: object, total_agents: int) -> dict:
    """Initialize PFSP pool with uniform weights. Returns PFSP state dict."""
    pool_size = len(PFSP_POOL_TYPES)
    cum_weights = [int((i + 1) / pool_size * 1000) for i in range(pool_size)]
    cum_weights[-1] = 1000
    pufferl.set_pfsp_weights(PFSP_POOL_TYPES, cum_weights)
    return {
        "cum_episodes": [0.0] * pool_size,
        "last_update_step": 0,
    }


def _update_pfsp(pufferl: object, pfsp_state: dict, global_step: int) -> None:
    """Recompute PFSP weights based on per-opponent win rates."""
    if (global_step - pfsp_state["last_update_step"]) < PFSP_UPDATE_INTERVAL:
        return

    wins_delta, episodes_delta = pufferl.get_pfsp_stats()
    pool_size = len(PFSP_POOL_TYPES)

    for i in range(pool_size):
        pfsp_state["cum_episodes"][i] += episodes_delta[i]

    if min(pfsp_state["cum_episodes"]) < PFSP_WARMUP_EPISODES:
        pfsp_state["last_update_step"] = global_step
        return

    raw_weights = []
    for i in range(pool_size):
        wr = wins_delta[i] / max(episodes_delta[i], 1)
        raw_weights.append(max((1.0 - wr) ** PFSP_P, PFSP_WEIGHT_FLOOR))
    total_w = sum(raw_weights)
    cum_weights = [int(sum(raw_weights[:i + 1]) / total_w * 1000) for i in range(pool_size)]
    cum_weights[-1] = 1000
    pufferl.set_pfsp_weights(PFSP_POOL_TYPES, cum_weights)
    pfsp_state["last_update_step"] = global_step


def run_trial(
    trial_idx: int,
    env_name: str,
    params: dict,
    protein: Protein,
    sweep_dir: Path,
) -> dict | None:
    """Run a single training trial using the shared training loop."""
    flat = dict(pufferlib.unroll_nested_dict(params))
    total_steps = int(flat.get("train/total_timesteps", 0))
    total_agents = int(flat.get("train/total_agents", 4096))

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

    config, vec_config, env_config, policy_config = build_configs(env_name, params)

    # PFSP setup for osrs_pvp
    pfsp_state = None
    if env_name == "osrs_pvp" and env_config.get("opponent_type", 0) == float(OPP_PFSP):
        pfsp_state = {"total_agents": total_agents}

    last_report_time = time.time()
    log_count = 0
    score_key = SCORE_METRIC_PER_ENV.get(env_name, "score")

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
        # SPS abort: check first 2 log points
        if log_count <= 2 and result_ref[0] and result_ref[0].entries:
            recent_sps = result_ref[0].entries[-1]["sps"]
            min_sps = MIN_SPS_PER_ENV.get(env_name, 100_000)
            if recent_sps < min_sps:
                print(f"  ABORT: SPS={recent_sps:.0f} < {min_sps}")
                return True
        return protein.should_stop(score, elapsed)

    pfsp_initialized = [False]

    def on_iteration(pufferl, global_step):
        if pfsp_state is not None:
            if not pfsp_initialized[0]:
                _init_pfsp(pufferl, total_agents)
                pfsp_state["cum_episodes"] = [0.0] * len(PFSP_POOL_TYPES)
                pfsp_state["last_update_step"] = 0
                pfsp_initialized[0] = True
                print(f"  PFSP: {len(PFSP_POOL_TYPES)} opponents, uniform initial weights")
            _update_pfsp(pufferl, pfsp_state, global_step)

    result_ref = [None]
    start_time = time.time()

    try:

        result = run_training(
            config, vec_config, env_config, policy_config,
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


def run_sweep(env_name: str, max_trials: int | None, timeout_h: float) -> None:
    """Run the Protein sweep for a simple env."""
    sweep_dir = SWEEP_DIR_BASE / env_name
    sweep_dir.mkdir(parents=True, exist_ok=True)
    obs_path = sweep_dir / "observations.jsonl"

    sweep_config = deepcopy(SWEEP_CONFIGS[env_name])
    # override metric distribution per env if specified
    if env_name in METRIC_DIST_PER_ENV:
        sweep_config["metric_distribution"] = METRIC_DIST_PER_ENV[env_name]
    default_params = DEFAULT_PARAMS_PER_ENV[env_name]

    protein = Protein(sweep_config, use_gpu=False, prune_pareto=True)

    existing_records = load_observations(obs_path)
    existing_trials: set[int] = set()
    if existing_records:
        for r in existing_records:
            existing_trials.add(r["trial"])
            # backfill params added after old sweep runs
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

    score_key = SCORE_METRIC_PER_ENV.get(env_name, "score")
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




# ============================================================================
# CLI: train mode (single run)
# ============================================================================

def parse_args():
    p = argparse.ArgumentParser(description="Metal training for simple envs")
    p.add_argument("--env", type=str, required=True,
                   choices=list(ENV_DEFAULTS.keys()))
    p.add_argument("--total-agents", type=int, default=2048)
    p.add_argument("--hidden-size", type=int, default=128)
    p.add_argument("--num-layers", type=int, default=1)
    p.add_argument("--horizon", type=int, default=32)
    p.add_argument("--total-timesteps", type=int, default=5_000_000)
    p.add_argument("--learning-rate", type=float, default=0.001)
    p.add_argument("--beta1", type=float, default=0.95)
    p.add_argument("--beta2", type=float, default=0.999)
    p.add_argument("--eps", type=float, default=1e-12)
    p.add_argument("--minibatch-size", type=int, default=4096)
    p.add_argument("--replay-ratio", type=float, default=0.25,
                   help="minibatch replays per rollout. values above ~0.5 cause catastrophic "
                        "policy drift in multi-head action spaces (7+ heads). breakout (1 head) "
                        "tolerates 1.9+, osrs_pvp (7 heads) needs 0.25-0.5.")
    p.add_argument("--ent-coef", type=float, default=0.01)
    p.add_argument("--gamma", type=float, default=0.99)
    p.add_argument("--gae-lambda", type=float, default=0.95)
    p.add_argument("--vtrace-rho-clip", type=float, default=1.0)
    p.add_argument("--vtrace-c-clip", type=float, default=1.0)
    p.add_argument("--prio-alpha", type=float, default=0.0)
    p.add_argument("--prio-beta0", type=float, default=0.4)
    p.add_argument("--clip-coef", type=float, default=0.2)
    p.add_argument("--vf-coef", type=float, default=0.5)
    p.add_argument("--vf-clip-coef", type=float, default=0.1)
    p.add_argument("--max-grad-norm", type=float, default=0.5)
    p.add_argument("--no-overlap", action="store_true")
    p.add_argument("--log-interval", type=int, default=10)
    p.add_argument("--seed", type=int, default=42)
    p.add_argument("--trace-path", type=str, default="",
                   help="optional jsonl trace output path for parity debugging")
    p.add_argument("--trace-every", type=int, default=1,
                   help="write one trace row every N training iterations")
    p.add_argument("--num-buffers", type=int, default=1)
    p.add_argument("--profile", action="store_true",
                   help="GPU sync after each training phase for accurate per-kernel profiling")
    p.add_argument("--cpu-inference", action="store_true",
                   help="CPU forward pass during rollout (no GPU sync, uses Accelerate cblas)")
    p.add_argument("--fp16", action="store_true",
                   help="fp16 training activations/grads (rollout stays fp32)")
    p.add_argument("--ns-iters", type=int, default=5,
                   help="Newton-Schulz iterations in muon optimizer (1-5, default 5)")
    p.add_argument("--scaffolding-ratio", type=float, default=None,
                   help="Override env scaffolding_ratio (g2048 only)")
    p.add_argument("--min-lr-ratio", type=float, default=0.0,
                   help="minimum LR as ratio of initial (upstream default: 0.0)")
    return p.parse_args()



def train_cli():
    args = parse_args()

    params = {
        "train": {
            "horizon": args.horizon,
            "learning_rate": args.learning_rate,
            "min_lr_ratio": args.min_lr_ratio,
            "beta1": args.beta1,
            "beta2": args.beta2,
            "eps": args.eps,
            "minibatch_size": args.minibatch_size,
            "replay_ratio": args.replay_ratio,
            "total_timesteps": args.total_timesteps,
            "max_grad_norm": args.max_grad_norm,
            "clip_coef": args.clip_coef,
            "vf_clip_coef": args.vf_clip_coef,
            "vf_coef": args.vf_coef,
            "ent_coef": args.ent_coef,
            "gamma": args.gamma,
            "gae_lambda": args.gae_lambda,
            "vtrace_rho_clip": args.vtrace_rho_clip,
            "vtrace_c_clip": args.vtrace_c_clip,
            "prio_alpha": args.prio_alpha,
            "prio_beta0": args.prio_beta0,
            "total_agents": args.total_agents,
            "num_buffers": args.num_buffers,
            "profile": 1 if args.profile else 0,
            "overlap": 0 if args.no_overlap else 1,
            "cpu_inference": 1 if args.cpu_inference else 0,
            "train_fp16": 1 if args.fp16 else 0,
            "ns_iters": args.ns_iters,
            "seed": args.seed,
        },
        "policy": {
            "hidden_size": args.hidden_size,
            "num_layers": args.num_layers,
        },
    }
    if args.scaffolding_ratio is not None:
        params["train"]["scaffolding_ratio"] = args.scaffolding_ratio

    config, vec_config, env_config, policy_config = build_configs(args.env, params)

    print(f"env={args.env}, agents={args.total_agents}, hidden={args.hidden_size}, "
          f"layers={args.num_layers}, horizon={args.horizon}, overlap={not args.no_overlap}, "
          f"cpu_infer={args.cpu_inference}, fp16={args.fp16}, seed={args.seed}")

    # optional trace output
    trace_file = None
    trace_every = max(int(args.trace_every), 1)
    if args.trace_path:
        trace_path = Path(args.trace_path).expanduser().resolve()
        trace_path.parent.mkdir(parents=True, exist_ok=True)
        trace_file = trace_path.open("w", encoding="utf-8")
        trace_file.write(json.dumps({
            "event": "meta", "env": args.env, "seed": args.seed,
            "total_agents": args.total_agents, "hidden_size": args.hidden_size,
            "num_layers": args.num_layers, "horizon": args.horizon,
            "total_timesteps": args.total_timesteps, "learning_rate": args.learning_rate,
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
        print(f"[step={global_step:>10,} | SPS={sps:>10,.0f} | "
              f"ret={ep_ret:>8.2f} score={score:>8.2f} len={ep_len:>6.0f} | "
              f"ent={ent:.3f} pg={pg:.4f} vf={vf:.4f}]")

        if trace_file and iteration % trace_every == 0:
            from pufferlib import _C as _c
            trace_row = {
                "event": "tick", "iteration": iteration, "step": global_step,
                "sps": sps, "score": score, "episode_return": ep_ret,
                "episode_length": ep_len, "entropy": ent,
                "pg_loss": pg, "vf_loss": vf,
            }
            trace_file.write(json.dumps(trace_row) + "\n")

    print(f"model params: {int(config.get('total_timesteps', 0)):,} steps target")

    result = run_training(
        config, vec_config, env_config, policy_config,
        log_interval=args.log_interval,
        on_log=on_log,
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




# ============================================================================
# CLI: sweep mode (Protein optimization)
# ============================================================================

def sweep_cli():
    parser = argparse.ArgumentParser(description="protein sweep for Metal simple envs")
    parser.add_argument("--env", type=str, required=True,
                        choices=list(SWEEP_CONFIGS.keys()))
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



# ============================================================================
# CLI dispatcher
# ============================================================================


def main():
    if len(sys.argv) < 2:
        print("usage: python pufferl.py [train|sweep|results] <env> [args]")
        sys.exit(1)

    mode = sys.argv.pop(1)
    if mode == "train":
        train_cli()
    elif mode == "sweep":
        sweep_cli()
    elif mode == "results":
        if len(sys.argv) < 2:
            print("usage: python pufferl.py results <env>")
            sys.exit(1)
        env = sys.argv.pop(1)
        print_results(SWEEP_DIR_BASE / env / "observations.jsonl")
    else:
        print(f"unknown mode: {mode}. use train, sweep, or results.")
        sys.exit(1)


if __name__ == "__main__":
    main()
