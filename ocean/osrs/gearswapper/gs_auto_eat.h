#ifndef GS_AUTO_EAT_H
#define GS_AUTO_EAT_H
#include "gs_auto_spec.h"

/* AutoEat.java: the smallest meal that keeps the opponent's knockout chance at or under a tolerance. Ticks are GameTick numbers.
 * The cost term lives in gs_auto_eat_decide (value = gain - forgone * stakes, gain priced above the tolerance), eat_delay and
 * eat_forgone_knockout. */
enum { GS_MAX_ATTACKS = 8, GS_MAX_HIDDEN = 6, GS_MAX_FOODS = 8, GS_MAX_MEAL = 4, GS_MAX_MEALS = 128, GS_MAX_HITS = 512 };
typedef enum { GS_EAT_NOW, GS_WAIT, GS_EXPOSED } GsDecision;
typedef enum { GS_ENGAGED, GS_IDLE } GsOffence;

typedef struct { GsConsumable kind; int heal, ready, remaining, drained; } GsFood;
typedef struct { int n, foods[GS_MAX_MEAL], offsets[GS_MAX_MEAL], following[GS_MAX_MEAL], earliest_process, resume, complete; } GsMeal;
typedef struct { int is_maul; GsAttack attack; double probability; } GsHidden;

typedef struct {
    int tick, hitpoints;
    GsPid pid;
    int distance;
    GsTimer opponent_timer;
    GsAttack attacks[GS_MAX_ATTACKS]; int attack_count;
    int energy_high;
    int has_maul; GsAttack maul;
    GsHidden hidden[GS_MAX_HIDDEN]; int hidden_count;
    int has_swing; GsSwing swing;
    GsVengeance vengeance;
    int recoil_seen;
    GsTimer local_timer;
    GsStyle local_style;
    int local_most, local_normal, local_interval;
    double local_opening_accuracy, local_accuracy;
    int has_local_swing; GsSwing local_swing;
    GsHealthRange opponent_hp;
    GsOffence offence;
    double tolerance, pid_flip;
    int drained_normal, drained_most;
    double drained_accuracy, drained_opening_accuracy;
    GsWarning warning;
    double stakes;
    int floor;
} GsEatSituation;

typedef enum { GS_URGENT, GS_DEFERRABLE } GsUrgency;
typedef struct { GsDecision decision; double risk, meal_risk; int foods[GS_MAX_MEAL], food_count, complete; GsUrgency urgency; GsHealth health; } GsVerdict;

typedef enum { SRC_SWING, SRC_NORMAL, SRC_SPEC, SRC_GODSWORD_SPEC, SRC_MAUL, SRC_VENGEANCE_ECHO, SRC_RECOIL_ECHO } GsSource;
typedef struct { GsSource source; int tick, least, most; double accuracy; } GsHit;
typedef struct { GsHit* hits; int count; } GsScenario;
typedef struct { int tick, amount, ceiling; } GsHeal;
static int gs_ceiling(const GsFood* food) { return GS_ASSUMED_MAX_HP + (food->drained ? food->heal : 0); }
typedef struct { int tick, resume, delay, earliest_resume, restored_at; } GsConsumptionAt;
typedef struct { double risk; int useful, waste; } GsEvaluation;
typedef enum { GS_SCHEDULE_ANY, GS_SCHEDULE_EARLIEST } GsSchedule;
typedef enum { GS_RESUME_LATEST, GS_RESUME_EARLIEST } GsResumeTiming;

typedef struct {
    GsVec heals, consumptions;
    int ready[GS_CONSUMABLES];
    int remaining[GS_MAX_FOODS];
    int next;
} GsPlan;

typedef struct { GsVec heals, consumptions, follows; } GsBranch;

typedef struct { const GsFood* foods; int food_count; const GsMeal* meals; int meal_count; Arena* arena; } GsEatCall;

static int eat_next_local(const GsEatSituation* s) { return s->local_timer.seen ? s->local_timer.next : s->tick + 1; }
static int eat_stored_local(const GsEatSituation* s) { return s->local_timer.seen ? s->local_timer.earliest : s->tick + 1; }
static int eat_next_opponent(const GsEatSituation* s) { return s->opponent_timer.seen ? s->opponent_timer.next : s->tick + 1; }

static int eat_ordered(const GsMeal* meal, const GsFood* foods) {
    int consumed[GS_CONSUMABLES] = {0};
    for (int i = 0; i < meal->n; i++) {
        GsConsumable kind = foods[meal->foods[i]].kind;
        for (int previous = 0; previous < GS_CONSUMABLES; previous++)
            if (consumed[previous] && consumable_ready_after(kind, (GsConsumable)previous, 0, INT_MIN) > 0) return 0;
        consumed[kind] = 1;
    }
    return 1;
}

static GsConsumption eat_meal_consumption(const GsFood* foods, const int* selected, int from, int to) {
    int has[GS_CONSUMABLES] = {0};
    for (int i = from; i < to; i++) has[foods[selected[i]].kind] = 1;
    return consumption_of(has[GS_POTION], has[GS_FAST_FOOD], has[GS_HARD_FOOD]);
}

