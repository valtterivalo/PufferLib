/**
 * @file binding.c
 * @brief Static-native binding for OSRS Inferno encounter.
 *
 * Bridges vecenv.h's contract (float actions, float terminals) with the
 * Inferno encounter's vtable interface.
 */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <pthread.h>
#include <stdint.h>
#include <errno.h>
#include <limits.h>
#include <sys/stat.h>
#include <time.h>

#ifdef __cplusplus
#define _Thread_local thread_local
#endif

#include "../osrs/osrs_env.h"  /* pulls in osrs_types, encounter, pvp stack */
#include "../osrs/osrs_assets.h"
#include "replay_best.h"

#define INFERNO_ENV_EXPORT __attribute__((visibility("default")))

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

/* encounter headers + render.h have many static helpers only used by the
   standalone viewer (not c_render) — suppress unused-function noise. */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#include "../osrs/encounters/encounter_inferno.h"
#include "../osrs/encounters/encounter_zulrah.h"  /* render.h references ZulrahState */
#ifdef __CUDACC__
#define float3 raymath_float3
#define float16 raymath_float16
#endif
#include "../osrs/osrs_render.h"
#ifdef __CUDACC__
#undef float3
#undef float16
#endif
#include "../osrs/osrs_scene_assets.h"
#pragma GCC diagnostic pop

#define INF_TOTAL_OBS (INF_NUM_OBS + INF_ACTION_MASK_SIZE)

typedef struct InfernoEnv {
    void* observations;
    float* actions;
    float* rewards;
    float* terminals;
    int num_agents;
    int rng;
    Log log;

    InfernoState state;
    InfernoContext context;
    int config_start_wave;  /* the start_wave from config (not curriculum override) */

    int acts_staging[INF_NUM_ACTION_HEADS];
    unsigned char term_staging;

    /* best-episode replay recording: all envs buffer their current episode's actions.
       on terminal, if the episode reached a new global best wave, flush to disk.
       binary format: [int32 num_ticks] [uint32 rng_state] [num_heads int32 per tick] */
    int* episode_actions;    /* buffer: episode_len * NUM_ATNS ints */
    int episode_action_cap;  /* max ticks we can buffer */
    int episode_action_len;  /* ticks buffered so far this episode */
    uint32_t episode_rng_start; /* RNG state at start of current episode */
    InfSnapshot episode_initial_snapshot;
    int episode_initial_snapshot_valid;

    /* replay playback: when PLAY_REPLAY=path is set, override the policy's
       actions with the recorded ones. first env only (env 0). */
    int* replay_actions;     /* full action buffer: num_ticks * NUM_ATNS */
    int replay_num_ticks;
    int replay_cursor;       /* ticks consumed so far */
    uint32_t replay_rng_seed;
    InfSnapshot replay_initial_snapshot;
    int replay_has_initial_snapshot;

    float ticks_per_second;
    double last_step_time;

    /* set by c_step on terminal-reset; consumed by c_render on its next call
       to mirror the standalone viewer's post-reset cleanup (osrs_visual.c:186). */
    int pending_render_reset;
    int render_status_frames;
    char render_status_text[ENCOUNTER_OVERLAY_STATUS_TEXT_LEN];

    OsrsEnv render_env; /* minimal env wrapper for pvp_render() */

    FILE* post_240_trace_file;
    int post_240_trace_id;
    int post_240_trace_active;
    int post_240_trace_rows;
    int post_240_trace_truncated;
    FILE* stall_trace_file;
    int stall_trace_id;
    int stall_trace_rows;
    int stall_trace_truncated;
    int stall_trace_ticks;
} InfernoEnv;

#define INF_ENV_STATE(env) ((EncounterState*)&((env)->state))
#define INF_ENV_CONTEXT(env) ((EncounterContext*)&((env)->context))
#define INF_ENV_INFERNO(env) (&((env)->state))
#define INF_ENV_INFERNO_CONTEXT(env) (&((env)->context))

#define OBS_SIZE INF_TOTAL_OBS
#define NUM_ATNS INF_NUM_ACTION_HEADS
#define ACT_SIZES INF_ACTION_DIMS_INIT
#define OBS_TENSOR_T FloatTensor
#define Env InfernoEnv
#define INF_RENDER_STATUS_FRAMES 180

/* global best episode tracking */
static pthread_mutex_t g_best_replay_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t g_post_240_trace_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t g_stall_trace_mutex = PTHREAD_MUTEX_INITIALIZER;
static InfernoReplayBest g_best_replay = {
    .wave = 0,
    .ticks = 999999,
    .min_zuk_hp = 999999,
    .rng_seed = UINT32_MAX,
};
static int g_post_240_trace_initialized = 0;
static int g_post_240_trace_next_id = 0;
static int g_post_240_trace_max = 0;
static int g_post_240_trace_tick_cap = 512;
static char g_post_240_trace_dir[1024] = "";
static int g_stall_trace_initialized = 0;
static int g_stall_trace_next_id = 0;
static int g_stall_trace_max = 0;
static int g_stall_trace_tick_cap = 512;
static int g_stall_trace_min_ticks = 64;
static char g_stall_trace_dir[1024] = "";

static void inferno_replay_lock_best(void) {
    if (pthread_mutex_lock(&g_best_replay_mutex) != 0) {
        fprintf(stderr, "RECORD_REPLAY: cannot lock best replay state\n");
        abort();
    }
}

static void inferno_replay_unlock_best(void) {
    if (pthread_mutex_unlock(&g_best_replay_mutex) != 0) {
        fprintf(stderr, "RECORD_REPLAY: cannot unlock best replay state\n");
        abort();
    }
}

static void inferno_replay_write_or_abort(
    const char* rpath,
    int episode_action_len,
    uint32_t episode_rng_start,
    const int* episode_actions,
    const InfSnapshot* initial_snapshot
) {
    FILE* fp = fopen(rpath, "wb");
    if (!fp) {
        fprintf(stderr, "RECORD_REPLAY: cannot open %s\n", rpath);
        abort();
    }

    size_t expected_actions = (size_t)episode_action_len * NUM_ATNS;
    int has_written_replay =
        fwrite(&episode_action_len, sizeof(int), 1, fp) == 1 &&
        fwrite(&episode_rng_start, sizeof(uint32_t), 1, fp) == 1 &&
        fwrite(episode_actions, sizeof(int), expected_actions, fp) == expected_actions;
    if (has_written_replay && initial_snapshot) {
        has_written_replay =
            fwrite(initial_snapshot, sizeof(*initial_snapshot), 1, fp) == 1;
    }
    if (!has_written_replay) {
        fprintf(stderr, "RECORD_REPLAY: short write to %s\n", rpath);
        fclose(fp);
        abort();
    }

    if (fclose(fp) != 0) {
        fprintf(stderr, "RECORD_REPLAY: cannot close %s\n", rpath);
        abort();
    }
}

static int inferno_read_int_env(const char* name, int default_value) {
    const char* raw = getenv(name);
    if (!raw || !raw[0]) return default_value;

    char* end = NULL;
    errno = 0;
    long value = strtol(raw, &end, 10);
    if (errno != 0 || end == raw || *end != '\0' || value < INT_MIN || value > INT_MAX) {
        fprintf(stderr, "%s must be an integer, got %s\n", name, raw);
        abort();
    }
    return (int)value;
}

static void inferno_post_240_trace_lock(void) {
    if (pthread_mutex_lock(&g_post_240_trace_mutex) != 0) {
        fprintf(stderr, "POST240_TRACE: cannot lock trace state\n");
        abort();
    }
}

static void inferno_post_240_trace_unlock(void) {
    if (pthread_mutex_unlock(&g_post_240_trace_mutex) != 0) {
        fprintf(stderr, "POST240_TRACE: cannot unlock trace state\n");
        abort();
    }
}

static void inferno_post_240_trace_init_once(void) {
    inferno_post_240_trace_lock();
    if (!g_post_240_trace_initialized) {
        const char* dir = getenv("POST240_TRACE_DIR");
        if (dir && dir[0]) {
            int n = snprintf(g_post_240_trace_dir, sizeof(g_post_240_trace_dir), "%s", dir);
            if (n < 0 || (size_t)n >= sizeof(g_post_240_trace_dir)) {
                fprintf(stderr, "POST240_TRACE_DIR too long\n");
                abort();
            }
            g_post_240_trace_max =
                inferno_read_int_env("POST240_TRACE_MAX_EPISODES", 40);
            g_post_240_trace_tick_cap =
                inferno_read_int_env("POST240_TRACE_TICK_CAP", 512);
            if (g_post_240_trace_max < 0) {
                fprintf(stderr, "POST240_TRACE_MAX_EPISODES must be >= 0\n");
                abort();
            }
            if (g_post_240_trace_tick_cap <= 0) {
                fprintf(stderr, "POST240_TRACE_TICK_CAP must be > 0\n");
                abort();
            }
            if (mkdir(g_post_240_trace_dir, 0775) != 0 && errno != EEXIST) {
                fprintf(stderr, "POST240_TRACE: cannot create %s\n", g_post_240_trace_dir);
                abort();
            }
        }
        g_post_240_trace_initialized = 1;
    }
    inferno_post_240_trace_unlock();
}

static int inferno_post_240_trace_reserve_id(void) {
    int id = -1;
    inferno_post_240_trace_lock();
    if (g_post_240_trace_dir[0] && g_post_240_trace_next_id < g_post_240_trace_max) {
        id = g_post_240_trace_next_id++;
    }
    inferno_post_240_trace_unlock();
    return id;
}

typedef struct {
    int selected_target_action;
    int selected_target_npc;
    int selected_target_type;
    int valid_target_actions;
    int selected_action_valid[INF_NUM_ACTION_HEADS];
    int selected_action_value[INF_NUM_ACTION_HEADS];
} InfernoStallTraceDecision;

static int inferno_action_mask_offset(int head) {
    if (head < 0 || head >= INF_NUM_ACTION_HEADS) {
        fprintf(stderr, "bad action head %d\n", head);
        abort();
    }
    int offset = 0;
    for (int h = 0; h < head; h++)
        offset += INF_ACTION_DIMS[h];
    return offset;
}

static void inferno_stall_trace_lock(void) {
    if (pthread_mutex_lock(&g_stall_trace_mutex) != 0) {
        fprintf(stderr, "STALL_TRACE: cannot lock trace state\n");
        abort();
    }
}

static void inferno_stall_trace_unlock(void) {
    if (pthread_mutex_unlock(&g_stall_trace_mutex) != 0) {
        fprintf(stderr, "STALL_TRACE: cannot unlock trace state\n");
        abort();
    }
}

static void inferno_stall_trace_init_once(void) {
    inferno_stall_trace_lock();
    if (!g_stall_trace_initialized) {
        const char* dir = getenv("STALL_TRACE_DIR");
        if (dir && dir[0]) {
            int n = snprintf(g_stall_trace_dir, sizeof(g_stall_trace_dir), "%s", dir);
            if (n < 0 || (size_t)n >= sizeof(g_stall_trace_dir)) {
                fprintf(stderr, "STALL_TRACE_DIR too long\n");
                abort();
            }
            g_stall_trace_max =
                inferno_read_int_env("STALL_TRACE_MAX_EPISODES", 16);
            g_stall_trace_tick_cap =
                inferno_read_int_env("STALL_TRACE_TICK_CAP", 512);
            g_stall_trace_min_ticks =
                inferno_read_int_env("STALL_TRACE_MIN_TICKS", 64);
            if (g_stall_trace_max < 0) {
                fprintf(stderr, "STALL_TRACE_MAX_EPISODES must be >= 0\n");
                abort();
            }
            if (g_stall_trace_tick_cap <= 0) {
                fprintf(stderr, "STALL_TRACE_TICK_CAP must be > 0\n");
                abort();
            }
            if (g_stall_trace_min_ticks <= 0) {
                fprintf(stderr, "STALL_TRACE_MIN_TICKS must be > 0\n");
                abort();
            }
            if (mkdir(g_stall_trace_dir, 0775) != 0 && errno != EEXIST) {
                fprintf(stderr, "STALL_TRACE: cannot create %s\n", g_stall_trace_dir);
                abort();
            }
        }
        g_stall_trace_initialized = 1;
    }
    inferno_stall_trace_unlock();
}

static int inferno_stall_trace_reserve_id(void) {
    int id = -1;
    inferno_stall_trace_lock();
    if (g_stall_trace_dir[0] && g_stall_trace_next_id < g_stall_trace_max)
        id = g_stall_trace_next_id++;
    inferno_stall_trace_unlock();
    return id;
}

static void inferno_stall_trace_capture_decision(
    Env* env,
    const float* action_mask,
    InfernoStallTraceDecision* out
) {
    InfernoState* s = INF_ENV_INFERNO(env);
    inf_refresh_current_obs_slots(s);
    memset(out, 0, sizeof(*out));
    out->selected_target_action = env->acts_staging[INF_HEAD_TARGET];
    out->selected_target_npc = -1;
    out->selected_target_type = -1;

    for (int h = 0; h < INF_NUM_ACTION_HEADS; h++) {
        int action = env->acts_staging[h];
        int offset = inferno_action_mask_offset(h);
        out->selected_action_value[h] = action;
        out->selected_action_valid[h] =
            action >= 0 && action < INF_ACTION_DIMS[h] &&
            action_mask[offset + action] > 0.5f;
    }

    int target_offset = inferno_action_mask_offset(INF_HEAD_TARGET);
    for (int a = 1; a <= INF_OBS_NPCS; a++) {
        if (action_mask[target_offset + a] > 0.5f)
            out->valid_target_actions++;
    }
    if (out->selected_target_action > 0 &&
            out->selected_target_action <= INF_OBS_NPCS) {
        int obs_slot = out->selected_target_action - 1;
        int npc_idx = s->current_obs_slots[obs_slot];
        out->selected_target_npc = npc_idx;
        if (npc_idx >= 0 && npc_idx < INF_MAX_NPCS)
            out->selected_target_type = s->npcs[npc_idx].type;
    }
}

static int inferno_stall_trace_has_alive_target(const InfernoState* s) {
    for (int i = 0; i < INF_MAX_NPCS; i++) {
        const InfNPC* npc = &s->npcs[i];
        if (!npc->active || npc->death_ticks > 0 || npc->hp <= 0)
            continue;
        if (npc->type == INF_NPC_ZUK_SHIELD)
            continue;
        return 1;
    }
    return 0;
}

static int inferno_stall_trace_tick_matches(const InfernoState* s) {
    if (s->episode_over) return 0;
    if (s->wave_spawn_delay > 0 || s->wave_ready_delay > 0) return 0;
    if (!inferno_stall_trace_has_alive_target(s)) return 0;
    if (s->player.attack_timer != 0) return 0;
    if (s->tick_scratch.player_attacked) return 0;
    if (s->tick_scratch.damage_dealt > 0.0f) return 0;
    return 1;
}

static void inferno_stall_trace_write_npcs(FILE* fp, InfernoState* s) {
    fprintf(fp, "\"npcs\":[");
    int first = 1;
    for (int i = 0; i < INF_MAX_NPCS; i++) {
        InfNPC* npc = &s->npcs[i];
        if (!npc->active || npc->death_ticks > 0 || npc->hp <= 0)
            continue;
        if (npc->type == INF_NPC_ZUK_SHIELD)
            continue;
        int obs_slot = inf_find_target_obs_slot(s, i);
        int player_can_attack =
            inf_player_can_attack_npc_from_current_tile(s, i);
        int npc_has_los = inf_npc_has_los_direct(s, i);
        if (!first) fprintf(fp, ",");
        first = 0;
        fprintf(fp,
            "{\"slot\":%d,\"type\":%d,\"hp\":%d,\"x\":%d,\"y\":%d,"
            "\"size\":%d,\"attack_timer\":%d,\"obs_slot\":%d,"
            "\"player_can_attack\":%d,\"npc_has_los\":%d}",
            i, npc->type, npc->hp, npc->x, npc->y, npc->size,
            npc->attack_timer, obs_slot, player_can_attack, npc_has_los);
    }
    fprintf(fp, "]");
}

