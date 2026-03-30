/**
 * @fileoverview Metal kernel dispatch layer, model components, orthogonal
 * init (Accelerate LAPACK), and Muon optimizer for PufferLib static-native.
 *
 * REQUIRES Metal 4 (macOS 15+, Apple Silicon M3+). All GPU dispatch uses
 * MTL4ComputeCommandEncoder + MTL4ArgumentTable (no Metal 3 fallback).
 *
 * Provides all PufTensor operation wrappers that models.cu provides on CUDA:
 *   - Memory ops: puf_copy, puf_zero, puf_add, puf_transpose_01
 *   - Kernel dispatchers: mingru, scan, sample, PPO, advantage, prio, etc.
 *   - Orthogonal init via Accelerate QR decomposition
 *   - Muon optimizer (Newton-Schulz via GPU GEMM + Metal kernels)
 *   - Model components: encoder, decoder, MinGRU forward/backward/init
 *
 * On Apple Silicon unified memory, CPU and GPU share the same physical pages.
 * Simple memory ops use CPU memcpy/memset (with GPU sync when needed).
 * Compute-heavy kernels dispatch to Metal GPU via MSL shaders.
 * GEMM uses Metal compute shaders (tensor_ops / steel_gemm on GPU).
 */

#import "metal_platform.h"

#include <arm_neon.h>
#include <algorithm>
#include <cstring>
#include <random>

// ============================================================================
// Helpers — stream access and GPU sync
// ============================================================================


// Bind a raw pointer (within a wrapped allocator) to a buffer binding slot.
static inline void mtl_set_ptr(MetalStream *ms, const void *ptr,
                               uint32_t index) {
  MetalContext *ctx = mtl_ctx();
  for (auto &wb : ctx->buffers) {
    if ((const char *)ptr >= wb.base &&
        (const char *)ptr < wb.base + wb.size) {
      NSUInteger offset = (NSUInteger)((const char *)ptr - wb.base);
      uint64_t addr = wb.buffer.gpuAddress + offset;
      if (ms->bound_addresses[index] != addr) {
        [ms->arg_table setAddress:addr atIndex:index];
        ms->bound_addresses[index] = addr;
      }
      return;
    }
  }
  assert(false && "Pointer not in any wrapped allocator buffer");
}

static inline void mtl_unwrap_ptr(const void *ptr_base) {
  auto &bufs = mtl_ctx()->buffers;
  bufs.erase(
      std::remove_if(bufs.begin(), bufs.end(),
                     [ptr_base](const WrappedBuffer &wb) {
                       return wb.base == (const char *)ptr_base;
                     }),
      bufs.end());
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
  bool gpu = puf_is_gpu_training() || puf_stream_has_encoder(stream);
  if (gpu && dst.dtype_size == 4) {
    mtl_copy_f32((float *)dst.bytes, (const float *)src.bytes,
                 (int)dst.numel(), stream);
  } else if (gpu && dst.dtype_size == 2) {
    mtl_copy_f16(dst.bytes, src.bytes, (int)dst.numel(), stream);
  } else {
    mtl_ensure_stream_synced(stream);
    memcpy(dst.bytes, src.bytes, dst.numel() * dst.dtype_size);
  }
}

void puf_zero(PufTensor &dst, cudaStream_t stream) {
  if (puf_is_gpu_training() && dst.dtype_size == 4) {
    mtl_fill_f32((float *)dst.bytes, 0.0f, (int)dst.numel(), stream);
  } else if (puf_is_gpu_training() && dst.dtype_size == 2) {
    mtl_fill_f16(dst.bytes, (int)dst.numel(), stream);
  } else {
    mtl_ensure_stream_synced(stream);
    memset(dst.bytes, 0, dst.numel() * dst.dtype_size);
  }
}

void puf_add(PufTensor &dst, const PufTensor &src, cudaStream_t stream) {
  assert(dst.numel() == src.numel() && "puf_add: size mismatch");
  assert(dst.dtype_size == src.dtype_size && "puf_add: dtype mismatch");
  if (puf_is_gpu_training()) {
    MetalStream *ms = mtl_resolve_stream(stream);
    ms->compute_encoder();
    const char *name = (dst.dtype_size == 2) ? "add_f16" : "add_f32";
    auto pso = mtl_pipeline(name);
    mtl_set_pso(ms, pso);
    mtl_set_ptr(ms, dst.bytes, 0);
    mtl_set_ptr(ms, src.bytes, 1);
    int count = (int)dst.numel();
    mtl_set_params(ms, count, 2);
    mtl_dispatch_1d(ms, pso, count);
  } else {
    assert(dst.dtype_size == 4 && "puf_add: CPU path supports f32 only");
    mtl_ensure_stream_synced(stream);
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
    MetalStream *ms = mtl_resolve_stream(stream);
    ms->compute_encoder();
    auto pso = mtl_pipeline("transpose_01_u64");
    mtl_set_pso(ms, pso);
    mtl_set_tensor(ms, dst, 0);
    mtl_set_tensor(ms, src, 1);
    struct {
      int A, B, C;
    } params = {A, B, C};
    mtl_set_params(ms, params, 2);
    mtl_dispatch_1d(ms, pso, A * B * C);
    return;
  }

  MetalStream *ms = mtl_resolve_stream(stream);
  ms->compute_encoder();
  auto pso = mtl_pipeline("transpose_01");
  mtl_set_pso(ms, pso);
  mtl_set_tensor(ms, dst, 0);
  mtl_set_tensor(ms, src, 1);
  struct {
    int A, B, C;
  } params = {A, B, C};
  mtl_set_params(ms, params, 2);
  mtl_dispatch_1d(ms, pso, A * B * C);
}

// ============================================================================
// Cast kernels
// ============================================================================

// CPU u8→f32 cast — NEON vectorized, no GPU dispatch, no sync needed.
void cpu_cast_u8_to_f32(float *dst, const uint8_t *src, int count) {
  int i = 0;
  for (; i + 16 <= count; i += 16) {
    uint8x16_t v = vld1q_u8(src + i);
    uint16x8_t lo16 = vmovl_u8(vget_low_u8(v));
    uint16x8_t hi16 = vmovl_u8(vget_high_u8(v));
    vst1q_f32(dst + i,      vcvtq_f32_u32(vmovl_u16(vget_low_u16(lo16))));
    vst1q_f32(dst + i + 4,  vcvtq_f32_u32(vmovl_u16(vget_high_u16(lo16))));
    vst1q_f32(dst + i + 8,  vcvtq_f32_u32(vmovl_u16(vget_low_u16(hi16))));
    vst1q_f32(dst + i + 12, vcvtq_f32_u32(vmovl_u16(vget_high_u16(hi16))));
  }
  for (; i < count; i++) dst[i] = (float)src[i];
}

void puf_cast_u8_to_f32(PufTensor &dst, const PufTensor &src,
                          cudaStream_t stream) {
  MetalStream *ms = mtl_resolve_stream(stream);
  ms->compute_encoder();
  auto pso = mtl_pipeline("cast_u8_to_f32");
  mtl_set_pso(ms, pso);
  mtl_set_tensor(ms, dst, 0);
  mtl_set_tensor(ms, src, 1);
  struct {
    int count;
  } params = {(int)src.numel()};
  mtl_set_params(ms, params, 2);
  mtl_dispatch_1d(ms, pso, (int)src.numel());
}

// ============================================================================
// fp16 cast dispatchers
// ============================================================================

