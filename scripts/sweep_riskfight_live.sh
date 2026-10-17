#!/bin/bash
# Protein sweep of the live-population objective: kills minus price x deaths per hour of the final checkpoint against the pool kits played by the script
# and OppFit (selfplay/bot_ladder_perf, config/osrs_riskfight.ini [bot_eval]); self-play opponents never enter the score.
#   cd REPO && ./build.sh osrs_riskfight && RF_OPPFIT_MODEL=... RF_OPPFIT_EAT=... scripts/sweep_riskfight_live.sh RUN_DIR
# Searched ([sweep.*] of the ini): total_timesteps, learning rate, gamma, gae_lambda, ent_coef, clip_coef, vf_coef, vf_clip_coef, horizon, minibatch, hidden size,
# layers, selfplay.enabled (0: one policy, env.self_play 0, the learner only meets the script, OppFit, pool kits and raggers; the trainer sets that switch),
# scripted_probability, selfplay_pool_probability, hist_policy_percent.
# Fixed here: session_ticks 3000, death_price 0 with price_table 0 (training draws the price of each new opponent log-uniform in [0.4, 2.5]; the score of
# [bot_eval] draws it from the live table instead), fight_starts 1 (a fight starts as one of the 161 recorded ones did, riskfight_start.h), no reward shaping,
# deployable, motor, live_loadout 3, domain randomization, the pool and OppFit (0.5) for the opponents the script plays, 8 env threads.
# Trial 0 is the anchor below: trial 270 of the second live sweep (20261017-riskfight-live-sweep2, 6.51 per hour on the old score: regeared starts, price log-uniform),
# which sits on three edges of the unchanged ranges (hidden size 512, clip_coef 0.9, vf_clip_coef 0.05); the rest are Protein's. Scores of this sweep (starts and prices of the recordings) are not comparable with the earlier
# sweeps'. max_runs and max_suggestion_cost come from the ini. A killed sweep resumes from RUN_DIR through --sweep.resume_dir (see src/sweep_resume.h).
set -euo pipefail
RUN=${1:?usage: sweep_riskfight_live.sh RUN_DIR}
: "${RF_OPPFIT_MODEL:?RF_OPPFIT_MODEL}" "${RF_OPPFIT_EAT:?RF_OPPFIT_EAT}"
mkdir -p "$RUN/checkpoints" "$RUN/logs"
exec ./puffer sweep \
  --base.seed=73 --base.checkpoint_dir="$RUN/checkpoints" --base.log_dir="$RUN/logs" --base.checkpoint_interval=128 --base.eval_episodes=0 \
  --env.live_loadout=3 --env.deployable=1 --env.motor=1 --env.domain_randomization=1 --env.opponent_pool=1 --env.oppfit_probability=0.5 \
  --env.death_price=0 --env.price_table=0 --env.fight_starts=1 --env.damage_reward_coeff=0 --env.chance_reward_coeff=0 --env.session_ticks=3000 \
  --selfplay.enabled=0 --vec.num_policies=2 --vec.total_agents=8192 --vec.num_threads=8 \
  --train.replay_ratio=1.37251544 --train.max_grad_norm=0.300172061 --train.momentum=0.98876369 \
  --policy.hidden_size=512 --policy.num_layers=2 --train.total_timesteps=185222000 --train.horizon=64 --train.minibatch_size=2048 \
  --train.learning_rate=0.000565917348 --train.ent_coef=3.76093999e-06 --train.gamma=0.994770944 --train.gae_lambda=0.886686385 \
  --train.clip_coef=0.899999976 --train.vf_coef=2.34742808 --train.vf_clip_coef=0.0500000007 \
  --env.scripted_probability=0.248850763 --env.selfplay_pool_probability=1 --vec.hist_policy_percent=0.5
