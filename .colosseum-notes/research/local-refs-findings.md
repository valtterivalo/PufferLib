# Fortis Colosseum: local reference repo findings

Mined 2026-06-10. Repos under `/Users/valtterivalo/Projects/pufferlib-metal/refs/` (NOT the worktree `.refs/`, which does not exist) plus the live modern cache at `/Users/valtterivalo/Projects/pufferlib-metal/.refs/osrs-cache-modern`. Facts only; every entry cites its source. Binary-cache values were decoded read-only with `refs/RuneC/tools/cache_pipeline/modern_cache_reader.py` + `rc_cache/definitions.py` (NpcDef/SpotanimDef/struct decoders), scripts run from /tmp.

Repo verdict up front:

| repo | colosseum content |
|---|---|
| osrs-client-deob | YES: gameval ID constants (NPC/anim/spotanim/varbit/varp/object/interface), region ids, 2 plugin touchpoints |
| osrs-cache-modern (live cache) | YES: full NpcType defs incl params, spotanim defs, modifier structs, CA structs |
| osrs-cache-openrs2-b238 | YES (binary only): same dat2 format, contains colosseum NPCs (12821 decodes to Sol Heredit lvl 1563 size 5). No extracted text files anywhere in the repo |
| osrs-dps-calc | YES: 13 colosseum monster stat blocks in `cdn/json/monsters.json`. NO colosseum-specific calc logic (verified, see section 5) |
| RuneC | YES: spawn/anchor coordinates (activity_spawns.bin), wiki infobox cache (full monster infoboxes), curated encounter spec, reports |
| rsmod-data | NO: single file `obj-enricher.toml` (item/obj data only; colosseum hits are reward items `tonalztics_of_ralos_uncharged/charged` at lines 16802/16817) |
| osrs-sdk | NO content: it is OldSchoolSDK, the engine behind colosim.com (Sol Heredit Trainer per README), but the only colosseum artifact is `src/sdk/EntityName.ts:20` `SOL_HEREDIT = "Sol Heredit"`. Colosim game content is not in this checkout |
| InfernoTrainer | nothing colosseum (content/inferno only) |
| rs-map-viewer | nothing colosseum (no ts/tsx/json matches) |
| autoresearch | nothing colosseum |

---

## 1. NPC DEFINITIONS

### 1.1 Gameval NPC ids (Jagex internal names)

Source: `refs/osrs-client-deob/runelite-api/src/main/java/net/runelite/api/gameval/NpcID.java` (lines 55862-55964, 57265-58548, 67633). Verbatim names:

```
COLOSSEUM_MASTER_OUTSIDE = 12807        COLOSSEUM_MASTER = 12808
COLOSSEUM_GLORY = 12809                 COLOSSEUM_JAGUAR_WARRIOR = 12810
COLOSSEUM_STANDARD_MAGER = 12811        COLOSSEUM_MINOTAUR = 12812
COLOSSEUM_MINOTAUR_ROUTEFIND = 12813    COLOSSEUM_WARBANDER_RANGED_FEMALE = 12814
COLOSSEUM_WARBANDER_MAGE_MALE = 12815   COLOSSEUM_WARBANDER_MELEE_MALE = 12816
COLOSSEUM_JAVELIN_COLOSSUS = 12817      COLOSSEUM_MANTICORE = 12818
COLOSSEUM_SHOCKWAVE_COLOSSUS = 12819    COLOSSEUM_SAFESPOT_DYING = 12820
COLOSSEUM_SOL_P1 = 12821                COLOSSEUM_DOOM_SCORPION = 12822
COLOSSEUM_MODIFIER_BEES = 12823         COLOSSEUM_BEAM_CRYSTAL = 12824
COLOSSEUM_HEALING_TOTEM = 12825         COLOSSEUM_SOLAR_FLARE = 12826
COLOSSEUM_BOSS_SEATED = 12827           COLOSSEUM_HUMAN_GIB = 12828
COLOSSEUM_MANTICORE_GIB = 12829         COLOSSEUM_MINOTAUR_GIB = 12830
COLOSSEUM_COLOSSI_GIB = 12831           COLOSSEUM_SOL_GIB = 12832
COLOSSEUM_PASSIONATE_SUPPORTER = 12833  COLOSSEUM_DUELIST_FLAVOUR = 12843
QUETZAL_COLOSSEUM = 13362               COLOSSEUM_TEOKI_RALOS = 13420
COLOSSEUM_TEOKI_RANUL = 13421           DEADMAN_BREACH_SOL_HEREDIT = 15554
COLOSSEUM_LOBBY_GUARD_STATIC_M_1..F_2 = 13110..13113
```

Notable: 12813 is named `COLOSSEUM_MINOTAUR_ROUTEFIND` (the "Red Flag" minotaur variant); 12820 `COLOSSEUM_SAFESPOT_DYING` is the in-game "Pillar" NPC; 12821 is `_SOL_P1` (no P2+ NPC ids exist in the file).

Community names cross-ref: `runelite-api/src/main/java/net/runelite/api/NpcID.java` lines 10748-10764 (`MINIMUS_12808`, `GLORIA`, `JAGUAR_WARRIOR`, `SERPENT_SHAMAN`, `MINOTAUR_12812/12813`, `FREMENNIK_WARBAND_ARCHER/SEER/BERSERKER`, `JAVELIN_COLOSSUS`, `MANTICORE`, `SHOCKWAVE_COLOSSUS`, `PILLAR = 12820`, `SOL_HEREDIT = 12821`, `DOOM_SCORPION`, `BEE_SWARM = 12823`, `HEALING_TOTEM = 12825`).

### 1.2 Full NpcType definitions from the modern cache (ground truth)

Source: `/Users/valtterivalo/Projects/pufferlib-metal/.refs/osrs-cache-modern`, index 2 group 9, decoded with `refs/RuneC/tools/cache_pipeline/rc_cache/definitions.py:decode_npc_definition` (all decodes `complete=True`, params captured via `_read_params`). stats = opcodes 74-79 in order [attack, defence, strength, hitpoints, ranged, magic] (verified identical to dps-calc skills for all 11 combat NPCs).

