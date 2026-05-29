#include "pufferlib.mm"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <mach/mach.h>
#include <pybind11/numpy.h>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <sstream>
#include <stdexcept>
#include <sys/time.h>
#include <vector>

namespace py = pybind11;

#define _PUFFER_STRINGIFY(x) #x
#define PUFFER_STRINGIFY(x) _PUFFER_STRINGIFY(x)

extern "C" void binding_set_pfsp_weights(
    StaticVec* vec, int* pool, int* cum_weights, int pool_size) __attribute__((weak_import));
extern "C" void binding_get_pfsp_stats(
    StaticVec* vec, float* out_wins, float* out_episodes, int* out_pool_size) __attribute__((weak_import));

static double wall_clock() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec + tv.tv_usec * 1e-6;
}

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

static std::string hash_float_tensor(const FloatTensor& tensor) {
    if (tensor.data == nullptr) return "";
    size_t nbytes = (size_t)puf_numel(tensor.shape) * sizeof(float);
    return hash_host_bytes((const unsigned char*)tensor.data, nbytes);
}

static std::string hash_puf_tensor(const PufTensor& tensor) {
    if (tensor.bytes == nullptr) return "";
    size_t nbytes = (size_t)tensor.numel() * tensor.dtype_size;
    return hash_host_bytes((const unsigned char*)tensor.bytes, nbytes);
}

static py::list hash_puf_rows(const PufTensor& tensor, int row_limit) {
    py::list out;
    if (tensor.bytes == nullptr || tensor.ndim() < 2 || row_limit <= 0) return out;
    int rows = std::min(row_limit, (int)tensor.shape[0]);
    int cols = (int)tensor.shape[1];
    size_t row_bytes = (size_t)cols * tensor.dtype_size;
    const unsigned char* bytes = (const unsigned char*)tensor.bytes;
    for (int r = 0; r < rows; r++) {
        out.append(hash_host_bytes(bytes + (size_t)r * row_bytes, row_bytes));
    }
    return out;
}

static std::string hash_actions_i32(const FloatTensor& tensor) {
    if (tensor.data == nullptr) return "";
    size_t n = (size_t)puf_numel(tensor.shape);
    std::vector<int32_t> ints(n);
    for (size_t i = 0; i < n; i++) {
        ints[i] = (int32_t)lrintf(tensor.data[i]);
    }
    return hash_host_bytes((const unsigned char*)ints.data(), ints.size() * sizeof(int32_t));
}

static std::string hash_vec_action_mask(const StaticVec* vec) {
    if (vec == nullptr || vec->gpu_action_mask == nullptr || vec->action_mask_size <= 0) return "";
    size_t nbytes = (size_t)vec->total_agents * vec->action_mask_size;
    return hash_host_bytes((const unsigned char*)vec->gpu_action_mask, nbytes);
}

static std::string tensor_repr(const FloatTensor& tensor) {
    char buffer[256];
    int position = snprintf(buffer, sizeof(buffer), "FloatTensor([");
    int dims = puf_ndim(tensor.shape);
    for (int i = 0; i < dims && position < (int)sizeof(buffer) - 32; i++) {
        position += snprintf(
            buffer + position,
            sizeof(buffer) - position,
            "%s%lld",
            i ? ", " : "",
            (long long)tensor.shape[i]);
    }
    snprintf(
        buffer + position,
        sizeof(buffer) - position,
        "], %lld elems)",
        (long long)puf_numel(tensor.shape));
    return std::string(buffer);
}

static void assert_static_env_name_matches(void) {
    (void)PUFFER_STRINGIFY(ENV_NAME);
}

static void destroy_dict(Dict* dict) {
    if (!dict) return;
    free(dict->items);
    free(dict);
}

static py::dict get_utilization(int gpu_id) {
    (void)gpu_id;
    py::dict result;

    MetalContext* ctx = mtl_ctx();
    if (ctx->device) {
        uint64_t gpu_budget = [ctx->device recommendedMaxWorkingSetSize];
        uint64_t gpu_current = [ctx->device currentAllocatedSize];
        result["gpu_percent"] = 0.0f;
        if (gpu_budget > 0) {
            result["gpu_mem"] = 100.0f * (float)gpu_current / (float)gpu_budget;
        } else {
            result["gpu_mem"] = 0.0f;
        }
        result["vram_used_gb"] = (float)gpu_current / (1024.0f * 1024.0f * 1024.0f);
        result["vram_total_gb"] = (float)gpu_budget / (1024.0f * 1024.0f * 1024.0f);
    }

    struct mach_task_basic_info info;
    mach_msg_type_number_t count = MACH_TASK_BASIC_INFO_COUNT;
    if (task_info(mach_task_self(), MACH_TASK_BASIC_INFO,
            (task_info_t)&info, &count) == KERN_SUCCESS) {
        result["cpu_mem_gb"] = (float)info.resident_size / (1024.0f * 1024.0f * 1024.0f);
    }

    return result;
}

