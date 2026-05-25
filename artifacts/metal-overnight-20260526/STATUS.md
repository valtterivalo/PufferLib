# Overnight Status

## 2026-05-26 01:12 EEST

- Created branch `valtteri/metal-overnight-opt` from detached `d649e493a`.
- Created heartbeat automation through 2026-05-27 09:00 EEST.
- Started milestone 00 setup.
- Root-cause hypothesis for setup: we cannot trust optimization claims until benchmark commands and captured artifacts are fixed and repeatable.

## 2026-05-26 01:29 EEST

- Added immutable plan, separate status log, milestone folders, and reusable benchmark runner scripts.
- Added explicit Metal-compatible `g2048` overrides after read-only agent review found the default config enables CUDA-only curriculum.
- Validated runner syntax, Python syntax, and whitespace with `bash -n`, `py_compile`, and `git diff --check`.

## 2026-05-26 01:33 EEST

- First `breakout` baseline failed during `tools/metal/build.sh` CPU prebuild.
- Root cause: `vecenv.h` includes `cuda_runtime_api.h`, and the CPU prebuild did not include `src/metal`, where the local compatibility header lives.
- Patched `tools/metal/build.sh` to pass `-Isrc/metal` into the CPU prebuild only.

## 2026-05-26 01:36 EEST

- Second `breakout` baseline built and trained to 4.2M steps.
- Runner failed before eval because training produced two checkpoints, while the harness expected one.
- Patched runner to write `checkpoints.txt` and `selected-checkpoint.txt`, then eval the highest step checkpoint by filename.

## 2026-05-26 01:39 EEST

- Third `breakout` baseline built and trained to 4.2M steps.
- No-render eval failed during backend construction because the wrapper forced `horizon = 1` but left `minibatch_size = 65536`.
- Patched no-render eval to set `train.minibatch_size = vec.total_agents`, preserving the backend divisibility invariant.

## 2026-05-26 01:43 EEST

- Clean `breakout` baseline completed: 4.2M train steps, 2.04M SPS, training score 2.46, explicit checkpoint eval score 0.0.
- Clean `g2048` baseline completed: 262K train steps, 347.6K SPS, training score 97.86, explicit checkpoint eval score 49.43.
- Overlay hygiene test failed because the new audit artifact root was outside the allowed Metal-owned path set.
- Patched `tests/metal/test_overlay_surface.py` to allow only `artifacts/metal-overnight-20260526/` in addition to the existing Metal-owned code paths.

## 2026-05-26 01:50 EEST

- Subagent review found three milestone 00 harness issues: `--slowly` could benchmark Torch fallback, the artifact whitelist was too broad, and `run-suite.sh` was not cwd-stable.
- Patched the benchmark runner to reject `--slowly`.
- Tightened overlay surface test to allow only the known overnight artifact files, not the whole artifact tree.
- Patched all `run-suite.sh` scripts to `cd` to the repo root before running pytest.

## 2026-05-26 01:55 EEST

- Subagent verification found an argparse abbreviation bypass: `--slow` and `--slo` still parse as `--slowly`.
- Patched runner to reject the full `--slo*` prefix.

## 2026-05-26 01:58 EEST

- Found that `run-suite.sh` called the parity fixture through pytest even though `test_native_backend_parity.py` is a CLI fixture.
- Patched all suite scripts to run `tests/metal/test_native_backend_parity.py --backend metal --write-json ...` directly, then run the overlay surface pytest.

## 2026-05-26 02:01 EEST

- Milestone 00 committed as `d699d8db3` with subagent review clear.
- Starting milestone 01 LOC pass.
- Root-cause hypothesis: the scan dispatch helpers duplicate host binding code for forward/backward and fp32/fp16 variants, so a tagged helper can reduce lines without changing MSL kernels or dataflow.

## 2026-05-26 02:05 EEST

- Refactored scan dispatch helpers in `src/metal/kernels.mm`.
- Net diff for the file: 25 insertions, 58 deletions.
- First milestone 01 suite attempt failed in the benchmark runner because the new `--slo*` guard expanded an empty bash array under `set -u`.
- Patched the guard to check array length before iterating.

## 2026-05-26 02:14 EEST

- Milestone 01 suite passed.
- `src/metal/kernels.mm` line count changed from 1652 to 1619.
- `breakout`: 2.04M baseline SPS to 2.25M milestone SPS, score 2.457 to 2.563, eval score stayed 0.0.
- `g2048`: 347.6K baseline SPS to 355.3K milestone SPS, score 97.855 to 98.889, eval score 49.43 to 49.48.
- Decision: keep the LOC refactor pending subagent code review. The benchmark gate shows no regression.

## 2026-05-26 02:23 EEST

- Subagent review found benchmark attribution gap: milestone artifacts recorded only `HEAD`, not the dirty refactor diff.
- Patched runner to capture `git-status.txt`, `git-diff.patch`, and `git_diff_sha256` in metadata for each run.
- Corrected stale LOC and numstat after wrap-only formatting.

## 2026-05-26 02:31 EEST

- Reran milestone 01 suite with dirty-diff attribution.
- `breakout`: 2.04M baseline SPS to 2.20M milestone SPS, score 2.457 to 2.262, eval score stayed 0.0.
- `g2048`: 347.6K baseline SPS to 358.6K milestone SPS, score 97.855 unchanged, eval score stayed 49.43.
- Both new run metadata files include identical `git_diff_sha256=755c36ba21d3d04a23f662c2d733e36db320c02926e2d4ce2fa0837ccec78558`.

## 2026-05-26 02:36 EEST

- Milestone 01 committed as `cdc249e26`.
- Starting milestone 02 hot-path pass.
- Root-cause hypothesis: `muon_addmm_dependency_boundary` forces a full stream sync after each Muon `puf_addmm_nn`, but `puf_addmm_nn` already inserts Metal barriers before dependent reads. Removing the CPU-visible sync should reduce Muon wall time without changing GPU ordering.

## 2026-05-26 02:43 EEST

- Tested removing `muon_addmm_dependency_boundary`.
- Result rejected despite speedup.
- `breakout` reached 3.2M SPS but losses became NaN by 4.2M steps.
- `g2048` reached 511K SPS but score collapsed from ~98 to 16, with eval score 13.84.
- Reverted the sync-removal code. Root cause update: Metal barriers were not enough for this Muon dependency chain, or the missing full sync exposed stale/unstable state before subsequent CPU-visible training bookkeeping.

## 2026-05-26 02:51 EEST

- Starting second milestone 02 candidate.
- Root-cause hypothesis: raw pointer buffer lookup and argument binding are duplicated across `kernels.mm`, `platform.h`, and GEMM helpers. Consolidating them through shared platform helpers should reduce LOC and may reduce repeated argument-table writes in GEMM paths by honoring `bound_addresses`.

## 2026-05-26 03:00 EEST

- Binding-helper candidate suite passed twice.
- Code LOC total across Metal-owned paths dropped from 10217 to 10184.
- Median vs milestone 01 accepted runs: `breakout` +2.70 percent SPS, `g2048` -0.16 percent SPS.
- Decision: do not count this as a speed win because it does not clear the 3 percent gate on both envs. Keep pending review as a LOC reduction because median SPS stayed within the 1 percent LOC gate and learnability/eval did not regress.
