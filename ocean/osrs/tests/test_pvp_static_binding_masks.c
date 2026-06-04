#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ocean/osrs_pvp/binding.c"

cudaError_t cudaHostAlloc(void** ptr, size_t size, unsigned int flags) {
    (void)flags;
    *ptr = calloc(1, size);
    return *ptr ? 0 : 1;
}

cudaError_t cudaMalloc(void** ptr, size_t size) {
    *ptr = calloc(1, size);
    return *ptr ? 0 : 1;
}

cudaError_t cudaMemcpy(void* dst, const void* src, size_t size, cudaMemcpyKind kind) {
    (void)kind;
    memcpy(dst, src, size);
    return 0;
}

cudaError_t cudaMemcpyAsync(
    void* dst,
    const void* src,
    size_t size,
    cudaMemcpyKind kind,
    cudaStream_t stream
) {
    (void)stream;
    return cudaMemcpy(dst, src, size, kind);
}

cudaError_t cudaMemset(void* ptr, int value, size_t size) {
    memset(ptr, value, size);
    return 0;
}

cudaError_t cudaFree(void* ptr) {
    free(ptr);
    return 0;
}

cudaError_t cudaFreeHost(void* ptr) {
    free(ptr);
    return 0;
}

cudaError_t cudaSetDevice(int device) {
    (void)device;
    return 0;
}

cudaError_t cudaDeviceSynchronize(void) {
    return 0;
}

cudaError_t cudaStreamSynchronize(cudaStream_t stream) {
    (void)stream;
    return 0;
}

cudaError_t cudaStreamCreateWithFlags(cudaStream_t* stream, unsigned int flags) {
    (void)flags;
    *stream = NULL;
    return 0;
}

cudaError_t cudaStreamQuery(cudaStream_t stream) {
    (void)stream;
    return 0;
}

const char* cudaGetErrorString(cudaError_t error) {
    (void)error;
    return "cuda stub";
}

static int tests_run = 0;
static int tests_passed = 0;
static int tests_failed = 0;

#define ASSERT_INT_EQ(label, actual, expected) do { \
    tests_run++; \
    if ((actual) == (expected)) { \
        tests_passed++; \
    } else { \
        tests_failed++; \
        printf("  FAIL: %s got %d expected %d\n", (label), (actual), (expected)); \
    } \
} while (0)

#define ASSERT_TRUE(label, condition) do { \
    tests_run++; \
    if (condition) { \
        tests_passed++; \
    } else { \
        tests_failed++; \
        printf("  FAIL: %s\n", (label)); \
    } \
} while (0)

#define ASSERT_FLOAT_NEAR(label, actual, expected, tolerance) do { \
    tests_run++; \
    float actual_value = (float)(actual); \
    float expected_value = (float)(expected); \
    if (fabsf(actual_value - expected_value) <= (tolerance)) { \
        tests_passed++; \
    } else { \
        tests_failed++; \
        printf("  FAIL: %s got %.6f expected %.6f\n", \
            (label), actual_value, expected_value); \
    } \
} while (0)

static Dict* pvp_kwargs(void) {
    Dict* kwargs = create_dict(8);
    dict_set(kwargs, "seed", 73);
    dict_set(kwargs, "use_rollout_opponent", 1);
    dict_set(kwargs, "opponent_type", OPP_IMPROVED);
    dict_set(kwargs, "gear_tier", 3);
    dict_set(kwargs, "shaping_enabled", 1);
    dict_set(kwargs, "shaping_scale", 1.0);
    return kwargs;
}

