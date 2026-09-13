#include <assert.h>
#include <stdio.h>
#include "../encounters/encounter_riskfight.h"

static RiskfightState state;
static RiskfightContext context;
static void reset(void) {
    riskfight_reset((EncounterState*)&state, (EncounterContext*)&context, 12345);
    context.self_play = 1;
}
static void step(int* actions) {
    riskfight_step((EncounterState*)&state, (EncounterContext*)&context, actions);
}
static void use(int agent, int slot) {
    osrs_player_use_inventory(&state.env.players[agent], &state.inventory_use[agent],
        &state.env.pvp_runtime.teleport[agent], slot, state.env.tick);
}
static void test_reset_and_equipment(void) {
    reset();
    for (int i = 0; i < 2; i++) {
        Player* p = &state.env.players[i];
        assert(p->equipped[GEAR_SLOT_AMMO] == ITEM_NONE);
        assert(p->current_hitpoints == 121 && p->current_prayer == 99);
        assert(p->current_attack == 118 && p->current_strength == 118 && p->current_defence == 118);
        assert(p->veng_active && p->veng_cooldown == 0 && p->special_energy == 100);
        assert(!osrs_interaction_active(&p->interaction));
        for (int slot = 0; slot < 27; slot++) assert(!osrs_inventory_cell_is_empty(&p->inventory_cells[slot]));
        assert(osrs_inventory_cell_is_empty(&p->inventory_cells[27]));
        p->attack_timer = 6;
        use(i, 22);
        assert(p->equipped[GEAR_SLOT_WEAPON] == ITEM_DHAROKS_GREATAXE);
        assert(p->equipped[GEAR_SLOT_SHIELD] == ITEM_NONE && p->attack_timer == 6);
        assert(osrs_inventory_cell_metadata(&p->inventory_cells[27])->item_idx == ITEM_AVERNIC_DEFENDER);
        use(i, 22);
        use(i, 27);
        assert(p->equipped[GEAR_SLOT_WEAPON] == ITEM_ABYSSAL_TENTACLE);
        assert(p->equipped[GEAR_SLOT_SHIELD] == ITEM_AVERNIC_DEFENDER);
        assert(osrs_inventory_cell_is_empty(&p->inventory_cells[27]));
    }
}
static void test_food(void) {
    reset();
    Player* p = &state.env.players[0];
    p->current_hitpoints = 20;
    use(0, 9); use(0, 7); use(0, 3);
    assert(p->current_hitpoints == 80);
    assert(p->attack_timer == 4);
    assert(osrs_inventory_cell_is_empty(&p->inventory_cells[9]));
    assert(osrs_inventory_cell_metadata(&p->inventory_cells[7])->dose_count == 3);
    assert(osrs_inventory_cell_is_empty(&p->inventory_cells[3]));
    use(0, 10); use(0, 8); use(0, 4);
    assert(p->current_hitpoints == 80);
    assert(!osrs_inventory_cell_is_empty(&p->inventory_cells[10]));
    reset(); p = &state.env.players[0]; p->current_hitpoints = 20;
    use(0, 0);
    assert(p->current_hitpoints == 31 && p->food_timer == 1 && p->attack_timer == 2);
    assert(osrs_inventory_cell_metadata(&p->inventory_cells[0])->raw_osrs_id == 7220);
    use(0, 0); assert(p->current_hitpoints == 31);
    p->food_timer--;
    use(0, 0);
    assert(p->current_hitpoints == 42 && p->attack_timer == 5);
    assert(osrs_inventory_cell_metadata(&p->inventory_cells[0])->raw_osrs_id == 2313);
    reset(); p = &state.env.players[0]; p->current_hitpoints = 20;
    use(0, 3); use(0, 7); use(0, 9);
    assert(p->current_hitpoints == 40);
}
static void test_orb_and_stop(void) {
    reset(); Player* p = &state.env.players[0];
    osrs_interaction_set(&p->interaction, 1);
    use(0, 26); use(0, 26);
    assert(p->current_hitpoints == 111 && p->veng_active);
    assert(p->hit_damage == 10 && p->hit_attacker_idx == -1);
    assert(!osrs_interaction_active(&p->interaction));
    state.env.tick++; p->current_hitpoints = 7;
    use(0, 26); assert(p->current_hitpoints == 1);
    reset();
    int actions[2 * RF_HEADS] = {0}; actions[RF_PRIMARY] = RF_ATTACK;
    step(actions); assert(osrs_interaction_active(&state.env.players[0].interaction));
    actions[RF_PRIMARY] = 0; step(actions);
    assert(osrs_interaction_active(&state.env.players[0].interaction));
    actions[RF_PRIMARY] = RF_STOP; step(actions);
    assert(!osrs_interaction_active(&state.env.players[0].interaction));
}
static void test_dharok_recoil(void) {
    assert(osrs_dharok_max_hit(50, 99, 121) == 50);
    assert(osrs_dharok_max_hit(50, 99, 99) == 50);
    assert(osrs_dharok_max_hit(50, 99, 1) == 98);
    for (int hp = 2; hp <= 121; hp++)
        assert(osrs_dharok_max_hit(50, 99, hp) <= osrs_dharok_max_hit(50, 99, hp - 1));
    reset(); Player* p = &state.env.players[0];
    use(0, 25); osrs_consume_recoil_charges(p, 17);
    use(0, 25); use(0, 25);
    assert(p->item_effect_state.recoil_charges == 23);
    osrs_consume_recoil_charges(p, 23);
    assert(p->equipped[GEAR_SLOT_RING] == ITEM_NONE);
    assert(p->item_effect_state.recoil_charges == 0);
}
static void test_potions(void) {
    reset(); Player* p = &state.env.players[0];
    p->current_hitpoints = 50; use(0, 7);
    assert(p->current_hitpoints == 66 && p->current_attack == 105 && p->current_magic == 88);
    p->potion_timer = 0; use(0, 17);
    assert(p->current_attack == 105 && p->current_magic == 99);
    p->potion_timer = 0; use(0, 19);
    assert(p->current_hitpoints == 56 && p->current_attack == 118);
    assert(state.inventory_use[0].divine_combat_ticks == OSRS_DIVINE_DURATION);
}
static void test_vengeance(void) {
    reset(); Player* p = &state.env.players[0];
    assert(!osrs_player_cast_inventory_vengeance(p, &state.inventory_use[0], state.env.tick));
    PendingHit hit = {.damage = 20, .attack_type = ATTACK_STYLE_MELEE, .hit_success = 1};
    apply_damage(&state.env, 1, 0, &hit);
    assert(!p->veng_active && p->current_hitpoints == 101);
    assert(state.env.players[1].current_hitpoints == 121);
    assert(p->num_pending_hits == 1 && p->pending_hits[0].damage == 15);
    pvp_process_incoming_hits(&state.env, 1);
    assert(state.env.players[1].current_hitpoints == 106);
    assert(state.env.players[1].hit_landed_this_tick && state.env.players[1].hit_damage == 15);
    state.env.tick++;
    assert(osrs_player_cast_inventory_vengeance(p, &state.inventory_use[0], state.env.tick));
    assert(state.inventory_use[0].vengeance_sacks == 99);
    apply_damage(&state.env, 1, 0, &hit);
    pvp_process_incoming_hits(&state.env, 1);
    assert(state.env.players[1].current_hitpoints == 91 && !p->veng_active);
    assert(!osrs_player_cast_inventory_vengeance(p, &state.inventory_use[0], state.env.tick));
    p->veng_cooldown = 0; p->veng_active = 1; hit.damage = 1;
    int before_chip = state.env.players[1].current_hitpoints;
    apply_damage(&state.env, 1, 0, &hit); assert(!p->veng_active);
    pvp_process_incoming_hits(&state.env, 1);
    assert(state.env.players[1].current_hitpoints == before_chip - 1);
    p->veng_active = 1; p->current_hitpoints = 5;
    state.env.players[1].current_hitpoints = 10; hit.damage = 20;
    apply_damage(&state.env, 1, 0, &hit); riskfight_finish(&state);
    assert(!state.env.episode_over && state.outcome[0] == RISKFIGHT_ONGOING);
    pvp_process_incoming_hits(&state.env, 1);
    riskfight_finish(&state);
    assert(state.env.players[1].current_hitpoints == 7);
    assert(state.outcome[0] == RISKFIGHT_DEATH && state.rewards[0] == -1 && state.rewards[1] == 1);
    reset();
    state.env.players[0].current_hitpoints = 5;
    state.env.players[1].current_hitpoints = 2;
    apply_damage(&state.env, 1, 0, &hit);
    riskfight_finish(&state);
    assert(!state.env.episode_over);
    pvp_process_incoming_hits(&state.env, 1);
    riskfight_finish(&state);
    assert(state.outcome[0] == RISKFIGHT_MUTUAL_DEATH);
    assert(state.rewards[0] == 0 && state.rewards[1] == 0);
}
static void test_special_and_outcomes(void) {
    reset(); Player* p = &state.env.players[0];
    use(0, 24); p->attack_timer = 5; p->has_attack_timer = 1;
    int actions[2 * RF_HEADS] = {0}; actions[RF_PRIMARY] = RF_ATTACK; actions[RF_SPECIAL] = 2;
    step(actions);
    assert(p->special_energy == 0 && p->attack_timer == 4);
    assert(osrs_spec_cost(ITEM_GRANITE_MAUL) == 60 && osrs_spec_cost(ITEM_GRANITE_MAUL_ORNATE) == 50);
    reset(); memset(actions, 0, sizeof(actions)); actions[RF_PRIMARY] = RF_TELEPORT;
    step(actions); assert(state.env.episode_over && state.outcome[0] == RISKFIGHT_ESCAPE);
    assert(osrs_inventory_cell_is_empty(&state.env.players[0].inventory_cells[20]));
    reset(); state.env.players[1].current_hitpoints = 0; riskfight_finish(&state);
    assert(state.rewards[0] == 1 && state.rewards[1] == -1);
}
static void test_visible_observation_boundary(void) {
    reset();
    state.env.tick = 1;
    Player* opponent = &state.env.players[1];
    float before[RF_OBS_SIZE], after[RF_OBS_SIZE];
    riskfight_observe_visible(&state, 0, 0);
    riskfight_write_observation(&state, 0, before);
    opponent->equipped[GEAR_SLOT_RING] = ITEM_RING_OF_RECOIL;
    opponent->equipped[GEAR_SLOT_AMMO] = ITEM_DRAGON_ARROWS;
    riskfight_observe_visible(&state, 0, 0);
    riskfight_write_observation(&state, 0, after);
    assert(memcmp(before, after, sizeof(before)) == 0);
    assert(after[RF_OPPONENT_START + GEAR_SLOT_RING] * RF_OBSERVATION_ITEM_SCALE == ITEM_NONE);
    assert(after[RF_OPPONENT_START + GEAR_SLOT_AMMO] * RF_OBSERVATION_ITEM_SCALE == ITEM_NONE);
    assert(after[RF_HISTORY_START + 6] == 0);

    opponent->equipped[GEAR_SLOT_WEAPON] = ITEM_DHAROKS_GREATAXE;
    opponent->hit_landed_this_tick = 1;
    opponent->hit_damage = 10;
    opponent->current_hitpoints = 90;
    riskfight_observe_visible(&state, 0, 0);
    riskfight_write_observation(&state, 0, after);
    assert(after[RF_OPPONENT_START + GEAR_SLOT_WEAPON] * RF_OBSERVATION_ITEM_SCALE == ITEM_DHAROKS_GREATAXE);
    assert(after[RF_HISTORY_START + 3] == 1);
    assert(after[RF_HISTORY_START + 4] * RF_OBSERVATION_DAMAGE_SCALE == 10);
    assert(after[RF_OPPONENT_START + NUM_GEAR_SLOTS] < 1);
    assert(memcmp(before, after, sizeof(before)) != 0);
}

