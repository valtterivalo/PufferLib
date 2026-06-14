/**
 * @fileoverview Standalone demo for OSRS PvP C Environment
 *
 * Demonstrates environment initialization, stepping, and basic performance.
 * Compile: make
 * Run: ./osrs_pvp
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <errno.h>
#include <math.h>
#include <stdint.h>
#include "osrs_env.h"
#include "osrs_assets.h"
#include "osrs_encounter.h"
#include "osrs_binary_io.h"
#include "encounters/encounter_nh_pvp.h"
#include "encounters/encounter_zulrah.h"
#if defined(__clang__) || defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#endif
#include "encounters/encounter_inferno.h"
#include "encounters/encounter_colosseum.h"
#if defined(__clang__) || defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

#ifdef OSRS_VISUAL
#include "osrs_render.h"
#include "puffernet.h"
#endif

#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#endif

static int encounter_name_is_pvp(const char* encounter_name) {
    return encounter_name &&
        (strcmp(encounter_name, "pvp") == 0 ||
         strcmp(encounter_name, "nh_pvp") == 0);
}

static void print_player_state(Player* p, int idx) {
    printf("Player %d: HP=%d/%d Prayer=%d Gear=%d Pos=(%d,%d) Frozen=%d\n",
           idx, p->current_hitpoints, p->base_hitpoints,
           p->current_prayer, p->current_gear, p->x, p->y, p->frozen_ticks);
}

static void print_env_state(OsrsEnv* env) {
    printf("\n=== Tick %d ===\n", env->tick);
    print_player_state(&env->players[0], 0);
    print_player_state(&env->players[1], 1);
    printf("PID holder: %d\n", env->pid_holder);
}

static void run_random_episode(OsrsEnv* env, int verbose) {
    pvp_reset(env);

    while (!env->episode_over) {
        for (int agent = 0; agent < NUM_AGENTS; agent++) {
            int* actions = env->actions + agent * NUM_ACTION_HEADS;
            for (int h = 0; h < NUM_ACTION_HEADS; h++) {
                actions[h] = rand() % ACTION_HEAD_DIMS[h];
            }
        }

        pvp_step(env);

        if (verbose && env->tick % 50 == 0) {
            print_env_state(env);
        }
    }

    if (verbose) {
        printf("\n=== Episode End ===\n");
        printf("Winner: Player %d\n", env->winner);
        printf("Length: %d ticks\n", env->tick);
        printf("P0 damage dealt: %.0f\n", env->players[0].total_damage_dealt);
        printf("P1 damage dealt: %.0f\n", env->players[1].total_damage_dealt);
    }
}

static void benchmark(OsrsEnv* env, int num_steps) {
    printf("Benchmarking %d steps...\n", num_steps);

    clock_t start = clock();
    int episodes = 0;
    int total_steps = 0;

    while (total_steps < num_steps) {
        pvp_reset(env);
        episodes++;

        while (!env->episode_over && total_steps < num_steps) {
            for (int agent = 0; agent < NUM_AGENTS; agent++) {
                int* actions = env->actions + agent * NUM_ACTION_HEADS;
                for (int h = 0; h < NUM_ACTION_HEADS; h++) {
                    actions[h] = rand() % ACTION_HEAD_DIMS[h];
                }
            }

            pvp_step(env);
            total_steps++;
        }
    }

    clock_t end = clock();
    double elapsed = (double)(end - start) / CLOCKS_PER_SEC;

    printf("Results:\n");
    printf("  Total steps: %d\n", total_steps);
    printf("  Episodes: %d\n", episodes);
    printf("  Time: %.3f seconds\n", elapsed);
    printf("  Steps/sec: %.0f\n", total_steps / elapsed);
    printf("  Avg episode length: %.1f ticks\n", (float)total_steps / episodes);
}

static EncounterContext* visual_create_encounter_context(const EncounterDef* edef) {
    if (!edef || edef->context_size == 0)
        return NULL;
    EncounterContext* context = (EncounterContext*)calloc(1, edef->context_size);
    if (!context) abort();
    if (edef->init_context)
        edef->init_context(context);
    return context;
}

static void visual_destroy_encounter_context(
    const EncounterDef* edef,
    EncounterContext** context
) {
    if (!context || !*context)
        return;
    if (edef && edef->destroy_context)
        edef->destroy_context(*context);
    free(*context);
    *context = NULL;
}

static double osrs_profile_now_seconds(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1000000000.0;
}

#ifdef COLO_PROFILE_ENABLED
static void osrs_print_colosseum_profile_results(void) {
    int count = colosseum_env_profile_count();
    if (count <= 0) return;
    double values[COLO_PROF_COUNT];
    int order[COLO_PROF_COUNT];
    for (int i = 0; i < count; i++) {
        values[i] = colosseum_env_profile_read_reset_ms(i);
        order[i] = i;
    }
    for (int i = 0; i < count; i++) {
        int best = i;
        for (int j = i + 1; j < count; j++) {
            if (values[order[j]] > values[order[best]]) best = j;
        }
        int tmp = order[i];
        order[i] = order[best];
        order[best] = tmp;
    }
    double total = values[COLO_PROF_C_STEP_TOTAL];
    printf("Colosseum profile buckets:\n");
    for (int r = 0; r < count; r++) {
        int slot = order[r];
        double pct = total > 0.0 ? 100.0 * values[slot] / total : 0.0;
        printf("  %-28s %.3f ms  %.2f%%\n",
            colosseum_env_profile_name(slot), values[slot], pct);
    }
}
#endif

static void run_profile(
    OsrsEnv* env,
    const char* encounter_name,
    int start_wave,
    int profile_steps
) {
    if (profile_steps > 0) {
        printf("Profiling %s for %d steps...\n",
            encounter_name ? encounter_name : "pvp",
            profile_steps);
    } else {
        printf("Profiling %s for 10 seconds...\n", encounter_name ? encounter_name : "pvp");
    }

    if (encounter_name) {
        const EncounterDef* edef = encounter_find(encounter_name);
        if (!edef) {
            fprintf(stderr, "unknown encounter: %s\n", encounter_name);
            return;
        }
        env->encounter_def = (void*)edef;
        env->encounter_state = edef->create();
        env->encounter_context = visual_create_encounter_context(edef);

        if (strcmp(encounter_name, "zulrah") == 0) {
            CollisionMap* cmap = collision_map_load(OSRS_ASSET("zulrah.cmap"));
            if (cmap) {
                edef->put_ptr(
                    env->encounter_state, env->encounter_context, "collision_map", cmap);
                edef->put_int(
                    env->encounter_state, env->encounter_context, "world_offset_x", 2256);
                edef->put_int(
                    env->encounter_state, env->encounter_context, "world_offset_y", 3061);
                env->collision_map = cmap;
            }
        } else if (strcmp(encounter_name, "inferno") == 0) {
            CollisionMap* cmap = collision_map_load(OSRS_ASSET("inferno.cmap"));
            if (cmap) {
                edef->put_ptr(
                    env->encounter_state, env->encounter_context, "collision_map", cmap);
                edef->put_int(
                    env->encounter_state, env->encounter_context, "world_offset_x", 2246);
                edef->put_int(
                    env->encounter_state, env->encounter_context, "world_offset_y", 5315);
                env->collision_map = cmap;
            }
        } else if (strcmp(encounter_name, "colosseum") == 0) {
            CollisionMap* cmap = collision_map_load(OSRS_ASSET("colosseum.cmap"));
            if (cmap) {
                edef->put_ptr(
                    env->encounter_state, env->encounter_context, "collision_map", cmap);
                edef->put_int(
                    env->encounter_state, env->encounter_context, "world_offset_x", 1808);
                edef->put_int(
                    env->encounter_state, env->encounter_context, "world_offset_y", 3090);
                env->collision_map = cmap;
            }
        }
        if (start_wave >= 0 && edef->put_int) {
            edef->put_int(
                env->encounter_state,
                env->encounter_context,
                "start_wave",
                start_wave);
            fprintf(stderr, "start_wave: %d\n", start_wave);
        }
        edef->reset(env->encounter_state, env->encounter_context, 0);
    } else {
        env->pvp_runtime.use_c_opponent = 1;
        env->pvp_runtime.opponent.type = OPP_IMPROVED;
        env->is_lms = 1;
        pvp_reset(env);
    }

    const EncounterDef* profile_edef = (const EncounterDef*)env->encounter_def;
    float* encounter_obs = NULL;
    if (profile_edef) {
        encounter_obs = (float*)calloc(
            (size_t)(profile_edef->obs_size + profile_edef->mask_size),
            sizeof(float));
        if (!encounter_obs) abort();
        profile_edef->write_obs(
            env->encounter_state,
            (EncounterContext*)env->encounter_context,
            encounter_obs);
        profile_edef->write_mask(
            env->encounter_state,
            (EncounterContext*)env->encounter_context,
            encounter_obs + profile_edef->obs_size);
#ifdef COLO_PROFILE_ENABLED
        if (strcmp(profile_edef->name, "colosseum") == 0) {
            int count = colosseum_env_profile_count();
            for (int i = 0; i < count; i++)
                (void)colosseum_env_profile_read_reset_ms(i);
        }
#endif
    }

    double start = osrs_profile_now_seconds();
    double elapsed = 0;
    int total_steps = 0;
    int enc_actions[16] = {0};

    while ((profile_steps > 0 && total_steps < profile_steps) ||
           (profile_steps <= 0 && elapsed < 10.0)) {
        if (env->encounter_def && env->encounter_state) {
            const EncounterDef* edef = (const EncounterDef*)env->encounter_def;
#ifdef COLO_PROFILE_ENABLED
            int col_profile_this_step = strcmp(edef->name, "colosseum") == 0;
            int col_prof_enabled = col_profile_this_step ? COLO_PROFILE_ENABLED() : 0;
            double col_prof_total_t0 = col_prof_enabled ? COLO_PROFILE_NOW_MS() : 0.0;
            double col_prof_t0 = col_prof_total_t0;
#endif
            for (int h = 0; h < edef->num_action_heads; h++) {
                enc_actions[h] = rand() % edef->action_head_dims[h];
            }
#ifdef COLO_PROFILE_ENABLED
            COLO_PROFILE_MARK(COLO_PROF_C_ACTIONS);
#endif
            edef->step(env->encounter_state, env->encounter_context, enc_actions);
#ifdef COLO_PROFILE_ENABLED
            COLO_PROFILE_MARK(COLO_PROF_C_ENCOUNTER_STEP);
#endif
            edef->write_obs(
                env->encounter_state,
                (EncounterContext*)env->encounter_context,
                encounter_obs);
#ifdef COLO_PROFILE_ENABLED
            COLO_PROFILE_MARK(COLO_PROF_C_WRITE_OBS);
#endif
            edef->write_mask(
                env->encounter_state,
                (EncounterContext*)env->encounter_context,
                encounter_obs + edef->obs_size);
#ifdef COLO_PROFILE_ENABLED
            COLO_PROFILE_MARK(COLO_PROF_C_WRITE_MASK);
#endif
            (void)edef->get_reward(
                env->encounter_state,
                (EncounterContext*)env->encounter_context);
            if (edef->is_terminal(env->encounter_state, env->encounter_context)) {
#ifdef COLO_PROFILE_ENABLED
                COLO_PROFILE_MARK(COLO_PROF_C_REWARD_TERMINAL);
                COLO_PROFILE_MARK(COLO_PROF_C_TERMINAL_LOG);
#endif
                edef->reset(
                    env->encounter_state, env->encounter_context, (uint32_t)rand());
                edef->write_obs(
                    env->encounter_state,
                    (EncounterContext*)env->encounter_context,
                    encounter_obs);
                edef->write_mask(
                    env->encounter_state,
                    (EncounterContext*)env->encounter_context,
                    encounter_obs + edef->obs_size);
#ifdef COLO_PROFILE_ENABLED
                COLO_PROFILE_MARK(COLO_PROF_C_RESET);
#endif
            } else {
#ifdef COLO_PROFILE_ENABLED
                COLO_PROFILE_MARK(COLO_PROF_C_REWARD_TERMINAL);
#endif
            }
#ifdef COLO_PROFILE_ENABLED
            if (col_prof_enabled)
                COLO_PROFILE_ADD(
                    COLO_PROF_C_STEP_TOTAL,
                    COLO_PROFILE_NOW_MS() - col_prof_total_t0);
#endif
        } else {
            for (int agent = 0; agent < NUM_AGENTS; agent++) {
                int* actions = env->actions + agent * NUM_ACTION_HEADS;
                for (int h = 0; h < NUM_ACTION_HEADS; h++) {
                    actions[h] = rand() % ACTION_HEAD_DIMS[h];
                }
            }
            pvp_step(env);
            if (env->episode_over) {
                pvp_reset(env);
            }
        }

        total_steps++;
        if (total_steps % 1000 == 0) {
            elapsed = osrs_profile_now_seconds() - start;
        }
    }
    elapsed = osrs_profile_now_seconds() - start;

    printf("Results:\n");
    printf("  Total steps: %d\n", total_steps);
    printf("  Time: %.3f seconds\n", elapsed);
    printf("  Steps/sec: %.0f\n", total_steps / elapsed);
#ifdef COLO_PROFILE_ENABLED
    if (encounter_name && strcmp(encounter_name, "colosseum") == 0)
        osrs_print_colosseum_profile_results();
#endif

    if (env->encounter_def && env->encounter_state) {
        ((const EncounterDef*)env->encounter_def)->destroy(env->encounter_state);
        env->encounter_state = NULL;
        visual_destroy_encounter_context(
            (const EncounterDef*)env->encounter_def,
            (EncounterContext**)&env->encounter_context);
    }
    free(encounter_obs);
}

#ifdef OSRS_VISUAL
/* replay file: binary format for pre-recorded actions.
   header: [int32 num_ticks] [uint32 rng_state], then num_ticks * num_heads int32 values. */
