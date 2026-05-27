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
