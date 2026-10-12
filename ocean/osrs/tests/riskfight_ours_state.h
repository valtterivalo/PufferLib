#ifndef RISKFIGHT_OURS_STATE_H
#define RISKFIGHT_OURS_STATE_H
#include "../gearswapper/gs_rules.h"
#include "riskfight_observer_state.h"
enum { LA_SCRIPT = 64 };
typedef struct Ours Ours;

enum { OURS_PENDING = 48 };
typedef struct { int due; HumanCommand command; } OursPending;
typedef struct {
    int n, due[LA_SCRIPT];
    HumanCommand command[LA_SCRIPT];
    int production, op[LA_SCRIPT], arg[LA_SCRIPT], fixed[LA_SCRIPT];
    int candidate_count[LA_SCRIPT], candidates[LA_SCRIPT][OSRS_INVENTORY_SIZE];
    int motor_active, motor_tick, motor_chain, motor_bound;
    double motor_ms;
    int attack_after, attack_tracking, registered, ready_index, ready_due, ready_sent;
    int aligned_active, aligned_tick;
    int spec_before, energy_before, spec_tracking;
    uint32_t timing_rng;
    const Ours* native_rules;
    const Observer* native_observer;
    int native_step;
} LaScript;

/* What our side remembers between decisions: queued clicks and spec decisions.
 * A burst confirms the first 50-energy spend before accepting nondecreasing regeneration;
 * burst_energy tracks subsequent observations so another spend invalidates the reservation. */
struct Ours {
    Arena arena;
    OursPending pending[OURS_PENDING];
    int pending_count, busy_until, vengeance_clicked;
    LaScript native_program;
    int native_active, native_cursor, native_resume;
    int last_decision;
    long decisions, ticks;
    int played_count, last_played_step, last_played_choice;
    int burst_pending, burst_started, burst_first, burst_spent, burst_spend_tick, burst_energy, burst_target;
    double burst_bar;
    struct { int step; double chance; int his_hitpoints; double expected, plan_mean, pit, bar_mid; int preempted, plan_land, plan_last, choice; } played[64];
    uint32_t rng;
    int ideal_timing, equal_stakes, truth, lookahead;
};

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
    ours->burst_pending = 0;
    memset(&ours->native_program, 0, sizeof(ours->native_program));
    ours->native_active = ours->native_cursor = ours->native_resume = 0;
    ours->busy_until = INT_MIN / 2;
    ours->vengeance_clicked = INT_MIN / 2;
    ours->last_decision = INT_MIN / 2;
    ours->played_count = 0;
    ours->last_played_step = INT_MIN / 2;
    ours->last_played_choice = -1;
    ours->rng = rng | 1;
}
#endif
