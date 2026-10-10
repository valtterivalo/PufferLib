#include <assert.h>
#include <math.h>
#include <stdlib.h>
#include "../osrs_env.h"

static int expected_damage(int roll, int dharok_base_hp, int attacker_hp,
    AttackStyle style, OverheadPrayer prayer) {
    int damage = roll;
    if (dharok_base_hp > attacker_hp)
        damage = damage * (10000 + dharok_base_hp *
            (dharok_base_hp - attacker_hp)) / 10000;
    if (damage == 0) damage = 1;
    int protected = (style == ATTACK_STYLE_MELEE && prayer == PRAYER_PROTECT_MELEE) ||
        (style == ATTACK_STYLE_RANGED && prayer == PRAYER_PROTECT_RANGED) ||
        (style == ATTACK_STYLE_MAGIC && prayer == PRAYER_PROTECT_MAGIC);
    return protected ? (int)(damage * 0.6f) : damage;
}

static void check_distribution(int min_hit, int max_hit, int dharok_base_hp,
    int attacker_hp, AttackStyle style, OverheadPrayer prayer) {
    int max_damage = expected_damage(max_hit, dharok_base_hp, attacker_hp, style, prayer);
    int* damage_counts = calloc((size_t)max_damage + 1, sizeof(*damage_counts));
    assert(damage_counts);
    int roll_count = max_hit - min_hit + 1;
    for (int roll = min_hit; roll <= max_hit; roll++) {
        int damage = expected_damage(roll, dharok_base_hp, attacker_hp, style, prayer);
        damage_counts[damage]++;
    }

    for (int accuracy_numerator = 0; accuracy_numerator <= 16; accuracy_numerator++) {
        float accuracy = (float)accuracy_numerator / 16.0f;
        for (int target_hp = 1; target_hp <= max_damage + 1; target_hp++) {
            int lethal_rolls = 0;
            for (int damage = target_hp; damage <= max_damage; damage++)
                lethal_rolls += damage_counts[damage];
            float expected = accuracy * (float)lethal_rolls / (float)roll_count;
            float actual = osrs_direct_ko_probability(min_hit, max_hit, accuracy,
                target_hp, dharok_base_hp, attacker_hp, prayer, style);
            assert(fabsf(actual - expected) < 0.000001f);
        }
    }
    free(damage_counts);
}

int main(void) {
    for (int style = ATTACK_STYLE_MELEE; style <= ATTACK_STYLE_MAGIC; style++) {
        OverheadPrayer protect = style == ATTACK_STYLE_MELEE ? PRAYER_PROTECT_MELEE
            : style == ATTACK_STYLE_RANGED ? PRAYER_PROTECT_RANGED : PRAYER_PROTECT_MAGIC;
        check_distribution(0, 60, 0, 99, (AttackStyle)style, PRAYER_NONE);
        check_distribution(20, 60, 0, 99, (AttackStyle)style, protect);
    }
    for (int attacker_hp = 1; attacker_hp <= 99; attacker_hp += 49) {
        check_distribution(0, 60, 99, attacker_hp,
            ATTACK_STYLE_MELEE, PRAYER_NONE);
        check_distribution(0, 60, 99, attacker_hp,
            ATTACK_STYLE_MELEE, PRAYER_PROTECT_MELEE);
    }
    return 0;
}