| id | name | size | cmb lvl | stats A/D/S/HP/R/M | stand | walk | run | rot180 | scale w/h | models |
|---|---|---|---|---|---|---|---|---|---|---|
| 12807 | Minimus | 1 | 0 | - | 11092 | 819 | -1 | -1 | 128/128 | [50749] |
| 12808 | Minimus | 1 | 0 | - | 11092 | 819 | -1 | -1 | 128/128 | [50749] |
| 12809 | Gloria | 1 | 0 | - | 808 | 819 | -1 | 820 | 128/128 | [382,51114,51065,353,51136,437] |
| 12810 | Jaguar warrior | 2 | 234 | 200/125/330/125/160/100 | 808 | 819 | -1 | 820 | 160/170 | [50748,53213,53214] |
| 12811 | Serpent shaman | 1 | 161 | 100/90/90/125/160/220 | 813 | 1205 | -1 | 1206 | 128/128 | [50747,53212,53213,53214] |
| 12812 | Minotaur | 3 | 318 | 300/190/360/225/120/250 | 10840 | 10842 | -1 | -1 | 128/128 | [52497] |
| 12813 | Minotaur (routefind) | 3 | 318 | 300/190/360/225/120/250 | 10840 | 10842 | -1 | -1 | 128/128 | [52497] |
| 12814 | Fremennik warband archer | 1 | 104 | 110/80/110/50/110/110 | 808 | 819 | -1 | 820 | 128/128 | [50931,53213,53214] |
| 12815 | Fremennik warband seer | 1 | 104 | 110/80/110/50/110/110 | 813 | 1205 | -1 | 1206 | 128/128 | [50725,53213,53214] |
| 12816 | Fremennik warband berserker | 1 | 103 | 110/80/110/48/110/110 | 808 | 819 | -1 | 820 | 128/128 | [50720,53213,53214] |
| 12817 | Javelin Colossus | 3 | 278 | 200/190/300/220/360/225 | 10889 | 10879 | -1 | -1 | 150/150 | [52579,53209] |
| 12818 | Manticore | 3 | 320 | 300/250/300/250/350/300 | 10863 | 10864 | -1 | 10865 | 44/44 | [52612] |
| 12819 | Shockwave Colossus | 3 | 239 | 120/150/190/125/220/350 | 10902 | 10881 | -1 | -1 | 128/128 | [52584,53209] |
| 12820 | `<col=00ffff>Pillar</col>` | 3 | 0 | - | -1 | -1 | -1 | -1 | 128/128 | [33046] |
| 12821 | Sol Heredit | 5 | 1563 | 350/200/400/1500/350/300 | 10874 | 10878 | -1 | -1 | 300/300 | [52580,52582,52578,52585] |
| 12822 | Doom Scorpion | 1 | 0 | - | 6252 | 6253 | -1 | -1 | 40/40 | [52511] (6 recolors) |
| 12823 | Bee Swarm | 2 | 0 | - | 10822 | 10822 | -1 | -1 | 128/128 | [53132] |
| 12824 | (unnamed, beam crystal) | 1 | 0 | - | 10799 | 10799 | -1 | -1 | 128/128 | [51243] |
| 12825 | Healing totem | 1 | 0 | - | 10827 | 10827 | -1 | -1 | 96/96 | [51251] |
| 12826 | (unnamed, solar flare) | 1 | 0 | - | 10817 | 10817 | -1 | -1 | 64/64 | [51242] |
| 12827 | Sol Heredit (seated) | 4 | 0 | - | 10875 | 10875 | -1 | -1 | 300/300 | [52580,52583] |
| 12828-12832 | (unnamed gib NPCs) | 1/3/3/3/5 | 0 | - | -1 | -1 | -1 | -1 | misc | [51247]/[51233]/[51250]/[51241]/[51241] |
| 12833 | Passionate Supporter | 1 | 7 | - | 808 | 819 | 824 | 820 | 120/120 | 7 models, 9 recolors |

Actions: every combat NPC has only `Attack` at op slot 31 (actions[1]). 12808 Minimus has `Start-wave`, `Leave`. 12807/12809 have `Talk-to`. None of 12807-12833 has varbit/varp transforms (`varbit=-1, varp=-1, transforms=[]`).

### 1.3 NPC params (opcode 249) from the same cache decode

12 params on each combat NPC. Empirical key mapping, verified exact against dps-calc `monsters.json` values for all 11 combat NPCs (see 1.4): key 3 = magic attack bonus, 4 = ranged attack bonus, 5/6/7 = defence stab/slash/crush, 8 = magic defence, 10 = melee strength bonus, 12 = ranged strength bonus, 65 = magic damage bonus (%), 14 and 26 see below, 46 = -1 everywhere (12833: 28958).

| id | 26 | 14 | 10 | 3 | 65 | 4 | 12 | 5 | 6 | 7 | 8 |
|---|---|---|---|---|---|---|---|---|---|---|---|
| 12810 Jaguar warrior | 1 | 5 | 25 | 0 | 0 | 0 | 0 | 30 | 30 | 30 | 15 |
| 12811 Serpent shaman | 5 | 5 | 0 | 50 | 15 | 0 | 0 | 30 | 30 | 30 | 15 |
| 12812 Minotaur | 2 | 5 | 64 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 |
| 12813 Minotaur (routefind) | 4 | 5 | 64 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 |
| 12814 Warband archer | 4 | 5 | 0 | 0 | 0 | 150 | 10 | 0 | 0 | 0 | 75 |
| 12815 Warband seer | 5 | 5 | 0 | 150 | 0 | 0 | 0 | 50 | 50 | 50 | 30 |
| 12816 Warband berserker | 0 | 5 | 90 | 0 | 0 | 0 | 0 | 50 | 50 | 50 | 0 |
| 12817 Javelin Colossus | 4 | 5 | 0 | 0 | 0 | 25 | 20 | 15 | 15 | 15 | 20 |
| 12818 Manticore | 0 | 10 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 10 |
| 12819 Shockwave Colossus | 5 | 5 | 0 | 55 | 35 | 0 | 0 | 15 | 15 | 15 | 5 |
| 12821 Sol Heredit | (absent) | (absent) | 5 | 0 | 0 | 150 | 5 | 65 | 5 | 30 | 750 |

