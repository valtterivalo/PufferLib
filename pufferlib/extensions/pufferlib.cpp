/* Checklist for avoiding diabolical capture bugs:
 * 1. Don't start separate streams before tracing (i.e. env gpu buffers)
 * 2. Make sure input/output buffer pointers don't change
 * 3. Make sure to restore the original stream after tracing
 * 4. All custom kernels need to use the default torch stream
 * 5. Make sure you are using the torch stream fns, not the c10 ones.
 * 6. Scalars get captured by value. They cannot change between calls.
 */

#include <torch/extension.h>
#include <torch/torch.h>

#ifdef WITH_CUDA
#include <c10/cuda/CUDACachingAllocator.h>
#include <cuda_runtime.h>
#include <cuda_profiler_api.h>
#include <nccl.h>
#include <ATen/cuda/CUDAGraph.h>
#include <ATen/cuda/CUDAGeneratorImpl.h>
#include <ATen/cuda/CUDAContext.h>
#include <nvtx3/nvToolsExt.h>
#include <nvml.h>
#endif

#include <unistd.h>
#include <vector>
#include <chrono>

#include "muon.h"
#include "env_binding.h"
#include "modules.h"

namespace pufferlib {

#include "models.cpp"
#include "advantage.cpp"
#include "ocean.cpp"

torch::Dtype to_torch_dtype(int dtype) {
    if (dtype == FLOAT) {
        return torch::kFloat32;
    } else if (dtype == INT) {
        return torch::kInt32;
    } else if (dtype == UNSIGNED_CHAR) {
        return torch::kUInt8;
    } else if (dtype == DOUBLE) {
        return torch::kFloat64;
    } else if (dtype == CHAR) {
        return torch::kInt8;
    } else {
        assert(false && "to_torch_dtype failed to convert dtype");
    }
    return torch::kFloat32;
}

typedef struct {
    Tensor obs;
    Tensor actions;
    Tensor rewards;
    Tensor terminals;
} EnvBuf;

tuple<StaticVec*, Tensor> create_environments(int num_buffers, int total_agents,
        const std::string& env_name, Dict* vec_kwargs, Dict* env_kwargs, EnvBuf& env) {
    StaticVec* vec = create_static_vec(total_agents, num_buffers, vec_kwargs, env_kwargs);

    int obs_size = get_obs_size();
    int num_atns = get_num_atns();

#ifdef WITH_CUDA
    // GPU path: wrap CUDA-mapped buffers
    auto obs_dev_t = torch::dtype(to_torch_dtype(get_obs_type())).device(g_device);
    env.obs = torch::from_blob(vec->gpu_observations, {total_agents, obs_size}, obs_dev_t);
    env.actions = torch::from_blob(vec->gpu_actions, {total_agents, num_atns}, dev_action);
    env.rewards = torch::from_blob(vec->gpu_rewards, {total_agents}, dev_f32);
    env.terminals = torch::from_blob(vec->gpu_terminals, {total_agents}, dev_f32);
#else
    // CPU/MPS path: wrap host buffers directly (from_blob gives a live view
    // into the C env's memory, updated in-place each step). Stays on CPU;
    // data is moved to device inside net_callback_wrapper via copy_.
    auto obs_cpu_t = torch::dtype(to_torch_dtype(get_obs_type()));
    env.obs = torch::from_blob(vec->observations, {total_agents, obs_size}, obs_cpu_t);
    env.actions = torch::from_blob(vec->actions, {total_agents, num_atns}, torch::kFloat64);
    env.rewards = torch::from_blob(vec->rewards, {total_agents}, torch::kFloat32);
    env.terminals = torch::from_blob(vec->terminals, {total_agents}, torch::kFloat32);
#endif

    Tensor act_sizes = torch::from_blob(get_act_sizes(), {num_atns}, torch::dtype(torch::kInt32)).clone();

    return std::make_tuple(vec, act_sizes);
}

typedef struct {
    Tensor mb_obs;
    Tensor mb_state;
    Tensor mb_actions;
    Tensor mb_logprobs;
    Tensor mb_advantages;
    Tensor mb_prio;
    Tensor mb_values;
    Tensor mb_returns;
    Tensor mb_ratio;
    Tensor mb_newvalue;
} TrainGraph;

TrainGraph create_train_graph(int mb_segments, int horizon, int input_size,
        int hidden_size, int num_atns, int num_layers) {
    return {
        .mb_obs = torch::zeros({mb_segments, horizon, input_size}, dev_t),
        .mb_state = torch::zeros({num_layers, mb_segments, 1, hidden_size}, dev_t),
        .mb_actions = torch::zeros({mb_segments, horizon, num_atns}, dev_action),
        .mb_logprobs = torch::zeros({mb_segments, horizon}, dev_t),
        .mb_advantages = torch::zeros({mb_segments, horizon}, dev_f32),  // always fp32 for precision
        .mb_prio = torch::zeros({mb_segments, 1}, dev_t),
        .mb_values = torch::zeros({mb_segments, horizon}, dev_t),
        .mb_returns = torch::zeros({mb_segments, horizon}, dev_t),
        .mb_ratio = torch::zeros({mb_segments, horizon}, dev_t),
        .mb_newvalue = torch::zeros({mb_segments, horizon, 1}, dev_t),
    };
}

typedef struct {
    Tensor observations;
    Tensor actions;
    Tensor values;
    Tensor logprobs;
    Tensor rewards;
    Tensor terminals;
    Tensor ratio;
    Tensor importance;
} RolloutBuf;

RolloutBuf create_rollouts(int horizon, int segments, int input_size, int num_atns) {
    return {
        .observations = torch::zeros({horizon, segments, input_size}, dev_t),
        .actions = torch::zeros({horizon, segments, num_atns}, dev_action),
        .values = torch::zeros({horizon, segments}, dev_t),
        .logprobs = torch::zeros({horizon, segments}, dev_t),
        .rewards = torch::zeros({horizon, segments}, dev_t),
        .terminals = torch::zeros({horizon, segments}, dev_t),
        .ratio = torch::zeros({horizon, segments}, dev_t),
        .importance = torch::zeros({horizon, segments}, dev_t),
    };
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
    // GAE
    float gamma;
    float gae_lambda;
    // VTrace
    float vtrace_rho_clip;
    float vtrace_c_clip;
    // Priority
    float prio_alpha;
    float prio_beta0;
    // Flags
    bool use_rnn;
    int cudagraphs;  // epoch at which to capture graph, -1 to disable
    bool kernels;
    bool profile;
    // Multi-GPU
    int rank;
    int world_size;
    std::string nccl_id_path;
    // Threading
    int num_threads;
    // Device
    std::string device;  // "cuda", "mps", or "cpu"
} HypersT;

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

#define NUM_TRAIN_EVENTS 5  // preloop start/end, loop misc start, forward start/end
typedef struct {
#ifdef WITH_CUDA
    cudaEvent_t events[NUM_TRAIN_EVENTS];
#else
    std::chrono::high_resolution_clock::time_point events[NUM_TRAIN_EVENTS];
#endif
    float accum[NUM_PROF];
} ProfileT;

typedef struct {
    Policy* policy_bf16;  // Working weights (bf16) - used for forward/backward
    Policy* policy_fp32;  // Master weights (fp32) - used for optimizer
    StaticVec* vec;
    Muon* muon;
    torch::Device device = torch::kCPU;  // Runtime device (CUDA, MPS, or CPU)
#ifdef WITH_CUDA
    ncclComm_t nccl_comm;  // NCCL communicator for multi-GPU
#endif
    HypersT hypers;
    bool is_continuous;  // True if all action dimensions are continuous (size==1)
    vector<Tensor> buffer_states;  // Per-buffer states for contiguous access
    RolloutBuf rollouts;
    EnvBuf env;
    TrainGraph train_buf;
#ifdef WITH_CUDA
    vector<vector<at::cuda::CUDAGraph>> fused_rollout_cudagraphs;  // [horizon][num_buffers]
    at::cuda::CUDAGraph train_cudagraph;
    at::cuda::MempoolId_t train_pool_id;     // Pool ID for releasing graph memory
    at::cuda::MempoolId_t rollout_pool_id;   // Pool ID for releasing graph memory
    vector<at::cuda::CUDAStream> torch_streams;  // PyTorch-managed streams for OMP
    bool rollout_captured;
    bool train_captured;
    Tensor rng_offset;  // CUDA tensor so increment is graphable
    nvmlDevice_t nvml_device;
#endif
    Tensor act_sizes;      // Device int32 tensor of action head sizes for MultiDiscrete
    Tensor act_sizes_cpu;  // CPU int64 tensor (pre-computed to avoid alloc during graph replay)
    std::vector<int64_t> act_sizes_vec;  // Backup copy for corruption detection
    Tensor losses;         // (NUM_LOSSES,) float32 accumulator for loss components
    ProfileT profile;
    int epoch;
    int train_warmup;
    uint64_t rng_seed;
    // Selfplay: frozen opponent policy running inside the rollout loop
    std::string env_name;
    int input_size;
    int act_n;
    Policy* selfplay_policy = nullptr;
    float* selfplay_obs_raw = nullptr;
    unsigned char* selfplay_mask_raw = nullptr;
    Tensor selfplay_obs;    // (N, obs_size) wraps selfplay_obs_raw
    Tensor selfplay_state;  // (num_layers, N, hidden) frozen policy RNN state (inference shape)
    bool selfplay_enabled = false;
} PuffeRL;

Dict* log_environments_impl(PuffeRL& pufferl) {
    Dict* out = create_dict(32);
    static_vec_log(pufferl.vec, out);
    return out;
}

// ============================================================================
// Rollout and train section functions
// ============================================================================

//TODO: Profile without sync
inline void profile_begin(const char* tag, bool enable) {
#ifdef WITH_CUDA
    if (enable) { cudaDeviceSynchronize(); nvtxRangePushA(tag); }
#else
    (void)tag; (void)enable;
#endif
}

inline void profile_end(bool enable) {
#ifdef WITH_CUDA
    if (enable) { cudaDeviceSynchronize(); nvtxRangePop(); }
#else
    (void)enable;
#endif
}

// Thread initialization callback - sets CUDA stream once per thread
extern "C" void thread_init_wrapper(void* ctx, int buf) {
#ifdef WITH_CUDA
    PuffeRL* pufferl = (PuffeRL*)ctx;
    at::cuda::setCurrentCUDAStream(pufferl->torch_streams[buf]);
#else
    (void)ctx; (void)buf;  // no streams on CPU/MPS
#endif
}

// Selfplay: run frozen policy on P1 observations, write int actions to C envs.
// Called after main policy actions are written, before c_step reads them.
//
// selfplay_state lives on CPU to avoid MPS fallback issues with index_select/
// index_copy_ (aten::index_copy.out has no MPS kernel — the automatic CPU
// fallback corrupts tensor state). Only the forward pass runs on-device.
void selfplay_step(PuffeRL* pufferl, int start, int block_size) {
    if (!pufferl->selfplay_enabled || !pufferl->selfplay_policy) return;

    unsigned char* mask = pufferl->selfplay_mask_raw;
    int num_atns = pufferl->hypers.num_atns;

    // Collect local indices of selfplay envs in this block
    std::vector<long> sp_local;
    for (int i = 0; i < block_size; i++) {
        if (mask[start + i]) sp_local.push_back(i);
    }
    if (sp_local.empty()) return;

    int sp_count = (int)sp_local.size();
    Tensor sp_idx = torch::from_blob(sp_local.data(), {sp_count}, torch::kLong).clone();
    Tensor sp_idx_global = sp_idx + start;  // stays on CPU (selfplay_state is CPU)

    // Gather P1 obs for selfplay envs (obs is on CPU, move to device for forward)
    Tensor obs_block = pufferl->selfplay_obs.narrow(0, start, block_size);
    Tensor sp_obs_cpu = obs_block.index_select(0, sp_idx);
    // Extract action mask from selfplay obs (last act_n floats, stays on CPU for sampling)
    int act_n = pufferl->act_n;
    Tensor sp_mask = sp_obs_cpu.narrow(1, sp_obs_cpu.size(1) - act_n, act_n);
    Tensor sp_obs = sp_obs_cpu.to(pufferl->device);

    // Gather RNN states on CPU, move to device for forward pass
    Tensor sp_state = pufferl->selfplay_state.index_select(1, sp_idx_global)
                          .to(pufferl->device);

    // Forward pass through frozen policy (on device)
    auto [logits, value, state_out] = pufferl->selfplay_policy->forward(sp_obs, sp_state);

    // Scatter RNN states back (move to CPU for index_copy_)
    pufferl->selfplay_state.index_copy_(1, sp_idx_global, state_out.to(torch::kCPU));

    // Sample actions per head (categorical) — on CPU to avoid MPS multinomial issues
    // Apply action mask before softmax to enforce valid actions
    Tensor logits_cpu = logits.mean.to(torch::kCPU);
    auto split = logits_cpu.split(
        c10::IntArrayRef(pufferl->act_sizes_vec.data(), num_atns), 1);
    std::vector<Tensor> act_vecs;
    int mask_offset = 0;
    for (int h = 0; h < num_atns; h++) {
        Tensor head_logits = split[h];
        Tensor head_mask = sp_mask.narrow(1, mask_offset, pufferl->act_sizes_vec[h]);
        head_logits = head_logits.masked_fill(head_mask < 0.5f, -1e9f);
        Tensor probs = head_logits.softmax(-1);
        act_vecs.push_back(at::multinomial(probs, 1, true));
        mask_offset += pufferl->act_sizes_vec[h];
    }
    Tensor sp_actions = torch::cat(act_vecs, 1).to(torch::kInt32);

    // Write to C envs via extern C function (Env type not visible in C++)
    int32_t* acts_ptr = sp_actions.data_ptr<int32_t>();
    for (int j = 0; j < sp_count; j++) {
        int env_idx = start + (int)sp_local[j];
        osrs_pvp_set_env_opponent_actions(pufferl->vec, env_idx,
            acts_ptr + j * num_atns);
    }
}

// Called by vecenv per buffer thread
extern "C" void net_callback_wrapper(void* ctx, int buf, int t) {
    torch::NoGradGuard no_grad;
    PuffeRL* pufferl = (PuffeRL*)ctx;
    HypersT& hypers = pufferl->hypers;
    profile_begin("fused_rollout", hypers.profile);

#ifdef WITH_CUDA
    if (pufferl->rollout_captured) {
        pufferl->fused_rollout_cudagraphs[t][buf].replay();
        profile_end(hypers.profile);
        return;
    }

    bool capturing = pufferl->epoch == hypers.cudagraphs;
    auto saved_stream = at::cuda::getCurrentCUDAStream();
    auto cap_stream = capturing ? at::cuda::getStreamFromPool() : saved_stream;
    if (capturing) {
        at::cuda::setCurrentCUDAStream(cap_stream);
        pufferl->fused_rollout_cudagraphs[t][buf].capture_begin(pufferl->rollout_pool_id);
    }
#endif

    RolloutBuf& rollouts = pufferl->rollouts;
    EnvBuf& env = pufferl->env;
    int block_size = pufferl->vec->total_agents / hypers.num_buffers;
    int start = buf * block_size;

    // Copy env data to rollout buffer.
    // CUDA uses non_blocking=true because env data lives in separate gpu_* buffers
    // that c_step doesn't touch. On MPS/CPU, env buffers ARE the copy source, so
    // we must block to prevent c_step from overwriting data before the copy completes.
#ifdef WITH_CUDA
    constexpr bool nb = true;
#else
    constexpr bool nb = false;
#endif
    Tensor obs = env.obs.narrow(0, start, block_size);
    rollouts.observations.select(0, t).narrow(0, start, block_size).copy_(obs, nb);
    Tensor rewards = env.rewards.narrow(0, start, block_size);
    rollouts.rewards.select(0, t).narrow(0, start, block_size).copy_(rewards, nb);
    Tensor terminals = env.terminals.narrow(0, start, block_size);
    rollouts.terminals.select(0, t).narrow(0, start, block_size).copy_(terminals, nb);

    // Forward pass (use rollout copy which is on-device for MPS/CUDA)
    Tensor obs_dev = rollouts.observations.select(0, t).narrow(0, start, block_size);
    Tensor& state = pufferl->buffer_states[buf];

    // Zero RNN state for envs that just terminated (episode boundary).
    // Each episode is independent — carrying state across episodes corrupts
    // the temporal context the RNN needs for value prediction.
    // terminals is (block_size,) with 1.0 for finished episodes.
    // state is (num_layers, block_size, hidden). Multiply by (1 - terminal)
    // to zero terminated rows while preserving ongoing episode state.
    if (hypers.use_rnn) {
        Tensor keep = (1.0f - terminals.to(state.device())).unsqueeze(0).unsqueeze(-1);
        state.mul_(keep);
    }

    auto [logits, value, state_out] = pufferl->policy_bf16->forward(obs_dev, state);
    state.copy_(state_out, false);

    // Sample actions, logprobs, values into rollout buffer
    Tensor actions = rollouts.actions.select(0, t).narrow(0, start, block_size);
    Tensor logprobs = rollouts.logprobs.select(0, t).narrow(0, start, block_size);
    Tensor values = rollouts.values.select(0, t).narrow(0, start, block_size);

    // Extract action mask from observations (last act_n floats, zero-copy view)
    int act_n = pufferl->act_n;
    Tensor mask_dev = obs_dev.narrow(1, obs_dev.size(1) - act_n, act_n);

#ifdef WITH_CUDA
    sample_actions(logits, value, actions, logprobs, values,
        pufferl->act_sizes, pufferl->act_sizes_vec.data(),
        pufferl->is_continuous, hypers.kernels, pufferl->rng_seed, pufferl->rng_offset,
        mask_dev);
#elif defined(WITH_METAL)
    if (hypers.kernels) {
        sample_logits_metal(logits.mean, logits.logstd, value,
            actions, logprobs, values,
            pufferl->act_sizes, pufferl->rng_seed, Tensor(),
            mask_dev);
    } else {
        Logits cpu_logits;
        cpu_logits.mean = logits.mean.to(torch::kCPU);
        if (logits.logstd.defined())
            cpu_logits.logstd = logits.logstd.to(torch::kCPU);
        Tensor cpu_value = value.to(torch::kCPU);
        Tensor mask_cpu = mask_dev.to(torch::kCPU);
        sample_actions(cpu_logits, cpu_value, actions, logprobs, values,
            pufferl->act_sizes, pufferl->act_sizes_vec.data(),
            pufferl->is_continuous, false, pufferl->rng_seed, Tensor(),
            mask_cpu);
    }
#else
    {
        Logits cpu_logits;
        cpu_logits.mean = logits.mean.to(torch::kCPU);
        if (logits.logstd.defined())
            cpu_logits.logstd = logits.logstd.to(torch::kCPU);
        Tensor cpu_value = value.to(torch::kCPU);
        sample_actions(cpu_logits, cpu_value, actions, logprobs, values,
            pufferl->act_sizes, pufferl->act_sizes_vec.data(),
            pufferl->is_continuous, false, pufferl->rng_seed, Tensor(),
            mask_dev);
    }
#endif

    // Copy actions to env
#ifdef WITH_CUDA
    // CUDA: direct device-to-device, non-blocking (env.actions wraps gpu_actions buffer)
    env.actions.narrow(0, start, block_size).copy_(actions, true);
#else
    // MPS/CPU: actions already on CPU from sample_actions above.
    env.actions.narrow(0, start, block_size).copy_(actions.to(torch::kCPU), false);

    // Selfplay: run frozen policy on P1 obs, write actions before c_step
    selfplay_step(pufferl, start, block_size);
#endif

#ifdef WITH_CUDA
    if (capturing) {
        pufferl->fused_rollout_cudagraphs[t][buf].capture_end();
        cap_stream.synchronize();
        cudaDeviceSynchronize();
        at::cuda::setCurrentCUDAStream(saved_stream);
    }
#endif
    profile_end(hypers.profile);
}

void rollouts_impl(PuffeRL& pufferl) {
    torch::NoGradGuard no_grad;
    HypersT& hypers = pufferl.hypers;

    int horizon = hypers.horizon;
    int num_buffers = hypers.num_buffers;

    for (int i = 0; i < num_buffers*horizon; ++i) {
        int buf = i % num_buffers;
        int h = i / num_buffers;

        net_callback_wrapper(&pufferl, buf, h);
#ifdef WITH_CUDA
        cudaDeviceSynchronize();
#endif
    }
}

void train_impl(PuffeRL& pufferl) {
    HypersT& hypers = pufferl.hypers;

    // Buffers are stored as {horizon, segments, ...} for contiguous rollout writes
    // Transpose to {segments, horizon, ...} for train logic
    // Need .contiguous() because puff_advantage uses raw data pointers
#ifdef WITH_CUDA
    cudaEventRecord(pufferl.profile.events[0]);  // pre-loop start
#else
    pufferl.profile.events[0] = std::chrono::high_resolution_clock::now();
#endif
    RolloutBuf rollouts;
    rollouts.observations = pufferl.rollouts.observations.permute({1, 0, 2}).contiguous();
    rollouts.actions = pufferl.rollouts.actions.transpose(0, 1).contiguous();
    rollouts.logprobs = pufferl.rollouts.logprobs.transpose(0, 1).contiguous();
    rollouts.rewards = pufferl.rollouts.rewards.transpose(0, 1).contiguous();
    rollouts.terminals = pufferl.rollouts.terminals.transpose(0, 1).contiguous();
    rollouts.ratio = pufferl.rollouts.ratio.transpose(0, 1).contiguous();
    rollouts.values = pufferl.rollouts.values.transpose(0, 1).contiguous();
    Tensor old_values = rollouts.values.clone();

    rollouts.rewards.clamp_(-1.0, 1.0);  // Clamp rewards here instead of in eval to save a kernel call per step

    int minibatch_size = hypers.minibatch_size;
    int batch_size = hypers.total_agents * hypers.horizon;
    int minibatch_segments = minibatch_size / hypers.horizon;
    float prio_beta0 = hypers.prio_beta0;
    float prio_alpha = hypers.prio_alpha;
    bool anneal_lr = hypers.anneal_lr;
    int current_epoch = pufferl.epoch;

    Policy* policy_bf16 = pufferl.policy_bf16;
    Muon* muon = pufferl.muon;

    int total_epochs = hypers.total_timesteps / batch_size;
    if (total_epochs < 1) total_epochs = 1;

    if (anneal_lr) {
        float lr_min = hypers.min_lr_ratio * hypers.lr;
        float lr = cosine_annealing(hypers.lr, lr_min, current_epoch, total_epochs);
        muon->lr.fill_(lr);
    }

    // Annealed priority exponent
    float anneal_beta = prio_beta0 + (1.0f - prio_beta0) * prio_alpha * (float)current_epoch/(float)total_epochs;

    // Zero out ratio at start of epoch (matches Python: self.ratio[:] = 1)
    rollouts.ratio.fill_(1.0);

    Tensor advantages = torch::zeros_like(rollouts.values, torch::kFloat32);  // fp32 precision
    int total_minibatches = hypers.replay_ratio * batch_size / hypers.minibatch_size;

    TrainGraph& graph = pufferl.train_buf;
#ifdef WITH_CUDA
    cudaEventRecord(pufferl.profile.events[1]);  // pre-loop end
#else
    pufferl.profile.events[1] = std::chrono::high_resolution_clock::now();
#endif

    for (int mb = 0; mb < total_minibatches; ++mb) {
#ifdef WITH_CUDA
        cudaEventRecord(pufferl.profile.events[2]);  // start of misc (overwritten each iter)
#else
        pufferl.profile.events[2] = std::chrono::high_resolution_clock::now();
#endif
        advantages.fill_(0.0);

        profile_begin("compute_advantage", hypers.profile);
#ifdef WITH_CUDA
        puff_advantage_cuda(rollouts.values, rollouts.rewards, rollouts.terminals,
            rollouts.ratio, advantages, hypers.gamma, hypers.gae_lambda,
            hypers.vtrace_rho_clip, hypers.vtrace_c_clip);
#elif defined(WITH_METAL)
        if (hypers.kernels) {
            pufferlib::puff_advantage_metal(rollouts.values, rollouts.rewards, rollouts.terminals,
                rollouts.ratio, advantages, hypers.gamma, hypers.gae_lambda,
                hypers.vtrace_rho_clip, hypers.vtrace_c_clip);
        } else {
            auto cpu_vals = rollouts.values.to(torch::kCPU).to(torch::kFloat32).contiguous();
            auto cpu_rew  = rollouts.rewards.to(torch::kCPU).to(torch::kFloat32).contiguous();
            auto cpu_term = rollouts.terminals.to(torch::kCPU).to(torch::kFloat32).contiguous();
            auto cpu_ratio = rollouts.ratio.to(torch::kCPU).to(torch::kFloat32).contiguous();
            auto cpu_adv  = advantages.to(torch::kCPU).contiguous();
            puff_advantage_cpu(cpu_vals, cpu_rew, cpu_term,
                cpu_ratio, cpu_adv, hypers.gamma, hypers.gae_lambda,
                hypers.vtrace_rho_clip, hypers.vtrace_c_clip);
            advantages.copy_(cpu_adv.to(advantages.device()));
        }
#else
        // CPU advantage requires CPU tensors — move from MPS/device if needed
        {
            auto cpu_vals = rollouts.values.to(torch::kCPU).to(torch::kFloat32).contiguous();
            auto cpu_rew  = rollouts.rewards.to(torch::kCPU).to(torch::kFloat32).contiguous();
            auto cpu_term = rollouts.terminals.to(torch::kCPU).to(torch::kFloat32).contiguous();
            auto cpu_ratio = rollouts.ratio.to(torch::kCPU).to(torch::kFloat32).contiguous();
            auto cpu_adv  = advantages.to(torch::kCPU).contiguous();
            puff_advantage_cpu(cpu_vals, cpu_rew, cpu_term,
                cpu_ratio, cpu_adv, hypers.gamma, hypers.gae_lambda,
                hypers.vtrace_rho_clip, hypers.vtrace_c_clip);
            advantages.copy_(cpu_adv.to(advantages.device()));
        }
#endif
        profile_end(hypers.profile);

        profile_begin("compute_prio", hypers.profile);
#ifdef WITH_CUDA
        auto prio_fn = hypers.kernels ? prio_replay_cuda : prio_replay_cpp;
#elif defined(WITH_METAL)
        auto prio_fn = hypers.kernels ? prio_replay_metal : prio_replay_cpp;
#else
        auto prio_fn = prio_replay_cpp;
#endif
        auto [idx, mb_prio] = prio_fn(advantages, prio_alpha, minibatch_segments,
            hypers.total_agents, anneal_beta);
        profile_end(hypers.profile);

        profile_begin("train_select_and_copy", hypers.profile);
#ifdef WITH_CUDA
        auto copy_fn = hypers.kernels ? train_select_and_copy_cuda : train_select_and_copy_cpp;
#elif defined(WITH_METAL)
        auto copy_fn = hypers.kernels ? train_select_and_copy_metal : train_select_and_copy_cpp;
#else
        auto copy_fn = train_select_and_copy_cpp;
#endif
        copy_fn(rollouts.observations, rollouts.actions, rollouts.logprobs,
            old_values, advantages, idx, mb_prio,
            graph.mb_obs, graph.mb_state, graph.mb_actions,
            graph.mb_logprobs, graph.mb_advantages, graph.mb_prio,
            graph.mb_values, graph.mb_returns);
        profile_end(hypers.profile);

#ifdef WITH_CUDA
        cudaEventRecord(pufferl.profile.events[3]);  // end misc / start forward
        if (pufferl.train_captured) {
            pufferl.train_cudagraph.replay();
        } else {
            bool capturing = pufferl.train_warmup == hypers.cudagraphs;
            auto saved_stream = at::cuda::getCurrentCUDAStream();
            auto cap_stream = capturing ? at::cuda::getStreamFromPool() : saved_stream;
            if (capturing) {
                at::cuda::setCurrentCUDAStream(cap_stream);
                pufferl.train_cudagraph.capture_begin(pufferl.train_pool_id);
            }
#else
        pufferl.profile.events[3] = std::chrono::high_resolution_clock::now();
        {
#endif
            auto [logits, newvalue] = pufferl.policy_bf16->forward_train(graph.mb_obs, graph.mb_state);
            Tensor newvalue_out = graph.mb_newvalue.view({graph.mb_ratio.size(0), graph.mb_ratio.size(1)});

            // Extract action mask from minibatch obs (last act_n floats)
            int act_n = pufferl.act_n;
            Tensor mb_mask = graph.mb_obs.narrow(-1, graph.mb_obs.size(-1) - act_n, act_n);
            Tensor mask_flat = mb_mask.reshape({graph.mb_obs.size(0) * graph.mb_obs.size(1), -1});

            // TODO: Try using global (epoch-level) adv mean/std instead of per-minibatch
#ifdef WITH_CUDA
            Tensor loss = (hypers.kernels
                ? PPOLoss::apply(logits.mean, logits.logstd, newvalue,
                    graph.mb_actions, graph.mb_logprobs, graph.mb_advantages, graph.mb_prio,
                    graph.mb_values, graph.mb_returns, graph.mb_ratio, newvalue_out,
                    pufferl.act_sizes, pufferl.losses,
                    hypers.clip_coef, hypers.vf_clip_coef, hypers.vf_coef, hypers.ent_coef)
                : fused_ppo_loss_cpp(logits.mean, logits.logstd, newvalue,
                    graph.mb_actions, graph.mb_logprobs, graph.mb_advantages, graph.mb_prio,
                    graph.mb_values, graph.mb_returns, graph.mb_ratio, newvalue_out,
                    pufferl.act_sizes, pufferl.losses,
                    hypers.clip_coef, hypers.vf_clip_coef, hypers.vf_coef, hypers.ent_coef,
                    mask_flat)
            )[0];
#elif defined(WITH_METAL)
            // Always use CPP loss path on Metal — PPOLossMetal doesn't support
            // action masks, which would cause a ratio mismatch with masked sampling.
            Tensor loss = fused_ppo_loss_cpp(logits.mean, logits.logstd, newvalue,
                    graph.mb_actions, graph.mb_logprobs, graph.mb_advantages, graph.mb_prio,
                    graph.mb_values, graph.mb_returns, graph.mb_ratio, newvalue_out,
                    pufferl.act_sizes, pufferl.losses,
                    hypers.clip_coef, hypers.vf_clip_coef, hypers.vf_coef, hypers.ent_coef,
                    mask_flat)[0];
#else
            Tensor loss = fused_ppo_loss_cpp(logits.mean, logits.logstd, newvalue,
                    graph.mb_actions, graph.mb_logprobs, graph.mb_advantages, graph.mb_prio,
                    graph.mb_values, graph.mb_returns, graph.mb_ratio, newvalue_out,
                    pufferl.act_sizes, pufferl.losses,
                    hypers.clip_coef, hypers.vf_clip_coef, hypers.vf_coef, hypers.ent_coef,
                    mask_flat)[0];
#endif

            loss.backward();

            if (USE_BF16) {
                copy_gradients_to_fp32(pufferl.policy_bf16, pufferl.policy_fp32);
            }
            clip_grad_norm_(pufferl.policy_fp32->parameters(), hypers.max_grad_norm);
            pufferl.muon->step();
            pufferl.muon->zero_grad();
            if (USE_BF16) {
                pufferl.policy_bf16->zero_grad();
                sync_policy_weights(pufferl.policy_bf16, pufferl.policy_fp32);
            }

#ifdef WITH_CUDA
            if (capturing) {
                pufferl.train_cudagraph.capture_end();
                cap_stream.synchronize();
                cudaDeviceSynchronize();
                at::cuda::setCurrentCUDAStream(saved_stream);
                pufferl.train_captured = true;
            }
#endif
            pufferl.train_warmup++;
        }

        Tensor new_ratio = graph.mb_ratio.detach().squeeze(-1).to(PRECISION_DTYPE);
        rollouts.ratio.index_copy_(0, idx, new_ratio);
        Tensor new_value = graph.mb_newvalue.detach().squeeze(-1).to(PRECISION_DTYPE);
        rollouts.values.index_copy_(0, idx, new_value);
#ifdef WITH_CUDA
        cudaEventRecord(pufferl.profile.events[4]);  // end forward
#else
        pufferl.profile.events[4] = std::chrono::high_resolution_clock::now();
#endif
    }
    pufferl.epoch += 1;

#ifdef WITH_CUDA
    cudaStreamSynchronize(at::cuda::getCurrentCUDAStream());
#endif

    if (total_minibatches > 0) {
        float ms;
#ifdef WITH_CUDA
        // Pre-loop setup (transpose, advantage, allocs)
        cudaEventElapsedTime(&ms, pufferl.profile.events[0], pufferl.profile.events[1]);
        pufferl.profile.accum[PROF_TRAIN_MISC] += ms;
        // In-loop misc (last iteration, representative) scaled by count
        cudaEventElapsedTime(&ms, pufferl.profile.events[2], pufferl.profile.events[3]);
        pufferl.profile.accum[PROF_TRAIN_MISC] += ms * total_minibatches;
        // In-loop forward (last iteration, representative) scaled by count
        cudaEventElapsedTime(&ms, pufferl.profile.events[3], pufferl.profile.events[4]);
        pufferl.profile.accum[PROF_TRAIN_FORWARD] += ms * total_minibatches;
#else
        // CPU/MPS timing via chrono (milliseconds)
        auto dur01 = std::chrono::duration<float, std::milli>(pufferl.profile.events[1] - pufferl.profile.events[0]);
        pufferl.profile.accum[PROF_TRAIN_MISC] += dur01.count();
        auto dur23 = std::chrono::duration<float, std::milli>(pufferl.profile.events[3] - pufferl.profile.events[2]);
        pufferl.profile.accum[PROF_TRAIN_MISC] += dur23.count() * total_minibatches;
        auto dur34 = std::chrono::duration<float, std::milli>(pufferl.profile.events[4] - pufferl.profile.events[3]);
        pufferl.profile.accum[PROF_TRAIN_FORWARD] += dur34.count() * total_minibatches;
#endif
    }
}

std::unique_ptr<pufferlib::PuffeRL> create_pufferl_impl(HypersT& hypers, const std::string& env_name, Dict* vec_kwargs, Dict* env_kwargs) {
    auto pufferl = std::make_unique<pufferlib::PuffeRL>();
    pufferl->hypers = hypers;

    // Determine runtime device from config
    std::string device_str = hypers.device;
#ifdef WITH_CUDA
    if (device_str == "cuda" || device_str.substr(0, 5) == "cuda:") {
        g_device = torch::kCUDA;
        pufferl->device = torch::kCUDA;
    } else {
        g_device = torch::kCPU;
        pufferl->device = torch::kCPU;
    }
    pufferl->nccl_comm = nullptr;
#else
    // MPS (Apple Silicon): supported with these constraints:
    // - fp32 only (bf16 causes NaN with Muon optimizer, controlled by PRECISION_FLOAT)
    // - float64 not available on MPS (action tensors use float32, see init_device_tensor_options)
    // - env buffers stay on CPU host memory; explicit copies to/from MPS each step
    // - advantage computation round-trips through CPU (puff_advantage_cpu uses raw pointers)
    // - no CUDA graphs, no custom kernels (cpp fallbacks used throughout)
    // - no GPU utilization metric (VRAM reported via Python-side torch.mps calls)
    if (device_str == "mps" && at::hasMPS()) {
        g_device = torch::Device(torch::kMPS);
        pufferl->device = torch::Device(torch::kMPS);
#ifdef WITH_METAL
        metal_init();
#endif
    } else {
        g_device = torch::kCPU;
        pufferl->device = torch::kCPU;
    }
#endif
    printf("Using %s device\n", pufferl->device.str().c_str());
    init_device_tensor_options();

#ifdef WITH_CUDA
    // Multi-GPU: initialize NCCL (device already set by Python)
    if (hypers.world_size > 1) {
        ncclUniqueId nccl_id;
        if (hypers.rank == 0) {
            ncclGetUniqueId(&nccl_id);
            FILE* f = fopen(hypers.nccl_id_path.c_str(), "wb");
            fwrite(&nccl_id, sizeof(nccl_id), 1, f);
            fclose(f);
        }
        // Wait for rank 0 to write the ID file
        while (access(hypers.nccl_id_path.c_str(), F_OK) != 0) {
            usleep(10000);  // 10ms
        }
        if (hypers.rank != 0) {
            // Small delay to ensure file is fully written
            usleep(50000);
            FILE* f = fopen(hypers.nccl_id_path.c_str(), "rb");
            fread(&nccl_id, sizeof(nccl_id), 1, f);
            fclose(f);
        }

        ncclCommInitRank(&pufferl->nccl_comm, hypers.world_size, nccl_id, hypers.rank);
        printf("Rank %d/%d: NCCL initialized\n", hypers.rank, hypers.world_size);
    }
#endif

    // Seeding (vary by rank for different random exploration)
    int seed = 42 + hypers.rank;
    torch::manual_seed(seed);
#ifdef WITH_CUDA
    torch::cuda::manual_seed(seed);
    pufferl->rng_offset = torch::zeros({1}, torch::dtype(torch::kInt64).device(torch::kCUDA));
#endif
    pufferl->rng_seed = seed;

#ifdef WITH_CUDA
    // Enable cuDNN benchmarking
    torch::globalContext().setBenchmarkCuDNN(true);
    torch::globalContext().setDeterministicCuDNN(false);
    torch::globalContext().setBenchmarkLimitCuDNN(32);

    // Enable TF32 for faster FP32 math (uses Tensor Cores on 4090)
    torch::globalContext().setAllowTF32CuBLAS(true);
    torch::globalContext().setAllowTF32CuDNN(true);

    // Enable faster FP16 reductions
    torch::globalContext().setAllowFP16ReductionCuBLAS(true);

    // BF16 reduction (if using bfloat16)
    torch::globalContext().setAllowBF16ReductionCuBLAS(true);
#endif

    // Load environment first to get input_size and action info from env
    // act_sizes: 1D tensor of action space sizes per head
    // num_action_heads: number of action heads (for MultiDiscrete)
    // act_n: sum of action space sizes (decoder output dim)
    auto [vec, act_sizes] = create_environments(hypers.num_buffers, hypers.total_agents,
        env_name, vec_kwargs, env_kwargs, pufferl->env);
    int num_action_heads = pufferl->env.actions.size(1);
    int act_n = act_sizes.sum().item<int>();
    pufferl->hypers.num_atns = num_action_heads;

    pufferl->vec = vec;
    pufferl->env_name = env_name;
    pufferl->act_n = act_n;
    pufferl->act_sizes = act_sizes.to(g_device);
    pufferl->act_sizes_cpu = act_sizes.to(torch::kInt64).contiguous();
    {
        auto* p = pufferl->act_sizes_cpu.data_ptr<int64_t>();
        pufferl->act_sizes_vec.assign(p, p + num_action_heads);
    }
    pufferl->losses = torch::zeros({NUM_LOSSES}, dev_f32);
#ifdef WITH_CUDA
    for (int i = 0; i < NUM_TRAIN_EVENTS; i++) {
        cudaEventCreate(&pufferl->profile.events[i]);
    }
#endif
    memset(pufferl->profile.accum, 0, sizeof(pufferl->profile.accum));

#ifdef WITH_CUDA
    nvmlInit();
    nvmlDeviceGetHandleByIndex(hypers.rank, &pufferl->nvml_device);
#endif

    // Determine action space type
    int* act_sizes_ptr = get_act_sizes();
    int num_continuous = 0;
    int num_discrete = 0;
    for (int i = 0; i < num_action_heads; i++) {
        if (act_sizes_ptr[i] == 1) {
            num_continuous++;
        } else {
            num_discrete++;
        }
    }
    TORCH_CHECK(num_continuous == 0 || num_discrete == 0,
        "Mixed continuous/discrete action spaces not supported. "
        "All action dimensions must be either continuous (size==1) or discrete (size>1). "
        "Got ", num_continuous, " continuous and ", num_discrete, " discrete.");
    pufferl->is_continuous = (num_continuous > 0);
    if (pufferl->is_continuous) {
        printf("Detected continuous action space with %d dimensions\n", num_action_heads);
    } else {
        printf("Detected discrete action space with %d heads\n", num_action_heads);
    }

    int input_size = pufferl->env.obs.size(1);
    pufferl->input_size = input_size;
    int hidden_size = hypers.hidden_size;
    int num_layers = hypers.num_layers;
    bool kernels = hypers.kernels;

    // Decoder output size: discrete = act_n (sum of action sizes), continuous = num_action_heads
    bool is_continuous = pufferl->is_continuous;
    int decoder_output_size = is_continuous ? num_action_heads : act_n;

    // Create fp32 master policy (for optimizer - precise gradient accumulation)
    Policy* policy_fp32 = create_policy(env_name, input_size, hidden_size,
        decoder_output_size, num_layers, act_n, is_continuous, kernels);
    policy_fp32->to(g_device);
    policy_fp32->to(torch::kFloat32);
    pufferl->policy_fp32 = policy_fp32;

    if (USE_BF16) {
        // create bf16 working policy (for fwd/bwd)
        Policy* policy_bf16 = create_policy(env_name, input_size, hidden_size,
            decoder_output_size, num_layers, act_n, is_continuous, kernels);
        policy_bf16->to(g_device);
        policy_bf16->to(torch::kBFloat16);
        pufferl->policy_bf16 = policy_bf16;
        sync_policy_weights(policy_bf16, policy_fp32); // initial sync
    } else {
        pufferl->policy_bf16 = policy_fp32;
    }

    // Optimizer uses fp32 master weights for precise gradient accumulation
    float lr = hypers.lr;
    float beta1 = hypers.beta1;
    float eps = hypers.eps;
    pufferl->muon = new Muon(policy_fp32->parameters(), lr, beta1, eps, 0.0);
    pufferl->muon->init_contiguous_weights();
#ifdef WITH_CUDA
    pufferl->muon->nccl_comm = pufferl->nccl_comm;
#endif
    pufferl->muon->world_size = hypers.world_size;
    // Allocate buffers
    int horizon = hypers.horizon;
    int total_agents = vec->total_agents;
    int batch = total_agents / hypers.num_buffers;
    int num_buffers = hypers.num_buffers;

    int minibatch_segments = hypers.minibatch_size / horizon;

    pufferl->rollouts = create_rollouts(horizon, total_agents, input_size, num_action_heads);
    // use actual RNN layer count from the policy (may differ from config num_layers
    // when env-specific create_policy overrides the RNN depth)
    int rnn_layers = (int)pufferl->policy_fp32->initial_state(1, g_device).size(0);
    pufferl->train_buf = create_train_graph(minibatch_segments, horizon, input_size,
        hidden_size, num_action_heads, rnn_layers);

    // Per-buffer states: each is {num_layers, block_size, hidden} for contiguous access
    pufferl->buffer_states.resize(num_buffers);
    for (int i = 0; i < num_buffers; i++) {
        pufferl->buffer_states[i] = pufferl->policy_bf16->initial_state(batch, g_device);
    }

#ifdef WITH_CUDA
    if (hypers.cudagraphs >= 0) {
        pufferl->train_cudagraph = at::cuda::CUDAGraph();
        pufferl->train_pool_id = at::cuda::graph_pool_handle();
        pufferl->train_warmup = 0;

        // Fused rollout cudagraphs: [horizon][num_buffers]
        pufferl->rollout_pool_id = at::cuda::graph_pool_handle();
        pufferl->fused_rollout_cudagraphs.resize(horizon);
        for (int h = 0; h < horizon; ++h) {
            pufferl->fused_rollout_cudagraphs[h].resize(num_buffers);
            for (int b = 0; b < num_buffers; ++b) {
                pufferl->fused_rollout_cudagraphs[h][b] = at::cuda::CUDAGraph();
            }
        }

        // Snapshot weights + optimizer state before init-time capture
        Tensor saved_weights = pufferl->muon->weight_buffer.clone();
        Tensor saved_momentum;
        if (pufferl->muon->momentum_buffer.defined()) {
            saved_momentum = pufferl->muon->momentum_buffer.clone();
        }

        // Run warmup + capture on a fresh stream (matching original capture_graph).
        // Tensors get associated with warmup_stream, not the default stream.
        // Captured graphs' event-waits reference warmup_stream which is dead at runtime.
        auto saved_stream = at::cuda::getCurrentCUDAStream();
        auto warmup_stream = at::cuda::getStreamFromPool();
        at::cuda::setCurrentCUDAStream(warmup_stream);

        // Init-time warmup + capture BEFORE creating streams/threads.
        // No per-buffer streams exist yet = no cross-stream deps baked into graphs.
        for (pufferl->epoch = 0; pufferl->epoch <= hypers.cudagraphs; pufferl->epoch++) {
            rollouts_impl(*pufferl);
        }
        pufferl->rollout_captured = true;

        for (int i = 0; i <= hypers.cudagraphs; i++) {
            train_impl(*pufferl);
        }

        warmup_stream.synchronize();
        cudaDeviceSynchronize();
        at::cuda::setCurrentCUDAStream(saved_stream);

        // Restore weights + optimizer state corrupted by warmup/capture
        {
        torch::NoGradGuard no_grad;
        pufferl->muon->weight_buffer.copy_(saved_weights);
        if (saved_momentum.defined()) {
            pufferl->muon->momentum_buffer.copy_(saved_momentum);
        } else {
            pufferl->muon->momentum_buffer = Tensor();
        }
        if (USE_BF16) {
            sync_policy_weights(pufferl->policy_bf16, pufferl->policy_fp32);
        }
        pufferl->muon->zero_grad();
        if (USE_BF16) {
            pufferl->policy_bf16->zero_grad();
        }
        } // end NoGradGuard

        pufferl->epoch = 0;
    }

    // Create PyTorch-managed streams and assign to vec
    for (int i = 0; i < num_buffers; i++) {
        pufferl->torch_streams.push_back(at::cuda::getStreamFromPool(false));
        vec->streams[i] = pufferl->torch_streams[i].stream();
    }
#endif

    create_static_threads(vec, hypers.num_threads, horizon, pufferl.get(),
        net_callback_wrapper, thread_init_wrapper);
    static_vec_reset(vec);

    return pufferl;
}

void close_impl(PuffeRL& pufferl) {
#ifdef WITH_CUDA
    cudaDeviceSynchronize();
    nvmlShutdown();
    for (int i = 0; i < NUM_TRAIN_EVENTS; i++) {
        cudaEventDestroy(pufferl.profile.events[i]);
    }

    // Reset CUDA graphs first (they hold references to tensor memory)
    pufferl.train_cudagraph.reset();
    pufferl.fused_rollout_cudagraphs.clear();
#endif

    delete pufferl.muon;
    pufferl.muon = nullptr;

    if (USE_BF16) {
        delete pufferl.policy_bf16;
    }
    delete pufferl.policy_fp32;
    pufferl.policy_bf16 = nullptr;
    pufferl.policy_fp32 = nullptr;

    // Clear buffer states (releases device tensors)
    pufferl.buffer_states.clear();

    // Clear rollout buffers
    pufferl.rollouts.observations = Tensor();
    pufferl.rollouts.actions = Tensor();
    pufferl.rollouts.values = Tensor();
    pufferl.rollouts.logprobs = Tensor();
    pufferl.rollouts.rewards = Tensor();
    pufferl.rollouts.terminals = Tensor();
    pufferl.rollouts.ratio = Tensor();
    pufferl.rollouts.importance = Tensor();

    // Clear train buffers
    pufferl.train_buf.mb_obs = Tensor();
    pufferl.train_buf.mb_state = Tensor();
    pufferl.train_buf.mb_actions = Tensor();
    pufferl.train_buf.mb_logprobs = Tensor();
    pufferl.train_buf.mb_advantages = Tensor();
    pufferl.train_buf.mb_prio = Tensor();
    pufferl.train_buf.mb_values = Tensor();
    pufferl.train_buf.mb_returns = Tensor();
    pufferl.train_buf.mb_ratio = Tensor();
    pufferl.train_buf.mb_newvalue = Tensor();

    // Clear misc tensors
    pufferl.act_sizes = Tensor();
    pufferl.act_sizes_cpu = Tensor();
    pufferl.losses = Tensor();
#ifdef WITH_CUDA
    pufferl.rng_offset = Tensor();
#endif

    // Clear env tensors (from_blob wrappers - don't own memory but hold refs)
    pufferl.env.obs = Tensor();
    pufferl.env.actions = Tensor();
    pufferl.env.rewards = Tensor();
    pufferl.env.terminals = Tensor();

#ifdef WITH_CUDA
    // Clear torch streams
    pufferl.torch_streams.clear();
#endif

    // Free selfplay resources
    if (pufferl.selfplay_policy) {
        delete pufferl.selfplay_policy;
        pufferl.selfplay_policy = nullptr;
    }
    if (pufferl.selfplay_obs_raw) {
        free(pufferl.selfplay_obs_raw);
        pufferl.selfplay_obs_raw = nullptr;
    }
    if (pufferl.selfplay_mask_raw) {
        free(pufferl.selfplay_mask_raw);
        pufferl.selfplay_mask_raw = nullptr;
    }
    pufferl.selfplay_obs = Tensor();
    pufferl.selfplay_state = Tensor();

    // Close environment vectorization (frees env buffers)
    static_vec_close(pufferl.vec);
    pufferl.vec = nullptr;

#ifdef WITH_CUDA
    // Cleanup NCCL
    if (pufferl.nccl_comm != nullptr) {
        ncclCommDestroy(pufferl.nccl_comm);
        pufferl.nccl_comm = nullptr;
    }

    // Force CUDA to release cached memory
    c10::cuda::CUDACachingAllocator::emptyCache();
    cudaDeviceSynchronize();
#endif
}

void profiler_start() {
#ifdef WITH_CUDA
    cudaDeviceSynchronize();
    printf("cudaProfilerStart()\n");
    cudaProfilerStart();
#else
    printf("profiler_start() is a no-op without CUDA\n");
#endif
}

void profiler_stop() {
#ifdef WITH_CUDA
    cudaDeviceSynchronize();
    cudaProfilerStop();
    printf("cudaProfilerStop()\n");
#else
    printf("profiler_stop() is a no-op without CUDA\n");
#endif
}

} // namespace pufferlib
