/**
 * @fileoverview Metal kernel dispatch using PyTorch MPS native stream pattern.
 *
 * Uses dispatch_sync on the MPS serial queue with stream->commandEncoder() for
 * encoding. Tensors are bound via getMTLBufferStorage() — zero-copy access to
 * MPS tensor backing storage on Apple Silicon unified memory. MPS manages command
 * buffer lifecycle: no manual commit, no waitUntilCompleted, no GPU starvation.
 *
 * MEMORY MANAGEMENT: compiled with -fno-objc-arc. No manual Metal buffer
 * allocation — all data lives in MPS tensors whose backing storage is accessed
 * directly by the GPU. PyTorch's MPS allocator pools memory for efficiency.
 *
 * The actual MSL kernel code lives in metal_shader_src.h and is JIT-compiled
 * once at metal_init() time by metal_context.mm.
 */

#import <Metal/Metal.h>
#import <Foundation/Foundation.h>

#include <torch/extension.h>
#include <ATen/mps/MPSStream.h>
#include "metal_context.h"
#include "metal_kernels.h"

// Pipeline state cache from metal_context.mm
namespace metal_ctx {
    extern id<MTLDevice> g_device;
    id<MTLComputePipelineState> get_pipeline(const std::string& name);
}

// --- MPS tensor helpers ------------------------------------------------

// Zero-copy: MPS tensor storage -> Metal buffer handle. No allocation, no copy.
// This is the same pattern PyTorch uses internally (getMTLBufferStorage).
static inline id<MTLBuffer> mps_buf(const torch::Tensor& t) {
    return __builtin_bit_cast(id<MTLBuffer>, t.storage().data());
}

// Bind MPS tensor to encoder slot with proper storage offset.
static inline void set_buf(id<MTLComputeCommandEncoder> enc, const torch::Tensor& t, unsigned idx) {
    [enc setBuffer:mps_buf(t) offset:t.storage_offset() * t.element_size() atIndex:idx];
}

// Encode constant data directly into the encoder's argument table.
template<typename T>
static inline void set_params(id<MTLComputeCommandEncoder> enc, const T& params, uint32_t index) {
    [enc setBytes:&params length:sizeof(T) atIndex:index];
}

// Ensure tensor is on MPS, fp32, contiguous. Fast-path returns immediately
// if already satisfied, avoiding PyTorch dispatch overhead.
static torch::Tensor to_mps(const torch::Tensor& t) {
    if (t.is_mps() && t.scalar_type() == torch::kFloat32 && t.is_contiguous()) return t;
    return t.to(torch::kMPS).to(torch::kFloat32).contiguous();
}

// Ensure tensor is on MPS, int32, contiguous.
static torch::Tensor to_mps_i32(const torch::Tensor& t) {
    if (t.is_mps() && t.scalar_type() == torch::kInt32 && t.is_contiguous()) return t;
    return t.to(torch::kMPS).to(torch::kInt32).contiguous();
}

// Allocate uninitialized MPS tensor.
static torch::Tensor mps_empty(c10::IntArrayRef sizes, torch::ScalarType dtype = torch::kFloat32) {
    return torch::empty(sizes, torch::TensorOptions().dtype(dtype).device(torch::kMPS));
}

// TensorOptions shortcuts
static const auto mps_f32 = torch::TensorOptions().dtype(torch::kFloat32).device(torch::kMPS);
static const auto mps_i32 = torch::TensorOptions().dtype(torch::kInt32).device(torch::kMPS);

// ============================================================================
// MinGRU inference
// ============================================================================

std::vector<torch::Tensor> mingru_gate_metal(torch::Tensor state, torch::Tensor combined) {
    TORCH_CHECK(metal_is_ready(), "Metal context not initialized");
    @autoreleasepool {

    auto state_m = to_mps(state);
    auto combined_m = to_mps(combined);

    int B = state_m.size(0);
    int H = state_m.size(-1);
    int N = B * H;

    auto out = mps_empty({B, H});
    auto next_st = mps_empty({B, H});

    struct { int H; int B; } params = { H, B };

    auto pso = metal_ctx::get_pipeline("mingru_gate_inference");
    auto* stream = at::mps::getCurrentMPSStream();
    dispatch_sync(stream->queue(), ^{
        auto enc = stream->commandEncoder();
        [enc setComputePipelineState:pso];
        set_buf(enc, out, 0);
        set_buf(enc, next_st, 1);
        set_buf(enc, combined_m, 2);
        set_buf(enc, state_m, 3);
        set_params(enc, params, 4);

        NSUInteger tg = MIN((NSUInteger)pso.maxTotalThreadsPerThreadgroup, 256);
        [enc dispatchThreads:MTLSizeMake(N, 1, 1) threadsPerThreadgroup:MTLSizeMake(tg, 1, 1)];
    });

    return {out, next_st};
    } // @autoreleasepool
}

