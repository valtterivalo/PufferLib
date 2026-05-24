# RLVG Inferno Paper Review Notes v3

Status: focused caveat critique for `paper_v0.tex`.

## External Critique Pass

ChatGPT reviewed the pushed GitHub file after the compact checkpoint caveat was added:

- repo: `valtterivalo/PufferLib`
- branch: `inferno-upstream-sync-may-14`
- file: `papers/rlvg-inferno/paper_v0.tex`
- model shown in UI: `Heavy`

Main feedback:

- The caveat helped, but it still needed to say directly that the checkpoint is not an evaluation of the current schema.
- The caveat does not fatally weaken the paper because the main contribution is benchmark construction, not a public leaderboard score.
- The result would only be fatal if the paper framed the 0.49 win rate as the headline public score.

## Changes Applied

- Added an abstract sentence saying the checkpoint used an earlier 5-action PvE overhead mapping and is not an evaluation of the current 744-feature, 9-head, 89-mask surface.
- Rewrote the checkpoint section opener as historical development telemetry, not a frozen-schema benchmark evaluation.
- Added a direct sentence before the result table saying the checkpoint action mapping differs from the current surface summarized in Table 1.
- Replaced "current benchmark version" with "benchmark line" when describing what the stored result proves.