- Param 14 matches dps-calc `speed` for jaguar/shaman/minotaur/javelin/manticore/shockwave (5,5,5,5,10,5). DIVERGENCE: warband archer/seer/berserker have param 14 = 5 in cache but `speed: 6` in dps-calc and `attack_speed: 6` in the wiki infobox. Sol has no param 14; dps-calc/wiki list speed 0.
- Param 26 values: 0 (berserker/manticore), 1 (jaguar), 2 (minotaur), 4 (routefind minotaur/archer/javelin), 5 (shaman/seer/shockwave). Correlates with dps-calc style Stab=0?/Slash=1/Crush=2/Ranged=4/Magic=5 except manticore (0, dps-calc style Melee+Ranged+Magic) and 12813 (4, dps-calc style Crush). Recorded as-is, not interpreted.
- Sol extra params: 509 = 2, 510 = 'Sol Heredit'.
- Doom Scorpion params: {14: 6, 46: -1}. Bee Swarm/Healing totem/12824/12826/12827: only {46: -1} or {} beyond defaults.
- No param carries ranged defence (light/standard/heavy); those exist only in dps-calc/wiki (e.g. jaguar 45/45/45).

### 1.4 dps-calc stat blocks

Source: `refs/osrs-dps-calc/cdn/json/monsters.json` (13 entries with id in 12806-12835: 12810-12819, 12821, 12825, 12833). Skills and bonuses identical to cache stats/params above wherever both exist. Fields unique to dps-calc per NPC: `style`, `speed`, `max_hit`, ranged defence triple (`light/standard/heavy`), `flat_armour: 0`, `attributes: []`, `immunities: {burn: null}`. Examples verbatim:

- 12810 Jaguar warrior: `"speed": 5, "style": ["Slash"], "max_hit": "47 (x3)"`, defensive heavy/standard/light 45/45/45.
- 12812/12813 Minotaur versions `"Normal"` / `"Red Flag"`, both `"max_hit": "74"`, speed 5, style Crush.
- 12817 Javelin Colossus `"max_hit": "48"`, speed 5, ranged def 30/30/30.
- 12818 Manticore `"speed": 10, "style": ["Melee","Ranged","Magic"]`, `"max_hit": "31 (Melee)/36 (Ranged)/31 (Magic)"` (wiki form, see 1.5), ranged def 25/25/25.
- 12821 Sol Heredit: skills 350/200/400/1500/350/300, offensive `atk 250, str 5, ranged 150, ranged_str 5`, defensive `stab 65, slash 5, crush 30, magic 750, light/standard/heavy 825/825/825`, speed 0, style `["Various"]`.
- Warband archer offensive block: `"magic": 150` AND `"ranged": 150` (cache param 3 is 0 for archer; only param 4=150). Berserker offensive `"magic": 150` too (cache param 3 = 0). DIVERGENCE recorded.

### 1.5 Wiki infoboxes (RuneC wiki cache)

Source: `refs/RuneC/tools/wiki_cache/infobox_monster_d04b58e9f1_002500.json` (full infobox dicts; all colosseum NPCs released "20 March 2024", all `poison_immune/venom_immune/cannon_immune: Immune`). Unique facts beyond 1.4:

- Jaguar warrior: `attack_speed: 5`, `max_hit: ["47 (x3)"]`, `experience_bonus: 5`.
- Warband archer/seer/berserker: `attack_speed: 6`, max hits 14 / 12 / 29, xp bonus 12.5 / 0 / 10.
- Minotaur: `attack_speed: 5`, `max_hit: ["74"]`, xp bonus 10.
- Javelin Colossus: `max_hit: ["48", "49 (Relentless I)", "51 (Relentless II)", "54 (Relentless III)"]`.
- Manticore: `attack_speed: 10`, `max_hit: ["31 (Melee)", "36 (Ranged)", "31 (Magic)"]`, `poisonous: "No, ([[venom]] if toggled)"`.
- Shockwave Colossus: `attack_speed: 5`, `max_hit: ["56"]`, magic_damage_bonus 35.
- Sol Heredit: `attack_speed: 0`, `max_hit: ["45 (Typeless AOE)", "45 (Grapple)", "15-25-35 (Triple Parry 1)", "15-30-45 (Triple Parry 2)"]`, `freeze_resistance: "100% resistance"`, `thrall_immune: "Immune"`, xp bonus 85, examine "Its stompin' time!".
- Healing totem: `hitpoints: 1`, size 1, examine "Good for your health." No combat stats in infobox.
- Bee Swarm / Doom Scorpion colosseum variants have NO wiki infobox in this cache (only "Bee Swarm (Deadman)" from 19 July 2024 exists).

### 1.6 Animation ids (gameval, full colosseum block)

Source: `refs/osrs-client-deob/runelite-api/src/main/java/net/runelite/api/gameval/AnimationID.java` lines 10802-10913. The contiguous block 10798-10911:

```
NPC_COLOSSEUM_CRYSTAL_SPAWN/IDLE/DESPAWN/CHARGE/ATTACK_01 = 10798-10802
VFX_COLOSSEUM_CRYSTAL_CHARGE_01_BEAM_01..04 = 10803-10806
VFX_COLOSSEUM_CRYSTAL_ATTACK_01_BEAM_01..04 = 10807-10810
VFX_COLOSSEUM_CRYSTAL_ATTACK_01_IMPACT_01 = 10811
VFX_COLOSSEUM_SUNFIRE_LIGHTNING_01_BEAM_01 = 10812
VFX_COLOSSEUM_HOT_SAND_01_IDLE/DESPAWN = 10813-10814
VFX_COLOSSEUM_HOT_SAND_02_PROJECTILE/IDLE = 10815-10816
NPC_COLOSSEUM_SOLAR_FLARE_CRYSTAL_IDLE_01 = 10817
VFX_DOOM_SCORPION_ATTACK_IMPACT_01 = 10818
VFX_DOOM_SCORPION_01_PLAYER_DEATH_01 = 10819
VFX_COLOSSEUM_BEES_JAR_IMPACT_01 = 10820
NPC_COLOSSEUM_BEES_SPAWN/IDLE/ATTACK/DESPAWN = 10821-10824
CHEST_COLOSSEUM01_REWARD01_SPAWN_01 = 10825
VFX_COLOSSEUM_TOTEM_PROJECTILE_01 = 10826
NPC_COLOSSEUM_TOTEM_IDLE/ATTACK = 10827-10828
VFX_COLOSSEUM_TOTEM_IMPACT_01 = 10829
VFX_COLOSSEUM_EXPLOSION01..04 = 10830-10833
VFX_COLOSSEUM_HUMAN_GIB_01 = 10834, MANTICORE_GIB = 10835, COLOSSI_GIB = 10836, MINOTAUR_GIB = 10837
VFX_COLOSSEUM_MANTICORE_EXPLOSION_01 = 10838, COLOSSI_EXPLOSION_01 = 10839
NPC_MINOTAUR_BOSS_IDLE/DEFEND/WALK/ATTACK_MELEE/ATTACK_MAGIC/SPAWN/DEATH = 10840-10846
NPC_JAGUAR_RANGER_CLAWS_ATTACK = 10847, NPC_JAGUAR_HUMAN_UNARMED_DEF = 10848, NPC_JAGUAR_HUMAN_DEATH = 10849
NPC_FREMENNIK_WARBANDER_ARCHER_ATT_COLOSSEUM = 10850
NPC_FREMENNIK_WARBAND_ARCHER_HUMAN_UNARMED_DEF = 10851, _DEATH = 10852
NPC_FREMENNIK_WARBANDER_MAGE_ZAROS_VERTICAL_CASTING_WALKMERGE = 10853
NPC_FREMENNIK_WARBAND_MAGE_HUMAN_UNARMED_DEF = 10854, _DEATH = 10855
NPC_FREMENNIK_WARBANDER_MELEE_HUMAN_SWORD_STAB = 10856
NPC_FREMENNIK_WARBAND_MELEE_HUMAN_UNARMED_DEF = 10857, _DEATH = 10858
NPC_SERPENT_MAGER_CASTING = 10859, NPC_SERPENT_MAGER_DEATH = 10860
NPC_HUMAN_STAFFREADY_SPAWN = 10861, NPC_HUMAN_READY_SPAWN = 10862
NPC_MANTICORE_01_IDLE/WALK/BACKWARDS_WALK/DEATH/DEATH_EXPLODE = 10863-10867
NPC_MANTICORE_01_TRIPLE_CHARGE = 10868, TRIPLE_THROW = 10869, SPAWN_01 = 10870, SPAWN_02 = 10871
HUMAN_SHIELD_COMBATANT_IDLE = 10872, HUMAN_DOOM_SCORPION_01_PLAYER_DEATH_01 = 10873
NPC_COLOSSI_FINALBOSS_01_IDLE = 10874, SITTING_IDLE = 10875, ARENA_JUMP = 10876, ARENA_LAND = 10877, WALK = 10878
NPC_COLOSSI_JAVELIN_WALK = 10879
NPC_COLOSSI_SHOCKWAVE_01_WALK_NOSOUND = 10880, WALK = 10881
NPC_COLOSSI_FINALBOSS_01_MELEE_ATTACK = 10882, MELEE_ATTACK_TELEGRAPH = 10883
NPC_COLOSSI_FINALBOSS_01_GRAPPLE_ATTACK_TELEGRAPH = 10884, SHIELDSLAM_TELEGRAPH = 10885
NPC_COLOSSI_FINALBOSS_TRIPLEATTACK = 10886, TRIPLEATTACK_SHORTER = 10887
NPC_COLOSSI_FINALBOSS_01_DEATH = 10888
NPC_COLOSSI_JAVELIN_01_IDLE = 10889, WALK = 10890, WALKFADE = 10891, RANGE_ATTACK = 10892, ARTILLERY_ATTACK = 10893, DEATH = 10894
NPC_COLOSSI_SHOCKWAVE_01_DEATH = 10895
NPC_COLOSSI_JAVELIN_01_SPEARHEAD = 10896, ARTILLERY_SLOW = 10897, ARTILLERY_FAST = 10898, ARTILLERY_FIRE = 10899
NPC_COLOSSI_JAVELIN_01_SPEARHEAD_FIRE_SLOW = 10900, SPEARHEAD_FIRE_FAST = 10901
NPC_COLOSSI_SHOCKWAVE_01_IDLE = 10902, CLAPATTACK = 10903
VFX_COLOSSI_SHOCKWAVE_CLAP_PROJ = 10904
SPOTANIM_COLOSSI_FINALBOSS_01_DEATH = 10905, LAND = 10906, TRIPLE_ATTACK_SHORTER = 10907, TRIPLE_ATTACK = 10908, MELEE = 10909
```

Plus `NPC_MINOTAUR_BOSS_WALK_FAST = 11746`, `NPC_MINOTAUR_BOSS_ATTACK_MELEE_LOUDER = 11747`, `NPC_MINOTAUR_BOSS_DEATH_LOUDER = 11748` (same file, lines 11747-11749).

### 1.7 Already-exported viewer mapping (worktree)

Source: `/Users/valtterivalo/.codex/worktrees/osrs-colosseum/pufferlib-metal/ocean/osrs/data/npc_models_colosseum.h` (generated by `ocean/osrs/tools/export_colosseum_npcs.py`). Format `{npc_id, synthetic_model, idle_anim, 0xFFFF, walk_anim}`:

```
{12810, 0xC320A, 808, 0xFFFF, 819},    /* Jaguar warrior */
{12811, 0xC320B, 813, 0xFFFF, 1205},   /* Serpent shaman */
{12812, 0xC320C, 10840, 0xFFFF, 10842},/* Minotaur */
{12814, 0xC320E, 808, 0xFFFF, 819},    /* Fremennik warband archer */
{12815, 0xC320F, 813, 0xFFFF, 1205},   /* Fremennik warband seer */
{12816, 0xC3210, 808, 0xFFFF, 819},    /* Fremennik warband berserker */
{12817, 0xC3211, 10889, 0xFFFF, 10879},/* Javelin Colossus */
{12818, 0xC3212, 10863, 0xFFFF, 10864},/* Manticore */
{12819, 0xC3213, 10902, 0xFFFF, 10881},/* Shockwave Colossus */
{12821, 0xC3215, 10874, 0xFFFF, 10878},/* Sol Heredit */
```

Matches the cache stand/walk anims in 1.2 exactly. `ocean/osrs/asset_manifest.json` lists the colosseum file group: `colosseum.terrain/.objects/.atlas/.cmap`, `colosseum_npcs.models/.anims` under asset_version `osrs-assets-v11`.

---

## 2. SPAWN / MAP DATA

### 2.1 Region ids

Source: `refs/osrs-client-deob/runelite-client/src/main/java/net/runelite/client/plugins/discord/DiscordGameEventType.java:308-309`, verbatim:

```java
MG_FORTIS_COLOSSEUM("Fortis Colosseum", DiscordAreaType.MINIGAMES, 7216),
MG_FORTIS_COLOSSEUM_LOBBY("Fortis Colosseum Lobby", DiscordAreaType.MINIGAMES, 7316),
```

7216 = (28 << 8) | 48 → mapsquare base (1792, 3072). 7316 = (28 << 8) | 148 → base (1792, 9472).