typedef struct {
    int* actions;      /* flat array: actions[tick * num_heads + head] */
    int  num_ticks;
    int  num_heads;
    int  current_tick;
    uint32_t rng_seed; /* RNG state at episode start — needed for deterministic replay */
    void* initial_snapshot;
    size_t initial_snapshot_size;
} ReplayFile;

static ReplayFile* replay_load(const char* path, int num_heads, size_t snapshot_size) {
    FILE* f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "replay: can't open %s\n", path);
        abort();
    }
    int num_ticks = 0;
    uint32_t rng_seed = 12345;
    osrs_read_exact(f, &num_ticks, 4, 1, path, "replay tick count");
    osrs_read_exact(f, &rng_seed, 4, 1, path, "replay rng seed");
    if (num_ticks < 0 || num_heads <= 0) {
        fprintf(stderr, "replay: invalid shape ticks=%d heads=%d\n",
            num_ticks, num_heads);
        abort();
    }
    if ((size_t)num_ticks > SIZE_MAX / (size_t)num_heads) {
        fprintf(stderr, "replay: action count overflow ticks=%d heads=%d\n",
            num_ticks, num_heads);
        abort();
    }
    ReplayFile* rf = (ReplayFile*)osrs_calloc_or_abort(
        1, sizeof(ReplayFile), "replay file");
    rf->num_ticks = num_ticks;
    rf->num_heads = num_heads;
    rf->current_tick = 0;
    rf->rng_seed = rng_seed;
    size_t action_count = (size_t)num_ticks * (size_t)num_heads;
    rf->actions = (int*)osrs_malloc_or_abort(
        action_count * sizeof(int), "replay actions");
    osrs_read_exact(f, rf->actions, sizeof(int), action_count, path, "replay actions");
    long payload_end = ftell(f);
    if (payload_end < 0) {
        fprintf(stderr, "replay: ftell failed for %s\n", path);
        abort();
    }
    if (fseek(f, 0, SEEK_END) != 0) {
        fprintf(stderr, "replay: seek failed for %s\n", path);
        abort();
    }
    long file_end = ftell(f);
    if (file_end < 0 || fseek(f, payload_end, SEEK_SET) != 0) {
        fprintf(stderr, "replay: seek failed for %s\n", path);
        abort();
    }
    long remaining = file_end - payload_end;
    if (remaining > 0) {
        if (snapshot_size == 0 || remaining != (long)snapshot_size) {
            fprintf(stderr,
                "replay: unexpected trailing bytes in %s: got %ld, expected %zu\n",
                path, remaining, snapshot_size);
            abort();
        }
        rf->initial_snapshot = osrs_malloc_or_abort(snapshot_size, "replay snapshot");
        rf->initial_snapshot_size = snapshot_size;
        osrs_read_exact(f, rf->initial_snapshot, 1, snapshot_size, path, "replay snapshot");
    }
    fclose(f);
    fprintf(stderr, "replay loaded: %d ticks, rng=%u from %s\n", num_ticks, rng_seed, path);
    return rf;
}

