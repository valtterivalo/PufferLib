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
// Metal 4 tensor_ops GEMM — MSL source for JIT compilation.
// Separate library because it needs different includes (metal_tensor,
// MetalPerformancePrimitives). Stays on the compute encoder — zero
// MPS encoder restart overhead.
// ============================================================================

static const char *get_tensor_ops_shader_source() {
  return R"METAL(
#include <metal_stdlib>
#include <metal_tensor>
#include <MetalPerformancePrimitives/MPPTensorOpsMatMul2d.h>
using namespace metal;
using namespace mpp::tensor_ops;

// C(M,N) = A(M,K) @ B(N,K)^T — float32, tensor_inline with device memory.
// Tile: 64 rows (M) x 32 cols (N), dynamic K.
// N MUST be a multiple of 32 (caller pads or falls back to MPS).
kernel void tensor_ops_gemm_nt_f32(
    device float* A_buf [[buffer(0)]],
    device float* B_buf [[buffer(1)]],
    device float* C_buf [[buffer(2)]],
    constant uint& M    [[buffer(3)]],
    constant uint& N    [[buffer(4)]],
    constant uint& K    [[buffer(5)]],
    uint2 tgid [[threadgroup_position_in_grid]])
{
    auto A = tensor<device float, dextents<int32_t, 2>, tensor_inline>(
        A_buf, dextents<int32_t, 2>(K, M));
    auto B = tensor<device float, dextents<int32_t, 2>, tensor_inline>(
        B_buf, dextents<int32_t, 2>(K, N));
    auto C = tensor<device float, dextents<int32_t, 2>, tensor_inline>(
        C_buf, dextents<int32_t, 2>(N, M));

    constexpr auto desc = matmul2d_descriptor(
        64, 32,
        static_cast<int>(dynamic_extent),
        false, true, false
    );
    matmul2d<desc, execution_simdgroups<4>> op;

    auto mA = A.slice(0, tgid.y * 64);
    auto mB = B.slice(0, tgid.x * 32);
    auto mC = C.slice(tgid.x * 32, tgid.y * 64);

    op.run(mA, mB, mC);
}

// C(M,N) = A(M,K) @ B(K,N) — float32, tensor_inline with device memory.
// Row-major NN maps to col-major: C_cm(N,M) = B_cm(N,K) @ A_cm(K,M).
// Tiling follows NT convention: tgid.y tiles M (stride 64), tgid.x tiles N (stride 32).
// M % 64 == 0 and N % 32 == 0 required (caller falls back to MPS).
kernel void tensor_ops_gemm_nn_f32(
    device float* A_buf [[buffer(0)]],
    device float* B_buf [[buffer(1)]],
    device float* C_buf [[buffer(2)]],
    constant uint& M    [[buffer(3)]],
    constant uint& N    [[buffer(4)]],
    constant uint& K    [[buffer(5)]],
    uint2 tgid [[threadgroup_position_in_grid]])
{
    // Row-major A(M,K) in memory == col-major A_cm(K,M)
    auto A_cm = tensor<device float, dextents<int32_t, 2>, tensor_inline>(
        A_buf, dextents<int32_t, 2>(K, M));
    // Row-major B(K,N) in memory == col-major B_cm(N,K)
    auto B_cm = tensor<device float, dextents<int32_t, 2>, tensor_inline>(
        B_buf, dextents<int32_t, 2>(N, K));
    // Row-major C(M,N) in memory == col-major C_cm(N,M)
    auto C_cm = tensor<device float, dextents<int32_t, 2>, tensor_inline>(
        C_buf, dextents<int32_t, 2>(N, M));

    // Col-major NN: C_cm(N,M) = B_cm(N,K) @ A_cm(K,M)
    // op.run convention: result = second @ first (same as NT kernel)
    // first=A_cm, second=B_cm, no transposes
    constexpr auto desc = matmul2d_descriptor(
        64, 32,
        static_cast<int>(dynamic_extent),
        false, false, false
    );
    matmul2d<desc, execution_simdgroups<4>> op;

    // tgid.y tiles M at stride 64 (tile_M), tgid.x tiles N at stride 32 (tile_N)
    // Same convention as NT kernel
    auto mFirst  = A_cm.slice(0, tgid.y * 64);
    auto mSecond = B_cm.slice(tgid.x * 32, 0);
    auto mResult = C_cm.slice(tgid.x * 32, tgid.y * 64);

    op.run(mFirst, mSecond, mResult);
}
)METAL";
}

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

