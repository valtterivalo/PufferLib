/**
 * @fileoverview Python bindings for PufferLib Metal backend (torch-free).
 *
 * Port of bindings.cu — same pybind11 interface, no CUDA/NCCL/nvml
 * dependencies. Unified memory means no cudaMemcpy for host<->device;
 * we read/write directly.
 */

#include "metal_pufferlib.mm"

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <cmath>
#include <mach/mach.h>

namespace py = pybind11;

struct FloatStats {
    double mean = 0.0;
    double stddev = 0.0;
    double abs_mean = 0.0;
    double min = 0.0;
    double max = 0.0;
};

static FloatStats compute_float_stats(const float* data, int64_t count) {
    FloatStats stats;
    if (data == nullptr || count <= 0) return stats;

    double sum = 0.0;
    double sum_sq = 0.0;
    double sum_abs = 0.0;
    float min_v = data[0];
    float max_v = data[0];
    for (int64_t i = 0; i < count; i++) {
        float v = data[i];
        sum += v;
        sum_sq += (double)v * (double)v;
        sum_abs += std::fabs((double)v);
        if (v < min_v) min_v = v;
        if (v > max_v) max_v = v;
    }

    double n = (double)count;
    double mean = sum / n;
    double var = (sum_sq / n) - (mean * mean);
    if (var < 0.0) var = 0.0;

    stats.mean = mean;
    stats.stddev = std::sqrt(var);
    stats.abs_mean = sum_abs / n;
    stats.min = min_v;
    stats.max = max_v;
    return stats;
}

static double compute_clipfrac(const float* data, int64_t count, double clip_coef) {
    if (data == nullptr || count <= 0) return 0.0;
    int64_t clipped = 0;
    for (int64_t i = 0; i < count; i++) {
        if (std::fabs((double)data[i] - 1.0) > clip_coef) clipped++;
    }
    return (double)clipped / (double)count;
}

static double compute_frac_gt(const float* data, int64_t count, double threshold) {
    if (data == nullptr || count <= 0) return 0.0;
    int64_t matched = 0;
    for (int64_t i = 0; i < count; i++) {
        if ((double)data[i] > threshold) matched++;
    }
    return (double)matched / (double)count;
}

static double compute_frac_lt(const float* data, int64_t count, double threshold) {
    if (data == nullptr || count <= 0) return 0.0;
    int64_t matched = 0;
    for (int64_t i = 0; i < count; i++) {
        if ((double)data[i] < threshold) matched++;
    }
    return (double)matched / (double)count;
}

static double compute_frac_abs_gt(const float* data, int64_t count, double threshold) {
    if (data == nullptr || count <= 0) return 0.0;
    int64_t matched = 0;
    for (int64_t i = 0; i < count; i++) {
        if (std::fabs((double)data[i]) > threshold) matched++;
    }
    return (double)matched / (double)count;
}

struct DistributionStats {
    double sum = 0.0;
    double min = 0.0;
    double max = 0.0;
    double entropy = 0.0;
    double ess = 0.0;
    double ess_fraction = 0.0;
};

static DistributionStats compute_distribution_stats(const float* data, int64_t count) {
    DistributionStats stats;
    if (data == nullptr || count <= 0) return stats;

    double sum = 0.0;
    double sum_sq = 0.0;
    double entropy = 0.0;
    float min_v = data[0];
    float max_v = data[0];
    for (int64_t i = 0; i < count; i++) {
        float v = data[i];
        if (v < min_v) min_v = v;
        if (v > max_v) max_v = v;
        if (v <= 0.0f) continue;
        double vd = (double)v;
        sum += vd;
        sum_sq += vd * vd;
        entropy -= vd * std::log(vd);
    }

    stats.sum = sum;
    stats.min = min_v;
    stats.max = max_v;
    stats.entropy = entropy;
    if (sum_sq > 0.0) {
        stats.ess = (sum * sum) / sum_sq;
        stats.ess_fraction = stats.ess / (double)count;
    }
    return stats;
}

