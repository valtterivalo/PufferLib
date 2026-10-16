#!/bin/bash
# Protein sweep of the live-population objective: kills minus price x deaths per hour of the final checkpoint against the pool kits played by the script
# and OppFit (selfplay/bot_ladder_perf, config/osrs_riskfight.ini [bot_eval]); self-play opponents never enter the score.
#   cd REPO && ./build.sh osrs_riskfight && RF_OPPFIT_MODEL=... RF_OPPFIT_EAT=... scripts/sweep_riskfight_live.sh RUN_DIR
# Searched ([sweep.*] of the ini): total_timesteps, learning rate, gamma, gae_lambda, ent_coef, clip_coef, vf_coef, vf_clip_coef, horizon, minibatch, hidden size,
# layers, selfplay.enabled (0: one policy, env.self_play 0, the learner only meets the script, OppFit, pool kits and raggers; the trainer sets that switch),
# scripted_probability, selfplay_pool_probability, hist_policy_percent.
# Fixed here: session_ticks 3000, death_price 0 (drawn per fight), no reward shaping, deployable, motor, live_loadout 3, domain randomization, the pool and
# OppFit (0.5) for the opponents the script plays, 1024 agents, replay_ratio / max_grad_norm / momentum of the anchor.
# Trial 0 is the anchor below (the integrate-1 100M-step run, every value inside its sweep range); the rest are Protein's. max_runs and max_suggestion_cost
# come from the ini. A killed sweep resumes from RUN_DIR through --sweep.resume_dir (see src/sweep_resume.h).
set -euo pipefail
RUN=${1:?usage: sweep_riskfight_live.sh RUN_DIR}
: "${RF_OPPFIT_MODEL:?RF_OPPFIT_MODEL}" "${RF_OPPFIT_EAT:?RF_OPPFIT_EAT}"
mkdir -p "$RUN/checkpoints" "$RUN/logs"
exec ./puffer sweep \
  --base.seed=73 --base.checkpoint_dir="$RUN/checkpoints" --base.log_dir="$RUN/logs" --base.checkpoint_interval=128 --base.eval_episodes=0 \
  --env.live_loadout=3 --env.deployable=1 --env.motor=1 --env.domain_randomization=1 --env.opponent_pool=1 --env.oppfit_probability=0.5 \
  --env.death_price=0 --env.damage_reward_coeff=0 --env.chance_reward_coeff=0 --env.session_ticks=3000 \
  --selfplay.enabled=1 --vec.num_policies=2 --vec.total_agents=1024 --vec.num_threads=8 \
  --train.replay_ratio=1.21378243 --train.max_grad_norm=1.01990378 --train.momentum=0.983296573 \
  --policy.hidden_size=256 --policy.num_layers=2 --train.total_timesteps=100000000 --train.horizon=64 --train.minibatch_size=4096 \
  --train.learning_rate=0.00136905 --train.ent_coef=0.000148606181 --train.gamma=0.99286592 --train.gae_lambda=0.867973983 \
  --train.clip_coef=0.475533664 --train.vf_coef=2.4654901 --train.vf_clip_coef=0.457149774 \
  --env.scripted_probability=0.5 --env.selfplay_pool_probability=0.5 --vec.hist_policy_percent=0.5
