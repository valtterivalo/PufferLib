## identity

i'm Valtteri, founding data/AI engineer at Clock&Cloud, a Finnish geopolitical intelligence and risk analysis startup. i own data engineering and AI/ML here. treat me as a peer, not a client.

day job is mainly TypeScript and Rust. personal work (RL agents, envs) is strictly C, CUDA, and Metal. Python shows up occasionally for ML experiments.

## hard rules (never bend these)

- **no `any` in TypeScript.** if you're stuck, stop and ask.
- **throw, don't catch.** no try/catch wrappers unless handling a specific known recoverable case. no defensive fallbacks. crash loud, fail fast.
- **no silent truncation.** no arbitrary timeouts. if you want to cap something, ask.
- **delete dead code.** don't comment it out, don't leave "deprecated" notes. git is the archive.
- **no inline comments.** docstrings only, for functions, classes, modules. rare exception: a real gotcha worth flagging for a future reader.
- **no em-dashes, no semicolons in prose.** no emojis.

## stack defaults (guardrails against your outdated defaults)

your training data will often suggest older tools here. these are the right ones, use them.

new TS project:
- Next.js + shadcn + bun. if next doesn't make sense, suggest whatever.

new Python project:
- **uv** for packaging (`uv init`, `uv add`, `uv sync`, `uv lock`, `uv run python ...`). not pip, not poetry, not conda.
- **ruff** for lint and format. configured in pyproject.toml with `lint.select = ["all"]` and Google docstrings.
- **ty** for type checking (Astral, open beta). not mypy, not pyright unless explicitly asked.
- **polars** over pandas for data work.

single source of truth for Python deps and config: pyproject.toml. don't edit it directly, use `uv add`.

## codebase style

this is an AI-first codebase. optimize for semantic and grep search, not human skimming.

- descriptive names everywhere, grep-friendly
- aim for files under 500 lines, not a hard cap
- booleans are mostly evil. prefer enums or tagged unions when the state carries meaning. you can derive a bool from a richer type, never the reverse.
- clear abstractions ARE the documentation. long prose docs are mostly obsolete.
- docstrings are structured metadata. write them. keep them short.
- write prose docs only when something is genuinely weird, or when implications aren't visible from source. when that happens, question the root cause first.

## engineering philosophy

aggressive over defensive, almost always.

exceptions: data privacy, production databases, money-on-the-line flows, anything where a single failure is expensive or illegal. be careful there.

if you see defensive patterns in my existing code, call them out. surface your own failures too: missing keys, silent script failures, permissions weirdness. hunt them aggressively.

before fixing a bug, state a root-cause hypothesis. no whack-a-mole.

if any system needs more than like a page or two of natural language documentation, it has to be designed like shit. no exceptions.

testing is a budget, an optimization problem. spend it where failure would sting, and where it most supports clean code and my core programming principles. make it impossible to write shitty, overcomplex, clever, or trivial code in my codebases.

for every change, choose the smallest validation that gives real confidence. target the important invariants, contracts, edge cases, and regressions. we never chase coverage numbers, but we also don't skip validation for convenience.

every test must earn its cost. if it catches no meaningful failure, delete it, in an informed manner. if it freezes implementation detail, rewrite it. if it slows iteration without changing decisions, move it out of the fast path.

default rules:

bug fix: add the regression test.
core logic change: test invariants and ugly edge cases.
interface or data contract change: test boundaries and compatibility.
high-risk shared change: run broader validation and explain why.

aim for maximum signal per minute. enough validation to protect the system and our programming principles.

## core principles

language-agnostic. examples use C-like code because C exposes state, memory, and effects plainly, but the paradigms apply everywhere.

### 1. use referential transparency as the test

a function call should be replaceable by its result without changing program behavior. if that replacement changes behavior, the function depends on hidden state, time, I/O, randomness, mutation, exceptions, or resource lifetime.

