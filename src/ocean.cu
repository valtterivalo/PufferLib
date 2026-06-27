// NMMO3 CUDA encoder: multihot, cuDNN conv, embedding, concat, projection
// Included by pufferlib.cu — requires precision_t, PrecisionTensor, Allocator, puf_mm, etc.

#include "cudnn_conv2d.cu"

// ---- NMMO3 constants ----

static constexpr int N3_MAP_H = 11, N3_MAP_W = 15, N3_NFEAT = 10;
static constexpr int N3_MULTIHOT = 59;
static constexpr int N3_MAP_SIZE = N3_MAP_H * N3_MAP_W * N3_NFEAT;
static constexpr int N3_PLAYER = 47, N3_REWARD = 10;
static constexpr int N3_EMBED_DIM = 32, N3_EMBED_VOCAB = 128;
static constexpr int N3_PLAYER_EMBED = N3_PLAYER * N3_EMBED_DIM;
static constexpr int N3_C1_IC = 59, N3_C1_OC = 128, N3_C1_K = 5, N3_C1_S = 3;
static constexpr int N3_C1_OH = 3, N3_C1_OW = 4;
static constexpr int N3_C2_IC = 128, N3_C2_OC = 128, N3_C2_K = 3, N3_C2_S = 1;
static constexpr int N3_C2_OH = 1, N3_C2_OW = 2;
static constexpr int N3_CONV_FLAT = N3_C2_OC * N3_C2_OH * N3_C2_OW;
static constexpr int N3_CONCAT = N3_CONV_FLAT + N3_PLAYER_EMBED + N3_PLAYER + N3_REWARD;

__constant__ int N3_OFFSETS[10] = {0, 4, 8, 25, 30, 33, 38, 43, 48, 55};

static cudnnDataType_t n3_cudnn_dtype() {
    return (PRECISION_SIZE == 2) ? CUDNN_DATA_BFLOAT16 : CUDNN_DATA_FLOAT;
}

// ---- NMMO3 kernels ----

__global__ void n3_multihot_kernel(
    precision_t* __restrict__ out, const precision_t* __restrict__ obs, int B, int obs_size) {
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx >= B * N3_MAP_H * N3_MAP_W) return;
    int b = idx / (N3_MAP_H * N3_MAP_W), rem = idx % (N3_MAP_H * N3_MAP_W);
    int h = rem / N3_MAP_W, w = rem % N3_MAP_W;
    const precision_t* src = obs + b * obs_size + (h * N3_MAP_W + w) * N3_NFEAT;
    precision_t* dst = out + b * N3_MULTIHOT * N3_MAP_H * N3_MAP_W;
    for (int f = 0; f < N3_NFEAT; f++)
        dst[(N3_OFFSETS[f] + (int)to_float(src[f])) * N3_MAP_H * N3_MAP_W + h * N3_MAP_W + w] = from_float(1.0f);
}

__global__ void n3_embedding_kernel(
    precision_t* __restrict__ out, const precision_t* __restrict__ obs,
    const precision_t* __restrict__ embed_w, int B, int obs_size) {
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx >= B * N3_PLAYER) return;
    int b = idx / N3_PLAYER, f = idx % N3_PLAYER;
    int val = (int)to_float(obs[b * obs_size + N3_MAP_SIZE + f]);
    const precision_t* src = embed_w + val * N3_EMBED_DIM;
    precision_t* dst = out + b * N3_PLAYER_EMBED + f * N3_EMBED_DIM;
    for (int d = 0; d < N3_EMBED_DIM; d++) dst[d] = src[d];
}

__global__ void n3_concat_kernel(
    precision_t* __restrict__ out, const precision_t* __restrict__ conv_flat,
    const precision_t* __restrict__ embed, const precision_t* __restrict__ obs,
    int B, int obs_size) {
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx >= B * N3_CONCAT) return;
    int b = idx / N3_CONCAT, c = idx % N3_CONCAT;
    precision_t val;
    if (c < N3_CONV_FLAT) {
        int oc = c / (N3_C2_OH * N3_C2_OW), r = c % (N3_C2_OH * N3_C2_OW);
        int oh = r / N3_C2_OW, ow = r % N3_C2_OW;
        val = conv_flat[b * N3_CONV_FLAT + oc * N3_C2_OH * N3_C2_OW + oh * N3_C2_OW + ow];
    } else if (c < N3_CONV_FLAT + N3_PLAYER_EMBED)
        val = embed[b * N3_PLAYER_EMBED + (c - N3_CONV_FLAT)];
    else if (c < N3_CONV_FLAT + N3_PLAYER_EMBED + N3_PLAYER)
        val = obs[b * obs_size + N3_MAP_SIZE + (c - N3_CONV_FLAT - N3_PLAYER_EMBED)];
    else
        val = obs[b * obs_size + obs_size - N3_REWARD + (c - N3_CONV_FLAT - N3_PLAYER_EMBED - N3_PLAYER)];
    out[idx] = val;
}

__global__ void n3_bias_relu_kernel(
    precision_t* __restrict__ data, const precision_t* __restrict__ bias, int total, int dim) {
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx >= total) return;
    data[idx] = from_float(fmaxf(0.0f, to_float(data[idx]) + to_float(bias[idx % dim])));
}

__global__ void n3_relu_backward_kernel(
    precision_t* __restrict__ grad, const precision_t* __restrict__ out, int total) {
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx >= total) return;
    if (to_float(out[idx]) <= 0.0f) grad[idx] = from_float(0.0f);
}


__global__ void bias_grad_kernel(
    precision_t* __restrict__ bgrad, const precision_t* __restrict__ grad, int N, int dim) {
    int d = blockIdx.x;
    if (d >= dim) return;
    float sum = 0.0f;
    for (int i = threadIdx.x; i < N; i += blockDim.x)
        sum += to_float(grad[i * dim + d]);
    for (int offset = 16; offset > 0; offset >>= 1)
        sum += __shfl_down_sync(0xffffffff, sum, offset);
    __shared__ float sdata[32];
    int lane = threadIdx.x % 32, warp = threadIdx.x / 32;
    if (lane == 0) sdata[warp] = sum;
    __syncthreads();
    if (warp == 0) {
        sum = (lane < (blockDim.x + 31) / 32) ? sdata[lane] : 0.0f;
        for (int offset = 16; offset > 0; offset >>= 1)
            sum += __shfl_down_sync(0xffffffff, sum, offset);
        if (lane == 0) bgrad[d] = from_float(sum);
    }
}

