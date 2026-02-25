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
 * - No per-buffer streams; single MetalStream for all work
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

// Serializes GPU access across buffer threads in multi-buffer mode.
// Without this, concurrent net_callback calls from different buffer threads
// interleave Metal commands on the same command buffer, causing
// "commit command buffer with uncommitted encoder" assertion failures.
static std::mutex g_rollout_gpu_mutex;

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
    if (g_prof_tb.denom == 0) mach_timebase_info(&g_prof_tb);
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
    // Threading
    int num_threads;
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
    HypersT hypers;
    bool is_continuous;
    std::vector<PufTensor> buffer_states;
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
    PuffeRL* pufferl = (PuffeRL*)ctx;
    HypersT& hypers = pufferl->hypers;

    RolloutBuf& rollouts = pufferl->rollouts;
    EnvBuf& env = pufferl->env;
    int block_size = pufferl->vec->total_agents / hypers.num_buffers;
    int start = buf * block_size;
    cudaStream_t stream = (cudaStream_t)mtl_stream();

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

    // Forward pass — GPU inference via MPS GEMM + Metal MinGRU gate.
    // All tensors are in unified memory (MTLBuffer-wrapped allocators), so GPU
    // reads/writes are zero-copy. One sync after forward to make results
    // CPU-visible for action sampling.
    PufTensor dec_puf = {};
    {
        std::lock_guard<std::mutex> gpu_guard(g_rollout_gpu_mutex);
        puf_set_gpu_training(true);
        PufTensor state_puf = pufferl->buffer_states[buf];
        PolicyWeights& infer_weights = pufferl->overlap_enabled
            ? pufferl->weights_infer : pufferl->weights_fp32;
        Policy* p = pufferl->policy;
        PolicyActivations& acts = pufferl->buffer_activations[buf];

        PufTensor mingru_input = p->encoder.forward(infer_weights.encoder, acts.encoder, obs_dst, stream);
        PufTensor h = p->network.forward(infer_weights.network, mingru_input, state_puf, acts.network, stream);
        dec_puf = p->decoder.forward(infer_weights.decoder, acts.decoder, h, stream);
        ensure_gpu_synced(stream);
        puf_set_gpu_training(false);
    }

    uint64_t tp2 = mach_absolute_time();

    // Sample actions, logprobs, values — CPU, no GPU dispatch
    PufTensor act_slice = puf_slice(rollouts.actions, t, start, block_size);
    PufTensor lp_slice = puf_slice(rollouts.logprobs, t, start, block_size);
    PufTensor val_slice = puf_slice(rollouts.values, t, start, block_size);

    int num_atns = (int)pufferl->act_sizes_puf.numel();
    int fused_cols = (int)dec_puf.shape[1];

    uint32_t* buf_rng_offset = (uint32_t*)((int64_t*)pufferl->rng_offset_puf.bytes + buf);
    uint64_t buf_rng_seed = pufferl->rng_seed + buf;

    cpu_sample_logits((const float*)dec_puf.bytes, fused_cols, block_size,
        (const int32_t*)pufferl->act_sizes_puf.bytes, num_atns,
        (double*)act_slice.bytes, (float*)lp_slice.bytes, (float*)val_slice.bytes,
        buf_rng_seed, buf_rng_offset);

    uint64_t tp3 = mach_absolute_time();

    // Copy actions to env — no GPU sync needed (all CPU)
    int64_t act_cols = env.actions.shape[1];
    memcpy(
        env.actions.bytes + start * act_cols * env.actions.dtype_size,
        act_slice.bytes,
        act_slice.numel() * act_slice.dtype_size);

    uint64_t tp4 = mach_absolute_time();

    // Accumulate fine-grained rollout timing
    pufferl->profile.accum[PROF_ROLLOUT_OBS_COPY] += prof_ms(tp0, tp1);
    pufferl->profile.accum[PROF_ROLLOUT_FWD] += prof_ms(tp1, tp2);
    pufferl->profile.accum[PROF_ROLLOUT_SAMPLE] += prof_ms(tp2, tp3);
    pufferl->profile.accum[PROF_ROLLOUT_ACT_COPY] += prof_ms(tp3, tp4);
}

// ============================================================================
// Weight copy: weights_fp32 → weights_infer (for rollout/training overlap)
// ============================================================================

static void copy_weights_to_infer(PuffeRL& pufferl) {
    int64_t nbytes = pufferl.alloc_fp32.params.total_elems * sizeof(float);
    memcpy(pufferl.infer_params_alloc.mem, pufferl.alloc_fp32.params.mem, nbytes);
}

