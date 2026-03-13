/**
 * @file osrs_pvp_human_input.h
 * @brief Interactive human control for the visual debug viewer.
 *
 * Collects mouse/keyboard input as semantic intents between render frames,
 * then translates them to encounter-specific action arrays at tick boundary.
 * Toggle human control with H key. Works across PvP and encounter modes.
 *
 * Architecture: clicks at 60Hz → HumanInput staging buffer → per-encounter
 * translator at tick rate → int[] action array fed to step().
 */

#ifndef OSRS_PVP_HUMAN_INPUT_H
#define OSRS_PVP_HUMAN_INPUT_H

#include "osrs_pvp_types.h"
#include "osrs_pvp_human_input_types.h"
#include "osrs_encounter.h"

/* forward declare — full struct lives in osrs_pvp_render.h */
struct RenderClient;

/* ======================================================================== */
/* init / reset                                                              */
/* ======================================================================== */

static void human_input_init(HumanInput* hi) {
    memset(hi, 0, sizeof(*hi));
    hi->pending_move_x = -1;
    hi->pending_move_y = -1;
    hi->pending_prayer = -1;
    hi->pending_offensive_prayer = -1;
    hi->click_cross_active = 0;
}

/** Clear pending actions after they've been consumed at tick boundary.
    Movement is NOT cleared here — it persists until the player reaches the
    destination or a new click overrides it. Use human_input_clear_move()
    for that. */
static void human_input_clear_pending(HumanInput* hi) {
    /* pending_move_x/y intentionally NOT cleared — movement is persistent */
    hi->pending_attack = 0;
    hi->pending_prayer = -1;
    hi->pending_offensive_prayer = -1;
    hi->pending_food = 0;
    hi->pending_karambwan = 0;
    hi->pending_potion = 0;
    hi->pending_veng = 0;
    hi->pending_spec = 0;
    hi->pending_spell = 0;
    /* don't clear cursor_mode or selected_spell — those persist until cancelled */
    /* don't clear click_tile — visual feedback fades on its own */
}

/** Clear persistent movement destination. Call when player reaches target tile. */
static void human_input_clear_move(HumanInput* hi) {
    hi->pending_move_x = -1;
    hi->pending_move_y = -1;
}

/* ======================================================================== */
/* screen-to-world conversion                                                */
/* ======================================================================== */

/** Convert screen X to world tile X (inverse of render_world_to_screen_x_rc).
    tile_size = RENDER_TILE_SIZE (passed to avoid header ordering issues). */
static inline int human_screen_to_world_x(int screen_x, int arena_base_x,
                                           int tile_size) {
    return screen_x / tile_size + arena_base_x;
}

/** Convert screen Y to world tile Y (inverse of render_world_to_screen_y_rc).
    OSRS Y increases north, screen Y increases down. */
static inline int human_screen_to_world_y(int screen_y, int arena_base_y,
                                           int arena_height, int header_h,
                                           int tile_size) {
    int flipped = (screen_y - header_h) / tile_size;
    return arena_base_y + (arena_height - 1) - flipped;
}

/* ======================================================================== */
/* click handlers — set semantic intents on HumanInput                       */
/* ======================================================================== */

/** Check if world tile (wx,wy) is within an NPC's bounding box.
    OSRS NPCs occupy npc_size x npc_size tiles anchored at (x,y) as southwest corner.
    Players have npc_size 0 or 1, occupying just their tile. */
static int human_tile_hits_entity(Player* ent, int wx, int wy) {
    int size = ent->npc_size > 1 ? ent->npc_size : 1;
    return wx >= ent->x && wx < ent->x + size &&
           wy >= ent->y && wy < ent->y + size;
}

/** Set click cross at screen position (2D overlay, like real OSRS client). */
static void human_set_click_cross(HumanInput* hi, int screen_x, int screen_y, int is_attack) {
    hi->click_screen_x = screen_x;
    hi->click_screen_y = screen_y;
    hi->click_cross_timer = 0;
    hi->click_cross_active = 1;
    hi->click_is_attack = is_attack;
}

/** Process a world-tile click: attack entity if hit, otherwise move.
    screen_x/y is the raw mouse position for the click cross overlay. */
