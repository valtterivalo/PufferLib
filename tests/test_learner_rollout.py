import shutil
import subprocess
from pathlib import Path

import pytest


@pytest.mark.parametrize("backend", ["cpu", "cuda"])
def test_learner_rollout_excludes_historical_rows(tmp_path: Path, backend: str) -> None:
    compiler = shutil.which("nvcc" if backend == "cuda" else "clang++")
    if compiler is None:
        pytest.skip(f"{backend} compiler unavailable")
    source = (Path(__file__).resolve().parents[1] / "src/pufferl.cu").read_text()
    gather = source[source.index("template <typename T>\n__global__ void gather_learner_rollout"):
                    source.index("// Cosine decay")]
    snapshot = source[source.index("static __device__ long state_elem_idx"):
                      source.index("// View slot of full")]
    harness = tmp_path / ("learner_rollout.cu" if backend == "cuda" else "learner_rollout.cpp")
    harness.write_text(r"""
#include <cassert>
#include <vector>
#ifdef __CUDACC__
#include <cuda_runtime.h>
#include <cuda_bf16.h>
#else
#define __global__
#define __device__
struct { int x; } blockIdx, blockDim, threadIdx;
#endif
struct Prec { float* data; long shape[3]; };
""" + gather + snapshot + r"""
template <typename T>
void check_rollout(int buffers, int learners_per_buf, int horizon, int cols) {
    const int actors_per_buf = 4;
    const int actors = buffers * actors_per_buf;
    const int learners = buffers * learners_per_buf;
    std::vector<T> src(horizon * actors * cols);
    std::vector<T> dst(horizon * learners * cols, 0);
    for (int t = 0; t < horizon; t++) {
        for (int actor = 0; actor < actors; actor++) {
            for (int col = 0; col < cols; col++) {
                int i = (t * actors + actor) * cols + col;
                src[i] = actor % actors_per_buf < learners_per_buf
                    ? T(i + 1) : T(-1);
            }
        }
    }
#ifdef __CUDACC__
    T *device_src, *device_dst;
    assert(cudaMalloc((void**)&device_src, src.size() * sizeof(T)) == cudaSuccess);
    assert(cudaMalloc((void**)&device_dst, dst.size() * sizeof(T)) == cudaSuccess);
    assert(cudaMemcpy(device_src, src.data(), src.size() * sizeof(T), cudaMemcpyHostToDevice) == cudaSuccess);
    gather_learner_rollout<<<(dst.size() + 255) / 256, 256>>>(
        device_dst, device_src, horizon, actors, cols, actors_per_buf, learners_per_buf);
    assert(cudaMemcpy(dst.data(), device_dst, dst.size() * sizeof(T), cudaMemcpyDeviceToHost) == cudaSuccess);
    assert(cudaFree(device_src) == cudaSuccess);
    assert(cudaFree(device_dst) == cudaSuccess);
#else
    blockDim.x = 1;
    threadIdx.x = 0;
    for (blockIdx.x = 0; blockIdx.x <= int(dst.size()); blockIdx.x++) {
        gather_learner_rollout(dst.data(), src.data(), horizon, actors, cols,
            actors_per_buf, learners_per_buf);
    }
#endif
    int learner = 0;
    for (int actor = 0; actor < actors; actor++) {
        if (actor % actors_per_buf >= learners_per_buf) continue;
        for (int t = 0; t < horizon; t++) {
            for (int col = 0; col < cols; col++) {
                assert(dst[(learner * horizon + t) * cols + col]
                    == T((t * actors + actor) * cols + col + 1));
            }
        }
        learner++;
    }
    assert(learner == learners);
}

void check_states(int buffers, int learners_per_buf) {
    const int layers = 2, hidden = 5;
    std::vector<float> dst(layers * buffers * learners_per_buf * hidden, -1);
#ifdef __CUDACC__
    float* device_dst;
    assert(cudaMalloc((void**)&device_dst, dst.size() * sizeof(float)) == cudaSuccess);
    assert(cudaMemcpy(device_dst, dst.data(), dst.size() * sizeof(float), cudaMemcpyHostToDevice) == cudaSuccess);
    Prec dst_view = {device_dst, {layers, buffers * learners_per_buf, hidden}};
#else
    Prec dst_view = {dst.data(), {layers, buffers * learners_per_buf, hidden}};
    blockDim.x = 1;
    threadIdx.x = 0;
#endif
    for (int buf = 0; buf < buffers; buf++) {
        std::vector<float> src(layers * learners_per_buf * hidden);
        for (int i = 0; i < int(src.size()); i++) src[i] = buf * 1000 + i;
#ifdef __CUDACC__
        float* device_src;
        assert(cudaMalloc((void**)&device_src, src.size() * sizeof(float)) == cudaSuccess);
        assert(cudaMemcpy(device_src, src.data(), src.size() * sizeof(float), cudaMemcpyHostToDevice) == cudaSuccess);
        Prec src_view = {device_src, {layers, learners_per_buf, hidden}};
        snapshot_state<<<1, 256>>>(dst_view, src_view, buf * learners_per_buf, learners_per_buf);
        assert(cudaDeviceSynchronize() == cudaSuccess);
        assert(cudaFree(device_src) == cudaSuccess);
#else
        Prec src_view = {src.data(), {layers, learners_per_buf, hidden}};
        for (blockIdx.x = 0; blockIdx.x <= int(src.size()); blockIdx.x++) {
            snapshot_state(dst_view, src_view, buf * learners_per_buf, learners_per_buf);
        }
#endif
    }
#ifdef __CUDACC__
    assert(cudaMemcpy(dst.data(), device_dst, dst.size() * sizeof(float), cudaMemcpyDeviceToHost) == cudaSuccess);
    assert(cudaFree(device_dst) == cudaSuccess);
#endif
    for (int layer = 0; layer < layers; layer++) {
        for (int learner = 0; learner < buffers * learners_per_buf; learner++) {
            for (int h = 0; h < hidden; h++) {
                int buf = learner / learners_per_buf;
                int local = learner % learners_per_buf;
                assert(dst[(layer * buffers * learners_per_buf + learner) * hidden + h]
                    == buf * 1000 + (layer * learners_per_buf + local) * hidden + h);
            }
        }
    }
}

int main() {
    for (int buffers : {1, 2}) {
        for (int learners_per_buf : {1, 3, 4}) {
            check_states(buffers, learners_per_buf);
            for (int horizon : {1, 8}) {
                for (int cols : {1, 3, 17}) {
                    check_rollout<float>(buffers, learners_per_buf, horizon, cols);
                    check_rollout<unsigned short>(buffers, learners_per_buf, horizon, cols);
#ifdef __CUDACC__
                    check_rollout<__nv_bfloat16>(buffers, learners_per_buf, horizon, cols);
#endif
                }
            }
        }
    }
}
""")
    executable = tmp_path / "learner_rollout"
    subprocess.run([compiler, "-std=c++17", "-O2", str(harness), "-o", str(executable)], check=True)
    subprocess.run([str(executable)], check=True)
