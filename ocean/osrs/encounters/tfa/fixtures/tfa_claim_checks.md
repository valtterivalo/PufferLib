# Claims in rules.txt and tfa-cycles-*.md against the recomputed fixtures

claimed = the note's count of attack-to-attack gaps equal to the stated value. computed = same count over every recording in this fixture set, which is larger than the notes' sample (later recordings add Ocellus, Dreadnoth and Ezekiel fights, and ezekiel_03 is a no-combat segment). 'by id' splits the NPC by (index, npc id) like the notes did, 'merged' follows one NPC index across id swaps.

| claim | source | claimed gap count (n) | computed merged (n) | computed by id (n) | verdict |
|---|---|---|---|---|---|
| Guardian 14751 next 5 | cycles-b | 780 (854) | 784 (859) | 784 (859) | DIFFERS |
| Guardian 14753 next 5 | cycles-b | 220 (241) | 219 (240) | 219 (240) | DIFFERS |
| Guardian 14754 next 15 | cycles-b | 72 (73) | 92 (93) | 72 (73) | match |
| Guardian 14748 next 5 | cycles-b | 64 (65) | 64 (65) | 64 (65) | match |
| Guardian 14745 next 4 | cycles-b | 18 (18) | 19 (19) | 19 (19) | DIFFERS |
| Xarth 14985 next 4 | cycles-b | 302 (302) | 327 (327) | 327 (327) | DIFFERS |
| Xarth 14983 next 4 | cycles-b | 4 (4) | 4 (4) | 4 (4) | match |
| Xarth 14992 next 3 | cycles-b | 12 (12) | 15 (15) | 15 (15) | DIFFERS |
| Dreadnoth 14780 next 8 | cycles-b | 44 (45) | 53 (58) | 53 (58) | DIFFERS |
| Bork 14542 next 7 | cycles-b | 16 (18) | 31 (73) | 31 (73) | DIFFERS |
| Moia 14785 next 5 | cycles-b | 6 (9) | 8 (9) | 8 (9) | DIFFERS |
| Ezekiel 14685 next 5 | cycles-a | 462 (481) | 464 (626) | 464 (626) | DIFFERS |
| Ezekiel 14686 next 5 | cycles-a | 18 (18) | 19 (23) | 19 (23) | DIFFERS |
| Ezekiel 14687 next 7 | cycles-a | 139 (160) | 139 (161) | 139 (161) | match |
| Ezekiel 14688 next 5 | cycles-a | 30 (33) | 30 (33) | 30 (33) | match |
| Ezekiel 14693 next 4 | cycles-a | 100 (103) | 101 (106) | 101 (106) | DIFFERS |
| Ocellus P3 14842 next 3 | cycles-a | 23 (23) | 562 (562) | 562 (562) | DIFFERS |
| Ocellus P3 14846 next 4 | cycles-a | 11 (12) | 254 (295) | 254 (295) | DIFFERS |
| Ocellus P3 14851 next 6 | cycles-a | 3 (3) | 36 (38) | 36 (38) | DIFFERS |
| Ocellus P1 14811 next 3 | cycles-a | 22 (55) | 66 (166) | 66 (166) | DIFFERS |
| Ocellus P1 14810 next 7 | cycles-a | 6 (6) | 23 (25) | 23 (25) | DIFFERS |

## rules.txt statements recomputed

| claim | computed | verdict |
|---|---|---|
| projectile 4201 flight ~2t | 2.5:1043 | match |
| anim 14751 hit +4 (388 of 856 anims) | delay hist 0:18 1:13 2:6 3:11 4:392 5:9 6:6 7:2 8:5; anims 861 | DIFFERS |
| projectile 4056 5t flight | 3.0:65 4.0:28 5.0:18 | DIFFERS |
| projectile 4057 5t flight | 3.0:76 4.0:20 5.0:15 | DIFFERS |
| projectile 4045 4t flight | 4.0:16 | match |
| projectile 4440 4.5t flight | 4.5:10 | match |
| anim 14686 magic lands 2t later | 2:23 | match |
| anim 14777 slam hits melee 1t after the anim | all P0 hits by offset from anim: 0:49 1:84 2:9 3:10 4:79 5:85 6:85 7:41 8:11 | match |
| anim 14777 then 3 ranged shots +4/+5/+6 (anim line) | offsets 4:79 5:85 6:85 | match |
| projectile 4211 volley: 3 shots landing +3/+4/+5t (projectile line, measured from anim) | first-observed minus anim, by flight: {(2, '2.5'): 252, (2, '3.5'): 252, (2, '4.5'): 252}; landing = observed + floor(flight) = anim +4/+5/+6 | DIFFERS |
| projectile 4212 glob lands ~6t later | 5.5:159 | match |
| anim 14779 basic attack every 4t | 4:29 5:3 6:1 7:4 8:2 9:3 10:2 11:2 12:1 15:3 16:1 17:6 19:2 20:3 21:4 22:1 23:1 25:4 26:1 28:1 29:3 30:1 33:1 35:1 37:1 39:1 46:1 50:1 53:1 54:1 75:1 | match |
| projectile 4213 magic dagger 5.5t flight | 5.5:3 | match |
| anim 14785 dagger hits all players ~1t later | first P0 hit delay 1:4 5:1 6:1 | match |
| projectile 4374 ranged flight 2t | 2.0:328 | match |
