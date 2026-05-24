# RLVG Inferno Submission Audit v0

Status: paper artifact current after the final surface wording pass. Venue and format documentation checked through `b0b517b4a`.

## Hard Requirements

- Short-form limit: pass. `paper_v0.pdf` is 4 pages by `pdfinfo`.
- References excluded from page limit: pass. The PDF includes references on page 4, still within 4 pages.
- Double blind: pass in the PDF. The author field is `Anonymous Authors`, PDF date metadata is omitted, and raw PDF strings contain no local paths, W&B ids, personal names as authors, or `valtterivalo` references.
- CFP fit: pass. The paper clearly targets benchmark design, alignment and evaluation in games, and practical RL systems for modern game-derived tasks.
- LLM policy: compatible. The CFP allows LLM writing tools if human authors own correctness and originality.
- External venue check: pass. On 2026-05-25, the OpenReview RLVG 2026 venue page listed the submission deadline as May 28 2026 11:59 UTC, which matches May 27 23:59 AoE. Its visible text points authors to the venue website and does not add a separate style-file requirement beyond the local CFP.

## Evidence Strength

- Environment surface: strong. The 744 base observation features, 9 action heads, and 89 discrete choices with matching embedded mask entries come from current Inferno config, encounter constants, and native binding action dimensions.
- OSRS task description: strong enough for short-form. The paper cites the OSRS Wiki for 69 waves, no restock, pillars, and Zuk shield mechanics.
- PufferLib 4 systems claims: strong enough for workshop prose. The paper cites public Puffer docs, Joseph Suarez articles, MinGRU, and the PufferLib arXiv paper.
- Checkpoint result: intentionally caveated. The stored checkpoint belongs to an earlier compact Redemption action surface, not the current explicit 89-logit surface. The paper now says this directly in the abstract and checkpoint section.
- Score definition: fixed. Table 3 reports the logged training score from `ocean/osrs_inferno/binding.c`, not the archive `progress_score` from the encounter snapshot path.
- Fresh evaluation: missing. A frozen-schema no-render eval would make the result table stronger, but the current compact checkpoint size does not match the current decoder. The remote run directory still exists, but it contains artifacts rather than the exact source checkout needed for a clean compatibility eval.

## Current Acceptance Risks

- The paper is more benchmark-construction note than benchmark leaderboard paper. This is acceptable for RLVG short-form but should stay explicit.
- The checkpoint caveat is honest but reduces headline result strength.
- The references to X articles are informal. They support systems context but should not be the only foundation for technical claims.
- The result table is precise because it reports stored telemetry. The prose now warns that it is downsampled and not a precise public eval.
- The PDF uses a real replay screenshot, but not an annotated figure with log snippets. That would improve the inspectability argument if there is time.
- The replay figure is embedded as stripped JPEG. The original PNG rendered correctly as an image file, but the PDF preview path was more robust after switching away from the PNG embed.

## Next Useful Edits

- Add one small appendix or supplemental note later with exact checkpoint metadata if the workshop format allows appendices.
- If a compatibility eval path is restored, replace Table 3 with explicit no-render evaluation over a pinned episode count.
- The abstract caveat was trimmed after the final risk pass. Keep it short unless new evidence changes the claim.
- If space opens, add one sentence saying human play was used as an action-interface test, not only as visual QA.
