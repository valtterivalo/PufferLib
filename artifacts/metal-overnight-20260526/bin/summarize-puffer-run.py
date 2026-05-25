#!/usr/bin/env python3
"""Summarize one PufferLib JSON log directory."""

from __future__ import annotations

import json
import sys
from pathlib import Path


def last_metric(metrics: dict[str, object], key: str) -> object:
    """Return the last value for one metric key."""
    values = metrics.get(key)
    if not isinstance(values, list) or not values:
        return None
    return values[-1]


def main() -> None:
    """Write one compact summary JSON for an end-to-end run."""
    if len(sys.argv) != 3:
        raise SystemExit("usage: summarize-puffer-run.py LOG_DIR OUT_JSON")

    log_dir = Path(sys.argv[1])
    out_json = Path(sys.argv[2])
    log_paths = sorted(log_dir.glob("*.json"))
    if len(log_paths) != 1:
        raise RuntimeError(f"expected exactly one log JSON in {log_dir}, found {len(log_paths)}")

    payload = json.loads(log_paths[0].read_text())
    metrics = payload["metrics"]
    summary = {
        "log_path": str(log_paths[0]),
        "env_name": payload["env_name"],
        "agent_steps": last_metric(metrics, "agent_steps"),
        "sps": last_metric(metrics, "SPS"),
        "uptime": last_metric(metrics, "uptime"),
        "score": last_metric(metrics, "env/score"),
        "env_n": last_metric(metrics, "env/n"),
        "perf_rollout": last_metric(metrics, "perf/rollout"),
        "perf_train": last_metric(metrics, "perf/train"),
        "perf_train_forward": last_metric(metrics, "perf/train_forward"),
        "perf_train_backward": last_metric(metrics, "perf/train_backward"),
        "perf_train_muon": last_metric(metrics, "perf/train_muon"),
    }
    out_json.write_text(json.dumps(summary, indent=2, sort_keys=True) + "\n")


if __name__ == "__main__":
    main()
