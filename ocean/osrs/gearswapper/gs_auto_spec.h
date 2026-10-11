#ifndef GS_AUTO_SPEC_H
#define GS_AUTO_SPEC_H
#include "gs_core.h"

/* AutoSpec.java and Appetite.java: the knockout chance of a plan against an opponent that eats to a hitpoints level he wants. */
enum { GS_MEAL_GAP = 3, GS_REACTION_TICKS = 7, GS_REACTIONS = GS_REACTION_TICKS + 3, GS_MEAL_FOODS = 3 };
static const int GS_MEAL_HEALS[GS_MEAL_FOODS] = {22, 16, 18};
static const GsConsumable GS_MEAL_KIND[GS_MEAL_FOODS] = {GS_HARD_FOOD, GS_POTION, GS_FAST_FOOD};
#ifdef GS_HP_BELIEF
enum { GS_SAFE_LOW = 50, GS_SAFE_HIGH = 85 };
static const int GS_MEAL_CAPS[GS_MEAL_FOODS] = {121, 99, 115};
#else
enum { GS_SAFE_LOW = 44, GS_SAFE_HIGH = 87 };
static const int GS_MEAL_CAPS[GS_MEAL_FOODS] = {99, 115, 99};
static const double GS_FIRST_MEAL[GS_REACTION_TICKS + 2] = {0.481, 0.250, 0.130, 0.067, 0.035, 0.018, 0.009, 0.005, 0.005};
static const double GS_ANTICIPATES = 0.066, GS_HIDES = 0.687;
#endif

static double gs_wants(int hitpoints) {
    double w = (GS_SAFE_HIGH - hitpoints) / (double)(GS_SAFE_HIGH - GS_SAFE_LOW);
    return fmax(0, fmin(1, w));
}

#ifndef GS_HP_BELIEF
/* Hitpoints.eat: every living value eats the foods from first to last while it wants to be above where they leave it. */
static void gs_hp_eat(Arena* arena, double* alive, int length, int first_food, int food_count) {
    size_t mark = arena->used;
    double* after = arena_alloc(arena, (size_t)length * sizeof(double));
    memset(after, 0, (size_t)length * sizeof(double));
    for (int hp = 1; hp < length; hp++) {
        double mass = alive[hp];
        int level = hp;
        double wanted = gs_wants(level);
        after[level] += mass * (1 - wanted);
        for (int f = 0; f < food_count; f++) {
            int food = first_food + f;
            level = imax(level, imin(GS_MEAL_CAPS[food], level + GS_MEAL_HEALS[food]));
            double wanted_after = f + 1 < food_count ? gs_wants(level) : 0;
            after[level] += mass * (wanted - wanted_after);
            wanted = wanted_after;
        }
    }
    memcpy(alive, after, (size_t)length * sizeof(double));
    arena->used = mark;
}

/* Appetite.hiding */
static void gs_hp_hiding(Arena* arena, double* alive, int length) {
    size_t mark = arena->used;
    double* ate = arena_alloc(arena, (size_t)length * sizeof(double));
    memcpy(ate, alive, (size_t)length * sizeof(double));
    gs_hp_eat(arena, ate, length, 0, GS_MEAL_FOODS);
    for (int hp = 0; hp < length; hp++) alive[hp] += GS_HIDES * (ate[hp] - alive[hp]);
    arena->used = mark;
}

typedef struct { double share; int has_meal; int after; } GsReaction;

/* Appetite.reactions: the share of each tick his first meal comes, no meal, and the meal on the tick before our first hit. */
static int gs_reactions(int before_our_hit, GsReaction* out) {
    int n = 0;
    for (int after = 0; after <= GS_REACTION_TICKS; after++) out[n++] = (GsReaction){GS_FIRST_MEAL[after] * (1 - GS_ANTICIPATES), 1, after};
    out[n++] = (GsReaction){GS_FIRST_MEAL[GS_REACTION_TICKS + 1] * (1 - GS_ANTICIPATES), 0, 0};
    out[n++] = (GsReaction){GS_ANTICIPATES, 1, imax(0, before_our_hit)};
    return n;
}
#endif

typedef enum { GS_ORIGIN_OURS, GS_ORIGIN_REFLECTED } GsOrigin;
typedef struct { int lands; const double* damage; int damages; GsOrigin origin; int first_positive_roll; double positive_each; } GsSpecHit;

static GsSpecHit gs_spec_hit_of(Arena* a, int lands, int least, int most, double accuracy) {
    double* damage = arena_alloc(a, (size_t)(most + 1) * sizeof(double));
    memset(damage, 0, (size_t)(most + 1) * sizeof(double));
    damage[0] = 1 - accuracy;
    for (int roll = least; roll <= most; roll++) damage[roll] += accuracy / (most - least + 1);
    return (GsSpecHit){lands, damage, most + 1, GS_ORIGIN_OURS, imax(1, least), most > 0 ? accuracy / (most - least + 1) : 0};
}

typedef struct { int lands, least, most; double accuracy; int attack; } GsIncoming;

typedef enum { GS_ECHO_VENGEANCE, GS_ECHO_RECOIL } GsEcho;

static int gs_vengeance_return(int damage) {
#ifdef GS_HP_BELIEF
    return damage * 3 / 4;
#else
    return damage > 0 ? imax(1, damage * 3 / 4) : 0;
#endif
}

static GsSpecHit gs_echo(Arena* a, const GsIncoming* incoming, GsEcho echo, int recoil_charges, GsPid pid) {
    int most = (echo == GS_ECHO_VENGEANCE ? gs_vengeance_return(incoming->most) : 0) + imin(recoil_charges, incoming->most / 10 + 1);
    double* damage = arena_alloc(a, (size_t)(most + 1) * sizeof(double));
    memset(damage, 0, (size_t)(most + 1) * sizeof(double));
    damage[0] = 1 - incoming->accuracy;
    for (int dealt = incoming->least; dealt <= incoming->most; dealt++) {
        int reflected = (echo == GS_ECHO_VENGEANCE ? gs_vengeance_return(dealt) : 0) + (dealt > 0 ? imin(recoil_charges, dealt / 10 + 1) : 0);
        damage[reflected] += incoming->accuracy / (incoming->most - incoming->least + 1);
    }
    return (GsSpecHit){incoming->lands + (pid == GS_PID_LOCAL ? 0 : 1), damage, most + 1, GS_ORIGIN_REFLECTED, 1, 0};
}

typedef enum { GS_REBOUND_UP, GS_REBOUND_CASTABLE, GS_REBOUND_SPENT } GsReboundKind;
typedef struct { GsReboundKind kind; int cast; } GsRebound;

typedef struct { int tick, amount, cap; } GsSpecHeal;
enum { GS_HEALTH_HEALS = 4 };
typedef struct { int hitpoints; GsSpecHeal heals[GS_HEALTH_HEALS]; int heal_count; } GsHealth;

static GsHealth gs_health_of(int hitpoints) { return (GsHealth){hitpoints, {{0}}, 0}; }

static GsHealth gs_health_healing(GsHealth health, int tick, int amount, int cap) {
    assert(health.heal_count < GS_HEALTH_HEALS);
    int at = health.heal_count;
    while (at > 0 && health.heals[at - 1].tick > tick) { health.heals[at] = health.heals[at - 1]; at--; }
    health.heals[at] = (GsSpecHeal){tick, amount, cap};
    health.heal_count++;
    return health;
}

typedef struct { int hard, potion, fast; } GsTargetReady;
typedef struct { GsTargetReady ready; double* hp; int levels; } GsTargetBranch;
typedef struct {
    int has_history, has_belief, base_hp;
    GsTargetBranch* history; int history_count;
    GsTargetBranch* belief; int belief_count;
} GsTarget;

typedef struct {
    GsHealthRange hitpoints;
    const GsSpecHit* underway; int underway_count;
    int hard_food_ready;
    GsVengeance vengeance;
    GsPid pid;
    double pid_flip;
    int has_incoming; GsIncoming incoming;
    int recoil_charges, tick;
    GsWarning warning;
    GsRebound rebound;
    GsHealth local;
    GsTarget target;
} GsSpecSituation;

static double jdk_sum(const double* values, int count) {
    double sum = 0, compensation = 0;
    for (int i = 0; i < count; i++) {
        double tmp = values[i] - compensation;
        double velvel = sum + tmp;
        compensation = (velvel - sum) - tmp;
        sum = velvel;
    }
    return sum - compensation;
}

typedef struct { double kill, death; } GsOutcome;