static int g_tensor_ops_dispatch_count = 0;
static int g_mps_dispatch_count = 0;

void mtl_gemm_stats(int *tensor_ops_count, int *mps_count) {
  *tensor_ops_count = g_tensor_ops_dispatch_count;
  *mps_count = g_mps_dispatch_count;
  g_tensor_ops_dispatch_count = 0;
  g_mps_dispatch_count = 0;
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
    opts.languageVersion = MTLLanguageVersion4_0;
    g_ctx.library =
        [g_ctx.device newLibraryWithSource:src options:opts error:&error];
    if (!g_ctx.library) {
      NSLog(@"Metal shader compilation failed: %@", error);
      assert(false && "MSL compilation failed");
    }

    g_ctx.pipelines = [NSMutableDictionary new];

    // Compile Metal 4 tensor_ops GEMM (separate library, different includes).
    // Falls back gracefully if compilation fails (e.g. older macOS).
    {
      NSString *tensor_src = [NSString stringWithUTF8String:get_tensor_ops_shader_source()];
      MTLCompileOptions *tensor_opts = [[MTLCompileOptions alloc] init];
      tensor_opts.mathMode = MTLMathModeFast;
      tensor_opts.languageVersion = MTLLanguageVersion4_0;
      NSError *tensor_err = nil;
      id<MTLLibrary> tensor_lib = [g_ctx.device newLibraryWithSource:tensor_src
                                                             options:tensor_opts
                                                               error:&tensor_err];
      if (tensor_lib) {
        id<MTLFunction> fn = [tensor_lib newFunctionWithName:@"tensor_ops_gemm_nt_f32"];
        MTLComputePipelineDescriptor *pd = [[MTLComputePipelineDescriptor alloc] init];
        pd.computeFunction = fn;
        pd.maxTotalThreadsPerThreadgroup = 128;
        g_ctx.tensor_ops_gemm_nt_f32 =
            [g_ctx.device newComputePipelineStateWithDescriptor:pd
                                                       options:0
                                                    reflection:nil
                                                         error:&tensor_err];
        if (g_ctx.tensor_ops_gemm_nt_f32) {
          // Also compile NN variant for muon Newton-Schulz
          id<MTLFunction> fn_nn = [tensor_lib newFunctionWithName:@"tensor_ops_gemm_nn_f32"];
          MTLComputePipelineDescriptor *pd_nn = [[MTLComputePipelineDescriptor alloc] init];
          pd_nn.computeFunction = fn_nn;
          pd_nn.maxTotalThreadsPerThreadgroup = 128;
          NSError *nn_err = nil;
          g_ctx.tensor_ops_gemm_nn_f32 =
              [g_ctx.device newComputePipelineStateWithDescriptor:pd_nn
                                                         options:0
                                                      reflection:nil
                                                           error:&nn_err];
          printf("[metal] tensor_ops GEMM: NT=%s NN=%s\n",
                 "OK",
                 g_ctx.tensor_ops_gemm_nn_f32 ? "OK" : nn_err.localizedDescription.UTF8String);
        } else {
          printf("[metal] tensor_ops GEMM pipeline failed: %s\n",
                 tensor_err.localizedDescription.UTF8String);
        }
      } else {
        printf("[metal] tensor_ops compilation failed (non-fatal): %s\n",
               tensor_err.localizedDescription.UTF8String);
      }
    }

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

bool puf_stream_has_encoder(cudaStream_t stream) {
  MetalStream *ms = get_stream(stream);
  return ms->enc_active;
}

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
  g_mps_dispatch_count++;
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

// ============================================================================
// tensor_ops GEMM: C(M,N) = A(M,K) @ B(N,K)^T on the compute encoder.
// Uses Metal 4 matmul2d with tensor_inline — no MPS encoder transition.
// Requires N % 32 == 0, M % 64 == 0 (tiles). Returns false if unavailable.
// ============================================================================

static bool tensor_ops_gemm_nt(const float *A, const float *B, float *C,
                                int M, int N, int K,
                                cudaStream_t stream) {
  if (!g_ctx.tensor_ops_gemm_nt_f32) return false;
  g_tensor_ops_dispatch_count++;

  MetalStream *ms = get_stream(stream);
  id<MTLComputeCommandEncoder> enc = ms->compute_encoder();
  [enc setComputePipelineState:g_ctx.tensor_ops_gemm_nt_f32];

  NSUInteger off_a, off_b, off_c;
  id<MTLBuffer> buf_a = buffer_for_ptr(A, &off_a);
  id<MTLBuffer> buf_b = buffer_for_ptr(B, &off_b);
  id<MTLBuffer> buf_c = buffer_for_ptr(C, &off_c);

  [enc setBuffer:buf_a offset:off_a atIndex:0];
  [enc setBuffer:buf_b offset:off_b atIndex:1];
  [enc setBuffer:buf_c offset:off_c atIndex:2];

  uint32_t mM = (uint32_t)M, mN = (uint32_t)N, mK = (uint32_t)K;
  [enc setBytes:&mM length:sizeof(mM) atIndex:3];
  [enc setBytes:&mN length:sizeof(mN) atIndex:4];
  [enc setBytes:&mK length:sizeof(mK) atIndex:5];

  int groups_m = (M + 63) / 64;
  int groups_n = (N + 31) / 32;
  [enc dispatchThreadgroups:MTLSizeMake(groups_n, groups_m, 1)
      threadsPerThreadgroup:MTLSizeMake(128, 1, 1)];

  ms->pending_work = true;
  return true;
}

// ============================================================================
// tensor_ops GEMM: C(M,N) = A(M,K) @ B(K,N) on the compute encoder.
// Uses Metal 4 matmul2d with tensor_inline — no MPS encoder transition.
// Requires M % 64 == 0, N % 32 == 0 (tile_M=64, tile_N=32).
// Returns false if unavailable.
// ============================================================================

static bool tensor_ops_gemm_nn(const float *A, const float *B, float *C,
                                int M, int N, int K,
                                cudaStream_t stream) {
  if (!g_ctx.tensor_ops_gemm_nn_f32) return false;
  g_tensor_ops_dispatch_count++;

  MetalStream *ms = get_stream(stream);
  id<MTLComputeCommandEncoder> enc = ms->compute_encoder();
  [enc setComputePipelineState:g_ctx.tensor_ops_gemm_nn_f32];

  NSUInteger off_a, off_b, off_c;
  id<MTLBuffer> buf_a = buffer_for_ptr(A, &off_a);
  id<MTLBuffer> buf_b = buffer_for_ptr(B, &off_b);
  id<MTLBuffer> buf_c = buffer_for_ptr(C, &off_c);

  [enc setBuffer:buf_a offset:off_a atIndex:0];
  [enc setBuffer:buf_b offset:off_b atIndex:1];
  [enc setBuffer:buf_c offset:off_c atIndex:2];

  uint32_t mM = (uint32_t)M, mN = (uint32_t)N, mK = (uint32_t)K;
  [enc setBytes:&mM length:sizeof(mM) atIndex:3];
  [enc setBytes:&mN length:sizeof(mN) atIndex:4];
  [enc setBytes:&mK length:sizeof(mK) atIndex:5];

  // tgid.y tiles M (stride 64), tgid.x tiles N (stride 32) — same as NT kernel
  int groups_m = (M + 63) / 64;
  int groups_n = (N + 31) / 32;
  [enc dispatchThreadgroups:MTLSizeMake(groups_n, groups_m, 1)
      threadsPerThreadgroup:MTLSizeMake(128, 1, 1)];

  ms->pending_work = true;
  return true;
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
  } else if ((N % 32 == 0) && !getenv("PUFFERLIB_NO_TENSOR_OPS") &&
             tensor_ops_gemm_nt((const float *)a.bytes, (const float *)b.bytes,
                                (float *)out.bytes, M, N, K, stream)) {
    // tensor_ops GEMM on compute encoder — no MPS encoder transition
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
  } else if ((M % 64 == 0) && (N % 32 == 0) && !getenv("PUFFERLIB_NO_TENSOR_OPS") &&
             tensor_ops_gemm_nn((const float *)a.bytes, (const float *)b.bytes,
                                (float *)out.bytes, M, N, K, stream)) {
    // tensor_ops NN GEMM on compute encoder — no MPS encoder transition
  } else {
    gpu_gemm((const float *)a.bytes, M, K, false,
             (const float *)b.bytes, K, N, false,
             (float *)out.bytes, M, N, K,
             1.0f, 0.0f, stream);
  }
}

