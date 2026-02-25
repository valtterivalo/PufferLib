/**
 * @fileoverview Metal platform implementation — device init, shader
 * compilation, buffer wrapping, GEMM (Accelerate cblas / MSL tiled kernel),
 * LAPACK (Accelerate).
 *
 * All GPU memory is allocated via page-aligned posix_memalign (see
 * WITH_METAL path in puf_types.h Allocator::create) and wrapped as
 * MTLBuffer with StorageModeShared for zero-copy GPU access on Apple
 * Silicon unified memory.
 */

#import "metal_platform.h"
#include "metal_shader_src.h"

#include <cassert>
#include <cstdio>
#include <cstring>

// ============================================================================
// Global singleton
// ============================================================================

static MetalContext g_ctx = {};

// ============================================================================
// MetalStream implementation
// ============================================================================

void MetalStream::begin() {
  enc = nil;
  enc_active = false;
  pending_work = false;
  cmd = [g_ctx.queue commandBuffer];
}

id<MTLComputeCommandEncoder> MetalStream::compute_encoder() {
  if (!enc_active) {
    enc = [cmd computeCommandEncoder];
    enc_active = true;
    pending_work = true;
  }
  return enc;
}

void MetalStream::end_compute() {
  if (enc_active) {
    [enc endEncoding];
    enc = nil;
    enc_active = false;
  }
}

// Sync profiling globals
static int g_sync_count = 0;
static double g_sync_total_ns = 0.0;
static mach_timebase_info_data_t g_timebase = {0, 0};

static double mach_to_ns(uint64_t ticks) {
  if (g_timebase.denom == 0) mach_timebase_info(&g_timebase);
  return (double)ticks * g_timebase.numer / g_timebase.denom;
}

void MetalStream::sync() {
  end_compute();
  uint64_t t0 = mach_absolute_time();
  [cmd commit];
  [cmd waitUntilCompleted];
  uint64_t t1 = mach_absolute_time();
  g_sync_count++;
  g_sync_total_ns += mach_to_ns(t1 - t0);
  begin();
}

void mtl_sync_stats(int *out_count, double *out_total_ms) {
  *out_count = g_sync_count;
  *out_total_ms = g_sync_total_ns / 1e6;
  g_sync_count = 0;
  g_sync_total_ns = 0.0;
}

// ============================================================================
// Lifecycle
// ============================================================================

void mtl_init() {
  @autoreleasepool {
    g_ctx.device = MTLCreateSystemDefaultDevice();
    assert(g_ctx.device && "No Metal device found");

    g_ctx.queue = [g_ctx.device newCommandQueue];

    // JIT-compile all MSL shaders from the embedded source string
    NSError *error = nil;
    NSString *src =
        [NSString stringWithUTF8String:get_all_metal_shader_source()];
    MTLCompileOptions *opts = [[MTLCompileOptions alloc] init];
    opts.mathMode = MTLMathModeFast;
    g_ctx.library =
        [g_ctx.device newLibraryWithSource:src options:opts error:&error];
    if (!g_ctx.library) {
      NSLog(@"Metal shader compilation failed: %@", error);
      assert(false && "MSL compilation failed");
    }

    g_ctx.pipelines = [NSMutableDictionary new];

    // Start the default stream
    g_ctx.stream.begin();

    printf("[metal] device: %s, unified memory: %s\n",
           g_ctx.device.name.UTF8String,
           g_ctx.device.hasUnifiedMemory ? "yes" : "no");
  }
}

MetalContext *mtl_ctx() { return &g_ctx; }

void *mtl_stream() { return &g_ctx.stream; }

void mtl_destroy() {
  g_ctx.stream.end_compute();
  g_ctx.stream.cmd = nil;
  g_ctx.buffers.clear();
  g_ctx.pipelines = nil;
  g_ctx.library = nil;
  g_ctx.queue = nil;
  g_ctx.device = nil;
}

