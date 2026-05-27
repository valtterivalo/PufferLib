from __future__ import annotations

import os
import subprocess
from pathlib import Path


ALLOWED_PREFIXES = (
    "src/metal/",
    "tools/metal/",
    "tests/metal/",
)

ALLOWED_ARTIFACT_PATHS = {
    "artifacts/metal-overnight-20260526/.gitignore",
    "artifacts/metal-overnight-20260526/PLAN.md",
    "artifacts/metal-overnight-20260526/STATUS.md",
    "artifacts/metal-overnight-20260526/bin/run-metal-benchmark.sh",
    "artifacts/metal-overnight-20260526/bin/summarize-puffer-run.py",
    "artifacts/metal-overnight-20260526/milestone-00-baseline/run-breakout.sh",
    "artifacts/metal-overnight-20260526/milestone-00-baseline/run-g2048.sh",
    "artifacts/metal-overnight-20260526/milestone-00-baseline/run-suite.sh",
    "artifacts/metal-overnight-20260526/milestone-01-loc-pass/run-breakout.sh",
    "artifacts/metal-overnight-20260526/milestone-01-loc-pass/run-g2048.sh",
    "artifacts/metal-overnight-20260526/milestone-01-loc-pass/run-suite.sh",
    "artifacts/metal-overnight-20260526/milestone-02-hot-path-pass/run-breakout.sh",
    "artifacts/metal-overnight-20260526/milestone-02-hot-path-pass/run-g2048.sh",
    "artifacts/metal-overnight-20260526/milestone-02-hot-path-pass/run-suite.sh",
    "artifacts/metal-overnight-20260526/milestone-03-cleanup/run-breakout.sh",
    "artifacts/metal-overnight-20260526/milestone-03-cleanup/run-g2048.sh",
    "artifacts/metal-overnight-20260526/milestone-03-cleanup/run-interactive-breakout.sh",
    "artifacts/metal-overnight-20260526/milestone-03-cleanup/run-suite.sh",
    "artifacts/metal-overnight-20260526/milestone-04-second-hot-path-pass/run-breakout.sh",
    "artifacts/metal-overnight-20260526/milestone-04-second-hot-path-pass/run-g2048.sh",
    "artifacts/metal-overnight-20260526/milestone-04-second-hot-path-pass/run-suite.sh",
    "artifacts/metal-overnight-20260526/milestone-05-final-audit/run-breakout.sh",
    "artifacts/metal-overnight-20260526/milestone-05-final-audit/run-g2048.sh",
    "artifacts/metal-overnight-20260526/milestone-05-final-audit/run-interactive-breakout.sh",
    "artifacts/metal-overnight-20260526/milestone-05-final-audit/run-suite.sh",
    "artifacts/metal-kernel-loc-20260527-v2/.gitignore",
    "artifacts/metal-kernel-loc-20260527-v2/PLAN.md",
    "artifacts/metal-kernel-loc-20260527-v2/STATUS.md",
    "artifacts/metal-kernel-loc-20260527-v2/bin/run-interactive-smoke.sh",
    "artifacts/metal-kernel-loc-20260527-v2/bin/run-gpu-inference-smoke.sh",
    "artifacts/metal-kernel-loc-20260527-v2/bin/run-metal-benchmark.sh",
    "artifacts/metal-kernel-loc-20260527-v2/bin/check-median-gate.py",
    "artifacts/metal-kernel-loc-20260527-v2/bin/summarize-milestone.py",
    "artifacts/metal-kernel-loc-20260527-v2/bin/summarize-puffer-run.py",
    "artifacts/metal-kernel-loc-20260527-v2/milestone-00-baseline/run-breakout.sh",
    "artifacts/metal-kernel-loc-20260527-v2/milestone-00-baseline/run-g2048.sh",
    "artifacts/metal-kernel-loc-20260527-v2/milestone-00-baseline/run-suite.sh",
    "artifacts/metal-kernel-loc-20260527-v2/milestone-01-port-v1-kernel-loc/run-breakout.sh",
    "artifacts/metal-kernel-loc-20260527-v2/milestone-01-port-v1-kernel-loc/run-g2048.sh",
    "artifacts/metal-kernel-loc-20260527-v2/milestone-01-port-v1-kernel-loc/run-interactive-breakout.sh",
    "artifacts/metal-kernel-loc-20260527-v2/milestone-01-port-v1-kernel-loc/run-suite.sh",
    "artifacts/metal-kernel-loc-20260527-v2/milestone-02-kernel-consolidation/run-breakout.sh",
    "artifacts/metal-kernel-loc-20260527-v2/milestone-02-kernel-consolidation/run-g2048.sh",
    "artifacts/metal-kernel-loc-20260527-v2/milestone-02-kernel-consolidation/run-interactive-breakout.sh",
    "artifacts/metal-kernel-loc-20260527-v2/milestone-02-kernel-consolidation/run-suite.sh",
    "artifacts/metal-kernel-loc-20260527-v2/milestone-03-cleanup/run-breakout.sh",
    "artifacts/metal-kernel-loc-20260527-v2/milestone-03-cleanup/run-g2048.sh",
    "artifacts/metal-kernel-loc-20260527-v2/milestone-03-cleanup/run-interactive-breakout.sh",
    "artifacts/metal-kernel-loc-20260527-v2/milestone-03-cleanup/run-suite.sh",
    "artifacts/metal-kernel-loc-20260527-v2/milestone-04-second-kernel-loc/run-breakout.sh",
    "artifacts/metal-kernel-loc-20260527-v2/milestone-04-second-kernel-loc/run-g2048.sh",
    "artifacts/metal-kernel-loc-20260527-v2/milestone-04-second-kernel-loc/run-interactive-breakout.sh",
    "artifacts/metal-kernel-loc-20260527-v2/milestone-04-second-kernel-loc/run-suite.sh",
    "artifacts/metal-kernel-loc-20260527-v2/milestone-05-final-audit/run-breakout.sh",
    "artifacts/metal-kernel-loc-20260527-v2/milestone-05-final-audit/run-g2048.sh",
    "artifacts/metal-kernel-loc-20260527-v2/milestone-05-final-audit/run-interactive-breakout.sh",
    "artifacts/metal-kernel-loc-20260527-v2/milestone-05-final-audit/run-suite.sh",
    "artifacts/metal-kernel-loc-20260527-v2/milestone-06-dead-prototypes/run-breakout.sh",
    "artifacts/metal-kernel-loc-20260527-v2/milestone-06-dead-prototypes/run-g2048.sh",
    "artifacts/metal-kernel-loc-20260527-v2/milestone-06-dead-prototypes/run-interactive-breakout.sh",
    "artifacts/metal-kernel-loc-20260527-v2/milestone-06-dead-prototypes/run-suite.sh",
    "artifacts/metal-kernel-loc-20260527-v2/milestone-07-host-param-format/run-breakout.sh",
    "artifacts/metal-kernel-loc-20260527-v2/milestone-07-host-param-format/run-g2048.sh",
    "artifacts/metal-kernel-loc-20260527-v2/milestone-07-host-param-format/run-interactive-breakout.sh",
    "artifacts/metal-kernel-loc-20260527-v2/milestone-07-host-param-format/run-suite.sh",
}

