# Kernel LOC Overnight Status V2

## 2026-05-27 07:50 EEST

- User asked to resume the overnight Metal 4 kernel LOC optimization goal from current `metal-local`.
- Machine clock reports `Wed May 27 07:50:40 EEST 2026`, while the requested run window is through 2026-05-28 09:00 EEST.
- `metal-local` worktree is occupied by unrelated OSRS PVP edits, but Metal-owned paths are clean.
- Created fresh worktree `/Users/valtterivalo/.codex/worktrees/metal-kernel-loc-night-20260527-v2/pufferlib-metal`.
- Created branch `valtteri/metal-kernel-loc-night-20260527-v2` from `metal-local` at `b490ce0a6`.
- Updated heartbeat automation `pufferlib-metal-kernel-loc-overnight-loop` to point at the v2 worktree and artifact root.
- Root-cause hypothesis for setup: current `metal-local` does not contain the accepted v1 kernel LOC branch, so the first high-signal candidate is to port accepted source reductions and revalidate them against this branch.

## 2026-05-27 08:05 EEST

- Subagent harness review found three blockers before setup commit: CPU-inference-only benchmarks could miss GPU rollout kernel breakage, overlay base used mutable `metal-local`, and suite scripts did not enforce repeated-run medians.
- Fixing setup before commit by pinning overlay base to `b490ce0a6`, adding two main runs per env, adding median-gate enforcement, copying config files, and adding real GPU-inference smoke runs for both benchmark envs.

## 2026-05-27 08:13 EEST

- Subagent re-review found two remaining harness blockers: median gate could pass from stale run artifacts, and the automated gate ignored score and eval collapse.
- Tightening `check-median-gate.py` to use only the latest `git_sha` plus `git_diff_sha256` cohort per env, require at least two runs from that cohort, and fail if train score or positive eval score drops below configured collapse floors.

## 2026-05-27 08:15 EEST

- Milestone 00 baseline executed the benchmark and smoke runs but failed during milestone summarization.
- Root cause: eval logs emit raw `env/score`, while `summarize-milestone.py` and `check-median-gate.py` expected a normalized `score` key.
- Fixing the harness parser and rerunning milestone 00 after review.

## 2026-05-27 08:18 EEST

- Milestone 00 rerun passed, including repeated main runs, GPU-inference smoke, native parity, overlay guard, and summarization.
- Found one audit-quality issue after the pass: `milestone-summary.json` includes pre-fix stale runs in aggregate medians, while `check-median-gate.py` correctly uses only the latest git/diff cohort.
- Aligning the summary helper with the gate script before recording baseline numbers in status.

## 2026-05-27 08:20 EEST

- Milestone 00 accepted baseline from latest fixed-run cohort at commit `918a901b6`.
- Main CPU-overlap baseline medians:
  - `breakout`: `3,434,839` SPS, train score `2.4234288930892944`, eval score `0.0`, median uptime `1.2305485010147095`.
  - `g2048`: `606,783` SPS, train score `97.85507202148438`, eval score `49.43283462524414`, median uptime `0.4320704936981201`.
- GPU-inference smoke passed for both envs:
  - `breakout`: `20260527T081642+0300-breakout`, `3,622,999` SPS, train score `0.9873490333557129`, eval score `0.0`.
  - `g2048`: `20260527T081652+0300-g2048`, `854,334` SPS, train score `97.85507202148438`, eval score `42.0`.
- Baseline LOC:
  - `src/metal/shader_src.h`: `2809` lines.
  - `src/metal/kernels.mm`: `1586` lines.
  - `src/metal/platform.mm`: `1213` full lines, tensor-ops shader block `216` lines.
  - Kernel scope total: `4611` lines.
  - Backend scope total across tracked `src/metal`, `tools/metal`, and `tests/metal`: `10174` lines.
- Milestone 00 artifacts: `artifacts/metal-kernel-loc-20260527-v2/milestone-00-baseline`.
- Starting milestone 01 source port.

