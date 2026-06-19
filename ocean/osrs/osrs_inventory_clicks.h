/**
 * @file osrs_inventory_clicks.h
 * @brief Shared OSRS inventory click interpretation, slot projection, and obs features.
 */

#ifndef OSRS_INVENTORY_CLICKS_H
#define OSRS_INVENTORY_CLICKS_H

#include <stdint.h>
#include <string.h>

#include "osrs_types.h"
#include "osrs_items.h"

typedef enum {
    OSRS_INVENTORY_SLOT_EMPTY,
    OSRS_INVENTORY_SLOT_EQUIPMENT,
    OSRS_INVENTORY_SLOT_FOOD,
    OSRS_INVENTORY_SLOT_KARAMBWAN,
    OSRS_INVENTORY_SLOT_BREW,
    OSRS_INVENTORY_SLOT_RESTORE,
    OSRS_INVENTORY_SLOT_COMBAT_POTION,
    OSRS_INVENTORY_SLOT_RANGED_POTION,
    OSRS_INVENTORY_SLOT_ANTIVENOM,
    OSRS_INVENTORY_SLOT_PRAYER_POTION,
    OSRS_INVENTORY_SLOT_BASTION_POTION,
    OSRS_INVENTORY_SLOT_STAMINA_POTION,
    OSRS_INVENTORY_SLOT_SATURATED_HEART,
} OsrsInventorySlotKind;

typedef enum {
    OSRS_CLICK_NONE = 0,
    OSRS_CLICK_EQUIP = 1,
    OSRS_CLICK_EAT = 2,
    OSRS_CLICK_DRINK = 3,
} OsrsClickAction;

typedef enum {
    OSRS_CONSUMABLE_NONE = 0,
    OSRS_CONSUMABLE_BREW = 1,
    OSRS_CONSUMABLE_SUPER_RESTORE = 2,
    OSRS_CONSUMABLE_SANFEW = 3,
    OSRS_CONSUMABLE_SUPER_COMBAT = 4,
    OSRS_CONSUMABLE_DIVINE_COMBAT = 5,
    OSRS_CONSUMABLE_RANGING = 6,
    OSRS_CONSUMABLE_DIVINE_RANGING = 7,
    OSRS_CONSUMABLE_SURGE = 8,
    OSRS_CONSUMABLE_GUTHIX_REST = 9,
    OSRS_CONSUMABLE_SATURATED_HEART = 10,
    OSRS_CONSUMABLE_SHARK_FOOD = 11,
    OSRS_CONSUMABLE_KARAMBWAN = 12,
    OSRS_CONSUMABLE_ANTIVENOM = 13,
    OSRS_CONSUMABLE_PRAYER_POTION = 14,
    OSRS_CONSUMABLE_BASTION = 15,
    OSRS_CONSUMABLE_STAMINA = 16,
} OsrsConsumableKind;

typedef enum {
    OSRS_CLICK_TICK_FIRST = 0,
    OSRS_CLICK_TICK_DUPLICATE = 1,
} OsrsClickTickMultiplicity;

typedef struct {
    uint16_t raw_osrs_id;
    OsrsClickAction click_action;
    OsrsConsumableKind consumable_kind;
    uint8_t dose_count;
} OsrsConsumableClick;

typedef struct {
    OsrsClickAction click_action;
    OsrsConsumableKind consumable_kind;
    uint8_t dose_count;
    uint16_t raw_osrs_id_after_drink;
} OsrsInventoryClickResolution;

typedef struct {
    OsrsInventorySlotKind kind;
    uint8_t item_idx;
    int doses;
    uint16_t raw_osrs_id;
} OsrsInventorySlotView;

typedef struct {
    OsrsInventorySlotView slots[OSRS_INVENTORY_SIZE];
} OsrsInventoryView;

typedef struct {
    int inventory_slot;
    OsrsInventorySlotView cell;
    OsrsInventoryClickResolution resolution;
} OsrsInventoryClickEvent;

#define OSRS_INVENTORY_CELL_OBS_FEATURES 16
#define OSRS_EQUIPPED_SELF_OBS_FEATURES 12

