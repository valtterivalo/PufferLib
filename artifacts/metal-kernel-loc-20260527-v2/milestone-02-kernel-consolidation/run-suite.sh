#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../../.." && pwd)"
ARTIFACT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
BASE_SHA=b490ce0a617b86da519323bb9759638fbaf5d484
"$SCRIPT_DIR/run-breakout.sh"
"$SCRIPT_DIR/run-g2048.sh"
"$SCRIPT_DIR/run-breakout.sh"
"$SCRIPT_DIR/run-g2048.sh"
"$SCRIPT_DIR/../bin/run-gpu-inference-smoke.sh" milestone-02-kernel-consolidation
cd "$REPO_ROOT"
PYTHONPATH="$REPO_ROOT" python tests/metal/test_native_backend_parity.py --backend metal --write-json "$SCRIPT_DIR/native-metal.json"
METAL_OVERLAY_BASE_REF="$BASE_SHA" METAL_OVERLAY_BASE_SHA="$BASE_SHA" PYTHONPATH="$REPO_ROOT" python -m pytest tests/metal/test_overlay_surface.py
"$SCRIPT_DIR/../bin/summarize-milestone.py" "$SCRIPT_DIR"
"$SCRIPT_DIR/../bin/check-median-gate.py" "$ARTIFACT_ROOT/milestone-00-baseline" "$SCRIPT_DIR" loc
