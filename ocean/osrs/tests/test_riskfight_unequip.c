#include <assert.h>
#include <stdio.h>
#include "../encounters/encounter_riskfight.h"

static RiskfightState state;
static RiskfightContext context;
static void reset(void) {
    context.self_play = 1;
    riskfight_reset((EncounterState*)&state, (EncounterContext*)&context, 123);
    state.env.players[0].inventory_cells[9] = osrs_inventory_cell_empty();
    state.env.players[0].inventory_cells[10] = osrs_inventory_cell_empty();
}
static void test_ordered_partial_removal(void) {
    reset();
    const int slots[] = {GEAR_SLOT_HEAD, GEAR_SLOT_BODY, GEAR_SLOT_LEGS, GEAR_SLOT_CAPE, GEAR_SLOT_HANDS};
    uint8_t items[5];
    HumanInput clicks;
    human_input_init(&clicks);
    Player* p = &state.env.players[0];
    for (int i = 0; i < 5; i++) {
        items[i] = p->equipped[slots[i]];
        human_input_queue_unequip(&clicks, slots[i]);
        assert(p->equipped[slots[i]] == items[i]);
    }
    assert(state.env.tick == 0);
    HumanCommandQueue empty = {0};
    riskfight_step_queues(&state, &context, &clicks.commands, &empty);
    assert(state.env.tick == 1);
    for (int i = 0; i < 5; i++) assert(p->equipped[slots[i]] == (i < 3 ? ITEM_NONE : items[i]));
    const int inventory_slots[] = {9, 10, 27};
    for (int i = 0; i < 3; i++)
        assert(osrs_inventory_cell_metadata(&p->inventory_cells[inventory_slots[i]])->item_idx == items[i]);
    assert(state.last_unequip_result[0] == OSRS_UNEQUIP_FULL);
    EncounterOverlay overlay = {0};
    riskfight_render_post_tick((EncounterState*)&state, (EncounterContext*)&context, &overlay);
    assert(overlay.status_text_active);
    assert(strcmp(overlay.status_text, "Not enough space in your inventory.") == 0);
    human_input_clear_pending(&clicks);
    for (int i = 0; i < 3; i++) human_input_queue_command(&clicks, (HumanCommand){
        .kind = HUMAN_COMMAND_INVENTORY_PRIMARY_CLICK, .inventory_slot = inventory_slots[i]});
    riskfight_step_queues(&state, &context, &clicks.commands, &empty);
    for (int i = 0; i < 5; i++) assert(p->equipped[slots[i]] == items[i]);
    riskfight_render_post_tick((EncounterState*)&state, (EncounterContext*)&context, &overlay);
    assert(!overlay.status_text_active);
    human_input_destroy(&clicks);
}
static void test_policy_gear_heads(void) {
    reset();
    int actions[2 * RF_HEADS] = {0};
    actions[RF_HEAD] = actions[RF_CAPE] = actions[RF_NECK] = actions[RF_BODY] = actions[RF_LEGS] = RF_UNEQUIP;
    float mask[RF_MASK_SIZE];
    riskfight_write_action_mask(&state, 0, mask);
    int offset = 0;
    for (int h = 0; h < RF_HEADS; h++) {
        if (actions[h]) assert(mask[offset + actions[h]]);
        offset += RF_ACTION_DIMS[h];
    }
    assert(offset == RF_MASK_SIZE && RF_MASK_SIZE == 461);
    riskfight_step((EncounterState*)&state, (EncounterContext*)&context, actions);
    Player* p = &state.env.players[0];
    assert(p->equipped[GEAR_SLOT_HEAD] == ITEM_NONE);
    assert(p->equipped[GEAR_SLOT_CAPE] == ITEM_NONE);
    assert(p->equipped[GEAR_SLOT_NECK] == ITEM_NONE);
    assert(p->equipped[GEAR_SLOT_BODY] == ITEM_DHAROKS_PLATEBODY);
    riskfight_write_action_mask(&state, 0, mask);
    offset = 0;
    for (int h = 0; h < RF_HEADS; h++) {
        if (RF_GEAR_SLOT_BY_HEAD[h] >= 0) assert(!mask[offset + RF_UNEQUIP]);
        offset += RF_ACTION_DIMS[h];
    }
    memset(actions, 0, sizeof(actions));
    actions[RF_HEAD] = 10; actions[RF_CAPE] = 11; actions[RF_NECK] = 28;
    riskfight_step((EncounterState*)&state, (EncounterContext*)&context, actions);
    assert(p->equipped[GEAR_SLOT_HEAD] == ITEM_DHAROKS_HELM);
    assert(p->equipped[GEAR_SLOT_CAPE] == ITEM_INFERNAL_CAPE);
    assert(p->equipped[GEAR_SLOT_NECK] == ITEM_AMULET_OF_RANCOUR);
}
int main(void) {
    riskfight_init_context((EncounterContext*)&context);
    riskfight_finalize_context((EncounterState*)&state, (EncounterContext*)&context);
    test_ordered_partial_removal();
    test_policy_gear_heads();
    riskfight_destroy_context((EncounterContext*)&context);
    puts("Riskfight unequip queue, capacity, policy masks and ordinary re-equip PASS");
}
