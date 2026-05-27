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
- Decision: accepted after subagent review and committed as `2dda74c58`. This is a kernel-scope LOC cleanup with unchanged explicit eval and g2048 throughput inside the 1 percent gate.

## 2026-05-27 Milestone 04b MinGRU Scan Template Candidate

- Root-cause hypothesis: `src/metal/shader_src.h` duplicates MinGRU scan forward and backward control flow for fp32 and fp16. A typed MSL helper layer can keep all eight exported kernel names and buffer layouts unchanged while centralizing scalar reads, writes, clamping, and the fp32-versus-fp16 `fast::exp`/`exp` choice.
- Risk classification: high. This touches policy recurrent-state math, optimizer gradients, and checkpoint buffers, so acceptance requires build success, parity, repeated `breakout` and `g2048` runs, comparable score versus elapsed time, interactive smoke, and subagent review.
- Acceptance gate: use the dedicated `milestone-04b-mingru-template-pass` runners, require median SPS within 1 percent of the accepted baseline or better, require train and eval scores comparable to milestone 04, and reject immediately on compile failure or determinism/parity drift.

## 2026-05-27 Milestone 04b MinGRU Scan Template Results

- Code change: replace duplicated fp32 and fp16 MinGRU scan forward and backward bodies with four typed MSL helpers plus eight thin exported kernels. Exported kernel names and host dispatch strings remain unchanged.
- LOC: `src/metal/shader_src.h` is `2,657` lines, down from `2,809` at setup and `2,809` after milestone 04. Kernel scope is `4,278` lines by the established tensor-op block convention, down from `4,430` after milestone 04 and down from setup `4,615`. Live backend scope is `9,822`, down from `9,970` after milestone 04.
- Static validation passed: `tools/metal/build.sh breakout`, `tools/metal/build.sh g2048`, `git diff --check`, `bash -n` for all milestone 04b runners, and `PYTHONPATH=$PWD python -m pytest tests/metal/test_overlay_surface.py`.
- First milestone 04b suite: `breakout` artifact `20260527T022242+0300-breakout` at `3,426,147` SPS, train score `2.413498878479004`, eval score `0.0`; `g2048` artifact `20260527T022253+0300-g2048` at `588,898` SPS, train score `97.85507202148438`, eval score `49.43283462524414`.
- Second milestone 04b suite: `breakout` artifact `20260527T022308+0300-breakout` at `3,475,413` SPS, train score `2.432997703552246`, eval score `0.0`; `g2048` artifact `20260527T022319+0300-g2048` at `598,417` SPS, train score `97.85507202148438`, eval score `49.43283462524414`.
- Medians: `breakout` `3,450,780` SPS, `+14.51%` versus milestone 00 baseline `3,013,517`; `g2048` `593,658` SPS, `-0.86%` versus same-night no-change baseline rerun `598,782`.
- Native Metal parity and overlay surface passed in both milestone 04b suites.
- Interactive smoke artifact: `milestone-04b-mingru-template-pass/20260527T022406+0300-interactive-breakout-mingru-template`. It built, loaded `resources/breakout/breakout_weights.bin`, reached raylib 5.5 GLFW initialization, then failed with `Failed to determine Monitor to center Window` and exit code `139`, matching the known desktop windowing boundary.
- Subagent review: Turing found no blocking findings and confirmed exported names, host dispatch strings, buffer indices, fp32 fast math, fp16 clamps, checkpoint semantics, reset semantics, narrow runner allowlist, and LOC arithmetic.
- Decision: accepted after subagent review. This is a high-risk kernel-scope LOC cleanup, but repeated end-to-end runs, parity, interactive smoke, and review all cleared the gate.

## 2026-05-27 Milestone 04c Dead Kernel Retest Candidate

- Root-cause hypothesis: after the accepted template cleanups, the same six shader entries from milestone 01 still have no host pipeline caller: `scale_f32_dev`, `axpy_f32_dev`, `add_scalar`, `compute_lr_scalars_kernel`, `sum_rows_f16_kernel`, and `cast_f64_to_f32`. Retesting this deletion against the current accepted baseline can distinguish the earlier g2048 miss from real runtime coupling.
- Risk classification: medium. No host dispatch strings point at these kernels, but milestone 01 was rejected for a same-night g2048 median miss, so this candidate must clear fresh repeated end-to-end gates before review.
- Acceptance gate: use the dedicated `milestone-04c-dead-kernel-retest` runners, require median SPS within 1 percent of the current accepted baseline or better, require train and eval scores comparable to milestone 04b, and reject again if g2048 falls outside the gate.

## 2026-05-27 Milestone 04c Dead Kernel Retest Results

- Code change: remove six no-caller MSL kernels from `src/metal/shader_src.h`: `scale_f32_dev`, `axpy_f32_dev`, `add_scalar`, `compute_lr_scalars_kernel`, `sum_rows_f16_kernel`, and `cast_f64_to_f32`.
- LOC: `src/metal/shader_src.h` is `2,562` lines, down from `2,657` after milestone 04b. Kernel scope is `4,183`, down from `4,278` after milestone 04b and down from setup `4,615`. Live backend scope is `9,731`, down from `9,822` after milestone 04b.
- Static validation passed: `tools/metal/build.sh breakout`, `tools/metal/build.sh g2048`, `git diff --check`, `bash -n` for all milestone 04c runners, and `PYTHONPATH=$PWD python -m pytest tests/metal/test_overlay_surface.py`.
- First milestone 04c suite: `breakout` artifact `20260527T023224+0300-breakout` at `3,475,584` SPS, train score `2.431506872177124`, eval score `0.0`; `g2048` artifact `20260527T023235+0300-g2048` at `603,709` SPS, train score `97.85507202148438`, eval score `49.43283462524414`.
- Second milestone 04c suite: `breakout` artifact `20260527T023249+0300-breakout` at `3,447,215` SPS, train score `2.6573257446289062`, eval score `0.0`; `g2048` artifact `20260527T023300+0300-g2048` at `601,542` SPS, train score `97.85507202148438`, eval score `49.43283462524414`.
- Medians: `breakout` `3,461,400` SPS, `+0.31%` versus milestone 04b accepted median `3,450,780`; `g2048` `602,626` SPS, `+1.51%` versus milestone 04b accepted median `593,658`.
- Native Metal parity and overlay surface passed in both milestone 04c suites.
- Interactive smoke artifact: `milestone-04c-dead-kernel-retest/20260527T023325+0300-interactive-breakout-dead-kernel-retest`. It built, loaded `resources/breakout/breakout_weights.bin`, reached raylib 5.5 GLFW initialization, then failed with `Failed to determine Monitor to center Window` and exit code `139`, matching the known desktop windowing boundary.
- Subagent review: Carson found no blocking findings, verified the six deleted shader names have no reachable host dispatch or dynamic lookup path, confirmed the 04c runner allowlist is exact, and checked STATUS LOC and benchmark arithmetic.
- Decision: accepted after subagent review. This retest reverses the milestone 01 rejection because the current repeated runs beat the accepted baseline rather than missing it.

