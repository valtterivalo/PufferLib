#!/bin/bash
# Full oracle wrapper eval (heavy agent r5 step 1).
# E0 + E4-E8 covers raw / target-only / +overhead / +gear+offensive / full / full@300.

set -e

REPO_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$REPO_ROOT"

CKPT="checkpoints/osrs_inferno/p2k4szzs/0000000049971200.bin"
OUT_DIR="experiments/heavy_research_phase2_v4/oracle_eval_v2"
TARGET_EPS="${TARGET_EPS:-20000}"

mkdir -p "$OUT_DIR"

for MODE in 0 4 5 6 7 8; do
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

echo "=== all 6 arms complete ==="
ls -la "$OUT_DIR"
