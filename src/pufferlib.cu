#include <cuda_runtime.h>
#include <cuda_profiler_api.h>
#include <nvtx3/nvToolsExt.h>
#include <nvml.h>
#include <nccl.h>

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>
#include <time.h>
#include "models.cu"
#include "ocean.cu"
#include "muon.cu"
#include "vecenv.h"
#include "demostore.h"
#include "phase2_curriculum.h"

extern "C" {
    struct InfernoEnv;
    size_t inferno_env_snapshot_bytes(void) __attribute__((weak));
    int inferno_env_build_demo_snapshot_ladder(
        struct InfernoEnv* env, const DemoTrajectory* demo,
        DemoSnapshotLadder* out_ladder, DemoObsCache* out_obs_cache) __attribute__((weak));
    void inferno_env_set_phase2_ctx(
        struct InfernoEnv* env, Phase2Context* ctx, int env_idx) __attribute__((weak));
    void inferno_env_force_phase2_reset(struct InfernoEnv* env) __attribute__((weak));
    int inferno_env_validate_ladders(
        struct InfernoEnv* env, const DemoStore* store,
        DemoSnapshotLadder* const* ladders, int* out_cursor_ticks) __attribute__((weak));
    int inferno_env_profile_count(void) __attribute__((weak));
    const char* inferno_env_profile_name(int slot) __attribute__((weak));
    double inferno_env_profile_read_reset_ms(int slot) __attribute__((weak));
    struct InfernoEnv* inferno_env_at(void* envs_void, int idx) __attribute__((weak));
    void inferno_env_store_live_recurrent_state(
        struct InfernoEnv* env, const uint8_t* hidden_layer_major,
        int num_layers, size_t layer_stride_bytes, size_t row_bytes,
        int is_valid_for_next_forward) __attribute__((weak));
    void inferno_env_store_live_first_forward(
        struct InfernoEnv* env, const uint8_t* obs, size_t obs_size,
        const uint8_t* logits_value, size_t logits_value_size) __attribute__((weak));
    void inferno_env_record_phase2_first_forward_compare(
        struct InfernoEnv* env, float logit_l2, float logit_max_abs,
        float value_abs, float obs_l2, float obs_max_abs,
        int allclose, int obs_allclose) __attribute__((weak));
    void inferno_env_record_phase2_hidden_restore_compare(
        struct InfernoEnv* env, float l2, float max_abs,
        int allclose) __attribute__((weak));
    void c_reset(struct InfernoEnv* env);
}

static double wall_clock() {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}

enum LossIdx {
    LOSS_PG = 0, LOSS_VF = 1, LOSS_ENT = 2, LOSS_TOTAL = 3,
    LOSS_OLD_APPROX_KL = 4, LOSS_APPROX_KL = 5, LOSS_CLIPFRAC = 6,
    LOSS_PARENT_KL = 7, LOSS_PARENT_LOGIT_DELTA = 8, LOSS_N = 9, NUM_LOSSES = 10,
};

enum ProfileIdx {
    PROF_ROLLOUT = 0,
    PROF_EVAL_GPU,
    PROF_EVAL_ENV,
    PROF_TRAIN_MISC,
    PROF_TRAIN_FORWARD,
    NUM_PROF,
};

static const char* PROF_NAMES[NUM_PROF] = {
    "rollout",
    "eval_gpu",
    "eval_env",
    "train_misc",
    "train_forward",
};

#define NUM_TRAIN_EVENTS 5
typedef struct {
    cudaEvent_t events[NUM_TRAIN_EVENTS];
    float accum[NUM_PROF];
} ProfileT;

// Data collected by parallel environment workers. Each worker handles
// a constant subset of agents 
struct RolloutBuf {
    PrecisionTensor observations;  // (horizon, agents, input_size)
    PrecisionTensor actions;       // (horizon, agents, num_atns)
    PrecisionTensor values;        // (horizon, agents)
    PrecisionTensor logprobs;      // ...
    PrecisionTensor rewards;
    PrecisionTensor terminals;
    PrecisionTensor ratio;
    PrecisionTensor importance;
};

// Buffers are initialized as raw structs with only shape information. alloc_register
// stores the shape and data pointer. Memory is only allocated after all buffers are registered.
void register_rollout_buffers(RolloutBuf& bufs, Allocator* alloc, int T, int B, int input_size, int num_atns) {
    bufs = (RolloutBuf){
        .observations = {.shape = {T, B, input_size}},
        .actions      = {.shape = {T, B, num_atns}},
        .values       = {.shape = {T, B}},
        .logprobs     = {.shape = {T, B}},
        .rewards      = {.shape = {T, B}},
        .terminals    = {.shape = {T, B}},
        .ratio        = {.shape = {T, B}},
        .importance   = {.shape = {T, B}},
    };
    alloc_register(alloc, &bufs.observations);
    alloc_register(alloc, &bufs.actions);
    alloc_register(alloc, &bufs.values);
    alloc_register(alloc, &bufs.logprobs);
    alloc_register(alloc, &bufs.rewards);
    alloc_register(alloc, &bufs.terminals);
    alloc_register(alloc, &bufs.ratio);
    alloc_register(alloc, &bufs.importance);
}

// Train data layout is transposed to (B, T) from rollouts layout (T, B)
// This allows env workers to collect data with contiguous writes and
// training to perform several (though not all) ops in contiguous memory
struct TrainGraph {
    PrecisionTensor mb_state;       // (layers, B, hidden)
    PrecisionTensor mb_obs;         // (B, T, input_size)
    PrecisionTensor mb_actions;     // (B, T, num_atns)
    PrecisionTensor mb_logprobs;    // (B, T)
    PrecisionTensor mb_advantages;  // ...
    PrecisionTensor mb_values;
    PrecisionTensor mb_terminals;
    PrecisionTensor mb_returns;
    PrecisionTensor mb_ratio;
    PrecisionTensor mb_newvalue;
    PrecisionTensor mb_prio;        // (B,)
};

void register_train_buffers(TrainGraph& bufs, Allocator* alloc, int B, int T, int input_size,
        int hidden_size, int num_atns, int num_layers) {
    bufs = (TrainGraph){
        .mb_state =         {.shape = {num_layers, B, hidden_size}},
        .mb_obs =           {.shape = {B, T, input_size}},
        .mb_actions =       {.shape = {B, T, num_atns}},
        .mb_logprobs =      {.shape = {B, T}},
        .mb_advantages =    {.shape = {B, T}},
        .mb_values =        {.shape = {B, T}},
        .mb_terminals =     {.shape = {B, T}},
        .mb_returns =       {.shape = {B, T}},
        .mb_ratio =         {.shape = {B, T}},
        .mb_newvalue =      {.shape = {B, T}},
        .mb_prio =          {.shape = {B}},
    };
    alloc_register(alloc, &bufs.mb_obs);
    alloc_register(alloc, &bufs.mb_state);
    alloc_register(alloc, &bufs.mb_actions);
    alloc_register(alloc, &bufs.mb_logprobs);
    alloc_register(alloc, &bufs.mb_advantages);
    alloc_register(alloc, &bufs.mb_prio);
    alloc_register(alloc, &bufs.mb_values);
    alloc_register(alloc, &bufs.mb_terminals);
    alloc_register(alloc, &bufs.mb_returns);
    alloc_register(alloc, &bufs.mb_ratio);
    alloc_register(alloc, &bufs.mb_newvalue);
}

// PPO buffers + args are quite complex. We do the entire
// forward + backwards pass for the full loss function in one kernel
struct PPOGraphArgs {
    precision_t* out_ratio;
    precision_t* out_newvalue;
    const precision_t* actions;
    const precision_t* old_logprobs;
    const precision_t* advantages;
    const precision_t* prio;
    const precision_t* values;
    const precision_t* returns;
};

struct PPOKernelArgs {
    float* grad_logits;
    float* grad_logstd; // For continuous actions
    float* grad_values_pred;
    const precision_t* logits;
    const precision_t* logstd; // Continuous only
    const precision_t* values_pred;
    const precision_t* parent_logits;
    const float* masks;
    const float* adv_mean;
    const float* adv_var;
    const int* act_sizes;
    int num_atns;
    float clip_coef, vf_clip_coef, vf_coef, ent_coef, parent_kl_coef;
    int T_seq, A_total, N;
    int logits_stride_n, logits_stride_t, logits_stride_a;
    int values_stride_n, values_stride_t;
    int mask_stride;
    bool is_continuous;
};

struct PPOBuffersPuf {
    FloatTensor loss_output, grad_loss;
    FloatTensor saved_for_bwd;
    FloatTensor grad_logits, grad_values, grad_logstd, adv_scratch;
};

void register_ppo_buffers(PPOBuffersPuf& bufs, Allocator* alloc, int N, int T, int A_total, bool is_continuous) {
    long total = (long)N * T;
    bufs = (PPOBuffersPuf){
        .loss_output = {.shape = {1}},
        .grad_loss = {.shape = {1}},
        .saved_for_bwd = {.shape = {total, 5}},
        .grad_logits = {.shape = {N, T, A_total}},
        .grad_values = {.shape = {N, T, 1}},
        .grad_logstd = {.shape = {N, T, A_total}},
        .adv_scratch = {.shape = {2}},
    };
    alloc_register(alloc, &bufs.loss_output);
    alloc_register(alloc, &bufs.saved_for_bwd);
    alloc_register(alloc, &bufs.grad_loss);
    alloc_register(alloc, &bufs.grad_logits);
    alloc_register(alloc, &bufs.grad_values);
    if (is_continuous) {
        alloc_register(alloc, &bufs.grad_logstd);
    }
    alloc_register(alloc, &bufs.adv_scratch);
}

// Prioritized replay over single-epoch data. These kernels are
// the least cleaned because we will likely have a better method in 5.0
struct PrioBuffers {
    FloatTensor prio_probs, cdf, mb_prio;
    IntTensor idx;
};

void register_prio_buffers(PrioBuffers& bufs, Allocator* alloc, int B, int minibatch_segments) {
    bufs = (PrioBuffers){
        .prio_probs = {.shape = {B}},
        .cdf = {.shape = {B}},
        .mb_prio = {.shape = {minibatch_segments}},
        .idx = {.shape = {minibatch_segments}},
    };
    alloc_register(alloc, &bufs.prio_probs);
    alloc_register(alloc, &bufs.cdf);
    alloc_register(alloc, &bufs.idx);
    alloc_register(alloc, &bufs.mb_prio);
}

#define PUFFER_CURRICULUM_TYPES
#include "curriculum.cu"
#undef PUFFER_CURRICULUM_TYPES

// Slice: select dim0 index t, then narrow dim0 from start for count.
// 3D (T, B, F) -> (count, F); 2D (T, B) -> (count,)
inline PrecisionTensor puf_slice(PrecisionTensor& p, int t, int start, int count) {
    if (ndim(p.shape) == 3) {
        long B = p.shape[1], F = p.shape[2];
        return {.data = p.data + (t*B + start)*F, .shape = {count, F}};
    } else {
        long B = p.shape[1];
        return {.data = p.data + (t*B + start), .shape = {count}};
    }
}

inline FloatTensor puf_slice(FloatTensor& p, int t, int start, int count) {
    if (ndim(p.shape) == 3) {
        long B = p.shape[1], F = p.shape[2];
        return {.data = p.data + (t*B + start)*F, .shape = {count, F}};
    } else {
        long B = p.shape[1];
        return {.data = p.data + (t*B + start), .shape = {count}};
    }
}

struct EnvBuf {
    OBS_TENSOR_T obs;      // (total_agents, obs_size) - type defined per-env in binding.c
    FloatTensor actions;   // (total_agents, num_atns)
    FloatTensor rewards;   // (total_agents,)
    FloatTensor terminals; // (total_agents,)
};

StaticVec* create_environments(int num_buffers, int total_agents,
        const std::string& env_name, Dict* vec_kwargs, Dict* env_kwargs, EnvBuf& env) {
    StaticVec* vec = create_static_vec(total_agents, num_buffers, 1, vec_kwargs, env_kwargs);
    env.obs = {
        .data = (decltype(env.obs.data))vec->gpu_observations,
        .shape = {total_agents, get_obs_size()},
    };
    env.actions = { .data = (float*)vec->gpu_actions, .shape = {total_agents, get_num_atns()} };
    env.rewards = { .data = (float*)vec->gpu_rewards, .shape = {total_agents} };
    env.terminals = { .data = (float*)vec->gpu_terminals, .shape = {total_agents} };
    return vec;
}

typedef struct {
    // Layout
    int horizon;
    int total_agents;
    int num_buffers;
    // Model architecture
    int num_atns;
    int hidden_size;
    int num_layers;
    // Learning rate
    float lr;
    float min_lr_ratio;
    bool anneal_lr;
    // Optimizer
    float beta1;
    float beta2;
    float eps;
    bool aurora;
    // Training
    int minibatch_size;
    float replay_ratio;
    long total_timesteps;
    float max_grad_norm;
    // PPO
    float clip_coef;
    float vf_clip_coef;
    float vf_coef;
    float ent_coef;
    float parent_kl_coef;
    bool parent_kl_log;
    // GAE
    float gamma;
    float gae_lambda;
    // VTrace
    float vtrace_rho_clip;
    float vtrace_c_clip;
    // Priority
    float prio_alpha;
    float prio_beta0;
    bool anneal_prio_beta;
    // Curriculum state buffer
    int state_buffer_size;
    float cl_frac;
    bool anneal_cl;
    int warmup_states;
    int state_checkpoint_interval;
    float explore_alpha;
    float explore_beta;
    float explore_decay;
    // Flags
    bool reset_state;
    bool terminal_reset_state;
    int cudagraphs;
    bool profile;
    // Multi-GPU
    int rank;
    int world_size;
    int gpu_id;
    std::string nccl_id;  // raw bytes of ncclUniqueId (empty for single-GPU)
    // Threading
    int num_threads;
    int seed;
} HypersT;

typedef struct {
    Policy policy;
    PolicyWeights weights;       // current precision_t weights (structured)
    PolicyActivations train_activations;
    PolicyWeights parent_weights;
    PolicyActivations parent_train_activations;
    Allocator params_alloc;
    Allocator grads_alloc;
    Allocator activations_alloc;
    Allocator parent_params_alloc;
    Allocator parent_activations_alloc;
    Allocator parent_grads_alloc;
    StaticVec* vec;
    Muon muon;
    ncclComm_t nccl_comm;  // NCCL communicator for multi-GPU
    HypersT hypers;
    bool is_continuous;  // True if all action dimensions are continuous (size==1)
    PrecisionTensor* buffer_states;  // Per-buffer states for contiguous access
    PolicyActivations* buffer_activations;  // Per-buffer inference activations
    RolloutBuf rollouts;
    RolloutBuf train_rollouts;  // Pre-allocated transposed copy for train_impl
    EnvBuf env;
    TrainGraph train_buf;
    bool has_mask;
    int env_obs_width;
    int mask_width;
    FloatTensor ones_mask;
    FloatTensor rollout_masks;
    FloatTensor train_masks;
    FloatTensor mb_masks;
    DemoStore* phase2_store;
    DemoSnapshotLadder** phase2_ladders;
    Phase2Context* phase2_ctx;
    size_t phase2_hidden_state_size;
    bool live_phase2_hidden_capture;
    void** live_phase2_hidden_host;
    size_t live_phase2_hidden_host_bytes;
    bool live_phase2_first_forward_capture;
    bool phase2_first_forward_compare;
    bool phase2_hidden_restore_compare;
    void** phase2_first_forward_host;
    void** phase2_first_forward_obs_host;
    void** phase2_hidden_restore_host;
    size_t phase2_first_forward_host_bytes;
    size_t phase2_first_forward_obs_host_bytes;
    size_t phase2_hidden_restore_host_bytes;
    PrecisionTensor advantages_puf;  // Pre-allocated for train_impl (B, T)
    cudaGraphExec_t* fused_rollout_cudagraphs;  // [horizon][num_buffers]
    cudaGraphExec_t train_cudagraph;
    cudaStream_t* streams;  // per-buffer raw CUDA streams
    cudaStream_t default_stream;  // main-thread stream (captured once at init)
    IntTensor act_sizes_puf;    // CUDA int32 tensor of action head sizes
    FloatTensor losses_puf;     // (NUM_LOSSES,) f32 accumulator
    PPOBuffersPuf ppo_bufs_puf; // Pre-allocated buffers for ppo_loss_fwd_bwd
    PrioBuffers prio_bufs;      // Pre-allocated buffers for prio_replay
    StateBuffer state_buf;
    int curriculum_enabled;
    FloatTensor master_weights;  // fp32 master weights (flat); same buffer as param_puf in fp32 mode
    FloatTensor anchor_weights;
    float anchor_coef;
    PrecisionTensor parent_param_puf;
    bool has_parent_policy;
    PrecisionTensor param_puf;
    PrecisionTensor grad_puf;
    LongTensor rng_offset_puf;   // (num_buffers+1,) int64 CUDA device counters
    ProfileT profile;
    nvmlDevice_t nvml_device;
    long epoch;
    long global_step;
    double start_time;
    double last_log_time;
    long last_log_step;
    int train_warmup;
    bool rollout_captured;
    bool train_captured;
    ulong seed;
    curandStatePhilox4_32_10_t** rng_states;  // per-buffer persistent RNG states [num_buffers]
} PuffeRL;

