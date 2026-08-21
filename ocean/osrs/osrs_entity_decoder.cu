static constexpr int OSRS_ENTITY_TARGET_BRANCH = 1;
static constexpr int OSRS_ENTITY_TARGET_START = 25;
static constexpr int OSRS_ENTITY_TARGET_SLOTS = 14;
static constexpr int OSRS_ENTITY_TARGET_KEY_DIM = OSRS_ENTITY_BOTTLENECK;

struct OsrsEntityDecoderWeights {
    Prec linear_w;
    Prec query_w;
    Prec key_w;
    Prec log_temperature;
    int hidden_dim;
    int output_dim;
};

struct OsrsEntityDecoderActivations {
    OsrsEntityEncoderActivations* encoder;
    Prec out;
    Prec saved_input;
    Prec linear_out;
    Prec query;
    Prec projected_keys;
    Prec linear_grad;
    Prec query_grad;
    Prec projected_key_grad;
    Prec key_grad;
    Prec linear_hidden_grad;
    Prec query_hidden_grad;
    Prec grad_input;
    Float temperature_grad_contrib;
    Prec linear_wgrad;
    Prec query_wgrad;
    Prec key_wgrad;
    Prec temperature_grad;
};

__global__ void osrs_entity_decoder_init_temperature(precision_t* value) {
    value[0] = from_float(logf(10.0f));
}

__global__ void osrs_entity_decoder_assemble(
    precision_t* __restrict__ out,
    const precision_t* __restrict__ linear,
    const precision_t* __restrict__ query,
    const precision_t* __restrict__ keys,
    const precision_t* __restrict__ log_temperature,
    int B,
    int output_dim
) {
    int output_stride = output_dim + 1;
    int linear_dim = output_stride - OSRS_ENTITY_TARGET_SLOTS;
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx >= B * output_stride) return;
    int b = idx / output_stride;
    int column = idx - b * output_stride;
    if (column < OSRS_ENTITY_TARGET_START) {
        out[idx] = linear[(int64_t)b * linear_dim + column];
        return;
    }
    if (column >= OSRS_ENTITY_TARGET_START + OSRS_ENTITY_TARGET_SLOTS) {
        out[idx] = linear[
            (int64_t)b * linear_dim + column - OSRS_ENTITY_TARGET_SLOTS];
        return;
    }

    int slot = column - OSRS_ENTITY_TARGET_START;
    const precision_t* q = query + (int64_t)b * OSRS_ENTITY_TARGET_KEY_DIM;
    const precision_t* key = keys +
        ((int64_t)b * OSRS_ENTITY_TARGET_SLOTS + slot) *
        OSRS_ENTITY_TARGET_KEY_DIM;
    float dot = 0.0f;
    float query_norm_squared = 0.0f;
    float key_norm_squared = 0.0f;
    for (int k = 0; k < OSRS_ENTITY_TARGET_KEY_DIM; k++) {
        float q_value = to_float(q[k]);
        float key_value = to_float(key[k]);
        dot += q_value * key_value;
        query_norm_squared += q_value * q_value;
        key_norm_squared += key_value * key_value;
    }
    float denominator = sqrtf(fmaxf(
        query_norm_squared * key_norm_squared, 1.0e-12f));
    out[idx] = from_float(
        expf(to_float(log_temperature[0])) * dot / denominator);
}

__global__ void osrs_entity_decoder_gather_linear_grad(
    precision_t* __restrict__ linear_grad,
    const float* __restrict__ grad_logits,
    const float* __restrict__ grad_value,
    int B,
    int output_dim
) {
    int linear_dim = output_dim + 1 - OSRS_ENTITY_TARGET_SLOTS;
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx >= B * linear_dim) return;
    int b = idx / linear_dim;
    int linear_column = idx - b * linear_dim;
    int output_column = linear_column < OSRS_ENTITY_TARGET_START
        ? linear_column : linear_column + OSRS_ENTITY_TARGET_SLOTS;
    float value = output_column == output_dim
        ? grad_value[b]
        : grad_logits[(int64_t)b * output_dim + output_column];
    linear_grad[idx] = from_float(value);
}

