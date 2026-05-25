# RLVG Inferno Submission Manifest

Status: generated 2026-05-25 for the current checked-out paper artifacts.

## Primary Artifact

- PDF: `papers/rlvg-inferno/paper_v0.pdf`
- SHA-256: `991a88bf034bc0d66782fd08393166a855703596494bc46ed746971f0b6ba5f3`
- Pages: 4
- Page size: letter
- File size: 435023 bytes
- Author metadata: blank
- Date metadata: omitted

## Source Artifacts

- TeX: `papers/rlvg-inferno/paper_v0.tex`
- TeX SHA-256: `9b916bba0e12ed8bb2052522f97642d203cebd02ae104e976b88f1cbb7eeea0b`
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
rg -n '[\x3b\x{2014}]' papers/rlvg-inferno/paper_v0.tex
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
- The live RLVG 2026 submission guide and OpenReview venue metadata were checked on 2026-05-25. They match the local CFP mechanics used for this artifact.
- The submission audit is in `papers/rlvg-inferno/submission_audit_v0.md`.
- The final external wording review is in `papers/rlvg-inferno/review_notes_v7.md`.
- The live venue mechanics check is in `papers/rlvg-inferno/review_notes_v8.md`.
- The final blocker review is in `papers/rlvg-inferno/review_notes_v9.md`.
- The source-strength follow-up cites the PufferLib arXiv paper for the general PufferLib context.
