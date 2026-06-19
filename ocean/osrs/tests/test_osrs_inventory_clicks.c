/**
 * @file test_osrs_inventory_clicks.c
 * @brief Shared OSRS inventory click SDK tests.
 */

#include <stdio.h>

#include "ocean/osrs/osrs_inventory.h"
#include "ocean/osrs/osrs_inventory_clicks.h"

#define CHECK(label, cond) do { \
    if (!(cond)) { \
        fprintf(stderr, "FAIL: %s\n", label); \
        return 1; \
    } \
} while (0)

static int test_item_index_classification(void) {
    CHECK("rune crossbow item index equips",
        osrs_item_click_action(ITEM_RUNE_CROSSBOW) == OSRS_CLICK_EQUIP);
    CHECK("empty item index noops",
        osrs_item_click_action(ITEM_NONE) == OSRS_CLICK_NONE);
    CHECK("gear item has no consumable tag",
        osrs_item_click_consumable_kind(ITEM_RUNE_CROSSBOW) ==
            OSRS_CONSUMABLE_NONE);
    return 0;
}

static int test_raw_consumable_classification(void) {
    OsrsConsumableClick brew =
        osrs_consumable_click_lookup_raw_osrs_id(OSRS_RAW_ID_BREW_4);
    CHECK("sara brew drinks", brew.click_action == OSRS_CLICK_DRINK);
    CHECK("sara brew kind", brew.consumable_kind == OSRS_CONSUMABLE_BREW);
    CHECK("sara brew four-dose count", brew.dose_count == 4);

    OsrsConsumableClick shark =
        osrs_consumable_click_lookup_raw_osrs_id(OSRS_RAW_ID_SHARK);
    CHECK("shark eats", shark.click_action == OSRS_CLICK_EAT);
    CHECK("shark food kind", shark.consumable_kind == OSRS_CONSUMABLE_SHARK_FOOD);
    CHECK("shark has no dose", shark.dose_count == 0);

    OsrsConsumableClick unknown =
        osrs_consumable_click_lookup_raw_osrs_id(9999);
    CHECK("unknown raw id noops", unknown.click_action == OSRS_CLICK_NONE);
    CHECK("unknown raw id has no kind",
        unknown.consumable_kind == OSRS_CONSUMABLE_NONE);
    CHECK("unknown raw id has no dose", unknown.dose_count == 0);
    return 0;
}

static int test_dose_variants(void) {
    CHECK("brew four dose",
        osrs_consumable_click_lookup_raw_osrs_id(OSRS_RAW_ID_BREW_4).dose_count == 4);
    CHECK("brew one dose",
        osrs_consumable_click_lookup_raw_osrs_id(OSRS_RAW_ID_BREW_1).dose_count == 1);
    CHECK("stamina one dose",
        osrs_consumable_click_lookup_raw_osrs_id(OSRS_RAW_ID_STAMINA_1).dose_count == 1);
    CHECK("antivenom maps",
        osrs_consumable_click_lookup_raw_osrs_id(OSRS_RAW_ID_ANTIVENOM_4).consumable_kind ==
            OSRS_CONSUMABLE_ANTIVENOM);
    CHECK("prayer maps",
        osrs_consumable_click_lookup_raw_osrs_id(OSRS_RAW_ID_PRAYER_4).consumable_kind ==
            OSRS_CONSUMABLE_PRAYER_POTION);
    CHECK("bastion maps",
        osrs_consumable_click_lookup_raw_osrs_id(OSRS_RAW_ID_BASTION_4).consumable_kind ==
            OSRS_CONSUMABLE_BASTION);
    return 0;
}

static int test_dose_after_drink(void) {
    CHECK("four-dose count decrements to three",
        osrs_consumable_dose_count_after_drink(4) == 3);
    CHECK("one-dose count decrements to empty",
        osrs_consumable_dose_count_after_drink(1) == 0);
    CHECK("brew decrements raw id",
        osrs_consumable_raw_osrs_id_after_drink(OSRS_RAW_ID_BREW_4) ==
            OSRS_RAW_ID_BREW_3);
    CHECK("stamina decrements raw id",
        osrs_consumable_raw_osrs_id_after_drink(OSRS_RAW_ID_STAMINA_2) ==
            OSRS_RAW_ID_STAMINA_1);
    CHECK("guthix rest uses odd id family",
        osrs_consumable_raw_osrs_id_after_drink(4417) == 4419);
    return 0;
}