## 2026-05-27 Milestone 04d Dead Helper Cleanup Candidate

- Root-cause hypothesis: after the accepted MinGRU and dead-kernel cleanups, several MSL helpers in `src/metal/shader_src.h` have no callers: `sigmoid_backward_f`, `tilde_relu_bwd`, `softplus_bwd`, `relu_f`, `relu_backward_f`, `Philox4x32`, and `atomic_add_float`. Removing them should reduce real dead kernel-source code without touching live kernels, dispatch strings, RNG helpers that are called, PPO math, or scan math.
- Risk classification: low to medium. These are no-caller helpers, but they live in the monolithic shader source, so the candidate still needs build, repeated end-to-end validation, parity, interactive smoke, and subagent review.
- Acceptance gate: use the dedicated `milestone-04d-dead-helper-cleanup` runners, require median SPS within 1 percent of the current accepted baseline or better, and reject if either env shows score or eval drift.

## 2026-05-27 Milestone 04d Dead Helper Cleanup Results

- Code change: remove no-caller MSL helpers `sigmoid_backward_f`, `tilde_relu_bwd`, `softplus_bwd`, `relu_f`, `relu_backward_f`, `Philox4x32`, and `atomic_add_float` from `src/metal/shader_src.h`.
- LOC: `src/metal/shader_src.h` is `2,522` lines, down from `2,562` after milestone 04c. Kernel scope is `4,143`, down from `4,183` after milestone 04c and down from setup `4,615`. Live backend scope is `9,695`, down from `9,731` after milestone 04c.
- Static validation passed: `tools/metal/build.sh breakout`, `tools/metal/build.sh g2048`, `git diff --check`, `bash -n` for all milestone 04d runners, and `PYTHONPATH=$PWD python -m pytest tests/metal/test_overlay_surface.py`.
- First milestone 04d suite: `breakout` artifact `20260527T024103+0300-breakout` at `3,460,705` SPS, train score `2.572317361831665`, eval score `0.0`; `g2048` artifact `20260527T024115+0300-g2048` at `597,851` SPS, train score `97.85507202148438`, eval score `49.43283462524414`.
- Second milestone 04d suite: `breakout` artifact `20260527T024131+0300-breakout` at `3,437,921` SPS, train score `2.332063913345337`, eval score `0.0`; `g2048` artifact `20260527T024142+0300-g2048` at `595,575` SPS, train score `97.85507202148438`, eval score `49.43283462524414`.
- Extra g2048 runner: `20260527T024206+0300-g2048` at `597,244` SPS, train score `97.85507202148438`, eval score `49.43283462524414`. This extra run was added because the two-run median was only just inside the 1 percent gate.
- Medians: `breakout` `3,449,313` SPS, `-0.35%` versus milestone 04c accepted median `3,461,400`; `g2048` three-run median `597,244` SPS, `-0.89%` versus milestone 04c accepted median `602,626`.
- Native Metal parity and overlay surface passed in both milestone 04d suites.
- Interactive smoke artifact: `milestone-04d-dead-helper-cleanup/20260527T024228+0300-interactive-breakout-dead-helper-cleanup`. It built, loaded `resources/breakout/breakout_weights.bin`, reached raylib 5.5 GLFW initialization, then failed with `Failed to determine Monitor to center Window` and exit code `139`, matching the known desktop windowing boundary.
- Subagent review: Meitner found no blocking findings, verified the deleted helpers have no live Metal references or dynamic lookup path, confirmed the 04d runner allowlist is exact, and checked STATUS LOC and benchmark arithmetic.
- Decision: accepted after subagent review. This is a small dead-code cleanup with repeated runtime medians inside the LOC gate.

## 2026-05-27 Milestone 04e Host Dispatch Helper Candidate

- Root-cause hypothesis: `src/metal/kernels.mm` still repeats the same one-dimensional host dispatch shape for unary and binary kernels: resolve stream, begin PSO, bind one or two pointers, bind params, dispatch. Local typed helpers can remove that duplication while preserving kernel names, argument indices, parameter structs, dispatch counts, barriers, and math.
- Risk classification: medium. This touches host dispatch wrappers used by optimizer and copy/cast paths, but not shader math or policy state. Acceptance still requires repeated `breakout` and `g2048`, parity, overlay, interactive smoke, and subagent review.
- Acceptance gate: use the dedicated `milestone-04e-dispatch-helper-cleanup` runners, require median SPS within 1 percent of the current accepted baseline or better, and reject if either env shows score or eval drift.

## 2026-05-27 Milestone 04e Host Dispatch Helper Results

- Candidate code change: temporarily consolidated repeated one-dimensional unary and binary host dispatch wrappers in `src/metal/kernels.mm` through local typed helpers. The candidate would have reduced `src/metal/kernels.mm` from `1,504` to `1,481` lines, but it is not kept.
- Static validation passed before benchmark rejection: `tools/metal/build.sh breakout`, `tools/metal/build.sh g2048`, `git diff --check`, `bash -n` for all milestone 04e runners, and `PYTHONPATH=$PWD python -m pytest tests/metal/test_overlay_surface.py`.
- First milestone 04e suite: `breakout` artifact `20260527T025123+0300-breakout` at `3,447,305` SPS, train score `2.359738349914551`, eval score `0.0`; `g2048` artifact `20260527T025134+0300-g2048` at `589,195` SPS, train score `97.85507202148438`, eval score `49.43283462524414`.
- Second milestone 04e suite: `breakout` artifact `20260527T025149+0300-breakout` at `3,440,061` SPS, train score `2.3505947589874268`, eval score `0.0`; `g2048` artifact `20260527T025159+0300-g2048` at `587,363` SPS, train score `97.85507202148438`, eval score `49.43283462524414`.
- Medians: `breakout` `3,443,683` SPS, `-0.16%` versus milestone 04d accepted median `3,449,313`; `g2048` `588,279` SPS, `-1.50%` versus milestone 04d accepted median `597,244`.
- Native Metal parity and overlay surface passed in both milestone 04e suites.
- Decision: rejected. The candidate exceeded the 1 percent g2048 LOC gate, so `src/metal/kernels.mm` was restored to the 04d accepted state. Only the rejection artifacts, runner scripts, overlay allowlist, and this status entry remain for audit.

