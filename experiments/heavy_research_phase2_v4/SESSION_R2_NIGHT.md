# Phase 2 v4 r2 night session — summary

Date: 2026-05-04 evening through 2026-05-05 ~03:15 EEST.

## Headline

**Phase 2 works.** Pushed Inferno score frontier from 0.738 (A' v3 200M best)
to 0.779 (C round 2 best, p2k4szzs) in a single night using v3 reward + new
demos generated from frontier policies. **0 wins across all 31 phase-2 cells**
— plateau at ~0.77-0.78 confirmed across 3 iterations + 1 long-training run.

Next lever: heavy-agent round 3 consult, then probably B (architecture scaleup)
or D-deep (death-cause instrumentation).

## What ran tonight (chronological)

1. **D-audit** (env-side tail counters + python runner)
   - Added `count_min_hp_le_300/240/150_normal` and `frac_normal` to env Log
   - Ran on P0–P4 v3 200M policies × 20k normal-start episodes each
   - **Verdict: GO-TO-C** — P0 had 9135/20k eps ≤300 HP, 5292 ≤240, 62 ≤150
   - Heavy agent's "go to C immediately" gates fired on all three criteria

2. **Demo extraction** via existing `archive_explore` infrastructure
   - 100 iterations × 2 seeds on P0 → 64 demos at q=0.79-0.83 (well above
     heavy agent's q≥0.75 preferred filter)
   - 5 seconds wall time, no new code needed

3. **C round 1** — 12 cells × 50M
   - C0 (PPO continuation control, 4 seeds): best 0.588, top10_med 0.578
   - C1 (phase 2 no BC, nsf 0.75/0.85): best 0.765, top10_med 0.681 (+0.103
     over C0 — heavy agent's gate fires hard)
   - C2 (phase 2 + BC): collapsed in 3/4 cells (heavy agent's BC warning
     validated). Only bc=0.005 s1 trained.
   - **Best cell: ggzxso9c (nsf=0.75 s1) score 0.7649**

4. **C round 2** — 8 cells × 50M, demos refreshed from ggzxso9c (q=0.842-0.845)
   - nsf in {0.65, 0.70, 0.75, 0.80} × 2 seeds, no BC
   - **Best: p2k4szzs (nsf=0.65 s1) score 0.7787** (current global best)
   - Both nsf=0.65 seeds hit ~0.78 — robust signal, not lottery
   - Lower nsf won this round (vs nsf=0.75 winning round 1) — better demos
     allow more aggressive phase 2 weighting

5. **C round 3** — 8 cells × 50M, demos from p2k4szzs (q=0.795-0.868)
   - nsf in {0.50, 0.55, 0.60, 0.65} × 2 seeds
   - Best: qovt84tm (nsf=0.55 s2) score 0.769 — **frontier did not advance**
   - 4 of 8 cells collapsed to 0.33-0.50 — phase 2 unstable below nsf=0.65
   - Plateau signal

6. **C long-train** — 3 seeds × 100M from p2k4szzs at nsf=0.65
   - Best: kvv4q3e8 (s1) score 0.770 — plateau confirmed
   - Notable: yo5qtuvs (s2) had 1.4% of eps below 150 HP (highest seen) but
     mean only 0.717
   - Doubling training time does NOT advance frontier

7. **B0 horizon=256** — 3 seeds × 50M from p2k4szzs at horizon=256
   - Best: jzz50pdq (s2) score 0.763 — also below round-2 frontier
   - Doubling rollout horizon does NOT advance frontier
   - Plateau is robust to knob changes (iter count, training time, horizon)

## Frontier history

| Stage | Best policy | Score | min_hp | wins | Notes |
|-------|-------------|-------|--------|------|-------|
| A (proposal) | cdevk9pk | 0.50ish | ~600 | 0 | Old 30M baseline |
| R1 v3 sweep | kvuqyvh9 | 0.709 | 349 | 0 | 50M, single-seed lottery |
| A' v3 200M | v3xzk1qs (P0) | 0.738 | 321 | 0 | 200M v3 continuation |
| C round 1 | ggzxso9c | 0.765 | 282 | 0 | 50M phase 2 from P0 |
| **C round 2** | **p2k4szzs** | **0.779** | **265** | **0** | **50M phase 2, current best** |
| C round 3 | qovt84tm | 0.769 | 277 | 0 | 50M phase 2 |
| C long-train | kvv4q3e8 | 0.770 | 277 | 0 | 100M phase 2 |
| B0 horizon=256 | jzz50pdq | 0.763 | 284 | 0 | 50M phase 2, horizon=256 |

## What's at the ceiling

p2k4szzs (current frontier):
- Mean score 0.779, mean min_zuk_hp 265 (≈ 22% of Zuk HP remaining)
- 73.9% of episodes reach ≤300 HP
- 47.2% reach ≤240 HP
- 0.16% reach ≤150 HP
- 0 wins out of ~thousands of episodes
- Mean phase reached 2.73 (between phase 3 and phase 4 / kill)

The policy reaches phase 3 reliably and crosses the 300 HP boundary half the
time, but cannot reliably finish off Zuk. The few episodes that get below
150 HP suggest the kill is *possible* but not learnable with current setup.

## Why phase 2 hit a ceiling

Best guesses (heavy-agent should weigh in):
1. **Demo set ceiling** — every iteration of demos from current frontier
   policies still doesn't contain real wins. Backward curriculum can only
   lift you to the demo's leaf quality.
2. **Capacity** — hs=256, L=3 may not represent late-Zuk policy adequately
3. **Reward structure** — v3 has win_bonus=8.0 but reward clamp [-1,1]
   bounds per-step gradient. Late-fight reward signal is weak.
4. **Long-horizon credit** — gamma 0.99974 with horizon 128 may not
   propagate the win signal far enough.

## What's NOT been tried

- **B: architecture scaleup** (heavy agent's secondary direction)
- **D-deep**: per-episode death-cause categorization, late-phase event
  counters (set/Jad/healer/shield/off-prayer split). Heavy agent listed
  these — we built only the count-based MVP D
- **Win-targeted reward variants**: we used v3 throughout. A reward that
  specifically encourages crossing the 300/200/100 boundaries (not just
  damage/win) might unblock the final phase
- **Extracting demos from late-game ESCAPE states**: archive_explore
  starts from c_reset; demos may not reach the deepest cells

## Recommended next move

**Round-3 heavy-agent consult.** Pre-drafted prompt at
`round3_consult_prompt.md` (this dir).

Key questions for the agent:
1. Plateau at ~0.78 — is this capacity, demo quality, or reward?
2. Should B (architecture) come before more D-deep instrumentation?
3. Demo set never contains wins → does archive_explore need a "deeper"
   exploration mode to push to phase 4?
4. Is there a reward variant (per-phase milestone redux, win-bias) worth
   trying before architecture?

## Files added/changed tonight

Source (committed to goexplore-robustify):
- `ocean/osrs/osrs_types.h` — 4 new Log fields for tail counters
- `ocean/osrs_inferno/binding.c` — terminal-block tail-counter updates,
  3 new dict_set surfaces (frac_<=300/240/150) plus frac_normal

Experiments (gitignored, force-added):
- `experiments/heavy_research_phase2_v4/run_d_audit.py`
- `experiments/heavy_research_phase2_v4/d_audit_results.json`
- `experiments/heavy_research_phase2_v4/sweeps/inferno_c_base.ini`
- `experiments/heavy_research_phase2_v4/run_c_scout.py` (round 1)
- `experiments/heavy_research_phase2_v4/run_c_round2.py`
- `experiments/heavy_research_phase2_v4/run_c_round3.py`
- `experiments/heavy_research_phase2_v4/run_c_longtrain.py`
- `experiments/heavy_research_phase2_v4/analyze_c.py`
- `experiments/heavy_research_phase2_v4/c_results.txt`
- `experiments/heavy_research_phase2_v4/c_logs/` (round 1)
- `experiments/heavy_research_phase2_v4/c_round2_logs/`
- `experiments/heavy_research_phase2_v4/c_round3_logs/`
- `experiments/heavy_research_phase2_v4/c_longtrain_logs/`

Demo binaries (NOT committed, ~276MB, regenerable in 5 sec):
- `experiments/heavy_research_phase2_v4/demos_p0/seed_{42,43}/demos/` (P0)
- `experiments/heavy_research_phase2_v4/demos_c1/seed_{42,43}/demos/` (ggzxso9c)
- `experiments/heavy_research_phase2_v4/demos_c2/seed_{42,43}/demos/` (p2k4szzs)

Commits pushed to `goexplore-robustify`:
- `bd72b82c1` D verdict
- `368fb68de` C scaffolding
- `3141d806e` C round 1 (phase 2 works)
- `ba8e24880` C round 2 (frontier 0.779)
- `fef95bf2f` C round 3 + longtrain runner
- `d0ddfd613` C long-train results

## Frontier checkpoint (use as proposal for next experiments)

```
checkpoints/osrs_inferno/p2k4szzs/0000000049971200.bin
```

Score 0.7787, min_zuk_hp 265, with v3 reward at e3vyhuh9 hparams.