#ifdef GS_HP_BELIEF
/* AutoSpec.Health.survives */
static double gs_health_survives(Arena* a, const GsHealth* health, const GsIncoming* hit, GsPid pid) {
    int length = imax(health->hitpoints, GS_OVERHEALED_HP) + 1;
    size_t mark = a->used;
    double* alive = hp_uniform(a, health->hitpoints, health->hitpoints, length);
    for (int i = 0; i < health->heal_count; i++)
        if (health->heals[i].tick < hit->lands || (health->heals[i].tick == hit->lands && pid == GS_PID_LOCAL))
            hp_heal(alive, health->heals[i].amount, health->heals[i].cap);
    double survives = 1 - hp_strike_range(a, alive, length, hit->least, hit->most, hit->accuracy);
    a->used = mark;
    return survives;
}

static GsIncoming gs_surviving_incoming(Arena* a, const GsHealth* health, const GsIncoming* hit, GsPid pid) {
    int hp = health->hitpoints;
    for (int i = 0; i < health->heal_count; i++)
        if (health->heals[i].tick < hit->lands || (health->heals[i].tick == hit->lands && pid == GS_PID_LOCAL))
            hp = imax(hp, imin(health->heals[i].cap, hp + health->heals[i].amount));
    int high = imin(hit->most, hp - 1);
    if (high < hit->least) return (GsIncoming){hit->lands, 0, 0, 1, hit->attack};
    double accuracy = hit->accuracy * (high - hit->least + 1) / (hit->most - hit->least + 1)
            / gs_health_survives(a, health, hit, pid);
    return (GsIncoming){hit->lands, hit->least, high, accuracy, hit->attack};
}

static double gs_spec_alive(Arena* a, const GsSpecSituation* s, const GsIncoming* his, const GsSpecHit* hit) {
    if (!his) return 1;
    int first = hit->origin == GS_ORIGIN_OURS && (hit->lands < his->lands || (hit->lands == his->lands && s->pid == GS_PID_LOCAL));
    return first ? 1 : gs_health_survives(a, &s->local, his, s->pid);
}

static int gs_lethal_echo(const GsSpecSituation* s, const GsSpecHit* hit, int damage) {
    return s->vengeance == GS_VENGEANCE_UP && hit->origin == GS_ORIGIN_OURS && damage * 3 / 4 >= s->local.hitpoints;
}

#endif

#ifdef GS_HP_BELIEF
/* Belief.java and Hitpoints.meal: what we hold of his hitpoints since his health bar last read, and the meals he may still eat. */
static const int GS_BELIEF_BAND_FROM[5] = {0, 31, 45, 61, 88};
static const double GS_BELIEF_LATE[5] = {0.26, 0.28, 0.18, 0.10, 0.064}, GS_BELIEF_EAGER[5] = {0.60, 0.53, 0.43, 0.17, 0.098};
static const double GS_EAGER_SHARE = 0.34, GS_INERT = 0.32, GS_BELIEF_HIDES = 0.687, GS_PANIC_RATE = 0.8;
static const double GS_HAZARD = 1.5, GS_HAZARD_HIGH_FACTOR = 0.6;
enum { GS_PANIC_HITPOINTS = 25, GS_HAZARD_HIGH_FROM = 61, GS_HAZARD_NONE_FROM = 88, GS_MEALS = 2, GS_ALL_FOODS = 7, GS_WITHOUT_HARD = 6 };
static const double GS_SERVING_SHARE[4] = {0.15, 0.34, 0.34, 0.17};
static const int GS_SERVING_FOODS[4] = {1, 3, 5, 7};

static double gs_belief_rate(int hp) {
    int band = 4;
    while (hp < GS_BELIEF_BAND_FROM[band]) band--;
    double rate = (1 - GS_EAGER_SHARE) * GS_BELIEF_LATE[band] + GS_EAGER_SHARE * GS_BELIEF_EAGER[band];
    return hp <= GS_PANIC_HITPOINTS && rate < GS_PANIC_RATE ? GS_PANIC_RATE : rate;
}
static double gs_belief_hazard(int hp) { return hp >= GS_HAZARD_NONE_FROM ? 0 : fmin(1, GS_HAZARD * (hp >= GS_HAZARD_HIGH_FROM ? GS_HAZARD_HIGH_FACTOR : 1) * gs_belief_rate(hp)); }
static double gs_gate_ate(int hp) { (void)hp; return 1 - GS_INERT; }
static double gs_gate_hidden(int hp) { return (1 - GS_INERT) * (hp < GS_BELIEF_BAND_FROM[4] ? GS_BELIEF_HIDES * gs_wants(hp) : gs_belief_rate(hp)); }

/* Hitpoints.serve */
static void gs_hp_serve(Arena* arena, double* alive, int length, int available) {
    size_t mark = arena->used;
    double* after = arena_alloc(arena, (size_t)length * sizeof(double));
    memset(after, 0, (size_t)length * sizeof(double));
    for (int hp = 1; hp < length; hp++) {
        for (int k = 0; k < 4; k++) {
            int level = hp;
            for (int f = 0; f < GS_MEAL_FOODS; f++)
                if ((GS_SERVING_FOODS[k] & available) >> f & 1) level = imax(level, imin(GS_MEAL_CAPS[f], level + GS_MEAL_HEALS[f]));
            after[level] += alive[hp] * GS_SERVING_SHARE[k];
        }
    }
    memcpy(alive, after, (size_t)length * sizeof(double));
    arena->used = mark;
}

/* Hitpoints.meal */
static void gs_hp_meal(Arena* arena, double* alive, int length, int available, double (*gate)(int)) {
    size_t mark = arena->used;
    double* eating = arena_alloc(arena, (size_t)length * sizeof(double));
    memset(eating, 0, (size_t)length * sizeof(double));
    for (int hp = 1; hp < length; hp++) {
        eating[hp] = alive[hp] * gate(hp);
        alive[hp] -= eating[hp];
    }
    gs_hp_serve(arena, eating, length, available);
    for (int hp = 1; hp < length; hp++) alive[hp] += eating[hp];
    arena->used = mark;
}

static void gs_belief_weigh(double* alive, int length, int ate) {
    double total = 0;
    for (int hp = 1; hp < length; hp++) {
        alive[hp] *= ate ? gs_belief_rate(hp) : 1 - gs_belief_rate(hp);
        total += alive[hp];
    }
    for (int hp = 1; hp < length; hp++) alive[hp] /= total;
}

/* Belief.after */
static double* gs_belief_after(Arena* arena, const GsHealthRange* band, const GsWarning* warning, int length) {
    double* alive = hp_uniform(arena, imax(1, band->low), band->high, length);
    for (int i = 0; i < warning->watched_count; i++) {
        switch (warning->watched[i]) {
            case GS_SEEN_QUIET: gs_belief_weigh(alive, length, 0); break;
            case GS_SEEN_ATE: gs_belief_weigh(alive, length, 1); gs_hp_meal(arena, alive, length, GS_ALL_FOODS, gs_gate_ate); break;
            case GS_SEEN_HIDDEN: gs_hp_meal(arena, alive, length, GS_ALL_FOODS, gs_gate_hidden); break;
        }
    }
    return alive;
}
#else
typedef struct { int tick, first_food, food_count; } GsEating;
#endif

static void spec_hits_sort(GsSpecHit* hits, int count) {
    for (int i = 1; i < count; i++) {
        GsSpecHit hit = hits[i];
        int j = i - 1;
        while (j >= 0 && hits[j].lands > hit.lands) { hits[j + 1] = hits[j]; j--; }
        hits[j + 1] = hit;
    }
}

#ifdef GS_HP_BELIEF
/* AutoSpec.mealDelay: the foods he eats at the middle of his hitpoints, those he wants at least half the time, one after the other. */
static int gs_meal_delay(int level) {
    int has[GS_CONSUMABLES] = {0};
    for (int food = 0; food < GS_MEAL_FOODS; food++) {
        if (gs_wants(level) < 0.5) break;
        has[GS_MEAL_KIND[food]] = 1;
        level = imin(GS_MEAL_CAPS[food], level + GS_MEAL_HEALS[food]);
    }
    return consumption_delay(consumption_of(has[GS_POTION], has[GS_FAST_FOOD], has[GS_HARD_FOOD]));
}

#endif

static double g_gs_expected_hp, g_gs_pit;
static int g_gs_truth_hp;

