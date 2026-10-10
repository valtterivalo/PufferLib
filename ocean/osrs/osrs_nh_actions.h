#ifndef OSRS_NH_ACTIONS_H
#define OSRS_NH_ACTIONS_H

#include "osrs_policy.h"
#include "osrs_nh_actions_generated.h"

#define NH_PVP_ACTION_SCHEMA_VERSION 3
#define NH_PVP_HEAD_PRIMARY 0
#define NH_PVP_HEAD_OVERHEAD 1
#define NH_PVP_HEAD_EQUIP_BASE 2
#define NH_PVP_HEAD_EQUIP_SLOT(slot) (NH_PVP_HEAD_EQUIP_BASE + (slot))
#define NH_PVP_HEAD_FOOD (NH_PVP_HEAD_EQUIP_BASE + NUM_GEAR_SLOTS)
#define NH_PVP_HEAD_DRINK (NH_PVP_HEAD_FOOD + 1)
#define NH_PVP_HEAD_KARAMBWAN (NH_PVP_HEAD_DRINK + 1)
#define NH_PVP_HEAD_UTILITY (NH_PVP_HEAD_KARAMBWAN + 1)
#define NH_PVP_HEAD_SPECIAL (NH_PVP_HEAD_UTILITY + 1)
#define NH_PVP_HEAD_OFFENSIVE (NH_PVP_HEAD_SPECIAL + 1)
#define NH_PVP_NUM_ACTION_HEADS (NH_PVP_HEAD_OFFENSIVE + 1)

typedef enum {
    NH_PVP_PRIMARY_RETAIN = 0,
    NH_PVP_PRIMARY_WEAPON = OSRS_PRIMARY_MOVE_ACTIONS,
    NH_PVP_PRIMARY_BLOOD,
    NH_PVP_PRIMARY_ICE,
    NH_PVP_PRIMARY_DIM,
} NhPvpPrimaryAction;

typedef enum {
    NH_PVP_EQUIPMENT_NO_CHANGE = 0,
    NH_PVP_EQUIPMENT_UNEQUIP = 1,
    NH_PVP_EQUIPMENT_ITEM_BASE = 2,
} NhPvpEquipmentAction;

typedef enum {
    NH_PVP_FOOD_NONE = 0,
    NH_PVP_FOOD_SHARK,
    NH_PVP_FOOD_MARLIN,
    NH_PVP_FOOD_SUMMER_PIE,
    NH_PVP_FOOD_DIM,
} NhPvpFoodAction;

typedef enum {
    NH_PVP_DRINK_NONE = 0,
    NH_PVP_DRINK_BREW,
    NH_PVP_DRINK_RESTORE,
    NH_PVP_DRINK_COMBAT,
    NH_PVP_DRINK_RANGING,
    NH_PVP_DRINK_SANFEW,
    NH_PVP_DRINK_ANTIVENOM,
    NH_PVP_DRINK_DIM,
} NhPvpDrinkAction;

typedef enum {
    NH_PVP_KARAMBWAN_NONE = 0,
    NH_PVP_KARAMBWAN_KARAMBWAN,
    NH_PVP_KARAMBWAN_HALIBUT,
    NH_PVP_KARAMBWAN_DIM,
} NhPvpKarambwanAction;

typedef enum {
    NH_PVP_UTILITY_NONE = 0,
    NH_PVP_UTILITY_VENGEANCE,
    NH_PVP_UTILITY_DIM,
} NhPvpUtilityAction;

typedef enum {
    NH_PVP_SPECIAL_RETAIN = 0,
    NH_PVP_SPECIAL_ARM,
    NH_PVP_SPECIAL_DISARM,
    NH_PVP_SPECIAL_PREPARE_DOUBLE,
    NH_PVP_SPECIAL_DIM,
} NhPvpSpecialAction;