BLOCKED_PATHS = {
    "build.sh",
    "config/default.ini",
    "pufferlib/pufferl.py",
    "src/bindings.cu",
    "src/curriculum.cu",
    "src/pufferlib.cu",
    "src/vecenv.h",
}


def git_lines(*args: str) -> set[str]:
    output = subprocess.check_output(("git", *args), text=True)
    return {line.strip() for line in output.splitlines() if line.strip()}


def changed_paths() -> set[str]:
    base_ref = os.environ.get("METAL_OVERLAY_BASE_REF", "upstream/5.0")
    expected_sha = os.environ.get("METAL_OVERLAY_BASE_SHA")
    if expected_sha is not None:
        actual_sha = subprocess.check_output(
            ("git", "rev-parse", base_ref),
            text=True,
        ).strip()
        assert actual_sha == expected_sha
    committed = git_lines("diff", "--name-only", f"{base_ref}...HEAD")
    unstaged = git_lines("diff", "--name-only", base_ref, "--")
    untracked = git_lines("ls-files", "--others", "--exclude-standard")
    return committed | unstaged | untracked


def test_metal_overlay_does_not_touch_upstream_core() -> None:
    root = Path.cwd()
    assert (root / ".git").exists() or (root / ".git").is_file()
    bad_paths = sorted(
        path
        for path in changed_paths()
        if path in BLOCKED_PATHS
        or (
            path not in ALLOWED_ARTIFACT_PATHS
            and not any(path.startswith(prefix) for prefix in ALLOWED_PREFIXES)
        )
    )
    assert bad_paths == []