#ifdef GS_HP_BELIEF
/* AutoSpec.strikes: the hits one after the other on the mass still alive. */
static GsOutcome gs_strikes(Arena* a, const GsSpecSituation* s, double* alive, int length, const GsSpecHit* hits, int hit_count, const GsIncoming* his) {
    double dead = 0, suicide = 0;
    for (int i = 0; i < hit_count; i++) {
        double* spared = arena_alloc(a, (size_t)hits[i].damages * sizeof(double));
        memcpy(spared, hits[i].damage, (size_t)hits[i].damages * sizeof(double));
        double lethal = 0;
        for (int roll = 0; roll < hits[i].damages; roll++)
            if (gs_lethal_echo(s, &hits[i], roll)) {
                lethal += spared[roll];
                spared[roll] = 0;
            }
        double chance = gs_spec_alive(a, s, his, &hits[i]);
        suicide += chance * jdk_sum(alive, length) * lethal;
        dead += chance * hp_strike_damage(a, alive, length, spared, hits[i].damages);
    }
    return (GsOutcome){dead, suicide};
}

/* AutoSpec.Walk.run: his meal on each tick from `from` to the last landing hit at the chance of his hitpoints then, with hits applied between. */
static GsOutcome gs_walk(Arena* arena, const GsSpecSituation* s, double* alive, int length, const GsSpecHit* hits, int hit_count, int from, int last, int meals_left, const GsIncoming* landing, GsEcho echo, int level, int first) {
    double dead = 0, suicide = 0;
    int applied = 0;
    for (int tick = from; tick <= last; tick++) {
        int upto = applied;
        while (upto < hit_count && (hits[upto].lands < tick || (hits[upto].lands == tick && s->pid == GS_PID_LOCAL))) upto++;
        if (upto > applied) {
            GsOutcome part = gs_strikes(arena, s, alive, length, hits + applied, upto - applied, landing);
            dead += part.kill;
            suicide += part.death;
            applied = upto;
        }
        if (meals_left == 0) continue;
        size_t inner = arena->used;
        double* eat = arena_alloc(arena, (size_t)length * sizeof(double));
        for (int hp = 0; hp < length; hp++) {
            eat[hp] = alive[hp] * (hp ? gs_belief_hazard(hp) : 0);
            alive[hp] -= eat[hp];
        }
        gs_hp_serve(arena, eat, length, s->hard_food_ready <= tick ? GS_ALL_FOODS : GS_WITHOUT_HARD);
        GsSpecHit* after = arena_alloc(arena, (size_t)(hit_count + 1) * sizeof(GsSpecHit));
        int count = 0;
        const GsIncoming* delayed_landing = landing;
        GsIncoming delayed;
        int delay = first && s->has_incoming && s->incoming.attack != GS_NONE && tick < s->incoming.attack ? gs_meal_delay(level) : 0;
        if (delay) {
            delayed = s->incoming;
            delayed.lands += delay;
            delayed.attack += delay;
            delayed_landing = &delayed;
        }
        for (int i = applied; i < hit_count; i++) {
            if (delay && hits[i].origin != GS_ORIGIN_OURS) continue;
            after[count++] = hits[i];
        }
        if (delay && delayed.lands <= last) {
            GsIncoming surviving = gs_surviving_incoming(arena, &s->local, &delayed, s->pid);
            after[count++] = gs_echo(arena, &surviving, echo, s->recoil_charges, s->pid);
        }
        spec_hits_sort(after, count);
        GsOutcome branch = gs_walk(arena, s, eat, length, after, count, tick + GS_MEAL_GAP, last, meals_left - 1, delayed_landing, echo, level, 0);
        dead += branch.kill;
        suicide += branch.death;
        arena->used = inner;
    }
    if (applied < hit_count) {
        GsOutcome part = gs_strikes(arena, s, alive, length, hits + applied, hit_count - applied, landing);
        dead += part.kill;
        suicide += part.death;
    }
    return (GsOutcome){dead, suicide};
}

/* AutoSpec.outcome */
static GsOutcome gs_spec_outcome(Arena* arena, const GsSpecSituation* s, const GsSpecHit* planned, int planned_count, GsEcho echo) {
    size_t mark = arena->used;
    int struck_count = s->underway_count + planned_count;
    GsSpecHit* hits = arena_alloc(arena, (size_t)(struck_count + 1) * sizeof(GsSpecHit));
    memcpy(hits, s->underway, (size_t)s->underway_count * sizeof(GsSpecHit));
    memcpy(hits + s->underway_count, planned, (size_t)planned_count * sizeof(GsSpecHit));
    int first = INT_MAX, last = INT_MIN;
    for (int i = 0; i < struck_count; i++) {
        first = imin(first, hits[i].lands);
        last = imax(last, hits[i].lands);
    }
    GsIncoming incoming = s->incoming;
    if (s->has_incoming) {
        GsIncoming surviving = gs_surviving_incoming(arena, &s->local, &incoming, s->pid);
        hits[struck_count] = gs_echo(arena, &surviving, echo, s->recoil_charges, s->pid);
        first = imin(first, hits[struck_count].lands);
    }
    if (first == INT_MAX) { arena->used = mark; return (GsOutcome){0, 0}; }
    int length = imax(s->hitpoints.max, GS_OVERHEALED_HP) + 1;
    double* start = gs_belief_after(arena, &s->hitpoints, &s->warning, length);
    double mean_products[1024];
    assert(length <= 1024);
    for (int hp = 0; hp < length; hp++) mean_products[hp] = hp * start[hp];
    double expected = jdk_sum(mean_products, length);
    int truth = imin(imax(g_gs_truth_hp, 0), length - 1);
    double below = 0;
    for (int hp = 0; hp < truth; hp++) below += start[hp];
    g_gs_pit = below + 0.5 * start[truth];
    g_gs_expected_hp = expected;
    int level = (int)floor(expected + 0.5);
    int count = struck_count + (s->has_incoming ? 1 : 0);
    spec_hits_sort(hits, count);
    GsOutcome result = gs_walk(arena, s, start, length, hits, count, s->tick + 1, last, GS_MEALS, s->has_incoming ? &incoming : NULL, echo, level, 1);
    arena->used = mark;
    return result;
}
#else
typedef enum { GS_FLIGHT_WAITING, GS_FLIGHT_COMMITTED, GS_FLIGHT_RESOLVED, GS_FLIGHT_CANCELLED } GsFlight;
typedef struct { const GsTargetReady* values; int count; } GsTargetClocks;

static int gs_ready_compare(GsTargetReady a, GsTargetReady b) {
    if (a.hard != b.hard) return a.hard < b.hard ? -1 : 1;
    if (a.potion != b.potion) return a.potion < b.potion ? -1 : 1;
    return a.fast == b.fast ? 0 : a.fast < b.fast ? -1 : 1;
}

static void gs_clocks_insert(GsTargetReady* values, int* count, GsTargetReady ready) {
    int at = 0;
    while (at < *count && gs_ready_compare(values[at], ready) < 0) at++;
    if (at < *count && gs_ready_compare(values[at], ready) == 0) return;
    memmove(values + at + 1, values + at, (size_t)(*count - at) * sizeof(*values));
    values[at] = ready;
    (*count)++;
}

static GsTargetClocks gs_clocks_at(Arena* arena, GsTargetClocks clocks, int tick) {
    int changes = 0;
    for (int i = 0; i < clocks.count; i++) {
        GsTargetReady r = clocks.values[i];
        changes |= r.hard < tick || r.potion < tick || r.fast < tick;
    }
    if (!changes) return clocks;
    GsTargetReady* values = arena_alloc(arena, (size_t)clocks.count * sizeof(*values));
    int count = 0;
    for (int i = 0; i < clocks.count; i++) {
        GsTargetReady r = clocks.values[i];
        gs_clocks_insert(values, &count, (GsTargetReady){imax(r.hard, tick), imax(r.potion, tick), imax(r.fast, tick)});
    }
    return (GsTargetClocks){values, count};
}

static GsTargetClocks gs_clocks_consume(Arena* arena, GsTargetClocks clocks, int food, int tick) {
    int available = 0;
    for (int i = 0; i < clocks.count; i++) {
        GsTargetReady r = clocks.values[i];
        available += (food == 0 ? r.hard : food == 1 ? r.potion : r.fast) <= tick;
    }
    if (!available) return (GsTargetClocks){NULL, 0};
    GsTargetReady* values = arena_alloc(arena, (size_t)available * sizeof(*values));
    int count = 0;
    for (int i = 0; i < clocks.count; i++) {
        GsTargetReady r = clocks.values[i];
        if ((food == 0 ? r.hard : food == 1 ? r.potion : r.fast) > tick) continue;
        r.hard = imax(r.hard, tick + 3);
        r.potion = imax(r.potion, food == 0 ? tick : tick + 3);
        r.fast = imax(r.fast, food == 2 ? tick + 3 : tick);
        gs_clocks_insert(values, &count, r);
    }
    return (GsTargetClocks){values, count};
}

