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
