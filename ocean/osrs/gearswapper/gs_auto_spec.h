#ifndef GS_AUTO_SPEC_H
#define GS_AUTO_SPEC_H
#include "gs_core.h"

/* AutoSpec.java: the knockout chance of a plan against an opponent that reacts as the recorded first meals did. */
enum { GS_REACTION_TICKS = 7 };
static const double GS_FIRST_MEAL[9] = {0.006, 0.221, 0.493, 0.166, 0.038, 0.032, 0.006, 0.006, 0.032};
static const double GS_SERVING_SHARES[4] = {0.15, 0.34, 0.34, 0.17};
typedef enum { GS_FOOD_HARD, GS_FOOD_BREW, GS_FOOD_KARAMBWAN } GsSpecFood;
static const int GS_FOOD_HEALS[3] = {22, 16, 18};
static const int GS_FOOD_CAPS[3] = {99, 115, 99};
static const GsConsumable GS_FOOD_KIND[3] = {GS_HARD_FOOD, GS_POTION, GS_FAST_FOOD};
static const int GS_SERVING_COUNT[4] = {1, 2, 2, 3};
static const GsSpecFood GS_SERVINGS[4][3] = {{GS_FOOD_HARD}, {GS_FOOD_HARD, GS_FOOD_KARAMBWAN}, {GS_FOOD_HARD, GS_FOOD_BREW},
    {GS_FOOD_HARD, GS_FOOD_KARAMBWAN, GS_FOOD_BREW}};

typedef struct { int after; int foods; const GsSpecFood* kinds; } GsReactionMeal;
typedef struct { double share; int has_meal; GsReactionMeal meal; } GsReaction;
enum { GS_REACTIONS = (GS_REACTION_TICKS + 1) * 4 + 1 };

static GsReaction GS_REACTION_TABLE[GS_REACTIONS];

__attribute__((constructor)) static void gs_build_reactions(void) {
    int n = 0;
    for (int after = 0; after <= GS_REACTION_TICKS; after++)
        for (int serving = 0; serving < 4; serving++)
            GS_REACTION_TABLE[n++] = (GsReaction){GS_FIRST_MEAL[after] * GS_SERVING_SHARES[serving], 1,
                {after, GS_SERVING_COUNT[serving], GS_SERVINGS[serving]}};
    GS_REACTION_TABLE[n++] = (GsReaction){GS_FIRST_MEAL[GS_REACTION_TICKS + 1], 0, {0, 0, NULL}};
}

typedef enum { GS_ORIGIN_OURS, GS_ORIGIN_REFLECTED } GsOrigin;
typedef struct { int lands; const double* damage; int damages; GsOrigin origin; } GsSpecHit;

static GsSpecHit gs_spec_hit_of(Arena* a, int lands, int least, int most, double accuracy) {
    double* damage = arena_alloc(a, (size_t)(most + 1) * sizeof(double));
    memset(damage, 0, (size_t)(most + 1) * sizeof(double));
    damage[0] = 1 - accuracy;
    for (int roll = least; roll <= most; roll++) damage[roll] += accuracy / (most - least + 1);
    return (GsSpecHit){lands, damage, most + 1, GS_ORIGIN_OURS};
}

typedef struct { int lands, least, most; double accuracy; int attack; } GsIncoming;

typedef enum { GS_ECHO_VENGEANCE, GS_ECHO_RECOIL } GsEcho;

