/**
 * @file osrs_inventory.h
 * @brief Shared 28-slot gear inventory and worn equipment helpers.
 */

#ifndef OSRS_INVENTORY_H
#define OSRS_INVENTORY_H

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "osrs_types.h"
#include "osrs_items.h"
#include "osrs_item_effects.h"

static inline int osrs_item_gear_slot(uint8_t item_idx) {
    if (item_idx >= NUM_ITEMS) return -1;
    switch (ITEM_DATABASE[item_idx].slot) {
        case SLOT_HEAD: return GEAR_SLOT_HEAD;
        case SLOT_CAPE: return GEAR_SLOT_CAPE;
        case SLOT_NECK: return GEAR_SLOT_NECK;
        case SLOT_WEAPON: return GEAR_SLOT_WEAPON;
        case SLOT_BODY: return GEAR_SLOT_BODY;
        case SLOT_SHIELD: return GEAR_SLOT_SHIELD;
        case SLOT_LEGS: return GEAR_SLOT_LEGS;
        case SLOT_HANDS: return GEAR_SLOT_HANDS;
        case SLOT_FEET: return GEAR_SLOT_FEET;
        case SLOT_RING: return GEAR_SLOT_RING;
        case SLOT_AMMO: return GEAR_SLOT_AMMO;
        default: return -1;
    }
}

static inline void osrs_update_spec_weapons_for_weapon(Player* p, uint8_t weapon_item) {
    p->melee_spec_weapon = MELEE_SPEC_NONE;
    p->ranged_spec_weapon = RANGED_SPEC_NONE;
    p->magic_spec_weapon = MAGIC_SPEC_NONE;

    switch (weapon_item) {
        case ITEM_DRAGON_DAGGER:
            p->melee_spec_weapon = MELEE_SPEC_DRAGON_DAGGER;
            break;
        case ITEM_DRAGON_CLAWS:
            p->melee_spec_weapon = MELEE_SPEC_DRAGON_CLAWS;
            break;
        case ITEM_AGS:
            p->melee_spec_weapon = MELEE_SPEC_AGS;
            break;
        case ITEM_ANCIENT_GS:
            p->melee_spec_weapon = MELEE_SPEC_ANCIENT_GS;
            break;
        case ITEM_GRANITE_MAUL:
            p->melee_spec_weapon = MELEE_SPEC_GRANITE_MAUL;
            break;
        case ITEM_VESTAS:
            p->melee_spec_weapon = MELEE_SPEC_VESTAS;
            break;
        case ITEM_VOIDWAKER:
            p->melee_spec_weapon = MELEE_SPEC_VOIDWAKER;
            break;
        case ITEM_STATIUS_WARHAMMER:
            p->melee_spec_weapon = MELEE_SPEC_DWH;
            break;
        case ITEM_DARK_BOW:
            p->ranged_spec_weapon = RANGED_SPEC_DARK_BOW;
            break;
        case ITEM_HEAVY_BALLISTA:
            p->ranged_spec_weapon = RANGED_SPEC_BALLISTA;
            break;
        case ITEM_ARMADYL_CROSSBOW:
            p->ranged_spec_weapon = RANGED_SPEC_ACB;
            break;
        case ITEM_ZARYTE_CROSSBOW:
            p->ranged_spec_weapon = RANGED_SPEC_ZCB;
            break;
        case ITEM_MORRIGANS_JAVELIN:
            p->ranged_spec_weapon = RANGED_SPEC_MORRIGANS;
            break;
        case ITEM_VOLATILE_STAFF:
            p->magic_spec_weapon = MAGIC_SPEC_VOLATILE_STAFF;
            break;
        default:
            break;
    }
}

static inline int osrs_item_is_spec_weapon(uint8_t weapon_item) {
    switch (weapon_item) {
        case ITEM_DRAGON_DAGGER:
        case ITEM_DRAGON_CLAWS:
        case ITEM_AGS:
        case ITEM_ANCIENT_GS:
        case ITEM_GRANITE_MAUL:
        case ITEM_VESTAS:
        case ITEM_VOIDWAKER:
        case ITEM_STATIUS_WARHAMMER:
        case ITEM_DARK_BOW:
        case ITEM_HEAVY_BALLISTA:
        case ITEM_ARMADYL_CROSSBOW:
        case ITEM_ZARYTE_CROSSBOW:
        case ITEM_MORRIGANS_JAVELIN:
        case ITEM_VOLATILE_STAFF:
            return 1;
        default:
            return 0;
    }
}