enum {
    OSRS_RAW_ID_SHARK = 385,
    OSRS_RAW_ID_KARAMBWAN = 3144,
    OSRS_RAW_ID_BREW_4 = 6685,
    OSRS_RAW_ID_BREW_3 = 6687,
    OSRS_RAW_ID_BREW_2 = 6689,
    OSRS_RAW_ID_BREW_1 = 6691,
    OSRS_RAW_ID_RESTORE_4 = 3024,
    OSRS_RAW_ID_RESTORE_3 = 3026,
    OSRS_RAW_ID_RESTORE_2 = 3028,
    OSRS_RAW_ID_RESTORE_1 = 3030,
    OSRS_RAW_ID_COMBAT_4 = 12695,
    OSRS_RAW_ID_COMBAT_3 = 12697,
    OSRS_RAW_ID_COMBAT_2 = 12699,
    OSRS_RAW_ID_COMBAT_1 = 12701,
    OSRS_RAW_ID_RANGED_4 = 2444,
    OSRS_RAW_ID_RANGED_3 = 169,
    OSRS_RAW_ID_RANGED_2 = 171,
    OSRS_RAW_ID_RANGED_1 = 173,
    OSRS_RAW_ID_ANTIVENOM_4 = 12913,
    OSRS_RAW_ID_ANTIVENOM_3 = 12915,
    OSRS_RAW_ID_ANTIVENOM_2 = 12917,
    OSRS_RAW_ID_ANTIVENOM_1 = 12919,
    OSRS_RAW_ID_PRAYER_4 = 2434,
    OSRS_RAW_ID_PRAYER_3 = 139,
    OSRS_RAW_ID_PRAYER_2 = 141,
    OSRS_RAW_ID_PRAYER_1 = 143,
    OSRS_RAW_ID_BASTION_4 = 22461,
    OSRS_RAW_ID_BASTION_3 = 22464,
    OSRS_RAW_ID_BASTION_2 = 22467,
    OSRS_RAW_ID_BASTION_1 = 22470,
    OSRS_RAW_ID_STAMINA_4 = 12625,
    OSRS_RAW_ID_STAMINA_3 = 12627,
    OSRS_RAW_ID_STAMINA_2 = 12629,
    OSRS_RAW_ID_STAMINA_1 = 12631,
    OSRS_RAW_ID_SATURATED_HEART = 27641,
};

