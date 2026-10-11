#ifndef GS_HEALTH_HISTORY_H
#define GS_HEALTH_HISTORY_H
#include "gs_auto_spec.h"
#include <stdio.h>

/* EatingHistory and HitpointsBelief retain recovery and consumption clocks until
 * projection into the immutable production target. No landed future damage is read. */
enum { GS_HEALTH_RECOVERY = 51, GS_HEALTH_LEVELS = 122, GS_HEALTH_BITS = GS_HEALTH_RECOVERY * GS_HEALTH_LEVELS,
       GS_HEALTH_WORDS = (GS_HEALTH_BITS + 63) / 64 };
typedef struct {
    int base;
    GsTargetReady ready;
    uint64_t support[GS_HEALTH_WORDS];
    double* mass;
} GsHealthBranch;
typedef struct {
    GsHealthBranch* branches;
    int count, capacity, tick, has_tick, weighted;
    int* index;
    int index_capacity;
} GsHealthMap;
typedef struct {
    int tick, consumption, has_bar, ratio, scale;
    int* damage;
    int damage_count, damage_capacity;
} GsHealthEvidence;
typedef struct {
    unsigned references;
    GsHealthMap prior, bank;
    GsHealthEvidence* evidence;
    int evidence_count, evidence_capacity, started, has_started, base, incorporated_before, earliest_hard_ready;
    uint64_t base_hp_support[100][2];
    GsTarget target;
} GsHealthHistory;

