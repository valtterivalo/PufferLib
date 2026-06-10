# Web research: Sol Heredit + modifier gap-fill (2026-06-10)

Targeted gap-fill over salvaged-workflow-claims.md. Facts only, every number sourced. All fetches 2026-06-10.

Source keys:
- W-MAIN = https://oldschool.runescape.wiki/w/Sol_Heredit (wiki, page mod 2026-05-14 per salvage)
- W-STRAT = https://oldschool.runescape.wiki/w/Fortis_Colosseum/Strategies (Sol Heredit section; note https://oldschool.runescape.wiki/w/Sol_Heredit/Strategies is a `#REDIRECT [[Fortis Colosseum/Strategies#Sol Heredit]]`)
- W-MODS = https://oldschool.runescape.wiki/w/Fortis_Colosseum/Modifiers
- W-COLO = https://oldschool.runescape.wiki/w/Fortis_Colosseum
- COLOSIM = colosim.com source, repo https://github.com/detuks-client/InfernoColoSimulator branch `colosseum` (the los.colosim.com hunt resolved here; detuks-client is the author org). Files cited as COLOSIM:<path> under src/content/colosseum/.
- UPD-0320 = https://oldschool.runescape.wiki/w/Update:Varlamore:_Part_One (20 Mar 2024, revised 22 Mar)
- UPD-0424 = https://oldschool.runescape.wiki/w/Update:Varlamore_Tweaks,_GameJam_V_Commences_&_More! (24 Apr 2024)
- UPD-0515 = https://oldschool.runescape.wiki/w/Update:Further_Project_Rebalance_(Skilling)_&_Varlamore_Changes (15 May 2024)

---

## GAP 1 — Sol triple-parry timing: DISCREPANCY RESOLVED

**The wiki 3/3/3-or-4 and colosim 2/5/8-or-9 numbers describe the same hit schedule.** Colosim's DelayedAction offsets (2/5/8, 2/5/9) are when each parry *projectile is added*; every parry projectile carries `setDelay: 1`, so damage resolves one tick later: absolute hit ticks 3/6/9 (short) and 3/6/10 (long) after attack start. Inter-hit gaps 3/3 and 3/4 — identical to the wiki. The salvage's "hits at ticks 2/5/8" reading missed `setDelay: 1`.

