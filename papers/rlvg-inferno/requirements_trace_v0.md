# RLVG Inferno Requirements Trace v0

Date: 2026-05-25.

This file maps the current paper bundle to the user goal and the RLVG CFP. It is not a claim that the overnight goal is complete before the 09:00 Helsinki review.

## User Goal Requirements

- Write and iterate on a short-form 4 page RLVG paper about the OSRS Inferno environment and PufferLib systems work.
  - Evidence: `papers/rlvg-inferno/paper_v0.tex`
  - Evidence: `papers/rlvg-inferno/paper_v0.pdf`
  - Evidence: `papers/rlvg-inferno/layout_audit_v0.md`
- Use actual Puffer source code and local git history rather than only high-level prose.
  - Evidence: `papers/rlvg-inferno/source_notes.md`
  - Evidence: `papers/rlvg-inferno/contract_test_audit_v0.md`
  - Evidence: `papers/rlvg-inferno/git_history_notes.md`
- Use Joseph Suarez's Puffer articles through the browser review and source loop.
  - Evidence: `papers/rlvg-inferno/source_notes.md`
  - Evidence: references 3, 4, and 6 in `papers/rlvg-inferno/paper_v0.tex`
- Use external ChatGPT review and iterate from feedback.
  - Evidence: `papers/rlvg-inferno/review_notes_v0.md` through `papers/rlvg-inferno/review_notes_v9.md`
  - Evidence: `papers/rlvg-inferno/review_notes_v10.md`
  - Evidence: `papers/rlvg-inferno/style_audit_v0.md`
- Avoid unnecessary jargon and common AI-slop writing patterns.
  - Evidence: `papers/rlvg-inferno/style_audit_v0.md`
- Keep the loop active until morning rather than closing the goal early.
  - Evidence: this trace remains marked as pre-morning support, not completion.

## CFP Requirements

- Short-form paper is 4 pages, excluding references and appendices.
  - Evidence: `papers/rlvg-inferno/layout_audit_v0.md`
  - Evidence: `papers/rlvg-inferno/submission_manifest.md`
- Submission is through OpenReview.
  - Evidence: `papers/rlvg-inferno/review_notes_v8.md`
  - Evidence: `papers/rlvg-inferno/morning_handoff.md`
- No one-page cover page is required.
  - Evidence: `RLVG-call-for-papers.md`
  - Evidence: `papers/rlvg-inferno/review_notes_v8.md`
- Review is double blind.
  - Evidence: `papers/rlvg-inferno/submission_audit_v0.md`
  - Evidence: `papers/rlvg-inferno/style_audit_v0.md`
- LLM writing tools are allowed with human author responsibility.
  - Evidence: `RLVG-call-for-papers.md`
  - Evidence: `papers/rlvg-inferno/review_notes_v8.md`
- The paper fits benchmark and deployment, alignment and evaluation in games, and practical RL systems themes.
  - Evidence: sections 1, 2, 3, and 5 in `papers/rlvg-inferno/paper_v0.tex`
  - Evidence: `papers/rlvg-inferno/submission_audit_v0.md`

## Current Artifact Evidence

- Primary PDF: `papers/rlvg-inferno/paper_v0.pdf`
- PDF SHA-256: `5a02522b8fab35e07ee9de51b9f8257240c78e38bf37ae8d50da54299f3872cf`
- TeX SHA-256: `4f4ef23e722e69800e21f11b31f68d90958fd51890c70a1e138428ff784a03a3`
- Replay figure SHA-256: `4ef69b90fd5276f71e9f844f1a7e4ca7d0b45dcfad54459cfc4a5ca128567369`
- Morning entrypoint: `papers/rlvg-inferno/morning_handoff.md`
- Manifest: `papers/rlvg-inferno/submission_manifest.md`

## Open Risks

- The public CFP and live submission guide do not show a template requirement, but the OpenReview upload workflow could still expose one.
- The PufferLib and OSRS topic cluster may let insiders infer provenance, even though the PDF has no explicit author leak.
- Table 3 is development telemetry from an earlier compact action mapping, not a frozen-schema evaluation. The paper states this directly.

## Morning Completion Audit

At or after 09:00 Helsinki time, verify:

- Branch is clean and pushed.
- `paper_v0.pdf` hashes to the value above.
- `pdfinfo` still reports 4 pages, blank author, no JavaScript, and no encryption.
- Raw PDF and image string scans are still clean.
- The user has had a chance to inspect the bundle.

Only then decide whether to mark the goal complete.