### 2.2 RuneC activity-spawn rows (THE coordinate find)

Source: `refs/RuneC/data/defs/activity_spawns.bin` (binary, format documented in `refs/RuneC/tools/export_activity_spawns.py`; curated TOML source `data/curated/activity_spawns.toml` is not present in the checkout). Decoded read-only; 16 rows for slug `fortis_colosseum_sol_heredit`. Asserted in `refs/RuneC/tests/test_activity_spawns_runtime.c:108-119` (`sol->npc_id == 12821 && sol->x == 1824 && sol->y == 3109`). Verbatim decode:

| kind | key | coords | npc/obj | ref |
|---|---|---|---|---|
| region | `strategy_tile_marker_extent` | (1807,3089)..(1841,3124) | - | `oldschool_wiki_api:Module:Tile_markers/Colosseu` |
| region | `wave_12_blocked_corner_north_east_provisional` | (1831,3113)..(1833,3115) | - | `private:okronos_sol_handler` |
| region | `wave_12_blocked_corner_north_west_provisional` | (1816,3113)..(1818,3115) | - | `private:okronos_sol_handler` |
| region | `wave_12_blocked_corner_south_east_provisional` | (1831,3098)..(1833,3100) | - | `private:okronos_sol_handler` |
| region | `wave_12_blocked_corner_south_west_provisional` | (1816,3098)..(1818,3100) | - | `private:okronos_sol_handler` |
| region | `wave_12_blocked_edge_east_provisional` | (1833,3098)..(1833,3114) | - | `private:okronos_sol_handler` |
| region | `wave_12_blocked_edge_north_provisional` | (1816,3115)..(1833,3115) | - | `private:okronos_sol_handler` |
| region | `wave_12_blocked_edge_south_provisional` | (1816,3098)..(1833,3098) | - | `private:okronos_sol_handler` |
| region | `wave_12_blocked_edge_west_provisional` | (1816,3098)..(1816,3114) | - | `private:okronos_sol_handler` |
| region | `wave_12_reduced_arena_outer_bounds_provisional` | (1816,3098)..(1833,3115) | - | `private:okronos_sol_handler` |
| wave_point (wave=12) | `wave_12_sol_provisional_spawn` | (1824,3109) | npc 12821 Sol Heredit | `private:okronos_sol_handler` |
| object_anchor | `pillar_north_east` | (1831,3113) plane 0 | obj 52490 Pillar | |
| object_anchor | `pillar_north_west` | (1816,3113) plane 0 | obj 52490 Pillar | |
| object_anchor | `pillar_south_east` | (1831,3098) plane 0 | obj 52490 Pillar | |
| object_anchor | `pillar_south_west` | (1816,3098) plane 0 | obj 52490 Pillar | |
| object_anchor | `sol_throne` | (1823,3123) plane 1 | obj 52514 Throne | |

Provenance caveats baked into the repo: `tools/reports/activity_spawns.txt` header says "Provisional rows are runtime-usable only when their source_status explicitly says so; they are not authoritative OSRS sign-off", and source statuses include `provisional_private_server_verified: 2`. `tools/reports/area_flags_sources.txt:67-80` states verbatim: "The Colosseum tile-marker page provides player strategy markers/deadzones, not authoritative wave-12 spawn/reduced-arena geometry" and "The cache confirms static object anchors, including ... Sol pillars on plane 0, and Sol throne object 52514 on plane 1. It does not expose server-authored NPC spawn or map-area groups."

NO authoritative per-wave NPC spawn tiles exist anywhere in the local repos (RuneC `npc_reconciliation.txt:44` lists "exact Nex and Sol Heredit activity-spawn extraction from cache/server-script/activity configs" as outstanding).

### 2.3 Object ids (gameval)

Source: `refs/osrs-client-deob/runelite-api/src/main/java/net/runelite/api/gameval/ObjectID1.java` (250 COLOSSEUM matches, mostly wallkit/roofkit decor). Gameplay-relevant:

```
COLOSSEUM_REWARD = 50741              COLOSSEUM_WAVE_EGG_SHELL = 50742
COLOSSEUM_MOLTEN_POOL_1/2/3 = 50743-50745
COLOSSEUM_HOLY_FIRE = 50746           COLOSSEUM_SCOREBOARD = 50747
COLOSSEUM_BANK = 50748                COLOSSEUM_ENTRANCE_OUTSIDE = 50749
COLOSSEUM_EXIT_LOBBY = 50750          COLOSSEUM_ENTRANCE = 50751
COLOSSEUM_EXIT = 50752
PILLAR_CIVITAS01_COLOSSEUM01 = 52490  (line 53833)
THRONE_CIVITAS_COLOSSEUM01_BIG01 = 52514  (line 53881)
```

### 2.4 Worktree map exports

`/Users/valtterivalo/.codex/worktrees/osrs-colosseum/pufferlib-metal/ocean/osrs/data/` already contains `colosseum.terrain`, `colosseum.objects`, `colosseum.atlas`, `colosseum.cmap`, `colosseum.tanim` (exported from the modern cache; binary).

---

## 3. PROJECTILE / SPOTANIM IDS

Source for names: `refs/osrs-client-deob/runelite-api/src/main/java/net/runelite/api/gameval/SpotanimID.java` lines 2673-2731 (plus leagues `_ECHO` clones 3145-3158). Source for contents: modern cache index 2 group 13, decoded with `rc_cache.definitions.decode_spotanim_definition` (model/anim/resize verbatim below).

