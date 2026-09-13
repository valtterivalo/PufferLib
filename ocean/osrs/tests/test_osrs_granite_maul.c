#include <assert.h>
#include <stdio.h>

#include "ocean/osrs/osrs_granite_maul.h"

static void test_single_homing_and_boundaries(void) {
    for (int age = 0; age <= 6; age++) {
        OsrsGraniteMaulState state = osrs_granite_maul_init();
        state = osrs_granite_maul_record_attack(state, 20, 7);
        state = osrs_granite_maul_special_click(state, 20 + age);
        OsrsGraniteMaulResolution out = osrs_granite_maul_finish_inputs(state, 20 + age, 1);
        assert(out.requested_hits == (age <= 5));
        if (out.requested_hits) {
            assert(out.target == 7);
            assert(out.state.preparation == OSRS_GRANITE_MAUL_IDLE);
        }
        assert(osrs_granite_maul_finish_inputs(out.state, 20 + age, 1).requested_hits == 0);
    }
    OsrsGraniteMaulState state = osrs_granite_maul_special_click(osrs_granite_maul_init(), 0);
    assert(osrs_granite_maul_finish_inputs(state, 0, 1).requested_hits == 0);
    state = osrs_granite_maul_record_attack(state, 20, 7);
    state = osrs_granite_maul_weapon_changed(state);
    state = osrs_granite_maul_special_click(state, 20);
    OsrsGraniteMaulResolution out = osrs_granite_maul_finish_inputs(state, 20, 0);
    assert(out.requested_hits == 0);
    assert(out.state.preparation == OSRS_GRANITE_MAUL_SELECTED);
    assert(osrs_granite_maul_finish_inputs(out.state, 21, 1).requested_hits == 0);
    assert(osrs_granite_maul_target_click(out.state, 21, 7).requested_hits == 1);
}

static OsrsGraniteMaulState double_click(int tick) {
    OsrsGraniteMaulState state = osrs_granite_maul_record_attack(osrs_granite_maul_init(), tick, 7);
    state = osrs_granite_maul_special_click(state, tick);
    return osrs_granite_maul_special_click(state, tick);
}

static void test_double_and_third_click(void) {
    OsrsGraniteMaulState state = double_click(20);
    assert(state.preparation == OSRS_GRANITE_MAUL_DESELECTED);
    OsrsGraniteMaulResolution out = osrs_granite_maul_finish_inputs(state, 20, 1);
    assert(out.requested_hits == 0);
    out = osrs_granite_maul_target_click(out.state, 20, 9);
    assert(out.target == 9);
    assert(out.requested_hits == 2);
    assert(osrs_granite_maul_target_click(out.state, 20, 9).requested_hits == 0);
    state = osrs_granite_maul_special_click(double_click(20), 20);
    out = osrs_granite_maul_finish_inputs(state, 20, 1);
    assert(out.target == 7);
    assert(out.requested_hits == 2);
    assert(out.state.preparation == OSRS_GRANITE_MAUL_IDLE);
}

static void test_expiry_and_equipment(void) {
    for (int elapsed = 0; elapsed <= 4; elapsed++) {
        OsrsGraniteMaulResolution out = osrs_granite_maul_target_click(double_click(20), 20 + elapsed, 7);
        assert(out.requested_hits == (elapsed < 3 ? 2 : 0));
    }
    OsrsGraniteMaulState expired = osrs_granite_maul_special_click(double_click(20), 23);
    assert(expired.prepared_hits == 1);
    assert(osrs_granite_maul_finish_inputs(expired, 23, 1).requested_hits == 1);
    OsrsGraniteMaulState selected = osrs_granite_maul_special_click(osrs_granite_maul_init(), 20);
    assert(osrs_granite_maul_target_click(selected, 100, 9).requested_hits == 1);
    OsrsGraniteMaulState state = osrs_granite_maul_weapon_changed(double_click(20));
    assert(state.preparation == OSRS_GRANITE_MAUL_IDLE);
    assert(state.last_attack_target == 7);
    assert(state.last_attack_tick == 20);
    assert(osrs_granite_maul_target_click(state, 20, 7).requested_hits == 0);
    state = osrs_granite_maul_special_click(state, 21);
    assert(osrs_granite_maul_finish_inputs(state, 21, 1).requested_hits == 1);
    state = osrs_granite_maul_record_attack(state, 22, 9);
    assert(osrs_granite_maul_target_click(state, 22, 9).target == 9);
    state = osrs_granite_maul_record_special(state, 22);
    assert(state.last_special_tick == 22);
    assert(osrs_granite_maul_weapon_changed(state).last_special_tick == 22);
    assert(osrs_granite_maul_init().last_special_tick == -1);
}

static void test_pending_target(void) {
    OsrsGraniteMaulState state = osrs_granite_maul_init();
    assert(state.pending_target == -1);
    assert(osrs_granite_maul_queue_target(state, 20, 9).pending_target == -1);
    state = osrs_granite_maul_queue_target(double_click(20), 20, 9);
    assert(state.pending_target == 9);
    assert(state.preparation_expires_tick == 23);
    assert(osrs_granite_maul_expire(state, 22).pending_target == 9);
    assert(osrs_granite_maul_expire(state, 23).pending_target == -1);
    assert(osrs_granite_maul_weapon_changed(state).pending_target == -1);
    state = osrs_granite_maul_queue_target(state, 21, 7);
    assert(state.pending_target == 7);
    assert(state.preparation_expires_tick == 23);
    OsrsGraniteMaulResolution out = osrs_granite_maul_target_click(state, 22, state.pending_target);
    assert(out.target == 7);
    assert(out.requested_hits == 2);
    assert(out.state.pending_target == -1);
    state = osrs_granite_maul_queue_target(double_click(20), 23, 9);
    assert(state.pending_target == -1);
    assert(state.preparation == OSRS_GRANITE_MAUL_IDLE);
}

int main(void) {
    test_single_homing_and_boundaries();
    test_double_and_third_click();
    test_expiry_and_equipment();
    test_pending_target();
    puts("Granite maul click state tests passed");
    return 0;
}