// PFSP C functions from binding.c (linked via static env lib).
// Weak defaults for envs that don't implement PFSP (breakout, g2048, etc.).
extern "C" {
    void binding_set_pfsp_weights(StaticVec* vec, int* pool, int* cum_weights, int pool_size)
        __attribute__((weak));
    void binding_get_pfsp_stats(StaticVec* vec, float* out_wins, float* out_episodes, int* out_pool_size)
        __attribute__((weak));
}

void binding_set_pfsp_weights(StaticVec*, int*, int*, int) {}
void binding_get_pfsp_stats(StaticVec*, float*, float*, int* out_pool_size) {
    if (out_pool_size) *out_pool_size = 0;
}

// ============================================================================
// Wrapper functions
// ============================================================================

pybind11::dict log_environments(pybind11::object pufferl_obj) {
    auto& pufferl = pufferl_obj.cast<PuffeRL&>();
    Dict* out = log_environments_impl(pufferl);
    pybind11::dict py_out;
    for (int i = 0; i < out->size; i++) {
        py_out[out->items[i].key] = out->items[i].value;
    }
    return py_out;
}

void python_vec_recv(pybind11::object /*pufferl_obj*/, int /*buf*/) {
    // Not used in static/OMP path
}

void python_vec_send(pybind11::object /*pufferl_obj*/, int /*buf*/) {
    // Not used in static/OMP path
}

void render(pybind11::object pufferl_obj, int env_id) {
    PuffeRL& pufferl = pufferl_obj.cast<PuffeRL&>();
    static_vec_render(pufferl.vec, env_id);
}

void rollouts(pybind11::object pufferl_obj) {
    PuffeRL& pufferl = pufferl_obj.cast<PuffeRL&>();
    // Reset sync stats before rollout to capture rollout-only syncs
    { int _c; double _m; mtl_sync_stats(&_c, &_m); }
    pybind11::gil_scoped_release no_gil;
    // Sync async training from previous iteration (if any).
    if (pufferl.train_pending) {
        auto wait_start = std::chrono::high_resolution_clock::now();
        sync_pending_train(pufferl);
        float wait_ms = std::chrono::duration<float, std::milli>(
            std::chrono::high_resolution_clock::now() - wait_start).count();
        pufferl.profile.accum[PROF_TRAIN_SYNC] += wait_ms;
    }

    auto t0 = std::chrono::high_resolution_clock::now();
    puf_set_gpu_training(true);
    static_vec_omp_step(pufferl.vec);
    puf_set_gpu_training(false);
    float sec = std::chrono::duration<float>(
        std::chrono::high_resolution_clock::now() - t0).count();
    pufferl.profile.accum[PROF_ROLLOUT] += sec * 1000.0f;

    float eval_prof[NUM_EVAL_PROF];
    static_vec_read_profile(pufferl.vec, eval_prof);
    pufferl.profile.accum[PROF_EVAL_GPU] += eval_prof[EVAL_GPU];
    pufferl.profile.accum[PROF_EVAL_ENV] += eval_prof[EVAL_ENV_STEP];

    // Capture rollout sync stats
    mtl_sync_stats(&pufferl.rollout_sync_count, &pufferl.rollout_sync_ms);
}

pybind11::dict train(pybind11::object pufferl_obj) {
    PuffeRL& pufferl = pufferl_obj.cast<PuffeRL&>();
    // Reset sync stats before train to capture train-only syncs
    { int _c; double _m; mtl_sync_stats(&_c, &_m); }
    {
        pybind11::gil_scoped_release no_gil;
        train_impl(pufferl);
    }
    // Capture train sync stats
    mtl_sync_stats(&pufferl.train_sync_count, &pufferl.train_sync_ms);
    pybind11::dict losses;
    return losses;
}