static inline void capture_curriculum_checkpoint(PuffeRL* pufferl, int buffer_idx, int t);

Dict* log_environments_impl(PuffeRL& pufferl) {
    Dict* out = create_dict(PUFFER_ENV_LOG_DICT_CAPACITY);
    static_vec_log(pufferl.vec, out);
    if (pufferl.phase2_ctx) {
        Phase2CursorStats cs = phase2_cursor_stats(pufferl.phase2_ctx);
        dict_set(out, "phase2_cursor_mean_frac", cs.mean_frac);
        dict_set(out, "phase2_cursor_min_frac", cs.min_frac);
        dict_set(out, "phase2_cursor_max_frac", cs.max_frac);
        dict_set(out, "phase2_cursor_at_start", (float)cs.num_at_start);
    }
    return out;
}

inline void profile_begin(const char* tag, bool enable) {
    if (enable) nvtxRangePushA(tag);
}

inline void profile_end(bool enable) {
    if (enable) nvtxRangePop();
}

// Thread-local stream for per-buffer threads (set once by thread_init_wrapper)
static thread_local cudaStream_t tl_stream = 0;

// Thread initialization callback - sets thread-local stream once per thread
extern "C" void thread_init_wrapper(void* ctx, int buf) {
    PuffeRL* pufferl = (PuffeRL*)ctx;
    tl_stream = pufferl->streams[buf];
}

__global__ void rng_init(curandStatePhilox4_32_10_t* states, uint64_t seed, int n) {
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx < n) {
        curand_init(seed, idx, 0, &states[idx]);
    }
}

__global__ void split_obs_mask_kernel(
        precision_t* __restrict__ features,
        float* __restrict__ masks,
        const float* __restrict__ obs,
        int rows, int env_obs_width, int input_size, int mask_width) {
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    int total = rows * (input_size + mask_width);
    if (idx >= total) {
        return;
    }

    int row = idx / (input_size + mask_width);
    int col = idx % (input_size + mask_width);
    const float* src = obs + row * env_obs_width;
    if (col < input_size) {
        features[row * input_size + col] = from_float(src[col]);
    } else {
        int mask_col = col - input_size;
        masks[row * mask_width + mask_col] = src[input_size + mask_col];
    }
}

__global__ void zero_terminal_recurrent_state_kernel(
        precision_t* __restrict__ state,
        const float* __restrict__ terminals,
        int layers, int batch, int hidden) {
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    int total = layers * batch * hidden;
    if (idx >= total) {
        return;
    }

    int b = (idx / hidden) % batch;
    if (terminals[b] > 0.5f) {
        state[idx] = from_float(0.0f);
    }
}

__global__ void transpose_102_f32(float* __restrict__ dst,
        const float* __restrict__ src, int A, int B, int C) {
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    int total = A * B * C;
    if (idx >= total) {
        return;
    }
    int a = idx / (B * C), rem = idx % (B * C), b = rem / C, c = rem % C;
    dst[b * A * C + a * C + c] = src[idx];
}

__global__ void select_mask_copy(FloatTensor train_masks, FloatTensor mb_masks,
        const int* __restrict__ idx) {
    int mb = blockIdx.x;
    int src_row = idx[mb];
    int row_bytes = (numel(train_masks.shape) / train_masks.shape[0]) * sizeof(float);
    copy_bytes((const char*)train_masks.data, (char*)mb_masks.data,
        src_row, mb, row_bytes);
}

__device__ __forceinline__ float mask_value(
        const float* __restrict__ masks, int row, int stride, int offset) {
    return masks[(stride > 0 ? row * stride : 0) + offset];
}

__device__ __forceinline__ float safe_logit(const precision_t* logits,
        int logits_base, int logits_offset, int offset) {
    float l = to_float(logits[logits_base + logits_offset + offset]);
    if (isnan(l)) {
        l = 0.0f;
    }
    if (isinf(l)) {
        l = (l > 0) ? 3.4028e+38f : -3.4028e+38f;
    }
    return l;
}

// Expects action logits and values to be in the same contiguous buffer. See default decoder
__global__ void sample_logits(
        PrecisionTensor dec_out,              // (B, logits_dim + 1 for values)
        PrecisionTensor logstd_puf,           // (1, od) - continuous actions only
        IntTensor act_sizes_puf,              // (num_atns,) action head sizes
        precision_t* __restrict__ actions,    // (B, num_atns)
        precision_t* __restrict__ logprobs,   // (B,)
        precision_t* __restrict__ value_out,  // (B,)
        curandStatePhilox4_32_10_t* __restrict__ rng_states,
        const float* __restrict__ masks,
        int mask_stride) {
    int B = dec_out.shape[0];
    int fused_cols = dec_out.shape[1];
    int num_atns = numel(act_sizes_puf.shape);
    const int* act_sizes = act_sizes_puf.data;
    const precision_t* logits = dec_out.data;
    int logits_stride = fused_cols;
    int value_stride = fused_cols;
    bool is_continuous = logstd_puf.data != nullptr && numel(logstd_puf.shape) > 0;
    const precision_t* logstd = logstd_puf.data;
    int logstd_stride = is_continuous ? 0 : 0;  // 1D broadcast: stride 0
    const precision_t* value = logits + (fused_cols - 1);  // last column

    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx >= B) {
        return;
    }

    // Load persistent RNG state (advanced in-place each call)
    curandStatePhilox4_32_10_t state = rng_states[idx];

    int logits_base = idx * logits_stride;
    float total_log_prob = 0.0f;

    if (is_continuous) {
        // Continuous action sampling from Normal(mean, exp(logstd))
        constexpr float LOG_2PI = 1.8378770664093453f;  // log(2*pi)
        int logstd_base = idx * logstd_stride;  // separate stride for logstd (may be 0 for broadcast)

        for (int h = 0; h < num_atns; ++h) {
            float mean = to_float(logits[logits_base + h]);
            float log_std = to_float(logstd[logstd_base + h]);
            float std = expf(log_std);

            // Sample from N(0,1) and transform: action = mean + std * noise
            float noise = curand_normal(&state);
            float action = mean + std * noise;

            // Log probability: -0.5 * ((action - mean) / std)^2 - 0.5 * log(2*pi) - log(std)
            float normalized = (action - mean) / std;
            float log_prob = -0.5f * normalized * normalized - 0.5f * LOG_2PI - log_std;

            actions[idx * num_atns + h] = from_float(action);
            total_log_prob += log_prob;
        }
    } else {
        // Discrete action sampling (original multinomial logic)
        int logits_offset = 0;  // offset within row for current action head

        for (int h = 0; h < num_atns; ++h) {
            int A = act_sizes[h];  // size of this action head

            // Step 1: Find max and sum for numerical stability (with nan_to_num)
            float max_val = -INFINITY;
            float sum_exp = 0.0f;
            for (int a = 0; a < A; ++a) {
                if (mask_value(masks, idx, mask_stride, logits_offset + a) <= 0.0f) {
                    continue;
                }
                float l = safe_logit(logits, logits_base, logits_offset, a);
                if (l > max_val) {
                    sum_exp *= expf(max_val - l);
                    max_val = l;
                }
                sum_exp += expf(l - max_val);
            }
            float logsumexp = max_val + logf(sum_exp);

            // Step 3: Generate random value for this action head
            float rand_val = curand_uniform(&state);

            // Step 4: Multinomial sampling using inverse CDF
            float cumsum = 0.0f;
            int sampled_action = A - 1;  // default to last valid action

            for (int a = 0; a < A; ++a) {
                if (mask_value(masks, idx, mask_stride, logits_offset + a) <= 0.0f) {
                    continue;
                }
                sampled_action = a;
                float l = safe_logit(logits, logits_base, logits_offset, a);
                float prob = expf(l - logsumexp);
                cumsum += prob;
                if (rand_val < cumsum) {
                    sampled_action = a;
                    break;
                }
            }

            // Step 5: Gather log probability of sampled action
            float sampled_logit = safe_logit(logits, logits_base, logits_offset, sampled_action);
            float log_prob = sampled_logit - logsumexp;

            // Write action for this head
            actions[idx * num_atns + h] = from_float(sampled_action);
            total_log_prob += log_prob;

            // Advance to next action head
            logits_offset += A;
        }
    }

    // Write summed log probability (log of joint probability)
    logprobs[idx] = from_float(total_log_prob);

    // Copy value (fused to avoid separate elementwise kernel for strided->contiguous copy)
    value_out[idx] = value[idx * value_stride];

    // Save RNG state back for next call
    rng_states[idx] = state;
}

void phase2_restore_recurrent_state_for_buffer(
        PuffeRL* pufferl, int buf, PrecisionTensor state_puf, cudaStream_t stream) {
    if (!pufferl->phase2_ctx || pufferl->phase2_hidden_state_size == 0) return;

    int block_size = pufferl->vec->total_agents / pufferl->hypers.num_buffers;
    int start = buf * block_size;
    int layers = state_puf.shape[0];
    int batch = state_puf.shape[1];
    int hidden = state_puf.shape[2];
    size_t row_bytes = (size_t)hidden * sizeof(precision_t);
    size_t state_bytes = (size_t)layers * row_bytes;
    if (state_bytes != pufferl->phase2_hidden_state_size) {
        std::fprintf(stderr,
            "phase2 hidden restore: runtime hidden bytes %zu != demo hidden bytes %zu\n",
            state_bytes, pufferl->phase2_hidden_state_size);
        std::abort();
    }

    char* dst_base = (char*)state_puf.data;
    for (int e = 0; e < block_size; e++) {
        int env_idx = start + e;
        Phase2EnvState* es = &pufferl->phase2_ctx->env_states[env_idx];
        if (!es->needs_hidden_restore) continue;
        if (es->demo_id < 0 || es->slot < 0) {
            es->needs_hidden_restore = 0;
            continue;
        }

        DemoSnapshotLadder* ladder = pufferl->phase2_ladders[es->demo_id];
        const char* hidden_state = (const char*)demo_snapshot_ladder_hidden_at(ladder, es->slot);
        if (!hidden_state || ladder->hidden_size != state_bytes) {
            std::fprintf(stderr,
                "phase2 hidden restore: missing hidden state for demo %d slot %d\n",
                es->demo_id, es->slot);
            std::abort();
        }

        for (int layer = 0; layer < layers; layer++) {
            size_t dst_offset = ((size_t)layer * (size_t)batch + (size_t)e) * row_bytes;
            size_t src_offset = (size_t)layer * row_bytes;
            cudaMemcpyAsync(dst_base + dst_offset, hidden_state + src_offset,
                row_bytes, cudaMemcpyHostToDevice, stream);
        }
        es->needs_hidden_restore = 0;
    }
}

void compare_phase2_restored_hidden_for_buffer(
        PuffeRL* pufferl, int buf, PrecisionTensor state_puf, cudaStream_t stream) {
    if (!pufferl->phase2_hidden_restore_compare ||
            !pufferl->phase2_ctx ||
            pufferl->phase2_hidden_state_size == 0)
        return;
    if (!inferno_env_at || !inferno_env_record_phase2_hidden_restore_compare)
        return;

    int block_size = pufferl->vec->total_agents / pufferl->hypers.num_buffers;
    int start = buf * block_size;
    int layers = state_puf.shape[0];
    int batch = state_puf.shape[1];
    int hidden = state_puf.shape[2];
    size_t row_bytes = (size_t)hidden * sizeof(precision_t);
    size_t state_bytes = (size_t)layers * row_bytes;
    size_t buffer_bytes = (size_t)layers * (size_t)batch * row_bytes;
    if (state_bytes != pufferl->phase2_hidden_state_size) {
        std::fprintf(stderr,
            "phase2 hidden compare: runtime hidden bytes %zu != demo hidden bytes %zu\n",
            state_bytes, pufferl->phase2_hidden_state_size);
        std::abort();
    }
    if (buffer_bytes > pufferl->phase2_hidden_restore_host_bytes) {
        std::fprintf(stderr,
            "phase2 hidden compare: buffer bytes %zu > host bytes %zu\n",
            buffer_bytes, pufferl->phase2_hidden_restore_host_bytes);
        std::abort();
    }

    cudaMemcpyAsync(pufferl->phase2_hidden_restore_host[buf], state_puf.data,
        buffer_bytes, cudaMemcpyDeviceToHost, stream);
    cudaStreamSynchronize(stream);

    const uint8_t* host = (const uint8_t*)pufferl->phase2_hidden_restore_host[buf];
    void* envs_void = pufferl->vec->envs;
    for (int e = 0; e < block_size; e++) {
        int env_idx = start + e;
        Phase2EnvState* es = &pufferl->phase2_ctx->env_states[env_idx];
        if (!es->first_action_pending || es->demo_id < 0 || es->slot < 0)
            continue;

        DemoSnapshotLadder* ladder = pufferl->phase2_ladders[es->demo_id];
        const precision_t* expected =
            (const precision_t*)demo_snapshot_ladder_hidden_at(ladder, es->slot);
        if (!expected || ladder->hidden_size != state_bytes) {
            std::fprintf(stderr,
                "phase2 hidden compare: missing hidden state for demo %d slot %d\n",
                es->demo_id, es->slot);
            std::abort();
        }

        float sum_sq = 0.0f;
        float max_abs = 0.0f;
        for (int layer = 0; layer < layers; layer++) {
            const precision_t* actual = (const precision_t*)(
                host + ((size_t)layer * (size_t)batch + (size_t)e) * row_bytes);
            const precision_t* ref = expected + (size_t)layer * (size_t)hidden;
            for (int h = 0; h < hidden; h++) {
                float diff = to_float(actual[h]) - to_float(ref[h]);
                sum_sq += diff * diff;
                float ad = fabsf(diff);
                if (ad > max_abs) max_abs = ad;
            }
        }

        InfernoEnv* env = inferno_env_at(envs_void, env_idx);
        inferno_env_record_phase2_hidden_restore_compare(
            env, sqrtf(sum_sq), max_abs, max_abs < 1e-7f);
    }
}

void capture_live_phase2_recurrent_state_for_buffer(
        PuffeRL* pufferl, int buf, int t, PrecisionTensor state_puf, cudaStream_t stream) {
    if (!pufferl->live_phase2_hidden_capture ||
            !inferno_env_store_live_recurrent_state || !inferno_env_at)
        return;

    int block_size = pufferl->vec->total_agents / pufferl->hypers.num_buffers;
    int start = buf * block_size;
    int layers = state_puf.shape[0];
    int batch = state_puf.shape[1];
    int hidden = state_puf.shape[2];
    size_t row_bytes = (size_t)hidden * sizeof(precision_t);
    size_t layer_stride_bytes = (size_t)batch * row_bytes;
    size_t buffer_bytes = (size_t)layers * layer_stride_bytes;
    if (buffer_bytes > pufferl->live_phase2_hidden_host_bytes) {
        std::fprintf(stderr,
            "live phase2 hidden capture: buffer bytes %zu > host bytes %zu\n",
            buffer_bytes, pufferl->live_phase2_hidden_host_bytes);
        std::abort();
    }

    cudaMemcpyAsync(pufferl->live_phase2_hidden_host[buf], state_puf.data,
        buffer_bytes, cudaMemcpyDeviceToHost, stream);
    cudaStreamSynchronize(stream);

    const uint8_t* host = (const uint8_t*)pufferl->live_phase2_hidden_host[buf];
    void* envs_void = pufferl->vec->envs;
    int is_valid_for_next_forward = t + 1 < pufferl->hypers.horizon;
    for (int e = 0; e < block_size; e++) {
        const uint8_t* hidden_layer_major = host + (size_t)e * row_bytes;
        inferno_env_store_live_recurrent_state(
            inferno_env_at(envs_void, start + e),
            hidden_layer_major,
            layers,
            layer_stride_bytes,
            row_bytes,
            is_valid_for_next_forward);
    }
}

