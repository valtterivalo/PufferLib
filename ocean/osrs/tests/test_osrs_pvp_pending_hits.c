/**
 * @file test_osrs_pvp_pending_hits.c
 * @brief PvP pending-hit queue contract tests.
 *
 * BUILD:
 *   cc -std=c11 -O0 -g -I. -o /tmp/test_osrs_pvp_pending_hits \
 *       ocean/osrs/tests/test_osrs_pvp_pending_hits.c -lm
 *   /tmp/test_osrs_pvp_pending_hits
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include "ocean/osrs/osrs_pvp_combat.h"

static int tests_run = 0;
static int tests_passed = 0;
static int tests_failed = 0;

#define ASSERT_INT_EQ(label, actual, expected) do { \
    tests_run++; \
    if ((actual) == (expected)) { \
        tests_passed++; \
    } else { \
        tests_failed++; \
        printf("  FAIL: %s - got %d, expected %d\n", \
            (label), (actual), (expected)); \
    } \
} while (0)

static void assert_child_aborts(const char* label, void (*fn)(void)) {
    fflush(NULL);
    pid_t pid = fork();
    if (pid == 0) {
        fn();
        _exit(0);
    }

    int status = 0;
    waitpid(pid, &status, 0);
    tests_run++;
    if (WIFSIGNALED(status) || (WIFEXITED(status) && WEXITSTATUS(status) != 0)) {
        tests_passed++;
    } else {
        tests_failed++;
        printf("  FAIL: %s - child returned successfully\n", label);
    }
}

static void queue_test_hit(Player* attacker, Player* defender, int damage) {
    queue_hit(123, 0, 1, attacker, defender, damage, ATTACK_STYLE_MAGIC,
        4, 0, damage > 0, 0, 0, 0, 0, 0);
}

static void test_pvp_queue_accepts_capacity(void) {
    printf("--- pvp queue accepts capacity ---\n");

    Player attacker = {0};
    Player defender = {0};
    defender.prayer = PRAYER_NONE;

    for (int i = 0; i < MAX_PENDING_HITS; i++)
        queue_test_hit(&attacker, &defender, i + 1);

    ASSERT_INT_EQ("queue reaches capacity", attacker.num_pending_hits, MAX_PENDING_HITS);
    ASSERT_INT_EQ("first damage kept", attacker.pending_hits[0].damage, 1);
    ASSERT_INT_EQ("last damage kept", attacker.pending_hits[MAX_PENDING_HITS - 1].damage,
        MAX_PENDING_HITS);
}

static void child_pvp_queue_overflow(void) {
    Player attacker = {0};
    Player defender = {0};
    defender.prayer = PRAYER_NONE;

    for (int i = 0; i < MAX_PENDING_HITS; i++)
        queue_test_hit(&attacker, &defender, i + 1);
    queue_test_hit(&attacker, &defender, 99);
}

static void test_pvp_queue_overflow_aborts(void) {
    printf("--- pvp queue overflow aborts ---\n");

    assert_child_aborts("pvp pending-hit overflow aborts", child_pvp_queue_overflow);
}

static void test_pvp_remove_compacts_and_clears_tail(void) {
    printf("--- pvp remove compacts and clears tail ---\n");

    Player attacker = {0};
    Player defender = {0};
    defender.prayer = PRAYER_NONE;
    queue_test_hit(&attacker, &defender, 3);
    queue_test_hit(&attacker, &defender, 7);
    queue_test_hit(&attacker, &defender, 11);

    pvp_remove_pending_hit(&attacker, 1);

    ASSERT_INT_EQ("queue count after remove", attacker.num_pending_hits, 2);
    ASSERT_INT_EQ("first hit remains", attacker.pending_hits[0].damage, 3);
    ASSERT_INT_EQ("third hit compacted", attacker.pending_hits[1].damage, 11);
    ASSERT_INT_EQ("tail damage cleared", attacker.pending_hits[2].damage, 0);
    ASSERT_INT_EQ("tail timer cleared", attacker.pending_hits[2].ticks_until_hit, 0);
}

int main(void) {
    test_pvp_queue_accepts_capacity();
    test_pvp_queue_overflow_aborts();
    test_pvp_remove_compacts_and_clears_tail();

    printf("\n%d/%d tests passed\n", tests_passed, tests_run);
    return tests_failed == 0 ? 0 : 1;
}
