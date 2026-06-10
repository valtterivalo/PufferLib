# Salvaged workflow claims: Fortis Colosseum deep research (wf_76235121-92d)

Salvaged 2026-06-10 from ~/.claude/projects/-Users-valtterivalo-Projects-pufferlib-metal/777c1930-8f1a-4f7d-9d65-426e8d1529cb/subagents/workflows/wf_76235121-92d/, a deep-research workflow that died from org spend-limit rate limiting mid-verification.

Pipeline state at death: 102 agent transcripts (1 scope, 6 search, 20 fetch, 75 verify). All scope/search/fetch outputs recovered (99 extracted claims). Verification: 25 distinct claims entered the verify phase with 3 adversarial voters each; 30 of those 75 voters returned verdicts covering 11 claims (all upheld, zero refutations), 45 voters died (spend limit or truncated). 5 of the 30 verdicts were recovered from transcript tool calls that the workflow journal never recorded.

Citation fidelity: every claim below carries exactly the source its transcript attached. Quotes are verbatim from the agents' extraction output.

---

## VERIFIED CLAIMS

Each claim was reviewed by up to 3 adversarial verifiers instructed to refute it (>=2/3 refutations kill a claim). Every returned verdict below is refuted=false at high confidence.

### "RuneLite gameval NpcID.java defines the Fortis Colosseum wave-NPC roster with exact NPC IDs and in-game display names: Jaguar warrior = 12810, Serpent shaman = 12811 (internal name COLOSSEUM_STANDARD_MAGER), Minotaur = 12812, Fremennik warband archer = 12814 (COLOSSEUM_WARBANDER_RANGED_FEMALE), Fremennik warband seer = 12815 (COLOSSEUM_WARBANDER_MAGE_MALE), Fremennik warband berserker = 12816 (COLOSSEUM_WARBANDER_MELEE_MALE), Javelin Colossus = 12817, Manticore = 12818, Shockwave Colossus = 12819, and Sol Heredit = 12821 (COLOSSEUM_SOL_P1, implying a phase-1 designation) plus a seated Sol variant COLOSSEUM_BOSS_SEATED = 12827."

