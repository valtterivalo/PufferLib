# Engine wire v2 (plugin <-> C decision engine)

Owner: CDrive. Numeric definitions: `gs_engine_wire.h`. Server: `tests/gs_lookahead_server.c`. Java mirror: `EngineBridge`, `EngineRequest`, `EnginePlan` in `plugins/gear-swapper`.
Transport: a persistent child process, ASCII lines over stdin/stdout, one request per decision, one response line per request, strictly in order. stderr is diagnostics only.

## Lifecycle
1. Plugin starts the server: `gs_lookahead_server MODEL.fit EAT_MIXTURE.fit [WORLDS HORIZON [THREADS]] [--budget-ms MS] [--min-worlds N]`, cwd = a directory where `ocean/osrs/data/*.cmap` resolve. The two flags may stand anywhere (`--budget-ms` also from env `GS_BUDGET_MS`; the flag wins); see "Anytime budget".
2. Server loads the fits, self-warms (runs a synthetic decision stream through the full request->score->emit path), prints exactly one line
   `READY <worst_warm_micros> <macro names joined by ';'> BUDGET <ms> <min_worlds>` (`BUDGET 0 64` = no budget) and then reads stdin. The plugin blocks on READY at start-up, never at fight start.
   It refuses to run unless macro 0 is `continue` and macro 1 is `no new clicks`.
3. Each game tick the plugin may send one request. A request not answered inside the plugin's deadline (default 50 ms; budget p99 < 10 ms, max < 30 ms) is a loud failure:
   automation suspends, no fallback policy exists.

## Request (lines, terminated by `END`)
`LA id tick` header. Then in any fixed order: `B` (belief of the opponent: hp range, spec-energy range, vengeance reading, PID, his attack clock, food, seen weapons, bar ratio), `K` (stakes),
`F` (feasibility facts: spec ok, fed, castable, energy), `O` (own state: stats, prayers, timers, positions, our attack clock), `Q` (warning/unseen), `W` (13 worn raw ids), `G` (28 bag raw ids),
`SHIELD`, `ARMOR` (opponent equipment: `OBSERVED ids...` or `UNKNOWN`), `PS`, `RA` (poison and auto-retaliate), `P` (pending hit queues: `KNOWN n` + `D` records, `VARIANTS`, or `UNKNOWN`), `R` (committed plan remainder).
Field-by-field grammar of the existing tags: header comment of `gs_lookahead_server.c` and `EngineRequest.java`.
- Items are ALWAYS raw client ids (`-1` empty). Ids the engine has no row for are sent verbatim; they are never dropped or renamed by the plugin.
- `R KNOWN n` + n `C due kind arg timing [count cand...]` = the unsent remainder of the plan this engine emitted earlier (echo of its A records, `due` rebased to > tick). `R KNOWN 0` = nothing committed.
  There is no `R UNKNOWN`: the plugin has no plan of its own.

## Response (one line)
`R id server_micros best_index` then one `feasible kill death value worlds` block per READY name, in READY order (21 for the lookahead: indexes 0..17 are the spec, meal, Vengeance and alignment plans, 18..20 the upkeep drinks), then
`A n` + n records, then optionally `PRIOR k` + k triples `side slot raw` (what the engine substituted a declared prior for; side 1 = opponent, slot 0 weapon, 1 shield, 2.. armor kit slots).
then optionally `ASSUME n name...` = the declared priors the answer rests on (own-side observations the plugin could not complete on that tick, once per name on stderr too):
`pending_empty` (`P UNKNOWN`: a launch, echo or proc it could not pin down, served as `P KNOWN 0`) and `own_attack_ready` (attack clock `UNKNOWN`, served as ready: `OBSERVED -1 -1`; the one thing it can get wrong is a cooldown still running),
then optionally `BUDGET exceeded <micros>` (see "Anytime budget"),
or `UNAVAILABLE id reason [raw...]` when an own-side observation is missing that has no declared prior (poison state unknown, auto-retaliate unknown, attack clock interval). Opponent gear, pending hits and the first-swing clock are never a reason.
The plugin completes what it can before it falls back on a prior: both holders of PID are sent as `P VARIANTS` (his Vengeance echo on us lands a tick later only if we hold PID; our echo on him only if he does, as `D 0 .. 2 ..` of exact damage when one damaging hit can have procced it), and an attack clock never seen is sent as ready once the plugin has watched longer than the slowest weapon plus the longest meal without a swing of ours.
`best_index` 0 `continue` keeps the committed remainder (A echoes it); 1 `no new clicks` cancels it (`A 0`); any other macro replaces the remainder with its own plan.
Ties go to `continue`: another macro needs a strictly better value.