- FACT | hit ticks (wiki): 3 after animation start, +3, +3 (third +4 below 50% HP) | W-MAIN | "1st attack: 3 ticks after the start of his animation ... 2nd attack: 3 ticks after first ... 3rd attack: 3 ticks after the second (delayed to 4 ticks if under 50%)"
- FACT | hit ticks (W-STRAT, same) | W-STRAT | "initial attack hits 3 ticks after the start of the charging animation, the second attack hits 3 ticks after that, and the final attack after another 3 ticks (delayed to 4 ticks beginning in Phase 4 at 50% health)"
- FACT | colosim scheduling: projectiles added at ticks 2/5/8 (short) or 2/5/9 (long) | COLOSIM:js/mobs/SolHeredit.ts | "DelayedAction(this.doParryAttack(15, 3).bind(this), 2) ... doParryAttack(short ? 25 : 30, 2).bind(this), 5) ... doParryAttack(35, 2).bind(this), 8) [short] ... doParryAttack(45, 3).bind(this), 9) [long]"
- FACT | colosim parry projectile lands 1 tick after being added | COLOSIM:js/mobs/SolHeredit.ts | "{ hidden: true, setDelay: 1, checkPrayerAtHit: !overheadWasOn }"
- FACT | colosim test confirms first damage on tick 3 | COLOSIM:tests/SolAttack.test.ts | test "check timing of damage is correct": after `world.tickWorld(2)` player still 99 HP, drops on the next tick
- FACT | damage per hit: 15/25/35 (phases 1-3 = "Triple Parry 1" = colosim short), 15/30/45 (phase 4+ below 50% = "Triple Parry 2" = colosim long) | W-MAIN + W-STRAT + COLOSIM | W-MAIN infobox "15-25-35 (Triple Parry 1) ... 15-30-45 (Triple Parry 2)"; W-STRAT "On phase 1-3, each attack will deal 15, 25, and 35 damage, respectively, if not properly prayed against. In phase 4 onwards, they will deal 15, 30, and 45 damage"
- FACT | counter: flick Protect from Melee per hit, precisely at the hit | W-MAIN | "requires the player to precisely activate Protect from Melee on the tick before he lands his spear"
- FACT | colosim counter semantics: prayer must be OFF during the lookback window (3 ticks before hit 1, 2 before hit 2, 2 before hit 3 short / 3 long) and is then checked at hit resolution | COLOSIM:js/mobs/SolHeredit.ts | "const overheadWasOn = this.wasOverheadOn(ticks); ... overheadWasOn ? new ParryUnblockableWeapon() : new MeleeWeapon() ... checkPrayerAtHit: !overheadWasOn"; lookback pops overheadHistory of any of "Protect from Melee", "Protect from Range", "Protect from Magic"
- FACT | early-prayer punishment: hit becomes unblockable (prayer ignored) | W-MAIN + COLOSIM | W-MAIN "Turning on your prayer early before Sol Heredit lands his attack will turn it off, forcing the player to take a hit"; COLOSIM `ParryUnblockableWeapon` when overheadWasOn
- FACT | every parry hit deactivates all three overhead protect prayers (forces re-flick per hit) | COLOSIM:js/mobs/SolHeredit.ts | "findPrayerByName(\"Protect from Melee\").deactivate(); ... \"Protect from Range\" ... \"Protect from Magic\" ..."
- FACT | properly prayed hits deal 0 (damage values are "if not properly prayed against") | W-STRAT | quote above
- NUANCE | one-tick convention: wiki says activate "on the tick before he lands", colosim checks prayer state at hit resolution with the preceding 3/2/2-3 ticks required OFF. Gaps and punish rule agree across sources; the exact client-input-tick convention is the only fuzz.
- FACT | triple attack unlock: below 90% HP | W-MAIN + COLOSIM | W-MAIN "The triple parry can be performed under 90% HP"; COLOSIM attack pool `this.phaseId >= 1 && [Attacks.TRIPLE_SHORT]`
- FACT | NO perfect-parry mechanic exists for the triple attack in any source | W-MAIN + W-STRAT + COLOSIM | wiki describes perfect parry only for grapple; grep of SolHeredit.ts found zero occurrences of "perfect"/"guaranteed"/"max hit"
- FACT | perfect parry is a GRAPPLE mechanic: defend on last possible tick -> next attack within 5 ticks is a guaranteed max hit | W-MAIN | "Defending a body part on the last possible tick will result in a perfect parry, which makes the player's next attack within 5 ticks be a guaranteed max hit."
- FACT | game message on perfect parry | W-STRAT | "You perfectly parry Sol Heredit's grapple!"
- FACT | colosim does NOT implement grapple perfect parry | colosim.com notes (salvaged) + COLOSIM grep | site notes "Not implemented yet: Grapple perfect parry"; no perfect-parry code in SolHeredit.ts

## GAP 2 — Sol fight structure

### (a) Normal attacks
- FACT | Sol's only "normal" attacks are the four AoEs: Spear 1, Spear 2, Shield 1, Shield 2. No separate auto-attack exists. Specials = triple, grapple. | W-MAIN + COLOSIM | W-MAIN "Sol Heredit has 4 main AoE attacks, all of which will create a 6x6 dust hazard under the boss, making the attacks avoidable only by stepping out"; COLOSIM attack pool contains only SHIELD x2, SPEAR x2 + gated TRIPLE_SHORT/TRIPLE_LONG/GRAPPLE
- FACT | first attack of the fight is always a spear; spear also forced after phase transitions | W-MAIN + COLOSIM | W-MAIN "Sol will always use a Spear attack first after phase transitions"; COLOSIM "forceAttack: Attacks | null = Attacks.SPEAR; // first attack is always a spear?"
- FACT | variant alternation: consecutive same-type attacks alternate 1 -> 2; switching type resets to variant 1 | W-MAIN | "if his previous attack was Spear 1, his next attack will be Spear 2 ... Using a single Spear attack and following up with a Shield attack, or vice-versa, will reset the pattern back to its first one." COLOSIM mirrors with firstSpear/firstShield booleans (grapple sets both back to true)
- FACT | specials require 2 normal attacks between them | COLOSIM:js/mobs/SolHeredit.ts | "const SPECIAL_ATTACK_COOLDOWN = 2; ... const canSpecial = this.specialAttackCooldown <= 0"
- FACT | spear and shield are double-weighted in the attack pool | COLOSIM:js/mobs/SolHeredit.ts | pool contains SHIELD twice and SPEAR twice
- FACT | next-attack delays: spear 7 ticks (6 below 75%), shield 6 (5 below 75%), triple-short 12 (11 when 50-75%), triple-long 12, grapple 7 | COLOSIM:js/mobs/SolHeredit.ts + tests | "return this.phaseId < 2 ? 7 : 6" (spear); "return this.phaseId < 2 ? 6 : 5" (shield); tests "check triple attack (above 75%) has attack delay of 12" / "(between 50 and 75%) ... 11" / "(below 50%) ... 12"; "check grapple attack has attack delay of 7"
- FACT | wiki corroborates the speed-up | W-STRAT | "These attacks speed up by 1 tick each after he goes below 75% health and does his phase transition."