pybind11::dict log_losses(pybind11::object pufferl_obj) {
    auto& pufferl = pufferl_obj.cast<PuffeRL&>();
    // Sync pending training — losses are written by GPU training
    sync_pending_train(pufferl);
    ensure_gpu_synced((cudaStream_t)mtl_stream());
    float* losses_host = (float*)pufferl.losses_puf.bytes;
    float n = losses_host[LOSS_N];
    pybind11::dict result;
    if (n > 0) {
        float inv_n = 1.0f / n;
        result["pg_loss"] = losses_host[LOSS_PG] * inv_n;
        result["vf_loss"] = losses_host[LOSS_VF] * inv_n;
        result["entropy"] = losses_host[LOSS_ENT] * inv_n;
        result["total_loss"] = losses_host[LOSS_TOTAL] * inv_n;
        result["old_approx_kl"] = losses_host[LOSS_OLD_APPROX_KL] * inv_n;
        result["approx_kl"] = losses_host[LOSS_APPROX_KL] * inv_n;
        result["clipfrac"] = losses_host[LOSS_CLIPFRAC] * inv_n;
    }
    cudaStream_t loss_stream = pufferl.overlap_enabled
        ? (cudaStream_t)mtl_train_stream()
        : (cudaStream_t)mtl_stream();
    mtl_fill_f32(losses_host, 0.0f, (int)pufferl.losses_puf.numel(), loss_stream);
    return result;
}

pybind11::dict log_profile(pybind11::object pufferl_obj) {
    auto& pufferl = pufferl_obj.cast<PuffeRL&>();
    pybind11::dict result;
    // Export all profile accumulators (ms -> seconds for Python)
    for (int i = 0; i < NUM_PROF; i++) {
        result[PROF_NAMES[i]] = pufferl.profile.accum[i] / 1000.0f;
    }
    // Train total from fine-grained phases (ms)
    float* a = pufferl.profile.accum;
    float train_ms = a[PROF_TRAIN_PRELOOP] + a[PROF_TRAIN_SYNC];
    for (int i = PROF_TRAIN_ADVANTAGE; i <= PROF_TRAIN_MUON; i++) train_ms += a[i];
    result["train"] = train_ms / 1000.0f;

    // Sync stats
    int r_sync = pufferl.rollout_sync_count;
    double r_sync_ms = pufferl.rollout_sync_ms;
    int t_sync = pufferl.train_sync_count;
    double t_sync_ms = pufferl.train_sync_ms;
    result["rollout_syncs"] = r_sync;
    result["rollout_sync_ms"] = r_sync_ms;
    result["train_syncs"] = t_sync;
    result["train_sync_ms"] = t_sync_ms;

    // Compact profile report to stderr
    fprintf(stderr,
        "[metal-prof] rollout: %d syncs (%.1fms), "
        "obs=%.1fms fwd=%.1fms sample=%.1fms act=%.1fms | "
        "env=%.1fms total=%.1fms\n",
        r_sync, r_sync_ms,
        a[PROF_ROLLOUT_OBS_COPY], a[PROF_ROLLOUT_FWD],
        a[PROF_ROLLOUT_SAMPLE], a[PROF_ROLLOUT_ACT_COPY],
        a[PROF_EVAL_ENV], a[PROF_ROLLOUT]);
    fprintf(stderr,
        "[metal-prof] train:  %d syncs (%.1fms), "
        "pre=%.1fms adv=%.1fms prio=%.1fms sel=%.1fms "
        "fwd=%.1fms ppo=%.1fms bwd=%.1fms gc=%.1fms clip=%.1fms muon=%.1fms sync=%.1fms\n",
        t_sync, t_sync_ms,
        a[PROF_TRAIN_PRELOOP], a[PROF_TRAIN_ADVANTAGE],
        a[PROF_TRAIN_PRIO], a[PROF_TRAIN_SELECT],
        a[PROF_TRAIN_FWD], a[PROF_TRAIN_PPO],
        a[PROF_TRAIN_BACKWARD], a[PROF_TRAIN_GRAD_COPY],
        a[PROF_TRAIN_GRAD_CLIP], a[PROF_TRAIN_MUON], a[PROF_TRAIN_SYNC]);
    // GPU timing diagnostic: actual kernel execution vs scheduling delay
    double gpu_exec_ms, sched_wait_ms;
    mtl_gpu_timing_stats(&gpu_exec_ms, &sched_wait_ms);
    result["gpu_exec"] = gpu_exec_ms / 1000.0;
    result["sched_wait"] = sched_wait_ms / 1000.0;

    int gemm_tensor_ops, gemm_mps;
    mtl_gemm_stats(&gemm_tensor_ops, &gemm_mps);
    result["gemm_tensor_ops"] = gemm_tensor_ops;
    result["gemm_mps"] = gemm_mps;

    fprintf(stderr,
        "[metal-prof] total: %d syncs, %.1fms sync, %.1fms rollout + %.1fms train | "
        "gpu_exec=%.1fms sched_wait=%.1fms | gemm: tensor_ops=%d mps=%d\n",
        r_sync + t_sync, r_sync_ms + t_sync_ms,
        a[PROF_ROLLOUT], train_ms, gpu_exec_ms, sched_wait_ms,
        gemm_tensor_ops, gemm_mps);

    memset(pufferl.profile.accum, 0, sizeof(pufferl.profile.accum));
    pufferl.rollout_sync_count = 0;
    pufferl.rollout_sync_ms = 0;
    pufferl.train_sync_count = 0;
    pufferl.train_sync_ms = 0;
    return result;
}

