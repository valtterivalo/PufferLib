/**
 * @file encounter_colosseum.h
 * @brief Fortis Colosseum — 12-wave PvM gauntlet ending in the Sol Heredit boss.
 *
 * Scaffold milestone: the data model, vtable, and binding are complete and
 * compiling, but gameplay (NPC stats, combat, wave scripts, boss state machine)
 * is left as placeholder for the lead to fill from OSRS Wiki values.
 *
 * waves 1-12 spawn Fremennik warband + colossi + beasts with a 40s reinforcement
 * cadence and a between-wave modifier draft. wave 12 is Sol Heredit (spear/shield
 * AoE combos, triple-parry, grapple, molten-sand hazards).
 *
 * mirrors the Inferno encounter's two-layer split: engine-agnostic encounter
 * logic here, PufferLib glue in ocean/osrs_colosseum/binding.c.
 */

#ifndef ENCOUNTER_COLOSSEUM_H
#define ENCOUNTER_COLOSSEUM_H

#include "../osrs_types.h"
#include "../osrs_items.h"
#include "../osrs_monsters_generated.h"
#include "../osrs_collision.h"
#include "../osrs_combat.h"
#include "../osrs_combat_visuals.h"
#include "../osrs_special_attacks.h"
#include "../osrs_pvp_gear.h"
#include "../osrs_encounter.h"
#include "../osrs_encounter_player.h"
#include "../osrs_encounter_visual_events.h"
#include "../osrs_player_consumables.h"
#include "../osrs_interaction.h"
#include "../data/npc_models.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdarg.h>
#include <ctype.h>
#include <errno.h>

/** Colosseum NPC roster. `type` is the discriminant for the ColoNPC union.
    waves 1-11 draw from the warband + colossi + beasts; wave 12 is Sol Heredit. */
typedef enum {
    COLO_FREMENNIK_BERSERKER = 0,  /* Fremennik Warband — melee */
    COLO_FREMENNIK_ARCHER,         /* Fremennik Warband — ranged */
    COLO_FREMENNIK_SEER,           /* Fremennik Warband — magic */
    COLO_SERPENT_SHAMAN,           /* poison magic caster */
    COLO_JAGUAR_WARRIOR,           /* 3-hit melee combo */
    COLO_JAVELIN_COLOSSUS,         /* long-range thrown */
    COLO_SHOCKWAVE_COLOSSUS,       /* AoE shockwave */
    COLO_MINOTAUR,                 /* heavy melee */
    COLO_MANTICORE,                /* 3-style barrage cycle */
    COLO_SOL_HEREDIT,              /* final boss */
    COLO_NUM_NPC_TYPES
} ColoNpcType;

/** terminal result of a Colosseum episode (the ColosseumState.winner field). */
typedef enum {
    COLO_OUTCOME_PLAYER_WON = 0,   /* cleared wave 12 (Sol Heredit dead) */
    COLO_OUTCOME_PLAYER_DIED = 1,  /* died or hit the tick cap */
} ColoOutcome;

#include "colosseum/encounter_colosseum_model.inc"
#include "colosseum/encounter_colosseum_helpers.inc"
#include "colosseum/encounter_colosseum_reset_spawn.inc"
#include "colosseum/encounter_colosseum_movement.inc"
#include "colosseum/encounter_colosseum_combat.inc"
#include "colosseum/encounter_colosseum_boss.inc"
#include "colosseum/encounter_colosseum_player_actions.inc"
#include "colosseum/encounter_colosseum_reward_step.inc"
#include "colosseum/encounter_colosseum_obs_mask.inc"
#include "colosseum/encounter_colosseum_render_snapshot.inc"

#endif /* ENCOUNTER_COLOSSEUM_H */
