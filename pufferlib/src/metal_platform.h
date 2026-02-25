/**
 * @fileoverview Metal backend infrastructure for PufferLib static-native.
 *
 * Provides device/queue management, MSL shader compilation with pipeline
 * caching, buffer wrapping for PufTensor memory (zero-copy on Apple Silicon
 * unified memory), command buffer lifecycle, and dispatch helpers.
 *
 * Only included from .mm files — uses Objective-C Metal framework types.
 */

#ifndef PUFFERLIB_METAL_PLATFORM_H
#define PUFFERLIB_METAL_PLATFORM_H

#import <Metal/Metal.h>
#include <mach/mach_time.h>

// Use the modern Accelerate BLAS/LAPACK headers (ILP32, not deprecated)
#define ACCELERATE_NEW_LAPACK
#import <Accelerate/Accelerate.h>

#include "puf_types.h"
#include <cassert>
#include <vector>

// ============================================================================
// MetalStream — command buffer + encoder lifecycle.
// Passed as cudaStream_t (void*) through PufferLib vtable function pointers.
// ============================================================================

struct MetalStream {
  id<MTLCommandBuffer> cmd;
  id<MTLComputeCommandEncoder> enc;
  id<MTLCommandQueue> queue;  // owning queue (for creating command buffers)
  bool enc_active = false;
  bool pending_work = false; // true when compute work is encoded but not synced
  bool flushed = false;      // true when cmd committed but not waited on

  // Create a fresh command buffer, ready for encoding.
  void begin();

  // Get the current compute encoder (creates one lazily).
  id<MTLComputeCommandEncoder> compute_encoder();

  // End the current compute encoder. Must call before sync.
  void end_compute();

  // Commit and wait for GPU completion, then begin a new command buffer.
  void sync();

  // Commit without waiting — GPU work continues asynchronously.
  // Call wait_completed() later to ensure it finished.
  void flush();

  // Wait for previously flushed command buffer to complete.
  // No-op if nothing was flushed.
  void wait_completed();
};

// ============================================================================
// WrappedBuffer — maps Allocator base pointer to MTLBuffer for GPU access.
// ============================================================================

struct WrappedBuffer {
  char *base;
  int64_t size;
  id<MTLBuffer> buffer;
};

// ============================================================================
// MetalContext — singleton managing device, queue, shaders, pipelines.
// ============================================================================

struct MetalContext {
  id<MTLDevice> device;
  id<MTLCommandQueue> queue;
  id<MTLCommandQueue> train_queue;  // separate queue for async training overlap
  id<MTLLibrary> library; // JIT-compiled MSL shaders
  NSMutableDictionary<NSString *, id<MTLComputePipelineState>> *pipelines;

  // Metal 4 tensor_ops GEMM — separate library (different MSL includes).
  // Stays on the compute encoder (no MPS encoder transitions).
  id<MTLComputePipelineState> tensor_ops_gemm_nt_f32;

  MetalStream stream;       // default stream (rollout)
  MetalStream train_stream; // training stream (separate queue for overlap)
  std::vector<WrappedBuffer> buffers;
};

// ============================================================================
// Lifecycle
// ============================================================================

// Initialize Metal context: device, queue, compile MSL shaders.
void mtl_init();

// Global context accessor.
MetalContext *mtl_ctx();

// Default stream as void* (cast to cudaStream_t for vtable functions).
void *mtl_stream();

// Training stream (separate command queue for async overlap with rollout).
void *mtl_train_stream();

// Tear down Metal context.
void mtl_destroy();

// ============================================================================
// Buffer management — zero-copy wrapping of Allocator memory
// ============================================================================

// Wrap an Allocator's memory as MTLBuffer for GPU access.
// Allocator::mem must be page-aligned (WITH_METAL path in puf_types.h).
// Returns the MTLBuffer (also tracked internally for lookup).
id<MTLBuffer> mtl_wrap_allocator(Allocator *alloc);

// Find the MTLBuffer containing a PufTensor and compute its byte offset.
// Linear search over wrapped allocators (typically 3-6, negligible cost).
id<MTLBuffer> mtl_buffer_for(const PufTensor &t, NSUInteger *out_offset);

// ============================================================================
// Pipeline cache
// ============================================================================

// Get or create a compute pipeline for the named MSL kernel function.
// First call compiles and caches; subsequent calls return cached PSO.
id<MTLComputePipelineState> mtl_pipeline(const char *name);

// ============================================================================
// Inline dispatch helpers
// ============================================================================

// Bind a PufTensor to the compute encoder at the given buffer index.
inline void mtl_set_tensor(id<MTLComputeCommandEncoder> enc,
                           const PufTensor &t, uint32_t index) {
  NSUInteger offset;
  id<MTLBuffer> buf = mtl_buffer_for(t, &offset);
  [enc setBuffer:buf offset:offset atIndex:index];
}