static int gs_clocks_hard(GsTargetClocks clocks) {
    int hard = INT_MAX;
    for (int i = 0; i < clocks.count; i++) hard = imin(hard, clocks.values[i].hard);
    return hard;
}

static int gs_clocks_equal(GsTargetClocks a, GsTargetClocks b) {
    if (a.count != b.count) return 0;
    for (int i = 0; i < a.count; i++) if (gs_ready_compare(a.values[i], b.values[i]) != 0) return 0;
    return 1;
}

typedef struct {
    int hp, delay;
    GsFlight flight;
    GsVengeance vengeance;
    int returned, returns_at, echoed, echoes_at;
    GsTargetClocks clocks;
    int pending_hard;
} GsJointHealth;
typedef struct { GsJointHealth key; double* targets; unsigned hash; int next; } GsJointEntry;
typedef struct { GsVec entries; int* buckets; int capacity, length; } GsJointStates;

static GsJointStates gs_joint_states(Arena* arena, int length) {
    GsJointStates states = {vec_make(arena, sizeof(GsJointEntry)), arena_alloc(arena, 4 * sizeof(int)), 4, length};
    memset(states.buckets, 0, 4 * sizeof(int));
    return states;
}

static GsJointHealth gs_joint_key(GsJointHealth key) {
    if ((key.flight == GS_FLIGHT_RESOLVED || key.flight == GS_FLIGHT_CANCELLED) && key.vengeance != GS_VENGEANCE_UP && !key.returned) key.hp = 1;
    if (key.flight != GS_FLIGHT_WAITING && key.flight != GS_FLIGHT_COMMITTED) key.delay = 0;
    if (!key.echoed) key.echoes_at = 0;
    return key;
}

static int gs_joint_equal(GsJointHealth a, GsJointHealth b) {
    return a.hp == b.hp && a.delay == b.delay && a.flight == b.flight && a.vengeance == b.vengeance
        && a.returned == b.returned && a.returns_at == b.returns_at && a.echoed == b.echoed && a.echoes_at == b.echoes_at
        && a.pending_hard == b.pending_hard && gs_clocks_equal(a.clocks, b.clocks);
}

static double* gs_joint_target(GsJointStates* states, GsJointHealth key) {
    key = gs_joint_key(key);
    unsigned hash = 2166136261u;
    const int fields[] = {key.hp, key.delay, key.flight, key.vengeance, key.returned, key.returns_at, key.echoed, key.echoes_at, key.pending_hard};
    for (int i = 0; i < 9; i++) hash = (hash ^ (unsigned)fields[i]) * 16777619u;
    for (int i = 0; i < key.clocks.count; i++) {
        GsTargetReady r = key.clocks.values[i];
        hash = (hash ^ (unsigned)r.hard) * 16777619u;
        hash = (hash ^ (unsigned)r.potion) * 16777619u;
        hash = (hash ^ (unsigned)r.fast) * 16777619u;
    }
    int bucket = hash & (states->capacity - 1);
    for (int index = states->buckets[bucket]; index; index = VEC_AT(GsJointEntry, states->entries, index - 1).next) {
        GsJointEntry* entry = &VEC_AT(GsJointEntry, states->entries, index - 1);
        GsJointHealth k = entry->key;
        if (entry->hash == hash && gs_joint_equal(k, key)) return entry->targets;
    }
    if (states->entries.count * 4 >= states->capacity * 3) {
        states->capacity *= 2;
        states->buckets = arena_alloc(states->entries.arena, (size_t)states->capacity * sizeof(int));
        memset(states->buckets, 0, (size_t)states->capacity * sizeof(int));
        for (int i = 0; i < states->entries.count; i++) {
            GsJointEntry* entry = &VEC_AT(GsJointEntry, states->entries, i);
            int at = entry->hash & (states->capacity - 1);
            entry->next = states->buckets[at];
            states->buckets[at] = i + 1;
        }
        bucket = hash & (states->capacity - 1);
    }
    GsJointEntry* entry = vec_push(&states->entries);
    *entry = (GsJointEntry){key, arena_alloc(states->entries.arena, (size_t)states->length * sizeof(double)), hash, states->buckets[bucket]};
    memset(entry->targets, 0, (size_t)states->length * sizeof(double));
    states->buckets[bucket] = states->entries.count;
    return entry->targets;
}

static void gs_joint_mass(GsJointStates* states, GsJointHealth key, int hp, double mass) {
    if (mass != 0) gs_joint_target(states, key)[hp] += mass;
}

static void gs_joint_prune(GsJointStates* states) {
    int count = 0;
    for (int i = 0; i < states->entries.count; i++) {
        GsJointEntry entry = VEC_AT(GsJointEntry, states->entries, i);
        int live = 0;
        for (int hp = 0; hp < states->length; hp++) if (entry.targets[hp] != 0) { live = 1; break; }
        if (live) VEC_AT(GsJointEntry, states->entries, count++) = entry;
    }
    if (count == states->entries.count) return;
    states->entries.count = count;
    memset(states->buckets, 0, (size_t)states->capacity * sizeof(int));
    for (int i = 0; i < count; i++) {
        GsJointEntry* entry = &VEC_AT(GsJointEntry, states->entries, i);
        int bucket = entry->hash & (states->capacity - 1);
        entry->next = states->buckets[bucket];
        states->buckets[bucket] = i + 1;
    }
}

typedef struct { GsJointHealth key; double weight; } GsJointArrival;