// ============================================================================
// addmm temp buffer — lazily allocated for tensor_ops addmm decomposition.
// Only used for aligned muon NS GEMMs (512×512). Page-aligned for MTLBuffer.
// ============================================================================

static char *g_addmm_temp_base = nullptr;
static int64_t g_addmm_temp_size = 0;

static float *addmm_temp_buf(int count) {
  int64_t needed = (int64_t)count * sizeof(float);
  int64_t page = 16384;
  int64_t size = (needed + page - 1) & ~(page - 1);
  if (size <= g_addmm_temp_size) return (float *)g_addmm_temp_base;

  // Remove old buffer from wrapped list
  if (g_addmm_temp_base) {
    auto &bufs = g_ctx.buffers;
    bufs.erase(std::remove_if(bufs.begin(), bufs.end(),
        [](const WrappedBuffer &wb) { return wb.base == g_addmm_temp_base; }),
        bufs.end());
    free(g_addmm_temp_base);
  }
  posix_memalign((void **)&g_addmm_temp_base, page, size);
  id<MTLBuffer> buf =
      [g_ctx.device newBufferWithBytesNoCopy:g_addmm_temp_base
                                      length:size
                                     options:MTLResourceStorageModeShared
                                 deallocator:nil];
  assert(buf && "addmm temp buffer MTLBuffer creation failed");
  g_ctx.buffers.push_back({g_addmm_temp_base, size, buf});
  g_addmm_temp_size = size;
  return (float *)g_addmm_temp_base;
}

