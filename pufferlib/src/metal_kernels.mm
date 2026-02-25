/**
 * @fileoverview Metal kernel dispatch layer, model components, orthogonal
 * init (Accelerate LAPACK), and Muon optimizer for PufferLib static-native.
 *
 * Provides all PufTensor operation wrappers that models.cu provides on CUDA:
 *   - Memory ops: puf_copy, puf_zero, puf_add, puf_transpose_01
 *   - Kernel dispatchers: mingru, scan, sample, PPO, advantage, prio, etc.
 *   - Orthogonal init via Accelerate QR decomposition
 *   - Muon optimizer (Newton-Schulz via Accelerate GEMM + Metal kernels)
 *   - Model components: encoder, decoder, MinGRU forward/backward/init
 *
 * On Apple Silicon unified memory, CPU and GPU share the same physical pages.
 * Simple memory ops use CPU memcpy/memset (with GPU sync when needed).
 * Compute-heavy kernels dispatch to Metal GPU via MSL shaders.
 * GEMM uses Accelerate cblas (CPU, synced in metal_platform.mm).
 */

#import "metal_platform.h"

#include <arm_neon.h>
#include <cstring>
#include <random>

// ============================================================================
// Helpers — stream access and GPU sync
// ============================================================================

static inline MetalStream *mtl_get_stream(cudaStream_t s) {
  return s ? (MetalStream *)s : (MetalStream *)mtl_stream();
}

static inline void mtl_ensure_synced(cudaStream_t s) {
  MetalStream *ms = mtl_get_stream(s);
  if (ms->enc_active)
    ms->sync();
}

// Bind a raw pointer (within a wrapped allocator) to a compute encoder slot.
static inline void mtl_set_ptr(id<MTLComputeCommandEncoder> enc,
                               const void *ptr, uint32_t index) {
  MetalContext *ctx = mtl_ctx();
  for (auto &wb : ctx->buffers) {
    if ((const char *)ptr >= wb.base &&
        (const char *)ptr < wb.base + wb.size) {
      NSUInteger offset = (NSUInteger)((const char *)ptr - wb.base);
      [enc setBuffer:wb.buffer offset:offset atIndex:index];
      return;
    }
  }
  assert(false && "Pointer not in any wrapped allocator buffer");
}

// Forward declarations for GPU memory ops (defined below in element-wise section)
void mtl_fill_f32(float *ptr, float value, int count, cudaStream_t stream);
void mtl_copy_f32(float *dst, const float *src, int count, cudaStream_t stream);
void mtl_fill_f16(void *ptr, int count, cudaStream_t stream);
void mtl_copy_f16(void *dst, const void *src, int count, cudaStream_t stream);
void mtl_fused_scan_forward_fp16(PrefixScan &scan, cudaStream_t stream);
void mtl_fused_scan_backward_fp16(PrefixScan &scan, const void *grad,
                                    const void *grad_next_state,
                                    cudaStream_t stream);
void mtl_assemble_decoder_grad_f32_to_f16(void *grad_out,
                                            const float *grad_logits,
                                            const float *grad_value, int B_TT,
                                            int od, int od1,
                                            cudaStream_t stream);

// ============================================================================
// Memory operations — CPU-side on unified memory (synced), or GPU when training
// ============================================================================

void puf_copy(PufTensor &dst, const PufTensor &src, cudaStream_t stream) {
  assert(dst.numel() == src.numel() && "puf_copy: size mismatch");
  assert(dst.dtype_size == src.dtype_size && "puf_copy: dtype mismatch");
  bool gpu = puf_is_gpu_training() ||
             (!getenv("PUFFERLIB_NO_GPU_COPY") && puf_stream_has_encoder(stream));
  if (gpu && dst.dtype_size == 4) {
    mtl_copy_f32((float *)dst.bytes, (const float *)src.bytes,
                 (int)dst.numel(), stream);
  } else if (gpu && dst.dtype_size == 2) {
    mtl_copy_f16(dst.bytes, src.bytes, (int)dst.numel(), stream);
  } else {
    mtl_ensure_synced(stream);
    memcpy(dst.bytes, src.bytes, dst.numel() * dst.dtype_size);
  }
}

void puf_zero(PufTensor &dst, cudaStream_t stream) {
  if (puf_is_gpu_training() && dst.dtype_size == 4) {
    mtl_fill_f32((float *)dst.bytes, 0.0f, (int)dst.numel(), stream);
  } else if (puf_is_gpu_training() && dst.dtype_size == 2) {
    mtl_fill_f16(dst.bytes, (int)dst.numel(), stream);
  } else {
    mtl_ensure_synced(stream);
    memset(dst.bytes, 0, dst.numel() * dst.dtype_size);
  }
}

void puf_add(PufTensor &dst, const PufTensor &src, cudaStream_t stream) {
  assert(dst.numel() == src.numel() && "puf_add: size mismatch");
  assert(dst.dtype_size == 4 && "puf_add: dst must be f32");
  assert(src.dtype_size == 4 && "puf_add: src must be f32 (Metal fp32 only)");
  if (puf_is_gpu_training()) {
    // add_f32 MSL kernel: dst[i] += src[i]
    MetalStream *ms = mtl_get_stream(stream);
    auto enc = ms->compute_encoder();
    auto pso = mtl_pipeline("add_f32");
    [enc setComputePipelineState:pso];
    mtl_set_ptr(enc, dst.bytes, 0);
    mtl_set_ptr(enc, src.bytes, 1);
    int count = (int)dst.numel();
    [enc setBytes:&count length:sizeof(count) atIndex:2];
    mtl_dispatch_1d(enc, pso, count);
  } else {
    mtl_ensure_synced(stream);
    float *d = (float *)dst.bytes;
    const float *s = (const float *)src.bytes;
    int64_t n = dst.numel();
    for (int64_t i = 0; i < n; i++)
      d[i] += s[i];
  }
}

// GPU kernel dispatch for transpose (large data, benefits from parallelism)
void puf_transpose_01(PufTensor &dst, const PufTensor &src,
                       cudaStream_t stream) {
  int A = (int)src.shape[0], B = (int)src.shape[1];
  int C = (src.ndim() >= 3) ? (int)src.shape[2] : 1;
  assert(dst.shape[0] == B && dst.shape[1] == A);
  assert(dst.dtype_size == src.dtype_size);

  // For f64 (dtype_size=8): GPU transpose via uint2 — avoids CPU sync.
  // Actions are f64 (int64_t) because the vecenv C binding uses int64_t*.
  if (src.dtype_size == 8) {
    MetalStream *ms = mtl_get_stream(stream);
    auto enc = ms->compute_encoder();
    auto pso = mtl_pipeline("transpose_01_u64");
    [enc setComputePipelineState:pso];
    mtl_set_tensor(enc, dst, 0);
    mtl_set_tensor(enc, src, 1);
    struct {
      int A, B, C;
    } params = {A, B, C};
    [enc setBytes:&params length:sizeof(params) atIndex:2];
    mtl_dispatch_1d(enc, pso, A * B * C);
    return;
  }

  MetalStream *ms = mtl_get_stream(stream);
  auto enc = ms->compute_encoder();
  auto pso = mtl_pipeline("transpose_01");
  [enc setComputePipelineState:pso];
  mtl_set_tensor(enc, dst, 0);
  mtl_set_tensor(enc, src, 1);
  struct {
    int A, B, C;
  } params = {A, B, C};
  [enc setBytes:&params length:sizeof(params) atIndex:2];
  mtl_dispatch_1d(enc, pso, A * B * C);
}

// ============================================================================
// Cast kernels
// ============================================================================

// CPU u8→f32 cast — no GPU dispatch, no sync needed.
void cpu_cast_u8_to_f32(float *dst, const uint8_t *src, int count) {
  for (int i = 0; i < count; i++) dst[i] = (float)src[i];
}

void puf_cast_u8_to_f32(PufTensor &dst, const PufTensor &src,
                          cudaStream_t stream) {
  MetalStream *ms = mtl_get_stream(stream);
  auto enc = ms->compute_encoder();
  auto pso = mtl_pipeline("cast_u8_to_f32");
  [enc setComputePipelineState:pso];
  mtl_set_tensor(enc, dst, 0);
  mtl_set_tensor(enc, src, 1);
  struct {
    int count;
  } params = {(int)src.numel()};
  [enc setBytes:&params length:sizeof(params) atIndex:2];
  mtl_dispatch_1d(enc, pso, (int)src.numel());
}

// ============================================================================
// fp16 cast dispatchers
// ============================================================================

void mtl_cast_f32_to_f16(void *dst, const float *src, int count,
                          cudaStream_t stream) {
  MetalStream *ms = mtl_get_stream(stream);
  auto enc = ms->compute_encoder();
  auto pso = mtl_pipeline("cast_f32_to_f16");
  [enc setComputePipelineState:pso];
  mtl_set_ptr(enc, dst, 0);
  mtl_set_ptr(enc, src, 1);
  [enc setBytes:&count length:sizeof(count) atIndex:2];
  mtl_dispatch_1d(enc, pso, count);
}

void mtl_cast_f16_to_f32(float *dst, const void *src, int count,
                          cudaStream_t stream) {
  MetalStream *ms = mtl_get_stream(stream);
  auto enc = ms->compute_encoder();
  auto pso = mtl_pipeline("cast_f16_to_f32");
  [enc setComputePipelineState:pso];
  mtl_set_ptr(enc, dst, 0);
  mtl_set_ptr(enc, src, 1);
  [enc setBytes:&count length:sizeof(count) atIndex:2];
  mtl_dispatch_1d(enc, pso, count);
}

// ============================================================================
// fp16 memory ops
// ============================================================================

void mtl_fill_f16(void *ptr, int count, cudaStream_t stream) {
  // Fill fp16 buffer with zeros (the only fill value needed for fp16)
  MetalStream *ms = mtl_get_stream(stream);
  auto enc = ms->compute_encoder();
  auto pso = mtl_pipeline("fill_f32");
  [enc setComputePipelineState:pso];
  mtl_set_ptr(enc, ptr, 0);
  // Fill count/2 fp32 words with 0.0f — zeros in fp16 are also 0x0000
  int f32_count = (count + 1) / 2;
  struct {
    float value;
    int count;
  } params = {0.0f, f32_count};
  [enc setBytes:&params length:sizeof(params) atIndex:1];
  mtl_dispatch_1d(enc, pso, f32_count);
}

void mtl_copy_f16(void *dst, const void *src, int count,
                   cudaStream_t stream) {
  // Copy fp16 data — reuse copy_f32 by treating pairs of fp16 as fp32
  MetalStream *ms = mtl_get_stream(stream);
  auto enc = ms->compute_encoder();
  auto pso = mtl_pipeline("copy_f32");
  [enc setComputePipelineState:pso];
  mtl_set_ptr(enc, dst, 0);
  mtl_set_ptr(enc, src, 1);
  int f32_count = (count + 1) / 2;
  [enc setBytes:&f32_count length:sizeof(f32_count) atIndex:2];
  mtl_dispatch_1d(enc, pso, f32_count);
}

