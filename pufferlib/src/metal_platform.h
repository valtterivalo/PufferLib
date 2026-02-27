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
  // Metal 3 (transient command buffers)
  id<MTLCommandBuffer> cmd;
  id<MTLComputeCommandEncoder> enc;
  id<MTLCommandQueue> queue;  // owning queue (for creating command buffers)

  // Metal 4 (reusable command buffers + argument tables)
  id<MTL4CommandAllocator> allocator;
  id<MTL4CommandBuffer> cmd4;
  id<MTL4ComputeCommandEncoder> enc4;
  id<MTL4ArgumentTable> arg_table;
  id<MTLBuffer> const_ring;            // per-stream ring buffer for inline constants
  NSUInteger const_ring_offset = 0;    // current write position in ring

  bool enc_active = false;
  bool pending_work = false; // true when compute work is encoded but not synced
  bool flushed = false;      // true when cmd committed but not waited on
  CFTimeInterval commit_time = 0; // host time at commit (for sched_wait diagnostic)

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

// Constants ring buffer size for Metal 4 setBytes replacement.
static const NSUInteger MTL_CONST_RING_SIZE = 64 * 1024;

struct MetalContext {
  id<MTLDevice> device;
  id<MTLCommandQueue> queue;
  id<MTLCommandQueue> train_queue;  // separate queue for async training overlap
  id<MTLLibrary> library; // JIT-compiled MSL shaders
  NSMutableDictionary<NSString *, id<MTLComputePipelineState>> *pipelines;

  // Metal 4 tensor_ops GEMM — separate library (different MSL includes).
  // Metal 4 tensor_ops GEMM pipelines (all layouts, f32 + f16).
  id<MTLComputePipelineState> tensor_ops_gemm_nt_f32;
  id<MTLComputePipelineState> tensor_ops_gemm_nn_f32;
  id<MTLComputePipelineState> tensor_ops_gemm_tn_f32;
  id<MTLComputePipelineState> tensor_ops_gemm_nt_f16;
  id<MTLComputePipelineState> tensor_ops_gemm_nn_f16;
  id<MTLComputePipelineState> tensor_ops_gemm_tn_f16;

  // Metal 4 reusable command buffer infrastructure
  bool has_metal4 = false;
  id<MTL4CommandQueue> queue4;        // Metal 4 rollout queue
  id<MTL4CommandQueue> train_queue4;  // Metal 4 training queue
  id<MTLResidencySet> residency_set;  // all wrapped buffers for GPU address access
  id<MTLSharedEvent> sync_event;      // CPU-GPU synchronization
  uint64_t sync_event_value = 0;      // monotonically increasing signal counter

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

// Reset lazy-init kernel scratch buffers (called by mtl_destroy).
void mtl_kernels_reset();

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
// Inline dispatch helpers — unified Metal 3 / Metal 4 interface.
// All take MetalStream* to support both encoder types transparently.
// ============================================================================

// Set compute pipeline state on the active encoder.
inline void mtl_set_pso(MetalStream *ms, id<MTLComputePipelineState> pso) {
  if (mtl_ctx()->has_metal4) {
    [ms->enc4 setComputePipelineState:pso];
  } else {
    [ms->enc setComputePipelineState:pso];
  }
}

// Bind a PufTensor at the given buffer index.
inline void mtl_set_tensor(MetalStream *ms, const PufTensor &t,
                           uint32_t index) {
  NSUInteger offset;
  id<MTLBuffer> buf = mtl_buffer_for(t, &offset);
  if (mtl_ctx()->has_metal4) {
    [ms->arg_table setAddress:(buf.gpuAddress + offset) atIndex:index];
  } else {
    [ms->enc setBuffer:buf offset:offset atIndex:index];
  }
}

// Bind constant data (replaces setBytes — uses ring buffer on Metal 4).
template <typename T>
inline void mtl_set_params(MetalStream *ms, const T &params, uint32_t index) {
  if (mtl_ctx()->has_metal4) {
    NSUInteger aligned = (sizeof(T) + 15) & ~15;
    assert(ms->const_ring_offset + aligned <= MTL_CONST_RING_SIZE);
    memcpy((char *)[ms->const_ring contents] + ms->const_ring_offset,
           &params, sizeof(T));
    [ms->arg_table
        setAddress:(ms->const_ring.gpuAddress + ms->const_ring_offset)
           atIndex:index];
    ms->const_ring_offset += aligned;
  } else {
    [ms->enc setBytes:&params length:sizeof(T) atIndex:index];
  }
}

// 1D dispatch with auto threadgroup sizing (capped at 256).
inline void mtl_dispatch_1d(MetalStream *ms, id<MTLComputePipelineState> pso,
                            int count) {
  NSUInteger tg = MIN((NSUInteger)pso.maxTotalThreadsPerThreadgroup, 256);
  if (mtl_ctx()->has_metal4) {
    [ms->enc4 dispatchThreads:MTLSizeMake(count, 1, 1)
        threadsPerThreadgroup:MTLSizeMake(tg, 1, 1)];
  } else {
    [ms->enc dispatchThreads:MTLSizeMake(count, 1, 1)
        threadsPerThreadgroup:MTLSizeMake(tg, 1, 1)];
  }
}

// Threadgroup dispatch (for kernels that use shared memory).
inline void mtl_dispatch_groups(MetalStream *ms,
                                id<MTLComputePipelineState> pso,
                                int num_groups, int group_size) {
  if (mtl_ctx()->has_metal4) {
    [ms->enc4 dispatchThreadgroups:MTLSizeMake(num_groups, 1, 1)
        threadsPerThreadgroup:MTLSizeMake(group_size, 1, 1)];
  } else {
    [ms->enc dispatchThreadgroups:MTLSizeMake(num_groups, 1, 1)
        threadsPerThreadgroup:MTLSizeMake(group_size, 1, 1)];
  }
}

// Set threadgroup memory length on the active encoder.
inline void mtl_set_threadgroup_memory(MetalStream *ms, NSUInteger length,
                                       uint32_t index) {
  if (mtl_ctx()->has_metal4) {
    [ms->enc4 setThreadgroupMemoryLength:length atIndex:index];
  } else {
    [ms->enc setThreadgroupMemoryLength:length atIndex:index];
  }
}

// ============================================================================
// GEMM wrappers — tensor_ops (aligned) or steel_gemm (unaligned) on GPU.
// CPU cblas_sgemm path used only during rollout (non-training mode).
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

// Read and reset GPU timing diagnostic (actual kernel time vs scheduling delay).
void mtl_gpu_timing_stats(double *gpu_exec_ms, double *sched_wait_ms);

// Read and reset GEMM dispatch counts.
void mtl_gemm_stats(int *tensor_ops_count, int *mps_count);

// Check if a stream has an active compute encoder (work pending on GPU).
// Used by puf_copy to avoid flushing the encoder chain during rollout.
bool puf_stream_has_encoder(cudaStream_t stream);

// GPU training mode — when true, puf_mm/puf_copy/puf_zero use GPU
// instead of sync+CPU, keeping everything on the compute encoder chain.
void puf_set_gpu_training(bool val);
bool puf_is_gpu_training();

// CPU u8→f32 cast for observation encoding (NEON-vectorized).
void cpu_cast_u8_to_f32(float *dst, const uint8_t *src, int count);

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
