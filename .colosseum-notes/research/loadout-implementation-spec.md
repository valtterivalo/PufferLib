# Colosseum loadout profiles: implementation spec

Reconciles loadout-beginner.md + loadout-speedrun.md against the sim. Two profiles
replace the placeholder maxed melee/ranged loadouts. Decisions tagged L<n> (modeled
decision, cite-backed where possible); code carries /* L<n> */ tags.

## Profiles

### BEGINNER — wiki Budget tab tier-1 + budget example inventory (first-quiver floor)
- Melee set: Serpentine helm, Infernal cape, Amulet of blood fury, god blessing (ammo, L7),
  Osmumten's fang, Dragon defender, Fighter torso, Crystal legs, Barrows gloves, Echo boots,
  Berserker ring (i).
- Ranged set: Crystal helm, Infernal cape, Necklace of anguish, god blessing (L7),
  Bow of faerdhinen, (no shield, 2h), Crystal body, Crystal legs, Barrows gloves, Echo boots,
  Berserker ring (i).
- Spec weapons: Saradomin godsword (A) + Dragon claws (B) (tier-1 in every beginner tab).
- Supplies (budget example, dose-collapsed): brew 24 doses (6x4), super restore 32 (8x4),
  super combat 8 (1 reg + 1 divine, L4), ranging 8 (1 reg + 1 divine, L4). No surge.

### SPEEDRUN — wiki High efficiency tab (sub-20 kit)
- Melee set: Torva full helm, Infernal cape, Amulet of rancour, Dragon arrow, Scythe of vitur,
  (no shield), Oathplate chest, Oathplate legs, Ferocious gloves, Avernic treads (max), Ultor ring.
- Ranged set: Masori mask (f), Dizana's quiver, Necklace of anguish, Dragon arrow, Twisted bow,
  (no shield), Masori body (f), Masori chaps (f), Zaryte vambraces, Avernic treads (max), Ultor ring.