static int replay_get_actions(ReplayFile* rf, int* out) {
    if (rf->current_tick >= rf->num_ticks) return 0;
    int base = rf->current_tick * rf->num_heads;
    for (int h = 0; h < rf->num_heads; h++) out[h] = rf->actions[base + h];
    rf->current_tick++;
    return 1;
}

static void __attribute__((unused)) replay_free(ReplayFile* rf) {
    if (rf) { free(rf->actions); free(rf->initial_snapshot); free(rf); }
}

#define VISUAL_POLICY_MAX_ACTION_HEADS 16

typedef enum {
    VISUAL_POLICY_NONE = 0,
    VISUAL_POLICY_SAMPLE = 1,
    VISUAL_POLICY_ARGMAX = 2,
} VisualPolicyMode;

typedef struct {
    int input_size;
    int decoder_value_heads;
} VisualPolicyModelShape;

typedef struct {
    int enabled;
    VisualPolicyMode mode;
    uint32_t rng_state;
    Weights* weights;
    PufferNet* net;
    float* obs;
    int obs_size;
    int mask_size;
    int action_dims[VISUAL_POLICY_MAX_ACTION_HEADS];
    int num_action_heads;
} VisualPolicy;

static uint32_t visual_policy_parse_seed(const char* value) {
    errno = 0;
    char* end = NULL;
    unsigned long parsed = strtoul(value, &end, 10);
    if (errno || !end || *end != '\0' || parsed > UINT32_MAX) {
        fprintf(stderr, "policy: invalid policy seed: %s\n", value);
        abort();
    }
    return (uint32_t)parsed;
}

static VisualPolicyMode visual_policy_parse_mode(const char* value) {
    if (!value || strcmp(value, "sample") == 0) return VISUAL_POLICY_SAMPLE;
    if (strcmp(value, "argmax") == 0) return VISUAL_POLICY_ARGMAX;
    fprintf(stderr, "policy: invalid policy mode: %s\n", value);
    abort();
}

static int visual_policy_is_continuous(
    const int* action_dims,
    int num_action_heads
) {
    for (int h = 0; h < num_action_heads; h++) {
        if (action_dims[h] != 1) return 0;
    }
    return 1;
}

static int64_t visual_policy_expected_weight_count(
    int input_size,
    int hidden_size,
    int num_layers,
    const int* action_dims,
    int num_action_heads,
    int decoder_value_heads
) {
    int action_sum = 0;
    for (int h = 0; h < num_action_heads; h++) {
        action_sum += action_dims[h];
    }

    int64_t total = 0;
    total += (int64_t)hidden_size * input_size;
    total += (int64_t)(action_sum + decoder_value_heads) * hidden_size;
    if (visual_policy_is_continuous(action_dims, num_action_heads)) {
        total += num_action_heads;
    }
    total += (int64_t)num_layers * 3 * hidden_size * hidden_size;
    return total;
}

static int64_t visual_policy_file_weight_count(const Weights* weights) {
    return weights->size - 7;
}

static VisualPolicyModelShape visual_policy_select_model_shape(
    const VisualPolicy* policy,
    const EncounterDef* edef,
    int hidden_size,
    int num_layers
) {
    int obs_input_size = policy->obs_size;
    int full_input_size = policy->obs_size + policy->mask_size;
    int64_t obs_value_expected = visual_policy_expected_weight_count(
        obs_input_size, hidden_size, num_layers, policy->action_dims,
        policy->num_action_heads, 1);
    int64_t full_value_expected = visual_policy_expected_weight_count(
        full_input_size, hidden_size, num_layers, policy->action_dims,
        policy->num_action_heads, 1);
    int64_t obs_policy_expected = visual_policy_expected_weight_count(
        obs_input_size, hidden_size, num_layers, policy->action_dims,
        policy->num_action_heads, 0);
    int64_t full_policy_expected = visual_policy_expected_weight_count(
        full_input_size, hidden_size, num_layers, policy->action_dims,
        policy->num_action_heads, 0);
    int64_t file_weights = visual_policy_file_weight_count(policy->weights);

    VisualPolicyModelShape match = {0};
    int matches = 0;
    if (obs_value_expected == file_weights) {
        match = (VisualPolicyModelShape){obs_input_size, 1};
        matches++;
    }
    if (full_value_expected == file_weights) {
        match = (VisualPolicyModelShape){full_input_size, 1};
        matches++;
    }
    if (obs_policy_expected == file_weights) {
        match = (VisualPolicyModelShape){obs_input_size, 0};
        matches++;
    }
    if (full_policy_expected == file_weights) {
        match = (VisualPolicyModelShape){full_input_size, 0};
        matches++;
    }

    if (matches != 1) {
        fprintf(stderr,
            "policy: %s model shape mismatch file=%lld floats obs_value=%lld full_value=%lld obs_policy=%lld full_policy=%lld\n",
            edef->name,
            (long long)file_weights,
            (long long)obs_value_expected,
            (long long)full_value_expected,
            (long long)obs_policy_expected,
            (long long)full_policy_expected);
        abort();
    }
    return match;
}