pybind11::dict log_train_debug(pybind11::object pufferl_obj) {
    auto& pufferl = pufferl_obj.cast<PuffeRL&>();
    sync_pending_train(pufferl);
    ensure_gpu_synced((cudaStream_t)mtl_stream());

    const float* mb_adv = (const float*)pufferl.train_buf.mb_advantages.bytes;
    int64_t mb_adv_n = pufferl.train_buf.mb_advantages.numel();
    const float* mb_prio = (const float*)pufferl.train_buf.mb_prio.bytes;
    int64_t mb_prio_n = pufferl.train_buf.mb_prio.numel();
    const float* mb_ratio = (const float*)pufferl.train_buf.mb_ratio.bytes;
    int64_t mb_ratio_n = pufferl.train_buf.mb_ratio.numel();
    const float* roll_ratio = (const float*)pufferl.train_rollouts.ratio.bytes;
    int64_t roll_ratio_n = pufferl.train_rollouts.ratio.numel();
    const float* param_fp32 = (const float*)pufferl.param_fp32_puf.bytes;
    int64_t param_fp32_n = pufferl.param_fp32_puf.numel();
    const float* prio_probs = (const float*)pufferl.prio_bufs.prio_probs.bytes;
    int64_t prio_probs_n = pufferl.prio_bufs.prio_probs.numel();
    const float* sampled_prio = (const float*)pufferl.prio_bufs.mb_prio.bytes;
    int64_t sampled_prio_n = pufferl.prio_bufs.mb_prio.numel();
    const float* grad_sum_sq = (const float*)pufferl.grad_norm_puf.bytes;

    FloatStats adv_stats = compute_float_stats(mb_adv, mb_adv_n);
    FloatStats prio_stats = compute_float_stats(mb_prio, mb_prio_n);
    FloatStats ratio_stats = compute_float_stats(mb_ratio, mb_ratio_n);
    FloatStats roll_ratio_stats = compute_float_stats(roll_ratio, roll_ratio_n);
    FloatStats sampled_prio_stats = compute_float_stats(sampled_prio, sampled_prio_n);
    FloatStats param_stats = compute_float_stats(param_fp32, param_fp32_n);
    DistributionStats prio_prob_stats = compute_distribution_stats(prio_probs, prio_probs_n);
    double grad_l2 = 0.0;
    if (grad_sum_sq != nullptr) grad_l2 = std::sqrt(std::max(0.0, (double)grad_sum_sq[0]));
    double grad_clip_coef = std::min(
        (double)pufferl.hypers.max_grad_norm / (grad_l2 + 1e-6), 1.0);
    double param_abs_max = std::max(std::fabs(param_stats.min), std::fabs(param_stats.max));

    pybind11::dict out;
    out["mb_adv_mean"] = adv_stats.mean;
    out["mb_adv_std"] = adv_stats.stddev;
    out["mb_adv_abs_mean"] = adv_stats.abs_mean;
    out["mb_adv_min"] = adv_stats.min;
    out["mb_adv_max"] = adv_stats.max;

    out["mb_prio_mean"] = prio_stats.mean;
    out["mb_prio_std"] = prio_stats.stddev;
    out["mb_prio_abs_mean"] = prio_stats.abs_mean;
    out["mb_prio_min"] = prio_stats.min;
    out["mb_prio_max"] = prio_stats.max;
    out["sampled_prio_mean"] = sampled_prio_stats.mean;
    out["sampled_prio_std"] = sampled_prio_stats.stddev;
    out["sampled_prio_min"] = sampled_prio_stats.min;
    out["sampled_prio_max"] = sampled_prio_stats.max;

    out["mb_ratio_mean"] = ratio_stats.mean;
    out["mb_ratio_std"] = ratio_stats.stddev;
    out["mb_ratio_abs_mean"] = ratio_stats.abs_mean;
    out["mb_ratio_min"] = ratio_stats.min;
    out["mb_ratio_max"] = ratio_stats.max;
    out["mb_ratio_clipfrac_raw"] = compute_clipfrac(
        mb_ratio, mb_ratio_n, pufferl.hypers.clip_coef);
    out["mb_ratio_gt_2_frac"] = compute_frac_gt(mb_ratio, mb_ratio_n, 2.0);
    out["mb_ratio_lt_0_5_frac"] = compute_frac_lt(mb_ratio, mb_ratio_n, 0.5);

    out["roll_ratio_mean"] = roll_ratio_stats.mean;
    out["roll_ratio_std"] = roll_ratio_stats.stddev;
    out["roll_ratio_abs_mean"] = roll_ratio_stats.abs_mean;
    out["roll_ratio_min"] = roll_ratio_stats.min;
    out["roll_ratio_max"] = roll_ratio_stats.max;
    out["roll_ratio_clipfrac_raw"] = compute_clipfrac(
        roll_ratio, roll_ratio_n, pufferl.hypers.clip_coef);
    out["prio_prob_sum"] = prio_prob_stats.sum;
    out["prio_prob_min"] = prio_prob_stats.min;
    out["prio_prob_max"] = prio_prob_stats.max;
    out["prio_prob_entropy"] = prio_prob_stats.entropy;
    out["prio_prob_ess"] = prio_prob_stats.ess;
    out["prio_prob_ess_frac"] = prio_prob_stats.ess_fraction;
    out["param_abs_max"] = param_abs_max;
    out["param_abs_gt_100"] = compute_frac_abs_gt(param_fp32, param_fp32_n, 100.0);
    out["param_abs_gt_1k"] = compute_frac_abs_gt(param_fp32, param_fp32_n, 1000.0);
    out["param_abs_gt_60k"] = compute_frac_abs_gt(param_fp32, param_fp32_n, 60000.0);
    out["grad_l2"] = grad_l2;
    out["grad_clip_coef"] = grad_clip_coef;

    float current_lr = *pufferl.muon->lr_ptr;
    out["optimizer_lr"] = current_lr;
    return out;
}

