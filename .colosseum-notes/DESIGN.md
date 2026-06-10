# Fortis Colosseum — OSRS Puffer Env Design

Integration spine. Confirmed facts below are from direct recon of `5.0`. Sections marked
DRAFT/pending are filled from the three knowledge-base agents (mechanics, inferno-arch, shared-api).

## Goal & constraints (directive)
- Build the OSRS **Fortis Colosseum** encounter as a buildable PufferLib env, Inferno-shaped.
- **Maximize reuse** of shared `ocean/osrs/osrs_*.h`; **minimize encounter-specific code**.
- Model mechanics **rigorously and truthfully** — OSRS Wiki is the source of truth.
- Clean topic branch `valtteri/osrs-colosseum` off `5.0` (upstream-shaped, no Puffer-internal edits).

## Confirmed architecture (5.0 recon)
Per-env wrapper dir `ocean/osrs_colosseum/`:
- `osrs_colosseum.c` → `#define OSRS_VISUAL` + `#include "../osrs/osrs_visual.c"` (2 lines, mirrors inferno).
- `binding.c` → PufferLib glue: obs/action/reward/terminal/truncation/mask/log buffers + reset()/step() + env-knob parsing. Inferno's is ~2471 lines; keep ours as thin as the obs/action space allows.
- `cpu_stubs/` — confirm whether needed for `--cpu`.

Encounter logic `ocean/osrs/encounters/encounter_colosseum.h` composing `colosseum/*.inc`,
mirroring inferno's split: model, helpers, reset_spawn, movement, combat, player_actions,
reward_step, (forecast?), obs_mask, render_snapshot.

Shared visual/standalone entry: `ocean/osrs/osrs_visual.c` (dispatch by compile define — CONFIRM).
Training config: `config/ocean/osrs_colosseum.ini` (mirror `osrs_inferno.ini` sections).
`build.sh`: `elif [ -d "ocean/$ENV" ]` sets `SRC_DIR`; standalone compiles `$SRC_DIR/$ENV.c`.
The `osrs_inferno` special-cases (`setup-data.sh`, `-Xcompiler=-fpermissive`) are gated on
`ENV==osrs_inferno`; generalize to `osrs_*` or add an `osrs_colosseum` arm.

## Env/training contract (from osrs_inferno.ini)
- `[base]` policy MinGRU + Recurrent, `score_metric=score`.
- `[vec]` 4096 agents / 2 buffers / 16 threads. `[policy]` hidden 512, layers 2.
- Action space multi-discrete (inferno: 9 heads, 89 total discrete choices).
- Obs: flat float vector + embedded action mask (inferno: 1290 obs + 89 mask, `mask_in_obs=1`).
- Long episodes (hundreds–thousands of ticks). Curriculum = fraction of agents starting at later waves.
- Reward shaping lives entirely in `[env]` knobs parsed by binding.c. Colosseum needs its own terms
  (per-wave/per-NPC damage + kill bonuses, boss-phase bonuses) replacing jad/zuk/shield terms.

## Encounter interface (CONFIRMED — osrs_encounter.h:2183 `EncounterDef` vtable)
Encounters are polymorphic. `osrs_visual.c` includes every encounter header and drives them
through this vtable via `encounter_find(name)` + an `encounter_register()` registry
(`MAX_ENCOUNTERS`, osrs_encounter.h:2321-2338). The training `binding.c` does the same.
So nearly all generic plumbing (visual harness, human input, snapshot/restore, curriculum
cell keys) is SHARED. Colosseum implements an `EncounterDef`:
- `create()` / `init_state` / `reset(state, ctx, seed)` / `destroy`
- `step(state, ctx, actions)`  ← the per-tick sim
- `write_obs(state, ctx, float* obs)` / `write_mask(state, ctx, float* mask)`  ← obs + action mask
- `get_reward` / `is_terminal` / `get_winner` / `get_tick` / `progress_score`
- `snapshot_size`/`snapshot`/restore (state save/load), `cell_key_size`/`write_cell_key` (curriculum)
- `get_entity_count`/get-entity (`Player*`), overlay/render hooks, `get_log`
- `put_int`/`put_float`/`put_ptr` param setters (how `.ini` [env] knobs + visual params reach the encounter)
This is the seam that satisfies "minimize env-specific code": implement the vtable on top of
shared helpers; the data model + obs/action/reward are the only bespoke parts.
Registration + `osrs_visual.c` include + (training) binding obs/action sizing are the wiring edits.

