# Overnight Status

## 2026-05-26 01:12 EEST

- Created branch `valtteri/metal-overnight-opt` from detached `d649e493a`.
- Created heartbeat automation through 2026-05-27 09:00 EEST.
- Started milestone 00 setup.
- Root-cause hypothesis for setup: we cannot trust optimization claims until benchmark commands and captured artifacts are fixed and repeatable.

## 2026-05-26 01:18 EEST

- Added immutable plan, separate status log, milestone folders, and reusable benchmark runner scripts.
- Added explicit Metal-compatible `g2048` overrides after read-only agent review found the default config enables CUDA-only curriculum.
- Validated runner syntax, Python syntax, and whitespace with `bash -n`, `py_compile`, and `git diff --check`.

## 2026-05-26 01:19 EEST

- First `breakout` baseline failed during `tools/metal/build.sh` CPU prebuild.
- Root cause: `vecenv.h` includes `cuda_runtime_api.h`, and the CPU prebuild did not include `src/metal`, where the local compatibility header lives.
- Patched `tools/metal/build.sh` to pass `-Isrc/metal` into the CPU prebuild only.

## 2026-05-26 01:20 EEST

- Second `breakout` baseline built and trained to 4.2M steps.
- Runner failed before eval because training produced two checkpoints, while the harness expected one.
- Patched runner to write `checkpoints.txt` and `selected-checkpoint.txt`, then eval the highest step checkpoint by filename.

## 2026-05-26 01:21 EEST

- Third `breakout` baseline built and trained to 4.2M steps.
- No-render eval failed during backend construction because the wrapper forced `horizon = 1` but left `minibatch_size = 65536`.
- Patched no-render eval to set `train.minibatch_size = vec.total_agents`, preserving the backend divisibility invariant.

## 2026-05-26 01:23 EEST

- Clean `breakout` baseline completed: 4.2M train steps, 2.04M SPS, training score 2.46, explicit checkpoint eval score 0.0.
- Clean `g2048` baseline completed: 262K train steps, 347.6K SPS, training score 97.86, explicit checkpoint eval score 49.43.
- Overlay hygiene test failed because the new audit artifact root was outside the allowed Metal-owned path set.
- Patched `tests/metal/test_overlay_surface.py` to allow only `artifacts/metal-overnight-20260526/` in addition to the existing Metal-owned code paths.

## 2026-05-26 01:27 EEST

- Subagent review found three milestone 00 harness issues: `--slowly` could benchmark Torch fallback, the artifact whitelist was too broad, and `run-suite.sh` was not cwd-stable.
- Patched the benchmark runner to reject `--slowly`.
- Tightened overlay surface test to allow only the known overnight artifact files, not the whole artifact tree.
- Patched all `run-suite.sh` scripts to `cd` to the repo root before running pytest.

## 2026-05-26 01:28 EEST

- Subagent verification found an argparse abbreviation bypass: `--slow` and `--slo` still parse as `--slowly`.
- Patched runner to reject the full `--slo*` prefix.

## 2026-05-26 01:30 EEST

- Found that `run-suite.sh` called the parity fixture through pytest even though `test_native_backend_parity.py` is a CLI fixture.
- Patched all suite scripts to run `tests/metal/test_native_backend_parity.py --backend metal --write-json ...` directly, then run the overlay surface pytest.

## 2026-05-26 01:31 EEST

- Milestone 00 committed as `d699d8db3` with subagent review clear.
- Starting milestone 01 LOC pass.
- Root-cause hypothesis: the scan dispatch helpers duplicate host binding code for forward/backward and fp32/fp16 variants, so a tagged helper can reduce lines without changing MSL kernels or dataflow.

## 2026-05-26 01:33 EEST

- Refactored scan dispatch helpers in `src/metal/kernels.mm`.
- Net diff for the file: 25 insertions, 58 deletions.
- First milestone 01 suite attempt failed in the benchmark runner because the new `--slo*` guard expanded an empty bash array under `set -u`.
- Patched the guard to check array length before iterating.

## 2026-05-26 01:34 EEST

- Milestone 01 suite passed.
- `src/metal/kernels.mm` line count changed from 1652 to 1619.
- `breakout`: 2.04M baseline SPS to 2.25M milestone SPS, score 2.457 to 2.563, eval score stayed 0.0.
- `g2048`: 347.6K baseline SPS to 355.3K milestone SPS, score 97.855 to 98.889, eval score 49.43 to 49.48.
- Decision: keep the LOC refactor pending subagent code review. The benchmark gate shows no regression.

## 2026-05-26 01:36 EEST

- Subagent review found benchmark attribution gap: milestone artifacts recorded only `HEAD`, not the dirty refactor diff.
- Patched runner to capture `git-status.txt`, `git-diff.patch`, and `git_diff_sha256` in metadata for each run.
- Corrected stale LOC and numstat after wrap-only formatting.

## 2026-05-26 01:38 EEST

