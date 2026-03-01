/**
 * @fileoverview Metal training loop for PufferLib static-native.
 *
 * Port of pufferlib.cu (CUDA) to Apple Silicon Metal. Key differences:
 * - No CUDA graphs (just re-run operations every step)
 * - No NCCL (single GPU only)
 * - No nvml (GPU utilization not reported)
 * - No bf16 (always PRECISION_FLOAT, USE_BF16 = false, PRECISION_SIZE = 4)
 * - Unified memory: memcpy/memset for host<->device (same physical memory)
 * - MetalStream passed as cudaStream_t (void*) through vtable function pointers
 * - Per-buffer Metal streams for rollout callback threads
 * - CPU timing via std::chrono instead of CUDA events
 */

#import "metal_platform.h"
#include "metal_kernels.mm"
#include "vecenv.h"

#include <vecLib/vecLib.h>  // BLASSetThreading

#include <chrono>
#include <cstring>
#include <memory>
#include <mutex>
#include <vector>

static thread_local cudaStream_t tl_rollout_stream = 0;
static std::mutex g_rollout_profile_mutex;

// ============================================================================
// GPU sync helper — flush any pending Metal compute work before CPU access.
// Mirrors the static inline in metal_platform.mm, needed here since that's
// a separate translation unit.
// ============================================================================

static inline MetalStream* get_stream(cudaStream_t s) {
    return s ? (MetalStream*)s : &mtl_ctx()->stream;
}

static inline void ensure_gpu_synced(cudaStream_t s) {
    MetalStream* ms = get_stream(s);
    if (ms->enc_active || ms->pending_work) ms->sync();
}

// Mach-time to milliseconds for profiling
static mach_timebase_info_data_t g_prof_tb = {0, 0};
static inline float prof_ms(uint64_t t0, uint64_t t1) {
    static std::once_flag g_prof_tb_once;
    std::call_once(g_prof_tb_once, []() { mach_timebase_info(&g_prof_tb); });
    return (float)((double)(t1 - t0) * g_prof_tb.numer / g_prof_tb.denom / 1e6);
}

// ============================================================================
// Observation dtype size
// ============================================================================

int obs_dtype_size(int dtype) {
    if (dtype == FLOAT || dtype == INT) {
        return sizeof(float);
    }
    if (dtype == DOUBLE) {
        return sizeof(double);
    }
    return sizeof(char);  // UNSIGNED_CHAR, CHAR
}

// ============================================================================
// Environment creation — unified memory, no GPU copy needed
// ============================================================================

StaticVec* create_environments(int num_buffers, int total_agents,
        const std::string& env_name, Dict* vec_kwargs, Dict* env_kwargs, EnvBuf& env) {
    StaticVec* vec = create_static_vec(total_agents, num_buffers, vec_kwargs, env_kwargs);

    int obs_size = get_obs_size();
    int num_atns = get_num_atns();
    int obs_type = get_obs_type();

    // Unified memory: env obs/actions/rewards/terminals point directly at vecenv buffers
    env.obs = {.bytes = (char*)vec->gpu_observations, .shape = {total_agents, obs_size}, .dtype_size = obs_dtype_size(obs_type)};
    env.obs_raw_dtype = obs_type;
    env.actions = {.bytes = (char*)vec->gpu_actions, .shape = {total_agents, num_atns}, .dtype_size = (int)sizeof(double)};
    env.rewards = {.bytes = (char*)vec->gpu_rewards, .shape = {total_agents}, .dtype_size = (int)sizeof(float)};
    env.terminals = {.bytes = (char*)vec->gpu_terminals, .shape = {total_agents}, .dtype_size = (int)sizeof(float)};

    return vec;
}

// ============================================================================
// Hyperparameters — single GPU only, no NCCL
// ============================================================================

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
    int cudagraphs;  // kept for API compat, always -1 (ignored)
    bool kernels;
    bool profile;
    bool overlap;  // async training overlap: train on separate GPU queue
    bool use_adam;  // use Adam optimizer instead of Muon
    // Architecture
    int arch_type;  // 0=ARCH_RICH (3-layer MLP + LN decoder), 1=ARCH_SIMPLE (upstream single-linear)
    // Threading
    int num_threads;
    // RNG seed
    uint64_t seed;
} HypersT;

// ============================================================================
// Profiling — CPU-based timing via std::chrono
// ============================================================================

enum ProfileIdx {
    PROF_ROLLOUT = 0,
    PROF_EVAL_GPU,
    PROF_EVAL_ENV,
    PROF_TRAIN_MISC,
    PROF_TRAIN_FORWARD,
    // Fine-grained rollout sub-phases
    PROF_ROLLOUT_OBS_COPY,
    PROF_ROLLOUT_FWD,
    PROF_ROLLOUT_SAMPLE,
    PROF_ROLLOUT_ACT_COPY,
    // Fine-grained training sub-phases
    PROF_TRAIN_PRELOOP,
    PROF_TRAIN_ADVANTAGE,
    PROF_TRAIN_PRIO,
    PROF_TRAIN_SELECT,
    PROF_TRAIN_FWD,
    PROF_TRAIN_PPO,
    PROF_TRAIN_BACKWARD,
    PROF_TRAIN_GRAD_COPY,
    PROF_TRAIN_GRAD_CLIP,
    PROF_TRAIN_MUON,
    PROF_TRAIN_SYNC,
    NUM_PROF,
};

static const char* PROF_NAMES[NUM_PROF] = {
    "rollout",
    "eval_gpu",
    "eval_env",
    "train_misc",
    "train_forward",
    "rollout_obs_copy",
    "rollout_fwd",
    "rollout_sample",
    "rollout_act_copy",
    "train_preloop",
    "train_advantage",
    "train_prio",
    "train_select",
    "train_fwd",
    "train_ppo",
    "train_backward",
    "train_grad_copy",
    "train_grad_clip",
    "train_muon",
    "train_sync",
};

typedef struct {
    float accum[NUM_PROF];
} ProfileT;

// ============================================================================
// PuffeRL state — Metal version (no CUDA graphs, NCCL, nvml, multi-stream)
// ============================================================================

typedef struct {
    Policy* policy;
    PolicyWeights weights_fp32;
    PolicyWeights weights_bf16;  // fp16 training weights (was shared with fp32)
    // Double-buffered inference weights for rollout/training overlap.
    // Rollout reads weights_infer (CPU cblas), training writes weights_fp32 (GPU).
    // After each training sync, weights_fp32 is memcpy'd to weights_infer.
    PolicyWeights weights_infer;
    Allocator infer_params_alloc;
    bool overlap_enabled = false;
    bool train_pending = false;  // GPU training dispatched but not yet synced
    PolicyActivations train_activations;
    AllocSet alloc_fp32;
    AllocSet alloc_fp16;  // fp16 training: weights, activations, gradients
    Allocator pufferl_alloc;
    StaticVec* vec;
    Muon* muon;
    Adam* adam;
    bool use_adam = false;
    HypersT hypers;
    bool is_continuous;
    std::vector<cudaStream_t> rollout_streams;
    std::vector<PufTensor> buffer_states;
    std::vector<PufTensor> sample_act_f32_buffers;
    std::vector<PolicyActivations> buffer_activations;
    std::vector<Allocator> buffer_allocs;
    RolloutBuf rollouts;
    RolloutBuf train_rollouts;
    EnvBuf env;
    TrainGraph train_buf;
    PufTensor old_values_puf;
    PufTensor advantages_puf;
    PufTensor act_sizes_puf;
    PufTensor losses_puf;
    PPOBuffersPuf ppo_bufs_puf;
    PrioBuffers prio_bufs;
    PufTensor param_fp32_puf;
    PufTensor param_fp16_puf;   // fp16 weight buffer (flat view)
    PufTensor grad_bf16_puf;    // fp16 gradient buffer (flat view)
    PufTensor grad_norm_puf;
    PufTensor rng_offset_puf;
    // fp16 boundary buffers: obs cast (fp32→fp16), dec_out cast (fp16→fp32),
    // state (zeroed fp16 for scan initial state)
    PufTensor fp16_obs_buf;
    PufTensor fp32_dec_out_buf;
    PufTensor fp16_state_buf;
    Allocator fp16_boundary_alloc;
    ProfileT profile;
    int rollout_sync_count;
    double rollout_sync_ms;
    int train_sync_count;
    double train_sync_ms;
    int epoch;
    uint64_t rng_seed;
    // Action mask: true if obs embeds a mask in the last act_n columns.
    // When false, a static all-ones buffer is used instead.
    bool has_mask = false;
    PufTensor ones_mask;  // (total_agents * obs_cols) all 1.0f, fallback mask
} PuffeRL;

