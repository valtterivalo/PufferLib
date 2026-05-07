# Go-Explore healer-phase bottleneck in osrs_inferno

## 1. Objective

We want an outside read on the next highest-value direction for Go-Explore style work in `osrs_inferno`.

The current quality-v2 archive and restore curriculum improves normal-start score and reaches Zuk healer range more often than matched no-demo control. It has not produced wins. The most visible bottleneck is converting healer-range reach into healer resolution and continued Zuk progress.

Please reason from the evidence here and from the attached code digest. Do not assume our current diagnosis is correct.

## 2. System context

Project: `pufferlib-metal`, a C, CUDA, and Metal backend aligned with PufferLib 4.0.

Environment: `osrs_inferno`, an Old School RuneScape Inferno Zuk encounter. The agent starts at wave 69. It must handle Zuk, Jad, Zuk healers, active set mobs, shield movement, prayers, gear, and supplies.

Current Go-Explore path:

- Phase 1 archive exploration stores simulator snapshots, recurrent hidden state, RNG seed, parent links, and action chunks.
- Phase 1 exports demos sorted by structural quality.
- Phase 2 restores simulator snapshots from demos during PPO training.
- CUDA phase 2 currently uses restore states only. Behavior cloning from demo actions is not implemented on CUDA and aborts if enabled.
- Direct simulator restore is the return mechanism.

Current main checkpoint:

- `p2k4szzs/0000000049971200.bin`
- Remote path: `/puffertank/docker/goexplore/checkpoints/osrs_inferno/p2k4szzs/0000000049971200.bin`

Current quality-v2 demos:

- Remote path: `/puffertank/docker/goexplore/experiments/heavy_research_phase2_v4/demos_quality_v2/seed_62/demos`

Key normal-start metrics:

- `score_normal`: current selection metric.
- `min_zuk_hp_normal`: lower is better, but Zuk healers can heal Zuk back up after spawning.
- `frac_min_hp_le_240_normal`: fraction of normal starts that reach Zuk healer threshold.
- `damage_after_240_normal`: damage after the run first reaches 240 HP.
- `frac_min_hp_le_150_normal`: fraction of normal starts that get below 150 HP.
- `damage_after_150_normal`: damage after first reaching 150 HP.
- `frac_died_with_zuk_healer_alive_normal`: death fraction with Zuk healers still alive.
- `wins_normal`: currently zero in these brackets.

Domain note from Valtteri: when Zuk reaches 240 HP, healers spawn and can heal Zuk back up, often toward 400 to 500 HP even if tagged quickly. Metrics based only on minimum Zuk HP can therefore overstate real post-healer competence.

## 3. Current issue

Go-Explore qv2 has a clear positive effect over matched no-demo control on score and healer-threshold reach. It has not solved the encounter.

Current best 500M bracket:

- Directory: `/puffertank/docker/goexplore/experiments/heavy_research_phase2_v4/remote_post150_candidate500m`
- n = 6
- median score `0.7937`
- mean score `0.7942`
- worst score `0.7873`
- best score `0.8036`
- catastrophic count `0`
- median min Zuk HP `247.6`
- median `frac_min_hp_le_240_normal` `0.7105`
- median `damage_after_240_normal` `42.2`
- median `frac_died_with_zuk_healer_alive_normal` `0.7094`
- median `frac_min_hp_le_150_normal` `0.00012`
- median `damage_after_150_normal` `6.5`
- wins `0`

Matched no-demo control at 500M:

- Directory: `/puffertank/docker/goexplore/experiments/heavy_research_phase2_v4/remote_low_lr_matched_control500m`
- n = 6
- median score `0.7211`
- mean score `0.7198`
- worst score `0.7140`
- best score `0.7222`
- median min Zuk HP `334.7`
- median `frac_min_hp_le_240_normal` `0.2407`
- median `damage_after_240_normal` `24.0`
- median `frac_died_with_zuk_healer_alive_normal` `0.2420`
- wins `0`

Interpretation we have been using:

- qv2 improves wall-clock progress and reaches healer range much more often.
- Most of that new reach still dies with Zuk healers alive.
- The next problem may be robustification of the healer transition, not finding lower pre-healer Zuk HP.

This interpretation may be incomplete or wrong. Please challenge it from the evidence.

## 4. What we tried

Archive and quality alignment:

