# RLVG Inferno Morning Handoff

Status: submit ready short-form bundle for `paper_v0.pdf`.

## Submit

- PDF: `papers/rlvg-inferno/paper_v0.pdf`
- TeX: `papers/rlvg-inferno/paper_v0.tex`
- Manifest: `papers/rlvg-inferno/submission_manifest.md`
- Audit: `papers/rlvg-inferno/submission_audit_v0.md`

## Current Artifact

- 4 pages, letter size
- Anonymous author field
- No PDF date metadata
- Replay figure metadata scan clean
- TeX source has no semicolons or em dashes
- Manifest hashes match current PDF, TeX, and replay figure

## Live Venue Check

Checked `https://sites.google.com/view/rlvg-2026/submission-guide` and OpenReview on 2026-05-25.

- Short-form papers are 4 pages, excluding references and appendices.
- No one-page cover page is required.
- Submission is through OpenReview.
- LLM writing tools are allowed with human responsibility.
- Review is double blind.

## Remaining Risks

- Hidden OpenReview upload workflow may still expose a template requirement. The public submission guide does not.
- Reviewers familiar with PufferLib and OSRS may infer provenance, but the PDF has no explicit author leak.
- Table 3 reports development telemetry from an earlier compact action mapping, not a frozen-schema eval. The paper states this in the abstract and checkpoint section.

## Latest Checks

- `pdflatex` twice, no warnings found by the log scan
- `pdfinfo` reports 4 pages and blank author
- PDF and image raw string scans clean
- `pdftocairo` rendered all 4 pages
- `test_inferno_attack_styles`: 1441/1441
- `_C.env_action_dims()` sums to 89