// ============================================================================
// Logging
// ============================================================================

Dict* log_environments_impl(PuffeRL& pufferl) {
    Dict* out = create_dict(32);
    static_vec_log(pufferl.vec, out);
    return out;
}

// ============================================================================
// Per-buffer thread init — called once per buffer thread at creation
// ============================================================================

extern "C" void thread_init_metal(void* ctx, int buf) {
    PuffeRL* pufferl = (PuffeRL*)ctx;
    assert(buf >= 0 && buf < (int)pufferl->rollout_streams.size());
    tl_rollout_stream = pufferl->rollout_streams[buf];
    assert(tl_rollout_stream && "thread_init_metal requires per-buffer stream");

    // Small GEMMs (256x384x128) don't benefit from multi-threaded BLAS.
    // Thread coordination overhead dominates. macOS 15+ per-thread setting.
    if (__builtin_available(macOS 15.0, *)) {
        BLASSetThreading(BLAS_THREADING_SINGLE_THREADED);
    }
}


// ============================================================================
// Rollout callback — called per buffer per horizon step
// ============================================================================

extern "C" void net_callback_wrapper(void* ctx, int buf, int t) {
  @autoreleasepool {
    PuffeRL* pufferl = (PuffeRL*)ctx;
    HypersT& hypers = pufferl->hypers;

    RolloutBuf& rollouts = pufferl->rollouts;
    EnvBuf& env = pufferl->env;
    int block_size = pufferl->vec->total_agents / hypers.num_buffers;
    int start = buf * block_size;
    cudaStream_t stream = tl_rollout_stream;
    assert(stream && "rollout callback requires thread-local stream");

    uint64_t tp0 = mach_absolute_time();

    // Copy env obs to rollout buffer — CPU only, no GPU dispatch
    PufTensor& obs_env = env.obs;
    PufTensor obs_src = {
        .bytes = obs_env.bytes + (int64_t)start * obs_env.shape[1] * obs_env.dtype_size,
        .shape = {block_size, obs_env.shape[1]},
        .dtype_size = obs_env.dtype_size
    };

    PufTensor obs_dst = puf_slice(rollouts.observations, t, start, block_size);

    if (obs_env.dtype_size == sizeof(char)) {
        cpu_cast_u8_to_f32((float*)obs_dst.bytes, (const uint8_t*)obs_src.bytes,
                           (int)obs_src.numel());
    } else if (obs_env.dtype_size == sizeof(float)) {
        memcpy(obs_dst.bytes, obs_src.bytes, obs_src.numel() * obs_src.dtype_size);
    } else {
        assert(false && "Unsupported obs dtype: only uint8 and float32 are supported");
    }

    // Rewards + terminals — direct memcpy, no sync check needed
    PufTensor rew_dst = puf_slice(rollouts.rewards, t, start, block_size);
    memcpy(rew_dst.bytes, env.rewards.bytes + start * (int)sizeof(float),
           block_size * sizeof(float));

    PufTensor term_dst = puf_slice(rollouts.terminals, t, start, block_size);
    memcpy(term_dst.bytes, env.terminals.bytes + start * (int)sizeof(float),
           block_size * sizeof(float));

    uint64_t tp1 = mach_absolute_time();

    // Forward pass + fused GPU sampling — dispatched on the same command buffer.
    // Single sync makes both forward results and sampled actions CPU-visible.
    PufTensor act_slice = puf_slice(rollouts.actions, t, start, block_size);
    PufTensor lp_slice = puf_slice(rollouts.logprobs, t, start, block_size);
    PufTensor val_slice = puf_slice(rollouts.values, t, start, block_size);
    int num_atns = (int)pufferl->act_sizes_puf.numel();
    uint32_t* buf_rng_offset = (uint32_t*)((int64_t*)pufferl->rng_offset_puf.bytes + buf);
    uint64_t buf_rng_seed = pufferl->rng_seed + buf;

    PufTensor state_puf = pufferl->buffer_states[buf];
    PolicyWeights& infer_weights = pufferl->overlap_enabled
        ? pufferl->weights_infer : pufferl->weights_fp32;
    Policy* p = pufferl->policy;
    PolicyActivations& acts = pufferl->buffer_activations[buf];
    PufTensor& act_f32_buf = pufferl->sample_act_f32_buffers[buf];

    // When fused encoder+layer0 is available, skip encoder and pass obs
    // directly to mingru (layer0 uses fused weight: obs @ fused^T → combined).
    MinGRUWeights *mw = (MinGRUWeights *)infer_weights.network;
    PufTensor mingru_input = mw->fused_enc_layer0.bytes
        ? obs_dst
        : p->encoder.forward(infer_weights.encoder, acts.encoder, obs_dst, stream);
    PufTensor h = p->network.forward(infer_weights.network, mingru_input, state_puf, acts.network, stream);
    PufTensor dec_puf = p->decoder.forward(infer_weights.decoder, acts.decoder, h, stream);

    // GPU sampling: dispatch on same command buffer, before sync.
    // Action mask: if env embeds mask in obs, read from last act_n columns.
    // Otherwise use all-ones fallback with stride=0 (no masking).
    int obs_cols = (int)obs_dst.shape[1];
    int fused_cols = (int)dec_puf.shape[1];
    const float* mask_ptr;
    int mask_stride;
    if (pufferl->has_mask) {
        int mask_offset = obs_cols - (fused_cols - 1);  // e.g. 373 - 39 = 334
        mask_ptr = (const float*)obs_dst.bytes + mask_offset;
        mask_stride = obs_cols;
    } else {
        mask_ptr = (const float*)pufferl->ones_mask.bytes;
        mask_stride = 0;  // all rows read same act_n ones
    }

    mtl_sample_logits_dispatch_to(
        dec_puf, pufferl->act_sizes_puf,
        (float*)act_f32_buf.bytes, (float*)lp_slice.bytes, (float*)val_slice.bytes,
        mask_ptr, mask_stride,
        buf_rng_seed, buf_rng_offset, stream);

    ensure_gpu_synced(stream);
    mtl_sample_logits_expand((const float*)act_f32_buf.bytes,
                             (double*)act_slice.bytes, block_size * num_atns);

    // Zero RNN state for agents that terminated this step.
    // After sync: terminals (CPU memcpy) and buffer_states (GPU write) are both visible.
    {
        PufTensor& st = pufferl->buffer_states[buf];
        const float* terms = (const float*)term_dst.bytes;
        int layers = (int)st.shape[0];
        int H = (int)st.shape[2];
        int row_bytes = H * (int)st.dtype_size;
        char* base = (char*)st.bytes;
        for (int a = 0; a < block_size; a++) {
            if (terms[a] != 0.0f) {
                for (int l = 0; l < layers; l++) {
                    memset(base + ((int64_t)l * block_size + a) * row_bytes, 0, row_bytes);
                }
            }
        }
    }

    uint64_t tp2 = mach_absolute_time();

    uint64_t tp3 = mach_absolute_time();

    int64_t act_cols = env.actions.shape[1];
    memcpy(
        env.actions.bytes + start * act_cols * env.actions.dtype_size,
        act_slice.bytes,
        act_slice.numel() * act_slice.dtype_size);

    uint64_t tp4 = mach_absolute_time();

    // Accumulate fine-grained rollout timing (callbacks run concurrently).
    {
        std::lock_guard<std::mutex> lk(g_rollout_profile_mutex);
        pufferl->profile.accum[PROF_ROLLOUT_OBS_COPY] += prof_ms(tp0, tp1);
        pufferl->profile.accum[PROF_ROLLOUT_FWD] += prof_ms(tp1, tp2);
        pufferl->profile.accum[PROF_ROLLOUT_SAMPLE] += prof_ms(tp2, tp3);
        pufferl->profile.accum[PROF_ROLLOUT_ACT_COPY] += prof_ms(tp3, tp4);
    }
  } // @autoreleasepool
}

