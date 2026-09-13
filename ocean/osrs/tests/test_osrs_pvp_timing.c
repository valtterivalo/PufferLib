#include <assert.h>
#include <stdio.h>
#include "../osrs_env.h"

static OsrsEnv env;

static void reset(void) {
    memset(&env, 0, sizeof(env));
    env.rng_state = 12345;
    for (int i = 0; i < 2; i++) {
        Player* player = &env.players[i];
        init_player(player);
        memset(player->equipped, ITEM_NONE, sizeof(player->equipped));
        player->current_hitpoints = 121;
        player->prayer = PRAYER_NONE;
        osrs_refresh_player_equipment(player);
    }
}

static void queue_damage(int source, int damage, AttackStyle style, int delay) {
    queue_hit(env.tick, source, 1 - source,
        &env.players[source], &env.players[1 - source], damage,
        style, delay, 0, damage > 0, 0, 0, 0, 0, 0);
}

static void equip_recoil(int actor) {
    env.players[actor].equipped[GEAR_SLOT_RING] = ITEM_RING_OF_RECOIL;
    osrs_refresh_player_equipment(&env.players[actor]);
}

static void test_melee_processing_order(void) {
    for (int first = 0; first < 2; first++) {
        reset();
        int second = 1 - first;
        pvp_process_incoming_hits(&env, first);
        queue_damage(first, 20, ATTACK_STYLE_MELEE, 0);
        assert(env.players[second].current_hitpoints == 121);
        pvp_process_incoming_hits(&env, second);
        assert(env.players[second].current_hitpoints == 101);
        queue_damage(second, 30, ATTACK_STYLE_MELEE, 0);
        assert(env.players[first].current_hitpoints == 121);
        assert(env.players[second].num_pending_hits == 1);
        env.tick++;
        pvp_process_incoming_hits(&env, first);
        assert(env.players[first].current_hitpoints == 91);
        pvp_process_incoming_hits(&env, second);
        assert(env.players[second].current_hitpoints == 101);
        assert(env.players[0].num_pending_hits == 0);
        assert(env.players[1].num_pending_hits == 0);
    }
}

static void test_base_delay_and_processing_order(void) {
    for (int first = 0; first < 2; first++) {
        reset();
        int second = 1 - first;
        pvp_process_incoming_hits(&env, first);
        queue_damage(first, 20, ATTACK_STYLE_MAGIC, 2);
        pvp_process_incoming_hits(&env, second);
        queue_damage(second, 30, ATTACK_STYLE_MAGIC, 2);
        for (int tick = 1; tick <= 3; tick++) {
            env.tick++;
            pvp_process_incoming_hits(&env, first);
            pvp_process_incoming_hits(&env, second);
            assert(env.players[second].current_hitpoints == (tick >= 2 ? 101 : 121));
            assert(env.players[first].current_hitpoints == (tick >= 3 ? 91 : 121));
        }
    }
}

static void assert_reflected_pair(int actor, int recoil, int vengeance) {
    Player* player = &env.players[actor];
    assert(player->num_pending_hits == 2);
    assert(player->pending_hits[0].kind == OSRS_HIT_RECOIL);
    assert(player->pending_hits[0].damage == recoil);
    assert(player->pending_hits[1].kind == OSRS_HIT_VENGEANCE);
    assert(player->pending_hits[1].damage == vengeance);
}

static void test_recorded_spec_exchange(void) {
    for (int mong = 0; mong < 2; mong++) {
        reset();
        int upthewazzoo = 1 - mong;
        env.tick = 89;
        equip_recoil(mong);
        equip_recoil(upthewazzoo);
        env.players[mong].veng_active = 1;
        env.players[upthewazzoo].veng_active = 1;
        queue_damage(upthewazzoo, 15, ATTACK_STYLE_MELEE, 0);
        pvp_process_incoming_hits(&env, mong);
        assert(env.players[mong].current_hitpoints == 106);
        assert_reflected_pair(mong, 2, 11);
        assert(env.players[upthewazzoo].current_hitpoints == 121);
        queue_damage(mong, 65, ATTACK_STYLE_MAGIC, 0);
        env.players[mong].pending_hits[2].is_special = 1;
        pvp_process_incoming_hits(&env, upthewazzoo);
        assert(env.players[upthewazzoo].current_hitpoints == 43);
        assert(!env.players[mong].veng_active);
        assert(!env.players[upthewazzoo].veng_active);
        assert_reflected_pair(upthewazzoo, 7, 48);
        assert(env.players[mong].item_effect_state.recoil_charges == 38);
        assert(env.players[upthewazzoo].item_effect_state.recoil_charges == 33);
        env.tick++;
        pvp_process_incoming_hits(&env, mong);
        assert(env.players[mong].current_hitpoints == 51);
        queue_damage(mong, 0, ATTACK_STYLE_MELEE, 0);
        env.players[mong].pending_hits[0].is_special = 1;
        pvp_process_incoming_hits(&env, upthewazzoo);
        assert(env.players[upthewazzoo].current_hitpoints == 43);
        assert(env.players[0].num_pending_hits == 0);
        assert(env.players[1].num_pending_hits == 0);
    }
}

