#include <assert.h>
#include <stdio.h>
#include "../encounters/encounter_riskfight.h"

int main(void) {
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