## 2026-05-27 Milestone 04f Steel GEMM Template Candidate

- Root-cause hypothesis: `steel_gemm` and `steel_gemm_f16` duplicate the same Metal tile traversal, three GEMM layout branches, K-remainder path, accumulator staging, and scalar edge store. A typed MSL helper can keep both exported kernel names unchanged while preserving fp32 direct fast-store behavior and fp16 staged half-output behavior.
- Risk classification: high. This touches the unaligned GEMM fallback path and fp16 fallback path, so acceptance requires runtime Metal JIT success, build success, repeated `breakout` and `g2048` runs, parity, overlay, interactive smoke, and subagent review.
- Acceptance gate: use the dedicated `milestone-04f-steel-gemm-template` runners, require median SPS within 1 percent of the current accepted baseline or better, require train and eval scores comparable to milestone 04d, and reject immediately on compile, parity, determinism, or score drift.

## 2026-05-27 Milestone 04f Steel GEMM Template Results

- First 04f suite attempt artifact `20260527T030833+0300-breakout` failed during Metal JIT. Root cause: program-scope constants needed `constant` address space, and threadgroup memory had to be declared in the exported kernel wrappers rather than inside the inline helper. The candidate was patched in place and rerun from the top.
- Code change: replace duplicated fp32 and fp16 Steel GEMM tile traversal, layout branches, K-remainder loading, and scalar edge store with one typed MSL helper plus two exported wrappers. Exported kernel names remain `steel_gemm` and `steel_gemm_f16`. The fp32 wrapper keeps the direct simdgroup fast-store path, while the fp16 wrapper keeps the staged half-output store and pre-store barrier.
- LOC: `src/metal/shader_src.h` is `2,361` lines, down from `2,522` after milestone 04d. Kernel scope is `3,982`, down from `4,143` after milestone 04d and down from setup `4,615`. Live backend scope is `9,542`, down from `9,699` before 04f and down from setup `10,160`.
- Static validation passed: `tools/metal/build.sh breakout`, `git diff --check`, `bash -n` for all milestone 04f runners, and `PYTHONPATH=$PWD python -m pytest tests/metal/test_overlay_surface.py`.
- First successful milestone 04f suite: `breakout` artifact `20260527T030928+0300-breakout` at `3,486,329` SPS, train score `2.109865427017212`, eval score `0.0`; `g2048` artifact `20260527T030940+0300-g2048` at `597,526` SPS, train score `97.85507202148438`, eval score `49.43283462524414`.
- Second milestone 04f suite: `breakout` artifact `20260527T030957+0300-breakout` at `3,398,993` SPS, train score `1.8505362272262573`, eval score `0.0`; `g2048` artifact `20260527T031007+0300-g2048` at `596,433` SPS, train score `97.85507202148438`, eval score `49.43283462524414`.
- Medians: `breakout` `3,442,661` SPS, `-0.19%` versus milestone 04d accepted median `3,449,313`; `g2048` `596,980` SPS, `-0.04%` versus milestone 04d accepted median `597,244`.
- Native Metal parity and overlay surface passed in both successful milestone 04f suites.
- Interactive smoke artifact: `milestone-04f-steel-gemm-template/20260527T031048+0300-interactive-breakout-steel-gemm-template`. It built, loaded `resources/breakout/breakout_weights.bin`, reached raylib 5.5 GLFW initialization, then failed with `Failed to determine Monitor to center Window` and exit code `139`, matching the known desktop windowing boundary.
- Subagent review: Aristotle found no blocking findings, verified exported Steel GEMM names, fp32 fast-store behavior, fp16 pre-store barrier behavior, exact runner allowlist scope, and STATUS LOC and benchmark arithmetic.
- Decision: accepted after subagent review. This is the largest kept kernel-scope LOC cleanup of the night so far and remains inside the throughput and learnability gates.

## 2026-05-27 Milestone 04g Tiny Kernel Cleanup Candidate

- Root-cause hypothesis: after the large Steel GEMM cleanup, several small pieces of kernel-source dead plumbing and duplication remain: reset MinGRU helpers accept unused buffers only to preserve exported wrapper slots, `prio_imp_weights_kernel` declares unused thread attributes, `transpose_01` duplicates index math for `float` and `uint2`, index copy and gather duplicate row-copy loops, and `SmallGemmParams.M` is never read by the shader.
- Risk classification: medium. This touches scan wrappers, rollout row copy, transpose, and the small unaligned GEMM fallback. The exported kernel names and buffer slots stay unchanged, and host param layout changes with the shader for `SmallGemmParams`.
- Acceptance gate: use the dedicated `milestone-04g-tiny-kernel-cleanup` runners, require median SPS within 1 percent of the current accepted baseline or better, require train and eval scores comparable to milestone 04f, and reject on compile, parity, determinism, or score drift.

## 2026-05-27 Milestone 04g Tiny Kernel Cleanup Results

