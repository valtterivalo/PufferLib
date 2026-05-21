#ifdef PUFFER_CURRICULUM_TYPES

struct StatePrioBuffers {
    FloatTensor prio_probs;
    FloatTensor cdf;
    IntTensor idx;
};

struct StateBuffer {
    void* states;
    void* candidate_states;
    float* priorities;
    precision_t* priorities_host;
    precision_t* env_scores_host;
    int* heap;
    int* heap_pos;
    int capacity;
    int size;
    int state_size;
    int num_envs;
    int num_checkpoints;
    int checkpoint_interval;
    int candidate_capacity;
    int agents_per_env;
    int num_cl_envs;
    int num_fresh_envs;
    int* env_state_inds_host;
    PrecisionTensor advantages;
    PrecisionTensor env_scores;
    PrecisionTensor importance;
    StatePrioBuffers prio_bufs;
};

void register_state_prio_buffers(StatePrioBuffers& bufs, Allocator* alloc,
        int capacity, int num_cl_envs) {
    bufs = (StatePrioBuffers){
        .prio_probs = {.shape = {capacity}},
        .cdf = {.shape = {capacity}},
        .idx = {.shape = {num_cl_envs}},
    };
    alloc_register(alloc, &bufs.prio_probs);
    alloc_register(alloc, &bufs.cdf);
    alloc_register(alloc, &bufs.idx);
}

void register_state_buffer(StateBuffer* buf, Allocator* alloc,
        int capacity, int total_agents, int num_envs, int agents_per_env,
        int num_cl_envs, int horizon, int checkpoint_interval) {
    buf->capacity = capacity;
    buf->size = 0;
    buf->state_size = 0;
    buf->num_envs = num_envs;
    buf->checkpoint_interval = checkpoint_interval;
    buf->num_checkpoints = (horizon + checkpoint_interval - 1) / checkpoint_interval;
    buf->candidate_capacity = num_envs * buf->num_checkpoints;
    buf->agents_per_env = agents_per_env;
    buf->num_cl_envs = num_cl_envs;
    buf->num_fresh_envs = num_envs - num_cl_envs;
    buf->advantages = {.shape = {capacity}};
    buf->env_scores = {.shape = {buf->candidate_capacity}};
    buf->importance = {.shape = {total_agents}};
    alloc_register(alloc, &buf->advantages);
    alloc_register(alloc, &buf->env_scores);
    alloc_register(alloc, &buf->importance);
    if (num_cl_envs > 0) {
        register_state_prio_buffers(buf->prio_bufs, alloc, capacity, num_cl_envs);
    }
}

int init_state_buffer(StateBuffer* buf, int total_agents) {
    (void)total_agents;
    buf->state_size = get_state_size();
    size_t capacity = (size_t)buf->capacity;
    size_t state_size = (size_t)buf->state_size;
    if (state_size == 0 || capacity > ((size_t)-1) / state_size) {
        fprintf(stderr, "Failed to allocate curriculum state buffer: invalid size\n");
        return 0;
    }

    size_t state_bytes = capacity * state_size;
    size_t candidate_bytes = (size_t)buf->candidate_capacity * state_size;
    buf->states = malloc(state_bytes);
    buf->candidate_states = malloc(candidate_bytes);
    buf->priorities = (float*)malloc(capacity * sizeof(float));
    buf->priorities_host = (precision_t*)malloc(capacity * sizeof(precision_t));
    buf->env_scores_host = (precision_t*)malloc(
        (size_t)buf->candidate_capacity * sizeof(precision_t));
    buf->heap = (int*)malloc(capacity * sizeof(int));
    buf->heap_pos = (int*)malloc(capacity * sizeof(int));
    buf->env_state_inds_host = (int*)malloc((size_t)buf->num_envs * sizeof(int));
    if (buf->states == NULL || buf->candidate_states == NULL ||
            buf->priorities == NULL || buf->priorities_host == NULL ||
            buf->env_scores_host == NULL || buf->heap == NULL ||
            buf->heap_pos == NULL || buf->env_state_inds_host == NULL) {
        fprintf(stderr,
            "Failed to allocate curriculum state buffer: capacity=%d state_size=%d bytes=%zu\n",
            buf->capacity, buf->state_size, state_bytes);
        free(buf->states);
        free(buf->candidate_states);
        free(buf->priorities);
        free(buf->priorities_host);
        free(buf->env_scores_host);
        free(buf->heap);
        free(buf->heap_pos);
        free(buf->env_state_inds_host);
        return 0;
    }
    return 1;
}