void mtl_cast_f32_to_f16(void *dst, const float *src, int count,
                          cudaStream_t stream) {
  MetalStream *ms = mtl_resolve_stream(stream);
  ms->compute_encoder();
  auto pso = mtl_pipeline("cast_f32_to_f16");
  mtl_set_pso(ms, pso);
  mtl_set_ptr(ms, dst, 0);
  mtl_set_ptr(ms, src, 1);
  mtl_set_params(ms, count, 2);
  mtl_dispatch_1d(ms, pso, count);
}

void mtl_cast_f16_to_f32(float *dst, const void *src, int count,
                          cudaStream_t stream) {
  MetalStream *ms = mtl_resolve_stream(stream);
  ms->compute_encoder();
  auto pso = mtl_pipeline("cast_f16_to_f32");
  mtl_set_pso(ms, pso);
  mtl_set_ptr(ms, dst, 0);
  mtl_set_ptr(ms, src, 1);
  mtl_set_params(ms, count, 2);
  mtl_dispatch_1d(ms, pso, count);
}

// ============================================================================
// fp16 memory ops
// ============================================================================

void mtl_fill_f16(void *ptr, int count, cudaStream_t stream) {
  assert(count % 2 == 0 && "mtl_fill_f16: odd count would overwrite adjacent memory");
  MetalStream *ms = mtl_resolve_stream(stream);
  ms->compute_encoder();
  auto pso = mtl_pipeline("fill_f32");
  mtl_set_pso(ms, pso);
  mtl_set_ptr(ms, ptr, 0);
  int f32_count = count / 2;
  struct {
    float value;
    int count;
  } params = {0.0f, f32_count};
  mtl_set_params(ms, params, 1);
  mtl_dispatch_1d(ms, pso, f32_count);
}

void mtl_copy_f16(void *dst, const void *src, int count,
                   cudaStream_t stream) {
  assert(count % 2 == 0 && "mtl_copy_f16: odd count would over-read/write adjacent memory");
  MetalStream *ms = mtl_resolve_stream(stream);
  ms->compute_encoder();
  auto pso = mtl_pipeline("copy_f32");
  mtl_set_pso(ms, pso);
  mtl_set_ptr(ms, dst, 0);
  mtl_set_ptr(ms, src, 1);
  int f32_count = count / 2;
  mtl_set_params(ms, f32_count, 2);
  mtl_dispatch_1d(ms, pso, f32_count);
}

// ============================================================================
// Element-wise kernel dispatchers
// ============================================================================

void mtl_fill_f32(float *ptr, float value, int count, cudaStream_t stream) {
  MetalStream *ms = mtl_resolve_stream(stream);
  ms->compute_encoder();
  auto pso = mtl_pipeline("fill_f32");
  mtl_set_pso(ms, pso);
  mtl_set_ptr(ms, ptr, 0);
  struct {
    float value;
    int count;
  } params = {value, count};
  mtl_set_params(ms, params, 1);
  mtl_dispatch_1d(ms, pso, count);
}

void mtl_copy_f32(float *dst, const float *src, int count,
                   cudaStream_t stream) {
  MetalStream *ms = mtl_resolve_stream(stream);
  ms->compute_encoder();
  auto pso = mtl_pipeline("copy_f32");
  mtl_set_pso(ms, pso);
  mtl_set_ptr(ms, dst, 0);
  mtl_set_ptr(ms, src, 1);
  mtl_set_params(ms, count, 2);
  mtl_dispatch_1d(ms, pso, count);
}

void mtl_clamp_f32(float *ptr, float lo, float hi, int count,
                    cudaStream_t stream) {
  MetalStream *ms = mtl_resolve_stream(stream);
  ms->compute_encoder();
  auto pso = mtl_pipeline("clamp_f32");
  mtl_set_pso(ms, pso);
  mtl_set_ptr(ms, ptr, 0);
  struct {
    float lo, hi;
    int count;
  } params = {lo, hi, count};
  mtl_set_params(ms, params, 1);
  mtl_dispatch_1d(ms, pso, count);
}

void mtl_scale_f32(float *ptr, float scale, int count, cudaStream_t stream) {
  MetalStream *ms = mtl_resolve_stream(stream);
  ms->compute_encoder();
  auto pso = mtl_pipeline("scale_f32");
  mtl_set_pso(ms, pso);
  mtl_set_ptr(ms, ptr, 0);
  struct {
    float scale;
    int count;
  } params = {scale, count};
  mtl_set_params(ms, params, 1);
  mtl_dispatch_1d(ms, pso, count);
}

// dst += alpha * src
void mtl_axpy_f32(float *dst, const float *src, float alpha, int count,
                   cudaStream_t stream) {
  MetalStream *ms = mtl_resolve_stream(stream);
  ms->compute_encoder();
  auto pso = mtl_pipeline("axpy_f32");
  mtl_set_pso(ms, pso);
  mtl_set_ptr(ms, dst, 0);
  mtl_set_ptr(ms, src, 1);
  struct {
    float alpha;
    int count;
  } params = {alpha, count};
  mtl_set_params(ms, params, 2);
  mtl_dispatch_1d(ms, pso, count);
}

void mtl_nesterov_f32(float *momentum, const float *grad, float mu, int count,
                       cudaStream_t stream) {
  MetalStream *ms = mtl_resolve_stream(stream);
  ms->compute_encoder();
  auto pso = mtl_pipeline("nesterov_f32");
  mtl_set_pso(ms, pso);
  mtl_set_ptr(ms, momentum, 0);
  mtl_set_ptr(ms, grad, 1);
  struct {
    float mu;
    int count;
  } params = {mu, count};
  mtl_set_params(ms, params, 2);
  mtl_dispatch_1d(ms, pso, count);
}

// ============================================================================
// Norm and clip kernels
// ============================================================================

static float *norm_partials_buf = nullptr;
static void ensure_norm_partials() {
  if (!norm_partials_buf)
    norm_partials_buf = (float *)mtl_alloc_scratch(256 * sizeof(float));
}

void mtl_norm_f32(float *partials, const float *data, int count,
                   int num_blocks, cudaStream_t stream) {
  MetalStream *ms = mtl_resolve_stream(stream);
  ms->compute_encoder();
  auto pso = mtl_pipeline("norm_f32_kernel");
  mtl_set_pso(ms, pso);
  mtl_set_ptr(ms, partials, 0);
  mtl_set_ptr(ms, data, 1);
  struct {
    int count;
  } params = {count};
  mtl_set_params(ms, params, 2);
  mtl_dispatch_groups(ms, pso, num_blocks, 256);
}

void mtl_norm_reduce(float *result, const float *partials, int num_blocks,
                      cudaStream_t stream) {
  MetalStream *ms = mtl_resolve_stream(stream);
  ms->compute_encoder();
  auto pso = mtl_pipeline("norm_reduce_kernel");
  mtl_set_pso(ms, pso);
  mtl_set_ptr(ms, result, 0);
  mtl_set_ptr(ms, partials, 1);
  struct {
    int num_blocks;
  } params = {num_blocks};
  mtl_set_params(ms, params, 2);
  mtl_dispatch_groups(ms, pso, 1, 256);
}

void mtl_clip_by_norm_f32(float *data, const float *norm_ptr,
                            float max_norm, float eps, int count,
                            cudaStream_t stream) {
  MetalStream *ms = mtl_resolve_stream(stream);
  ms->compute_encoder();
  auto pso = mtl_pipeline("clip_by_norm_f32");
  mtl_set_pso(ms, pso);
  mtl_set_ptr(ms, data, 0);
  mtl_set_ptr(ms, norm_ptr, 1);
  struct {
    float max_norm, eps;
    int count;
  } params = {max_norm, eps, count};
  mtl_set_params(ms, params, 2);
  mtl_dispatch_1d(ms, pso, count);
}