- Code change: remove dead MinGRU reset helper parameters while preserving exported kernel buffer slots, remove unused priority-weight thread attributes, share `transpose_01` index math through a typed helper, share index copy and gather row-copy loops, and remove unused `SmallGemmParams.M` with the host parameter layout updated in `small_gemm_nt_dispatch`.
- LOC: `src/metal/shader_src.h` is `2,346` lines, down from `2,361` after milestone 04f. Kernel scope is `3,967`, down from `3,982` after milestone 04f and down from setup `4,615`. Live backend scope is `9,531`, down from `9,542` after milestone 04f and down from setup `10,160`.
- Static validation passed: `git diff --check`, `bash -n` for all milestone 04g runners, and `PYTHONPATH=$PWD python -m pytest tests/metal/test_overlay_surface.py`.
- First milestone 04g suite: `breakout` artifact `20260527T031828+0300-breakout` at `3,417,038` SPS, train score `2.611736297607422`, eval score `0.0`; `g2048` artifact `20260527T031840+0300-g2048` at `590,317` SPS, train score `97.85507202148438`, eval score `49.212120056152344`.
- Second milestone 04g suite: `breakout` artifact `20260527T031857+0300-breakout` at `3,481,110` SPS, train score `2.367737054824829`, eval score `0.0`; `g2048` artifact `20260527T031908+0300-g2048` at `598,345` SPS, train score `97.85507202148438`, eval score `49.43283462524414`.
- Extra g2048 runner: `20260527T031940+0300-g2048` at `596,033` SPS, train score `97.85507202148438`, eval score `49.43283462524414`. This extra run was added because the first g2048 sample missed the 1 percent gate and had a slightly lower single eval sample.
- Medians: `breakout` `3,449,074` SPS, `+0.19%` versus milestone 04f accepted median `3,442,661`; `g2048` three-run median `596,033` SPS, `-0.16%` versus milestone 04f accepted median `596,980`.
- Native Metal parity and overlay surface passed in both milestone 04g suites.
- Interactive smoke artifact: `milestone-04g-tiny-kernel-cleanup/20260527T032004+0300-interactive-breakout-tiny-kernel-cleanup`. It built, loaded `resources/breakout/breakout_weights.bin`, reached raylib 5.5 GLFW initialization, then failed with `Failed to determine Monitor to center Window` and exit code `139`, matching the known desktop windowing boundary.
- Subagent review: Hooke found no blocking findings, verified MinGRU exported buffer slots, priority-weight buffer contract, typed transpose semantics, indexed row-copy address spaces, `SmallGemmParams` host and shader layout, exact runner allowlist scope, and STATUS LOC and benchmark arithmetic.
- Decision: accepted after subagent review. This is a small kernel-scope cleanup with repeated runtime medians inside the LOC gate.

## 2026-05-27 Milestone 04h Host Cleanup Candidate

- Root-cause hypothesis: after the kernel-source passes, two host-side duplication clusters remain: six tensor-ops forwarding wrappers only pass a cached PSO into `tensor_ops_dispatch`, and `puf_transpose_01(FloatTensor)` duplicates the existing `PufTensor` transpose dispatch shape. Inlining the PSO at call sites and delegating the FloatTensor overload should reduce backend LOC without changing PSO names, argument binding, dispatch dimensions, shader code, or tensor data flow.
- Risk classification: low to medium. This is host dispatch code, not shader math, but tensor-ops GEMM dispatch and transpose are live paths. Acceptance requires the same repeated benchmark, parity, overlay, interactive smoke, and subagent review gates.
- Acceptance gate: use the dedicated `milestone-04h-host-cleanup` runners, require median SPS within 1 percent of the current accepted baseline or better, and reject on compile, parity, determinism, or score drift.

## 2026-05-27 Milestone 04h Host Cleanup Results

- Candidate code change: temporarily removed six tensor-ops forwarding wrappers and delegated `puf_transpose_01(FloatTensor)` through the `PufTensor` overload. The candidate would have reduced `src/metal/kernels.mm` from `1,504` to `1,494` lines and full `src/metal/platform.mm` from `1,093` to `1,071` lines, but it is not kept.
- Static validation passed before benchmark rejection: `git diff --check`, `bash -n` for all milestone 04h runners, and `PYTHONPATH=$PWD python -m pytest tests/metal/test_overlay_surface.py`.
- First milestone 04h suite: `breakout` artifact `20260527T032735+0300-breakout` at `3,414,858` SPS, train score `2.3648035526275635`, eval score `0.0`; `g2048` artifact `20260527T032747+0300-g2048` at `582,103` SPS, train score `97.85507202148438`, eval score `49.43283462524414`.
- Second milestone 04h suite: `breakout` artifact `20260527T032805+0300-breakout` at `3,457,217` SPS, train score `2.671255111694336`, eval score `0.0`; `g2048` artifact `20260527T032816+0300-g2048` at `581,083` SPS, train score `97.85507202148438`, eval score `49.43283462524414`.
- Medians: `breakout` `3,436,038` SPS, `-0.38%` versus milestone 04g accepted median `3,449,074`; `g2048` `581,593` SPS, `-2.42%` versus milestone 04g accepted median `596,033`.
- Native Metal parity and overlay surface passed in both milestone 04h suites.
- Decision: rejected. The candidate exceeded the 1 percent g2048 LOC gate, so `src/metal/kernels.mm` and `src/metal/platform.mm` were restored to the 04g accepted state. Only rejection artifacts, runner scripts, overlay allowlist, and this status entry remain for audit.

## 2026-05-27 Milestone 05 Final Audit

- Current branch and commit: `valtteri/metal-kernel-loc-night-20260527` at `f67656e87`.
- Current kernel scope: `3,967` lines by the established convention: `src/metal/shader_src.h` `2,346`, `src/metal/kernels.mm` `1,504`, and the `src/metal/platform.mm` tensor-ops shader block `117`. Net accepted kernel-scope delta versus setup `4,615`: `-648` lines.
- Current backend scope: `9,535` lines across `src/metal`, `tools/metal`, and `tests/metal`. Net accepted backend-scope delta versus setup `10,160`: `-625` lines. This includes audit allowlist growth for rejected and accepted milestone runners.
- Final audit runner passed: `artifacts/metal-kernel-loc-20260527/milestone-05-final-audit/run-suite.sh`.
- Final audit `breakout` artifact `20260527T033222+0300-breakout`: `3,457,145` SPS, train score `2.242490768432617`, eval score `0.0`, uptime `1.2224819660186768`.
- Final audit `g2048` artifact `20260527T033233+0300-g2048`: `589,636` SPS, train score `97.85507202148438`, eval score `49.43283462524414`, uptime `0.44462013244628906`.
- Native Metal parity passed and wrote `milestone-05-final-audit/native-metal.json`. Overlay surface test passed.
- Interactive smoke artifact: `milestone-05-final-audit/20260527T033243+0300-interactive-breakout-final-audit`. It built, loaded `resources/breakout/breakout_weights.bin`, reached raylib 5.5 GLFW initialization, then failed with `Failed to determine Monitor to center Window` and exit code `139`, matching the known desktop windowing boundary.
- Accepted changes: dispatch helper consolidation, shared dispatch helper, tensor-ops shader template, MinGRU scan template, dead shader kernel deletion, dead helper deletion, Steel GEMM shader template, and tiny shader plumbing cleanup.
- Rejected changes: first dead shader cleanup against the setup baseline due g2048 miss, host dispatch helper cleanup due `-1.50%` g2048 median, and host tensor-ops/transpose cleanup due `-2.42%` g2048 median.
- Open risk: the final single g2048 audit sample `589,636` is below the accepted 04g three-run median `596,033`, but train and eval were unchanged and the sample sits inside the observed same-night variation. No accepted code change is based on this single audit sample.