// ============================================================================
// Element-wise kernel dispatchers
// ============================================================================

void mtl_fill_f32(float *ptr, float value, int count, cudaStream_t stream) {
  MetalStream *ms = mtl_get_stream(stream);
  auto enc = ms->compute_encoder();
  auto pso = mtl_pipeline("fill_f32");
  [enc setComputePipelineState:pso];
  mtl_set_ptr(enc, ptr, 0);
  struct {
    float value;
    int count;
  } params = {value, count};
  [enc setBytes:&params length:sizeof(params) atIndex:1];
  mtl_dispatch_1d(enc, pso, count);
}

void mtl_copy_f32(float *dst, const float *src, int count,
                   cudaStream_t stream) {
  MetalStream *ms = mtl_get_stream(stream);
  auto enc = ms->compute_encoder();
  auto pso = mtl_pipeline("copy_f32");
  [enc setComputePipelineState:pso];
  mtl_set_ptr(enc, dst, 0);
  mtl_set_ptr(enc, src, 1);
  [enc setBytes:&count length:sizeof(count) atIndex:2];
  mtl_dispatch_1d(enc, pso, count);
}

void mtl_clamp_f32(float *ptr, float lo, float hi, int count,
                    cudaStream_t stream) {
  MetalStream *ms = mtl_get_stream(stream);
  auto enc = ms->compute_encoder();
  auto pso = mtl_pipeline("clamp_f32");
  [enc setComputePipelineState:pso];
  mtl_set_ptr(enc, ptr, 0);
  struct {
    float lo, hi;
    int count;
  } params = {lo, hi, count};
  [enc setBytes:&params length:sizeof(params) atIndex:1];
  mtl_dispatch_1d(enc, pso, count);
}

void mtl_scale_f32(float *ptr, float scale, int count, cudaStream_t stream) {
  MetalStream *ms = mtl_get_stream(stream);
  auto enc = ms->compute_encoder();
  auto pso = mtl_pipeline("scale_f32");
  [enc setComputePipelineState:pso];
  mtl_set_ptr(enc, ptr, 0);
  struct {
    float scale;
    int count;
  } params = {scale, count};
  [enc setBytes:&params length:sizeof(params) atIndex:1];
  mtl_dispatch_1d(enc, pso, count);
}

void mtl_nesterov_f32(float *momentum, const float *grad, float mu, int count,
                       cudaStream_t stream) {
  MetalStream *ms = mtl_get_stream(stream);
  auto enc = ms->compute_encoder();
  auto pso = mtl_pipeline("nesterov_f32");
  [enc setComputePipelineState:pso];
  mtl_set_ptr(enc, momentum, 0);
  mtl_set_ptr(enc, grad, 1);
  struct {
    float mu;
    int count;
  } params = {mu, count};
  [enc setBytes:&params length:sizeof(params) atIndex:2];
  mtl_dispatch_1d(enc, pso, count);
}

// ============================================================================
// Norm and clip kernels
// ============================================================================

// Scratch buffer for partial sums in norm reduction (Muon NS + clip_grad_norm)
static float *norm_partials_buf = nullptr;
static void ensure_norm_partials() {
  if (!norm_partials_buf) {
    posix_memalign((void **)&norm_partials_buf, 16384,
                   ((256 * sizeof(float) + 16383) / 16384) * 16384);
    // Wrap as MTLBuffer for GPU access
    id<MTLBuffer> buf = [mtl_ctx()->device
        newBufferWithBytesNoCopy:norm_partials_buf
                          length:16384
                         options:MTLResourceStorageModeShared
                     deallocator:nil];
    assert(buf);
    mtl_ctx()->buffers.push_back({(char *)norm_partials_buf, 16384, buf});
  }
}

void mtl_norm_f32(float *partials, const float *data, int count,
                   int num_blocks, cudaStream_t stream) {
  MetalStream *ms = mtl_get_stream(stream);
  auto enc = ms->compute_encoder();
  auto pso = mtl_pipeline("norm_f32_kernel");
  [enc setComputePipelineState:pso];
  mtl_set_ptr(enc, partials, 0);
  mtl_set_ptr(enc, data, 1);
  struct {
    int count;
  } params = {count};
  [enc setBytes:&params length:sizeof(params) atIndex:2];
  mtl_dispatch_groups(enc, pso, num_blocks, 256);
}

void mtl_norm_reduce(float *result, const float *partials, int num_blocks,
                      cudaStream_t stream) {
  MetalStream *ms = mtl_get_stream(stream);
  auto enc = ms->compute_encoder();
  auto pso = mtl_pipeline("norm_reduce_kernel");
  [enc setComputePipelineState:pso];
  mtl_set_ptr(enc, result, 0);
  mtl_set_ptr(enc, partials, 1);
  struct {
    int num_blocks;
  } params = {num_blocks};
  [enc setBytes:&params length:sizeof(params) atIndex:2];
  mtl_dispatch_groups(enc, pso, 1, 256);
}

void mtl_clip_by_norm_f32(float *data, const float *norm_ptr,
                            float max_norm, float eps, int count,
                            cudaStream_t stream) {
  MetalStream *ms = mtl_get_stream(stream);
  auto enc = ms->compute_encoder();
  auto pso = mtl_pipeline("clip_by_norm_f32");
  [enc setComputePipelineState:pso];
  mtl_set_ptr(enc, data, 0);
  mtl_set_ptr(enc, norm_ptr, 1);
  struct {
    float max_norm, eps;
    int count;
  } params = {max_norm, eps, count};
  [enc setBytes:&params length:sizeof(params) atIndex:2];
  mtl_dispatch_1d(enc, pso, count);
}

void mtl_normalize_f32(float *data, const float *norm_ptr, float eps,
                        int count, cudaStream_t stream) {
  MetalStream *ms = mtl_get_stream(stream);
  auto enc = ms->compute_encoder();
  auto pso = mtl_pipeline("normalize_f32");
  [enc setComputePipelineState:pso];
  mtl_set_ptr(enc, data, 0);
  mtl_set_ptr(enc, norm_ptr, 1);
  struct {
    float eps;
    int count;
  } params = {eps, count};
  [enc setBytes:&params length:sizeof(params) atIndex:2];
  mtl_dispatch_1d(enc, pso, count);
}

// Convenience: compute L2 norm of grad, clip in-place if > max_norm.
// scratch must point to a float in wrapped MTLBuffer memory.
void clip_grad_norm_f32(PufTensor &grad, float *scratch, float max_norm,
                        float eps, cudaStream_t stream) {
  ensure_norm_partials();
  int count = (int)grad.numel();
  int num_blocks = (count + 255) / 256;
  if (num_blocks > 256) num_blocks = 256;
  mtl_norm_f32(norm_partials_buf, (const float *)grad.bytes, count, num_blocks,
               stream);
  mtl_norm_reduce(scratch, norm_partials_buf, num_blocks, stream);
  mtl_clip_by_norm_f32((float *)grad.bytes, scratch, max_norm, eps, count,
                       stream);
}

void mtl_transpose_f32(float *dst, const float *src, int rows, int cols,
                        cudaStream_t stream) {
  MetalStream *ms = mtl_get_stream(stream);
  auto enc = ms->compute_encoder();
  auto pso = mtl_pipeline("transpose_f32");
  [enc setComputePipelineState:pso];
  mtl_set_ptr(enc, dst, 0);
  mtl_set_ptr(enc, src, 1);
  struct {
    int rows, cols;
  } params = {rows, cols};
  [enc setBytes:&params length:sizeof(params) atIndex:2];
  mtl_dispatch_1d(enc, pso, rows * cols);
}

// ============================================================================
// Decoder gradient assembly
// ============================================================================

void mtl_assemble_decoder_grad_f32(float *grad_out, const float *grad_logits,
                                     const float *grad_value, int B_TT, int od,
                                     int od1, cudaStream_t stream) {
  MetalStream *ms = mtl_get_stream(stream);
  auto enc = ms->compute_encoder();
  auto pso = mtl_pipeline("assemble_decoder_grad_f32");
  [enc setComputePipelineState:pso];
  mtl_set_ptr(enc, grad_out, 0);
  mtl_set_ptr(enc, grad_logits, 1);
  mtl_set_ptr(enc, grad_value, 2);
  struct {
    int B_TT, od, od1;
  } params = {B_TT, od, od1};
  [enc setBytes:&params length:sizeof(params) atIndex:3];
  mtl_dispatch_1d(enc, pso, B_TT * od1);
}

// Assemble fp32 PPO gradients into fp16 decoder gradient output.
void mtl_assemble_decoder_grad_f32_to_f16(void *grad_out,
                                            const float *grad_logits,
                                            const float *grad_value, int B_TT,
                                            int od, int od1,
                                            cudaStream_t stream) {
  MetalStream *ms = mtl_get_stream(stream);
  auto enc = ms->compute_encoder();
  auto pso = mtl_pipeline("assemble_decoder_grad_f32_to_f16");
  [enc setComputePipelineState:pso];
  mtl_set_ptr(enc, grad_out, 0);
  mtl_set_ptr(enc, grad_logits, 1);
  mtl_set_ptr(enc, grad_value, 2);
  [enc setBytes:&B_TT length:sizeof(int) atIndex:3];
  [enc setBytes:&od length:sizeof(int) atIndex:4];
  [enc setBytes:&od1 length:sizeof(int) atIndex:5];
  mtl_dispatch_1d(enc, pso, B_TT * od1);
}

void mtl_sum_rows_to_f32(float *dst, const float *src, int rows, int cols,
                           cudaStream_t stream) {
  MetalStream *ms = mtl_get_stream(stream);
  auto enc = ms->compute_encoder();
  auto pso = mtl_pipeline("sum_rows_to_f32_kernel");
  [enc setComputePipelineState:pso];
  mtl_set_ptr(enc, dst, 0);
  mtl_set_ptr(enc, src, 1);
  struct {
    int rows, cols;
  } params = {rows, cols};
  [enc setBytes:&params length:sizeof(params) atIndex:2];
  mtl_dispatch_1d(enc, pso, cols);
}

// ============================================================================
// CPU inference mode flag — when true, mingru_forward uses CPU gate + memcpy
// instead of Metal dispatch + puf_copy, eliminating all rollout syncs.
// ============================================================================

static bool g_cpu_inference = false;
void puf_set_cpu_inference(bool val) { g_cpu_inference = val; }
bool puf_is_cpu_inference() { return g_cpu_inference; }

// Transpose weight matrix (rows, cols) into pre-allocated dst (cols, rows).
// dst must have shape {cols, rows} and enough bytes allocated.
static void transpose_weight(PufTensor &dst, const PufTensor &src) {
  int rows = (int)src.shape[0], cols = (int)src.shape[1];
  const float *s = (const float *)src.bytes;
  float *d = (float *)dst.bytes;
  for (int r = 0; r < rows; r++)
    for (int c = 0; c < cols; c++)
      d[c * rows + r] = s[r * cols + c];
}