void capture_or_compare_phase2_first_forward_for_buffer(
        PuffeRL* pufferl, int buf, PrecisionTensor obs_puf,
        PrecisionTensor dec_puf, cudaStream_t stream) {
    if (!pufferl->live_phase2_first_forward_capture &&
            !pufferl->phase2_first_forward_compare)
        return;
    if (!inferno_env_at) return;

    int block_size = pufferl->vec->total_agents / pufferl->hypers.num_buffers;
    int start = buf * block_size;
    int row_elems = dec_puf.shape[1];
    size_t row_bytes = (size_t)row_elems * sizeof(precision_t);
    int obs_elems = obs_puf.shape[1];
    size_t obs_row_bytes = (size_t)obs_elems * sizeof(precision_t);
    size_t buffer_bytes = (size_t)block_size * row_bytes;
    size_t obs_buffer_bytes = (size_t)block_size * obs_row_bytes;
    if (buffer_bytes > pufferl->phase2_first_forward_host_bytes) {
        std::fprintf(stderr,
            "phase2 first-forward: buffer bytes %zu > host bytes %zu\n",
            buffer_bytes, pufferl->phase2_first_forward_host_bytes);
        std::abort();
    }
    if (obs_buffer_bytes > pufferl->phase2_first_forward_obs_host_bytes) {
        std::fprintf(stderr,
            "phase2 first-forward obs: buffer bytes %zu > host bytes %zu\n",
            obs_buffer_bytes, pufferl->phase2_first_forward_obs_host_bytes);
        std::abort();
    }

    cudaMemcpyAsync(pufferl->phase2_first_forward_host[buf], dec_puf.data,
        buffer_bytes, cudaMemcpyDeviceToHost, stream);
    cudaMemcpyAsync(pufferl->phase2_first_forward_obs_host[buf], obs_puf.data,
        obs_buffer_bytes, cudaMemcpyDeviceToHost, stream);
    cudaStreamSynchronize(stream);

    const uint8_t* host = (const uint8_t*)pufferl->phase2_first_forward_host[buf];
    const uint8_t* obs_host =
        (const uint8_t*)pufferl->phase2_first_forward_obs_host[buf];
    void* envs_void = pufferl->vec->envs;
    for (int e = 0; e < block_size; e++) {
        InfernoEnv* env = inferno_env_at(envs_void, start + e);
        const uint8_t* actual_bytes = host + (size_t)e * row_bytes;
        const uint8_t* actual_obs_bytes = obs_host + (size_t)e * obs_row_bytes;
        if (pufferl->live_phase2_first_forward_capture) {
            if (!inferno_env_store_live_first_forward) {
                std::fprintf(stderr,
                    "RECORD_LIVE_PHASE2_DEMO_EQUIV requested but first-forward hook is unavailable\n");
                std::abort();
            }
            inferno_env_store_live_first_forward(
                env, actual_obs_bytes, obs_row_bytes, actual_bytes, row_bytes);
        }

        if (!pufferl->phase2_first_forward_compare) continue;
        if (!pufferl->phase2_ctx || !inferno_env_record_phase2_first_forward_compare)
            continue;
        Phase2EnvState* es = &pufferl->phase2_ctx->env_states[start + e];
        if (!es->first_action_pending || es->demo_id < 0 || es->slot < 0)
            continue;
        uint32_t expected_bytes = 0;
        const void* expected = demostore_first_forward_at(
            pufferl->phase2_store, es->demo_id, es->slot, &expected_bytes);
        uint32_t expected_obs_bytes = 0;
        const void* expected_obs = demostore_first_forward_obs_at(
            pufferl->phase2_store, es->demo_id, es->slot, &expected_obs_bytes);
        if (!expected || expected_bytes != row_bytes) {
            std::fprintf(stderr,
                "phase2 first-forward compare: missing payload for demo %d slot %d "
                "(got %u bytes, want %zu)\n",
                es->demo_id, es->slot, expected_bytes, row_bytes);
            std::abort();
        }
        if (expected_obs && expected_obs_bytes != obs_row_bytes) {
            std::fprintf(stderr,
                "phase2 first-forward compare: obs payload for demo %d slot %d "
                "has %u bytes, want %zu\n",
                es->demo_id, es->slot, expected_obs_bytes, obs_row_bytes);
            std::abort();
        }

        const precision_t* actual = (const precision_t*)actual_bytes;
        const precision_t* ref = (const precision_t*)expected;
        float sum_sq = 0.0f;
        float max_abs = 0.0f;
        int max_idx = -1;
        for (int i = 0; i < row_elems - 1; i++) {
            float diff = to_float(actual[i]) - to_float(ref[i]);
            sum_sq += diff * diff;
            float ad = fabsf(diff);
            if (ad > max_abs) {
                max_abs = ad;
                max_idx = i;
            }
        }
        float value_abs = fabsf(
            to_float(actual[row_elems - 1]) - to_float(ref[row_elems - 1]));
        float l2 = sqrtf(sum_sq);
        int allclose = max_abs < 1e-4f && value_abs < 1e-4f;
        float obs_l2 = 0.0f;
        float obs_max_abs = 0.0f;
        int obs_max_idx = -1;
        int obs_allclose = 0;
        if (expected_obs) {
            const precision_t* actual_obs = (const precision_t*)actual_obs_bytes;
            const precision_t* ref_obs = (const precision_t*)expected_obs;
            float obs_sum_sq = 0.0f;
            for (int i = 0; i < obs_elems; i++) {
                float diff = to_float(actual_obs[i]) - to_float(ref_obs[i]);
                obs_sum_sq += diff * diff;
                float ad = fabsf(diff);
                if (ad > obs_max_abs) {
                    obs_max_abs = ad;
                    obs_max_idx = i;
                }
            }
            obs_l2 = sqrtf(obs_sum_sq);
            obs_allclose = obs_max_abs < 1e-4f;
        }
        inferno_env_record_phase2_first_forward_compare(
            env, l2, max_abs, value_abs, obs_l2, obs_max_abs,
            allclose, obs_allclose);

        static int printed_mismatches = 0;
        const char* print_env = std::getenv("PRINT_PHASE2_FIRST_FORWARD_MISMATCHES");
        int print_limit = print_env && print_env[0] ? std::atoi(print_env) : 0;
        if (print_limit > 0 && (!allclose || !obs_allclose)) {
            int printed = __sync_fetch_and_add(&printed_mismatches, 1);
            if (printed < print_limit) {
                std::fprintf(stderr,
                    "phase2 first-forward mismatch demo=%d slot=%d "
                    "logit_l2=%.9f logit_max=%.9f value_abs=%.9f "
                    "obs_l2=%.9f obs_max=%.9f logit_idx=%d obs_idx=%d "
                    "allclose=%d obs_allclose=%d\n",
                    es->demo_id, es->slot, l2, max_abs, value_abs,
                    obs_l2, obs_max_abs, max_idx, obs_max_idx,
                    allclose, obs_allclose);
            }
        }
    }
}

// Single step rollout forward pass. Called by each environment worker in their
// own buffer thread. This operation is cudagraphed.
extern "C" void net_callback_wrapper(void* ctx, int buf, int t) {
    PuffeRL* pufferl = (PuffeRL*)ctx;
    HypersT& hypers = pufferl->hypers;
    if (pufferl->curriculum_enabled) {
        capture_curriculum_checkpoint(pufferl, buf, t);
    }
    int graph = t * hypers.num_buffers + buf;
    profile_begin("fused_rollout", hypers.profile);

    cudaStream_t current_stream = tl_stream;
    bool phase2_hidden_active = pufferl->phase2_hidden_state_size > 0;
    bool live_hidden_capture_active = pufferl->live_phase2_hidden_capture;
    bool first_forward_active =
        pufferl->live_phase2_first_forward_capture ||
        pufferl->phase2_first_forward_compare;
    bool hidden_restore_compare_active = pufferl->phase2_hidden_restore_compare;
    if (pufferl->rollout_captured && !phase2_hidden_active &&
            !live_hidden_capture_active && !first_forward_active &&
            !hidden_restore_compare_active) {
        cudaGraphLaunch(pufferl->fused_rollout_cudagraphs[graph], current_stream);
        profile_end(hypers.profile);
        return;
    }

    bool capturing = pufferl->epoch == hypers.cudagraphs &&
        !phase2_hidden_active && !live_hidden_capture_active &&
        !first_forward_active && !hidden_restore_compare_active;
    if (capturing) {
        cudaStreamBeginCapture(current_stream, cudaStreamCaptureModeGlobal);
    }

    RolloutBuf& rollouts = pufferl->rollouts;
    EnvBuf& env = pufferl->env;
    int block_size = pufferl->vec->total_agents / hypers.num_buffers;
    int start = buf * block_size;
    cudaStream_t stream = current_stream;

    // Copy observations, rewards, terminals from GPU env buffers to rollout buffer
    OBS_TENSOR_T& obs_env = env.obs;
    PrecisionTensor obs_dst = puf_slice(rollouts.observations, t, start, block_size);
    if (pufferl->has_mask) {
        FloatTensor mask_dst = puf_slice(pufferl->rollout_masks, t, start, block_size);
        int n = block_size * (obs_dst.shape[1] + pufferl->mask_width);
        split_obs_mask_kernel<<<grid_size(n), BLOCK_SIZE, 0, stream>>>(
            obs_dst.data, mask_dst.data,
            (const float*)obs_env.data + (long)start * pufferl->env_obs_width,
            block_size, pufferl->env_obs_width, obs_dst.shape[1], pufferl->mask_width);
    } else {
        int n = block_size * obs_env.shape[1];
        cast<<<grid_size(n), BLOCK_SIZE, 0, stream>>>(
            obs_dst.data, obs_env.data + (long)start*obs_env.shape[1], n);
    }

    PrecisionTensor rew_dst = puf_slice(rollouts.rewards, t, start, block_size);
    int n = block_size;
    cast<<<grid_size(n), BLOCK_SIZE, 0, stream>>>(
        rew_dst.data, env.rewards.data + start, n);

    PrecisionTensor term_dst = puf_slice(rollouts.terminals, t, start, block_size);
    cast<<<grid_size(n), BLOCK_SIZE, 0, stream>>>(
        term_dst.data, env.terminals.data + start, n);

    // Policy forward pass for rollouts
    PrecisionTensor state_puf = pufferl->buffer_states[buf];
    if (hypers.terminal_reset_state) {
        int layers = state_puf.shape[0];
        int batch = state_puf.shape[1];
        int hidden = state_puf.shape[2];
        zero_terminal_recurrent_state_kernel<<<grid_size(layers * batch * hidden), BLOCK_SIZE, 0, stream>>>(
            state_puf.data, env.terminals.data + start, layers, batch, hidden);
    }
    phase2_restore_recurrent_state_for_buffer(pufferl, buf, state_puf, stream);
    compare_phase2_restored_hidden_for_buffer(pufferl, buf, state_puf, stream);
    PrecisionTensor dec_puf = policy_forward(&pufferl->policy, pufferl->weights, pufferl->buffer_activations[buf], obs_dst, state_puf, stream);
    capture_live_phase2_recurrent_state_for_buffer(pufferl, buf, t, state_puf, stream);
    capture_or_compare_phase2_first_forward_for_buffer(
        pufferl, buf, obs_dst, dec_puf, stream);

    // Sample actions, logprobs, values into rollout buffer
    PrecisionTensor act_slice = puf_slice(rollouts.actions, t, start, block_size);
    PrecisionTensor lp_slice = puf_slice(rollouts.logprobs, t, start, block_size);
    PrecisionTensor val_slice = puf_slice(rollouts.values, t, start, block_size);
    PrecisionTensor p_logstd = {};
    DecoderWeights* dw = (DecoderWeights*)pufferl->weights.decoder;
    if (dw->continuous) {
        p_logstd = dw->logstd;
    }
    const float* mask_ptr = pufferl->has_mask
        ? puf_slice(pufferl->rollout_masks, t, start, block_size).data
        : pufferl->ones_mask.data;
    int mask_stride = pufferl->has_mask ? pufferl->mask_width : 0;

    sample_logits<<<grid_size(block_size), BLOCK_SIZE, 0, stream>>>(
        dec_puf, p_logstd, pufferl->act_sizes_puf,
        act_slice.data, lp_slice.data, val_slice.data,
        pufferl->rng_states[buf], mask_ptr, mask_stride);

    // Copy actions to env
    long act_cols = env.actions.shape[1];
    cast<<<grid_size(numel(act_slice.shape)), BLOCK_SIZE, 0, stream>>>(
            env.actions.data + start * act_cols, act_slice.data, numel(act_slice.shape));

    if (capturing) {
        cudaGraph_t _graph;
        cudaStreamEndCapture(current_stream, &_graph);
        cudaGraphInstantiate(&pufferl->fused_rollout_cudagraphs[graph], _graph, 0);
        cudaGraphDestroy(_graph);
        cudaDeviceSynchronize();
    }
    profile_end(hypers.profile);
}


__device__ __forceinline__ void ppo_discrete_head(
        const precision_t* __restrict__ logits, int logits_base,
        const float* __restrict__ masks, int mask_base,
        int logits_stride_a, int logits_offset, int A, int act,
        float* out_logsumexp, float* out_entropy, float* out_logp) {
    float max_logit = -INFINITY;
    float sum = 0.0f;
    float act_logit = 0.0f;

    for (int a = 0; a < A; ++a) {
        if (masks[mask_base + logits_offset + a] <= 0.0f) {
            continue;
        }
        float l = to_float(logits[logits_base + (logits_offset + a) * logits_stride_a]);
        if (a == act) {
            act_logit = l;
        }
        if (l > max_logit) {
            sum *= __expf(max_logit - l);
            max_logit = l;
        }
        sum += __expf(l - max_logit);
    }
    float logsumexp = max_logit + __logf(sum);

    float ent = 0.0f;
    for (int a = 0; a < A; ++a) {
        if (masks[mask_base + logits_offset + a] <= 0.0f) {
            continue;
        }
        float l = to_float(logits[logits_base + (logits_offset + a) * logits_stride_a]);
        float logp = l - logsumexp;
        float p = __expf(logp);
        ent -= p * logp;
    }

    *out_logsumexp = logsumexp;
    *out_entropy = ent;
    *out_logp = act_logit - logsumexp;
}

__device__ __forceinline__ float ppo_masked_logsumexp(
        const precision_t* __restrict__ logits, int logits_base,
        const float* __restrict__ masks, int mask_base,
        int logits_stride_a, int logits_offset, int A) {
    float max_logit = -INFINITY;
    float sum = 0.0f;

    for (int a = 0; a < A; ++a) {
        if (masks[mask_base + logits_offset + a] <= 0.0f) {
            continue;
        }
        float l = to_float(logits[logits_base + (logits_offset + a) * logits_stride_a]);
        if (l > max_logit) {
            sum *= __expf(max_logit - l);
            max_logit = l;
        }
        sum += __expf(l - max_logit);
    }

    return max_logit + __logf(sum);
}

__device__ __forceinline__ void ppo_continuous_head(
        float mean, float log_std, float action,
        float* out_logp, float* out_entropy) {
    constexpr float HALF_LOG_2PI = 0.9189385332046727f;
    constexpr float HALF_1_PLUS_LOG_2PI = 1.4189385332046727f;
    float std = __expf(log_std);
    float normalized = (action - mean) / std;
    *out_logp = -0.5f * normalized * normalized - HALF_LOG_2PI - log_std;
    *out_entropy = HALF_1_PLUS_LOG_2PI + log_std;
}

