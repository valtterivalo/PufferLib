#ifndef RISKFIGHT_OBSERVER_STATE_H
#define RISKFIGHT_OBSERVER_STATE_H
#include "../gearswapper/gs_infer.h"
#include "../gearswapper/gs_stakes.h"

enum { OBSERVE_HITS = 24 };

typedef struct { int source, target, kind, damage; } ObservedHit;

typedef struct {
    GsInfer inf;
    int bar_hit_tick, bar_ratio, hp_xp, xp_tenths;
    int rival_worn[48], rival_worn_count, rival_tracked;
    int interacting[2];
    int units[3];
    int hit_count;
    ObservedHit hits[OBSERVE_HITS];
    int events;
    GsStakes stakes;
} Observer;

static void observer_clear(Observer* observer) {
    gs_infer_clear(&observer->inf);
}

static void observer_assign(Observer* out, const Observer* source) {
    if (out == source) return;
    observer_clear(out);
    *out = *source;
    if (out->inf.health) out->inf.health->references++;
}

#endif
