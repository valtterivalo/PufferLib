#include "ocean/osrs/osrs_env.h"
#include "ocean/osrs/tests/osrs_test_check.h"

int main(void) {
    for (int attack_clock = -1; attack_clock <= 2; attack_clock++) {
        Player player = {0};
        player.current_hitpoints = 40;
        player.base_hitpoints = 99;
        player.food_count = 1;
        player.has_attack_timer = attack_clock >= 0;
        player.attack_timer = attack_clock < 0 ? 0 : attack_clock - 1;
        int before_clock = player.attack_timer;
        int before_flag = player.has_attack_timer;
        OsrsPlayerEatResult result = osrs_player_eat_food_type(&player, FOOD_SHARK);
        CHECK("ready and cooling food consumption succeeds", result.consumed);
        ASSERT_INT_EQ("only existing positive cooldown is extended", player.attack_timer,
            before_clock > 0 ? before_clock + 3 : before_clock);
        ASSERT_INT_EQ("food preserves ready timer ownership", player.has_attack_timer, before_flag);
    }
    return osrs_test_summary();
}
