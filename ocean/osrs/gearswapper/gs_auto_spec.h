#ifndef GS_AUTO_SPEC_H
#define GS_AUTO_SPEC_H
#include "gs_core.h"

/* AutoSpec.java: the knockout chance of a plan against an opponent that reacts as the recorded first meals did. */
enum { GS_REACTION_TICKS = 7 };
static const double GS_FIRST_MEAL[9] = {0.01, 0.21, 0.49, 0.18, 0.03, 0.05, 0.01, 0.01, 0.01};
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

typedef struct { int lands; const double* damage; int damages; } GsSpecHit;

static GsSpecHit gs_spec_hit_of(Arena* a, int lands, int least, int most, double accuracy) {
    double* damage = arena_alloc(a, (size_t)(most + 1) * sizeof(double));
    memset(damage, 0, (size_t)(most + 1) * sizeof(double));
    damage[0] = 1 - accuracy;
    for (int roll = least; roll <= most; roll++) damage[roll] += accuracy / (most - least + 1);
    return (GsSpecHit){lands, damage, most + 1};
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
    return (GsSpecHit){incoming->lands + (pid == GS_PID_LOCAL ? 0 : 1), damage, most + 1};
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
} GsSpecSituation;

typedef struct { int tick, amount, cap; } GsSpecHeal;

static double gs_knockout_with_heals(Arena* a, const GsSpecSituation* s, const GsSpecHit* hits, int hit_count, const GsSpecHeal* heals, int heal_count) {
    int length = imax(s->hitpoints.max, GS_OVERHEALED_HP) + 1;
    double* alive = hp_uniform(a, imax(1, s->hitpoints.low), s->hitpoints.high, length);
    double dead = 0;
    int next = 0;
    for (int i = 0; i < hit_count; i++) {
        while (next < heal_count && (heals[next].tick < hits[i].lands || (heals[next].tick == hits[i].lands && s->pid != GS_PID_LOCAL))) {
            hp_heal(alive, heals[next].amount, heals[next].cap);
            next++;
        }
        dead += hp_strike_damage(a, alive, length, hits[i].damage, hits[i].damages);
    }
    return dead;
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
static double gs_spec_knockout(Arena* arena, const GsSpecSituation* s, const GsSpecHit* planned, int planned_count, GsEcho echo) {
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
    if (first == INT_MAX) { arena->used = mark; return 0; }
    int has_since = s->warning.since != GS_NONE && s->warning.since >= s->tick - GS_REACTION_TICKS;
    int anchor = has_since ? s->warning.since : first;
    int last = INT_MIN;
    for (int i = 0; i < struck_count; i++) last = imax(last, struck[i].lands);
    double shares = 0;
    int live[GS_REACTIONS], live_count = 0;
    for (int r = 0; r < GS_REACTIONS; r++) {
        const GsReaction* reaction = &GS_REACTION_TABLE[r];
        int unseen = 0;
        if (reaction->has_meal)
            for (int u = 0; u < s->warning.unseen_count; u++) unseen |= s->warning.unseen[u] == anchor + reaction->meal.after;
        if (!has_since || !reaction->has_meal || anchor + reaction->meal.after > s->tick || unseen) {
            live[live_count++] = r;
            shares += reaction->share;
        }
    }
    double dead = 0;
    for (int li = 0; li < live_count; li++) {
        const GsReaction* reaction = &GS_REACTION_TABLE[live[li]];
        size_t inner = arena->used;
        GsSpecHit* hits = arena_alloc(arena, (size_t)(struck_count + 1) * sizeof(GsSpecHit));
        int count = struck_count;
        memcpy(hits, struck, (size_t)struck_count * sizeof(GsSpecHit));
        if (s->has_incoming) {
            int delay = 0;
            if (s->incoming.attack != GS_NONE && reaction->has_meal && anchor + reaction->meal.after < s->incoming.attack)
                delay = reaction_delay(&reaction->meal);
            GsIncoming thrown = s->incoming;
            if (delay) {
                thrown.lands += delay;
                thrown.attack += delay;
            }
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
        dead += reaction->share / shares * gs_knockout_with_heals(arena, s, hits, count, heals, heal_count);
        arena->used = inner;
    }
    arena->used = mark;
    return dead;
}
#endif