static py::dict puf_log(py::object pufferl_obj) {
    auto& pufferl = pufferl_obj.cast<PuffeRL&>();
    py::dict result;

    if (pufferl.train_pending) {
        auto wait_start = std::chrono::high_resolution_clock::now();
        sync_pending_train(pufferl);
        float wait_ms = std::chrono::duration<float, std::milli>(
            std::chrono::high_resolution_clock::now() - wait_start).count();
        pufferl.profile.accum[PROF_TRAIN_SYNC] += wait_ms;
    }
    mtl_ensure_stream_synced((cudaStream_t)mtl_stream());

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

    py::dict env_dict;
    Dict* env_out = log_environments_impl(pufferl);
    for (int i = 0; i < env_out->size; i++) {
        env_dict[env_out->items[i].key] = env_out->items[i].value;
    }
    destroy_dict(env_out);
    result["env"] = env_dict;

    py::dict loss_dict;
    float* losses = pufferl.losses_puf.data;
    float n = losses[LOSS_N];
    if (n > 0) {
        float inv_n = 1.0f / n;
        loss_dict["policy"] = losses[LOSS_PG] * inv_n;
        loss_dict["value"] = losses[LOSS_VF] * inv_n;
        loss_dict["entropy"] = losses[LOSS_ENT] * inv_n;
        loss_dict["total"] = losses[LOSS_TOTAL] * inv_n;
        loss_dict["old_kl"] = losses[LOSS_OLD_APPROX_KL] * inv_n;
        loss_dict["kl"] = losses[LOSS_APPROX_KL] * inv_n;
        loss_dict["clipfrac"] = losses[LOSS_CLIPFRAC] * inv_n;
        loss_dict["bc"] = losses[LOSS_BC] * inv_n;
    }
    cudaStream_t loss_stream = pufferl.overlap_enabled
        ? (cudaStream_t)mtl_train_stream()
        : (cudaStream_t)mtl_stream();
    mtl_fill_f32(losses, 0.0f, (int)puf_numel(pufferl.losses_puf.shape), loss_stream);
    result["loss"] = loss_dict;

    py::dict perf_dict;
    float train_ms = 0.0f;
    for (int i = 0; i < NUM_PROF; i++) {
        float sec = pufferl.profile.accum[i] / 1000.0f;
        perf_dict[PROF_NAMES[i]] = sec;
        if (i >= PROF_TRAIN_PRELOOP) {
            train_ms += pufferl.profile.accum[i];
        }
    }
    perf_dict["train"] = train_ms / 1000.0f;
    memset(pufferl.profile.accum, 0, sizeof(pufferl.profile.accum));
    pufferl.rollout_sync_count = 0;
    pufferl.rollout_sync_ms = 0;
    pufferl.train_sync_count = 0;
    pufferl.train_sync_ms = 0;
    result["perf"] = perf_dict;

    result["util"] = get_utilization(0);
    return result;
}

static py::dict puf_eval_log(py::object pufferl_obj) {
    auto& pufferl = pufferl_obj.cast<PuffeRL&>();
    py::dict result;

    double now = wall_clock();
    pufferl.last_log_time = now;
    pufferl.last_log_step = pufferl.global_step;

    py::dict env_dict;
    Dict* env_out = create_dict(64);
    static_vec_eval_log(pufferl.vec, env_out);
    for (int i = 0; i < env_out->size; i++) {
        env_dict[env_out->items[i].key] = env_out->items[i].value;
    }
    destroy_dict(env_out);
    result["env"] = env_dict;
    return result;
}

static void render(py::object pufferl_obj, int env_id) {
    PuffeRL& pufferl = pufferl_obj.cast<PuffeRL&>();
    static_vec_render(pufferl.vec, env_id);
}

static void rollouts(py::object pufferl_obj) {
    PuffeRL& pufferl = pufferl_obj.cast<PuffeRL&>();

    { int count; double ms; mtl_sync_stats(&count, &ms); }

    py::gil_scoped_release no_gil;

    if (pufferl.train_pending) {
        auto wait_start = std::chrono::high_resolution_clock::now();
        sync_pending_train(pufferl);
        float wait_ms = std::chrono::duration<float, std::milli>(
            std::chrono::high_resolution_clock::now() - wait_start).count();
        pufferl.profile.accum[PROF_TRAIN_SYNC] += wait_ms;
    }

    auto t0 = std::chrono::high_resolution_clock::now();
    if (!pufferl.cpu_inference) puf_set_gpu_training(true);
    if (pufferl.hypers.reset_state) {
        for (size_t i = 0; i < pufferl.buffer_states.size(); i++) {
            cudaStream_t stream = i < pufferl.rollout_streams.size() ? pufferl.rollout_streams[i] : 0;
            puf_zero(&pufferl.buffer_states[i], stream);
        }
    }
    static_vec_omp_step(pufferl.vec);
    if (!pufferl.cpu_inference) puf_set_gpu_training(false);
    float sec = std::chrono::duration<float>(
        std::chrono::high_resolution_clock::now() - t0).count();
    pufferl.profile.accum[PROF_ROLLOUT] += sec * 1000.0f;

    float eval_prof[NUM_EVAL_PROF];
    static_vec_read_profile(pufferl.vec, eval_prof);
    pufferl.profile.accum[PROF_EVAL_GPU] += eval_prof[EVAL_GPU];
    pufferl.profile.accum[PROF_EVAL_ENV] += eval_prof[EVAL_ENV_STEP];

    mtl_sync_stats(&pufferl.rollout_sync_count, &pufferl.rollout_sync_ms);
    pufferl.global_step += pufferl.hypers.horizon * pufferl.hypers.total_agents;

}

static py::dict rollout_hashes(py::object pufferl_obj) {
    PuffeRL& pufferl = pufferl_obj.cast<PuffeRL&>();
    if (pufferl.train_pending) {
        sync_pending_train(pufferl);
    }
    if (!pufferl.cpu_inference) {
        mtl_ensure_stream_synced((cudaStream_t)mtl_stream());
    }

    py::dict out;
    out["observations"] = hash_float_tensor(pufferl.rollouts.observations);
    out["actions"] = hash_float_tensor(pufferl.rollouts.actions);
    out["values"] = hash_float_tensor(pufferl.rollouts.values);
    out["logprobs"] = hash_float_tensor(pufferl.rollouts.logprobs);
    out["rewards"] = hash_float_tensor(pufferl.rollouts.rewards);
    out["terminals"] = hash_float_tensor(pufferl.rollouts.terminals);
    out["ratio"] = hash_float_tensor(pufferl.rollouts.ratio);
    out["importance"] = hash_float_tensor(pufferl.rollouts.importance);
    out["action_mask"] = pufferl.has_mask ? hash_float_tensor(pufferl.rollout_masks) : "";
    return out;
}

static py::dict parity_hashes(py::object pufferl_obj) {
    PuffeRL& pufferl = pufferl_obj.cast<PuffeRL&>();
    if (pufferl.train_pending) {
        sync_pending_train(pufferl);
    }
    if (!pufferl.cpu_inference) {
        mtl_ensure_stream_synced((cudaStream_t)mtl_stream());
    }

    py::dict out;
    out["env_observations"] = hash_puf_tensor(pufferl.env.obs);
    out["env_rewards"] = hash_float_tensor(pufferl.env.rewards);
    out["env_terminals"] = hash_float_tensor(pufferl.env.terminals);
    out["env_action_mask"] = hash_vec_action_mask(pufferl.vec);
    out["rollout_actions_i32"] = hash_actions_i32(pufferl.rollouts.actions);
    return out;
}

