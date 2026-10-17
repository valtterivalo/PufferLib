# Riskfight policy findings

What the deployed nets did live, why, and how we found out. Each entry gives the session, the policy, the observation, the probe and the fix. Probes run `ocean/osrs/tests/rl_explain.c` on the client's engine trace (`~/.light2/engine-traces/run-<client run>.requests`): the server is deterministic, so a probe is the live computation with chosen inputs changed.

## 2026-10-16, j2: blind to his swings, eats at 99, shield ping-pong

Session 20261016T141036.

- **Eating at 99:** the sim let anglerfish overheal in combat; the wiki blocks it for 16 ticks after a hit. Eats at 99 had p 1.00.
- **Shield toggling:** the deployable mask allowed unequipping the shield with no two-hander in the bag.
- **No re-attack:** after eating it never attacked again. Injecting his swings into the trace restored attacks. The real cause was that the plugin's attack table lacked the rapier swing (8145, also Belle's folly), so every `V` line said "no swing" for the whole fight.
- **Fixes:** the overheal lock and mask, the shield mask, and the attack table plus `tools/attack_anim_audit.py`.

## 2026-10-17, s2t64 (sweep 2 trial 64, 512x2): eats then teleports on the second tick of a fight

Session 20261017T121142, fight from tick 106.
- **Live (from the recording):**
  - We stood at 120 HP (overhealed) with Vengeance up.
  - At 106 he opened with a 24 hit, taking us to 96. Our Vengeance echoed 18 and his recoil 3, so 21 landed on him at 107 while his bar stayed full (30/30): he was overhealed too.
  - The net ate an anglerfish at 106 (p 0.97) and attacked. At 107 it chose teleport (p 0.63), and the tab was clicked at 108.
  - We had not specced, so by the rude rule this teleport was polite.
- **Probes** (p of teleport at ticks 106 / 107 / 108):

| trace edit | 106 | 107 | 108 |
|---|---|---|---|
| none (live) | 0.06 | 0.63 | 0.58 |
| our HP 121 every tick | 0.01 | 0.18 | 0.20 |
| our HP 121 at 106-107 only, 99 at 108 | 0.01 | 0.18 | 0.64 |
| his swings removed | 0.03 | 0.60 | 0.78 |
| price 0.5 | 0.04 | 0.49 | 0.45 |
| price 2.5 | 0.16 | 0.86 | 0.81 |


Between 106 and 107 only these lines change: `B` (his HP bounds), `O` (his last swing age, distance, clock), `P`, and `V` (106: his swing; 107: the 21 on him at a full bar). Replacing one 107 line with its 106 value (teleport p at 107):

| 107 line replaced | teleport at 107 |
|---|---|
| B | 0.63 |
| O | 0.63 |
| V | 0.08 |
| fight started 60 / 300 ticks earlier (age) | 0.61 / 0.54 |

Variants of the 107 `V` line (teleport at 107):

| 107 `V` line | teleport at 107 |
|---|---|
| no hit on him, bar 30/30 | 0.08 |
| a 0 on him, bar 30/30 | 0.70 |
| 21 on him, bar 27/30 | 0.37 |
| 21 on him, bar 25/30 | 0.23 |

- **Reading:** the trigger is a hitsplat on him that does not move his bar, i.e. he is overhealed, right after he hit us and spent our Vengeance. It is not our HP being low at the start: the first hypothesis was wrong, since we started at 120. The sim produces the same event (recoil and Vengeance echoes set `hit_landed_this_tick`, osrs_pvp_combat.h:456), so this is not a live/sim observation mismatch.
- **Working hypothesis, not yet tested in the sim:** after a bad opening (his first hit landed, our Vengeance spent on an overhealed target), an early teleport is a cheap reset. It costs only the rematch wait (median 106 ticks, 61% rematch), refills everything including Vengeance, and is not rude by our rule, which only covers spec.
- **Discriminating test:**
  - Measure the sim policy's teleports in the first 10 ticks of fights, conditioned on this opening.
  - Ask the user whether an early reset costs anything in the real game (his account so far: "teleporting out instantly is pointless").
- **Other nets on the same trace:** s2t270 (sweep 2 trial 270, 512x2, same env) attacks through 106-113 with teleport p 0.00, both on the live trace and on the "0 on him, full bar" variant. So the early reset is not forced by the env; it is one of the solutions training can land in. s2t270 replaced s2t64 live on 2026-10-17.

  Simulated sessions, 300 per price, argmax (net per hour at price 0.4 / 0.7 / 1.2 / 2.33, then rude teleports per session):

  | net | 0.4 | 0.7 | 1.2 | 2.33 | rude per session |
  |---|---|---|---|---|---|
  | s2t270 | 7.18 | 6.84 | 6.13 | 6.03 | 0.4-0.7 |
  | s2t64 | 4.44 | 4.64 | 3.92 | 3.95 | 1.7-2.1 |

## 2026-10-17, teleport norms: kosher against rude, and our prayers

Origin: the user's account of real PvP norms. Teleporting right after a normal opening (he attacked us, nobody has specced) "would raise a ton of question marks; there is absolutely no point": the normal answer to being attacked is Smite and Piety and fighting back. Kosher teleports come after both players have used all their spec, or when we have run out of a supply class (one kind of food, prayer restores, or boosts after brewing down), and even then not always: a good kill chance may remain. A rude teleport (ours after our spec, before his) makes that opponent stop fighting us; repeated, it hurts our name and fights get hard to find.

- **Rule** (`riskfight_norms.h`, both seats): kosher = both players' special energy is below the cheapest special weapon each carries, OR the teleporter is out of hard food (anglerfish, shark, marlin, summer pie; karambwan/halibut are the combo clock and not a class), OR out of prayer restore doses (prayer potion, super restore, sanfew), OR out of boosts (no combat/ranging potion dose and no boost still on). Every other teleport is rude: no rematch, +1 reputation when ours. The old "after our spec, before his" case is inside it; "nobody has specced" and "ours unspent, his spent" are now rude too (they were free before: that is what made the early reset in the s2t64 entry cheap).
- **Opponents**: the script, OppFit and a second agent are classified by the same function from their true supplies. A rude exit of his changes nothing for us beyond the end of the fight and the lost rematch (the longer wait for a new opponent); reputation is ours alone. Logged as `opponent_rude_teleports`.
- **Observation**: session block `[7]` = kosher now (player's reading: own bag and energy, his estimated energy against the threat block's weapons with p >= 0.5). Block is now 8 floats, `RF_OBS_SIZE` 456: old nets do not load (rf_net: "architecture needs 8185856" for s2t270).
- **Prayers**: with `deployable = 1` the prayer heads are masked, so a net's action 0 reached the sim every tick as Piety OFF / overhead no-change: our seat fought with no Piety and no Smite, while the live plugin turns both on at the fight start (live O lines carry piety 1 and the smite bit). Training was out of distribution for those lines, and prayer was never spent, so prayer restores were useless. Now `riskfight_plugin_prayers` (called for the net seats in `puf_step`) sets Piety and Smite every tick: up from tick 1, draining 24 + 12 per tick against 60 + 2 x prayer bonus (about one point per 2 ticks, so 99 prayer lasts a few hundred ticks), off at 0, back after a restore. Scripts keep their own prayers; the plugin's re-enabling after a restore is an assumption. Nets trained before this need retraining.
