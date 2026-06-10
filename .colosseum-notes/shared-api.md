# Shared OSRS C header reuse map for Fortis Colosseum

Read-only catalog of the shared `ocean/osrs/osrs_*.h` headers, grouped by capability.
The thesis: a new PvM encounter (Colosseum) implements one `EncounterDef` vtable and
its own state struct, then calls the shared pure-math + scaffold functions below
instead of writing new combat, movement, pathfinding, gear, or visual code.

All paths are relative to `ocean/osrs/`. Line numbers are from the
`osrs-colosseum` worktree checkout (HEAD at audit time).

IMPORTANT scope note: the worktree checkout is missing `ocean/osrs/data/` and
`ocean/osrs/data/npc_models.h` (the `data/` dir is gitignored for the binary asset
blobs, but `npc_models.h` IS tracked and present in the main worktree). Read it from
the main repo: `/Users/valtterivalo/Projects/pufferlib-metal/ocean/osrs/data/npc_models.h`.

---

## 0. The contract: what Colosseum actually has to write

`EncounterDef` (osrs_encounter.h:2183-2287) is the vtable every encounter fills.
The env (`OsrsEnv` in osrs_types.h:1166) dispatches reset/step/obs/reward/render
through this struct. Colosseum supplies:

- `EncounterState` / `EncounterContext` opaque structs (you define the layout) +
  `state_size`/`context_size` so the runtime can `calloc` them.
- Lifecycle: `init_context`, `init_state`, `reset(state, ctx, seed)`,
  `step(state, ctx, actions)`.
- RL interface: `write_obs`, `write_mask`, `get_reward`, `is_terminal`,
  `obs_size`, `num_action_heads`, `action_head_dims`, `mask_size`.
- Rendering: `get_entity_count`, `fill_render_entities` (writes `RenderEntity[]`),
  `render_post_tick` (writes `EncounterOverlay`), arena bounds.
- Optional but valuable for Go-Explore / archive training:
  `snapshot_size`/`snapshot`/`restore` and `cell_key_size`/`write_cell_key`/`progress_score`.
- Optional human mode: `translate_human_input`, `step_human_commands`,
  `is_human_targetable_npc_slot`, plus `head_move`/`head_prayer`/`head_target` indices.

Register with `encounter_register(&COLOSSEUM_DEF)` (osrs_encounter.h:2330); the env
finds it via `encounter_find("colosseum")` (osrs_encounter.h:2338).

Concrete reference wiring: `encounter_inferno.h` is the canonical multi-NPC PvM
encounter. It splits into `encounters/inferno/*.inc` (model, reset_spawn, movement,
combat, player_actions, reward_step, forecast, obs_mask, render_snapshot). Copy its
include block and `.inc` partition shape.

---

## 1. Combat math (accuracy, hit roll, max hit, prayer/style, defence, damage)

Pure, stateless, zero inferno coupling. **Reuse wholesale.**

### Accuracy + hit rolls — osrs_combat.h
- `osrs_hit_chance(att_roll, def_roll)` — osrs_combat.h:58. Standard accuracy float.
- `osrs_hit_chance_double(att_roll, def_roll)` — osrs_combat.h:641. Osmumten/confliction double roll.
- `osrs_hit_chance_fraction(att, def, *num, *den)` — osrs_combat.h:125. Exact rational form.
- `encounter_roll_hit_chance(*rng, att, def)` — osrs_combat.h:140. **The main per-attack accuracy roll** (u16 ratio, deterministic).
- `encounter_roll_hit_chance_double(*rng, att, def)` — osrs_combat.h:169. Double-roll variant.
- `encounter_roll_ratio_u16(*rng, num, den)` — osrs_combat.h:114. Generic probability roll.

### RNG (use these, never inline xorshift) — osrs_combat.h
- `encounter_xorshift(*state)` — osrs_combat.h:98.
- `encounter_rand_int(*state, max)` — osrs_combat.h:105. Damage rolls: `encounter_rand_int(rng, max_hit + 1)`.
- `encounter_rand_float(*state)` — osrs_combat.h:110.
- `encounter_shuffle(arr, n, *rng)` — osrs_combat.h:523. Fisher-Yates for spawn placement.
- `encounter_resolve_seed(saved_rng, explicit_seed)` — osrs_encounter.h:1562. Reset seed priority. NOTE default seed is hardcoded 12345; per-env seeding lives in the env wrapper (see MEMORY: inferno RNG overfitting).

### Player offensive rolls — osrs_combat.h
- `osrs_player_eff_level(base, prayer_mult, style_bonus)` — osrs_combat.h:538.
- `osrs_player_att_roll(eff, bonus)` — osrs_combat.h:598.
- `osrs_player_melee_max_hit(eff_str, str_bonus)` — osrs_combat.h:604.
- `osrs_player_ranged_max_hit(eff_range, ranged_str_bonus)` — osrs_combat.h:610.
- `osrs_player_magic_max_hit(spell_base_dmg, magic_dmg_pct)` — osrs_combat.h:618.

