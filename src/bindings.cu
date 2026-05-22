// bindings.cpp - Python bindings for pufferlib (torch-free)

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <cstring>
#include <stdexcept>
#include <string>
#include "pufferlib.cu"

#define _PUFFER_STRINGIFY(x) #x
#define PUFFER_STRINGIFY(x) _PUFFER_STRINGIFY(x)

namespace py = pybind11;

static void cuda_check(cudaError_t err, const char* op) {
    if (err != cudaSuccess) {
        throw std::runtime_error(
            std::string(op) + ": " + cudaGetErrorString(err));
    }
}

static void assert_static_env_name_matches(void) {
    const char* binding_env_name = PUFFER_STRINGIFY(ENV_NAME);
    const char* static_env_name = get_static_env_name();
    if (strcmp(binding_env_name, static_env_name) != 0) {
        throw std::runtime_error(
            std::string("compiled _C env mismatch: binding env_name=") +
            binding_env_name + ", static_env_name=" + static_env_name);
    }
}

// Wrapper functions for Python bindings
pybind11::dict puf_log(pybind11::object pufferl_obj) {
    auto& pufferl = pufferl_obj.cast<PuffeRL&>();
    pybind11::dict result;

    // Summary
    int gpus = pufferl.hypers.world_size;
    long global_step = pufferl.global_step;
    long epoch = pufferl.epoch;
    double now = wall_clock();
    double dt = now - pufferl.last_log_time;
    long sps = dt > 0 ? (long)((global_step - pufferl.last_log_step) / dt) : 0;
    pufferl.last_log_time = now;
    pufferl.last_log_step = global_step;

    result["SPS"] = sps * gpus;
    result["agent_steps"] = global_step * gpus;
    result["uptime"] = now - pufferl.start_time;
    result["epoch"] = epoch;

    // Environment stats
    pybind11::dict env_dict;
    Dict* env_out = log_environments_impl(pufferl);
    for (int i = 0; i < env_out->size; i++) {
        env_dict[env_out->items[i].key] = env_out->items[i].value;
    }
    result["env"] = env_dict;

    // Losses
    pybind11::dict losses_dict;
    float losses_host[NUM_LOSSES];
    cudaMemcpy(losses_host, pufferl.losses_puf.data, sizeof(losses_host), cudaMemcpyDeviceToHost);
    float n = losses_host[LOSS_N];
    if (n > 0) {
        float inv_n = 1.0f / n;
        losses_dict["policy"] = losses_host[LOSS_PG] * inv_n;
        losses_dict["value"] = losses_host[LOSS_VF] * inv_n;
        losses_dict["entropy"] = losses_host[LOSS_ENT] * inv_n;
        losses_dict["total"] = losses_host[LOSS_TOTAL] * inv_n;
        losses_dict["old_kl"] = losses_host[LOSS_OLD_APPROX_KL] * inv_n;
        losses_dict["kl"] = losses_host[LOSS_APPROX_KL] * inv_n;
        losses_dict["clipfrac"] = losses_host[LOSS_CLIPFRAC] * inv_n;
        losses_dict["parent_kl"] = losses_host[LOSS_PARENT_KL] * inv_n;
        losses_dict["parent_logit_delta"] = losses_host[LOSS_PARENT_LOGIT_DELTA] * inv_n;
    }
    cudaMemset(pufferl.losses_puf.data, 0, numel(pufferl.losses_puf.shape) * sizeof(float));
    result["loss"] = losses_dict;

    // Profile
    pybind11::dict perf_dict;
    float train_total = 0;
    for (int i = 0; i < NUM_PROF; i++) {
        float sec = pufferl.profile.accum[i] / 1000.0f;
        perf_dict[PROF_NAMES[i]] = sec;
        if (i >= PROF_TRAIN_MISC) train_total += sec;
    }
    perf_dict["train"] = train_total;
    if (inferno_env_profile_count &&
            inferno_env_profile_name &&
            inferno_env_profile_read_reset_ms) {
        int profile_count = inferno_env_profile_count();
        for (int i = 0; i < profile_count; i++) {
            const char* name = inferno_env_profile_name(i);
            double ms = inferno_env_profile_read_reset_ms(i);
            std::string key = std::string("inferno_") + name;
            perf_dict[pybind11::str(key)] = ms / 1000.0;
        }
    }
    memset(pufferl.profile.accum, 0, sizeof(pufferl.profile.accum));
    result["perf"] = perf_dict;

    // Utilization
    pybind11::dict util_dict;
    nvmlUtilization_t util;
    nvmlDeviceGetUtilizationRates(pufferl.nvml_device, &util);
    util_dict["gpu_percent"] = (float)util.gpu;

    nvmlMemory_t mem;
    nvmlDeviceGetMemoryInfo(pufferl.nvml_device, &mem);
    util_dict["gpu_mem"] = 100.0f * (float)mem.used / (float)mem.total;

    size_t cuda_free, cuda_total;
    cudaMemGetInfo(&cuda_free, &cuda_total);
    util_dict["vram_used_gb"] = (float)(cuda_total - cuda_free) / (1024.0f * 1024.0f * 1024.0f);
    util_dict["vram_total_gb"] = (float)cuda_total / (1024.0f * 1024.0f * 1024.0f);

    long rss_kb = 0;
    FILE* f = fopen("/proc/self/status", "r");
    if (f) {
        char line[256];
        while (fgets(line, sizeof(line), f)) {
            if (sscanf(line, "VmRSS: %ld", &rss_kb) == 1) break;
        }
        fclose(f);
    }
    util_dict["cpu_mem_gb"] = (float)rss_kb / (1024.0f * 1024.0f);
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
    Dict* env_out = create_dict(PUFFER_ENV_LOG_DICT_CAPACITY);
    static_vec_eval_log(pufferl.vec, env_out);
    for (int i = 0; i < env_out->size; i++) {
        env_dict[env_out->items[i].key] = env_out->items[i].value;
    }
    result["env"] = env_dict;

    return result;
}

