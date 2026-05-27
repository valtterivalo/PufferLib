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
- Decision: rejected and reverting source changes.
- Rejection reason: `g2048` missed the LOC gate after two main runs.
  - Baseline `g2048` median: `606,783` SPS.
  - Required 1 percent floor: `600,715.17` SPS.
  - Candidate `g2048` median: `588,629` SPS, `-2.9918438716971307%`.
  - Candidate train score remained `97.85507202148438`, eval score remained `49.43283462524414`, so this was a throughput gate failure, not a learnability failure.
  - `breakout` passed the gate but regressed to `3,421,469` SPS versus baseline `3,434,839`, `-0.38924677401181995%`.
- Rejection artifacts: `artifacts/metal-kernel-loc-20260527-v2/milestone-02-kernel-consolidation`.
- Updated root-cause hypothesis: edits inside `shader_src.h` can change Metal compilation or code layout enough to move g2048 throughput even when removed kernels are not on the measured path. Future LOC candidates should isolate host-side cleanup first, or touch shader source only in tiny audited slices.