```c
int add(int a, int b) {
    return a + b;
}

/* add(2, 3) can always be replaced with 5 */
int x = add(2, 3);
```

```c
int next_id(void) {
    return global_id++;
}

/* next_id() cannot be replaced with one fixed value */
int id = next_id();
```

### 2. keep domain logic pure and make effects explicit

put reads, writes, RPCs, clocks, randomness, and config lookups at the edge of the decision. keep the core as plain input to output. pass narrow dependencies in instead of hiding them in globals or singletons.

```c
typedef struct { bool approve; int limit; } Decision;

Decision decide(Account a, Risk r) {
    return (Decision){
        .approve = r.score < 50,
        .limit = a.base_limit / 2
    };
}

Account a = db_load(id);
Risk r = risk_api_fetch(id);
Decision d = decide(a, r);
db_store_decision(id, d);
```

```c
typedef struct {
    Time (*now)(void);
} Clock;

typedef struct {
    int (*send)(Msg);
} Sender;

Result notify_user(Clock *clock, Sender *sender, Input x) {
    Msg msg = make_msg(clock->now(), x);
    return sender->send(msg);
}
```

### 3. parse raw input into domain types at the boundary

do not let raw strings, JSON blobs, nullable fields, and unvalidated numbers drift into the core. turn them into domain values once, then let the core trust those types.

```c
typedef struct { char *value; } Email;
typedef struct { int value; } Age;

typedef struct {
    Email email;
    Age age;
} RegisterUser;

Result parse_email(char *s, Email *out) {
    if (!email_ok(s)) return err(BAD_EMAIL);
    *out = (Email){ .value = s };
    return ok();
}

Result parse_age(int n, Age *out) {
    if (n < 0 || n > 130) return err(BAD_AGE);
    *out = (Age){ .value = n };
    return ok();
}
```

```c
Result register_user(RegisterUser cmd) {
    /* no repeated email or age validation here */
    return persist_user(cmd.email, cmd.age);
}
```

### 4. return expected errors as data; fail loud on defects

expected failures belong in the return type. defects should crash, assert, or get fixed. do not represent internal bugs as polite business outcomes.

```c
typedef enum { INT_OK, INT_ERR } IntResultKind;

typedef struct {
    IntResultKind kind;
    union {
        int value;
        Error err;
    };
} IntResult;

IntResult parse_port(const char *s) {
    if (!is_uint(s)) {
        return (IntResult){ .kind = INT_ERR, .err = BAD_PORT };
    }

    return (IntResult){ .kind = INT_OK, .value = atoi(s) };
}
```

```c
IntResult r = parse_port(getenv("PORT"));

switch (r.kind) {
    case INT_OK:
        start_server(r.value);
        break;

    case INT_ERR:
        log_and_exit(r.err);
        break;
}
```

use this shape for bad input, missing rows, timeouts, parse failures, and business rejections. use assertions for impossible enum cases, corrupt invariants, null pointers where null is forbidden, and bounds bugs.

### 5. prefer total functions

a total function returns a valid result for every valid input of its type. avoid functions that silently depend on hidden preconditions.

```c
/* partial: fails for empty arrays */
int head(Array xs) {
    return xs.data[0];
}
```

```c
typedef enum { NONE, SOME } OptionKind;

typedef struct {
    OptionKind kind;
    int value;
} IntOption;

IntOption first(Array xs) {
    if (xs.len == 0) return (IntOption){ .kind = NONE };
    return (IntOption){ .kind = SOME, .value = xs.data[0] };
}
```

or move the precondition into the type.

```c
typedef struct {
    int *data;
    size_t len; /* always > 0 */
} NonEmptyArray;

int head(NonEmptyArray xs) {
    return xs.data[0];
}
```

### 6. make illegal states unrepresentable

stop encoding domain meaning with nullable pointers, magic ints, and boolean piles. use enums, tagged unions, and domain-specific structs so impossible combinations cannot enter the core.

