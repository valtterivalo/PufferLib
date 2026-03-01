"""Compare two bench trace JSONL files and report first divergence."""

from __future__ import annotations

import argparse
import json
import math
from dataclasses import dataclass
from pathlib import Path


EXCLUDED_KEYS = {
    "event",
    "iteration",
    "step",
}


@dataclass(frozen=True)
class Divergence:
    """Represents a metric divergence at one training step."""

    step: int
    metric: str
    left_value: float
    right_value: float
    absolute_delta: float
    relative_delta: float


def parse_args() -> argparse.Namespace:
    """Parse command-line arguments."""
    parser = argparse.ArgumentParser(
        description="compare two training traces from bench.py --trace-path",
    )
    parser.add_argument("--left", type=Path, required=True, help="left trace jsonl path")
    parser.add_argument("--right", type=Path, required=True, help="right trace jsonl path")
    parser.add_argument("--atol", type=float, default=1e-6, help="absolute tolerance")
    parser.add_argument("--rtol", type=float, default=1e-3, help="relative tolerance")
    parser.add_argument(
        "--metrics",
        type=str,
        default=(
            "score,episode_return,episode_length,entropy,pg_loss,vf_loss,"
            "total_loss,old_approx_kl,approx_kl,clipfrac,sps"
        ),
        help="comma-separated metrics to compare",
    )
    return parser.parse_args()


def load_trace(trace_path: Path) -> dict[int, dict[str, float]]:
    """Load trace ticks keyed by step from a trace JSONL file."""
    ticks_by_step: dict[int, dict[str, float]] = {}
    with trace_path.open("r", encoding="utf-8") as trace_file:
        for raw_line in trace_file:
            line = raw_line.strip()
            if not line:
                continue
            payload = json.loads(line)
            if payload.get("event") != "tick":
                continue
            step_value = payload.get("step")
            if not isinstance(step_value, int):
                continue
            numeric_payload: dict[str, float] = {}
            for key, value in payload.items():
                if key in EXCLUDED_KEYS:
                    continue
                if isinstance(value, (int, float)):
                    numeric_payload[key] = float(value)
            ticks_by_step[step_value] = numeric_payload
    return ticks_by_step


def compute_relative_delta(left_value: float, right_value: float) -> float:
    """Return symmetric relative delta."""
    scale = max(abs(left_value), abs(right_value), 1e-12)
    return abs(left_value - right_value) / scale


def values_differ(
    left_value: float,
    right_value: float,
    atol: float,
    rtol: float,
) -> tuple[bool, float, float]:
    """Check whether two float values differ beyond tolerances."""
    left_is_finite = math.isfinite(left_value)
    right_is_finite = math.isfinite(right_value)
    if left_is_finite != right_is_finite:
        return True, math.inf, math.inf
    if not left_is_finite and not right_is_finite:
        return False, 0.0, 0.0
    absolute_delta = abs(left_value - right_value)
    relative_delta = compute_relative_delta(left_value, right_value)
    threshold = atol + rtol * max(abs(left_value), abs(right_value))
    return absolute_delta > threshold, absolute_delta, relative_delta


def first_divergence(
    left_ticks: dict[int, dict[str, float]],
    right_ticks: dict[int, dict[str, float]],
    metrics: list[str],
    atol: float,
    rtol: float,
) -> Divergence | None:
    """Find the first step and metric where traces diverge."""
    common_steps = sorted(set(left_ticks.keys()) & set(right_ticks.keys()))
    for step in common_steps:
        left_row = left_ticks[step]
        right_row = right_ticks[step]
        for metric_name in metrics:
            if metric_name not in left_row or metric_name not in right_row:
                continue
            left_value = left_row[metric_name]
            right_value = right_row[metric_name]
            is_different, absolute_delta, relative_delta = values_differ(
                left_value,
                right_value,
                atol=atol,
                rtol=rtol,
            )
            if is_different:
                return Divergence(
                    step=step,
                    metric=metric_name,
                    left_value=left_value,
                    right_value=right_value,
                    absolute_delta=absolute_delta,
                    relative_delta=relative_delta,
                )
    return None


def summarize_overlap(
    left_ticks: dict[int, dict[str, float]],
    right_ticks: dict[int, dict[str, float]],
) -> tuple[int, int, int]:
    """Return counts for left-only, right-only, and shared steps."""
    left_steps = set(left_ticks.keys())
    right_steps = set(right_ticks.keys())
    shared_steps = left_steps & right_steps
    return len(left_steps - right_steps), len(right_steps - left_steps), len(shared_steps)


def main() -> int:
    """Run trace comparison and print result summary."""
    args = parse_args()
    left_path = args.left.expanduser().resolve()
    right_path = args.right.expanduser().resolve()
    left_ticks = load_trace(left_path)
    right_ticks = load_trace(right_path)

    left_only, right_only, shared = summarize_overlap(left_ticks, right_ticks)
    print(f"left ticks: {len(left_ticks)}  path={left_path}")
    print(f"right ticks: {len(right_ticks)}  path={right_path}")
    print(f"shared steps: {shared}  left-only: {left_only}  right-only: {right_only}")
    if shared == 0:
        print("no shared steps; cannot compare")
        return 2

    metrics = [metric.strip() for metric in args.metrics.split(",") if metric.strip()]
    divergence = first_divergence(
        left_ticks,
        right_ticks,
        metrics=metrics,
        atol=args.atol,
        rtol=args.rtol,
    )

    if divergence is None:
        print("no divergence found within selected metrics and tolerances")
        return 0

    print("\nfirst divergence:")
    print(f"  step={divergence.step}")
    print(f"  metric={divergence.metric}")
    print(f"  left={divergence.left_value}")
    print(f"  right={divergence.right_value}")
    print(f"  abs_delta={divergence.absolute_delta}")
    print(f"  rel_delta={divergence.relative_delta}")
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