__global__ void osrs_entity_decoder_pointer_grad(
    precision_t* __restrict__ query_grad,
    precision_t* __restrict__ projected_key_grad,
    float* __restrict__ temperature_grad_contrib,
    const float* __restrict__ grad_logits,
    const precision_t* __restrict__ query,
    const precision_t* __restrict__ keys,
    const precision_t* __restrict__ log_temperature,
    int B,
    int output_dim
) {
    int b = blockIdx.x;
    int k = threadIdx.x;
    if (b >= B || k >= OSRS_ENTITY_TARGET_KEY_DIM) return;
    const precision_t* q = query + (int64_t)b * OSRS_ENTITY_TARGET_KEY_DIM;
    float query_value = to_float(q[k]);
    float query_norm_squared = 0.0f;
    for (int d = 0; d < OSRS_ENTITY_TARGET_KEY_DIM; d++) {
        float value = to_float(q[d]);
        query_norm_squared += value * value;
    }
    float scale = expf(to_float(log_temperature[0]));
    float accumulated_query_grad = 0.0f;
    for (int slot = 0; slot < OSRS_ENTITY_TARGET_SLOTS; slot++) {
        int record = b * OSRS_ENTITY_TARGET_SLOTS + slot;
        const precision_t* key = keys +
            (int64_t)record * OSRS_ENTITY_TARGET_KEY_DIM;
        float dot = 0.0f;
        float key_norm_squared = 0.0f;
        for (int d = 0; d < OSRS_ENTITY_TARGET_KEY_DIM; d++) {
            float q_value = to_float(q[d]);
            float key_value = to_float(key[d]);
            dot += q_value * key_value;
            key_norm_squared += key_value * key_value;
        }
        float norm_product = query_norm_squared * key_norm_squared;
        float denominator = sqrtf(fmaxf(norm_product, 1.0e-12f));
        float cosine = dot / denominator;
        float score = scale * cosine;
        float gradient = grad_logits[
            (int64_t)b * output_dim + OSRS_ENTITY_TARGET_START + slot];
        float scaled_gradient = gradient * scale;
        float key_value = to_float(key[k]);
        float query_component = key_value / denominator;
        float key_component = query_value / denominator;
        if (norm_product > 1.0e-12f) {
            query_component -=
                cosine * query_value / query_norm_squared;
            key_component -= cosine * key_value / key_norm_squared;
        }
        accumulated_query_grad += scaled_gradient * query_component;
        projected_key_grad[
            (int64_t)record * OSRS_ENTITY_TARGET_KEY_DIM + k] =
            from_float(scaled_gradient * key_component);
        if (k == 0) {
            temperature_grad_contrib[record] = gradient * score;
        }
    }
    query_grad[(int64_t)b * OSRS_ENTITY_TARGET_KEY_DIM + k] =
        from_float(accumulated_query_grad);
}

__global__ void osrs_entity_decoder_add_hidden_grad(
    precision_t* __restrict__ out,
    const precision_t* __restrict__ linear_grad,
    const precision_t* __restrict__ query_grad,
    int count
) {
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx >= count) return;
    out[idx] = from_float(
        to_float(linear_grad[idx]) + to_float(query_grad[idx]));
}

static Prec osrs_entity_decoder_forward(
    void* weights,
    void* activations,
    Prec input,
    cudaStream_t stream
) {
    OsrsEntityDecoderWeights* dw = (OsrsEntityDecoderWeights*)weights;
    OsrsEntityDecoderActivations* a =
        (OsrsEntityDecoderActivations*)activations;
    int B = input.shape[0];
    if (a->saved_input.data != nullptr) {
        puf_copy(&a->saved_input, &input, stream);
    }
    OsrsEntityBranchActivations* branches =
        osrs_entity_branch_activations(a->encoder);
    Prec keys = branches[OSRS_ENTITY_TARGET_BRANCH].h1;
    puf_mm(&input, &dw->linear_w, &a->linear_out, stream);
    puf_mm(&input, &dw->query_w, &a->query, stream);
    puf_mm(&keys, &dw->key_w, &a->projected_keys, stream);
    int count = B * (dw->output_dim + 1);
    osrs_entity_decoder_assemble<<<grid_size(count), BLOCK_SIZE, 0, stream>>>(
        a->out.data, a->linear_out.data, a->query.data,
        a->projected_keys.data, dw->log_temperature.data,
        B, dw->output_dim);
    return a->out;
}

static void osrs_entity_decoder_init_weights(
    void* weights,
    ulong* seed,
    cudaStream_t stream
) {
    OsrsEntityDecoderWeights* dw = (OsrsEntityDecoderWeights*)weights;
    puf_kaiming_init(&dw->linear_w, 1.0f, (*seed)++, stream);
    puf_kaiming_init(&dw->query_w, 1.0f, (*seed)++, stream);
    puf_kaiming_init(&dw->key_w, 1.0f, (*seed)++, stream);
    osrs_entity_decoder_init_temperature<<<1, 1, 0, stream>>>(
        dw->log_temperature.data);
}