### (b) Movement
- FACT | Sol chases the player (pathfinds toward aggro) | COLOSIM:js/mobs/SolHeredit.ts | "override getNextMovementStep() { if (!this.aggro) { return { dx: this.location.x, dy: this.location.y }; }" then pathfinding toward aggro
- FACT | must be adjacent at start of tick to launch an AoE | W-STRAT + COLOSIM | W-STRAT "Sol must be next to the player at the start of a tick (before movement is calculated) to initiate an AOE attack"; COLOSIM "const isAdjacent = Math.abs(dx) <= 1 && Math.abs(dy) <= 1; this.hasLOS = isAdjacent" and "const inRange = this.hasLOS || this.forceAttack === Attacks.PHASE_TRANSITION"
- FACT | must be stationary to attack (colosim) | COLOSIM:js/mobs/SolHeredit.ts | "if (inRange && this.attackDelay <= 0 && this.stationaryTimer > 0)"
- FACT | kiting delays his attacks | W-STRAT | "If the player walks away from Sol on a tick before Sol finishes his attack cooldown, they can delay Sol's next attack."
- CONFLICT (wiki vs colosim) | immobility while attacking: wiki = 4 ticks for AoE attacks; colosim freeze = 6 (spear), 4 (shield), 5 (grapple), 5 (phase transition) | W-STRAT "Sol will be unable to move for 4 ticks starting on the tick he uses an AOE attack."; COLOSIM "this.freeze(6)" / "this.freeze(4)" / "this.freeze(5)"

### (c) Arena
- FACT | wave 12 arena = area within the four pillars, blocked off by NPCs | W-MAIN + W-STRAT | W-MAIN "the Colosseum arena is reduced to the area between the four pillars, bordered by NPCs wielding shields"; W-STRAT "groups of gladiators will barricade the centre of the arena, limiting the player's movement against him to the area within the four pillars (roughly a 16x15 arena)"
- FACT | colosim walls: local x 19-34, y 18-33 = 16x16 usable box | COLOSIM:js/Constants.ts (verified directly) | "ARENA_WEST: 19", "ARENA_EAST: 34", "ARENA_NORTH: 18", "ARENA_SOUTH: 33"
- FACT | Skairunner: 16x16 + 4 corner-jut tiles | https://oldschool.runescape.wiki/w/User:Skairunner/Fortis (salvaged) | "limiting the amount of space to a 16x16 area (with four tiles jutting out from the corners)"
- CONFLICT (wiki vs wiki/colosim) | usable dims: W-STRAT "roughly a 16x15", Skairunner 16x16 (+4 corner tiles), colosim exactly 16x16. No source says 18x18; an 18x18 env box is consistent only if its outer ring is the impassable NPC-barricade ring (usable interior 16x16).

