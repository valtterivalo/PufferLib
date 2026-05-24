# RLVG Inferno Submission Audit v0

Status: current after commit `ca32f8010`.

## Hard Requirements

- Short-form limit: pass. `paper_v0.pdf` is 4 pages by `pdfinfo`.
- References excluded from page limit: pass. The PDF includes references on page 4, still within 4 pages.
- Double blind: pass in the PDF. The author field is `Anonymous Authors`, and the paper contains no local paths, W&B ids, personal names as authors, or `valtterivalo` references.
- CFP fit: pass. The paper clearly targets benchmark design, alignment and evaluation in games, and practical RL systems for modern game-derived tasks.
- LLM policy: compatible. The CFP allows LLM writing tools if human authors own correctness and originality.

## Evidence Strength

- Environment surface: strong. The 744 observation features, 9 action heads, and 89 mask logits come from current Inferno config and source constants.
- OSRS task description: strong enough for short-form. The paper cites the OSRS Wiki for 69 waves, no restock, pillars, and Zuk shield mechanics.
- PufferLib 4 systems claims: strong enough for workshop prose. The paper cites public Puffer docs, Joseph Suarez articles, MinGRU, and the PufferLib arXiv paper.
- Checkpoint result: intentionally caveated. The stored checkpoint belongs to an earlier compact Redemption action surface, not the current explicit 89-logit surface. The paper now says this directly in the abstract and checkpoint section.
- Fresh evaluation: missing. A frozen-schema no-render eval would make the result table stronger, but the current compact checkpoint size does not match the current decoder.

## Current Acceptance Risks

- The paper is more benchmark-construction note than benchmark leaderboard paper. This is acceptable for RLVG short-form but should stay explicit.
- The checkpoint caveat is honest but reduces headline result strength.
- The references to X articles are informal. They support systems context but should not be the only foundation for technical claims.
- The result table is precise because it reports stored telemetry. The prose now warns that it is downsampled and not a precise public eval.
- The PDF uses a real replay screenshot, but not an annotated figure with log snippets. That would improve the inspectability argument if there is time.

## Next Useful Edits

- Add one small appendix or supplemental note later with exact checkpoint metadata if the workshop format allows appendices.
- If a compatibility eval path is restored, replace Table 3 with explicit no-render evaluation over a pinned episode count.
- Consider trimming the abstract by one sentence if the submission system renders cramped previews.
- If space opens, add one sentence saying human play was used as an action-interface test, not only as visual QA.
