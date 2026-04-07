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
#include <sys/time.h>

namespace py = pybind11;

static double wall_clock() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec + tv.tv_usec * 1e-6;
}

struct FloatStats {
    double mean = 0.0;
    double stddev = 0.0;
    double abs_mean = 0.0;
    double min = 0.0;
    double max = 0.0;
};

static FloatStats compute_float_stats(const float* data, int64_t count) {
    assert(data && count > 0);
    FloatStats stats;

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
    assert(data && count > 0);
    int64_t clipped = 0;
    for (int64_t i = 0; i < count; i++) {
        if (std::fabs((double)data[i] - 1.0) > clip_coef) clipped++;
    }
    return (double)clipped / (double)count;
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
    assert(data && count > 0);

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
    if (!pufferl.cpu_inference) puf_set_gpu_training(true);
    static_vec_omp_step(pufferl.vec);
    if (!pufferl.cpu_inference) puf_set_gpu_training(false);
    float sec = std::chrono::duration<float>(
        std::chrono::high_resolution_clock::now() - t0).count();
    pufferl.profile.accum[PROF_ROLLOUT] += sec * 1000.0f;

    float eval_prof[NUM_EVAL_PROF];
    static_vec_read_profile(pufferl.vec, eval_prof);
    pufferl.profile.accum[PROF_EVAL_GPU] += eval_prof[EVAL_GPU];
    pufferl.profile.accum[PROF_EVAL_ENV] += eval_prof[EVAL_ENV_STEP];

    // Capture rollout sync stats
    mtl_sync_stats(&pufferl.rollout_sync_count, &pufferl.rollout_sync_ms);

    // Track global_step and start_time (matching upstream PuffeRL fields)
    pufferl.global_step += pufferl.hypers.horizon * pufferl.hypers.total_agents;
    if (pufferl.start_time == 0) pufferl.start_time = wall_clock();
}

void train(pybind11::object pufferl_obj) {
    PuffeRL& pufferl = pufferl_obj.cast<PuffeRL&>();
    // Reset sync stats before train to capture train-only syncs
    { int _c; double _m; mtl_sync_stats(&_c, &_m); }
    {
        pybind11::gil_scoped_release no_gil;
        train_impl(pufferl);
    }
    // Capture train sync stats
    mtl_sync_stats(&pufferl.train_sync_count, &pufferl.train_sync_ms);
}

