# Inferno encounter architecture (implementation reference)

Exhaustive map of the OSRS Inferno env so Fortis Colosseum can be cloned as a
buildable PufferLib env with minimal env-specific code. All paths absolute. All
`file:line` against the `valtteri/osrs-colosseum` worktree
(`/Users/valtterivalo/.codex/worktrees/osrs-colosseum/pufferlib-metal`).

Two-layer split, mirror it exactly:
- **Encounter logic** (engine-agnostic, no PufferLib): `ocean/osrs/encounters/encounter_inferno.h` + 11 `inferno/*.inc`. Exposes one `static const EncounterDef ENCOUNTER_INFERNO` vtable and self-registers via a constructor.
- **PufferLib binding** (engine glue): `ocean/osrs_inferno/binding.c` drives that vtable through `vecenv.h` and fills the obs/action/reward/terminal buffers.
- **Standalone viewer**: `ocean/osrs_inferno/osrs_inferno.c` is 2 lines (`#define OSRS_VISUAL` + include `osrs/osrs_visual.c`). The viewer finds the encounter by name through the registry.

---

## 1. CORE STRUCT

### `InfernoState` — the env state (`encounter_inferno_model.inc:833-1025`)
Plain C struct, `memset`-zeroed at reset, snapshotted by value. Top-level fields grouped:

- **Player**: `Player player` (`:834`) — the shared OSRS player struct from `osrs_types.h` (HP, prayer, stats, equipment, inventory, brew/restore/bastion/stamina doses, special_energy, attack_timer, run_energy, item_effect_state). Inferno does NOT define its own player struct; it reuses `Player`.
- **Per-tick scratch**: `InfTickScratch tick_scratch` (`:836`) — see below.
- **NPC array**: `InfNPC npcs[INF_MAX_NPCS]` (`:838`), `INF_MAX_NPCS = 32` (`model.inc:354`). `uint32_t next_npc_render_id` (`:839`).
- **Obs slot map**: `int current_obs_slots[INF_OBS_NPCS]` (`:840`), `INF_OBS_NPCS = 37` (`model.inc:355`) — maps each fixed obs slot to an NPC array index or `-1`.
- **Pillars**: `InfPillar pillars[INF_NUM_PILLARS]` (`:841`), `INF_NUM_PILLARS = 3` (`model.inc:18`). `InfPillar` = `{int x,y; int hp; int active;}` (`:511-515`). `INF_PILLAR_HP=255`, `INF_PILLAR_SIZE=3`.
- **Boss state**: `InfZukState zuk` (`:842`) — shield_idx/dir/freeze, set spawn timers, enraged, healer_spawned, jad_spawned, timer_paused (`:518-535`).
- **Dead-mob store** (mager resurrection): `InfDeadMob dead_mobs[INF_MAX_DEAD_MOBS]` + `int dead_mob_count` (`:845-846`), `INF_MAX_DEAD_MOBS=16`. `InfDeadMob` = `{type,x,y,hp,max_hp}` (`:361-365`).
- **LOS blockers**: `LOSBlocker los_blockers[INF_NUM_PILLARS]` + count (`:849-850`), rebuilt when pillars change.
- **Wave tracking** (`:853-859`): `int wave` (0-indexed 0..68), `int wave_spawn_target`, `int tick`, `int wave_spawn_delay`, `int wave_ready_delay`, `int episode_over`, `InfOutcome winner`.
- **Reward bookkeeping** (`:862-1024`): `float reward` (this tick), `float episode_return`, `float min_zuk_hp_seen`, dozens of Zuk-phase diagnostic counters/timers (`tick_at_le_300/240/150`, `damage_after_*`, healer-tag/kill counters, idle counters, per-NPC-type `prayer_correct_by_type[]`/`attacks_by_type[]`/`dmg_from_type[]`/`killed_by_type[]`). Most of these are inferno-specific telemetry; Colosseum replaces them.
- **Interaction**: `OsrsInteraction interaction` (`:947`) — shared click-target state. `player_last_interaction_target_slot/age` (`:948-949`).
- **Gear**: `InfWeaponSet weapon_set` (`:952`), `InfLoadoutProfile active_loadout_profile` (`:953`), `EncounterLoadoutStats loadout_stats[INF_NUM_WEAPON_SETS]` (`:954`, precomputed combat stats per weapon set), `int stamina_active_ticks`.
- **Player attack render event** (`:959-962`): `player_attack_npc_idx`, `player_attack_dmg`, `player_attack_style_id`, `EncounterProjectileTiming player_attack_timing`.
- **Projectiles in flight**:
  - On player: `EncounterPendingHitQueue player_pending_hits` (`:965`) — shared queue type.
  - Per NPC: `EncounterPendingHitQueue pending_hits` inside each `InfNPC` (`:443`).
  - Zuk-healer sparks: `InfPendingSpark pending_sparks[INF_MAX_PENDING_SPARKS]` (`:966`), cap 32. `InfPendingSpark` = `{active,src_x,src_y,x,y,damage,ticks_remaining,visual_emitted}` (`:371-378`).
  - NPC-on-NPC hits: `InfNpcTargetHit npc_target_hits[INF_MAX_NPC_TARGET_HITS]` (`:967`), cap 16 (`:382-387`).
- **Collision grids** (`:984-985`): `uint8_t npc_collision_flags[INF_ARENA_WIDTH][INF_ARENA_HEIGHT]`, `uint8_t player_collision_flags[...]`. Arena 29x30 (`model.inc:6-7`). Pathfinding ignores these; movement application checks them.
- **LOS cache**: `int8_t npc_los_cache[INF_MAX_NPCS]` (`:981`) — lazy per-tick (-1 unset / 0 / 1).
- **Config copy / RNG**: `int start_wave` (`:988`), `uint32_t rng_state` (`:989`). RNG defaults to 12345 then reseeded per-env via `encounter_resolve_seed` at reset.

### NPC typing: `InfNPC` (`model.inc:404-465`)
Single struct for every monster; `type` is the discriminant. Flat fields read in
generic per-tick loops; type-specific state lives in a tagged union:
```c
union { InfJadState jad; InfHealerState healer; } type_state;   // :437-440
```
Accessed only through asserting accessors `inf_npc_jad()` / `inf_npc_healer()`
(`:475-489`) — never touch the union directly. `inf_npc_init_type_state()`
(`:495-509`) sets per-arm spawn defaults in one place.

