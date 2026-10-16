#define GS_RL_SERVER_NO_MAIN
#include "gs_rl_server.c"

/* The RL server's anglerfish overheal lock comes from the swings of his it was shown (V lines), not from the O line's his_last_ago estimate.
 * The swings of the live fight 20261016T141036 (corrected trace): 430, 440, 445, 450, 455, melee (offset by 400 here so the fight starts at tick 0). */

static RtFight fight;

/* Feeds ticks 0..last one at a time, swinging on `swings` (styles in `styles`), as rt_decide does: clear the ring slot, write the event, track the landing.
 * Returns the lock seen after each tick in lock[]. */
static void run(const int* swings, const int* styles, int count, int last, int* lock) {
    memset(&fight, 0, sizeof(fight));
    fight.land_pending[0] = fight.land_pending[1] = fight.last_land = -1;
    for (int t = 0; t <= last; t++) {
        float* event = fight.vis.events[t % RF_HISTORY_TICKS];
        memset(event, 0, RT_EVENT_WIDTH * sizeof(float));
        for (int i = 0; i < count; i++)
            if (swings[i] == t) { event[0] = 1; event[1] = ITEM_VOIDWAKER; event[2] = (float)styles[i]; }
        rt_track_landing(&fight, t + 1);
        lock[t] = rt_overheal_block(&fight, t + 1);
    }
}

static void test_melee_swings(void) {
    static const int swings[] = {30, 40, 45, 50, 55}, styles[] = {1, 1, 1, 1, 1};
    int lock[80];
    run(swings, styles, 5, 79, lock);
    for (int t = 0; t <= 30; t++) assert(lock[t] == 0);
    /* A melee swing at T lands at T + 1: 16 ticks of lock from T + 1 (16 at T + 1, 1 at T + 16), then none. */
    assert(lock[31] == 16 && lock[32] == 15 && lock[39] == 8 && lock[40] == 7);
    /* The swing at 40 lands at 41 and renews the lock before the first runs out; every swing renews it, so it never lapses until 16 ticks after the last landing. */
    for (int t = 31; t <= 71; t++) assert(lock[t] > 0);
    assert(lock[41] == 16 && lock[46] == 16 && lock[51] == 16 && lock[56] == 16);
    assert(lock[71] == 1 && lock[72] == 0 && lock[79] == 0);
}

/* The window 437..455 of the live fight (swings at 430 and every 5 ticks after) is locked at every tick. */
static void test_live_fight_window(void) {
    static const int swings[] = {15, 20, 30, 40, 45, 50, 55}, styles[] = {1, 1, 1, 1, 1, 1, 1};
    int lock[80];
    run(swings, styles, 7, 79, lock);
    for (int t = 37; t <= 55; t++) assert(lock[t] > 0);
}

static void test_ranged_lands_a_tick_later(void) {
    static const int swings[] = {10}, styles[] = {ATTACK_STYLE_MAGIC};
    int lock[40];
    run(swings, styles, 1, 39, lock);
    assert(lock[10] == 0 && lock[11] == 0 && lock[12] == 16 && lock[27] == 1 && lock[28] == 0);
}

int main(void) {
    test_melee_swings();
    test_live_fight_window();
    test_ranged_lands_a_tick_later();
    puts("rl server overheal lock passed");
    return 0;
}
