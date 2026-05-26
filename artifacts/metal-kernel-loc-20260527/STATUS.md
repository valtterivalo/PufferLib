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

## 2026-05-27 Milestone 02 Dispatch Boilerplate Candidate

- Root-cause hypothesis: `src/metal/kernels.mm` repeats the same host kernel setup shape across many dispatch wrappers: activate the compute encoder, look up a PSO by name, then bind the PSO. A local helper can remove duplicated source while preserving PSO names, argument binding order, dispatch counts, RNG state, optimizer math, and compiled MSL.
- Acceptance gate: run the milestone 02 suite twice for `breakout` and `g2048`, capture interactive smoke, require median SPS within 1 percent of the accepted baseline or better, keep train and eval comparable, and block commit on subagent review.
- Harness fix: the first milestone 02 interactive smoke attempt wrote `milestone-02-kernel-consolidation-pass/20260527T014826+0300-interactive-breakout-dispatch-boilerplate` and failed with exit `127` because `run-interactive-breakout.sh` was missing for this milestone folder. Add the exact runner and allowlist path before rerunning interactive smoke.

## 2026-05-27 Milestone 02 Dispatch Boilerplate Results

- Code change: add `mtl_begin_kernel` in `src/metal/kernels.mm` and replace repeated compute-encoder, PSO lookup, and PSO bind triplets. No MSL source, dispatch dimensions, barrier order, argument indices, RNG, optimizer math, or policy data flow changed.
- LOC: `src/metal/kernels.mm` is `1,512` lines, down from `1,586`. Kernel scope is `4,541`, down from `4,615`. Live backend scope is `10,089`, down from the post-milestone-01 live scope `10,162`, and down from setup baseline `10,160`.
- Static validation passed: `git diff --check`, Python compile for the summarizer and overlay test, `bash -n` for the milestone 02 interactive runner, and `PYTHONPATH=$PWD python -m pytest tests/metal/test_overlay_surface.py`.
- First milestone 02 suite: `breakout` artifact `20260527T014724+0300-breakout` at `3,427,852` SPS, train score `2.5335967540740967`, eval score `0.0`; `g2048` artifact `20260527T014735+0300-g2048` at `599,712` SPS, train score `97.85507202148438`, eval score `49.43283462524414`.
- Second milestone 02 suite: `breakout` artifact `20260527T014753+0300-breakout` at `3,425,516` SPS, train score `2.4037489891052246`, eval score `0.0`; `g2048` artifact `20260527T014803+0300-g2048` at `597,629` SPS, train score `97.85507202148438`, eval score `49.43283462524414`.
- Medians: `breakout` `3,426,684` SPS, `+13.71%` versus milestone 00 baseline `3,013,517`; `g2048` `598,671` SPS, `-0.02%` versus the same-night no-change baseline rerun `598,782`.
- Native Metal parity and overlay surface passed in both milestone 02 suites.
- Interactive smoke artifact: `milestone-02-kernel-consolidation-pass/20260527T014915+0300-interactive-breakout-dispatch-boilerplate`. It built, loaded `resources/breakout/breakout_weights.bin`, reached raylib 5.5 GLFW initialization, then failed with `Failed to determine Monitor to center Window` and exit code `139`, matching the known desktop windowing boundary.
- Decision: accepted pending subagent review. This is a LOC cleanup with no learnability regression and no measurable throughput regression on the comparable g2048 rerun baseline.

## 2026-05-27 Milestone 03 Shared Dispatch Helper Candidate

- Root-cause hypothesis: milestone 02 proved the dispatch setup helper preserves behavior in `src/metal/kernels.mm`. The same triplet still appears in `src/metal/platform.mm`. Moving the helper to `src/metal/platform.h` and using it in both files should remove more backend duplication without changing MSL source, PSO names, argument binding order, dispatch dimensions, barriers, RNG, optimizer math, or policy data flow.
- Acceptance gate: run the milestone 03 suite twice for `breakout` and `g2048`, capture interactive smoke through the milestone 03 runner, require median SPS within 1 percent of the accepted baseline or better, keep train and eval comparable, and block commit on subagent review.

## 2026-05-27 Milestone 03 Shared Dispatch Helper Results