// ============================================================================
// Fused scan forward (MinGRU training)
// ============================================================================

std::vector<torch::Tensor> fused_scan_forward_metal(
    torch::Tensor combined, torch::Tensor state) {
    TORCH_CHECK(metal_is_ready(), "Metal context not initialized");
    @autoreleasepool {

    auto combined_m = to_mps(combined);
    auto state_m = to_mps(state);

    int B = state_m.size(0);
    int H = state_m.size(-1);
    int T = combined_m.size(1);
    int N = B * H;
    int T_buf = T + 1;

    auto out = mps_empty({B, T, H});
    auto next_state = mps_empty({B, H});
    auto astar = mps_empty({B, T_buf, H});
    auto s_out = mps_empty({B, T_buf, H});
    auto lv = mps_empty({B, T_buf, H});

    struct { int T_seq; int H; int B; } params = { T, H, B };

    auto pso = metal_ctx::get_pipeline("fused_scan_forward_checkpointed");
    auto* stream = at::mps::getCurrentMPSStream();
    dispatch_sync(stream->queue(), ^{
        auto enc = stream->commandEncoder();
        [enc setComputePipelineState:pso];
        set_buf(enc, out, 0);
        set_buf(enc, next_state, 1);
        set_buf(enc, astar, 2);
        set_buf(enc, s_out, 3);
        set_buf(enc, lv, 4);
        set_buf(enc, combined_m, 5);
        set_buf(enc, state_m, 6);
        set_params(enc, params, 7);

        NSUInteger tg = MIN((NSUInteger)pso.maxTotalThreadsPerThreadgroup, 256);
        [enc dispatchThreads:MTLSizeMake(N, 1, 1) threadsPerThreadgroup:MTLSizeMake(tg, 1, 1)];
    });

    return {out, next_state, astar, s_out, lv};
    } // @autoreleasepool
}

// ============================================================================
// Fused scan backward (MinGRU training)
// ============================================================================

std::vector<torch::Tensor> fused_scan_backward_metal(
    torch::Tensor grad_out, torch::Tensor grad_next_state,
    torch::Tensor combined, torch::Tensor state,
    torch::Tensor a_star_buf, torch::Tensor s_buf, torch::Tensor log_values_buf) {
    TORCH_CHECK(metal_is_ready(), "Metal context not initialized");
    @autoreleasepool {

    auto grad_out_m = to_mps(grad_out);
    auto grad_ns_m = to_mps(grad_next_state);
    auto combined_m = to_mps(combined);
    auto state_m = to_mps(state);
    auto astar_m = to_mps(a_star_buf);
    auto s_m = to_mps(s_buf);
    auto lv_m = to_mps(log_values_buf);

    int B = state_m.size(0);
    int H = state_m.size(-1);
    int T = combined_m.size(1);
    int N = B * H;

    // grad_combined must match combined's shape [B, T, 3*H] — the kernel writes
    // at 3*H stride (hidden, gate, projection gradients per timestep)
    auto grad_combined = mps_empty(combined_m.sizes().vec());
    auto grad_state = mps_empty(state_m.sizes().vec());

    struct { int T_seq; int H; int B; } params = { T, H, B };

    auto pso = metal_ctx::get_pipeline("fused_scan_backward_checkpointed");
    auto* stream = at::mps::getCurrentMPSStream();
    dispatch_sync(stream->queue(), ^{
        auto enc = stream->commandEncoder();
        [enc setComputePipelineState:pso];
        set_buf(enc, grad_combined, 0);
        set_buf(enc, grad_state, 1);
        set_buf(enc, grad_out_m, 2);
        set_buf(enc, grad_ns_m, 3);
        set_buf(enc, combined_m, 4);
        set_buf(enc, state_m, 5);
        set_buf(enc, astar_m, 6);
        set_buf(enc, s_m, 7);
        set_buf(enc, lv_m, 8);
        set_params(enc, params, 9);

        NSUInteger tg = MIN((NSUInteger)pso.maxTotalThreadsPerThreadgroup, 256);
        [enc dispatchThreads:MTLSizeMake(N, 1, 1) threadsPerThreadgroup:MTLSizeMake(tg, 1, 1)];
    });

    return {grad_combined, grad_state};
    } // @autoreleasepool
}