// NCHW bias grad: sum over (B, OH, OW) for each OC channel
__global__ void n3_conv_bias_grad_nchw(
    precision_t* __restrict__ bgrad, const precision_t* __restrict__ grad,
    int B, int OC, int spatial) {
    int oc = blockIdx.x;
    if (oc >= OC) return;
    float sum = 0.0f;
    int total = B * spatial;
    for (int i = threadIdx.x; i < total; i += blockDim.x) {
        int b = i / spatial, s = i % spatial;
        sum += to_float(grad[b * OC * spatial + oc * spatial + s]);
    }
    for (int offset = 16; offset > 0; offset >>= 1)
        sum += __shfl_down_sync(0xffffffff, sum, offset);
    __shared__ float sdata[32];
    int lane = threadIdx.x % 32, warp = threadIdx.x / 32;
    if (lane == 0) sdata[warp] = sum;
    __syncthreads();
    if (warp == 0) {
        sum = (lane < (blockDim.x + 31) / 32) ? sdata[lane] : 0.0f;
        for (int offset = 16; offset > 0; offset >>= 1)
            sum += __shfl_down_sync(0xffffffff, sum, offset);
        if (lane == 0) bgrad[oc] = from_float(sum);
    }
}

__global__ void n3_concat_backward_conv_kernel(
    precision_t* __restrict__ conv_grad, const precision_t* __restrict__ concat_grad, int B) {
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx >= B * N3_CONV_FLAT) return;
    int b = idx / N3_CONV_FLAT, c = idx % N3_CONV_FLAT;
    conv_grad[b * N3_CONV_FLAT + c] = concat_grad[b * N3_CONCAT + c];
}

// Embedding backward: scatter-add grad from concat_grad's player_embed region
// into embed_wgrad (float accumulation buffer).
// Each (b, f) looked up row obs[b, MAP_SIZE+f] from the table.
__global__ void n3_embedding_backward_kernel(
    float* __restrict__ embed_wgrad_f,
    const precision_t* __restrict__ concat_grad,
    const precision_t* __restrict__ obs,
    int B, int obs_size) {
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx >= B * N3_PLAYER * N3_EMBED_DIM) return;
    int b = idx / (N3_PLAYER * N3_EMBED_DIM);
    int rem = idx % (N3_PLAYER * N3_EMBED_DIM);
    int f = rem / N3_EMBED_DIM;
    int d = rem % N3_EMBED_DIM;
    int val = (int)to_float(obs[b * obs_size + N3_MAP_SIZE + f]);
    float g = to_float(concat_grad[b * N3_CONCAT + N3_CONV_FLAT + f * N3_EMBED_DIM + d]);
    atomicAdd(&embed_wgrad_f[val * N3_EMBED_DIM + d], g);
}

// Cast float buffer to precision_t
__global__ void n3_float_to_precision_kernel(
    precision_t* __restrict__ dst, const float* __restrict__ src, int n) {
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx < n) dst[idx] = from_float(src[idx]);
}

// ---- atomicAdd for precision_t ----
#ifdef PRECISION_FLOAT
__device__ __forceinline__ void atomicAdd_precision(precision_t* addr, precision_t val) {
    atomicAdd(addr, val);
}
#else
__device__ __forceinline__ void atomicAdd_precision(precision_t* addr, precision_t val) {
    // bf16 atomicAdd via CAS on enclosing 32-bit word
    unsigned int* addr_u32 = (unsigned int*)((size_t)addr & ~2ULL);
    bool is_high = ((size_t)addr & 2) != 0;
    unsigned int old_u32 = *addr_u32, assumed;
    do {
        assumed = old_u32;
        __nv_bfloat16* pair = (__nv_bfloat16*)&old_u32;
        float sum = __bfloat162float(pair[is_high]) + __bfloat162float(val);
        unsigned int new_u32 = assumed;
        ((__nv_bfloat16*)&new_u32)[is_high] = __float2bfloat16(sum);
        old_u32 = atomicCAS(addr_u32, assumed, new_u32);
    } while (old_u32 != assumed);
}
#endif

// ---- NCHW bias kernels for im2col conv path ----

__global__ void conv_bias_kernel(precision_t* __restrict__ data,
        const precision_t* __restrict__ bias, int B, int OC, int spatial) {
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    int total = B * OC * spatial;
    if (idx >= total) return;
    int oc = (idx / spatial) % OC;
    data[idx] = from_float(to_float(data[idx]) + to_float(bias[oc]));
}

__global__ void conv_bias_relu_kernel(precision_t* __restrict__ data,
        const precision_t* __restrict__ bias, int B, int OC, int spatial) {
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    int total = B * OC * spatial;
    if (idx >= total) return;
    int oc = (idx / spatial) % OC;
    data[idx] = from_float(fmaxf(0.0f, to_float(data[idx]) + to_float(bias[oc])));
}

// ---- im2col + cuBLAS conv (no cuDNN) ----
// NCHW layout throughout. Weight stored as (OC, IC*K*K).
// im2col produces (B*OH*OW, IC*K*K), matmul with W^T gives (B*OH*OW, OC),
// then reshape to NCHW (B, OC, OH, OW).

__global__ void im2col_kernel(
    const precision_t* __restrict__ input, precision_t* __restrict__ col,
    int B, int IC, int IH, int IW, int K, int S, int OH, int OW
) {
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    int total = B * OH * OW * IC * K * K;
    if (idx >= total) return;
    int col_w = IC * K * K;
    int row = idx / col_w;
    int c = idx % col_w;
    int b = row / (OH * OW);
    int rem = row % (OH * OW);
    int oh = rem / OW, ow = rem % OW;
    int ic = c / (K * K), kk = c % (K * K);
    int kh = kk / K, kw = kk % K;
    int ih = oh * S + kh, iw = ow * S + kw;
    col[idx] = input[b * IC * IH * IW + ic * IH * IW + ih * IW + iw];
}

// Backward: col2im — input-centric gather to avoid atomics.
// Each thread owns one (b, ic, ih, iw) element and sums contributions from all
// (oh, ow, kh, kw) patches that map to it.
__global__ void col2im_kernel(
    const precision_t* __restrict__ col, precision_t* __restrict__ grad_input,
    int B, int IC, int IH, int IW, int K, int S, int OH, int OW
) {
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    int total = B * IC * IH * IW;
    if (idx >= total) return;
    int iw = idx % IW;
    int ih = (idx / IW) % IH;
    int ic = (idx / (IW * IH)) % IC;
    int b  = idx / (IW * IH * IC);
    float sum = 0.0f;
    for (int kh = 0; kh < K; kh++) {
        int ih_off = ih - kh;
        if (ih_off < 0 || ih_off % S != 0) continue;
        int oh = ih_off / S;
        if (oh >= OH) continue;
        for (int kw = 0; kw < K; kw++) {
            int iw_off = iw - kw;
            if (iw_off < 0 || iw_off % S != 0) continue;
            int ow = iw_off / S;
            if (ow >= OW) continue;
            int col_idx = (b * OH * OW + oh * OW + ow) * (IC * K * K) + ic * K * K + kh * K + kw;
            sum += to_float(col[col_idx]);
        }
    }
    grad_input[idx] = from_float(sum);
}

