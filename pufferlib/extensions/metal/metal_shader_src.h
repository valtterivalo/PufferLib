/**
 * @fileoverview All Metal Shading Language (MSL) kernel sources as embedded C strings.
 *
 * Compiled JIT at runtime via [device newLibraryWithSource:...].
 * Organized into sections matching the CUDA kernel inventory:
 *   1. Math ops (sigmoid, tilde_relu, lerp, softplus, log_coeffs_and_values)
 *   2. MinGRU inference (mingru_gate_inference)
 *   3. MinGRU training (fused_scan_forward/backward, logcumsumexp)
 *   4. Sampling (sample_logits with Philox RNG)
 *   5. PPO loss (forward/backward with threadgroup reductions)
 *   6. Advantage (puff_advantage)
 *   7. Select+Copy (select_copy for minibatch prep)
 *   8. Priority replay (prio_adv_reduction, prio_normalize, prio_imp_weights)
 */

#ifndef PUFFERLIB_METAL_SHADER_SRC_H
#define PUFFERLIB_METAL_SHADER_SRC_H

static const char* get_all_metal_shader_source() {
    return R"METAL(
#include <metal_stdlib>
#include <metal_math>
#include <metal_simdgroup>
#include <metal_atomic>
using namespace metal;

// ============================================================================
// Section 1: Math ops (port of ops.cuh)
// ============================================================================

inline float sigmoid_f(float x) {
    float z = exp(-abs(x));
    return x >= 0.0f ? 1.0f / (1.0f + z) : z / (1.0f + z);
}

inline float fast_tanh_f(float x) {
    float v1 = clamp(x, -9.0f, 9.0f);
    float v2 = v1 * v1;
    // Horner polynomial (matches PyTorch implementation)
    float p = v2 * (-2.76076847742355e-16f) + 2.00018790482477e-13f;
    p = v2 * p + (-8.60467152213735e-11f);
    p = v2 * p + 5.12229709037114e-08f;
    p = v2 * p + 1.48572235717979e-05f;
    p = v2 * p + 6.37261928875436e-04f;
    p = v2 * p + 4.89352455891786e-03f;
    p = v1 * p;
    float q = v2 * 1.19825839466702e-06f + 1.18534705686654e-04f;
    q = v2 * q + 2.26843463243900e-03f;
    q = v2 * q + 4.89352518554385e-03f;
    return p / q;
}

inline float fast_sigmoid_f(float x) {
    float y = fast_tanh_f(x * 0.5f);
    return clamp((y + 1.0f) * 0.5f, 0.0f, 1.0f);
}

inline float tilde_relu_fwd(float x) {
    return x >= 0.0f ? x + 0.5f : fast_sigmoid_f(x);
}

inline float tilde_relu_bwd(float x, float grad) {
    if (x >= 0.0f) return grad;
    float sig = fast_sigmoid_f(x);
    return grad * sig * (1.0f - sig);
}

inline float lerp_f(float a, float b, float w) {
    float diff = b - a;
    return abs(w) < 0.5f ? a + w * diff : b - diff * (1.0f - w);
}

constant float SOFTPLUS_BETA = 1.0f;
constant float SOFTPLUS_THRESHOLD = 20.0f;

inline float softplus_fwd(float x) {
    float xs = x * SOFTPLUS_BETA;
    return xs > SOFTPLUS_THRESHOLD ? x : log(1.0f +exp(xs)) / SOFTPLUS_BETA;
}

inline void log_coeffs_and_values_fwd(float gate, float hidden,
                                       thread float& log_coeff, thread float& log_value) {
    float abs_gate = abs(gate);
    float sp_neg = log(1.0f +exp(-abs_gate));
    float softplus_gate, softplus_neg_gate;
    if (gate >= 0.0f) {
        softplus_gate = gate + sp_neg;
        softplus_neg_gate = sp_neg;
    } else {
        softplus_gate = sp_neg;
        softplus_neg_gate = -gate + sp_neg;
    }
    log_coeff = -softplus_gate;
    float log_z = -softplus_neg_gate;
    float log_tilde_h = hidden >= 0.0f ? log(hidden + 0.5f) : -softplus_fwd(-hidden);
    log_value = log_z + log_tilde_h;
}

inline void log_coeffs_and_values_bwd(float grad_lc, float grad_lv,
                                       float gate, float hidden,
                                       thread float& grad_gate, thread float& grad_hidden) {
    float sig_gate = sigmoid_f(gate);
    grad_gate = -grad_lc * sig_gate + grad_lv * (1.0f - sig_gate);
    if (hidden >= 0.0f) {
        grad_hidden = grad_lv / (hidden + 0.5f);
    } else {
        grad_hidden = grad_lv * sigmoid_f(-hidden);
    }
}

// ============================================================================
// Section 2: MinGRU inference kernel
// ============================================================================

// mingru_gate_inference: fused chunk + tilde_relu + lerp + sigmoid(proj)
// combined is (B, 3*H) = [hidden, gate, proj], state is (B, H)
// out = sigmoid(proj) * mingru_out, next_state = mingru_out
struct MingruGateParams {
    int H;
    int B;
};

kernel void mingru_gate_inference(
    device float* out               [[buffer(0)]],
    device float* next_state        [[buffer(1)]],
    const device float* combined    [[buffer(2)]],
    const device float* state_in    [[buffer(3)]],
    constant MingruGateParams& p    [[buffer(4)]],
    uint idx [[thread_position_in_grid]]
) {
    int N = p.B * p.H;
    if ((int)idx >= N) return;

    int b = (int)idx / p.H;
    int h = (int)idx % p.H;
    int base = b * 3 * p.H;

    float hidden = combined[base + h];
    float gate = combined[base + p.H + h];
    float proj = combined[base + 2 * p.H + h];
    float state = state_in[idx];

    float gate_sig = sigmoid_f(gate);
    float hidden_tilde = tilde_relu_fwd(hidden);
    float mingru_out = lerp_f(state, hidden_tilde, gate_sig);

    next_state[idx] = mingru_out;
    out[idx] = sigmoid_f(proj) * mingru_out;
}

// ============================================================================
// Section 3: Puff advantage kernel
// ============================================================================

struct AdvantageParams {
    float gamma;
    float lambda;
    float rho_clip;
    float c_clip;
    int num_steps;
    int horizon;
};

kernel void puff_advantage_kernel(
    const device float* values      [[buffer(0)]],
    const device float* rewards     [[buffer(1)]],
    const device float* dones       [[buffer(2)]],
    const device float* importance  [[buffer(3)]],
    device float* advantages        [[buffer(4)]],
    constant AdvantageParams& p     [[buffer(5)]],
    uint row [[thread_position_in_grid]]
) {
    if ((int)row >= p.num_steps) return;

    int offset = (int)row * p.horizon;
    const device float* v = values + offset;
    const device float* r = rewards + offset;
    const device float* d = dones + offset;
    const device float* imp = importance + offset;
    device float* adv = advantages + offset;

    float lastpufferlam = 0.0f;
    for (int t = p.horizon - 2; t >= 0; t--) {
        int t_next = t + 1;
        float nextnonterminal = 1.0f - d[t_next];
        float rho_t = min(imp[t], p.rho_clip);
        float c_t = min(imp[t], p.c_clip);
        float delta = rho_t * (r[t_next] + p.gamma * v[t_next] * nextnonterminal - v[t]);
        lastpufferlam = delta + p.gamma * p.lambda * c_t * lastpufferlam * nextnonterminal;
        adv[t] = lastpufferlam;
    }
}

// ============================================================================
// Section 4: Select+Copy kernel (for minibatch preparation)
// ============================================================================

struct SelectCopyParams {
    int obs_row_bytes;
    int actions_row_bytes;
    int logprobs_row_bytes;
    int horizon;
};

// Each threadgroup handles one (mb, channel) pair.
// channel 0=obs, 1=actions, 2=logprobs, 3=values+adv+returns, 4=prio
kernel void select_copy_kernel(
    const device int64_t* idx           [[buffer(0)]],
    const device char* src_obs          [[buffer(1)]],
    device char* dst_obs                [[buffer(2)]],
    const device char* src_actions      [[buffer(3)]],
    device char* dst_actions            [[buffer(4)]],
    const device char* src_logprobs     [[buffer(5)]],
    device char* dst_logprobs           [[buffer(6)]],
    const device float* src_values      [[buffer(7)]],
    device float* dst_values            [[buffer(8)]],
    const device float* src_advantages  [[buffer(9)]],
    device float* dst_advantages        [[buffer(10)]],
    device float* dst_returns           [[buffer(11)]],
    const device float* src_prio        [[buffer(12)]],
    device float* dst_prio              [[buffer(13)]],
    constant SelectCopyParams& p        [[buffer(14)]],
    uint2 gid [[thread_position_in_grid]],
    uint tid [[thread_index_in_threadgroup]]
) {
    int mb = gid.x;
    int ch = gid.y;
    int src_row = (int)idx[mb];

    if (ch == 0) {
        // Copy obs row (byte-level copy using int4 for coalescing)
        const device int* s = (const device int*)(src_obs + (int64_t)src_row * p.obs_row_bytes);
        device int* d = (device int*)(dst_obs + (int64_t)mb * p.obs_row_bytes);
        for (int i = (int)tid; i < p.obs_row_bytes / 4; i += 256) {
            d[i] = s[i];
        }
    } else if (ch == 1) {
        const device int* s = (const device int*)(src_actions + (int64_t)src_row * p.actions_row_bytes);
        device int* d = (device int*)(dst_actions + (int64_t)mb * p.actions_row_bytes);
        for (int i = (int)tid; i < p.actions_row_bytes / 4; i += 256) {
            d[i] = s[i];
        }
    } else if (ch == 2) {
        const device int* s = (const device int*)(src_logprobs + (int64_t)src_row * p.logprobs_row_bytes);
        device int* d = (device int*)(dst_logprobs + (int64_t)mb * p.logprobs_row_bytes);
        for (int i = (int)tid; i < p.logprobs_row_bytes / 4; i += 256) {
            d[i] = s[i];
        }
    } else if (ch == 3) {
        int srh = src_row * p.horizon;
        int drh = mb * p.horizon;
        for (int i = (int)tid; i < p.horizon; i += 256) {
            float val = src_values[srh + i];
            float adv_val = src_advantages[srh + i];
            dst_values[drh + i] = val;
            dst_advantages[drh + i] = adv_val;
            dst_returns[drh + i] = val + adv_val;
        }
    } else if (ch == 4) {
        if (tid == 0) {
            dst_prio[mb] = src_prio[mb];
        }
    }
}

// ============================================================================
// Section 5: Philox RNG (for sampling kernel)
// ============================================================================

// Philox4x32-10 counter-based RNG (same algorithm as cuRAND)
struct Philox4x32 {
    uint4 counter;
    uint2 key;
};

inline uint4 philox4x32_round(uint4 ctr, uint2 key) {
    // Philox S-box constants
    constexpr uint PHILOX_M0 = 0xD2511F53u;
    constexpr uint PHILOX_M1 = 0xCD9E8D57u;
    uint hi0 = mulhi(PHILOX_M0, ctr.x);
    uint lo0 = PHILOX_M0 * ctr.x;
    uint hi1 = mulhi(PHILOX_M1, ctr.z);
    uint lo1 = PHILOX_M1 * ctr.z;
    return uint4(hi1 ^ ctr.y ^ key.x, lo1, hi0 ^ ctr.w ^ key.y, lo0);
}

inline uint4 philox4x32_10(uint4 counter, uint2 key) {
    // 10 rounds of Philox with bump constants
    constexpr uint PHILOX_W0 = 0x9E3779B9u;
    constexpr uint PHILOX_W1 = 0xBB67AE85u;
    for (int i = 0; i < 10; i++) {
        counter = philox4x32_round(counter, key);
        key.x += PHILOX_W0;
        key.y += PHILOX_W1;
    }
    return counter;
}

inline float philox_uniform(thread uint& state_idx, uint4 rng_out) {
    uint val;
    switch (state_idx & 3) {
        case 0: val = rng_out.x; break;
        case 1: val = rng_out.y; break;
        case 2: val = rng_out.z; break;
        default: val = rng_out.w; break;
    }
    state_idx++;
    // Convert to (0, 1] uniform
    return (float(val >> 8) + 0.5f) / 16777216.0f;
}

// Box-Muller transform for Normal(0,1)
inline float philox_normal(float u1, float u2) {
    return sqrt(-2.0f * log(u1)) * cos(2.0f * M_PI_F * u2);
}

// ============================================================================
// Section 6: Sample logits kernel
// ============================================================================

struct SampleParams {
    uint64_t seed;
    int num_atns;
    int B;
    int logits_stride;
    int logstd_stride;
    int value_stride;
    int is_continuous;  // 1 for continuous, 0 for discrete
};

kernel void sample_logits_kernel(
    device float* actions               [[buffer(0)]],
    device float* logprobs              [[buffer(1)]],
    device float* value_out             [[buffer(2)]],
    const device float* logits          [[buffer(3)]],
    const device float* logstd          [[buffer(4)]],
    const device float* value           [[buffer(5)]],
    const device int* act_sizes         [[buffer(6)]],
    device atomic_uint* offset_ptr      [[buffer(7)]],
    constant SampleParams& sp           [[buffer(8)]],
    uint idx [[thread_position_in_grid]]
) {
    if ((int)idx >= sp.B) return;

    // Read and increment offset atomically
    uint offset = atomic_fetch_add_explicit(offset_ptr, 1u, memory_order_relaxed);

    // Generate Philox RNG state
    uint4 counter = uint4((uint)idx, offset, 0u, 0u);
    uint2 key = uint2((uint)(sp.seed & 0xFFFFFFFF), (uint)(sp.seed >> 32));
    uint4 rng_out = philox4x32_10(counter, key);
    uint rng_idx = 0;

    int logits_base = (int)idx * sp.logits_stride;
    float total_log_prob = 0.0f;

    if (sp.is_continuous) {
        constexpr float LOG_2PI = 1.8378770664093453f;
        int logstd_base = (int)idx * sp.logstd_stride;

        for (int h = 0; h < sp.num_atns; h++) {
            float mean = logits[logits_base + h];
            float log_std = logstd[logstd_base + h];
            float std = exp(log_std);

            // Need 2 uniforms for Box-Muller
            float u1 = philox_uniform(rng_idx, rng_out);
            float u2 = philox_uniform(rng_idx, rng_out);
            if (rng_idx >= 4) {
                // Regenerate RNG block
                counter.z++;
                rng_out = philox4x32_10(counter, key);
                rng_idx = 0;
            }
            float noise = philox_normal(u1, u2);
            float action = mean + std * noise;

            float normalized = (action - mean) / std;
            float log_prob = -0.5f * normalized * normalized - 0.5f * LOG_2PI - log_std;

            actions[(int)idx * sp.num_atns + h] = action;
            total_log_prob += log_prob;
        }
    } else {
        // Discrete action sampling (multinomial)
        int logits_offset = 0;

        for (int h = 0; h < sp.num_atns; h++) {
            int A = act_sizes[h];

            // Find max for numerical stability (with nan_to_num)
            float max_val = -INFINITY;
            for (int a = 0; a < A; a++) {
                float l = logits[logits_base + logits_offset + a];
                if (isnan(l)) l = 0.0f;
                if (isinf(l)) l = (l > 0) ? 3.4028e+38f : -3.4028e+38f;
                max_val = fmax(max_val, l);
            }

            // logsumexp
            float sum_exp = 0.0f;
            for (int a = 0; a < A; a++) {
                float l = logits[logits_base + logits_offset + a];
                if (isnan(l)) l = 0.0f;
                if (isinf(l)) l = (l > 0) ? 3.4028e+38f : -3.4028e+38f;
                sum_exp += exp(l - max_val);
            }
            float logsumexp_val = max_val + log(sum_exp);

            // Random sample
            float rand_val = philox_uniform(rng_idx, rng_out);
            if (rng_idx >= 4) {
                counter.z++;
                rng_out = philox4x32_10(counter, key);
                rng_idx = 0;
            }

            // Inverse CDF sampling
            float cumsum = 0.0f;
            int sampled_action = A - 1;
            for (int a = 0; a < A; a++) {
                float l = logits[logits_base + logits_offset + a];
                if (isnan(l)) l = 0.0f;
                if (isinf(l)) l = (l > 0) ? 3.4028e+38f : -3.4028e+38f;
                float prob = exp(l - logsumexp_val);
                cumsum += prob;
                if (rand_val < cumsum) {
                    sampled_action = a;
                    break;
                }
            }

            // Gather log probability
            float sampled_logit = logits[logits_base + logits_offset + sampled_action];
            if (isnan(sampled_logit)) sampled_logit = 0.0f;
            if (isinf(sampled_logit)) sampled_logit = (sampled_logit > 0) ? 3.4028e+38f : -3.4028e+38f;
            float log_prob = sampled_logit - logsumexp_val;

            actions[(int)idx * sp.num_atns + h] = float(sampled_action);
            total_log_prob += log_prob;

            logits_offset += A;
        }
    }

    logprobs[idx] = total_log_prob;
    value_out[idx] = value[(int)idx * sp.value_stride];
}

// ============================================================================
// Section 7: MinGRU training kernels (fused scan + logcumsumexp)
// ============================================================================

constant int CHECKPOINT_INTERVAL = 4;

struct ScanParams {
    int T_seq;
    int H;
    int B;
};

kernel void fused_scan_forward_checkpointed(
    device float* out               [[buffer(0)]],
    device float* next_state        [[buffer(1)]],
    device float* a_star_buf        [[buffer(2)]],
    device float* s_buf             [[buffer(3)]],
    device float* log_values_buf    [[buffer(4)]],
    const device float* combined    [[buffer(5)]],
    const device float* state       [[buffer(6)]],
    constant ScanParams& p          [[buffer(7)]],
    uint idx [[thread_position_in_grid]]
) {
    if ((int)idx >= p.B * p.H) return;

    int b = (int)idx / p.H;
    int h = (int)idx % p.H;
    int bH = b * p.H;
    int H3 = 3 * p.H;
    int bHT = bH * p.T_seq;
    int out_base = bHT + h;
    int cbase = 3 * bHT;

    float a_star = 0.0f;
    float log_value = 0.0f;
    float s = log(state[bH + h]);
    log_value = s;

    int T_out = p.T_seq + 1;
    int buf_base = b * T_out * p.H + h;
    int buf_curr = buf_base;
    a_star_buf[buf_curr] = a_star;
    s_buf[buf_curr] = s;
    log_values_buf[buf_curr] = log_value;

    int out_curr = out_base;
    int t_offset = 0;

    for (int t = 1; t < p.T_seq + 1; t++) {
        float hidden_val = combined[cbase + h + t_offset];
        float gate_val = combined[cbase + p.H + h + t_offset];
        float proj_val = combined[cbase + 2 * p.H + h + t_offset];

        float log_coeff_val;
        log_coeffs_and_values_fwd(gate_val, hidden_val, log_coeff_val, log_value);

        a_star += log_coeff_val;

        float z = log_value - a_star;
        float max_val = fmax(s, z);
        s = max_val + log(1.0f +exp(-abs(s - z)));

        float scan_result = exp(a_star + s);
        float proj_sigmoid = sigmoid_f(proj_val);

        out[out_curr] = proj_sigmoid * scan_result;

        buf_curr += p.H;
        out_curr += p.H;
        t_offset += H3;

        if (t % CHECKPOINT_INTERVAL == 0) {
            a_star_buf[buf_curr] = a_star;
            s_buf[buf_curr] = s;
            log_values_buf[buf_curr] = log_value;
        }
    }

    next_state[bH + h] = exp(a_star + s);
}

kernel void fused_scan_backward_checkpointed(
    device float* grad_combined          [[buffer(0)]],
    device float* grad_state             [[buffer(1)]],
    const device float* grad_out         [[buffer(2)]],
    const device float* grad_next_state  [[buffer(3)]],
    const device float* combined         [[buffer(4)]],
    const device float* state            [[buffer(5)]],
    const device float* a_star_buf       [[buffer(6)]],
    const device float* s_buf            [[buffer(7)]],
    const device float* log_values_buf   [[buffer(8)]],
    constant ScanParams& p               [[buffer(9)]],
    uint idx [[thread_position_in_grid]]
) {
    if ((int)idx >= p.B * p.H) return;

    int b = (int)idx / p.H;
    int h = (int)idx % p.H;
    int bHT = b * p.H * p.T_seq;
    int cbase = 3 * bHT;
    int H3 = 3 * p.H;
    int state_idx = b * p.H + h;
    int out_base = bHT + h;

    int T_out = p.T_seq + 1;
    int buf_base = b * T_out * p.H + h;

    float acc = 0.0f;
    float s_val_next = 0.0f;
    float carry_grad_a = 0.0f;

    for (int chunk_end = p.T_seq; chunk_end > 0; chunk_end -= CHECKPOINT_INTERVAL) {
        int chunk_start = (chunk_end > CHECKPOINT_INTERVAL) ? (chunk_end - CHECKPOINT_INTERVAL) : 0;
        int chunk_len = chunk_end - chunk_start;

        // Chunk storage in thread-local arrays
        float chunk_a_star[CHECKPOINT_INTERVAL];
        float chunk_s[CHECKPOINT_INTERVAL];
        float chunk_log_values[CHECKPOINT_INTERVAL];
        float chunk_hidden[CHECKPOINT_INTERVAL];
        float chunk_gate[CHECKPOINT_INTERVAL];

        // Load checkpoint
        int ckpt_buf_idx = buf_base + chunk_start * p.H;
        float recomp_a_star = a_star_buf[ckpt_buf_idx];
        float recomp_s = s_buf[ckpt_buf_idx];
        float recomp_log_value = log_values_buf[ckpt_buf_idx];

        // Forward recompute within chunk
        for (int i = 0; i < chunk_len; i++) {
            int t = chunk_start + 1 + i;
            int t_offset = (t - 1) * H3;
            float hv = combined[cbase + h + t_offset];
            float gv = combined[cbase + p.H + h + t_offset];

            float lc;
            log_coeffs_and_values_fwd(gv, hv, lc, recomp_log_value);
            recomp_a_star += lc;

            float z = recomp_log_value - recomp_a_star;
            float mv = fmax(recomp_s, z);
            recomp_s = mv + log(1.0f +exp(-abs(recomp_s - z)));

            chunk_a_star[i] = recomp_a_star;
            chunk_s[i] = recomp_s;
            chunk_log_values[i] = recomp_log_value;
            chunk_hidden[i] = hv;
            chunk_gate[i] = gv;
        }

        // Backward through chunk
        for (int i = chunk_len - 1; i >= 0; i--) {
            int t = chunk_start + 1 + i;
            int t_offset = (t - 1) * H3;

            float a_star_t = chunk_a_star[i];
            float s_t = chunk_s[i];
            float log_value_t = chunk_log_values[i];
            float hidden_val = chunk_hidden[i];
            float gate_val = chunk_gate[i];
            float proj_val = combined[cbase + 2 * p.H + h + t_offset];

            float scan_result = exp(a_star_t + s_t);
            float z = log_value_t - a_star_t;

            float grad_out_val = grad_out[out_base + (t - 1) * p.H];
            float grad_scan_from_next = (t == p.T_seq) ? grad_next_state[state_idx] : 0.0f;

            float proj_sigmoid = sigmoid_f(proj_val);
            float grad_scan_result = grad_scan_from_next + grad_out_val * proj_sigmoid;
            float grad_proj = grad_out_val * scan_result * proj_sigmoid * (1.0f - proj_sigmoid);

            float grad_log_h = grad_scan_result * scan_result;
            float grad_s = grad_log_h;

            if (t == p.T_seq) {
                acc = grad_s;
            } else {
                acc = grad_s + acc * exp(s_t - s_val_next);
            }
            float grad_z = acc * exp(z - s_t);
            s_val_next = s_t;

            float grad_a = grad_log_h + carry_grad_a - grad_z;
            carry_grad_a = grad_a;

            float grad_g, grad_h;
            log_coeffs_and_values_bwd(grad_a, grad_z, gate_val, hidden_val, grad_g, grad_h);

            grad_combined[cbase + h + t_offset] = grad_h;
            grad_combined[cbase + p.H + h + t_offset] = grad_g;
            grad_combined[cbase + 2 * p.H + h + t_offset] = grad_proj;
        }
    }

    // Gradient for initial state (t=0)
    int ckpt_0_idx = buf_base;
    float a_star_0 = a_star_buf[ckpt_0_idx];
    float s_0 = s_buf[ckpt_0_idx];
    float log_value_0 = log_values_buf[ckpt_0_idx];

    float scan_result_0 = exp(a_star_0 + s_0);
    float z_0 = log_value_0 - a_star_0;

    float grad_log_h_0 = 0.0f;
    float grad_s_0 = grad_log_h_0;

    acc = grad_s_0 + acc * exp(s_0 - s_val_next);
    float grad_z_0 = acc * exp(z_0 - s_0);

    grad_state[state_idx] = grad_z_0 / state[state_idx];
}

// logcumsumexp: uses Kahan compensated summation (fp32) instead of fp64
struct LogcumsumexpParams {
    int T_total;
    int H;
    int B;
};

kernel void logcumsumexp_forward_kernel(
    device float* out               [[buffer(0)]],
    device float* s_buf             [[buffer(1)]],
    const device float* x           [[buffer(2)]],
    constant LogcumsumexpParams& p  [[buffer(3)]],
    uint idx [[thread_position_in_grid]]
) {
    if ((int)idx >= p.B * p.H) return;

    int b = (int)idx / p.H;
    int h = (int)idx % p.H;
    int base_off = b * p.T_total * p.H + h;

    // Kahan compensated accumulation in log-space
    float s = -INFINITY;
    float compensation = 0.0f;

    for (int t = 0; t < p.T_total; t++) {
        int curr = base_off + t * p.H;
        float x_val = x[curr];

        if (s == -INFINITY) {
            s = x_val;
        } else {
            float min_v = fmin(s, x_val);
            float max_v = fmax(s, x_val);
            float y = log(1.0f +exp(min_v - max_v)) - compensation;
            float t_val = max_v + y;
            compensation = (t_val - max_v) - y;
            s = t_val;
        }

        out[curr] = s;
        s_buf[curr] = s;
    }
}

kernel void logcumsumexp_backward_kernel(
    device float* grad_x                [[buffer(0)]],
    const device float* grad_out        [[buffer(1)]],
    const device float* x               [[buffer(2)]],
    const device float* s_buf           [[buffer(3)]],
    constant LogcumsumexpParams& p      [[buffer(4)]],
    uint idx [[thread_position_in_grid]]
) {
    if ((int)idx >= p.B * p.H) return;

    int b = (int)idx / p.H;
    int h = (int)idx % p.H;
    int base_off = b * p.T_total * p.H + h;

    float acc_val = 0.0f;
    float s_val_next = 0.0f;

    for (int t = p.T_total - 1; t >= 0; t--) {
        int curr = base_off + t * p.H;
        float x_val = x[curr];
        float s_val = s_buf[curr];
        float g_val = grad_out[curr];

        acc_val = g_val + acc_val * exp(s_val - s_val_next);
        s_val_next = s_val;
        grad_x[curr] = acc_val * exp(x_val - s_val);
    }
}

// ============================================================================
// Section 8: PPO loss kernels
// ============================================================================

constant int PPO_THREADS = 256;
constant int LOSS_PG = 0;
constant int LOSS_VF = 1;
constant int LOSS_ENT = 2;
constant int LOSS_TOTAL = 3;
constant int LOSS_OLD_APPROX_KL = 4;
constant int LOSS_APPROX_KL = 5;
constant int LOSS_CLIPFRAC = 6;
constant int LOSS_N_COMPONENTS = 7;
constant int MAX_ATN_HEADS = 16;

// Float atomic add via CAS loop (MSL has no native atomic<float>)
inline void atomic_add_float(device atomic_uint* addr, float val) {
    uint expected = atomic_load_explicit(addr, memory_order_relaxed);
    while (true) {
        float current = as_type<float>(expected);
        float desired = current + val;
        uint desired_bits = as_type<uint>(desired);
        if (atomic_compare_exchange_weak_explicit(addr, &expected, desired_bits,
                memory_order_relaxed, memory_order_relaxed)) {
            return;
        }
    }
}

struct PPOParams {
    float clip_coef;
    float vf_clip_coef;
    float vf_coef;
    float ent_coef;
    int T_seq;
    int A_total;
    int N;
    int logits_stride_n;
    int logits_stride_t;
    int logits_stride_a;
    int values_stride_n;
    int values_stride_t;
    int num_atns;
    int is_continuous;
};

kernel void ppo_loss_forward_kernel(
    device atomic_uint* loss            [[buffer(0)]],
    device atomic_uint* losses_acc      [[buffer(1)]],
    const device float* logits          [[buffer(2)]],
    const device float* logstd          [[buffer(3)]],
    const device float* values_pred     [[buffer(4)]],
    const device float* actions          [[buffer(5)]],
    const device float* old_logprobs    [[buffer(6)]],
    const device float* advantages      [[buffer(7)]],
    const device float* prio            [[buffer(8)]],
    const device float* values          [[buffer(9)]],
    const device float* returns_buf     [[buffer(10)]],
    const device float* adv_mean        [[buffer(11)]],
    const device float* adv_var         [[buffer(12)]],
    const device int* act_sizes         [[buffer(13)]],
    constant PPOParams& pp              [[buffer(14)]],
    uint idx [[thread_position_in_grid]],
    uint tid [[thread_index_in_threadgroup]]
) {
    int total_elements = pp.N * pp.T_seq;

    // Shared memory for block reduction
    threadgroup float block_losses[LOSS_N_COMPONENTS][PPO_THREADS];
    for (int c = 0; c < LOSS_N_COMPONENTS; c++) {
        block_losses[c][tid] = 0.0f;
    }

    if ((int)idx < total_elements) {
        int n = (int)idx / pp.T_seq;
        int t = (int)idx % pp.T_seq;
        int nt = n * pp.T_seq + t;
        int logits_base = n * pp.logits_stride_n + t * pp.logits_stride_t;
        int values_idx = n * pp.values_stride_n + t * pp.values_stride_t;

        float total_log_prob = 0.0f;
        float total_entropy = 0.0f;

        if (pp.is_continuous) {
            constexpr float HALF_LOG_2PI = 0.9189385332046727f;
            constexpr float HALF_1_PLUS_LOG_2PI = 1.4189385332046727f;

            for (int h = 0; h < pp.num_atns; h++) {
                float mean = logits[logits_base + h * pp.logits_stride_a];
                float log_std = logstd[logits_base + h * pp.logits_stride_a];
                float std = exp(log_std);
                float action = float(actions[nt * pp.num_atns + h]);

                float normalized = (action - mean) / std;
                total_log_prob += -0.5f * normalized * normalized - HALF_LOG_2PI - log_std;
                total_entropy += HALF_1_PLUS_LOG_2PI + log_std;
            }
        } else {
            int logits_offset = 0;
            for (int h = 0; h < pp.num_atns; h++) {
                int A = act_sizes[h];
                int act = int(actions[nt * pp.num_atns + h]);

                float max_logit = -INFINITY;
                float sum = 0.0f;
                float act_logit = 0.0f;

                for (int a = 0; a < A; a++) {
                    float l = logits[logits_base + (logits_offset + a) * pp.logits_stride_a];
                    if (a == act) act_logit = l;
                    if (l > max_logit) {
                        sum *= exp(max_logit - l);
                        max_logit = l;
                    }
                    sum += exp(l - max_logit);
                }
                float lse = max_logit + log(sum);

                float head_entropy = 0.0f;
                for (int a = 0; a < A; a++) {
                    float l = logits[logits_base + (logits_offset + a) * pp.logits_stride_a];
                    float logp = l - lse;
                    float p = exp(logp);
                    head_entropy -= p * logp;
                }

                total_log_prob += act_logit - lse;
                total_entropy += head_entropy;
                logits_offset += A;
            }
        }

        float new_logp = total_log_prob;
        float entropy = total_entropy;
        float old_logp = old_logprobs[nt];
        float adv = advantages[nt];
        float w = prio[n];
        float adv_std = sqrt(adv_var[0]);
        float adv_normalized = (adv - adv_mean[0]) / (adv_std + 1e-8f);

        float logratio = new_logp - old_logp;
        float ratio = exp(logratio);

        float ratio_clipped = clamp(ratio, 1.0f - pp.clip_coef, 1.0f + pp.clip_coef);
        float wa = -w * adv_normalized;
        float pg_loss = fmax(wa * ratio, wa * ratio_clipped);

        float val = values[nt];
        float ret = returns_buf[nt];
        float val_pred = values_pred[values_idx];

        float v_error = val_pred - val;
        float v_clipped = val + clamp(v_error, -pp.vf_clip_coef, pp.vf_clip_coef);
        float v_loss_unclipped = (val_pred - ret) * (val_pred - ret);
        float v_loss_clipped = (v_clipped - ret) * (v_clipped - ret);
        float v_loss = 0.5f * fmax(v_loss_unclipped, v_loss_clipped);

        float thread_loss = (pg_loss + pp.vf_coef * v_loss - pp.ent_coef * entropy) / float(total_elements);
        float inv_total = 1.0f / float(total_elements);

        block_losses[LOSS_PG][tid] = pg_loss * inv_total;
        block_losses[LOSS_VF][tid] = v_loss * inv_total;
        block_losses[LOSS_ENT][tid] = entropy * inv_total;
        block_losses[LOSS_TOTAL][tid] = thread_loss;
        block_losses[LOSS_OLD_APPROX_KL][tid] = (-logratio) * inv_total;
        block_losses[LOSS_APPROX_KL][tid] = ((ratio - 1.0f) - logratio) * inv_total;
        block_losses[LOSS_CLIPFRAC][tid] = (abs(ratio - 1.0f) > pp.clip_coef ? 1.0f : 0.0f) * inv_total;
    }

    threadgroup_barrier(mem_flags::mem_threadgroup);

    // Block reduction (tree reduction)
    for (int stride = PPO_THREADS / 2; stride > 0; stride >>= 1) {
        if ((int)tid < stride) {
            for (int c = 0; c < LOSS_N_COMPONENTS; c++) {
                block_losses[c][tid] += block_losses[c][tid + stride];
            }
        }
        threadgroup_barrier(mem_flags::mem_threadgroup);
    }

    if (tid == 0) {
        atomic_add_float(loss, block_losses[LOSS_TOTAL][0]);
        for (int c = 0; c < LOSS_N_COMPONENTS; c++) {
            atomic_add_float(&losses_acc[c], block_losses[c][0]);
        }
    }
}

kernel void ppo_loss_backward_kernel(
    device float* grad_logits           [[buffer(0)]],
    device float* grad_logstd           [[buffer(1)]],
    device float* grad_values_pred      [[buffer(2)]],
    const device float* grad_loss       [[buffer(3)]],
    const device float* logits          [[buffer(4)]],
    const device float* logstd          [[buffer(5)]],
    const device float* values_pred     [[buffer(6)]],
    const device float* actions          [[buffer(7)]],
    const device float* old_logprobs    [[buffer(8)]],
    const device float* advantages      [[buffer(9)]],
    const device float* prio            [[buffer(10)]],
    const device float* values          [[buffer(11)]],
    const device float* returns_buf     [[buffer(12)]],
    const device float* adv_mean        [[buffer(13)]],
    const device float* adv_var         [[buffer(14)]],
    const device int* act_sizes         [[buffer(15)]],
    constant PPOParams& pp              [[buffer(16)]],
    uint idx [[thread_position_in_grid]]
) {
    int total_elements = pp.N * pp.T_seq;
    if ((int)idx >= total_elements) return;

    float inv_NT = 1.0f / float(total_elements);
    int n = (int)idx / pp.T_seq;
    int t = (int)idx % pp.T_seq;
    int nt = n * pp.T_seq + t;
    int logits_base = n * pp.logits_stride_n + t * pp.logits_stride_t;
    int values_idx = n * pp.values_stride_n + t * pp.values_stride_t;

    float old_logp = old_logprobs[nt];
    float adv = advantages[nt];
    float w = prio[n];
    float val = values[nt];
    float ret = returns_buf[nt];
    float val_pred = values_pred[values_idx];

    float adv_std_val = sqrt(adv_var[0]);
    float adv_normalized = (adv - adv_mean[0]) / (adv_std_val + 1e-8f);

    float dL = grad_loss[0] * inv_NT;
    float d_pg_loss = dL;
    float d_entropy_term = dL * (-pp.ent_coef);

    // Value gradient
    float v_error = val_pred - val;
    float v_clipped = val + clamp(v_error, -pp.vf_clip_coef, pp.vf_clip_coef);
    float v_loss_unclipped = (val_pred - ret) * (val_pred - ret);
    float v_loss_clipped = (v_clipped - ret) * (v_clipped - ret);
    bool use_clipped_vf = (v_loss_clipped > v_loss_unclipped);

    float d_val_pred = 0.0f;
    if (use_clipped_vf) {
        if (v_error >= -pp.vf_clip_coef && v_error <= pp.vf_clip_coef) {
            d_val_pred = v_clipped - ret;
        }
    } else {
        d_val_pred = val_pred - ret;
    }
    grad_values_pred[values_idx] = dL * pp.vf_coef * d_val_pred;

    if (pp.is_continuous) {
        constexpr float HALF_LOG_2PI = 0.9189385332046727f;
        float total_log_prob = 0.0f;

        for (int h = 0; h < pp.num_atns; h++) {
            float mean = logits[logits_base + h * pp.logits_stride_a];
            float log_std = logstd[logits_base + h * pp.logits_stride_a];
            float std = exp(log_std);
            float action = float(actions[nt * pp.num_atns + h]);

            float normalized = (action - mean) / std;
            total_log_prob += -0.5f * normalized * normalized - HALF_LOG_2PI - log_std;
        }

        float ratio = exp(total_log_prob - old_logp);
        float ratio_clipped = clamp(ratio, 1.0f - pp.clip_coef, 1.0f + pp.clip_coef);
        float pg_loss1 = -w * adv_normalized * ratio;
        float pg_loss2 = -w * adv_normalized * ratio_clipped;

        float d_ratio = -w * adv_normalized * d_pg_loss;
        if (pg_loss2 > pg_loss1) {
            if (ratio <= (1.0f - pp.clip_coef) || ratio >= (1.0f + pp.clip_coef)) {
                d_ratio = 0.0f;
            }
        }
        float d_new_logp = d_ratio * ratio;

        for (int h = 0; h < pp.num_atns; h++) {
            float mean = logits[logits_base + h * pp.logits_stride_a];
            float log_std = logstd[logits_base + h * pp.logits_stride_a];
            float std = exp(log_std);
            float var = std * std;
            float action = float(actions[nt * pp.num_atns + h]);
            float diff = action - mean;

            grad_logits[logits_base + h * pp.logits_stride_a] = d_new_logp * diff / var;
            grad_logstd[logits_base + h * pp.logits_stride_a] =
                d_new_logp * (diff * diff / var - 1.0f) + d_entropy_term;
        }
    } else {
        // Discrete backward
        int logits_offset = 0;
        float total_log_prob = 0.0f;
        float head_logsumexp_arr[MAX_ATN_HEADS];
        float head_entropy_arr[MAX_ATN_HEADS];
        int head_act_arr[MAX_ATN_HEADS];

        for (int h = 0; h < pp.num_atns; h++) {
            int A = act_sizes[h];
            int act = int(actions[nt * pp.num_atns + h]);
            head_act_arr[h] = act;

            float max_logit = -INFINITY;
            float sum = 0.0f;
            float act_logit = 0.0f;

            for (int a = 0; a < A; a++) {
                float l = logits[logits_base + (logits_offset + a) * pp.logits_stride_a];
                if (a == act) act_logit = l;
                if (l > max_logit) {
                    sum *= exp(max_logit - l);
                    max_logit = l;
                }
                sum += exp(l - max_logit);
            }
            float lse = max_logit + log(sum);
            head_logsumexp_arr[h] = lse;

            float ent = 0.0f;
            for (int a = 0; a < A; a++) {
                float l = logits[logits_base + (logits_offset + a) * pp.logits_stride_a];
                float logp = l - lse;
                float p = exp(logp);
                ent -= p * logp;
            }
            head_entropy_arr[h] = ent;
            total_log_prob += act_logit - lse;
            logits_offset += A;
        }

        float ratio = exp(total_log_prob - old_logp);
        float ratio_clipped = clamp(ratio, 1.0f - pp.clip_coef, 1.0f + pp.clip_coef);
        float pg_loss1 = -w * adv_normalized * ratio;
        float pg_loss2 = -w * adv_normalized * ratio_clipped;

        float d_ratio = -w * adv_normalized * d_pg_loss;
        if (pg_loss2 > pg_loss1) {
            if (ratio <= (1.0f - pp.clip_coef) || ratio >= (1.0f + pp.clip_coef)) {
                d_ratio = 0.0f;
            }
        }
        float d_new_logp = d_ratio * ratio;

        logits_offset = 0;
        for (int h = 0; h < pp.num_atns; h++) {
            int A = act_sizes[h];
            int act = head_act_arr[h];
            float lse = head_logsumexp_arr[h];
            float ent = head_entropy_arr[h];

            for (int a = 0; a < A; a++) {
                float l = logits[logits_base + (logits_offset + a) * pp.logits_stride_a];
                float logp = l - lse;
                float p = exp(logp);

                float d_logit = (a == act) ? d_new_logp : 0.0f;
                d_logit -= p * d_new_logp;
                d_logit += d_entropy_term * p * (-ent - logp);

                grad_logits[logits_base + (logits_offset + a) * pp.logits_stride_a] = d_logit;
            }
            logits_offset += A;
        }
    }
}

// ============================================================================
// Section 9: Priority replay kernels
// ============================================================================

struct PrioParams {
    float prio_alpha;
    int stride;
};

kernel void prio_adv_reduction_kernel(
    const device float* advantages  [[buffer(0)]],
    device float* prio_weights      [[buffer(1)]],
    constant PrioParams& pp         [[buffer(2)]],
    uint row [[threadgroup_position_in_grid]],
    uint tx [[thread_index_in_threadgroup]],
    uint simd_lane [[thread_index_in_simdgroup]]
) {
    int offset = (int)row * pp.stride;

    float local_sum = 0.0f;
    for (int t = (int)tx; t < pp.stride; t += 256) {
        local_sum += abs(advantages[offset + t]);
    }

    // Simdgroup reduction
    local_sum = simd_sum(local_sum);

    if (simd_lane == 0 && tx < 32) {
        float pw = pow(local_sum, pp.prio_alpha);
        if (isnan(pw) || isinf(pw)) pw = 0.0f;
        prio_weights[row] = pw;
    }
}

struct PrioNormParams {
    int length;
};

kernel void prio_normalize_kernel(
    device float* prio_weights          [[buffer(0)]],
    constant PrioNormParams& pp         [[buffer(1)]],
    uint tx [[thread_index_in_threadgroup]],
    uint simd_lane [[thread_index_in_simdgroup]],
    uint simd_id [[simdgroup_index_in_threadgroup]]
) {
    constexpr float eps = 1e-6f;
    constexpr int NUM_WARPS = 8;  // 256 / 32

    threadgroup float shmem[NUM_WARPS];
    threadgroup float block_sum;

    float local_sum = 0.0f;
    for (int t = (int)tx; t < pp.length; t += 256) {
        local_sum += prio_weights[t];
    }

    // Simdgroup reduction
    local_sum = simd_sum(local_sum);

    if (simd_lane == 0) shmem[simd_id] = local_sum;
    threadgroup_barrier(mem_flags::mem_threadgroup);

    if (simd_id == 0) {
        float val = (simd_lane < NUM_WARPS) ? shmem[simd_lane] : 0.0f;
        val = simd_sum(val);
        if (tx == 0) block_sum = val + eps;
    }
    threadgroup_barrier(mem_flags::mem_threadgroup);

    for (int t = (int)tx; t < pp.length; t += 256) {
        prio_weights[t] = (prio_weights[t] + eps) / block_sum;
    }
}

struct PrioImpParams {
    int total_agents;
    float anneal_beta;
    int minibatch_segments;
};

kernel void prio_imp_weights_kernel(
    const device int64_t* indices       [[buffer(0)]],
    const device float* prio_probs      [[buffer(1)]],
    device float* mb_prio               [[buffer(2)]],
    constant PrioImpParams& pp          [[buffer(3)]],
    uint tx [[thread_position_in_grid]]
) {
    if ((int)tx < pp.minibatch_segments) {
        float value = prio_probs[indices[tx]] * float(pp.total_agents);
        mb_prio[tx] = pow(value, -pp.anneal_beta);
    }
}

)METAL";
}

#endif // PUFFERLIB_METAL_SHADER_SRC_H