- Reran milestone 01 suite with dirty-diff attribution.
- `breakout`: 2.04M baseline SPS to 2.20M milestone SPS, score 2.457 to 2.262, eval score stayed 0.0.
- `g2048`: 347.6K baseline SPS to 358.6K milestone SPS, score 97.855 unchanged, eval score stayed 49.43.
- Both new run metadata files include identical `git_diff_sha256=755c36ba21d3d04a23f662c2d733e36db320c02926e2d4ce2fa0837ccec78558`.

## 2026-05-26 01:39 EEST

- Milestone 01 committed as `cdc249e26`.
- Starting milestone 02 hot-path pass.
- Root-cause hypothesis: `muon_addmm_dependency_boundary` forces a full stream sync after each Muon `puf_addmm_nn`, but `puf_addmm_nn` already inserts Metal barriers before dependent reads. Removing the CPU-visible sync should reduce Muon wall time without changing GPU ordering.

## 2026-05-26 01:41 EEST

- Tested removing `muon_addmm_dependency_boundary`.
- Result rejected despite speedup.
- `breakout` reached 3.2M SPS but losses became NaN by 4.2M steps.
- `g2048` reached 511K SPS but score collapsed from ~98 to 16, with eval score 13.84.
- Reverted the sync-removal code. Root cause update: Metal barriers were not enough for this Muon dependency chain, or the missing full sync exposed stale/unstable state before subsequent CPU-visible training bookkeeping.

## 2026-05-26 01:42 EEST

- Starting second milestone 02 candidate.
- Root-cause hypothesis: raw pointer buffer lookup and argument binding are duplicated across `kernels.mm`, `platform.h`, and GEMM helpers. Consolidating them through shared platform helpers should reduce LOC and may reduce repeated argument-table writes in GEMM paths by honoring `bound_addresses`.

## 2026-05-26 01:45 EEST

- Binding-helper candidate suite passed twice.
- Code LOC total across Metal-owned paths dropped from 10217 to 10184.
- Median vs milestone 01 accepted runs: `breakout` +2.70 percent SPS, `g2048` -0.16 percent SPS.
- Decision: do not count this as a speed win because it does not clear the 3 percent gate on both envs. Keep pending review as a LOC reduction because median SPS stayed within the 1 percent LOC gate and learnability/eval did not regress.

## 2026-05-26 01:47 EEST

- Milestone 02 binding consolidation committed as `3921f59f4` after subagent review.
- Starting milestone 03 cleanup.

## 2026-05-26 01:49 EEST

- Cleanup scan found no temporary debug code or stale feature flags introduced by the accepted commits.
- Milestone 03 suite passed.
- Cleanup `breakout`: 4.2M steps, 2.03M SPS, train score 2.586, eval score 0.0.
- Cleanup `g2048`: 262K steps, 326.7K SPS, train score 97.855, eval score 49.70. No code changed after `3921f59f4`, so this is recorded as validation noise unless it repeats.

## 2026-05-26 01:51 EEST

- Interactive breakout smoke failed before opening render: non-None eval went through `pufferl.eval`, which forces `horizon = 1` while leaving `minibatch_size = 65536`.
- Patched `tools/metal/puffer-metal.py` to append `--train.minibatch-size <vec.total_agents>` for regular eval when the user has not set a minibatch size.
- Reran interactive breakout smoke. It built, loaded `resources/breakout/breakout_weights.bin`, initialized raylib on Apple M4 Pro, opened the render window, and stayed active until manually closed with Ctrl-C.

## 2026-05-26 01:53 EEST

- Fixed the explicit-user-override check to look for `--train.minibatch-size`, not the nonexistent `--train.minibatch` flag.
- Validated the eval patch with `py_compile`, `git diff --check`, overlay surface pytest, and a real no-render breakout eval.
- No-render eval loaded `resources/breakout/breakout_weights.bin` and returned one episode with score 338.

## 2026-05-26 01:56 EEST

- Subagent review found an argparse abbreviation loophole: `--train.minibatch` is accepted as an abbreviation for `--train.minibatch-size`, but the override guard missed it.
- First patch still missed shorter accepted abbreviations like `--train.mini`.
- Patched the guard to treat accepted unique `--train.minibatch-size` abbreviations as user-provided minibatch-size overrides.
- Validated with `py_compile`, `git diff --check`, overlay surface pytest, and a direct helper check for split, equals, and abbreviated flag forms.

## 2026-05-26 01:59 EEST

- Confirmed `--train.mini` is the shortest accepted unique abbreviation for `--train.minibatch-size`, while `--train.min` remains ambiguous.
- Revalidated the guard against exact, equals, `--train.minibatch`, `--train.minibatch-s`, and `--train.mini` forms, including a `load_config` parse check.

## 2026-05-26 02:00 EEST

- Eval minibatch fix committed as `e3f663e6e` after subagent review cleared.
- Starting milestone 04 second hot-path pass.
- Root-cause hypothesis: the fp32 small GEMM fallback handles decoder-shaped unaligned NT matmuls with unsigned dimensions and indices, while nearby host dispatch already reasons in signed `int`. Matching the shader parameter and loop types to signed host dimensions may reduce cast and address arithmetic cost without changing memory order or floating-point accumulation.

## 2026-05-26 02:03 EEST

