#!/usr/bin/env python3
"""Reproduce sweep-style reinit across changing model sizes.

Build first:
    python setup.py build_breakout --inplace

Run:
    python test_sweep_crash.py
"""

import os
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))

from bench import ENV_DEFAULTS
from pufferlib import _C

ENV_NAME = "breakout"
ENV_CONFIG = ENV_DEFAULTS[ENV_NAME]

BASE_CONFIG = {
    "total_timesteps": 1_000_000.0,
    "horizon": 32.0,
    "learning_rate": 0.001,
    "min_lr_ratio": 0.1,
    "anneal_lr": 1.0,
    "beta1": 0.95,
    "beta2": 0.999,
    "eps": 1e-12,
    "minibatch_size": 4096.0,
    "replay_ratio": 0.25,
    "max_grad_norm": 1.5,
    "clip_coef": 0.2,
    "vf_clip_coef": 0.2,
    "vf_coef": 2.0,
    "ent_coef": 0.01,
    "gamma": 0.99,
    "gae_lambda": 0.90,
    "vtrace_rho_clip": 1.0,
    "vtrace_c_clip": 1.0,
    "prio_alpha": 0.8,
    "prio_beta0": 0.2,
    "use_rnn": 1.0,
    "cudagraphs": -1.0,
    "kernels": 1.0,
    "profile": 0.0,
    "overlap": 1.0,
    "use_adam": 1.0,
    "env_name": ENV_NAME,
}

VEC_CONFIG = {
    "total_agents": 4096.0,
    "num_buffers": 2.0,
    "num_threads": 4.0,
}

POLICY_CONFIGS = [
    {"hidden_size": 128.0, "num_layers": 1.0, "arch": 1.0},
    {"hidden_size": 512.0, "num_layers": 2.0, "arch": 1.0},
    {"hidden_size": 256.0, "num_layers": 3.0, "arch": 1.0},
    {"hidden_size": 1024.0, "num_layers": 1.0, "arch": 1.0},
]


def run_single_trial(trial_id: int, policy_config: dict[str, float]) -> None:
    """Run one create-rollout-train-close cycle for one policy size."""
    hidden_size = int(policy_config["hidden_size"])
    num_layers = int(policy_config["num_layers"])
    os.write(
        2,
        f"\n=== trial {trial_id} (hidden={hidden_size}, layers={num_layers}) ===\n".encode(),
    )

    pufferl = _C.create_pufferl(BASE_CONFIG, VEC_CONFIG, ENV_CONFIG, policy_config)
    os.write(2, f"  created ({pufferl.num_params():,} params)\n".encode())

    for iteration_index in range(3):
        _C.rollouts(pufferl)
        _C.train(pufferl)
        os.write(2, f"  iter {iteration_index} done\n".encode())

    os.write(2, b"  closing\n")
    _C.close(pufferl)
    os.write(2, f"  trial {trial_id} closed\n".encode())


if __name__ == "__main__":
    for trial_id, policy_config in enumerate(POLICY_CONFIGS):
        run_single_trial(trial_id=trial_id, policy_config=policy_config)
    os.write(2, b"\nall done\n")
