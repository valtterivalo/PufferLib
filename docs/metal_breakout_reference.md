# metal breakout reference

## upstream static-native reference (joseph screenshot)

captured from the user-provided screenshot in this thread.

- env: `puffer_breakout`
- params: `32.4K`
- steps: `102.8M`
- sps: `23.1M`
- epoch: `359`
- losses:
  - `pg_loss=0.005`
  - `vf_loss=0.049`
  - `entropy=0.151`
  - `total_loss=0.064`
  - `old_approx_kl=0.000`
  - `approx_kl=0.000`
  - `clipfrac=0.000`
- user stats:
  - `score=843.089`
  - `episode_length=16982.564`
  - `n=2058`
  - `agent_steps=94109696`

## local metal baseline (m4 pro)

latest validated run after ppo-path fixes:

- command family: `bench.py --env breakout --total-agents 4096 --hidden-size 64 --num-layers 2 --horizon 64 --minibatch-size 65536 --replay-ratio 1.0 --optimizer muon`
- 20m-step check:
  - `avg sps ~= 2.55M`
  - losses stayed finite (no `pg=inf`, no `vf=nan`, no entropy collapse)
  - return/score stayed near `~0.8` and did not show strong learning yet

## current gap

- stability improved for the main `64x2` config.
- throughput is still far below upstream cuda (expected on m4 pro vs high-end nvidia, but there is still optimization headroom).
- learning quality is still below target and needs deeper root-cause work (not only numeric guards).

## next bug-hunt focus

- find first-op divergence via fail-fast non-finite traps in rollout/train critical tensors.
- verify ppo ratio/logprob/value-target consistency against cuda semantics.
- isolate whether poor learning is from optimizer dynamics, advantage/prio scaling, or value-target path.