pybind11::dict log_losses(pybind11::object pufferl_obj) {
    auto& pufferl = pufferl_obj.cast<PuffeRL&>();
    // Sync pending training — losses are written by GPU training
    sync_pending_train(pufferl);
    mtl_ensure_stream_synced((cudaStream_t)mtl_stream());
    float* losses_host = pufferl.losses_puf.data;
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
    mtl_fill_f32(losses_host, 0.0f, (int)puf_numel(pufferl.losses_puf.shape), loss_stream);
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
    for (int i = PROF_TRAIN_PRIO; i <= PROF_TRAIN_MUON; i++) train_ms += a[i];
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
        "obs=%.1fms fwd=%.1fms act=%.1fms | "
        "env=%.1fms total=%.1fms\n",
        r_sync, r_sync_ms,
        a[PROF_ROLLOUT_OBS_COPY], a[PROF_ROLLOUT_FWD],
        a[PROF_ROLLOUT_ACT_COPY],
        a[PROF_EVAL_ENV], a[PROF_ROLLOUT]);
    fprintf(stderr,
        "[metal-prof] train:  %d syncs (%.1fms), "
        "pre=%.1fms prio=%.1fms sel=%.1fms "
        "fwd=%.1fms ppo=%.1fms bwd=%.1fms gc=%.1fms clip=%.1fms muon=%.1fms sync=%.1fms\n",
        t_sync, t_sync_ms,
        a[PROF_TRAIN_PRELOOP],
        a[PROF_TRAIN_PRIO], a[PROF_TRAIN_SELECT],
        a[PROF_TRAIN_FWD], a[PROF_TRAIN_PPO],
        a[PROF_TRAIN_BACKWARD], a[PROF_TRAIN_GRAD_COPY],
        a[PROF_TRAIN_GRAD_CLIP], a[PROF_TRAIN_MUON], a[PROF_TRAIN_SYNC]);
    // GPU timing diagnostic: actual kernel execution vs scheduling delay
    double gpu_exec_ms, sched_wait_ms;
    mtl_gpu_timing_stats(&gpu_exec_ms, &sched_wait_ms);
    result["gpu_exec"] = gpu_exec_ms / 1000.0;
    result["sched_wait"] = sched_wait_ms / 1000.0;

    int gemm_gpu;
    mtl_gemm_stats(&gemm_gpu);
    result["gemm_gpu"] = gemm_gpu;

    fprintf(stderr,
        "[metal-prof] total: %d syncs, %.1fms sync, %.1fms rollout + %.1fms train | "
        "gpu_exec=%.1fms sched_wait=%.1fms | gemm: gpu=%d\n",
        r_sync + t_sync, r_sync_ms + t_sync_ms,
        a[PROF_ROLLOUT], train_ms, gpu_exec_ms, sched_wait_ms,
        gemm_gpu);

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
    mtl_ensure_stream_synced((cudaStream_t)mtl_stream());

    const float* mb_adv = pufferl.train_buf.mb_advantages.data;
    int64_t mb_adv_n = puf_numel(pufferl.train_buf.mb_advantages.shape);
    const float* mb_prio = pufferl.train_buf.mb_prio.data;
    int64_t mb_prio_n = puf_numel(pufferl.train_buf.mb_prio.shape);
    const float* mb_ratio = pufferl.train_buf.mb_ratio.data;
    int64_t mb_ratio_n = puf_numel(pufferl.train_buf.mb_ratio.shape);
    const float* roll_ratio = pufferl.train_rollouts.ratio.data;
    int64_t roll_ratio_n = puf_numel(pufferl.train_rollouts.ratio.shape);
    const float* param_fp32 = pufferl.param_fp32_puf.data;
    int64_t param_fp32_n = puf_numel(pufferl.param_fp32_puf.shape);
    const float* prio_probs = pufferl.prio_bufs.prio_probs.data;
    int64_t prio_probs_n = puf_numel(pufferl.prio_bufs.prio_probs.shape);
    const float* sampled_prio = pufferl.prio_bufs.mb_prio.data;
    int64_t sampled_prio_n = puf_numel(pufferl.prio_bufs.mb_prio.shape);
    const float* grad_sum_sq = pufferl.grad_norm_puf.data;

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

    /* 10 essential metrics — training health + weight health */
    out["mb_ratio_abs_mean"] = ratio_stats.abs_mean;
    out["mb_ratio_clipfrac_raw"] = compute_clipfrac(
        mb_ratio, mb_ratio_n, pufferl.hypers.clip_coef);
    out["grad_l2"] = grad_l2;
    out["grad_clip_coef"] = grad_clip_coef;
    out["param_abs_max"] = param_abs_max;

    float current_lr = *pufferl.muon->lr_ptr;
    out["optimizer_lr"] = current_lr;

    // Decoder + encoder + GRU weight stats
    {
        DecoderWeights* dw = (DecoderWeights*)pufferl.weights_fp32.decoder;
        int od = dw->output_dim;
        int H = dw->hidden_dim;
        const float* w = dw->weight.data;
        if (w) {
            // Policy rows = first od rows, value row = last row of fused weight
            FloatStats pw_stats = compute_float_stats(w, (int64_t)od * H);
            FloatStats vw_stats = compute_float_stats(w + (int64_t)od * H, (int64_t)H);
            out["dec_policy_abs_max"] = std::max(std::fabs(pw_stats.min), std::fabs(pw_stats.max));
            out["dec_value_abs_max"] = std::max(std::fabs(vw_stats.min), std::fabs(vw_stats.max));
        }

        EncoderWeights* ew = (EncoderWeights*)pufferl.weights_fp32.encoder;
        if (ew && ew->weight.data) {
            FloatStats ew_stats = compute_float_stats(
                ew->weight.data, puf_numel(ew->weight.shape));
            out["enc_w_abs_max"] = std::max(std::fabs(ew_stats.min), std::fabs(ew_stats.max));
        }
    }

    // Per-layer GRU weight abs_max
    {
        MinGRUWeights* gw = (MinGRUWeights*)pufferl.weights_fp32.network;
        if (gw) {
            float gru_max = 0;
            for (int l = 0; l < gw->num_layers; l++) {
                int64_t n = puf_numel(gw->weights[l].shape);
                const float* w = gw->weights[l].data;
                if (!w) continue;
                FloatStats s = compute_float_stats(w, n);
                float lmax = std::max(std::fabs(s.min), std::fabs(s.max));
                char key[32];
                snprintf(key, sizeof(key), "gru_L%d_w_abs_max", l);
                out[key] = lmax;
                if (lmax > gru_max) gru_max = lmax;
            }
            out["gru_w_abs_max"] = gru_max;
        }
    }

    return out;
}