void close_state_buffer(StateBuffer* buf) {
    free(buf->states);
    free(buf->candidate_states);
    free(buf->priorities);
    free(buf->priorities_host);
    free(buf->env_scores_host);
    free(buf->heap);
    free(buf->heap_pos);
    free(buf->env_state_inds_host);
    buf->states = NULL;
    buf->candidate_states = NULL;
    buf->priorities = NULL;
    buf->priorities_host = NULL;
    buf->env_scores_host = NULL;
    buf->heap = NULL;
    buf->heap_pos = NULL;
    buf->env_state_inds_host = NULL;
}

#endif

#ifdef PUFFER_CURRICULUM_IMPL

__global__ void compute_state_prio_abs(
        const precision_t* __restrict__ advantages,
        float* __restrict__ prio_weights,
        float prio_alpha, int length) {
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx >= length) return;

    float adv = fabsf(to_float(advantages[idx]));
    float pw;
    if (prio_alpha == 0.0f) {
        pw = 1.0f;
    } else if (adv == 0.0f) {
        pw = 0.0f;
    } else {
        pw = __powf(adv, prio_alpha);
    }
    if (isnan(pw) || isinf(pw)) pw = 0.0f;
    prio_weights[idx] = pw;
}

__global__ void build_curriculum_state_importance(
        precision_t* __restrict__ out,
        const int* __restrict__ state_inds,
        const float* __restrict__ prio_probs,
        int fresh_agents, int num_cl_envs, int agents_per_env,
        int buffer_size, float beta) {
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    int total = num_cl_envs * agents_per_env;
    if (idx >= total) return;

    int env_idx = idx / agents_per_env;
    int state_idx = state_inds[env_idx];
    float value = prio_probs[state_idx] * (float)buffer_size;
    float weight = __powf(value, -beta);
    if (isnan(weight) || isinf(weight)) weight = 1.0f;
    out[fresh_agents + idx] = from_float(weight);
}

__global__ void compute_curriculum_checkpoint_scores(
        precision_t* __restrict__ dst,
        const precision_t* __restrict__ advantages_bt,
        int num_fresh_envs, int num_cl_envs,
        int num_checkpoints, int checkpoint_interval,
        int agents_per_env, int horizon) {
    int row = blockIdx.x * blockDim.x + threadIdx.x;
    int fresh_rows = num_checkpoints * num_fresh_envs;
    int total_rows = fresh_rows + num_cl_envs;
    if (row >= total_rows) return;

    int env_idx;
    int start_t = 0;
    int end_t = horizon;
    if (row < fresh_rows) {
        int checkpoint_idx = row / num_fresh_envs;
        env_idx = row - checkpoint_idx * num_fresh_envs;
        start_t = checkpoint_idx * checkpoint_interval;
        end_t = start_t + checkpoint_interval;
        if (end_t > horizon) end_t = horizon;
    } else {
        int replay_idx = row - fresh_rows;
        env_idx = num_fresh_envs + replay_idx;
    }

    int agent_start = env_idx * agents_per_env;
    float sum_agent_abs = 0.0f;
    for (int a = 0; a < agents_per_env; a++) {
        int offset = (agent_start + a) * horizon;
        float agent_abs = 0.0f;
        for (int t = start_t; t < end_t; t++) {
            agent_abs += fabsf(to_float(advantages_bt[offset + t]));
        }
        sum_agent_abs += agent_abs;
    }
    dst[row] = from_float(sum_agent_abs / (float)agents_per_env);
}