- Replaced first-write archive behavior with separate `sampling_quality` and `structural_quality`.
- Duplicate cells now update sampling quality independently.
- Better structural quality replaces snapshot, hidden state, parent, RNG seed, and action chunk.
- Archive file version bumped to v2. v1 archive load fails loudly.
- Demo export sorts by structural quality.
- Inferno quality became transition-aware, with credit for Jad clear, Zuk healer clear, active set clear, and post-Jad re-engagement.
- Cell key remains 16 bytes, with active set count including mager, ranger, and meleer, and an added Jad HP bucket.

CUDA training changes:

- Added terminal-aware recurrent hidden-state reset behind `train.terminal_reset_state = 1`.
- CUDA rollout zeros per-env recurrent state after terminal before the next `policy_forward`.
- CUDA training forward also receives terminal masks so recurrent state semantics match rollout.
- Seed handling was fixed so CUDA reads top-level `seed` or falls back to `train.seed`.
- CUDA action-mask handling was aligned with Metal.

Reward and shaping work:

- Current preference is minimal shaping in PufferLib style.
- Prior reward variants added extra Jad, healer, and set terms but did not improve the main outcome.
- We are avoiding another reward-shaping pass unless evidence says it is the highest-value path.

Experiments:

- 50M qv2 scout showed qv2 `normal_start_frac=0.50` beating control.
- 200M qv2 had strong median and upper tail but one seed collapsed early.
- Terminal-reset recurrent-state patch was added after outside advice.
- Multiple 500M qv2 brackets then beat matched no-demo control.
- Low learning-rate Protein scouts found stronger qv2 hparams.
- Healer diagnostics showed some arms could tunnel score by reaching healer range more often while still dying with healers alive.
- Post-150 Protein scout selected a candidate that improved 500M score and gave a small post-150 signal.
- Restore-mix scout varied normal-start fraction and RNG randomization around the candidate. It was negative.

## 5. Observations

Important completed results:

### Matched no-demo control, 500M

- n `6`
- median score `0.7211`
- mean score `0.7198`
- worst score `0.7140`
- best score `0.7222`
- median min Zuk HP `334.7`
- median `frac_min_hp_le_240_normal` `0.2407`
- median `ticks_after_240_normal` `7.11`
- median `damage_after_240_normal` `24.0`
- median `frac_died_with_zuk_healer_alive_normal` `0.2420`
- wins `0`

### qv2 low-lr balanced, 500M

- n `6`
- median score `0.7685`
- mean score `0.7718`
- worst score `0.7408`
- best score `0.8123`
- median min Zuk HP `277.8`
- median `frac_min_hp_le_240_normal` `0.5013`
- median `ticks_after_240_normal` `4.24`
- median `frac_died_with_zuk_healer_alive_normal` `0.5013`
- wins `0`

### qv2 low-lr more, 500M

- n `6`
- median score `0.7873`
- mean score `0.7893`
- worst score `0.7743`
- best score `0.8058`
- median min Zuk HP `255.2`
- median `frac_min_hp_le_240_normal` `0.6182`
- median `damage_after_240_normal` `47.2`
- median `frac_died_with_zuk_healer_alive_normal` `0.6163`
- wins `0`

### Healer diagnostic arms, 300M

`source_balanced`:

- n `3`
- median score `0.7897`
- median `frac_min_hp_le_240_normal` `0.7249`
- median `frac_died_with_zuk_healer_alive_normal` `0.7260`
- median `damage_after_240_normal` `30.8`

`tight_gated`:

- n `3`
- median score `0.7676`
- median `frac_min_hp_le_240_normal` `0.5208`
- median `frac_died_with_zuk_healer_alive_normal` `0.5211`
- median `damage_after_240_normal` `25.0`

`long_post240_low_reach`:

- n `3`
- median score `0.7590`
- median `frac_min_hp_le_240_normal` `0.5977`
- median `frac_died_with_zuk_healer_alive_normal` `0.5974`
- median `damage_after_240_normal` `23.7`

`raw_score_tunnel`:

- n `3`
- median score `0.8031`
- mean score `0.8110`
- median `frac_min_hp_le_240_normal` `0.8303`
- median `frac_died_with_zuk_healer_alive_normal` `0.8309`
- median `damage_after_240_normal` `53.3`

### Post-150 candidate, 500M

