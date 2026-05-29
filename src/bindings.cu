// bindings.cpp - Python bindings for pufferlib (torch-free)

#ifdef ENV_BINDING_SRC
#include ENV_BINDING_SRC
#endif

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <pybind11/numpy.h>
#include <algorithm>
#include <iomanip>
#include <cmath>
#include <sstream>
#include <vector>
#include "pufferlib.cu"

#define _PUFFER_STRINGIFY(x) #x
#define PUFFER_STRINGIFY(x) _PUFFER_STRINGIFY(x)

namespace py = pybind11;

static std::string hash_host_bytes(const unsigned char* data, size_t nbytes) {
    uint64_t h = 1469598103934665603ull;
    for (size_t i = 0; i < nbytes; i++) {
        h ^= (uint64_t)data[i];
        h *= 1099511628211ull;
    }
    std::ostringstream out;
    out << std::hex << std::setw(16) << std::setfill('0') << h;
    return out.str();
}

static std::string hash_cuda_bytes(const void* ptr, size_t nbytes) {
    if (ptr == nullptr) return "";
    std::vector<unsigned char> host(nbytes);
    if (nbytes > 0) {
        cudaMemcpy(host.data(), ptr, nbytes, cudaMemcpyDeviceToHost);
    }
    return hash_host_bytes(host.data(), host.size());
}

static std::string hash_precision_tensor(const PrecisionTensor& tensor) {
    if (tensor.data == nullptr) return "";
    return hash_cuda_bytes(tensor.data, (size_t)numel(tensor.shape) * sizeof(precision_t));
}

static std::string hash_float_tensor(const FloatTensor& tensor) {
    if (tensor.data == nullptr) return "";
    return hash_cuda_bytes(tensor.data, (size_t)numel(tensor.shape) * sizeof(float));
}

static std::string hash_byte_tensor(const ByteTensor& tensor) {
    if (tensor.data == nullptr) return "";
    return hash_cuda_bytes(tensor.data, (size_t)numel(tensor.shape));
}

static std::string hash_obs_tensor(const OBS_TENSOR_T& tensor) {
    if (tensor.data == nullptr) return "";
    return hash_cuda_bytes(tensor.data, (size_t)numel(tensor.shape) * get_obs_elem_size());
}

static py::list hash_obs_rows(const OBS_TENSOR_T& tensor, int row_limit) {
    py::list out;
    if (tensor.data == nullptr || ndim(tensor.shape) < 2 || row_limit <= 0) return out;
    int rows = std::min(row_limit, (int)tensor.shape[0]);
    int cols = (int)tensor.shape[1];
    size_t elem_size = get_obs_elem_size();
    size_t row_bytes = (size_t)cols * elem_size;
    std::vector<unsigned char> host((size_t)rows * row_bytes);
    cudaMemcpy(host.data(), tensor.data, host.size(), cudaMemcpyDeviceToHost);
    for (int r = 0; r < rows; r++) {
        out.append(hash_host_bytes(host.data() + (size_t)r * row_bytes, row_bytes));
    }
    return out;
}

static std::string hash_precision_tensor_i32(const PrecisionTensor& tensor) {
    if (tensor.data == nullptr) return "";
    size_t n = (size_t)numel(tensor.shape);
    std::vector<precision_t> host(n);
    if (n > 0) {
        cudaMemcpy(host.data(), tensor.data, n * sizeof(precision_t), cudaMemcpyDeviceToHost);
    }

    std::vector<int32_t> ints(n);
    for (size_t i = 0; i < n; i++) {
        ints[i] = (int32_t)lrintf(to_float(host[i]));
    }
    return hash_host_bytes((const unsigned char*)ints.data(), ints.size() * sizeof(int32_t));
}

