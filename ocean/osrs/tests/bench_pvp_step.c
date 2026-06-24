#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static int bench_profile_enabled = 0;
static double bench_profile_ms[64];

static double bench_profile_now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec * 1e-6;
}

static void bench_profile_add(int slot, double ms) {
    if (slot < 0 || slot >= 64) abort();
    bench_profile_ms[slot] += ms;
}

static void bench_profile_mark(int enabled, double* t0, int slot) {
    if (!enabled) return;
    double now_ms = bench_profile_now_ms();
    bench_profile_add(slot, now_ms - *t0);
    *t0 = now_ms;
}

#define OSRS_PVP_PROFILE_ENABLED() bench_profile_enabled
#define OSRS_PVP_PROFILE_NOW_MS() bench_profile_now_ms()
#define OSRS_PVP_PROFILE_ADD(slot, ms) bench_profile_add((slot), (ms))
#define OSRS_PVP_PROFILE_MARK(slot) \
    bench_profile_mark(osrs_pvp_prof_enabled, &osrs_pvp_prof_t0, (slot))

#include "ocean/osrs/osrs_env.h"

typedef struct {
    int steps;
    uint32_t seed;
    OpponentType opponent;
    int action_mode;
    int profile;
    int action_masks_agents;
} BenchConfig;

static uint64_t fnv1a_bytes(uint64_t hash, const void* data, size_t len) {
    const unsigned char* bytes = (const unsigned char*)data;
    for (size_t i = 0; i < len; i++) {
        hash ^= (uint64_t)bytes[i];
        hash *= 1099511628211ULL;
    }
    return hash;
}

static double monotonic_seconds(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

static CollisionMap* benchmark_wilderness_collision_map(void) {
    static CollisionMap* cmap = NULL;
    if (cmap == NULL) {
        osrs_asset_require_group(OSRS_ASSET_GROUP_PVP);
        cmap = collision_map_load(OSRS_ASSET("wilderness.cmap"));
        if (cmap == NULL) {
            fprintf(stderr, "bench_pvp_step: failed to load wilderness.cmap\n");
            abort();
        }
    }
    return cmap;
}

static OpponentType parse_opponent(const char* value) {
    if (strcmp(value, "adaptive") == 0) return OPP_ADAPTIVE_NH;
    if (strcmp(value, "nightmare") == 0) return OPP_NIGHTMARE_NH;
    if (strcmp(value, "savant") == 0) return OPP_SAVANT_NH;
    if (strcmp(value, "master") == 0) return OPP_MASTER_NH;
    if (strcmp(value, "expert") == 0) return OPP_EXPERT_NH;
    fprintf(stderr, "bench_pvp_step: unknown opponent %s\n", value);
    abort();
}

static int parse_action_mode(const char* value) {
    if (strcmp(value, "idle") == 0) return 0;
    if (strcmp(value, "mixed") == 0) return 1;
    fprintf(stderr, "bench_pvp_step: unknown action mode %s\n", value);
    abort();
}

static BenchConfig parse_config(int argc, char** argv) {
    BenchConfig cfg = {
        .steps = 200000,
        .seed = 1337,
        .opponent = OPP_ADAPTIVE_NH,
        .action_mode = 1,
        .profile = 0,
        .action_masks_agents = 0x3,
    };

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--steps") == 0 && i + 1 < argc) {
            cfg.steps = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--seed") == 0 && i + 1 < argc) {
            cfg.seed = (uint32_t)strtoul(argv[++i], NULL, 10);
        } else if (strcmp(argv[i], "--opponent") == 0 && i + 1 < argc) {
            cfg.opponent = parse_opponent(argv[++i]);
        } else if (strcmp(argv[i], "--actions") == 0 && i + 1 < argc) {
            cfg.action_mode = parse_action_mode(argv[++i]);
        } else if (strcmp(argv[i], "--profile") == 0) {
            cfg.profile = 1;
        } else if (strcmp(argv[i], "--mask-agents") == 0 && i + 1 < argc) {
            const char* value = argv[++i];
            if (strcmp(value, "p0") == 0) {
                cfg.action_masks_agents = 0x1;
            } else if (strcmp(value, "both") == 0) {
                cfg.action_masks_agents = 0x3;
            } else {
                fprintf(stderr, "bench_pvp_step: unknown mask agents %s\n", value);
                abort();
            }
        } else {
            fprintf(stderr, "bench_pvp_step: bad args near %s\n", argv[i]);
            abort();
        }
    }

    if (cfg.steps <= 0) {
        fprintf(stderr, "bench_pvp_step: steps must be positive\n");
        abort();
    }
    if (cfg.seed == 0) {
        fprintf(stderr, "bench_pvp_step: seed must be non-zero\n");
        abort();
    }
    return cfg;
}

