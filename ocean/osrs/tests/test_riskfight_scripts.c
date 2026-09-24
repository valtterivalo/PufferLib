#include <assert.h>
#include <stdio.h>
#include "../encounters/encounter_riskfight.h"

static RiskfightState state;
static RiskfightContext context;

static void reset(void) {
    riskfight_reset((EncounterState*)&state, (EncounterContext*)&context, 12345);
    context.self_play = 1;
}

static void decide(RiskfightOpponent opponent, int* actions) {
    float obs[RF_OBS_SIZE];
    riskfight_write_observation(&state, 0, obs);
    riskfight_script(obs, opponent, 1, actions);
}

static OsrsConsumableKind drink_kind(const int* actions) {
    if (!actions[RF_DRINK]) return OSRS_CONSUMABLE_NONE;
    return (OsrsConsumableKind)osrs_inventory_cell_metadata(
        &state.env.players[0].inventory_cells[actions[RF_DRINK] - 1])->consumable_kind;
}

static void execute(const int* actions) {
    HumanInput input = {0};
    riskfight_policy_commands(&state, 0, actions, &input);
    for (int i = 0; i < input.commands.count; i++)
        riskfight_execute_command(&state, &context, 0, &input.commands.items[i]);
    free(input.commands.items);
}

static int item_count(int item) {
    Player* p = &state.env.players[0];
    int count = 0;
    for (int i = 0; i < NUM_GEAR_SLOTS; i++) count += p->equipped[i] == item;
    for (int i = 0; i < OSRS_INVENTORY_SIZE; i++)
        count += osrs_inventory_cell_metadata(&p->inventory_cells[i])->item_idx == item;
    return count;
}

static void test_return_to_one_handed_weapon(void) {
    reset();
    Player* p = &state.env.players[0];
    p->current_hitpoints = 65;
    p->attack_timer = 0;
    state.env.tick = 10;
    riskfight_observe_visible(&state, 0, 1);
    state.env.tick = 11;
    state.env.players[1].hit_landed_this_tick = 1;
    state.env.players[1].hit_damage = 95;
    state.env.players[1].current_hitpoints = 25;
    state.env.players[0].special_energy = 0;
    riskfight_observe_visible(&state, 0, 0);
    int actions[RF_HEADS];
    decide(RISKFIGHT_TRADER, actions);
    execute(actions);
    assert(p->equipped[GEAR_SLOT_WEAPON] == ITEM_DHAROKS_GREATAXE);
    assert(p->equipped[GEAR_SLOT_SHIELD] == ITEM_NONE);
    p->current_hitpoints = 100;
    decide(RISKFIGHT_TRADER, actions);
    assert(actions[RF_WEAPON] && actions[RF_SHIELD]);
    execute(actions);
    assert(p->equipped[GEAR_SLOT_WEAPON] == ITEM_ABYSSAL_TENTACLE);
    assert(p->equipped[GEAR_SLOT_SHIELD] == ITEM_AVERNIC_DEFENDER);
    assert(item_count(ITEM_DHAROKS_GREATAXE) == 1);
    assert(item_count(ITEM_ABYSSAL_TENTACLE) == 1);
    assert(item_count(ITEM_AVERNIC_DEFENDER) == 1);
}

static void test_joint_weapon_shield_mask(void) {
    reset();
    Player* p = &state.env.players[0];
    p->current_hitpoints = 65;
    p->attack_timer = 0;
    state.env.tick = 10;
    riskfight_observe_visible(&state, 0, 1);
    state.env.tick = 11;
    state.env.players[1].hit_landed_this_tick = 1;
    state.env.players[1].hit_damage = 95;
    state.env.players[1].current_hitpoints = 25;
    state.env.players[0].special_energy = 0;
    riskfight_observe_visible(&state, 0, 0);
    int actions[RF_HEADS];
    decide(RISKFIGHT_TRADER, actions);
    execute(actions);
    assert(p->equipped[GEAR_SLOT_WEAPON] == ITEM_DHAROKS_GREATAXE);
    assert(osrs_first_empty_inventory_cell(p->inventory_cells, -1) == -1);
    p->current_hitpoints = 100;
    decide(RISKFIGHT_TRADER, actions);
    int shield_action = actions[RF_SHIELD];
    assert(shield_action > 0);
    assert(!osrs_can_equip_from_cell(p, p->inventory_cells, shield_action - 1));
    float mask[RF_MASK_SIZE];
    riskfight_write_action_mask(&state, 0, mask);
    assert(mask[RF_ACTION_DIMS[RF_WEAPON] + shield_action] == 1);
    assert(mask[actions[RF_WEAPON]] == 1);

    OsrsInventoryCell before[OSRS_INVENTORY_SIZE];
    memcpy(before, p->inventory_cells, sizeof(before));
    int shield_only[RF_HEADS] = {0};
    shield_only[RF_SHIELD] = shield_action;
    execute(shield_only);
    assert(p->equipped[GEAR_SLOT_WEAPON] == ITEM_DHAROKS_GREATAXE);
    assert(p->equipped[GEAR_SLOT_SHIELD] == ITEM_NONE);
    assert(memcmp(before, p->inventory_cells, sizeof(before)) == 0);
    execute(actions);
    assert(p->equipped[GEAR_SLOT_WEAPON] == ITEM_ABYSSAL_TENTACLE);
    assert(p->equipped[GEAR_SLOT_SHIELD] == ITEM_AVERNIC_DEFENDER);
    assert(item_count(ITEM_DHAROKS_GREATAXE) == 1);
    assert(item_count(ITEM_ABYSSAL_TENTACLE) == 1);
    assert(item_count(ITEM_AVERNIC_DEFENDER) == 1);
}