## 2026-05-27 Milestone 04i Parameter Cleanup Candidate

- Root-cause hypothesis: several live Metal kernels still carry unused parameter fields or one-field parameter wrapper structs that add source bulk without encoding richer state. Converting those wrappers to scalar `constant int&` bindings and sharing the identical float-plus-count parameter layout should reduce kernel LOC without changing arithmetic, dispatch order, RNG, reductions, exported kernel names, or buffer indices.
- Candidate code change: remove unused `SampleParams.num_atns_total`, `SampleParams.logstd_stride`, and `PPOFusedParams.num_atns_total`; replace one-field MSL parameter structs for PPO reduction, priority normalization, add, norm, norm reduction, variance and mean, and u8 cast with scalar bindings; share `fill_f32`, `scale_f32`, and `axpy_f32` through one `FloatCountParams` layout; update matching host argument payloads.
- LOC before validation: `src/metal/shader_src.h` is `2,305` lines, down from accepted `2,346`; `src/metal/kernels.mm` is `1,486`, down from accepted `1,504`. Kernel scope is `3,908`, down from accepted `3,967` and setup `4,615`. Backend scope is `9,480`, down from accepted `9,535` and setup `10,160`.
- Risk classification: medium. This is meant to be ABI-clear cleanup, but it touches live PPO, priority sampling, optimizer norm, cast, fill, scale, axpy, and add paths. Acceptance requires build success, repeated `breakout` and `g2048`, native parity, overlay surface, interactive smoke, comparable train and eval scores, and subagent review.
- Acceptance gate: use the dedicated `milestone-04i-param-cleanup` runners, require median SPS within 1 percent of the current accepted baseline or better, require learnability and eval to remain comparable, and reject on compile, parity, determinism, or score drift.

## 2026-05-27 Milestone 04i Parameter Cleanup Results

- Candidate code change: temporarily removed unused parameter fields, scalarized one-field parameter structs, and shared identical float-plus-count parameter layout across fill, scale, and axpy. The candidate would have reduced `src/metal/shader_src.h` from `2,346` to `2,305`, `src/metal/kernels.mm` from `1,504` to `1,486`, and kernel scope from `3,967` to `3,908`, but it is not kept.
- Static validation passed before benchmark rejection: `tools/metal/build.sh breakout`, `tools/metal/build.sh g2048`, `git diff --check`, `bash -n` for all milestone 04i runners, and `PYTHONPATH=$PWD python -m pytest tests/metal/test_overlay_surface.py`.
- First milestone 04i suite: `breakout` artifact `20260527T035106+0300-breakout` at `3,421,575` SPS, train score `2.2388393878936768`, eval score `0.0`; `g2048` artifact `20260527T035118+0300-g2048` at `596,314` SPS, train score `97.85507202148438`, eval score `49.43283462524414`.
- Second milestone 04i suite: `breakout` artifact `20260527T035133+0300-breakout` at `3,405,731` SPS, train score `2.549274206161499`, eval score `0.0`; `g2048` artifact `20260527T035144+0300-g2048` at `596,580` SPS, train score `97.85507202148438`, eval score `49.43283462524414`.
- Extra breakout runner: `20260527T035218+0300-breakout` at `3,040,413` SPS, train score `2.1712379455566406`, eval score `0.0`. This extra run was added because the two-run breakout median missed the 1 percent gate by a narrow margin.
- Medians: `breakout` three-run median `3,405,731` SPS, `-1.26%` versus milestone 04g accepted median `3,449,074`; `g2048` two-run median `596,447` SPS, `+0.07%` versus milestone 04g accepted median `596,033`.
- Native Metal parity and overlay surface passed in both milestone 04i suites.
- Decision: rejected. The candidate exceeded the 1 percent breakout SPS gate, so `src/metal/shader_src.h` and `src/metal/kernels.mm` were restored to the accepted 04g state. Only rejection artifacts, runner scripts, overlay allowlist, and this status entry remain for audit.

## 2026-05-27 Milestone 04j Kernel Prose Cleanup Candidate

- Root-cause hypothesis: after the real kernel consolidation passes, the Metal kernel scope still carries stale section banners and comments that repeat function names, tensor shapes, or obvious loop roles. Deleting those lines should reduce source LOC without changing executable MSL or C++ control flow. The one retained Steel GEMM comment names a non-obvious optimization invariant: direct device loads beat threadgroup staging here because Apple Silicon L2 handles tile reuse.
- Candidate code change: delete redundant kernel prose in `src/metal/shader_src.h`, `src/metal/kernels.mm`, and the tensor-ops source preamble in `src/metal/platform.mm`; add dedicated milestone 04j runners and exact overlay allowlist paths.
- LOC before validation: `src/metal/shader_src.h` is `2,297` lines, down from accepted `2,346`; `src/metal/kernels.mm` is `1,496`, down from accepted `1,504`; full `src/metal/platform.mm` is `1,088`, down from accepted `1,093`. Kernel scope is `3,910`, down from accepted `3,967` and setup `4,615`. Backend scope is `9,481`, down from accepted `9,539` and setup `10,160`.
- Risk classification: low. This deletes non-executable prose, but the raw shader string changes, so acceptance still requires build success, repeated `breakout` and `g2048`, native parity, overlay surface, interactive smoke, comparable train and eval scores, and subagent review.
- Acceptance gate: use the dedicated `milestone-04j-prose-cleanup` runners, require median SPS within 1 percent of the current accepted baseline or better, require learnability and eval to remain comparable, and reject on compile, parity, determinism, or score drift.

## 2026-05-27 Milestone 04j Kernel Prose Cleanup Results