- Source: https://github.com/runelite/runelite/blob/master/runelite-api/src/main/java/net/runelite/api/gameval/SpotanimID.java
- Verdict: UPHELD, 3/3 voters returned, 0 refutations, all high confidence
- Reasoning: All 11 NPC-ID constants and javadoc display names re-fetched verbatim from runelite master (2026-06-10).
- Verifier corrections: the constants live in NpcID.java, not the cited SpotanimID.java (claim text names the right file, the citation URL is wrong; correct URL: https://github.com/runelite/runelite/blob/master/runelite-api/src/main/java/net/runelite/api/gameval/NpcID.java, lines ~55873-55950). Claim omits COLOSSEUM_MINOTAUR_ROUTEFIND = 12813 (also javadoc 'Minotaur').

### "SpotanimID.java hardcodes per-attack graphics IDs for Colosseum wave NPCs: Manticore has distinct projectile and impact spotanims per combat style (magic 2681/2682, ranged 2683/2684, melee 2685/2686), the Javelin Colossus has spearhead spotanim 2673 plus separate SLOW and FAST artillery spotanims 2674/2675 (and fire variants 2676-2678), and the Shockwave Colossus attack is internally a 'clap attack' (spotanim 2679). AnimationID.java additionally defines Manticore NPC_MANTICORE_01_TRIPLE_CHARGE = 10868 and NPC_MANTICORE_01_TRIPLE_THROW = 10869, confirming a charge-then-throw triple-orb attack in game data (orb ORDER is not encoded in these files)."

- Source: https://github.com/runelite/runelite/blob/master/runelite-api/src/main/java/net/runelite/api/gameval/SpotanimID.java
- Verdict: UPHELD, 2/3 voters returned, 0 refutations, all high confidence (1 voter(s) died before voting)
- Reasoning: All 13 graphics/animation IDs verified exactly against SpotanimID.java lines 2680-2693 and AnimationID.java lines 10872-10873 on master (2026-06-10).

### "Wave compositions 1-11 follow fixed rules: 3 Fremennik every wave (4 with Quartet); 1 Serpent Shaman as an initial spawn on waves 1-6; Javelin Colossus appears on waves 2 (x1) and 3 (x2), skips waves 1 and 4, then from wave 5 alternates 1 on odd waves and 2 on even waves; Manticore appears from wave 4, single on waves 4-8 and double on waves 9-11; Shockwave Colossus appears only on waves 7, 8, and 11 (doubled by Dynamic Duo). Wave 12 is Sol Heredit alone (plus 1 Fremennik if Quartet is active), and the Bees! modifier adds an Angry Bees spawn."

- Source: https://github.com/LlemonDuck/fortis-colosseum
- Verdict: UPHELD, 3/3 voters returned, 0 refutations, all high confidence
- Reasoning: WaveSpawns.java implements every clause verbatim (QUARTET?4:3 frems; shaman wave<=6; javelins wave-1 on 2-3 and 2-(wave%2) for wave>=5; manticore wave>=4 single<=8; shockwave 7/8/11 x2 under DYNAMIC_DUO; wave 12 = 1 SOL_HEREDIT + 1 FREMENNIK iff QUARTET); wiki Strategies wave-breakdown matches all 12 waves.
- Verifier caveats: wave-12 Quartet Fremennik and Bees spawn count rest on the plugin alone (uncontradicted); Bees II/III spawn level-count swarms, so 'an Angry Bees spawn' is tier-I phrasing. Main-page prose 'two javelin colossi present... at waves 3, 6, 8 and 10' names the double-javelin waves, not NPC debut waves (Strategies table places manticore debut at wave 4).

### "The Red Flag modifier's Minotaur is a distinct NPC variant whose RuneLite gameval constant is COLOSSEUM_MINOTAUR_ROUTEFIND (the plugin comments it 'Minotaur (Red Flag)'), indicating Red Flag swaps the Minotaur to a route-finding NPC; the plugin's trackable wave roster is exactly six NPC types: Serpent shaman (gameval name COLOSSEUM_STANDARD_MAGER), Javelin Colossus, Jaguar warrior, Manticore, Minotaur (two ID variants), and Shockwave Colossus, with no Fremennik or Sol Heredit entries."

- Source: https://github.com/willediger/Colosseum-Waves
- Verdict: UPHELD, 3/3 voters returned, 0 refutations, all high confidence
- Reasoning: COLOSSEUM_WAVE_NPCS map verified: exactly 7 entries / 6 NPC types with '// Minotaur (Red Flag)' on COLOSSEUM_MINOTAUR_ROUTEFIND; repo-wide grep confirms no Fremennik/Sol entries; gameval NpcID (12812/12813) and wiki Minotaur infobox (version Normal id 12812, version Red Flag id 12813) corroborate routefind behavior.

### "The Manticore charges exactly three orbs, one each of magic, ranged, and melee, identified by spotanims VFX_MANTICORE_01_PROJECTILE_MAGIC_01 / _RANGED_01 / _MELEE_01; the plugin's LoS-export encoding treats an order whose third orb is melee as the canonical pattern (encodable by the first orb alone, i.e. only the magic/ranged order varies and melee fires last), and any other completed order is labelled an 'unknown variant'."

- Source: https://github.com/LlemonDuck/fortis-colosseum
- Verdict: UPHELD, 3/3 voters returned, 0 refutations, all high confidence
- Reasoning: ManticoreOrbOrder.toLoSCode() logic verified verbatim (melee-third encoded by first orb alone, other completed orders take the 'unknown variant' path); wiki corroborates range-magic or magic-range first two with melee always last, Mantimayhem III being the non-canonical case.

### "AnimationID.java enumerates Sol Heredit's attack kit with exact animation IDs, including separate telegraph animations and TWO triple-attack sequence variants: melee attack 10882, melee attack telegraph 10883, grapple attack telegraph 10884, shield-slam telegraph 10885, triple attack 10886, a distinct 'shorter' triple attack 10887, arena jump 10876 / land 10877, walk 10878, sitting idle 10875, death 10888. SpotanimID.java pairs this with four melee spotanims SPOTANIM_COLOSSI_FINALBOSS_01..04_MELEE = 2669-2672 and eight stab dust spotanims VFX_COLOSSI_STAB_DUST_01..08 = 2699-2706 (matching an 8-pattern AoE dust mechanic). Exact tick timings are NOT in these files."

- Source: https://github.com/runelite/runelite/blob/master/runelite-api/src/main/java/net/runelite/api/gameval/SpotanimID.java
- Verdict: UPHELD, 1/3 voters returned, 0 refutations, all high confidence (2 voter(s) died before voting)
- Reasoning: Every claimed constant verified against live AnimationID.java/SpotanimID.java; the _SHORTER triple-attack variant is independently corroborated by SpotanimID telegraph constants NPC_COLOSSI_COLOSSI_TRIPLEATTACK_01_TELEGRAPH=2667/_SHORTER=2668.
- Verifier caveat: '8-pattern AoE dust mechanic' is an inference from the 8 VFX constants, not file content.

### "The Fortis Colosseum arena occupies map region ID 7216; the plugin determines whether the player is in the Colosseum by checking loaded map regions against this single hardcoded constant."

- Source: https://github.com/willediger/Colosseum-Waves
- Verdict: UPHELD, 3/3 voters returned, 0 refutations, all high confidence
- Reasoning: Constant verbatim at ColosseumWavesPlugin.java line 68; isInColosseum() checks loaded map regions against it alone. Corroborated by RuneLite core DiscordGameEventType.java (Fortis Colosseum minigame = 7216, lobby = 7316) and blert-io/plugin. Region math: 7216 = (28<<8)|48.

### "The complete modifier set is exactly 14 entries: Bees!, Blasphemy, Doom, Dynamic Duo, Frailty, Mantimayhem, Myopia, Reentry, Red Flag, Relentless, Solarflare, Quartet, Totemic, Volatility. Ten are tiered I/II/III (three sprite IDs plus a level varbit, e.g. Bees varbit 9791, Blasphemy 9790, Doom 10681, Frailty 9796, Mantimayhem 4588, Myopia 9795, Reentry 9792, Relentless 9798, Solarflare 9797, Volatility 9799) while Dynamic Duo, Red Flag, Quartet, and Totemic are untiered (single sprite, levelVarb -1). Each modifier has an internal bitmask ID (e.g. Mantimayhem 0, Reentry 1, Bees 2, Volatility 3, Blasphemy 4, Relentless 5, Quartet 6, Totemic 7, Doom 8, Dynamic Duo 9, Solarflare 10, Myopia 11, Frailty 12, Red Flag 13)."

- Source: https://github.com/LlemonDuck/fortis-colosseum
- Verdict: UPHELD, 3/3 voters returned, 0 refutations, all high confidence
- Reasoning: Modifier.java enum verified verbatim (14 entries, 10 tiered with level varbits, 4 untiered levelVarb -1, all 14 bitmask IDs); wiki Modifiers page lists the same 14 with the same tiered/untiered split; hidden wiki comment maps cache enum 5312 structs 891-915 consistently with the plugin IDs (Mantimayhem in removed Doom Scorpion's slot 0).
- Verifier caveat: varbit/sprite numbers rest on the plugin's hardcoded mapping (corroborated only by the plugin functioning in production, not an independent cache dump). Modifier.java last changed 2024-04-10; no modifier added/removed since.

### "Hardcoded client IDs: the Colosseum lobby is map region 7316 and the arena is instanced map region 7216; the modifier-selection interface is initialized by clientscript 4931 whose args carry exactly 3 offered modifier options per wave (args 2-4) plus a bitmask of already-active modifiers (arg 8); varbit 9788 stores the 1-based index of the selected modifier (0 = none selected); the modifier-offering NPC Minimus has NPC ID 12808; wave 12 start is signalled by the game message 'Sol Heredit jumps down from his seat'."

- Source: https://github.com/LlemonDuck/fortis-colosseum
- Verdict: UPHELD, 3/3 voters returned, 0 refutations, all high confidence
- Reasoning: Every element verified in ColosseumStateTracker.java and independently corroborated by two unrelated production trackers (blert-io/plugin ColosseumChallenge.java, Marco648135/cTimers ColosseumHandler.java) using identical script 4931 / args[2..4] / varbit 9788 / NPC 12808 / boss-start message.
- Verifier caveats: wiki lists Minimus under two NPC IDs (12807 and 12808; 12808 is the in-arena modifier-offering variant); the args[8] active-modifier bitmask is single-sourced to LlemonDuck's plugin; full in-game message ends with '...'.

### "Reinforcement composition per wave: Jaguar Warrior is reinforcement-only and reinforces on every wave 1-6; from wave 7 onward the Minotaur replaces it as the reinforcement; the Serpent Shaman additionally arrives as a reinforcement on waves 4-6 and 10-11. (This repo encodes which NPCs reinforce per wave but contains no reinforcement timer value, so the exact tick delay remains UNKNOWN from this source.)"

- Source: https://github.com/LlemonDuck/fortis-colosseum
- Verdict: UPHELD, 3/3 voters returned, 0 refutations, all high confidence
- Reasoning: WaveSpawns.java gates verified (jaguar reinforcement-only wave<=6, minotaur wave>=7, shaman (4<=w<=6)||(w>=10) with wave 12 early-return); three wiki pages corroborate; repo-wide grep confirms no reinforcement timer constant exists, so the tick delay is genuinely UNKNOWN from this source.
- Verifier note: the wiki separately supplies the timer the repo lacks, reinforcements arrive 'if the player does not complete a wave within 40 seconds'.

### "Manticores always spawn uncharged and are fully charged at exactly three orbs; the plugin detects orb charge-up per orb type via the RuneLite spotanim constants VFX_MANTICORE_01_PROJECTILE_MAGIC_01, VFX_MANTICORE_01_PROJECTILE_RANGED_01, and VFX_MANTICORE_01_PROJECTILE_MELEE_01 appearing on the NPC, polled every game tick because graphics events may not fire when NPCs are behind pillars."

- Source: https://github.com/willediger/Colosseum-Waves
- Verdict: UPHELD, 3/3 voters returned, 0 refutations, all high confidence
- Reasoning: All three spotanim constants, isCharged() >= 3 orbs, 'ALWAYS uncharged at spawn' comment, and the per-tick polling rationale ('GraphicChanged events may not fire when NPCs are behind pillars') verified verbatim in ManticoreHandler.java / ColosseumWavesPlugin.java; wiki Manticore page corroborates.

### Claims whose verification died entirely (spawned, 0/3 voters returned)

These 14 claims entered the verify phase but got no verdict. They remain in EXTRACTED CLAIMS below with full quotes; treat as unverified.

- "Mantimayhem is a stack-based modifier whose level is exposed in the client varbit COLOSSEUM_MODIFIER_MANTIMAYHEM_STACKS_CLIENT, with 'Mantimayhem 3' meaning stacks >= 3; under Mantimayhem 3 the standard manticore cha...
  (source: https://github.com/willediger/Colosseum-Waves)
- "Mantimayhem was added as a new modifier in the 10 April 2024 update: Tier 1 adds an additional projectile per Manticore orb (twice as many hits per attack), Tier 2 adds a venom effect to Manticore damage, and Tier 3 ...
  (source: https://oldschool.runescape.wiki/w/Update:Undead_Pirates,_Colosseum_Changes_&_more!)
- "As of the 10 April 2024 update, the Relentless modifier at tiers 1 and 2 causes enemy attacks to ignore 33% and 66% of the player's Defence respectively, while tier 3 ignores all Defence."
  (source: https://oldschool.runescape.wiki/w/Update:Undead_Pirates,_Colosseum_Changes_&_more!)
- "As of the 3 April 2024 update, the Myopia modifier affects autocast spells (autocasting no longer bypasses the modifier), while manual casting remains unaffected by it."
  (source: https://oldschool.runescape.wiki/w/Update:Varlamore_Tweaks_&_Drop_Rates)
- "As of the 24 April 2024 update, the Mantimayhem modifier is no longer offered as a modifier option going into Wave 12 of the Fortis Colosseum, because it has no effect during the Sol Heredit fight. This also confirms...
  (source: https://oldschool.runescape.wiki/w/Update:Varlamore_Tweaks,_GameJam_V_Commences_&_More!)
- "A Jaguar warrior spawns as a delayed reinforcement during wave 1, and killing the initial enemies quickly enough prevents it from spawning (no timer value is given on this page)."
  (source: https://oldschool.runescape.wiki/w/Money_making_guide/Completing_the_Fortis_Colosseum_(Wave_1))
- "As of the 3 April 2024 update, the Relentless modifier's defence-ignoring effect is 33% at stack 1, 66% at stack 2, and 100% (all defences) at stack 3, while the additional damage per stack was left unchanged."
  (source: https://oldschool.runescape.wiki/w/Update:Varlamore_Tweaks_&_Drop_Rates)
- "Wave 1 of the Fortis Colosseum starts with 4 enemies spawned close enough together that a single AoE special targeted on the Serpent shaman hits all of them; the initial group includes a Serpent shaman and multiple F...
  (source: https://oldschool.runescape.wiki/w/Money_making_guide/Completing_the_Fortis_Colosseum_(Wave_1))
- "The Manticore's triple attack always ends with melee, and the first two hits are either ranged-then-magic or magic-then-ranged (the page does not state what determines which of the two orderings is used, nor whether ...
  (source: https://oldschool.runescape.wiki/w/Manticore)
- "The Mantimayhem modifier (introduced/described in the 3 April 2024 update) makes Manticore orb attacks deal more damage per tier chosen but does not change the number of prayer flicks required against the Manticore."
  (source: https://oldschool.runescape.wiki/w/Update:Varlamore_Tweaks_&_Drop_Rates)
- "As of the 10 April 2024 update, Doom is a stackable modifier: stacks reset to 0 at the end of each wave, death is guaranteed at 15, 10, or 5 stacks depending on the active modifier tier, and only Colosseum-sourced da...
  (source: https://oldschool.runescape.wiki/w/Update:Undead_Pirates,_Colosseum_Changes_&_more!)
- "As of the 10 April 2024 update, Totemic totems respawn 2 minutes after destruction (previously 1 minute) and heal the targeted NPC for 30% of its health (previously 40%). Additionally, under Blasphemy and Myopia: sel...
  (source: https://oldschool.runescape.wiki/w/Update:Undead_Pirates,_Colosseum_Changes_&_more!)
- "As of the 3 April 2024 update, the Doom modifier has three tiers with death thresholds of 15, 10, and 5 Doom stacks respectively, stacks reset to 0 at the end of each wave, and the Soul Reaper Axe and Divine Potions ...
  (source: https://oldschool.runescape.wiki/w/Update:Varlamore_Tweaks_&_Drop_Rates)
- "As of the 10 April 2024 update, the Bees! modifier's swarms move every 12 ticks (7.2s), up from 7 ticks (4.2s), and respawn 50 ticks (30s) after being killed, up from 18 ticks (~11s)."
  (source: https://oldschool.runescape.wiki/w/Update:Undead_Pirates,_Colosseum_Changes_&_more!)

---

## EXTRACTED CLAIMS (UNVERIFIED)

Every falsifiable claim the 20 fetch agents extracted, grouped by source, quotes verbatim. Importance labels (central/supporting/tangential) are the fetch agents' own. Verification markers cross-reference the section above.

### https://www.youtube.com/watch?v=dxqrQQNl2_4

Title: OSRS Fortis Colosseum Quick Guide - Resources & Plugins (with wave solves)  
Source quality (fetch agent's label): blog | publish date: 2024-03-23

1. (central) Modifier draft rule: picking a tier 1 modifier increases the probability that its tier 2 version is offered in later wave drafts (modifiers were called 'Invocations' at launch). Player should 'invest' in tier 1 picks to steer future offers.
   > I don't emphasize this enough in the video, YOU ARE MORE LIKELY TO BE OFFERED TIER 2 INVOS OF THE TIER 1s you pick. If you are offered a good tier 2 or an ok tier 1, you should "invest" in picking the tier 1 to prioritize it in your upcoming invocation offerings.

2. (central) Jagex shipped a balance change to the Colosseum modifiers ('Invocations'), specifically including Myopia, on 10 April 2024 (US-format 4/10/2024), invalidating week-1 gear recommendations. Checkable against official patch notes in the March-June 2024 window.
   > My new recommended 0kc setups budget/max(Arceus Spellbook Death Charge) https://imgur.com/a/94lmjnI after the 4/10/2024 changes to Invocations & Myopia

3. (central) Sol Heredit's grapple attack targets specific equipment slots including the FEET slot, and the targeted slot is announced via a chatbox message (detectable by the RuneLite Watchdog plugin via a chatbox trigger).
   > My full Watchdog configuration export (Sol Heredit Grapple Chatbox Popup) (now actually working for the feet slot grapple): https://pastebin.com/jQ0a4uBX

4. (supporting) Wave NPC spawn locations follow documented, manipulable mechanics: a community spawn map by 'B5' exists (imgur G21mpEz) and a 'spawn fixing' method by Wotury & B5 was published on r/2007scape in week 1, implying spawn positions are deterministic enough to be forced/corrected by player behavior.
   > Colosseum Spawns: https://i.imgur.com/G21mpEz.png by B5 Spawn Fixing guide by Wotury & B5: https://www.reddit.com/r/2007scape/comments/1bka2f6/colosseum_spawn_mechanics_spawn_fixing_method_by/

5. (supporting) Fremennik Warband behavior runs on a fixed tick cycle anchored to the wave start: a community 'Tick Cycle Writeup' documents it, and safe handling is done by timing wave starts with a visual metronome synced to that cycle.
   > I also go through my plugin setup that helps me time my wave starts and handle the Fremenniks safely ... Fremennik Warband & Tick Cycle Writeup by I don't know who?: https://i.imgur.com/8ict0rl.png Visual Metronome Config for above Tick Cycle Writeup: https://i.imgur.com/vWd5XjY.png

### https://oldschool.runescape.wiki/w/Javelin_Colossus

Title: Javelin Colossus - OSRS Wiki  
Source quality (fetch agent's label): secondary | publish date: 2026-04-28 (page last modified)

1. (central) The Javelin Colossus fires its javelin-rain special every 5 attacks; the javelin lands on the tile the player occupied at launch a few ticks later (exact tick delay not specified on this page), and the hit ignores Protect from Missiles.
   > Every five attacks, they will launch a javelin high into the air, landing on the player's current position a few ticks afterwards. This will deal heavy damage that ignores Protect from Missiles.

2. (central) The Javelin Colossus is a ranged attacker with a 5-tick (3.0 second) attack speed and a 15-tile attack range.
   > Attack speed: 5 ticks (3.0 seconds) ... throwing their javelins at the player with a range of 15 tiles

3. (central) The Javelin Colossus has a base max hit of 48, which the Relentless modifier raises per tier to 49 (Relentless I), 51 (Relentless II), and 54 (Relentless III).
   > Max hit: 48, 49 (Relentless I), 51 (Relentless II), 54 (Relentless III)

4. (supporting) The Javelin Colossus is NPC ID 12817, combat level 278, with 220 hitpoints, and occupies a 3x3 tile footprint.
   > Combat level: 278 ... Hitpoints: 220 ... Monster ID: 12817 ... Size: 3x3

5. (tangential) The Javelin Colossus is immune to poison, venom, and cannon damage, and the wiki lists its release date as 20 March 2024 (note: one week earlier than the 27 March 2024 Colosseum release date stated in the research question).
   > Immunities: Poison and venom (100% resistance); immune to cannons ... Released: 20 March 2024

### https://github.com/runelite/runelite/blob/master/runelite-api/src/main/java/net/runelite/api/gameval/SpotanimID.java

Title: runelite/runelite gameval SpotanimID.java (+ sibling NpcID.java, AnimationID.java)  
Source quality (fetch agent's label): primary | publish date: 2026-06-02 (last commit touching SpotanimID.java on master; file header: "This file is automatically generated. Do not edit.")

1. (central) [UPHELD 3/3 voters returned, 0 refutations] RuneLite gameval NpcID.java defines the Fortis Colosseum wave-NPC roster with exact NPC IDs and in-game display names: Jaguar warrior = 12810, Serpent shaman = 12811 (internal name COLOSSEUM_STANDARD_MAGER), Minotaur = 12812, Fremennik warband archer = 12814 (COLOSSEUM_WARBANDER_RANGED_FEMALE), Fremennik warband seer = 12815 (COLOSSEUM_WARBANDER_MAGE_MALE), Fremennik warband berserker = 12816 (COLOSSEUM_WARBANDER_MELEE_MALE), Javelin Colossus = 12817, Manticore = 12818, Shockwave Colossus = 12819, and Sol Heredit = 12821 (COLOSSEUM_SOL_P1, implying a phase-1 designation) plus a seated Sol variant COLOSSEUM_BOSS_SEATED = 12827.
   > /** Serpent shaman */ public static final int COLOSSEUM_STANDARD_MAGER = 12811; ... /** Fremennik warband berserker */ public static final int COLOSSEUM_WARBANDER_MELEE_MALE = 12816; ... /** Javelin Colossus */ public static final int COLOSSEUM_JAVELIN_COLOSSUS = 12817; /** Manticore */ public static final int COLOSSEUM_MANTICORE = 12818; /** Shockwave Colossus */ public static final int COLOSSEUM_SHOCKWAVE_COLOSSUS = 12819; ... /** Sol Heredit */ public static final int COLOSSEUM_SOL_P1 = 12821;

2. (central) [UPHELD 2/3 voters returned, 0 refutations] SpotanimID.java hardcodes per-attack graphics IDs for Colosseum wave NPCs: Manticore has distinct projectile and impact spotanims per combat style (magic 2681/2682, ranged 2683/2684, melee 2685/2686), the Javelin Colossus has spearhead spotanim 2673 plus separate SLOW and FAST artillery spotanims 2674/2675 (and fire variants 2676-2678), and the Shockwave Colossus attack is internally a 'clap attack' (spotanim 2679). AnimationID.java additionally defines Manticore NPC_MANTICORE_01_TRIPLE_CHARGE = 10868 and NPC_MANTICORE_01_TRIPLE_THROW = 10869, confirming a charge-then-throw triple-orb attack in game data (orb ORDER is not encoded in these files).
   > public static final int NPC_COLOSSI_JAVELIN_01_ARTILLERY_SLOW = 2674; public static final int NPC_COLOSSI_JAVELIN_01_ARTILLERY_FAST = 2675; ... public static final int NPC_COLOSSI_SHOCKWAVE_01_CLAPATTACK = 2679; ... public static final int VFX_MANTICORE_01_PROJECTILE_MAGIC_01 = 2681; public static final int VFX_MANTICORE_01_PROJECTILE_IMPACT_MAGIC_01 = 2682; public static final int VFX_MANTICORE_01_PROJECTILE_RANGED_01 = 2683; ... public static final int VFX_MANTICORE_01_PROJECTILE_MELEE_01 = 2685; [AnimationID.java] public static final int NPC_MANTICORE_01_TRIPLE_CHARGE = 10868; public static final int NPC_MANTICORE_01_TRIPLE_THROW = 10869;

3. (central) [UPHELD 1/3 voters returned, 0 refutations] AnimationID.java enumerates Sol Heredit's attack kit with exact animation IDs, including separate telegraph animations and TWO triple-attack sequence variants: melee attack 10882, melee attack telegraph 10883, grapple attack telegraph 10884, shield-slam telegraph 10885, triple attack 10886, a distinct 'shorter' triple attack 10887, arena jump 10876 / land 10877, walk 10878, sitting idle 10875, death 10888. SpotanimID.java pairs this with four melee spotanims SPOTANIM_COLOSSI_FINALBOSS_01..04_MELEE = 2669-2672 and eight stab dust spotanims VFX_COLOSSI_STAB_DUST_01..08 = 2699-2706 (matching an 8-pattern AoE dust mechanic). Exact tick timings are NOT in these files.
   > public static final int NPC_COLOSSI_FINALBOSS_01_MELEE_ATTACK = 10882; public static final int NPC_COLOSSI_FINALBOSS_01_MELEE_ATTACK_TELEGRAPH = 10883; public static final int NPC_COLOSSI_FINALBOSS_01_GRAPPLE_ATTACK_TELEGRAPH = 10884; public static final int NPC_COLOSSI_FINALBOSS_01_SHIELDSLAM_TELEGRAPH = 10885; public static final int NPC_COLOSSI_FINALBOSS_TRIPLEATTACK = 10886; public static final int NPC_COLOSSI_FINALBOSS_TRIPLEATTACK_SHORTER = 10887; [SpotanimID.java] public static final int VFX_COLOSSI_STAB_DUST_01 = 2699; ... public static final int VFX_COLOSSI_STAB_DUST_08 = 2706;

4. (supporting) The Colosseum Minotaur exists as two distinct NPC IDs both displaying the name 'Minotaur': COLOSSEUM_MINOTAUR = 12812 and COLOSSEUM_MINOTAUR_ROUTEFIND = 12813. Game data therefore contains a dedicated route-finding Minotaur variant, i.e. two movement-behavior configurations (the files do not state which condition, e.g. the Red Flag modifier, selects the routefind variant — that mapping is UNKNOWN from this source).
   > /** Minotaur */ public static final int COLOSSEUM_MINOTAUR = 12812; /** Minotaur */ public static final int COLOSSEUM_MINOTAUR_ROUTEFIND = 12813;

5. (supporting) Modifier-spawned entities have dedicated NPC IDs in NpcID.java: Doom Scorpion = 12822, Bee Swarm = 12823 (internal COLOSSEUM_MODIFIER_BEES), beam crystal = 12824 (COLOSSEUM_BEAM_CRYSTAL), Healing totem = 12825, solar flare = 12826 (COLOSSEUM_SOLAR_FLARE); SpotanimID.java gives matching graphics (totem projectile 2687 / impact 2688, crystal charge beams 2689-2692, crystal attack beams 2693-2696 + impact 2697, sunfire lightning 2698, bee jar impact 2707 / travel 2708, hot sand projectiles 2709/2710, Doom scorpion player-death 2711 and attack impact 2712), confirming Doom, Bees, Totemic, Solarflare and a crystal-beam mechanic exist as engine entities. Per-tier modifier effects are NOT encoded here.
   > public static final int COLOSSEUM_DOOM_SCORPION = 12822; ... public static final int COLOSSEUM_MODIFIER_BEES = 12823; public static final int COLOSSEUM_BEAM_CRYSTAL = 12824; ... public static final int COLOSSEUM_HEALING_TOTEM = 12825; public static final int COLOSSEUM_SOLAR_FLARE = 12826; [SpotanimID.java] public static final int VFX_COLOSSEUM_BEE_JAR_IMPACT_01 = 2707; public static final int VFX_COLOSSEUM_BEE_JAR_TRAVEL = 2708;

### https://oldschool.runescape.wiki/w/Minotaur_(Fortis_Colosseum)

Title: Minotaur (Fortis Colosseum) - OSRS Wiki  
Source quality (fetch agent's label): secondary | publish date: 2026-04-28

1. (central) Minotaur heal decision rule: each time its action timer fires, it melees the player if the player is in melee range; otherwise it scans a 10-tile radius from its centre coordinate (respecting line of sight) and heals an NPC that is not a minotaur, is below 75% of total hitpoints, has its centre coordinate in line of sight of the minotaur's centre, and is 7 or fewer tiles away. The timer's period (heal frequency) is not stated on the page.
   > if the player is in melee range when the timer runs, it will melee you. If you're not, then it will scan for a radius of 10 tiles from it's centre coordinate, respecting line of sight. If it finds an NPC in this radius that is: - not a minotaur - has less than 75% of its total hitpoints - its centre coordinate is also in the line of sight to the minotaurs centre coordinate - and is 7 or less tiles away Then it heals the npc

2. (central) The minotaur's heal restores wounded allies to full health, and it only attempts to heal when the player is not within melee distance (including diagonally). The page's intro states a 6-tile heal range, which conflicts with the 7-tile cap given in the detailed mechanics section on the same page.
   > If the player is not within melee distance (including diagonally) the minotaur will attempt to heal other wounded monsters to full health within 6 tiles of it.

3. (central) Minotaur combat profile: combat level 318, 225 hitpoints, melee attacks with a max hit of 74, attack speed 5 ticks (3.0 seconds). Defence bonuses are +0 across stab/slash/crush/magic and its size is 3x3 tiles.
   > Combat level: 318, Hitpoints: 225, Max hit: 74, Attack speed: 5 ticks (3.0 seconds)

4. (central) The minotaur's melee damage is applied one game tick after its attack animation plays, making it tick-eatable (same delayed-damage behavior as Vardorvis). This is a load-bearing hit-delay detail for an exact tick simulator.
   > Like Vardorvis, the minotaur can be tick eaten, as its damage is calculated one tick later than its attack animation despite being a melee attack.

5. (central) Under the Red Flag modifier, minotaurs gain routefinding pathfinding, letting them move around obstacles and making them impossible to safespot. The minotaur has two NPC ID variants (base Monster ID 12812, plus a separate ID for the Red Flag variant, listed as 12813 in the infobox), and it begins moving immediately upon spawning before visually appearing.
   > If the Red Flag handicap is active, minotaurs will gain the ability to routefind which allows it to move around obstacles and becoming impossible to safespot.

### https://oldschool.runescape.wiki/w/Update:Undead_Pirates,_Colosseum_Changes_&_more!

Title: Update: Undead Pirates, Colosseum Changes & more! (10 April 2024, revised 12 April) - OSRS Wiki archive of official Jagex post  
Source quality (fetch agent's label): primary | publish date: 2024-04-10

1. (central) [verification spawned, all 3 voters died (no verdict)] As of the 10 April 2024 update, the Bees! modifier's swarms move every 12 ticks (7.2s), up from 7 ticks (4.2s), and respawn 50 ticks (30s) after being killed, up from 18 ticks (~11s).
   > Increased time between movements from 7 ticks (4.2s) to 12 ticks (7.2s). Increased respawn delay from 18 ticks (~11s) to 50 ticks (30s).

2. (central) [verification spawned, all 3 voters died (no verdict)] As of the 10 April 2024 update, Doom is a stackable modifier: stacks reset to 0 at the end of each wave, death is guaranteed at 15, 10, or 5 stacks depending on the active modifier tier, and only Colosseum-sourced damage applies stacks (self-dealt damage such as Soulreaper Axe or Divine potions does not).
   > Doom stacks reset to 0 at the end of each wave. Death is guaranteed at 15, 10, or 5 stacks of Doom depending on the active modifier tier. Doom stacks will only be applied by Colosseum sources. Instances of self-dealt damage like the Soulreaper Axe or Divine Potions will no longer add Doom stacks.

3. (central) [verification spawned, all 3 voters died (no verdict)] Mantimayhem was added as a new modifier in the 10 April 2024 update: Tier 1 adds an additional projectile per Manticore orb (twice as many hits per attack), Tier 2 adds a venom effect to Manticore damage, and Tier 3 removes the forced Melee orb from the final slot of the Manticore's attack sequence — which also confirms the Manticore's baseline attack sequence ends in a forced Melee orb.
   > Tier 1 adds an additional projectile per orb, meaning you'll get hit twice as many times for each attack, making hits off-Prayer more punishing. Tier 2 adds a Venom effect to any damage received from the Manticore's attacks. Tier 3 removes the 'forced' Melee orb in the final slot of the Manticore's attack sequence.

4. (central) [verification spawned, all 3 voters died (no verdict)] As of the 10 April 2024 update, the Relentless modifier at tiers 1 and 2 causes enemy attacks to ignore 33% and 66% of the player's Defence respectively, while tier 3 ignores all Defence.
   > Tiers 1 and 2 now cause enemy attacks to ignore 33% and 66% of your Defence, respectively. Tier 3 is unchanged and ignores all Defence.

5. (central) [verification spawned, all 3 voters died (no verdict)] As of the 10 April 2024 update, Totemic totems respawn 2 minutes after destruction (previously 1 minute) and heal the targeted NPC for 30% of its health (previously 40%). Additionally, under Blasphemy and Myopia: self-dealt damage no longer drains Prayer Points, and Myopia affects autocast spells but not manually-cast spells.
   > Increased respawn timer from 1 minute to 2 minutes after a totem has been destroyed. Reduced healing from 40% to 30% of the targeted NPCs health.

### https://github.com/willediger/Colosseum-Waves

Title: willediger/Colosseum-Waves — spawn/reinforcement tile capture plugin with ManticoreHandler  
Source quality (fetch agent's label): primary | publish date: 2025-08-13

1. (central) [UPHELD 3/3 voters returned, 0 refutations] The Fortis Colosseum arena occupies map region ID 7216; the plugin determines whether the player is in the Colosseum by checking loaded map regions against this single hardcoded constant.
   > private static final int COLOSSEUM_REGION_ID = 7216;

2. (central) [UPHELD 3/3 voters returned, 0 refutations] The Red Flag modifier's Minotaur is a distinct NPC variant whose RuneLite gameval constant is COLOSSEUM_MINOTAUR_ROUTEFIND (the plugin comments it 'Minotaur (Red Flag)'), indicating Red Flag swaps the Minotaur to a route-finding NPC; the plugin's trackable wave roster is exactly six NPC types: Serpent shaman (gameval name COLOSSEUM_STANDARD_MAGER), Javelin Colossus, Jaguar warrior, Manticore, Minotaur (two ID variants), and Shockwave Colossus, with no Fremennik or Sol Heredit entries.
   > .put(NpcID.COLOSSEUM_MINOTAUR, 5) // Minotaur .put(NpcID.COLOSSEUM_MINOTAUR_ROUTEFIND, 5) // Minotaur (Red Flag)

3. (supporting) The plugin classifies tracked NPCs spawning more than 10 game ticks after the wave-start chat message as reinforcement spawns (initial wave spawns are expected within the first 10 ticks), and detects wave boundaries via the exact game messages 'Wave: <n>' and 'Wave <n> completed'. Note this 10-tick cutoff is the author's classification heuristic for separating the two spawn groups, not a measured in-game reinforcement timer.
   > if (ticksSinceWaveStart > 10 && !reinforcementsPhase) { reinforcementsPhase = true; ... Pattern.compile("Wave: (\\d+)") ... Pattern.compile("Wave (\\d+) completed")

4. (central) [UPHELD 3/3 voters returned, 0 refutations] Manticores always spawn uncharged and are fully charged at exactly three orbs; the plugin detects orb charge-up per orb type via the RuneLite spotanim constants VFX_MANTICORE_01_PROJECTILE_MAGIC_01, VFX_MANTICORE_01_PROJECTILE_RANGED_01, and VFX_MANTICORE_01_PROJECTILE_MELEE_01 appearing on the NPC, polled every game tick because graphics events may not fire when NPCs are behind pillars.
   > // For initial spawn, manticores are ALWAYS uncharged at spawn ... boolean isCharged() { return orbOrder.size() >= 3; } ... private static final int MAGIC_ORB_GRAPHIC_ID = SpotanimID.VFX_MANTICORE_01_PROJECTILE_MAGIC_01;

5. (central) [verification spawned, all 3 voters died (no verdict)] Mantimayhem is a stack-based modifier whose level is exposed in the client varbit COLOSSEUM_MODIFIER_MANTIMAYHEM_STACKS_CLIENT, with 'Mantimayhem 3' meaning stacks >= 3; under Mantimayhem 3 the standard manticore charge sequence ends with melee as the third orb (non-melee-third sequences are non-standard), while without MM3 the first orb alone determines the manticore's attack pattern.
   > int mantimayhemLevel = client.getVarbitValue(COLOSSEUM_MODIFIER_MANTIMAYHEM_STACKS_CLIENT); boolean mm3Active = mantimayhemLevel >= 3; ... // If third orb is melee, it's a standard sequence - abbreviate ... // Without MM3, having first orb is enough // With MM3, need full sequence (3 orbs)

### https://oldschool.runescape.wiki/w/Fremennik_warband

Title: Fremennik warband - OSRS Wiki  
Source quality (fetch agent's label): secondary | publish date: None

1. (supporting) The Fremennik warband is a trio of NPCs (berserker, seer, archer) that spawns during the Fortis Colosseum.
   > The Fremennik warband is a trio of enemies that spawn in during the Fortis Colosseum.

2. (central) The warband attacks on a fixed 6-tick cycle in a strict order: berserker first, seer 1 tick later, archer 1 tick after that, then 3 ticks with no attacks before the berserker restarts the cycle.
   > The berserker attacks first, followed by the seer, then the archer, with a 1 tick interval between each attack. After the archer attacks, none will attack for three ticks before the berserker attacks again, repeating the cycle.

3. (central) If the player is moving on the tick a warband member is scheduled to attack, that member skips its attack entirely and does not attack until the next cycle 6 ticks later (player movement is a dodge mechanic against them).
   > If the player is moving when a warband member is scheduled to attack, the warband member will skip their attack and not attack until the next cycle in 6 ticks.

4. (central) Attacking a warband member with the combat style it is weak to guarantees both a successful hit and the player's maximum hit against it (accuracy roll and damage roll are both forced to max).
   > Attacking a warband member with the style they are weakest to will always result in a successful hit and will deal your maximum hit against them.

5. (central) The warband forms a rock-paper-scissors triangle: the archer attacks with Ranged and is weak to melee (Attack), the seer attacks with Magic and is weak to Ranged, the berserker attacks with melee and is weak to Magic.
   > Archer: Weakness [![Attack](/images/Attack_icon.png?b4bce)](/w/Attack) [![Ranged](/images/Ranged_icon.png?01b0e)](/w/Ranged) ... The archer uses ranged attacks and is weak to melee; the seer uses magic and is weak to ranged; the berserker uses melee and is weak to magic.

### https://www.youtube.com/watch?v=52aMbMJ5cZw

Title: Updated Colosseum Modifier Tier List 2.0 - YouTube  
Source quality (fetch agent's label): blog | publish date: 2024-04-13

1. (central) After the April 2024 rebalance, Relentless no longer guarantees enemy hits at all tiers; instead Relentless I makes enemies ignore 33% of the player's defence, Relentless II ignores 66%, and Relentless III ignores all defence (guaranteed hits only at tier III).
   > Relentless no longer automatically guarantees uh hits from enemies anymore now the way that Relentless works is that tier one makes it so that enemies ignore 33% of your defense tier 2 ignores 66 of your defense and then tier three ignores all of your defense so get guarantees that you get hit every single time

2. (central) Mantimayhem I does not add a fourth orb to the Manticore's charge-up despite its in-game description saying it 'shoots an additional orb'; it instead makes the Manticore fire two projectiles of each style, with each doubled pair landing on the same tick (double mage, double range, double melee).
   > it says for manam 1 that it shoots an additional orb ... what it actually does is that it actually shoots double of each attack now ... it'll start shooting double Mage double range double melee all on the same tick okay so like double Mage on the on the same tick double range on the same tick double melee on the same tick

3. (central) Mantimayhem II applies venom to the player whenever they miss a prayer flick against a Manticore attack, and a serpentine helm negates this effect entirely.
   > level two is not hard it just means that anytime you miss a flick you get venomed ... start bringing a Ser Helm it'll help a lot for man Mayhem you won't have to worry about the Venom it kind of just ends up making that tier two irrelevant

4. (central) Mantimayhem III randomizes the position of the melee orb in the Manticore's charge order, implying the baseline (unmodified) Manticore always fires its melee attack last in the three-orb sequence.
   > Man mham level three makes it so that melee can be placed anywhere in the order now it's not always going to be last anymore uh so it just Shuffle randomly shuffles up the order

5. (supporting) In the April 2024 modifier rework, Mantimayhem is a new modifier that replaced the Doom Scorpion modifier while keeping the same modifier icon, and Quartet's extra rangers were buffed from dealing 1 damage to meaningful damage comparable to the regular Fremennik archers.
   > Mana mayhem's the new one they replace Doom scorpion with man but they kept the symbol for the modifier the same ... quartet is a little bit more annoying now since the Rangers deal more damage they don't do one damage anymore they're kind of like the majors

### https://oldschool.runescape.wiki/w/Fortis_Colosseum

Title: Fortis Colosseum - OSRS Wiki  
Source quality (fetch agent's label): secondary | publish date: None

1. (central) If a wave is not completed within 40 seconds, enemy reinforcements arrive from the north or south gate, whichever the player is closest to.
   > If the player does not complete a wave within 40 seconds, additional enemy reinforcements will arrive from either the north or south gates, depending on which one they are closest to.

2. (central) Players must pick 1 of 3 offered modifiers before each next wave; the page lists 14 modifiers: Bees!, Blasphemy, Doom, Dynamic Duo, Frailty, Mantimayhem, Myopia, Reentry, Red Flag, Relentless, Solarflare, Quartet, Totemic, Volatility. Most have 3 levels, but the page does NOT spell out exact per-tier (I/II/III) numeric effects, so tier specifics remain UNKNOWN from this page.
   > Before progressing to the next wave, players must choose one of three modifiers that will make subsequent waves harder to clear.

3. (central) The Red Flag modifier changes Minotaur movement: it grants Minotaurs routefinding so they can path around obstacles (by implication they cannot routefind without it).
   > Minotaurs gain the ability to routefind, allowing it to move around obstacles.

4. (central) A Fremennik warband trio member is guaranteed to spawn in every wave except wave 12, uses smart pathing, and runs to the player's current position; each member has a style-specific guaranteed-max-hit gimmick (e.g. Berserker: 'Hitting with magic attacks will always result in a max hit').
   > Part of the Fremennik warband trio, guaranteed to spawn in every wave except for wave 12. Has smart pathing and runs to where the player currently is.

5. (central) The Javelin Colossus replaces every fifth attack with a javelin launched into the air (skyfall special), and this attack ignores protection prayers; warning time and damage numbers are not given on this page (UNKNOWN here).
   > Every fifth attack is replaced with a javelin launched into the air...this attack ignores prayer.

### https://oldschool.runescape.wiki/w/Sol_Heredit

Title: Sol Heredit - OSRS Wiki  
Source quality (fetch agent's label): secondary | publish date: 2026-05-14

1. (central) Sol Heredit transitions phases at 90%, 75%, 50%, 25% and 10% HP, and each phase opens with 6 light beams placed randomly in a 9x9 area around the player that spawn molten sand after 2 ticks, plus a crystal that rotates around the arena edges.
   > Sol Heredit transitions phases at 90%, 75%, 50%, 25% and 10% HP respectively. Each phase starts with 6 beams of light randomly placed in a 9x9 area around the player, which will spawn molten sand after 2 ticks. Each new phase will spawn a crystal which will rotate around the edges of the arena.

2. (central) The triple parry sequence lands its first hit 3 ticks after the animation starts, the second 3 ticks after the first, and the third 3 ticks after the second, with the third hit delayed to 4 ticks when Sol Heredit is under 50% HP.
   > 1st attack: 3 ticks after the start of his animation ... 2nd attack: 3 ticks after first ... 3rd attack: 3 ticks after the second (delayed to 4 ticks if under 50%)

3. (central) Sol Heredit's max hit table lists 44 for the typeless AOE, 44 for the grapple, and escalating triple-parry damage of 15-25-35 (Triple Parry 1) and 15-30-45 (Triple Parry 2); he has 1,500 hitpoints at combat level 1,563 with NPC ID 12821.
   > 44 (Typeless AOE) ... 44 (Grapple) ... 15-25-35 (Triple Parry 1) ... 15-30-45 (Triple Parry 2)

4. (central) For the grapple special, Sol Heredit drops his shield and calls out a body part, giving the player exactly 4 ticks to click the item in the corresponding equipment slot to parry the attack.
   > he will drop his shield and call out a body part, the player will have 4 ticks to click on the item in the respective slot to parry the attack.

5. (central) In the enrage phase at 10% HP, molten sand spawns every 3 game ticks on random tiles around the arena and lasers fire more frequently with less reaction time; the fight arena is the area between the four pillars bordered by shield-wielding NPCs, but the page gives no exact tile dimensions (UNKNOWN), no explicit between-specials auto-attack description (UNKNOWN), and no animation/projectile/graphics IDs (UNKNOWN).
   > molten sand will spawn every 3 game ticks on a random tile around the arena, lasers will fire much more often, and give less time to react.

### https://colosim.com/

Title: Sol Heredit Trainer (colosim) - browser tick simulator  
Source quality (fetch agent's label): secondary | publish date: None

1. (central) colosim's source (bundled at colosim.com/main.js, SolHeredit.ts) encodes the triple-attack parry sequence as three hits of 15/25/35 damage (short version) or 15/30/45 (long version), with the hit actions scheduled 2, 5, and 8 ticks (short) or 2, 5, and 9 ticks (long) after the attack starts; each hit is made unblockable if the player had an overhead protection prayer active during the preceding 3/2/2-or-3-tick window, and each hit deactivates Protect from Melee/Range/Magic.
   > DelayedAction(this.doParryAttack(15, 3).bind(this), 2) ... doParryAttack(short ? 25 : 30, 2).bind(this), 5) ... [if short] this.doParryAttack(35, 2).bind(this), 8) ... [else] this.doParryAttack(45, 3).bind(this), 9) ... overheadWasOn ? new ParryUnblockableWeapon() : new osrs_sdk_1.MeleeWeapon()

2. (central) colosim sets Sol Heredit's phase transitions at 1500/1350/1125/750/375/150 HP out of 1500 max (i.e. 100/90/75/50/25/10 percent), each with a dialogue line; each transition drops a sand pool under the player plus 4-5 more nearby, transitions 1-4 each spawn one laser orb (edges in order N/E/S/W, fired every 25-35 ticks, 12 in enrage), and in the final phase (150 HP) one pool spawns within +-4 tiles of the player every 3 ticks.
   > exports.PHASE_TRANSITION_POINTS = [[1500, "Let's start by testing your footwork."], [1350, "Not bad. Let's try something else..."], [1125, "Impressive. Let's see how you handle this..."], [750, "You can't win!"], [375, "Ralos guides my hand!"], [150, "LET'S END THIS!"]] ... if (toPhase >= 1 && toPhase <= 4) { this.createLaserOrb(); } ... this.finalPhasePoolTimer = 3

3. (central) colosim implements the grapple as targeting one random equipment slot of five (chest/back/gloves/legs/feet, announced via overhead text like "I'LL CRUSH YOUR BODY!"), parried by clicking that equipment slot; on failure it deals 20 + rand(0-24) = 20-44 unblockable damage resolving 4 ticks after the attack starts, 0 on success, with the boss's next attack 7 ticks later; grapple is only used below 75% HP (phaseId >= 2). The sim's own notes list "Grapple perfect parry" as not implemented yet.
   > new osrs_sdk_1.Projectile(new ParryUnblockableWeapon(), didParry ? 0 : 20 + Math.floor(osrs_sdk_1.Random.get() * 25), _this, _this.aggro, "stab", { hidden: true, setDelay: 0 }) ... }, 4)); return 7; // only used under 75%, so always at 7 ... <p>Not implemented yet:</p>\n<ul>\n <li>Grapple <span style=\"color: orange\">perfect parry</span></li>

4. (central) colosim's Sol Heredit attack cadence: the first attack is always spear; after a spear the next attack comes in 7 ticks during phases 0-1 and 6 ticks from phase 2 onward; after a shield 6 then 5 ticks; special attacks require a 2-normal-attack cooldown (SPECIAL_ATTACK_COOLDOWN = 2); triple-short unlocks at phase 1 (below 1350 HP), grapple at phase 2 (below 1125), and triple-long replaces triple-short at phase 3 (below 750); spear and shield are each double-weighted in the random attack pool.
   > _this.forceAttack = Attacks.SPEAR; // first attack is always a spear? ... return this.phaseId < 2 ? 7 : 6; ... return this.phaseId < 2 ? 6 : 5; ... var SPECIAL_ATTACK_COOLDOWN = 2; ... canSpecial && this.phaseId >= 3 && [Attacks.TRIPLE_LONG] ... this.phaseId >= 1 && this.phaseId < 3 && [Attacks.TRIPLE_SHORT] ... canSpecial && this.phaseId >= 2 && [Attacks.GRAPPLE]

5. (central) colosim models the shield slam as a filled 19x19 rectangle around the 5x5 boss (from x-7,y-12 to x+12,y+7) with a safe square ring at radius 4 on the first shield and radius 5 on the second; spear slams cover the tiles under/adjacent to the boss plus 7-tile-long lines toward the player (2 lines on first spear, 3 on second); every slam tile deals 20 + rand(0-24) = 20-44 damage only to a player standing exactly on that tile, landing 1 tick after the tile appears, and the author's comment flags the real min/max hit as uncertain. The sim covers only the Sol Heredit duel (no wave 1-11 NPCs), in a walled arena spanning x 19-34, y 18-33, boss combat level 1200.
   > doFirstShield = function () { this.fillRect(this.location.x - 7, this.location.y - 12, this.location.x + 12, this.location.y + 7, 4); } ... doSecondShield ... 5); ... // up to 45? not sure what min hit is this.damage = 20 + Math.floor(osrs_sdk_1.Random.get() * 25); ... ARENA_WEST: 19, ARENA_EAST: 34, ARENA_NORTH: 18, ARENA_SOUTH: 33

### https://oldschool.runescape.wiki/w/Fortis_Colosseum/Modifiers

Title: Fortis Colosseum/Modifiers - OSRS Wiki  
Source quality (fetch agent's label): secondary | publish date: 2026-04-01

1. (central) Red Flag is a single-tier modifier that changes Minotaur movement: Minotaurs gain advanced NPC pathing so they can move around obstacles and cannot be safespotted, and Red Flag is not offered as an option after wave 11.
   > Minotaurs now have advanced NPC pathing, allowing them to move around obstacles and making them impossible to safespot. Will not be given as an option after wave 11.

2. (central) Relentless tiers I/II/III make enemy attacks bypass 33%/66%/all of the player's Defence (tier III fully ignores accuracy checks) and raise enemy max hits by +1/+3/+6 respectively.
   > Enemy attacks will now bypass 33% of the player's Defence level, and have their max hit increased by 1. ... Enemies will fully ignore accuracy checks, and max hits are now increased by 6.

3. (central) Solarflare spawns a damaging orb circling the pillars with tick-exact movement: tier I moves every 2 ticks and stops 7 ticks at corners; tier II moves every 2 ticks without stopping and deals more damage; tier III moves every tick, stops 2 ticks at corners, and additionally disables prayers on hit.
   > A damaging orb circles around the pillars, moving every 2 ticks, then stopping for 7 ticks when it reaches a corner. ... The orb now moves every tick, stopping for 2 ticks when it reaches a corner. It will also now disable prayers if hit.

4. (central) Player-debuff modifier tier values: Doom kills the player at 15/10/5 stacks (one stack per damage instance taken, stacks cleared on wave completion); Blasphemy drains prayer points by 20%/40%/60% of damage taken; Frailty reduces base Hitpoints by 10%/20%/40% and disables overhealing; Myopia reduces the player's attack range by 2/4/6 tiles with manually cast spells unaffected.
   > A stack of Doom is gained whenever damage is taken, indicated by a hitsplat. The player is killed upon gaining 15 stacks. Stacks of doom are cleared after completing a wave.

5. (central) Spawn-altering modifiers: Quartet adds one extra random Fremennik Warbander every wave; Dynamic Duo makes Shockwave Colossi spawn in pairs, with the paired Colossus spawning near the main one but not necessarily on one of the arena's 12 default spawn points (confirming 12 default NPC spawn locations exist), and is not offered after wave 11; Mantimayhem I adds an extra projectile per Manticore orb (twice per attack cycle), II makes Manticores venomous if unprayed, III randomizes the magic/range/melee orb order. The page does not document draft/offer rules (which waves offer modifiers, pick-of-3, skipping, or tier upgrades) — UNKNOWN from this source.
   > Shockwave Colossi will now spawn in pairs. The paired Colossus spawns near the main Colossus, but not necessarily on one of the 12 default spawns. Will not be given as an option after wave 11.

### https://www.youtube.com/watch?v=-S1ucgeazKc

Title: OSRS Colosseum Guide - Modifier Choices - YouTube  
Source quality (fetch agent's label): blog | publish date: 2024-08-12

1. (central) Red Flag modifier changes Minotaur pathing: Minotaurs (which appear from wave 7 onward) become able to path around and through obstacles, making them impossible to safespot.
   > next we have red flag this makes the minotaurs from Level from wave seven and on be able to path around and through everything in its way making it impossible to be safe spotted

2. (central) Mantimayhem tiers I and II add extra projectiles to Manticore attacks, tier II additionally inflicts venom when a Manticore hit lands, and tier III changes the orb charge order so the melee orb's position in the sequence can vary.
   > the first two levels add extra projectiles to the mantore attacks and level two will also Venom you if you end up taking a hit but three switches the order in which the melee orbs can be placed

3. (central) Doom modifier adds one doom stack each time the player takes damage and instantly kills the player at a stack threshold regardless of remaining HP; the threshold is 15 stacks at tier I and 5 stacks at tier III.
   > this modifier gives you a stack of Doom every time you take damage and you'll be instantly killed when you hit a certain stack even if you have HP level one starts off at 15 and level three ends at level five

4. (central) Relentless modifier makes enemies bypass 33% of the player's defence at tier I, scaling to 100% bypass at tier III, and also increases enemies' max hits.
   > this makes enemies bypass a certain percentage of your defense starting at 33% with level one and you're increasing to 100% at level three and also increases the enemy's Max hits

5. (central) Myopia modifier reduces the player's attack range cumulatively per tier: 2 tiles reduced at tier I, totaling 6 tiles reduced at tier III.
   > this reduces your attack range every time you choose it it starts at two tiles and level three ends up being six tiles reduced

### https://oldschool.runescape.wiki/w/Fortis_Colosseum/Strategies

Title: Fortis Colosseum/Strategies - OSRS Wiki  
Source quality (fetch agent's label): secondary | publish date: 2026-05-29 (last wiki revision; page continuously edited since March 2024 release)

1. (central) Modifier draft rules: the wave 1 offer is always a selection of Relentless, Blasphemy and Frailty; Red Flag and Dynamic Duo only become available from wave 7 onward; all other offers are random with a bias toward upgrading previously picked Tier 1 and Tier 2 modifiers, and after wave 1 the player cannot move until a selection is made.
   > The first wave will always consist of a selection of Relentless, Blasphemy and Frailty. Red Flag and Dynamic Duo become available before wave 7 starts. The remaining modifiers are picked at random with a preference towards upgrades to previously picked Tier 1 and Tier 2 modifiers. [...] After the first wave is cleared, the player will not be able to move when Minimus appears until they make a selection.

2. (central) Reinforcements spawn exactly 40 seconds after a wave starts (the timing the RuneLite 'Fortis Colosseum' plugin timer is built around).
   > Fortis Colosseum: displays wave information, a handicap overlay, and a timer (used to anticipate reinforcements spawning, which occurs 40 seconds after the wave starts).

3. (central) Relentless per-tier effects: tier I makes enemy attacks bypass 33% of the player's Defence level and adds +1 max hit; tier II bypasses 66% and adds +3; tier III makes enemies fully ignore accuracy checks and adds +6 max hit.
   > Enemy attacks will now bypass 33% of the player's Defence level and have their max hit increased by 1. [...] Attacks now bypass 66% of the player's Defence, and max hits are now increased by 3. [...] Enemies will fully ignore accuracy checks, and max hits are now increased by 6.

4. (central) Solarflare per-tier tick timings: tier I orb moves every 2 ticks and stops for 7 ticks at corners; tier II moves every 2 ticks without stopping and deals more damage; tier III moves every tick, stops 2 ticks at corners, and additionally disables prayers on hit; during wave 12 (Sol Heredit) the orbs circle a set 5x5 pattern within the smaller arena.
   > A damaging orb circles around the pillars, moving every 2 ticks, then stopping for 7 ticks when it reaches a corner. [...] The orb now moves every tick, stopping for 2 ticks when it reaches a corner. It will also now disable prayers if hit [...] Solarflare orbs will continually rotate in a 5x5 pattern by the corners of the improvised arena.

5. (central) Complete Colosseum NPC ID list: 12810 Jaguar warrior, 12811 Serpent shaman, 12812 Minotaur, 12813 Minotaur (with Red Flag active, a distinct routefinding variant), 12814 Fremennik warband archer, 12815 Fremennik warband seer, 12816 Fremennik warband berserker, 12817 Javelin Colossus, 12818 Manticore, 12819 Shockwave Colossus, 12821 Sol Heredit, 12823 Bees, 12825 Healing totem, 12826 Solarflare.
   > 12810 Jaguar warrior; 12811 Serpent shaman; 12812 Minotaur; 12813 Minotaur (with Red Flag active); 12814 Fremennik warband archer; 12815 Fremennik warband seer; 12816 Fremennik warband berserker; 12817 Javelin Colossus; 12818 Manticore; 12819 Shockwave Colossus; 12821 Sol Heredit; 12823 Bees; 12825 Healing totem; 12826 Solarflare

### https://oldschool.runescape.wiki/w/User:Skairunner/Fortis

Title: User:Skairunner/Fortis - OSRS Wiki  
Source quality (fetch agent's label): blog | publish date: 2025-04-12

1. (central) The page lists full wave compositions 1-12 with per-wave reinforcement NPCs: wave 1 is Fremennik Warband + Serpent shaman with Jaguar warrior reinforcement, wave 7 is Fremennik Warband + Javelin Colossus + Manticore + Shockwave Colossus with Minotaur reinforcement, wave 12 is Sol Heredit alone; reinforcements shift from Jaguar warrior (waves 1-3) to Jaguar warrior + Serpent shaman (4-6) to Minotaur (7-9) to Minotaur + Serpent shaman (10-11). No reinforcement timer (seconds or ticks) and no spawn tile coordinates are given anywhere on the page.
   > Wave 1: Fremennik Warband, Serpent shaman | Reinforcements: Jaguar warrior ... Wave 7: Fremennik Warband, Javelin Colossus, Manticore, Shockwave Colossus | Reinforcements: Minotaur ... Wave 12: Sol Heredit

2. (central) Fremennik Warband NPCs run toward the player and can only attack while standing still adjacent to the player, or from up to 2 tiles away when frozen. The page gives no tiles-per-tick movement speed number.
   > They will run towards the player and only attack when standing still next to the player or when frozen up to 2 tiles away.

3. (central) Each Jaguar warrior attack lands three hits on the player, with accuracy and strength rolled independently for each hit.
   > Each attack lands three hits on the player, with their accuracy and strength rolled independently from each other.

4. (central) The Manticore's attack pattern is three attacks delivered in three consecutive ticks, preceded by a seven-tick charge-up; separately, the Minotaur heals other monsters to full health within a 6-tile radius.
   > Has a unique attack pattern of three attacks in three ticks with a seven tick charge-up time. ... Heals monsters to full health within a 6 tile radius.

5. (central) The Sol Heredit fight (wave 12) is confined to a 16x16 tile usable area with four extra tiles jutting out from the corners, and Sol Heredit enters an enrage phase at roughly 150 remaining hitpoints.
   > limiting the amount of space to a 16x16 area (with four tiles jutting out from the corners) ... When Sol Heredit reaches ~150 hitpoints, he will enter an enrage phase

### https://oldschool.runescape.wiki/w/Update:Varlamore_Tweaks_&_Drop_Rates

Title: Update: Varlamore Tweaks & Drop Rates (3 April 2024) - OSRS Wiki archive of official Jagex post  
Source quality (fetch agent's label): primary | publish date: 2024-04-03

1. (central) [verification spawned, all 3 voters died (no verdict)] As of the 3 April 2024 update, the Doom modifier has three tiers with death thresholds of 15, 10, and 5 Doom stacks respectively, stacks reset to 0 at the end of each wave, and the Soul Reaper Axe and Divine Potions no longer add Doom stacks.
   > There will now be three tiers of Doom. Each tier will allow you to take up to 15, 10 or 5 Stacks of Doom (respectively) before meeting your end. Doom will now reset back to 0 stacks at the end of each wave. We're removing the Soul Reaper Axe & Divine Potions from adding to stacks of Doom.

2. (central) [verification spawned, all 3 voters died (no verdict)] As of the 3 April 2024 update, the Relentless modifier's defence-ignoring effect is 33% at stack 1, 66% at stack 2, and 100% (all defences) at stack 3, while the additional damage per stack was left unchanged.
   > Stack 1 ignores 33% defence. Stack 2 ignores 66% defence. Stack 3 ignores all defences. Additional damage per stack is left unchanged.

3. (central) [verification spawned, all 3 voters died (no verdict)] The Mantimayhem modifier (introduced/described in the 3 April 2024 update) makes Manticore orb attacks deal more damage per tier chosen but does not change the number of prayer flicks required against the Manticore.
   > Manticores orb attacks become stronger, each time this is chosen. This makes missing your flicks slightly more punishing but wouldn't change how many times you need to flick.

4. (central) [verification spawned, all 3 voters died (no verdict)] As of the 3 April 2024 update, the Myopia modifier affects autocast spells (autocasting no longer bypasses the modifier), while manual casting remains unaffected by it.
   > Remove the ability to autocast spells without being affected. This means you'll still be able to manually cast with this Modifier.

5. (supporting) On 3 April 2024 Jagex buffed the Fremennik Ranged Warbanders' Ranged level and reduced the Warband Melee (berserker) NPC's max hit, meaning warband stat values recorded before this date are stale; exact post-change numbers are not given in the post (UNKNOWN here).
   > The Ranged Warbanders have been given a little buff, so their ranged level more accurately reflects the rest of their stats. [...] We've reduced the Warband Melee's maxhit to give you a little more of a fighting chance up against those fellas.

### https://oldschool.runescape.wiki/w/Money_making_guide/Completing_the_Fortis_Colosseum_(Wave_1)

Title: Money making guide/Completing the Fortis Colosseum (Wave 1) - OSRS Wiki  
Source quality (fetch agent's label): secondary | publish date: 2025-10-02 (page last modified)

1. (central) [verification spawned, all 3 voters died (no verdict)] Wave 1 of the Fortis Colosseum starts with 4 enemies spawned close enough together that a single AoE special targeted on the Serpent shaman hits all of them; the initial group includes a Serpent shaman and multiple Fremennik warband members (an archer among them).
   > attack the Serpent shaman to hit all 4 enemies at once

2. (central) [verification spawned, all 3 voters died (no verdict)] A Jaguar warrior spawns as a delayed reinforcement during wave 1, and killing the initial enemies quickly enough prevents it from spawning (no timer value is given on this page).
   > to prevent the Jaguar warrior from spawning

3. (supporting) The recommended overhead prayer for wave 1's initial spawns is Protect from Magic (with Piety), implying the dominant ranged-threat damage type at wave start is magic.
   > Pray Protect from magic and Piety before the wave starts

4. (supporting) A modifier ('handicap') choice is offered before wave 1 begins, and Blasphemy is one of the selectable options at that first draft.
   > Pick the Blasphemy handicap and start the wave

5. (tangential) Leaving the Colosseum after claiming rewards restores the player's health, prayer, and run energy.
   > You should not take any food or prayer restoration potions as leaving after claiming rewards restores your health, prayer, and run energy.

### https://oldschool.runescape.wiki/w/Update:Varlamore_Tweaks,_GameJam_V_Commences_&_More!

Title: Update: Varlamore Tweaks, GameJam V Commences & More! (24 April 2024) - OSRS Wiki archive of official Jagex post  
Source quality (fetch agent's label): primary | publish date: 2024-04-24

1. (central) [verification spawned, all 3 voters died (no verdict)] As of the 24 April 2024 update, the Mantimayhem modifier is no longer offered as a modifier option going into Wave 12 of the Fortis Colosseum, because it has no effect during the Sol Heredit fight. This also confirms modifier offers occur going into waves (including, previously, wave 12) and that Mantimayhem's effect is inert in the boss wave.
   > The 'Mantimayhem' modifier will no longer be offered as an option going into Wave 12, since it doesn't do anything during the Sol Heredit fight.

2. (supporting) Sol Heredit has a 'grab' attack that deals damage, and prior to the 24 April 2024 fix it could still damage players after Sol Heredit ran out of hitpoints. Post-fix, the grab deals no damage once his HP reaches 0.
   > Sol Heredit will no longer damage players with his 'grab' attack after he runs out of hitpoints.

3. (tangential) The Bees! modifier spawns bee entities inside the Colosseum arena that players must visually track. On 24 April 2024 Jagex made them more visible. This is a visual change only, with no stated mechanical effect.
   > Bees inside the Colosseum are now more visible and easier to keep track of.

4. (tangential) On 24 April 2024 the Colosseum combat achievement 'I Brought Mine Too' was expanded to count all spears and hastae as well as halberds, implying Sol Heredit is killable with those weapon classes. No combat-mechanic change is stated.
   > 'I Brought Mine Too' has been updated to include all spears and hastae, as well as halberds.

### https://github.com/LlemonDuck/fortis-colosseum

Title: GitHub - LlemonDuck/fortis-colosseum (RuneLite plugin source)  
Source quality (fetch agent's label): primary | publish date: 2024-03-20 (repo created); fetched main branch state, last push 2026-02-11

1. (central) [UPHELD 3/3 voters returned, 0 refutations] The complete modifier set is exactly 14 entries: Bees!, Blasphemy, Doom, Dynamic Duo, Frailty, Mantimayhem, Myopia, Reentry, Red Flag, Relentless, Solarflare, Quartet, Totemic, Volatility. Ten are tiered I/II/III (three sprite IDs plus a level varbit, e.g. Bees varbit 9791, Blasphemy 9790, Doom 10681, Frailty 9796, Mantimayhem 4588, Myopia 9795, Reentry 9792, Relentless 9798, Solarflare 9797, Volatility 9799) while Dynamic Duo, Red Flag, Quartet, and Totemic are untiered (single sprite, levelVarb -1). Each modifier has an internal bitmask ID (e.g. Mantimayhem 0, Reentry 1, Bees 2, Volatility 3, Blasphemy 4, Relentless 5, Quartet 6, Totemic 7, Doom 8, Dynamic Duo 9, Solarflare 10, Myopia 11, Frailty 12, Red Flag 13).
   > BEES("Bees!", new int[]{5544, 5559, 5574}, 2, 9791), BLASPHEMY("Blasphemy", new int[]{5538, 5553, 5568}, 4, 9790), DOOM("Doom", new int[]{5543, 5558, 5573}, 8, 10681), DYNAMIC_DUO("Dynamic Duo", new int[]{5545}, 9, -1),

2. (central) [UPHELD 3/3 voters returned, 0 refutations] Wave compositions 1-11 follow fixed rules: 3 Fremennik every wave (4 with Quartet); 1 Serpent Shaman as an initial spawn on waves 1-6; Javelin Colossus appears on waves 2 (x1) and 3 (x2), skips waves 1 and 4, then from wave 5 alternates 1 on odd waves and 2 on even waves; Manticore appears from wave 4, single on waves 4-8 and double on waves 9-11; Shockwave Colossus appears only on waves 7, 8, and 11 (doubled by Dynamic Duo). Wave 12 is Sol Heredit alone (plus 1 Fremennik if Quartet is active), and the Bees! modifier adds an Angry Bees spawn.
   > // frems every wave, 3 by default or 4 with quartet builder.spawn(new WaveSpawn(modifiers.contains(Modifier.QUARTET) ? 4 : 3, Enemy.FREMENNIK)); ... // javelins alternate 1 and 2 spawns, but skip waves 1 and 4 ... // single spawn on wave 4-8, double thereafter ... // shockwave waves 7, 8, and 11, and 2 spawns if dynamic duo is on

3. (central) [UPHELD 3/3 voters returned, 0 refutations] Reinforcement composition per wave: Jaguar Warrior is reinforcement-only and reinforces on every wave 1-6; from wave 7 onward the Minotaur replaces it as the reinforcement; the Serpent Shaman additionally arrives as a reinforcement on waves 4-6 and 10-11. (This repo encodes which NPCs reinforce per wave but contains no reinforcement timer value, so the exact tick delay remains UNKNOWN from this source.)
   > // and also as a reinforcement 4-6 and 10-11 ... // jaguar warrior is reinforcement only, all waves up to 6 ... // minotaur replaces jaguar warrior in replacements wave 7 and up

4. (central) [UPHELD 3/3 voters returned, 0 refutations] Hardcoded client IDs: the Colosseum lobby is map region 7316 and the arena is instanced map region 7216; the modifier-selection interface is initialized by clientscript 4931 whose args carry exactly 3 offered modifier options per wave (args 2-4) plus a bitmask of already-active modifiers (arg 8); varbit 9788 stores the 1-based index of the selected modifier (0 = none selected); the modifier-offering NPC Minimus has NPC ID 12808; wave 12 start is signalled by the game message 'Sol Heredit jumps down from his seat'.
   > private static final int REGION_LOBBY = 7316; private static final int REGION_COLOSSEUM = 7216; private static final int SCRIPT_MODIFIER_SELECT_INIT = 4931; private static final int VARBIT_MODIFIER_SELECTED = 9788;

5. (central) [UPHELD 3/3 voters returned, 0 refutations] The Manticore charges exactly three orbs, one each of magic, ranged, and melee, identified by spotanims VFX_MANTICORE_01_PROJECTILE_MAGIC_01 / _RANGED_01 / _MELEE_01; the plugin's LoS-export encoding treats an order whose third orb is melee as the canonical pattern (encodable by the first orb alone, i.e. only the magic/ranged order varies and melee fires last), and any other completed order is labelled an 'unknown variant'.
   > if (third == ManticoreOrbType.MELEE) { return String.valueOf(first.getCode()); } // unless we have all 3, just go with unknown variant

### https://oldschool.runescape.wiki/w/Manticore

Title: Manticore - OSRS Wiki  
Source quality (fetch agent's label): secondary | publish date: Living wiki page, no single publish date; documents updates through at least 2024-04-17

1. (central) [verification spawned, all 3 voters died (no verdict)] The Manticore's triple attack always ends with melee, and the first two hits are either ranged-then-magic or magic-then-ranged (the page does not state what determines which of the two orderings is used, nor whether the orbs are visible above its head).
   > Upon spotting a player in attack range, the Manticore will proceed to charge up a triple attack with all three combat styles. It will either use a range-magic or magic-range as the first two hits; the last hit is always melee.

2. (central) The three hits of the Manticore's attack sequence fire on consecutive game ticks (0.6 s apart) and have zero projectile travel time, i.e. each hit lands on the tick it is launched.
   > These attacks are launched one tick after each other (0.6 seconds) and land with a projectile travel time of 0.

3. (central) The Manticore's attack cycle is 10 ticks (listed attack speed '10 ticks (6.0 seconds)', described as a 'full 10-tick charge-up'), and when one manticore attacks, any other manticore that has finished charging has its own attack delayed by 5 ticks — a built-in stagger/desync rule for multi-manticore waves.
   > When a manticore attacks, any other manticore that is ready to attack (i.e., finished its full 10-tick charge-up) will have its attack delayed by 5 ticks.

4. (central) Manticores first appear on wave 4 of the Fortis Colosseum and spawn in pairs from wave 9 onward; a paired manticore copies the other's attack pattern if it is within 15 tiles with line of sight when the first selects its pattern.
   > Starting from wave 9, manticores will begin spawning in pairs. If one manticore selects an attack pattern and the other is within 15 tiles of it with line of sight, the other manticore will copy its attack pattern.

5. (supporting) Manticore combat profile: monster ID 12818, combat level 320, 250 hitpoints, max hits of 31 melee / 36 ranged / 31 magic; the 'Mantimayhem' handicap buffing manticores was added on 10 April 2024 (post-release balancing). Animation/projectile/spotanim IDs, movement-while-charging behavior, and prayer interaction are NOT on this page (UNKNOWN from this source).
   > Max hit: 31 (Melee), 36 (Ranged), 31 (Magic) ... A handicap, 'Mantimayhem', has been added that gives manticores buffs when chosen.

---

## SOURCE URLS

All URLs surfaced by the 6 search agents, deduped. FETCHED = a fetch agent extracted claims from it (see above). NOT FETCHED = follow-up target.

- [FETCHED] https://oldschool.runescape.wiki/w/Update:Undead_Pirates,_Colosseum_Changes_&_more!
  Update: Undead Pirates, Colosseum Changes & more! (10 April 2024, revised 12 April) - OSRS Wiki archive of official Jagex post (relevance high; surfaced by: Jagex patch notes: March-June 2024 balancing)
- [FETCHED] https://oldschool.runescape.wiki/w/Update:Varlamore_Tweaks_&_Drop_Rates
  Update: Varlamore Tweaks & Drop Rates (3 April 2024) - OSRS Wiki archive of official Jagex post (relevance high; surfaced by: Jagex patch notes: March-June 2024 balancing)
- [FETCHED] https://oldschool.runescape.wiki/w/Update:Varlamore_Tweaks,_GameJam_V_Commences_&_More!
  Update: Varlamore Tweaks, GameJam V Commences & More! (24 April 2024) - OSRS Wiki archive of official Jagex post (relevance high; surfaced by: Jagex patch notes: March-June 2024 balancing)
- [NOT FETCHED] https://oldschool.runescape.wiki/w/Update:Further_Project_Rebalance_(Skilling)_&_Varlamore_Changes
  Update: Further Project Rebalance (Skilling) & Varlamore Changes (15 May 2024) - OSRS Wiki archive of official Jagex post (relevance medium; surfaced by: Jagex patch notes: March-June 2024 balancing)
- [NOT FETCHED] https://secure.runescape.com/m=news/undead-pirates-colosseum-changes--more?oldschool=true
  Undead Pirates, Colosseum Changes & more! - official Jagex news post (primary source) (relevance medium; surfaced by: Jagex patch notes: March-June 2024 balancing)
- [NOT FETCHED] https://oldschool.runescape.wiki/w/Update:Easter_&_Varlamore_Updates
  Update: Easter & Varlamore Updates - OSRS Wiki archive of official Jagex post (Colosseum launch window) (relevance medium; surfaced by: Jagex patch notes: March-June 2024 balancing)
- [FETCHED] https://oldschool.runescape.wiki/w/Fortis_Colosseum/Strategies
  Fortis Colosseum/Strategies - OSRS Wiki (relevance high; surfaced by: Per-NPC attack mechanics; Sol Heredit tick-precise boss kit; Modifiers: full list and tier effects; Wiki canonical: waves, spawns, reinforcements)
- [FETCHED] https://oldschool.runescape.wiki/w/Manticore
  Manticore - OSRS Wiki (relevance high; surfaced by: Per-NPC attack mechanics)
- [FETCHED] https://oldschool.runescape.wiki/w/Fremennik_warband
  Fremennik warband - OSRS Wiki (relevance high; surfaced by: Per-NPC attack mechanics)
- [FETCHED] https://oldschool.runescape.wiki/w/Javelin_Colossus
  Javelin Colossus - OSRS Wiki (relevance high; surfaced by: Per-NPC attack mechanics)
- [FETCHED] https://oldschool.runescape.wiki/w/Minotaur_(Fortis_Colosseum)
  Minotaur (Fortis Colosseum) - OSRS Wiki (relevance high; surfaced by: Per-NPC attack mechanics; Wiki canonical: waves, spawns, reinforcements)
- [FETCHED] https://oldschool.runescape.wiki/w/Fortis_Colosseum
  Fortis Colosseum - OSRS Wiki (relevance high; surfaced by: Per-NPC attack mechanics; Modifiers: full list and tier effects; Wiki canonical: waves, spawns, reinforcements)
- [FETCHED] https://oldschool.runescape.wiki/w/Sol_Heredit
  Sol Heredit - OSRS Wiki (relevance high; surfaced by: Sol Heredit tick-precise boss kit)
- [FETCHED] https://colosim.com/
  Sol Heredit Trainer (colosim) - browser tick simulator (relevance high; surfaced by: Sol Heredit tick-precise boss kit)
- [FETCHED] https://github.com/LlemonDuck/fortis-colosseum
  LlemonDuck/fortis-colosseum - RuneLite plugin source on GitHub (relevance high/medium; surfaced by: Sol Heredit tick-precise boss kit; RuneLite plugin source code (IDs, hardcoded ticks); Modifiers: full list and tier effects)
- [NOT FETCHED] https://detuks.com/blog/osrs-colosseum-simulator
  OSRS Colosseum Simulator | Detuks Plugins (relevance medium; surfaced by: Sol Heredit tick-precise boss kit)
- [NOT FETCHED] https://www.youtube.com/watch?v=DUfBewpVk1s
  [OSRS] Sol Heredit MADE EASY - L Method - Get Your First Quiver! - YouTube (relevance low; surfaced by: Sol Heredit tick-precise boss kit)
- [FETCHED] https://github.com/willediger/Colosseum-Waves
  willediger/Colosseum-Waves — spawn/reinforcement tile capture plugin with ManticoreHandler (relevance high; surfaced by: RuneLite plugin source code (IDs, hardcoded ticks))
- [FETCHED] https://github.com/runelite/runelite/blob/master/runelite-api/src/main/java/net/runelite/api/gameval/SpotanimID.java
  runelite/runelite gameval SpotanimID.java (+ sibling NpcID.java, AnimationID.java) (relevance high; surfaced by: RuneLite plugin source code (IDs, hardcoded ticks))
- [NOT FETCHED] https://github.com/bopsec/bop-plugins
  bopsec/bop-plugins — 'Inferno & Colo Additions' plugin-hub plugin (HP tracking) (relevance medium; surfaced by: RuneLite plugin source code (IDs, hardcoded ticks))
- [NOT FETCHED] https://los.colosim.com/
  Colosseum LOS (los.colosim.com) — tile-precise line-of-sight/spawn simulator (relevance medium; surfaced by: RuneLite plugin source code (IDs, hardcoded ticks))
- [NOT FETCHED] https://github.com/runelite/plugin-hub
  runelite/plugin-hub — plugin registry pinning Colosseum plugin sources (relevance low; surfaced by: RuneLite plugin source code (IDs, hardcoded ticks))
- [FETCHED] https://oldschool.runescape.wiki/w/Fortis_Colosseum/Modifiers
  Fortis Colosseum/Modifiers - OSRS Wiki (relevance high; surfaced by: Modifiers: full list and tier effects)
- [FETCHED] https://www.youtube.com/watch?v=52aMbMJ5cZw
  Updated Colosseum Modifier Tier List 2.0 - YouTube (relevance medium; surfaced by: Modifiers: full list and tier effects)
- [FETCHED] https://www.youtube.com/watch?v=-S1ucgeazKc
  OSRS Colosseum Guide - Modifier Choices - YouTube (relevance medium; surfaced by: Modifiers: full list and tier effects)
- [FETCHED] https://oldschool.runescape.wiki/w/Money_making_guide/Completing_the_Fortis_Colosseum_(Wave_1)
  Money making guide/Completing the Fortis Colosseum (Wave 1) - OSRS Wiki (relevance medium; surfaced by: Wiki canonical: waves, spawns, reinforcements)
- [FETCHED] https://oldschool.runescape.wiki/w/User:Skairunner/Fortis
  User:Skairunner/Fortis - OSRS Wiki (relevance medium; surfaced by: Wiki canonical: waves, spawns, reinforcements)
- [FETCHED] https://www.youtube.com/watch?v=dxqrQQNl2_4
  OSRS Fortis Colosseum Quick Guide - Resources & Plugins (with wave solves) (relevance low; surfaced by: Wiki canonical: waves, spawns, reinforcements)

Additional URLs surfaced inside search snippets and verifier evidence (never fetched by this workflow, candidate follow-up targets):

- https://github.com/blert-io/plugin
  ColosseumChallenge.java / WaveDataTracker.java, verifiers used it to corroborate region 7216/7316, script 4931, varbit 9788, Minimus 12808, boss-start message (repo pushed 2026-06-08)
- https://github.com/Marco648135/cTimers
  ColosseumHandler.java, second independent corroboration of script 4931 / args[2..4] / NPC 12808 / varbit 9788
- https://github.com/detuks-client/InfernoColoSimulator
  reported source repo of colosim.com (colosseum branch) per search snippet
- https://i.imgur.com/G21mpEz.png
  community Colosseum spawn map by 'B5' (from YouTube guide description)
- https://www.reddit.com/r/2007scape/comments/1bka2f6/colosseum_spawn_mechanics_spawn_fixing_method_by/
  spawn-fixing method by Wotury & B5 (from YouTube guide description)
- https://i.imgur.com/8ict0rl.png
  Fremennik Warband & Tick Cycle Writeup image (from YouTube guide description)
- https://i.imgur.com/vWd5XjY.png
  visual metronome config for the tick-cycle writeup (from YouTube guide description)
- https://pastebin.com/jQ0a4uBX
  Watchdog config export for Sol Heredit grapple chatbox popup (from YouTube guide description)
- https://oldschool.runescape.wiki/w/Minimus
  wiki page verifiers used: Minimus NPC IDs 12807 and 12808

---
## COVERAGE NOTES

Factual map of which research domains got claims out of this salvage, and which look thin or uncovered.

**WAVES & SPAWNS: strong on composition, empty on tiles.**
Wave 1-11 composition rules and wave-12 Sol solo both VERIFIED 3/3 against plugin code plus wiki Strategies. Reinforcement composition per wave VERIFIED 3/3 (Jaguar 1-6, Minotaur 7-11, Serpent shaman extra on 4-6 and 10-11). Reinforcement timer: two unverified wiki claims say 40 seconds after wave start, from north or south gate whichever is closer to the player. No tick-precise reinforcement value recovered, and verifiers confirmed the plugin repos contain no timer constant. Exact spawn tile coordinates: ZERO claims recovered. The tile ground-truth sources surfaced but were never fetched: los.colosim.com, willediger/Colosseum-Waves NpcSpawn.java (captures exact spawn tiles), the B5 spawn map (imgur G21mpEz), and the Wotury/B5 spawn-fixing reddit thread. Wave-start sequencing: only the wave-12 trigger message and the cannot-move-until-modifier-selected rule recovered.

**FREMENNIK WARBAND: behavior covered, movement speed number missing.**
Unverified wiki claims cover the 6-tick attack cycle (berserker, then seer +1 tick, then archer +1 tick, then 3 silent ticks), the attack-skip when the player moves on a member's attack tick, the weakness triangle, and guaranteed-hit-plus-guaranteed-max-hit when attacking with the weakness style. Spawn every wave except 12, smart pathing, runs to player, attacks only while standing still adjacent (or up to 2 tiles when frozen). The reported faster-than-normal movement speed in tiles per tick: NOT recovered, no claim carries a number. 3 April 2024 rebalance (archer ranged-level buff, berserker max-hit nerf) recovered with post-change numbers UNKNOWN. The community tick-cycle writeup images were linked but never fetched.

**NPC MECHANICS: Manticore and Minotaur strong, Serpent shaman and Shockwave Colossus thin.**
Manticore: best-covered NPC. VERIFIED: spawns uncharged, exactly 3 orbs, melee-last canonical order with only magic/ranged lead varying. Unverified wiki: 10-tick attack cycle, 3 hits on consecutive ticks with 0 projectile travel time, 5-tick stagger when two manticores are ready, pattern-copy within 15 tiles with LoS from wave 9, max hits 31/36/31. Minotaur: heal decision rule (melee if in range, else scan, heal NPC below 75% HP within 7 tiles, to full), 6-vs-7-tile range conflict flagged on the wiki page itself, max hit 74, 5-tick speed, damage lands 1 tick after the attack animation (tick-eatable like Vardorvis), all unverified. Red Flag routefind variant VERIFIED. Javelin Colossus: every-5th-attack skyfall landing on launch-tick player tile and ignoring Protect from Missiles, 5-tick speed, 15-tile range, max hit 48 (49/51/54 under Relentless I/II/III), all unverified. Jaguar warrior: 3 hits per attack with independent accuracy/strength rolls (unverified, plus a search-snippet-only max 47 per hit, 141 total). Serpent shaman: THIN. No dedicated fetch. Only search snippets (10-tile range, can be offticked via line of sight) and its wave/reinforcement roles. Attack style, projectile, area denial: uncovered. Shockwave Colossus: THIN. No dedicated fetch. Internally a 'clap attack' (spotanim 2679, verified inside the ID claim) plus one search snippet calling it a long-range 15-tile magic attacker with no complex specials. AoE shape, timing, dodgeability: uncovered.

**MODIFIERS: list and varbits verified, most per-tier numbers recovered unverified, Volatility and Reentry thin.**
The complete 14-modifier set with tiered/untiered split, level varbits, and bitmask IDs: VERIFIED 3/3. Per-tier numbers recovered (unverified): Relentless 33/66/all defence bypass with +1/+3/+6 max hit, Doom 15/10/5 stacks with per-wave reset and Colosseum-only sources, Blasphemy 20/40/60 percent prayer drain, Frailty 10/20/40 percent HP with no overheal, Myopia minus 2/4/6 tiles attack range (autocast affected, manual cast exempt), Solarflare per-tier orb tick patterns, Mantimayhem double projectiles / venom (serpentine helm negates, per video) / melee-orb order shuffle, Quartet +1 Fremennik, Dynamic Duo paired Shockwave (its wiki text also confirms 12 default spawn points exist), post-10-April Totemic (30 percent heal, 2-minute respawn) and Bees (12-tick moves, 50-tick respawn) numbers. Draft rules recovered (unverified): pick 1 of 3 before each wave, wave-1 offer always Relentless/Blasphemy/Frailty, bias toward upgrading previously picked tiers (wiki Strategies plus an independent YouTube claim), Red Flag and Dynamic Duo enter the pool before wave 7 and are not offered after wave 11, Mantimayhem not offered into wave 12 (24 April 2024 change). Thin: Volatility and Reentry per-tier effects exist only as search snippets (death explosion radius/molten sand, permanent damaging sand from javelin specials). Whether a modifier offer can be skipped: uncovered.

**SOL HEREDIT: dense but almost entirely unverified, with one verified ID claim.**
The animation-ID kit (10875-10888, telegraphs, two triple-attack variants) is the only VERIFIED Sol claim (1/3 voters returned). Everything else is unverified single-source: wiki phase thresholds 90/75/50/25/10 percent and colosim HP constants 1500/1350/1125/750/375/150, triple parry timing (wiki 3/3/3-or-4 ticks vs colosim scheduled 2/5/8 or 2/5/9 with 15/25/35 and 15/30/45 damage and prayer-punish unblockable rules), grapple (4-tick window, 5 equipment slots, colosim 20-44 damage on fail, below-75-percent gate, next attack 7 ticks later), attack cadence (first attack always spear, spear-to-next 7 then 6 ticks by phase, shield 6 then 5, specials need 2 normal attacks between, spear and shield double-weighted), shield slam 19x19 with safe ring radius 4 then 5, spear line slams, slam tiles 20-44 damage landing 1 tick after appearing, 6 beams in 9x9 spawning sand after 2 ticks, lasers every 25-35 ticks (12 in enrage), enrage sand every 3 ticks, arena 16x16 usable area (Skairunner) vs colosim walls x 19-34 / y 18-33. colosim flags its own gaps: grapple perfect parry unimplemented, slam min/max hit uncertain in an author comment. The wiki page itself marks arena tile dimensions, between-specials auto attacks, and all IDs as UNKNOWN. The wiki/colosim parry-tick discrepancy is unreconciled in the salvage.

**IDs: strongest domain, mostly verified.**
NPC IDs 12808-12827 including modifier entities (Doom Scorpion 12822, Bee Swarm 12823, beam crystal 12824, Healing totem 12825, solar flare 12826): VERIFIED. Spotanim map 2667-2712 (Sol melee 2669-2672, javelin 2673-2678, shockwave clap 2679, manticore 2681-2686, totem 2687/2688, crystal beams 2689-2697, sunfire lightning 2698, stab dust 2699-2706, bees 2707/2708, hot sand 2709/2710, doom scorpion 2711/2712): VERIFIED. Manticore animations 10868/10869 and Sol kit 10875-10888: VERIFIED. Regions 7216/7316, clientscript 4931, selection varbit 9788, per-modifier level varbits: VERIFIED. Projectile IDs as distinct from spotanim IDs: not separately recovered.

**Patch timeline recovered:** 3 April 2024 (first rebalance, includes a Monster Examine fix implying launch-week attack-style data was wrong), 10 April 2024 (the big modifier rework), 24 April 2024 (Mantimayhem wave-12 removal, Sol grab-after-death fix). The 15 May 2024 post (arena tile walkability fix, relevant to a tile-precise simulator) and the launch-window Easter post surfaced in search but were never fetched.
