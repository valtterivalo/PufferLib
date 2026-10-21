# Claims in rules.txt and tfa-cycles-*.md against the recomputed fixtures

claimed = the note's count of attack-to-attack gaps equal to the stated value. computed = same count over every recording in this fixture set, which is larger than the notes' sample (later recordings add Ocellus, Dreadnoth and Ezekiel fights, and ezekiel_03 is a no-combat segment). 'by id' splits the NPC by (index, npc id) like the notes did, 'merged' follows one NPC index across id swaps.

| claim | source | claimed gap count (n) | computed merged (n) | computed by id (n) | verdict |
|---|---|---|---|---|---|
| Guardian 14751 next 5 | cycles-b | 780 (854) | 780 (854) | 780 (854) | match |
| Guardian 14753 next 5 | cycles-b | 220 (241) | 218 (239) | 218 (239) | DIFFERS |
| Guardian 14754 next 15 | cycles-b | 72 (73) | 92 (93) | 72 (73) | match |
| Guardian 14748 next 5 | cycles-b | 64 (65) | 64 (65) | 64 (65) | match |
| Guardian 14745 next 4 | cycles-b | 18 (18) | 18 (18) | 18 (18) | match |
| Xarth 14985 next 4 | cycles-b | 302 (302) | 302 (302) | 302 (302) | match |
| Xarth 14983 next 4 | cycles-b | 4 (4) | 4 (4) | 4 (4) | match |
| Xarth 14992 next 3 | cycles-b | 12 (12) | 12 (12) | 12 (12) | match |
| Dreadnoth 14780 next 8 | cycles-b | 44 (45) | 45 (49) | 45 (49) | DIFFERS |
| Bork 14542 next 7 | cycles-b | 16 (18) | 17 (19) | 17 (19) | DIFFERS |
| Moia 14785 next 5 | cycles-b | 6 (9) | 8 (9) | 8 (9) | DIFFERS |
| Ezekiel 14685 next 5 | cycles-a | 462 (481) | 464 (626) | 464 (626) | DIFFERS |
| Ezekiel 14686 next 5 | cycles-a | 18 (18) | 19 (23) | 19 (23) | DIFFERS |
| Ezekiel 14687 next 7 | cycles-a | 139 (160) | 139 (161) | 139 (161) | match |
| Ezekiel 14688 next 5 | cycles-a | 30 (33) | 30 (33) | 30 (33) | match |
| Ezekiel 14693 next 4 | cycles-a | 100 (103) | 101 (106) | 101 (106) | DIFFERS |
| Ocellus P3 14842 next 3 | cycles-a | 23 (23) | 82 (82) | 82 (82) | DIFFERS |
| Ocellus P3 14846 next 4 | cycles-a | 11 (12) | 38 (42) | 38 (42) | DIFFERS |
| Ocellus P3 14851 next 6 | cycles-a | 3 (3) | 5 (5) | 5 (5) | DIFFERS |
| Ocellus P1 14811 next 3 | cycles-a | 22 (55) | 34 (78) | 34 (78) | DIFFERS |
| Ocellus P1 14810 next 7 | cycles-a | 6 (6) | 9 (11) | 9 (11) | DIFFERS |

## rules.txt statements recomputed

| claim | computed | verdict |
|---|---|---|
| projectile 4201 flight ~2t | 2.5:1038 | match |
| anim 14751 hit +4 (388 of 856 anims) | delay hist 0:18 1:12 2:6 3:11 4:388 5:9 6:6 7:2 8:5; anims 856 | match |
| projectile 4056 5t flight | 5.0:6 | match |
| projectile 4057 5t flight | 5.0:3 | match |
| projectile 4045 4t flight | 4.0:3 | match |
| projectile 4440 4.5t flight | 4.5:3 | match |
| anim 14686 magic lands 2t later | 2:23 | match |
| anim 14777 slam hits melee 1t after the anim | all P0 hits by offset from anim: 0:46 1:78 2:9 3:10 4:73 5:79 6:79 7:38 8:11 | match |
| anim 14777 then 3 ranged shots +4/+5/+6 (anim line) | offsets 4:73 5:79 6:79 | match |
| projectile 4211 volley: 3 shots landing +3/+4/+5t (projectile line, measured from anim) | first-observed minus anim, by flight: {(2, '2.5'): 222, (2, '3.5'): 222, (2, '4.5'): 222}; landing = observed + floor(flight) = anim +4/+5/+6 | DIFFERS |
| projectile 4212 glob lands ~6t later | 5.5:128 | match |
| anim 14779 basic attack every 4t | 4:25 5:2 6:1 7:4 8:2 9:3 10:2 11:2 12:1 15:3 16:1 17:6 19:2 20:3 21:3 23:1 25:3 26:1 28:1 29:3 30:1 33:1 35:1 37:1 39:1 46:1 50:1 53:1 54:1 75:1 | match |
| projectile 4213 magic dagger 5.5t flight | 5.5:3 | match |
| anim 14785 dagger hits all players ~1t later | first P0 hit delay 1:4 5:1 6:1 | match |
| projectile 4374 ranged flight 2t | 2.0:302 | match |
