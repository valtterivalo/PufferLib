/* Which copy of a consumable the live client clicks (gs_rl_server rt_choose_copies): a triple eat takes the copies that make the shortest cursor path, a lone eat the copy
 * farthest from the potions, gear clicks stay put, and the chosen slot always holds the item the net picked.
 *   cc -std=gnu11 -O2 -I. -Isrc -Wno-comment -o /tmp/t ocean/osrs/tests/test_rl_slot_choice.c -lm -lpthread && /tmp/t   (cwd = repo root)
 *
 * Bag (slot = row * 4 + col):   row 0: A . . K     anglers A at 0, 13, 22; karambwans K at 3, 18; brews (4) B at 14, 27;
 *                               row 2: W . . S     tentacle W at 8, avernic defender S at 11
 *                               row 3: . A B .
 *                               row 4: . . K .
 *                               row 5: . . A .
 *                               row 6: . . . B */
#define GS_RL_SERVER_NO_MAIN
#include "gs_rl_server.c"

static int entry_of(const Player* p, int slot) {
    RiskfightBagOrder order = riskfight_bag_order(p);
    for (int entry = 0; entry < OSRS_INVENTORY_SIZE; entry++)
        if (order.slot[entry] == slot) return entry + 1;
    abort();
}

/* The net's choice of the copies at the physical slots `pick` (one per head, 0 = no action, slot + 1 otherwise) as the click records the server sends. */
static int clicks(const RiskfightState* s, const int pick[RF_HEADS], RtRecord* out, uint16_t* code_before) {
    const Player* us = &s->env.players[0];
    int actions[RF_HEADS] = {0};
    for (int head = 0; head < RF_HEADS; head++) if (pick[head]) actions[head] = entry_of(us, pick[head] - 1);
    riskfight_bag_to_physical(us, actions);
    for (int head = 0; head < RF_HEADS; head++) assert(actions[head] == pick[head]);
    int n = rt_records(actions, s, out);
    for (int i = 0; i < n; i++) code_before[i] = out[i].kind == GS_WIRE_CLICK_SLOT ? us->inventory_cells[out[i].arg].content_code : 0;
    rt_choose_copies(us, out, n);
    for (int i = 0; i < n; i++)
        if (out[i].kind == GS_WIRE_CLICK_SLOT) assert(us->inventory_cells[out[i].arg].content_code == code_before[i]); /* same item, maybe another copy */
    return n;
}

int main(void) {
    observer_register_items();
    sim_init(&g_base);
    RiskfightState* s = &g_base.state;
    Player* us = &s->env.players[0];
    const uint16_t angler = osrs_inventory_content_code_from_consumable(OSRS_CONSUMABLE_ANGLERFISH, 0);
    const uint16_t karambwan = osrs_inventory_content_code_from_consumable(OSRS_CONSUMABLE_KARAMBWAN, 0);
    const uint16_t brew = osrs_inventory_content_code_from_consumable(OSRS_CONSUMABLE_BREW, 4);
    for (int slot = 0; slot < OSRS_INVENTORY_SIZE; slot++) us->inventory_cells[slot] = osrs_inventory_cell_empty();
    static const int anglers[] = {0, 13, 22}, karambwans[] = {3, 18}, brews[] = {14, 27};
    for (int i = 0; i < 3; i++) us->inventory_cells[anglers[i]] = osrs_inventory_cell_from_content_code(angler);
    for (int i = 0; i < 2; i++) us->inventory_cells[karambwans[i]] = osrs_inventory_cell_from_content_code(karambwan);
    for (int i = 0; i < 2; i++) us->inventory_cells[brews[i]] = osrs_inventory_cell_from_content_code(brew);
    us->inventory_cells[8] = osrs_inventory_cell_from_item(ITEM_ABYSSAL_TENTACLE);
    us->inventory_cells[11] = osrs_inventory_cell_from_item(ITEM_AVERNIC_DEFENDER);
    assert(osrs_inventory_cell_metadata(&us->inventory_cells[0])->click_action == OSRS_CLICK_EAT);
    assert(osrs_inventory_cell_metadata(&us->inventory_cells[3])->click_action == OSRS_CLICK_EAT);
    assert(osrs_inventory_cell_metadata(&us->inventory_cells[14])->click_action == OSRS_CLICK_DRINK);
    RtRecord out[GS_WIRE_MAX_RECORDS];
    uint16_t code[GS_WIRE_MAX_RECORDS];

    /* Triple eat from the copies farthest apart (angler 0, brew 27, karambwan 3): the path angler 13 -> brew 14 -> karambwan 18 is 42 + 36 px, the shortest there is,
     * and the angler and karambwan it takes are the copies nearest that brew. */
    {
        int pick[RF_HEADS] = {0};
        pick[RF_FOOD] = 0 + 1; pick[RF_DRINK] = 27 + 1; pick[RF_COMBO] = 3 + 1;
        int n = clicks(s, pick, out, code);
        assert(n == 3 && out[0].arg == 13 && out[1].arg == 14 && out[2].arg == 18);
        for (int i = 0; i < 3; i++) assert(rt_slot_distance(out[1].arg, anglers[i]) >= rt_slot_distance(out[1].arg, out[0].arg));
        for (int i = 0; i < 2; i++) assert(rt_slot_distance(out[1].arg, karambwans[i]) >= rt_slot_distance(out[1].arg, out[2].arg));
        printf("triple eat: angler@0 brew@27 karambwan@3 -> angler@%d brew@%d karambwan@%d (%.0f px)\n", out[0].arg, out[1].arg, out[2].arg,
            rt_slot_distance(out[0].arg, out[1].arg) + rt_slot_distance(out[1].arg, out[2].arg));
    }
    /* A lone angler eat from the copy next to a brew (13) eats the one farthest from both brews (0), and from 0 stays at 0. */
    for (int from = 0; from < 3; from++) {
        int pick[RF_HEADS] = {0};
        pick[RF_FOOD] = anglers[from] + 1;
        int n = clicks(s, pick, out, code);
        assert(n == 1 && out[0].arg == 0);
    }
    /* A lone karambwan eat: 3 is farther from the brews than 18. A lone brew drink is not moved. */
    {
        int pick[RF_HEADS] = {0};
        pick[RF_COMBO] = 18 + 1;
        assert(clicks(s, pick, out, code) == 1 && out[0].arg == 3);
        int drink[RF_HEADS] = {0};
        drink[RF_DRINK] = 27 + 1;
        assert(clicks(s, drink, out, code) == 1 && out[0].arg == 27);
    }
    /* Gear clicks stay where they are, alone or anchoring a meal: weapon 8, shield 11, then the angler nearest the defender (13). */
    {
        int pick[RF_HEADS] = {0};
        pick[RF_WEAPON] = 8 + 1; pick[RF_SHIELD] = 11 + 1;
        int n = clicks(s, pick, out, code);
        assert(n == 2 && out[0].arg == 8 && out[1].arg == 11);
        pick[RF_FOOD] = 0 + 1;
        n = clicks(s, pick, out, code);
        assert(n == 3 && out[0].arg == 8 && out[1].arg == 11 && out[2].arg == 13);
    }
    puts("Live slot choice contracts passed");
    return 0;
}