// ============================================================================
// Weight copy: weights_fp32 → weights_infer (for rollout/training overlap)
// ============================================================================

static void copy_weights_to_infer(PuffeRL& pufferl) {
    int64_t nbytes = pufferl.alloc_fp32.params.total_elems * sizeof(float);
    memcpy(pufferl.infer_params_alloc.mem, pufferl.alloc_fp32.params.mem, nbytes);
}

// Allocate fused encoder+layer0 weight buffer.
// Fused encoder+layer0 disabled: nonlinear 3-layer encoder can't be fused.
// sync_fused_weight kept as no-op — called from metal_bindings.mm.
void sync_fused_weight(PuffeRL& pufferl) {
    (void)pufferl; // nothing to sync with nonlinear encoder
}

// ============================================================================
// Rollout loop — serial (single stream, no per-buffer threads for GPU work)
// ============================================================================

void rollouts_impl(PuffeRL& pufferl) {
    int horizon = pufferl.hypers.horizon;
    int num_buffers = pufferl.hypers.num_buffers;

    for (int i = 0; i < num_buffers * horizon; ++i) {
        int buf = i % num_buffers;
        int h = i / num_buffers;
        net_callback_wrapper(&pufferl, buf, h);
    }
}

// ============================================================================
// Training loop
// ============================================================================

