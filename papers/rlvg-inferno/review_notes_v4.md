# RLVG Inferno Paper Review Notes v4

Status: final acceptance-risk pass for `paper_v0.tex` on 2026-05-25.

## External Critique Pass

ChatGPT reviewed the pushed GitHub file after the compact checkpoint caveat:

- repo: `valtterivalo/PufferLib`
- branch: `inferno-upstream-sync-may-14`
- file: `papers/rlvg-inferno/paper_v0.tex`
- model shown in UI: `Heavy`

Main feedback:

- No fatal argument-level risk.
- The score definition was inconsistent. Table 3 reports a 0.489635 win rate and 0.736389 score, so the table score could not be the archive score where wins return 2.0.
- After the score fix, no fatal risks remained. The paper was judged submit-ready for a short-form workshop paper, with the caveat that it is a benchmark-construction paper rather than a strong frozen-evaluation results paper.
- The one remaining edit was to shorten the abstract caveat so the contribution felt less buried under warnings.

## Local Evidence Check

- `ocean/osrs/encounters/inferno/encounter_inferno_render_snapshot.inc` defines archive `progress_score`: wins return `2.0`, while partial Zuk states receive minimum-Zuk-HP progress and late-add bonuses.
- `ocean/osrs_inferno/binding.c` logs the Table 3 `score` separately. For full-start runs, it computes `wins + (1 - wins) * wave / 69 * 0.5`.
- That formula explains the stored summary numbers: 0.489635 win rate, 66.720871 average final wave, and 0.736389 score.

## Changes Applied

- Renamed Table 3 from internal progress score to internal training score.
- Added the exact logged-score formula to the benchmark surface section.
- Updated source notes and submission audit to separate logged score from archive `progress_score`.
- Trimmed the abstract caveat after the final acceptance-risk pass.
- Replaced the replay figure PNG with a stripped JPEG embed after sequential PDF preview showed the JPEG path rendered cleanly.
- Omitted PDF date metadata and trailer IDs for double-blind submission hygiene.

## Validation

- `pdflatex` builds `paper_v0.pdf` in two passes.
- `pdfinfo` reports 4 pages on letter paper.
- `pdftocairo` renders all four pages, including the replay figure.
- `rg -n '[;—]' papers/rlvg-inferno/paper_v0.tex` returns no matches.
- Raw PDF strings contain no local paths, author identity strings, W&B ids, run ids, or date metadata.
- `git diff --check` is clean.
