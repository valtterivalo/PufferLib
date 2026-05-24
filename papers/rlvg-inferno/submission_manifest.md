# RLVG Inferno Submission Manifest

Status: generated 2026-05-25 for the current checked-out paper artifacts.

## Primary Artifact

- PDF: `papers/rlvg-inferno/paper_v0.pdf`
- SHA-256: `4912c3a7cf93e8b367a8b1691e3f5e6d79c565d2512e39394beab8f16d57d1b9`
- Pages: 4
- Page size: letter
- File size: 434924 bytes
- Author metadata: blank
- Date metadata: omitted

## Source Artifacts

- TeX: `papers/rlvg-inferno/paper_v0.tex`
- TeX SHA-256: `2ff216d809fffdb80fc7a372f2e15377ead7cfa88b42d406081c46cc35837255`
- Replay figure: `papers/rlvg-inferno/figures/inferno_replay_wave69.jpg`
- Replay figure SHA-256: `4ef69b90fd5276f71e9f844f1a7e4ca7d0b45dcfad54459cfc4a5ca128567369`

## Validation Commands

```bash
cd /Users/valtterivalo/Projects/pufferlib-metal/papers/rlvg-inferno
rm -f paper_v0.pdf paper_v0.aux paper_v0.log paper_v0.out
pdflatex -interaction=nonstopmode -halt-on-error paper_v0.tex
pdflatex -interaction=nonstopmode -halt-on-error paper_v0.tex
rm -f paper_v0.aux paper_v0.log paper_v0.out
pdfinfo paper_v0.pdf
```

```bash
cd /Users/valtterivalo/Projects/pufferlib-metal
rg -n '[;—]' papers/rlvg-inferno/paper_v0.tex
git diff --check
strings papers/rlvg-inferno/paper_v0.pdf | rg -n -i 'creationdate|moddate|eest|valtteri|valo|/users|pufferlib-metal|wandb|whl5mxay|j6bgoiu4|openai|codex|puffertank'
strings papers/rlvg-inferno/figures/inferno_replay_wave69.jpg | rg -n -i 'exif|xmp|photoshop|date|software|artist|copyright|valtteri|valo|/users|pufferlib'
pdftocairo -png -r 140 papers/rlvg-inferno/paper_v0.pdf /tmp/rlvg_cairo_preview/page
```

Expected results:

- `pdfinfo` reports 4 pages, letter size, blank author, and no creation or modification dates.
- The semicolon and em-dash search returns no matches.
- The raw PDF and image string scans return no matches.
- `pdftocairo` renders all four pages, including the replay figure on page 2.

## Review State

- Final external critique found no fatal acceptance risks.
- The score-definition issue was fixed by separating logged training score from archive `progress_score`.
- The final caveat frames the checkpoint as development telemetry, not a frozen-schema benchmark evaluation.
- OpenReview visible venue metadata was checked on 2026-05-25. The listed deadline matches the local CFP AoE deadline, and no extra style-file requirement was visible there.
- The submission audit is in `papers/rlvg-inferno/submission_audit_v0.md`.
- The final critique record is in `papers/rlvg-inferno/review_notes_v4.md`.
