#ifndef OSRS_POISON_H
#define OSRS_POISON_H
#include <limits.h>
#include <stdlib.h>
#include "osrs_types.h"

/* https://oldschool.runescape.wiki/w/Poison: severity loses one per 30 ticks,
 * damage is ceil(severity/5); Sanfew cures and grants six minutes immunity.
 * No modal-interface pauses are modeled in the active Riskfight cohort. */
enum { OSRS_POISON_PERIOD = 30, OSRS_SANFEW_IMMUNITY_TICKS = 600 };

static inline int osrs_poison_state_valid(OsrsPoisonState state) {
    return state.severity >= 0 && state.severity <= 100 && state.immunity_ticks >= 0 &&
        state.ticks_until_pulse >= 0 && state.ticks_until_pulse <= OSRS_POISON_PERIOD &&
        (!state.severity || (state.ticks_until_pulse > 0 && !state.immunity_ticks)) &&
        (state.severity || state.ticks_until_pulse == 0);
}

static inline void osrs_poison_cure(OsrsPoisonState* state, int immunity_ticks) {
    if (immunity_ticks < 0) abort();
    *state = (OsrsPoisonState){.immunity_ticks = immunity_ticks};
}

/* Deterministic state transition AFTER an independently resolved poison proc.
 * Callers must require positive resolved player damage and supply the known phase.
 * Unknown proc granularity or application phase is excluded at request ingress. */
static inline void osrs_poison_apply(OsrsPoisonState* state, int severity, int ticks_until_pulse) {
    if (severity < 1 || severity > 100 || ticks_until_pulse < 1 ||
        ticks_until_pulse > OSRS_POISON_PERIOD) abort();
    if (state->immunity_ticks > 0) return;
    state->severity = severity;
    state->ticks_until_pulse = ticks_until_pulse;
}

/* Advance existing immunity with other tick clocks, before either actor's
 * inputs. A cure applied this tick retains its complete new duration. */
static inline void osrs_poison_tick_immunity(OsrsPoisonState* state) {
    if (state->immunity_ticks > 0) state->immunity_ticks--;
}

/* Tick before this actor's incoming hits. Returned damage bypasses direct-hit
 * mitigation, Vengeance, recoil and Smite; the PvP damage observer still sees it. */
static inline int osrs_poison_tick(OsrsPoisonState* state) {
    if (!state->severity || --state->ticks_until_pulse > 0) return 0;
    int damage = (state->severity + 4) / 5;
    state->severity--;
    state->ticks_until_pulse = state->severity ? OSRS_POISON_PERIOD : 0;
    return damage;
}

/* RuneLite VarPlayer.POISON (102) documents negative values as immunity in
 * 30-tick units, including values below -38 (venom AND poison immunity).
 * Without the pulse phase only this lower bound is known, not exact expiry.
 * https://github.com/runelite/runelite/blob/master/runelite-api/src/main/java/net/runelite/api/VarPlayer.java */
static inline int osrs_poison_immunity_lower_bound(int varp) {
    if (varp >= 0 || varp == INT_MIN || varp < -INT_MAX / OSRS_POISON_PERIOD) abort();
    return (-varp - 1) * OSRS_POISON_PERIOD;
}
#endif