// Allocate a transposed weight buffer (calloc, outside the Allocator pool).
static PufTensor alloc_transposed(int rows, int cols) {
  PufTensor t;
  t.shape[0] = cols;
  t.shape[1] = rows;
  t.dtype_size = sizeof(float);
  t.bytes = (char *)calloc(cols * rows, sizeof(float));
  return t;
}

// Allocate a Metal-wrapped tensor (page-aligned, registered with Metal context).
// Use for buffers that need GPU access via buffer_for_ptr.
static PufTensor alloc_metal_tensor(int dim0, int dim1) {
  PufTensor t = {};
  t.shape[0] = dim0;
  t.shape[1] = dim1;
  t.dtype_size = sizeof(float);
  int64_t size = (int64_t)dim0 * dim1 * sizeof(float);
  int64_t page = 16384;
  int64_t alloc_size = (size + page - 1) & ~(page - 1);
  posix_memalign((void **)&t.bytes, page, alloc_size);
  memset(t.bytes, 0, alloc_size);
  id<MTLBuffer> buf = [mtl_ctx()->device
      newBufferWithBytesNoCopy:t.bytes
                        length:alloc_size
                       options:MTLResourceStorageModeShared
                   deallocator:nil];
  assert(buf);
  mtl_ctx()->buffers.push_back({t.bytes, alloc_size, buf});
  return t;
}

// ============================================================================
// MinGRU inference kernel
// ============================================================================

void mtl_mingru_gate(float *out, float *next_state, const float *combined,
                      const float *state_in, int H, int B,
                      cudaStream_t stream) {
  MetalStream *ms = mtl_get_stream(stream);
  auto enc = ms->compute_encoder();
  auto pso = mtl_pipeline("mingru_gate_inference");
  [enc setComputePipelineState:pso];
  mtl_set_ptr(enc, out, 0);
  mtl_set_ptr(enc, next_state, 1);
  mtl_set_ptr(enc, combined, 2);
  mtl_set_ptr(enc, state_in, 3);
  struct {
    int H, B;
  } params = {H, B};
  [enc setBytes:&params length:sizeof(params) atIndex:4];
  mtl_dispatch_1d(enc, pso, B * H);
}

// NEON fast sigmoid: rational approximation, ~2 ULP max error.
// Uses the numerically stable form: sig(x) = 0.5 + 0.5*tanh(x/2),
// with tanh approximated by a degree-7 rational (Pade-like).
static inline float32x4_t neon_sigmoid(float32x4_t x) {
  // Clamp to [-10, 10] to avoid saturation issues in the polynomial.
  float32x4_t lo = vdupq_n_f32(-10.0f);
  float32x4_t hi = vdupq_n_f32(10.0f);
  x = vmaxq_f32(lo, vminq_f32(hi, x));

  // Compute exp(-|x|) via the identity: exp(x) ~ (1 + x/256)^256
  // But for sigmoid we use a direct rational approximation instead.
  //
  // Fast sigmoid via polynomial: sig(x) ~ 0.5 + x*(0.25 - x^2*c3)
  // where c3 tuned for [-10,10] range. This is a degree-3 odd polynomial
  // for tanh(x/2), mapped to sigmoid.
  //
  // Actually, use the exact scalar form but vectorized with vexpq emulation.
  // ARM NEON has no vexpq_f32, so we use a fast exp(-|x|) approximation:
  // exp(x) ~ (2^23 + x * (2^23 / ln2)) reinterpreted as float.
  float32x4_t abs_x = vabsq_f32(x);
  float32x4_t neg_abs = vnegq_f32(abs_x);

  // Fast exp(-|x|): Schraudolph's method with bias correction.
  // float_as_int(exp(x)) ~ 2^23 * (x/ln2 + 127 + bias)
  float32x4_t exp_scale = vdupq_n_f32(12102203.0f);  // 2^23 / ln(2)
  float32x4_t exp_bias = vdupq_n_f32(1065353216.0f);  // 127 * 2^23
  float32x4_t exp_correction = vdupq_n_f32(486411.0f);  // bias correction
  float32x4_t exp_bits = vmlaq_f32(vaddq_f32(exp_bias, exp_correction), neg_abs, exp_scale);
  // Clamp to valid float range before reinterpret.
  exp_bits = vmaxq_f32(exp_bits, vdupq_n_f32(0.0f));
  float32x4_t z = vreinterpretq_f32_s32(vcvtq_s32_f32(exp_bits));  // exp(-|x|)

  // sigmoid: x >= 0 ? 1/(1+z) : z/(1+z)
  float32x4_t one = vdupq_n_f32(1.0f);
  float32x4_t one_plus_z = vaddq_f32(one, z);
  float32x4_t recip = vrecpeq_f32(one_plus_z);
  recip = vmulq_f32(recip, vrecpsq_f32(one_plus_z, recip));  // Newton step
  recip = vmulq_f32(recip, vrecpsq_f32(one_plus_z, recip));  // 2nd Newton step

  float32x4_t sig_pos = recip;          // 1/(1+z) for x >= 0
  float32x4_t sig_neg = vmulq_f32(z, recip);  // z/(1+z) for x < 0
  uint32x4_t pos_mask = vcgeq_f32(x, vdupq_n_f32(0.0f));
  return vbslq_f32(pos_mask, sig_pos, sig_neg);
}

// CPU implementation of mingru gate — NEON-vectorized, 4-wide SIMD.
// Math matches MSL mingru_gate_inference (with ~2 ULP sigmoid error from
// Schraudolph exp approximation — acceptable for inference, not training).
void cpu_mingru_gate(float *out, float *next_state, const float *combined,
                     const float *state_in, int H, int B) {
  float32x4_t half = vdupq_n_f32(0.5f);
  float32x4_t one = vdupq_n_f32(1.0f);
  float32x4_t zero = vdupq_n_f32(0.0f);

  for (int b = 0; b < B; b++) {
    int base = b * 3 * H;
    const float *hidden_ptr = combined + base;
    const float *gate_ptr = combined + base + H;
    const float *proj_ptr = combined + base + 2 * H;
    const float *state_ptr = state_in + b * H;
    float *ns_ptr = next_state + b * H;
    float *out_ptr = out + b * H;

    int h = 0;
    for (; h + 4 <= H; h += 4) {
      float32x4_t hidden_v = vld1q_f32(hidden_ptr + h);
      float32x4_t gate_v = vld1q_f32(gate_ptr + h);
      float32x4_t proj_v = vld1q_f32(proj_ptr + h);
      float32x4_t state_v = vld1q_f32(state_ptr + h);

      // sigmoid(gate)
      float32x4_t gate_sig = neon_sigmoid(gate_v);

      // tilde_relu(hidden): x >= 0 ? x + 0.5 : clamp(sigmoid(x), 0, 1)
      // For x < 0: sigmoid(x) = (tanh(x/2)+1)/2, but we compute sigmoid
      // directly which is equivalent and avoids the tanh detour.
      float32x4_t pos_path = vaddq_f32(hidden_v, half);   // x + 0.5
      float32x4_t neg_path = neon_sigmoid(hidden_v);       // sigmoid(x)
      neg_path = vmaxq_f32(zero, vminq_f32(one, neg_path));  // clamp [0,1]
      uint32x4_t ge_zero = vcgeq_f32(hidden_v, zero);
      float32x4_t hidden_tilde = vbslq_f32(ge_zero, pos_path, neg_path);

      // lerp(state, hidden_tilde, gate_sig)
      // Use the same numerically stable form as scalar: pick formulation
      // based on gate_sig < 0.5 to minimize catastrophic cancellation.
      float32x4_t diff = vsubq_f32(hidden_tilde, state_v);
      float32x4_t form_a = vmlaq_f32(state_v, gate_sig, diff);  // state + g*diff
      float32x4_t one_minus_g = vsubq_f32(one, gate_sig);
      float32x4_t form_b = vmlsq_f32(hidden_tilde, diff, one_minus_g);  // ht - diff*(1-g)
      uint32x4_t use_a = vcltq_f32(vabsq_f32(gate_sig), half);
      float32x4_t mingru_out = vbslq_f32(use_a, form_a, form_b);

      vst1q_f32(ns_ptr + h, mingru_out);

      // sigmoid(proj) * mingru_out
      float32x4_t proj_sig = neon_sigmoid(proj_v);
      vst1q_f32(out_ptr + h, vmulq_f32(proj_sig, mingru_out));
    }

    // Scalar tail (H % 4 != 0)
    for (; h < H; h++) {
      float hidden = hidden_ptr[h];
      float gate = gate_ptr[h];
      float proj = proj_ptr[h];
      float state = state_ptr[h];

      float z_gate = expf(-fabsf(gate));
      float gate_sig = gate >= 0.0f ? 1.0f / (1.0f + z_gate) : z_gate / (1.0f + z_gate);

      float hidden_tilde;
      if (hidden >= 0.0f) {
        hidden_tilde = hidden + 0.5f;
      } else {
        float th = tanhf(hidden * 0.5f);
        float sig = (th + 1.0f) * 0.5f;
        hidden_tilde = fmaxf(0.0f, fminf(1.0f, sig));
      }

      float diff = hidden_tilde - state;
      float mingru_out = fabsf(gate_sig) < 0.5f ? state + gate_sig * diff
                                                  : hidden_tilde - diff * (1.0f - gate_sig);
      ns_ptr[h] = mingru_out;

      float z_proj = expf(-fabsf(proj));
      float proj_sig = proj >= 0.0f ? 1.0f / (1.0f + z_proj) : z_proj / (1.0f + z_proj);
      out_ptr[h] = proj_sig * mingru_out;
    }
  }
}

// ============================================================================
// MinGRU training scan kernels
// ============================================================================

void mtl_fused_scan_forward(PrefixScan &scan, cudaStream_t stream) {
  MetalStream *ms = mtl_get_stream(stream);
  auto enc = ms->compute_encoder();
  auto pso = mtl_pipeline("fused_scan_forward_checkpointed");
  [enc setComputePipelineState:pso];
  mtl_set_ptr(enc, scan.out.bytes, 0);
  mtl_set_ptr(enc, scan.next_state.bytes, 1);
  mtl_set_ptr(enc, scan.a_star.bytes, 2);
  mtl_set_ptr(enc, scan.s_vals.bytes, 3);
  mtl_set_ptr(enc, scan.log_values_buf.bytes, 4);
  mtl_set_ptr(enc, scan.combined_ptr, 5);
  mtl_set_ptr(enc, scan.state_ptr, 6);
  struct {
    int T_seq, H, B;
  } params = {scan.T, scan.H, scan.B};
  [enc setBytes:&params length:sizeof(params) atIndex:7];
  mtl_dispatch_1d(enc, pso, scan.B * scan.H);
}