// ============================================================================
// Buffer management
// ============================================================================

id<MTLBuffer> mtl_wrap_allocator(Allocator *alloc) {
  assert(alloc->mem && "Allocator::create() not called");

  // Find the highest byte offset used by any registered tensor
  int64_t max_end = 0;
  for (auto *t : alloc->regs) {
    int64_t end =
        (t->bytes - (char *)alloc->mem) + t->numel() * t->dtype_size;
    if (end > max_end)
      max_end = end;
  }

  // Round up to ARM64 page boundary (16KB)
  int64_t page = 16384;
  int64_t size = (max_end + page - 1) & ~(page - 1);

  // Zero-copy wrap: StorageModeShared on Apple Silicon means CPU and GPU
  // access the same physical memory pages. No deallocator — Allocator
  // owns the memory and frees it in destroy().
  id<MTLBuffer> buf =
      [g_ctx.device newBufferWithBytesNoCopy:alloc->mem
                                      length:size
                                     options:MTLResourceStorageModeShared
                                 deallocator:nil];
  assert(buf && "newBufferWithBytesNoCopy failed — is Allocator::mem "
                "page-aligned? (requires WITH_METAL)");

  g_ctx.buffers.push_back({(char *)alloc->mem, size, buf});
  return buf;
}

id<MTLBuffer> mtl_buffer_for(const PufTensor &t, NSUInteger *out_offset) {
  for (auto &wb : g_ctx.buffers) {
    if (t.bytes >= wb.base && t.bytes < wb.base + wb.size) {
      *out_offset = (NSUInteger)(t.bytes - wb.base);
      return wb.buffer;
    }
  }
  assert(false && "PufTensor not in any wrapped allocator buffer");
  __builtin_unreachable();
}

// ============================================================================
// Pipeline cache
// ============================================================================

id<MTLComputePipelineState> mtl_pipeline(const char *name) {
  NSString *key = [NSString stringWithUTF8String:name];
  id<MTLComputePipelineState> pso = g_ctx.pipelines[key];
  if (pso)
    return pso;

  id<MTLFunction> fn = [g_ctx.library newFunctionWithName:key];
  assert(fn && "Kernel function not found in MSL library");

  NSError *error = nil;
  pso = [g_ctx.device newComputePipelineStateWithFunction:fn error:&error];
  if (!pso) {
    NSLog(@"Pipeline creation failed for '%@': %@", key, error);
    assert(false && "Pipeline creation failed");
  }

  g_ctx.pipelines[key] = pso;
  return pso;
}

// ============================================================================
// GEMM — MSL tiled kernel (GPU) or Accelerate cblas (CPU fallback)
//
// These match the cuBLAS calling conventions in models.cu. cuBLAS operates
// in column-major but PufferLib stores data row-major. The cuBLAS calls
// use the standard "flip everything" trick.
//
// Default: cblas_sgemm via Accelerate (AMX coprocessor) — fastest for small
// matrices (hidden_size ≤ 512). Set PUFFERLIB_GPU_GEMM=1 to use the MSL
// tiled kernel instead (useful for large hidden sizes or benchmarking).
// ============================================================================

static inline MetalStream *get_stream(cudaStream_t s) {
  return s ? (MetalStream *)s : &g_ctx.stream;
}

static inline void ensure_gpu_synced(cudaStream_t s) {
  MetalStream *ms = get_stream(s);
  if (ms->enc_active || ms->pending_work)
    ms->sync();
}

// Runtime flag: PUFFERLIB_GPU_GEMM=1 forces MSL tiled kernel instead of cblas.
// Default is cblas (Accelerate AMX) — faster for small matrices (hidden ≤ 512).
static int g_use_gpu_gemm = -1; // -1 = uninitialized
static bool use_gpu_gemm() {
  if (g_use_gpu_gemm < 0) {
    const char *env = getenv("PUFFERLIB_GPU_GEMM");
    g_use_gpu_gemm = (env && env[0] == '1') ? 1 : 0;
  }
  return g_use_gpu_gemm != 0;
}

