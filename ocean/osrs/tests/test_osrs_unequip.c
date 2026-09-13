#include <assert.h>
#include <stdio.h>
#include "../osrs_inventory_actions.h"

static Player player;

static void reset(void) {
    memset(&player, 0, sizeof(player));
    memset(player.equipped, ITEM_NONE, sizeof(player.equipped));
    for (int i = 0; i < OSRS_INVENTORY_SIZE; i++)
        player.inventory_cells[i] = osrs_inventory_cell_empty();
    player.base_hitpoints = 99;
    player.current_hitpoints = 10;
    player.equipped[GEAR_SLOT_HEAD] = ITEM_DHAROKS_HELM;
    player.equipped[GEAR_SLOT_BODY] = ITEM_DHAROKS_PLATEBODY;
    player.equipped[GEAR_SLOT_LEGS] = ITEM_DHAROKS_PLATELEGS;
    player.equipped[GEAR_SLOT_WEAPON] = ITEM_DHAROKS_GREATAXE;
    player.equipped[GEAR_SLOT_HANDS] = ITEM_FEROCIOUS_GLOVES;
    player.equipped[GEAR_SLOT_RING] = ITEM_RING_OF_RECOIL;
    player.attack_timer = 5;
    player.attack_timer_uncapped = 5;
    player.has_attack_timer = 1;
    osrs_refresh_player_equipment(&player);
}

static void count_items(int* counts) {
    memset(counts, 0, NUM_ITEMS * sizeof(int));
    for (int i = 0; i < NUM_GEAR_SLOTS; i++)
        if (player.equipped[i] != ITEM_NONE) counts[player.equipped[i]]++;
    for (int i = 0; i < OSRS_INVENTORY_SIZE; i++) {
        int item = osrs_inventory_cell_item_index(&player.inventory_cells[i]);
        if (item != ITEM_NONE) counts[item]++;
    }
}

static int prepared_max_hit(void) {
    return osrs_prepare_attack_effects(&player.equipment_effect_profile,
        &player.item_effect_state, player.equipped[GEAR_SLOT_WEAPON], ATTACK_STYLE_MELEE,
        OSRS_MAGIC_ATTACK_NONE, osrs_target_ref_none(), 1, 100, 40,
        osrs_target_effect_context_none(), player.current_hitpoints, player.base_hitpoints).max_hit;
}

static void test_sequential_capacity(void) {
    reset();
    const int free_cells[] = {2, 11, 25};
    for (int i = 0; i < OSRS_INVENTORY_SIZE; i++)
        player.inventory_cells[i] = osrs_inventory_cell_from_item(ITEM_VOIDWAKER);
    for (int i = 0; i < 3; i++) player.inventory_cells[free_cells[i]] = osrs_inventory_cell_empty();
    int before[NUM_ITEMS], after[NUM_ITEMS];
    count_items(before);
    const int slots[] = {GEAR_SLOT_HEAD, GEAR_SLOT_BODY, GEAR_SLOT_LEGS, GEAR_SLOT_HANDS, GEAR_SLOT_WEAPON};
    for (int i = 0; i < 5; i++) {
        int item = player.equipped[slots[i]];
        Player snapshot = player;
        OsrsUnequipResult result = osrs_unequip_to_inventory(&player, player.inventory_cells, slots[i]);
        if (i < 3) {
            assert(result == OSRS_UNEQUIP_SUCCESS);
            assert(player.equipped[slots[i]] == ITEM_NONE);
            assert(osrs_inventory_cell_item_index(&player.inventory_cells[free_cells[i]]) == item);
        } else {
            assert(result == OSRS_UNEQUIP_FULL);
            assert(memcmp(&snapshot, &player, sizeof(player)) == 0);
        }
        count_items(after);
        assert(memcmp(before, after, sizeof(before)) == 0);
    }
    Player snapshot = player;
    assert(osrs_unequip_to_inventory(&player, player.inventory_cells, GEAR_SLOT_HEAD) == OSRS_UNEQUIP_EMPTY);
    assert(memcmp(&snapshot, &player, sizeof(player)) == 0);
}

static void test_stats_dharok_and_reequip(void) {
    reset();
    int before[NUM_ITEMS], after[NUM_ITEMS];
    count_items(before);
    int defence = player.slot_cached_bonuses.stab_defence;
    int boosted = prepared_max_hit();
    assert(boosted > 40);
    assert(osrs_unequip_to_inventory(&player, player.inventory_cells, GEAR_SLOT_HEAD) == OSRS_UNEQUIP_SUCCESS);
    assert(player.slot_cached_bonuses.stab_defence < defence);
    assert(player.equipment_effect_profile.dharok_piece_count == 3);
    assert(prepared_max_hit() == 40);
    assert(player.attack_timer == 5 && player.attack_timer_uncapped == 5 && player.has_attack_timer);
    assert(osrs_equip_from_cell(&player, player.inventory_cells, 0) == GEAR_SLOT_HEAD);
    assert(player.equipment_effect_profile.dharok_piece_count == 4);
    assert(prepared_max_hit() == boosted);
    assert(player.slot_cached_bonuses.stab_defence == defence);
    count_items(after);
    assert(memcmp(before, after, sizeof(before)) == 0);
}

static void test_weapon_and_recoil_state(void) {
    reset();
    osrs_consume_recoil_charges(&player, 17);
    assert(player.item_effect_state.recoil_damage_used == 17);
    player.spec_armed = 1;
    assert(osrs_unequip_to_inventory(&player, player.inventory_cells, GEAR_SLOT_RING) == OSRS_UNEQUIP_SUCCESS);
    assert(player.item_effect_state.recoil_damage_used == 17);
    assert(player.spec_armed == 1);
    assert(osrs_equip_from_cell(&player, player.inventory_cells, 0) == GEAR_SLOT_RING);
    assert(player.item_effect_state.recoil_charges == 23);
    assert(osrs_unequip_to_inventory(&player, player.inventory_cells, GEAR_SLOT_WEAPON) == OSRS_UNEQUIP_SUCCESS);
    assert(player.spec_armed == 0);
    assert(player.attack_timer == 5 && player.has_attack_timer);
    assert(osrs_equip_from_cell(&player, player.inventory_cells, 0) == GEAR_SLOT_WEAPON);
    assert(player.attack_timer == 5 && player.attack_timer_uncapped == 5);
    assert(player.item_effect_state.recoil_damage_used == 17);
    assert(player.item_effect_state.recoil_charges == 23);
}

int main(void) {
    test_sequential_capacity();
    test_stats_dharok_and_reequip();
    test_weapon_and_recoil_state();
    puts("Shared unequip contracts passed");
}