static void test_hidden_state_and_replay(void) {
    reset(); float a[RF_OBS_SIZE], b[RF_OBS_SIZE];
    riskfight_write_observation(&state, 0, a);
    Player* opponent = &state.env.players[1];
    opponent->attack_timer = 88; opponent->special_energy = 3;
    opponent->inventory_cells[0] = osrs_inventory_cell_empty();
    opponent->pending_hits[0].damage = 99; opponent->num_pending_hits = 1;
    opponent->veng_cooldown = 44;
    riskfight_write_observation(&state, 0, b);
    assert(memcmp(a, b, sizeof(a)) == 0);
    state.env.tick = 1;
    opponent->cast_veng_this_tick = 1; opponent->just_attacked = 1;
    riskfight_observe_visible(&state, 0, 0);
    riskfight_write_observation(&state, 0, a);
    assert(a[RF_HISTORY_START + 5] == 0);
    opponent->just_attacked = 0;
    riskfight_observe_visible(&state, 0, 0);
    riskfight_write_observation(&state, 0, a);
    assert(a[RF_HISTORY_START + 5] == 1);
    reset(); static RiskfightState initial; initial = state;
    int actions[2 * RF_HEADS] = {0}; actions[RF_FOOD] = 10; actions[RF_DRINK] = 8; actions[RF_COMBO] = 4;
    state.env.players[0].current_hitpoints = 20; initial = state;
    step(actions); static RiskfightState expected; expected = state;
    state = initial;
    HumanInput hi; human_input_init(&hi);
    human_input_queue_inventory_primary_click(&hi, 9);
    human_input_queue_inventory_primary_click(&hi, 7);
    human_input_queue_inventory_primary_click(&hi, 3);
    HumanCommandQueue empty = {0};
    riskfight_step_queues(&state, &context, &hi.commands, &empty);
    assert(memcmp(&state, &expected, sizeof(state)) == 0);
    free(hi.commands.items);
    reset(); initial = state;
    for (int i = 0; i < 25 && !state.env.episode_over; i++) {
        riskfight_write_observation(&state, 0, a); riskfight_script(a, RISKFIGHT_TRADER, actions);
        riskfight_write_observation(&state, 1, a); riskfight_script(a, RISKFIGHT_AGGRESSIVE, actions + RF_HEADS);
        step(actions);
    }
    expected = state; state = initial;
    for (int i = 0; i < 25 && !state.env.episode_over; i++) {
        riskfight_write_observation(&state, 0, a); riskfight_script(a, RISKFIGHT_TRADER, actions);
        riskfight_write_observation(&state, 1, a); riskfight_script(a, RISKFIGHT_AGGRESSIVE, actions + RF_HEADS);
        step(actions);
    }
    assert(memcmp(&state, &expected, sizeof(state)) == 0);
    float mask[RF_MASK_SIZE]; riskfight_write_action_mask(&state, 0, mask);
}
static void test_tick_order_and_boundaries(void) {
    reset(); state.env.pid_holder = 0;
    Player* attacker = &state.env.players[0];
    Player* defender = &state.env.players[1];
    attacker->num_pending_hits = 1;
    attacker->pending_hits[0] = (PendingHit){.damage = 20,
        .attack_type = ATTACK_STYLE_MELEE, .hit_success = 1, .ticks_until_hit = 0};
    int actions[2 * RF_HEADS] = {0};
    actions[RF_HEADS + RF_VENGEANCE] = 1;
    step(actions);
    assert(!defender->veng_active && state.inventory_use[1].vengeance_sacks == 100);
    step(actions);
    assert(defender->veng_active && state.inventory_use[1].vengeance_sacks == 99);
    int attacker_hp = attacker->current_hitpoints;
    attacker->num_pending_hits = 1;
    attacker->pending_hits[0] = (PendingHit){.damage = 20,
        .attack_type = ATTACK_STYLE_MELEE, .hit_success = 1};
    step(actions);
    assert(!defender->veng_active && attacker->current_hitpoints == attacker_hp - 15);
    reset(); state.env.pid_holder = 0;
    attacker = &state.env.players[0]; defender = &state.env.players[1];
    defender->veng_active = 0;
    attacker->num_pending_hits = 1;
    attacker->pending_hits[0] = (PendingHit){.damage = 200,
        .attack_type = ATTACK_STYLE_MELEE, .hit_success = 1};
    memset(actions, 0, sizeof(actions)); actions[RF_HEADS + RF_PRIMARY] = RF_ATTACK;
    step(actions);
    assert(state.rewards[0] == 1 && !defender->just_attacked);
    reset(); Player* p = &state.env.players[0];
    p->current_hitpoints = 20;
    use(0, 7); use(0, 9);
    assert(p->current_hitpoints == 36);
    p->potion_timer = 0; use(0, 19);
    p->potion_timer = 0; use(0, 7);
    int drained = p->current_attack;
    osrs_player_inventory_tick(p, &state.inventory_use[0]);
    assert(p->current_attack == drained);
    state.inventory_use[0].divine_combat_ticks = 1;
    p->current_attack = 118;
    osrs_player_inventory_tick(p, &state.inventory_use[0]);
    assert(p->current_attack == 99);
    reset(); memset(actions, 0, sizeof(actions));
    for (int tick = 0; tick < 601; tick++) step(actions);
    assert(!state.env.episode_over && state.env.tick == 601);
    for (int code = 0; code < OSRS_ITEM_CONTENT_COUNT; code++) {
        const OsrsItemContentMetadata* meta = osrs_item_content_metadata(code);
        assert(osrs_inventory_cell_obs_code_decode(osrs_inventory_cell_obs_code_encode(code)) == code);
        if (meta->raw_osrs_id)
            assert(osrs_inventory_content_code_from_raw_osrs_id(meta->raw_osrs_id) == code);
    }
}

