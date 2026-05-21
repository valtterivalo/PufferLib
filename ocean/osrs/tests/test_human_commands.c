/**
 * @file test_human_commands.c
 * @brief tests for human command queue staging used by encounter human mode
 *
 * BUILD:
 *   cc -std=c11 -O0 -g -I. -o /tmp/test_human_commands \
 *       ocean/osrs/tests/test_human_commands.c -lm
 *   /tmp/test_human_commands
 */

#include <stdio.h>
#include <string.h>

#include "ocean/osrs/osrs_types.h"
#include "ocean/osrs/osrs_items.h"
#include "ocean/osrs/osrs_human_input_types.h"

enum {
    TEST_GUI_SPELL_BLOOD_BARRAGE = 14,
    TEST_GUI_SPELL_ICE_BARRAGE = 15,
};

static int tests_run = 0;
static int tests_passed = 0;
static int tests_failed = 0;

#define ASSERT_INT_EQ(label, actual, expected) do { \
    tests_run++; \
    int _actual = (actual); \
    int _expected = (expected); \
    if (_actual == _expected) { \
        tests_passed++; \
    } else { \
        tests_failed++; \
        printf("  FAIL: %s: got %d, expected %d\n", \
            (label), _actual, _expected); \
    } \
} while (0)

static void test_command_queue_preserves_order(void) {
    printf("--- human command queue preserves order ---\n");

    HumanInput input;
    human_input_init(&input);

    human_input_queue_walk(&input, 10, 20);
    human_input_queue_attack_npc(&input, 7);
    human_input_queue_drink(&input, POTION_BASTION, 3);

    ASSERT_INT_EQ("queue count", input.commands.count, 3);
    ASSERT_INT_EQ("first kind", input.commands.items[0].kind, HUMAN_COMMAND_WALK);
    ASSERT_INT_EQ("first x", input.commands.items[0].world_x, 10);
    ASSERT_INT_EQ("second kind", input.commands.items[1].kind, HUMAN_COMMAND_ATTACK_NPC);
    ASSERT_INT_EQ("second npc slot", input.commands.items[1].npc_slot, 7);
    ASSERT_INT_EQ("third kind", input.commands.items[2].kind, HUMAN_COMMAND_DRINK);
    ASSERT_INT_EQ("third potion", input.commands.items[2].potion, POTION_BASTION);

    human_input_destroy(&input);
}

static void test_command_queue_grows_without_silent_cap(void) {
    printf("--- human command queue grows without silent cap ---\n");

    HumanInput input;
    human_input_init(&input);

    for (int i = 0; i < 40; i++) {
        human_input_queue_equip_inventory_item(&input, i, ITEM_TOXIC_BLOWPIPE, GEAR_SLOT_WEAPON);
    }

    ASSERT_INT_EQ("queue count after growth", input.commands.count, 40);
    ASSERT_INT_EQ("first slot", input.commands.items[0].inventory_slot, 0);
    ASSERT_INT_EQ("last slot", input.commands.items[39].inventory_slot, 39);

    human_input_destroy(&input);
}

static void test_command_queue_clear_drains_commands(void) {
    printf("--- human command queue clear drains commands ---\n");

    HumanInput input;
    human_input_init(&input);

    human_input_queue_walk(&input, 1, 2);
    human_input_queue_spec_toggle(&input);
    human_input_clear_commands(&input);

    ASSERT_INT_EQ("queue count after clear", input.commands.count, 0);
    ASSERT_INT_EQ("queue capacity retained", input.commands.capacity > 0, 1);

    human_input_destroy(&input);
}

