# RLVG Inferno Paper Review Notes v2

Status: post-PDF critique notes for `paper_v0.tex`.

## External Critique Pass

ChatGPT reviewed the pushed GitHub file:

- repo: `valtterivalo/PufferLib`
- branch: `inferno-upstream-sync-may-14`
- file: `papers/rlvg-inferno/paper_v0.tex`
- model shown in UI: `Heavy`

Main acceptance risks it flagged:

- The result table still reads more precise than the downsampled run artifact can prove.
- The inspectability thesis needed a real replay frame, not only a schematic.
- The internal progress score needed a concrete definition.
- The paper risked reading as a PufferLib systems pitch instead of a benchmark paper.
- The title scope needed to avoid implying a full MMO client.

## Changes Applied

- Replaced the schematic figure with a real Inferno wave 69 replay screenshot.
- Retitled the paper as an OSRS boss-fight benchmark instead of a broad MMO benchmark.
- Rewrote the abstract result claim to call the data a stored run summary, not solved-benchmark evidence.
- Made the compact checkpoint action-surface mismatch explicit: the stored policy used a 5-action compact Redemption overhead mapping, while the current code exposes an explicit Redemption overhead action and 89 mask logits.
- Defined internal progress score from the actual code shape: wins return `2.0`, while partial runs receive Zuk HP progress and late-fight bonuses.
- Relabeled the result table as development telemetry.
- Replaced player-intent phrasing with player-readable task-structure phrasing.

## Remaining Evidence Work

- A frozen-schema no-render evaluation with explicit episode count would still strengthen the paper.
- A fresh frozen-schema checkpoint or a compatibility eval path would remove the compact-action-surface caveat.
- The result table should report policy mode, hardware, and checkpoint selection rule if a fresh eval is produced.
- The current screenshot is strong enough for inspectability, but a later annotated frame with arrows and log snippets would be better.
- If page pressure appears, cut Table 2 before cutting the real replay figure.