// Transpose (B, OC, OH, OW) -> (B*OH*OW, OC)  [NCHW to row-major spatial-first]
__global__ void nchw_to_rows_kernel(
    const precision_t* __restrict__ src, precision_t* __restrict__ dst,
    int B, int OC, int spatial
) {
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    int total = B * OC * spatial;
    if (idx >= total) return;
    int b = idx / (OC * spatial);
    int oc = (idx / spatial) % OC;
    int s = idx % spatial;
    dst[(b * spatial + s) * OC + oc] = src[idx];
}

// Transpose (B*OH*OW, OC) -> (B, OC, OH, OW)  [row-major spatial-first to NCHW]
__global__ void rows_to_nchw_kernel(
    const precision_t* __restrict__ src, precision_t* __restrict__ dst,
    int B, int OC, int spatial
) {
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    int total = B * OC * spatial;
    if (idx >= total) return;
    int b = idx / (OC * spatial);
    int oc = (idx / spatial) % OC;
    int s = idx % spatial;
    dst[idx] = src[(b * spatial + s) * OC + oc];
}

// Forward: im2col conv + bias + optional relu. All NCHW.
// col_buf: pre-allocated (max_B * OH * OW, IC * K * K)
// mm_buf:  pre-allocated (max_B * OH * OW, OC)  — row-major (spatial-first)
static void gemm_conv_forward(
    PrecisionTensor* weight, PrecisionTensor* bias,
    precision_t* input, precision_t* output,
    precision_t* col_buf, precision_t* mm_buf,
    int B, int IC, int IH, int IW, int OC, int K, int S, int OH, int OW,
    bool relu, cudaStream_t stream
) {
    int col_rows = B * OH * OW;
    int col_cols = IC * K * K;
    int total_col = col_rows * col_cols;
    int total_out = B * OC * OH * OW;

    // im2col: input NCHW -> col (B*OH*OW, IC*K*K)
    im2col_kernel<<<grid_size(total_col), BLOCK_SIZE, 0, stream>>>(
        input, col_buf, B, IC, IH, IW, K, S, OH, OW);

    // matmul: col (B*OH*OW, IC*K*K) @ W^T (IC*K*K, OC) = mm_buf (B*OH*OW, OC)
    PrecisionTensor col_t = {.data = col_buf, .shape = {col_rows, col_cols}};
    PrecisionTensor mm_t  = {.data = mm_buf,  .shape = {col_rows, OC}};
    puf_mm(&col_t, weight, &mm_t, stream);

    // transpose (B*OH*OW, OC) -> (B, OC, OH, OW) NCHW + bias + relu
    int spatial = OH * OW;
    rows_to_nchw_kernel<<<grid_size(total_out), BLOCK_SIZE, 0, stream>>>(
        mm_buf, output, B, OC, spatial);
    if (relu) {
        conv_bias_relu_kernel<<<grid_size(total_out), BLOCK_SIZE, 0, stream>>>(
            output, bias->data, B, OC, spatial);
    } else {
        conv_bias_kernel<<<grid_size(total_out), BLOCK_SIZE, 0, stream>>>(
            output, bias->data, B, OC, spatial);
    }
}

// Backward: weight grad + optional input grad via im2col/col2im + cuBLAS.
// grad_output is NCHW (B, OC, OH, OW). saved_input is NCHW.
// Caller handles relu backward and bias grad (same as cuDNN path).
static void gemm_conv_backward(
    PrecisionTensor* weight,
    precision_t* saved_input, precision_t* grad_output,
    precision_t* wgrad, precision_t* input_grad,
    precision_t* col_buf, precision_t* mm_buf,
    int B, int IC, int IH, int IW, int OC, int K, int S, int OH, int OW,
    cudaStream_t stream
) {
    int col_rows = B * OH * OW;
    int col_cols = IC * K * K;
    int total_col = col_rows * col_cols;
    int total_out = B * OC * OH * OW;
    int spatial = OH * OW;

    // Transpose grad_output NCHW -> (B*OH*OW, OC)
    nchw_to_rows_kernel<<<grid_size(total_out), BLOCK_SIZE, 0, stream>>>(
        grad_output, mm_buf, B, OC, spatial);

    // im2col of saved_input
    im2col_kernel<<<grid_size(total_col), BLOCK_SIZE, 0, stream>>>(
        saved_input, col_buf, B, IC, IH, IW, K, S, OH, OW);

    // Weight grad: mm_buf^T (OC, B*OH*OW) @ col_buf (B*OH*OW, IC*K*K) = wgrad (OC, IC*K*K)
    PrecisionTensor mm_t  = {.data = mm_buf,  .shape = {col_rows, OC}};
    PrecisionTensor col_t = {.data = col_buf, .shape = {col_rows, col_cols}};
    PrecisionTensor wg_t  = {.data = wgrad,   .shape = {OC, col_cols}};
    puf_mm_tn(&mm_t, &col_t, &wg_t, stream);

    // Input grad (optional): mm_buf (B*OH*OW, OC) @ weight (OC, IC*K*K) = col_grad (B*OH*OW, IC*K*K)
    if (input_grad) {
        puf_mm_nn(&mm_t, weight, &col_t, stream);  // reuse col_buf as col_grad
        col2im_kernel<<<grid_size(B * IC * IH * IW), BLOCK_SIZE, 0, stream>>>(
            col_buf, input_grad, B, IC, IH, IW, K, S, OH, OW);
    }
}

// ---- NMMO3 encoder structs ----

struct NMMO3EncoderWeights {
    ConvWeights conv1, conv2;
    PrecisionTensor embed_w, proj_w, proj_b;
    int obs_size, hidden;
};

struct NMMO3EncoderActivations {
    ConvActivations conv1, conv2;
    PrecisionTensor col1, mm1, col2, mm2;  // im2col + matmul scratch buffers
    PrecisionTensor multihot, embed_out, concat, out, saved_obs;
    PrecisionTensor embed_wgrad, proj_wgrad, proj_bgrad;
    FloatTensor embed_wgrad_f;  // float accumulation buffer for scatter-add
};