py::dict rollout_hashes(py::object pufferl_obj) {
    auto& pufferl = pufferl_obj.cast<PuffeRL&>();
    py::dict out;
    out["observations"] = hash_precision_tensor(pufferl.rollouts.observations);
    out["actions"] = hash_precision_tensor(pufferl.rollouts.actions);
    out["values"] = hash_precision_tensor(pufferl.rollouts.values);
    out["logprobs"] = hash_precision_tensor(pufferl.rollouts.logprobs);
    out["rewards"] = hash_precision_tensor(pufferl.rollouts.rewards);
    out["terminals"] = hash_precision_tensor(pufferl.rollouts.terminals);
    out["ratio"] = hash_precision_tensor(pufferl.rollouts.ratio);
    out["importance"] = hash_precision_tensor(pufferl.rollouts.importance);
    out["action_mask"] = hash_precision_tensor(pufferl.rollouts.action_mask);
    return out;
}

py::dict parity_hashes(py::object pufferl_obj) {
    auto& pufferl = pufferl_obj.cast<PuffeRL&>();
    py::dict out;
    out["env_observations"] = hash_obs_tensor(pufferl.env.obs);
    out["env_rewards"] = hash_float_tensor(pufferl.env.rewards);
    out["env_terminals"] = hash_float_tensor(pufferl.env.terminals);
    out["env_action_mask"] = hash_byte_tensor(pufferl.env.action_mask);
    out["rollout_actions_i32"] = hash_precision_tensor_i32(pufferl.rollouts.actions);
    return out;
}

static py::list precision_actions_rows(const PrecisionTensor& tensor, int rows) {
    py::list out;
    if (tensor.data == nullptr || ndim(tensor.shape) < 3) return out;
    int row_count = std::min(rows, (int)tensor.shape[1]);
    int cols = (int)tensor.shape[2];
    size_t n = (size_t)row_count * cols;
    std::vector<precision_t> host(n);
    if (n > 0) {
        cudaMemcpy(host.data(), tensor.data, n * sizeof(precision_t), cudaMemcpyDeviceToHost);
    }
    for (int r = 0; r < row_count; r++) {
        py::list row;
        for (int c = 0; c < cols; c++) {
            row.append((int)lrintf(to_float(host[(size_t)r * cols + c])));
        }
        out.append(row);
    }
    return out;
}

static py::list precision_actions_rows_range(
        const PrecisionTensor& tensor, int start_row, int rows) {
    py::list out;
    if (tensor.data == nullptr || ndim(tensor.shape) < 3 || rows <= 0) return out;
    int total_rows = (int)tensor.shape[1];
    if (start_row < 0 || start_row >= total_rows) return out;
    int row_count = std::min(rows, total_rows - start_row);
    int cols = (int)tensor.shape[2];
    size_t n = (size_t)row_count * cols;
    std::vector<precision_t> host(n);
    if (n > 0) {
        cudaMemcpy(
            host.data(),
            tensor.data + (size_t)start_row * cols,
            n * sizeof(precision_t),
            cudaMemcpyDeviceToHost);
    }
    for (int r = 0; r < row_count; r++) {
        py::list row;
        for (int c = 0; c < cols; c++) {
            row.append((int)lrintf(to_float(host[(size_t)r * cols + c])));
        }
        out.append(row);
    }
    return out;
}

static py::list precision_rows_f32(const PrecisionTensor& tensor, int rows) {
    py::list out;
    if (tensor.data == nullptr || ndim(tensor.shape) < 2) return out;
    int row_count = std::min(rows, (int)tensor.shape[0]);
    int cols = (int)tensor.shape[1];
    size_t n = (size_t)row_count * cols;
    std::vector<precision_t> host(n);
    if (n > 0) {
        cudaMemcpy(host.data(), tensor.data, n * sizeof(precision_t), cudaMemcpyDeviceToHost);
    }
    for (int r = 0; r < row_count; r++) {
        py::list row;
        for (int c = 0; c < cols; c++) {
            row.append(to_float(host[(size_t)r * cols + c]));
        }
        out.append(row);
    }
    return out;
}