static inline void osrs_player_refresh_weapon_state(Player* p, uint8_t weapon_item) {
    osrs_update_spec_weapons_for_weapon(p, weapon_item);
    if (weapon_item >= NUM_ITEMS) return;

    int style = get_item_attack_style(weapon_item);
    if (osrs_item_is_spec_weapon(weapon_item)) {
        p->current_gear = GEAR_SPEC;
    } else if (style == ATTACK_STYLE_MELEE) {
        p->current_gear = GEAR_MELEE;
    } else if (style == ATTACK_STYLE_RANGED) {
        p->current_gear = GEAR_RANGED;
    } else if (style == ATTACK_STYLE_MAGIC) {
        p->current_gear = GEAR_MAGE;
    }

    if (weapon_item == ITEM_VOIDWAKER) {
        p->visible_gear = GEAR_MAGE;
    } else if (style == ATTACK_STYLE_MELEE) {
        p->visible_gear = GEAR_MELEE;
    } else if (style == ATTACK_STYLE_RANGED) {
        p->visible_gear = GEAR_RANGED;
    } else if (style == ATTACK_STYLE_MAGIC) {
        p->visible_gear = GEAR_MAGE;
    }
}

static inline void osrs_player_inventory_clear(Player* p) {
    memset(p->inventory, ITEM_NONE, sizeof(p->inventory));
}

static inline int osrs_player_inventory_count(const Player* p) {
    int count = 0;
    for (int i = 0; i < OSRS_INVENTORY_SIZE; i++) {
        if (p->inventory[i] != ITEM_NONE) count++;
    }
    return count;
}

static inline int osrs_player_inventory_free_slots(const Player* p) {
    return OSRS_INVENTORY_SIZE - osrs_player_inventory_count(p);
}

static inline int osrs_player_inventory_find(const Player* p, uint8_t item_idx) {
    if (item_idx == ITEM_NONE) return -1;
    for (int i = 0; i < OSRS_INVENTORY_SIZE; i++) {
        if (p->inventory[i] == item_idx) return i;
    }
    return -1;
}

static inline int osrs_player_inventory_has_item(const Player* p, uint8_t item_idx) {
    return osrs_player_inventory_find(p, item_idx) >= 0;
}

static inline int osrs_player_has_equipped_item(const Player* p, uint8_t item_idx) {
    for (int i = 0; i < NUM_GEAR_SLOTS; i++) {
        if (p->equipped[i] == item_idx) return 1;
    }
    return 0;
}

static inline int osrs_player_owns_item(const Player* p, uint8_t item_idx) {
    return osrs_player_has_equipped_item(p, item_idx) ||
        osrs_player_inventory_has_item(p, item_idx);
}

static inline int osrs_player_inventory_add(Player* p, uint8_t item_idx) {
    if (item_idx == ITEM_NONE) return -1;
    for (int i = 0; i < OSRS_INVENTORY_SIZE; i++) {
        if (p->inventory[i] == ITEM_NONE) {
            p->inventory[i] = item_idx;
            return i;
        }
    }
    return -1;
}

static inline uint8_t osrs_player_inventory_remove_slot(Player* p, int slot) {
    if (slot < 0 || slot >= OSRS_INVENTORY_SIZE) return ITEM_NONE;
    uint8_t item = p->inventory[slot];
    p->inventory[slot] = ITEM_NONE;
    return item;
}

static inline int osrs_player_inventory_remove_item(Player* p, uint8_t item_idx) {
    int slot = osrs_player_inventory_find(p, item_idx);
    if (slot < 0) return 0;
    p->inventory[slot] = ITEM_NONE;
    return 1;
}

static inline void osrs_player_set_equipment_slot(
    Player* p,
    int gear_slot,
    uint8_t item_idx
) {
    if (gear_slot < 0 || gear_slot >= NUM_GEAR_SLOTS) {
        fprintf(stderr, "osrs_player_set_equipment_slot: invalid gear slot %d\n", gear_slot);
        abort();
    }
    p->equipped[gear_slot] = item_idx;
    if (gear_slot == GEAR_SLOT_WEAPON) {
        osrs_player_refresh_weapon_state(p, item_idx);
        if (item_idx < NUM_ITEMS && item_is_two_handed(item_idx)) {
            p->equipped[GEAR_SLOT_SHIELD] = ITEM_NONE;
        }
    }
    p->slot_gear_dirty = 1;
    osrs_refresh_player_equipment(p);
}

static inline void osrs_player_equip_direct_item(Player* p, uint8_t item_idx) {
    int gear_slot = osrs_item_gear_slot(item_idx);
    if (gear_slot < 0) {
        fprintf(stderr, "osrs_player_equip_direct_item: item %u has no gear slot\n",
            (unsigned)item_idx);
        abort();
    }
    osrs_player_set_equipment_slot(p, gear_slot, item_idx);
}

