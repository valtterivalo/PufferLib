#define _POSIX_C_SOURCE 200809L
#include "riskfight_rollout.h"

static RiskfightState base, world;
static RiskfightContext context;

int main(void) {
    riskfight_reset((EncounterState*)&base, (EncounterContext*)&context, 12345);
    context.self_play = 1;
    for (int side = 0; side < 2; side++) {
        osrs_interaction_clear(&base.env.players[side].interaction);
        base.poison_source[side] = OSRS_POISON_CLEAR_PRIOR;
    }
    Player* him = &base.env.players[1];
    him->current_hitpoints = 20;
    him->inventory_cells[9] = osrs_inventory_cell_from_content_code(riskfight_supply_code(OSRS_CONSUMABLE_ANGLERFISH, 0));
    him->inventory_cells[10] = him->inventory_cells[9];
    assert(osrs_inventory_cell_metadata(&him->inventory_cells[9])->consumable_kind == OSRS_CONSUMABLE_ANGLERFISH);
    HumanCommand eat = {.kind = HUMAN_COMMAND_INVENTORY_PRIMARY_CLICK, .inventory_slot = 9};
    HumanCommandQueue empty = {0}, meal = {.items = &eat, .count = 1};
    riskfight_step_queues(&base, &context, &empty, &meal);
    assert(him->ate_food_this_tick && him->food_timer == 2);
    Hist initial = hist_start(), history;
    LaBelief belief = {.hp_low = 42, .hp_high = 42, .energy_low = 100, .energy_high = 100,
        .vengeance = GS_VENGEANCE_DOWN, .pid = GS_PID_LOCAL, .food_ready = 103, .seen_since_eat = 0,
        .combat_level = observer_combat_level(him),
        .gear = {GS_GEAR_COMPOSITION_OBSERVED, raw_of_item(him->equipped[GEAR_SLOT_WEAPON]), raw_of_item(him->equipped[GEAR_SLOT_SHIELD])}};
    for (int slot = 0; slot < GS_ARMOR_SLOTS; slot++) belief.gear.armor[slot] = raw_of_item(him->equipped[WORN_TO_GEAR[GS_ARMOR_KIT_SLOT[slot]]]);
    int accepted[2] = {0};
    for (int arm = 0; arm < 2; arm++) {
        if (arm == 0) world = base;
        else {
            uint32_t rng = 7;
            la_world(&base, &initial, &belief, 100, &rng, &world, &history);
        }
        int slot = slot_of_kind(&world.env.players[1], OSRS_CONSUMABLE_ANGLERFISH);
        assert(slot >= 0);
        eat.inventory_slot = slot;
        for (int next = 101; next <= 104; next++) {
            riskfight_step_queues(&world, &context, &empty, &meal);
            if (world.env.players[1].ate_food_this_tick && !accepted[arm]) accepted[arm] = next;
        }
    }
    assert(accepted[0] == 103 && accepted[1] == 103);
    belief.food_ready = INT_MIN;
    uint32_t rng = 7;
    la_world(&base, &initial, &belief, 100, &rng, &world, &history);
    assert(world.env.players[1].food_timer == 0);
    eat.inventory_slot = slot_of_kind(&world.env.players[1], OSRS_CONSUMABLE_ANGLERFISH);
    riskfight_step_queues(&world, &context, &empty, &meal);
    assert(world.env.players[1].ate_food_this_tick);
    printf("Hard-food deadline preserved: continuous engine %d, seeded world %d\n", accepted[0], accepted[1]);
}