static void benchmark_fill_agent_action(int* actions, int tick, int mode) {
    memset(actions, 0, NUM_ACTION_HEADS * sizeof(int));
    if (mode == 0) return;

    int phase = tick % 24;
    if (phase == 0) {
        actions[HEAD_EQUIP_SLOT(GEAR_SLOT_WEAPON)] = 1 + (tick % OSRS_INVENTORY_SIZE);
        actions[HEAD_EQUIP_SLOT(GEAR_SLOT_BODY)] = 1 + ((tick + 7) % OSRS_INVENTORY_SIZE);
    }
    if (phase == 3) {
        actions[HEAD_SPECIAL] = SPECIAL_ARM;
    }
    if (phase == 4 || phase == 12 || phase == 20) {
        actions[HEAD_ATTACK] = ATTACK_ATK;
    } else if (phase == 8) {
        actions[HEAD_ATTACK] = ATTACK_ICE;
    } else if (phase == 16) {
        actions[HEAD_ATTACK] = ATTACK_BLOOD;
    }
    if ((tick % 11) == 0) {
        actions[HEAD_OVERHEAD] = ENCOUNTER_OVERHEAD_SET_REFRESH_MAGIC;
    } else if ((tick % 11) == 4) {
        actions[HEAD_OVERHEAD] = ENCOUNTER_OVERHEAD_SET_REFRESH_RANGED;
    } else if ((tick % 11) == 8) {
        actions[HEAD_OVERHEAD] = ENCOUNTER_OVERHEAD_SET_REFRESH_MELEE;
    }
    if ((tick % 37) == 5) {
        actions[HEAD_FOOD] = 1 + ((tick + 3) % OSRS_INVENTORY_SIZE);
    }
    if ((tick % 53) == 9) {
        actions[HEAD_DRINK] = 1 + ((tick + 11) % OSRS_INVENTORY_SIZE);
    }
    if ((tick % 71) == 17) {
        actions[HEAD_KARAMBWAN] = 1 + ((tick + 19) % OSRS_INVENTORY_SIZE);
    }
    if ((tick % 29) == 6) {
        actions[HEAD_MOVE] = 1 + (tick % (MOVE_DIM - 1));
    }
}

static uint64_t benchmark_hash(
    OsrsEnv* env,
    const float* agent_obs,
    const float* agent_rewards,
    const unsigned char* agent_terminals
) {
    uint64_t hash = 1469598103934665603ULL;
    hash = fnv1a_bytes(hash, &env->tick, sizeof(env->tick));
    hash = fnv1a_bytes(hash, &env->episode_over, sizeof(env->episode_over));
    hash = fnv1a_bytes(hash, &env->winner, sizeof(env->winner));
    hash = fnv1a_bytes(hash, &env->pid_holder, sizeof(env->pid_holder));
    hash = fnv1a_bytes(hash, &env->pid_shuffle_countdown, sizeof(env->pid_shuffle_countdown));
    hash = fnv1a_bytes(hash, &env->rng_state, sizeof(env->rng_state));
    hash = fnv1a_bytes(hash, &env->rng_reset_count, sizeof(env->rng_reset_count));
    hash = fnv1a_bytes(hash, env->players, sizeof(env->players));
    hash = fnv1a_bytes(hash, env->pending_actions, sizeof(env->pending_actions));
    hash = fnv1a_bytes(hash, env->last_executed_actions, sizeof(env->last_executed_actions));
    hash = fnv1a_bytes(hash, env->observations, NUM_AGENTS * SLOT_NUM_OBSERVATIONS * sizeof(float));
    hash = fnv1a_bytes(hash, env->action_masks, NUM_AGENTS * ACTION_MASK_SIZE);
    hash = fnv1a_bytes(hash, &env->log, sizeof(env->log));
    hash = fnv1a_bytes(hash, agent_obs, OCEAN_OBS_SIZE * sizeof(float));
    hash = fnv1a_bytes(hash, agent_rewards, sizeof(float));
    hash = fnv1a_bytes(hash, agent_terminals, sizeof(unsigned char));
    return hash;
}