static PufferNet* visual_policy_make_puffernet(
    Weights* weights,
    int input_dim,
    int hidden_dim,
    int num_layers,
    int action_dims[],
    int num_action_heads,
    int decoder_value_heads
) {
    PufferNet* net = (PufferNet*)calloc(1, sizeof(PufferNet));
    if (!net) {
        fprintf(stderr, "policy: failed to allocate puffer net\n");
        abort();
    }
    net->num_agents = 1;
    net->obs = (float*)calloc((size_t)input_dim, sizeof(float));
    if (!net->obs) {
        fprintf(stderr, "policy: failed to allocate puffer net obs\n");
        abort();
    }

    int action_sum = 0;
    int is_continuous = visual_policy_is_continuous(action_dims, num_action_heads);
    for (int h = 0; h < num_action_heads; h++) {
        action_sum += action_dims[h];
    }
    if (is_continuous && decoder_value_heads == 0) {
        fprintf(stderr, "policy: continuous policy-only decoder is unsupported\n");
        abort();
    }

    net->is_continuous = is_continuous;
    net->num_actions = num_action_heads;
    net->encoder = make_linear(weights, 1, input_dim, hidden_dim);
    net->decoder = make_linear(weights, 1, hidden_dim, action_sum + decoder_value_heads);
    if (net->is_continuous) {
        net->log_std = get_weights(weights, num_action_heads);
    }
    net->mingru = make_mingru(weights, 1, hidden_dim, num_layers);
    if (!net->is_continuous) {
        net->multidiscrete = make_multidiscrete(1, action_dims, num_action_heads);
    }
    return net;
}

static uint32_t visual_policy_next_u32(VisualPolicy* policy) {
    policy->rng_state = policy->rng_state * 1664525u + 1013904223u;
    return policy->rng_state;
}

static float visual_policy_next_uniform(VisualPolicy* policy) {
    return (float)((visual_policy_next_u32(policy) >> 8) * (1.0 / 16777216.0));
}

static void visual_policy_init(
    VisualPolicy* policy,
    const EncounterDef* edef,
    const char* model_path,
    VisualPolicyMode mode,
    uint32_t seed
) {
    memset(policy, 0, sizeof(*policy));
    if (!model_path || !model_path[0]) return;
    if (!edef) {
        fprintf(stderr, "policy: missing encounter definition\n");
        abort();
    }
    if (edef->num_action_heads > VISUAL_POLICY_MAX_ACTION_HEADS) {
        fprintf(stderr, "policy: too many action heads: %d\n", edef->num_action_heads);
        abort();
    }
    int action_mask_size = 0;
    for (int h = 0; h < edef->num_action_heads; h++) {
        action_mask_size += edef->action_head_dims[h];
    }
    if (action_mask_size != edef->mask_size) {
        fprintf(stderr, "policy: %s mask mismatch heads=%d mask=%d\n",
            edef->name, action_mask_size, edef->mask_size);
        abort();
    }
    policy->obs_size = edef->obs_size;
    policy->mask_size = edef->mask_size;
    policy->num_action_heads = edef->num_action_heads;
    for (int h = 0; h < edef->num_action_heads; h++) {
        policy->action_dims[h] = edef->action_head_dims[h];
    }
    policy->weights = load_weights(model_path);
    if (!policy->weights) {
        fprintf(stderr, "policy: failed to load model: %s\n", model_path);
        abort();
    }
    int hidden_size = (strcmp(edef->name, "inferno") == 0 ||
                       strcmp(edef->name, "colosseum") == 0) ? 512 : 128;
    int num_layers = 2;
    VisualPolicyModelShape model_shape = visual_policy_select_model_shape(
        policy, edef, hidden_size, num_layers);
    policy->net = visual_policy_make_puffernet(
        policy->weights,
        model_shape.input_size,
        hidden_size,
        num_layers,
        policy->action_dims,
        policy->num_action_heads,
        model_shape.decoder_value_heads);
    int64_t file_weights = visual_policy_file_weight_count(policy->weights);
    if (policy->weights->idx != file_weights) {
        fprintf(stderr,
            "policy: model shape mismatch consumed=%d floats file=%lld floats\n",
            policy->weights->idx, (long long)file_weights);
        abort();
    }
    policy->obs = (float*)osrs_calloc_or_abort(
        (size_t)(policy->obs_size + policy->mask_size),
        sizeof(float),
        "visual policy obs");
    policy->mode = mode;
    policy->rng_state = seed;
    policy->enabled = 1;
    fprintf(stderr, "policy: loaded %s mode=%s seed=%u\n",
        model_path, mode == VISUAL_POLICY_ARGMAX ? "argmax" : "sample", seed);
}

static void __attribute__((unused)) visual_policy_destroy(VisualPolicy* policy) {
    if (!policy) return;
    if (policy->net) free_puffernet(policy->net);
    free(policy->weights);
    free(policy->obs);
    memset(policy, 0, sizeof(*policy));
}

static void visual_policy_reset_recurrent(VisualPolicy* policy) {
    if (!policy || !policy->net || !policy->net->mingru) return;
    memset(policy->net->mingru->state, 0,
        (size_t)policy->net->mingru->num_layers *
        (size_t)policy->net->mingru->batch_size *
        (size_t)policy->net->mingru->hidden_size *
        sizeof(float));
}

static int visual_policy_argmax_masked(const float* logits, const float* mask, int dim) {
    int best_action = -1;
    float best_logit = -INFINITY;
    for (int a = 0; a < dim; a++) {
        if (mask[a] <= 0.5f) continue;
        if (best_action < 0 || logits[a] > best_logit) {
            best_action = a;
            best_logit = logits[a];
        }
    }
    return best_action;
}

static int visual_policy_sample_masked(
    VisualPolicy* policy,
    const float* logits,
    const float* mask,
    int dim
) {
    int best_action = visual_policy_argmax_masked(logits, mask, dim);
    if (best_action < 0) return -1;
    float max_logit = logits[best_action];
    float sum = 0.0f;
    for (int a = 0; a < dim; a++) {
        if (mask[a] <= 0.5f) continue;
        sum += expf(logits[a] - max_logit);
    }
    if (!(sum > 0.0f) || !isfinite(sum)) {
        fprintf(stderr, "policy: invalid masked softmax sum %f\n", sum);
        abort();
    }
    float threshold = visual_policy_next_uniform(policy) * sum;
    float acc = 0.0f;
    for (int a = 0; a < dim; a++) {
        if (mask[a] <= 0.5f) continue;
        acc += expf(logits[a] - max_logit);
        if (threshold <= acc) return a;
    }
    return best_action;
}

static void visual_policy_actions(
    VisualPolicy* policy,
    const EncounterDef* edef,
    EncounterState* state,
    EncounterContext* context,
    int* actions
) {
    if (!policy || !policy->enabled) return;
    edef->write_obs(state, context, policy->obs);
    edef->write_mask(state, context, policy->obs + policy->obs_size);
    linear(policy->net->encoder, policy->obs);
    mingru(policy->net->mingru, policy->net->encoder->output);
    linear(policy->net->decoder, policy->net->mingru->output);

    const float* logits = policy->net->decoder->output;
    const float* mask = policy->obs + policy->obs_size;
    int logit_offset = 0;
    int mask_offset = 0;
    for (int h = 0; h < policy->num_action_heads; h++) {
        int dim = policy->action_dims[h];
        int action = policy->mode == VISUAL_POLICY_ARGMAX
            ? visual_policy_argmax_masked(logits + logit_offset, mask + mask_offset, dim)
            : visual_policy_sample_masked(
                policy, logits + logit_offset, mask + mask_offset, dim);
        if (action < 0) {
            fprintf(stderr, "policy: action head %d has no valid mask entry\n", h);
            abort();
        }
        actions[h] = action;
        logit_offset += dim;
        mask_offset += dim;
    }
}

typedef struct {
    OsrsEnv* env;
    const char* encounter_name;
    ReplayFile* replay;
    VisualPolicy policy;
    int start_wave;
    /* per-frame state */
    double episode_end_time;  /* >0 when holding final frame */
    int episode_ended;
    int seen_lab_restore_generation;
} VisualState;

