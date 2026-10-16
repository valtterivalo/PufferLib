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

## Anglerfish overheal lock (no wire field)
An anglerfish overheals only for a player who was not attacked or damaged by the other player (any hitsplat, 0s included) in the last 16 ticks (wiki Anglerfish; the arena is always a PvP area); the sim keeps `Player.pvp_overheal_block` (ticks left, set to 16 when a hit lands, counted down every tick) and both sides obey it.
A snapshot has no field for it: `riskfight_load_state` derives each side's lock from the other side's last swing (`seen_last_attack_ago`: a swing `s >= 1` ticks ago landed `s - 1` ticks ago, lock `17 - s`; a swing that has not landed is in the pending queue and sets the lock when the sim lands it).
The O line's `his_last_ago` is the plugin's estimate from his attack timer and reads 0 while that timer is unseen, so the lock is exact only when the timer is; the RL policy never reads it (its observation and mask do not carry it), the lookahead's rollouts do.
An exact source would be one more request line, `HU ago` = ticks since the last hitsplat on the local player from the tracked opponent (-1 when none), stamped by the hitsplat handler like `EngineSeen.hit`; not implemented.

## Threat features of the RL observation (no wire field)
The RL observation is `RF_PRICE` (357), then 90 threat floats (`RF_THREAT_START` = 358; the last two are the Soulreaper stacks of him and of us; layout in `encounters/riskfight/riskfight_threat.h`), then the 7 session floats (`RF_SESSION_START` = 448, `RF_OBS_SIZE` = 455; "RL session block" below). The threat floats are five slots of his weapons (the wielded one, the weapons he has swung this fight, then the weapons the opponent pool says a fighter in his visible gear carries, with the probability each is in his kit) and four slots of ours (the wielded weapon and the bag's), each with max hit, hit chance, speed and special (cost, most it can deal, hit chance). Everything is computed by the server from fields the request already has, with the same code the sim uses:
- his gear and weapon: the `W` / `ARMOR` / `SHIELD` lines and his weapon of the `O` line, through `osrs_threat_weapon` (item rows, `osrs_spec_cost`, `osrs_spec_accuracy`); the weapons he swung: `swing_raw` of the `V` lines (the server stores raw ids and re-resolves them every request, because the stat-priced item rows are rewritten per request);
- our side: the `O` line's levels, our `W` and `G` lines (worn gear and bag), our prayer;
- his levels, prayer, ammunition and defence are assumed (`OSRS_THREAT_ASSUMED_*`: a maxed account on piety, rigour or augury with a super combat or ranging potion, defence 99; the ammunition of `osrs_ammo_assumption`), his ring is not part of the features.
An item the sim has no row for is a prior (`[prior]` on stderr), as before; weapons met in recordings have rows (`ocean/osrs/items/wiki_item_rows.tsv`, one wiki row each, `scripts/gen_osrs_item_rows.py`).
Summer pies are modelled: the server's bag view keeps raw 7218 (summer pie, 2 bites), 7220 (half a summer pie, 1 bite) and 2313 (the pie dish a last bite leaves), so the sim holds the same cells as a native pies kit and the food head can pick them; `CLICK_SLOT` on such a slot makes the plugin click the item's left-click option, Eat (`EnginePlan.click`). Every other food the sim has no row for is still inert filler. (`F` line `fed` is computed from `EngineRequest.FOOD_IDS`, which has no pie: the RL server does not read it.)
Soulreaper axe: the client shows no stack counter of another player (the wiki puts it in the wielder's own special-attack interface), so his stacks are counted from his swings as `SoulStacks.java` does: +1 (cap 5) per swing with the axe, a swing with the Behead animation (10173) takes them all, one is lost per 50 ticks since his last axe swing; stacks he held before the fight are assumed 0. A Behead is told apart only by the optional `swing_spec` field of the `V` line: the plugin must send `swing_spec` = 1 for animation 10173 (it costs no special energy, so his energy drop cannot show it); without it his stacks read too high until they decay.
One optional wire field would make our own block exact: our attack style (the combat-style varp 43, 0..3). The sim's Player starts on accurate and the request does not carry it, so our max hit is rated on accurate (3 effective levels under aggressive, up to 1 point of max hit).

## Swappable decision policy (wire v2.1, backward compatible)
The plugin never knows which algorithm decides. The server is started with `--policy lookahead|search|rl:CKPT` (default lookahead; every policy answers with the SAME response shape):
- `READY <warm_micros> <names;...> [POLICY <name>] [OBS]`. names always begin `continue;no new clicks;` and may end with the policy's own plan names. A policy that is not a macro lookahead lists a single plan name (e.g. `search`, `rl`) at index 2; its `kill death value worlds` block entries are the policy's own estimates for that plan (worlds = samples, 0 and feasible 0 when it has none), and `best_index` 2 means: replace the remainder with the A records. Ties keep favouring continue.
- `OBS`: the policy needs the observation every game tick, not only decision ticks (RL history rings, search belief updates). The plugin then sends `LA id tick` ... `END` with an extra header token `OBS` (`LA id tick OBS`) on ticks it would not decide (committed run, unlanded clicks); the server updates its per-fight state and answers `O id`. No A records, no plan change.
- Per-fight state (history rings, recurrent state) is owned by the server; it resets on a tick gap > 2 or on `FIGHT tick` (a header line before the first request of a fight, sent by the plugin when the tracked opponent changes).
- Latency budget applies per request including OBS requests.
- RL session block (`RF_SESSION_START`, `RF_SESSION_SIZE` = 7 entries, appended behind the rest of the observation): `[0]` session ticks left over 1500, capped at 1, `[1]` his special energy as a player counts it, `[2]` 1 during a wait between fights (training only: every tick of a wait is a step with only the no-op allowed), `[3]` the wait ticks left over 1000, `[4]` our reputation (rude teleports of the session, decaying) over 4, capped at 1, `[5]` how annoyed he is with us: our overeats of this fight over `RF_OVEREAT_PATIENCE` + 1 (= 11), capped at 1, and `[6]` our hitpoints over his chance range, `(hp - range) / 121` in [-1, 1]. A live session has no budget and no waits, so the server always feeds `[0] = 1`, `[2] = [3] = 0` ("the end of the session is far", which is also what most of a training session shows). `[1]` starts at 100 at every `FIGHT`, loses the cost of every special swing the `V` line reports (`swing_spec` = 1, optional tenth field, 0 when absent: the client must set it when his attack animation is a special one, VW 11275, maul 1667, ...) and regenerates 10 per 50 ticks below 100. A client that does not send `swing_spec` leaves `[1]` at 100. `[6]` is the hitpoints of the `O` line over the same chance range as `[5]` (`riskfight_chance_range`: his largest normal max hit, his special's when his estimated energy allows it, the echo of our hit while his Vengeance is up).
- Session `[4]` is always 0 live: our reputation is the server-side state of the sim's session (how many rude teleports we have made lately), not something a client can observe, so the wire has no field for it and the server feeds none. A policy trained with reputation sees it as 0, the most common value of a training session.
- Session `[5]` is counted from the wire, per fight (reset by `FIGHT` or a tick gap above 2), with the sim's own definition (`riskfight_step.h`): a meal eaten while our hitpoints at the START of that tick were strictly above his chance range is an overeat; each meal counts (a food + karambwan + brew triple-eat above the range is three overeats, the tick's first meal does not move the bar of the others); a meal is food or a karambwan or a pie bite or a brew dose, not a restore, super combat or other potion. The request of tick N carries the state after tick N, which is the start of tick N + 1, so the meals of tick N + 1 are judged against request N's `O` hitpoints and chance range. Meals come from the `G` line: the servings (a brew's doses, a pie's two bites, one per food item) that left the bag between consecutive requests, totalled per delay clock (hard food, karambwan/halibut, potion), so moving items between slots counts as nothing. At most one meal per clock per tick (a clock has a 1 to 3 tick delay), so a tick has at most 3 meals; a clock that loses 2 or more servings at once lost them some other way and counts no meal (`bag_unexplained`). The first request of a fight counts nothing, and a tick missing from the stream (gap 2) is judged against the last request seen. Foods without a content row in the sim's inventory table are not meals.
- Why not the `O` timers: they are right about WHEN a meal happened (a jump to 3 of the food, potion or karambwan timer, on the request where the bag changes) but cannot tell meals apart: a karambwan sets all three timers, so karambwan alone, karambwan + hard food and karambwan + brew all read `3 3 3` (the recorded triple-eat of tick 180 of `/tmp/t4.requests` and its karambwan + anglerfish pairs read the same); a brew reads `3 3 0` with or without a hard food, and so does a restore, a super combat or a prayer potion (which are no meals). They also miss a meal now and then (the first anglerfish of `fixtures/live_swings.requests`, tick 416, moves the bag and the hitpoints but no timer). The hitpoints cannot count them either: damage lands on the same tick. The bag counts every meal of both recordings (48 + 595 ticks, 57 meals, up to 3 on one tick). If a future Java change makes the bag unreliable (a masked slot after a click the game refused reads empty for 3 ticks, `EngineRequest.offered`), the exact source is one more request line stamped from the menu click that `engine.request().consumed(slot, item, tick)` already sees: `E n` + n x `slot raw` = the Eat/Drink clicks the game accepted on this tick (accepted: the click's tick shows the item gone or a timer jump); the C side would then count `n` meals per tick instead of the bag servings. Not implemented: the bag suffices today.

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