static void eat_consumptions(Arena* arena, GsVec* out, const GsFood* foods, const GsMeal* meal, const int* offsets, int dispatch, int resume) {
    int next = 0;
    while (next < meal->n) {
        int at = offsets[next];
        int from = next, drained = 0;
        do { drained |= foods[meal->foods[next]].drained; next++; } while (next < meal->n && offsets[next] == at);
        GsConsumptionAt* c = vec_push(out);
        c->tick = dispatch + at;
        c->resume = next == meal->n ? resume : GS_NONE;
        c->delay = consumption_delay(eat_meal_consumption(foods, meal->foods, from, next));
        c->earliest_resume = resume;
        c->restored_at = drained ? consumable_ready_after(GS_POTION, GS_POTION, dispatch + at, GS_NONE) : GS_NONE;
    }
    (void)arena;
}

static GsPlan plan_eat(const GsEatCall* call, const GsPlan* plan, const GsMeal* meal, const int* offsets, int dispatch, int resume, int following) {
    GsPlan result;
    result.heals = vec_copy(call->arena, &plan->heals);
    result.consumptions = vec_copy(call->arena, &plan->consumptions);
    memcpy(result.ready, plan->ready, sizeof(result.ready));
    memcpy(result.remaining, plan->remaining, sizeof(result.remaining));
    for (int i = 0; i < meal->n; i++) {
        int index = meal->foods[i];
        const GsFood* food = &call->foods[index];
        int at = dispatch + offsets[i];
        GsHeal* heal = vec_push(&result.heals);
        heal->tick = at;
        heal->amount = food->heal;
        heal->ceiling = gs_ceiling(food);
        result.remaining[index]--;
        for (int kind = 0; kind < GS_CONSUMABLES; kind++) result.ready[kind] = consumable_ready_after((GsConsumable)kind, food->kind, at, result.ready[kind]);
    }
    eat_consumptions(call->arena, &result.consumptions, call->foods, meal, offsets, dispatch, resume);
    result.next = following;
    return result;
}

static int plan_dispatch(const GsEatCall* call, const GsPlan* plan, const GsMeal* meal, int* out) {
    if (!eat_ordered(meal, call->foods)) return 0;
    int dispatch = plan->next;
    for (int i = 0; i < meal->n; i++) {
        int index = meal->foods[i];
        if (plan->remaining[index] <= 0) return 0;
        long ready = (long)plan->ready[call->foods[index].kind] - 1;
        if (ready > dispatch) dispatch = (int)ready;
    }
    *out = dispatch;
    return 1;
}

static void plan_followed(const GsEatCall* call, const GsPlan* plan, GsVec* plans) {
    for (int m = 0; m < call->meal_count; m++) {
        const GsMeal* meal = &call->meals[m];
        int dispatch;
        if (!plan_dispatch(call, plan, meal, &dispatch)) continue;
        *(GsPlan*)vec_push(plans) = plan_eat(call, plan, meal, meal->following, dispatch, dispatch + meal->following[meal->n - 1], dispatch + imax(1, meal->complete));
    }
}

static int plan_last_heal(const GsPlan* plan) { return VEC_AT(GsHeal, plan->heals, plan->heals.count - 1).tick; }

static int branch_last(const GsBranch* branch) {
    int last = INT_MIN;
    for (int i = 0; i < branch->follows.count; i++) last = imax(last, plan_last_heal(&VEC_AT(GsPlan, branch->follows, i)));
    return last;
}

static GsBranch eat_branch(const GsEatCall* call, const GsEatSituation* s, const GsMeal* current, int window) {
    GsPlan start = {vec_make(call->arena, sizeof(GsHeal)), vec_make(call->arena, sizeof(GsConsumptionAt)), {0}, {0}, s->tick + 1};
    for (int k = 0; k < GS_CONSUMABLES; k++) start.ready[k] = INT_MIN;
    for (int i = 0; i < call->food_count; i++) {
        start.ready[call->foods[i].kind] = imax(start.ready[call->foods[i].kind], call->foods[i].ready);
        start.remaining[i] = call->foods[i].remaining;
    }
    GsBranch branch = {start.heals, start.consumptions, vec_make(call->arena, sizeof(GsPlan))};
    if (current) {
        GsPlan committed = plan_eat(call, &start, current, current->offsets, s->tick, s->tick + current->resume, s->tick + imax(1, current->complete));
        branch.heals = committed.heals;
        branch.consumptions = committed.consumptions;
        plan_followed(call, &committed, &branch.follows);
        return branch;
    }
    for (int m = 0; m < call->meal_count; m++) {
        const GsMeal* meal = &call->meals[m];
        int dispatch;
        if (!plan_dispatch(call, &start, meal, &dispatch)) continue;
        GsPlan first = plan_eat(call, &start, meal, meal->offsets, dispatch, dispatch + meal->offsets[meal->n - 1], dispatch + imax(1, meal->complete));
        GsVec followed = vec_make(call->arena, sizeof(GsPlan));
        plan_followed(call, &first, &followed);
        int kept = 0;
        for (int i = 0; i < followed.count; i++)
            if (plan_last_heal(&VEC_AT(GsPlan, followed, i)) <= window) { *(GsPlan*)vec_push(&branch.follows) = VEC_AT(GsPlan, followed, i); kept++; }
        if (!kept) *(GsPlan*)vec_push(&branch.follows) = first;
    }
    return branch;
}

static int eat_own_lands(const GsEatSituation* s, int at) { return gs_lands(at, s->local_style, s->distance, gs_local_lead(s->pid)); }