Key `InfNPC` fields: `type,x,y,hp,max_hp,size,render_id,attack_timer,attack_style,active,target_x/y,stun_timer`; meleer dig (`no_los_ticks,dig_freeze_timer,dig_attack_delay`); blob (`blob_scanned_prayer,had_los_last_tick`); mager (`resurrect_cooldown,resurrection_count`); `frozen_ticks`; `pending_hits`; `death_ticks` (death-animation linger, >0 = dying); `aggro_target` (-1=player, else NPC idx); a block of per-tick render flags cleared each step (`attacked_this_tick,attack_style_this_tick,hit_landed_this_tick,hit_damage,...`).

### NPC type enum: `InfNPCType` (`encounter_inferno.h:48-64`)
14 types: NIBBLER, BAT, BLOB, BLOB_MELEE, BLOB_RANGE, BLOB_MAGE, MELEER, RANGER, MAGER, JAD, ZUK, HEALER_JAD, HEALER_ZUK, ZUK_SHIELD; `INF_NUM_NPC_TYPES=14`.

NPC stats come from a **two-table merge** (key pattern to copy):
- `MonsterIndex INF_NPC_TO_MON[]` (`encounter_inferno.h:161-176`) maps each type to a row in the generated `MONSTER_DATABASE` (`osrs_monsters_generated.h`).
- `InfNPCOverlay INF_NPC_OVERLAY[]` (`:179-194`) holds encounter-specific fields the generated DB lacks (attack_range, default_style, melee_style, can_melee, magic dmg, max_hit_cap, stun_on_spawn, can_move).
- `inf_build_npc_stats()` (`:200-241`) merges both into `InfNPCStats INF_NPC_STATS[]` at startup (called from `inf_reset_ctx` and `inf_init_state`).
- `InfNPCType -> cache def id` via `INF_NPC_DEF_IDS[]` (`:74-89`) for the renderer.

### Other model-level types
- `InfWaveDef INF_WAVES[INF_NUM_WAVES]` (`:251-350`): each wave is `{uint8_t types[9]; int count}`. 69 waves (`INF_NUM_WAVES=69`), `INF_MAX_NPCS_PER_WAVE=9`. Built with `W(...)` X-macro.
- `InfConfig` (`:719-771`): the full env-knob struct (~70 fields: start_wave, reward coeffs, curriculum knobs, oracle_mode, forecast mode, etc.).
- `InfernoContext` (`:773-782`): `{InfConfig config; const CollisionMap* collision_map; int world_offset_x/y; Log* log; HumanCommand* human_commands; ...}`. **State vs context split**: gameplay state in `InfernoState`, live-process/config data in `InfernoContext`.
- `InfTickScratch` (`:789-831`): every per-tick reward/event accumulator. Reset in ONE assignment `s->tick_scratch = (InfTickScratch){.pillar_lost=-1};` (`reward_step.inc:506`) so a new field is reset for free.
- Loadout tables `INF_*_LOADOUT[NUM_GEAR_SLOTS]` (`:558-657`): item-id arrays per weapon set per profile.

---

## 2. EACH .inc FILE

Included in dependency order by `encounter_inferno.h:42-52`. All functions
`static`; a `_ctx`-suffixed variant takes an explicit `InfernoContext*`, the
bare variant forwards to `inf_legacy_context()` (a process-global fallback,
`helpers.inc:959-969`). The binding always calls the `_ctx` forms.

### `encounter_inferno_model.inc` (1025 lines) — data model
Defines every constant, enum, and struct above. No behavior except
`inf_build_npc_stats()` and the union-init/accessor helpers. **This is where the
bulk of Colosseum's "new struct" work goes.**

### `encounter_inferno_helpers.inc` (992 lines) — shared primitives + context lifecycle
- Pending-hit queue wrappers: `inf_queue_npc_pending_hit()` (`:5`), `inf_queue_player_pending_hit()` (`:42`).
- LOS: `inf_rebuild_los()` (`:60`), `inf_npc_has_los_to_area/tile/direct()` (`:123/137/144`), cached `inf_npc_has_los()` (`:152`), `inf_invalidate_los_cache()` (`:161`).
- Threat model (drives obs + AI): `InfNpcPlayerThreat` + `inf_npc_player_threat()` (`:542`), `InfNpcPressureSummary` + `inf_npc_pressure_summary()` (`:631`).
- Projectile timing: `inf_npc_projectile_timing()` (`:742`), `inf_player_projectile_timing()` (`:750`).
- Anim id lookups, max-hit helpers, dead-mob store `inf_store_dead_mob()` (`:856`).
- Context lifecycle: `inf_default_config()` (`:891`), `inf_init_context()` (`:951`), `inf_destroy_context()` (`:955`), `inf_legacy_context()` (`:959`), `inf_init_state_typed()` (`:971`), `inf_create()`/`inf_destroy()` (`:984/990`).
- Forward declarations of the cross-inc functions (`:872-889`).

### `encounter_inferno_reset_spawn.inc` (760 lines) — episode setup + wave spawning
- Supply profile model: `INF_SUPPLY_PROFILE_ANCHORS[]` (`:11`), `inf_supplies_for_start_wave()` (`:221`), curriculum jitter `inf_apply_curriculum_supply_variation()` (`:173`).
- **`inf_reset_ctx()` (`:275`)** — vtable `.reset`. Zeroes state (preserving start_wave + rng), inits player (99s, mage gear, supplies), pillars, LOS, then calls `inf_spawn_wave()`. See Lifecycle.
- Collision-grid stamp/unstamp helpers (`:446-504`).
- NPC slot mgmt: `inf_find_free_npc()` (`:425`), `inf_init_npc()` (`:516`), `inf_deactivate_npc()` (`:506`).
- **`inf_spawn_wave()` (`:610`)** — clears NPCs, shuffles spawn positions, special-cases waves 66/67/68 (jad/triple-jad/Zuk) and zuk-checkpoint curriculum starts; otherwise spawns `INF_WAVES[wave]` at shuffled positions.