// GPU training mode — when true, puf_mm forces GPU GEMM to avoid ensure_gpu_synced.
// Set by train_impl to keep all training ops on the GPU encoder chain.
static bool g_gpu_training = false;
void puf_set_gpu_training(bool val) { g_gpu_training = val; }
bool puf_is_gpu_training() { return g_gpu_training; }

// Find MTLBuffer containing a raw pointer and compute byte offset.
static id<MTLBuffer> buffer_for_ptr(const void *ptr, NSUInteger *out_offset) {
  for (auto &wb : g_ctx.buffers) {
    if ((const char *)ptr >= wb.base &&
        (const char *)ptr < wb.base + wb.size) {
      *out_offset = (NSUInteger)((const char *)ptr - wb.base);
      return wb.buffer;
    }
  }
  assert(false && "Pointer not in any wrapped allocator buffer");
  __builtin_unreachable();
}

// GPU GEMM: C = alpha * op(A) @ op(B) + beta * C
// Uses custom MSL tiled kernel on the compute encoder — no MPS/compute
// interop overhead, no per-GEMM sync needed.
//
// physical_rows_A/cols_A describe the STORED layout of A (before transpose).
// Same for B. The kernel indexes into the stored layout using trans_a/trans_b.
//
// Auto-detects tall-K shapes (few M/N tiles, large K) and uses K-split GEMM
// to increase GPU occupancy. Threshold: < 128 spatial TGs triggers K-split.
//
// fp16 variant (gpu_gemm_fp16): half inputs, float accumulation, half output.
// K-split path uses fp32 partials (same scratch buffer), reduce writes half.

static constexpr int GEMM_TILE_SIZE = 32;
static constexpr int KSPLIT_TG_TARGET = 128;

// Persistent scratch buffer for K-split partial sums.
static float *g_ksplit_buf = nullptr;
static int64_t g_ksplit_capacity = 0;

static void ensure_ksplit_buf(int64_t floats_needed) {
  if (g_ksplit_buf && floats_needed <= g_ksplit_capacity) return;
  if (g_ksplit_buf) {
    // Remove old buffer from wrapped list (it's the last one we added)
    for (auto it = g_ctx.buffers.begin(); it != g_ctx.buffers.end(); ++it) {
      if (it->base == (char *)g_ksplit_buf) {
        g_ctx.buffers.erase(it);
        break;
      }
    }
    free(g_ksplit_buf);
  }
  g_ksplit_capacity = floats_needed;
  int64_t bytes = g_ksplit_capacity * (int64_t)sizeof(float);
  int64_t page = 16384;
  bytes = (bytes + page - 1) & ~(page - 1);
  posix_memalign((void **)&g_ksplit_buf, page, bytes);
  id<MTLBuffer> buf =
      [g_ctx.device newBufferWithBytesNoCopy:g_ksplit_buf
                                      length:bytes
                                     options:MTLResourceStorageModeShared
                                 deallocator:nil];
  assert(buf);
  g_ctx.buffers.push_back({(char *)g_ksplit_buf, bytes, buf});
}

