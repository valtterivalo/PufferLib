# Round-3 heavy-agent consult prompt

Paste this with the GitHub repo attached (branch `goexplore-robustify`).

---

```
Round 3 consult on Inferno Zuk RL. We've executed your D + C protocol from
round 2, broken through the A' plateau, and hit a NEW plateau. Need
your read on what's next.

Repo: branch `goexplore-robustify` on this fork. New since last consult:
- experiments/heavy_research_phase2_v4/SESSION_R2_NIGHT.md  <-- read first, summarises everything
- experiments/heavy_research_phase2_v4/d_audit_results.json
- experiments/heavy_research_phase2_v4/c_results.txt
- experiments/heavy_research_phase2_v4/sweeps/inferno_c_base.ini
- experiments/heavy_research_phase2_v4/run_d_audit.py
- experiments/heavy_research_phase2_v4/run_c_scout.py (round 1)
- experiments/heavy_research_phase2_v4/run_c_round2.py
- experiments/heavy_research_phase2_v4/run_c_round3.py
- experiments/heavy_research_phase2_v4/run_c_longtrain.py
- experiments/heavy_research_phase2_v4/analyze_c.py
- ocean/osrs/osrs_types.h, ocean/osrs_inferno/binding.c (new tail counters)

Frontier history:
  A' v3 200M best     0.738 (v3xzk1qs)
  C round 1 best      0.765 (ggzxso9c)  50M phase 2
  C round 2 best      0.779 (p2k4szzs)  50M phase 2 from C1   <- global best
  C round 3 best      0.769 (qovt84tm)  50M phase 2 from C2
  C long-train best   0.770 (kvv4q3e8)  100M phase 2 from C2
  31 phase-2 cells run total. 0 wins.

Current best p2k4szzs (50M phase 2 from C1, nsf=0.65):
  mean score 0.779, mean min_zuk_hp 265
  73.9% eps reach <=300 HP, 47.2% <=240, 0.16% <=150
  0 wins, mean phase 2.73 (just past phase 3 boundary)

Findings tonight:
  1. D verdict was right - go-to-C gates fired hard. Phase 2 with strong
     v3 demos works well; +0.103 top-10 lift over PPO control.
  2. BC collapsed (3/4 cells stuck) as you warned, even at bc=0.001 with
     q=0.79+ demos. Use no-BC.
  3. Iterating phase 2 from new frontier each round (Go-Explore style)
     pushed score 0.738 -> 0.765 -> 0.779 in two iterations, then plateau.
  4. Round 3 (lower nsf, refreshed demos) plateau'd; 4/8 cells collapsed
     with high seed variance below nsf=0.65. Sweet spot is nsf=0.65.
  5. 100M training (vs 50M) does NOT advance frontier. Plateau is real.

Hypotheses for the plateau (rank these):
  H1 - demo set ceiling: archive_explore demos never contain wins, so
       backward curriculum cannot bootstrap past phase-3-deep-into-4
       transitions
  H2 - architecture too small (hs=256, L=3) for late-Zuk policy
  H3 - reward structure: per-step clamp [-1,1] bounds win bonus signal
       even with win_bonus_coeff=8.0
  H4 - long-horizon credit: gamma 0.99974, horizon 128 - win signal
       doesn't propagate far enough back
  H5 - phase 4 mechanics genuinely require behaviors not in our action
       space or reward landscape

Questions:

1. Of H1-H5, which is most likely the binding constraint? Specifically,
   how would you discriminate experimentally between H1 (demo) and
   H2 (capacity)?

2. archive_explore samples cells from a hash-based archive built from
   policy rollouts. It does NOT bias toward "deepest" cells - just
   coverage. For our use case (we WANT phase-4 cells), should we modify
   the explore loop to prioritize low-min-zuk-hp cells? Or use a
   different demo-generation strategy?

3. The "deeper D" (death-cause categorization, late-phase event counters)
   you listed in round 2 was not implemented (we built only the MVP
   count-based version). Is it worth building now, given we know we die
   somewhere in phase 3-4? What specific events/transitions matter most?

4. We have 6h+ of compute available before next sync. Of:
   D-deep    -- death-cause + event counters, then re-eval frontier
   B-50M     -- single arch sweep at hs=512, L=4, horizon=256
   reward-V  -- new reward variant (e.g., per-phase milestones with
                stronger weights) + 50M sweep
   demo-deep -- modify archive_explore to prioritize low-min-hp cells,
                regenerate demos, run another phase-2 round
   what's the highest-EV use of compute?

5. If you had to bet: is the next breakthrough mostly likely to come
   from architecture, demo-generation strategy, or reward redesign?

Constraint: M4 Pro single GPU. Phase 2 at 256 hidden / 50M = 3.5 min/cell.
Architecture changes don't load-transfer (incompatible weight shapes), so
B requires fresh starts (longer wall time per cell).

Please give:
  1. Ranked next direction with one-line justification.
  2. Concrete experiment plan for the top recommendation.
  3. Any code-level / methodology observations from reading the new files.
  4. Specifically: is the "iterate Go-Explore demos from frontier" loop
     fundamentally limited by the frontier policy's inability to win?
     If yes, what's the way around it?
```

---
