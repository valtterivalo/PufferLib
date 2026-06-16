/**
 * @file osrs_inventory_clicks.h
 * @brief Pure inventory-click classification and consumable registry.
 *
 * Stage 1 owns only the pure interpretation layer. Stage 2 binds the
 * OSRS_CLICK_EQUIP result to the shared inventory equip primitive and binds
 * consumable results to each encounter's existing boost or heal functions.
 *
 * A single inventory click resolves as follows. Empty cells resolve to
 * OSRS_CLICK_NONE. Gear cells resolve to OSRS_CLICK_EQUIP and Stage 2 must
 * equip-swap through osrs_player_can_equip_from_inventory_slot and
 * osrs_player_equip_from_inventory_slot, preserving two-handed suppression,
 * in-place swap semantics, special-attack disarm, and slot_gear_dirty. Raw
 * consumable OSRS ids resolve through the registry, apply the encounter's
 * existing boost or heal once, then decrement that cell's dose. Repeated clicks
 * on the same cell in the same tick pass OSRS_CLICK_TICK_DUPLICATE and resolve
 * to OSRS_CLICK_NONE, so a duplicate applies at most once.
 */

#ifndef OSRS_INVENTORY_CLICKS_H
#define OSRS_INVENTORY_CLICKS_H

#include "osrs_items.h"

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

static const OsrsConsumableClick OSRS_CONSUMABLE_CLICK_REGISTRY[] = {
    {6685, OSRS_CLICK_DRINK, OSRS_CONSUMABLE_BREW, 4},
    {6687, OSRS_CLICK_DRINK, OSRS_CONSUMABLE_BREW, 3},
    {6689, OSRS_CLICK_DRINK, OSRS_CONSUMABLE_BREW, 2},
    {6691, OSRS_CLICK_DRINK, OSRS_CONSUMABLE_BREW, 1},
    {3024, OSRS_CLICK_DRINK, OSRS_CONSUMABLE_SUPER_RESTORE, 4},
    {3026, OSRS_CLICK_DRINK, OSRS_CONSUMABLE_SUPER_RESTORE, 3},
    {3028, OSRS_CLICK_DRINK, OSRS_CONSUMABLE_SUPER_RESTORE, 2},
    {3030, OSRS_CLICK_DRINK, OSRS_CONSUMABLE_SUPER_RESTORE, 1},
    {10925, OSRS_CLICK_DRINK, OSRS_CONSUMABLE_SANFEW, 4},
    {10927, OSRS_CLICK_DRINK, OSRS_CONSUMABLE_SANFEW, 3},
    {10929, OSRS_CLICK_DRINK, OSRS_CONSUMABLE_SANFEW, 2},
    {10931, OSRS_CLICK_DRINK, OSRS_CONSUMABLE_SANFEW, 1},
    {12695, OSRS_CLICK_DRINK, OSRS_CONSUMABLE_SUPER_COMBAT, 4},
    {12697, OSRS_CLICK_DRINK, OSRS_CONSUMABLE_SUPER_COMBAT, 3},
    {12699, OSRS_CLICK_DRINK, OSRS_CONSUMABLE_SUPER_COMBAT, 2},
    {12701, OSRS_CLICK_DRINK, OSRS_CONSUMABLE_SUPER_COMBAT, 1},
    {23685, OSRS_CLICK_DRINK, OSRS_CONSUMABLE_DIVINE_COMBAT, 4},
    {23688, OSRS_CLICK_DRINK, OSRS_CONSUMABLE_DIVINE_COMBAT, 3},
    {23691, OSRS_CLICK_DRINK, OSRS_CONSUMABLE_DIVINE_COMBAT, 2},
    {23694, OSRS_CLICK_DRINK, OSRS_CONSUMABLE_DIVINE_COMBAT, 1},
    {2444, OSRS_CLICK_DRINK, OSRS_CONSUMABLE_RANGING, 4},
    {169, OSRS_CLICK_DRINK, OSRS_CONSUMABLE_RANGING, 3},
    {171, OSRS_CLICK_DRINK, OSRS_CONSUMABLE_RANGING, 2},
    {173, OSRS_CLICK_DRINK, OSRS_CONSUMABLE_RANGING, 1},
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
    {27641, OSRS_CLICK_DRINK, OSRS_CONSUMABLE_SATURATED_HEART, 1},
    {385, OSRS_CLICK_EAT, OSRS_CONSUMABLE_SHARK_FOOD, 0},
    {3144, OSRS_CLICK_EAT, OSRS_CONSUMABLE_KARAMBWAN, 0},
};

static inline void osrs_inventory_clicks_trap(void) {
#if defined(__clang__) || defined(__GNUC__)
    __builtin_trap();
#else
    *(volatile int*)0 = 0;
#endif
}

/**
 * Classifies an ITEM_DATABASE index for the inventory "Use" action.
 *
 * ITEM_NONE is a valid empty cell and resolves to OSRS_CLICK_NONE. Any other
 * index outside ITEM_DATABASE is a defect and traps.
 */
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

/**
 * Returns the consumable kind for an ITEM_DATABASE index.
 *
 * Stage 1 keeps consumables out of ITEM_DATABASE, so valid gear and ITEM_NONE
 * return OSRS_CONSUMABLE_NONE. Invalid non-empty item indexes trap.
 */
