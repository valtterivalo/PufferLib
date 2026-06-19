# Colosseum inventory-clicking port spec (v9 contract)

Goal: replace colosseum's 3-fixed-weapon-SET gear model with a real OSRS 28-cell
inventory the agent clicks, so gear/potion/food choices (rancour vs blood fury,
divine vs regular pots, weapon/style switches, when to brew) all become learned
decisions. Source model: current PvP v11 slot-click + item-affordance system
(valtteri/pvp-selfplay-v8). User decisions (2026-06-17):
- obs depth: LEAN colosseum-tuned (~16 features/slot), not PvP's full 56.
- click budget: NO arbitrary per-tick cap -> per-cell binary click head.
- scope: FULL kit as real items, INCLUDING consumables (dose pools -> vials);
  clicking a cell performs the item's real action (equip / drink / eat).

## Inventory model
- `Player.inventory[28]` flat (migrate off the legacy 2D `[11][10]`), `equipped[11]`,
  ITEM_NONE=255. Reuse PvP's Player-based osrs_inventory.h equip primitives
  (`osrs_player_can_equip_from_inventory_slot`, `osrs_player_equip_from_inventory_slot`:
  in-place equipped<->inventory swap, 2H vacates shield, weapon-change disarms spec,
  `slot_gear_dirty=1`).
- Consumables are real cells with per-cell dose state (a 4-dose sara brew vial is one
  cell; drinking decrements it 4->3->2->1->empty-vial). Replaces the 5 scalar dose
  pools (brew/restore/combat/ranged/surge). Combat boost-on-drink logic unchanged;
  only storage + the trigger (click a cell vs the POTION head) change.
- Seed the bag at reset from the wiki kit (COLO_INVENTORY_DISPLAY), equip the melee
  set, compute ONE live loadout. Data reconciliation (the load-bearing chore): every
  kit id -> ITEM_DATABASE. Add missing equippables (Venator bow 27610, Confliction
  gauntlets 31106, ...) and missing consumables (divine pots, sanfew, surge, guthix
  rest) as items with stats/effects/dose-profiles. Update the EXPECTED_ITEM_EFFECTS
  pin test in lockstep.

## Action contract (v9)
Remove: COLO_HEAD_GEAR (dim4), COLO_HEAD_EAT (dim2), COLO_HEAD_POTION (dim5).
Add: 28 per-cell binary "use" heads (dim2 each: noop / use-cell-i). Resolve in cell
order each tick; a cell's "use" resolves by item kind: gear -> equip-swap, potion ->
drink one dose, food -> eat. Combo-eat/drink/equip in one tick is naturally allowed.
Keep: MOVE(25), PRAYER/OVERHEAD(4), TARGET(25), SPEC(3 arm/disarm of equipped spec
weapon), MODIFIER(4), GRAPPLE(6), OFFENSIVE(5), SPELL(3).
New head count = 8 kept + 28 cell heads = 36 heads. Mask = old 86 - (4+2+5) + 28*2
= 75 + 56 = 131. (Spec stays a separate head: arming the spec bar is not an inventory
click in OSRS.)

