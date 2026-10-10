#include "../osrs_env.h"
#include "osrs_test_check.h"

static OsrsEnv env;

int main(void) {
    const struct {
        uint8_t weapon;
        AttackStyle style;
        int spell, accurate;
        uint32_t seed, expected_rng;
    } cases[] = {
        {ITEM_WHIP, ATTACK_STYLE_MELEE, 0, 1, 10, 671267850},
        {ITEM_WHIP, ATTACK_STYLE_MELEE, 0, 0, 12800, 3460699424},
        {ITEM_BOW_OF_FAERDHINEN, ATTACK_STYLE_RANGED, 0, 1, 2, 134253570},
        {ITEM_BOW_OF_FAERDHINEN, ATTACK_STYLE_RANGED, 0, 0, 13312, 3599163200},
        {ITEM_KODAI_WAND, ATTACK_STYLE_MAGIC, 1, 1, 7, 470389255},
        {ITEM_KODAI_WAND, ATTACK_STYLE_MAGIC, 1, 0, 10752, 2906995872},
    };
    for (int actor = 0; actor < 2; actor++) {
        for (int type = ENTITY_PLAYER; type <= ENTITY_NPC; type++) {
            for (size_t i = 0; i < sizeof(cases) / sizeof(*cases); i++) {
                memset(&env, 0, sizeof(env));
                env.rng_state = cases[i].seed;
                for (int side = 0; side < 2; side++) {
                    init_player(&env.players[side]);
                    memset(env.players[side].equipped, ITEM_NONE, sizeof(env.players[side].equipped));
                    env.players[side].x = 3 + side;
                    env.players[side].y = 3;
                    osrs_refresh_player_equipment(&env.players[side]);
                }
                Player* attacker = &env.players[actor];
                attacker->entity_type = (EntityType)type;
                slot_equip_item(attacker, GEAR_SLOT_WEAPON, cases[i].weapon);
                perform_attack(&env, actor, 1 - actor, cases[i].style, 0, cases[i].spell, 1);
                const PendingHit* hit = &attacker->pending_hits[0];
                CHECK("natural accuracy outcome unchanged", hit->hit_success == cases[i].accurate);
                CHECK("accurate player zero becomes one, misses and NPC zero remain zero",
                    hit->damage == (type == ENTITY_PLAYER && cases[i].accurate ? 1 : 0));
                CHECK("normal minimum correction consumes no RNG", env.rng_state == cases[i].expected_rng);
            }
        }
        memset(&env, 0, sizeof(env));
        env.rng_state = 228;
        for (int side = 0; side < 2; side++) {
            init_player(&env.players[side]);
            memset(env.players[side].equipped, ITEM_NONE, sizeof(env.players[side].equipped));
            env.players[side].x = 3 + side;
            env.players[side].y = 3;
            osrs_refresh_player_equipment(&env.players[side]);
        }
        Player* attacker = &env.players[actor];
        slot_equip_item(attacker, GEAR_SLOT_WEAPON, ITEM_DRAGON_DAGGER);
        perform_attack(&env, actor, 1 - actor, ATTACK_STYLE_MELEE, 0, 0, 1);
        CHECK("DDS natural accurate zero now damages", attacker->pending_hits[0].hit_success &&
            attacker->pending_hits[0].damage == 1);
        CHECK("newly nonzero DDS hit eligible for poison", attacker->pending_hits[0].affliction == OSRS_AFFLICTION_POISON);
        uint32_t expected_rng = 2534089590;
        xorshift32(&expected_rng);
        CHECK("DDS adds only its newly eligible poison proc draw", env.rng_state == expected_rng);
        pvp_process_incoming_hits(&env, 1 - actor);
        CHECK("new one-damage DDS hit actually poisons", env.players[1 - actor].status.kind == OSRS_AFFLICTION_POISON);
    }
    return osrs_test_summary();
}
