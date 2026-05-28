# Inferno data-model simplification — session handoff

Ephemeral working doc for the next agent. Delete it before this branch merges
anywhere. Written by the prior session; treat every claim below as a lead to
verify, not gospel (see "Trust nothing blindly").

## Where we are

- Branch: `simplify/inferno-data-model`, cut off `5.0`.
- Goal: a principles-based simplification pass over the Inferno data model
  (`programming-core-principles`), agreed scope "full data model" (hotspots
  #1-#4 below).
- Done and committed:
  - `07999e287` golden-master trajectory test (the regression net).
  - `6c27d9425` hotspot #1: per-tick scratch grouped into `InfTickScratch`.
- Next: hotspot #2 (InfNPC tagged union) via the safe recipe below. #3 and #4
  were judged marginal — see verdicts.

## Trust nothing blindly

Re-derive these before acting on them. The prior session was wrong or imprecise
at least twice and only caught it by checking:

1. "Enum-typing is a clean compiler-checked win" — FALSE for this build. Verify
   the actual warning flags in `build.sh` yourself (`-Wall` + targeted
   `-Werror=`, no `-Wswitch-enum`, no global `-Werror`). Confirm whether that's
   still true on your branch.
2. The InfNPC type-specific fields are NOT cleanly partitioned by type at the
   init boundary. Re-verify by reading `inf_spawn_npc` in
   `encounter_inferno_reset_spawn.inc` — it inits type-specific fields for ALL
   types, several to `-1`, not zero. Do not assume any field is blob-only /
   jad-only without grepping its read sites and their type guards.
3. The golden master has a COVERAGE GAP (below). Do not treat "golden master
   green" as "behavior fully preserved" for deep NPC logic — lean on the
   scenario test for that.

## The regression net — run before AND after every change

Four standalone C test binaries. NOTE: the convenience runner lived at
`/tmp/inf_regress.sh` (ephemeral, may be gone). Recreate or run by hand:

```bash
# from worktree root
cd /Users/valtterivalo/Projects/pufferlib-metal/.claude/worktrees/unified-painting-thimble
for t in test_inferno_golden test_inferno_attack_styles test_inferno_lab test_inferno_replay_best; do
  cc -std=c11 -O2 -I. -o /tmp/$t ocean/osrs/tests/$t.c -lm && /tmp/$t | tail -1
done
```

Expected green baseline:
- `test_inferno_golden`: 15/15 configs match baseline
- `test_inferno_attack_styles`: 1435/1435 tests passed
- `test_inferno_lab`: 44/44 tests passed
- `test_inferno_replay_best`: 2 passed, 0 failed

### Build prerequisite — the gitignored data symlink

`encounter_inferno.h` includes `../data/npc_models.h`, and `ocean/osrs/data/`
is gitignored (generated assets), so a fresh worktree lacks it. The main
checkout has it. This symlink was created last session and must exist:

```bash
ls ocean/osrs/data/npc_models.h 2>/dev/null || \
  ln -s /Users/valtterivalo/Projects/pufferlib-metal/ocean/osrs/data ocean/osrs/data
```

### What each test actually covers

- `test_inferno_golden.c` (NEW): runs deterministic episodes across 15 (start_wave,
  seed) configs spanning every NPC family, and FNV-1a-hashes the observation
  vector + reward + outcome each tick, via the public env interface only
  (`inf_write_obs`, `state->reward`). Layout-independent: a behavior-preserving
  refactor must reproduce the digests bit-for-bit, and any obs drift flips one.
  Regenerate baselines with `/tmp/test_inferno_golden --print` ONLY for an
  intentional behavior change, and say why.
  - COVERAGE GAP: random actions kill the player in 22-102 ticks at high waves,
    so blob splits, mager resurrection, jad/zuk healer spawns, zuk shield/spark/
    set phases never fire under it. It guards spawn + movement + obs + early
    combat. Deep type-specific behavior is guarded by the scenario test instead.
- `test_inferno_attack_styles.c`: 1435 deterministic scenario assertions that
  FORCE the deep behaviors (healer tags, jad/zuk healer phases, shield tags,
  blob, reward shaping) via the lab control surface (SPAWN_NPC / SET_NPC_HP /
  KILL_NPC / STEP_TICKS). This is the net for the union refactor.
- `test_inferno_lab.c`, `test_inferno_replay_best.c`: lab API + replay ordering.

Also typecheck the real training binding after struct changes (the test
binaries are not the only consumer):

```bash
cc -std=c11 -fsyntax-only -I. \
  -I/Users/valtterivalo/Projects/pufferlib-metal/raylib-5.5_macos/include \
  ocean/osrs_inferno/binding.c 2>&1 | grep -iE "this_tick|tick_scratch|union|error:"
# stops at missing vecenv.h (a PufferLib build-time header) — that's expected
# and environmental; you only care that there are no struct/field errors before it.
```

## Hotspot inventory (the principles pass)

The file layout is already healthy (12 `.inc` files, none over ~1400 lines,
aggregated by a 54-line `encounter_inferno.h`). The mess is in the data model.

- #1 DONE — `InfernoState` mixed three lifetimes in one flat struct. 34 per-tick
  reward/diagnostic accumulators were cleared by 34 hand-written assignments
  across two files → a new counter could silently miss the reset (P7). Grouped
  into `InfTickScratch tick_scratch;` reset by one assignment. `InfTickScratch`
  is defined just above `InfernoState` in `encounter_inferno_model.inc`.
  - Watch-outs that bit last session: `InfernoState` already has `int tick;`
    (the counter), so the member is `tick_scratch`, not `tick`. `pillar_lost`
    resets to `-1` not 0 (designated init in the reset). `player_moved_last_tick`
    is a persistent carry-over read from `tick_scratch.player_moved` before the
    reset zeroes it. The InfNPC per-tick flags (`attacked_this_tick`,
    `moved_this_tick`, etc.) and the Player scratch (`ate_food_this_tick`, ...)
    are SEPARATE and were deliberately left alone.
- #2 NEXT — `InfNPC` is a ~62-field int-soup carrying every type's state on every
  NPC (a nibbler holds `jad_attack_style`, `blob_scan_timer`, `heal_target`...).
  Tagged-union candidate, keyed on the existing `type`. Recipe below.
- #3 enum-typing (`attack_style`/`default_style`/`jad_attack_style`/`winner`):
  VERDICT marginal. Documentation-only in this build (weak C enums, no
  `-Wswitch-enum`). Low risk, low payoff. Optional.
- #4 `InfNPCStats`/`InfNPCOverlay` dedup: VERDICT marginal / arguable wash. The
  two-struct split is defensibly clear (overlay = "fields the generated monster
  DB doesn't carry"). Merging trades clarity for less duplication.

## Hotspot #2 — the InfNPC tagged union, done safely

In C a union gets NO arm-checking ever — reading the wrong arm is silent
garbage. So this is NOT a mechanical field-move; do it as a restructure with a
seatbelt, or don't do it.

Type-specific field groups today (`encounter_inferno_model.inc`, InfNPC ~lines
379-440). VERIFY ownership before trusting these groupings:
- meleer: `no_los_ticks`, `dig_freeze_timer`, `dig_attack_delay`
- blob: `blob_scan_timer`, `blob_scanned_prayer`, `had_los_last_tick`
- jad: `jad_attack_style`, `jad_healer_spawned`
- healer: `jad_owner_idx` (NOTE: comment says this is a HEALER field — which jad
  it heals — despite the `jad_` prefix), `heal_target`, `heal_timer`
- mager resurrection: `resurrect_cooldown`, `resurrection_count`,
  `resurrecting_this_tick`, `resurrection_visual_target`
- NOT type-specific, keep in the common struct: `frozen_ticks` (ice barrage,
  applies to any NPC), `aggro_target`, `death_ticks`, `pending_hit`, and the
  per-tick render flags. Audit each before moving.

Recipe:
1. Zero-init the whole `InfNPC`, then a per-type arm-init `switch` sets each
   arm's non-zero defaults (the `-1`s). This replaces the uniform init in
   `inf_spawn_npc` and makes the state transition explicit (P7). This is the
   step that defuses the footgun.
2. Asserting accessors: `inf_npc_blob(n)` asserts `n->type` is a blob before
   returning `&n->blob`, etc. Route all ~90 access sites through them so a
   wrong-arm access crashes loud in debug (P4) instead of corrupting silently.
3. Run the net + the asserts together — the 1435 scenario test exercises the
   deep paths, so violations surface as a crash during the test, not a silent
   training bug.
4. Optional: add `-Wswitch-enum` to the test build to get exhaustiveness checks
   on the per-type dispatch.
5. Slice and commit per NPC family, net-gated each time: blob first (most
   self-contained, `blob_scanned_prayer` has the trickiest init), then jad, then
   healer/resurrect, then meleer-dig.

If a slice can't be made clean (cross-type reads you can't untangle), STOP and
report rather than forcing the union — that's the failure mode to avoid.

## Rename mechanics that worked (reuse for #2)

InfernoState aliases in the .inc/.c are `s`, `sim`, `dst`, `state`; other structs
are always reached through an intervening `.`/`[` (`s->player.x`, `s->npcs[i].y`),
so anchoring a regex on `(s|sim|dst|state)->FIELD` hits only InfernoState. The
test file (`test_inferno_attack_styles.c`) uses stack values (`state.X`,
`healing_state.X`) reached by `.`, and `binding.c` is a separate consumer with
its own access sites + JSON string-literal keys (don't rewrite the string keys).
Grep all four surfaces: the 11 `.inc`, the scenario test, `binding.c`, and
`encounter_inferno_obs_mask.inc`/`render_snapshot.inc` specifically.

## Branch / build notes

- You're in a git worktree. Run everything from the worktree root; don't `cd`
  into the main checkout.
- `5.0` and the feature branches carry essentially identical OSRS code; there's
  no `setup.py` here, the build is `build.sh`, and `data/` + `raylib-5.5_macos/`
  live in the main checkout (gitignored).