static const OsrsConsumableClick OSRS_CONSUMABLE_CLICK_REGISTRY[] = {
    {OSRS_RAW_ID_BREW_4, OSRS_CLICK_DRINK, OSRS_CONSUMABLE_BREW, 4},
    {OSRS_RAW_ID_BREW_3, OSRS_CLICK_DRINK, OSRS_CONSUMABLE_BREW, 3},
    {OSRS_RAW_ID_BREW_2, OSRS_CLICK_DRINK, OSRS_CONSUMABLE_BREW, 2},
    {OSRS_RAW_ID_BREW_1, OSRS_CLICK_DRINK, OSRS_CONSUMABLE_BREW, 1},
    {OSRS_RAW_ID_RESTORE_4, OSRS_CLICK_DRINK, OSRS_CONSUMABLE_SUPER_RESTORE, 4},
    {OSRS_RAW_ID_RESTORE_3, OSRS_CLICK_DRINK, OSRS_CONSUMABLE_SUPER_RESTORE, 3},
    {OSRS_RAW_ID_RESTORE_2, OSRS_CLICK_DRINK, OSRS_CONSUMABLE_SUPER_RESTORE, 2},
    {OSRS_RAW_ID_RESTORE_1, OSRS_CLICK_DRINK, OSRS_CONSUMABLE_SUPER_RESTORE, 1},
    {10925, OSRS_CLICK_DRINK, OSRS_CONSUMABLE_SANFEW, 4},
    {10927, OSRS_CLICK_DRINK, OSRS_CONSUMABLE_SANFEW, 3},
    {10929, OSRS_CLICK_DRINK, OSRS_CONSUMABLE_SANFEW, 2},
    {10931, OSRS_CLICK_DRINK, OSRS_CONSUMABLE_SANFEW, 1},
    {OSRS_RAW_ID_COMBAT_4, OSRS_CLICK_DRINK, OSRS_CONSUMABLE_SUPER_COMBAT, 4},
    {OSRS_RAW_ID_COMBAT_3, OSRS_CLICK_DRINK, OSRS_CONSUMABLE_SUPER_COMBAT, 3},
    {OSRS_RAW_ID_COMBAT_2, OSRS_CLICK_DRINK, OSRS_CONSUMABLE_SUPER_COMBAT, 2},
    {OSRS_RAW_ID_COMBAT_1, OSRS_CLICK_DRINK, OSRS_CONSUMABLE_SUPER_COMBAT, 1},
    {23685, OSRS_CLICK_DRINK, OSRS_CONSUMABLE_DIVINE_COMBAT, 4},
    {23688, OSRS_CLICK_DRINK, OSRS_CONSUMABLE_DIVINE_COMBAT, 3},
    {23691, OSRS_CLICK_DRINK, OSRS_CONSUMABLE_DIVINE_COMBAT, 2},
    {23694, OSRS_CLICK_DRINK, OSRS_CONSUMABLE_DIVINE_COMBAT, 1},
    {OSRS_RAW_ID_RANGED_4, OSRS_CLICK_DRINK, OSRS_CONSUMABLE_RANGING, 4},
    {OSRS_RAW_ID_RANGED_3, OSRS_CLICK_DRINK, OSRS_CONSUMABLE_RANGING, 3},
    {OSRS_RAW_ID_RANGED_2, OSRS_CLICK_DRINK, OSRS_CONSUMABLE_RANGING, 2},
    {OSRS_RAW_ID_RANGED_1, OSRS_CLICK_DRINK, OSRS_CONSUMABLE_RANGING, 1},
    {23733, OSRS_CLICK_DRINK, OSRS_CONSUMABLE_DIVINE_RANGING, 4},
    {23736, OSRS_CLICK_DRINK, OSRS_CONSUMABLE_DIVINE_RANGING, 3},
    {23739, OSRS_CLICK_DRINK, OSRS_CONSUMABLE_DIVINE_RANGING, 2},
    {23742, OSRS_CLICK_DRINK, OSRS_CONSUMABLE_DIVINE_RANGING, 1},
    {30875, OSRS_CLICK_DRINK, OSRS_CONSUMABLE_SURGE, 4},
    {30878, OSRS_CLICK_DRINK, OSRS_CONSUMABLE_SURGE, 3},
    {30881, OSRS_CLICK_DRINK, OSRS_CONSUMABLE_SURGE, 2},
    {30884, OSRS_CLICK_DRINK, OSRS_CONSUMABLE_SURGE, 1},
    {4417, OSRS_CLICK_DRINK, OSRS_CONSUMABLE_GUTHIX_REST, 4},
    {4419, OSRS_CLICK_DRINK, OSRS_CONSUMABLE_GUTHIX_REST, 3},
    {4421, OSRS_CLICK_DRINK, OSRS_CONSUMABLE_GUTHIX_REST, 2},
    {4423, OSRS_CLICK_DRINK, OSRS_CONSUMABLE_GUTHIX_REST, 1},
    {OSRS_RAW_ID_ANTIVENOM_4, OSRS_CLICK_DRINK, OSRS_CONSUMABLE_ANTIVENOM, 4},
    {OSRS_RAW_ID_ANTIVENOM_3, OSRS_CLICK_DRINK, OSRS_CONSUMABLE_ANTIVENOM, 3},
    {OSRS_RAW_ID_ANTIVENOM_2, OSRS_CLICK_DRINK, OSRS_CONSUMABLE_ANTIVENOM, 2},
    {OSRS_RAW_ID_ANTIVENOM_1, OSRS_CLICK_DRINK, OSRS_CONSUMABLE_ANTIVENOM, 1},
    {OSRS_RAW_ID_PRAYER_4, OSRS_CLICK_DRINK, OSRS_CONSUMABLE_PRAYER_POTION, 4},
    {OSRS_RAW_ID_PRAYER_3, OSRS_CLICK_DRINK, OSRS_CONSUMABLE_PRAYER_POTION, 3},
    {OSRS_RAW_ID_PRAYER_2, OSRS_CLICK_DRINK, OSRS_CONSUMABLE_PRAYER_POTION, 2},
    {OSRS_RAW_ID_PRAYER_1, OSRS_CLICK_DRINK, OSRS_CONSUMABLE_PRAYER_POTION, 1},
    {OSRS_RAW_ID_BASTION_4, OSRS_CLICK_DRINK, OSRS_CONSUMABLE_BASTION, 4},
    {OSRS_RAW_ID_BASTION_3, OSRS_CLICK_DRINK, OSRS_CONSUMABLE_BASTION, 3},
    {OSRS_RAW_ID_BASTION_2, OSRS_CLICK_DRINK, OSRS_CONSUMABLE_BASTION, 2},
    {OSRS_RAW_ID_BASTION_1, OSRS_CLICK_DRINK, OSRS_CONSUMABLE_BASTION, 1},
    {OSRS_RAW_ID_STAMINA_4, OSRS_CLICK_DRINK, OSRS_CONSUMABLE_STAMINA, 4},
    {OSRS_RAW_ID_STAMINA_3, OSRS_CLICK_DRINK, OSRS_CONSUMABLE_STAMINA, 3},
    {OSRS_RAW_ID_STAMINA_2, OSRS_CLICK_DRINK, OSRS_CONSUMABLE_STAMINA, 2},
    {OSRS_RAW_ID_STAMINA_1, OSRS_CLICK_DRINK, OSRS_CONSUMABLE_STAMINA, 1},
    {OSRS_RAW_ID_SATURATED_HEART, OSRS_CLICK_DRINK, OSRS_CONSUMABLE_SATURATED_HEART, 1},
    {OSRS_RAW_ID_SHARK, OSRS_CLICK_EAT, OSRS_CONSUMABLE_SHARK_FOOD, 0},
    {OSRS_RAW_ID_KARAMBWAN, OSRS_CLICK_EAT, OSRS_CONSUMABLE_KARAMBWAN, 0},
};

