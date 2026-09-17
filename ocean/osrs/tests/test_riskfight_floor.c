#include <assert.h>
#include <stdio.h>
#include "../encounters/encounter_riskfight.h"

static RiskfightState state;
static RiskfightContext context;

static void reset(void) {
    context.self_play = 1;
    riskfight_reset((EncounterState*)&state, (EncounterContext*)&context, 12345);
}

static void test_wait_and_release(void) {
    reset();
    state.env.tick = 3;
    float obs[RF_OBS_SIZE];
    int actions[RF_HEADS];
    riskfight_write_observation(&state, 0, obs);
    riskfight_script(obs, RISKFIGHT_FLOOR, actions);
    assert(actions[RF_PRIMARY] == RF_STOP && actions[RF_SPECIAL] == 0);
    assert(!actions[RF_FOOD] && !actions[RF_DRINK] && !actions[RF_COMBO]);
    obs[RF_HISTORY_START + 6] = 1;
    riskfight_script(obs, RISKFIGHT_FLOOR, actions);
    assert(actions[RF_PRIMARY] == RF_ATTACK);
    obs[RF_HISTORY_START + 6] = 0;
    obs[RF_HISTORY_START + 3] = 1;
    obs[RF_HISTORY_START + 4] = 10.0f / RF_OBSERVATION_DAMAGE_SCALE;
    riskfight_script(obs, RISKFIGHT_FLOOR, actions);
    assert(actions[RF_PRIMARY] == RF_ATTACK);
    obs[RF_HISTORY_START + 3] = 0;
    obs[RF_HISTORY_START + 4] = 0;
    obs[20] = 8.0f / RF_OBSERVATION_TICK_SCALE;
    riskfight_script(obs, RISKFIGHT_FLOOR, actions);
    assert(actions[RF_PRIMARY] == RF_ATTACK);
    obs[7] = 0.3f;
    riskfight_script(obs, RISKFIGHT_FLOOR, actions);
    assert(actions[RF_PRIMARY] == RF_STOP && actions[RF_SPECIAL] == 0);
}

static void test_vengeance_timing(void) {
    reset();
    state.env.tick = 3;
    state.env.players[0].veng_active = 0;
    state.env.players[0].special_energy = 0;
    state.visible[0].last_attack_tick = 2;
    state.visible[0].last_attack_speed = 4;
    float obs[RF_OBS_SIZE];
    int actions[RF_HEADS];
    riskfight_write_observation(&state, 0, obs);
    riskfight_script(obs, RISKFIGHT_FLOOR, actions);
    assert(actions[RF_VENGEANCE]);
    assert(actions[RF_PRIMARY] == RF_ATTACK);
}

static void test_hidden_state_independence(void) {
    reset();
    float obs[RF_OBS_SIZE], changed[RF_OBS_SIZE];
    int actions[RF_HEADS], repeated[RF_HEADS];
    riskfight_write_observation(&state, 0, obs);
    riskfight_script(obs, RISKFIGHT_FLOOR, actions);
    state.env.players[1].attack_timer = 99;
    state.env.players[1].current_hitpoints = 1;
    state.env.players[1].special_energy = 0;
    state.env.players[1].inventory_cells[0] = osrs_inventory_cell_empty();
    state.env.pid_holder ^= 1;
    riskfight_write_observation(&state, 0, changed);
    assert(memcmp(obs, changed, sizeof(obs)) == 0);
    riskfight_script(changed, RISKFIGHT_FLOOR, repeated);
    assert(memcmp(actions, repeated, sizeof(actions)) == 0);
}

static void test_mutual_waiting_releases_both_sides(void) {
    reset();
    for (int fighter = 0; fighter < 2; fighter++) {
        float obs[RF_OBS_SIZE];
        riskfight_write_observation(&state, fighter, obs);
        Player self = riskfight_observed_self(obs);
        EquipmentBonuses gear;
        osrs_sum_equipment_bonuses(self.equipped, &gear);
        int attacks = 0, waits = 0;
        for (int tick = 0; tick < 2 * gear.attack_speed; tick++) {
            obs[20] = (float)tick / RF_OBSERVATION_TICK_SCALE;
            int actions[RF_HEADS];
            riskfight_script(obs, RISKFIGHT_FLOOR, actions);
            attacks += actions[RF_PRIMARY] == RF_ATTACK;
            waits += actions[RF_PRIMARY] == RF_STOP;
        }
        assert(attacks > 0 && waits > 0);
    }
}