static NMMO3EncoderWeights* nmmo3_encoder_create(int obs_size, int hidden) {
    NMMO3EncoderWeights* ew = (NMMO3EncoderWeights*)calloc(1, sizeof(NMMO3EncoderWeights));
    ew->obs_size = obs_size; ew->hidden = hidden;
    conv_init(&ew->conv1, N3_C1_IC, N3_C1_OC, N3_C1_K, N3_C1_S, N3_MAP_H, N3_MAP_W, true);
    conv_init(&ew->conv2, N3_C2_IC, N3_C2_OC, N3_C2_K, N3_C2_S, N3_C1_OH, N3_C1_OW, false);
    return ew;
}

// ---- NMMO3 encoder interface ----

static PrecisionTensor nmmo3_encoder_forward(void* w, void* activations, PrecisionTensor input, cudaStream_t stream) {
    NMMO3EncoderWeights* ew = (NMMO3EncoderWeights*)w;
    NMMO3EncoderActivations* a = (NMMO3EncoderActivations*)activations;
    int B = input.shape[0];

    if (a->saved_obs.data) puf_copy(&a->saved_obs, &input, stream);

    cudaMemsetAsync(a->multihot.data, 0, (int64_t)B * N3_MULTIHOT * N3_MAP_H * N3_MAP_W * sizeof(precision_t), stream);
    n3_multihot_kernel<<<grid_size(B * N3_MAP_H * N3_MAP_W), BLOCK_SIZE, 0, stream>>>(
        a->multihot.data, input.data, B, ew->obs_size);

    gemm_conv_forward(&ew->conv1.w, &ew->conv1.b, a->multihot.data, a->conv1.out.data,
        a->col1.data, a->mm1.data, B, N3_C1_IC, N3_MAP_H, N3_MAP_W,
        N3_C1_OC, N3_C1_K, N3_C1_S, N3_C1_OH, N3_C1_OW, true, stream);
    if (a->conv1.saved_input.data)
        cudaMemcpyAsync(a->conv1.saved_input.data, a->multihot.data,
            (int64_t)B * N3_C1_IC * N3_MAP_H * N3_MAP_W * sizeof(precision_t), cudaMemcpyDeviceToDevice, stream);
    gemm_conv_forward(&ew->conv2.w, &ew->conv2.b, a->conv1.out.data, a->conv2.out.data,
        a->col2.data, a->mm2.data, B, N3_C2_IC, N3_C1_OH, N3_C1_OW,
        N3_C2_OC, N3_C2_K, N3_C2_S, N3_C2_OH, N3_C2_OW, false, stream);
    if (a->conv2.saved_input.data)
        cudaMemcpyAsync(a->conv2.saved_input.data, a->conv1.out.data,
            (int64_t)B * N3_C2_IC * N3_C1_OH * N3_C1_OW * sizeof(precision_t), cudaMemcpyDeviceToDevice, stream);

    n3_embedding_kernel<<<grid_size(B * N3_PLAYER), BLOCK_SIZE, 0, stream>>>(
        a->embed_out.data, input.data, ew->embed_w.data, B, ew->obs_size);
    n3_concat_kernel<<<grid_size(B * N3_CONCAT), BLOCK_SIZE, 0, stream>>>(
        a->concat.data, a->conv2.out.data, a->embed_out.data, input.data, B, ew->obs_size);

    puf_mm(&a->concat, &ew->proj_w, &a->out, stream);
    n3_bias_relu_kernel<<<grid_size(B * ew->hidden), BLOCK_SIZE, 0, stream>>>(
        a->out.data, ew->proj_b.data, B * ew->hidden, ew->hidden);
    return a->out;
}

static void nmmo3_encoder_backward(void* w, void* activations, PrecisionTensor grad, cudaStream_t stream) {
    NMMO3EncoderWeights* ew = (NMMO3EncoderWeights*)w;
    NMMO3EncoderActivations* a = (NMMO3EncoderActivations*)activations;
    int B = grad.shape[0], H = ew->hidden;

    n3_relu_backward_kernel<<<grid_size(B * H), BLOCK_SIZE, 0, stream>>>(
        grad.data, a->out.data, B * H);
    bias_grad_kernel<<<H, 256, 0, stream>>>(
        a->proj_bgrad.data, grad.data, B, H);
    puf_mm_tn(&grad, &a->concat, &a->proj_wgrad, stream);

    PrecisionTensor grad_concat = {.data = a->concat.data, .shape = {B, N3_CONCAT}};
    puf_mm_nn(&grad, &ew->proj_w, &grad_concat, stream);

    n3_concat_backward_conv_kernel<<<grid_size(B * N3_CONV_FLAT), BLOCK_SIZE, 0, stream>>>(
        a->conv2.grad.data, grad_concat.data, B);

    n3_conv_bias_grad_nchw<<<ew->conv2.OC, 256, 0, stream>>>(
        a->conv2.bgrad.data, a->conv2.grad.data,
        B, ew->conv2.OC, ew->conv2.OH * ew->conv2.OW);
    gemm_conv_backward(&ew->conv2.w, a->conv2.saved_input.data, a->conv2.grad.data,
        a->conv2.wgrad.data, a->conv1.grad.data,
        a->col2.data, a->mm2.data, B, N3_C2_IC, N3_C1_OH, N3_C1_OW,
        N3_C2_OC, N3_C2_K, N3_C2_S, N3_C2_OH, N3_C2_OW, stream);

    n3_relu_backward_kernel<<<grid_size(B * ew->conv1.OC * ew->conv1.OH * ew->conv1.OW), BLOCK_SIZE, 0, stream>>>(
        a->conv1.grad.data, a->conv1.out.data,
        B * ew->conv1.OC * ew->conv1.OH * ew->conv1.OW);
    n3_conv_bias_grad_nchw<<<ew->conv1.OC, 256, 0, stream>>>(
        a->conv1.bgrad.data, a->conv1.grad.data,
        B, ew->conv1.OC, ew->conv1.OH * ew->conv1.OW);
    gemm_conv_backward(&ew->conv1.w, a->conv1.saved_input.data, a->conv1.grad.data,
        a->conv1.wgrad.data, NULL,
        a->col1.data, a->mm1.data, B, N3_C1_IC, N3_MAP_H, N3_MAP_W,
        N3_C1_OC, N3_C1_K, N3_C1_S, N3_C1_OH, N3_C1_OW, stream);

    // Embedding backward: scatter-add from concat gradient into float buffer, then cast
    int embed_n = N3_EMBED_VOCAB * N3_EMBED_DIM;
    cudaMemsetAsync(a->embed_wgrad_f.data, 0, embed_n * sizeof(float), stream);
    n3_embedding_backward_kernel<<<grid_size(B * N3_PLAYER * N3_EMBED_DIM), BLOCK_SIZE, 0, stream>>>(
        a->embed_wgrad_f.data, grad_concat.data, a->saved_obs.data, B, ew->obs_size);
    n3_float_to_precision_kernel<<<grid_size(embed_n), BLOCK_SIZE, 0, stream>>>(
        a->embed_wgrad.data, a->embed_wgrad_f.data, embed_n);
}

