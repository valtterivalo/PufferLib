# Colosseum mechanics → code mapping

Implementation reference for the truth-critical phases (tasks 4/5/6). Behavioral encoding
of mechanics.md. Stat-block numbers (levels/bonuses) come from npc-stat-blocks.md.
Tick = 0.6s. 40s reinforcement window = **66 ticks** (round 40/0.6=66.67 → 66).

## NPC type enum (10) — ColoNpcType
BERSERKER, ARCHER, SEER, SERPENT_SHAMAN, JAGUAR_WARRIOR, JAVELIN_COLOSSUS,
SHOCKWAVE_COLOSSUS, MINOTAUR, MANTICORE, SOL_HEREDIT.
MON_COLO_* added to monsters.json + regenerated (generate_monsters.py). Sizes: Fremennik 1x1,
Jaguar 2x2, Serpent 1x1, Javelin/Shockwave/Minotaur/Manticore 3x3, Sol 5x5.

## Wave script (waves 1..11 start with Fremennik warband = berserker+archer+seer)
Encode `ColoWaveDef waves[12]` = `{ start_spawns[], reinforce_species }`. Warband implicit each wave 1-11.
```
W1:  +SerpentShaman                                   reinforce: Jaguar
W2:  +Serpent +1 Javelin                              reinforce: Jaguar
W3:  +Serpent +2 Javelin                              reinforce: Jaguar
W4:  +Serpent +1 Manticore                            reinforce: Jaguar, Serpent
W5:  +Serpent +1 Javelin +1 Manticore                 reinforce: Jaguar, Serpent
W6:  +Serpent +2 Javelin +1 Manticore                 reinforce: Jaguar, Serpent
W7:  +1 Javelin +1 Manticore +1 Shockwave             reinforce: Minotaur          (no serpent from W7)
W8:  +2 Javelin +1 Manticore +1 Shockwave             reinforce: Minotaur
W9:  +1 Javelin +2 Manticore                          reinforce: Minotaur
W10: +2 Javelin +2 Manticore                          reinforce: Minotaur, Serpent
W11: +1 Javelin +2 Manticore +1 Shockwave             reinforce: Minotaur, Serpent
W12: Sol Heredit only (no warband unless Quartet)
```
Reinforcement rule: if wave not cleared in 66 ticks, spawn `reinforce_species` (one of each listed)
from the gate (north/south) nearest the player. ASSUMPTION (UNCONFIRMED): one reinforcement set per
overrun, not repeating. Flag in code with a named constant `COLO_REINFORCE_TICKS=66`.

## Per-NPC behavior (combat.inc)
- **Fremennik berserker**: melee stab, 6-tick, range 1. RPS: **magic always max-hits it**.
- **Fremennik archer**: ranged, 6-tick. range UNCONFIRMED → ASSUME 10. RPS: **melee always max-hits**.
- **Fremennik seer**: magic, 6-tick. range UNCONFIRMED → ASSUME 10. RPS: **ranged always max-hits**.
  (RPS = when hit by the listed style, force a max-accuracy+max-damage roll.)
- **Serpent shaman**: pure magic Water Surge, range 10, 5-tick, maxhit 28. Protect Magic blocks (PvE 100%). NOT poisonous.
- **Jaguar warrior**: melee slash, 5-tick, 2x2. **3 hits per attack, independent acc+dmg rolls, each ≤47.** Protect Melee.
- **Shockwave Colossus**: plain magic, range 15, 5-tick, maxhit 56. Protect Magic. No special. (Dynamic Duo → pairs.)
- **Javelin Colossus**: ranged, range 15, 5-tick, maxhit 48. Protect Missiles. **Skyfall: every 5th attack** a javelin lands on the
  player's CURRENT tile a few ticks later (ASSUME 3-tick delay), **ignores Protect Missiles**; dodge by moving off the tile.
- **Minotaur**: melee crush, 5-tick, 3x3, maxhit 74. Protect Melee. **Heals wounded allies within 6 tiles** unless player is in its
  melee range. Damage delayed → **tick-eatable**. Simple pathing (no obstacle routing) unless Red Flag.
- **Manticore**: range 15, 3x3, **10-tick cycle**. 3-orb barrage: orbs 1&2 are {ranged, magic} in random order, **orb 3 always melee**.
  Orbs fire **1 tick apart, travel time 0** (each prayed individually). Maxhits rng36/mag31/melee31. **Multi-manticore: a ready manticore's
  attack delays 5 ticks if another is mid-attack** (stagger). Mantimayhem escalates.

## Sol Heredit state machine (boss.inc) — 1500 HP, 5x5, slash-weak, 16x16 arena + 4 corner tiles + 4 pillars
Phase thresholds at HP fractions **0.90/0.75/0.50/0.25/0.10**. Track `phase` + `last_attack`/`last_substyle`.

