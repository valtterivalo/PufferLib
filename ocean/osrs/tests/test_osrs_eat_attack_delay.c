/**
 * Engine contract (osrs-engine-quirks section 12): food adds its attack delay
 * ONLY to an existing cooldown. From a ready weapon, eating adds nothing.
 * Karambwan adds 2, standard food 3, and they stack onto a live cooldown.
 */
#include <stdio.h>
#include <string.h>
#include "ocean/osrs/encounters/encounter_colosseum.h"
#include "ocean/osrs/osrs_player_inventory_use.h"
#include "ocean/osrs/tests/osrs_test_check.h"

static Player ready_player(void) {
    Player p;
    memset(&p, 0, sizeof(p));
    p.base_hitpoints = 99;
    p.current_hitpoints = 50;
    p.attack_timer = 0;
    p.attack_timer_uncapped = 0;
    p.has_attack_timer = 0;
    return p;
}

static void test_engine_contract(void) {
    Player p = ready_player();
    osrs_player_eat_food_effects(&p, FOOD_SHARK);
    CHECK("shark from ready adds no attack delay", p.attack_timer == 0);

    p = ready_player();
    OsrsPlayerEatResult eaten = osrs_player_eat_food_effects(&p, FOOD_KARAMBWAN);
    CHECK("karambwan from ready adds no attack delay", p.attack_timer == 0);
    CHECK("karambwan below max heals normally", eaten.consumed && eaten.hp_healed == 18 && p.current_hitpoints == 68);

    p = ready_player();
    p.attack_timer = 4;
    osrs_player_eat_food_effects(&p, FOOD_SHARK);
    CHECK("shark adds 3 to an existing cooldown", p.attack_timer == 7);

    p = ready_player();
    p.attack_timer = 4;
    osrs_player_eat_food_effects(&p, FOOD_KARAMBWAN);
    CHECK("karambwan adds 2 to an existing cooldown", p.attack_timer == 6);

    /* the bug: has_attack_timer unset must not erase a live cooldown */
    p = ready_player();
    p.attack_timer = 5;
    p.has_attack_timer = 0;
    osrs_player_eat_food_effects(&p, FOOD_SHARK);
    CHECK("a live cooldown survives the first eat when has_attack_timer is unset", p.attack_timer == 8);

    /* food then karambwan on a live cooldown stacks 3 + 2 */
    p = ready_player();
    p.attack_timer = 4;
    osrs_player_eat_food_effects(&p, FOOD_SHARK);
    osrs_player_eat_food_effects(&p, FOOD_KARAMBWAN);
    CHECK("shark then karambwan stack onto a live cooldown", p.attack_timer == 9);

    p = ready_player();
    p.current_hitpoints = 50;
    osrs_player_eat_food_effects(&p, FOOD_SHARK);
    CHECK("eating still heals from ready", p.current_hitpoints > 50);

    const int full_hp[] = {99, 115};
    for (size_t i = 0; i < sizeof(full_hp) / sizeof(full_hp[0]); i++) {
        p = ready_player();
        p.current_hitpoints = full_hp[i];
        p.attack_timer = p.attack_timer_uncapped = 4;
        p.inventory_cells[0] = osrs_inventory_cell_from_content_code(
            osrs_inventory_content_code_from_consumable(OSRS_CONSUMABLE_KARAMBWAN, 0));
        OsrsInventoryUseState inventory_use = {0};
        CHECK("zero-heal karambwan remains a legal inventory action", osrs_can_eat_consumable_kind(&p, OSRS_CONSUMABLE_KARAMBWAN));
        OsrsInventoryUseResult used = osrs_player_use_inventory(&p, &inventory_use, NULL, 0, 1);
        CHECK("zero-heal karambwan is consumed", used == OSRS_INVENTORY_USE_CONSUMED && osrs_inventory_cell_is_empty(&p.inventory_cells[0]));
        CHECK("zero-heal karambwan preserves existing HP", p.current_hitpoints == full_hp[i]);
        CHECK("zero-heal karambwan records waste", p.last_karambwan_heal == 0 && p.last_karambwan_waste == 18);
        CHECK("zero-heal karambwan applies cooldowns", p.attack_timer == 6 && p.attack_timer_uncapped == 6 && p.food_timer == 3 && p.potion_timer == 3 && p.karambwan_timer == 2);
    }
}

static void test_colosseum_charges_the_cooldown(void) {
    ColosseumContext ctx;
    static ColosseumState s;
    col_init_context_typed(&ctx);
    col_finalize_route_topology(&ctx);
    memset(&s, 0, sizeof(s));
    col_reset_ctx((EncounterState*)&s, (EncounterContext*)&ctx, 4242u);

    s.player.current_hitpoints = 20;
    s.player.attack_timer = 4;
    s.player.food_timer = 0;
    s.player.karambwan_timer = 0;
    int before = s.player.current_hitpoints;

    int consumed = col_apply_food_cell(&s, OSRS_CONSUMABLE_SHARK_FOOD);
    CHECK("colosseum shark is consumed when the food timer is clear", consumed == 1);
    CHECK("colosseum shark heals", s.player.current_hitpoints > before);
    CHECK("colosseum eating charges the attack cooldown instead of being free", s.player.attack_timer == 7);

    /* refused eat must report not-consumed so the caller keeps the food */
    s.player.food_timer = 3;
    int refused = col_apply_food_cell(&s, OSRS_CONSUMABLE_SHARK_FOOD);
    CHECK("colosseum eat under an active food timer is refused", refused == 0);
}

int main(void) {
    test_engine_contract();
    test_colosseum_charges_the_cooldown();
    return osrs_test_summary();
}