void python_vec_recv(pybind11::object pufferl_obj, int buf) {
    // Not used in static/OMP path
}

void python_vec_send(pybind11::object pufferl_obj, int buf) {
    // Not used in static/OMP path
}

void render(pybind11::object pufferl_obj, int env_id) {
    PuffeRL& pufferl = pufferl_obj.cast<PuffeRL&>();
    static_vec_render(pufferl.vec, env_id);
}

void rollouts(pybind11::object pufferl_obj) {
    PuffeRL& pufferl = pufferl_obj.cast<PuffeRL&>();
    pybind11::gil_scoped_release no_gil;
    double t0 = wall_clock();

    // Zero state buffers
    if (pufferl.hypers.reset_state) {
        for (int i = 0; i < pufferl.hypers.num_buffers; i++) {
            puf_zero(&pufferl.buffer_states[i], pufferl.default_stream);
        }
    }

    if (pufferl.curriculum_enabled) {
        curriculum_rollout_begin(&pufferl);
    } else {
        pufferl.vec->log_env_limit = 0;
    }

    static_vec_omp_step(pufferl.vec);
    float sec = (float)(wall_clock() - t0);
    pufferl.profile.accum[PROF_ROLLOUT] += sec * 1000.0f;  // store as ms

    float eval_prof[NUM_EVAL_PROF];
    static_vec_read_profile(pufferl.vec, eval_prof);
    pufferl.profile.accum[PROF_EVAL_GPU] += eval_prof[EVAL_GPU];
    pufferl.profile.accum[PROF_EVAL_ENV] += eval_prof[EVAL_ENV_STEP];
    pufferl.global_step += pufferl.hypers.horizon * pufferl.hypers.total_agents;
    if (pufferl.phase2_ctx) {
        phase2_apply_cursor_gate(pufferl.phase2_ctx);
    }
}

pybind11::dict train(pybind11::object pufferl_obj) {
    PuffeRL& pufferl = pufferl_obj.cast<PuffeRL&>();
    {
        pybind11::gil_scoped_release no_gil;
        train_impl(pufferl);
    }
    pybind11::dict losses;
    return losses;
}

void puf_close(pybind11::object pufferl_obj) {
    PuffeRL& pufferl = pufferl_obj.cast<PuffeRL&>();
    close_impl(pufferl);
}

void save_weights(pybind11::object pufferl_obj, const std::string& path) {
    PuffeRL& pufferl = pufferl_obj.cast<PuffeRL&>();
    int64_t nbytes = numel(pufferl.master_weights.shape) * sizeof(float);
    std::vector<char> buf(nbytes);
    cuda_check(
        cudaMemcpy(buf.data(), pufferl.master_weights.data, nbytes,
            cudaMemcpyDeviceToHost),
        "save_weights device-to-host copy");
    FILE* f = fopen(path.c_str(), "wb");
    if (!f) throw std::runtime_error("Failed to open " + path + " for writing");
    fwrite(buf.data(), 1, nbytes, f);
    fclose(f);
}