## 2026-05-27 08:23 EEST

- Milestone 01 candidate: ported the final accepted v1 source state into `src/metal/kernels.mm`, `src/metal/platform.h`, `src/metal/platform.mm`, and `src/metal/shader_src.h`.
- Candidate LOC delta before rejection:
  - `src/metal/shader_src.h`: `2809` to `2162`, `-647`.
  - `src/metal/kernels.mm`: `1586` to `1413`, `-173`.
  - `src/metal/platform.mm`: tensor-ops shader block `216` to `113`, `-103`.
  - Kernel scope total: `4611` to `3688`, `-923`.
  - Backend scope total: `10174` to `9236`, `-938`.
- Validation run:
  - `tools/metal/build.sh breakout` passed.
  - `tools/metal/build.sh g2048` passed.
  - Milestone 01 suite reached the median gate after repeated main runs, GPU-inference smoke, native parity, and overlay guard.
- Decision: rejected and reverted source port.
- Rejection reason: `g2048` missed the LOC gate after three main runs.
  - Baseline `g2048` median: `606,783` SPS.
  - Required 1 percent floor: `600,715.17` SPS.
  - Candidate `g2048` median: `598,730` SPS, `-1.327163087957306%`.
  - Candidate train score remained `97.85507202148438`, eval score remained `49.43283462524414`, so this was a throughput gate failure, not a learnability failure.
  - `breakout` passed: candidate median `3,447,708` SPS versus baseline `3,434,839`, `+0.37466093752864804%`, eval `0.0`.
- Source restored to `HEAD`; no backend source diff remains.
- Rejection artifacts: `artifacts/metal-kernel-loc-20260527-v2/milestone-01-port-v1-kernel-loc`.

## 2026-05-27 08:25 EEST

- Because milestone 01 was rejected, milestone 02 must compare against `milestone-00-baseline`, not the rejected milestone 01 folder.
- Updating the milestone 02 suite gate before trying a smaller LOC candidate.

## 2026-05-27 08:32 EEST

- Milestone 02 candidate: dead-code-only source subset from the previous accepted v1 run.
- Candidate LOC delta before rejection:
  - `src/metal/shader_src.h`: `2809` to `2653`, `-156`.
  - `src/metal/kernels.mm`: `1586` to `1562`, `-24`.
  - `src/metal/platform.mm`: `1213` to `1212`, `-1`.
  - Kernel scope total: `4611` to `4431`, `-180`.
- Validation run:
  - `tools/metal/build.sh breakout` passed.
  - `tools/metal/build.sh g2048` passed.
  - Milestone 02 suite reached the median gate after repeated main runs, GPU-inference smoke, native parity, and overlay guard.
- Decision: rejected and reverted source changes.
- Rejection reason: `g2048` missed the LOC gate after two main runs.
  - Baseline `g2048` median: `606,783` SPS.
  - Required 1 percent floor: `600,715.17` SPS.
  - Candidate `g2048` median: `588,629` SPS, `-2.9918438716971307%`.
  - Candidate train score remained `97.85507202148438`, eval score remained `49.43283462524414`, so this was a throughput gate failure, not a learnability failure.
  - `breakout` passed the gate but regressed to `3,421,469` SPS versus baseline `3,434,839`, `-0.38924677401181995%`.
- Rejection artifacts: `artifacts/metal-kernel-loc-20260527-v2/milestone-02-kernel-consolidation`.
- Updated root-cause hypothesis: edits inside `shader_src.h` can change Metal compilation or code layout enough to move g2048 throughput even when removed kernels are not on the measured path. Future LOC candidates should isolate host-side cleanup first, or touch shader source only in tiny audited slices.

## 2026-05-27 08:36 EEST

