/**
 * @file test_osrs_inventory_clicks.c
 * @brief Tests the pure shared inventory-click SDK.
 *
 * BUILD:
 *   cc -std=c11 -O0 -g -I. -o /tmp/test_osrs_inventory_clicks \
 *       ocean/osrs/tests/test_osrs_inventory_clicks.c -lm
 *   /tmp/test_osrs_inventory_clicks
 */

#include <stdio.h>

#include "ocean/osrs/osrs_inventory_clicks.h"

#define CHECK(label, cond) do { \
    if (!(cond)) { \
        fprintf(stderr, "FAIL: %s\n", label); \
        return 1; \
    } \
} while (0)

static int test_item_index_classification(void) {
    CHECK("venator bow item index equips",
          osrs_item_click_action(ITEM_VENATOR_BOW) == OSRS_CLICK_EQUIP);
    CHECK("empty item index noops",
          osrs_item_click_action(ITEM_NONE) == OSRS_CLICK_NONE);
    CHECK("gear item has no consumable tag",
          osrs_item_click_consumable_kind(ITEM_VENATOR_BOW) ==
              OSRS_CONSUMABLE_NONE);
    return 0;
}

static int test_raw_consumable_classification(void) {
    OsrsConsumableClick brew =
        osrs_consumable_click_lookup_raw_osrs_id(6685);
    CHECK("sara brew drinks", brew.click_action == OSRS_CLICK_DRINK);
    CHECK("sara brew kind", brew.consumable_kind == OSRS_CONSUMABLE_BREW);
    CHECK("sara brew four-dose count", brew.dose_count == 4);

    OsrsConsumableClick shark =
        osrs_consumable_click_lookup_raw_osrs_id(385);
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

static int test_sara_brew_dose_variants(void) {
    OsrsConsumableClick brew4 =
        osrs_consumable_click_lookup_raw_osrs_id(6685);
    OsrsConsumableClick brew3 =
        osrs_consumable_click_lookup_raw_osrs_id(6687);
    OsrsConsumableClick brew2 =
        osrs_consumable_click_lookup_raw_osrs_id(6689);
    OsrsConsumableClick brew1 =
        osrs_consumable_click_lookup_raw_osrs_id(6691);

    CHECK("6685 is four-dose", brew4.dose_count == 4);
    CHECK("6687 is three-dose", brew3.dose_count == 3);
    CHECK("6689 is two-dose", brew2.dose_count == 2);
    CHECK("6691 is one-dose", brew1.dose_count == 1);
    return 0;
}

static int test_dose_after_drink(void) {
    CHECK("four-dose count decrements to three",
          osrs_consumable_dose_count_after_drink(4) == 3);
    CHECK("one-dose count decrements to empty",
          osrs_consumable_dose_count_after_drink(1) == 0);
    CHECK("6685 decrements to 6687",
          osrs_consumable_raw_osrs_id_after_drink(6685) == 6687);
    CHECK("6687 decrements to 6689",
          osrs_consumable_raw_osrs_id_after_drink(6687) == 6689);
    CHECK("6691 decrements to empty",
          osrs_consumable_raw_osrs_id_after_drink(6691) == 0);
    CHECK("guthix rest uses odd id family",
          osrs_consumable_raw_osrs_id_after_drink(4417) == 4419);
    return 0;
}

static int test_pure_click_interpreter(void) {
    OsrsInventoryClickResolution gear = osrs_inventory_click_interpret(
        ITEM_VENATOR_BOW,
        27610,
        OSRS_CLICK_TICK_FIRST
    );
    CHECK("gear click resolves equip", gear.click_action == OSRS_CLICK_EQUIP);
    CHECK("gear click has no consumable kind",
          gear.consumable_kind == OSRS_CONSUMABLE_NONE);

    OsrsInventoryClickResolution brew = osrs_inventory_click_interpret(
        ITEM_NONE,
        6685,
        OSRS_CLICK_TICK_FIRST
    );
    CHECK("brew click resolves drink", brew.click_action == OSRS_CLICK_DRINK);
    CHECK("brew interpreter kind", brew.consumable_kind == OSRS_CONSUMABLE_BREW);
    CHECK("brew interpreter dose", brew.dose_count == 4);
    CHECK("brew interpreter next raw id", brew.raw_osrs_id_after_drink == 6687);

    OsrsInventoryClickResolution shark = osrs_inventory_click_interpret(
        ITEM_NONE,
        385,
        OSRS_CLICK_TICK_FIRST
    );
    CHECK("shark click resolves eat", shark.click_action == OSRS_CLICK_EAT);
    CHECK("shark interpreter kind",
          shark.consumable_kind == OSRS_CONSUMABLE_SHARK_FOOD);

    OsrsInventoryClickResolution duplicate = osrs_inventory_click_interpret(
        ITEM_NONE,
        6685,
        OSRS_CLICK_TICK_DUPLICATE
    );
    CHECK("duplicate click noops", duplicate.click_action == OSRS_CLICK_NONE);
    return 0;
}

int main(void) {
    if (test_item_index_classification()) return 1;
    if (test_raw_consumable_classification()) return 1;
    if (test_sara_brew_dose_variants()) return 1;
    if (test_dose_after_drink()) return 1;
    if (test_pure_click_interpreter()) return 1;

    printf("test_osrs_inventory_clicks: OK\n");
    return 0;
}
