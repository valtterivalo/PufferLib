# Round-5 oracle wrapper eval (full target+gear+overhead+offensive)

Heavy agent r5 step 1: tests whether forcing target+gear+overhead+offensive
override when Jad alive (zuk.jad_spawned || zuk_hp<=600) lets the policy
kill Jad. If yes, capability for Jad-killing exists; reward/curriculum
can teach it.

Source: `p2k4szzs/0000000049971200.bin`. 20k normal-start episodes per arm.

## Arms

| Arm | mode | trigger              | overrides                                     |
|-----|------|----------------------|------------------------------------------------|
| E0  | 0    | none                 | none (raw policy)                              |
| E4  | 4    | Jad spawn / hp<=600  | target only                                    |
| E5  | 5    | Jad spawn / hp<=600  | target + overhead                              |
| E6  | 6    | Jad spawn / hp<=600  | target + gear + offensive prayer               |
| E7  | 7    | Jad spawn / hp<=600  | target + gear + offensive prayer + overhead    |
| E8  | 8    | hp<=300              | full (timing comparison vs round-4 E1/E2)      |

## Results

```
metric             E0       E4       E5       E6       E7       E8
score          0.7191   0.5211   0.5216   0.5210   0.5222   0.7058
ret_n           7.971    5.776    5.782    5.777    5.790    7.957
wins           0        0        0        0        0        0
min_zhp         337.1    574.7    574.0    574.8    573.4    353.0
fr<=300        0.4173   0.0000   0.0000   0.0000   0.0000   0.4094
fr<=240        0.2295   0.0000   0.0000   0.0000   0.0000   0.0174
fr<=150        0.0012   0.0000   0.0000   0.0000   0.0000   0.0000
die_jad        0.8131   0.0106   0.0149   0.0096   0.0188   0.8146
die_zuh        0.2292   0.0000   0.0000   0.0000   0.0000   0.0158
die_jah        0.0000   0.0009   0.0008   0.0011   0.0015   0.0198
die_set        0.9878   0.9855   0.9703   0.9830   0.9700   0.9880
tk_a300        13.28    0.000    0.000    0.000    0.000    13.53
dmg_a300       57.77    0.000    0.000    0.000    0.000    23.66
tk_a240         8.18    0.000    0.000    0.000    0.000     7.05
dmg_a240       25.47    0.000    0.000    0.000    0.000     6.11
heal_dmg       0.289    0.000    0.000    0.000    0.000    0.001
p_dies_jad     0.114    0.006    0.008    0.005    0.011    0.124
phase           2.40     1.98     1.98     1.98     1.98     2.39
gear_sw        0.086    0.068    0.061    0.040    0.041    0.080
pray_ok        0.567    0.519    0.538    0.515    0.536    0.575
```

## Note on D-deep numbers

This run uses the corrected `dmg_below_threshold` helper (heavy agent r5
flagged a bug where damage was double-counted across thresholds). E0 dmg_a300
is now 57.8 (vs 87.6 buggy). Round-4 RESULTS.md numbers are inflated.

## The four-way tie

E4, E5, E6, E7 produce essentially identical results (within 0.5% on score).
**Target alone is sufficient when fired at Jad spawn.** The richer overrides
(gear, prayer, offensive prayer) add no measurable benefit. The policy already
has reasonable gear/prayer for the Jad-spawn moment because the game state
isn't yet degraded.

This is the central round-5 finding: **timing matters more than richness**.

## E8 comparison: full oracle at zuk_hp<=300

E8 fires at the same zuk_hp<=300 trigger as round-4 E1/E2 but with full
overrides (target+gear+overhead+offensive). Result: jad_kills 0.185 (vs
E0's 0.187 baseline rate, ~unchanged). die_jad 0.815 (vs E0 0.813,
unchanged). fr<=240 0.017 (collapsed 14x like round-4).

The policy state at zuk_hp<=300 is too degraded for ANY oracle override to
recover. Adding gear/prayer/offensive on top of target-at-300 doesn't help.

## Heavy agent r5 gates

```
                  E4       E5       E6       E7       E8
jad_kills frac  0.989    0.985    0.990    0.981    0.185
  >=0.25         PASS     PASS     PASS     PASS     fail
die_jad         0.011    0.015    0.010    0.019    0.815
  <0.65          PASS     PASS     PASS     PASS     fail
fr<=240         0.000    0.000    0.000    0.000    0.017
  >=0.18         fail     fail     fail     fail     fail
fr<=150         0.000    0.000    0.000    0.000    0.000
  >=0.0025       fail     fail     fail     fail     fail
score delta    -0.198   -0.197   -0.198   -0.197   -0.013
  >=-0.03        fail     fail     fail     fail     PASS
wins              0        0        0        0        0
  >0             fail     fail     fail     fail     fail
```