static GsSwing eat_own(const GsEatSituation* s, int attack, int opening, int drained) {
    int most = drained ? opening ? s->drained_most : s->drained_normal : opening ? s->local_most : s->local_normal;
    double accuracy = drained ? opening ? s->drained_opening_accuracy : s->drained_accuracy : opening ? s->local_opening_accuracy : s->local_accuracy;
    return (GsSwing){eat_own_lands(s, attack), 0, most, accuracy, 1};
}

/* AutoEat.ownSwings */
static GsVec eat_own_swings(Arena* arena, const GsEatSituation* s, const GsVec* consumptions, int horizon, GsResumeTiming timing) {
    GsVec swings = vec_make(arena, sizeof(GsSwing));
    if (s->has_local_swing) *(GsSwing*)vec_push(&swings) = s->local_swing;
    int next = eat_stored_local(s);
    GsOffence offence = s->offence;
    int opening = 1, restored_at = GS_NONE;
    for (int i = 0; i < consumptions->count; i++) {
        const GsConsumptionAt* c = &VEC_AT(GsConsumptionAt, *consumptions, i);
        int at = timing == GS_RESUME_EARLIEST ? imin(c->tick, c->earliest_resume) : c->tick;
        while (offence == GS_ENGAGED && imax(next, s->tick + 1) < at) {
            next = imax(next, s->tick + 1);
            *(GsSwing*)vec_push(&swings) = eat_own(s, next, opening, next < restored_at);
            opening = 0;
            next += s->local_interval;
        }
        next += c->delay;
        offence = GS_IDLE;
        restored_at = imax(restored_at, c->restored_at);
        if (c->resume != GS_NONE) {
            next = imax(next, c->resume);
            offence = GS_ENGAGED;
        }
    }
    next = imax(next, s->tick + 1);
    while (offence == GS_ENGAGED && next <= horizon) {
        *(GsSwing*)vec_push(&swings) = eat_own(s, next, opening, next < restored_at);
        opening = 0;
        next += s->local_interval;
    }
    for (int i = 1; i < swings.count; i++) {
        GsSwing swing = VEC_AT(GsSwing, swings, i);
        int j = i - 1;
        while (j >= 0 && VEC_AT(GsSwing, swings, j).lands > swing.lands) { VEC_AT(GsSwing, swings, j + 1) = VEC_AT(GsSwing, swings, j); j--; }
        VEC_AT(GsSwing, swings, j + 1) = swing;
    }
    return swings;
}

/* AutoEat.echoes: alternatives of reflected hits, one scenario each. */
static GsVec eat_echoes(Arena* arena, const GsEatSituation* s, const GsVec* consumptions, int horizon) {
    GsVec alternatives = vec_make(arena, sizeof(GsScenario));
    int echo_delay = gs_opponent_lead(s->pid) == GS_LEAD_ATTACKER ? 0 : 1;
    for (int timing = 0; timing < 2; timing++) {
        GsVec swings = eat_own_swings(arena, s, consumptions, horizon, (GsResumeTiming)timing);
        for (int first = s->vengeance == GS_VENGEANCE_UP ? 0 : swings.count; first <= swings.count; first++) {
            GsHit* reflected = arena_alloc(arena, (size_t)(2 * swings.count + 1) * sizeof(GsHit));
            int count = 0;
            for (int i = 0; i < swings.count; i++) {
                const GsSwing* swing = &VEC_AT(GsSwing, swings, i);
                int dealt = imin(swing->most, s->opponent_hp.high);
                if (i == first) reflected[count++] = (GsHit){SRC_VENGEANCE_ECHO, swing->lands + echo_delay, 0, swing->most * 3 / 4, swing->accuracy};
                if (s->recoil_seen && dealt > 0) reflected[count++] = (GsHit){SRC_RECOIL_ECHO, swing->lands + echo_delay, 0, dealt / 10 + 1, swing->accuracy};
            }
            *(GsScenario*)vec_push(&alternatives) = (GsScenario){reflected, count};
        }
    }
    return alternatives;
}

static void eat_sort_hits(GsHit* hits, int count) {
    for (int i = 1; i < count; i++) {
        GsHit hit = hits[i];
        int j = i - 1;
        while (j >= 0 && hits[j].tick > hit.tick) { hits[j + 1] = hits[j]; j--; }
        hits[j + 1] = hit;
    }
}

/* AutoEat.odds */
static double eat_stream_sum(const double* values, int count) {
    double sum = 0, compensation = 0;
    for (int i = 0; i < count; i++) {
        double tmp = values[i] - compensation;
        double velvel = sum + tmp;
        compensation = (velvel - sum) - tmp;
        sum = velvel;
    }
    return sum - compensation;
}

