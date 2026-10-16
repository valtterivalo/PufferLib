# OSRS encounters

## Build

```bash
./build.sh osrs_colosseum
./build.sh osrs_inferno
```

## Train and eval

```bash
./puffer train osrs_colosseum
./puffer eval osrs_colosseum
./puffer eval osrs_colosseum --load-model-path=checkpoints/osrs_colosseum/model.bin
./puffer sweep osrs_inferno
```

## Tests

```bash
cc -std=c11 -O2 -I. -o /tmp/t ocean/osrs/tests/test_colosseum_golden.c -lm && /tmp/t
```

## Riskfight env throughput

`puf_step` of `osrs_riskfight` (what the trainer's env workers run) is built so that a tick rewrites only what changed. The observation and mask buffers are
persistent per agent and the env relies on that:

- A tick of the wait between fights steps no sim, so after the observation written in full at the start of the wait (`wait_obs_valid`) only the three session
  clock floats (`riskfight_session_clock`) are stored; the wait mask is not rewritten.
- The action mask is rewritten only when `RiskfightMaskKey` (everything `riskfight_write_action_mask` reads) differs from what the buffer holds. A new input of
  the mask writer has to be added to the key.
- The bf16 observation store is hand-vectorized (NEON, SSE2); the threat slots' pool prior is memoized per thread (`riskfight_pool_prior_memo`).

Every change must be bit-exact. `test_riskfight_env_trace.c` hashes every buffer the trainer reads after every step over 288 sessions in five cases (bf16 or
`-DTRACE_FLOAT` observations; goldens for Apple clang arm64 and x86-64 GCC, equal before and after the work), and compares the buffers with a full recomputation
every step. `bench_riskfight_env.c` measures fight ticks against idle ticks, single or multi-threaded, with a per-stage breakdown (`stages`) and a policy-free
replay of one action stream for profiling (`replay`):

```bash
cc -std=gnu11 -O2 -I. -Isrc -I$RAYLIB_INC -o bench ocean/osrs/tests/bench_riskfight_env.c riskfight_oppfit_bridge.o -lm -lpthread
RF_OPPFIT_MODEL=ocean/osrs/tests/fixtures/model.fit RF_OPPFIT_EAT=ocean/osrs/tests/fixtures/eat_mixture.fit ./bench 6000 64 script 1
```

## Gear Swapper advisory snapshots

Riskfight snapshots retain both players' committed damage queues. Unresolved PID
retains a queue for each possible holder; the sampled holder selects its queue
before uncertain damage is drawn once. Raw attacks retain launch-time prayer,
and final damage and reflections are not mitigated again. Queue metadata is
paired with each source/index and follows compaction, reuse and snapshot copies;
the shared `PendingHit`/`Player` layout is unchanged.

The lookahead server requires explicit pending and production-plan knowledge:

```text
P KNOWN <hit-count> | P UNKNOWN
D <source> <ticks-until-hit> <kind> <style> <special> <success> <damage-mode> <damage> <min> <max> <accuracy-bp> <defender-prayer> <smite>
P VARIANTS <local-holder-hit-count> <opponent-holder-hit-count>
D <holder> <source> <ticks-until-hit> <kind> <style> <special> <success> <damage-mode> <damage> <min> <max> <accuracy-bp> <defender-prayer> <smite>
R KNOWN <command-count> | R UNKNOWN
C <due-tick> <kind> <argument> <timing>
C <due-tick> <recipe-kind> 0 <timing> <candidate-count> <signed-raw-id>...
```

`D.source` is the attacker (0 local, 1 opponent); zero delay is the next
recipient phase. Hit kinds are direct (0), recoil (1), and Vengeance (2).
Damage modes are raw (0), final (1), and uncertain final (2). Smite is
landing-time (-1), inactive (0), or active (1).
`P VARIANTS` retains both concrete queues (holder 0 local, 1 opponent) until
the common world's holder is chosen, before damage sampling and macro cloning.
Known launch PID is frozen in both queues; fixed `P KNOWN` is used only when
the retained queues are identical. Missing concrete knowledge remains `P UNKNOWN`.
Counts are exact and each holder/source queue must fit the native capacity;
overflow or malformed records are rejected, never truncated.

`C.timing=0` is an unsent lower bound processed with the shared
FIRST/GAP/CUTOFF motor clock; `1` is an already-known landing or a no-click
guard. Command kinds are inventory slot (0), attack (1), enable spec (2),
Vengeance (3), disarm (4), remove worn slot (5), Ready (6), Hit (7),
Energy (8), HP (9), next-tick group (10), equip raw ID (11), consume raw
ID (12), toggle spec (13), live eat recipe (14), live equip recipe (15),
live unequip recipe (16), stop attack (17), and absolute aligned-ready (18).
Recipes bind after guards to the current inventory
and equipment, not the initial slot. Positive candidates are exact IDs; negative
eat candidates select the supported consumable dose family. Candidate order
preserves preference; equip skips any already-worn alternative. Empty equip and
unequip recipes retain the production skip behavior. Ambiguous eat cursor-path
selection, unsupported recipe metadata, or canonical aliases whose exact
identity is not retained make the comparison unavailable.
The unsupported raw food ID 24589 likewise returns `local_food_unsupported`,
rather than advertising a healing macro over an inert slot.
Guards do not consume clicks or reset the motor clock. Ready reengages the target
when required; bare Hit waits for attack registration, while a minimum-damage
Hit waits for damage.
Stop processing and observed clear are separate phases; commands after stop
cannot run before the following observation. Its motor clock remains charged.
Aligned-ready retires an expired launch instead of moving its deadline; ordinary
Ready remains a lower-bound cooldown guard.

Macro 0 scores the actual production plan; macro 1 is no action. Native
rule comparisons continue their closed-loop controller and observer in each
cloned world. Java requests carry the current accepted open-loop run,
excluding an uncommitted conditional second ticket. Native closed-loop dumps
use `R UNKNOWN` when their future policy cannot be represented by this script.
Unknown pending or plan state yields unavailable scores, never a fabricated
empty baseline. The fourth response token is the best macro index, followed
by eighteen feasibility/kill/death/value/world-count groups.
These include immediate single/burst specials, Vengeance combinations and three
delayed single/burst alignment windows. A delayed stop cannot be admitted when
its required observation and motor work cannot reach the absolute launch tick.

`RA OBSERVED <varp-172>` admits a stop only for observed OFF (`1`).
`RA OFF_PRIOR` and `PS <side> CLEAR_PRIOR` are explicit synthetic assumptions,
not historical facts. `PS <side> KNOWN <damage> <timer> <immunity>` retains
poison state; causal observer knowledge is admitted only for the local side.
Unknown poison or an unsupported local item makes the request unavailable;
occupied known inert slots are not erased or treated as consumables.
The world retains the sampled PID's priority-rank order while preserving its
independent shuffle clocks. Poison hits retain HP damage without attack credit,
PID evidence, recoil or Vengeance.

The Java recorder captures the exact live `PRODUCTION_PLAN_OBSERVED` frame before
transport. Historical replay without that event sends `R UNKNOWN`, not an empty
production baseline. Native closed-loop policy comparisons are bounded model
experiments, not certification of Java closed-loop parity or optimal play.

The 2026-10-12 release fixes reversed stake ownership in the server and evaluator:
wire `K` is opponent then local; `GsStakes` is initialized by named fields.
The final sealed 169-input candidate completed 2,880 bounded experiments:
76,677,120 evaluation outcomes plus 9,584,640 selector rollouts, two seeds,
separate trained/held-out opponent profiles, exact/uncertain PID, three stakes
ratios, 18 macro labels and a 15-tick horizon. The winning recording audit found
that eligible two-ticket bursts were disabled by default in these experiments
and the advisory server. The historical held-out mean empirical regret
(0–0.000426 local-stake units, with individual ranking reversals up to 0.0400)
therefore covers a restricted candidate set, not two-ticket burst optimality.
The selector used 512 worlds; production retains its 64-world default.
The baseline was an explicit synthetic plan, continuation scale zero and
auto-retaliate OFF a declared prior; no uncaptured Java baseline was invented.
Final recording inference matched all 622 automated and 240 manual ticks;
causal Java planner availability and unresolved recoil still remain unavailable.
The 30,000,000-step seeded clone/replay run completed 278,848 episodes with
consumer hash `730b37f0135dbb1c`. Repeated source epochs are not independent data.
The retained 52 legacy tapes have 8,117 unknown-clock frames and zero exact
clock frames. Their calibration run also rejects an unfitted ready-swing hazard
(`hazard 2 ... sits at its default`). These exports cannot certify snapshot
replay or that fitted hazard; fresh causal observer exports are required.

The winning `20261012T095332.861Z` recording exposed and corrected default burst
admission. The default producer regression now executes both tickets at legal
cooldowns without first enabling bursts inside the test. `GS_LA_BURST=0` remains
an explicit calibration ablation. The standalone evaluator uses the owning
`request_unavailable` validation: a known vial stays an occupied inert slot,
while unsupported items and unknown pending damage or production plans remain
unavailable. Usable Dharok axe variants 4886–4889 retain the full-set low-HP
damage effect; broken 4890 remains unsupported.

Five exact original live requests (ticks 617, 618, 685, 737 and 753) were compared
with 8,192 held-out damage worlds and 512 selector worlds per request over
15 ticks. Selection uses 36 training profiles and evaluation uses 8 held-out
profiles mined from spectator archives, not the advisory server's `MODEL_FIT`
or a fit to this particular opponent. Three evaluation seeds agreed at tick 618;
they vary damage worlds, not the tick-derived motor schedule. No subsequent
Java replanning is simulated, so these are not complete live-fight probabilities.
The tick-756 original frame is
not suitable for a kill counterfactual: it declared `P KNOWN 0` between the local
Vengeance proc and the 39+6 return hits. The Java producer now marks that
unresolved outgoing echo unavailable until a fresh observation settles its bound.

Exported tapes use one `Q <tick> <source>` row per tick: 0 is unavailable,
1 is explicitly empty synthetic state, and 2 is causal observer provenance.
Missing provenance remains unavailable. Duplicate or out-of-range provenance
and incomplete tick sequences are rejected at the tape boundary; future
hitsplats are not used to seed damage at an earlier branchpoint.

### Combat oracle target state

The Java eat/spec tapes explicitly declare their target inputs:

```text
T <history-present> <belief-present> <highest-base-hp>
TH <hard-ready> <potion-ready> <fast-ready> <length> <0-or-1-support>...
TB <hard-ready> <potion-ready> <fast-ready> <length> <probability>...
```

History retains all readiness alternatives for each possible HP; belief retains
weighted HP/readiness correlation. The native joint evaluator uses those clocks,
pending hard-food retries, food ordering and base-dependent brew healing. Missing
`T` is invalid, not permission to invent a range-only target. Native continuation
values match production Java; there is no separate native-only KO scaling.
Spec hit records retain the uniform-roll metadata used by Java's survival
reduction, rather than reconstructing it from a rounded probability array:

```text
H <lands> <damage-count> <origin> <first-positive-roll> <positive-each> <damage-probabilities>...
```

Standalone eat/spec fixtures are complete records from the 20261011 recording
at ticks 904–910, 1876–1888 and 2144–2152, plus the producer's independent
two-ticket spec cases. Full-recording differential gates run separately; the
bounded fixtures do not replace them.

Primary source wire records are separate from counterfactual pricing records.
Combat-level provenance, damage membership, pending queues and target state are
typed inputs. Native plan construction still uses its established measured press
approximation; source comparison reports plan availability/timing unavailable
without the Java hand-model inputs. This does not replace the accepted `R` plan
or its executed-click FIRST/GAP/CUTOFF timing.

The primary Java `PENDING` oracle has the `AutoEater.PendingDamage` scope:
opponent attack launches (including remaining ranged arrows), causally paired
incoming Vengeance returns, and unresolved Vengeance-proc knowledge. Its typed
knowledge, landing ticks, damage bounds, accuracy and arrow counts are compared
strictly. It is not the two-player native snapshot's `P` contract. The native
snapshot also retains outgoing local launches and unresolved recoil returns;
those additional sources keep `P UNKNOWN` and macro scores unavailable until
their own causal knowledge settles. A canonical `PENDING ... KNOWN 0 0` therefore
does not imply an empty or known full native queue. The source differential reports
extended-queue unavailability separately and never clears that native uncertainty
to match the narrower Java API. Eat/spec policy presence and plan timing remain
explicitly unavailable without serialized Java hand-model inputs; every serialized
health, pending, spec/underway input and supported plan-damage value is still gated.

Unresolved Vengeance-proc association lasts through the proc tick and retires only
at the next fresh observation, independent of the PID phase of a successfully
paired return. Multiple owned applied hits do not fabricate a unique pairing.
Spec input hard-food readiness is the earliest clock of its highest-base target
history, bounded below by the current tick, as in Java's `AutoSpec.Situation`;
it is not the earliest clock across all base-HP hypotheses.

Causal wire consumers respect the primary `S`/`SS ... END` oracle framing.
Target-history `T` rows inside those annotations are not recorded game ticks and
must never advance an observer or its retained health history. Streaming readers
reject missing or unmatched `END` boundaries rather than replaying oracle rows as
client inputs; the differential reader remains responsible for typed oracle parity.

`gs_wire_apply` owns the complete causal game-tick transition, including the
living target's health-bar/current-animation observation. Streaming consumers do
not need to request a verdict to advance health evidence or retire hidden meal
ticks, and must not repeat that observation after applying `T`. Real `X` and fake
`FHP` hitpoints gains use the observed **target** combat level for damage;
unavailable local combat level does not suppress them. `FHP` never advances the
cumulative real-XP baseline.