__global__ void ppo_loss_compute(
        float* __restrict__ ppo_partials,
        PPOKernelArgs a, PPOGraphArgs g) {
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    int tid = threadIdx.x;
    int total_elements = a.N * a.T_seq;
    float inv_NT = 1.0f / float(total_elements);

    __shared__ float block_losses[LOSS_N][PPO_THREADS];
    for (int c = 0; c < LOSS_N; c++) {
        block_losses[c][tid] = 0.0f;
    }

    if (idx >= total_elements) {
        goto reduce;
    }

    {
    int n = idx / a.T_seq;
    int t = idx % a.T_seq;
    int nt = n * a.T_seq + t;

    int logits_base = n * a.logits_stride_n + t * a.logits_stride_t;
    int values_idx = n * a.values_stride_n + t * a.values_stride_t;
    int grad_logits_base = nt * a.A_total;
    int mask_base = (a.mask_stride > 0) ? nt * a.mask_stride : 0;

    // Shared computation (used by both forward and backward)

    float old_logp = to_float(g.old_logprobs[nt]);
    float adv = to_float(g.advantages[nt]);
    float w = to_float(g.prio[n]);
    float val = to_float(g.values[nt]);
    float ret = to_float(g.returns[nt]);
    float val_pred = to_float(a.values_pred[values_idx]);
    g.out_newvalue[nt] = from_float(val_pred);

    float adv_std = sqrtf(float(a.adv_var[0]));
    float adv_normalized = (adv - float(a.adv_mean[0])) / (adv_std + 1e-8f);

    // grad_loss is always 1.0 (set in post_create, never changes)
    float dL = inv_NT;
    float d_pg_loss = dL;
    float d_entropy_term = dL * (-a.ent_coef);

    // Value loss (forward) + value gradient (backward)

    float v_error = val_pred - val;
    float v_clipped = val + fmaxf(-a.vf_clip_coef, fminf(a.vf_clip_coef, v_error));
    float v_loss_unclipped = (val_pred - ret) * (val_pred - ret);
    float v_loss_clipped = (v_clipped - ret) * (v_clipped - ret);
    float v_loss = 0.5f * fmaxf(v_loss_unclipped, v_loss_clipped);

    // Value gradient
    bool use_clipped_vf = (v_loss_clipped > v_loss_unclipped);
    float d_val_pred = 0.0f;
    if (use_clipped_vf) {
        if (v_error >= -a.vf_clip_coef && v_error <= a.vf_clip_coef) {
            d_val_pred = v_clipped - ret;
        }
    } else {
        d_val_pred = val_pred - ret;
    }
    a.grad_values_pred[nt] = dL * a.vf_coef * d_val_pred;

    // Policy loss + gradients

    float pg_loss, total_entropy, logratio, ratio;
    float total_log_prob = 0.0f;
    total_entropy = 0.0f;
    float parent_kl = 0.0f;
    float parent_logit_delta = 0.0f;

    // Discrete-only: per-head arrays needed across forward + backward
    float head_logsumexp[MAX_ATN_HEADS];
    float head_entropy[MAX_ATN_HEADS];
    float parent_head_logsumexp[MAX_ATN_HEADS];
    int head_act[MAX_ATN_HEADS];

    if (!a.is_continuous) {
        int logits_offset = 0;
        for (int h = 0; h < a.num_atns; ++h) {
            int A = a.act_sizes[h];
            int act = static_cast<int>(g.actions[nt * a.num_atns + h]);
            head_act[h] = act;
            float lse, ent, lp;
            ppo_discrete_head(a.logits, logits_base, a.masks, mask_base,
                a.logits_stride_a, logits_offset, A, act, &lse, &ent, &lp);
            head_logsumexp[h] = lse;
            head_entropy[h] = ent;
            if (a.parent_logits != nullptr) {
                parent_head_logsumexp[h] = ppo_masked_logsumexp(
                    a.parent_logits, logits_base, a.masks, mask_base,
                    a.logits_stride_a, logits_offset, A);
            }
            total_log_prob += lp;
            total_entropy += ent;
            logits_offset += A;
        }
    } else {
        for (int h = 0; h < a.num_atns; ++h) {
            float mean = to_float(a.logits[logits_base + h * a.logits_stride_a]);
            float log_std = to_float(a.logstd[h]);
            float action = float(g.actions[nt * a.num_atns + h]);
            float lp, ent;
            ppo_continuous_head(mean, log_std, action, &lp, &ent);
            total_log_prob += lp;
            total_entropy += ent;
        }
    }

    // Shared pg loss computation
    logratio = total_log_prob - old_logp;
    ratio = __expf(logratio);
    g.out_ratio[nt] = from_float(ratio);
    float ratio_clipped = fmaxf(1.0f - a.clip_coef, fminf(1.0f + a.clip_coef, ratio));
    float wa = -w * adv_normalized;
    float pg_loss1 = wa * ratio;
    float pg_loss2 = wa * ratio_clipped;
    pg_loss = fmaxf(pg_loss1, pg_loss2);

    float d_ratio = wa * d_pg_loss;
    if (pg_loss2 > pg_loss1) {
        if (ratio <= (1.0f - a.clip_coef) || ratio >= (1.0f + a.clip_coef)) {
            d_ratio = 0.0f;
        }
    }
    float d_new_logp = d_ratio * ratio;

    if (!a.is_continuous) {
        int logits_offset = 0;
        for (int h = 0; h < a.num_atns; ++h) {
            int A = a.act_sizes[h];
            int act = head_act[h];
            float logsumexp = head_logsumexp[h];
            float ent = head_entropy[h];
            float parent_logsumexp = (a.parent_logits != nullptr) ? parent_head_logsumexp[h] : 0.0f;

            for (int j = 0; j < A; ++j) {
                if (a.masks[mask_base + logits_offset + j] <= 0.0f) {
                    a.grad_logits[grad_logits_base + logits_offset + j] = 0.0f;
                    continue;
                }
                float l = to_float(a.logits[logits_base + (logits_offset + j) * a.logits_stride_a]);
                float logp = l - logsumexp;
                float p = __expf(logp);
                float d_logit = (j == act) ? d_new_logp : 0.0f;
                d_logit -= p * d_new_logp;
                d_logit += d_entropy_term * p * (-ent - logp);
                if (a.parent_logits != nullptr) {
                    float parent_l = to_float(a.parent_logits[logits_base + (logits_offset + j) * a.logits_stride_a]);
                    float parent_logp = parent_l - parent_logsumexp;
                    float parent_p = __expf(parent_logp);
                    parent_kl += parent_p * (parent_logp - logp);
                    parent_logit_delta += fabsf(l - parent_l);
                    d_logit += dL * a.parent_kl_coef * (p - parent_p);
                }
                a.grad_logits[grad_logits_base + logits_offset + j] = d_logit;
            }
            logits_offset += A;
        }
    } else {
        for (int h = 0; h < a.num_atns; ++h) {
            float mean = to_float(a.logits[logits_base + h * a.logits_stride_a]);
            float log_std = to_float(a.logstd[h]);
            float std = __expf(log_std);
            float var = std * std;
            float action = float(g.actions[nt * a.num_atns + h]);
            float diff = action - mean;

            a.grad_logits[grad_logits_base + h] = d_new_logp * diff / var;
            a.grad_logstd[nt * a.num_atns + h] = d_new_logp * (diff * diff / var - 1.0f) + d_entropy_term;
        }
    }

    // Forward: loss partials
    float thread_loss = (pg_loss + a.vf_coef * v_loss - a.ent_coef * total_entropy
        + a.parent_kl_coef * parent_kl) * inv_NT;
    block_losses[LOSS_PG][tid] = pg_loss * inv_NT;
    block_losses[LOSS_VF][tid] = v_loss * inv_NT;
    block_losses[LOSS_ENT][tid] = total_entropy * inv_NT;
    block_losses[LOSS_TOTAL][tid] = thread_loss;
    block_losses[LOSS_OLD_APPROX_KL][tid] = (-logratio) * inv_NT;
    block_losses[LOSS_APPROX_KL][tid] = ((ratio - 1.0f) - logratio) * inv_NT;
    block_losses[LOSS_CLIPFRAC][tid] = (fabsf(ratio - 1.0f) > a.clip_coef ? 1.0f : 0.0f) * inv_NT;
    block_losses[LOSS_PARENT_KL][tid] = parent_kl * inv_NT;
    block_losses[LOSS_PARENT_LOGIT_DELTA][tid] = parent_logit_delta * inv_NT;
    } // end if (idx < total_elements)

// Deterministic aggregation
reduce:
    __syncthreads();

    for (int stride = PPO_THREADS / 2; stride > 0; stride >>= 1) {
        if (tid < stride) {
            for (int c = 0; c < LOSS_N; c++) {
                block_losses[c][tid] += block_losses[c][tid + stride];
            }
        }
        __syncthreads();
    }

    if (tid == 0) {
        int base = blockIdx.x * (LOSS_N + 1);
        ppo_partials[base] = block_losses[LOSS_TOTAL][0];
        for (int c = 0; c < LOSS_N; c++) {
            ppo_partials[base + 1 + c] = block_losses[c][0];
        }
    }
}

// Deterministic reduction of per-block PPO loss partials + count increment
__global__ void ppo_loss_reduce(
        float* __restrict__ loss,
        float* __restrict__ losses_acc,
        const float* __restrict__ partials,
        int num_blocks) {
    int tid = threadIdx.x;
    if (tid > LOSS_N) {
        return;
    }

    float sum = 0.0f;
    for (int b = 0; b < num_blocks; b++) {
        sum += partials[b * (LOSS_N + 1) + tid];
    }

    if (tid == 0) {
        *loss += sum;
    } else {
        losses_acc[tid - 1] += sum;
    }

    // Fold add_scalar: increment epoch count
    if (tid == 0) {
        losses_acc[LOSS_N] += 1.0f;
    }
}

__global__ void ppo_var_mean(const precision_t* __restrict__ src,
        float* __restrict__ var_out, float* __restrict__ mean_out, int n) {
    __shared__ float sdata[256];
    int tid = threadIdx.x;
    float sum = 0.0f;
    for (int i = tid; i < n; i += blockDim.x) {
        sum += to_float(src[i]);
    }
    sdata[tid] = sum;
    __syncthreads();
    for (int s = blockDim.x / 2; s > 0; s >>= 1) {
        if (tid < s) {
            sdata[tid] += sdata[tid + s];
        }
        __syncthreads();
    }
    float mean = sdata[0] / (float)n;
    if (tid == 0) {
        *mean_out = mean;
    }
    __syncthreads();
    float ss = 0.0f;
    for (int i = tid; i < n; i += blockDim.x) {
        float d = to_float(src[i]) - mean;
        ss += d * d;
    }
    sdata[tid] = ss;
    __syncthreads();
    for (int s = blockDim.x / 2; s > 0; s >>= 1) {
        if (tid < s) {
            sdata[tid] += sdata[tid + s];
        }
        __syncthreads();
    }
    if (tid == 0) {
        *var_out = sdata[0] / (float)(n - 1);
    }
}

// This is a huge kernel for a relatively cheap operation. But without this,
// it's death by a thousand cuts with repeated kernel launches. Even graphed, you
// blow up the memory bandwidth.
void ppo_loss_fwd_bwd(
        PrecisionTensor& dec_out,    // (N, T, fused_cols) — fused logits+value from decoder
        PrecisionTensor& logstd,     // continuous logstd or empty
        PrecisionTensor& parent_dec_out,
        TrainGraph& graph,
        IntTensor& act_sizes, FloatTensor& losses_acc,
        float clip_coef, float vf_clip_coef, float vf_coef, float ent_coef,
        float parent_kl_coef,
        PPOBuffersPuf& bufs, bool is_continuous,
        const float* masks, int mask_stride,
        cudaStream_t stream) {
    int N = dec_out.shape[0], T = dec_out.shape[1], fused_cols = dec_out.shape[2];
    int A_total = fused_cols - 1;  // last column is value
    int total = N * T;

    // Pointers into fused decoder output
    const precision_t* logits_ptr = dec_out.data;

    float* adv_var_ptr = bufs.adv_scratch.data;
    float* adv_mean_ptr = adv_var_ptr + 1;
    ppo_var_mean<<<1, 256, 0, stream>>>(
        graph.mb_advantages.data, adv_var_ptr, adv_mean_ptr, numel(graph.mb_advantages.shape));

    int ppo_grid = (total + PPO_THREADS - 1) / PPO_THREADS;

    static float* ppo_partials_buf = nullptr;
    static int ppo_partials_capacity = 0;
    int ppo_partials_needed = ppo_grid * (LOSS_N + 1);
    if (!ppo_partials_buf || ppo_partials_needed > ppo_partials_capacity) {
        if (ppo_partials_buf) cudaFree(ppo_partials_buf);
        ppo_partials_capacity = ppo_partials_needed;
        cudaMalloc(&ppo_partials_buf, ppo_partials_capacity * sizeof(float));
    }

    cudaMemsetAsync(bufs.loss_output.data, 0, sizeof(float), stream);

    PPOGraphArgs graph_args = {
        .out_ratio = graph.mb_ratio.data,
        .out_newvalue = graph.mb_newvalue.data,
        .actions = graph.mb_actions.data,
        .old_logprobs = graph.mb_logprobs.data,
        .advantages = graph.mb_advantages.data,
        .prio = graph.mb_prio.data,
        .values = graph.mb_values.data,
        .returns = graph.mb_returns.data,
    };

    PPOKernelArgs args = {
        .grad_logits = bufs.grad_logits.data,
        .grad_logstd = is_continuous ? bufs.grad_logstd.data : nullptr,
        .grad_values_pred = bufs.grad_values.data,
        .logits = logits_ptr,
        .logstd = is_continuous ? logstd.data : nullptr,
        .values_pred = logits_ptr + A_total,
        .parent_logits = parent_dec_out.data,
        .masks = masks,
        .adv_mean = adv_mean_ptr,
        .adv_var = adv_var_ptr,
        .act_sizes = act_sizes.data,
        .num_atns = (int)numel(act_sizes.shape),
        .clip_coef = clip_coef, .vf_clip_coef = vf_clip_coef,
        .vf_coef = vf_coef, .ent_coef = ent_coef, .parent_kl_coef = parent_kl_coef,
        .T_seq = T, .A_total = A_total, .N = N,
        .logits_stride_n = T * fused_cols, .logits_stride_t = fused_cols, .logits_stride_a = 1,
        .values_stride_n = T * fused_cols, .values_stride_t = fused_cols,
        .mask_stride = mask_stride,
        .is_continuous = is_continuous,
    };

    ppo_loss_compute<<<ppo_grid, PPO_THREADS, 0, stream>>>(ppo_partials_buf, args, graph_args);

    ppo_loss_reduce<<<1, LOSS_N + 1, 0, stream>>>(
        bufs.loss_output.data, losses_acc.data, ppo_partials_buf, ppo_grid);
}

#define PRIO_WARP_SIZE 32
#define PRIO_FULL_MASK 0xffffffff
#define PRIO_BLOCK_SIZE 256
#define PRIO_NUM_WARPS (PRIO_BLOCK_SIZE / PRIO_WARP_SIZE)
__global__ void compute_prio_adv_reduction(
        const precision_t* __restrict__ advantages,
        float* prio_weights, float prio_alpha, int stride) {
    int row = blockIdx.x;
    int tx = threadIdx.x;
    int offset = row * stride;

    float local_sum = 0.0f;
    for (int t = tx; t < stride; t += blockDim.x) {
        local_sum += fabsf(to_float(advantages[offset + t]));
    }

    for (int s = PRIO_WARP_SIZE / 2; s >= 1; s /= 2) {
        local_sum += __shfl_down_sync(PRIO_FULL_MASK, local_sum, s);
    }
    if (tx == 0) {
        float pw = __powf(local_sum, prio_alpha);
        if (isnan(pw) || isinf(pw)) {
            pw = 0.0f;
        }
        prio_weights[row] = pw;
    }
}

