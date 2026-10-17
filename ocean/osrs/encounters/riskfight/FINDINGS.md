# Riskfight policy findings

What the deployed nets did live, why, and how we found out. Each entry gives the session, the policy, the observation, the probe and the fix. Probes run `ocean/osrs/tests/rl_explain.c` on the client's engine trace (`~/.light2/engine-traces/run-<client run>.requests`): the server is deterministic, so a probe is the live computation with chosen inputs changed.

## 2026-10-16, j2: blind to his swings, eats at 99, shield ping-pong

Session 20261016T141036.

- **Eating at 99:** the sim let anglerfish overheal in combat; the wiki blocks it for 16 ticks after a hit. Eats at 99 had p 1.00.
- **Shield toggling:** the deployable mask allowed unequipping the shield with no two-hander in the bag.
- **No re-attack:** after eating it never attacked again. Injecting his swings into the trace restored attacks. The real cause was that the plugin's attack table lacked the rapier swing (8145, also Belle's folly), so every `V` line said "no swing" for the whole fight.
- **Fixes:** the overheal lock and mask, the shield mask, and the attack table plus `tools/attack_anim_audit.py`.

## 2026-10-17, s2t64 (sweep 2 trial 64, 512x2): eats then teleports on the second tick of a fight

Session 20261017T121142, fight from tick 106.
- **Live (from the recording):**
  - We stood at 120 HP (overhealed) with Vengeance up.
  - At 106 he opened with a 24 hit, taking us to 96. Our Vengeance echoed 18 and his recoil 3, so 21 landed on him at 107 while his bar stayed full (30/30): he was overhealed too.
  - The net ate an anglerfish at 106 (p 0.97) and attacked. At 107 it chose teleport (p 0.63), and the tab was clicked at 108.
  - We had not specced, so by the rude rule this teleport was polite.
- **Probes** (p of teleport at ticks 106 / 107 / 108):

| trace edit | 106 | 107 | 108 |
|---|---|---|---|
| none (live) | 0.06 | 0.63 | 0.58 |
| our HP 121 every tick | 0.01 | 0.18 | 0.20 |
| our HP 121 at 106-107 only, 99 at 108 | 0.01 | 0.18 | 0.64 |
| his swings removed | 0.03 | 0.60 | 0.78 |
| price 0.5 | 0.04 | 0.49 | 0.45 |
| price 2.5 | 0.16 | 0.86 | 0.81 |


Between 106 and 107 only these lines change: `B` (his HP bounds), `O` (his last swing age, distance, clock), `P`, and `V` (106: his swing; 107: the 21 on him at a full bar). Replacing one 107 line with its 106 value (teleport p at 107):

| 107 line replaced | teleport at 107 |
|---|---|
| B | 0.63 |
| O | 0.63 |
| V | 0.08 |
| fight started 60 / 300 ticks earlier (age) | 0.61 / 0.54 |

Variants of the 107 `V` line (teleport at 107):

| 107 `V` line | teleport at 107 |
|---|---|
| no hit on him, bar 30/30 | 0.08 |
| a 0 on him, bar 30/30 | 0.70 |
| 21 on him, bar 27/30 | 0.37 |
| 21 on him, bar 25/30 | 0.23 |

- **Reading:** the trigger is a hitsplat on him that does not move his bar, i.e. he is overhealed, right after he hit us and spent our Vengeance. It is not our HP being low at the start: the first hypothesis was wrong, since we started at 120. The sim produces the same event (recoil and Vengeance echoes set `hit_landed_this_tick`, osrs_pvp_combat.h:456), so this is not a live/sim observation mismatch.
- **Working hypothesis, not yet tested in the sim:** after a bad opening (his first hit landed, our Vengeance spent on an overhealed target), an early teleport is a cheap reset. It costs only the rematch wait (median 106 ticks, 61% rematch), refills everything including Vengeance, and is not rude by our rule, which only covers spec.
- **Discriminating test:**
  - Measure the sim policy's teleports in the first 10 ticks of fights, conditioned on this opening.
  - Ask the user whether an early reset costs anything in the real game (his account so far: "teleporting out instantly is pointless").
