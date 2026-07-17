# PR draft: branch osrs-colosseum-inferno-v2 -> PufferAI/PufferLib 5c

Doctrine verdict applied 2026-07-17: terminal wins both envs, truncation
commits dropped. Branch rebuilt as osrs-colosseum-inferno-v2 (old remote
osrs-colosseum-inferno is stale, delete it). Commits:
16132364b env-name, 1bcda0fdd head arrays, 732602fe3 logratio clamp,
7fbb7b79d envs, 2a49ae9c7 encoder.

Title: OSRS Fortis Colosseum and Inferno environments

Body (plain text, paste as-is):

Adds two OSRS combat environments on the pufferenv.h seam: the Fortis
Colosseum (12 waves ending at Sol Heredit) and the Inferno (69 waves ending
at TzKal-Zuk), plus the shared OSRS simulation stack they build on (tick
combat, BFS pathfinding, prayers, inventory, consumables, special attacks).
Single agent, multidiscrete with native action masks: colosseum 20 heads /
452 logits over 3044 obs, inferno 9 heads / 89 logits over 1570 obs. Assets
(models, animations, collision maps) fetch automatically on first build from
a release archive with sha256 verification, so ./build.sh osrs_colosseum
works on a fresh clone.

Context: 5c branched before these envs landed on our 4.x-line fork, so none
of this tree exists on 5c yet. This supersedes our fork's OSRS tree, ported
and re-validated against the 5c trainer.

Five commits, each builds standalone:

1. build.sh fix: the pufferl cleanup introduced PUFFER_ENV_NAME but nothing
defines it, so native builds of every env fail at tip. One line. Happy to
split this into its own PR if you want it faster.

2. algo.cu: size the loss kernel's per-head arrays from the env action dim
instead of the hardcoded 16 (implements the existing TODO on that line; the
single-TU build makes NUM_ATNS compile-time visible). Above 16 heads the
fixed arrays silently corrupt kernel stack: no crash, entropy collapses,
nothing learns. Colosseum's 20 heads hit this. At or below 16 heads the
generated code is unchanged.

3. algo.cu: clamp the summed multidiscrete logratio to [-10, 10] before
__expf in the PPO loss. Many-head joint logratios overflow __expf into inf
ratios that poison the surrogate. Written as comparisons, not fmaxf, so a
pre-existing NaN still propagates. Identity for any logratio the clip range
would keep anyway. A/B on breakout and maze: bitwise identical trajectories;
minimal: within noise.

4. the environments themselves, configs, build wiring, and the C test
batteries (assert-based suites plus golden trajectory replays that pin the
sim bit-exact across refactors). Also makes the sim compile as C++ for the
single-TU trainer build, no -fpermissive needed.

5. ColosseumEntityEncoder in ocean.cu following the house pattern (env-named
block, strcmp dispatch, textually parallel to nmmo3/minimal): a global obs
projection plus fused GELU + masked-maxpool pools over 24 NPC records and 28
inventory slots.

Scope notes: zulrah and NH pvp encounter code rides in the shared stack and
is covered by the test suite, but has no build targets or configs yet, WIP.
The raylib viewer under --local runs local CPU inference of trained .bins.

Validation: both envs build with nvcc and train end-to-end on a 4090. Full
colosseum training run (143M steps): reaches wave 8.9 of 12, matching the
best results from our fork's trainer on identical config. Inferno learns to
wave 66+ of 69. Full C battery green: colosseum goldens 12/12, inferno
goldens 15/15, modifiers battery 10409, attack-style battery 1805.