// Matches CUDA normalize_f32_kernel: inv_norm = 1/max(sqrt(norm), eps), no cap.
void mtl_normalize_f32(float *data, const float *norm_ptr, float eps,
                        int count, cudaStream_t stream) {
  MetalStream *ms = mtl_resolve_stream(stream);
  ms->compute_encoder();
  auto pso = mtl_pipeline("normalize_f32");
  mtl_set_pso(ms, pso);
  mtl_set_ptr(ms, data, 0);
  mtl_set_ptr(ms, norm_ptr, 1);
  struct {
    float eps;
    int count;
  } params = {eps, count};
  mtl_set_params(ms, params, 2);
  mtl_dispatch_1d(ms, pso, count);
}

// Convenience: compute L2 norm of grad, clip in-place if > max_norm.
// scratch must point to a float in wrapped MTLBuffer memory.
void clip_grad_norm_f32(PufTensor &grad, float *scratch, float max_norm,
                        float eps, cudaStream_t stream) {
  MetalStream *ms = mtl_resolve_stream(stream);
  ensure_norm_partials();
  int count = (int)grad.numel();
  int num_blocks = (count + 255) / 256;
  if (num_blocks > 256) num_blocks = 256;
  mtl_norm_f32(norm_partials_buf, (const float *)grad.bytes, count, num_blocks,
               stream);
  mtl_barrier(ms);
  mtl_norm_reduce(scratch, norm_partials_buf, num_blocks, stream);
  mtl_barrier(ms);
  mtl_clip_by_norm_f32((float *)grad.bytes, scratch, max_norm, eps, count,
                       stream);
}

void mtl_transpose_f32(float *dst, const float *src, int rows, int cols,
                        cudaStream_t stream) {
  MetalStream *ms = mtl_resolve_stream(stream);
  ms->compute_encoder();
  auto pso = mtl_pipeline("transpose_f32");
  mtl_set_pso(ms, pso);
  mtl_set_ptr(ms, dst, 0);
  mtl_set_ptr(ms, src, 1);
  struct {
    int rows, cols;
  } params = {rows, cols};
  mtl_set_params(ms, params, 2);
  mtl_dispatch_1d(ms, pso, rows * cols);
}

// ============================================================================
// Decoder gradient assembly
// ============================================================================

void mtl_assemble_decoder_grad_f32(float *grad_out, const float *grad_logits,
                                     const float *grad_value, int B_TT, int od,
                                     int od1, cudaStream_t stream) {
  MetalStream *ms = mtl_resolve_stream(stream);
  ms->compute_encoder();
  auto pso = mtl_pipeline("assemble_decoder_grad_f32");
  mtl_set_pso(ms, pso);
  mtl_set_ptr(ms, grad_out, 0);
  mtl_set_ptr(ms, grad_logits, 1);
  mtl_set_ptr(ms, grad_value, 2);
  struct {
    int B_TT, od, od1;
  } params = {B_TT, od, od1};
  mtl_set_params(ms, params, 3);
  mtl_dispatch_1d(ms, pso, B_TT * od1);
}

// Assemble fp32 PPO gradients into fp16 decoder gradient output.
void mtl_assemble_decoder_grad_f32_to_f16(void *grad_out,
                                            const float *grad_logits,
                                            const float *grad_value, int B_TT,
                                            int od, int od1,
                                            cudaStream_t stream) {
  MetalStream *ms = mtl_resolve_stream(stream);
  ms->compute_encoder();
  auto pso = mtl_pipeline("assemble_decoder_grad_f32_to_f16");
  mtl_set_pso(ms, pso);
  mtl_set_ptr(ms, grad_out, 0);
  mtl_set_ptr(ms, grad_logits, 1);
  mtl_set_ptr(ms, grad_value, 2);
  struct { int B_TT, od, od_plus_1; } params = {B_TT, od, od1};
  mtl_set_params(ms, params, 3);
  mtl_dispatch_1d(ms, pso, B_TT * od1);
}

void mtl_sum_rows_to_f32(float *dst, const float *src, int rows, int cols,
                           cudaStream_t stream) {
  MetalStream *ms = mtl_resolve_stream(stream);
  ms->compute_encoder();
  auto pso = mtl_pipeline("sum_rows_to_f32_kernel");
  mtl_set_pso(ms, pso);
  mtl_set_ptr(ms, dst, 0);
  mtl_set_ptr(ms, src, 1);
  struct {
    int rows, cols;
  } params = {rows, cols};
  mtl_set_params(ms, params, 2);
  mtl_dispatch_1d(ms, pso, cols);
}

// ============================================================================
// MinGRU inference kernel
// ============================================================================

void mtl_mingru_gate(float *out, float *next_state, const float *combined,
                      const float *state_in, const float *x_in, int H, int B,
                      cudaStream_t stream) {
  MetalStream *ms = mtl_resolve_stream(stream);
  ms->compute_encoder();
  auto pso = mtl_pipeline("mingru_gate_inference");
  mtl_set_pso(ms, pso);
  mtl_set_ptr(ms, out, 0);
  mtl_set_ptr(ms, next_state, 1);
  mtl_set_ptr(ms, combined, 2);
  mtl_set_ptr(ms, state_in, 3);
  mtl_set_ptr(ms, x_in, 4);
  struct {
    int H, B;
  } params = {H, B};
  mtl_set_params(ms, params, 5);
  mtl_dispatch_1d(ms, pso, B * H);
}

// ============================================================================
// MinGRU training scan kernels
// ============================================================================

// Shared scan dispatch: binds all buffers and dispatches the named kernel.
static void dispatch_scan_forward(const char *kernel_name, PrefixScan &scan,
                                   cudaStream_t stream) {
  MetalStream *ms = mtl_resolve_stream(stream);
  ms->compute_encoder();
  auto pso = mtl_pipeline(kernel_name);
  mtl_set_pso(ms, pso);
  mtl_set_ptr(ms, scan.out.bytes, 0);
  mtl_set_ptr(ms, scan.next_state.bytes, 1);
  mtl_set_ptr(ms, scan.a_star.bytes, 2);
  mtl_set_ptr(ms, scan.s_vals.bytes, 3);
  mtl_set_ptr(ms, scan.log_values_buf.bytes, 4);
  mtl_set_ptr(ms, scan.combined_ptr, 5);
  mtl_set_ptr(ms, scan.state_ptr, 6);
  mtl_set_ptr(ms, scan.input_ptr, 7);
  struct { int T_seq, H, B; } params = {scan.T, scan.H, scan.B};
  mtl_set_params(ms, params, 8);
  mtl_dispatch_1d(ms, pso, scan.B * scan.H);
}

static void dispatch_scan_backward(const char *kernel_name, PrefixScan &scan,
                                    const void *grad, const void *grad_next_state,
                                    cudaStream_t stream) {
  MetalStream *ms = mtl_resolve_stream(stream);
  ms->compute_encoder();
  auto pso = mtl_pipeline(kernel_name);
  mtl_set_pso(ms, pso);
  mtl_set_ptr(ms, scan.grad_combined.bytes, 0);
  mtl_set_ptr(ms, scan.grad_state.bytes, 1);
  mtl_set_ptr(ms, scan.grad_input.bytes, 2);
  mtl_set_ptr(ms, grad, 3);
  mtl_set_ptr(ms, grad_next_state, 4);
  mtl_set_ptr(ms, scan.combined_ptr, 5);
  mtl_set_ptr(ms, scan.state_ptr, 6);
  mtl_set_ptr(ms, scan.input_ptr, 7);
  mtl_set_ptr(ms, scan.a_star.bytes, 8);
  mtl_set_ptr(ms, scan.s_vals.bytes, 9);
  mtl_set_ptr(ms, scan.log_values_buf.bytes, 10);
  struct { int T_seq, H, B; } params = {scan.T, scan.H, scan.B};
  mtl_set_params(ms, params, 11);
  mtl_dispatch_1d(ms, pso, scan.B * scan.H);
}