- Code change: delete stale section banners and comments that repeated function names, tensor shapes, or local loop roles in `src/metal/shader_src.h`, `src/metal/kernels.mm`, and `src/metal/platform.mm`. One Steel GEMM gotcha comment was retained in a shorter form.
- LOC: `src/metal/shader_src.h` is `2,297` lines, down from accepted `2,346`; `src/metal/kernels.mm` is `1,496`, down from accepted `1,504`; full `src/metal/platform.mm` is `1,088`, down from accepted `1,093`. Kernel scope is `3,910`, down from accepted `3,967` and setup `4,615`. Backend scope is `9,481`, down from accepted `9,539` and setup `10,160`.
- Static validation passed: `tools/metal/build.sh breakout`, `tools/metal/build.sh g2048`, `git diff --check`, `bash -n` for all milestone 04j runners, and `PYTHONPATH=$PWD python -m pytest tests/metal/test_overlay_surface.py`.
- First milestone 04j suite: `breakout` artifact `20260527T040111+0300-breakout` at `3,439,771` SPS, train score `2.497969150543213`, eval score `0.0`; `g2048` artifact `20260527T040122+0300-g2048` at `603,359` SPS, train score `97.85507202148438`, eval score `49.43283462524414`.
- Second milestone 04j suite: `breakout` artifact `20260527T040138+0300-breakout` at `3,446,814` SPS, train score `2.504157304763794`, eval score `0.0`; `g2048` artifact `20260527T040149+0300-g2048` at `606,448` SPS, train score `97.85507202148438`, eval score `49.43283462524414`.
- Medians: `breakout` `3,443,293` SPS, `-0.17%` versus milestone 04g accepted median `3,449,074`; `g2048` `604,904` SPS, `+1.49%` versus milestone 04g accepted median `596,033`.
- Native Metal parity and overlay surface passed in both milestone 04j suites.
- Interactive smoke artifact: `milestone-04j-prose-cleanup/20260527T040210+0300-interactive-breakout-prose-cleanup`. It built, loaded `resources/breakout/breakout_weights.bin`, reached raylib 5.5 GLFW initialization, then failed with `Failed to determine Monitor to center Window` and exit code `139`, matching the known desktop windowing boundary.
- Subagent review: Nash found no blocking findings, verified the diff only removes prose apart from the shorter retained Steel GEMM gotcha comment, confirmed runner and overlay scope, and checked LOC and benchmark arithmetic.
- Decision: accepted after subagent review. This is source cleanup only, with repeated runtime medians inside the LOC gate.

## 2026-05-27 Milestone 04k Unused Parameter Fields Candidate

- Root-cause hypothesis: the broad 04i parameter cleanup mixed several independent changes and missed the breakout SPS gate. A narrower cleanup can remove only fields no live shader reads: `SampleParams.num_atns_total`, `SampleParams.logstd_stride`, and `PPOFusedParams.num_atns_total`, plus the matching host payload fields and one now-unused local variable. This should reduce constant-buffer source without changing math, reductions, wrapper structs, dispatch order, RNG, exported kernel names, or buffer indices.
- Candidate code change: remove the three unused fields from `src/metal/shader_src.h` and `src/metal/kernels.mm`; add dedicated milestone 04k runners and exact overlay allowlist paths.
- LOC before validation: `src/metal/shader_src.h` is `2,294` lines, down from accepted `2,297`; `src/metal/kernels.mm` is `1,491`, down from accepted `1,496`. Kernel scope is `3,902`, down from accepted `3,910` and setup `4,615`. Backend scope is `9,477`, down from accepted `9,481` and setup `10,160`.
- Risk classification: medium. This changes live constant-buffer layouts in sampling and PPO, so acceptance requires build success, repeated `breakout` and `g2048`, native parity, overlay surface, interactive smoke, comparable train and eval scores, and subagent review.
- Acceptance gate: use the dedicated `milestone-04k-unused-param-fields` runners, require median SPS within 1 percent of the current accepted baseline or better, require learnability and eval to remain comparable, and reject on compile, parity, determinism, or score drift.

## 2026-05-27 Milestone 04k Unused Parameter Fields Results

- Code change: remove `SampleParams.num_atns_total`, `SampleParams.logstd_stride`, and `PPOFusedParams.num_atns_total`, plus matching host payload fields and one now-unused local variable.
- LOC: `src/metal/shader_src.h` is `2,294` lines, down from accepted `2,297`; `src/metal/kernels.mm` is `1,491`, down from accepted `1,496`. Kernel scope is `3,902`, down from accepted `3,910` and setup `4,615`. Backend scope is `9,477`, down from accepted `9,481` and setup `10,160`.
- Static validation passed: `tools/metal/build.sh breakout`, `tools/metal/build.sh g2048`, `git diff --check`, `bash -n` for all milestone 04k runners, and `PYTHONPATH=$PWD python -m pytest tests/metal/test_overlay_surface.py`.
- First milestone 04k suite: `breakout` artifact `20260527T041023+0300-breakout` at `3,426,297` SPS, train score `2.4714393615722656`, eval score `0.0`; `g2048` artifact `20260527T041034+0300-g2048` at `597,198` SPS, train score `97.85507202148438`, eval score `49.43283462524414`.
- Second milestone 04k suite: `breakout` artifact `20260527T041048+0300-breakout` at `3,428,588` SPS, train score `2.4022727012634277`, eval score `0.0`; `g2048` artifact `20260527T041059+0300-g2048` at `609,120` SPS, train score `97.85507202148438`, eval score `49.43283462524414`.
- Medians: `breakout` `3,427,443` SPS, `-0.46%` versus milestone 04j accepted median `3,443,293`; `g2048` `603,159` SPS, `-0.29%` versus milestone 04j accepted median `604,904`.
- Native Metal parity and overlay surface passed in both milestone 04k suites.
- Interactive smoke artifact: `milestone-04k-unused-param-fields/20260527T041122+0300-interactive-breakout-unused-param-fields`. It built, loaded `resources/breakout/breakout_weights.bin`, reached raylib 5.5 GLFW initialization, then failed with `Failed to determine Monitor to center Window` and exit code `139`, matching the known desktop windowing boundary.
- Subagent review: Herschel found no blocking findings, verified shader and host struct layouts, confirmed no removed field is read, checked exported names and buffer indices, and validated LOC and benchmark arithmetic.
- Decision: accepted after subagent review. This is a narrow constant-buffer cleanup with repeated runtime medians inside the LOC gate.

## 2026-05-27 Milestone 05 Final Audit Rerun

