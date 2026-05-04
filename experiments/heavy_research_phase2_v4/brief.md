# Heavy research agent v4 brief — phase 2 ship/no-ship verdict + next direction

## 1. Objective

Stage A screening sweep is complete. Phase 2 (Go-Explore backward curriculum) loses decisively against the same proposal + plain PPO continue control. We need direction on what to try next: stop curriculum work, retry phase 2 under different conditions, or pivot to a different research thread.

## 2. System context

- Project: PufferLib Metal training, OSRS Inferno (Zuk-only, start_wave=69) on M4 Pro
- Proposal checkpoint: `cdevk9pk/0000000029982720.bin` (30M scratch PPO, ret_normal=1.07, score_normal=0.090) — frozen artifact, sha256 `bd0215ca4549...`
- Two arms tested per the v3 protocol you locked:
  - **baseline**: load proposal → PPO continue 50M steps, 13 PPO hparams swept by Protein
  - **phase2**: load proposal → PPO continue 50M steps + Go-Explore backward curriculum (BC off, bc_coef=0), 13 PPO + 7 phase 2 hparams swept
- Both arms target `episode_return_normal` (return averaged over normal/random-start episodes only — apples-to-apples scoring)
- Sweep configs frozen in `experiments/heavy_research_phase2_v3/sweeps/{baseline,phase2_curriculum}_v1.ini`
- Demos: 256 v2 multi-snapshot demos at `/tmp/inferno_phase2_v3/curriculum_demos`, q range 0.377–0.469

## 3. Current issue / verdict

**Stage A complete: 80 baseline + 89 phase 2 trials. Phase 2 LOSES screening on every gate.**

Final RLiable bootstrap (n=all per arm, 10000 resamples):
- baseline IQM 3.51, max 7.31
- phase 2 IQM 2.37, max 6.15
- **delta_iqm = -1.14**, 95% CI **[-1.46, -0.77]** (entirely negative)
- **PI(phase2 > baseline) = 0.276**, 95% CI [0.20, 0.36] (need ≥0.70 to pass)
- **rel IQM lift = -32.4%**
- screening gate FAIL, small-n gate FAIL → no stage B

Top-K stats:
- baseline top-10 IQM 6.30
- phase 2 top-10 IQM 3.99

**Critical secondary finding** — Protein converged on phase 2 configs that minimize curriculum use:
- All 5 best phase 2 configs hit `phase2_normal_start_frac = 0.60` (the sweep ceiling)
- Most hit `phase2_backstep_ticks = 16` (the sweep ceiling — biggest cursor moves)
- The optimizer is trying to escape the curriculum. If we'd allowed nsf>0.6, it would have gone higher.

The implication: **phase 2 is harmful across the operating range**, not just at unlucky hparams. The optimizer minimized phase 2 exposure within the allowed bounds.

## 4. What we tried (and what we found out along the way)

**Sweep methodology** (matched your v3 protocol):
- Same proposal both arms ✓
- Fresh log dir per arm so Protein observations don't cross-contaminate ✓
- Identical 50M total_timesteps, max_runs=80 ✓
- All trials ran to completion post-fix (see below) ✓

**A real bug surfaced and was fixed mid-sweep** (commit `d2dc2dc34`):
- Phase 2 workers had a ~50% crash rate at ~92-97% of training. Crashes were `BUG IN CLIENT OF LIBMALLOC: memory corruption of free block` (libmalloc detector aborting), surfacing in `puf_eval_log` Python dict allocation.
- Root cause: `puf_eval_log` ([src/metal_bindings.mm:163](src/metal_bindings.mm)) created a C `Dict` with capacity 32, but inferno's `my_log` ([ocean/osrs_inferno/binding.c:730](ocean/osrs_inferno/binding.c)) writes 33 keys when phase 2 is active (`n_normal>0` AND `n_snapshot>0` adds 4+4 split keys). Baseline never tripped it (snapshot keys absent, 29 keys, fits). The `assert(dict->size < dict->capacity)` in `dict_set` is a no-op in release builds, so the overflow corrupted heap silently.
- Fix: bump capacity to 128, replace assert with `abort()` so future overflows are caught at the source.
- Also fixed two latent phase 2 data races discovered during diagnosis (per-env splitmix64 RNG instead of shared `ctx->rng`; `__atomic_fetch_add` on `demo_attempts/demo_successes`). These were corrupting cursor stats but not heap.
- Sweep main wedge: workers dying without posting to `result_queue` hung `Queue.get()` forever. Added 300s timeout + try/except wrapper in `_train`.
- Post-fix verdict (n=67 finished phase 2 only, 0 crashes): IQM 2.82 vs baseline 3.51 — same ship/no-ship answer. The bug didn't bias the result.

