# Fortis Colosseum — state of the art

Single source of truth for the current best Colosseum training config and the
score it reaches. Updated on every new SOTA so we never have to re-derive "what
is our best config" from 900 W&B runs again.

## Current SOTA (2026-06-29)

**easy-donkey-852** — honest fair-fight **wave 8.26 / depth ~11**, 0 full wins.

- W&B: project `osrs-colosseum`, group `colosseum-fixsim-repro-20260627`, run id `d9i3bppz`
- Metric keys: `env/wave` (mean wave reached), `env/max_depth_reached`, `env/score`
- Architecture: native NPC **entity encoder ON**, `hidden_size 2048`, `num_layers 3`, MinGRU, bf16
- Horizon: `total_timesteps ~249M`; `gamma 0.99999966`, `gae_lambda 0.01`, `lr 1.40e-3`,
  `ent_coef 1e-7`, `replay_ratio 3.29`, `minibatch 4096`
- Sim: honest 2565-float obs, all cheat/oracle flags OFF (`invuln_mode=0`,
  `bis_gear_oracle_mode=0`, `prayer_oracle_mode=0`)
- **This config IS the box default.** Reproduce with a bare `pufferl train osrs_colosseum`
  (no overrides) on a checkout at or after the 2026-06-29 bake commit.

## History (honest runs unless flagged)

| date | run | wave | depth | score | arch | notes |
|---|---|---|---|---|---|---|
| 2026-06-27 | easy-donkey-852 | **8.26** | ~11 | — | hs2048/L3 + entity-enc | current SOTA (best seed of the entity-encoder config) |
| 2026-06-26 | devoted-field-824 | 7.91 | 10.76 | 0.5075 | hs2048/L3 | on the OLD dishonest 2492-obs, not comparable |
| 2026-06-27 | hardy-pond-851 | 7.58 | 10.66 | 0.4986 | hs2048/L3 + entity-enc | same config as easy-donkey-852, weaker seed |
| 2026-06-27 | feasible-pond-847 | 7.38 | 10.48 | 0.446 | hs2048/L3 | devoted-field config on honest obs (entity-enc OFF) |
| 2026-06-25 | golden-eon-796 | 7.23 | 10.39 | ~0.43 | hs1024/L2 | prior baked default, superseded |
| 2026-06-24 | lilac-smoke-534 | 9.18 | — | — | hs512/L2 | CHEAT (`invuln_mode=1`), not a fair fight — excluded |

## The wall

Plateau holds at ~wave 7.5-8.3 with 0 full wins. Diagnosed (Flywheel root
"OSRS Colosseum: solving the wave-7 wall") as **positioning/survival**, not
prayer/gear/PPO config: ~94% of damage taken is positional, much of it
multi-style same-tick conflict. Levers that did NOT help: prayer oracle (neutral),
BIS gear oracle (hurt), SimpleCL prioritized-replay curriculum (collapsed the env).
The dominant tuning lever was freeing the compute horizon (`total_timesteps`);
width helps monotonically up to hs2048 under a 1hr cap; `num_layers 3`.

## Practice — do not lose track of SOTA again

On every new state of the art:

1. **Bake it as the box default** — set `config/ocean/osrs_colosseum.ini`
   `[env]/[policy]/[train]` to the winning run's resolved config so a bare
   `pufferl train osrs_colosseum` reproduces it. Refresh the ini header.
2. **Update this file** — add the run to the history table, move the prior best down.
3. **Verdict metric**: weight `env/score` over `env/wave` (score is the lower-variance
   signal). A retrain within ~0.3 wave of the prior best is noise, not a win.
4. Optional: merge the config bake to the `5.0` trunk (the repo source of truth) so it
   survives branch churn. Note the entity-encoder config depends on the entity-encoder
   code (currently on `valtteri/colo-entity-encoder`), so a 5.0 merge needs that code first.
