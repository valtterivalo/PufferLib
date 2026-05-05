# Heavy agent round-5 verdict (received during round-5 implementation)

## Top recommendation

Run a **full-oracle capability eval before any more PPO training**.

The target-only oracle showed correct target is not enough: Jad kills moved
only from 0.114 to 0.122, fr<=240 collapsed from 0.2295 to 0.016 in E1/E2,
all four gates failed. That makes reward-first training a high-variance bet.
The next narrow question:

**Can the current policy exploit correct target + correct overhead +
correct weapon, with movement still left to the policy?**

If yes: the problem is learnable through reward/curriculum/imitation.
If no: blocker is movement/position/timing/supply, and v6 reward will
mostly teach the model to abandon its current tunnel-Zuk survival hack.

## Staged full-oracle eval

20k normal-start eps per arm on p2k4szzs.

  E0  none                                   raw baseline
  E4  Jad alive @ jad_spawned or zuk<=600    target Jad only
  E5  same trigger                           target Jad + overhead
  E6  same trigger                           target Jad + gear/offensive
  E7  same trigger                           full
  E8  zuk_hp <= 300                          full (round-4 timing comparison)

Key correction: do NOT set Jad weapon based on Jad's attack style. Jad's
style is what to PRAY against, not what weapon to use. Always TBOW + RIGOUR
for Jad. Overhead per Jad's NEXT style. For zuk-healers: BLOWPIPE + RIGOUR.
For sets: TBOW for ranger/mager.

## Gates

  jad_kills:  >= 0.25 absolute, preferably 0.35+
  die_jad:    < 0.65
  fr<=240:    no worse than 0.18
  fr<=150:    >= 2x E0
  score:      no drop worse than -0.03 unless Jad kills rise sharply
  wins:       any > 0 is strong pass

## v6-soft-E (what soft means)

Trigger: jad_spawned || zuk_hp <= 600

  jad_damage_reward          0.004
  zuk_healer_damage_reward   0.006
  set_damage_reward          0.002
  jad_kill_bonus             0.35
  zuk_healer_kill_bonus      0.12
  set_kill_bonus             0.08

No Zuk multiplier in first scout. Optional 0.90 multiplier while Jad alive
ONLY as a softer second variant. NEVER 0.50 - target-only oracle showed
disrupting tunnel-Zuk can destroy progress before the alternative is learned.

## Reward clamp

PPO clamps reward to [-1, 1] per tick. Kill bonuses on the same tick as
dense damage reward will clip. Best fix: queue and emit gradually.

```c
pending_jad_kill_bonus += 0.35;
emit = min(0.07, pending_jad_kill_bonus);
reward += emit;
pending_jad_kill_bonus -= emit;
```

## Code-level observations

1. **damage_after_X overcounts crossing hits.** Hit from 310->230 adds 80 to
   damage_after_300 AND 80 to damage_after_240. Should be 80 and 10. Fix:

```c
static inline float dmg_below_threshold(float old_hp, float new_hp, float t) {
    if (new_hp >= old_hp) return 0.0f;
    float a = old_hp < t ? old_hp : t;
    float b = new_hp < t ? new_hp : t;
    float d = a - b;
    return d > 0.0f ? d : 0.0f;
}
```

2. **healer_alive mixes Jad healers and Zuk healers.** Split:

```
count_died_with_jad_healer_alive_normal
count_died_with_zuk_healer_alive_normal
```

3. **Frontier sampler can silently no-op.** When no cells meet q_floor,
   total_f is zero and falls through to count-decay sampler. G2 likely ran
   as count-decay since max q (0.831) < q_floor (0.85). Add logging.

4. **Archive quality update doesn't replace stored snapshot.** If a
   rediscovered cell has higher quality, archive_insert updates quality but
   keeps first-write parent/snapshot. Demo export sorts by quality but
   exports stale trajectory. Use separate structural_quality + sampling_quality.

5. **Phase-2 cursor still rewards Zuk progress, not add handling.** Add
   alternative success condition based on jad_killed / zuk_healer_killed.

## Plan D (budget portfolio)

Step 1: full-oracle eval (cheap)
If E7 passes -> v6-soft-E bracket: 4 v3 control + 8 v6-soft-E (50M each)
If E7 fails -> diagnostic counters + small v6 + movement oracle eval
If E7 mixed -> 4 v3 control / 4 v6-soft-E / 4 v6-soft-E + 0.90 multiplier

## Revised hypothesis ladder

1. H_capability: target/gear/prayer/position/timing capability gap (leading)
2. H_localopt: tunnel-Zuk is best learned local survival hack (strong)
3. H_cap_pos / H_cap_timing (rises if full oracle fails)
4. H_reward (weakened but plausible)
5. H_demo_dist (plausible, archive refresh useful only after cell quality
   represents add-stable states)
6. H_capacity (plausible, not first)
7. H_frontier_sampler (low - sampler needs logging but more frontier
   sampling is not the next bet)
