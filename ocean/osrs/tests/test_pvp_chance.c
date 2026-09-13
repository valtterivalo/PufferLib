#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include "../osrs_env.h"
static void test_probability_against_damage_enumeration(void) {
    for (int hp = 1; hp <= 121; hp++) {
        for (int attacker_hp = 1; attacker_hp <= 121; attacker_hp += 10) {
            for (int prayer = PRAYER_NONE; prayer <= PRAYER_PROTECT_MELEE; prayer++) {
                int lethal = 0;
                for (int roll = 0; roll <= 60; roll++) {
                    int damage = osrs_dharok_max_hit(roll, 99, attacker_hp);
                    damage = osrs_prayer_reduce_damage(damage, (OverheadPrayer)prayer, ATTACK_STYLE_MELEE, 1);
                    lethal += damage >= hp;
                }
                float probability = osrs_direct_ko_probability(0, 60, 0.5f, hp, 99,
                    attacker_hp, (OverheadPrayer)prayer, ATTACK_STYLE_MELEE);
                assert(fabsf(probability - 0.5f * lethal / 61) < 1e-6f);
            }
        }
    }
    assert(osrs_direct_ko_probability(20, 60, 1, 20, 0, 99, PRAYER_NONE, ATTACK_STYLE_MAGIC) == 1);
    assert(osrs_direct_ko_probability(20, 60, 1, 61, 0, 99, PRAYER_NONE, ATTACK_STYLE_MAGIC) == 0);
    assert(osrs_direct_ko_probability(20, 60, 1, 0, 0, 99, PRAYER_NONE, ATTACK_STYLE_MAGIC) == 0);
    assert(osrs_direct_ko_probability(0, 60, 0, 1, 0, 99, PRAYER_NONE, ATTACK_STYLE_MELEE) == 0);
    assert(fabsf(osrs_direct_ko_probability(20, 60, 1, 60, 0, 99, PRAYER_NONE, ATTACK_STYLE_MAGIC) - 1.0f / 41) < 1e-6f);
}

static void test_voidwaker_ignores_defence(void) {
    for (uint32_t seed = 1; seed <= 1000; seed++) {
        uint32_t weak_rng = seed, strong_rng = seed;
        SpecResult weak = osrs_resolve_spec(ITEM_VOIDWAKER, 0, 41, INT_MAX, 99, &weak_rng);
        SpecResult strong = osrs_resolve_spec(ITEM_VOIDWAKER, 100000, 41, 0, 1, &strong_rng);
        assert(weak.damage[0] >= 20 && weak.damage[0] <= 61);
        assert(weak.damage[0] == strong.damage[0] && weak_rng == strong_rng);
        assert(weak.num_hits == 1 && weak.total_damage == weak.damage[0]);
    }
}

int main(void) {
    test_probability_against_damage_enumeration();
    test_voidwaker_ignores_defence();
    puts("PvP direct KO probability contracts passed");
}
