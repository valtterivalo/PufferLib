# Oracle target-priority wrapper eval (heavy agent r4 step 1)

Tests whether forcing the policy's target to Jad/healer/set when zuk_hp <= threshold
fixes the late-fight failure mode.

Source: `p2k4szzs/0000000049971200.bin`. 20k normal-start episodes per arm.

## Arms

| Arm | oracle_mode | override |
|---|---|---|
| E0 | 0 | none (raw policy) |
| E1 | 1 | Jad-only when zuk_hp <= 300 |
| E2 | 2 | Jad > zuk-healer > set when zuk_hp <= 300 |
| E3 | 3 | Jad > zuk-healer > set when zuk_hp <= 240 |

## Results

```
n_normal:  E0=20170  E1=20025  E2=20023  E3=20036

metric         E0       E1       E2       E3
score        0.7191   0.7074   0.7073   0.7190
ret_n         7.971    7.977    7.976    7.972
wins         0        0        0        0
fr<=300      0.4173   0.4236   0.4226   0.4198
fr<=240      0.2295   0.0160   0.0157   0.2324
fr<=150      0.0012   0.0000   0.0000   0.0000
die_jad      0.8131   0.8165   0.8167   0.8218
die_heal     0.2292   0.0334   0.0325   0.2276
die_set      0.9878   0.9886   0.9891   0.9885
tk_a300      13.28    13.65    13.58    13.21
dmg_a300     87.6     53.6     53.5     86.5
tk_a240       8.18     7.29     7.56     8.06
dmg_a240     55.5     62.2     62.2     52.5
tk_a150       0.59     0.00     0.00     0.00
dmg_a150      5.5      0.0      0.0      0.0
heal_dmg     0.289    0.001    0.001    0.109
jad_kills    0.114    0.122    0.121    0.123
phase         2.40     2.40     2.40     2.40
```

E0 reproduces the round-4 D-deep numbers (score 0.72, fr<=300 0.42, fr<=240 0.23,
die_jad 0.81). Eval is calibrated to the round-4 baseline.

## Interpretation per heavy agent r4 gates

```
Gate              E1     E2     E3
score >= +0.03    fail   fail   fail
wins > 0          fail   fail   fail
fr<=150 >= 2x     fail   fail   fail
die_jad <= -30%   fail   fail   fail
```

All four gates fail in every override arm. **E1/E2 actively make things worse.**
E3 is essentially a no-op because <23% of episodes ever cross 240.

## What the data says

1. **Forcing target=Jad breaks Zuk progress.** dmg_a300 drops 87.6 → 53.5 HP in
   E1/E2 — about 34 HP of Zuk damage that the raw policy was extracting after
   crossing 300 vanishes when the override fires. fr<=240 collapses 14x in E1/E2
   (0.230 → 0.016).

2. **Forced target=Jad does NOT meaningfully kill Jad.** jad_kills fraction moves
   only +0.7% (0.114 → 0.122). The policy can attack Jad when commanded but
   cannot kill Jad effectively in the late-Zuk state — wrong gear, low prayer,
   bad position, or all three.

3. **die_jad fraction barely moves (+0.4%, +0.4%, +1.1%).** The single most
   striking number in this whole eval. Even with full priority override, the
   agent dies with Jad alive at the same rate. This is the structural finding:
   **the policy lacks the capability to kill Jad late, not just the priority.**

4. **Healer damage destroyed in E1/E2.** heal_dmg 0.289 → 0.001 — the agent
   never reaches the healer-kill phase because it's stuck on Jad after the
   override.

5. **The policy's "ignore adds and tunnel Zuk" is a local survival hack.**
   Score 0.72, all losses, but it's the best the model found. Forcing the
   alternative strategy (kill adds first) makes everything worse because the
   capabilities for that alternative do not exist in the policy.

## Implications for round 5

Per heavy agent r4 interpretation guide, this is the
**"Wrapper worsens / Wrapper does not improve"** branch:

> "current policy's 'ignore adds' may be a local survival hack
>  v6 still worth a small scout, but avoid strong Zuk reward gating"

> "add handling needs movement/prayer/timing, not target priority alone
>  add D-deep counters for shield/prayer/death-source before training v6 heavily"

Concrete updates to the priority list:

- **v6 scout: yes, but soft only.** The Zuk reward multiplier in v6-soft-priority
  must stay weak. Strong Zuk-reward gating risks breaking the survival hack
  before the agent can develop alternatives.

- **Add D-deep prayer/shield/death-source counters before v6.** We need to know
  whether late-fight failures are damage-budget exhausted, prayer-drained,
  shield-blocked, or wrong-style-against-Jad. The current counters surface the
  symptom (died with Jad alive) but not the cause.

- **Architecture probe rises in priority.** If a forced add-priority policy
  cannot kill Jad, the bottleneck may not be reward-shapeable. Distillation of
  p2k4szzs into hs=512/L=4 (B-transfer) becomes a useful capacity test once
  v6 scout closes out.

- **Selective BC bar raised.** Even oracle-priority demos would not teach the
  agent to kill Jad — only to attack Jad. Need demos that show *successful*
  late-game add-kills (correct gear/prayer/positioning), which means we need
  a working oracle that switches gear+prayer+target, not just target.

## Files

- `oracle_eval/E0.json` ... `E3.json` — full metric dumps
- `oracle_eval/E0.log` ... `E3.log` — runner logs
- `run_oracle_eval.py` — single-arm runner
- `run_oracle_eval_all.sh` — runs all 4 arms back to back
- `compare_oracle_eval.py` — side-by-side table + gate evaluation

## Implementation

Override hooks into `inf_tick_player` at `actions[INF_HEAD_TARGET]` read site,
shadowing the value with the priority-picked obs slot when conditions hold.
See `ocean/osrs/encounters/encounter_inferno.h` `inf_oracle_pick_target_slot`
and the override block before the existing target-decoding logic.

Config flows: Python env dict → `binding.c my_init` → `inf_put_int(state,
"oracle_mode", v)` → `InfernoState.oracle_mode`. Defaults to 0 (off).
