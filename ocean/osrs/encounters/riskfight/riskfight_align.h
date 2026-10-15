#ifndef OSRS_RISKFIGHT_ALIGN_H
#define OSRS_RISKFIGHT_ALIGN_H
#include "riskfight_step.h"

/* Policy-agnostic counts of how one agent's swings line up with the other side's. Melee hits land on the swing
 * tick, so two swings on the same tick are two hits landing together ("aligned") and swings one tick apart are
 * "adjacent". Observe once per riskfight_step, after it; reset at each fight start. */
typedef struct {
    int swings, aligned, adjacent, spec_swings, spec_aligned, spec_adjacent;
    int stops, ready_idle, vengs, eats;
    int stop_spec, veng_spec, stop_veng_spec, spec_delay, norm_delay, idle_run;
    int our_last, his_last, our_last_spec, our_last_marked, last_stop, last_veng;
} RiskfightAlign;

static void riskfight_align_reset(RiskfightAlign* a) {
    memset(a, 0, sizeof(*a));
    a->our_last = a->his_last = a->last_stop = a->last_veng = -100;
}

static void riskfight_align_observe(const RiskfightState* s, int agent, int stop_issued, RiskfightAlign* a) {
    const Player* us = &s->env.players[agent];
    const Player* him = &s->env.players[1 - agent];
    int tick = s->env.tick;
    a->stops += stop_issued;
    a->vengs += us->cast_veng_this_tick;
    if (stop_issued) a->last_stop = tick;
    if (us->cast_veng_this_tick) a->last_veng = tick;
    a->eats += us->ate_food_this_tick || us->ate_karambwan_this_tick;
    int swung = us->attack_style_this_tick != ATTACK_STYLE_NONE;
    int his_swung = him->attack_style_this_tick != ATTACK_STYLE_NONE;
    int idle = !swung && us->current_hitpoints > 0 && him->current_hitpoints > 0 && us->attack_timer <= 0 && !s->escaped[0] && !s->escaped[1];
    if (idle && !his_swung) a->ready_idle++;
    a->idle_run += idle;
    if (his_swung && !swung && a->our_last == tick - 1 && !a->our_last_marked) {
        /* His swing follows ours by one tick. */
        a->adjacent++;
        a->spec_adjacent += a->our_last_spec;
        a->our_last_marked = 1;
    }
    if (swung) {
        int spec = us->used_special_this_tick;
        int exact = his_swung;
        int near = !exact && a->his_last == tick - 1;
        a->swings++;
        if (us->used_special_this_tick) a->spec_delay += a->idle_run; else a->norm_delay += a->idle_run;
        a->idle_run = 0;
        a->aligned += exact;
        a->adjacent += near;
        if (spec) {
            a->spec_swings++;
            a->spec_aligned += exact;
            a->spec_adjacent += near;
            int stopped = tick - a->last_stop <= 6, vengd = tick - a->last_veng <= 4;
            a->stop_spec += stopped;
            a->veng_spec += vengd;
            a->stop_veng_spec += stopped && vengd;
        }
        a->our_last = tick;
        a->our_last_spec = spec;
        a->our_last_marked = exact || near;
    }
    if (his_swung) a->his_last = tick;
}
#endif