static void nmmo3_encoder_init_weights(void* w, uint64_t* seed, cudaStream_t stream) {
    NMMO3EncoderWeights* ew = (NMMO3EncoderWeights*)w;
    conv_init_weights(&ew->conv1, seed, stream);
    conv_init_weights(&ew->conv2, seed, stream);
    auto init2d = [&](PrecisionTensor& t, int rows, int cols, float gain) {
        PrecisionTensor wt = {.data = t.data, .shape = {rows, cols}};
        puf_kaiming_init(&wt, gain, (*seed)++, stream);
    };
    puf_normal_init(&ew->embed_w, 1.0f, (*seed)++, stream);
    init2d(ew->proj_w, ew->hidden, N3_CONCAT, 1.0f);
    cudaMemsetAsync(ew->proj_b.data, 0, numel(ew->proj_b.shape) * sizeof(precision_t), stream);
}

static void nmmo3_encoder_reg_params(void* w, Allocator* alloc) {
    NMMO3EncoderWeights* ew = (NMMO3EncoderWeights*)w;
    conv_reg_params(&ew->conv1, alloc);
    conv_reg_params(&ew->conv2, alloc);
    ew->embed_w = {.shape = {N3_EMBED_VOCAB, N3_EMBED_DIM}};
    ew->proj_w  = {.shape = {ew->hidden, N3_CONCAT}};
    ew->proj_b  = {.shape = {ew->hidden}};
    alloc_register(alloc,&ew->embed_w);
    alloc_register(alloc,&ew->proj_w);  alloc_register(alloc,&ew->proj_b);
}

static void nmmo3_encoder_reg_train(void* w, void* activations, Allocator* acts, Allocator* grads, int B_TT) {
    NMMO3EncoderWeights* ew = (NMMO3EncoderWeights*)w;
    NMMO3EncoderActivations* a = (NMMO3EncoderActivations*)activations;
    *a = {};
    a->multihot = {.shape = {B_TT, N3_MULTIHOT * N3_MAP_H * N3_MAP_W}};
    alloc_register(acts,&a->multihot);
    // Conv1 buffers
    a->conv1.out         = {.shape = {B_TT * N3_C1_OC * N3_C1_OH * N3_C1_OW}};
    a->conv1.grad        = {.shape = {B_TT * N3_C1_OC * N3_C1_OH * N3_C1_OW}};
    a->conv1.saved_input = {.shape = {B_TT * N3_C1_IC * N3_MAP_H * N3_MAP_W}};
    a->conv1.wgrad       = {.shape = {N3_C1_OC, N3_C1_IC * N3_C1_K * N3_C1_K}};
    a->conv1.bgrad       = {.shape = {N3_C1_OC}};
    alloc_register(acts,&a->conv1.out); alloc_register(acts,&a->conv1.grad); alloc_register(acts,&a->conv1.saved_input);
    alloc_register(grads,&a->conv1.wgrad); alloc_register(grads,&a->conv1.bgrad);
    a->col1 = {.shape = {B_TT * N3_C1_OH * N3_C1_OW, N3_C1_IC * N3_C1_K * N3_C1_K}};
    a->mm1  = {.shape = {B_TT * N3_C1_OH * N3_C1_OW, N3_C1_OC}};
    alloc_register(acts,&a->col1); alloc_register(acts,&a->mm1);
    // Conv2 buffers
    a->conv2.out         = {.shape = {B_TT * N3_C2_OC * N3_C2_OH * N3_C2_OW}};
    a->conv2.grad        = {.shape = {B_TT * N3_C2_OC * N3_C2_OH * N3_C2_OW}};
    a->conv2.saved_input = {.shape = {B_TT * N3_C2_IC * N3_C1_OH * N3_C1_OW}};
    a->conv2.wgrad       = {.shape = {N3_C2_OC, N3_C2_IC * N3_C2_K * N3_C2_K}};
    a->conv2.bgrad       = {.shape = {N3_C2_OC}};
    alloc_register(acts,&a->conv2.out); alloc_register(acts,&a->conv2.grad); alloc_register(acts,&a->conv2.saved_input);
    alloc_register(grads,&a->conv2.wgrad); alloc_register(grads,&a->conv2.bgrad);
    a->col2 = {.shape = {B_TT * N3_C2_OH * N3_C2_OW, N3_C2_IC * N3_C2_K * N3_C2_K}};
    a->mm2  = {.shape = {B_TT * N3_C2_OH * N3_C2_OW, N3_C2_OC}};
    alloc_register(acts,&a->col2); alloc_register(acts,&a->mm2);
    a->embed_out = {.shape = {B_TT, N3_PLAYER_EMBED}};
    a->concat    = {.shape = {B_TT, N3_CONCAT}};
    a->out       = {.shape = {B_TT, ew->hidden}};
    a->saved_obs = {.shape = {B_TT, ew->obs_size}};
    alloc_register(acts,&a->embed_out); alloc_register(acts,&a->concat);
    alloc_register(acts,&a->out);       alloc_register(acts,&a->saved_obs);
    a->embed_wgrad = {.shape = {N3_EMBED_VOCAB, N3_EMBED_DIM}};
    a->embed_wgrad_f = {.shape = {N3_EMBED_VOCAB, N3_EMBED_DIM}};
    a->proj_wgrad  = {.shape = {ew->hidden, N3_CONCAT}};
    a->proj_bgrad  = {.shape = {ew->hidden}};
    alloc_register(grads,&a->embed_wgrad);
    alloc_register(acts,&a->embed_wgrad_f);
    alloc_register(grads,&a->proj_wgrad);  alloc_register(grads,&a->proj_bgrad);
}

