/**
 * @file inferno_profile.h
 * @brief Gated per-bucket profiler for the OSRS Inferno environment.
 */

#ifndef PUFFER_INFERNO_PROFILE_H
#define PUFFER_INFERNO_PROFILE_H

#include <stdlib.h>
#include <time.h>

#ifndef INFERNO_ENV_EXPORT
#define INFERNO_ENV_EXPORT __attribute__((visibility("default")))
#endif

typedef enum {
    INF_PROF_C_STEP_TOTAL = 0,
    INF_PROF_C_ACTIONS,
    INF_PROF_C_PRE_STEP_TRACES,
    INF_PROF_C_ENCOUNTER_STEP,
    INF_PROF_C_WRITE_OBS,
    INF_PROF_C_WRITE_MASK,
    INF_PROF_C_REWARD_TERMINAL,
    INF_PROF_C_POST_STEP_TRACES,
    INF_PROF_C_TERMINAL_LOG,
    INF_PROF_C_RESET,
    INF_PROF_OBS_PREFIX,
    INF_PROF_OBS_REFRESH_SLOTS,
    INF_PROF_OBS_NPC_SLOTS,
    INF_PROF_OBS_FORECAST,
    INF_PROF_OBS_PENDING_HITS,
    INF_PROF_OBS_SPARKS,
    INF_PROF_FORECAST_LANDING,
    INF_PROF_FORECAST_STATE_COPY,
    INF_PROF_FORECAST_NPC_MOVE,
    INF_PROF_FORECAST_NPC_ATTACK,
    INF_PROF_FORECAST_CALLS,
    INF_PROF_FORECAST_VALID_ACTIONS,
    INF_PROF_FORECAST_DISTINCT_LANDINGS,
    INF_PROF_COUNT,
} InfernoProfileSlot;

static int g_inferno_profile_enabled = -1;
static double g_inferno_profile_ms[INF_PROF_COUNT];

static const char* g_inferno_profile_names[INF_PROF_COUNT] = {
    "c_step_total",
    "c_actions",
    "c_pre_step_traces",
    "c_encounter_step",
    "c_write_obs",
    "c_write_mask",
    "c_reward_terminal",
    "c_post_step_traces",
    "c_terminal_log",
    "c_reset",
    "obs_prefix",
    "obs_refresh_slots",
    "obs_npc_slots",
    "obs_forecast",
    "obs_pending_hits",
    "obs_sparks",
    "forecast_landing",
    "forecast_state_copy",
    "forecast_npc_move",
    "forecast_npc_attack",
    "forecast_calls",
    "forecast_valid_actions",
    "forecast_distinct_landings",
};

static int inferno_profile_enabled(void) {
    if (g_inferno_profile_enabled < 0) {
        const char* text = getenv("PUFFER_INFERNO_PROFILE");
        g_inferno_profile_enabled = (text && text[0] && text[0] != '0') ? 1 : 0;
    }
    return g_inferno_profile_enabled;
}

static double inferno_profile_now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1000000.0;
}

static void inferno_profile_add(int slot, double ms) {
    if (slot < 0 || slot >= INF_PROF_COUNT) abort();
    #pragma omp atomic update
    g_inferno_profile_ms[slot] += ms;
}

static void inferno_profile_mark(int enabled, double* last_ms, int slot) {
    if (!enabled) return;
    double now = inferno_profile_now_ms();
    inferno_profile_add(slot, now - *last_ms);
    *last_ms = now;
}

INFERNO_ENV_EXPORT int inferno_env_profile_count(void) {
    return inferno_profile_enabled() ? INF_PROF_COUNT : 0;
}

INFERNO_ENV_EXPORT const char* inferno_env_profile_name(int slot) {
    if (slot < 0 || slot >= INF_PROF_COUNT) abort();
    return g_inferno_profile_names[slot];
}

INFERNO_ENV_EXPORT double inferno_env_profile_read_reset_ms(int slot) {
    if (slot < 0 || slot >= INF_PROF_COUNT) abort();
    double value;
    #pragma omp atomic read
    value = g_inferno_profile_ms[slot];
    #pragma omp atomic write
    g_inferno_profile_ms[slot] = 0.0;
    return value;
}

#define INF_PROFILE_ENABLED() inferno_profile_enabled()
#define INF_PROFILE_NOW_MS() inferno_profile_now_ms()
#define INF_PROFILE_ADD(slot, ms) inferno_profile_add((slot), (ms))
#define INF_PROFILE_MARK(slot) inferno_profile_mark(inf_prof_enabled, &inf_prof_t0, (slot))

#endif
