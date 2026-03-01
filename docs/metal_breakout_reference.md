# breakout reference metrics

## upstream static-native reference (joseph screenshot)
- env: `puffer_breakout`
- params: `32.4K`
- steps: `102.8M`
- SPS: `23.1M`
- user stat `score`: `843.089`
- user stat `episode_length`: `16982.564`
- losses (shown): `pg_loss=0.005`, `vf_loss=0.049`, `entropy=0.151`

## this metal branch (m4 pro) before root-cause fix
- symptom: loss logs frequently `nan` or unstable, policy stayed near uniform entropy.
- typical score/episode_return stayed around `0.8` with little learning.

## this metal branch after fp32-train-gradient fix
command:
```bash
python bench.py --env breakout \
  --total-agents 4096 \
  --hidden-size 64 \
  --num-layers 2 \
  --horizon 64 \
  --total-timesteps 100000000 \
  --learning-rate 0.1 \
  --beta1 0.7279714073125252 \
  --beta2 0.9986265112492152 \
  --eps 0.00008339460257113628 \
  --minibatch-size 65536 \
  --replay-ratio 1.4242098997083206 \
  --ent-coef 0.0033240721522812535 \
  --gamma 0.9721246598992744 \
  --gae-lambda 0.948721675814334 \
  --vtrace-rho-clip 2.1017317041552603 \
  --vtrace-c-clip 1.0830442742115065 \
  --prio-alpha 0.1 \
  --prio-beta0 0.8247156461060179 \
  --clip-coef 0.6746497927896418 \
  --vf-coef 1.2195502588297364 \
  --vf-clip-coef 1.2291681640124468 \
  --max-grad-norm 1.8109182724544075 \
  --num-buffers 8 \
  --num-threads 8 \
  --optimizer muon \
  --log-interval 5
```

observed:
- run completed `99,876,864` steps
- avg SPS: `822,472`
- policy entropy moved off uniform (`~1.098 -> ~0.03-0.2`)
- score improved from `<1` to a stable `~6.6-6.8`
- no NaN loss reporting in this run

note:
- this fix prioritizes numeric stability (finite gradients) over raw throughput.
- additional tuning is still needed to approach upstream task performance.

## march 2026 update (current investigation)
- upstream `static-native` moved from `6a4646ff` to `0a479a58` (`git fetch upstream static-native`).
- metal no longer resets recurrent rollout state at rollout boundaries (cuda parity).

observed bottlenecks:
- `num_buffers=8` reduces inference batch size from `4096` to `512` and increases rollout callback count from `64` to `512` per iteration.
- per-callback sync is still required before env step consumes actions, but rollout now runs on per-buffer metal streams with per-buffer sampling scratch (global rollout gpu mutex removed).
- this raised throughput in the `num_buffers=8` tuned run from roughly `~0.82m` to `~1.00m` SPS on m4 pro.

important parity caveat:
- cuda can recompute priority sampling every minibatch fully on gpu.
- current metal prio path still builds cdf on cpu (`prio_precompute` sync), so forcing full per-minibatch cuda parity on metal causes heavy stalls and learning regression.
- root-cause follow-up should be a full gpu prio sampler on metal (to remove cpu cdf sync), then re-test per-minibatch parity.

latest local checkpoints:
- `num_buffers=1`, `num_threads=1`, 10M-step sanity run:
  - avg SPS: `~1.14m`
  - score: `~0.82` at 9.17M steps
- `num_buffers=8`, `num_threads=8`, tuned 10M-step run:
  - avg SPS: `~1.00m`
  - score: `~5.43` at 9.17M steps