static void nmmo3_encoder_reg_rollout(void* w, void* activations, Allocator* alloc, int B) {
    NMMO3EncoderWeights* ew = (NMMO3EncoderWeights*)w;
    NMMO3EncoderActivations* a = (NMMO3EncoderActivations*)activations;
    a->multihot = {.shape = {B, N3_MULTIHOT * N3_MAP_H * N3_MAP_W}};
    alloc_register(alloc,&a->multihot);
    a->conv1.out = {.shape = {B * N3_C1_OC * N3_C1_OH * N3_C1_OW}};
    alloc_register(alloc,&a->conv1.out);
    a->col1 = {.shape = {B * N3_C1_OH * N3_C1_OW, N3_C1_IC * N3_C1_K * N3_C1_K}};
    a->mm1  = {.shape = {B * N3_C1_OH * N3_C1_OW, N3_C1_OC}};
    alloc_register(alloc,&a->col1); alloc_register(alloc,&a->mm1);
    a->conv2.out = {.shape = {B * N3_C2_OC * N3_C2_OH * N3_C2_OW}};
    alloc_register(alloc,&a->conv2.out);
    a->col2 = {.shape = {B * N3_C2_OH * N3_C2_OW, N3_C2_IC * N3_C2_K * N3_C2_K}};
    a->mm2  = {.shape = {B * N3_C2_OH * N3_C2_OW, N3_C2_OC}};
    alloc_register(alloc,&a->col2); alloc_register(alloc,&a->mm2);
    a->embed_out = {.shape = {B, N3_PLAYER_EMBED}};
    a->concat    = {.shape = {B, N3_CONCAT}};
    a->out       = {.shape = {B, ew->hidden}};
    alloc_register(alloc,&a->embed_out); alloc_register(alloc,&a->concat); alloc_register(alloc,&a->out);
}

static void* nmmo3_encoder_create_weights(void* self) {
    Encoder* e = (Encoder*)self;
    return nmmo3_encoder_create(e->in_dim, e->out_dim);
}
static void nmmo3_encoder_free_weights(void* weights) { free(weights); }
static void nmmo3_encoder_free_activations(void* activations) { free(activations); }

// ---- Colosseum entity encoder ----
//
// Mirrors pufferlib/models.py ColosseumEntityEncoder: output = global(flat obs) +
// masked-maxpool over a shared 2-layer MLP applied to the COLO_ENT_NUM_NPCS NPC
// records. Every Linear is bias-free (the native backend convention) and GELU uses
// the tanh approximation so the CUDA, puffernet, and torch paths stay bit-comparable.
//
// Obs layout (verified against encounter_colosseum_obs_mask.inc): the NPC block is
// COLO_ENT_NUM_NPCS contiguous records of COLO_ENT_FEATS floats starting at
// COLO_ENT_NPC_START. Each record begins with a COLO_ENT_TYPE_ONEHOT-wide NPC-type
// one-hot, so an active record has type one-hot sum > 0 and an inactive record is
// fully zero (col_write_obs_ctx memsets the obs before writing).
static constexpr int COLO_ENT_NPC_START   = 1058;
static constexpr int COLO_ENT_NUM_NPCS    = 24;
static constexpr int COLO_ENT_FEATS       = 43;
static constexpr int COLO_ENT_TYPE_ONEHOT = 12;
static constexpr int COLO_ENT_BOTTLENECK  = 16;
static constexpr int COLO_ENT_NPC_BLOCK   = COLO_ENT_NUM_NPCS * COLO_ENT_FEATS;

struct ColosseumEntityEncoderWeights {
    PrecisionTensor global_w;    // [hidden, obs]
    PrecisionTensor entity_l1_w; // [16, 43]
    PrecisionTensor entity_l2_w; // [hidden, 16]
    int obs_size, hidden;
};

struct ColosseumEntityEncoderActivations {
    PrecisionTensor out;          // [B, hidden] = global + pooled
    PrecisionTensor saved_obs;    // [B, obs] (for global wgrad)
    PrecisionTensor npc_flat;     // [B*24, 43] contiguous NPC records (for l1 wgrad)
    PrecisionTensor entity_z1;    // [B*24, 16] pre-GELU (for GELU' in backward)
    PrecisionTensor entity_h1;    // [B*24, 16] post-GELU (for l2 wgrad)
    PrecisionTensor entity_e;     // [B*24, hidden] per-NPC embeddings
    PrecisionTensor grad_e;       // [B*24, hidden] maxpool backward scratch
    PrecisionTensor grad_z1;      // [B*24, 16] backward scratch
    IntTensor pool_argmax;        // [B, hidden] winning NPC index per channel (-1 if none)
    PrecisionTensor global_wgrad;    // [hidden, obs]
    PrecisionTensor entity_l1_wgrad; // [16, 43]
    PrecisionTensor entity_l2_wgrad; // [hidden, 16]
};

// Gather the strided NPC block out of the flat obs into a tight [B*24, 43] buffer.
__global__ void colo_ent_gather_npcs(
    precision_t* __restrict__ npc_flat, const precision_t* __restrict__ obs,
    int B, int obs_size) {
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    int total = B * COLO_ENT_NPC_BLOCK;
    if (idx >= total) return;
    int b = idx / COLO_ENT_NPC_BLOCK;
    int off = idx % COLO_ENT_NPC_BLOCK;
    npc_flat[idx] = obs[(int64_t)b * obs_size + COLO_ENT_NPC_START + off];
}

// out[i] = 0.5*x*(1 + tanh(0.7978845608*(x + 0.044715*x^3)))  (tanh GELU)
__device__ __forceinline__ float colo_ent_gelu_fwd(float x) {
    float inner = 0.7978845608028654f * (x + 0.044715f * x * x * x);
    return 0.5f * x * (1.0f + tanhf(inner));
}

// d/dx of the tanh GELU above. Matches the forward exactly so finite-difference holds.
__device__ __forceinline__ float colo_ent_gelu_grad(float x) {
    float x3 = x * x * x;
    float inner = 0.7978845608028654f * (x + 0.044715f * x3);
    float t = tanhf(inner);
    float dinner = 0.7978845608028654f * (1.0f + 3.0f * 0.044715f * x * x);
    return 0.5f * (1.0f + t) + 0.5f * x * (1.0f - t * t) * dinner;
}

__global__ void colo_ent_gelu_fwd_kernel(
    precision_t* __restrict__ h1, const precision_t* __restrict__ z1, int total) {
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx >= total) return;
    h1[idx] = from_float(colo_ent_gelu_fwd(to_float(z1[idx])));
}

// grad_z1 = grad_h1 * GELU'(z1), in place over grad_z1 (which holds grad_h1 on entry).
__global__ void colo_ent_gelu_bwd_kernel(
    precision_t* __restrict__ grad_z1, const precision_t* __restrict__ z1, int total) {
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx >= total) return;
    grad_z1[idx] = from_float(to_float(grad_z1[idx]) * colo_ent_gelu_grad(to_float(z1[idx])));
}