__global__ void compute_prio_normalize(float* prio_weights, int length) {
    __shared__ float shmem[PRIO_NUM_WARPS];
    __shared__ float block_sum;

    int tx = threadIdx.x;
    int lane = tx % PRIO_WARP_SIZE;
    int warp_id = tx / PRIO_WARP_SIZE;
    const float eps = 1e-6f;

    float local_sum = 0.0f;
    for (int t = tx; t < length; t += blockDim.x) {
        local_sum += prio_weights[t];
    }
    for (int s = PRIO_WARP_SIZE / 2; s >= 1; s /= 2) {
        local_sum += __shfl_down_sync(PRIO_FULL_MASK, local_sum, s);
    }
    if (lane == 0) {
        shmem[warp_id] = local_sum;
    }
    __syncthreads();

    if (warp_id == 0) {
        float val = (lane < PRIO_NUM_WARPS) ? shmem[lane] : 0.0f;
        for (int s = PRIO_NUM_WARPS / 2; s >= 1; s /= 2) {
            val += __shfl_down_sync(PRIO_FULL_MASK, val, s);
        }
        if (tx == 0) {
            block_sum = val + eps;
        }
    }
    __syncthreads();

    for (int t = tx; t < length; t += blockDim.x) {
        prio_weights[t] = (prio_weights[t] + eps) / block_sum;
    }
}

__global__ void compute_state_prio_normalize(float* prio_weights, int length) {
    __shared__ float shmem[PRIO_NUM_WARPS];
    __shared__ float block_sum;
    __shared__ int use_uniform;

    if (length <= 0) return;

    int tx = threadIdx.x;
    int lane = tx % PRIO_WARP_SIZE;
    int warp_id = tx / PRIO_WARP_SIZE;

    float local_sum = 0.0f;
    for (int t = tx; t < length; t += blockDim.x) {
        local_sum += prio_weights[t];
    }
    for (int s = PRIO_WARP_SIZE / 2; s >= 1; s /= 2) {
        local_sum += __shfl_down_sync(PRIO_FULL_MASK, local_sum, s);
    }
    if (lane == 0) {
        shmem[warp_id] = local_sum;
    }
    __syncthreads();

    if (warp_id == 0) {
        float val = (lane < PRIO_NUM_WARPS) ? shmem[lane] : 0.0f;
        for (int s = PRIO_NUM_WARPS / 2; s >= 1; s /= 2) {
            val += __shfl_down_sync(PRIO_FULL_MASK, val, s);
        }
        if (tx == 0) {
            block_sum = val;
            use_uniform = (val <= 0.0f || isnan(val) || isinf(val));
        }
    }
    __syncthreads();

    float inv_sum = use_uniform ? (1.0f / (float)length) : (1.0f / block_sum);
    for (int t = tx; t < length; t += blockDim.x) {
        prio_weights[t] = use_uniform ? inv_sum : prio_weights[t] * inv_sum;
    }
}

// mb_prio[i] = pow(total_agents * prio_probs[idx[i]], -anneal_beta)
__global__ void compute_prio_imp_weights(
        const int* __restrict__ indices,
        const float* __restrict__ prio_probs,
        float* mb_prio, int total_agents,
        float anneal_beta, int minibatch_segments) {
    int tx = threadIdx.x + blockIdx.x * blockDim.x;
    if (tx < minibatch_segments) {
        float value = prio_probs[indices[tx]] * (float)total_agents;
        mb_prio[tx] = __powf(value, -anneal_beta);
    }
}

__global__ void build_cdf(
        float* __restrict__ cdf, const float* __restrict__ probs, int B) {
    if (blockIdx.x == 0 && threadIdx.x == 0) {
        float cum = 0.0f;
        for (int i = 0; i < B; i++) {
            cum += probs[i];
            cdf[i] = cum;
        }
    }
}

__global__ void advance_rng_offset(long* __restrict__ offset_ptr, long delta) {
    if (blockIdx.x == 0 && threadIdx.x == 0) {
        *offset_ptr += delta;
    }
}

__global__ void multinomial_sample(int* __restrict__ out_idx, const float* __restrict__ cdf,
        int B, int num_samples, uint64_t seed, const long* __restrict__ offset_ptr) {
    int tid = blockIdx.x * blockDim.x + threadIdx.x;
    if (tid >= num_samples) return;

    uint64_t base_off = (uint64_t)(*offset_ptr);
    curandStatePhilox4_32_10_t rng_state;
    curand_init(seed, base_off + tid, 0, &rng_state);
    float u = curand_uniform(&rng_state);

    int lo = 0, hi = B - 1;
    while (lo < hi) {
        int mid = (lo + hi) / 2;
        if (cdf[mid] < u) lo = mid + 1;
        else hi = mid;
    }
    out_idx[tid] = lo;
}

// Prioritize high absolute advantage trajectories
// This is a form of implicit curriculum learning
// It is a major improvement in some complex environments
// The values of alpha and beta found by sweeps will tell you
// whether it is important for your task
void prio_replay_cuda(PrecisionTensor& advantages, float prio_alpha,
        int minibatch_segments, int total_agents, float anneal_beta,
        PrioBuffers& bufs, ulong seed, long* offset_ptr, cudaStream_t stream) {
    int B = advantages.shape[0], T = advantages.shape[1];
    compute_prio_adv_reduction<<<B, PRIO_WARP_SIZE, 0, stream>>>(
        advantages.data, bufs.prio_probs.data, prio_alpha, T);
    compute_prio_normalize<<<1, PRIO_BLOCK_SIZE, 0, stream>>>(
        bufs.prio_probs.data, B);
    build_cdf<<<1, 1, 0, stream>>>(bufs.cdf.data, bufs.prio_probs.data, B);
    int threads = 256;
    int blocks = (minibatch_segments + threads - 1) / threads;
    multinomial_sample<<<blocks, threads, 0, stream>>>(
        bufs.idx.data, bufs.cdf.data, B, minibatch_segments, seed, offset_ptr);
    advance_rng_offset<<<1, 1, 0, stream>>>(offset_ptr, (long)minibatch_segments);
    int p3_blocks = (minibatch_segments + PRIO_BLOCK_SIZE - 1) / PRIO_BLOCK_SIZE;
    compute_prio_imp_weights<<<p3_blocks, PRIO_BLOCK_SIZE, 0, stream>>>(
        bufs.idx.data, bufs.prio_probs.data,
        bufs.mb_prio.data, total_agents, anneal_beta, minibatch_segments);
}

#define PUFFER_CURRICULUM_IMPL
#include "curriculum.cu"
#undef PUFFER_CURRICULUM_IMPL

// Experience the puffer advantage! Generalized advantage estimation + V-Trace
// importance sampling correction in a single streamlined operation
__device__ void puff_advantage_row_scalar(
        const precision_t* values, const precision_t* rewards, const precision_t* dones,
        const precision_t* importance, precision_t* advantages, float gamma, float lambda,
        float rho_clip, float c_clip, int horizon) {
    float lastpufferlam = 0;
    for (int t = horizon-2; t >= 0; t--) {
        int t_next = t + 1;
        float nextnonterminal = 1.0f - to_float(dones[t_next]);
        float imp = to_float(importance[t]);
        float rho_t = fminf(imp, rho_clip);
        float c_t = fminf(imp, c_clip);
        float r_nxt = to_float(rewards[t_next]);
        float v = to_float(values[t]);
        float v_nxt = to_float(values[t_next]);
        float delta = rho_t*r_nxt + gamma*v_nxt*nextnonterminal - v;
        lastpufferlam = delta + gamma*lambda*c_t*lastpufferlam*nextnonterminal;
        advantages[t] = from_float(lastpufferlam);
    }
}

// These loading fns just optimize bandwidth for advantage since we call it on all
// the data every minibatch. This should change in 5.0
__device__ __forceinline__ void adv_vec_load(const float* ptr, float* out) {
    float4 v = *reinterpret_cast<const float4*>(ptr);
    out[0] = v.x; out[1] = v.y; out[2] = v.z; out[3] = v.w;
}

__device__ __forceinline__ void adv_vec_load(const __nv_bfloat16* ptr, float* out) {
    uint4 raw = *reinterpret_cast<const uint4*>(ptr);
    const __nv_bfloat16* bf = reinterpret_cast<const __nv_bfloat16*>(&raw);
    #pragma unroll
    for (int i = 0; i < 8; i++) {
        out[i] = __bfloat162float(bf[i]);
    }
}

// Store N floats as precision_t via 128-bit writes (float4 for f32, uint4 for bf16)
__device__ __forceinline__ void adv_vec_store(float* ptr, const float* vals) {
    *reinterpret_cast<float4*>(ptr) = make_float4(vals[0], vals[1], vals[2], vals[3]);
}

__device__ __forceinline__ void adv_vec_store(__nv_bfloat16* ptr, const float* vals) {
    // N=8 for bf16: all 8 elements fit in one uint4 (128 bits)
    __nv_bfloat16 tmp[8];
    #pragma unroll
    for (int i = 0; i < 8; i++) tmp[i] = __float2bfloat16(vals[i]);
    *reinterpret_cast<uint4*>(ptr) = *reinterpret_cast<const uint4*>(tmp);
}

__device__ __forceinline__ void puff_advantage_row_vec(
        const precision_t* values, const precision_t* rewards, const precision_t* dones,
        const precision_t* importance, precision_t* advantages, float gamma, float lambda,
        float rho_clip, float c_clip, int horizon) {
    constexpr int N = 16 / sizeof(precision_t);

    float lastpufferlam = 0.0f;
    int num_chunks = horizon / N;

    float next_value = to_float(values[horizon - 1]);
    float next_done = to_float(dones[horizon - 1]);
    float next_reward = to_float(rewards[horizon - 1]);

    for (int chunk = num_chunks - 1; chunk >= 0; chunk--) {
        int base = chunk * N;

        float v[N], r[N], d[N], imp[N];
        adv_vec_load(values + base, v);
        adv_vec_load(rewards + base, r);
        adv_vec_load(dones + base, d);
        adv_vec_load(importance + base, imp);

        float adv[N] = {0};
        int start_idx = (chunk == num_chunks - 1) ? (N - 2) : (N - 1);

        #pragma unroll
        for (int i = start_idx; i >= 0; i--) {
            float nextnonterminal = 1.0f - next_done;
            float rho_t = fminf(imp[i], rho_clip);
            float c_t = fminf(imp[i], c_clip);
            float delta = rho_t * (next_reward + gamma * next_value * nextnonterminal - v[i]);
            lastpufferlam = delta + gamma * lambda * c_t * lastpufferlam * nextnonterminal;
            adv[i] = lastpufferlam;
            next_value = v[i];
            next_done = d[i];
            next_reward = r[i];
        }

        adv_vec_store(advantages + base, adv);
    }
}

__global__ void puff_advantage(const precision_t* values, const precision_t* rewards,
        const precision_t* dones, const precision_t* importance, precision_t* advantages, float gamma,
        float lambda, float rho_clip, float c_clip, int num_steps, int horizon) {
    int row = blockIdx.x*blockDim.x + threadIdx.x;
    if (row >= num_steps) {
        return;
    }
    int offset = row*horizon;
    puff_advantage_row_vec(values + offset, rewards + offset, dones + offset,
        importance + offset, advantages + offset, gamma, lambda, rho_clip, c_clip, horizon);
}

__global__ void puff_advantage_scalar(const precision_t* values, const precision_t* rewards,
        const precision_t* dones, const precision_t* importance, precision_t* advantages, float gamma,
        float lambda, float rho_clip, float c_clip, int num_steps, int horizon) {
    int row = blockIdx.x*blockDim.x + threadIdx.x;
    if (row >= num_steps) {
        return;
    }
    int offset = row*horizon;
    puff_advantage_row_scalar(values + offset, rewards + offset, dones + offset,
        importance + offset, advantages + offset, gamma, lambda, rho_clip, c_clip, horizon);
}

void puff_advantage_cuda(PrecisionTensor& values, PrecisionTensor& rewards,
        PrecisionTensor& dones, PrecisionTensor& importance, PrecisionTensor& advantages,
        float gamma, float lambda, float rho_clip, float c_clip, cudaStream_t stream) {
    int num_steps = values.shape[0], horizon = values.shape[1];
    int blocks = grid_size(num_steps);
    constexpr int N = 16 / sizeof(precision_t);
    auto kernel = (horizon % N == 0) ? puff_advantage : puff_advantage_scalar;
    kernel<<<blocks, 256, 0, stream>>>(
        values.data, rewards.data, dones.data, importance.data,
        advantages.data, gamma, lambda, rho_clip, c_clip, num_steps, horizon);
}

// Minor copy bandwidth optimizations
__global__ void index_copy(char* __restrict__ dst, const int* __restrict__ idx,
        const char* __restrict__ src, int num_idx, int row_bytes) {
    int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i < num_idx) {
        int dst_row = idx[i];
        memcpy(dst + (int64_t)dst_row * row_bytes, src + (int64_t)i * row_bytes, row_bytes);
    }
}

__device__ __forceinline__ void copy_values_adv_returns(
        const precision_t* __restrict__ src_values, precision_t* __restrict__ dst_values,
        const precision_t* __restrict__ src_advantages, precision_t* __restrict__ dst_advantages,
        precision_t* __restrict__ dst_returns,
        int src_row, int dst_row, int horizon) {
    int srh = (int64_t)src_row * horizon;
    int drh = (int64_t)dst_row * horizon;
    const precision_t* s_values = src_values + srh;
    const precision_t* s_adv = src_advantages + srh;
    precision_t* d_values = dst_values + drh;
    precision_t* d_adv = dst_advantages + drh;
    precision_t* d_returns = dst_returns + drh;
    for (int i = threadIdx.x; i < horizon; i += blockDim.x) {
        precision_t val = s_values[i];
        precision_t adv = s_adv[i];
        d_values[i] = val;
        d_adv[i] = adv;
        d_returns[i] = from_float(to_float(val) + to_float(adv));
    }
}

__global__ void select_copy(RolloutBuf rollouts, TrainGraph graph,
        const int* __restrict__ idx, const precision_t* __restrict__ advantages,
        const float* __restrict__ mb_prio,
        const precision_t* __restrict__ row_importance) {
    int mb = blockIdx.x;
    int ch = blockIdx.y;
    int src_row = idx[mb];

    // Compute row byte counts from tensor shapes
    int obs_row_bytes = (numel(rollouts.observations.shape) / rollouts.observations.shape[0]) * sizeof(precision_t);
    int act_row_bytes = (numel(rollouts.actions.shape) / rollouts.actions.shape[0]) * sizeof(precision_t);
    int lp_row_bytes = (numel(rollouts.logprobs.shape) / rollouts.logprobs.shape[0]) * sizeof(precision_t);
    int term_row_bytes = (numel(rollouts.terminals.shape) / rollouts.terminals.shape[0]) * sizeof(precision_t);
    int horizon = rollouts.values.shape[1];

    switch (ch) {
    case 0:
        copy_bytes((const char*)rollouts.observations.data, (char*)graph.mb_obs.data, src_row, mb, obs_row_bytes);
        break;
    case 1:
        copy_bytes((const char*)rollouts.actions.data, (char*)graph.mb_actions.data, src_row, mb, act_row_bytes);
        break;
    case 2:
        copy_bytes((const char*)rollouts.logprobs.data, (char*)graph.mb_logprobs.data, src_row, mb, lp_row_bytes);
        break;
    case 3:
        copy_values_adv_returns(rollouts.values.data, graph.mb_values.data,
                advantages, graph.mb_advantages.data,
                graph.mb_returns.data, src_row, mb, horizon);
        break;
    case 4:
        if (threadIdx.x == 0) {
            float prio = mb_prio[mb];
            if (row_importance != nullptr) {
                prio *= to_float(row_importance[src_row]);
            }
            graph.mb_prio.data[mb] = from_float(prio);
        }
        break;
    case 5:
        copy_bytes((const char*)rollouts.terminals.data, (char*)graph.mb_terminals.data, src_row, mb, term_row_bytes);
        break;
    }
}

inline float cosine_annealing(float lr_base, float lr_min, long t, long T) {
    if (T == 0) return lr_base;
    float ratio = (double )t / (double) T;
    ratio = std::max(0.0f, std::min(1.0f, ratio));
    return lr_min + 0.5f*(lr_base - lr_min)*(1.0f + std::cos(M_PI * ratio));
}

__global__ void anchor_blend_weights(float* weights, const float* anchor, float coef, int n) {
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx >= n) return;
    weights[idx] += coef * (anchor[idx] - weights[idx]);
}

