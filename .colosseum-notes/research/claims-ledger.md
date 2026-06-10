# Fortis Colosseum sim — claims ledger

Exhaustive inventory of every gameplay-mechanic value and behavioral rule the sim asserts,
for verification against external research. Records claims only: no correctness judgments.
Audited at befefc9cc (2026-06-10), worktree osrs-colosseum.

File key (all under `ocean/osrs/encounters/`):
- `hdr` = encounter_colosseum.h
- `model` = colosseum/encounter_colosseum_model.inc
- `spawn` = colosseum/encounter_colosseum_reset_spawn.inc
- `move` = colosseum/encounter_colosseum_movement.inc
- `combat` = colosseum/encounter_colosseum_combat.inc
- `boss` = colosseum/encounter_colosseum_boss.inc
- `mods` = colosseum/encounter_colosseum_modifiers.inc
- `pact` = colosseum/encounter_colosseum_player_actions.inc
- `step` = colosseum/encounter_colosseum_reward_step.inc
- `shared` = ocean/osrs/osrs_combat.h (shared formulas the encounter relies on)

Line numbers are physical lines of each file (ignore the `#line` directives).

Provenance tags:
- **WIKI** — comment or the keyed notes file (.colosseum-notes/mechanics.md /
  mechanics-to-code.md) cites the OSRS wiki for this exact value/rule.
- **DPSCALC** — value from the COLO_NPC_BASE stat table, sourced from
  refs/osrs-dps-calc monsters.json and cross-checked against wiki infobox raw wikitext
  (.colosseum-notes/npc-stat-blocks.md, pulled 2026-06-09). Treat as wiki-confirmed
  unless flagged otherwise.
- **UNCONFIRMED** — a comment or notes file explicitly marks it unconfirmed/assumed.
- **SILENT-ASSUMPTION** — no provenance comment anywhere; the number/behavior was invented.

---

## 1. ARENA & SPAWNS