static void gpu_gemm(const float *A, int physical_rows_A, int physical_cols_A,
                      bool transpose_A,
                      const float *B, int physical_rows_B, int physical_cols_B,
                      bool transpose_B,
                      float *C, int result_rows, int result_cols,
                      int interior_cols,
                      float alpha, float beta,
                      cudaStream_t stream) {
  int M = result_rows, N = result_cols, K = interior_cols;
  int tiles_m = (M + GEMM_TILE_SIZE - 1) / GEMM_TILE_SIZE;
  int tiles_n = (N + GEMM_TILE_SIZE - 1) / GEMM_TILE_SIZE;
  int spatial_tgs = tiles_m * tiles_n;

  // K-split path for tall-K shapes (backward weight grads)
  if (spatial_tgs < KSPLIT_TG_TARGET && K > GEMM_TILE_SIZE) {
    int num_splits = (KSPLIT_TG_TARGET + spatial_tgs - 1) / spatial_tgs;
    // Don't split finer than BK=16
    int max_splits = (K + 15) / 16;
    if (num_splits > max_splits) num_splits = max_splits;
    int k_per_split = (K + num_splits - 1) / num_splits;

    ensure_ksplit_buf((int64_t)num_splits * M * N);

    MetalStream *ms = get_stream(stream);

    // Pass 1: K-split GEMM → partials
    {
      auto enc = ms->compute_encoder();
      auto pso = mtl_pipeline("sgemm_ksplit");
      [enc setComputePipelineState:pso];

      NSUInteger off_a, off_b, off_p;
      id<MTLBuffer> buf_a = buffer_for_ptr(A, &off_a);
      id<MTLBuffer> buf_b = buffer_for_ptr(B, &off_b);
      id<MTLBuffer> buf_p = buffer_for_ptr(g_ksplit_buf, &off_p);

      [enc setBuffer:buf_a offset:off_a atIndex:0];
      [enc setBuffer:buf_b offset:off_b atIndex:1];
      [enc setBuffer:buf_p offset:off_p atIndex:2];

      struct {
        int M, N, K;
        int lda, ldb, ldc;
        float alpha, beta;
        int trans_a, trans_b;
      } params = {
          M, N, K,
          physical_cols_A, physical_cols_B, N,
          1.0f, 0.0f,  // raw partials, alpha/beta applied in reduce
          transpose_A ? 1 : 0, transpose_B ? 1 : 0};
      [enc setBytes:&params length:sizeof(params) atIndex:3];
      [enc setBytes:&k_per_split length:sizeof(k_per_split) atIndex:4];

      MTLSize groups = MTLSizeMake(tiles_n, tiles_m, num_splits);
      MTLSize tg_size = MTLSizeMake(8, 8, 1);  // 8×8 = 64 threads, TM=TN=4
      [enc dispatchThreadgroups:groups threadsPerThreadgroup:tg_size];
    }

    // Pass 2: reduce partials → C
    {
      auto enc = ms->compute_encoder();
      auto pso = mtl_pipeline("reduce_ksplit");
      [enc setComputePipelineState:pso];

      NSUInteger off_p, off_c;
      id<MTLBuffer> buf_p = buffer_for_ptr(g_ksplit_buf, &off_p);
      id<MTLBuffer> buf_c = buffer_for_ptr(C, &off_c);

      [enc setBuffer:buf_p offset:off_p atIndex:0];
      [enc setBuffer:buf_c offset:off_c atIndex:1];

      struct {
        int MN;
        int num_splits;
        float alpha_val;
        float beta_val;
      } rparams = {M * N, num_splits, alpha, beta};
      [enc setBytes:&rparams length:sizeof(rparams) atIndex:2];

      // 1D dispatch over M*N elements
      int total = M * N;
      int threads_per_tg = (int)pso.maxTotalThreadsPerThreadgroup;
      if (threads_per_tg > 256) threads_per_tg = 256;
      int groups_needed = (total + threads_per_tg - 1) / threads_per_tg;
      [enc dispatchThreadgroups:MTLSizeMake(groups_needed, 1, 1)
          threadsPerThreadgroup:MTLSizeMake(threads_per_tg, 1, 1)];
    }

    return;
  }

  // Regular path: enough spatial TGs for good occupancy
  MetalStream *ms = get_stream(stream);
  auto enc = ms->compute_encoder();
  auto pso = mtl_pipeline("sgemm_reg");
  [enc setComputePipelineState:pso];

  NSUInteger off_a, off_b, off_c;
  id<MTLBuffer> buf_a = buffer_for_ptr(A, &off_a);
  id<MTLBuffer> buf_b = buffer_for_ptr(B, &off_b);
  id<MTLBuffer> buf_c = buffer_for_ptr(C, &off_c);

  [enc setBuffer:buf_a offset:off_a atIndex:0];
  [enc setBuffer:buf_b offset:off_b atIndex:1];
  [enc setBuffer:buf_c offset:off_c atIndex:2];

  struct {
    int M, N, K;
    int lda, ldb, ldc;
    float alpha, beta;
    int trans_a, trans_b;
  } params = {
      M, N, K,
      physical_cols_A, physical_cols_B, N,
      alpha, beta,
      transpose_A ? 1 : 0, transpose_B ? 1 : 0};
  [enc setBytes:&params length:sizeof(params) atIndex:3];

  MTLSize groups = MTLSizeMake(tiles_n, tiles_m, 1);
  MTLSize tg_size = MTLSizeMake(8, 8, 1);  // 8×8 = 64 threads, TM=TN=4
  [enc dispatchThreadgroups:groups threadsPerThreadgroup:tg_size];
}