static void visual_frame(void* arg) {
    VisualState* vs = (VisualState*)arg;
    OsrsEnv* env = vs->env;
    RenderClient* rc = (RenderClient*)env->client;
    if (rc->lab_restore_generation != vs->seen_lab_restore_generation) {
        vs->seen_lab_restore_generation = rc->lab_restore_generation;
        vs->episode_ended = 0;
        visual_policy_reset_recurrent(&vs->policy);
    }

    /* rewind: restore historical state and re-render */
    if (rc->step_back) {
        rc->step_back = 0;
        render_restore_snapshot(rc, env);
        /* if we restored the latest snapshot, exit rewind mode */
        if (rc->history_cursor >= rc->history_count - 1) {
            rc->history_cursor = -1;
        }
        pvp_render(env);
        return;
    }

    /* in rewind mode viewing history: just render, don't step */
    if (rc->history_cursor >= 0) {
        pvp_render(env);
        return;
    }

    /* episode ended: hold final frame for 2 seconds then reset */
    if (vs->episode_ended) {
        pvp_render(env);
        if (GetTime() - vs->episode_end_time >= 2.0) {
            vs->episode_ended = 0;
            if (env->encounter_def) {
                ((const EncounterDef*)env->encounter_def)->reset(
                    env->encounter_state,
                    (EncounterContext*)env->encounter_context,
                    (uint32_t)rand());
            } else {
                pvp_reset(env);
            }
            render_reset_episode_visual_state(rc, env);
            visual_policy_reset_recurrent(&vs->policy);
            render_save_snapshot(rc, env);
        }
        return;
    }

    /* paused: render but don't step */
    if (rc->is_paused && !rc->step_once) {
        pvp_render(env);
        return;
    }
    rc->step_once = 0;

    /* tick pacing: keep rendering while waiting */
    if (rc->ticks_per_second > 0.0f) {
        double interval = 1.0 / rc->ticks_per_second;
        if (GetTime() - rc->last_tick_time < interval) {
            pvp_render(env);
            return;
        }
    }
    rc->last_tick_time = GetTime();

    /* step the simulation */
    render_pre_tick(rc, env);

    if (env->encounter_def && env->encounter_state) {
        /* encounter mode */
        const EncounterDef* edef = (const EncounterDef*)env->encounter_def;
        int enc_actions[16] = {0};
        int used_human_step = 0;

        if (rc->human_input.enabled && edef->step_human_commands) {
            edef->step_human_commands(
                env->encounter_state,
                (EncounterContext*)env->encounter_context,
                &rc->human_input);
            used_human_step = 1;
        } else if (rc->human_input.enabled) {
            /* human control: per-encounter translator */
            if (edef->translate_human_input)
                edef->translate_human_input(&rc->human_input, enc_actions,
                                            env->encounter_state,
                                            (EncounterContext*)env->encounter_context);
            /* set encounter destination from human click for proper pathfinding.
               attacking an NPC cancels movement (OSRS: server stops walking
               to old dest and auto-walks toward target instead). */
            if (rc->human_input.pending_move_x >= 0 && edef->put_int) {
                edef->put_int(env->encounter_state,
                              (EncounterContext*)env->encounter_context,
                              "player_dest_x",
                              rc->human_input.pending_move_x);
                edef->put_int(env->encounter_state,
                              (EncounterContext*)env->encounter_context,
                              "player_dest_y",
                              rc->human_input.pending_move_y);
            } else if (rc->human_input.pending_attack && edef->put_int) {
                edef->put_int(
                    env->encounter_state,
                    (EncounterContext*)env->encounter_context,
                    "player_dest_x",
                    -1);
                edef->put_int(
                    env->encounter_state,
                    (EncounterContext*)env->encounter_context,
                    "player_dest_y",
                    -1);
            }
            human_input_clear_pending(&rc->human_input);
        } else if (vs->replay && replay_get_actions(vs->replay, enc_actions)) {
            /* replay mode: actions come from pre-recorded file */
        } else if (vs->policy.enabled) {
            visual_policy_actions(
                &vs->policy,
                edef,
                env->encounter_state,
                (EncounterContext*)env->encounter_context,
                enc_actions);
        } else if (strcmp(edef->name, "zulrah") == 0) {
            zul_heuristic_actions((ZulrahState*)env->encounter_state, enc_actions);
        } else {
            for (int h = 0; h < edef->num_action_heads; h++) {
                enc_actions[h] = rand() % edef->action_head_dims[h];
            }
        }
        if (!used_human_step) {
            edef->step(
                env->encounter_state,
                (EncounterContext*)env->encounter_context,
                enc_actions);
        }
        /* sync env->tick so renderer HP bars/splats use correct tick */
        env->tick = edef->get_tick(
            env->encounter_state, (EncounterContext*)env->encounter_context);

        /* clear human move when player arrived at clicked destination */
        if (rc->human_input.enabled && rc->human_input.pending_move_x >= 0) {
            Player* ply = edef->get_entity(
                env->encounter_state, (EncounterContext*)env->encounter_context, 0);
            if (ply && ply->x == rc->human_input.pending_move_x &&
                ply->y == rc->human_input.pending_move_y) {
                human_input_clear_move(&rc->human_input);
            }
        }

    } else {
        /* PvP mode */
        if (rc->human_input.enabled) {
            /* human control: translate staged clicks to PvP actions for agent 0 */
            human_to_pvp_actions(&rc->human_input,
                                  env->actions, &env->players[0], &env->players[1]);
            /* opponent still gets random actions */
            int* opp = env->actions + NUM_ACTION_HEADS;
            for (int h = 0; h < NUM_ACTION_HEADS; h++) {
                opp[h] = rand() % ACTION_HEAD_DIMS[h];
            }
            human_input_clear_pending(&rc->human_input);
        } else {
            for (int agent = 0; agent < NUM_AGENTS; agent++) {
                int* actions = env->actions + agent * NUM_ACTION_HEADS;
                for (int h = 0; h < NUM_ACTION_HEADS; h++) {
                    actions[h] = rand() % ACTION_HEAD_DIMS[h];
                }
            }
        }
        pvp_step(env);

        /* clear human move when player arrived at clicked destination */
        if (rc->human_input.enabled && rc->human_input.pending_move_x >= 0) {
            Player* p0 = &env->players[0];
            if (p0->x == rc->human_input.pending_move_x &&
                p0->y == rc->human_input.pending_move_y) {
                human_input_clear_move(&rc->human_input);
            }
        }
    }

    render_post_tick(rc, env);
    render_save_snapshot(rc, env);
    pvp_render(env);

    /* auto-reset on episode end */
    int is_over = env->encounter_def
        ? ((const EncounterDef*)env->encounter_def)->is_terminal(
            env->encounter_state, (EncounterContext*)env->encounter_context)
        : env->episode_over;
    if (is_over) {
        vs->episode_ended = 1;
        vs->episode_end_time = GetTime();
    }
}