pybind11::dict log_utilization(pybind11::object pufferl_obj) {
    (void)pufferl_obj;
    pybind11::dict result;

    // Metal device memory info
    MetalContext* ctx = mtl_ctx();
    if (ctx->device) {
        // recommendedMaxWorkingSetSize is the GPU memory budget
        uint64_t gpu_budget = [ctx->device recommendedMaxWorkingSetSize];
        uint64_t gpu_current = [ctx->device currentAllocatedSize];
        result["gpu_util"] = 0.0f;  // no GPU utilization counter on Metal
        result["gpu_mem"] = 100.0f * (float)gpu_current / (float)gpu_budget;
        result["vram_used_gb"] = (float)gpu_current / (1024.0f * 1024.0f * 1024.0f);
        result["vram_total_gb"] = (float)gpu_budget / (1024.0f * 1024.0f * 1024.0f);
    }

    // CPU memory via mach task_info (macOS equivalent of /proc/self/status)
    struct mach_task_basic_info info;
    mach_msg_type_number_t count = MACH_TASK_BASIC_INFO_COUNT;
    if (task_info(mach_task_self(), MACH_TASK_BASIC_INFO,
                  (task_info_t)&info, &count) == KERN_SUCCESS) {
        result["cpu_mem_gb"] = (float)info.resident_size / (1024.0f * 1024.0f * 1024.0f);
    }

    return result;
}