### NPC offensive rolls (the Colosseum mobs attacking the player) — osrs_combat.h
- `osrs_npc_attack_roll(att_level, att_bonus)` — osrs_combat.h:290.
- `osrs_npc_melee_max_hit(str, melee_str_bonus)` — osrs_combat.h:273.
- `osrs_npc_ranged_max_hit(range, ranged_str_bonus)` — osrs_combat.h:278.
- `osrs_npc_magic_max_hit(base_spell_dmg, magic_dmg_pct)` — osrs_combat.h:284.
- `osrs_npc_max_hit(style, str, range, melee_str, ranged_str, mag_base, mag_pct)` — osrs_combat.h:330. Style dispatcher.
- `encounter_npc_roll_attack(att_roll, def_roll, max_hit, *rng)` — osrs_combat.h:347. **Accuracy + damage in one call** (returns damage, 0 on miss). This is the NPC-attacks-player primitive.

### Defence rolls + prayer + style — osrs_combat.h
- `osrs_player_def_roll_vs_npc(def, magic, bonus, attack_style)` — osrs_combat.h:301. Player defence roll vs an NPC attack (handles magic 0.7/0.3 split).
- `encounter_player_def_bonus(stab,slash,crush,magic,ranged, attack_style, melee_style)` — osrs_combat.h:316. Picks the right defensive bonus for the incoming style.
- `encounter_prayer_correct_for_style(prayer, attack_style)` — osrs_combat.h:361. **Overhead prayer match check** (the protect-from-X test).
- Stance modifiers `osrs_stance_att_bonus`/`_str_bonus`/`_def_bonus`/`_speed_mod`/`_range_mod`(FightStyle) — osrs_combat.h:555-593.

### TBow + AoE — osrs_combat.h
- `osrs_tbow_acc_mult(target_magic)` / `osrs_tbow_dmg_mult(target_magic)` — osrs_combat.h:70/85.
- `osrs_barrage_resolve(targets, n, att_roll, max_hit, *rng, spell_type, double_acc)` — osrs_combat.h:219. 3x3 AoE with independent rolls + ice-freeze; `BarrageTarget` (osrs_combat.h:185) / `BarrageResult` (osrs_combat.h:198).

### Damage application + mitigation — osrs_damage.h / osrs_encounter.h
- `osrs_prayer_reduce_damage(dmg, prayer, style, is_pvp)` — osrs_combat.h:627. PvE 100% block vs PvP 40%. **Colosseum is PvE → pass is_pvp=0.**
- `osrs_apply_damage_pipeline(raw, style, target_prayer, is_pvp, veng, recoil, smite)` — osrs_damage.h:86. Full prayer→veng→recoil→smite chain (pure). `DamageResult` at osrs_damage.h:34.
- `osrs_has_recoil_ring(equipped)` — osrs_damage.h:111.
- `encounter_damage_player(Player*, dmg, *tracker)` — osrs_encounter.h:1431. Apply dmg to player (clamps HP, sets splat flag, accumulates tracker). Inferno calls this directly (combat.inc:1237).
- `encounter_damage_npc(*hp, *hit_landed, *hit_damage, dmg)` — osrs_encounter.h:1448. Apply dmg to any NPC via field pointers; returns capped damage.

### Equipment bonus summation — osrs_combat.h
- `EquipmentBonuses` struct — osrs_combat.h:657.
- `osrs_sum_equipment_bonuses(loadout[NUM_GEAR_SLOTS], *out)` — osrs_combat.h:664. Sums all gear stats from `ITEM_DATABASE`; weapon slot drives speed/range.

---

## 2. NPC representation + spawning a new NPC type + generic AI/stepping

### Stat table model — osrs_monsters_generated.h
- `MonsterStats` struct — osrs_monsters_generated.h:37. Fields: hp, att/str/def/magic/range levels, attack_speed, size, max_hit, 6 offensive bonuses, 5 defensive bonuses.
- `MONSTER_DATABASE[NUM_MONSTERS]` — osrs_monsters_generated.h:64. Designated-initializer table indexed by `MonsterIndex`.
- `MonsterIndex` enum — osrs_monsters_generated.h:14. **This enum is content-specific** (only inferno + Zulrah mobs today). To add Colosseum mobs (Sol Heredit, Serpent Shaman, Javelin Colossus, Manticore, Minotaur, Fremennik warband, etc.) you edit `tools/monsters.json` and regenerate via `python ocean/osrs/tools/generate_monsters.py`. The struct + table pattern is the reuse; the enum values are not.

