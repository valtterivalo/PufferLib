/**
 * @fileoverview Metal backend infrastructure for PufferLib static-native.
 *
 * REQUIRES Metal 4 (macOS 15+, Apple Silicon M3+). Uses reusable command
 * buffers, argument tables, residency sets, and shared events. No Metal 3
 * transient command buffer fallback — this code assumes Metal 4 is available.
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
#include <atomic>
#include <vector>

// ============================================================================
// MetalStream — command buffer + encoder lifecycle.
// Passed as cudaStream_t (void*) through PufferLib vtable function pointers.
// ============================================================================

struct MetalStream {
  id<MTL4CommandAllocator> allocator;
  id<MTL4CommandBuffer> cmd;
  id<MTL4ComputeCommandEncoder> enc;
  id<MTL4ArgumentTable> arg_table;
  id<MTLSharedEvent> sync_event;       // per-stream CPU/GPU synchronization event
  id<MTLBuffer> const_ring;            // per-stream ring buffer for inline constants
  NSUInteger const_ring_offset = 0;    // current write position in ring

  // Argument table binding cache — skip redundant setAddress calls
  uint64_t bound_addresses[32] = {};

  bool enc_active = false;
  bool pending_work = false; // true when compute work is encoded but not synced
  bool flushed = false;      // true when cmd committed but not waited on
  CFTimeInterval commit_time = 0; // host time at commit (for sched_wait diagnostic)
  uint64_t flush_event_val = 0;   // sync_event value saved by flush(), waited on by wait_completed()
  uint64_t sync_event_value = 0;  // monotonically increasing per-stream signal counter

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

  // Commit current work and immediately begin a fresh command buffer.
  // Used to insert scheduling yield points inside long async training loops.
  void commit_chunk();

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
// Must hold all params pushed between begin() and sync()/flush().
// High replay_ratio (e.g. 2-4x) encodes 32+ minibatches per command buffer,
// each with ~2-3KB of params (forward + PPO + backward + optimizer GEMMs).
static const NSUInteger MTL_CONST_RING_SIZE = 2 * 1024 * 1024;

struct MetalContext {
  id<MTLDevice> device;
  id<MTLLibrary> library; // JIT-compiled MSL shaders
  NSMutableDictionary<NSString *, id<MTLComputePipelineState>> *pipelines;

  // tensor_ops GEMM pipelines (all layouts, f32 + f16)
  id<MTLComputePipelineState> tensor_ops_gemm_nt_f32;
  id<MTLComputePipelineState> tensor_ops_gemm_nn_f32;
  id<MTLComputePipelineState> tensor_ops_gemm_tn_f32;
  id<MTLComputePipelineState> tensor_ops_gemm_nt_f16;
  id<MTLComputePipelineState> tensor_ops_gemm_nn_f16;
  id<MTLComputePipelineState> tensor_ops_gemm_tn_f16;

  // Metal 4 reusable command buffer infrastructure
  id<MTL4CommandQueue> queue;        // rollout queue
  id<MTL4CommandQueue> train_queue;  // training queue (async overlap)
  id<MTLResidencySet> residency_set;  // all wrapped buffers for GPU address access

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

// Stream resolution: returns the typed MetalStream* for a cudaStream_t handle,
// falling back to the default stream when s is null.
static inline MetalStream *mtl_resolve_stream(cudaStream_t s) {
  return s ? (MetalStream *)s : (MetalStream *)mtl_stream();
}

// Ensure all pending GPU work on a stream is committed and synced to CPU.
// Checks both enc_active (encoder needs ending) and pending_work (command
// buffer needs committing). Both must be checked — the encoder can be ended
// but the command buffer not yet committed.
static inline void mtl_ensure_stream_synced(cudaStream_t s) {
  MetalStream *ms = mtl_resolve_stream(s);
  if (ms->enc_active || ms->pending_work) ms->sync();
}

// Create/destroy additional rollout streams (one per vecenv buffer thread).
void *mtl_create_stream();
void mtl_destroy_stream(void *stream);

// Allocate page-aligned scratch memory and register as a shared MTLBuffer
// in the global residency set. Returns the CPU pointer (also GPU-accessible
// on Apple Silicon unified memory).
static inline void *mtl_alloc_scratch(int64_t bytes) {
  int64_t page = 16384;
  int64_t alloc = ((bytes + page - 1) / page) * page;
  void *ptr = nullptr;
  posix_memalign(&ptr, page, alloc);
  assert(ptr && "mtl_alloc_scratch: posix_memalign failed");
  memset(ptr, 0, alloc);
  MetalContext *ctx = mtl_ctx();
  id<MTLBuffer> buf = [ctx->device newBufferWithBytesNoCopy:ptr
                                                     length:(NSUInteger)alloc
                                                    options:MTLResourceStorageModeShared
                                                deallocator:nil];
  assert(buf);
  ctx->buffers.push_back({(char *)ptr, alloc, buf});
  [ctx->residency_set addAllocation:buf];
  [ctx->residency_set commit];
  [ctx->residency_set requestResidency];
  return ptr;
}

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
// Inline dispatch helpers — Metal 4 argument table + reusable encoder.
// ============================================================================

// Set compute pipeline state on the active encoder.
inline void mtl_set_pso(MetalStream *ms, id<MTLComputePipelineState> pso) {
  [ms->enc setComputePipelineState:pso];
}

// Bind a PufTensor at the given buffer index (with binding cache).
inline void mtl_set_tensor(MetalStream *ms, const PufTensor &t,
                           uint32_t index) {
  NSUInteger offset;
  id<MTLBuffer> buf = mtl_buffer_for(t, &offset);
  uint64_t addr = buf.gpuAddress + offset;
  if (ms->bound_addresses[index] != addr) {
    [ms->arg_table setAddress:addr atIndex:index];
    ms->bound_addresses[index] = addr;
  }
}

// Bind constant data via ring buffer (replaces setBytes).
template <typename T>
inline void mtl_set_params(MetalStream *ms, const T &params, uint32_t index) {
  NSUInteger aligned = (sizeof(T) + 15) & ~15;
  assert(ms->const_ring_offset + aligned <= MTL_CONST_RING_SIZE);
  memcpy((char *)[ms->const_ring contents] + ms->const_ring_offset,
         &params, sizeof(T));
  uint64_t addr = ms->const_ring.gpuAddress + ms->const_ring_offset;
  [ms->arg_table setAddress:addr atIndex:index];
  ms->bound_addresses[index] = addr;
  ms->const_ring_offset += aligned;
}

// 1D dispatch with auto threadgroup sizing (capped at 256).
inline void mtl_dispatch_1d(MetalStream *ms, id<MTLComputePipelineState> pso,
                            int count) {
  NSUInteger tg = MIN((NSUInteger)pso.maxTotalThreadsPerThreadgroup, 256);
  [ms->enc dispatchThreads:MTLSizeMake(count, 1, 1)
      threadsPerThreadgroup:MTLSizeMake(tg, 1, 1)];
}

// Threadgroup dispatch (for kernels that use shared memory).
inline void mtl_dispatch_groups(MetalStream *ms,
                                id<MTLComputePipelineState> pso,
                                int num_groups, int group_size) {
  [ms->enc dispatchThreadgroups:MTLSizeMake(num_groups, 1, 1)
      threadsPerThreadgroup:MTLSizeMake(group_size, 1, 1)];
}

// Set threadgroup memory length on the active encoder.
inline void mtl_set_threadgroup_memory(MetalStream *ms, NSUInteger length,
                                       uint32_t index) {
  [ms->enc setThreadgroupMemoryLength:length atIndex:index];
}

// Memory barrier between dependent compute dispatches.
// On Metal 4, use explicit intrapass barrier for dispatch->dispatch visibility.
// Fallback to encoder boundary when the API isn't available.
inline void mtl_barrier(MetalStream *ms) {
  if (ms->enc_active) {
#if __OBJC__
    if (__builtin_available(macOS 15.0, *)) {
      [ms->enc barrierAfterEncoderStages:MTLStageDispatch
                      beforeEncoderStages:MTLStageDispatch
                        visibilityOptions:MTL4VisibilityOptionDevice];
      ms->pending_work = true;
      return;
    }
#endif
    ms->end_compute();
    ms->compute_encoder();
  }
}

// ============================================================================
// GEMM wrappers — tensor_ops (aligned) or steel_gemm (unaligned) on GPU.
// All GEMM dispatches go through Metal compute shaders (no CPU cblas).
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
void mtl_enable_gpu_timing(bool enable);
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