static void test_live_attack_processing_order(void) {
    for (int first = 0; first < 2; first++) {
        reset();
        state.env.pid_holder = first;
        state.env.priority[first].rank = 0;
        state.env.priority[1 - first].rank = 1;
        int actions[2 * RF_HEADS] = {0};
        for (int i = 0; i < 2; i++) {
            state.env.players[i].veng_active = 0;
            actions[i * RF_HEADS + RF_WEAPON] = 24;
            actions[i * RF_HEADS + RF_PRIMARY] = RF_ATTACK;
            actions[i * RF_HEADS + RF_SPECIAL] = 1;
        }
        step(actions);
        assert(state.env.players[first].current_hitpoints == 121);
        assert(state.env.players[1 - first].current_hitpoints < 121);
        assert(state.env.players[first].num_pending_hits == 0);
        assert(state.env.players[1 - first].num_pending_hits == 1);
        assert(state.env.players[0].special_energy == 50 && state.env.players[1].special_energy == 50);
        memset(actions, 0, sizeof(actions));
        actions[RF_PRIMARY] = actions[RF_HEADS + RF_PRIMARY] = RF_STOP;
        step(actions);
        assert(state.env.players[first].current_hitpoints < 121);
        assert(state.env.players[0].num_pending_hits == 0 && state.env.players[1].num_pending_hits == 0);
    }
}

int main(void) {
    riskfight_init_context((EncounterContext*)&context);
    riskfight_finalize_context((EncounterState*)&state, (EncounterContext*)&context);
    test_reset_and_equipment(); test_food(); test_orb_and_stop(); test_dharok_recoil();
    test_potions(); test_vengeance(); test_special_and_outcomes(); test_visible_observation_boundary(); test_hidden_state_and_replay();
    test_tick_order_and_boundaries();
    test_live_attack_processing_order();
    riskfight_destroy_context((EncounterContext*)&context);
    puts("Riskfight contracts passed");
}