**How to define+spawn a new NPC type:** there is no shared "Npc" entity struct.
Encounters represent NPCs as `Player` structs with `entity_type = ENTITY_NPC` and the
NPC fields populated (`npc_def_id`, `npc_size`, `npc_anim_id`, HP from `MonsterStats`).
Inferno keeps its own NPC array inside `EncounterState`. So: add rows to
`MONSTER_DATABASE`, then in your `reset`/spawn code copy stats from
`MONSTER_DATABASE[MON_*]` into per-slot `Player`-shaped state. The `MonsterStats`
fields feed directly into the section-1 NPC combat functions.

### Render/model mapping — data/npc_models.h (main worktree)
- `NpcModelMapping` struct {npc_id, model_id, idle_anim, attack_anim, walk_anim} — npc_models.h:13.
- `npc_model_lookup(npc_id)` — npc_models.h:121. Generic linear lookup over per-encounter tables. Colosseum regenerates this with its own NPC model/anim IDs (generator: `scripts/export_inferno_npcs.py`, despite the name).

### Generic NPC AI / stepping helpers — osrs_encounter.h
There is **no shared "NPC brain"** (attack-selection / phase logic is per-encounter
policy and belongs in your `.inc`). But the *movement* of NPCs is fully shared and
size-aware:
- `encounter_npc_step_toward(*x,*y, tx,ty, npc_size, target_size, stop_at_melee, is_blocked, ctx)` — osrs_encounter.h:1390. **OSRS-accurate size-aware chase step** (diagonal-first, x-then-y fallback, edge-tile sweep for multi-tile NPCs). Inferno calls it at movement.inc:208.
- `encounter_npc_step_out_from_under(*x,*y,size, px,py, is_blocked, ctx, hold_overlap, *rng)` — osrs_encounter.h:1261. OSRS "shuffle off the player tile" rule. Returns MOVED/HELD/NONE (osrs_encounter.h:1252-1254).
- `encounter_npc_try_step` (osrs_encounter.h:1352), `encounter_npc_x_edge_clear`/`_y_edge_clear` (1310/1323), `encounter_npc_axis_gap`/`_axis_dir` (1336/1344) — the lower-level multi-tile movement primitives.
- Callback types: `encounter_npc_blocked_fn(ctx,x,y,size)` (osrs_encounter.h:1249), `encounter_npc_overlap_hold_fn` (1250).

### Pending-hit queue (NPC attack → delayed damage on player) — osrs_encounter.h
- `EncounterPendingHit` (osrs_encounter.h:119) / `EncounterPendingHitQueue` (136, cap `ENCOUNTER_MAX_PENDING_HITS`=32).
- `encounter_pending_hit_queue_clear/_push/_remove/_earliest/_damage_sum` — osrs_encounter.h:141-216.
- `encounter_resolve_player_pending_hits(queue, Player*, active_prayer, *dmg_acc, *prayer_correct, *off_prayer)` — osrs_encounter.h:1496. **Ticks down NPC attacks, applies damage at land, handles jad-style deferred prayer checks.** Call every tick.
- `encounter_resolve_npc_pending_hit(ph, *npc_hp, *hit_landed, *hit_damage, *frozen, *blood_heal, *dmg_acc)` — osrs_encounter.h:1468. Player attack landing on an NPC (handles ice freeze / blood heal).
- The `prayer_check_delay` field (osrs_encounter.h:125) models jad's T+3 prayer lock — directly reusable for Colosseum's Jad-like delayed mechanics.

---

## 3. Movement + pathfinding (player + multi-tile NPC, LoS, collision)

### Player movement intent — osrs_encounter.h
- `ENCOUNTER_MOVE_ACTIONS` = 25, `ENCOUNTER_MOVE_TARGET_DX/DY[25]` — osrs_encounter.h:651-671. The idle+8walk+16run delta grid (this is the `HEAD_MOVE` action space).
- `encounter_move_to_target(Player*, dx, dy, is_walkable, ctx)` — osrs_encounter.h:681. Greedy 1-or-2-tile step; sets `is_running`.
- `encounter_move_toward_dest(Player*, *dest_x, *dest_y, cmap, off_x, off_y, is_walkable, ctx, extra_blocked, blk_ctx, arena…)` — osrs_encounter.h:753. BFS click-to-walk toward a stored destination.
- `encounter_walkable_fn(ctx,x,y)` callback type — osrs_encounter.h:675.

### BFS pathfinding — osrs_pathfinding.h + osrs_encounter.h wrappers
- `PathResult` {found, next_dx, next_dy, dest_x, dest_y} — osrs_pathfinding.h:53.
- `pathfind_step(map, height, sx,sy, dx,dy, extra_blocked, ctx)` — osrs_pathfinding.h:96. 104x104 BFS with closest-reachable fallback.
- `pathfind_step_arena(... , origin_x, origin_y, w, h)` — osrs_pathfinding.h:346. **Smaller arena grid (≤48), thread-local gen-counter, no per-call memset — use this for a bounded Colosseum arena** (perf-critical; the 104x104 memset was a documented hot path).
- `encounter_pathfind(...)` (osrs_encounter.h:721) / `encounter_pathfind_arena(...)` (osrs_encounter.h:735) — local→world coordinate wrappers.
- `encounter_pathfind_arena_attack_approach(...)` — osrs_encounter.h:881. BFS that paths to the nearest tile from which the player *can attack* the target (range + LoS aware). Powers auto-walk-into-melee.
- `pathfind_blocked_fn(ctx, abs_x, abs_y)` callback — osrs_pathfinding.h:81.

