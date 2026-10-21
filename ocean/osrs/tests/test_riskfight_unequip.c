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
    uint16_t items[5];
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
    unsigned char mask[RF_MASK_SIZE];
    riskfight_write_action_mask(&state, 0, mask);
    int offset = 0;
    for (int h = 0; h < RF_HEADS; h++) {
        if (actions[h]) assert(mask[offset + actions[h]]);
        offset += RF_ACTION_DIMS[h];
    }
    assert(offset == RF_MASK_SIZE);
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

/* Whether the deployable (or full) mask lets the shield come off with `kit` in the bag, after emptying cell `empty` (-1: none) and filling `fill`. */
static int shield_off_allowed(const RiskfightLoadout* kit, int deployable, int empty, int fill) {
    context.loadout[0] = kit;
    riskfight_reset((EncounterState*)&state, (EncounterContext*)&context, 123);
    context.loadout[0] = NULL;
    Player* p = &state.env.players[0];
    assert(p->equipped[GEAR_SLOT_SHIELD] != ITEM_NONE);
    if (empty >= 0) p->inventory_cells[empty] = osrs_inventory_cell_empty();
    if (fill >= 0) p->inventory_cells[fill] = osrs_inventory_cell_from_content_code(riskfight_supply_code(OSRS_CONSUMABLE_ANGLERFISH, 0));
    riskfight_deployable = deployable;
    unsigned char mask[RF_MASK_SIZE];
    riskfight_write_action_mask(&state, 0, mask);
    riskfight_deployable = 0;
    return mask[RF_HEAD_OFFSET[RF_SHIELD] + RF_UNEQUIP];
}

static void test_shield_off_needs_a_two_handed_weapon(void) {
    /* The live kit of 20261016T141036 (Voidwaker, dragon defender, no two-handed weapon): the shield never comes off, empty slots or not. */
    assert(RISKFIGHT_VW_LOADOUT.equipment[GEAR_SLOT_WEAPON] == ITEM_VOIDWAKER && RISKFIGHT_VW_LOADOUT.equipment[GEAR_SLOT_SHIELD] == ITEM_DRAGON_DEFENDER);
    assert(shield_off_allowed(&RISKFIGHT_VW_LOADOUT, 0, 5, -1) == 1);
    assert(shield_off_allowed(&RISKFIGHT_VW_LOADOUT, 1, 5, -1) == 0);
    assert(shield_off_allowed(&RISKFIGHT_VW_LOADOUT, 1, 5, 6) == 0);
    assert(shield_off_allowed(&RISKFIGHT_VW_LOADOUT, 1, -1, -1) == 0);
    /* The maul kit: the ornate maul sits in cell 26 and cell 27 is empty, so the shield may come off for it. */
    assert(shield_off_allowed(&RISKFIGHT_LIVE_LOADOUT, 1, -1, -1) == 1);
    assert(shield_off_allowed(&RISKFIGHT_LIVE_LOADOUT, 1, 26, -1) == 0); /* maul gone from the bag */
    assert(shield_off_allowed(&RISKFIGHT_LIVE_LOADOUT, 1, -1, 27) == 0); /* no empty cell to take the shield */
    /* The default kit carries the Dharok's greataxe. */
    assert(shield_off_allowed(NULL, 1, -1, -1) == 1);
}

static void test_vw_kit_shape(void) {
    const RiskfightLoadout* kit = &RISKFIGHT_VW_LOADOUT;
    int fish = 0, karambwan = 0, restore = 0, brew = 0, combat = 0, sack = 0, tab = 0, empty = 0;
    for (int slot = 0; slot < OSRS_INVENTORY_SIZE; slot++) {
        const OsrsItemContentMetadata* m = osrs_item_content_metadata(kit->inventory[slot]);
        assert(m->item_idx == ITEM_NONE || !item_is_two_handed(m->item_idx));
        fish += m->consumable_kind == OSRS_CONSUMABLE_ANGLERFISH; karambwan += m->consumable_kind == OSRS_CONSUMABLE_KARAMBWAN;
        restore += m->consumable_kind == OSRS_CONSUMABLE_SUPER_RESTORE; brew += m->consumable_kind == OSRS_CONSUMABLE_BREW;
        combat += m->consumable_kind == OSRS_CONSUMABLE_SUPER_COMBAT; sack += m->consumable_kind == OSRS_CONSUMABLE_VENGEANCE_SACK;
        tab += m->consumable_kind == OSRS_CONSUMABLE_TELEPORT; empty += kit->inventory[slot] == 0;
    }
    assert(fish == 14 && karambwan == 7 && restore == 2 && brew == 2 && combat == 1 && sack == 1 && tab == 1 && empty == 0);
    for (int slot = 0; slot < NUM_GEAR_SLOTS; slot++) assert(!item_is_two_handed(kit->equipment[slot]));
    riskfight_put_int((EncounterState*)&state, (EncounterContext*)&context, "live_loadout", 2);
    assert(context.loadout[0] == &RISKFIGHT_VW_LOADOUT && context.loadout[1] == &RISKFIGHT_LIVE_LOADOUT && context.kit_mix == RF_KIT_MIX_NONE);
    riskfight_put_int((EncounterState*)&state, (EncounterContext*)&context, "live_loadout", 3);
    assert(context.kit_mix == RF_KIT_MIX_VW && context.loadout[1] == &RISKFIGHT_LIVE_LOADOUT);
    riskfight_put_int((EncounterState*)&state, (EncounterContext*)&context, "live_loadout", 4);
    assert(context.kit_mix == RF_KIT_MIX_DHAROK && context.loadout[0] == &RISKFIGHT_VW_LOADOUT && context.loadout[1] == &RISKFIGHT_LIVE_LOADOUT);
    riskfight_put_int((EncounterState*)&state, (EncounterContext*)&context, "live_loadout", 0);
    assert(context.kit_mix == RF_KIT_MIX_NONE && !context.loadout[0] && !context.loadout[1]);
}

int main(void) {
    riskfight_init_context((EncounterContext*)&context);
    riskfight_finalize_context((EncounterState*)&state, (EncounterContext*)&context);
    test_ordered_partial_removal();
    test_policy_gear_heads();
    test_shield_off_needs_a_two_handed_weapon();
    test_vw_kit_shape();
    riskfight_destroy_context((EncounterContext*)&context);
    puts("Riskfight unequip queue, capacity, policy masks, shield-off rule and ordinary re-equip PASS");
}