static GsOutcome gs_strikes(Arena* arena, const GsSpecSituation* s, const GsSpecHit* hits, int hit_count,
        const GsEating* eatings, int eating_count, GsEcho echo, int anchor, int through) {
    int length = imax(s->hitpoints.max, GS_OVERHEALED_HP) + 1;
    double* initial = NULL;
    if (!s->target.has_belief) {
        if (s->target.has_history) {
            initial = arena_alloc(arena, (size_t)length * sizeof(double));
            memset(initial, 0, (size_t)length * sizeof(double));
            int supported = 0;
            for (int b = 0; b < s->target.history_count; b++) {
                GsTargetBranch branch = s->target.history[b];
                for (int hp = 1; hp < branch.levels; hp++) if (branch.hp[hp] && !initial[hp]) { initial[hp] = 1; supported++; }
            }
            for (int hp = 1; hp < length; hp++) initial[hp] /= supported;
        } else {
            initial = hp_uniform(arena, imax(1, s->hitpoints.low), s->hitpoints.high, length);
            for (int u = 0; u < s->warning.unseen_count; u++) if (s->warning.unseen[u] < anchor) gs_hp_hiding(arena, initial, length);
        }
    }
    const GsIncoming* his = &s->incoming;
    int own_hit_max = 0, last_return = INT_MIN;
    for (int h = 0; h < hit_count; h++) {
        GsSpecHit hit = hits[h];
        if (hit.origin != GS_ORIGIN_OURS) continue;
        own_hit_max = imax(own_hit_max, hit.damages - 1);
        if (hit.damages > 1 && hit.lands <= through)
            last_return = imax(last_return, hit.lands + (s->pid == GS_PID_LOCAL ? 1 : 0));
    }
    if (last_return == INT_MIN) last_return = through;
    int return_max = s->vengeance == GS_VENGEANCE_UP && own_hit_max > 0 ? imax(1, own_hit_max * 3 / 4) : 0;
    int survives = s->local.hitpoints > (s->has_incoming ? his->most : 0) + return_max
        && (s->vengeance != GS_VENGEANCE_UP || last_return <= through);
    static const double zero_roll[] = {1};
    GsSpecHit incoming = {0, zero_roll, 1, GS_ORIGIN_OURS, 1, 0};
    if (!(survives && echo == GS_ECHO_RECOIL && s->recoil_charges == 0) && s->has_incoming)
        incoming = gs_spec_hit_of(arena, his->lands, his->least, his->most, his->accuracy);
    double* prefix = !survives && s->vengeance == GS_VENGEANCE_UP ? arena_alloc(arena, (size_t)(length + 1) * sizeof(double)) : NULL;
    GsJointArrival* arrivals = s->has_incoming ? arena_alloc(arena, (size_t)incoming.damages * sizeof(*arrivals)) : NULL;
    double totals[4] = {0};
    GsJointStates states = gs_joint_states(arena, length);
    GsJointHealth start = {.hp = s->local.hitpoints,
        .flight = !s->has_incoming ? GS_FLIGHT_RESOLVED : his->attack != GS_NONE ? GS_FLIGHT_WAITING : GS_FLIGHT_COMMITTED,
        .vengeance = s->vengeance, .pending_hard = GS_NONE};
    if (s->target.has_belief) {
        for (int b = 0; b < s->target.belief_count; b++) {
            GsTargetBranch branch = s->target.belief[b];
            start.clocks = (GsTargetClocks){&s->target.belief[b].ready, 1};
            for (int hp = 1; hp < branch.levels; hp++) gs_joint_mass(&states, start, hp, branch.hp[hp]);
        }
    } else if (s->target.has_history) {
        GsTargetReady* scratch = arena_alloc(arena, (size_t)s->target.history_count * sizeof(*scratch));
        for (int hp = 1; hp < length; hp++) {
            if (!initial[hp]) continue;
            int count = 0;
            for (int b = 0; b < s->target.history_count; b++) {
                GsTargetBranch branch = s->target.history[b];
                if (hp < branch.levels && branch.hp[hp]) gs_clocks_insert(scratch, &count, branch.ready);
            }
            GsTargetClocks clocks = {scratch, count};
            int found = 0;
            for (int i = 0; i < states.entries.count; i++) {
                GsTargetClocks existing = VEC_AT(GsJointEntry, states.entries, i).key.clocks;
                if (gs_clocks_equal(existing, clocks)) { clocks = existing; found = 1; break; }
            }
            if (!found) {
                GsTargetReady* values = arena_alloc(arena, (size_t)count * sizeof(*values));
                memcpy(values, scratch, (size_t)count * sizeof(*values));
                clocks.values = values;
            }
            start.clocks = clocks;
            gs_joint_target(&states, start)[hp] += initial[hp];
        }
    } else {
        GsTargetReady* ready = arena_alloc(arena, sizeof(*ready));
        *ready = (GsTargetReady){s->hard_food_ready, GS_NONE, GS_NONE};
        start.clocks = (GsTargetClocks){ready, 1};
        memcpy(gs_joint_target(&states, start), initial, (size_t)length * sizeof(double));
    }
    int first = s->tick + 1;
    for (int i = 0; i < hit_count; i++) first = imin(first, hits[i].lands);
    for (int i = 0; i < eating_count; i++) first = imin(first, eatings[i].tick);
    if (s->has_incoming) first = imin(first, his->attack == GS_NONE ? his->lands : his->attack);
    for (int i = 0; i < s->local.heal_count; i++) first = imin(first, s->local.heals[i].tick);
    for (int tick = first; tick <= through; tick++) {
        int scheduled = 0;
        for (int e = 0; e < eating_count; e++) scheduled |= eatings[e].tick >= tick;
        GsJointStates normalized = gs_joint_states(arena, length);
        for (int i = 0; i < states.entries.count; i++) {
            GsJointEntry entry = VEC_AT(GsJointEntry, states.entries, i);
            GsJointHealth key = entry.key;
            key.clocks = scheduled || key.pending_hard != GS_NONE ? gs_clocks_at(arena, key.clocks, tick) : (GsTargetClocks){NULL, 0};
            double* out = gs_joint_target(&normalized, key);
            for (int hp = 0; hp < length; hp++) out[hp] += entry.targets[hp];
        }
        states = normalized;
        int commits = 0;
        if (s->has_incoming && his->attack != GS_NONE)
            for (int i = 0; i < states.entries.count; i++) {
                GsJointHealth key = VEC_AT(GsJointEntry, states.entries, i).key;
                commits |= key.flight == GS_FLIGHT_WAITING && tick >= his->attack + key.delay;
            }
        if (commits) {
            GsJointStates next = gs_joint_states(arena, length);
            for (int i = 0; i < states.entries.count; i++) {
                GsJointEntry entry = VEC_AT(GsJointEntry, states.entries, i);
                GsJointHealth key = entry.key;
                if (key.flight == GS_FLIGHT_WAITING && tick >= his->attack + key.delay) key.flight = GS_FLIGHT_COMMITTED;
                for (int hp = 0; hp < length; hp++) {
                    if (entry.targets[hp] == 0) continue;
                    GsJointHealth target = key;
                    if (hp == 0 && entry.key.flight == GS_FLIGHT_WAITING) target.flight = GS_FLIGHT_CANCELLED;
                    gs_joint_target(&next, target)[hp] += entry.targets[hp];
                }
            }
            states = next;
        }
        for (int phase = 0; phase < 3; phase++) {
            if (!survives && phase == (s->pid == GS_PID_LOCAL ? 0 : 2)) {
                for (int h = 0; h < s->local.heal_count; h++) {
                    GsSpecHeal heal = s->local.heals[h];
                    if (heal.tick != tick) continue;
                    GsJointStates next = gs_joint_states(arena, length);
                    for (int i = 0; i < states.entries.count; i++) {
                        GsJointEntry entry = VEC_AT(GsJointEntry, states.entries, i);
                        GsJointHealth key = entry.key;
                        key.hp = imax(key.hp, imin(heal.cap, key.hp + heal.amount));
                        double* out = gs_joint_target(&next, key);
                        for (int hp = 0; hp < length; hp++) out[hp] += entry.targets[hp];
                    }
                    states = next;
                }
            }
            if (phase == (s->pid == GS_PID_LOCAL ? 2 : 0)) {
                int retries = 0;
                for (int i = 0; i < states.entries.count; i++) {
                    int pending = VEC_AT(GsJointEntry, states.entries, i).key.pending_hard;
                    retries |= pending != GS_NONE && pending <= tick;
                }
                for (int e = 0; e < eating_count + retries; e++) {
                    int retry = e == eating_count;
                    GsEating eating = retry ? (GsEating){tick, 0, 1} : eatings[e];
                    if (eating.tick != tick) continue;
                    GsJointStates next = gs_joint_states(arena, length);
                    for (int i = 0; i < states.entries.count; i++) {
                        GsJointEntry entry = VEC_AT(GsJointEntry, states.entries, i);
                        GsJointHealth key = entry.key;
                        if (retry && (key.pending_hard == GS_NONE || key.pending_hard > tick)) {
                            double* out = gs_joint_target(&next, key);
                            for (int hp = 0; hp < length; hp++) out[hp] += entry.targets[hp];
                            continue;
                        }
                        gs_joint_mass(&next, key, 0, entry.targets[0]);
                        int pending = key.pending_hard;
                        if (eating.first_food == 0 && eating.food_count > 0) {
                            int hard = gs_clocks_hard(key.clocks);
                            pending = hard <= tick ? GS_NONE : hard;
                        }
                        GsJointHealth refused = key;
                        refused.pending_hard = pending;
                        GsJointHealth eaten[GS_MEAL_FOODS];
                        int available[GS_MEAL_FOODS], count = 0;
                        GsTargetClocks clocks = key.clocks;
                        int delay = key.delay;
                        for (int food = eating.first_food; food < eating.first_food + eating.food_count; food++) {
                            GsTargetClocks consumed = gs_clocks_consume(arena, clocks, food, tick);
                            if (!consumed.count) continue;
                            clocks = consumed;
                            if (tick > s->tick && key.flight == GS_FLIGHT_WAITING && tick < his->attack + delay)
                                delay += food == 0 ? 3 : food == 2 ? 2 : 0;
                            available[count] = food;
                            eaten[count] = key;
                            eaten[count].delay = delay;
                            eaten[count].clocks = clocks;
                            eaten[count].pending_hard = pending == GS_NONE ? GS_NONE : gs_clocks_hard(clocks);
                            count++;
                        }
                        int base = s->target.has_history ? s->target.base_hp : 99;
                        for (int hp = 1; hp < length; hp++) {
                            double mass = entry.targets[hp];
                            if (mass == 0) continue;
                            int level = hp;
                            double wanted = gs_wants(level);
                            gs_joint_mass(&next, refused, level, mass * (1 - wanted));
                            for (int f = 0; f < count; f++) {
                                int food = available[f], heal = food == 1 ? base * 15 / 100 + 2 : GS_MEAL_HEALS[food];
                                level = imax(level, imin(food == 1 ? base + heal : base, level + heal));
                                double after = f + 1 < count ? gs_wants(level) : 0;
                                gs_joint_mass(&next, eaten[f], level, mass * (wanted - after));
                                wanted = after;
                            }
                            gs_joint_mass(&next, count ? eaten[count - 1] : refused, level, mass * wanted);
                        }
                    }
                    states = next;
                }
            }
            if (phase != 1) continue;
            int returns = 0;
            for (int i = 0; i < states.entries.count; i++) returns |= VEC_AT(GsJointEntry, states.entries, i).key.returns_at == tick;
            if (returns) {
                GsJointStates next = gs_joint_states(arena, length);
                for (int i = 0; i < states.entries.count; i++) {
                    GsJointEntry entry = VEC_AT(GsJointEntry, states.entries, i);
                    GsJointHealth key = entry.key;
                    int damage = key.returns_at == tick ? key.returned : 0;
                    if (damage >= key.hp) { totals[1] += jdk_sum(entry.targets, length); continue; }
                    if (key.returns_at == tick) { key.hp -= damage; key.returned = 0; key.returns_at = 0; }
                    double* out = gs_joint_target(&next, key);
                    for (int hp = 0; hp < length; hp++) out[hp] += entry.targets[hp];
                }
                states = next;
            }
            for (int order = 0; order < 2; order++) {
                if (order == (s->pid == GS_PID_LOCAL ? 0 : 1)) {
                    for (int h = 0; h < hit_count; h++) {
                        GsSpecHit hit = hits[h];
                        if (hit.lands != tick) continue;
                        GsJointStates next = gs_joint_states(arena, length);
                        double* tail = arena_alloc(arena, (size_t)hit.damages * sizeof(double));
                        double remaining = 0;
                        for (int roll = hit.damages - 1; roll >= 1; roll--) { remaining += hit.damage[roll]; tail[roll] = remaining; }
                        int at = hit.lands + (s->pid == GS_PID_LOCAL ? 1 : 0);
                        int can_rescue = 0;
                        if (s->pid == GS_PID_LOCAL)
                            for (int h = 0; h < s->local.heal_count; h++) can_rescue |= s->local.heals[h].tick == at;
                        for (int i = 0; i < states.entries.count; i++) {
                            GsJointEntry entry = VEC_AT(GsJointEntry, states.entries, i);
                            GsJointHealth key = entry.key;
                            double* unchanged = gs_joint_target(&next, key);
                            unchanged[0] += entry.targets[0];
                            if (survives || hit.origin != GS_ORIGIN_OURS || key.vengeance != GS_VENGEANCE_UP) {
                                for (int hp = 1; hp < length; hp++) {
                                    if (entry.targets[hp] == 0) continue;
                                    for (int roll = 0; roll < hit.damages; roll++) unchanged[imax(0, hp - roll)] += entry.targets[hp] * hit.damage[roll];
                                }
                                continue;
                            }
                            int highest = length - 1;
                            while (highest > 0 && entry.targets[highest] == 0) highest--;
                            for (int hp = 1; hp <= highest; hp++) unchanged[hp] += entry.targets[hp] * hit.damage[0];
                            if ((key.flight == GS_FLIGHT_RESOLVED || key.flight == GS_FLIGHT_CANCELLED) && at <= through && !can_rescue) {
                                int lethal = key.hp == 1 ? 1 : (4 * key.hp + 2) / 3;
                                if (lethal < hit.damages) {
                                    double endangered = 0;
                                    for (int hp = lethal; hp <= highest; hp++) endangered += entry.targets[hp];
                                    totals[1] += tail[lethal] * endangered;
                                }
                                if (lethal > 1) {
                                    GsJointHealth changed = key;
                                    changed.hp = 1; changed.vengeance = GS_VENGEANCE_DOWN;
                                    changed.returned = 0; changed.returns_at = 0;
                                    double* out = gs_joint_target(&next, changed);
                                    int most = imin(lethal - 1, hit.damages - 1);
                                    for (int dealt = 1; dealt <= most && dealt <= highest; dealt++) out[0] += entry.targets[dealt] * tail[dealt];
                                    prefix[1] = 0;
                                    for (int hp = 1; hp <= highest; hp++) prefix[hp + 1] = prefix[hp] + entry.targets[hp];
                                    for (int hp = 1; hp <= highest; hp++)
                                        out[hp] += hit.positive_each * (prefix[imin(highest + 1, hp + most + 1)]
                                            - prefix[imin(highest + 1, hp + imin(most + 1, hit.first_positive_roll))]);
                                }
                                continue;
                            }
                            for (int dealt = 1; dealt < hit.damages && dealt <= highest; dealt++) {
                                if (hit.damage[dealt] == 0 && entry.targets[dealt] * tail[dealt] == 0) continue;
                                int returned = gs_vengeance_return(dealt);
                                GsJointHealth changed = key;
                                changed.vengeance = GS_VENGEANCE_DOWN;
                                if (at == hit.lands || ((key.flight == GS_FLIGHT_RESOLVED || key.flight == GS_FLIGHT_CANCELLED)
                                        && at <= through && returned < key.hp)) {
                                    if (returned >= key.hp) {
                                        totals[1] += entry.targets[dealt] * tail[dealt];
                                        for (int hp = dealt + 1; hp <= highest; hp++) totals[1] += entry.targets[hp] * hit.damage[dealt];
                                        continue;
                                    }
                                    changed.hp -= returned; changed.returned = 0; changed.returns_at = 0;
                                } else { changed.returned = returned; changed.returns_at = at; }
                                double* out = gs_joint_target(&next, changed);
                                out[0] += entry.targets[dealt] * tail[dealt];
                                for (int hp = dealt + 1; hp <= highest; hp++) out[hp - dealt] += entry.targets[hp] * hit.damage[dealt];
                            }
                        }
                        gs_joint_prune(&next);
                        states = next;
                    }
                } else if (s->has_incoming) {
                    int arrives = 0;
                    for (int i = 0; i < states.entries.count; i++) {
                        GsJointHealth key = VEC_AT(GsJointEntry, states.entries, i).key;
                        arrives |= key.flight == GS_FLIGHT_COMMITTED && tick == his->lands + key.delay;
                    }
                    if (!arrives) continue;
                    GsJointStates next = gs_joint_states(arena, length);
                    for (int i = 0; i < states.entries.count; i++) {
                        GsJointEntry entry = VEC_AT(GsJointEntry, states.entries, i);
                        GsJointHealth key = entry.key;
                        if (key.flight != GS_FLIGHT_COMMITTED || tick != his->lands + key.delay) {
                            double* out = gs_joint_target(&next, key);
                            for (int hp = 0; hp < length; hp++) out[hp] += entry.targets[hp];
                            continue;
                        }
                        double mass = jdk_sum(entry.targets, length);
                        int highest = length - 1;
                        while (highest > 0 && entry.targets[highest] == 0) highest--;
                        int lowest = 0;
                        while (lowest < highest && entry.targets[lowest] == 0) lowest++;
                        int arrival_count = 0;
                        for (int roll = 0; roll < incoming.damages; roll++) {
                            double weight = incoming.damage[roll];
                            if (weight == 0) continue;
                            if (roll >= key.hp) { totals[2] += weight * mass; continue; }
                            int remaining_hp = survives ? key.hp : key.hp - roll;
                            if (key.vengeance != GS_VENGEANCE_UP && key.returned >= remaining_hp && key.returns_at <= through && s->local.heal_count == 0) {
                                totals[1] += weight * mass;
                                continue;
                            }
                            int risk = key.vengeance == GS_VENGEANCE_UP ? return_max : key.returned;
                            int survives_return = remaining_hp > risk && (key.vengeance == GS_VENGEANCE_UP ? last_return <= through : key.returns_at <= through);
                            GsJointHealth changed = key;
                            changed.hp = survives_return ? 1 : remaining_hp;
                            changed.flight = GS_FLIGHT_RESOLVED;
                            changed.vengeance = survives_return ? GS_VENGEANCE_DOWN : key.vengeance;
                            changed.returned = survives_return ? 0 : key.returned;
                            changed.returns_at = survives_return ? 0 : key.returns_at;
                            changed.echoed = (echo == GS_ECHO_VENGEANCE ? gs_vengeance_return(roll) : 0) + (roll > 0 ? imin(s->recoil_charges, roll / 10 + 1) : 0);
                            changed.echoes_at = tick + (s->pid == GS_PID_LOCAL ? 0 : 1);
                            changed = gs_joint_key(changed);
                            int a = 0;
                            while (a < arrival_count && !gs_joint_equal(arrivals[a].key, changed)) a++;
                            if (a == arrival_count) arrivals[arrival_count++] = (GsJointArrival){changed, weight};
                            else arrivals[a].weight += weight;
                        }
                        for (int a = 0; a < arrival_count; a++) {
                            double* out = gs_joint_target(&next, arrivals[a].key);
                            double weight = arrivals[a].weight;
                            for (int hp = lowest; hp <= highest; hp++)
                                if (entry.targets[hp] != 0) out[hp] += weight * entry.targets[hp];
                        }
                    }
                    states = next;
                }
            }
            int echoes = 0;
            for (int i = 0; i < states.entries.count; i++) echoes |= VEC_AT(GsJointEntry, states.entries, i).key.echoes_at == tick;
            if (echoes) {
                GsJointStates next = gs_joint_states(arena, length);
                for (int i = 0; i < states.entries.count; i++) {
                    GsJointEntry entry = VEC_AT(GsJointEntry, states.entries, i);
                    GsJointHealth key = entry.key;
                    int damage = tick == key.echoes_at ? key.echoed : 0;
                    if (tick == key.echoes_at) { key.echoed = 0; key.echoes_at = 0; }
                    double* out = gs_joint_target(&next, key);
                    for (int hp = 0; hp < length; hp++) out[imax(0, hp - damage)] += entry.targets[hp];
                }
                states = next;
            }
        }
        for (int i = 0; i < states.entries.count; i++) {
            GsJointEntry entry = VEC_AT(GsJointEntry, states.entries, i);
            if (entry.key.flight != GS_FLIGHT_COMMITTED && entry.key.returns_at <= tick) { totals[0] += entry.targets[0]; entry.targets[0] = 0; }
        }
        gs_joint_prune(&states);
    }
    for (int i = 0; i < states.entries.count; i++) totals[3] += jdk_sum(VEC_AT(GsJointEntry, states.entries, i).targets, length);
    assert(fabs(jdk_sum(totals, 4) - 1) < 1e-9);
    return (GsOutcome){totals[0], totals[1]};
}

