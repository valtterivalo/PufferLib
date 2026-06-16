#include <nccl.h>

__global__ void muon_norm_reduce(float* __restrict__ out, const float* __restrict__ partials, int num_blocks) {
    __shared__ float sdata[256];
    int tid = threadIdx.x;
    sdata[tid] = (tid < num_blocks) ? partials[tid] : 0.0f;
    __syncthreads();
    for (int s = blockDim.x / 2; s > 0; s >>= 1) {
        if (tid < s) {
            sdata[tid] += sdata[tid + s];
        }
        __syncthreads();
    }
    if (tid == 0) {
        *out = sdata[0];
    }
}

__global__ void muon_norm_partials(float* __restrict__ partials, const precision_t* __restrict__ src, int n) {
    __shared__ float sdata[256];
    int tid = threadIdx.x;
    float sum = 0.0f;
    for (int i = blockIdx.x * blockDim.x + tid; i < n; i += blockDim.x * gridDim.x) {
        float v = to_float(src[i]);
        sum += v * v;
    }
    sdata[tid] = sum;
    __syncthreads();
    for (int s = blockDim.x / 2; s > 0; s >>= 1) {
        if (tid < s) {
            sdata[tid] += sdata[tid + s];
        }
        __syncthreads();
    }
    if (tid == 0) {
        partials[blockIdx.x] = sdata[0];
    }
}

__global__ void muon_norm_apply_scaled(precision_t* __restrict__ dst,
        const float* __restrict__ norm_ptr, float safety_factor, float eps, int n) {
    float inv_norm = 1.0f / (sqrtf(*norm_ptr) * safety_factor + eps);
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx < n) {
        dst[idx] = from_float(to_float(dst[idx]) * inv_norm);
    }
}

// Nesterov with f32 momentum accumulator and precision_t gradients
__global__ void muon_nesterov(float* __restrict__ mb, precision_t* __restrict__ gc, float mu, int n) {
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx < n) {
        float m = mu * mb[idx] + to_float(gc[idx]);
        mb[idx] = m;
        gc[idx] = from_float(to_float(gc[idx]) + mu * m);
    }
}

// Fused weight update: wb = wb * (1 - lr*wd) - lr * scale * update
__global__ void muon_weight_update(float* __restrict__ wb, const precision_t* __restrict__ update,
        const float* __restrict__ lr_ptr, float wd, float scale, int n) {
    float lr = *lr_ptr;
    float wd_scale = 1.0f - lr * wd;
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx < n) {
        wb[idx] = wb[idx] * wd_scale - lr * scale * to_float(update[idx]);
    }
}

__global__ void muon_clip_norm(precision_t* __restrict__ dst,
        const float* __restrict__ sum_sq_ptr, float max_norm, float eps, int n) {
    float clip_coef = fminf(max_norm / (sqrtf(*sum_sq_ptr) + eps), 1.0f);
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx < n) {
        dst[idx] = from_float(to_float(dst[idx]) * clip_coef);
    }
}

__global__ void muon_init_long_dim_scales(float* __restrict__ long_dim_scale,
        const precision_t* __restrict__ src, int rows, int cols,
        bool transposed, float eps) {
    int row = blockIdx.x;
    if (row >= rows) return;
    float sum = 0.0f;
    for (int col = threadIdx.x; col < cols; col += blockDim.x) {
        int idx = transposed ? col * rows + row : row * cols + col;
        float v = to_float(src[idx]);
        sum += v * v;
    }

    __shared__ float sdata[256];
    int tid = threadIdx.x;
    sdata[tid] = sum;
    __syncthreads();
    for (int s = blockDim.x / 2; s > 0; s >>= 1) {
        if (tid < s) sdata[tid] += sdata[tid + s];
        __syncthreads();
    }
    if (tid == 0) {
        long_dim_scale[row] = 1.0f / fmaxf(sqrtf(sdata[0]), eps);
    }
}

__global__ void muon_apply_long_dim_scales(precision_t* __restrict__ dst,
        const precision_t* __restrict__ src, const float* __restrict__ long_dim_scale,
        int R, int C, bool wide) {
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    int n = R * C;
    if (idx >= n) return;
    int r = idx / C;
    int c = idx - r * C;
    float scale = wide ? long_dim_scale[c] : long_dim_scale[r];
    dst[idx] = from_float(to_float(src[idx]) * scale);
}

