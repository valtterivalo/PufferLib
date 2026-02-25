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
#import <MetalPerformanceShaders/MetalPerformanceShaders.h>
#include "metal_shader_src.h"

#include <cassert>
#include <cstdio>
#include <cstring>
#include <unordered_map>

// ============================================================================
// Global singleton
// ============================================================================

static MetalContext g_ctx = {};

// ============================================================================
// MPS GEMM cache — reuse MPSMatrixMultiplication objects across calls.
// Each object encodes (M, N, K, transpose flags, alpha, beta, fp16) and
// configures the GPU pipeline internally. Creating one per GEMM call adds
// significant ObjC overhead; caching eliminates it for the ~10-20 unique
// shapes used during training and rollout.
// ============================================================================

struct GemmKey {
  int M, N, K;
  bool tA, tB, fp16;
  float alpha, beta;
  bool operator==(const GemmKey& o) const {
    return M == o.M && N == o.N && K == o.K && tA == o.tA && tB == o.tB
        && fp16 == o.fp16 && alpha == o.alpha && beta == o.beta;
  }
};

struct GemmKeyHash {
  size_t operator()(const GemmKey& k) const {
    size_t h = (size_t)k.M * 73856093u ^ (size_t)k.N * 19349663u
             ^ (size_t)k.K * 83492791u;
    h ^= (size_t)k.tA | ((size_t)k.tB << 1) | ((size_t)k.fp16 << 2);
    h ^= (k.beta == 0.0f ? 0u : 7u) << 3;
    return h;
  }
};

static std::unordered_map<GemmKey, MPSMatrixMultiplication*, GemmKeyHash> g_matmul_cache;

static MPSMatrixMultiplication* cached_matmul(int M, int N, int K,
                                               bool tA, bool tB,
                                               float alpha, float beta,
                                               bool fp16) {
  GemmKey key = {M, N, K, tA, tB, fp16, alpha, beta};
  auto it = g_matmul_cache.find(key);
  if (it != g_matmul_cache.end()) return it->second;
  MPSMatrixMultiplication* mm = [[MPSMatrixMultiplication alloc]
      initWithDevice:g_ctx.device transposeLeft:tA transposeRight:tB
          resultRows:M resultColumns:N interiorColumns:K
               alpha:(double)alpha beta:(double)beta];
  g_matmul_cache[key] = mm;
  return mm;
}

// ============================================================================
// MetalStream implementation
// ============================================================================

void MetalStream::begin() {
  enc = nil;
  enc_active = false;
  pending_work = false;
  flushed = false;
  cmd = [queue commandBuffer];
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

void MetalStream::flush() {
  end_compute();
  if (pending_work) {
    [cmd commit];
    flushed = true;
    pending_work = false;
  }
}

void MetalStream::wait_completed() {
  if (flushed) {
    uint64_t t0 = mach_absolute_time();
    [cmd waitUntilCompleted];
    uint64_t t1 = mach_absolute_time();
    g_sync_count++;
    g_sync_total_ns += mach_to_ns(t1 - t0);
    flushed = false;
    begin();
  }
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
    g_ctx.train_queue = [g_ctx.device newCommandQueue];

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

    // Start the default stream (rollout) and training stream
    g_ctx.stream.queue = g_ctx.queue;
    g_ctx.stream.begin();
    g_ctx.train_stream.queue = g_ctx.train_queue;
    g_ctx.train_stream.begin();

    printf("[metal] device: %s, unified memory: %s\n",
           g_ctx.device.name.UTF8String,
           g_ctx.device.hasUnifiedMemory ? "yes" : "no");
  }
}

MetalContext *mtl_ctx() { return &g_ctx; }

void *mtl_stream() { return &g_ctx.stream; }

void *mtl_train_stream() { return &g_ctx.train_stream; }