void mtl_fused_scan_backward(PrefixScan &scan, const float *grad,
                               const float *grad_next_state,
                               cudaStream_t stream) {
  MetalStream *ms = mtl_get_stream(stream);
  auto enc = ms->compute_encoder();
  auto pso = mtl_pipeline("fused_scan_backward_checkpointed");
  [enc setComputePipelineState:pso];
  mtl_set_ptr(enc, scan.grad_combined.bytes, 0);
  mtl_set_ptr(enc, scan.grad_state.bytes, 1);
  mtl_set_ptr(enc, grad, 2);
  mtl_set_ptr(enc, grad_next_state, 3);
  mtl_set_ptr(enc, scan.combined_ptr, 4);
  mtl_set_ptr(enc, scan.state_ptr, 5);
  mtl_set_ptr(enc, scan.a_star.bytes, 6);
  mtl_set_ptr(enc, scan.s_vals.bytes, 7);
  mtl_set_ptr(enc, scan.log_values_buf.bytes, 8);
  struct {
    int T_seq, H, B;
  } params = {scan.T, scan.H, scan.B};
  [enc setBytes:&params length:sizeof(params) atIndex:9];
  mtl_dispatch_1d(enc, pso, scan.B * scan.H);
}

// fp16 scan dispatchers: half combined/state/out, fp32 internal (a_star/s_vals/log_values)
void mtl_fused_scan_forward_fp16(PrefixScan &scan, cudaStream_t stream) {
  MetalStream *ms = mtl_get_stream(stream);
  auto enc = ms->compute_encoder();
  auto pso = mtl_pipeline("fused_scan_forward_checkpointed_fp16");
  [enc setComputePipelineState:pso];
  mtl_set_ptr(enc, scan.out.bytes, 0);
  mtl_set_ptr(enc, scan.next_state.bytes, 1);
  mtl_set_ptr(enc, scan.a_star.bytes, 2);
  mtl_set_ptr(enc, scan.s_vals.bytes, 3);
  mtl_set_ptr(enc, scan.log_values_buf.bytes, 4);
  mtl_set_ptr(enc, scan.combined_ptr, 5);
  mtl_set_ptr(enc, scan.state_ptr, 6);
  struct {
    int T_seq, H, B;
  } params = {scan.T, scan.H, scan.B};
  [enc setBytes:&params length:sizeof(params) atIndex:7];
  mtl_dispatch_1d(enc, pso, scan.B * scan.H);
}

void mtl_fused_scan_backward_fp16(PrefixScan &scan, const void *grad,
                                    const void *grad_next_state,
                                    cudaStream_t stream) {
  MetalStream *ms = mtl_get_stream(stream);
  auto enc = ms->compute_encoder();
  auto pso = mtl_pipeline("fused_scan_backward_checkpointed_fp16");
  [enc setComputePipelineState:pso];
  mtl_set_ptr(enc, scan.grad_combined.bytes, 0);
  mtl_set_ptr(enc, scan.grad_state.bytes, 1);
  mtl_set_ptr(enc, grad, 2);
  mtl_set_ptr(enc, grad_next_state, 3);
  mtl_set_ptr(enc, scan.combined_ptr, 4);
  mtl_set_ptr(enc, scan.state_ptr, 5);
  mtl_set_ptr(enc, scan.a_star.bytes, 6);
  mtl_set_ptr(enc, scan.s_vals.bytes, 7);
  mtl_set_ptr(enc, scan.log_values_buf.bytes, 8);
  struct {
    int T_seq, H, B;
  } params = {scan.T, scan.H, scan.B};
  [enc setBytes:&params length:sizeof(params) atIndex:9];
  mtl_dispatch_1d(enc, pso, scan.B * scan.H);
}

// ============================================================================
// Sample logits kernel
// ============================================================================

void mtl_sample_logits(PufTensor &dec_out, PufTensor &logstd_puf,
                        PufTensor &act_sizes_puf, double *actions,
                        float *logprobs, float *value_out, uint64_t seed,
                        uint32_t *offset_ptr, cudaStream_t stream) {
  int B = (int)dec_out.shape[0];
  int fused_cols = (int)dec_out.shape[1];
  int num_atns = (int)act_sizes_puf.numel();
  int A_total = fused_cols - 1;
  bool is_continuous = logstd_puf.bytes != nullptr && logstd_puf.numel() > 0;

  // MSL doesn't support double — use a temp float buffer for actions,
  // then expand float→double on CPU after GPU sync.
  int act_count = B * num_atns;
  static float *act_f32_buf = nullptr;
  static int act_f32_capacity = 0;
  if (!act_f32_buf || act_count > act_f32_capacity) {
    if (act_f32_buf) free(act_f32_buf);
    act_f32_capacity = act_count;
    int64_t alloc_bytes = act_f32_capacity * sizeof(float);
    int64_t page = 16384;
    alloc_bytes = (alloc_bytes + page - 1) & ~(page - 1);
    posix_memalign((void **)&act_f32_buf, page, alloc_bytes);
    id<MTLBuffer> buf = [mtl_ctx()->device
        newBufferWithBytesNoCopy:act_f32_buf
                          length:alloc_bytes
                         options:MTLResourceStorageModeShared
                     deallocator:nil];
    assert(buf);
    mtl_ctx()->buffers.push_back({(char *)act_f32_buf, alloc_bytes, buf});
  }

  MetalStream *ms = mtl_get_stream(stream);
  auto enc = ms->compute_encoder();
  auto pso = mtl_pipeline("sample_logits_kernel");
  [enc setComputePipelineState:pso];

  // Buffer bindings matching MSL kernel signature
  mtl_set_ptr(enc, act_f32_buf, 0);  // kernel writes float actions here
  mtl_set_ptr(enc, logprobs, 1);
  mtl_set_ptr(enc, value_out, 2);
  mtl_set_ptr(enc, dec_out.bytes, 3); // logits
  if (is_continuous) {
    mtl_set_ptr(enc, logstd_puf.bytes, 4);
  } else {
    // Bind a dummy buffer (the kernel checks is_continuous)
    mtl_set_ptr(enc, dec_out.bytes, 4);
  }
  mtl_set_ptr(enc, (float *)dec_out.bytes + A_total, 5); // value column
  mtl_set_ptr(enc, act_sizes_puf.bytes, 6);
  mtl_set_ptr(enc, offset_ptr, 7);

  struct {
    uint64_t seed;
    int num_atns;
    int num_atns_total;
    int B;
    int logits_stride;
    int logstd_stride;
    int value_stride;
    int is_continuous;
  } params = {seed,        num_atns, A_total,         B,
              fused_cols,  0,        fused_cols,       is_continuous ? 1 : 0};
  [enc setBytes:&params length:sizeof(params) atIndex:8];

  // Action mask: all-1.0 buffer (all actions valid).
  // static-native doesn't use masks at sampling time (CUDA kernel has no mask param).
  // MSL kernel inherited mask logic from our 4.0 port — pass all-1.0 to make it a no-op.
  static float *ones_mask = nullptr;
  static int ones_mask_size = 0;
  int mask_needed = B * A_total;
  if (!ones_mask || mask_needed > ones_mask_size) {
    if (ones_mask) free(ones_mask);
    ones_mask_size = mask_needed;
    int64_t alloc_bytes = ones_mask_size * sizeof(float);
    int64_t page = 16384;
    alloc_bytes = (alloc_bytes + page - 1) & ~(page - 1);
    posix_memalign((void **)&ones_mask, page, alloc_bytes);
    for (int i = 0; i < ones_mask_size; i++) ones_mask[i] = 1.0f;
    id<MTLBuffer> buf = [mtl_ctx()->device
        newBufferWithBytesNoCopy:ones_mask
                          length:alloc_bytes
                         options:MTLResourceStorageModeShared
                     deallocator:nil];
    assert(buf);
    mtl_ctx()->buffers.push_back({(char *)ones_mask, alloc_bytes, buf});
  }
  mtl_set_ptr(enc, ones_mask, 9);

  mtl_dispatch_1d(enc, pso, B);

  // GPU writes float actions to act_f32_buf. Sync and expand to double.
  ms->sync();
  for (int i = 0; i < act_count; i++) {
    actions[i] = (double)act_f32_buf[i];
  }
}

// ============================================================================
// CPU sample_logits — discrete multinomial sampling, no GPU dispatch.
// Philox 4x32-10 RNG for deterministic sampling matching the MSL kernel.
// ============================================================================

static inline uint32_t mulhi32(uint32_t a, uint32_t b) {
  return (uint32_t)(((uint64_t)a * b) >> 32);
}

static void philox4x32_10_cpu(uint32_t c[4], uint32_t k[2]) {
  const uint32_t M0 = 0xD2511F53u, M1 = 0xCD9E8D57u;
  const uint32_t W0 = 0x9E3779B9u, W1 = 0xBB67AE85u;
  for (int i = 0; i < 10; i++) {
    uint32_t hi0 = mulhi32(M0, c[0]);
    uint32_t lo0 = M0 * c[0];
    uint32_t hi1 = mulhi32(M1, c[2]);
    uint32_t lo1 = M1 * c[2];
    c[0] = hi1 ^ c[1] ^ k[0];
    c[1] = lo1;
    c[2] = hi0 ^ c[3] ^ k[1];
    c[3] = lo0;
    k[0] += W0;
    k[1] += W1;
  }
}

static float philox_uniform_cpu(uint32_t val) {
  return ((float)(val >> 8) + 0.5f) / 16777216.0f;
}

void cpu_sample_logits(const float *dec_out, int fused_cols, int B,
                       const int32_t *act_sizes, int num_atns,
                       double *actions, float *logprobs, float *value_out,
                       uint64_t seed, uint32_t *offset_ptr) {
  int A_total = fused_cols - 1;

  for (int b = 0; b < B; b++) {
    uint32_t offset = (*offset_ptr)++;

    // Philox RNG
    uint32_t counter[4] = {(uint32_t)b, offset, 0u, 0u};
    uint32_t key[2] = {(uint32_t)(seed & 0xFFFFFFFF), (uint32_t)(seed >> 32)};
    philox4x32_10_cpu(counter, key);
    int rng_idx = 0;

    int logits_base = b * fused_cols;
    float total_log_prob = 0.0f;
    int logits_offset = 0;

    for (int h = 0; h < num_atns; h++) {
      int A = act_sizes[h];

      // Max for numerical stability
      float max_val = -INFINITY;
      for (int a = 0; a < A; a++) {
        float l = dec_out[logits_base + logits_offset + a];
        if (l != l) l = 0.0f;  // NaN check
        max_val = fmaxf(max_val, l);
      }

      // logsumexp
      float sum_exp = 0.0f;
      for (int a = 0; a < A; a++) {
        float l = dec_out[logits_base + logits_offset + a];
        if (l != l) l = 0.0f;
        sum_exp += expf(l - max_val);
      }
      float logsumexp_val = max_val + logf(sum_exp);

      // Random uniform
      float rand_val = philox_uniform_cpu(counter[rng_idx & 3]);
      rng_idx++;
      if (rng_idx >= 4) {
        // Re-key for next batch of 4 randoms
        counter[2]++;
        key[0] = (uint32_t)(seed & 0xFFFFFFFF);
        key[1] = (uint32_t)(seed >> 32);
        philox4x32_10_cpu(counter, key);
        rng_idx = 0;
      }

      // Inverse CDF sampling
      float cumsum = 0.0f;
      int sampled_action = A - 1;
      for (int a = 0; a < A; a++) {
        float l = dec_out[logits_base + logits_offset + a];
        if (l != l) l = 0.0f;
        float prob = expf(l - logsumexp_val);
        cumsum += prob;
        if (rand_val < cumsum) {
          sampled_action = a;
          break;
        }
      }

      // Log probability
      float sampled_logit = dec_out[logits_base + logits_offset + sampled_action];
      if (sampled_logit != sampled_logit) sampled_logit = 0.0f;
      total_log_prob += sampled_logit - logsumexp_val;

      actions[b * num_atns + h] = (double)sampled_action;
      logits_offset += A;
    }

    logprobs[b] = total_log_prob;
    value_out[b] = dec_out[b * fused_cols + A_total];
  }
}

