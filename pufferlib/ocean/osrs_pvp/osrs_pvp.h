/**
 * @file osrs_pvp.h
 * @brief OSRS PvP environment - high-performance C simulation
 *
 * Include this file for all environment access. It pulls in all
 * subsystem headers in the correct order:
 *
 * - osrs_pvp_types.h     Core types, enums, structs, constants
 * - osrs_pvp_gear.h      Gear bonus tables and computation
 * - osrs_pvp_combat.h    Damage calculations and special attacks
 * - osrs_pvp_movement.h  Tile selection and pathfinding
 * - osrs_pvp_observations.h  Observation generation and action masks
 * - osrs_pvp_actions.h   Action processing (slot-based mode)
 * - osrs_pvp_api.h       Public API (pvp_reset, pvp_step, pvp_seed, pvp_close)
 *
 * Performance: ~1.1M steps/sec in pure C, ~200k steps/sec via Python binding (no opponent),
 * ~56-90k steps/sec with scripted opponents.
 *
 * Usage:
 *   OsrsPvp env = {0};
 *   pvp_reset(&env);
 *   while (!env.episode_over) {
 *       // set actions
 *       pvp_step(&env);
 *   }
 *   pvp_close(&env);
 */

#ifndef OSRS_PVP_H
#define OSRS_PVP_H

#include "osrs_pvp_types.h"
#include "osrs_pvp_collision.h"
#include "osrs_pvp_pathfinding.h"
#include "osrs_pvp_gear.h"
#include "osrs_pvp_combat.h"
#include "osrs_pvp_movement.h"
#include "osrs_pvp_observations.h"
#include "osrs_pvp_actions.h"
#include "osrs_pvp_opponents.h"
#include "osrs_pvp_api.h"

#endif // OSRS_PVP_H
