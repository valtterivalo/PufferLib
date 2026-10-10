#include "../osrs_env.h"
#include "osrs_test_check.h"

static uint32_t reference_next(uint32_t* state) {
    *state ^= *state << 13;
    *state ^= *state >> 17;
    *state ^= *state << 5;
    return *state;
}

static int reference_hit(uint32_t roll, int attack, int defence) {
    uint64_t numerator = attack > defence ? 2ull * attack - defence : (uint64_t)attack;
    uint64_t denominator = attack > defence ? 2ull * (attack + 1) : 2ull * (defence + 1);
    return (roll & 65535) * denominator < numerator * 65536;
}

int main(void) {
    const struct {
        int weapon, attack_num, attack_den, max_num, max_den, defence_den, min_den, hits, fixed_max;
    } cases[] = {
        {ITEM_AGS, 2, 1, 11, 8, 1, 0, 1, 0},
        {ITEM_STATIUS_WARHAMMER, 5, 4, 5, 4, 1, 0, 1, 0},
        {ITEM_BGS, 2, 1, 121, 100, 1, 0, 1, 0},
        {ITEM_ZGS, 2, 1, 11, 10, 1, 0, 1, 0},
        {ITEM_SGS, 2, 1, 11, 10, 1, 0, 1, 0},
        {ITEM_ANCIENT_GS, 2, 1, 11, 10, 1, 0, 1, 0},
        {ITEM_VESTAS, 1, 1, 6, 5, 4, 5, 1, 0},
        {ITEM_GRANITE_MAUL, 1, 1, 1, 1, 1, 0, 1, 0},
        {ITEM_GRANITE_MAUL_ORNATE, 1, 1, 1, 1, 1, 0, 1, 0},
        {ITEM_DRAGON_DAGGER, 23, 20, 23, 20, 1, 0, 2, 0},
        {ITEM_ELDER_MAUL, 5, 4, 1, 1, 1, 0, 1, 0},
        {ITEM_TOXIC_BLOWPIPE, 2, 1, 3, 2, 1, 0, 1, 0},
        {ITEM_MAGIC_SHORTBOW_I, 10, 7, 1, 1, 1, 0, 2, 0},
        {ITEM_HEAVY_BALLISTA, 5, 4, 5, 4, 1, 0, 1, 0},
        {ITEM_ZARYTE_CROSSBOW, 2, 1, 1, 1, 1, 0, 1, 0},
        {ITEM_MORRIGANS_JAVELIN, 1, 1, 6, 5, 4, 5, 1, 0},
        {ITEM_ARMADYL_CROSSBOW, 2, 1, 1, 1, 1, 0, 1, 0},
        {ITEM_VOLATILE_STAFF, 3, 2, 1, 1, 1, 0, 1, 58},
        {ITEM_EYE_OF_AYAK, 2, 1, 13, 10, 1, 0, 1, 0},
    };
    for (size_t c = 0; c < sizeof(cases) / sizeof(cases[0]); c++) {
        int damage_matches = 1, rng_matches = 1, zero_draws = 0, misses = 0, positive_draws = 0;
        for (int max_hit = 1; max_hit <= 40; max_hit += 39) {
            int attack = 20000 * cases[c].attack_num / cases[c].attack_den;
            int defence = 12000 / cases[c].defence_den;
            int maximum = cases[c].fixed_max ? cases[c].fixed_max
                : max_hit * cases[c].max_num / cases[c].max_den;
            int minimum = cases[c].min_den ? max_hit / cases[c].min_den : 0;
            for (uint32_t seed = 1; seed <= 65535; seed++) {
                uint32_t expected_rng = seed, actual_rng = seed;
                int expected[2] = {0}, total = 0;
                for (int hit = 0; hit < cases[c].hits; hit++) {
                    int accurate = reference_hit(reference_next(&expected_rng), attack, defence);
                    if (accurate) {
                        int raw = minimum + reference_next(&expected_rng) % (maximum - minimum + 1);
                        expected[hit] = raw == 0 ? 1 : raw;
                        zero_draws += raw == 0;
                        positive_draws += raw > 0;
                    } else misses++;
                    total += expected[hit];
                }
                SpecResult result = osrs_resolve_spec(cases[c].weapon, 20000, max_hit, 12000, 99, &actual_rng);
                damage_matches &= result.num_hits == cases[c].hits && result.total_damage == total;
                for (int hit = 0; hit < cases[c].hits; hit++) damage_matches &= result.damage[hit] == expected[hit];
                rng_matches &= actual_rng == expected_rng;
            }
        }
        printf("weapon=%d accurate_zero=%d misses=%d positive=%d\n", cases[c].weapon, zero_draws, misses, positive_draws);
        CHECK("ordinary special boosts passed-accuracy zero and preserves misses/positive damage", damage_matches);
        CHECK("ordinary special keeps original conditional RNG draw order", rng_matches);
        CHECK("ordinary special fixture exercises accurate zero, miss and positive", zero_draws && misses && positive_draws);
    }

    int blowpipe_matches = 1, blowpipe_rng_matches = 1, blowpipe_zeros = 0, blowpipe_misses = 0;
    for (uint32_t seed = 1; seed <= 65535; seed++) {
        uint32_t actual_rng = seed, expected_rng = seed;
        int accurate = reference_hit(reference_next(&expected_rng), 40000, (99 + 9) * (50 + 64));
        int expected = 0;
        if (accurate) {
            int raw = reference_next(&expected_rng) % 61;
            expected = raw == 0 ? 1 : raw;
            blowpipe_zeros += raw == 0;
        } else blowpipe_misses++;
        int actual = osrs_blowpipe_spec_resolve(20000, 40, 99, 50, &actual_rng);
        blowpipe_matches &= actual == expected;
        blowpipe_rng_matches &= actual_rng == expected_rng;
    }
    CHECK("Inferno blowpipe helper boosts accurate zero without changing misses", blowpipe_matches && blowpipe_zeros && blowpipe_misses);
    CHECK("Inferno blowpipe helper preserves RNG order", blowpipe_rng_matches);

    OsrsPreparedAttackEffects prepared = { .attack_roll = 10000000, .min_hit = 0, .max_hit = 0 };
    int prepared_matches = 1, prepared_rng_matches = 1, npc_zero = 0;
    for (uint32_t seed = 1; seed <= 65535; seed++) {
        uint32_t actual_rng = seed, expected_rng = seed;
        reference_next(&expected_rng);
        reference_next(&expected_rng);
        prepared_matches &= osrs_roll_prepared_attack_damage(&prepared, 0, 0, &actual_rng) == 1;
        prepared_rng_matches &= actual_rng == expected_rng;
        actual_rng = seed;
        npc_zero |= encounter_npc_roll_attack(10000000, 0, 0, &actual_rng) == 0;
    }
    CHECK("damaging Scythe tiny splat promotes accurate zero even when splat max is zero", prepared_matches);
    CHECK("prepared player damage keeps damage-before-accuracy draws", prepared_rng_matches);
    CHECK("NPC attacks keep their separate zero damage rule", npc_zero);
    prepared.attack_roll = 0;
    uint32_t missed_rng = 1;
    CHECK("prepared player misses stay zero", osrs_roll_prepared_attack_damage(&prepared, 10000, 0, &missed_rng) == 0);

    static OsrsEnv env;
    for (int actor = 0; actor < NUM_AGENTS; actor++) {
        init_player(&env.players[actor]);
        memset(env.players[actor].equipped, ITEM_NONE, sizeof(env.players[actor].equipped));
        env.players[actor].x = FIGHT_AREA_BASE_X + 10 + actor;
        env.players[actor].y = FIGHT_AREA_BASE_Y + 10;
        env.players[actor].equipped[GEAR_SLOT_WEAPON] = actor ? ITEM_WHIP : ITEM_GRANITE_MAUL_ORNATE;
        osrs_refresh_player_equipment(&env.players[actor]);
        pvp_refresh_visible_gear(&env.players[actor]);
    }
    env.players[0].offensive_prayer = OFFENSIVE_PRAYER_PIETY;
    env.players[0].fight_style = FIGHT_STYLE_ACCURATE;
    env.players[0].current_gear = GEAR_SPEC;
    env.players[1].prayer = PRAYER_PROTECT_MELEE;
    env.rng_state = 971212956;
    perform_attack(&env, 0, 1, ATTACK_STYLE_MELEE, 1, 0, 1);
    CHECK("actual maul accurate zero queues raw one with success", env.players[0].num_pending_hits == 1 &&
        env.players[0].pending_hits[0].damage == 1 && env.players[0].pending_hits[0].hit_success == 1);
    pvp_process_incoming_hits(&env, 1);
    CHECK("protection may round successful raw one to landed zero", env.players[1].hit_damage == 0 &&
        env.players[1].current_hitpoints == 99 && env.players[0].num_pending_hits == 0);
    return osrs_test_summary();
}