```
2666 NPC_COLOSSI_COLOSSI_01_LAND                       model 51211 anim 10906 resize 300/300
2667 NPC_COLOSSI_COLOSSI_TRIPLEATTACK_01_TELEGRAPH     model 51244 anim 10908 resize 300/300
2668 NPC_COLOSSI_COLOSSI_TRIPLEATTACK_01_TELEGRAPH_SHORTER model 51244 anim 10907 resize 300/300
2669 SPOTANIM_COLOSSI_FINALBOSS_01_MELEE               model 52613 anim 10830
2670 SPOTANIM_COLOSSI_FINALBOSS_02_MELEE               model 52613 anim 10831
2671 SPOTANIM_COLOSSI_FINALBOSS_03_MELEE               model 52613 anim 10832
2672 SPOTANIM_COLOSSI_FINALBOSS_04_MELEE               model 52613 anim 10833
2673 NPC_COLOSSI_JAVELIN_01_SPEARHEAD                  model 52586 anim 10896
2674 NPC_COLOSSI_JAVELIN_01_ARTILLERY_SLOW             model 52587 anim 10900
2675 NPC_COLOSSI_JAVELIN_01_ARTILLERY_FAST             model 52587 anim 10901
2676 NPC_COLOSSI_JAVELIN_01_ARTILLERY_FIRE             model 52586 anim 10899
2677 NPC_COLOSSI_JAVELIN_01_SPEARHEAD_FIRE_SLOW        model 52588 anim 10900
2678 NPC_COLOSSI_JAVELIN_01_SPEARHEAD_FIRE_FAST        model 52588 anim 10901
2679 NPC_COLOSSI_SHOCKWAVE_01_CLAPATTACK               model 51210 anim 10903
2680 NPC_COLOSSI_FINALBOSS_01_DEATH                    model 46395 anim 10905 resize 300/300
2681 VFX_MANTICORE_01_PROJECTILE_MAGIC_01              model 51215 anim 10329
2682 VFX_MANTICORE_01_PROJECTILE_IMPACT_MAGIC_01       model 51215 anim 10330
2683 VFX_MANTICORE_01_PROJECTILE_RANGED_01             model 51221 anim 10327
2684 VFX_MANTICORE_01_PROJECTILE_IMPACT_RANGED_01      model 48342 anim 10330
2685 VFX_MANTICORE_01_PROJECTILE_MELEE_01              model 51213 anim 10328
2686 VFX_MANTICORE_01_PROJECTILE_IMPACT_MELEE_01       model 48337 anim 10330
2687 VFX_COLOSSEUM_TOTEM_PROJECTILE_01                 model 51249 anim 10826
2688 VFX_COLOSSEUM_TOTEM_IMPACT_01                     model 51249 anim 10829
2689-2692 VFX_COLOSSEUM_CRYSTAL_CHARGE_01_BEAM_01..04  model 51245 anims 10803-10806
2693-2696 VFX_COLOSSEUM_CRYSTAL_ATTACK_01_BEAM_01..04  model 51245 anims 10807-10810
2697 VFX_COLOSSEUM_CRYSTAL_ATTACK_01_IMPACT_01         model 51245 anim 10811
2698 VFX_COLOSSEUM_SUNFIRE_LIGHTNING_01_BEAM_01        model 51243 anim 10812
2699-2706 VFX_COLOSSI_STAB_DUST_01..08                 model 52521 anims 10098-10105
2707 VFX_COLOSSEUM_BEE_JAR_IMPACT_01                   model 52523 anim 10820 resize 64/64
2708 VFX_COLOSSEUM_BEE_JAR_TRAVEL                      model 46382 anim 9642  resize 64/64
2709 VFX_COLOSSEUM_HOT_SAND_02_PROJECTILE_01           model 51237 anim 10815
2710 VFX_COLOSSEUM_HOT_SAND_02_PROJECTILE_02           model 51234 anim 10815
2711 VFX_DOOM_SCORPION_01_PLAYER_DEATH_01              model 51232 anim 10819
2712 VFX_DOOM_SCORPION_ATTACK_IMPACT_01                model 35394 anim 10818
2713-2720 VFX_COLOSSEUM_HUMAN_EXPLOSION_01..08         model 46421 anim 9610
2721 VFX_COLOSSEUM_MANTICORE_EXPLOSION_01              model 46421 anim 10838 resize 44/44
2722 VFX_COLOSSEUM_COLOSSI_EXPLOSION_01                model 46421 anim 10839 resize 150/150
2723 VFX_COLOSSEUM_MINOTAUR_EXPLOSION_01               model 46421 anim 9610  resize 300/300
2724 VFX_COLOSSEUM_FINALBOSS_EXPLOSION_01              model 46421 anim 10839 resize 300/300
```

Minotaur magic projectile (separate range): `VFX_MINOTAUR_PROJECTILE_MAGIC_01 = 2950`, `VFX_MINOTAUR_PROJECTILE_IMPACT_MAGIC_01 = 2951` (SpotanimID.java:2957-2958).

---

## 4. MODIFIER INTERNALS

### 4.1 Modifier registry from cache structs (names, internal ids, tier texts, costs)

Source: modern cache index 2 group 34 (structs), ids 891-915, decoded read-only (params op 249). Param keys: 1895 = modifier internal id, 1896 = name, 1897/1898/1899 = tier I/II/III description, 1901 = has-tiers flag, 1903 = cost/points value, 1914 = ordering index. Verbatim tier texts:

| mod id | struct | name | tiers | cost | tier descriptions |
|---|---|---|---|---|---|
| 0 | 915 | Mantimayhem | 3 | 150 | I: "Manticores fire an additional projectile per orb." II: "+ apply Venom on hit." III: "+ apply Venom and Melee orbs can appear anywhere in the sequence." |
| 1 | 891 | Reentry | 3 | 150 | I: "Javelins leave a temporary pool of molten sand where they land." II: "big permanent pool" III: "very big permanent pool" |
| 2 | 892 | Bees! | 3 | 150 | I: "A swarm of angry bees drifts around the arena." II: "Two swarms" III: "Three swarms" |
| 3 | 893 | Volatility | 3 | 100 | I: "Enemies explode on death." II: "explode in a large radius on death." III: "Damaging energy in a large radius on death and leave behind molten sand." |
| 4 | 894 | Blasphemy | 3 | 100 | I: "Prayer is drained by 20% of damage received from enemies off-prayer." II: 40% III: 60% |
| 5 | 897 | Relentless | 3 | 200 | I: "Enemies ignore 33% of your defence and hit an extra 1 damage." II: "66% ... extra 3" III: "all defence ... extra 6" |
| 6 | 898 | Quartet | 1 | 100 | "An extra random Fremennik Warbander spawns every wave." |
| 7 | 899 | Totemic | 1 | 200 | "Healing totems spawn in and aid your enemies." |
| 8 | 900 | Doom | 3 | 200 | I: "A stack of Doom is gained every time you take damage. Certain death at 15 stacks. Resets each wave." II: "10 stacks" III: "5 stacks" |
| 9 | 901 | Dynamic Duo | 1 | 150 | "Shockwave Colossi always spawn in pairs." |
| 10 | 902 | Solarflare | 3 | 250 | I: "Damaging energy orbits an area slowly." II: "quickly and deals more damage." III: "very quickly, deals more damage and disables prayer." |
| 11 | 903 | Myopia | 3 | 200 | I: "Your range is reduced by 2 tiles with autocast and ranged attacks." II: 4 tiles III: 6 tiles |
| 12 | 906 | Frailty | 3 | 200 | I: "Your maximum HP is reduced by 10% and can no longer be boosted." II: 20% III: 40% |
| 13 | 907 | Red Flag | 1 | 250 | "Minotaurs can no longer be safespotted." |