// ============================================================================
// LogCumsumExp
// ============================================================================

std::vector<torch::Tensor> logcumsumexp_forward_metal(torch::Tensor x) {
    TORCH_CHECK(metal_is_ready(), "Metal context not initialized");
    @autoreleasepool {

    auto x_m = to_mps(x);
    int B = x_m.size(0);
    int T = x_m.size(1);
    int H = x_m.size(2);
    int N = B * H;

    auto out = mps_empty({B, T, H});
    auto s_out = mps_empty({B, T, H});

    struct { int T_total; int H; int B; } params = { T, H, B };

    auto pso = metal_ctx::get_pipeline("logcumsumexp_forward_kernel");
    auto* stream = at::mps::getCurrentMPSStream();
    dispatch_sync(stream->queue(), ^{
        auto enc = stream->commandEncoder();
        [enc setComputePipelineState:pso];
        set_buf(enc, out, 0);
        set_buf(enc, s_out, 1);
        set_buf(enc, x_m, 2);
        set_params(enc, params, 3);

        NSUInteger tg = MIN((NSUInteger)pso.maxTotalThreadsPerThreadgroup, 256);
        [enc dispatchThreads:MTLSizeMake(N, 1, 1) threadsPerThreadgroup:MTLSizeMake(tg, 1, 1)];
    });

    return {out, s_out};
    } // @autoreleasepool
}

torch::Tensor logcumsumexp_backward_metal(
    torch::Tensor grad_out, torch::Tensor x, torch::Tensor s_buf) {
    TORCH_CHECK(metal_is_ready(), "Metal context not initialized");
    @autoreleasepool {

    auto grad_m = to_mps(grad_out);
    auto x_m = to_mps(x);
    auto s_m = to_mps(s_buf);
    int B = x_m.size(0);
    int T = x_m.size(1);
    int H = x_m.size(2);
    int N = B * H;

    auto grad_x = mps_empty({B, T, H});

    struct { int T_total; int H; int B; } params = { T, H, B };

    auto pso = metal_ctx::get_pipeline("logcumsumexp_backward_kernel");
    auto* stream = at::mps::getCurrentMPSStream();
    dispatch_sync(stream->queue(), ^{
        auto enc = stream->commandEncoder();
        [enc setComputePipelineState:pso];
        set_buf(enc, grad_x, 0);
        set_buf(enc, grad_m, 1);
        set_buf(enc, x_m, 2);
        set_buf(enc, s_m, 3);
        set_params(enc, params, 4);

        NSUInteger tg = MIN((NSUInteger)pso.maxTotalThreadsPerThreadgroup, 256);
        [enc dispatchThreads:MTLSizeMake(N, 1, 1) threadsPerThreadgroup:MTLSizeMake(tg, 1, 1)];
    });

    return grad_x;
    } // @autoreleasepool
}

// ============================================================================
// Sample logits
// ============================================================================