### (d) Phases
- FACT | thresholds 90/75/50/25/10% = HP 1350/1125/750/375/150 of 1500 | W-MAIN + COLOSIM | W-MAIN "Sol Heredit transitions phases at 90%, 75%, 50%, 25% and 10% HP respectively."; COLOSIM "PHASE_TRANSITION_POINTS: [[1500, \"Let's start by testing your footwork.\"], [1350, ...], [1125, ...], [750, \"You can't win!\"], [375, \"Ralos guides my hand!\"], [150, \"LET'S END THIS!\"]]"
- FACT | unlock ladder: triple-short at 90% (phase 2), grapple at 75% (phase 3), triple-long replaces triple-short below 50% (phase 4) | W-STRAT + COLOSIM | W-STRAT "Phase 2 ... addition of arena obstacles and the triple combo special attack" / "Phase 3 starts with the addition of another new Light Beam, 6 more Molten Sands, and the grapple special attack" / Phase 4 "Triple Attack is slightly modified. The final hit in the combo is delayed by 1 tick"; COLOSIM "canSpecial && this.phaseId >= 1 && this.phaseId < 3 && [Attacks.TRIPLE_SHORT]" / "this.phaseId >= 2 && [Attacks.GRAPPLE]" / "this.phaseId >= 3 && [Attacks.TRIPLE_LONG]"
- FACT | cadence speed-up at 75% (spear 7->6, shield 6->5) | W-STRAT + COLOSIM | quotes in (a)
- FACT | each phase start drops beams + sand around the player | W-MAIN | "Each phase starts with 6 beams of light randomly placed in a 9x9 area around the player, which will spawn molten sand after 2 ticks."
- FACT | phase 5 (25%) adds nothing new except more hazards | W-STRAT | "This phase is exactly the same as Phase 4, with the addition of another new Light Beam and 6 more Molten Sands"

### (e) Grapple
- FACT | gate: below 75% HP only | W-MAIN + COLOSIM | W-MAIN "His grapple attack can only be performed below 75% HP"; COLOSIM "this.phaseId >= 2 && [Attacks.GRAPPLE]"
- FACT | mechanic: drops shield, calls out a body part, player clicks the worn item in that equipment slot within 4 ticks | W-MAIN | "he will drop his shield and call out a body part, the player will have 4 ticks to click on the item in the respective slot to parry the attack."
- FACT | exactly 5 slots: body (platebody), back (cape), hands (gloves), legs (platelegs), feet (boots), chosen uniformly at random, announced via overhead text | COLOSIM:js/mobs/SolHeredit.ts + W-STRAT | COLOSIM GRAPPLE_SLOTS = CHEST "I'LL CRUSH YOUR BODY", BACK "I'LL BREAK YOUR BACK", GLOVES "I'LL TWIST YOUR HANDS", LEGS "I'LL BREAK YOUR LEGS", FEET "I'LL CUT YOUR FEET"; "const slotIdx = Math.floor(Random.get() * Object.keys(GRAPPLE_SLOTS).length)"; W-STRAT "'I'LL CRUSH YOUR BODY!' indicates that you must defend the gear in the platebody slot" plus cape/gloves/platelegs/boots
- FACT | resolves 4 ticks after attack start; fail = unblockable 20 + rand(0..24) = 20-44; success = 0 damage; boss's next attack 7 ticks later | COLOSIM:js/mobs/SolHeredit.ts + tests | "DelayedAction(() => { ... didParry ? 0 : 20 + Math.floor(Random.get() * 25) ... new ParryUnblockableWeapon() ... { hidden: true, setDelay: 0 } ... }, 4)" and "return 7"; test "check grapple attack resets equipment interaction 4 ticks after attack"
- CONFLICT (wiki vs wiki vs colosim) | grapple fail damage cap: W-MAIN infobox "44 (Grapple)"; W-STRAT "up to 45"; colosim 20-44
- FACT | perfect parry: last-tick defend -> guaranteed max hit within 5 ticks | W-MAIN | quote in GAP 1
- FACT | post-0-HP grab fix (24 Apr 2024) | UPD-0424 (salvaged) | "Sol Heredit will no longer damage players with his 'grab' attack after he runs out of hitpoints."