__global__ void muon_update_long_dim_scales(float* __restrict__ long_dim_scale,
        const precision_t* __restrict__ update, int rows, int cols,
        bool transposed, float target_row_sq, float beta, float eps_sq) {
    int row = blockIdx.x;
    if (row >= rows) return;
    float sum = 0.0f;
    for (int col = threadIdx.x; col < cols; col += blockDim.x) {
        int idx = transposed ? col * rows + row : row * cols + col;
        float v = to_float(update[idx]);
        sum += v * v;
    }

    __shared__ float sdata[256];
    int tid = threadIdx.x;
    sdata[tid] = sum;
    __syncthreads();
    for (int s = blockDim.x / 2; s > 0; s >>= 1) {
        if (tid < s) sdata[tid] += sdata[tid + s];
        __syncthreads();
    }
    if (tid == 0) {
        float row_sq = fmaxf(sdata[0], eps_sq);
        long_dim_scale[row] *= powf(target_row_sq / row_sq, beta);
    }
}

static constexpr int muon_orthogonalize_iters = 5;
static constexpr float polar_express_safety = 1.01f;
static constexpr float muon_rectangular_beta = 0.5f;
static constexpr double polar_express_coeffs[muon_orthogonalize_iters][3] = {
    {8.20516041400557, -22.901934987056, 16.4607249101803},
    {4.06639515994277, -2.86115408675514, 0.518399522669474},
    {3.90959490443792, -2.82335173503952, 0.525036976939003},
    {3.28556401719862, -2.41530195963594, 0.485294065527909},
    {2.27787328708398, -1.61982176526544, 0.398480787041684},
};

static bool muon_is_packed_mingru(long R, long C) {
    return R == 3 * C;
}

static bool muon_rectangular_eligible(long R, long C) {
    return R > C && !muon_is_packed_mingru(R, C) && C >= 2;
}

static float muon_mup_fan_in_scale(long R, long C) {
    return sqrtf((float)max(R, C) / (float)C);
}

struct Muon {
    double momentum, weight_decay;
    float lr_val_init;
    float* lr_ptr;
    float* norm_ptr;
    float* grad_norm_ptr;
    FloatTensor lr_puf, ortho_norm_puf, grad_norm_puf;
    FloatTensor mb_puf;
    PrecisionTensor gram, gram_buf, x_buf, orig_buf;
    FloatTensor norm_partials, long_dim_scale_puf;
    long max_M, max_N;
    Allocator* param_alloc;  // params allocator — shapes used by muon_step
    ncclComm_t nccl_comm;
    int world_size;
};

void muon_init(Muon* m, Allocator* param_alloc, double lr_val,
        double momentum, double weight_decay, Allocator* alloc) {
    m->momentum = momentum;
    m->weight_decay = weight_decay;
    m->lr_val_init = (float)lr_val;
    m->lr_ptr = nullptr;
    m->param_alloc = param_alloc;
    m->nccl_comm = nullptr;
    m->world_size = 1;
    m->max_M = 0; m->max_N = 0;
    long n = param_alloc->total_elems;
    m->lr_puf =         {.shape = {1}};
    m->mb_puf =         {.shape = {n}};
    m->norm_partials =  {.shape = {256}};
    m->grad_norm_puf =  {.shape = {1}};
    alloc_register(alloc, &m->lr_puf);
    alloc_register(alloc, &m->mb_puf);
    alloc_register(alloc, &m->norm_partials);
    alloc_register(alloc, &m->grad_norm_puf);
    long max_M = 0, max_N = 0, max_rect_N = 0;
    for (int _i = 0; _i < param_alloc->num_regs; _i++) {
        AllocEntry& e = param_alloc->regs[_i];
        if (ndim(e.shape) >= 2) {
            long R = e.shape[0], C = numel(e.shape) / R;
            max_M = max(max_M, min(R, C));
            max_N = max(max_N, max(R, C));
            if (muon_rectangular_eligible(R, C)) {
                max_rect_N = max(max_rect_N, max(R, C));
            }
        }
    }
    if (max_M > 0) {
        m->max_M = max_M; m->max_N = max_N;
        m->gram =        {.shape = {max_M, max_M}};
        m->gram_buf =    {.shape = {max_M, max_M}};
        m->x_buf =       {.shape = {max_M, max_N}};
        m->ortho_norm_puf = {.shape = {1}};
        alloc_register(alloc, &m->gram);
        alloc_register(alloc, &m->gram_buf);
        alloc_register(alloc, &m->x_buf);
        if (max_rect_N > 0) {
            m->orig_buf =           {.shape = {max_M, max_N}};
            m->long_dim_scale_puf = {.shape = {max_rect_N}};
            alloc_register(alloc, &m->orig_buf);
            alloc_register(alloc, &m->long_dim_scale_puf);
        }
        alloc_register(alloc, &m->ortho_norm_puf);
    }
}