## Anytime budget (lookahead server)
The decision is evaluated on common random worlds in stages (successive elimination after 16, 64, 256 worlds). A loaded host (the game client, another process) can deschedule one of the 8 worker threads and delay a whole
decision by tens of ms, so the evaluation is anytime with a wall-clock budget: `--budget-ms MS` (default 12; `0` = off) measured from the moment the request has been read.
- After the deadline no world beyond the minimum (`--min-worlds`, default 64: regret against 512 worlds is about 0.003..0.007 in value units at 64 and about 0.0007..0.002 at 256) is started and no new stage begins once the minimum is complete.
  The completed worlds are always a prefix of the world sequence, so the answer is exactly what an unbudgeted run with that many worlds returns (`tests/test_lookahead_budget.c` checks byte equality); the `worlds` column of every macro says how many it got.
  `--budget-ms 0` evaluates all of them: today's deterministic behaviour, what tests and evaluators use.
- Below the minimum the deadline is ignored (correctness first). If that pushed the answer past the deadline the response ends in ` BUDGET exceeded <micros>` (micros past the deadline); the plugin logs it as a WARN and accepts the answer.
- A worker that has not published its world when the others are done is waited for ~150 us (3 mean world times) and then its world is recomputed by the answering thread (same pure function, same result); the late worker discards its copy.
  The helpers are not waited for after the answer is written either, but they must leave before the next request is parsed (the parser rewrites the stat-priced item rows the rollouts read): the server waits for them right after it has answered, while the plugin waits for the next game tick.
- The plugin's own 50 ms deadline stays the hard failure; the budget is what keeps a normal answer far from it.
- Threads: default 6 (was 8). Measured on pufferbox (32 threads shared with other jobs, about 2.5x slower per core than the Mac; server and 2..5 busy-loop processes pinned to the same 4 or 8 logical CPUs with `taskset`, budget 12 ms, 200..300 requests a run, 3..4 runs):
  mean of the run p99 in ms over loads of 0 / 2 / 3 hogs on 4 CPUs and 0 / 3 / 5 hogs on 8 CPUs: 4 threads 14.0 24.5 30.9 / 13.6 22.8 21.5; 6 threads 14.8 20.2 25.5 / 12.6 15.8 18.8; 8 threads 15.6 19.1 32.4 / 14.5 16.5 21.7.
  4 threads are 20-25% worse under contention, 6 and 8 are within the noise of each other with 6 ahead in 4 of 6 scenarios and leaving cores to the game client.

## The action list (what the plugin executes)
`A` is a concrete, tick-indexed action list: each record is `due kind arg timing [count cand...]`, `kind` from `GsWireKind` (see header): clicks (slot, attack, spec, cast, unequip, equip, item),
barriers (ready, hit, spec launch, aligned ready), conditions (energy, hp), group boundaries and recipes. A search plan may use only timed clicks:
`due=T kind=CLICK_SLOT arg=slot timing=1` means "have that click processed on tick T". The plugin maps a plan to ActionRun groups; a record whose `due` is later than tick+1 is held behind a tick gate
(start the hand at tick due-1). Records execute in list order within a group; groups (kind 10) are sequential. `due` is never in the past of the request tick; ties keep list order.