void init_parent_policy(PuffeRL& pufferl) {
    if (pufferl.has_parent_policy) {
        return;
    }

    int B_TT = pufferl.hypers.minibatch_size;
    pufferl.parent_weights = policy_weights_create(&pufferl.policy, &pufferl.parent_params_alloc);
    pufferl.parent_train_activations = policy_reg_train(
        &pufferl.policy, pufferl.parent_weights,
        &pufferl.parent_activations_alloc, &pufferl.parent_grads_alloc, B_TT);

    cudaError_t params_err = alloc_create(&pufferl.parent_params_alloc);
    if (params_err != cudaSuccess) {
        throw std::runtime_error("failed to allocate parent policy params");
    }
    cudaError_t acts_err = alloc_create(&pufferl.parent_activations_alloc);
    if (acts_err != cudaSuccess) {
        throw std::runtime_error("failed to allocate parent policy activations");
    }

    pufferl.parent_param_puf = {
        .data = (precision_t*)pufferl.parent_params_alloc.mem,
        .shape = {pufferl.parent_params_alloc.total_elems},
    };
    pufferl.has_parent_policy = true;
}

void load_parent_policy_weights(PuffeRL& pufferl, const std::vector<float>& weights) {
    init_parent_policy(pufferl);
    int64_t n = numel(pufferl.parent_param_puf.shape);
    if ((int64_t)weights.size() != n) {
        throw std::runtime_error("parent policy weight count mismatch");
    }
    if (USE_BF16) {
        float* weight_buf = nullptr;
        cudaMalloc(&weight_buf, weights.size() * sizeof(float));
        cudaMemcpy(weight_buf, weights.data(), weights.size() * sizeof(float), cudaMemcpyHostToDevice);
        cast<<<grid_size(n), BLOCK_SIZE, 0, pufferl.default_stream>>>(
            pufferl.parent_param_puf.data, weight_buf, n);
        cudaFree(weight_buf);
    } else {
        cudaMemcpy(
            pufferl.parent_param_puf.data, weights.data(),
            weights.size() * sizeof(float), cudaMemcpyHostToDevice);
    }
}

void train_impl(PuffeRL& pufferl) {
    // Update to HypersT& p
    HypersT& hypers = pufferl.hypers;
    if ((hypers.parent_kl_coef > 0.0f || hypers.parent_kl_log) && !pufferl.has_parent_policy) {
        throw std::runtime_error("parent KL requires loaded parent policy weights");
    }

    cudaEventRecord(pufferl.profile.events[0]);  // pre-loop start
    cudaStream_t train_stream = pufferl.default_stream;

    // Transpose from rollout layout (T, B, ...) to train layout (B, T, ...)
    RolloutBuf& src = pufferl.rollouts;
    RolloutBuf& rollouts = pufferl.train_rollouts;
    PrecisionTensor& advantages_puf = pufferl.advantages_puf;

    int T = src.observations.shape[0], B = src.observations.shape[1];
    int obs_size = (ndim(src.observations.shape) >= 3) ? src.observations.shape[2] : 1;
    int num_atns = (ndim(src.actions.shape) >= 3) ? src.actions.shape[2] : 1;

    transpose_102<<<grid_size(T*B*obs_size), BLOCK_SIZE, 0, train_stream>>>(
        rollouts.observations.data, src.observations.data, T, B, obs_size);
    transpose_102<<<grid_size(T*B*num_atns), BLOCK_SIZE, 0, train_stream>>>(
        rollouts.actions.data, src.actions.data, T, B, num_atns);
    transpose_102<<<grid_size(T*B), BLOCK_SIZE, 0, train_stream>>>(
        rollouts.logprobs.data, src.logprobs.data, T, B, 1);
    transpose_102<<<grid_size(T*B), BLOCK_SIZE, 0, train_stream>>>(
        rollouts.rewards.data, src.rewards.data, T, B, 1);
    transpose_102<<<grid_size(T*B), BLOCK_SIZE, 0, train_stream>>>(
        rollouts.terminals.data, src.terminals.data, T, B, 1);
    transpose_102<<<grid_size(T*B), BLOCK_SIZE, 0, train_stream>>>(
        rollouts.ratio.data, src.ratio.data, T, B, 1);
    transpose_102<<<grid_size(T*B), BLOCK_SIZE, 0, train_stream>>>(
        rollouts.values.data, src.values.data, T, B, 1);
    if (pufferl.has_mask) {
        transpose_102_f32<<<grid_size(T*B*pufferl.mask_width), BLOCK_SIZE, 0, train_stream>>>(
            pufferl.train_masks.data, pufferl.rollout_masks.data, T, B, pufferl.mask_width);
    }

    // We hard-clamp rewards to -1, 1. Our envs are mostly designed to respect this range
    clamp_precision_kernel<<<grid_size(numel(rollouts.rewards.shape)), BLOCK_SIZE, 0, train_stream>>>(
        rollouts.rewards.data, -1.0f, 1.0f, numel(rollouts.rewards.shape));

    // Set importance weights to 1.0
    fill_precision_kernel<<<grid_size(numel(rollouts.ratio.shape)), BLOCK_SIZE, 0, train_stream>>>(
        rollouts.ratio.data, from_float(1.0f), numel(rollouts.ratio.shape));

    // Inline any of these only used once
    int minibatch_size = hypers.minibatch_size;
    int batch_size = hypers.total_agents * hypers.horizon;
    int minibatch_segments = minibatch_size / hypers.horizon;
    float prio_beta0 = hypers.prio_beta0;
    float prio_alpha = hypers.prio_alpha;
    bool anneal_lr = hypers.anneal_lr;
    int current_epoch = pufferl.epoch;

    Muon* muon = &pufferl.muon;
    int total_epochs = hypers.total_timesteps / batch_size;
    if (anneal_lr) {
        float lr_min = hypers.min_lr_ratio * hypers.lr;
        float lr = cosine_annealing(hypers.lr, lr_min, current_epoch, total_epochs);
        cudaMemcpy(muon->lr_ptr, &lr, sizeof(float), cudaMemcpyHostToDevice);
    }

    float anneal_beta = prio_beta0;
    if (hypers.anneal_prio_beta && total_epochs > 0) {
        anneal_beta += (1.0f - prio_beta0) * prio_alpha *
            (float)current_epoch / (float)total_epochs;
    }
    TrainGraph& graph = pufferl.train_buf;
    cudaEventRecord(pufferl.profile.events[1]);  // pre-loop end

    int total_minibatches = hypers.replay_ratio * batch_size / hypers.minibatch_size;
    for (int mb = 0; mb < total_minibatches; ++mb) {
        cudaEventRecord(pufferl.profile.events[2]);  // start of misc (overwritten each iter)
        puf_zero(&advantages_puf, train_stream);

        profile_begin("compute_advantage", hypers.profile);
        puff_advantage_cuda(rollouts.values, rollouts.rewards, rollouts.terminals,
            rollouts.ratio, advantages_puf, hypers.gamma, hypers.gae_lambda,
            hypers.vtrace_rho_clip, hypers.vtrace_c_clip, train_stream);
        if (mb == 0 && pufferl.curriculum_enabled) {
            curriculum_update_advantages(&pufferl, &advantages_puf, train_stream);
        }
        profile_end(hypers.profile);

        profile_begin("compute_prio", hypers.profile);
        // Use the training RNG offset slot (last slot, index num_buffers)
        long* train_rng_offset = pufferl.rng_offset_puf.data + hypers.num_buffers;
        prio_replay_cuda(advantages_puf, prio_alpha, minibatch_segments,
            hypers.total_agents, anneal_beta,
            pufferl.prio_bufs, pufferl.seed, train_rng_offset, train_stream);
        profile_end(hypers.profile);

        profile_begin("train_select_and_copy", hypers.profile);
        if (hypers.reset_state) puf_zero(&graph.mb_state, train_stream);
        {
            RolloutBuf sel_src = rollouts;
            sel_src.values = rollouts.values;
            int mb_segs = pufferl.prio_bufs.idx.shape[0];
            const precision_t* row_importance = pufferl.curriculum_enabled
                ? pufferl.state_buf.importance.data : nullptr;
            select_copy<<<dim3(mb_segs, 6), SELECT_COPY_THREADS, 0, train_stream>>>(
                sel_src, graph, pufferl.prio_bufs.idx.data,
                advantages_puf.data, pufferl.prio_bufs.mb_prio.data,
                row_importance);
            if (pufferl.has_mask) {
                select_mask_copy<<<mb_segs, SELECT_COPY_THREADS, 0, train_stream>>>(
                    pufferl.train_masks, pufferl.mb_masks, pufferl.prio_bufs.idx.data);
            }
        }
        profile_end(hypers.profile);

        cudaEventRecord(pufferl.profile.events[3]);  // end misc / start forward
        profile_begin("train_forward_backward", hypers.profile);
        if (pufferl.train_captured) {
            cudaGraphLaunch(pufferl.train_cudagraph, train_stream);
        } else {
            bool capturing = pufferl.train_warmup == hypers.cudagraphs;
            if (capturing) {
                cudaStreamBeginCapture(train_stream, cudaStreamCaptureModeGlobal);
            }

            cudaStream_t stream = train_stream;
            PrecisionTensor obs_puf = graph.mb_obs;
            PrecisionTensor state_puf = graph.mb_state;
            PrecisionTensor reset_puf = hypers.terminal_reset_state ? graph.mb_terminals : PrecisionTensor();
            PrecisionTensor dec_puf = policy_forward_train(&pufferl.policy, pufferl.weights, pufferl.train_activations, obs_puf, state_puf, reset_puf, stream);
            PrecisionTensor parent_dec_puf = {};
            if (pufferl.has_parent_policy) {
                parent_dec_puf = policy_forward_train(&pufferl.policy, pufferl.parent_weights,
                    pufferl.parent_train_activations, obs_puf, state_puf, reset_puf, stream);
            }
            DecoderWeights* dw_train = (DecoderWeights*)pufferl.weights.decoder;
            PrecisionTensor p_logstd;
            if (dw_train->continuous) {
                p_logstd = dw_train->logstd;
            }

            ppo_loss_fwd_bwd(dec_puf, p_logstd, parent_dec_puf, graph,
                pufferl.act_sizes_puf, pufferl.losses_puf,
                hypers.clip_coef, hypers.vf_clip_coef, hypers.vf_coef, hypers.ent_coef,
                hypers.parent_kl_coef,
                pufferl.ppo_bufs_puf, pufferl.is_continuous,
                pufferl.has_mask ? pufferl.mb_masks.data : pufferl.ones_mask.data,
                pufferl.has_mask ? pufferl.mask_width : 0,
                stream);

            FloatTensor grad_logits_puf = pufferl.ppo_bufs_puf.grad_logits;
            FloatTensor grad_logstd_puf = pufferl.is_continuous ? pufferl.ppo_bufs_puf.grad_logstd : FloatTensor();
            FloatTensor grad_values_puf = pufferl.ppo_bufs_puf.grad_values;
            policy_backward(&pufferl.policy, pufferl.weights, pufferl.train_activations,
                grad_logits_puf, grad_logstd_puf, grad_values_puf, stream);

            muon_step(&pufferl.muon, pufferl.master_weights, pufferl.grad_puf, hypers.max_grad_norm, stream);
            if (USE_BF16) {
                int n = numel(pufferl.param_puf.shape);
                cast<<<grid_size(n), BLOCK_SIZE, 0, stream>>>(
                    pufferl.param_puf.data, pufferl.master_weights.data, n);
            }
            if (capturing) {
                cudaGraph_t _graph;
                cudaStreamEndCapture(train_stream, &_graph);
                cudaGraphInstantiate(&pufferl.train_cudagraph, _graph, 0);
                cudaGraphDestroy(_graph);
                cudaDeviceSynchronize();
                pufferl.train_captured = true;
            }
            pufferl.train_warmup++;
        }
        profile_end(hypers.profile);

        // This version is consistent with PufferLib 3.0. One of the major algorithmic
        // questions remaining is how and when to update value and advantage estimates.
        {
            int num_idx = numel(pufferl.prio_bufs.idx.shape);
            int row_bytes = (numel(graph.mb_ratio.shape) / graph.mb_ratio.shape[0]) * sizeof(precision_t);
            index_copy<<<grid_size(num_idx), BLOCK_SIZE, 0, train_stream>>>(
                (char*)rollouts.ratio.data, pufferl.prio_bufs.idx.data,
                (const char*)graph.mb_ratio.data, num_idx, row_bytes);
        }
        {
            int num_idx = numel(pufferl.prio_bufs.idx.shape);
            int row_bytes = graph.mb_newvalue.shape[1] * sizeof(precision_t);
            index_copy<<<grid_size(num_idx), BLOCK_SIZE, 0, train_stream>>>(
                (char*)rollouts.values.data, pufferl.prio_bufs.idx.data,
                (const char*)graph.mb_newvalue.data, num_idx, row_bytes);
        }
        cudaEventRecord(pufferl.profile.events[4]);  // end forward
    }
    if (pufferl.anchor_weights.data && pufferl.anchor_coef > 0.0f) {
        int n = numel(pufferl.master_weights.shape);
        anchor_blend_weights<<<grid_size(n), BLOCK_SIZE, 0, train_stream>>>(
            pufferl.master_weights.data,
            pufferl.anchor_weights.data,
            pufferl.anchor_coef,
            n);
        if (USE_BF16) {
            cast<<<grid_size(n), BLOCK_SIZE, 0, train_stream>>>(
                pufferl.param_puf.data, pufferl.master_weights.data, n);
        }
    }
    pufferl.epoch += 1;

    cudaStreamSynchronize(pufferl.default_stream);

    if (total_minibatches > 0) {
        float ms;
        // Pre-loop setup (transpose, advantage, allocs)
        cudaEventElapsedTime(&ms, pufferl.profile.events[0], pufferl.profile.events[1]);
        pufferl.profile.accum[PROF_TRAIN_MISC] += ms;
        // In-loop misc (last iteration, representative) scaled by count
        cudaEventElapsedTime(&ms, pufferl.profile.events[2], pufferl.profile.events[3]);
        pufferl.profile.accum[PROF_TRAIN_MISC] += ms * total_minibatches;
        // In-loop forward (last iteration, representative) scaled by count
        cudaEventElapsedTime(&ms, pufferl.profile.events[3], pufferl.profile.events[4]);
        pufferl.profile.accum[PROF_TRAIN_FORWARD] += ms * total_minibatches;
    }

}

