#include "ocean/osrs/encounters/encounter_nh_pvp.h"
#include "ocean/osrs/tests/osrs_test_check.h"

static OsrsEnv env;

int main(void) {
    const struct {
        int hp, damage, percentage, flat, success, expected;
    } cases[] = {
        {60, 20, 25, 0, 1, 65},
        {98, 20, 25, 0, 1, 99},
        {99, 20, 25, 0, 1, 99},
        {110, 20, 25, 0, 1, 110},
        {110, 1, 25, 0, 1, 110},
        {60, 20, 50, 0, 1, 70},
        {110, 20, 50, 0, 1, 110},
        {60, 20, 0, 15, 1, 75},
        {98, 20, 0, 15, 1, 99},
        {110, 20, 0, 15, 1, 110},
        {60, 20, 25, 15, 1, 80},
        {98, 20, 25, 15, 1, 99},
        {110, 20, 25, 15, 1, 110},
        {0, 20, 25, 15, 1, 0},
        {60, 20, 25, 15, 0, 60},
    };
    for (unsigned i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        memset(&env, 0, sizeof(env));
        env.rng_state = 73;
        for (int actor = 0; actor < NUM_AGENTS; actor++) {
            init_player(&env.players[actor]);
            memset(env.players[actor].equipped, ITEM_NONE,
                sizeof(env.players[actor].equipped));
            osrs_refresh_player_equipment(&env.players[actor]);
        }
        Player* attacker = &env.players[0];
        Player* defender = &env.players[1];
        attacker->base_hitpoints = 99;
        attacker->current_hitpoints = cases[i].hp;
        defender->current_hitpoints = 99;
        queue_hit(0, 0, 1, attacker, defender, cases[i].damage,
            ATTACK_STYLE_MAGIC, 2, 0, cases[i].success, 0,
            cases[i].percentage, 0, 0, cases[i].flat);
        apply_damage(&env, 0, 1, &attacker->pending_hits[0]);
        printf("case=%u hp_before=%d hp_after=%d expected=%d\n",
            i, cases[i].hp, attacker->current_hitpoints, cases[i].expected);
        ASSERT_INT_EQ("healing restores missing HP without removing existing boost",
            attacker->current_hitpoints, cases[i].expected);
    }

    memset(&env, 0, sizeof(env));
    env.rng_state = 73;
    for (int actor = 0; actor < NUM_AGENTS; actor++) {
        init_player(&env.players[actor]);
        memset(env.players[actor].equipped, ITEM_NONE,
            sizeof(env.players[actor].equipped));
        osrs_refresh_player_equipment(&env.players[actor]);
        env.players[actor].base_hitpoints = 99;
        env.players[actor].current_hitpoints = actor == 0 ? 60 : 99;
    }
    env.players[1].prayer = PRAYER_PROTECT_MAGIC;
    queue_hit(0, 0, 1, &env.players[0], &env.players[1], 20,
        ATTACK_STYLE_MAGIC, 2, 0, 1, 0, 25, 0, 0, 0);
    env.players[1].prayer = PRAYER_PROTECT_MELEE;
    apply_damage(&env, 0, 1, &env.players[0].pending_hits[0]);
    ASSERT_INT_EQ("delayed Blood uses launch protection", env.players[1].hit_damage, 12);
    ASSERT_INT_EQ("Blood healing uses final protected damage", env.players[0].current_hitpoints, 63);
    ASSERT_INT_EQ("locked defender protection survives later switches",
        env.players[1].hit_defender_prayer, PRAYER_PROTECT_MAGIC);
    printf("RESULT: %d/%d passed\n", tests_passed, tests_run);
    return tests_failed != 0;
}
