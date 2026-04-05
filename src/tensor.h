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

#ifdef __CUDACC__
typedef struct {
    precision_t* data;
    int64_t shape[PUF_MAX_DIMS];
} PrecisionTensor;
#else
// Metal always uses fp32 — PrecisionTensor is FloatTensor.
typedef FloatTensor PrecisionTensor;
#define PRECISION_SIZE ((int)sizeof(float))
#endif

#endif // PUFFERLIB_TENSOR_H