### (f) Shield slam + spear slams
- FACT | Shield 1 geometry (wiki): hazard under him 7x7, then a 1-tile safe gap, then hazard covering the rest of the arena | W-MAIN | "Sol Heredit will create a 7x7 hazard under him, with a 1 tile gap between the next line of hazards which will cover the entire arena. Dodged by moving 1 tile back."
- FACT | Shield 2 (wiki): middle hazard 9x9, dodge 2 tiles back | W-MAIN | "Same as shield 1, however increasing the middle hazard's area to 9x9, which is dodged by moving 2 tiles back."
- FACT | W-STRAT framing: "large 15x15 AoE with a safe line at 9x9" (Shield 1), "safe line at 11x11" (Shield 2) | W-STRAT | verbatim
- FACT | colosim framing: full rect around boss with safe ring at Chebyshev radius 4 (first shield) / 5 (second) from boss center | COLOSIM:js/mobs/SolHeredit.ts | "doFirstShield ... this.fillRect(this.location.x - 7, this.location.y - 12, this.location.x + 12, this.location.y + 7, 4)" / second shield same rect with ", 5)"
- RECONCILIATION | all three describe the same shape: inner hazard block (7x7 / 9x9 around the 5x5 boss), one safe 1-tile ring at radius 4 (the 9x9 perimeter) or radius 5 (11x11 perimeter), hazard outside it to the arena edge. The task brief's "19x19" was an approximation; colosim's rect is 20x20 from the boss SW-corner anchor and is clipped by the 16x16 arena anyway.
- FACT | spear slams (W-STRAT): Spear 1 = "5x6 (under him + in front) with two 4x1 lines towards the player's direction. Avoided by stepping back 1 tile from his centre tile, or his corner tiles."; Spear 2 = "5x5 (under him) with three 4x1 lines towards the player's direction. Avoided by stepping back 1 tile diagonally on either side of his centre tile." | W-STRAT | verbatim
- CONFLICT (wiki vs colosim) | spear line length: W-STRAT says 4x1 lines; colosim fillLine uses LINE_LENGTH = 7 (2 lines on first spear, 3 on second, aimed at attack direction) | COLOSIM:js/mobs/SolHeredit.ts | doFirstSpear "fillRect(this.location.x, this.location.y - this.size, this.location.x + this.size, this.location.y)" + fillLine(.., 7) x2; doSecondSpear wider rect + 3 lines
- FACT | per-tile slam damage 20-44, author unsure of true min | COLOSIM:js/entities/SolGroundSlam.ts | "this.damage = 20 + Math.floor(Random.get() * 25)" with comment "up to 45? not sure what min hit is"
- FACT | telegraph: slam tile deals damage 1 tick after it appears, only to a player standing exactly on it; tile lives ~3 ticks | COLOSIM:js/entities/SolGroundSlam.ts | "if (this.age == 1)" deals damage; "this.location.x === this.to.location.x && this.location.y === this.to.location.y"; dying counter set to 2 at age 1
- FACT | wiki gives no dust->damage tick count; visual is "6x6 dust hazard under the boss" warning | W-MAIN | quote in (a); W-STRAT contains no tick number either
- CONFLICT (wiki vs wiki vs colosim) | AoE max: infobox "44 (Typeless AOE)"; W-STRAT phase-1 prose "deal up to 45 Typeless Melee damage which can be completely avoided if dodged correctly"; colosim 20-44

### (g) Beams / lasers / molten sand
- FACT | each phase start: 6 light beams random in 9x9 around player, molten sand after 2 ticks | W-MAIN | "Each phase starts with 6 beams of light randomly placed in a 9x9 area around the player, which will spawn molten sand after 2 ticks."
- FACT | sand pool damage (colosim): 5 + rand(0..4) = 5-9 typeless per tick while standing on it, active from age >= 2, no despawn (permanent) | COLOSIM:js/entities/SolSandPool.ts | "if (this.age >= 2 && player.location.x === this.location.x && player.location.y === this.location.y)"; "const damage = 5 + Math.floor(Random.get() * 5)"; "typeless", setDelay: 0; no despawn timer in file
- CONFLICT (wiki vs colosim) | sand damage per tick: W-STRAT "~6-8 damage (and increment Doom, if applicable) per tick" vs colosim 5-9
- FACT | sand persists for the whole fight | W-STRAT | "puddle of molten sand that will remain for the rest of the encounter"
- FACT | crystal/laser: spawns at phase transitions, rotates around arena edges, stops, telegraphs, then fires | W-MAIN | "Each new phase will spawn a crystal which will rotate around the edges of the arena which will stop periodically and shoot a harmless beam of light, after 4 ticks a sphere of light will be launched, dealing up to 75 damage."
- FACT | reaction window 3 ticks (2 in enrage) | W-STRAT | "If your position is targeted, you have 3 ticks to react and move out of the way" (2 ticks in enrage)
- FACT | colosim laser numbers: damage 60 + rand(0..19) = 60-79; firing cooldown 25-35 ticks, 12 in enrage; orb moveTick = 4; firing sequence counter starts at 9 with beam visible counts 6..2 and projectile at 3 | COLOSIM:js/entities/LaserOrb.ts + js/mobs/SolHeredit.ts | "const damage = 60 + Math.floor(Random.get() * 20)"; "MIN_LASER_ORB_COOLDOWN = 25; MAX_LASER_ORB_COOLDOWN = 35; ENRAGE_LASER_ORB_COOLDOWN = 12"; "firingFreeze = 9", beam shown "this.firingFreeze >= 2 && this.firingFreeze <= 6", "if (this.firingFreeze === 3)" launches
- CONFLICT (wiki vs wiki vs colosim) | laser damage: W-MAIN "up to 75"; W-STRAT "70+ damage"; colosim 60-79
- FACT | lasers are unprayable in colosim (no prayer check in LaserOrb.ts); wiki calls them "by far the most dangerous hazard" | COLOSIM:js/entities/LaserOrb.ts + W-MAIN | "These lasers must be avoided at all costs, as they are by far the most dangerous hazard during the fight."
- CONFLICT (wiki vs wiki vs colosim) | crystal count: W-MAIN "each new phase" spawns one (implies 5 by enrage); W-STRAT adds beams at phases 2, 3, 5 only (3 total); colosim spawns one per transition for toPhase 1-4 (4 total: 90/75/50/25) per salvaged "if (toPhase >= 1 && toPhase <= 4) { this.createLaserOrb(); }"
- NOTE | the task brief's "12 in enrage" misread the salvage: 12 is the enrage laser COOLDOWN in ticks, not a count of 12 lasers.

