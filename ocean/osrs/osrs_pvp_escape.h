#pragma once

typedef enum { OSRS_ESCAPE_NO_HEALING, OSRS_ESCAPE_OTHER_HEALING,
    OSRS_ESCAPE_DOUBLE_EATS, OSRS_ESCAPE_TRIPLE_EATS } OsrsEscapeHealing;

typedef struct {
    OsrsEscapeHealing healing;
    int marlins, brew_doses, halibut, pie_bites, boost_doses;
    int hp, attack, strength, attack_base, strength_base;
    int food_timer, potion_timer, combo_timer;
    int special_energy, minimum_spec_cost;
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
            case OSRS_CONSUMABLE_DIVINE_COMBAT: out.boost_doses += item->dose_count; break;
        }
        healing += item->click_action == OSRS_CLICK_EAT ||
            item->consumable_kind == OSRS_CONSUMABLE_BREW ||
            item->consumable_kind == OSRS_CONSUMABLE_GUTHIX_REST;
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
    out.attack = player->current_attack; out.strength = player->current_strength;
    out.attack_base = player->base_attack; out.strength_base = player->base_strength;
    out.food_timer = player->food_timer; out.potion_timer = player->potion_timer;
    out.combo_timer = player->karambwan_timer;
    out.special_energy = player->special_energy;
    return out;
}

static int osrs_escape_no_boost(const OsrsEscapeSupplies* supplies) {
    return supplies->boost_doses == 0 && supplies->attack <= supplies->attack_base &&
        supplies->strength <= supplies->strength_base;
}

static int osrs_escape_no_special(const OsrsEscapeSupplies* supplies) {
    return !supplies->minimum_spec_cost || supplies->special_energy < supplies->minimum_spec_cost;
}