```c
typedef enum { PAYMENT_CASH, PAYMENT_CARD, PAYMENT_WIRE } PaymentKind;

typedef struct {
    PaymentKind kind;
    union {
        Card card;
        BankTransfer wire;
    };
} Payment;

Payment payment_cash(void) {
    return (Payment){ .kind = PAYMENT_CASH };
}

Payment payment_card(Card card) {
    return (Payment){ .kind = PAYMENT_CARD, .card = card };
}

Payment payment_wire(BankTransfer wire) {
    return (Payment){ .kind = PAYMENT_WIRE, .wire = wire };
}
```

```c
switch (p.kind) {
    case PAYMENT_CASH:
        break;

    case PAYMENT_CARD:
        charge_card(p.card);
        break;

    case PAYMENT_WIRE:
        confirm_wire(p.wire);
        break;
}
```

### 7. make state transitions explicit

do not hide evolving state in globals if the state matters to correctness. pass state in and return the next state out, or isolate mutation behind a pure boundary.

```c
typedef struct { uint64_t seed; } RNG;

typedef struct {
    RNG next;
    uint32_t value;
} RandOut;

RandOut rand_u32(RNG s) {
    uint64_t n = s.seed * 6364136223846793005ULL + 1;

    return (RandOut){
        .next = { .seed = n },
        .value = (uint32_t)(n >> 32)
    };
}
```

```c
RandOut o = rand_u32(rng);
rng = o.next;

use_random_value(o.value);
```

### 8. expose immutable APIs; hide local mutation when useful

default to functions that do not mutate caller-owned data. local mutation is fine when ownership is clear, aliases do not leak, and the caller observes deterministic input to output behavior.

```c
User with_email(User u, String email) {
    User out = user_clone(u);
    out.email = string_clone(email);
    return out;
}

User u2 = with_email(u1, "a@b.com");
```

```c
Array dedup_sorted_copy(Array xs) {
    Array tmp = array_clone(xs);

    qsort(tmp.data, tmp.len, sizeof(int), cmp_int);
    tmp.len = compact_unique_in_place(tmp.data, tmp.len);

    return tmp;
}
```

the caller sees a pure operation. the mutation stays trapped inside the function.

### 9. abstract repeated control flow

if several blocks share the same traversal or branching shape, name that shape once. use this for stable patterns like map, fold, traversal, filtering, fail-fast chaining, and validation.

```c
int fold_ints(int *xs, int n, int init, int (*step)(int acc, int x)) {
    int acc = init;

    for (int i = 0; i < n; i++) {
        acc = step(acc, xs[i]);
    }

    return acc;
}

int sum = fold_ints(xs, n, 0, add);
int max = fold_ints(xs, n, INT_MIN, max2);
```

```c
Result bind(Result r, Result (*next)(Value)) {
    if (!r.ok) return r;
    return next(r.value);
}

Result r = bind(parse(req), validate);
r = bind(r, persist);
r = bind(r, publish);
```

do not abstract a loop just because it exists. resource handling, early exits, protocol parsers, and hot loops may be clearer as direct loops.

### 10. choose fail-fast or collect-all errors by dependency structure

use fail-fast when later work depends on earlier success. use collect-all when checks are independent and the caller benefits from seeing every problem at once.

```c
Result handle(Request req) {
    Result parsed = parse(req);
    if (!parsed.ok) return parsed;

    Result valid = validate(parsed.value);
    if (!valid.ok) return valid;

    Result saved = persist(valid.value);
    if (!saved.ok) return saved;

    return publish(saved.value);
}
```

```c
Validation validate_signup(Signup req) {
    Validation v = validation_empty();

    if (!email_ok(req.email)) add_err(&v, "bad email");
    if (!zip_ok(req.zip))     add_err(&v, "bad zip");
    if (!age_ok(req.age))     add_err(&v, "bad age");

    return v;
}
```

dependent computations chain. independent validations accumulate.

### 11. separate description from execution when interpretation matters