static inline void osrs_inventory_clicks_trap(void) {
#if defined(__clang__) || defined(__GNUC__)
    __builtin_trap();
#else
    *(volatile int*)0 = 0;
#endif
}

static inline float osrs_clamp_unit(float v) {
    if (v < -1.0f) return -1.0f;
    if (v > 1.0f) return 1.0f;
    return v;
}

static inline int osrs_consumable_click_registry_count(void) {
    return (int)(sizeof(OSRS_CONSUMABLE_CLICK_REGISTRY) /
        sizeof(OSRS_CONSUMABLE_CLICK_REGISTRY[0]));
}

static inline OsrsConsumableClick osrs_consumable_click_lookup_raw_osrs_id(
    uint16_t raw_osrs_id
) {
    int count = osrs_consumable_click_registry_count();
    for (int i = 0; i < count; i++) {
        if (OSRS_CONSUMABLE_CLICK_REGISTRY[i].raw_osrs_id == raw_osrs_id)
            return OSRS_CONSUMABLE_CLICK_REGISTRY[i];
    }

    return (OsrsConsumableClick){
        .raw_osrs_id = raw_osrs_id,
        .click_action = OSRS_CLICK_NONE,
        .consumable_kind = OSRS_CONSUMABLE_NONE,
        .dose_count = 0,
    };
}

static inline uint16_t osrs_consumable_raw_osrs_id(
    OsrsConsumableKind kind,
    uint8_t dose_count
) {
    int count = osrs_consumable_click_registry_count();
    for (int i = 0; i < count; i++) {
        OsrsConsumableClick candidate = OSRS_CONSUMABLE_CLICK_REGISTRY[i];
        if (candidate.consumable_kind == kind && candidate.dose_count == dose_count)
            return candidate.raw_osrs_id;
    }
    return 0;
}

static inline uint8_t osrs_consumable_dose_count_after_drink(uint8_t dose_count) {
    switch (dose_count) {
        case 1: return 0;
        case 2: return 1;
        case 3: return 2;
        case 4: return 3;
        default:
            osrs_inventory_clicks_trap();
            return 0;
    }
}

static inline uint16_t osrs_consumable_raw_osrs_id_after_drink(uint16_t raw_osrs_id) {
    OsrsConsumableClick before =
        osrs_consumable_click_lookup_raw_osrs_id(raw_osrs_id);
    if (before.click_action != OSRS_CLICK_DRINK || before.dose_count == 0)
        osrs_inventory_clicks_trap();

    uint8_t after_dose =
        osrs_consumable_dose_count_after_drink(before.dose_count);
    if (after_dose == 0) return 0;

    uint16_t after = osrs_consumable_raw_osrs_id(
        before.consumable_kind, after_dose);
    if (after == 0) osrs_inventory_clicks_trap();
    return after;
}

static inline OsrsConsumableKind osrs_inventory_slot_kind_consumable_kind(
    OsrsInventorySlotKind kind
) {
    switch (kind) {
        case OSRS_INVENTORY_SLOT_FOOD: return OSRS_CONSUMABLE_SHARK_FOOD;
        case OSRS_INVENTORY_SLOT_KARAMBWAN: return OSRS_CONSUMABLE_KARAMBWAN;
        case OSRS_INVENTORY_SLOT_BREW: return OSRS_CONSUMABLE_BREW;
        case OSRS_INVENTORY_SLOT_RESTORE: return OSRS_CONSUMABLE_SUPER_RESTORE;
        case OSRS_INVENTORY_SLOT_COMBAT_POTION: return OSRS_CONSUMABLE_SUPER_COMBAT;
        case OSRS_INVENTORY_SLOT_RANGED_POTION: return OSRS_CONSUMABLE_RANGING;
        case OSRS_INVENTORY_SLOT_ANTIVENOM: return OSRS_CONSUMABLE_ANTIVENOM;
        case OSRS_INVENTORY_SLOT_PRAYER_POTION: return OSRS_CONSUMABLE_PRAYER_POTION;
        case OSRS_INVENTORY_SLOT_BASTION_POTION: return OSRS_CONSUMABLE_BASTION;
        case OSRS_INVENTORY_SLOT_STAMINA_POTION: return OSRS_CONSUMABLE_STAMINA;
        case OSRS_INVENTORY_SLOT_SATURATED_HEART: return OSRS_CONSUMABLE_SATURATED_HEART;
        case OSRS_INVENTORY_SLOT_EMPTY:
        case OSRS_INVENTORY_SLOT_EQUIPMENT:
            return OSRS_CONSUMABLE_NONE;
        default:
            osrs_inventory_clicks_trap();
            return OSRS_CONSUMABLE_NONE;
    }
}