std::unique_ptr<PuffeRL> create_pufferl_impl(HypersT& hypers,
        const std::string& env_name, Dict* vec_kwargs, Dict* env_kwargs) {
    auto pufferl = std::make_unique<PuffeRL>();
    pufferl->hypers = hypers;
    pufferl->nccl_comm = nullptr;
    pufferl->default_stream = 0;

    cudaSetDevice(hypers.gpu_id);

    // Multi-GPU: initialize NCCL
    if (hypers.world_size > 1) {
        if (hypers.nccl_id.size() != sizeof(ncclUniqueId))
            throw std::runtime_error("nccl_id must be " + std::to_string(sizeof(ncclUniqueId)) + " bytes");
        ncclUniqueId nccl_id;
        memcpy(&nccl_id, hypers.nccl_id.data(), sizeof(nccl_id));
        ncclCommInitRank(&pufferl->nccl_comm, hypers.world_size, nccl_id, hypers.rank);
        printf("Rank %d/%d: NCCL initialized\n", hypers.rank, hypers.world_size);
    }

    ulong seed = hypers.seed + hypers.rank;
    pufferl->seed = seed;

    // Load environment first to get input_size and action info from env
    // Create environments and set up action sizes
    StaticVec* vec = create_environments(hypers.num_buffers, hypers.total_agents,
        env_name, vec_kwargs, env_kwargs, pufferl->env);
    pufferl->vec = vec;
    assert(hypers.cl_frac >= 0.0f && "cl_frac must be nonnegative");
    assert(hypers.cl_frac <= 0.9f && "cl_frac must be <= 0.9");
    int initial_num_cl_envs = clamp_int(
        (int)(hypers.cl_frac * (float)vec->size), 0, vec->size);
    pufferl->curriculum_enabled = hypers.state_buffer_size > 0 && initial_num_cl_envs > 0;
    int agents_per_env = 0;
    if (pufferl->curriculum_enabled) {
        agents_per_env = fixed_agents_per_env(vec);
        assert(hypers.warmup_states >= 0 && "warmup_states must be nonnegative");
        assert(hypers.warmup_states <= hypers.state_buffer_size &&
            "warmup_states must be <= state_buffer_size");
        assert(hypers.state_checkpoint_interval > 0 &&
            "state_checkpoint_interval must be positive");
        assert(hypers.explore_decay >= 0.0f && hypers.explore_decay <= 1.0f &&
            "explore_decay must be in [0, 1]");
    }

    // Sanity check action space
    int num_action_heads = pufferl->env.actions.shape[1];
    int* raw_act_sizes = get_act_sizes();  // CPU int32 pointer from env
    int act_n = 0;
    int num_continuous = 0;
    int num_discrete = 0;
    for (int i = 0; i < num_action_heads; i++) {
        int val = raw_act_sizes[i];
        if (val == 1) {
            num_continuous++;
        } else {
            num_discrete++;
        }
        act_n += val;
    }
    assert((num_continuous == 0 || num_discrete == 0) &&
        "Mixed continuous/discrete action spaces not supported");
    pufferl->is_continuous = (num_continuous > 0);
    if (pufferl->is_continuous) {
        printf("Detected continuous action space with %d dimensions\n", num_action_heads);
    } else {
        printf("Detected discrete action space with %d heads\n", num_action_heads);
    }

    // Create profiling events
    for (int i = 0; i < NUM_TRAIN_EVENTS; i++) {
        cudaEventCreate(&pufferl->profile.events[i]);
    }
    memset(pufferl->profile.accum, 0, sizeof(pufferl->profile.accum));
    nvmlInit();
    nvmlDeviceGetHandleByIndex(hypers.gpu_id, &pufferl->nvml_device);

    DictItem* mask_entry = dict_get_unsafe(env_kwargs, "mask_in_obs");
    pufferl->has_mask = mask_entry && mask_entry->value > 0.0f;
    pufferl->env_obs_width = pufferl->env.obs.shape[1];
    pufferl->mask_width = act_n;

    int input_size = pufferl->has_mask
        ? pufferl->env_obs_width - pufferl->mask_width
        : pufferl->env_obs_width;
    if (input_size <= 0) {
        throw std::runtime_error("mask_in_obs leaves no observation features");
    }
    int hidden_size = hypers.hidden_size;
    int num_layers = hypers.num_layers;
    bool is_continuous = pufferl->is_continuous;
    int decoder_output_size = is_continuous ? num_action_heads : act_n;
    int minibatch_segments = hypers.minibatch_size / hypers.horizon;
    int inf_batch = vec->total_agents / hypers.num_buffers;
    int B_TT = minibatch_segments * hypers.horizon;
    int horizon = hypers.horizon;
    int total_agents = vec->total_agents;
    int batch = total_agents / hypers.num_buffers;
    int num_buffers = hypers.num_buffers;
    int num_cl_envs = pufferl->curriculum_enabled ?
        clamp_int((int)(hypers.cl_frac * (float)vec->size), 0, vec->size) : 0;

    Encoder encoder = {
        .forward = encoder_forward,
        .backward = encoder_backward,
        .init_weights = encoder_init_weights,
        .reg_params = encoder_reg_params,
        .reg_train = encoder_reg_train,
        .reg_rollout = encoder_reg_rollout,
        .create_weights = encoder_create_weights,
        .free_weights = encoder_free_weights,
        .free_activations = encoder_free_activations,
        .in_dim = input_size, .out_dim = hidden_size,
        .activation_size = sizeof(EncoderActivations),
    };
    create_custom_encoder(env_name, &encoder);
    Decoder decoder = {
        .forward = decoder_forward,
        .backward = decoder_backward,
        .init_weights = decoder_init_weights,
        .reg_params = decoder_reg_params,
        .reg_train = decoder_reg_train,
        .reg_rollout = decoder_reg_rollout,
        .create_weights = decoder_create_weights,
        .free_weights = decoder_free_weights,
        .free_activations = decoder_free_activations,
        .hidden_dim = hidden_size, .output_dim = decoder_output_size, .continuous = is_continuous,
    };
    Network network = {
        .forward = mingru_forward,
        .forward_train = mingru_forward_train,
        .backward = mingru_backward,
        .init_weights = mingru_init_weights,
        .reg_params = mingru_reg_params,
        .reg_train = mingru_reg_train,
        .reg_rollout = mingru_reg_rollout,
        .create_weights = mingru_create_weights,
        .free_weights = mingru_free_weights,
        .free_activations = mingru_free_activations,
        .hidden = hidden_size, .num_layers = num_layers, .horizon = hypers.horizon,
    };
    pufferl->policy = Policy{
        .encoder = encoder, .decoder = decoder, .network = network,
        .input_dim = input_size, .hidden_dim = hidden_size, .output_dim = decoder_output_size,
        .num_atns = act_n,
    };

    // Create and allocate params
    Allocator* params = &pufferl->params_alloc;
    Allocator* acts = &pufferl->activations_alloc;
    Allocator* grads = &pufferl->grads_alloc;

    // Buffers for weights, grads, and activations
    pufferl->weights = policy_weights_create(&pufferl->policy, params);
    pufferl->train_activations = policy_reg_train(&pufferl->policy, pufferl->weights, acts, grads, B_TT);
    pufferl->buffer_activations = (PolicyActivations*)calloc(num_buffers, sizeof(PolicyActivations));
    pufferl->buffer_states = (PrecisionTensor*)calloc(num_buffers, sizeof(PrecisionTensor));
    for (int i = 0; i < num_buffers; i++) {
        pufferl->buffer_activations[i] = policy_reg_rollout(
            &pufferl->policy, pufferl->weights, acts, inf_batch);
        pufferl->buffer_states[i] = {
            .shape = {num_layers, batch, hidden_size},
        };
        alloc_register(acts, &pufferl->buffer_states[i]);
    }
    register_rollout_buffers(pufferl->rollouts,
        acts, horizon, total_agents, input_size, num_action_heads);
    register_train_buffers(pufferl->train_buf,
        acts, minibatch_segments, horizon, input_size,
        hidden_size, num_action_heads, num_layers);
    register_rollout_buffers(pufferl->train_rollouts,
        acts, total_agents, horizon, input_size, num_action_heads);
    register_ppo_buffers(pufferl->ppo_bufs_puf,
        acts, minibatch_segments, hypers.horizon, decoder_output_size, is_continuous);
    register_prio_buffers(pufferl->prio_bufs,
        acts, hypers.total_agents, minibatch_segments);
    if (pufferl->curriculum_enabled) {
        register_state_buffer(&pufferl->state_buf,
            acts, hypers.state_buffer_size, total_agents, vec->size, agents_per_env,
            num_cl_envs, horizon, hypers.state_checkpoint_interval);
    }
    if (pufferl->has_mask) {
        pufferl->rollout_masks = {.shape = {horizon, total_agents, act_n}};
        pufferl->train_masks = {.shape = {total_agents, horizon, act_n}};
        pufferl->mb_masks = {.shape = {minibatch_segments, hypers.horizon, act_n}};
        alloc_register(acts, &pufferl->rollout_masks);
        alloc_register(acts, &pufferl->train_masks);
        alloc_register(acts, &pufferl->mb_masks);
    } else {
        pufferl->ones_mask = {.shape = {act_n}};
        alloc_register(acts, &pufferl->ones_mask);
    }

    // Extra cuda buffers just reuse activ allocator
    pufferl->rng_offset_puf = {.shape = {num_buffers + 1 + (int)pufferl->curriculum_enabled}};
    alloc_register(acts, &pufferl->rng_offset_puf);

    pufferl->act_sizes_puf  = {.shape = {num_action_heads}};
    alloc_register(acts, &pufferl->act_sizes_puf);

    pufferl->losses_puf = {.shape = {NUM_LOSSES}};
    alloc_register(acts, &pufferl->losses_puf);

    pufferl->advantages_puf = {.shape = {total_agents, horizon}};
    alloc_register(acts, &pufferl->advantages_puf);

    muon_init(&pufferl->muon, params, hypers.lr, hypers.beta1, hypers.eps, 0.0, acts, hypers.aurora);
    pufferl->muon.nccl_comm = pufferl->nccl_comm;
    pufferl->muon.world_size = hypers.world_size;

    // All buffers allocated here
    if (alloc_create(params) != cudaSuccess) {
        return nullptr;
    }
    if (alloc_create(grads) != cudaSuccess) {
        return nullptr;
    }
    if (alloc_create(acts) != cudaSuccess) {
        return nullptr;
    }

    pufferl->grad_puf = {.data = (precision_t*)grads->mem, .shape = {grads->total_elems}};
    pufferl->param_puf = {.data = (precision_t*)params->mem, .shape = {params->total_elems}};
    if (pufferl->curriculum_enabled) {
        if (!init_state_buffer(&pufferl->state_buf, hypers.total_agents)) {
            alloc_free(params);
            alloc_free(grads);
            alloc_free(acts);
            return nullptr;
        }
    }

    ulong init_seed = hypers.seed;
    policy_init_weights(&pufferl->policy, pufferl->weights, &init_seed, pufferl->default_stream);
    pufferl->master_weights = {.data = (float*)pufferl->param_puf.data, .shape = {params->total_elems}};
    if (USE_BF16) {
        pufferl->master_weights = {.shape = {params->total_elems}};
        cudaMalloc(&pufferl->master_weights.data, params->total_elems * sizeof(float));
        int n = numel(pufferl->param_puf.shape);
        cast<<<grid_size(n), BLOCK_SIZE, 0, pufferl->default_stream>>>(
            pufferl->master_weights.data, pufferl->param_puf.data, n);
    }
    pufferl->anchor_weights = {.data = NULL, .shape = {params->total_elems}};
    pufferl->anchor_coef = 0.0f;
    pufferl->parent_param_puf = {};
    pufferl->has_parent_policy = false;
    const char* live_hidden_capture_env = std::getenv("RECORD_LIVE_PHASE2_DEMO_HIDDEN");
    pufferl->live_phase2_hidden_capture =
        live_hidden_capture_env && live_hidden_capture_env[0] &&
        std::atoi(live_hidden_capture_env) != 0;
    const char* live_first_forward_env = std::getenv("RECORD_LIVE_PHASE2_DEMO_EQUIV");
    pufferl->live_phase2_first_forward_capture =
        live_first_forward_env && live_first_forward_env[0] &&
        std::atoi(live_first_forward_env) != 0;
    const char* compare_first_forward_env = std::getenv("COMPARE_PHASE2_FIRST_FORWARD");
    pufferl->phase2_first_forward_compare =
        compare_first_forward_env && compare_first_forward_env[0] &&
        std::atoi(compare_first_forward_env) != 0;
    const char* compare_hidden_restore_env = std::getenv("COMPARE_PHASE2_HIDDEN_RESTORE");
    pufferl->phase2_hidden_restore_compare =
        compare_hidden_restore_env && compare_hidden_restore_env[0] &&
        std::atoi(compare_hidden_restore_env) != 0;
    if (pufferl->live_phase2_hidden_capture) {
        if (!inferno_env_store_live_recurrent_state || !inferno_env_at) {
            std::fprintf(stderr,
                "RECORD_LIVE_PHASE2_DEMO_HIDDEN requested but inferno hidden hook is unavailable\n");
            std::abort();
        }
        size_t hidden_host_bytes =
            (size_t)num_layers * (size_t)batch * (size_t)hidden_size * sizeof(precision_t);
        pufferl->live_phase2_hidden_host_bytes = hidden_host_bytes;
        pufferl->live_phase2_hidden_host =
            (void**)std::calloc((size_t)num_buffers, sizeof(void*));
        if (!pufferl->live_phase2_hidden_host) {
            std::fprintf(stderr, "live phase2 hidden capture host pointer allocation failed\n");
            std::abort();
        }
        for (int i = 0; i < num_buffers; i++) {
            cudaError_t err = cudaHostAlloc(
                &pufferl->live_phase2_hidden_host[i],
                hidden_host_bytes,
                cudaHostAllocPortable);
            if (err != cudaSuccess) {
                std::fprintf(stderr,
                    "live phase2 hidden capture host allocation failed: %s\n",
                    cudaGetErrorString(err));
                std::abort();
            }
        }
    }
    if (pufferl->live_phase2_first_forward_capture ||
            pufferl->phase2_first_forward_compare) {
        if (!inferno_env_at ||
                (pufferl->live_phase2_first_forward_capture &&
                 !inferno_env_store_live_first_forward) ||
                (pufferl->phase2_first_forward_compare &&
                 !inferno_env_record_phase2_first_forward_compare)) {
            std::fprintf(stderr,
                "phase2 first-forward diagnostic requested but inferno hooks are unavailable\n");
            std::abort();
        }
        size_t first_forward_host_bytes =
            (size_t)batch * (size_t)(decoder_output_size + 1) * sizeof(precision_t);
        size_t first_forward_obs_host_bytes =
            (size_t)batch * (size_t)input_size * sizeof(precision_t);
        pufferl->phase2_first_forward_host_bytes = first_forward_host_bytes;
        pufferl->phase2_first_forward_obs_host_bytes = first_forward_obs_host_bytes;
        pufferl->phase2_first_forward_host =
            (void**)std::calloc((size_t)num_buffers, sizeof(void*));
        pufferl->phase2_first_forward_obs_host =
            (void**)std::calloc((size_t)num_buffers, sizeof(void*));
        if (!pufferl->phase2_first_forward_host ||
                !pufferl->phase2_first_forward_obs_host) {
            std::fprintf(stderr, "phase2 first-forward host pointer allocation failed\n");
            std::abort();
        }
        for (int i = 0; i < num_buffers; i++) {
            cudaError_t err = cudaHostAlloc(
                &pufferl->phase2_first_forward_host[i],
                first_forward_host_bytes,
                cudaHostAllocPortable);
            if (err != cudaSuccess) {
                std::fprintf(stderr,
                    "phase2 first-forward host allocation failed: %s\n",
                    cudaGetErrorString(err));
                std::abort();
            }
            err = cudaHostAlloc(
                &pufferl->phase2_first_forward_obs_host[i],
                first_forward_obs_host_bytes,
                cudaHostAllocPortable);
            if (err != cudaSuccess) {
                std::fprintf(stderr,
                    "phase2 first-forward obs host allocation failed: %s\n",
                    cudaGetErrorString(err));
                std::abort();
            }
        }
    }
    if (pufferl->phase2_hidden_restore_compare) {
        if (!inferno_env_at || !inferno_env_record_phase2_hidden_restore_compare) {
            std::fprintf(stderr,
                "phase2 hidden restore diagnostic requested but inferno hooks are unavailable\n");
            std::abort();
        }
        size_t hidden_restore_host_bytes =
            (size_t)num_layers * (size_t)batch *
            (size_t)hidden_size * sizeof(precision_t);
        pufferl->phase2_hidden_restore_host_bytes = hidden_restore_host_bytes;
        pufferl->phase2_hidden_restore_host =
            (void**)std::calloc((size_t)num_buffers, sizeof(void*));
        if (!pufferl->phase2_hidden_restore_host) {
            std::fprintf(stderr, "phase2 hidden restore host pointer allocation failed\n");
            std::abort();
        }
        for (int i = 0; i < num_buffers; i++) {
            cudaError_t err = cudaHostAlloc(
                &pufferl->phase2_hidden_restore_host[i],
                hidden_restore_host_bytes,
                cudaHostAllocPortable);
            if (err != cudaSuccess) {
                std::fprintf(stderr,
                    "phase2 hidden restore host allocation failed: %s\n",
                    cudaGetErrorString(err));
                std::abort();
            }
        }
    }

    // Per-buffer persistent RNG states
    int agents_per_buf = total_agents / num_buffers;
    pufferl->rng_states = (curandStatePhilox4_32_10_t**)calloc(num_buffers, sizeof(curandStatePhilox4_32_10_t*));
    for (int i = 0; i < num_buffers; i++) {
        cudaMalloc(&pufferl->rng_states[i], agents_per_buf * sizeof(curandStatePhilox4_32_10_t));
        rng_init<<<grid_size(agents_per_buf), BLOCK_SIZE>>>(
            pufferl->rng_states[i], pufferl->seed + i, agents_per_buf);
    }

    // Post-create initialization
    cudaMemcpy(pufferl->act_sizes_puf.data, raw_act_sizes, num_action_heads * sizeof(int), cudaMemcpyHostToDevice);
    if (!pufferl->has_mask) {
        std::vector<float> ones(act_n, 1.0f);
        cudaMemcpy(pufferl->ones_mask.data, ones.data(), act_n * sizeof(float), cudaMemcpyHostToDevice);
    }
    cudaMemset(pufferl->losses_puf.data, 0, NUM_LOSSES * sizeof(float));
    float one = 1.0f;
    cudaMemcpy(pufferl->ppo_bufs_puf.grad_loss.data, &one, sizeof(float), cudaMemcpyHostToDevice);
    muon_post_create(&pufferl->muon);

    // Cudagraph rolluts and entire training step
    if (hypers.cudagraphs >= 0 && !pufferl->live_phase2_hidden_capture &&
            !pufferl->live_phase2_first_forward_capture &&
            !pufferl->phase2_first_forward_compare) {
        pufferl->fused_rollout_cudagraphs = (cudaGraphExec_t*)calloc(horizon*num_buffers, sizeof(cudaGraphExec_t));
        pufferl->train_warmup = 0;

        // Snapshot weights + optimizer state before init-time capture
        long wb_bytes = numel(pufferl->master_weights.shape) * sizeof(float);
        void* saved_weights;
        cudaMalloc(&saved_weights, wb_bytes);
        cudaMemcpy(saved_weights, pufferl->master_weights.data, wb_bytes, cudaMemcpyDeviceToDevice);
        void* saved_momentum;
        cudaMalloc(&saved_momentum, wb_bytes);
        cudaMemcpy(saved_momentum, pufferl->muon.mb_puf.data, wb_bytes, cudaMemcpyDeviceToDevice);

        // Create per-buffer streams before capture so graphs are
        // captured and replayed on the same streams.
        pufferl->streams = (cudaStream_t*)calloc(num_buffers, sizeof(cudaStream_t));
        for (int i = 0; i < num_buffers; i++) {
            cudaStreamCreate(&pufferl->streams[i]);
            vec->streams[i] = pufferl->streams[i];
        }

        cudaStream_t saved_default = pufferl->default_stream;
        cudaStream_t saved_tl = tl_stream;
        cudaStream_t warmup_stream;
        cudaStreamCreate(&warmup_stream);
        pufferl->default_stream = warmup_stream;
        int saved_curriculum_enabled = pufferl->curriculum_enabled;
        pufferl->curriculum_enabled = 0;

        for (pufferl->epoch = 0; pufferl->epoch <= hypers.cudagraphs; pufferl->epoch++) {
            for (int i = 0; i < num_buffers * horizon; ++i) {
                int buf = i % num_buffers;
                tl_stream = pufferl->streams[buf];
                net_callback_wrapper(pufferl.get(), buf, i / num_buffers);
                cudaDeviceSynchronize();
            }
        }
        pufferl->rollout_captured = true;

        bool needs_parent_policy = hypers.parent_kl_coef > 0.0f || hypers.parent_kl_log;
        if (!needs_parent_policy) {
            tl_stream = warmup_stream;
            for (int i = 0; i <= hypers.cudagraphs; i++) {
                train_impl(*pufferl);
            }
        }

        cudaStreamSynchronize(warmup_stream);
        cudaDeviceSynchronize();
        pufferl->default_stream = saved_default;
        tl_stream = saved_tl;
        pufferl->curriculum_enabled = saved_curriculum_enabled;
        cudaStreamDestroy(warmup_stream);

        // Restore weights + optimizer state corrupted by warmup/capture
        cudaMemcpy(pufferl->master_weights.data, saved_weights, wb_bytes, cudaMemcpyDeviceToDevice);
        cudaFree(saved_weights);
        cudaMemcpy(pufferl->muon.mb_puf.data, saved_momentum, wb_bytes, cudaMemcpyDeviceToDevice);
        cudaFree(saved_momentum);
        if (USE_BF16) {
            int n = numel(pufferl->param_puf.shape);
            cast<<<grid_size(n), BLOCK_SIZE, 0, pufferl->default_stream>>>(
                pufferl->param_puf.data, pufferl->master_weights.data, n);
        }

        // Re-init RNG states corrupted by warmup
        for (int i = 0; i < num_buffers; i++) {
            rng_init<<<grid_size(agents_per_buf), BLOCK_SIZE>>>(
                pufferl->rng_states[i], pufferl->seed + i, agents_per_buf);
        }
        cudaMemset(pufferl->rng_offset_puf.data, 0,
            numel(pufferl->rng_offset_puf.shape) * sizeof(long));
        cudaDeviceSynchronize();

        pufferl->epoch = 0;
        pufferl->global_step = 0;
    }

    // Create per-buffer streams if not already created by cudagraph path
    if (!pufferl->streams) {
        pufferl->streams = (cudaStream_t*)calloc(num_buffers, sizeof(cudaStream_t));
        for (int i = 0; i < num_buffers; i++) {
            cudaStreamCreate(&pufferl->streams[i]);
            vec->streams[i] = pufferl->streams[i];
        }
    }

    create_static_threads(vec, hypers.num_threads, horizon, pufferl.get(),
        net_callback_wrapper, thread_init_wrapper);
    static_vec_reset(vec);

    if (hypers.profile) {
        cudaDeviceSynchronize();
        cudaProfilerStart();
    }

    double now = wall_clock();
    pufferl->start_time = now;
    pufferl->last_log_time = now;
    pufferl->last_log_step = 0;

    return pufferl;
}

