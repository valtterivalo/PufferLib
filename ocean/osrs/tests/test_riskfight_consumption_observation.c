#include <assert.h>
#include <stdio.h>
#include "../encounters/encounter_riskfight.h"

static RiskfightState state;
static RiskfightContext context;

static void reset(void) {
    riskfight_reset((EncounterState*)&state, (EncounterContext*)&context, 123);
    context.self_play = 1;
    state.env.players[1].current_hitpoints = 30;
}

static void assert_consumption_visible(float* obs) {
    riskfight_write_observation(&state, 0, obs);
    assert(obs[RF_HISTORY_START + 6] == 1);
    assert(obs[RF_OPPONENT_START + NUM_GEAR_SLOTS] == 1);
}

int main(void) {
    riskfight_init_context((EncounterContext*)&context);
    riskfight_finalize_context((EncounterState*)&state, (EncounterContext*)&context);
    float single[RF_OBS_SIZE], triple[RF_OBS_SIZE];
    reset();
    int actions[2 * RF_HEADS] = {0};
    actions[RF_HEADS + RF_FOOD] = 10;
    riskfight_step((EncounterState*)&state, (EncounterContext*)&context, actions);
    assert_consumption_visible(single);
    reset();
    actions[RF_HEADS + RF_DRINK] = 8;
    actions[RF_HEADS + RF_COMBO] = 4;
    riskfight_step((EncounterState*)&state, (EncounterContext*)&context, actions);
    assert_consumption_visible(triple);
    assert(memcmp(single, triple, sizeof(single)) == 0);
    const int potion_slots[] = {7, 17, 19};
    for (size_t i = 0; i < sizeof(potion_slots) / sizeof(potion_slots[0]); i++) {
        reset();
        memset(actions, 0, sizeof(actions));
        actions[RF_HEADS + RF_DRINK] = potion_slots[i] + 1;
        riskfight_step((EncounterState*)&state, (EncounterContext*)&context, actions);
        riskfight_write_observation(&state, 0, single);
        assert(single[RF_HISTORY_START + 6] == 1);
        RenderEntity entities[2] = {0};
        int count;
        riskfight_render_entities((EncounterState*)&state, (EncounterContext*)&context, entities, 2, &count);
        assert(count == 2 && entities[1].ate_food_this_tick);
        assert(state.inventory_use[1].potion_animation_tick_plus_one == state.env.tick);
        memset(actions, 0, sizeof(actions));
        riskfight_step((EncounterState*)&state, (EncounterContext*)&context, actions);
        riskfight_write_observation(&state, 0, single);
        assert(single[RF_HISTORY_START + 6] == 0);
        assert(single[RF_HISTORY_START + RF_EVENT_WIDTH + 6] == 1);
    }
    reset();
    memset(actions, 0, sizeof(actions));
    actions[RF_HEADS + RF_FOOD] = 10;
    actions[RF_HEADS + RF_WEAPON] = 23;
    riskfight_step((EncounterState*)&state, (EncounterContext*)&context, actions);
    assert_consumption_visible(single);
    assert(single[RF_OPPONENT_START + GEAR_SLOT_WEAPON] * RF_OBSERVATION_ITEM_SCALE == ITEM_DHAROKS_GREATAXE);
    Player* p = &state.env.players[1];
    p->cast_veng_this_tick = 1;
    riskfight_observe_visible(&state, 0, 0);
    assert(state.visible[0].events[0][5] == 0);
    p->attack_style_this_tick = ATTACK_STYLE_MELEE;
    riskfight_observe_visible(&state, 0, 0);
    assert(state.visible[0].events[0][6] == 0);
    p->attack_style_this_tick = ATTACK_STYLE_NONE;
    p->current_hitpoints = 0;
    riskfight_observe_visible(&state, 0, 0);
    assert(state.visible[0].events[0][6] == 0);
    riskfight_destroy_context((EncounterContext*)&context);
    puts("Consumption visibility, hidden item identity, history and animation priority PASS");
}