### (h) Enrage (10%)
- FACT | sand spam: 5 pools immediately, then 1 per 3 ticks on random tiles | W-STRAT + W-MAIN + COLOSIM | W-STRAT "Sol Heredit adds an initial 5 Molten Sands, and 1 extra Molten Sand every 1.8 seconds"; W-MAIN "molten sand will spawn every 3 game ticks on a random tile around the arena"; COLOSIM "this.finalPhasePoolTimer = 3" with "this.tryPlacePools(this.aggro.location.x, this.aggro.location.y, 1)"
- FACT | lasers fire more often with less reaction time: ~every 7 s (~12 ticks), reaction 2 ticks | W-STRAT + COLOSIM | W-STRAT "fire much faster and more frequently, sending out an attack approximately every 7 seconds"; COLOSIM ENRAGE_LASER_ORB_COOLDOWN = 12
- FACT | colosim final-phase pools target within +-4 tiles of the player | COLOSIM (salvaged main.js quote) | "in the final phase (150 HP) one pool spawns within +-4 tiles of the player every 3 ticks"
- FACT | no new attacks unlock at enrage; cadence/hazard pressure only | W-STRAT + COLOSIM | W-STRAT enrage text lists only sand + laser changes; colosim attack pool has no phaseId >= 5 entry

## GAP 3 — Modifier gaps

### (a) Volatility
- FACT | tier I: enemies explode on death, radius = 1 tile beyond their size | W-MODS | "Upon death, the enemy will explode one tile greater than their size."
- FACT | tier II: radius 2 tiles beyond size | W-MODS | "The explosion radius is now two tiles greater, ex. a 3x3 monster will now explode in a 7x7 radius."
- FACT | tier III: adds a temporary molten sand pool at the explosion centre | W-MODS | "The tile at the centre of the explosion now leaves behind a temporary pool of molten sand."
- UNKNOWN | explosion damage number (any tier) and exact trigger timing (death tick vs delayed) — not documented on W-MODS, W-COLO, or in search results

### (b) Reentry
- FACT | tier I: javelin specials leave a temporary sand pool at the landing tile, gone at wave end | W-MODS | "Javelins launched into the air by Javelin Colossi will now leave a temporary pool of molten sand where they land, disappearing after the wave ends."
- FACT | tier II: sand becomes permanent and covers the targeted tile + the tile south-west of it | W-MODS | "The molten sand is now permanent, and now includes the targeted tile and the tile south-west of it."
- FACT | tier III: adds the tile west of the targeted tile | W-MODS | "The tiles where molten sand is left behind now includes the tile west of the targeted tile."
- NOTE | per-tick sand damage is the molten-sand standard (W-STRAT "~6-8 ... per tick" / colosim 5-9); Reentry tiers scale tile count and persistence, not damage.

### (c) Skipping a modifier offer
- FACT | selection is mandatory to proceed; player is frozen until picking | W-COLO + W-STRAT | W-COLO "Before progressing to the next wave, players must choose one of three modifiers that will make subsequent waves harder to clear."; W-STRAT "After the first wave is cleared, the player will not be able to move when Minimus appears until they make a selection."
- UNKNOWN | any skip/decline mechanic — none documented anywhere; the only documented alternative is leaving the Colosseum with banked rewards between waves

