/* The user's Dharok kit (riskfight_model.h RISKFIGHT_DHAROK_LOADOUT) end to end: its shape and set effect, the training draw that mixes it with the Voidwaker kits
 * (live_loadout 4), the deployable mask's body/legs strip and re-equip and its orb rule, and the live server's click records for them.
 *   cc -std=gnu11 -O2 -I. -Isrc -Wno-comment -o /tmp/t ocean/osrs/tests/test_riskfight_dharok_kit.c -lm -lpthread && /tmp/t   (cwd = repo root) */
#define GS_RL_SERVER_NO_MAIN
#include "gs_rl_server.c"
#include "ini.h" /* Dict: riskfight_training_config */
#include "../../osrs_riskfight/riskfight_training.h"

static RiskfightState state;
static RiskfightContext context;

static void reset_with(const RiskfightLoadout* kit) {
    context.self_play = 1;
    context.loadout[0] = kit;
    riskfight_reset((EncounterState*)&state, (EncounterContext*)&context, 123);
    context.loadout[0] = NULL;
}

static unsigned char* mask_of(const RiskfightState* s, int deployable) {
    static unsigned char mask[RF_MASK_SIZE];
    riskfight_deployable = deployable;
    riskfight_write_action_mask(s, 0, mask);
    riskfight_deployable = 0;
    return mask;
}

/** The mask bit of a bag head for the item in physical slot `slot` (the mask indexes the canonical bag). */
static int bag_bit(const unsigned char* mask, const Player* p, int head, int slot) {
    int actions[RF_HEADS] = {0};
    actions[head] = slot + 1;
    riskfight_bag_to_canonical(p, actions);
    return mask[RF_HEAD_OFFSET[head] + actions[head]];
}

static int first_slot_of(const Player* p, OsrsConsumableKind kind, uint8_t item) {
    for (int slot = 0; slot < OSRS_INVENTORY_SIZE; slot++) {
        const OsrsItemContentMetadata* m = osrs_inventory_cell_metadata(&p->inventory_cells[slot]);
        if (osrs_inventory_cell_is_empty(&p->inventory_cells[slot])) continue;
        if (kind != OSRS_CONSUMABLE_NONE ? m->consumable_kind == kind : m->item_idx == item) return slot;
    }
    return -1;
}

