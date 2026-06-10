# Web research: Colosseum spawn tiles + NPC gap-fill (2026-06-10)

Gap-filling pass over salvaged-workflow-claims.md. Every FACT row: value | source URL | verbatim quote (or code). DERIVED rows are computations from cited facts, marked as such with the derivation shown. Fetched live 2026-06-10.

Coordinate conventions used throughout:
- REGION coords: region 7216 template-local tiles, regionX 0-63 east, regionY 0-63 north (RuneLite ground-marker format).
- WORLD coords: world = (1792 + regionX, 3072 + regionY), since region 7216 = (28<<8)|48 and (28*64, 48*64) = (1792, 3072).
- LOS coords: los.colosim.com 34x34 grid, x east, y SOUTH (flipped).

---

## GAP 1 — exact wave spawn tiles: PINNED

### The 12 default spawn points exist

FACT | "12 default spawns" phrase is current wiki text (Dynamic Duo entry) | https://oldschool.runescape.wiki/w/Fortis_Colosseum/Modifiers | "Shockwave Colossi will now spawn in pairs. The paired Colossus spawns near the main Colossus, but not necessarily on one of the 12 default spawns."

### Source A: los.colosim.com bundle hardcodes the 12 points

FACT | 12 spawn zones, 3x3 each, hardcoded as array `ao` in the production JS bundle, drawn as grey 3x3 boxes behind a `showSpawns` toggle using the same anchor convention as pillars and 3x3 NPCs | https://los.colosim.com/assets/index-nXbs0Cgv.js | `ao = [[3,19],[9,17],[3,14],[13,14],[19,14],[17,9],[13,20],[19,20],[16,24],[24,16],[28,14],[28,19]]` ... `this.showSpawns ? t.globalAlpha = .35 : t.globalAlpha = 0, t.fillStyle = "#999"; for (var o = 0; o < ao.length; o++) t.fillRect(ao[o][0] * w, (ao[o][1] + 1) * w, 3 * w, -3 * w);`

FACT | pillars: four 3x3, los anchors | same bundle | `const Qe = [[8,10],[23,10],[8,25],[23,25]]` (drawn 3x3, `isPillar` collides size 3)

FACT | default player tile in sim | same bundle | `Bt = [7,15]` (drawn green "#9F9")

FACT | grid is 34x34 | same bundle | `ae = 34, Pt = 34`

FACT | arena walkability per row (blocked extents, los rows 0=north to 33=south) | same bundle | `Lr = [[[0,13],[15,19],[21,34]], [[0,9],[25,34]], [[0,7],[27,34]], [[0,6],[29,34]], [[0,5],[29,34]], [[0,3],[31,34]], [[0,3],[31,34]], [[0,2],[32,34]], [[0,2],[32,34]], [[0,1],[33,34]], [[0,1],[33,34]], [[0,1],[32,34]], [[0,1],[31,34]], [[31,34]], [[31,34]], [[0,1],[31,34]], [[0,1],[31,34]], [[0,1],[31,34]], [[0,1],[31,34]], [[31,34]], [[31,34]], [[0,1],[31,34]], [[0,1],[32,34]], [[0,1],[33,34]], [[0,1],[33,34]], [[0,2],[32,34]], [[0,2],[32,34]], [[0,3],[31,34]], [[0,3],[31,34]], [[0,5],[29,34]], [[0,5],[29,34]], [[0,7],[27,34]], [[0,9],[25,34]], [[0,13],[15,19],[21,34]]]`

FACT | coordinate scheme linking los coords to RuneLite scene coords (the capture pipeline feeding the sim) | https://github.com/willediger/Colosseum-Waves (ColosseumWavesPlugin.java) | `int losX = sceneLocation.getX() - LOS_COORD_OFFSET_X; int losY = LOS_COORD_OFFSET_Y - sceneLocation.getY();` with `LOS_COORD_OFFSET_X = 32; LOS_COORD_OFFSET_Y = 83;` spawn code format `String.format("%02d%02d%d", losPos.getX(), losPos.getY(), losNpcId)`