- **Other nets on the same trace:** s2t270 (sweep 2 trial 270, 512x2, same env) attacks through 106-113 with teleport p 0.00, both on the live trace and on the "0 on him, full bar" variant. So the early reset is not forced by the env; it is one of the solutions training can land in. s2t270 replaced s2t64 live on 2026-10-17.

  Simulated sessions, 300 per price, argmax (net per hour at price 0.4 / 0.7 / 1.2 / 2.33, then rude teleports per session):

  | net | 0.4 | 0.7 | 1.2 | 2.33 | rude per session |
  |---|---|---|---|---|---|
  | s2t270 | 7.18 | 6.84 | 6.13 | 6.03 | 0.4-0.7 |
  | s2t64 | 4.44 | 4.64 | 3.92 | 3.95 | 1.7-2.1 |

## 2026-10-17, how real fights start and what the price is live (branch start-states)

`scripts/measure_fight_starts.py` over all 161 recorded fights (the 159 tapes plus the two live net-played ones, 20261016T141036 and 20261017T121142; 97 recordings, 12 of the fights under 15 ticks, 44 rematches) writes `fight_starts.tsv` (every field per fight) and `riskfight_starts_data.h` (what the sim reads). A start is the end of the first tick on which either side swings (the Gear Swapper's attack table). Everything below is read off that tick's `T` row.

This corrects the entry above: in the live fight the row's HP is 96 because his 24 landed on tick 106; we stood at 120 before it. The start-state work makes training look like the recordings; it is not the explanation of that teleport.

- **Who swung first:** we 80, he 68, both on one tick 13. Gap to the other side's first swing (ticks): 0:13, 1:21, 2:53, 3:27, 4:18, 5:7, 6+: 15, never in the window: 7. The first swing is on the first tick of the window in 92 of 161 fights (p75 1 tick, p90 6).
- **Whose hit lands first** (a swing's hit, damage or zero): his 62 (+5 where ours never landed), ours 76 (+2), same tick 16. Damaging hits: his first 78 (+7), ours first 64 (+3), same tick 8, neither 1.
- **Our Vengeance against his opener:** an echo on him within a tick of his first damaging hit on us in 127 of 157 fights where he hit us (0.81), 71 of 85 where his hit was the first damage of the fight (0.84). Our first echo on him (143 fights, 5-48, median 21) left his bar full in 35, lower in 107 (none shown in 1).
- **Our hitpoints after the first tick:** 121 in 63, 120 in 59, 99-119 in 24, below 99 in 15 (min 23). Before the tick (hitpoints plus what landed on it): 121 in 68, 120 in 61, below 120 in 32.
- **His health bar** (of 30): none shown in 108, full in 41, partial in 12 (15-29): his hitpoints are known to a range in 12 fights and to 99-121 in 149.
- **Special energy:** ours 100 in 160 (50 in 1: we opened with a spec). His estimated 100 in 147, 50 in 14 (he opened with a spec; 11 of the 14 in rematches).
- **Vengeance up after the tick:** ours in 60 of 161 (his hit of that tick spends it), his (tape estimate) in 132.
- **Distance:** 1 in 141, 2 in 9, 0 in 2, 3-9 in 9.
- **Supplies,** as what the user had used up against the most his recording held: anglerfish 0:130, 1:20, 2:9, 3+:2; karambwan 0:134, 1:25, 2:2; brew doses 0:147, 1:11, 2:3; restore doses 0:149, 1:9, 2:3; super combat doses 0:144, 1:12, 2+:5; Vengeance sacks left p10 20, median 25, p90 29. Held: 9-15 anglerfish (his kit changed over the weeks), 6-9 karambwan, 8 doses each of brew and restore in 143 and 146.
- **Boosted stats:** attack and strength 118 in 153, defence 118 or more in 158 (max 120), the rest 99 (unboosted); prayer points 52-90, median 88.
- **His weapon on the first swing** (composition observed from October 10, 74 fights): Voidwaker 15, 29607 10, 28039 9, 4587 9, 27857 4, 12006 4, 13652 4, 33335 3, 27908 3, 10 other weapons in 13 fights; not exposed before that (87). Opening animations of his (81 fights; he had not swung yet in 80): slash 390 38, claws spec 7514 10, bow 426 9, whip 1658 7, Voidwaker spec 11275 4, soulreaper 10172 3, 401 3, others 7. Ours: 390 in 92 fights, the Voidwaker spec in 1.
- **Hits in flight at the start** (landing the next tick): direct 38 on us / 46 on him, Vengeance echo 23 / 25, recoil 26 / 30. 37 / 47 landed on the first tick.
- **Rematches differ** from first meetings (Fisher): we swung first 0.32 vs 0.56 (p 0.008), a swing on the first window tick 0.34 vs 0.66 (p 0.0004), his opening swing a special 0.25 vs 0.03 (p < 0.0001), our Vengeance up 0.59 vs 0.29 (p 0.0009), supplies below the maximum 0.45 vs 0.09. Hitpoints do not differ (p 1.0). The sim draws rematches from their 44 rows and first meetings from their 117.

**Price.** The client logs' `risk: us X gp ... him Y gp` lines carry no name; the line standing for a fight is the last one at or before its first swing, no earlier than 30 s before the window (else the first inside it), joined by client run and time to the fights of the recordings. 129 of 161 fights have one (the rest are before the plugin logged stakes, 29 on October 4-5, or have none). The live server prices a fight us / him clamped to [0.1, 2.5]. Table = the clamped price of the 89 first meetings with a price (a rematch keeps its price): median 1.08, mean 1.18, geometric mean 0.79; 11 at the 0.1 clamp, 15 at 2.5; by bin 0.1-0.2: 14, 0.2-0.4: 7, 0.4-0.7: 7, 0.7-1.0: 10, 1.0-1.4: 23, 1.4-2.0: 6, 2.0-2.5: 22, 2.5: 15. The raw ratio is outside the [0.4, 2.5] the sweep drew from in 36 of 89. The sweep score draws from the table (`[bot_eval] price_table`); training keeps the wide log-uniform.

**Sim.** `fight_starts` (ini `[env]` and `[bot_eval]`) draws a whole row, plays the opening swings in the sim, then puts the measured state in; `ocean/osrs/tests/test_riskfight_starts.c` checks the mapping per draw and the marginals over 12000 starts of each pool (4.5 sigma). Off, a fight is the regear and the trace hashes are unchanged.

**What it does to a net** (s2t64 512x2, session_probe, 100 sessions, argmax, oppfit 0.5, seed 7, net per hour): regear and wide price 4.52; real starts, wide price 2.48; regear, price table 4.87; real starts and price table 2.29. Kills per session fall from 2.5 to 1.5; in 65% of the starts our Vengeance is already gone. One seed of 100 sessions: the size of the drop is indicative, not the second digit.
## 2026-10-17, teleport norms: kosher against rude, and our prayers

Origin: the user's account of real PvP norms. Teleporting right after a normal opening (he attacked us, nobody has specced) "would raise a ton of question marks; there is absolutely no point": the normal answer to being attacked is Smite and Piety and fighting back. Kosher teleports come after both players have used all their spec, or when we have run out of a supply class (one kind of food, prayer restores, or boosts after brewing down), and even then not always: a good kill chance may remain. A rude teleport (ours after our spec, before his) makes that opponent stop fighting us; repeated, it hurts our name and fights get hard to find.

- **Rule** (`riskfight_norms.h`, both seats): kosher = both players' special energy is below the cheapest special weapon each carries, OR the teleporter is out of hard food (anglerfish, shark, marlin, summer pie; karambwan/halibut are the combo clock and not a class), OR out of prayer restore doses (prayer potion, super restore, sanfew), OR out of boosts (no combat/ranging potion dose and no boost still on). Every other teleport is rude: no rematch, +1 reputation when ours. The old "after our spec, before his" case is inside it; "nobody has specced" and "ours unspent, his spent" are now rude too (they were free before: that is what made the early reset in the s2t64 entry cheap).
- **Opponents**: the script, OppFit and a second agent are classified by the same function from their true supplies. A rude exit of his changes nothing for us beyond the end of the fight and the lost rematch (the longer wait for a new opponent); reputation is ours alone. Logged as `opponent_rude_teleports`.
- **Observation**: session block `[7]` = kosher now (player's reading: own bag and energy, his estimated energy against the threat block's weapons with p >= 0.5). Block is now 8 floats, `RF_OBS_SIZE` 456: old nets do not load (rf_net: "architecture needs 8185856" for s2t270).
- **Prayers**: with `deployable = 1` the prayer heads are masked, so a net's action 0 reached the sim every tick as Piety OFF / overhead no-change: our seat fought with no Piety and no Smite, while the live plugin turns both on at the fight start (live O lines carry piety 1 and the smite bit). Training was out of distribution for those lines, and prayer was never spent, so prayer restores were useless. Now `riskfight_plugin_prayers` (called for the net seats in `puf_step`) sets Piety and Smite every tick: up from tick 1, draining 24 + 12 per tick against 60 + 2 x prayer bonus (about one point per 2 ticks, so 99 prayer lasts a few hundred ticks), off at 0, back after a restore. Scripts keep their own prayers; the plugin's re-enabling after a restore is an assumption. Nets trained before this need retraining.

## 2026-10-17, s3t0 (sweep 3 trial 0, 512x2, real starts, norms, prayers): fights, then teleports rudely at 68 HP

Session 20261017T134203, fight 75-124 against a Voidwaker user.
- **Live:**
  - It specced at 75 (good, his HP 72-115 to 24-87) and again at 97, when he had just eaten back up to 86-115 (wasteful, per the user).
  - At 118 his Voidwaker hit took us to 28. We ate to 68 (anglerfish plus karambwan, so food is locked until 122).
  - At 120 it teleported (p 0.61). We had no spec left and he had 50 (one more Voidwaker spec), so it was rude by the norm. Price 0.84.
- **Probes, teleport p at 119-123:**

  | edit | 119-123 |
  |---|---|
  | live | 0.12 0.61 0.53 0.32 0.89 |
  | our HP 99 | ≤0.01 |
  | our HP 121 | 0.00 |
  | his bar 10/30 | ≤0.02 |
  | our attack/strength restored to 118 (were 105) | 0.06 0.37 0.31 0.12 0.69 |
  | price 0.4 | 0.08 0.44 0.41 0.16 0.78 |
  | price 2.5 | 0.23 0.88 0.68 0.82 0.98 |

- **Reading:** a coherent defensive choice: low HP while food-locked, a healthy opponent who still has a Voidwaker spec, our boost faded. The policy prices staying (his spec could KO from 68) above the rude cost (no rematch, +1 reputation: 1.5x search, 15% raggers). The user says players would eat up and fight on.
- **Open:** either the rude cost is too small, or the sim's death risk in this state is too high (opponent spec timing), or both. Measure P(death within 10 ticks | this state) in the sim against recordings before changing either.
- **Death risk check (DeathRisk agent, /tmp/deathrisk):** P(we die within 15 ticks | HP 55-80, his bar ≥ 15/30, his spec affordable) is 1.33% in our recordings (95% CI 0.26-2.71%, deaths in 6 of 131 fights) and 0.52% in the sim with s3t0 kept from teleporting (CI 0.40-0.65%). The sim does not overstate the threat. The teleport is the policy's pricing.
- **In the sim these nets are almost never rude:** 0.00-0.04 rude teleports per 3000-tick session for sweep 3's top six (probe with real starts and the live price table). The live state (attack/strength drained to 105 by brews with super combat unused, food-locked at 68, he holds a Voidwaker spec) is rare in training.
- **Stronger reputation (rep-knobs branch, sweep 3 trial-0 config, seed 73):**

  | | defaults | search 2.0, rag 0.3, half-life 6000 |
  |---|---|---|
  | rude share of our teleports | 9 of 331 | 0 of 545 |
  | teleports per session | 1.10 | 1.82 |
  | net per hour | 8.43 | 8.04 |

- **s3t34** (sweep 3's best by probe, 10.65 net/h vs s3t0 8.17, 512x1): on the live trace it also teleports at 115-120 (p 0.63-0.93). At 75-82 it drinks a super restore and a brew at 121 HP, the brew draining stats for nothing. Not installed.
- **What the net received at 119-124** (rl_explain now prints the session block and the kosher predicates): kosher 0 on every tick (no_spec 1; no_food, no_restore and no_boost 0; his cheapest spec 50 against his estimated 50), reputation 0, HP margin over his chance range +0.01 at 68 HP. The live input agrees with the manual classification: the net knew the teleport was rude and that 68 sat at the edge of his Voidwaker spec range. It is a pricing decision, not a reconstruction bug.

## 2026-10-17, s4t0 (sweep 4 trial 0, stronger reputation): brews at 121 HP on the first tick

On the live trace (fight from tick 75), s4t0 never teleports, but on tick 75 it drinks a Saradomin brew at 121 HP with p 1.00. With 118 attack/strength that drains about 13 of each for +2 defence.
- **The sim gives an opening brew no value.** In 4000 rollouts from a 121 HP, 118-stat start, a forced brew gets 0.33 kills in 50 ticks against 0.46 without. Left alone in the sim, the net drinks a super restore on tick 0, never a brew.
- **The sim had a mechanics bug:** a brew above its cap lowered HP and defence (121 became 115). Fixed on `brew-fix` (51e058ae1), with tests.
- **Cause of the live brew: slot positions.** Bisecting the live tick-75 observation against the sim's, substituting sim values, needs 18 entries to move the brew below 0.5, 16 of them inventory rows. The training kits keep a fixed layout (restores 21-22, brews 23-24, combat 25); the user's bag keeps restores 4/8, brews 13/15, combat 20. The net learned the slots, not the contents.
- **Two more mismatches:**
  - The live server starts his health bar at full while the sim's first observation has 0.
  - The live start offset is the observed one, while the matched sim start was 0,0.
- **Fix:** permute the bag per fight in training, and match the server's tick-0 bar to the sim's (`layout-fix`).
- **Done (`layout-fix`):**
  - Training permutes both bags uniformly at every fight (`riskfight_training_shuffle_bags`, on the training RNG), so nets trained from here see no fixed layout. The scripts and OppFit find items by content (kind and item slot tables), and the mask follows the contents.
  - The bar: the "0" is the regear start (`fight_starts 0`), whose first observation is tick 0 with an empty history. Training uses `fight_starts 1`, and there the first observation is tick 1. That observation shows the start's bar: the bar the client showed on that tick, hit or not, or full when none showed. The server's first request is that tick (n = 1). It used to take a bar only from a hitsplat, so a bar that was showing without a hit read as full. `rt_fight_reset` now takes the bar the client shows. `test_rl_server_start_bar` checks all 161 starts, and only one of them was wrong before (start 53: bar 18, no hit).
- **Shuffling bags is not enough.** With bags permuted per fight (2434e2694), sweep 5's anchor (sweep 3 trial 34's config) scored 0.30 per hour, against 8.19 in sweep 4 without the shuffle: the net can't learn slot semantics from contents in that budget. Next fix: a canonical bag view (cells sorted by content, actions mapped back to physical slots), so slot positions never reach the net.
- **Canonical bag view (7c358ba8c, riskfight_bag.h):** the net sees bag cells sorted by content, and actions map back to physical slots. Sweep 6's anchor scored 7.50 per hour (8.19 unshuffled, 0.30 shuffled without it). s6t0 on both live traces:
  - no teleport (p 0.00 on every tick);
  - no brew at full HP;
  - the opening drink is a super restore. That trace carries no stat levels, so its use is unknown. A restore is useful for brew-drained stats (the user's point), but 121 HP does not prove a drain, and 118 attack/strength is a boost above base 99, not a drain. The next live fight (run-20261017T185147Z.11198, whose `O` lines carry levels) had every stat at 118 throughout; its restores refilled prayer only partially (see below).

  Installed 2026-10-17.

## 2026-10-17, useful consumables only (branch useful-consumables)

Live, s6t0 drank super restores at full prayer with nothing drained (prayer 87-90 of 90, every stat at its boosted 118). Two causes:
- **The mask offered every drink and every karambwan.** `riskfight_consumable_useful` now gates the drink, food and combo heads. Restores (super restore, sanfew; blighted ids map to the plain kinds) are offered when a level they restore is below base, or prayer is missing at least the dose's restore (`osrs_consumable_prayer_restore_amount`); a prayer potion only on the prayer rule. Boosts (super combat, divine, ranging) are offered when a level they boost is below base + boost. A brew is offered when hitpoints or defence is below its cap; drained attack and strength are no reason. A meal is offered when it heals something (`osrs_eat_food`): below base, or an anglerfish outside the overheal lock below base + 22. Partial waste stays allowed. `RiskfightMaskKey` carries one useful bit per bag slot.
- **The sim's prayer level was 99.** Both seats (and so the live server, whose snapshot has no recorded levels for us) had base prayer 99, while the user's is 90. `RF_PRAYER_LEVEL` = 90 now, for both seats, since in self-play either seat plays the user.
- On the live trace (ticks 123-174, `rl_explain`, s6t0), the net's restore drinks fell from 13 to 0; wholly wasted meals stayed at 0.

## 2026-10-17, s6t0 live (run-20261017T185147Z.11198): restore spam, early specs, a kosher teleport

- **Restores:** 13 restore clicks were decided in ticks 123-174; this counts decisions, not verified consumed doses. No stat was ever drained (all at 118). Most of the clicks came at prayer 87-90 of base 90. Three refills show in the `O` lines: 74 to 89 at tick 148, 83 to 89 at 159, 88 to 89 at 163. So the restores did partially refill prayer; none restored a full dose (30). The sim gave seat 0 base prayer 99; the user's is 90. Fixed by the useful-consumable mask (useful-consumables) and base prayer 90. With the mask, rl_explain on this trace decides 0 restore clicks.
- **Eating to 99 was waste (user's call, and the trace agrees):** none of the meals was wholly wasted, but the net ate 93 to 99 at margin -0.05 only because the chance range (about 100) added an echo for a Vengeance he was never seen to cast (the `last_cast_tick >= last_proc_tick` tie holds when nothing was seen). His real threat was the Soulreaper axe (o), max about 65. Its 65 hit at tick 167 (99 to 34) does not justify the meal: 93 survives it too. Eating costs tempo, because the sim adds the meal's attack delay. Fix: count the echo only after a seen cast, and mask meals above the range (threat-masks). Blighted supplies cost 35-2,900 gp against stakes near 1M, so their price can't teach anything.
- **His spec estimate was right:** 100 to 40 at tick 145 is a plain granite maul special (cost 60). 27690 in the `W` line is our Voidwaker, not his.
- **Replay found a latent failure:** the RL server inherited the lookahead's poison-horizon checks and answered UNAVAILABLE at tick 53 (poison_application_unverified), which suspends automation. The RL net reads no poison; the checks are now the lookahead's only (e199502b9).
- **s6t11 installed** (sweep 6 best, 8.19 per hour, with the new masks at inference). On the three live traces: no restores, no brews, no meals at 93+ HP, teleport p 0.00 outside the kosher stretch of this fight.
- **Threat masks (threat-masks, 13f038310), the user's "no blatant waste" rules:**
  - **Meals:** offered only while our hitpoints are within his chance range.
  - **His Vengeance echo:** counted only after a seen cast; patience is now 11, as measured with this strict test.
  - **Arming a special:** offered only when 1 × spec max (k × for an instant maul) plus the max of our hits in flight reaches his bar's lower bound. Disarming is always offered. In-flight hits count at their max, never their rolled damage, which would leak the roll to the net.
  - **Effect on run-20261017T185147Z.11198:**
    - the range at ticks 148-166 went from 97-105 to 63-71;
    - the meals at 90+ HP and the arming spec at tick 124 (69 < 82) are masked.
- **Sweep 7** (useful-consumable masks, prayer 90) scored up to 10.47 per hour (run 18), against 8.19 in sweep 6. **s7t18 installed** with the threat masks at inference.
  - In the live fight above, it ate only at 13-53 HP and never armed a special.
  - Its teleport intent (p 0.7-0.93) falls in ticks 167-174, after the 65 hit, at 34-52 HP, kosher (both specs spent).
  - Sweep 8 trains with the threat masks.
- **His one-tick threat is a combo, not a max (c7d3c64bb).** `riskfight_chance_range_of` now takes one opening hit (a normal hit, or a special he can afford), adds every instant maul special the remaining energy pays for (a maul the pool only says he may carry counts too: it hides in the bag), and adds his Vengeance echo while it is up. The old max-of-weapons made the meal gate unsafe: an axe hit plus a maul special in one tick can kill from above the axe's max. Regression: `test_riskfight_threat_masks::test_combo_range`.
- **His Vengeance at fight start is usually up.** In the user's recordings, 107 of 123 fights showed a proc before any cast, so the strict "only after a seen cast" rule was wrong. Rule now: up from fight start; down after a proc or after a damaging hit of ours with no proc; up again on a seen cast. Shared by the sim, `gs_rl_server` and the overeat measurement. Re-measured patience: p90 7, so `RF_OVEREAT_PATIENCE` = 8.
  - On the live fight: range 146 at tick 125 (opening hit + two ornate maul specials), 63-71 at ticks 146-160 (Vengeance down since our hit at 124).
- **Timing block (7900537a1, RF_OBS_SIZE 461):** his readiness min/max including his meals' attack delay, his next-tick damage bound (the normal hit only when ready, instant maul specials always), and his hitpoints bounds on the damage scale.
  - Live trace: 3-4 ticks / 88 before his maul spec at 145, 4-4 / 0 right after his axe swing at 167.
  - Sweep 10 trains with it; s7t18 (461-incompatible) stays installed from c7d3c64bb.
- **The scripted opponent now does the hit + maul combo** (`riskfight_script_combo`, `test_riskfight_combo`). Before, `riskfight_script` picked one weapon per tick and never followed a hit with a maul special.
  - Live timing (s1437, game ticks 440-441): axe animation and our 54 hitsplat on 440, then the maul's 1667 and its 0 + 23 on 441. In the tapes, 18 of 29 maul specials come exactly one tick after his swing; none is detectable on the swing's own tick. The engine cannot do it on one tick either: inputs resolve before the actor-pass swing, so a maul switch on the swing's tick cancels the axe swing. So the script swings on tick t and mauls on t + 1, using the existing instant path (equip, k special clicks, target click). Both land on t + 1 when we hold the PID.
  - Rate per swing, by margin = our HP before his swing - (opening max + k x maul spec max) (`scripts/measure_combos.py`, `tests/combo_tapes.c`, 123 fights; 263 swings of fighters who mauled in that fight, with energy for one maul special): margin < -40: 8/111 (72 per mille); -40 to 0: 3/146 (21); >= 0: 0/6. 10 of the 11 combos were k = 2 (100 energy, ornate maul).

## 2026-10-18, s7t18 live (run-20261017T211323Z.1437, 5 fights): pies, STOP, a near death

- **Pies and STOP are rare in the sim too.** Rerun with s7t18's training config (OppFit at 0.5 with the sweep's fit files, run ini keys; 4,504 fights, argmax; the first run had OppFit off by accident):
  - pie bites: 0.17 per pie-kit fight (0.19 against the script, 0.065 against OppFit);
  - pie share of food choices: 10.3% (3.9% against OppFit);
  - STOP while attacking: 0.24% of attacking steps (0.04% against OppFit).

  OppFit drove 16% of fights. Live (no pies and no STOP in 205 attacking ticks) fits the OppFit rates. STOP can pay in the engine (the timer runs on, and re-attacking swings on a chosen tick); the net has learned almost no use for it.
- **Near death in fight 411 (98 to 8 HP):**
  - From the recording's hitsplats on us: one axe hit of 54 at tick 435, another 54 at 440, then a double maul special at 441 (0 + 23). The 98 to 45 at 435 is the 54 hit. A karambwan eaten at 98 HP the tick before healed only 1, so it was nearly wasted tempo. An earlier draft said "71", which was the net HP change, not a hitsplat.
  - Our triple eat at 438 (angler, restore, karambwan) lost the karambwan to the next tick. The clicks landed at +281, +462 and +679 ms against a ~480 ms server cutoff, so the karambwan came alone at 439 and re-armed all three timers through 442.
  - Across the recordings since 10-15, batches of 3 clicks miss the tick 60% of the time by the client's tick and ~100% by the 480 ms cutoff. The sweep trains with the motor model (`--env.motor=1`, riskfight_motor.h, fitted on 11,202 clicks, first/second/third click at 261/437/639 ms), so the net trains with this. The live order (karambwan last) is what lost the karambwan.
- **The Saradomin sword had no item row:**
  - his main weapon got no threat slot live;
  - the 2 pool kits with it were dropped.

  Fixed with the sword (Saradomin's Lightning implemented per the wiki), 4 more item rows and 39 aliases (bd940cb02); the pool went from 273 to 342 kits. 148 raws still have no row: the uint8 item index is full (NUM_ITEMS 214, stat slots at the 40 floor). The item content code scale is now 512.
- **The sim's opponents never did the combo:**
  - Axe kits are scripted, one weapon per tick.
  - Live, the combo is an axe swing at t, then maul special(s) at t+1. 18 of 29 recorded maul specials came one tick after his swing, and none on the swing tick.
  - Measured on the user's 123 fights, 11 combos in 1,730 eligible swings (energy and a maul shown or plausible):
    - over all fights, the rate per swing is 0.3-1.2% by margin, never at or above 0;
    - in the fights where he showed a maul, it is 7.2% when our HP is more than 40 below his combo max and 2.1% from -40 to 0.
  - The script plays only kits that carry a maul, so it uses the conditional rates (d73aa408a, scripts/measure_combos.py). They are biased upward: conditioning on fights where the maul was used selects for opponents who combo.
- **Faster tick batches (light #346, #347), the user's call:** engine batches of 2 or more clicks, and every eat and drink, play the same recorded human paths at 0.50 of the hand's pace limits, with a 32 ms hold. 0.50 is the slowest share where at most 1% of triples miss.
  - The first figure (0.40, p99 393 ms) came from a synthetic fixture whose hand-pace third click was 778 ms against 579 live; that fixture is deleted.
  - The timing is now measured on the installed donor library, from 48 recorded start positions, and calibrated to live within 8 ms at hand pace (587 vs 579 ms third-click p50). Last action after queueing:
    - the chooser's triple eat: p50 294 / p99 351 ms;
    - the 28 recorded triples: p50 314 / p99 441, 0% past 460 ms.
  - At 0.75 the recorded triples go past 460 ms 49% of the time (16% for the chooser's triple), and at 0.60 5.4% (0%). Not yet measured live.
  - The live server now clicks the copy of a consumable that shortens the path. Triple eats use the copies near the potion; single eats use the copy farthest from the potions (24bc3f4a6, the user's idea). On the trace, the summed path of multi-click batches went from 1,397 to 792 px.
  - The sim's motor model (riskfight_motor.h) still has the old, slower clock. Refit it from recordings made with the new plugin, then retrain.