static py::list precision_rows_f32_range(
        const PrecisionTensor& tensor, int start_row, int rows) {
    py::list out;
    if (tensor.data == nullptr || ndim(tensor.shape) < 2 || rows <= 0) return out;
    int total_rows = (int)tensor.shape[0];
    if (start_row < 0 || start_row >= total_rows) return out;
    int row_count = std::min(rows, total_rows - start_row);
    int cols = (int)tensor.shape[1];
    size_t n = (size_t)row_count * cols;
    std::vector<precision_t> host(n);
    if (n > 0) {
        cudaMemcpy(
            host.data(),
            tensor.data + (size_t)start_row * cols,
            n * sizeof(precision_t),
            cudaMemcpyDeviceToHost);
    }
    for (int r = 0; r < row_count; r++) {
        py::list row;
        for (int c = 0; c < cols; c++) {
            row.append(to_float(host[(size_t)r * cols + c]));
        }
        out.append(row);
    }
    return out;
}

static py::list obs_rows_f32(const OBS_TENSOR_T& tensor, int rows) {
    py::list out;
    if (tensor.data == nullptr || ndim(tensor.shape) < 2) return out;
    int row_count = std::min(rows, (int)tensor.shape[0]);
    int cols = (int)tensor.shape[1];
    size_t n = (size_t)row_count * cols;
    size_t elem_size = get_obs_elem_size();
    std::vector<unsigned char> host(n * elem_size);
    if (n > 0) {
        cudaMemcpy(host.data(), tensor.data, host.size(), cudaMemcpyDeviceToHost);
    }
    for (int r = 0; r < row_count; r++) {
        py::list row;
        for (int c = 0; c < cols; c++) {
            size_t idx = (size_t)r * cols + c;
            if (elem_size == sizeof(float)) {
                row.append(((float*)host.data())[idx]);
            } else {
                row.append((float)host[idx]);
            }
        }
        out.append(row);
    }
    return out;
}

static py::list obs_rows_f32_range(const OBS_TENSOR_T& tensor, int start_row, int rows) {
    py::list out;
    if (tensor.data == nullptr || ndim(tensor.shape) < 2 || rows <= 0) return out;
    int total_rows = (int)tensor.shape[0];
    if (start_row < 0 || start_row >= total_rows) return out;
    int row_count = std::min(rows, total_rows - start_row);
    int cols = (int)tensor.shape[1];
    size_t elem_size = get_obs_elem_size();
    size_t n = (size_t)row_count * cols;
    std::vector<unsigned char> host(n * elem_size);
    if (n > 0) {
        cudaMemcpy(
            host.data(),
            tensor.data + (size_t)start_row * cols,
            host.size(),
            cudaMemcpyDeviceToHost);
    }
    for (int r = 0; r < row_count; r++) {
        py::list row;
        for (int c = 0; c < cols; c++) {
            size_t idx = (size_t)r * cols + c;
            if (elem_size == sizeof(float)) {
                row.append(((float*)host.data())[idx]);
            } else {
                row.append((float)host[idx]);
            }
        }
        out.append(row);
    }
    return out;
}

py::dict policy_debug_sample(py::object pufferl_obj) {
    auto& pufferl = pufferl_obj.cast<PuffeRL&>();
    py::dict out;
    out["actions"] = precision_actions_rows(pufferl.rollouts.actions, 4);
    DecoderActivations* decoder = (DecoderActivations*)pufferl.buffer_activations[0].decoder;
    out["decoder_out"] = precision_rows_f32(decoder->out, 4);
    return out;
}

py::dict policy_debug_rows(py::object pufferl_obj, int start_row, int rows) {
    auto& pufferl = pufferl_obj.cast<PuffeRL&>();
    py::dict out;
    out["actions"] = precision_actions_rows_range(pufferl.rollouts.actions, start_row, rows);
    DecoderActivations* decoder = (DecoderActivations*)pufferl.buffer_activations[0].decoder;
    out["decoder_out"] = precision_rows_f32_range(decoder->out, start_row, rows);
    return out;
}