static py::list float_actions_rows(const FloatTensor& tensor, int rows) {
    py::list out;
    if (tensor.data == nullptr || puf_ndim(tensor.shape) < 3) return out;
    int row_count = std::min(rows, (int)tensor.shape[1]);
    int cols = (int)tensor.shape[2];
    for (int r = 0; r < row_count; r++) {
        py::list row;
        for (int c = 0; c < cols; c++) {
            row.append((int)lrintf(tensor.data[(size_t)r * cols + c]));
        }
        out.append(row);
    }
    return out;
}

static py::list float_actions_rows_range(const FloatTensor& tensor, int start_row, int rows) {
    py::list out;
    if (tensor.data == nullptr || puf_ndim(tensor.shape) < 3 || rows <= 0) return out;
    int total_rows = (int)tensor.shape[1];
    if (start_row < 0 || start_row >= total_rows) return out;
    int row_count = std::min(rows, total_rows - start_row);
    int cols = (int)tensor.shape[2];
    for (int r = 0; r < row_count; r++) {
        py::list row;
        for (int c = 0; c < cols; c++) {
            size_t idx = ((size_t)start_row + r) * cols + c;
            row.append((int)lrintf(tensor.data[idx]));
        }
        out.append(row);
    }
    return out;
}

static py::list precision_rows(const PrecisionTensor& tensor, int rows) {
    py::list out;
    if (tensor.data == nullptr || puf_ndim(tensor.shape) < 2) return out;
    int row_count = std::min(rows, (int)tensor.shape[0]);
    int cols = (int)tensor.shape[1];
    for (int r = 0; r < row_count; r++) {
        py::list row;
        for (int c = 0; c < cols; c++) {
            row.append(tensor.data[(size_t)r * cols + c]);
        }
        out.append(row);
    }
    return out;
}

static py::list precision_rows_range(
        const PrecisionTensor& tensor, int start_row, int rows) {
    py::list out;
    if (tensor.data == nullptr || puf_ndim(tensor.shape) < 2 || rows <= 0) return out;
    int total_rows = (int)tensor.shape[0];
    if (start_row < 0 || start_row >= total_rows) return out;
    int row_count = std::min(rows, total_rows - start_row);
    int cols = (int)tensor.shape[1];
    for (int r = 0; r < row_count; r++) {
        py::list row;
        for (int c = 0; c < cols; c++) {
            size_t idx = ((size_t)start_row + r) * cols + c;
            row.append(tensor.data[idx]);
        }
        out.append(row);
    }
    return out;
}

static py::list puf_rows_f32(const PufTensor& tensor, int rows) {
    py::list out;
    if (tensor.bytes == nullptr || tensor.ndim() < 2) return out;
    int row_count = std::min(rows, (int)tensor.shape[0]);
    int cols = (int)tensor.shape[1];
    for (int r = 0; r < row_count; r++) {
        py::list row;
        for (int c = 0; c < cols; c++) {
            size_t idx = (size_t)r * cols + c;
            if (tensor.dtype_size == sizeof(float)) {
                row.append(((float*)tensor.bytes)[idx]);
            } else {
                row.append((float)((unsigned char*)tensor.bytes)[idx]);
            }
        }
        out.append(row);
    }
    return out;
}

static py::list puf_rows_f32_range(const PufTensor& tensor, int start_row, int rows) {
    py::list out;
    if (tensor.bytes == nullptr || tensor.ndim() < 2 || rows <= 0) return out;
    int total_rows = (int)tensor.shape[0];
    if (start_row < 0 || start_row >= total_rows) return out;
    int row_count = std::min(rows, total_rows - start_row);
    int cols = (int)tensor.shape[1];
    const unsigned char* bytes = (const unsigned char*)tensor.bytes;
    for (int r = 0; r < row_count; r++) {
        py::list row;
        for (int c = 0; c < cols; c++) {
            size_t idx = ((size_t)start_row + r) * cols + c;
            if (tensor.dtype_size == sizeof(float)) {
                row.append(((float*)bytes)[idx]);
            } else {
                row.append((float)bytes[idx]);
            }
        }
        out.append(row);
    }
    return out;
}

static py::dict policy_debug_sample(py::object pufferl_obj) {
    PuffeRL& pufferl = pufferl_obj.cast<PuffeRL&>();
    if (pufferl.train_pending) {
        sync_pending_train(pufferl);
    }
    if (!pufferl.cpu_inference) {
        mtl_ensure_stream_synced((cudaStream_t)mtl_stream());
    }

    py::dict out;
    out["actions"] = float_actions_rows(pufferl.rollouts.actions, 4);
    DecoderActivations* decoder = (DecoderActivations*)pufferl.buffer_activations[0].decoder;
    out["decoder_out"] = precision_rows(decoder->out, 4);
    return out;
}

static py::dict policy_debug_rows(py::object pufferl_obj, int start_row, int rows) {
    PuffeRL& pufferl = pufferl_obj.cast<PuffeRL&>();
    if (pufferl.train_pending) {
        sync_pending_train(pufferl);
    }
    if (!pufferl.cpu_inference) {
        mtl_ensure_stream_synced((cudaStream_t)mtl_stream());
    }

    py::dict out;
    out["actions"] = float_actions_rows_range(pufferl.rollouts.actions, start_row, rows);
    py::list decoder_rows;
    int agents_per_buffer = pufferl.vec->total_agents / pufferl.hypers.num_buffers;
    for (int row = 0; row < rows; row++) {
        int global_row = start_row + row;
        if (global_row < 0 || global_row >= pufferl.vec->total_agents) {
            decoder_rows.append(py::list());
            continue;
        }
        int buf = global_row / agents_per_buffer;
        int local_row = global_row % agents_per_buffer;
        DecoderActivations* decoder = (DecoderActivations*)pufferl.buffer_activations[buf].decoder;
        py::list one = precision_rows_range(decoder->out, local_row, 1);
        if (one.size() > 0) {
            decoder_rows.append(one[0]);
        } else {
            decoder_rows.append(py::list());
        }
    }
    out["decoder_out"] = decoder_rows;
    return out;
}

static py::dict env_debug_sample(py::object pufferl_obj) {
    PuffeRL& pufferl = pufferl_obj.cast<PuffeRL&>();
    if (pufferl.train_pending) {
        sync_pending_train(pufferl);
    }
    if (!pufferl.cpu_inference) {
        mtl_ensure_stream_synced((cudaStream_t)mtl_stream());
    }

    py::dict out;
    out["observations"] = puf_rows_f32(pufferl.env.obs, 1);
    return out;
}

