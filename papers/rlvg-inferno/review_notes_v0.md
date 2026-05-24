# Review Notes For Draft v0

Status: revision notes from local critique plus ChatGPT browser critique on 2026-05-25.

## Main Diagnosis

The core is strong: an inspectable OSRS Inferno benchmark, built around player-readable state, structured action heads, strict masks, replayable failures, and high-throughput recurrent training.

Draft v0 reads too much like an internal lab report. It spreads the contribution across the Inferno, PufferLib 4, checkpoint numbers, and general RLVG philosophy. The next draft should put the benchmark contract first and make PufferLib 4 the enabling system.

## Revision Targets

- Name the contribution in the abstract and first page.
- Collapse eight sections into five.
- Cut most internal run names from the main prose.
- Treat checkpoint numbers as development evidence unless the exact eval protocol is added.
- Add one compact design table.
- Reserve space for one annotated replay figure or training curve.
- Keep the wit, but avoid cute claims where a reviewer can attack scope.

## Best External Critique Points

- A four-page paper should not feel like a compressed dissertation.
- The contribution is credible if framed as a benchmark pattern, not as a solved game result.
- The older checkpoint result distracts unless presented in a non-comparable historical table.
- "Player-readable failures" is the strongest phrase and should become a central term.
- Action masks should be framed as removing interface noise, not making the game easier.
- The result table needs protocol caveats: selection rule, eval episodes, seeds, start distribution, and stochastic versus deterministic actions.

## V1 Shape

1. Introduction and contribution.
2. Benchmark surface.
3. Training and iteration loop.
4. Checkpoint evidence.
5. Lessons and limitations.