static void test_static_binding_exposes_separate_action_mask(void) {
    printf("--- PvP static binding exposes separate action mask ---\n");

    Env env;
    memset(&env, 0, sizeof(env));
    StaticVec vec;
    memset(&vec, 0, sizeof(vec));

    float observations[NUM_AGENTS * OBS_SIZE];
    float actions[NUM_AGENTS * NUM_ATNS];
    float rewards[NUM_AGENTS];
    float terminals[NUM_AGENTS];
    unsigned char action_mask[NUM_AGENTS * MY_ACTION_MASK];
    memset(observations, 0, sizeof(observations));
    memset(actions, 0, sizeof(actions));
    memset(rewards, 0, sizeof(rewards));
    memset(terminals, 0, sizeof(terminals));
    memset(action_mask, 0, sizeof(action_mask));

    static_obs_set(&vec.observations, observations, NUM_AGENTS);
    vec.actions = actions;
    vec.rewards = rewards;
    vec.terminals = terminals;
    vec.action_mask = action_mask;
    vec.total_agents = NUM_AGENTS;
    vec.action_mask_size = MY_ACTION_MASK;

    Dict* kwargs = pvp_kwargs();
    my_init(&env, kwargs);
    my_setup_perm(&vec, &env, 0);
    c_reset(&env);

    ASSERT_INT_EQ("obs size keeps embedded mask tail", OBS_SIZE, OCEAN_OBS_SIZE);
    ASSERT_INT_EQ("separate mask size", MY_ACTION_MASK, ACTION_MASK_SIZE);
    ASSERT_TRUE("slot 0 action mask pointer", env.action_mask_ptr[0] == action_mask);
    ASSERT_TRUE("slot 1 action mask pointer",
        env.action_mask_ptr[1] == action_mask + ACTION_MASK_SIZE);

    for (int agent = 0; agent < NUM_AGENTS; agent++) {
        for (int i = 0; i < ACTION_MASK_SIZE; i++) {
            int mask_value = action_mask[agent * ACTION_MASK_SIZE + i];
            int obs_value = (int)observations[
                agent * OBS_SIZE + SLOT_NUM_OBSERVATIONS + i];
            ASSERT_INT_EQ("separate mask mirrors embedded mask", mask_value, obs_value);
        }
    }

    int offset = 0;
    for (int head = 0; head < NUM_ACTION_HEADS; head++) {
        int valid = 0;
        for (int action = 0; action < ACTION_HEAD_DIMS[head]; action++) {
            valid += action_mask[offset + action] != 0;
        }
        ASSERT_TRUE("each action head has a valid action", valid > 0);
        offset += ACTION_HEAD_DIMS[head];
    }
    ASSERT_INT_EQ("mask offset reaches ACTION_MASK_SIZE", offset, ACTION_MASK_SIZE);

    free(kwargs->items);
    free(kwargs);
    c_close(&env);
}

static void test_static_binding_sets_scripted_opponents(void) {
    printf("--- PvP static binding sets scripted opponents ---\n");

    Env envs[2];
    memset(envs, 0, sizeof(envs));
    StaticVec vec;
    memset(&vec, 0, sizeof(vec));
    vec.envs = envs;
    vec.size = 2;

    int scripted_opps[2] = { OPP_MASTER_NH, -1 };
    static_vec_set_env_scripted_opps(&vec, scripted_opps);

    ASSERT_INT_EQ("env 0 scripted opponent", envs[0].scripted_opp_type, OPP_MASTER_NH);
    ASSERT_INT_EQ("env 1 scripted opponent", envs[1].scripted_opp_type, -1);
}

static void test_binding_pfsp_stats_round_trip(void) {
    printf("--- PvP binding PFSP stats round trip ---\n");

    Env envs[2];
    memset(envs, 0, sizeof(envs));
    StaticVec vec;
    memset(&vec, 0, sizeof(vec));
    vec.envs = envs;
    vec.size = 2;

    envs[0].pvp.pvp_runtime.pfsp.pool_size = 1;
    envs[1].pvp.pvp_runtime.pfsp.pool_size = 1;

    int pool[2] = { OPP_MASTER_NH, OPP_SAVANT_NH };
    int cum_weights[2] = { 400, 1000 };
    binding_set_pfsp_weights(&vec, pool, cum_weights, 2);

    envs[0].pvp.pvp_runtime.pfsp.wins[0] = 1.0f;
    envs[0].pvp.pvp_runtime.pfsp.episodes[0] = 2.0f;
    envs[1].pvp.pvp_runtime.pfsp.wins[1] = 3.0f;
    envs[1].pvp.pvp_runtime.pfsp.episodes[1] = 4.0f;

    float wins[MAX_OPPONENT_POOL];
    float episodes[MAX_OPPONENT_POOL];
    memset(wins, 0, sizeof(wins));
    memset(episodes, 0, sizeof(episodes));
    int pool_size = 0;
    binding_get_pfsp_stats(&vec, wins, episodes, &pool_size);

    ASSERT_INT_EQ("pfsp pool size", pool_size, 2);
    ASSERT_FLOAT_NEAR("pfsp wins 0", wins[0], 1.0f, 1e-6f);
    ASSERT_FLOAT_NEAR("pfsp episodes 0", episodes[0], 2.0f, 1e-6f);
    ASSERT_FLOAT_NEAR("pfsp wins 1", wins[1], 3.0f, 1e-6f);
    ASSERT_FLOAT_NEAR("pfsp episodes 1", episodes[1], 4.0f, 1e-6f);
    ASSERT_FLOAT_NEAR("pfsp stats reset", envs[0].pvp.pvp_runtime.pfsp.episodes[0], 0.0f, 1e-6f);

}