/* diagnostic: dump rollout buffer stats to find PPO ratio=0 bug */
pybind11::dict dump_rollout_debug(pybind11::object pufferl_obj) {
    auto& pufferl = pufferl_obj.cast<PuffeRL&>();
    sync_pending_train(pufferl);
    mtl_ensure_stream_synced((cudaStream_t)mtl_stream());
    pybind11::dict out;

    /* rollout logprobs */
    int64_t lp_n = puf_numel(pufferl.rollouts.logprobs.shape);
    float* lp = pufferl.rollouts.logprobs.data;
    float lp_sum = 0, lp_abssum = 0;
    int lp_zero = 0, lp_neginf = 0;
    for (int64_t i = 0; i < lp_n; i++) {
        lp_sum += lp[i];
        lp_abssum += fabsf(lp[i]);
        if (lp[i] == 0.0f) lp_zero++;
        if (lp[i] < -1e6f) lp_neginf++;
    }
    out["lp_count"] = (int)lp_n;
    out["lp_mean"] = lp_n > 0 ? lp_sum / lp_n : 0;
    out["lp_abs_mean"] = lp_n > 0 ? lp_abssum / lp_n : 0;
    out["lp_zeros"] = lp_zero;
    out["lp_neginf"] = lp_neginf;
    out["lp_first5"] = pybind11::make_tuple(
        lp_n > 0 ? lp[0] : 0, lp_n > 1 ? lp[1] : 0, lp_n > 2 ? lp[2] : 0,
        lp_n > 3 ? lp[3] : 0, lp_n > 4 ? lp[4] : 0);

    /* rollout rewards */
    int64_t rew_n = puf_numel(pufferl.rollouts.rewards.shape);
    float* rew = pufferl.rollouts.rewards.data;
    float rew_sum = 0;
    for (int64_t i = 0; i < rew_n; i++) rew_sum += rew[i];
    out["rew_count"] = (int)rew_n;
    out["rew_mean"] = rew_n > 0 ? rew_sum / rew_n : 0;

    /* rollout actions */
    int64_t act_n = puf_numel(pufferl.rollouts.actions.shape);
    float* act = pufferl.rollouts.actions.data;
    float act_sum = 0;
    int act_zero = 0;
    for (int64_t i = 0; i < act_n; i++) {
        act_sum += act[i];
        if (act[i] == 0.0f) act_zero++;
    }
    out["act_count"] = (int)act_n;
    out["act_mean"] = act_n > 0 ? act_sum / act_n : 0;
    out["act_zeros"] = act_zero;

    /* losses buffer */
    float* loss = pufferl.losses_puf.data;
    int loss_n = (int)puf_numel(pufferl.losses_puf.shape);
    out["loss_N"] = loss_n > LOSS_N ? loss[LOSS_N] : -1;
    out["loss_PG"] = loss_n > LOSS_PG ? loss[LOSS_PG] : -1;
    out["loss_VF"] = loss_n > LOSS_VF ? loss[LOSS_VF] : -1;
    out["loss_ENT"] = loss_n > LOSS_ENT ? loss[LOSS_ENT] : -1;

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

// ============================================================================
// Unified log functions (upstream-compatible API)
// ============================================================================

pybind11::dict puf_log(pybind11::object pufferl_obj) {
    auto& pufferl = pufferl_obj.cast<PuffeRL&>();
    pybind11::dict result;

    // Summary (matches upstream puf_log in bindings.cu)
    long global_step = pufferl.global_step;
    int epoch = pufferl.epoch;
    double now = wall_clock();
    double dt = now - pufferl.last_log_time;
    long sps = dt > 0 ? (long)((global_step - pufferl.last_log_step) / dt) : 0;
    pufferl.last_log_time = now;
    pufferl.last_log_step = global_step;

    result["SPS"] = sps;
    result["agent_steps"] = global_step;
    result["uptime"] = now - pufferl.start_time;
    result["epoch"] = epoch;

    // Environment stats
    pybind11::dict env_dict;
    Dict* env_out = log_environments_impl(pufferl);
    for (int i = 0; i < env_out->size; i++) {
        env_dict[env_out->items[i].key] = env_out->items[i].value;
    }
    result["env"] = env_dict;

    // Losses — sync pending training, read from unified memory
    sync_pending_train(pufferl);
    mtl_ensure_stream_synced((cudaStream_t)mtl_stream());
    float* losses_host = pufferl.losses_puf.data;
    float n = losses_host[LOSS_N];
    pybind11::dict losses_dict;
    if (n > 0) {
        float inv_n = 1.0f / n;
        // Map to upstream key names: policy, value, entropy, total, old_kl, kl, clipfrac
        losses_dict["policy"] = losses_host[LOSS_PG] * inv_n;
        losses_dict["value"] = losses_host[LOSS_VF] * inv_n;
        losses_dict["entropy"] = losses_host[LOSS_ENT] * inv_n;
        losses_dict["total"] = losses_host[LOSS_TOTAL] * inv_n;
        losses_dict["old_kl"] = losses_host[LOSS_OLD_APPROX_KL] * inv_n;
        losses_dict["kl"] = losses_host[LOSS_APPROX_KL] * inv_n;
        losses_dict["clipfrac"] = losses_host[LOSS_CLIPFRAC] * inv_n;
    }
    cudaStream_t loss_stream = pufferl.overlap_enabled
        ? (cudaStream_t)mtl_train_stream()
        : (cudaStream_t)mtl_stream();
    mtl_fill_f32(losses_host, 0.0f, (int)puf_numel(pufferl.losses_puf.shape), loss_stream);
    result["loss"] = losses_dict;

    // Profile
    pybind11::dict perf_dict;
    for (int i = 0; i < NUM_PROF; i++) {
        perf_dict[PROF_NAMES[i]] = pufferl.profile.accum[i] / 1000.0f;
    }
    float* a = pufferl.profile.accum;
    float train_ms = a[PROF_TRAIN_PRELOOP] + a[PROF_TRAIN_SYNC];
    for (int i = PROF_TRAIN_PRIO; i <= PROF_TRAIN_MUON; i++) train_ms += a[i];
    perf_dict["train"] = train_ms / 1000.0f;
    memset(pufferl.profile.accum, 0, sizeof(pufferl.profile.accum));
    result["perf"] = perf_dict;

    // Utilization (Metal / macOS)
    pybind11::dict util_dict;
    MetalContext* ctx = mtl_ctx();
    if (ctx->device) {
        uint64_t gpu_budget = [ctx->device recommendedMaxWorkingSetSize];
        uint64_t gpu_current = [ctx->device currentAllocatedSize];
        util_dict["gpu_percent"] = 0.0f;
        util_dict["gpu_mem"] = 100.0f * (float)gpu_current / (float)gpu_budget;
        util_dict["vram_used_gb"] = (float)gpu_current / (1024.0f * 1024.0f * 1024.0f);
        util_dict["vram_total_gb"] = (float)gpu_budget / (1024.0f * 1024.0f * 1024.0f);
    }
    struct mach_task_basic_info info;
    mach_msg_type_number_t count = MACH_TASK_BASIC_INFO_COUNT;
    if (task_info(mach_task_self(), MACH_TASK_BASIC_INFO,
                  (task_info_t)&info, &count) == KERN_SUCCESS) {
        util_dict["cpu_mem_gb"] = (float)info.resident_size / (1024.0f * 1024.0f * 1024.0f);
    }
    result["util"] = util_dict;

    return result;
}

pybind11::dict puf_eval_log(pybind11::object pufferl_obj) {
    auto& pufferl = pufferl_obj.cast<PuffeRL&>();
    pybind11::dict result;

    double now = wall_clock();
    pufferl.last_log_time = now;
    pufferl.last_log_step = pufferl.global_step;

    pybind11::dict env_dict;
    Dict* env_out = create_dict(32);
    static_vec_eval_log(pufferl.vec, env_out);
    for (int i = 0; i < env_out->size; i++) {
        env_dict[env_out->items[i].key] = env_out->items[i].value;
    }
    result["env"] = env_dict;

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
    mtl_ensure_stream_synced((cudaStream_t)mtl_stream());
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
    mtl_ensure_stream_synced((cudaStream_t)mtl_stream());
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

std::unique_ptr<PuffeRL> create_pufferl(py::dict args) {
    py::dict train_kwargs = args["train"].cast<py::dict>();
    py::dict vec_kwargs = args["vec"].cast<py::dict>();
    py::dict env_kwargs = args["env"].cast<py::dict>();
    py::dict policy_kwargs = args["policy"].cast<py::dict>();

    HypersT hypers;
    // Layout (total_agents and num_buffers come from vec config)
    hypers.total_agents = get_config(vec_kwargs, "total_agents");
    hypers.num_buffers = get_config(vec_kwargs, "num_buffers");
    hypers.num_threads = get_config(vec_kwargs, "num_threads");
    hypers.horizon = get_config(train_kwargs, "horizon");
    // Model architecture
    hypers.hidden_size = get_config(policy_kwargs, "hidden_size");
    hypers.num_layers = get_config(policy_kwargs, "num_layers");
    hypers.seed = args.contains("seed") ? (uint64_t)get_config(args, "seed")
        : train_kwargs.contains("seed") ? (uint64_t)get_config(train_kwargs, "seed") : 42;
    // Learning rate
    hypers.lr = get_config(train_kwargs, "learning_rate");
    hypers.min_lr_ratio = get_config(train_kwargs, "min_lr_ratio");
    hypers.anneal_lr = get_config(train_kwargs, "anneal_lr");
    // Optimizer (Muon only)
    hypers.beta1 = get_config(train_kwargs, "beta1");
    hypers.weight_decay = get_config(train_kwargs, "weight_decay");
    // Training
    hypers.minibatch_size = get_config(train_kwargs, "minibatch_size");
    hypers.replay_ratio = get_config(train_kwargs, "replay_ratio");
    hypers.total_timesteps = get_config(train_kwargs, "total_timesteps");
    hypers.max_grad_norm = get_config(train_kwargs, "max_grad_norm");
    // PPO
    hypers.clip_coef = get_config(train_kwargs, "clip_coef");
    hypers.vf_clip_coef = get_config(train_kwargs, "vf_clip_coef");
    hypers.vf_coef = get_config(train_kwargs, "vf_coef");
    hypers.ent_coef = get_config(train_kwargs, "ent_coef");
    // GAE
    hypers.gamma = get_config(train_kwargs, "gamma");
    hypers.gae_lambda = get_config(train_kwargs, "gae_lambda");
    // VTrace
    hypers.vtrace_rho_clip = get_config(train_kwargs, "vtrace_rho_clip");
    hypers.vtrace_c_clip = get_config(train_kwargs, "vtrace_c_clip");
    // Priority
    hypers.prio_alpha = get_config(train_kwargs, "prio_alpha");
    hypers.prio_beta0 = get_config(train_kwargs, "prio_beta0");
    // Flags — check both top-level args and train sub-dict
    hypers.reset_state = (args.contains("reset_state") && get_config(args, "reset_state") > 0)
        || (train_kwargs.contains("reset_state") && get_config(train_kwargs, "reset_state") > 0);
    hypers.profile = train_kwargs.contains("profile") ? get_config(train_kwargs, "profile")
        : args.contains("profile") ? get_config(args, "profile") : 0;
    mtl_enable_gpu_timing(hypers.profile);
    hypers.overlap = (train_kwargs.contains("overlap") && get_config(train_kwargs, "overlap") > 0)
        || (args.contains("overlap") && get_config(args, "overlap") > 0);
    hypers.cpu_inference = (train_kwargs.contains("cpu_inference") && get_config(train_kwargs, "cpu_inference") > 0)
        || (args.contains("cpu_inference") && get_config(args, "cpu_inference") > 0);
    hypers.train_fp16 = (train_kwargs.contains("train_fp16") && get_config(train_kwargs, "train_fp16") > 0)
        || (args.contains("train_fp16") && get_config(args, "train_fp16") > 0);
    hypers.ns_iters = train_kwargs.contains("ns_iters") ? (int)get_config(train_kwargs, "ns_iters")
        : args.contains("ns_iters") ? (int)get_config(args, "ns_iters") : 5;
    hypers.gpu_id = args.contains("gpu_id") ? (int)get_config(args, "gpu_id") : 0;

    std::string env_name = args["env_name"].cast<std::string>();
    Dict* vec_dict = py_dict_to_c_dict(vec_kwargs);
    Dict* env_dict = py_dict_to_c_dict(env_kwargs);

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
    // Module attributes (upstream compat)
    m.attr("gpu") = 0;  // Metal uses unified memory, not discrete GPU

    // Unified log functions (upstream API)
    m.def("log", &puf_log);
    m.def("eval_log", &puf_eval_log);
    m.def("uptime", [](py::object pufferl_obj) -> double {
        PuffeRL& pufferl = pufferl_obj.cast<PuffeRL&>();
        double now = wall_clock();
        return now - pufferl.start_time;
    });

    // Metal-specific granular log functions (kept for backward compat)
    m.def("log_environments", &log_environments);
    m.def("log_losses", &log_losses);
    m.def("log_profile", &log_profile);
    m.def("log_train_debug", &log_train_debug);
    m.def("dump_rollout_debug", &dump_rollout_debug);
    m.def("log_utilization", &log_utilization);

    // Core functions
    m.def("render", &render);
    m.def("rollouts", &rollouts);
    m.def("train", &train);
    m.def("close", &puf_close);
    m.def("save_weights", &save_weights);
    m.def("load_weights", &load_weights);

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
        .def_readwrite("weight_decay", &HypersT::weight_decay)
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
        .def_readwrite("reset_state", &HypersT::reset_state)
        .def_readwrite("profile", &HypersT::profile)
        .def_readwrite("overlap", &HypersT::overlap)
        .def_readwrite("train_fp16", &HypersT::train_fp16)
        .def_readwrite("ns_iters", &HypersT::ns_iters)
        .def_readwrite("gpu_id", &HypersT::gpu_id);

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
        .def_readonly("epoch", &PuffeRL::epoch)
        .def_readonly("global_step", &PuffeRL::global_step)
        .def_readonly("last_log_time", &PuffeRL::last_log_time)
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