### (d) Quartet on wave 12
- FACT | YES: with Quartet, one Fremennik warbander spawns at the start of the Sol fight | W-STRAT + LlemonDuck plugin (salvage-VERIFIED 3/3) | W-STRAT "a random Fremennik warband member will spawn at the start of the fight and should be dealt with immediately"; plugin WaveSpawns wave 12 = 1 SOL_HEREDIT + 1 FREMENNIK iff QUARTET
- UNKNOWN | its spawn tile within the reduced arena — no source states it

### (e) Totemic
- FACT | totem appears when an enemy drops to <= 50% HP; heals target 30% of its health "every few ticks"; totem has 1 HP (attackable); respawns 2 minutes after destruction, or stops with the enemy's death | W-MODS | "When an enemy is reduced to 50% hitpoints or below, a healing totem will appear ... healing them for a 30% their health every few ticks. They have 1 Hitpoint and will respawn two minutes after being destroyed, or after the enemy dies"
- FACT | 10 Apr 2024 values current: heal 30% (was 40%), respawn 2 min (was 1 min) | UPD-0410 (salvaged) | "Increased respawn timer from 1 minute to 2 minutes after a totem has been destroyed. Reduced healing from 40% to 30% of the targeted NPCs health."
- FACT | wave 12 special-case: totems start when Sol hits 50% HP and heal him 75 HP every 4.2 s (= 7 ticks) | W-STRAT | "If Totemic is active, the totems will begin spawning when Sol Heredit reaches 50% of his health and will heal him for 75 hitpoints every 4.2 seconds if given the chance until destroyed."
- UNKNOWN | exact heal pulse period in ticks for waves 1-11 ("every few ticks" is all the wiki gives)

### (f) Bees!
- FACT | movement: drifts toward the player, moving every 12 ticks | W-MODS | "A Bee Swarm will drift around the arena, slowly converging on the player at a speed of 12 ticks"
- FACT | contact effect: standing on the swarm's tile = up to 10 unblockable poison damage per tick | W-MODS | "If beneath the player, it will deal up to 10 unblockable poison damage every tick"
- FACT | 1 HP, respawns 30 s (= 50 ticks) after death | W-MODS + UPD-0410 (salvaged) | "They have 1 Hitpoint and will respawn 30 seconds after being killed"; UPD-0410 "Increased respawn delay from 18 ticks (~11s) to 50 ticks (30s)" and "Increased time between movements from 7 ticks (4.2s) to 12 ticks (7.2s)"
- FACT | tier II/III: 2/3 swarms | W-MODS | "The amount of bee swarms is increased to two." / "... to three."

### (g) Solarflare
- FACT | tier I: one damaging orb circles the pillars, moves every 2 ticks, pauses 7 ticks at corners | W-MODS | "A damaging orb circles around the pillars, moving every 2 ticks, then stopping for 7 ticks when it reaches a corner."
- FACT | tier II: moves every 2 ticks with no corner pause, "deals more damage" | W-MODS | "The orb now moves every two ticks without stopping, and deals more damage."
- FACT | tier III: moves every tick, 2-tick corner pause, disables prayers on hit | W-MODS | "The orb now moves every tick, stopping for 2 ticks when it reaches a corner. It will also now disable prayers if hit."
- FACT | wave 12: orbs rotate in a 5x5 pattern at the pillar corners of the reduced arena | W-STRAT | "Solarflare orbs will continually rotate in a 5x5 pattern by the corners of the improvised arena."
- UNKNOWN | orb contact damage numbers at any tier — not documented (wiki only says tier II "deals more damage")

### (h) Doom
- FACT | one stack per damage instance taken (hitsplat-indicated) | W-MODS | "A stack of Doom is gained whenever damage is taken, indicated by a hitsplat."
- FACT | death at 15/10/5 stacks (tier I/II/III) regardless of HP; stacks clear on wave completion | W-MODS | "The player is killed upon gaining 15 stacks. Stacks of doom are cleared after completing a wave." / "... 10 stacks." / "... 5 stacks."
- FACT | only Colosseum-sourced damage counts (self-damage exempt since 10 Apr 2024) | UPD-0410 (salvaged) | "Doom stacks will only be applied by Colosseum sources. Instances of self-dealt damage like the Soulreaper Axe or Divine Potions will no longer add Doom stacks."
- FACT | molten sand increments Doom per damaging tick | W-STRAT | "~6-8 damage (and increment Doom, if applicable) per tick"
- FACT | nothing reduces stacks mid-wave — no reduction mechanic documented in any source; reset happens only at wave completion | W-MODS | quote above

