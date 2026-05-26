# Immutable Kernel LOC Overnight Plan

This plan is frozen after milestone 00 setup. Status updates go only in `STATUS.md`.

## Run Window

- Start: 2026-05-27 evening EEST
- Stop: 2026-05-28 09:00 EEST
- Repo: `/Users/valtterivalo/.codex/worktrees/metal-kernel-loc-night-20260527/pufferlib-metal`
- Branch: `valtteri/metal-kernel-loc-night-20260527`
- Baseline commit: `b490ce0a6`
- Parent lane: `metal-local`
- Benchmarks: `breakout` and `g2048`
- Primary optimization goal: reduce local Metal 4 kernel and backend source LOC
- Secondary optimization goal: keep or improve throughput without hurting score, determinism, or interactive use

## Kernel LOC Scope

Track these counts separately:

- MSL and Metal kernel source: `src/metal/kernels.mm`, `src/metal/shader_src.h`, and only the tensor-ops shader block in `src/metal/platform.mm`.
- Metal backend host source: `src/metal`, `tools/metal`, and `tests/metal`.
- Audit harness source under this artifact folder.

Kernel LOC wins are preferred over harness LOC wins. A change that only moves code from one kernel file to another is not a win unless it removes real duplicated logic or dead behavior.

## Final State

By the last milestone, the repo should be in one of these states:

- Accepted path: one or more reviewed Metal-owned commits that reduce kernel or backend LOC, preserve benchmark behavior, and keep the system runnable.
- Rejected path: no backend cleanup commits kept, with artifacts showing the attempted changes failed acceptance or were not meaningful.

The final status must include:

- Current git branch and commit.
- Net line-count delta for kernel scope and Metal backend scope.
- Accepted and rejected changes with rollback reason.
- For both `breakout` and `g2048`: median SPS, score trend, eval score, elapsed wall time, and exact runner path.
- Interactive smoke status and command.
- Open risks that still matter at 2026-05-28 09:00 EEST.

## Global Acceptance

Every significant backend or harness change must pass all gates below before it is kept:

- Build the Metal overlay for `breakout` and `g2048`.
- Run the milestone runner scripts, not ad hoc command lines.
- Capture stdout, stderr, config, git SHA, summary JSON, checkpoints, eval JSON, and git diff in the milestone folder.
- Compare against the prior accepted baseline, not memory.
- Keep a kernel LOC change only when repeated benchmark SPS stays within 1 percent of baseline or improves, train score does not collapse, explicit eval stays comparable, parity tests pass, and interactive use still reaches the known platform boundary or better.
- Keep a speed change only when repeated end-to-end runs show median SPS improvement of at least 3 percent and no score collapse.
- Treat SPS as throughput, not learning success. If a change touches policy math, optimizer order, sampling, RNG, rewards, observations, or training data flow, compare score versus elapsed wall time and require subagent review to classify it as safe.
- Keep no change that breaks fixed-seed determinism, checkpoint load, eval, or no-render mode.
- Block each commit on subagent code review. The root agent may fix findings, then request review again.

## Anti-Patterns

- Edit this plan after milestone 00.
- Put status updates in this plan.
- Move code around to fake LOC reduction.
- Delete a kernel fallback just because the benchmark pair does not hit it.
- Build a mock harness or fake backend runner.
- Benchmark Torch fallback and call it Metal performance.
- Compare different env configs, seeds, model sizes, or Metal flags as if they were one experiment.
- Use latest checkpoint selection for serious comparisons.
- Keep an SPS win that collapses score or worsens wall-clock-to-score.
- Keep debug logging, feature flags, fallback paths, legacy handlers, or dead code after cleanup milestones.
- Hide failed runs by truncating logs or overwriting artifact folders.
- Rely on microbenchmarks when end-to-end integration can run.
- Commit before subagent review.
- Touch upstream Puffer internals outside the Metal-owned paths unless the root cause proves the backend contract is wrong.

## Milestones

### Milestone 00: Baseline And Kernel Census

Folder: `artifacts/metal-kernel-loc-20260527/milestone-00-baseline`

Acceptance:

- Add dedicated runner scripts for `breakout` and `g2048`.
- Capture baseline end-to-end train and eval artifacts for both envs.
- Run Metal overlay surface tests.
- Record current kernel-scope LOC and backend-scope LOC.
- Identify top kernel LOC clusters before editing backend behavior.

### Milestone 01: Kernel LOC Pass

Folder: `artifacts/metal-kernel-loc-20260527/milestone-01-kernel-loc-pass`

Acceptance:

- Delete or consolidate dead, duplicated, or misleading Metal kernel code.
- Prefer real kernel/source simplification over host-only cleanup.
- Keep behavior identical by parity tests and benchmark comparison.
- Reject cleanup that slows median SPS by more than 1 percent or worsens eval.

### Milestone 02: Kernel Consolidation Pass

Folder: `artifacts/metal-kernel-loc-20260527/milestone-02-kernel-consolidation-pass`

Acceptance:

- State the root-cause hypothesis before editing.
- Consolidate repeated dispatch, shader, or argument-binding shapes only when it reduces real duplication.
- Keep only changes with repeated-run benchmark gates and clean review.
- Reject changes that hide fallback behavior or make Metal feature requirements less explicit.

### Milestone 03: Cleanup

Folder: `artifacts/metal-kernel-loc-20260527/milestone-03-cleanup`

Acceptance:

- Delete temporary logging and diagnostic code.
- Remove stale flags, unused fallback paths, old helpers, and dead code exposed by the first two passes.
- Break up or DRY code only when it reduces real complexity.
- Re-run both benchmarks and interactive smoke.

### Milestone 04: Second Kernel LOC Pass

Folder: `artifacts/metal-kernel-loc-20260527/milestone-04-second-kernel-loc-pass`

Acceptance:

- Try the next ranked kernel LOC candidate only if previous passes left measured duplication.
- Keep the change only if it clears the same repeated-run gate.
- Do not broaden scope to unrelated envs or upstream trainer code.

### Milestone 05: Final Audit

Folder: `artifacts/metal-kernel-loc-20260527/milestone-05-final-audit`

Acceptance:

- Run the full accepted benchmark suite one final time.
- Run parity and overlay tests.
- Run interactive smoke or record the exact blocking platform failure.
- Produce final status in `STATUS.md`.
- Leave the worktree clean except for deliberate artifacts and accepted commits.