AoE attacks (dodge only, typeless max **44**, prayer does NOT block): alternate spear/shield; if same style twice in a row, second uses
the other sub-pattern; **fight begins with Spear 1**.
```
Spear1: 6x6 dust under boss; dodge = step back off centre/edge
Spear2: 6x6 dust;            dodge = step to an off-centre tile
Shield1: 7x7 with 1-tile gap; dodge = step back 1
Shield2: 9x9;                 dodge = step back 2
```
**Triple Parry** (available <90% HP): 3-hit spear combo; block each by Protect-from-Melee flick on the tick BEFORE the hit.
Hit ticks: 3, then +3, then +3 (the 3rd becomes **+4 ticks when Sol <50% HP**). Misprayed damage: TP1 = 15/25/35, TP2 = 15/30/45.
**Grapple** (available <75% HP): Sol drops shield + calls a body part; player has **4 ticks** to click that equipment slot.
Click on the **last tick** = perfect parry → player's next attack within 5 ticks is a **guaranteed max**. Fail = **44** hit.
**Phase transition** (each threshold): spawn **6 light beams** + (later phases) a **crystal rotating around the arena edge** firing spheres,
each up to **75 dmg** with **4-tick delay** on the targeted tile.
**Enrage <10% HP**: molten sand spawns on a random tile **every 3 ticks**; spheres/lasers fire much more often.
UNCONFIRMED: Sol base auto-attack speed/maxhit outside named specials (only AoE44/grapple44/TP combos documented); "Last Recall" not
evidenced → do not implement. Model Sol's offense purely as the AoE rotation + TP combos + grapple + phase hazards.

## Modifiers (14) — ColoModifier enum + active set (chosen 1-of-3 after each wave 1..11, persist)
Group by effect target. v1 priority = the ones that change the combat MDP most; lower-priority can land as no-ops first (flag clearly).
```
PLAYER nerfs:   Blasphemy(prayer drain 20/40/60% of dmg taken), Frailty(-10/-20/-40% base HP; T1 disables overheal),
                Myopia(player atk range -2/-4/-6), Doom(stack on dmg; die at 15/10/5; reset per wave)
ENEMY buffs:    Relentless(bypass 33/66/100% def, +1/+3/+6 maxhit), Mantimayhem(manticore extra orb / venom / unpredictable),
                Red Flag(minotaur routefinding), Dynamic Duo(shockwave pairs), Quartet(+1 random warbander/wave incl W12)
HAZARDS:        Bees!(1/2/3 swarms, move every 12t, ≤10 unblockable poison/tick on contact),
                Solarflare(orb circling pillars: T1 every 2t pause 7t at corners / T2 continuous more dmg / T3 every tick, disables prayer on hit),
                Reentry(javelin skyfall leaves molten sand: temp / permanent / +west tile), Volatility(death explosion 1/2 tiles beyond size / +molten pool),
                Totemic(enemy at 50% HP spawns healing totem ~30%/few ticks until destroyed)
```
"Not offered after wave 11": Dynamic Duo, Mantimayhem, Reentry, Red Flag (their entities are pre-boss). ASSUMPTION: last modifier choice
offered after wave 11 too (UNCONFIRMED) — gate by a `COLO_MODIFIER_LAST_WAVE` constant so it is one-line tunable.

## Modeling assumptions to surface in code (truthful = explicit)
- Arena dims (waves 1-11) UNCONFIRMED → pick a concrete size (ASSUME 31x31 to comfortably hold 3x3 NPCs + gates N/S), named constant.
- Boss arena 16x16 + 4 corner tiles + 4 pillars, named constants.
- Player spawn = arena centre.
- Reinforcement quantity = one of each listed species per overrun, single overrun.
- Fremennik archer/seer range = 10. Manticore atk/def levels from npc-stat-blocks.md (proposed where UNCONFIRMED).
- Sol auto-attack: none outside AoE rotation + TP + grapple + hazards.
Every assumption gets a named constant or a one-line docstring so it is auditable and tunable.

## Reward shaping (reward_step.inc) — sparse + light dense, anti suicide-perversity
+damage_reward_coeff * dmg dealt; +wave_clear_bonus per wave; +boss_phase_bonus at each 90/75/50/25/10% threshold crossed;
+win_bonus on Sol death. Death/terminal penalty OFF by default (mirror inferno). ColosseumLog: wave reached, boss hp/phase, dmg dealt/taken,
deaths-by-source, modifier picks. score (binding my_log) = wins + (1-wins)*wave_frac*0.5, boss-phase fraction blended in like inferno's zuk-hp score.