void mtl_destroy() {
  g_ctx.stream.end_compute();
  g_ctx.stream.cmd = nil;
  g_ctx.train_stream.end_compute();
  g_ctx.train_stream.cmd = nil;
  g_matmul_cache.clear();
  g_ctx.buffers.clear();
  g_ctx.pipelines = nil;
  g_ctx.library = nil;
  g_ctx.queue = nil;
  g_ctx.train_queue = nil;
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

// ============================================================================
// Metal compute GEMM: uses simdgroup_matrix hardware instructions (M3+).
// Stays in the compute encoder — zero MPS encoder restart overhead.
// Used for fp32 GEMMs (rollout inference + Muon optimizer).
// ============================================================================

// Must match MSL GemmParams layout exactly (10 x 4 bytes = 40 bytes).
struct HostGemmParams {
  int M, N, K, lda, ldb, ldc;
  float alpha, beta;
  int trans_a, trans_b;
};

// C(M,N) = alpha * op(A) @ op(B) + beta * C
// op(A) is MxK, op(B) is KxN. lda/ldb/ldc are physical row strides.
static void compute_gemm(const float *A, const float *B, float *C,
                          int M, int N, int K,
                          bool trans_a, bool trans_b,
                          int lda, int ldb, int ldc,
                          float alpha, float beta,
                          cudaStream_t stream) {
  MetalStream *ms = get_stream(stream);
  id<MTLComputeCommandEncoder> enc = ms->compute_encoder();
  id<MTLComputePipelineState> pso = mtl_pipeline("steel_gemm");
  [enc setComputePipelineState:pso];

  NSUInteger off_a, off_b, off_c;
  id<MTLBuffer> buf_a = buffer_for_ptr(A, &off_a);
  id<MTLBuffer> buf_b = buffer_for_ptr(B, &off_b);
  id<MTLBuffer> buf_c = buffer_for_ptr(C, &off_c);

  [enc setBuffer:buf_a offset:off_a atIndex:0];
  [enc setBuffer:buf_b offset:off_b atIndex:1];
  [enc setBuffer:buf_c offset:off_c atIndex:2];

  HostGemmParams params = {M, N, K, lda, ldb, ldc, alpha, beta,
                            trans_a ? 1 : 0, trans_b ? 1 : 0};
  [enc setBytes:&params length:sizeof(params) atIndex:3];

  // 64x64 output tile per threadgroup, 128 threads (4 simdgroups)
  int groups_m = (M + 63) / 64;
  int groups_n = (N + 63) / 64;
  [enc dispatchThreadgroups:MTLSizeMake(groups_n, groups_m, 1)
      threadsPerThreadgroup:MTLSizeMake(128, 1, 1)];

  ms->pending_work = true;
}

// ============================================================================
// MPS GEMM: C = alpha * op(A) @ op(B) + beta * C
// Uses MPSMatrixMultiplication — best for fp16 large-batch training GEMMs.
// MPS encodes to the command buffer (not compute encoder), so we end any
// active compute encoder before encoding.
// ============================================================================

static void gpu_gemm(const float *A, int physical_rows_A, int physical_cols_A,
                      bool transpose_A,
                      const float *B, int physical_rows_B, int physical_cols_B,
                      bool transpose_B,
                      float *C, int result_rows, int result_cols,
                      int interior_cols,
                      float alpha, float beta,
                      cudaStream_t stream) {
  int M = result_rows, N = result_cols, K = interior_cols;

  MetalStream *ms = get_stream(stream);
  ms->end_compute();

  NSUInteger off_a, off_b, off_c;
  id<MTLBuffer> buf_a = buffer_for_ptr(A, &off_a);
  id<MTLBuffer> buf_b = buffer_for_ptr(B, &off_b);
  id<MTLBuffer> buf_c = buffer_for_ptr(C, &off_c);

  MPSMatrixDescriptor *desc_a = [MPSMatrixDescriptor
      matrixDescriptorWithRows:physical_rows_A
                       columns:physical_cols_A
                      rowBytes:physical_cols_A * sizeof(float)
                      dataType:MPSDataTypeFloat32];
  MPSMatrixDescriptor *desc_b = [MPSMatrixDescriptor
      matrixDescriptorWithRows:physical_rows_B
                       columns:physical_cols_B
                      rowBytes:physical_cols_B * sizeof(float)
                      dataType:MPSDataTypeFloat32];
  MPSMatrixDescriptor *desc_c = [MPSMatrixDescriptor
      matrixDescriptorWithRows:M
                       columns:N
                      rowBytes:N * sizeof(float)
                      dataType:MPSDataTypeFloat32];

  MPSMatrix *mat_a = [[MPSMatrix alloc] initWithBuffer:buf_a offset:off_a descriptor:desc_a];
  MPSMatrix *mat_b = [[MPSMatrix alloc] initWithBuffer:buf_b offset:off_b descriptor:desc_b];
  MPSMatrix *mat_c = [[MPSMatrix alloc] initWithBuffer:buf_c offset:off_c descriptor:desc_c];

  MPSMatrixMultiplication *matmul = cached_matmul(M, N, K, transpose_A, transpose_B,
                                                   alpha, beta, /*fp16=*/false);
  [matmul encodeToCommandBuffer:ms->cmd leftMatrix:mat_a rightMatrix:mat_b resultMatrix:mat_c];
  ms->pending_work = true;
}

// GPU GEMM fp16: half inputs, half output, float accumulation inside MPS.
static void gpu_gemm_fp16(const void *A, int physical_rows_A, int physical_cols_A,
                           bool transpose_A,
                           const void *B, int physical_rows_B, int physical_cols_B,
                           bool transpose_B,
                           void *C, int result_rows, int result_cols,
                           int interior_cols,
                           float alpha, float beta,
                           cudaStream_t stream) {
  int M = result_rows, N = result_cols, K = interior_cols;

  MetalStream *ms = get_stream(stream);
  ms->end_compute();

  NSUInteger off_a, off_b, off_c;
  id<MTLBuffer> buf_a = buffer_for_ptr(A, &off_a);
  id<MTLBuffer> buf_b = buffer_for_ptr(B, &off_b);
  id<MTLBuffer> buf_c = buffer_for_ptr(C, &off_c);

  MPSMatrixDescriptor *desc_a = [MPSMatrixDescriptor
      matrixDescriptorWithRows:physical_rows_A
                       columns:physical_cols_A
                      rowBytes:physical_cols_A * sizeof(__fp16)
                      dataType:MPSDataTypeFloat16];
  MPSMatrixDescriptor *desc_b = [MPSMatrixDescriptor
      matrixDescriptorWithRows:physical_rows_B
                       columns:physical_cols_B
                      rowBytes:physical_cols_B * sizeof(__fp16)
                      dataType:MPSDataTypeFloat16];
  MPSMatrixDescriptor *desc_c = [MPSMatrixDescriptor
      matrixDescriptorWithRows:M
                       columns:N
                      rowBytes:N * sizeof(__fp16)
                      dataType:MPSDataTypeFloat16];

  MPSMatrix *mat_a = [[MPSMatrix alloc] initWithBuffer:buf_a offset:off_a descriptor:desc_a];
  MPSMatrix *mat_b = [[MPSMatrix alloc] initWithBuffer:buf_b offset:off_b descriptor:desc_b];
  MPSMatrix *mat_c = [[MPSMatrix alloc] initWithBuffer:buf_c offset:off_c descriptor:desc_c];

  MPSMatrixMultiplication *matmul = cached_matmul(M, N, K, transpose_A, transpose_B,
                                                   alpha, beta, /*fp16=*/true);
  [matmul encodeToCommandBuffer:ms->cmd leftMatrix:mat_a rightMatrix:mat_b resultMatrix:mat_c];
  ms->pending_work = true;
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
// Only called from Muon Newton-Schulz (small fp32 GEMMs: 512×512).
// Uses compute encoder to avoid MPS encoder restarts (40 per training step).
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
  // No-op on Metal. GPU work is already synced inside net_callback_wrapper
  // (ensure_gpu_synced under mutex). The vecenv memcpys are also no-ops
  // (unified memory). Calling sync() here would race with other buffer
  // threads that hold the GPU mutex and have an active encoder.
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