- Code change: move `mtl_begin_kernel` from `src/metal/kernels.mm` to `src/metal/platform.h`, then replace the same dispatch setup triplet in `src/metal/platform.mm`. The helper still runs `compute_encoder`, `mtl_pipeline`, and `mtl_set_pso` in that order.
- LOC: `src/metal/kernels.mm` is `1,504` lines, down from `1,512` after milestone 02. Full `src/metal/platform.mm` is `1,196` lines, down from `1,213`; `src/metal/platform.h` is `294` lines, up from `286`. Kernel scope is `4,533`, down from `4,541`. Live backend scope is `10,072`, down from `10,089`.
- Static validation passed: `git diff --check`, `bash -n` for the milestone 03 interactive runner, and `PYTHONPATH=$PWD python -m pytest tests/metal/test_overlay_surface.py`.
- First milestone 03 suite: `breakout` artifact `20260527T015558+0300-breakout` at `3,420,369` SPS, train score `2.294158458709717`, eval score `0.0`; `g2048` artifact `20260527T015609+0300-g2048` at `594,853` SPS, train score `97.85507202148438`, eval score `49.43283462524414`; interactive artifact `20260527T015619+0300-interactive-breakout-cleanup` reached the known GLFW monitor boundary.
- Second milestone 03 suite: `breakout` artifact `20260527T015635+0300-breakout` at `3,469,139` SPS, train score `2.3981192111968994`, eval score `0.0`; `g2048` artifact `20260527T015646+0300-g2048` at `601,581` SPS, train score `97.85507202148438`, eval score `49.43283462524414`; interactive artifact `20260527T015656+0300-interactive-breakout-cleanup` reached the known GLFW monitor boundary.
- Medians: `breakout` `3,444,754` SPS, `+14.31%` versus milestone 00 baseline `3,013,517`; `g2048` `598,217` SPS, `-0.09%` versus same-night no-change baseline rerun `598,782`.
- Native Metal parity and overlay surface passed in both milestone 03 suites.
- Decision: accepted pending subagent review. This cleanup reduces shared host dispatch boilerplate and stays inside the throughput and learnability gates.

## 2026-05-27 Milestone 04 Tensor-Ops Shader Template Candidate

- Root-cause hypothesis: the Metal 4 tensor-ops shader block in `src/metal/platform.mm` duplicates the same fp32 and fp16 kernel bodies for NT, NN, and TN GEMM layouts. Typed MSL helper functions can keep the six exported kernel names and PSO lookup names unchanged while removing duplicated source in the kernel scope.
- Acceptance gate: build `breakout` and `g2048`, run the milestone 04 suite twice, capture interactive smoke with a milestone 04 runner, require median SPS within 1 percent of the accepted baseline or better, keep train and eval comparable, and block commit on subagent review.
- Harness fix: add the milestone 04 interactive breakout runner and exact overlay allowlist path before acceptance testing, since the global gate requires interactive usability evidence for every kept backend change.

## 2026-05-27 Milestone 04 Tensor-Ops Shader Template Results

- Code change: replace duplicated fp32 and fp16 tensor-ops NT, NN, and TN shader bodies with three typed MSL helpers. The six exported kernel names remain `tensor_ops_gemm_nt_f32`, `tensor_ops_gemm_nn_f32`, `tensor_ops_gemm_tn_f32`, `tensor_ops_gemm_nt_f16`, `tensor_ops_gemm_nn_f16`, and `tensor_ops_gemm_tn_f16`.
- LOC: full `src/metal/platform.mm` is `1,093` lines, down from `1,196` after milestone 03. The tensor-ops shader block is `117` lines, down from setup `220`. Kernel scope is `4,430`, down from `4,533` after milestone 03 and down from setup `4,615`. Live backend scope is `9,970`, down from `10,072` after milestone 03.
- Static validation passed: `tools/metal/build.sh breakout`, `git diff --check`, `bash -n` for the milestone 04 interactive runner, and `PYTHONPATH=$PWD python -m pytest tests/metal/test_overlay_surface.py`.
- First milestone 04 suite: `breakout` artifact `20260527T020534+0300-breakout` at `3,453,475` SPS, train score `2.7511961460113525`, eval score `0.0`; `g2048` artifact `20260527T020546+0300-g2048` at `608,646` SPS, train score `97.85507202148438`, eval score `49.43283462524414`.
- Second milestone 04 suite: `breakout` artifact `20260527T020602+0300-breakout` at `3,397,213` SPS, train score `2.3502695560455322`, eval score `0.0`; `g2048` artifact `20260527T020613+0300-g2048` at `577,706` SPS, train score `97.85507202148438`, eval score `49.43283462524414`.
- Extra g2048 runner: `20260527T020712+0300-g2048` at `595,902` SPS, train score `97.85507202148438`, eval score `49.43283462524414`. This extra run was added because the two-run g2048 median was close to the 1 percent LOC gate.
- Medians: `breakout` `3,425,344` SPS, `+13.66%` versus milestone 00 baseline `3,013,517`; `g2048` three-run median `595,902` SPS, `-0.48%` versus same-night no-change baseline rerun `598,782`.
- Native Metal parity and overlay surface passed in both milestone 04 suites.
- Interactive smoke artifact: `milestone-04-second-kernel-loc-pass/20260527T020631+0300-interactive-breakout-tensor-ops-template`. It built, loaded `resources/breakout/breakout_weights.bin`, reached raylib 5.5 GLFW initialization, then failed with `Failed to determine Monitor to center Window` and exit code `139`, matching the known desktop windowing boundary.
- Decision: accepted pending subagent review. This is a kernel-scope LOC cleanup with unchanged explicit eval and g2048 throughput inside the 1 percent gate.