// Allocate pre-transposed weight buffers for CPU NoTrans inference.
// Called once at init. The buffers are calloc'd (outside the Allocator pool).
static void alloc_transposed_weights(PolicyWeights& w) {
    EncoderWeights *ew = (EncoderWeights *)w.encoder;
    ew->weight_t = alloc_transposed(ew->out_dim, ew->in_dim);

    DecoderWeights *dw = (DecoderWeights *)w.decoder;
    dw->weight_t = alloc_transposed(dw->output_dim + 1, dw->hidden_dim);

    MinGRUWeights *mw = (MinGRUWeights *)w.network;
    mw->weights_t.resize(mw->num_layers);
    for (int i = 0; i < mw->num_layers; i++)
        mw->weights_t[i] = alloc_transposed(3 * mw->hidden, mw->hidden);

    // Fused encoder+layer0 disabled: the fused weight is (obs_dim, 3*H) but
    // mingru_forward receives encoder OUTPUT (B, H), not raw obs (B, obs_dim).
    // To enable, encoder_forward must pass through raw obs when fused is active.
    mw->fused_obs_dim = ew->in_dim;
    mw->fused_enc_layer0 = {};
}

// Transpose current fp32 weights into the pre-transposed buffers.
// Also compute fused encoder+layer0 weight.
// Called after weight init and before each rollout (after training updates).
static void sync_transposed_weights(PolicyWeights& w) {
    EncoderWeights *ew = (EncoderWeights *)w.encoder;
    transpose_weight(ew->weight_t, ew->weight);

    DecoderWeights *dw = (DecoderWeights *)w.decoder;
    transpose_weight(dw->weight_t, dw->weight);

    MinGRUWeights *mw = (MinGRUWeights *)w.network;
    for (int i = 0; i < mw->num_layers; i++)
        transpose_weight(mw->weights_t[i], mw->weights[i]);

    // Compute fused weight: enc_weight_t(obs_dim, H) × mingru0_weight_t(H, 3*H)
    // Result is (obs_dim, 3*H) stored row-major.
    if (mw->fused_enc_layer0.bytes) {
        int obs_dim = mw->fused_obs_dim;
        int H = mw->hidden;
        int N3H = 3 * H;
        vDSP_mmul((const float *)ew->weight_t.bytes, 1,
                  (const float *)mw->weights_t[0].bytes, 1,
                  (float *)mw->fused_enc_layer0.bytes, 1,
                  obs_dim, N3H, H);
    }
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

    cudaStream_t train_stream = (cudaStream_t)mtl_stream();

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
    PufTensor& old_values_puf = pufferl.old_values_puf;
    puf_copy(old_values_puf, rollouts.values, train_stream);

    // Zero pre-allocated advantages buffer
    PufTensor& advantages_puf = pufferl.advantages_puf;

    int minibatch_size = hypers.minibatch_size;
    int batch_size = hypers.total_agents * hypers.horizon;
    int minibatch_segments = minibatch_size / hypers.horizon;
    float prio_beta0 = hypers.prio_beta0;
    float prio_alpha = hypers.prio_alpha;
    bool anneal_lr = hypers.anneal_lr;
    int current_epoch = pufferl.epoch;

    Muon* muon = pufferl.muon;
    int total_epochs = hypers.total_timesteps / batch_size;

    if (anneal_lr) {
        float lr_min = hypers.min_lr_ratio * hypers.lr;
        float lr = cosine_annealing(hypers.lr, lr_min, current_epoch, total_epochs);
        *muon->lr_ptr = lr;
    }

    // Annealed priority exponent
    float anneal_beta = prio_beta0 + (1.0f - prio_beta0) * prio_alpha
        * (float)current_epoch / (float)total_epochs;

    int total_minibatches = hypers.replay_ratio * batch_size / hypers.minibatch_size;

    TrainGraph& graph = pufferl.train_buf;

    uint64_t tp_preloop1 = mach_absolute_time();

    // Advantage + prio precompute: hoisted outside the minibatch loop.
    // Advantages depend only on rollout data (values, rewards, terminals, ratio)
    // which doesn't change between minibatches. Computing once + single GPU sync
    // eliminates per-minibatch syncs that were the dominant training cost.
    puf_zero(advantages_puf, train_stream);
    puff_advantage_cuda(rollouts.values, rollouts.rewards, rollouts.terminals,
        rollouts.ratio, advantages_puf, hypers.gamma, hypers.gae_lambda,
        hypers.vtrace_rho_clip, hypers.vtrace_c_clip, train_stream);
    prio_precompute(advantages_puf, prio_alpha, pufferl.prio_bufs, train_stream);

    uint64_t tp_prio_done = mach_absolute_time();
    pufferl.profile.accum[PROF_TRAIN_PRELOOP] += prof_ms(tp_preloop0, tp_preloop1);
    pufferl.profile.accum[PROF_TRAIN_ADVANTAGE] += prof_ms(tp_preloop1, tp_prio_done);

    uint32_t* train_rng_offset = (uint32_t*)((int64_t*)pufferl.rng_offset_puf.bytes + hypers.num_buffers);

    for (int mb = 0; mb < total_minibatches; ++mb) {
        uint64_t tp0 = mach_absolute_time();

        // Sample from cached CDF + dispatch GPU importance weights (no sync)
        prio_sample(minibatch_segments, hypers.total_agents, anneal_beta,
            pufferl.prio_bufs, pufferl.rng_seed, train_rng_offset, train_stream);

        uint64_t tp2 = mach_absolute_time();

        // Select and copy minibatch data
        puf_zero(graph.mb_state, train_stream);
        {
            RolloutBuf sel_src = rollouts;
            sel_src.values = old_values_puf;
            mtl_select_copy(sel_src, graph,
                (const int64_t*)pufferl.prio_bufs.idx.bytes,
                (const float*)advantages_puf.bytes,
                (const float*)pufferl.prio_bufs.mb_prio.bytes,
                minibatch_segments, train_stream);
        }

        uint64_t tp3 = mach_absolute_time();

        // Cast fp32 observations → fp16 for encoder input
        mtl_cast_f32_to_f16(pufferl.fp16_obs_buf.bytes,
                            (const float*)graph.mb_obs.bytes,
                            (int)graph.mb_obs.numel(), train_stream);

        // Zero fp16 state buffer (scan initial state, zeroed each minibatch)
        puf_zero(pufferl.fp16_state_buf, train_stream);

        // Forward pass (all fp16: weights, activations, scan)
        PufTensor obs_puf = pufferl.fp16_obs_buf;
        PufTensor state_puf = pufferl.fp16_state_buf;
        PufTensor dec_puf = policy_forward_train(pufferl.policy, pufferl.weights_bf16,
            pufferl.train_activations, obs_puf, state_puf, train_stream);

        uint64_t tp4 = mach_absolute_time();

        // Cast fp16 decoder output → fp32 for PPO kernel
        mtl_cast_f16_to_f32((float*)pufferl.fp32_dec_out_buf.bytes,
                            dec_puf.bytes,
                            (int)dec_puf.numel(), train_stream);
        PufTensor dec_puf_f32 = pufferl.fp32_dec_out_buf;

        // PPO loss (operates on fp32 decoder output)
        DecoderWeights* dw_train = (DecoderWeights*)pufferl.weights_bf16.decoder;
        PufTensor p_logstd;
        if (dw_train->continuous) {
            p_logstd = dw_train->logstd;
        }

        ppo_loss_fwd_bwd(dec_puf_f32, p_logstd, graph,
            pufferl.act_sizes_puf, pufferl.losses_puf,
            hypers.clip_coef, hypers.vf_clip_coef, hypers.vf_coef, hypers.ent_coef,
            pufferl.ppo_bufs_puf, pufferl.is_continuous, train_stream);

        uint64_t tp5 = mach_absolute_time();

        // Backward pass (fp16 activations/weights, PPO grads are fp32 —
        // decoder_backward handles the f32→f16 boundary via assemble kernel)
        PufTensor grad_logits_puf = pufferl.ppo_bufs_puf.grad_logits;
        PufTensor grad_logstd_puf = pufferl.is_continuous ? pufferl.ppo_bufs_puf.grad_logstd : PufTensor();
        PufTensor grad_values_puf = pufferl.ppo_bufs_puf.grad_values;
        policy_backward(pufferl.policy, pufferl.weights_bf16, pufferl.train_activations,
            grad_logits_puf, grad_logstd_puf, grad_values_puf, train_stream);

        uint64_t tp6 = mach_absolute_time();

        // Cast fp16 grads → fp32 for muon optimizer
        mtl_cast_f16_to_f32((float*)pufferl.muon->gc_puf.bytes,
                            pufferl.grad_bf16_puf.bytes,
                            (int)pufferl.grad_bf16_puf.numel(), train_stream);

        uint64_t tp7 = mach_absolute_time();

        // Clip grad norm (fp32)
        {
            PufTensor& grad = pufferl.muon->gc_puf;
            float* scratch = (float*)pufferl.grad_norm_puf.bytes;
            clip_grad_norm_f32(grad, scratch, hypers.max_grad_norm, 1e-6f, train_stream);
        }

        uint64_t tp8 = mach_absolute_time();

        muon_step(pufferl.muon, train_stream);

        // Cast fp32 master weights → fp16 training weights (after optimizer)
        mtl_cast_f32_to_f16(pufferl.param_fp16_puf.bytes,
                            (const float*)pufferl.param_fp32_puf.bytes,
                            (int)pufferl.param_fp32_puf.numel(), train_stream);

        uint64_t tp9 = mach_absolute_time();

        // Accumulate fine-grained training timing (every minibatch)
        // NOTE: PROF_TRAIN_ADVANTAGE is tracked in the hoisted pre-loop section
        pufferl.profile.accum[PROF_TRAIN_PRIO] += prof_ms(tp0, tp2);
        pufferl.profile.accum[PROF_TRAIN_SELECT] += prof_ms(tp2, tp3);
        pufferl.profile.accum[PROF_TRAIN_FWD] += prof_ms(tp3, tp4);
        pufferl.profile.accum[PROF_TRAIN_PPO] += prof_ms(tp4, tp5);
        pufferl.profile.accum[PROF_TRAIN_BACKWARD] += prof_ms(tp5, tp6);
        pufferl.profile.accum[PROF_TRAIN_GRAD_COPY] += prof_ms(tp6, tp7);
        pufferl.profile.accum[PROF_TRAIN_GRAD_CLIP] += prof_ms(tp7, tp8);
        pufferl.profile.accum[PROF_TRAIN_MUON] += prof_ms(tp8, tp9);

        // Legacy coarse-grained accumulators (backward compat)
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

// Sync pending GPU training and copy weights to inference buffer.
// Called at the start of rollouts when overlap is enabled.
static void sync_pending_train(PuffeRL& pufferl) {
    if (!pufferl.train_pending) return;
    ensure_gpu_synced((cudaStream_t)mtl_stream());
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
    int seed = 42;
    pufferl->rng_seed = seed;

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

    bool is_continuous = pufferl->is_continuous;
    int decoder_output_size = is_continuous ? num_action_heads : act_n;

    int minibatch_segments = hypers.minibatch_size / hypers.horizon;
    int inf_batch = vec->total_agents / hypers.num_buffers;

    // ========================================================================
    // fp32 master weights (for optimizer)
    // ========================================================================

    int esz_fp32 = sizeof(float);
    pufferl->alloc_fp32.esz = esz_fp32;
    Allocator& fp32_params = pufferl->alloc_fp32.params;

    Encoder encoder = {
        .forward = encoder_forward,
        .backward = encoder_backward,
        .init_weights = encoder_init_weights,
        .reg_params = encoder_reg_params,
        .reg_train = encoder_reg_train,
        .reg_rollout = encoder_reg_rollout,
    };
    Decoder decoder = {
        .forward = decoder_forward,
        .backward = decoder_backward,
        .init_weights = decoder_init_weights,
        .reg_params = decoder_reg_params,
        .reg_train = decoder_reg_train,
        .reg_rollout = decoder_reg_rollout,
    };
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
        w.encoder = new EncoderWeights{.in_dim = input_size, .out_dim = hidden_size};
        w.decoder = new DecoderWeights{.hidden_dim = hidden_size, .output_dim = decoder_output_size, .continuous = is_continuous};
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
        uint64_t seed = 42;
        encoder.init_weights(wfp32.encoder, &seed, default_stream);
        decoder.init_weights(wfp32.decoder, &seed, default_stream);
        network.init_weights(wfp32.network, &seed, default_stream);
    }

    // Pre-transposed weight buffers for CPU NoTrans GEMM during inference.
    alloc_transposed_weights(wfp32);
    sync_transposed_weights(wfp32);

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
    }
    // Overlap disabled: unified memory contention doubles rollout fwd time
    // (25ms→52ms), negating overlap savings. Fundamental M-series constraint.
    pufferl->overlap_enabled = false;

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

    // Register train activations/grads with fp16 allocators.
    // reg_train uses PRECISION_SIZE (4) for dtype_size, so we fix them up after.
    PolicyActivations& tb = pufferl->train_activations;
    tb.encoder = new EncoderActivations{};
    tb.decoder = new DecoderActivations{};
    tb.network = new MinGRUActivations{};
    encoder.reg_train(wfp16.encoder, tb.encoder, &acts, &grads, B_TT);
    decoder.reg_train(wfp16.decoder, tb.decoder, &acts, &grads, B_TT);
    network.reg_train(wfp16.network, tb.network, &acts, &grads, B_TT);

    // Fix up dtype_size: reg_train sets PRECISION_SIZE (4), we need fp16 (2).
    // Scan internal buffers (a_star, s_vals, log_values_buf) must stay fp32
    // for numerical stability in the parallel scan accumulation.
    for (auto *t : acts.regs) {
        if (t->dtype_size == PRECISION_SIZE) t->dtype_size = esz_fp16;
    }
    for (auto *t : grads.regs) {
        if (t->dtype_size == PRECISION_SIZE) t->dtype_size = esz_fp16;
    }
    // Restore fp32 on scan internal buffers (blanket fixup incorrectly changed them
    // because sizeof(float) == PRECISION_SIZE on Metal)
    {
        MinGRUActivations *ma = (MinGRUActivations *)tb.network;
        for (int i = 0; i < num_layers; i++) {
            ma->scan_bufs[i].a_star.dtype_size = esz_fp32;
            ma->scan_bufs[i].s_vals.dtype_size = esz_fp32;
            ma->scan_bufs[i].log_values_buf.dtype_size = esz_fp32;
        }
    }

    pufferl->alloc_fp16.create();

    // Wrap fp16 allocators for Metal GPU access
    mtl_wrap_allocator(&fp16_params);
    mtl_wrap_allocator(&acts);
    mtl_wrap_allocator(&grads);

    pufferl->param_fp16_puf = {.bytes = (char*)fp16_params.mem, .shape = {fp16_params.total_elems}, .dtype_size = esz_fp16};
    pufferl->grad_bf16_puf = {.bytes = (char*)grads.mem, .shape = {grads.total_elems}, .dtype_size = esz_fp16};

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
    float eps = hypers.eps;
    pufferl->muon = new Muon{};
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
    for (int i = 0; i < num_buffers; i++) {
        pufferl->buffer_states[i] = {.shape = {num_layers, batch, hidden_size}, .dtype_size = p};
        alloc.reg(&pufferl->buffer_states[i]);
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

    // Muon optimizer (init + register buffers)
    muon_init(pufferl->muon, &fp32_params,
        pufferl->param_fp32_puf, lr, beta1, eps, 0.0, alloc);
    pufferl->muon->nccl_comm = nullptr;
    pufferl->muon->world_size = 1;
    // Single allocation for all registered buffers
    alloc.create();

    // Wrap pufferl_alloc for Metal GPU access
    mtl_wrap_allocator(&alloc);

    // Post-create initialization: unified memory, write directly
    memset(pufferl->rng_offset_puf.bytes, 0, (num_buffers + 1) * sizeof(int64_t));
    memcpy(pufferl->act_sizes_puf.bytes, raw_act_sizes, num_action_heads * sizeof(int32_t));
    memset(pufferl->losses_puf.bytes, 0, NUM_LOSSES * sizeof(float));

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

    // Per-buffer inference activations (separate allocators)
    pufferl->buffer_activations.resize(num_buffers);
    pufferl->buffer_allocs.resize(num_buffers);
    for (int i = 0; i < num_buffers; i++) {
        PolicyActivations& rbuf = pufferl->buffer_activations[i];
        Allocator& ralloc = pufferl->buffer_allocs[i];
        rbuf.encoder = new EncoderActivations{};
        rbuf.decoder = new DecoderActivations{};
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

    // Create threads for vecenv — thread_init sets BLAS single-threading
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
    // Sync any pending GPU work
    sync_pending_train(pufferl);
    ensure_gpu_synced((cudaStream_t)mtl_stream());

    delete pufferl.muon;

    auto delete_weights = [](PolicyWeights& w) {
        delete (EncoderWeights*)w.encoder;
        delete (DecoderWeights*)w.decoder;
        delete (MinGRUWeights*)w.network;
    };
    delete (EncoderActivations*)pufferl.train_activations.encoder;
    delete (DecoderActivations*)pufferl.train_activations.decoder;
    delete (MinGRUActivations*)pufferl.train_activations.network;
    delete_weights(pufferl.weights_fp32);
    delete_weights(pufferl.weights_bf16);  // separate fp16 weights
    delete_weights(pufferl.weights_infer);
    for (auto& rbuf : pufferl.buffer_activations) {
        delete (EncoderActivations*)rbuf.encoder;
        delete (DecoderActivations*)rbuf.decoder;
        delete (MinGRUActivations*)rbuf.network;
    }
    delete pufferl.policy;

    pufferl.alloc_fp32.destroy();
    pufferl.alloc_fp16.destroy();
    pufferl.fp16_boundary_alloc.destroy();
    pufferl.infer_params_alloc.destroy();
    pufferl.pufferl_alloc.destroy();
    for (auto& a : pufferl.buffer_allocs) {
        a.destroy();
    }

    static_vec_close(pufferl.vec);

    mtl_destroy();
}
