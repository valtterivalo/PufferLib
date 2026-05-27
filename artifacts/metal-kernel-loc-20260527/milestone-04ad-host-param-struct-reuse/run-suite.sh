#!/usr/bin/env bash
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
cd "$REPO_ROOT"

"$REPO_ROOT/artifacts/metal-kernel-loc-20260527/milestone-04ad-host-param-struct-reuse/run-breakout.sh"
"$REPO_ROOT/artifacts/metal-kernel-loc-20260527/milestone-04ad-host-param-struct-reuse/run-g2048.sh"
PYTHONDONTWRITEBYTECODE=1 PYTHONPATH="$REPO_ROOT" python -m pytest -p no:cacheprovider tests/metal/test_overlay_surface.py