void sample_logits_metal(
    torch::Tensor logits, torch::Tensor logstd, torch::Tensor value,
    torch::Tensor actions_out, torch::Tensor logprobs_out, torch::Tensor value_out,
    torch::Tensor act_sizes, uint64_t seed, torch::Tensor offset,
    torch::Tensor mask) {
    TORCH_CHECK(metal_is_ready(), "Metal context not initialized");
    @autoreleasepool {

    auto logits_m = to_mps(logits);
    auto value_m = to_mps(value);
    auto act_sizes_m = to_mps_i32(act_sizes);

    bool is_continuous = logstd.defined() && logstd.numel() > 0;
    auto logstd_m = is_continuous ? to_mps(logstd) : torch::zeros({1}, mps_f32);

    int B = logits_m.size(0);
    int num_atns = act_sizes_m.size(0);

    // Cache act_sizes sum and RNG offset tensor (constant across training)
    static int cached_num_atns_total = -1;
    static torch::Tensor persistent_offset;
    if (cached_num_atns_total < 0) {
        cached_num_atns_total = (int)act_sizes.to(torch::kInt32).sum().item<int>();
        persistent_offset = torch::zeros({1}, mps_i32);
    }
    int num_atns_total = cached_num_atns_total;

    // Prepare mask tensor on MPS (use provided mask or all-ones fallback)
    auto mask_m = (mask.defined() && mask.numel() > 0)
        ? to_mps(mask)
        : torch::ones({B, num_atns_total}, mps_f32);

    // Bind caller's output tensors directly if they're MPS fp32 contiguous,
    // avoiding intermediate allocation + copy_. Otherwise allocate and copy.
    bool lp_direct = logprobs_out.is_mps() && logprobs_out.scalar_type() == torch::kFloat32 && logprobs_out.is_contiguous();
    bool vo_direct = value_out.is_mps() && value_out.scalar_type() == torch::kFloat32 && value_out.is_contiguous();

    auto lp_buf = lp_direct ? logprobs_out : mps_empty({B});
    auto vo_buf = vo_direct ? value_out : mps_empty({B});
    // actions always need intermediate (kernel writes float, caller expects int)
    auto act_buf = mps_empty({B, num_atns});
    auto& offset_m = persistent_offset;

    struct {
        uint64_t seed;
        int num_atns;
        int num_atns_total;
        int B;
        int logits_stride;
        int logstd_stride;
        int value_stride;
        int is_continuous;
    } params = {
        seed, num_atns, num_atns_total, B,
        (int)logits_m.stride(0),
        is_continuous ? (int)logstd_m.stride(0) : 0,
        (int)value_m.stride(0),
        is_continuous ? 1 : 0
    };

    auto pso = metal_ctx::get_pipeline("sample_logits_kernel");
    auto* stream = at::mps::getCurrentMPSStream();
    dispatch_sync(stream->queue(), ^{
        auto enc = stream->commandEncoder();
        [enc setComputePipelineState:pso];
        set_buf(enc, act_buf, 0);
        set_buf(enc, lp_buf, 1);
        set_buf(enc, vo_buf, 2);
        set_buf(enc, logits_m, 3);
        set_buf(enc, logstd_m, 4);
        set_buf(enc, value_m, 5);
        set_buf(enc, act_sizes_m, 6);
        set_buf(enc, offset_m, 7);
        set_params(enc, params, 8);
        set_buf(enc, mask_m, 9);

        NSUInteger tg = MIN((NSUInteger)pso.maxTotalThreadsPerThreadgroup, 256);
        [enc dispatchThreads:MTLSizeMake(B, 1, 1) threadsPerThreadgroup:MTLSizeMake(tg, 1, 1)];
    });

    // Only copy where we couldn't bind directly
    actions_out.copy_(act_buf.to(actions_out.dtype()));
    if (!lp_direct) logprobs_out.copy_(lp_buf);
    if (!vo_direct) value_out.copy_(vo_buf);
    } // @autoreleasepool
}

// ============================================================================
// PPO loss forward
// ============================================================================

