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
                    source.index("static double monotonic_seconds(void)")]
    snapshot = source[source.index("static __device__ long state_elem_idx"):
                      source.index("// View slot of full")]
    rotation = source[source.index("    int row_offset ="):source.index(
        "    cudaMemcpyAsync(pufferl->train_row_offset.data")]
    rotation = rotation.removeprefix("    int row_offset =").strip().removesuffix(";")
    for original, local in (
        ("pufferl->epoch", "epoch"),
        ("total_minibatches", "updates"),
        ("hypers->minibatch_size", "minibatch"),
        ("hypers->horizon", "horizon"),
        ("pufferl->train_rollouts.observations.shape[0]", "learners"),
    ):
        rotation = rotation.replace(original, local)
    harness = tmp_path / ("learner_rollout.cu" if backend == "cuda" else "learner_rollout.cpp")
    harness.write_text(r"""
#include <cassert>
#include <cstdint>
#include <numeric>
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
void check_rollout(int buffers, int learners_per_buf, int horizon, int cols, int offset) {
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
    int* device_offset;
    assert(cudaMalloc((void**)&device_src, src.size() * sizeof(T)) == cudaSuccess);
    assert(cudaMalloc((void**)&device_dst, dst.size() * sizeof(T)) == cudaSuccess);
    assert(cudaMalloc((void**)&device_offset, sizeof(int)) == cudaSuccess);
    assert(cudaMemcpy(device_offset, &offset, sizeof(int), cudaMemcpyHostToDevice) == cudaSuccess);
    assert(cudaMemcpy(device_src, src.data(), src.size() * sizeof(T), cudaMemcpyHostToDevice) == cudaSuccess);
    gather_learner_rollout<<<(dst.size() + 255) / 256, 256>>>(
        device_dst, device_src, horizon, actors, cols, actors_per_buf, learners_per_buf, device_offset);
    assert(cudaMemcpy(dst.data(), device_dst, dst.size() * sizeof(T), cudaMemcpyDeviceToHost) == cudaSuccess);
    assert(cudaFree(device_src) == cudaSuccess);
    assert(cudaFree(device_dst) == cudaSuccess);
    assert(cudaFree(device_offset) == cudaSuccess);
#else
    blockDim.x = 1;
    threadIdx.x = 0;
    for (blockIdx.x = 0; blockIdx.x <= int(dst.size()); blockIdx.x++) {
        gather_learner_rollout(dst.data(), src.data(), horizon, actors, cols,
            actors_per_buf, learners_per_buf, &offset);
    }
#endif
    for (int learner = 0; learner < learners; learner++) {
        int source_learner = (learner + offset) % learners;
        int actor = source_learner / learners_per_buf * actors_per_buf
            + source_learner % learners_per_buf;
        for (int t = 0; t < horizon; t++) {
            for (int col = 0; col < cols; col++) {
                assert(dst[(learner * horizon + t) * cols + col]
                    == T((t * actors + actor) * cols + col + 1));
            }
        }
    }
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

void check_epoch_coverage(int actors_per_buf,int learners_per_buf) {
    const int buffers=2;
    const int actors=buffers*actors_per_buf;
    const int learners=buffers*learners_per_buf, horizon=256, cols=7;
    const int minibatch=8192, segments=minibatch/horizon;
    const int updates=int(0.837464094f*learners*horizon/minibatch);
    const int layers=2, hidden=3;
    if(learners==1024) assert(updates==26);
    const int epochs=learners/std::gcd(learners,updates*segments);
    std::vector<float> src(horizon*actors*cols), dst(horizon*learners*cols);
    std::vector<float> initial(layers*learners*hidden), state(initial.size());
    std::vector<int> coverage(learners,0);
    for(int t=0;t<horizon;t++) for(int row=0;row<actors;row++)
        for(int col=0;col<cols;col++) src[(t*actors+row)*cols+col]=
            row%actors_per_buf<learners_per_buf ? row*8+col : -1;
    for(int layer=0;layer<layers;layer++) for(int row=0;row<learners;row++)
        for(int h=0;h<hidden;h++) initial[(layer*learners+row)*hidden+h]=layer*10000+row*8+h;
    int offset=0;
#ifdef __CUDACC__
    float *device_src,*device_dst,*device_initial,*device_state;
    int* device_offset;
    assert(cudaMalloc((void**)&device_src,src.size()*sizeof(float))==cudaSuccess);
    assert(cudaMalloc((void**)&device_dst,dst.size()*sizeof(float))==cudaSuccess);
    assert(cudaMalloc((void**)&device_initial,initial.size()*sizeof(float))==cudaSuccess);
    assert(cudaMalloc((void**)&device_state,state.size()*sizeof(float))==cudaSuccess);
    assert(cudaMalloc((void**)&device_offset,sizeof(int))==cudaSuccess);
    assert(cudaMemcpy(device_src,src.data(),src.size()*sizeof(float),cudaMemcpyHostToDevice)==cudaSuccess);
    assert(cudaMemcpy(device_initial,initial.data(),initial.size()*sizeof(float),cudaMemcpyHostToDevice)==cudaSuccess);
    cudaStream_t stream;
    cudaGraph_t graph;
    cudaGraphExec_t executable;
    assert(cudaStreamCreate(&stream)==cudaSuccess);
    assert(cudaStreamBeginCapture(stream,cudaStreamCaptureModeThreadLocal)==cudaSuccess);
    gather_learner_rollout<<<(dst.size()+255)/256,256,0,stream>>>(
        device_dst,device_src,horizon,actors,cols,actors_per_buf,learners_per_buf,device_offset);
    gather_learner_state<<<(state.size()+255)/256,256,0,stream>>>(
        Prec{device_state,{layers,learners,hidden}},Prec{device_initial,{layers,learners,hidden}},device_offset);
    assert(cudaStreamEndCapture(stream,&graph)==cudaSuccess);
    assert(cudaGraphInstantiate(&executable,graph,0)==cudaSuccess);
#endif
    for(int epoch=0;epoch<epochs;epoch++) {
        offset=__EPOCH_OFFSET__;
#ifdef __CUDACC__
        assert(cudaMemcpyAsync(device_offset,&offset,sizeof(int),cudaMemcpyHostToDevice,stream)==cudaSuccess);
        assert(cudaGraphLaunch(executable,stream)==cudaSuccess);
        assert(cudaStreamSynchronize(stream)==cudaSuccess);
        assert(cudaMemcpy(dst.data(),device_dst,dst.size()*sizeof(float),cudaMemcpyDeviceToHost)==cudaSuccess);
        assert(cudaMemcpy(state.data(),device_state,state.size()*sizeof(float),cudaMemcpyDeviceToHost)==cudaSuccess);
#else
        blockDim.x=1;
        threadIdx.x=0;
        for(blockIdx.x=0;blockIdx.x<=int(dst.size());blockIdx.x++)
            gather_learner_rollout(dst.data(),src.data(),horizon,actors,cols,actors_per_buf,learners_per_buf,&offset);
        for(blockIdx.x=0;blockIdx.x<=int(state.size());blockIdx.x++)
            gather_learner_state(Prec{state.data(),{layers,learners,hidden}},
                Prec{initial.data(),{layers,learners,hidden}},&offset);
#endif
        for(int row=0;row<learners;row++) {
            int expected=(row+offset)%learners;
            int actor=expected/learners_per_buf*actors_per_buf+expected%learners_per_buf;
            for(int t=0;t<horizon;t++) for(int col=0;col<cols;col++)
                assert(dst[(row*horizon+t)*cols+col]==actor*8+col);
            for(int layer=0;layer<layers;layer++) for(int h=0;h<hidden;h++)
                assert(state[(layer*learners+row)*hidden+h]==layer*10000+expected*8+h);
            if(row<updates*segments) coverage[expected]++;
        }
    }
    for(int count:coverage) assert(count==epochs*updates*segments/learners);
#ifdef __CUDACC__
    assert(cudaGraphExecDestroy(executable)==cudaSuccess);
    assert(cudaGraphDestroy(graph)==cudaSuccess);
    assert(cudaStreamDestroy(stream)==cudaSuccess);
    assert(cudaFree(device_src)==cudaSuccess);
    assert(cudaFree(device_dst)==cudaSuccess);
    assert(cudaFree(device_initial)==cudaSuccess);
    assert(cudaFree(device_state)==cudaSuccess);
    assert(cudaFree(device_offset)==cudaSuccess);
#endif
}

int main() {
    for (int buffers : {1, 2}) {
        for (int learners_per_buf : {1, 3, 4}) {
            check_states(buffers, learners_per_buf);
            for (int horizon : {1, 8}) {
                for (int cols : {1, 3, 17}) {
                    for(int offset : {0,1,buffers*learners_per_buf-1}) {
                    check_rollout<float>(buffers, learners_per_buf, horizon, cols, offset);
                    check_rollout<unsigned short>(buffers, learners_per_buf, horizon, cols, offset);
#ifdef __CUDACC__
                    check_rollout<__nv_bfloat16>(buffers, learners_per_buf, horizon, cols, offset);
#endif
                    }
                }
            }
        }
    }
    check_epoch_coverage(512,512);
    check_epoch_coverage(512,384);
}
""".replace("__EPOCH_OFFSET__", rotation))
    executable = tmp_path / "learner_rollout"
    subprocess.run([compiler, "-std=c++17", "-O2", str(harness), "-o", str(executable)], check=True)
    subprocess.run([str(executable)], check=True)