## Open questions (resolve from agents)
- inferno-arch: exact core struct, step-loop order, binding obs/action/mask wiring, visual dispatch, new-encounter checklist.
- shared-api: the reuse map — which shared fns for combat math, movement/pathfinding, projectiles, consumables, prayer, gear, NPC stat tables.
- mechanics: 12 waves + counts, full NPC stat roster, modifier system, Sol Heredit moveset + prayer sequences.

## Data model (DRAFT — fill after agents)
- Reuse shared player struct (HP/prayer/inventory/equipment/position) from `osrs_encounter_player.h`.
- NPC array reusing shared NPC struct + stat-table lookup; Colosseum NPC type enum.
- Wave/spawn state, modifier set (active modifiers bitmask + levels), boss substruct (phase, attack queue, heat).

## Obs / action / reward (DRAFT)
- Action: mirror inferno multi-discrete heads (move, attack-target, prayer, gear/spec, eat/drink, ...). Trim heads Colosseum doesn't need.
- Obs: egocentric tiles, player vitals/prayer, per-NPC features (type/pos/hp/attack-tells), active modifiers, boss state, incoming-attack forecast.
- Reward: sparse progress (wave clear, boss phase, win) + small dense damage shaping; avoid suicide-perversity (see feedback_reward_design).

## Confirmed per-encounter conventions (from Zulrah, encounter_zulrah.h)
- Each encounter declares its own obs/action constants: `ZUL_NUM_OBS 123`, `ZUL_NUM_ACTION_HEADS 7`,
  `ZUL_MOVE_DIM = ENCOUNTER_MOVE_ACTIONS` (SHARED move head), `ZUL_ATTACK_DIM`, `ZUL_PRAYER_DIM`,
  `ZUL_ACTION_MASK_SIZE = sum(head dims)`. Colosseum gets `COLO_NUM_OBS`, `COLO_NUM_ACTION_HEADS`, etc.
- **NPC stats come from the shared `MONSTER_DATABASE`** (osrs_monsters_generated.h): e.g.
  `MONSTER_DATABASE[MON_ZULRAH_GREEN].hp`. So Colosseum NPCs should be added as generated monster
  entries and referenced by `MON_*` id, NOT hand-coded stat literals. (Confirm generation source via shared-api agent.)
- Encounter-specific obs are appended after a shared base block (Zulrah uses indices up to 84 for shared,
  85+ for cloud-specific). Mirror: shared player/npc base obs + Colosseum-specific (waves, modifiers, boss) tail.
- Bespoke mechanics modeled as explicit state machines (Zulrah: phase action lists `ZulAction`/`ZUL_MAX_PHASE_ACTIONS`).
  Colosseum: wave script + Sol Heredit attack-combo state machine.

## Implementation phases
0. Knowledge base (agents running).
1. Lock design + scaffold that compiles empty (env dir, encounter header w/ shared includes, build+config wiring, visual dispatch).
2. Arena + reset/spawn + player (shared player/movement/prayer).
3. NPC roster: stats, AI, attack styles, combat resolution via shared combat math.
4. Wave progression + reinforcements + modifier selection/effects.
5. Sol Heredit boss state machine (attack combos, prayer tells, shield/grapple, heat/phases).
6. Obs vector + action mask + reward shaping.
7. binding.c + config/ocean/osrs_colosseum.ini + build.sh wiring.
8. Validate: `./build.sh osrs_colosseum --local` (standalone+sanitizers), visual inspect, smoke train.

---

# LOCKED DECISIONS (all 3 agents in; detail in mechanics.md / inferno-architecture.md / shared-api.md)

## File plan (mirror inferno; 15 create / ~4 edit, constructor auto-registers)
CREATE:
- `ocean/osrs/encounters/encounter_colosseum.h` (shared includes + composes the .inc below + `EncounterDef ENCOUNTER_COLOSSEUM` + constructor `encounter_register`).
- `ocean/osrs/encounters/colosseum/{model,helpers,reset_spawn,movement,combat,player_actions,reward_step,obs_mask,render_snapshot}.inc`. SKIP `forecast.inc`+`lab.inc` (inferno extras); add a lean boss-AoE telegraph inside obs instead.
- `ocean/osrs_colosseum/osrs_colosseum.c` (`#define OSRS_VISUAL` + include ../osrs/osrs_visual.c).
- `ocean/osrs_colosseum/binding.c` (mirror inferno; entry pts c_step/c_reset/c_close/c_render/my_init/my_vec_init/my_log).
- `config/ocean/osrs_colosseum.ini`.
EDIT:
- `ocean/osrs/osrs_visual.c`: add `#include "encounters/encounter_colosseum.h"` + name strcmp arm (collision_map/world_offset params).
- `build.sh`: generalize the two `ENV==osrs_inferno` special-cases (`setup-data.sh`, `-Xcompiler=-fpermissive`) to `osrs_*`.
- Log: define `ColosseumLog` in state + `get_log` hook. Do NOT add fields to shared `osrs_types.h` Log (it is ~95% inferno/Zuk). Confirm binding `my_log` reads our log via the hook.
- `asset_manifest.json`: DEFER to visual phase.