static inline OsrsInventorySlotKind osrs_consumable_kind_inventory_slot_kind(
    OsrsConsumableKind kind
) {
    switch (kind) {
        case OSRS_CONSUMABLE_SHARK_FOOD: return OSRS_INVENTORY_SLOT_FOOD;
        case OSRS_CONSUMABLE_KARAMBWAN: return OSRS_INVENTORY_SLOT_KARAMBWAN;
        case OSRS_CONSUMABLE_BREW: return OSRS_INVENTORY_SLOT_BREW;
        case OSRS_CONSUMABLE_SUPER_RESTORE: return OSRS_INVENTORY_SLOT_RESTORE;
        case OSRS_CONSUMABLE_SUPER_COMBAT: return OSRS_INVENTORY_SLOT_COMBAT_POTION;
        case OSRS_CONSUMABLE_RANGING: return OSRS_INVENTORY_SLOT_RANGED_POTION;
        case OSRS_CONSUMABLE_ANTIVENOM: return OSRS_INVENTORY_SLOT_ANTIVENOM;
        case OSRS_CONSUMABLE_PRAYER_POTION: return OSRS_INVENTORY_SLOT_PRAYER_POTION;
        case OSRS_CONSUMABLE_BASTION: return OSRS_INVENTORY_SLOT_BASTION_POTION;
        case OSRS_CONSUMABLE_STAMINA: return OSRS_INVENTORY_SLOT_STAMINA_POTION;
        case OSRS_CONSUMABLE_SATURATED_HEART: return OSRS_INVENTORY_SLOT_SATURATED_HEART;
        case OSRS_CONSUMABLE_NONE:
        case OSRS_CONSUMABLE_SANFEW:
        case OSRS_CONSUMABLE_DIVINE_COMBAT:
        case OSRS_CONSUMABLE_DIVINE_RANGING:
        case OSRS_CONSUMABLE_SURGE:
        case OSRS_CONSUMABLE_GUTHIX_REST:
            return OSRS_INVENTORY_SLOT_EMPTY;
        default:
            osrs_inventory_clicks_trap();
            return OSRS_INVENTORY_SLOT_EMPTY;
    }
}

static inline uint16_t osrs_inventory_slot_kind_raw_osrs_id(
    OsrsInventorySlotKind kind,
    int doses
) {
    OsrsConsumableKind consumable =
        osrs_inventory_slot_kind_consumable_kind(kind);
    if (consumable == OSRS_CONSUMABLE_NONE) return 0;

    uint8_t dose_count = 0;
    switch (kind) {
        case OSRS_INVENTORY_SLOT_FOOD:
        case OSRS_INVENTORY_SLOT_KARAMBWAN:
            dose_count = 0;
            break;
        case OSRS_INVENTORY_SLOT_SATURATED_HEART:
            dose_count = 1;
            break;
        default:
            dose_count = (uint8_t)(doses >= 4 ? 4 : doses);
            if (dose_count == 0) return 0;
            break;
    }
    return osrs_consumable_raw_osrs_id(consumable, dose_count);
}

static inline void osrs_inventory_view_clear(OsrsInventoryView* view) {
    memset(view, 0, sizeof(*view));
    for (int slot = 0; slot < OSRS_INVENTORY_SIZE; slot++) {
        view->slots[slot] = (OsrsInventorySlotView){
            .kind = OSRS_INVENTORY_SLOT_EMPTY,
            .item_idx = ITEM_NONE,
            .doses = 0,
            .raw_osrs_id = 0,
        };
    }
}

static inline int osrs_inventory_view_first_empty(const OsrsInventoryView* view) {
    for (int slot = 0; slot < OSRS_INVENTORY_SIZE; slot++) {
        if (view->slots[slot].kind == OSRS_INVENTORY_SLOT_EMPTY) return slot;
    }
    return -1;
}

