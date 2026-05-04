#!/bin/bash
# Watch the control sweep (PID arg, default 51495). When it exits, kick off
# the variant chain (v1, v2, v3 sequentially). Log to /tmp/autochain.log.
# Detach with: nohup bash autochain.sh > /tmp/autochain.log 2>&1 &

CTRL_PID=${1:-51495}
LOG=/tmp/autochain.log
REPO=/Users/valtterivalo/Projects/pufferlib-metal-inferno-sync
VARIANT_SCRIPT=$REPO/experiments/heavy_research_phase2_v4/run_variants_after_control.sh

echo "=== $(date) autochain starting; watching PID=$CTRL_PID ===" | tee -a $LOG

while kill -0 $CTRL_PID 2>/dev/null; do
    sleep 60
done

echo "=== $(date) control PID $CTRL_PID exited; verifying finished count ===" | tee -a $LOG

cd $REPO && python -c "
import sys
sys.argv = ['x']
import wandb
api = wandb.Api()
runs = list(api.runs('valtterivalo-clock-cloud/puffer4',
    filters={'tags': 'stage-r1-current-widened', 'state': 'finished'}))
print(f'finished count: {len(runs)}')
" 2>&1 | tee -a $LOG

echo "=== $(date) launching variant chain ===" | tee -a $LOG
bash $VARIANT_SCRIPT >> $LOG 2>&1
echo "=== $(date) variant chain done ===" | tee -a $LOG