## Data materialization (build prerequisite)
`ocean/osrs/data/` is generated/gitignored (not in worktree). For local build, symlink
`$WT/ocean/osrs/data -> ~/Projects/pufferlib-metal/ocean/osrs/data` (branch-independent generated assets).
Colosseum render model IDs (npc_model_lookup) deferred to the OSRS-cache export pipeline; stub initially so sim/training is unblocked. NPC stat tables live in `osrs_monsters_generated.h` (NOT data/npc_models.h).

## Data model `ColosseumState` (flat, snapshot-able; embed shared `Player`)
- `ColoNPC npcs[COLO_MAX_NPCS]` — single struct + `ColoNpcType` enum (berserker, archer, seer, serpent_shaman, jaguar, javelin_colossus, shockwave_colossus, minotaur, manticore, sol_heredit) + union for type state (manticore barrage cycle, jaguar 3-hit, boss).
- NPC stats via `MONSTER_DATABASE[MON_*]` + a `COLO_NPC_OVERLAY[]` extras table merged like inferno's `inf_build_npc_stats()`. Add `MON_COLO_*` entries (regen osrs_monsters_generated.h or overlay-only — decide in NPC phase).
- Wave/spawn: wave index 1..12, spawn list, **40s (66-tick) reinforcement timer**, gate spawn points.
- Modifiers: active bitmask + per-modifier tier; between-wave offer of 3, pick 1, persists.
- `SolHereditState`: phase (100/90/75/50/25/10% thresholds), AoE attack-combo queue (Spear1/Spear2/Shield1 7x7/Shield2 9x9, dodge-only typeless max 44), triple-parry seq (<90%), grapple (<75%: body-part slot + 4-tick timer), crystal/light-beam spheres, molten-sand tiles (<10%, every 3t).
- Reuse `EncounterPendingHitQueue` for deferred hits; `OsrsEncounterArena`/collision grids; `uint32_t rng_state`; `ColosseumLog` telemetry.

## Obs / action (constants in header, hard-assert in writer like inferno)
- Action heads (lean, reuse shared dims): MOVE=`ENCOUNTER_MOVE_ACTIONS`(25), PRAYER(protect melee/range/magic + offensive), TARGET(N-nearest NPC), GEAR(equip-slot swaps; doubles as grapple body-part click), EAT, POTION, SPEC, MODIFIER_SELECT(1of3 between waves). Drop SPELL unless a freeze/mage loadout is modeled. `COLO_NUM_ACTION_HEADS` + `COLO_ACTION_MASK_SIZE`.
- Obs: shared player vitals/prayer/equipment + per-NPC slots (type, egocentric dx/dy, hp, attack-style tell, ticks-to-attack) + active modifiers + wave state + boss block (phase, current AoE pattern + safe-tile vector, parry tick, grapple slot+timer, molten-sand map). `COLO_NUM_OBS`, mask embedded (`mask_in_obs=1`).

## Reward (sparse + light dense; anti suicide-perversity per feedback_reward_design)
- +damage_reward_coeff * dmg dealt, +wave_clear_bonus, +boss_phase_bonus per HP threshold, +win_bonus. Death/terminal penalty OFF (inferno uses 0). Boss-damage + per-phase kill bonuses as separate knobs. ColosseumLog: wave reached, boss hp/phase, deaths.

## Step loop (mirror inferno exactly — reward_step.inc:494 order)
pretick prayer → wave-gap timers → resolve in-flight projectiles → tick NPCs (move+attack+AI) → **early death check (BEFORE player actions)** → tick player (target→move→attack OSRS order) → reward → death → wave spawn → wave-complete (all dead → bonus + next/win) → tick-cap. Win=clear wave 12 (Sol Heredit dead); loss=HP<=0 or tick cap.