static void benchmark_print_profile(void) {
    static const char* keys[PVP_PROF_COUNT] = {
        [PVP_PROF_C_STEP_TOTAL] = "c_step_total",
        [PVP_PROF_ACTION_DECODE] = "action_decode",
        [PVP_PROF_OPPONENT_ROUTE] = "opponent_route",
        [PVP_PROF_PVP_STEP] = "pvp_step",
        [PVP_PROF_TERMINAL_LOG] = "terminal_log",
        [PVP_PROF_RESET_OBS] = "reset_obs",
        [PVP_PROF_MASK_COPY] = "mask_copy",
        [PVP_PROF_STATE_STORE] = "state_store",
        [PVP_PROF_API_TOTAL] = "api_total",
        [PVP_PROF_API_CLEAR_FLAGS] = "api_clear_flags",
        [PVP_PROF_API_ACTION_COPY] = "api_action_copy",
        [PVP_PROF_API_C_OPPONENT] = "api_c_opponent",
        [PVP_PROF_API_SWITCHES] = "api_switches",
        [PVP_PROF_API_MOVEMENT] = "api_movement",
        [PVP_PROF_API_COMBAT] = "api_combat",
        [PVP_PROF_API_PENDING_HITS] = "api_pending_hits",
        [PVP_PROF_API_REWARD_TERMINAL] = "api_reward_terminal",
        [PVP_PROF_API_OBS_MASK] = "api_obs_mask",
        [PVP_PROF_API_OBS_GENERATE] = "api_obs_generate",
        [PVP_PROF_API_OCEAN_WRITE] = "api_ocean_write",
        [PVP_PROF_API_TERMINAL_SCORING] = "api_terminal_scoring",
        [PVP_PROF_API_AUTO_RESET] = "api_auto_reset",
    };
    for (int i = 0; i < PVP_PROF_COUNT; i++) {
        if (bench_profile_ms[i] <= 0.0) continue;
        printf("profile_%s_ms=%.6f\n", keys[i], bench_profile_ms[i]);
    }
}

int main(int argc, char** argv) {
    BenchConfig cfg = parse_config(argc, argv);
    static OsrsEnv env;
    static float agent_obs[OCEAN_OBS_SIZE];
    static int agent_actions[NUM_ACTION_HEADS];
    static float agent_rewards[1];
    static unsigned char agent_terminals[1];

    memset(&env, 0, sizeof(env));
    pvp_init(&env);
    env.action_masks_agents = cfg.action_masks_agents;
    env.collision_map = benchmark_wilderness_collision_map();
    env.ocean_io.agent_obs = agent_obs;
    env.ocean_io.agent_actions = agent_actions;
    env.ocean_io.agent_rewards = agent_rewards;
    env.ocean_io.agent_terminals = agent_terminals;
    env.pvp_runtime.use_c_opponent = 1;
    env.pvp_runtime.opponent.type = cfg.opponent;
    pvp_seed(&env, cfg.seed);
    pvp_reset(&env);
    bench_profile_enabled = cfg.profile;

    int episodes = 0;
    int wins = 0;
    double started = monotonic_seconds();
    for (int step = 0; step < cfg.steps; step++) {
        benchmark_fill_agent_action(agent_actions, step, cfg.action_mode);
        pvp_step(&env);
        if (agent_terminals[0]) {
            episodes++;
            if (env.log.wins > 0.5f) wins++;
        }
    }
    double elapsed = monotonic_seconds() - started;
    uint64_t hash = benchmark_hash(&env, agent_obs, agent_rewards, agent_terminals);
    printf(
        "steps=%d elapsed_sec=%.6f sps=%.2f episodes=%d wins=%d hash=%016" PRIx64 "\n",
        cfg.steps,
        elapsed,
        (double)cfg.steps / elapsed,
        episodes,
        wins,
        hash
    );
    if (cfg.profile) {
        benchmark_print_profile();
    }
    return 0;
}