static inline int clamp_int(int v, int lo, int hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

static int fixed_agents_per_env(StaticVec* vec) {
    assert(static_vec_has_state(vec) &&
        "state_buffer_size > 0 requires PUFFER_STATE_T env support");
    assert(get_state_size() > 0 &&
        "state_buffer_size > 0 requires nonzero env state size");
    assert(vec->size > 0 && "state curriculum requires at least one env");
    int agents_per_env = static_vec_fixed_agents_per_env(vec);
    assert(agents_per_env > 0 &&
        "state curriculum currently requires fixed positive env agent count");
    assert(vec->agents_per_buffer % agents_per_env == 0 &&
        "state curriculum requires agents_per_buffer divisible by env agent count");
    return agents_per_env;
}

static inline void* state_slot(StateBuffer* buf, int slot) {
    return (char*)buf->states + (size_t)slot * (size_t)buf->state_size;
}

static inline void* candidate_state_slot(StateBuffer* buf, int slot) {
    return (char*)buf->candidate_states + (size_t)slot * (size_t)buf->state_size;
}

static inline float clean_state_priority(float priority) {
    if (priority < 0.0f || isnan(priority) || isinf(priority)) {
        return 0.0f;
    }
    return priority;
}

static inline void state_heap_swap(StateBuffer* buf, int a, int b) {
    int slot_a = buf->heap[a];
    int slot_b = buf->heap[b];
    buf->heap[a] = slot_b;
    buf->heap[b] = slot_a;
    buf->heap_pos[slot_a] = b;
    buf->heap_pos[slot_b] = a;
}

static inline void state_heap_sift_up(StateBuffer* buf, int pos) {
    while (pos > 0) {
        int parent = (pos - 1) / 2;
        if (buf->priorities[buf->heap[parent]] <= buf->priorities[buf->heap[pos]]) {
            break;
        }
        state_heap_swap(buf, parent, pos);
        pos = parent;
    }
}

static inline void state_heap_sift_down(StateBuffer* buf, int pos) {
    while (true) {
        int left = 2 * pos + 1;
        int right = left + 1;
        int best = pos;
        if (left < buf->size &&
                buf->priorities[buf->heap[left]] < buf->priorities[buf->heap[best]]) {
            best = left;
        }
        if (right < buf->size &&
                buf->priorities[buf->heap[right]] < buf->priorities[buf->heap[best]]) {
            best = right;
        }
        if (best == pos) {
            break;
        }
        state_heap_swap(buf, pos, best);
        pos = best;
    }
}

static inline void state_heap_update_slot(StateBuffer* buf, int slot, float priority,
        float decay) {
    priority = clean_state_priority(priority);
    float old_priority = buf->priorities[slot];
    if (priority < old_priority) {
        priority = decay * old_priority;
    }
    buf->priorities[slot] = priority;
    buf->priorities_host[slot] = from_float(priority);
    int pos = buf->heap_pos[slot];
    if (priority < old_priority) {
        state_heap_sift_up(buf, pos);
    } else {
        state_heap_sift_down(buf, pos);
    }
}

static inline void state_heap_insert(StateBuffer* buf, const void* state, float priority) {
    priority = clean_state_priority(priority);
    if (buf->size < buf->capacity) {
        int slot = buf->size;
        memcpy(state_slot(buf, slot), state, (size_t)buf->state_size);
        buf->priorities[slot] = priority;
        buf->priorities_host[slot] = from_float(priority);
        buf->heap[slot] = slot;
        buf->heap_pos[slot] = slot;
        buf->size++;
        state_heap_sift_up(buf, slot);
        return;
    }

    int min_slot = buf->heap[0];
    if (priority <= buf->priorities[min_slot]) {
        return;
    }
    memcpy(state_slot(buf, min_slot), state, (size_t)buf->state_size);
    buf->priorities[min_slot] = priority;
    buf->priorities_host[min_slot] = from_float(priority);
    state_heap_sift_down(buf, 0);
}

static inline void capture_curriculum_checkpoint(PuffeRL* pufferl, int buffer_idx, int t) {
    StateBuffer* buf = &pufferl->state_buf;
    int interval = buf->checkpoint_interval;
    if ((t % interval) != 0) {
        return;
    }
    int checkpoint_idx = t / interval;
    if (checkpoint_idx >= buf->num_checkpoints) {
        return;
    }

    StaticVec* vec = pufferl->vec;
    int env_start = vec->buffer_env_starts[buffer_idx];
    int env_end = env_start + vec->buffer_env_counts[buffer_idx];
    if (env_start >= buf->num_fresh_envs) {
        return;
    }
    if (env_end > buf->num_fresh_envs) {
        env_end = buf->num_fresh_envs;
    }

    int candidate_offset = checkpoint_idx * buf->num_envs;
    for (int env_idx = env_start; env_idx < env_end; env_idx++) {
        static_vec_store_state(
            vec, env_idx, candidate_state_slot(buf, candidate_offset + env_idx));
    }
}

void curriculum_rollout_begin(PuffeRL* pufferl) {
    HypersT* h = &pufferl->hypers;
    StateBuffer* buf = &pufferl->state_buf;
    StaticVec* vec = pufferl->vec;
    cudaStream_t stream = pufferl->default_stream;
    int total_envs = vec->size;
    int agents_per_env = buf->agents_per_env;
    int total_epochs = h->total_timesteps / (h->total_agents * h->horizon);
    float progress = total_epochs > 0 ? (float)pufferl->epoch / (float)total_epochs : 1.0f;
    progress = fminf(1.0f, fmaxf(0.0f, progress));
    float current_cl_frac = h->cl_frac;
    if (h->anneal_cl) {
        current_cl_frac *= 1.0f - progress;
    }
    int num_cl_envs = (buf->size == 0 || buf->size < h->warmup_states) ? 0 :
        clamp_int((int)(current_cl_frac * (float)total_envs), 0, total_envs);
    int num_fresh_envs = total_envs - num_cl_envs;
    int total_agents = total_envs * agents_per_env;
    int fresh_agents = num_fresh_envs * agents_per_env;

    buf->num_cl_envs = num_cl_envs;
    buf->num_fresh_envs = num_fresh_envs;
    vec->log_env_limit = (num_cl_envs > 0) ? num_fresh_envs : 0;
    fill_precision_kernel<<<grid_size(total_agents), BLOCK_SIZE, 0, stream>>>(
        buf->importance.data, from_float(1.0f), total_agents);

    if (num_cl_envs > 0) {
        compute_state_prio_abs<<<grid_size(buf->size), BLOCK_SIZE, 0, stream>>>(
            buf->advantages.data, buf->prio_bufs.prio_probs.data,
            h->explore_alpha, buf->size);
        compute_state_prio_normalize<<<1, PRIO_BLOCK_SIZE, 0, stream>>>(
            buf->prio_bufs.prio_probs.data, buf->size);
        build_cdf<<<1, 1, 0, stream>>>(
            buf->prio_bufs.cdf.data, buf->prio_bufs.prio_probs.data, buf->size);
        int threads = 256;
        int blocks = (num_cl_envs + threads - 1) / threads;
        long* rng_offset = pufferl->rng_offset_puf.data + h->num_buffers + 1;
        multinomial_sample<<<blocks, threads, 0, stream>>>(
            buf->prio_bufs.idx.data, buf->prio_bufs.cdf.data,
            buf->size, num_cl_envs, pufferl->seed, rng_offset);
        advance_rng_offset<<<1, 1, 0, stream>>>(rng_offset, (long)num_cl_envs);
        build_curriculum_state_importance<<<grid_size(num_cl_envs * agents_per_env),
            BLOCK_SIZE, 0, stream>>>(
            buf->importance.data, buf->prio_bufs.idx.data, buf->prio_bufs.prio_probs.data,
            fresh_agents, num_cl_envs, agents_per_env, buf->size, h->explore_beta);
        cudaMemcpyAsync(buf->env_state_inds_host + num_fresh_envs, buf->prio_bufs.idx.data,
            (size_t)num_cl_envs * sizeof(int), cudaMemcpyDeviceToHost, stream);
        cudaStreamSynchronize(stream);

        for (int i = 0; i < num_cl_envs; i++) {
            int env_idx = num_fresh_envs + i;
            int slot = buf->env_state_inds_host[env_idx];
            static_vec_load_state(vec, env_idx, state_slot(buf, slot));
        }
        if (vec->gpu) {
            cudaMemcpy(vec->gpu_observations, vec->observations,
                (size_t)vec->total_agents * get_obs_size() * get_obs_elem_size(),
                cudaMemcpyHostToDevice);
        }
    }
}

void curriculum_update_advantages(PuffeRL* pufferl, PrecisionTensor* advantages,
        cudaStream_t stream) {
    StateBuffer* buf = &pufferl->state_buf;
    int horizon = advantages->shape[1];
    int num_fresh_envs = buf->num_fresh_envs;
    int num_cl_envs = buf->num_cl_envs;
    int agents_per_env = buf->agents_per_env;
    int score_rows = buf->num_checkpoints * num_fresh_envs + num_cl_envs;

    compute_curriculum_checkpoint_scores<<<grid_size(score_rows), BLOCK_SIZE, 0, stream>>>(
        buf->env_scores.data, advantages->data, num_fresh_envs, num_cl_envs,
        buf->num_checkpoints, buf->checkpoint_interval, agents_per_env, horizon);
    cudaMemcpyAsync(buf->env_scores_host, buf->env_scores.data,
        (size_t)score_rows * sizeof(precision_t), cudaMemcpyDeviceToHost, stream);
    cudaStreamSynchronize(stream);

    int cl_score_offset = buf->num_checkpoints * num_fresh_envs;
    for (int i = 0; i < num_cl_envs; i++) {
        int env_idx = num_fresh_envs + i;
        int slot = buf->env_state_inds_host[env_idx];
        float priority = to_float(buf->env_scores_host[cl_score_offset + i]);
        state_heap_update_slot(buf, slot, priority, pufferl->hypers.explore_decay);
    }

    for (int checkpoint_idx = 0; checkpoint_idx < buf->num_checkpoints; checkpoint_idx++) {
        int candidate_offset = checkpoint_idx * buf->num_envs;
        for (int i = 0; i < num_fresh_envs; i++) {
            int candidate_idx = candidate_offset + i;
            float priority = to_float(
                buf->env_scores_host[checkpoint_idx * num_fresh_envs + i]);
            state_heap_insert(buf, candidate_state_slot(buf, candidate_idx), priority);
        }
    }

    cudaMemcpyAsync(buf->advantages.data, buf->priorities_host,
        (size_t)buf->size * sizeof(precision_t), cudaMemcpyHostToDevice, stream);
}

#endif