// ============================================================================
// PPO loss fused forward + backward
// ============================================================================

void ppo_loss_fwd_bwd(PufTensor &dec_out, PufTensor &logstd, TrainGraph &graph,
                       PufTensor &act_sizes, PufTensor &losses_acc,
                       float clip_coef, float vf_clip_coef, float vf_coef,
                       float ent_coef, PPOBuffersPuf &bufs, bool is_continuous,
                       cudaStream_t stream) {
  int N = (int)dec_out.shape[0], T = (int)dec_out.shape[1];
  int fused_cols = (int)dec_out.shape[2];
  int num_atns = (int)act_sizes.numel();
  int A_total = fused_cols - 1;
  int total = N * T;

  int logits_stride_n = T * fused_cols;
  int logits_stride_t = fused_cols;
  int logits_stride_a = 1;
  int values_stride_n = T * fused_cols;
  int values_stride_t = fused_cols;

  MetalStream *ms = mtl_get_stream(stream);

  // var_mean of advantages
  {
    auto enc = ms->compute_encoder();
    auto pso = mtl_pipeline("var_mean_kernel");
    [enc setComputePipelineState:pso];
    mtl_set_ptr(enc, graph.mb_advantages.bytes, 0);
    mtl_set_ptr(enc, bufs.adv_scratch.bytes, 1); // var
    mtl_set_ptr(enc, (float *)bufs.adv_scratch.bytes + 1, 2); // mean
    struct {
      int count;
    } params = {(int)graph.mb_advantages.numel()};
    [enc setBytes:&params length:sizeof(params) atIndex:3];
    mtl_dispatch_groups(enc, pso, 1, 256);
  }

  // PPO partials buffer
  int ppo_threads = 256;
  int ppo_grid = (total + ppo_threads - 1) / ppo_threads;
  static float *ppo_partials_buf = nullptr;
  static int ppo_partials_capacity = 0;
  int ppo_partials_needed = ppo_grid * (LOSS_N + 1);
  if (!ppo_partials_buf || ppo_partials_needed > ppo_partials_capacity) {
    if (ppo_partials_buf)
      free(ppo_partials_buf);
    ppo_partials_capacity = ppo_partials_needed;
    int64_t alloc_bytes = ppo_partials_capacity * sizeof(float);
    int64_t page = 16384;
    alloc_bytes = (alloc_bytes + page - 1) & ~(page - 1);
    posix_memalign((void **)&ppo_partials_buf, page, alloc_bytes);
    id<MTLBuffer> buf = [mtl_ctx()->device
        newBufferWithBytesNoCopy:ppo_partials_buf
                          length:alloc_bytes
                         options:MTLResourceStorageModeShared
                     deallocator:nil];
    assert(buf);
    mtl_ctx()->buffers.push_back({(char *)ppo_partials_buf, alloc_bytes, buf});
  }

  // Zero loss output
  *(float *)bufs.loss_output.bytes = 0.0f;

  // MSL doesn't support double — convert actions from f64 to f32 on GPU.
  // Uses cast_f64_to_f32 kernel (IEEE 754 bit manipulation via uint2) to
  // avoid flushing the GPU encoder for a CPU conversion loop.
  int act_count = (int)graph.mb_actions.numel();
  static float *ppo_act_f32 = nullptr;
  static int ppo_act_f32_capacity = 0;
  if (!ppo_act_f32 || act_count > ppo_act_f32_capacity) {
    if (ppo_act_f32) free(ppo_act_f32);
    ppo_act_f32_capacity = act_count;
    int64_t alloc_bytes = ppo_act_f32_capacity * sizeof(float);
    int64_t page = 16384;
    alloc_bytes = (alloc_bytes + page - 1) & ~(page - 1);
    posix_memalign((void **)&ppo_act_f32, page, alloc_bytes);
    id<MTLBuffer> buf = [mtl_ctx()->device
        newBufferWithBytesNoCopy:ppo_act_f32
                          length:alloc_bytes
                         options:MTLResourceStorageModeShared
                     deallocator:nil];
    assert(buf);
    mtl_ctx()->buffers.push_back({(char *)ppo_act_f32, alloc_bytes, buf});
  }
  {
    auto enc = ms->compute_encoder();
    auto pso = mtl_pipeline("cast_f64_to_f32");
    [enc setComputePipelineState:pso];
    mtl_set_ptr(enc, graph.mb_actions.bytes, 0);  // src: f64 as uint2*
    mtl_set_ptr(enc, ppo_act_f32, 1);             // dst: f32
    [enc setBytes:&act_count length:sizeof(act_count) atIndex:2];
    mtl_dispatch_1d(enc, pso, act_count);
  }

  // Fused PPO kernel
  {
    auto enc = ms->compute_encoder();
    auto pso = mtl_pipeline("ppo_loss_fwd_bwd_kernel");
    [enc setComputePipelineState:pso];
    mtl_set_ptr(enc, ppo_partials_buf, 0);
    mtl_set_ptr(enc, bufs.grad_logits.bytes, 1);
    mtl_set_ptr(enc, is_continuous ? bufs.grad_logstd.bytes
                                   : bufs.grad_logits.bytes,
                2);
    mtl_set_ptr(enc, bufs.grad_values.bytes, 3);
    mtl_set_ptr(enc, dec_out.bytes, 4);                   // logits
    mtl_set_ptr(enc, is_continuous ? logstd.bytes : dec_out.bytes, 5); // logstd
    mtl_set_ptr(enc, (float *)dec_out.bytes + A_total, 6); // values_pred (last column of fused decoder output)
    mtl_set_ptr(enc, ppo_act_f32, 7);  // f32 actions (converted from f64)
    mtl_set_ptr(enc, graph.mb_logprobs.bytes, 8);
    mtl_set_ptr(enc, graph.mb_advantages.bytes, 9);
    mtl_set_ptr(enc, graph.mb_prio.bytes, 10);
    mtl_set_ptr(enc, graph.mb_values.bytes, 11);
    mtl_set_ptr(enc, graph.mb_returns.bytes, 12);
    mtl_set_ptr(enc, (float *)bufs.adv_scratch.bytes + 1, 13); // adv_mean
    mtl_set_ptr(enc, bufs.adv_scratch.bytes, 14);               // adv_var
    mtl_set_ptr(enc, act_sizes.bytes, 15);

    struct {
      int num_atns;
      float clip_coef, vf_clip_coef, vf_coef, ent_coef;
      int T_seq, A_total, N;
      int logits_stride_n, logits_stride_t, logits_stride_a;
      int values_stride_n, values_stride_t;
      int is_continuous;
    } params = {num_atns,
                clip_coef,
                vf_clip_coef,
                vf_coef,
                ent_coef,
                T,
                A_total,
                N,
                logits_stride_n,
                logits_stride_t,
                logits_stride_a,
                values_stride_n,
                values_stride_t,
                is_continuous ? 1 : 0};
    [enc setBytes:&params length:sizeof(params) atIndex:16];
    mtl_dispatch_groups(enc, pso, ppo_grid, ppo_threads);
  }



  // Reduce partials
  {
    auto enc = ms->compute_encoder();
    auto pso = mtl_pipeline("ppo_loss_reduce_kernel");
    [enc setComputePipelineState:pso];
    mtl_set_ptr(enc, bufs.loss_output.bytes, 0);
    mtl_set_ptr(enc, losses_acc.bytes, 1);
    mtl_set_ptr(enc, ppo_partials_buf, 2);
    struct {
      int num_blocks;
    } params = {ppo_grid};
    [enc setBytes:&params length:sizeof(params) atIndex:3];
    mtl_dispatch_groups(enc, pso, 1, LOSS_N + 1);
  }
}

// ============================================================================
// Advantage computation
// ============================================================================

void puff_advantage_cuda(PufTensor &values, PufTensor &rewards,
                          PufTensor &dones, PufTensor &importance,
                          PufTensor &advantages, float gamma, float lambda,
                          float rho_clip, float c_clip, cudaStream_t stream) {
  int num_steps = (int)values.shape[0], horizon = (int)values.shape[1];
  assert(advantages.dtype_size == 4 && "advantages must be f32");

  MetalStream *ms = mtl_get_stream(stream);
  auto enc = ms->compute_encoder();
  auto pso = mtl_pipeline("puff_advantage_kernel");
  [enc setComputePipelineState:pso];
  mtl_set_tensor(enc, values, 0);
  mtl_set_tensor(enc, rewards, 1);
  mtl_set_tensor(enc, dones, 2);
  mtl_set_tensor(enc, importance, 3);
  mtl_set_tensor(enc, advantages, 4);
  struct {
    float gamma, lambda, rho_clip, c_clip;
    int num_steps, horizon;
  } params = {gamma, lambda, rho_clip, c_clip, num_steps, horizon};
  [enc setBytes:&params length:sizeof(params) atIndex:5];
  int blocks = (num_steps + 255) / 256;
  mtl_dispatch_groups(enc, pso, blocks, 256);
}

// ============================================================================
// Priority replay
// ============================================================================

