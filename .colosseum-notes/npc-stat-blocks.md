# Fortis Colosseum NPC Combat Stat Blocks

Source: Old School RuneScape Wiki (oldschool.runescape.wiki). Every number below is taken
verbatim from each NPC's `{{Infobox Monster}}` template via `?action=raw` (the raw wikitext
that renders the "Monster combat stats", "Aggressive stats", and "Defensive stats" tables).
Pulled 2026-06-09.

Note on the three "Fremennik" trash mobs: the in-game Colosseum versions live on the wiki at
`Fremennik warband berserker / archer / seer` (the page titles do NOT contain "(Fortis
Colosseum)"). Those are the Colosseum encounters and the ids match the Colosseum NPC ids.

## Wiki field -> struct field mapping

| struct field        | wiki infobox param            | notes |
|---------------------|-------------------------------|-------|
| hp                  | `hitpoints`                   | |
| att/str/def_level   | `att` / `str` / `def`         | "Monster combat stats" |
| magic_level         | `mage`                        | |
| range_level         | `range`                       | |
| attack_speed        | `attack speed`                | ticks |
| size                | `size`                        | tiles |
| max_hit             | `max hit`                     | base value; multi-hit / per-style noted |
| melee_att_bonus     | `attbns`                      | wiki gives one combined melee attack bonus (the style it attacks with) |
| melee_str_bonus     | `strbns`                      | |
| magic_att_bonus     | `amagic`                      | |
| magic_str_bonus     | `mbns`                        | "Magic Strength" / magic damage integer |
| range_att_bonus     | `arange`                      | |
| ranged_str_bonus    | `rngbns`                      | |
| stab_def            | `dstab`                       | |
| slash_def           | `dslash`                      | |
| crush_def           | `dcrush`                      | |
| magic_def           | `dmagic`                      | |
| ranged_def          | `dstandard` (== `dlight` == `dheavy`) | wiki splits ranged defence into light/standard/heavy; all three are equal for every Colosseum NPC, so a single `ranged_def` = the standard value is exact |

The OSRS `{{Infobox Monster}}` uses a single combined `attbns` for the monster's melee attack
bonus rather than separate stab/slash/crush attack columns, so `melee_att_bonus = attbns`
regardless of which melee style the NPC swings.

---

## Confirmed combat stats (all 10)

| # | NPC | npc_id | hp | att | str | def | mage | range | speed | size | max_hit | attack style |
|---|-----|-------:|---:|----:|----:|----:|-----:|------:|------:|-----:|--------:|--------------|
| 1 | Fremennik warband berserker | 12816 | 48 | 110 | 110 | 80 | 110 | 110 | 6 | 1 | 29 | Stab (melee) |
| 2 | Fremennik warband archer | 12814 | 50 | 110 | 110 | 80 | 110 | 110 | 6 | 1 | 14 | Ranged |
| 3 | Fremennik warband seer | 12815 | 50 | 110 | 110 | 80 | 110 | 110 | 6 | 1 | 12 | Magic |
| 4 | Serpent shaman | 12811 | 125 | 100 | 90 | 90 | 220 | 160 | 5 | 1 | 28 | Magic |
| 5 | Jaguar warrior | 12810 | 125 | 200 | 330 | 125 | 100 | 160 | 5 | 2 | 47 (x3 hits) | Slash (melee) |
| 6 | Javelin Colossus | 12817 | 220 | 200 | 300 | 190 | 225 | 360 | 5 | 3 | 48 (base; 49/51/54 w/ Relentless I/II/III) | Ranged |
| 7 | Shockwave Colossus | 12819 | 125 | 120 | 190 | 150 | 350 | 220 | 5 | 3 | 56 | Magic |
| 8 | Minotaur (Fortis Colosseum) | 12812 / 12813 | 225 | 300 | 360 | 190 | 250 | 120 | 5 | 3 | 74 | Crush (melee) |
| 9 | Manticore | 12818 | 250 | 300 | 300 | 250 | 300 | 350 | 10 | 3 | 31 melee / 36 ranged / 31 magic | Melee + Ranged + Magic |
| 10 | Sol Heredit | 12821 | 1500 | 350 | 400 | 200 | 300 | 350 | Varies | 5 | 44 typeless AOE / 44 grapple / 15-25-35 Triple Parry I / 15-30-45 Triple Parry II | Various (melee + ranged) |

