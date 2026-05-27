#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
"$SCRIPT_DIR/../bin/run-metal-benchmark.sh" milestone-04ad-host-param-struct-reuse breakout 4194304 64 1 1 0