| # | Claim | Current value | Location | Provenance |
|---|---|---|---|---|
| 1.1 | Main arena bounds | x,y in [0,33] → 34x34 tiles | model:11-16 | UNCONFIRMED (mechanics.md: dims unconfirmed; mechanics-to-code.md proposed 31x31, code uses 34x34 with no comment on the change) |
| 1.2 | Player start tile (waves 1-11) | (17, 5) | model:18-19, spawn:222-223 | SILENT-ASSUMPTION (mechanics-to-code.md assumed "arena centre"; code uses a near-north-gate tile, no comment) |
| 1.3 | NPC spawn position table | 8 positions: corners (4,4),(29,4),(4,29),(29,29) + edge mids (17,4),(4,17),(29,17),(17,29) | model:33-37 | SILENT-ASSUMPTION (wiki only says "spawn similarly to Fight Caves/Inferno") |
| 1.4 | Spawn selection logic | spawn_order[0..7] Fisher-Yates shuffled per wave; NPCs placed at successive shuffled positions; cursor wraps mod 8 (positions reused if >8 spawns); no occupancy check | spawn:110-116, 93-97 | SILENT-ASSUMPTION |
| 1.5 | Gate tiles (reinforcement entry) | x=17; north y=4, south y=29 | model:41-43 | SILENT-ASSUMPTION (gates' existence N/S is WIKI; exact tiles invented) |
| 1.6 | Gate choice for reinforcements | nearer gate by abs(player.y − gate_y) only (x ignored), ties go north | spawn:155-157 | WIKI ("whichever the player is closest to"); the y-only metric is SILENT-ASSUMPTION |
| 1.7 | Reinforcement placement | species i at x = 17 + i*size, y = gate_y; falls back to x = 17 − i if east edge exceeded | spawn:159-165 | SILENT-ASSUMPTION |
| 1.8 | Sol Heredit spawn tile | SW corner (17,17), 5x5 footprint [17,21]² | model:45-46, spawn:89-91 | SILENT-ASSUMPTION |
| 1.9 | Boss arena bounds (wave 12) | [12,27]x[12,27] = 16x16 box; player + NPC movement clamped inside while wave 12 live | model:48-57, boss:21-28, move:26,48 | WIKI for 16x16 size; comment flags dims UNCONFIRMED; the wiki's "four tiles jutting out from the corners" are NOT modeled |
| 1.10 | Boss-arena player start | (19, 26), south-centre; player teleported there when wave 12 begins, interaction cleared | model:61-62, boss:69-74 | SILENT-ASSUMPTION |
| 1.11 | Pillars | exactly 4, 1x1 each, two tiles in from each boss-arena corner: (14,14),(25,14),(14,25),(25,25); block movement (player+NPC) and crystal-sphere LoS, only while boss arena active | model:65-71, move:12-19,27,49, boss:281-299 | WIKI that 4 pillars exist and block LoS; exact positions/size SILENT-ASSUMPTION |
| 1.12 | No pillars / no LoS blockers on waves 1-11 | open arena; every NPC always has LoS to the player; pillar safespotting impossible pre-boss | move:34-39 | SILENT-ASSUMPTION (documented simplification; wiki describes pillar safespotting vs ranged enemies as a core strategy) |
| 1.13 | NPC collision | NPCs stamp size×size footprints; NPCs block each other and the player; pathfinding ignores entity flags, movement checks them | spawn:17-41, model:838-840 | SILENT-ASSUMPTION (engine convention) |
| 1.14 | Episode tick cap | 9000 ticks (90 min @ 0.6 s); hitting it ends episode as PLAYER_DIED | model:21, step:164-167 | SILENT-ASSUMPTION |

## 2. WAVES

| # | Claim | Current value | Location | Provenance |
|---|---|---|---|---|
| 2.1 | Total waves | 12; wave index 11 = Sol Heredit | model:22-23 | WIKI |
| 2.2 | Wave 1 start | warband(BZ,AR,SE) + 1 shaman | model:300 | WIKI (User:Skairunner/Fortis table) |
| 2.3 | Wave 2 start | warband + shaman + 1 javelin | model:301 | WIKI |
| 2.4 | Wave 3 start | warband + shaman + 2 javelin | model:302 | WIKI |
| 2.5 | Wave 4 start | warband + shaman + 1 manticore | model:303 | WIKI |
| 2.6 | Wave 5 start | warband + shaman + 1 javelin + 1 manticore | model:304 | WIKI |
| 2.7 | Wave 6 start | warband + shaman + 2 javelin + 1 manticore | model:305 | WIKI |
| 2.8 | Wave 7 start | warband + 1 javelin + 1 manticore + 1 shockwave | model:306 | WIKI |
| 2.9 | Wave 8 start | warband + 2 javelin + 1 manticore + 1 shockwave | model:307 | WIKI |
| 2.10 | Wave 9 start | warband + 1 javelin + 2 manticore | model:308 | WIKI |
| 2.11 | Wave 10 start | warband + 2 javelin + 2 manticore | model:309 | WIKI |
| 2.12 | Wave 11 start | warband + 1 javelin + 2 manticore + 1 shockwave | model:310 | WIKI |
| 2.13 | Wave 12 | Sol Heredit alone (plus Quartet warbander if active) | model:311, spawn:122-128 | WIKI |
| 2.14 | Reinforcement species W1-3 | 1x jaguar | model:300-302 | WIKI |
| 2.15 | Reinforcement species W4-6 | 1x jaguar + 1x shaman | model:303-305 | WIKI |
| 2.16 | Reinforcement species W7-9 | 1x minotaur | model:306-308 | WIKI |
| 2.17 | Reinforcement species W10-11 | 1x minotaur + 1x shaman | model:309-310 | WIKI |
| 2.18 | Wave 12 reinforcements | none | model:311 | WIKI |
| 2.19 | Reinforcement timer | 66 ticks (= 40 s, rounded down from 66.67) from wave spawn; counts down only while NPCs remain and gameplay is live (not during gaps) | model:25, spawn:130, step:135-143 | WIKI for 40 s; the floor-to-66 rounding is a documented derivation (mechanics-to-code.md) |
| 2.20 | Reinforcement count per overrun | exactly one set of the listed species, at most once per wave (sentinel COLO_REINFORCE_FIRED = −1) | spawn:137,147-169 | UNCONFIRMED (mechanics.md flags quantity/repetition unconfirmed; "one set, single overrun" is the documented modeling assumption) |
| 2.21 | Shaman center-spawn quirk | NOT modeled (wiki: shaman reinforcement spawns in arena centre if player camps the gate spawn) | spawn:147-169 (absent) | SILENT-ASSUMPTION (omission; quirk documented in mechanics.md §3.3) |
| 2.22 | Wave-clear condition | all NPC slots inactive | step:131-134,145 | SILENT-ASSUMPTION (matches genre convention) |
| 2.23 | Inter-wave spawn delay | 9 ticks between wave clear and next wave spawn | step:159 | SILENT-ASSUMPTION |
| 2.24 | Episode-start ready delay | 6 ticks (NPCs frozen, player actions gated, no attacks) | model:24, spawn:232, step:82-93,107 | SILENT-ASSUMPTION |
| 2.25 | During gaps | projectiles still resolve; NPC ticks, modifier hazards, and player attacks are gated off | step:85-93,107 | SILENT-ASSUMPTION |
| 2.26 | Win condition | clearing wave 12 (Sol + any Quartet warbander dead) = PLAYER_WON; reward +1.0 per wave cleared is hardcoded | step:145-153 | WIKI (Sol death ends run); the all-slots-dead requirement interacts with 2.27 |
| 2.27 | Quartet warbander on wave 12 spawns OUTSIDE the boss box | extra warbander uses the shuffled main-arena spawn table (all 8 positions lie outside [12,27]²) while player+NPC movement is clamped inside; the warbander can neither enter nor be reached | spawn:122-128, move:26,48 | SILENT-ASSUMPTION (interaction nobody commented on; with Quartet active wave 12 cannot satisfy the all-dead clear condition → tick-cap death) |
| 2.28 | Max NPCs per wave roster | 8 (headroom over the 7-NPC waves) | model:262-263 | WIKI-derived headroom |

## 3. FREMENNIK WARBAND

Stat rows (DPSCALC = refs/osrs-dps-calc cross-checked vs wiki infobox; full infoboxes exist for all three, including offensive bonuses):

| # | Claim | Current value | Location | Provenance |
|---|---|---|---|---|
| 3.1 | Berserker stats | id 12816, hp 48, att/str/def/mag/rng 110/110/80/110/110, speed 6t, size 1, max hit 29, melee att+150/str+90, magic att+150, defs stab/slash/crush 50, magic 0, ranged 75 | model:143-147 | DPSCALC |
| 3.2 | Archer stats | id 12814, hp 50, levels 110/110/80/110/110, speed 6t, size 1, max hit 14, melee att+150/str+150, magic att+150, ranged att+150/str+10, defs 0/0/0, magic 75, ranged 50 | model:148-152 | DPSCALC |
| 3.3 | Seer stats | id 12815, hp 50, levels 110/110/80/110/110, speed 6t, size 1, max hit 12, magic att+150 (all other off. bonuses 0), defs 50/50/50, magic 30, ranged 0 | model:153-157 | DPSCALC |
| 3.4 | Berserker behavior | melee (stab), attack range 1, 6-tick cycle | model:204, combat:360,374 | WIKI ("only attacks in melee distance, fixed 6-tick cycle") |
| 3.5 | Archer behavior | ranged, attack range 10, 6-tick | model:205 | UNCONFIRMED range (notes: "range UNCONFIRMED → ASSUME 10"); style/speed WIKI |
| 3.6 | Seer behavior | magic, attack range 10, 6-tick | model:206 | UNCONFIRMED range (same assumption); style/speed WIKI |
| 3.7 | RPS weakness mapping | berserker ← magic, archer ← melee, seer ← ranged | model:204-206 | WIKI |
| 3.8 | RPS implementation | player attack whose loadout style matches `player_style_that_max_hits` skips accuracy AND damage rolls: deterministic flat loadout max_hit every attack | pact:128-133 | WIKI semantics ("always max-hits"); the skip-accuracy + zero-variance interpretation is SILENT-ASSUMPTION (wiki phrasing: "spells other than Ice Barrage max hit even at negative magic bonus") |
| 3.9 | Warband overhead prayers | NOT modeled (no NPC overheads anywhere in the sim) | combat (absent) | SILENT-ASSUMPTION (omission) |
| 3.10 | Warband grouping/pathing | none: each warbander independently greedy-chases the player at 1 tile/tick; archer/seer hold once within range 10 with LoS | move:87-118 | SILENT-ASSUMPTION (no special logic claimed or implemented) |
| 3.11 | Warband spawn rule | trio listed explicitly at the start of every wave 1-11; never on 12 (except Quartet) | model:278-311 | WIKI |
| 3.12 | Attack delivery | berserker melee = 1-tick deferred hit, prayer-checked at land; archer/seer = projectile with distance-based delay, prayer-checked at land (flickable) | combat:151-161,134-148 | SILENT-ASSUMPTION (deferral pattern mirrors inferno engine) |

## 4. PER-NPC MECHANICS

### Serpent shaman

| # | Claim | Current value | Location | Provenance |
|---|---|---|---|---|
| 4.1 | Stats | id 12811, hp 125, att/str/def/mag/rng 100/90/90/220/160, speed 5t, size 1, max hit 28, magic att+50/str+15, defs 30/30/30, magic 15, ranged 50 | model:158-162 | DPSCALC |
| 4.2 | Behavior | plain magic projectile (Water Surge per notes), range 10, no poison, blockable by Protect from Magic | model:207, combat:378-380 | WIKI (explicitly NOT poisonous; premise correction documented in mechanics.md) |

### Jaguar warrior

| # | Claim | Current value | Location | Provenance |
|---|---|---|---|---|
| 4.3 | Stats | id 12810, hp 125, levels 200/330/125/100/160, speed 5t, size 2, max hit 47, melee str+25, defs 30/30/30, magic 15, ranged 45 | model:163-167 | DPSCALC |
| 4.4 | 3-hit combo | 3 independent melee hits per attack cycle, each its own accuracy + 0..47 damage roll, each individually Protect-from-Melee checked, all landing on the same tick (1-tick deferral) | combat:164-169 | WIKI (3 independent hits ≤47); same-tick landing detail SILENT-ASSUMPTION |
| 4.5 | Style | slash melee, range 1, 5-tick | model:208 | WIKI |

### Javelin Colossus

| # | Claim | Current value | Location | Provenance |
|---|---|---|---|---|
| 4.6 | Stats | id 12817, hp 220, levels 200/300/190/225/360, speed 5t, size 3, max hit 48, ranged att+25/str+20, defs 15/15/15, magic 20, ranged 30 | model:168-172 | DPSCALC |
| 4.7 | Normal throw | ranged projectile, range 15, Protect-from-Missiles blockable | model:209, combat:210 | WIKI |
| 4.8 | Skyfall cadence | every 5th throw (attack_count % 5 == 0, i.e. throws 5,10,15…) | model:585, combat:197 | WIKI ("every five attacks") |
| 4.9 | Skyfall delay | marked tile lands 3 ticks after throw | model:648, combat:203 | UNCONFIRMED (wiki: "a few ticks"; notes: "ASSUME 3-tick delay") |
| 4.10 | Skyfall prayer rule | ignores Protect from Missiles (damage applied directly, no prayer check); dodged by being off the marked tile at land time | combat:213-231 | WIKI |
| 4.11 | Skyfall damage | normal accuracy roll vs player ranged defence at mark time; on accuracy success, uniform 0..48 damage; on failure the skyfall lands for 0 | combat:198-204,222-224 | SILENT-ASSUMPTION (wiki says "heavy damage", no roll/accuracy detail; the sim subjects an "unblockable" special to a regular accuracy check) |
| 4.12 | Skyfall resolution order | resolved before NPC movement each tick, on the tile the player occupied at land tick | combat:402-408 | SILENT-ASSUMPTION |

### Shockwave Colossus

| # | Claim | Current value | Location | Provenance |
|---|---|---|---|---|
| 4.13 | Stats | id 12819, hp 125, levels 120/190/150/350/220, speed 5t, size 3, max hit 56, magic att+55/str+35, defs 15/15/15, magic 5, ranged 35 | model:173-177 | DPSCALC |
| 4.14 | Behavior | plain magic projectile, range 15, NO knockback / NO AoE / NO prayer-disable; only Dynamic Duo modifies it (pairs) | model:210, combat:378-380, spawn:117-119 | WIKI (premise correction: wiki says "no special mechanics") |

### Minotaur

| # | Claim | Current value | Location | Provenance |
|---|---|---|---|---|
| 4.15 | Stats | id 12812, hp 225, levels 300/360/190/250/120, speed 5t, size 3, max hit 74, melee att+15/str+64, defs 0/0/0, magic 0, ranged 12 | model:178-182 | DPSCALC |
| 4.16 | Style | crush melee, range 1, 5-tick | model:211 | WIKI |
| 4.17 | Heal conditions | runs its 5-tick cycle regardless of reach: if player at distance 1 it melees, ELSE it heals; i.e. heal suppressed only by melee contact | combat:346-353 | WIKI (heal suppressed when player in its melee range) |
| 4.18 | Heal range | all wounded, alive, non-full-HP NPCs within rect distance 6 of its footprint, all healed in the same cycle | combat:173-188 | WIKI for the 6-tile range; the heal-all-in-range-simultaneously detail is SILENT-ASSUMPTION |
| 4.19 | Heal amount/interval | uniform 0..10 HP per ally per attack cycle (every 5 ticks), capped at max_hp | combat:184-186 | SILENT-ASSUMPTION (wiki gives no amount or cadence) |
| 4.20 | Tick-eat ("eat behavior") | melee damage 1-tick deferred (queue), so a lethal hit can be eaten before land; this deferral applies to ALL melee NPCs, not minotaur-specifically | combat:151-161, step:95-104 | WIKI for minotaur ("damage calculated on a delay, can be tick-eaten"); generalization to every melee NPC is SILENT-ASSUMPTION |
| 4.21 | Movement | greedy chase, no obstacle routing unless Red Flag (then orthogonal sidestep, see 5.16) | move:82-118 | WIKI (simple pathing default; Red Flag grants routefinding) |

### Manticore

| # | Claim | Current value | Location | Provenance |
|---|---|---|---|---|
| 4.22 | Stats | id 12818, hp 250, levels 300/300/250/300/350, speed 10t, size 3, table max_hit 36, ALL aggressive bonuses 0, defs 0/0/0, magic 10, ranged 25 | model:183-187 | DPSCALC (wiki infobox lists every aggressive bonus as 0; levels wiki-confirmed per npc-stat-blocks.md, superseding mechanics.md's UNCONFIRMED flag) |
| 4.23 | Per-style max hits | ranged 36 / magic 31 / melee 31 | model:196-198, combat:246-250 | WIKI |
| 4.24 | Orb order | orbs 0,1 = {ranged, magic} in random order (50/50); orb 2 always melee | combat:305-314 | WIKI |
| 4.25 | Orb timing | 3 orbs fire 1 tick apart, flight time 0 (each lands 1 tick after fire), each prayed individually at land | combat:255-280 | WIKI |
| 4.26 | Cycle length | attack_timer reset to 10 (attack_speed) AFTER orb 3 fires → effective barrage-start-to-barrage-start period = 12 ticks (10 idle + 2 firing) | combat:276-279,334-341 | WIKI claims "10-tick cycle"; the reset-at-end anchoring (yielding 12) is SILENT-ASSUMPTION |
| 4.27 | Multi-manticore stagger | a ready manticore delays 5 ticks if any other manticore is mid-barrage | model:650, combat:236-244,295-297 | WIKI |
| 4.28 | Melee orb at range | orb 3 (melee style) is delivered from up to 15 tiles like the others, prayer-checked vs Protect from Melee | combat:255-267 | WIKI (barrage hits at range) |
| 4.29 | Movement while charging | no restriction: manticore moves like any ranged NPC during idle AND mid-barrage (holds within range 15 + LoS, else steps); barrage orbs keep firing while stunned/frozen checks pass | move:87-118, combat:283-288 | SILENT-ASSUMPTION (wiki describes a charge-up; sim has no movement/charge coupling) |
| 4.30 | Range | 15 tiles | model:212 | WIKI |

### Sol Heredit stat row (see section 6 for the boss machine)

| # | Claim | Current value | Location | Provenance |
|---|---|---|---|---|
| 4.31 | Stats | id 12821, hp 1500, levels 350/400/200/300/350, attack_speed 5, size 5, max_hit 44, melee att+250/str+5, ranged att+150/str+5, defs stab 65 / slash 5 / crush 30 / magic 750 / ranged 825 | model:188-192 | DPSCALC; attack_speed 5 is UNCONFIRMED (wiki "Varies", proposed 5 per npc-stat-blocks.md); ranged_def 825 from dstandard (drange param absent) |
| 4.32 | Sol offensive stat usage | table att/str/bonus values are UNUSED: every Sol damage source is flat typeless (no accuracy rolls); only his defensive stats are exercised (player attacks) | boss (whole file), combat:330 | SILENT-ASSUMPTION (consequence of 6.22) |

## 5. MODIFIERS

Draft system:

| # | Claim | Current value | Location | Provenance |
|---|---|---|---|---|
| 5.1 | Modifier list | exactly 14 real modifiers (ids 0-13; 16-wide id space with 2 unused) | hdr:65-87, model:330 | WIKI (Audacity/Blowing Raspberries/Dragon Hunter/Heavy-handed explicitly rejected as nonexistent) |
| 5.2 | Draft shape | 3 distinct options offered, player picks 1, pick persists for the run | model:331, mods:52-92 | WIKI |
| 5.3 | Draft timing | offered during the inter-wave gap after clearing waves 1..11 (0-based wave ≤ COLO_MODIFIER_LAST_WAVE=10), so up to 11 picks incl. one going into the boss | model:333-338, step:154-157 | UNCONFIRMED (comment: whether ANY choice is offered after wave 11 is unconfirmed; "offer through wave 11" is the modeled choice, one-line tunable) |
| 5.4 | Option pool | uniform random among eligible modifiers (not tier-maxed; pre-boss-only excluded once next wave ≥ 12); re-offering an active tiered modifier upgrades it one tier | mods:38-70,77-92 | SILENT-ASSUMPTION (wiki says "3 randomised"; eligibility/upgrade-pool mechanics invented) |
| 5.5 | Pre-boss-only set | Dynamic Duo, Mantimayhem, Reentry, Red Flag not offered after wave 11 | model:360-365 | WIKI |
| 5.6 | Tier caps | single-tier: Dynamic Duo, Red Flag, Quartet, Totemic; I/II/III: the other 10 | model:341-356 | WIKI |
| 5.7 | Pick is skippable | action index 0 = skip; optional draft window (config, default 0 = open until acted on) auto-closes an ignored draft | pact:182-186, mods:96-104, helpers:106 | SILENT-ASSUMPTION (wiki: "must choose one" — skip/auto-close is a training affordance) |
| 5.8 | Glory economy | NOT modeled (no glory/points) | — | SILENT-ASSUMPTION (omission; wiki documents per-modifier glory) |

Per-modifier effects:

| # | Modifier | Current values | Location | Provenance |
|---|---|---|---|---|
| 5.9 | Bees! | tier = swarm count 1/2/3; swarm steps 1 tile toward player every 12 ticks (both axes per step, i.e. diagonal allowed); on sharing the player's tile deals uniform 0..10 unblockable damage EVERY tick; spawns at a random spawn-table position each wave sync; no HP (kill/respawn modeled as move-only); ignores all collision incl. boss-arena clamp | model:380-388, mods:312-351 | WIKI (counts, 12-tick move, ≤10/tick unblockable); spawn location, diagonal step, no-kill model, clamp immunity SILENT-ASSUMPTION |
| 5.10 | Blasphemy | prayer drained = 20/40/60% of damage taken, by tier; applied per-tick on aggregated pending-hit damage AND per-instance for hazard/typeless damage | model:444, combat:70-75, mods:147-155 | WIKI percentages; dual application path SILENT-ASSUMPTION |
| 5.11 | Doom | die at 15/10/5 stacks (tier I/II/III); +1 stack per off-prayer damaging pending hit and +1 per hazard/typeless damage instance (incl. venom ticks, molten, AoE, spheres); stacks reset to 0 at each wave spawn; lethal check both pre- and post-action | model:426, combat:76, mods:157,161-164, step:97,116, mods:446 | WIKI (caps, reset between waves); per-instance granularity SILENT-ASSUMPTION |
| 5.12 | Dynamic Duo | each Shockwave Colossus roster entry spawns a second one | spawn:117-119 | WIKI |
| 5.13 | Frailty | player max HP −10/−20/−40% of base 99 (floored at 1), re-applied on pick + every wave spawn, current HP clamped down; tier-I "disables overheal" is stated in the enum comment but NOT separately implemented (no overheal source exists: food is shark) | hdr:76, model:447, mods:111-117,449 | WIKI percentages + overheal clause; non-implementation of overheal SILENT-ASSUMPTION |
| 5.14 | Mantimayhem | T1+: 2 projectiles per orb (each its own accuracy+damage roll); T2+: venom on a successful orb hit the current overhead does not block; T3: all 3 orb styles independently uniform-random | combat:260-271,300-304, mods:170-175 | WIKI tier effects; T3 "unpredictable" → fully-random interpretation SILENT-ASSUMPTION |
| 5.15 | Mantimayhem venom numbers | venom starts at 6 damage, ticks every 5 ticks, DECAYS by 1 per application until 0; re-application resets to 6 | model:454-456, mods:178-189 | SILENT-ASSUMPTION (no source cited; note real-OSRS venom escalates rather than decays — for the researcher to check) |
| 5.16 | Myopia | player attack range −2/−4/−6 by tier, floored at 1; applied to attack gating and chase-into-range | model:450, mods:136-140, pact:89-93,107 | WIKI |
| 5.17 | Reentry | skyfall landing leaves molten sand: T1 temporary pool (8 ticks), T2 permanent pool, T3 also melts the tile one WEST of the target; pool contact = flat 12/tick | model:435-437, mods:282-288 | WIKI (temp/permanent/west-tile); 8-tick T1 lifetime and 12 damage SILENT-ASSUMPTION; "permanent" implemented as never-cleared (persists across waves; wiki says until wave end) — scope SILENT-ASSUMPTION |
| 5.18 | Red Flag | minotaur whose greedy step is blocked takes a 1-tile orthogonal sidestep (axis-priority heuristic), as a stand-in for full routefinding | move:61-80,111-115 | WIKI rule; sidestep approximation documented in comment, exact behavior SILENT-ASSUMPTION |
| 5.19 | Relentless | NPC attacks bypass 33/66/100% of the player's defence ROLL (level+bonus product scaled, not defence level), max hit +1/+3/+6, T3 additionally forces every hit to succeed (accuracy ignored) | model:440-441, mods:124-131, combat:113-121 | WIKI numbers (incl. T3 ignore-accuracy); applying bypass to the whole def roll rather than the defence level is SILENT-ASSUMPTION |
| 5.20 | Solarflare path | orb walks an 8-step ring (4 nodes + midpoints) clockwise; nodes = the 4 boss pillars on wave 12, else the 4 arena spawn corners (orb exists on ALL waves) | mods:358-378,403-414 | WIKI ("circles the pillars"); pre-boss corner-ring substitute documented in comment but SILENT-ASSUMPTION vs reality (real arena pillars exist during waves 1-11) |
| 5.21 | Solarflare cadence | T1 moves every 2 ticks + pauses 7 ticks at corners; T2 every 2 ticks continuous; T3 every tick | model:411, mods:403-414 | WIKI |
| 5.22 | Solarflare damage | contact (same tile) rolls uniform 0..12 / 0..18 / 0..24 by tier; T3 also sets overhead prayer to NONE on contact | model:412-414, mods:381-385,418-423 | T1 12 SILENT-ASSUMPTION; T2 18 / T3 24 UNCONFIRMED (comments say so); T3 prayer-disable WIKI |
| 5.23 | Quartet | +1 uniformly-random warbander (berserker/archer/seer) added to every wave's spawn, including wave 12 | spawn:122-128 | WIKI (but see 2.27 for the wave-12 placement interaction) |
| 5.24 | Totemic | first time an NPC (never Sol) drops to ≤50% HP a totem spawns one tile off its east edge (west fallback); heals that NPC 30% of max HP every 5 ticks; destroyed when the player is within rect distance 1 (adjacent incl. diagonal) or the NPC dies; one totem per NPC slot | model:393-397, mods:195-240, combat:48-49,19-20 | WIKI (50% trigger, ~30% heal, "until destroyed"); 5-tick interval UNCONFIRMED (comment); adjacency-destroy + east placement SILENT-ASSUMPTION (documented modeling choice) |
| 5.25 | Volatility | on NPC death an explosion spans the footprint + 1 tile (T1) / + 2 tiles (T2+) on all sides; player overlap takes flat 25; T3 also leaves a molten pool (10 ticks) at the death centre | model:430-431, mods:295-306 | WIKI (reach tiers + T3 pool); 25 damage UNCONFIRMED (comment); 10-tick pool lifetime SILENT-ASSUMPTION |
| 5.26 | Molten pool shared rules | pool contact = flat 12/tick, max one burn per tick across overlapping pools; pre-boss pool list capped at 32 with oldest-recycling; pools NOT cleared between waves | model:435, mods:246-278 | SILENT-ASSUMPTION (all of it) |

## 6. SOL HEREDIT

| # | Claim | Current value | Location | Provenance |
|---|---|---|---|---|
| 6.1 | HP | 1500 | model:189, model:485 | WIKI |
| 6.2 | Phase thresholds | 6 phases: >90% / ≤90% / ≤75% / ≤50% / ≤25% / ≤10% of 1500 (integer math hp > max*pct/100); transitions fire on downward crossings, multi-phase drops walk through each | model:484, boss:31-39,429-431 | WIKI (90/75/50/25/10) |
| 6.3 | Decision cadence | Sol picks a new special every 5 ticks (COLO_SOL_ATTACK_INTERVAL), only when not mid-AoE/parry/grapple (specials never overlap) | model:545, boss:392-395,454-458 | UNCONFIRMED (wiki attack speed "Varies"; 5 proposed in npc-stat-blocks.md) |
| 6.4 | Decision weights | roll 0-99: triple parry if <90% HP and roll<35; grapple if <75% HP and roll<60; else AoE (i.e. 35% TP / 25% grapple / 40% AoE once both unlocked) | boss:375-376,398-414 | SILENT-ASSUMPTION (wiki gives no selection model) |
| 6.5 | No auto-attack | Sol has NO basic attack outside the named specials; his offense is exactly AoE rotation + triple parry + grapple + crystal spheres + enrage molten | boss:6-10, combat:330 | UNCONFIRMED (mechanics.md 5.6: base attack speed/max hit outside specials unconfirmed; modeled as none) |
| 6.6 | All Sol damage typeless | prayer never reduces any Sol damage except the triple-parry melee flick; survival = dodge + flick + slot-click | boss:11-15 | WIKI |
| 6.7 | Sol is stationary | overlay can_move=0 and the boss machine never moves him; he sits at [17,21]² all fight | model:213, boss (no movement code) | SILENT-ASSUMPTION (comment says "boss logic drives it later"; no movement was added) |
| 6.8 | AoE roster | Spear1 6x6, Spear2 6x6, Shield1 7x7 (solid — the wiki's 1-tile gap NOT modeled), Shield2 9x9; square blocks centred on the 5x5 boss footprint | model:459-468, boss:80-101 | WIKI footprint spans; Shield1 gap + spear sub-pattern safe tiles UNCONFIRMED (comment); centering math SILENT-ASSUMPTION |
| 6.9 | AoE rotation rule | fight opens with Spear1; thereafter style flips spear↔shield with probability 0.6 per attack; a same-style repeat uses the other sub-pattern | boss:104-130 | WIKI (opens Spear1, alternation, repeat→other sub-pattern); the 60% flip bias is SILENT-ASSUMPTION |
| 6.10 | AoE telegraph window | 2 ticks between telegraph and resolution (the dodge window) | model:546, boss:412-413,443-446 | SILENT-ASSUMPTION |
| 6.11 | AoE damage | flat 44 typeless if the player's tile is inside the block at resolve time (no roll, no partial) | model:547, boss:133-147 | WIKI max hit 44; flat-not-rolled SILENT-ASSUMPTION |
| 6.12 | Triple parry unlock | < 90% HP | boss:400 | WIKI |
| 6.13 | Triple parry timing | hits land at +3/+6/+9 ticks; third gap becomes 4 (+3/+6/+10) once Sol ≤50% HP | model:551-552, boss:175-182 | WIKI |
| 6.14 | Triple parry damage | combo A = 15/25/35, combo B = 15/30/45 (misprayed, flat); combos alternate strictly each invocation (A,B,A,B…) | boss:183-186 | WIKI damage sets; strict alternation SILENT-ASSUMPTION |
| 6.15 | Triple parry defense | each hit is a pending melee hit with prayer check locked on the tick BEFORE landing; Protect from Melee on that tick blocks it fully | boss:154-170 | WIKI ("activate Protect from Melee on the tick before each spear lands") |
| 6.16 | Grapple unlock | < 75% HP | boss:401 | WIKI |
| 6.17 | Grapple window | 4 ticks to click the called slot; called slot uniform-random over all 11 equipment slots | model:549, boss:217-223 | WIKI 4-tick window; uniform-over-all-gear-slots domain SILENT-ASSUMPTION (wiki: "calls out a body part") |
| 6.18 | Perfect parry | click with grapple_timer ≤ 1 (last tick) → next player attack is a guaranteed max, window 5 ticks | model:550, boss:237-243 | WIKI |
| 6.19 | Guaranteed-max consumption | flag + 5-tick countdown exist and are decremented, but `next_attack_guaranteed_max` is never consulted by the player-attack path (no max-hit override happens) | boss:437-440, pact:123-152 (absent) | SILENT-ASSUMPTION (stated mechanic, unwired effect) |
| 6.20 | Grapple failure | unparried grapple lands flat 44 typeless when timer hits 0 | model:548, boss:249-260 | WIKI |
| 6.21 | Phase-transition beams | every transition shows 6 light-beam telegraphs lingering 4 ticks; beams deal NO damage (obs/render only) | model:557-558, boss:379-389,433-436 | WIKI (6 beams exist); 4-tick linger + zero damage SILENT-ASSUMPTION |
| 6.22 | Crystal activation | rotating crystal starts at phase ≥ 2 (HP < 75%) and stays up | boss:384-387 | UNCONFIRMED (wiki: "later phases"; the <75% choice is the modeled cut) |
| 6.23 | Crystal motion | advances 1 perimeter step per tick, clockwise around the boss-arena edge | boss:265-278,315 | SILENT-ASSUMPTION |
| 6.24 | Sphere cadence | crystal fires a sphere every 6 ticks; every 2 ticks at enrage (phase 5) | model:555-556, boss:318-324 | SILENT-ASSUMPTION (wiki: "much more often" at enrage; both numbers invented) |
| 6.25 | Sphere behavior | targets the player's tile at fire time; lands 4 ticks later; uniform 0..75 typeless if the player is on the tile; firing suppressed if a pillar blocks the crystal→player segment (Bresenham) | model:553-554, boss:298-310,327-338 | WIKI (up to 75, 4-tick delay, pillar LoS); uniform roll + target-current-tile detail SILENT-ASSUMPTION |
| 6.26 | Enrage | phase 5 (<10% HP): molten sand placed on a uniform-random boss-arena tile every 3 ticks; tiles permanent for the rest of the fight (32-slot cap with random recycling); standing on one = flat 12 typeless per tick (max one burn/tick) | model:559-560, boss:345-370 | WIKI (every 3 ticks, random tile); 12 damage, permanence, cap SILENT-ASSUMPTION |
| 6.27 | Boss-arena confinement | player walkability and NPC steps clamped to [12,27]² while wave 12 live; pillar tiles unwalkable; player teleported to (19,26) at wave start | boss:56-75, move:23-31,43-56 | WIKI (gladiators barricade a 16x16 area); implementation details SILENT-ASSUMPTION |
| 6.28 | Sol defence vs player | player melee attacks roll vs Sol's STAB defence (65) — the player-attack path uses stab_def for all melee regardless of weapon style, so the slash weakness (slash_def 5) in the stat table is unreachable | pact:135-138, model:192 | SILENT-ASSUMPTION (stat table itself is DPSCALC; the stab-only lookup is an uncommented sim-wide choice) |
| 6.29 | Win detection | Sol death deactivates his slot; wave-12 clear (all slots dead) sets PLAYER_WON; no finisher animation/cutscene modeled | combat:44-47, step:145-153 | WIKI (kill ends run); finisher omission UNCONFIRMED in mechanics.md |
| 6.30 | "Last Recall" / shield-bash knockback | NOT implemented | — | WIKI (mechanics.md: not evidenced, do not implement) |

## 7. PROJECTILES / TIMING / COMBAT MATH

| # | Claim | Current value | Location | Provenance |
|---|---|---|---|---|
| 7.1 | Tick | 0.6 s per tick | model:21 comment, notes | WIKI |
| 7.2 | NPC magic projectile delay | floor((1+dist)/3) + 1 ticks (shared formula, dist = Chebyshev to nearest footprint tile) | combat:125-132, shared:368-371 | DPSCALC (shared module refs osrs-dps-calc; no colosseum-specific source) |
| 7.3 | NPC ranged projectile delay | floor((3+dist)/6) + 1 ticks | combat:125-132, shared:373-376 | DPSCALC (same) |
| 7.4 | NPC melee hit delay | 1 tick (deferred pending hit, prayer-checked at land → flickable/tick-eatable) | combat:151-161 | SILENT-ASSUMPTION |
| 7.5 | Manticore orb flight | 0 (1-tick land), one orb per tick | combat:263-267 | WIKI |
| 7.6 | Javelin skyfall flight | 3 ticks | model:648 | UNCONFIRMED |
| 7.7 | Sol sphere flight | 4 ticks | model:553 | WIKI |
| 7.8 | Player hit delay | flat 3 ticks for any ranged/magic loadout, 1 tick for melee (distance-independent) | pact:143 | SILENT-ASSUMPTION |
| 7.9 | Attack ranges | berserker/jaguar/minotaur/Sol 1; archer/seer/shaman 10; javelin/shockwave/manticore 15 | model:203-214 | WIKI for shaman 10 and the 15s; UNCONFIRMED for archer/seer 10; melee-1 WIKI |
| 7.10 | Melee attack gate | NPC melee fires only at distance exactly 1 (Chebyshev to footprint — includes diagonals despite the comment saying "cardinal contact only") | combat:360, shared:434-448,517-519 | SILENT-ASSUMPTION |
| 7.11 | Ranged/magic attack gate | 1 ≤ dist ≤ attack_range AND line of sight (LoS always true pre-boss) | combat:355-358 | SILENT-ASSUMPTION (range-floor 1 = no point-blank shots) |
| 7.12 | NPC accuracy roll | (att_level + 9) * (att_bonus + 64), style-appropriate level/bonus | combat:98-110, shared:288-292 | DPSCALC (shared formula, standard OSRS) |
| 7.13 | Player defence roll vs NPC | melee/ranged: (def+8)*(bonus+64); magic: (floor(0.7*magic + 0.3*def)+8)*(bonus+64); no stance bonus | combat:86-93, shared:294-311 | DPSCALC (shared formula) |
| 7.14 | NPC damage roll | uniform 0..max_hit inclusive; max hit is the Jagex handset value, never recomputed from str levels | combat:119, model:105-107,219-224 | SILENT-ASSUMPTION for uniform distribution; handset-max-hit design documented |
| 7.15 | Protect prayers in PvE | correct overhead blocks 100% of a prayer-checked NPC hit; check deferred to land tick (flicking works); Sol TP checks the tick before land | helpers:58-91, mods/boss as cited | WIKI convention (notes: "Protect Magic blocks (PvE 100%)") |
| 7.16 | NPC vs player target NPC-defence roll | (def_level + 9) * (style def_bonus + 64); player melee uses stab_def, ranged uses ranged_def, magic uses magic_def | pact:135-139 | SILENT-ASSUMPTION (+9 on the NPC defence side and stab-only melee lookup uncommented) |
| 7.17 | NPC movement speed | 1 tile per tick, every species (greedy step; ranged species hold in range) | move:109-110 | SILENT-ASSUMPTION |
| 7.18 | NPC aggro | every NPC permanently targets the player (aggro_target −1; no NPC-vs-NPC, no aggro loss) | spawn:77, move:96-97 | SILENT-ASSUMPTION (wiki "Aggro: Yes" for all; permanence assumed) |
| 7.19 | Player loadouts | placeholder maxed gear reused from inferno (melee set = Kodai wand + Elysian; ranged = Twisted bow + Masori); 2 weapon sets, no magic set; 99s across stats, shark food, restore potion 3-tick cooldown | model:680-711, spawn:189-220, pact:162-176 | SILENT-ASSUMPTION (comment marks gear placeholder; not a Colosseum claim — flagged so research scopes player-side DPS correctly) |
| 7.20 | Spawn-time NPC state | attack_timer starts at full attack_speed (first attack ≥ speed ticks after spawn); no spawn stun (stun_on_spawn 0 for all) | spawn:71,76, model:203-214 | SILENT-ASSUMPTION |

---

## TOP SUSPECTS

Entries most likely to be wrong vs external research — every one UNCONFIRMED or
SILENT-ASSUMPTION with material difficulty/behavior impact, ordered by expected impact.

1. **Sol has no auto-attack** (6.5) — entire boss DPS rests on the modeled specials; if real Sol autos between specials, sim difficulty is far off.
2. **Sol decision model: 35/25/40 weights on a flat 5-tick cadence** (6.3, 6.4) — real fight is widely described as a choreographed rotation, not iid rolls.
3. **Sol is stationary** (6.7) — real Sol moves/repositions; stationary 5x5 changes melee uptime and AoE dodging completely.
4. **AoE telegraph = 2 ticks** (6.10) — the dodge window is the boss fight's core skill check; no source.
5. **AoE damage flat 44, sub-patterns collapsed** (6.8, 6.11) — Shield1's 1-tile gap and spear safe-tiles omitted; flat-not-rolled damage on contact.
6. **Quartet on wave 12 spawns outside the boss-arena clamp** (2.27) — warbander is unreachable and unkillable → all-dead clear condition can never fire → guaranteed tick-cap loss whenever Quartet is active on W12.
7. **Crystal numbers: activation <75%, fire every 6 ticks (2 at enrage), 1 step/tick** (6.22-6.24) — only the 75-damage/4-tick sphere is sourced.
8. **Grapple slot domain = uniform over all 11 gear slots** (6.17) — wiki says "body part"; if the real callout set is smaller, parry difficulty is overstated.
9. **Perfect-parry guaranteed max is never consumed** (6.19) — mechanic advertised in state/obs but has zero gameplay effect.
10. **Enrage molten: 12/tick, permanent tiles** (6.26) — unsourced damage; permanent accumulation makes enrage a hard timer.
11. **Player melee always rolls vs stab defence** (6.28, 7.16) — Sol's slash weakness (slash 5 vs stab 65) is unreachable; boss TTK inflated relative to scythe-meta research.
12. **Manticore effective cycle = 12 ticks, not 10** (4.26) — timer reset anchored at barrage end; ~17% less manticore DPS than the wiki cycle.
13. **Skyfall is accuracy-rolled vs player ranged defence with uniform 0..48 damage** (4.11) — wiki frames it as unblockable heavy damage; an accuracy gate may roughly halve its threat.
14. **Skyfall delay 3 ticks** (4.9) — "a few ticks" assumption; defines the dodge window.
15. **Minotaur heal 0..10 per ally per 5-tick cycle** (4.19) — unsourced amount; sets whether ignoring the minotaur is viable.
16. **Venom model decays 6→0 at 5-tick intervals** (5.15) — real OSRS venom escalates (6,8,…,20 every 30 ticks); current model is both faster and weaker.
17. **Totem heal interval 5 ticks + destroyed by adjacency** (5.24) — cadence unconfirmed; adjacency-destroy replaces an attackable entity.
18. **Solarflare damage 12/18/24** (5.22) — T1 silent, T2/T3 flagged unconfirmed; with T3 prayer-disable this swings hazard cost a lot.
19. **Arena 34x34 with invented spawn/gate/start tiles** (1.1-1.5, 1.8) — kiting space, ranged uptime, and reinforcement pressure all scale with this; notes had proposed 31x31.
20. **No pillars / no LoS blockers on waves 1-11** (1.12) — wiki strategy is built on pillar safespotting; sim makes every ranged NPC permanently on-target.
21. **Reinforcements: one set, once per wave** (2.20) — explicitly unconfirmed whether overruns repeat every 40 s.
22. **Modifier draft offered after wave 11 + skippable + upgrade-pool mechanics** (5.3, 5.4, 5.7) — pick count and tier escalation drive late-run difficulty.
23. **Archer/seer range 10** (3.5, 3.6) — assumed; controls how much of the arena the warband covers.
24. **Inter-wave pacing: 6-tick ready delay, 9-tick wave gap** (2.23, 2.24) — invented; affects regen/eat windows and draft timing.
25. **Reentry T2 pools persist across waves** (5.17, 5.26) — wiki says until wave end; sim accumulates them for the run.
