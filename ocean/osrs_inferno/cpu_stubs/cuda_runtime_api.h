/* CPU-build shim: CUDA runtime API declarations for the shared binding paths. */
#ifndef OSRS_INFERNO_CPU_STUBS_CUDA_RUNTIME_API_H
#define OSRS_INFERNO_CPU_STUBS_CUDA_RUNTIME_API_H

#include <stddef.h>

typedef int cudaError_t;
typedef int cudaMemcpyKind;
typedef struct CUstream_st* cudaStream_t;

enum {
    cudaMemcpyHostToDevice = 1,
    cudaMemcpyDeviceToHost = 2,
    cudaHostAllocPortable = 1,
    cudaStreamNonBlocking = 1,
};

cudaError_t cudaHostAlloc(void** ptr, size_t size, unsigned int flags);
cudaError_t cudaMalloc(void** ptr, size_t size);
cudaError_t cudaMemcpy(void* dst, const void* src, size_t size, cudaMemcpyKind kind);
cudaError_t cudaMemcpyAsync(
    void* dst,
    const void* src,
    size_t size,
    cudaMemcpyKind kind,
    cudaStream_t stream);
cudaError_t cudaMemset(void* ptr, int value, size_t size);
cudaError_t cudaFree(void* ptr);
cudaError_t cudaFreeHost(void* ptr);
cudaError_t cudaSetDevice(int device);
cudaError_t cudaDeviceSynchronize(void);
cudaError_t cudaStreamSynchronize(cudaStream_t stream);
cudaError_t cudaStreamCreateWithFlags(cudaStream_t* stream, unsigned int flags);
cudaError_t cudaStreamQuery(cudaStream_t stream);
const char* cudaGetErrorString(cudaError_t error);

#endif