14 modifiers total (param 1895 values 0-13, no others exist in group 34). Param 1900 = 0 on all.

### 4.2 Varbits / VarPlayers

Source: `refs/osrs-client-deob/runelite-api/src/main/java/net/runelite/api/gameval/VarbitID.java` (lines 3347, 5763-5786, 6334, 6896) and `VarPlayerID.java` (lines 1780-1789). Verbatim:

```
COLOSSEUM_MODIFIER_MANTIMAYHEM_STACKS_CLIENT = 4588
COLOSSEUM_SELECTED_MODIFIER = 9788
COLOSSEUM_MODIFIER_BLASPHEMY_STACKS_CLIENT = 9790
COLOSSEUM_MODIFIER_TOXICITY_STACKS_CLIENT = 9791
COLOSSEUM_MODIFIER_REENTRY_STACKS_CLIENT = 9792
COLOSSEUM_MODIFIER_MYOPIA_STACKS_CLIENT = 9795
COLOSSEUM_MODIFIER_FRAILTY_STACKS_CLIENT = 9796
COLOSSEUM_MODIFIER_SOLARFLARE_STACKS_CLIENT = 9797
COLOSSEUM_MODIFIER_RELENTLESS_STACKS_CLIENT = 9798
COLOSSEUM_MODIFIER_VOLATILITY_STACKS_CLIENT = 9799
COLOSSEUM_SOL_GRAPPLE_PENDING = 9800
COLOSSEUM_DOOM_STACKS_CLIENT = 9801
COLOSSEUM_LOOT_OPENED = 9802
COLOSSEUM_GLORIA_MET = 9804
COLOSSEUM_HERB_PATCH_CHAT = 9806
COLOSSEUM_MASTER_INTRO = 9807
COLOSSEUM_PASSIONATE_SUPPORTER_SPEECH = 9808
COLOSSEUM_BOSS_CUTSCENE_SEEN = 9809
COLOSSEUM_SOL_FAILURES = 9810
COLOSSEUM_KILLTIME_BEST = 9811
COLOSSEUM_KILLTIME_LATEST = 9812
COLOSSEUM_MODIFIER_DOOM_STACKS_CLIENT = 10681
COLOSSEUM_HIGHEST_WAVE = 11410
CA_TOTAL_TASKS_COMPLETED_COLOSSEUM = 9778

VarPlayer:
COLOSSEUM_INT_TEMP_TRANSMIT_1/2/3 = 4127/4128/4129
COLOSSEUM_GLORY = 4130
TOTAL_COLOSSEUM_WAVES_COMPLETED = 4131
COLOSSEUM_CURRENT_GLORY = 4132
COLOSSEUM_WAVE_START_TIME = 4133
COLOSSEUM_WAVE_DAMAGE_TAKEN = 4134
COLOSSEUM_LAST_MODIFIER_GLORY = 4135
COLOSSEUM_LAST_WAVE_DURATION = 4136
```

Note: a `TOXICITY` stacks varbit (9791) exists in gameval with no matching modifier struct in 4.1 (the venom behavior is folded into Mantimayhem II/III text). 9789, 9793-9794 are non-colosseum (`SOUL_WARS_*`). Old-api `Varbits.java:968` documents `COLOSSEUM_DOOM = 9801` as "The amount of Doom stacks received in the Fortis Colosseum."

Old-api cross-check: RuneLite `timersandbuffs/TimersAndBuffsConfig.java:460-465` has a `showColosseumDoom` toggle ("Configures whether Fortis Colosseum doom buff is displayed.").

### 4.3 Interfaces

Source: `gameval/InterfaceID.java` lines 253-874, 9442:

```
COLOSSEUM_REWARD_CHEST = 246      COLOSSEUM_INTERMISSION = 626
COLOSSEUM_REWARD_CHEST_2 = 864    COLOSSEUM_INTERMISSION_2 = 865
COLOSSEUM_REWARD = 866            COLOSSEUM_SCOREBOARD = 867
COLOSSEUM_REWARD_VALUE = 0x00f6_0003 (component on 246)
```

### 4.4 Modifier-related NPC entities (cache, from 1.2)

Bee Swarm 12823 (anims 10821-10824), beam crystal 12824 (stand 10799, model 51243 = the SUNFIRE_LIGHTNING beam model), Healing totem 12825 (idle 10827, attack 10828, projectile/impact spotanims 2687/2688), solar flare 12826 (stand 10817), Doom Scorpion 12822 (stand 6252, walk 6253, attack-impact spotanim 2712, player-death spotanim 2711).

### 4.5 Doom buff HUD struct

Modern cache struct 888 (group 34): `{1276: 12303291, 1277: 2, 1279: 'Doom.', 1280: 409625, 1278: 4766, 1275: 113, 1288: 1}` (buff-bar definition referencing tooltip text "Doom."). Recorded raw, keys not interpreted.

---

## 5. DPS-CALC LOGIC

Negative result, verified. In `refs/osrs-dps-calc/src/` there are ZERO occurrences (case-insensitive) of: colosseum, heredit, frailty, myopia, relentless, solarflare, blasphemy, totemic, mantimayhem, volatility, reentry, quartet, "dynamic duo", "red flag". The "Doom" hits in source are Doom of Mokhaiotl, a different boss (`src/lib/constants.ts:531` `DOOM_OF_MOKHAIOTL_IDS = [14707]`). No Sol parry logic, no colosseum modifier handling, no Doom-stack multipliers exist in the calculator. Colosseum content in this repo is exclusively the 13 monster JSON entries (section 1.4).

Tangentially colosseum-adjacent player gear logic that DOES exist:
- `src/lib/PlayerVsNPCCalc.ts:792` `if (this.wearing('Tonalztics of ralos')) {` and `:1558` ranged charged-version handling (Tonalztics is the Sol Heredit drop, logic is generic weapon math).
- Sunfire rune min-hit: `src/types/Player.ts:163-167` "Sunfire runes ensure 10% minimum hit."

---

## 6. MISC

### 6.1 RuneC curated encounter spec (RuneC-authored, NOT raw game data)