### Distance + range + LoS — osrs_combat.h / osrs_collision.h / osrs_encounter.h
- `chebyshev_distance` (osrs_types.h:1257), `is_in_melee_range(Player*,Player*)` cardinal-only (osrs_types.h:1276).
- `encounter_rect_distance(ax,ay,asz, bx,by,bsz)` (osrs_combat.h:434), `encounter_dist_to_npc(px,py, nx,ny, npc_size)` (osrs_combat.h:517), `encounter_entity_footprint_distance(...)` (osrs_encounter.h:789), `encounter_entity_footprints_overlap(...)` (osrs_encounter.h:809).
- `encounter_player_can_attack(px,py, tx,ty,tsize, range, los_blockers, count)` — osrs_encounter.h:820. **In-range AND LoS check** — the single gate for "can I attack now".
- LoS: `entity_has_line_of_sight(blockers,count, ax,ay,asz, tx,ty,tsz, range)` — osrs_collision.h:506; `has_line_of_sight(...)` Q16 ray trace (osrs_collision.h:421); `npc_has_line_of_sight(...)` (osrs_collision.h:548). `LOSBlocker` struct (osrs_collision.h:394) for pillars/walls.

### Collision map — osrs_collision.h
- `CollisionMap` / `CollisionRegion` (osrs_collision.h:52-66), `collision_map_create/_free/_load/_save` (osrs_collision.h:90/143/308/361). NULL map = flat open arena (everything traversable).
- `collision_mark_blocked` (186), `collision_mark_occupant(... width,length, impenetrable)` (191) — for placing Colosseum walls/pillars/spawn-blockers.
- `collision_traversable_{north,south,east,west,…diagonals}` (osrs_collision.h:207-272), `collision_traversable_step(map,h,x,y,dx,dy)` (287), `collision_tile_walkable` (278).

### Shared player step orchestrator — osrs_encounter_player.h
**This is the single biggest reuse for player-side input handling.** It folds
target interaction + explicit move + chase-into-attack-range into one call.
- `OsrsEncounterArena` (osrs_encounter_player.h:23) — bundles collision map, world offset, walkable/blocked callbacks, LoS blockers, arena bounds.
- `OsrsAttackTarget` (osrs_encounter_player.h:39) + `OsrsAttackTargetLookupFn` (47) — your callback returns an NPC's {x,y,size,attack_range} by slot.
- `OsrsPlayerStepInput` (osrs_encounter_player.h:52) / `OsrsPlayerStepResult` (68).
- `osrs_encounter_player_step(*input)` — osrs_encounter_player.h:173. Handles new-target acquisition, move interrupts, explicit move vs chase, and sets `can_attack`. `OsrsPlayerMoveKind` {NONE, ACTION, DESTINATION} (osrs_encounter_player.h:12). Inferno calls it at player_actions.inc:733.

---

## 4. Projectiles + forecast

### Hit-delay math — osrs_combat.h
- `encounter_magic_hit_delay(dist, is_player)` (osrs_combat.h:369), `encounter_ranged_hit_delay` (374), `encounter_thrown_hit_delay`/`_blowpipe` (380/384), `encounter_ballista_hit_delay` (388), `encounter_dark_bow_second_hit_delay` (392), `encounter_eye_of_ayak_hit_delay` (396).
- Unified API: `EncounterProjectileDelayKind` enum (osrs_combat.h:405), `EncounterProjectileDelayOptions` (415, setDelay/reduceDelay/visual), `encounter_projectile_hit_delay(dist, is_player, kind, opts)` (487), `encounter_projectile_timing(...)` → `EncounterProjectileTiming` {damage_delay, visual_start_delay, visual_duration} (osrs_combat.h:501).
- `encounter_projectile_distance(sx,sy,ssz, tx,ty,tsz, mode)` (osrs_combat.h:450), `EncounterProjectileDistanceMode` (400).

### Projectile orientation/arc (visual only) — osrs_projectile_orientation.h
- `OsrsProjectileOrientation` {yaw, pitch} (osrs_projectile_orientation.h:12).
- `osrs_projectile_height_at_progress(...)` (23), `osrs_projectile_orientation_from_step(dx,dy,dh)` (47), subtile/anchor converters (39/43). Pure trig; only needed by the visual binary.

