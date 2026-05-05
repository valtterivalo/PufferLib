#!/bin/bash
# Run E0/E1/E2/E3 oracle wrapper evals back to back.
# Heavy agent r4 step 1.

set -e

REPO_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$REPO_ROOT"

CKPT="checkpoints/osrs_inferno/p2k4szzs/0000000049971200.bin"
OUT_DIR="experiments/heavy_research_phase2_v4/oracle_eval"
TARGET_EPS="${TARGET_EPS:-20000}"

mkdir -p "$OUT_DIR"

for MODE in 0 1 2 3; do
    LABEL="E$MODE"
    OUT="$OUT_DIR/${LABEL}.json"
    LOG="$OUT_DIR/${LABEL}.log"
    echo "=== $LABEL  oracle_mode=$MODE  target=$TARGET_EPS ==="
    python experiments/heavy_research_phase2_v4/run_oracle_eval.py \
        --checkpoint "$CKPT" \
        --oracle-mode "$MODE" \
        --target-episodes "$TARGET_EPS" \
        --output "$OUT" 2>&1 | tee "$LOG"
done

echo "=== all 4 arms complete ==="
ls -la "$OUT_DIR"
