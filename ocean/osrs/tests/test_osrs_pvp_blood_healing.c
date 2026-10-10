#include "../osrs_env.h"
#include "osrs_test_check.h"

static OsrsEnv env;

static void reset_case(int actor, int pid, int magic, int hp) {
    memset(&env, 0, sizeof(env));
    env.tick = 100;
    env.pid_holder = pid;
    env.rng_state = 12345;
    for (int i = 0; i < NUM_AGENTS; i++) {
        init_player(&env.players[i]);
        memset(env.players[i].equipped, ITEM_NONE, sizeof(env.players[i].equipped));
        env.players[i].x = FIGHT_AREA_BASE_X + 10 + i;
        env.players[i].y = FIGHT_AREA_BASE_Y + 10;
        env.players[i].frozen_ticks = 1;
        osrs_refresh_player_equipment(&env.players[i]);
    }
    Player* player = &env.players[actor];
    slot_equip_item(player, GEAR_SLOT_WEAPON, ITEM_AHRIM_STAFF);
    player->current_magic = magic;
    player->current_hitpoints = hp;
    player->is_lunar_spellbook = 0;
    player->has_attack_timer = 1;
    player->attack_timer = -1;
}

int main(void) {
    const int levels[] = {56, 67, 68, 79, 80, 91, 92, 99};
    for (int actor = 0; actor < NUM_AGENTS; actor++) {
        for (int pid = 0; pid < NUM_AGENTS; pid++) {
            for (size_t level = 0; level < sizeof(levels) / sizeof(*levels); level++) {
                reset_case(actor, pid, levels[level], 40);
                int actions[OSRS_BASE_NUM_ACTION_HEADS] = {0};
                actions[OSRS_HEAD_PRIMARY] = OSRS_PRIMARY_MOVE_ACTIONS;
                actions[OSRS_HEAD_SPELL] = OSRS_SPELL_BLOOD_BARRAGE;
                execute_attack_combat(&env, actor, actions, NULL);
                Player* player = &env.players[actor];
                CHECK("blood request launches at every legal tier", player->just_attacked && player->magic_type_this_tick == OSRS_MAGIC_ATTACK_ANCIENT_BLOOD);
                ASSERT_INT_EQ("blood cast queues one hit", player->num_pending_hits, 1);
                ASSERT_INT_EQ("every blood tier heals one quarter", player->pending_hits[0].heal_percent, 25);
                int damage = player->pending_hits[0].damage;
                int delay = player->pending_hits[0].ticks_until_hit;
                for (int tick = 0; tick <= delay; tick++) pvp_process_incoming_hits(&env, 1 - actor);
                ASSERT_INT_EQ("landed cast heals floor of one quarter", player->current_hitpoints, 40 + damage / 4);

                for (int roll = 0; roll <= 29; roll++) {
                    reset_case(actor, pid, levels[level], 40);
                    player = &env.players[actor];
                    queue_hit(env.tick, actor, 1 - actor, player, &env.players[1 - actor], roll,
                        ATTACK_STYLE_MAGIC, 0, 0, 1, 0, get_blood_heal_percent(levels[level]), 0, 0, 0);
                    pvp_process_incoming_hits(&env, 1 - actor);
                    ASSERT_INT_EQ("quarter healing integer boundaries", player->current_hitpoints, 40 + roll / 4);
                }
                reset_case(actor, pid, levels[level], 98);
                player = &env.players[actor];
                queue_hit(env.tick, actor, 1 - actor, player, &env.players[1 - actor], 20,
                    ATTACK_STYLE_MAGIC, 0, 0, 1, 0, get_blood_heal_percent(levels[level]), 0, 0, 0);
                pvp_process_incoming_hits(&env, 1 - actor);
                ASSERT_INT_EQ("blood healing cannot overheal", player->current_hitpoints, 99);
            }
        }
    }
    return osrs_test_summary();
}