void puf_close(pybind11::object pufferl_obj) {
    PuffeRL& pufferl = pufferl_obj.cast<PuffeRL&>();
    close_impl(pufferl);
}

void save_weights(pybind11::object pufferl_obj, const std::string& path) {
    PuffeRL& pufferl = pufferl_obj.cast<PuffeRL&>();
    int64_t nbytes = pufferl.alloc_fp32.params.total_elems * sizeof(float);
    // Sync pending training before reading weights
    sync_pending_train(pufferl);
    ensure_gpu_synced((cudaStream_t)mtl_stream());
    FILE* f = fopen(path.c_str(), "wb");
    assert(f && "Failed to open weight file for writing");
    fwrite(pufferl.alloc_fp32.params.mem, 1, nbytes, f);
    fclose(f);
}

void load_weights(pybind11::object pufferl_obj, const std::string& path) {
    PuffeRL& pufferl = pufferl_obj.cast<PuffeRL&>();
    int64_t nbytes = pufferl.alloc_fp32.params.total_elems * sizeof(float);
    FILE* f = fopen(path.c_str(), "rb");
    assert(f && "Failed to open weight file for reading");
    fseek(f, 0, SEEK_END);
    long file_size = ftell(f);
    fseek(f, 0, SEEK_SET);
    assert(file_size == nbytes && "Weight file size mismatch");
    // Sync pending training before overwriting weights
    sync_pending_train(pufferl);
    ensure_gpu_synced((cudaStream_t)mtl_stream());
    size_t nread = fread(pufferl.alloc_fp32.params.mem, 1, nbytes, f);
    assert((int64_t)nread == nbytes && "Failed to read weight file");
    fclose(f);
    // Update inference weights with loaded data
    copy_weights_to_infer(pufferl);
}

// ============================================================================
// Config parsing
// ============================================================================

double get_config(py::dict& kwargs, const char* key) {
    assert(kwargs.contains(key) && "Missing config key");
    return kwargs[key].cast<double>();
}

Dict* py_dict_to_c_dict(py::dict py_dict) {
    Dict* c_dict = create_dict(py_dict.size());
    for (auto item : py_dict) {
        const char* key = PyUnicode_AsUTF8(item.first.ptr());
        try {
            dict_set(c_dict, key, item.second.cast<double>());
        } catch (const py::cast_error&) {
            // Skip non-numeric values
        }
    }
    return c_dict;
}