void load_weights(pybind11::object pufferl_obj, const std::string& path) {
    PuffeRL& pufferl = pufferl_obj.cast<PuffeRL&>();
    int64_t nbytes = numel(pufferl.master_weights.shape) * sizeof(float);
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) throw std::runtime_error("Failed to open " + path + " for reading");
    // Verify file size matches
    fseek(f, 0, SEEK_END);
    long file_size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (file_size != nbytes) {
        fclose(f);
        throw std::runtime_error("Weight file size mismatch: expected " +
            std::to_string(nbytes) + " bytes, got " + std::to_string(file_size));
    }
    std::vector<char> buf(nbytes);
    size_t nread = fread(buf.data(), 1, nbytes, f);
    if ((int64_t)nread != nbytes) {
        fclose(f);
        throw std::runtime_error("Failed to read weight file");
    }
    fclose(f);
    cuda_check(
        cudaMemcpy(pufferl.master_weights.data, buf.data(), nbytes,
            cudaMemcpyHostToDevice),
        "load_weights host-to-device copy");
    if (USE_BF16) {
        int n = numel(pufferl.param_puf.shape);
        cast<<<grid_size(n), BLOCK_SIZE, 0, pufferl.default_stream>>>(
            pufferl.param_puf.data, pufferl.master_weights.data, n);
        cuda_check(cudaGetLastError(), "load_weights fp32-to-bf16 launch");
        cuda_check(
            cudaStreamSynchronize(pufferl.default_stream),
            "load_weights fp32-to-bf16 sync");
    }
}

struct TrainingStateHeader {
    uint32_t magic;
    uint32_t version;
    int64_t num_weights;
    int64_t global_step;
    int64_t epoch;
};

static constexpr uint32_t TRAINING_STATE_MAGIC = 0x54534650u;
static constexpr uint32_t TRAINING_STATE_VERSION = 1u;

void save_training_state(pybind11::object pufferl_obj, const std::string& path) {
    PuffeRL& pufferl = pufferl_obj.cast<PuffeRL&>();
    int64_t num_weights = numel(pufferl.master_weights.shape);
    int64_t nbytes = num_weights * sizeof(float);
    TrainingStateHeader header = {
        .magic = TRAINING_STATE_MAGIC,
        .version = TRAINING_STATE_VERSION,
        .num_weights = num_weights,
        .global_step = pufferl.global_step,
        .epoch = pufferl.epoch,
    };
    std::vector<char> weights(nbytes);
    std::vector<char> momentum(nbytes);
    cuda_check(
        cudaMemcpy(weights.data(), pufferl.master_weights.data, nbytes,
            cudaMemcpyDeviceToHost),
        "save_training_state weights device-to-host copy");
    cuda_check(
        cudaMemcpy(momentum.data(), pufferl.muon.mb_puf.data, nbytes,
            cudaMemcpyDeviceToHost),
        "save_training_state momentum device-to-host copy");

    FILE* f = fopen(path.c_str(), "wb");
    if (!f) throw std::runtime_error("Failed to open " + path + " for writing");
    bool ok =
        fwrite(&header, sizeof(header), 1, f) == 1 &&
        fwrite(weights.data(), 1, nbytes, f) == (size_t)nbytes &&
        fwrite(momentum.data(), 1, nbytes, f) == (size_t)nbytes;
    if (fclose(f) != 0 || !ok) {
        throw std::runtime_error("Failed to write training state " + path);
    }
}

