# Fortis Colosseum — Simulation Spec (OSRS Wiki, exact numbers)

Research compiled from oldschool.runescape.wiki for a faithful C reinforcement-learning
environment. Every number is cited to the exact wiki page and section. Items that could
not be confirmed from the wiki are marked **UNCONFIRMED**. Where the task premise differs
from the wiki (e.g. "Shockwave Colossus knockback", "Serpent Shaman poison"), the wiki is
treated as ground truth and the divergence is flagged.

Conventions: 1 game tick = 0.6 seconds. "Range N tiles" = max attack distance. NPC size
in tiles is the footprint side length (e.g. 3x3). All HP/max-hit/tick numbers are the
in-game encounter values, not generic Slayer variants.

---

## 1. Overview & Arena

| Property | Value | Source |
|---|---|---|
| Location | Large stadium on the cliffs east of Civitas illa Fortis | [Fortis Colosseum](https://oldschool.runescape.wiki/w/Fortis_Colosseum) (intro) |
| Purpose | Solo wave-survival activity; up to 12 waves, final wave is Sol Heredit | [Fortis Colosseum](https://oldschool.runescape.wiki/w/Fortis_Colosseum) (intro) |
| Total waves | 11 regular waves + 1 boss wave = 12 | [Fortis Colosseum](https://oldschool.runescape.wiki/w/Fortis_Colosseum) |
| Reinforcement rule | If a wave is not cleared within **40 seconds**, reinforcements arrive from the **north or south gate**, whichever the player is closest to | [Fortis Colosseum](https://oldschool.runescape.wiki/w/Fortis_Colosseum) (mechanics) |
| Spawn style | Enemies spawn similarly to TzHaar Fight Caves / Inferno | [Fortis Colosseum](https://oldschool.runescape.wiki/w/Fortis_Colosseum) |
| Boss-fight arena | During Sol Heredit's wave, gladiators barricade the centre, limiting movement to a **16x16 area with four tiles jutting out from the corners** (strategies phrase it as "roughly 16x15 within the four pillars") | [Fortis Colosseum](https://oldschool.runescape.wiki/w/Fortis_Colosseum); [Strategies](https://oldschool.runescape.wiki/w/Fortis_Colosseum/Strategies) |
| Pillars | Four pillars frame the central boss arena; used for line-of-sight blocking against ranged/magic enemies (Solarflare orb circles the pillars) | [Modifiers](https://oldschool.runescape.wiki/w/Fortis_Colosseum/Modifiers) (Solarflare) |
| Standard wave arena dimensions | **UNCONFIRMED** exact tile count. The full arena is larger than the 16x16 boss box; north and south gates bound it. Wiki does not give an explicit width/height for waves 1-11. |
| Player spawn tile | **UNCONFIRMED** exact coordinates. Player starts near arena centre; enemies arrive from the gates. |

Line-of-sight / pathing notes:
- Ranged enemies (Javelin Colossus, Shockwave Colossus, Manticore, Serpent shaman) attack from up to 10-15 tiles and require line of sight; pillars block LoS, which is the basis of "pillar safespotting" in strategies. ([Strategies](https://oldschool.runescape.wiki/w/Fortis_Colosseum/Strategies))
- Most large enemies use **simple/non-routefinding NPC pathing** by default. The **Red Flag** modifier specifically grants Minotaurs advanced routefinding (move around obstacles), which implies the default is dumb pathing. ([Minotaur](https://oldschool.runescape.wiki/w/Minotaur_(Fortis_Colosseum)); [Modifiers](https://oldschool.runescape.wiki/w/Fortis_Colosseum/Modifiers))
- Fremennik warband berserkers only attack in melee distance and "attack on a fixed 6-tick cycle." ([Fremennik warband berserker](https://oldschool.runescape.wiki/w/Fremennik_warband_berserker))

---

## 2. Wave Structure (all 12 waves)

Verbatim wave breakdown. "Fremennik Warband" = the trio (1 berserker + 1 archer + 1 seer),
guaranteed at the start of every wave **except wave 12**. Reinforcements are the enemies
that arrive if the wave is not cleared in 40 seconds.

Source: [User:Skairunner/Fortis](https://oldschool.runescape.wiki/w/User:Skairunner/Fortis) (wave breakdown table), cross-checked against individual NPC spawn-wave statements and [Fortis Colosseum](https://oldschool.runescape.wiki/w/Fortis_Colosseum).

| Wave | Starting spawns | Reinforcements (after 40 s) |
|---|---|---|
| 1 | Fremennik Warband, 1x Serpent shaman | Jaguar warrior |
| 2 | Fremennik Warband, 1x Serpent shaman, 1x Javelin Colossus | Jaguar warrior |
| 3 | Fremennik Warband, 1x Serpent shaman, 2x Javelin Colossus | Jaguar warrior |
| 4 | Fremennik Warband, 1x Serpent shaman, 1x Manticore | Jaguar warrior, Serpent shaman |
| 5 | Fremennik Warband, 1x Serpent shaman, 1x Javelin Colossus, 1x Manticore | Jaguar warrior, Serpent shaman |
| 6 | Fremennik Warband, 1x Serpent shaman, 2x Javelin Colossus, 1x Manticore | Jaguar warrior, Serpent shaman |
| 7 | Fremennik Warband, 1x Javelin Colossus, 1x Manticore, 1x Shockwave Colossus | Minotaur |
| 8 | Fremennik Warband, 2x Javelin Colossus, 1x Manticore, 1x Shockwave Colossus | Minotaur |
| 9 | Fremennik Warband, 1x Javelin Colossus, 2x Manticore | Minotaur |
| 10 | Fremennik Warband, 2x Javelin Colossus, 2x Manticore | Minotaur, Serpent shaman |
| 11 | Fremennik Warband, 1x Javelin Colossus, 2x Manticore, 1x Shockwave Colossus | Minotaur, Serpent shaman |
| 12 | Sol Heredit (boss only) | — (none; gladiators barricade the arena) |

Cross-checks and notes:
- **Javelin Colossi count escalation**: the main page says "two javelin colossi present" at **waves 3, 6, 8 and 10**, matching the 2x entries above. ([Fortis Colosseum](https://oldschool.runescape.wiki/w/Fortis_Colosseum))
- **Serpent shaman**: "appears at the start of the first six waves, and appears as reinforcements for waves 4, 5, 6, 10 and 11." ([Serpent shaman](https://oldschool.runescape.wiki/w/Serpent_shaman)) — consistent with the table.
- **Manticore**: starts on **wave 4**; "starting from wave 9, manticores begin spawning in pairs." ([Manticore](https://oldschool.runescape.wiki/w/Manticore)) — consistent (2x at waves 9, 10, 11).
- **Minotaur**: appears during **waves 7-11**, often with a serpent shaman on waves 10-11. ([Minotaur](https://oldschool.runescape.wiki/w/Minotaur_(Fortis_Colosseum))) — consistent (Minotaur is the wave 7-11 reinforcement).
- **Jaguar warrior**: reinforcement during the **first six waves** if not cleared in 40 s. ([Jaguar warrior](https://oldschool.runescape.wiki/w/Jaguar_warrior)) — consistent.
- **Reinforcement quantity** (how many of each reinforcement NPC arrive per 40-s tick, and whether they keep arriving) is **UNCONFIRMED** beyond the species listed. Modeling assumption: one reinforcement set per 40-s overrun; verify against gameplay.

### Modifier selection between waves

| Rule | Value | Source |
|---|---|---|
| When | After clearing each wave, before progressing to the next | [Fortis Colosseum](https://oldschool.runescape.wiki/w/Fortis_Colosseum) |
| Offered | **3** modifiers (randomised) | [Fortis Colosseum](https://oldschool.runescape.wiki/w/Fortis_Colosseum): "players must choose one of three modifiers" |
| Chosen | **1** of the 3 | same |
| Persistence | Chosen modifier "will persist for all subsequent waves of that run" | same |
| Total picks per full run | One choice after each of waves 1-11 = up to **11 modifiers** stacked by wave 12 (UNCONFIRMED whether a choice is offered after wave 11 going into the boss; some modifiers are explicitly "not offered after wave 11", implying choices stop at/after wave 11) | derived; see Modifiers section |
| Skippable | Implied mandatory ("must choose one") | [Fortis Colosseum](https://oldschool.runescape.wiki/w/Fortis_Colosseum) |

---

## 3. NPC Stat Tables (exact)

### 3.1 Master combat table

| NPC | Cmb lvl | HP | Max hit | Style | Prayer to protect | Atk speed | Range (tiles) | Size | Aggro |
|---|---|---|---|---|---|---|---|---|---|
| Fremennik warband berserker | 103 | 48 | 29 | Stab (melee) | Protect from Melee | 6 ticks | melee (1) | 1x1 | Yes |
| Fremennik warband archer | 104 | 50 | 14 | Ranged | Protect from Missiles | 6 ticks | UNCONFIRMED (ranged) | 1x1 | Yes |
| Fremennik warband seer | 104 | 50 | 12 | Magic | Protect from Magic | 6 ticks | UNCONFIRMED (ranged) | 1x1 | Yes |
| Serpent shaman | 161 | 125 | 28 | Magic (Water Surge) | Protect from Magic | 5 ticks | 10 | 1x1 | Yes |
| Jaguar warrior | 234 | 125 | 47 (x3 hits) | Slash (melee) | Protect from Melee | 5 ticks | melee | 2x2 | Yes |
| Shockwave Colossus | 239 | 125 | 56 | Magic | Protect from Magic | 5 ticks | 15 | 3x3 | Yes |
| Javelin Colossus | 278 | 220 | 48 (54 w/ Relentless III) | Ranged | Protect from Missiles | 5 ticks | 15 | 3x3 | Yes |
| Minotaur | 318 | 225 | 74 | Crush (melee) | Protect from Melee | 5 ticks | melee | 3x3 | Yes |
| Manticore | 320 | 250 | 36 (rng) / 31 (mag) / 31 (melee) | Ranged + Magic + Melee (sequential) | rotates (see below) | 10 ticks/cycle | 15 | 3x3 | Yes |
| Sol Heredit (boss) | 1,563 | 1,500 | 44 typeless AoE; combos 15/25/35 & 15/30/45; grapple 44 | Mixed/typeless (mostly dodge) | see Section 5 | see Section 5 | 5x5 | Yes |

Sources per row: [Fremennik warband berserker](https://oldschool.runescape.wiki/w/Fremennik_warband_berserker), [archer](https://oldschool.runescape.wiki/w/Fremennik_warband_archer), [seer](https://oldschool.runescape.wiki/w/Fremennik_warband_seer); [Serpent shaman](https://oldschool.runescape.wiki/w/Serpent_shaman); [Jaguar warrior](https://oldschool.runescape.wiki/w/Jaguar_warrior); [Shockwave Colossus](https://oldschool.runescape.wiki/w/Shockwave_Colossus); [Javelin Colossus](https://oldschool.runescape.wiki/w/Javelin_Colossus); [Minotaur](https://oldschool.runescape.wiki/w/Minotaur_(Fortis_Colosseum)); [Manticore](https://oldschool.runescape.wiki/w/Manticore); [Sol Heredit](https://oldschool.runescape.wiki/w/Sol_Heredit).

### 3.2 Defensive stats / affinities

| NPC | Atk (lvl) | Str | Def (lvl) | Magic | Ranged | Stab def | Slash def | Crush def | Magic def | Ranged def (all) | Poison/Venom | Cannon |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| Fremennik berserker | 110 | 110 | 80 | 110 | 110 | +50 | +50 | +50 | +0 | +75 | UNCONFIRMED | UNCONFIRMED |
| Fremennik archer | 110 | 110 | 80 | 110 | 110 | +0 | +0 | +0 | +75 | +50 | UNCONFIRMED | UNCONFIRMED |
| Fremennik seer | 110 | 110 | 80 | 110 | 110 | +50 | +50 | +50 | +30 | +0 | UNCONFIRMED | UNCONFIRMED |
| Serpent shaman | — | — | — | 220 | — | +30 | +30 | +30 | +15 | +50 | 100% immune | Immune |
| Jaguar warrior | 200 | 330 | 125 | 100 | 160 | +30 | +30 | +30 | +15 | +45 | 100% immune | Immune |
| Shockwave Colossus | 120 | 190 | 150 | 350 | 220 | +15 | +15 | +15 | +5 | +35 | 100% immune | Immune |
| Javelin Colossus | 200 | 300 | 190 | 225 | 360 | +15 | +15 | +15 | +20 | +30 | 100% immune | Immune |
| Minotaur | 300 | 360 | 190 | 250 | 120 | +0 | +0 | +0 | +0 | +12 | 100% immune | Immune |
| Manticore | UNCONFIRMED | UNCONFIRMED | UNCONFIRMED | UNCONFIRMED | UNCONFIRMED | +0 | +0 | +0 | +10 | +25 | 100% immune (base) | Immune |
| Sol Heredit | atk +250 / str +5 (melee bonus) | — | — | 300 | — | UNCONFIRMED | weak to slash | UNCONFIRMED | +750 | +825 | UNCONFIRMED | UNCONFIRMED |

Offensive bonuses worth noting: Javelin Colossus ranged strength +25/+20; Minotaur strength bonus +64, attack bonus +15; Fremennik archer/seer offensive bonuses UNCONFIRMED. Sources same as 3.1.

### 3.3 Per-NPC special mechanics

**Fremennik warband (trio)** — [berserker](https://oldschool.runescape.wiki/w/Fremennik_warband_berserker) / [archer](https://oldschool.runescape.wiki/w/Fremennik_warband_archer) / [seer](https://oldschool.runescape.wiki/w/Fremennik_warband_seer)
- Spawn as a set of 3 at the start of every wave except wave 12 (the **Quartet** modifier adds one extra random warbander each wave, and makes a warbander appear even on wave 12).
- Rock-paper-scissors weakness (always-max-hit when hit by the listed style):
  - Berserker: **magic** always max-hits ("spells other than Ice Barrage max hit even at negative magic bonus").
  - Archer: **melee** always max-hits.
  - Seer: **ranged** always max-hits.
- Berserker only attacks in melee distance, on a fixed 6-tick cycle. Archer (ranged) and seer (magic) attack at range; exact tile range UNCONFIRMED.

**Serpent shaman** — [Serpent shaman](https://oldschool.runescape.wiki/w/Serpent_shaman)
- Magic attacker using **Water Surge**, **range 10 tiles**, 5-tick attack, max hit **28**, high accuracy (magic level 220, +50 magic attack, +15 magic strength). Protect from Magic reduces it.
- **NOT poisonous** ("Poisonous: No"; 100% poison/venom immune). NOTE: the task premise of "Serpent Shaman magic+poison" is not supported by the wiki — model it as pure magic, no poison.
- Reinforcement quirk: on waves 4/5/6/10/11, if the player stands near the Jaguar/Minotaur reinforcement spawn point, the shaman spawns in the arena centre instead.

**Jaguar warrior** — [Jaguar warrior](https://oldschool.runescape.wiki/w/Jaguar_warrior)
- Melee (Slash), 5-tick, 2x2, uses the **dragon claws special-attack animation**.
- **Three hits per attack**, each with **independent accuracy and strength rolls**, each up to **47** (so worst case ~141 in one attack cycle — "potential for instant elimination", mirrors nail beasts). Protect from Melee mitigates.

**Shockwave Colossus** — [Shockwave Colossus](https://oldschool.runescape.wiki/w/Shockwave_Colossus)
- Magic attacker, **range 15 tiles**, 5-tick, max hit **56** (the highest standard-enemy max hit; higher than the ranged Javelin Colossus). Protect from Magic mitigates. 3x3.
- **NO knockback / no prayer-disable / no AoE shockwave** per the wiki: "they have no special mechanics, though they have an even higher max hit than their ranged counterparts." NOTE: the task premise of "Shockwave Colossus shockwave knockback" is **not supported** by the wiki — model it as a plain high-damage magic attacker. (The name is flavour; the only structural modifier is **Dynamic Duo**, which makes them spawn in pairs.)

**Javelin Colossus** — [Javelin Colossus](https://oldschool.runescape.wiki/w/Javelin_Colossus)
- Ranged attacker, **range 15 tiles**, 5-tick, base max hit **48** (49 / 51 / 54 under Relentless I / II / III). Protect from Missiles mitigates the normal throw.
- **Skyfall special**: "every five attacks, it launches a javelin high into the air, landing on the player's current position a few ticks afterwards, dealing heavy damage that **ignores Protect from Missiles**." Avoided by moving off the targeted tile before it lands.
- With **Reentry** modifier the landed javelin leaves a molten-sand pool (temporary / permanent / +1 tile west by tier).

**Minotaur** — [Minotaur (Fortis Colosseum)](https://oldschool.runescape.wiki/w/Minotaur_(Fortis_Colosseum))
- Melee (Crush), 5-tick, 3x3, max hit **74** (highest single-hit melee). Protect from Melee mitigates.
- **Heals other wounded monsters within 6 tiles** if the player is not in melee range of the Minotaur (a "support healer" — staying in its melee range suppresses the heal).
- Damage is calculated on a delay, so it can be **tick-eaten**.
- Default pathing is **simple** (does not route around obstacles) UNLESS **Red Flag** is active, which grants routefinding.

**Manticore** — [Manticore](https://oldschool.runescape.wiki/w/Manticore)
- 3x3, **range 15 tiles**, attacks in a **3-style sequential barrage** with a **10-tick attack cycle** (charge-up then fire). Max hits: ranged 36, magic 31, melee 31.
- Barrage order: first two orbs are **range-magic OR magic-range** (random which of the two leads); the **third (last) hit is always melee**. The three orbs launch **one tick after each other (0.6 s)** with **projectile travel time 0** (so each must be prayed on its own tick).
- Prayer handling: switch protection prayer to match each incoming orb in sequence (e.g. Missiles -> Magic -> Melee, or Magic -> Missiles -> Melee). Mispraying a single orb takes the corresponding max hit.
- Multi-manticore stagger: when one manticore attacks, "any other manticore that is ready to attack will have its attack delayed by 5 ticks" (so paired manticores from wave 9+ offset their barrages rather than overlapping perfectly).
- **Mantimayhem** modifier escalates it (extra projectile per orb / venom / less predictable pattern — see Modifiers).

---

## 4. Modifiers (handicaps)

Selection: after each cleared wave the player is offered **3 randomised** modifiers and must
pick **1**; the pick **persists for the rest of the run**. Several modifiers are flagged
"not offered after wave 11" (because the entities they affect no longer spawn before the
boss). Source for the full list and tiers:
[Fortis Colosseum/Modifiers](https://oldschool.runescape.wiki/w/Fortis_Colosseum/Modifiers).
Glory economy: harder modifiers grant more Glory; modifier points range ~200-2,600 by wave
([Glory](https://oldschool.runescape.wiki/w/Glory)).

The wiki lists exactly **14 modifiers**. NOTE: the task mentioned "Audacity", "Blowing
Raspberries", "Dragon Hunter", and "Heavy-handed" — **none of these exist** on the OSRS
Fortis Colosseum modifiers page. They are not part of this encounter (likely confused with
a Leagues relic set or another activity). Do not implement them.

| Modifier | Tiers (glory) | Exact effect by tier |
|---|---|---|
| **Bees!** | I (150) / II (300) / III (450) | I: a Bee Swarm drifts around the arena, converging on the player, **moving every 12 ticks**, dealing up to **10 unblockable poison damage every tick** on contact (killable in one hit but respawns). II: two swarms. III: three swarms. |
| **Blasphemy** | I (100) / II (200) / III (300) | Prayer points drained by a fraction of damage taken from enemies: I = **20%**, II = **40%**, III = **60%**. |
| **Doom** | I (200) / II (400) / III (600) | Gain a Doom stack whenever damage is taken; **die at** I = **15 stacks**, II = **10 stacks**, III = **5 stacks**. (Stacks reset between waves.) |
| **Dynamic Duo** | single (150) | Shockwave Colossi spawn in **pairs**. Not offered after wave 11. |
| **Frailty** | I (200) / II (400) / III (600) | Base Hitpoints reduced: I = **-10%** (and **overhealing disabled**), II = **-20%**, III = **-40%**. |
| **Mantimayhem** | I (150) / II (300) / III (450) | I: Manticores **add an additional projectile per orb** (attack twice per attack cycle). II: Manticores become **venomous** (unprotected hits inflict venom). III: attack pattern becomes **less predictable / unpredictable**. Not offered after wave 11. |
| **Myopia** | I (200) / II (400) / III (600) | Player **attack range reduced**: I = **-2 tiles**, II = **-4 tiles**, III = **-6 tiles**. |
| **Reentry** | I (150) / II (300) / III (450) | Javelin Colossus skyfall javelins leave molten sand: I = **temporary pool**, II = **permanent pool** (until wave end / boss death), III = **also melts the tile west of the target**. Not offered after wave 11. |
| **Red Flag** | single (250) | Minotaurs gain **advanced NPC pathing / routefinding** (move around obstacles). Not offered after wave 11. |
| **Relentless** | I (200) / II (400) / III (600) | Enemy attacks bypass Defence and gain max hit: I = **bypass 33% of Defence level, +1 max hit**, II = **bypass 66%, +3 max hit**, III = **fully ignore accuracy checks, +6 max hit**. |
| **Solarflare** | I (250) / II (500) / III (750) | A damaging orb circles the pillars. I = moves **every 2 ticks, stopping 7 ticks at corners**. II = moves **continuously every 2 ticks (no stopping), more damage**. III = moves **every tick, increased damage, and disables prayers if it hits you**. |
| **Quartet** | single (100) | An **extra random Fremennik warbander spawns every wave** (including making one appear on wave 12). |
| **Totemic** | single (200) | When an enemy reaches **50% HP**, a **healing totem** appears near it, healing ~**30% of its health every few ticks** until destroyed. |
| **Volatility** | I (100) / II (200) / III (300) | On death, enemy explodes: I = **1 tile beyond its size**, II = **2 tiles beyond its size**, III = explosion centre **leaves a temporary molten-sand pool**. |

UNCONFIRMED details: exact Bees!/Solarflare orb damage at higher tiers; exact Totemic heal cadence ("every few ticks"); whether modifier choices are still offered after wave 11 going into wave 12 (the "not offered after wave 11" flags strongly suggest the last meaningful choice is after wave 10 or 11).

---

## 5. Sol Heredit (Wave 12 boss)

Source: [Sol Heredit](https://oldschool.runescape.wiki/w/Sol_Heredit); arena from
[Fortis Colosseum](https://oldschool.runescape.wiki/w/Fortis_Colosseum) and
[Strategies](https://oldschool.runescape.wiki/w/Fortis_Colosseum/Strategies).

### 5.1 Core stats

| Property | Value |
|---|---|
| Hitpoints | **1,500** |
| Combat level | 1,563 |
| Size | **5x5 tiles** |
| Magic level | 300 |
| Magic defence bonus | **+750** |
| Ranged defence bonus | **+825** (all ranged types) |
| Melee offensive bonus | Attack +250, Strength +5 |
| Weakness | **Slash** (scythe of Vitur best; soulreaper axe / blade of Saeldor / abyssal tentacle acceptable). Magic and ranged are strongly discouraged due to the +750/+825 defences. |
| Arena | 16x16 (with four corner tiles jutting out), bounded by four pillars |

### 5.2 The four AoE attacks (dodge mechanics, not prayer)

All four create a dust hazard under/around the boss and are **unaffected by protection
prayers** — you must **physically dodge** by moving. He alternates spear and shield attacks;
**if two of the same style are used in a row, the second uses a different sub-pattern.** A
fight always **begins with Spear 1.**

| Attack | Hazard footprint | How to dodge |
|---|---|---|
| **Spear 1** | 6x6 dust under the boss | Move back from his **centre or edge tiles** |
| **Spear 2** | 6x6 dust | Move to any of his **off-centre tiles** |
| **Shield 1** | **7x7** hazard with a **1-tile gap** before the next ring | Move **1 tile back** |
| **Shield 2** | **9x9** middle hazard | Move **2 tiles back** |

Typeless AoE max hit (if you eat the dust): **44**. These are unblockable by prayer; only
dodging avoids them.

### 5.3 Triple Parry (combo) — Protect from Melee flicking

- Becomes available **below 90% HP**.
- The combo is a 3-hit spear sequence; you must **activate Protect from Melee on the tick before each spear lands**:
  - 1st hit: **3 ticks after the start of his animation**
  - 2nd hit: **3 ticks after the first**
  - 3rd hit: **3 ticks after the second** (**delayed to 4 ticks if Sol is under 50% HP**)
- Damage per hit if mispraying:
  - Triple Parry 1: **15 / 25 / 35**
  - Triple Parry 2: **15 / 30 / 45** (the second-attack delay to 4 ticks applies under 50% HP)
- Correct prayer flicking on each tick blocks the hit. This is the central "prayer-switch requirement" of the fight.

### 5.4 Grapple (item-slot parry)

- Becomes available **below 75% HP**.
- Sol **drops his shield and calls out a body part**; the player has **4 ticks to click the item in the corresponding equipment slot** to parry.
- Parrying **on the last possible tick** = a **perfect parry**, which makes the player's **next attack within 5 ticks a guaranteed max hit**.
- Failing the parry takes a **44** hit (grapple max hit).
- "Shield bash" terminology: the grapple is the shield-drop/body-part-callout mechanic; the wiki does **not** describe a separate knockback/displacement on Sol. Any "shield bash / knockback" beyond the grapple parry is **UNCONFIRMED / likely nonexistent**.

### 5.5 Phases, crystals, and enrage

- Phase transitions trigger at **90%, 75%, 50%, 25%, and 10% HP.**
- Each phase transition spawns **6 beams of light** and (later phases) **a crystal that rotates around the edges of the arena**, launching light spheres that deal **up to 75 damage** after a **4-tick** delay on the targeted tile.
- **Enrage below 10% HP**: **molten sand spawns every 3 game ticks on a random tile**, and the lasers/spheres fire much more often.

### 5.6 Line of sight, parry, fight end

- Line of sight: the central 16x16 box with four pillars; pillars and the boss's 5x5 body constrain movement and LoS. Specific LoS exploits against Sol are **UNCONFIRMED**; the fight is dodge-and-melee, not safespot.
- "Unblockable / parry" summary: spears and shields are **unblockable by prayer (dodge only)**; the **Triple Parry** is defended by **Protect from Melee flicking**; the **Grapple** is defended by **clicking the called equipment slot** within 4 ticks.
- **Last Recall / spec**: **UNCONFIRMED** from the pages fetched. The wiki page sections retrieved do not describe a "Last Recall" attack by name. (Last Recall is a player jewellery teleport item in OSRS; it may not be a Sol mechanic. Flag for verification.)
- **How the fight ends**: at low HP Sol can be finished; on the killing blow there is a finishing animation/cutscene and the wave (and run) completes, granting the rewards chest. Exact finisher animation/trigger is **UNCONFIRMED** in detail.
- Sol's base attack speed / max melee hit outside the named specials is **UNCONFIRMED** (the documented damage numbers are the AoE 44, grapple 44, and the two triple-parry combos).

---

## 6. Rewards & End Conditions

Source: [Fortis Colosseum](https://oldschool.runescape.wiki/w/Fortis_Colosseum); [Rewards Chest (Fortis Colosseum)](https://oldschool.runescape.wiki/w/Rewards_Chest_(Fortis_Colosseum)); [Glory](https://oldschool.runescape.wiki/w/Glory).

| Condition | Outcome |
|---|---|
| Death (HP reaches 0, or Doom stack cap, etc.) | Run ends; player removed from the Colosseum. Standard OSRS death rules for the activity (UNCONFIRMED whether items are lost or safe — verify). |
| Victory (Sol Heredit defeated on wave 12) | Run complete; rewards chest. |
| Quitting | Player may leave between waves (UNCONFIRMED exact mechanic). |
| **Dizana's quiver** | **Guaranteed** on first full completion (beating the Colosseum). Ranged ammo/cape slot item. |
| **Sunfire splinters** | Drop currency; used to charge the quiver and to craft **sunfire runes, searing pages, and sunfire wine**. |
| **Glory** | Personal-best scoring (not cumulative). Components: Completion Bonus (100 @ W1 to 1,200 @ W12), No Damage Bonus (same scale), Modifier Points (200-2,600 by wave), Time Bonus. Theoretical max ~72,000 (all waves first-tick), practical ceiling ~60,000-65,000, minimum completion 16,350. |
| Echo / hard-mode rewards | **UNCONFIRMED** beyond the above (e.g. tonalztics, other uniques) — out of scope for the sim's core loop. |

---

## Open items to verify in-game / against fresh wiki reads (UNCONFIRMED)

1. Standard wave-arena tile dimensions and exact player spawn tile (waves 1-11).
2. Fremennik archer/seer attack range in tiles, and their offensive bonuses.
3. Reinforcement quantities per 40-s overrun and whether they keep arriving on repeated overruns.
4. Manticore's own attack/defence levels (only its defensive bonuses and max hits are confirmed).
5. Whether a modifier choice is offered after wave 11 (the "not offered after wave 11" flags suggest the meaningful choices end by then).
6. Sol Heredit: base melee attack speed/max hit outside named specials; existence of any "Last Recall" or shield-bash/knockback; exact finisher trigger; LoS exploits.
7. Death penalty specifics (items kept vs lost) for the activity.
8. Higher-tier exact damage for Bees! and Solarflare; Totemic heal cadence.

## Premise corrections (wiki disagrees with the task brief)

- **Shockwave Colossus**: no knockback, no shockwave AoE, no prayer-disable. It is a plain magic attacker (max hit 56). Only Dynamic Duo (pairs) modifies it.
- **Serpent shaman**: not poisonous (100% poison/venom immune). Pure Water Surge magic, range 10.
- **Modifiers Audacity / Blowing Raspberries / Dragon Hunter / Heavy-handed**: do not exist in Fortis Colosseum. The real list is the 14 above.