static void inferno_stall_trace_close(Env* env, const char* reason) {
    if (!env->stall_trace_file)
        return;

    InfernoState* s = INF_ENV_INFERNO(env);
    fprintf(env->stall_trace_file,
        "{\"kind\":\"end\",\"reason\":\"%s\",\"trace_id\":%d,"
        "\"tick\":%d,\"winner\":%d,\"rows\":%d,\"truncated\":%d}\n",
        reason,
        env->stall_trace_id,
        s->tick,
        s->winner,
        env->stall_trace_rows,
        env->stall_trace_truncated);
    if (fclose(env->stall_trace_file) != 0) {
        fprintf(stderr, "STALL_TRACE: cannot close trace file\n");
        abort();
    }
    env->stall_trace_file = NULL;
}

static void inferno_stall_trace_open(Env* env, const InfernoState* s) {
    if (env->stall_trace_file)
        return;

    int id = inferno_stall_trace_reserve_id();
    if (id < 0)
        return;

    char path[1400];
    int n = snprintf(path, sizeof(path),
        "%s/stall_%06d_env%04d_seed%u.jsonl",
        g_stall_trace_dir,
        id,
        env->rng,
        env->episode_rng_start);
    if (n < 0 || (size_t)n >= sizeof(path)) {
        fprintf(stderr, "STALL_TRACE path too long\n");
        abort();
    }

    env->stall_trace_file = fopen(path, "w");
    if (!env->stall_trace_file) {
        fprintf(stderr, "STALL_TRACE: cannot open %s\n", path);
        abort();
    }
    env->stall_trace_id = id;
    env->stall_trace_rows = 0;
    env->stall_trace_truncated = 0;
    fprintf(env->stall_trace_file,
        "{\"kind\":\"meta\",\"trace_id\":%d,\"env_idx\":%d,"
        "\"rng_start\":%u,\"start_wave\":%d,\"min_ticks\":%d,"
        "\"tick_cap\":%d}\n",
        id,
        env->rng,
        env->episode_rng_start,
        s->start_wave + 1,
        g_stall_trace_min_ticks,
        g_stall_trace_tick_cap);
}

static void inferno_stall_trace_capture(
    Env* env,
    const InfernoStallTraceDecision* decision,
    int is_term
) {
    if (!g_stall_trace_initialized)
        inferno_stall_trace_init_once();
    if (!g_stall_trace_dir[0] || g_stall_trace_max <= 0)
        return;

    InfernoState* s = INF_ENV_INFERNO(env);
    if (inferno_stall_trace_tick_matches(s)) {
        env->stall_trace_ticks++;
    } else {
        env->stall_trace_ticks = 0;
        if (is_term)
            inferno_stall_trace_close(env, "terminal");
        return;
    }

    if (env->stall_trace_ticks < g_stall_trace_min_ticks)
        return;

    inferno_stall_trace_open(env, s);
    if (!env->stall_trace_file)
        return;

    if (env->stall_trace_rows >= g_stall_trace_tick_cap) {
        env->stall_trace_truncated = 1;
        return;
    }

    int current_target_slot = osrs_interaction_active(&s->interaction)
        ? s->interaction.target_slot : -1;
    int current_target_type = -1;
    int current_target_attackable = 0;
    int current_target_in_range = 0;
    int current_target_los = 0;
    if (current_target_slot >= 0 && current_target_slot < INF_MAX_NPCS) {
        current_target_type = s->npcs[current_target_slot].type;
        current_target_attackable = inf_obs_slot_is_targetable(
            s,
            INF_ENV_INFERNO_CONTEXT(env),
            inf_find_target_obs_slot(s, current_target_slot));
        current_target_in_range =
            inf_player_can_attack_npc_from_current_tile(s, current_target_slot);
        current_target_los = inf_npc_has_los_direct(s, current_target_slot);
    }

    fprintf(env->stall_trace_file,
        "{\"kind\":\"stall_tick\",\"trace_id\":%d,\"tick\":%d,"
        "\"stall_ticks\":%d,\"wave\":%d,\"reward\":%.6f,"
        "\"episode_return\":%.6f,\"player_x\":%d,\"player_y\":%d,"
        "\"player_hp\":%d,\"player_prayer\":%d,"
        "\"player_attack_timer\":%d,\"player_dest_x\":%d,"
        "\"player_dest_y\":%d,\"player_moved\":%d,"
        "\"active_overhead\":%d,"
        "\"offensive_prayer\":%d,\"weapon\":%d,\"ranged\":%d,"
        "\"magic\":%d,\"brews\":%d,\"restores\":%d,\"bastions\":%d,"
        "\"policy_move\":%d,\"policy_prayer\":%d,"
        "\"policy_target\":%d,\"policy_gear\":%d,"
        "\"policy_eat\":%d,\"policy_potion\":%d,"
        "\"policy_spell\":%d,\"policy_spec\":%d,"
        "\"policy_offensive\":%d,\"valid_target_actions\":%d,"
        "\"selected_target_npc\":%d,\"selected_target_type\":%d,"
        "\"selected_action_valid\":[",
        env->stall_trace_id,
        s->tick,
        env->stall_trace_ticks,
        s->wave + 1,
        s->reward,
        s->episode_return,
        s->player.x,
        s->player.y,
        s->player.current_hitpoints,
        s->player.current_prayer,
        s->player.attack_timer,
        s->player_dest_x,
        s->player_dest_y,
        s->tick_scratch.player_moved,
        s->player.prayer,
        s->player.offensive_prayer,
        s->weapon_set,
        s->player.current_ranged,
        s->player.current_magic,
        s->player.brew_doses,
        s->player.restore_doses,
        s->player.bastion_doses,
        decision->selected_action_value[INF_HEAD_MOVE],
        decision->selected_action_value[INF_HEAD_PRAYER],
        decision->selected_action_value[INF_HEAD_TARGET],
        decision->selected_action_value[INF_HEAD_GEAR],
        decision->selected_action_value[INF_HEAD_EAT],
        decision->selected_action_value[INF_HEAD_POTION],
        decision->selected_action_value[INF_HEAD_SPELL],
        decision->selected_action_value[INF_HEAD_SPEC],
        decision->selected_action_value[INF_HEAD_OFFENSIVE],
        decision->valid_target_actions,
        decision->selected_target_npc,
        decision->selected_target_type);

    for (int h = 0; h < INF_NUM_ACTION_HEADS; h++) {
        if (h > 0) fprintf(env->stall_trace_file, ",");
        fprintf(env->stall_trace_file, "%d", decision->selected_action_valid[h]);
    }
    fprintf(env->stall_trace_file,
        "],\"current_target_slot\":%d,\"current_target_type\":%d,"
        "\"current_target_attackable\":%d,\"current_target_in_range\":%d,"
        "\"current_target_los\":%d,",
        current_target_slot,
        current_target_type,
        current_target_attackable,
        current_target_in_range,
        current_target_los);
    inferno_stall_trace_write_npcs(env->stall_trace_file, s);
    fprintf(env->stall_trace_file, "}\n");
    env->stall_trace_rows++;
}

static int inferno_trace_find_zuk_hp(const InfernoState* s) {
    int zuk_idx = inf_find_live_zuk_idx(s);
    return zuk_idx >= 0 ? s->npcs[zuk_idx].hp : 0;
}

static void inferno_trace_pending_sparks(
    const InfernoState* s,
    int* count,
    int* min_ticks
) {
    *count = 0;
    *min_ticks = -1;
    for (int i = 0; i < INF_MAX_PENDING_SPARKS; i++) {
        if (!s->pending_sparks[i].active) continue;
        (*count)++;
        if (*min_ticks < 0 || s->pending_sparks[i].ticks_remaining < *min_ticks)
            *min_ticks = s->pending_sparks[i].ticks_remaining;
    }
}

static void inferno_trace_set_state(
    const InfernoState* s,
    int* active_count,
    int* ranger_alive,
    int* mager_alive,
    int* meleer_alive,
    int* ranger_hp,
    int* mager_hp,
    int* set_targeting_player,
    int* set_targeting_shield,
    int* min_attack_timer
) {
    *active_count = 0;
    *ranger_alive = 0;
    *mager_alive = 0;
    *meleer_alive = 0;
    *ranger_hp = 0;
    *mager_hp = 0;
    *set_targeting_player = 0;
    *set_targeting_shield = 0;
    *min_attack_timer = -1;
    for (int i = 0; i < INF_MAX_NPCS; i++) {
        const InfNPC* npc = &s->npcs[i];
        if (!npc->active || npc->death_ticks != 0 || npc->hp <= 0)
            continue;
        if (!inf_npc_type_is_set_pressure(npc->type))
            continue;
        (*active_count)++;
        if (*min_attack_timer < 0 || npc->attack_timer < *min_attack_timer)
            *min_attack_timer = npc->attack_timer;
        if (npc->type == INF_NPC_RANGER) {
            *ranger_alive = 1;
            *ranger_hp += npc->hp;
        } else if (npc->type == INF_NPC_MAGER) {
            *mager_alive = 1;
            *mager_hp += npc->hp;
        } else if (npc->type == INF_NPC_MELEER) {
            *meleer_alive = 1;
        }
        if (npc->aggro_target < 0)
            (*set_targeting_player)++;
        else if (npc->aggro_target == s->zuk.shield_idx)
            (*set_targeting_shield)++;
    }
}

static void inferno_trace_jad_state(
    const InfernoState* s,
    int* alive,
    int* next_style
) {
    *alive = 0;
    *next_style = ATTACK_STYLE_NONE;
    for (int i = 0; i < INF_MAX_NPCS; i++) {
        const InfNPC* npc = &s->npcs[i];
        if (!npc->active || npc->death_ticks != 0 || npc->hp <= 0)
            continue;
        if (npc->type != INF_NPC_JAD)
            continue;
        *alive = 1;
        *next_style = inf_npc_jad_const(npc)->attack_style;
        return;
    }
}

static void inferno_post_240_trace_close(Env* env, const char* reason) {
    if (!env->post_240_trace_file)
        return;

    InfernoState* s = INF_ENV_INFERNO(env);
    fprintf(env->post_240_trace_file,
        "{\"kind\":\"end\",\"reason\":\"%s\",\"trace_id\":%d,"
        "\"tick\":%d,\"winner\":%d,\"last_hit_by_type\":%d,"
        "\"killed_by_zuk\":%d,\"killed_by_ranger\":%d,"
        "\"killed_by_mager\":%d,\"killed_by_healer_zuk\":%d,"
        "\"all_healers_dead_tick\":%d,\"rows\":%d,\"truncated\":%d}\n",
        reason,
        env->post_240_trace_id,
        s->tick,
        s->winner,
        s->last_hit_by_type,
        s->killed_by_type[INF_NPC_ZUK],
        s->killed_by_type[INF_NPC_RANGER],
        s->killed_by_type[INF_NPC_MAGER],
        s->killed_by_type[INF_NPC_HEALER_ZUK],
        s->tick_at_all_zuk_healers_dead,
        env->post_240_trace_rows,
        env->post_240_trace_truncated);
    if (fclose(env->post_240_trace_file) != 0) {
        fprintf(stderr, "POST240_TRACE: cannot close trace file\n");
        abort();
    }
    env->post_240_trace_file = NULL;
    env->post_240_trace_active = 0;
}

static void inferno_post_240_trace_open(Env* env, const InfernoState* s) {
    if (env->post_240_trace_file)
        return;
    if (s->tick_at_le_240 < 0)
        return;
    if (s->start_wave != env->config_start_wave)
        return;

    int id = inferno_post_240_trace_reserve_id();
    if (id < 0)
        return;

    char path[1400];
    int n = snprintf(path, sizeof(path),
        "%s/trace_%06d_env%04d_seed%u_oracle%d.jsonl",
        g_post_240_trace_dir,
        id,
        env->rng,
        env->episode_rng_start,
        INF_ENV_INFERNO_CONTEXT(env)->config.oracle_mode);
    if (n < 0 || (size_t)n >= sizeof(path)) {
        fprintf(stderr, "POST240_TRACE path too long\n");
        abort();
    }

    env->post_240_trace_file = fopen(path, "w");
    if (!env->post_240_trace_file) {
        fprintf(stderr, "POST240_TRACE: cannot open %s\n", path);
        abort();
    }
    env->post_240_trace_id = id;
    env->post_240_trace_active = 1;
    env->post_240_trace_rows = 0;
    env->post_240_trace_truncated = 0;
    fprintf(env->post_240_trace_file,
        "{\"kind\":\"meta\",\"trace_id\":%d,\"env_idx\":%d,"
        "\"rng_start\":%u,\"oracle_mode\":%d,\"start_wave\":%d,"
        "\"tick_at_le_240\":%d,\"tick_at_zuk_healer_spawn\":%d}\n",
        id,
        env->rng,
        env->episode_rng_start,
        INF_ENV_INFERNO_CONTEXT(env)->config.oracle_mode,
        s->start_wave + 1,
        s->tick_at_le_240,
        s->tick_at_zuk_healer_spawn);
}

static void inferno_post_240_trace_write_healers(
    FILE* fp,
    const InfernoState* s,
    const InfernoContext* ctx
) {
    for (int h = 0; h < 4; h++) {
        int obs_slot = 33 + h;
        int npc_idx = s->current_obs_slots[obs_slot];
        int alive = 0;
        int hp = 0;
        int tagged = 0;
        int attackable = 0;
        int in_range = 0;
        int targeted = 0;
        int hit_this_tick = 0;
        int healing_zuk = 0;
        int attack_timer = -1;
        if (npc_idx >= 0 && npc_idx < INF_MAX_NPCS) {
            const InfNPC* npc = &s->npcs[npc_idx];
            alive = npc->active && npc->death_ticks == 0 && npc->hp > 0 &&
                npc->type == INF_NPC_HEALER_ZUK;
            if (alive) {
                hp = npc->hp;
                tagged = !inf_is_untagged_live_zuk_healer_slot(s, npc_idx);
                attackable = inf_obs_slot_is_targetable((InfernoState*)s, ctx, obs_slot);
                in_range = inf_player_can_attack_npc_from_current_tile(s, npc_idx);
                targeted = osrs_interaction_active(&s->interaction) &&
                    s->interaction.target_slot == npc_idx;
                hit_this_tick = s->tick_scratch.player_attacked &&
                    s->player_attack_npc_idx == npc_idx;
                healing_zuk = inf_is_untagged_live_zuk_healer_slot(s, npc_idx);
                attack_timer = npc->attack_timer;
            }
        }
        fprintf(fp,
            ",\"h%d_alive\":%d,\"h%d_hp\":%d,\"h%d_tagged\":%d,"
            "\"h%d_attackable\":%d,\"h%d_in_range\":%d,"
            "\"h%d_targeted\":%d,\"h%d_hit\":%d,"
            "\"h%d_healing_zuk\":%d,\"h%d_attack_timer\":%d",
            h, alive,
            h, hp,
            h, tagged,
            h, attackable,
            h, in_range,
            h, targeted,
            h, hit_this_tick,
            h, healing_zuk,
            h, attack_timer);
    }
}

