#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
"$SCRIPT_DIR/../bin/run-metal-benchmark.sh" milestone-04af-small-wrapper-macros breakout 4194304 64 1 1 0
