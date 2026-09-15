#include <assert.h>
#include <stdio.h>
#include "../encounters/encounter_riskfight.h"

static RiskfightState state;
static RiskfightContext context;

static void remove_kind(Player* p, OsrsConsumableKind kind) {
    for (int slot = 0; slot < OSRS_INVENTORY_SIZE; slot++)
        if (osrs_inventory_cell_metadata(&p->inventory_cells[slot])->consumable_kind == kind)
            p->inventory_cells[slot] = osrs_inventory_cell_empty();
}

int main(void) {
    context.self_play = 1;
    riskfight_reset((EncounterState*)&state, (EncounterContext*)&context, 12345);
    Player p = state.env.players[0];
    RiskfightThreatWindow threat = {70, 3, 0};
    p.current_hitpoints = 85;
    remove_kind(&p, OSRS_CONSUMABLE_BREW);
    remove_kind(&p, OSRS_CONSUMABLE_HALIBUT);
    assert(riskfight_exit_decision(&p, threat, OSRS_ESCAPE_DOUBLE_EATS, 1, 0, 1, 50) == RISKFIGHT_LAST_ATTACK);
    p.attack_timer = 2;
    assert(riskfight_exit_decision(&p, threat, OSRS_ESCAPE_DOUBLE_EATS, 1, 0, 0, 50) == RISKFIGHT_CONTINUE);
    p.attack_timer = 7;
    assert(riskfight_exit_decision(&p, threat, OSRS_ESCAPE_DOUBLE_EATS, 1, 0, 0, 50) == RISKFIGHT_RETREAT);
    p.attack_timer = 0;
    threat.ticks_until = 0;
    assert(riskfight_exit_decision(&p, threat, OSRS_ESCAPE_DOUBLE_EATS, 1, 0, 1, 50) == RISKFIGHT_RETREAT);
    p = state.env.players[0];
    threat.damage = 144;
    assert(riskfight_exit_decision(&p, threat, OSRS_ESCAPE_DOUBLE_EATS, 0, 0, 0, 50) == RISKFIGHT_CONTINUE);
    assert(riskfight_exit_decision(&p, threat, OSRS_ESCAPE_DOUBLE_EATS, 1, 0, 0, 50) == RISKFIGHT_RETREAT);
    threat = (RiskfightThreatWindow){70, 2, 0};
    p.attack_timer = 7;
    remove_kind(&p, OSRS_CONSUMABLE_HALIBUT);
    assert(riskfight_exit_decision(&p, threat, OSRS_ESCAPE_DOUBLE_EATS, 1, 0, 0, 50) == RISKFIGHT_CONTINUE);
    assert(riskfight_exit_decision(&p, threat, OSRS_ESCAPE_TRIPLE_EATS, 1, 0, 0, 50) == RISKFIGHT_RETREAT);
    p = state.env.players[0];
    p.attack_timer = 7;
    remove_kind(&p, OSRS_CONSUMABLE_DIVINE_COMBAT);
    assert(riskfight_exit_decision(&p, threat, OSRS_ESCAPE_DOUBLE_EATS, 1, 0, 0, 50) == RISKFIGHT_CONTINUE);
    p.current_attack = p.base_attack;
    p.current_strength = p.base_strength;
    assert(riskfight_exit_decision(&p, threat, OSRS_ESCAPE_DOUBLE_EATS, 1, 0, 0, 50) == RISKFIGHT_RETREAT);
    state.visible[0].equipment[GEAR_SLOT_WEAPON] = ITEM_GRANITE_MAUL_ORNATE;
    state.visible[0].equipment[GEAR_SLOT_SHIELD] = ITEM_NONE;
    float obs[RF_OBS_SIZE];
    int actions[RF_HEADS];
    riskfight_write_observation(&state, 0, obs);
    riskfight_script(obs, RISKFIGHT_TACTICIAN, actions);
    assert(actions[RF_PRIMARY] != RF_TELEPORT);
    riskfight_reset((EncounterState*)&state, (EncounterContext*)&context, 12345);
    state.env.tick = 10;
    state.visible[0].last_attack_tick = 8;
    state.visible[0].last_attack_speed = 7;
    state.env.players[0].current_hitpoints = 85;
    remove_kind(&state.env.players[0], OSRS_CONSUMABLE_BREW);
    remove_kind(&state.env.players[0], OSRS_CONSUMABLE_HALIBUT);
    riskfight_write_observation(&state, 0, obs);
    riskfight_script(obs, RISKFIGHT_TACTICIAN, actions);
    assert(actions[RF_PRIMARY] == RF_ATTACK && actions[RF_SPECIAL] == 0);
    int paired[2 * RF_HEADS] = {0};
    memcpy(paired, actions, sizeof(actions));
    riskfight_step((EncounterState*)&state, (EncounterContext*)&context, paired);
    assert(state.env.players[0].just_attacked && state.env.players[0].special_energy == 100);
    puts("Riskfight supply-aware exits passed");
}
