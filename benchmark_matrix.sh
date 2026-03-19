#!/bin/bash
# benchmark matrix: 3 envs × 3 configs, ~5min each
# usage: ./benchmark_matrix.sh [label]
# output: benchmark_results_[label].txt

LABEL="${1:-baseline}"
OUT="benchmark_results_${LABEL}.txt"
echo "=== benchmark matrix: $LABEL ===" | tee "$OUT"
echo "started: $(date)" | tee -a "$OUT"

# common args
STEPS_5MIN_BREAKOUT=200000000    # breakout runs ~3M SPS = ~900M in 5min, but 200M is ~60s
STEPS_5MIN_G2048=100000000       # g2048 ~2M SPS
STEPS_5MIN_PVP=30000000          # pvp ~500K SPS

run_bench() {
    local env=$1 label=$2; shift 2
    echo "" | tee -a "$OUT"
    echo "--- $env / $label ---" | tee -a "$OUT"
    python setup.py "build_${env}" --force 2>/dev/null
    result=$(python pufferl.py train --env "$env" "$@" 2>&1 | grep -E "avg SPS|done\.")
    echo "$result" | tee -a "$OUT"
}

# breakout configs
run_bench breakout "small_hs64_L2" \
    --total-timesteps $STEPS_5MIN_BREAKOUT --hidden-size 64 --num-layers 2 \
    --total-agents 2048 --num-buffers 8 --horizon 32

run_bench breakout "medium_hs128_L2" \
    --total-timesteps $STEPS_5MIN_BREAKOUT --hidden-size 128 --num-layers 2 \
    --total-agents 2048 --num-buffers 4 --horizon 32

run_bench breakout "large_hs128_L4" \
    --total-timesteps $STEPS_5MIN_BREAKOUT --hidden-size 128 --num-layers 4 \
    --total-agents 2048 --num-buffers 4 --horizon 32

# g2048 configs
run_bench g2048 "small_hs64_L2" \
    --total-timesteps $STEPS_5MIN_G2048 --hidden-size 64 --num-layers 2 \
    --total-agents 2048 --horizon 32

run_bench g2048 "medium_hs128_L2" \
    --total-timesteps $STEPS_5MIN_G2048 --hidden-size 128 --num-layers 2 \
    --total-agents 2048 --horizon 32

run_bench g2048 "large_hs128_L4" \
    --total-timesteps $STEPS_5MIN_G2048 --hidden-size 128 --num-layers 4 \
    --total-agents 1024 --horizon 32

# osrs_pvp configs
run_bench osrs_pvp "small_hs64_L2" \
    --total-timesteps $STEPS_5MIN_PVP --hidden-size 64 --num-layers 2 \
    --total-agents 2048 --horizon 32 --replay-ratio 0.25 \
    --gamma 0.96 --learning-rate 0.001 --ent-coef 0.01 --vf-coef 0.5 --max-grad-norm 0.5

run_bench osrs_pvp "medium_hs128_L2" \
    --total-timesteps $STEPS_5MIN_PVP --hidden-size 128 --num-layers 2 \
    --total-agents 2048 --horizon 32 --replay-ratio 0.25 \
    --gamma 0.96 --learning-rate 0.001 --ent-coef 0.01 --vf-coef 0.5 --max-grad-norm 0.5

run_bench osrs_pvp "large_hs128_L4" \
    --total-timesteps $STEPS_5MIN_PVP --hidden-size 128 --num-layers 4 \
    --total-agents 1024 --horizon 32 --replay-ratio 0.25 \
    --gamma 0.96 --learning-rate 0.001 --ent-coef 0.01 --vf-coef 0.5 --max-grad-norm 0.5

echo "" | tee -a "$OUT"
echo "finished: $(date)" | tee -a "$OUT"
echo "=== done ===" | tee -a "$OUT"