- n `6`
- median score `0.7937`
- mean score `0.7942`
- worst score `0.7873`
- best score `0.8036`
- median min Zuk HP `247.6`
- median `frac_min_hp_le_240_normal` `0.7105`
- median `damage_after_240_normal` `42.2`
- median `frac_died_with_zuk_healer_alive_normal` `0.7094`
- median `frac_min_hp_le_150_normal` `0.00012`
- median `damage_after_150_normal` `6.5`
- wins `0`

Post-150 candidate hparams:

- learning rate `0.0008677578914770761`
- min LR ratio `0.8089916809727309`
- gamma `0.9999998`
- gae lambda `0.965`
- clip coef `0.061146749752710124`
- entropy coef `0.022220449623925543`
- value coef `0.28`
- value clip coef `1.7523166497179001`
- max grad norm `0.545683594293967`
- replay ratio `0.6727154877617739`
- priority alpha `0.054680769116347894`
- vtrace rho clip `1.9`
- vtrace c clip `1.9`
- phase2 normal start fraction `0.40`
- phase2 randomize RNG fraction `0.04047939437287103`
- phase2 backstep ticks `8`
- phase2 success q delta `0.0010560670538554925`
- phase2 BC coef `0`

### Post-150 restore-mix scout, 300M

This varied normal-start fraction and restored RNG randomization around the post-150 candidate.

`base_nsf40_rng04`:

- n `3`
- median score `0.7644`
- worst score `0.7590`
- median `frac_min_hp_le_240_normal` `0.5077`
- median `damage_after_150_normal` `0`
- wins `0`

`nsf30_rng04`:

- n `3`
- median score `0.7724`
- worst score `0.7684`
- median `frac_min_hp_le_240_normal` `0.6417`
- median `damage_after_150_normal` `0`
- wins `0`

`nsf50_rng04`:

- n `3`
- median score `0.7636`
- worst score `0.7618`
- median `frac_min_hp_le_240_normal` `0.4373`
- median `damage_after_150_normal` `0`
- wins `0`

`nsf40_rng12`:

- n `3`
- median score `0.7657`
- worst score `0.7640`
- median `frac_min_hp_le_240_normal` `0.5405`
- median `damage_after_150_normal` `4.5`
- wins `0`

`nsf50_rng12`:

- n `3`
- median score `0.7522`
- worst score `0.7471`
- median `frac_min_hp_le_240_normal` `0.3064`
- median `damage_after_150_normal` `0`
- wins `0`

The mix scout was negative compared to the post-150 candidate and was not followed by another bracket.

## 6. Constraints

- Stay close to upstream PufferLib 4.0. Avoid building a parallel RL framework.
- Keep reward shaping minimal unless evidence supports a targeted change.
- CUDA experiments run on one RTX 4090 inside the `puffertank` container.
- The 4090 gets about `0.85M` to `1.4M` SPS depending on config.
- Old archive and demo files are disposable experiment artifacts.
- The external research response should not assume access to the live box. It should work from this brief, the digest, the branch, and the papers.

## 7. Open questions

These are deliberately broad.

1. What are the most plausible causes of the healer-phase bottleneck?
2. Are our metrics selecting policies that merely reach healer threshold instead of resolving the transition?
3. Is the phase-2 restore-state curriculum structurally insufficient without action-target behavior cloning?
4. Is demo slot selection or cursor initialization likely training from the wrong parts of the trajectories?
5. Should the archive quality or cell identity change again, or should we leave qv2 alone and focus on robustification?
6. Is there a better comparison than normal-start score at fixed steps for judging whether Go-Explore is helping in this PufferLib high-throughput setting?
7. What experiment would best separate "healer mechanics not learned" from "training objective rewards healer reach more than healer resolution"?
8. What code-level changes would improve observability or training reliability before spending another large bracket?
9. What should we explicitly not try next?

## 8. Requested deliverable

Please provide a concrete, ranked response:

1. Summary of what the evidence most strongly says.
2. Ranked hypotheses for the current bottleneck, with code-level evidence when possible.
3. A proposed next 12 to 24 hour experiment plan for one RTX 4090.
4. Metrics and go or no-go gates for each proposed experiment.
5. Any code changes that should happen before the next bracket.
6. Any changes to archive quality, demo filtering, curriculum, BC, reward, or eval selection that you think are worth considering.
7. What extra context would most improve your confidence.

Please avoid generic Go-Explore or PPO advice. Work from the provided facts. Challenge our interpretation where warranted.