void muon_post_create(Muon* m) {
    m->lr_ptr = m->lr_puf.data;
    m->grad_norm_ptr = m->grad_norm_puf.data;
    if (m->ortho_norm_puf.data) m->norm_ptr = m->ortho_norm_puf.data;
    cudaMemcpy(m->lr_ptr, &m->lr_val_init, sizeof(float), cudaMemcpyHostToDevice);
    cudaMemset(m->mb_puf.data, 0, numel(m->mb_puf.shape) * sizeof(float));
}

static PrecisionTensor muon_orthogonalize_matrix(Muon* m, PrecisionTensor x, PrecisionTensor x_buf,
        PrecisionTensor gram, PrecisionTensor gram_buf, cudaStream_t stream) {
    long R = x.shape[0], C = x.shape[1];
    long M = min(R, C), N = max(R, C);
    bool tall = R > C;

    int nblk = min((int)grid_size(numel(x.shape)), 256);
    muon_norm_partials<<<nblk, 256, 0, stream>>>(
        m->norm_partials.data, x.data, numel(x.shape));
    muon_norm_reduce<<<1, 256, 0, stream>>>(m->norm_ptr, m->norm_partials.data, nblk);
    muon_norm_apply_scaled<<<grid_size(numel(x.shape)), BLOCK_SIZE, 0, stream>>>(
        x.data, m->norm_ptr, polar_express_safety, 1e-7f, numel(x.shape));

    cublasOperation_t gram_op_a = tall ? CUBLAS_OP_T : CUBLAS_OP_N;
    cublasOperation_t gram_op_b = tall ? CUBLAS_OP_N : CUBLAS_OP_T;
    for (int i = 0; i < muon_orthogonalize_iters; ++i) {
        PrecisionTensor& src = (i % 2 == 0) ? x : x_buf;
        PrecisionTensor& dst = (i % 2 == 0) ? x_buf : x;
        cublasGemmExDense(gram_op_a, gram_op_b, (int)M, (int)M, (int)N,
            src.data, src.data, gram.data, stream);
        puf_copy(&gram_buf, &gram, stream);
        puf_addmm_nn(&gram, &gram, &gram_buf,
            polar_express_coeffs[i][2], polar_express_coeffs[i][1], stream);
        puf_copy(&dst, &src, stream);
        cublasGemmExDense(CUBLAS_OP_N, CUBLAS_OP_N, (int)R, (int)C, (int)M,
            tall ? src.data : gram_buf.data, tall ? gram_buf.data : src.data, dst.data,
            stream, 1.0f, polar_express_coeffs[i][0]);
    }
    return (muon_orthogonalize_iters % 2 == 0) ? x : x_buf;
}

static PrecisionTensor muon_rectangular_correct(Muon* m, PrecisionTensor x, PrecisionTensor x_buf,
        PrecisionTensor gram, PrecisionTensor gram_buf, cudaStream_t stream) {
    long R = x.shape[0], C = x.shape[1];
    long M = min(R, C), N = max(R, C);
    bool wide = R < C;
    PrecisionTensor orig = {.data = m->orig_buf.data, .shape = {R, C}};

    puf_copy(&orig, &x, stream);
    muon_init_long_dim_scales<<<(int)N, 256, 0, stream>>>(
        m->long_dim_scale_puf.data, orig.data, (int)N, (int)M, wide, 1e-7f);
    muon_apply_long_dim_scales<<<grid_size(numel(x.shape)), BLOCK_SIZE, 0, stream>>>(
        x.data, orig.data, m->long_dim_scale_puf.data, (int)R, (int)C, wide);

    PrecisionTensor update = muon_orthogonalize_matrix(m, x, x_buf, gram, gram_buf, stream);
    float target_row_sq = (float)M / (float)N;
    muon_update_long_dim_scales<<<(int)N, 256, 0, stream>>>(
        m->long_dim_scale_puf.data, update.data, (int)N, (int)M, wide,
        target_row_sq, muon_rectangular_beta, 1e-14f);

    muon_apply_long_dim_scales<<<grid_size(numel(x.shape)), BLOCK_SIZE, 0, stream>>>(
        x.data, orig.data, m->long_dim_scale_puf.data, (int)R, (int)C, wide);
    return muon_orthogonalize_matrix(m, x, x_buf, gram, gram_buf, stream);
}