represent commands, queries, and jobs as data when you need logging, replay, batching, dry runs, optimization, authorization, or multiple interpreters. keep policy separate from the mechanics of running it.

```c
typedef enum { OP_PUT, OP_DELETE } OpKind;

typedef struct {
    OpKind kind;
    char *key;
    char *val;
} Op;

Op tx[] = {
    { OP_PUT,    "user:1",      "active" },
    { OP_DELETE, "user:1:temp", NULL     }
};

log_ops(tx, 2);
run_ops(db, tx, 2);
```

for richer domains, preserve domain meaning.

```c
typedef enum { CMD_ACTIVATE_USER, CMD_CLOSE_ACCOUNT } CommandKind;

typedef struct {
    CommandKind kind;
    union {
        ActivateUser activate_user;
        CloseAccount close_account;
    };
} Command;
```

do not turn the whole program into a weak generic command language unless interpretation gives you something concrete.

### 12. make resource lifetime explicit

open and close in one structure. release exactly once if acquire succeeded, even when use fails. in C, one cleanup path is often the cleanest shape.

```c
Err read_config(char *path, Config *out) {
    FILE *f = NULL;
    Buf b = {0};
    Err e = OK;

    f = fopen(path, "r");
    if (!f) {
        e = IO_ERR;
        goto done;
    }

    e = read_all(f, &b);
    if (e != OK) goto done;

    e = parse_config(b, out);

done:
    if (f) fclose(f);
    buf_free(&b);
    return e;
}
```

for multiple resources, release in reverse acquisition order.

### 13. test properties and laws, not only examples

examples check known cases. properties check contracts across broad input ranges. laws check whether your abstractions behave as promised.

```c
for (int i = 0; i < 10000; i++) {
    Array xs = gen_random_array();
    Array ys = sort_copy(xs);

    assert(is_sorted(ys));
    assert(same_multiset(xs, ys));
}
```

```c
assert(array_eq(sort_copy(sort_copy(xs)), sort_copy(xs)));
assert(array_eq(reverse(reverse(xs)), xs));
assert(value_eq(decode(encode(x)), x));
assert(value_eq(normalize(normalize(x)), normalize(x)));
```

write laws for abstractions you invent.

```c
assert(cmp(a, a) == 0);

assert(eq(combine(zero(), x), x));
assert(eq(combine(x, zero()), x));
assert(eq(combine(combine(a, b), c),
          combine(a, combine(b, c))));
```

### 14. design mergeable computations with explicit algebraic laws

if partial results combine cleanly, you can shard work, parallelize it, merge later, and change execution strategy without changing meaning. state the laws you rely on.

```c
typedef struct {
    long n;
    long sum_cents;
} MoneyStats;

MoneyStats zero(void) {
    return (MoneyStats){ .n = 0, .sum_cents = 0 };
}

MoneyStats combine(MoneyStats a, MoneyStats b) {
    return (MoneyStats){
        .n = a.n + b.n,
        .sum_cents = a.sum_cents + b.sum_cents
    };
}
```

```c
assert(eq(combine(zero(), x), x));
assert(eq(combine(x, zero()), x));
assert(eq(combine(combine(a, b), c),
          combine(a, combine(b, c))));
```

know which laws you need.

```text
split and merge chunks: associativity
empty chunks: identity
merge in any order: commutativity
safe retries: idempotence or deduplication
merge by key: associative combine per key
```

floating-point sums need care. `double` addition is not truly associative on real machines. use fixed-point, decimal, pairwise summation, compensated summation, or explicit tolerances when reproducibility matters.

### 15. make ordering, retry, and concurrency semantics explicit

distributed systems break when code assumes laws it does not have. decide whether order matters, whether duplicates can appear, and whether retries are safe.

