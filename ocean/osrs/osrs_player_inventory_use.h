#ifndef OSRS_PLAYER_INVENTORY_USE_H
#define OSRS_PLAYER_INVENTORY_USE_H

#include "osrs_encounter.h"
#include "osrs_inventory_actions.h"
#include "osrs_player_consumables.h"

enum {
    OSRS_LOCATOR_ORB_DAMAGE = 10,
    OSRS_DIVINE_DAMAGE = 10,
    OSRS_DIVINE_DURATION = 500,
    OSRS_VENGEANCE_COOLDOWN = 50,
    OSRS_VENGEANCE_MAGIC_LEVEL = 94,
};

typedef struct {
    int orb_used_tick;
    int vengeance_sacks;
    int vengeance_consumed_tick;
    int divine_combat_ticks;
    int stat_drift_timer;
} OsrsInventoryUseState;

typedef enum {
    OSRS_INVENTORY_USE_NONE,
    OSRS_INVENTORY_USE_CONSUMED,
    OSRS_INVENTORY_USE_ESCAPED,
} OsrsInventoryUseResult;

static inline int osrs_player_self_damage(Player* p, int damage, int hp_floor) {
    int dealt = p->current_hitpoints - hp_floor;
    if (dealt < 0) dealt = 0;
    if (dealt > damage) dealt = damage;
    p->current_hitpoints -= dealt;
    if (dealt > 0) {
        p->hit_landed_this_tick = 1;
        p->hit_damage += dealt;
        p->hit_style = ATTACK_STYLE_NONE;
        p->hit_attacker_idx = -1;
    }
    return dealt;
}

static inline void osrs_player_inventory_drink_effect(void* player, OsrsConsumableKind kind) {
    Player* p = (Player*)player;
    switch (kind) {
        case OSRS_CONSUMABLE_BREW:
            encounter_apply_brew_heal(p, osrs_brew_heal_amount(p->base_hitpoints));
            encounter_brew_drain_stats(p);
            p->ate_brew_this_tick = 1;
            break;
        case OSRS_CONSUMABLE_SANFEW:
        case OSRS_CONSUMABLE_SUPER_RESTORE:
            if (kind == OSRS_CONSUMABLE_SANFEW) {
                encounter_sanfew_restore_stats(p);
                encounter_add_prayer_restore(p, osrs_sanfew_restore_amount(p->base_prayer));
            } else {
                encounter_restore_stats(p);
                encounter_add_prayer_restore(p, osrs_super_restore_amount(p->base_prayer));
            }
            encounter_cap_prayer_restore(p);
            break;
        case OSRS_CONSUMABLE_DIVINE_COMBAT:
            osrs_player_self_damage(p, OSRS_DIVINE_DAMAGE, 1);
            encounter_super_combat_boost(p);
            break;
        case OSRS_CONSUMABLE_SUPER_COMBAT:
            encounter_super_combat_boost(p);
            break;
        default: abort();
    }
}