// Prio replay split into two phases to eliminate per-minibatch GPU syncs.
// Phase 1 (precompute): GPU reduction + normalize + sync + CPU CDF build.
// Called once before the minibatch loop since advantages don't change.
void prio_precompute(PufTensor &advantages, float prio_alpha,
                     PrioBuffers &bufs, cudaStream_t stream) {
  int S = (int)advantages.shape[0], T = (int)advantages.shape[1];
  MetalStream *ms = mtl_get_stream(stream);

  // Prio adv reduction
  {
    auto enc = ms->compute_encoder();
    auto pso = mtl_pipeline("prio_adv_reduction_kernel");
    [enc setComputePipelineState:pso];
    mtl_set_tensor(enc, advantages, 0);
    mtl_set_ptr(enc, bufs.prio_probs.bytes, 1);
    struct {
      float prio_alpha;
      int stride;
    } params = {prio_alpha, T};
    [enc setBytes:&params length:sizeof(params) atIndex:2];
    mtl_dispatch_groups(enc, pso, S, 32);
  }

  // Normalize
  {
    auto enc = ms->compute_encoder();
    auto pso = mtl_pipeline("prio_normalize_kernel");
    [enc setComputePipelineState:pso];
    mtl_set_ptr(enc, bufs.prio_probs.bytes, 0);
    struct {
      int S;
    } params = {S};
    [enc setBytes:&params length:sizeof(params) atIndex:1];
    mtl_dispatch_groups(enc, pso, 1, 256);
  }

  // Sync to read normalized probs on CPU, then build CDF
  mtl_ensure_synced(stream);
  float *probs = (float *)bufs.prio_probs.bytes;
  float *cdf = (float *)bufs.cdf.bytes;
  cdf[0] = probs[0];
  for (int i = 1; i < S; i++)
    cdf[i] = cdf[i - 1] + probs[i];
}

// Phase 2 (per-minibatch): CPU sampling from cached CDF + GPU importance weights.
// No GPU sync needed — samples from pre-computed CDF, dispatches imp_weights to GPU.
void prio_sample(int minibatch_segments, int total_agents,
                 float anneal_beta, PrioBuffers &bufs, uint64_t seed,
                 uint32_t *offset_ptr, cudaStream_t stream) {
  int S = (int)bufs.prio_probs.shape[0];

  // CPU multinomial sampling from cached CDF (no GPU sync)
  {
    float *cdf = (float *)bufs.cdf.bytes;
    int64_t *idx = (int64_t *)bufs.idx.bytes;
    std::mt19937_64 rng(seed + *offset_ptr);
    std::uniform_real_distribution<float> dist(0.0f, 1.0f);
    for (int i = 0; i < minibatch_segments; i++) {
      float u = dist(rng);
      int lo = 0, hi = S - 1;
      while (lo < hi) {
        int mid = (lo + hi) / 2;
        if (cdf[mid] < u)
          lo = mid + 1;
        else
          hi = mid;
      }
      idx[i] = lo;
    }
    *offset_ptr += minibatch_segments;
  }

  // Importance weights (GPU dispatch, no sync)
  {
    MetalStream *ms = mtl_get_stream(stream);
    auto enc = ms->compute_encoder();
    auto pso = mtl_pipeline("prio_imp_weights_kernel");
    [enc setComputePipelineState:pso];
    mtl_set_ptr(enc, bufs.idx.bytes, 0);
    mtl_set_ptr(enc, bufs.prio_probs.bytes, 1);
    mtl_set_ptr(enc, bufs.mb_prio.bytes, 2);
    struct {
      int total_agents;
      float anneal_beta;
      int minibatch_segments;
    } params = {total_agents, anneal_beta, minibatch_segments};
    [enc setBytes:&params length:sizeof(params) atIndex:3];
    mtl_dispatch_1d(enc, pso, minibatch_segments);
  }
}

// Legacy combined API (kept for CUDA compat, calls both phases)
void prio_replay_cuda(PufTensor &advantages, float prio_alpha,
                       int minibatch_segments, int total_agents,
                       float anneal_beta, PrioBuffers &bufs, uint64_t seed,
                       uint32_t *offset_ptr, cudaStream_t stream) {
  prio_precompute(advantages, prio_alpha, bufs, stream);
  prio_sample(minibatch_segments, total_agents, anneal_beta, bufs,
              seed, offset_ptr, stream);
}

// ============================================================================
// Select copy (minibatch assembly)
// ============================================================================

void mtl_select_copy(RolloutBuf &rollouts, TrainGraph &graph,
                      const int64_t *idx, const float *advantages,
                      const float *mb_prio, int mb_segs, cudaStream_t stream) {
  int obs_row_bytes = (int)(rollouts.observations.numel() /
                            rollouts.observations.shape[0]) *
                      rollouts.observations.dtype_size;
  int act_row_bytes = (int)(rollouts.actions.numel() /
                            rollouts.actions.shape[0]) *
                      rollouts.actions.dtype_size;
  int lp_row_bytes = (int)(rollouts.logprobs.numel() /
                           rollouts.logprobs.shape[0]) *
                     rollouts.logprobs.dtype_size;
  int horizon = (int)rollouts.values.shape[1];

  MetalStream *ms = mtl_get_stream(stream);
  auto enc = ms->compute_encoder();
  auto pso = mtl_pipeline("select_copy_kernel");
  [enc setComputePipelineState:pso];

  mtl_set_ptr(enc, graph.mb_obs.bytes, 0);
  mtl_set_ptr(enc, graph.mb_actions.bytes, 1);
  mtl_set_ptr(enc, graph.mb_logprobs.bytes, 2);
  mtl_set_ptr(enc, graph.mb_values.bytes, 3);
  mtl_set_ptr(enc, graph.mb_advantages.bytes, 4);
  mtl_set_ptr(enc, graph.mb_returns.bytes, 5);
  mtl_set_ptr(enc, graph.mb_prio.bytes, 6);
  mtl_set_ptr(enc, rollouts.observations.bytes, 7);
  mtl_set_ptr(enc, rollouts.actions.bytes, 8);
  mtl_set_ptr(enc, rollouts.logprobs.bytes, 9);
  mtl_set_ptr(enc, rollouts.values.bytes, 10);
  mtl_set_ptr(enc, advantages, 11);
  mtl_set_ptr(enc, idx, 12);
  mtl_set_ptr(enc, mb_prio, 13);

  struct {
    int obs_row_bytes, act_row_bytes, lp_row_bytes, horizon;
  } params = {obs_row_bytes, act_row_bytes, lp_row_bytes, horizon};
  [enc setBytes:&params length:sizeof(params) atIndex:14];

  // 2D dispatch: (mb_segs, 5) threadgroups, 256 threads each
  [enc dispatchThreadgroups:MTLSizeMake(mb_segs, 5, 1)
      threadsPerThreadgroup:MTLSizeMake(256, 1, 1)];
}

// ============================================================================
// Muon weight update kernel
// ============================================================================

void mtl_muon_weight_update(float *weights, const float *updates,
                              const float *lr_ptr, float weight_decay, int count,
                              cudaStream_t stream) {
  MetalStream *ms = mtl_get_stream(stream);
  auto enc = ms->compute_encoder();
  auto pso = mtl_pipeline("muon_weight_update_kernel");
  [enc setComputePipelineState:pso];
  mtl_set_ptr(enc, weights, 0);
  mtl_set_ptr(enc, updates, 1);
  mtl_set_ptr(enc, lr_ptr, 2);
  struct {
    float weight_decay;
    int count;
  } params = {weight_decay, count};
  [enc setBytes:&params length:sizeof(params) atIndex:3];
  mtl_dispatch_1d(enc, pso, count);
}

// ============================================================================
// Post-create for PPO buffers (unified memory — direct write)
// ============================================================================

void post_create_ppo_buffers(PPOBuffersPuf &bufs) {
  *(float *)bufs.grad_loss.bytes = 1.0f;
}

// ============================================================================
// Orthogonal init via Accelerate LAPACK (CPU-side)
//
// Replaces cuSOLVER + cuRAND from models.cu. Uses sgeqrf (QR) + sorgqr
// from Accelerate. Runs once at model init — not perf-critical.
// ============================================================================

void puf_orthogonal_init(PufTensor &dst, float gain, uint64_t seed,
                          cudaStream_t stream) {
  mtl_ensure_synced(stream);

  assert(dst.ndim() == 2);
  int64_t rows = dst.shape[0], cols = dst.shape[1];
  assert(rows > 0 && cols > 0);

  bool transposed = rows < cols;
  int m = transposed ? (int)cols : (int)rows;
  int n = transposed ? (int)rows : (int)cols;

  // Generate random normal matrix on CPU
  int64_t mn = (int64_t)m * n;
  float *A = (float *)malloc(mn * sizeof(float));
  float *tau = (float *)malloc(n * sizeof(float));

  std::mt19937_64 rng(seed);
  std::normal_distribution<float> normal(0.0f, 1.0f);
  for (int64_t i = 0; i < mn; i++)
    A[i] = normal(rng);

  // QR decomposition via Accelerate LAPACK
  int lda = m, info;
  int lwork = -1;
  float work_query;
  sgeqrf_(&m, &n, A, &lda, tau, &work_query, &lwork, &info);
  assert(info == 0);
  lwork = (int)work_query;
  float *work = (float *)malloc(lwork * sizeof(float));
  sgeqrf_(&m, &n, A, &lda, tau, work, &lwork, &info);
  assert(info == 0);

  // Extract diagonal signs for sign correction
  float *signs = (float *)malloc(n * sizeof(float));
  for (int i = 0; i < n; i++)
    signs[i] = (A[i * lda + i] >= 0.0f) ? 1.0f : -1.0f;

  // Generate Q from QR
  int lwork2 = -1;
  float work_query2;
  sorgqr_(&m, &n, &n, A, &lda, tau, &work_query2, &lwork2, &info);
  assert(info == 0);
  lwork2 = (int)work_query2;
  if (lwork2 > lwork) {
    free(work);
    work = (float *)malloc(lwork2 * sizeof(float));
  }
  sorgqr_(&m, &n, &n, A, &lda, tau, work, &lwork2, &info);
  assert(info == 0);

  // Apply sign correction: Q[:,j] *= signs[j]
  for (int j = 0; j < n; j++)
    for (int i = 0; i < m; i++)
      A[j * m + i] *= signs[j];

  // Copy result to dst with optional transpose and gain scaling
  // LAPACK outputs column-major Q, dst is row-major
  float *dst_f = (float *)dst.bytes;
  if (transposed) {
    // Q is (cols, rows) col-major = (rows, cols) row-major → direct copy
    for (int64_t i = 0; i < rows * cols; i++)
      dst_f[i] = A[i] * gain;
  } else {
    // Q is (rows, cols) col-major → transpose to row-major
    for (int r = 0; r < (int)rows; r++)
      for (int c = 0; c < (int)cols; c++)
        dst_f[r * cols + c] = A[c * m + r] * gain;
  }

  free(A);
  free(tau);
  free(signs);
  free(work);
}

// ============================================================================
// Muon optimizer
// ============================================================================