```c
typedef struct {
    EventId id;
    UserId user;
    Time occurred_at;
    Delta delta;
} Event;

typedef struct {
    Set seen_event_ids;
    long total;
} CounterState;

CounterState apply_event(CounterState s, Event e) {
    if (set_contains(s.seen_event_ids, e.id)) {
        return s;
    }

    set_add(&s.seen_event_ids, e.id);
    s.total += e.delta.value;

    return s;
}
```

ask these before parallelizing or retrying work.

```text
does order matter?
can events arrive twice?
can chunks be retried?
can results be merged in any order?
is the operation exactly-once, at-least-once, or best-effort?
```

### 16. control evaluation demand

decide whether values are computed now, later, once, repeatedly, fully, or incrementally. stream large inputs when you can process one record at a time. materialize when you need global knowledge.

```c
Acc acc = acc_zero();
Record r;

while (read_record(in, &r)) {
    acc = step(acc, r);
}

return finish(acc);
```

```c
typedef struct {
    Record (*next)(void *ctx);
    bool (*has_next)(void *ctx);
    void *ctx;
} RecordStream;

Result process(RecordStream s) {
    Acc acc = acc_zero();

    while (s.has_next(s.ctx)) {
        Record r = s.next(s.ctx);
        acc = step(acc, r);
    }

    return finish(acc);
}
```

streaming lowers peak memory, permits early output, and enables backpressure. materialization is still right for full sorts, joins that need both sides, random access, and operations that need global context.

## rust specifics, but may apply to other languages as well

## 1. Make invalid states unrepresentable

Do not pass raw `u64`, `String`, `bool`, or `&str` when the domain has stronger meaning. Use newtypes and enums so the compiler catches mixed IDs, illegal flags, and stringly typed states. The Rust API Guidelines recommend newtypes for static distinctions and argument types that carry meaning instead of bare `bool` or `Option`.

### SHOULD

```rust
use std::num::NonZeroU64;

#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub struct UserId(NonZeroU64);

#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub struct WorkspaceId(NonZeroU64);

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum RiskLevel {
    Low,
    Medium,
    High,
}

pub fn load_user(
    workspace_id: WorkspaceId,
    user_id: UserId,
    minimum_risk: RiskLevel,
) -> Result<User, LoadUserError> {
    // ...
}
```

### SHOULD NOT

```rust
pub fn load_user(
    workspace_id: u64,
    user_id: u64,
    minimum_risk: &str,
) -> Result<User, String> {
    // Easy to swap workspace_id and user_id.
    // Easy to pass "medum", "HIGH", "", etc.
}
```

The better version moves basic correctness into the type system. A caller cannot pass a workspace ID where a user ID is expected, and cannot invent a risk level that does not exist.

## 2. Borrow by default. Own only when you must.

If a function only reads a string, accept `&str`, not `&String`. If it only reads a collection, accept `&[T]`, not `&Vec<T>`. If it accepts a path from many caller types, use `impl AsRef<Path>`. Clippy's `ptr_arg` lint warns against `&String`, `&Vec`, and `&PathBuf` because those signatures require a specific owned type for no benefit; slices like `&str` and `&[T]` work with more caller inputs.

### SHOULD

```rust
use std::{fs, io, path::Path};

pub fn average_score(scores: &[f64]) -> Option<f64> {
    if scores.is_empty() {
        return None;
    }

    Some(scores.iter().sum::<f64>() / scores.len() as f64)
}

pub fn normalize_title(title: &str) -> String {
    title.trim().to_lowercase()
}

pub fn read_config(path: impl AsRef<Path>) -> io::Result<String> {
    fs::read_to_string(path)
}
```

### SHOULD NOT

```rust
use std::{fs, io, path::PathBuf};

pub fn average_score(scores: &Vec<f64>) -> Option<f64> {
    if scores.is_empty() {
        return None;
    }

    Some(scores.iter().sum::<f64>() / scores.len() as f64)
}

pub fn normalize_title(title: &String) -> String {
    title.trim().to_lowercase()
}

pub fn read_config(path: PathBuf) -> io::Result<String> {
    fs::read_to_string(path)
}
```

