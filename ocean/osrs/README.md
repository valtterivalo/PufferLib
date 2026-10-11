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
and live unequip recipe (16). Recipes bind after guards to the current inventory
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

Macro 0 scores the actual production plan; macro 1 is no action. Native
rule comparisons continue their closed-loop controller and observer in each
cloned world. Java requests carry the current accepted open-loop run,
excluding an uncommitted conditional second ticket. Native closed-loop dumps
use `R UNKNOWN` when their future policy cannot be represented by this script.
Unknown pending or plan state yields unavailable scores, never a fabricated
empty baseline. The fourth response token is the best macro index, followed
by nine feasibility/kill/death/value/world-count groups.

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
