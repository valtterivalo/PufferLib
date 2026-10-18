/* The scripted Dharok fighter (riskfight_script, riskfight_script_dharok_armour, measured by scripts/measure_dharok_play.py): with a free bag cell he strips
 * platebody and/or platelegs in the measured situation (full set on, a weapon other than the axe in hand, no switch), at about the measured rate; a full bag
 * never strips; a stripped fighter puts the set back on before he takes the greataxe, so the swing has all four pieces; and the locator orb takes 10 of his
 * hitpoints. Seat 0 plays the default kit: Dharok's helm, platebody and platelegs, the tentacle and avernic defender, the axe and orb in a bag with one free cell.
 * cc -std=gnu11 -O2 -I. -o /tmp/test_riskfight_dharok_script ocean/osrs/tests/test_riskfight_dharok_script.c -lm */
#include <assert.h>
#include <stdio.h>
#include "../encounters/encounter_riskfight.h"

static RiskfightState state;
static RiskfightContext context;

static const RiskfightProfile QUIET = {{0}, 0, 0, {0}, 0, 0, {{0}}};
static const RiskfightProfile AXER = {{0}, 0, 0, {1000, 1000, 1000}, 0, 0, {{0}}};
static const RiskfightProfile ORBER = {{0}, 0, 0, {1000, 1000, 1000}, 1000, 0, {{0}}};

enum { SEEDS = 400, FIRST_TICK = 10, TICKS = 150, STRIP_HP = 90 };

static void reset(uint32_t seed) {
    riskfight_reset((EncounterState*)&state, (EncounterContext*)&context, seed);
    context.self_play = 1;
}

static void decide(const RiskfightProfile* profile, uint32_t seed, int* actions) {
    float obs[RF_OBS_SIZE];
    riskfight_write_script_observation(&state, 0, obs);
    riskfight_script(obs, profile, seed, 0, actions);
}

static void execute(const int* actions) {
    HumanInput input = {0};
    riskfight_policy_commands(&state, 0, actions, &input);
    for (int i = 0; i < input.commands.count; i++)
        riskfight_execute_command(&state, &context, 0, &input.commands.items[i]);
    free(input.commands.items);
}

static int dharok_pieces_worn(void) {
    const Player* p = &state.env.players[0];
    int n = 0;
    for (int slot = 0; slot < NUM_GEAR_SLOTS; slot++) n += riskfight_is_dharok_piece(p->equipped[slot]);
    return n;
}

static int in_bag(uint8_t item) {
    const Player* p = &state.env.players[0];
    for (int slot = 0; slot < OSRS_INVENTORY_SIZE; slot++)
        if (osrs_inventory_cell_metadata(&p->inventory_cells[slot])->item_idx == item) return 1;
    return 0;
}

static int strips(const int* actions) { return actions[RF_BODY] == RF_UNEQUIP || actions[RF_LEGS] == RF_UNEQUIP; }

/* Over many seeds, the first strip of each: it happens, at about the measured rate (bar 24-30, 281 per 100,000 ticks), never with a full bag. Each strip is
 * then played on: the axe brings the set back first, then the swing comes with four pieces. */