#define NH_PVP_ACTION_DIMS_INIT { \
    NH_PVP_PRIMARY_DIM, OSRS_OVERHEAD_DIM, \
    NH_PVP_EQUIPMENT_DIM_0, NH_PVP_EQUIPMENT_DIM_1, \
    NH_PVP_EQUIPMENT_DIM_2, NH_PVP_EQUIPMENT_DIM_3, \
    NH_PVP_EQUIPMENT_DIM_4, NH_PVP_EQUIPMENT_DIM_5, \
    NH_PVP_EQUIPMENT_DIM_6, NH_PVP_EQUIPMENT_DIM_7, \
    NH_PVP_EQUIPMENT_DIM_8, NH_PVP_EQUIPMENT_DIM_9, \
    NH_PVP_EQUIPMENT_DIM_10, NH_PVP_FOOD_DIM, NH_PVP_DRINK_DIM, \
    NH_PVP_KARAMBWAN_DIM, NH_PVP_UTILITY_DIM, \
    NH_PVP_SPECIAL_DIM, OSRS_OFFENSIVE_DIM \
}
#define NH_PVP_ACTION_MASK_SIZE \
    (NH_PVP_PRIMARY_DIM + OSRS_OVERHEAD_DIM + NH_PVP_EQUIPMENT_LOGITS + \
     NH_PVP_FOOD_DIM + NH_PVP_DRINK_DIM + NH_PVP_KARAMBWAN_DIM + \
     NH_PVP_UTILITY_DIM + NH_PVP_SPECIAL_DIM + OSRS_OFFENSIVE_DIM)

static const int NH_PVP_ACTION_DIMS[NH_PVP_NUM_ACTION_HEADS] = NH_PVP_ACTION_DIMS_INIT;
static const OsrsConsumableKind NH_PVP_FOOD_KINDS[NH_PVP_FOOD_DIM] = {
    OSRS_CONSUMABLE_NONE, OSRS_CONSUMABLE_SHARK_FOOD,
    OSRS_CONSUMABLE_MARLIN, OSRS_CONSUMABLE_SUMMER_PIE,
};
static const OsrsConsumableKind NH_PVP_DRINK_KINDS[NH_PVP_DRINK_DIM] = {
    OSRS_CONSUMABLE_NONE, OSRS_CONSUMABLE_BREW, OSRS_CONSUMABLE_SUPER_RESTORE,
    OSRS_CONSUMABLE_SUPER_COMBAT, OSRS_CONSUMABLE_RANGING,
    OSRS_CONSUMABLE_SANFEW, OSRS_CONSUMABLE_ANTIVENOM_PLUS,
};
static const OsrsConsumableKind NH_PVP_KARAMBWAN_KINDS[NH_PVP_KARAMBWAN_DIM] = {
    OSRS_CONSUMABLE_NONE, OSRS_CONSUMABLE_KARAMBWAN, OSRS_CONSUMABLE_HALIBUT,
};

static inline int nh_pvp_action_head_offset(int head) {
    assert(head >= 0 && head < NH_PVP_NUM_ACTION_HEADS);
    int offset = 0;
    for (int preceding = 0; preceding < head; preceding++)
        offset += NH_PVP_ACTION_DIMS[preceding];
    return offset;
}

static inline int nh_pvp_equipment_action_for_item(int slot, uint8_t item) {
    assert(slot >= 0 && slot < NUM_GEAR_SLOTS);
    int count = NH_PVP_ACTION_DIMS[NH_PVP_HEAD_EQUIP_SLOT(slot)] - NH_PVP_EQUIPMENT_ITEM_BASE;
    for (int index = 0; index < count; index++)
        if (NH_PVP_EQUIPMENT_ITEMS[slot][index] == item)
            return index + NH_PVP_EQUIPMENT_ITEM_BASE;
    fprintf(stderr, "NH equipment action: item%d does not belong to slot%d\n", item, slot);
    abort();
}

static inline int nh_pvp_find_item_cell(const Player* player, uint8_t item) {
    for (int cell = 0; cell < OSRS_INVENTORY_SIZE; cell++)
        if (osrs_inventory_cell_item_index(&player->inventory_cells[cell]) == item)
            return cell;
    return -1;
}

static inline int nh_pvp_find_consumable_cell(const Player* player, OsrsConsumableKind kind) {
    for (int cell = 0; cell < OSRS_INVENTORY_SIZE; cell++)
        if (osrs_inventory_cell_metadata(&player->inventory_cells[cell])->consumable_kind == kind)
            return cell;
    return -1;
}

#endif