Notes:
- **Minotaur** has two ids in its infobox (`id1 = 12812`, `id2 = 12813`); use 12812 as the
  primary `npc_id`.
- **Jaguar warrior** `max hit = 47 (x3)`: three independent hits of 47 each (per-hit accuracy
  and strength rolls), so the struct's single `max_hit` is the per-hit 47.
- **Javelin Colossus** `max hit` scales with its Relentless buff stacks; base is 48.
- **Manticore** and **Sol Heredit** are multi-style; `max_hit` in the struct uses the highest
  single-style value (see per-NPC blocks for the breakdown).
- **Sol Heredit** `attack speed = Varies` on the wiki (per-attack timing differs); see
  UNCONFIRMED proposal below.

---

## UNCONFIRMED bonus / value fields per NPC

The wiki lists every aggressive and defensive bonus for the 9 non-boss NPCs, so the only
genuinely missing values are on Sol Heredit (a boss) plus the multi-style mapping ambiguity on
Manticore. Confirmed-vs-proposed is called out explicitly.

| NPC | UNCONFIRMED field(s) | proposal | one-line rationale |
|-----|----------------------|----------|--------------------|
| Fremennik warband berserker | none | - | full infobox present |
| Fremennik warband archer | none | - | full infobox present |
| Fremennik warband seer | none (ranged def explicitly 0) | - | `dlight = dstandard = dheavy = 0`, `drange` param absent => ranged_def = 0 |
| Serpent shaman | none | - | full infobox present |
| Jaguar warrior | none | - | full infobox present |
| Javelin Colossus | none | - | full infobox present |
| Shockwave Colossus | none | - | full infobox present |
| Minotaur (Fortis Colosseum) | none | - | full infobox present |
| Manticore | `melee_att_bonus`, `melee_str_bonus`, `magic_att_bonus`, `magic_str_bonus`, `range_att_bonus`, `ranged_str_bonus` (all read 0 on the wiki) | keep all 0 as listed | Manticore's infobox lists every aggressive bonus as 0; it derives damage from raw levels (300/300/350), so 0 is the wiki-confirmed value, not a gap. `max_hit` style mapping resolved to 36 (highest = ranged). |
| Sol Heredit | `attack_speed` (wiki: "Varies"); `ranged_def` (`drange` param absent) | `attack_speed = 5`; `ranged_def = 825` | Sol's basic attacks resolve on a ~5-tick cadence like the other Colosseum bosses; the wiki omits `drange` but lists `dlight = dstandard = dheavy = 825`, so 825 is the right ranged-defence value. `max_hit = 44` (highest single non-parry hit). |

