#!/usr/bin/env bash
set -euo pipefail

if [ "$#" -ne 2 ]; then
    echo "usage: run-interactive-smoke.sh MILESTONE LABEL"
    exit 1
fi

MILESTONE=$1
LABEL=$2
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ARTIFACT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
REPO_ROOT="$(cd "$ARTIFACT_ROOT/../.." && pwd)"
RUN_ID="$(date '+%Y%m%dT%H%M%S%z')-interactive-breakout-${LABEL}"
OUT_DIR="$ARTIFACT_ROOT/$MILESTONE/$RUN_ID"
mkdir -p "$OUT_DIR"

{
    echo "repo=$REPO_ROOT"
    echo "pwd=$(pwd)"
    echo "git_sha=$(git -C "$REPO_ROOT" rev-parse HEAD)"
    echo "branch=$(git -C "$REPO_ROOT" branch --show-current)"
    echo "date=$(date '+%Y-%m-%d %H:%M:%S %Z %z')"
} > "$OUT_DIR/metadata.txt"
git -C "$REPO_ROOT" status --short --untracked-files=all > "$OUT_DIR/git-status.txt"
git -C "$REPO_ROOT" diff --binary HEAD -- > "$OUT_DIR/git-diff.patch"
echo "$ARTIFACT_ROOT/$MILESTONE/run-interactive-breakout.sh" > "$OUT_DIR/command.txt"

set +e
"$ARTIFACT_ROOT/$MILESTONE/run-interactive-breakout.sh" > "$OUT_DIR/output.log" 2>&1
code=$?
set -e
echo "$code" > "$OUT_DIR/exit-code.txt"
if [ "$code" -eq 0 ]; then
    echo "$OUT_DIR"
    exit 0
fi

if grep -q "Failed to determine Monitor to center Window" "$OUT_DIR/output.log"; then
    echo "$OUT_DIR"
    exit 0
fi

echo "interactive smoke failed with unexpected exit code $code"
echo "$OUT_DIR"
exit "$code"