- Current branch and commit: `valtteri/metal-kernel-loc-night-20260527` at `848c90a816`.
- Current kernel scope: `3,902` lines by the established convention: `src/metal/shader_src.h` `2,294`, `src/metal/kernels.mm` `1,491`, and the `src/metal/platform.mm` tensor-ops shader block `117`. Net accepted kernel-scope delta versus setup `4,615`: `-713` lines.
- Current backend scope: `9,477` lines across `src/metal`, `tools/metal`, and `tests/metal`. Net accepted backend-scope delta versus setup `10,160`: `-683` lines. This includes audit allowlist growth for accepted and rejected milestone runners.
- Final audit runner passed: `artifacts/metal-kernel-loc-20260527/milestone-05-final-audit/run-suite.sh`.
- Final audit `breakout` artifact `20260527T041647+0300-breakout`: `3,393,543` SPS, train score `2.4794721603393555`, eval score `0.0`, uptime `1.2340800762176514`.
- Final audit `g2048` artifact `20260527T041658+0300-g2048`: `584,232` SPS, train score `97.85507202148438`, eval score `49.43283462524414`, uptime `0.448742151260376`.
- Native Metal parity passed and wrote `milestone-05-final-audit/native-metal.json`. Overlay surface test passed.
- Interactive smoke artifact: `milestone-05-final-audit/20260527T041708+0300-interactive-breakout-final-audit`. It built, loaded `resources/breakout/breakout_weights.bin`, reached raylib 5.5 GLFW initialization, then failed with `Failed to determine Monitor to center Window` and exit code `139`, matching the known desktop windowing boundary.
- Accepted changes since the first final audit: kernel prose cleanup and unused parameter field cleanup.
- Open risk: the final single g2048 audit sample `584,232` is below the accepted 04k two-run median `603,159`, but train and eval were unchanged and no accepted code change was based on this single audit sample. Treat the repeated milestone medians as the decision data.

## 2026-05-27 Milestone 04l Scalar Integer Params Candidate

- Root-cause hypothesis: the rejected 04i parameter cleanup mixed scalar integer wrappers with float-plus-count parameter sharing. The float-plus-count sharing may have changed shader compile shape enough to miss the breakout gate. A narrower pass can scalarize only one-field integer parameter wrappers while leaving fill, scale, axpy, clamp, normalize, math, reductions, RNG, exported kernel names, buffer indices, and dispatch order intact.
- Candidate code change: replace `PPOReduceParams`, `PrioNormParams`, `AddParams`, `NormParams`, `NormReduceParams`, `VarMeanParams`, and `CastU8Params` with scalar `constant int&` shader parameters and matching direct host `mtl_set_params` calls; add dedicated milestone 04l runners and exact overlay allowlist paths.
- LOC before validation: `src/metal/shader_src.h` is `2,266` lines, down from accepted `2,294`; `src/metal/kernels.mm` is `1,477`, down from accepted `1,491`. Kernel scope is `3,860`, down from accepted `3,902` and setup `4,615`. Backend scope is `9,439`, down from accepted `9,477` and setup `10,160`.
- Risk classification: medium. This changes live constant-buffer parameter spelling for PPO reduction, priority normalization, add, norm, variance and mean, and u8 cast. Acceptance requires build success, repeated `breakout` and `g2048`, native parity, overlay surface, interactive smoke, comparable train and eval scores, and subagent review.
- Acceptance gate: use the dedicated `milestone-04l-scalar-int-params` runners, require median SPS within 1 percent of the current accepted baseline or better, require learnability and eval to remain comparable, and reject on compile, parity, determinism, or score drift.

## 2026-05-27 Milestone 04l Scalar Integer Params Results

- Code change: replace one-field integer parameter structs for PPO loss reduction, priority normalization, add, norm, norm reduction, variance and mean, and u8 cast with scalar `constant int&` shader parameters and matching direct host `mtl_set_params` calls.
- LOC: `src/metal/shader_src.h` is `2,266` lines, down from accepted `2,294`. `src/metal/kernels.mm` is `1,477`, down from accepted `1,491`. Kernel scope is `3,860`, down from accepted `3,902` and setup `4,615`. Backend scope is `9,439`, down from accepted `9,477` and setup `10,160`.
- Static validation passed: `tools/metal/build.sh breakout`, `tools/metal/build.sh g2048`, `git diff --check`, `bash -n` for all milestone 04l runners, and `PYTHONPATH=$PWD python -m pytest tests/metal/test_overlay_surface.py`.
- First milestone 04l suite: `breakout` artifact `20260527T042403+0300-breakout` at `3,475,730` SPS, train score `2.4309816360473633`, eval score `0.0`. `g2048` artifact `20260527T042414+0300-g2048` at `597,374` SPS, train score `97.85507202148438`, eval score `49.43283462524414`.
- Second milestone 04l suite: `breakout` artifact `20260527T042613+0300-breakout` at `3,472,216` SPS, train score `2.6624293327331543`, eval score `0.0`. `g2048` artifact `20260527T042625+0300-g2048` at `607,532` SPS, train score `97.85507202148438`, eval score `49.43283462524414`.
- Extra g2048 runner: `20260527T042705+0300-g2048` at `592,481` SPS, train score `97.85507202148438`, eval score `49.43283462524414`. This extra run was added because g2048 had been the edge case for prior cleanup candidates.
- Medians: `breakout` two-run median `3,473,973` SPS, `+1.36%` versus milestone 04k accepted median `3,427,443`; `g2048` three-run median `597,374` SPS, `-0.96%` versus milestone 04k accepted median `603,159`.
- Native Metal parity passed and wrote `milestone-04l-scalar-int-params/native-metal.json`. Overlay surface passed in both milestone 04l suites.
- Interactive smoke artifact: `milestone-04l-scalar-int-params/20260527T042726+0300-interactive-breakout-scalar-int-params`. It built, loaded `resources/breakout/breakout_weights.bin`, reached raylib 5.5 GLFW initialization, then failed with `Failed to determine Monitor to center Window` and exit code `139`, matching the known desktop windowing boundary.
- Subagent review: Lovelace found no blocking findings, verified scalar shader and host bindings, confirmed exported names, buffer indices, dispatch dimensions, math and reduction order, runner scope, ignored generated artifacts, and STATUS LOC and benchmark arithmetic.
- Decision: accepted after subagent review. This is a LOC cleanup only, not a throughput claim, because g2048 sits inside the allowed slowdown window.

## 2026-05-27 Milestone 04m Reset Kernel Argument Cleanup Candidate

