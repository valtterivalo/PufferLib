/* The delays of a session, in game ticks (0.6 s), as deciles (minimum, 10th ... 90th percentile, maximum) of the measured samples, linear between sample ranks.
 * Source: scripts/measure_session_delays.py over the 168 recordings of 2026-10-04 .. 10-16 that hold a player fight (153 real fights, 80 opponents, 6.07 h;
 * a "teleport" is the house tablet in the open area and the Exit Arena portal in the arena, which plays the same role). A delay is the start of the next
 * fight minus the end of the last one, only for pairs less than 10 minutes apart (longer gaps are the end of a recorded session, not a delay). Ticks come
 * from the tick labels where the recorder kept running and from the wall clock / 0.6 across recordings (52 of 152 gaps, lower fidelity). */

/* R: our teleport to the next fight with a NEW opponent (restock and search): n = 34 (R for all escapes, rematches included: n = 87, median 131). */
static const RiskfightDelay RF_DELAY_RESTOCK = {{59, 124, 138, 154, 178, 256, 322, 392, 540, 637, 791}};
/* M: our teleport to a rematch with the same opponent: n = 53, median 106 (Exit Arena 105, tablet 115). */
static const RiskfightDelay RF_DELAY_REMATCH = {{42, 72, 83, 88, 102, 106, 112, 126, 140, 190, 505}};
/* P(rematch | we teleported and a next fight followed within 10 minutes) = 53 / 87 (Exit Arena 42 / 60, tablet 11 / 27). */
#define RF_REMATCH_PROBABILITY 0.609f
/* D: our death to the next fight (respawn, regear): n = 4 in-session samples (403, 567, 941, 985); 9 of 13 deaths were followed by a gap over 10 minutes. THIN. */
static const RiskfightDelay RF_DELAY_RESPAWN = {{403, 452, 501, 551, 642, 754, 866, 945, 959, 972, 985}};
/* S: our kill (or his teleport, or a round limit) to the next fight with a new opponent. NOT MEASURABLE: one in-session kill (168 ticks) and two of his
 * teleports (67, 383); 8 of 9 kills were followed by a gap over 10 minutes. PROPOSED: the new-opponent teleport delay without the rematch gap,
 * max(20, R - 106) per decile (median 150), which brackets the three samples. */
static const RiskfightDelay RF_DELAY_SEARCH = {{20, 20, 32, 48, 72, 150, 216, 286, 434, 531, 685}};