/* AutoEat.odds: hits that are unavoidable (before the floor, or on it without our PID) deal their damage and give no death credit, the survivors are renormalised. */
static GsEvaluation eat_odds(Arena* arena, GsHit* hits, int hit_count, const GsVec* heals, int counted, int hitpoints, GsPid pid, int floor_tick) {
    size_t mark = arena->used;
    eat_sort_hits(hits, hit_count);
    int top = GS_ASSUMED_MAX_HP;
    if (heals->count) {
        top = INT_MIN;
        for (int i = 0; i < heals->count; i++) top = imax(top, VEC_AT(GsHeal, *heals, i).ceiling);
    }
    int length = imax(hitpoints, top) + 1;
    double* alive = arena_alloc(arena, (size_t)length * sizeof(double));
    memset(alive, 0, (size_t)length * sizeof(double));
    alive[hitpoints] = 1;
    double dead = 0;
    int expected = hitpoints, useful = 0, waste = 0, next = 0;
    for (int i = 0; i < hit_count; i++) {
        const GsHit* hit = &hits[i];
        while (next < heals->count && gs_beats(VEC_AT(GsHeal, *heals, next).tick, hit->tick, pid)) {
            int amount = VEC_AT(GsHeal, *heals, next).amount, ceiling = VEC_AT(GsHeal, *heals, next).ceiling;
            int added = imax(0, imin(ceiling - expected, amount));
            expected += added;
            if (next < counted) {
                useful += added;
                waste += amount - added;
            }
            hp_heal(alive, amount, ceiling);
            next++;
        }
        double killed = hp_strike_range(arena, alive, length, hit->least, hit->most, hit->accuracy);
        if (hit->tick < floor_tick || (hit->tick == floor_tick && pid != GS_PID_LOCAL)) {
            double living = eat_stream_sum(alive, length);
            for (int hp = 1; living > 0 && hp < length; hp++) alive[hp] /= living;
        } else dead += killed;
        expected -= (int)floor(hit->accuracy * (hit->least + hit->most) / 2 + 0.5);
    }
    while (next < counted) {
        int amount = VEC_AT(GsHeal, *heals, next).amount, ceiling = VEC_AT(GsHeal, *heals, next).ceiling;
        int added = imax(0, imin(ceiling - expected, amount));
        expected += added;
        useful += added;
        waste += amount - added;
        next++;
    }
    arena->used = mark;
    return (GsEvaluation){dead, useful, waste};
}

typedef enum { COMBO_NONE, COMBO_SPEC_THEN_MAUL, COMBO_MAUL_THEN_SPEC, COMBO_SPEC_THEN_SPEC, COMBO_GODSWORD_THEN_MAUL, COMBOS } GsCombo;
static const double GS_COMBO_EXECUTED[COMBOS] = {1, 27 / 314.0, 21 / 219.0, 38 / 314.0, 12 / 50.0};

static GsCombo eat_combo_of(const GsHit* hits, int count) {
    for (int m = 0; m < count; m++) {
        if (hits[m].source != SRC_MAUL) continue;
        for (int p = 0; p < count; p++)
            if ((hits[p].source == SRC_SPEC || hits[p].source == SRC_GODSWORD_SPEC) && abs(hits[p].tick - hits[m].tick) <= 1) {
                if (hits[p].tick > hits[m].tick) return COMBO_MAUL_THEN_SPEC;
                return hits[p].source == SRC_GODSWORD_SPEC ? COMBO_GODSWORD_THEN_MAUL : COMBO_SPEC_THEN_MAUL;
            }
    }
    int specs = 0;
    for (int i = 0; i < count; i++) specs += hits[i].source == SRC_SPEC || hits[i].source == SRC_GODSWORD_SPEC;
    return specs >= 2 ? COMBO_SPEC_THEN_SPEC : COMBO_NONE;
}

/* AutoEat.danger */
static GsEvaluation eat_danger(Arena* arena, const GsEatSituation* s, const GsVec* heals, int counted, int horizon, const GsVec* consumptions,
    const GsVec* scenarios, double bound) {
    size_t mark = arena->used;
    GsVec echoes = eat_echoes(arena, s, consumptions, horizon);
    double risks[COMBOS] = {0};
    int useful = 0, waste = 0;
    GsHit* hits = arena_alloc(arena, GS_MAX_HITS * sizeof(GsHit));
    for (int si = 0; si < scenarios->count; si++) {
        const GsScenario* scenario = &VEC_AT(GsScenario, *scenarios, si);
        for (int ei = 0; ei < echoes.count; ei++) {
            const GsScenario* reflected = &VEC_AT(GsScenario, echoes, ei);
            int count = 0;
            for (int i = 0; i < scenario->count; i++) hits[count++] = scenario->hits[i];
            for (int i = 0; i < reflected->count; i++) hits[count++] = reflected->hits[i];
            assert(count <= GS_MAX_HITS);
            int kept = 0;
            for (int i = 0; i < count; i++) {
                if (hits[i].tick > horizon) continue;
                hits[kept++] = hits[i];
            }
            GsEvaluation evaluation = eat_odds(arena, hits, kept, heals, counted, s->hitpoints, s->pid, s->floor);
            GsHit avoidable[GS_MAX_HITS];
            int avoidable_count = 0;
            for (int i = 0; i < kept; i++)
                if (!(hits[i].tick < s->floor || (hits[i].tick == s->floor && s->pid != GS_PID_LOCAL))) avoidable[avoidable_count++] = hits[i];
            GsCombo combo = eat_combo_of(avoidable, avoidable_count);
            if (evaluation.risk > risks[combo]) {
                risks[combo] = evaluation.risk;
                if (combo == COMBO_NONE && evaluation.risk > bound) { arena->used = mark; return (GsEvaluation){INFINITY, 0, 0}; }
            }
            useful = imax(useful, evaluation.useful);
            waste = imax(waste, evaluation.waste);
        }
    }
    double simple = risks[COMBO_NONE];
    double risk = simple;
    for (int combo = COMBO_SPEC_THEN_MAUL; combo < COMBOS; combo++) risk += GS_COMBO_EXECUTED[combo] * fmax(0, risks[combo] - simple);
    arena->used = mark;
    return (GsEvaluation){risk, useful, waste};
}

