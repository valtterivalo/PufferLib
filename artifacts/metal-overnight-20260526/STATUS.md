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

## 2026-05-26 02:50 EEST

- Resumed from clean commit `7b40b59ab`.
- Starting sixth milestone 04 candidate.
- Root-cause hypothesis: `prio_imp_weights_kernel` writes one priority weight per minibatch segment, but the host launches only one 256-thread group while current benchmark configs use 1024 minibatch segments. Rows above 255 can therefore reuse stale `mb_prio` values inside `select_copy_kernel`, which may perturb learning and wastes the intended priority replay contract. Dispatching this as a normal 1D grid should make priority weights total over all minibatch rows. This is a semantic fix candidate, so acceptance requires both learnability and the normal benchmark gates.

## 2026-05-26 02:52 EEST

- Tested priority importance-weight full-grid dispatch twice.
- `breakout` SPS runs: 2,285,440 and 2,280,391. Median versus current accepted baseline: -0.35 percent. Training scores increased to 4.55 and 6.12, explicit eval scores were 0.40 and 0.0.
- `g2048` SPS runs: 352,723 and 353,095. Median versus current accepted baseline: -0.82 percent. Training score was 96.21 both runs, explicit eval score was 54.40 both runs.
- Decision: reject and revert after subagent review. The code-level fix was plausible, but it changed `mb_prio` values consumed by PPO and therefore did not qualify for the LOC-only gate. It also did not clear the milestone 04 speed gate. Keep this as a later explicit correctness item, not an overnight optimization commit.

## 2026-05-26 03:00 EEST

- Priority-weight rejection committed as `ae7b01e52`.
- Starting seventh milestone 04 candidate.
- Root-cause hypothesis: PPO reduction still carries a legacy `loss_output` buffer and an extra total-loss partial lane. The reducer writes `loss_output`, but logging reads `losses_puf` only. Removing the duplicate lane and unused buffer should reduce one zero dispatch, one buffer allocation, one argument binding, and host/shader LOC without changing logged losses.

## 2026-05-26 03:02 EEST

- Tested dead PPO `loss_output` removal twice.
- `breakout` SPS runs: 2,280,738 and 2,281,764. Median versus current accepted baseline: -0.42 percent. Training scores were 2.41 and 2.56, explicit eval score stayed 0.0.
- `g2048` SPS runs: 353,976 and 349,353. Median versus current accepted baseline: -1.16 percent. Training score stayed 97.855, explicit eval score stayed 49.43.
- Decision: reject and revert. The cleanup was semantically clean, but `g2048` missed the 1 percent LOC gate on repeated runs.

## 2026-05-26 03:05 EEST

- Dead PPO cleanup rejection committed as `d05cc4631`.
- Starting eighth milestone 04 candidate.
- Root-cause hypothesis: after PPO, `mtl_scatter_ppo_outputs` launches the same indexed row copy twice with identical indices and row width, once for ratio and once for values. A paired scatter kernel can combine the two memory passes into one dispatch without changing the copied data.

## 2026-05-26 03:06 EEST

- Tested paired PPO scatter once.
- `breakout`: 2,283,270 SPS, score 2.42, explicit eval score 0.0.
- `g2048`: 354,996 SPS, score 97.855, explicit eval score 49.43.
- Decision: reject and revert without a second run. The result did not approach the 3 percent speed gate, and the change added shader code.

## 2026-05-26 03:08 EEST

- Paired scatter rejection committed as `289926e1b`.
- Starting cleanup cadence pass after late milestone 04 attempts.
- Cleanup scan found no uncommitted backend diff, no temporary logging, and no leftover code from the priority-weight, PPO loss-output, or paired-scatter rejected experiments.
- Current tracked Metal-owned LOC across `src/metal`, `tools/metal`, and `tests/metal`: 10208.

## 2026-05-26 03:10 EEST

- Cleanup suite passed on accepted code.
- Cleanup `breakout`: 4.2M steps, 2.27M SPS, train score 2.596, eval score 0.0.
- Cleanup `g2048`: 262K steps, 354.4K SPS, train score 97.855, eval score 49.43.

## 2026-05-26 03:13 EEST

- Cleanup checkpoint committed as `446954267`.
- Starting ninth milestone 04 candidate.
- Root-cause hypothesis: `breakout` and `g2048` use no action masks, but GPU logprob recompute and PPO still load the all-ones mask and call mask-aware branches for every action. A no-mask branch in those kernels should remove mask memory traffic and branch work from the benchmark hot path without changing probabilities.

## 2026-05-26 03:15 EEST

- Tested GPU no-mask recompute/PPO branch once.
- `breakout`: 2,246,855 SPS, score 2.26, explicit eval score 0.0.
- `g2048`: 365,522 SPS, score 97.855, explicit eval score 49.43.
- Decision: reject and revert without a second run. The candidate hurt `breakout` by about 1.9 percent and `g2048` did not clear the 3 percent speed gate.

## 2026-05-26 03:18 EEST

