# Immutable Overnight Plan

This plan is frozen after milestone 00 setup. Status updates go only in `STATUS.md`.

## Run Window

- Start: 2026-05-26 01:12 EEST
- Stop: 2026-05-27 09:00 EEST
- Repo: `/Users/valtterivalo/.codex/worktrees/b7d8/pufferlib-metal`
- Branch: `valtteri/metal-overnight-opt`
- Baseline commit: `d649e493a`
- Benchmarks: `breakout` and `g2048`
- Optimization goals: reduce local Metal backend lines of code and improve speed without breaking determinism, learnability, or interactive use

## Final State

By the last milestone, the repo should be in one of these states:

- Accepted path: one or more small Metal-owned commits that lower backend LOC or improve benchmark speed, with benchmark artifacts showing no determinism, learnability, or interactive usability regression.
- Rejected path: no backend optimization commits kept, with artifacts showing the attempted changes failed acceptance or were not statistically meaningful.

The final status must include:

- Current git branch and commit.
- Net line-count delta for `src/metal`, `tools/metal`, and `tests/metal`.
- Accepted and rejected changes with rollback reason.
- For both `breakout` and `g2048`: median SPS, score trend, final eval result, and exact runner path.
- Interactive smoke status and command.
- Open risks that still matter at 2026-05-27 09:00 EEST.

## Global Acceptance

Every significant backend or harness change must pass all gates below before it is kept:

- Build the Metal overlay for `breakout` and `g2048`.
- Run the milestone runner scripts, not ad hoc command lines.
- Capture stdout, stderr, config, git SHA, summary JSON, checkpoints, and eval JSON in the milestone folder.
- Compare against the prior accepted baseline, not against memory.
- Keep a speed change only when repeated end-to-end runs show a median SPS improvement of at least 3 percent and no environment score collapse.
- Keep a LOC-only change only when benchmark SPS stays within 1 percent of baseline or improves, parity tests pass, and interactive use still works.
- Keep no change that breaks fixed-seed determinism, checkpoint load, eval, or no-render mode.
- Block each commit on subagent code review. The root agent may fix findings, then request review again.

## Anti-Patterns

- Edit this plan after milestone 00.
- Put status updates in this plan.
- Build a mock harness or fake backend runner.
- Benchmark Torch fallback and call it Metal performance.
- Compare different env configs, seeds, model sizes, or Metal flags as if they were one experiment.
- Use latest checkpoint selection for serious comparisons.
- Keep a speed change that does not clear the statistical gate.
- Keep debug logging, feature flags, fallback paths, legacy handlers, or dead code after cleanup milestones.
- Hide failed runs by truncating logs or overwriting artifact folders.
- Rely on microbenchmarks when end-to-end integration can run.
- Commit before subagent review.
- Touch upstream Puffer internals outside the Metal-owned paths unless the root cause proves the backend contract is wrong.

## Milestones

### Milestone 00: Baseline And Harness

Folder: `artifacts/metal-overnight-20260526/milestone-00-baseline`

Acceptance:

- Add dedicated runner scripts for `breakout` and `g2048`.
- Capture baseline end-to-end train and eval artifacts for both envs.
- Run Metal overlay surface tests.
- Record current LOC for Metal-owned paths.
- Do not change backend behavior.

### Milestone 01: LOC Pass

Folder: `artifacts/metal-overnight-20260526/milestone-01-loc-pass`

Acceptance:

- Delete or consolidate dead, duplicated, or misleading Metal-owned code.
- Keep behavior identical by parity tests and benchmark comparison.
- Reject cleanup that slows median SPS by more than 1 percent.

### Milestone 02: Hot Path Pass

Folder: `artifacts/metal-overnight-20260526/milestone-02-hot-path-pass`

Acceptance:

- State the root-cause hypothesis before editing.
- Measure the actual end-to-end hot path on both envs.
- Keep only changes with repeated-run median SPS improvement of at least 3 percent.
- Reject changes that hurt either benchmark unless the faster env is the explicit target and the plan says so. This plan does not say so.

### Milestone 03: Cleanup

Folder: `artifacts/metal-overnight-20260526/milestone-03-cleanup`

Acceptance:

- Delete temporary logging and diagnostic code.
- Remove stale flags, unused fallback paths, old helpers, and dead code exposed by the first two passes.
- Break up or DRY code only when it reduces real complexity.
- Re-run both benchmarks and interactive smoke.

### Milestone 04: Second Hot Path Pass

Folder: `artifacts/metal-overnight-20260526/milestone-04-second-hot-path-pass`

Acceptance:

- Try the next ranked optimization only if milestone 02 left a measured bottleneck.
- Keep the change only if it clears the same repeated-run gate.
- Do not broaden scope to unrelated envs or upstream trainer code.

### Milestone 05: Final Audit

Folder: `artifacts/metal-overnight-20260526/milestone-05-final-audit`

Acceptance:

- Run the full accepted benchmark suite one final time.
- Run parity and overlay tests.
- Run interactive smoke or record the exact blocking platform failure.
- Produce final status in `STATUS.md`.
- Leave the worktree clean except for deliberate artifacts and accepted commits.