void load_training_state(pybind11::object pufferl_obj, const std::string& path) {
    PuffeRL& pufferl = pufferl_obj.cast<PuffeRL&>();
    int64_t num_weights = numel(pufferl.master_weights.shape);
    int64_t nbytes = num_weights * sizeof(float);
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) throw std::runtime_error("Failed to open " + path + " for reading");

    TrainingStateHeader header;
    if (fread(&header, sizeof(header), 1, f) != 1) {
        fclose(f);
        throw std::runtime_error("Failed to read training state header " + path);
    }
    if (header.magic != TRAINING_STATE_MAGIC ||
            header.version != TRAINING_STATE_VERSION ||
            header.num_weights != num_weights) {
        fclose(f);
        throw std::runtime_error("Training state header mismatch in " + path);
    }

    std::vector<char> weights(nbytes);
    std::vector<char> momentum(nbytes);
    bool ok =
        fread(weights.data(), 1, nbytes, f) == (size_t)nbytes &&
        fread(momentum.data(), 1, nbytes, f) == (size_t)nbytes;
    if (fclose(f) != 0 || !ok) {
        throw std::runtime_error("Failed to read training state " + path);
    }

    cuda_check(
        cudaMemcpy(pufferl.master_weights.data, weights.data(), nbytes,
            cudaMemcpyHostToDevice),
        "load_training_state weights host-to-device copy");
    cuda_check(
        cudaMemcpy(pufferl.muon.mb_puf.data, momentum.data(), nbytes,
            cudaMemcpyHostToDevice),
        "load_training_state momentum host-to-device copy");
    if (USE_BF16) {
        int n = numel(pufferl.param_puf.shape);
        cast<<<grid_size(n), BLOCK_SIZE, 0, pufferl.default_stream>>>(
            pufferl.param_puf.data, pufferl.master_weights.data, n);
        cuda_check(cudaGetLastError(), "load_training_state fp32-to-bf16 launch");
        cuda_check(
            cudaStreamSynchronize(pufferl.default_stream),
            "load_training_state fp32-to-bf16 sync");
    }
    pufferl.global_step = header.global_step;
    pufferl.epoch = header.epoch;
    pufferl.last_log_step = pufferl.global_step;
}

void load_anchor_weights(
    pybind11::object pufferl_obj,
    const std::string& path,
    double anchor_coef
) {
    PuffeRL& pufferl = pufferl_obj.cast<PuffeRL&>();
    int64_t nbytes = numel(pufferl.master_weights.shape) * sizeof(float);
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) throw std::runtime_error("Failed to open " + path + " for reading");
    fseek(f, 0, SEEK_END);
    long file_size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (file_size != nbytes) {
        fclose(f);
        throw std::runtime_error("Anchor weight file size mismatch: expected " +
            std::to_string(nbytes) + " bytes, got " + std::to_string(file_size));
    }

    int64_t num_weights = numel(pufferl.master_weights.shape);
    std::vector<float> buf(num_weights);
    size_t nread = fread(buf.data(), 1, nbytes, f);
    if (fclose(f) != 0 || (int64_t)nread != nbytes) {
        throw std::runtime_error("Failed to read anchor weights " + path);
    }
    if (anchor_coef > 0.0) {
        if (!pufferl.anchor_weights.data) {
            pufferl.anchor_weights = {.shape = {num_weights}};
            cuda_check(
                cudaMalloc(&pufferl.anchor_weights.data, nbytes),
                "load_anchor_weights allocation");
        }
        cuda_check(
            cudaMemcpy(pufferl.anchor_weights.data, buf.data(), nbytes,
                cudaMemcpyHostToDevice),
            "load_anchor_weights host-to-device copy");
    }
    if (pufferl.hypers.parent_kl_coef > 0.0f || pufferl.hypers.parent_kl_log) {
        load_parent_policy_weights(pufferl, buf);
    }
    pufferl.anchor_coef = (float)anchor_coef;
}
void py_puff_advantage(
        long long values_ptr, long long rewards_ptr,
        long long dones_ptr,  long long importance_ptr,
        long long advantages_ptr,
        int num_steps, int horizon,
        float gamma, float lambda, float rho_clip, float c_clip) {
    constexpr int N = 16 / sizeof(precision_t);
    int blocks = grid_size(num_steps);
    auto kernel = (horizon % N == 0) ? puff_advantage : puff_advantage_scalar;
    kernel<<<blocks, 256>>>(
        (const precision_t*)values_ptr, (const precision_t*)rewards_ptr,
        (const precision_t*)dones_ptr,  (const precision_t*)importance_ptr,
        (precision_t*)advantages_ptr,
        gamma, lambda, rho_clip, c_clip, num_steps, horizon);
}

double get_config(py::dict& kwargs, const char* key) {
    if (!kwargs.contains(key)) {
        throw std::runtime_error(std::string("Missing config key: ") + key);
    }
    try {
        return kwargs[key].cast<double>();
    } catch (const py::cast_error& e) {
        throw std::runtime_error(std::string("Failed to cast config key '") + key + "': " + e.what());
    }
}