int phase2_init_impl(
    PuffeRL& pufferl,
    const char* demo_dir,
    int num_atns,
    int snapshot_stride,
    int max_demos,
    uint64_t seed,
    float normal_start_frac,
    float randomize_rng_frac,
    float bc_coef,
    int bc_demos_per_minibatch,
    float promote_rate,
    float demote_rate,
    int backstep_ticks,
    float success_q_delta
) {
    if (bc_coef > 0.0f || bc_demos_per_minibatch > 0) {
        std::fprintf(stderr, "phase2_init: CUDA path supports curriculum resets only, not BC rows\n");
        std::abort();
    }
    if (!inferno_env_snapshot_bytes || !inferno_env_build_demo_snapshot_ladder ||
        !inferno_env_set_phase2_ctx || !inferno_env_validate_ladders ||
        !inferno_env_at) {
        std::fprintf(stderr, "phase2_init: inferno env hooks are unavailable\n");
        std::abort();
    }

    size_t snapshot_size = inferno_env_snapshot_bytes();
    DemoStore* store = demostore_create(max_demos);
    int loaded = demostore_load_dir(store, demo_dir, num_atns, 1,
        max_demos, (uint32_t)snapshot_size);
    if (loaded <= 0) {
        demostore_destroy(store);
        std::fprintf(stderr, "phase2_init: no demos loaded from %s\n", demo_dir);
        std::abort();
    }

    PrecisionTensor state0 = pufferl.buffer_states[0];
    size_t runtime_hidden_size = (size_t)state0.shape[0] *
        (size_t)state0.shape[2] * sizeof(precision_t);
    size_t demo_hidden_size = store->demos[0].hidden_state_size;
    for (int i = 0; i < store->num_demos; i++) {
        if (store->demos[i].hidden_state_size != demo_hidden_size) {
            std::fprintf(stderr, "phase2_init: inconsistent hidden size in demo %d\n", i);
            std::abort();
        }
    }
    if (demo_hidden_size > 0 && demo_hidden_size != runtime_hidden_size) {
        std::fprintf(stderr,
            "phase2_init: demo hidden bytes %zu != runtime hidden bytes %zu\n",
            demo_hidden_size, runtime_hidden_size);
        std::abort();
    }

    DemoSnapshotLadder** ladders = (DemoSnapshotLadder**)std::calloc(
        (size_t)store->num_demos, sizeof(DemoSnapshotLadder*));
    void* envs_void = pufferl.vec->envs;
    InfernoEnv* env0 = inferno_env_at(envs_void, 0);
    for (int i = 0; i < store->num_demos; i++) {
        DemoTrajectory* demo = &store->demos[i];
        ladders[i] = demo_snapshot_ladder_create(
            i, snapshot_stride, demo->num_snapshots,
            snapshot_size, demo->hidden_state_size);
        if (inferno_env_build_demo_snapshot_ladder(env0, demo, ladders[i], NULL) != 0) {
            std::fprintf(stderr, "phase2_init: ladder build failed for demo %d\n", i);
            std::abort();
        }
    }

    int* cursor_ticks = (int*)std::calloc((size_t)store->num_demos, sizeof(int));
    inferno_env_validate_ladders(env0, store, ladders, cursor_ticks);
    for (int i = 0; i < store->num_demos; i++) {
        store->demos[i].cursor_tick = cursor_ticks[i];
    }
    std::free(cursor_ticks);
    c_reset(env0);

    Phase2Context* ctx = phase2_ctx_create(store, ladders, pufferl.vec->total_agents, seed);
    ctx->normal_start_frac = normal_start_frac;
    ctx->randomize_rng_frac = randomize_rng_frac;
    ctx->promote_rate = promote_rate;
    ctx->demote_rate = demote_rate;
    ctx->backstep_ticks = backstep_ticks;
    ctx->success_q_delta = success_q_delta;

    for (int e = 0; e < pufferl.vec->total_agents; e++) {
        inferno_env_set_phase2_ctx(inferno_env_at(envs_void, e), ctx, e);
    }

    pufferl.phase2_store = store;
    pufferl.phase2_ladders = ladders;
    pufferl.phase2_ctx = ctx;
    pufferl.phase2_hidden_state_size = demo_hidden_size;

    std::fprintf(stderr,
        "phase2_init: %d demos, stride=%d, %d envs, hidden_bytes=%zu\n",
        store->num_demos, snapshot_stride, pufferl.vec->total_agents,
        demo_hidden_size);
    return store->num_demos;
}

void phase2_reset_impl(PuffeRL& pufferl) {
    if (!pufferl.phase2_ctx) {
        std::fprintf(stderr, "phase2_reset: phase2 context is not initialized\n");
        std::abort();
    }
    if (!inferno_env_force_phase2_reset || !inferno_env_at) {
        std::fprintf(stderr, "phase2_reset: inferno env hooks are unavailable\n");
        std::abort();
    }

    void* envs_void = pufferl.vec->envs;
    for (int e = 0; e < pufferl.vec->total_agents; e++) {
        inferno_env_force_phase2_reset(inferno_env_at(envs_void, e));
    }
    std::memset(pufferl.vec->rewards, 0,
        (size_t)pufferl.vec->total_agents * sizeof(float));
    std::memset(pufferl.vec->terminals, 0,
        (size_t)pufferl.vec->total_agents * sizeof(float));
    if (pufferl.vec->gpu) {
        cudaMemcpy(
            pufferl.vec->gpu_observations,
            pufferl.vec->observations,
            (size_t)pufferl.vec->total_agents *
                (size_t)pufferl.env.obs.shape[1] *
                sizeof(*pufferl.env.obs.data),
            cudaMemcpyHostToDevice);
        cudaMemset(
            pufferl.vec->gpu_rewards,
            0,
            (size_t)pufferl.vec->total_agents * sizeof(float));
        cudaMemset(
            pufferl.vec->gpu_terminals,
            0,
            (size_t)pufferl.vec->total_agents * sizeof(float));
    }
}

void phase2_close(PuffeRL& pufferl) {
    if (!pufferl.phase2_ctx) {
        return;
    }

    void* envs_void = pufferl.vec->envs;
    for (int e = 0; e < pufferl.vec->total_agents; e++) {
        inferno_env_set_phase2_ctx(inferno_env_at(envs_void, e), NULL, e);
    }

    phase2_ctx_destroy(pufferl.phase2_ctx);
    for (int i = 0; i < pufferl.phase2_store->num_demos; i++) {
        demo_snapshot_ladder_destroy(pufferl.phase2_ladders[i]);
    }
    std::free(pufferl.phase2_ladders);
    demostore_destroy(pufferl.phase2_store);
    pufferl.phase2_ctx = NULL;
    pufferl.phase2_ladders = NULL;
    pufferl.phase2_store = NULL;
    pufferl.phase2_hidden_state_size = 0;
}

void close_impl(PuffeRL& pufferl) {
    cudaDeviceSynchronize();
    phase2_close(pufferl);
    if (pufferl.hypers.profile) {
        cudaProfilerStop();
    }

    if (pufferl.train_captured) {
        cudaGraphExecDestroy(pufferl.train_cudagraph);
    }
    if (pufferl.rollout_captured) {
        for (int i = 0; i < pufferl.hypers.horizon * pufferl.hypers.num_buffers; i++) {
            cudaGraphExecDestroy(pufferl.fused_rollout_cudagraphs[i]);
        }
    }

    policy_weights_free(&pufferl.policy, &pufferl.weights);
    policy_activations_free(&pufferl.policy, pufferl.train_activations);
    if (pufferl.has_parent_policy) {
        policy_weights_free(&pufferl.policy, &pufferl.parent_weights);
        policy_activations_free(&pufferl.policy, pufferl.parent_train_activations);
    }
    for (int buf = 0; buf < pufferl.hypers.num_buffers; buf++) {
        policy_activations_free(&pufferl.policy, pufferl.buffer_activations[buf]);
    }

    for (int i = 0; i < pufferl.hypers.num_buffers; i++) {
        cudaFree(pufferl.rng_states[i]);
    }
    free(pufferl.rng_states);
    if (pufferl.live_phase2_hidden_host) {
        for (int i = 0; i < pufferl.hypers.num_buffers; i++) {
            if (pufferl.live_phase2_hidden_host[i])
                cudaFreeHost(pufferl.live_phase2_hidden_host[i]);
        }
        free(pufferl.live_phase2_hidden_host);
    }
    if (pufferl.phase2_first_forward_host) {
        for (int i = 0; i < pufferl.hypers.num_buffers; i++) {
            if (pufferl.phase2_first_forward_host[i])
                cudaFreeHost(pufferl.phase2_first_forward_host[i]);
        }
        free(pufferl.phase2_first_forward_host);
    }
    if (pufferl.phase2_first_forward_obs_host) {
        for (int i = 0; i < pufferl.hypers.num_buffers; i++) {
            if (pufferl.phase2_first_forward_obs_host[i])
                cudaFreeHost(pufferl.phase2_first_forward_obs_host[i]);
        }
        free(pufferl.phase2_first_forward_obs_host);
    }
    if (pufferl.phase2_hidden_restore_host) {
        for (int i = 0; i < pufferl.hypers.num_buffers; i++) {
            if (pufferl.phase2_hidden_restore_host[i])
                cudaFreeHost(pufferl.phase2_hidden_restore_host[i]);
        }
        free(pufferl.phase2_hidden_restore_host);
    }

    if (USE_BF16) {
        cudaFree(pufferl.master_weights.data);
    }
    if (pufferl.anchor_weights.data) {
        cudaFree(pufferl.anchor_weights.data);
        pufferl.anchor_weights.data = NULL;
    }
    if (pufferl.curriculum_enabled) {
        close_state_buffer(&pufferl.state_buf);
    }

    alloc_free(&pufferl.params_alloc);
    alloc_free(&pufferl.grads_alloc);
    alloc_free(&pufferl.activations_alloc);
    alloc_free(&pufferl.parent_params_alloc);
    alloc_free(&pufferl.parent_activations_alloc);
    alloc_free(&pufferl.parent_grads_alloc);

    for (int i = 0; i < pufferl.hypers.num_buffers; i++) {
        cudaStreamDestroy(pufferl.streams[i]);
    }
    for (int i = 0; i < NUM_TRAIN_EVENTS; i++) {
        cudaEventDestroy(pufferl.profile.events[i]);
    }
    nvmlShutdown();

    static_vec_close(pufferl.vec);

    free(pufferl.buffer_states);
    free(pufferl.buffer_activations);
    free(pufferl.fused_rollout_cudagraphs);
    free(pufferl.streams);

    if (pufferl.nccl_comm != nullptr) {
        ncclCommDestroy(pufferl.nccl_comm);
    }
}