static void run_visual(
    OsrsEnv* env,
    const char* encounter_name,
    const char* replay_path,
    int start_wave,
    int gear_tier,
    const char* model_path,
    VisualPolicyMode policy_mode,
    uint32_t policy_seed
) {
    env->client = NULL;

    /* set up encounter if specified, otherwise default to PvP */
    if (encounter_name) {
        const EncounterDef* edef = encounter_find(encounter_name);
        if (!edef) {
            fprintf(stderr, "unknown encounter: %s\n", encounter_name);
            return;
        }
        env->encounter_def = (void*)edef;
        env->encounter_state = edef->create();
        env->encounter_context = visual_create_encounter_context(edef);
        if (encounter_name_is_pvp(encounter_name) && edef->put_int) {
            edef->put_int(env->encounter_state, env->encounter_context, "use_c_opponent", 1);
            edef->put_int(env->encounter_state, env->encounter_context, "opponent_type", OPP_IMPROVED);
#ifdef __EMSCRIPTEN__
            edef->put_int(env->encounter_state, env->encounter_context, "use_c_opponent_p0", 0);
#else
            edef->put_int(env->encounter_state, env->encounter_context, "use_c_opponent_p0", 1);
            edef->put_int(env->encounter_state, env->encounter_context, "opponent_p0_type", OPP_IMPROVED);
#endif
            edef->put_int(env->encounter_state, env->encounter_context, "is_lms", 1);
            edef->put_int(env->encounter_state, env->encounter_context, "gear_tier", gear_tier);
        }
        /* seed=0 matches training binding (uses default RNG, not explicit seed) */

        /* load encounter-specific collision map.
           world offset translates encounter-local (0,0) → world coords for cmap lookup.
           the Zulrah island collision data has ~69 walkable tiles forming the
           irregular island shape (narrow south, wide north, pillar alcoves). */
        if (strcmp(encounter_name, "zulrah") == 0) {
            CollisionMap* cmap = collision_map_load(OSRS_ASSET("zulrah.cmap"));
            if (cmap) {
                edef->put_ptr(
                    env->encounter_state, env->encounter_context, "collision_map", cmap);
                edef->put_int(
                    env->encounter_state, env->encounter_context, "world_offset_x", 2256);
                edef->put_int(
                    env->encounter_state, env->encounter_context, "world_offset_y", 3061);
                env->collision_map = cmap;
                fprintf(stderr, "zulrah collision map: %d regions, offset (2256, 3061)\n",
                        cmap->count);
            }
        } else if (strcmp(encounter_name, "inferno") == 0) {
            CollisionMap* cmap = collision_map_load(OSRS_ASSET("inferno.cmap"));
            if (cmap) {
                edef->put_ptr(
                    env->encounter_state, env->encounter_context, "collision_map", cmap);
                edef->put_int(
                    env->encounter_state, env->encounter_context, "world_offset_x", 2246);
                edef->put_int(
                    env->encounter_state, env->encounter_context, "world_offset_y", 5315);
                env->collision_map = cmap;
                fprintf(stderr, "inferno collision map: %d regions, offset (2246, 5315)\n",
                        cmap->count);
            }
        } else if (strcmp(encounter_name, "colosseum") == 0) {
            CollisionMap* cmap = collision_map_load(OSRS_ASSET("colosseum.cmap"));
            if (cmap) {
                edef->put_ptr(
                    env->encounter_state, env->encounter_context, "collision_map", cmap);
                edef->put_int(
                    env->encounter_state, env->encounter_context, "world_offset_x", 1808);
                edef->put_int(
                    env->encounter_state, env->encounter_context, "world_offset_y", 3090);
                env->collision_map = cmap;
                fprintf(stderr, "colosseum collision map: %d regions, offset (1808, 3090)\n",
                        cmap->count);
            }
        }

        if (start_wave >= 0 && edef->put_int) {
            edef->put_int(
                env->encounter_state,
                env->encounter_context,
                "start_wave",
                start_wave);
        }
        edef->reset(env->encounter_state, env->encounter_context, 0);
        fprintf(stderr, "encounter: %s (obs=%d, heads=%d)%s\n",
                edef->name, edef->obs_size, edef->num_action_heads,
                start_wave >= 0 ? "" : "");
        if (start_wave >= 0)
            fprintf(stderr, "start_wave: %d\n", start_wave);
    } else {
        env->pvp_runtime.use_c_opponent = 1;
        env->pvp_runtime.opponent.type = OPP_IMPROVED;
        env->is_lms = 1;
        pvp_reset(env);
    }

    /* load collision map from env var if set */
    const char* cmap_path = getenv("OSRS_COLLISION_MAP");
    if (cmap_path && cmap_path[0]) {
        env->collision_map = collision_map_load(cmap_path);
        if (env->collision_map) {
            fprintf(stderr, "collision map loaded: %d regions\n",
                    ((CollisionMap*)env->collision_map)->count);
        }
    }

    /* init window before main loop (WindowShouldClose needs a window) */
    pvp_render(env);
    RenderClient* rc = (RenderClient*)env->client;
#ifdef __EMSCRIPTEN__
    if (!encounter_name || encounter_name_is_pvp(encounter_name)) {
        rc->ticks_per_second = 15.0f;
    }
#endif

    if (!encounter_name || encounter_name_is_pvp(encounter_name)) {
        osrs_asset_require_group(OSRS_ASSET_GROUP_PVP);
    } else if (strcmp(encounter_name, "zulrah") == 0) {
        osrs_asset_require_group(OSRS_ASSET_GROUP_ZULRAH);
        osrs_asset_require_group(OSRS_ASSET_GROUP_COMBAT_VISUALS);
    } else if (strcmp(encounter_name, "inferno") == 0) {
        osrs_asset_require_group(OSRS_ASSET_GROUP_INFERNO);
        osrs_asset_require_group(OSRS_ASSET_GROUP_COMBAT_VISUALS);
    } else if (strcmp(encounter_name, "colosseum") == 0) {
        osrs_asset_require_group(OSRS_ASSET_GROUP_COLOSSEUM);
        osrs_asset_require_group(OSRS_ASSET_GROUP_COMBAT_VISUALS);
    }

    /* share collision map pointer with renderer for overlays */
    if (env->collision_map) {
        rc->collision_map = (const CollisionMap*)env->collision_map;
    }

    /* load 3D assets if available */
    rc->model_cache = model_cache_load(OSRS_ASSET("equipment.models"));
    if (rc->model_cache) {
        rc->show_models = 1;
    }
    rc->anim_cache = anim_cache_load(OSRS_ASSET("equipment.anims"));
    render_load_projectile_assets(rc);
    render_init_overlay_models(rc);
    /* load terrain/objects per encounter */
    if (!encounter_name || encounter_name_is_pvp(encounter_name)) {
        rc->terrain = terrain_load(OSRS_ASSET("wilderness.terrain"));
        rc->objects = NULL;
        rc->npcs = NULL;
    } else if (strcmp(encounter_name, "zulrah") == 0) {
        rc->terrain = terrain_load(OSRS_ASSET("zulrah.terrain"));
        rc->objects = objects_load(OSRS_ASSET("zulrah.objects"));

        /* Zulrah coordinate alignment: three coordinate spaces are in play:

           1. OSRS world coords: absolute tile positions (e.g. 2256, 3061).
              terrain, objects, and collision maps are all authored in this space.

           2. encounter-local coords: the encounter arena uses (0,0) as origin.
              the encounter state, entity positions, and arena bounds all use this.

           3. raylib world coords: X = east, Y = up, Z = -north (right-handed).
              terrain_offset/objects_offset subtract the world origin so that
              encounter-local (0,0) maps to raylib (0,0).

           terrain/objects offset: subtract (2256, 3061) from world coords.
             regions (35,47)+(35,48) start at world (2240, 3008).
             the island platform is at world ~(2256, 3061), so offset = 2240+16, 3008+53.

           collision map offset: ADD (2254, 3060) to encounter-local coords.
             collision_get_flags expects world coords, so when the renderer or
             encounter queries tile (x, y) in local space, it looks up
             (x + 2254, y + 3060) in the collision map. */
        int zul_off_x = 2240 + 16;
        int zul_off_y = 3008 + 53;
        if (rc->terrain)
            terrain_offset(rc->terrain, zul_off_x, zul_off_y);
        if (rc->objects)
            objects_offset(rc->objects, zul_off_x, zul_off_y);

        rc->collision_map = (const CollisionMap*)env->collision_map;
        rc->collision_world_offset_x = 2256;
        rc->collision_world_offset_y = 3061;

        rc->npc_model_cache = model_cache_load(OSRS_ASSET("zulrah.models"));
        rc->npc_anim_cache = anim_cache_load(OSRS_ASSET("zulrah.anims"));
        fprintf(stderr, "zulrah: npc_models=%d, npc_anims=%d seqs\n",
                rc->npc_model_cache ? rc->npc_model_cache->count : 0,
                rc->npc_anim_cache ? rc->npc_anim_cache->seq_count : 0);
    } else if (encounter_name && strcmp(encounter_name, "inferno") == 0) {
        rc->terrain = terrain_load(OSRS_ASSET("inferno.terrain"));
        rc->objects = objects_load(OSRS_ASSET("inferno.objects"));
        rc->objects_zuk = objects_load(OSRS_ASSET("inferno_zuk.objects"));
        /* inferno region (35,83) starts at world (2240, 5312).
           encounter uses region-local coords (10-40, 13-44).
           offset terrain/objects so local coord 0 maps to world 2240. */
        if (rc->terrain)
            terrain_offset(rc->terrain, 2246, 5315);
        if (rc->objects)
            objects_offset(rc->objects, 2246, 5315);
        if (rc->objects_zuk)
            objects_offset(rc->objects_zuk, 2246, 5315);

        rc->npc_model_cache = model_cache_load(OSRS_ASSET("inferno.models"));
        rc->npc_anim_cache = anim_cache_load(OSRS_ASSET("inferno.anims"));

        /* collision map for debug overlay (C key) */
        if (env->collision_map) {
            rc->collision_map = (const CollisionMap*)env->collision_map;
            rc->collision_world_offset_x = 2246;
            rc->collision_world_offset_y = 5315;
        }

        fprintf(stderr, "inferno: terrain=%s, cmap=%s, npc_models=%d, npc_anims=%d seqs\n",
                rc->terrain ? "loaded" : "MISSING",
                rc->collision_map ? "loaded" : "MISSING",
                rc->npc_model_cache ? rc->npc_model_cache->count : 0,
                rc->npc_anim_cache ? rc->npc_anim_cache->seq_count : 0);
    } else if (encounter_name && strcmp(encounter_name, "colosseum") == 0) {
        /* Fortis Colosseum overworld stadium: map region (28, 48) starts at world
           (1792, 3072). The encounter uses arena-local coords (0..33); the 34x34
           playable square (= the los grid) sits at world SW corner (1808, 3090),
           so offset terrain/objects/collision so local 0 maps to world 1808/3090.
           The old (1807,3089) anchor was the deadzone-outline bbox corner, one
           tile SW of the playable square. */
        rc->terrain = terrain_load(OSRS_ASSET("colosseum.terrain"));
        rc->objects = objects_load(OSRS_ASSET("colosseum.objects"));
        if (rc->terrain)
            terrain_offset(rc->terrain, 1808, 3090);
        if (rc->objects)
            objects_offset(rc->objects, 1808, 3090);
        rc->npc_model_cache = model_cache_load(OSRS_ASSET("colosseum_npcs.models"));
        rc->npc_anim_cache = anim_cache_load(OSRS_ASSET("colosseum_npcs.anims"));
        if (env->collision_map) {
            rc->collision_map = (const CollisionMap*)env->collision_map;
            rc->collision_world_offset_x = 1808;
            rc->collision_world_offset_y = 3090;
        }
        fprintf(stderr, "colosseum: terrain=%s, cmap=%s, npc_models=%d, npc_anims=%d seqs\n",
                rc->terrain ? "loaded" : "MISSING",
                rc->collision_map ? "loaded" : "MISSING",
                rc->npc_model_cache ? rc->npc_model_cache->count : 0,
                rc->npc_anim_cache ? rc->npc_anim_cache->seq_count : 0);
    }

    /* populate entity pointers (also sets arena bounds from encounter) */
    render_populate_entities(rc, env);

    /* update camera target to center on the (possibly new) arena */
    rc->cam_target_x = (float)rc->arena_base_x + (float)rc->arena_width / 2.0f;
    rc->cam_target_z = -((float)rc->arena_base_y + (float)rc->arena_height / 2.0f);

    for (int i = 0; i < rc->entity_count; i++) {
        int size = rc->entities[i].npc_size > 1 ? rc->entities[i].npc_size : 1;
        rc->sub_x[i] = rc->entities[i].x * 128 + size * 64;
        rc->sub_y[i] = rc->entities[i].y * 128 + size * 64;
        rc->dest_x[i] = rc->sub_x[i];
        rc->dest_y[i] = rc->sub_y[i];
    }

    /* load replay file if specified */
    ReplayFile* replay = NULL;
    if (replay_path && env->encounter_def) {
        const EncounterDef* edef = (const EncounterDef*)env->encounter_def;
        size_t snapshot_size = 0;
        if (edef->snapshot_size)
            snapshot_size = edef->snapshot_size(
                env->encounter_state,
                env->encounter_context);
        replay = replay_load(replay_path, edef->num_action_heads, snapshot_size);
        if (replay && replay->initial_snapshot) {
            if (!edef->restore) {
                fprintf(stderr, "replay: encounter has snapshot data but no restore hook\n");
                abort();
            }
            edef->restore(
                env->encounter_state,
                env->encounter_context,
                replay->initial_snapshot,
                replay->initial_snapshot_size);
        } else if (replay && edef->put_int) {
            edef->reset(env->encounter_state, env->encounter_context, 0);
            edef->put_int(
                env->encounter_state,
                env->encounter_context,
                "seed",
                (int)replay->rng_seed);
        }
        if (replay) {
            render_populate_entities(rc, env);
            for (int i = 0; i < rc->entity_count; i++) {
                int size = rc->entities[i].npc_size > 1 ? rc->entities[i].npc_size : 1;
                rc->sub_x[i] = rc->entities[i].x * 128 + size * 64;
                rc->sub_y[i] = rc->entities[i].y * 128 + size * 64;
                rc->dest_x[i] = rc->sub_x[i];
                rc->dest_y[i] = rc->sub_y[i];
            }
        }
    }

    VisualPolicy policy;
    visual_policy_init(
        &policy,
        (const EncounterDef*)env->encounter_def,
        model_path,
        policy_mode,
        policy_seed);

    /* save initial state as first snapshot */
    render_save_snapshot(rc, env);

#ifdef __EMSCRIPTEN__
    static VisualState web_visual_state;
    web_visual_state = (VisualState){
        .env = env,
        .encounter_name = encounter_name,
        .replay = replay,
        .policy = policy,
        .start_wave = start_wave,
        .episode_end_time = 0,
        .episode_ended = 0,
        .seen_lab_restore_generation = rc->lab_restore_generation,
    };
    emscripten_set_main_loop_arg(visual_frame, &web_visual_state, 0, 1);
#else
    VisualState vs = {
        .env = env,
        .encounter_name = encounter_name,
        .replay = replay,
        .policy = policy,
        .start_wave = start_wave,
        .episode_end_time = 0,
        .episode_ended = 0,
        .seen_lab_restore_generation = rc->lab_restore_generation,
    };

    while (!WindowShouldClose()) {
        visual_frame(&vs);
    }

    replay_free(replay);
    visual_policy_destroy(&vs.policy);

    if (env->client) {
        render_destroy_client((RenderClient*)env->client);
        env->client = NULL;
    }
    if (env->encounter_def && env->encounter_state) {
        ((const EncounterDef*)env->encounter_def)->destroy(env->encounter_state);
        env->encounter_state = NULL;
        visual_destroy_encounter_context(
            (const EncounterDef*)env->encounter_def,
            (EncounterContext**)&env->encounter_context);
    }
#endif
}
#endif

