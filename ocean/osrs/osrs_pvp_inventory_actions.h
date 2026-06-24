/**
 * @file osrs_pvp_inventory_actions.h
 * @brief PvP inventory-cell action head routing.
 */

#ifndef OSRS_PVP_INVENTORY_ACTIONS_H
#define OSRS_PVP_INVENTORY_ACTIONS_H

#include "osrs_inventory_clicks.h"
#include "osrs_items.h"
#include "osrs_types.h"

static inline int pvp_inventory_head_for_cell(OsrsInventorySlotView cell) {
    if (cell.kind == OSRS_INVENTORY_SLOT_EQUIPMENT) {
        int gear_slot = osrs_item_gear_slot(cell.item_idx);
        if (gear_slot < 0 || gear_slot >= NUM_GEAR_SLOTS) return -1;
        return HEAD_EQUIP_SLOT(gear_slot);
    }
    if (cell.kind == OSRS_INVENTORY_SLOT_FOOD) return HEAD_FOOD;
    if (cell.kind == OSRS_INVENTORY_SLOT_KARAMBWAN) return HEAD_KARAMBWAN;
    if (cell.kind == OSRS_INVENTORY_SLOT_BREW ||
            cell.kind == OSRS_INVENTORY_SLOT_RESTORE ||
            cell.kind == OSRS_INVENTORY_SLOT_COMBAT_POTION ||
            cell.kind == OSRS_INVENTORY_SLOT_RANGED_POTION ||
            cell.kind == OSRS_INVENTORY_SLOT_ANTIVENOM ||
            cell.kind == OSRS_INVENTORY_SLOT_PRAYER_POTION ||
            cell.kind == OSRS_INVENTORY_SLOT_BASTION_POTION ||
            cell.kind == OSRS_INVENTORY_SLOT_STAMINA_POTION ||
            cell.kind == OSRS_INVENTORY_SLOT_SATURATED_HEART) {
        return HEAD_DRINK;
    }
    return -1;
}

static inline int pvp_inventory_queue_cell_click(
    const Player* p,
    int* actions,
    int inventory_slot
) {
    if (inventory_slot < 0 || inventory_slot >= OSRS_INVENTORY_SIZE) return 0;
    OsrsInventoryView view;
    osrs_inventory_view_build(p, &view);
    int head = pvp_inventory_head_for_cell(view.slots[inventory_slot]);
    if (head < 0) return 0;
    actions[head] = inventory_slot + 1;
    return 1;
}

static inline int pvp_inventory_queue_kind_click(
    const Player* p,
    int* actions,
    OsrsInventorySlotKind kind
) {
    if (kind == OSRS_INVENTORY_SLOT_EMPTY) return 0;
    OsrsInventoryView view;
    osrs_inventory_view_build(p, &view);
    int inventory_slot = osrs_inventory_view_find_kind(&view, kind);
    if (inventory_slot < 0) return 0;
    int head = pvp_inventory_head_for_cell(view.slots[inventory_slot]);
    if (head < 0) return 0;
    actions[head] = inventory_slot + 1;
    return 1;
}

#endif
