/**
 * @file osrs_human_commands.h
 * @brief Shared human command queue parsing for OSRS encounters.
 */

#ifndef OSRS_HUMAN_COMMANDS_H
#define OSRS_HUMAN_COMMANDS_H

#include <stdio.h>
#include <stdlib.h>

#include "osrs_human_input_types.h"
#include "osrs_types.h"
#include "osrs_items.h"

typedef struct {
    int has_walk;
    int walk_x;
    int walk_y;
    int has_attack_target;
    int target_slot;
    int has_spell_target;
    int spell_target_slot;
    int spell;
    int overhead_prayer;
    int offensive_prayer;
    int food;
    int karambwan;
    int potion;
    int vengeance;
    int spec_toggle;
    int path_command_seen;
    uint8_t queued_weapon;
} OsrsHumanCommandFrame;

static inline OsrsHumanCommandFrame osrs_human_command_frame_init(uint8_t weapon) {
    return (OsrsHumanCommandFrame){
        .has_walk = 0,
        .walk_x = -1,
        .walk_y = -1,
        .has_attack_target = 0,
        .target_slot = -1,
        .has_spell_target = 0,
        .spell_target_slot = -1,
        .spell = 0,
        .overhead_prayer = -1,
        .offensive_prayer = -1,
        .food = 0,
        .karambwan = 0,
        .potion = 0,
        .vengeance = 0,
        .spec_toggle = 0,
        .path_command_seen = 0,
        .queued_weapon = weapon,
    };
}

static inline void osrs_human_command_frame_apply_legacy_pending(
    OsrsHumanCommandFrame* frame,
    const HumanInput* hi
) {
    if (!frame->path_command_seen && hi->pending_move_x >= 0 && hi->pending_move_y >= 0) {
        frame->has_walk = 1;
        frame->walk_x = hi->pending_move_x;
        frame->walk_y = hi->pending_move_y;
        frame->path_command_seen = 1;
    }
    if (!frame->path_command_seen && hi->pending_attack) {
        if (is_spell_attack_action(hi->pending_spell)) {
            frame->has_spell_target = 1;
            frame->spell_target_slot = hi->pending_target_idx;
            frame->spell = hi->pending_spell;
        } else {
            frame->has_attack_target = 1;
            frame->target_slot = hi->pending_target_idx;
        }
        frame->path_command_seen = 1;
    }
    if (frame->overhead_prayer < 0 && hi->pending_prayer >= 0)
        frame->overhead_prayer = hi->pending_prayer;
    if (frame->offensive_prayer < 0 && hi->pending_offensive_prayer >= 0)
        frame->offensive_prayer = hi->pending_offensive_prayer;
    if (!frame->food && hi->pending_food)
        frame->food = 1;
    if (!frame->karambwan && hi->pending_karambwan)
        frame->karambwan = 1;
    if (!frame->potion && hi->pending_potion > 0)
        frame->potion = hi->pending_potion;
    if (!frame->vengeance && hi->pending_veng)
        frame->vengeance = 1;
    if (!frame->spec_toggle && hi->pending_spec)
        frame->spec_toggle = 1;
}

static inline OsrsHumanCommandFrame osrs_human_command_frame_from_input(
    const HumanInput* hi,
    uint8_t current_weapon
) {
    OsrsHumanCommandFrame frame = osrs_human_command_frame_init(current_weapon);

    for (int i = 0; i < hi->commands.count; i++) {
        const HumanCommand* cmd = &hi->commands.items[i];
        if (cmd->kind == HUMAN_COMMAND_EQUIP_INVENTORY_ITEM &&
                cmd->gear_slot == GEAR_SLOT_WEAPON &&
                cmd->item_db_idx >= 0 && cmd->item_db_idx < NUM_ITEMS) {
            frame.queued_weapon = (uint8_t)cmd->item_db_idx;
        }

        switch (cmd->kind) {
            case HUMAN_COMMAND_WALK:
                frame.has_walk = 1;
                frame.walk_x = cmd->world_x;
                frame.walk_y = cmd->world_y;
                frame.has_attack_target = 0;
                frame.has_spell_target = 0;
                frame.path_command_seen = 1;
                break;
            case HUMAN_COMMAND_ATTACK_NPC:
                frame.has_attack_target = 1;
                frame.target_slot = cmd->npc_slot;
                frame.has_spell_target = 0;
                frame.path_command_seen = 1;
                break;
            case HUMAN_COMMAND_SPELL_TARGET:
                frame.has_spell_target = 1;
                frame.spell_target_slot = cmd->npc_slot;
                frame.spell = cmd->spell;
                frame.has_attack_target = 0;
                frame.path_command_seen = 1;
                break;
            case HUMAN_COMMAND_OVERHEAD_PRAYER:
                frame.overhead_prayer = cmd->overhead_prayer;
                break;
            case HUMAN_COMMAND_OFFENSIVE_PRAYER:
                frame.offensive_prayer = cmd->offensive_prayer;
                break;
            case HUMAN_COMMAND_EAT:
                if (cmd->food == 1)
                    frame.karambwan = 1;
                else
                    frame.food = 1;
                break;
            case HUMAN_COMMAND_DRINK:
                frame.potion = cmd->potion;
                break;
            case HUMAN_COMMAND_SPEC_TOGGLE:
                frame.spec_toggle = 1;
                break;
            case HUMAN_COMMAND_EQUIP_INVENTORY_ITEM:
            case HUMAN_COMMAND_FIGHT_STYLE:
            case HUMAN_COMMAND_SET_AUTOCAST:
            case HUMAN_COMMAND_ITEM_ON_ITEM:
            case HUMAN_COMMAND_ITEM_ON_WIDGET:
            case HUMAN_COMMAND_SPELL_ON_WIDGET:
            case HUMAN_COMMAND_NONE:
                break;
            default:
                fprintf(stderr, "osrs_human_command_frame_from_input: bad command kind %d\n",
                    (int)cmd->kind);
                abort();
        }
    }

    osrs_human_command_frame_apply_legacy_pending(&frame, hi);
    return frame;
}

#endif