static void test_expected_damage_prayer_modifier(void) {
    printf("--- PvP expected damage prayer modifier ---\n");

    float off_prayer = pvp_expected_reduced_uniform_damage(
        0, 100, PRAYER_NONE, ATTACK_STYLE_RANGED);
    float on_prayer = pvp_expected_reduced_uniform_damage(
        0, 100, PRAYER_PROTECT_RANGED, ATTACK_STYLE_RANGED);

    ASSERT_FLOAT_NEAR("off-prayer uniform EV", off_prayer, 50.0f, 1e-6f);
    ASSERT_TRUE("on-prayer EV is lower", on_prayer < off_prayer);
    ASSERT_FLOAT_NEAR("on-prayer EV ratio", on_prayer / off_prayer, 0.59f, 0.02f);
}

static void test_expected_damage_zero_accuracy(void) {
    printf("--- PvP expected damage zero accuracy ---\n");

    float ev = pvp_expected_spec_damage(
        ITEM_AGS, 0, 50, 100000, PRAYER_NONE, ATTACK_STYLE_MELEE);

    ASSERT_FLOAT_NEAR("zero accuracy EV", ev, 0.0f, 1e-6f);
}

static void test_expected_damage_standard_uniform(void) {
    printf("--- PvP expected damage standard uniform ---\n");

    float ev = pvp_expected_reduced_uniform_damage(
        0, 10, PRAYER_NONE, ATTACK_STYLE_MELEE);

    ASSERT_FLOAT_NEAR("uniform 0..10 EV", ev, 5.0f, 1e-6f);
}

static void test_expected_damage_representative_specs(void) {
    printf("--- PvP expected damage representative specs ---\n");

    float ags = pvp_expected_spec_damage(
        ITEM_AGS, 100000, 40, 0, PRAYER_NONE, ATTACK_STYLE_MELEE);
    float ags_expected = osrs_hit_chance(200000, 0)
        * pvp_expected_reduced_uniform_damage(0, 55, PRAYER_NONE, ATTACK_STYLE_MELEE);
    float claws = pvp_expected_spec_damage(
        ITEM_DRAGON_CLAWS, 100000, 40, 0, PRAYER_NONE, ATTACK_STYLE_MELEE);
    float dark_bow_miss = pvp_expected_spec_damage(
        ITEM_DARK_BOW, 0, 40, 100000, PRAYER_NONE, ATTACK_STYLE_RANGED);
    float voidwaker_off_prayer = pvp_expected_spec_damage(
        ITEM_VOIDWAKER, 100000, 40, 0, PRAYER_NONE, ATTACK_STYLE_MELEE);
    float voidwaker_on_prayer = pvp_expected_spec_damage(
        ITEM_VOIDWAKER, 100000, 40, 0, PRAYER_PROTECT_MAGIC, ATTACK_STYLE_MELEE);

    ASSERT_FLOAT_NEAR("AGS EV formula", ags, ags_expected, 1e-5f);
    ASSERT_TRUE("claws EV positive", claws > 0.0f);
    ASSERT_FLOAT_NEAR("dark bow miss min EV", dark_bow_miss, 16.0f, 1e-6f);
    ASSERT_TRUE("voidwaker magic prayer reduces EV", voidwaker_on_prayer < voidwaker_off_prayer);
}

int main(void) {
    setbuf(stdout, NULL);
    test_static_binding_exposes_separate_action_mask();
    test_static_binding_sets_scripted_opponents();
    test_binding_pfsp_stats_round_trip();
    test_expected_damage_prayer_modifier();
    test_expected_damage_zero_accuracy();
    test_expected_damage_standard_uniform();
    test_expected_damage_representative_specs();

    printf("\n=== results: %d/%d passed ===\n", tests_passed, tests_run);
    if (tests_failed != 0) {
        return 1;
    }
    return 0;
}
