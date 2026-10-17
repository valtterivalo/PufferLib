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
- **Live:** we start at 96 HP (not overhealed) and he opens with a Voidwaker swing. At 106 the net eats an anglerfish (p 0.97) and attacks. At 107 it teleports (p 0.63), and at 108 the tab is clicked.
- **Probes** (p of teleport at ticks 106 / 107 / 108):

| trace edit | 106 | 107 | 108 |
|---|---|---|---|
| none (live) | 0.06 | 0.63 | 0.58 |
| our HP 121 every tick | 0.01 | 0.18 | 0.20 |
| our HP 121 at 106-107 only, 99 at 108 | 0.01 | 0.18 | 0.64 |
| his swings removed | 0.03 | 0.60 | 0.78 |
| price 0.5 | 0.04 | 0.49 | 0.45 |
| price 2.5 | 0.16 | 0.86 | 0.81 |

- **Reading:** the teleport is driven by our HP being below 121 early in a fight, not by his swing. Training starts every fight at 121 (both players regear and overheal at the bank), so 96 to 99 HP two ticks into a fight means, in training, that a big hit already landed. The price scales it the expected way.
- **Fix:** draw fight start states from the measured distribution of real fight starts (our HP, his bar, who swings first, spec, supplies), not one regeared state.
