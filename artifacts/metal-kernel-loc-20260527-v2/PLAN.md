# Immutable Kernel LOC Overnight Plan V2

This plan is frozen after milestone 00 setup. Status updates go only in `STATUS.md`.

## Run Window

- Start: 2026-05-27 evening EEST
- Stop: 2026-05-28 09:00 EEST
- Repo: `/Users/valtterivalo/.codex/worktrees/metal-kernel-loc-night-20260527-v2/pufferlib-metal`
- Branch: `valtteri/metal-kernel-loc-night-20260527-v2`
- Baseline commit: `b490ce0a6`
- Parent lane: `metal-local`
- Benchmarks: `breakout` and `g2048`
- Main throughput point: Metal training with CPU inference overlap, matching the current local fast path
- Rollout coverage point: Metal training and eval with GPU inference enabled as a smoke gate
- Primary goal: reduce local Metal 4 kernel source LOC
- Secondary goal: reduce Metal backend LOC and keep or improve wall-clock-to-score

## Kernel LOC Scope

Track these counts separately:

- Kernel scope: `src/metal/shader_src.h`, `src/metal/kernels.mm`, and the tensor-ops shader block in `src/metal/platform.mm`.
- Backend scope: tracked files under `src/metal`, `tools/metal`, and `tests/metal`.
- Harness scope: tracked files under this artifact folder.

Kernel LOC wins outrank host-only cleanup. Moving code from one kernel file to another is not a win unless it removes duplicated logic, dead behavior, or repeated declarations.

## Final State

By the last milestone, the repo should be in one of these states:

- Accepted path: one or more reviewed Metal-owned commits that reduce kernel or backend LOC, preserve benchmark behavior, and keep the system runnable.
- Rejected path: no backend cleanup commits kept, with artifacts showing the attempted changes failed acceptance or were not meaningful.

The final status must include:

- Current git branch and commit.
- Net line-count delta for kernel scope and backend scope.
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
- Use two main train/eval runs per env and enforce the median SPS gate with the milestone check script.
- Run a real GPU-inference train/eval smoke for both envs so CPU-overlap benchmarks cannot hide broken Metal rollout inference.
- Keep a kernel LOC change only when repeated benchmark SPS stays within 1 percent of baseline or improves, train score does not collapse, explicit eval stays comparable, parity tests pass, and interactive use still reaches the known platform boundary or better.
- Keep a speed change only when repeated end-to-end runs show median SPS improvement of at least 3 percent and no score collapse.
- Treat SPS as throughput, not learning success. If a change touches policy math, optimizer order, sampling, RNG, rewards, observations, or training data flow, compare score versus elapsed wall time and require subagent review to classify it as safe.
- Keep no change that breaks fixed-seed determinism, checkpoint load, eval, or no-render mode.
- Block each commit on subagent code review. The root agent may fix findings, then request review again.

## Anti-Patterns

- Edit this plan after milestone 00.
- Put status updates in this plan.
- Cherry-pick previous work and call it accepted without rerunning the current branch.
- Count rejected audit commits as source improvement.
- Move code around to fake LOC reduction.
- Delete a kernel fallback just because `breakout` and `g2048` do not hit it.
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

Folder: `artifacts/metal-kernel-loc-20260527-v2/milestone-00-baseline`

Acceptance:

- Add dedicated runner scripts for `breakout` and `g2048`.
- Capture baseline end-to-end train and eval artifacts for both envs.
- Run Metal overlay surface tests.
- Record current kernel-scope LOC and backend-scope LOC.
- Identify top kernel LOC clusters before editing backend behavior.

### Milestone 01: Port Accepted V1 Kernel LOC

Folder: `artifacts/metal-kernel-loc-20260527-v2/milestone-01-port-v1-kernel-loc`

Acceptance:

- Port only source changes from accepted v1 commits that reduce kernel or backend LOC.
- Do not port rejected audit commits or status-only commits as source improvements.
- Revalidate on this branch with repeated benchmark runs, parity, overlay, and interactive smoke.
- Reject any port that misses the current-branch benchmark gates.

### Milestone 02: Kernel Consolidation

Folder: `artifacts/metal-kernel-loc-20260527-v2/milestone-02-kernel-consolidation`

Acceptance:

- State the root-cause hypothesis before editing.
- Consolidate repeated dispatch, shader, or argument-binding shapes only when it reduces real duplication.
- Keep only changes with repeated-run benchmark gates and clean review.
- Reject changes that hide fallback behavior or make Metal feature requirements less explicit.

### Milestone 03: Cleanup

Folder: `artifacts/metal-kernel-loc-20260527-v2/milestone-03-cleanup`

Acceptance:

- Delete temporary logging and diagnostic code.
- Remove stale flags, unused fallback paths, old helpers, and dead code exposed by the first two passes.
- Break up or DRY code only when it reduces real complexity.
- Re-run both benchmarks and interactive smoke.

### Milestone 04: Second Kernel LOC Pass

Folder: `artifacts/metal-kernel-loc-20260527-v2/milestone-04-second-kernel-loc`

Acceptance:

- Try the next ranked kernel LOC candidate only if previous passes left measured duplication.
- Keep the change only if it clears the same repeated-run gate.
- Do not broaden scope to unrelated envs or upstream trainer code.

### Milestone 05: Final Audit

Folder: `artifacts/metal-kernel-loc-20260527-v2/milestone-05-final-audit`

Acceptance:

- Run the full accepted benchmark suite one final time.
- Run parity and overlay tests.
- Run interactive smoke or record the exact blocking platform failure.
- Produce final status in `STATUS.md`.
- Leave the worktree clean except for deliberate artifacts and accepted commits.