void mtl_fused_scan_forward(PrefixScan &scan, cudaStream_t stream) {
  dispatch_scan_forward("fused_scan_forward_checkpointed", scan, stream);
}
void mtl_fused_scan_backward(PrefixScan &scan, const float *grad,
                               const float *grad_next_state, cudaStream_t stream) {
  dispatch_scan_backward("fused_scan_backward_checkpointed", scan, grad, grad_next_state, stream);
}
void mtl_fused_scan_forward_fp16(PrefixScan &scan, cudaStream_t stream) {
  dispatch_scan_forward("fused_scan_forward_checkpointed_fp16", scan, stream);
}
void mtl_fused_scan_backward_fp16(PrefixScan &scan, const void *grad,
                                    const void *grad_next_state, cudaStream_t stream) {
  dispatch_scan_backward("fused_scan_backward_checkpointed_fp16", scan, grad, grad_next_state, stream);
}

// ============================================================================
// Sample logits kernel
// ============================================================================

// Dispatch GPU sampling kernel on the current command buffer (no sync).
// Call BEFORE ensure_gpu_synced so sampling runs in the same command buffer
// as the forward pass.
void mtl_sample_logits_dispatch_to(
    PufTensor &dec_out, PufTensor &act_sizes_puf,
    float *action_out_f32, float *logprobs, float *value_out,
    const float *action_mask, int mask_stride,
    uint64_t seed, uint32_t *offset_ptr, cudaStream_t stream) {

  int B = (int)dec_out.shape[0];
  int fused_cols = (int)dec_out.shape[1];
  int num_atns = (int)act_sizes_puf.numel();
  int A_total = fused_cols - 1;

  assert(action_out_f32 && "sampling destination buffer must be allocated");

  MetalStream *ms = mtl_resolve_stream(stream);
  ms->compute_encoder();
  auto pso = mtl_pipeline("sample_logits_kernel");
  mtl_set_pso(ms, pso);

  mtl_set_ptr(ms, action_out_f32, 0);
  mtl_set_ptr(ms, logprobs, 1);
  mtl_set_ptr(ms, value_out, 2);
  mtl_set_ptr(ms, dec_out.bytes, 3);
  mtl_set_ptr(ms, dec_out.bytes, 4);  // dummy logstd (discrete only)
  // value column is the last fused decoder column.
  mtl_set_ptr(ms, (float *)dec_out.bytes + (fused_cols - 1), 5);
  mtl_set_ptr(ms, act_sizes_puf.bytes, 6);
  uint32_t offset_snapshot = *offset_ptr;
  *offset_ptr = offset_snapshot + 1u;

  struct {
    uint64_t seed;
    uint32_t offset;
    int num_atns;
    int num_atns_total;
    int B;
    int logits_stride;
    int logstd_stride;
    int value_stride;
    int is_continuous;
    int mask_stride;
  } params = {seed, offset_snapshot, num_atns, A_total, B,
              fused_cols, 0, fused_cols, 0, mask_stride};
  mtl_set_params(ms, params, 7);

  mtl_set_ptr(ms, (void *)action_mask, 8);

  mtl_dispatch_1d(ms, pso, B);
}

// Expand f32 GPU actions to f64 (call after ensure_gpu_synced).
void mtl_sample_logits_expand(const float *f32, double *f64, int count) {
  for (int i = 0; i < count; i++) f64[i] = (double)f32[i];
}

// Recompute logprobs from CPU-produced logits using GPU fast::exp.
// Dispatches on the given stream, no sync. The tiny kernel (B threads)
// completes in ~1us and ensures old_logp matches PPO training precision.
void mtl_recompute_logprobs(
    float *logprobs, const float *logits, const float *actions_f32,
    const int *act_sizes, const float *action_mask, int mask_stride,
    int B, int num_atns, int fused_cols, cudaStream_t stream) {

  MetalStream *ms = mtl_resolve_stream(stream);
  ms->compute_encoder();
  auto pso = mtl_pipeline("recompute_logprobs_kernel");
  mtl_set_pso(ms, pso);

  mtl_set_ptr(ms, logprobs, 0);
  mtl_set_ptr(ms, (void *)logits, 1);
  mtl_set_ptr(ms, (void *)actions_f32, 2);
  mtl_set_ptr(ms, (void *)act_sizes, 3);
  mtl_set_ptr(ms, (void *)action_mask, 4);

  struct {
    int B, num_atns, logits_stride, mask_stride;
  } params = {B, num_atns, fused_cols, mask_stride};
  mtl_set_params(ms, params, 5);

  mtl_dispatch_1d(ms, pso, B);
}

// ============================================================================
// PPO loss fused forward + backward
// ============================================================================

// PPO scratch buffers — file-scope so mtl_kernels_reset() can free them.
static float *ppo_partials_buf = nullptr;
static int ppo_partials_capacity = 0;
static float *ppo_act_f32 = nullptr;
static int ppo_act_f32_capacity = 0;

