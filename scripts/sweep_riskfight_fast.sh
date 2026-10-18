#!/bin/bash
# Short Protein sweep for active iteration (the user's rule, 2026-10-18: while the observation, actions and sim still change, sweeps answer a specific question fast;
# they are not the final hyperparameter search). Same objective, env and anchor as scripts/sweep_riskfight_live.sh, plus:
#   at most RUNS trials (default 6: about 20 minutes on one GPU), each costing at most 240 s (max_suggestion_cost),
#   a narrow search space around the anchor (total_timesteps 5e7..2.2e8, nets 256..512 x 1..2, the PPO knobs near their good region).
#   cd REPO && ./build.sh osrs_riskfight && RF_OPPFIT_MODEL=... RF_OPPFIT_EAT=... scripts/sweep_riskfight_fast.sh RUN_DIR [RUNS] [more ./puffer sweep args]
set -euo pipefail
RUN=${1:?usage: sweep_riskfight_fast.sh RUN_DIR [RUNS] [more ./puffer sweep args]}
RUNS=${2:-6}
exec "$(dirname "$0")/sweep_riskfight_live.sh" "$RUN" \
  --sweep.max_runs="$RUNS" --sweep.max_suggestion_cost=240 \
  --sweep.train.total_timesteps.min=5e7 --sweep.train.total_timesteps.max=2.2e8 \
  --sweep.policy.hidden_size.min=256 --sweep.policy.hidden_size.max=512 \
  --sweep.policy.num_layers.min=1 --sweep.policy.num_layers.max=2 \
  --sweep.train.horizon.min=32 --sweep.train.horizon.max=64 \
  --sweep.train.minibatch_size.min=1024 --sweep.train.minibatch_size.max=4096 \
  --sweep.train.learning_rate.min=0.0003 --sweep.train.learning_rate.max=0.002 \
  --sweep.train.ent_coef.min=1e-05 --sweep.train.ent_coef.max=0.003 \
  --sweep.train.gamma.min=0.995 --sweep.train.gamma.max=0.9995 \
  --sweep.train.gae_lambda.min=0.85 --sweep.train.gae_lambda.max=0.95 \
  --sweep.train.clip_coef.min=0.2 --sweep.train.clip_coef.max=0.9 \
  --sweep.train.max_grad_norm.min=0.3 --sweep.train.max_grad_norm.max=1 \
  --sweep.train.vf_coef.min=1 --sweep.train.vf_coef.max=3 \
  --sweep.train.vf_clip_coef.min=0.05 --sweep.train.vf_clip_coef.max=0.3 \
  --sweep.env.scripted_probability.min=0 --sweep.env.scripted_probability.max=0.2 \
  --sweep.env.selfplay_pool_probability.min=0.8 --sweep.env.selfplay_pool_probability.max=1 \
  --sweep.vec.hist_policy_percent.min=0.3 --sweep.vec.hist_policy_percent.max=0.7 "${@:3}"