static void test_restore_and_boost_priority(void) {
    reset();
    Player* p = &state.env.players[0];
    int actions[RF_HEADS];
    p->current_prayer = 20;
    decide(RISKFIGHT_TRADER, actions);
    assert(drink_kind(actions) == OSRS_CONSUMABLE_SANFEW);
    execute(actions);
    assert(p->current_prayer > 20);
    p->current_prayer = 20;
    decide(RISKFIGHT_TRADER, actions);
    assert(actions[RF_DRINK] == 0);

    reset();
    p->current_attack = 99;
    p->current_strength = 99;
    p->current_defence = 99;
    decide(RISKFIGHT_TRADER, actions);
    assert(drink_kind(actions) == OSRS_CONSUMABLE_SUPER_COMBAT);
    execute(actions);
    assert(p->current_attack == 118 && p->current_strength == 118 && p->current_defence == 118);
    assert(p->current_hitpoints == 121);

    reset();
    p->current_magic = 89;
    p->current_attack = 99;
    decide(RISKFIGHT_TRADER, actions);
    assert(drink_kind(actions) == OSRS_CONSUMABLE_SANFEW);
    execute(actions);
    assert(p->current_magic == 99);

    reset();
    p->current_hitpoints = 20;
    p->current_prayer = 0;
    p->current_attack = 80;
    decide(RISKFIGHT_TRADER, actions);
    assert(drink_kind(actions) == OSRS_CONSUMABLE_BREW);
}

static void test_cautious_checks_supplies_not_cooldowns(void) {
    reset();
    Player* p = &state.env.players[0];
    int actions[RF_HEADS];
    p->current_hitpoints = 20;
    p->food_timer = p->karambwan_timer = p->potion_timer = 3;
    decide(RISKFIGHT_CAUTIOUS, actions);
    assert(actions[RF_PRIMARY] != RF_TELEPORT);
    for (int i = 0; i < OSRS_INVENTORY_SIZE; i++) {
        const OsrsItemContentMetadata* meta = osrs_inventory_cell_metadata(&p->inventory_cells[i]);
        if (meta->click_action == OSRS_CLICK_EAT)
            p->inventory_cells[i] = osrs_inventory_cell_empty();
    }
    decide(RISKFIGHT_CAUTIOUS, actions);
    assert(actions[RF_PRIMARY] != RF_TELEPORT);
    for (int i = 0; i < OSRS_INVENTORY_SIZE; i++)
        if (osrs_inventory_cell_metadata(&p->inventory_cells[i])->consumable_kind == OSRS_CONSUMABLE_BREW)
            p->inventory_cells[i] = osrs_inventory_cell_empty();
    decide(RISKFIGHT_CAUTIOUS, actions);
    assert(actions[RF_PRIMARY] == RF_TELEPORT);
}

int main(void) {
    riskfight_init_context((EncounterContext*)&context);
    riskfight_finalize_context((EncounterState*)&state, (EncounterContext*)&context);
    test_return_to_one_handed_weapon();
    test_joint_weapon_shield_mask();
    test_restore_and_boost_priority();
    test_cautious_checks_supplies_not_cooldowns();
    riskfight_destroy_context((EncounterContext*)&context);
    puts("Riskfight script contracts passed");
}