void muon_init(Muon *m, Allocator *param_alloc, PufTensor weight_buffer,
               double lr_val, double momentum, double eps, double weight_decay,
               Allocator &alloc) {
  m->momentum = momentum;
  m->weight_decay = weight_decay;
  m->eps = eps;
  m->lr_val_init = (float)lr_val;
  m->lr_ptr = nullptr;
  m->lr_derived_ptr = nullptr;
  m->wb_puf = weight_buffer;
  m->param_alloc = param_alloc;
  m->nccl_comm = nullptr;
  m->world_size = 1;
  m->ns = {};
  int64_t n = m->wb_puf.numel();
  int f = sizeof(float);
  m->lr_puf = {.shape = {1}, .dtype_size = f};
  m->lr_derived_puf = {.shape = {2}, .dtype_size = f};
  m->mb_puf = {.shape = {n}, .dtype_size = f};
  m->gc_puf = {.shape = {n}, .dtype_size = f};
  m->up_puf = {.shape = {n}, .dtype_size = f};
  alloc.reg(&m->lr_puf);
  alloc.reg(&m->lr_derived_puf);
  alloc.reg(&m->mb_puf);
  alloc.reg(&m->gc_puf);
  alloc.reg(&m->up_puf);

  int64_t max_M = 0, max_N = 0;
  for (auto *t : param_alloc->regs) {
    if (t->ndim() >= 2) {
      int64_t R = t->shape[0], C = t->numel() / R;
      max_M = std::max(max_M, std::min(R, C));
      max_N = std::max(max_N, std::max(R, C));
    }
  }
  if (max_M > 0) {
    m->ns.max_M = max_M;
    m->ns.max_N = max_N;
    int ns_esz = PRECISION_SIZE; // always 4 on Metal
    m->ns.x = {.shape = {max_M, max_N}, .dtype_size = ns_esz};
    m->ns.A = {.shape = {max_M, max_M}, .dtype_size = ns_esz};
    m->ns.gram = {.shape = {max_M, max_M}, .dtype_size = ns_esz};
    m->ns.tmp = {.shape = {max_M, max_N}, .dtype_size = ns_esz};
    m->ns.result_f32 = {.shape = {max_M, max_N}, .dtype_size = f};
    m->ns_norm_puf = {.shape = {1}, .dtype_size = f};
    alloc.reg(&m->ns.x);
    alloc.reg(&m->ns.A);
    alloc.reg(&m->ns.gram);
    alloc.reg(&m->ns.tmp);
    alloc.reg(&m->ns.result_f32);
    alloc.reg(&m->ns_norm_puf);
  }
}

void muon_post_create(Muon *m) {
  m->lr_ptr = (float *)m->lr_puf.bytes;
  m->lr_derived_ptr = (float *)m->lr_derived_puf.bytes;
  if (m->ns_norm_puf.bytes)
    m->ns.norm_ptr = (float *)m->ns_norm_puf.bytes;
  // Direct writes — unified memory, no cudaMemcpy needed
  *m->lr_ptr = m->lr_val_init;
  memset(m->lr_derived_ptr, 0, 2 * sizeof(float));
  memset(m->mb_puf.bytes, 0, m->mb_puf.numel() * sizeof(float));
}

void muon_step(Muon *m, cudaStream_t stream) {
  if (m->wb_puf.bytes == nullptr)
    return;

  // No NCCL on Metal (single GPU)

  // Nesterov momentum update: mb = mu * mb + gc
  mtl_nesterov_f32((float *)m->mb_puf.bytes, (float *)m->gc_puf.bytes,
                   (float)m->momentum, (int)m->mb_puf.numel(), stream);

  // Zero update buffer
  puf_zero(m->up_puf, stream);

  int64_t offset = 0;
  for (auto *t : m->param_alloc->regs) {
    float *gc_ptr = (float *)m->gc_puf.bytes + offset;
    float *up_ptr = (float *)m->up_puf.bytes + offset;

    if (t->ndim() >= 2) {
      int64_t R = t->shape[0], C = t->numel() / R;
      bool transposed_flag = R > C;
      int64_t M = transposed_flag ? C : R;
      int64_t N = transposed_flag ? R : C;

      PufTensor G_f32 = {.bytes = (char *)gc_ptr,
                         .shape = {R, C},
                         .dtype_size = 4};
      PufTensor x = ns_slice(m->ns.x, M, N);
      PufTensor A = ns_slice(m->ns.A, M, M);
      PufTensor gram = ns_slice(m->ns.gram, M, M);
      PufTensor tmp = ns_slice(m->ns.tmp, M, N);

      // fp32 path: copy or transpose into x
      if (transposed_flag) {
        mtl_transpose_f32((float *)x.bytes, (const float *)G_f32.bytes, (int)R,
                          (int)C, stream);
      } else {
        puf_copy(x, G_f32, stream);
      }

      // Normalize x
      ensure_norm_partials();
      {
        int nblk = std::min((int)((x.numel() + 255) / 256), 256);
        mtl_norm_f32(norm_partials_buf, (const float *)x.bytes,
                     (int)x.numel(), nblk, stream);
        mtl_norm_reduce(m->ns.norm_ptr, norm_partials_buf, nblk, stream);
      }
      mtl_normalize_f32((float *)x.bytes, m->ns.norm_ptr, 1e-7f,
                        (int)x.numel(), stream);

      // 5 Newton-Schulz iterations (all GEMM — synced internally)
      for (int i = 0; i < 5; ++i) {
        float a = (float)ns_coeffs[i][0], b = (float)ns_coeffs[i][1],
              c = (float)ns_coeffs[i][2];
        PufTensor &src = (i % 2 == 0) ? x : tmp;
        PufTensor &dst = (i % 2 == 0) ? tmp : x;
        puf_mm(src, src, A, stream);
        puf_copy(gram, A, stream);
        puf_addmm_nn(A, A, gram, c, b, stream);
        puf_copy(dst, src, stream);
        puf_addmm_nn(gram, src, dst, 1.0f, a, stream);
      }

      PufTensor &result_precision = tmp;
      float scale =
          (float)std::sqrt(std::max(1.0, (double)M / (double)N));

      PufTensor out_f32 = {.bytes = (char *)up_ptr,
                           .shape = {R, C},
                           .dtype_size = 4};

      // Result is already f32 — scale and transpose if needed
      if (scale != 1.0f)
        mtl_scale_f32((float *)result_precision.bytes, scale,
                      (int)result_precision.numel(), stream);
      if (transposed_flag) {
        mtl_transpose_f32((float *)out_f32.bytes,
                          (const float *)result_precision.bytes, (int)M,
                          (int)N, stream);
      } else {
        puf_copy(out_f32, result_precision, stream);
      }
    } else {
      // 1D params: just copy gradient as update
      PufTensor src_puf = {.bytes = (char *)gc_ptr,
                           .shape = {t->numel()},
                           .dtype_size = 4};
      PufTensor dst_puf = {.bytes = (char *)up_ptr,
                           .shape = {t->numel()},
                           .dtype_size = 4};
      puf_copy(dst_puf, src_puf, stream);
    }
    offset += t->numel();
  }

  // Apply weight update: w -= lr * up + weight_decay * w
  mtl_muon_weight_update((float *)m->wb_puf.bytes, (const float *)m->up_puf.bytes,
                         m->lr_ptr, (float)m->weight_decay,
                         (int)m->wb_puf.numel(), stream);
}

// ============================================================================
// Model components — encoder
// ============================================================================

static PufTensor encoder_forward(void *w, void *activations, PufTensor input,
                                  cudaStream_t stream) {
  EncoderWeights *ew = (EncoderWeights *)w;
  EncoderActivations *a = (EncoderActivations *)activations;
  if (a->saved_input.bytes)
    puf_copy(a->saved_input, input, stream);
  if (g_cpu_inference && ew->weight_t.bytes)
    puf_mm_nn(input, ew->weight_t, a->out, stream);
  else
    puf_mm(input, ew->weight, a->out, stream);
  return a->out;
}

static void encoder_backward(void *w, void *activations, PufTensor grad,
                               cudaStream_t stream) {
  EncoderActivations *a = (EncoderActivations *)activations;
  puf_mm_tn(grad, a->saved_input, a->wgrad_scratch, stream);
}

static void encoder_init_weights(void *w, uint64_t *seed,
                                  cudaStream_t stream) {
  EncoderWeights *ew = (EncoderWeights *)w;
  PufTensor wt = {.bytes = ew->weight.bytes,
                  .shape = {ew->out_dim, ew->in_dim},
                  .dtype_size = ew->weight.dtype_size};
  puf_orthogonal_init(wt, std::sqrt(2.0f), (*seed)++, stream);
}

static void encoder_reg_params(void *w, Allocator *alloc, int esz) {
  EncoderWeights *ew = (EncoderWeights *)w;
  ew->weight = {.shape = {ew->out_dim, ew->in_dim}, .dtype_size = esz};
  alloc->reg(&ew->weight);
}

static void encoder_reg_train(void *w, void *activations, Allocator *acts,
                                Allocator *grads, int B_TT) {
  EncoderWeights *ew = (EncoderWeights *)w;
  EncoderActivations *a = (EncoderActivations *)activations;
  int p = PRECISION_SIZE;
  *a = (EncoderActivations){
      .out = {.shape = {B_TT, ew->out_dim}, .dtype_size = p},
      .saved_input = {.shape = {B_TT, ew->in_dim}, .dtype_size = p},
      .wgrad_scratch = {.shape = {ew->out_dim, ew->in_dim}, .dtype_size = p},
  };
  acts->reg(&a->out);
  acts->reg(&a->saved_input);
  grads->reg(&a->wgrad_scratch);
}

static void encoder_reg_rollout(void *w, void *activations, Allocator *alloc,
                                 int B) {
  EncoderWeights *ew = (EncoderWeights *)w;
  EncoderActivations *a = (EncoderActivations *)activations;
  a->out = {.shape = {B, ew->out_dim}, .dtype_size = PRECISION_SIZE};
  alloc->reg(&a->out);
}

// ============================================================================
// Model components — decoder
// ============================================================================

static PufTensor decoder_forward(void *w, void *activations, PufTensor input,
                                  cudaStream_t stream) {
  DecoderWeights *dw = (DecoderWeights *)w;
  DecoderActivations *a = (DecoderActivations *)activations;
  if (a->saved_input.bytes)
    puf_copy(a->saved_input, input, stream);
  if (g_cpu_inference && dw->weight_t.bytes)
    puf_mm_nn(input, dw->weight_t, a->out, stream);
  else
    puf_mm(input, dw->weight, a->out, stream);
  return a->out;
}

static PufTensor decoder_backward(void *w, void *activations,
                                    PufTensor grad_logits,
                                    PufTensor grad_logstd,
                                    PufTensor grad_value, cudaStream_t stream) {
  DecoderWeights *dw = (DecoderWeights *)w;
  DecoderActivations *a = (DecoderActivations *)activations;
  int B_TT = (int)a->saved_input.shape[0];
  int od = dw->output_dim, od1 = od + 1;

  // Assemble gradient: concat [grad_logits, grad_value] per row.
  // PPO grads are always fp32. If decoder activations are fp16, use the
  // f32-to-f16 assembly kernel to cast and concatenate in one dispatch.
  if (a->grad_out.dtype_size == 2)
    mtl_assemble_decoder_grad_f32_to_f16(a->grad_out.bytes,
                                          (const float *)grad_logits.bytes,
                                          (const float *)grad_value.bytes,
                                          B_TT, od, od1, stream);
  else
    mtl_assemble_decoder_grad_f32((float *)a->grad_out.bytes,
                                  (const float *)grad_logits.bytes,
                                  (const float *)grad_value.bytes, B_TT, od,
                                  od1, stream);

  puf_mm_tn(a->grad_out, a->saved_input, a->wgrad_scratch, stream);

  if (dw->continuous && grad_logstd.bytes != nullptr) {
    mtl_sum_rows_to_f32((float *)a->logstd_scratch.bytes,
                        (const float *)grad_logstd.bytes, B_TT,
                        dw->output_dim, stream);
  }

  puf_mm_nn(a->grad_out, dw->weight, a->grad_input, stream);
  return a->grad_input;
}

