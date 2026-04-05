// Typed tensor definitions for PufferLib static-native.
// Upstream 4.0 typed tensor system: statically typed .data pointers
// instead of runtime-typed char* bytes + dtype_size.
//
// Metal path: PrecisionTensor uses void* (fp16 data passed as opaque).
// CUDA path: PrecisionTensor uses precision_t* (bf16 or f32).

#ifndef PUFFERLIB_TENSOR_H
#define PUFFERLIB_TENSOR_H

#include <stdint.h>

#define PUF_MAX_DIMS 8

typedef struct {
    float* data;
    int64_t shape[PUF_MAX_DIMS];
} FloatTensor;

typedef struct {
    unsigned char* data;
    int64_t shape[PUF_MAX_DIMS];
} ByteTensor;

typedef struct {
    long* data;
    int64_t shape[PUF_MAX_DIMS];
} LongTensor;

typedef struct {
    int* data;
    int64_t shape[PUF_MAX_DIMS];
} IntTensor;

// PrecisionTensor: platform-dependent data pointer type.
// CUDA: precision_t* (bf16 or f32 depending on compile flags).
// Metal: void* (fp16 data handled as opaque bytes, cast at kernel dispatch).
// CPU-only: void* (never used for compute, only for allocation plumbing).
#ifdef __CUDACC__
typedef struct {
    precision_t* data;
    int64_t shape[PUF_MAX_DIMS];
} PrecisionTensor;
#else
typedef struct {
    void* data;
    int64_t shape[PUF_MAX_DIMS];
} PrecisionTensor;
#endif

// Free functions operating on shape arrays (no methods needed per tensor type).
// Matches upstream 4.0 kernels.cu pattern.
inline int puf_ndim(const int64_t* shape) {
    int n = 0;
    while (n < PUF_MAX_DIMS && shape[n] != 0) n++;
    return n;
}

inline int64_t puf_numel(const int64_t* shape) {
    int64_t n = 1;
    for (int i = 0; i < PUF_MAX_DIMS && shape[i] != 0; i++) n *= shape[i];
    return n;
}

inline int64_t puf_batch_size(const int64_t* shape) {
    int n = puf_ndim(shape);
    int64_t b = 1;
    for (int i = 0; i < n - 2; i++) b *= shape[i];
    return b;
}

#endif // PUFFERLIB_TENSOR_H
