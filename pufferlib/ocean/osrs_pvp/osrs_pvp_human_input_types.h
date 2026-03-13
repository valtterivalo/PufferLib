/**
 * @file osrs_pvp_human_input_types.h
 * @brief HumanInput struct and CursorMode enum — separated from human_input.h
 *        to break circular include dependency (gui.h needs HumanInput, but
 *        human_input.h needs gui.h for prayer/spell grid constants).
 */

#ifndef OSRS_PVP_HUMAN_INPUT_TYPES_H
#define OSRS_PVP_HUMAN_INPUT_TYPES_H

typedef enum {
    CURSOR_NORMAL = 0,
    CURSOR_SPELL_TARGET,   /* clicked a combat spell, waiting for target click */
} CursorMode;

typedef struct HumanInput {
    int enabled;                /* H key toggle: 1 = human controls active */

    /* semantic action staging (set by clicks, consumed at tick boundary) */
    int pending_move_x, pending_move_y;   /* world tile coords, -1 = none */
    int pending_attack;                    /* 1 = attack target entity */
    int pending_prayer;                    /* OverheadAction value, -1 = no change */
    int pending_offensive_prayer;          /* 0=none, 1=piety, 2=rigour, 3=augury, -1=no change */
    int pending_food;                      /* 1 = eat food */
    int pending_karambwan;                 /* 1 = eat karambwan */
    int pending_potion;                    /* PotionAction value, 0 = none */
    int pending_veng;                      /* 1 = cast vengeance */
    int pending_spec;                      /* 1 = use special attack */
    int pending_spell;                     /* 0=none, ATTACK_ICE or ATTACK_BLOOD */

    CursorMode cursor_mode;
    int selected_spell;                    /* ATTACK_ICE or ATTACK_BLOOD for targeting */

    /* visual feedback: click cross at screen-space position (like real OSRS client) */
    int click_screen_x, click_screen_y;    /* screen pixel where click occurred */
    int click_cross_timer;                 /* counts up from 0, animation progresses over time */
    int click_cross_active;                /* 1 = cross is visible */
    int click_is_attack;                   /* 1 = red cross (attack), 0 = yellow cross (move) */
} HumanInput;

#endif /* OSRS_PVP_HUMAN_INPUT_TYPES_H */