/* AutoSpec.knockout(Situation, planned, echo) */
static GsOutcome gs_spec_outcome(Arena* arena, const GsSpecSituation* s, const GsSpecHit* planned, int planned_count, GsEcho echo, int through) {
    if (s->local.hitpoints == 0) return (GsOutcome){0, 0};
    size_t mark = arena->used;
    int struck_count = s->underway_count + planned_count;
    GsSpecHit* struck = arena_alloc(arena, (size_t)(struck_count + 1) * sizeof(GsSpecHit));
    memcpy(struck, s->underway, (size_t)s->underway_count * sizeof(GsSpecHit));
    memcpy(struck + s->underway_count, planned, (size_t)planned_count * sizeof(GsSpecHit));
    int first = INT_MAX;
    for (int i = 0; i < struck_count; i++) first = imin(first, struck[i].lands);
    GsSpecHit undelayed = {0};
    if (s->has_incoming) {
        undelayed = gs_echo(arena, &s->incoming, echo, s->recoil_charges, s->pid);
        first = imin(first, undelayed.lands);
    }
    if (first == INT_MAX) { arena->used = mark; return (GsOutcome){0, 0}; }
    g_gs_expected_hp = (s->hitpoints.low + s->hitpoints.high) / 2.0;
    int has_since = s->warning.since != GS_NONE && s->warning.since >= s->tick - GS_REACTION_TICKS;
    int anchor = has_since ? s->warning.since : first;
    int our_first = INT_MAX;
    for (int i = 0; i < struck_count; i++) if (struck[i].origin == GS_ORIGIN_OURS) our_first = imin(our_first, struck[i].lands);
    if (our_first == INT_MAX) our_first = anchor + 1;
    GsReaction reactions[GS_REACTIONS];
    int reaction_count = gs_reactions(our_first - 1 - anchor, reactions);
    double shares_sum = 0, shares_compensation = 0;
    int live[GS_REACTIONS], live_count = 0;
    for (int r = 0; r < reaction_count; r++) {
        const GsReaction* reaction = &reactions[r];
        int unseen = 0;
        if (reaction->has_meal)
            for (int u = 0; u < s->warning.unseen_count; u++) unseen |= s->warning.unseen[u] == anchor + reaction->after;
        if (!reaction->has_meal || anchor + reaction->after > s->tick || (!s->target.has_history && (!has_since || unseen))) {
            live[live_count++] = r;
            double tmp = reaction->share - shares_compensation;
            double velvel = shares_sum + tmp;
            shares_compensation = (velvel - shares_sum) - tmp;
            shares_sum = velvel;
        }
    }
    double shares = shares_sum - shares_compensation;
    double dead = 0, suicide = 0;
    for (int li = 0; li < live_count; li++) {
        const GsReaction* reaction = &reactions[live[li]];
        size_t inner = arena->used;
        GsEating eatings[1];
        int eating_count = 0;
        if (reaction->has_meal) {
            int at = anchor + reaction->after;
            if (at <= through && (!s->target.has_history || at > s->tick))
                eatings[eating_count++] = (GsEating){at, 0, GS_MEAL_FOODS};
        }
        GsOutcome outcome = gs_strikes(arena, s, struck, struck_count, eatings, eating_count, echo, anchor, through);
        dead += reaction->share / shares * outcome.kill;
        suicide += reaction->share / shares * outcome.death;
        arena->used = inner;
    }
    arena->used = mark;
    return (GsOutcome){dead, suicide};
}
#endif

