# OSRS test suite

standalone C test binaries for the shared OSRS layers and the encounter envs.
no test framework — each file is a self-contained binary with its own `main()`.
exit code 0 = all passed.

## building and running

all commands run from the repo root (`pufferlib-metal/`):

```bash
cc -std=c11 -O0 -g -I. -o /tmp/t ocean/osrs/tests/<file>.c -lm && /tmp/t
```

the inferno golden test wants `-O2` (2000-tick trajectories x 15 configs).

## test files

| file | what it covers |
|---|---|
| `test_osrs_combat_rolls.c` | shared hit-chance fractions and roll-ratio RNG consumption (assert-based, silent on pass) |
| `test_osrs_pending_hit_queue.c` | shared pending-hit queue contract |
| `test_osrs_pvp_pending_hits.c` | PvP pending-hit semantics on the shared queue |
| `test_osrs_special_attacks.c` | shared spec resolver (costs, SGS minimums, claws cascade bounds, def drains), item-effect laws (identity, tbow monotonicity, crystal scaling, blood fury proc rate, scythe splat rule), consumable formula home + Player-application laws (restore convergence, boost caps) |
| `test_osrs_item_effect_masks.c` | data guard pinning the exact item -> effect_mask mapping in `osrs_items_generated.h`; the column is hand-maintained (no generator on this branch) so this catches a dropped or wrong mask |
| `test_inferno_attack_styles.c` | inferno NPC attack-style fidelity battery |
| `test_inferno_lab.c` | inferno lab command grammar + snapshot/restore (also the cross-encounter shared-plumbing regression guard) |
| `test_inferno_golden.c` | inferno characterization digests: refactor => bit-identical trajectory. BASELINE is branch-local (re-seeded 2026-06-10 on valtteri/osrs-colosseum); re-seed with `--print` after any INTENDED behavior change |
| `test_inferno_replay_best.c` | inferno replay regression on a recorded best episode |
| `test_colosseum_modifiers.c` | colosseum battery: obs/mask fuzz, modifiers/drafts, arena geometry, warband, NPC mechanics, Sol Heredit, researched loadout profiles + consumables + specs |
| `bench_inferno_forecast.c` | inferno step-out forecast benchmark (not a test) |
| `inferno_lab_cli.c` | interactive lab harness (not a test) |

## layering rule

shared-layer behavior (osrs_combat.h, osrs_consumables.h, osrs_special_attacks.h,
osrs_item_effects.h, osrs_encounter.h helpers) is tested ONCE in the `test_osrs_*`
files, property-style where possible. encounter test files only cover behavior the
encounter itself owns. when an encounter test wants to assert a shared formula,
the assertion belongs in the shared file instead.

## when to run

- after any change to the shared headers above: the full battery
- after any change to items (`osrs_items.h`, `osrs_items_generated.h`) or monsters
- after any encounter combat change: that encounter's tests + `test_inferno_golden`
  (drift guard) + `test_inferno_lab` (shared plumbing)
- before committing changes to any of the above

## reference data

tests are cross-referenced against:

- `.refs/` reference repos where checked out (osrs-dps-calc, osrs-sdk, InfernoTrainer)
- OSRS wiki formula descriptions (cited in the code under test)
