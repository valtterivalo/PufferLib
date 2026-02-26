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
#include <mach/mach.h>

namespace py = pybind11;

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

void sync_fused_weight(PuffeRL& pufferl);

void rollouts(pybind11::object pufferl_obj) {
    PuffeRL& pufferl = pufferl_obj.cast<PuffeRL&>();
    // Reset sync stats before rollout to capture rollout-only syncs
    { int _c; double _m; mtl_sync_stats(&_c, &_m); }
    pybind11::gil_scoped_release no_gil;
    // Sync async training from previous iteration (if any).
    // Overlap: wait for train_stream to complete — weights_infer and fused weight
    // were updated on the GPU as the last ops in train_impl's async dispatch.
    // Baseline: recompute fused weight on CPU after training updated weights_fp32.
    if (pufferl.train_pending) {
        sync_pending_train(pufferl);
    } else if (!pufferl.overlap_enabled) {
        sync_fused_weight(pufferl);
    }
    auto t0 = std::chrono::high_resolution_clock::now();
    static_vec_omp_step(pufferl.vec);
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
    // Zero the accumulator (unified memory: memset directly)
    memset(losses_host, 0, pufferl.losses_puf.numel() * pufferl.losses_puf.dtype_size);
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

    fprintf(stderr,
        "[metal-prof] total: %d syncs, %.1fms sync, %.1fms rollout + %.1fms train | "
        "gpu_exec=%.1fms sched_wait=%.1fms\n",
        r_sync + t_sync, r_sync_ms + t_sync_ms,
        a[PROF_ROLLOUT], train_ms, gpu_exec_ms, sched_wait_ms);

    memset(pufferl.profile.accum, 0, sizeof(pufferl.profile.accum));
    pufferl.rollout_sync_count = 0;
    pufferl.rollout_sync_ms = 0;
    pufferl.train_sync_count = 0;
    pufferl.train_sync_ms = 0;
    return result;
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
    hypers.use_adam = kwargs.contains("use_adam") && get_config(kwargs, "use_adam") > 0;

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
        });
}