- Tested signed-index small GEMM fallback in two full milestone 04 suite runs.
- Repeated-run medians versus the prior accepted baseline: `breakout` +0.75 percent SPS, `g2048` +0.91 percent SPS.
- Decision: reject and revert. The change was clean but did not clear the 3 percent speed gate.

## 2026-05-26 02:06 EEST

- Starting second milestone 04 candidate.
- Root-cause hypothesis: CPU inference already writes sampled float actions into `rollouts.actions`, and training already transposes that buffer before PPO. The separate `rollout_actions_f32` and `train_actions_f32` buffers duplicate the same action data solely for old-logprob recompute, adding rollout copies, one training transpose, and allocator surface without changing semantics.

## 2026-05-26 02:09 EEST

- Removed the duplicate action recompute buffers and pointed old-logprob recompute at the already-transposed training actions.
- Milestone 04 suite passed twice.
- Repeated-run median versus the prior accepted baseline: `breakout` +0.15 percent SPS, `g2048` -0.15 percent SPS.
- Decision: keep pending subagent review as a LOC-only cleanup. It is inside the 1 percent LOC gate, but it is not a speed win.

## 2026-05-26 02:20 EEST

- Duplicate action-buffer cleanup committed as `971343e2f`.
- Starting third milestone 04 candidate.
- Root-cause hypothesis: training always transposes `rollouts.ratio` and usually transposes `rollouts.logprobs`, but ratio is immediately overwritten with ones, and logprobs are immediately recomputed when `cpu_inference` or `train_fp16` is active. Skipping those dead transposes should remove memory passes without changing PPO inputs.

## 2026-05-26 02:21 EEST

- Tested dead-transpose skip once.
- `breakout` regressed from the current accepted median 2.29M SPS to 2.26M SPS, -1.30 percent. `g2048` improved 1.75 percent, but the plan requires no benchmark regression.
- Decision: reject and revert. The change missed the LOC gate on `breakout`.

## 2026-05-26 02:24 EEST

- Dead-transpose rejection committed as `04f4f5f58`.
- Starting fourth milestone 04 candidate.
- Root-cause hypothesis: `puff_advantage_kernel` overwrites every advantage timestep except the final one, so the preceding full-buffer zero exists only to set `adv[horizon - 1] = 0`. Writing that final element inside the kernel should remove a full-buffer zero dispatch with identical advantage values.

## 2026-05-26 02:26 EEST

- Tested advantage-zero folding once.
- Rejected immediately: `breakout` SPS regressed -1.74 percent and learning shape changed sharply, with training score 6.32. `g2048` SPS regressed -0.51 percent and training score dropped to 92.69.
- Reverted. Root cause update: the full-buffer zero is not safely equivalent to per-row final-timestep writes in the current execution schedule, or it masks stale advantage state outside the assumed row coverage.

## 2026-05-26 02:28 EEST

- Advantage-zero rejection committed as `794e671b2`.
- Starting cleanup candidate.
- Root-cause hypothesis: Metal priority replay allocates a `cdf` buffer copied from the CUDA structure, but Metal sampling reads normalized `prio_probs` directly and no Metal path reads or writes `cdf`. Removing it should reduce memory and LOC without changing sampling semantics.

## 2026-05-26 02:30 EEST

- Tested unused Metal priority `cdf` removal twice.
- Rejected and reverted. Repeated-run median versus current accepted baseline: `breakout` -1.58 percent SPS, `g2048` -0.95 percent SPS.
- Root cause update: the buffer is not referenced by name, but changing allocator layout perturbs performance enough to miss the LOC gate.

## 2026-05-26 02:35 EEST

- CDF cleanup rejection committed as `e64dea8fa`.
- Starting cleanup cadence pass after milestone 04 attempts.
- Cleanup scan found no uncommitted backend diff, no temporary logging introduced by accepted commits, and no stale feature flags from the kept changes.
- Current tracked Metal-owned LOC across `src/metal`, `tools/metal`, and `tests/metal`: 10208.

## 2026-05-26 02:36 EEST

- Cleanup suite passed on accepted code.
- Cleanup `breakout`: 4.2M steps, 2.28M SPS, train score 2.148, eval score 0.0.
- Cleanup `g2048`: 262K steps, 351.7K SPS, train score 97.855, eval score 49.43.

## 2026-05-26 02:42 EEST

- Cleanup checkpoint committed as `fb88d47ea`.
- Starting fifth milestone 04 candidate.
- Root-cause hypothesis: Muon copies `A = src @ src^T` into `gram` before computing `gram = b*A + c*A@A`. On the aligned tensor-ops path, `puf_addmm_nn` first writes `A@A` into a separate temp buffer before mutating `out`, so the first Muon addmm can safely write back into `A` and skip the `A -> gram` copy. Fallback cases must keep the old copy path.

## 2026-05-26 02:44 EEST

- Tested guarded in-place Muon gram once.
- Rejected and reverted. The run stayed learnable but only reached `breakout` +0.83 percent SPS and `g2048` +0.29 percent SPS versus the current accepted baseline, while adding code. It did not justify a second run under the 3 percent speed gate.