// Bind constant data directly into the encoder's argument table.
template <typename T>
inline void mtl_set_params(id<MTLComputeCommandEncoder> enc, const T &params,
                           uint32_t index) {
  [enc setBytes:&params length:sizeof(T) atIndex:index];
}

// 1D dispatch with auto threadgroup sizing (capped at 256).
inline void mtl_dispatch_1d(id<MTLComputeCommandEncoder> enc,
                            id<MTLComputePipelineState> pso, int count) {
  NSUInteger tg = MIN((NSUInteger)pso.maxTotalThreadsPerThreadgroup, 256);
  [enc dispatchThreads:MTLSizeMake(count, 1, 1)
      threadsPerThreadgroup:MTLSizeMake(tg, 1, 1)];
}

// Threadgroup dispatch (for kernels that use shared memory).
inline void mtl_dispatch_groups(id<MTLComputeCommandEncoder> enc,
                                id<MTLComputePipelineState> pso,
                                int num_groups, int group_size) {
  [enc dispatchThreadgroups:MTLSizeMake(num_groups, 1, 1)
      threadsPerThreadgroup:MTLSizeMake(group_size, 1, 1)];
}

// ============================================================================
// GEMM wrappers — cblas_sgemm (CPU, default) or MSL tiled kernel (GPU).
// Set PUFFERLIB_GPU_GEMM=1 to force GPU path (for benchmarking).
//
// These match the cuBLAS GEMM conventions in models.cu:
//   puf_mm:    out = a @ b^T   (OP_T, OP_N in cuBLAS column-major)
//   puf_mm_tn: out = a^T @ b   (OP_N, OP_T in cuBLAS column-major)
//   puf_mm_nn: out = a @ b     (OP_N, OP_N in cuBLAS column-major)
// ============================================================================

// out(...,N) = a(...,K) @ b(N,K)^T
void puf_mm(PufTensor &a, PufTensor &b, PufTensor &out, cudaStream_t stream);

// out(M,N) = a(...,M)^T @ b(...,N)
void puf_mm_tn(PufTensor &a, PufTensor &b, PufTensor &out,
               cudaStream_t stream);

// out(...,N) = a(...,K) @ b(K,N)
void puf_mm_nn(PufTensor &a, PufTensor &b, PufTensor &out,
               cudaStream_t stream);

// out(...,N) = beta*out + alpha * a(...,K) @ b(K,N)
void puf_addmm_nn(PufTensor &a, PufTensor &b, PufTensor &out, float alpha,
                   float beta, cudaStream_t stream);

// ============================================================================
// Sync profiling — count and time every MetalStream::sync() call
// ============================================================================

// Read and reset sync stats since last call.
void mtl_sync_stats(int *out_count, double *out_total_ms);

// Read and reset GEMM dispatch counts.
void mtl_gemm_stats(int *tensor_ops_count, int *mps_count);

// CPU inference mode — when true, mingru_forward uses CPU gate + memcpy
// instead of Metal dispatch, eliminating rollout syncs.
void puf_set_cpu_inference(bool val);
bool puf_is_cpu_inference();

// Check if a stream has an active compute encoder (work pending on GPU).
// Used by puf_copy to avoid flushing the encoder chain during rollout.
bool puf_stream_has_encoder(cudaStream_t stream);

// GPU training mode — when true, puf_mm/puf_copy/puf_zero use GPU
// instead of sync+CPU, keeping everything on the compute encoder chain.
void puf_set_gpu_training(bool val);
bool puf_is_gpu_training();

// CPU kernels for sync-free rollout inference
void cpu_cast_u8_to_f32(float *dst, const uint8_t *src, int count);
void cpu_sample_logits(const float *dec_out, int fused_cols, int B,
                       const int32_t *act_sizes, int num_atns,
                       double *actions, float *logprobs, float *value_out,
                       uint64_t seed, uint32_t *offset_ptr);

// ============================================================================
// fp16 cast dispatchers — GPU kernel dispatch for f32↔f16 conversion
// ============================================================================

void mtl_cast_f32_to_f16(void *dst, const float *src, int count,
                          cudaStream_t stream);
void mtl_cast_f16_to_f32(float *dst, const void *src, int count,
                          cudaStream_t stream);

// ============================================================================
// LAPACK via Accelerate — symmetric eigendecomposition for Muon optimizer
// ============================================================================

// Symmetric eigendecomposition (divide-and-conquer).
// A is (N,N) float, overwritten with eigenvectors on output.
// eigenvalues is (N,) float output.
void mtl_syevd(float *A, float *eigenvalues, int N);

#endif // PUFFERLIB_METAL_PLATFORM_H