void ppo_loss_fwd_bwd(PufTensor &dec_out, PufTensor &logstd, TrainGraph &graph,
                       PufTensor &act_sizes, PufTensor &losses_acc,
                       float clip_coef, float vf_clip_coef, float vf_coef,
                       float ent_coef, PPOBuffersPuf &bufs, bool is_continuous,
                       const float *ext_mask_ptr, int ext_mask_stride,
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

  MetalStream *ms = mtl_resolve_stream(stream);

  // var_mean of advantages
  {
    ms->compute_encoder();
    auto pso = mtl_pipeline("var_mean_kernel");
    mtl_set_pso(ms, pso);
    mtl_set_ptr(ms, graph.mb_advantages.bytes, 0);
    mtl_set_ptr(ms, bufs.adv_scratch.bytes, 1); // var
    mtl_set_ptr(ms, (float *)bufs.adv_scratch.bytes + 1, 2); // mean
    struct {
      int count;
    } params = {(int)graph.mb_advantages.numel()};
    mtl_set_params(ms, params, 3);
    mtl_dispatch_groups(ms, pso, 1, 256);
  }

  // PPO partials buffer
  int ppo_threads = 256;
  int ppo_grid = (total + ppo_threads - 1) / ppo_threads;
  int ppo_partials_needed = ppo_grid * (LOSS_N + 1);
  if (!ppo_partials_buf || ppo_partials_needed > ppo_partials_capacity) {
    if (ppo_partials_buf) {
      mtl_unwrap_ptr(ppo_partials_buf);
      free(ppo_partials_buf);
    }
    ppo_partials_capacity = ppo_partials_needed;
    ppo_partials_buf = (float *)mtl_alloc_scratch(ppo_partials_capacity * sizeof(float));
  }

  // Zero loss output on GPU (CUDA uses cudaMemsetAsync here — CPU write races
  // with reduce kernel's *loss += sum from the previous minibatch).
  puf_zero(bufs.loss_output, stream);

  // MSL doesn't support double — convert actions from f64 to f32 on GPU.
  // Uses cast_f64_to_f32 kernel (IEEE 754 bit manipulation via uint2) to
  // avoid flushing the GPU encoder for a CPU conversion loop.
  int act_count = (int)graph.mb_actions.numel();
  if (!ppo_act_f32 || act_count > ppo_act_f32_capacity) {
    if (ppo_act_f32) {
      mtl_unwrap_ptr(ppo_act_f32);
      free(ppo_act_f32);
    }
    ppo_act_f32_capacity = act_count;
    ppo_act_f32 = (float *)mtl_alloc_scratch(ppo_act_f32_capacity * sizeof(float));
  }
  {
    ms->compute_encoder();
    auto pso = mtl_pipeline("cast_f64_to_f32");
    mtl_set_pso(ms, pso);
    mtl_set_ptr(ms, graph.mb_actions.bytes, 0);  // src: f64 as uint2*
    mtl_set_ptr(ms, ppo_act_f32, 1);             // dst: f32
    mtl_set_params(ms, act_count, 2);
    mtl_dispatch_1d(ms, pso, act_count);
  }

  // Action mask: either from external all-ones buffer (no mask) or embedded in obs.
  int input_size = (int)graph.mb_obs.shape[2];
  const float *mask_ptr;
  int mask_stride;
  if (ext_mask_ptr) {
    mask_ptr = ext_mask_ptr;
    mask_stride = ext_mask_stride;
  } else {
    // Mask embedded in mb_obs last A_total columns
    int mask_offset = input_size - A_total;
    mask_ptr = (const float *)graph.mb_obs.bytes + mask_offset;
    mask_stride = input_size;
  }

  // Metal 4 coherence: barrier so PPO kernel sees var_mean + cast_f64 outputs.
  // Without this, adv_mean/adv_var reads as zero → 1e8× gradient explosion.
  mtl_barrier(ms);

  // Fused PPO kernel
  {
    ms->compute_encoder();
    auto pso = mtl_pipeline("ppo_loss_fwd_bwd_kernel");
    mtl_set_pso(ms, pso);
    mtl_set_ptr(ms, ppo_partials_buf, 0);
    mtl_set_ptr(ms, bufs.grad_logits.bytes, 1);
    mtl_set_ptr(ms, is_continuous ? bufs.grad_logstd.bytes
                                   : bufs.grad_logits.bytes,
                2);
    mtl_set_ptr(ms, bufs.grad_values.bytes, 3);
    mtl_set_ptr(ms, dec_out.bytes, 4);                   // logits
    mtl_set_ptr(ms, is_continuous ? logstd.bytes : dec_out.bytes, 5); // logstd
    mtl_set_ptr(ms, (float *)dec_out.bytes + A_total, 6); // values_pred (last column of fused decoder output)
    mtl_set_ptr(ms, ppo_act_f32, 7);  // f32 actions (converted from f64)
    mtl_set_ptr(ms, graph.mb_logprobs.bytes, 8);
    mtl_set_ptr(ms, graph.mb_advantages.bytes, 9);
    mtl_set_ptr(ms, graph.mb_prio.bytes, 10);
    mtl_set_ptr(ms, graph.mb_values.bytes, 11);
    mtl_set_ptr(ms, graph.mb_returns.bytes, 12);
    mtl_set_ptr(ms, (float *)bufs.adv_scratch.bytes + 1, 13); // adv_mean
    mtl_set_ptr(ms, bufs.adv_scratch.bytes, 14);               // adv_var
    mtl_set_ptr(ms, act_sizes.bytes, 15);

    struct {
      int num_atns;
      float clip_coef, vf_clip_coef, vf_coef, ent_coef;
      int T_seq, A_total, N;
      int logits_stride_n, logits_stride_t, logits_stride_a;
      int values_stride_n, values_stride_t;
      int is_continuous;
      int num_atns_total;
      int mask_stride_val;
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
                is_continuous ? 1 : 0,
                A_total,
                mask_stride};
    mtl_set_params(ms, params, 16);
    mtl_set_ptr(ms, (void *)mask_ptr, 17);
    mtl_set_ptr(ms, graph.mb_ratio.bytes, 18);
    mtl_set_ptr(ms, graph.mb_newvalue.bytes, 19);
    mtl_dispatch_groups(ms, pso, ppo_grid, ppo_threads);
  }

  // Metal 4 coherence: force visibility before reduce kernel consumes partials.
  mtl_barrier(ms);

  // Reduce partials
  {
    ms->compute_encoder();
    auto pso = mtl_pipeline("ppo_loss_reduce_kernel");
    mtl_set_pso(ms, pso);
    mtl_set_ptr(ms, bufs.loss_output.bytes, 0);
    mtl_set_ptr(ms, losses_acc.bytes, 1);
    mtl_set_ptr(ms, ppo_partials_buf, 2);
    struct {
      int num_blocks;
    } params = {ppo_grid};
    mtl_set_params(ms, params, 3);

    mtl_dispatch_groups(ms, pso, 1, LOSS_N + 1);
  }
}

// Scatter mb_ratio and mb_newvalue from minibatch back into rollout buffers.
// Called after ppo_loss_fwd_bwd so subsequent minibatches see updated values.
void mtl_scatter_ppo_outputs(TrainGraph& graph, RolloutBuf& rollouts,
                              const int64_t* idx, cudaStream_t stream) {
    MetalStream *ms = mtl_resolve_stream(stream);
    int num_idx = (int)graph.mb_ratio.shape[0];

    auto scatter = [&](PufTensor& dst, PufTensor& src) {
        ms->compute_encoder();
        auto pso = mtl_pipeline("index_copy_kernel");
        mtl_set_pso(ms, pso);
        int row_bytes = (int)(src.shape[1] * src.dtype_size);
        mtl_set_ptr(ms, dst.bytes, 0);
        mtl_set_ptr(ms, (void*)idx, 1);
        mtl_set_ptr(ms, src.bytes, 2);
        struct { int num_idx; int row_bytes; } p = {num_idx, row_bytes};
        mtl_set_params(ms, p, 3);
        mtl_dispatch_groups(ms, pso, (num_idx + 255) / 256, 256);
    };

    scatter(rollouts.ratio, graph.mb_ratio);
    scatter(rollouts.values, graph.mb_newvalue);
}

// ============================================================================
// Advantage computation
// ============================================================================

void puff_advantage(PufTensor &values, PufTensor &rewards,
                          PufTensor &dones, PufTensor &importance,
                          PufTensor &advantages, float gamma, float lambda,
                          float rho_clip, float c_clip, cudaStream_t stream) {
  int num_steps = (int)values.shape[0], horizon = (int)values.shape[1];
  assert(advantages.dtype_size == 4 && "advantages must be f32");

  MetalStream *ms = mtl_resolve_stream(stream);
  ms->compute_encoder();
  auto pso = mtl_pipeline("puff_advantage_kernel");
  mtl_set_pso(ms, pso);
  mtl_set_tensor(ms, values, 0);
  mtl_set_tensor(ms, rewards, 1);
  mtl_set_tensor(ms, dones, 2);
  mtl_set_tensor(ms, importance, 3);
  mtl_set_tensor(ms, advantages, 4);
  struct {
    float gamma, lambda, rho_clip, c_clip;
    int num_steps, horizon;
  } params = {gamma, lambda, rho_clip, c_clip, num_steps, horizon};
  mtl_set_params(ms, params, 5);
  int blocks = (num_steps + 255) / 256;
  mtl_dispatch_groups(ms, pso, blocks, 256);
}

// ============================================================================
// Priority replay
// ============================================================================

