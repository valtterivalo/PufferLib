#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#include "ocean/osrs/osrs_types.h"
#include "ocean/osrs/osrs_entity_priority.h"

static uint32_t counting_random(uint32_t* state) {
    return (*state)++;
}

static void test_interval_endpoints(void) {
    uint32_t draw = 0;
    assert(osrs_entity_priority_interval(OSRS_PRIORITY_PVP_WORLD, &draw, counting_random) == 40);
    draw = 20;
    assert(osrs_entity_priority_interval(OSRS_PRIORITY_PVP_WORLD, &draw, counting_random) == 60);
    draw = 0;
    assert(osrs_entity_priority_interval(OSRS_PRIORITY_STANDARD, &draw, counting_random) == 100);
    draw = 50;
    assert(osrs_entity_priority_interval(OSRS_PRIORITY_STANDARD, &draw, counting_random) == 150);
}

static void test_refresh(OsrsPriorityPolicy policy, int minimum, int maximum) {
    uint32_t seed = 0x951781a3;
    OsrsEntityPriority priority;
    osrs_entity_priority_init(&priority, policy, &seed, xorshift32);
    for (int refresh = 0; refresh < 1000; refresh++) {
        int interval = priority.shuffle_ticks;
        uint32_t rank = priority.rank;
        uint32_t initial_seed = seed;
        assert(interval >= minimum && interval <= maximum);
        for (int tick = 1; tick < interval; tick++) {
            osrs_entity_priority_tick(&priority, policy, &seed, xorshift32);
            assert(priority.rank == rank);
            assert(priority.shuffle_ticks == interval - tick);
            assert(seed == initial_seed);
        }
        uint32_t expected_seed = seed;
        OsrsEntityPriority expected;
        osrs_entity_priority_init(&expected, policy, &expected_seed, xorshift32);
        osrs_entity_priority_tick(&priority, policy, &seed, xorshift32);
        assert(priority.rank == expected.rank);
        assert(priority.shuffle_ticks == expected.shuffle_ticks);
        assert(seed == expected_seed);
    }
}

static void test_independent_refresh_and_replay(void) {
    uint32_t seed = 17351;
    uint32_t replay_seed = seed;
    OsrsEntityPriority players[2], replay[2];
    for (int i = 0; i < 2; i++) {
        osrs_entity_priority_init(&players[i], OSRS_PRIORITY_PVP_WORLD, &seed, xorshift32);
        osrs_entity_priority_init(&replay[i], OSRS_PRIORITY_PVP_WORLD, &replay_seed, xorshift32);
    }
    int retained = 0, changed = 0, single_refreshes = 0;
    for (int tick = 0; tick < 10000; tick++) {
        int before = osrs_entity_priority_compare(&players[0], 0, &players[1], 1);
        int refreshed = 0;
        for (int i = 0; i < 2; i++) {
            refreshed += players[i].shuffle_ticks == 1;
            osrs_entity_priority_tick(&players[i], OSRS_PRIORITY_PVP_WORLD, &seed, xorshift32);
            osrs_entity_priority_tick(&replay[i], OSRS_PRIORITY_PVP_WORLD, &replay_seed, xorshift32);
            assert(players[i].rank == replay[i].rank);
            assert(players[i].shuffle_ticks == replay[i].shuffle_ticks);
        }
        assert(seed == replay_seed);
        int after = osrs_entity_priority_compare(&players[0], 0, &players[1], 1);
        if (refreshed == 1) single_refreshes++;
        if (refreshed) {
            retained += before == after;
            changed += before != after;
        } else {
            assert(before == after);
        }
    }
    assert(single_refreshes > 0 && retained > 0 && changed > 0);
}

static void test_fixed_and_ties(void) {
    uint32_t seed = 73;
    OsrsEntityPriority fixed;
    osrs_entity_priority_init(&fixed, OSRS_PRIORITY_FIXED, &seed, xorshift32);
    uint32_t rank = fixed.rank, unchanged_seed = seed;
    for (int tick = 0; tick < 1000; tick++)
        osrs_entity_priority_tick(&fixed, OSRS_PRIORITY_FIXED, &seed, xorshift32);
    assert(fixed.rank == rank && fixed.shuffle_ticks == 0 && seed == unchanged_seed);
    assert(osrs_entity_priority_compare(&fixed, 0, &fixed, 1) == -1);
    assert(osrs_entity_priority_compare(&fixed, 1, &fixed, 0) == 1);
    assert(osrs_entity_priority_compare(&fixed, 1, &fixed, 1) == 0);
}

int main(void) {
    test_interval_endpoints();
    test_refresh(OSRS_PRIORITY_STANDARD, 100, 150);
    test_refresh(OSRS_PRIORITY_PVP_WORLD, 40, 60);
    test_independent_refresh_and_replay();
    test_fixed_and_ties();
    puts("Entity priority checks passed");
    return 0;
}
