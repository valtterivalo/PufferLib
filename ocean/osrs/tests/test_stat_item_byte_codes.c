#include <assert.h>
#include <stdio.h>
#include "riskfight_rollout.h"

/* All 40 stat slots keep their one-byte code (the item index) through the riskfight observation and the lookahead fingerprint. Run from the repository root:
 *   cc -std=c11 -O2 -I. -o /tmp/test_stat_item_byte_codes ocean/osrs/tests/test_stat_item_byte_codes.c -lm && /tmp/test_stat_item_byte_codes */
static RiskfightState state;
static RiskfightContext context;
static float obs[RF_OBS_SIZE];

int main(void) {
    riskfight_init_context((EncounterContext*)&context);
    riskfight_finalize_context((EncounterState*)&state, (EncounterContext*)&context);
    riskfight_reset((EncounterState*)&state, (EncounterContext*)&context, 7);
    osrs_stat_items_clear();
    uint64_t seen[OSRS_STAT_ITEM_SLOTS];
    for (int i = 0; i < OSRS_STAT_ITEM_SLOTS; i++) {
        Item row = ITEM_DATABASE[ITEM_WHIP];
        row.item_id = (uint16_t)(40000 + i);
        int index = osrs_stat_item_register(&row, ATTACK_STYLE_MELEE);
        assert(index == NUM_ITEMS + i && osrs_item_byte_code((uint16_t)index) == index);
        state.env.players[0].equipped[GEAR_SLOT_WEAPON] = (uint16_t)index;
        state.visible[0].equipment[GEAR_SLOT_WEAPON] = (uint16_t)index;
        riskfight_write_observation(&state, 0, obs);
        assert(obs[RF_EQUIPPED_START + GEAR_SLOT_WEAPON] == (float)index / RF_OBSERVATION_ITEM_SCALE);
        assert(obs[RF_OPPONENT_START + GEAR_SLOT_WEAPON] == (float)index / RF_OBSERVATION_ITEM_SCALE);
        state.env.players[1].equipped[GEAR_SLOT_WEAPON] = (uint16_t)index;
        seen[i] = la_fingerprint(&state);
        for (int j = 0; j < i; j++) assert(seen[j] != seen[i]);
    }
    assert(NUM_ITEMS + OSRS_STAT_ITEM_SLOTS - 1 == 253);
    osrs_stat_items_clear();
    riskfight_destroy_context((EncounterContext*)&context);
    printf("Stat item byte codes passed: slots %d..%d\n", NUM_ITEMS, NUM_ITEMS + OSRS_STAT_ITEM_SLOTS - 1);
    return 0;
}