static void inferno_post_240_trace_capture(Env* env, int is_term) {
    InfernoState* s = INF_ENV_INFERNO(env);
    if (!g_post_240_trace_initialized)
        inferno_post_240_trace_init_once();
    if (!g_post_240_trace_dir[0] || g_post_240_trace_max <= 0)
        return;

    inferno_post_240_trace_open(env, s);
    if (!env->post_240_trace_file)
        return;

    if (env->post_240_trace_rows < g_post_240_trace_tick_cap) {
        int spark_count = 0;
        int spark_min_ticks = -1;
        int set_count = 0;
        int ranger_alive = 0;
        int mager_alive = 0;
        int meleer_alive = 0;
        int ranger_hp = 0;
        int mager_hp = 0;
        int set_targeting_player = 0;
        int set_targeting_shield = 0;
        int set_attack_timer_min = -1;
        int jad_alive = 0;
        int jad_next_style = ATTACK_STYLE_NONE;
        int shield_x = -1;
        int shield_y = -1;
        int shield_active = 0;
        int target_slot = osrs_interaction_active(&s->interaction)
            ? s->interaction.target_slot : -1;
        int target_type = -1;
        int target_attackable = 0;
        int target_in_range = 0;
        int target_is_healer = 0;
        int target_is_set = 0;
        int target_is_zuk = 0;
        if (s->zuk.shield_idx >= 0 && s->zuk.shield_idx < INF_MAX_NPCS) {
            const InfNPC* shield = &s->npcs[s->zuk.shield_idx];
            shield_active = shield->active && shield->death_ticks == 0 && shield->hp > 0;
            if (shield_active) {
                shield_x = shield->x;
                shield_y = shield->y;
            }
        }
        if (target_slot >= 0 && target_slot < INF_MAX_NPCS) {
            const InfNPC* target = &s->npcs[target_slot];
            target_type = target->type;
            target_attackable =
                inf_obs_slot_is_targetable(
                    s,
                    INF_ENV_INFERNO_CONTEXT(env),
                    inf_find_target_obs_slot(s, target_slot));
            target_in_range = inf_player_can_attack_npc_from_current_tile(s, target_slot);
            target_is_healer = target_type == INF_NPC_HEALER_ZUK;
            target_is_set = inf_npc_type_is_set_pressure(target_type);
            target_is_zuk = target_type == INF_NPC_ZUK;
        }

        inferno_trace_pending_sparks(s, &spark_count, &spark_min_ticks);
        inferno_trace_set_state(s, &set_count, &ranger_alive, &mager_alive,
            &meleer_alive, &ranger_hp, &mager_hp, &set_targeting_player,
            &set_targeting_shield, &set_attack_timer_min);
        inferno_trace_jad_state(s, &jad_alive, &jad_next_style);

        fprintf(env->post_240_trace_file,
            "{\"kind\":\"tick\",\"trace_id\":%d,\"tick\":%d,"
            "\"tick_since_240\":%d,\"terminal\":%d,\"winner\":%d,"
            "\"zuk_hp\":%d,\"zuk_hp_max_after_healer_spawn\":%.3f,"
            "\"player_x\":%d,\"player_y\":%d,\"player_hp\":%d,"
            "\"player_prayer\":%d,\"player_attack_timer\":%d,"
            "\"active_overhead\":%d,\"offensive_prayer\":%d,"
            "\"current_weapon\":%d,\"brews\":%d,\"restores\":%d,"
            "\"shield_x\":%d,\"shield_y\":%d,\"shield_dir\":%d,"
            "\"shield_freeze\":%d,\"shield_active\":%d,"
            "\"behind_shield\":%d,\"offshield_after_240\":%d,"
            "\"pending_spark_count\":%d,\"pending_spark_min_ticks\":%d,"
            "\"spark_damage_this_tick\":%.3f,"
            "\"set_alive_count\":%d,\"set_ranger_alive\":%d,"
            "\"set_mager_alive\":%d,\"set_meleer_alive\":%d,"
            "\"set_ranger_hp\":%d,\"set_mager_hp\":%d,"
            "\"set_targeting_player\":%d,\"set_targeting_shield\":%d,"
            "\"set_attack_timer_min\":%d,"
            "\"jad_alive\":%d,\"jad_attack_style_next\":%d,"
            "\"policy_move\":%d,\"policy_prayer\":%d,\"policy_target\":%d,"
            "\"policy_gear\":%d,\"policy_eat\":%d,\"policy_potion\":%d,"
            "\"policy_spell\":%d,\"policy_spec\":%d,\"policy_offensive\":%d,"
            "\"current_target_slot\":%d,\"current_target_type\":%d,"
            "\"target_is_healer\":%d,\"target_is_set\":%d,\"target_is_zuk\":%d,"
            "\"target_attackable\":%d,\"target_in_range\":%d,"
            "\"actual_attack_fired\":%d,\"actual_attack_target\":%d,"
            "\"hit_landed\":%d,\"hit_damage\":%d,"
            "\"damage_zuk_this_tick\":%.3f,\"damage_set_this_tick\":%.3f,"
            "\"damage_zuk_healers_this_tick\":%.3f,"
            "\"total_zuk_healer_tags\":%d,\"total_zuk_healer_kills\":%d,"
            "\"tick_at_first_zuk_healer_target\":%d,"
            "\"tick_at_first_zuk_healer_attack\":%d,"
            "\"tick_at_first_zuk_healer_tag\":%d,"
            "\"tick_at_all_zuk_healers_tagged\":%d,"
            "\"tick_at_all_zuk_healers_dead\":%d,"
            "\"tick_at_first_zuk_hit_after_all_healers_dead\":%d",
            env->post_240_trace_id,
            s->tick,
            s->tick_at_le_240 >= 0 ? s->tick - s->tick_at_le_240 : -1,
            is_term,
            s->winner,
            inferno_trace_find_zuk_hp(s),
            s->zuk_hp_max_after_healer_spawn,
            s->player.x,
            s->player.y,
            s->player.current_hitpoints,
            s->player.current_prayer,
            s->player.attack_timer,
            s->player.prayer,
            s->player.offensive_prayer,
            s->weapon_set,
            s->player.brew_doses,
            s->player.restore_doses,
            shield_x,
            shield_y,
            s->zuk.shield_dir,
            s->zuk.shield_freeze,
            shield_active,
            inf_player_behind_zuk_shield_now(s),
            s->offshield_ticks_after_240,
            spark_count,
            spark_min_ticks,
            s->tick_scratch.spark_damage,
            set_count,
            ranger_alive,
            mager_alive,
            meleer_alive,
            ranger_hp,
            mager_hp,
            set_targeting_player,
            set_targeting_shield,
            set_attack_timer_min,
            jad_alive,
            jad_next_style,
            env->acts_staging[INF_HEAD_MOVE],
            env->acts_staging[INF_HEAD_PRAYER],
            env->acts_staging[INF_HEAD_TARGET],
            env->acts_staging[INF_HEAD_GEAR],
            env->acts_staging[INF_HEAD_EAT],
            env->acts_staging[INF_HEAD_POTION],
            env->acts_staging[INF_HEAD_SPELL],
            env->acts_staging[INF_HEAD_SPEC],
            env->acts_staging[INF_HEAD_OFFENSIVE],
            target_slot,
            target_type,
            target_is_healer,
            target_is_set,
            target_is_zuk,
            target_attackable,
            target_in_range,
            s->tick_scratch.player_attacked,
            s->player_attack_npc_idx,
            s->tick_scratch.player_attacked && s->player_attack_dmg > 0,
            s->player_attack_dmg,
            s->tick_scratch.damage_zuk,
            s->tick_scratch.damage_set,
            s->tick_scratch.damage_zuk_healers,
            s->total_zuk_healer_tags,
            s->total_zuk_healer_kills,
            s->tick_at_first_zuk_healer_target,
            s->tick_at_first_zuk_healer_attack,
            s->tick_at_first_zuk_healer_tag,
            s->tick_at_all_zuk_healers_tagged,
            s->tick_at_all_zuk_healers_dead,
            s->tick_at_first_zuk_hit_after_all_healers_dead);
        inferno_post_240_trace_write_healers(
            env->post_240_trace_file, s, INF_ENV_INFERNO_CONTEXT(env));
        fprintf(env->post_240_trace_file, "}\n");
        env->post_240_trace_rows++;
    } else {
        env->post_240_trace_truncated = 1;
    }

    if (is_term)
        inferno_post_240_trace_close(env, "terminal");
}

static inline void inferno_env_write_obs_mask(Env* env) {
    float* obs = (float*)env->observations;
    ENCOUNTER_INFERNO.write_obs(INF_ENV_STATE(env), INF_ENV_CONTEXT(env), obs);
    ENCOUNTER_INFERNO.write_mask(INF_ENV_STATE(env), INF_ENV_CONTEXT(env), obs + INF_NUM_OBS);
}

static inline void inferno_env_write_post_restore_state(Env* env) {
    inferno_env_write_obs_mask(env);
    env->rewards[0] = 0.0f;
    env->term_staging = 0;
    env->terminals[0] = 0.0f;
}

static inline void inferno_env_refresh_after_state_load(Env* env) {
    inf_refresh_after_state_load(INF_ENV_INFERNO(env), INF_ENV_INFERNO_CONTEXT(env));
    inferno_env_write_post_restore_state(env);
}

static inline void inferno_env_mark_episode_start(Env* env) {
    env->episode_rng_start = INF_ENV_INFERNO(env)->rng_state;
}

static int inferno_terminal_shield_active(const InfernoState* s) {
    int si = s->zuk.shield_idx;
    return si >= 0 &&
        s->npcs[si].active &&
        s->npcs[si].death_ticks == 0 &&
        s->npcs[si].hp > 0;
}

static int inferno_terminal_behind_shield(const InfernoState* s) {
    if (!inferno_terminal_shield_active(s)) return 0;

    int si = s->zuk.shield_idx;
    int sx = s->npcs[si].x;
    int sz = INF_NPC_STATS[INF_NPC_ZUK_SHIELD].size;
    return s->player.x >= sx &&
        s->player.x < sx + sz &&
        s->player.y >= 41;
}

