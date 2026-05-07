# Experiment results snapshot

Date: 2026-05-07

Branch: `goexplore-robustify`

Commit: `562c650f4 go-explore qv2 archive + terminal reset`

Remote experiment root: `/puffertank/docker/goexplore/experiments/heavy_research_phase2_v4`

## Current best and controls

| Run | n | Median score | Worst score | Median min HP | Median <=240 | Median damage >240 | Median died with healers alive | Median <=150 | Median damage >150 | Wins |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| matched control 500M | 6 | 0.7211 | 0.7140 | 334.7 | 0.2407 | 24.0 | 0.2420 | unavailable | unavailable | 0 |
| qv2 low-lr balanced 500M | 6 | 0.7685 | 0.7408 | 277.8 | 0.5013 | unavailable | 0.5013 | unavailable | unavailable | 0 |
| qv2 low-lr more 500M | 6 | 0.7873 | 0.7743 | 255.2 | 0.6182 | 47.2 | 0.6163 | unavailable | unavailable | 0 |
| post-150 candidate 500M | 6 | 0.7937 | 0.7873 | 247.6 | 0.7105 | 42.2 | 0.7094 | 0.00012 | 6.5 | 0 |

## Healer diagnostic, 300M

| Arm | n | Median score | Median <=240 | Median damage >240 | Median died with healers alive |
| --- | ---: | ---: | ---: | ---: | ---: |
| source_balanced | 3 | 0.7897 | 0.7249 | 30.8 | 0.7260 |
| tight_gated | 3 | 0.7676 | 0.5208 | 25.0 | 0.5211 |
| long_post240_low_reach | 3 | 0.7590 | 0.5977 | 23.7 | 0.5974 |
| raw_score_tunnel | 3 | 0.8031 | 0.8303 | 53.3 | 0.8309 |

## Post-150 restore-mix scout, 300M

| Arm | n | Median score | Worst score | Median <=240 | Median damage >150 | Wins |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| base_nsf40_rng04 | 3 | 0.7644 | 0.7590 | 0.5077 | 0.0 | 0 |
| nsf30_rng04 | 3 | 0.7724 | 0.7684 | 0.6417 | 0.0 | 0 |
| nsf50_rng04 | 3 | 0.7636 | 0.7618 | 0.4373 | 0.0 | 0 |
| nsf40_rng12 | 3 | 0.7657 | 0.7640 | 0.5405 | 4.5 | 0 |
| nsf50_rng12 | 3 | 0.7522 | 0.7471 | 0.3064 | 0.0 | 0 |

## Notes

- The post-150 candidate is the current best 500M bracket.
- The restore-mix scout was negative and did not justify another bracket.
- The GPU was left idle after the negative scout.
- Zuk healer spawn at 240 HP can make `min_zuk_hp` misleading because healers can heal Zuk back above the threshold.
