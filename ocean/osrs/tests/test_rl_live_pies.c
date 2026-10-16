/* Summer pies on the live wire: a bag with pies (raw 7218, half a pie 7220, the pie dish 2313 a last bite leaves) reaches the sim as the same cells a native VW-pies kit holds
 * (riskfight_model.h RISKFIGHT_VW_PIES_LOADOUT), so a net trained with pies sees them, and the food head can pick one: the mask offers the slot and the head's action becomes a
 * CLICK_SLOT record on it (EnginePlan.click on that slot's item takes its left-click option, Eat, for both the pie and half a pie).
 *   cc -std=gnu11 -O2 -I. -Isrc -Wno-comment -o /tmp/t ocean/osrs/tests/test_rl_live_pies.c -lm -lpthread && /tmp/t   (cwd = repo root) */
#define GS_RL_SERVER_NO_MAIN
#include "gs_rl_server.c"

#define SUMMER_PIE 7218
#define HALF_SUMMER_PIE 7220
#define PIE_DISH 2313

static void feed(Request* q, RtSeen* seen) {
    FILE* in = fopen("ocean/osrs/tests/fixtures/live_swings.requests", "r");
    if (!in) { perror("fixtures/live_swings.requests"); exit(2); }
    int new_fight, fight_start, obs_only;
    assert(rt_parse(in, q, seen, &new_fight, &fight_start, &obs_only));
    fclose(in);
}

int main(void) {
    g_fight.net = NULL;
    g_fight.rng = 0x9e3779b9u;
    g_fight.hard = 1;
    riskfight_deployable = 1;
    g_ablation.hp = HP_UNIFORM; g_ablation.escape_model = 1; g_ablation.his_defence = 99; g_ablation.panic = 0.8; g_ablation.panic_hp = 25;
    observer_register_items();
    sim_init(&g_base);

    /* The pie ids are the sim's own, and they pass the server's canonical map untouched. */
    assert(raw_id_to_content(SUMMER_PIE) == osrs_inventory_content_code_from_consumable(OSRS_CONSUMABLE_SUMMER_PIE, 2));
    assert(raw_id_to_content(HALF_SUMMER_PIE) == osrs_inventory_content_code_from_consumable(OSRS_CONSUMABLE_SUMMER_PIE, 1));
    assert(canonical(SUMMER_PIE) == SUMMER_PIE && canonical(HALF_SUMMER_PIE) == HALF_SUMMER_PIE && canonical(PIE_DISH) == PIE_DISH);

    /* The wire bag of the native pies kit: every slot's raw id, then one pie bitten (half a pie) and one finished (the dish). */
    static Request q;
    RtSeen seen;
    feed(&q, &seen);
    const RiskfightLoadout* kit = &RISKFIGHT_VW_PIES_LOADOUT;
    int pie_slots[TAPE_BAG], pies = 0;
    for (int i = 0; i < TAPE_BAG; i++) {
        const OsrsItemContentMetadata* m = osrs_item_content_metadata(kit->inventory[i]);
        q.bag[i] = kit->inventory[i] == 0 ? -1 : m->raw_osrs_id;
        if (kit->inventory[i] != 0 && m->consumable_kind == OSRS_CONSUMABLE_SUMMER_PIE) pie_slots[pies++] = i;
    }
    assert(pies == RF_VW_PIES && pies >= 3);
    q.bag[pie_slots[1]] = HALF_SUMMER_PIE;
    q.bag[pie_slots[2]] = PIE_DISH;
    q.ours.hp = 60; /* wounded: a pie heals */
    q.food_timer = q.potion_timer = q.karambwan_timer = 0;
    request_declare_priors(&q);
    assert(server_unavailable(&q) == NULL);
    RtRecord records[GS_WIRE_MAX_RECORDS];
    int count;
    rt_decide(&q, &seen, 1, q.tick, records, &count);
    RiskfightState* s = &g_base.state;
    const Player* us = &s->env.players[0];

    /* Same cells as the native kit: the pie, the half pie and the dish by content code; every supply slot (food, karambwan, brews, restores) as well. */
    for (int slot = 0; slot < TAPE_BAG; slot++) {
        uint16_t native = kit->inventory[slot];
        if (slot == pie_slots[1]) native = osrs_inventory_content_code_from_consumable(OSRS_CONSUMABLE_SUMMER_PIE, 1);
        if (slot == pie_slots[2]) native = raw_id_to_content(PIE_DISH);
        const OsrsItemContentMetadata* m = osrs_item_content_metadata(native);
        if (native != 0 && m->consumable_kind == OSRS_CONSUMABLE_NONE) continue; /* gear: the server maps the ids it models */
        OsrsInventoryCell expected = osrs_inventory_cell_from_content_code(native);
        assert(osrs_inventory_cell_metadata(&us->inventory_cells[slot]) == osrs_inventory_cell_metadata(&expected));
    }
    assert(osrs_inventory_cell_metadata(&us->inventory_cells[pie_slots[0]])->consumable_kind == OSRS_CONSUMABLE_SUMMER_PIE &&
        osrs_inventory_cell_metadata(&us->inventory_cells[pie_slots[0]])->dose_count == 2);
    assert(osrs_inventory_cell_metadata(&us->inventory_cells[pie_slots[1]])->dose_count == 1);
    assert(osrs_inventory_cell_metadata(&us->inventory_cells[pie_slots[2]])->consumable_kind == OSRS_CONSUMABLE_PIE_DISH);

    /* The food head offers the pie and half a pie (the dish is no food), and its action is a click on that slot. */
    unsigned char mask[RF_MASK_SIZE];
    riskfight_write_action_mask(s, 0, mask);
    const unsigned char* food = mask + RF_HEAD_OFFSET[RF_FOOD];
    assert(food[pie_slots[0] + 1] && food[pie_slots[1] + 1] && !food[pie_slots[2] + 1]);
    for (int i = 0; i < 2; i++) {
        int actions[RF_HEADS] = {0};
        actions[RF_FOOD] = pie_slots[i] + 1;
        RtRecord out[GS_WIRE_MAX_RECORDS];
        int n = rt_records(actions, s, out);
        assert(n == 1 && out[0].kind == GS_WIRE_CLICK_SLOT && out[0].arg == pie_slots[i]);
        assert(q.bag[out[0].arg] == (i == 0 ? SUMMER_PIE : HALF_SUMMER_PIE)); /* what EnginePlan.click reads off the bag: a pie item with the Eat option */
    }
    printf("pies: native kit cells (%d pies, one bitten, one dish), food head offers pie and half pie, CLICK_SLOT on their slots\n", pies);
    puts("Live pie contracts passed");
    return 0;
}