std::vector<torch::Tensor> ppo_loss_forward_metal(
    torch::Tensor logits, torch::Tensor logstd,
    torch::Tensor values_pred, torch::Tensor actions,
    torch::Tensor old_logprobs, torch::Tensor advantages,
    torch::Tensor prio, torch::Tensor values, torch::Tensor returns,
    torch::Tensor ratio_out, torch::Tensor newvalue_out,
    torch::Tensor act_sizes, torch::Tensor losses_acc,
    double clip_coef, double vf_clip_coef, double vf_coef, double ent_coef,
    bool is_continuous) {
    TORCH_CHECK(metal_is_ready(), "Metal context not initialized");
    @autoreleasepool {

    // Compute advantage stats on MPS (stays async until we need CPU values)
    auto adv_fp32 = advantages.to(torch::kMPS).to(torch::kFloat32);
    auto adv_mean = adv_fp32.mean();
    auto adv_var = adv_fp32.var();

    auto logits_m = to_mps(logits);
    auto vp_m = to_mps(values_pred);
    auto act_m = to_mps(actions);
    auto olp_m = to_mps(old_logprobs);
    auto adv_m = adv_fp32.contiguous();
    auto prio_m = to_mps(prio);
    auto val_m = to_mps(values);
    auto ret_m = to_mps(returns);
    auto as_m = to_mps_i32(act_sizes);
    auto logstd_m = (is_continuous && logstd.defined())
        ? to_mps(logstd)
        : torch::zeros({1}, mps_f32);

    int N = logits_m.size(0);
    int T_seq = logits_m.size(1);
    int total_elements = N * T_seq;

    // A_total computed on CPU to avoid triggering a sync
    int A_total = (int)act_sizes.to(torch::kInt32).sum().item<int>();

    // Atomic accumulation buffers — zero-initialized
    auto loss_t = torch::zeros({1}, mps_i32);
    auto losses_t = torch::zeros({8}, mps_i32);

    struct {
        float clip_coef; float vf_clip_coef; float vf_coef; float ent_coef;
        int T_seq; int A_total; int N;
        int logits_stride_n; int logits_stride_t; int logits_stride_a;
        int values_stride_n; int values_stride_t;
        int num_atns; int is_continuous;
    } params = {
        (float)clip_coef, (float)vf_clip_coef, (float)vf_coef, (float)ent_coef,
        T_seq,
        A_total,
        N,
        (int)logits_m.stride(0), (int)logits_m.stride(1),
        logits_m.dim() >= 3 ? (int)logits_m.stride(2) : 1,
        (int)vp_m.stride(0),
        vp_m.dim() >= 2 ? (int)vp_m.stride(1) : 1,
        (int)as_m.size(0),
        is_continuous ? 1 : 0
    };

    auto pso = metal_ctx::get_pipeline("ppo_loss_forward_kernel");
    auto* stream = at::mps::getCurrentMPSStream();
    dispatch_sync(stream->queue(), ^{
        auto enc = stream->commandEncoder();
        [enc setComputePipelineState:pso];
        set_buf(enc, loss_t, 0);
        set_buf(enc, losses_t, 1);
        set_buf(enc, logits_m, 2);
        set_buf(enc, logstd_m, 3);
        set_buf(enc, vp_m, 4);
        set_buf(enc, act_m, 5);
        set_buf(enc, olp_m, 6);
        set_buf(enc, adv_m, 7);
        set_buf(enc, prio_m, 8);
        set_buf(enc, val_m, 9);
        set_buf(enc, ret_m, 10);
        set_buf(enc, adv_mean, 11);
        set_buf(enc, adv_var, 12);
        set_buf(enc, as_m, 13);
        set_params(enc, params, 14);

        NSUInteger tg = 256;
        NSUInteger num_groups = (total_elements + tg - 1) / tg;
        [enc dispatchThreadgroups:MTLSizeMake(num_groups, 1, 1) threadsPerThreadgroup:MTLSizeMake(tg, 1, 1)];
    });

    // Write ratio_out and newvalue_out — used by V-trace advantage computation
    // in subsequent minibatches. The Metal kernel doesn't expose these, so we
    // compute them here using MPS tensor ops.
    {
        int batch = N * T_seq;
        auto flat_logits = torch::nan_to_num(logits_m.reshape({batch, -1}), 1e-8, 1e-8, 1e-8);
        auto as_cpu = act_sizes.to(torch::kCPU).to(torch::kInt64).contiguous();
        int num_heads = (int)actions.size(-1);
        auto split = torch::split(flat_logits,
            c10::IntArrayRef(as_cpu.data_ptr<int64_t>(), num_heads), 1);
        std::vector<torch::Tensor> lp_vec;
        for (int h = 0; h < num_heads; h++) {
            auto log_p = torch::log_softmax(split[h], 1);
            auto ha = act_m.select(-1, h).reshape({batch}).to(torch::kInt64);
            lp_vec.push_back(log_p.gather(1, ha.unsqueeze(1)));
        }
        auto logprob = torch::cat(lp_vec, 1).sum(1);  // (batch,)
        auto ratio = (logprob.reshape({N, T_seq}) - olp_m).exp();
        ratio_out.copy_(ratio.to(ratio_out.dtype()));
        newvalue_out.copy_(vp_m.squeeze(-1).to(newvalue_out.dtype()));
    }

    // loss_t/losses_t are int32 MPS buffers with atomically accumulated float bits
    // (CAS loop in kernel). Must bitcast int32→float32 via .view(kFloat32) on CPU
    // because MPS doesn't support cross-dtype view. The .to(kCPU) syncs the MPS
    // stream (one sync per minibatch — acceptable since it's only 9 elements).
    auto loss_cpu = loss_t.to(torch::kCPU).view(torch::kFloat32);
    auto losses_cpu = losses_t.to(torch::kCPU).view(torch::kFloat32);

    auto losses_acc_cpu = losses_acc.to(torch::kCPU);
    losses_acc_cpu.add_(losses_cpu.narrow(0, 0, losses_acc_cpu.size(0)));
    // Increment minibatch counter at index 7 (LOSS_N) — PufferLib divides
    // accumulated losses by this count for averaging. Without it, n=0 and
    // log_losses() returns empty dict.
    losses_acc_cpu.select(0, 7).add_(1.0);
    losses_acc.copy_(losses_acc_cpu.to(losses_acc.device()));

    auto loss_out = loss_cpu.to(logits.device());
    return {loss_out};
    } // @autoreleasepool
}