// GPU GEMM fp16: half inputs, float accumulation, half output.
// Uses hgemm_reg / hgemm_ksplit + reduce_ksplit_fp16 kernels.
// K-split partials are fp32 (reuses g_ksplit_buf), reduce writes half.
static void gpu_gemm_fp16(const void *A, int physical_rows_A, int physical_cols_A,
                           bool transpose_A,
                           const void *B, int physical_rows_B, int physical_cols_B,
                           bool transpose_B,
                           void *C, int result_rows, int result_cols,
                           int interior_cols,
                           float alpha, float beta,
                           cudaStream_t stream) {
  int M = result_rows, N = result_cols, K = interior_cols;
  int tiles_m = (M + GEMM_TILE_SIZE - 1) / GEMM_TILE_SIZE;
  int tiles_n = (N + GEMM_TILE_SIZE - 1) / GEMM_TILE_SIZE;
  int spatial_tgs = tiles_m * tiles_n;

  // K-split path for tall-K shapes (backward weight grads)
  if (spatial_tgs < KSPLIT_TG_TARGET && K > GEMM_TILE_SIZE) {
    int num_splits = (KSPLIT_TG_TARGET + spatial_tgs - 1) / spatial_tgs;
    int max_splits = (K + 15) / 16;
    if (num_splits > max_splits) num_splits = max_splits;
    int k_per_split = (K + num_splits - 1) / num_splits;

    // K-split partials are fp32 (same scratch buffer as fp32 GEMM)
    ensure_ksplit_buf((int64_t)num_splits * M * N);

    MetalStream *ms = get_stream(stream);

    // Pass 1: K-split GEMM → fp32 partials
    {
      auto enc = ms->compute_encoder();
      auto pso = mtl_pipeline("hgemm_ksplit");
      [enc setComputePipelineState:pso];

      NSUInteger off_a, off_b, off_p;
      id<MTLBuffer> buf_a = buffer_for_ptr(A, &off_a);
      id<MTLBuffer> buf_b = buffer_for_ptr(B, &off_b);
      id<MTLBuffer> buf_p = buffer_for_ptr(g_ksplit_buf, &off_p);

      [enc setBuffer:buf_a offset:off_a atIndex:0];
      [enc setBuffer:buf_b offset:off_b atIndex:1];
      [enc setBuffer:buf_p offset:off_p atIndex:2];

      struct {
        int M, N, K;
        int lda, ldb, ldc;
        float alpha, beta;
        int trans_a, trans_b;
      } params = {
          M, N, K,
          physical_cols_A, physical_cols_B, N,
          1.0f, 0.0f,  // raw partials, alpha/beta applied in reduce
          transpose_A ? 1 : 0, transpose_B ? 1 : 0};
      [enc setBytes:&params length:sizeof(params) atIndex:3];
      [enc setBytes:&k_per_split length:sizeof(k_per_split) atIndex:4];

      MTLSize groups = MTLSizeMake(tiles_n, tiles_m, num_splits);
      MTLSize tg_size = MTLSizeMake(8, 8, 1);
      [enc dispatchThreadgroups:groups threadsPerThreadgroup:tg_size];
    }

    // Pass 2: reduce fp32 partials → half output
    {
      auto enc = ms->compute_encoder();
      auto pso = mtl_pipeline("reduce_ksplit_fp16");
      [enc setComputePipelineState:pso];

      NSUInteger off_p, off_c;
      id<MTLBuffer> buf_p = buffer_for_ptr(g_ksplit_buf, &off_p);
      id<MTLBuffer> buf_c = buffer_for_ptr(C, &off_c);

      [enc setBuffer:buf_p offset:off_p atIndex:0];
      [enc setBuffer:buf_c offset:off_c atIndex:1];

      struct {
        int MN;
        int num_splits;
        float alpha_val;
        float beta_val;
      } rparams = {M * N, num_splits, alpha, beta};
      [enc setBytes:&rparams length:sizeof(rparams) atIndex:2];

      int total = M * N;
      int threads_per_tg = (int)pso.maxTotalThreadsPerThreadgroup;
      if (threads_per_tg > 256) threads_per_tg = 256;
      int groups_needed = (total + threads_per_tg - 1) / threads_per_tg;
      [enc dispatchThreadgroups:MTLSizeMake(groups_needed, 1, 1)
          threadsPerThreadgroup:MTLSizeMake(threads_per_tg, 1, 1)];
    }

    return;
  }

  // Regular path: enough spatial TGs for good occupancy
  MetalStream *ms = get_stream(stream);
  auto enc = ms->compute_encoder();
  auto pso = mtl_pipeline("hgemm_reg");
  [enc setComputePipelineState:pso];

  NSUInteger off_a, off_b, off_c;
  id<MTLBuffer> buf_a = buffer_for_ptr(A, &off_a);
  id<MTLBuffer> buf_b = buffer_for_ptr(B, &off_b);
  id<MTLBuffer> buf_c = buffer_for_ptr(C, &off_c);

  [enc setBuffer:buf_a offset:off_a atIndex:0];
  [enc setBuffer:buf_b offset:off_b atIndex:1];
  [enc setBuffer:buf_c offset:off_c atIndex:2];

  struct {
    int M, N, K;
    int lda, ldb, ldc;
    float alpha, beta;
    int trans_a, trans_b;
  } params = {
      M, N, K,
      physical_cols_A, physical_cols_B, N,
      alpha, beta,
      transpose_A ? 1 : 0, transpose_B ? 1 : 0};
  [enc setBytes:&params length:sizeof(params) atIndex:3];

  MTLSize groups = MTLSizeMake(tiles_n, tiles_m, 1);
  MTLSize tg_size = MTLSizeMake(8, 8, 1);
  [enc dispatchThreadgroups:groups threadsPerThreadgroup:tg_size];
}