// Masked maxpool over the 24 NPC records, per (batch, channel). Inactive records
// (type one-hot sum == 0) are skipped. An all-inactive row writes 0 and argmax -1.
// Ties resolve to the lowest NPC index to match torch.max's argmax tie-break.
__global__ void colo_ent_masked_maxpool(
    precision_t* __restrict__ pooled, int* __restrict__ argmax,
    const precision_t* __restrict__ entity_e, const precision_t* __restrict__ npc_flat,
    int B, int hidden) {
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx >= B * hidden) return;
    int b = idx / hidden;
    int c = idx % hidden;

    float best = -CUDART_INF_F;
    int best_n = -1;
    for (int n = 0; n < COLO_ENT_NUM_NPCS; n++) {
        const precision_t* rec = npc_flat + ((int64_t)b * COLO_ENT_NUM_NPCS + n) * COLO_ENT_FEATS;
        float type_sum = 0.0f;
        for (int t = 0; t < COLO_ENT_TYPE_ONEHOT; t++) type_sum += to_float(rec[t]);
        if (type_sum <= 0.0f) continue;
        float v = to_float(entity_e[((int64_t)b * COLO_ENT_NUM_NPCS + n) * hidden + c]);
        if (v > best) { best = v; best_n = n; }
    }
    pooled[idx] = from_float(best_n < 0 ? 0.0f : best);
    argmax[idx] = best_n;
}

__global__ void colo_ent_add_kernel(
    precision_t* __restrict__ out, const precision_t* __restrict__ pooled, int total) {
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx >= total) return;
    out[idx] = from_float(to_float(out[idx]) + to_float(pooled[idx]));
}

// Route the upstream grad to the winning NPC per (batch, channel); zero elsewhere.
__global__ void colo_ent_maxpool_backward(
    precision_t* __restrict__ grad_e, const precision_t* __restrict__ grad,
    const int* __restrict__ argmax, int B, int hidden) {
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx >= B * hidden) return;
    int b = idx / hidden;
    int c = idx % hidden;
    int n = argmax[idx];
    if (n < 0) return;
    grad_e[((int64_t)b * COLO_ENT_NUM_NPCS + n) * hidden + c] = grad[idx];
}

static PrecisionTensor colo_entity_encoder_forward(void* w, void* activations, PrecisionTensor input, cudaStream_t stream) {
    ColosseumEntityEncoderWeights* ew = (ColosseumEntityEncoderWeights*)w;
    ColosseumEntityEncoderActivations* a = (ColosseumEntityEncoderActivations*)activations;
    int B = input.shape[0];
    int H = ew->hidden;
    int NB = B * COLO_ENT_NUM_NPCS;

    if (a->saved_obs.data) puf_copy(&a->saved_obs, &input, stream);

    puf_mm(&input, &ew->global_w, &a->out, stream);

    colo_ent_gather_npcs<<<grid_size(B * COLO_ENT_NPC_BLOCK), BLOCK_SIZE, 0, stream>>>(
        a->npc_flat.data, input.data, B, ew->obs_size);

    PrecisionTensor npc2d = {.data = a->npc_flat.data, .shape = {NB, COLO_ENT_FEATS}};
    puf_mm(&npc2d, &ew->entity_l1_w, &a->entity_z1, stream);
    colo_ent_gelu_fwd_kernel<<<grid_size(NB * COLO_ENT_BOTTLENECK), BLOCK_SIZE, 0, stream>>>(
        a->entity_h1.data, a->entity_z1.data, NB * COLO_ENT_BOTTLENECK);
    puf_mm(&a->entity_h1, &ew->entity_l2_w, &a->entity_e, stream);

    PrecisionTensor pooled = {.data = a->grad_e.data, .shape = {B, H}};
    colo_ent_masked_maxpool<<<grid_size(B * H), BLOCK_SIZE, 0, stream>>>(
        pooled.data, a->pool_argmax.data, a->entity_e.data, a->npc_flat.data, B, H);
    colo_ent_add_kernel<<<grid_size(B * H), BLOCK_SIZE, 0, stream>>>(
        a->out.data, pooled.data, B * H);
    return a->out;
}

static void colo_entity_encoder_backward(void* w, void* activations, PrecisionTensor grad, cudaStream_t stream) {
    ColosseumEntityEncoderWeights* ew = (ColosseumEntityEncoderWeights*)w;
    ColosseumEntityEncoderActivations* a = (ColosseumEntityEncoderActivations*)activations;
    int B = grad.shape[0];
    int H = ew->hidden;
    int NB = B * COLO_ENT_NUM_NPCS;

    puf_mm_tn(&grad, &a->saved_obs, &a->global_wgrad, stream);

    cudaMemsetAsync(a->grad_e.data, 0, (int64_t)NB * H * sizeof(precision_t), stream);
    colo_ent_maxpool_backward<<<grid_size(B * H), BLOCK_SIZE, 0, stream>>>(
        a->grad_e.data, grad.data, a->pool_argmax.data, B, H);

    puf_mm_tn(&a->grad_e, &a->entity_h1, &a->entity_l2_wgrad, stream);
    puf_mm_nn(&a->grad_e, &ew->entity_l2_w, &a->grad_z1, stream);
    colo_ent_gelu_bwd_kernel<<<grid_size(NB * COLO_ENT_BOTTLENECK), BLOCK_SIZE, 0, stream>>>(
        a->grad_z1.data, a->entity_z1.data, NB * COLO_ENT_BOTTLENECK);

    PrecisionTensor npc2d = {.data = a->npc_flat.data, .shape = {NB, COLO_ENT_FEATS}};
    puf_mm_tn(&a->grad_z1, &npc2d, &a->entity_l1_wgrad, stream);
}

static void colo_entity_encoder_init_weights(void* w, uint64_t* seed, cudaStream_t stream) {
    ColosseumEntityEncoderWeights* ew = (ColosseumEntityEncoderWeights*)w;
    auto init2d = [&](PrecisionTensor& t, int rows, int cols) {
        PrecisionTensor wt = {.data = t.data, .shape = {rows, cols}};
        puf_kaiming_init(&wt, std::sqrt(2.0f), (*seed)++, stream);
    };
    init2d(ew->global_w, ew->hidden, ew->obs_size);
    init2d(ew->entity_l1_w, COLO_ENT_BOTTLENECK, COLO_ENT_FEATS);
    init2d(ew->entity_l2_w, ew->hidden, COLO_ENT_BOTTLENECK);
}

// Boot-time guard: muon and the .bin layout pack params with no padding, but
// alloc_create rounds each tensor up to 16 bytes. They agree only if every param
// tensor's byte size is a multiple of 16, i.e. (bf16) numel % 8 == 0.
static void colo_entity_assert_aligned(int64_t numel, const char* name) {
    if (numel % 8 != 0) {
        fprintf(stderr, "colosseum entity encoder: %s numel %lld not a multiple of 8; "
            "bf16 packing would corrupt weights\n", name, (long long)numel);
        abort();
    }
}

