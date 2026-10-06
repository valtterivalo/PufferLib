#ifndef RISKFIGHT_OURS_STATE_H
#define RISKFIGHT_OURS_STATE_H
#include "../gearswapper/gs_rules.h"

/* What the ported rules remember between decisions of our side. */
typedef struct {
    Arena arena;
    int eat_added, last_swing_seen, seen_weapons;
    int stage;
    int stage_choice;
    int last_decision;
    long decisions, ticks;
    int played_count, last_played_step, last_played_choice;
    struct { int step; double chance; } played[64];
} Ours;

static void ours_init(Ours* ours) {
    memset(ours, 0, sizeof(*ours));
    ours->arena = arena_make((size_t)1 << 27);
    ours->last_swing_seen = -1;
    ours->last_decision = INT_MIN / 2;
    ours->last_played_step = INT_MIN / 2;
}

#endif
