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
- **Tactics block (RF_OBS_SIZE 491, `test_riskfight_tactics_obs`):** 30 floats a player computes cheaply, so the net spends its capacity on fight dynamics: the greataxe max now and after one and two orb clicks with stripped pieces back on, the strip flag, the OURS rows' weapon identities, `can_wield_2h`, exact knockout chances (axe hit, VW hit, VW special, one and two maul specials) over his hitpoints bounds, his Vengeance, orb and HP > 1, and the safe window (his ready_min minus our swing clock). Nets of size 461 do not load.
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

## 2026-10-18, s7t18 live (run-20261017T230317Z.71610): overeating against an axe, a rude kosher teleport (branch gate-kosher)

- **Overeating.** The opponent had a Soulreaper axe and a maul the pool said he might carry, and never specced (100 all fight). The chance range was 146-159 (axe hit plus two maul specials), so every meal passed the gate; the net ate at 96-99 and brewed 99 to 115.
  - The user's account: the axe hit and the double maul land on different ticks, so he eats in between; he camps at 85-92 against an axe whose owner may double-maul.
  - The recordings agree: in all 11 recorded hit-then-maul combos (scripts/measure_combos.py) the opening's hitsplat and the maul's land on different ticks. With his PID the opening lands on the swing's tick and the maul on the next; without it, both a tick later. One combo opened with a dragon claws special whose second pair landed with a single maul special; that sum is under the max form too.
  - The range is now the larger of the opening hit and the k maul specials his energy pays for, plus the Vengeance echo (`riskfight_tick_damage_bound`). In the live fight it reads 88.
  - Share of the user's meals at or below the range (122 fights with composition seen, 1,667 meals; IsolationSam of October 6 stops the harness on his dragon darts, which have no attack style). The maul term counts only when it is:
    - wielded or swung: 64.9%;
    - pool prior >= 0.5: 71.6%;
    - prior >= 0.25: 73.4%;
    - in any slot (the old sum): 81.9%;
    - in any slot, max form (chosen): 73.7%.
  - No variant reaches 95%. The max form is the one that matches the user's band.
  - Share of the net's 7 meal ticks in the live fight that each variant would mask: 86%, 86%, 86%, 0%, and 57% for the chosen max form (387, 398, 408 and 409 masked; 373, 379 and 391 offered).
  - Of the user's meals above the new range: 20% come while he eats on that tick or the one before, 49% while his ready_min >= 2, and 37% under neither.
  - RF_OVEREAT_PATIENCE is now 9 (p90 of the user's overeats per fight: 8).
- **The teleport at 410** was kosher 1 because no spec weapon of his was known or likely (`his_cheapest 0`), though he had 100 energy. A player assumes the other side carries a special at the commonest cost, 50 (`RF_KOSHER_ASSUMED_SPEC`). After our specials, a teleport is now kosher only once his estimate is below 50.
- **Prayers in training:** the plugin turns on Smite and Piety 1-3 ticks (uniform) after the fight starts. A deployed net's seat in `puf_step` now gets them from a per-fight draw of 1-3 steps, not on the first tick.

## 2026-10-18 night: the meal gate as a one-tick bound, s12t4, the Dharok kit and mixed-kit training

- **Overeating in run-20261017T230317Z.71610:** the chance range summed his axe and a possible double maul (146-159 all fight), so it never gated a meal.
  - The user's account: his axe hit and maul specials land on different ticks, and you eat in between; a camp of 85-92 HP against an axe of about 70 is near optimal.
  - The data agrees: in all 11 recorded combos the opening and the maul land on different ticks.
  - The range is now the larger of the two (a036f19c9; reads 88 in that fight).
  - Calibrated on the user's 1,667 meals, 73.7% of them are within it; above it, 20% are while he eats and 49% while he can't swing next tick. The gate is stricter than the user.
- **Rude teleport read as kosher:** no spec weapon of his reached the 0.5 prior, so `riskfight_teleport_kosher_now` treated him as unable to spec, with 100 energy unspent. It now assumes a 50 special.
- **Specs in that fight:** 62 and 57, near max; he ate before each.
- **Plugin:**
  - fight prayers now start 1-3 random ticks in (light #348), and the sim matches;
  - triple eats landed in the tick live (last click 343 and 321 ms) after the batch pace change.
- **s12t4 installed** (sweep 12 best, 256x2, VW kits; C a196976aa, branch live-s12), on the four live traces against s7t18:
  - fewer meals and restores;
  - teleports only kosher ones;
  - no meal at 90+ HP.
- **The user's Dharok kit** (MCP snapshot `~/.light2/kits/dharok-20261018.json`):
  - worn: Dharok helm/body/legs, Voidwaker, dragon defender;
  - bag: 2 pies, 6 halibut, 10 marlin, 2 restores, 2 brews, ornate maul, greataxe, super combat, locator orb, Vengeance, tab;
  - risk ~5M against ~1M for the VW kit.

  It is now RISKFIGHT_DHAROK_LOADOUT (5650a5856).
  - `live_loadout=4` draws our kit per session: Dharok half the time, the three VW kits the rest. The sweep and its [bot_eval] score use it.
  - The deployable mask offers body/legs strip and re-strip (the game's empty-cell rule holds) and the orb (with an orb and HP > 1).
  - The RL server emits them; `SIM_IDS` knows halibut, marlin, the orb and the Dharok ids.
  - Plugin (light #349):
    - halibut/marlin/pies are meals;
    - `specOk` uses the spec cost table, not "Voidwaker";
    - the orb clicks "Feel";
    - double spec toggles play as one tick batch.
- **Dharok opponents from spectator data** (672bd9ddb, `scripts/measure_dharok_play.py`, 599 Dharok fighters):
  - strips per 1,000 full-set ticks: 1.5 / 2.1 / 2.8 by his bar band (low / mid / high);
  - strip duration about 4.5 ticks;
  - 88 of 98 strips followed by an axe were re-equipped first;
  - axe wields per 1,000 ticks: 75 / 44 / 15 by his bar;
  - orb: exactly-10 HP drops 441 times against about 3.5 for neighbouring values; 248 per 1,000 axe swings follow an orb; 53-69% of axe fighters carry it.

  The script now strips and re-equips at those rates, and 60% of axe kits carry the orb.
- **The script's axe now rolls on his own hitpoints** (bands at 40 and 80, `axe_pm` = wields per tick without the axe, 75 / 44 / 15 per 1,000 on the training mean). The profiles' old opponent-bar shares only set each profile's level (`axe_wield_profiles` in `mine_riskfight_behavior.py`). The two axe-only Dharok kits now hold a Voidwaker and an avernic defender, the commonest main weapon in the pool's Dharok rows (219 fights against 193 for the tentacle), and the axe is a switch. The 99 HP gate is gone.
  - On `/tmp/dhsim` (6,000 script fights), the share of axe swings at 75+ HP fell from 66% to 51% (real: 23%). The rest is the HP gap: sim fighters spend 63% of ticks at 80+ HP, real ones 30%. *(The real share read the carried bar, a stale-bar artifact: the bar moves only with hitsplats. Re-read with meals placed between hitsplats, real time at HP < 40 / 40-79 / 80+ is 0.050 / 0.631 / 0.319, not 0.089 / 0.637 / 0.273; see 2026-10-18 night.)*
- **Housekeeping:**
  - env_trace goldens refreshed on both platforms (5db4ceb3a);
  - a sweep dir copied from the last one keeps the old `./puffer`. A failed nvcc build then silently ran the old binary once (sweep 13's first launch, killed within seconds); the binary is now deleted before every build.
- **Dharok kit, sweep 13** (mixed draw, before the opponent fixes below): the nets play the Dharok kit as a VW kit. The per-kit probe (`session_probe ... 4`, 300 sessions, argmax, OppFit 0.5, price table):

  | Run | Dharok net/h | VW net/h | Dharok deaths/session | Orb clicks/fight | Strips/fight | Axe swings/fight |
  |---|---|---|---|---|---|---|
  | s13t04 (256x2) | 9.50 | 9.89 | 0.06 | 0 | 0.005 | 0 |
  | s13t23 (512x1) | 9.00 | 8.93 | 0.17 | 0 | 0.43 | 0 |
  | s13t36 (512x1) | 9.28 | 9.56 | 0.24 | 0.003 | 0 | 0 |

  **s13t04 installed** (C 85924de10, branch live-s13). Replays of four live sessions pass, and on the four VW live traces it decides like s12t4. Its Dharok play is untested live.
- **Why no axe:** in that sim the axe earns nothing. A hand rule swinging it below 60 HP got 55 kills per 100 against 54-56 without; the orb raised deaths. The sim's Dharok fights did not match 261 spectated Dharok-vs-Dharok duels:

  | Measure | Real | Sim before | Sim after the fixes |
  |---|---|---|---|
  | KOs by axe / Veng reflect / axe+reflect stack | 41 / 10 / 21% | 21 / 16 / 5% | 9.2% by axe |
  | Hits landed against Dharok armour (axe / VW / tentacle) | 26 / 25 / 29% | 21 / 22 / 23% | 25 / 26 / 27% |
  | Fighter-ticks at 80+ / 40-79 / <40 HP | 38 / 54 / 8% | 61 / 35 / 4% | 48 / 44 / 8% |
  | Victim ate in the 2 ticks before a KO | 21% | 2% | |

  Fixes (85924de10), opponent seat only; our seat and nets keep their own levels:
  - the script swings the axe on its OWN HP (it used ours), at the measured 75/44/15 wields per 1,000 ticks by its bar;
  - axe-only archetypes now hold a VW;
  - scripted Dharok opponents fight on defence 80, boosted. Measured hit rates need an effective defence of about 129 against the sim's 155; our live crater opponents look fully defended, so the VW population keeps 99;
  - their eating follows the spectated rates per HP band (`scripts/measure_dharok_eating.py`), with the real single/combo mix;
  - with a hidden start bar, they start at the spectated start HP.

  **Still open:** sim Dharok fighters take 1.65 damage per tick against 1.95 real, and the axe KO share is 9% against 41%. The script never stacks an axe hit with a Vengeance reflect or an orb'd low HP the way real KOs do. Sweep 14 trains on this env.
- **The axe KO setup** (`scripts/measure_dharok_play.py` (d)): the "axe+reflect stack" is not an axe play. 13 real KOs carry the killer's reflect, but only 1 has his axe swing; most are a VW spec on the reflect. The 26 axe KOs come at his hitpoints 36 (56 at all axe swings) on a victim bar of 12.7/30 (17.4). *(Stale-bar check, 2026-10-18 night: the killer's 36 survives the re-read. 23 of 27 axe KOs show no eat animation since his last hitsplat, so the bar is his HP to within hidden meals; those 23 give a median of 33. The victim's bar is a lower bound.)* Vengeance up (0.19 vs 0.32), the orb (0.12 vs 0.11) and strips (0 vs 0.03) do not stand out. The script's axe now rolls per attack on `RF_AXE_SWING_PM`, the measured share of non-special swings by own HP × Veng up × victim bar (e.g. 25 at 80+ HP on a full bar, 490 at 40-79 HP on a bar under 10, 794 below 40 HP), scaled by the profile's axe propensity. Orb, strip and Veng rules are unchanged: nothing in the data times them to the KO.
  - `/tmp/dhsim` (6,000 fights, fight starts): KO by axe went from 10.1% to 22.7% (real 41%); reflect 12.8 → 13.2% (real 9.5); stack 3.4 → 3.0% (real 20.6, mostly VW); VW 35.5 → 33.0% (real 27); other 37.1 → 27.0% (real 1.6). Damage per tick taken went from 1.657 to 1.669 (real 1.973). The gap is no longer the axe decision: sim axe shares by state are within a few points of real. It is the HP distribution and the many small-hit KOs ("other").
- **Overeat counting:** most meals the probe counts above the gate are motor-delayed clicks from a chain decided in range. One counting artefact: a brew consumed that tick shrank the range it was judged against. The range is now taken at the tick's start (d747134a6).
- **Axe roll from the real KO table (9754d0d83).** The script's axe roll is now a per-attack table from 12,189 real non-special swings: per 1,000, by own HP × victim bar × own Vengeance. The victim's bar matters most (87 / 334 / 526), then own HP (50 / 194 / 640), then Vengeance (127 down, 239 up).

  | Sim (6,000 fights) | Before | After | Real |
  |---|---|---|---|
  | KOs by axe | 10.1% | 22.7% | 41.3% |
  | Damage per tick | 1.66 | 1.67 | 1.97 |

  What's left is KOs from many small hits.
  - The real "axe+reflect stack" is mostly a VW/AGS special landing on a reflect; 1 of 13 is an axe.
  - Before axe KOs, the orb (0.12 against 0.03 base) and Vengeance (0.19 against 0.20) are hardly over-represented, so the script times neither to the axe.
- **The axe does not pay for our seat in the current sim** (hand rules on our Dharok kit against the scripted pool, about 13,000 fights per arm, kills/deaths per 100):

  | Rule | Kills | Deaths |
  |---|---|---|
  | Never axe | 63.0 ± 0.4 | 1.3 |
  | Axe below 60 HP | 63.0 | 1.9 |
  | The real fighters' table | 62.5 | 1.8 |

  So the nets' zero axe use is the sim's optimum, not an exploration failure. This contradicts the user's account of real Dharok fights. Either the sim still misses what makes the axe pay (stacking with a reflect, strips that bait hits, the opponent misjudging his eat), or our VW main is simply better. Open question for the user.
- **Install for the morning: s13t04 stays.** In the newest env (sweep 15's probe binary) it scores Dharok 10.77 and VW 10.92 net/h. s14t0 scores 10.53 / 9.82 and s14t4 9.89 / 10.86. Sweep 15 trains on the newest env.
- **Three mechanics fixes from the reachability study (`test_riskfight_mechanics`; RF_OBS_SIZE 492, nets of size 491 do not load):**
  - Busy hand: while the motor chain of our previous decision had unlanded clicks, the next decision was dropped whole and nothing told the net (ATTACK adds a click every tick, so about 40% of follow-ups such as orb then axe were lost). Now that decision's mask is the no-op of every head and tactics `[30]` hand_busy reads 1 (`RiskfightState.hand_busy`). Live, the same bit is an `OBS` request (the plugin's committed run or unlanded clicks).
  - Spec gate: wield maul + k specials was gated on the wielded Voidwaker (k = 2 disarms it, always offered), so it double-specced at his full hitpoints. `riskfight_spec_clicks` drops the toggles when the maul's own gate (k × its spec max + in flight ≥ his lower bound) is shut, in the step and in `rt_records`. The mask is unchanged.
  - Dharok roll: a normal hit rolled 0..max and then multiplied the roll, leaving gaps in the damage grid. It now rolls uniformly over 0..the multiplied max (`calculate_max_hit`, as the specials did). `osrs_direct_ko_probability` lost its Dharok parameter. The other post-roll multiplier in the engine, the Elysian spirit shield, is a defensive reduction that applies to damage taken, which is correct.

## 2026-10-18 morning: why the nets never use the Dharok axe, orb or maul

- **Interpretability (s13t04, 82,955 Dharok fight steps; harness `/tmp/np`):**
  - **Dead pathway.** Where the axe or maul is offered (48,105 steps), the chosen action's logit sits above "wield axe" by a median 7.9 nats (maul 8.9, orb 8.3, double spec 9.3), independent of state. Below 50 HP it is still 7.7; unmasked, the axe ranks 28th of 30 on the weapon head.
  - **Attribution.** d(axe logit)/d(axe-row max) is −0.0014 nats per point of damage, and the axe max is the 91st most influential input.
  - **Counterfactual.** Setting the axe max to 0, ×0.5, ×1.5 or 128 moves the axe logit by at most 0.026 nats, and no weapon/orb/body choice flips. Food heads move up to 0.33, so the method can see effects.
  - **The hidden state does encode the relevant quantities** (held-out ridge R², layer 1 / layer 2): our HP 0.78/0.72, his HP lower bound 0.93/0.92, axe max 0.73/0.64, his spec estimate 0.82/0.68, his ready_min 0.46/0.35, "a maul spec could KO" 0.55/0.68. The last beats a linear fit on the raw observation (0.53), so the net builds it.
  - The decoder rows for axe/maul/orb ignore those directions (≤ 0.035 nats per SD). This is a learned state-independent "never", not a judgment made state by state.
- **Value in the sim, before the fixes below** (strongest hand rules from engine truth, ~20,000 fights per arm, kills per 100):
  - holding the VW special until P(KO) ≥ 0.3: +8.2 ± 0.5 over "spec when allowed";
  - maul KO at P ≥ 0.5: +0.6;
  - orb-then-axe: −0.7 at best.

  Why the axe can't pay:
  - scripted opponents sat at 73-75 HP (eat below 73);
  - our hit chance is 0.31 for both weapons, and per tick the axe's KO chance (0.0047) barely beats VW's (0.0035), which the switch and the lost defender eat;
  - his reachable damage at a window (64-70) means orbing below about 69 HP is never safe.
- **Reachability:** no engine or mask block. Two misfires:
  - the motor silently dropped a whole decision while a click was pending (40% of orb→axe follow-ups lost);
  - the special head's KO gate used the wielded VW when the same decision wielded the maul, so wield maul + k=2 double-specced at full HP.
- **Damage roll:** the Dharok bonus multiplied the ROLLED damage; OSRS rolls 0 to the multiplied max.
- **Fixed** (merged at 3dd7c0513; RF_OBS_SIZE 492):
  - **Tactics block (30 + 1 floats):**
    - axe max now / at HP−10 / at HP−20 with the set back on, and a "set incomplete" flag;
    - an identity one-hot per OURS row;
    - can_wield_2h;
    - exact P(KO) for an axe hit, a VW hit, a VW spec, 1 and 2 maul specs, over his HP bounds;
    - his Vengeance up; orb carried; HP > 1;
    - the safe window;
    - hand busy.
  - **Hand busy:** in the sim, our decisions are masked to the no-op while motor clicks are pending. Live, the same holds on OBS ticks (the plugin is mid-run).
  - **Attack clicks:** only when not already attacking (or with a special), as live. A double maul spec while already attacking now fires live too; it didn't before.
  - **Spec clicks** on a same-decision maul wield follow the maul's KO gate (one shared function, sim and server).
  - **Dharok damage** rolls 0 to the multiplied max.
  - **Dharok opponents** eat on his attack clock (measured by HP band × phase: after his swing / ready / between), and VW specials are gated at his bar ≤ 25 as 96% of 451 real ones are.

    | Measure | Before | After | Real |
    |---|---|---|---|
    | VW specs per tick | 0.0042 | 0.0068 | 0.0068 |
    | Damage per tick | 1.69 | 1.77 | 2.03 incl. orb |
    | KOs by axe | 23% | 27% | 41% |

    "Other" KOs (plain hits on low victims) are still 26% against 1.6%.
  - **Training zeroes the recurrent state at each fight start** without a terminal: an env `resets` flag, and `zero_reset_state` in the rollout and the BPTT replay. GAE still runs across fights. Live always started fights from zero state; training had carried it across the ~7 fights of a session.
- **Sweep 16** trains on all of this. Sweep 15 (461 observations) reached 11.65 (run 85).
- **Chronology of the suppression** (the s13t04 run's checkpoints; median logit gap to the chosen action on Dharok offer steps, 60 sessions each):

  | Steps | Axe | Maul | Orb | Double spec | Weapon-head entropy |
  |---|---|---|---|---|---|
  | 4.2M | 4.7 | 5.0 | 5.5 | 1.9 | 0.11 |
  | 8.4-12.6M (closest) | 2.8 | 3.1-3.3 | 3.8-4.4 | 0 | 0.41-0.42 |
  | 21-29M | 5.5-7.1 | | 6.2-6.6 | | 0.02-0.06 |
  | 88.8M (= s13t04) | 8.1 | 8.9 | 8.3 | 9.6 | 0.01 |

  The suppression is settled in the first ~20M steps and deepens as the weapon head's entropy collapses. A correction to an earlier claim: the Dharok roll-then-multiply bug gave the right max and mean; it coarsened the damage grid near KO thresholds and did not make the axe weaker.
- **Brews only when they heal (861be5137):** s15t85 sipped brews at 120 HP to lift defence 118 to 120, draining attack and strength; the useful-consumable rule now offers a brew only below its heal cap.
- **s15t85 installed** (sweep 15 run 85, 512x2, 461 inputs; C 21fe13b99 = live-s15 + the brew rule). In sweep 15's env (300 sessions):
  - Dharok 10.75 / VW 12.15 net per hour, deaths 0.05 / 0.07 per session, against s13t04's 10.77 / 10.92;
  - it never uses the axe, orb or maul either.

  On the live VW traces, with the brew rule: 2 meals at 90+ HP in run 1437 (18 without it), teleports only kosher.
- **The orb → axe → teleport play** (the user's account):
  - **Spectator data:** 34 sequences in 34 duels (6.3% of fights, 25 of the 105 axe fighters), median swinger HP about 26, 1-4 orb clicks, both specs spent in 21-30 of 34. **0 KOs:** 18 zeros, 9 no hitsplat (the opponent teleported first in all 9), 7 hits of 9-44. The opponent teleported too in 33 of 34. It reads as an end-of-fight parting shot.
  - **Sim (hand rule after both specs, orb to T, axe, teleport the next tick; ~12,000 fights per arm):** stake per fight 0.589 baseline, 0.586 at T=1, 0.561 at T=10, 0.549 at T=20. It turns later VW wins into escapes. The axe max at 1 HP is 96, as the user said.
  - **Sim mismatch found:** the teleport tick wipes his recoil/Vengeance damage from our own axe hit, so 1-HP swings looked safer than in OSRS (being fixed).
- **Correction: the teleport wipe was right.** The user says a teleport stops all damage on you immediately, recoil and Vengeance included. The data agrees: 106 swing-then-teleport cases where the swinger's hit landed, scanned from the hit tick to his despawn, show no reflect or recoil splat on him at or after his teleport animation. 13 of 16 axe hits that procced his Vengeance put nothing on the teleporter; the other reflects land at +1, before the teleport animation. The change of 607ba342f (an inference) is reverted (46e254104). The orb → axe → teleport hand rule stays a wash in the sim: stake per fight 0.586 at T=1 against 0.589 baseline.
- **Sweep 16 nets (tactics block, per-fight state reset)** still don't use the Dharok tools: axe swings 0.000-0.003 and orb 0 per fight, while strips run 0-0.86. This agrees with the oracle: even the strongest hand rules for axe, orb and maul don't pay in the current sim. What remains is how faithful the sim is, not the net's inputs.

## 2026-10-18 afternoon: KO setups, orb timing, self-play

- **The victim-mistake hypothesis, tested in matched windows** (state-defined: a recent Vengeance proc/reflect or a 20+ exchange, victim at 30-60 HP, attacker's specs spent or his timer free; 726 real windows, 5,798 sim):
  - **Heals match:** sim victims don't heal faster or bigger. Heal by 3 ticks: 0.40 sim, inside the real 0.20-0.48 (eat animations miss 47% of heals). Heal sizes match when free (about 0.5 / 0.4 / 0.1 single / double / triple).
  - **Locked victims:** with a timer on, real victims eat later (0.14 against 0.31 by 3 ticks) but bigger, a weak sign for the timer half of the hypothesis (19 heals).
  - **The difference is the attacker:** a 50+ burst within 3 ticks happens 7.0% real against 3.6% sim, KO within 6 ticks 7.9% against 5.1%. In these windows the real attacker sits at 50 HP against 62 in the sim: more Dharok damage.
  - An earlier comparison conditioned on KOs and was selection-biased. It doesn't refute the hypothesis; these windows don't support it either.
- **Orb rate from visible ticks (486b94e33):**
  - On the tick after his swing an orb's 10 can't be told from his hit, so the earlier "never orb on that tick" rule was a measurement artefact.
  - Rates are now fitted on visible ticks only: 33 per 1,000 against 16 counted over all ticks. Orbs on ticks 2+ after his swing (33.0) and on the ready tick (36.9) are within noise, so no timing preference is visible.
  - Sim opponent orb damage per tick: 0.29, against about 0.33 real.
- **Dharok opponents teleport on their own HP** (67a46efca): per tick by HP band 28.5 / 13.5 / 3.5 / 0.8 per 1,000, against the old 6.8 / 7.2 / 3.1 / 2.2. KO share by axe unmoved (26%).
- **Self-play:**
  - **Why it never learned** (33 trials): every trial collapsed to teleporting out (episode length about 5, entropy 0.01) within ~5% of steps. Causes:
    - seat 1 got seat 0's death price, so an even fight lost value for both;
    - ragger draws in pure self-play envs trained seat 1's ignored actions;
    - the opponent pool had 1-3 checkpoints.
  - **Fixes (a7a55f2cf):** seat 1 uses 1/price in reward and observation; no raggers or scripts in pure self-play envs; `[selfplay] checkpoint_interval` 16.
  - **A latent crash, also live (4f87a277a):** with five weapons swung and a sixth wielded, `riskfight_threat_refresh` built 6 slots where 5 fit, and `riskfight_threat_write` overran the observation buffer, corrupting the env pointer. Only our kits carry that many weapons, so self-play hit it first, but a live opponent switching through 6 weapons would hit it too. Cherry-picked onto the live branch.
  - **3-arm test, 50M steps, ladder perf against the live population:**

    | Arm | Ladder perf |
    |---|---|
    | A: no self-play | 9.05 |
    | B: frozen opponents and scripts only | 6.25 |
    | C: half self-play | 3.98 |

    In C, seat 1 (the same policy) teleports in 98.5% of fights against seat 0's 0.8%: a seat-1 asymmetry remains (being investigated).
- **Fast sweeps** (`scripts/sweep_riskfight_fast.sh`, 6 runs at most 240 s each in a narrow space; the user's call for iteration speed): fast1 best 10.12 in 52 s of training, fast2 best 9.70. Neither net uses the axe, orb or maul.

## 2026-10-18 evening: data-fitted escapes for a net in seat 1, and the seat asymmetry of the mirror (branch data-escapes)
- **Escapes come from the data** (`[env] data_escapes 1`): a net in seat 1 (pure self-play or a frozen checkpoint) leaves only when the scripts' escape rule fires (`riskfight_data_escape`: `RF_DHAROK_TAB_PM` on its HP in a Dharok kit, else the opponent profile's `tab_pm`, and the out-of-heals rule). Its teleport is masked, and on the tick the rule fires its mask is the no-op, so that row gives no policy gradient. In pure self-play envs both seats' rows are policy 0 and train (`src/pufferl.cu` env_setup), so seat 1's rows never train the teleport logit.
  - `probe_riskfight_escapes` (30k fights, both seats the script with its teleport removed): seat 1's rate per 1,000 ticks equals the script's within noise in every cell. Dharok kit by HP 0-19 / 20-39 / 40-59 / 60+: net 28.2 / 14.3 / 3.9 / 1.0, script without DR 29.5 / 13.9 / 3.9 / 1.0. The net's own teleports: 0 (never offered). With DR the scripts' rate shifts to 22.2 / 10.8 / 4.4 / 1.4, because their reaction lag (`opp_lag`, 0-3 ticks) delays the click.
- **Seat asymmetry, two causes, both fixed** (`probe_riskfight_seats`: one policy on both seats, teleports removed, our kits, price 1):
  - Domain randomization weakened seat 1 alone (levels 100-118, a third of the anglerfish and karambwans gone), in the mirror too. Script policy, 20k fights: kills by seat 0 / seat 1 10877 / 8484 (17 sigma) before, 9724 / 9626 (0.7 sigma) after. DR no longer touches the mirror.
  - Kit cadence: our kit was drawn per session and seat 1's per opponent, so a kit that ends fights sooner fills more of seat 0's fights. Under a policy that orbs at random seat 0 had the Dharok kit in 54% of fights and seat 1 in 51%, and seat 1 got 5 sigma more kills. Seat 1's mirror kit is now drawn with ours and kept for the session. Uniform random policy, 40-60k fights: 20041 / 16927 (16 sigma) before, 27655 / 27522 (0.6 sigma) after.
  - Not the cause: PID (seat 1 holds it on 0.500 of ticks), motor, deployable masks, recorded starts (the user's side was already drawn at random for either seat).
## 2026-10-18 evening: kits conditioned on our class, random kits for the learner (branch kit-mix)

- **The ask (the user):** in Dharok's we mostly fight Dharok's with small differences (a strength piece, an AGS for the Voidwaker), rarely a Voidwaker-only or a Soulreaper kit, and the other way round. Train a little on every pairing instead of relying on generalization.
- **Classes** (`riskfight_loadout_class`): Dharok's (helm, platebody, platelegs worn and the greataxe held), Soulreaper axe, ranged, other melee. The pool (452 kits) by draw mass: Dharok's 30.4% (216 kits: 214 distinct loadouts, 106 gear and weapon setups, the rest orb copies), Soulreaper 9.7%, ranged 16.0%, melee 43.9%. That is enough Dharok's variants (Voidwaker, tentacle, AGS, claws, maul, scimitar mains; strength, torture, rancour, fury, glory amulets), so none are generated.
- **`env.kit_same_class` (0.8):** a new opponent's pool kit comes from seat 0's class that often, else from the whole pool. A ragger keeps his ranged kit.
- **`env.learner_random_kit` (0.25, live_loadout 3 or 4):** seat 0 wears a pool kit for the session, by pool weight among the kits with no ranged weapon. Ranged kits are blocked for the learner: under deployable the plugin keeps Piety (`riskfight_plugin_prayers`) and the prayer and style heads are masked, so a net cannot fight on Rigour and rapid. Everything else is kit-agnostic: the canonical bag sorts by content code, the gear heads wield any bag weapon, the spec head reads `riskfight_spec_weapon`, the meal and drink masks read the consumable kind. A kit with more than three bag weapons shows only the three lowest item indices in the OURS rows, as before.
- **Price:** the sim has no per-kit risk. A new opponent's price is drawn (log-uniform or the live table) whatever either side wears, so seat 0 in a pool kit keeps that draw; pure self-play stays at 1 for both seats.
- **Pure self-play (tag 0):** seat 1's kit is drawn with ours at the session's start by seat 0's rule (our kits or a random one), given seat 0's class with kit_same_class, and kept for the session as ours is (the cadence of c2ce25292). P(a, b) = p(a)((1 - s)p(b) + s[a = b]) is symmetric. This replaces the "our kits only" mirror of 0affb75f8. A frozen net in our kits draws per opponent, given our class too.
- **Measured** (test_riskfight_mix, 100k draws each, live_loadout 4, s 0.8, r 0.25): seat 0 is Dharok's in 0.465. Against the script, P(his class | ours) matches s[c = d] + (1 - s) mass(d) within 0.005: Dharok's v Dharok's 0.865 (0.861), melee v melee 0.889 (0.888). **Dharok v Dharok is 0.40 of all fights** (0.15 before). In pure self-play the seats' class laws agree (Dharok's 0.464 / 0.464), same class 0.894 (0.895), Dharok v Dharok 0.41.
- **Seat balance** (`probe_riskfight_seats`, script policy, teleports masked on both, 20k fights): kills seat 0 / seat 1 9563 / 9655 (-0.7 sigma), Dharok's fights 0.423 / 0.423; with both knobs 0, 9709 / 9610 (+0.7 sigma). The probe now counts a Dharok's fight by class, not by our kit's pointer.
- **Not changed:** the threat block's spec-weapon prior still reads the unconditioned pool. [bot_eval] pins both knobs to 0, so the ladder score stays comparable with earlier sweeps.

## 2026-10-18 evening: argmax against sampling, the three-arm rerun, kit mix

- **Argmax and sampling score differently, early in training.** Deployment takes the argmax (`gs_rl_server`, hard). The sweep score sampled. The three 50M-step arms below, `session_probe`, 400 sessions, net_stake per hour, Dharok / VW:

  | Arm | Argmax | Sampled | Ladder (sampled) |
  |---|---|---|---|
  | A: no self-play | 6.64 / 6.23 | 7.89 / 7.60 | 7.97 |
  | B: frozen + scripts | 6.01 / 5.28 | 7.01 / 5.02 | 6.29 |
  | C: half self-play | 6.25 / 4.37 | 5.52 / 2.26 | 4.10 |

  - For arm A, argmax dies far more often than sampling: 0.98 deaths per session against 0.13 (greedy ladder 6.26 against 7.97 sampled). A mixed escape (teleport at p 0.2-0.4 for a few ticks) fires when sampled and never under argmax.
  - A converged net doesn't have this gap: s15t85 argmax 10.75 / 12.15, sampled 10.91 / 11.30.
  - The ladder now scores the argmax (`[selfplay] eval_greedy = 1`, 176f0b747), the policy we deploy. Scores aren't comparable with earlier sweeps.
  - (A first read of these arms ranked C best. That came from probe outputs that ran in parallel and printed in completion order, so the blocks were mislabelled. Retracted.)
- **Self-play at 50M steps** does not beat the plain arm on any measure. Whether opponents that improve help over longer training is untested.
- **Seat asymmetries in the mirror** (c2ce25292), measured with teleport masked on both seats over 20k fights:
  - domain randomization weakened seat 1 only;
  - seat 1's kit was redrawn per opponent while ours was per session (Dharok 51% against 54%).

  Kills seat 0 / seat 1 went from 10877 / 8484 to 9724 / 9626 (script policy on both seats).
- **Data escapes** (`data_escapes`): a net in seat 1 can't choose teleport; it leaves when the scripts' fitted rule fires (`RF_DHAROK_TAB_PM` for Dharok kits). The escape rate per HP band matches the script (28.2 / 14.3 / 3.9 / 1.0 per 1,000 ticks against 29.5 / 13.9 / 3.9 / 1.0). Reason: in a symmetric zero-sum mirror a fight is worth the same as an escape (0), so fleeing is a free equilibrium. Escape behaviour belongs to the measured population; fighting skill comes from the net.
- **Kit mix (the user's call, d30c7b536).** The opponent's kit class matches ours with p 0.8 (`kit_same_class`), and the learner wears a pool kit in 25% of sessions (`learner_random_kit`; ranged kits are left out, since deployable keeps Piety). The pool already holds 216 Dharok kits (106 distinct gear and weapon setups), so no synthetic variants were needed. Dharok vs Dharok is now 0.40 of the fights against scripts, from 0.15. The ladder keeps both knobs at 0 for comparability.
- **Both seats escape by the data model where both are nets** (branch sp-both-escapes): fast sweep 3 still collapsed with seat 1 alone on `data_escapes`. The learner in seat 0 teleported in 0.59 of pure self-play fights, nearly all of them rude, and the ladder fell to 0.11 / −3.76 / 5.13 against 9-10 without self-play. Against an equal copy fleeing is correct, and the shared policy carried it to the live population. Now `RiskfightState.data_escapes` is per seat: in pure self-play and frozen-checkpoint envs seat 0's teleport is masked too, and seat 0 leaves by `riskfight_data_escape(s, 0)` (its own kit decides Dharok or profile, its own script seed and supplies) on a no-op-only tick. The learner's teleport logit trains only in script/OppFit envs. No observation float carries the mask or the env type. The net can still infer the env indirectly: the price float reads 1 in the mirror, and the opponent's kit is visible.

## 2026-10-18 late: why low hitpoints kill in the sim and not in the spectated duels

Measured on the 261 spectated Dharok v Dharok duels against 6000 sim fights (`riskfight_script` HUMAN seat 0 against the Dharok pool opponent, both on pool axe kits, starts drawn; c-main 176f0b747). The scripts are in /tmp/vw (`haz.py`, `haz2.py`, `prehit.py`, `clock.py`).

- **Per tick, being low is about 6 times more lethal in the sim.** P(death within 6 ticks) at 20-35 HP is 0.015 real (n 2863) against 0.10 sim (n 40k); at 35-50 it is 0.012 against 0.047; at 50-65, 0.006 against 0.019. Whether his swing is ready makes no difference. Damage per tick, hits of 25+, specs and heal per tick are not higher in the sim.
- **The spectated bar moves only with a hitsplat.** Of 2275 eat animations below 60 HP, none shows the bar rising before his next hitsplat. 1503 rise exactly on that hit, and 728 not within 6 ticks. Between hits a fighter's real HP is his last bar plus every meal since, so bar-read HP is stale and too low. That also makes "a meal and a hit on the same tick" (84% of real hits of 15+ below 50 HP, against 34% in the sim) and "lethal on paper, survived" (83% real, 21% sim) artifacts, not tick-eating. Any measure that reads real HP or meal times from bar rises between hits has the same bias, `measure_dharok_eating.py` (c) included: its "meal the tick after his swing" is the tick his hit lands. RF_DHAROK_MEAL_PM's phase split was fitted on it.
- **The engine order is right; no fix.** Both actors' inputs, meals included, resolve before either actor's incoming hits (`riskfight_step_queues`, Fact 3). Melee hit delay is 0: an attacker on PID lands on his swing tick and one off PID lands the tick after. A meal clicked to act on the landing tick heals first. The wiki agrees: hits apply in queue order after the eat. Its "tick eating" (damage capped at calculation) does not exist in PvP.
- **Read only at fresh bars, the gap is HP at the next hit, not hit size.** Take a hit by the opponent that leaves a fighter in a band, then his HP just before the opponent's next hit (bar + damage on that tick):

  | Band after the hit | Real p10 HP | Real < 30 | Sim p10 HP | Sim < 30 | Next hit kills, real (n) | Next hit kills, sim (n) |
  |---|---|---|---|---|---|---|
  | 1-20 | 48 | 7% | 10 | 23% | 0.043 (161) | 0.198 (2935) |
  | 20-35 | 56 | 2% | 27 | 13% | 0.032 (317) | 0.110 (5649) |
  | 35-50 | 66 | 0% | 41 | 2% | 0.023 (870) | 0.050 (10373) |

  The next opponent hit is the same size: 40+ in 0.19-0.24 real against 0.18-0.21 sim. Real low fighters are healed up before the opponent's next swing nearly every time. The sim's Dharok fighter is still low one time in five to eight. Most real "hits" on fighters already low are their own locator orb (10 damage, no opposing swing), not the opponent's.
- **Meal timing, from eat animations rather than the bar.** Below 60 HP, the share of real meals at 0..7 ticks after the opponent's swing is 0, .16, .19, .29, .12, .08, .09, .06 (1834 animations). An animation is hidden on the eater's own swing tick and shows about half the time on a hit tick. The script's meals are .24, .46, .12, .10, .05, .02, .01, 0 (21975). It eats on the opponent's swing tick and the tick after, at per-tick tries (at most 424 per mille) that sometimes miss the whole window before his next swing. Real fighters spread their meals over the gap and finish before it closes.
- **Open (done 2026-10-18 night, below):** refit the Dharok meal rule from eat animations, corrected for visibility, with a "healed before his next swing" target, and re-read the knockout numbers that used bar HP between hits on hitsplat ticks only.

## 2026-10-18 night: the Dharok meal rule refitted from eat animations; stale-bar numbers re-read

- **New meal rule** (`scripts/fit_dharok_meals.py`, `RF_DHAROK_MEAL_PM[band][phase]`): the phases are ticks since the other side's swing (1, 2, 3, 4 up while his weapon is not ready, ready, none yet), not after / frees / between.
  - Meals are timed by their eat animation, weighted by the chance it shows (`RF_MEAL_VISIBLE_*`, 0 on the eater's own swing tick).
  - HP between hitsplats is the last bar plus the gap's heal, in step with the visible animations.
  - Each band is scaled so its meals carry the heal the next hitsplat shows. The 829 animation is also a potion's: only 0.25-0.64 of animations by band are healing meals.
  - Below 60 HP the rates are 154-301 per mille on every tick of his clock. The old fit's 225-424 "after his swing" was his hit landing.
- **Sim, Dharok opponent as victim** (`dh_eat`, 6,000 fights, before → after), against real. HP just before the opponent's next hit, after an opponent hit left him in the band:

  | Band | Real p10 / < 30 / next hit kills | Sim before | Sim after |
  |---|---|---|---|
  | 1-20 | 48 / 0.07 / 0.043 | 8 / 0.28 / 0.241 | 15 / 0.17 / 0.147 |
  | 20-35 | 56 / 0.02 / 0.032 | 25 / 0.14 / 0.119 | 30 / 0.10 / 0.082 |
  | 35-50 | 66 / 0.00 / 0.023 | 39 / 0.02 / 0.053 | 43 / 0.01 / 0.039 |

  - **Meal clock**, ticks 0..7 since his swing:
    - Real (visible animations): 0, .16, .19, .29, .12, .08, .09, .06.
    - Sim before: .24, .46, .12, .10, .05, .02, .01, 0.
    - Sim after: .18, .28, .22, .24, .04, .03, .02, 0, with per-tick rates now flat at 165-254 per mille.
  - **Knockouts:** KO share of fights 0.496 → 0.436 (real 63 of 261, 0.24). KO by axe 0.31 → 0.32 (real 27 of 63, 0.43). Seat 0 of `dh_eat` is a HUMAN-profile script, not the Dharok meal rule.
- **What is left:** the gap is roughly halved, not closed. Real low fighters almost never reach the next hit still low. Per-tick tries are memoryless, so a few sim fighters still go a whole cycle without a meal. Closing it needs a rule that depends on time since he was last hit low, outside the current per-tick form.
- **Stale-bar re-reads.** The spectated bar moves only with hitsplats. Old values are stale-bar artifacts unless noted:
  - **Killer HP at an axe KO:** 36 in FINDINGS (33 in the session reports). It holds. 23 of 27 axe KOs show no eat animation since the killer's last hitsplat; those 23 give a median of 33 (p25 16, p75 56).
  - **Victim HP before a KO:** 46 (carried bar) is a stale-bar lower bound. 15 of 63 victims show an eat animation after their last hitsplat. Victims read within 2 ticks of the KO give a median of 50 (n 25). KO hitsplats exceed the last read in 49 of 63 (hitsplats are not capped at HP), so the KO tick cannot give his HP.
  - **Time in HP bands:** < 40 / 40-79 / 80+ is 0.050 / 0.631 / 0.319 with meals placed between hitsplats. The carried bar gave 0.089 / 0.637 / 0.273, so real fighters spend about half as much time below 40 as the bar said.
- Tests: test_riskfight_mix, _dharok_kit, _mechanics, _session, _dharok_script and _scripts pass. The env_trace goldens are not refreshed.

## 2026-10-18 night, later: the Dharok meal rate below 79 HP refitted on current status

- **The gap was the level, not memory.** Current status (`scripts/fit_dharok_meals.py`): after the other side's hit leaves him in a band, is he healed (next hitsplat's bar + damage 8+ over the read) by that side's next hit, L ticks of its clock later? It needs no visibility model. Spectated, by his next 4-tick swing:

  | Band | Healed | n | Fitted h (per mille per tick) |
  |---|---|---|---|
  | 0-20 | 0.97 | 38 | 589 |
  | 20-40 | 0.98 | 192 | 572 |
  | 40-60 | 0.94 | 534 | 468 |
  | 60-79 | 0.41 | 529 | 115 |

  A constant per-tick rate h fits P(healed by L) = 1 - (1 - h)^L within noise at every L. Hazards by ticks since his last hit and since entering the band are both flat within noise. The previous animation-weighted table (154-301 per mille) left about 0.35 unfed by his next 4-tick swing. Bands 0-3 now take h on every phase. Band 4 keeps the animation rates, since the bar hides heals past 99.
- **Sim, both `dh_eat` seats on the Dharok script** (6,000 fights; the Dharok meal rule is now forced on seat 0 too). HP just before the opponent's next hit:

  | Band | Real p10 / < 30 / next hit kills | Sim |
  |---|---|---|
  | 1-20 | 48 / 0.07 / 0.043 | 50 / 0.03 / 0.035 |
  | 20-35 | 56 / 0.02 / 0.032 | 55 / 0.01 / 0.018 |
  | 35-50 | 66 / 0.00 / 0.023 | 60 / 0.00 / 0.012 |

  - **Healed by a 4-tick swing:** sim 0.96 / 0.93 / 0.88 (bands 0-20 / 20-40 / 40-60).
  - **Meal clock**, ticks 0..7 since his swing: sim .18, .29, .30, .17, .04, .02, .01, 0; real (visible animations) 0, .16, .19, .29, .12, .08, .09, .06.
  - **Knockouts:** KO share 0.16 against real 0.24 (63 of 261); KO by axe 0.33 against 0.43. The sim now slightly under-kills low fighters.
- Six tests pass (mix, dharok_kit, mechanics, session, dharok_script, scripts). The env_trace goldens are not refreshed.

## 2026-10-18 night: self-play with both-seat data escapes, two seeds

182.7M steps on the anchor (9a13c40f7: refitted Dharok meal rates, kit mix, both net seats escape by the data model in self-play and frozen-opponent envs). Greedy ladder, plus the argmax `session_probe` (300 sessions, net_stake per hour):

| Arm | Seed | Ladder | Dharok | VW |
|---|---|---|---|---|
| A: no self-play | 73 | 7.37 | 7.11 | 7.15 |
| C: half self-play | 73 | 6.45 | 6.58 | 5.85 |
| A: no self-play | 74 | 8.41 | 8.46 | 8.53 |
| C: half self-play | 74 | 6.84 | 6.76 | 6.63 |

- **The collapse is gone:** with both seats escaping by the data model, self-play trials score 6.4-6.8, against 0.1 / −3.8 / 5.1 in fast sweep 3. They still trail the plain arm by 0.9-1.6 per hour at this budget, on both seeds and both kits.
- **Self-play is off for now.** Correction: the pin `--sweep.selfplay.enabled.max=0.4` never reached fast4 or fast5, because `sweep_riskfight_fast.sh` dropped extra arguments (fixed: they now pass through). Both sweeps sampled self-play in runs 1, 2 and 5. In fast5 those scored 5.73 / 4.10 / 4.18, against 8.68 / 7.69 / 7.07 without self-play, which agrees with the two-seed test. The machinery stays in place.
- **Neither arm uses the Dharok tools:** no orb; axe swings only at about 96 HP or not at all.
- **Scores fell across the board** compared with fast3 (run 0 10.31): after the meal refit, sim Dharok opponents heal before the next swing as real fighters do, so they are harder to KO. Scores aren't comparable across that change.

## 2026-10-18 night: no joint meal fit; low damage intake makes the sim's Dharok fights long

Harness: `dh_eat` with both seats on the Dharok script (6,000 fights per arm, starts drawn), against the 261 spectated Dharok v Dharok duels. Scripts are in /tmp/mj (`ends.py`, `split.py`, `msize.py`, `tab.py`, `dmg.py`, `tib.py`).

| Measure | Real | Sim, meals before tick-eat | Sim, 702cfae21 | Sim, c-main 0af2941c3 |
|---|---|---|---|---|
| Opponent HP p25/med/p75 when the other is ready at 1-20 | 43/59/69 (170) | 53/70/84 | 53/70/84 | 63/79/89 (633) |
| ... at 20-35 | 46/66/73 (312) | 54/70/84 | 57/71/84 | 65/78/89 |
| ... at 35-50 | 53/66/76 (684) | 58/72/85 | 60/73/86 | 67/80/89 |
| Ends: KO / teleport by lower / by leader / other | .24 / .43 / .14 / .17 | .50 / .37 / .12 / 0 | .46 / .39 / .14 / 0 | .17 / .61 / .22 / 0 |
| Time at HP < 40 / 40-79 / 80+ | .05 / .63 / .32 (gap estimate) | .08 / .46 / .46 | .07 / .46 / .47 | .03 / .38 / .60 |
| Damage taken per tick | 1.90 | 1.87 | - | 1.53 |

- **The escape share came in with the meal refits:** .50 before tick-eat, .54 after the first refit, .83 after the current-status refit. Fighters fed before every hit stay high, so the greataxe hits less (Dharok's set scales with missing HP). Fights run longer (median 187 ticks against 146 real). 45% of sim escapers leave on the scripts' out-of-heals rule (below 30 HP with no food left).
- **The meal data leaves no room for a joint fit.**
  - Opponent HP doesn't matter. P(healed by his next hit) by the opponent's HP < 50 / 50-79 / 80+ is 34/35 / 43/46 / 15/16 in the lowest band and 111/123 / 562/608 / 166/174 at 40-60.
  - Meal sizes check out. Gaps with one visible eat animation give single/double/triple .10/.14/.69 at 0-20, .12/.46/.39 at 20-40, .55/.38/.02 at 40-60 and .51/.01/0 at 60-79, which is what `RF_DHAROK_MEAL_SINGLE_PM` / `_DOUBLE_PM` give.
  - The 60-79 band matches at a 4-tick swing: 0.45 healed sim against 0.41 real.
  - Lowering the 60-79 rate to 60 per mille, or the 80+ rates to half or zero, moves KO share by at most 0.015 (.166 to .180) and opponent HP by 5. Making seat 0 base Defence 80 like a Dharok kit moves nothing (KO .169).
- **What differs is damage intake:** 1.53-1.62 per tick in the sim against 1.90 real, at every HP band. So sim fighters sit at 80+ 0.60 of the time against 0.32. That is not a meal parameter. The next suspects are swing timing and hit sizes against Dharok-armoured defenders at given HP.
- **The teleport table has the same stale-bar bias.** Re-read on the gap HP estimate, tabs per 1,000 ticks at HP 0-19 / 20-39 / 40-59 / 60+ are 46.5 / 18.0 / 4.6 / 0.4, against the carried-bar fit's 25.2 / 11.5 / 3.4 / 0.5 (`RF_DHAROK_TAB_PM` = 29, 14, 4, 1). Swapping it in (per 10,000) moves the sim's escape share from .83 to .82 and KO share from .17 to .18. It is not committed: the low bands are rarely visited, and 60+ goes from 1 to 0.4 per 1,000, which the per-mille table can't hold.

## 2026-10-18 night, later: teleport table on corrected hitpoints; the damage-intake gap is mostly a counting gap

- **`RF_DHAROK_TAB_P10K`** (`scripts/fit_dharok_tab.py`) replaces `RF_DHAROK_TAB_PM`. It is fitted on HP between hitsplats (last hitsplat's bar plus the next one's heal, in step with eat animations), with the leaving side read from his teleport animation.

  | | HP 0-19 | 20-39 | 40-59 | 60+ |
  |---|---|---|---|---|
  | Per 10,000 fighter-ticks (new) | 465 | 180 | 46 | 4 |
  | Old carried-bar fit, same units | 252 | 115 | 34 | 5 |

  It is per 10,000 because the 60+ rate is under one per mille. Non-Dharok profiles keep their per-mille roll unchanged.
- **Damage per tick, decomposed** (`/tmp/mj/decomp.py`). Real: swings from attack animations, hits from the defender's hitsplat on the swing tick or the next. Sim: the probe's queued hits. Attacker HP is the gap estimate (real) or true HP (sim).
  - **Like-for-like, the gap mostly goes away.** The 1.53 against 1.90 per tick came from two harness and counting effects:
    - Seat 0 kept Defence 99. Base 80, as the opponent's Dharok kit gets, adds 0.09.
    - The real figure counts the fighter's own orb 10s as hitsplats; the sim tally didn't (0.25 per tick of HP drops with no hit and no meal).
    - HP lost per tick: 1.96 real against 1.62 + 0.25 = 1.87 sim.
  - **Parts:**

    | Part | Real | Sim |
    |---|---|---|
    | Swings per tick | 0.175 | 0.195 |
    | Accuracy of normal swings | 0.27-0.31 | 0.25-0.26 |
    | Mean hit given a hit: axe | 32.6 | 28.6 |
    | ... VW | 19.9 | 21.1 |
    | ... VW spec | 44.1 | 41.1 |

    The script doesn't lose attack ticks to eating, and its weapon shares are close. No mechanics error found.
  - **The biggest term left is low attackers.** At own HP < 40, damage per tick is 2.22 real against 1.26 sim:
    - Greataxe 1.12 against 0.61 (swings per tick 0.092 against 0.062, accuracy 0.35 against 0.26).
    - Voidwaker spec 0.49 against 0.23 (0.011 against 0.006 per tick).

    It is small overall, because the sim spends 0.026 of ticks below 40 against real 0.05.
- **Heal intake is not lower in the sim:** 1.60 per tick against at least 1.70 real (the bar hides heals above 99).
  - Fight length: real median 146 ticks counts the spectated window only, and 0.17 of real duels end with neither a KO nor a teleport seen. So "187 against 146" is not a like-for-like gap.
- **Re-measured** (6,000 fights, both seats on the Dharok script, seat 0 at base Defence 80, new tab table):

  | Measure | Real | Sim |
  |---|---|---|
  | Opponent HP when the low fighter is ready (median), low fighter 1-20 / 20-35 / 35-50 | 59 / 66 / 66 | 78 / 80 / 80 |
  | KO / teleport by lower / by leader | .24 / .43 / .14 | .19 / .66 / .15 |
  | Time at HP < 40 / 40-79 / 80+ | .05 / .63 / .32 | .03 / .39 / .59 |
  | HP lost per tick | 1.96 | 1.61 + 0.25 orb |
  | Fight length, median ticks | 146 (spectated window) | 200 |

  The opponent-HP and time-in-band gaps remain. With damage and heal intake matched per tick, the next suspect is the HP the fight starts at: the spectated window starts mid-fight, while the sim starts at the drawn start HP.

## 2026-10-18 night, last: the opponent-HP gap is real; the 60-79 meal band split

- **Not a phase artefact.** On ticks after both fighters have been below 80 once (real: a hitsplat read below 80; sim: true HP), with real HP from the gap estimate (`/tmp/mj/phase.py`):

  | | Real | Sim before |
  |---|---|---|
  | Time at HP < 40 / 40-79 / 80+ | .08 / .72 / .20 | .03 / .42 / .55 |
  | Opponent median HP when the low fighter (1-20 / 20-35 / 35-50) is ready | 66 / 69 / 69 | 76 / 78 / 79 |

- **How the sim opponent stays high.** Not damage: per tick at 60-79 and 80+ the sim takes 1.61-1.67 plus 0.25 orb, against real 1.83-2.11 with its orb included. He eats back over 80. Up-crossings over 80 per tick spent below 80 were 84 per mille sim against 27 real. Stays above 80 lasted a median 10 ticks against 6.
  - The cause is the 60-79 meal band. On current status (`scripts/fit_dharok_meals.py`), a fighter hit into 59-69 eats at 241 per mille (0.70 fed by a 4-tick swing, n 265). Hit into 69-79 he eats at 36 (0.13, n 264), and from 79 up at 10. The single 60-79 band at 115 fed him from 70-79 past 80.
  - From 80 up, 0.95 of single-animation gaps show no heal, so those animations are potions.
- **Change:** `RF_DHAROK_MEAL_PM` now has 6 bands (bar 0-5, 6-11, 12-17, 18-20, 21-23, 24 up; `riskfight_dharok_meal_band`). All are fitted on current status: 589, 572, 468, 241, 36, 10.
- **Re-measured** (6,000 fights, both seats on the Dharok script, seat 0 base Defence 80, `RF_DHAROK_TAB_P10K`), after both have been below 80:

  | | Real | Sim |
  |---|---|---|
  | Time at HP < 40 / 40-79 / 80+ | .08 / .72 / .20 | .03 / .50 / .47 |
  | Opponent median HP (low fighter 1-20 / 20-35 / 35-50) | 66 / 69 / 69 | 74 / 75 / 77 |
  | KO share | .24 | .19 |
  | Escape share | .59 | .81 |
  | HP p10 before the opponent's next hit (bands 1-20 / 20-35 / 35-50) | 48 / 56 / 66 | 52 / 54 / 61 |

- **Still open:** up-crossings over 80 are 67 per mille in the sim against 27 real, 45 of them from meals at 60-79. A constant rate per band, fitted on ticks right after a hit, still feeds a fighter who climbed into 60-69 by eating. The eat-animation rates split by "hit since his last meal" differ only 1.2-1.5x by band, so that alone won't close it. The real crossing count rests on the gap estimate, which spreads a gap's heal over its eat animations.

## 2026-10-19: is low-HP Dharok play worth it in a calibrated sim?

- **Calibration so far** (meal-joint, 2392bd4d0), both seats on the Dharok script against 261 real Dharok v Dharok duels:
  - Damage per tick matches like for like: 1.87 sim against 1.96 real. The earlier 1.53 left out orb 10s and kept seat 0 at Defence 99.
  - Swings per tick: 0.195 sim, 0.175 real.
  - Accuracy: .25 sim, .27-.31 real.
  - Hit sizes match per weapon.
  - Meal rates come in six HP bands, fitted on "healed before his next hit". 80+ eat animations are 95% potions.
  - Teleports per 10,000 fighter-ticks: 465 / 180 / 46 / 4, fitted on HP between hitsplats.
- **Still open:**
  - The sim opponent's median HP when the low fighter is ready: 74-77, against 66-69 real.
  - Time at 80+: .47 sim, .20 real.
  - Escapes: .81 sim, .59 real. Another .17 of real windows end unseen.
- **Oracle at ed5a2c4f2** (two seeds, 6,000 sessions per arm): no axe rule beats never swinging. Arms that swing 0.7-1.2 times per fight lose 0.03-0.35 per hour. One swing KOs at P 0.05-0.06 from our HP below 50, against his median 66-68. The real opponent sits at about the same HP when the low fighter is ready (66-69), and the real victim's 50 just before a KO is selection on the KO.
- **Conclusion:** in a sim calibrated this closely, low-HP greataxe play is not worth it, and the nets' zero axe use is correct. Real fighters' axe KOs come from low-HP exchanges where both sides are low. Real data doesn't show that getting low pays.

## 2026-10-18 evening: live/sim observation parity (obs-parity)

Per-float audit of 474 live decisions (the 5 VW traces of 2026-10-17 through `gs_rl_server`) against seat 0 of the training recipe (s19t00, argmax, VW kits); dump and compare scripts in `/tmp/obsaudit`.
- **Meal clocks were a tick high live.** The O line sends `ready - tick` (`Consumable.readyAfter`: the meal's tick + 3) on the request of tick N, the state after tick N; the sim lowers its clock at the end of the step that ate (`riskfight_step.h`), so after tick N it reads `ready - N - 1`. Live read 3/3, 2/3, 1/3 after a meal (1.0 in 15% of decisions, never in the sim); `snapshot_of` now converts, and live reads 2/3, 1/3, 0 on the ticks the sim does (trace 211323Z tick 229 meal; sim tick 18). The lookahead server reads the same snapshot, so its rollouts now allow the next meal on the tick the game does.
- **His offset is a distance now.** Observation `opponent[1..2]` was his signed tile offset; the sim always seats him a tile east (+1 in 99.4% of steps), live he stood west in 66% of decisions, which the net had never seen. The writer takes `abs`, in the sim and live alike. Nothing else reads the sign (scripts read other opponent floats; pathing and range use positions).
- **Pie kit is the live one.** `RF_VW_PIES` 4 to 2: 8 of the 9 live fight starts carry 2 summer pies and 12 anglerfish, the ninth the plain VW kit.
- Left: live opponents stand far off at fight starts (dx up to 10, dy up to 15 in 7% of decisions; the sim has no approach), live eats far more (karambwan clock up 13% vs 0.2%), and a summer pie's hard-food clock: the plugin sends `tick + 3` for every hard food while the sim's pie locks 1 tick, so after a pie live still reads one tick high up to 2/3 where the sim reads 0.

## 2026-10-19: live/sim observation parity audit, and live candidate a1

Method: 474 live decisions from 5 VW traces compared, float by float, with 103k sim steps of s19t00 (`/tmp/obsaudit`). Bugs found and fixed:

| Float | Live | Sim | Fix |
|---|---|---|---|
| Food / potion / karambwan timers | the plugin's `ready - tick`, up to 3/3 | its post-step clock, at most 2/3 | the server subtracts 1 (84be227c4) |
| Pie lock | 3 ticks, as for any hard food | 1 tick | the lock is per item in the plugin (light #352) |
| Our Vengeance cooldown | constant 30 (0.6) | counts down from 50 | the plugin sends the countdown from the tick varbit 2451 turned on; a click in flight reports 50 (light #350) |
| Recoil damage used | literal 0 | counted | the plugin sends `RecoilRing` 40 − charges (light #351) |
| Teleport lock after our special | never set, so the mask was always open | 8 ticks | the server records the special from the drop in `spec_us` (d1bf61bac) |
| His offset dx/dy | he stood west of us (−1) in 66% of decisions | always +1 | both paths write |dx|, |dy| |
| VW pies kit | 2 pies + 12 anglerfish in the live bag | 4 pies | the sim kit is now 2 pies |

Open, not bugs:
- Fights live start from approach distances that the sim never trains.
- Live opponents were all melee.
- The live divine timer is assumed (`500 - n`).
- The Vengeance spam seen in replays is a replay artefact: the replay can't carry out the net's clicks.

Live candidate a1: anchor hyperparameters, 182.7M steps on 37d061a46. Greedy ladder 8.88; argmax probe 7.85 per hour in the Dharok kit, 8.65 in VW. No axe, orb or maul use. Body or legs come off on 0.55 steps per fight. Installed with the parity fixes; replays of 4 live sessions run clean.

## 2026-10-19: armour strips stay allowed (the user's call)

- **a1's strips don't pay.** a1 takes off Dharok body or legs on 0.55 steps per fight in the sim; live it unequipped the legs, worn slot 7, at ticks 179-182. A paired counterfactual (65a6d7335, reverted: clone at each strip, strip vs never strip for the rest of the fight) gave:
  - return −0.009 ± 0.010 against the script and −0.022 ± 0.010 against OppFit;
  - more damage taken: 16.3 to 17.9 over the next 10 steps;
  - no reaction from him, and no channel through the price or the observation.

  a1 never swings the axe, so these strips are thrashing on an action it never found a use for.
- **The user rejected the mask that would have blocked strips with no spare piece in the bag.** Stripping is a real top-player tactic: take more damage on purpose to feed Vengeance or recoil, lower your own HP for the axe, bait the opponent, even drop Piety. The fix belongs in the world: opponents and observations under which the payoff exists and can be seen. A ban would remove the tactic.
- **Low-HP non-eating, by the same principle:** risking HP is valid when he truly can't threaten us (no spec energy, no Vengeance, on his attack timer). The bug is learning it as a blanket rule because sim opponents at low HP are harmless. LowHpThreat found the sim mechanic behind that: an eat clears the attack interaction, so a meal costs the tick's swing. Being checked against data.

## 2026-10-19: Dharok fighters differ; per-opponent personas (branch personas)

- **Data:** `spectator.duels` re-exported with the two 2026-10-18 sessions. It is a clean superset (the old file is a byte prefix), kept as `spectator.duels.bak-20261018`. Duels 537 → 687, Dharok v Dharok 261 → 310, third-party rows 0.032 (unchanged). The export now also writes `spectator.duels.names` (hashed names per duel side). That gives 134 Dharok v Dharok fighters, 100 with 2+ fights and 52 with 5+. The meal, tab, axe and orb fits move within noise (meal 589/572/468/241/36 → 583/581/470/241/35; tab 465/180/46/4 → 468/176/48/5), so no table changes.
- **Between-fighter heterogeneity** (`scripts/fit_dharok_personas.py`). For each behaviour: Pearson X² over fighters against a parametric-bootstrap null at each fighter's n, the moment sd of a fighter's multiplier on the pooled cell rates, and the persistent share. The persistent share is the split-half cross moment of his alternate fights over the fighter-duel variance.

  | Behaviour | X² / null (p) | sd, fighters [90%] | Persistent share |
  |---|---|---|---|
  | Unfed by the next hit, < 60 HP | 1.37 (0.10) | 0.53 [0.31, 0.71] | 2.4 [1.0, 4.1] |
  | Healed by the next hit, 60-79 HP | 1.53 (0.005) | 0.21 [0.17, 0.26] | 1.2 [0.65, 1.9] |
  | Swing on a ready tick | 2.31 (0.005) | 0.07 | 0.73 |
  | VW spec per tick, his bar ≤ 25 | 1.09 (0.23) | — | — |
  | Axe share of swings | 3.41 (0.005) | 0.33 [0.28, 0.37] | 0.68 [0.47, 0.91] |
  | Orb per visible tick | 8.56 (0.005) | 0.87 | 0.65 |
  | Tab per tick | 0.92 (0.60) | — | — |
  | Double or triple meal | 1.73 (0.005) | 0.16 [0.11, 0.21] | 1.45 |

  - Meal promptness, uptime, axe, orb and meal size are real traits, and they persist across a fighter's fights. Spec rate and tab-by-HP are not distinguishable from noise at these n.
  - The orb spread is already in the sim: a 0/1 carry at `RF_KIT_ORB_PM` 0.6 gives a multiplier sd of 0.82.
  - Correlations (fights-weighted r of empirical-Bayes multipliers, 90% CI):
    - Fast eaters take bigger meals: +0.44 [0.26, 0.62].
    - Fast eaters swing less on ready ticks: −0.45 [−0.60, −0.26]. The same holds for big-meal takers and uptime: −0.50.
    - Axe use is independent of eating (+0.06).
  - There is one style axis, careful against aggressive.
- **Outcomes per fight** (r, 90% CI):
  - Slow eaters (< 60) take more KOs (+0.22 [0.09, 0.35]) and escape less (−0.37).
  - Fast eaters escape more (+0.21).
  - VW spec rate goes with KOs dealt (+0.29 [0.18, 0.41]).
  - Axe propensity predicts nothing (+0.09 [−0.04, 0.21]).

  Behaviours carry outcome signal, but the signal is weak at the fighter level.
- **Personas** (`riskfight_personas.h`, generated):
  - **Fit:** one row per spectated fighter (131), drawn in proportion to his fights. Each trait gets a random effect th ~ N(mu, sigma²), fitted by marginal likelihood. The posterior means are spread back to sigma² × min(1, persistent share), and the rows are kept whole so the correlations stay.
  - **Traits:**
    - EAT: the exponent on the log-survival of `RF_DHAROK_MEAL_PM` bands 0-4. p10/p50/p90 are 0.77/1.05/1.34.
    - BIG_MEAL: the odds of a double or triple meal (0.62/0.93/1.34).
    - AXE: the odds of `RF_AXE_SWING_PM` (0.57/0.99/1.59). Axe takes only its persistent share, because the per-fight profile `axe_pm` (sd 0.23) already supplies the within-fighter part.
  - **Wiring:** `RiskfightState.opponent_persona` is drawn off `opp_roll`, so a rematch keeps him, and the step passes it in `RiskfightProfile.dharok_persona`. 0 means pooled rates, so scripts and tests that build a profile are unchanged.
  - **Not modelled:**
    - Uptime: the script always attacks, so there is no table to scale.
    - Spec timing and tab HP: no detectable spread.
- **Validation** (`dh` harness, both seats on the Dharok script, starts drawn, 6,000 fights per arm, pooled vs personas):
  - Pooled targets are unchanged:
    - KO .197 / .188 (real .248).
    - Escape share .802 / .809 (real .577).
    - Opponent HP p25/med/p75 when the low fighter is ready at 1-20: 60/75/84 and 61/74/84.
  - The spread moves toward real. Axe share sd over fighter-fights is .35 pooled, .40-.41 with personas, and .46 real.
  - The harness's crude unfed-at-next-hit reading (.38 sim against .064 real on current status) is not like for like, so the eat spread is not checked in the sim.
- Tests pass: test_riskfight_dharok_script, _mix, _mechanics, _session. The goldens are not refreshed: the persona changes Dharok pool opponents' meal and axe rolls.

## 2026-10-19: low-HP offence, and eating while we are below his range

- **The policy side: in the sim, not eating when he is low pays.** Setup:
  - a1, argmax, in the training env (pool opponent, Dharok kit half the time), 180 sessions.
  - At every decision with our HP at or below his chance range and a meal allowed, the state is cloned. Branch A forces a meal, branch B forces none, then a1 plays the fight out.
  - When he is below 40 HP, a1 eats on 0.07 (Dharok kit) to 0.12 (VW kit) of these decisions. From 40 up it eats on 0.37-0.38.
  - Before the fix below, Dharok kit, he < 40: KO .362 and death .010 with the meal, against .429 and .014 without. VW kit: .428 / .014 against .485 / .017.
- **Cause 1, the click rule.** In the game, an inventory action (eat, drink, equip, the orb) clears the attack interaction. An attack click after it on the same tick locks back on.
  - The user confirmed this. Our live recordings agree: of 91 meals within 8 ticks of our swing, our interaction read NONE within 2 ticks in 72, and the 4 meals with no attack click after them had no swing.
  - `riskfight_attack_click` used to re-click only when no interaction was active, so any meal or switch while attacking dropped the target. That held for the net, the scripts and live alike.
  - It now re-clicks after any inventory action. The step and `gs_rl_server` queue the inventory clicks before the attack.
  - The food's attack delay stays: +3 hard food, +2 fast food, 0 for drinks. `test_riskfight_mechanics`: on a ready tick, drink + attack swings that tick (a tick later without the re-click), and hard food + attack swings 3 ticks later.
- **After the fix**, same probe:

  | | Meal: KO / death | No meal: KO / death |
  |---|---|---|
  | Dharok kit, he < 40 | .362 / .015 | .435 / .032 |
  | VW kit, he < 40 | .353 / .020 | .406 / .057 |

  Low sim opponents now kill a non-eater 2-3 times as often, but skipping the meal still buys 0.05-0.07 KO.
- **Cause 2, low attackers, real against sim** (`scripts/measure_dharok_low_attack.py`, both seats on the Dharok script, 6,000 fights). After a hitsplat read leaves him in a band, over the next 8 ticks, real / sim before / sim after:

  | Band | P(swing) | Swing by +2 | Axe swings | Damage per tick |
  |---|---|---|---|---|
  | 0-19 | .50 / .50 / .50 | .23 / .10 / .11 | .38 / .16 / .18 | .68 / .50 / .50 |
  | 20-39 | .60 / .64 / .64 | .31 / .13 / .13 | .34 / .19 / .19 | .74 / .64 / .65 |
  | 40-59 | .73 / .81 / .81 | .30 / .21 / .20 | .33 / .18 / .18 | .99 / .90 / .90 |

  - The earlier "2.22 against 1.26 damage per tick below 40" was a gap-estimate artefact. An eat animation never shows on the eater's swing tick, so a meal eaten with the swing leaves the swing in too low a band.
  - The real gap is swing timing and axe share, not swing rate.
  - Removing the script's STOP on meal ticks moved none of these (KO .198 to .186), so it stays.
  - KO share: .198 before, .194 after (real .24).
- **Open:**
  - `RF_AXE_SWING_PM` is fitted on the stale bar at the swing, which doesn't count meals eaten since his last hitsplat. The script reads its band from true HP after its meals, so low fighters drop out of the <40 axe band.
  - Reading the band from his HP at his last hitsplat is the next step. The `personas` branch also edits the `axe_pm` line.

## 2026-10-19: food attack delay on the ready tick, data against the wiki's wording

- **The wiki's wording:** "If your weapon is ready to attack again then eating does not add any new delay."
- **Our 159 own tapes contradict it on the ready tick:**
  - A meal on the tick the swing is due delays that swing by the full amount: +3 anglerfish, +2 karambwan, +5 both, in 101 of 106 meals.
  - Each idle tick before the meal earns one tick back: e=1, 5 of 6; e=2, 2 of 2. From 3 idle ticks there is no delay.
  - Without a meal, a click on a ready weapon swings that tick 83 of 95 times.
- **The engine already matches the data,** so it stays unchanged. Its signed attack history keeps counting while idle, and food adds to it (320c10838; that commit chose the model for convenience, but the data confirm it).
- **Reading:** the wiki's "ready" means idle long enough for the delay to have run out, i.e. 3 ticks for hard food. The older rule (add only when the timer is above 0) would be wrong at timer 0 too: 74 of 80 meals one tick before ready still added the full delay.

## 2026-10-19: the greataxe band read from the bar at his last hit

- **What changed:** the band of `RF_AXE_SWING_PM` is now his own bar the tick before the swing. The bar moves only with a hitsplat, so it is his HP at his last hit, before any meals since. `scripts/measure_dharok_play.py` (d) is refitted on 14,872 swings from `spectator.duels` of 10-19.
- **The script reads the same quantity:** the bar the other side last saw, put in the script observation as `RF_SCRIPT_OWN_BAR`. It used to read its true HP after its meals, which lifted low fighters who had just eaten into a higher band.
- **The table barely moved:** <40 755 / 722 / 509 (was 794 / 736 / 494); 40-79 and 80+ within 0-35 per mille.
- **Personas:** the persona axe column is refitted on the same band (`scripts/fit_dharok_personas.py`); 129 of 131 multipliers moved, the eat and big_meal columns are unchanged.
- **Re-measured** (`scripts/measure_dharok_low_attack.py`, both seats on the Dharok script, 6,000 fights; real is now 310 duels). Real / before / after:

  | Band | Swing by +2 | Axe swings | Damage per tick |
  |---|---|---|---|
  | 0-19 | .25 / .11 / .12 | .39 / .18 / .23 | .66 / .50 / .51 |
  | 20-39 | .31 / .13 / .13 | .34 / .19 / .24 | .72 / .65 / .66 |
  | 40-59 | .30 / .20 / .19 | .32 / .18 / .18 | 1.00 / .90 / .89 |

  KO share .194 → .195 (real .24).
- **What is left:**
  - The swing that follows is the one the script chose: with his bar below 12, 0.45 of sim swings are axe and the script wanted the axe on 0.45 of them, against 0.64 real.
  - The remaining axe gap sits in the table's other axes: the opponent's bar (the sim opponent sits higher, 74-77 against 66-69) and the profiles' axe propensity.
  - Swing within 2 ticks (.12 against .25) is unchanged. On the ready tick the meal's attack delay is right (093cedadc), so the gap is in when the script eats relative to its swing.
- **Tests:** test_riskfight_dharok_script (axe cells within 2% of the new table), _mix, _mechanics, _session, _scripts (now sets the bar along with our HP), test_riskfight, _humanlike, _training, test_soulreaper_zcb and test_rf_serve_visible pass. test_riskfight_long_determinism is remote-only and timed out locally. The env_trace goldens are not refreshed.

## 2026-10-19: why a low sim Dharok fighter swings late, and a meal rule on his own clock

- **Where the gap is** (after a hitsplat leaves him at bar 1-11; real from `spectator.duels`, sim from 6,000 Dharok-script fights; his own attack clock at the next tick, meals counted in):

  | Clock at t+1 | Real share / swing by +2 / eats first | Sim before | Sim after |
  |---|---|---|---|
  | 0 (may swing) | .32 / .48 / .42 | .18 / .49 / .56 | .18 / .61 / .44 |
  | 1 | .17 / .42 / .51 | .19 / .23 / .81 | .19 / .48 / .56 |
  | 2 | .24 / .10 / .63 | .29 / .00 / .91 | .26 / .00 / .67 |
  | 3 up | .28 / .07 / .78 | .34 / .00 / .95 | .37 / .00 / .94 |

  - **His clock at the hit:** real fighters are ready more often (.32 against .18). This accounts for about half of the swing-by-+2 gap. The other half is the meal order at clock 1: the sim ate first (.81) on a running clock, and the meal's attack delay pushed the swing out 3 ticks.
  - Eating and swinging can't happen on one ready tick (the meal delays the swing; 093cedadc), so the tick order is "eat first" or "swing first".
  - PID is not a factor: a swing's hitsplat lands on the same tick .52 real and .53 sim (landed hits only).
- **Change:** `RF_DHAROK_MEAL_CLOCK` = 632 / 200 / 316 / 2852 (`scripts/fit_dharok_meal_clock.py`).
  - Below 40 HP (meal bands 0-1), the meal hazard is raised to that exponent per mille by the fighter's own attack timer (0, 1, 2, 3 up).
  - Clocks 0-2 are fitted on the real "eats first" shares.
  - Clock 3 up takes the rest of a 4-tick cycle's 4000, so meals per cycle stay as before.
  - The persona eat exponent applies on top.
- **Low-attacker table** (`scripts/measure_dharok_low_attack.py`), real / before / after:

  | Band | Swing by +2 | Axe swings | Damage per tick |
  |---|---|---|---|
  | 0-19 | .24 / .12 / .19 | .37 / .23 / .27 | .72 / .51 / .65 |
  | 20-39 | .28 / .13 / .20 | .31 / .24 / .28 | .75 / .66 / .73 |
  | 40-59 | .30 / .19 / .19 | .31 / .18 / .19 | .95 / .89 / .92 |

  - KO share .195 → .200 (real .24).
  - Cost on the meal target: healed by the opponent's next hit is down 0.02-0.04 (0-20: .82 → .78). His next hit kills at .048 → .051, .025 → .025, .010 → .011.
  - A clock-0 exponent of 1200 restores the healed share and gives back most of the swing gain.
- **a2, paired meal / no-meal probe** (our HP at or below his chance range, a meal allowed, he < 40; 180 sessions; KO / death):

  | | Before: meal | Before: no meal | After: meal | After: no meal |
  |---|---|---|---|---|
  | Dharok kit (a2 eats .047 / .039) | .404 / .023 | .569 / .034 | .381 / .015 | .464 / .025 |
  | VW kit (a2 eats .052 / .052) | .398 / .021 | .478 / .042 | .369 / .019 | .451 / .060 |

  - The rule halves the KO premium of skipping the meal in the Dharok kit (+.165 → +.083). In the VW kit it raises the death cost (+.021 → +.041).
  - Skipping still pays unless a death costs more than about 2 kills (VW) or 8 (Dharok).
- **Open:**
  - The axe share is still short of real at every clock (.39-.43 against .54-.62 per first swing). It comes from `RF_AXE_SWING_PM`'s other inputs: the opponent's bar, and the profiles' axe propensity.
  - The clock mix at the hit (.18 against .32 ready) reflects the script swinging on nearly every ready tick at 60 up (.91 against .39 real).
- Tests: test_riskfight_dharok_script, _mix, _mechanics, _session, _scripts, test_riskfight, _humanlike and _training pass. The env_trace goldens are not refreshed.

## 2026-10-19: Dharok refit on 1,093 duels (branch refit-1093)

- **Data:** 1,093 duels, 510 Dharok v Dharok (261 → 310 → 510), third-party rows 0.033. 172 DvD fighters, 131 with 2+ fights, 80 with 5+.
- **Noise test:** duel-bootstrap SE of every fit on the new file (60 reps, `/tmp/personas/boot.py`). The old file is a byte prefix of the new one, so SE(new − old) = SE_new × sqrt(n_new / n_old − 1). The duel bootstrap ignores fighter clustering, so it understates the SE slightly.

  | Table | Old → new | Outside noise? |
  |---|---|---|
  | `RF_DHAROK_MEAL_PM` | 589 / 572 / 468 / **241** / 36 / 10 → 568 / 560 / 464 / **205** / 37 / 10 | band 3 only (59-69 HP, z −4.1, n 881); the rest \|z\| ≤ 0.5 |
  | `RF_DHAROK_MEAL_CLOCK` | 632 / 200 / 316 / 2852 → 647 / 169 / 310 / 2874 | no (\|z\| ≤ 1.0) |
  | `RF_DHAROK_TAB_P10K` | 465 / 180 / 46 / 4 → 485 / 174 / 49 / 5 | no (\|z\| ≤ 1.3) |
  | `RF_AXE_SWING_PM` (18 cells) | e.g. 484 / 316 / 97 → 514 / 332 / 101 at 40-79 HP, no Vengeance | no: \|z\| ≤ 1.9, Σz² ≈ 18 on about 14 free cells. The drift is upward at 40+ HP. |
  | `RF_DHAROK_ORB_PM` | {74, 109, 102, 30, 62}, {32, 59, 78, 55, 45} → {69, 103, 88, 31, 54}, {37, 59, 68, 53, 40} | no: two cells at z −2.4 / −2.1, Σz² ≈ 13 on 10 |
  | `riskfight_personas.h` | 131 → 169 fighters | regenerated: the rows are the spectated population, and 38 fighters were missing. Axe sigma 0.52 → 0.68 and big-meal sigma 0.40 → 0.48 are within their CIs. |

  - **Committed:** only band 3 moves to 205. The other bands are kept, so the meal clock's base hazard (bands 0-1) stays valid.
  - `fit_dharok_personas.py` `MEAL_H` follows.
- **Heterogeneity at the new n** (994 fighter-duels, 169 fighters):

  | Behaviour | X² / null (p) | sd [90%] | Persistent share |
  |---|---|---|---|
  | Unfed by the next hit, < 60 HP | 1.65 (0.01) | 0.37 [0.22, 0.49] | 1.7 |
  | Healed by the next hit, 60-79 HP | 1.95 (0.005) | 0.23 | 0.58 |
  | Eat (all) | 2.15 (0.005) | 0.07 | 0.45 |
  | Swing on a ready tick | 2.62 (0.005) | 0.07 | 0.53 |
  | VW spec | 1.25 (0.06) | 0.29 | — |
  | Axe share | 5.82 (0.005) | 0.39 [0.28, 0.54] | 0.84 |
  | Orb | 11.0 (0.005) | 0.94 | 0.73 |
  | Tab | 1.31 (0.065) | 0.37 | — |
  | Big meal | 2.19 (0.005) | 0.15 | 0.90 |

  - Spec and tab are now borderline (p ≈ 0.06) but still not modelled.
  - Correlations hold: eat ~ big meal +0.51, eat ~ uptime −0.42.
  - VW spec rate goes with KOs dealt (+0.37 [0.25, 0.48]).
- **Re-measured** (`dh` harness, both seats on the Dharok script with personas, starts drawn, 6,000 fights; c-main 727192ed2 → refit):
  - KO .198 → .193 (real .251).
  - Escapes .799 → .806 (real .580).
  - Opponent HP p25/med/p75 when the low fighter is ready at 1-20 HP: 59/74/85 → 61/74/84 (real 43/59/69).
  - Low attackers (`measure_dharok_low_attack.py`, 8 ticks after a hit, bands 0-19 / 20-39 / 40-59):

    | | Swing by +2 | Axe swings | Damage per tick |
    |---|---|---|---|
    | Real | .23 / .28 / .30 | .36 / .32 / .31 | .68 / .71 / .93 |
    | Before | .20 / .20 / .19 | .29 / .28 / .19 | .57 / .74 / .92 |
    | After | .19 / .20 / .19 | .27 / .28 / .19 | .61 / .70 / .91 |

  The band-3 refit moves none of the sim targets beyond seed noise.
- Tests pass: test_riskfight_dharok_script, _mix, _mechanics, _session. The goldens are not refreshed.

## 2026-10-19: the 4-HP skip is the policy's error, not the sim's

- **Paired probe** (a2 argmax, 750 sessions, c-main 727192ed2): every decision with his HP < 40, our HP ≤ 60 and within his threat range, food allowed. Each is cloned and played out with a meal forced vs none.

  | Our HP | n | a2 eats | Skipping gains KO / adds deaths | Skipping pays only if a death costs less than |
  |---|---|---|---|---|
  | ≤ 20 | 957 | .12 | +.08 / +.086 | 0.9 kills |
  | 21-40 | 2940 | .09 | +.09 / +.067 | 1.3 kills |
  | 41-60 | 4627 | .05 | +.12 / +.019 | 6 kills |
- **The sim's harshness on a skipper is about real.** At ≤ 20 HP with him < 40 and no heal before his next hit, that hit kills .25 in the sim against .19 real when skipping (real n 53, 95% CI about .09-.29), and .022 against .017 when eating.
- **The live fight priced our death at 2.04 of his kills.** K line: him/us 0.49. We were unskulled, so the 3 most valuable items are kept.
- **a2 ignores the price there.** On live ticks 169-176, setting the price obs to 0.4, 1.0, 2.04 or 2.5 leaves food use at 0.00-0.09; our HP (4 / 30 / 59) doesn't move it either. Only his bar does.
- **Reading:** the net learned "he's low, don't eat" from the common case (41-60 HP, where it pays) and didn't separate the rarer ≤ 20 HP / high-price states where it's a losing trade. The sim and observation carry what's needed: our HP, the price, his timer, his spec, his Vengeance. A 182M-step anchor net doesn't use them here, which makes training budget, not fidelity, the next question for this behaviour.
- **a3** (anchor, 182.7M steps on acd1a9800: meal clock + 1,093-duel refit; ladder 8.64) at live ticks 173-176 (our HP 4, his bar 5):
  - Food use rises over the 4 ticks: .04, .15, .34, .32 (a2: .00-.05).
  - It now moves with the price (at tick 175: .14 at price 0.4, .34 at 2.04, .42 at 2.5) and with our HP (≈0 at our HP 30).
  - It is still below .5, so argmax still skips. The direction is right; the margin isn't there yet.
- **Fast sweep 6** (369fdba02, 20 min): the anchor (run 0, 182.7M steps, 8.64, identical to a3) is best again.
  - The five other trials are Protein's random warm-up draws, all at 5.2-5.9e7 steps, so the sweep never tested longer training.
  - Run 4 (512x1, 5.9e7 steps) scores 8.10 in 53 s. At the live 4-HP ticks it eats at .00-.02 and ignores price and our HP: worse than a3.
- **The acceptance gate is the greedy meal action, not summed eat probability.** rl_explain's "use" sums every food slot, but argmax picks the single best slot against none (`rf_net.h:97-100`).
  - The counterfactual probe now prints the chosen food/combo/drink action and the best slot's probability against none.
  - a3 at live ticks 173-176 in the actual state: no meal; best slot .03-.31 against none .66-.96. At price 2.5, .05-.42 against .57-.94: still no meal.
  - It eats on the same ticks once his bar reads 13+, and at 177 in every variant.

## 2026-10-19: the escape gap is a knockout gap; meals after a miss (branch escape-gap)

Harness: `dh` (both seats on the Dharok script, personas on, starts drawn, 6,000 fights, 3 seeds), against the 510 Dharok v Dharok duels. The sim rows are also written out as `spectator.duels` (bar = the server ratio, the sim's eat-animation visibility, swing and tab animations), so the real readers run on both unchanged. Scripts are in /tmp/eg (`classify.py` on the raw recordings, `sim2duels.py`, `gaps.py`, `ko.py`, `ends.py`, `occ.py`).

- **The harness under-counted damage.** It logged `damage_applied_this_tick`, which leaves out the orb's 10 (0.25 per tick). With `hit_damage`, damage between reads is 1.7-2.0 per tick in the sim against 1.8-2.4 real.
- **How real windows end** (raw recordings: view events, spot animations):

  | | Share of 510 |
  |---|---|
  | KO | .251 |
  | Tab animation seen | .580 |
  | Despawn showing the tab graphic (678) without the animation: missed tabs | .014 |
  | Despawn next to him, no teleport graphic (mostly short windows at full HP) | .049 |
  | Both still in view, targeting stopped | .031 |
  | Recording ended (censored) | .075 |

  - Leaving out the censored windows, real is KO .271, escape .642, or .695 if the bare despawns are tabs.
  - Windows that open before any swing (n 123) end like mid-fight ones (KO .244 / tab .569 / unseen .187), so the entry point doesn't matter.
- **The escape hazard matches; the knockout hazard doesn't.** Read the same way on both, per 10,000 fighter-ticks:

  | | Tabs | KOs |
  |---|---|---|
  | Real | 22.8 | 9.9 |
  | Sim before | 22.0 | 5.0 |
  | Sim after | 22.3 | 6.2 |

  - By HP before the KO (0-19 / 20-39 / 40-59 / 60-79 / 80+), the after sim has 92 / 47 / 17 / 2.3 / 0.5 against 117 / 47 / 32 / 4.6 / 2.1 real. The missing KOs come from 40 HP up.
- **The out-of-heals rule follows from fight length, not from extra leaving.**
  - 0.22-0.25 of sim fighters run out of food (first at tick 250, median); the rule then gives 0.39-0.41 of escapes at a median of 267 ticks.
  - Real fighters also leave when supplies run out:
    - No real window heals more than 479. 5% heal 400 or more, against 34% in the sim.
    - Real windows that run 250+ ticks from the first swing end by tab in 28 of 31.
    - Leavers had healed more than stayers (median 309 against 247).
  - The sim runs out because its fights run long, since its knockouts are rare. The sim kit holds about 520 HP of heal against a real ceiling near 450-480 `[INFERENCE: real bags aren't observed]`.
  - Who leaves: the lower fighter makes .74 of real tabs and .85 in the sim, read the same way.
- **Occupancy:**
  - **Bars 0-17:** heal per tick between reads matches.
  - **Bars 18-26:** the sim healed 1.4-2.5 times as much.
  - **By the hit that opened the gap:**
    - After damage, sim and real agree.
    - After a miss (0), the sim fed .46 / .15 / .05 of gaps at bars 18-20 / 21-23 / 24-26, against .24 / .05 / .01 real.
  - **Cause:** `fit_dharok_meals.py` fitted only gaps opened by damage, and the script applied the rate after misses too.
- **Change:** `RF_DHAROK_MEAL_PM[band][his last hit did 0]`.
  - The table is {589, 589}, {572, 572}, {468, 199}, {205, 57}, {36, 9}, {10, 12}. Below 40 there are only 13 and 40 misses, so those cells take the damage column.
  - The state is `RiskfightVisibleOpponent.last_hit_zero`, read by the script as `RF_SCRIPT_LAST_HIT_ZERO`.
  - The dead phase column (all six equal) and `riskfight_meal_phase` are removed. `fit_dharok_meals.py` now fits both columns and drops its animation-phase printout.
  - Sim after misses: .20 / .05 / .05.
- **The gap list,** real / before / after (3 seeds for the endings):
  - Escape share: .580 (.642-.695 uncensored) / .803 / .777.
  - KO: .251 (.271 uncensored) / .195 / .221.
  - Opponent's median HP when the low fighter (1-20) is ready:
    - Same reader on both: 59 / 66 / 66.
    - The old 74 used the sim's true HP against a bar read, so half the gap was the reading.
  - Time at 80+ after both have been below 80, gap estimate on both: .21 / .44 / .38. True HP: .48 / .41.
  - Fight length, median: 205 → 190 sim (real windows aren't comparable).
- **Open:**
  - Knockouts from 40 HP up: 2-4 times rarer in the sim.
  - The rest of the 80+ time: at bar 27+ the closing hit is 0-9 in .71 of sim gaps against .64 real.
- Tests pass: test_riskfight_dharok_script, _mix, _mechanics, _session and _scripts. The env_trace goldens are not refreshed.

## 2026-10-19: KOs from 40 HP up; sim Dharok fighters lost their strength boost (branch ko-high)

Harness: `dh`, both seats on the Dharok script with personas, starts drawn, 6,000 fights, 3 seeds. The sim is written in `spectator.duels` format with weapon swing animations, specials, Vengeance procs and hitsplat counts, so the same readers score real and sim. Scripts are in /tmp/eg (`big.py`, `atbar.py`, `axesz.py`, `kodump.py`, `strsz2.py`, `sim2duels2.py`).

- **Real KOs of victims read at 40+:**
  - The rate is 6.0 per 10,000 victim-ticks, against 3.1 in the sim.
  - By the attacker's swing: greataxe 2.7, VW spec 2.3 (0.99 of it with a second splat, often his Vengeance echo), the rest under 0.3 each.
  - The killing tick is often 70-112 damage in 1-3 splats:
    - a VW spec, AGS or axe together with the attacker's Vengeance echo;
    - an axe from an attacker taken down to 1-36 HP by the victim's own big hit or by orb 10s just before.
  - The victim had eaten since his last read in 8 of 72.
  - The sim already has every one of these patterns, though VW spec plus maul spec is rare: 3.5 big ticks per 10,000 real against 0.04 sim, but only 0.08 KO.
- **Big hits come as often; they kill less.**
  - Ticks of 40+ damage per 10,000: 116 real, 92 sim.
  - P(KO) per swing at a victim bar of 19-26 is 2-7 times lower in the sim for the axe and the VW spec.
- **The sim's greataxe hits are smaller,** 3-9 lower in landed mean at the same attacker bar (real 29-35 at bars 6-23).
  - Direct hits are uniform up to the swing's max, so the roll is fine.
  - 0.51 of sim axe swings came at strength below 112: on consume_pm the script repotted rarely, never under 65 HP, and its single combat potion ran out (0.68 of unboosted swings had none left).
  - Read the same way, boosted sim swings land about real sizes; unboosted ones are 4-5 lower.
  - Real fighters drink potions that heal nothing at 22 / 39 / 35 per 1,000 ticks (under 40 / 40-79 / 80 up), against 20 / 21 / 13 in the sim (`scripts/measure_dharok_potions.py`).
- **Change** (data: the hit sizes, the potion rate, and the heal ceiling of the escape-gap entry):
  - **The greataxe bag** carries one more combat potion and one more restore in place of two anglerfish (`RF_KIT_DHAROK_EXTRA_POTIONS`). Its heal drops to about 476, against a real ceiling of 479.
  - **The repot:** a Dharok fighter repots at any HP, at `RF_DHAROK_REPOT_PM` = 300 / 1000 / 1000. The sim now shows 19 / 30 / 21 potions per 1,000 ticks, and unboosted axe swings fall to 0.18.
  - **Also affected:** the learner's pool kits with an axe carry the new bag.
- **Re-measured,** real / before / after (3 seeds; same readers on both):

  | Measure | Real | Before | After |
  |---|---|---|---|
  | KO share | .251 (.271 uncensored) | .221 | .283 |
  | Escape share | .580 (.642-.695 uncensored) | .777 | .716 |
  | KO per 10,000 fighter-ticks, all | 9.9 | 6.2 | 9.7 |
  | ... by victim HP 0-19 / 20-39 / 40-59 / 60-79 / 80+ | 117 / 47 / 32 / 4.6 / 2.1 | 87 / 49 / 17 / 2.4 / 0.4 | 99 / 59 / 23 / 4.6 / 0.9 |
  | KO at victim read 40+, per 10,000 | 6.0 | 3.1 | 5.3 |
  | Tabs per 10,000 fighter-ticks | 22.8 | 22.3 | 24.9 |
  | Opponent's median HP, low fighter 1-20 ready | 59 | 66 | 63 |
  | Time at 80+ after both below 80 (gap estimate) | .21 | .37 | .34 |
- **Open:**
  - KOs at 40-59 and 80+ HP are still short.
  - VW-plus-maul spec combos.
  - The repot table is a sim-side calibration to the spectated potion animations, not a direct fit.
  - Prayer potions, if real fighters run Smite, are not modelled.
- Tests pass: test_riskfight_dharok_script, _mix, _mechanics, _session and _scripts. The env_trace goldens are not refreshed: the pool bags changed.

## 2026-10-19 night: overnight sweep restarted on the corrected opponents

- **night1** ran on 369fdba02: steps swept 1e8-2e9, at most 1800 s per trial, self-play pinned off. Its 21 trials (anchor 8.64) topped at 8.91 (run 18, 179M steps), 8.84 (runs 6 and 13) and 8.76 (run 4). Protein drew every trial at 1.0-1.8e8 steps, never near the cost cap.
- **Stopped and restarted as night2 on 31f499eaf** (the rule: restart sweeps when the env changes). Since night1's build, the opponents changed materially:
  - meals after a miss (1f90982b6);
  - Dharok fighters keep their strength boost: the greataxe bag and the repot table (dcc97daa0).

  KO share went from .195 to .283 (real .271 uncensored) and escape share from .803 to .716.

## 2026-10-19: Smite and dry prayer; where the 40-59 and 80+ KOs and the 80+ time remain (branch ko-high2)

Harness: `dh`, both seats on the Dharok script with personas, starts drawn, 6,000 fights, 3 seeds; read with the same tools on the `spectator.duels` format. Overheads come from the raw recordings (`overheadIcon`). Scripts are in /tmp/eg (`smite.py`, `top.py`, `accb.py`, `healsz.py`, `vengspec.py`, `coinc.py`).

- **Real fighters keep Smite up and never run dry.**
  - Smite is on 0.91 of Dharok fighter-ticks from the first swing, and on by 2 ticks after the first swing (median).
  - Smite goes off in 24 of 1,020 fighter-windows, for a median of 4 ticks.
  - No KO came after a dry bar. Dry prayer isn't behind the high-HP KO gap.
- **The scripts never prayed Smite.** Their overhead action was 0 (none). Only the net's seat got Smite and Piety, from the plugin.
  - Now every script keeps Smite up (`actions[RF_OVERHEAD]`), as the user said players do and the overhead data show.
  - Sim prayer still never runs dry (2.7e-5 of ticks at 0 prayer), because the restores keep up.
  - Potions that heal nothing rise from 19 / 30 / 21 to 25 / 32 / 23 per 1,000 ticks (real 22 / 39 / 35).
  - KOs, endings and occupancy don't move. Smite drains our net's prayer as it does live.
- **KOs at 40-59:** VW specs at victim bar 22-26 kill at P .038 real against .009 sim.
  - Real 70-112 killing ticks stack a VW spec or AGS with the attacker's own Vengeance echo.
  - Real VW specs land with his Vengeance proc in .079 of specs, against .026 for normal swings (3.0×). The sim shows .033 against .040 (0.8×).
  - **Tried and reverted:** a timing rule (VW spec × up to 12 when his Vengeance is up and the other side's weapon is ready). It only moves the ratio to 1.4 and leaves KOs at 40-59 at 24, because spec energy binds. The real timing needs a better cue than his attack clock. It is open.
- **80+ time:** after both have been below 80, sim fighters spend .12 of ticks at bar 27+, against .04 real.
  - These match real: entries from lower bands (meal sizes and fed shares by band within .05), damage per tick at 27+ (1.39 against 1.53), and accuracy at every other band.
  - Normal swings into victims back at bar 27+ hit at .39 real against .31 sim (n 721). This is the one mismatch found. Strips are 2% in every band, so they don't explain it.
- **Gap list after** (3 seeds; c-main before is in brackets): KO share .288 (.283); escape .712 (.716); KO per 10,000 by victim HP 98 / 61 / 24 / 4.7 / 0.9 (real 117 / 47 / 32 / 4.6 / 2.1); time at 80+ .33 (real .21); opponent's median HP when the low fighter (1-20) is ready 63 (real 59).
- Tests pass: test_riskfight_dharok_script, _mix, _mechanics, _session and _scripts. The env_trace goldens are not refreshed: script overheads changed.
- **Restarted again as night3 on 2df8c8261:** the scripts now keep Smite up, as 91% of real Dharok fighter-ticks show (fa4ab04d6). That drains our prayer as it does live.
  - night2 had reached its anchor at 9.38 (on dcc97daa0 opponents) before the restart.
  - The morning check also uses `probe_lowhp_meals` (1d3764077).

## 2026-10-19 morning: night3 results and the installed candidate n3r33

- **night3** (2df8c8261, 60 trials, self-play off, steps swept 1e8-2e9):
  - Protein drew every trial at 1.0-2.1e8 steps. The `time`-scaled step parameter plus cost-aware selection keeps it near the floor.
  - Best scores: 9.69 (run 52), 9.69 (33), 9.63 (42), 9.53 (57); anchor 9.19.
- **Morning check** of the top 4. Probe columns: argmax net_stake per hour; `probe_lowhp_meals` at 300 sessions, greedy meals, our HP ≤ 20 with him < 40:

  | Run | Dharok / VW | Strips per fight | ≤20 greedy eats | Greedy picks better branch |
  |---|---|---|---|---|
  | 52 | 9.10 / 8.97 | 1.48 | .27 | .55 |
  | 33 | 8.86 / 9.63 | 0.00 | .40 | .54 |
  | 42 | 9.57 / 9.96 | 0.96 | .21 | .21 |
  | 57 | 8.59 / 8.84 | 0.94 | .20 | .48 |

- **Live 4-HP ticks (173-176), greedy actions in the actual state:**
  - Run 33 eats on all four, best slot .36-.55 against none .00-.22.
  - With our HP set to 4 on the earlier ticks it is mixed: it eats at 169 and 172, but not at 170 (.16 against .64) or 171 (.09 against .76).
  - It doesn't eat at the actual 59 HP on 169-172, where the sim says skipping pays below price 1.74.
  - Run 52 skips all four.
- **Installed: n3r33** (512x1, 150M steps, C 467413f58). Replays of 4 live sessions run clean.
- **Timestep-only sweep night4** (run 52's other hyperparameters fixed, steps 1.4e8-1.5e9) was started after the advisory rejected a hand-picked 600M run. Its random draws also stay at 1.4-1.8e8. Scores range 8.79-9.69 at fixed hyperparameters, so trial-to-trial noise is about ±0.4: single-trial gaps under about 0.5 don't rank nets.
- **Correction (the user's): the claim that sweeps "never test long training" because Protein searches locally is retracted.** What the evidence supports:
  - The random warm-up draws are fixed by the sweep seed: night4, night5 and night6 drew identical step counts whatever the anchor or `scale`.
  - In its model-guided phase, Protein chose 1.0-2.1e8 steps over about 50 trials across night3 and night4. night6's 8e8 anchor scored 9.44, inside the 8.8-9.8 noise of the short runs, so longer training didn't pay and Protein's choice of short runs is consistent with that.
  - Nothing here shows a structural limit. **night6:** timestep-only, run 52's other hyperparameters, anchor 8e8, range 1.4e8-1.5e9.
- **night6 result: more training doesn't clearly help.** Protein's random warm-up draws were the same step counts as night4's whatever the anchor, so only the anchor ran long.
  - Anchor at 8e8 steps (956 s): ladder 9.44, against 8.79-9.80 for the same hyperparameters at 1.4-1.9e8. Probe 8.24 Dharok / 9.08 VW, strips 0.01.
  - `probe_lowhp_meals` at ≤20 HP: greedy eats .37, picks the better branch .66. n3r33: .40 / .54.
  - Live 4-HP ticks: eats at 173 and 176, skips at 174 and 175 (.47 against .52 at 175), and reacts to the price.
  - Neither net dominates on these tests. n3r33 stays installed: a better ladder score and probe, no strips, eats on all four live ticks.
  - The break-even price differs by net (≤20: 1.17 for run 52, 0.83 for run 33, 2.40 for the 8e8 net). The probe's playouts continue with the net under test, so "the better branch" depends on how well that net finishes either branch.

## 2026-10-19: reacting to his swing on PID; the PID block (branch pid-obs)

- **The mechanic holds in the sim** (`test_riskfight_pid_react`). We hold PID and he swings a greataxe or a Voidwaker special on tick T: his hit lands on T + 1. A Vengeance cast on T + 1 resolves before that hit lands (Fact 3), so it reflects the hit. If he holds PID, the hit lands on T, before any decision of ours sees the swing, and a cast on T + 1 reflects nothing.
- **What the net saw on the decision tick before this change:**
  - The history row of T: his swing, its weapon and its style.
  - His energy estimate (session [1]), which drops on the swing's tick for a special. Live it drops when `swing_spec` arrives, which can be a tick late.
  - Not whether the hit is still in the air, and not who holds PID: no observation writer read PID.
  - Timing [2] `his_next_tick_max` never counts the swing already started. After a swing `his_ready_min` is the weapon's speed, so the opening hit is dropped and only instant maul specials and his echo remain.
- **The PID block** (4 floats, `RF_PID_START` = 492, `RF_OBS_SIZE` 492 → 496; nets of 492 inputs, n3r33 included, do not load). It is written by the shared writer for the sim and the live server:
  - [0] the chance we hold PID;
  - [1] the chance his melee swing of the tick just played lands on the next tick (equal to [0] when there is such a swing, else 0);
  - [2] that swing's max, or its spec max when it was a special (threat-block scale);
  - [3] 1 when it was a special.
- **The PID reading follows the plugin's rule (light `Pid.java`).** The latest melee pairing of an attack animation with its hitsplat sets the owner: on the same tick, the attacker holds PID; on the next tick, the target does. Flip odds come from the age of the reading: .119 up to 3 ticks, .178 for 4-7, .221 beyond (measured on 33 recorded fights). The reading is UNKNOWN until the first melee pairing. Instant maul specials and anything that could be an echo don't pair.
  - The sim's viewer reads its own swings by the same rule (`riskfight_pid_seen`).
  - The live server reads the `B` line's owner and flip. Before this change the live state's PID was a coin flip: `snapshot_of` sends '?'.
- **Limits.** A swing that landed on its own tick reads .119 on [1], not 0, because the wire doesn't say whether his hitsplat on us came on the swing's tick. An optional `V` field for that would make [1] exact. A `swing_spec` that arrives a tick late leaves [3] at 0 on the decision tick.
- **Parity** (`test_rl_pid_obs_parity`): sim fights are replayed into `rt_observe` as `V` and `B` lines. Over 6 fights, every tick has the same PID block, the same newest history row and the same energy estimate live and in the sim.
## 2026-10-19: swing first or eat first when low, a persona trait (branch persona-risk)

Data: `spectator.duels` (510 Dharok v Dharok duels, 994 fighter-duels, 169 fighters with events). Sim: `dh` harness (both seats on the Dharok script, personas on, starts drawn, 6,000 fights, 3 seeds), written as `spectator.duels` with the persona as the fighter's name, so `scripts/fit_dharok_personas.py` reads real and sim the same way. Scripts in /tmp/personarisk (`dh.c`, `truth.py`, `bypersona.py`, `ends.py`).

- **New behaviours in `fit_dharok_personas.py`:**
  - order: after a hit leaves his bar at 1-11 (under 40) with his own clock at 0-2 at the next tick, a meal before his next swing. A meal is an eat animation whose gap of fresh reads shows a heal of 8+, so potion sips don't count (the visibility correction of `fit_dharok_meals.py`). Cell: his clock. order_ready: clock 0 only.
  - hidden_meal: a swing of his at bar 1-11 whose gap shows a heal of 8+ with no eat animation in it (a meal on the swing tick, or one hidden by a hit or carry).
- **Heterogeneity** (real; X² over the null, p; sd of a fighter's multiplier [90% CI]; persistent share [90% CI]):

  | Behaviour | Events | X² / null (p) | sd, fighters | Persistent share |
  |---|---|---|---|---|
  | Eat first, < 40, clock 0-2 | 1137 | 1.55 (0.01) | 0.24 [0.13, 0.39] | 0.36 [0.00, 1.06] |
  | Eat first, < 40, clock 0 | 506 | 1.32 (0.01) | 0.33 [0.01, 0.56] | 0.39 |
  | Eat first, < 50, clock 0-2 | 2297 | 1.82 (0.005) | 0.21 [0.15, 0.28] | 0.49 [0.23, 0.88] |
  | Hidden meal on a low swing | 706 | 1.30 (0.03) | 0.31 [0.18, 0.43] | unstable (3.7) |

  - Pooled: eat first .47; a hidden meal on .31 of low swings.
  - Correlations (EB multipliers): eat first goes with fast eating (eat +0.29, unfed below 60 −0.42) and against uptime (−0.32). It is part of the careful/aggressive axis, not separate from it. Axe use: −0.06; hidden meal: −0.22.
  - Outcomes: fighters who eat first escape more (+0.17 [0.01, 0.32]). There's no clear link to KOs dealt (+0.04) or deaths (−0.06 to −0.10, CIs span 0).
- **Trait EAT_FIRST** (`RF_PERSONA_EAT_FIRST`, a fifth persona column). It scales `RF_DHAROK_MEAL_CLOCK` clocks 0-2, and clock 3 up keeps the rest of the cycle's 4000. A fighter who swings first eats right after his swing instead, and eats as much per cycle.
  - Fit: a random effect on the logit of the clocks-0-2 share of the cycle (so it is bounded at 1 / 0.287). It is fitted on top of his EAT trait (the script applies both), then spread back as the other traits are.
  - **The sim under-expresses it.** Read the same way, the sim's eat-first rate moves 0.22-0.29 per unit log exponent, against about 0.7 for a clean hazard. That holds in the true state too (`truth.py`), so it isn't the reader. The direct fit (p10/p90 0.73/1.25) gave a sim fighter sd of .09.
  - The spread is calibrated in the sim: `ORDER_GAIN` 3 (p10/p50/p90 0.38/0.94/1.89) gives .16 (gain 2: .14), against real .24 per fighter (.22 persistent). The bound saturates beyond that.
- **Validation** (c-main personas → persona-risk, 3 seeds):
  - Pooled targets stay:
    - KO .288 → .292.
    - Escape .714 → .711.
    - Eat first .462 → .451 (real .47).
  - Low attackers, bands 0-19 / 20-39 / 40-59: swing by +2 is .20 / .20 / .20 in both arms (real .23 / .28 / .30); axe swings .30 / .29 / .22 in both (real .36 / .32 / .31).
  - Spread: eat first, fighter sd .06 → .16, persistent share .07 → .22 (real .24, .36).
- **Personas more broadly:**
  - Read the same way, the sim under-disperses every persona trait: fighter sd eat .02 (real .07), big meal .04 (.15), axe .18 (.39), unfed below 60 .13 (.37).
  - The spread-back targets the latent sigma, but the script's other paths dilute it: potions, food timers, running out of food, combos.
  - So each trait's spread should be calibrated in the sim as EAT_FIRST is here.
  - Real spread that is still pooled:
    - Hidden meals: sd .31.
    - Uptime: .07; the script always attacks, so there is no table to scale.
    - Tab by HP: p .11.
    - VW spec rate: p .06.
    - The orb spread (.94) is already in the kit carry.
  - Whole vectors are the right unit: the traits correlate (eat ~ big meal +0.51, eat first ~ unfed −0.42, ~ uptime −0.32), and drawing them independently would lose that.
- Tests pass: test_riskfight_dharok_script, _mix and _scripts. The env_trace goldens are not refreshed: Dharok pool opponents' meal rolls change.

## 2026-10-19: persona spread calibrated in the sim (branch persona-spread)

Harness: `dh` (both seats on the Dharok script, personas on, starts drawn, 6,000 fights, 3 seeds), written as `spectator.duels` with the persona as the fighter's name and read by `scripts/fit_dharok_personas.py`, the same reader as the real data. Scripts in /tmp/pspread (`cal.sh`, `run.sh`).

- **Committed:** `GAIN` in `fit_dharok_personas.py`, one per trait: order (eat first) 3; eat, big_meal and axe 1.
  - A trait's spread back is multiplied by its gain, then its mean is shifted so the fights-weighted event rate over the pooled cells stays as at gain 1.
  - A trait fitted on top of another (order on eat) takes that trait's gain-1 value.
  - The table is now `uint32_t`, so a gained axe tail (past 65,535 per mille) fits.
- **Committed arm against c-main 00171508c** (3 seeds each; c-main / committed / real):
  - KO share: .288 / .292 / .25-.27.
  - Escape share: .714 / .711 / .58-.64.
  - Unfed by the next hit, < 60: .088 / .089 / .070.
  - Healed by the next hit, 60-79: .427 / .426 / .43.
  - Low attackers, bands 0-19 / 20-39 / 40-59: swing by +2 .20 / .20 / .20 and axe swings .30 / .29 / .22, the same in both arms.
  - All of these are within seed noise (the KO sd per seed is .004).
  - Eat-first spread: .07 → .17 (real .24 [.13, .39]).
- **Spread the gains reach, not committed** (fighter sd of the multiplier; real [90% CI] / sim at gain 1 / sim with eat 2, big_meal 3, axe 3, order 3):

  | Trait | Real [90% CI] | Gain 1 | Gained |
  |---|---|---|---|
  | Eat (healed by the next hit, bands 0-4) | .07 [.05, .08] | .02 | .04 |
  | Unfed by the next hit, < 60 | .37 [.21, .52] | .13 | .24 |
  | Healed by the next hit, 60-79 | .23 [.17, .30] | .05 | .10 |
  | Big meal | .15 [.12, .19] | .04 | .11 |
  | Axe share | .39 [.28, .54] | .18 | .46 |

- **What those gains do to the pooled targets** (c-main → gained):
  - KO .292 → .307; escape .711 → .695.
  - Unfed below 60 .089 → .097; healed at 60-79 .426 → .451.
  - Low-attacker axe swings .30 / .29 / .22 → .27 / .26 / .19.
  - Big meal at gain 5 (sd .15) raises the KO share to .334.
  - Axe at gain 3 on its own:
    - KO .288 → .278 and low-attacker axe swings → .25 / .25 / .20.
    - A log shift of +0.14 restores the per-swing axe share (.155 → .167), but not those two.
- **Why one shift isn't enough:** a wider log-scale multiplier lowers the mean rate where the base rate is high and raises it where the base rate is low:
  - Eat: feeding falls under 60 and rises at 60-79.
  - Axe: at bar under 12, where `RF_AXE_SWING_PM` is 509-755 per mille, axe swings fall.
- **Next step:** a shift per cell (meal band, axe cell) that holds each cell's sim rate. Then eat, big_meal and axe can take the gains above.
- **Not added:**
  - Hidden meals: real sd .31, but no script table makes a meal on the swing tick.
  - Tab by HP: p .11 on the same 994 fighter-duels.
  - Uptime: no table.
- Tests pass: test_riskfight_dharok_script, _mix and _scripts. The env_trace goldens are not refreshed.

## 2026-10-19: spectated Dharok kits for our seat, and the spec-chain floats (branch dharok-kits)

- **Why.** Our Dharok kit wields the Voidwaker, so a special is one toggle. Many spectated Dharok fighters wield a tentacle instead and switch to a Voidwaker or an Armadyl godsword for the special, often with a granite maul to follow it. The maul's low accuracy hides what the chain is worth in raw numbers.
- **Setups** (`scripts/measure_dharok_play.py` (g): the 1,020 fighters of the 510 Dharok v Dharok duels; main weapon = most ticks in hand, the axe and mauls aside; the spec weapons and the maul are the ones seen wielded, so the bag shares are lower bounds):

  | Main | Spec weapon | Maul seen | Share |
  |---|---|---|---|
  | Voidwaker | - | no | 0.341 |
  | tentacle | Voidwaker | yes | 0.208 |
  | Voidwaker | - | yes | 0.118 (the user's kit) |
  | tentacle | - | no / yes | 0.063 / 0.058 |
  | tentacle | Voidwaker | no | 0.043 |
  | tentacle | AGS | yes / no | 0.021 / 0.004 |
  | other (scimitar, abyssal dagger, ...) | | | 0.144 |

  Tentacle + Voidwaker is 0.251, Voidwaker wielded 0.459, tentacle + AGS 0.025. The raw ids 29607 (Voidwaker), 26484 (tentacle) and 29605 (AGS) are variants that the item table already maps.
- **Three kits for our seat** (`RISKFIGHT_DHAROK_SPECTATED_LOADOUT`, riskfight_model.h). Each is the user's Dharok kit with the tentacle wielded and the Voidwaker in the bag:
  - tentacle + VW + maul: the Voidwaker takes the last marlin's cell (9 marlin);
  - tentacle + VW: the Voidwaker takes the maul's cell (10 marlin);
  - tentacle + AGS + maul: the godsword takes the last marlin's cell.

  Same armour, defender, axe, orb, Vengeance and tab. All 28 cells are full, the class is Dharok and the base Defence is 99.
- **Draw: `live_loadout 5`** (`RF_KIT_MIX_DHAROK_SPECTATED`). It is live_loadout 4, but a Dharok draw takes a spectated kit, evenly, with the chance `env.dharok_spectated_share` (0.5, `RF_DHAROK_SPECTATED_SHARE`). Measured over 120k draws: user's Dharok 0.249, each spectated kit 0.083-0.085, VW kits 0.499. live_loadout 4 draws the same as before, rng included, and never yields a spectated kit. `scripts/sweep_riskfight_live.sh` now trains on 5.
- **The ladder still scores the user's kits.** `[bot_eval]` keeps live_loadout 4, so `bot_ladder_perf` stays comparable with earlier sweeps.
- **Live.** `SIM_IDS` gains 12006 (tentacle) and 11802 (AGS). Before this, a godsword in our bag was inert filler on the server. `test_riskfight_dharok_spectated` feeds each kit as a request, with a full bag and with a free cell:
  - the cells match the kit's;
  - opener wield + special + attack becomes CLICK_SLOT / SPEC_TOGGLE / ATTACK;
  - the godsword, axe and maul wields are offered only with a free cell and are a CLICK_SLOT on their slot.

  In the sim, the same test arms and spends the opener's 50, then the maul's special on the next decision (energy 0). Not checked: the plugin's (light) handling of a godsword or tentacle kit.
- **`session_probe`** with live_loadout 5 prints the same per-kit lines for each spectated kit, plus per fight: opener specials (VW or AGS), maul special events, and chains (a maul special within 3 ticks of an opener). Checked on a zero-weight net only; no 499-input checkpoint exists yet.
- **Spec-chain floats** (tactics [31..33], `RF_TACTICS_KO_AGS_SPEC` = 31, `RF_TACTICS_CHAIN` = 32; RF_TACTICS_SIZE 31 → 34, `RF_PID_START` 492 → 495, **`RF_OBS_SIZE` 496 → 499**; nets of 496 inputs do not load). They are computed from the OURS rows by the shared writer, so the sim and live get the same value. His hitpoints are uniform over the timing block's bounds, as for the `ko` floats:
  - [31] an AGS special alone (spec_hit, uniform 0..spec_max);
  - [32] a VW special (uniform max/2..spec_max, always hits) plus one maul special (spec_hit, uniform 0..spec_max), damage summed, exact convolution (`riskfight_ko_chain`);
  - [33] the same with the AGS opener.

  What they ignore:
  - His meal between the two. The maul lands on its click tick, and the opener lands a tick after its swing without PID, so the two often land on one tick.
  - Our energy, as with ko [25].
  - A second maul special after the opener: it would need 150 energy.

  The existing `maul2` (ko [25]) is two maul specials on one tick (100 energy) and no opener. `riskfight_ko_chance`'s two-attack path is now `riskfight_ko_chain` of an attack with itself, the same arithmetic.
- **Example** (`test_riskfight_tactics_obs`, his bar 12: hitpoints 38-40, our HP 99). User's kit and tentacle+VW+maul: VW special 0.432, maul 0, maul2 0.019, **VW + maul 0.518**. Tentacle+AGS+maul: AGS special 0.202, **AGS + maul 0.236**. Hand-checked against `riskfight_ko_chain(1, 2, 3, .5, 0, 1, 4, 4)` = 0.125 and an enumeration of both rolls.
- **Tests:** test_riskfight_dharok_spectated (new), test_riskfight_tactics_obs, _mix, _session, _mechanics, _dharok_kit, _unequip, _pid_react, _threat_masks, test_rl_pid_obs_parity, test_threat_live, test_threat_features, test_rl_live_pies all pass on the Mac. The `test_riskfight_env_trace` goldens are left to refresh (the observation grew).

## 2026-10-19 midday: candidate p1 (PID block, spec-chain floats, tentacle kits, eat-first personas)

p1 = night3 trial 33's hyperparameters, 150M steps on 4f8d314cb, `live_loadout` 5. Ladder 8.98, scored on `live_loadout` 4; not comparable with earlier envs.

- **Per-kit `session_probe` (argmax):**

  | Kit | Net per hour | Opener specials per fight |
  |---|---|---|
  | Dharok (the user's) | 9.39 | 1.65 |
  | Tentacle + VW + maul | 9.25 | 1.71 |
  | Tentacle + VW | 8.79 | 1.75 |
  | Tentacle + AGS + maul | 5.83 | 1.32 |
  | VW | 9.37 | 1.69 |

  Maul specials, chains, axe swings and orb clicks are 0 in every kit. Strips ≤ 0.04.
- **`probe_lowhp_meals` (we're low, he's < 40):**

  | Our HP | p1 greedy eats | Eating vs skipping | p1 matches the better branch |
  |---|---|---|---|
  | ≤ 20 | .31 | eat always (KO .39 / death .03 against .38 / .12) | .31 |
  | 21-40 | .19 | skip below price 1.71 | .75 |
  | 41-60 | .11 | eat always | .11 |
- **Live 4-HP ticks:** eats at 175 and 176, not at 173 or 174 (best slot .15 / .07 against none .57 / .87). n3r33 ate on all four.
- **persona-cells** (e12af34e2): per-band persona factors at gain 1, so they change nothing yet. The branch stays unmerged until a per-band and per-clock eat factor keeps feeding, eat-first and KO together.
- **Correction:** p1's zero maul use and the per-kit scores describe this policy, not the value of the maul or of a kit. p1's own ≤ 20 HP probe shows it skipping a dominated choice: eating gives more KOs and fewer deaths, yet it mostly skips. So non-use is no evidence that an action doesn't pay. The maul's value is unestablished until paired rollouts from eligible states (double maul, maul now, opener then maul, against the net's own choice) measure it: `probe_maul_chain`, in progress.
- **Maul value, paired** (`probe_maul_chain`, 5ac4b2fd6; p1, 2 × 100 sessions; each line minus p1's own choice on the same decisions and clones):
  - Double maul now: −.13 to −.55 stake. One maul special now: lower in 11 of 12 cells.
  - Opener then maul: within −.09 to +.03.
  - The chain as the user plays it, with the maul's KO gate skipped: likewise, −.10 to +.03. The gate isn't what stops the chain from paying.
  - The only gain is +.03 ± .015, in the tentacle+VW+maul kit with his bar above 70%.
  - So in this sim, against these opponents, forcing the maul doesn't beat p1's continuation: this is a value result, unlike p1's non-use. It is only as faithful as the opponents' behaviour after a big hit.
- **The user's Dharok kit can't wield the maul in most states:** the 2h rule needs a free cell for the defender, and the bag is full, so the wield was blocked in 6091 of 7460 decisions with his bar above 70%. The user will leave one cell free live (2026-10-19); the sim kit should match.
- **p2** (p1's config, the 2026-10-19 Dharok bag with one cell free): ladder 8.93. Probe net per hour: Dharok 8.33, tentacle+VW+maul 8.17, tentacle+VW 8.26, tentacle+AGS+maul 4.52, VW 7.77. Maul specials 0. ≤ 20 HP greedy eats .13 (matches the better branch .62).
  - **Correction (advisory):** p1 → p2 is not a seed-noise measurement. p2's environment differs: one halibut fewer, two-handers unlocked, a free cell in the tentacle kits, and therefore the training distribution. p1's numbers were measured on its own environment.
  - The like-for-like checks: p1 evaluated on p2's environment (same probe, same seeds); and seeds 74-77 of p2's config on p2's environment, which measure seed variance.

## 2026-10-19: how n3r33's fights end (session_probe fight-ending table)

`session_probe` now prints each fight ending (our tab, his tab by his kit, our death, our kill) with the state at the start of its tick. n3r33 on its own env (night3 repo, `live_loadout` 4, pool, recorded starts, price table, argmax), seeds 7-9 × 200 sessions, 4,228 fights:

| Ending | Per fight | Actor HP mean (0-19 / 20-39 / 40-59 / 60-79 / 80+) | Other HP mean | Fight tick < 50 / 100+ | Actor lower | Food actor / other |
|---|---|---|---|---|---|---|
| Our kill | .672 | 20 (.54 / .26 / .17 / .03 / 0) | 69 | .63 / .17 | .96 | 9.4 / 17.7 |
| His tab, Dharok kit | .118 | 35 (.31 / .38 / .15 / .09 / .07) | 78 | .26 / .53 | .92 | 3.7 / 13.3 (none .50) |
| His tab, other kit | .171 | 49 (.19 / .28 / .15 / .17 / .21) | 76 | .31 / .58 | .80 | 5.4 / 14.7 (none .36) |
| Our tab | .023 | 61 (.07 / .12 / .27 / .34 / .20) | 73 | 0 / .99 | .69 | 2.1 / 3.6 (none .19) |
| Our death (mutual included) | .019 | 26 (.44 / .21 / .23 / .11 / 0) | 29 | .61 / .21 | .56 | 16.5 / 10.4 |

- Per 3,000-tick session: 4.72 kills, 0.13 deaths (mutual included), 0.16 our tabs, 2.03 his tabs (0.23 ragged). Dharok kit 4.43 kills / 0.10 deaths, VW kits 4.96 / 0.16.
- Observed distributions, not causes: 95 of our 96 tabs fall at tick 100+ (one at 50-99), with 2.1 food items left on average (.19 none). Our deaths fall early (.61 before tick 50) with 16.5 food left; .44 start their tick at 0-19 HP.
- **Joint columns** (added after an advisory: "no food" counts eat items only, while the scripts' forced tab needs no eat item, no brew dose and HP < 30):

  | Ending | Under 30, no heals (the forced-tab state) | Under 40, heals left |
  |---|---|---|
  | Our tab | 0 | .19 |
  | His tab, Dharok kit | .38 | .29 |
  | His tab, other kit | .26 | .18 |
  | Our death | 0 | .66 |
  | Our kill (his state) | .04 | .74 |

  - .38 of Dharok-kit tabs come in the forced state, not the .50 the eat-item column suggested. The rule tabs at once in that state, so those are its tabs `[INFERENCE: the per-tick roll could fire on the same tick]`.
  - All our tabs under 40 HP (.19) had heals left, so n3r33 does tab at low HP with food left. Every death under 40 HP had heals left.
- Real duels between players end by KO .25-.27 (either side). The net KOs .67 of its sim opponents: the net's edge over the fitted scripts is the sim's claim, still unmeasured live.

## 2026-10-19: the risk ledger, real against sim (how much death chance fighters accept for a kill chance)

Tools:
- `scripts/measure_risk_ledger.py`, one reader for real and sim duels.
- `ocean/osrs/tests/risk_laws.c`: the engine's viewer threat math (`osrs_pvp_threat.h`) per weapon and composition.
- `probe_risk_duels.c`: a net against the env's opponents, written as duels.
- `probe_script_duels.c`: the Dharok script on both seats, with personas and recorded starts; the dh harness.
- Both probes share `riskfight_duel_writer.h`. The env's new `Env.fight_end` hook lets them read a fight's last tick before the env moves on (NULL in training).

How it's read:
- Per fighter-tick, **K** is his best one-attack knockout chance over the weapons he is seen with: normal hits, plus single-hit specials while his estimated energy allows. **D** is the other side's K against him.
- Hitpoints come from `gap_hitpoints`.
- Not counted: Vengeance, recoil, two hits landing on one tick.
- A decision point is the tick his clock frees after a swing; the reader records what he does first in the next 4 ticks.

Corpora:
- Real: 510 Dharok v Dharok duels.
- Scripts v scripts: 5,995 fights, 3 seeds, c-main.
- n3r33 against the env's opponents: 1,950 Dharok v Dharok fights, 6 seeds, night3 env.
  - All of these are script opponents. The oppfit model never appears in a Dharok v Dharok fight.

| | Real v real | Scripts v scripts | Scripts v n3r33 | n3r33 |
|---|---|---|---|---|
| Fighter-ticks at D ≥ .25 | .058 | .050 | .094 | .027 |
| At D ≥ .25, decision points: swing / meal / none first | .70 / .21 / .08 | .65 / .19 / .14 | .64 / .16 / .18 | .72 / .16 / .12 |
| At D ≥ .25 and own K ≥ .10: swing first | .65 (n 387) | .62 (n 3,755) | .49 (none .31) | .76 |
| Per fighter, swing first at D ≥ .10: p10 / median / p90 | .54 / .68 / .84 | .62 / .69 / .75 | .58 / .70 / .79 | .73 |
| Per fighter: SD beyond sampling noise | .067 (57 fighters) | .019 (168 personas) | .019 | - |
| Mean D / K at a swing | .038 / .045 | .036 / .042 | .051 / .019 | .018 / .066 |
| Axe swings per 1,000 ticks at own HP < 20 | 87 | 62 | 31 | 0 |

- **On average the scripts take about real risk.** Under threat they swing first .65 against .70 real, and the death and kill chances at their swings match (.036 / .042 against .038 / .045).
- **The spread is missing.** Real fighters differ in risk appetite about 3.5 times as much as the personas, beyond sampling noise. Both the aggressive tail (p90 .84 against .75) and the cautious one (p10 .54 against .62) are thin in the sim. The personas' traits (eat, big meal, axe, eat-first) don't carry this.
- **Desperate axe swings:** the scripts swing the axe at HP < 20 at 62 per 1,000 ticks, against 87 real.
- **Against n3r33 the scripts barely threaten.** Their kill chance at a swing is .019, against .042 against each other. In mutual danger with a kill chance they swing first only .49, with .31 "none" (no swing, shown meal or tab within 4 ticks).
  - The net keeps its own D low: .018 at its swings, and .027 of its ticks at D ≥ .25.
  - What the scripts do in those "none" windows is not yet known (hidden meals, a clock pushed back by a meal, the net out of reach).
- Side observation: degraded Dharok pieces (helm/body/legs 75: 4881, 4893, 4899) and a few cosmetic items have no item row. They are 0.7% of worn slot-ticks in the corpus. A viewer reading an opponent in them misses his set effect, live too.
- **Correction (advisories): the "3.5 times the spread" claim is retracted.** It subtracted iid binomial noise, but a fighter's decision points cluster in duels, and fighters face different mixes of D and K.
  - Re-measured with `split_half` in the ledger: residuals against the group's D × K cell rates, each fighter's duels alternated into two halves, the covariance of the halves across fighters.
  - Swinging first under threat (D ≥ .10) that repeats across duels: real SD .043 [90%: 0, .076] over 48 fighters, split-half r .09; scripts v scripts .020 [0, .034], r .08.
  - Real players' risk appetite on this measure is not shown to vary beyond chance. It is not a calibration target.
- **What holds, with duel-resampled intervals: the greataxe at low true hitpoints.** At own HP < 20, swings per 1,000 ticks are alike (real 127 [119, 136], sim 139), but the axe takes .69 [.65, .73] of real swings against .44 [.43, .46] in the sim.
- Axe share of non-special swings, by own HP (gap estimate) and the opponent's, real [90% over duels] (n) against scripts v scripts:

  | Own HP | Opp < 40 | Opp 40-79 | Opp 80+ |
  |---|---|---|---|
  | < 20 | .78 [.68, .85] (58) / .59 | .75 [.70, .81] (162) / .47 | .89 [.81, .95] (55) / .42 |
  | 20-39 | .79 [.72, .86] (128) / .57 | .64 [.60, .68] (490) / .41 | .61 [.54, .66] (140) / .37 |
  | 40-79 | .46 [.42, .49] (591) / .34 | .17 (9145) / .19 | .10 (3329) / .15 |
  | 80+ | .20 [.14, .26] (157) / .20 | .07 (3466) / .11 | .02 (5142) / .09 |

  - Real fighters key the axe on their own current HP, which they know. Low with the opponent healthy, they take it on .89 of swings.
  - The script keys it on its own bar at its last hit (`RF_SCRIPT_OWN_BAR`). A fighter who has eaten back up keeps the low band, one still low gets no more axe, and the healthy ones get too much. The old table matches real by that key (`measure_dharok_play.py` (d) on the sim duels: bar 0-11, 601 against 633 real) but misplaces the axe in true HP.

## 2026-10-19: the Dharok script's greataxe keyed on its own hitpoints (branch axe-true-hp)

- **Validation first:**
  - The gap estimate of HP was checked against the sim's truth at swing ticks (a truth-writing build of `probe_script_duels`). It is close at low HP (mean error +1, 1-2% off by 20+), but low cells pick up higher-HP swings, so it is not used for the axe.
  - A fresh read (his bar at a hitsplat in the 3 ticks before the swing, no meal of his shown since) gives the sim's true-HP table within .02-.03.
- **Real against sim on the fresh read** (axe share of swings without a special, by his HP and the other side's bar <12 / 12-23 / 24+):

  | His HP | Real | Sim before |
  |---|---|---|
  | < 20 | .91 / .87 / .80 | .64 / .64 / .50 |
  | 20-39 | .95 / .70 / .43 | .70 / .53 / .45 |
  | 40-79 | .60 / .22 / .07 | .36 / .19 / .12 |
  | 80+ | .25 / .06 / .02 | .20 / .09 / .07 |

- **Change:** `RF_AXE_SWING_PM[4][2][3]` is keyed on his own HP (0-19 / 20-39 / 40-79 / 80+), Vengeance up, and the other side's bar (<12 / 12-23 / 24+).
  - Calibrated in the sim with `scripts/fit_dharok_axe.py`: an odds-ratio fixed point, 5 steps, script on both seats, 6,000 fights per step.
  - The profile's axe propensity now multiplies the odds, not the share. On the share, the keen capped at 1000 and the rest pulled the low bands under the spectated .87-.95.
  - `RF_SCRIPT_OWN_BAR` (the script's own bar at its last hit) is removed; nothing else read it.
- **After:** every cell within .03 of real, except 80+ against a bar of 24+ without Vengeance (.036 against .012, at a table value of 0).
  - Pooled under 20 HP: .81 [.79, .82] against .88 [.84, .92] real (.60 before).
  - The rest is the state mix. A real fighter under 20 swings at a bar under 12 on .46 of his swings, a sim one on .21: the known gap where the sim's low fighter faces a healthier opponent.
- **Fight outcomes, script v script:** KO share .288 → .294, tab .712 → .706. Fights end the same way; the axe now comes when the swinger is truly low.
- **Personas:** the axe trait (`fit_dharok_personas.py`) is a fighter's odds within the old cells. The script applies it to the new table as a propensity, so it was not refitted.
- Tests: `test_riskfight_dharok_script` waits for the axe roll after a strip (the odds are never certain) and checks the new cells. Goldens refreshed.

## 2026-10-19: seed spread at a fixed config, and p1 on p2's environment

Same config and env (p2: the 2026-10-19 bag), 150M steps, five seeds. Argmax `session_probe` (300 sessions, `live_loadout` 5), net stake per hour, and `probe_lowhp_meals` at ≤ 20 HP:

| Net | Ladder | Dharok | tentacle+VW+maul | tentacle+VW | tentacle+AGS+maul | VW | ≤ 20 greedy eats / matches better branch |
|---|---|---|---|---|---|---|---|
| p2 (seed 73) | 8.93 | 8.33 | 8.17 | 8.26 | 4.52 | 7.77 | .13 / .62 |
| seed 74 | 8.94 | 9.01 | 9.52 | 10.07 | 5.15 | 9.31 | .31 / .46 |
| seed 75 | 9.20 | 8.96 | 10.37 | 8.50 | 6.63 | 9.20 | .37 / .45 |
| seed 76 | 8.25 | 7.93 | 8.21 | 8.01 | 5.29 | 8.17 | .49 / .51 |
| seed 77 | 7.64 | 6.59 | 5.59 | 8.22 | 4.30 | 7.08 | .49 / .49 |
| p1 (bag full) on p2's env | - | 8.86 | 8.23 | 9.78 | 5.99 | 8.49 | .33 / .41 |

- Seeds alone move the Dharok kit's probe from 6.6 to 9.0 and the ladder from 7.6 to 9.2. p1 measured on p2's environment sits inside that range, so the bag change and p1 against p2 are not separable from seed noise at one seed each.
- Low-HP greedy eating varies .13-.49 by seed alone. Picking a net on one gate selects a seed's luck: candidates need several seeds, judged together on the gates.

## 2026-10-19: p3, three seeds against the rekeyed axe (c-main d09d98948)

p2's config, 150M steps, seeds 81-83. Argmax `session_probe` (300 sessions, `live_loadout` 5), net stake per hour, and `probe_lowhp_meals` at ≤ 20 HP:

| Seed | Ladder | Dharok (deaths per session) | tentacle+VW+maul | tentacle+VW | tentacle+AGS+maul | VW | ≤ 20 greedy eats / matches better branch / break-even |
|---|---|---|---|---|---|---|---|
| 81 | 8.02 | 9.00 (.17) | 7.43 | 7.58 | 4.92 | 8.91 | .33 / .33 / eat always |
| 82 | 9.47 | 9.92 (.07) | 8.59 | 8.37 | 4.80 | 9.13 | .27 / .27 / eat always |
| 83 | 8.83 | 8.73 (.15) | 8.54 | 9.15 | 5.47 | 9.12 | .13 / .42 / skip below 0.69 |

- At ≤ 20 HP eating is now the better branch at every price for two of three seeds (the opponents' low-HP axe makes the trade deadlier). The nets still eat greedily only .13-.33 of the time. None passes the low-HP gate; n3r33 stays installed.
- No maul specials, orb clicks or axe swings. Seed 83 strips (1.63 per fight with tentacle+AGS+maul).
- **Ledger, seed 82 against the env's opponents** (1,864 Dharok v Dharok fights):
  - The scripts' kill chance at a swing is .017 (.042 against each other). The net's D at its swings is .016, and it spends .024 of its ticks at D ≥ .25.
  - Under 20 HP the scripts take the axe on .38 of swings against the net (.81 against each other). The net is mostly on a bar of 24+, where the table is .45-.52, and a Dharok max of 96 does not reach 99.
  - The rekeyed axe doesn't reach a net that stays healthy. The lethality gap left is FINDINGS' KO hazard from 40 HP up (sim 17 / 2.3 / 0.5 per 10,000 fighter-ticks at 40-59 / 60-79 / 80+, real 32 / 4.6 / 2.1): bursts from mid HP, not the low-HP axe.

## 2026-10-19: what kills from mid HP; the Voidwaker special keyed on Vengeance (branch vw-veng)

- **KO anatomy, real against script v script** (`/tmp/risk/burst.py`, same reader): damage on the victim in the 3 ticks to the KO, and the attacker's specials, axe swings and Vengeance procs around it.

  | | Real (112 KOs) | Sim (1,628) |
  |---|---|---|
  | 60+ damage in the last 3 ticks | .71 | .57 |
  | KOs by an axe swing without a special | .26 | .32 |
  | KOs by a Voidwaker special with his Vengeance proc | .20 | .07 |
  | KOs by a Voidwaker special alone | .15 | .20 |

  - Hitsplat counts per burst can't be compared. The sim writer marks a hitsplat per tick (0/1), and the engine counts only attack hits (no reflect, recoil or orb), while the export counts every hitsplat.
- **Spectated Voidwaker specials depend on the attacker's own Vengeance; the script's did not.** Per 1,000 ticks of a fighter holding a Voidwaker, by the other side's bar <12 / 12-23 / 24+:

  | | Vengeance down | Vengeance up |
  |---|---|---|
  | Real | 15.6 / 7.8 / 2.7 | 20.9 / 17.0 / 11.5 |
  | Sim before | 11.9 / 13.4 / 6.1 | 12.3 / 13.1 / 5.4 |

- **Change:** a Dharok script's Voidwaker special uses `RF_DHAROK_VW_SPEC_PM[veng][his bar band]`, with the profile's vw_pm as an odds propensity (mean 202).
  - Calibrated in the sim with `scripts/fit_dharok_vw.py`, 3 steps; every cell within 8% of real.
  - Scripts not on the Dharok rules keep vw_pm.
- **After:** KOs by a Voidwaker special with a Vengeance proc are still .08 against .20. The spec is now there; the victim's hit into the Vengeance that lands with it is not (the timing of the reflect, an open question since the escape-gap work).
- **KO hazard by victim HP** (`/tmp/eg/ko.py`, gap estimate; per 10,000 fighter-ticks at 0-19 / 20-39 / 40-59 / 60-79 / 80+, and in total):

  | | By HP band | Total |
  |---|---|---|
  | Real | 117 / 47 / 32 / 4.6 / 2.1 | 9.9 |
  | Sim before | 97 / 62 / 24 / 4.8 / 0.9 | 10.2 |
  | Rekeyed axe | 100 / 67 / 23 / 5.1 / 0.8 | 10.4 |
  | Plus Vengeance-keyed Voidwaker | 115 / 69 / 23 / 5.2 / 0.9 | 10.6 |

  - The total matches. The shift from 40-59 to 20-39 is within reach of the gap estimate's bias (a meal hidden on a swing tick moves HP), so the band split isn't a target.
- Tests pass; goldens refreshed.

## 2026-10-19: the low-HP meal probe, per decision (correction)

- **Correction (advisories):** `probe_lowhp_meals`' "greedy matches better branch" compared each decision with its band's pooled branch means, not with that state's own outcomes. The "eating is the better option at every price" read was a pooled result.
  - The probe now plays each branch 6 times per decision, with paired combat dice, and reports pooled and per-decision results apart.
- **p3 seeds at ≤ 20 HP** (150 sessions; d is the forced meal's benefit in KO − price × death per fight):

  | Seed | Greedy eats | Mean d (se) | Meal better / worse / not told apart | Greedy picks the better, of those told apart |
  |---|---|---|---|---|
  | 81 | .33 | +.164 (.052) | .11 / .06 / .83 | .42 |
  | 82 | .30 | +.024 (.045) | .06 / .05 / .88 | .87 |
  | 83 | .12 | +.105 (.040) | .08 / .07 / .85 | .55 |

  - On average a forced meal at ≤ 20 HP is worth it for seeds 81 and 83; for 82 it isn't told from zero.
  - With 6 binary rolls, 83-88% of decisions can't be told apart. The per-state accuracy rests on 15-30 decisions per seed and isn't a result. Separating d of about .15 per state needs about 50 rolls.
  - The pooled break-even for seed 82 flipped between runs ("eat always" at 2 rolls, "skip below 0.16" at 6): the pooled column is noise at this n.
- **Live real2sim for the ledger:** `ocean/osrs/tests/export_tape_duels.c` writes own tapes as duels. `tools/rollout_export.py` gives 8 tapes for the Oct 17-18 sessions, and only one is Dharok v Dharok, so a live ledger needs more live Dharok fights.

## 2026-10-19: p4, three seeds against the Vengeance-keyed Voidwaker (c-main d34183ce6)

| Seed | Ladder | Dharok (deaths per session) | tentacle+VW+maul | tentacle+VW | tentacle+AGS+maul | VW | ≤ 20: greedy eats / mean meal benefit d (se) |
|---|---|---|---|---|---|---|---|
| 91 | 8.80 | 9.09 (.18) | 7.38 | 10.91 | 4.85 | 9.18 | .20 / +.031 (.023) |
| 92 | 8.72 | 7.81 (.15) | 7.91 | 7.61 | 3.46 | 9.59 | .32 / +.109 (.034) |
| 93 | 8.86 | 8.55 (.03) | 7.04 | 8.42 | 4.89 | 9.56 | .17 / +.080 (.029) |

- The forced meal at ≤ 20 HP is worth +.03 to +.16 per fight on average in five of six p3/p4 seeds (four of them over 2 se), while the nets eat greedily .12-.33 of the time.
  - The pooled evidence that nets under-eat at very low HP holds. Which states they get wrong is not resolved at 6 rolls.
- No candidate replaces n3r33.

## 2026-10-19: the Voidwaker special after one's own Vengeance is spent (branch vw-after-proc)

- **Real Dharok fighters spec right after their reflect lands.** Voidwaker specials per 1,000 ticks against a bar under 24, in the 1-2 ticks after their own "Taste vengeance!": 34.3 / 44.1 real, against 6.5 / 5.5 in the sim. With Vengeance up and unspent, it is 16.6 against 19.2.
  - The special follows the reflect before the other side can eat. In the sim the spec rate fell after the proc, because Vengeance was down.
- **Change:**
  - New script observation float `RF_SCRIPT_VENG_PROC_AGO`: ticks since his own Vengeance was spent, from `inventory_use.vengeance_consumed_tick`, in the slot `RF_SCRIPT_OWN_BAR` left.
  - `RF_DHAROK_VW_SPEC_PM[3][3]` gets a third Vengeance state, spent 1-2 ticks ago. Calibrated with `scripts/fit_dharok_vw.py`, 3 steps; every cell within 7% except spent against a bar of 24+ (9.1 against 9.7).
- **After:**
  - KOs by a Voidwaker special with his reflect: .07 → .14 (real .20). Such bursts: .03 → .06 (real .08).
  - KO total per 10,000 fighter-ticks: 11.0 (real 9.9). At 40-59 HP: 24.6 (real 32).
- Tests pass; goldens refreshed.

## 2026-10-19: the low-HP meal probe with session-clustered intervals (correction)

- **Correction (advisories):** the per-decision SE treated decisions as independent, but consecutive eligible ticks share a fight. The "clearly above zero in four seeds" claim is withdrawn.
  - The probe now resamples sessions for 90% intervals and reports d where greedy skips. There the skip branch is greedy's own play, so it is the direct test of whether an extra meal pays.
- **≤ 20 HP, 300 sessions, seed 2, 6 rolls per branch:**

  | Net | Greedy eats | Mean d [90%] | d where greedy skips (n) [90%] |
  |---|---|---|---|
  | p3 s81 | .26 | +.014 [−.038, +.072] | +.010 (232) [−.056, +.084] |
  | p3 s82 | .19 | +.022 [−.070, +.120] | +.032 (208) [−.075, +.145] |
  | p3 s83 | .16 | +.134 [+.065, +.205] | +.105 (312) [+.026, +.180] |
  | p4 s91 | .21 | +.104 [+.033, +.186] | +.083 (315) [−.007, +.174] |
  | p4 s92 | .30 | +.120 [+.039, +.200] | +.119 (227) [+.017, +.219] |
  | p4 s93 | .22 | +.063 [−.017, +.143] | +.059 (219) [−.027, +.148] |

  - Where greedy skips, an extra meal's point estimate is positive for all six nets (mean +.068 per fight in KO − price × death). Two of six intervals exclude 0.
  - As six independent training runs, all-positive has a one-sided sign-test p of .016. The under-eating is modest and real on average, but no single net shows it firmly.

## 2026-10-19: p5 and the install of p5 s101 (c-main f32837f1c, the opponents with all three rules)

| Seed | Ladder | Dharok (deaths per session) | tentacle+VW+maul | tentacle+VW | tentacle+AGS+maul | VW | ≤ 20: greedy eats / d where greedy skips (n) [90%] |
|---|---|---|---|---|---|---|---|
| 101 | 9.21 | 9.27 (.04) | 9.27 | 8.70 | 5.54 | 9.33 | .42 / +.008 (86) [−.122, +.158] |
| 102 | 7.75 | 7.36 (.08) | 7.05 | 9.24 | 5.90 | 7.51 | .75 / +.030 (30) [−.074, +.136] |
| 103 | 9.03 | 8.34 (.32) | 8.52 | 8.31 | 5.67 | 9.13 | .22 / +.024 (268) [−.056, +.106] |

- **Seed 101:**
  - Best ladder of the three, and the fewest Dharok-kit deaths of any candidate so far.
  - At ≤ 20 HP it eats greedily .42 of the time, and where it skips, an extra meal shows no gain.
  - No orb, axe or maul. Strips .08 per fight in the Dharok kit.
- **Live 4-HP ticks (session 20261018T185228, factual state):** it eats at 173 (food), drinks at 174, and takes neither at 175-176. n3r33 ate at all four. In the real fight the HP stayed at 4 there, so those states don't follow its own meals.
- **Installed:** `engine-install --checkpoint p5s101`, C 01a94b697 (the same env code as f32837f1c plus FINDINGS and probe commits).
  - Replays of 20261018T185228, 20261017T211452, 20261015T203026 and 20261017T230732 all run clean.
  - Going back to n3r33 needs its own server build (492 inputs): `engine-install --checkpoint n3r33` from a checkout at 467413f58. `rl.args.previous` alone would abort the new server.