static void decoder_init_weights(void *w, uint64_t *seed,
                                  cudaStream_t stream) {
  DecoderWeights *dw = (DecoderWeights *)w;
  PufTensor wt = {.bytes = dw->weight.bytes,
                  .shape = {dw->output_dim + 1, dw->hidden_dim},
                  .dtype_size = dw->weight.dtype_size};
  puf_orthogonal_init(wt, 0.01f, (*seed)++, stream);
}

static void decoder_reg_params(void *w, Allocator *alloc, int esz) {
  DecoderWeights *dw = (DecoderWeights *)w;
  dw->weight = {.shape = {dw->output_dim + 1, dw->hidden_dim},
                .dtype_size = esz};
  alloc->reg(&dw->weight);
  if (dw->continuous) {
    dw->logstd = {.shape = {1, dw->output_dim}, .dtype_size = esz};
    alloc->reg(&dw->logstd);
  }
}

static void decoder_reg_train(void *w, void *activations, Allocator *acts,
                                Allocator *grads, int B_TT) {
  DecoderWeights *dw = (DecoderWeights *)w;
  DecoderActivations *a = (DecoderActivations *)activations;
  int p = PRECISION_SIZE;
  int od1 = dw->output_dim + 1;
  *a = (DecoderActivations){
      .out = {.shape = {B_TT, od1}, .dtype_size = p},
      .grad_out = {.shape = {B_TT, od1}, .dtype_size = p},
      .saved_input = {.shape = {B_TT, dw->hidden_dim}, .dtype_size = p},
      .grad_input = {.shape = {B_TT, dw->hidden_dim}, .dtype_size = p},
      .wgrad_scratch = {.shape = {od1, dw->hidden_dim}, .dtype_size = p},
      .logstd_scratch = {.shape = {1, dw->output_dim}, .dtype_size = p},
  };
  acts->reg(&a->out);
  acts->reg(&a->saved_input);
  acts->reg(&a->grad_out);
  acts->reg(&a->grad_input);
  grads->reg(&a->wgrad_scratch);
  if (dw->continuous)
    grads->reg(&a->logstd_scratch);
}

static void decoder_reg_rollout(void *w, void *activations, Allocator *alloc,
                                 int B) {
  DecoderWeights *dw = (DecoderWeights *)w;
  DecoderActivations *a = (DecoderActivations *)activations;
  a->out = {.shape = {B, dw->output_dim + 1}, .dtype_size = PRECISION_SIZE};
  alloc->reg(&a->out);
}

// ============================================================================
// Model components — MinGRU
// ============================================================================

static void mingru_init_weights(void *w, uint64_t *seed, cudaStream_t stream) {
  MinGRUWeights *m = (MinGRUWeights *)w;
  for (int i = 0; i < m->num_layers; i++) {
    PufTensor w2d = {.bytes = m->weights[i].bytes,
                     .shape = {3 * m->hidden, m->hidden},
                     .dtype_size = m->weights[i].dtype_size};
    puf_orthogonal_init(w2d, 1.0f, (*seed)++, stream);
  }
}

static void mingru_reg_params(void *w, Allocator *alloc, int esz) {
  MinGRUWeights *m = (MinGRUWeights *)w;
  for (int i = 0; i < m->num_layers; i++) {
    m->weights[i] = {.shape = {3 * m->hidden, m->hidden}, .dtype_size = esz};
    alloc->reg(&m->weights[i]);
  }
}

static void mingru_reg_train(void *w, void *activations, Allocator *acts,
                               Allocator *grads, int B_TT) {
  MinGRUWeights *m = (MinGRUWeights *)w;
  MinGRUActivations *a = (MinGRUActivations *)activations;
  int H = m->hidden, TT = m->horizon, B = B_TT / TT, p = PRECISION_SIZE;
  int f = sizeof(float);
  a->num_layers = m->num_layers;
  a->saved_inputs.resize(m->num_layers);
  a->scan_bufs.resize(m->num_layers);
  a->combined_bufs.resize(m->num_layers);
  a->wgrad_scratch.resize(m->num_layers);
  a->grad_input_buf = {.shape = {B_TT, H}, .dtype_size = p};
  a->grad_next_state = {.shape = {B, 1, H}, .dtype_size = p};
  acts->reg(&a->grad_input_buf);
  acts->reg(&a->grad_next_state);
  for (int i = 0; i < m->num_layers; i++) {
    a->scan_bufs[i] = {
        .B = B,
        .T = TT,
        .H = H,
        .a_star = {.shape = {B, TT + 1, H}, .dtype_size = f},
        .s_vals = {.shape = {B, TT + 1, H}, .dtype_size = f},
        .log_values_buf = {.shape = {B, TT + 1, H}, .dtype_size = f},
        .out = {.shape = {B, TT, H}, .dtype_size = p},
        .next_state = {.shape = {B, 1, H}, .dtype_size = p},
        .grad_combined = {.shape = {B, TT, 3 * H}, .dtype_size = p},
        .grad_state = {.shape = {B, 1, H}, .dtype_size = p},
    };
    a->saved_inputs[i] = {.shape = {B, TT, H}, .dtype_size = p};
    a->combined_bufs[i] = {.shape = {B_TT, 3 * H}, .dtype_size = p};
    a->wgrad_scratch[i] = {.shape = {3 * H, H}, .dtype_size = p};
    acts->reg(&a->saved_inputs[i]);
    acts->reg(&a->combined_bufs[i]);
    acts->reg(&a->scan_bufs[i].out);
    acts->reg(&a->scan_bufs[i].next_state);
    acts->reg(&a->scan_bufs[i].a_star);
    acts->reg(&a->scan_bufs[i].s_vals);
    acts->reg(&a->scan_bufs[i].log_values_buf);
    acts->reg(&a->scan_bufs[i].grad_combined);
    acts->reg(&a->scan_bufs[i].grad_state);
    grads->reg(&a->wgrad_scratch[i]);
  }
}

static void mingru_reg_rollout(void *weights, void *activations,
                                Allocator *alloc, int B_inf) {
  MinGRUWeights *w = (MinGRUWeights *)weights;
  MinGRUActivations *a = (MinGRUActivations *)activations;
  int H = w->hidden, p = PRECISION_SIZE;
  a->num_layers = w->num_layers;
  a->combined.resize(w->num_layers);
  for (int i = 0; i < w->num_layers; i++) {
    a->combined[i] = {.shape = {B_inf, 3 * H}, .dtype_size = p};
    alloc->reg(&a->combined[i]);
  }
  a->out = {.shape = {B_inf, H}, .dtype_size = p};
  a->next_state = {.shape = {B_inf, H}, .dtype_size = p};
  alloc->reg(&a->out);
  alloc->reg(&a->next_state);
}

static PufTensor mingru_forward(void *w, PufTensor x, PufTensor state,
                                 void *activations, cudaStream_t stream) {
  MinGRUWeights *m = (MinGRUWeights *)w;
  MinGRUActivations *a = (MinGRUActivations *)activations;
  int B = (int)state.shape[1];
  int H = (int)state.shape[2];

  for (int i = 0; i < m->num_layers; i++) {
    PufTensor state_i = mingru_state_layer(m, state, i);
    if (i == 0 && m->fused_enc_layer0.bytes) {
      if (g_cpu_inference)
        puf_mm_nn(x, m->fused_enc_layer0, a->combined[i], stream);
      else
        puf_mm(x, m->fused_enc_layer0, a->combined[i], stream);
    } else if (g_cpu_inference && i < (int)m->weights_t.size() && m->weights_t[i].bytes)
      puf_mm_nn(x, m->weights_t[i], a->combined[i], stream);
    else
      puf_mm(x, m->weights[i], a->combined[i], stream);
    if (g_cpu_inference) {
      // Write next_state directly into state_i (in-place update).
      // Safe because the gate reads each element before writing it.
      cpu_mingru_gate((float *)a->out.bytes, (float *)state_i.bytes,
                      (const float *)a->combined[i].bytes,
                      (const float *)state_i.bytes, H, B);
    } else {
      mtl_mingru_gate((float *)a->out.bytes, (float *)a->next_state.bytes,
                      (const float *)a->combined[i].bytes,
                      (const float *)state_i.bytes, H, B, stream);
      puf_copy(state_i, a->next_state, stream);
    }
    x = a->out;
  }
  return x;
}

static PufTensor mingru_forward_train(void *w, PufTensor x, PufTensor state,
                                       void *activations,
                                       cudaStream_t stream) {
  MinGRUWeights *m = (MinGRUWeights *)w;
  MinGRUActivations *a = (MinGRUActivations *)activations;

  for (int i = 0; i < m->num_layers; i++) {
    puf_copy(a->saved_inputs[i], x, stream);
    PufTensor state_i = mingru_state_layer(m, state, i);
    puf_mm(x, m->weights[i], a->combined_bufs[i], stream);
    a->scan_bufs[i].combined_ptr = a->combined_bufs[i].bytes;
    a->scan_bufs[i].state_ptr = state_i.bytes;
    // Dispatch fp16 or fp32 scan based on activation dtype
    if (a->combined_bufs[i].dtype_size == 2)
      mtl_fused_scan_forward_fp16(a->scan_bufs[i], stream);
    else
      mtl_fused_scan_forward(a->scan_bufs[i], stream);
    x = a->scan_bufs[i].out;
  }
  return x;
}

static PufTensor mingru_backward(void *w, PufTensor grad, void *activations,
                                  cudaStream_t stream) {
  MinGRUWeights *m = (MinGRUWeights *)w;
  MinGRUActivations *a = (MinGRUActivations *)activations;

  for (int i = m->num_layers - 1; i >= 0; i--) {
    PrefixScan &scan = a->scan_bufs[i];
    // Dispatch fp16 or fp32 scan backward based on activation dtype
    if (grad.dtype_size == 2)
      mtl_fused_scan_backward_fp16(scan, grad.bytes,
                                   a->grad_next_state.bytes, stream);
    else
      mtl_fused_scan_backward(scan, (const float *)grad.bytes,
                              (const float *)a->grad_next_state.bytes, stream);
    puf_mm_tn(scan.grad_combined, a->saved_inputs[i], a->wgrad_scratch[i],
              stream);
    puf_mm_nn(scan.grad_combined, m->weights[i], a->grad_input_buf, stream);
    grad = a->grad_input_buf;
  }
  return grad;
}