### `encounter_inferno_movement.inc` (291 lines) — pathing + meleer dig
- Arena/pillar predicates `inf_in_arena()` (`:2`), `inf_blocked_by_pillar()` (`:12`).
- Collision-flag blockers for pathfinding callbacks (`:38-107`).
- **`inf_npc_move_ctx()` (`:150`)** — per-NPC movement: stun/freeze gates, step-out-from-under-player, target area selection, stop-when-LOS, then `encounter_npc_step_toward()` (shared). Nibblers path to pillars.
- **`inf_meleer_dig_check_ctx()` (`:225`)** — meleer burrow state machine (38-tick LOS-loss trigger, forced at 50, 6-tick freeze, re-emerge next to player).

### `encounter_inferno_combat.inc` (1390 lines) — all combat resolution + NPC AI
- Player attack: `inf_resolve_player_attack_ctx()` (`:106`), reach/LOS checks `inf_player_can_attack_npc_from_current_tile()` (`:215`), barrage/phantom-target helpers.
- NPC-on-NPC hits (set→shield): `inf_resolve_npc_target_hits()` (`:433`).
- Defensive item procs: `inf_apply_elysian_to_player_hit()` (`:444`), `inf_apply_echo_boots_recoil()` (`:457`).
- **`inf_npc_attack_ctx()` (`:497`)** — the big per-NPC attack routine (~420 lines): style selection, prayer-check, projectile queueing, blob scan, jad 50/50, damage to player.
- Mager resurrection `inf_mager_resurrect_ctx()` (`:945`); jad-healer spawn `inf_jad_check_healers_ctx()` (`:1045`); **`inf_zuk_tick()` (`:1076`)** (shield bounce, set/jad/healer spawns, enrage).
- Zuk-healer sparks `inf_queue_zuk_healer_sparks()` (`:1261`) / `inf_resolve_pending_sparks()` (`:1289`).
- Redemption prayer `inf_apply_redemption_if_due()` (`:1203`).
- **`inf_tick_npcs_ctx()` (`:1308`)** — the NPC update loop (see Lifecycle); also handles blob splits and death linger.

### `encounter_inferno_player_actions.inc` (1178 lines) — action heads + player tick
- **Action head layout** (`:2-23`): the 9 `INF_HEAD_*` indices, `INF_ACTION_DIMS_INIT`, `INF_ACTION_DIMS[]`, `INF_ACTION_MASK_SIZE`. `inf_action_head_mask_offset()` (`:26`).
- Offensive-prayer helpers, reward-damage attribution `inf_record_player_reward_damage()` (`:81`).
- Player attack target lookup `inf_lookup_player_attack_target()` (`:142`), NPC death `inf_apply_npc_death()` (`:185`), blood-barrage heal (`:208`).
- Oracle (scripted-policy debug) `inf_oracle_pick_full()` (`:236`) and override gates.
- **`inf_player_pretick()` (`:379`)** — applies prayer/offensive-prayer actions + drain BEFORE movement/attack (called first in step).
- Human-input command application (`:435`).
- **`inf_tick_player_ctx()` (`:493`)** — the main per-tick player routine (~500 lines): gear switch, spec toggle, stat decay, brew/potions, target selection (with interaction interrupt rules), movement, attack. **OSRS order: target → move → attack.**
- Projectile resolution onto NPCs / player: `inf_resolve_player_projectiles_on_npcs()` (`:1062`), `inf_resolve_player_pending_hits()` (`:1122`), `inf_resolve_jad_prayer_checks_after_player()` (`:1163`).

### `encounter_inferno_reward_step.inc` (724 lines) — reward + THE STEP LOOP
- Reward terms: `inf_zuk_low_watermark_reward()` (`:61`), `inf_compute_joseph_reward()` (`:124`), **`inf_compute_reward_ctx()` (`:156`)** (banks per-tick totals, returns shaped reward; terminal returns ±1 / -death_penalty), `inf_terminal_loss_reward()` (`:339`).
- Idle/progress diagnostics (`:347-492`).
- **`inf_step_ctx()` (`:494`)** — vtable `.step`, the tick orchestrator. See Lifecycle.

### `encounter_inferno_forecast.inc` (1162 lines) — step-out lookahead obs + obs-size constants
- **Obs-size constants (`:3-16`)**: `INF_PLAYER_OBS_SIZE 75`, `INF_PILLAR_OBS_SIZE (3*5)`, `INF_BASE_NPC_OBS_SIZE 896`, `INF_STEP_OUT_FORECAST_*`, `INF_FEATURES_PER_HIT 5`, `INF_FEATURES_PER_SPARK 7`, **`INF_NUM_OBS`** (the total). These define the obs tensor width — Colosseum must redefine them.
- The "step-out forecast" simulates each of the 25 move actions forward `INF_STEP_OUT_FORECAST_HORIZON=4` ticks to tell the policy which tile is safe. Modes 0-3 (off / exact-rollout / fast-static / fast-readonly). This is inferno's heaviest obs feature and ~85% of env step time per project memory. **Optional for Colosseum** — can ship mode 0 (off) and a smaller obs.

### `encounter_inferno_lab.inc` (1031 lines) — dev scenario harness
The "inferno lab": hand-place NPCs/pillars and replay scenarios from the viewer
(`inf_lab_apply_command()` `:200`, script parsing, JSON dump). Pure debug tooling,
NOT part of the env contract. **Skip entirely for v1 Colosseum.**

### `encounter_inferno_obs_mask.inc` (931 lines) — obs writer + action mask + render entities
- **`inf_refresh_current_obs_slots_ctx()` (`:89`)** — assigns each NPC to a fixed obs slot by type (the `slot_offsets`/`slot_max` table `:97-110` maps types to slot ranges: 2 mager, 2 ranger, 2 meleer, 2 blob, 2 bat, 2 each blob-split, 6 nibbler, 3 jad, 1 zuk, 1 shield, 6 jad-healer, 4 zuk-healer = 37).
- **`inf_write_obs_ctx()` (`:144`)** — vtable `.write_obs`. Writes all `INF_NUM_OBS` floats. Hard `abort()` if the running index `i` ever mismatches the section constants (`:474,523,578`).
- **`inf_write_mask_ctx()` (`:628`)** — vtable `.write_mask`. Per-head validity (see §4).
- `inf_fill_render_entities_ctx()` (`:820`) — vtable `.fill_render_entities`.

