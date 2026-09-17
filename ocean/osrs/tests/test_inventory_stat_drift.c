#include <assert.h>
#include <stdio.h>
#include "../encounters/encounter_riskfight.h"

static void test_divine_decay_hold(void) {
    // Divine holds 118 against decay but never snaps a brew drain back up:
    // drained stats wait for an explicit super-combat re-boost.
    Player divine;
    init_player(&divine);
    encounter_init_maxed_player_combat_stats(&divine, 99);
    encounter_super_combat_boost(&divine);
    OsrsInventoryUseState held = {0};
    held.divine_combat_ticks = OSRS_DIVINE_DURATION;
    encounter_brew_drain_stats(&divine);
    assert(divine.current_attack == 105 && divine.current_strength == 105);
    for (int tick = 0; tick < ENCOUNTER_STAT_DRIFT_TICKS; tick++)
        osrs_player_inventory_tick(&divine, &held);
    assert(divine.current_attack == 105 && divine.current_strength == 105);
    // A super-combat sip re-boosts without touching the divine clock.
    int ticks_before = held.divine_combat_ticks;
    encounter_super_combat_boost(&divine);
    assert(divine.current_attack == 118 && divine.current_strength == 118);
    assert(held.divine_combat_ticks == ticks_before);
    // An undrained 118 survives the 100-tick pulse while divine holds.
    Player fresh;
    init_player(&fresh);
    encounter_init_maxed_player_combat_stats(&fresh, 99);
    encounter_super_combat_boost(&fresh);
    OsrsInventoryUseState fresh_hold = {0};
    fresh_hold.divine_combat_ticks = OSRS_DIVINE_DURATION;
    for (int tick = 0; tick < ENCOUNTER_STAT_DRIFT_TICKS; tick++)
        osrs_player_inventory_tick(&fresh, &fresh_hold);
    assert(fresh.current_attack == 118 && fresh.current_strength == 118);
    // Expiry still snaps boosted stats to base.
    held.divine_combat_ticks = 1;
    osrs_player_inventory_tick(&divine, &held);
    assert(divine.current_attack == 99 && divine.current_strength == 99);
    assert(held.divine_combat_ticks == 0);
}

static void test_no_divine_decay(void) {
    // Without divine, a regular 118 pre-pot decays on the 100-tick pulse.
    Player plain;
    init_player(&plain);
    encounter_init_maxed_player_combat_stats(&plain, 99);
    encounter_super_combat_boost(&plain);
    OsrsInventoryUseState state = {0};
    for (int tick = 0; tick < ENCOUNTER_STAT_DRIFT_TICKS; tick++)
        osrs_player_inventory_tick(&plain, &state);
    assert(plain.current_attack == 117 && plain.current_strength == 117);
}
int main(void) {
    test_divine_decay_hold();
    test_no_divine_decay();
    const int starting_hp[] = {0, 1, 68, 98, 99, 100, 121};
    const int expected_hp[] = {0, 2, 69, 99, 99, 99, 120};
    for (size_t i = 0; i < sizeof(starting_hp) / sizeof(starting_hp[0]); i++) {
        Player player;
        init_player(&player);
        player.current_hitpoints = starting_hp[i];
        OsrsInventoryUseState state = {0};
        for (int tick = 1; tick < ENCOUNTER_STAT_DRIFT_TICKS; tick++) {
            osrs_player_inventory_tick(&player, &state);
            assert(player.current_hitpoints == starting_hp[i]);
        }
        osrs_player_inventory_tick(&player, &state);
        assert(state.stat_drift_timer == 0);
        assert(player.current_hitpoints == expected_hp[i]);
    }
    puts("Inventory HP drift runs once per pulse and never revives dead players");
}