static int set_insert(int* set, int count, int value) {
    int at = 0;
    while (at < count && set[at] < value) at++;
    if (at < count && set[at] == value) return count;
    for (int i = count; i > at; i--) set[i] = set[i - 1];
    set[at] = value;
    return count + 1;
}

static int eat_volley(GsHit* hits, int count, GsSource source, int lands, int arrows, int least, int most, double accuracy) {
    for (int arrow = 0; arrow < arrows; arrow++) hits[count++] = (GsHit){source, lands + arrow, least, most, accuracy};
    return count;
}

static void eat_regulars(Arena* arena, const GsEatSituation* s, int due, int energy, const GsHit* hits, int hit_count, const int* mauled, int mauled_count,
    int horizon, GsSchedule schedule, const GsVec* heals, GsVec* scenarios) {
    GsScenario* self = vec_push(scenarios);
    self->hits = arena_alloc(arena, (size_t)(hit_count + 1) * sizeof(GsHit));
    memcpy(self->hits, hits, (size_t)hit_count * sizeof(GsHit));
    self->count = hit_count;
    for (int a = 0; a < s->attack_count; a++) {
        const GsAttack* attack = &s->attacks[a];
        if (attack->cost > energy || attack->most == 0) continue;
        size_t mark = arena->used;
        int cap = imax(horizon - due + 2, 0) + 2 * heals->count + 4;
        int* starts = arena_alloc(arena, (size_t)cap * sizeof(int));
        int start_count = set_insert(starts, 0, due);
        if (schedule == GS_SCHEDULE_ANY) {
            for (int started = due + 1; started <= horizon; started++) start_count = set_insert(starts, start_count, started);
        } else {
            int delay = gs_lands(0, attack->style, s->distance, gs_opponent_lead(s->pid));
            for (int h = 0; h < heals->count; h++) {
                int tick = VEC_AT(GsHeal, *heals, h).tick;
                start_count = set_insert(starts, start_count, imax(due, tick - delay));
                start_count = set_insert(starts, start_count, imax(due, tick + 1 - delay));
            }
        }
        for (int i = 0; i < start_count; i++) {
            int started = starts[i];
            int at = gs_lands(started, attack->style, s->distance, gs_opponent_lead(s->pid));
            if (at > horizon) break;
            int skip = 0;
            for (int m = 0; m < mauled_count; m++) skip |= mauled[m] == started;
            if (skip) continue;
            GsHit next[GS_MAX_HITS];
            assert(hit_count + attack->arrows <= GS_MAX_HITS);
            memcpy(next, hits, (size_t)hit_count * sizeof(GsHit));
            int next_count = eat_volley(next, hit_count, attack->cost == 0 ? SRC_NORMAL : attack->godsword ? SRC_GODSWORD_SPEC : SRC_SPEC, at, attack->arrows, attack->least, attack->most, attack->accuracy);
            eat_regulars(arena, s, started + attack->interval, energy - attack->cost, next, next_count, mauled, mauled_count, horizon, schedule, heals, scenarios);
        }
        (void)mark;
    }
}

static GsVec eat_scenarios(Arena* arena, const GsEatSituation* s, int horizon, GsSchedule schedule, const GsVec* heals) {
    int due = eat_next_opponent(s);
    int placements[512];
    int placement_count = set_insert(placements, 0, s->tick + 1);
    if (schedule == GS_SCHEDULE_ANY) {
        for (int at = s->tick + 2; at <= horizon; at++) placement_count = set_insert(placements, placement_count, at);
    } else {
        for (int h = 0; h < heals->count; h++) {
            int tick = VEC_AT(GsHeal, *heals, h).tick;
            placement_count = set_insert(placements, placement_count, imax(s->tick + 1, tick));
            placement_count = set_insert(placements, placement_count, imax(s->tick + 1, tick + 1));
        }
    }
    assert(placement_count < 512);
    GsHit committed[GS_MAX_HITS];
    int committed_count = 0;
    if (s->has_swing) committed_count = eat_volley(committed, committed_count, SRC_SWING, s->swing.lands, s->swing.arrows, s->swing.least, s->swing.most, s->swing.accuracy);
    GsVec scenarios = vec_make(arena, sizeof(GsScenario));
    eat_regulars(arena, s, due, s->energy_high, committed, committed_count, NULL, 0, horizon, schedule, heals, &scenarios);
    if (!s->has_maul) return scenarios;
    const GsAttack* maul = &s->maul;
    int mauls = s->energy_high / maul->cost;
    for (int fi = 0; fi < placement_count; fi++) {
        int first = placements[fi];
        if (mauls < 1) break;
        GsHit one[GS_MAX_HITS];
        memcpy(one, committed, (size_t)committed_count * sizeof(GsHit));
        int one_count = committed_count;
        one[one_count++] = (GsHit){SRC_MAUL, gs_lands(first, GS_MELEE, s->distance, gs_opponent_lead(s->pid)), 0, maul->most, maul->accuracy};
        int mauled1[1] = {first};
        eat_regulars(arena, s, due, s->energy_high - maul->cost, one, one_count, mauled1, 1, horizon, schedule, heals, &scenarios);
        for (int si = 0; si < placement_count; si++) {
            int second = placements[si];
            if (mauls < 2 || second < first) continue;
            GsHit two[GS_MAX_HITS];
            memcpy(two, one, (size_t)one_count * sizeof(GsHit));
            int two_count = one_count;
            two[two_count++] = (GsHit){SRC_MAUL, gs_lands(second, GS_MELEE, s->distance, gs_opponent_lead(s->pid)), 0, maul->most, maul->accuracy};
            int mauled2[2] = {first, second};
            eat_regulars(arena, s, due, s->energy_high - 2 * maul->cost, two, two_count, mauled2, 2, horizon, schedule, heals, &scenarios);
        }
    }
    return scenarios;
}

