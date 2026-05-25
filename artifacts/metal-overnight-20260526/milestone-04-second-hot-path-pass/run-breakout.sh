#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
"$SCRIPT_DIR/../bin/run-metal-benchmark.sh" milestone-04-second-hot-path-pass breakout 4194304 64 1 1 0