### Forecast
There is **no shared forecast module**. Inferno's step-out forecast is encounter-local
(`encounters/inferno/encounter_inferno_forecast.inc`, the documented env-step hot path).
It is built from the shared movement/combat primitives above (e.g. it calls
`encounter_npc_step_toward` at forecast.inc:795). Colosseum builds its own forecast the
same way if it needs one — reuse the primitives, not a shared forecast.

---

## 5. Consumables / prayer / gear / special attacks

### Consumables (pure) — osrs_consumables.h
- `FoodType` (osrs_consumables.h:24), `PotionType` (34). Results: `EatResult` (46), `DrinkResult` (52), `BrewResult` (61).
- `osrs_food_heal_amount(type)` (71), `osrs_eat_food(type, hp, max, timer)` (96), `osrs_drink_potion(type, cur_prayer, prayer_lvl, timer)` (125), `osrs_brew_effect(base_hp,att,str,range,magic)` (171).
- `osrs_can_eat(timer)` (82) / `osrs_can_drink(timer)` (83), `osrs_imbued_heart_magic_boost` (85) / `osrs_saturated_heart_magic_boost` (89).

### Consumables bound to a Player (state transitions) — osrs_player_consumables.h
- `OsrsPlayerEatResult` (osrs_player_consumables.h:15).
- `osrs_player_can_eat_food_type(Player*, FoodType)` (45), `osrs_player_eat_food_type(Player*, FoodType)` (61) — **mutates the Player**: decrements count, sets food/karambwan/potion timers + attack delay, clamps HP. This is the full eat transition; reuse directly.
- `osrs_player_food_timer`/`_food_count`/`_food_wasted_hp` (35/40/53).

### Prayer (set/refresh + drain) — osrs_encounter.h
- Overhead action encoding `ENCOUNTER_OVERHEAD_*` + dims (DIM_PVE=5, DIM_PVE_REDEMPTION=6, DIM_PVP=7) — osrs_encounter.h:590-599.
- Offensive encoding `ENCOUNTER_OFFENSIVE_*` (DIM=5) — osrs_encounter.h:602-607.
- `encounter_apply_overhead_action(*overhead, action)` (612) / `encounter_apply_offensive_action(*offensive, action)` (632) — return 1 on OFF→ON activation (for the activation-tick drain skip).
- `encounter_drain_all_prayers(Player*, prayer_bonus)` (osrs_encounter.h:1619) — drains both slots per tick with activation-tick skip + pp=0 auto-clear. `encounter_player_prayer_bonus(Player*)` (1608), drain-rate helpers `encounter_overhead_drain_effect`/`_offensive_drain_effect` (1584/1596).
- `OverheadPrayer` (osrs_types.h:168) / `OffensivePrayer` (osrs_types.h:185) enums.
- Colosseum prayer wiring (4 steps): declare two heads (overhead dim 5/6, offensive dim 5) → call apply_overhead + apply_offensive on pretick → call drain_all_prayers on pretick. Documented at osrs_encounter.h:579-585.

### Gear loadout + derived combat stats — osrs_encounter.h
- `EncounterLoadoutStats` (osrs_encounter.h:1676) — attack_bonus, strength_bonus, eff_level, max_hit, attack_speed, attack_range, def_*, prayer mults.
- `encounter_compute_loadout_stats(loadout, style, offensive_prayer, base_level, fight_style, spell_base_dmg, *out)` — osrs_encounter.h:1735. **Derive a full stat block from a gear array.** Inferno builds its mage/longrange/blowpipe loadouts this way (reset_spawn.inc:380-384).
- `encounter_update_loadout_level(...)` (1830), `encounter_recompute_loadout_max_hits(...)` (2021), `encounter_compute_player_equipped_stats(...)` (1853) — recompute after brew drain / potion boost.
- Stat-boost transitions on Player: `encounter_apply_saturated_heart_boost` (1909), `encounter_tick_saturated_heart` (1917), `encounter_decay_player_combat_stats_toward_base` (1944), `encounter_brew_drain_stats` (1960), `encounter_restore_stats` (1984), `encounter_bastion_boost` (2005).
- `encounter_offensive_prayer_mults(op, *att, *str)` (1698) / `encounter_offensive_magic_dmg_mult(op)` (1716) — the piety/rigour/augury multipliers (single source of truth).
- `encounter_apply_loadout(...)` (osrs_encounter.h:2057) — memcpy a loadout onto the player + set gear state.
- `encounter_init_maxed_player_combat_stats(Player*)` (1888) — set 99s.

