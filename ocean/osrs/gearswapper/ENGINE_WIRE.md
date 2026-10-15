# Engine wire v2 (plugin <-> C decision engine)

Owner: CDrive. Numeric definitions: `gs_engine_wire.h`. Server: `tests/gs_lookahead_server.c`. Java mirror: `EngineBridge`, `EngineRequest`, `EnginePlan` in `plugins/gear-swapper`.
Transport: a persistent child process, ASCII lines over stdin/stdout, one request per decision, one response line per request, strictly in order. stderr is diagnostics only.

## Lifecycle
1. Plugin starts the server: `gs_lookahead_server MODEL.fit EAT_MIXTURE.fit [WORLDS HORIZON [THREADS]]`, cwd = a directory where `ocean/osrs/data/*.cmap` resolve.
2. Server loads the fits, self-warms (runs a synthetic decision stream through the full request->score->emit path), prints exactly one line
   `READY <worst_warm_micros> <macro names joined by ';'>` and then reads stdin. The plugin blocks on READY at start-up, never at fight start.
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
`R id server_micros best_index` then 18 x `feasible kill death value worlds` (macro order = READY names), then
`A n` + n records, then optionally `PRIOR k` + k triples `side slot raw` (what the engine substituted a declared prior for; side 1 = opponent, slot 0 weapon, 1 shield, 2.. armor kit slots).
or `UNAVAILABLE id reason [raw...]` when an own-side observation is missing (pending hits unknown, attack history unknown, poison unknown, auto-retaliate unknown). Opponent gear is never a reason.
`best_index` 0 `continue` keeps the committed remainder (A echoes it); 1 `no new clicks` cancels it (`A 0`); any other macro replaces the remainder with its own plan.
Ties go to `continue`: another macro needs a strictly better value.

## The action list (what the plugin executes)
`A` is a concrete, tick-indexed action list: each record is `due kind arg timing [count cand...]`, `kind` from `GsWireKind` (see header): clicks (slot, attack, spec, cast, unequip, equip, item),
barriers (ready, hit, spec launch, aligned ready), conditions (energy, hp), group boundaries and recipes. A search plan may use only timed clicks:
`due=T kind=CLICK_SLOT arg=slot timing=1` means "have that click processed on tick T". The plugin maps a plan to ActionRun groups; a record whose `due` is later than tick+1 is held behind a tick gate
(start the hand at tick due-1). Records execute in list order within a group; groups (kind 10) are sequential. `due` is never in the past of the request tick; ties keep list order.

## Invariants
- parse(emit(plan)) is the identity for every plan the engine emits; an echoed A is a valid R.
- One decision per observed tick; while a sent click's effect has not been observed the plugin does not ask (it logs `committed`) and the remainder keeps executing.
- Any protocol violation by either side is fatal for the session: the server exits 2 with the failing assertion on stderr, the plugin throws.
- Changing a kind number, a tag grammar or the response shape bumps `GS_ENGINE_WIRE_VERSION`; `READY` will carry it once the Java side parses it (today: macro-name check).