/* AutoEat.after */
static GsEvaluation eat_after(const GsEatCall* call, const GsEatSituation* s, const GsMeal* current, int window) {
    Arena* arena = call->arena;
    size_t mark = arena->used;
    GsBranch branch = eat_branch(call, s, current, window);
    GsEvaluation result;
    if (branch.follows.count == 0) {
        int strongest = 0, interval = 1, interval_set = 0;
        for (int i = 0; i < s->attack_count; i++) {
            if (s->attacks[i].cost == 0) strongest = imax(strongest, s->attacks[i].most * s->attacks[i].arrows);
            interval = interval_set ? imax(interval, s->attacks[i].interval) : s->attacks[i].interval;
            interval_set = 1;
        }
        if (!interval_set) interval = 1;
        int total = s->hitpoints;
        for (int i = 0; i < branch.heals.count; i++) total += VEC_AT(GsHeal, branch.heals, i).amount;
        int attacks = strongest > 0 ? (total + strongest - 1) / strongest : s->energy_high;
        int due = eat_next_opponent(s);
        int travel = INT_MIN;
        for (int i = 0; i < s->attack_count; i++) travel = imax(travel, gs_lands(0, s->attacks[i].style, s->distance, gs_opponent_lead(s->pid)));
        if (s->attack_count == 0) travel = gs_lands(0, GS_RANGED, s->distance, GS_LEAD_ATTACKER);
        int horizon = due + attacks * interval + travel;
        if (strongest == 0) {
            horizon = imax(horizon, s->has_swing ? s->swing.lands + s->swing.arrows - 1 : s->tick);
            horizon = imax(horizon, s->has_local_swing ? s->local_swing.lands + 1 : s->tick);
            if (s->offence == GS_ENGAGED || branch.consumptions.count > 0) {
                int next = eat_next_local(s);
                int reflected = imin(s->local_normal, s->opponent_hp.high);
                int echoes = s->recoil_seen && reflected > 0 ? (total + reflected / 10) / (reflected / 10 + 1) : 1;
                int delayed = 0;
                for (int i = 0; i < branch.consumptions.count; i++) delayed += VEC_AT(GsConsumptionAt, branch.consumptions, i).delay;
                horizon = imax(horizon, next + delayed + echoes * s->local_interval + gs_lands(0, s->local_style, s->distance, gs_local_lead(s->pid)) + 1);
                for (int i = 0; i < branch.consumptions.count; i++) {
                    const GsConsumptionAt* c = &VEC_AT(GsConsumptionAt, branch.consumptions, i);
                    if (c->resume != GS_NONE)
                        horizon = imax(horizon, c->resume + delayed + echoes * s->local_interval + gs_lands(0, s->local_style, s->distance, gs_local_lead(s->pid)) + 1);
                }
            }
        }
        int reach = imax(horizon, window);
        GsVec scenarios = eat_scenarios(arena, s, reach, GS_SCHEDULE_EARLIEST, &branch.heals);
        result = eat_danger(arena, s, &branch.heals, branch.heals.count, reach, &branch.consumptions, &scenarios, INFINITY);
        arena->used = mark;
        return result;
    }
    GsVec everything = eat_scenarios(arena, s, window, GS_SCHEDULE_ANY, &(GsVec){0, 0, 0, sizeof(GsHeal), arena});
    int count = branch.follows.count;
    int order[256];
    assert(count <= 256);
    int sums[256];
    for (int i = 0; i < count; i++) {
        order[i] = i;
        sums[i] = 0;
        const GsPlan* plan = &VEC_AT(GsPlan, branch.follows, i);
        for (int h = 0; h < plan->heals.count; h++) sums[i] += VEC_AT(GsHeal, plan->heals, h).amount;
    }
    for (int i = 1; i < count; i++) {
        int item = order[i];
        int j = i - 1;
        while (j >= 0 && -sums[order[j]] > -sums[item]) { order[j + 1] = order[j]; j--; }
        order[j + 1] = item;
    }
    double best = INFINITY;
    GsEvaluation chosen = {0, 0, 0};
    int have = 0;
    for (int i = 0; i < count; i++) {
        const GsPlan* follow = &VEC_AT(GsPlan, branch.follows, order[i]);
        GsEvaluation evaluation = eat_danger(arena, s, &follow->heals, branch.heals.count, window, &follow->consumptions, &everything, best);
        best = fmin(best, evaluation.risk);
        int better = !have || evaluation.risk < chosen.risk || (evaluation.risk == chosen.risk &&
            (-evaluation.useful < -chosen.useful || (evaluation.useful == chosen.useful && evaluation.waste < chosen.waste)));
        if (better) { chosen = evaluation; have = 1; }
    }
    arena->used = mark;
    return chosen;
}

