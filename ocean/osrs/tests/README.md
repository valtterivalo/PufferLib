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
| `test_osrs_npc_movement.c` | shared SDK-shaped NPC movement primitive: 1x1 diagonal edge checks, aggro-target overlap rewrites, melee contact, and under-player hold |
| `test_osrs_pvp_pending_hits.c` | PvP pending-hit semantics on the shared queue |
| `test_osrs_special_attacks.c` | shared spec resolver (costs, SGS minimums, claws cascade bounds, def drains), item-effect laws (identity, tbow monotonicity, crystal scaling, blood fury proc rate, scythe splat rule), consumable formula home + Player-application laws (restore convergence, boost caps) |
| `test_osrs_item_effect_masks.c` | data guard pinning the exact item -> effect_mask mapping in `osrs_items_generated.h`; the column is hand-maintained (no generator on this branch) so this catches a dropped or wrong mask |
| `test_osrs_inventory_clicks.c` | shared inventory-click SDK: item-index classification and click-action resolution |
| `test_osrs_venator_bow_bounce.c` | pure Venator bow bounce geometry, chain selection, and damage laws |
| `test_inferno_attack_styles.c` | inferno NPC attack-style fidelity battery |
| `test_inferno_lab.c` | inferno lab command grammar + snapshot/restore (also the cross-encounter shared-plumbing regression guard) |
| `test_inferno_golden.c` | inferno characterization digests: refactor => bit-identical trajectory. BASELINE is branch-local (re-seeded 2026-06-10 on valtteri/osrs-colosseum); re-seed with `--print` after any INTENDED behavior change |
| `test_inferno_replay_best.c` | inferno replay regression on a recorded best episode |
| `test_inferno_forecast_exact.c` | inferno step-out forecast binary-exactness golden; `--write-golden DIR` seeds, `--compare DIR` re-checks |
| `test_colosseum_modifiers.c` | colosseum battery: obs/mask fuzz, modifiers/drafts, arena geometry, warband, NPC mechanics, Sol Heredit, researched loadout profiles + consumables + specs |
| `test_colosseum_forecast_exact.c` | binary-exactness gate for the colosseum step-out forecast hot path, plus the LoS/footprint lookup-table selftests |
| `test_colosseum_golden.c` | byte-identity golden-master for the Fortis Colosseum env; re-seed with `--print` after any intended behavior change |
| `test_colosseum_sol_spear_shapes.c` | tile-exact parity of Sol spear hazard shapes vs colosim, all 8 directions on both spear variants |
| `test_colosseum_consumable_sprite_assets.c` | colosseum consumable dose-chain inventory sprite regression (needs `OSRS_ASSET_ROOT`) |
| `bench_inferno_forecast.c` | inferno step-out forecast benchmark (not a test) |
| `bench_colosseum_forecast_profile.c` | colosseum step-out forecast bucket profiler (not a test) |
| `inferno_lab_cli.c` | interactive lab harness (not a test) |

one-off diagnostics live alongside the suite but are not part of it: the `probe_*` and
`trace_*` files, plus the report-only `test_colo_prayer_wiring.c`. they print findings
rather than asserting pass/fail, and are run by hand during investigations.

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