py::dict env_debug_sample(py::object pufferl_obj) {
    auto& pufferl = pufferl_obj.cast<PuffeRL&>();
    py::dict out;
    out["observations"] = obs_rows_f32(pufferl.env.obs, 1);
    return out;
}

py::list env_obs_row_hashes(py::object pufferl_obj, int row_limit) {
    auto& pufferl = pufferl_obj.cast<PuffeRL&>();
    return hash_obs_rows(pufferl.env.obs, row_limit);
}

py::list env_obs_rows(py::object pufferl_obj, int start_row, int rows) {
    auto& pufferl = pufferl_obj.cast<PuffeRL&>();
    return obs_rows_f32_range(pufferl.env.obs, start_row, rows);
}

py::dict env_state_debug(py::object pufferl_obj, int row) {
    auto& pufferl = pufferl_obj.cast<PuffeRL&>();
    py::dict out;
#ifdef INF_TOTAL_OBS
    if (row < 0 || row >= pufferl.vec->size) return out;
    Env* env = &pufferl.vec->envs[row];
    InfernoState* s = (InfernoState*)INF_ENV_STATE(env);
    out["tick"] = s->tick;
    out["wave"] = s->wave;
    out["rng_state"] = s->rng_state;
    out["player_x"] = s->player.x;
    out["player_y"] = s->player.y;
    out["player_hp"] = s->player.current_hitpoints;
    py::list sparks;
    for (int i = 0; i < INF_MAX_PENDING_SPARKS; i++) {
        const InfPendingSpark* spark = &s->pending_sparks[i];
        if (!spark->active) continue;
        py::dict item;
        item["slot"] = i;
        item["x"] = spark->x;
        item["y"] = spark->y;
        item["src_x"] = spark->src_x;
        item["src_y"] = spark->src_y;
        item["damage"] = spark->damage;
        item["ticks_remaining"] = spark->ticks_remaining;
        sparks.append(item);
    }
    out["pending_sparks"] = sparks;
#endif
    return out;
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
    // Capacity 64 to fit chess's per-bank hist_score_bank/hist_n_bank entries
    // (16 keys across 8 banks) on top of base env-log fields.
    Dict* env_out = create_dict(64);
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

    // Zero state buffers (primary + every frozen bank, so all banks see fresh
    // state symmetrically — otherwise frozen banks accumulate indefinitely while
    // primary resets, giving primary an unfair in-distribution advantage).
    if (pufferl.hypers.reset_state) {
        for (int i = 0; i < pufferl.hypers.num_buffers; i++) {
            puf_zero(&pufferl.buffer_states[i], pufferl.default_stream);
        }
        for (int b = 0; b < pufferl.num_frozen_banks; b++) {
            for (int i = 0; i < pufferl.hypers.num_buffers; i++) {
                puf_zero(&pufferl.frozen_banks[b].buffer_states[i], pufferl.default_stream);
            }
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
    cudaMemcpy(buf.data(), pufferl.master_weights.data, nbytes, cudaMemcpyDeviceToHost);
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
    cudaMemcpy(pufferl.master_weights.data, buf.data(), nbytes, cudaMemcpyHostToDevice);
    if (USE_BF16) {
        int n = numel(pufferl.param_puf.shape);
        cast<<<grid_size(n), BLOCK_SIZE, 0, pufferl.default_stream>>>(
            pufferl.param_puf.data, pufferl.master_weights.data, n);
    }
}

int py_add_frozen_bank(py::object pufferl_obj, int slice_size,
                       int hidden_size, int num_layers) {
    PuffeRL& pufferl = pufferl_obj.cast<PuffeRL&>();
    return pufferl_add_frozen_bank(&pufferl, slice_size, hidden_size, num_layers);
}

void py_load_frozen_bank(py::object pufferl_obj, int bank_idx, const std::string& path) {
    PuffeRL& pufferl = pufferl_obj.cast<PuffeRL&>();
    pufferl_load_frozen_bank(&pufferl, bank_idx, path.c_str());
}

void py_set_agent_perm(py::object pufferl_obj, py::array_t<int> perm) {
    PuffeRL& pufferl = pufferl_obj.cast<PuffeRL&>();
    auto buf = perm.request();
    if (buf.ndim != 1) throw std::runtime_error("agent_perm must be 1-D");
    if ((int)buf.shape[0] != pufferl.vec->total_agents) {
        throw std::runtime_error("agent_perm length must equal total_agents");
    }
    pufferl_set_agent_perm(&pufferl, (const int*)buf.ptr);
}

void py_set_env_tags(py::object pufferl_obj, py::array_t<int> tags) {
    PuffeRL& pufferl = pufferl_obj.cast<PuffeRL&>();
    auto buf = tags.request();
    if (buf.ndim != 1) throw std::runtime_error("env_tags must be 1-D");
    int num_envs = pufferl_num_envs(&pufferl);
    if ((int)buf.shape[0] != num_envs) {
        throw std::runtime_error("env_tags length must equal num_envs");
    }
    pufferl_set_env_tags(&pufferl, (const int*)buf.ptr);
}

int py_count_aligned(py::object pufferl_obj, int tag_value, int reset_flags) {
    PuffeRL& pufferl = pufferl_obj.cast<PuffeRL&>();
    return pufferl_count_aligned(&pufferl, tag_value, reset_flags);
}

int py_num_envs(py::object pufferl_obj) {
    PuffeRL& pufferl = pufferl_obj.cast<PuffeRL&>();
    return pufferl_num_envs(&pufferl);
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
    if (!kwargs.contains(key)) return default_value;
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
// Python-facing VecEnv: wraps StaticVec for use from python_pufferl.py.
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
    Dict* out = create_dict(32);
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
    hypers.momentum = get_config(train_kwargs, "momentum");
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
    hypers.min_ent_coef_ratio = get_config(train_kwargs, "min_ent_coef_ratio");
    hypers.anneal_ent_coef = get_config(train_kwargs, "anneal_ent_coef");
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
    hypers.state_buffer_size = get_config(train_kwargs, "state_buffer_size");
    hypers.cl_frac = get_config(train_kwargs, "cl_frac");
    hypers.anneal_cl = get_config(train_kwargs, "anneal_cl");
    hypers.warmup_states = get_config(train_kwargs, "warmup_states");
    hypers.state_checkpoint_interval = get_config(train_kwargs, "state_checkpoint_interval");
    hypers.explore_alpha = get_config(train_kwargs, "explore_alpha");
    hypers.explore_beta = get_config(train_kwargs, "explore_beta");
    hypers.explore_decay = get_config(train_kwargs, "explore_decay");
    hypers.reset_state = get_config(args, "reset_state");
    // Base-level config ([base] section becomes top-level in args)
    hypers.cudagraphs = get_config(args, "cudagraphs");
    hypers.profile = get_config(args, "profile");
    hypers.eval_action_mode = (int)get_optional_config(args, "eval_action_mode", 0.0);
    if (hypers.eval_action_mode < 0 || hypers.eval_action_mode > 2) {
        throw std::runtime_error("eval_action_mode must be 0, 1, or 2");
    }
    if (hypers.eval_action_mode == 2 && hypers.cudagraphs >= 0) {
        throw std::runtime_error("eval_action_mode=2 requires cudagraphs=-1");
    }
    // Multi-GPU / device selection
    hypers.rank = get_config(args, "rank");
    hypers.world_size = get_config(args, "world_size");
    hypers.gpu_id = get_config(args, "gpu_id");
    hypers.nccl_id = args["nccl_id"].cast<std::string>();
    // Seed
    hypers.seed = get_config(args, "seed");

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
        throw std::runtime_error("OOM: failed to allocate training or curriculum state buffers");
    }

    return pufferl;
}

PYBIND11_MODULE(_C, m) {
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
    m.attr("gpu") = 1;

    // Core functions
    m.def("log", &puf_log);
    m.def("eval_log", &puf_eval_log);
    m.def("render", &render);
    m.def("rollouts", &rollouts);
    m.def("rollout_hashes", &rollout_hashes);
    m.def("parity_hashes", &parity_hashes);
    m.def("policy_debug_sample", &policy_debug_sample);
    m.def("policy_debug_rows", &policy_debug_rows);
    m.def("env_debug_sample", &env_debug_sample);
    m.def("env_obs_row_hashes", &env_obs_row_hashes);
    m.def("env_obs_rows", &env_obs_rows);
    m.def("env_state_debug", &env_state_debug);
    m.def("train", &train);
    m.def("close", &puf_close);
    m.def("save_weights", &save_weights);
    m.def("load_weights", &load_weights);
    m.def("add_frozen_bank", &py_add_frozen_bank);
    m.def("load_frozen_bank", &py_load_frozen_bank);
    m.def("set_agent_perm", &py_set_agent_perm);
    m.def("set_env_tags", &py_set_env_tags);
    m.def("count_aligned", &py_count_aligned);
    m.def("num_envs", &py_num_envs);
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
        .def_readwrite("momentum", &HypersT::momentum)
        .def_readwrite("total_timesteps", &HypersT::total_timesteps)
        .def_readwrite("max_grad_norm", &HypersT::max_grad_norm)
        .def_readwrite("clip_coef", &HypersT::clip_coef)
        .def_readwrite("vf_clip_coef", &HypersT::vf_clip_coef)
        .def_readwrite("vf_coef", &HypersT::vf_coef)
        .def_readwrite("ent_coef", &HypersT::ent_coef)
        .def_readwrite("min_ent_coef_ratio", &HypersT::min_ent_coef_ratio)
        .def_readwrite("anneal_ent_coef", &HypersT::anneal_ent_coef)
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
        .def_readwrite("cudagraphs", &HypersT::cudagraphs)
        .def_readwrite("profile", &HypersT::profile)
        .def_readwrite("eval_action_mode", &HypersT::eval_action_mode)
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
    m.def("create_vec", &create_vec, py::arg("args"), py::arg("gpu") = 1);
    m.def("env_obs_size", []() -> int { return get_obs_size(); });
    m.def("env_num_action_heads", []() -> int { return get_num_atns(); });
    m.def("env_action_dims", []() {
        py::list dims;
        int* sizes = get_act_sizes();
        for (int i = 0; i < get_num_act_sizes(); i++) dims.append(sizes[i]);
        return dims;
    });
    py::class_<VecEnv, std::unique_ptr<VecEnv>>(m, "VecEnv")
        .def_readonly("total_agents",  &VecEnv::total_agents)
        .def_readonly("obs_size",      &VecEnv::obs_size)
        .def_readonly("num_atns",      &VecEnv::num_atns)
        .def_readonly("act_sizes",     &VecEnv::act_sizes)
        .def_readonly("obs_dtype",     &VecEnv::obs_dtype)
        .def_readonly("obs_elem_size", &VecEnv::obs_elem_size)
        .def_readonly("gpu",           &VecEnv::gpu)
        // GPU buffer pointers — wrap with torch.from_blob(..., device='cuda')
        .def_property_readonly("gpu_obs_ptr",       [](VecEnv& ve) { return (long long)ve.vec->gpu_observations.data; })
        .def_property_readonly("gpu_rewards_ptr",   [](VecEnv& ve) { return (long long)ve.vec->gpu_rewards; })
        .def_property_readonly("gpu_terminals_ptr", [](VecEnv& ve) { return (long long)ve.vec->gpu_terminals; })
        // CPU buffer pointers (same as gpu_ in CPU mode since they alias)
        .def_property_readonly("obs_ptr",       [](VecEnv& ve) { return (long long)ve.vec->observations.data; })
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
