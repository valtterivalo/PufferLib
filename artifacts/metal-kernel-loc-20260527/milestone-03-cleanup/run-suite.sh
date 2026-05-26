#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../../.." && pwd)"
"$SCRIPT_DIR/run-breakout.sh"
"$SCRIPT_DIR/run-g2048.sh"
"$SCRIPT_DIR/../bin/run-interactive-smoke.sh" milestone-03-cleanup cleanup
cd "$REPO_ROOT"
PYTHONPATH="$REPO_ROOT" python tests/metal/test_native_backend_parity.py --backend metal --write-json "$SCRIPT_DIR/native-metal.json"
PYTHONPATH="$REPO_ROOT" python -m pytest tests/metal/test_overlay_surface.py