- Starting milestone 03 cleanup candidate.
- Root-cause hypothesis: `src/metal/kernels.mm` repeats the same host setup sequence across many dispatch wrappers: activate the compute encoder, look up a PSO by name, and bind it. A tiny helper can remove duplicated host code while preserving MSL source text, PSO names, argument binding order, dispatch dimensions, barriers, RNG state, optimizer math, and policy data flow.
- Acceptance gate remains the full milestone suite against the accepted milestone 00 baseline. This is a LOC cleanup, so median SPS must stay within the 1 percent floor, scores and positive eval must remain comparable, GPU-inference smoke must pass, native parity and overlay must pass, and commit still requires subagent review.
- Because milestones 01 and 02 were both rejected, milestone 03 must compare against `milestone-00-baseline`. Updated the milestone 03 runner gate before executing benchmarks.

## 2026-05-27 08:39 EEST

- Milestone 03 candidate: add `mtl_begin_kernel` in `src/metal/kernels.mm` and replace repeated compute-encoder, PSO lookup, and PSO bind triplets.
- Candidate LOC delta before rejection:
  - `src/metal/kernels.mm`: `1586` to `1512`, `-74`.
  - Kernel scope total: `4611` to `4537`, `-74`.
- Validation run:
  - Repeated main train and eval ran for both `breakout` and `g2048`.
  - GPU-inference smoke passed for both envs.
  - Native Metal parity wrote `milestone-03-cleanup/native-metal.json`.
  - Overlay surface test passed.
  - Interactive smoke did not run because the median gate stopped the suite first.
- Decision: rejected and reverted source changes.
- Rejection reason: `g2048` missed the LOC gate after two main runs.
  - Baseline `g2048` median: `606,783` SPS.
  - Required 1 percent floor: `600,715.17` SPS.
  - Candidate `g2048` median: `599,601.5` SPS, `-1.1835367833311072%`.
  - Candidate train score remained `97.85507202148438`, eval score remained `49.43283462524414`, so this was a throughput gate failure.
  - `breakout` passed: candidate median `3,406,360` SPS versus baseline `3,434,839`, `-0.8291218307466486%`, eval `0.0`.
- Rejection artifacts: `artifacts/metal-kernel-loc-20260527-v2/milestone-03-cleanup`.
- Decision discipline: not adding after-the-fact runs to rescue a near miss. The first full gate failed, so the source cleanup is out.

## 2026-05-27 08:43 EEST

- Starting milestone 04 second kernel LOC candidate.
- Root-cause hypothesis: after the failed behavioral cleanups, the kernel scope still contains stale section banners and prose that repeat function names, tensor shapes, or obvious local loop roles. Deleting those lines should reduce source LOC without changing executable C++ control flow or MSL semantics. The one retained Steel GEMM comment names a non-obvious optimization invariant: direct device loads beat threadgroup staging because Apple Silicon L2 handles tile reuse.
- Because milestones 01, 02, and 03 were rejected, milestone 04 must compare against `milestone-00-baseline`. Updated the milestone 04 runner gate before executing benchmarks and added the missing interactive smoke step after the median gate.
- Acceptance gate remains the full milestone suite against the accepted milestone 00 baseline. This is a LOC cleanup, so median SPS must stay within the 1 percent floor, scores and positive eval must remain comparable, GPU-inference smoke must pass, native parity and overlay must pass, interactive use must reach the known platform boundary or better, and commit still requires subagent review.
- Candidate LOC before validation:
  - `src/metal/shader_src.h`: `2809` to `2760`, `-49`.
  - `src/metal/kernels.mm`: `1586` to `1578`, `-8`.
  - `src/metal/platform.mm`: `1213` to `1208`, `-5` full-file lines, with the counted tensor-ops shader block unchanged at `216`.
  - Kernel scope total: `4611` to `4554`, `-57`.

## 2026-05-27 08:47 EEST