void c_step(Env* env) {
    int inf_prof_enabled = INF_PROFILE_ENABLED();
    double inf_prof_total_t0 = inf_prof_enabled ? INF_PROFILE_NOW_MS() : 0.0;
    double inf_prof_t0 = inf_prof_total_t0;
    int used_human_commands = 0;
    RenderClient* render_client = (RenderClient*)env->render_env.client;

    /* replay playback: if this env has a loaded replay, override policy actions */
    if (env->replay_actions && env->replay_cursor < env->replay_num_ticks) {
        int off = env->replay_cursor * NUM_ATNS;
        for (int i = 0; i < NUM_ATNS; i++)
            env->acts_staging[i] = env->replay_actions[off + i];
        env->replay_cursor++;
        if (render_client) {
            human_input_clear_pending(&render_client->human_input);
            human_input_clear_move(&render_client->human_input);
            ENCOUNTER_INFERNO.put_int(INF_ENV_STATE(env), INF_ENV_CONTEXT(env), "player_dest_x", -1);
            ENCOUNTER_INFERNO.put_int(INF_ENV_STATE(env), INF_ENV_CONTEXT(env), "player_dest_y", -1);
            ENCOUNTER_INFERNO.put_int(INF_ENV_STATE(env), INF_ENV_CONTEXT(env), "human_command_mode", 0);
        }
    } else if (render_client && render_client->human_input.enabled &&
               ENCOUNTER_INFERNO.step_human_commands) {
        if (env->episode_actions && render_client->human_input.commands.count > 0) {
            fprintf(stderr, "RECORD_REPLAY cannot record human command mode\n");
            abort();
        }
        ENCOUNTER_INFERNO.step_human_commands(INF_ENV_STATE(env), INF_ENV_CONTEXT(env), &render_client->human_input);
        used_human_commands = 1;
    } else {
        if (render_client) {
            human_input_clear_pending(&render_client->human_input);
            human_input_clear_move(&render_client->human_input);
            ENCOUNTER_INFERNO.put_int(INF_ENV_STATE(env), INF_ENV_CONTEXT(env), "player_dest_x", -1);
            ENCOUNTER_INFERNO.put_int(INF_ENV_STATE(env), INF_ENV_CONTEXT(env), "player_dest_y", -1);
            ENCOUNTER_INFERNO.put_int(INF_ENV_STATE(env), INF_ENV_CONTEXT(env), "human_command_mode", 0);
        }
        for (int i = 0; i < NUM_ATNS; i++)
            env->acts_staging[i] = (int)env->actions[i];
    }

    INF_PROFILE_MARK(INF_PROF_C_ACTIONS);
    float action_mask_before[INF_ACTION_MASK_SIZE];
    memcpy(action_mask_before,
        (float*)env->observations + INF_NUM_OBS,
        sizeof(action_mask_before));
    InfernoStallTraceDecision stall_decision;
    inferno_stall_trace_capture_decision(
        env, action_mask_before, &stall_decision);

    /* buffer actions for best-episode recording */
    if (env->episode_actions && !used_human_commands) {
        if (env->episode_action_len == 0) {
            env->episode_rng_start = INF_ENV_INFERNO(env)->rng_state;
            ENCOUNTER_INFERNO.snapshot(
                INF_ENV_STATE(env),
                INF_ENV_CONTEXT(env),
                &env->episode_initial_snapshot);
            env->episode_initial_snapshot_valid = 1;
        }
        if (env->episode_action_len < env->episode_action_cap) {
            memcpy(&env->episode_actions[env->episode_action_len * NUM_ATNS],
                   env->acts_staging, NUM_ATNS * sizeof(int));
            env->episode_action_len++;
        }
    }

    INF_PROFILE_MARK(INF_PROF_C_PRE_STEP_TRACES);
    if (!used_human_commands)
        ENCOUNTER_INFERNO.step(INF_ENV_STATE(env), INF_ENV_CONTEXT(env), env->acts_staging);
    INF_PROFILE_MARK(INF_PROF_C_ENCOUNTER_STEP);

    float* obs = (float*)env->observations;
    ENCOUNTER_INFERNO.write_obs(INF_ENV_STATE(env), INF_ENV_CONTEXT(env), obs);
    INF_PROFILE_MARK(INF_PROF_C_WRITE_OBS);
    ENCOUNTER_INFERNO.write_mask(INF_ENV_STATE(env), INF_ENV_CONTEXT(env), obs + INF_NUM_OBS);
    INF_PROFILE_MARK(INF_PROF_C_WRITE_MASK);

    env->rewards[0] = ENCOUNTER_INFERNO.get_reward(INF_ENV_STATE(env), INF_ENV_CONTEXT(env));

    int is_term = ENCOUNTER_INFERNO.is_terminal(INF_ENV_STATE(env), INF_ENV_CONTEXT(env));
    env->term_staging = (unsigned char)is_term;
    env->terminals[0] = (float)is_term;
    INF_PROFILE_MARK(INF_PROF_C_REWARD_TERMINAL);

    if (!used_human_commands)
        inferno_stall_trace_capture(env, &stall_decision, is_term);
    inferno_post_240_trace_capture(env, is_term);

    INF_PROFILE_MARK(INF_PROF_C_POST_STEP_TRACES);

    /* terminal-only logging: accumulate completed episode stats into env->log.
       vecenv polls with static_vec_log() which sums across agents, divides by
       total n, then clears. this gives proper per-completed-episode averages
       instead of noisy mid-episode snapshots. */
    if (is_term) {
        InfernoState* s = INF_ENV_INFERNO(env);
        inf_write_terminal_status_text(s, env->render_status_text,
            sizeof(env->render_status_text));
        env->render_status_frames =
            env->render_status_text[0] != '\0' ? INF_RENDER_STATUS_FRAMES : 0;
        float min_zuk_hp_term = (s->winner == 0)
            ? 0.0f
            : (s->min_zuk_hp_seen > 0.0f ? s->min_zuk_hp_seen : 1200.0f);
        int terminal_shield_active = inferno_terminal_shield_active(s);
        int terminal_behind_shield = inferno_terminal_behind_shield(s);

        /* only count episodes that match the configured start_wave.
           curriculum agents (overridden wave) are excluded from metrics. */
        if (s->start_wave != env->config_start_wave) goto skip_log;

        env->log.episode_return += s->episode_return;
        env->log.episode_length += (float)s->tick;
        env->log.damage_dealt += s->total_damage_dealt;
        env->log.zuk_healer_damage += s->total_zuk_healer_damage;
        env->log.damage_received += s->total_damage_received;
        env->log.hp_restored += s->total_hp_restored;
        env->log.wins += (s->winner == 0) ? 1.0f : 0.0f;
        env->log.wave += (float)s->wave;
        env->log.prayer_correct += (float)s->total_prayer_correct;
        env->log.prayer_total += (float)s->total_npc_attacks;
        env->log.idle_ticks += (float)s->total_idle_ticks;
        env->log.brews_used += (float)s->total_brews_used;
        env->log.blood_healed += (float)s->total_blood_healed;
        env->log.unavoidable_off_prayer += (float)s->total_unavoidable_off;
        env->log.ranger_mager_same_tick_attacks +=
            (float)s->total_ranger_mager_same_tick_attacks;
        env->log.step_out_ranger_mager_same_tick_attacks +=
            (float)s->total_step_out_ranger_mager_same_tick_attacks;
        env->log.brews_remaining += (float)s->player.brew_doses;
        env->log.restores_remaining += (float)s->player.restore_doses;
        env->log.prayer_at_death += (float)s->player.current_prayer;
        env->log.npc_kills += (float)s->total_npc_kills;
        env->log.gear_switches += (float)s->total_gear_switches;
        env->log.current_ranged += (float)s->player.current_ranged;
        env->log.current_magic += (float)s->player.current_magic;
        env->log.start_wave += (float)env->config_start_wave;

        for (int t = 0; t < INF_NUM_NPC_TYPES; t++) {
            env->log.prayer_correct_by_type[t] += (float)s->prayer_correct_by_type[t];
            env->log.attacks_by_type[t] += (float)s->attacks_by_type[t];
            env->log.dmg_from_type[t] += s->dmg_from_type[t];
            env->log.killed_by_type[t] += (float)s->killed_by_type[t];
        }

        /* Zuk shield tracking */
        env->log.behind_shield_pct += (s->total_zuk_ticks > 0)
            ? (float)s->behind_shield_ticks / (float)s->total_zuk_ticks : 0.0f;

        /* Zuk HP remaining at episode end */
        {
            float zhp = 1200.0f;
            for (int n = 0; n < INF_MAX_NPCS; n++) {
                if (s->npcs[n].type == INF_NPC_ZUK) {
                    zhp = (float)s->npcs[n].hp;
                    break;
                }
            }
            if (s->winner == 0) zhp = 0.0f;
            env->log.zuk_hp_remaining += zhp;
        }
        env->log.min_zuk_hp_seen += min_zuk_hp_term;

        env->log.n += 1.0f;

        {
            env->log.episode_return_normal += s->episode_return;
            env->log.wins_normal += (s->winner == 0) ? 1.0f : 0.0f;
            env->log.min_zuk_hp_normal += min_zuk_hp_term;
            env->log.n_normal += 1.0f;
            int won = (s->winner == 0);
            int phase_bucket = won ? 4
                : (min_zuk_hp_term <= 300.0f) ? 3
                : (min_zuk_hp_term <= 600.0f) ? 2
                : (min_zuk_hp_term <= 900.0f) ? 1 : 0;
            env->log.phase_reached_normal_sum += (float)phase_bucket;
            if (!won) {
                env->log.episode_length_normal_died += (float)s->tick;
                env->log.n_normal_died += 1.0f;
                env->log.brews_remaining_normal_died += (float)s->player.brew_doses;
                env->log.restores_remaining_normal_died += (float)s->player.restore_doses;
                env->log.prayer_at_death_normal_died += (float)s->player.current_prayer;
                if (terminal_shield_active)
                    env->log.count_died_with_shield_active_normal += 1.0f;
                if (terminal_behind_shield)
                    env->log.count_died_behind_shield_normal += 1.0f;
                for (int t = 0; t < INF_NUM_NPC_TYPES; t++)
                    env->log.killed_by_type_normal[t] += (float)s->killed_by_type[t];
            }
            if (min_zuk_hp_term <= 300.0f) env->log.count_min_hp_le_300_normal += 1.0f;
            if (min_zuk_hp_term <= 240.0f) env->log.count_min_hp_le_240_normal += 1.0f;
            if (min_zuk_hp_term <= 150.0f) env->log.count_min_hp_le_150_normal += 1.0f;
            if (min_zuk_hp_term < env->log.best_min_zuk_hp_normal)
                env->log.best_min_zuk_hp_normal = min_zuk_hp_term;

            /* Per-threshold survival and damage, accumulated only after crossing. */
            if (s->tick_at_le_300 >= 0) {
                env->log.ticks_after_300_normal_sum += (float)(s->tick - s->tick_at_le_300);
                env->log.damage_after_300_normal_sum += s->damage_after_300;
            }
            if (s->tick_at_le_240 >= 0) {
                env->log.ticks_after_240_normal_sum += (float)(s->tick - s->tick_at_le_240);
                env->log.damage_after_240_normal_sum += s->damage_after_240;
            }
            if (s->tick_at_le_150 >= 0) {
                env->log.ticks_after_150_normal_sum += (float)(s->tick - s->tick_at_le_150);
                env->log.damage_after_150_normal_sum += s->damage_after_150;
            }
            if (s->tick_at_zuk_healer_spawn >= 0 || s->zuk.healer_spawned) {
                env->log.count_healer_spawned_normal += 1.0f;
                env->log.zuk_hp_max_after_healer_spawn_normal_sum +=
                    s->zuk_hp_max_after_healer_spawn;
            }
            env->log.shield_tags_normal_sum += (float)s->total_shield_tags;
            if (s->total_shield_tags >= 1)
                env->log.count_shield_tags_ge_1_normal += 1.0f;
            if (s->total_zuk_healer_tags >= 1)
                env->log.count_zuk_healers_tagged_ge_1_normal += 1.0f;
            if (s->total_zuk_healer_tags >= 2)
                env->log.count_zuk_healers_tagged_ge_2_normal += 1.0f;
            if (s->total_zuk_healer_tags >= 4)
                env->log.count_zuk_healers_tagged_ge_4_normal += 1.0f;
            if (s->total_zuk_healer_kills >= 1)
                env->log.count_zuk_healers_killed_ge_1_normal += 1.0f;
            if (s->total_zuk_healer_kills >= 2)
                env->log.count_zuk_healers_killed_ge_2_normal += 1.0f;
            if (s->total_zuk_healer_kills >= 4)
                env->log.count_zuk_healers_killed_ge_4_normal += 1.0f;
            if (s->tick_at_all_zuk_healers_dead >= 0) {
                env->log.count_all_zuk_healers_dead_normal += 1.0f;
                float post_healer_survival =
                    (float)(s->tick - s->tick_at_all_zuk_healers_dead);
                env->log.post_healer_survival_ticks_normal_sum +=
                    post_healer_survival;
                env->log.damage_after_all_zuk_healers_dead_normal_sum +=
                    s->damage_after_all_zuk_healers_dead;
                env->log.zuk_hp_at_all_zuk_healers_dead_normal_sum +=
                    s->zuk_hp_at_all_zuk_healers_dead;
                env->log.offshield_ticks_after_all_zuk_healers_dead_normal_sum +=
                    (float)s->offshield_ticks_after_all_zuk_healers_dead;
                if (post_healer_survival >= 20.0f || won)
                    env->log.count_healer_resolved_20_normal += 1.0f;
                if (s->damage_after_all_zuk_healers_dead > 0.0f)
                    env->log.count_reengaged_zuk_after_healers_normal += 1.0f;
                if (s->tick_at_first_zuk_hit_after_all_healers_dead >= 0) {
                    env->log.ticks_all_healers_dead_to_first_zuk_hit_normal_sum +=
                        (float)(s->tick_at_first_zuk_hit_after_all_healers_dead -
                            s->tick_at_all_zuk_healers_dead);
                }
            }
            if (s->total_zuk_healer_target_ticks >= 1)
                env->log.count_zuk_healers_targeted_ge_1_normal += 1.0f;
            if (s->total_zuk_healer_attack_fires >= 1)
                env->log.count_zuk_healers_attacked_ge_1_normal += 1.0f;
            if (s->total_zuk_healer_attackable_ticks >= 1)
                env->log.count_zuk_healers_attackable_ge_1_normal += 1.0f;
            env->log.zuk_untagged_healer_target_bonus_coeff_normal_sum +=
                INF_ENV_INFERNO_CONTEXT(env)->config.zuk_untagged_healer_target_bonus_coeff;
            env->log.zuk_safe_untagged_healer_target_bonus_coeff_normal_sum +=
                INF_ENV_INFERNO_CONTEXT(env)->config.zuk_safe_untagged_healer_target_bonus_coeff;
            env->log.zuk_healer_reward_mode_normal_sum +=
                (float)INF_ENV_INFERNO_CONTEXT(env)->config.zuk_healer_reward_mode;
            env->log.zuk_untagged_healer_targets_normal_sum +=
                (float)s->total_zuk_untagged_healer_targets;
            env->log.zuk_safe_untagged_healer_targets_normal_sum +=
                (float)s->total_zuk_safe_untagged_healer_targets;
            env->log.zuk_unsafe_untagged_healer_targets_normal_sum +=
                (float)s->total_zuk_unsafe_untagged_healer_targets;
            env->log.zuk_untagged_healer_target_reward_count_normal_sum +=
                (float)s->total_zuk_untagged_healer_target_rewards;
            env->log.zuk_safe_untagged_healer_target_reward_count_normal_sum +=
                (float)s->total_zuk_safe_untagged_healer_target_rewards;
            env->log.post_healer_set_damage_reward_coeff_normal_sum +=
                INF_ENV_INFERNO_CONTEXT(env)->config.post_healer_set_damage_reward_coeff;
            env->log.post_healer_set_kill_bonus_coeff_normal_sum +=
                INF_ENV_INFERNO_CONTEXT(env)->config.post_healer_set_kill_bonus;
            env->log.post_healer_set_alive_penalty_coeff_normal_sum +=
                INF_ENV_INFERNO_CONTEXT(env)->config.post_healer_set_alive_tick_penalty_coeff;
            env->log.post_healer_set_alive_penalty_cap_normal_sum +=
                INF_ENV_INFERNO_CONTEXT(env)->config.post_healer_set_alive_penalty_cap;
            env->log.post_healer_set_damage_reward_normal_sum +=
                s->post_healer_set_damage_reward_total;
            env->log.post_healer_set_kill_bonus_reward_normal_sum +=
                s->post_healer_set_kill_bonus_total;
            env->log.post_healer_set_alive_penalty_normal_sum +=
                s->post_healer_set_alive_penalty_total;
            env->log.post_healer_set_pressure_normal_sum +=
                s->post_healer_set_pressure_total;
            env->log.action_mask_checks_normal_sum +=
                (float)s->total_action_mask_checks;
            env->log.target_head_valid_healer_count_normal_sum +=
                (float)s->target_head_valid_healer_count;
            env->log.target_head_valid_zuk_count_normal_sum +=
                (float)s->target_head_valid_zuk_count;
            env->log.target_head_valid_set_count_normal_sum +=
                (float)s->target_head_valid_set_count;
            if (s->total_action_mask_checks > 0) {
                for (int h = 0; h < 9; h++) {
                    env->log.zero_valid_action_head_count_normal_sum[h] +=
                        (float)s->zero_valid_action_head_count[h];
                    env->log.valid_action_count_min_by_head_normal_sum[h] +=
                        (float)s->min_valid_action_count_by_head[h];
                }
            }
            if (s->total_zuk_healer_target_ticks >= 1) {
                env->log.zuk_healer_target_cannot_attack_ticks_normal_sum +=
                    (float)s->total_zuk_healer_cannot_attack_ticks;
                env->log.zuk_healer_target_cooldown_ticks_normal_sum +=
                    (float)s->total_zuk_healer_cooldown_ticks;
                env->log.zuk_healer_target_out_of_range_ticks_normal_sum +=
                    (float)s->total_zuk_healer_out_of_range_ticks;
                env->log.zuk_healer_target_attackable_ticks_normal_sum +=
                    (float)s->total_zuk_healer_attackable_ticks;
            }
            if (s->tick_at_le_240 >= 0) {
                env->log.hp_restored_after_240_normal_sum += s->hp_restored_after_240;
                env->log.spark_damage_after_240_normal_sum += s->spark_damage_after_240;
                env->log.offshield_ticks_after_240_normal_sum +=
                    (float)s->offshield_ticks_after_240;
                if (s->tick_at_first_zuk_healer_target >= 0) {
                    env->log.ticks_240_to_first_healer_target_normal_sum +=
                        (float)(s->tick_at_first_zuk_healer_target - s->tick_at_le_240);
                }
                if (s->tick_at_first_zuk_healer_attack >= 0) {
                    env->log.ticks_240_to_first_healer_attack_normal_sum +=
                        (float)(s->tick_at_first_zuk_healer_attack - s->tick_at_le_240);
                }
                if (s->tick_at_first_zuk_healer_tag >= 0) {
                    env->log.ticks_240_to_first_healer_tag_normal_sum +=
                        (float)(s->tick_at_first_zuk_healer_tag - s->tick_at_le_240);
                }
                if (s->tick_at_all_zuk_healers_tagged >= 0) {
                    env->log.ticks_240_to_all_healers_tagged_normal_sum +=
                        (float)(s->tick_at_all_zuk_healers_tagged - s->tick_at_le_240);
                }
                if (s->tick_at_all_zuk_healers_dead >= 0) {
                    env->log.ticks_240_to_all_healers_dead_normal_sum +=
                        (float)(s->tick_at_all_zuk_healers_dead - s->tick_at_le_240);
                }
            }
            env->log.redemption_proc_opportunities_normal_sum +=
                (float)s->redemption_proc_opportunities;
            env->log.redemption_zero_hit_proc_opportunities_normal_sum +=
                (float)s->redemption_zero_hit_proc_opportunities;
            env->log.redemption_proc_opportunities_after_240_normal_sum +=
                (float)s->redemption_proc_opportunities_after_240;
            env->log.redemption_heal_potential_normal_sum +=
                s->redemption_heal_potential;
            env->log.redemption_heal_potential_after_240_normal_sum +=
                s->redemption_heal_potential_after_240;
            env->log.redemption_deaths_from_band_normal +=
                (float)s->redemption_deaths_from_band;
            env->log.redemption_deaths_from_band_after_240_normal +=
                (float)s->redemption_deaths_from_band_after_240;
            env->log.redemption_deaths_from_above_band_normal +=
                (float)s->redemption_deaths_from_above_band;
            env->log.redemption_action_count_normal_sum +=
                (float)s->redemption_action_count;
            env->log.redemption_active_ticks_normal_sum +=
                (float)s->redemption_active_ticks;
            env->log.redemption_proc_count_normal_sum +=
                (float)s->redemption_proc_count;
            env->log.redemption_zero_hit_proc_count_normal_sum +=
                (float)s->redemption_zero_hit_proc_count;
            env->log.redemption_heal_done_normal_sum +=
                s->redemption_heal_done;
            for (int t = 0; t < INF_NUM_NPC_TYPES; t++) {
                env->log.redemption_proc_opportunities_by_type_normal[t] +=
                    (float)s->redemption_proc_opportunities_by_type[t];
                env->log.redemption_zero_hit_proc_opportunities_by_type_normal[t] +=
                    (float)s->redemption_zero_hit_proc_opportunities_by_type[t];
                env->log.redemption_heal_potential_by_type_normal[t] +=
                    s->redemption_heal_potential_by_type[t];
                env->log.redemption_deaths_from_band_by_type_normal[t] +=
                    (float)s->redemption_deaths_from_band_by_type[t];
            }
            /* Terminal death pressure by phase. */
            if (!won) {
                int jad_alive = 0, zuk_healer_alive = 0, jad_healer_alive = 0, set_alive = 0;
                for (int n = 0; n < INF_MAX_NPCS; n++) {
                    if (s->npcs[n].hp <= 0) continue;
                    int t = s->npcs[n].type;
                    if (t == INF_NPC_JAD) jad_alive = 1;
                    else if (t == INF_NPC_HEALER_ZUK) zuk_healer_alive = 1;
                    else if (t == INF_NPC_HEALER_JAD) jad_healer_alive = 1;
                    else if (inf_npc_type_is_set_pressure(t)) set_alive = 1;
                }
                if (jad_alive) env->log.count_died_with_jad_alive_normal += 1.0f;
                if (zuk_healer_alive || jad_healer_alive)
                    env->log.count_died_with_healer_alive_normal += 1.0f;
                if (zuk_healer_alive) env->log.count_died_with_zuk_healer_alive_normal += 1.0f;
                if (jad_healer_alive) env->log.count_died_with_jad_healer_alive_normal += 1.0f;
                if (set_alive) env->log.count_died_with_set_alive_normal += 1.0f;
                if (s->tick_at_le_240 >= 0) {
                    env->log.count_died_after_240_normal += 1.0f;
                    env->log.brews_remaining_after_240_death_normal_sum +=
                        (float)s->player.brew_doses;
                    env->log.restores_remaining_after_240_death_normal_sum +=
                        (float)s->player.restore_doses;
                    env->log.prayer_at_death_after_240_normal_sum +=
                        (float)s->player.current_prayer;
                    if (terminal_shield_active)
                        env->log.count_died_after_240_shield_active_normal += 1.0f;
                    if (terminal_behind_shield)
                        env->log.count_died_after_240_behind_shield_normal += 1.0f;
                    if (s->tick_at_all_zuk_healers_dead >= 0 ||
                            s->total_zuk_healer_kills >= 4) {
                        env->log.count_died_after_240_all_healers_dead_normal += 1.0f;
                        if (set_alive)
                            env->log.count_died_after_all_healers_dead_with_set_alive_normal += 1.0f;
                        if (s->killed_by_type[INF_NPC_ZUK] > 0)
                            env->log.count_died_after_all_healers_dead_killed_by_zuk_normal += 1.0f;
                        if (s->killed_by_type[INF_NPC_RANGER] > 0)
                            env->log.count_died_after_all_healers_dead_killed_by_ranger_normal += 1.0f;
                        if (s->killed_by_type[INF_NPC_MAGER] > 0)
                            env->log.count_died_after_all_healers_dead_killed_by_mager_normal += 1.0f;
                        if (terminal_shield_active)
                            env->log.count_died_after_all_healers_dead_with_shield_active_normal += 1.0f;
                        if (terminal_behind_shield)
                            env->log.count_died_after_all_healers_dead_behind_shield_normal += 1.0f;
                        env->log.brews_remaining_after_all_healers_dead_death_normal_sum +=
                            (float)s->player.brew_doses;
                        env->log.restores_remaining_after_all_healers_dead_death_normal_sum +=
                            (float)s->player.restore_doses;
                    } else if (s->total_zuk_healer_kills > 0) {
                        env->log.count_died_after_240_some_healers_killed_normal += 1.0f;
                    } else if (s->total_zuk_healer_tags > 0) {
                        env->log.count_died_after_240_some_healers_tagged_normal += 1.0f;
                    } else {
                        env->log.count_died_after_240_never_tagged_healer_normal += 1.0f;
                    }
                }
            }
        }
    skip_log:;
    }

    if (is_term) {
        /* check if this episode is a new global best — if so, flush replay to disk.
           for full runs (start_wave 0): best = highest wave reached, then fewest ticks.
           for zuk-only (start_wave 68+): best = most damage to zuk (lowest zuk HP), then fewest ticks.
           curriculum starts from mid-waves also record. */
        if (env->episode_actions && env->episode_action_len > 0) {
            InfernoState* st = INF_ENV_INFERNO(env);
            int wave = st->wave;
            int ticks = env->episode_action_len;
            int min_zuk_hp = (st->winner == 0)
                ? 0
                : (st->min_zuk_hp_seen > 0.0f ? (int)st->min_zuk_hp_seen : 1200);

            inferno_replay_lock_best();
            int is_new_best = inferno_replay_is_better(
                &g_best_replay,
                st->start_wave,
                wave,
                ticks,
                min_zuk_hp,
                env->episode_rng_start);
            if (is_new_best) {
                inferno_replay_best_apply(
                    &g_best_replay, wave, ticks, min_zuk_hp, env->episode_rng_start);
                const char* rpath = getenv("RECORD_REPLAY");
                if (rpath && rpath[0]) {
                    inferno_replay_write_or_abort(
                        rpath,
                        env->episode_action_len,
                        env->episode_rng_start,
                        env->episode_actions,
                        env->episode_initial_snapshot_valid ?
                            &env->episode_initial_snapshot : NULL);
                    if (st->start_wave >= 68) {
                        fprintf(stderr, "replay: new best min zuk hp=%d (%d ticks, rng=%u) saved to %s\n",
                                g_best_replay.min_zuk_hp, env->episode_action_len, env->episode_rng_start, rpath);
                    } else {
                        fprintf(stderr, "replay: new best wave %d (%d ticks, rng=%u) saved to %s\n",
                                wave, env->episode_action_len, env->episode_rng_start, rpath);
                    }
                }
            }
            inferno_replay_unlock_best();
        }
        env->episode_action_len = 0;
        env->episode_initial_snapshot_valid = 0;
        INF_PROFILE_MARK(INF_PROF_C_TERMINAL_LOG);

        ENCOUNTER_INFERNO.reset(INF_ENV_STATE(env), INF_ENV_CONTEXT(env), 0);
        ENCOUNTER_INFERNO.write_obs(INF_ENV_STATE(env), INF_ENV_CONTEXT(env), obs);
        ENCOUNTER_INFERNO.write_mask(INF_ENV_STATE(env), INF_ENV_CONTEXT(env), obs + INF_NUM_OBS);
        inferno_env_mark_episode_start(env);
        env->pending_render_reset = 1;
        INF_PROFILE_MARK(INF_PROF_C_RESET);
    }
    if (inf_prof_enabled)
        INF_PROFILE_ADD(INF_PROF_C_STEP_TOTAL, INF_PROFILE_NOW_MS() - inf_prof_total_t0);
}

