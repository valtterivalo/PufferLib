#!/usr/bin/env python3
"""Reproduce create-close-create stability with `num_buffers=2` on breakout.

Build first:
    python setup.py build_breakout --inplace

Run:
    python test_num_buffers.py
"""

import os
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))

from bench import ENV_DEFAULTS
from pufferlib import _C


def create_pufferl() -> object:
    """Create a breakout pufferl instance for a short smoke run."""
    env_name = "breakout"
    env_config = ENV_DEFAULTS[env_name]

    config = {
        "total_timesteps": 1_000_000.0,
        "horizon": 32.0,
        "learning_rate": 0.001,
        "min_lr_ratio": 0.0,
        "anneal_lr": 0.0,
        "beta1": 0.95,
        "beta2": 0.999,
        "eps": 1e-8,
        "minibatch_size": 4096.0,
        "replay_ratio": 0.25,
        "max_grad_norm": 0.5,
        "clip_coef": 0.2,
        "vf_clip_coef": 0.2,
        "vf_coef": 0.5,
        "ent_coef": 0.01,
        "gamma": 0.99,
        "gae_lambda": 0.95,
        "vtrace_rho_clip": 1.0,
        "vtrace_c_clip": 1.0,
        "prio_alpha": 0.6,
        "prio_beta0": 0.4,
        "use_rnn": 1.0,
        "kernels": 1.0,
        "profile": 0.0,
        "overlap": 1.0,
        "env_name": env_name,
    }

    vec_config = {
        "total_agents": 4096.0,
        "num_buffers": 2.0,
        "num_threads": 4.0,
    }

    policy_config = {
        "hidden_size": 128.0,
        "num_layers": 1.0,
        "arch": 1.0,
    }

    return _C.create_pufferl(config, vec_config, env_config, policy_config)


def run_trial(trial_id: int, iterations: int) -> None:
    """Run a short rollout-train-close loop for one trial."""
    os.write(2, f"\n=== trial {trial_id} ===\n".encode())
    pufferl = create_pufferl()
    os.write(2, b"  created\n")

    for iteration_index in range(iterations):
        _C.rollouts(pufferl)
        _C.train(pufferl)
        os.write(2, f"  iter {iteration_index} done\n".encode())

    os.write(2, b"  closing\n")
    _C.close(pufferl)
    os.write(2, b"  closed\n")


if __name__ == "__main__":
    run_trial(trial_id=0, iterations=3)
    run_trial(trial_id=1, iterations=1)
    os.write(2, b"\nall done\n")
