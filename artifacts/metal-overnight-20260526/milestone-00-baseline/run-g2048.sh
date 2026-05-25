#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
"$SCRIPT_DIR/../bin/run-metal-benchmark.sh" milestone-00-baseline g2048 262144 64 1 1 0 \
    --seed 73 \
    --train.seed 42 \
    --vec.seed 73 \
    --vec.total-agents 1024 \
    --vec.num-buffers 2 \
    --vec.num-threads 2 \
    --policy.hidden-size 128 \
    --policy.num-layers 2 \
    --train.horizon 32 \
    --train.minibatch-size 32768 \
    --train.state-buffer-size 0 \
    --train.cl-frac 0 \
    --train.warmup-states 0
