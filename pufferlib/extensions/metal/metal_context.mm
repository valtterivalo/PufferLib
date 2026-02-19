/**
 * @fileoverview Metal compute context — singleton device, JIT shader library.
 *
 * Kernel dispatch uses PyTorch MPS's native stream pattern: dispatch_sync on
 * the MPS serial queue, encode via stream->commandEncoder(), bind tensors via
 * getMTLBufferStorage() (zero-copy on Apple Silicon unified memory). MPS manages
 * command buffer lifecycle — no manual commit/waitUntilCompleted needed.
 *
 * MEMORY MANAGEMENT: compiled with -fno-objc-arc (manual reference counting).
 * All alloc/new objects must be explicitly released. Kernel dispatch no longer
 * allocates Metal buffers (uses MPS tensor storage directly), so BufferCleanup
 * RAII is no longer needed.
 */

#import <Metal/Metal.h>
#import <Foundation/Foundation.h>

#include <torch/extension.h>
#include <ATen/mps/MPSStream.h>
#include <unordered_map>
#include <string>
#include <mutex>

// All Metal shader sources are embedded as string literals from separate headers
#include "metal_shader_src.h"

namespace metal_ctx {

// Borrowed reference — owned by PyTorch MPS runtime, do NOT release.
id<MTLDevice> g_device = nil;

// Owned by us — created at init, never released (lives for process lifetime).
id<MTLLibrary> g_library = nil;
std::unordered_map<std::string, id<MTLComputePipelineState>> g_pipelines;
std::mutex g_init_mutex;
bool g_initialized = false;

} // namespace metal_ctx

void metal_init() {
    std::lock_guard<std::mutex> lock(metal_ctx::g_init_mutex);
    if (metal_ctx::g_initialized) return;

    auto* mps_stream = at::mps::getCurrentMPSStream();
    metal_ctx::g_device = mps_stream->device();

    TORCH_CHECK(metal_ctx::g_device != nil, "Metal: MPS device is nil");

    // JIT compile all shader sources
    NSError* error = nil;
    MTLCompileOptions* opts = [[MTLCompileOptions alloc] init];
    opts.mathMode = MTLMathModeSafe;
    opts.languageVersion = MTLLanguageVersion3_0;

    NSString* src = [NSString stringWithUTF8String:get_all_metal_shader_source()];
    metal_ctx::g_library = [metal_ctx::g_device newLibraryWithSource:src
                                                             options:opts
                                                               error:&error];
    [opts release];

    TORCH_CHECK(metal_ctx::g_library != nil,
        "Metal shader compilation failed: ",
        error ? [[error localizedDescription] UTF8String] : "unknown error");

    metal_ctx::g_initialized = true;
    printf("Metal context initialized (MPS stream dispatch): %s\n",
           [[metal_ctx::g_device name] UTF8String]);
}

bool metal_is_ready() {
    return metal_ctx::g_initialized;
}

void metal_synchronize() {
    if (!metal_ctx::g_initialized) return;
    auto* stream = at::mps::getCurrentMPSStream();
    stream->synchronize(at::mps::SyncType::COMMIT_AND_WAIT);
}

// Pipeline state cache — used by kernel dispatch functions
namespace metal_ctx {

id<MTLComputePipelineState> get_pipeline(const std::string& name) {
    auto it = g_pipelines.find(name);
    if (it != g_pipelines.end()) return it->second;

    NSString* fn_name = [NSString stringWithUTF8String:name.c_str()];
    id<MTLFunction> fn = [g_library newFunctionWithName:fn_name];
    TORCH_CHECK(fn != nil, "Metal: kernel function '", name, "' not found in library");

    NSError* error = nil;
    id<MTLComputePipelineState> pso =
        [g_device newComputePipelineStateWithFunction:fn error:&error];
    [fn release];

    TORCH_CHECK(pso != nil,
        "Metal: failed to create pipeline for '", name, "': ",
        error ? [[error localizedDescription] UTF8String] : "unknown");

    g_pipelines[name] = pso;
    return pso;
}

} // namespace metal_ctx
