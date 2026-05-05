# Round-5 heavy-agent consult prompt (self-contained)

Designed to be readable by a fresh heavy agent with no prior session memory.
Attach: this prompt + Go-Explore paper(s) + repo digest of the
`goexplore-robustify` branch.

---

```
Round 5 consult. Inferno Zuk RL training for OSRS via PufferLib-Metal.
We have hit a stable plateau and need a strategic re-frame.

This prompt is self-contained. Treat earlier consult turns as gone unless
explicitly summarised here.

============================================================================
PROJECT
============================================================================

OSRS = Old School RuneScape. The Inferno is a 70-wave PvE encounter that
ends with a final boss, TzKal-Zuk. We have a custom C simulation of the
encounter (faithful to the live game's tick-level mechanics, NPC AI,
projectile timing, prayer system, gear switching) wrapped as a Gymnasium-
style RL environment. The simulation is fully deterministic given an
RNG seed.

The agent fights solo. Action space is multi-headed (9 heads):
  - MOVE: 25 (idle, 8 walk dirs, 16 run dirs)
  - PRAYER: 4 (overhead toggle: range/mage/melee/none)
  - TARGET: 38 (0=none, 1-37 = obs-slot index of NPC to target)
  - GEAR: 5 (no-switch, mage, tbow, blowpipe, tank)
  - EAT: 2 (none, brew)
  - POTION: 4 (none, restore, bastion, stamina)
  - SPELL: 3 (no-change, blood-barrage, ice-barrage)
  - SPEC: 2 (no-change, blowpipe-spec)
  - OFFENSIVE prayer: 4 (no-change, piety, rigour, augury)

Observation: 1157 features including 37 NPC obs slots (mager/ranger/meleer
sets, blobs, bats, nibblers, JAD, ZUK, ZUK_SHIELD, healer-jad, healer-zuk),
player state, prayer points, supplies, attack/cooldown timers.

Encounter structure (Zuk wave only, wave 68):
  Phase 1: Zuk @ 1200 HP, no adds, mage-style shield projectile
  Phase 2: at 900 HP, set spawns (mager+ranger+meleer)
  Phase 3: at 600 HP, JAD spawns (50/50 random ranged/magic, healed by
           4 jad-healers if not killed quickly)
  Phase 4: at 300 HP, two zuk-healers spawn at fixed positions, fly to Zuk
           and heal him every few ticks
  Win:     Zuk at 0 HP

Scoring (sweep target):
  score_normal = min(1, max(0, (1200 - min_zuk_hp_seen) / 1200))
  per normal-start episode (i.e., phase2 disabled or start_wave=Zuk).
  0.0 = never damaged Zuk, 1.0 = killed Zuk.

Hardware: M4 Pro single GPU, ~52K SPS at hs=256/L=3/agents=512/horizon=8
training-config; ~245K SPS at smaller archive-explore configs. Total
50M-step phase-2 cell ~3.5 min wall clock. Architecture changes break
weight transfer (different param shapes).

============================================================================
TRAINING TRAJECTORY (compact)
============================================================================

R0: reward-independent baselines, scoring sanity, demo metric audit.
R1: 4 reward variant sweeps. v3 reward emerged best:
       damage_reward 0.01, shield_penalty 0.002, tag_reward 0.10,
       win_bonus 8.0, death_penalty 1.0, shield_penalty_episode_cap 0.5.
       e3vyhuh9 hparams (lr 0.0092, ent 0.00154, gamma 0.99974, etc.) won.
R2 / A': 200M continuation training on R1 best. Reached score ~0.74.
       Plateaued. PPO + recurrent core, hs=256, num_layers=3 (MinGRU).
D: deep audit. Confirmed v3 reward shape, locked R1 best hparams.

C: phase 2 = Go-Explore-style backward curriculum.
   Stage: from a strong base policy, generate phase-2 demos via
   archive_explore (random/policy-driven exploration of cell space, keep
   high-quality cells). Then resume PPO with mixed normal-start and
   demo-restored episodes. The point of phase 2 is to teach the model
   the late game by warm-starting episodes deep in the encounter.

   Demo file format: header with quality + leaf-cell key + 1+ snapshots,
   chain of action chunks. On reset, env restores to leaf snapshot,
   replays a backstep (~ 2 tick rewind, configurable), then hands control
   to the agent. Backstep gives a few free ticks for the policy to settle
   into the late-game state before agent acts.

   C round 1 (3 arms x 4 seeds, 50M):
     C0: PPO continuation control     best 0.762
     C1: phase 2 only                 best 0.765
     C2: phase 2 + BC                 BC collapsed, best 0.541, median 0.230
   C round 2: drop BC, narrow proposal/demos, longer train. Best 0.779.
              p2k4szzs is the round-2 best. Still global frontier.
   C round 3: scout new hparams. Best 0.769.
   C long-train (200M from p2k4szzs, no BC): 0.770.
   B0: horizon=256 retrain from p2k4szzs. 0.763.

C4 (round-3 plan from previous heavy-agent consult):
   Three frontier-biased demo generation strategies x 4 seeds x 50M:
     A baseline: standard count-decay sampler, max q 0.804
     B G1:       frontier sampler q_floor=0.80 power=4, max q 0.822
     C G2:       frontier sampler q_floor=0.85 power=8 + 300 iter, max q 0.831

   Frontier sampler (src/archive.h):
     90% of archive samples weighted q^power / sqrt(chosen+1) for cells
     with quality >= q_floor; 10% fallback to standard count-decay.

   Result:
     A baseline    best 0.761 median 0.722  (4 cells, all trained)
     B g1          best 0.705 median 0.371  (3 of 4 collapsed in training)
     C g2          best 0.753 median 0.745  (most reliable, no improvement)

   None advanced past 0.779. H1 (demo ceiling) weakened.

   IMPORTANT note: G2 may not have used frontier path at all. With
   q_floor=0.85 and observed max q 0.831, eligible cell count was likely 0,
   silently falling through to standard count-decay. We have not yet logged
   frontier_eligible_count to confirm.

D-deep instrumentation (added between rounds 3 and 4):
   Per-episode tail counters in encounter_inferno.h InfernoState:
     tick_at_le_300, tick_at_le_240, tick_at_le_150 (-1 sentinel)
     damage_after_300, damage_after_240, damage_after_150
   Per-episode death-cause flags accumulated in binding.c terminal handler:
     died_with_jad_alive, died_with_zukhealer_alive, died_with_setNPC_alive
   Surface as Log fields; my_log emits 9 new env/* metrics.

D-deep on p2k4szzs (the global best, round-2):
   1731 normal eps: score 0.720, fr<=300 0.44, fr<=240 0.26, fr<=150 0.0006
   ticks_after_300 = 14.8   damage_after_300 = 94 HP
   ticks_after_240 = 9.5    damage_after_240 = 56 HP
   ticks_after_150 = 3.0    damage_after_150 = 34 HP
   frac_died_with_jad_alive = 0.82
   frac_died_with_healer_alive = 0.26
   frac_died_with_set_alive = 0.99

The plateau looks structural: the policy reaches phase 4 (crosses 300 HP),
has roughly 94 HP of further Zuk damage before dying, and dies with Jad
alive in 82% of episodes and with at least one set/add NPC alive in 99%.

============================================================================
ROUND 4 (just completed) - oracle target-priority wrapper eval
============================================================================

Heavy agent's prior round-4 verdict (paraphrased):
  - C4 weakens demo-ceiling hypothesis enough; stop iterating archive sampling
  - Top recommendation: v6 late-add-priority reward (soft Zuk multiplier +
    dense add damage rewards + add kill bonuses)
  - Cheapest sanity check first: oracle target-priority wrapper. Override
    target-head action only when zuk_hp <= threshold. If wrapper improves
    score / kills / late-tail, reward/priority is the binding constraint
    and v6 has a strong prior. If wrapper does not improve, the issue is
    deeper (movement/prayer/timing) and v6 needs to be cautious.

We built the wrapper as a one-line override hook in the encounter step
(encounter_inferno.h `inf_oracle_pick_target_slot` + override block before
the existing target-decode in `inf_tick_player`). Plumbed via env config
`oracle_mode` (0=off, 1=Jad-only @300, 2=full priority @300, 3=full @240).

Priority order:
  1. Jad alive    -> target Jad
  2. Zuk healer   -> target Zuk healer
  3. Set NPC      -> target first non-Zuk/non-Jad/non-healer add
  4. Zuk          -> default (no override)

20k normal-start episodes per arm on p2k4szzs:

  metric         E0       E1       E2       E3
  score        0.7191   0.7074   0.7073   0.7190
  ret_n         7.971    7.977    7.976    7.972
  wins         0        0        0        0
  fr<=300      0.4173   0.4236   0.4226   0.4198
  fr<=240      0.2295   0.0160   0.0157   0.2324
  fr<=150      0.0012   0.0000   0.0000   0.0000
  die_jad      0.8131   0.8165   0.8167   0.8218
  die_heal     0.2292   0.0334   0.0325   0.2276
  die_set      0.9878   0.9886   0.9891   0.9885
  tk_a300      13.28    13.65    13.58    13.21
  dmg_a300     87.6     53.6     53.5     86.5
  tk_a240       8.18     7.29     7.56     8.06
  dmg_a240     55.5     62.2     62.2     52.5
  tk_a150       0.59     0.00     0.00     0.00
  dmg_a150      5.5      0.0      0.0      0.0
  heal_dmg     0.289    0.001    0.001    0.109
  jad_kills    0.114    0.122    0.121    0.123
  phase         2.40     2.40     2.40     2.40

E0 reproduces p2k4szzs's round-4 D-deep numbers exactly (eval calibrated).

Heavy agent's r4 gates:
  Gate              E1     E2     E3
  score >= +0.03    fail   fail   fail
  wins > 0          fail   fail   fail
  fr<=150 >= 2x     fail   fail   fail
  die_jad <= -30%   fail   fail   fail

All four gates fail in every override arm.

The signal:

1. Forcing target=Jad does not meaningfully kill Jad. jad_kills moves
   only +0.7% (0.114 -> 0.122). The policy CAN attack Jad when commanded
   but cannot kill Jad effectively in late-Zuk state.

2. Forcing target=Jad breaks Zuk progress. dmg_a300 drops 87.6 -> 53.5 HP
   in E1/E2; ~34 HP of Zuk damage that the raw policy was extracting after
   crossing 300 vanishes. fr<=240 collapses 14x (0.230 -> 0.016).

3. die_jad fraction barely moves (+0.4 to +1.1%) across all arms. Even
   with full priority override, the agent dies with Jad alive at the same
   rate. THIS IS THE CENTRAL FINDING: the policy lacks the capability to
   kill Jad late-game, not just the priority.

4. Healer damage destroyed in E1/E2: heal_dmg 0.289 -> 0.001. Override-
   busy agents never reach healer-kill phase.

5. The policy's "ignore adds and tunnel Zuk" is a local survival hack
   rather than a deliberate priority. Forcing the alternative makes
   everything worse because the capabilities (right gear, prayer, position,
   timing) for that alternative do not exist in the policy.

E3 (override at 240) is essentially a no-op: most episodes never cross 240,
so the override rarely fires.

Per heavy agent r4's interpretation guide, this is the "Wrapper worsens /
Wrapper does not improve" branch:
  "current policy's 'ignore adds' may be a local survival hack
   v6 still worth a small scout, but avoid strong Zuk reward gating"
  "add handling needs movement/prayer/timing, not target priority alone
   add D-deep counters for shield/prayer/death-source before training v6"

============================================================================
HYPOTHESIS LADDER (revised after r4)
============================================================================

H_capability (NEW, leading): the policy lacks the skill to kill Jad/healers
  late-game even when given correct target. This decomposes into:
    H_cap_gear:    wrong gear out (mage instead of range)
    H_cap_prayer:  wrong overhead/offensive prayer for Jad's style
    H_cap_pos:    wrong position relative to Jad's projectile + Zuk
    H_cap_timing: switching mid-attack, prayer-flick errors
    H_cap_supply: low prayer/HP at the moment override fires

H_localopt: "ignore adds, tunnel Zuk" is a local optimum the policy fell
  into. v3 reward + the encounter dynamics make damage-Zuk-and-die-fast
  the highest-EV strategy at the policy's current capability level.

H_capacity: hs=256 / L=3 too small for late-fight branching (jad-active
  vs healer-active vs different add combinations require very different
  responses).

H_demo_dist: phase-2 demos under-represent successful late-add states,
  so the policy has no training signal on "what to do once Jad is dead."
  Even oracle-override demos would not help since the override doesn't
  produce Jad-dead trajectories.

H_reward (weakened from r4 leading position): no add-priority signal in
  the reward. v6 still worth scouting but with soft gating.

============================================================================
QUESTIONS FOR ROUND 5
============================================================================

Q1. WRAPPER VARIANT - "full oracle"

The current oracle overrides only target. Per H_capability, that is too
weak: the policy has wrong gear/prayer for the new target.

Should we build a "full oracle" wrapper that also forces gear and overhead
prayer alongside target?

Suggestion:
  if zuk_hp <= 300 and Jad alive:
      target = Jad
      overhead_prayer = (range if Jad's current attack style is ranged
                        else mage)
      gear = tbow if Jad's style is ranged else mage
  if zuk_hp <= 300 and zuk-healer alive:
      target = nearest zuk-healer
      gear = blowpipe (fast hits, healers have low HP)

Pros: directly tests H_capability vs H_priority decomposition. If full
oracle improves score / Jad kill / win rate, capability is real and
fixable via curriculum or imitation. If full oracle ALSO does not improve,
the bottleneck is even deeper (movement/timing/positioning).

Cons: more code, larger override surface. The result is also
interpretable only as "what we wired in"; we can't easily attribute a
positive result to gear vs prayer vs both.

Is this the right next experiment, or is something cheaper available?

Q2. v6 REWARD - what does "soft gating" actually mean

Heavy agent r4 said: "v6 still worth a small scout, but avoid strong Zuk
reward gating". What does "soft" mean concretely given:
  - PPO reward clamp [-1, 1] before advantage computation
  - existing v3 dense damage reward 0.01 per HP of Zuk damage
  - existing tag_reward 0.10 for tagging the Zuk healer

Concrete interpretations:

  v6-soft-A: Zuk damage reward unchanged (still 0.01/HP after 300).
             Add jad_damage_reward = 0.008/HP, healer_damage = 0.006/HP,
             set_damage = 0.004/HP. Add Jad/healer/set kill bonuses
             0.75/0.25/0.25. No Zuk reward multiplier.

  v6-soft-B: as v6-soft-A but with weak Zuk reward multiplier:
             0.75x while Jad alive, 0.50x while healer alive, no set.

  v6-soft-C: as v6-soft-A but only AFTER zuk_hp <= 300, before that the
             reward is pure v3.

  v6-soft-D: as v6-soft-C plus prayer-based bonus:
             +0.001 per tick correct overhead vs current incoming-attack
             style (already known mid-fight from npc.attack_style).

Which of these has the lowest reward-hacking risk? The reward clamp at
[-1, 1] worries me about (a) Jad kill bonus being clipped, and (b) one
extreme tick (e.g., gear switch + brew + take damage + heal + spec
attack) clipping legitimate signal.

Q3. ARCHITECTURE PROBE viability

The capacity hypothesis (H_capacity) was de-prioritized in r3 but rises
now. Heavy agent r4 suggested B-transfer (distill p2k4szzs into hs=512/
L=4 student). My concern: distillation only matches what p2k4szzs
already knows. A larger student that perfectly imitates p2k4szzs has the
same score and the same late-fight failures.

The point of B-transfer must be that the larger student, ONCE warm-
started near p2k4szzs, can then learn faster from v6 RL because it has
more capacity for the branching late-fight policy. Is that the right
read? If so:

  - What distillation loss makes sense? L2 on logits + value? Mask-aware
    cross-entropy on each action head?
  - Should distillation oversample late-game states (zuk_hp <= 300) since
    that is where capacity is hypothetically needed?
  - What is a reasonable distillation budget on M4 Pro before we know if
    the student can match teacher? 5M? 20M?

Q4. WHAT D-DEEP COUNTERS NEXT

Heavy agent r4 named: shield/prayer/death-source counters. Concretely
(in the InfernoState struct, surfaced via my_log):

  - Per-tick prayer correctness vs incoming attack style:
    correct_overhead_ticks_after_300/240/150 / total_ticks_after_X
  - Shield-block breakdown:
    shield_block_count_after_300, shield_block_dmg_after_300
  - Death-source attribution:
    last_hit_by_type when episode terminates (already partly tracked as
    s->last_hit_by_type)
  - Gear at moment of death:
    weapon_set_at_death, prayer_at_death (already have prayer_at_death)
  - Average Jad style during late fight:
    jad_attack_style_distribution
  - Conditional fractions (heavy agent r4 already requested):
    frac_died_after_300_with_jad_alive vs current
    frac_died_with_jad_alive_normal (count / n_normal)

What is the minimum set of new counters that lets us discriminate H_cap_*
sub-hypotheses?

Q5. IS THIS STILL GO-EXPLORE?

The original Go-Explore framework (attached) emphasizes:
  - Cell-based archive of reachable states
  - Quality-weighted sampling for exploration
  - Robustification phase (we are doing)
  - The agent's training signal is "reach the end state from here"

In our setup, demo replay puts the agent at deep cells, but the cells are
sampled from policies that themselves cannot solve the task. The archive
is a static product of policy-trajectory exploration; we are not doing
the iterative Go-Explore loop where the archive is rebuilt from
improved policies.

Question: should we restart phase 1 (archive_explore) from p2k4szzs to
get a NEW archive that reflects the current best policy's reach? Then
generate fresh demos for phase 2. This is closer to canonical Go-Explore
than what we are doing.

Concretely:
  archive_explore(proposal=p2k4szzs, num_iterations=50, archive_capacity=300k)
  -> new archive
  -> generate ~200 demos via standard count-decay (skip frontier)
  -> phase 2 from p2k4szzs with these new demos
  -> 4 seeds x 50M

The hope: p2k4szzs has reached state cells that the round-1 base policy
never reached. New archive reflects p2k4szzs's reach. Demos generated
from that archive should be richer in late-Zuk states with adds dead /
healers killed. Phase 2 from these demos teaches the policy what to do
AFTER killing Jad rather than just AT phase 4 onset.

Risks: late-fight states are sparse in policy rollouts (only 0.06% of
episodes get below 150 HP). Archive_explore from p2k4szzs may produce
even fewer late-fight cells than we already have, since the seed policy's
late-fight behavior IS the failure we are trying to fix.

Worth doing? If yes, what proposal / archive params?

Q6. CAPACITY-VS-DEMO TRADEOFF

Given a fixed compute budget (e.g., 1 hour M4 Pro = ~17 cells of 50M
phase 2), what is the best portfolio to discriminate hypotheses?

Some options:
  Plan A:
    1 cell  full-oracle wrapper eval (free, 30s)
    8 cells v6 reward scout (4 v3 control + 4 v6-soft-A)
    4 cells refresh phase-1 archive from p2k4szzs (Q5)
    4 cells phase-2 from refreshed demos
  Plan B:
    1 cell  full-oracle wrapper eval
    4 cells v6-soft-A
    4 cells v6-soft-B
    4 cells v6-soft-C
    4 cells distillation B-transfer warmup
  Plan C:
    1 cell  full-oracle wrapper eval
    16 cells single-arm v6-soft-A x 16 seeds (statistical power)

Plan C has the highest statistical power for one variant. Plan A
attempts the broadest hypothesis coverage. Plan B brackets v6 variants.

Which plan, or another?

============================================================================
DELIVERABLE WE NEED
============================================================================

1. Top recommendation. What is the ONE next experiment.
2. Concrete implementation details (config knobs, expected runtime,
   gates, no-go signals, reward-hacking flags).
3. Code-level observations from the oracle eval results, the C4 commit
   (2523814bf), or the archive_sample frontier-mode implementation
   (src/archive.h).
4. Sanity checks we should run before committing more compute.
5. A revised hypothesis ladder ranking after this round.

Constraint: M4 Pro single GPU. 50M phase 2 ~ 3.5 min/cell. Architecture
changes break weight transfer. Total budget for round 5: ~1-2 hours of
compute, plus engineering time for new code.

============================================================================
ATTACHED
============================================================================

- This prompt (round5_consult_prompt.md)
- Repo digest of branch goexplore-robustify (gitingest output)
- Go-Explore primary references:
   * Ecoffet et al. 2021 (Nature, First Return Then Explore) - canonical
   * Ecoffet et al. 2019 (NeurIPS, Go-Explore: a New Approach for
     Hard-Exploration Problems) - precursor
- Round-4 oracle eval RESULTS.md
- p2k4szzs checkpoint metadata (hparams, config used to train)

============================================================================
KEY FILES
============================================================================

ocean/osrs/encounters/encounter_inferno.h
  - InfernoState struct, encounter mechanics, oracle override hook,
    inf_oracle_pick_target_slot, D-deep tail counters

ocean/osrs_inferno/binding.c
  - my_init env config plumbing, terminal-handler death-cause attribution,
    Log surface

src/archive.h
  - Archive struct, archive_sample, frontier_mode logic

src/phase2_curriculum.h
  - Phase2Context, demo replay, cursor gate, BC interface

src/metal_pufferlib.mm
  - PuffeRL struct, archive_explore_impl, phase2 wiring, the inner
    rollout callback (net_callback_wrapper)

experiments/heavy_research_phase2_v4/oracle_eval/
  - E0/E1/E2/E3 .json + .log + RESULTS.md

experiments/heavy_research_phase2_v4/sweeps/inferno_c_base.ini
  - Current hparams + reward shape (v3, e3vyhuh9 hparams)
```

---
