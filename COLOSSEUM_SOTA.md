# Fortis Colosseum — state of the art

Single source of truth for the current best Colosseum training config and the
score it reaches. Updated on every new SOTA so we never have to re-derive "what
is our best config" from 900 W&B runs again.

## Current SOTA (2026-07-10)

**dainty-hill-985** (max score) / **clear-brook-983** (pareto knee, the baked default) —
honest fair-fight **score 0.526 final / 0.664 peak, wave ~8.0, depth 10.7**, 0 full wins.

- W&B: project `osrs-colosseum`, group `colosseum-solve-20260708`, run ids `zar6uvk1`
  (dainty-hill-985) and `vmg95xgy` (clear-brook-983)
- The knee matches the top scorer inside noise (0.521 vs 0.526 final) at 5.4x the
  throughput (67.5K vs 12.4K SPS, hs2048 vs hs4096) — 0.90h vs 4.35h per run
- Recipe that broke the 0.30-0.40 plateau: `clip_coef ~0.07` (off the old 0.027),
  classic curriculum fracs ON (waves 4/8/12), `wave_clear_bonus ~1-1.5`, low-but-nonzero
  `death_penalty`, `lr ~3.7-4.4e-4`, `vf_coef 1.4-1.9`, `ent_coef 1e-7`
- Sim: 2x-optimized env with honest forecast (corner-cut fix), threat field, farm cap;
  all cheat/oracle flags OFF. CAVEAT: trials ran with BARE late starts
  (pre-`late_start_state_mode`), so the curriculum-frac pricing needs an honest retrial.
- **clear-brook-983 IS the box default.** Reproduce with a bare `pufferl train
  osrs_colosseum` on a checkout at or after the 2026-07-10 bake commit; dainty-hill-985
  deltas are recorded in the ini header.

## History (honest runs unless flagged)

| date | run | wave | depth | score | arch | notes |
|---|---|---|---|---|---|---|
| 2026-07-10 | dainty-hill-985 | 8.00 | 10.75 | **0.526** (peak 0.664) | hs4096/L2 + entity-enc | solve-sweep top scorer, 12.4K SPS |
| 2026-07-10 | clear-brook-983 | 8.01 | 10.53 | 0.521 (peak 0.577) | hs2048/L2 + entity-enc | solve-sweep pareto knee, 67.5K SPS, **baked default** |
| 2026-06-27 | easy-donkey-852 | 8.26 | ~11 | — | hs2048/L3 + entity-enc | prior SOTA on the pre-DPT-delete obs (2565); as bigsweep anchor on current obs drew 7.50/6.63 |
| 2026-06-26 | devoted-field-824 | 7.91 | 10.76 | 0.5075 | hs2048/L3 | on the OLD dishonest 2492-obs, not comparable |
| 2026-06-27 | hardy-pond-851 | 7.58 | 10.66 | 0.4986 | hs2048/L3 + entity-enc | same config as easy-donkey-852, weaker seed |
| 2026-06-27 | feasible-pond-847 | 7.38 | 10.48 | 0.446 | hs2048/L3 | devoted-field config on honest obs (entity-enc OFF) |
| 2026-06-25 | golden-eon-796 | 7.23 | 10.39 | ~0.43 | hs1024/L2 | prior baked default, superseded |
| 2026-06-24 | lilac-smoke-534 | 9.18 | — | — | hs512/L2 | CHEAT (`invuln_mode=1`), not a fair fight — excluded |

## The wall

The 2026-07-08 solve sweep broke the 0.30-0.40 score plateau (0.526/0.664 peak) but
mean wave holds at ~8 and **wins remain 0 across every configuration ever tried** —
31 solve-sweep trials on top of the bigsweep/clsweep hundreds. Per the
sweep-as-diagnostic principle, a frontier no config pushes past points at the
task/sim, not PPO. Next probe (2026-07-10): train on **wave 12 / Sol Heredit alone**
(`--env.start-wave 12`, honest late-start entry states) — if Sol is unbeatable even
when it is the whole task, the wall is in the sim, obs, or action space, not in the
journey to reach him. Levers that did NOT help across sweeps: prayer oracle (neutral;
07-06 reading confounded by the manticore mispray bug, fixed 07-10), BIS gear oracle
(hurt), best-trajectory CL (pinned ~0 twice, second time on the honest env).

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
