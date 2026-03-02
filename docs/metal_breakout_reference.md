# breakout reference metrics

## upstream static-native reference
- source: joseph suarez screenshot shared in-thread
- env: `puffer_breakout`
- params: `32.4k`
- steps: `102.8m`
- sps: `23.1m`
- score: `843.089`
- episode length: `16982.564`
- losses (shown): `pg_loss=0.005`, `vf_loss=0.049`, `entropy=0.151`

## metal branch status (m4 pro)

### latest stable 100m runs on current head
- config family: tuned `muon`, `total_agents=4096`, `hidden=64`, `layers=2`, `horizon=64`, `num_buffers=8`, `num_threads=8`
- seed 40: final `score=479.03`, `avg sps=879,518` (`/tmp/metal_seed_40.log`)
- seed 41: final `score=516.69`, `avg sps=861,528` (`/tmp/metal_seed_41.log`)
- seed 42: final `score=455.57`, `avg sps=848,112` (`/tmp/metal_seed_42.log`)
- no nan loss reporting in these runs

### best known sweep points (historical, still valid)
- from `/tmp/metal_breakout_sweep_results_latest.txt`
- best score found: `672.82` at `126.5m` steps, `1.12m sps` (trial `#29`)
- fastest pareto point: `2.35m sps` at `score=304.19` (trial `#20`)

## interpretation
- metal is now learning reliably (far beyond the previous `~0.8` plateau).
- sample-efficiency gap still exists vs upstream reference at similar step counts.
- this gap is larger than expected from hardware differences alone, so backend/parity work is still needed.

## current priorities
1. close learning-efficiency gap first (parity/debugging).
2. then optimize throughput aggressively after semantics are trusted.

## known caveats to keep in mind
- `bench.py` and `sweep_bench.py` are fork-specific harnesses; direct comparability to upstream harness is imperfect.
- `python -m pufferlib.pufferl train puffer_breakout ...` currently behaves differently and can collapse; treat that path as a separate parity target.
