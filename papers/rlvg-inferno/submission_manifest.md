# RLVG Inferno Submission Manifest

Status: generated 2026-05-25 for the current checked-out paper artifacts.

## Primary Artifact

- PDF: `papers/rlvg-inferno/paper_v0.pdf`
- SHA-256: `5a02522b8fab35e07ee9de51b9f8257240c78e38bf37ae8d50da54299f3872cf`
- Pages: 4
- Page size: letter
- File size: 435117 bytes
- Author metadata: blank
- Date metadata: omitted

## Source Artifacts

- TeX: `papers/rlvg-inferno/paper_v0.tex`
- TeX SHA-256: `4f4ef23e722e69800e21f11b31f68d90958fd51890c70a1e138428ff784a03a3`
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
- The post-observation-row review is in `papers/rlvg-inferno/review_notes_v10.md`.
- The layout audit is in `papers/rlvg-inferno/layout_audit_v0.md`.
- The reference link audit is in `papers/rlvg-inferno/reference_link_audit_v0.md`.
- The Inferno contract test audit is in `papers/rlvg-inferno/contract_test_audit_v0.md`.
- The writing-style audit is in `papers/rlvg-inferno/style_audit_v0.md`.
- The requirement trace is in `papers/rlvg-inferno/requirements_trace_v0.md`.
- The OpenReview upload checklist is in `papers/rlvg-inferno/openreview_upload_checklist_v0.md`.
- The morning audit checklist is in `papers/rlvg-inferno/morning_audit_checklist_v0.md`.
- The source-strength follow-up cites the PufferLib arXiv paper for the general PufferLib context.
- The Zuk HP result row reports `env/min_zuk_hp_normal`, where wins are logged as 0 HP.