static void test_kit_shape_and_set_effect(void) {
    const RiskfightLoadout* kit = &RISKFIGHT_DHAROK_LOADOUT;
    int pie = 0, halibut = 0, marlin = 0, restore = 0, brew = 0, combat = 0, orb = 0, sack = 0, tab = 0, maul = 0, axe = 0;
    for (int slot = 0; slot < OSRS_INVENTORY_SIZE; slot++) {
        assert(kit->inventory[slot] != 0);
        const OsrsItemContentMetadata* m = osrs_item_content_metadata(kit->inventory[slot]);
        pie += m->consumable_kind == OSRS_CONSUMABLE_SUMMER_PIE; halibut += m->consumable_kind == OSRS_CONSUMABLE_HALIBUT;
        marlin += m->consumable_kind == OSRS_CONSUMABLE_MARLIN; restore += m->consumable_kind == OSRS_CONSUMABLE_SUPER_RESTORE;
        brew += m->consumable_kind == OSRS_CONSUMABLE_BREW; combat += m->consumable_kind == OSRS_CONSUMABLE_SUPER_COMBAT;
        orb += m->click_action == OSRS_CLICK_SELF_DAMAGE; sack += m->consumable_kind == OSRS_CONSUMABLE_VENGEANCE_SACK;
        tab += m->consumable_kind == OSRS_CONSUMABLE_TELEPORT;
        maul += m->item_idx == ITEM_GRANITE_MAUL_ORNATE; axe += m->item_idx == ITEM_DHAROKS_GREATAXE;
    }
    assert(pie == 2 && halibut == 6 && marlin == 10 && restore == 2 && brew == 2 && combat == 1 && orb == 1 && sack == 1 && tab == 1 && maul == 1 && axe == 1);
    assert(kit->inventory[0] == riskfight_supply_code(OSRS_CONSUMABLE_SUMMER_PIE, 2) && kit->vengeance_sacks == 25);
    assert(kit->equipment[GEAR_SLOT_HEAD] == ITEM_DHAROKS_HELM && kit->equipment[GEAR_SLOT_BODY] == ITEM_DHAROKS_PLATEBODY &&
        kit->equipment[GEAR_SLOT_LEGS] == ITEM_DHAROKS_PLATELEGS && kit->equipment[GEAR_SLOT_WEAPON] == ITEM_VOIDWAKER &&
        kit->equipment[GEAR_SLOT_SHIELD] == ITEM_DRAGON_DEFENDER && kit->equipment[GEAR_SLOT_RING] == ITEM_RING_OF_RECOIL);

    reset_with(kit);
    Player* p = &state.env.players[0];
    int full = 0;
    for (int slot = 0; slot < OSRS_INVENTORY_SIZE; slot++) full += !osrs_inventory_cell_is_empty(&p->inventory_cells[slot]);
    assert(full == 28);
    assert(p->equipment_effect_profile.dharok_piece_count == 3); /* helm, body, legs: the Voidwaker is wielded */
    /* The axe is two-handed: the defender needs a free cell, as after a meal. */
    p->inventory_cells[first_slot_of(p, OSRS_CONSUMABLE_MARLIN, 0)] = osrs_inventory_cell_empty();
    int axe_slot = first_slot_of(p, OSRS_CONSUMABLE_NONE, ITEM_DHAROKS_GREATAXE);
    assert(axe_slot >= 0 && osrs_equip_from_cell(p, p->inventory_cells, axe_slot) == GEAR_SLOT_WEAPON);
    assert(p->equipped[GEAR_SLOT_WEAPON] == ITEM_DHAROKS_GREATAXE && p->equipped[GEAR_SLOT_SHIELD] == ITEM_NONE);
    assert(p->equipment_effect_profile.dharok_piece_count == 4);
    puts("kit: 28 cells as the snapshot, the 4-piece set effect with the axe wielded");
}

static void test_mixed_draw(void) {
    RiskfightTraining training;
    memset(&training, 0, sizeof(training));
    training.rng = 0x2545f491u;
    const RiskfightLoadout* kits[4] = {&RISKFIGHT_DHAROK_LOADOUT, &RISKFIGHT_LIVE_LOADOUT, &RISKFIGHT_VW_LOADOUT, &RISKFIGHT_VW_PIES_LOADOUT};
    enum { DRAWS = 60000 };
    int counts[4] = {0}, vw_only_dharok = 0;
    for (int i = 0; i < DRAWS; i++) {
        const RiskfightLoadout* drawn = riskfight_training_draw_kit(&training, RF_KIT_MIX_DHAROK);
        int k = 0;
        while (k < 4 && kits[k] != drawn) k++;
        assert(k < 4);
        counts[k]++;
        vw_only_dharok += riskfight_training_draw_kit(&training, RF_KIT_MIX_VW) == &RISKFIGHT_DHAROK_LOADOUT;
    }
    const double expected[4] = {0.5, 1.0 / 6, 1.0 / 6, 1.0 / 6};
    for (int k = 0; k < 4; k++) {
        double sigma = sqrt(DRAWS * expected[k] * (1 - expected[k]));
        assert(fabs(counts[k] - DRAWS * expected[k]) <= 4.5 * sigma);
    }
    assert(vw_only_dharok == 0);
    printf("draw: Dharok %.3f, live %.3f, vw %.3f, vw pies %.3f of %d resets (live_loadout 3 never draws Dharok)\n", (double)counts[0] / DRAWS,
        (double)counts[1] / DRAWS, (double)counts[2] / DRAWS, (double)counts[3] / DRAWS, DRAWS);
}

