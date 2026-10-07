#ifndef RISKFIGHT_OURS_STATE_H
#define RISKFIGHT_OURS_STATE_H
#include "../gearswapper/gs_rules.h"

enum { OURS_PENDING = 48 };
typedef struct { int due; HumanCommand command; } OursPending;

/* What our side remembers between decisions: the clicks of its run still to execute, and the spec decisions it played. */
typedef struct {
    Arena arena;
    OursPending pending[OURS_PENDING];
    int pending_count, busy_until, vengeance_clicked;
    int last_decision;
    long decisions, ticks;
    int played_count, last_played_step, last_played_choice;
    struct { int step; double chance; int his_hitpoints; double expected, plan_mean, pit, bar_mid; int preempted, plan_land, plan_last, choice; } played[64];
    uint32_t rng;
    int ideal_timing, equal_stakes, truth;
} Ours;

static void ours_init(Ours* ours) {
    memset(ours, 0, sizeof(*ours));
    ours->arena = arena_make((size_t)1 << 27);
    ours->last_decision = INT_MIN / 2;
    ours->last_played_step = INT_MIN / 2;
    ours->vengeance_clicked = INT_MIN / 2;
}

static void ours_reset(Ours* ours, uint32_t rng, int ideal_timing, int equal_stakes, int truth) {
    ours->truth = truth;
    ours->ideal_timing = ideal_timing;
    ours->equal_stakes = equal_stakes;
    ours->pending_count = 0;
    ours->busy_until = INT_MIN / 2;
    ours->vengeance_clicked = INT_MIN / 2;
    ours->last_decision = INT_MIN / 2;
    ours->played_count = 0;
    ours->last_played_step = INT_MIN / 2;
    ours->last_played_choice = -1;
    ours->rng = rng | 1;
}
#endif
