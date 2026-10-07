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

/* StrictMath.pow (fdlibm, which Math.pow runs on aarch64) for a positive normal x and a small non-negative y: the domain of HiddenWeapon.carried. */
static int fd_hi(double x) { int64_t b; memcpy(&b, &x, 8); return (int)(b >> 32); }
static int fd_lo(double x) { int64_t b; memcpy(&b, &x, 8); return (int)b; }
static double fd_with_lo(double x, int lo) { int64_t b; memcpy(&b, &x, 8); b = (b & ~0xffffffffLL) | (uint32_t)lo; memcpy(&x, &b, 8); return x; }
static double fd_with_hi(double x, int hi) { int64_t b; memcpy(&b, &x, 8); b = (b & 0xffffffffLL) | ((int64_t)hi << 32); memcpy(&x, &b, 8); return x; }

static double gs_pow(double x, double y) {
    assert(x > 0 && x < INFINITY && y >= 0 && y < 2147483647.0 && fd_hi(x) >= 0x00100000);
    if (y == 0.0) return 1.0;
    if (y == 2.0) return x * x;
    if (y == 1.0) return x;
    static const double BP[2] = {1.0, 1.5};
    static const double DP_H[2] = {0.0, 0x1.2b8034p-1};
    static const double DP_L[2] = {0.0, 0x1.cfdeb43cfd006p-27};
    const double L1 = 0x1.3333333333303p-1, L2 = 0x1.b6db6db6fabffp-2, L3 = 0x1.55555518f264dp-2, L4 = 0x1.17460a91d4101p-2, L5 = 0x1.d864a93c9db65p-3, L6 = 0x1.a7e284a454eefp-3;
    const double CP = 0x1.ec709dc3a03fdp-1, CP_H = 0x1.ec709ep-1, CP_L = -0x1.e2fe0145b01f5p-28;
    double x_abs = x;
    int ix = fd_hi(x_abs);
    int n = ((ix) >> 20) - 0x3ff;
    int j = ix & 0x000fffff;
    int k;
    ix = j | 0x3ff00000;
    if (j <= 0x3988E) k = 0;
    else if (j < 0xBB67A) k = 1;
    else { k = 0; n += 1; ix -= 0x00100000; }
    x_abs = fd_with_hi(x_abs, ix);
    double u = x_abs - BP[k];
    double v = 1.0 / (x_abs + BP[k]);
    double ss = u * v;
    double s_h = fd_with_lo(ss, 0);
    double t_h = fd_with_hi(0.0, ((ix >> 1) | 0x20000000) + 0x00080000 + (k << 18));
    double t_l = x_abs - (t_h - BP[k]);
    double s_l = v * ((u - s_h * t_h) - s_h * t_l);
    double s2 = ss * ss;
    double r = s2 * s2 * (L1 + s2 * (L2 + s2 * (L3 + s2 * (L4 + s2 * (L5 + s2 * L6)))));
    r += s_l * (s_h + ss);
    s2 = s_h * s_h;
    t_h = fd_with_lo(3.0 + s2 + r, 0);
    t_l = r - ((t_h - 3.0) - s2);
    u = s_h * t_h;
    v = s_l * t_h + t_l * ss;
    double p_h = fd_with_lo(u + v, 0);
    double p_l = v - (p_h - u);
    double z_h = CP_H * p_h;
    double z_l = CP_L * p_h + p_l * CP + DP_L[k];
    double t = (double)n;
    double t1 = fd_with_lo((((z_h + z_l) + DP_H[k]) + t), 0);
    double t2 = z_l - (((t1 - t) - DP_H[k]) - z_h);
    double y1 = fd_with_lo(y, 0);
    p_l = (y - y1) * t1 + y * t2;
    p_h = y1 * t1;
    double z = p_l + p_h;
    j = fd_hi(z);
    assert(j < 0x40900000 && (j & 0x7fffffff) < 0x4090cc00);
    const double P1 = 0x1.555555555553ep-3, P2 = -0x1.6c16c16bebd93p-9, P3 = 0x1.1566aaf25de2cp-14, P4 = -0x1.bbd41c5d26bf1p-20, P5 = 0x1.6376972bea4d0p-25;
    const double LG2 = 0x1.62e42fefa39efp-1, LG2_H = 0x1.62e43p-1, LG2_L = -0x1.05c610ca86c39p-29;
    int i = j & 0x7fffffff;
    k = (i >> 20) - 0x3ff;
    n = 0;
    if (i > 0x3fe00000) {
        n = j + (0x00100000 >> (k + 1));
        k = ((n & 0x7fffffff) >> 20) - 0x3ff;
        t = fd_with_hi(0.0, n & ~(0x000fffff >> k));
        n = ((n & 0x000fffff) | 0x00100000) >> (20 - k);
        if (j < 0) n = -n;
        p_h -= t;
    }
    t = fd_with_lo(p_l + p_h, 0);
    u = t * LG2_H;
    v = (p_l - (t - p_h)) * LG2 + t * LG2_L;
    z = u + v;
    double w = v - (z - u);
    t = z * z;
    t1 = z - t * (P1 + t * (P2 + t * (P3 + t * (P4 + t * P5))));
    r = (z * t1) / (t1 - 2.0) - (w + z * w);
    z = 1.0 - (r - z);
    j = fd_hi(z);
    j += (n << 20);
    assert((j >> 20) > 0);
    return fd_with_hi(z, fd_hi(z) + (n << 20));
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
typedef struct { int seen, next, earliest; } GsTimer;
enum { GS_WATCHED_LIMIT = 1024 };
typedef enum { GS_SEEN_QUIET, GS_SEEN_ATE, GS_SEEN_HIDDEN } GsSeen;
typedef struct { int since; int unseen[64]; int unseen_count; uint8_t watched[GS_WATCHED_LIMIT]; int watched_count; } GsWarning;
typedef struct { int lands, least, most; double accuracy; int arrows; } GsSwing;
typedef struct { GsStyle style; int least, most, cost, interval; double accuracy; int godsword; int arrows; } GsAttack;

static int gs_beats(int processed, int lands, GsPid pid) {
    return processed < lands || (processed == lands && pid == GS_PID_LOCAL);
}

typedef enum { GS_LEAD_ATTACKER, GS_LEAD_TARGET } GsLead;

static int gs_lands(int attack_tick, GsStyle style, int distance, GsLead lead) {
    switch (style) {
        case GS_MELEE: return attack_tick + (lead == GS_LEAD_ATTACKER ? 0 : 1);
        case GS_RANGED: return attack_tick + 1 + (3 + distance) / 6 + (lead == GS_LEAD_ATTACKER ? 0 : 1);
        case GS_MAGIC: return attack_tick + 1 + (1 + distance) / 3;
    }
    abort();
}

static GsLead gs_opponent_lead(GsPid pid) { return pid == GS_PID_LOCAL ? GS_LEAD_TARGET : GS_LEAD_ATTACKER; }
static GsLead gs_local_lead(GsPid pid) { return pid == GS_PID_OPPONENT ? GS_LEAD_TARGET : GS_LEAD_ATTACKER; }
#endif
