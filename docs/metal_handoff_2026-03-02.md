# metal breakout handoff (2026-03-02)

## branch snapshot
- branch: `osrs-pvp-metal`
- head: `5b0ae44f`
- ahead of `origin/osrs-pvp-metal`: `20` commits

## what changed in the latest phase
- stabilized long-run behavior and removed frequent hangs/ctrl-c deadlocks.
- aligned several ppo/rng semantics with static-native.
- added trace tooling and sweep harness improvements.
- moved training from non-learning/unstable states to reliable learning.

recent high-impact commits:
- `cd3e5922` multi-stream sync stall + ctrl-c hang fixes.
- `3baf2983` allocator wrap/arg-table cache tracking fixes.
- `1c142cd6` forward dependency ordering fix for long runs.
- `a443d5dd` sweep stabilization + parity trace harness.
- `01d38846` ppo/rng semantic alignment.
- `5b0ae44f` deep train debug trace metrics.

## current performance envelope
- 100m tuned runs on current head are in the `~455-517` score range with `~0.85-0.88m` avg sps.
- best sweep result seen in this repo snapshot: `score=672.82` at `126.5m` steps (`~1.12m sps`).

see `docs/metal_breakout_reference.md` for the exact numbers and command shape.

## open gap
- upstream static-native reference reaches much higher return by ~100m steps.
- this is likely not pure hardware throughput: there is still a semantic/parity gap.

## important current conclusion
- overlap itself is not the primary root cause for the learning gap in tested configs.
- a remaining mismatch is likely in training semantics around sampling/minibatch/update behavior and/or harness-path differences (`bench.py` vs `pufferl` train path).

## in-progress experimental changes that were reverted before this handoff
- uncommitted prioritized replay cdf/binary-search sampling patch in:
  - `src/metal_shader_src.h`
  - `src/metal_kernels.mm`
- this patch was not validated enough to hand off as stable, so it was reverted.

## recommended immediate next pass
1. compare resolved runtime hyper/config values between `bench.py` and `python -m pufferlib.pufferl train ...` for strict parity.
2. run targeted a/b tests for prioritized sampling semantics with short/medium runs before reintroducing kernel-level changes.
3. once learning parity is closer, do throughput-focused work (queue chunking/cooperative scheduling and per-path profiling).