### Item database + inventory — osrs_items.h / osrs_inventory.h
- `Item` struct (osrs_items.h:48), `ITEM_DATABASE`/`NUM_ITEMS` (via osrs_items_generated.h), `get_item(idx)` (127), `ITEM_*` index constants. `EquipmentSlot` enum (osrs_items.h:18), `OsrsItemEffectMask` (33).
- `get_item_attack_style(idx)` (149), `item_is_weapon` (137), `item_is_shield` (143), `item_is_two_handed` (194), `get_item_stats_normalized(idx, out[18])` (239) for obs.
- Full 28-slot bag: `OsrsInventory` (osrs_inventory.h:24), `osrs_inventory_init/_count/_free_slots/_find/_add/_remove` (31-84), `osrs_item_gear_slot` (89), `osrs_equip_direct` (111), `osrs_equip_from_inventory` (125, handles 2H→shield), `osrs_unequip_to_inventory` (174).
- NOTE: `ITEMS_BY_SLOT` / `NUM_ITEMS_IN_SLOT` (osrs_items.h:79/112) are an **LMS/PvP-curated gear table** (the per-slot option lists are tuned for NH PvP). Colosseum either reuses the `Item`/`ITEM_DATABASE` layer directly with a fixed loadout, or defines its own per-slot option lists. The `Item` struct + DB are generic; the slot-option lists are PvP content.

### Special attacks (pure) — osrs_special_attacks.h
- `SpecResult` (osrs_special_attacks.h:47), `osrs_spec_cost(weapon_idx)` (61), `osrs_resolve_spec(weapon_idx, att_roll, max_hit, def_roll, target_def_level, *rng)` (91). 20+ weapons (AGS, claws, DWH, BGS, ZGS, SGS, voidwaker, blowpipe, dark bow, ZCB, ballista, volatile, eye of ayak…). `osrs_blowpipe_spec_resolve(...)` (33).
- Spec arming on Player: `encounter_use_spec(Player*, cost)` (osrs_encounter.h:2048), `encounter_tick_spec_regen(Player*)` (2042); `osrs_spec_toggle`/`_disarm` (osrs_interaction.h:74/78), `is_lightbearer_equipped` (osrs_types.h:1342).

### Interaction model — osrs_interaction.h
- `OsrsInteraction` {target_slot} (osrs_interaction.h:17), `osrs_interaction_set/_clear/_active/_init` (22-36).
- `OSRS_IACT_*` action kinds (osrs_interaction.h:39-46) + `osrs_interaction_check_interrupt(ix, action_type)` (52). Models OSRS "click-to-attack persists; ground-click/eat/equip interrupts; prayer/spec don't."

---

## 6. Shared player encounter helpers (osrs_encounter_player.h)

Covered in §3 (it is the movement+target orchestrator). Summary of the reuse: build
one `OsrsEncounterArena` describing your Colosseum arena + collision + LoS pillars,
implement an `OsrsAttackTargetLookupFn` over your NPC array, then call
`osrs_encounter_player_step` once per tick. It returns `{moved, chased_target,
interaction_active, target_slot, can_attack}`. Zero inferno coupling; this file is
the purpose-built generic player scaffold.

---

## 7. Visual / animation / render helpers an encounter emits

The training `.so` does not need any of these (headless). They matter only if you
build the Colosseum visual binary (`cd ocean/osrs && make visual`).

### Overlay the encounter populates — osrs_encounter.h
- `EncounterOverlay` (osrs_encounter.h:239) — hazards[16], boss state, adds[4], projectiles[48], melee-target, status text.
- `encounter_emit_projectile(ov, sx,sy, dx,dy, style, dmg, duration, start_h,end_h,curve, arc, tracks, ssz,dsz, model_id, impact_gfx)` — osrs_encounter.h:306. Plus `encounter_set_projectile_{source_player,source_npc_slot,target_npc_slot,motion_mode,animation,launch_gfx,offset}` (369-433). `encounter_attack_style_to_proj_style(style)` (295).

### Higher-level visual-event emitters — osrs_encounter_visual_events.h
**Cleanest reuse for rendering** — these wrap overlay/RenderEntity population:
- `OsrsNpcRenderEntitySpec` (osrs_encounter_visual_events.h:12) + `osrs_render_entity_from_npc_spec(...)` (99), `osrs_render_entity_from_npc_player(...)` (164), `osrs_render_entity_from_player_entity(...)` (179).
- NPC anim event helpers: `osrs_npc_primary_anim_event_set/_should_emit/_expire` (130/147/155), `osrs_render_entity_suppress_pose_anims` (194).
- Death linger: `osrs_npc_death_linger_start/_tick` (72/88).
- Projectile emit specs: `OsrsProjectileEventSpec` (33), `OsrsCombatProjectileEmitSpec` (54), `osrs_emit_projectile_with_spec` (209), `osrs_emit_projectile_player_to_npc`/`_npc_to_player`/`_npc_to_npc` (312/323/333), `osrs_emit_combat_projectile_profile_player_to_npc(...)` (255).