- Milestone 04 candidate passed the full suite.
- Code change: deleted stale section banners and comments that repeated function names, tensor shapes, or obvious local loop roles in `src/metal/shader_src.h`, `src/metal/kernels.mm`, and `src/metal/platform.mm`. Retained one shorter Steel GEMM invariant comment.
- Accepted LOC delta:
  - `src/metal/shader_src.h`: `2809` to `2760`, `-49`.
  - `src/metal/kernels.mm`: `1586` to `1578`, `-8`.
  - `src/metal/platform.mm`: `1213` to `1208`, `-5` full-file lines, with the counted tensor-ops shader block unchanged at `216`.
  - Kernel scope total: `4611` to `4554`, `-57`.
  - Backend scope total: `10174` to `10112`, `-62`.
- Main CPU-overlap medians:
  - `breakout`: `3,421,539.5` SPS versus baseline `3,434,839`, `-0.387194276063596%`; train score `2.310427665710449`, eval score `0.0`.
  - `g2048`: `604,841.5` SPS versus baseline `606,783`, `-0.3199661163875711%`; train score `97.85507202148438`, eval score `49.43283462524414`.
- Main run artifacts:
  - `breakout`: `20260527T084342+0300-breakout`, `20260527T084404+0300-breakout`.
  - `g2048`: `20260527T084354+0300-g2048`, `20260527T084415+0300-g2048`.
- GPU-inference smoke passed for both envs:
  - `breakout`: `20260527T084425+0300-breakout`, `3,613,712` SPS, train score `0.9953243732452393`, eval score `0.0`.
  - `g2048`: `20260527T084435+0300-g2048`, `872,777` SPS, train score `97.85507202148438`, eval score `42.0`.
- Native Metal parity wrote `milestone-04-second-kernel-loc/native-metal.json`.
- Overlay surface test passed.
- Interactive smoke artifact: `milestone-04-second-kernel-loc/20260527T084446+0300-interactive-breakout-second-kernel-loc`. It built, invoked eval with `resources/breakout/breakout_weights.bin`, reached raylib 5.5 GLFW initialization, then failed with `Failed to determine Monitor to center Window` and exit code `139`, matching the known desktop windowing boundary.
- Decision: accepted pending subagent review. This is a source LOC cleanup with no executable behavior change and repeated runtime medians inside the LOC gate.

## 2026-05-27 08:56 EEST

- Milestone 05 final audit failed for the committed milestone 04 prose cleanup.
- First final audit run failed on `g2048`: clean committed median `596,486.5` SPS versus required `598,793.085` SPS from milestone 04.
- Ran one full final-audit rerun with the failed clean samples kept in the same cohort. The combined clean cohort still failed:
  - `breakout`: four-run median `3,479,308.5` SPS versus milestone 04 median `3,421,539.5`, `+1.6883920235321037%`; train score `2.4856642484664917`, eval score `0.0`.
  - `g2048`: four-run median `596,477.5` SPS versus milestone 04 median `604,841.5`, `-1.3828416204906602%`; train score `97.85507202148438`, eval score `49.43283462524414`.
- Final audit artifacts: `artifacts/metal-kernel-loc-20260527-v2/milestone-05-final-audit`.
- GPU-inference smoke, native parity, and overlay ran before the final-audit median gate failed on both attempts.
- Interactive smoke did not run in milestone 05 because the median gate stopped both final-audit suite attempts first.
- Decision: rejected the prose cleanup after final audit. Restored `src/metal/kernels.mm`, `src/metal/platform.mm`, and `src/metal/shader_src.h` to the pre-cleanup source state.
- Current net source LOC after restore:
  - `src/metal/shader_src.h`: `2809` lines.
  - `src/metal/kernels.mm`: `1586` lines.
  - `src/metal/platform.mm`: `1213` full-file lines, tensor-ops shader block `216`.
  - Kernel scope total: `4611` lines, net `0` versus baseline.
  - Backend scope total: `10174` lines, net `0` versus baseline.

## 2026-05-27 09:27 EEST

