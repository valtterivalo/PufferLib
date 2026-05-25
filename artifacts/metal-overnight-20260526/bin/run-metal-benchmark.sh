#!/usr/bin/env bash
set -euo pipefail

if [ "$#" -lt 7 ]; then
    echo "usage: run-metal-benchmark.sh MILESTONE ENV TOTAL_TIMESTEPS EVAL_EPISODES OVERLAP CPU_INFERENCE TRAIN_FP16 [CONFIG_OVERRIDES...]"
    exit 1
fi

MILESTONE=$1
ENV_NAME=$2
TOTAL_TIMESTEPS=$3
EVAL_EPISODES=$4
METAL_OVERLAP=$5
METAL_CPU_INFERENCE=$6
METAL_TRAIN_FP16=$7
shift 7
EXTRA_ARGS=()
if [ "$#" -gt 0 ]; then
    EXTRA_ARGS=("$@")
fi
for arg in "${EXTRA_ARGS[@]}"; do
    case "$arg" in
    --slo*)
        echo "refusing --slowly because this runner measures the native Metal backend"
        exit 1
        ;;
    esac
done

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ARTIFACT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
REPO_ROOT="$(cd "$ARTIFACT_ROOT/../.." && pwd)"
RUN_ID="$(date '+%Y%m%dT%H%M%S%z')-${ENV_NAME}"
OUT_DIR="$ARTIFACT_ROOT/$MILESTONE/$RUN_ID"
COMMAND_FILE="$OUT_DIR/commands.txt"
TRAIN_CMD=(
    "$REPO_ROOT/tools/metal/puffer-metal.py"
    --metal-overlap "$METAL_OVERLAP"
    --metal-cpu-inference "$METAL_CPU_INFERENCE"
    --metal-train-fp16 "$METAL_TRAIN_FP16"
    train "$ENV_NAME"
    --train.total-timesteps "$TOTAL_TIMESTEPS"
    --eval-episodes "$EVAL_EPISODES"
    --checkpoint-dir "$OUT_DIR/checkpoints"
    --log-dir "$OUT_DIR/logs"
    --render-mode None
)
if [ "${#EXTRA_ARGS[@]}" -gt 0 ]; then
    TRAIN_CMD+=("${EXTRA_ARGS[@]}")
fi

mkdir -p "$OUT_DIR"

{
    echo "repo=$REPO_ROOT"
    echo "pwd=$(pwd)"
    echo "git_sha=$(git -C "$REPO_ROOT" rev-parse HEAD)"
    echo "branch=$(git -C "$REPO_ROOT" branch --show-current)"
    echo "date=$(date '+%Y-%m-%d %H:%M:%S %Z %z')"
    echo "env=$ENV_NAME"
    echo "total_timesteps=$TOTAL_TIMESTEPS"
    echo "eval_episodes=$EVAL_EPISODES"
    echo "metal_overlap=$METAL_OVERLAP"
    echo "metal_cpu_inference=$METAL_CPU_INFERENCE"
    echo "metal_train_fp16=$METAL_TRAIN_FP16"
    printf 'extra_args='
    if [ "${#EXTRA_ARGS[@]}" -gt 0 ]; then
        printf '%q ' "${EXTRA_ARGS[@]}"
    fi
    printf '\n'
} > "$OUT_DIR/metadata.txt"

{
    echo "$REPO_ROOT/tools/metal/build.sh $ENV_NAME"
    printf 'PYTHONPATH=%q ' "$REPO_ROOT"
    printf '%q ' "${TRAIN_CMD[@]}"
    printf '\n'
    printf 'PYTHONPATH=%q %q --metal-overlap %q --metal-cpu-inference %q --metal-train-fp16 %q eval %q --load-model-path EXPLICIT_CHECKPOINT --eval-episodes %q --checkpoint-dir %q --log-dir %q --render-mode None ' "$REPO_ROOT" "$REPO_ROOT/tools/metal/puffer-metal.py" "$METAL_OVERLAP" "$METAL_CPU_INFERENCE" "$METAL_TRAIN_FP16" "$ENV_NAME" "$EVAL_EPISODES" "$OUT_DIR/checkpoints" "$OUT_DIR/logs"
    if [ "${#EXTRA_ARGS[@]}" -gt 0 ]; then
        printf '%q ' "${EXTRA_ARGS[@]}"
    fi
    printf '\n'
} > "$COMMAND_FILE"

cd "$REPO_ROOT"

"$REPO_ROOT/tools/metal/build.sh" "$ENV_NAME" 2>&1 | tee "$OUT_DIR/build.log"

PYTHONPATH="$REPO_ROOT" "${TRAIN_CMD[@]}" 2>&1 | tee "$OUT_DIR/train.log"

"$SCRIPT_DIR/summarize-puffer-run.py" "$OUT_DIR/logs/$ENV_NAME" "$OUT_DIR/summary.json"

CHECKPOINT_LIST="$OUT_DIR/checkpoints.txt"
find "$OUT_DIR/checkpoints/$ENV_NAME" -type f -name '*.bin' -print | sort > "$CHECKPOINT_LIST"
CHECKPOINT_COUNT=$(wc -l < "$CHECKPOINT_LIST" | tr -d ' ')
if [ "$CHECKPOINT_COUNT" = "0" ]; then
    echo "expected at least one checkpoint, found 0"
    exit 1
fi
CHECKPOINT_PATH=$(tail -n 1 "$CHECKPOINT_LIST")
echo "$CHECKPOINT_PATH" > "$OUT_DIR/selected-checkpoint.txt"

EVAL_CMD=(
    "$REPO_ROOT/tools/metal/puffer-metal.py"
    --metal-overlap "$METAL_OVERLAP"
    --metal-cpu-inference "$METAL_CPU_INFERENCE"
    --metal-train-fp16 "$METAL_TRAIN_FP16"
    eval "$ENV_NAME"
    --load-model-path "$CHECKPOINT_PATH"
    --eval-episodes "$EVAL_EPISODES"
    --checkpoint-dir "$OUT_DIR/checkpoints"
    --log-dir "$OUT_DIR/logs"
    --render-mode None
)
if [ "${#EXTRA_ARGS[@]}" -gt 0 ]; then
    EVAL_CMD+=("${EXTRA_ARGS[@]}")
fi

PYTHONPATH="$REPO_ROOT" "${EVAL_CMD[@]}" 2>&1 | tee "$OUT_DIR/eval.log"

echo "$OUT_DIR"