// ============================================================================
// PPO loss backward
// ============================================================================

std::vector<torch::Tensor> ppo_loss_backward_metal(
    torch::Tensor grad_loss,
    torch::Tensor logits, torch::Tensor logstd,
    torch::Tensor values_pred, torch::Tensor actions,
    torch::Tensor old_logprobs, torch::Tensor advantages,
    torch::Tensor prio, torch::Tensor values, torch::Tensor returns,
    torch::Tensor act_sizes,
    double clip_coef, double vf_clip_coef, double vf_coef, double ent_coef,
    bool is_continuous) {
    TORCH_CHECK(metal_is_ready(), "Metal context not initialized");
    @autoreleasepool {

    auto adv_fp32 = advantages.to(torch::kMPS).to(torch::kFloat32);
    auto adv_mean = adv_fp32.mean();
    auto adv_var = adv_fp32.var();

    auto grad_m = to_mps(grad_loss);
    auto logits_m = to_mps(logits);
    auto vp_m = to_mps(values_pred);
    auto act_m = to_mps(actions);
    auto olp_m = to_mps(old_logprobs);
    auto adv_m = adv_fp32.contiguous();
    auto prio_m = to_mps(prio);
    auto val_m = to_mps(values);
    auto ret_m = to_mps(returns);
    auto as_m = to_mps_i32(act_sizes);
    auto logstd_m = (is_continuous && logstd.defined())
        ? to_mps(logstd)
        : torch::zeros({1}, mps_f32);

    int N = logits_m.size(0);
    int T_seq = logits_m.size(1);
    int total_elements = N * T_seq;
    int A_total = (int)act_sizes.to(torch::kInt32).sum().item<int>();

    auto grad_logits = mps_empty(logits_m.sizes().vec());
    auto gls_t = is_continuous ? mps_empty(logstd_m.sizes().vec()) : mps_empty({1});
    auto grad_vp = mps_empty(vp_m.sizes().vec());

    struct {
        float clip_coef; float vf_clip_coef; float vf_coef; float ent_coef;
        int T_seq; int A_total; int N;
        int logits_stride_n; int logits_stride_t; int logits_stride_a;
        int values_stride_n; int values_stride_t;
        int num_atns; int is_continuous;
    } params = {
        (float)clip_coef, (float)vf_clip_coef, (float)vf_coef, (float)ent_coef,
        T_seq,
        A_total,
        N,
        (int)logits_m.stride(0), (int)logits_m.stride(1),
        logits_m.dim() >= 3 ? (int)logits_m.stride(2) : 1,
        (int)vp_m.stride(0),
        vp_m.dim() >= 2 ? (int)vp_m.stride(1) : 1,
        (int)as_m.size(0),
        is_continuous ? 1 : 0
    };

    auto pso = metal_ctx::get_pipeline("ppo_loss_backward_kernel");
    auto* stream = at::mps::getCurrentMPSStream();
    dispatch_sync(stream->queue(), ^{
        auto enc = stream->commandEncoder();
        [enc setComputePipelineState:pso];
        set_buf(enc, grad_logits, 0);
        set_buf(enc, gls_t, 1);
        set_buf(enc, grad_vp, 2);
        set_buf(enc, grad_m, 3);
        set_buf(enc, logits_m, 4);
        set_buf(enc, logstd_m, 5);
        set_buf(enc, vp_m, 6);
        set_buf(enc, act_m, 7);
        set_buf(enc, olp_m, 8);
        set_buf(enc, adv_m, 9);
        set_buf(enc, prio_m, 10);
        set_buf(enc, val_m, 11);
        set_buf(enc, ret_m, 12);
        set_buf(enc, adv_mean, 13);
        set_buf(enc, adv_var, 14);
        set_buf(enc, as_m, 15);
        set_params(enc, params, 16);

        NSUInteger tg = 256;
        [enc dispatchThreads:MTLSizeMake(total_elements, 1, 1) threadsPerThreadgroup:MTLSizeMake(tg, 1, 1)];
    });

    // grad tensors are already on MPS — only transfer if logits came from elsewhere
    if (logits.is_mps()) {
        auto grad_logstd = is_continuous
            ? gls_t : torch::empty({0}, mps_f32);
        return {grad_logits, grad_logstd, grad_vp};
    }
    auto orig_device = logits.device();
    auto grad_logstd = is_continuous
        ? gls_t.to(orig_device)
        : torch::empty({0}, torch::TensorOptions().device(orig_device));
    return {grad_logits.to(orig_device), grad_logstd, grad_vp.to(orig_device)};
    } // @autoreleasepool
}