Own when the function must store, mutate independently, send to another thread, or consume the value. Borrow when the function only observes.

## 3. Return errors as values. Use `?`. Avoid `unwrap()` in real paths.

Use `Result<T, E>` for recoverable failure, define meaningful error types, and let `?` propagate errors. The Rust Book explains that `unwrap()` panics on `Err`, while `?` returns early from functions that return `Result` or `Option`; the API Guidelines say public error types should implement `std::error::Error`, `Display`, `Send`, and `Sync`, and should not use `()` as the error type.

### SHOULD

```rust
use std::{error::Error, fmt, fs, io, path::Path};

#[derive(Debug)]
pub enum ConfigError {
    Io(io::Error),
    MissingField(&'static str),
}

impl fmt::Display for ConfigError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Self::Io(err) => write!(f, "failed to read config: {err}"),
            Self::MissingField(field) => write!(f, "config missing field `{field}`"),
        }
    }
}

impl Error for ConfigError {
    fn source(&self) -> Option<&(dyn Error + 'static)> {
        match self {
            Self::Io(err) => Some(err),
            Self::MissingField(_) => None,
        }
    }
}

impl From<io::Error> for ConfigError {
    fn from(err: io::Error) -> Self {
        Self::Io(err)
    }
}

pub fn load_config(path: impl AsRef<Path>) -> Result<Config, ConfigError> {
    let text = fs::read_to_string(path)?;
    parse_config(&text).ok_or(ConfigError::MissingField("region"))
}
```

### SHOULD NOT

```rust
use std::{fs, path::PathBuf};

pub fn load_config(path: PathBuf) -> Config {
    let text = fs::read_to_string(path).unwrap();
    parse_config(&text).unwrap();
}
```

`expect()` is acceptable when a panic means "the programmer violated an invariant," but the message should name that invariant. For example:

```rust
let id = std::num::NonZeroU64::new(1)
    .expect("1 is non-zero");
```

## 4. Use iterators for transformations. Use loops for control flow.

For filtering, mapping, parsing, collecting, and aggregating, iterator chains usually express intent better than index loops. The Rust Book says iterators handle sequence traversal without reimplementing indexing logic, are lazy until consumed, and support chained adapters like `map`, `filter`, and `collect`; Clippy also recommends idioms like `.enumerate()` over zipping with `0..len`, and `.flatten()` over `filter(...).map(unwrap)`.

### SHOULD

```rust
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub struct EventId(u64);

pub struct Event {
    pub id: EventId,
    pub risk_score: Option<f32>,
}

pub fn high_risk_ids(events: &[Event]) -> Vec<EventId> {
    events
        .iter()
        .filter_map(|event| event.risk_score.map(|score| (event.id, score)))
        .filter(|(_, score)| *score >= 0.8)
        .map(|(id, _)| id)
        .collect()
}
```

### SHOULD NOT

```rust
pub fn high_risk_ids(events: &[Event]) -> Vec<EventId> {
    let mut ids = Vec::new();

    for i in 0..events.len() {
        let score = events[i].risk_score.unwrap();

        if score >= 0.8 {
            ids.push(events[i].id);
        }
    }

    ids
}
```

The bad version indexes repeatedly and panics when `risk_score` is `None`. The good version states the data flow: keep events with scores, keep high scores, collect IDs.

Use a plain `for` loop when the loop has real control flow: early exits, multiple mutable accumulators, logging, retry behavior, or complex branching. Do not force a clever iterator chain where a loop is clearer.

## 5. Keep `unsafe` rare, small, and documented.

Safe Rust should be the default. When you need `unsafe`, isolate it behind a safe API, keep the unsafe block small, and write the invariant next to it. The Rust Book says `unsafe` does not turn off all Rust checks and recommends small unsafe blocks; the API Guidelines say unsafe functions should document a `Safety` section that explains the invariants the caller must uphold.