- Heartbeat resumed the overnight loop from clean commit `85f3ef428`.
- Starting milestone 06 dead-prototype candidate.
- Root-cause hypothesis: the top-of-file declarations for `mtl_mingru_scan_forward_fp16`, `mtl_mingru_scan_backward_fp16`, and `mtl_assemble_decoder_grad_f32_to_f16` are redundant because their definitions appear before every call site in `src/metal/kernels.mm`. Removing those declarations should reduce source LOC without changing definitions, call sites, dispatch order, MSL source, exported kernel names, argument binding, RNG, PPO math, or eval behavior.
- Added dedicated milestone 06 runner scripts and overlay allowlist entries before validation.
- Acceptance gate remains the full milestone suite against `milestone-00-baseline`, plus subagent review before commit.
- Candidate LOC before validation:
  - `src/metal/kernels.mm`: `1586` to `1577`, `-9`.
  - Kernel scope total: `4611` to `4602`, `-9`.
  - Backend scope total after overlay allowlist growth: `10174` to `10169`, net `-5`.

## 2026-05-27 09:31 EEST

- Milestone 06 dead-prototype candidate failed the acceptance gate and source was restored.
- Validation run:
  - Repeated main train and eval ran for both `breakout` and `g2048`.
  - GPU-inference smoke ran for both envs before the median gate.
  - Native Metal parity wrote `milestone-06-dead-prototypes/native-metal.json`.
  - Overlay surface test passed.
  - Interactive smoke did not run because the median gate stopped the suite first.
- Rejection reason: `breakout` missed the LOC throughput gate.
  - Baseline `breakout` median: `3,434,839` SPS.
  - Required 1 percent floor: `3,400,490.61` SPS.
  - Candidate `breakout` median: `3,369,743.5` SPS, `-1.8951543289219663%`.
  - Candidate train score remained comparable at `2.330354332923889`, eval score remained `0.0`.
  - `g2048` passed: candidate median `602,639` SPS versus baseline `606,783`, `-0.6829459625599221%`, train score `97.85507202148438`, eval median `49.56490135192871`.
- Rejection artifacts: `artifacts/metal-kernel-loc-20260527-v2/milestone-06-dead-prototypes`.
- Current source LOC after restore:
  - `src/metal/shader_src.h`: `2809` lines.
  - `src/metal/kernels.mm`: `1586` lines.
  - `src/metal/platform.mm`: `1213` full-file lines, tensor-ops shader block `216`.
  - Kernel scope total: `4611` lines, net `0` versus baseline.

## 2026-05-27 09:46 EEST

- Starting milestone 07 host-param-format candidate.
- Root-cause hypothesis: `src/metal/kernels.mm` has five one-field anonymous host parameter structs written across three lines each. Collapsing each declaration to one line removes formatting-only host LOC while preserving the same C++ types, initializers, `mtl_set_params` calls, dispatch order, embedded MSL source, RNG, PPO math, and eval behavior.
- This deliberately avoids the previously rejected dispatch helper and dead shader cleanup paths.
- Added dedicated milestone 07 runner scripts and overlay allowlist entries before validation.
- Acceptance gate remains the full milestone suite against `milestone-00-baseline`, plus subagent review before commit.
- Candidate LOC before validation:
  - `src/metal/kernels.mm`: `1586` to `1576`, `-10`.
  - Kernel scope total: `4611` to `4601`, `-10`.
  - Backend scope total after overlay allowlist growth: `10178` to `10172`, net `-2` versus baseline.

## 2026-05-27 09:44 EEST

- Milestone 07 host-param-format candidate passed the full suite.
- Code change: collapsed five one-field anonymous host parameter structs in `src/metal/kernels.mm` from three lines each to one line each. No dispatch helper, embedded MSL source, binding order, parameter values, or control flow changed.
- Accepted LOC delta before commit:
  - `src/metal/kernels.mm`: `1586` to `1576`, `-10`.
  - Kernel scope total: `4611` to `4601`, `-10`.
  - Backend scope total: `10178` to `10172`, net `-2` versus baseline after milestone 06 and 07 allowlist growth.