- Root-cause hypothesis: MinGRU reset wrappers still declare unused exported shader arguments that exist only as dead placeholders. Because every later argument uses an explicit `[[buffer(N)]]` index, removing the dead declarations should preserve host binding slots, exported kernel names, dispatch dimensions, math, reduction order, RNG, and data flow while reducing shader source LOC.
- Candidate code change: remove `unused_buf` from forward reset f32/fp16 kernels and remove unused `state` and `unused_buf` from backward reset f32/fp16 kernels; add dedicated milestone 04m runners and exact overlay allowlist paths.
- LOC before validation: `src/metal/shader_src.h` is `2,260` lines, down from accepted `2,266`. `src/metal/kernels.mm` is unchanged at `1,477`. Kernel scope is `3,854`, down from accepted `3,860` and setup `4,615`. Backend scope is `9,437`, down from accepted `9,439` and setup `10,160`.
- Risk classification: medium. This touches live MinGRU reset kernel signatures, so acceptance requires build success, repeated `breakout` and `g2048`, native parity, overlay surface, interactive smoke, comparable train and eval scores, and subagent review.
- Acceptance gate: use the dedicated `milestone-04m-reset-arg-cleanup` runners, require median SPS within 1 percent of the current accepted baseline or better, require learnability and eval to remain comparable, and reject on compile, parity, determinism, or score drift.

## 2026-05-27 Milestone 04m Reset Kernel Argument Cleanup Results

- Candidate code change: temporarily removed dead exported shader arguments from MinGRU reset wrappers. The candidate would have reduced `src/metal/shader_src.h` from `2,266` to `2,260` and kernel scope from `3,860` to `3,854`, but it is not kept.
- Static validation passed before benchmark rejection: `tools/metal/build.sh breakout`, `tools/metal/build.sh g2048`, `git diff --check`, `bash -n` for all milestone 04m runners, and `PYTHONPATH=$PWD python -m pytest tests/metal/test_overlay_surface.py`.
- First milestone 04m suite: `breakout` artifact `20260527T043601+0300-breakout` at `3,427,071` SPS, train score `2.417233467102051`, eval score `0.0`. `g2048` artifact `20260527T043612+0300-g2048` at `599,462` SPS, train score `97.85507202148438`, eval score `49.43283462524414`.
- Second milestone 04m suite: `breakout` artifact `20260527T043636+0300-breakout` at `3,448,160` SPS, train score `2.4175572395324707`, eval score `0.0`. `g2048` artifact `20260527T043647+0300-g2048` at `595,554` SPS, train score `97.85507202148438`, eval score `49.43283462524414`.
- Extra breakout runner: `20260527T043714+0300-breakout` at `3,420,879` SPS, train score `2.210171937942505`, eval score `0.0`. This extra run was added because the two-run breakout median narrowly missed the 1 percent gate.
- Medians: `breakout` three-run median `3,427,071` SPS, `-1.35%` versus milestone 04l accepted median `3,473,973`; `g2048` two-run median `597,508` SPS, `+0.02%` versus milestone 04l accepted median `597,374`.
- Native Metal parity and overlay surface passed in both milestone 04m suites.
- Decision: rejected. The candidate exceeded the 1 percent breakout SPS gate, so `src/metal/shader_src.h` was restored to the 04l accepted state. Only rejection artifacts, runner scripts, overlay allowlist, and this status entry remain for audit.

## 2026-05-27 Milestone 04n Comment Cleanup Candidate

- Root-cause hypothesis: the accepted kernel files still contain comments that repeat branch names, visible formulas, field names, and dispatch shapes. Deleting that prose should reduce source LOC without changing executable C++ or MSL tokens apart from comments and trailing inline text.
- Candidate code change: remove redundant comments from `src/metal/shader_src.h`, `src/metal/kernels.mm`, and `src/metal/platform.mm`, while keeping comments that explain precision drift, NaN avoidance, determinism, Metal visibility, and measured Steel GEMM behavior.
- LOC before validation: `src/metal/shader_src.h` is `2,220` lines, down from accepted `2,266`. `src/metal/kernels.mm` is `1,447`, down from accepted `1,477`. Full `src/metal/platform.mm` is `997`, down from accepted `1,088`. Kernel scope is `3,784`, down from accepted `3,860` and setup `4,615`. Backend scope is `9,280` after runner allowlist growth, down from accepted `9,443` after the rejected 04m audit commit.
- Risk classification: low. This is comment-only cleanup, but the embedded shader source string changes, so acceptance still requires build success, repeated `breakout` and `g2048`, native parity, overlay surface, interactive smoke, comparable train and eval scores, and subagent review.
- Acceptance gate: use the dedicated `milestone-04n-comment-cleanup` runners, require median SPS within 1 percent of the current accepted baseline or better, require learnability and eval to remain comparable, and reject on compile, parity, determinism, or score drift.

## 2026-05-27 Milestone 04n Comment Cleanup Results

- Candidate code change: temporarily removed redundant comments from Metal shader and host source. The candidate would have reduced kernel scope from `3,860` to `3,784`, but it is not kept.
- Static validation passed before benchmark rejection: `tools/metal/build.sh breakout`, `tools/metal/build.sh g2048`, `git diff --check`, `bash -n` for all milestone 04n runners, and `PYTHONPATH=$PWD python -m pytest tests/metal/test_overlay_surface.py`.
- First milestone 04n suite: `breakout` artifact `20260527T044635+0300-breakout` at `3,453,085` SPS, train score `2.370234489440918`, eval score `0.0`. `g2048` artifact `20260527T044646+0300-g2048` at `593,241` SPS, train score `97.85507202148438`, eval score `49.43283462524414`.
- Second milestone 04n suite: `breakout` artifact `20260527T044715+0300-breakout` at `3,402,927` SPS, train score `2.1449999809265137`, eval score `0.0`. `g2048` artifact `20260527T044726+0300-g2048` at `599,530` SPS, train score `97.85507202148438`, eval score `49.43283462524414`.
- Extra breakout runner: `20260527T044752+0300-breakout` at `3,407,134` SPS, train score `2.201909065246582`, eval score `0.0`. This extra run was added because the two-run breakout median missed the 1 percent gate.
- Medians: `breakout` three-run median `3,407,134` SPS, `-1.93%` versus milestone 04l accepted median `3,473,973`. `g2048` two-run median `596,386` SPS, `-0.17%` versus milestone 04l accepted median `597,374`.
- Native Metal parity and overlay surface passed in both milestone 04n suites.
- Decision: rejected. The candidate exceeded the 1 percent breakout SPS gate, so `src/metal/shader_src.h`, `src/metal/kernels.mm`, and `src/metal/platform.mm` were restored to the 04l accepted state. Only rejection artifacts, runner scripts, overlay allowlist, and this status entry remain for audit.