### `encounter_inferno_render_snapshot.inc` (1244 lines) — config kwargs + snapshot + vtable
- **`inf_put_int_ctx()` (`:12`)**, **`inf_put_float_ctx()` (`:106`)**, **`inf_put_ptr_ctx()` (`:206`)** — the `key`-string config dispatch. Unknown key → `encounter_abort_unknown_config()`. **This is the binding-kwargs contract** — every `[env]` knob is parsed here.
- `inf_get_tick/winner/log_ctx()` (`:222/231/251`), `inf_render_post_tick_ctx()` (`:368`), human-input translate (`:687`), `inf_step_human_commands_ctx()` (`:809`).
- Go-Explore cell key `inf_cell_key_size/write_cell_key` + `inf_progress_score_ctx()` (`:895-1005`).
- **Snapshot/restore** (`:1010-1182`): `InfSnapshot` = `{magic,version,state_size,reserved,config_fingerprint, InfernoState state}` (`:1015-1022`). `inf_config_fingerprint()` hashes every config field; restore aborts on magic/version/size/fingerprint mismatch. `inf_refresh_after_state_load()` (`:1091`) rebuilds derived state (loadout stats, LOS, collision flags, obs slots).
- **`inf_refresh_after_state_load()`** is also the `puffer_state_refresh` hook the binding calls after archive loads.
- **THE VTABLE: `static const EncounterDef ENCOUNTER_INFERNO` (`:1184-1239`)** plus `__attribute__((constructor)) inf_register()` (`:1241-1243`) that calls `encounter_register(&ENCOUNTER_INFERNO)`.

---

## 3. LIFECYCLE

### Reset/spawn (`inf_reset_ctx`, reset_spawn.inc:275)
1. `inf_build_npc_stats()`; save `start_wave`+`rng_state`; `memset(s,0)`; restore them; reseed via `encounter_resolve_seed`.
2. Init `-1` sentinels (dest, interaction, all Zuk-phase tick markers).
3. Init `Player`: all base/current stats 99, mage gear via `encounter_apply_loadout`, inventory via `encounter_populate_inventory`, supplies from the wave's supply profile + curriculum jitter, prayer NONE, special 100, run energy full.
4. Precompute `loadout_stats[3]` via `encounter_compute_loadout_stats`.
5. Player spawn tile (zuk-wave vs normal), `inf_rebuild_player_collision_flags`.
6. Pillars (none if `effective_start >= 66`), `inf_rebuild_los`.
7. Set `wave/wave_spawn_target = effective_start`, `wave_spawn_delay=0`, `wave_ready_delay=INF_START_READY_TICKS(6)`, then `inf_spawn_wave()`, `inf_invalidate_los_cache()`.

