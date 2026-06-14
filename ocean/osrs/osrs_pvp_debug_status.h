#ifndef OSRS_PVP_DEBUG_STATUS_H
#define OSRS_PVP_DEBUG_STATUS_H

#include "osrs_types.h"
#include <stdio.h>
#include <stdlib.h>

#define OSRS_PVP_DEBUG_LINE_LEN 128

typedef struct {
    char title[OSRS_PVP_DEBUG_LINE_LEN];
    char combat[OSRS_PVP_DEBUG_LINE_LEN];
    char status[OSRS_PVP_DEBUG_LINE_LEN];
    char resources[OSRS_PVP_DEBUG_LINE_LEN];
} OsrsPvpDebugStatusLines;

static inline const char* osrs_pvp_debug_overhead_name(OverheadPrayer prayer) {
    switch (prayer) {
        case PRAYER_NONE: return "none";
        case PRAYER_PROTECT_MAGIC: return "mage";
        case PRAYER_PROTECT_RANGED: return "range";
        case PRAYER_PROTECT_MELEE: return "melee";
        case PRAYER_SMITE: return "smite";
        case PRAYER_REDEMPTION: return "redemp";
    }
    fprintf(stderr, "bad overhead prayer %d\n", prayer);
    abort();
}

static inline const char* osrs_pvp_debug_offensive_name(OffensivePrayer prayer) {
    switch (prayer) {
        case OFFENSIVE_PRAYER_NONE: return "none";
        case OFFENSIVE_PRAYER_MELEE_LOW: return "melee";
        case OFFENSIVE_PRAYER_RANGED_LOW: return "range";
        case OFFENSIVE_PRAYER_MAGIC_LOW: return "mage";
        case OFFENSIVE_PRAYER_PIETY: return "piety";
        case OFFENSIVE_PRAYER_RIGOUR: return "rigour";
        case OFFENSIVE_PRAYER_AUGURY: return "augury";
    }
    fprintf(stderr, "bad offensive prayer %d\n", prayer);
    abort();
}

static inline const char* osrs_pvp_debug_fight_style_name(FightStyle style) {
    switch (style) {
        case FIGHT_STYLE_ACCURATE: return "accurate";
        case FIGHT_STYLE_AGGRESSIVE: return "aggressive";
        case FIGHT_STYLE_CONTROLLED: return "controlled";
        case FIGHT_STYLE_DEFENSIVE: return "defensive";
        case FIGHT_STYLE_RAPID: return "rapid";
        case FIGHT_STYLE_LONGRANGE: return "longrange";
        case FIGHT_STYLE_AUTOCAST: return "autocast";
        case FIGHT_STYLE_DEFENSIVE_AUTOCAST: return "def autocast";
    }
    fprintf(stderr, "bad fight style %d\n", style);
    abort();
}

static inline const char* osrs_pvp_debug_autocast_name(const Player* player) {
    if (!player->autocast_enabled) return "off";
    switch (player->autocast_spell) {
        case ENCOUNTER_SPELL_ICE: return "ice";
        case ENCOUNTER_SPELL_BLOOD: return "blood";
    }
    fprintf(stderr, "bad autocast spell %d\n", player->autocast_spell);
    abort();
}

static inline int osrs_pvp_debug_attack_timer_uncapped(const Player* player) {
    return player->has_attack_timer ? player->attack_timer_uncapped : player->attack_timer;
}

static inline void osrs_pvp_debug_status_lines(
    const Player* player,
    const char* name,
    OsrsPvpDebugStatusLines* out
) {
    snprintf(out->title, sizeof(out->title), "%s  HP:%d/%d  Pray:%d/%d",
        name,
        player->current_hitpoints,
        player->base_hitpoints,
        player->current_prayer,
        player->base_prayer);
    snprintf(out->combat, sizeof(out->combat),
        "ATK:%d UC:%d  FS:%s  AC:%s  SPEC:%d%s",
        player->attack_timer,
        osrs_pvp_debug_attack_timer_uncapped(player),
        osrs_pvp_debug_fight_style_name(player->fight_style),
        osrs_pvp_debug_autocast_name(player),
        player->special_energy,
        player->spec_armed ? "*" : "");
    snprintf(out->status, sizeof(out->status),
        "FRZ:%d IMM:%d  OH:%s OFF:%s  VENG:%d/%d",
        remaining_ticks(player->frozen_ticks),
        remaining_ticks(player->freeze_immunity_ticks),
        osrs_pvp_debug_overhead_name(player->prayer),
        osrs_pvp_debug_offensive_name(player->offensive_prayer),
        player->veng_active,
        remaining_ticks(player->veng_cooldown));
    snprintf(out->resources, sizeof(out->resources),
        "Food:%d K:%d Brew:%d Rest:%d  TMR F:%d P:%d K:%d",
        player->food_count,
        player->karambwan_count,
        player->brew_doses,
        player->restore_doses,
        remaining_ticks(player->food_timer),
        remaining_ticks(player->potion_timer),
        remaining_ticks(player->karambwan_timer));
}

#endif
