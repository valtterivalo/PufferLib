"""Shared Metal training loop for single runs and sweeps.

All training goes through run_training(). bench.py and sweep_bench.py
are thin wrappers that build configs and call this function.
"""

import math
import time
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from pufferlib import _C

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
