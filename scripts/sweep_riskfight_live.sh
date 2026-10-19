#!/bin/bash
# Protein sweep of the live-population objective: kills minus price x deaths per hour of the final checkpoint against the pool kits played by the script
# and OppFit (selfplay/bot_ladder_perf, config/osrs_riskfight.ini [bot_eval]); self-play opponents never enter the score.
#   cd REPO && ./build.sh osrs_riskfight && RF_OPPFIT_MODEL=... RF_OPPFIT_EAT=... scripts/sweep_riskfight_live.sh RUN_DIR
# Searched ([sweep.*] of the ini): total_timesteps, learning rate, gamma, gae_lambda, ent_coef, clip_coef, vf_coef, vf_clip_coef, horizon, minibatch, hidden size,
# layers, selfplay.enabled (0: one policy, env.self_play 0, the learner only meets the script, OppFit, pool kits and raggers; the trainer sets that switch),
# scripted_probability, selfplay_pool_probability, hist_policy_percent.
# Fixed here: session_ticks 3000, death_price 0 with price_table 0 (training draws the price of each new opponent log-uniform in [0.4, 2.5]; the score of
# [bot_eval] draws it from the live table instead), fight_starts 1 (a fight starts as one of the 161 recorded ones did, riskfight_start.h), no reward shaping,
# deployable, motor, live_loadout 5 (our kit drawn per session: a Dharok kit half the time, the user's or, with dharok_spectated_share 0.5 of
# those, a spectated one; the VW kits the rest; [bot_eval] scores the user's kits, live_loadout 4), domain randomization, the pool and OppFit
# (0.5) for the opponents the script plays, 8 env threads.
# Checkpoints: every 128 epochs (--base.checkpoint_interval) without self-play; a self-play trial checkpoints every [selfplay] checkpoint_interval (16) epochs
# of the ini instead, each joining its frozen opponent pool.
# Trial 0 is the anchor below: trial 34 of the third live sweep (20261017-riskfight-live-sweep3, 11.0 per hour, best on the probe too), run under the stronger
# reputation cost of 2026-10-17 (rep_search 2.0, rep_rag 0.3, rep_half_life 6000, the ini defaults); the rest are Protein's. Scores are not comparable with sweep 3's.
# max_runs and max_suggestion_cost come from the ini. A killed sweep resumes from RUN_DIR through --sweep.resume_dir (see src/sweep_resume.h). Arguments after RUN_DIR
# are passed on to ./puffer sweep (scripts/sweep_riskfight_fast.sh narrows the search with them).
set -euo pipefail
RUN=${1:?usage: sweep_riskfight_live.sh RUN_DIR}
: "${RF_OPPFIT_MODEL:?RF_OPPFIT_MODEL}" "${RF_OPPFIT_EAT:?RF_OPPFIT_EAT}"
mkdir -p "$RUN/checkpoints" "$RUN/logs"
exec ./puffer sweep \
  --base.seed=73 --base.checkpoint_dir="$RUN/checkpoints" --base.log_dir="$RUN/logs" --base.checkpoint_interval=128 --base.eval_episodes=0 \
  --env.live_loadout=5 --env.deployable=1 --env.motor=1 --env.domain_randomization=1 --env.opponent_pool=1 --env.oppfit_probability=0.5 \
  --env.death_price=0 --env.price_table=0 --env.fight_starts=1 --env.damage_reward_coeff=0 --env.chance_reward_coeff=0 --env.session_ticks=3000 \
  --selfplay.enabled=0 --vec.num_policies=2 --vec.total_agents=16384 --vec.num_threads=8 \
  --train.replay_ratio=1.63940668 --train.max_grad_norm=0.408098608 --train.momentum=0.980446517 \
  --policy.hidden_size=512 --policy.num_layers=1 --train.total_timesteps=182724576 --train.horizon=32 --train.minibatch_size=2048 \
  --train.learning_rate=0.000703111524 --train.ent_coef=1.73160424e-05 --train.gamma=0.999118865 --train.gae_lambda=0.907420635 \
  --train.clip_coef=0.899999976 --train.vf_coef=2.16847849 --train.vf_clip_coef=0.0500000007 \
  --env.scripted_probability=0.0657308102 --env.selfplay_pool_probability=1 --vec.hist_policy_percent=0.5 "${@:2}"