// ============================================================================
// Advantage
// ============================================================================

namespace pufferlib {
void puff_advantage_metal(
    torch::Tensor values, torch::Tensor rewards,
    torch::Tensor dones, torch::Tensor importance, torch::Tensor advantages,
    double gamma, double lambda, double rho_clip, double c_clip) {
    TORCH_CHECK(metal_is_ready(), "Metal context not initialized");
    @autoreleasepool {

    auto v_m = to_mps(values);
    auto r_m = to_mps(rewards);
    auto d_m = to_mps(dones);
    auto i_m = to_mps(importance);

    int num_steps = v_m.size(0);
    int horizon = v_m.size(1);

    auto adv_out = mps_empty({num_steps, horizon});

    struct {
        float gamma; float lambda; float rho_clip; float c_clip;
        int num_steps; int horizon;
    } params = {
        (float)gamma, (float)lambda, (float)rho_clip, (float)c_clip,
        num_steps, horizon
    };

    auto pso = metal_ctx::get_pipeline("puff_advantage_kernel");
    auto* stream = at::mps::getCurrentMPSStream();
    dispatch_sync(stream->queue(), ^{
        auto enc = stream->commandEncoder();
        [enc setComputePipelineState:pso];
        set_buf(enc, v_m, 0);
        set_buf(enc, r_m, 1);
        set_buf(enc, d_m, 2);
        set_buf(enc, i_m, 3);
        set_buf(enc, adv_out, 4);
        set_params(enc, params, 5);

        NSUInteger tg = MIN((NSUInteger)pso.maxTotalThreadsPerThreadgroup, 256);
        [enc dispatchThreads:MTLSizeMake(num_steps, 1, 1) threadsPerThreadgroup:MTLSizeMake(tg, 1, 1)];
    });

    advantages.copy_(adv_out.to(advantages.device()));
    } // @autoreleasepool
}
} // namespace pufferlib

// ============================================================================
// Select + Copy (CPU fallback — memory-bound, not compute-bound)
// ============================================================================

void train_select_and_copy_metal(
    torch::Tensor observations, torch::Tensor actions,
    torch::Tensor logprobs, torch::Tensor values, torch::Tensor advantages,
    torch::Tensor idx, torch::Tensor mb_prio,
    torch::Tensor dst_obs, torch::Tensor dst_state,
    torch::Tensor dst_actions, torch::Tensor dst_logprobs,
    torch::Tensor dst_advantages, torch::Tensor dst_prio,
    torch::Tensor dst_values, torch::Tensor dst_returns) {
    TORCH_CHECK(metal_is_ready(), "Metal context not initialized");
    @autoreleasepool {

    // Vectorized index gather on MPS — replaces per-row CPU loop.
    // index_select is a single MPS kernel dispatch per tensor.
    auto idx_m = idx.to(torch::kMPS).to(torch::kLong);

    dst_obs.copy_(observations.to(torch::kMPS).index_select(0, idx_m));
    dst_state.zero_();
    dst_actions.copy_(actions.to(torch::kMPS).index_select(0, idx_m));
    dst_logprobs.copy_(logprobs.to(torch::kMPS).index_select(0, idx_m));

    auto vals = values.to(torch::kMPS).to(torch::kFloat32);
    auto advs = advantages.to(torch::kMPS).to(torch::kFloat32);
    dst_values.copy_(vals.index_select(0, idx_m));
    dst_advantages.copy_(advs.index_select(0, idx_m));
    dst_returns.copy_((vals + advs).index_select(0, idx_m));
    dst_prio.copy_(mb_prio.to(torch::kMPS).to(torch::kFloat32));
    } // @autoreleasepool
}