static inline OsrsInventoryUseResult osrs_player_use_inventory(
    Player* p, OsrsInventoryUseState* state, const OsrsTeleportState* teleport, int slot, int tick
) {
    assert(slot >= 0 && slot < OSRS_INVENTORY_SIZE);
    OsrsInventoryCell* cell = &p->inventory_cells[slot];
    const OsrsItemContentMetadata* meta = osrs_inventory_cell_metadata(cell);
    OsrsConsumableKind kind = (OsrsConsumableKind)meta->consumable_kind;
    switch (meta->click_action) {
        case OSRS_CLICK_EQUIP:
            if (osrs_equip_from_cell(p, p->inventory_cells, slot) < 0)
                return OSRS_INVENTORY_USE_NONE;
            osrs_interaction_check_interrupt(&p->interaction, OSRS_IACT_EQUIP);
            return OSRS_INVENTORY_USE_CONSUMED;
        case OSRS_CLICK_EAT: {
            FoodType food;
            switch (kind) {
                case OSRS_CONSUMABLE_MARLIN: food = FOOD_MARLIN; break;
                case OSRS_CONSUMABLE_HALIBUT: food = FOOD_HALIBUT; break;
                case OSRS_CONSUMABLE_SUMMER_PIE: food = FOOD_SUMMER_PIE; break;
                case OSRS_CONSUMABLE_SHARK_FOOD: food = FOOD_SHARK; break;
                case OSRS_CONSUMABLE_KARAMBWAN: food = FOOD_KARAMBWAN; break;
                default: abort();
            }
            if (!osrs_player_eat_food_effects(p, food).consumed)
                return OSRS_INVENTORY_USE_NONE;
            osrs_inventory_cell_consume_eat(cell);
            osrs_interaction_check_interrupt(&p->interaction, OSRS_IACT_EAT);
            return OSRS_INVENTORY_USE_CONSUMED;
        }
        case OSRS_CLICK_DRINK: {
            if (kind == OSRS_CONSUMABLE_DIVINE_COMBAT &&
                p->current_hitpoints <= OSRS_DIVINE_DAMAGE) return OSRS_INVENTORY_USE_NONE;
            OsrsInventoryClickResolution click =
                osrs_inventory_cell_click_interpret(cell, OSRS_CLICK_TICK_FIRST);
            if (!osrs_inventory_cell_consume_drink_one_dose(cell, click,
                    &p->potion_timer, osrs_player_inventory_drink_effect, p).consumed)
                return OSRS_INVENTORY_USE_NONE;
            p->food_timer = 3;
            if (kind == OSRS_CONSUMABLE_DIVINE_COMBAT)
                state->divine_combat_ticks = OSRS_DIVINE_DURATION;
            osrs_interaction_check_interrupt(&p->interaction, OSRS_IACT_DRINK);
            return OSRS_INVENTORY_USE_CONSUMED;
        }
        case OSRS_CLICK_SELF_DAMAGE:
            if (state->orb_used_tick == tick) return OSRS_INVENTORY_USE_NONE;
            state->orb_used_tick = tick;
            osrs_player_self_damage(p, OSRS_LOCATOR_ORB_DAMAGE, 1);
            osrs_interaction_clear(&p->interaction);
            return OSRS_INVENTORY_USE_CONSUMED;
        case OSRS_CLICK_TELEPORT:
            if (!osrs_teleport_allowed(*teleport, tick)) return OSRS_INVENTORY_USE_NONE;
            *cell = osrs_inventory_cell_empty();
            osrs_interaction_clear(&p->interaction);
            return OSRS_INVENTORY_USE_ESCAPED;
        case OSRS_CLICK_NONE:
            return OSRS_INVENTORY_USE_NONE;
    }
    abort();
}

static inline void osrs_player_inventory_tick(Player* p, OsrsInventoryUseState* state) {
    EncounterStatDriftPins pins = encounter_stat_drift_no_pins();
    if (state->divine_combat_ticks > 0) {
        pins.attack_floor = p->current_attack;
        pins.strength_floor = p->current_strength;
        pins.defence_floor = p->current_defence;
    }
    encounter_tick_stat_drift(p, &state->stat_drift_timer, pins);
    if (state->stat_drift_timer == 0 && p->current_hitpoints > 0) {
        if (p->current_hitpoints < p->base_hitpoints) p->current_hitpoints++;
        else if (p->current_hitpoints > p->base_hitpoints) p->current_hitpoints--;
    }
    if (state->divine_combat_ticks > 0 && --state->divine_combat_ticks == 0) {
        if (p->current_attack > p->base_attack) p->current_attack = p->base_attack;
        if (p->current_strength > p->base_strength) p->current_strength = p->base_strength;
        if (p->current_defence > p->base_defence) p->current_defence = p->base_defence;
    }
}

static inline int osrs_player_cast_inventory_vengeance(Player* p, OsrsInventoryUseState* state, int tick) {
    if (state->vengeance_consumed_tick == tick || p->veng_active || p->veng_cooldown > 0 ||
        p->current_magic < OSRS_VENGEANCE_MAGIC_LEVEL || state->vengeance_sacks == 0) return 0;
    for (int i = 0; i < OSRS_INVENTORY_SIZE; i++) {
        if (osrs_inventory_cell_metadata(&p->inventory_cells[i])->consumable_kind !=
            OSRS_CONSUMABLE_VENGEANCE_SACK) continue;
        if (--state->vengeance_sacks == 0)
            p->inventory_cells[i] = osrs_inventory_cell_empty();
        p->veng_active = 1;
        p->veng_cooldown = OSRS_VENGEANCE_COOLDOWN;
        p->cast_veng_this_tick = 1;
        return 1;
    }
    return 0;
}

#endif