### RenderEntity + facing — osrs_encounter.h
- `RenderEntity` value struct (osrs_encounter.h:434), `render_entity_from_player(Player*, *out)` (519), `encounter_resolve_attack_target(entities, count, target_slot)` (559), `render_entity_select_facing_mode(...)` (506), `RenderEntityFacingMode` (467).

### Combat-visual lookup tables — osrs_combat_visuals.h
- `OsrsCombatProjectileProfile` (osrs_combat_visuals.h:87) — launch/travel/impact spotanim, model, heights, angle, timing.
- Lookups by id: `osrs_combat_visual_find_item_id` (465), `_find_npc_id` (504), `_find_spell` (520), `osrs_combat_projectile_profile(...)` (615), `osrs_combat_visual_weapon_attack_anim_for_fight_style(...)` (582), `osrs_combat_visual_ranged/magic_projectile_profile(...)` (660/708). `osrs_combat_projectile_value_or(value, fallback)` (204).
- `osrs_combat_visuals_generated.h` (1MB) is the generated data table backing these.

### Asset loaders (raylib, visual binary only) — generic, no encounter coupling
- `osrs_anim.h` — .anims vertex-group animation runtime (`AnimFrameBase` etc.).
- `osrs_objects.h` — `ObjectMesh`, `objects_load`/`_offset`/`_free` for placed map objects (atlas-textured).
- `osrs_spotanims.h` — `OsrsSpotAnimDef`/`OsrsSpotAnimSet`, `osrs_spotanims_load`/`osrs_spotanim_find`.
- `osrs_terrain.h`, `osrs_render*.h`, `osrs_models.h`, `osrs_gui.h`, `osrs_assets.h` — terrain mesh, scene render, model loader, GUI panels, asset-path resolution. All generic; pull in as needed for the viewer.

---

## INFERNO-SPECIFIC LEAKAGE IN SHARED HEADERS (avoid coupling Colosseum)

Flagged so you do not accidentally inherit inferno/Zuk/Zulrah structure.

1. **`Log` struct (osrs_types.h:731-1005) is ~95% inferno/Zuk/Zulrah-specific.**
   This is the single biggest leak. Hundreds of fields: `min_zuk_hp_*`,
   `count_zuk_healers_tagged_*`, `behind_shield_pct`, `zulrah_tier_*`,
   `count_died_with_jad_alive_*`, `phase2_*` self-play probe fields, `prayer_correct_by_type[14]`
   (the 14 = inferno NPC types), `zero_valid_action_head_count_normal_sum[9]`, etc.
   ~122 lines reference zuk/healer/zulrah/jad by name. Only the first ~20 fields
   (episode_return, episode_length, wins, damage_dealt/received, prayer_correct/total,
   idle_ticks, npc_kills, gear_switches) are generically useful.
   → **Action for Colosseum:** do not extend this `Log`. Define a `ColosseumLog` in your
   `EncounterState` and surface metrics via the `EncounterDef.get_log` hook. The env
   reads logs through that pointer; you are not forced into the shared `Log`.

2. **`OSRS_INFERNO_IDLE_PHASE_COUNT` = 6 (osrs_types.h:358)** and the four
   `*_by_phase[OSRS_INFERNO_IDLE_PHASE_COUNT]` arrays (osrs_types.h:750-753) — inferno
   wave-phase bucketing. Ignore.

3. **`Player` struct (osrs_types.h:474-729) carries PvP + inferno-specific fields.**
   The struct is the shared entity ABI and is fine to reuse, but large chunks are not
   Colosseum-relevant: PvP opponent-modeling (`target_*` observed-stat fields ~656-670,
   `recent_*` history buffers 642-653), morrigan DoT (`morr_dot_*` 609-611), veng,
   PvP spec-weapon enums, `gui_*` panel fields. `OsrsPvpRuntime` (osrs_types.h:1137) and
   `OpponentState`/`PFSPState`/`OpponentType` (osrs_types.h:1043-1135) are pure PvP —
   ignore them; the comment at osrs_types.h:1201 confirms "encounters that bypass the
   PvP stack can ignore this." Colosseum uses the position/HP/prayer/gear/timer/
   consumable/`item_effect_state` fields and leaves the PvP/inferno fields zero.

4. **`MonsterIndex` enum (osrs_monsters_generated.h:14)** — only inferno + Zulrah mob
   IDs. Regenerate with Colosseum mobs; do not hardcode against `MON_TZKAL_ZUK` etc.

5. **`data/npc_models.h` (main worktree)** — model/anim mappings are entirely Zulrah +
   inferno today (`NPC_MODEL_MAP_INFERNO`, `INF_GFX_*`, `INF_PILLAR_MODEL_*`,
   `SNAKELING_*`, `ZULRAH_ANIM_*`). The `NpcModelMapping` struct + `npc_model_lookup`
   pattern are reusable; the data and the `INF_*`/`ZULRAH_*` macros are not.

