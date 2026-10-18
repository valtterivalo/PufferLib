/* Training permutes both bags at every fight (riskfight_training_shuffle_bags): two fights draw different layouts, and each bag holds exactly its kit's items.
 * The regear (fight_starts 0), no pool and no domain randomization, so each bag is its loadout untouched but for the order. */
#include "../../osrs_riskfight/osrs_riskfight.h"
#include <assert.h>
#include <stdio.h>

static float observations[2][RF_OBS_SIZE];
static float actions[2][RF_HEADS];
static float rewards[2], terminals[2], resets[2];
static unsigned char masks[2][RF_MASK_SIZE];

static int cmp_code(const void* a, const void* b) { return (int)*(const uint16_t*)a - (int)*(const uint16_t*)b; }

/** The bag of `seat` as content codes, slot by slot. */
static void bag_of(const Env* e, int seat, uint16_t* codes) {
    for (int slot = 0; slot < OSRS_INVENTORY_SIZE; slot++) codes[slot] = e->state.env.players[seat].inventory_cells[slot].content_code;
}

static void check_multiset(const Env* e, int seat, const uint16_t* codes) {
    const RiskfightLoadout* loadout = e->context.loadout[seat];
    uint16_t have[OSRS_INVENTORY_SIZE], want[OSRS_INVENTORY_SIZE];
    for (int slot = 0; slot < OSRS_INVENTORY_SIZE; slot++) {
        have[slot] = codes[slot];
        want[slot] = osrs_inventory_cell_from_content_code(loadout->inventory[slot]).content_code;
    }
    qsort(have, OSRS_INVENTORY_SIZE, sizeof(*have), cmp_code);
    qsort(want, OSRS_INVENTORY_SIZE, sizeof(*want), cmp_code);
    assert(!memcmp(have, want, sizeof(have)));
}

int main(void) {
    DictItem items[] = {{.key = "opponent_type", .value = 0}, {.key = "self_play", .value = 0}, {.key = "session_ticks", .value = 3000},
        {.key = "curveball_probability", .value = 0.1}, {.key = "death_price", .value = 0}, {.key = "live_loadout", .value = 3},
        {.key = "opponent_pool", .value = 0}, {.key = "domain_randomization", .value = 0}, {.key = "deployable", .value = 1}, {.key = "fight_starts", .value = 0}};
    Dict kwargs = {.items = items, .size = sizeof(items) / sizeof(*items)};
    int fights = 0;
    for (unsigned seed = 1; seed <= 50; seed++) {
        Env* e = (Env*)calloc(1, sizeof(*e));
        e->rng = seed;
        puf_init(e, &kwargs);
        for (int i = 0; i < 2; i++) e->agents[i] = (Agent){
            .observations = observations[i], .actions = actions[i], .rewards = &rewards[i], .terminals = &terminals[i], .resets = &resets[i], .action_mask = masks[i], .policy = i};
        puf_reset(e);
        uint16_t first[2][OSRS_INVENTORY_SIZE], second[2][OSRS_INVENTORY_SIZE];
        for (int seat = 0; seat < 2; seat++) {
            bag_of(e, seat, first[seat]);
            check_multiset(e, seat, first[seat]);
        }
        /* The next fight of the session, a rematch: the same kits, regeared. */
        e->wait_transition = (RiskfightTransition){.delay = 0, .rematch = 1, .rude = 0};
        riskfight_native_next_fight(e);
        for (int seat = 0; seat < 2; seat++) {
            bag_of(e, seat, second[seat]);
            check_multiset(e, seat, second[seat]);
            assert(memcmp(first[seat], second[seat], sizeof(first[seat])) != 0);
        }
        fights += 2;
        puf_close(e);
        free(e);
    }
    printf("bag layout: %d fights, every bag its kit's items, a new layout per fight on both seats\n", fights);
    return 0;
}
