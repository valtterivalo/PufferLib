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
- Current tracked backend scope: `10,158` lines across `src/metal`, `tools/metal`, and `tests/metal`.
- Shader declarations: `56` `kernel void` declarations in `src/metal/shader_src.h`, `6` Metal tensor-op shader declarations in `src/metal/platform.mm`.
- Setup validation: `git diff --check` passed, Python compile passed for the new summarizer and overlay test, and `PYTHONPATH=$PWD python -m pytest tests/metal/test_overlay_surface.py` passed.
- Overlay guard fix: `changed_paths()` now checks committed, staged, unstaged, and untracked paths. The old unstaged check compared the worktree against `upstream/5.0`, which misclassified upstream `boxoban` drift as local dirty paths, and a first setup patch missed staged blocked-path edits.

## 2026-05-27 Milestone 00 Baseline

- Milestone 00 runner passed: `artifacts/metal-kernel-loc-20260527/milestone-00-baseline/run-suite.sh`.
- `breakout` artifact `milestone-00-baseline/20260527T011534+0300-breakout`: `3,013,517` SPS, train score `2.385693311691284`, eval score `0.0`, eval episodes `527`, uptime `1.417306900024414`, rollout `0.21421721577644348`, train `0.41755735874176025`, train sync `0.4138440191745758`, train Muon `0.0026830416172742844`.
- `g2048` artifact `milestone-00-baseline/20260527T011549+0300-g2048`: `549,537` SPS, train score `96.67605590820312`, eval score `55.5`, eval episodes `64`, uptime `0.4770832061767578`, rollout `0.19284191727638245`, train `0.2045634686946869`, train sync `0.20018157362937927`, train Muon `0.0024486251641064882`.
- Native Metal parity wrote `milestone-00-baseline/native-metal.json`, and `tests/metal/test_overlay_surface.py` passed.
- Decision: keep setup pending subagent review. No backend behavior change yet.