### Source B: wiki tile-marker module (region coords, ground truth anchor)

FACT | full Colosseum tile-marker JSON in region 7216 coords, including the arena-edge deadzone outline, A/B tiles, labeled safespots | https://oldschool.runescape.wiki/w/Module:Tile_markers/Colosseum.json | labeled entries verbatim: `{"regionId": 7216, "regionX": 23, "regionY": 43, "z": 0, "color": "#00000000", "label": "A"}, {"regionX": 23, "regionY": 42, ..., "label": "B"}, {"regionX": 26, "regionY": 50, ..., "label": "jaguar trap"}, {"regionX": 25, "regionY": 50}, destack labels at (17,41),(16,38),(26,19),(29,18),(17,28),(16,31), {"regionX": 21, "regionY": 43, "label": "offtick"}, {"regionX": 21, "regionY": 26, "label": "stall"}, {"regionX": 19, "regionY": 46, "label": "drag"}` plus ~120 unlabeled `#00000000` deadzone outline tiles spanning regionX 15-49, regionY 17-52, plus four 5x5 ring outlines at regionX 27-31/34-38 x regionY 29-33/36-40 (the marker list description says the pack includes "Solarflare paths for the Sol fight")

FACT | what the marker pack contains | https://oldschool.runescape.wiki/w/Fortis_Colosseum/Strategies (Tile markers section wikitext) | "Wave start tiles for both NW and SW pillars * Commonly-used A and B tiles for offticking double south spawns * Deadzones (tiles on the edge of the arena that you cannot enter) * Solarflare paths for the Sol fight * Destacking tiles * NW jaguar trap tiles * Tiles for offticking (NW) or stalling (SW) the reinforcement serpent shamans ... {{Tile markers box|Module:Tile markers/Colosseum.json}}"

### Source C: b5/Wotury discovery thread (mechanics + tile list)

FACT | spawn mechanics, verbatim from the original 2024-03-21 reddit post (r/2007scape 1bka2f6, author AskYouEverything = b5, recovered via pullpush.io archive) | https://www.reddit.com/r/2007scape/comments/1bka2f6/colosseum_spawn_mechanics_spawn_fixing_method_by/ | "Each marked tile on this map is a potential spawn location of any of the primary NPCs in the Colosseum. For NPCs larger than 1x1, the marked tiles will be the location of their southwest tile." / "The yellow square contains tiles that the Fremmy Ranger may spawn on, with the mager and the meleer offset from that." / "The yellow line controls whether or not the mid-wave set will spawn at the north gate or the south gate. If the player is south of the yellow line, the mid-wave set will spawn south and vice versa." / "Primary wave spawns cannot spawn within 4 tiles of the player's current position." / "If the player stands on the white tile in the above image, no primary NPC will be able to spawn within the red box." / "The tile can be moved both south 3 tiles and east 2 tiles and accomplish the equivalent thing." / "the player has 3 ticks of movement to get behind pillar before anything attacks the player."

FACT | the thread's tile-marker pastebin (linked in-thread by chg1730: "For people that want the tiles") lists 13 green spawn tiles + 1 "start" tile, region 7216 | https://pastebin.com/Nzu8tDaP | `(19,32),(25,34),(19,37),(29,37),(32,38),(35,37),(33,42),(29,31),(35,31),(32,27),(40,35),(44,37),(44,32)` all `#FF018E2B`, plus `{"regionX":21,"regionY":36,...,"label":"start"}`

### Cross-verification and the resulting canonical tile list