6. **`ITEMS_BY_SLOT` / `NUM_ITEMS_IN_SLOT` (osrs_items.h:79/112)** — NH-PvP-curated gear
   option lists (LMS supply assumptions baked into adjacent constants too, e.g.
   `MAXED_*` LMS supply counts at osrs_types.h:141-147). Reuse `Item`/`ITEM_DATABASE`;
   define your own slot-option lists if Colosseum allows gear switching.

7. **PvP-tuned macros in osrs_types.h** — `WILD_*`/`FIGHT_AREA_*` arena bounds (26-34),
   `ICE_/BLOOD_*` spell level/maxhit (42-60), `ATTACK_*`/`MOVE_*` combat-head encoding
   for the merged 13-dim PvP head (292-305). Colosseum defines its own arena bounds via
   `EncounterDef.arena_*` and its own action heads.

8. Minor reference-only mentions (not actual coupling): `osrs_damage.h:110` and
   `osrs_combat.h:522` name zulrah/snakeling in **comments** only; the code is generic.

Bottom line: the **pure-math layer** (osrs_combat.h, osrs_damage.h, osrs_consumables.h,
osrs_special_attacks.h, osrs_collision.h, osrs_pathfinding.h, osrs_projectile_orientation.h),
the **scaffold** (osrs_encounter.h functions, osrs_encounter_player.h,
osrs_encounter_visual_events.h, osrs_interaction.h, osrs_inventory.h), and the **Item /
MonsterStats struct+table patterns** are clean and reusable. The leakage is concentrated
in `Log` (osrs_types.h), the content-specific generated enums/tables, and the PvP gear/
arena/action constants.

---

## TOP ~25 REUSABLE FUNCTIONS/STRUCTS (by capability)

Combat math:
1. `encounter_npc_roll_attack` — osrs_combat.h:347 (NPC attack: accuracy+damage)
2. `osrs_player_att_roll` / `osrs_player_*_max_hit` — osrs_combat.h:598/604/610/618
3. `osrs_npc_max_hit` + `osrs_npc_attack_roll` — osrs_combat.h:330/290
4. `osrs_player_def_roll_vs_npc` + `encounter_player_def_bonus` — osrs_combat.h:301/316
5. `osrs_prayer_reduce_damage` (PvE 100% block) — osrs_combat.h:627
6. `encounter_prayer_correct_for_style` — osrs_combat.h:361
7. `encounter_roll_hit_chance` + `encounter_rand_int` — osrs_combat.h:140/105
8. `osrs_sum_equipment_bonuses` + `EquipmentBonuses` — osrs_combat.h:664/657

Damage application:
9. `encounter_damage_player` / `encounter_damage_npc` — osrs_encounter.h:1431/1448
10. `EncounterPendingHitQueue` + `encounter_resolve_player_pending_hits` — osrs_encounter.h:136/1496

NPC representation:
11. `MonsterStats` + `MONSTER_DATABASE` — osrs_monsters_generated.h:37/64
12. `encounter_npc_step_toward` (size-aware chase) — osrs_encounter.h:1390
13. `encounter_npc_step_out_from_under` — osrs_encounter.h:1261

Movement / pathfinding / LoS:
14. `osrs_encounter_player_step` + `OsrsEncounterArena` + `OsrsPlayerStepInput` — osrs_encounter_player.h:173/23/52
15. `encounter_move_to_target` + `ENCOUNTER_MOVE_TARGET_DX/DY` — osrs_encounter.h:681/654
16. `pathfind_step_arena` / `encounter_pathfind_arena_attack_approach` — osrs_pathfinding.h:346 / osrs_encounter.h:881
17. `encounter_player_can_attack` (range+LoS gate) — osrs_encounter.h:820
18. `entity_has_line_of_sight` + `LOSBlocker` + collision map — osrs_collision.h:506/394/52

Projectiles:
19. `encounter_projectile_timing` + delay-kind API — osrs_combat.h:501/487

Consumables / prayer / gear / spec:
20. `osrs_player_eat_food_type` (full eat transition) — osrs_player_consumables.h:61
21. `osrs_drink_potion` / `osrs_brew_effect` — osrs_consumables.h:125/171
22. `encounter_apply_overhead_action` + `encounter_drain_all_prayers` — osrs_encounter.h:612/1619
23. `encounter_compute_loadout_stats` + `EncounterLoadoutStats` — osrs_encounter.h:1735/1676
24. `osrs_resolve_spec` + `SpecResult` + `osrs_spec_cost` — osrs_special_attacks.h:91/47/61

Dispatch + visuals:
25. `EncounterDef` vtable + `encounter_register`/`encounter_find` — osrs_encounter.h:2183/2330/2338
    (companion: `RenderEntity` + `osrs_render_entity_from_npc_spec` + `EncounterOverlay`
     + `osrs_emit_projectile_*` — osrs_encounter.h:434/239, osrs_encounter_visual_events.h:99/312)