// out(...,N) = beta*out + alpha * a(...,K) @ b(K,N)
// Only called from Muon Newton-Schulz (small fp32 GEMMs: 512×512).
// When tensor_ops NN is available and dimensions align, decomposes into:
//   temp = A @ B (tensor_ops, compute encoder)
//   out *= beta (scale, compute encoder)
//   out += alpha * temp (axpy, compute encoder)
// All three ops stay on the compute encoder — zero MPS transitions.
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
  } else if ((M % 64 == 0) && (N % 32 == 0) &&
             !getenv("PUFFERLIB_NO_TENSOR_OPS") &&
             g_ctx.tensor_ops_gemm_nn_f32) {
    // Decompose: out = beta*out + alpha*(a@b)
    // Step 1: temp = a @ b via tensor_ops NN (compute encoder)
    float *temp = addmm_temp_buf(M * N);
    tensor_ops_gemm_nn((const float *)a.bytes, (const float *)b.bytes,
                       temp, M, N, K, stream);

    // Step 2: out *= beta (compute encoder)
    MetalStream *ms = get_stream(stream);
    id<MTLComputeCommandEncoder> enc = ms->compute_encoder();
    int count = M * N;

    if (beta != 1.0f) {
      auto pso = mtl_pipeline("scale_f32");
      [enc setComputePipelineState:pso];
      NSUInteger off_out;
      [enc setBuffer:buffer_for_ptr(out.bytes, &off_out)
              offset:off_out atIndex:0];
      struct { float alpha; int n; } sp = {beta, count};
      [enc setBytes:&sp length:sizeof(sp) atIndex:1];
      mtl_dispatch_1d(enc, pso, count);
    }

    // Step 3: out += alpha * temp (compute encoder)
    {
      auto pso = mtl_pipeline("axpy_f32");
      [enc setComputePipelineState:pso];
      NSUInteger off_out, off_temp;
      [enc setBuffer:buffer_for_ptr(out.bytes, &off_out)
              offset:off_out atIndex:0];
      [enc setBuffer:buffer_for_ptr(temp, &off_temp)
              offset:off_temp atIndex:1];
      struct { float alpha; int n; } ap = {alpha, count};
      [enc setBytes:&ap length:sizeof(ap) atIndex:2];
      mtl_dispatch_1d(enc, pso, count);
    }
    ms->pending_work = true;
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