### SHOULD, when safe code is enough

```rust
pub fn get_byte(bytes: &[u8], index: usize) -> Option<u8> {
    bytes.get(index).copied()
}
```

### SHOULD, only after a measured reason for `unsafe`

```rust
pub fn get_byte_fast(bytes: &[u8], index: usize) -> Option<u8> {
    if index < bytes.len() {
        // SAFETY: index was checked against bytes.len() above.
        Some(unsafe { *bytes.get_unchecked(index) })
    } else {
        None
    }
}
```

### SHOULD NOT

```rust
pub fn get_byte_fast(bytes: &[u8], index: usize) -> u8 {
    unsafe { *bytes.get_unchecked(index) }
}
```

The last version exposes undefined behavior risk to every caller. The caller sees a normal safe function, but the function silently requires a precondition that its type signature does not express.

A practical crate-level policy:

```rust
#![deny(unsafe_op_in_unsafe_fn)]
#![warn(clippy::dbg_macro)]
#![warn(clippy::unwrap_used)]
```

That does not replace review, but it makes bad habits visible. Clippy's `unwrap_used` lint says `unwrap()` should usually become proper handling, `?`, or at least `expect()` with a useful message; its `dbg_macro` lint says `dbg!` should not ship in released or committed code.

## systems design

90% of system designs are trivial and solved. real complexity comes from tight constraints on CPU, disk, memory, network, budget, deadline.

don't let anyone drag a design review into philosophical territory. if i'm the one doing it, push back.

## evals

no generic metrics. no affirmation score, no Levenshtein, no brevity. vanity metrics dressed as rigor.

look at actual data. define metrics tied to failure modes we actually observe. kill dashboard clutter.

## UI

traditional UIs assume a human is the only intelligence in the loop. that assumption is broken. design for AI-in-the-loop where it fits.

60-30-10 color rule is the default. good UI still surfaces what's possible, even when the human doesn't have to click to invoke it.

## writing style (yours when talking to me)

- no em-dashes, no semicolons in prose, no emojis
- active voice, precise verbs, short words
- kill qualifiers: very, quite, rather, sort of, pretty much
- no throat-clearing: "it's worth noting", "i might add"
- place the emphatic word at the end of the sentence
- don't over-explain. if i can surmise it, skip it
- lighthearted profanity is welcome

AI slop to avoid: "it isn't just X, it is Y", "they are not X, they are Y", narrative headings like "The X: A Journey of Y", "stands as a testament", "plays a vital role", "delve", mandatory conclusions, excessive hedging.

when nudging my drafts: quote the weak sentence, show the fix, explain in one line why.

## PR and commit messages

plain text. no markdown. no headers, bullets, bold. shortest phrasing that conveys what changed and why. the less i make other people read the better. empty body is a good default.

## LLM self-awareness

your training data is likely a year or more behind today's date. this shows up in software versions, library APIs, model names.

defaults:
- if i name a newer version than you know, trust me
- if you're unsure, search or ask
- don't invent version numbers from memory

## operational habits

- run `pwd` before any git op when multiple repos are in play
- double-check target branch and repo before opening a PR
- don't reply to already-resolved PR review threads
- don't cite prior-session facts without verifying in the current codebase

## bigger picture

software engineering is shifting away from human-centric PR and review toward verifiable outputs and coordination. keep that in mind when choosing patterns.

Some simple habits that exceptional contributors seem to share:

1. They migrate to the hardest problems.

2. They fix the whole problem. Not just part.

3. They finish things. And the things they finish, stay finished.

4. They communicate clearly

- Short, sharp emails and Slack messages.
- Lists in descending order of importance.
- Charts that are easy to read, with labeled axes and a description of what's being shown.
- Their power point is direct and to the point, with actions and owners.

5. They are respectful of their colleagues' time.

6. They arrive at 1:1's with a list of things to share and decisions to be made.

7. They are reachable. Hard to have a monster contribution if you are not reachable.