static int gs_spec_shift(const GsSpecSituation* s) { return s->pid == GS_PID_LOCAL ? -1 : 1; }

static int gs_spec_cast_before_hit(const GsSpecSituation* s);

static GsSpecSituation gs_spec_flipped(const GsSpecSituation* s) {
    assert(s->pid != GS_PID_UNKNOWN);
    GsSpecSituation other = *s;
    int shift = gs_spec_shift(s);
    other.pid = s->pid == GS_PID_LOCAL ? GS_PID_OPPONENT : GS_PID_LOCAL;
    other.pid_flip = 0;
    if (other.has_incoming) {
        other.incoming.lands += shift;
        if (other.incoming.attack != GS_NONE) other.incoming.attack += shift;
    }
    return other;
}

/* AutoSpec.priced: the outcome as the owner read last holds PID and as the other owner would, weighted by the chance it flipped. */
static GsOutcome gs_spec_priced(Arena* arena, const GsSpecSituation* s, const GsSpecHit* planned, int planned_count, GsEcho standing, int cast_aware
#ifndef GS_HP_BELIEF
    , int through
#endif
) {
    GsEcho held_echo = cast_aware && gs_spec_cast_before_hit(s) ? GS_ECHO_VENGEANCE : standing;
    GsOutcome held = gs_spec_outcome(arena, s, planned, planned_count, held_echo
#ifndef GS_HP_BELIEF
        , through
#endif
    );
    if (s->pid_flip == 0) return held;
    GsSpecSituation other = gs_spec_flipped(s);
    size_t mark = arena->used;
    GsSpecHit* shifted = arena_alloc(arena, (size_t)(planned_count + 1) * sizeof(GsSpecHit));
    for (int i = 0; i < planned_count; i++) {
        shifted[i] = planned[i];
        shifted[i].lands -= gs_spec_shift(s);
    }
    GsEcho turned_echo = cast_aware && gs_spec_cast_before_hit(&other) ? GS_ECHO_VENGEANCE : standing;
    GsOutcome turned = gs_spec_outcome(arena, &other, shifted, planned_count, turned_echo
#ifndef GS_HP_BELIEF
        , through
#endif
    );
    arena->used = mark;
    return (GsOutcome){(1 - s->pid_flip) * held.kill + s->pid_flip * turned.kill, (1 - s->pid_flip) * held.death + s->pid_flip * turned.death};
}

static double gs_spec_knockout(Arena* arena, const GsSpecSituation* s, const GsSpecHit* planned, int planned_count, GsEcho echo
#ifndef GS_HP_BELIEF
    , int through
#endif
) {
    return gs_spec_priced(arena, s, planned, planned_count, echo, 0
#ifndef GS_HP_BELIEF
        , through
#endif
    ).kill;
}

#ifndef GS_HP_BELIEF
static int gs_spec_horizon(const GsSpecSituation* s, const GsSpecHit* programs, int program_count) {
    int through = s->tick;
    int maximum_delay = consumption_delay(consumption_of(1, 1, 1));
    for (int branch = 0; branch < 2; branch++) {
        if ((!branch && s->pid_flip == 1) || (branch && s->pid_flip == 0)) continue;
        int shift = branch ? gs_spec_shift(s) : 0;
        GsPid pid = branch ? gs_spec_flipped(s).pid : s->pid;
        for (int i = 0; i < s->underway_count; i++) through = imax(through, s->underway[i].lands + (pid == GS_PID_LOCAL ? 1 : 0));
        for (int i = 0; i < program_count; i++) through = imax(through, programs[i].lands - shift + (pid == GS_PID_LOCAL ? 1 : 0));
        if (s->has_incoming) through = imax(through, s->incoming.lands + shift + (s->incoming.attack != GS_NONE ? maximum_delay : 0) + (pid == GS_PID_LOCAL ? 0 : 1));
    }
    return through;
}
#endif