static void* gs_health_alloc(size_t bytes) {
    if (!bytes) return NULL;
    void* p = calloc(1, bytes);
    if (!p) abort();
    return p;
}
static int gs_health_cap(int base) {
    return base + imax(imax(base / 10 + 2 * (base / 25) + 5 * (base / 93) + 2, base * 15 / 100 + 2), 5);
}
static GsTargetReady gs_health_ready_at(GsTargetReady r, int tick) {
    return (GsTargetReady){imax(r.hard, tick), imax(r.potion, tick), imax(r.fast, tick)};
}
static unsigned gs_health_hash(int base, GsTargetReady r) {
    unsigned h = (unsigned)base;
    h = h * 31 + (unsigned)r.hard; h = h * 31 + (unsigned)r.potion; h = h * 31 + (unsigned)r.fast;
    h ^= h >> 16; h *= 0x7feb352dU; h ^= h >> 15;
    return h;
}
static int gs_health_key(const GsHealthBranch* b, int base, GsTargetReady r) {
    return b->base == base && b->ready.hard == r.hard && b->ready.potion == r.potion && b->ready.fast == r.fast;
}
static void gs_health_reindex(GsHealthMap* m, int capacity) {
    free(m->index);
    m->index = gs_health_alloc((size_t)capacity * sizeof(int));
    m->index_capacity = capacity;
    for (int i = 0; i < m->count; i++) {
        GsHealthBranch* b = &m->branches[i];
        unsigned at = gs_health_hash(b->base, b->ready) & (unsigned)(capacity - 1);
        while (m->index[at]) at = (at + 1) & (unsigned)(capacity - 1);
        m->index[at] = i + 1;
    }
}
static GsHealthBranch* gs_health_branch(GsHealthMap* m, int base, GsTargetReady r) {
    if (!m->index_capacity || (m->count + 1) * 2 > m->index_capacity)
        gs_health_reindex(m, m->index_capacity ? m->index_capacity * 2 : 32);
    unsigned at = gs_health_hash(base, r) & (unsigned)(m->index_capacity - 1);
    while (m->index[at]) {
        GsHealthBranch* b = &m->branches[m->index[at] - 1];
        if (gs_health_key(b, base, r)) return b;
        at = (at + 1) & (unsigned)(m->index_capacity - 1);
    }
    if (m->count == m->capacity) {
        m->capacity = m->capacity ? m->capacity * 2 : 16;
        void* p = realloc(m->branches, (size_t)m->capacity * sizeof(*m->branches));
        if (!p) abort();
        m->branches = p;
    }
    GsHealthBranch* b = &m->branches[m->count];
    memset(b, 0, sizeof(*b)); b->base = base; b->ready = r;
    if (m->weighted) b->mass = gs_health_alloc((size_t)(gs_health_cap(base) + 1) * GS_HEALTH_RECOVERY * sizeof(double));
    m->index[at] = ++m->count;
    return b;
}
static void gs_health_map_clear(GsHealthMap* m) {
    for (int i = 0; i < m->count; i++) free(m->branches[i].mass);
    free(m->branches); free(m->index); memset(m, 0, sizeof(*m));
}
static GsHealthMap gs_health_map_copy(const GsHealthMap* source) {
    GsHealthMap out = {.weighted = source->weighted, .tick = source->tick, .has_tick = source->has_tick};
    for (int i = 0; i < source->count; i++) {
        const GsHealthBranch* b = &source->branches[i];
        GsHealthBranch* target = gs_health_branch(&out, b->base, b->ready);
        memcpy(target->support, b->support, sizeof(b->support));
        if (source->weighted) memcpy(target->mass, b->mass, (size_t)(gs_health_cap(b->base) + 1) * GS_HEALTH_RECOVERY * sizeof(double));
    }
    return out;
}
static int gs_health_bit(const GsHealthBranch* b, int bit) { return (int)(b->support[bit / 64] >> (bit % 64) & 1); }
static void gs_health_put(GsHealthMap* m, int base, GsTargetReady ready, int hp, int recovery, double mass) {
    GsHealthBranch* b = gs_health_branch(m, base, ready);
    int bit = hp * GS_HEALTH_RECOVERY + recovery;
    if (m->weighted) b->mass[bit] += mass;
    else b->support[bit / 64] |= UINT64_C(1) << (bit % 64);
}
static GsHealthMap gs_health_start(int lower) {
    GsHealthMap out = {0};
    GsTargetReady ready = {INT_MIN, INT_MIN, INT_MIN};
    for (int base = lower; base <= 99; base++) for (int hp = 1; hp <= gs_health_cap(base); hp++)
        gs_health_put(&out, base, ready, hp, 0, 1);
    return out;
}
static GsHealthMap gs_health_weight_start(int base, int tick) {
    GsHealthMap out = {.weighted = 1, .has_tick = 1, .tick = tick};
    GsHealthBranch* b = gs_health_branch(&out, base, (GsTargetReady){tick, tick, tick});
    int cap = gs_health_cap(base);
    double maximum = -INFINITY, total = 0;
    for (int hp = 1; hp <= cap; hp++) {
        double logit = 7.417562295626411 * imax(0, hp - base) / (cap - base) + (hp >= base ? -2.402956826541529 : 0);
        b->mass[hp * GS_HEALTH_RECOVERY] = logit; maximum = fmax(maximum, logit);
    }
    for (int hp = 1; hp <= cap; hp++) { double* p = &b->mass[hp * GS_HEALTH_RECOVERY]; *p = exp(*p - maximum); total += *p; }
    for (int hp = 1; hp <= cap; hp++) b->mass[hp * GS_HEALTH_RECOVERY] /= total;
    return out;
}
static GsHealthMap gs_health_advance(const GsHealthMap* source, int tick) {
    if (source->has_tick && tick < source->tick) abort();
    if (source->has_tick && tick == source->tick) return gs_health_map_copy(source);
    int elapsed = source->has_tick ? tick - source->tick : 0;
    if (source->weighted && elapsed > 1) {
        GsHealthMap current = gs_health_advance(source, source->tick + 1);
        while (current.tick < tick) {
            GsHealthMap next = gs_health_advance(&current, current.tick + 1);
            gs_health_map_clear(&current); current = next;
        }
        return current;
    }
    GsHealthMap out = {.weighted = source->weighted, .has_tick = 1, .tick = tick};
    for (int i = 0; i < source->count; i++) {
        const GsHealthBranch* b = &source->branches[i];
        GsTargetReady ready = gs_health_ready_at(b->ready, tick);
        for (int hp = 0; hp <= gs_health_cap(b->base); hp++) {
            int first_recovery = -1;
            for (int recovery = 0; recovery < GS_HEALTH_RECOVERY; recovery++) {
                int bit = hp * GS_HEALTH_RECOVERY + recovery;
                double probability = source->weighted ? b->mass[bit] : gs_health_bit(b, bit);
                if (!probability) continue;
                if (!source->weighted) {
                    if (first_recovery < 0) first_recovery = recovery;
                    continue;
                }
                if (!elapsed || hp == 0 || recovery > 1) gs_health_put(&out, b->base, ready, hp, imax(0, recovery - elapsed), probability);
                else {
                    gs_health_put(&out, b->base, ready, hp, 0, probability * (1 - 0.012736330644131629));
                    int direction = (b->base > hp) - (b->base < hp);
                    gs_health_put(&out, b->base, ready, hp + direction, 50, probability * 0.012736330644131629);
                }
            }
            if (first_recovery >= 0) {
                gs_health_put(&out, b->base, ready, hp, imax(0, first_recovery - elapsed), 1);
                int first = imax(1, first_recovery), distance = hp == 0 ? 0 : abs(b->base - hp);
                for (int pulses = 1; pulses <= distance && first + (pulses - 1) * 50 <= elapsed; pulses++)
                    gs_health_put(&out, b->base, ready, hp + ((b->base > hp) - (b->base < hp)) * pulses,
                        imax(0, first + pulses * 50 - elapsed), 1);
            }
        }
    }
    return out;
}
static int gs_health_bar_contains(int base, int hp, const GsHealthEvidence* e) {
    if (!e->has_bar) return 1;
    int low = e->ratio == 0 ? 0 : e->ratio == 1 ? 1 : (base * (e->ratio - 1) + e->scale - 2) / (e->scale - 1);
    int high = e->ratio == 0 ? 0 : e->ratio == e->scale ? gs_health_cap(base) : imin(base, (base * e->ratio - 1) / (e->scale - 1));
    return hp >= low && hp <= high;
}
static const int GS_HISTORY_ITEM_HEALS[11] = {20,21,22,24,11,11,0,5,16,18,20};
static const int GS_HEALTH_DELAYS[6] = {3,3,3,3,1,3};
static const double GS_HEALTH_MASK_LOGITS[8] = {0,-2.6453738910895472,-1.6882877332587156,-4.333661624348263,-5.25827736620845,-7.903651257297997,-6.946565099467166,-9.591938990556713};
static const double GS_HEALTH_ITEM_LOGITS[11] = {0,5.68419077426806,8.864425512136672,7.713026878675549,6.713311720866542,-163.83573860638774,0,-4.481476767771444,-2.8511538249235904,0,-0.8198123501936592};
typedef struct { int hard, potion, fast, mask; double item_log; } GsHealthMeal;
static int gs_health_menu(GsHealthMeal meals[84]) {
    double probabilities[11];
    const int starts[3] = {0,6,9}, ends[3] = {6,9,11};
    for (int kind = 0; kind < 3; kind++) {
        double maximum = -INFINITY, total = 0;
        for (int i = starts[kind]; i < ends[kind]; i++) maximum = fmax(maximum, GS_HEALTH_ITEM_LOGITS[i]);
        for (int i = starts[kind]; i < ends[kind]; i++) total += exp(GS_HEALTH_ITEM_LOGITS[i] - maximum);
        for (int i = starts[kind]; i < ends[kind]; i++) probabilities[i] = GS_HEALTH_ITEM_LOGITS[i] - maximum - log(total);
    }
    int count = 0;
    for (int h = -1; h < 6; h++) for (int p = 5; p < 9; p++) for (int f = 8; f < 11; f++) {
        int potion = p == 5 ? -1 : p, fast = f == 8 ? -1 : f;
        int mask = (h >= 0) | ((potion >= 0) << 1) | ((fast >= 0) << 2);
        meals[count++] = (GsHealthMeal){h, potion, fast, mask,
            (h >= 0 ? probabilities[h] : 0) + (potion >= 0 ? probabilities[potion] : 0) + (fast >= 0 ? probabilities[fast] : 0)};
    }
    return count;
}
static int gs_health_meal_ready(GsTargetReady before, const GsHealthMeal* meal, int tick, GsTargetReady* after) {
    *after = gs_health_ready_at(before, tick);
    if (meal->hard >= 0) {
        if (after->hard > tick) return 0;
        after->hard = tick + GS_HEALTH_DELAYS[meal->hard];
    }
    if (meal->potion >= 0) {
        if (after->potion > tick) return 0;
        after->hard = imax(after->hard, tick + 3); after->potion = imax(after->potion, tick + 3);
    }
    if (meal->fast >= 0) {
        if (after->fast > tick) return 0;
        after->hard = imax(after->hard, tick + 3); after->potion = imax(after->potion, tick + 3); after->fast = imax(after->fast, tick + 3);
    }
    return 1;
}
static int gs_health_heal(int hp, int base, int item) {
    if (item < 0 || hp == 0) return hp;
    int amount = item == 8 ? base * 15 / 100 + 2 : GS_HISTORY_ITEM_HEALS[item];
    int ceiling = item == 8 ? base + amount : item == 7 && hp >= base ? base + 5 : base;
    return imax(hp, imin(ceiling, hp + amount));
}
static int gs_health_meal_hp(int hp, int base, const GsHealthMeal* meal) {
    hp = gs_health_heal(hp, base, meal->hard); hp = gs_health_heal(hp, base, meal->potion);
    return gs_health_heal(hp, base, meal->fast);
}
static GsHealthMap gs_health_observed(const GsHealthMap* source, const GsHealthEvidence* e) {
    GsHealthMap prior = gs_health_advance(source, e->tick);
    GsHealthMap out = {.weighted = source->weighted, .has_tick = 1, .tick = e->tick};
    GsHealthMeal menu[84]; int menu_count = gs_health_menu(menu);
    double* ranks = gs_health_alloc((size_t)(e->damage_count + 1) * sizeof(double)); ranks[0] = 1;
    for (int hit = 0; hit < e->damage_count; hit++) for (int rank = hit + 1; rank >= 0; rank--)
        ranks[rank] = ranks[rank] * (1 - 0.1546382380380581) + (rank ? ranks[rank - 1] * 0.1546382380380581 : 0);
    int order_count = e->consumption ? e->damage_count + 2 : 1;
    for (int order = 0; order < order_count; order++) {
        int before_bar = e->consumption && order == 0;
        int preceding = !e->consumption || before_bar ? e->damage_count : order - 1;
        double order_probability = !e->consumption ? 1 : before_bar ? 0.26318513406589134 : (1 - 0.26318513406589134) * ranks[preceding];
        for (int i = 0; i < prior.count; i++) {
            const GsHealthBranch* b = &prior.branches[i];
            GsTargetReady choices_ready[84]; int available[84], choices_count = 0;
            for (int meal = 0; meal < menu_count; meal++) {
                if (!e->consumption && menu[meal].mask) continue;
                if (gs_health_meal_ready(b->ready, &menu[meal], e->tick, &choices_ready[choices_count])) available[choices_count++] = meal;
            }
            for (int hp = 0; hp <= gs_health_cap(b->base); hp++) {
                int present = 0;
                for (int r = 0; r < 51; r++) present |= source->weighted ? b->mass[hp * 51 + r] > 0 : gs_health_bit(b, hp * 51 + r);
                if (!present) continue;
                int before = hp;
                for (int hit = 0; hit < preceding; hit++) before = imax(0, before - e->damage[hit]);
                if (before_bar && !gs_health_bar_contains(b->base, before, e)) continue;
                double logits[84], maximum = -INFINITY, denominator = 0;
                for (int choice = 0; choice < choices_count; choice++) {
                    const GsHealthMeal* meal = &menu[available[choice]];
                    logits[choice] = before == 0 && meal->mask ? -INFINITY : GS_HEALTH_MASK_LOGITS[meal->mask]
                        + (meal->mask ? 8.696489857377006 * (b->base - before) / b->base : 0) + meal->item_log;
                    maximum = fmax(maximum, logits[choice]);
                }
                if (source->weighted) for (int choice = 0; choice < choices_count; choice++) denominator += exp(logits[choice] - maximum);
                for (int choice = 0; choice < choices_count; choice++) {
                    const GsHealthMeal* meal = &menu[available[choice]];
                    if ((e->consumption == 2 && !meal->mask) || (before == 0 && meal->mask)) continue;
                    int after = gs_health_meal_hp(before, b->base, meal);
                    for (int hit = preceding; hit < e->damage_count; hit++) after = imax(0, after - e->damage[hit]);
                    if (!before_bar && !gs_health_bar_contains(b->base, after, e)) continue;
                    double probability = source->weighted ? order_probability * exp(logits[choice] - maximum) / denominator : 1;
                    if (!probability) continue;
                    for (int r = 0; r < 51; r++) {
                        double mass = source->weighted ? b->mass[hp * 51 + r] : gs_health_bit(b, hp * 51 + r);
                        if (mass) gs_health_put(&out, b->base, choices_ready[choice], after, r, mass * probability);
                    }
                }
            }
        }
    }
    free(ranks); gs_health_map_clear(&prior);
    if (!out.count) {
        fprintf(stderr, "causal HP evidence has no support: tick=%d consumption=%d bar=%d ratio=%d scale=%d damages=%d prior_branches=%d weighted=%d\n",
            e->tick, e->consumption, e->has_bar, e->ratio, e->scale, e->damage_count, source->count, source->weighted);
        abort();
    }
    if (source->weighted) {
        double total = 0;
        for (int i = 0; i < out.count; i++) for (int bit = 0; bit < (gs_health_cap(out.branches[i].base) + 1) * 51; bit++) total += out.branches[i].mass[bit];
        if (!(total > 0)) {
            fprintf(stderr, "causal HP evidence has no probability mass: tick=%d consumption=%d bar=%d ratio=%d scale=%d damages=%d\n",
                e->tick, e->consumption, e->has_bar, e->ratio, e->scale, e->damage_count);
            abort();
        }
        for (int i = 0; i < out.count; i++) for (int bit = 0; bit < (gs_health_cap(out.branches[i].base) + 1) * 51; bit++) out.branches[i].mass[bit] /= total;
    }
    return out;
}
static void gs_health_target_clear(GsTarget* target) {
    for (int i = 0; i < target->history_count; i++) free(target->history[i].hp);
    for (int i = 0; i < target->belief_count; i++) free(target->belief[i].hp);
    free(target->history); free(target->belief); memset(target, 0, sizeof(*target));
}
static void gs_health_project(GsHealthHistory* h, const GsHealthMap* current, const GsHealthMap* belief, int unread) {
    gs_health_target_clear(&h->target);
    h->target.has_history = 1; h->target.has_belief = !unread; h->target.base_hp = unread ? GS_ASSUMED_MAX_HP : h->base;
    int count = 0;
    for (int i = 0; i < current->count; i++) count += current->branches[i].base == h->base;
    h->target.history = gs_health_alloc((size_t)count * sizeof(GsTargetBranch));
    for (int i = 0; i < current->count; i++) {
        const GsHealthBranch* source = &current->branches[i]; if (source->base != h->base) continue;
        GsTargetBranch* b = &h->target.history[h->target.history_count++];
        *b = (GsTargetBranch){source->ready, gs_health_alloc(GS_HEALTH_LEVELS * sizeof(double)), GS_HEALTH_LEVELS};
        if (unread) b->hp[GS_OVERHEALED_HP] = 1;
        else for (int hp = 0; hp <= gs_health_cap(h->base); hp++) for (int r = 0; r < 51; r++)
            if (gs_health_bit(source, hp * 51 + r)) { b->hp[hp] = 1; break; }
    }
    if (unread) return;
    h->target.belief = gs_health_alloc((size_t)belief->count * sizeof(GsTargetBranch));
    double alive = 0;
    for (int i = 0; i < belief->count; i++) {
        const GsHealthBranch* source = &belief->branches[i];
        GsTargetBranch* b = &h->target.belief[h->target.belief_count++];
        int levels = gs_health_cap(h->base) + 1;
        *b = (GsTargetBranch){source->ready, gs_health_alloc((size_t)levels * sizeof(double)), levels};
        for (int hp = 1; hp < levels; hp++) {
            for (int r = 0; r < 51; r++) b->hp[hp] += source->mass[hp * 51 + r];
            alive += b->hp[hp];
        }
    }
    if (!(alive > 0)) {
        fprintf(stderr, "causal HP forecast has no living mass: base=%d current_branches=%d belief_branches=%d evidence=%d\n",
            h->base, current->count, belief->count, h->evidence_count);
        abort();
    }
    for (int i = 0; i < h->target.belief_count; i++)
        for (int hp = 1; hp < h->target.belief[i].levels; hp++) h->target.belief[i].hp[hp] /= alive;
}
static GsHealthHistory* gs_health_history_make(void) {
    GsHealthHistory* h = gs_health_alloc(sizeof(*h)); h->references = 1;
    return h;
}
static void gs_health_history_release(GsHealthHistory* h) {
    if (!h || --h->references) return;
    gs_health_map_clear(&h->prior); gs_health_map_clear(&h->bank);
    for (int i = 0; i < h->evidence_count; i++) free(h->evidence[i].damage);
    free(h->evidence); gs_health_target_clear(&h->target); free(h);
}
static GsHealthHistory* gs_health_history_copy(const GsHealthHistory* source) {
    GsHealthHistory* h = gs_health_alloc(sizeof(*h)); h->references = 1;
    h->prior = gs_health_map_copy(&source->prior); h->bank = gs_health_map_copy(&source->bank);
    h->target = source->target;
    h->target.history = gs_health_alloc((size_t)h->target.history_count * sizeof(GsTargetBranch));
    h->target.belief = gs_health_alloc((size_t)h->target.belief_count * sizeof(GsTargetBranch));
    for (int kind = 0; kind < 2; kind++) {
        int count = kind ? h->target.belief_count : h->target.history_count;
        GsTargetBranch* out = kind ? h->target.belief : h->target.history;
        const GsTargetBranch* in = kind ? source->target.belief : source->target.history;
        for (int i = 0; i < count; i++) {
            out[i] = in[i];
            out[i].hp = gs_health_alloc((size_t)in[i].levels * sizeof(double));
            memcpy(out[i].hp, in[i].hp, (size_t)in[i].levels * sizeof(double));
        }
    }
    h->started = source->started; h->has_started = source->has_started; h->base = source->base;
    h->incorporated_before = source->incorporated_before;
    h->earliest_hard_ready = source->earliest_hard_ready;
    memcpy(h->base_hp_support, source->base_hp_support, sizeof(h->base_hp_support));
    h->evidence_capacity = h->evidence_count = source->evidence_count;
    h->evidence = gs_health_alloc((size_t)h->evidence_count * sizeof(*h->evidence));
    for (int i = 0; i < h->evidence_count; i++) {
        h->evidence[i] = source->evidence[i];
        h->evidence[i].damage_capacity = h->evidence[i].damage_count;
        h->evidence[i].damage = gs_health_alloc((size_t)h->evidence[i].damage_count * sizeof(int));
        if (h->evidence[i].damage_count)
            memcpy(h->evidence[i].damage, source->evidence[i].damage, (size_t)h->evidence[i].damage_count * sizeof(int));
    }
    return h;
}
static GsHealthHistory* gs_health_history_mutate(GsHealthHistory** owner) {
    if (!*owner) *owner = gs_health_history_make();
    else if ((*owner)->references > 1) {
        GsHealthHistory* next = gs_health_history_copy(*owner); gs_health_history_release(*owner); *owner = next;
    }
    return *owner;
}
static GsHealthEvidence* gs_health_evidence(GsHealthHistory* h, int tick) {
    int at = 0; while (at < h->evidence_count && h->evidence[at].tick < tick) at++;
    if (at < h->evidence_count && h->evidence[at].tick == tick) return &h->evidence[at];
    if (h->evidence_count == h->evidence_capacity) {
        h->evidence_capacity = h->evidence_capacity ? 2 * h->evidence_capacity : 16;
        void* p = realloc(h->evidence, (size_t)h->evidence_capacity * sizeof(*h->evidence)); if (!p) abort(); h->evidence = p;
    }
    memmove(h->evidence + at + 1, h->evidence + at, (size_t)(h->evidence_count - at) * sizeof(*h->evidence)); h->evidence_count++;
    h->evidence[at] = (GsHealthEvidence){.tick = tick}; return &h->evidence[at];
}
static void gs_health_damage(GsHealthHistory** owner, int tick, int damage) {
    if (damage < 0) abort();
    GsHealthEvidence* e = gs_health_evidence(gs_health_history_mutate(owner), tick);
    if (e->damage_count == e->damage_capacity) {
        e->damage_capacity = e->damage_capacity ? e->damage_capacity * 2 : 4;
        void* p = realloc(e->damage, (size_t)e->damage_capacity * sizeof(int)); if (!p) abort(); e->damage = p;
    }
    e->damage[e->damage_count++] = damage;
}
static void gs_health_consumption(GsHealthHistory** owner, int tick, int observed) {
    GsHealthEvidence* e = gs_health_evidence(gs_health_history_mutate(owner), tick);
    if (observed || !e->consumption) e->consumption = observed ? 2 : 1;
}
static void gs_health_observe(GsHealthHistory** owner, int tick, int lower_base, int has_bar, int ratio, int scale, int unread) {
    GsHealthHistory* h = gs_health_history_mutate(owner);
    if (lower_base < 10 || lower_base > 99 || (has_bar && (scale <= 1 || ratio < 0 || ratio > scale))) abort();
    if (has_bar) { GsHealthEvidence* e = gs_health_evidence(h, tick); e->has_bar = 1; e->ratio = ratio; e->scale = scale; }
    if (!h->has_started) h->prior = gs_health_start(lower_base);
    if (!h->has_started) { h->started = h->evidence_count ? imin(tick, h->evidence[0].tick) : tick; h->has_started = 1; }
    int restricted = 0;
    for (int i = 0; i < h->prior.count; i++) restricted |= h->prior.branches[i].base < lower_base;
    if (restricted) {
        GsHealthMap filtered = {.tick = h->prior.tick, .has_tick = h->prior.has_tick};
        for (int i = 0; i < h->prior.count; i++) {
            const GsHealthBranch* b = &h->prior.branches[i]; if (b->base < lower_base) continue;
            GsHealthBranch* to = gs_health_branch(&filtered, b->base, b->ready);
            memcpy(to->support, b->support, sizeof(b->support));
        }
        gs_health_map_clear(&h->prior); h->prior = filtered;
    }
    GsHealthMap current = gs_health_map_copy(&h->prior);
    int boundary = h->has_started && h->prior.has_tick ? h->incorporated_before : INT_MIN;
    for (int i = 0; i < h->evidence_count; i++) {
        GsHealthEvidence* e = &h->evidence[i]; if (e->tick < boundary) continue;
        GsHealthMap next = gs_health_observed(&current, e); gs_health_map_clear(&current); current = next;
        if (e->tick < tick) { gs_health_map_clear(&h->prior); h->prior = gs_health_map_copy(&current); }
    }
    h->incorporated_before = tick;
    int base = 0; for (int i = 0; i < current.count; i++) base = imax(base, current.branches[i].base);
    if (!base) abort();
    GsHealthMap weighted;
    int replay = !h->bank.count || h->base != base;
    if (replay) weighted = gs_health_weight_start(base, h->started);
    else weighted = gs_health_map_copy(&h->bank);
    for (int i = 0; i < h->evidence_count; i++) {
        GsHealthEvidence* e = &h->evidence[i];
        if (!replay && e->tick < h->bank.tick) continue;
        if (e->tick == tick) {
            GsHealthMap bank = gs_health_advance(&weighted, tick); gs_health_map_clear(&h->bank); h->bank = bank;
        }
        GsHealthMap next = gs_health_observed(&weighted, e); gs_health_map_clear(&weighted); weighted = next;
    }
    int current_tick_evidence = 0; for (int i = 0; i < h->evidence_count; i++) current_tick_evidence |= h->evidence[i].tick == tick;
    GsHealthMap belief = gs_health_advance(&weighted, tick); gs_health_map_clear(&weighted);
    if (!current_tick_evidence) { gs_health_map_clear(&h->bank); h->bank = gs_health_map_copy(&belief); }
    GsHealthMap advanced = gs_health_advance(&current, tick); gs_health_map_clear(&current);
    h->base = base;
    h->earliest_hard_ready = INT_MAX;
    for (int i = 0; i < advanced.count; i++)
        h->earliest_hard_ready = imin(h->earliest_hard_ready, imax(tick, advanced.branches[i].ready.hard));
    memset(h->base_hp_support, 0, sizeof(h->base_hp_support));
    for (int i = 0; i < advanced.count; i++) {
        const GsHealthBranch* branch = &advanced.branches[i];
        for (int hp = 0; hp <= gs_health_cap(branch->base); hp++)
            for (int recovery = 0; recovery < GS_HEALTH_RECOVERY; recovery++)
                if (gs_health_bit(branch, hp * GS_HEALTH_RECOVERY + recovery)) {
                    h->base_hp_support[branch->base][hp / 64] |= UINT64_C(1) << (hp % 64);
                    break;
                }
    }
    gs_health_project(h, &advanced, &belief, unread);
    gs_health_map_clear(&advanced); gs_health_map_clear(&belief);
}
#endif