int main(int argc, char** argv) {
    int use_visual = 1;  /* default to visual mode */
    int use_profile = 0;
    int gear_tier = -1;  /* -1 = random (default LMS distribution) */
    int start_wave = -1; /* -1 = default (wave 0) */
    int profile_steps = 0;
    const char* encounter_name __attribute__((unused)) = NULL;
    const char* replay_path __attribute__((unused)) = NULL;
    const char* model_path __attribute__((unused)) = NULL;
    const char* policy_mode_name __attribute__((unused)) = "sample";
    uint32_t policy_seed __attribute__((unused)) = 1;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--visual") == 0) use_visual = 1;
        else if (strcmp(argv[i], "--profile") == 0) { use_profile = 1; use_visual = 0; }
        else if (strcmp(argv[i], "--encounter") == 0 && i + 1 < argc)
            encounter_name = argv[++i];
        else if (strcmp(argv[i], "--replay") == 0 && i + 1 < argc)
            replay_path = argv[++i];
        else if (strcmp(argv[i], "--model") == 0 && i + 1 < argc)
            model_path = argv[++i];
        else if (strcmp(argv[i], "--policy-mode") == 0 && i + 1 < argc)
            policy_mode_name = argv[++i];
        else if (strcmp(argv[i], "--policy-seed") == 0 && i + 1 < argc)
            policy_seed = visual_policy_parse_seed(argv[++i]);
        else if (strcmp(argv[i], "--tier") == 0 && i + 1 < argc)
            gear_tier = atoi(argv[++i]);
        else if (strcmp(argv[i], "--wave") == 0 && i + 1 < argc)
            start_wave = atoi(argv[++i]);
        else if ((strcmp(argv[i], "--start-wave") == 0 ||
                  strcmp(argv[i], "--start_wave") == 0) && i + 1 < argc)
            start_wave = atoi(argv[++i]);
        else if (strcmp(argv[i], "--profile-steps") == 0 && i + 1 < argc)
            profile_steps = atoi(argv[++i]);
    }