static void test_strip_then_reequip_before_axe(void) {
    int stripped = 0, decisions = 0, reequipped_before_axe = 0;
    for (uint32_t seed = 1; seed <= SEEDS; seed++) {
        reset(seed);
        Player* p = &state.env.players[0];
        assert(dharok_pieces_worn() == 3 && osrs_first_empty_inventory_cell(p->inventory_cells, -1) >= 0);
        p->current_hitpoints = STRIP_HP;
        p->attack_timer = 2; /* not the tentacle's speed - 1, the maul combo's tick (riskfight_script_combo), which switches weapons */
        int actions[RF_HEADS];
        for (int tick = FIRST_TICK; tick < FIRST_TICK + TICKS; tick++) {
            state.env.tick = tick;
            decide(&QUIET, seed, actions);
            decisions++;
            if (!strips(actions)) continue;
            stripped++;
            assert(actions[RF_WEAPON] == 0);
            execute(actions);
            assert(riskfight_is_dharok_piece(p->equipped[GEAR_SLOT_HEAD]));
            assert(p->equipped[GEAR_SLOT_BODY] == ITEM_NONE || p->equipped[GEAR_SLOT_LEGS] == ITEM_NONE);
            assert(in_bag(ITEM_DHAROKS_PLATEBODY) == (p->equipped[GEAR_SLOT_BODY] == ITEM_NONE));
            assert(in_bag(ITEM_DHAROKS_PLATELEGS) == (p->equipped[GEAR_SLOT_LEGS] == ITEM_NONE));
            /* His hitpoints drop and the axe is wanted: he puts the set back on first, the axe waits. */
            p->current_hitpoints = 60;
            state.env.tick = ++tick;
            decide(&AXER, seed, actions);
            assert(actions[RF_BODY] != RF_UNEQUIP && actions[RF_LEGS] != RF_UNEQUIP);
            assert(!actions[RF_WEAPON] || osrs_inventory_cell_metadata(&p->inventory_cells[actions[RF_WEAPON] - 1])->item_idx != ITEM_DHAROKS_GREATAXE);
            execute(actions);
            assert(dharok_pieces_worn() == 3 && p->equipped[GEAR_SLOT_WEAPON] != ITEM_DHAROKS_GREATAXE);
            state.env.tick = ++tick;
            decide(&AXER, seed, actions);
            execute(actions);
            assert(p->equipped[GEAR_SLOT_WEAPON] == ITEM_DHAROKS_GREATAXE && dharok_pieces_worn() == 4);
            reequipped_before_axe++;
            break;
        }
    }
    double rate = 1e5 * stripped / decisions;
    printf("strips: %d first strips over %d decisions (%.0f per 100,000, measured %d), %d re-equipped before the axe\n",
        stripped, decisions, rate, RF_STRIP_PER_100K[2], reequipped_before_axe);
    assert(stripped >= 1 && reequipped_before_axe == stripped);
    assert(rate > RF_STRIP_PER_100K[2] / 2.0 && rate < RF_STRIP_PER_100K[2] * 2.0);
}

/* The game's rule: an item comes off only into a free cell, so a full bag never strips. */
static void test_full_bag_never_strips(void) {
    for (uint32_t seed = 1; seed <= SEEDS; seed++) {
        reset(seed);
        Player* p = &state.env.players[0];
        int free_cell = osrs_first_empty_inventory_cell(p->inventory_cells, -1);
        assert(free_cell >= 0);
        p->inventory_cells[free_cell] = osrs_inventory_cell_from_content_code(riskfight_supply_code(OSRS_CONSUMABLE_ANGLERFISH, 0));
        assert(osrs_first_empty_inventory_cell(p->inventory_cells, -1) < 0);
        p->current_hitpoints = STRIP_HP;
        p->attack_timer = 2;
        int actions[RF_HEADS];
        for (int tick = FIRST_TICK; tick < FIRST_TICK + TICKS; tick++) {
            state.env.tick = tick;
            decide(&QUIET, seed, actions);
            assert(!strips(actions));
        }
    }
}

/* The orb under the script's rule: the axe in hand, his swing a tick away, above 20 hitpoints: "Feel" takes 10. */
static void test_orb_takes_ten(void) {
    reset(7);
    Player* p = &state.env.players[0];
    p->current_hitpoints = 60;
    p->attack_timer = 2;
    state.env.tick = FIRST_TICK;
    int actions[RF_HEADS];
    decide(&AXER, 7, actions);
    execute(actions);
    assert(p->equipped[GEAR_SLOT_WEAPON] == ITEM_DHAROKS_GREATAXE && dharok_pieces_worn() == 4);
    p->attack_timer = 1;
    state.env.tick = FIRST_TICK + 1;
    decide(&ORBER, 7, actions);
    assert(actions[RF_ORB] == 1);
    execute(actions);
    assert(p->current_hitpoints == 50);
    printf("orb: 60 -> %d hitpoints before the axe swing\n", p->current_hitpoints);
}

int main(void) {
    test_strip_then_reequip_before_axe();
    test_full_bag_never_strips();
    test_orb_takes_ten();
    printf("riskfight dharok script: passed\n");
    return 0;
}