/* AutoSpec.decide */
typedef enum { GS_CHOICE_HOLD, GS_CHOICE_VOIDWAKER, GS_CHOICE_MAUL_VOIDWAKER, GS_CHOICE_MAUL, GS_CHOICE_VOIDWAKER_BURST, GS_CHOICE_COUNT } GsChoiceKind;
static const int GS_CHOICE_TICKETS[GS_CHOICE_COUNT] = {0, 1, 2, 1, 2};
typedef enum { GS_CAST_NOW, GS_CAST_KEEP } GsCast;
typedef struct { GsChoiceKind choice; const GsSpecHit* hits; int hit_count; } GsSpecPlan;
typedef struct { GsChoiceKind choice; GsCast cast; GsChoiceKind aim; double chance, bar, death, held, kept, burst_bar; int through; } GsSpecDecision;

enum { GS_TICKET = 50, GS_MOST_TICKETS = 2 };
/* AutoSpec.CONTINUATION: port the policy values, not its retired calibration input. */
static const double GS_CONTINUATION[GS_MOST_TICKETS + 1] = {0, 0.30164267160692, 0.39811423486135994};

/* AutoSpec.bar: the chance a plan costing cost specs must reach with energy to be played. */
static double gs_spec_held(int energy) { return GS_CONTINUATION[imin(GS_MOST_TICKETS, energy / GS_TICKET)]; }
static double gs_spec_kept(int energy, int cost) {
    int tickets = imin(GS_MOST_TICKETS, energy / GS_TICKET);
    return tickets < cost ? 0 : GS_CONTINUATION[tickets - cost];
}
static double gs_spec_bar(int energy, int cost) {
    if (imin(GS_MOST_TICKETS, energy / GS_TICKET) < cost) return INFINITY;
    double spent = gs_spec_kept(energy, cost);
    return (gs_spec_held(energy) - spent) / (1 - spent);
}

static double gs_spec_value(GsOutcome outcome, int energy, GsChoiceKind choice) {
    int cost = GS_CHOICE_TICKETS[choice];
    if (energy < cost * GS_TICKET) return -INFINITY;
    return outcome.kill + (1 - outcome.kill) * gs_spec_kept(energy, cost);
}

static int spec_plan_done(const GsSpecPlan* plan) {
    int done = INT_MIN;
    for (int i = 0; i < plan->hit_count; i++) done = imax(done, plan->hits[i].lands);
    return plan->hit_count ? done : INT_MAX;
}

static int spec_rebound_casts_before(const GsRebound* r, int lands, GsPid first) {
    return r->kind == GS_REBOUND_CASTABLE && (r->cast < lands || (r->cast == lands && first == GS_PID_LOCAL));
}

static int gs_spec_cast_before_hit(const GsSpecSituation* s) {
    return s->has_incoming && s->incoming.attack == GS_NONE && spec_rebound_casts_before(&s->rebound, s->incoming.lands, s->pid);
}

static GsSpecDecision gs_auto_spec_decide_priced(Arena* arena, const GsSpecSituation* s, const GsSpecPlan* plans, int plan_count,
    const GsSpecPlan* after_cast, int after_cast_count, int energy, double death_price) {
    static const GsSpecPlan nothing = {GS_CHOICE_HOLD, NULL, 0};
    int casts = gs_spec_cast_before_hit(s);
    if (!casts && s->pid_flip > 0) {
        GsSpecSituation other = gs_spec_flipped(s);
        casts = gs_spec_cast_before_hit(&other);
    }
    int through = 0;
#ifndef GS_HP_BELIEF
    through = gs_spec_horizon(s, NULL, 0);
    for (int i = 0; i < plan_count; i++) through = imax(through, gs_spec_horizon(s, plans[i].hits, plans[i].hit_count));
    if (casts) for (int i = 0; i < after_cast_count; i++) through = imax(through, gs_spec_horizon(s, after_cast[i].hits, after_cast[i].hit_count));
#endif
    GsEcho standing = s->rebound.kind == GS_REBOUND_UP ? GS_ECHO_VENGEANCE : GS_ECHO_RECOIL;
    const GsSpecPlan* best = &nothing;
    GsCast cast = GS_CAST_KEEP;
    GsOutcome chosen = {0, 0};
    for (int i = 0; i <= plan_count; i++) {
        const GsSpecPlan* plan = i < plan_count ? &plans[i] : &nothing;
        if (plan->choice == GS_CHOICE_VOIDWAKER_BURST) continue;
        GsOutcome kept = gs_spec_priced(arena, s, plan->hits, plan->hit_count, standing, 0
#ifndef GS_HP_BELIEF
            , through
#endif
        );
        if (kept.kill > chosen.kill || (kept.kill == chosen.kill && kept.kill > 0 && spec_plan_done(plan) < spec_plan_done(best))) {
            best = plan;
            chosen = kept;
        }
    }
    for (int i = 0; i < plan_count; i++) {
        const GsSpecPlan* plan = &plans[i];
        if (plan->choice != GS_CHOICE_VOIDWAKER_BURST || energy < 2 * GS_TICKET) continue;
        GsOutcome burst = gs_spec_priced(arena, s, plan->hits, plan->hit_count, standing, 0
#ifndef GS_HP_BELIEF
            , through
#endif
        );
        if (gs_spec_value(burst, energy, plan->choice) - death_price * burst.death
                > gs_spec_value(chosen, energy, best->choice) - death_price * chosen.death) {
            best = plan;
            chosen = burst;
        }
    }
    if (casts) {
        for (int i = 0; i <= after_cast_count; i++) {
            const GsSpecPlan* plan = i < after_cast_count ? &after_cast[i] : &nothing;
            GsOutcome with_cast = gs_spec_priced(arena, s, plan->hits, plan->hit_count, standing, 1
#ifndef GS_HP_BELIEF
                , through
#endif
            );
            int better = plan->choice == GS_CHOICE_VOIDWAKER_BURST || best->choice == GS_CHOICE_VOIDWAKER_BURST
                ? energy >= GS_CHOICE_TICKETS[plan->choice] * GS_TICKET
                    && gs_spec_value(with_cast, energy, plan->choice) - death_price * with_cast.death
                        > gs_spec_value(chosen, energy, best->choice) - death_price * chosen.death
                : with_cast.kill > chosen.kill;
            if (better) {
                best = plan;
                cast = GS_CAST_NOW;
                chosen = with_cast;
            }
        }
    }
    double bar = gs_spec_bar(energy, GS_CHOICE_TICKETS[best->choice]);
    double held = gs_spec_held(energy), kept = gs_spec_kept(energy, GS_CHOICE_TICKETS[best->choice]);
    if (chosen.kill < bar || chosen.kill + (1 - chosen.kill) * kept - death_price * chosen.death < held)
        return (GsSpecDecision){GS_CHOICE_HOLD, GS_CAST_KEEP, best->choice, chosen.kill, bar, chosen.death, held, kept, 0, through};
    double burst_bar = 0;
    if (best->choice == GS_CHOICE_VOIDWAKER_BURST) {
        GsOutcome first = gs_spec_priced(arena, s, best->hits, 1, standing, cast == GS_CAST_NOW
#ifndef GS_HP_BELIEF
            , through
#endif
        );
        burst_bar = fmax(0, (held - first.kill) / (1 - first.kill));
    }
    return (GsSpecDecision){best->choice, cast, best->choice, chosen.kill, bar, chosen.death, held, kept, burst_bar, through};
}

static GsSpecDecision gs_auto_spec_decide(Arena* arena, const GsSpecSituation* s, const GsSpecPlan* plans, int plan_count,
    const GsSpecPlan* after_cast, int after_cast_count, int energy) {
    return gs_auto_spec_decide_priced(arena, s, plans, plan_count, after_cast, after_cast_count, energy, 0);
}

static GsSpecDecision gs_spec_continue_burst(Arena* arena, const GsSpecSituation* s, const GsSpecPlan* single,
    double reserved_bar, double death_price, int through) {
#ifdef GS_HP_BELIEF
    (void)through;
#endif
    GsEcho standing = s->rebound.kind == GS_REBOUND_UP ? GS_ECHO_VENGEANCE : GS_ECHO_RECOIL;
    GsOutcome outcome = gs_spec_priced(arena, s, single->hits, single->hit_count, standing, 0
#ifndef GS_HP_BELIEF
        , through
#endif
    );
    GsChoiceKind choice = outcome.kill > 0 && outcome.kill - death_price * outcome.death >= reserved_bar ? GS_CHOICE_VOIDWAKER : GS_CHOICE_HOLD;
    return (GsSpecDecision){choice, GS_CAST_KEEP, GS_CHOICE_VOIDWAKER, outcome.kill, reserved_bar, outcome.death, reserved_bar, 0, 0, through};
}
#endif
