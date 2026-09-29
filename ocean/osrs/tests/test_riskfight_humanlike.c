#include <assert.h>
#include <stdio.h>
#include <math.h>
#include <string.h>
#include "../encounters/encounter_riskfight.h"

static RiskfightState state;
static RiskfightContext context;

static void reset(void) {
    riskfight_reset((EncounterState*)&state, (EncounterContext*)&context, 12345);
    context.self_play = 1;
}

static void decide(const RiskfightProfile* profile, uint32_t seed, int* actions) {
    float obs[RF_OBS_SIZE];
    riskfight_write_observation(&state, 0, obs);
    riskfight_script(obs, profile, seed, 0, actions);
    for (int head = 0; head < RF_HEADS; head++)
        assert(actions[head] >= 0 && actions[head] < RF_ACTION_DIMS[head]);
}

static int inventory_action(uint8_t item) {
    for (int slot = 0; slot < OSRS_INVENTORY_SIZE; slot++)
        if (osrs_inventory_cell_metadata(&state.env.players[0].inventory_cells[slot])->item_idx == item) return slot + 1;
    assert(0 && "item not in inventory");
    return 0;
}

static void assert_rate(int events, int trials, int per_mille) {
    double p = per_mille / 1000.0;
    assert(fabs(events - trials * p) <= 5 * sqrt(trials * p * (1 - p)) + 5);
}

static void test_pools_never_leak_heldout(void) {
    for (uint32_t roll = 0; roll < 4096; roll++) {
        ptrdiff_t train = riskfight_pick_profile(RISKFIGHT_HUMAN, roll * 2654435761u) - RISKFIGHT_HUMAN_PROFILES;
        ptrdiff_t heldout = riskfight_pick_profile(RISKFIGHT_HUMAN_HELDOUT, roll * 2654435761u) - RISKFIGHT_HUMAN_PROFILES;
        assert(train >= 0 && train < RISKFIGHT_HUMAN_TRAIN_PROFILES);
        assert(heldout >= RISKFIGHT_HUMAN_TRAIN_PROFILES &&
            heldout < RISKFIGHT_HUMAN_TRAIN_PROFILES + RISKFIGHT_HUMAN_HELDOUT_PROFILES);
    }
}

static void test_deterministic(void) {
    reset();
    state.env.tick = 77;
    int first[RF_HEADS], second[RF_HEADS];
    decide(&RISKFIGHT_HUMAN_PROFILES[0], 1, first);
    decide(&RISKFIGHT_HUMAN_PROFILES[0], 1, second);
    assert(memcmp(first, second, sizeof(first)) == 0);
}

static void test_hazards_match_profile(const RiskfightProfile* profile) {
    reset();
    state.env.players[0].current_hitpoints = 60;
    int consumes = 0, tabs = 0;
    const int ticks = 20000;
    for (int tick = 0; tick < ticks; tick++) {
        state.env.tick = tick;
        int actions[RF_HEADS];
        decide(profile, 7, actions);
        consumes += actions[RF_FOOD] != 0;
        tabs += actions[RF_PRIMARY] == RF_TELEPORT;
    }
    assert_rate(consumes, ticks, profile->consume_pm[1]);
    assert_rate(tabs, ticks, profile->tab_pm[0][0]);
}

static void test_one_weapon_decision_per_attack(void) {
    reset();
    state.env.players[0].current_hitpoints = 110;
    state.visible[0].health_bar = 18;
    int vw = 0;
    for (uint32_t seed = 1; seed <= 400; seed++) {
        int decisions[3];
        for (int timer = 3; timer >= 1; timer--) {
            state.env.tick = 100 + 3 - timer;
            state.env.players[0].attack_timer = timer;
            int actions[RF_HEADS];
            decide(&RISKFIGHT_HUMAN_PROFILES[0], seed, actions);
            decisions[3 - timer] = actions[RF_WEAPON] == inventory_action(ITEM_VOIDWAKER);
        }
        assert(decisions[0] == decisions[1] && decisions[1] == decisions[2]);
        vw += decisions[0];
    }
    assert(vw > 0 && vw < 400);
}

static void test_full_hp_never_eats_or_specs_without_energy(void) {
    reset();
    state.env.players[0].current_hitpoints = 121;
    state.env.players[0].special_energy = 0;
    for (int tick = 0; tick < 500; tick++) {
        state.env.tick = tick;
        int actions[RF_HEADS];
        decide(&RISKFIGHT_HUMAN_PROFILES[1], 3, actions);
        assert(!actions[RF_FOOD] && !actions[RF_COMBO] && actions[RF_SPECIAL] == 0);
    }
}

int main(void) {
    riskfight_init_context((EncounterContext*)&context);
    riskfight_finalize_context((EncounterState*)&state, (EncounterContext*)&context);
    test_pools_never_leak_heldout();
    test_deterministic();
    for (int i = 0; i < RISKFIGHT_HUMAN_TRAIN_PROFILES + RISKFIGHT_HUMAN_HELDOUT_PROFILES; i++)
        test_hazards_match_profile(&RISKFIGHT_HUMAN_PROFILES[i]);
    test_one_weapon_decision_per_attack();
    test_full_hp_never_eats_or_specs_without_energy();
    riskfight_destroy_context((EncounterContext*)&context);
    puts("Riskfight human profile contracts passed");
}
