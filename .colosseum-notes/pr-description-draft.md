# PR draft: branch osrs-colosseum-inferno -> PufferAI/PufferLib 5c

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

Five commits, each builds standalone:

1. build.sh fix: the pufferl cleanup introduced PUFFER_ENV_NAME but nothing
defines it, so native builds of every env fail at tip. One line. Happy to
split this into its own PR if you want it faster.

2. truncation channel: Agent grows float* truncations mirroring terminals
through every vec/rollout/train buffer. In the GAE a truncation zeroes the
lambda-recursion boundary but keeps the gamma*V bootstrap term; a terminal
still zeroes both. RNN state reset treats terminal-or-truncation as an
episode boundary while the advantage math sees the channels separately.
With truncations all zero the advantage computation is bitwise identical to
current behavior. Motivation: time-limit episode ends otherwise train as
deaths (bootstrap from V=0), which poisoned value estimates badly enough in
these envs that we treat it as a correctness fix. The OSRS envs consume the
channel in commit 5.

3. the environments themselves, configs, build wiring, and the C test
batteries (assert-based suites plus golden trajectory replays that pin the
sim bit-exact across refactors). Also makes the sim compile as C++ for the
single-TU trainer build, no -fpermissive needed.

4. ColosseumEntityEncoder in ocean.cu following the house pattern (env-named
block, strcmp dispatch, textually parallel to nmmo3/minimal): a global obs
projection plus fused GELU + masked-maxpool pools over 24 NPC records and 28
inventory slots.

5. the envs report time-limit truncations on the new channel (two-line
consumers).

Scope notes: zulrah and NH pvp encounter code rides in the shared stack and
is covered by the test suite, but has no build targets or configs yet, WIP.
The raylib viewer under --local runs local CPU inference of trained .bins
(and builds on arm64 mac: SIMD flags are arch-conditional, openmp detection
handles apple clang; linux paths unchanged).

Validation: both envs build with nvcc and train end-to-end on a 4090 (2M
step smokes, metrics flowing, checkpoints written). Full C battery green:
colosseum goldens 12/12, inferno goldens 15/15, modifiers battery 10409,
attack-style battery 1805.