static void osrs_entity_decoder_reg_params(void* weights, Allocator* allocator) {
    OsrsEntityDecoderWeights* dw = (OsrsEntityDecoderWeights*)weights;
    int linear_dim = dw->output_dim + 1 - OSRS_ENTITY_TARGET_SLOTS;
    dw->linear_w = {.shape = {linear_dim, dw->hidden_dim}};
    dw->query_w = {.shape = {OSRS_ENTITY_TARGET_KEY_DIM, dw->hidden_dim}};
    dw->key_w = {
        .shape = {OSRS_ENTITY_TARGET_KEY_DIM, OSRS_ENTITY_TARGET_KEY_DIM},
    };
    dw->log_temperature = {.shape = {1}};
    alloc_register(allocator, &dw->linear_w);
    alloc_register(allocator, &dw->query_w);
    alloc_register(allocator, &dw->key_w);
    alloc_register(allocator, &dw->log_temperature);
}

static void osrs_entity_decoder_reg_train(
    void* weights,
    void* activations,
    Allocator* acts,
    Allocator* grads,
    int B
) {
    OsrsEntityDecoderWeights* dw = (OsrsEntityDecoderWeights*)weights;
    OsrsEntityDecoderActivations* a =
        (OsrsEntityDecoderActivations*)activations;
    int linear_dim = dw->output_dim + 1 - OSRS_ENTITY_TARGET_SLOTS;
    *a = {};
    a->encoder = osrs_entity_encoder_last;
    assert(a->encoder != nullptr);
    a->out = {.shape = {B, dw->output_dim + 1}};
    a->saved_input = {.shape = {B, dw->hidden_dim}};
    a->linear_out = {.shape = {B, linear_dim}};
    a->query = {.shape = {B, OSRS_ENTITY_TARGET_KEY_DIM}};
    a->projected_keys = {
        .shape = {
            B * OSRS_ENTITY_TARGET_SLOTS,
            OSRS_ENTITY_TARGET_KEY_DIM,
        },
    };
    a->linear_grad = a->linear_out;
    a->query_grad = a->query;
    a->projected_key_grad = a->projected_keys;
    a->key_grad = a->projected_keys;
    a->linear_hidden_grad = {.shape = {B, dw->hidden_dim}};
    a->query_hidden_grad = a->linear_hidden_grad;
    a->grad_input = a->linear_hidden_grad;
    a->temperature_grad_contrib = {
        .shape = {B * OSRS_ENTITY_TARGET_SLOTS, 1},
    };
    alloc_register(acts, &a->out);
    alloc_register(acts, &a->saved_input);
    alloc_register(acts, &a->linear_out);
    alloc_register(acts, &a->query);
    alloc_register(acts, &a->projected_keys);
    alloc_register(acts, &a->linear_grad);
    alloc_register(acts, &a->query_grad);
    alloc_register(acts, &a->projected_key_grad);
    alloc_register(acts, &a->key_grad);
    alloc_register(acts, &a->linear_hidden_grad);
    alloc_register(acts, &a->query_hidden_grad);
    alloc_register(acts, &a->grad_input);
    alloc_register(acts, &a->temperature_grad_contrib);
    a->linear_wgrad = {.shape = {linear_dim, dw->hidden_dim}};
    a->query_wgrad = {
        .shape = {OSRS_ENTITY_TARGET_KEY_DIM, dw->hidden_dim},
    };
    a->key_wgrad = {
        .shape = {OSRS_ENTITY_TARGET_KEY_DIM, OSRS_ENTITY_TARGET_KEY_DIM},
    };
    a->temperature_grad = {.shape = {1}};
    alloc_register(grads, &a->linear_wgrad);
    alloc_register(grads, &a->query_wgrad);
    alloc_register(grads, &a->key_wgrad);
    alloc_register(grads, &a->temperature_grad);
    osrs_entity_decoder_keygrad = &a->key_grad;
}