static void human_process_tile_click(HumanInput* hi,
                                      int wx, int wy,
                                      int screen_x, int screen_y,
                                      Player** entities, int entity_count,
                                      int gui_entity_idx) {
    /* check if an attackable entity occupies this tile (bounding box) */
    for (int i = 0; i < entity_count; i++) {
        if (i == gui_entity_idx) continue;  /* can't attack self */
        if (!entities[i]->npc_visible && entities[i]->entity_type == ENTITY_NPC) continue;
        if (human_tile_hits_entity(entities[i], wx, wy)) {
            hi->pending_attack = 1;
            /* attack cancels movement — server stops walking to old dest
               and auto-walks toward target instead (OSRS server behavior) */
            hi->pending_move_x = -1;
            hi->pending_move_y = -1;
            if (hi->cursor_mode == CURSOR_SPELL_TARGET) {
                hi->pending_spell = hi->selected_spell;
                hi->cursor_mode = CURSOR_NORMAL;
            }
            human_set_click_cross(hi, screen_x, screen_y, 1);
            return;
        }
    }

    /* no entity — movement click */
    if (hi->cursor_mode == CURSOR_SPELL_TARGET) {
        hi->cursor_mode = CURSOR_NORMAL;
        return;
    }

    hi->pending_move_x = wx;
    hi->pending_move_y = wy;
    human_set_click_cross(hi, screen_x, screen_y, 0);
}

/** Handle ground/entity click in 2D grid mode.
    tile_size and header_h are RENDER_TILE_SIZE/RENDER_HEADER_HEIGHT. */
static void human_handle_ground_click(HumanInput* hi,
                                       int mouse_x, int mouse_y,
                                       int arena_base_x, int arena_base_y,
                                       int arena_width, int arena_height,
                                       Player** entities, int entity_count,
                                       int gui_entity_idx,
                                       int tile_size, int header_h) {
    if (mouse_y < header_h) return;
    int grid_pixel_w = arena_width * tile_size;
    int grid_pixel_h = arena_height * tile_size;
    if (mouse_x < 0 || mouse_x >= grid_pixel_w) return;
    if (mouse_y >= header_h + grid_pixel_h) return;

    int wx = human_screen_to_world_x(mouse_x, arena_base_x, tile_size);
    int wy = human_screen_to_world_y(mouse_y, arena_base_y, arena_height,
                                      header_h, tile_size);
    human_process_tile_click(hi, wx, wy, mouse_x, mouse_y,
                              entities, entity_count, gui_entity_idx);
}

/** Handle prayer icon click. Hit-tests the 5-col prayer grid.
    Reuses the same layout math as gui_draw_prayer(). */