- No-mask rejection committed as `2a04f8910`.
- Starting milestone 01 follow-up LOC candidate.
- Root-cause hypothesis: `puf_copy` and `puf_zero` duplicate the same raw f32/f16 GPU and CPU memcpy/memset branches across `PufTensor` and `FloatTensor`. A shared raw helper can reduce host LOC while preserving the current copy rules exactly.

## 2026-05-26 03:22 EEST

- Refactored `FloatTensor` copy/zero helpers to route through the existing `PufTensor` implementations using a local adapter that preserves all tensor shape dimensions.
- Current tracked Metal-owned LOC across `src/metal`, `tools/metal`, and `tests/metal`: 10205.
- LOC suite passed.
- `breakout`: 4.2M steps, 2.31M SPS, train score 2.521, eval score 0.0. Versus current accepted median: +0.71 percent.
- `g2048`: 262K steps, 362.4K SPS, train score 97.855, eval score 49.43. Versus current accepted median: +1.85 percent.
- Decision: keep pending subagent review as a LOC cleanup.

## 2026-05-26 03:30 EEST

- Tensor copy-helper LOC cleanup passed subagent review after preserving all tensor shape dimensions in the adapter.
- Cleanup committed as `eaa763e97`.
- Worktree resumed clean at heartbeat. Current tracked Metal-owned LOC across `src/metal`, `tools/metal`, and `tests/metal`: 10205.

## 2026-05-26 03:33 EEST

- Starting tenth milestone 01 LOC candidate.
- Root-cause hypothesis: the host backend still hand-builds `PufTensor` and `PrecisionTensor` wrappers from `FloatTensor` and `PufTensor` in several rollout and training paths. The accepted tensor copy-helper patch showed the safer invariant: wrappers must preserve every shape dimension and the actual dtype. Centralizing that adapter logic in `platform.h` should delete repeated initializer code without changing dispatch, RNG, masks, or learning math.

## 2026-05-26 03:37 EEST

- Tested shared tensor-wrapper adapters twice.
- `breakout` SPS runs: 2,250,520 and 2,274,256. Median versus current accepted baseline: -1.24 percent. Training scores were 2.674 and 2.509, explicit eval score stayed 0.0.
- `g2048` SPS runs: 353,648 and 356,293. Median versus current accepted baseline: -0.24 percent. Training score stayed 97.855, explicit eval score stayed 49.43.
- Decision: reject and revert. The cleanup reduced tracked Metal-owned LOC to 10181, but `breakout` missed the 1 percent LOC gate.

## 2026-05-26 04:00 EEST

- Starting eleventh milestone 01 LOC candidate.
- Root-cause hypothesis: `src/metal/pufferlib.mm` still carries banner comments and obvious section labels that explain file shape rather than invariants. Removing those lines should lower Metal-owned LOC without changing tokens that compile into behavior, dispatch order, RNG state, masks, or learning math. Keep only if end-to-end LOC gates still pass, since the plan requires real benchmark artifacts even for comment-only cleanup.

## 2026-05-26 04:02 EEST

- Tested `pufferlib.mm` banner-comment cleanup twice.
- `breakout` SPS runs: 2,244,897 and 2,271,970. Median versus current accepted baseline: -1.42 percent. Training scores were 2.634 and 2.502, explicit eval score stayed 0.0.
- `g2048` SPS runs: 369,367 and 356,003. Median versus current accepted baseline: +1.93 percent. Training score stayed 97.855, explicit eval score stayed 49.43.
- Decision: reject and revert. The cleanup removed 84 lines and was compile-token neutral, but the benchmark median still missed the milestone 01 LOC gate on `breakout`.

## 2026-05-26 04:30 EEST

- Starting twelfth milestone 01 LOC candidate.
- Root-cause hypothesis: `mtl_sample_logits_expand` is a leftover f32-to-f64 action expansion helper with no source caller and no header declaration. Current rollout code copies f32 actions directly into the env action buffer, so deleting the helper should lower LOC without changing any runtime path.

## 2026-05-26 04:34 EEST

- Tested dead `mtl_sample_logits_expand` removal twice.
- `breakout` SPS runs: 2,300,542 and 2,297,579. Median versus current accepted baseline: +0.36 percent. Training scores were 2.716 and 2.309, explicit eval score stayed 0.0.
- `g2048` SPS runs: 384,106 and 356,872. Median versus current accepted baseline: +4.13 percent. Training score stayed 97.855, explicit eval score stayed 49.43.
- Milestone 01 suite also passed native Metal parity and overlay surface tests on both runs. Current tracked Metal-owned LOC: 10200.
- Interactive smoke artifacts: `milestone-01-loc-pass/20260526T043138+0300-interactive-breakout` and `milestone-01-loc-pass/20260526T043225+0300-interactive-breakout-pty`. Both built, loaded `resources/breakout/breakout_weights.bin`, and reached raylib 5.5 initialization, then failed in GLFW window centering with `Failed to determine Monitor to center Window` followed by `Segmentation fault: 11`. Treat as a heartbeat windowing-platform blocker unless subagent review says the global interactive gate requires rejecting this change.
- Decision: keep pending subagent review. The code deletion is not on any runtime path, benchmark and no-render eval gates pass, and the interactive failure occurs after raylib starts in the desktop windowing layer.