void c_reset(Env* env) {
    inferno_post_240_trace_close(env, "reset");
    inferno_stall_trace_close(env, "reset");
    env->stall_trace_ticks = 0;
    uint32_t seed = env->replay_actions ? env->replay_rng_seed : 0;
    if (env->replay_actions && env->replay_has_initial_snapshot) {
        ENCOUNTER_INFERNO.restore(
            INF_ENV_STATE(env),
            INF_ENV_CONTEXT(env),
            &env->replay_initial_snapshot,
            sizeof(env->replay_initial_snapshot));
    } else {
        ENCOUNTER_INFERNO.reset(INF_ENV_STATE(env), INF_ENV_CONTEXT(env), seed);
    }
    env->replay_cursor = 0;
    inferno_env_mark_episode_start(env);
    inferno_env_write_post_restore_state(env);
}

void c_close(Env* env) {
    inferno_post_240_trace_close(env, "close");
    inferno_stall_trace_close(env, "close");
    free(env->episode_actions);
    env->episode_actions = NULL;
    free(env->replay_actions);
    env->replay_actions = NULL;
    ENCOUNTER_INFERNO.destroy_context(INF_ENV_CONTEXT(env));
    if (env->render_env.client) {
        render_destroy_client((RenderClient*)env->render_env.client);
        env->render_env.client = NULL;
    }
}

static void inferno_env_apply_render_status_overlay(Env* env, RenderClient* rc) {
    if (env->render_status_frames <= 0 || env->render_status_text[0] == '\0')
        return;
    rc->encounter_overlay.status_text_active = 1;
    snprintf(rc->encounter_overlay.status_text,
        sizeof(rc->encounter_overlay.status_text),
        "%s", env->render_status_text);
}

void c_render(Env* env) {
    OsrsEnv* re = &env->render_env;
    re->encounter_def = (void*)&ENCOUNTER_INFERNO;
    re->encounter_state = INF_ENV_STATE(env);
    re->encounter_context = INF_ENV_CONTEXT(env);
    re->tick = ENCOUNTER_INFERNO.get_tick(
        INF_ENV_STATE(env), INF_ENV_CONTEXT(env));

    int first_call = (re->client == NULL);
    if (first_call) {
        re->client = render_make_client();
        RenderClient* rc = (RenderClient*)re->client;
        rc->ticks_per_second = env->ticks_per_second;
        EncounterSceneConfig scene = {
            .required_groups = {
                OSRS_ASSET_GROUP_INFERNO,
                OSRS_ASSET_GROUP_COMBAT_VISUALS,
                (OsrsAssetGroupKind)-1,
                (OsrsAssetGroupKind)-1,
            },
            .terrain_path = OSRS_ASSET("inferno.terrain"),
            .objects_path = OSRS_ASSET("inferno.objects"),
            .objects_secondary_path = OSRS_ASSET("inferno_zuk.objects"),
            .npc_models_path = OSRS_ASSET("inferno.models"),
            .npc_anims_path = OSRS_ASSET("inferno.anims"),
            /* inferno region (35,83) starts at world (2246, 5315). */
            .world_origin_x = 2246,
            .world_origin_y = 5315,
        };
        encounter_load_scene_assets(rc, &scene);

        /* inferno renders in encounter-local tiles, but render_make_client()
           initializes the camera to wilderness PvP world coords. mirror the
           standalone viewer's post-load bootstrap so the first live frame uses
           inferno arena bounds and entity positions. */
        render_populate_entities(rc, re);
        rc->cam_target_x = (float)rc->arena_base_x + (float)rc->arena_width / 2.0f;
        rc->cam_target_z = -((float)rc->arena_base_y + (float)rc->arena_height / 2.0f);
        for (int i = 0; i < rc->entity_count; i++) {
            int size = rc->entities[i].npc_size > 1 ? rc->entities[i].npc_size : 1;
            rc->sub_x[i] = rc->entities[i].x * 128 + size * 64;
            rc->sub_y[i] = rc->entities[i].y * 128 + size * 64;
            rc->dest_x[i] = rc->sub_x[i];
            rc->dest_y[i] = rc->sub_y[i];
        }
        env->last_step_time = GetTime();
    }
    RenderClient* rc = (RenderClient*)re->client;
    if (!rc) return;

    /* post-reset cleanup: c_step raised the flag after resetting the encounter.
       mirror osrs_visual.c:186-201 so damage splats, in-flight effects, stale
       inventory state, and last-frame sub-tile coordinates (from dead-player
       body) don't leak into the next episode. */
    if (env->pending_render_reset) {
        render_reset_episode_visual_state(rc, re);
        env->pending_render_reset = 0;
    }

    /* update NPC visual positions once per tick (not per frame).
       render_post_tick snapshots the existing rc->entities before repopulating
       so it can detect new NPC identities and clear stale splats/HP bars. */
    render_post_tick(rc, re);
    inferno_env_apply_render_status_overlay(env, rc);
    if (env->render_status_frames > 0) env->render_status_frames--;

    /* Match the standalone viewer's visual_frame pattern: render until the
       next sim tick is due. pvp_render scales the client-tick clock by replay
       speed, so high-speed evals still drain projectile flights and effects. */
    if (rc->ticks_per_second <= 0.0f) {
        pvp_render(re);
        env->last_step_time = GetTime();
        return;
    }
    float tps = render_effective_ticks_per_second(rc);
    double interval = 1.0 / (double)tps;
    double deadline = env->last_step_time + interval;
    int rendered = 0;
    while (GetTime() < deadline) {
        pvp_render(re);
        rendered = 1;
    }
    if (!rendered) {
        pvp_render(re);
    }
    env->last_step_time = GetTime();
}

typedef InfernoState State;

static inline void puffer_state_refresh(Env* env) {
    inferno_env_refresh_after_state_load(env);
}

	#define MY_VEC_INIT
	#include "vecenv.h"

/* max episode length for action buffer (INF_MAX_TICKS from encounter) */
#define REPLAY_MAX_TICKS INF_MAX_TICKS

static void inferno_env_put_float(Env* env, const char* key, float value) {
    ENCOUNTER_INFERNO.put_float(
        INF_ENV_STATE(env),
        INF_ENV_CONTEXT(env),
        key,
        value);
}

static void inferno_env_put_int(Env* env, const char* key, int value) {
    ENCOUNTER_INFERNO.put_int(
        INF_ENV_STATE(env),
        INF_ENV_CONTEXT(env),
        key,
        value);
}

static void inferno_apply_obs_profile(Env* env, int obs_profile) {
    switch (obs_profile) {
        case 0:
            inferno_env_put_int(env, "step_out_forecast_obs_enabled", 0);
            break;
        case 1:
            inferno_env_put_int(env, "step_out_forecast_obs_enabled", 1);
            break;
        default:
            fprintf(stderr, "obs_profile must be 0 or 1, got %d\n", obs_profile);
            abort();
    }
}

static void inferno_zero_phase_reward_coeffs(Env* env) {
    static const char* const keys[] = {
        "jad_damage_reward_coeff",
        "zuk_healer_damage_reward_coeff",
        "set_damage_reward_coeff",
        "jad_kill_bonus",
        "zuk_healer_kill_bonus",
        "set_kill_bonus",
        "post_healer_zuk_damage_coeff",
        "zuk_healer_phase_hp_delta_coeff",
        "post_healer_set_damage_reward_coeff",
        "post_healer_set_kill_bonus",
        "post_healer_set_alive_tick_penalty_coeff",
        "post_healer_set_alive_penalty_cap",
        "zuk_untagged_healer_tick_penalty_coeff",
        "zuk_untagged_healer_target_bonus_coeff",
        "zuk_safe_untagged_healer_target_bonus_coeff",
    };
    for (size_t i = 0; i < sizeof(keys) / sizeof(*keys); i++)
        inferno_env_put_float(env, keys[i], 0.0f);
    inferno_env_put_float(env, "post_jad_zuk_multiplier", 1.0f);
    inferno_env_put_float(env, "jad_alive_zuk_multiplier", 1.0f);
}

static void inferno_apply_reward_profile(Env* env, int reward_profile) {
    switch (reward_profile) {
        case 0:
            inferno_env_put_int(env, "joseph_reward_mode", 0);
            inferno_env_put_float(env, "damage_reward_coeff", 0.0f);
            inferno_env_put_float(env, "shield_penalty_coeff", 0.0f);
            inferno_env_put_float(env, "tag_reward_coeff", 0.0f);
            inferno_env_put_float(env, "shield_tag_reward_coeff", 0.0f);
            inferno_env_put_float(
                env,
                "zuk_untagged_healer_nonmagic_attack_bonus_coeff",
                0.0f);
            inferno_env_put_float(env, "zuk_healer_mage_attack_penalty_coeff", 0.0f);
            inferno_zero_phase_reward_coeffs(env);
            break;
        case 1:
            inferno_env_put_int(env, "joseph_reward_mode", 0);
            break;
        case 2:
            inferno_env_put_int(env, "joseph_reward_mode", 1);
            inferno_env_put_float(env, "shield_tag_reward_coeff", 0.0f);
            inferno_zero_phase_reward_coeffs(env);
            break;
        default:
            fprintf(stderr, "reward_profile must be in [0,2], got %d\n", reward_profile);
            abort();
    }
}

