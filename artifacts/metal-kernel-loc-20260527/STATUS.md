# Kernel LOC Overnight Status

## 2026-05-27 Setup

- Created fresh branch `valtteri/metal-kernel-loc-night-20260527` from `metal-local` at `b490ce0a6`.
- Worktree: `/Users/valtterivalo/.codex/worktrees/metal-kernel-loc-night-20260527/pufferlib-metal`.
- Existing `metal-local` checkout has unrelated PVP worktree changes, so this run uses the clean `metal-local` ref and does not touch that checkout.
- New heartbeat automation: `pufferlib-metal-kernel-loc-overnight-loop`, active until 2026-05-28 09:00 EEST.
- Root-cause hypothesis for setup: last night improved speed more than kernel/source size. Tonight needs a stricter kernel LOC census, plus the same end-to-end benchmark gates, so we remove real Metal 4 kernel/source duplication without trading away learnability or wall-clock usefulness.

## 2026-05-27 Setup Validation

- Current tracked kernel scope: `4,615` lines across `src/metal/kernels.mm`, `src/metal/shader_src.h`, and the tensor-ops shader block in `src/metal/platform.mm`.
- Largest kernel-scope files: `src/metal/shader_src.h` `2,809` lines, `src/metal/kernels.mm` `1,586` lines, `src/metal/platform.mm` tensor-ops shader block `220` lines.
- Setup baseline tracked backend scope: `10,160` lines across `src/metal`, `tools/metal`, and `tests/metal`.
- Shader declarations: `56` `kernel void` declarations in `src/metal/shader_src.h`, `6` Metal tensor-op shader declarations in `src/metal/platform.mm`.
- Setup validation: `git diff --check` passed, Python compile passed for the new summarizer and overlay test, and `PYTHONPATH=$PWD python -m pytest tests/metal/test_overlay_surface.py` passed.
- Overlay guard fix: `changed_paths()` now checks committed, staged, unstaged, and untracked paths. The old unstaged check compared the worktree against `upstream/5.0`, which misclassified upstream `boxoban` drift as local dirty paths, and a first setup patch missed staged blocked-path edits.

## 2026-05-27 Milestone 00 Baseline

- Milestone 00 runner passed: `artifacts/metal-kernel-loc-20260527/milestone-00-baseline/run-suite.sh`.
- `breakout` artifact `milestone-00-baseline/20260527T011534+0300-breakout`: `3,013,517` SPS, train score `2.385693311691284`, eval score `0.0`, eval episodes `527`, uptime `1.417306900024414`, rollout `0.21421721577644348`, train `0.41755735874176025`, train sync `0.4138440191745758`, train Muon `0.0026830416172742844`.
- `g2048` artifact `milestone-00-baseline/20260527T011549+0300-g2048`: `549,537` SPS, train score `96.67605590820312`, eval score `55.5`, eval episodes `64`, uptime `0.4770832061767578`, rollout `0.19284191727638245`, train `0.2045634686946869`, train sync `0.20018157362937927`, train Muon `0.0024486251641064882`.
- Native Metal parity wrote `milestone-00-baseline/native-metal.json`, and `tests/metal/test_overlay_surface.py` passed.
- Decision: keep setup pending subagent review. No backend behavior change yet.

## 2026-05-27 Milestone 01 Dead Shader Kernel Candidate

- Root-cause hypothesis: source search shows six MSL kernels in `src/metal/shader_src.h` have no host pipeline caller in the Metal backend: `scale_f32_dev`, `axpy_f32_dev`, `add_scalar`, `compute_lr_scalars_kernel`, `sum_rows_f16_kernel`, and `cast_f64_to_f32`. Removing those dead shader entries should reduce kernel LOC without changing dispatch order, compiled live PSOs, RNG, rollout data, PPO math, eval, or learnability.

## 2026-05-27 Milestone 01 Harness Allowlist Fix

- First dead-shader-kernel suite reached the final overlay guard after completing `breakout`, `g2048`, and native parity, then failed because the setup commit intentionally tracks `milestone-00-baseline/native-metal.json` but the overlay allowlist did not include it.
- Fix: add only that exact baseline parity artifact to `ALLOWED_ARTIFACT_PATHS`.

## 2026-05-27 Milestone 01 Dead Shader Kernel Results

- Tested dead shader kernel removal twice after the allowlist fix.
- Candidate temporarily removed `95` lines from `src/metal/shader_src.h`. Candidate kernel scope was `4,520` lines, down from `4,615`. During the benchmark state after the first allowlist fix, candidate backend scope was `10,066` lines, down from `10,160`.
- `breakout` SPS runs: `3,044,620` and `3,034,794`. Median versus milestone 00 baseline: `+0.87%`. Training scores were `2.5583012104034424` and `2.3190717697143555`, explicit eval score stayed `0.0`.
- `g2048` SPS runs: `549,880` and `555,804`. Median versus milestone 00 baseline: `+0.60%`. Training score stayed `97.85507202148438`, explicit eval scores were `50.25` and `49.43283462524414`.
- Milestone 01 suite passed native Metal parity and overlay surface tests on the second full run. The first full run completed both env benchmarks and native parity, then failed only on the harness allowlist bug above.
- Subagent review blocked acceptance.
- Review findings: missing interactive artifact for the kept-change decision, `g2048` eval lower than the single milestone 00 baseline eval, and backend LOC accounting used the pre-setup count rather than the post-setup count.
- Decision after first review: blocked. Next action is to capture milestone 01 interactive smoke and gather enough `g2048` eval evidence to decide whether the lower explicit eval is noise or a real gate failure.

## 2026-05-27 Milestone 01 Review Follow-Up

- Interactive smoke artifact: `milestone-01-kernel-loc-pass/20260527T013232+0300-interactive-breakout-dead-shader-kernels`. It built, loaded `resources/breakout/breakout_weights.bin`, reached raylib 5.5 GLFW initialization, then failed with `Failed to determine Monitor to center Window` and exit code `139`. This matches the known desktop windowing blocker.
- Reran a no-change setup-commit `g2048` baseline in a detached worktree with the same milestone 00 runner, then copied the artifact to `milestone-01-kernel-loc-pass/20260527T013315+0300-baseline-g2048`.
- Baseline rerun result: `598,782` SPS, train score `97.85507202148438`, explicit eval score `49.43283462524414`, uptime `0.4378530979156494`.
- Eval interpretation: the original milestone 00 `g2048` eval score `55.5` was a high single-run sample. Candidate evals `50.25` and `49.43283462524414` match the rerun baseline band and do not show a learnability regression.
- Corrected LOC accounting: setup baseline backend scope is `10,160`. Final live backend scope after rejection is `10,162` because the kept harness fixes add two allowlist lines. If the shader deletion had been kept with those harness fixes, backend scope would have been `10,067`, a net `-93` line candidate.
- Second subagent review rejected acceptance. The no-change setup rerun measured `598,782` SPS on `g2048`, which makes the candidate median `552,842` SPS a `-7.67%` throughput regression against the comparable same-night baseline.
- Decision: reject and revert the dead shader kernel removal. The code is back at setup kernel scope: `4,615` kernel lines. Live backend scope is `10,162` lines because the only kept backend changes are the two overlay allowlist entries needed to make milestone 01 auditable.
