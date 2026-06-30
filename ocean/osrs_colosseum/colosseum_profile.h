/**
 * @file colosseum_profile.h
 * @brief Gated per-bucket profiler for the OSRS Colosseum environment.
 */

#ifndef PUFFER_COLOSSEUM_PROFILE_H
#define PUFFER_COLOSSEUM_PROFILE_H

#include <stdlib.h>
#include <time.h>

#ifndef COLOSSEUM_ENV_EXPORT
#define COLOSSEUM_ENV_EXPORT __attribute__((visibility("default")))
#endif

typedef enum {
    COLO_PROF_C_STEP_TOTAL = 0,
    COLO_PROF_C_ACTIONS,
    COLO_PROF_C_ENCOUNTER_STEP,
    COLO_PROF_C_WRITE_OBS,
    COLO_PROF_C_WRITE_MASK,
    COLO_PROF_C_REWARD_TERMINAL,
    COLO_PROF_C_TERMINAL_LOG,
    COLO_PROF_C_RESET,
    COLO_PROF_OBS_REFRESH_SLOTS,
    COLO_PROF_OBS_PREFIX,
    COLO_PROF_OBS_PILLARS,
    COLO_PROF_OBS_BESTGEAR,
    COLO_PROF_OBS_INVENTORY,
    COLO_PROF_OBS_VENATOR,
    COLO_PROF_OBS_NPC_SLOTS,
    COLO_PROF_OBS_MODIFIERS,
    COLO_PROF_OBS_BOSS,
    COLO_PROF_OBS_PENDING_HITS,
    COLO_PROF_OBS_FORECAST,
    COLO_PROF_FC_SETUP,
    COLO_PROF_FC_ROLLOUT,
    COLO_PROF_FC_SOLARFLARE,
    COLO_PROF_STEP_NPC_TOTAL,
    COLO_PROF_STEP_SOL_BOSS,
    COLO_PROF_STEP_JAVELIN_SKYFALL,
    COLO_PROF_STEP_NPC_MOVEMENT,
    COLO_PROF_STEP_NPC_PATHFINDING,
    COLO_PROF_STEP_NPC_ATTACK,
    COLO_PROF_STEP_MANTICORE_BARRAGE,
    COLO_PROF_STEP_WARBAND_ATTACK,
    COLO_PROF_STEP_MODIFIERS_HAZARDS,
    COLO_PROF_COUNT,
} ColosseumProfileSlot;

static int g_colosseum_profile_enabled = -1;
static double g_colosseum_profile_ms[COLO_PROF_COUNT];

static const char* g_colosseum_profile_names[COLO_PROF_COUNT] = {
    "c_step_total",
    "c_actions",
    "c_encounter_step",
    "c_write_obs",
    "c_write_mask",
    "c_reward_terminal",
    "c_terminal_log",
    "c_reset",
    "obs_refresh_slots",
    "obs_prefix",
    "obs_pillars",
    "obs_bestgear_build",
    "obs_inventory",
    "obs_venator",
    "obs_npc_slots",
    "obs_modifiers",
    "obs_boss",
    "obs_pending_hits",
    "obs_forecast",
    "fc_setup",
    "fc_rollout",
    "fc_solarflare",
    "step_npc_total",
    "step_sol_boss",
    "step_javelin_skyfall",
    "step_npc_movement",
    "step_npc_pathfinding",
    "step_npc_attack",
    "step_manticore_barrage",
    "step_warband_attack",
    "step_modifiers_hazards",
};

static int colosseum_profile_enabled(void) {
    if (g_colosseum_profile_enabled < 0) {
        const char* text = getenv("PUFFER_COLOSSEUM_PROFILE");
        g_colosseum_profile_enabled = (text && text[0] && text[0] != '0') ? 1 : 0;
    }
    return g_colosseum_profile_enabled;
}

static double colosseum_profile_now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1000000.0;
}

static void colosseum_profile_add(int slot, double ms) {
    if (slot < 0 || slot >= COLO_PROF_COUNT) abort();
    #pragma omp atomic update
    g_colosseum_profile_ms[slot] += ms;
}

static void colosseum_profile_mark(int enabled, double* last_ms, int slot) {
    if (!enabled) return;
    double now = colosseum_profile_now_ms();
    colosseum_profile_add(slot, now - *last_ms);
    *last_ms = now;
}

COLOSSEUM_ENV_EXPORT int colosseum_env_profile_count(void) {
    return colosseum_profile_enabled() ? COLO_PROF_COUNT : 0;
}

COLOSSEUM_ENV_EXPORT const char* colosseum_env_profile_name(int slot) {
    if (slot < 0 || slot >= COLO_PROF_COUNT) abort();
    return g_colosseum_profile_names[slot];
}

COLOSSEUM_ENV_EXPORT double colosseum_env_profile_read_reset_ms(int slot) {
    if (slot < 0 || slot >= COLO_PROF_COUNT) abort();
    double value;
    #pragma omp atomic read
    value = g_colosseum_profile_ms[slot];
    #pragma omp atomic write
    g_colosseum_profile_ms[slot] = 0.0;
    return value;
}

#define COLO_PROFILE_ENABLED() colosseum_profile_enabled()
#define COLO_PROFILE_NOW_MS() colosseum_profile_now_ms()
#define COLO_PROFILE_ADD(slot, ms) colosseum_profile_add((slot), (ms))
#define COLO_PROFILE_MARK(slot) colosseum_profile_mark(col_prof_enabled, &col_prof_t0, (slot))

#endif
