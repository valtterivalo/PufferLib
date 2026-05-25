# RLVG Inferno Morning Handoff

Status: submit ready short-form bundle for `paper_v0.pdf`.

## Submit

- PDF: `papers/rlvg-inferno/paper_v0.pdf`
- TeX: `papers/rlvg-inferno/paper_v0.tex`
- Manifest: `papers/rlvg-inferno/submission_manifest.md`
- Audit: `papers/rlvg-inferno/submission_audit_v0.md`
- OpenReview checklist: `papers/rlvg-inferno/openreview_upload_checklist_v0.md`

## Current Hashes

- PDF SHA-256: `1429ff1b3c4ed8ffefaf8b06eaeef7b7074d349db6586fa906f71d5eb161744d`
- TeX SHA-256: `2782b8d02230982f563ad8b74374a287cac4b3258b7539f5b66b2bd11cd0698b`
- Replay figure SHA-256: `4ef69b90fd5276f71e9f844f1a7e4ca7d0b45dcfad54459cfc4a5ca128567369`

## Current Artifact

- 4 pages, letter size
- Anonymous author field
- No PDF date metadata
- No JavaScript or encryption in the PDF
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
- `pdfinfo` reports 4 pages, blank author, no JavaScript, and no encryption
- PDF and image raw string scans clean
- `pdftocairo` rendered all 4 pages
- `pdfimages -list` reports one embedded JPEG image on page 2
- `test_inferno_attack_styles`: 1441/1441 tests passed
- Style audit found no blocker-level writing issue in `paper_v0.pdf`
- `_C.env_action_dims()` sums to 89 in the current Inferno native binding check

## Evidence Files

- Venue and mechanics check: `papers/rlvg-inferno/review_notes_v8.md`
- Final blocker review: `papers/rlvg-inferno/review_notes_v9.md`
- Layout audit: `papers/rlvg-inferno/layout_audit_v0.md`
- Contract test audit: `papers/rlvg-inferno/contract_test_audit_v0.md`
- Style audit: `papers/rlvg-inferno/style_audit_v0.md`
- OpenReview upload checklist: `papers/rlvg-inferno/openreview_upload_checklist_v0.md`