- Main CPU-overlap medians:
  - `breakout`: `3,446,301.5` SPS versus baseline `3,434,839`, `+0.3337128756253138%`; train score `2.3739752769470215`, eval score `0.0`.
  - `g2048`: `606,157` SPS versus baseline `606,783`, `-0.10316703005852634%`; train score `97.85507202148438`, eval score `49.43283462524414`.
- Main run artifacts:
  - `breakout`: `20260527T094212+0300-breakout`, `20260527T094234+0300-breakout`.
  - `g2048`: `20260527T094224+0300-g2048`, `20260527T094244+0300-g2048`.
- GPU-inference smoke passed for both envs:
  - `breakout`: `20260527T094254+0300-breakout`, `3,524,459` SPS, train score `1.002888560295105`, eval score `0.0`.
  - `g2048`: `20260527T094304+0300-g2048`, `876,325` SPS, train score `97.85507202148438`, eval score `42.0`.
- Native Metal parity wrote `milestone-07-host-param-format/native-metal.json`.
- Overlay surface test passed.
- Interactive smoke artifact: `milestone-07-host-param-format/20260527T094314+0300-interactive-breakout-host-param-format`. It built, invoked eval with `resources/breakout/breakout_weights.bin`, reached raylib 5.5 GLFW initialization, then failed with `Failed to determine Monitor to center Window` and exit code `139`, matching the known desktop windowing boundary.
- Decision: accepted pending subagent review. Because the previous accepted cleanup failed clean final audit, this candidate still needs a post-commit clean audit before it can be treated as final.

## 2026-05-27 09:48 EEST

- Committed milestone 07 as `5052b3732` after subagent review found no blockers.
- Post-commit clean audit reused the milestone 07 suite so the same dedicated runner folder contains both the dirty candidate cohort and clean committed cohort. The latest clean cohort passed.
- Clean audit medians:
  - `breakout`: `3,400,761.5` SPS versus baseline `3,434,839`, `-0.9921134585929692%`; train score `2.2924174070358276`, eval score `0.0`.
  - `g2048`: `602,675.5` SPS versus baseline `606,783`, `-0.6769306325325575%`; train score `97.85507202148438`, eval score `49.43283462524414`.
- Clean GPU-inference smoke passed for both envs:
  - `breakout`: `20260527T094711+0300-breakout`, `3,542,520` SPS, train score `0.9884593486785889`, eval score `0.0`.
  - `g2048`: `20260527T094721+0300-g2048`, `876,565` SPS, train score `97.85507202148438`, eval score `42.0`.
- Clean native Metal parity wrote `milestone-07-host-param-format/native-metal.json`.
- Clean overlay surface test passed.
- Clean interactive smoke artifact: `milestone-07-host-param-format/20260527T094731+0300-interactive-breakout-host-param-format`, with the same raylib monitor boundary and exit code `139`.
- Current accepted source LOC:
  - `src/metal/shader_src.h`: `2809` lines.
  - `src/metal/kernels.mm`: `1576` lines.
  - `src/metal/platform.mm`: `1213` full-file lines, tensor-ops shader block `216`.
  - Kernel scope total: `4601` lines, net `-10` versus baseline.
  - Backend scope total: `10172` lines, net `-2` versus baseline.

## 2026-05-27 09:58 EEST

- Starting milestone 08 tensor-desc-format candidate.
- Root-cause hypothesis: the tensor-ops shader block in `src/metal/platform.mm` repeats six `matmul2d_descriptor` calls over five lines each. Folding each call into one line removes whitespace-only embedded MSL LOC while preserving descriptor values, transpose flags, execution scope, kernel names, dispatch order, host bindings, RNG, PPO math, and eval behavior.
- This touches embedded MSL text, so it gets the full suite and a clean post-commit audit if accepted.
- Added dedicated milestone 08 runner scripts and overlay allowlist entries before validation.
- Acceptance gate remains the full milestone suite against `milestone-00-baseline`, plus subagent review before commit.
- Candidate LOC before validation:
  - `src/metal/platform.mm`: `1213` to `1189`, `-24` full-file lines.
  - Tensor-ops shader block: `216` to `192`, `-24`.
  - Kernel scope total: `4601` to `4577`, net `-34` versus baseline.
  - Backend scope total after overlay allowlist growth: `10172` to `10152`, net `-22` versus baseline.

