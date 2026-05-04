#!/bin/bash
# Stage R1 variant sweeps. Run AFTER inferno_reward_current_widened is complete
# (it's 80 trials, ~4h on M4 Pro). This runs v1, v2, v3 sequentially: 60 trials
# each, ~3h each, ~9h total. Tags: stage-r1-v{1,2,3}-{progress,milestones,rebalanced}.
#
# Usage:
#   bash experiments/heavy_research_phase2_v4/run_variants_after_control.sh

set -e
REPO=/Users/valtterivalo/Projects/pufferlib-metal-inferno-sync
SWEEPS=$REPO/experiments/heavy_research_phase2_v4/sweeps
PROPOSAL=$REPO/checkpoints/osrs_inferno/cdevk9pk/0000000029982720.bin
export PUFFER_SWEEP_WORKER_TIMEOUT=300

cd $REPO

run_arm() {
    local config=$1
    local tag=$2
    local logdir=$3
    local logfile=$4
    echo "=== $(date) starting $tag ==="
    PUFFER_CONFIG_FILE=$SWEEPS/$config python -m pufferlib.pufferl sweep osrs_inferno \
        --load-model-path "$PROPOSAL" \
        --log-dir logs/$logdir \
        --wandb \
        --tag $tag \
        > /tmp/$logfile 2>&1
    echo "=== $(date) $tag done ==="
    tail -5 /tmp/$logfile
}

run_arm inferno_reward_v1_progress.ini    stage-r1-v1-progress    r1_v1_progress    sweep_r1_v1.log
run_arm inferno_reward_v2_milestones.ini  stage-r1-v2-milestones  r1_v2_milestones  sweep_r1_v2.log
run_arm inferno_reward_v3_rebalanced.ini  stage-r1-v3-rebalanced  r1_v3_rebalanced  sweep_r1_v3.log

echo "=== $(date) all variant sweeps done ==="