void muon_step(Muon* m, FloatTensor weights, PrecisionTensor grads, float max_grad_norm, cudaStream_t stream = 0) {
    // Multi-GPU support: simple all-reduce over a contiguous grad buffer
    if (m->nccl_comm != nullptr && m->world_size > 1) {
        ncclAllReduce(grads.data, grads.data, numel(grads.shape),
            NCCL_PRECISION, ncclAvg, m->nccl_comm, stream);
    }

    // Clip gradients by norm
    int clip_blocks = min((int)grid_size(numel(grads.shape)), 256);
    muon_norm_partials<<<clip_blocks, 256, 0, stream>>>(
        m->norm_partials.data, grads.data, numel(grads.shape));
    muon_norm_reduce<<<1, 256, 0, stream>>>(m->grad_norm_ptr, m->norm_partials.data, clip_blocks);
    muon_clip_norm<<<grid_size(numel(grads.shape)), BLOCK_SIZE, 0, stream>>>(
        grads.data, m->grad_norm_ptr, max_grad_norm, 1e-6f, numel(grads.shape));

    // Nesterov momentum
    muon_nesterov<<<grid_size(numel(m->mb_puf.shape)), BLOCK_SIZE, 0, stream>>>(
        m->mb_puf.data, grads.data, (float)m->momentum, numel(m->mb_puf.shape));

    long offset = 0;
    for (int _i = 0; _i < m->param_alloc->num_regs; _i++) {
        AllocEntry& e = m->param_alloc->regs[_i];
        precision_t* gc_ptr = grads.data + offset;
        float* wb_ptr = weights.data + offset;
        long ne = numel(e.shape);
        const precision_t* update_ptr = gc_ptr;
        float scale = 1.0f;

        // Orthogonalize the update
        if (ndim(e.shape) >= 2) {
            long R = e.shape[0], C = ne / R;
            long M = min(R, C);
            PrecisionTensor gram = {.data = m->gram.data, .shape = {M, M}};
            PrecisionTensor gram_buf = {.data = m->gram_buf.data, .shape = {M, M}};

            if (muon_is_packed_mingru(R, C)) {
                // MinGRU packs hidden/gate/proj as one (3H, H) matrix.
                // Orthogonalize each HxH slice independently, then keep the
                // MuP fan-in scaling implied by the raw polar update RMS.
                long chunk_ne = C * C;
                PrecisionTensor chunk_gram = {.data = m->gram.data, .shape = {C, C}};
                PrecisionTensor chunk_gram_buf = {.data = m->gram_buf.data, .shape = {C, C}};
                const precision_t* packed_update_ptr = nullptr;
                for (int chunk = 0; chunk < 3; chunk++) {
                    PrecisionTensor x = {
                        .data = gc_ptr + chunk * chunk_ne,
                        .shape = {C, C},
                    };
                    PrecisionTensor x_buf = {
                        .data = m->x_buf.data + chunk * chunk_ne,
                        .shape = {C, C},
                    };
                    PrecisionTensor chunk_update = muon_orthogonalize_matrix(
                        m, x, x_buf, chunk_gram, chunk_gram_buf, stream);
                    if (chunk == 0) packed_update_ptr = chunk_update.data;
                }
                update_ptr = packed_update_ptr;
                // Keep the raw HxH polar RMS 1/sqrt(H) with MuP fan-in scaling.
                scale = 1.0f;
            } else {
                PrecisionTensor x = {.data = gc_ptr, .shape = {R, C}};
                PrecisionTensor x_buf = {.data = m->x_buf.data, .shape = {R, C}};
                PrecisionTensor update = muon_rectangular_eligible(R, C)
                    ? muon_rectangular_correct(m, x, x_buf, gram, gram_buf, stream)
                    : muon_orthogonalize_matrix(m, x, x_buf, gram, gram_buf, stream);
                update_ptr = update.data;
                // Fan-in scaling keeps the layer output update roughly
                // invariant to input dimension for W shaped [out, in].
                scale = muon_mup_fan_in_scale(R, C);
            }
        }

        muon_weight_update<<<grid_size(ne), BLOCK_SIZE, 0, stream>>>(
            wb_ptr, update_ptr, m->lr_ptr, (float)m->weight_decay,
            scale, (int)ne);
        offset += ne;
    }
}