static void colo_entity_encoder_reg_params(void* w, Allocator* alloc) {
    ColosseumEntityEncoderWeights* ew = (ColosseumEntityEncoderWeights*)w;
    ew->global_w    = {.shape = {ew->hidden, ew->obs_size}};
    ew->entity_l1_w = {.shape = {COLO_ENT_BOTTLENECK, COLO_ENT_FEATS}};
    ew->entity_l2_w = {.shape = {ew->hidden, COLO_ENT_BOTTLENECK}};
    colo_entity_assert_aligned(numel(ew->global_w.shape), "global_w");
    colo_entity_assert_aligned(numel(ew->entity_l1_w.shape), "entity_l1_w");
    colo_entity_assert_aligned(numel(ew->entity_l2_w.shape), "entity_l2_w");
    alloc_register(alloc, &ew->global_w);
    alloc_register(alloc, &ew->entity_l1_w);
    alloc_register(alloc, &ew->entity_l2_w);
}

static void colo_entity_encoder_reg_train(void* w, void* activations, Allocator* acts, Allocator* grads, int B_TT) {
    ColosseumEntityEncoderWeights* ew = (ColosseumEntityEncoderWeights*)w;
    ColosseumEntityEncoderActivations* a = (ColosseumEntityEncoderActivations*)activations;
    int H = ew->hidden;
    int NB = B_TT * COLO_ENT_NUM_NPCS;
    *a = {};
    a->out        = {.shape = {B_TT, H}};
    a->saved_obs  = {.shape = {B_TT, ew->obs_size}};
    a->npc_flat   = {.shape = {NB, COLO_ENT_FEATS}};
    a->entity_z1  = {.shape = {NB, COLO_ENT_BOTTLENECK}};
    a->entity_h1  = {.shape = {NB, COLO_ENT_BOTTLENECK}};
    a->entity_e   = {.shape = {NB, H}};
    a->grad_e     = {.shape = {NB, H}};
    a->grad_z1    = {.shape = {NB, COLO_ENT_BOTTLENECK}};
    a->pool_argmax = {.shape = {B_TT, H}};
    alloc_register(acts, &a->out);
    alloc_register(acts, &a->saved_obs);
    alloc_register(acts, &a->npc_flat);
    alloc_register(acts, &a->entity_z1);
    alloc_register(acts, &a->entity_h1);
    alloc_register(acts, &a->entity_e);
    alloc_register(acts, &a->grad_e);
    alloc_register(acts, &a->grad_z1);
    alloc_register(acts, &a->pool_argmax);
    // Grad scratch order MUST match reg_params (global, l1, l2) so muon's shared
    // offset walk pairs each grad with its weight.
    a->global_wgrad    = {.shape = {H, ew->obs_size}};
    a->entity_l1_wgrad = {.shape = {COLO_ENT_BOTTLENECK, COLO_ENT_FEATS}};
    a->entity_l2_wgrad = {.shape = {H, COLO_ENT_BOTTLENECK}};
    alloc_register(grads, &a->global_wgrad);
    alloc_register(grads, &a->entity_l1_wgrad);
    alloc_register(grads, &a->entity_l2_wgrad);
}

static void colo_entity_encoder_reg_rollout(void* w, void* activations, Allocator* alloc, int B) {
    ColosseumEntityEncoderWeights* ew = (ColosseumEntityEncoderWeights*)w;
    ColosseumEntityEncoderActivations* a = (ColosseumEntityEncoderActivations*)activations;
    int H = ew->hidden;
    int NB = B * COLO_ENT_NUM_NPCS;
    a->out        = {.shape = {B, H}};
    a->npc_flat   = {.shape = {NB, COLO_ENT_FEATS}};
    a->entity_z1  = {.shape = {NB, COLO_ENT_BOTTLENECK}};
    a->entity_h1  = {.shape = {NB, COLO_ENT_BOTTLENECK}};
    a->entity_e   = {.shape = {NB, H}};
    a->grad_e     = {.shape = {NB, H}};
    a->pool_argmax = {.shape = {B, H}};
    alloc_register(alloc, &a->out);
    alloc_register(alloc, &a->npc_flat);
    alloc_register(alloc, &a->entity_z1);
    alloc_register(alloc, &a->entity_h1);
    alloc_register(alloc, &a->entity_e);
    alloc_register(alloc, &a->grad_e);
    alloc_register(alloc, &a->pool_argmax);
}

static void* colo_entity_encoder_create_weights(void* self) {
    Encoder* e = (Encoder*)self;
    ColosseumEntityEncoderWeights* ew =
        (ColosseumEntityEncoderWeights*)calloc(1, sizeof(ColosseumEntityEncoderWeights));
    ew->obs_size = e->in_dim;
    ew->hidden = e->out_dim;
    return ew;
}
static void colo_entity_encoder_free_weights(void* weights) { free(weights); }
static void colo_entity_encoder_free_activations(void* activations) { free(activations); }

// Override encoder vtable for known ocean environments. No-op for unknown envs.
// entity_encoder selects the colosseum NPC-pool encoder over the default Linear.
static void create_custom_encoder(const std::string& env_name, Encoder* enc, bool entity_encoder) {
    if (env_name == "osrs_colosseum" && entity_encoder) {
        *enc = Encoder{
            .forward = colo_entity_encoder_forward,
            .backward = colo_entity_encoder_backward,
            .init_weights = colo_entity_encoder_init_weights,
            .reg_params = colo_entity_encoder_reg_params,
            .reg_train = colo_entity_encoder_reg_train,
            .reg_rollout = colo_entity_encoder_reg_rollout,
            .create_weights = colo_entity_encoder_create_weights,
            .free_weights = colo_entity_encoder_free_weights,
            .free_activations = colo_entity_encoder_free_activations,
            .in_dim = enc->in_dim, .out_dim = enc->out_dim,
            .activation_size = sizeof(ColosseumEntityEncoderActivations),
        };
        return;
    }
    if (env_name == "nmmo3") {
        *enc = Encoder{
            .forward = nmmo3_encoder_forward,
            .backward = nmmo3_encoder_backward,
            .init_weights = nmmo3_encoder_init_weights,
            .reg_params = nmmo3_encoder_reg_params,
            .reg_train = nmmo3_encoder_reg_train,
            .reg_rollout = nmmo3_encoder_reg_rollout,
            .create_weights = nmmo3_encoder_create_weights,
            .free_weights = nmmo3_encoder_free_weights,
            .free_activations = nmmo3_encoder_free_activations,
            .in_dim = enc->in_dim, .out_dim = enc->out_dim,
            .activation_size = sizeof(NMMO3EncoderActivations),
        };
    }
}