### Per-tick step loop (`inf_step_ctx`, reward_step.inc:494) — EXACT ORDER
1. `if (s->episode_over) return;` (`:497`).
2. Clear scratch: `reward=0`, save `player_moved_last_tick`, `tick_scratch = {.pillar_lost=-1}`, clear player tick flags, clear every NPC's per-tick render flags (`:500-522`).
3. `s->tick++` (`:523`).
4. **`inf_player_pretick()`** — prayer/offensive-prayer actions + drain (`:524`).
5. **Wave-gap timers** (`:526-535`): decrement `wave_spawn_delay` (sets `spawn_wave_now` when it hits 0) and `wave_ready_delay`.
6. **Resolve in-flight projectiles** (always, even during gaps): player projectiles onto NPCs, pending hits onto player, pending sparks (`:537-539`).
7. **`inf_tick_npcs_ctx()`** — only if not in wave/ready gap (`:541-545`): rebuild player collision flags, invalidate LOS cache, then the NPC loop.
8. `inf_update_healer_transition_stats()` (`:546`).
9. **Early death check** (`:548-558`): if HP<=0, compute reward, credit killer, set `episode_over`+`winner=DIED`, `reward = terminal_loss`, return. (Stops before player actions so a lethal hit can't be brewed back.)
10. **`inf_tick_player_ctx()`** — player actions (gear/spec/potions/target/move/attack), gated by `can_player_attack = !gap` (`:560-567`). Recompute `player_moved`, invalidate LOS if moved.
11. `inf_resolve_jad_prayer_checks_after_player()` (`:573`).
12. Idle/pressure/diagnostic accounting (`:575-661`).
13. **`s->reward = inf_compute_reward_ctx()`** — banks per-tick damage totals into episode totals, returns shaped reward (`:665`).
14. **Death check again** (`:668-676`): post-action HP<=0 → terminal loss, `goto finish_step`.
15. **Wave spawn** (`:678-684`): if `spawn_wave_now`, set `wave=wave_spawn_target`, `inf_spawn_wave()`, goto finish. If still in spawn delay, goto finish.
16. **Wave completion** (`:686-704`): if all NPC slots inactive → `wave_completed=1`, reward `1.0 + supply_milestone_surplus`. If `wave+1 >= 69` → `episode_over`, `winner=WON`. Else queue next wave with `wave_spawn_delay=9`.
17. **Tick cap** (`:706-710`): `tick >= INF_MAX_TICKS(18000)` → `episode_over`, `winner=DIED`, terminal loss.
18. `finish_step:` idle diagnostics, `episode_return += reward` (`:712-719`).

### NPC update loop (`inf_tick_npcs_ctx`, combat.inc:1308)
`inf_resolve_npc_target_hits()` → `inf_zuk_tick()` → for each active NPC: healer
heal-landing, death-linger decrement (deactivate + queue blob splits when done),
freeze/resurrect-cooldown decrement, meleer dig check, **`inf_npc_move_ctx()`**,
**`inf_npc_attack_ctx()`**, jad-healer spawn. After the loop, spawn queued blob splits.

### Wave advance & termination
- Advance: wave clears when every NPC slot is inactive → 9-tick gap → `wave = wave+1` → `inf_spawn_wave()`. There is no per-wave timer; clearing all NPCs is the only trigger (except boss waves which end the episode).
- End WON: clearing wave index 68 (Zuk) → `episode_over`, `winner = INF_OUTCOME_PLAYER_WON`, reward 1.0.
- End DIED: player HP<=0 (either death check) or `tick >= INF_MAX_TICKS`. `winner = INF_OUTCOME_PLAYER_DIED`, reward = `inf_terminal_loss_reward` (`-1` if `terminal_penalty_enabled` else `-death_penalty_coeff`).
- `InfOutcome` (`encounter_inferno.h:67-70`): `PLAYER_WON=0`, `PLAYER_DIED=1`.

---

## 4. binding.c (CRITICAL) — `ocean/osrs_inferno/binding.c` (2472 lines)

### Includes + glue defines
`#include "../osrs/osrs_env.h"` (pulls the whole shared OSRS stack) +
`encounter_inferno.h` + render headers (`:23-138`). Then:
```c
#define INF_TOTAL_OBS (INF_NUM_OBS + INF_ACTION_MASK_SIZE)   // :140
#define OBS_SIZE   INF_TOTAL_OBS        // :205  obs buffer = obs + embedded mask
#define NUM_ATNS   INF_NUM_ACTION_HEADS // :206  = 9
#define ACT_SIZES  INF_ACTION_DIMS_INIT // :207
#define OBS_TENSOR_T FloatTensor        // :208
#define Env InfernoEnv                  // :209
```
The mask is **embedded in the obs tensor** (obs slots `[INF_NUM_OBS .. INF_TOTAL_OBS)`), because `mask_in_obs=1`.

### `InfernoEnv` struct (`:142-198`) — the PufferLib env
```c
typedef struct InfernoEnv {
    void* observations; float* actions; float* rewards; float* terminals; // PufferLib buffers
    int num_agents; int rng; Log log;
    InfernoState state; InfernoContext context;   // embedded by value
    int config_start_wave;
    int acts_staging[INF_NUM_ACTION_HEADS];        // float actions -> int
    unsigned char term_staging;
    /* replay record/playback buffers, trace files, render_env (OsrsEnv), ... */
} InfernoEnv;
```
Buffers (`observations/actions/rewards/terminals`) are pointers **assigned by
`vecenv.h`**, not allocated here. The encounter `state`/`context` are embedded.
Accessor macros `INF_ENV_STATE/CONTEXT/INFERNO(env)` (`:200-203`) cast to the
shared `EncounterState*`/`EncounterContext*` or the typed `InfernoState*`.

### PufferLib entry points (the contract vecenv.h calls)
- **`my_init(Env* env, Dict* kwargs)` (`:1940-2212`)** — per-env constructor. `num_agents=1`; `init_context`+`init_state`; zero `Log`; then parse every kwarg via `ENCOUNTER_INFERNO.put_int/put_float` (a long allowlist of `optional_float_keys`/`optional_int_keys` + named ones). Applies `obs_profile`/`reward_profile` macro-knobs. Sets up `RECORD_REPLAY`/`PLAY_REPLAY` env-var buffers.
- **`my_vec_init(...)` (`:2218-2329`)** — allocates `calloc(total_agents, sizeof(Env))`, calls `my_init` per env, then assigns **curriculum start_waves** to a tail fraction of envs (tiers from `curriculum_wave_N`/`curriculum_frac_N`). Fills `buffer_env_starts/counts`.
- **`c_reset(Env* env)` (`:1710-1730`)** — closes trace files, then either `ENCOUNTER_INFERNO.restore(snapshot)` (replay) or `ENCOUNTER_INFERNO.reset(seed)`, marks episode start, writes obs+mask.
- **`c_step(Env* env)` (`:1208-1708`)** — the hot path:
  1. Lab/replay/human-input action source selection; else `acts_staging[i] = (int)env->actions[i]` (`:1246-1250`).
  2. Snapshot `action_mask_before`, stall-trace capture, buffer actions for replay (`:1253-1276`).
  3. **`ENCOUNTER_INFERNO.step(state, context, acts_staging)`** (`:1280`).
  4. **`write_obs(state, context, obs)`** then **`write_mask(state, context, obs + INF_NUM_OBS)`** (`:1284-1286`).
  5. `env->rewards[0] = get_reward(...)` (`:1289`).
  6. `is_term = is_terminal(...)`; `env->terminals[0] = (float)is_term` (`:1291-1293`).
  7. **Terminal-only logging** into `env->log` (`:1306-1647`): accumulates ~150 fields, gated on `start_wave == config_start_wave` (curriculum agents excluded). Best-replay flush (`:1649-1705`).
  8. **On terminal, reset in place** (`:1697-1704`): `ENCOUNTER_INFERNO.reset(...)` + rewrite obs/mask + `pending_render_reset=1`. (PufferLib expects auto-reset; there is no separate truncation signal — both win and timeout set `terminals[0]=1`.)
- **`c_close(Env* env)` (`:1732-1744`)** — free replay buffers + `destroy_context` + render client.
- **`c_render(Env* env)` (`:1755-1843`)** — lazily makes a render client, loads inferno scene assets, drives `pvp_render`.
- **`my_log(Log* log, Dict* out)` (`:2353-2471`)** — converts the accumulated `Log` into the metrics dict. **Computes `score`** (`:2419-2428`): for start_wave>=68 it's `(1200 - min_zuk_hp_seen)/1200`; else `wins + (1-wins)*wave_frac*0.5`. This is the sweep objective key.
- `typedef InfernoState State;` + `puffer_state_refresh()` + `#define MY_VEC_INIT` + `#include "vecenv.h"` (`:1845-1852`) — `vecenv.h` is the shared PufferLib harness that defines the actual exported symbols and calls `my_init`/`c_reset`/`c_step`/`my_log`.

### Observation tensor layout (total `INF_TOTAL_OBS`; raw `INF_NUM_OBS`)
`INF_NUM_OBS = INF_PLAYER_OBS_SIZE + INF_PILLAR_OBS_SIZE + INF_TOTAL_NPC_OBS_SIZE + INF_STEP_OUT_FORECAST_OBS_SIZE + INF_PENDING_HIT_OBS_SIZE + INF_PENDING_SPARK_OBS_SIZE` (`forecast.inc:16`). Written in order by `inf_write_obs_ctx` (`obs_mask.inc:144-582`):

| segment | size | encodes |
|---|---|---|
| Player + Zuk-phase | 75 (`INF_PLAYER_OBS_SIZE`) | HP, 4 wall distances, 3 overhead-prayer one-hots, 3 offensive-prayer one-hots, brew/restore frac, prayer pts, wave frac, 6 phase one-hots, zuk attack-timer, 3 gear one-hots, bastion/stamina frac+active, potion/attack timers, defence/ranged/magic, interaction-active, weapon range, dead-mob frac, gear stats (max_hit/speed/3 defs/special), 5 prayer-threat distilled features, 17 NPC-pressure-summary features, 1 is-zuk flag + 9 zuk-only features (`obs_mask.inc:161-332`) |
| Pillars | 15 (3*5) | per pillar: active, hp frac, rel x, rel y, footprint size (`:335-341`) |
| NPC slots | 896 (`INF_BASE_NPC_OBS_SIZE`) | 37 slots in a fixed type order; per slot a variable feature count (base 11 + optional timer/style/los/scan/target-category/targeted/meleer-dig). Empty slot → zeros. Hard-asserts total == 896 (`:351-479`) |
| Step-out forecast | `25 * 8` = 200 | per move action: valid, first-attack-tick, style mask, max-hit, conflict flags. Zeroed when forecast mode off (`:481-528`) |
| Pending hits on player | 5 * 32 (`INF_FEATURES_PER_HIT * ENCOUNTER_MAX_PENDING_HITS`) | per hit: active, ranged?, magic?, timer, damage (`:530-542`) |
| Pending Zuk sparks | 7 * 32 | per spark: active, rel x/y, src rel x/y, ticks, damage (`:547-573`) |

**Computed total: `INF_NUM_OBS = 75 + 15 + 896 + 200 + 160 + 224 = 1570`**, mask `89`, so `INF_TOTAL_OBS = 1659`. (The config header comment at `osrs_inferno.ini:2` says "1290 base obs" — that is STALE; the constant arithmetic in `forecast.inc:3-16` is the source of truth, and `inf_write_obs_ctx` hard-asserts the writer matches it. Use the constants, never the comment.)

### Action space (multi-discrete, 9 heads — `player_actions.inc:2-23`)
`INF_NUM_ACTION_HEADS = 9` (`model.inc:45`). `INF_ACTION_DIMS[]` =
`{25, 6, 38, 4, 2, 4, 3, 2, 5}` summing to `INF_ACTION_MASK_SIZE = 89`:

| head | const | dim | meaning |
|---|---|---|---|
| 0 | `INF_HEAD_MOVE` | 25 (`ENCOUNTER_MOVE_ACTIONS`) | idle + 8 walk + 16 run targets |
| 1 | `INF_HEAD_PRAYER` | 6 (`ENCOUNTER_OVERHEAD_DIM_PVE_REDEMPTION`) | no_change, off, set melee/ranged/magic/redemption |
| 2 | `INF_HEAD_TARGET` | 38 (`INF_OBS_NPCS+1`) | none, or attack obs-slot 1..37 |
| 3 | `INF_HEAD_GEAR` | 4 | no_switch, mage, long_range, bp |
| 4 | `INF_HEAD_EAT` | 2 | none, brew |
| 5 | `INF_HEAD_POTION` | 4 | none, restore, bastion, stamina |
| 6 | `INF_HEAD_SPELL` | 3 | no_change, blood_barrage, ice_barrage |
| 7 | `INF_HEAD_SPEC` | 2 | no_change, toggle blowpipe spec |
| 8 | `INF_HEAD_OFFENSIVE` | 5 (`ENCOUNTER_OFFENSIVE_DIM`) | no_change, off, piety/rigour/augury |

Actions arrive as floats, cast to int in `acts_staging` (`binding.c:1249`).

### Action-mask logic (`inf_write_mask_ctx`, obs_mask.inc:628-767)
Writes 89 floats (1=valid). Head by head: MOVE — idle always valid, each
walk/run valid iff target tile in-arena and not pillar-blocked. PRAYER/OFFENSIVE —
no_change always valid; "off" valid iff a prayer active; set-X valid iff prayer
points > 0. TARGET — slot 0 (none) valid (unless forced-safe-healer mode);
slot k valid iff `inf_obs_slot_is_targetable` (alive, not shield, or phantom-barrage
candidate). GEAR — switch valid iff not already that set. EAT — brew valid iff
doses>0 + no potion timer + HP below base. POTION — restore/bastion/stamina each
gated on doses+timer+usefulness. SPELL — barrages gated on magic level. SPEC —
toggle valid iff blowpipe + enough energy. Tail of the function records
per-head valid-count diagnostics.

### Log/info fields emitted (`my_log`, binding.c:2353-2471)
`episode_return, damage_dealt, damage_received, episode_length, damage_per_100_ticks,
wins, wave, idle_ticks` (+ per-phase variants), NPC-pressure metrics, `brews_used,
prayer_correct_rate, offensive_prayer_*_rate, brews/restores_remaining,
behind_shield_pct, min_zuk_hp_seen, hp_restored, zuk_healer_damage`, the **`score`**
objective, and a `_normal` block (curriculum-excluded aggregates:
`score_normal, phase_reached_normal, frac_*`). The `Log` struct itself is shared
(`ocean/osrs/osrs_types.h`) — inferno fields are added there, not in binding.c.

---

## 5. osrs_visual.c — standalone/visual dispatch (`ocean/osrs/osrs_visual.c`, 1354 lines)

- **Compile defines**: the file compiles under two modes. Training binding includes the *encounter header* only. The standalone viewer is built by compiling `ocean/osrs_inferno/osrs_inferno.c`, which is literally:
  ```c
  #define OSRS_VISUAL
  #include "../osrs/osrs_visual.c"
  ```
  `OSRS_VISUAL` (`:31`) gates `#include "osrs_render.h"` + `puffernet.h` and all the live-window/policy/replay machinery. Without it the file builds as a headless profiler/benchmark.
- **Encounter selection is by name through the registry, NOT a compile switch.** `osrs_visual.c` `#include`s `encounter_nh_pvp.h`, `encounter_zulrah.h`, `encounter_inferno.h` (`:20-29`); each header's `__attribute__((constructor))` registers its vtable. `main()` (`:1197`) parses `--encounter <name>` (`:1210-1211`), **defaults to `"inferno"`** (`:1227`), and resolves the vtable via `encounter_find(encounter_name)` (`run_profile` `:147`, the live path `:892`).
- Per-encounter setup is a `strcmp(encounter_name, ...)` ladder: collision-map load + `world_offset` puts for `"zulrah"` / `"inferno"` (`:167-178`, `:918-941`), terrain/objects/model/anim asset loads (`:1053-1077`), then `edef->reset(state, context, seed)` and `edef->step(state, context, actions)` driven through the same vtable the binding uses.
- Policy inference (`visual_policy_*`) reads `edef->obs_size`, `edef->mask_size`, `edef->action_head_dims` generically; the only inferno special-case is `hidden_size = strcmp(name,"inferno")==0 ? 512 : 128` (`:550`).
- **For Colosseum**: add `#include "encounters/encounter_colosseum.h"` near `:26`, add a `"colosseum"` arm to each `strcmp` ladder (asset paths, world offsets), and the `hidden_size` line. The viewer is then driven by `osrs_colosseum --encounter colosseum` (or default if you change `:1227`).

---

## 6. config/ocean/osrs_inferno.ini (396 lines)

Sections (env discovery is purely by the `osrs_<name>.ini` filename in `config/ocean/`):
- **`[base]`** (`:3-7`): `env_name = osrs_inferno`, `policy_name = MinGRU`, `rnn_name = Recurrent`, `score_metric = score`.
- **`[env]`** (`:9-85`): every key here is forwarded to `ENCOUNTER_INFERNO.put_int/put_float` by `my_init`. Inferno knobs: `start_wave`, reward coeffs (`damage_reward_coeff`, `tag_reward_coeff`, `shield_penalty_coeff`, `jad/zuk_healer/set_*_reward_coeff`, `*_kill_bonus`, `post_jad_zuk_multiplier`, etc.), curriculum (`curriculum_wave_1..8`, `curriculum_frac_1..8`, supply jitter), `loadout_profile_mode`, **macro knobs `obs_profile`/`reward_profile`** (binding-side presets `:1873-1938`), `mask_in_obs=1.0`, `step_out_forecast_obs_mode`, trace dirs, `oracle_mode`. The default config starts at wave 1 with a curriculum mix (e.g. 27.5% at wave 54, fractions at 67/69/70/71).
- **`[vec]`** (`:87-90`): `total_agents=4096`, `num_buffers=2`, `num_threads=16`.
- **`[policy]`** (`:92-95`): `hidden_size=512`, `num_layers=2`, `expansion_factor=1`.
- **`[train]`** (`:97-124`): PPO/V-trace hyperparams — `total_timesteps=138326477`, `horizon=16`, `learning_rate`, `gamma=0.9985`, `gae_lambda`, `vtrace_*`, `prio_*`, `replay_ratio=4.0`, `minibatch_size=4096`.
- **`[sweep]`** (`:126-138`): Protein, `metric=score`, `goal=maximize`, a long `sweep_only` list.
- **`[sweep.*.<param>]`** (`:140-397`): per-param distributions/ranges.

---

## 7. CHECKLIST — Adding a new encounter (Fortis Colosseum)

Replace `<name>` with `colosseum`, `<NAME>`/prefix with `COL`/`col`. Every step
mirrors an inferno line reference.

### A. New files to CREATE

1. **`ocean/osrs/encounters/encounter_colosseum.h`** — mirror `encounter_inferno.h:1-54`: includes of the shared `osrs_*.h` stack, then `#include "colosseum/encounter_colosseum_*.inc"` in the **same dependency order** (model → helpers → reset_spawn → movement → combat → player_actions → reward_step → [forecast] → [lab] → obs_mask → render_snapshot). Also define the NPC type enum, `InfOutcome`-equivalent, `INF_NPC_DEF_IDS`/`INF_NPC_TO_MON`/`INF_NPC_OVERLAY` analogs here (mirror `encounter_inferno.h:48-241`).

2. **`ocean/osrs/encounters/colosseum/encounter_colosseum_model.inc`** — arena bounds, wave count/defs, `ColState` (mirror `InfernoState` `model.inc:833-1025`), `ColNPC` (mirror `InfNPC` `:404-465`), `ColConfig`/`ColContext`/`ColTickScratch`, action-head count, `COL_MAX_NPCS`/`COL_OBS_NPCS`. **Biggest authoring effort.**

3. **`encounter_colosseum_helpers.inc`** — context lifecycle (`col_default_config`, `col_init_context`, `col_legacy_context`, `col_init_state`, `col_create/destroy`) mirroring `helpers.inc:891-992`, plus LOS/threat/pressure/projectile-timing primitives you keep.

4. **`encounter_colosseum_reset_spawn.inc`** — `col_reset_ctx` (mirror `reset_spawn.inc:275`) + `col_spawn_wave` (mirror `:610`) + collision/NPC-slot helpers.

5. **`encounter_colosseum_movement.inc`** — arena/blocker predicates + `col_npc_move_ctx` (mirror `movement.inc:150`). Reuse shared `encounter_npc_step_toward` / pathfinding.

6. **`encounter_colosseum_combat.inc`** — `col_npc_attack_ctx`, player-attack resolution, boss tick, `col_tick_npcs_ctx` (mirror `combat.inc:497/1308`).

7. **`encounter_colosseum_player_actions.inc`** — **action head layout** (`INF_HEAD_*` / `INF_ACTION_DIMS_INIT` / `INF_ACTION_MASK_SIZE` / `inf_action_head_mask_offset`, mirror `player_actions.inc:2-31`) + `col_player_pretick` (`:379`) + `col_tick_player_ctx` (`:493`) + projectile resolvers.

8. **`encounter_colosseum_reward_step.inc`** — `col_compute_reward_ctx` (`reward_step.inc:156`) + **`col_step_ctx`** the tick orchestrator (`:494`). Keep the exact 18-step ordering.

9. **`encounter_colosseum_obs_mask.inc`** — `col_refresh_current_obs_slots_ctx` (`obs_mask.inc:89`), **`col_write_obs_ctx`** (`:144`, with running-index asserts), **`col_write_mask_ctx`** (`:628`), `col_fill_render_entities_ctx` (`:820`).

10. **`encounter_colosseum_render_snapshot.inc`** — **`col_put_int_ctx`/`col_put_float_ctx`/`col_put_ptr_ctx`** (the kwargs contract, mirror `render_snapshot.inc:12/106/206`), get_tick/winner/log, snapshot/restore (`:1010-1182`), `col_refresh_after_state_load` (`:1091`), and **the `static const EncounterDef ENCOUNTER_COLOSSEUM` vtable + `__attribute__((constructor)) col_register()`** (mirror `:1184-1243`). Put obs-size constants (`COL_PLAYER_OBS_SIZE`, `COL_NUM_OBS`, etc.) here or in a forecast.inc analog (inferno puts them in `forecast.inc:3-16`).

11. *(optional)* `encounter_colosseum_forecast.inc` and `_lab.inc` — only if you want step-out lookahead obs or a dev scenario harness. **Skip for v1** (ship forecast mode off, smaller obs).

12. **`ocean/osrs_colosseum/osrs_colosseum.c`** — exactly 2 lines, mirror `ocean/osrs_inferno/osrs_inferno.c`:
    ```c
    #define OSRS_VISUAL
    #include "../osrs/osrs_visual.c"
    ```

13. **`ocean/osrs_colosseum/binding.c`** — mirror `ocean/osrs_inferno/binding.c`. Required pieces: includes + the `OBS_SIZE/NUM_ATNS/ACT_SIZES/OBS_TENSOR_T/Env` defines (`binding.c:140-209`), the `ColosseumEnv` struct (`:142-198`), `my_init` (`:1940`), `my_vec_init` (`:2218`, curriculum), `c_reset`/`c_step`/`c_close`/`c_render` (`:1710/1208/1732/1755`), `my_log` with the **`score`** computation (`:2353`), and the `typedef ColState State; ... #define MY_VEC_INIT #include "vecenv.h"` tail (`:1845-1852`). Drop the inferno-specific trace machinery (post-240/stall traces) and replay-best unless wanted. This is inferno's largest file (~2471 lines) mostly because of telemetry — a lean Colosseum binding can be a few hundred lines.

14. **`ocean/osrs_colosseum/cpu_stubs/cuda_runtime_api.h`** — copy `ocean/osrs_inferno/cpu_stubs/cuda_runtime_api.h` only if you intend to build with `--cpu` (build.sh `:151` includes `$SRC_DIR/cpu_stubs` for the cpu mode).

15. **`config/ocean/osrs_colosseum.ini`** — mirror `config/ocean/osrs_inferno.ini`. `[base] env_name = osrs_colosseum`, `policy_name = MinGRU`, `score_metric = score`. `[env]` with Colosseum's own reward knobs (per-wave/per-NPC damage + kill bonuses + boss-phase bonuses, replacing jad/zuk/shield terms), `mask_in_obs=1.0`, curriculum waves matching 12-wave structure. `[vec]`/`[policy]`/`[train]`/`[sweep]` copied and retuned.

### B. Existing files to EDIT

16. **`ocean/osrs/osrs_visual.c`** — (a) add `#include "encounters/encounter_colosseum.h"` near `:26` (inside the same `-Wunused-function` pragma block); (b) add a `"colosseum"` arm to each `strcmp(encounter_name, ...)` ladder: the headless setup in `run_profile` (`:167`), the live setup (`:931`), the asset loads (`:1053`); (c) add `colosseum` to the `hidden_size` line (`:550`) if its policy width differs; (d) optionally change the default at `:1227` if you want `colosseum` to be the default `--encounter`.

17. **`build.sh`** — (a) `ocean/$ENV` auto-detection at `:140-141` already finds `ocean/osrs_colosseum`, so the basic build works with no edit; BUT (b) generalize the two `osrs_inferno` special-cases: the asset setup `if [ "$ENV" = "osrs_inferno" ]; then bash ocean/osrs/scripts/setup-data.sh; fi` (`:146-148`) and the `-Xcompiler=-fpermissive` host flag (`:156-158`). Change both `[ "$ENV" = "osrs_inferno" ]` to a pattern like `case "$ENV" in osrs_*)` so Colosseum gets the same data download + permissive C++ compile. Build commands: `./build.sh osrs_colosseum` (CUDA training `.so`), `--cpu` (CPU `.so`), `--local`/`--fast` (standalone viewer binary).

18. **`ocean/osrs/osrs_types.h`** — add Colosseum-specific fields to the shared `Log` struct if you emit new per-encounter metrics (inferno's `count_zuk_healers_*`, `min_zuk_hp_*`, etc. live here). The shared `Player`, `OsrsInteraction`, `EncounterPendingHitQueue`, `EncounterLoadoutStats`, `CollisionMap` types are reused as-is.

19. **`ocean/osrs/asset_manifest.json`** (+ run `ocean/osrs/scripts/setup-data.sh`) — add Colosseum's terrain/objects/models/anims/cmap assets so the manifest's required-asset check passes and the viewer can load them. Reference the inferno asset names in `osrs_visual.c:1053-1068` and `binding.c:1775-1782` for the naming pattern (`<name>.terrain/.objects/.models/.anims/.cmap`).

20. **Registry/enum**: no central registry edit needed — `encounter_register()` is called by each encounter's `__attribute__((constructor))` (`render_snapshot.inc:1241`) into the header-static `g_encounter_registry` (`osrs_encounter.h:2328`). Just including `encounter_colosseum.h` from `osrs_visual.c` / `binding.c` registers it. `MAX_ENCOUNTERS=32` (`osrs_encounter.h:2319`) has headroom.

### C. The vtable contract you MUST fill (`EncounterDef`, osrs_encounter.h:2183-2287)
Required for a buildable trainable env: `name, obs_size, num_action_heads,
action_head_dims, mask_size, state_size, context_size, init_context,
destroy_context, init_state, create, destroy, reset, step, write_obs, write_mask,
get_reward, is_terminal, put_int, put_float, get_tick, get_winner`, plus the arena
bounds and `head_move/head_prayer/head_target`. Optional (NULL ok):
`step_human_commands, snapshot*, cell_key*, progress_score, get_entity*,
fill_render_entities, render_post_tick, translate_human_input, get_log, put_ptr`.
Snapshot/restore are needed only for archive-based exploration; render hooks only
for the viewer.