static void human_handle_prayer_click(HumanInput* hi, GuiState* gs, Player* p,
                                       int mouse_x, int mouse_y) {
    int oy = gui_content_y(gs) + 4;
    /* skip prayer points bar */
    int bar_h = 18;
    oy += bar_h + 6;

    int cols = 5;
    int gap = 2;
    int icon_sz = (gs->panel_w - 16 - gap * (cols - 1)) / cols;
    int grid_w = cols * icon_sz + (cols - 1) * gap;
    int gx = gs->panel_x + (gs->panel_w - grid_w) / 2;

    /* hit-test: which cell was clicked? */
    if (mouse_x < gx || mouse_y < oy) return;
    int col = (mouse_x - gx) / (icon_sz + gap);
    int row = (mouse_y - oy) / (icon_sz + gap);
    if (col < 0 || col >= cols) return;

    int idx = row * cols + col;
    if (idx < 0 || idx >= GUI_PRAYER_GRID_COUNT) return;

    /* check click is within the cell bounds (not in the gap) */
    int cell_x = gx + col * (icon_sz + gap);
    int cell_y = oy + row * (icon_sz + gap);
    if (mouse_x > cell_x + icon_sz || mouse_y > cell_y + icon_sz) return;

    GuiPrayerIdx pidx = GUI_PRAYER_GRID[idx];

    /* map prayer to action — only actionable prayers */
    switch (pidx) {
        case GUI_PRAY_PROTECT_MAGIC:
            hi->pending_prayer = (p->prayer == PRAYER_PROTECT_MAGIC)
                ? OVERHEAD_NONE : OVERHEAD_MAGE;
            break;
        case GUI_PRAY_PROTECT_MISSILES:
            hi->pending_prayer = (p->prayer == PRAYER_PROTECT_RANGED)
                ? OVERHEAD_NONE : OVERHEAD_RANGED;
            break;
        case GUI_PRAY_PROTECT_MELEE:
            hi->pending_prayer = (p->prayer == PRAYER_PROTECT_MELEE)
                ? OVERHEAD_NONE : OVERHEAD_MELEE;
            break;
        case GUI_PRAY_SMITE:
            hi->pending_prayer = (p->prayer == PRAYER_SMITE)
                ? OVERHEAD_NONE : OVERHEAD_SMITE;
            break;
        case GUI_PRAY_REDEMPTION:
            hi->pending_prayer = (p->prayer == PRAYER_REDEMPTION)
                ? OVERHEAD_NONE : OVERHEAD_REDEMPTION;
            break;
        case GUI_PRAY_PIETY:
            hi->pending_offensive_prayer = (p->offensive_prayer == OFFENSIVE_PRAYER_PIETY)
                ? 0 : 1;
            break;
        case GUI_PRAY_RIGOUR:
            hi->pending_offensive_prayer = (p->offensive_prayer == OFFENSIVE_PRAYER_RIGOUR)
                ? 0 : 2;
            break;
        case GUI_PRAY_AUGURY:
            hi->pending_offensive_prayer = (p->offensive_prayer == OFFENSIVE_PRAYER_AUGURY)
                ? 0 : 3;
            break;
        default:
            break;  /* non-actionable prayer */
    }
}

/** Handle spell icon click. Hit-tests the 4-col spell grid.
    Ice/blood spells enter CURSOR_SPELL_TARGET mode; vengeance is instant. */
static void human_handle_spell_click(HumanInput* hi, GuiState* gs,
                                      int mouse_x, int mouse_y) {
    int oy = gui_content_y(gs) + 8;
    int cols = 4;
    int gap = 2;
    int icon_sz = (gs->panel_w - 16 - gap * (cols - 1)) / cols;
    int grid_w = cols * icon_sz + (cols - 1) * gap;
    int gx = gs->panel_x + (gs->panel_w - grid_w) / 2;

    if (mouse_x < gx || mouse_y < oy) return;
    int col = (mouse_x - gx) / (icon_sz + gap);
    int row = (mouse_y - oy) / (icon_sz + gap);
    if (col < 0 || col >= cols) return;

    int idx = row * cols + col;
    if (idx < 0 || idx >= GUI_SPELL_GRID_COUNT) return;

    int cell_x = gx + col * (icon_sz + gap);
    int cell_y = oy + row * (icon_sz + gap);
    if (mouse_x > cell_x + icon_sz || mouse_y > cell_y + icon_sz) return;

    GuiSpellIdx sidx = GUI_SPELL_GRID[idx].idx;

    if (sidx == GUI_SPELL_VENGEANCE) {
        /* vengeance is instant — no targeting needed */
        hi->pending_veng = 1;
    } else if (sidx >= GUI_SPELL_ICE_RUSH && sidx <= GUI_SPELL_ICE_BARRAGE) {
        /* ice spell — enter targeting mode */
        hi->cursor_mode = CURSOR_SPELL_TARGET;
        hi->selected_spell = ATTACK_ICE;
    } else if (sidx >= GUI_SPELL_BLOOD_RUSH && sidx <= GUI_SPELL_BLOOD_BARRAGE) {
        /* blood spell — enter targeting mode */
        hi->cursor_mode = CURSOR_SPELL_TARGET;
        hi->selected_spell = ATTACK_BLOOD;
    }
}

/** Handle combat panel click (fight style buttons + spec bar).
    Fight style is set directly on Player (not in the action space).
    Spec bar click sets pending_spec. */