void train_impl(PuffeRL& pufferl) {
    HypersT& hypers = pufferl.hypers;
    uint64_t tp_preloop0 = mach_absolute_time();

    cudaStream_t train_stream = pufferl.overlap_enabled
        ? (cudaStream_t)mtl_train_stream()
        : (cudaStream_t)mtl_stream();

    // GPU training: keep all ops on the Metal encoder (GEMM, copy, zero, add).
    puf_set_gpu_training(true);

    // Transpose rollouts from (horizon, segments, ...) to (segments, horizon, ...)
    RolloutBuf& src = pufferl.rollouts;
    RolloutBuf& rollouts = pufferl.train_rollouts;

    puf_transpose_01(rollouts.observations, src.observations, train_stream);
    puf_transpose_01(rollouts.actions, src.actions, train_stream);
    puf_transpose_01(rollouts.logprobs, src.logprobs, train_stream);
    puf_transpose_01(rollouts.rewards, src.rewards, train_stream);
    puf_transpose_01(rollouts.terminals, src.terminals, train_stream);
    puf_transpose_01(rollouts.ratio, src.ratio, train_stream);
    puf_transpose_01(rollouts.values, src.values, train_stream);

    // Clamp rewards and fill ratio (f32 path only, no bf16)
    mtl_clamp_f32((float*)rollouts.rewards.bytes, -1.0f, 1.0f,
                  (int)rollouts.rewards.numel(), train_stream);
    mtl_fill_f32((float*)rollouts.ratio.bytes, 1.0f,
                 (int)rollouts.ratio.numel(), train_stream);

    // old_values = values.clone()
    puf_copy(pufferl.old_values_puf, rollouts.values, train_stream);
    // Metal 4 visibility boundary before minibatch loop consumes transposed rollouts.
    mtl_barrier((MetalStream*)train_stream);

    int batch_size = hypers.total_agents * hypers.horizon;
    int minibatch_segments = hypers.minibatch_size / hypers.horizon;
    float prio_alpha = hypers.prio_alpha;
    int current_epoch = pufferl.epoch;
    int total_epochs = hypers.total_timesteps / batch_size;
    int total_minibatches = hypers.replay_ratio * batch_size / hypers.minibatch_size;

    if (hypers.anneal_lr) {
        float lr_min = hypers.min_lr_ratio * hypers.lr;
        float lr = cosine_annealing(hypers.lr, lr_min, current_epoch, total_epochs);
        float* lr_ptr = pufferl.use_adam ? pufferl.adam->lr_ptr : pufferl.muon->lr_ptr;
        *lr_ptr = lr;
    }

    float anneal_beta = hypers.prio_beta0 + (1.0f - hypers.prio_beta0) * prio_alpha
        * (float)current_epoch / (float)total_epochs;

    uint64_t tp_preloop1 = mach_absolute_time();

    // Advantage + prio precompute (single GPU sync for CDF read).
    uint64_t tp_prio_done = mach_absolute_time();
    pufferl.profile.accum[PROF_TRAIN_PRELOOP] += prof_ms(tp_preloop0, tp_preloop1);
    pufferl.profile.accum[PROF_TRAIN_ADVANTAGE] += prof_ms(tp_preloop1, tp_prio_done);

    puf_set_gpu_training(false);

    if (pufferl.overlap_enabled) {
        // Iteration-level async: dispatch ALL training minibatches on train_stream
        // (separate Metal command queue). GPU executes the training work asynchronously
        // during the next rollout's CPU env_step (~4.5s of GPU-idle time per rollout).
        // Inference runs on the default queue, reading from weights_infer (updated at
        // the end of training via GPU blit). 1-iteration policy lag — PPO handles this
        // via importance ratios (stored log-probs).
        cudaStream_t ts = train_stream;
        uint32_t* train_rng_offset = (uint32_t*)((int64_t*)pufferl.rng_offset_puf.bytes + hypers.num_buffers);

        puf_set_gpu_training(true);

        for (int mb = 0; mb < total_minibatches; ++mb) {
            uint64_t tp0 = mach_absolute_time();

            puf_zero(pufferl.advantages_puf, ts);
            puff_advantage_cuda(rollouts.values, rollouts.rewards, rollouts.terminals,
                rollouts.ratio, pufferl.advantages_puf, hypers.gamma, hypers.gae_lambda,
                hypers.vtrace_rho_clip, hypers.vtrace_c_clip, ts);
            prio_replay_cuda(pufferl.advantages_puf, prio_alpha, minibatch_segments,
                hypers.total_agents, anneal_beta,
                pufferl.prio_bufs, pufferl.rng_seed, train_rng_offset, ts);
            mtl_barrier((MetalStream*)ts); // prio idx/weights -> select copy

            uint64_t tp2 = mach_absolute_time();

            puf_zero(pufferl.train_buf.mb_state, ts);
            {
                RolloutBuf sel_src = rollouts;
                sel_src.values = rollouts.values;
                mtl_select_copy(sel_src, pufferl.train_buf,
                    (const int64_t*)pufferl.prio_bufs.idx.bytes,
                    (const float*)pufferl.advantages_puf.bytes,
                    (const float*)pufferl.prio_bufs.mb_prio.bytes,
                    minibatch_segments,
                    pufferl.fp16_obs_buf.bytes, ts);
            }
            mtl_barrier((MetalStream*)ts); // minibatch buffers -> forward/PPO

            uint64_t tp3 = mach_absolute_time();

            PufTensor obs_puf = pufferl.train_buf.mb_obs;
            PufTensor state_puf = pufferl.train_buf.mb_state;
            PufTensor dec_puf = policy_forward_train(pufferl.policy, pufferl.weights_fp32,
                pufferl.train_activations, obs_puf, state_puf, ts);

            uint64_t tp4 = mach_absolute_time();

            PufTensor dec_puf_f32 = dec_puf;

            PufTensor p_logstd;
            if (pufferl.is_continuous) {
                if (hypers.arch_type == ARCH_SIMPLE) {
                    p_logstd = ((SimpleDecoderWeights*)pufferl.weights_fp32.decoder)->logstd;
                } else {
                    p_logstd = ((DecoderWeights*)pufferl.weights_fp32.decoder)->logstd;
                }
            }

            ppo_loss_fwd_bwd(dec_puf_f32, p_logstd, pufferl.train_buf,
                pufferl.act_sizes_puf, pufferl.losses_puf,
                hypers.clip_coef, hypers.vf_clip_coef, hypers.vf_coef, hypers.ent_coef,
                pufferl.ppo_bufs_puf, pufferl.is_continuous,
                pufferl.has_mask ? nullptr : (const float*)pufferl.ones_mask.bytes,
                pufferl.has_mask ? 0 : 0,  // stride=0: all rows read same ones
                ts);
            mtl_barrier((MetalStream*)ts); // PPO outputs -> scatter

            mtl_scatter_ppo_outputs(pufferl.train_buf, rollouts,
                (const int64_t*)pufferl.prio_bufs.idx.bytes, ts);
            mtl_barrier((MetalStream*)ts); // scatter -> next mb advantage/select

            uint64_t tp5 = mach_absolute_time();

            PufTensor grad_logits_puf = pufferl.ppo_bufs_puf.grad_logits;
            PufTensor grad_logstd_puf = pufferl.is_continuous ? pufferl.ppo_bufs_puf.grad_logstd : PufTensor();
            PufTensor grad_values_puf = pufferl.ppo_bufs_puf.grad_values;
            policy_backward(pufferl.policy, pufferl.weights_fp32, pufferl.train_activations,
                grad_logits_puf, grad_logstd_puf, grad_values_puf, ts);

            uint64_t tp6 = mach_absolute_time();

            PufTensor& gc = pufferl.use_adam ? pufferl.adam->gc_puf : pufferl.muon->gc_puf;
            if (pufferl.grad_bf16_puf.dtype_size == 2) {
                mtl_barrier((MetalStream*)ts);
                mtl_cast_f16_to_f32((float*)gc.bytes,
                                    pufferl.grad_bf16_puf.bytes,
                                    (int)pufferl.grad_bf16_puf.numel(), ts);
            } else {
                puf_copy(gc, pufferl.grad_bf16_puf, ts);
            }

            uint64_t tp7 = mach_absolute_time();

            mtl_barrier((MetalStream*)ts); // grad_cast→clip
            {
                float* scratch = (float*)pufferl.grad_norm_puf.bytes;
                clip_grad_norm_f32(gc, scratch, hypers.max_grad_norm, 1e-6f, ts);
            }

            uint64_t tp8 = mach_absolute_time();

            mtl_barrier((MetalStream*)ts); // clip→optimizer
            // Optimizer step with fused fp32→fp16 param cast
            if (pufferl.use_adam)
                adam_step(pufferl.adam, pufferl.param_fp16_puf.bytes, ts);
            else
                muon_step(pufferl.muon, pufferl.param_fp16_puf.bytes, ts);

            // Metal 4 coherence: barrier so next minibatch's forward pass
            // sees updated weights from optimizer.
            mtl_barrier((MetalStream*)ts);

            uint64_t tp9 = mach_absolute_time();

            pufferl.profile.accum[PROF_TRAIN_PRIO] += prof_ms(tp0, tp2);
            pufferl.profile.accum[PROF_TRAIN_SELECT] += prof_ms(tp2, tp3);
            pufferl.profile.accum[PROF_TRAIN_FWD] += prof_ms(tp3, tp4);
            pufferl.profile.accum[PROF_TRAIN_PPO] += prof_ms(tp4, tp5);
            pufferl.profile.accum[PROF_TRAIN_BACKWARD] += prof_ms(tp5, tp6);
            pufferl.profile.accum[PROF_TRAIN_GRAD_COPY] += prof_ms(tp6, tp7);
            pufferl.profile.accum[PROF_TRAIN_GRAD_CLIP] += prof_ms(tp7, tp8);
            pufferl.profile.accum[PROF_TRAIN_MUON] += prof_ms(tp8, tp9);
            pufferl.profile.accum[PROF_TRAIN_MISC] += prof_ms(tp0, tp3);
            pufferl.profile.accum[PROF_TRAIN_FORWARD] += prof_ms(tp3, tp9);
        }

        // After all minibatches: GPU-copy weights to infer and recompute fused weight.
        // Both dispatched on train_stream so they execute after training completes.
        {
            int64_t total_elems = pufferl.alloc_fp32.params.total_elems;
            PufTensor fp32_all = {.bytes = (char*)pufferl.alloc_fp32.params.mem,
                                  .shape = {total_elems}, .dtype_size = sizeof(float)};
            PufTensor infer_all = {.bytes = (char*)pufferl.infer_params_alloc.mem,
                                   .shape = {total_elems}, .dtype_size = sizeof(float)};
            puf_copy(infer_all, fp32_all, ts);
            // fused_enc_layer0 disabled (nonlinear encoder)
        }

        puf_set_gpu_training(false);

        // Flush train_stream: commit command buffer so GPU starts executing
        // asynchronously. wait_completed() is called at the start of the next
        // rollouts() to ensure training is done before inference reads weights_infer.
        ((MetalStream*)train_stream)->flush();

        pufferl.train_pending = true;
        pufferl.epoch += 1;
        return;
    }

    // Non-overlap: run minibatch loop synchronously.
    uint32_t* train_rng_offset = (uint32_t*)((int64_t*)pufferl.rng_offset_puf.bytes + hypers.num_buffers);

    puf_set_gpu_training(true);
    for (int mb = 0; mb < total_minibatches; ++mb) {
        uint64_t tp0 = mach_absolute_time();

        puf_zero(pufferl.advantages_puf, train_stream);
        puff_advantage_cuda(rollouts.values, rollouts.rewards, rollouts.terminals,
            rollouts.ratio, pufferl.advantages_puf, hypers.gamma, hypers.gae_lambda,
            hypers.vtrace_rho_clip, hypers.vtrace_c_clip, train_stream);
        prio_replay_cuda(pufferl.advantages_puf, prio_alpha, minibatch_segments,
            hypers.total_agents, anneal_beta,
            pufferl.prio_bufs, pufferl.rng_seed, train_rng_offset, train_stream);
        mtl_barrier((MetalStream*)train_stream); // prio idx/weights -> select copy

        uint64_t tp2 = mach_absolute_time();

        puf_zero(pufferl.train_buf.mb_state, train_stream);
        {
            RolloutBuf sel_src = rollouts;
            sel_src.values = rollouts.values;
            mtl_select_copy(sel_src, pufferl.train_buf,
                (const int64_t*)pufferl.prio_bufs.idx.bytes,
                (const float*)pufferl.advantages_puf.bytes,
                (const float*)pufferl.prio_bufs.mb_prio.bytes,
                minibatch_segments,
                pufferl.fp16_obs_buf.bytes, train_stream);
        }
        mtl_barrier((MetalStream*)train_stream); // minibatch buffers -> forward/PPO

        uint64_t tp3 = mach_absolute_time();

        PufTensor obs_puf = pufferl.train_buf.mb_obs;
        PufTensor state_puf = pufferl.train_buf.mb_state;
        PufTensor dec_puf = policy_forward_train(pufferl.policy, pufferl.weights_fp32,
            pufferl.train_activations, obs_puf, state_puf, train_stream);

        uint64_t tp4 = mach_absolute_time();

        PufTensor dec_puf_f32 = dec_puf;

        PufTensor p_logstd;
        if (pufferl.is_continuous) {
            if (hypers.arch_type == ARCH_SIMPLE) {
                p_logstd = ((SimpleDecoderWeights*)pufferl.weights_fp32.decoder)->logstd;
            } else {
                p_logstd = ((DecoderWeights*)pufferl.weights_fp32.decoder)->logstd;
            }
        }
        ppo_loss_fwd_bwd(dec_puf_f32, p_logstd, pufferl.train_buf,
            pufferl.act_sizes_puf, pufferl.losses_puf,
            hypers.clip_coef, hypers.vf_clip_coef, hypers.vf_coef, hypers.ent_coef,
            pufferl.ppo_bufs_puf, pufferl.is_continuous,
            pufferl.has_mask ? nullptr : (const float*)pufferl.ones_mask.bytes,
            pufferl.has_mask ? 0 : 0,  // stride=0: all rows read same ones
            train_stream);
        mtl_barrier((MetalStream*)train_stream); // PPO outputs -> scatter

        mtl_scatter_ppo_outputs(pufferl.train_buf, rollouts,
            (const int64_t*)pufferl.prio_bufs.idx.bytes, train_stream);
        mtl_barrier((MetalStream*)train_stream); // scatter -> next mb advantage/select

        uint64_t tp5 = mach_absolute_time();

        PufTensor grad_logits_puf = pufferl.ppo_bufs_puf.grad_logits;
        PufTensor grad_logstd_puf = pufferl.is_continuous ? pufferl.ppo_bufs_puf.grad_logstd : PufTensor();
        PufTensor grad_values_puf = pufferl.ppo_bufs_puf.grad_values;
        policy_backward(pufferl.policy, pufferl.weights_fp32, pufferl.train_activations,
            grad_logits_puf, grad_logstd_puf, grad_values_puf, train_stream);

        uint64_t tp6 = mach_absolute_time();

        PufTensor& gc_sync = pufferl.use_adam ? pufferl.adam->gc_puf : pufferl.muon->gc_puf;
        if (pufferl.grad_bf16_puf.dtype_size == 2) {
            mtl_barrier((MetalStream*)train_stream);
            mtl_cast_f16_to_f32((float*)gc_sync.bytes,
                                pufferl.grad_bf16_puf.bytes,
                                (int)pufferl.grad_bf16_puf.numel(), train_stream);
        } else {
            puf_copy(gc_sync, pufferl.grad_bf16_puf, train_stream);
        }

        uint64_t tp7 = mach_absolute_time();

        mtl_barrier((MetalStream*)train_stream); // grad_cast→clip
        {
            float* scratch = (float*)pufferl.grad_norm_puf.bytes;
            clip_grad_norm_f32(gc_sync, scratch, hypers.max_grad_norm, 1e-6f, train_stream);
        }

        uint64_t tp8 = mach_absolute_time();

        mtl_barrier((MetalStream*)train_stream); // clip→optimizer
        if (pufferl.use_adam)
            adam_step(pufferl.adam, pufferl.param_fp16_puf.bytes, train_stream);
        else
            muon_step(pufferl.muon, pufferl.param_fp16_puf.bytes, train_stream);

        // Metal 4 coherence: barrier so next minibatch's forward pass
        // sees updated weights from optimizer.
        mtl_barrier((MetalStream*)train_stream);

        uint64_t tp9 = mach_absolute_time();

        pufferl.profile.accum[PROF_TRAIN_PRIO] += prof_ms(tp0, tp2);
        pufferl.profile.accum[PROF_TRAIN_SELECT] += prof_ms(tp2, tp3);
        pufferl.profile.accum[PROF_TRAIN_FWD] += prof_ms(tp3, tp4);
        pufferl.profile.accum[PROF_TRAIN_PPO] += prof_ms(tp4, tp5);
        pufferl.profile.accum[PROF_TRAIN_BACKWARD] += prof_ms(tp5, tp6);
        pufferl.profile.accum[PROF_TRAIN_GRAD_COPY] += prof_ms(tp6, tp7);
        pufferl.profile.accum[PROF_TRAIN_GRAD_CLIP] += prof_ms(tp7, tp8);
        pufferl.profile.accum[PROF_TRAIN_MUON] += prof_ms(tp8, tp9);
        pufferl.profile.accum[PROF_TRAIN_MISC] += prof_ms(tp0, tp3);
        pufferl.profile.accum[PROF_TRAIN_FORWARD] += prof_ms(tp3, tp9);
    }

    pufferl.epoch += 1;

    uint64_t tp_sync0 = mach_absolute_time();
    puf_set_gpu_training(false);
    ensure_gpu_synced(train_stream);
    uint64_t tp_sync1 = mach_absolute_time();
    pufferl.profile.accum[PROF_TRAIN_SYNC] += prof_ms(tp_sync0, tp_sync1);

}

