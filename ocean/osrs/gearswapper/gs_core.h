#ifndef GS_CORE_H
#define GS_CORE_H
/* Ports of the Light Gear Swapper's pure combat decisions (platform/interaction net.light.local.combat).
 * Names follow the Java classes so a rule changed there is changed here at the same place.
 * FP_CONTRACT is off: Java never fuses multiply-add and the port must round the same way. */
#pragma STDC FP_CONTRACT OFF
#include <assert.h>
#include <limits.h>
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

enum { GS_NONE = INT_MIN, GS_ASSUMED_MAX_HP = 99, GS_OVERHEALED_HP = 121 };

typedef enum { GS_MELEE, GS_RANGED, GS_MAGIC } GsStyle;
typedef enum { GS_PID_LOCAL, GS_PID_OPPONENT, GS_PID_UNKNOWN } GsPid;
typedef enum { GS_VENGEANCE_UNKNOWN, GS_VENGEANCE_UP, GS_VENGEANCE_DOWN, GS_VENGEANCE_MAYBE } GsVengeance;
typedef enum { GS_POTION, GS_FAST_FOOD, GS_HARD_FOOD, GS_CONSUMABLES } GsConsumable;

typedef struct Arena Arena;
struct Arena { char* base; size_t used, capacity; };

static Arena arena_make(size_t capacity) {
    Arena a = {malloc(capacity), 0, capacity};
    assert(a.base);
    return a;
}

static void* arena_alloc(Arena* a, size_t bytes) {
    size_t aligned = (a->used + 15) & ~(size_t)15;
    assert(aligned + bytes <= a->capacity);
    a->used = aligned + bytes;
    return a->base + aligned;
}

typedef struct { void* data; int count, capacity; size_t size; Arena* arena; } GsVec;

static GsVec vec_make(Arena* arena, size_t size) { return (GsVec){NULL, 0, 0, size, arena}; }

static void* vec_push(GsVec* v) {
    if (v->count == v->capacity) {
        int capacity = v->capacity ? v->capacity * 2 : 4;
        void* data = arena_alloc(v->arena, (size_t)capacity * v->size);
        if (v->count) memcpy(data, v->data, (size_t)v->count * v->size);
        v->data = data;
        v->capacity = capacity;
    }
    return (char*)v->data + (size_t)v->count++ * v->size;
}

static GsVec vec_copy(Arena* arena, const GsVec* from) {
    GsVec v = vec_make(arena, from->size);
    if (from->count) {
        v.data = arena_alloc(arena, (size_t)from->count * from->size);
        memcpy(v.data, from->data, (size_t)from->count * from->size);
        v.count = v.capacity = from->count;
    }
    return v;
}

#define VEC_AT(type, v, i) (((type*)(v).data)[i])

/* Hitpoints.java: a distribution indexed by hitpoints, index 0 empty, the caller counts the dead. */
static double* hp_uniform(Arena* a, int low, int high, int size) {
    double* alive = arena_alloc(a, (size_t)size * sizeof(double));
    memset(alive, 0, (size_t)size * sizeof(double));
    for (int hp = low; hp <= high; hp++) alive[hp] = 1.0 / (high - low + 1);
    return alive;
}

static int imin(int a, int b) { return a < b ? a : b; }
static int imax(int a, int b) { return a > b ? a : b; }

static double hp_strike_range(Arena* arena, double* alive, int length, int least, int most, double accuracy) {
    double* prefix = arena_alloc(arena, (size_t)(length + 1) * sizeof(double));
    prefix[0] = 0;
    for (int hp = 0; hp < length; hp++) prefix[hp + 1] = prefix[hp] + alive[hp];
    double each = accuracy / (most - least + 1);
    double killed = 0;
    for (int hp = 1; hp < length; hp++) {
        double before = prefix[hp + 1] - prefix[hp];
        killed += before * each * imax(0, most - imax(hp, least) + 1);
        alive[hp] = (1 - accuracy) * before + each * (prefix[imin(length, hp + most + 1)] - prefix[imin(length, hp + least)]);
    }
    return killed;
}

static double hp_strike_damage(Arena* arena, double* alive, int length, const double* damage, int damages) {
    double* after = arena_alloc(arena, (size_t)length * sizeof(double));
    memset(after, 0, (size_t)length * sizeof(double));
    double killed = 0;
    for (int hp = 1; hp < length; hp++)
        for (int dealt = 0; dealt < damages; dealt++) {
            double mass = alive[hp] * damage[dealt];
            if (dealt >= hp) killed += mass;
            else after[hp - dealt] += mass;
        }
    memcpy(alive, after, (size_t)length * sizeof(double));
    return killed;
}

static void hp_heal(double* alive, int amount, int cap) {
    for (int hp = cap - 1; hp > 0; hp--) {
        double mass = alive[hp];
        alive[hp] = 0;
        alive[imin(cap, hp + amount)] += mass;
    }
}

/* Consumable.readyAfter */
static int consumable_ready_after(GsConsumable self, GsConsumable consumed, int tick, int previous_ready) {
    return consumed == GS_FAST_FOOD || self == GS_HARD_FOOD || self == consumed ? imax(previous_ready, tick + 3) : previous_ready;
}

typedef enum { GS_CONSUMPTION_POTION, GS_CONSUMPTION_FAST, GS_CONSUMPTION_HARD, GS_CONSUMPTION_COMBO } GsConsumption;
static int consumption_delay(GsConsumption c) {
    static const int delay[] = {0, 2, 3, 5};
    return delay[c];
}

/* Consumption.of over the kinds present: a hard food with a fast food is the combo. */
static GsConsumption consumption_of(int has_potion, int has_fast, int has_hard) {
    (void)has_potion;
    if (has_hard && has_fast) return GS_CONSUMPTION_COMBO;
    if (has_hard) return GS_CONSUMPTION_HARD;
    return has_fast ? GS_CONSUMPTION_FAST : GS_CONSUMPTION_POTION;
}

typedef struct { int low, high, max; } GsHealthRange;
typedef struct { int seen, next; } GsTimer;
typedef struct { int since; int unseen[16]; int unseen_count; } GsWarning;
typedef struct { int lands, least, most; double accuracy; } GsSwing;
typedef struct { GsStyle style; int least, most, cost, interval; double accuracy; } GsAttack;

static int gs_beats(int processed, int lands, GsPid pid) {
    return processed < lands || (processed == lands && pid == GS_PID_LOCAL);
}

typedef enum { GS_LEAD_ATTACKER, GS_LEAD_TARGET } GsLead;

static int gs_lands(int attack_tick, GsStyle style, int distance, GsLead lead) {
    switch (style) {
        case GS_MELEE: return attack_tick + (lead == GS_LEAD_ATTACKER ? 0 : 1);
        case GS_RANGED: return attack_tick + 1 + (3 + distance) / 6;
        case GS_MAGIC: return attack_tick + 1 + (1 + distance) / 3;
    }
    abort();
}

static GsLead gs_opponent_lead(GsPid pid) { return pid == GS_PID_LOCAL ? GS_LEAD_TARGET : GS_LEAD_ATTACKER; }
static GsLead gs_local_lead(GsPid pid) { return pid == GS_PID_OPPONENT ? GS_LEAD_TARGET : GS_LEAD_ATTACKER; }
#endif