DERIVED | los-to-region transform: losX = regionX - 16, losY = 51 - regionY. Solved by aligning the los `Lr` wall map against the wiki deadzone outline over all candidate translations: this transform places all 96 in-grid deadzone tiles on blocked los cells with ZERO mismatches (next-best candidate: 22 mismatches). Independently confirmed by the b5 pastebin: applying the transform to the los `ao` array reproduces 12 of the pastebin's 13 green tiles EXACTLY. Additionally, overlaying the transformed zones on b5's arena screenshot (https://i.imgur.com/G21mpEz.png, 930x1058) puts each zone on a visibly darker floor tile, 12/12, with the dark tile at the zone's SW corner, matching the post's "marked tiles will be the location of their southwest tile".

CANONICAL 12 SPAWN ANCHORS (SW tile of 3x3 zone; this is the b5 pastebin list = transformed los list):

| # | region (x,y) | world (x,y) | los anchor |
|---|---|---|---|
| 1 | (19,32) | (1811,3104) | [3,19] |
| 2 | (25,34) | (1817,3106) | [9,17] |
| 3 | (19,37) | (1811,3109) | [3,14] |
| 4 | (29,37) | (1821,3109) | [13,14] |
| 5 | (35,37) | (1827,3109) | [19,14] |
| 6 | (33,42) | (1825,3114) | [17,9] |
| 7 | (29,31) | (1821,3103) | [13,20] |
| 8 | (35,31) | (1827,3103) | [19,20] |
| 9 | (32,27) | (1824,3099) | [16,24] |
| 10 | (40,35) | (1832,3107) | [24,16] |
| 11 | (44,37) | (1836,3109) | [28,14] |
| 12 | (44,32) | (1836,3104) | [28,19] |

13TH TILE | region (32,38), world (1824,3110) | in the b5 pastebin but NOT in the los sim's 12-entry `ao` array. It lies inside the measured "Fremmy Ranger" yellow square (below). Semantics not stated in any fetched source.

DERIVED | pillars in region coords (via transform): SW anchors (24,41) NW, (39,41) NE, (24,26) SW, (39,26) SE, each 3x3, i.e. world (1816,3113), (1831,3113), (1816,3098), (1831,3098). 15-tile spacing both axes. Consistent with pillar bases detected in the b5 screenshot at the same grid positions.

DERIVED | sim default player tile Bt [7,15] = region (23,36) = world (1815,3108).

DERIVED | arena bounding box from deadzone outline: regionX 15-49, regionY 17-52 = world (1807,3089)-(1841,3124). Matches the project's known arena tile-marker extent exactly. los grid covers the interior region x 16-49, y 18-51.

### b5 image measurements (image anchored to grid via pillar bases, 21.47 px/tile)

MEASURED | yellow line: horizontal, spans the arena at the regionY 33/34 tile boundary (measured 33.48-33.52). Player on regionY <= 33 -> mid-wave (reinforcement) set spawns at SOUTH gate; regionY >= 34 -> NORTH gate (rule quoted above from the b5 post).
MEASURED | white "spawn fixing" tile in the image = region (21,36) (measured center (20.81,36.15)) = the pastebin "start" tile = world (1813,3108). The red box in the image spans tiles regionX 17-25, regionY 32-40, exactly the 9x9 Chebyshev-radius-4 neighborhood of (21,36), i.e. the rendered "cannot spawn within 4 tiles" exclusion. It suppresses exactly 3 of the 12 anchors: (19,32),(19,37),(25,34).
MEASURED | yellow square ("tiles that the Fremmy Ranger may spawn on"): interior tiles regionX 28-34, regionY 33-39 (7x7; outline measured at x 27.4-34.8, y 32.4-39.5). Contains anchors (29,37),(32,38) and abuts (35,37),(32,27). A cyan-outlined single tile with a small character sprite sits at region (32,34) (measured (32.11,34.19)); its meaning is not stated in the post.

DERIVED (consistency note, not a sourced fact) | SW-pillar guide says "4 out of 9 spawns will see you on spawn, as opposed to 2 out of 9 on NW" (quote below). 12 anchors minus the 3 suppressed by the 4-tile exclusion at a wave-start tile = 9 candidate slots, matching the guide's denominator.

FACT | spawn-slot probabilities | https://oldschool.runescape.wiki/w/Guide:Fortis_Colosseum_-_SW_pillar_breakdown | "4 out of 9 spawns will see you on spawn, as opposed to 2 out of 9 on NW"; double north on SW occurs "about four times as often" as double south on NW; "triple south on waves 3, 5, or 6"
FACT | double-south odds on NW | https://oldschool.runescape.wiki/w/Fortis_Colosseum/Strategies (Wave start wikitext) | "You have a 1 in 6 chance to get a double south spawn on waves 8, 10, and 11. '''On SW, you have nearly a 2 in 3 chance for double north!'''"
FACT | player position at spawn-resolution matters | same section | "By clicking towards the pillar on the 5th tick, NPCs are prevented from spawning in the west area, minimising the number of NPCs the player has to deal with at wave start."
FACT | warband spawns center | https://oldschool.runescape.wiki/w/Fortis_Colosseum/Strategies (Monsters section wikitext) | "the warband will spawn in the centre of the arena, though unlike the nibs, they will immediately dart towards the player upon spawning"
FACT | warband formation | same | "the berserker will prioritise standing north of the player, the seer to their east and the ranger to the west"; Quartet: "a fourth member will spawn to the south, creating a diamond-like formation"
FACT | serpent shaman center-spawn fallback (fetch-extracted phrasing, not verbatim wiki) | https://oldschool.runescape.wiki/w/Serpent_shaman | fetch reported: "Can spawn in arena center if player stands near ally spawn points"

### Gates and reinforcement locations

FACT | reinforcements arrive at gates, not the 12 points | https://oldschool.runescape.wiki/w/Fortis_Colosseum | "If the player does not complete a wave within 40 seconds, additional enemy reinforcements will arrive from either the north or south gates, depending on which one they are closest to."
FACT | b5 refinement: gate choice governed by the yellow line (regionY 33/34 boundary), quoted above | reddit 1bka2f6
DERIVED | gate geometry from deadzone outline gaps + los walls: NORTH gate: outer wall row regionY 52 unmarked at regionX 31-34 (marked deadzones 28-30 and 35-37); inner row regionY 51 blocked at x 31-34 (los row 0 block [15,19)), walkable flanks at (29-30,51) and (35-36,51). SOUTH gate mirrored at regionY 17 (gap x 31-34) / regionY 18 inner. WEST entrance: outer x 15 unmarked at regionY 34-35, inner x 16 blocked regionY 33-36, walkable at x16 regionY 31-32 and 37-38. EAST edge: fully blocked in the los map; deadzone markers only at (49, 26-28) and (49, 41-43).
UNKNOWN | the exact tile(s) reinforcement NPCs materialize on at a gate.

---

## GAP 2 — reinforcement timer precision: PARTIAL (40 s only, no tick-exact source)

FACT | 40 seconds after wave start | https://oldschool.runescape.wiki/w/Fortis_Colosseum | "If the player does not complete a wave within 40 seconds, additional enemy reinforcements will arrive from either the north or south gates, depending on which one they are closest to."
FACT | 40 seconds, Strategies corroboration | https://oldschool.runescape.wiki/w/Fortis_Colosseum/Strategies (Tips/RuneLite plugins wikitext) | "a timer (used to anticipate reinforcements spawning, which occurs 40 seconds after the wave starts)"
FACT | wave-clear procedure phrasing | https://oldschool.runescape.wiki/w/Fortis_Colosseum/Strategies (Wave clearing wikitext) | "Once 40 seconds have passed) Kill reinforcements."
FACT | reinforcements preventable by clearing fast (wave 1) | https://oldschool.runescape.wiki/w/Money_making_guide/Completing_the_Fortis_Colosseum_(Wave_1) (prior pass) | "to prevent the Jaguar warrior from spawning"
FACT | no plugin hardcodes a reinforcement timer: willediger/Colosseum-Waves classifies any tracked spawn >10 ticks after wave start as reinforcement (author heuristic, not a measured timer) | https://github.com/willediger/Colosseum-Waves | `if (ticksSinceWaveStart > 10 && !reinforcementsPhase) { reinforcementsPhase = true; ... }`
FACT | blert-io plugin tracks wave start/end via chat messages only, no reinforcement constant | https://github.com/blert-io/plugin (WaveDataTracker.java) | `private final String waveStartMessage = "Wave: " + wave;` ... `"Wave " + wave + " completed! Wave duration: ..."`
UNKNOWN | tick-exact value (40 s / 0.6 s = 66.67; whether the engine uses 66 or 67 ticks). No community measurement found.
UNKNOWN | whether the timer pauses for any reason.
UNKNOWN | whether more than one reinforcement set can arrive in a single wave.

---

## GAP 3 — Serpent shaman: PINNED

All from https://oldschool.runescape.wiki/w/Serpent_shaman (fetched 2026-06-10) unless noted.

FACT | attack style | "Attack style: Magic"
FACT | attack | "using high-accuracy Water Surge spells with a range of 10 tiles"
FACT | attack speed | "Attack speed: 5 ticks (3.0 seconds)"
FACT | max hit | "Max hit: 28"
FACT | combat level | "Combat level: 161"
FACT | hitpoints | "Hitpoints: 125"
FACT | size | "Size: 1x1"
FACT | magic level 220, magic defence +15, defence 90, melee defence bonuses all +30 (fetch-extracted infobox values)
FACT | immune to poison/venom ("100% resistance"), immune to cannons, aggressive
FACT | spawn role | "appear at the start of the first six waves" and "appear as reinforcements for waves 4, 5, 6, 10 and 11"
FACT | no special | https://oldschool.runescape.wiki/w/Fortis_Colosseum/Strategies (Monsters wikitext) | "use a moderately strong magic attack against the player. They have a range of 10 tiles" ... "no other special mechanics" ... "particularly troublesome enemies due to their small size and high accuracy"
FACT | main-page one-liner | https://oldschool.runescape.wiki/w/Fortis_Colosseum | "Can only attack with mage. Spawns primarily in the early waves."
FACT | April 2024 change is cosmetic | Serpent shaman page | "The serpent shaman's magic attack now appears as Water Surge rather than Water Strike"
FACT | reinforcement shamans move differently from wave-start shamans | https://los.colosim.com/assets/index-nXbs0Cgv.js (UI tooltip string) | 'Place a Reinforcement Serpent Shaman. These "wiggle" differently from shamans spawned at the start of the wave.' (sim models them as a distinct NPC type, id 6 vs 1, same size/range/cd)
UNKNOWN | precise movement/"wiggle" rule for either shaman variant; projectile travel time; area denial (none mentioned anywhere); venom (none; it is not poisonous per infobox).

---

## GAP 4 — Shockwave Colossus: PARTIAL (stats pinned; clap = its ordinary cast, no special per wiki)

All from https://oldschool.runescape.wiki/w/Shockwave_Colossus (fetched 2026-06-10) unless noted.

FACT | attack style | "Attack style: Magic"
FACT | range | "Attack range of 15 tiles"
FACT | attack speed | "Attack speed: 5 ticks (3.0 seconds)"
FACT | max hit | "Max hit: 56"
FACT | combat level | "Combat level: 239"
FACT | hitpoints | "Hitpoints: 125"
FACT | size | "Size: 3x3"
FACT | levels (fetch-extracted infobox): Magic 350, Defence 150, Strength 190, Attack 120, Ranged 220; poison/venom 100% resistance; aggressive
FACT | NO special mechanics | https://oldschool.runescape.wiki/w/Fortis_Colosseum/Strategies (Monsters wikitext) | "attack using magic" ... "have a very long attack range of 15 tiles" ... "do not have any special mechanics" ... "have an even higher max hit than their ranged counterparts"
FACT | main-page one-liner | https://oldschool.runescape.wiki/w/Fortis_Colosseum | "Can only use a simple mage attack. Has low hp, but hits the hardest of the enemies that can attack from a distance."
FACT | Dynamic Duo pairing | Shockwave page | "They will spawn in pairs" when Dynamic Duo is active; Modifiers page: "The paired Colossus spawns near the main Colossus, but not necessarily on one of the 12 default spawns."
NOTE | the "clap" (SpotanimID NPC_COLOSSI_SHOCKWAVE_01_CLAPATTACK = 2679, verified in prior pass) is therefore the graphic of its standard 5-tick single-target magic attack; no AoE/telegraph/dodge mechanic is documented anywhere fetched.
UNKNOWN | behavior when player is adjacent (e.g. whether it melees or keeps casting); projectile travel time; sim's los entry gives cd 5, range 15, size 3 consistent with wiki (https://los.colosim.com bundle, `SHOCKWAVE_COLOSSUS: { id: 3, size: 3, range: 15, cd: 5 }`).

---

## GAP 5 — Fremennik warband movement + pathing: MOSTLY PINNED (no tiles/tick number exists)

FACT | smart pathing + runs | https://oldschool.runescape.wiki/w/Fortis_Colosseum | berserker/archer/seer rows each: "Has smart pathing and runs to where the player currently is."
FACT | routefinding | https://oldschool.runescape.wiki/w/Fremennik_warband_berserker (same wording on archer/seer pages) | possesses "routefinding" and "will move around pillars to reach players"
FACT | spawn + immediate chase | https://oldschool.runescape.wiki/w/Fortis_Colosseum/Strategies (Monsters wikitext) | "the warband will spawn in the centre of the arena, though unlike the nibs, they will immediately dart towards the player upon spawning"
FACT | formation targets | same | "the berserker will prioritise standing north of the player, the seer to their east and the ranger to the west"; Quartet adds a fourth "to the south, creating a diamond-like formation"
FACT | attack gating | berserker/archer/seer pages | "They attack on a fixed 6 tick cycle, only attacking when they are in melee distance and are not moving."
FACT | cycle order/timing | https://oldschool.runescape.wiki/w/Fremennik_warband | "The berserker attacks first, followed by the seer, then the archer, with a 1 tick interval between each attack. After the archer attacks, none will attack for three ticks before the berserker attacks again, repeating the cycle."
FACT | attack-skip on player movement | same | "If the player is moving when a warband member is scheduled to attack, the warband member will skip their attack and not attack until the next cycle in 6 ticks (unless their attack is also skipped in that cycle)."
FACT | desync risk | Strategies Monsters wikitext | "If the player uses a hit-and-run tactic on them in an attempt to delay their hits, this may cause them to attack off sync"
FACT | frozen attack range | https://oldschool.runescape.wiki/w/User:Skairunner/Fortis (prior pass) | "They will run towards the player and only attack when standing still next to the player or when frozen up to 2 tiles away."
FACT | cycle anchored to wave start + 6-tick wave-start delay (launch-week community writeup; stats in it are stale) | https://i.imgur.com/8ict0rl.png ("Fremennik Warband & Wave Tick Cycle Writeup", linked from https://www.youtube.com/watch?v=dxqrQQNl2_4 description) | "If you click the button to start the wave on tick N, the wave will start on tick N+6. (e.g. if you click on 3, the enemies will spawn on the next 3)" / "N: safe to click to attack; N+1: berserker attack will happen if you do not click to move on this tick; N+2: seer attack ...; N+3: archer attack ...; N+4: (aka N-2): safe ...; N+5: (aka N-1): safe ..." / "Protection prayers fully protect against fremennik attacks." (note: its listed max hits 40/12/1 are pre-3-April-2024 values)
FACT | post-April-2024 stats, berserker | https://oldschool.runescape.wiki/w/Fremennik_warband_berserker | combat 103, HP 48, max hit 29, "Attack speed: 6 ticks (3.6 seconds)", Atk/Str/Mag/Rng 110, Def 80, style Stab, 1x1; changelog: "Melee strength bonus decreased from 150 to 90", "Max hit decreased from 40 to 29"
FACT | post-April-2024 stats, archer | https://oldschool.runescape.wiki/w/Fremennik_warband_archer | combat 104, HP 50, max hit 14, 6-tick speed, style Ranged, 1x1; changelog: "Ranged level increased from 3 to 110", "Ranged attack bonus increased from 0 to +150", "Ranged strength bonus increased from 0 to +10" (max hit 1 -> 14)
FACT | stats, seer | https://oldschool.runescape.wiki/w/Fremennik_warband_seer | combat 104, HP 50, max hit 12, 6-tick speed, style Magic, 1x1; "Attacking them with ranged will always result in a max hit."; freeze thresholds quoted: "+104 magic attack bonus with Augury" or "+140 without" for Ice Barrage
FACT | b5 thread wave-start grace | reddit 1bka2f6 | "the player has 3 ticks of movement to get behind pillar before anything attacks the player"
UNKNOWN | movement speed as tiles per game tick. No fetched source states a number ("runs"/"dart" is the strongest wording; OSRS NPC run = 2 tiles/tick is NOT stated by any fetched source for these NPCs).
UNKNOWN | whether members stall while another attacks (only the fixed stagger cycle is documented); whether they path around each other.

---

## GAP 6 — Minotaur heal range: CONFLICT DOCUMENTED, 7-FROM-CENTER BEST SUPPORTED

Both passages live on the CURRENT page (fetched 2026-06-10), https://oldschool.runescape.wiki/w/Minotaur_(Fortis_Colosseum):

FACT | intro passage (6 tiles) | "If the player is not within melee distance (including diagonally) the minotaur will attempt to heal other wounded monsters to full health within 6 tiles of it."
FACT | detailed mechanics passage (10-tile scan, 7-tile cap) | "it will scan for a radius of 10 tiles from it's centre coordinate, respecting line of sight. If it finds an NPC in this radius that is: - not a minotaur - has less than 75% of its total hitpoints - its centre coordinate is also in the line of sight to the minotaurs centre coordinate - and is 7 or less tiles away"
FACT | heal is to FULL | "heal other wounded monsters to full health" (both the page and Strategies: "will continuously heal them to '''full health'''")
FACT | melee priority over healing | https://oldschool.runescape.wiki/w/Fortis_Colosseum/Strategies (Monsters wikitext) | "If in melee range, they will prioritise attacking the player over healing damaged NPCs"; Minotaur page (prior pass, same page text): "if the player is in melee range when the timer runs, it will melee you"
FACT | Strategies sides with 6 | Strategies Monsters wikitext | "within 6 tiles of them and in line of sight"
FACT | los.colosim.com implements a dedicated 7-tile LoS radius drawn from the minotaur's CENTER tile | https://los.colosim.com/assets/index-nXbs0Cgv.js | `io = 5, so = 7, uo = "purple"` and `c === io && this.drawLOS(this.mobs[...][0] + 1, this.mobs[...][1] - 1, 1, so, !1, uo)` (minotaur enum = 5; +1,-1 from SW anchor = center of the 3x3)
FACT | LoS blockable by pillars (search-result phrasing of wiki text) | https://oldschool.runescape.wiki/w/Minotaur_(Fortis_Colosseum) | "the minotaur's line of sight is based on their centre tile, so their healing can be blocked by the pillars"
FACT | search-result summary attributes the detailed 7-tile rule to a Jagex moderator statement ("According to an official statement from a Jagex moderator..."), but the attribution was not verified against the page markup itself.
NOTE (reconciliation hypothesis, not a fact) | 7 tiles from the 3x3 minotaur's CENTER = 6 tiles from its body edge; the 6-tile phrasings are consistent with the 7-from-center rule under that reading. No source states this explicitly.
FACT | combat profile | Minotaur page | "Attack speed: 5 ticks (3.0 seconds)", "Max hit: 74" (prior pass: combat 318, HP 225, 3x3, melee damage lands 1 tick after the attack animation, tick-eatable like Vardorvis)
FACT | unresolved talk-page feedback claims passive self-regen | https://oldschool.runescape.wiki/w/Talk:Minotaur_(Fortis_Colosseum) | "The Minotaur has a passive healing effect where it heals ~5 hp every 20 ticks." (submitted 2026-02-02, unresolved, single anonymous report)
UNKNOWN | heal cast frequency (the page ties heal checks to "the timer" that also drives melee, with attack speed 5 ticks, but no source states the heal-check period explicitly); whether the heal has a cooldown separate from the attack timer.

---

## Bonus: los.colosim.com NPC table (corroborates wiki stats; los NPC ids used in spawn-code URLs)

FACT | https://los.colosim.com/assets/index-nXbs0Cgv.js | player id -1 (size 1, range 10); SERPENT_SHAMAN id 1 (size 1, range 10, cd 5); JAVELIN_COLOSSUS id 2 (size 3, range 15, cd 5); SHOCKWAVE_COLOSSUS id 3 (size 3, range 15, cd 5); JAGUAR_WARRIOR id 4 (size 2, range 1, cd 5); MINOTAUR id 5 (size 3, range 1, cd 5); REINFORCEMENT_SHAMAN id 6 (same as shaman); MANTICORE id 0 (size 3, range 15, cd 10). Manticore orb-order codes: `du = { r: [0,1,2], m: [1,0,2], Mrm: [2,0,1], Mmr: [2,1,0], rMm: [0,2,1], mMr: [1,2,0] }` with non-MM3 set `hp = ["r","m"]` and full set `pp` adding the four MM3 ("M"-prefixed) variants; "u"-prefixed variants mark uncharged manticores (right-click toggle).
NOTE | jaguar warrior size 2x2 per the sim's table (wiki pages fetched do not state its size).

## Bonus: Volatility + Reentry per-tier (thin in salvage, now quoted)

FACT | https://oldschool.runescape.wiki/w/Fortis_Colosseum/Modifiers (wikitext) | Volatility I: "Upon death, the enemy will explode one tile greater than their size. For example, a manticore, whose size is 3x3 tiles, will explode in a 5x5 radius." II: "The explosion radius is now '''two''' tiles greater, ex. a 3x3 monster will now explode in a 7x7 radius." III: "The tile at the centre of the explosion now leaves behind a '''temporary''' pool of molten sand, disappearing after the wave ends."
FACT | same | Reentry I: "Javelins launched into the air by Javelin Colossi will now leave a '''temporary''' pool of molten sand where they land, disappearing after the wave ends." II: "The molten sand is now '''permanent''', and now includes the targeted tile and the tile south-west of it, if accessible." III: "The tiles where molten sand is left behind now includes the tile west of the targeted tile."
FACT | same | Quartet: "An extra random Fremennik Warbander spawns every wave."

---

## UNKNOWN list (explicit)

1. Reinforcement timer in ticks (66 vs 67); whether it pauses; whether multiple reinforcement sets can arrive in one wave.
2. Exact tile(s) where reinforcements materialize at the north/south gates (gate gaps located at regionX 31-34 on rows regionY 52 / 17; spawn tile not pinned).
3. Spawn-slot RNG: how NPCs are assigned to the 9-12 available anchors per wave (only the quoted probabilities and the 4-tile exclusion are documented).
4. Semantics of the 13th pastebin tile (32,38) and the cyan tile (32,34) in the b5 map.
5. Fremennik warband movement speed in tiles/tick; pathing interactions between members.
6. Minotaur heal-check period; the talk-page passive-regen claim (~5 hp / 20 ticks) is unverified.
7. Serpent shaman movement rule (incl. the reinforcement-shaman "wiggle" difference); projectile travel times for shaman/shockwave/javelin standard attacks.
8. Shockwave Colossus behavior when the player is adjacent.
9. Whether the warband's wave-start-anchored cycle phase (N+1/+2/+3, from the launch-week writeup image) survived the April 2024 rebalances unchanged.