// Phase 1: compute normalized per-segment probabilities on GPU.
void prio_precompute(PufTensor &advantages, float prio_alpha,
                     PrioBuffers &bufs, cudaStream_t stream) {
  int S = (int)advantages.shape[0], T = (int)advantages.shape[1];
  MetalStream *ms = mtl_resolve_stream(stream);

  // Prio adv reduction
  {
    ms->compute_encoder();
    auto pso = mtl_pipeline("prio_adv_reduction_kernel");
    mtl_set_pso(ms, pso);
    mtl_set_tensor(ms, advantages, 0);
    mtl_set_ptr(ms, bufs.prio_probs.bytes, 1);
    struct {
      float prio_alpha;
      int stride;
    } params = {prio_alpha, T};
    mtl_set_params(ms, params, 2);
    mtl_dispatch_groups(ms, pso, S, 32);
  }
  mtl_barrier(ms); // reduction -> normalize

  // Normalize
  {
    ms->compute_encoder();
    auto pso = mtl_pipeline("prio_normalize_kernel");
    mtl_set_pso(ms, pso);
    mtl_set_ptr(ms, bufs.prio_probs.bytes, 0);
    struct {
      int S;
    } params = {S};
    mtl_set_params(ms, params, 1);
    mtl_dispatch_groups(ms, pso, 1, 256);
  }
  mtl_barrier(ms); // normalize -> sample
}

// Phase 2 (per-minibatch): GPU sampling from prio_probs + GPU importance weights.
void prio_sample(int minibatch_segments, int total_agents,
                 float anneal_beta, PrioBuffers &bufs, uint64_t seed,
                 uint32_t *offset_ptr, cudaStream_t stream) {
  int S = (int)bufs.prio_probs.shape[0];
  MetalStream *ms = mtl_resolve_stream(stream);

  // prio_probs -> sampled indices
  ms->compute_encoder();
  {
    auto pso = mtl_pipeline("prio_sample_kernel");
    mtl_set_pso(ms, pso);
    mtl_set_ptr(ms, bufs.idx.bytes, 0);
    mtl_set_ptr(ms, bufs.prio_probs.bytes, 1);
    uint32_t base_offset = *offset_ptr;
    *offset_ptr = base_offset + (uint32_t)minibatch_segments;
    struct {
      uint64_t seed;
      uint32_t base_offset;
      int total_segments;
      int minibatch_segments;
    } params = {seed, base_offset, S, minibatch_segments};
    mtl_set_params(ms, params, 2);
    mtl_dispatch_1d(ms, pso, minibatch_segments);
  }

  mtl_barrier(ms);  // sampled idx -> imp-weights

  // sampled indices + prio_probs -> importance weights
  ms->compute_encoder();
  {
    auto pso = mtl_pipeline("prio_imp_weights_kernel");
    mtl_set_pso(ms, pso);
    mtl_set_ptr(ms, bufs.idx.bytes, 0);
    mtl_set_ptr(ms, bufs.prio_probs.bytes, 1);
    mtl_set_ptr(ms, bufs.mb_prio.bytes, 2);
    struct {
      int total_agents;
      float anneal_beta;
      int minibatch_segments;
    } params = {total_agents, anneal_beta, minibatch_segments};
    mtl_set_params(ms, params, 3);
    mtl_dispatch_1d(ms, pso, minibatch_segments);
  }
}

// ============================================================================
// Select copy (minibatch assembly)
// ============================================================================

void mtl_select_copy(RolloutBuf &rollouts, TrainGraph &graph,
                      const int64_t *idx, const float *advantages,
                      const float *mb_prio, int mb_segs,
                      void *fp16_obs_out, cudaStream_t stream) {
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

  MetalStream *ms = mtl_resolve_stream(stream);
  ms->compute_encoder();
  auto pso = mtl_pipeline("select_copy_kernel");
  mtl_set_pso(ms, pso);

  mtl_set_ptr(ms, graph.mb_obs.bytes, 0);
  mtl_set_ptr(ms, graph.mb_actions.bytes, 1);
  mtl_set_ptr(ms, graph.mb_logprobs.bytes, 2);
  mtl_set_ptr(ms, graph.mb_values.bytes, 3);
  mtl_set_ptr(ms, graph.mb_advantages.bytes, 4);
  mtl_set_ptr(ms, graph.mb_returns.bytes, 5);
  mtl_set_ptr(ms, graph.mb_prio.bytes, 6);
  mtl_set_ptr(ms, rollouts.observations.bytes, 7);
  mtl_set_ptr(ms, rollouts.actions.bytes, 8);
  mtl_set_ptr(ms, rollouts.logprobs.bytes, 9);
  mtl_set_ptr(ms, rollouts.values.bytes, 10);
  mtl_set_ptr(ms, advantages, 11);
  mtl_set_ptr(ms, idx, 12);
  mtl_set_ptr(ms, mb_prio, 13);

  struct {
    int obs_row_bytes, act_row_bytes, lp_row_bytes, horizon;
  } params = {obs_row_bytes, act_row_bytes, lp_row_bytes, horizon};
  mtl_set_params(ms, params, 14);

  mtl_set_ptr(ms, fp16_obs_out, 15);

  // 2D dispatch: (mb_segs, 5) threadgroups, 256 threads each
  [ms->enc dispatchThreadgroups:MTLSizeMake(mb_segs, 5, 1)
      threadsPerThreadgroup:MTLSizeMake(256, 1, 1)];
}

// ============================================================================
// Muon weight update kernel
// ============================================================================

void mtl_muon_weight_update(float *weights, const float *updates,
                              const float *lr_ptr, int count,
                              cudaStream_t stream) {
  MetalStream *ms = mtl_resolve_stream(stream);
  ms->compute_encoder();
  auto pso = mtl_pipeline("muon_weight_update_kernel");
  mtl_set_pso(ms, pso);
  mtl_set_ptr(ms, weights, 0);
  mtl_set_ptr(ms, updates, 1);
  mtl_set_ptr(ms, lr_ptr, 2);
  struct {
    int count;
  } params = {count};
  mtl_set_params(ms, params, 3);
  mtl_dispatch_1d(ms, pso, count);
}

// ============================================================================
// Orthogonal init via Accelerate LAPACK (CPU-side)
//
// Replaces cuSOLVER + cuRAND from models.cu. Uses sgeqrf (QR) + sorgqr
// from Accelerate. Runs once at model init — not perf-critical.
// ============================================================================

