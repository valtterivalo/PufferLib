#include "../osrs_env.h"
#include "osrs_test_check.h"

static OsrsEnv env;

static void reset(uint8_t weapon, int distance, int energy, int armed) {
    memset(&env, 0, sizeof(env));
    env.tick = 10;
    env.rng_state = 12345;
    for (int i = 0; i < NUM_AGENTS; i++) {
        init_player(&env.players[i]);
        memset(env.players[i].equipped, ITEM_NONE, sizeof(env.players[i].equipped));
        env.players[i].x = FIGHT_AREA_BASE_X + 10 + i * distance;
        env.players[i].y = FIGHT_AREA_BASE_Y + 10;
        env.players[i].frozen_ticks = 1;
        osrs_refresh_player_equipment(&env.players[i]);
    }
    slot_equip_item(&env.players[0], GEAR_SLOT_WEAPON, weapon);
    env.players[0].special_energy = energy;
    env.players[0].spec_armed = armed;
}

int main(void) {
    int magic_range = get_item(ITEM_VOLATILE_STAFF)->attack_range;
    const struct {
        const char* name;
        uint8_t weapon;
        int distance, energy, armed, special_action, spell;
        AttackStyle style;
        int expected_energy, expected_armed, special, magic_type;
    } cases[] = {
        {"Volatile special at magic range", ITEM_VOLATILE_STAFF, magic_range, 100, 0, 1, OSRS_SPELL_NONE, ATTACK_STYLE_MAGIC, 45, 0, 1, 0},
        {"Volatile special in melee range", ITEM_VOLATILE_STAFF, 1, 100, 0, 1, OSRS_SPELL_NONE, ATTACK_STYLE_MAGIC, 45, 0, 1, 0},
        {"Volatile retains already armed special", ITEM_VOLATILE_STAFF, magic_range, 55, 1, 0, OSRS_SPELL_NONE, ATTACK_STYLE_MAGIC, 0, 0, 1, 0},
        {"Volatile special outside magic range", ITEM_VOLATILE_STAFF, magic_range + 1, 100, 0, 1, OSRS_SPELL_NONE, ATTACK_STYLE_NONE, 100, 1, 0, 0},
        {"Unarmed Volatile whacks", ITEM_VOLATILE_STAFF, 1, 100, 0, 0, OSRS_SPELL_NONE, ATTACK_STYLE_MELEE, 100, 0, 0, 0},
        {"Unarmed Volatile cannot whack at range", ITEM_VOLATILE_STAFF, 2, 100, 0, 0, OSRS_SPELL_NONE, ATTACK_STYLE_NONE, 100, 0, 0, 0},
        {"Unfunded Volatile whacks", ITEM_VOLATILE_STAFF, 1, 54, 0, 1, OSRS_SPELL_NONE, ATTACK_STYLE_MELEE, 54, 0, 0, 0},
        {"Unfunded armed Volatile whacks", ITEM_VOLATILE_STAFF, 1, 54, 1, 0, OSRS_SPELL_NONE, ATTACK_STYLE_MELEE, 54, 1, 0, 0},
        {"Cancelled Volatile whacks", ITEM_VOLATILE_STAFF, 1, 100, 1, 2, OSRS_SPELL_NONE, ATTACK_STYLE_MELEE, 100, 0, 0, 0},
        {"Ordinary staff whacks", ITEM_AHRIM_STAFF, 1, 100, 0, 1, OSRS_SPELL_NONE, ATTACK_STYLE_MELEE, 100, 0, 0, 0},
        {"Ice barrage precedes special", ITEM_VOLATILE_STAFF, 9, 100, 0, 1, OSRS_SPELL_ICE_BARRAGE, ATTACK_STYLE_MAGIC, 100, 1, 0, 1},
        {"Blood barrage precedes special", ITEM_VOLATILE_STAFF, 9, 100, 0, 1, OSRS_SPELL_BLOOD_BARRAGE, ATTACK_STYLE_MAGIC, 100, 1, 0, 2},
        {"Voidwaker special stays magic", ITEM_VOIDWAKER, 1, 100, 0, 1, OSRS_SPELL_NONE, ATTACK_STYLE_MAGIC, 50, 0, 1, 0},
        {"Voidwaker ordinary attack stays melee", ITEM_VOIDWAKER, 1, 100, 0, 0, OSRS_SPELL_NONE, ATTACK_STYLE_MELEE, 100, 0, 0, 0},
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(*cases); i++) {
        printf("%s\n", cases[i].name);
        reset(cases[i].weapon, cases[i].distance, cases[i].energy, cases[i].armed);
        int actions[OSRS_BASE_NUM_ACTION_HEADS] = {0};
        actions[OSRS_HEAD_PRIMARY] = OSRS_PRIMARY_MOVE_ACTIONS;
        actions[OSRS_HEAD_SPECIAL] = cases[i].special_action;
        actions[OSRS_HEAD_SPELL] = cases[i].spell;
        execute_switches(&env, 0, actions, NULL);
        execute_attack_combat(&env, 0, actions, NULL, NULL);
        Player* player = &env.players[0];
        CHECK("attack execution", player->just_attacked == (cases[i].style != ATTACK_STYLE_NONE));
        CHECK("executed damage style", player->last_attack_style == cases[i].style);
        CHECK("special energy", player->special_energy == cases[i].expected_energy);
        CHECK("special arm state", player->spec_armed == cases[i].expected_armed);
        CHECK("special executed", player->used_special_this_tick == cases[i].special);
        CHECK("spell priority", player->magic_type_this_tick == cases[i].magic_type);
        if (player->just_attacked) {
            CHECK("hit queued", player->num_pending_hits == 1);
            CHECK("queued damage style", player->pending_hits[0].attack_type == cases[i].style);
            CHECK("queued special", player->pending_hits[0].is_special == cases[i].special);
        }
    }
    int raw_damage = 0;
    for (int protect = 0; protect < 2; protect++) {
        reset(ITEM_VOLATILE_STAFF, 9, 100, 0);
        env.players[1].prayer = protect ? PRAYER_PROTECT_MAGIC : PRAYER_NONE;
        int actions[OSRS_BASE_NUM_ACTION_HEADS] = {0};
        actions[OSRS_HEAD_PRIMARY] = OSRS_PRIMARY_MOVE_ACTIONS;
        actions[OSRS_HEAD_SPECIAL] = 1;
        execute_switches(&env, 0, actions, NULL);
        execute_attack_combat(&env, 0, actions, NULL, NULL);
        Player* player = &env.players[0];
        CHECK("magic special queues one hit", player->num_pending_hits == 1);
        assert(player->num_pending_hits == 1);
        PendingHit hit = player->pending_hits[0];
        CHECK("protection captured at attack", hit.defender_prayer_at_attack == env.players[1].prayer);
        if (!protect) raw_damage = hit.damage;
        CHECK("protection does not alter queued raw damage", hit.damage == raw_damage);
        env.players[1].prayer = protect ? PRAYER_NONE : PRAYER_PROTECT_MAGIC;
        for (int tick = 0; tick <= hit.ticks_until_hit; tick++) pvp_process_incoming_hits(&env, 1);
        CHECK("landed protection follows attack-time prayer", env.players[1].hit_was_on_prayer == protect);
        CHECK("landed damage follows magic protection", env.players[1].hit_damage == (protect ? (int)(raw_damage * 0.6f) : raw_damage));
    }
    return osrs_test_summary();
}
