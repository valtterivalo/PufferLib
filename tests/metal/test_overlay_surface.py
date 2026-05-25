from __future__ import annotations

import subprocess
from pathlib import Path


ALLOWED_PREFIXES = (
    "src/metal/",
    "tools/metal/",
    "tests/metal/",
)

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
    committed = git_lines("diff", "--name-only", "upstream/5.0...HEAD")
    unstaged = git_lines("diff", "--name-only", "upstream/5.0", "--")
    untracked = git_lines("ls-files", "--others", "--exclude-standard")
    return committed | unstaged | untracked


def test_metal_overlay_does_not_touch_upstream_core() -> None:
    root = Path.cwd()
    assert (root / ".git").exists() or (root / ".git").is_file()
    bad_paths = sorted(
        path
        for path in changed_paths()
        if path in BLOCKED_PATHS
        or not any(path.startswith(prefix) for prefix in ALLOWED_PREFIXES)
    )
    assert bad_paths == []