static void osrs_entity_decoder_reg_rollout(
    void* weights,
    void* activations,
    Allocator* allocator,
    int B
) {
    OsrsEntityDecoderWeights* dw = (OsrsEntityDecoderWeights*)weights;
    OsrsEntityDecoderActivations* a =
        (OsrsEntityDecoderActivations*)activations;
    int linear_dim = dw->output_dim + 1 - OSRS_ENTITY_TARGET_SLOTS;
    *a = {};
    a->encoder = osrs_entity_encoder_last;
    assert(a->encoder != nullptr);
    a->out = {.shape = {B, dw->output_dim + 1}};
    a->linear_out = {.shape = {B, linear_dim}};
    a->query = {.shape = {B, OSRS_ENTITY_TARGET_KEY_DIM}};
    a->projected_keys = {
        .shape = {
            B * OSRS_ENTITY_TARGET_SLOTS,
            OSRS_ENTITY_TARGET_KEY_DIM,
        },
    };
    alloc_register(allocator, &a->out);
    alloc_register(allocator, &a->linear_out);
    alloc_register(allocator, &a->query);
    alloc_register(allocator, &a->projected_keys);
}

static void* osrs_entity_decoder_create_weights(void* self) {
    Decoder* decoder = (Decoder*)self;
    OsrsEntityDecoderWeights* weights =
        (OsrsEntityDecoderWeights*)calloc(1, sizeof(OsrsEntityDecoderWeights));
    weights->hidden_dim = decoder->hidden_dim;
    weights->output_dim = decoder->output_dim;
    return weights;
}

static Prec osrs_entity_decoder_backward(
    void* weights,
    void* activations,
    Float grad_logits,
    Float grad_logstd,
    Float grad_value,
    cudaStream_t stream
) {
    (void)grad_logstd;
    OsrsEntityDecoderWeights* dw = (OsrsEntityDecoderWeights*)weights;
    OsrsEntityDecoderActivations* a =
        (OsrsEntityDecoderActivations*)activations;
    int B = a->saved_input.shape[0];
    int linear_dim = dw->output_dim + 1 - OSRS_ENTITY_TARGET_SLOTS;
    int linear_count = B * linear_dim;
    osrs_entity_decoder_gather_linear_grad<<<
        grid_size(linear_count), BLOCK_SIZE, 0, stream>>>(
        a->linear_grad.data, grad_logits.data, grad_value.data,
        B, dw->output_dim);
    osrs_entity_decoder_pointer_grad<<<B, OSRS_ENTITY_TARGET_KEY_DIM, 0, stream>>>(
        a->query_grad.data, a->projected_key_grad.data,
        a->temperature_grad_contrib.data, grad_logits.data,
        a->query.data, a->projected_keys.data,
        dw->log_temperature.data, B, dw->output_dim);

    OsrsEntityBranchActivations* branches =
        osrs_entity_branch_activations(a->encoder);
    Prec keys = branches[OSRS_ENTITY_TARGET_BRANCH].h1;
    puf_mm_tn(&a->linear_grad, &a->saved_input, &a->linear_wgrad, stream);
    puf_mm_tn(&a->query_grad, &a->saved_input, &a->query_wgrad, stream);
    puf_mm_tn(&a->projected_key_grad, &keys, &a->key_wgrad, stream);
    sum_rows_to_precision_kernel<<<1, BLOCK_SIZE, 0, stream>>>(
        a->temperature_grad.data, a->temperature_grad_contrib.data,
        B * OSRS_ENTITY_TARGET_SLOTS, 1);
    puf_mm_nn(
        &a->linear_grad, &dw->linear_w, &a->linear_hidden_grad, stream);
    puf_mm_nn(&a->query_grad, &dw->query_w, &a->query_hidden_grad, stream);
    puf_mm_nn(&a->projected_key_grad, &dw->key_w, &a->key_grad, stream);
    int hidden_count = B * dw->hidden_dim;
    osrs_entity_decoder_add_hidden_grad<<<
        grid_size(hidden_count), BLOCK_SIZE, 0, stream>>>(
        a->grad_input.data, a->linear_hidden_grad.data,
        a->query_hidden_grad.data, hidden_count);
    return a->grad_input;
}

static void create_osrs_entity_decoder(Decoder* decoder) {
    assert(!decoder->continuous);
    assert(decoder->output_dim == 436);
    *decoder = Decoder{
        .forward = osrs_entity_decoder_forward,
        .backward = osrs_entity_decoder_backward,
        .init_weights = osrs_entity_decoder_init_weights,
        .reg_params = osrs_entity_decoder_reg_params,
        .reg_train = osrs_entity_decoder_reg_train,
        .reg_rollout = osrs_entity_decoder_reg_rollout,
        .create_weights = osrs_entity_decoder_create_weights,
        .hidden_dim = decoder->hidden_dim,
        .output_dim = decoder->output_dim,
        .continuous = decoder->continuous,
        .activation_size = sizeof(OsrsEntityDecoderActivations),
    };
}