void puf_orthogonal_init(PufTensor &dst, float gain, uint64_t seed,
                          cudaStream_t stream) {
  mtl_ensure_stream_synced(stream);

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
               double lr_val, double momentum,
               int ns_iters, Allocator &alloc) {
  m->momentum = momentum;
  m->ns_iters = (ns_iters > 0 && ns_iters <= 5) ? ns_iters : 5;
  m->lr_val_init = (float)lr_val;
  m->lr_ptr = nullptr;
  m->lr_derived_ptr = nullptr;
  m->wb_puf = weight_buffer;
  m->param_alloc = param_alloc;
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

void muon_step(Muon *m, cudaStream_t stream) {
  assert(m->wb_puf.bytes != nullptr && "muon_step: weights not initialized");
  MetalStream *ms = mtl_resolve_stream(stream);

  // No NCCL on Metal (single GPU)

  // Nesterov momentum update: mb = mu * mb + gc
  mtl_nesterov_f32((float *)m->mb_puf.bytes, (float *)m->gc_puf.bytes,
                   (float)m->momentum, (int)m->mb_puf.numel(), stream);
  mtl_barrier(ms);

  // Zero update buffer
  puf_zero(m->up_puf, stream);
  mtl_barrier(ms);

  int64_t offset = 0;
  for (auto *t : m->param_alloc->regs) {
    float *gc_ptr = (float *)m->gc_puf.bytes + offset;
    float *up_ptr = (float *)m->up_puf.bytes + offset;
    int64_t R = t->shape[0];
    int64_t C = t->numel() / std::max<int64_t>(1, R);
    // NS orthogonalization for 2D+ params with M >= 2.
    // 1-row tensors (e.g. value weight) use direct gradient update.
    if (t->ndim() >= 2 && std::min(R, C) >= 2) {
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
      mtl_barrier(ms);

      // Normalize x (matches CUDA models.cu:1219 — no inv_norm cap)
      ensure_norm_partials();
      {
        int nblk = std::min((int)((x.numel() + 255) / 256), 256);
        mtl_norm_f32(norm_partials_buf, (const float *)x.bytes,
                     (int)x.numel(), nblk, stream);
        mtl_barrier(ms);
        mtl_norm_reduce(m->ns.norm_ptr, norm_partials_buf, nblk, stream);
        mtl_barrier(ms);
      }
      mtl_normalize_f32((float *)x.bytes, m->ns.norm_ptr, 1e-7f,
                        (int)x.numel(), stream);
      mtl_barrier(ms);

      // Newton-Schulz iterations (all GEMM — synced internally)
      // When ns_iters < 5, space out coefficient indices evenly across the
      // full 5-step schedule (e.g. ns_iters=3 uses coeffs {0,2,4}) so we
      // get both aggressive early corrections and conservative polishing.
      for (int i = 0; i < m->ns_iters; ++i) {
        int ci = i * 4 / (m->ns_iters - 1 + (m->ns_iters == 1));
        float a = (float)ns_coeffs[ci][0], b = (float)ns_coeffs[ci][1],
              c = (float)ns_coeffs[ci][2];
        PufTensor &src = (i % 2 == 0) ? x : tmp;
        PufTensor &dst = (i % 2 == 0) ? tmp : x;
        puf_mm(src, src, A, stream);
        mtl_barrier(ms);
        puf_copy(gram, A, stream);
        mtl_barrier(ms);
        puf_addmm_nn(A, A, gram, c, b, stream);
        mtl_barrier(ms);
        puf_copy(dst, src, stream);
        mtl_barrier(ms);
        puf_addmm_nn(gram, src, dst, 1.0f, a, stream);
        mtl_barrier(ms);
      }

      PufTensor &result_precision = (m->ns_iters % 2 == 0) ? x : tmp;

      // Scale matches CUDA models.cu:1233: sqrt(max(1.0, M/N)).
      // No post-NS re-normalization — CUDA doesn't do it.
      float scale = (float)std::sqrt(std::max(1.0, (double)M / (double)N));
      if (scale != 1.0f) {
        mtl_scale_f32((float *)result_precision.bytes, scale,
                      (int)result_precision.numel(), stream);
        mtl_barrier(ms);
      }

      PufTensor out_f32 = {.bytes = (char *)up_ptr,
                           .shape = {R, C},
                           .dtype_size = 4};
      if (transposed_flag) {
        mtl_transpose_f32((float *)out_f32.bytes,
                          (const float *)result_precision.bytes, (int)M,
                          (int)N, stream);
      } else {
        puf_copy(out_f32, result_precision, stream);
      }
      mtl_barrier(ms);
    } else {
      // 1D and tiny matrix params: use direct gradient update.
      PufTensor src_puf = {.bytes = (char *)gc_ptr,
                           .shape = {t->numel()},
                           .dtype_size = 4};
      PufTensor dst_puf = {.bytes = (char *)up_ptr,
                           .shape = {t->numel()},
                           .dtype_size = 4};
      puf_copy(dst_puf, src_puf, stream);
      mtl_barrier(ms);
    }
    offset += t->numel();
  }

  // Apply weight update: w -= lr * up
  mtl_muon_weight_update((float *)m->wb_puf.bytes, (const float *)m->up_puf.bytes,
                         m->lr_ptr, (int)m->wb_puf.numel(), stream);
  mtl_barrier(ms);
}

// ============================================================================
// Model components — encoder (single linear, matching upstream CUDA)
// ============================================================================

static PufTensor encoder_forward(void *w, void *activations,
                                         PufTensor input, cudaStream_t stream) {
  EncoderWeights *ew = (EncoderWeights *)w;
  EncoderActivations *a = (EncoderActivations *)activations;
  MetalStream *ms = mtl_resolve_stream(stream);
  if (a->saved_input.bytes)
    puf_copy(a->saved_input, input, stream);

  puf_mm(input, ew->weight, a->out, stream);
  mtl_barrier(ms);

  return a->out;
}

static void encoder_backward(void *w, void *activations, PufTensor grad,
                                      cudaStream_t stream) {
  EncoderActivations *a = (EncoderActivations *)activations;
  puf_mm_tn(grad, a->saved_input, a->wgrad, stream);
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

static void encoder_reg_train(void *w, void *activations,
                                       Allocator *acts, Allocator *grads,
                                       int B_TT, int precision) {
  EncoderWeights *ew = (EncoderWeights *)w;
  EncoderActivations *a = (EncoderActivations *)activations;
  int p = precision;
  *a = (EncoderActivations){
      .out = {.shape = {B_TT, ew->out_dim}, .dtype_size = p},
      .saved_input = {.shape = {B_TT, ew->in_dim}, .dtype_size = p},
      .wgrad = {.shape = {ew->out_dim, ew->in_dim}, .dtype_size = p},
  };
  acts->reg(&a->out);
  acts->reg(&a->saved_input);
  grads->reg(&a->wgrad);
}

static void encoder_reg_rollout(void *w, void *activations,
                                         Allocator *alloc, int B) {
  EncoderWeights *ew = (EncoderWeights *)w;
  EncoderActivations *a = (EncoderActivations *)activations;
  a->out = {.shape = {B, ew->out_dim}, .dtype_size = PRECISION_SIZE};
  alloc->reg(&a->out);
}

static PufTensor decoder_forward(void *w, void *activations,
                                          PufTensor input,
                                          cudaStream_t stream) {
  DecoderWeights *dw = (DecoderWeights *)w;
  DecoderActivations *a = (DecoderActivations *)activations;
  MetalStream *ms = mtl_resolve_stream(stream);
  if (a->saved_input.bytes)
    puf_copy(a->saved_input, input, stream);
  puf_mm(input, dw->weight, a->out, stream);
  mtl_barrier(ms);
  return a->out;
}

static PufTensor decoder_backward(void *w, void *activations,
                                           PufTensor grad_logits,
                                           PufTensor grad_logstd,
                                           PufTensor grad_value,
                                           cudaStream_t stream) {
  DecoderWeights *dw = (DecoderWeights *)w;
  DecoderActivations *a = (DecoderActivations *)activations;
  int B_TT = (int)a->saved_input.shape[0];
  int od = dw->output_dim, od1 = od + 1;

  MetalStream *ms = mtl_resolve_stream(stream);

  // assemble gradient: concat [grad_logits, grad_value] per row
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
  mtl_barrier(ms);  // assemble writes grad_out, GEMMs read it

  // weight grad: grad_out^T @ saved_input
  puf_mm_tn(a->grad_out, a->saved_input, a->wgrad, stream);

  if (dw->continuous && grad_logstd.bytes != nullptr) {
    mtl_sum_rows_to_f32((float *)a->logstd_scratch.bytes,
                        (const float *)grad_logstd.bytes, B_TT,
                        dw->output_dim, stream);
  }

  // grad → hidden: grad_out @ weight
  puf_mm_nn(a->grad_out, dw->weight, a->grad_input, stream);
  mtl_barrier(ms);  // grad_input consumed by mingru_backward
  return a->grad_input;
}

static void decoder_init_weights(void *w, uint64_t *seed,
                                          cudaStream_t stream) {
  DecoderWeights *dw = (DecoderWeights *)w;
  int od1 = dw->output_dim + 1;
  PufTensor wt = {.bytes = dw->weight.bytes,
                  .shape = {od1, dw->hidden_dim},
                  .dtype_size = dw->weight.dtype_size};
  puf_orthogonal_init(wt, 0.01f, (*seed)++, stream);
}

static void decoder_reg_params(void *w, Allocator *alloc, int esz) {
  DecoderWeights *dw = (DecoderWeights *)w;
  int od = dw->output_dim;
  int H = dw->hidden_dim;
  // Single fused weight {od+1, H} — value row participates in NS orthogonalization,
  // matching CUDA models.cu:792-795.
  dw->weight = {.shape = {od + 1, H}, .dtype_size = esz};
  alloc->reg(&dw->weight);
  if (dw->continuous) {
    dw->logstd = {.shape = {1, od}, .dtype_size = esz};
    alloc->reg(&dw->logstd);
  }
}

static void decoder_reg_train(void *w, void *activations,
                                       Allocator *acts, Allocator *grads,
                                       int B_TT, int precision) {
  DecoderWeights *dw = (DecoderWeights *)w;
  DecoderActivations *a = (DecoderActivations *)activations;
  int p = precision;
  int od1 = dw->output_dim + 1;
  *a = (DecoderActivations){
      .out = {.shape = {B_TT, od1}, .dtype_size = p},
      .grad_out = {.shape = {B_TT, od1}, .dtype_size = p},
      .saved_input = {.shape = {B_TT, dw->hidden_dim}, .dtype_size = p},
      .grad_input = {.shape = {B_TT, dw->hidden_dim}, .dtype_size = p},
      .wgrad = {.shape = {od1, dw->hidden_dim}, .dtype_size = p},
      .logstd_scratch = {.shape = {1, dw->output_dim}, .dtype_size = p},
  };
  acts->reg(&a->out);
  acts->reg(&a->saved_input);
  // grad registration order MUST match param registration order in reg_params
  acts->reg(&a->grad_out);
  acts->reg(&a->grad_input);
  grads->reg(&a->wgrad);
  if (dw->continuous)
    grads->reg(&a->logstd_scratch);
}

static void decoder_reg_rollout(void *w, void *activations,
                                         Allocator *alloc, int B) {
  DecoderWeights *dw = (DecoderWeights *)w;
  DecoderActivations *a = (DecoderActivations *)activations;
  int od1 = dw->output_dim + 1;
  *a = {};
  a->out = {.shape = {B, od1}, .dtype_size = PRECISION_SIZE};
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
                               Allocator *grads, int B_TT, int precision) {
  MinGRUWeights *m = (MinGRUWeights *)w;
  MinGRUActivations *a = (MinGRUActivations *)activations;
  int H = m->hidden, TT = m->horizon, B = B_TT / TT, p = precision;
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
        .grad_input = {.shape = {B, TT, H}, .dtype_size = p},
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
    acts->reg(&a->scan_bufs[i].grad_input);
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
  MetalStream *ms = mtl_resolve_stream(stream);

  for (int i = 0; i < m->num_layers; i++) {
    PufTensor state_i = mingru_state_layer(m, state, i);
    puf_mm(x, m->weights[i], a->combined[i], stream);
    mtl_barrier(ms);
    mtl_mingru_gate((float *)a->out.bytes, (float *)a->next_state.bytes,
                    (const float *)a->combined[i].bytes,
                    (const float *)state_i.bytes, (const float *)x.bytes, H, B,
                    stream);
    mtl_barrier(ms);
    puf_copy(state_i, a->next_state, stream);
    if (i + 1 < m->num_layers)
      mtl_barrier(ms);
    x = a->out;
  }
  return x;
}

static PufTensor mingru_forward_train(void *w, PufTensor x, PufTensor state,
                                       void *activations,
                                       cudaStream_t stream) {
  MinGRUWeights *m = (MinGRUWeights *)w;
  MinGRUActivations *a = (MinGRUActivations *)activations;
  MetalStream *ms = mtl_resolve_stream(stream);

  for (int i = 0; i < m->num_layers; i++) {
    puf_copy(a->saved_inputs[i], x, stream);
    PufTensor state_i = mingru_state_layer(m, state, i);
    puf_mm(x, m->weights[i], a->combined_bufs[i], stream);
    // Layer-local dependency: scan reads matmul output from this layer.
    mtl_barrier(ms);
    a->scan_bufs[i].combined_ptr = a->combined_bufs[i].bytes;
    a->scan_bufs[i].state_ptr = state_i.bytes;
    a->scan_bufs[i].input_ptr = a->saved_inputs[i].bytes;
    // Dispatch fp16 or fp32 scan based on activation dtype
    if (a->combined_bufs[i].dtype_size == 2)
      mtl_fused_scan_forward_fp16(a->scan_bufs[i], stream);
    else
      mtl_fused_scan_forward(a->scan_bufs[i], stream);
    // barrier: next layer or decoder consumes this layer's scan output.
    mtl_barrier(ms);
    x = a->scan_bufs[i].out;
  }
  return x;
}

static PufTensor mingru_backward(void *w, PufTensor grad, void *activations,
                                  cudaStream_t stream) {
  MinGRUWeights *m = (MinGRUWeights *)w;
  MinGRUActivations *a = (MinGRUActivations *)activations;
  MetalStream *ms = mtl_resolve_stream(stream);

  // grad_next_state must be zero — no downstream consumer of the final
  // hidden state during training (state is reset each minibatch).
  // without this, stale values from the previous backward pass inject
  // garbage gradients at the last timestep of the scan.
  puf_zero(a->grad_next_state, stream);
  mtl_barrier(ms);

  for (int i = m->num_layers - 1; i >= 0; i--) {
    PrefixScan &scan = a->scan_bufs[i];
    // Dispatch fp16 or fp32 scan backward based on activation dtype
    if (grad.dtype_size == 2)
      mtl_fused_scan_backward_fp16(scan, grad.bytes,
                                   a->grad_next_state.bytes, stream);
    else
      mtl_fused_scan_backward(scan, (const float *)grad.bytes,
                              (const float *)a->grad_next_state.bytes, stream);
    // scan.grad_combined is consumed by the GEMMs below.
    mtl_barrier(ms);
    puf_mm_tn(scan.grad_combined, a->saved_inputs[i], a->wgrad_scratch[i],
              stream);
    puf_mm_nn(scan.grad_combined, m->weights[i], a->grad_input_buf, stream);
    mtl_barrier(ms);  // mm_nn writes grad_input_buf, puf_add reads+writes it
    puf_add(a->grad_input_buf, scan.grad_input, stream);
    // barrier: next layer or encoder_backward consumes grad_input_buf.
    mtl_barrier(ms);
    grad = a->grad_input_buf;
  }
  return grad;
}

// ============================================================================
// Reset lazy-init scratch buffers between trials
// ============================================================================

// Called from mtl_destroy() to prevent stale pointers surviving into a new
// Metal context (g_ctx.buffers is cleared but these statics would persist,
// causing mtl_set_ptr assertion failures on the second trial).
void mtl_kernels_reset() {
  if (norm_partials_buf) {
    mtl_unwrap_ptr(norm_partials_buf);
    free(norm_partials_buf);
    norm_partials_buf = nullptr;
  }
  if (ppo_partials_buf) {
    mtl_unwrap_ptr(ppo_partials_buf);
    free(ppo_partials_buf);
    ppo_partials_buf = nullptr;
    ppo_partials_capacity = 0;
  }
  if (ppo_act_f32) {
    mtl_unwrap_ptr(ppo_act_f32);
    free(ppo_act_f32);
    ppo_act_f32 = nullptr;
    ppo_act_f32_capacity = 0;
  }
}