// Wait for async GPU training to complete, then snapshot weights for inference.
// Called at the end of rollouts() before the next iteration needs updated weights.
static void sync_pending_train(PuffeRL& pufferl) {
    if (!pufferl.train_pending) return;
    // Wait for async training on train_stream (separate queue) to complete.
    // After this, weights_infer and fused weight are up-to-date.
    MetalStream* ts = (MetalStream*)mtl_train_stream();
    ts->wait_completed();
    pufferl.train_pending = false;
}

// ============================================================================
// Initialization
// ============================================================================

std::unique_ptr<PuffeRL> create_pufferl_impl(HypersT& hypers,
        const std::string& env_name, Dict* vec_kwargs, Dict* env_kwargs) {
    auto pufferl = std::make_unique<PuffeRL>();
    pufferl->hypers = hypers;

    fprintf(stderr, "[metal] init: starting Metal backend...\n");
    // Initialize Metal backend
    mtl_init();
    fprintf(stderr, "[metal] init: Metal ready\n");

    // Seed
    pufferl->rng_seed = hypers.seed;

    fprintf(stderr, "[metal] init: creating environments (agents=%d, buffers=%d)...\n",
        hypers.total_agents, hypers.num_buffers);
    // Create environments
    StaticVec* vec = create_environments(hypers.num_buffers, hypers.total_agents,
        env_name, vec_kwargs, env_kwargs, pufferl->env);
    pufferl->vec = vec;
    fprintf(stderr, "[metal] init: environments created (vec=%p, size=%d)\n",
        (void*)vec, vec->size);

    int num_action_heads = pufferl->env.actions.shape[1];
    int* raw_act_sizes = get_act_sizes();
    int act_n = 0;
    for (int i = 0; i < num_action_heads; i++) {
        act_n += raw_act_sizes[i];
    }

    // CPU-based profiling (no CUDA events)
    memset(pufferl->profile.accum, 0, sizeof(pufferl->profile.accum));

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
    assert((num_continuous == 0 || num_discrete == 0) &&
        "Mixed continuous/discrete action spaces not supported");
    pufferl->is_continuous = (num_continuous > 0);
    if (pufferl->is_continuous) {
        printf("Detected continuous action space with %d dimensions\n", num_action_heads);
    } else {
        printf("Detected discrete action space with %d heads\n", num_action_heads);
    }

    int input_size = pufferl->env.obs.shape[1];
    int hidden_size = hypers.hidden_size;
    int num_layers = hypers.num_layers;

    // Auto-detect action masks: if obs_size > act_n, the env likely embeds
    // a mask in the last act_n columns (e.g. PVP: 373 = 334 features + 39 mask).
    // When obs_size <= act_n or there's no room, use an all-ones fallback mask.
    // Action mask: if env_config contains "mask_in_obs" > 0, the env embeds a mask
    // in the last act_n columns of observations. Otherwise use all-ones (no masking).
    {
        DictItem* mask_entry = dict_get_unsafe(env_kwargs, "mask_in_obs");
        pufferl->has_mask = (mask_entry && mask_entry->value > 0.0f);
    }
    if (!pufferl->has_mask) {
        fprintf(stderr, "[metal] init: no action mask in obs (obs=%d, act_n=%d), using all-ones\n",
            input_size, act_n);
    } else {
        fprintf(stderr, "[metal] init: action mask in obs (obs=%d, act_n=%d)\n",
            input_size, act_n);
    }

    fprintf(stderr, "[metal] init: arch=%s\n",
        hypers.arch_type == ARCH_SIMPLE ? "simple" : "rich");

    bool is_continuous = pufferl->is_continuous;
    int decoder_output_size = is_continuous ? num_action_heads : act_n;

    int minibatch_segments = hypers.minibatch_size / hypers.horizon;
    int inf_batch = vec->total_agents / hypers.num_buffers;

    // Legacy global sampling init is a no-op on Metal; rollout uses per-buffer
    // scratch buffers registered below.
    mtl_sample_logits_init(inf_batch, num_action_heads);

    // ========================================================================
    // fp32 master weights (for optimizer)
    // ========================================================================

    int esz_fp32 = sizeof(float);
    pufferl->alloc_fp32.esz = esz_fp32;
    Allocator& fp32_params = pufferl->alloc_fp32.params;

    Encoder encoder;
    Decoder decoder;
    bool simple_arch = (hypers.arch_type == ARCH_SIMPLE);
    if (simple_arch) {
        encoder = {
            .forward = simple_encoder_forward,
            .backward = simple_encoder_backward,
            .init_weights = simple_encoder_init_weights,
            .reg_params = simple_encoder_reg_params,
            .reg_train = simple_encoder_reg_train,
            .reg_rollout = simple_encoder_reg_rollout,
        };
        decoder = {
            .forward = simple_decoder_forward,
            .backward = simple_decoder_backward,
            .init_weights = simple_decoder_init_weights,
            .reg_params = simple_decoder_reg_params,
            .reg_train = simple_decoder_reg_train,
            .reg_rollout = simple_decoder_reg_rollout,
        };
    } else {
        encoder = {
            .forward = encoder_forward,
            .backward = encoder_backward,
            .init_weights = encoder_init_weights,
            .reg_params = encoder_reg_params,
            .reg_train = encoder_reg_train,
            .reg_rollout = encoder_reg_rollout,
        };
        decoder = {
            .forward = decoder_forward,
            .backward = decoder_backward,
            .init_weights = decoder_init_weights,
            .reg_params = decoder_reg_params,
            .reg_train = decoder_reg_train,
            .reg_rollout = decoder_reg_rollout,
        };
    }
    Network network = {
        .forward = mingru_forward,
        .forward_train = mingru_forward_train,
        .backward = mingru_backward,
        .init_weights = mingru_init_weights,
        .reg_params = mingru_reg_params,
        .reg_train = mingru_reg_train,
        .reg_rollout = mingru_reg_rollout,
    };

    Policy* policy = new Policy{
        .encoder = encoder, .decoder = decoder, .network = network,
        .input_dim = input_size, .hidden_dim = hidden_size, .output_dim = decoder_output_size,
        .num_atns = act_n,
    };
    pufferl->policy = policy;

    // fp32 master weights
    auto new_weights = [&](int esz) -> PolicyWeights {
        PolicyWeights w;
        if (simple_arch) {
            w.encoder = new SimpleEncoderWeights{.in_dim = input_size, .out_dim = hidden_size};
            w.decoder = new SimpleDecoderWeights{.hidden_dim = hidden_size, .output_dim = decoder_output_size, .continuous = is_continuous};
        } else {
            w.encoder = new EncoderWeights{.in_dim = input_size, .mid_dim = 2 * hidden_size, .out_dim = hidden_size};
            w.decoder = new DecoderWeights{.hidden_dim = hidden_size, .output_dim = decoder_output_size, .continuous = is_continuous};
        }
        w.network = new MinGRUWeights{.hidden = hidden_size, .num_layers = num_layers, .horizon = hypers.horizon};
        ((MinGRUWeights*)w.network)->weights.resize(num_layers);
        return w;
    };

    pufferl->weights_fp32 = new_weights(esz_fp32);
    PolicyWeights& wfp32 = pufferl->weights_fp32;
    encoder.reg_params(wfp32.encoder, &fp32_params, esz_fp32);
    decoder.reg_params(wfp32.decoder, &fp32_params, esz_fp32);
    network.reg_params(wfp32.network, &fp32_params, esz_fp32);

    pufferl->alloc_fp32.create();

    // Wrap fp32 params allocator for Metal GPU access
    mtl_wrap_allocator(&fp32_params);

    pufferl->param_fp32_puf = {.bytes = (char*)fp32_params.mem, .shape = {fp32_params.total_elems}, .dtype_size = esz_fp32};

    // Init weights on fp32 master
    {
        cudaStream_t default_stream = (cudaStream_t)mtl_stream();
        uint64_t init_seed = hypers.seed;
        encoder.init_weights(wfp32.encoder, &init_seed, default_stream);
        decoder.init_weights(wfp32.decoder, &init_seed, default_stream);
        network.init_weights(wfp32.network, &init_seed, default_stream);
        ensure_gpu_synced(default_stream);
    }

    // Fused encoder+layer0 is disabled: nonlinear 3-layer encoder can't be fused.
    // The null guard in mingru_forward falls back to encoder.forward().

    // ========================================================================
    // Double-buffered inference weights (for rollout/training overlap)
    // Same shapes as weights_fp32, separate allocator for isolation.
    // ========================================================================

    {
        Allocator& infer_alloc = pufferl->infer_params_alloc;
        pufferl->weights_infer = new_weights(esz_fp32);
        PolicyWeights& wi = pufferl->weights_infer;
        encoder.reg_params(wi.encoder, &infer_alloc, esz_fp32);
        decoder.reg_params(wi.decoder, &infer_alloc, esz_fp32);
        network.reg_params(wi.network, &infer_alloc, esz_fp32);
        infer_alloc.create();
        mtl_wrap_allocator(&infer_alloc);
        // Initial copy: weights_fp32 → weights_infer
        copy_weights_to_infer(*pufferl);
        // Fused encoder+layer0 disabled (nonlinear encoder)
    }
    pufferl->overlap_enabled = hypers.overlap;
    pufferl->use_adam = hypers.use_adam;

    // ========================================================================
    // fp16 training weights, activations, gradients
    // Separate allocators with dtype_size=2 for all training tensors.
    // Rollout stays fp32. Muon optimizer operates on fp32 master weights.
    // Boundaries: obs fp32→fp16 (before encoder), dec_out fp16→fp32 (before PPO),
    //   PPO grads fp32→fp16 (decoder backward handles this), grads fp16→fp32 (before muon),
    //   weights fp32→fp16 (after muon step).
    // ========================================================================

    int B_TT = minibatch_segments * hypers.horizon;
    int esz_fp16 = 2;

    // fp16 training weights (separate allocation from fp32 master)
    pufferl->alloc_fp16.esz = esz_fp16;
    Allocator& fp16_params = pufferl->alloc_fp16.params;
    Allocator& acts = pufferl->alloc_fp16.acts;
    Allocator& grads = pufferl->alloc_fp16.grads;

    pufferl->weights_bf16 = new_weights(esz_fp16);
    PolicyWeights& wfp16 = pufferl->weights_bf16;

    encoder.reg_params(wfp16.encoder, &fp16_params, esz_fp16);
    decoder.reg_params(wfp16.decoder, &fp16_params, esz_fp16);
    network.reg_params(wfp16.network, &fp16_params, esz_fp16);

    // Register train activations/grads. Keep fp32 training buffers on Metal:
    // fp16 accumulation over long horizons overflows easily for MinGRU/encoder grads.
    PolicyActivations& tb = pufferl->train_activations;
    if (simple_arch) {
        tb.encoder = new SimpleEncoderActivations{};
        tb.decoder = new SimpleDecoderActivations{};
    } else {
        tb.encoder = new EncoderActivations{};
        tb.decoder = new DecoderActivations{};
    }
    tb.network = new MinGRUActivations{};
    encoder.reg_train(wfp16.encoder, tb.encoder, &acts, &grads, B_TT);
    decoder.reg_train(wfp16.decoder, tb.decoder, &acts, &grads, B_TT);
    network.reg_train(wfp16.network, tb.network, &acts, &grads, B_TT);

    pufferl->alloc_fp16.create();

    // Wrap fp16 allocators for Metal GPU access
    mtl_wrap_allocator(&fp16_params);
    mtl_wrap_allocator(&acts);
    mtl_wrap_allocator(&grads);

    pufferl->param_fp16_puf = {.bytes = (char*)fp16_params.mem, .shape = {fp16_params.total_elems}, .dtype_size = esz_fp16};
    pufferl->grad_bf16_puf = {.bytes = (char*)grads.mem, .shape = {grads.total_elems}, .dtype_size = esz_fp32};

    // Cast fp32 master weights → fp16 training weights
    {
        cudaStream_t s = (cudaStream_t)mtl_stream();
        mtl_cast_f32_to_f16(pufferl->param_fp16_puf.bytes,
                            (const float*)fp32_params.mem,
                            (int)fp32_params.total_elems, s);
        ensure_gpu_synced(s);
    }

    // Boundary buffers: fp16 obs (encoder input), fp32 dec_out (PPO input),
    // fp16 state (scan initial state — mb_state is fp32 but scan reads half*)
    {
        int dec_fused = decoder_output_size + 1;
        pufferl->fp16_obs_buf = {.shape = {minibatch_segments, hypers.horizon, input_size}, .dtype_size = esz_fp16};
        pufferl->fp32_dec_out_buf = {.shape = {minibatch_segments, hypers.horizon, dec_fused}, .dtype_size = esz_fp32};
        pufferl->fp16_state_buf = {.shape = {num_layers, minibatch_segments, 1, hidden_size}, .dtype_size = esz_fp16};
        pufferl->fp16_boundary_alloc.reg(&pufferl->fp16_obs_buf);
        pufferl->fp16_boundary_alloc.reg(&pufferl->fp32_dec_out_buf);
        pufferl->fp16_boundary_alloc.reg(&pufferl->fp16_state_buf);
        pufferl->fp16_boundary_alloc.create();
        mtl_wrap_allocator(&pufferl->fp16_boundary_alloc);
    }

    // ========================================================================
    // Optimizer (Muon) — operates on fp32 master weights
    // ========================================================================

    float lr = hypers.lr;
    float beta1 = hypers.beta1;
    float beta2 = hypers.beta2;
    float eps = hypers.eps;
    pufferl->muon = new Muon{};
    pufferl->adam = new Adam{};
    int horizon = hypers.horizon;
    int total_agents = vec->total_agents;
    int batch = total_agents / hypers.num_buffers;
    int num_buffers = hypers.num_buffers;

    // ========================================================================
    // Register all init-to-close buffers into pufferl_alloc, then create once
    // ========================================================================
    Allocator& alloc = pufferl->pufferl_alloc;

    int p = PRECISION_SIZE;
    pufferl->rng_offset_puf = {.shape = {num_buffers + 1}, .dtype_size = (int)sizeof(int64_t)};
    pufferl->act_sizes_puf = {.shape = {num_action_heads}, .dtype_size = (int)sizeof(int32_t)};
    pufferl->losses_puf = {.shape = {NUM_LOSSES}, .dtype_size = (int)sizeof(float)};
    pufferl->grad_norm_puf = {.shape = {1}, .dtype_size = (int)sizeof(float)};
    alloc.reg(&pufferl->rng_offset_puf);
    alloc.reg(&pufferl->act_sizes_puf);
    alloc.reg(&pufferl->losses_puf);
    alloc.reg(&pufferl->grad_norm_puf);

    // Per-buffer RNN states
    pufferl->buffer_states.resize(num_buffers);
    pufferl->sample_act_f32_buffers.resize(num_buffers);
    for (int i = 0; i < num_buffers; i++) {
        pufferl->buffer_states[i] = {.shape = {num_layers, batch, hidden_size}, .dtype_size = p};
        alloc.reg(&pufferl->buffer_states[i]);
        pufferl->sample_act_f32_buffers[i] = {.shape = {batch, num_action_heads}, .dtype_size = (int)sizeof(float)};
        alloc.reg(&pufferl->sample_act_f32_buffers[i]);
    }

    // Rollout buffers (horizon, total_agents, ...)
    register_rollout_buffers(pufferl->rollouts, alloc, horizon, total_agents, input_size, num_action_heads);

    // Train graph buffers
    register_train_buffers(pufferl->train_buf, alloc, minibatch_segments, horizon, input_size,
        hidden_size, num_action_heads, num_layers);

    // Pre-allocated transposed rollouts for train_impl (total_agents, horizon, ...)
    register_rollout_buffers(pufferl->train_rollouts, alloc, total_agents, horizon, input_size, num_action_heads);

    // Pre-allocated train temporaries
    pufferl->old_values_puf = {.shape = {total_agents, horizon}, .dtype_size = p};
    pufferl->advantages_puf = {.shape = {total_agents, horizon}, .dtype_size = (int)sizeof(float)};
    alloc.reg(&pufferl->old_values_puf);
    alloc.reg(&pufferl->advantages_puf);

    // PPO loss buffers
    register_ppo_buffers(pufferl->ppo_bufs_puf, alloc, minibatch_segments, hypers.horizon, decoder_output_size, is_continuous);

    // Priority replay buffers
    register_prio_buffers(pufferl->prio_bufs, alloc, hypers.total_agents, minibatch_segments);

    // All-ones mask fallback for envs without embedded action masks.
    // With mask_stride=0, all rows read the same act_n floats, so we only need act_n elements.
    if (!pufferl->has_mask) {
        pufferl->ones_mask = {.shape = {act_n}, .dtype_size = (int)sizeof(float)};
        alloc.reg(&pufferl->ones_mask);
    }

    // Optimizer init (register buffers with shared allocator)
    muon_init(pufferl->muon, &fp32_params,
        pufferl->param_fp32_puf, lr, beta1, eps, 0.0, alloc);
    pufferl->muon->nccl_comm = nullptr;
    pufferl->muon->world_size = 1;
    adam_init(pufferl->adam, &fp32_params,
        pufferl->param_fp32_puf, lr, beta1, beta2, eps, 0.0, alloc);
    // Single allocation for all registered buffers
    alloc.create();

    // Wrap pufferl_alloc for Metal GPU access
    mtl_wrap_allocator(&alloc);

    // Post-create initialization: unified memory, write directly
    memset(pufferl->rng_offset_puf.bytes, 0, (num_buffers + 1) * sizeof(int64_t));
    memcpy(pufferl->act_sizes_puf.bytes, raw_act_sizes, num_action_heads * sizeof(int32_t));
    memset(pufferl->losses_puf.bytes, 0, NUM_LOSSES * sizeof(float));

    // Fill all-ones mask (after alloc.create + mtl_wrap)
    if (!pufferl->has_mask) {
        float* ones = (float*)pufferl->ones_mask.bytes;
        for (int i = 0; i < act_n; i++) ones[i] = 1.0f;
    }

    // post_create_ppo_buffers: write 1.0f to grad_loss (unified memory)
    *(float*)pufferl->ppo_bufs_puf.grad_loss.bytes = 1.0f;

    // muon_post_create: write lr and zero momentum (unified memory)
    pufferl->muon->lr_ptr = (float*)pufferl->muon->lr_puf.bytes;
    pufferl->muon->lr_derived_ptr = (float*)pufferl->muon->lr_derived_puf.bytes;
    if (pufferl->muon->ns_norm_puf.bytes)
        pufferl->muon->ns.norm_ptr = (float*)pufferl->muon->ns_norm_puf.bytes;
    *pufferl->muon->lr_ptr = pufferl->muon->lr_val_init;
    memset(pufferl->muon->lr_derived_ptr, 0, 2 * sizeof(float));
    memset(pufferl->muon->mb_puf.bytes, 0, pufferl->muon->mb_puf.numel() * sizeof(float));

    // adam_post_create
    adam_post_create(pufferl->adam);

    // Per-buffer inference activations (separate allocators)
    pufferl->buffer_activations.resize(num_buffers);
    pufferl->buffer_allocs.resize(num_buffers);
    for (int i = 0; i < num_buffers; i++) {
        PolicyActivations& rbuf = pufferl->buffer_activations[i];
        Allocator& ralloc = pufferl->buffer_allocs[i];
        if (simple_arch) {
            rbuf.encoder = new SimpleEncoderActivations{};
            rbuf.decoder = new SimpleDecoderActivations{};
        } else {
            rbuf.encoder = new EncoderActivations{};
            rbuf.decoder = new DecoderActivations{};
        }
        rbuf.network = new MinGRUActivations{};
        // Rollout uses fp32 — register with fp32 weights (dimensions only, dtype from PRECISION_SIZE)
        encoder.reg_rollout(pufferl->weights_fp32.encoder, rbuf.encoder, &ralloc, inf_batch);
        decoder.reg_rollout(pufferl->weights_fp32.decoder, rbuf.decoder, &ralloc, inf_batch);
        network.reg_rollout(pufferl->weights_fp32.network, rbuf.network, &ralloc, inf_batch);
        ralloc.create();
        // Wrap each per-buffer allocator for Metal GPU access
        mtl_wrap_allocator(&ralloc);
    }

    // No CUDA graph warmup on Metal (cudagraphs always -1)

    // Create per-buffer Metal streams for rollout callback workers.
    pufferl->rollout_streams.resize(num_buffers);
    for (int i = 0; i < num_buffers; i++) {
        cudaStream_t s = (cudaStream_t)mtl_create_stream();
        pufferl->rollout_streams[i] = s;
        vec->streams[i] = s;
    }

    // Create threads for vecenv — thread_init binds per-thread rollout stream
    create_static_threads(vec, hypers.num_threads, horizon, pufferl.get(),
        net_callback_wrapper, thread_init_metal);
    static_vec_reset(vec);

    pufferl->epoch = 0;

    return pufferl;
}

