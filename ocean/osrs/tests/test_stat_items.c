#include <assert.h>
#include <stdio.h>
#include "../encounters/encounter_riskfight.h"
#include "riskfight_tape.h"

/* Items without a hand-written row are priced from their own stats (Java item table), exactly, not by the nearest supported item. */
static RiskfightState state;
static RiskfightContext context;

int main(void) {
    riskfight_init_context((EncounterContext*)&context);
    riskfight_finalize_context((EncounterState*)&state, (EncounterContext*)&context);
    osrs_stat_items_clear();
    enum { EXCALIBUR = 35, MONKS_ROBE = 542, AMMO_MOULD = 4 };
    assert(osrs_item_index_from_raw_id(EXCALIBUR) < 0);
    int sword = gs_item_priced_from_stats(EXCALIBUR, 0);
    assert(sword >= NUM_ITEMS && osrs_item_row_valid(sword));
    const Item* row = &ITEM_DATABASE[sword];
    assert(row->item_id == EXCALIBUR && row->slot == SLOT_WEAPON && row->attack_slash == 29 && row->attack_stab == 20 && row->attack_crush == -2 &&
        row->melee_strength == 25 && row->attack_speed == 5 && !row->two_handed && row->effect_mask == 0);
    assert(get_item_attack_style((uint8_t)sword) == ATTACK_STYLE_MELEE && osrs_spec_cost((uint8_t)sword) == 0);
    assert(gs_item_priced_from_stats(EXCALIBUR, 0) == sword);                 /* registered once */
    assert(osrs_item_index_from_raw_id(EXCALIBUR) == sword);
    int robe = gs_item_priced_from_stats(MONKS_ROBE, 0);
    assert(robe >= NUM_ITEMS && ITEM_DATABASE[robe].slot == SLOT_LEGS && ITEM_DATABASE[robe].prayer == 5);
    assert(gs_item_priced_from_stats(AMMO_MOULD, 0) < 0);                     /* no stats row: explicit unknown */
    assert(gs_item_priced_from_stats(ITEM_DATABASE[ITEM_WHIP].item_id, 0) == ITEM_WHIP); /* a fixed row stays itself */
    assert(osrs_inventory_content_code_from_item((uint8_t)sword) == osrs_inventory_content_code_from_raw_osrs_id(27281));

    /* He fights with it in the sim: weapon clock, style and bonuses come from the row. */
    riskfight_reset((EncounterState*)&state, (EncounterContext*)&context, 7);
    Player* him = &state.env.players[1];
    him->equipped[GEAR_SLOT_WEAPON] = (uint8_t)sword;
    osrs_refresh_player_equipment(him);
    assert(get_slot_weapon_attack_style(him) == ATTACK_STYLE_MELEE);
    osrs_stat_items_clear();
    assert(osrs_item_index_from_raw_id(EXCALIBUR) < 0);
    riskfight_destroy_context((EncounterContext*)&context);
    puts("Stat-priced item contracts passed");
    return 0;
}