static int test_pure_click_interpreter(void) {
    OsrsInventoryClickResolution gear = osrs_inventory_click_interpret(
        ITEM_RUNE_CROSSBOW,
        ITEM_DATABASE[ITEM_RUNE_CROSSBOW].item_id,
        OSRS_CLICK_TICK_FIRST
    );
    CHECK("gear click resolves equip", gear.click_action == OSRS_CLICK_EQUIP);
    CHECK("gear click has no consumable kind",
        gear.consumable_kind == OSRS_CONSUMABLE_NONE);

    OsrsInventoryClickResolution brew = osrs_inventory_click_interpret(
        ITEM_NONE,
        OSRS_RAW_ID_BREW_4,
        OSRS_CLICK_TICK_FIRST
    );
    CHECK("brew click resolves drink", brew.click_action == OSRS_CLICK_DRINK);
    CHECK("brew interpreter kind", brew.consumable_kind == OSRS_CONSUMABLE_BREW);
    CHECK("brew interpreter dose", brew.dose_count == 4);
    CHECK("brew interpreter next raw id",
        brew.raw_osrs_id_after_drink == OSRS_RAW_ID_BREW_3);

    OsrsInventoryClickResolution shark = osrs_inventory_click_interpret(
        ITEM_NONE,
        OSRS_RAW_ID_SHARK,
        OSRS_CLICK_TICK_FIRST
    );
    CHECK("shark click resolves eat", shark.click_action == OSRS_CLICK_EAT);
    CHECK("shark interpreter kind",
        shark.consumable_kind == OSRS_CONSUMABLE_SHARK_FOOD);

    OsrsInventoryClickResolution duplicate = osrs_inventory_click_interpret(
        ITEM_NONE,
        OSRS_RAW_ID_BREW_4,
        OSRS_CLICK_TICK_DUPLICATE
    );
    CHECK("duplicate click noops", duplicate.click_action == OSRS_CLICK_NONE);
    return 0;
}

static int test_projected_view_raw_ids(void) {
    Player p = {0};
    osrs_player_inventory_clear(&p);
    p.inventory[3] = ITEM_RUNE_CROSSBOW;
    p.food_count = 1;
    p.brew_doses = 5;
    p.stamina_doses = 1;

    OsrsInventoryView view;
    osrs_inventory_view_build(&p, &view);

    CHECK("gear keeps physical slot", view.slots[3].item_idx == ITEM_RUNE_CROSSBOW);
    CHECK("gear raw id", view.slots[3].raw_osrs_id == ITEM_DATABASE[ITEM_RUNE_CROSSBOW].item_id);

    int food_slot = osrs_inventory_view_find_kind(&view, OSRS_INVENTORY_SLOT_FOOD);
    int brew_slot = osrs_inventory_view_find_kind(&view, OSRS_INVENTORY_SLOT_BREW);
    int stamina_slot = osrs_inventory_view_find_kind(&view, OSRS_INVENTORY_SLOT_STAMINA_POTION);
    CHECK("food projected", food_slot >= 0);
    CHECK("food raw id", view.slots[food_slot].raw_osrs_id == OSRS_RAW_ID_SHARK);
    CHECK("brew projected", brew_slot >= 0);
    CHECK("brew first vial is four-dose",
        view.slots[brew_slot].raw_osrs_id == OSRS_RAW_ID_BREW_4);
    CHECK("stamina projected", stamina_slot >= 0);
    CHECK("stamina raw id", view.slots[stamina_slot].raw_osrs_id == OSRS_RAW_ID_STAMINA_1);
    return 0;
}

static int test_event_collection_left_to_right_dedupes(void) {
    Player p = {0};
    osrs_player_inventory_clear(&p);
    p.inventory[0] = ITEM_RUNE_CROSSBOW;
    p.food_count = 1;

    OsrsInventoryView view;
    osrs_inventory_view_build(&p, &view);

    int food_slot = osrs_inventory_view_find_kind(&view, OSRS_INVENTORY_SLOT_FOOD);
    int actions[] = {1, 1, food_slot + 1, food_slot + 1, 0};
    OsrsInventoryClickEvent events[OSRS_INVENTORY_SIZE];
    int count = osrs_inventory_click_collect_events(
        &view, actions, 5, events, OSRS_INVENTORY_SIZE);

    CHECK("duplicate slots no-op", count == 2);
    CHECK("first event is gear", events[0].resolution.click_action == OSRS_CLICK_EQUIP);
    CHECK("second event is food", events[1].resolution.consumable_kind ==
        OSRS_CONSUMABLE_SHARK_FOOD);
    return 0;
}

int main(void) {
    if (test_item_index_classification()) return 1;
    if (test_raw_consumable_classification()) return 1;
    if (test_dose_variants()) return 1;
    if (test_dose_after_drink()) return 1;
    if (test_pure_click_interpreter()) return 1;
    if (test_projected_view_raw_ids()) return 1;
    if (test_event_collection_left_to_right_dedupes()) return 1;

    printf("test_osrs_inventory_clicks: OK\n");
    return 0;
}