#ifdef __EMSCRIPTEN__
    if (!encounter_name) encounter_name = "inferno";
    if (encounter_name && strcmp(encounter_name, "pvp") == 0) encounter_name = "nh_pvp";
#else
    if (encounter_name && strcmp(encounter_name, "pvp") == 0) encounter_name = "nh_pvp";
#endif
    VisualPolicyMode policy_mode __attribute__((unused)) =
        visual_policy_parse_mode(policy_mode_name);

    srand((unsigned int)time(NULL));

#ifdef __EMSCRIPTEN__
    static OsrsEnv env;
#else
    OsrsEnv env;
#endif
    memset(&env, 0, sizeof(OsrsEnv));

    if (use_profile) {
        env.observations = (float*)calloc(NUM_AGENTS * SLOT_NUM_OBSERVATIONS, sizeof(float));
        env.actions = (int*)calloc(NUM_AGENTS * NUM_ACTION_HEADS, sizeof(int));
        env.rewards = (float*)calloc(NUM_AGENTS, sizeof(float));
        env.terminals = (unsigned char*)calloc(NUM_AGENTS, sizeof(unsigned char));
        env.action_masks = (unsigned char*)calloc(NUM_AGENTS * ACTION_MASK_SIZE, sizeof(unsigned char));
        env.action_masks_agents = (1 << NUM_AGENTS) - 1;
        env.ocean_io.agent_actions = env.actions;
        env.ocean_io.agent_obs = (float*)calloc(OCEAN_OBS_SIZE, sizeof(float));
        env.ocean_io.agent_rewards = env.rewards;
        env.ocean_io.agent_terminals = env.terminals;

        run_profile(&env, encounter_name, start_wave, profile_steps);

        free(env.observations);
        free(env.actions);
        free(env.rewards);
        free(env.terminals);
        free(env.action_masks);
        free(env.ocean_io.agent_obs);
        pvp_close(&env);
        return 0;
    }

    if (use_visual) {
#ifdef OSRS_VISUAL
        /* pvp_init uses internal buffers — no malloc needed */
        pvp_init(&env);
        /* set gear tier: --tier N forces both players to tier N,
           otherwise default LMS distribution (mostly tier 0) */
        if (gear_tier >= 0 && gear_tier <= 3) {
            for (int t = 0; t < 4; t++) env.pvp_runtime.gear_tier_weights[t] = 0.0f;
            env.pvp_runtime.gear_tier_weights[gear_tier] = 1.0f;
        } else {
            /* default LMS: 60% tier 0, 25% tier 1, 10% tier 2, 5% tier 3 */
            env.pvp_runtime.gear_tier_weights[0] = 0.60f;
            env.pvp_runtime.gear_tier_weights[1] = 0.25f;
            env.pvp_runtime.gear_tier_weights[2] = 0.10f;
            env.pvp_runtime.gear_tier_weights[3] = 0.05f;
        }
        env.ocean_io.agent_actions = env.actions;
        env.ocean_io.agent_obs = env._obs_buf;
        env.ocean_io.agent_rewards = env.rewards;
        env.ocean_io.agent_terminals = env.terminals;
        run_visual(
            &env,
            encounter_name,
            replay_path,
            start_wave,
            gear_tier,
            model_path,
            policy_mode,
            policy_seed);
        pvp_close(&env);
#else
        fprintf(stderr, "not compiled with visual support (use: make visual)\n");
        return 1;
#endif
    } else {
        /* headless: allocate external buffers (matches original demo) */
        env.observations = (float*)calloc(NUM_AGENTS * SLOT_NUM_OBSERVATIONS, sizeof(float));
        env.actions = (int*)calloc(NUM_AGENTS * NUM_ACTION_HEADS, sizeof(int));
        env.rewards = (float*)calloc(NUM_AGENTS, sizeof(float));
        env.terminals = (unsigned char*)calloc(NUM_AGENTS, sizeof(unsigned char));
        env.action_masks = (unsigned char*)calloc(NUM_AGENTS * ACTION_MASK_SIZE, sizeof(unsigned char));
        env.action_masks_agents = (1 << NUM_AGENTS) - 1;
        env.ocean_io.agent_actions = env.actions;
        env.ocean_io.agent_obs = (float*)calloc(OCEAN_OBS_SIZE, sizeof(float));
        env.ocean_io.agent_rewards = env.rewards;
        env.ocean_io.agent_terminals = env.terminals;

        printf("OSRS PvP C Environment Demo\n\n");

        printf("Running single verbose episode...\n");
        run_random_episode(&env, 1);

        printf("\n");
        benchmark(&env, 100000);

        printf("\nVerifying observations...\n");
        pvp_reset(&env);
        printf("Observation count per agent: %d\n", SLOT_NUM_OBSERVATIONS);
        printf("First 10 observations (agent 0): ");
        for (int i = 0; i < 10; i++) {
            printf("%.2f ", env.observations[i]);
        }
        printf("\n");

        printf("\nAction heads: %d\n", NUM_ACTION_HEADS);
        printf("Action dims: [");
        for (int i = 0; i < NUM_ACTION_HEADS; i++) {
            printf("%d", ACTION_HEAD_DIMS[i]);
            if (i < NUM_ACTION_HEADS - 1) {
                printf(", ");
            }
        }
        printf("]\n");

        printf("\nDemo complete.\n");

        free(env.observations);
        free(env.actions);
        free(env.rewards);
        free(env.terminals);
        free(env.action_masks);
        free(env.ocean_io.agent_obs);
        pvp_close(&env);
    }

    return 0;
}