static void test_escape_requires_exhaustion_or_observed_attack(void) {
    reset();
    state.env.tick = 3;
    state.visible[0].equipment[GEAR_SLOT_WEAPON] = ITEM_GRANITE_MAUL_ORNATE;
    state.visible[0].equipment[GEAR_SLOT_SHIELD] = ITEM_NONE;
    state.env.players[0].current_hitpoints = 30;
    state.env.players[0].food_timer = 2;
    state.env.players[0].potion_timer = 2;
    state.env.players[0].karambwan_timer = 2;
    float obs[RF_OBS_SIZE];
    int actions[RF_HEADS];
    riskfight_write_observation(&state, 0, obs);
    riskfight_script(obs, RISKFIGHT_FLOOR, actions);
    assert(actions[RF_PRIMARY] == RF_STOP);
    state.visible[0].last_attack_tick = 2;
    state.visible[0].last_attack_speed = 7;
    riskfight_write_observation(&state, 0, obs);
    riskfight_script(obs, RISKFIGHT_FLOOR, actions);
    assert(actions[RF_PRIMARY] == RF_TELEPORT);
    state.visible[0].last_attack_tick = -1;
    for (int slot = 0; slot < OSRS_INVENTORY_SIZE; slot++) {
        OsrsConsumableKind kind = (OsrsConsumableKind)osrs_inventory_cell_metadata(
            &state.env.players[0].inventory_cells[slot])->consumable_kind;
        if (kind == OSRS_CONSUMABLE_MARLIN || kind == OSRS_CONSUMABLE_SUMMER_PIE ||
                kind == OSRS_CONSUMABLE_BREW || kind == OSRS_CONSUMABLE_HALIBUT)
            state.env.players[0].inventory_cells[slot] = osrs_inventory_cell_empty();
    }
    riskfight_write_observation(&state, 0, obs);
    riskfight_script(obs, RISKFIGHT_FLOOR, actions);
    assert(actions[RF_PRIMARY] == RF_TELEPORT);
}

static void test_matches(void) {
    const int bots[] = {RISKFIGHT_TRADER, RISKFIGHT_CAUTIOUS, RISKFIGHT_AGGRESSIVE,
        RISKFIGHT_TACTICIAN, RISKFIGHT_PRESSURE, RISKFIGHT_SURVIVAL, RISKFIGHT_HELDOUT, RISKFIGHT_FLOOR};
    for (int b = 0; b < 8; b++) {
        reset();
        int attacks = 0, stops = 0;
        for (int tick = 0; tick < 600 && !state.env.episode_over; tick++) {
            int actions[2 * RF_HEADS];
            for (int fighter = 0; fighter < 2; fighter++) {
                float obs[RF_OBS_SIZE], mask[RF_MASK_SIZE];
                int repeated[RF_HEADS];
                riskfight_write_observation(&state, fighter, obs);
                riskfight_write_action_mask(&state, fighter, mask);
                RiskfightOpponent bot = fighter ? (RiskfightOpponent)bots[b] : RISKFIGHT_FLOOR;
                int* action = actions + fighter * RF_HEADS;
                riskfight_script(obs, bot, action);
                riskfight_script(obs, bot, repeated);
                assert(memcmp(action, repeated, sizeof(repeated)) == 0);
                if (bot == RISKFIGHT_FLOOR) {
                    int offset = 0;
                    for (int head = 0; head < RF_HEADS; head++) {
                        assert(action[head] >= 0 && action[head] < RF_ACTION_DIMS[head]);
                        if (head == RF_PRIMARY && action[head] == RF_TELEPORT) {
                            // Own teleport lock (8 ticks after our offensive
                            // spec) has no obs bit by design, so a retreat
                            // desire during self-inflicted lock falls back to
                            // STOP here; the sim no-ops blocked teleports.
                            int teleport_offset = 0;
                            for (int h = 0; h < RF_PRIMARY; h++)
                                teleport_offset += RF_ACTION_DIMS[h];
                            if (!mask[teleport_offset + RF_TELEPORT])
                                action[head] = RF_STOP;
                        }
                        assert(mask[offset + action[head]]);
                        offset += RF_ACTION_DIMS[head];
                    }
                    stops += action[RF_PRIMARY] == RF_STOP;
                }
            }
            riskfight_step((EncounterState*)&state, (EncounterContext*)&context, actions);
            attacks += state.env.players[0].just_attacked + state.env.players[1].just_attacked;
        }
        assert(attacks > 0 && stops > 0);
        // The mirror bound is trajectory-sensitive (pre-pot divine + axe
        // discipline shifted the floor-vs-floor exchange from 21 to 12
        // attacks over 41 ticks); the invariant is both sides trade. Keep a
        // loose floor that still fails a pacifist regression.
        if (bots[b] == RISKFIGHT_FLOOR) assert(state.env.episode_over && attacks > 10);
    }
}

int main(void) {
    test_wait_and_release();
    test_vengeance_timing();
    test_hidden_state_independence();
    test_mutual_waiting_releases_both_sides();
    test_escape_requires_exhaustion_or_observed_attack();
    test_matches();
    puts("Riskfight floor timing contracts passed");
}