double get_optional_config(py::dict& kwargs, const char* key, double default_value) {
    if (!kwargs.contains(key)) {
        return default_value;
    }
    return get_config(kwargs, key);
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

// ============================================================================
// Python-facing VecEnv wrapper.
// After vec_step(), GPU buffers are current — Python wraps them zero-copy
// with torch.from_blob(ptr, shape, dtype, device='cuda').
// ============================================================================

struct VecEnv {
    StaticVec* vec;
    int total_agents;
    int obs_size;
    int num_atns;
    std::vector<int> act_sizes;
    std::string obs_dtype;
    size_t obs_elem_size;
    int gpu;
};

std::unique_ptr<VecEnv> create_vec(py::dict args, int gpu) {
    py::dict vec_kwargs = args["vec"].cast<py::dict>();
    py::dict env_kwargs = args["env"].cast<py::dict>();

    int total_agents = (int)get_config(vec_kwargs, "total_agents");
    int num_buffers  = (int)get_config(vec_kwargs, "num_buffers");

    Dict* vec_dict = py_dict_to_c_dict(vec_kwargs);
    Dict* env_dict = py_dict_to_c_dict(env_kwargs);

    auto ve = std::make_unique<VecEnv>();
    ve->gpu = gpu;
    {
        py::gil_scoped_release no_gil;
        ve->vec = create_static_vec(total_agents, num_buffers, gpu, vec_dict, env_dict);
    }
    ve->total_agents  = total_agents;
    ve->obs_size      = get_obs_size();
    ve->num_atns      = get_num_atns();
    {
        int* raw = get_act_sizes();
        int  n   = get_num_act_sizes();
        ve->act_sizes = std::vector<int>(raw, raw + n);
    }
    ve->obs_dtype     = std::string(get_obs_dtype());
    ve->obs_elem_size = get_obs_elem_size();
    return ve;
}

void vec_reset(VecEnv& ve) {
    py::gil_scoped_release no_gil;
    static_vec_reset(ve.vec);
}

void gpu_vec_step_py(VecEnv& ve, long long actions_ptr) {
    cudaMemcpy(ve.vec->gpu_actions, (void*)actions_ptr,
        (size_t)ve.total_agents * ve.num_atns * sizeof(float),
        cudaMemcpyDeviceToDevice);
    {
        py::gil_scoped_release no_gil;
        gpu_vec_step(ve.vec);
    }
}

void cpu_vec_step_py(VecEnv& ve, long long actions_ptr) {
    memcpy(ve.vec->actions, (void*)actions_ptr,
        (size_t)ve.total_agents * ve.num_atns * sizeof(float));
    {
        py::gil_scoped_release no_gil;
        cpu_vec_step(ve.vec);
    }
}

py::dict vec_log(VecEnv& ve) {
    Dict* out = create_dict(PUFFER_ENV_LOG_DICT_CAPACITY);
    static_vec_log(ve.vec, out);
    py::dict result;
    for (int i = 0; i < out->size; i++) {
        result[out->items[i].key] = out->items[i].value;
    }
    free(out->items);
    free(out);
    return result;
}

void vec_close(VecEnv& ve) {
    static_vec_close(ve.vec);
    ve.vec = nullptr;
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
    // Model architecture (num_atns computed from env in C++)
    hypers.hidden_size = get_config(policy_kwargs, "hidden_size");
    hypers.num_layers = get_config(policy_kwargs, "num_layers");
    // Learning rate
    hypers.lr = get_config(train_kwargs, "learning_rate");
    hypers.min_lr_ratio = get_config(train_kwargs, "min_lr_ratio");
    hypers.anneal_lr = get_config(train_kwargs, "anneal_lr");
    // Optimizer
    hypers.beta1 = get_config(train_kwargs, "beta1");
    hypers.beta2 = get_config(train_kwargs, "beta2");
    hypers.eps = get_config(train_kwargs, "eps");
    hypers.aurora = get_config(train_kwargs, "aurora");
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
    hypers.parent_kl_coef = get_config(train_kwargs, "parent_kl_coef");
    hypers.parent_kl_log = get_config(train_kwargs, "parent_kl_log");
    // GAE
    hypers.gamma = get_config(train_kwargs, "gamma");
    hypers.gae_lambda = get_config(train_kwargs, "gae_lambda");
    // VTrace
    hypers.vtrace_rho_clip = get_config(train_kwargs, "vtrace_rho_clip");
    hypers.vtrace_c_clip = get_config(train_kwargs, "vtrace_c_clip");
    // Priority
    hypers.prio_alpha = get_config(train_kwargs, "prio_alpha");
    hypers.prio_beta0 = get_config(train_kwargs, "prio_beta0");
    hypers.anneal_prio_beta = get_config(train_kwargs, "anneal_prio_beta");
    // Curriculum state buffer
    int state_curriculum_mode =
        (int)get_optional_config(train_kwargs, "state_curriculum_mode", 1.0);
    if (state_curriculum_mode < 0 || state_curriculum_mode > 1) {
        throw std::runtime_error("state_curriculum_mode must be 0 or 1");
    }
    hypers.state_buffer_size = get_config(train_kwargs, "state_buffer_size");
    hypers.cl_frac = get_config(train_kwargs, "cl_frac");
    hypers.anneal_cl = get_config(train_kwargs, "anneal_cl");
    hypers.warmup_states = get_config(train_kwargs, "warmup_states");
    hypers.state_checkpoint_interval = get_config(train_kwargs, "state_checkpoint_interval");
    if (state_curriculum_mode == 0) {
        hypers.state_buffer_size = 0;
        hypers.cl_frac = 0.0f;
        hypers.warmup_states = 0;
    }
    hypers.explore_alpha = get_config(train_kwargs, "explore_alpha");
    hypers.explore_beta = get_config(train_kwargs, "explore_beta");
    hypers.explore_decay = get_config(train_kwargs, "explore_decay");
    hypers.reset_state = get_config(args, "reset_state");
    hypers.terminal_reset_state = get_config(train_kwargs, "terminal_reset_state");
    // Base-level config ([base] section becomes top-level in args)
    hypers.cudagraphs = get_config(args, "cudagraphs");
    hypers.profile = get_config(args, "profile");
    // Multi-GPU / device selection
    hypers.rank = get_config(args, "rank");
    hypers.world_size = get_config(args, "world_size");
    hypers.gpu_id = get_config(args, "gpu_id");
    hypers.nccl_id = args["nccl_id"].cast<std::string>();
    // Seed
    hypers.seed = args.contains("seed") ? get_config(args, "seed")
        : train_kwargs.contains("seed") ? get_config(train_kwargs, "seed") : 42;

    int device_count = 0;
    cudaGetDeviceCount(&device_count);
    assert(device_count > 0 && "CUDA is not available");

    std::string env_name = args["env_name"].cast<std::string>();
    Dict* vec_dict = py_dict_to_c_dict(vec_kwargs.cast<py::dict>());
    Dict* env_dict = py_dict_to_c_dict(env_kwargs.cast<py::dict>());

    std::unique_ptr<PuffeRL> pufferl;
    {
        pybind11::gil_scoped_release no_gil;
        pufferl = create_pufferl_impl(hypers, env_name, vec_dict, env_dict);
    }

    if (!pufferl) {
        throw std::runtime_error("CUDA OOM: failed to allocate training buffers");
    }

    return pufferl;
}

PYBIND11_MODULE(_C, m) {
    assert_static_env_name_matches();

    // Multi-GPU: generate NCCL unique ID (call on rank 0, pass bytes to all ranks)
    m.def("get_nccl_id", []() {
        ncclUniqueId id;
        ncclGetUniqueId(&id);
        return py::bytes(reinterpret_cast<char*>(&id), sizeof(id));
    });
    // Standalone utilization monitor (no PuffeRL instance needed)
    m.def("get_utilization", [](int gpu_id) {
        static bool nvml_inited = false;
        if (!nvml_inited) { nvmlInit(); nvml_inited = true; }

        py::dict util_dict;
        nvmlDevice_t device;
        nvmlDeviceGetHandleByIndex(gpu_id, &device);

        nvmlUtilization_t util;
        nvmlDeviceGetUtilizationRates(device, &util);
        util_dict["gpu_percent"] = (float)util.gpu;

        nvmlMemory_t mem;
        nvmlDeviceGetMemoryInfo(device, &mem);
        util_dict["gpu_mem"] = 100.0f * (float)mem.used / (float)mem.total;

        size_t cuda_free, cuda_total;
        cudaMemGetInfo(&cuda_free, &cuda_total);
        util_dict["vram_used_gb"] = (float)(cuda_total - cuda_free) / (1024.0f * 1024.0f * 1024.0f);
        util_dict["vram_total_gb"] = (float)cuda_total / (1024.0f * 1024.0f * 1024.0f);

        long rss_kb = 0;
        FILE* f = fopen("/proc/self/status", "r");
        if (f) {
            char line[256];
            while (fgets(line, sizeof(line), f)) {
                if (sscanf(line, "VmRSS: %ld", &rss_kb) == 1) break;
            }
            fclose(f);
        }
        util_dict["cpu_mem_gb"] = (float)rss_kb / (1024.0f * 1024.0f);

        return util_dict;
    });

    m.attr("precision_bytes") = (int)sizeof(precision_t);
    m.attr("env_name") = PUFFER_STRINGIFY(ENV_NAME);
    m.attr("static_env_name") = get_static_env_name();
    m.attr("gpu") = 1;

    // Core functions
    m.def("log", &puf_log);
    m.def("eval_log", &puf_eval_log);
    m.def("render", &render);
    m.def("rollouts", &rollouts);
    m.def("train", &train);
    m.def("close", &puf_close);
    m.def("save_weights", &save_weights);
    m.def("load_weights", &load_weights);
    m.def("save_training_state", &save_training_state);
    m.def("load_training_state", &load_training_state);
    m.def("load_anchor_weights", &load_anchor_weights);
    m.def("phase2_init", [](
        py::object pufferl_obj,
        const std::string& demo_dir,
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
    ) -> int {
        PuffeRL& pufferl = pufferl_obj.cast<PuffeRL&>();
        py::gil_scoped_release no_gil;
        return phase2_init_impl(
            pufferl, demo_dir.c_str(), num_atns, snapshot_stride, max_demos,
            seed, normal_start_frac, randomize_rng_frac,
            bc_coef, bc_demos_per_minibatch,
            promote_rate, demote_rate, backstep_ticks, success_q_delta);
    },
    py::arg("pufferl"),
    py::arg("demo_dir"),
    py::arg("num_atns"),
    py::arg("snapshot_stride") = 4,
    py::arg("max_demos") = 64,
    py::arg("seed") = 42,
    py::arg("normal_start_frac") = 0.25f,
    py::arg("randomize_rng_frac") = 0.25f,
    py::arg("bc_coef") = 0.0f,
    py::arg("bc_demos_per_minibatch") = 0,
    py::arg("promote_rate") = 0.30f,
    py::arg("demote_rate") = 0.10f,
    py::arg("backstep_ticks") = 4,
    py::arg("success_q_delta") = 0.005f);
    m.def("phase2_reset", [](py::object pufferl_obj) {
        PuffeRL& pufferl = pufferl_obj.cast<PuffeRL&>();
        py::gil_scoped_release no_gil;
        phase2_reset_impl(pufferl);
    }, py::arg("pufferl"));
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
        .def_readwrite("aurora", &HypersT::aurora)
        .def_readwrite("total_timesteps", &HypersT::total_timesteps)
        .def_readwrite("max_grad_norm", &HypersT::max_grad_norm)
        .def_readwrite("clip_coef", &HypersT::clip_coef)
        .def_readwrite("vf_clip_coef", &HypersT::vf_clip_coef)
        .def_readwrite("vf_coef", &HypersT::vf_coef)
        .def_readwrite("ent_coef", &HypersT::ent_coef)
        .def_readwrite("parent_kl_coef", &HypersT::parent_kl_coef)
        .def_readwrite("parent_kl_log", &HypersT::parent_kl_log)
        .def_readwrite("gamma", &HypersT::gamma)
        .def_readwrite("gae_lambda", &HypersT::gae_lambda)
        .def_readwrite("vtrace_rho_clip", &HypersT::vtrace_rho_clip)
        .def_readwrite("vtrace_c_clip", &HypersT::vtrace_c_clip)
        .def_readwrite("prio_alpha", &HypersT::prio_alpha)
        .def_readwrite("prio_beta0", &HypersT::prio_beta0)
        .def_readwrite("anneal_prio_beta", &HypersT::anneal_prio_beta)
        .def_readwrite("state_buffer_size", &HypersT::state_buffer_size)
        .def_readwrite("cl_frac", &HypersT::cl_frac)
        .def_readwrite("anneal_cl", &HypersT::anneal_cl)
        .def_readwrite("warmup_states", &HypersT::warmup_states)
        .def_readwrite("state_checkpoint_interval", &HypersT::state_checkpoint_interval)
        .def_readwrite("explore_alpha", &HypersT::explore_alpha)
        .def_readwrite("explore_beta", &HypersT::explore_beta)
        .def_readwrite("explore_decay", &HypersT::explore_decay)
        .def_readwrite("terminal_reset_state", &HypersT::terminal_reset_state)
        .def_readwrite("cudagraphs", &HypersT::cudagraphs)
        .def_readwrite("profile", &HypersT::profile)
        .def_readwrite("rank", &HypersT::rank)
        .def_readwrite("world_size", &HypersT::world_size)
        .def_readwrite("gpu_id", &HypersT::gpu_id)
        .def_readwrite("nccl_id", &HypersT::nccl_id);

    py::class_<PrecisionTensor>(m, "PrecisionTensor")
        .def("__repr__", [](const PrecisionTensor& t) { return std::string(puf_repr(&t)); })
        .def("ndim", [](const PrecisionTensor& t) { return ndim(t.shape); })
        .def("numel", [](const PrecisionTensor& t) { return numel(t.shape); });
    py::class_<FloatTensor>(m, "FloatTensor")
        .def("__repr__", [](const FloatTensor& t) { return std::string(puf_repr(&t)); })
        .def("ndim", [](const FloatTensor& t) { return ndim(t.shape); })
        .def("numel", [](const FloatTensor& t) { return numel(t.shape); });

    py::class_<RolloutBuf>(m, "RolloutBuf")
        .def_readwrite("observations", &RolloutBuf::observations)
        .def_readwrite("actions", &RolloutBuf::actions)
        .def_readwrite("values", &RolloutBuf::values)
        .def_readwrite("logprobs", &RolloutBuf::logprobs)
        .def_readwrite("rewards", &RolloutBuf::rewards)
        .def_readwrite("terminals", &RolloutBuf::terminals)
        .def_readwrite("ratio", &RolloutBuf::ratio)
        .def_readwrite("importance", &RolloutBuf::importance);

    m.def("uptime", [](py::object pufferl_obj) -> double {
        PuffeRL& pufferl = pufferl_obj.cast<PuffeRL&>();
        double now = wall_clock();
        return now - pufferl.start_time;
    });
    m.def("puff_advantage", &py_puff_advantage);
    m.def("env_obs_size", []() -> int { return get_obs_size(); });
    m.def("env_num_action_heads", []() -> int { return get_num_atns(); });
    m.def("env_action_dims", []() {
        py::list dims;
        int* sizes = get_act_sizes();
        for (int i = 0; i < get_num_act_sizes(); i++) dims.append(sizes[i]);
        return dims;
    });
    m.def("create_vec", &create_vec, py::arg("args"), py::arg("gpu") = 1);
    py::class_<VecEnv, std::unique_ptr<VecEnv>>(m, "VecEnv")
        .def_readonly("total_agents",  &VecEnv::total_agents)
        .def_readonly("obs_size",      &VecEnv::obs_size)
        .def_readonly("num_atns",      &VecEnv::num_atns)
        .def_readonly("act_sizes",     &VecEnv::act_sizes)
        .def_readonly("obs_dtype",     &VecEnv::obs_dtype)
        .def_readonly("obs_elem_size", &VecEnv::obs_elem_size)
        .def_readonly("gpu",           &VecEnv::gpu)
        // GPU buffer pointers — wrap with torch.from_blob(..., device='cuda')
        .def_property_readonly("gpu_obs_ptr",       [](VecEnv& ve) { return (long long)ve.vec->gpu_observations; })
        .def_property_readonly("gpu_rewards_ptr",   [](VecEnv& ve) { return (long long)ve.vec->gpu_rewards; })
        .def_property_readonly("gpu_terminals_ptr", [](VecEnv& ve) { return (long long)ve.vec->gpu_terminals; })
        // CPU buffer pointers (same as gpu_ in CPU mode since they alias)
        .def_property_readonly("obs_ptr",       [](VecEnv& ve) { return (long long)ve.vec->observations; })
        .def_property_readonly("rewards_ptr",   [](VecEnv& ve) { return (long long)ve.vec->rewards; })
        .def_property_readonly("terminals_ptr", [](VecEnv& ve) { return (long long)ve.vec->terminals; })
        .def("reset", &vec_reset)
        .def("gpu_step", &gpu_vec_step_py)
        .def("cpu_step", &cpu_vec_step_py)
        .def("render", [](VecEnv& ve, int env_id) { static_vec_render(ve.vec, env_id); })
        .def("log",   &vec_log)
        .def("close", &vec_close);

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
            return numel(self.master_weights.shape);
        });
}
