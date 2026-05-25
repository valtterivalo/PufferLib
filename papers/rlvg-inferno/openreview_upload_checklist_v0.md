# RLVG Inferno OpenReview Upload Checklist v0

Status: upload checklist for the current `paper_v0.pdf` short-form artifact.

## Submit

- Upload only `papers/rlvg-inferno/paper_v0.pdf`.
- Use the paper title: `Training Through the Inferno: An Inspectable OSRS Boss-Fight Benchmark for RL`.
- Keep the PDF anonymous. The visible author line is `Anonymous Authors`.
- Use actual human authors only in OpenReview author fields if the site asks for them.
- Do not upload TeX source, notes, audits, local screenshots, checkpoint artifacts, W&B links, or repository paths.

## Preflight

- Confirm PDF SHA-256: `7f2d3d319bee99bb1a2e980b5c0076fdbb0c6e252b2883024d5b2d9c05d10cc7`.
- Confirm the PDF has 4 pages on letter size.
- Confirm `pdfinfo` shows blank author metadata, no JavaScript, and no encryption.
- Confirm raw PDF string scans show no local paths, personal names, W&B ids, OpenAI, Codex, or pufferbox strings.
- Confirm the submission type is short-form.
- Confirm the venue is RLVG 2026 on OpenReview.
- Confirm the review mode remains double blind.

## Accepted-Paper Logistics

- If accepted, at least one coauthor must register for RLC 2026 and present in person.
- The workshop has no remote attendance or presentation option.

## Evidence Files

- Submission manifest: `papers/rlvg-inferno/submission_manifest.md`
- Submission audit: `papers/rlvg-inferno/submission_audit_v0.md`
- Source-claim audit: `papers/rlvg-inferno/source_claim_audit_v0.md`
- Requirements trace: `papers/rlvg-inferno/requirements_trace_v0.md`
- Morning handoff: `papers/rlvg-inferno/morning_handoff.md`
- OpenReview form fields: `papers/rlvg-inferno/openreview_form_fields_v0.md`
- Live venue check: `papers/rlvg-inferno/review_notes_v8.md`
- Final blocker review: `papers/rlvg-inferno/review_notes_v9.md`
- Final Extended Pro review: `papers/rlvg-inferno/review_notes_v11.md`

## Remaining Risks

- Hidden OpenReview form fields may expose a template requirement not listed in the public guide.
- Topic familiarity may create soft anonymity risk for reviewers who know the PufferLib and OSRS work.
- Table 3 is development telemetry from the checkpoint line, not a frozen-schema public evaluation.