static void human_handle_combat_click(HumanInput* hi, GuiState* gs, Player* p,
                                       int mouse_x, int mouse_y) {
    int ox = gs->panel_x + 8;
    int oy = gui_content_y(gs) + 8;

    /* skip weapon name/sprite area */
    Texture2D wpn_tex = gui_get_item_sprite(gs, p->equipped[GEAR_SLOT_WEAPON]);
    oy += (wpn_tex.id != 0) ? 60 : 22;

    /* 2x2 fight style buttons */
    int btn_gap = 6;
    int btn_w = (gs->panel_w - 16 - btn_gap) / 2;
    int btn_h = 60;

    for (int i = 0; i < 4; i++) {
        int col = i % 2;
        int row = i / 2;
        int bx = ox + col * (btn_w + btn_gap);
        int by = oy + row * (btn_h + btn_gap);
        if (mouse_x >= bx && mouse_x < bx + btn_w &&
            mouse_y >= by && mouse_y < by + btn_h) {
            p->fight_style = (FightStyle)i;
            return;
        }
    }
    oy += 2 * (btn_h + btn_gap) + 10;

    /* skip "Special Attack" label */
    oy += 16;

    /* spec bar */
    int spec_w = gs->panel_w - 16;
    int spec_h = 26;
    if (mouse_x >= ox && mouse_x < ox + spec_w &&
        mouse_y >= oy && mouse_y < oy + spec_h) {
        hi->pending_spec = 1;
    }
}

/* ======================================================================== */
/* action translators — convert semantic intents to action arrays             */
/* ======================================================================== */

/** Translate human input to PvP 7-head action array for agent 0.
    Movement is target-relative (ADJACENT/UNDER/DIAGONAL/FARCAST_N). */
static void human_to_pvp_actions(HumanInput* hi, int* actions,
                                  Player* agent, Player* target) {
    /* zero all heads */
    for (int h = 0; h < NUM_ACTION_HEADS; h++) actions[h] = 0;

    /* HEAD_LOADOUT: keep current gear (human equips items via inventory clicks) */
    actions[HEAD_LOADOUT] = LOADOUT_KEEP;

    /* HEAD_COMBAT: attack or movement */
    if (hi->pending_attack) {
        if (hi->pending_spell == ATTACK_ICE) {
            actions[HEAD_COMBAT] = ATTACK_ICE;
        } else if (hi->pending_spell == ATTACK_BLOOD) {
            actions[HEAD_COMBAT] = ATTACK_BLOOD;
        } else {
            actions[HEAD_COMBAT] = ATTACK_ATK;
        }
    } else if (hi->pending_move_x >= 0 && hi->pending_move_y >= 0) {
        /* convert absolute tile to target-relative movement */
        int dx = hi->pending_move_x - target->x;
        int dy = hi->pending_move_y - target->y;
        int dist = (abs(dx) > abs(dy)) ? abs(dx) : abs(dy);  /* chebyshev */

        if (dist == 0) {
            actions[HEAD_COMBAT] = MOVE_UNDER;
        } else if (dist == 1) {
            /* check if cardinal (adjacent) or diagonal */
            if (dx == 0 || dy == 0) {
                actions[HEAD_COMBAT] = MOVE_ADJACENT;
            } else {
                actions[HEAD_COMBAT] = MOVE_DIAGONAL;
            }
        } else {
            /* farcast: clamp to 2-7 */
            int fc = dist;
            if (fc < 2) fc = 2;
            if (fc > 7) fc = 7;
            actions[HEAD_COMBAT] = MOVE_FARCAST_2 + (fc - 2);
        }
    }

    /* HEAD_OVERHEAD: prayer */
    if (hi->pending_prayer >= 0) {
        actions[HEAD_OVERHEAD] = hi->pending_prayer;
    }

    /* HEAD_FOOD */
    if (hi->pending_food) {
        actions[HEAD_FOOD] = FOOD_EAT;
    }

    /* HEAD_POTION */
    if (hi->pending_potion > 0) {
        actions[HEAD_POTION] = hi->pending_potion;
    }

    /* HEAD_KARAMBWAN */
    if (hi->pending_karambwan) {
        actions[HEAD_KARAMBWAN] = KARAM_EAT;
    }

    /* HEAD_VENG */
    if (hi->pending_veng) {
        actions[HEAD_VENG] = VENG_CAST;
    }

    /* spec: use LOADOUT_SPEC_MELEE/RANGE/MAGIC based on current weapon style */
    if (hi->pending_spec) {
        AttackStyle style = get_item_attack_style(agent->equipped[GEAR_SLOT_WEAPON]);
        switch (style) {
            case ATTACK_STYLE_MELEE:  actions[HEAD_LOADOUT] = LOADOUT_SPEC_MELEE; break;
            case ATTACK_STYLE_RANGED: actions[HEAD_LOADOUT] = LOADOUT_SPEC_RANGE; break;
            case ATTACK_STYLE_MAGIC:  actions[HEAD_LOADOUT] = LOADOUT_SPEC_MAGIC; break;
            default: break;
        }
    }

    /* offensive prayer: set via loadout-aware mechanism.
       piety/rigour/augury are auto-set by the action system based on loadout,
       so we handle this by setting the appropriate loadout if prayer changed.
       for human play we just directly mutate the player's offensive prayer. */
    if (hi->pending_offensive_prayer >= 0) {
        switch (hi->pending_offensive_prayer) {
            case 0: agent->offensive_prayer = OFFENSIVE_PRAYER_NONE; break;
            case 1: agent->offensive_prayer = OFFENSIVE_PRAYER_PIETY; break;
            case 2: agent->offensive_prayer = OFFENSIVE_PRAYER_RIGOUR; break;
            case 3: agent->offensive_prayer = OFFENSIVE_PRAYER_AUGURY; break;
            default: break;
        }
    }
}

