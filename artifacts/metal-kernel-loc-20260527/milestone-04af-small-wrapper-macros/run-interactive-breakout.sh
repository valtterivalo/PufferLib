#!/usr/bin/env bash
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
cd "$REPO_ROOT"
tools/metal/build.sh breakout
PYTHONPATH="$REPO_ROOT" tools/metal/puffer-metal.py --metal-overlap 1 --metal-cpu-inference 1 --metal-train-fp16 0 eval breakout --load-model-path resources/breakout/breakout_weights.bin --eval-episodes 1