// out(...,N) = a(...,K) @ b(N,K)^T — leading dims folded into M
void puf_mm(PufTensor &a, PufTensor &b, PufTensor &out,
            cudaStream_t stream) {
  int na = a.ndim(), nb = b.ndim();
  int M = (int)(a.batch_size() * a.shape[na - 2]);
  int K = (int)a.shape[na - 1];
  int N = (int)b.shape[nb - 2];

  if (!use_gpu_gemm() && !g_gpu_training) {
    ensure_gpu_synced(stream);
    cblas_sgemm(CblasRowMajor, CblasNoTrans, CblasTrans, M, N, K, 1.0f,
                (const float *)a.bytes, K, (const float *)b.bytes, K,
                0.0f, (float *)out.bytes, N);
  } else if (a.dtype_size == 2) {
    gpu_gemm_fp16(a.bytes, M, K, false,
                  b.bytes, N, K, true,
                  out.bytes, M, N, K,
                  1.0f, 0.0f, stream);
  } else {
    gpu_gemm((const float *)a.bytes, M, K, false,
             (const float *)b.bytes, N, K, true,
             (float *)out.bytes, M, N, K,
             1.0f, 0.0f, stream);
  }
}

// out(M,N) = a(...,M)^T @ b(...,N) — leading dims folded into K
void puf_mm_tn(PufTensor &a, PufTensor &b, PufTensor &out,
               cudaStream_t stream) {
  int na = a.ndim(), nb = b.ndim();
  int K = (int)(a.batch_size() * a.shape[na - 2]);
  int M = (int)a.shape[na - 1];
  int N = (int)b.shape[nb - 1];

  if (!use_gpu_gemm() && !g_gpu_training) {
    ensure_gpu_synced(stream);
    cblas_sgemm(CblasRowMajor, CblasTrans, CblasNoTrans, M, N, K, 1.0f,
                (const float *)a.bytes, M, (const float *)b.bytes, N,
                0.0f, (float *)out.bytes, N);
  } else if (a.dtype_size == 2) {
    gpu_gemm_fp16(a.bytes, K, M, true,
                  b.bytes, K, N, false,
                  out.bytes, M, N, K,
                  1.0f, 0.0f, stream);
  } else {
    gpu_gemm((const float *)a.bytes, K, M, true,
             (const float *)b.bytes, K, N, false,
             (float *)out.bytes, M, N, K,
             1.0f, 0.0f, stream);
  }
}

