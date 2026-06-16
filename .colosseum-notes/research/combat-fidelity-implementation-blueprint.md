# Combat-fidelity pass — implementation blueprint (locked 2026-06-16)

Full spec: workflow wf_853348a4 output (loadout-spec). This file = the locked
decisions + chunked plan. Melee+ranged sets ALREADY match the wiki Budget/High-eff
tabs; only the magic set + thralls + Death Charge are new.

## Locked decisions (defaults applied; user can veto any before retrain)

- Profile mapping: BEGINNER <- wiki Budget, SPEEDRUN <- wiki High-efficiency. Keep
  enum names BEGINNER/SPEEDRUN (no rename) unless the user asks.
- Magic weapon per kit: budget = ITEM_TRIDENT_OF_SWAMP (base max 31 = floor(99/3)-2),
  high-eff = ITEM_TUMEKENS_SHADOW (base max 34 = floor(99/3)+1). base_level=99.
- Magic-set ARMOR = 1:1 with what each kit actually carries (user said "copy 1:1"),
  NOT an idealized mage set:
  - budget magic set: Trident + the budget general/melee armor it wears + Dragon
    defender (1h). No Occult (budget carries none) -> weak ~31 max. Faithful to the
    budget kit using Trident only to two-shot the melee frem.
  - high-eff magic set: Shadow + Occult necklace + Confliction gauntlets (both are
    in the high-eff inventory) on the kit body armor. 2h -> no shield.
  FLAG to user: the spec workflow RECOMMENDED idealized Ancestral/Virtus sets
  (budget ~38, high-eff ~50). I chose 1:1 per the directive; mage power is tunable
  DATA (no contract impact) so we can buff later with only a retrain, not a contract
  break. If the weak budget mage is useless in practice, revisit.
- Tumeken's shadow 3x: add OSRS_ITEM_EFFECT_TUMEKENS_SHADOW (1u<<14); in the MAGIC
  branch of encounter_compute_loadout_stats triple the gear magic_damage% (clamp 100),
  NOT the base hit. Mirror in encounter_update_loadout_level so brew/augury recompute
  stays right. (Tbow effect is the pattern.)
- Thralls: COLO_THRALL_MAX_HIT=3 (greater) BOTH kits, uniform 0..3, ignore accuracy,
  4-tick cadence, damage via col_queue_npc_pending_hit (player-credited), Sol immune.
  Lifetime per kit: COLO_THRALL_LIFETIME_TICKS={ [SPEEDRUN]=198, [BEGINNER]=99 }.
  17-tick recast. One thrall; re-summon replaces.
- Death Charge: 15% spec on a player-credited killing blow within a 100-tick armed
  window; cooldown 100 ticks STARTS when a kill consumes it (silent close if window
  expires unused); hooks the existing player.special_energy field.
- Obs block: LEAN 6 fields (no reserve slots, per no-dead-code principle):
  thrall_active, thrall_lifetime_norm, thrall_recast_norm, dc_armed, dc_window_norm,
  dc_cd_norm. Appended AFTER THREAT_LOS.
- Missing wiki items: leave omitted (Venator bow, Abyssal tentacle, Burning claws,
  Dragon warhammer, Bellator ring, Book of the dead) — aux/spec alts or modeled
  abstractly; none block the pass.

## Contract delta (LOCKED)
- COLO_NUM_OBS 816 -> 823 (+1 magic gear one-hot in player block, +6 thrall/DC block).
- COLO_NUM_ACTION_HEADS 10 -> 11 (new COLO_HEAD_SPELL, dim 3: none/summon-thrall/death-charge).
- COLO_GEAR_DIM 3 -> 4 (no_switch/melee/ranged/magic). COLO_ACTION_MASK_SIZE 71 -> 76 (+1 gear, +3 spell... net +4 => 75? recount at impl; the running-index assert is the guard).
- COLO_NUM_WEAPON_SETS 2 -> 3 (COLO_GEAR_MAGIC=2). loadout_stats[3]/set_effects[3] auto-size.
- COLO_SNAPSHOT_VERSION 7u -> 8u.
- FULL retrain from scratch; CUDA/Metal parity harness regenerates against v8. HOLD
  the retrain for explicit user go.

## Chunked implementation (each chunk must build + pass the battery before commit)
1. MAGIC SET: ColoWeaponSet+COLO_GEAR_MAGIC; COLO_*_MAGIC_LOADOUT arrays (1:1);
   append to COLO_*_LOADOUTS; OSRS_ITEM_EFFECT_TUMEKENS_SHADOW + MAGIC-branch 3x;
   magic compute at reset_spawn + render_snapshot recompute; GEAR dim 3->4 + handler
   + mask; obs magic one-hot (+1); snapshot v8. Tests: magic max-hit math (budget 31->~31,
   high-eff shadow 34->~50), contract sizes. Build + battery. Commit.
2. THRALLS + DEATH CHARGE: ColosseumState fields + constants; COLO_HEAD_SPELL (dim 3)
   + handler + mask; per-tick thrall + DC decrement; thrall attack via queue; NPC-death
   DC proc hook; Sol-immunity guard; obs +6 block; lab_json emit. Tests: thrall regression,
   Death Charge regression, snapshot round-trip, contract sizes 823/11-heads/v8. Build + battery. Commit.
3. VERIFY (workflow): adversarial lenses — contract consistency, magic math, thrall
   fidelity, DC edge cases, Sol immunity, cross-encounter safety. Then CPU smoke-train.
4. HOLD for user go -> CUDA retrain on pufferbox (config: agents=4096 hs=512 L=2 horizon=16, 138M).