**Configs tested**:
- Baseline best: `um0rbblw` ret_normal=7.31 (lr=5e-3 ceiling, ent_coef=0.003, gamma=0.99999 ceiling, replay_ratio=0.75 ceiling, gae_lambda=0.98 ceiling)
- Phase 2 best: `p1hl132v` ret_normal=6.15 (lr=5e-3, ent=0.032, gamma=0.99996, **nsf=0.60 ceiling, max_demos=128, promote_rate=0.15, demote_rate=0.16, backstep_ticks=8**)

## 5. Observations

- Baseline saturates the sweep search space on lr (5e-3 ceiling), gamma (0.99999 ceiling), replay_ratio (0.75 ceiling), gae_lambda (0.98 ceiling), min_lr_ratio (0.5 ceiling), and floor on clip_coef (0.05). Search space is too narrow on those knobs — top configs hitting boundaries means real optima are likely outside.
- All 80 baseline trials reached env/wins=0. Reward is harshly capped — best policy halves Zuk HP (min_zuk_hp_normal ~409) but never wins. This is consistent with the user's prior that "this is mainly a reward issue."
- Phase 2 cursor advancement DID work mechanically (phase2_cursor_max_frac approached 1.0 and phase2_cursor_mean_frac progressed through training in successful runs). The mechanism is healthy; its presence just doesn't help.
- Phase 2 top trial `p1hl132v` reached ret_normal=6.15 — within striking distance of baseline median 3.61 but well below baseline max 7.31. Even the best phase 2 trial doesn't reach baseline top-10.
- Crash rate post-fix: 0/67 finished. Heap is clean.

## 6. Constraints

- Compute: M4 Pro, ~52K SPS at standard config, 50M-step trial = ~3 min. 80 trials = ~4 hours.
- We've spent ~3 days on phase 2 v1/v2/v3 work plus this sweep. Marginal value of continued curriculum iteration is low if the fundamental approach doesn't help here.
- Phase 2 infrastructure (Phase2Demo file format, ladder builder, cursor advancement, snapshot/restore plumbing, score split metrics, sweep config plumbing) is in main and works — that's reusable for any future curriculum work.
- Single-machine, single-user — no distributed budget.

## 7. Open questions for you

1. **Stronger proposal?** Your earlier reasoning was that curriculum value-add only emerges when basic PPO is plateauing. Our 30M scratch proposal has ret_normal=1.07; baseline can push it to 7.31 in 50M more steps (so PPO is far from exhausted). Should we run a stronger proposal first (e.g., the baseline top-1 at ret_n=7.31 → continue from there) and re-test phase 2 then? Or is the search space ceiling-saturation in baseline a sign that we should fix the reward shape FIRST before any further curriculum work?

2. **Phase 2 + BC?** This sweep had bc_coef=0. Does BC enabled change your prior — could the curriculum need the BC supervised signal to compensate for the value-net split? Or is BC orthogonal and we'd see the same ship/no-ship?

3. **Cross-env validation?** The 30M-scratch proposal is OSRS-Inferno-specific. Should we redo this protocol on Zulrah (or a non-OSRS env) before declaring phase 2 a general no-go? The general claim "Go-Explore backward curriculum hurts when PPO continue is unsaturated" might or might not be env-specific.

4. **Pivot to reward design?** Baseline saturates wins=0 at the sweep optima, and the user has flagged reward as the actual bottleneck. Is the rigorous next step to redesign the inferno reward (e.g., shaping toward Zuk damage, removing terminal sparseness) and re-baseline before any further curriculum work?

5. **Search space for baseline?** Multiple sweep dimensions saturated at the boundary. Should we widen the baseline search space (lr up to 1e-2, replay_ratio up to 1.0, gamma allow 0.999999, etc.) and re-baseline before any other comparison?

## 8. Requested deliverable

Pick the highest-EV next direction and tell us:
1. Which of {stronger proposal, phase 2+BC, cross-env, reward redesign, widen baseline search} to do first
2. The methodology spec for it (similar level of detail to your v3 protocol — proposal checkpoint, sweep dimensions, success gate, n)
3. What you'd consider a positive result vs a clean no-go
4. If "stronger proposal": whether we should also widen the baseline search space first, and what the proposal-strength criterion is (e.g., "PPO continue lifts metric by <X% per 50M before we call it plateaued")
5. Anything in our methodology you'd flag as a confound that should be addressed before committing to direction (e.g., the search space ceiling-saturation, the wins=0 floor, the q range of demos, the proposal selection).

Concrete, ranked, with go/no-go gates. Be willing to recommend "stop curriculum work entirely and go fix the reward" if that's what the data argues for.
