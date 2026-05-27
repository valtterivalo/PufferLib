#!/usr/bin/env bash
set -euo pipefail

if [ "$#" -ne 1 ]; then
    echo "usage: run-gpu-inference-smoke.sh MILESTONE"
    exit 1
fi

MILESTONE=$1
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
"$SCRIPT_DIR/run-metal-benchmark.sh" "$MILESTONE" breakout 1048576 8 1 0 0
"$SCRIPT_DIR/run-metal-benchmark.sh" "$MILESTONE" g2048 262144 8 1 0 0 \
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
