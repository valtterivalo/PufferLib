/**
 * @fileoverview Metal autograd Function implementations for PrefixScanMetal,
 * LogCumsumExpMetal, and PPOLossMetal.
 *
 * These wrap the Metal kernel dispatch functions (from metal_kernels.mm) into
 * torch::autograd::Function subclasses so they integrate with PyTorch's
 * automatic differentiation.
 */

#include <torch/extension.h>
#include "../modules.h"
#include "metal_kernels.h"

#ifdef WITH_METAL

// ============================================================================
// PrefixScanMetal (MinGRU training forward/backward)
// ============================================================================

tensor_list PrefixScanMetal::forward(AutogradCtx* ctx,
    torch::Tensor combined, torch::Tensor state) {
    // Forward through Metal kernel
    auto results = fused_scan_forward_metal(combined, state);
    // results = {out, next_state, a_star_buf, s_buf, log_values_buf}
    auto out = results[0];
    auto next_state = results[1];
    auto a_star_buf = results[2];
    auto s_buf = results[3];
    auto log_values_buf = results[4];

    ctx->save_for_backward({combined, state, a_star_buf, s_buf, log_values_buf});
    return {out, next_state};
}

tensor_list PrefixScanMetal::backward(AutogradCtx* ctx, tensor_list grad_outputs) {
    auto saved = ctx->get_saved_variables();
    auto combined = saved[0];
    auto state = saved[1];
    auto a_star_buf = saved[2];
    auto s_buf = saved[3];
    auto log_values_buf = saved[4];

    auto grad_out = grad_outputs[0];
    auto grad_next_state = grad_outputs[1];

    auto grads = fused_scan_backward_metal(
        grad_out, grad_next_state, combined, state,
        a_star_buf, s_buf, log_values_buf);

    return {grads[0], grads[1]};  // grad_combined, grad_state
}

// ============================================================================
// LogCumsumExpMetal
// ============================================================================

tensor_list LogCumsumExpMetal::forward(AutogradCtx* ctx, torch::Tensor x) {
    auto results = logcumsumexp_forward_metal(x);
    auto out = results[0];
    auto s_buf = results[1];

    ctx->save_for_backward({x, s_buf});
    return {out};
}

tensor_list LogCumsumExpMetal::backward(AutogradCtx* ctx, tensor_list grad_outputs) {
    auto saved = ctx->get_saved_variables();
    auto x = saved[0];
    auto s_buf = saved[1];

    auto grad_x = logcumsumexp_backward_metal(grad_outputs[0], x, s_buf);
    return {grad_x};
}

// ============================================================================
// PPOLossMetal
// ============================================================================

tensor_list PPOLossMetal::forward(AutogradCtx* ctx,
    torch::Tensor logits, torch::Tensor logstd,
    torch::Tensor values_pred, torch::Tensor actions,
    torch::Tensor old_logprobs, torch::Tensor advantages,
    torch::Tensor prio, torch::Tensor values, torch::Tensor returns,
    torch::Tensor ratio_out, torch::Tensor newvalue_out,
    torch::Tensor act_sizes, torch::Tensor losses_acc,
    double clip_coef, double vf_clip_coef, double vf_coef, double ent_coef) {

    bool is_continuous = logstd.defined() && logstd.numel() > 0;

    auto result = ppo_loss_forward_metal(
        logits, logstd, values_pred, actions,
        old_logprobs, advantages, prio, values, returns,
        ratio_out, newvalue_out, act_sizes, losses_acc,
        clip_coef, vf_clip_coef, vf_coef, ent_coef, is_continuous);

    ctx->save_for_backward({logits, logstd, values_pred, actions,
                            old_logprobs, advantages, prio, values, returns, act_sizes});
    ctx->saved_data["clip_coef"] = clip_coef;
    ctx->saved_data["vf_clip_coef"] = vf_clip_coef;
    ctx->saved_data["vf_coef"] = vf_coef;
    ctx->saved_data["ent_coef"] = ent_coef;
    ctx->saved_data["is_continuous"] = is_continuous;

    return result;
}

tensor_list PPOLossMetal::backward(AutogradCtx* ctx, tensor_list grad_outputs) {
    auto saved = ctx->get_saved_variables();
    auto logits = saved[0];
    auto logstd = saved[1];
    auto values_pred = saved[2];
    auto actions = saved[3];
    auto old_logprobs = saved[4];
    auto advantages = saved[5];
    auto prio = saved[6];
    auto values = saved[7];
    auto returns = saved[8];
    auto act_sizes = saved[9];

    double clip_coef = ctx->saved_data["clip_coef"].toDouble();
    double vf_clip_coef = ctx->saved_data["vf_clip_coef"].toDouble();
    double vf_coef = ctx->saved_data["vf_coef"].toDouble();
    double ent_coef = ctx->saved_data["ent_coef"].toDouble();
    bool is_continuous = ctx->saved_data["is_continuous"].toBool();

    auto grads = ppo_loss_backward_metal(
        grad_outputs[0], logits, logstd, values_pred, actions,
        old_logprobs, advantages, prio, values, returns, act_sizes,
        clip_coef, vf_clip_coef, vf_coef, ent_coef, is_continuous);

    // Return gradients matching forward args:
    // logits, logstd, values_pred, actions, old_logprobs, advantages,
    // prio, values, returns, ratio_out, newvalue_out, act_sizes, losses_acc,
    // clip_coef, vf_clip_coef, vf_coef, ent_coef
    return {grads[0], grads[1], grads[2],
            torch::Tensor(), torch::Tensor(), torch::Tensor(),
            torch::Tensor(), torch::Tensor(), torch::Tensor(),
            torch::Tensor(), torch::Tensor(), torch::Tensor(), torch::Tensor(),
            torch::Tensor(), torch::Tensor(), torch::Tensor(), torch::Tensor()};
}

#endif // WITH_METAL