static void test_strip_and_reequip(void) {
    reset_with(&RISKFIGHT_DHAROK_LOADOUT);
    Player* p = &state.env.players[0];
    /* A full bag: nothing comes off. */
    unsigned char* mask = mask_of(&state, 1);
    assert(!mask[RF_HEAD_OFFSET[RF_BODY] + RF_UNEQUIP] && !mask[RF_HEAD_OFFSET[RF_LEGS] + RF_UNEQUIP]);
    int eaten = first_slot_of(p, OSRS_CONSUMABLE_HALIBUT, 0);
    p->inventory_cells[eaten] = osrs_inventory_cell_empty();
    mask = mask_of(&state, 1);
    assert(mask[RF_HEAD_OFFSET[RF_BODY] + RF_UNEQUIP] && mask[RF_HEAD_OFFSET[RF_LEGS] + RF_UNEQUIP]);
    /* The other armour heads stay masked; the weapon and the ring never come off. */
    assert(!mask[RF_HEAD_OFFSET[RF_HEAD] + RF_UNEQUIP] && !mask[RF_HEAD_OFFSET[RF_WEAPON] + RF_UNEQUIP] && !mask[RF_HEAD_OFFSET[RF_RING] + RF_UNEQUIP]);

    int actions[2 * RF_HEADS] = {0};
    actions[RF_BODY] = RF_UNEQUIP;
    riskfight_step((EncounterState*)&state, (EncounterContext*)&context, actions);
    assert(p->equipped[GEAR_SLOT_BODY] == ITEM_NONE);
    assert(osrs_inventory_cell_metadata(&p->inventory_cells[eaten])->item_idx == ITEM_DHAROKS_PLATEBODY);
    assert(p->equipment_effect_profile.dharok_piece_count == 2);
    mask = mask_of(&state, 1);
    assert(bag_bit(mask, p, RF_BODY, eaten));                  /* back on */
    assert(!mask[RF_HEAD_OFFSET[RF_BODY] + RF_UNEQUIP]);        /* nothing worn there */
    assert(!mask[RF_HEAD_OFFSET[RF_LEGS] + RF_UNEQUIP]);        /* the body took the free cell */

    /* A bag piece never swaps out a worn one. */
    RiskfightState worn = state;
    worn.env.players[0].equipped[GEAR_SLOT_BODY] = ITEM_FIGHTER_TORSO;
    assert(!bag_bit(mask_of(&worn, 1), &worn.env.players[0], RF_BODY, eaten));
    assert(bag_bit(mask_of(&worn, 0), &worn.env.players[0], RF_BODY, eaten)); /* the full mask keeps the swap */

    memset(actions, 0, sizeof(actions));
    actions[RF_BODY] = eaten + 1;
    riskfight_step((EncounterState*)&state, (EncounterContext*)&context, actions);
    assert(p->equipped[GEAR_SLOT_BODY] == ITEM_DHAROKS_PLATEBODY && osrs_inventory_cell_is_empty(&p->inventory_cells[eaten]));
    assert(p->equipment_effect_profile.dharok_piece_count == 3);
    mask = mask_of(&state, 1);
    assert(mask[RF_HEAD_OFFSET[RF_LEGS] + RF_UNEQUIP]);
    puts("strip: body and legs come off into a free cell and go back on into a bare slot");
}

