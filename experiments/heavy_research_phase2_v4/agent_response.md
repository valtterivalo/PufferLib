# Heavy research agent v4 response — pivot to reward + widened baseline

## TL;DR verdict

**Stop Phase 2 work for Zuk-only for now.** Stage A result is strong enough to call current Phase 2 curriculum a no-go in this regime.

**Highest-EV next direction: reward redesign first, with widened PPO baseline as the control.**

Do NOT do Phase 2+BC, stronger proposal, or cross-env before that.

## Key confound the agent surfaced

Phase 2 demos are q=0.377-0.469. Best PPO baseline implied q ≈ (1200-409)/1200 = **0.659**. **The curriculum was starting from states WEAKER than what PPO learns from random starts.** BC would amplify the harm. Phase 2 was set up to fail.

## Ranked next directions

1. **Reward redesign + widened baseline** (do first)
2. **Widen baseline search** (part of the reward track, not separate)
3. Stronger proposal (later, only if reward track succeeds)
4. Phase 2 + BC (only with q≥0.85 demos that beat PPO plateau)
5. Cross-env validation (later, after one env has a credible positive)

## Stage R0: add reward-independent metrics first

Before any sweeps. Log these from inferno env, all normal-start only:

- `score_normal = mean((1200 - min_zuk_hp_normal) / 1200)` ✅ already exists in my_log
- `win_rate_normal` (we have `wins_normal` count — need rate)
- `min_zuk_hp_normal` ✅ already exists
- `death_tick_normal` (need to add)
- `phase_reached_normal` (need to add — which Zuk phase reached)
- `zuk_objective_normal = score_normal + 2.0 * win_rate_normal` ← **primary sweep metric**

`episode_return_normal` becomes a training diagnostic only once reward changes (incomparable across variants).

## Stage R1: 50M screening, 4 reward arms

```
total_timesteps = 50M
total_agents = 256, horizon = 128, minibatch_size = 4096
phase2_demo_dir = ""
phase2_bc_coef = 0
metric = zuk_objective_normal
```

Run counts:
- current reward, **widened** hparams: 80 trials
- reward_v1 (progress-delta): 60 trials
- reward_v2 (progress + milestones): 60 trials
- reward_v3 (rebalanced current): 60 trials

Cheaper first pass: 50 + 32 + 32 + 32.

### Reward variants

**Current reward, widened hparams**: control. Answers "was Stage A baseline artificially capped?"

**v1 — bounded progress-delta**: reward only on best-so-far Zuk HP reduction (not raw damage), prevents healing-farm reward.
```c
q_t = (1200 - min_zuk_hp_seen_t) / 1200
dq = max(0, q_t - q_prev)
reward += progress_coeff * dq          // progress_coeff = 10.0
reward += win_bonus if won              // 5.0
reward -= death_penalty if died         // 1.0
// tag_reward_coeff = 0 or 0.05
// shield_penalty_coeff = 0.001-0.003 with episode cap 1.0
```

**v2 — v1 + phase milestones**: one-time bonuses at HP 900/600/300, set-spawn-handled, jad spawn/damaged, healers handled. Keep small relative to true Zuk progress.

**v3 — rebalanced current**: keep current reward shape, fix:
- damage_reward_coeff: best-so-far HP reduction only, not repeat damage
- tag_reward_coeff: 0.05–0.15, one-time per entity
- shield_penalty_coeff: 0.001–0.003 (cumulative episode cap < death_penalty)
- death_penalty: -1.0
- win_bonus: +5.0 to +10.0

Avoid the "death cheaper than playing" suicide-perversity from per-tick shield penalties exceeding death penalty.

## Widened sweep ranges (apply to all reward arms)

```ini
[sweep]
metric = zuk_objective_normal
sweep_only = learning_rate, ent_coef, gamma, gae_lambda, min_lr_ratio,
             clip_coef, vf_coef, vf_clip_coef, max_grad_norm,
             replay_ratio, prio_alpha, vtrace_rho_clip, vtrace_c_clip

[sweep.train.learning_rate]   distribution=log_normal,  min=0.002, max=0.020
[sweep.train.ent_coef]        distribution=log_normal,  min=0.0005, max=0.080
[sweep.train.gamma]           distribution=logit_normal, min=0.9995, max=0.999999
[sweep.train.gae_lambda]      distribution=logit_normal, min=0.70,  max=0.95   ← NOT widened up; top-5 sat at 0.75-0.86
[sweep.train.min_lr_ratio]    distribution=uniform,     min=0.20,  max=1.00
[sweep.train.clip_coef]       distribution=uniform,     min=0.01,  max=0.20
[sweep.train.vf_coef]         distribution=log_normal,  min=0.03,  max=0.50
[sweep.train.vf_clip_coef]    distribution=uniform,     min=0.10,  max=2.00
[sweep.train.max_grad_norm]   distribution=uniform,     min=0.30,  max=3.00
[sweep.train.replay_ratio]    distribution=uniform,     min=0.25,  max=1.50
[sweep.train.prio_alpha]      distribution=logit_normal, min=0.0,  max=0.20
[sweep.train.vtrace_rho_clip] distribution=uniform,     min=1.0,   max=5.0
[sweep.train.vtrace_c_clip]   distribution=uniform,     min=1.0,   max=3.0
```

Key changes vs Stage A baseline:
- LR ceiling: 0.005 → 0.020
- Replay ceiling: 0.75 → 1.50
- Gamma ceiling: 0.99999 → 0.999999
- Clip lower: 0.05 → 0.01
- GAE: NOT widened up (top-5 didn't saturate)

## Stage R1 gates

**Positive (variant beats widened-current)**:
- IQM(zuk_objective_normal) lifts ≥ 20%
- PI(variant > current-wide) ≥ 0.70
- score_normal top-10 median lifts ≥ 0.05 absolute

**Strong positive evidence**:
- normal-start wins in ≥ 2 independent top-10 runs
- normal-start min_zuk_hp p50 < 300
- normal-start score_normal ≥ 0.75

**Clean no-go**:
- IQM lift ≤ 10% AND no normal-start wins AND improvement only in reward hacking diagnostics

**Reward hacking diagnostics**:
- higher episode_return_normal but flat score_normal
- earlier death with higher return
- tag reward up without Zuk HP progress
- shield penalty avoidance via suicide
- phase_reached_normal flat

## Stage R2: locked re-evaluation

Top 3 from current-wide + top 3 from best variant × 200M × 3-5 seeds. Bootstrap on `zuk_objective_normal`. Same gate structure as Stage A.

## Phase 2 retest protocol (LATER, only if reward track succeeds)

Plateau criteria for "Phase-2-worthy" proposal:
- score_normal lift over last 50M < 0.03
- min_zuk_hp improvement < 50 HP
- low/no win growth

Archive demo criteria:
- top-K median q ≥ PPO normal-start p90 q + 0.05
- OR top-K contains wins / q ≥ 0.85 near-wins

Then sweep wider phase 2 ranges (nsf 0.10-0.90, max_demos 16-128, backstep 2-16, succ_q_delta 0.001-0.03).

If optimizer pushes nsf to 0.90 ceiling again, phase 2 is still harmful.

## Methodology confounds (for written results)

1. Phase 2 demos weaker than PPO continuation (q 0.377-0.469 vs 0.659) — biggest confound
2. `episode_return_normal` incomparable across reward variants
3. Stage A baseline search too narrow
4. Report post-fix subset separately from full set in any written result
5. GAE does not look boundary-saturated (top-5: 0.75-0.86) — don't widen GAE up
6. Don't proceed to Stage B for Phase 2 v3 — would just confirm a bad result