static inline int osrs_player_can_equip_from_inventory_slot(const Player* p, int inventory_slot) {
    if (inventory_slot < 0 || inventory_slot >= OSRS_INVENTORY_SIZE) return 0;
    uint8_t item_idx = p->inventory[inventory_slot];
    if (item_idx == ITEM_NONE) return 0;

    int gear_slot = osrs_item_gear_slot(item_idx);
    if (gear_slot < 0) return 0;
    if (p->equipped[gear_slot] == item_idx) return 0;

    uint8_t old_item = p->equipped[gear_slot];
    uint8_t old_shield = p->equipped[GEAR_SLOT_SHIELD];

    int equipping_two_handed =
        gear_slot == GEAR_SLOT_WEAPON && item_is_two_handed(item_idx);

    if (equipping_two_handed && old_shield != ITEM_NONE && old_item != ITEM_NONE &&
            osrs_player_inventory_free_slots(p) <= 0) {
        return 0;
    }

    return 1;
}

static inline int osrs_player_equip_from_inventory_slot(Player* p, int inventory_slot) {
    if (inventory_slot < 0 || inventory_slot >= OSRS_INVENTORY_SIZE) return 0;
    uint8_t item_idx = p->inventory[inventory_slot];
    if (item_idx == ITEM_NONE) return 0;

    int gear_slot = osrs_item_gear_slot(item_idx);
    if (gear_slot < 0) return 0;
    if (p->equipped[gear_slot] == item_idx) return 0;

    uint8_t old_item = p->equipped[gear_slot];
    uint8_t old_weapon = p->equipped[GEAR_SLOT_WEAPON];
    uint8_t old_shield = p->equipped[GEAR_SLOT_SHIELD];

    int equipping_two_handed =
        gear_slot == GEAR_SLOT_WEAPON && item_is_two_handed(item_idx);
    int equipping_shield_over_two_handed =
        gear_slot == GEAR_SLOT_SHIELD && old_weapon < NUM_ITEMS &&
        item_is_two_handed(old_weapon);

    if (equipping_two_handed && old_shield != ITEM_NONE && old_item != ITEM_NONE &&
            osrs_player_inventory_free_slots(p) <= 0) {
        return 0;
    }

    p->equipped[gear_slot] = item_idx;
    p->inventory[inventory_slot] = old_item;

    if (equipping_two_handed && old_shield != ITEM_NONE) {
        p->equipped[GEAR_SLOT_SHIELD] = ITEM_NONE;
        if (old_item == ITEM_NONE) {
            p->inventory[inventory_slot] = old_shield;
        } else if (osrs_player_inventory_add(p, old_shield) < 0) {
            fprintf(stderr, "osrs_player_equip_from_inventory_slot: shield precheck failed\n");
            abort();
        }
    } else if (equipping_shield_over_two_handed) {
        p->equipped[GEAR_SLOT_WEAPON] = ITEM_NONE;
        p->inventory[inventory_slot] = old_weapon;
    }

    if (gear_slot == GEAR_SLOT_WEAPON || equipping_shield_over_two_handed) {
        osrs_player_refresh_weapon_state(p, p->equipped[GEAR_SLOT_WEAPON]);
        if (p->equipped[GEAR_SLOT_WEAPON] != old_weapon) {
            p->spec_armed = 0;
        }
    }
    p->slot_gear_dirty = 1;
    osrs_refresh_player_equipment(p);
    return 1;
}

static inline int osrs_player_equip_inventory_item(Player* p, uint8_t item_idx) {
    if (item_idx == ITEM_NONE) return 0;
    int gear_slot = osrs_item_gear_slot(item_idx);
    if (gear_slot < 0) return 0;
    if (p->equipped[gear_slot] == item_idx) return 0;
    int inventory_slot = osrs_player_inventory_find(p, item_idx);
    if (inventory_slot < 0) return 0;
    return osrs_player_equip_from_inventory_slot(p, inventory_slot);
}

static inline int osrs_player_equip_command_item(
    Player* p,
    int inventory_slot,
    uint8_t item_idx
) {
    if (inventory_slot >= 0 && inventory_slot < OSRS_INVENTORY_SIZE &&
            p->inventory[inventory_slot] == item_idx) {
        return osrs_player_equip_from_inventory_slot(p, inventory_slot);
    }
    return osrs_player_equip_inventory_item(p, item_idx);
}

static inline int osrs_player_unequipped_gear_count(const Player* p) {
    return osrs_player_inventory_count(p);
}

static inline AttackStyle osrs_player_weapon_attack_style(const Player* p) {
    uint8_t weapon = p->equipped[GEAR_SLOT_WEAPON];
    if (weapon >= NUM_ITEMS) return ATTACK_STYLE_NONE;
    return (AttackStyle)get_item_attack_style(weapon);
}

#endif