Sol Heredit specifics worth flagging for the struct:
- `attack speed = Varies` is a string on the wiki, not an integer. The `uint8_t attack_speed`
  field cannot hold "Varies"; **proposed 5** (matches every other size-3+ Colosseum boss and
  Sol's standard attack rhythm). Marked UNCONFIRMED.
- `max hit` is multi-modal (44 typeless AOE, 44 grapple, plus Triple Parry sequences). For a
  single `int16_t max_hit`, **44** is the largest single-component hit (UNCONFIRMED as the
  "canonical" choice; the parry sequences can sum higher across the 3-hit combo: 15+30+45=90).
- All melee defensive bonuses are low (`dstab=65`, `dslash=5`, `dcrush=30`) but magic/ranged
  defence is enormous (`dmagic=750`, ranged 825) — Sol is meant to be meleed.

---

## Ready-to-paste C `MonsterStats` initializers

Field order matches the struct:
`{ npc_id, name, hp, att_level, str_level, def_level, magic_level, range_level, attack_speed,
size, max_hit, melee_att_bonus, melee_str_bonus, magic_att_bonus, magic_str_bonus,
range_att_bonus, ranged_str_bonus, stab_def, slash_def, crush_def, magic_def, ranged_def }`

```c
/* https://oldschool.runescape.wiki/w/Fremennik_warband_berserker
   Colosseum "berserker"; attacks with Stab. melee_att_bonus=attbns(150). */
{ 12816, "Fremennik warband berserker", 48, 110, 110, 80, 110, 110, 6, 1, 29,
  150, 90, 150, 0, 0, 0, 50, 50, 50, 0, 75 },

/* https://oldschool.runescape.wiki/w/Fremennik_warband_archer
   Colosseum "archer"; attacks with Ranged. */
{ 12814, "Fremennik warband archer", 50, 110, 110, 80, 110, 110, 6, 1, 14,
  150, 150, 150, 0, 150, 10, 0, 0, 0, 75, 50 },

/* https://oldschool.runescape.wiki/w/Fremennik_warband_seer
   Colosseum "seer"; attacks with Magic. dlight=dstandard=dheavy=0 -> ranged_def=0. */
{ 12815, "Fremennik warband seer", 50, 110, 110, 80, 110, 110, 6, 1, 12,
  0, 0, 150, 0, 0, 0, 50, 50, 50, 30, 0 },

/* https://oldschool.runescape.wiki/w/Serpent_shaman
   Attacks with Magic (Water Surge). magic_str_bonus=mbns(15). */
{ 12811, "Serpent shaman", 125, 100, 90, 90, 220, 160, 5, 1, 28,
  0, 0, 50, 15, 0, 0, 30, 30, 30, 15, 50 },

/* https://oldschool.runescape.wiki/w/Jaguar_warrior
   Attacks with Slash; max hit 47 per hit, 3 hits per attack. */
{ 12810, "Jaguar warrior", 125, 200, 330, 125, 100, 160, 5, 2, 47,
  0, 25, 0, 0, 0, 0, 30, 30, 30, 15, 45 },

/* https://oldschool.runescape.wiki/w/Javelin_Colossus
   Attacks with Ranged; base max hit 48 (49/51/54 with Relentless I/II/III). */
{ 12817, "Javelin Colossus", 220, 200, 300, 190, 225, 360, 5, 3, 48,
  0, 0, 0, 0, 25, 20, 15, 15, 15, 20, 30 },

/* https://oldschool.runescape.wiki/w/Shockwave_Colossus
   Attacks with Magic. magic_str_bonus=mbns(35). */
{ 12819, "Shockwave Colossus", 125, 120, 190, 150, 350, 220, 5, 3, 56,
  0, 0, 55, 35, 0, 0, 15, 15, 15, 5, 35 },

/* https://oldschool.runescape.wiki/w/Minotaur_(Fortis_Colosseum)
   Two ids 12812/12813; using 12812. Attacks with Crush. melee_att_bonus=attbns(15). */
{ 12812, "Minotaur", 225, 300, 360, 190, 250, 120, 5, 3, 74,
  15, 64, 0, 0, 0, 0, 0, 0, 0, 0, 12 },

/* https://oldschool.runescape.wiki/w/Manticore
   Multi-style (Melee+Ranged+Magic); all aggressive bonuses listed as 0.
   max_hit=36 (highest = ranged; melee/magic 31). */
{ 12818, "Manticore", 250, 300, 300, 250, 300, 350, 10, 3, 36,
  0, 0, 0, 0, 0, 0, 0, 0, 0, 10, 25 },

/* https://oldschool.runescape.wiki/w/Sol_Heredit
   Final boss; attacks Melee + Ranged. melee_att_bonus=attbns(250), range_att_bonus=arange(150).
   UNCONFIRMED: attack_speed (wiki "Varies") -> proposed 5; ranged_def (drange absent) -> 825
   from dstandard; max_hit 44 (largest single hit; Triple Parry sums higher). */
{ 12821, "Sol Heredit", 1500, 350, 400, 200, 300, 350, 5, 5, 44,
  250, 5, 0, 0, 150, 5, 65, 5, 30, 750, 825 },
```

### Per-NPC source-section citations

All values are from the `{{Infobox Monster}}` template (rendered as the "Monster combat stats"
+ "Aggressive stats" + "Defensive stats" boxes) on each page below:

1. Fremennik warband berserker -> https://oldschool.runescape.wiki/w/Fremennik_warband_berserker (id 12816, hp 48, att/str/def/mage/range 110/110/80/110/110, speed 6, size 1, max hit 29, attbns 150, strbns 90, amagic 150, mbns 0, arange 0, rngbns 0, dstab/dslash/dcrush 50, dmagic 0, dlight/dstandard/dheavy 75)
2. Fremennik warband archer -> https://oldschool.runescape.wiki/w/Fremennik_warband_archer (id 12814, hp 50, levels 110/110/80/110/110, speed 6, size 1, max hit 14, attbns 150, strbns 150, amagic 150, mbns 0, arange 150, rngbns 10, dstab/dslash/dcrush 0, dmagic 75, dlight/dstandard/dheavy 50)
3. Fremennik warband seer -> https://oldschool.runescape.wiki/w/Fremennik_warband_seer (id 12815, hp 50, levels 110/110/80/110/110, speed 6, size 1, max hit 12, attbns 0, strbns 0, amagic 150, mbns 0, arange 0, rngbns 0, dstab/dslash/dcrush 50, dmagic 30, dlight/dstandard/dheavy 0)
4. Serpent shaman -> https://oldschool.runescape.wiki/w/Serpent_shaman (id 12811, hp 125, levels 100/90/90/220/160, speed 5, size 1, max hit 28, attbns 0, strbns 0, amagic 50, mbns 15, arange 0, rngbns 0, dstab/dslash/dcrush 30, dmagic 15, dlight/dstandard/dheavy 50)
5. Jaguar warrior -> https://oldschool.runescape.wiki/w/Jaguar_warrior (id 12810, hp 125, levels 200/330/125/100/160, speed 5, size 2, max hit 47 x3, attbns 0, strbns 25, amagic 0, mbns 0, arange 0, rngbns 0, dstab/dslash/dcrush 30, dmagic 15, dlight/dstandard/dheavy 45)
6. Javelin Colossus -> https://oldschool.runescape.wiki/w/Javelin_Colossus (id 12817, hp 220, levels 200/300/190/225/360, speed 5, size 3, max hit 48 base, attbns 0, strbns 0, amagic 0, mbns 0, arange 25, rngbns 20, dstab/dslash/dcrush 15, dmagic 20, dlight/dstandard/dheavy 30)
7. Shockwave Colossus -> https://oldschool.runescape.wiki/w/Shockwave_Colossus (id 12819, hp 125, levels 120/190/150/350/220, speed 5, size 3, max hit 56, attbns 0, strbns 0, amagic 55, mbns 35, arange 0, rngbns 0, dstab/dslash/dcrush 15, dmagic 5, dlight/dstandard/dheavy 35)
8. Minotaur (Fortis Colosseum) -> https://oldschool.runescape.wiki/w/Minotaur_(Fortis_Colosseum) (id1 12812 / id2 12813, hp 225, levels 300/360/190/250/120, speed 5, size 3, max hit 74, attbns 15, strbns 64, amagic 0, mbns 0, arange 0, rngbns 0, dstab/dslash/dcrush 0, dmagic 0, dlight/dstandard/dheavy 12)
9. Manticore -> https://oldschool.runescape.wiki/w/Manticore (id 12818, hp 250, levels 300/300/250/300/350, speed 10, size 3, max hit 31 melee/36 ranged/31 magic, attbns 0, strbns 0, amagic 0, mbns 0, arange 0, rngbns 0, dstab/dslash/dcrush 0, dmagic 10, dlight/dstandard/dheavy 25)
10. Sol Heredit -> https://oldschool.runescape.wiki/w/Sol_Heredit (id 12821, hp 1500, levels 350/400/200/300/350, speed "Varies", size 5, max hit 44 AOE/44 grapple/Triple Parry seqs, attbns 250, strbns 5, amagic 0, mbns 0, arange 150, rngbns 5, dstab 65, dslash 5, dcrush 30, dmagic 750, dlight/dstandard/dheavy 825, drange param absent)
