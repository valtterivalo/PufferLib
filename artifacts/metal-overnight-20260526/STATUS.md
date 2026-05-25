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
