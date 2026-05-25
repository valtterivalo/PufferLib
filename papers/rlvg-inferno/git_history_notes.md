# RLVG Inferno Git History Notes

Status: evidence notes from local git history, not submission prose.

Verification note: on the current `inferno-upstream-sync-may-14` branch, the named layout, observation, forecast, prayer, renderer, visual, lab, and paper commits are ancestors of `HEAD`. The healer-sequence hashes below are retained as local-history evidence for the iteration path. The active branch evidence for the same claim is the current healer metric surface in `ocean/osrs_inferno/binding.c` and the focused Inferno contract test.

## Why This Exists

The paper claims that Inferno became useful through a combined simulator, renderer, replay, logging, and training loop. The commit history supports that claim. The useful pattern is not a single miracle checkpoint. It is a sequence of contract repairs that made wrong policy behavior visible.

## Development Arc

- `729dd1fa4 sync inferno onto upstream layout`
  - Early branch alignment so Inferno could live inside the upstream-style PufferLib layout.
- `5cafedd73 fix inferno mechanics and lab tools`
  - Mechanics and lab tooling were corrected together, supporting the paper claim that playability and simulation correctness were linked.
- `163ceaaf4 update inferno observations and diagnostics`
  - Large observation and diagnostic expansion across env code, tests, binding, and Puffer logging.
- `42805984f update inferno default and forecast perf`
  - Added `bench_inferno_forecast.c`, showing forecast observations were treated as a performance-sensitive contract.
- `cbe546b55 fix inferno prayer flick semantics`
  - Shared prayer semantics touched Inferno, Zulrah, PvP, GUI, human input, render, tests, CUDA bindings, and Puffer trainer surfaces.
- `6d06b5c31 add inferno forecast obs sweep knob`
  - Forecast observations became an explicit sweepable environment knob.
- Healer sequence commits
  - `092546964 add inferno healer transition metrics`
  - `0324ef4c7 add restored-start healer diagnostics`
  - `e1d2d97cc add healer resolution diagnostics`
  - `7492f1191 Add Inferno healer targeting diagnostics`
  - `2bec1b157 Log Inferno healer transition ticks`
  - `4fd7a9524 fix inferno zuk healer sim`
  - This cluster supports the paper claim that late-Zuk progress needed behavior-level counters, not just scalar reward.
- `b81dab0d0 checkpoint osrs inferno renderer and state work`
  - Split the old monolithic Inferno encounter into focused `.inc` modules and added many asset, UI, render, cache-export, visual, lab, and test surfaces.
- Visual contract commits
  - `b8b153545 add combat projectile visual profiles`
  - `ba00710a1 add projectile launch spotanims`
  - `7ef7651a2 add inferno combat visual rows`
  - `0cb06585a add combat projectile sequences`
  - `8efec350f tighten inferno render asset contract`
  - These commits support the paper claim that rendered projectile and animation correctness became part of the benchmark contract.
- `41297e0dc Add OSRS lab visuals`
  - Added the browser lab path that later produced the paper replay screenshot.
- `ca32f8010 clarify rlvg checkpoint caveat`
  - Made the compact checkpoint caveat explicit in the paper after review.

## Paper Implications

- The current Table 2 is justified. It compresses repeated history into five representative failure modes.
- The paper should avoid a chronological changelog section. The four-page format cannot support it.
- The strongest history-derived sentence is already present: training, watching, tracing, contract tests, and sweeping formed one loop.
- If space opens, add one line that the healer and prayer fixes touched both environment code and human or visual surfaces, which is the concrete sign of a shared contract.