## 2026-05-27 10:00 EEST

- Milestone 08 tensor-desc-format candidate passed the full suite.
- Code change: folded six `matmul2d_descriptor` calls in the tensor-ops shader block from five lines each to one line each. Descriptor values, transpose flags, execution scopes, kernel names, host bindings, and dispatch order stayed unchanged.
- Accepted LOC delta before commit:
  - `src/metal/platform.mm`: `1213` to `1189`, `-24` full-file lines.
  - Tensor-ops shader block: `216` to `192`, `-24`.
  - Kernel scope total: `4601` to `4577`, net `-34` versus baseline.
  - Backend scope total after overlay allowlist growth: `10172` to `10152`, net `-22` versus baseline.
- Main CPU-overlap medians:
  - `breakout`: `3,438,433.5` SPS versus baseline `3,434,839`, `+0.10464828191365516%`; train score `2.4659218788146973`, eval score `0.0`.
  - `g2048`: `604,770.5` SPS versus baseline `606,783`, `-0.3316671693175288%`; train score `97.85507202148438`, eval score `49.43283462524414`.
- Main run artifacts:
  - `breakout`: `20260527T095836+0300-breakout`, `20260527T095857+0300-breakout`.
  - `g2048`: `20260527T095848+0300-g2048`, `20260527T095908+0300-g2048`.
- GPU-inference smoke passed for both envs:
  - `breakout`: `20260527T095917+0300-breakout`, `3,560,856` SPS, train score `0.9913344979286194`, eval score `0.0`.
  - `g2048`: `20260527T095927+0300-g2048`, `820,988` SPS, train score `97.85507202148438`, eval score `42.0`.
- Native Metal parity wrote `milestone-08-tensor-desc-format/native-metal.json`.
- Overlay surface test passed.
- Interactive smoke artifact: `milestone-08-tensor-desc-format/20260527T095937+0300-interactive-breakout-tensor-desc-format`. It built, invoked eval with `resources/breakout/breakout_weights.bin`, reached raylib 5.5 GLFW initialization, then failed with `Failed to determine Monitor to center Window` and exit code `139`, matching the known desktop windowing boundary.
- Decision: accepted pending subagent review. Because the change touches embedded MSL source text, it still needs a post-commit clean audit before it can be treated as final.

## 2026-05-27 10:04 EEST

- Milestone 08 failed the post-commit clean audit after commit `5e6949654`, so the tensor descriptor formatting change was rejected and source was restored.
- Clean audit failure:
  - `breakout`: `3,068,869` SPS versus required `3,400,490.61`, `-10.654647859768684%`; train score `2.3927189111709595`, eval score `0.0`.
  - `g2048`: `556,697` SPS versus required `600,715.17`, `-8.2543512260561%`; train score `100.07987213134766`, eval score `51.96200942993164`.
- Clean audit still ran GPU-inference smoke, native Metal parity, and overlay surface before the median gate failed.
- Interactive smoke did not run in the clean audit because the median gate stopped the suite first.
- Current accepted source LOC after restore:
  - `src/metal/shader_src.h`: `2809` lines.
  - `src/metal/kernels.mm`: `1576` lines.
  - `src/metal/platform.mm`: `1213` full-file lines, tensor-ops shader block `216`.
  - Kernel scope total: `4601` lines, net `-10` versus baseline.
  - Backend scope total after keeping milestone 08 audit allowlist: `10176` lines, net `+2` versus baseline.