// out(...,N) = a(...,K) @ b(K,N) — leading dims folded into M
void puf_mm_nn(PufTensor &a, PufTensor &b, PufTensor &out,
               cudaStream_t stream) {
  int na = a.ndim(), nb = b.ndim();
  int M = (int)(a.batch_size() * a.shape[na - 2]);
  int K = (int)a.shape[na - 1];
  int N = (int)b.shape[nb - 1];

  if (!use_gpu_gemm() && !g_gpu_training) {
    ensure_gpu_synced(stream);
    // vDSP_mmul: simpler dispatch than cblas_sgemm (no enum/scaling args).
    // C(M,N) = A(M,K) * B(K,N), all row-major stride-1.
    vDSP_mmul((const float *)a.bytes, 1, (const float *)b.bytes, 1,
              (float *)out.bytes, 1, M, N, K);
  } else if (a.dtype_size == 2) {
    gpu_gemm_fp16(a.bytes, M, K, false,
                  b.bytes, K, N, false,
                  out.bytes, M, N, K,
                  1.0f, 0.0f, stream);
  } else {
    gpu_gemm((const float *)a.bytes, M, K, false,
             (const float *)b.bytes, K, N, false,
             (float *)out.bytes, M, N, K,
             1.0f, 0.0f, stream);
  }
}

// out(...,N) = beta*out + alpha * a(...,K) @ b(K,N)
void puf_addmm_nn(PufTensor &a, PufTensor &b, PufTensor &out, float alpha,
                   float beta, cudaStream_t stream) {
  int na = a.ndim(), nb = b.ndim();
  int M = (int)(a.batch_size() * a.shape[na - 2]);
  int K = (int)a.shape[na - 1];
  int N = (int)b.shape[nb - 1];

  if (!use_gpu_gemm() && !g_gpu_training) {
    ensure_gpu_synced(stream);
    cblas_sgemm(CblasRowMajor, CblasNoTrans, CblasNoTrans, M, N, K, alpha,
                (const float *)a.bytes, K, (const float *)b.bytes, N,
                beta, (float *)out.bytes, N);
  } else {
    // addmm only used by Muon Newton-Schulz (always fp32)
    gpu_gemm((const float *)a.bytes, M, K, false,
             (const float *)b.bytes, K, N, false,
             (float *)out.bytes, M, N, K,
             alpha, beta, stream);
  }
}