static py::list env_obs_row_hashes(py::object pufferl_obj, int row_limit) {
    PuffeRL& pufferl = pufferl_obj.cast<PuffeRL&>();
    if (pufferl.train_pending) {
        sync_pending_train(pufferl);
    }
    if (!pufferl.cpu_inference) {
        mtl_ensure_stream_synced((cudaStream_t)mtl_stream());
    }

    return hash_puf_rows(pufferl.env.obs, row_limit);
}

static py::list env_obs_rows(py::object pufferl_obj, int start_row, int rows) {
    PuffeRL& pufferl = pufferl_obj.cast<PuffeRL&>();
    if (pufferl.train_pending) {
        sync_pending_train(pufferl);
    }
    if (!pufferl.cpu_inference) {
        mtl_ensure_stream_synced((cudaStream_t)mtl_stream());
    }

    return puf_rows_f32_range(pufferl.env.obs, start_row, rows);
}

static py::dict env_state_debug(py::object pufferl_obj, int row) {
    PuffeRL& pufferl = pufferl_obj.cast<PuffeRL&>();
    if (pufferl.train_pending) {
        sync_pending_train(pufferl);
    }
    if (!pufferl.cpu_inference) {
        mtl_ensure_stream_synced((cudaStream_t)mtl_stream());
    }

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

static py::dict train(py::object pufferl_obj) {
    PuffeRL& pufferl = pufferl_obj.cast<PuffeRL&>();
    { int count; double ms; mtl_sync_stats(&count, &ms); }

    {
        py::gil_scoped_release no_gil;
        train_impl(pufferl);
    }

    mtl_sync_stats(&pufferl.train_sync_count, &pufferl.train_sync_ms);
    return py::dict();
}

static void puf_close(py::object pufferl_obj) {
    PuffeRL& pufferl = pufferl_obj.cast<PuffeRL&>();
    close_impl(pufferl);
}

static void save_weights(py::object pufferl_obj, const std::string& path) {
    PuffeRL& pufferl = pufferl_obj.cast<PuffeRL&>();
    int64_t nbytes = pufferl.alloc_fp32.params.total_elems * sizeof(float);

    sync_pending_train(pufferl);
    mtl_ensure_stream_synced((cudaStream_t)mtl_stream());

    FILE* f = fopen(path.c_str(), "wb");
    if (!f) {
        throw std::runtime_error("Failed to open " + path + " for writing");
    }
    size_t expected = (size_t)nbytes;
    size_t written = fwrite(pufferl.alloc_fp32.params.mem, 1, expected, f);
    int close_result = fclose(f);
    if (written != expected) {
        throw std::runtime_error(
            "Failed to write " + path + ": expected " + std::to_string(expected)
            + " bytes, wrote " + std::to_string(written));
    }
    if (close_result != 0) {
        throw std::runtime_error("Failed to close " + path + " after writing");
    }
}

static void load_weights(py::object pufferl_obj, const std::string& path) {
    PuffeRL& pufferl = pufferl_obj.cast<PuffeRL&>();
    int64_t param_count = pufferl.alloc_fp32.params.total_elems;
    int64_t nbytes = param_count * sizeof(float);

    FILE* f = fopen(path.c_str(), "rb");
    if (!f) {
        throw std::runtime_error("Failed to open " + path + " for reading");
    }
    fseek(f, 0, SEEK_END);
    long file_size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (file_size != nbytes) {
        fclose(f);
        throw std::runtime_error(
            "Weight file size mismatch: expected " + std::to_string(nbytes)
            + " bytes, got " + std::to_string(file_size));
    }

    sync_pending_train(pufferl);
    mtl_ensure_stream_synced((cudaStream_t)mtl_stream());

    size_t nread = fread(pufferl.alloc_fp32.params.mem, 1, nbytes, f);
    if ((int64_t)nread != nbytes) {
        fclose(f);
        throw std::runtime_error("Failed to read weight file");
    }
    fclose(f);

    copy_weights_to_infer(pufferl);
    if (pufferl.train_fp16) {
        cudaStream_t stream = (cudaStream_t)mtl_stream();
        mtl_cast_f32_to_f16(
            pufferl.param_fp16_puf.bytes,
            (const float*)pufferl.alloc_fp32.params.mem,
            (int)param_count,
            stream);
        mtl_ensure_stream_synced(stream);
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

static void save_training_state(py::object pufferl_obj, const std::string& path) {
    PuffeRL& pufferl = pufferl_obj.cast<PuffeRL&>();
    int64_t num_weights = pufferl.alloc_fp32.params.total_elems;
    int64_t nbytes = num_weights * sizeof(float);
    TrainingStateHeader header = {
        .magic = TRAINING_STATE_MAGIC,
        .version = TRAINING_STATE_VERSION,
        .num_weights = num_weights,
        .global_step = pufferl.global_step,
        .epoch = pufferl.epoch,
    };

    sync_pending_train(pufferl);
    mtl_ensure_stream_synced((cudaStream_t)mtl_stream());

    FILE* f = fopen(path.c_str(), "wb");
    if (!f) {
        throw std::runtime_error("Failed to open " + path + " for writing");
    }

    size_t expected = (size_t)nbytes;
    bool ok =
        fwrite(&header, sizeof(header), 1, f) == 1 &&
        fwrite(pufferl.alloc_fp32.params.mem, 1, expected, f) == expected &&
        fwrite(pufferl.muon->mb_puf.data, 1, expected, f) == expected;
    if (fclose(f) != 0 || !ok) {
        throw std::runtime_error("Failed to write training state " + path);
    }
}

static void load_training_state(py::object pufferl_obj, const std::string& path) {
    PuffeRL& pufferl = pufferl_obj.cast<PuffeRL&>();
    int64_t num_weights = pufferl.alloc_fp32.params.total_elems;
    int64_t nbytes = num_weights * sizeof(float);

    FILE* f = fopen(path.c_str(), "rb");
    if (!f) {
        throw std::runtime_error("Failed to open " + path + " for reading");
    }

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

    sync_pending_train(pufferl);
    mtl_ensure_stream_synced((cudaStream_t)mtl_stream());

    size_t expected = (size_t)nbytes;
    bool ok =
        fread(pufferl.alloc_fp32.params.mem, 1, expected, f) == expected &&
        fread(pufferl.muon->mb_puf.data, 1, expected, f) == expected;
    if (fclose(f) != 0 || !ok) {
        throw std::runtime_error("Failed to read training state " + path);
    }

    pufferl.global_step = header.global_step;
    pufferl.epoch = header.epoch;
    pufferl.last_log_step = pufferl.global_step;
    copy_weights_to_infer(pufferl);
    if (pufferl.train_fp16) {
        cudaStream_t stream = (cudaStream_t)mtl_stream();
        mtl_cast_f32_to_f16(
            pufferl.param_fp16_puf.bytes,
            (const float*)pufferl.alloc_fp32.params.mem,
            (int)num_weights,
            stream);
        mtl_ensure_stream_synced(stream);
    }
}

static void load_anchor_weights(
    py::object pufferl_obj,
    const std::string& path,
    double anchor_coef
) {
    PuffeRL& pufferl = pufferl_obj.cast<PuffeRL&>();
    int64_t num_weights = pufferl.alloc_fp32.params.total_elems;
    int64_t nbytes = num_weights * sizeof(float);

    FILE* f = fopen(path.c_str(), "rb");
    if (!f) {
        throw std::runtime_error("Failed to open " + path + " for reading");
    }
    fseek(f, 0, SEEK_END);
    long file_size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (file_size != nbytes) {
        fclose(f);
        throw std::runtime_error(
            "Anchor weight file size mismatch: expected " + std::to_string(nbytes)
            + " bytes, got " + std::to_string(file_size));
    }

    sync_pending_train(pufferl);
    mtl_ensure_stream_synced((cudaStream_t)mtl_stream());

    if (!pufferl.anchor_weights.data) {
        pufferl.anchor_weights = {
            .data = (float*)mtl_alloc_scratch(nbytes),
            .shape = {num_weights},
        };
    }

    size_t expected = (size_t)nbytes;
    size_t nread = fread(pufferl.anchor_weights.data, 1, expected, f);
    if (fclose(f) != 0 || nread != expected) {
        throw std::runtime_error("Failed to read anchor weights " + path);
    }
    pufferl.anchor_coef = (float)anchor_coef;
}

static void py_puff_advantage_cpu(
        long long values_ptr, long long rewards_ptr,
        long long dones_ptr, long long importance_ptr,
        long long advantages_ptr,
        int num_steps, int horizon,
        float gamma, float lambda, float rho_clip, float c_clip) {
    const float* values = (const float*)values_ptr;
    const float* rewards = (const float*)rewards_ptr;
    const float* dones = (const float*)dones_ptr;
    const float* importance = (const float*)importance_ptr;
    float* advantages = (float*)advantages_ptr;

    for (int row = 0; row < num_steps; row++) {
        int offset = row * horizon;
        float last = 0.0f;
        for (int t = horizon - 2; t >= 0; t--) {
            int next_t = t + 1;
            float next_nonterminal = 1.0f - dones[offset + next_t];
            float imp = importance[offset + t];
            float rho_t = imp < rho_clip ? imp : rho_clip;
            float c_t = imp < c_clip ? imp : c_clip;
            bool vector_mode = (horizon % 4) == 0;
            float delta = vector_mode
                ? rho_t * (rewards[offset + next_t] + gamma * values[offset + next_t] * next_nonterminal - values[offset + t])
                : rho_t * rewards[offset + next_t] + gamma * values[offset + next_t] * next_nonterminal - values[offset + t];
            last = delta + gamma * lambda * c_t * last * next_nonterminal;
            advantages[offset + t] = last;
        }
    }
}

static double get_config(py::dict& kwargs, const char* key) {
    if (!kwargs.contains(key)) {
        throw std::invalid_argument(std::string("Missing config key: ") + key);
    }
    try {
        return kwargs[key].cast<double>();
    } catch (const py::cast_error&) {
        throw std::invalid_argument(std::string(key) + " must be numeric");
    }
}

static double get_optional_config(py::dict& kwargs, const char* key, double default_value) {
    if (!kwargs.contains(key)) return default_value;
    return get_config(kwargs, key);
}

static int get_config_int(py::dict& kwargs, const char* key) {
    return mtl_parse_int_config_value(key, get_config(kwargs, key));
}

static int get_config_positive_int(py::dict& kwargs, const char* key) {
    return mtl_validate_positive_config_value(key, get_config_int(kwargs, key));
}

static long get_config_positive_long(py::dict& kwargs, const char* key) {
    double value = get_config(kwargs, key);
    if (!std::isfinite(value)) {
        throw std::invalid_argument(std::string(key) + " must be a finite positive integer");
    }
    double rounded = std::round(value);
    if (rounded <= 0.0) {
        throw std::invalid_argument(std::string(key) + " must be a positive integer");
    }
    if (rounded > (double)std::numeric_limits<long>::max()) {
        throw std::invalid_argument(std::string(key) + " is outside long range");
    }
    return (long)rounded;
}

static uint64_t get_config_uint64(py::dict& kwargs, const char* key) {
    double value = get_config(kwargs, key);
    if (!std::isfinite(value) || std::trunc(value) != value || value < 0.0) {
        throw std::invalid_argument(std::string(key) + " must be a non-negative integer");
    }
    if (value > (double)std::numeric_limits<uint64_t>::max()) {
        throw std::invalid_argument(std::string(key) + " is outside uint64 range");
    }
    return (uint64_t)value;
}

static bool is_python_side_channel_env_key(const char* key) {
    return std::strcmp(key, "record_best_replay_path") == 0 ||
           std::strcmp(key, "play_replay_path") == 0 ||
           std::strcmp(key, "post_240_trace_dir") == 0 ||
           std::strcmp(key, "stall_trace_dir") == 0;
}

static bool metal_env_flag(const char* key) {
    const char* value = std::getenv(key);
    if (!value || value[0] == '\0' || std::strcmp(value, "0") == 0) {
        return false;
    }
    if (std::strcmp(value, "1") == 0) {
        return true;
    }
    throw std::runtime_error(std::string(key) + " must be 0 or 1");
}

static Dict* py_dict_to_c_dict(py::dict py_dict, bool is_env_dict) {
    Dict* c_dict = create_dict(py_dict.size());
    for (auto item : py_dict) {
        const char* key = PyUnicode_AsUTF8(item.first.ptr());
        if (!key) {
            throw std::invalid_argument("Config dict keys must be strings");
        }
        try {
            dict_set(c_dict, key, item.second.cast<double>());
        } catch (const py::cast_error&) {
            if (is_env_dict && is_python_side_channel_env_key(key)) continue;
            throw std::invalid_argument(std::string(key) + " must be numeric");
        }
    }
    return c_dict;
}

static void ensure_env_seed(py::dict args, py::dict env_kwargs) {
    if (!env_kwargs.contains("seed") && args.contains("seed")) {
        env_kwargs["seed"] = args["seed"];
    }
}

struct VecEnv {
    StaticVec* vec;
    int total_agents;
    int obs_size;
    int num_atns;
    std::vector<int> act_sizes;
    std::string obs_dtype;
    size_t obs_elem_size;
};

static std::unique_ptr<VecEnv> create_vec(py::dict args, int gpu = 0) {
    (void)gpu;
    py::dict vec_kwargs = args["vec"].cast<py::dict>();
    py::dict env_kwargs = args["env"].cast<py::dict>();
    ensure_env_seed(args, env_kwargs);

    int total_agents = get_config_positive_int(vec_kwargs, "total_agents");
    int num_buffers = get_config_positive_int(vec_kwargs, "num_buffers");
    mtl_validate_divisible_config_values(
        "total_agents", total_agents, "num_buffers", num_buffers);
    Dict* vec_dict = py_dict_to_c_dict(vec_kwargs, false);
    Dict* env_dict = py_dict_to_c_dict(env_kwargs, true);

    auto ve = std::make_unique<VecEnv>();
    {
        py::gil_scoped_release no_gil;
        ve->vec = create_static_vec(total_agents, num_buffers, 0, vec_dict, env_dict);
    }

    ve->total_agents = total_agents;
    ve->obs_size = get_obs_size();
    ve->num_atns = get_num_atns();
    {
        int* raw = get_act_sizes();
        int n = get_num_act_sizes();
        ve->act_sizes = std::vector<int>(raw, raw + n);
    }
    ve->obs_dtype = std::string(get_obs_dtype());
    ve->obs_elem_size = get_obs_elem_size();
    return ve;
}

static void vec_reset(VecEnv& ve) {
    py::gil_scoped_release no_gil;
    static_vec_reset(ve.vec);
}

static void cpu_vec_step_py(VecEnv& ve, long long actions_ptr) {
    memcpy(
        ve.vec->actions,
        (void*)actions_ptr,
        (size_t)ve.total_agents * ve.num_atns * sizeof(float));
    {
        py::gil_scoped_release no_gil;
        cpu_vec_step(ve.vec);
    }
}

static py::dict vec_log(VecEnv& ve) {
    Dict* out = create_dict(64);
    static_vec_log(ve.vec, out);
    py::dict result;
    for (int i = 0; i < out->size; i++) {
        result[out->items[i].key] = out->items[i].value;
    }
    destroy_dict(out);
    return result;
}

static void vec_close(VecEnv& ve) {
    static_vec_close(ve.vec);
    ve.vec = nullptr;
}

static std::unique_ptr<PuffeRL> create_pufferl(py::dict args) {
    py::dict train_kwargs = args["train"].cast<py::dict>();
    py::dict vec_kwargs = args["vec"].cast<py::dict>();
    py::dict env_kwargs = args["env"].cast<py::dict>();
    ensure_env_seed(args, env_kwargs);
    py::dict policy_kwargs = args["policy"].cast<py::dict>();

    HypersT hypers;
    hypers.total_agents = get_config_positive_int(vec_kwargs, "total_agents");
    hypers.num_buffers = get_config_positive_int(vec_kwargs, "num_buffers");
    hypers.num_threads = get_config_positive_int(vec_kwargs, "num_threads");
    hypers.horizon = get_config_positive_int(train_kwargs, "horizon");
    hypers.hidden_size = get_config_positive_int(policy_kwargs, "hidden_size");
    hypers.num_layers = get_config_positive_int(policy_kwargs, "num_layers");
    hypers.seed = args.contains("seed") ? get_config_uint64(args, "seed")
        : train_kwargs.contains("seed") ? get_config_uint64(train_kwargs, "seed") : 42;
    hypers.lr = get_config(train_kwargs, "learning_rate");
    hypers.min_lr_ratio = get_config(train_kwargs, "min_lr_ratio");
    hypers.anneal_lr = get_config(train_kwargs, "anneal_lr");
    hypers.beta1 = get_config(train_kwargs, "momentum");
    hypers.minibatch_size = get_config_positive_int(train_kwargs, "minibatch_size");
    hypers.replay_ratio = get_config(train_kwargs, "replay_ratio");
    hypers.total_timesteps = get_config_positive_long(train_kwargs, "total_timesteps");
    hypers.max_grad_norm = get_config(train_kwargs, "max_grad_norm");
    hypers.clip_coef = get_config(train_kwargs, "clip_coef");
    hypers.vf_clip_coef = get_config(train_kwargs, "vf_clip_coef");
    hypers.vf_coef = get_config(train_kwargs, "vf_coef");
    hypers.ent_coef = get_config(train_kwargs, "ent_coef");
    bool aurora = get_optional_config(train_kwargs, "aurora", 0.0) > 0;
    if (aurora) {
        throw std::runtime_error("Aurora is implemented only in CUDA");
    }
    float parent_kl_coef = get_optional_config(train_kwargs, "parent_kl_coef", 0.0);
    bool parent_kl_log = get_optional_config(train_kwargs, "parent_kl_log", 0.0) > 0;
    if (parent_kl_coef > 0.0f || parent_kl_log) {
        throw std::runtime_error("parent KL is implemented only in CUDA");
    }
    hypers.gamma = get_config(train_kwargs, "gamma");
    hypers.gae_lambda = get_config(train_kwargs, "gae_lambda");
    hypers.vtrace_rho_clip = get_config(train_kwargs, "vtrace_rho_clip");
    hypers.vtrace_c_clip = get_config(train_kwargs, "vtrace_c_clip");
    hypers.prio_alpha = get_config(train_kwargs, "prio_alpha");
    hypers.prio_beta0 = get_config(train_kwargs, "prio_beta0");
    hypers.anneal_prio_beta = get_config(train_kwargs, "anneal_prio_beta") > 0;
    int state_curriculum_mode =
        (int)get_optional_config(train_kwargs, "state_curriculum_mode", 0.0);
    if (state_curriculum_mode < 0 || state_curriculum_mode > 1) {
        throw std::runtime_error("state_curriculum_mode must be 0 or 1");
    }
    if (state_curriculum_mode != 0 ||
            get_optional_config(train_kwargs, "state_buffer_size", 0.0) > 0.0 ||
            get_optional_config(train_kwargs, "cl_frac", 0.0) > 0.0 ||
            get_optional_config(train_kwargs, "warmup_states", 0.0) > 0.0) {
        throw std::runtime_error("state-buffer curriculum is implemented only in CUDA");
    }
    if ((train_kwargs.contains("gpus") && get_config(train_kwargs, "gpus") > 1.0) ||
            (args.contains("world_size") && get_config(args, "world_size") > 1.0)) {
        throw std::runtime_error("Metal backend does not support multi-GPU");
    }
    hypers.reset_state = args.contains("reset_state") && get_config(args, "reset_state") > 0;
    hypers.terminal_reset_state = get_optional_config(train_kwargs, "terminal_reset_state", 0.0) > 0;
    hypers.profile = train_kwargs.contains("profile") ? get_config(train_kwargs, "profile")
        : args.contains("profile") ? get_config(args, "profile") : 0;
    hypers.eval_action_mode = (int)get_optional_config(args, "eval_action_mode", 0.0);
    if (hypers.eval_action_mode < 0 || hypers.eval_action_mode > 2) {
        throw std::runtime_error("eval_action_mode must be 0, 1, or 2");
    }
    hypers.overlap = metal_env_flag("PUFFER_METAL_OVERLAP");
    hypers.cpu_inference = metal_env_flag("PUFFER_METAL_CPU_INFERENCE");
    hypers.train_fp16 = metal_env_flag("PUFFER_METAL_TRAIN_FP16");
    hypers.sample_mask_in_obs = metal_env_flag("PUFFER_METAL_SAMPLE_MASK_IN_OBS");
    hypers.gpu_id = args.contains("gpu_id") ? get_config_int(args, "gpu_id") : 0;
    mtl_validate_divisible_config_values(
        "total_agents", hypers.total_agents, "num_buffers", hypers.num_buffers);
    mtl_validate_divisible_config_values(
        "minibatch_size", hypers.minibatch_size, "horizon", hypers.horizon);
    long long batch_size_long = (long long)hypers.total_agents * (long long)hypers.horizon;
    if (batch_size_long > (long long)std::numeric_limits<int>::max()) {
        throw std::invalid_argument("total_agents * horizon is outside int range");
    }
    int batch_size = (int)batch_size_long;
    mtl_validate_divisible_config_values(
        "total_agents * horizon", batch_size, "minibatch_size", hypers.minibatch_size);

    std::string env_name = args["env_name"].cast<std::string>();
    Dict* vec_dict = py_dict_to_c_dict(vec_kwargs, false);
    Dict* env_dict = py_dict_to_c_dict(env_kwargs, true);

    std::unique_ptr<PuffeRL> pufferl;
    {
        py::gil_scoped_release no_gil;
        pufferl = create_pufferl_impl(hypers, env_name, vec_dict, env_dict);
    }

    return pufferl;
}

PYBIND11_MODULE(_C, m) {
    assert_static_env_name_matches();

    m.def("get_nccl_id", []() -> py::bytes {
        throw std::runtime_error("Metal backend does not support multi-GPU");
    });
    m.def("get_utilization", &get_utilization);

    m.attr("precision_bytes") = 4;
    m.attr("env_name") = PUFFER_STRINGIFY(ENV_NAME);
    m.attr("static_env_name") = PUFFER_STRINGIFY(ENV_NAME);
    m.attr("gpu") = 0;

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
    m.def("save_training_state", &save_training_state);
    m.def("load_training_state", &load_training_state);
    m.def("load_anchor_weights", &load_anchor_weights);

    /* Self-play multi-bank PFSP entry points. Matches the CUDA backend API so
       pufferlib/selfplay.py works against either backend. */
    m.def("add_frozen_bank", [](py::object pufferl_obj, int slice_size) -> int {
        PuffeRL& pufferl = pufferl_obj.cast<PuffeRL&>();
        return pufferl_add_frozen_bank(&pufferl, slice_size);
    }, py::arg("pufferl"), py::arg("slice_size"));

    m.def("load_frozen_bank", [](py::object pufferl_obj, int bank_idx, const std::string& path) {
        PuffeRL& pufferl = pufferl_obj.cast<PuffeRL&>();
        pufferl_load_frozen_bank(&pufferl, bank_idx, path.c_str());
    }, py::arg("pufferl"), py::arg("bank_idx"), py::arg("path"));

    m.def("set_agent_perm", [](py::object pufferl_obj, py::array_t<int> perm) {
        PuffeRL& pufferl = pufferl_obj.cast<PuffeRL&>();
        auto buf = perm.request();
        if (buf.size != pufferl.vec->total_agents) {
            throw std::runtime_error("set_agent_perm: perm length must equal total_agents");
        }
        pufferl_set_agent_perm(&pufferl, (const int*)buf.ptr);
    }, py::arg("pufferl"), py::arg("perm"));

    m.def("set_env_tags", [](py::object pufferl_obj, py::array_t<int> tags) {
        PuffeRL& pufferl = pufferl_obj.cast<PuffeRL&>();
        auto buf = tags.request();
        if (buf.size != pufferl.vec->size) {
            throw std::runtime_error("set_env_tags: tags length must equal num_envs");
        }
        pufferl_set_env_tags(&pufferl, (const int*)buf.ptr);
    }, py::arg("pufferl"), py::arg("tags"));

    m.def("count_aligned", [](py::object pufferl_obj, int tag_value, int reset_flags) -> int {
        PuffeRL& pufferl = pufferl_obj.cast<PuffeRL&>();
        return pufferl_count_aligned(&pufferl, tag_value, reset_flags);
    }, py::arg("pufferl"), py::arg("tag_value"), py::arg("reset_flags"));

    m.def("num_envs", [](py::object pufferl_obj) -> int {
        PuffeRL& pufferl = pufferl_obj.cast<PuffeRL&>();
        return pufferl_num_envs(&pufferl);
    }, py::arg("pufferl"));

    m.def("set_pfsp_weights", [](py::object pufferl_obj, py::array_t<int> pool,
                                 py::array_t<int> cum_weights) {
        if (!binding_set_pfsp_weights) {
            throw std::runtime_error("set_pfsp_weights: env has no binding_set_pfsp_weights");
        }
        PuffeRL& pufferl = pufferl_obj.cast<PuffeRL&>();
        auto pool_buf = pool.request();
        auto weights_buf = cum_weights.request();
        if (pool_buf.ndim != 1) throw std::runtime_error("pfsp pool must be 1-D");
        if (weights_buf.ndim != 1) throw std::runtime_error("pfsp cum_weights must be 1-D");
        if (pool_buf.shape[0] != weights_buf.shape[0]) {
            throw std::runtime_error("pfsp pool and cum_weights lengths must match");
        }
        binding_set_pfsp_weights(
            pufferl.vec,
            (int*)pool_buf.ptr,
            (int*)weights_buf.ptr,
            (int)pool_buf.shape[0]);
    }, py::arg("pufferl"), py::arg("pool"), py::arg("cum_weights"));

    m.def("get_pfsp_stats", [](py::object pufferl_obj) -> py::dict {
        if (!binding_get_pfsp_stats) {
            throw std::runtime_error("get_pfsp_stats: env has no binding_get_pfsp_stats");
        }
        PuffeRL& pufferl = pufferl_obj.cast<PuffeRL&>();
        const int capacity = 64;
        std::vector<float> wins(capacity, 0.0f);
        std::vector<float> episodes(capacity, 0.0f);
        int pool_size = 0;
        binding_get_pfsp_stats(pufferl.vec, wins.data(), episodes.data(), &pool_size);
        if (pool_size < 0 || pool_size > capacity) {
            throw std::runtime_error("get_pfsp_stats: invalid pool_size");
        }
        py::dict out;
        out["pool_size"] = pool_size;
        out["wins"] = py::array_t<float>(pool_size, wins.data());
        out["episodes"] = py::array_t<float>(pool_size, episodes.data());
        return out;
    }, py::arg("pufferl"));

    m.def("uptime", [](py::object pufferl_obj) -> double {
        PuffeRL& pufferl = pufferl_obj.cast<PuffeRL&>();
        return wall_clock() - pufferl.start_time;
    });

    m.def("puff_advantage_cpu", &py_puff_advantage_cpu);
    m.def("create_vec", &create_vec, py::arg("args"), py::arg("gpu") = 0);

    py::class_<Policy>(m, "Policy");
    py::class_<Muon>(m, "Muon");
    py::class_<Allocator>(m, "Allocator").def(py::init<>());

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
        .def_readwrite("anneal_prio_beta", &HypersT::anneal_prio_beta)
        .def_readwrite("reset_state", &HypersT::reset_state)
        .def_readwrite("terminal_reset_state", &HypersT::terminal_reset_state)
        .def_readwrite("profile", &HypersT::profile)
        .def_readwrite("eval_action_mode", &HypersT::eval_action_mode)
        .def_readwrite("overlap", &HypersT::overlap)
        .def_readwrite("cpu_inference", &HypersT::cpu_inference)
        .def_readwrite("train_fp16", &HypersT::train_fp16)
        .def_readwrite("gpu_id", &HypersT::gpu_id);

    py::class_<FloatTensor>(m, "FloatTensor")
        .def("__repr__", [](const FloatTensor& t) { return tensor_repr(t); })
        .def("ndim", [](const FloatTensor& t) { return puf_ndim(t.shape); })
        .def("numel", [](const FloatTensor& t) { return puf_numel(t.shape); });
    m.attr("PrecisionTensor") = m.attr("FloatTensor");

    py::class_<RolloutBuf>(m, "RolloutBuf")
        .def_readwrite("observations", &RolloutBuf::observations)
        .def_readwrite("actions", &RolloutBuf::actions)
        .def_readwrite("values", &RolloutBuf::values)
        .def_readwrite("logprobs", &RolloutBuf::logprobs)
        .def_readwrite("rewards", &RolloutBuf::rewards)
        .def_readwrite("terminals", &RolloutBuf::terminals)
        .def_readwrite("ratio", &RolloutBuf::ratio)
        .def_readwrite("importance", &RolloutBuf::importance);

    py::class_<VecEnv, std::unique_ptr<VecEnv>>(m, "VecEnv")
        .def_readonly("total_agents", &VecEnv::total_agents)
        .def_readonly("obs_size", &VecEnv::obs_size)
        .def_readonly("num_atns", &VecEnv::num_atns)
        .def_readonly("act_sizes", &VecEnv::act_sizes)
        .def_readonly("obs_dtype", &VecEnv::obs_dtype)
        .def_readonly("obs_elem_size", &VecEnv::obs_elem_size)
        .def_property_readonly("gpu", [](VecEnv&) { return 0; })
        .def_property_readonly("gpu_obs_ptr", [](VecEnv& ve) { return (long long)ve.vec->gpu_observations.data; })
        .def_property_readonly("gpu_rewards_ptr", [](VecEnv& ve) { return (long long)ve.vec->gpu_rewards; })
        .def_property_readonly("gpu_terminals_ptr", [](VecEnv& ve) { return (long long)ve.vec->gpu_terminals; })
        .def_property_readonly("obs_ptr", [](VecEnv& ve) { return (long long)ve.vec->observations.data; })
        .def_property_readonly("rewards_ptr", [](VecEnv& ve) { return (long long)ve.vec->rewards; })
        .def_property_readonly("terminals_ptr", [](VecEnv& ve) { return (long long)ve.vec->terminals; })
        .def("reset", &vec_reset)
        .def("cpu_step", &cpu_vec_step_py)
        .def("render", [](VecEnv& ve, int env_id) { static_vec_render(ve.vec, env_id); })
        .def("log", &vec_log)
        .def("close", &vec_close);

    m.def("env_obs_size", []() -> int { return get_obs_size(); });
    m.def("env_num_action_heads", []() -> int { return get_num_atns(); });
    m.def("env_action_dims", []() {
        py::list dims;
        int* sizes = get_act_sizes();
        for (int i = 0; i < get_num_act_sizes(); i++) dims.append(sizes[i]);
        return dims;
    });
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
        });
}