static inline void osrs_inventory_view_add_supply(
    OsrsInventoryView* view,
    OsrsInventorySlotKind kind,
    int count_or_doses
) {
    int remaining = count_or_doses;
    while (remaining > 0) {
        int slot = osrs_inventory_view_first_empty(view);
        if (slot < 0) return;
        int is_food = kind == OSRS_INVENTORY_SLOT_FOOD ||
            kind == OSRS_INVENTORY_SLOT_KARAMBWAN ||
            kind == OSRS_INVENTORY_SLOT_SATURATED_HEART;
        int amount = is_food ? 1 : (remaining >= 4 ? 4 : remaining);
        view->slots[slot] = (OsrsInventorySlotView){
            .kind = kind,
            .item_idx = ITEM_NONE,
            .doses = amount,
            .raw_osrs_id = osrs_inventory_slot_kind_raw_osrs_id(kind, amount),
        };
        remaining -= amount;
    }
}

static inline void osrs_inventory_view_build(const Player* p, OsrsInventoryView* view) {
    osrs_inventory_view_clear(view);

    for (int slot = 0; slot < OSRS_INVENTORY_SIZE; slot++) {
        uint8_t item = p->inventory[slot];
        if (item == ITEM_NONE) continue;
        uint16_t raw_osrs_id = item < NUM_ITEMS
            ? (uint16_t)ITEM_DATABASE[item].item_id
            : 0;
        view->slots[slot] = (OsrsInventorySlotView){
            .kind = OSRS_INVENTORY_SLOT_EQUIPMENT,
            .item_idx = item,
            .doses = 0,
            .raw_osrs_id = raw_osrs_id,
        };
    }

    osrs_inventory_view_add_supply(view, OSRS_INVENTORY_SLOT_FOOD, p->food_count);
    osrs_inventory_view_add_supply(view, OSRS_INVENTORY_SLOT_KARAMBWAN, p->karambwan_count);
    osrs_inventory_view_add_supply(view, OSRS_INVENTORY_SLOT_BREW, p->brew_doses);
    osrs_inventory_view_add_supply(view, OSRS_INVENTORY_SLOT_RESTORE, p->restore_doses);
    osrs_inventory_view_add_supply(
        view, OSRS_INVENTORY_SLOT_COMBAT_POTION, p->combat_potion_doses);
    osrs_inventory_view_add_supply(
        view, OSRS_INVENTORY_SLOT_RANGED_POTION, p->ranged_potion_doses);
    osrs_inventory_view_add_supply(view, OSRS_INVENTORY_SLOT_ANTIVENOM, p->antivenom_doses);
    osrs_inventory_view_add_supply(
        view, OSRS_INVENTORY_SLOT_PRAYER_POTION, p->prayer_pot_doses);
    osrs_inventory_view_add_supply(view, OSRS_INVENTORY_SLOT_BASTION_POTION, p->bastion_doses);
    osrs_inventory_view_add_supply(view, OSRS_INVENTORY_SLOT_STAMINA_POTION, p->stamina_doses);
    osrs_inventory_view_add_supply(
        view, OSRS_INVENTORY_SLOT_SATURATED_HEART, p->saturated_heart_count);
}

static inline int osrs_inventory_view_find_kind(
    const OsrsInventoryView* view,
    OsrsInventorySlotKind kind
) {
    for (int slot = 0; slot < OSRS_INVENTORY_SIZE; slot++) {
        if (view->slots[slot].kind == kind) return slot;
    }
    return -1;
}

static inline OsrsClickAction osrs_item_click_action(uint8_t item_idx) {
    if (item_idx == ITEM_NONE) return OSRS_CLICK_NONE;
    if (item_idx >= NUM_ITEMS) osrs_inventory_clicks_trap();

    switch (ITEM_DATABASE[item_idx].slot) {
        case SLOT_HEAD:
        case SLOT_CAPE:
        case SLOT_NECK:
        case SLOT_WEAPON:
        case SLOT_BODY:
        case SLOT_SHIELD:
        case SLOT_LEGS:
        case SLOT_HANDS:
        case SLOT_FEET:
        case SLOT_RING:
        case SLOT_AMMO:
            return OSRS_CLICK_EQUIP;
        default:
            osrs_inventory_clicks_trap();
            return OSRS_CLICK_NONE;
    }
}

static inline OsrsConsumableKind osrs_item_click_consumable_kind(uint8_t item_idx) {
    if (item_idx == ITEM_NONE) return OSRS_CONSUMABLE_NONE;
    if (item_idx >= NUM_ITEMS) osrs_inventory_clicks_trap();
    return OSRS_CONSUMABLE_NONE;
}

