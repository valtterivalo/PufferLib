#ifndef OSRS_GRANITE_MAUL_H
#define OSRS_GRANITE_MAUL_H

#include <assert.h>

#define OSRS_GRANITE_MAUL_HOMING_TICKS 5
#define OSRS_GRANITE_MAUL_PREPARED_TICKS 3

typedef enum {
    OSRS_GRANITE_MAUL_IDLE,
    OSRS_GRANITE_MAUL_SELECTED,
    OSRS_GRANITE_MAUL_DESELECTED,
} OsrsGraniteMaulPreparation;

typedef struct {
    OsrsGraniteMaulPreparation preparation;
    int prepared_hits;
    int preparation_expires_tick;
    int homing_click_tick;
    int last_attack_target;
    int last_attack_tick;
    int last_special_tick;
    int pending_target;
} OsrsGraniteMaulState;

typedef struct {
    OsrsGraniteMaulState state;
    int target;
    int requested_hits;
} OsrsGraniteMaulResolution;

static inline OsrsGraniteMaulState osrs_granite_maul_init(void) {
    return (OsrsGraniteMaulState){
        .homing_click_tick = -1,
        .last_attack_target = -1,
        .last_special_tick = -1,
        .pending_target = -1,
    };
}

static inline OsrsGraniteMaulState osrs_granite_maul_clear_preparation(
    OsrsGraniteMaulState state
) {
    state.preparation = OSRS_GRANITE_MAUL_IDLE;
    state.prepared_hits = 0;
    state.preparation_expires_tick = 0;
    state.homing_click_tick = -1;
    state.pending_target = -1;
    return state;
}

static inline OsrsGraniteMaulState osrs_granite_maul_weapon_changed(
    OsrsGraniteMaulState state
) {
    return osrs_granite_maul_clear_preparation(state);
}

static inline OsrsGraniteMaulState osrs_granite_maul_record_attack(
    OsrsGraniteMaulState state, int tick, int target
) {
    assert(target >= 0);
    state.last_attack_target = target;
    state.last_attack_tick = tick;
    return state;
}

static inline OsrsGraniteMaulState osrs_granite_maul_record_special(
    OsrsGraniteMaulState state, int tick
) {
    state.last_special_tick = tick;
    return state;
}

static inline OsrsGraniteMaulState osrs_granite_maul_expire(
    OsrsGraniteMaulState state, int tick
) {
    if (state.preparation == OSRS_GRANITE_MAUL_DESELECTED &&
        tick >= state.preparation_expires_tick)
        return osrs_granite_maul_clear_preparation(state);
    return state;
}

static inline OsrsGraniteMaulState osrs_granite_maul_special_click(
    OsrsGraniteMaulState state, int tick
) {
    state = osrs_granite_maul_expire(state, tick);
    if (state.preparation == OSRS_GRANITE_MAUL_SELECTED) {
        state.preparation = OSRS_GRANITE_MAUL_DESELECTED;
        state.prepared_hits = 2;
        state.preparation_expires_tick = tick + OSRS_GRANITE_MAUL_PREPARED_TICKS;
        state.homing_click_tick = -1;
    } else {
        if (state.preparation == OSRS_GRANITE_MAUL_IDLE)
            state.prepared_hits = 1;
        state.preparation = OSRS_GRANITE_MAUL_SELECTED;
        state.homing_click_tick = tick;
    }
    return state;
}

static inline OsrsGraniteMaulState osrs_granite_maul_queue_target(
    OsrsGraniteMaulState state, int tick, int target
) {
    assert(target >= 0);
    state = osrs_granite_maul_expire(state, tick);
    if (state.prepared_hits > 0) state.pending_target = target;
    return state;
}

static inline OsrsGraniteMaulResolution osrs_granite_maul_target_click(
    OsrsGraniteMaulState state, int tick, int target
) {
    assert(target >= 0);
    state = osrs_granite_maul_expire(state, tick);
    return (OsrsGraniteMaulResolution){
        .state = osrs_granite_maul_clear_preparation(state),
        .target = target,
        .requested_hits = state.prepared_hits,
    };
}

static inline OsrsGraniteMaulResolution osrs_granite_maul_finish_inputs(
    OsrsGraniteMaulState state, int tick, int last_target_adjacent
) {
    state = osrs_granite_maul_expire(state, tick);
    int homing = state.preparation == OSRS_GRANITE_MAUL_SELECTED &&
        state.homing_click_tick == tick && state.last_attack_target >= 0 &&
        tick - state.last_attack_tick <= OSRS_GRANITE_MAUL_HOMING_TICKS &&
        last_target_adjacent;
    state.homing_click_tick = -1;
    return (OsrsGraniteMaulResolution){
        .state = homing ? osrs_granite_maul_clear_preparation(state) : state,
        .target = homing ? state.last_attack_target : -1,
        .requested_hits = homing ? state.prepared_hits : 0,
    };
}

#endif
