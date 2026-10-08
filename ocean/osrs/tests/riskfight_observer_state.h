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

#endif
