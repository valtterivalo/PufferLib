# Round-4 heavy-agent consult prompt

Paste this with the GitHub repo attached on `goexplore-robustify`.

---

```
Round 4 consult on Inferno Zuk RL. We've executed your r3 plan: built
frontier-biased archive sampler, regenerated demos with floor=0.80 (G1)
and floor=0.85 (G2), built D-deep death-cause counters, ran C4 scout.

H1 (demo-generation ceiling) is now weakened. Frontier sampling does NOT
break the 0.78 plateau. D-deep flags add-handling as the dominant failure
mode. Need your read on which lever to pull next.

Repo: branch `goexplore-robustify`. New since last consult:
- ocean/osrs/encounters/encounter_inferno.h  (D-deep state fields)
- ocean/osrs/osrs_types.h                    (D-deep Log fields)
- ocean/osrs_inferno/binding.c               (D-deep accumulators + surface)
- src/archive.h                              (frontier sampler)
- src/metal_bindings.mm                      (frontier exposure)
- src/metal_pufferlib.mm                     (frontier wiring)
- scripts/run_archive_explore.py             (frontier CLI flags)
- experiments/heavy_research_phase2_v4/run_c4_scout.py
- experiments/heavy_research_phase2_v4/c4_logs/  (12 training logs)

Frontier sampler implementation (heavy_research_phase2_v4 commit 2523814bf):
  90% of samples weighted q^power / sqrt(chosen+1) for cells with q >= q_floor
  10% fall back to standard count-decay sampler

D-deep counters (per-episode tail behavior, conditional means):
  ticks_after_300_normal     : ticks survived after first crossing 300 HP
  ticks_after_240_normal     : same for 240
  ticks_after_150_normal     : same for 150
  damage_after_300/240/150_normal: HP of fresh damage to Zuk after crossing
  frac_died_with_jad_alive_normal      : episode died w/ Jad still alive
  frac_died_with_healer_alive_normal   : episode died w/ Zuk-healer alive
  frac_died_with_set_alive_normal      : episode died w/ any non-zuk combat NPC alive

D-deep diagnostic on p2k4szzs (frontier policy, 1731 normal eps):
  score 0.720, fr<=300 0.44, fr<=240 0.26, fr<=150 0.0006
  ticks_after_300 = 14.8   damage_after_300 = 94 HP
  ticks_after_240 = 9.5    damage_after_240 = 56 HP
  ticks_after_150 = 3.0    damage_after_150 = 34 HP
  frac_died_with_jad_alive = 0.82
  frac_died_with_healer_alive = 0.26
  frac_died_with_set_alive = 0.99

Strong signal: late-fight death is dominated by adds, not damage failure.
Policy crosses 300 HP, has 94 HP of damage budget left, dies with Jad alive
in 82% of cases.

C4 scout (12 cells x 50M from p2k4szzs at nsf=0.65, no BC):
  arm A baseline (standard sampler, max q 0.804):
    best 0.761  median 0.722  (all 4 cells trained)
  arm B g1 (frontier f=0.80 p=4, max q 0.822):
    best 0.705  median 0.371  (3 of 4 collapsed)
  arm C g2 (frontier f=0.85 p=8, max q 0.831, 300iter, larger archive):
    best 0.753  median 0.745  (most reliable across seeds)

  Round 2 best was 0.779. None of C4 arms beat it.
  G1 frontier sampling caused training collapse in 3/4 seeds (worse than
  baseline). G2 with stricter floor + more iterations was stable but no
  ceiling improvement.

  died_with_jad fraction across C4 cells that reached late-game:
    0.65 to 0.93. Consistent across all arms.

  Frontier history (still capped):
    A' v3 200M       0.738
    C round 1        0.765
    C round 2        0.779   <- still global best
    C round 3        0.769
    C long-train     0.770
    B0 horizon=256   0.763
    C4 best          0.761

Conclusion: frontier-biased demos didn't fix it. Policy plays the same way
regardless of which deep state it restores from. The add-priority failure
is structural to the trained policy, not a demo coverage issue.

Hypothesis ranking (revised):
  H5 - missing add-priority behavior - leading
       Policy reaches phase 4 but never kills Jad/healers fast enough.
       Same behavior across demos confirms it's policy-resident.
  H3 - reward shape doesn't incentivize add-killing in late phase
       Current v3 reward: damage_reward + win_bonus. No specific signal
       for "kill Jad" or "kill healer". Reward clamp [-1,1] limits any
       milestone bonus.
  H2 - capacity limit
       hs=256 / L=3 may be too small for the late-fight branching
       behavior (jad-active vs healer-active vs different add combinations
       require different priority orderings).
  H1 - demo ceiling - weakened
       C4 evidence shows it's not the binding constraint.

Questions:

1. Given the add-handling diagnosis, what's the right reward variant?
   Options I considered:
   v6a: -X penalty per tick with Jad alive after crossing 300 HP
   v6b: +Y bonus on Jad kill after crossing 300 HP
   v6c: scaled damage_reward when applied to Jad/healers (already have
        tag_reward_coeff for healers; could add jad_reward_coeff)
   v6d: completion-style: lose damage_reward unless add-clear after 300

2. Architecture vs reward priority. Heavy agent r3 had B as last priority.
   Should it move up given H1 weakened? Specifically:
     - B-transfer: distill p2k4szzs into hs=512 / L=4 / horizon=256
     - B-fresh: train new arch from scratch with phase 2 + v3 reward
   What's the cheapest valid B test that could discriminate H2?

3. We're not using BC (it collapsed in C round 1 even with q=0.79+ demos).
   But BC is the natural way to teach add-priority from demos. If we could
   collect demos that DEMONSTRATE add-priority (e.g., always-kill-Jad-first),
   would BC still collapse? What tunable would change that?

4. Are there any free wins from the death-cause data we should mine before
   committing compute? E.g., is there an obvious script/heuristic to test?
   The full D-deep table is in the runner output and aprime_results.txt-style
   files we'll write up.

5. Sanity check on the frontier sampler implementation: src/archive.h
   archive_sample() with frontier_mode picks (1-eps) of the time from
   cells weighted q^power / sqrt(chosen+1) when q >= q_floor. Was the
   collapse in B-g1 (3/4 seeds at score ~0.3) likely due to:
     a. demos really being worse (max q 0.822 < baseline 0.804+ ... wait
        baseline was 0.804 max so g1 wasn't lower)
     b. some pathology in the specific demo set / leaf distribution
     c. training instability that needs more seeds to wash out

Please give:
  1. Top recommendation (specific reward variant or architecture probe).
  2. Concrete experiment plan + gates.
  3. Code-level observations from the C4 scout, D-deep results, or sampler
     implementation.
  4. Any sanity check we should run before committing more compute.

Constraint: M4 Pro single GPU. 50M phase 2 = ~3.5 min/cell. Architecture
changes break weight transfer.
```

---