## Upkeep macros (18..20, appended after the original 18)
`drink prayer`, `drink restore`, `drink combat`: `[click the carried potion's slot (kind 0), (Vengeance cast, kind 3), attack]`, timed through the motor model like the meals. They read only the O and G lines
(prayer points, boosted levels, active prayers, potion timer, bag) and the F `castable` bit; no wire field was added. A plain slot click, not a family recipe (the recipe candidates are canonical sim ids, the bag holds client ids).
No worth term was added to the value. The rollouts price the effects exactly inside the horizon (an empty prayer turns Piety, Protect and Smite off; a drained level lowers accuracy and max hit; magic under 94 blocks Vengeance; a drink restores at once), so
a drink is a macro only in a state where its effect shows inside the horizon, and the value (kill and death over the rollout) decides whether it is worth the click:
- `drink prayer`: a prayer potion (else a super restore) is carried and the prayers that are on drain the points to 0 within the horizon (drain counter taken at its fullest). An empty prayer is not a state it acts on: nothing is on, and the engine has no prayer click (PrayerKeeper's job).
- `drink restore`: a super restore is carried and a level the weapon swings with, or defence, is below base, or magic is under 94 with a restore bringing it back and Vengeance otherwise castable (not up, off cooldown, a sack carried, F says not castable): then the plan casts Vengeance after the drink.
- `drink combat`: a super combat is carried and attack/strength (melee weapon) or defence is below base plus the boost.
All three need the potion timer 0 (O line) at the request. A drink locks hard food three ticks (the sim and the game agree) and adds no attack delay; the click only interrupts the attack, hence the closing attack record.
A level the horizon does not see is not priced: stat drain that would only matter after it, and the three-tick food lock after a drink (the rollouts' baseline holds, it does not eat).

## Invariants
- parse(emit(plan)) is the identity for every plan the engine emits; an echoed A is a valid R.
- One decision per observed tick; while a sent click's effect has not been observed the plugin does not ask (it logs `committed`) and the remainder keeps executing.
- Any protocol violation by either side is fatal for the session: the server exits 2 with the failing assertion on stderr, the plugin throws.
- Changing a kind number, a tag grammar or the response shape bumps `GS_ENGINE_WIRE_VERSION`; `READY` will carry it once the Java side parses it (today: macro-name check).

## Swappable decision policy (wire v2.1, backward compatible)
The plugin never knows which algorithm decides. The server is started with `--policy lookahead|search|rl:CKPT` (default lookahead; every policy answers with the SAME response shape):
- `READY <warm_micros> <names;...> [POLICY <name>] [OBS]`. names always begin `continue;no new clicks;` and may end with the policy's own plan names. A policy that is not a macro lookahead lists a single plan name (e.g. `search`, `rl`) at index 2; its `kill death value worlds` block entries are the policy's own estimates for that plan (worlds = samples, 0 and feasible 0 when it has none), and `best_index` 2 means: replace the remainder with the A records. Ties keep favouring continue.
- `OBS`: the policy needs the observation every game tick, not only decision ticks (RL history rings, search belief updates). The plugin then sends `LA id tick` ... `END` with an extra header token `OBS` (`LA id tick OBS`) on ticks it would not decide (committed run, unlanded clicks); the server updates its per-fight state and answers `O id`. No A records, no plan change.
- Per-fight state (history rings, recurrent state) is owned by the server; it resets on a tick gap > 2 or on `FIGHT tick` (a header line before the first request of a fight, sent by the plugin when the tracked opponent changes).
- Latency budget applies per request including OBS requests.

## Release build (what the plugin launches)
One command, from any directory inside the checkout, on the Mac (arm64, `-O2`, no sanitizers, asserts kept: a violated protocol invariant must abort with exit 2):

    ocean/osrs/gearswapper/build_engine_release.sh [RELEASE_DIR]      # default ~/.light2/engine-release; OSRS_DATA=<dir> overrides ocean/osrs/data

It runs exactly

    cc -std=c11 -O2 -I. -o RELEASE_DIR/gs_lookahead_server ocean/osrs/tests/gs_lookahead_server.c -lm -lpthread
    cc -std=c11 -O2 -I. -o RELEASE_DIR/gs_rl_server        ocean/osrs/tests/gs_rl_server.c        -lm -lpthread

and lays out the files `EngineBridge` expects (`~/.light2/opponent-loop` is the live default; point the plugin at a release with `-D` properties for server, model, mixture and home):

    RELEASE_DIR/gs_lookahead_server
    RELEASE_DIR/gs_rl_server                                  (needs an RL checkpoint of the current observation size; none is shipped)
    RELEASE_DIR/model.fit  eat_mixture.fit                    (opponent fit v2, copied from ocean/osrs/tests/fixtures)
    RELEASE_DIR/home/ocean/osrs/data/wilderness.cmap          (the only data file the server reads; cwd of the child = RELEASE_DIR/home)
    RELEASE_DIR/BUILD                                         (git sha, date, compiler, platform)

The script finishes by starting the server once with stdin at EOF and requires its `READY` line. The server takes `MODEL.fit EAT_MIXTURE.fit [WORLDS HORIZON [THREADS]] [--budget-ms MS] [--min-worlds N]` (defaults 512 worlds, horizon 15, 6 threads, budget 12 ms, minimum 64 worlds).
The C suite is `ocean/osrs/tests/run_suite.sh [-j N] [-t SECONDS] [test_name ...]` (on Linux it needs `RAYLIB_INC`/`RAYLIB_LIB` for the render and model tests).

Rule (Java executor): a policy plan (best_index >= 2) with `A 0` cancels the committed remainder, like macro 1. A policy plan answers `feasible 0 worlds 0` in its own score block when it has no estimate; the plugin accepts an infeasible best only for index >= 2 on a server that announced a non-lookahead POLICY.
