#!/usr/bin/env python3
"""Check repeated benchmark medians against an accepted baseline."""

from __future__ import annotations

import json
import statistics
import sys
from pathlib import Path


def metadata_value(path: Path, key: str) -> str:
    """Read one key from benchmark metadata."""
    for line in path.read_text().splitlines():
        if line.startswith(f"{key}="):
            return line.split("=", 1)[1]
    raise RuntimeError(f"missing {key} in {path}")


def run_records(milestone_dir: Path, env_name: str) -> list[dict[str, object]]:
    """Load main CPU-overlap train/eval summaries for one env."""
    records: list[dict[str, object]] = []
    for summary_path in sorted(milestone_dir.glob(f"*-{env_name}/summary.json")):
        metadata_path = summary_path.parent / "metadata.txt"
        if metadata_value(metadata_path, "metal_cpu_inference") != "1":
            continue
        if metadata_value(metadata_path, "metal_train_fp16") != "0":
            continue
        summary = json.loads(summary_path.read_text())
        eval_summary = json.loads((summary_path.parent / "eval-summary.json").read_text())
        records.append(
            {
                "run": summary_path.parent.name,
                "git_sha": metadata_value(metadata_path, "git_sha"),
                "git_diff_sha256": metadata_value(metadata_path, "git_diff_sha256"),
                "sps": float(summary["sps"]),
                "score": float(summary["score"]),
                "eval_score": float(eval_summary["score"]),
                "uptime": float(summary["uptime"]),
            }
        )
    return records


def latest_cohort(records: list[dict[str, object]], env_name: str, milestone_dir: Path) -> list[dict[str, object]]:
    """Keep only records from the latest git SHA and dirty-diff cohort."""
    if not records:
        raise RuntimeError(f"{milestone_dir} has no main {env_name} runs")
    latest = records[-1]
    key = (latest["git_sha"], latest["git_diff_sha256"])
    return [
        record
        for record in records
        if (record["git_sha"], record["git_diff_sha256"]) == key
    ]


def median(records: list[dict[str, object]], key: str) -> float:
    """Return the median for a numeric record key."""
    return float(statistics.median(float(record[key]) for record in records))


def main() -> None:
    """Check LOC or speed acceptance medians for breakout and g2048."""
    if len(sys.argv) != 4:
        raise SystemExit("usage: check-median-gate.py BASELINE_DIR CANDIDATE_DIR loc|speed")

    baseline_dir = Path(sys.argv[1])
    candidate_dir = Path(sys.argv[2])
    mode = sys.argv[3]
    if mode not in {"loc", "speed"}:
        raise SystemExit("mode must be loc or speed")

    factor = 0.99 if mode == "loc" else 1.03
    score_floor_factor = 0.80
    eval_floor_factor = 0.95
    result: dict[str, object] = {
        "mode": mode,
        "factor": factor,
        "score_floor_factor": score_floor_factor,
        "eval_floor_factor": eval_floor_factor,
        "envs": {},
    }
    failures: list[str] = []

    for env_name in ("breakout", "g2048"):
        baseline = latest_cohort(run_records(baseline_dir, env_name), env_name, baseline_dir)
        candidate = latest_cohort(run_records(candidate_dir, env_name), env_name, candidate_dir)
        if len(baseline) < 2:
            raise RuntimeError(f"{baseline_dir} has {len(baseline)} main {env_name} runs, expected at least 2")
        if len(candidate) < 2:
            raise RuntimeError(f"{candidate_dir} has {len(candidate)} main {env_name} runs, expected at least 2")

        baseline_sps = median(baseline, "sps")
        candidate_sps = median(candidate, "sps")
        baseline_score = median(baseline, "score")
        candidate_score = median(candidate, "score")
        baseline_eval_score = median(baseline, "eval_score")
        candidate_eval_score = median(candidate, "eval_score")
        floor = baseline_sps * factor
        score_floor = baseline_score * score_floor_factor
        eval_floor = baseline_eval_score * eval_floor_factor if baseline_eval_score > 0.0 else baseline_eval_score
        passed_sps = candidate_sps >= floor
        passed_score = candidate_score >= score_floor
        passed_eval = candidate_eval_score >= eval_floor
        result["envs"][env_name] = {
            "baseline_runs": len(baseline),
            "candidate_runs": len(candidate),
            "baseline_git_sha": baseline[-1]["git_sha"],
            "candidate_git_sha": candidate[-1]["git_sha"],
            "baseline_git_diff_sha256": baseline[-1]["git_diff_sha256"],
            "candidate_git_diff_sha256": candidate[-1]["git_diff_sha256"],
            "baseline_sps_median": baseline_sps,
            "candidate_sps_median": candidate_sps,
            "required_sps": floor,
            "candidate_vs_baseline": (candidate_sps / baseline_sps) - 1.0,
            "baseline_score_median": baseline_score,
            "candidate_score_median": candidate_score,
            "required_score": score_floor,
            "baseline_eval_score_median": baseline_eval_score,
            "candidate_eval_score_median": candidate_eval_score,
            "required_eval_score": eval_floor,
            "passed_sps_gate": passed_sps,
            "passed_score_gate": passed_score,
            "passed_eval_gate": passed_eval,
        }
        if not passed_sps:
            failures.append(
                f"{env_name} median SPS {candidate_sps:.0f} below required {floor:.0f} "
                f"from baseline {baseline_sps:.0f}"
            )
        if not passed_score:
            failures.append(
                f"{env_name} train score median {candidate_score:.3f} below collapse floor "
                f"{score_floor:.3f} from baseline {baseline_score:.3f}"
            )
        if not passed_eval:
            failures.append(
                f"{env_name} eval score median {candidate_eval_score:.3f} below comparable floor "
                f"{eval_floor:.3f} from baseline {baseline_eval_score:.3f}"
            )

    out_path = candidate_dir / "median-gate.json"
    out_path.write_text(json.dumps(result, indent=2, sort_keys=True) + "\n")
    if failures:
        raise RuntimeError("\n".join(failures))


if __name__ == "__main__":
    main()