## Interpretation

This is heavy agent's **"E7 mixed"** branch:

> "Example: Jad kills improve but score drops. Then run a smaller v6-soft-E
>  bracket: 4 v3 control / 4 v6-soft-E no Zuk multiplier / 4 v6-soft-E with
>  0.90 Jad-alive multiplier"

Three findings worth highlighting:

### 1. Jad-killing capability exists when invoked early

E4-E7 all kill Jad in 98-99% of episodes regardless of override richness.
The policy CAN execute Jad-targeting and survive against Jad — given clear
direction at Jad-spawn moment. That answers H_capability for Jad-kill: it's
not absent, it just isn't reachable from the policy's current state when
left to its own devices.

### 2. The missing skill is Jad-dead → Zuk re-engagement

Once Jad dies in E4-E7, the override moves to next priority (zuk-healer or
set NPC). Sets respawn periodically (~350 ticks). The policy is therefore
held against sets indefinitely, never returning to Zuk damage. That's why
fr<=300 collapses to zero in modes E4-E7 — the agent never gets back to
attacking Zuk after Jad.

This is a transition-skill gap, not a capability gap. The policy doesn't
know how to return to Zuk after add cleanup. Reward shaping must teach
this, not just add-killing in isolation.

### 3. Late-firing oracle (E8) cannot rescue

Full oracle at zuk_hp<=300 produces same Jad-kill rate as no oracle (~19%)
and same die_jad fraction (~81%). The policy at zuk_hp<=300 has already
spent supplies, cycled prayer, taken positioning damage. No amount of
target/gear/prayer override gets a Jad kill from there.

Round-4 E1/E2 collapse of fr<=240 was confirmed: same fr<=240 collapse
(0.017) appears in E8 even with full overrides. The collapse is
fundamental to the policy's late-game state, not the oracle's incompleteness.

## Implications for round 6

Per heavy agent's r5 prescription, this is the "E7 mixed" branch -> small
v6-soft-E bracket. But the new finding (target-only @Jad-spawn is sufficient
for Jad-kill) suggests the v6 design space narrows:

**v6 must reward both add-clearing AND post-add Zuk re-engagement.** Heavy
agent's `v6-soft-E` (jad_damage 0.004, healer 0.006, set 0.002, kill bonuses
0.35/0.12/0.08, no Zuk multiplier) handles add-clearing. But to teach
re-engagement we may need either:

- A "kill-Jad-then-zuk-progress" composite shaping (e.g., bonus for any
  Zuk damage AFTER Jad has been killed in this episode)
- A milestone reward for "transitioning back to Zuk damage post-add-kill"

Without that, v6 risks teaching the same failure mode E4-E7 demonstrated:
agent kills Jad but stays stuck on adds, never advancing Zuk.

Other observations:

- **Architecture probe (B-transfer) gains evidence.** The transition skill
  may benefit from larger capacity (hs=512/L=4) since it requires
  state-conditional branching: "if Jad dead and Zuk progress incomplete,
  return to Zuk".

- **Selective BC may not help unless demos demonstrate the transition.**
  Even oracle-generated Jad-kill demos won't cover the Jad-dead-then-Zuk
  transition unless the oracle releases the override after Jad dies. Worth
  testing a "release-after-jad-dead" oracle variant.

- **Local optimum is real.** "Ignore Jad, tunnel Zuk" was the policy's
  best solution because the alternative (kill Jad, then can't re-engage)
  ends in zero Zuk progress. v6 must make Jad-kill-then-Zuk strictly
  dominate Jad-ignore-then-tunnel.

## Files

- `oracle_eval_v2/E0.json ... E8.json` — full metric dumps
- `oracle_eval_v2/E0.log ... E8.log` — runner logs
- `run_oracle_eval.py` — single-arm runner (extended for modes 0-8)
- `run_oracle_eval_all.sh` — runs all 6 arms (E0/E4/E5/E6/E7/E8)
- `compare_oracle_eval_v2.py` — side-by-side table + heavy agent's gates

## Code changes

- `encounter_inferno.h`: `inf_dmg_below_threshold()` helper, `InfOraclePick`
  struct + helpers, full-oracle override hooks in `inf_player_pretick`
  (prayer/offensive) and `inf_tick_player` (gear/target). oracle_mode
  range extended to [0,8].
- `binding.c`: split healer death-cause counters (zuk_healer + jad_healer
  separate from healer_alive aggregate).
- `osrs_types.h`: added `count_died_with_zuk_healer_alive_normal`,
  `count_died_with_jad_healer_alive_normal` to Log struct.