/** Translate human input to encounter actions using the encounter vtable.
    Movement is translated from absolute tile coords to the encounter's
    movement system. The encounter's player entity is obtained via get_entity(0).

    Generic implementation that works for any encounter with standard
    action heads: MOVE(8-dir), ATTACK, PRAYER, FOOD, POTION, SPEC.
    For encounter-specific translators, override in the .c file. */
static void human_to_encounter_actions_generic(HumanInput* hi, int* actions,
                                                const EncounterDef* edef,
                                                void* encounter_state) {
    for (int h = 0; h < edef->num_action_heads; h++) actions[h] = 0;

    /* get player position via vtable */
    Player* player = edef->get_entity(encounter_state, 0);
    if (!player) return;

    /* movement: convert absolute tile to 8-directional.
       assumes head 0 is MOVE with dim=9, directions at indices 1-8. */
    if (hi->pending_move_x >= 0 && hi->pending_move_y >= 0 &&
        edef->num_action_heads > 0 && edef->action_head_dims[0] == 9) {
        int dx = hi->pending_move_x - player->x;
        int dy = hi->pending_move_y - player->y;
        int sx = (dx > 0) ? 1 : (dx < 0) ? -1 : 0;
        int sy = (dy > 0) ? 1 : (dy < 0) ? -1 : 0;

        /* 8-direction LUT: index 1-8 maps to (dx,dy) pairs.
           standard order: N(0,1), NE(1,1), E(1,0), SE(1,-1),
           S(0,-1), SW(-1,-1), W(-1,0), NW(-1,1) */
        static const int DX8[9] = { 0, 0, 1, 1, 1, 0, -1, -1, -1 };
        static const int DY8[9] = { 0, 1, 1, 0, -1, -1, -1, 0, 1 };
        for (int m = 1; m < 9; m++) {
            if (DX8[m] == sx && DY8[m] == sy) {
                actions[0] = m;
                break;
            }
        }
    }

    /* attack: head 1, values 0=none, 1=mage, 2=range (encounter convention) */
    if (hi->pending_attack && edef->num_action_heads > 1) {
        if (hi->pending_spell == ATTACK_ICE || hi->pending_spell == ATTACK_BLOOD) {
            actions[1] = 1;  /* mage attack */
        } else {
            actions[1] = 2;  /* range attack */
        }
    }

    /* prayer: head 2, values 0=none, 1=magic, 2=ranged, 3=melee */
    if (hi->pending_prayer >= 0 && edef->num_action_heads > 2) {
        switch (hi->pending_prayer) {
            case OVERHEAD_NONE:   actions[2] = 0; break;
            case OVERHEAD_MAGE:   actions[2] = 1; break;
            case OVERHEAD_RANGED: actions[2] = 2; break;
            case OVERHEAD_MELEE:  actions[2] = 3; break;
            default: break;
        }
    }

    /* food: head 3 */
    if (hi->pending_food && edef->num_action_heads > 3) {
        actions[3] = 1;
    }

    /* potion: head 4 */
    if (hi->pending_potion > 0 && edef->num_action_heads > 4) {
        if (hi->pending_potion == POTION_BREW) actions[4] = 1;
        else if (hi->pending_potion == POTION_RESTORE) actions[4] = 2;
        else if (hi->pending_potion == POTION_ANTIVENOM) actions[4] = 3;
    }

    /* spec: head 5 */
    if (hi->pending_spec && edef->num_action_heads > 5) {
        actions[5] = 1;
    }
}