static void test_ui_intent_selected_item_queues_item_targets(void) {
    printf("--- ui intent selected item queues item targets ---\n");

    HumanInput input;
    human_input_init(&input);

    human_input_apply_ui_intent(&input,
        osrs_ui_intent_select_item(4, ITEM_TOXIC_BLOWPIPE, 12926));
    ASSERT_INT_EQ("selected item cursor", input.cursor_mode, CURSOR_ITEM_TARGET);
    ASSERT_INT_EQ("selected source slot", input.selected_item_inventory_slot, 4);
    ASSERT_INT_EQ("selected item db idx", input.selected_item_db_idx, ITEM_TOXIC_BLOWPIPE);

    human_input_apply_ui_intent(&input,
        osrs_ui_intent_item_on_item(4, 9, ITEM_TOXIC_BLOWPIPE, 12926));
    ASSERT_INT_EQ("item-on-item clears cursor", input.cursor_mode, CURSOR_NORMAL);
    ASSERT_INT_EQ("item-on-item command count", input.commands.count, 1);
    ASSERT_INT_EQ("item-on-item kind",
        input.commands.items[0].kind, HUMAN_COMMAND_ITEM_ON_ITEM);
    ASSERT_INT_EQ("item-on-item source slot", input.commands.items[0].inventory_slot, 4);
    ASSERT_INT_EQ("item-on-item target slot", input.commands.items[0].target_inventory_slot, 9);
    ASSERT_INT_EQ("item-on-item osrs id", input.commands.items[0].item_osrs_id, 12926);

    human_input_apply_ui_intent(&input,
        osrs_ui_intent_select_item(2, ITEM_KODAI_WAND, 21006));
    human_input_apply_ui_intent(&input,
        osrs_ui_intent_item_on_widget(2, ITEM_KODAI_WAND, 21006,
            osrs_ui_intent_widget_component_id(387, 15)));

    ASSERT_INT_EQ("item-on-widget command count", input.commands.count, 2);
    ASSERT_INT_EQ("item-on-widget kind",
        input.commands.items[1].kind, HUMAN_COMMAND_ITEM_ON_WIDGET);
    ASSERT_INT_EQ("item-on-widget source slot", input.commands.items[1].inventory_slot, 2);
    ASSERT_INT_EQ("item-on-widget osrs id", input.commands.items[1].item_osrs_id, 21006);
    ASSERT_INT_EQ("item-on-widget component group",
        osrs_ui_intent_widget_group_id(input.commands.items[1].widget_component_id), 387);
    ASSERT_INT_EQ("item-on-widget component child",
        osrs_ui_intent_widget_child_id(input.commands.items[1].widget_component_id), 15);

    human_input_destroy(&input);
}

static void test_ui_intent_selected_spell_queues_target_and_widget(void) {
    printf("--- ui intent selected spell queues target and widget ---\n");

    HumanInput input;
    human_input_init(&input);

    human_input_apply_ui_intent(&input,
        osrs_ui_intent_select_spell(ATTACK_ICE, TEST_GUI_SPELL_ICE_BARRAGE));
    ASSERT_INT_EQ("selected spell cursor", input.cursor_mode, CURSOR_SPELL_TARGET);
    ASSERT_INT_EQ("selected spell attack", input.selected_spell, ATTACK_ICE);
    ASSERT_INT_EQ("selected spell gui idx", input.selected_spell_gui_idx, TEST_GUI_SPELL_ICE_BARRAGE);

    human_input_apply_ui_intent(&input,
        osrs_ui_intent_spell_on_target(ATTACK_ICE, TEST_GUI_SPELL_ICE_BARRAGE, 6));
    ASSERT_INT_EQ("spell-on-target clears cursor", input.cursor_mode, CURSOR_NORMAL);
    ASSERT_INT_EQ("spell-on-target pending spell", input.pending_spell, ATTACK_ICE);
    ASSERT_INT_EQ("spell-on-target pending target", input.pending_target_idx, 6);
    ASSERT_INT_EQ("spell-on-target command count", input.commands.count, 1);
    ASSERT_INT_EQ("spell-on-target kind",
        input.commands.items[0].kind, HUMAN_COMMAND_SPELL_TARGET);
    ASSERT_INT_EQ("spell-on-target spell", input.commands.items[0].spell, ATTACK_ICE);
    ASSERT_INT_EQ("spell-on-target npc", input.commands.items[0].npc_slot, 6);

    human_input_apply_ui_intent(&input,
        osrs_ui_intent_select_spell(ATTACK_BLOOD, TEST_GUI_SPELL_BLOOD_BARRAGE));
    human_input_apply_ui_intent(&input,
        osrs_ui_intent_spell_on_widget(ATTACK_BLOOD, TEST_GUI_SPELL_BLOOD_BARRAGE,
            osrs_ui_intent_widget_component_id(218, 47)));

    ASSERT_INT_EQ("spell-on-widget command count", input.commands.count, 2);
    ASSERT_INT_EQ("spell-on-widget kind",
        input.commands.items[1].kind, HUMAN_COMMAND_SPELL_ON_WIDGET);
    ASSERT_INT_EQ("spell-on-widget spell", input.commands.items[1].spell, ATTACK_BLOOD);
    ASSERT_INT_EQ("spell-on-widget gui idx",
        input.commands.items[1].spell_gui_idx, TEST_GUI_SPELL_BLOOD_BARRAGE);
    ASSERT_INT_EQ("spell-on-widget component group",
        osrs_ui_intent_widget_group_id(input.commands.items[1].widget_component_id), 218);
    ASSERT_INT_EQ("spell-on-widget component child",
        osrs_ui_intent_widget_child_id(input.commands.items[1].widget_component_id), 47);

    human_input_destroy(&input);
}

int main(void) {
    test_command_queue_preserves_order();
    test_command_queue_grows_without_silent_cap();
    test_command_queue_clear_drains_commands();
    test_ui_intent_selected_item_queues_item_targets();
    test_ui_intent_selected_spell_queues_target_and_widget();

    printf("\n%d/%d tests passed", tests_passed, tests_run);
    if (tests_failed > 0) {
        printf(" (%d failed)\n", tests_failed);
        return 1;
    }
    printf("\n");
    return 0;
}