static GsSpecHit gs_echo(Arena* a, const GsIncoming* incoming, GsEcho echo, int recoil_charges, GsPid pid) {
    int most = (echo == GS_ECHO_VENGEANCE ? incoming->most * 3 / 4 : 0) + imin(recoil_charges, incoming->most / 10 + 1);
    double* damage = arena_alloc(a, (size_t)(most + 1) * sizeof(double));
    memset(damage, 0, (size_t)(most + 1) * sizeof(double));
    damage[0] = 1 - incoming->accuracy;
    for (int dealt = incoming->least; dealt <= incoming->most; dealt++) {
        int reflected = (echo == GS_ECHO_VENGEANCE ? dealt * 3 / 4 : 0) + (dealt > 0 ? imin(recoil_charges, dealt / 10 + 1) : 0);
        damage[reflected] += incoming->accuracy / (incoming->most - incoming->least + 1);
    }
    return (GsSpecHit){incoming->lands + (pid == GS_PID_LOCAL ? 0 : 1), damage, most + 1, GS_ORIGIN_REFLECTED};
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

typedef struct {
    GsHealthRange hitpoints;
    const GsSpecHit* underway; int underway_count;
    int hard_food_ready;
    GsVengeance vengeance;
    GsPid pid;
    int has_incoming; GsIncoming incoming;
    int recoil_charges, tick;
    GsWarning warning;
    GsRebound rebound;
    GsHealth local;
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

static double gs_spec_alive(Arena* a, const GsSpecSituation* s, const GsIncoming* his, const GsSpecHit* hit) {
    if (!his) return 1;
    int first = hit->origin == GS_ORIGIN_OURS && (hit->lands < his->lands || (hit->lands == his->lands && s->pid == GS_PID_LOCAL));
    return first ? 1 : gs_health_survives(a, &s->local, his, s->pid);
}

static int gs_lethal_echo(const GsSpecSituation* s, const GsSpecHit* hit, int damage) {
    return s->vengeance == GS_VENGEANCE_UP && hit->origin == GS_ORIGIN_OURS && damage * 3 / 4 >= s->local.hitpoints;
}

/* AutoSpec.strikes */
static GsOutcome gs_strikes(Arena* a, const GsSpecSituation* s, const GsSpecHit* hits, int hit_count, const GsSpecHeal* heals, int heal_count, const GsIncoming* his) {
    int length = imax(s->hitpoints.max, GS_OVERHEALED_HP) + 1;
    double* alive = hp_uniform(a, imax(1, s->hitpoints.low), s->hitpoints.high, length);
    double dead = 0, suicide = 0;
    int next = 0;
    for (int i = 0; i < hit_count; i++) {
        size_t mark = a->used;
        while (next < heal_count && (heals[next].tick < hits[i].lands || (heals[next].tick == hits[i].lands && s->pid != GS_PID_LOCAL))) {
            hp_heal(alive, heals[next].amount, heals[next].cap);
            next++;
        }
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
        (void)mark;
    }
    return (GsOutcome){dead, suicide};
}

static void spec_hits_sort(GsSpecHit* hits, int count) {
    for (int i = 1; i < count; i++) {
        GsSpecHit hit = hits[i];
        int j = i - 1;
        while (j >= 0 && hits[j].lands > hit.lands) { hits[j + 1] = hits[j]; j--; }
        hits[j + 1] = hit;
    }
}

static int reaction_delay(const GsReactionMeal* meal) {
    int has[GS_CONSUMABLES] = {0};
    for (int i = 0; i < meal->foods; i++) has[GS_FOOD_KIND[meal->kinds[i]]] = 1;
    return consumption_delay(consumption_of(has[GS_POTION], has[GS_FAST_FOOD], has[GS_HARD_FOOD]));
}

/* AutoSpec.knockout(Situation, planned, echo) */
static GsOutcome gs_spec_outcome(Arena* arena, const GsSpecSituation* s, const GsSpecHit* planned, int planned_count, GsEcho echo) {
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
    int has_since = s->warning.since != GS_NONE && s->warning.since >= s->tick - GS_REACTION_TICKS;
    int anchor = has_since ? s->warning.since : first;
    int last = INT_MIN;
    for (int i = 0; i < struck_count; i++) last = imax(last, struck[i].lands);
    double shares_sum = 0, shares_compensation = 0;
    int live[GS_REACTIONS], live_count = 0;
    for (int r = 0; r < GS_REACTIONS; r++) {
        const GsReaction* reaction = &GS_REACTION_TABLE[r];
        int unseen = 0;
        if (reaction->has_meal)
            for (int u = 0; u < s->warning.unseen_count; u++) unseen |= s->warning.unseen[u] == anchor + reaction->meal.after;
        if (!has_since || !reaction->has_meal || anchor + reaction->meal.after > s->tick || unseen) {
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
        const GsReaction* reaction = &GS_REACTION_TABLE[live[li]];
        size_t inner = arena->used;
        GsSpecHit* hits = arena_alloc(arena, (size_t)(struck_count + 1) * sizeof(GsSpecHit));
        int count = struck_count;
        memcpy(hits, struck, (size_t)struck_count * sizeof(GsSpecHit));
        const GsIncoming* landing = NULL;
        GsIncoming thrown;
        if (s->has_incoming) {
            int delay = 0;
            if (s->incoming.attack != GS_NONE && reaction->has_meal && anchor + reaction->meal.after < s->incoming.attack)
                delay = reaction_delay(&reaction->meal);
            thrown = s->incoming;
            if (delay) {
                thrown.lands += delay;
                thrown.attack += delay;
            }
            landing = &thrown;
            if (delay == 0 || thrown.lands <= last) hits[count++] = gs_echo(arena, &thrown, echo, s->recoil_charges, s->pid);
        }
        spec_hits_sort(hits, count);
        GsSpecHeal heals[3];
        int heal_count = 0;
        if (reaction->has_meal)
            for (int f = 0; f < reaction->meal.foods; f++) {
                GsSpecFood food = reaction->meal.kinds[f];
                int at = anchor + reaction->meal.after;
                heals[heal_count++] = (GsSpecHeal){food == GS_FOOD_HARD ? imax(at, s->hard_food_ready) : at, GS_FOOD_HEALS[food], GS_FOOD_CAPS[food]};
            }
        for (int i = 1; i < heal_count; i++) {
            GsSpecHeal heal = heals[i];
            int j = i - 1;
            while (j >= 0 && heals[j].tick > heal.tick) { heals[j + 1] = heals[j]; j--; }
            heals[j + 1] = heal;
        }
        GsOutcome outcome = gs_strikes(arena, s, hits, count, heals, heal_count, landing);
        dead += reaction->share / shares * outcome.kill;
        suicide += reaction->share / shares * outcome.death;
        arena->used = inner;
    }
    arena->used = mark;
    return (GsOutcome){dead, suicide};
}

static double gs_spec_knockout(Arena* arena, const GsSpecSituation* s, const GsSpecHit* planned, int planned_count, GsEcho echo) {
    return gs_spec_outcome(arena, s, planned, planned_count, echo).kill;
}

/* AutoSpec.decide */
typedef enum { GS_CHOICE_HOLD, GS_CHOICE_VOIDWAKER, GS_CHOICE_MAUL_VOIDWAKER, GS_CHOICE_MAUL } GsChoiceKind;
static const int GS_CHOICE_TICKETS[4] = {0, 1, 2, 1};
typedef enum { GS_CAST_NOW, GS_CAST_KEEP } GsCast;
typedef struct { GsChoiceKind choice; const GsSpecHit* hits; int hit_count; } GsSpecPlan;
typedef struct { GsChoiceKind choice; GsCast cast; GsChoiceKind aim; double chance, bar, death, held, kept; } GsSpecDecision;

enum { GS_TICKET = 50, GS_MOST_TICKETS = 2 };
#define GS_WINDOW_PEAK_COUNT 128
static const double GS_WINDOW_PEAKS[GS_WINDOW_PEAK_COUNT] = {
    0.000, 0.000, 0.000, 0.000, 0.000, 0.000, 0.000, 0.000, 0.000, 0.000, 0.000, 0.000,
    0.000, 0.000, 0.000, 0.000, 0.000, 0.000, 0.000, 0.000, 0.000, 0.000, 0.000, 0.000,
    0.000, 0.000, 0.000, 0.000, 0.000, 0.000, 0.000, 0.000, 0.000, 0.000, 0.000, 0.000,
    0.000, 0.000, 0.000, 0.000, 0.000, 0.000, 0.000, 0.000, 0.000, 0.000, 0.000, 0.002,
    0.002, 0.004, 0.004, 0.005, 0.006, 0.008, 0.010, 0.016, 0.016, 0.016, 0.017, 0.021,
    0.032, 0.034, 0.034, 0.035, 0.045, 0.047, 0.048, 0.049, 0.050, 0.051, 0.054, 0.054,
    0.055, 0.057, 0.067, 0.071, 0.071, 0.072, 0.075, 0.076, 0.078, 0.087, 0.091, 0.093,
    0.095, 0.095, 0.096, 0.097, 0.098, 0.098, 0.099, 0.100, 0.110, 0.117, 0.117, 0.118,
    0.120, 0.120, 0.126, 0.141, 0.150, 0.151, 0.155, 0.163, 0.164, 0.166, 0.166, 0.166,
    0.170, 0.172, 0.188, 0.188, 0.205, 0.212, 0.218, 0.222, 0.238, 0.257, 0.273, 0.330,
    0.330, 0.344, 0.438, 0.456, 0.456, 0.521, 0.617, 0.705};
static double GS_WORTH[GS_MOST_TICKETS + 1];
#define GS_FIGHT_ENDS (21 / 206.0)

__attribute__((constructor)) static void gs_build_worth(void) {
    GS_WORTH[0] = 0;
    for (int k = 1; k <= GS_MOST_TICKETS; k++) {
        double previous;
        GS_WORTH[k] = 0;
        do {
            previous = GS_WORTH[k];
            double total = 0;
            for (int i = 0; i < GS_WINDOW_PEAK_COUNT; i++) {
                double peak = GS_WINDOW_PEAKS[i];
                total += fmax(peak + (1 - peak) * (1 - GS_FIGHT_ENDS) * GS_WORTH[k - 1], (1 - GS_FIGHT_ENDS) * GS_WORTH[k]);
            }
            GS_WORTH[k] = total / GS_WINDOW_PEAK_COUNT;
        } while (fabs(GS_WORTH[k] - previous) > 1e-12);
    }
}

/* AutoSpec.bar: the chance a plan costing cost specs must reach with energy to be played. */
static double gs_spec_held(int energy) { return (1 - GS_FIGHT_ENDS) * GS_WORTH[imin(GS_MOST_TICKETS, energy / GS_TICKET)]; }
static double gs_spec_kept(int energy, int cost) {
    int tickets = imin(GS_MOST_TICKETS, energy / GS_TICKET);
    return tickets < cost ? 0 : (1 - GS_FIGHT_ENDS) * GS_WORTH[tickets - cost];
}
static double gs_spec_bar(int energy, int cost) {
    if (imin(GS_MOST_TICKETS, energy / GS_TICKET) < cost) return INFINITY;
    double spent = gs_spec_kept(energy, cost);
    return (gs_spec_held(energy) - spent) / (1 - spent);
}

static int spec_plan_done(const GsSpecPlan* plan) {
    int done = INT_MIN;
    for (int i = 0; i < plan->hit_count; i++) done = imax(done, plan->hits[i].lands);
    return plan->hit_count ? done : INT_MAX;
}

static int spec_rebound_casts_before(const GsRebound* r, int lands, GsPid first) {
    return r->kind == GS_REBOUND_CASTABLE && (r->cast < lands || (r->cast == lands && first == GS_PID_LOCAL));
}

static GsSpecDecision gs_auto_spec_decide(Arena* arena, const GsSpecSituation* s, const GsSpecPlan* plans, int plan_count,
    const GsSpecPlan* after_cast, int after_cast_count, int energy) {
    static const GsSpecPlan nothing = {GS_CHOICE_HOLD, NULL, 0};
    GsEcho standing = s->rebound.kind == GS_REBOUND_UP ? GS_ECHO_VENGEANCE : GS_ECHO_RECOIL;
    const GsSpecPlan* best = &nothing;
    GsCast cast = GS_CAST_KEEP;
    GsOutcome chosen = {0, 0};
    for (int i = 0; i <= plan_count; i++) {
        const GsSpecPlan* plan = i < plan_count ? &plans[i] : &nothing;
        GsOutcome kept = gs_spec_outcome(arena, s, plan->hits, plan->hit_count, standing);
        if (kept.kill > chosen.kill || (kept.kill == chosen.kill && kept.kill > 0 && spec_plan_done(plan) < spec_plan_done(best))) {
            best = plan;
            chosen = kept;
        }
    }
    if (s->has_incoming && s->incoming.attack == GS_NONE && spec_rebound_casts_before(&s->rebound, s->incoming.lands, s->pid)) {
        for (int i = 0; i <= after_cast_count; i++) {
            const GsSpecPlan* plan = i < after_cast_count ? &after_cast[i] : &nothing;
            GsOutcome with_cast = gs_spec_outcome(arena, s, plan->hits, plan->hit_count, GS_ECHO_VENGEANCE);
            if (with_cast.kill > chosen.kill) {
                best = plan;
                cast = GS_CAST_NOW;
                chosen = with_cast;
            }
        }
    }
    double bar = gs_spec_bar(energy, GS_CHOICE_TICKETS[best->choice]);
    double held = gs_spec_held(energy), kept = gs_spec_kept(energy, GS_CHOICE_TICKETS[best->choice]);
    if (chosen.kill < bar) return (GsSpecDecision){GS_CHOICE_HOLD, GS_CAST_KEEP, best->choice, chosen.kill, bar, chosen.death, held, kept};
    return (GsSpecDecision){best->choice, cast, best->choice, chosen.kill, bar, chosen.death, held, kept};
}
#endif