- Spec weapons: Dragon claws (A) + Elder maul (B) ("Burning claws / Dragon claws + Elder maul /
  Dragon warhammer"; claws+maul are the members our spec resolver already implements).
- Supplies: brew 4 doses (1x4), sanfew-as-restore 28 doses (7x4, cures venom L5), super combat 4
  (divine, L4), ranging 4 (divine, L4), surge potion 4 doses (25% spec each, 500-tick cooldown,
  cooldown frozen during wave gaps per wiki).

## New ITEM_DATABASE entries (wiki-exact, fetched 2026-06-10)
| Item | id | slot | key stats |
|---|---|---|---|
| Serpentine helm (charged) | 12931 | head | att mag -5 rng -5; def +52/+55/+58/0/+50; str +5; VENOM_IMMUNE effect |
| Amulet of blood fury | 24780 | neck | att +10 all/+10 mag/+10 rng; def +15 all; str +8; prayer +5; BLOOD_FURY effect |
| Fighter torso | 10551 | body | att mag -40; def 62/85/62/-10/67; str +4 |
| Amulet of rancour | 29801 | neck | att stab/slash/crush +25, mag -6, rng -8; def 0; str +12; prayer +2 |
| Oathplate chest | 30753 | body | att slash +16, mag -16, rng -18; def 105/128/100/-5/112; str +4 |
| Oathplate legs | 30756 | legs | att slash +12, mag -12, rng -14; def 75/100/73/-3/81; str +2 |

All six rows verified against raw wiki infobox wikitext (action=raw, 2026-06-10); the
serpentine helm row uses the Charged (id 12931) version2 params.

## Effect bits (additive, shared enum)
- OSRS_ITEM_EFFECT_BLOOD_FURY (1u<<11): 20% chance on melee damage to heal 30% of damage dealt.
  Proc applied colosseum-side where player melee damage lands.
- OSRS_ITEM_EFFECT_VENOM_IMMUNE (1u<<12): blocks NEW venom application (Mantimayhem II) while
  the ACTIVE set's head carries it; does not cure existing venom (wiki).

## Modeled decisions
- L1 Two weapon sets only (melee + ranged). The real kits carry a Tumeken's shadow / venator bow
  /thrall stack; player spellcasting, venator bounces, book of the dead and rune pouch are out of
  sim scope. Documented divergence.
- L2 Spec = weapon-only swap on the melee gear base: spec loadout stats computed at reset from the
  melee array with the weapon slot replaced. SPEC head {none, arm A, arm B}; armed spec fires on
  the next attack via osrs_resolve_spec, consumes osrs_spec_cost energy.
- L3 SGS heal/prayer minimums (10 HP / 5 prayer, wiki) applied colosseum-side post-resolve; the
  shared resolver is untouched (PvP/zulrah callers unaffected).
- L4 Divine potions modeled as their base boost; the divine re-boost behavior is not modeled;
  doses pool with the regular variant (e.g. beginner combat = 8 doses).
- L5 Sanfew serum modeled as the speedrun restore: exact restore formula 4 + floor(30% base) per
  stat (super restore: 8 + floor(25% base)), prayer 4 + floor(3/10 level); drink clears venom
  (collapses the real venom->poison->immune two-step; strategies-page intent).
- L6 Guthix rest, moonlight meat, saturated heart: not modeled (1-slot QoL items).
- L7 Beginner ammo slot = god blessing: avoids phantom dragon-arrow ranged strength on the
  ammo-less bowfa (mirrors inferno budget; wiki budget lists Rada's blessing 4 for blowpipe).
  Speedrun keeps Dragon arrow (tbow legitimately consumes arrows).
- L8 No stamina potions (absent from every wiki colosseum inventory).
- L9 Profile sampling mirrors inferno: loadout_profile_mode {speedrun_only=0 (default),
  beginner_only=1, mixed=2} + beginner_loadout_fraction (default 0.5).
- L10 start_wave>1 curriculum starts get FULL supplies (no wave scaling yet); revisit with
  curriculum work.
- L11 Twisted bow + crystal armour effects wired into colosseum's attack path via the shared
  OsrsEffectProfile modifiers (tbow scales by target magic level — strong vs Sol magic 300, weak
  vs low-magic wave NPCs; crystal+bowfa +acc/+dmg). Fixes a latent placeholder inaccuracy.
- L12 Loadout stats refresh (encounter helper) after every consumable drink and offensive-prayer
  toggle so brew drains and pot boosts actually move max hits.
- L13 Surge potion cooldown 500 ticks (300s), frozen while in_wave_gap/in_ready_gap (wiki:
  "timer pauses between waves in the Fortis Colosseum"); undrinkable at 100% energy.
- L14 EAT head stays dim 2 {none, brew} (shark -> brew; no solid food in any colosseum kit).
- L15 POTION head dim 2->5 {none, restore, super combat, ranging, surge}.
- L16 Burning claws -> Dragon claws (wiki: difference "very minimal"; resolver has dragon claws).

## Contract changes (FLAG FOR USER REVIEW)
- Obs player block 29 -> 37: +4 supply fractions (brew/restore/combat/ranging, normalized by
  profile full doses), +2 surge (doses frac, cooldown frac), +2 spec-armed one-hot. 533 -> 541.
- Mask: potion +3, spec +1 => 62 -> 66.
- Snapshot version v3 -> v4 (profile + supplies + spec state).
- Profile identity is implicit in obs via gear-derived loadout stats + supply normalization
  (inferno precedent: no profile one-hot).

## Config (binding.c + osrs_colosseum.ini)
- loadout_profile_mode int (0 speedrun_only / 1 beginner_only / 2 mixed), default 0.
- beginner_loadout_fraction float, default 0.5 (used in mode 2).

## Human mode + lab
- Potion keys extend the existing consumable key map (restore/combat/ranging/surge).
- Spec key arms A; shift-spec arms B (or two keys, follow existing human input shape).
- Lab: loadout command (profile select) + supplies dump in `dump`.

## Test plan (extend test_colosseum_modifiers.c)
- Profile gear arrays resolve to expected loadout stats (fang slash style, scythe slash, tbow/bowfa
  ranged; spec stats use melee base armour).
- Supply init per profile; EAT/POTION/SPEC mask gating (doses, timers, surge cooldown+full-energy,
  energy >= cost).
- Brew drain -> loadout stats drop -> restore recovers (L12 invariant).
- Sanfew clears venom; serp helm blocks application in melee set but not ranged set (L5/effect).
- Blood fury procs heal only on melee damage (statistical bounds over many rolls).
- SGS minimums (L3); claws cascade total within [0, 2*max-1]; elder maul drains 35% Sol def.
- Surge: +25/dose, cap 100, cooldown gate, gap-frozen cooldown.
- Tbow scaling: max hit vs Sol (magic 300) > max hit vs warband archer (low magic) (L11).
- Crystal armour: bowfa max hit/acc higher with crystal set than without (L11).
- Obs/mask fuzz across both profiles (mixed mode) — index assertions catch layout drift.
