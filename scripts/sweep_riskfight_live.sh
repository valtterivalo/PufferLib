#!/bin/bash
# Protein sweep of the live-population objective: kills minus price x deaths per hour of the final checkpoint against the pool kits played by the script
# and OppFit (selfplay/bot_ladder_perf, config/osrs_riskfight.ini [bot_eval]); self-play opponents never enter the score.
#   cd REPO && ./build.sh osrs_riskfight && RF_OPPFIT_MODEL=... RF_OPPFIT_EAT=... scripts/sweep_riskfight_live.sh RUN_DIR
# Searched ([sweep.*] of the ini): total_timesteps, learning rate, gamma, gae_lambda, ent_coef, clip_coef, vf_coef, vf_clip_coef, horizon, minibatch, hidden size,
# layers, selfplay.enabled (0: one policy, env.self_play 0, the learner only meets the script, OppFit, pool kits and raggers; the trainer sets that switch),
# scripted_probability, selfplay_pool_probability, hist_policy_percent.
# Fixed here: session_ticks 3000, death_price 0 (drawn per fight), no reward shaping, deployable, motor, live_loadout 3, domain randomization, the pool and
# OppFit (0.5) for the opponents the script plays, 8 env threads.
# Trial 0 is the anchor below: trial 29 of the first live sweep (20261017-riskfight-live-sweep, 3.94 per hour), whose ranges were then widened where its top
# quartile sat on an edge; the rest are Protein's. max_runs and max_suggestion_cost
# come from the ini. A killed sweep resumes from RUN_DIR through --sweep.resume_dir (see src/sweep_resume.h).
set -euo pipefail
RUN=${1:?usage: sweep_riskfight_live.sh RUN_DIR}
: "${RF_OPPFIT_MODEL:?RF_OPPFIT_MODEL}" "${RF_OPPFIT_EAT:?RF_OPPFIT_EAT}"
mkdir -p "$RUN/checkpoints" "$RUN/logs"
exec ./puffer sweep \
  --base.seed=73 --base.checkpoint_dir="$RUN/checkpoints" --base.log_dir="$RUN/logs" --base.checkpoint_interval=128 --base.eval_episodes=0 \
  --env.live_loadout=3 --env.deployable=1 --env.motor=1 --env.domain_randomization=1 --env.opponent_pool=1 --env.oppfit_probability=0.5 \
  --env.death_price=0 --env.damage_reward_coeff=0 --env.chance_reward_coeff=0 --env.session_ticks=3000 \
  --selfplay.enabled=0 --vec.num_policies=2 --vec.total_agents=8192 --vec.num_threads=8 \
  --train.replay_ratio=3.47172999 --train.max_grad_norm=1.5 --train.momentum=0.993514776 \
  --policy.hidden_size=256 --policy.num_layers=2 --train.total_timesteps=72323768 --train.horizon=32 --train.minibatch_size=2048 \
  --train.learning_rate=0.000171211403 --train.ent_coef=1.37824545e-05 --train.gamma=0.99 --train.gae_lambda=0.5 \
  --train.clip_coef=0.5 --train.vf_coef=0.928761363 --train.vf_clip_coef=0.179643989 \
  --env.scripted_probability=0.211089134 --env.selfplay_pool_probability=0.99 --vec.hist_policy_percent=0.5