std::unique_ptr<PuffeRL> create_pufferl(pybind11::dict kwargs,
        pybind11::dict vec_kwargs, pybind11::dict env_kwargs,
        pybind11::dict policy_kwargs) {
    HypersT hypers;
    // Layout
    hypers.total_agents = get_config(vec_kwargs, "total_agents");
    hypers.num_buffers = get_config(vec_kwargs, "num_buffers");
    hypers.num_threads = get_config(vec_kwargs, "num_threads");
    hypers.horizon = get_config(kwargs, "horizon");
    // Model architecture
    hypers.hidden_size = get_config(policy_kwargs, "hidden_size");
    hypers.num_layers = get_config(policy_kwargs, "num_layers");
    hypers.arch_type = policy_kwargs.contains("arch") ? (int)get_config(policy_kwargs, "arch") : ARCH_RICH;
    hypers.seed = kwargs.contains("seed") ? (uint64_t)get_config(kwargs, "seed") : 42;
    // Learning rate
    hypers.lr = get_config(kwargs, "learning_rate");
    hypers.min_lr_ratio = get_config(kwargs, "min_lr_ratio");
    hypers.anneal_lr = get_config(kwargs, "anneal_lr");
    // Optimizer
    hypers.beta1 = get_config(kwargs, "beta1");
    hypers.beta2 = get_config(kwargs, "beta2");
    hypers.eps = get_config(kwargs, "eps");
    // Training
    hypers.minibatch_size = get_config(kwargs, "minibatch_size");
    hypers.replay_ratio = get_config(kwargs, "replay_ratio");
    hypers.total_timesteps = get_config(kwargs, "total_timesteps");
    hypers.max_grad_norm = get_config(kwargs, "max_grad_norm");
    // PPO
    hypers.clip_coef = get_config(kwargs, "clip_coef");
    hypers.vf_clip_coef = get_config(kwargs, "vf_clip_coef");
    hypers.vf_coef = get_config(kwargs, "vf_coef");
    hypers.ent_coef = get_config(kwargs, "ent_coef");
    // GAE
    hypers.gamma = get_config(kwargs, "gamma");
    hypers.gae_lambda = get_config(kwargs, "gae_lambda");
    // VTrace
    hypers.vtrace_rho_clip = get_config(kwargs, "vtrace_rho_clip");
    hypers.vtrace_c_clip = get_config(kwargs, "vtrace_c_clip");
    // Priority
    hypers.prio_alpha = get_config(kwargs, "prio_alpha");
    hypers.prio_beta0 = get_config(kwargs, "prio_beta0");
    // Flags
    hypers.use_rnn = get_config(kwargs, "use_rnn");
    hypers.cudagraphs = -1;  // always disabled on Metal
    hypers.kernels = true;   // always use Metal kernels
    hypers.profile = get_config(kwargs, "profile");
    hypers.overlap = kwargs.contains("overlap") && get_config(kwargs, "overlap") > 0;

    std::string env_name = kwargs["env_name"].cast<std::string>();
    Dict* vec_dict = py_dict_to_c_dict(vec_kwargs.cast<py::dict>());
    Dict* env_dict = py_dict_to_c_dict(env_kwargs.cast<py::dict>());

    std::unique_ptr<PuffeRL> pufferl;
    {
        pybind11::gil_scoped_release no_gil;
        pufferl = create_pufferl_impl(hypers, env_name, vec_dict, env_dict);
    }

    return pufferl;
}

// ============================================================================
// Module definition
// ============================================================================