/* AutoEat.mixed */
static GsEvaluation eat_mixed(const GsEatCall* call, const GsEatSituation* s, const GsMeal* current, int window, int answerable) {
    if (s->hidden_count == 0 && s->pid_flip == 0) return eat_after(call, s, current, window);
    double risk = 0;
    int useful = 0, waste = 0;
    int owners = s->pid_flip > 0 ? 2 : 1;
    for (int o = 0; o < owners; o++) {
        GsPid owner = o == 0 ? s->pid : s->pid == GS_PID_LOCAL ? GS_PID_OPPONENT : GS_PID_LOCAL;
        double owner_weight = o == 0 ? 1 - s->pid_flip : s->pid_flip;
        for (int mask = 0; mask < 1 << s->hidden_count; mask++) {
            double weight = owner_weight;
            GsEatSituation variant = *s;
            variant.pid = owner;
            variant.hidden_count = 0;
            for (int i = 0; i < s->hidden_count; i++) {
                const GsHidden* hidden = &s->hidden[i];
                if ((mask >> i & 1) == 0) {
                    weight *= 1 - hidden->probability;
                } else {
                    weight *= hidden->probability;
                    if (hidden->is_maul) { variant.has_maul = 1; variant.maul = hidden->attack; }
                    else { assert(variant.attack_count < GS_MAX_ATTACKS); variant.attacks[variant.attack_count++] = hidden->attack; }
                }
            }
            GsEvaluation evaluation = eat_after(call, &variant, current, mask == 0 ? window : imin(window, answerable));
            risk += weight * evaluation.risk;
            useful = imax(useful, evaluation.useful);
            waste = imax(waste, evaluation.waste);
        }
    }
    return (GsEvaluation){risk, useful, waste};
}

static int eat_delay(const GsEatSituation* s, const GsMeal* meal, const GsFood* foods) {
    int has[GS_CONSUMABLES] = {0};
    for (int i = 0; i < meal->n; i++) has[foods[meal->foods[i]].kind] = 1;
    GsConsumption consumption = consumption_of(has[GS_POTION], has[GS_FAST_FOOD], has[GS_HARD_FOOD]);
    return imax(0, consumption_delay(consumption) - imax(0, s->tick + meal->offsets[0] - eat_stored_local(s)));
}

static GsSpecSituation eat_hunt(const GsEatSituation* s) {
    return (GsSpecSituation){s->opponent_hp, NULL, 0, GS_NONE, s->vengeance, s->pid, 0, {0}, 0, s->tick, s->warning, {GS_REBOUND_SPENT, GS_NONE}, gs_health_of(s->hitpoints)};
}

static double eat_knockout(const GsEatCall* call, const GsEatSituation* s, const GsSpecSituation* hunt, const GsVec* consumptions, int until) {
    Arena* arena = call->arena;
    size_t mark = arena->used;
    GsVec swings = eat_own_swings(arena, s, consumptions, until, GS_RESUME_LATEST);
    GsSpecHit* hits = arena_alloc(arena, (size_t)(swings.count + 1) * sizeof(GsSpecHit));
    for (int i = 0; i < swings.count; i++) {
        const GsSwing* swing = &VEC_AT(GsSwing, swings, i);
        hits[i] = gs_spec_hit_of(arena, swing->lands, swing->least, swing->most, swing->accuracy);
    }
    double chance = gs_spec_knockout(arena, hunt, hits, swings.count, GS_ECHO_RECOIL);
    arena->used = mark;
    return chance;
}

static double eat_forgone_knockout(const GsEatCall* call, const GsEatSituation* s, const GsMeal* meal, int delay, double waiting_risk, double meal_risk) {
    int resume = s->tick + meal->resume;
    GsVec none = vec_make(call->arena, sizeof(GsConsumptionAt));
    GsVec eaten = vec_make(call->arena, sizeof(GsConsumptionAt));
    eat_consumptions(call->arena, &eaten, call->foods, meal, meal->offsets, s->tick, resume);
    int restored = GS_NONE;
    for (int i = 0; i < eaten.count; i++) restored = imax(restored, VEC_AT(GsConsumptionAt, eaten, i).restored_at);
    int until = imax(imax(resume, eat_next_local(s) + delay), restored);
    GsSpecSituation hunt = eat_hunt(s);
    return eat_knockout(call, s, &hunt, &none, until) * (1 - waiting_risk) - eat_knockout(call, s, &hunt, &eaten, until) * (1 - meal_risk);
}

typedef struct { const GsMeal* meal; GsEvaluation evaluation; double gain, value; int size, non_hard, brew, fast_food, heal; } GsChoice;

/* Double.compare of the negated values, the first key of the Java comparator. */
static int value_order(double a, double b) {
    if (a < b) return -1;
    if (a > b) return 1;
    return signbit(a) == signbit(b) ? 0 : signbit(a) ? -1 : 1;
}

static int choice_less(const GsChoice* a, const GsChoice* b) {
    int order = value_order(-a->value, -b->value);
    if (order) return order < 0;
    if (a->size != b->size) return a->size < b->size;
    if (a->non_hard != b->non_hard) return a->non_hard < b->non_hard;
    if (a->brew != b->brew) return a->brew < b->brew;
    if (a->fast_food != b->fast_food) return a->fast_food < b->fast_food;
    if (a->heal != b->heal) return a->heal < b->heal;
    return a->evaluation.waste < b->evaluation.waste;
}

