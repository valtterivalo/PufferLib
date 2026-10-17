#pragma once

typedef enum { OSRS_ESCAPE_NO_HEALING, OSRS_ESCAPE_OTHER_HEALING,
    OSRS_ESCAPE_DOUBLE_EATS, OSRS_ESCAPE_TRIPLE_EATS } OsrsEscapeHealing;

typedef struct {
    OsrsEscapeHealing healing;
    int marlins, brew_doses, halibut, pie_bites, boost_doses;
    int food, potion_doses; /* every eat item in the bag, and every dose of every drink */
    int hard_food;          /* eat items of the main food clock: anglerfish, shark, marlin, a summer pie with a bite left (not the karambwan clock: karambwan, halibut) */
    int restore_doses;      /* doses of every drink that restores prayer: prayer potion, super restore, sanfew serum */
    int hp, attack, strength, ranged, attack_base, strength_base, ranged_base;
    int food_timer, potion_timer, combo_timer;
    int special_energy, minimum_spec_cost;
    int weapon;
} OsrsEscapeSupplies;

static OsrsEscapeSupplies osrs_escape_supplies(const Player* player) {
    OsrsEscapeSupplies out = {.healing = OSRS_ESCAPE_NO_HEALING};
    int healing = 0;
    for (int slot = 0; slot < OSRS_INVENTORY_SIZE; slot++) {
        const OsrsItemContentMetadata* item = osrs_inventory_cell_metadata(&player->inventory_cells[slot]);
        switch (item->consumable_kind) {
            case OSRS_CONSUMABLE_MARLIN: out.marlins++; break;
            case OSRS_CONSUMABLE_BREW: out.brew_doses += item->dose_count; break;
            case OSRS_CONSUMABLE_HALIBUT: out.halibut++; break;
            case OSRS_CONSUMABLE_SUMMER_PIE: out.pie_bites += item->dose_count; break;
            case OSRS_CONSUMABLE_SUPER_COMBAT:
            case OSRS_CONSUMABLE_DIVINE_COMBAT:
            case OSRS_CONSUMABLE_RANGING:
            case OSRS_CONSUMABLE_DIVINE_RANGING:
            case OSRS_CONSUMABLE_BASTION: out.boost_doses += item->dose_count; break;
            default: break;
        }
        switch (item->consumable_kind) {
            case OSRS_CONSUMABLE_ANGLERFISH:
            case OSRS_CONSUMABLE_SHARK_FOOD:
            case OSRS_CONSUMABLE_MARLIN:
            case OSRS_CONSUMABLE_SUMMER_PIE: out.hard_food++; break;
            case OSRS_CONSUMABLE_PRAYER_RESTORE:
            case OSRS_CONSUMABLE_SUPER_RESTORE:
            case OSRS_CONSUMABLE_SANFEW: out.restore_doses += item->dose_count; break;
            default: break;
        }
        healing += item->click_action == OSRS_CLICK_EAT ||
            item->consumable_kind == OSRS_CONSUMABLE_BREW ||
            item->consumable_kind == OSRS_CONSUMABLE_GUTHIX_REST;
        out.food += item->click_action == OSRS_CLICK_EAT;
        if (item->click_action == OSRS_CLICK_DRINK) out.potion_doses += item->dose_count;
        if (item->gear_slot == GEAR_SLOT_WEAPON) {
            int cost = osrs_spec_cost(item->item_idx);
            if (cost > 0 && (!out.minimum_spec_cost || cost < out.minimum_spec_cost))
                out.minimum_spec_cost = cost;
        }
    }
    int cost = osrs_spec_cost(player->equipped[GEAR_SLOT_WEAPON]);
    if (cost > 0 && (!out.minimum_spec_cost || cost < out.minimum_spec_cost)) out.minimum_spec_cost = cost;
    out.healing = out.marlins && out.brew_doses && out.halibut ? OSRS_ESCAPE_TRIPLE_EATS :
        out.marlins && (out.brew_doses || out.halibut) ? OSRS_ESCAPE_DOUBLE_EATS :
        healing ? OSRS_ESCAPE_OTHER_HEALING : OSRS_ESCAPE_NO_HEALING;
    out.hp = player->current_hitpoints;
    out.attack = player->current_attack; out.strength = player->current_strength; out.ranged = player->current_ranged;
    out.attack_base = player->base_attack; out.strength_base = player->base_strength; out.ranged_base = player->base_ranged;
    out.food_timer = player->food_timer; out.potion_timer = player->potion_timer;
    out.combo_timer = player->karambwan_timer;
    out.special_energy = player->special_energy;
    out.weapon = player->equipped[GEAR_SLOT_WEAPON];
    return out;
}

/* A supply class is out when the bag holds none of it. Each is read off the consumable kinds of the bag, so the sim's players and the live G line (which becomes the same
 * cells) answer alike. A class a bag never held reads as out too: every fight starts from a regear with every class stocked, so that only happens in a fixture. */

/** Hard food: the eat items of the main food clock (anglerfish, shark, marlin, summer pie). Karambwan and halibut run on the combo clock, which tops up a main
 * heal and cannot carry a fight alone, so they are not the food a player counts when he says he is out of food. */
static int osrs_escape_no_hard_food(const OsrsEscapeSupplies* supplies) {
    return supplies->hard_food == 0;
}

/** Prayer restores: a dose of a prayer potion, super restore or sanfew serum. With Piety and Smite up the prayer bar is what the restores keep alive. */
static int osrs_escape_no_restore(const OsrsEscapeSupplies* supplies) {
    return supplies->restore_doses == 0;
}

/** Combat boost: no dose of a combat potion (super or divine combat; ranging, divine ranging, bastion for a ranger) left AND no boost still on. A player who has
 * brewed down is at or under his base levels, and without a potion in the bag he cannot get them back: his kit has run out of boosts. While a boost is still
 * active it is not out, whatever the bag holds. */
static int osrs_escape_no_boost(const OsrsEscapeSupplies* supplies) {
    return supplies->boost_doses == 0 && supplies->attack <= supplies->attack_base &&
        supplies->strength <= supplies->strength_base && supplies->ranged <= supplies->ranged_base;
}

static int osrs_escape_no_special(const OsrsEscapeSupplies* supplies) {
    return !supplies->minimum_spec_cost || supplies->special_energy < supplies->minimum_spec_cost;
}