static inline OsrsInventoryClickResolution osrs_inventory_click_interpret(
    uint8_t item_idx,
    uint16_t raw_osrs_id,
    OsrsClickTickMultiplicity tick_multiplicity
) {
    OsrsInventoryClickResolution none_resolution = {
        .click_action = OSRS_CLICK_NONE,
        .consumable_kind = OSRS_CONSUMABLE_NONE,
        .dose_count = 0,
        .raw_osrs_id_after_drink = 0,
    };

    switch (tick_multiplicity) {
        case OSRS_CLICK_TICK_DUPLICATE:
            return none_resolution;
        case OSRS_CLICK_TICK_FIRST:
            break;
        default:
            osrs_inventory_clicks_trap();
    }

    if (item_idx != ITEM_NONE) {
        OsrsClickAction action = osrs_item_click_action(item_idx);
        if (raw_osrs_id != 0 && raw_osrs_id != ITEM_DATABASE[item_idx].item_id)
            osrs_inventory_clicks_trap();
        return (OsrsInventoryClickResolution){
            .click_action = action,
            .consumable_kind = OSRS_CONSUMABLE_NONE,
            .dose_count = 0,
            .raw_osrs_id_after_drink = 0,
        };
    }

    if (raw_osrs_id == 0) return none_resolution;

    OsrsConsumableClick consumable =
        osrs_consumable_click_lookup_raw_osrs_id(raw_osrs_id);
    uint16_t after_drink = 0;
    if (consumable.click_action == OSRS_CLICK_DRINK && consumable.dose_count > 0)
        after_drink = osrs_consumable_raw_osrs_id_after_drink(raw_osrs_id);

    return (OsrsInventoryClickResolution){
        .click_action = consumable.click_action,
        .consumable_kind = consumable.consumable_kind,
        .dose_count = consumable.dose_count,
        .raw_osrs_id_after_drink = after_drink,
    };
}

static inline int osrs_inventory_click_queue_slot(
    int* click_heads,
    int click_head_count,
    int inventory_slot
) {
    if (inventory_slot < 0 || inventory_slot >= OSRS_INVENTORY_SIZE) return 0;
    int action = inventory_slot + 1;
    for (int h = 0; h < click_head_count; h++) {
        if (click_heads[h] == action) return 1;
    }
    for (int h = 0; h < click_head_count; h++) {
        if (click_heads[h] == 0) {
            click_heads[h] = action;
            return 1;
        }
    }
    return 0;
}

static inline int osrs_inventory_click_queue_kind(
    const Player* p,
    int* click_heads,
    int click_head_count,
    OsrsInventorySlotKind kind
) {
    if (kind == OSRS_INVENTORY_SLOT_EMPTY) return 0;
    OsrsInventoryView view;
    osrs_inventory_view_build(p, &view);
    return osrs_inventory_click_queue_slot(
        click_heads,
        click_head_count,
        osrs_inventory_view_find_kind(&view, kind));
}

static inline int osrs_inventory_click_action_to_slot(int action) {
    if (action <= 0 || action > OSRS_INVENTORY_SIZE) return -1;
    return action - 1;
}

static inline int osrs_inventory_click_collect_events(
    const OsrsInventoryView* view,
    const int* click_actions,
    int click_action_count,
    OsrsInventoryClickEvent* events,
    int event_capacity
) {
    uint8_t clicked_slots[OSRS_INVENTORY_SIZE] = {0};
    int event_count = 0;

    for (int h = 0; h < click_action_count; h++) {
        int inventory_slot = osrs_inventory_click_action_to_slot(click_actions[h]);
        if (inventory_slot < 0) continue;

        OsrsClickTickMultiplicity multiplicity = clicked_slots[inventory_slot]
            ? OSRS_CLICK_TICK_DUPLICATE
            : OSRS_CLICK_TICK_FIRST;
        clicked_slots[inventory_slot] = 1;

        OsrsInventorySlotView cell = view->slots[inventory_slot];
        OsrsInventoryClickResolution resolution = osrs_inventory_click_interpret(
            cell.item_idx, cell.raw_osrs_id, multiplicity);
        if (resolution.click_action == OSRS_CLICK_NONE) continue;
        if (event_count >= event_capacity) osrs_inventory_clicks_trap();
        events[event_count++] = (OsrsInventoryClickEvent){
            .inventory_slot = inventory_slot,
            .cell = cell,
            .resolution = resolution,
        };
    }

    return event_count;
}