PYBIND11_MODULE(_C, m) {
    m.def("log_environments", &log_environments);
    m.def("log_losses", &log_losses);
    m.def("log_profile", &log_profile);
    m.def("log_train_debug", &log_train_debug);
    m.def("log_utilization", &log_utilization);
    m.def("render", &render);
    m.def("rollouts", &rollouts);
    m.def("train", &train);
    m.def("close", &puf_close);
    m.def("save_weights", &save_weights);
    m.def("load_weights", &load_weights);
    m.def("python_vec_recv", &python_vec_recv);
    m.def("python_vec_send", &python_vec_send);

    py::class_<Policy>(m, "Policy");
    py::class_<Muon>(m, "Muon");
    py::class_<Allocator>(m, "Allocator")
        .def(py::init<>());

    py::class_<HypersT>(m, "HypersT")
        .def_readwrite("horizon", &HypersT::horizon)
        .def_readwrite("total_agents", &HypersT::total_agents)
        .def_readwrite("num_buffers", &HypersT::num_buffers)
        .def_readwrite("num_atns", &HypersT::num_atns)
        .def_readwrite("hidden_size", &HypersT::hidden_size)
        .def_readwrite("replay_ratio", &HypersT::replay_ratio)
        .def_readwrite("num_layers", &HypersT::num_layers)
        .def_readwrite("seed", &HypersT::seed)
        .def_readwrite("lr", &HypersT::lr)
        .def_readwrite("min_lr_ratio", &HypersT::min_lr_ratio)
        .def_readwrite("anneal_lr", &HypersT::anneal_lr)
        .def_readwrite("beta1", &HypersT::beta1)
        .def_readwrite("beta2", &HypersT::beta2)
        .def_readwrite("eps", &HypersT::eps)
        .def_readwrite("total_timesteps", &HypersT::total_timesteps)
        .def_readwrite("max_grad_norm", &HypersT::max_grad_norm)
        .def_readwrite("clip_coef", &HypersT::clip_coef)
        .def_readwrite("vf_clip_coef", &HypersT::vf_clip_coef)
        .def_readwrite("vf_coef", &HypersT::vf_coef)
        .def_readwrite("ent_coef", &HypersT::ent_coef)
        .def_readwrite("gamma", &HypersT::gamma)
        .def_readwrite("gae_lambda", &HypersT::gae_lambda)
        .def_readwrite("vtrace_rho_clip", &HypersT::vtrace_rho_clip)
        .def_readwrite("vtrace_c_clip", &HypersT::vtrace_c_clip)
        .def_readwrite("prio_alpha", &HypersT::prio_alpha)
        .def_readwrite("prio_beta0", &HypersT::prio_beta0)
        .def_readwrite("use_rnn", &HypersT::use_rnn)
        .def_readwrite("cudagraphs", &HypersT::cudagraphs)
        .def_readwrite("kernels", &HypersT::kernels)
        .def_readwrite("profile", &HypersT::profile)
        .def_readwrite("overlap", &HypersT::overlap);

    py::class_<PufTensor>(m, "PufTensor")
        .def("__repr__", &PufTensor::repr)
        .def("ndim", &PufTensor::ndim)
        .def("numel", &PufTensor::numel)
        .def_readonly("dtype_size", &PufTensor::dtype_size);

    py::class_<RolloutBuf>(m, "RolloutBuf")
        .def_readwrite("observations", &RolloutBuf::observations)
        .def_readwrite("actions", &RolloutBuf::actions)
        .def_readwrite("values", &RolloutBuf::values)
        .def_readwrite("logprobs", &RolloutBuf::logprobs)
        .def_readwrite("rewards", &RolloutBuf::rewards)
        .def_readwrite("terminals", &RolloutBuf::terminals)
        .def_readwrite("ratio", &RolloutBuf::ratio)
        .def_readwrite("importance", &RolloutBuf::importance);

    m.def("create_pufferl", &create_pufferl);
    py::class_<PuffeRL, std::unique_ptr<PuffeRL>>(m, "PuffeRL")
        .def_readwrite("policy", &PuffeRL::policy)
        .def_readwrite("muon", &PuffeRL::muon)
        .def_readwrite("hypers", &PuffeRL::hypers)
        .def_readwrite("rollouts", &PuffeRL::rollouts)
        .def("num_params", [](PuffeRL& self) -> int64_t {
            return self.alloc_fp32.params.total_elems;
        })
        .def("set_pfsp_weights", [](PuffeRL& self, py::list pool, py::list cum_weights) {
            int pool_size = (int)py::len(pool);
            std::vector<int> pool_arr(pool_size);
            std::vector<int> weights_arr(pool_size);
            for (int i = 0; i < pool_size; i++) {
                pool_arr[i] = pool[i].cast<int>();
                weights_arr[i] = cum_weights[i].cast<int>();
            }
            binding_set_pfsp_weights(self.vec, pool_arr.data(), weights_arr.data(), pool_size);
        })
        .def("get_pfsp_stats", [](PuffeRL& self) {
            float wins[32] = {0};
            float episodes[32] = {0};
            int pool_size = 0;
            binding_get_pfsp_stats(self.vec, wins, episodes, &pool_size);
            py::list wins_list, eps_list;
            for (int i = 0; i < pool_size; i++) {
                wins_list.append(wins[i]);
                eps_list.append(episodes[i]);
            }
            return py::make_tuple(wins_list, eps_list);
        });
}