## Obs contract (v9), lean ~16/slot
Remove from the 45-float player block: gear set one-hot (3) + loadout scalars (~8) +
supply fractions (5) + surge cd (1) + divine stat-deltas (5) + divine timers (2)
[keep special_energy, spec_armed]. ~ -24 floats.
Add per inventory cell (28 cells x ~16): present, item-index/NUM_ITEMS, gear-slot
one-hot-compressed, is-weapon, attack-style, can-use (mask mirror), is-equipped,
dose-fraction (consumables), 6 post-use stat deltas (what equipping/this would change),
key effect bits (blood-fury/tbow/fang/dharok/crystal/tumeken). ~28*16 = 448.
Add equipped-self block (11 slots x ~12): item-index, style, key stats, effect bits.
~132. (Drop PvP's target-equipped block: PvE has no opponent player.)
Net COLO_NUM_OBS ~= 1190 - 24 + 448 + 132 ~= 1746. Share the per-item feature writer +
affordance/post-use-delta projection into a new osrs_gear_obs.h used by both envs.
Only can-use / is-equipped / dose / post-use-deltas are dynamic per tick; the rest is a
static per-item template memcpy (cheap; env is forecast/obs-bound).

## Sim/combat changes
- Collapse loadout_stats[3]/set_effects[3]/spec_stats[2]/spec_effects[2] -> ONE live
  EncounterLoadoutStats + OsrsEquipmentEffectProfile recomputed on `slot_gear_dirty`
  (lazy). Rewrite the ~6 consumers (col_player_attack_target, col_player_spec_attack,
  col_player_attack_range, obs player block, mask, forecast) to read live equipped[] +
  the live loadout instead of [weapon_set].
- Spec: COLO_HEAD_SPEC arms whatever spec weapon is equipped (equip claws via a cell
  click -> arm via SPEC -> fire via attack). spec_stats derived from the equipped
  weapon, not a fixed table.

## Snapshot / forecast / curriculum
- COLO_SNAPSHOT_VERSION 8->9 (state shape + obs + action all change). col_refresh_after
  _state_load must TRUST s->player.equipped/inventory (not re-equip a static set).
- Forecast (mode 3 FAST_READONLY_MOVE) is movement-only: equip/use cell heads masked
  inert during the rollout (like other non-move heads today); flat inventory rides the
  ColosseumState memcpy for free.
- SimpleCL + reset-from-state: snapshots now carry per-cell inventory/dose state;
  verify mid-episode partially-switched state restores correctly.
- From-scratch CUDA retrain (obs + action contract change; not v8-checkpoint-compatible).

## Open micro-decisions (resolve during impl)
- Exact lean feature list per slot (lock the ~16) + equipped-self (~12).
- Dose-vial item representation (one item id per potion family + a per-cell dose byte,
  vs distinct 4/3/2/1 item ids). Per-cell dose byte is cleaner.
- Whether food/karambwan/saturated-heart all become cells (yes, per "full kit").

## Stage 3 exact contract (the break) — v9

### Cell inventory (ColosseumState)
```c
typedef struct {
    uint16_t osrs_id;   /* raw OSRS item id; 0 = empty cell */
    uint8_t  item_idx;  /* ITEM_DATABASE index for gear; ITEM_NONE for non-gear/empty */
    uint8_t  dose;      /* consumable doses remaining 1..4; 0 for gear/food/empty */
} ColoInvCell;
ColoInvCell inventory_cells[28];
```
Seed at reset from COLO_INVENTORY_DISPLAY[profile]: gear id -> {osrs_id, item_idx via raw->ITEM_DATABASE reverse lookup, dose 0}; consumable id -> {osrs_id, ITEM_NONE, dose from the SDK registry}; display-only id (rune pouch 27281) -> {osrs_id, ITEM_NONE, 0} (click = noop). Equip the melee set into player.equipped at reset (mark loadout dirty). The render bridge (col_build_live_inventory_display) now reads inventory_cells directly.

### Action heads (36): replace GEAR(4)/EAT(2)/POTION(5) with
- COLO_HEAD_INV_CLICK_0..27, each dim COLO_INV_CLICK_DIM=29 (0=noop, k=click cell k-1).
- COLO_HEAD_SPEC stays dim 3 {none, arm, disarm}: arm valid iff the EQUIPPED weapon has osrs_spec_cost>0 and energy>=cost (spec now fires off whatever spec weapon the agent equipped via a click; drop COLO_SPEC_WEAPONS from the SPEC head + drop col_compute_spec_loadout — the live loadout already reflects the equipped weapon).
- Keep MOVE(25)/PRAYER(4)/TARGET(25)/MODIFIER(4)/GRAPPLE(6)/OFFENSIVE(5)/SPELL(3).
- COLO_ACTION_DIMS order: MOVE, PRAYER, TARGET, [28x INV_CLICK], SPEC, MODIFIER, GRAPPLE, OFFENSIVE, SPELL. COLO_ACTION_MASK_SIZE = 25+4+25 + 28*29 + 3+4+6+5+3 = 887.

### Dispatch (col_tick_player_ctx), order-sensitive
After move/prayer/target: loop the 28 click heads in head order; a local clicked[28] dedupes (a cell clicked by >1 head applies once, first head wins). For each clicked cell call osrs_inventory_click_interpret(cell.item_idx, cell.osrs_id, FIRST/DUPLICATE):
- EQUIP -> col_equip_from_cell: set player.equipped[gear_slot]=item_idx, swap the displaced worn item back into THIS cell (osrs_id+item_idx), 2H weapon vacates the shield into this cell (or first empty cell), mark loadout dirty. Skip if already equipped (mask should prevent).
- DRINK -> route consumable_kind to the EXISTING colosseum boost/heal (reuse current drink logic: brew, super/divine combat, ranging/divine ranging, super restore/sanfew, surge, guthix rest, saturated heart), set potion_timer, decrement cell.dose; dose 0 -> empty cell.
- EAT -> heal (shark/karambwan via existing food logic), set food_timer, empty the cell.
- NONE -> noop.
Then SPEC arm/disarm on the equipped weapon. Forecast (mode-3 movement-only): all 28 click heads + SPEC are masked inert exactly like the other non-move heads are today.

### Obs (lean ~16/cell). Player block: DROP the 3-way weapon-set one-hot + 5 supply fractions + surge cd + divine stat-delta/timer floats; KEEP special_energy, spec_armed, and the live-loadout aggregate scalars (max_hit/attack_range/attack_speed/defences) + prayer/hp/etc. ADD:
- inventory affordance block: 28 cells x 16 floats:
  [0]present [1]is_gear [2]is_consumable [3]can_use(mask mirror) [4]is_equipped
  [5]dose_frac(dose/4) [6-8]weapon attack-style one-hot(melee/ranged/magic, else 0)
  [9-14] 6 post-USE deltas: for gear = post-equip GearBonuses deltas (d slash_att, d melee_str, d ranged_att+str, d magic, d aggregate defence) projected via the live-loadout recompute against a hypothetical equip; for consumables 0. [15] has_effect (item effect_mask != NONE).
- equipped-self block: 11 slots x 12 floats: [0]present [1-3]style one-hot [4-9] att/str/def key stats normalized [10]has_effect [11]is_weapon.
Put the per-item/affordance feature writer in the shared osrs_inventory_clicks.h (or a sibling osrs_gear_obs.h) so pvp/inferno/zulrah reuse it. Update COLO_NUM_OBS + every running-index assert; recompute the exact total.

### Mask (28 click heads x 29): [0] noop always 1; [k] = 1 iff cell k-1 is actionable now: gear -> equippable AND not already equipped in its slot (2H room rule); consumable -> the EXISTING per-type benefit predicate (dose>0, potion_timer==0, drained/low/venom/spec-missing as today); food -> hurt AND food timer ok; empty/display -> 0. SPEC mask: arm valid iff equipped weapon spec cost <= energy or already armed.

### Snapshot v8->v9 + state-load trusts inventory_cells + equipped (do NOT re-equip a static set on load). From-scratch retrain.