static void test_reflections_do_not_recurse(void) {
    reset();
    equip_recoil(0);
    equip_recoil(1);
    env.players[0].veng_active = 1;
    env.players[1].veng_active = 1;
    pvp_process_incoming_hits(&env, 0);
    queue_damage(0, 20, ATTACK_STYLE_MELEE, 0);
    pvp_process_incoming_hits(&env, 1);
    assert_reflected_pair(1, 3, 15);
    env.tick++;
    pvp_process_incoming_hits(&env, 0);
    assert(env.players[0].current_hitpoints == 103);
    assert(env.players[0].veng_active);
    assert(env.players[0].item_effect_state.recoil_charges == 40);
    assert(env.players[0].num_pending_hits == 0);
}

static void test_lethal_hit_preserves_reflections(void) {
    for (int attacker = 0; attacker < 2; attacker++) {
        reset();
        int defender = 1 - attacker;
        env.players[attacker].current_hitpoints = 10;
        env.players[defender].current_hitpoints = 5;
        env.players[defender].veng_active = 1;
        pvp_process_incoming_hits(&env, attacker);
        queue_damage(attacker, 20, ATTACK_STYLE_MELEE, 0);
        pvp_process_incoming_hits(&env, defender);
        assert(env.players[defender].current_hitpoints == 0);
        assert(env.players[attacker].current_hitpoints == 10);
        assert(env.players[defender].num_pending_hits == 1);
        assert(!pvp_death_is_settled(&env));
        env.tick++;
        pvp_process_incoming_hits(&env, attacker);
        assert(env.players[attacker].current_hitpoints == 7);
        assert(env.players[defender].num_pending_hits == 0);
        assert(pvp_death_is_settled(&env));
    }
}

static void test_recorded_lethal_overkill_reflections(void) {
    for (int attacker = 0; attacker < 2; attacker++) {
        reset();
        int defender = 1 - attacker;
        env.players[defender].current_hitpoints = 30;
        env.players[defender].veng_active = 1;
        equip_recoil(defender);
        pvp_process_incoming_hits(&env, attacker);
        queue_damage(attacker, 45, ATTACK_STYLE_MELEE, 0);
        pvp_process_incoming_hits(&env, defender);
        assert(env.players[defender].current_hitpoints == 0);
        assert(env.players[defender].hit_damage == 45);
        assert_reflected_pair(defender, 4, 22);
        assert(env.players[defender].item_effect_state.recoil_charges == 36);
        env.tick++;
        pvp_process_incoming_hits(&env, attacker);
        assert(env.players[attacker].current_hitpoints == 95);
    }
}

static void test_dead_source_pending_hit_lands(void) {
    for (int first = 0; first < 2; first++) {
        reset();
        env.players[0].current_hitpoints = 10;
        env.players[1].current_hitpoints = 10;
        queue_damage(0, 20, ATTACK_STYLE_MELEE, 0);
        queue_damage(1, 20, ATTACK_STYLE_MELEE, 0);
        pvp_process_incoming_hits(&env, first);
        assert(env.players[first].current_hitpoints == 0);
        assert(env.players[first].num_pending_hits == 1);
        pvp_process_incoming_hits(&env, 1 - first);
        assert(env.players[1 - first].current_hitpoints == 0);
        assert(env.players[0].num_pending_hits == 0);
        assert(env.players[1].num_pending_hits == 0);
    }
}

static void test_instant_input_hits_precede_actor_passes(void) {
    for (int first = 0; first < 2; first++) {
        reset();
        int second = 1 - first;
        queue_damage(first, 15, ATTACK_STYLE_MELEE, 0);
        queue_damage(first, 25, ATTACK_STYLE_MELEE, 0);
        queue_damage(second, 10, ATTACK_STYLE_MELEE, 0);
        assert(env.players[first].num_pending_hits == 2);
        pvp_process_incoming_hits(&env, first);
        pvp_process_incoming_hits(&env, second);
        assert(env.players[first].current_hitpoints == 111);
        assert(env.players[second].current_hitpoints == 81);
        assert(env.players[0].num_pending_hits == 0);
        assert(env.players[1].num_pending_hits == 0);
    }
}