// ============================================================================
// Cleanup
// ============================================================================

void close_impl(PuffeRL& pufferl) {
    fprintf(stderr, "[metal] close: syncing GPU\n");
    sync_pending_train(pufferl);
    ensure_gpu_synced((cudaStream_t)mtl_stream());

    fprintf(stderr, "[metal] close: deleting structs\n");
    delete pufferl.muon;
    delete pufferl.adam;

    bool simple = (pufferl.hypers.arch_type == ARCH_SIMPLE);
    auto delete_weights = [simple](PolicyWeights& w) {
        if (simple) {
            delete (SimpleEncoderWeights*)w.encoder;
            delete (SimpleDecoderWeights*)w.decoder;
        } else {
            delete (EncoderWeights*)w.encoder;
            delete (DecoderWeights*)w.decoder;
        }
        delete (MinGRUWeights*)w.network;
    };
    if (simple) {
        delete (SimpleEncoderActivations*)pufferl.train_activations.encoder;
        delete (SimpleDecoderActivations*)pufferl.train_activations.decoder;
    } else {
        delete (EncoderActivations*)pufferl.train_activations.encoder;
        delete (DecoderActivations*)pufferl.train_activations.decoder;
    }
    delete (MinGRUActivations*)pufferl.train_activations.network;
    delete_weights(pufferl.weights_fp32);
    delete_weights(pufferl.weights_bf16);
    delete_weights(pufferl.weights_infer);
    for (auto& rbuf : pufferl.buffer_activations) {
        if (simple) {
            delete (SimpleEncoderActivations*)rbuf.encoder;
            delete (SimpleDecoderActivations*)rbuf.decoder;
        } else {
            delete (EncoderActivations*)rbuf.encoder;
            delete (DecoderActivations*)rbuf.decoder;
        }
        delete (MinGRUActivations*)rbuf.network;
    }
    delete pufferl.policy;

    fprintf(stderr, "[metal] close: closing vec env\n");
    static_vec_close(pufferl.vec);

    fprintf(stderr, "[metal] close: destroying rollout streams\n");
    for (cudaStream_t s : pufferl.rollout_streams) {
        mtl_destroy_stream((void*)s);
    }
    pufferl.rollout_streams.clear();

    // Release MTLBuffers BEFORE freeing the underlying memory they reference.
    // MTLBuffers created with newBufferWithBytesNoCopy need their backing pages
    // still mapped when ARC releases them (Metal unmaps the GPU address space).
    fprintf(stderr, "[metal] close: destroying Metal context\n");
    mtl_destroy();

    fprintf(stderr, "[metal] close: freeing allocators\n");
    pufferl.alloc_fp32.destroy();
    pufferl.alloc_fp16.destroy();
    pufferl.fp16_boundary_alloc.destroy();
    pufferl.infer_params_alloc.destroy();
    pufferl.pufferl_alloc.destroy();
    for (auto& a : pufferl.buffer_allocs) {
        a.destroy();
    }

    fprintf(stderr, "[metal] close: done\n");
}
