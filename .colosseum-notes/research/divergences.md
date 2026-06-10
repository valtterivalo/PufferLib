# Fortis Colosseum sim vs research: authoritative divergence list

Reconciliation of claims-ledger.md (sim @ befefc9cc, verified = worktree HEAD) against
salvaged-workflow-claims.md, local-refs-findings.md, web-spawns-npcs.md, web-sol-mods.md.
This is the work order for the implementation pass. Sections: A WRONG, B MISSING,
C CONFIRMED, D STILL-UNKNOWN. A and B ordered by gameplay impact (5 = changes
difficulty/strategy fundamentally).

Source keys: WSN = web-spawns-npcs.md, WSM = web-sol-mods.md, LRF = local-refs-findings.md,
SWC = salvaged-workflow-claims.md. Ledger refs are section numbers in claims-ledger.md.
File keys (hdr/model/spawn/move/combat/boss/mods/pact/step) as in the ledger, all under
`ocean/osrs/encounters/` (+ `colosseum/`).

## Coordinate frames (read first)

- WORLD = region-7216-local + (1792,3072). Region 7216 base = world (1792,3072).
- los.colosim frame: losX = regionX − 16, losY = 51 − regionY (los y+ = SOUTH). Transform
  cross-verified against the wiki deadzone tile markers with 0 mismatches (WSN#GAP1).
- SIM-LOCAL = the sim's abstract [0,33]² grid. The viewer anchors local (0,0) at world
  (1807,3089) (osrs_visual.c:185-187, 1114-1121), so sim-local (x,y) ↔ world (1807+x, 3089+y),
  with local y+ = world NORTH.
- Researched playable square = the los 34x34 grid = world (1808,3090)-(1841,3123). The
  (1807,3089)-(1841,3124) extent in activity_spawns.bin is the deadzone-OUTLINE bounding box
  (35x36 incl. the unwalkable rim), not the playable grid. The sim's viewer anchor therefore
  sits one tile SW of the playable square; re-anchor local (0,0) to world (1808,3090) (or
  shift all researched coords by (−1,−1)) during implementation. DONE in P1: the sim and
  the viewer now anchor (0,0) at world (1808,3090). All sim-local coords below
  use the CURRENT anchor (1807,3089).
- Orientation wart: under the viewer anchor, sim GATE_NORTH_Y=4 renders at world y 3093,
  i.e. on the SOUTH side. The sim's north/south labels are inverted in the world frame.

---

## A. WRONG — sim value contradicted by research

Format: ledger | sim value (file:line) | correct value | source | confidence | impact.

### A1. Sol attack cadence + selection model (ledger 6.3, 6.4, 6.9-part)
- Sim: new special rolled every 5 ticks (COLO_SOL_ATTACK_INTERVAL, model:545); iid roll
  35% triple / 25% grapple / 40% AoE (boss:375-414); style flips spear↔shield p=0.6.
- Correct: per-attack next-attack delays — spear 7t (6t below 75% HP), shield 6t (5t below
  75%), triple 12t (11t in the 50-75% band), grapple 7t. Attack pool = {Spear x2, Shield x2}
  + gated specials; specials require 2 normal attacks between them (SPECIAL_ATTACK_COOLDOWN=2).
  First attack of the fight AND the first attack after every phase transition is forced Spear.
  Variant alternation: consecutive same-type attacks alternate 1→2; switching type resets to
  variant 1 (sim's repeat→other-sub-pattern matches; the 0.6 flip bias and the flat 5t clock do not).
- Source: WSM#GAP2a (colosim SolHeredit.ts + tests; W-STRAT "speed up by 1 tick each after
  he goes below 75%"; W-MAIN "always use a Spear attack first after phase transitions").
- Confidence: multi-source (delays/cooldown colosim-only but corroborated in shape by wiki).
- Impact: 5.

### A2. Sol is stationary (ledger 6.7)
- Sim: can_move=0, fixed at [17,21]² all fight (model:213, boss).
- Correct: Sol chases the player (pathfinds toward aggro), must be ADJACENT to the player at
  the start of a tick to initiate an AoE, must be stationary to attack, and is frozen in
  place for the attack duration (tick counts conflict → D17). Kiting on the cooldown tick
  delays his next attack.
- Source: WSM#GAP2b (colosim getNextMovementStep + W-STRAT "Sol must be next to the player at
  the start of a tick (before movement is calculated) to initiate an AOE attack", "If the
  player walks away ... they can delay Sol's next attack").
- Confidence: multi-source.
- Impact: 5. The entire boss dance (kite between attacks, melee uptime windows) depends on it.

### A3. Sol AoE shapes + damage model (ledger 6.8, 6.11)
- Sim: solid centred squares Spear1 6x6, Spear2 6x6, Shield1 7x7, Shield2 9x9; flat 44 typeless
  if inside at resolve (model:459-468, boss:80-147).
- Correct shapes: Spear1 = 5x6 hazard (under him + in front) + TWO 1-wide lines toward the
  player's direction (wiki 4 long, colosim 7 → D14); dodge = 1 tile back from his centre/corner
  tiles. Spear2 = 5x5 under him + THREE lines; dodge = 1 tile back diagonally. Shield1 = 7x7
  hazard under him, then a 1-tile SAFE RING at Chebyshev radius 4 from boss centre (the 9x9
  perimeter), then hazard covering the REST of the arena; dodge = step 1 tile back onto the ring.
  Shield2 = same with 9x9 inner block, safe ring at radius 5 (11x11 perimeter), hazard outside;
  dodge = 2 tiles back. W-STRAT framing: "large 15x15 AoE with a safe line at 9x9 / 11x11".
- Correct damage: rolled per tile, colosim 20 + rand(0..24) = 20-44, only to a player standing
  exactly on a hazard tile (wiki max 44/45 conflict and true min → D13). Not flat.
- Source: WSM#GAP2f (W-MAIN + W-STRAT verbatim shapes; colosim fillRect/fillLine + SolGroundSlam).
- Confidence: multi-source for shapes (three sources reconciled), single-source for the 20-44 roll.
- Impact: 5. Shield dodges become ring-steps not box-exits; spear lines punish the player's
  direction; outer shield coverage means "stand far away" is no longer safe.

### A4. NPC spawn position table (ledger 1.3, 1.4-part) — IMPLEMENTED P1
- Sim: 8 invented positions — corners (4,4),(29,4),(4,29),(29,29) + edge mids (17,4),(4,17),
  (29,17),(17,29) (model:33-37), Fisher-Yates per wave, cursor wraps, no exclusion.
- Correct: TWELVE canonical 3x3 spawn zones, anchor = SW tile of the zone (= SW tile of 3x3
  NPCs). World coords (sim-local under current anchor in parens):
  (1811,3104)(4,15) | (1817,3106)(10,17) | (1811,3109)(4,20) | (1821,3109)(14,20) |
  (1827,3109)(20,20) | (1825,3114)(18,25) | (1821,3103)(14,14) | (1827,3103)(20,14) |
  (1824,3099)(17,10) | (1832,3107)(25,18) | (1836,3109)(29,20) | (1836,3104)(29,15).
  Primary spawns cannot appear within 4 tiles (Chebyshev) of the player's position at spawn
  resolution (see B3). The warband does NOT use these anchors (it spawns centre, see B2).
  Per-slot assignment RNG unknown → D4.
- Source: WSN#GAP1 (los bundle `ao` array + b5 pastebin + wiki "12 default spawns", three-way
  cross-verified 12/12; b5 thread for SW-anchor + exclusion).
- Confidence: verified (multi-source, exact-coordinate agreement).
- Impact: 5. Spawn geometry is the wave game: aggro timing, offtick setups, safespot routes.

### A5. Warband archer/seer attack range (ledger 3.5, 3.6, 7.9-part) — IMPLEMENTED P2
- Sim: archer and seer attack_range=10, hold-at-range with LoS (model:205-206, move:87-118).
- Correct: ALL THREE warbanders only attack in MELEE DISTANCE while standing still ("They
  attack on a fixed 6 tick cycle, only attacking when they are in melee distance and are not
  moving" — stated on the berserker, archer, AND seer pages; Skairunner: "only attack when
  standing still next to the player or when frozen up to 2 tiles away"). Range 10 turns them
  into arena-wide turrets; really they are chase-and-stick melee-range attackers whose hits
  are dodged by moving (see B2).
- Source: WSN#GAP5 (wiki per-NPC pages + Skairunner).
- Confidence: multi-source.
- Impact: 5. Completely changes warband threat and the value of movement.

### A6. Quartet warbander on wave 12 + wave-12 clear condition (ledger 2.27, 2.26-part) — IMPLEMENTED P1
- Sim: extra warbander spawns on the main-arena spawn table, outside the [12,27]² clamp;
  unreachable and unkillable; all-slots-dead clear can never fire → guaranteed tick-cap loss
  whenever Quartet is active on wave 12 (spawn:122-128, move:26,48, step:145-153).
- Correct: with Quartet, "a random Fremennik warband member will spawn at the start of the
  fight and should be dealt with immediately" — INSIDE the reduced arena (exact tile → D19).
  Additionally, Sol's death ends the run (victory) regardless of a surviving warbander; the
  wave-12 win check must be Sol-death, not all-slots-dead.
- Source: WSM#GAP3d (W-STRAT + LlemonDuck WaveSpawns, salvage-verified 3/3); SWC (Sol kill
  ends run).
- Confidence: multi-source / verified.
- Impact: 5 (sim-breaking bug under an entire modifier).

### A7. Arena walkability: open 34x34 square (ledger 1.1) — IMPLEMENTED P1
- Sim: every tile in [0,33]² walkable (model:11-16).
- Correct: the 34x34 grid SIZE is right (los grid is 34x34 = world (1808,3090)-(1841,3123)),
  but the walkable area is an irregular octagon. Per-row blocked extents are pinned verbatim
  by the los `Lr` wall map (WSN#GAP1, cross-verified vs the wiki deadzone outline with zero
  mismatches): corners cut progressively (e.g. los row 0 = north inner row has only 4 walkable
  tiles, the gate flanks; rows 13-20 lose only the outermost 1-2 columns), gate gaps at
  regionX 31-34 on the north/south walls, a west entrance gap at regionX 15-16 / regionY 34-35,
  east edge fully walled. Implementation: port `Lr` (34 rows of blocked x-extents) as the
  walkability mask, re-anchored per the frame note above.
- Source: WSN#GAP1 (los `Lr` + wiki Module:Tile_markers/Colosseum.json deadzone outline).
- Confidence: verified (two independent sources, exact agreement).
- Impact: 4. Kiting space, corner traps, and gate geometry all depend on the real walls.

### A8. Wave-12 geometry: pillars, boss box, Sol spawn (ledger 1.11, 1.8, 1.9-part, 6.27-part) — IMPLEMENTED P1
- Sim: four 1x1 pillars at local (14,14),(25,14),(14,25),(25,25) two tiles inside a [12,27]²
  boss box; Sol SW at (17,17); player teleported to (19,26) (model:45-71, boss:56-75).
- Correct: pillars are 3x3 objects (obj 52490, cache-verified anchors) at world SW
  (1816,3098)(local 9,9), (1831,3098)(24,9), (1816,3113)(9,24), (1831,3113)(24,24) — 15-tile
  spacing both axes — and they FORM the reduced arena's corners. Wave-12 outer bounds
  (okronos provisional, private-server verified): world (1816,3098)-(1833,3115) = local
  (9,9)-(26,26), an 18x18 outer ring whose interior 16x16 = local (10,10)-(25,25) is the
  usable box ("roughly 16x15"/16x16 per W-STRAT/Skairunner/colosim; pillar footprints intrude
  2x2 into each interior corner → D29). Sol provisional spawn: SW (1824,3109) = local (17,20),
  npc 12821 (RuneC test-asserted). The sim's box is shifted (+3,+3) world-relative and its
  player start (19,26) = world (1826,3115) sits ON the researched north wall row.
- Source: LRF#2.2 (activity_spawns.bin rows + caveats), WSN#GAP1 (pillar anchors via los/b5),
  WSM#GAP2c.
- Confidence: verified for pillar object anchors; multi-source for 16x16; provisional
  (private-server verified) for the blocked-edge rows and Sol tile.
- Impact: 4. Pillar size 3x3 vs 1x1 changes sphere-LoS cover and pathing chokes completely.

### A9. Crystal lifecycle (ledger 6.22, 6.23, 6.24)
- Sim: one crystal, appears at <75% HP, advances 1 perimeter step/tick continuously, fires
  every 6 ticks (every 2 at enrage) while moving (boss:265-338).
- Correct: a crystal spawns at EACH phase transition starting at 90% (they accumulate; final
  count 3/4/5 conflicts → D16). Motion: moves every 4 ticks around the arena edges (colosim
  moveTick=4), STOPS periodically to fire: telegraph beam, then sphere launch (W-MAIN "stop
  periodically and shoot a harmless beam of light, after 4 ticks a sphere of light will be
  launched"). Firing cooldown 25-35 ticks per crystal, 12 ticks at enrage; player reaction
  window 3 ticks (2 at enrage).
- Source: WSM#GAP2g (W-MAIN + W-STRAT + colosim LaserOrb.ts constants).
- Confidence: multi-source for spawn-per-phase and stop-telegraph-fire; single-source
  (colosim) for 25-35/12 cooldown and moveTick 4; W-STRAT corroborates enrage ~7 s ≈ 12t.
- Impact: 4. Multiple accumulating crystals with long cooldowns ≠ one fast metronome.

### A10. Sphere damage distribution (ledger 6.25-part)
- Sim: uniform 0..75 typeless (model:553-554).
- Correct: high-min heavy roll — colosim 60 + rand(0..19) = 60-79; wiki "up to 75" / "70+
  damage" (exact range → D15). Unprayable. A 0..75 uniform makes tanking spheres viable;
  really they are near-lethal must-dodges ("by far the most dangerous hazard").
- Source: WSM#GAP2g.
- Confidence: multi-source that damage is ~60+; exact bounds conflict.
- Impact: 4.

### A11. Phase-transition beams spawn molten sand (ledger 6.21, 6.26-part)
- Sim: 6 light beams linger 4 ticks, deal nothing, purely cosmetic (boss:379-389).
- Correct: each phase start places 6 beams randomly in a 9x9 area around the PLAYER; each
  spawns a molten sand pool after 2 ticks; pools persist for the rest of the fight and deal
  5-9/tick (see A22). Enrage additionally opens with 5 immediate pools, then 1 per 3 ticks.
  Five transitions × 6 pools = up to 30 permanent hazard tiles before enrage spam.
- Source: WSM#GAP2d/g/h (W-MAIN "Each phase starts with 6 beams ... which will spawn molten
  sand after 2 ticks"; W-STRAT enrage "initial 5 Molten Sands"; colosim SolSandPool).
- Confidence: multi-source.
- Impact: 4. Arena denial is the boss fight's accumulating clock; sim has none until enrage.

### A12. Grapple slot domain (ledger 6.17-part)
- Sim: called slot uniform over all 11 equipment slots (boss:217-223).
- Correct: exactly 5 slots — body, back, hands, legs, feet — chosen uniformly, announced via
  overhead text ("I'LL CRUSH YOUR BODY" etc.).
- Source: WSM#GAP2e (colosim GRAPPLE_SLOTS + W-STRAT callout list).
- Confidence: multi-source.
- Impact: 4. Parry difficulty and the action-head design both change (5 candidates, not 11).

### A13. Minotaur heal semantics (ledger 4.18, 4.19) — IMPLEMENTED P3
- Sim: heals ALL wounded, non-full-HP NPCs (including other minotaurs) within rect distance 6
  of its footprint, 0..10 HP each per 5-tick cycle (combat:173-188).
- Correct: heals ONE eligible NPC TO FULL per heal action. Eligibility: not a minotaur, below
  75% of total HP, centre ≤7 tiles from the minotaur's centre, centre-to-centre line of sight
  (pillar-blockable). The sim's 6-from-footprint-edge ≈ 7-from-centre, so the RANGE is fine;
  amount (to full), single-target, <75% gate, non-minotaur, and LoS are all wrong/missing.
  Heal-check period not pinned → D9.
- Source: WSN#GAP6 (Minotaur page detailed passage + Strategies "to full health" + los 7-tile
  centre radius implementation).
- Confidence: multi-source (the 6 vs 7 wording conflict reconciles as edge vs centre).
- Impact: 4. Heal-to-full makes the minotaur a hard kill-priority; 0..10/5t is ignorable.

### A14. Player melee always rolls vs Sol's stab defence (ledger 6.28, 7.16-part)
- Sim: player-attack path uses stab_def for all melee (pact:135-138); Sol slash_def 5 unreachable.
- Correct: Sol is weak to SLASH (stab 65 / slash 5 / crush 30; "Sol is weak to slash, making
  the scythe of Vitur the strongest option"). The melee lookup must use the weapon's melee
  style, and the placeholder loadout needs a slash weapon for the boss meta to exist.
- Source: WSM#GAP4 (W-MAIN infobox + W-STRAT), LRF#1.3 (cache params 5/6/7 = 65/5/30).
- Confidence: verified.
- Impact: 4. Boss TTK and enrage exposure are inflated ~stab/slash accuracy ratio otherwise.

### A15. AoE telegraph window (ledger 6.10)
- Sim: 2 ticks between telegraph and resolution (model:546).
- Correct: hazard tiles deal damage 1 tick after they appear (colosim SolGroundSlam age==1;
  tile lives ~3 ticks). The effective dodge window comes from the attack animation lead-in,
  not a 2-tick tile grace. Wiki gives no number.
- Source: WSM#GAP2f.
- Confidence: single-source (colosim).
- Impact: 4. This is the boss fight's core skill check.

### A16. Modifier draft schedule + pool rules (ledger 5.3, 5.4)
- Sim: draft offered only in gaps after clearing waves 1-11 (11 picks), uniform pool over
  eligible modifiers (model:333-365, mods:38-92).
- Correct: a draft occurs BEFORE EVERY wave including wave 1 (money-making guide: "Pick the
  Blasphemy handicap and start the wave") and into wave 12 (UPD-0424 removed only Mantimayhem
  from wave-12 offers "since it doesn't do anything during the Sol Heredit fight") → 12 picks
  per full run. Pool rules: the wave-1 offer is ALWAYS {Relentless, Blasphemy, Frailty};
  Red Flag and Dynamic Duo only enter the pool before wave 7; offers are biased toward
  upgrading previously picked tier-1/2 modifiers (sim's re-offer-upgrades exists but unbiased).
- Source: SWC (W-STRAT draft rules + money-making guide + UPD-0424; tier-upgrade bias
  independently in the launch-week video).
- Confidence: multi-source for schedule and wave-1 pool; single-source-corroborated for bias.
- Impact: 4. One extra pick + fixed early pool + upgrade bias reshapes run difficulty scaling.

### A17. Gates: location and side-selection rule (ledger 1.5, 1.6, 1.7) — IMPLEMENTED P1
- Sim: gate tiles at x=17, north y=4 / south y=29; nearer gate by |player.y − gate_y|; species
  placed in a row east from x=17 (model:41-43, spawn:155-165).
- Correct: gates are wall GAPS spanning world x 1823-1826 (regionX 31-34, sim-local x 16-19)
  in the north wall (outer row world y 3124) and south wall (outer row world y 3089); inner
  rows blocked at the gap with walkable flanks. Side selection: the b5 "yellow line" at the
  regionY 33/34 boundary (world y 3105/3106, sim-local y 16/17): player at or south of the
  line → SOUTH gate, north of it → NORTH gate (a fixed threshold, not nearest-distance).
  Exact materialization tiles unknown → D2. Note the sim's north/south labels are inverted in
  the viewer frame (frame note above).
- Source: WSN#GAP1 (deadzone outline + los walls + b5 thread/image measurement).
- Confidence: multi-source for gap locations; single-source (b5, measured) for the line.
- Impact: 3.

### A18. Player wave-start tile (ledger 1.2) — IMPLEMENTED P1
- Sim: (17,5) sim-local = world (1824,3094), south-centre (model:18-19).
- Correct: the canonical start is the WEST side — los default player tile world (1815,3108)
  (sim-local 8,19); the b5 "start"/spawn-fixing tile is world (1813,3108) (local 6,19); the
  player enters via the west entrance gap. Wave-start strategy revolves around the NW/SW
  pillars adjacent to that side ("4 out of 9 spawns will see you on SW, 2 out of 9 on NW").
- Source: WSN#GAP1 (los `Bt`, b5 pastebin "start", wiki tile-marker pack).
- Confidence: multi-source.
- Impact: 3. Start tile + 4-tile exclusion together set which anchors can spawn on top of you.

### A19. Manticore effective cycle 12t → 10t (ledger 4.26) — IMPLEMENTED P3
- Sim: attack_timer reset to 10 AFTER orb 3 fires → 12-tick barrage-to-barrage period
  (combat:276-279).
- Correct: 10-tick full cycle ("full 10-tick charge-up"; Skairunner: 7-tick charge-up + 3
  attacks in 3 ticks = 10). Anchor the reset at barrage START (or 7-tick recharge after the
  last orb).
- Source: SWC (Manticore page + Skairunner).
- Confidence: multi-source.
- Impact: 3 (~17% manticore DPS understated).

### A20. Triple-parry combo selection (ledger 6.14-part)
- Sim: combos A (15/25/35) and B (15/30/45) strictly alternate per invocation (boss:183-186).
- Correct: phase-gated, not alternating — 15/25/35 ("Triple Parry 1", colosim TRIPLE_SHORT)
  while HP is 50-90%; 15/30/45 ("Triple Parry 2", TRIPLE_LONG, with the +4 third gap) REPLACES
  it below 50%. Damage values and hit ticks themselves are confirmed (C).
- Source: WSM#GAP1/GAP2d (W-MAIN + W-STRAT + colosim attack-pool gating).
- Confidence: multi-source.
- Impact: 3.

### A21. Totemic totem entity model (ledger 5.24-part)
- Sim: totem destroyed by player adjacency (rect distance 1), no HP, never respawns
  (mods:195-240).
- Correct: the totem is an attackable NPC (id 12825) with 1 HP, killed by an attack, and it
  RESPAWNS 2 minutes (200 ticks) after destruction, or stops with the owner NPC's death.
  The ≤50% trigger and 30% heal are confirmed (C); heal pulse period → D22; wave-12 Sol
  behavior → B5.
- Source: WSM#GAP3e (W-MODS + UPD-0410), LRF#1.5 (Healing totem hitpoints 1).
- Confidence: verified.
- Impact: 3. Adjacency-destroy removes the attack-or-ignore decision and the 2-min re-threat.

### A22. Molten sand: damage and lifetimes (ledger 5.26, 5.17-part, 5.25-part, 6.26-part)
- Sim: all pools deal flat 12/tick; Reentry T1 pool lives 8 ticks; Volatility T3 pool 10
  ticks; pools never cleared between waves (model:435-437, mods:246-306).
- Correct: standard molten sand deals ~6-8/tick (W-STRAT) / 5 + rand(0..4) = 5-9 (colosim) —
  adopt 5-9. "Temporary" pools (Reentry T1, Volatility T3) last UNTIL WAVE END, not 8/10
  ticks. Reentry T2 = permanent AND covers the targeted tile + the tile SOUTH-WEST of it
  (sim is missing the SW tile); T3 adds the WEST tile (sim has this). Sol-fight sand is
  permanent for the fight (sim's enrage permanence ✓ but at 12/tick → 5-9).
- Source: WSM#GAP2g/GAP3a/b (W-MODS verbatim tiers; W-STRAT damage; colosim SolSandPool).
- Confidence: multi-source for lifetimes/tiles; multi-source for the 5-9 damage band.
- Impact: 3. Sim sand is ~2x too hot but vanishes too fast; threat profile inverted.

### A23. Relentless bypass basis (ledger 5.19-part)
- Sim: bypass scales the player's whole defence ROLL (level+bonus product) (mods:124-131).
- Correct: bypasses 33/66/100% of the player's Defence LEVEL (gear defence bonuses intact):
  "Enemy attacks will now bypass 33% of the player's Defence level". Max-hit +1/+3/+6 and
  T3 ignore-accuracy are confirmed (C).
- Source: SWC (W-MODS + W-STRAT identical phrasing + UPD-0403).
- Confidence: multi-source (consistent wiki phrasing; no contrary source).
- Impact: 2.

### A24. Mantimayhem T3 orb randomization (ledger 5.14-part)
- Sim: all 3 orb styles independently uniform-random (can roll 3x melee) (mods:170-175).
- Correct: T3 randomizes the MELEE ORB'S POSITION in the sequence — a shuffle of the fixed
  {magic, ranged, melee} set, one of each ("Melee orbs can appear anywhere in the sequence";
  "removes the 'forced' Melee orb in the final slot"; video: "randomly shuffles up the order").
- Source: LRF#4.1 (cache struct tier text), SWC (UPD-0410 + videos).
- Confidence: multi-source.
- Impact: 2.

### A25. Mantimayhem venom model (ledger 5.15)
- Sim: venom starts 6, ticks every 5 ticks, DECAYS by 1 to 0; reapplication resets to 6
  (model:454-456, mods:178-189).
- Correct: standard OSRS venom ESCALATES — starts 6, +2 per subsequent proc up to cap 20,
  damage every 30 ticks (18 s). No colosseum-specific override exists in any source.
- Source: ledger's own researcher note + standard OSRS venom mechanics (not colosseum-fetched;
  the cache/wiki tier text says only "apply Venom on hit").
- Confidence: multi-source for the general mechanic; the colosseum applying STANDARD venom is
  the natural reading.
- Impact: 2 (current model is both faster and far weaker).

### A26. Dynamic Duo pair placement (ledger 5.12-part) — IMPLEMENTED P3
- Sim: second Shockwave Colossus takes the next shuffled spawn-table position (spawn:117-119).
- Correct: "The paired Colossus spawns near the main Colossus, but not necessarily on one of
  the 12 default spawns" — place it adjacent/near its partner, off-anchor allowed.
- Source: WSN#GAP1 / WSM (W-MODS verbatim).
- Confidence: single-source (wiki, explicit).
- Impact: 2.

### A27. Solarflare path nodes + T3 corner stop (ledger 5.20, 5.21-part)
- Sim: pre-boss ring nodes = the 4 arena spawn corners; T3 moves every tick with NO corner
  pause (mods:358-414).
- Correct: the orb circles THE PILLARS — which exist on all waves (B1) at the researched 3x3
  positions; T3 moves every tick AND stops 2 ticks at corners. Wave-12 5x5 pattern by the
  pillar corners ✓ (C). T1/T2 cadence ✓ (C).
- Source: WSM#GAP3g (W-MODS verbatim), WSN#GAP1 (pillar positions).
- Confidence: multi-source.
- Impact: 2.

### A28. Grapple fail damage (ledger 6.20-part)
- Sim: flat 44 typeless on timer expiry (model:548, boss:249-260).
- Correct: rolled — colosim 20 + rand(0..24) = 20-44 (wiki cap 44/45 conflict → D13),
  resolving 4 ticks after attack start (window ✓ C); 0 on successful parry.
- Source: WSM#GAP2e.
- Confidence: single-source for distribution, multi-source for the cap region.
- Impact: 2.

### A29. Wave roster cap 8 (ledger 2.28) — IMPLEMENTED P1
- Sim: COLO_MAX wave-roster entries = 8 (model:262-263).
- Correct: must hold ≥9 initial spawns — wave 8 or 11 with Quartet + Dynamic Duo = 3 warband
  + 1 quartet + 2 javelin + 1-2 manticore + 2 shockwave = 9. Derived from the verified
  composition rules.
- Source: SWC (LlemonDuck WaveSpawns, verified 3/3).
- Confidence: verified (derived).
- Impact: 2 (overflow/dropped-spawn bug under modifier combos).

### A30. Red Flag implementation (ledger 5.18) — IMPLEMENTED P2 (BFS routefinding; viewer def-id 12813 deferred, no exported model)
- Sim: blocked minotaur takes a 1-tile orthogonal sidestep (move:61-80).
- Correct: Red Flag swaps the minotaur to a dedicated ROUTEFINDING variant (distinct NPC id
  12813, COLOSSEUM_MINOTAUR_ROUTEFIND) with real pathing around obstacles — "impossible to
  safespot". Meaningless until pillars exist (B1); then it needs actual routefinding (BFS),
  not a sidestep heuristic.
- Source: SWC (verified 3/3: plugin + gameval + wiki), LRF#1.1.
- Confidence: verified.
- Impact: 2 (5 once B1 lands; gates the pillar-safespot strategy).

### A31. Warband offensive-bonus table skew vs cache (ledger 3.1, 3.2-part) — IMPLEMENTED P2
- Sim: archer melee_str_bonus=150 (model:151) vs cache param 10 = 0; berserker/archer
  magic_att_bonus=150 (model:146,151) vs cache param 3 = 0 (dps-calc carries the 150s; LRF
  recorded the divergence). Combat-irrelevant today (archer never melees, berserker never
  casts) but the table claims cache authority.
- Correct: align to cache params when touching the table (archer melee str 0; magic att 0 for
  berserker/archer).
- Source: LRF#1.3/1.4.
- Confidence: verified (cache decode).
- Impact: 1.

---

## B. MISSING — real mechanics absent from the sim

Cross-refs for items the task brief expected here: Sol chasing/movement → A2 (asserted
stationary, so it is a contradiction, with the movement system to build); Quartet-on-12
exists but is broken → A6; Bees under-player damage already exists (ledger 5.9) → C; the sim
already carries post-3-April-2024 warband stats, so there is no launch-stat skew to fix → C.

### B1. The four arena pillars on waves 1-11 (+ routefinding/LoS) (ledger 1.12 omission) — IMPLEMENTED P1 (pillars + LoS + attack gating; NPC routefinding landed in P2: warband + Red Flag minotaur)
- Missing: four 3x3 pillars (obj 52490) at world SW (1816,3098), (1831,3098), (1816,3113),
  (1831,3113) (sim-local (9,9),(24,9),(9,24),(24,24)) exist on EVERY wave. They block movement
  and line of sight. Consequences the sim currently cannot express: pillar safespotting vs
  ranged NPCs (the core wave strategy), ranged/magic NPCs repositioning for LoS, b5's "3 ticks
  of movement to get behind pillar before anything attacks", minotaur heals blocked by pillar
  LoS (A13), Red Flag's entire reason to exist (A30), Solarflare orbiting the pillars (A27),
  NW/SW-pillar wave-start strategies (A18). Requires NPC pathing around obstacles (warband has
  routefinding natively; others path simply and CAN be safespotted).
- Source: WSN#GAP1 (los `Qe` + b5 + wiki tile markers), LRF#2.2 (cache object anchors),
  wiki strategies throughout.
- Confidence: verified.
- Impact: 5. The single largest structural gap in the sim.

### B2. Warband shared cycle, attack-skip, formation, centre spawn (ledger 3.10, 3.4-part, 7.20-part) — IMPLEMENTED P2 (centre spawn was P1)
- Missing: (a) the trio attacks on ONE shared 6-tick cycle anchored to wave start — berserker
  at N+1, seer N+2, archer N+3, then 3 silent ticks (launch-week writeup: wave starts 6 ticks
  after the start click; first berserker window 1 tick after spawn — not after a full 6-tick
  timer as the sim has it); (b) a member scheduled to attack while the player IS MOVING skips
  entirely until the next cycle (+6t) — movement is the dodge mechanic; (c) formation: the
  berserker prioritises standing NORTH of the player, seer EAST, archer WEST (Quartet's 4th
  member SOUTH, diamond); (d) the warband spawns at the ARENA CENTRE (ranger within the b5
  7x7 box regionX 28-34 / regionY 33-39, world (1820,3105)-(1826,3111), the others offset)
  and immediately "darts" toward the player — it does not use the 12 anchors; (e) members
  have native routefinding. Hit-and-run can desync the cycle (wiki notes off-sync risk).
- Source: WSN#GAP5 (Fremennik warband page + Strategies + b5 + writeup image), SWC.
- Confidence: multi-source (cycle order/skip on the wiki proper; wave-anchoring from the
  launch-week writeup, post-April survival → D, but the in-cycle structure is current wiki).
- Impact: 5. With A5, the warband becomes a movement puzzle instead of three turrets.

### B3. Spawn-anchor resolution semantics: player-proximity exclusion (ledger 1.4 omission) — IMPLEMENTED P1
- Missing: primary wave spawns cannot appear within 4 tiles (Chebyshev radius 4, a 9x9 box)
  of the player's position at spawn resolution; standing on the b5 tile (1813,3108) suppresses
  exactly 3 of the 12 anchors ((1811,3104),(1811,3109),(1817,3106)) → 9 candidate slots,
  matching the "4 out of 9 / 2 out of 9 see you on spawn" guide arithmetic. NPC zones place
  the NPC's SW tile on the anchor. Player position on the resolution tick matters (the
  5th-tick reposition tech in W-STRAT). Per-anchor assignment RNG → D4.
- Source: WSN#GAP1 (b5 thread verbatim + measured exclusion box + guide probabilities).
- Confidence: multi-source.
- Impact: 5. Spawn fixing/manipulation is a real, documented player skill the sim must allow.

### B4. Sol parry prayer punish layer (ledger 6.15 extension)
- Missing: (a) EARLY-PRAYER PUNISH — having an overhead protect prayer on during the pre-hit
  window makes that parry hit UNBLOCKABLE ("Turning on your prayer early ... will turn it
  off, forcing the player to take a hit"; colosim lookback windows 3/2/2-3 ticks); (b) every
  parry hit DEACTIVATES all three overhead protect prayers, forcing a re-flick per hit.
  The sim's plain prayer-check-at-land lets the agent camp Protect from Melee through the
  whole combo for free.
- Source: WSM#GAP1 (W-MAIN + colosim, agreeing on both rules).
- Confidence: multi-source.
- Impact: 4. Without this, the triple parry is trivial instead of the fight's signature check.

### B5. Totemic wave-12: totems heal Sol (ledger 5.24 "never Sol" contradicted)
- Missing: with Totemic active, totems begin spawning when SOL reaches 50% HP and heal him
  75 HP every 4.2 s (7 ticks) until destroyed. The sim excludes Sol from Totemic entirely.
- Source: WSM#GAP3e (W-STRAT verbatim).
- Confidence: single-source (explicit).
- Impact: 4. A boss DPS check the modifier is supposed to impose.

### B6. Mandatory modifier pick, player frozen until selected (ledger 5.7 contradicted)
- Missing: selection is mandatory to proceed; after a wave clears the player CANNOT MOVE when
  Minimus appears until a pick is made. No skip mechanic exists anywhere. The sim's skip
  action (pact:182-186) and auto-close window are training affordances that diverge from the
  real decision structure (and interact with A16's 12-draft schedule).
- Source: WSM#GAP3c (W-COLO + W-STRAT).
- Confidence: multi-source.
- Impact: 4.

### B7. Perfect-parry guaranteed-max consumption (ledger 6.19)
- Missing: the `next_attack_guaranteed_max` flag is set and decremented but never read by the
  player-attack path — the advertised reward (next attack within 5 ticks is a guaranteed max
  hit) has zero effect. Wire it into pact's damage roll.
- Source: WSM#GAP1 (W-MAIN; mechanic confirmed real, grapple-only); sim-internal wiring gap.
- Confidence: verified (mechanic), verified (unwired, per ledger audit).
- Impact: 3.

### B8. Reinforcement gate-entry geometry + shaman quirks (ledger 1.7-part, 2.21) — IMPLEMENTED P1 (gate-entry geometry; shaman quirks pending)
- Missing: reinforcements stream in THROUGH the wall gaps (world x 1823-1826) on the north or
  south wall (selection rule → A17), not teleport onto an interior row. Exact materialization
  tiles unpinned → D2. The serpent-shaman reinforcement is a distinct variant (los id 6) that
  "wiggles" differently from wave-start shamans (rule unknown → D24), and a shaman
  reinforcement spawns in the ARENA CENTRE if the player camps the gate spawn (mechanics.md
  §3.3, corroborated by the Serpent shaman page fetch).
- Source: WSN#GAP1/GAP3.
- Confidence: multi-source for gates; single-source for the centre-spawn quirk.
- Impact: 3.

### B9. Bees are killable: 1 HP + 50-tick respawn (ledger 5.9-part)
- Missing: bee swarms have 1 Hitpoint, can be attacked/killed, and respawn 50 ticks (30 s)
  after death (UPD-0410 values current). The sim models them as unkillable movers, removing
  the kill-the-bee tempo decision. Movement (12t) and ≤10/t under-player damage are ✓ (C).
- Source: WSM#GAP3f (W-MODS + UPD-0410), LRF (Bee Swarm npc 12823).
- Confidence: verified.
- Impact: 2.

### B10. Manticore pair pattern-copy (absent; relates ledger 4.24) — IMPLEMENTED P3
- Missing: from wave 9, when one manticore selects its orb pattern, another within 15 tiles
  WITH LoS copies that pattern. Affects double-manticore flick planning alongside the
  confirmed 5-tick stagger.
- Source: SWC (Manticore page verbatim).
- Confidence: single-source.
- Impact: 2.

### B11. Glory economy (ledger 5.8 omission)
- Missing: per-modifier glory values exist (cache struct param 1903: 100-250 per modifier;
  varps 4130-4132 track glory). Only matters if reward shaping wants the real incentive
  gradient; optional for RL.
- Source: LRF#4.1/4.2.
- Confidence: verified.
- Impact: 1.

### B12. Sol 20-22 Mar 2024 spear/shield range hotfix (patch-history check)
- Missing as a concern, not as work: "Extended the range of Sol Heredit's Spear Strike and
  Shield Slam attacks" (UPD-0320). All researched AoE geometry (W-MAIN/W-STRAT/colosim)
  postdates the hotfix, so implementing A3's shapes satisfies it. No separate change; listed
  so the implementation pass does not source pre-hotfix geometry from launch-week material.
- Source: WSM#GAP4.
- Confidence: verified.
- Impact: 1.

---

## C. CONFIRMED — sim values the research independently corroborates

Arena/waves:
- 12 waves, wave 12 = Sol Heredit (2.1); 34x34 grid SIZE (1.1-part, los grid).
- Wave compositions 1-12 exactly as modeled, incl. javelin skip-1-and-4 / odd-even cadence,
  manticore single 4-8 double 9-11, shockwave 7/8/11 (2.2-2.13) — salvage-VERIFIED 3/3
  (LlemonDuck WaveSpawns + wiki).
- Reinforcement species schedule: jaguar W1-3, jaguar+shaman W4-6, minotaur W7-9,
  minotaur+shaman W10-11, none W12 (2.14-2.18) — VERIFIED 3/3.
- Reinforcements at 40 s, gate chosen by player position, preventable by fast clear (2.19
  seconds-part; tick-exactness → D1).
- Wave-clear = all NPCs dead incl. reinforcements (2.22, "Kill reinforcements" procedure).
- 6-tick wave-start ready delay matches the community writeup's click+6 (2.24, single-source).
- Episode NPC ids / cache def ids (model COLO_NPC_DEF_IDS) — cache-verified (LRF#1.1/1.2).

NPC stats (all cross-checked cache + dps-calc + wiki infobox; sim table is CURRENT
post-3-April-2024 — berserker max 29 / str+90, archer ranged 110 / +150 / +10 — no launch
skew anywhere):
- Berserker/archer/seer stat rows incl. hp 48/50/50, defences, 6-tick speed (3.1-3.3; cache
  param 14=5 footnote → D30; offensive-bonus skew → A31).
- Warband styles, stab/ranged/magic, RPS weakness triangle (3.4-part, 3.7).
- RPS implementation: weakness style = guaranteed hit AND guaranteed max (3.8) — wiki states
  exactly this semantics.
- Serpent shaman: stats, Water Surge magic, range 10, 5t, max 28, no poison, no special,
  high accuracy (4.1, 4.2).
- Jaguar warrior: stats, size 2, slash, 5t, 3 independent hits ≤47 each, individually prayed
  (4.3-4.5).
- Javelin Colossus: stats, range 15, 5t, max 48 (Relentless 49/51/54 ladder); skyfall every
  5th attack; skyfall ignores Protect from Missiles, dodged by moving (4.6-4.8, 4.10).
- Shockwave Colossus: stats, range 15, 5t, max 56, NO special mechanics, no AoE/knockback
  (4.13, 4.14 — premise correction independently re-verified).
- Minotaur: stats, crush, 5t, max 74; melee-priority-over-heal at melee distance (incl.
  diagonal); heal RANGE ≈ sim's footprint-6 = centre-7 (4.15-4.17, 4.18 range-part);
  1-tick-delayed melee damage, tick-eatable, Vardorvis-like (4.20 minotaur-part).
- Manticore: stats, all aggressive bonuses 0, speed param 10, size 3, per-style maxes
  36/31/31; orbs melee-last with random magic/ranged lead; 3 orbs on consecutive ticks,
  individually prayed; 5-tick multi-manticore stagger; range 15; spawns uncharged (4.22-4.25,
  4.27, 4.28, 4.30) — orb-order and uncharged-spawn salvage-VERIFIED 3/3.
- Sol stat row: hp 1500, levels 350/400/200/300/350, size 5, melee att+250/str+5, ranged
  att+150/str+5, defs 65/5/30/750/825, immunities (4.31, 6.1).

Sol fight:
- Phase thresholds 90/75/50/25/10% (6.2) — wiki + colosim HP constants.
- No auto-attack outside the kit: his "normals" ARE the four AoEs (6.5 — top-suspect cleared).
- All Sol damage typeless except the parry melee hits (6.6, 4.32).
- Triple parry unlock <90% (6.12); hit timing +3/+6/+9, third +4 below 50% (6.13 — the
  wiki/colosim discrepancy is RESOLVED: identical schedules); damage sets 15/25/35 and
  15/30/45 (6.14 values; selection → A20); prayer checked at land, flickable, "tick before
  he lands" convention (6.15; punish layer → B4, input convention → D18).
- Grapple unlock <75% (6.16); 4-tick window (6.17 window-part); perfect parry on the last
  tick → guaranteed max within 5 ticks (6.18; consumption wiring → B7).
- Sphere flight 4 ticks (6.25-part, 7.7); pillar LoS blocks spheres (6.25-part).
- Enrage: sand every 3 ticks, permanent for the fight (6.26 cadence/permanence; damage → A22).
- Boss arena usable size 16x16 (1.9/6.27 size; placement → A8).
- Four pillars exist on wave 12 and block LoS (1.11 count/function; size/position → A8).
- No "Last Recall"/shield-bash knockback exists (6.30) — no source describes one.
- Win = Sol's death ends the run (6.29; wave-12 clear predicate → A6).

Modifiers:
- Exactly 14 modifiers, the sim's list (5.1) — cache structs 891-915 ids 0-13, VERIFIED.
- Draft = 3 distinct options, pick 1, persists (5.2) — clientscript 4931 args, VERIFIED.
- Single-tier set {Dynamic Duo, Red Flag, Quartet, Totemic}; 10 tiered I/II/III (5.6).
- Pre-boss-only exclusions: Red Flag + Dynamic Duo "not after wave 11" (wiki verbatim),
  Mantimayhem not into wave 12 (UPD-0424) (5.5; Reentry exclusion unsourced → D31).
- Bees: 1/2/3 swarms, move every 12 ticks, ≤10 unblockable per tick beneath the player
  (5.9 core; killability → B9; spawn/diagonal details → D34).
- Blasphemy 20/40/60% prayer drain (5.10).
- Doom: death at 15/10/5 stacks, +1 per damage instance (hitsplat-indicated, hazards
  included), reset at wave completion (5.11).
- Dynamic Duo: shockwaves spawn in pairs (5.12 core).
- Frailty: max HP −10/20/40%, boost/overheal disable moot with shark-only food (5.13).
- Mantimayhem: T1 doubles projectiles per orb (same-tick pairs), T2 venom on hit that
  overheads do not prevent, T3 melee-anywhere (5.14 core; T3 distribution → A24).
- Myopia: attack range −2/4/6, floored; autocast+ranged affected, manual cast exempt (no
  manual casting in sim) (5.16).
- Reentry tier STRUCTURE: T1 temporary, T2 permanent, T3 +west tile (5.17 core; lifetime,
  SW tile, damage → A22).
- Relentless: 33/66/100% bypass + max hit +1/+3/+6, T3 ignores accuracy (5.19 numbers).
- Solarflare: T1 2t-move/7t corner stop, T2 2t continuous + more damage, T3 prayer-disable
  on contact; wave-12 5x5 pattern by the pillar corners (5.20 wave-12 part, 5.21 T1/T2,
  5.22 prayer-disable).
- Quartet: +1 uniformly random warbander every wave incl. 12 (5.23) — VERIFIED 3/3.
- Totemic: trigger ≤50% HP, heal 30% of max (5.24 core).
- Volatility: radius = size+1 (T1) / size+2 (T2+), T3 leaves a centre pool (5.25 structure).

Engine/timing:
- 0.6 s tick (7.1); standard NPC accuracy and player-defence rolls (7.12, 7.13); uniform
  0..max damage convention (7.14); handset max-hit design (7.14).
- Protect prayers block 100% of prayer-checked NPC damage in PvE; warband fully prayable
  (7.15).
- Permanent player aggro, all NPCs (7.18).
- Region identity 7216, world base (1792,3072) (frame section) — VERIFIED.

---

## D. STILL-UNKNOWN — no source pins it; modeled decisions for the implementation pass

Format: sim's current value | evidence state | recommendation (all MODELED DECISION unless noted).

1. Reinforcement timer in ticks (2.19): sim 66 | 40 s = 66.67t; no tick-exact community
   measurement; pause behavior and repeat behavior also unpinned (2.20) | KEEP 66, no
   pausing, exactly one reinforcement set per wave.
2. Reinforcement materialization tile (1.7): sim row east from x=17 | gate gaps pinned at
   world x 1823-1826, exact tiles unknown | materialize species side-by-side inside the gap
   (local x 16-19) on the inner walkable row of the chosen wall. IMPLEMENTED P1.
3. Warband movement speed (7.17): sim 1 tile/tick | "runs"/"darts", no tiles/tick figure
   anywhere | 2 tiles/tick for the three warbanders (OSRS run pace), 1 for everything else.
   IMPLEMENTED P2.
4. Spawn-slot RNG (1.4): sim Fisher-Yates over the table | only the 4-tile exclusion plus
   outcome odds ("4/9 see you on SW", "1 in 6 double south on 8/10/11 NW", "~2/3 double
   north SW") are documented | uniform without replacement over non-excluded anchors;
   validate the produced odds against the quoted guide numbers. IMPLEMENTED P1.
5. 13th b5 tile world (1824,3110) + cyan tile (1824,3106) (new): not in sim | in the b5
   pastebin/image but absent from the los 12-anchor array; semantics unstated | exclude;
   model exactly the 12 anchors. IMPLEMENTED P1.
6. Skyfall delay (4.9, 7.6): sim 3 ticks | wiki "a few ticks" | KEEP 3.
7. Skyfall accuracy/damage (4.11): sim accuracy-rolled vs ranged def, uniform 0..48 | wiki
   frames it as unblockable "heavy damage" dodged by moving; no roll documented | DROP the
   accuracy gate (guaranteed when standing on the mark at land), keep the 0..48 roll.
   IMPLEMENTED P3.
8. Skyfall resolution order (4.12): resolved pre-NPC-movement on the player's land-tick tile
   | absent | keep.
9. Minotaur heal-check period (4.19-part): sim every 5t cycle | the page ties heals to "the
   timer" that also drives melee (attack speed 5t), never states the period | KEEP 5t,
   restructured per A13 (one target, to full). IMPLEMENTED P3.
10. Minotaur passive self-regen: not modeled | single unresolved talk-page report (~5 hp /
    20t, anonymous, 2026-02) | do NOT model. IMPLEMENTED P3 (not modeled; noted in code).
11. Manticore movement/charge coupling (4.29): sim unrestricted movement | "charge-up"
    described, no movement rule anywhere | keep unrestricted; first-attack timing from
    spawn-uncharged ≈ sim's full 10t timer, keep. IMPLEMENTED P3 (kept unrestricted; noted
    in code).
12. Manticore orb travel (7.5): sim fires then lands next tick | wiki: orbs launched on
    consecutive ticks and "land with a projectile travel time of 0" | land on the launch
    tick (shift by one); flick cadence is unchanged either way. IMPLEMENTED P3.
13. Sol AoE/grapple max hit 44 vs 45 + slam minimum (6.11, 6.20): sim 44 flat | W-MAIN live
    fetch says 44/44, RuneC wiki-cache infobox says 45/45, W-STRAT prose "up to 45", colosim
    20-44 with an author comment doubting the min | adopt colosim 20 + rand(0..24) = 20-44
    for both AoE tiles and failed grapple.
14. Spear line length (A3): n/a in sim yet | W-STRAT "4x1 lines" vs colosim LINE_LENGTH 7 |
    use 4 (maintained wiki text, post-hotfix); revisit if the boss feels under-lethal.
15. Sphere/laser damage range (A10): sim 0..75 | colosim 60-79 vs W-MAIN ≤75 vs W-STRAT 70+ |
    60 + rand(0..15) = 60-75 (colosim min ∩ wiki cap).
16. Crystal count by enrage (6.22): sim 1 | W-MAIN implies 5 (each phase), W-STRAT describes
    3 (phases 2/3/5), colosim spawns 4 (transitions at 90/75/50/25) | 4 per colosim; enrage
    raises cadence, not count.
17. Sol immobility-while-attacking ticks (A2): n/a in sim | wiki 4t for AoEs vs colosim 6
    (spear) / 4 (shield) / 5 (grapple) / 5 (transition) | colosim per-attack values (more
    specific, engine-derived).
18. Parry/perfect-parry input-tick convention (6.15, 6.18): sim prayer-at-land, grapple_timer
    ≤1 | wiki "on the tick before he lands" vs colosim hit-tick check with 3/2/2-3-tick
    prior-off windows; grapple "last possible tick" undefined in ticks | keep prayer-at-land
    + the B4 lookback windows from colosim; keep grapple_timer ≤1 as "last tick".
19. Quartet wave-12 spawn tile (A6): n/a | no source states it | random walkable tile inside
    the reduced arena, ≥4 tiles from the player (mirrors the primary-spawn exclusion). IMPLEMENTED P1.
20. Volatility explosion damage + timing (5.25): sim flat 25 on overlap | no damage number in
    any source, trigger timing undocumented | keep flat 25, trigger on the death tick.
21. Solarflare contact damage (5.22): sim 12/18/24 by tier | no numbers anywhere; T2+ only
    "deals more damage" | keep 12/18/24.
22. Totemic heal pulse, waves 1-11 (5.24): sim every 5t | wiki "every few ticks"; only the
    wave-12 figure (75 HP / 7t) is pinned | 7 ticks, mirroring the wave-12 cadence.
23. Shockwave Colossus when player is adjacent (4.14-part): sim keeps casting (range floor 1)
    | explicitly UNKNOWN in research | keep casting.
24. Shaman/reinforcement-shaman movement ("wiggle") + standard projectile travel times
    (4.2, 7.2, 7.3): sim greedy chase + shared dps-calc delay formulas | wiggle rule and
    colosseum-specific travel times unpinned | keep both as-is.
25. Enrage pool placement (6.26): sim uniform-random arena tile | W-MAIN "random tile around
    the arena" vs colosim within ±4 tiles of the player | colosim ±4-of-player (keeps enrage
    pressure on the agent; wiki phrasing does not exclude it).
26. Inter-wave pacing (2.23, 5.7-part): sim 9-tick gap + optional draft auto-close | real
    pacing is player-gated by the mandatory draft (B6); no fixed gap documented | replace the
    9t gap with draft-gate + short fixed post-pick delay; keep the 6t ready delay (C).
27. Boss-wave player start/teleport (1.10): sim teleports to (19,26) | no teleport in any
    source; player is wherever they are when gladiators barricade; sim tile sits on the
    researched wall row (A8) | keep the teleport affordance but move the start tile inside
    the researched 16x16 (e.g. south-centre world (1824,3100) = local (17,11)). IMPLEMENTED P1.
28. Sol repositioning during phase transitions: n/a | arena jump/land anims exist (10876/7);
    colosim freezes him 5t in place; no source describes movement | stationary during
    transition, forced Spear after (A1).
29. Wave-12 corner detail (A8): n/a | okronos rows give 2x2 pillar intrusions into the 16x16
    interior; Skairunner says "four tiles jutting out from the corners" | model pillars as
    3x3 blockers at the cache anchors and let the intersection define the corners. IMPLEMENTED P1.
30. Warband attack-rate param: cache param 14 = 5 vs wiki/dps-calc 6t | the scripted shared
    cycle is 6t (B2/C) | model the 6t cycle; treat param 14 as engine-internal. Also D:
    whether the wave-anchored cycle phase survived the April 2024 rebalance (writeup is
    launch-week) | assume yes. IMPLEMENTED P2.
31. Reentry offered after wave 11 (5.5): sim excludes it | only Red Flag/Dynamic Duo/
    Mantimayhem exclusions are sourced | keep excluding (it is inert vs Sol, same logic
    Jagex applied to Mantimayhem).
32. Episode tick cap (1.14): sim 9000t | the real game has no run timer | keep (training
    affordance).
33. Melee gate diagonals (7.10): sim Chebyshev-1 for all | minotaur heal text implies
    diagonal counts as melee distance; per-NPC attack diagonality unpinned (OSRS 1x1 melee
    is normally cardinal-only) | keep Chebyshev for size-2/3 NPCs and Sol; consider
    cardinal-only for the 1x1 warband/shaman if cheap. IMPLEMENTED P2 (warbanders
    cardinal-only; shaman unchanged).
34. Bees spawn location / diagonal step / boss-arena clamp immunity (5.9): sim random
    spawn-table tile, diagonal allowed, ignores clamp | unpinned; bees ARE active during the
    Sol fight (CA "Reinforcements" requires Bees! II vs Sol) | keep all three (clamp
    immunity is required for wave-12 bees anyway).
35. Molten misc (5.26): one-burn-per-tick across pools, 32-pool cap with recycling; Blasphemy
    dual application path (5.10) | absent from sources | keep.
36. Player loadout (7.19): placeholder inferno gear, no slash weapon, no freeze | out of
    colosseum scope but A14 requires a slash option vs Sol; warband freeze interactions
    (attack up to 2 tiles frozen) only matter if a freeze loadout is added | add a slash
    melee set; defer freezes.
