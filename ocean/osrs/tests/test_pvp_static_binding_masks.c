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

int main(void) {
    test_static_binding_exposes_separate_action_mask();

    printf("\n=== results: %d/%d passed ===\n", tests_passed, tests_run);
    if (tests_failed != 0) {
        return 1;
    }
    return 0;
}