def test_learner_gather_large_source_and_destination_indices(tmp_path: Path) -> None:
    source = (Path(__file__).resolve().parents[1] / "src/pufferl.cu").read_text()
    gather = source[source.index("template <typename T>\n__global__ void gather_learner_rollout"):
                    source.index("__global__ void gather_learner_state")]
    grid = source[source.index("int grid_size("):source.index("// Compile vs a single env")]
    harness = tmp_path / "large_gather.cpp"
    harness.write_text(r"""
#include <cassert>
#include <cstdint>
#include <sys/mman.h>
#define __global__
#define BLOCK_SIZE 256
struct { int x; } blockIdx, blockDim, threadIdx;
""" + grid + gather + r"""
int main() {
    int horizon=256, actors=16384, apb=8192, lpb=6822, cols=1003, offset=0;
    int learners=2*lpb;
    int64_t src_n=(int64_t)horizon*actors*cols;
    int64_t dst_n=(int64_t)horizon*learners*cols;
    assert(src_n>INT32_MAX && dst_n>INT32_MAX);
    float* src=(float*)mmap(nullptr,src_n*sizeof(float),PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANON,-1,0);
    float* dst=(float*)mmap(nullptr,dst_n*sizeof(float),PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANON,-1,0);
    assert(src!=MAP_FAILED && dst!=MAP_FAILED);
    int64_t idx=dst_n-1;
    int actor=apb+lpb-1;
    int64_t source_idx=((int64_t)(horizon-1)*actors+actor)*cols+cols-1;
    src[source_idx]=73;
    blockDim.x=256;
    blockIdx.x=idx/256;
    threadIdx.x=idx%256;
    assert(grid_size(dst_n)==(dst_n+255)/256);
    gather_learner_rollout(dst,src,horizon,actors,cols,apb,lpb,&offset);
    assert(dst[idx]==73);
    assert(munmap(src,src_n*sizeof(float))==0);
    assert(munmap(dst,dst_n*sizeof(float))==0);
}
""")
    binary = tmp_path / "large_gather"
    subprocess.run(["clang++", "-std=c++17", "-O2", str(harness), "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