Source: `refs/RuneC/data/defs/encounters.bin` (slug `colosseum`, format per `tools/export_encounters.py` docstring; curated TOML source absent from checkout). Verbatim decode:

```
npc_ids = [12819, 12821, 12818]
attack 'Combo Slash':   style=crush  max_hit=28 warn_ticks=0 owner_npc=12821
attack 'Spear Throw':   style=ranged max_hit=28 warn_ticks=2 owner_npc=12821
attack 'Shield Stun':   style=crush  max_hit=40 warn_ticks=3 owner_npc=12821
attack 'Triple Strike': style=crush  max_hit=25 warn_ticks=0 owner_npc=12821
mechanic 'Sol Phase Style Shift':   primitive=hp_gated_style_increase
mechanic 'Modifier Stack Effects':  primitive=run_level_modifier_registry
```

These max hits conflict with the wiki infobox values in 1.5 (45/45, triple 15-25-35 and 15-30-45). Both recorded; the encounters.bin numbers are RuneC's hand-authored curation.

Related: `refs/RuneC/data/defs/activity_mechanics.bin` has one colosseum row: `slug='Sol_Heredit' name='Sol Heredit' npc_ids=[12821] sections=[('Fight overview', 3846 bytes, hash 0xfe2c6331)]` sourced from wiki pages `'Sol Heredit;Sol Heredit/Strategies'`; the section TEXT is not stored (hash+length only). `tools/reports/activity_schemas.txt:34,40`: "colosseum status=READY npcs=3 objects=0 states=0 mechanics=3 spawns=0/12" and "fortis_colosseum_sol_heredit status=READY_WITH_ACCEPTED_SIMPLIFICATIONS npcs=2 objects=2 states=0 mechanics=0 spawns=0/1".

### 6.2 Boss-info struct (cache)

Modern cache struct 881 (group 34), verbatim: `{1313: 'Fortis Colosseum', 1314: 1563, 1315: 25, 1319: 'Sol Heredit is the final boss of the Fortis Colosseum, located in Civitas illa Fortis in Varlamore.<br><br>Attack Styles - Melee, Ranged, Magic, Special<br>Immunities - Venom<br>Slayer Categories - None', 1320: 'Mandatory requirements:<br> - Children of the Sun', 1317: 29887520, 1318: 40, 1322: 52580, 1999: 55551, 1323: 10874, 1324: 0, 1325: 170, 1326: 18, 1327: 339, 1328: 0, 1329: 1650, 675: 13}` (1323 = Sol idle anim 10874; other keys recorded raw).

### 6.3 Combat achievements (cache structs, wave facts embedded)

Modern cache group 34, structs with param 1312 == 25 (colosseum CA category; 13 tasks, ca ids 538-550). Wave-mechanic facts contained in the verbatim descriptions:

- 542 "Furball": "Complete Wave 4 without taking avoidable damage from a Manticore."
- 545 "Denied": "Complete Wave 7 without the Minotaur ever healing other enemies."
- 546 "One-off": 'Complete Wave 11 with either "Red Flag", "Dynamic Duo", or "Doom II" active.'
- 547 "Reinforcements": 'Defeat Sol Heredit with the "Bees! II", "Quartet", and "Solarflare II" modifiers active.'
- 543 "Perfect Footwork": "Defeat Sol Heredit without taking any damage from his Spear, Shield, Grapple or Triple Attack."
- 540 "I Brought Mine Too": "Defeat Sol Heredit using only a Spear, Hasta or Halberd."
- 541 "Slow Dancing in the Sand": "...without running during the fight with him."
- 544 "Showboating": "...after Fortis Saluting to the North, East, South and West of the arena while he's below 10% Hitpoints."
- 548 "I was here first!": "Kill a Jaguar Warrior using a Claw-type weapon special attack."
- 549/550 speed: total time 28:00 / 24:00 or less.

### 6.4 Other touchpoints

- `refs/osrs-client-deob/runelite-client/.../screenshot/ScreenshotPlugin.java:440-443`: completion chat message verbatim `"Search the chest nearby to retrieve your earned rewards!"` → `KillType.FORTIS_COLOSSEUM`.
- gameval `DBTableID.java`: `MUSIC_FORTIS_COLOSSEUM = 3475`, `QUETZAL_COLOSSEUM = 3526`, `HISCORES_ACTIVITY_COLOSSEUM_GLORY = 3998`, `SYNTH_COLOSSEUMREWARDS = 4377`, `SYNTH_NPCCOLOSSEUM = 4379`, `SYNTH_COLOSSEUMMODIFIERS = 4381` (sound/hiscore rows, no gameplay data).
- RuneC hiscore struct 909 (group 34): `{689: 'Fortis Colosseum', 690: 5414, 2519: 25}`.
- `refs/osrs-cache-openrs2-b238/cache/` is a Jagex dat2/idx disk store, readable with the same `ModernCacheReader`; npc 12821 decodes identically (Sol Heredit, lvl 1563, size 5). No extracted json/text files exist in that repo.
- `refs/rsmod-data/` contains ONLY `obj-enricher.toml` (719KB item enrichment). No NPC, spawn, or map data. Colosseum-adjacent lines: 16802 `obj = 'tonalztics_of_ralos_uncharged'`, 16817 `obj = 'tonalztics_of_ralos_charged'`.
- RuneC wiki_cache `pages`/`transcript` buckets contain NO colosseum modifier text (checked all 14 modifier names).
- Sol P2-P4 phase NPCs do not exist; only 12821 (`COLOSSEUM_SOL_P1`) and 12827 (seated). No NPC-def varbit transforms on any colosseum NPC, so phase transitions are server-side (consistent with `COLOSSEUM_SOL_GRAPPLE_PENDING` varbit + anims 10882-10888 living on one NPC).

## Dead ends (explicit)

1. Per-wave NPC spawn tiles for waves 1-11: NOT in any local repo (RuneC explicitly lists this as unresolved; cache holds no server spawn data).
2. Modifier numeric internals beyond the tier descriptions (e.g. Solarflare orbit damage, bee swarm damage, totem heal rate): not present locally.
3. Sol Heredit attack timings/cycle: not present locally (RuneC's curated 4-attack spec is their own simplification; wiki infobox attack_speed is 0).
4. osrs-sdk: engine only, no colosseum content despite being the Colosim engine (content lives in a repo not checked out here).
5. dps-calc: no colosseum calc logic of any kind.
6. rs-map-viewer, autoresearch, InfernoTrainer: nothing colosseum.
