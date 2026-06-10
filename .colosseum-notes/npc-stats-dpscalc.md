# Colosseum NPC stats — canonical from refs/osrs-dps-calc/cdn/json/monsters.json
# (cross-checked against wiki infoboxes in npc-stat-blocks.md; agreement confirmed)

| type | id | hp | att | str | def | mag | rng | spd | sz | maxhit | mAtk | mStr | magAtk | magStr | rAtk | rStr | dStab | dSlash | dCrush | dMag | dRng |
|---|--|--|--|--|--|--|--|--|--|--|--|--|--|--|--|--|--|--|--|--|--|
| BERSERKER | 12816 | 48 | 110 | 110 | 80 | 110 | 110 | 6 | 1 | 29 | 150 | 90 | 150 | 0 | 0 | 0 | 50 | 50 | 50 | 0 | 75 |
| ARCHER | 12814 | 50 | 110 | 110 | 80 | 110 | 110 | 6 | 1 | 14 | 150 | 150 | 150 | 0 | 150 | 10 | 0 | 0 | 0 | 75 | 50 |
| SEER | 12815 | 50 | 110 | 110 | 80 | 110 | 110 | 6 | 1 | 12 | 0 | 0 | 150 | 0 | 0 | 0 | 50 | 50 | 50 | 30 | 0 |
| SERPENT_SHAMAN | 12811 | 125 | 100 | 90 | 90 | 220 | 160 | 5 | 1 | 28 | 0 | 0 | 50 | 15 | 0 | 0 | 30 | 30 | 30 | 15 | 50 |
| JAGUAR_WARRIOR | 12810 | 125 | 200 | 330 | 125 | 100 | 160 | 5 | 2 | 47 | 0 | 25 | 0 | 0 | 0 | 0 | 30 | 30 | 30 | 15 | 45 |
| JAVELIN_COLOSSUS | 12817 | 220 | 200 | 300 | 190 | 225 | 360 | 5 | 3 | 48 | 0 | 0 | 0 | 0 | 25 | 20 | 15 | 15 | 15 | 20 | 30 |
| SHOCKWAVE_COLOSSUS | 12819 | 125 | 120 | 190 | 150 | 350 | 220 | 5 | 3 | 56 | 0 | 0 | 55 | 35 | 0 | 0 | 15 | 15 | 15 | 5 | 35 |
| MINOTAUR | 12812 | 225 | 300 | 360 | 190 | 250 | 120 | 5 | 3 | 74 | 15 | 64 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 12 |
| MANTICORE | 12818 | 250 | 300 | 300 | 250 | 300 | 350 | 10 | 3 | 36 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 10 | 25 |
| SOL_HEREDIT | 12821 | 1500 | 350 | 400 | 200 | 300 | 350 | 5 | 5 | 44 | 250 | 5 | 0 | 0 | 150 | 5 | 65 | 5 | 30 | 750 | 825 |

Notes: speed=0 in data for Sol ("Varies") -> use per-attack timing from the boss state machine, not a fixed npc speed.
maxhit per-style: Manticore rng36/mag31/melee31; Jaguar 47 x3 independent; Javelin 48 base; Sol 44 AoE/grapple + parry combos.
Sol weakness: slash_def=5 (vs stab 65 / crush 30 / magic 750 / ranged 825) -> slash is the only viable style, matches wiki.
