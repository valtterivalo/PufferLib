#!/usr/bin/env python3
"""Summarize repeated benchmark runs in one milestone folder."""

from __future__ import annotations

import json
import statistics
import sys
from pathlib import Path


def metadata(path: Path) -> dict[str, str]:
    """Read metadata key-value lines."""
    out = {}
    for line in path.read_text().splitlines():
        if "=" in line:
            key, value = line.split("=", 1)
            out[key] = value
    return out


def eval_score(payload: dict[str, object]) -> float:
    """Read eval score from normalized or raw Puffer eval JSON."""
    if "score" in payload:
        return float(payload["score"])
    return float(payload["env/score"])


def latest_cohort(records: list[dict[str, object]]) -> list[dict[str, object]]:
    """Keep records from the latest git SHA and dirty-diff cohort."""
    if not records:
        return []
    latest = records[-1]
    key = (latest["git_sha"], latest["git_diff_sha256"])
    return [
        record
        for record in records
        if (record["git_sha"], record["git_diff_sha256"]) == key
    ]


def main() -> None:
    """Write milestone-summary.json next to the runner scripts."""
    if len(sys.argv) != 2:
        raise SystemExit("usage: summarize-milestone.py MILESTONE_DIR")

    milestone_dir = Path(sys.argv[1])
    envs: dict[str, list[dict[str, object]]] = {"breakout": [], "g2048": []}
    for env_name in envs:
        for summary_path in sorted(milestone_dir.glob(f"*-{env_name}/summary.json")):
            summary = json.loads(summary_path.read_text())
            run_meta = metadata(summary_path.parent / "metadata.txt")
            eval_summary = json.loads((summary_path.parent / "eval-summary.json").read_text())
            envs[env_name].append(
                {
                    "run": summary_path.parent.name,
                    "sps": summary["sps"],
                    "score": summary["score"],
                    "eval_score": eval_score(eval_summary),
                    "uptime": summary["uptime"],
                    "git_sha": run_meta["git_sha"],
                    "git_diff_sha256": run_meta["git_diff_sha256"],
                    "metal_cpu_inference": run_meta["metal_cpu_inference"],
                    "metal_train_fp16": run_meta["metal_train_fp16"],
                }
            )

    out = {"envs": {}}
    for env_name, records in envs.items():
        main_records = [
            record
            for record in latest_cohort(records)
            if record["metal_cpu_inference"] == "1" and record["metal_train_fp16"] == "0"
        ]
        out["envs"][env_name] = {
            "runs": records,
            "latest_main_run_count": len(main_records),
            "latest_main_sps_median": statistics.median(record["sps"] for record in main_records)
            if main_records
            else None,
            "latest_main_score_median": statistics.median(record["score"] for record in main_records)
            if main_records
            else None,
            "latest_main_eval_score_median": statistics.median(record["eval_score"] for record in main_records)
            if main_records
            else None,
        }

    (milestone_dir / "milestone-summary.json").write_text(json.dumps(out, indent=2, sort_keys=True) + "\n")


if __name__ == "__main__":
    main()