static void test_dead_nh_inputs_cannot_revive(void) {
    for (int hp = 0; hp <= 1; hp++) {
        reset();
        Player* player = &env.players[0];
        player->current_hitpoints = hp;
        player->inventory_cells[0] = osrs_inventory_cell_from_content_code(
            osrs_inventory_content_code_from_consumable(OSRS_CONSUMABLE_SHARK_FOOD, 0));
        player->inventory_cells[1] = osrs_inventory_cell_from_content_code(
            osrs_inventory_content_code_from_consumable(OSRS_CONSUMABLE_BREW, 4));
        OsrsInventoryCell initial_food = player->inventory_cells[0];
        OsrsInventoryCell initial_brew = player->inventory_cells[1];
        int initial_food_count = player->food_count;
        int initial_brew_doses = player->brew_doses;
        queue_damage(0, 20, ATTACK_STYLE_MELEE, 0);
        int actions[OSRS_BASE_NUM_ACTION_HEADS] = {0};
        actions[OSRS_HEAD_EAT] = 1;
        actions[OSRS_HEAD_DRINK] = 2;
        execute_switches(&env, 0, actions, NULL);
        assert(player->num_pending_hits == 1);
        if (hp == 0) {
            assert(player->current_hitpoints == 0);
            assert(player->food_count == initial_food_count);
            assert(player->brew_doses == initial_brew_doses);
            assert(memcmp(&player->inventory_cells[0], &initial_food, sizeof(initial_food)) == 0);
            assert(memcmp(&player->inventory_cells[1], &initial_brew, sizeof(initial_brew)) == 0);
            assert(player->food_timer == 0 && player->potion_timer == 0);
            assert(!pvp_death_is_settled(&env));
        } else {
            assert(player->current_hitpoints > hp);
            assert(osrs_inventory_cell_is_empty(&player->inventory_cells[0]));
            assert(osrs_inventory_cell_metadata(&player->inventory_cells[1])->dose_count == 3);
        }
    }
}

static void test_pending_hits_survive_priority_change(void) {
    for (int original_first = 0; original_first < 2; original_first++) {
        reset();
        int source = 1 - original_first;
        env.pid_holder = original_first;
        pvp_process_incoming_hits(&env, original_first);
        pvp_process_incoming_hits(&env, source);
        queue_damage(source, 20, ATTACK_STYLE_MELEE, 0);
        queue_damage(source, 40, ATTACK_STYLE_MAGIC, 1);
        env.tick++;
        env.pid_holder = source;
        pvp_process_incoming_hits(&env, source);
        assert(env.players[source].pending_hits[0].ticks_until_hit == 0);
        assert(env.players[source].pending_hits[1].ticks_until_hit == 1);
        queue_damage(source, 30, ATTACK_STYLE_MELEE, 0);
        assert(env.players[source].num_pending_hits == 3);
        assert(env.players[source].pending_hits[0].damage == 20);
        assert(env.players[source].pending_hits[2].damage == 30);
        pvp_process_incoming_hits(&env, original_first);
        assert(env.players[original_first].current_hitpoints == 71);
        assert(env.players[original_first].hit_damage == 50);
        assert(env.players[source].num_pending_hits == 1);
        assert(env.players[source].pending_hits[0].damage == 40);
        assert(env.players[source].pending_hits[0].ticks_until_hit == 0);
        env.tick++;
        pvp_process_incoming_hits(&env, source);
        pvp_process_incoming_hits(&env, original_first);
        assert(env.players[original_first].current_hitpoints == 31);
        assert(env.players[source].num_pending_hits == 0);
    }
}

int main(void) {
    test_melee_processing_order();
    test_base_delay_and_processing_order();
    test_recorded_spec_exchange();
    test_reflections_do_not_recurse();
    test_lethal_hit_preserves_reflections();
    test_recorded_lethal_overkill_reflections();
    test_dead_source_pending_hit_lands();
    test_instant_input_hits_precede_actor_passes();
    test_dead_nh_inputs_cannot_revive();
    test_pending_hits_survive_priority_change();
    puts("PvP recipient timing contracts passed");
}