void my_init(Env* env, Dict* kwargs) {
    env->num_agents = 1;
    ENCOUNTER_INFERNO.init_context(INF_ENV_CONTEXT(env));
    ENCOUNTER_INFERNO.init_state(INF_ENV_STATE(env), INF_ENV_CONTEXT(env));
    memset(&env->log, 0, sizeof(Log));
    env->log.best_min_zuk_hp_normal = 1200.0f;

    DictItem* start_wave = dict_get_unsafe(kwargs, "start_wave");
    if (start_wave)
        ENCOUNTER_INFERNO.put_int(INF_ENV_STATE(env), INF_ENV_CONTEXT(env), "start_wave", (int)start_wave->value);
    ENCOUNTER_INFERNO.put_float(
        INF_ENV_STATE(env), INF_ENV_CONTEXT(env), "damage_reward_coeff",
        (float)dict_get_unsafe(kwargs, "damage_reward_coeff")->value);
    ENCOUNTER_INFERNO.put_float(
        INF_ENV_STATE(env), INF_ENV_CONTEXT(env), "shield_penalty_coeff",
        (float)dict_get_unsafe(kwargs, "shield_penalty_coeff")->value);
    ENCOUNTER_INFERNO.put_float(
        INF_ENV_STATE(env), INF_ENV_CONTEXT(env), "tag_reward_coeff",
        (float)dict_get_unsafe(kwargs, "tag_reward_coeff")->value);
    static const char* const optional_float_keys[] = {
        "shield_tag_reward_coeff",
        "budget_loadout_fraction",
        "death_penalty_coeff",
        "phase_900_bonus", "phase_600_bonus", "phase_300_bonus",
        "shield_penalty_episode_cap",
        "supply_milestone_brew_reward_coeff",
        "supply_milestone_restore_reward_coeff",
        "jad_damage_reward_coeff", "zuk_healer_damage_reward_coeff",
        "set_damage_reward_coeff",
        "jad_kill_bonus", "zuk_healer_kill_bonus", "set_kill_bonus",
        "post_healer_zuk_damage_coeff", "zuk_healer_phase_hp_delta_coeff",
        "post_healer_set_damage_reward_coeff", "post_healer_set_kill_bonus",
        "post_healer_set_alive_tick_penalty_coeff",
        "post_healer_set_alive_penalty_cap",
        "zuk_untagged_healer_tick_penalty_coeff",
        "zuk_untagged_healer_target_bonus_coeff",
        "zuk_safe_untagged_healer_target_bonus_coeff",
        "zuk_untagged_healer_nonmagic_attack_bonus_coeff",
        "zuk_healer_mage_attack_penalty_coeff",
        "post_jad_zuk_multiplier", "jad_alive_zuk_multiplier",
    };
    for (size_t k = 0; k < sizeof(optional_float_keys)/sizeof(*optional_float_keys); k++) {
        DictItem* item = dict_get_unsafe(kwargs, optional_float_keys[k]);
        if (item) {
            ENCOUNTER_INFERNO.put_float(INF_ENV_STATE(env), INF_ENV_CONTEXT(env), optional_float_keys[k], (float)item->value);
        }
    }
    DictItem* supply_profile_scale =
        dict_get_unsafe(kwargs, "late_start_supply_profile_scale");
    if (supply_profile_scale) {
        ENCOUNTER_INFERNO.put_float(
            INF_ENV_STATE(env), INF_ENV_CONTEXT(env), "late_start_supply_profile_scale",
            (float)supply_profile_scale->value);
    }
    DictItem* oracle_mode = dict_get_unsafe(kwargs, "oracle_mode");
    if (oracle_mode) {
        ENCOUNTER_INFERNO.put_int(
            INF_ENV_STATE(env), INF_ENV_CONTEXT(env), "oracle_mode",
            (int)oracle_mode->value);
    }
    DictItem* terminal_penalty_enabled =
        dict_get_unsafe(kwargs, "terminal_penalty_enabled");
    if (terminal_penalty_enabled) {
        ENCOUNTER_INFERNO.put_int(
            INF_ENV_STATE(env), INF_ENV_CONTEXT(env), "terminal_penalty_enabled",
            (int)terminal_penalty_enabled->value);
    }
    DictItem* step_out_forecast_obs_enabled =
        dict_get_unsafe(kwargs, "step_out_forecast_obs_enabled");
    if (step_out_forecast_obs_enabled) {
        ENCOUNTER_INFERNO.put_int(
            INF_ENV_STATE(env), INF_ENV_CONTEXT(env), "step_out_forecast_obs_enabled",
            (int)step_out_forecast_obs_enabled->value);
    }
    DictItem* obs_profile = dict_get_unsafe(kwargs, "obs_profile");
    if (obs_profile) {
        inferno_apply_obs_profile(env, (int)obs_profile->value);
    }
    DictItem* loadout_profile_mode =
        dict_get_unsafe(kwargs, "loadout_profile_mode");
    if (loadout_profile_mode) {
        ENCOUNTER_INFERNO.put_int(
            INF_ENV_STATE(env), INF_ENV_CONTEXT(env), "loadout_profile_mode",
            (int)loadout_profile_mode->value);
    }
    DictItem* zuk_healer_reward_mode =
        dict_get_unsafe(kwargs, "zuk_healer_reward_mode");
    if (zuk_healer_reward_mode) {
        ENCOUNTER_INFERNO.put_int(
            INF_ENV_STATE(env), INF_ENV_CONTEXT(env), "zuk_healer_reward_mode",
            (int)zuk_healer_reward_mode->value);
    }
    DictItem* joseph_reward_mode =
        dict_get_unsafe(kwargs, "joseph_reward_mode");
    if (joseph_reward_mode) {
        ENCOUNTER_INFERNO.put_int(
            INF_ENV_STATE(env), INF_ENV_CONTEXT(env), "joseph_reward_mode",
            (int)joseph_reward_mode->value);
    }
    DictItem* reward_profile = dict_get_unsafe(kwargs, "reward_profile");
    if (reward_profile) {
        inferno_apply_reward_profile(env, (int)reward_profile->value);
    }
    DictItem* safe_healer_target_mask =
        dict_get_unsafe(kwargs, "zuk_safe_untagged_healer_target_mask");
    if (safe_healer_target_mask) {
        ENCOUNTER_INFERNO.put_int(
            INF_ENV_STATE(env), INF_ENV_CONTEXT(env), "zuk_safe_untagged_healer_target_mask",
            (int)safe_healer_target_mask->value);
    }
    DictItem* force_safe_healer_target_mask =
        dict_get_unsafe(kwargs, "zuk_force_safe_untagged_healer_target_mask");
    if (force_safe_healer_target_mask) {
        ENCOUNTER_INFERNO.put_int(
            INF_ENV_STATE(env), INF_ENV_CONTEXT(env),
            "zuk_force_safe_untagged_healer_target_mask",
            (int)force_safe_healer_target_mask->value);
    }
    /* match the 1-indexed → 0-indexed conversion done by encounter's put_int */
    int sw = start_wave ? (int)start_wave->value : 0;
    env->config_start_wave = (sw > 0) ? sw - 1 : 0;

    const char* record_path = getenv("RECORD_REPLAY");
    const char* play_path = getenv("PLAY_REPLAY");
    if (record_path && record_path[0] && play_path && play_path[0]) {
        fprintf(stderr, "RECORD_REPLAY and PLAY_REPLAY cannot both be set\n");
        abort();
    }

    if (record_path && record_path[0]) {
        env->episode_actions = (int*)malloc(REPLAY_MAX_TICKS * NUM_ATNS * sizeof(int));
        if (!env->episode_actions) {
            fprintf(stderr, "RECORD_REPLAY: out of memory\n");
            abort();
        }
        env->episode_action_cap = REPLAY_MAX_TICKS;
    } else {
        env->episode_actions = NULL;
        env->episode_action_cap = 0;
    }
    env->episode_action_len = 0;
    env->episode_initial_snapshot_valid = 0;

    /* playback: first env (env 0) loads the replay if PLAY_REPLAY is set.
       we use g_play_replay_loaded to ensure only one env loads it. */
    env->replay_actions = NULL;
    env->replay_num_ticks = 0;
    env->replay_cursor = 0;
    env->replay_rng_seed = 0;
    env->replay_has_initial_snapshot = 0;
    env->post_240_trace_file = NULL;
    env->post_240_trace_id = -1;
    env->post_240_trace_active = 0;
    env->post_240_trace_rows = 0;
    env->post_240_trace_truncated = 0;
    env->stall_trace_file = NULL;
    env->stall_trace_id = -1;
    env->stall_trace_rows = 0;
    env->stall_trace_truncated = 0;
    env->stall_trace_ticks = 0;
    env->ticks_per_second = 1.667f;
    env->last_step_time = 0.0;
    static int g_play_replay_loaded = 0;
    if (play_path && play_path[0] && !g_play_replay_loaded) {
        FILE* fp = fopen(play_path, "rb");
        if (!fp) {
            fprintf(stderr, "PLAY_REPLAY: cannot open %s\n", play_path);
            abort();
        }
        int num_ticks = 0;
        uint32_t rng_seed = 0;
        if (fread(&num_ticks, sizeof(int), 1, fp) != 1 ||
            fread(&rng_seed, sizeof(uint32_t), 1, fp) != 1 ||
            num_ticks <= 0 || num_ticks > REPLAY_MAX_TICKS) {
            fprintf(stderr, "PLAY_REPLAY: invalid replay header in %s\n", play_path);
            fclose(fp);
            abort();
        }
        int* buf = (int*)malloc(num_ticks * NUM_ATNS * sizeof(int));
        if (!buf) {
            fprintf(stderr, "PLAY_REPLAY: out of memory\n");
            fclose(fp);
            abort();
        }
        if (fread(buf, sizeof(int), num_ticks * NUM_ATNS, fp) != (size_t)(num_ticks * NUM_ATNS)) {
            fprintf(stderr, "PLAY_REPLAY: short read from %s\n", play_path);
            free(buf);
            fclose(fp);
            abort();
        }
        long payload_end = ftell(fp);
        if (payload_end < 0) {
            fprintf(stderr, "PLAY_REPLAY: ftell failed for %s\n", play_path);
            free(buf);
            fclose(fp);
            abort();
        }
        if (fseek(fp, 0, SEEK_END) != 0) {
            fprintf(stderr, "PLAY_REPLAY: seek failed for %s\n", play_path);
            free(buf);
            fclose(fp);
            abort();
        }
        long file_end = ftell(fp);
        if (file_end < 0 || fseek(fp, payload_end, SEEK_SET) != 0) {
            fprintf(stderr, "PLAY_REPLAY: seek failed for %s\n", play_path);
            free(buf);
            fclose(fp);
            abort();
        }
        InfSnapshot initial_snapshot;
        int has_initial_snapshot = 0;
        long remaining = file_end - payload_end;
        if (remaining == (long)sizeof(InfSnapshot)) {
            if (fread(&initial_snapshot, sizeof(initial_snapshot), 1, fp) != 1) {
                fprintf(stderr, "PLAY_REPLAY: short snapshot read from %s\n", play_path);
                free(buf);
                fclose(fp);
                abort();
            }
            has_initial_snapshot = 1;
        } else if (remaining != 0) {
            fprintf(stderr, "PLAY_REPLAY: unexpected trailing bytes in %s\n", play_path);
            free(buf);
            fclose(fp);
            abort();
        }
        fclose(fp);
        env->replay_actions = buf;
        env->replay_num_ticks = num_ticks;
        env->replay_rng_seed = rng_seed;
        env->replay_has_initial_snapshot = has_initial_snapshot;
        if (has_initial_snapshot) env->replay_initial_snapshot = initial_snapshot;
        g_play_replay_loaded = 1;
        fprintf(stderr, "PLAY_REPLAY: loaded %d ticks, rng=%u from %s\n",
                num_ticks, rng_seed, play_path);
        if (has_initial_snapshot) {
            ENCOUNTER_INFERNO.restore(
                INF_ENV_STATE(env),
                INF_ENV_CONTEXT(env),
                &env->replay_initial_snapshot,
                sizeof(env->replay_initial_snapshot));
        } else {
            ENCOUNTER_INFERNO.reset(INF_ENV_STATE(env), INF_ENV_CONTEXT(env), rng_seed);
        }

    }
}

/* curriculum wave mixing: start some agents at later waves for late-game gradient signal.
   base-start agents are scored normally; curriculum agents train but don't affect sweep metric. */
#define MAX_CURRICULUM_TIERS 8

Env* my_vec_init(int* num_envs_out, int* buffer_env_starts, int* buffer_env_counts,
                 Dict* vec_kwargs, Dict* env_kwargs) {
    int total_agents = (int)dict_get(vec_kwargs, "total_agents")->value;
    int num_buffers = (int)dict_get(vec_kwargs, "num_buffers")->value;
    int agents_per_buffer = total_agents / num_buffers;
    DictItem* base_start_wave_item = dict_get_unsafe(env_kwargs, "start_wave");
    int base_start_wave = base_start_wave_item ? (int)base_start_wave_item->value : 0;
    DictItem* classic_curriculum_mode_item =
        dict_get_unsafe(env_kwargs, "classic_curriculum_mode");
    int classic_curriculum_mode = classic_curriculum_mode_item
        ? (int)classic_curriculum_mode_item->value : 1;
    if (classic_curriculum_mode < 0 || classic_curriculum_mode > 1) {
        fprintf(stderr, "classic_curriculum_mode must be 0 or 1, got %d\n",
            classic_curriculum_mode);
        abort();
    }

    /* parse curriculum tiers from env config */
    static const char* wave_keys[] = {
        "curriculum_wave_1","curriculum_wave_2","curriculum_wave_3","curriculum_wave_4",
        "curriculum_wave_5","curriculum_wave_6","curriculum_wave_7","curriculum_wave_8",
    };
    static const char* frac_keys[] = {
        "curriculum_frac_1","curriculum_frac_2","curriculum_frac_3","curriculum_frac_4",
        "curriculum_frac_5","curriculum_frac_6","curriculum_frac_7","curriculum_frac_8",
    };
    int curriculum_waves[MAX_CURRICULUM_TIERS];
    float curriculum_fracs[MAX_CURRICULUM_TIERS];
    int num_tiers = 0;
    if (classic_curriculum_mode == 1) {
        for (int i = 0; i < MAX_CURRICULUM_TIERS; i++) {
            DictItem* w = dict_get_unsafe(env_kwargs, wave_keys[i]);
            DictItem* f = dict_get_unsafe(env_kwargs, frac_keys[i]);
            if (w && f && f->value > 0.0) {
                curriculum_waves[num_tiers] = (int)w->value;
                curriculum_fracs[num_tiers] = (float)f->value;
                num_tiers++;
            }
        }
    }

    /* allocate and init all envs (same as default my_vec_init) */
    Env* envs = (Env*)calloc(total_agents, sizeof(Env));
    int num_envs = 0;
    int agents_created = 0;
    while (agents_created < total_agents) {
        srand(num_envs);
        envs[num_envs].rng = num_envs;
        my_init(&envs[num_envs], env_kwargs);
        agents_created += envs[num_envs].num_agents;
        num_envs++;
    }
    envs = (Env*)realloc(envs, num_envs * sizeof(Env));

    /* assign curriculum start_waves to agents at the end of the array */
    if (num_tiers > 0) {
        int tier_counts[MAX_CURRICULUM_TIERS];
        int curriculum_total = 0;
        for (int t = 0; t < num_tiers; t++) {
            tier_counts[t] = (int)(curriculum_fracs[t] * num_envs);
            if (tier_counts[t] < 1) tier_counts[t] = 1;
            curriculum_total += tier_counts[t];
        }
        while (curriculum_total > num_envs) {
            for (int t = num_tiers - 1; t >= 0 && curriculum_total > num_envs; t--) {
                if (tier_counts[t] > 0) {
                    tier_counts[t]--;
                    curriculum_total--;
                }
            }
        }
        int base_count = num_envs - curriculum_total;
        int cursor = base_count;
        for (int t = 0; t < num_tiers; t++) {
            for (int i = 0; i < tier_counts[t] && cursor < num_envs; i++, cursor++) {
                ENCOUNTER_INFERNO.put_int(
                    INF_ENV_STATE(&envs[cursor]),
                    INF_ENV_CONTEXT(&envs[cursor]),
                    "start_wave",
                    curriculum_waves[t]);
            }
        }
        fprintf(stderr, "curriculum: %d wave-%d", base_count, base_start_wave);
        for (int t = 0; t < num_tiers; t++)
            fprintf(stderr, ", %d wave-%d", tier_counts[t], curriculum_waves[t]);
        fprintf(stderr, " (%d total)\n", num_envs);
    }

    /* fill buffer info (same as default) */
    int buf = 0;
    int buf_agents = 0;
    buffer_env_starts[0] = 0;
    buffer_env_counts[0] = 0;
    for (int i = 0; i < num_envs; i++) {
        buf_agents += envs[i].num_agents;
        buffer_env_counts[buf]++;
        if (buf_agents >= agents_per_buffer && buf < num_buffers - 1) {
            buf++;
            buffer_env_starts[buf] = i + 1;
            buffer_env_counts[buf] = 0;
            buf_agents = 0;
        }
    }

    *num_envs_out = num_envs;
    return envs;
}

static void inferno_set_death_cause_metrics(
    Dict* out,
    const char* const* keys,
    const float* counts,
    float denom
) {
    for (int t = 0; t < INF_NUM_NPC_TYPES; t++) {
        dict_set(out, keys[t], denom > 0.0f ? counts[t] / denom : 0.0f);
    }
}