static inline OsrsInventorySlotKind osrs_inventory_slot_kind_for_potion_action(int potion) {
    switch (potion) {
        case POTION_BREW: return OSRS_INVENTORY_SLOT_BREW;
        case POTION_RESTORE: return OSRS_INVENTORY_SLOT_RESTORE;
        case POTION_COMBAT: return OSRS_INVENTORY_SLOT_COMBAT_POTION;
        case POTION_RANGED: return OSRS_INVENTORY_SLOT_RANGED_POTION;
        case POTION_ANTIVENOM: return OSRS_INVENTORY_SLOT_ANTIVENOM;
        case POTION_PRAYER_POT: return OSRS_INVENTORY_SLOT_PRAYER_POTION;
        case POTION_BASTION: return OSRS_INVENTORY_SLOT_BASTION_POTION;
        case POTION_STAMINA: return OSRS_INVENTORY_SLOT_STAMINA_POTION;
        default: return OSRS_INVENTORY_SLOT_EMPTY;
    }
}

static inline uint8_t osrs_item_index_for_raw_osrs_id(uint16_t raw_osrs_id) {
    for (int i = 0; i < NUM_ITEMS; i++) {
        if (ITEM_DATABASE[i].item_id == raw_osrs_id) return (uint8_t)i;
    }
    return ITEM_NONE;
}

static inline void osrs_write_inventory_cell_affordance_features(
    float* out,
    uint8_t item_idx,
    uint16_t raw_osrs_id,
    uint8_t dose,
    int can_use,
    int is_equipped,
    const float post_use_deltas[6]
) {
    OsrsConsumableClick consumable =
        osrs_consumable_click_lookup_raw_osrs_id(raw_osrs_id);
    int present = raw_osrs_id != 0 || item_idx != ITEM_NONE;
    int is_gear = item_idx != ITEM_NONE;
    int is_consumable = consumable.click_action != OSRS_CLICK_NONE;
    int style = is_gear ? get_item_attack_style(item_idx) : 0;
    uint32_t effect_mask = is_gear ? ITEM_DATABASE[item_idx].effect_mask : OSRS_ITEM_EFFECT_NONE;

    out[0] = present ? 1.0f : 0.0f;
    out[1] = is_gear ? 1.0f : 0.0f;
    out[2] = is_consumable ? 1.0f : 0.0f;
    out[3] = can_use ? 1.0f : 0.0f;
    out[4] = is_equipped ? 1.0f : 0.0f;
    out[5] = dose > 0 ? osrs_clamp_unit((float)dose / 4.0f) : 0.0f;
    out[6] = style == 1 ? 1.0f : 0.0f;
    out[7] = style == 2 ? 1.0f : 0.0f;
    out[8] = style == 3 ? 1.0f : 0.0f;
    for (int i = 0; i < 6; i++) {
        out[9 + i] = post_use_deltas ? osrs_clamp_unit(post_use_deltas[i]) : 0.0f;
    }
    out[15] = effect_mask != OSRS_ITEM_EFFECT_NONE ? 1.0f : 0.0f;
}

static inline void osrs_write_equipped_self_features(float* out, uint8_t item_idx) {
    for (int i = 0; i < OSRS_EQUIPPED_SELF_OBS_FEATURES; i++) out[i] = 0.0f;
    if (item_idx == ITEM_NONE) return;
    if (item_idx >= NUM_ITEMS) osrs_inventory_clicks_trap();

    const Item* item = &ITEM_DATABASE[item_idx];
    int style = get_item_attack_style(item_idx);
    out[0] = 1.0f;
    out[1] = style == 1 ? 1.0f : 0.0f;
    out[2] = style == 2 ? 1.0f : 0.0f;
    out[3] = style == 3 ? 1.0f : 0.0f;
    out[4] = osrs_clamp_unit((float)item->attack_slash / STAT_NORM_ATTACK);
    out[5] = osrs_clamp_unit((float)item->melee_strength / STAT_NORM_STRENGTH);
    out[6] = osrs_clamp_unit((float)item->attack_ranged / STAT_NORM_ATTACK);
    out[7] = osrs_clamp_unit((float)item->ranged_strength / STAT_NORM_STRENGTH);
    out[8] = osrs_clamp_unit(((float)item->attack_magic / STAT_NORM_ATTACK) +
        ((float)item->magic_damage / STAT_NORM_MAGIC_DMG));
    out[9] = osrs_clamp_unit((float)(item->defence_stab + item->defence_slash +
        item->defence_crush + item->defence_magic + item->defence_ranged) /
        (5.0f * STAT_NORM_DEFENCE));
    out[10] = item->effect_mask != OSRS_ITEM_EFFECT_NONE ? 1.0f : 0.0f;
    out[11] = item->slot == SLOT_WEAPON ? 1.0f : 0.0f;
}

#endif
