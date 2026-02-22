/**
 * @fileoverview All Metal kernel C++ function declarations.
 *
 * Each function dispatches a Metal compute kernel and synchronizes.
 * Tensors must be fp32 contiguous (PRECISION_FLOAT is always set on Darwin).
 * Unified memory means zero-copy bridging between PyTorch and Metal.
 */

#ifndef PUFFERLIB_METAL_KERNELS_H
#define PUFFERLIB_METAL_KERNELS_H

#ifdef WITH_METAL

#include <torch/extension.h>
#include <tuple>
#include <vector>

// --- MinGRU inference ---
std::vector<torch::Tensor> mingru_gate_metal(torch::Tensor state, torch::Tensor combined);

// --- MinGRU training (autograd wrappers defined in modules.h) ---
// Forward: returns {out (B,T,H), next_state (B,1,H)}
std::vector<torch::Tensor> fused_scan_forward_metal(
    torch::Tensor combined, torch::Tensor state);
// Backward: returns {grad_combined, grad_state}
std::vector<torch::Tensor> fused_scan_backward_metal(
    torch::Tensor grad_out, torch::Tensor grad_next_state,
    torch::Tensor combined, torch::Tensor state,
    torch::Tensor a_star_buf, torch::Tensor s_buf, torch::Tensor log_values_buf);

// --- LogCumsumExp ---
std::vector<torch::Tensor> logcumsumexp_forward_metal(torch::Tensor x);
torch::Tensor logcumsumexp_backward_metal(
    torch::Tensor grad_out, torch::Tensor x, torch::Tensor s_buf);

// --- Sampling ---
// mask: (B, num_atns_total) float, 1=valid 0=invalid action
void sample_logits_metal(
    torch::Tensor logits, torch::Tensor logstd, torch::Tensor value,
    torch::Tensor actions_out, torch::Tensor logprobs_out, torch::Tensor value_out,
    torch::Tensor act_sizes, uint64_t seed, torch::Tensor offset,
    torch::Tensor mask);

// --- PPO loss ---
std::vector<torch::Tensor> ppo_loss_forward_metal(
    torch::Tensor logits, torch::Tensor logstd,
    torch::Tensor values_pred, torch::Tensor actions,
    torch::Tensor old_logprobs, torch::Tensor advantages,
    torch::Tensor prio, torch::Tensor values, torch::Tensor returns,
    torch::Tensor ratio_out, torch::Tensor newvalue_out,
    torch::Tensor act_sizes, torch::Tensor losses_acc,
    double clip_coef, double vf_clip_coef, double vf_coef, double ent_coef,
    bool is_continuous);
std::vector<torch::Tensor> ppo_loss_backward_metal(
    torch::Tensor grad_loss,
    torch::Tensor logits, torch::Tensor logstd,
    torch::Tensor values_pred, torch::Tensor actions,
    torch::Tensor old_logprobs, torch::Tensor advantages,
    torch::Tensor prio, torch::Tensor values, torch::Tensor returns,
    torch::Tensor act_sizes,
    double clip_coef, double vf_clip_coef, double vf_coef, double ent_coef,
    bool is_continuous);

// --- Advantage ---
namespace pufferlib {
void puff_advantage_metal(
    torch::Tensor values, torch::Tensor rewards,
    torch::Tensor dones, torch::Tensor importance, torch::Tensor advantages,
    double gamma, double lambda, double rho_clip, double c_clip);
}

// --- Select + Copy ---
void train_select_and_copy_metal(
    torch::Tensor observations, torch::Tensor actions,
    torch::Tensor logprobs, torch::Tensor values, torch::Tensor advantages,
    torch::Tensor idx, torch::Tensor mb_prio,
    torch::Tensor dst_obs, torch::Tensor dst_state,
    torch::Tensor dst_actions, torch::Tensor dst_logprobs,
    torch::Tensor dst_advantages, torch::Tensor dst_prio,
    torch::Tensor dst_values, torch::Tensor dst_returns);

// --- Priority replay ---
std::tuple<torch::Tensor, torch::Tensor> prio_replay_metal(
    torch::Tensor advantages, float prio_alpha,
    int minibatch_segments, int total_agents, float anneal_beta);

#endif // WITH_METAL
#endif // PUFFERLIB_METAL_KERNELS_H
