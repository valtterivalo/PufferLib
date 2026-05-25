"""Compare deterministic native backend parity JSON files."""

from __future__ import annotations

import argparse
import json
from pathlib import Path

import numpy as np


def assert_close(name: str, lhs: list[float], rhs: list[float], atol: float, rtol: float) -> None:
    """Raise on the first parity mismatch."""
    left = np.asarray(lhs, dtype=np.float32)
    right = np.asarray(rhs, dtype=np.float32)
    if left.shape != right.shape:
        raise AssertionError(f"{name}: shape mismatch {left.shape} != {right.shape}")
    if not np.allclose(left, right, atol=atol, rtol=rtol, equal_nan=True):
        delta = np.max(np.abs(left - right))
        raise AssertionError(f"{name}: max abs diff {delta} exceeds atol={atol}, rtol={rtol}")


def main() -> None:
    """Compare CUDA and Metal fixture outputs."""
    parser = argparse.ArgumentParser()
    parser.add_argument("metal_json", type=Path)
    parser.add_argument("cuda_json", type=Path)
    parser.add_argument("--atol", type=float, default=2e-5)
    parser.add_argument("--rtol", type=float, default=2e-5)
    args = parser.parse_args()

    metal = json.loads(args.metal_json.read_text())
    cuda = json.loads(args.cuda_json.read_text())

    if metal["backend"] != "metal":
        raise AssertionError(f"first file must be metal, got {metal['backend']}")
    if cuda["backend"] != "cuda":
        raise AssertionError(f"second file must be cuda, got {cuda['backend']}")

    for horizon, metal_fixture in metal["advantage"].items():
        cuda_fixture = cuda["advantage"][horizon]
        assert_close(
            f"advantage[{horizon}] metal-reference",
            metal_fixture["actual"],
            metal_fixture["reference"],
            args.atol,
            args.rtol,
        )
        assert_close(
            f"advantage[{horizon}] cuda-reference",
            cuda_fixture["actual"],
            cuda_fixture["reference"],
            args.atol,
            args.rtol,
        )
        assert_close(
            f"advantage[{horizon}] metal-cuda",
            metal_fixture["actual"],
            cuda_fixture["actual"],
            args.atol,
            args.rtol,
        )

    for key in ("probabilities", "importance_weights"):
        assert_close(
            f"priority_reference[{key}]",
            metal["priority_reference"][key],
            cuda["priority_reference"][key],
            args.atol,
            args.rtol,
        )

    print("native backend parity fixtures match")


if __name__ == "__main__":
    main()