static inline OsrsConsumableKind osrs_item_click_consumable_kind(uint8_t item_idx) {
    if (item_idx == ITEM_NONE) return OSRS_CONSUMABLE_NONE;
    if (item_idx >= NUM_ITEMS) osrs_inventory_clicks_trap();
    return OSRS_CONSUMABLE_NONE;
}

/**
 * Looks up a raw OSRS item id in the colosseum consumable registry.
 *
 * Unknown ids are valid non-consumables and resolve to OSRS_CLICK_NONE.
 */
static inline OsrsConsumableClick osrs_consumable_click_lookup_raw_osrs_id(
    uint16_t raw_osrs_id
) {
    int count = (int)(
        sizeof(OSRS_CONSUMABLE_CLICK_REGISTRY) /
        sizeof(OSRS_CONSUMABLE_CLICK_REGISTRY[0])
    );

    for (int i = 0; i < count; i++) {
        if (OSRS_CONSUMABLE_CLICK_REGISTRY[i].raw_osrs_id == raw_osrs_id) {
            return OSRS_CONSUMABLE_CLICK_REGISTRY[i];
        }
    }

    return (OsrsConsumableClick){
        .raw_osrs_id = raw_osrs_id,
        .click_action = OSRS_CLICK_NONE,
        .consumable_kind = OSRS_CONSUMABLE_NONE,
        .dose_count = 0,
    };
}

/**
 * Computes the dose count remaining after one drink.
 *
 * Dose counts outside 1..4 are defects because food and unknown cells should
 * never route through a drink transition.
 */
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

/**
 * Returns the raw OSRS id for a vial after one drink.
 *
 * A one-dose vial returns 0, meaning the cell becomes empty in Stage 2. Unknown
 * ids, food, and non-dose consumables trap because they are not vial drinks.
 */
static inline uint16_t osrs_consumable_raw_osrs_id_after_drink(uint16_t raw_osrs_id) {
    OsrsConsumableClick before =
        osrs_consumable_click_lookup_raw_osrs_id(raw_osrs_id);
    if (before.click_action != OSRS_CLICK_DRINK || before.dose_count == 0) {
        osrs_inventory_clicks_trap();
    }

    uint8_t after_dose =
        osrs_consumable_dose_count_after_drink(before.dose_count);
    if (after_dose == 0) return 0;

    int count = (int)(
        sizeof(OSRS_CONSUMABLE_CLICK_REGISTRY) /
        sizeof(OSRS_CONSUMABLE_CLICK_REGISTRY[0])
    );

    for (int i = 0; i < count; i++) {
        OsrsConsumableClick candidate = OSRS_CONSUMABLE_CLICK_REGISTRY[i];
        if (candidate.consumable_kind == before.consumable_kind &&
            candidate.dose_count == after_dose) {
            return candidate.raw_osrs_id;
        }
    }

    osrs_inventory_clicks_trap();
    return 0;
}

/**
 * Pure Stage 1 resolver for one inventory-cell click.
 *
 * item_idx is ITEM_DATABASE state for gear cells or ITEM_NONE for raw-id-only
 * consumables. raw_osrs_id is 0 for empty cells, may equal the gear item's
 * ITEM_DATABASE raw id for gear cells, and carries the potion or food item id
 * for consumable cells. If both item_idx and raw_osrs_id are set for gear, they
 * must agree. Stage 2 owns the state mutation after this pure result.
 */
static inline OsrsInventoryClickResolution osrs_inventory_click_interpret(
    uint8_t item_idx,
    uint16_t raw_osrs_id,
    OsrsClickTickMultiplicity tick_multiplicity
) {
    switch (tick_multiplicity) {
        case OSRS_CLICK_TICK_DUPLICATE:
            return (OsrsInventoryClickResolution){0};
        case OSRS_CLICK_TICK_FIRST:
            break;
        default:
            osrs_inventory_clicks_trap();
    }

    if (item_idx != ITEM_NONE) {
        OsrsClickAction action = osrs_item_click_action(item_idx);
        if (raw_osrs_id != 0 && raw_osrs_id != ITEM_DATABASE[item_idx].item_id) {
            osrs_inventory_clicks_trap();
        }
        return (OsrsInventoryClickResolution){
            .click_action = action,
            .consumable_kind = OSRS_CONSUMABLE_NONE,
            .dose_count = 0,
            .raw_osrs_id_after_drink = 0,
        };
    }

    if (raw_osrs_id == 0) {
        return (OsrsInventoryClickResolution){0};
    }

    OsrsConsumableClick consumable =
        osrs_consumable_click_lookup_raw_osrs_id(raw_osrs_id);
    uint16_t after_drink = 0;
    if (consumable.click_action == OSRS_CLICK_DRINK &&
        consumable.dose_count > 0) {
        after_drink = osrs_consumable_raw_osrs_id_after_drink(raw_osrs_id);
    }

    return (OsrsInventoryClickResolution){
        .click_action = consumable.click_action,
        .consumable_kind = consumable.consumable_kind,
        .dose_count = consumable.dose_count,
        .raw_osrs_id_after_drink = after_drink,
    };
}

#endif
