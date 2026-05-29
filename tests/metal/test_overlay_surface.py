from __future__ import annotations

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
}

BLOCKED_PATHS = {
    "build.sh",
    "config/default.ini",
    "src/curriculum.cu",
    "src/vecenv.h",
}

BACKEND_PARITY_CORE_PATHS = {
    "pufferlib/pufferl.py",
    "src/bindings.cu",
    "src/pufferlib.cu",
}


def git_lines(*args: str) -> set[str]:
    output = subprocess.check_output(("git", *args), text=True)
    return {line.strip() for line in output.splitlines() if line.strip()}


def changed_paths() -> set[str]:
    staged = git_lines("diff", "--cached", "--name-only")
    unstaged = git_lines("diff", "--name-only")
    untracked = git_lines("ls-files", "--others", "--exclude-standard")
    return staged | unstaged | untracked


def test_metal_overlay_does_not_touch_upstream_core() -> None:
    root = Path.cwd()
    assert (root / ".git").exists() or (root / ".git").is_file()
    bad_paths = sorted(
        path
        for path in changed_paths()
        if path in BLOCKED_PATHS
        or (
            path not in BACKEND_PARITY_CORE_PATHS
            and path not in ALLOWED_ARTIFACT_PATHS
            and not any(path.startswith(prefix) for prefix in ALLOWED_PREFIXES)
        )
    )
    assert bad_paths == []