static void test_orb_rule(void) {
    reset_with(&RISKFIGHT_DHAROK_LOADOUT);
    Player* p = &state.env.players[0];
    for (int deployable = 0; deployable < 2; deployable++) {
        p->current_hitpoints = 121;
        assert(mask_of(&state, deployable)[RF_HEAD_OFFSET[RF_ORB] + 1]);
        p->current_hitpoints = 2;
        assert(mask_of(&state, deployable)[RF_HEAD_OFFSET[RF_ORB] + 1]);
        RiskfightMaskKey above, at_one;
        riskfight_mask_key(&state, 0, NULL, &above);
        p->current_hitpoints = 1;
        assert(!mask_of(&state, deployable)[RF_HEAD_OFFSET[RF_ORB] + 1]);
        riskfight_mask_key(&state, 0, NULL, &at_one);
        assert(above.orb_ok && !at_one.orb_ok);
    }
    p->current_hitpoints = 80;
    int orb = first_slot_of(p, OSRS_CONSUMABLE_LOCATOR_ORB, 0);
    assert(orb >= 0);
    p->inventory_cells[orb] = osrs_inventory_cell_empty();
    assert(!mask_of(&state, 0)[RF_HEAD_OFFSET[RF_ORB] + 1] && !mask_of(&state, 1)[RF_HEAD_OFFSET[RF_ORB] + 1]);
    reset_with(&RISKFIGHT_VW_LOADOUT);
    assert(!mask_of(&state, 1)[RF_HEAD_OFFSET[RF_ORB] + 1]);
    puts("orb: offered only with an orb in the bag and more than 1 hitpoint");
}

/* A hand-written request of the user's kit: Dharok worn (the 100 ids the client shows), the snapshot's bag with the halibut of slot 2 eaten. */
static const char* const DHAROK_REQUEST =
    "LA 7 804\n"
    "B 79 115 100 100 2 1 0.11899791231732777 1 806 808 804 3 0 1 24\n"
    "K 363066 1000000\n"
    "F 1 1 1 100\n"
    "O 92 72 118 118 120 99 100 0 0 0 0 22 0 3 0 0 0 1 1 0 4 -1 4 4587 0 0 -1 OBSERVED 3 3\n"
    "Q none 3 802 803 804\n"
    "W 4880 21295 1725 27690 4892 12954 -1 4898 -1 7462 11840 -1 2550\n"
    "G 7218 7218 -1 32336 24598 32352 32336 32336 24598 32352 32336 32336 32352 6685 32352 6685 32352 32352 32352 32352 24225 4718 12695 32352 22081 32352 24621 8013\n"
    "SHIELD OBSERVED 8849\n"
    "ARMOR OBSERVED 10828 6570 1725 10551 2497 7462 4131\n"
    "PS 0 KNOWN 0 0 0\n"
    "PS 1 CLEAR_PRIOR\n"
    "RA OBSERVED 1\n"
    "P KNOWN 1\n"
    "D 0 0 0 0 0 1 1 15 0 0 10000 0 -1\n"
    "R KNOWN 0\n"
    "END\n";