/* ======================================================================== */
/* visual feedback drawing                                                   */
/* ======================================================================== */

/* click cross sprite textures: 4 yellow (move) + 4 red (attack) animation frames.
   loaded from data/sprites/gui/cross_*.png, indexed [0..3] yellow, [4..7] red. */
#define CLICK_CROSS_NUM_FRAMES 4
#define CLICK_CROSS_ANIM_TICKS 20  /* total animation duration in client ticks (50Hz) */

/** Draw click cross at screen-space position using sprite animation.
    cross_sprites must point to 8 loaded Texture2D (4 yellow + 4 red).
    Falls back to line drawing if sprites aren't loaded. */
static void human_draw_click_cross(HumanInput* hi, Texture2D* cross_sprites, int sprites_loaded) {
    if (!hi->click_cross_active) return;
    if (hi->click_cross_timer >= CLICK_CROSS_ANIM_TICKS) {
        hi->click_cross_active = 0;
        return;
    }

    int frame = hi->click_cross_timer * CLICK_CROSS_NUM_FRAMES / CLICK_CROSS_ANIM_TICKS;
    if (frame >= CLICK_CROSS_NUM_FRAMES) frame = CLICK_CROSS_NUM_FRAMES - 1;
    int sprite_idx = hi->click_is_attack ? frame + CLICK_CROSS_NUM_FRAMES : frame;

    int cx = hi->click_screen_x;
    int cy = hi->click_screen_y;

    if (sprites_loaded && cross_sprites[sprite_idx].id > 0) {
        Texture2D tex = cross_sprites[sprite_idx];
        /* center sprite on click position (OSRS draws at mouseX-8, mouseY-8 for 16px) */
        DrawTexture(tex, cx - tex.width / 2, cy - tex.height / 2, WHITE);
    } else {
        /* fallback: simple X lines */
        float progress = 1.0f - (float)hi->click_cross_timer / CLICK_CROSS_ANIM_TICKS;
        int alpha = (int)(progress * 255);
        Color c = hi->click_is_attack
            ? CLITERAL(Color){ 255, 50, 50, (unsigned char)alpha }
            : CLITERAL(Color){ 255, 255, 0, (unsigned char)alpha };
        DrawLine(cx - 6, cy - 6, cx + 6, cy + 6, c);
        DrawLine(cx + 6, cy - 6, cx - 6, cy + 6, c);
    }
}

/** Draw HUD indicators for human control mode.
    Call from the header rendering section. */
static void human_draw_hud(HumanInput* hi) {
    if (!hi->enabled) return;

    /* "HUMAN" indicator in header */
    DrawText("HUMAN", 8, 8, 16, YELLOW);

    /* spell targeting mode indicator */
    if (hi->cursor_mode == CURSOR_SPELL_TARGET) {
        const char* spell = (hi->selected_spell == ATTACK_ICE) ? "[ICE]" :
                            (hi->selected_spell == ATTACK_BLOOD) ? "[BLOOD]" : "[SPELL]";
        DrawText(spell, 80, 8, 14, CLITERAL(Color){100, 200, 255, 255});
    }
}

/** Tick the click cross animation timer. Call at 50Hz (client tick rate). */
static void human_tick_visuals(HumanInput* hi) {
    if (hi->click_cross_active) {
        hi->click_cross_timer++;
        if (hi->click_cross_timer >= CLICK_CROSS_ANIM_TICKS) {
            hi->click_cross_active = 0;
        }
    }
}

#endif /* OSRS_PVP_HUMAN_INPUT_H */