## GAP 4 — Sol stats + patch history

- FACT | infobox: HP 1500, combat 1563, size 5x5, NPC ID 12821, attack style "Various", attack speed "Varies" | W-MAIN | infobox params; max hits "44 (Typeless AOE), 44 (Grapple), 15-25-35 (Triple Parry 1), 15-30-45 (Triple Parry 2)"
- FACT | combat levels: Attack 350, Strength 400, Defence 200, Magic 300, Ranged 350 | W-MAIN | infobox "Att 350, Str 400, Def 200, Mage 300, Range 350"
- FACT | aggressive bonuses: attack +250, strength +5, ranged +150, ranged str +5, magic +0 | W-MAIN | infobox bonus params (attbns 250, strbns 5, arangebns 150, rngbns 5, amagic 0)
- FACT | defensive bonuses: stab +65, slash +5, crush +30, magic +750, ranged light/standard/heavy +825 each | W-MAIN | infobox "DStab 65, DSlash 5, DCrush 30, DMagic 750, DLight 825, DStandard 825, DHeavy 825"
- FACT | slash is the weakest defence; recommended style | W-STRAT | "Sol is weak to slash, making the scythe of Vitur the strongest option; a soulreaper axe, blade of Saeldor, or abyssal tentacle are acceptable alternatives."
- FACT | no elemental weakness; immune to poison (100%), venom (100%), freeze (100%), thralls | W-MAIN | infobox immunities
- FACT | real attack cadence behind "Varies": spear 7 ticks (6 below 75%), shield 6 (5 below 75%), triple 12 (11 in 50-75% band), grapple 7 | COLOSIM (see GAP 2a) | tick gaps are between consecutive boss attacks
- FACT | 20-22 Mar 2024 launch-window hotfix: Sol's spear/shield range EXTENDED | UPD-0320 | "Extended the range of Sol Heredit's Spear Strike and Shield Slam attacks."
- FACT | 20-22 Mar 2024: death fee lenience raised 50 -> 100 waves | UPD-0320 | "Increased the lenience of the reduced Colosseum death fees. Currently, your fee is reduced by 75% until you've completed 50 waves, we're looking to up this to 100 waves for now."
- FACT | 24 Apr 2024: grab-after-death fix | UPD-0424 (salvaged) | "Sol Heredit will no longer damage players with his 'grab' attack after he runs out of hitpoints."
- FACT | 15 May 2024: arena tile walkability fix, no mechanics detail given | UPD-0515 | "Prevented certain tiles in the Fortis Colosseum from blocking players who tried to walk on them." (post contains no other Colosseum/Sol items)
- FACT | no other Sol balancing found in Apr-Jun 2024 windows searched (searches over wiki update namespace surfaced only the above plus the salvaged 3/10/24 Apr posts)

---

## UNKNOWN LIST

1. Volatility explosion damage (any tier) and trigger timing relative to the death tick.
2. Solarflare orb contact damage numbers (any tier); magnitude of tier II's "more damage".
3. Totemic heal pulse period in ticks for waves 1-11 (wiki: "every few ticks"; wave 12 is pinned at 7 ticks / 75 HP).
4. Quartet wave-12 warbander spawn tile within the reduced arena.
5. Any modifier-offer skip mechanic (none documented; selection gates progression).
6. Exact dust-telegraph tick count from the wiki (colosim says damage 1 tick after tile appears; wiki gives no number).
7. Sol grapple perfect-parry input-timing definition in ticks ("last possible tick" of the 4-tick window is all the wiki gives).
8. Crystal/laser orb count by enrage (3 vs 4 vs 5 — three sources disagree, see GAP 2g conflict).
9. True min hit of slam tiles and grapple (colosim author comment "up to 45? not sure what min hit is"; wiki caps conflict 44 vs 45).
10. Whether Sol repositions (jumps to center) during phase transitions (transition animation IDs exist; no source describes movement during it; colosim freezes him 5 ticks in place).
11. Exact one-tick convention for parry prayer input (wiki "on the tick before he lands" vs colosim hit-tick prayer check with 3/2/2-3-tick prior-off window).
