"""Deterministic native backend parity fixtures.

Run after building the native backend for the target env. The script writes a
small JSON record that can be compared across CUDA and Metal builds without
depending on backend RNG equality.
"""

from __future__ import annotations

import argparse
import json
from pathlib import Path
from typing import Any

import numpy as np


def advantage_reference(
    values: np.ndarray,
    rewards: np.ndarray,
    dones: np.ndarray,
    importance: np.ndarray,
    gamma: float,
    gae_lambda: float,
    rho_clip: float,
    c_clip: float,
) -> np.ndarray:
    """Match current CUDA advantage dispatch semantics."""
    num_steps, horizon = values.shape
    out = np.zeros_like(values, dtype=np.float32)
    vector_mode = horizon % 4 == 0
    for row in range(num_steps):
        last = np.float32(0.0)
        for t in range(horizon - 2, -1, -1):
            next_t = t + 1
            next_nonterminal = np.float32(1.0) - dones[row, next_t]
            rho_t = min(importance[row, t], rho_clip)
            c_t = min(importance[row, t], c_clip)
            if vector_mode:
                delta = rho_t * (
                    rewards[row, next_t]
                    + gamma * values[row, next_t] * next_nonterminal
                    - values[row, t]
                )
            else:
                delta = (
                    rho_t * rewards[row, next_t]
                    + gamma * values[row, next_t] * next_nonterminal
                    - values[row, t]
                )
            last = delta + gamma * gae_lambda * c_t * last * next_nonterminal
            out[row, t] = last
    return out


def run_metal_advantage(cmod: Any, fixture: dict[str, Any]) -> list[float]:
    """Run the Metal CPU-exposed advantage helper against fixed arrays."""
    values = np.ascontiguousarray(fixture["values"], dtype=np.float32)
    rewards = np.ascontiguousarray(fixture["rewards"], dtype=np.float32)
    dones = np.ascontiguousarray(fixture["dones"], dtype=np.float32)
    importance = np.ascontiguousarray(fixture["importance"], dtype=np.float32)
    out = np.zeros_like(values, dtype=np.float32)
    cmod.puff_advantage_cpu(
        values.ctypes.data,
        rewards.ctypes.data,
        dones.ctypes.data,
        importance.ctypes.data,
        out.ctypes.data,
        values.shape[0],
        values.shape[1],
        fixture["gamma"],
        fixture["gae_lambda"],
        fixture["rho_clip"],
        fixture["c_clip"],
    )
    return out.reshape(-1).tolist()


def run_cuda_advantage(cmod: Any, fixture: dict[str, Any]) -> list[float]:
    """Run the CUDA advantage kernel against fixed arrays."""
    import torch

    if not torch.cuda.is_available():
        raise RuntimeError("CUDA backend parity requires torch.cuda.is_available()")

    values = torch.tensor(fixture["values"], dtype=torch.float32, device="cuda")
    rewards = torch.tensor(fixture["rewards"], dtype=torch.float32, device="cuda")
    dones = torch.tensor(fixture["dones"], dtype=torch.float32, device="cuda")
    importance = torch.tensor(fixture["importance"], dtype=torch.float32, device="cuda")
    out = torch.zeros_like(values)
    cmod.puff_advantage(
        values.data_ptr(),
        rewards.data_ptr(),
        dones.data_ptr(),
        importance.data_ptr(),
        out.data_ptr(),
        values.shape[0],
        values.shape[1],
        fixture["gamma"],
        fixture["gae_lambda"],
        fixture["rho_clip"],
        fixture["c_clip"],
    )
    torch.cuda.synchronize()
    return out.cpu().numpy().reshape(-1).tolist()


def priority_reference() -> dict[str, list[float]]:
    """Return current CUDA priority probability and raw importance fixtures."""
    advantages = np.array(
        [
            [0.0, -0.5, 0.25, 0.125],
            [0.75, 0.0, -0.25, 0.5],
            [0.0, 0.0, 0.0, 0.0],
        ],
        dtype=np.float32,
    )
    alpha = np.float32(0.6)
    beta = np.float32(0.4)
    weights = np.power(np.sum(np.abs(advantages), axis=1), alpha, dtype=np.float32)
    weights = np.where(np.isfinite(weights), weights, np.float32(0.0))
    probs = (weights + np.float32(1e-6)) / (np.sum(weights) + np.float32(1e-6))
    picked = np.array([0, 1, 2], dtype=np.int64)
    importance = np.power(probs[picked] * np.float32(advantages.shape[0]), -beta)
    return {
        "probabilities": probs.astype(np.float32).tolist(),
        "importance_weights": importance.astype(np.float32).tolist(),
    }


def make_advantage_fixture(horizon: int) -> dict[str, Any]:
    """Build one scalar or vector-dispatch advantage fixture."""
    values = np.linspace(-0.2, 0.7, 2 * horizon, dtype=np.float32).reshape(2, horizon)
    rewards = np.linspace(0.3, -0.4, 2 * horizon, dtype=np.float32).reshape(2, horizon)
    dones = np.zeros((2, horizon), dtype=np.float32)
    dones[0, horizon // 2] = 1.0
    importance = np.linspace(0.25, 1.75, 2 * horizon, dtype=np.float32).reshape(2, horizon)
    return {
        "horizon": horizon,
        "values": values,
        "rewards": rewards,
        "dones": dones,
        "importance": importance,
        "gamma": 0.97,
        "gae_lambda": 0.91,
        "rho_clip": 1.2,
        "c_clip": 0.8,
    }


def main() -> None:
    """Run backend fixtures and write JSON."""
    parser = argparse.ArgumentParser()
    parser.add_argument("--backend", choices=("metal", "cuda"), required=True)
    parser.add_argument("--write-json", type=Path, required=True)
    args = parser.parse_args()

    from pufferlib import _C as cmod

    advantage_results = {}
    for horizon in (7, 8):
        fixture = make_advantage_fixture(horizon)
        ref = advantage_reference(
            fixture["values"],
            fixture["rewards"],
            fixture["dones"],
            fixture["importance"],
            fixture["gamma"],
            fixture["gae_lambda"],
            fixture["rho_clip"],
            fixture["c_clip"],
        )
        actual = (
            run_metal_advantage(cmod, fixture)
            if args.backend == "metal"
            else run_cuda_advantage(cmod, fixture)
        )
        advantage_results[str(horizon)] = {
            "reference": ref.reshape(-1).tolist(),
            "actual": actual,
        }

    result = {
        "backend": args.backend,
        "compiled_env_name": getattr(cmod, "env_name", "unknown"),
        "static_env_name": getattr(cmod, "static_env_name", "unknown"),
        "precision_bytes": getattr(cmod, "precision_bytes", None),
        "advantage": advantage_results,
        "priority_reference": priority_reference(),
        "rng_diagnostic": "sampling RNG is intentionally not a strict parity gate",
    }
    args.write_json.parent.mkdir(parents=True, exist_ok=True)
    args.write_json.write_text(json.dumps(result, indent=2, sort_keys=True) + "\n")


if __name__ == "__main__":
    main()