static void test_server_records(void) {
    g_fight.net = NULL;
    g_fight.rng = 0x9e3779b9u;
    g_fight.hard = 1;
    g_ablation.hp = HP_UNIFORM; g_ablation.escape_model = 1; g_ablation.his_defence = 99; g_ablation.panic = 0.8; g_ablation.panic_hp = 25;
    observer_register_items();
    sim_init(&g_base);
    riskfight_deployable = 1;

    static Request q;
    FILE* in = fmemopen((void*)DHAROK_REQUEST, strlen(DHAROK_REQUEST), "r");
    read_request(in, &q);
    fclose(in);
    RtSeen seen = {-1, 0, 0, 0, 0, 0, -1, -1, 0, 0, rt_bag_servings(q.bag)};
    request_declare_priors(&q);
    assert(server_unavailable(&q) == NULL);
    RtRecord records[GS_WIRE_MAX_RECORDS];
    int count;
    rt_decide(&q, &seen, 1, q.tick, records, &count);
    RiskfightState* s = &g_base.state;
    const Player* us = &s->env.players[0];
    /* The kit reaches the sim: the Dharok rows worn, the orb, halibut and marlin as their own cells, not inert filler. */
    assert(us->equipped[GEAR_SLOT_HEAD] == ITEM_DHAROKS_HELM && us->equipped[GEAR_SLOT_BODY] == ITEM_DHAROKS_PLATEBODY &&
        us->equipped[GEAR_SLOT_LEGS] == ITEM_DHAROKS_PLATELEGS);
    assert(osrs_inventory_cell_metadata(&us->inventory_cells[24])->click_action == OSRS_CLICK_SELF_DAMAGE);
    assert(osrs_inventory_cell_metadata(&us->inventory_cells[3])->consumable_kind == OSRS_CONSUMABLE_HALIBUT);
    assert(osrs_inventory_cell_metadata(&us->inventory_cells[5])->consumable_kind == OSRS_CONSUMABLE_MARLIN);
    assert(osrs_inventory_cell_metadata(&us->inventory_cells[21])->item_idx == ITEM_DHAROKS_GREATAXE);
    assert(osrs_inventory_cell_is_empty(&us->inventory_cells[2]));

    unsigned char mask[RF_MASK_SIZE];
    riskfight_write_action_mask(s, 0, mask);
    assert(mask[RF_HEAD_OFFSET[RF_BODY] + RF_UNEQUIP] && mask[RF_HEAD_OFFSET[RF_ORB] + 1]);
    int actions[RF_HEADS] = {0};
    actions[RF_BODY] = RF_UNEQUIP;
    actions[RF_ORB] = 1;
    RtRecord out[GS_WIRE_MAX_RECORDS];
    int n = rt_records(actions, s, out);
    assert(n == 2);
    assert(out[0].kind == GS_WIRE_UNEQUIP_SLOT && out[0].arg == 4 && WORN_TO_GEAR[out[0].arg] == GEAR_SLOT_BODY); /* the client's worn slot 4 */
    assert(out[1].kind == GS_WIRE_CLICK_SLOT && out[1].arg == 24);                                              /* Feel on the orb */

    /* Legs back on from the bag: a CLICK_SLOT (Wear) on the bag piece, before the meals, as the sim queues it. */
    static Request bare;
    in = fmemopen((void*)DHAROK_REQUEST, strlen(DHAROK_REQUEST), "r");
    read_request(in, &bare);
    fclose(in);
    bare.worn[7] = -1;
    bare.bag[2] = 4898;
    seen.servings = rt_bag_servings(bare.bag);
    request_declare_priors(&bare);
    assert(server_unavailable(&bare) == NULL);
    rt_decide(&bare, &seen, 0, bare.tick, records, &count);
    assert(us->equipped[GEAR_SLOT_LEGS] == ITEM_NONE && osrs_inventory_cell_metadata(&us->inventory_cells[2])->item_idx == ITEM_DHAROKS_PLATELEGS);
    riskfight_write_action_mask(s, 0, mask);
    assert(bag_bit(mask, us, RF_LEGS, 2));
    memset(actions, 0, sizeof(actions));
    actions[RF_LEGS] = 3;
    actions[RF_FOOD] = 6; /* the marlin of slot 5 */
    n = rt_records(actions, s, out);
    assert(n == 2 && out[0].kind == GS_WIRE_CLICK_SLOT && out[0].arg == 2 && out[1].kind == GS_WIRE_CLICK_SLOT && out[1].arg == 5);
    riskfight_deployable = 0;
    puts("server: body off is UNEQUIP_SLOT 4, the orb CLICK_SLOT on its slot, legs back on CLICK_SLOT on the bag piece");
}

int main(void) {
    riskfight_init_context((EncounterContext*)&context);
    riskfight_finalize_context((EncounterState*)&state, (EncounterContext*)&context);
    test_kit_shape_and_set_effect();
    test_mixed_draw();
    test_strip_and_reequip();
    test_orb_rule();
    test_server_records();
    riskfight_destroy_context((EncounterContext*)&context);
    puts("riskfight Dharok kit PASS");
    return 0;
}