// ============================================================================
// Priority replay
// ============================================================================

std::tuple<torch::Tensor, torch::Tensor> prio_replay_metal(
    torch::Tensor advantages, float prio_alpha,
    int minibatch_segments, int total_agents, float anneal_beta) {
    TORCH_CHECK(metal_is_ready(), "Metal context not initialized");
    @autoreleasepool {

    auto adv_m = to_mps(advantages);
    int num_segments = adv_m.size(0);
    int horizon = adv_m.size(1);

    auto pw_t = mps_empty({num_segments});

    struct { float prio_alpha; int stride; } prio_params = { prio_alpha, horizon };
    struct { int length; } norm_params = { num_segments };

    auto pso1 = metal_ctx::get_pipeline("prio_adv_reduction_kernel");
    auto pso2 = metal_ctx::get_pipeline("prio_normalize_kernel");

    // Steps 1+2: reduction then normalize with data dependency barrier
    auto* stream = at::mps::getCurrentMPSStream();
    dispatch_sync(stream->queue(), ^{
        // Step 1: per-segment priority weights
        auto enc = stream->commandEncoder();
        [enc setComputePipelineState:pso1];
        set_buf(enc, adv_m, 0);
        set_buf(enc, pw_t, 1);
        set_params(enc, prio_params, 2);
        [enc dispatchThreadgroups:MTLSizeMake(num_segments, 1, 1)
           threadsPerThreadgroup:MTLSizeMake(32, 1, 1)];

        // Barrier: step 1 must complete before step 2 reads pw_t
        stream->endKernelCoalescing();

        // Step 2: normalize in-place
        enc = stream->commandEncoder();
        [enc setComputePipelineState:pso2];
        set_buf(enc, pw_t, 0);
        set_params(enc, norm_params, 1);
        [enc dispatchThreadgroups:MTLSizeMake(1, 1, 1)
           threadsPerThreadgroup:MTLSizeMake(256, 1, 1)];
    });

    // Step 3: CPU multinomial — .to(kCPU) triggers sync (commit + wait)
    auto prio_weights_cpu = pw_t.to(torch::kCPU);
    auto idx = at::multinomial(prio_weights_cpu, minibatch_segments, true).to(torch::kLong);

    // Step 4: importance weights on GPU
    auto idx_m = idx.to(torch::kMPS).contiguous();
    auto mp_out = mps_empty({minibatch_segments});

    struct { int total_agents; float anneal_beta; int minibatch_segments; } imp_params = {
        total_agents, anneal_beta, minibatch_segments
    };

    auto pso3 = metal_ctx::get_pipeline("prio_imp_weights_kernel");
    dispatch_sync(stream->queue(), ^{
        auto enc = stream->commandEncoder();
        [enc setComputePipelineState:pso3];
        set_buf(enc, idx_m, 0);
        set_buf(enc, pw_t, 1);  // reuse MPS tensor (sync already completed)
        set_buf(enc, mp_out, 2);
        set_params(enc, imp_params, 3);

        NSUInteger tg = MIN((NSUInteger)pso3.maxTotalThreadsPerThreadgroup, 256);
        [enc dispatchThreads:MTLSizeMake(minibatch_segments, 1, 1)
          threadsPerThreadgroup:MTLSizeMake(tg, 1, 1)];
    });

    // idx is on CPU (from multinomial), mp_out is on MPS
    if (advantages.is_mps()) {
        return {idx.to(torch::kMPS), mp_out.unsqueeze(1)};
    }
    auto orig_device = advantages.device();
    return {idx.to(orig_device), mp_out.unsqueeze(1).to(orig_device)};
    } // @autoreleasepool
}