void my_log(Log* log, Dict* out) {
    static const char* killed_by_normal_keys[] = {
        "frac_deaths_killed_by_nibbler_normal",
        "frac_deaths_killed_by_bat_normal",
        "frac_deaths_killed_by_blob_normal",
        "frac_deaths_killed_by_blob_mel_normal",
        "frac_deaths_killed_by_blob_rng_normal",
        "frac_deaths_killed_by_blob_mag_normal",
        "frac_deaths_killed_by_meleer_normal",
        "frac_deaths_killed_by_ranger_normal",
        "frac_deaths_killed_by_mager_normal",
        "frac_deaths_killed_by_jad_normal",
        "frac_deaths_killed_by_zuk_normal",
        "frac_deaths_killed_by_heal_jad_normal",
        "frac_deaths_killed_by_heal_zuk_normal",
        "frac_deaths_killed_by_shield_normal",
    };
    static const char* zero_valid_head_normal_keys[] = {
        "zero_valid_action_head_move_normal",
        "zero_valid_action_head_prayer_normal",
        "zero_valid_action_head_target_normal",
        "zero_valid_action_head_gear_normal",
        "zero_valid_action_head_eat_normal",
        "zero_valid_action_head_potion_normal",
        "zero_valid_action_head_spell_normal",
        "zero_valid_action_head_spec_normal",
        "zero_valid_action_head_offensive_normal",
    };
    static const char* min_valid_head_normal_keys[] = {
        "valid_action_count_min_move_normal",
        "valid_action_count_min_prayer_normal",
        "valid_action_count_min_target_normal",
        "valid_action_count_min_gear_normal",
        "valid_action_count_min_eat_normal",
        "valid_action_count_min_potion_normal",
        "valid_action_count_min_spell_normal",
        "valid_action_count_min_spec_normal",
        "valid_action_count_min_offensive_normal",
    };

    dict_set(out, "episode_return", log->episode_return);
    dict_set(out, "damage_dealt", log->damage_dealt);
    dict_set(out, "damage_received", log->damage_received);
    dict_set(out, "episode_length", log->episode_length);
    float damage_per_tick = log->episode_length > 0.0f
        ? log->damage_dealt / log->episode_length : 0.0f;
    dict_set(out, "damage_per_tick", damage_per_tick);
    dict_set(out, "damage_per_100_ticks", damage_per_tick * 100.0f);
    dict_set(out, "ticks_per_100_damage", log->damage_dealt > 0.0f
        ? 100.0f * log->episode_length / log->damage_dealt : 0.0f);
    dict_set(out, "wins", log->wins);
    dict_set(out, "wave", log->wave);
    dict_set(out, "idle_ticks", log->idle_ticks);
    dict_set(out, "brews_used", log->brews_used);
    dict_set(out, "blood_healed", log->blood_healed);

    /* prayer analysis: correct rate + unavoidable breakdown */
    float prayer_rate = (log->prayer_total > 0.0f)
        ? log->prayer_correct / log->prayer_total : 0.0f;
    dict_set(out, "prayer_correct_rate", prayer_rate);
    /* what fraction of off-prayer hits were unavoidable (multi-style same tick) */
    float off_prayer = log->prayer_total - log->prayer_correct;
    float unavoidable_rate = (off_prayer > 0.0f)
        ? log->unavoidable_off_prayer / off_prayer : 0.0f;
    dict_set(out, "unavoidable_off_prayer_rate", unavoidable_rate);
    dict_set(out, "brews_remaining", log->brews_remaining);
    dict_set(out, "restores_remaining", log->restores_remaining);
    dict_set(out, "prayer_at_death", log->prayer_at_death);
    dict_set(out, "ranger_mager_same_tick_attacks",
        log->ranger_mager_same_tick_attacks);
    dict_set(out, "step_out_ranger_mager_same_tick_attacks",
        log->step_out_ranger_mager_same_tick_attacks);

    dict_set(out, "current_ranged", log->current_ranged);
    dict_set(out, "current_magic", log->current_magic);
    dict_set(out, "behind_shield_pct", log->behind_shield_pct);
    dict_set(out, "zuk_hp_remaining", log->zuk_hp_remaining);
    dict_set(out, "min_zuk_hp_seen", log->min_zuk_hp_seen);
    dict_set(out, "hp_restored", log->hp_restored);
    dict_set(out, "zuk_healer_damage", log->zuk_healer_damage);
    dict_set(out, "deaths_to_jad", log->killed_by_type[INF_NPC_JAD] / log->n);
    if (log->n_normal > 0.0f) {
        float min_zhp_n = log->min_zuk_hp_normal / log->n_normal;
        float score_n = (1200.0f - min_zhp_n) / 1200.0f;
        float win_rate_n = log->wins_normal / log->n_normal;
        float frac_le_240_n = log->count_min_hp_le_240_normal / log->n_normal;
        float frac_le_150_n = log->count_min_hp_le_150_normal / log->n_normal;
        float frac_tagged_ge_1_n =
            log->count_zuk_healers_tagged_ge_1_normal / log->n_normal;
        float frac_tagged_ge_4_n =
            log->count_zuk_healers_tagged_ge_4_normal / log->n_normal;
        float frac_killed_ge_1_n =
            log->count_zuk_healers_killed_ge_1_normal / log->n_normal;
        float frac_all_healers_dead_n =
            log->count_all_zuk_healers_dead_normal / log->n_normal;
        float frac_died_with_zuk_healer_n =
            log->count_died_with_zuk_healer_alive_normal / log->n_normal;
        dict_set(out, "episode_return_normal", log->episode_return_normal / log->n_normal);
        dict_set(out, "wins_normal", win_rate_n);
        dict_set(out, "min_zuk_hp_normal", min_zhp_n);
        dict_set(out, "score_normal", score_n);
        dict_set(out, "zuk_objective_normal", score_n + 2.0f * win_rate_n);
        dict_set(out, "phase_reached_normal", log->phase_reached_normal_sum / log->n_normal);
        if (log->n_normal_died > 0.0f) {
            dict_set(out, "death_tick_normal", log->episode_length_normal_died / log->n_normal_died);
            dict_set(out, "brews_remaining_normal_died",
                log->brews_remaining_normal_died / log->n_normal_died);
            dict_set(out, "restores_remaining_normal_died",
                log->restores_remaining_normal_died / log->n_normal_died);
            dict_set(out, "prayer_at_death_normal_died",
                log->prayer_at_death_normal_died / log->n_normal_died);
            dict_set(out, "frac_deaths_with_shield_active_normal",
                log->count_died_with_shield_active_normal / log->n_normal_died);
            dict_set(out, "frac_deaths_behind_shield_normal",
                log->count_died_behind_shield_normal / log->n_normal_died);
            dict_set(out, "frac_deaths_after_240_normal",
                log->count_died_after_240_normal / log->n_normal_died);
            inferno_set_death_cause_metrics(out, killed_by_normal_keys,
                log->killed_by_type_normal, log->n_normal_died);
        }
        /* aggregator divides every Log field by n_total, so raw counts arrive
           as count/n_total. Dividing by n_normal (also count/n_total) cancels
           n_total and yields the true fraction-of-normal-episodes.
           best_min_zuk_hp_normal is intentionally not surfaced: averaging mins
           across envs is meaningless. Use the count grid instead. */
        dict_set(out, "frac_min_hp_le_300_normal", log->count_min_hp_le_300_normal / log->n_normal);
        dict_set(out, "frac_min_hp_le_240_normal", frac_le_240_n);
        dict_set(out, "frac_min_hp_le_150_normal", frac_le_150_n);
        dict_set(out, "frac_normal", log->n_normal);
        /* Conditional on having crossed the boundary. The
           aggregator divided everything by n_total, so dividing by
           count_min_hp_le_X (also divided by n_total) cancels n_total and
           gives the true conditional mean. Surface 0 when no eps crossed
           so pufferl's first-log metric registration always has the keys. */
        float t300 = log->count_min_hp_le_300_normal > 0.0f
            ? log->ticks_after_300_normal_sum / log->count_min_hp_le_300_normal : 0.0f;
        float t240 = log->count_min_hp_le_240_normal > 0.0f
            ? log->ticks_after_240_normal_sum / log->count_min_hp_le_240_normal : 0.0f;
        float t150 = log->count_min_hp_le_150_normal > 0.0f
            ? log->ticks_after_150_normal_sum / log->count_min_hp_le_150_normal : 0.0f;
        float d300 = log->count_min_hp_le_300_normal > 0.0f
            ? log->damage_after_300_normal_sum / log->count_min_hp_le_300_normal : 0.0f;
        float d240 = log->count_min_hp_le_240_normal > 0.0f
            ? log->damage_after_240_normal_sum / log->count_min_hp_le_240_normal : 0.0f;
        float d150 = log->count_min_hp_le_150_normal > 0.0f
            ? log->damage_after_150_normal_sum / log->count_min_hp_le_150_normal : 0.0f;
        dict_set(out, "ticks_after_300_normal", t300);
        dict_set(out, "ticks_after_240_normal", t240);
        dict_set(out, "ticks_after_150_normal", t150);
        dict_set(out, "damage_after_300_normal", d300);
        dict_set(out, "damage_after_240_normal", d240);
        dict_set(out, "damage_after_150_normal", d150);
        dict_set(out, "frac_healer_spawned_normal",
            log->count_healer_spawned_normal / log->n_normal);
        dict_set(out, "shield_tags_normal",
            log->shield_tags_normal_sum / log->n_normal);
        dict_set(out, "frac_shield_tags_ge_1_normal",
            log->count_shield_tags_ge_1_normal / log->n_normal);
        dict_set(out, "frac_zuk_healers_tagged_ge_1_normal", frac_tagged_ge_1_n);
        dict_set(out, "frac_zuk_healers_tagged_ge_2_normal",
            log->count_zuk_healers_tagged_ge_2_normal / log->n_normal);
        dict_set(out, "frac_zuk_healers_tagged_ge_4_normal", frac_tagged_ge_4_n);
        dict_set(out, "frac_zuk_healers_killed_ge_1_normal", frac_killed_ge_1_n);
        dict_set(out, "frac_zuk_healers_killed_ge_2_normal",
            log->count_zuk_healers_killed_ge_2_normal / log->n_normal);
        dict_set(out, "frac_zuk_healers_killed_ge_4_normal",
            log->count_zuk_healers_killed_ge_4_normal / log->n_normal);
        dict_set(out, "frac_all_zuk_healers_dead_normal", frac_all_healers_dead_n);
        dict_set(out, "frac_zuk_healers_targeted_ge_1_normal",
            log->count_zuk_healers_targeted_ge_1_normal / log->n_normal);
        dict_set(out, "frac_zuk_healers_attacked_ge_1_normal",
            log->count_zuk_healers_attacked_ge_1_normal / log->n_normal);
        dict_set(out, "frac_zuk_healers_attackable_ge_1_normal",
            log->count_zuk_healers_attackable_ge_1_normal / log->n_normal);
        float target_den = log->count_zuk_healers_targeted_ge_1_normal;
        dict_set(out, "zuk_healer_target_cannot_attack_ticks_normal",
            target_den > 0.0f
                ? log->zuk_healer_target_cannot_attack_ticks_normal_sum / target_den
                : 0.0f);
        dict_set(out, "zuk_healer_target_cooldown_ticks_normal",
            target_den > 0.0f
                ? log->zuk_healer_target_cooldown_ticks_normal_sum / target_den
                : 0.0f);
        dict_set(out, "zuk_healer_target_out_of_range_ticks_normal",
            target_den > 0.0f
                ? log->zuk_healer_target_out_of_range_ticks_normal_sum / target_den
                : 0.0f);
        dict_set(out, "zuk_healer_target_attackable_ticks_normal",
            target_den > 0.0f
                ? log->zuk_healer_target_attackable_ticks_normal_sum / target_den
                : 0.0f);
        float untagged_target_coeff =
            log->zuk_untagged_healer_target_bonus_coeff_normal_sum /
                log->n_normal;
        float safe_target_coeff =
            log->zuk_safe_untagged_healer_target_bonus_coeff_normal_sum /
                log->n_normal;
        dict_set(out, "zuk_untagged_healer_target_bonus_coeff_normal",
            untagged_target_coeff);
        dict_set(out, "zuk_safe_untagged_healer_target_bonus_coeff_normal",
            safe_target_coeff);
        dict_set(out, "zuk_healer_reward_mode_normal",
            log->zuk_healer_reward_mode_normal_sum / log->n_normal);
        dict_set(out, "zuk_untagged_healer_targets_normal",
            log->zuk_untagged_healer_targets_normal_sum / log->n_normal);
        dict_set(out, "zuk_safe_untagged_healer_targets_normal",
            log->zuk_safe_untagged_healer_targets_normal_sum / log->n_normal);
        dict_set(out, "zuk_unsafe_untagged_healer_targets_normal",
            log->zuk_unsafe_untagged_healer_targets_normal_sum / log->n_normal);
        dict_set(out, "zuk_untagged_healer_target_reward_count_normal",
            log->zuk_untagged_healer_target_reward_count_normal_sum /
                log->n_normal);
        dict_set(out, "zuk_safe_untagged_healer_target_reward_count_normal",
            log->zuk_safe_untagged_healer_target_reward_count_normal_sum /
                log->n_normal);
        dict_set(out, "zuk_untagged_healer_target_reward_normal",
            log->zuk_untagged_healer_target_reward_count_normal_sum *
                untagged_target_coeff / log->n_normal);
        dict_set(out, "zuk_safe_untagged_healer_target_reward_normal",
            log->zuk_safe_untagged_healer_target_reward_count_normal_sum *
                safe_target_coeff / log->n_normal);
        dict_set(out, "post_healer_set_damage_reward_coeff_normal",
            log->post_healer_set_damage_reward_coeff_normal_sum / log->n_normal);
        dict_set(out, "post_healer_set_kill_bonus_coeff_normal",
            log->post_healer_set_kill_bonus_coeff_normal_sum / log->n_normal);
        dict_set(out, "post_healer_set_alive_penalty_coeff_normal",
            log->post_healer_set_alive_penalty_coeff_normal_sum / log->n_normal);
        dict_set(out, "post_healer_set_alive_penalty_cap_normal",
            log->post_healer_set_alive_penalty_cap_normal_sum / log->n_normal);
        dict_set(out, "post_healer_set_damage_reward_normal",
            log->post_healer_set_damage_reward_normal_sum / log->n_normal);
        dict_set(out, "post_healer_set_kill_bonus_reward_normal",
            log->post_healer_set_kill_bonus_reward_normal_sum / log->n_normal);
        dict_set(out, "post_healer_set_alive_penalty_normal",
            log->post_healer_set_alive_penalty_normal_sum / log->n_normal);
        dict_set(out, "post_healer_set_pressure_normal",
            log->post_healer_set_pressure_normal_sum / log->n_normal);
        dict_set(out, "action_mask_checks_normal",
            log->action_mask_checks_normal_sum / log->n_normal);
        dict_set(out, "target_head_valid_healer_count_normal",
            log->target_head_valid_healer_count_normal_sum / log->n_normal);
        dict_set(out, "target_head_valid_zuk_count_normal",
            log->target_head_valid_zuk_count_normal_sum / log->n_normal);
        dict_set(out, "target_head_valid_set_count_normal",
            log->target_head_valid_set_count_normal_sum / log->n_normal);
        for (int h = 0; h < 9; h++) {
            dict_set(out, zero_valid_head_normal_keys[h],
                log->zero_valid_action_head_count_normal_sum[h] / log->n_normal);
            dict_set(out, min_valid_head_normal_keys[h],
                log->valid_action_count_min_by_head_normal_sum[h] /
                    log->n_normal);
        }
        float first_target_ticks = log->count_zuk_healers_targeted_ge_1_normal > 0.0f
            ? log->ticks_240_to_first_healer_target_normal_sum /
                log->count_zuk_healers_targeted_ge_1_normal : 0.0f;
        float first_attack_ticks = log->count_zuk_healers_attacked_ge_1_normal > 0.0f
            ? log->ticks_240_to_first_healer_attack_normal_sum /
                log->count_zuk_healers_attacked_ge_1_normal : 0.0f;
        float first_tag_ticks = log->count_zuk_healers_tagged_ge_1_normal > 0.0f
            ? log->ticks_240_to_first_healer_tag_normal_sum /
                log->count_zuk_healers_tagged_ge_1_normal : 0.0f;
        float all_tagged_ticks = log->count_zuk_healers_tagged_ge_4_normal > 0.0f
            ? log->ticks_240_to_all_healers_tagged_normal_sum /
                log->count_zuk_healers_tagged_ge_4_normal : 0.0f;
        float all_dead_ticks = log->count_all_zuk_healers_dead_normal > 0.0f
            ? log->ticks_240_to_all_healers_dead_normal_sum /
                log->count_all_zuk_healers_dead_normal : 0.0f;
        float healer_resolve_n =
            log->count_healer_resolved_20_normal / log->n_normal;
        float post_healer_survival_n =
            log->count_all_zuk_healers_dead_normal > 0.0f
                ? log->post_healer_survival_ticks_normal_sum /
                    log->count_all_zuk_healers_dead_normal : 0.0f;
        float post_healer_zuk_damage_n =
            log->count_all_zuk_healers_dead_normal > 0.0f
                ? log->damage_after_all_zuk_healers_dead_normal_sum /
                    log->count_all_zuk_healers_dead_normal : 0.0f;
        float reengaged_zuk_after_healers_n =
            log->count_reengaged_zuk_after_healers_normal / log->n_normal;
        float first_zuk_hit_after_all_dead_n =
            log->count_reengaged_zuk_after_healers_normal > 0.0f
                ? log->ticks_all_healers_dead_to_first_zuk_hit_normal_sum /
                    log->count_reengaged_zuk_after_healers_normal : 0.0f;
        float zuk_hp_when_all_dead_n =
            log->count_all_zuk_healers_dead_normal > 0.0f
                ? log->zuk_hp_at_all_zuk_healers_dead_normal_sum /
                    log->count_all_zuk_healers_dead_normal : 0.0f;
        float hp_restored_after_240 = log->count_min_hp_le_240_normal > 0.0f
            ? log->hp_restored_after_240_normal_sum /
                log->count_min_hp_le_240_normal : 0.0f;
        float spark_damage_after_240 = log->count_min_hp_le_240_normal > 0.0f
            ? log->spark_damage_after_240_normal_sum /
                log->count_min_hp_le_240_normal : 0.0f;
        float max_hp_after_spawn = log->count_healer_spawned_normal > 0.0f
            ? log->zuk_hp_max_after_healer_spawn_normal_sum /
                log->count_healer_spawned_normal : 0.0f;
        float offshield_after_240_n = log->count_min_hp_le_240_normal > 0.0f
            ? log->offshield_ticks_after_240_normal_sum /
                log->count_min_hp_le_240_normal : 0.0f;
        float offshield_after_all_dead_n = log->count_all_zuk_healers_dead_normal > 0.0f
            ? log->offshield_ticks_after_all_zuk_healers_dead_normal_sum /
                log->count_all_zuk_healers_dead_normal : 0.0f;
        dict_set(out, "ticks_240_to_first_healer_target_normal", first_target_ticks);
        dict_set(out, "ticks_240_to_first_healer_attack_normal", first_attack_ticks);
        dict_set(out, "ticks_240_to_first_healer_tag_normal", first_tag_ticks);
        dict_set(out, "ticks_240_to_all_healers_tagged_normal", all_tagged_ticks);
        dict_set(out, "ticks_240_to_all_healers_dead_normal", all_dead_ticks);
        dict_set(out, "healer_resolve_normal", healer_resolve_n);
        dict_set(out, "post_healer_objective_normal",
            healer_resolve_n + 0.001f * post_healer_zuk_damage_n -
                0.1f * frac_died_with_zuk_healer_n);
        dict_set(out, "post_healer_survival_ticks_normal", post_healer_survival_n);
        dict_set(out, "post_healer_zuk_damage_normal", post_healer_zuk_damage_n);
        dict_set(out, "frac_reengaged_zuk_after_healers_normal",
            reengaged_zuk_after_healers_n);
        dict_set(out, "ticks_all_healers_dead_to_first_zuk_hit_normal",
            first_zuk_hit_after_all_dead_n);
        dict_set(out, "zuk_hp_when_all_healers_dead_normal", zuk_hp_when_all_dead_n);
        dict_set(out, "offshield_ticks_after_240_normal", offshield_after_240_n);
        dict_set(out, "offshield_ticks_after_all_healers_dead_normal",
            offshield_after_all_dead_n);
        dict_set(out, "hp_restored_after_240_normal", hp_restored_after_240);
        dict_set(out, "zuk_hp_max_after_healer_spawn_normal", max_hp_after_spawn);
        dict_set(out, "spark_damage_after_240_normal", spark_damage_after_240);
        dict_set(out, "redemption_proc_opportunities_normal",
            log->redemption_proc_opportunities_normal_sum / log->n_normal);
        dict_set(out, "redemption_zero_hit_proc_opportunities_normal",
            log->redemption_zero_hit_proc_opportunities_normal_sum / log->n_normal);
        dict_set(out, "redemption_proc_opportunities_after_240_normal",
            log->redemption_proc_opportunities_after_240_normal_sum / log->n_normal);
        dict_set(out, "redemption_heal_potential_normal",
            log->redemption_heal_potential_normal_sum / log->n_normal);
        dict_set(out, "redemption_heal_potential_after_240_normal",
            log->redemption_heal_potential_after_240_normal_sum / log->n_normal);
        dict_set(out, "frac_redemption_deaths_from_band_normal",
            log->redemption_deaths_from_band_normal / log->n_normal);
        dict_set(out, "frac_redemption_deaths_from_band_after_240_normal",
            log->redemption_deaths_from_band_after_240_normal / log->n_normal);
        dict_set(out, "frac_redemption_deaths_from_above_band_normal",
            log->redemption_deaths_from_above_band_normal / log->n_normal);
        dict_set(out, "redemption_action_count_normal",
            log->redemption_action_count_normal_sum / log->n_normal);
        dict_set(out, "redemption_active_ticks_normal",
            log->redemption_active_ticks_normal_sum / log->n_normal);
        dict_set(out, "redemption_proc_count_normal",
            log->redemption_proc_count_normal_sum / log->n_normal);
        dict_set(out, "redemption_zero_hit_proc_count_normal",
            log->redemption_zero_hit_proc_count_normal_sum / log->n_normal);
        dict_set(out, "redemption_heal_done_normal",
            log->redemption_heal_done_normal_sum / log->n_normal);
        dict_set(out, "redemption_proc_opportunities_heal_zuk_normal",
            log->redemption_proc_opportunities_by_type_normal[INF_NPC_HEALER_ZUK] /
                log->n_normal);
        dict_set(out, "redemption_zero_hit_proc_opportunities_heal_zuk_normal",
            log->redemption_zero_hit_proc_opportunities_by_type_normal[INF_NPC_HEALER_ZUK] /
                log->n_normal);
        dict_set(out, "redemption_heal_potential_heal_zuk_normal",
            log->redemption_heal_potential_by_type_normal[INF_NPC_HEALER_ZUK] /
                log->n_normal);
        dict_set(out, "frac_redemption_deaths_from_band_heal_zuk_normal",
            log->redemption_deaths_from_band_by_type_normal[INF_NPC_HEALER_ZUK] /
                log->n_normal);
        dict_set(out, "redemption_proc_opportunities_ranger_normal",
            log->redemption_proc_opportunities_by_type_normal[INF_NPC_RANGER] /
                log->n_normal);
        dict_set(out, "redemption_zero_hit_proc_opportunities_ranger_normal",
            log->redemption_zero_hit_proc_opportunities_by_type_normal[INF_NPC_RANGER] /
                log->n_normal);
        dict_set(out, "redemption_proc_opportunities_mager_normal",
            log->redemption_proc_opportunities_by_type_normal[INF_NPC_MAGER] /
                log->n_normal);
        dict_set(out, "redemption_zero_hit_proc_opportunities_mager_normal",
            log->redemption_zero_hit_proc_opportunities_by_type_normal[INF_NPC_MAGER] /
                log->n_normal);
        dict_set(out, "redemption_proc_opportunities_zuk_normal",
            log->redemption_proc_opportunities_by_type_normal[INF_NPC_ZUK] /
                log->n_normal);
        dict_set(out, "redemption_zero_hit_proc_opportunities_zuk_normal",
            log->redemption_zero_hit_proc_opportunities_by_type_normal[INF_NPC_ZUK] /
                log->n_normal);
        dict_set(out, "redemption_proc_opportunities_jad_normal",
            log->redemption_proc_opportunities_by_type_normal[INF_NPC_JAD] /
                log->n_normal);
        dict_set(out, "redemption_zero_hit_proc_opportunities_jad_normal",
            log->redemption_zero_hit_proc_opportunities_by_type_normal[INF_NPC_JAD] /
                log->n_normal);
        if (log->n_normal_died > 0.0f) {
            dict_set(out, "frac_deaths_redemption_from_band_normal",
                log->redemption_deaths_from_band_normal / log->n_normal_died);
            dict_set(out, "frac_deaths_redemption_from_band_after_240_normal",
                log->redemption_deaths_from_band_after_240_normal /
                    log->n_normal_died);
        } else {
            dict_set(out, "frac_deaths_redemption_from_band_normal", 0.0f);
            dict_set(out, "frac_deaths_redemption_from_band_after_240_normal", 0.0f);
        }
        /* Death-cause fractions, out of normal-start episodes. */
        dict_set(out, "frac_died_with_jad_alive_normal",
            log->count_died_with_jad_alive_normal / log->n_normal);
        dict_set(out, "frac_died_with_healer_alive_normal",
            log->count_died_with_healer_alive_normal / log->n_normal);
        dict_set(out, "frac_died_with_zuk_healer_alive_normal",
            frac_died_with_zuk_healer_n);
        dict_set(out, "frac_died_with_jad_healer_alive_normal",
            log->count_died_with_jad_healer_alive_normal / log->n_normal);
        dict_set(out, "frac_died_with_set_alive_normal",
            log->count_died_with_set_alive_normal / log->n_normal);
        dict_set(out, "frac_died_after_240_never_tagged_healer_normal",
            log->count_died_after_240_never_tagged_healer_normal / log->n_normal);
        dict_set(out, "frac_died_after_240_some_healers_tagged_normal",
            log->count_died_after_240_some_healers_tagged_normal / log->n_normal);
        dict_set(out, "frac_died_after_240_some_healers_killed_normal",
            log->count_died_after_240_some_healers_killed_normal / log->n_normal);
        dict_set(out, "frac_died_after_240_all_healers_dead_normal",
            log->count_died_after_240_all_healers_dead_normal / log->n_normal);
        float all_healers_dead_death_den =
            log->count_died_after_240_all_healers_dead_normal;
        dict_set(out, "frac_died_after_all_healers_dead_with_set_alive_normal",
            log->count_died_after_all_healers_dead_with_set_alive_normal /
                log->n_normal);
        dict_set(out, "frac_died_after_all_healers_dead_killed_by_zuk_normal",
            log->count_died_after_all_healers_dead_killed_by_zuk_normal /
                log->n_normal);
        dict_set(out, "frac_died_after_all_healers_dead_killed_by_ranger_normal",
            log->count_died_after_all_healers_dead_killed_by_ranger_normal /
                log->n_normal);
        dict_set(out, "frac_died_after_all_healers_dead_killed_by_mager_normal",
            log->count_died_after_all_healers_dead_killed_by_mager_normal /
                log->n_normal);
        dict_set(out, "frac_died_after_all_healers_dead_with_shield_active_normal",
            log->count_died_after_all_healers_dead_with_shield_active_normal /
                log->n_normal);
        dict_set(out, "frac_died_after_all_healers_dead_behind_shield_normal",
            log->count_died_after_all_healers_dead_behind_shield_normal /
                log->n_normal);
        if (all_healers_dead_death_den > 0.0f) {
            dict_set(out, "frac_after_all_healers_dead_deaths_with_set_alive_normal",
                log->count_died_after_all_healers_dead_with_set_alive_normal /
                    all_healers_dead_death_den);
            dict_set(out, "frac_after_all_healers_dead_deaths_killed_by_zuk_normal",
                log->count_died_after_all_healers_dead_killed_by_zuk_normal /
                    all_healers_dead_death_den);
            dict_set(out, "frac_after_all_healers_dead_deaths_killed_by_ranger_normal",
                log->count_died_after_all_healers_dead_killed_by_ranger_normal /
                    all_healers_dead_death_den);
            dict_set(out, "frac_after_all_healers_dead_deaths_killed_by_mager_normal",
                log->count_died_after_all_healers_dead_killed_by_mager_normal /
                    all_healers_dead_death_den);
            dict_set(out, "frac_after_all_healers_dead_deaths_with_shield_active_normal",
                log->count_died_after_all_healers_dead_with_shield_active_normal /
                    all_healers_dead_death_den);
            dict_set(out, "frac_after_all_healers_dead_deaths_behind_shield_normal",
                log->count_died_after_all_healers_dead_behind_shield_normal /
                    all_healers_dead_death_den);
            dict_set(out, "brews_remaining_after_all_healers_dead_death_normal",
                log->brews_remaining_after_all_healers_dead_death_normal_sum /
                    all_healers_dead_death_den);
            dict_set(out, "restores_remaining_after_all_healers_dead_death_normal",
                log->restores_remaining_after_all_healers_dead_death_normal_sum /
                    all_healers_dead_death_den);
        } else {
            dict_set(out, "frac_after_all_healers_dead_deaths_with_set_alive_normal", 0.0f);
            dict_set(out, "frac_after_all_healers_dead_deaths_killed_by_zuk_normal", 0.0f);
            dict_set(out, "frac_after_all_healers_dead_deaths_killed_by_ranger_normal", 0.0f);
            dict_set(out, "frac_after_all_healers_dead_deaths_killed_by_mager_normal", 0.0f);
            dict_set(out, "frac_after_all_healers_dead_deaths_with_shield_active_normal", 0.0f);
            dict_set(out, "frac_after_all_healers_dead_deaths_behind_shield_normal", 0.0f);
            dict_set(out, "brews_remaining_after_all_healers_dead_death_normal", 0.0f);
            dict_set(out, "restores_remaining_after_all_healers_dead_death_normal", 0.0f);
        }
        dict_set(out, "frac_died_with_shield_active_normal",
            log->count_died_with_shield_active_normal / log->n_normal);
        dict_set(out, "frac_died_behind_shield_normal",
            log->count_died_behind_shield_normal / log->n_normal);
        dict_set(out, "frac_died_after_240_normal",
            log->count_died_after_240_normal / log->n_normal);
        if (log->count_died_after_240_normal > 0.0f) {
            dict_set(out, "brews_remaining_after_240_death_normal",
                log->brews_remaining_after_240_death_normal_sum /
                    log->count_died_after_240_normal);
            dict_set(out, "restores_remaining_after_240_death_normal",
                log->restores_remaining_after_240_death_normal_sum /
                    log->count_died_after_240_normal);
            dict_set(out, "prayer_at_death_after_240_normal",
                log->prayer_at_death_after_240_normal_sum /
                    log->count_died_after_240_normal);
            dict_set(out, "frac_after_240_deaths_with_shield_active_normal",
                log->count_died_after_240_shield_active_normal /
                    log->count_died_after_240_normal);
            dict_set(out, "frac_after_240_deaths_behind_shield_normal",
                log->count_died_after_240_behind_shield_normal /
                    log->count_died_after_240_normal);
        } else {
            dict_set(out, "brews_remaining_after_240_death_normal", 0.0f);
            dict_set(out, "restores_remaining_after_240_death_normal", 0.0f);
            dict_set(out, "prayer_at_death_after_240_normal", 0.0f);
            dict_set(out, "frac_after_240_deaths_with_shield_active_normal", 0.0f);
            dict_set(out, "frac_after_240_deaths_behind_shield_normal", 0.0f);
        }
    }
    float gear_switch_rate = (log->episode_length > 0.0f)
        ? log->gear_switches / log->episode_length : 0.0f;
    dict_set(out, "gear_switch_rate", gear_switch_rate);

    float wr = log->wins;
    float score;
    int start_wave = (int)(log->start_wave + 0.5f);
    if (start_wave >= 68) {
        /* Zuk-only: score = fraction of lowest Zuk HP reached (0..1), wins = 1.0 */
        score = (1200.0f - log->min_zuk_hp_seen) / 1200.0f;
    } else {
        /* full runs: wave progress (0..0.5) + win bonus (0..1) */
        float wave_frac = log->wave / (float)INF_NUM_WAVES;
        score = wr + (1.0f - wr) * wave_frac * 0.5f;
    }
    dict_set(out, "score", score);

    /* per-NPC-type prayer rates and damage (wandb only).
       keys must be string literals — dict_set stores the pointer, not a copy. */
    /*
    static const char* pray_keys[] = {
        "pray_nibbler","pray_bat","pray_blob","pray_blob_mel","pray_blob_rng","pray_blob_mag",
        "pray_meleer","pray_ranger","pray_mager","pray_jad","pray_zuk","pray_heal_jad","pray_heal_zuk","pray_shield"
    };
    static const char* dmg_keys[] = {
        "dmg_from_nibbler","dmg_from_bat","dmg_from_blob","dmg_from_blob_mel","dmg_from_blob_rng","dmg_from_blob_mag",
        "dmg_from_meleer","dmg_from_ranger","dmg_from_mager","dmg_from_jad","dmg_from_zuk","dmg_from_heal_jad","dmg_from_heal_zuk","dmg_from_shield"
    };
    static const char* kill_keys[] = {
        "killed_by_nibbler","killed_by_bat","killed_by_blob","killed_by_blob_mel","killed_by_blob_rng","killed_by_blob_mag",
        "killed_by_meleer","killed_by_ranger","killed_by_mager","killed_by_jad","killed_by_zuk","killed_by_heal_jad","killed_by_heal_zuk","killed_by_shield"
    };
    for (int t = 0; t < INF_NUM_NPC_TYPES; t++) {
        if (log->attacks_by_type[t] > 0.0f) {
            dict_set(out, pray_keys[t], log->prayer_correct_by_type[t] / log->attacks_by_type[t]);
            dict_set(out, dmg_keys[t], log->dmg_from_type[t]);
        }
        if (log->killed_by_type[t] > 0.0f)
            dict_set(out, kill_keys[t], log->killed_by_type[t]);
    }
    */
}