static GsMeal eat_meal_later(const GsMeal* meal) {
    GsMeal later = *meal;
    for (int i = 0; i < meal->n; i++) {
        later.offsets[i]++;
        later.following[i]++;
    }
    later.earliest_process++;
    later.resume++;
    later.complete++;
    return later;
}

static GsEatSituation eat_late(const GsEatSituation* s) {
    GsEatSituation late = *s;
    late.floor = s->tick + 1;
    return late;
}

/* AutoEat.decide, on a call whose arena the caller resets between decisions. */
static GsVerdict gs_auto_eat_decide(const GsEatSituation* s, const GsFood* foods, int food_count, const GsMeal* meals, int meal_count, Arena* arena) {
    GsEatCall call = {foods, food_count, meals, meal_count, arena};
    size_t base = arena->used;
    const GsMeal* legal[GS_MAX_MEALS];
    int legal_count = 0;
    for (int m = 0; m < meal_count; m++) {
        int eatable = eat_ordered(&meals[m], foods);
        int helps = 0;
        for (int i = 0; i < meals[m].n; i++) helps |= foods[meals[m].foods[i]].heal > 0 && s->hitpoints < gs_ceiling(&foods[meals[m].foods[i]]);
        eatable &= helps;
        for (int i = 0; i < meals[m].n; i++) {
            const GsFood* food = &foods[meals[m].foods[i]];
            eatable &= food->remaining > 0 && s->tick + meals[m].earliest_process >= food->ready;
        }
        if (eatable) legal[legal_count++] = &meals[m];
    }
    int answerable;
    {
        size_t mark = arena->used;
        GsBranch branch = eat_branch(&call, s, NULL, INT_MIN);
        answerable = branch_last(&branch);
        arena->used = mark;
    }
    int window = answerable;
    for (int l = 0; l < legal_count; l++) {
        size_t mark = arena->used;
        GsBranch branch = eat_branch(&call, s, legal[l], INT_MIN);
        window = imax(window, branch_last(&branch));
        arena->used = mark;
    }
    GsEvaluation waiting = eat_mixed(&call, s, NULL, window, answerable);
    GsVerdict verdict = {GS_WAIT, waiting.risk, waiting.risk, {0}, 0, s->tick, GS_URGENT, gs_health_of(s->hitpoints)};
    if (waiting.risk <= s->tolerance) { arena->used = base; return verdict; }
    GsEatSituation late = eat_late(s);
    double avoidable = eat_mixed(&call, &late, NULL, window, answerable).risk;
    GsChoice choices[GS_MAX_MEALS];
    int choice_count = 0;
    for (int l = 0; l < legal_count; l++) {
        const GsMeal* meal = legal[l];
        GsEvaluation evaluation = eat_mixed(&call, s, meal, window, answerable);
        if (evaluation.useful == 0) continue;
        double gain = avoidable - eat_mixed(&call, &late, meal, window, answerable).risk;
        int delay = eat_delay(s, meal, foods);
        int drain = 0;
        for (int i = 0; i < meal->n; i++) drain |= foods[meal->foods[i]].drained;
        double forgone = delay == 0 && !drain ? 0 : eat_forgone_knockout(&call, s, meal, delay, waiting.risk, evaluation.risk);
        double value = gain - forgone * s->stakes;
        if (value >= 0) {
            GsChoice* c = &choices[choice_count++];
            *c = (GsChoice){meal, evaluation, gain, value, meal->n, 0, 0, 0, 0};
            for (int i = 0; i < meal->n; i++) {
                const GsFood* food = &foods[meal->foods[i]];
                c->non_hard += food->kind != GS_HARD_FOOD;
                c->brew += food->drained;
                c->fast_food += food->kind == GS_FAST_FOOD;
                c->heal += food->heal;
            }
        }
    }
    const GsChoice* selected = NULL;
    int fewest = INT_MAX;
    for (int c = 0; c < choice_count; c++) if (choices[c].evaluation.risk <= s->tolerance) fewest = imin(fewest, choices[c].size);
    for (int c = 0; c < choice_count; c++) {
        int sufficient = choices[c].evaluation.risk <= s->tolerance;
        int taken = fewest != INT_MAX ? sufficient && choices[c].size == fewest : choices[c].gain >= s->tolerance;
        if (taken && (!selected || choice_less(&choices[c], selected))) selected = &choices[c];
    }
    int eats = selected && selected->evaluation.risk <= s->tolerance;
    verdict.decision = eats ? GS_EAT_NOW : GS_EXPOSED;
    if (eats) {
        GsMeal later = eat_meal_later(selected->meal);
        double deferred = eat_mixed(&call, s, &later, window, answerable).risk;
        verdict.urgency = deferred - selected->evaluation.risk < s->tolerance ? GS_DEFERRABLE : GS_URGENT;
    }
    verdict.meal_risk = selected ? selected->evaluation.risk : waiting.risk;
    if (selected) {
        verdict.food_count = selected->meal->n;
        memcpy(verdict.foods, selected->meal->foods, (size_t)selected->meal->n * sizeof(int));
        verdict.complete = s->tick + selected->meal->complete;
        for (int i = 0; i < selected->meal->n; i++) {
            const GsFood* food = &foods[selected->meal->foods[i]];
            verdict.health = gs_health_healing(verdict.health, s->tick + selected->meal->offsets[i], food->heal, gs_ceiling(food));
        }
    } else {
        verdict.complete = s->tick;
    }
    arena->used = base;
    return verdict;
}
#endif
