#include "../../osrs_riskfight/osrs_riskfight.h"

static void remove_kind(Player* p, OsrsConsumableKind kind) {
    for (int i = 0; i < OSRS_INVENTORY_SIZE; i++)
        if (osrs_inventory_cell_metadata(&p->inventory_cells[i])->consumable_kind == kind)
            p->inventory_cells[i] = osrs_inventory_cell_empty();
}

int main(void) {
    RiskfightState* s = (RiskfightState*)calloc(1, sizeof(*s));
    RiskfightContext ctx;
    riskfight_init_context((EncounterContext*)&ctx);
    ctx.self_play = 1;
    riskfight_reset((EncounterState*)s, (EncounterContext*)&ctx, 123);
    Player* p = &s->env.players[0];
    OsrsEscapeSupplies profile = osrs_escape_supplies(p);
    assert(profile.healing == OSRS_ESCAPE_TRIPLE_EATS);
    assert(profile.marlins == 8 && profile.brew_doses == 8 && profile.halibut == 4);
    assert(profile.pie_bites == 6 && profile.boost_doses == 4);
    assert(profile.minimum_spec_cost == 50 && !osrs_escape_no_special(&profile));
    remove_kind(p, OSRS_CONSUMABLE_BREW);
    assert(osrs_escape_supplies(p).healing == OSRS_ESCAPE_DOUBLE_EATS);
    remove_kind(p, OSRS_CONSUMABLE_HALIBUT);
    assert(osrs_escape_supplies(p).healing == OSRS_ESCAPE_OTHER_HEALING);
    p->inventory_cells[3] = osrs_inventory_cell_from_content_code(
        osrs_inventory_content_code_from_consumable(OSRS_CONSUMABLE_BREW, 1));
    assert(osrs_escape_supplies(p).healing == OSRS_ESCAPE_DOUBLE_EATS);
    remove_kind(p, OSRS_CONSUMABLE_MARLIN);
    assert(osrs_escape_supplies(p).healing == OSRS_ESCAPE_OTHER_HEALING);
    remove_kind(p, OSRS_CONSUMABLE_BREW);
    p->inventory_cells[0] = osrs_inventory_cell_from_content_code(
        osrs_inventory_content_code_from_consumable(OSRS_CONSUMABLE_SUMMER_PIE, 1));
    assert(osrs_escape_supplies(p).pie_bites == 5);
    remove_kind(p, OSRS_CONSUMABLE_SUMMER_PIE);
    assert(osrs_escape_supplies(p).healing == OSRS_ESCAPE_NO_HEALING);
    remove_kind(p, OSRS_CONSUMABLE_SUPER_COMBAT);
    profile = osrs_escape_supplies(p);
    assert(!osrs_escape_no_boost(&profile));
    p->current_attack = p->base_attack;
    p->current_strength = p->base_strength;
    profile = osrs_escape_supplies(p);
    assert(osrs_escape_no_boost(&profile));
    p->special_energy = 49;
    profile = osrs_escape_supplies(p);
    assert(osrs_escape_no_special(&profile));
    p->special_energy = 50;
    profile = osrs_escape_supplies(p);
    assert(!osrs_escape_no_special(&profile));

    riskfight_reset((EncounterState*)s, (EncounterContext*)&ctx, 123);
    HumanInput input;
    human_input_init(&input);
    int actions[RF_HEADS] = {0};
    actions[RF_PRIMARY] = RF_TELEPORT;
    riskfight_policy_commands(s, 0, actions, &input);
    for (int i = 0; i < input.commands.count; i++)
        riskfight_execute_command(s, &ctx, 0, &input.commands.items[i]);
    assert(s->escaped[0] && !s->escaped[1]);
    assert(s->escape_supplies[0][0].healing == OSRS_ESCAPE_TRIPLE_EATS);
    remove_kind(&s->env.players[0], OSRS_CONSUMABLE_MARLIN);
    assert(s->escape_supplies[0][0].marlins == 8);
    free(input.commands.items);
    riskfight_destroy_context((EncounterContext*)&ctx);
    free(s);
    puts("Escape supply tiers, dose counts, boost/spec availability and click snapshot passed");
}
