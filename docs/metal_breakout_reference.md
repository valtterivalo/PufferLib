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