// ============================================================================
// LAPACK via Accelerate — symmetric eigendecomposition (divide-and-conquer)
// Used by Muon optimizer's Newton-Schulz normalization.
// ============================================================================

// ============================================================================
// CUDA compatibility stubs for vecenv.h
//
// vecenv.h declares extern CUDA functions (cudaMemcpy, cudaMalloc, etc.) that
// the env's binding.c calls. On CUDA, libcudart provides these. On Metal,
// we provide trivial implementations:
// - cudaHostAlloc/cudaMalloc → calloc (unified memory, no separate GPU alloc)
// - cudaMemcpy/cudaMemcpyAsync → memcpy (same physical pages)
// - cudaMemset → memset
// - cudaStream* → no-op (single stream managed by MetalStream)
// - cudaDevice* → no-op
// ============================================================================

extern "C" {

int cudaHostAlloc(void **ptr, size_t size, unsigned int /*flags*/) {
  *ptr = calloc(1, size);
  return 0;
}

int cudaMalloc(void **ptr, size_t size) {
  *ptr = calloc(1, size);
  return 0;
}

int cudaMemcpy(void *dst, const void *src, size_t size, int /*kind*/) {
  memcpy(dst, src, size);
  return 0;
}

int cudaMemcpyAsync(void *dst, const void *src, size_t size, int /*kind*/,
                    void * /*stream*/) {
  memcpy(dst, src, size);
  return 0;
}

int cudaMemset(void *ptr, int value, size_t size) {
  memset(ptr, value, size);
  return 0;
}

int cudaFree(void *ptr) {
  free(ptr);
  return 0;
}

int cudaFreeHost(void *ptr) {
  free(ptr);
  return 0;
}

int cudaSetDevice(int /*device*/) { return 0; }

int cudaDeviceSynchronize(void) {
  if (g_ctx.stream.enc_active)
    g_ctx.stream.sync();
  return 0;
}

int cudaStreamSynchronize(void * /*stream*/) {
  // vecenv.h passes per-buffer streams, but Metal uses a single stream.
  // Sync the global stream to ensure all GPU work is visible to CPU.
  if (g_ctx.stream.enc_active)
    g_ctx.stream.sync();
  return 0;
}

int cudaStreamCreateWithFlags(void ** /*stream*/, unsigned int /*flags*/) {
  return 0;
}

int cudaStreamQuery(void * /*stream*/) { return 0; }

const char *cudaGetErrorString(int /*error*/) { return "metal-compat-stub"; }

} // extern "C"

// ============================================================================
// LAPACK via Accelerate — symmetric eigendecomposition (divide-and-conquer)
// Used by Muon optimizer's Newton-Schulz normalization.
// ============================================================================

void mtl_syevd(float *A, float *eigenvalues, int N) {
  char jobz = 'V'; // compute eigenvalues AND eigenvectors
  char uplo = 'U'; // upper triangle
  int n = N, lda = N;
  int lwork = -1, liwork = -1, info;
  float work_query;
  int iwork_query;

  // Query optimal workspace sizes
  ssyevd_(&jobz, &uplo, &n, A, &lda, eigenvalues, &work_query, &lwork,
           &iwork_query, &liwork, &info);
  assert(info == 0 && "ssyevd workspace query failed");

  lwork = (int)work_query;
  liwork = iwork_query;
  float *work = (float *)malloc(lwork * sizeof(float));
  int *iwork = (int *)malloc(liwork * sizeof(int));

  // Compute eigendecomposition
  ssyevd_(&jobz, &uplo, &n, A, &lda, eigenvalues, work, &lwork, iwork,
           &liwork, &info);
  assert(info == 0 && "ssyevd failed");

  free(work);
  free(iwork);
}
