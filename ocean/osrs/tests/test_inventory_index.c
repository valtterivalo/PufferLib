#include <assert.h>
#include <stdio.h>
#include "../osrs_inventory.h"

int main(void) {
    OsrsInventoryCell cells[OSRS_INVENTORY_SIZE];
    for (int base = 0; base < OSRS_ITEM_CONTENT_COUNT; base++) {
        for (int slot = 0; slot < OSRS_INVENTORY_SIZE; slot++)
            cells[slot] = osrs_inventory_cell_from_content_code(
                (base + (slot / 2) * 17) % OSRS_ITEM_CONTENT_COUNT);
        OsrsInventoryIndex index = osrs_inventory_index(cells);
        for (int item = 0; item < NUM_ITEMS; item++) {
            int expected = 0;
            for (int slot = 0; slot < OSRS_INVENTORY_SIZE && !expected; slot++)
                if (osrs_inventory_cell_metadata(&cells[slot])->item_idx == item) expected = slot + 1;
            assert(index.item_slot_plus_one[item] == expected);
        }
        for (int kind = 0; kind < OSRS_CONSUMABLE_COUNT; kind++) {
            int expected = 0;
            for (int slot = 0; slot < OSRS_INVENTORY_SIZE && !expected; slot++)
                if (osrs_inventory_cell_metadata(&cells[slot])->consumable_kind == kind) expected = slot + 1;
            assert(index.consumable_slot_plus_one[kind] == expected);
        }
    }
    for (int slot = 0; slot < OSRS_INVENTORY_SIZE; slot++) cells[slot] = osrs_inventory_cell_empty();
    cells[0] = cells[1] = osrs_inventory_cell_from_item(ITEM_DHAROKS_GREATAXE);
    OsrsInventoryIndex before = osrs_inventory_index(cells);
    cells[0] = osrs_inventory_cell_empty();
    OsrsInventoryIndex after = osrs_inventory_index(cells);
    assert(before.item_slot_plus_one[ITEM_DHAROKS_GREATAXE] == 1);
    assert(after.item_slot_plus_one[ITEM_DHAROKS_GREATAXE] == 2);
    puts("Inventory index matches first-slot searches across all content identities");
}
