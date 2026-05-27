#ifndef PUFFERLIB_METAL_SHADER_SRC_H
#define PUFFERLIB_METAL_SHADER_SRC_H

static const char *get_all_metal_shader_source() {
  return R"METAL(
#include <metal_stdlib>
#include <metal_math>
#include <metal_simdgroup>
#include <metal_simdgroup_matrix>
#include <metal_atomic>
using namespace metal;

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

inline float lerp_f(float a, float b, float w) {
    float diff = b - a;
    return abs(w) < 0.5f ? a + w * diff : b - diff * (1.0f - w);
}

// MSL has no built-in log1p. Goldberg's trick: compensates for rounding in 1+x.
inline float log1p_f(float x) {
    float u = 1.0f + x;
    return (u == 1.0f) ? x : log(u) * x / (u - 1.0f);
}

constant float SOFTPLUS_BETA = 1.0f;
constant float SOFTPLUS_THRESHOLD = 20.0f;

inline float softplus_fwd(float x) {
    float xs = x * SOFTPLUS_BETA;
    return xs > SOFTPLUS_THRESHOLD ? x : log1p_f(exp(xs)) / SOFTPLUS_BETA;
}

inline void log_coeffs_and_values_fwd(float gate, float hidden,
                                       thread float& log_coeff, thread float& log_value) {
    float abs_gate = abs(gate);
    float sp_neg = log1p_f(exp(-abs_gate));
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

inline uint4 philox4x32_round(uint4 ctr, uint2 key) {
    constexpr uint PHILOX_M0 = 0xD2511F53u;
    constexpr uint PHILOX_M1 = 0xCD9E8D57u;
    uint hi0 = mulhi(PHILOX_M0, ctr.x);
    uint lo0 = PHILOX_M0 * ctr.x;
    uint hi1 = mulhi(PHILOX_M1, ctr.z);
    uint lo1 = PHILOX_M1 * ctr.z;
    return uint4(hi1 ^ ctr.y ^ key.x, lo1, hi0 ^ ctr.w ^ key.y, lo0);
}

inline uint4 philox4x32_10(uint4 counter, uint2 key) {
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

struct MingruGateParams {
    int H;
    int B;
};

kernel void mingru_gate_inference(
    device float* out               [[buffer(0)]],
    device float* next_state        [[buffer(1)]],
    const device float* combined    [[buffer(2)]],
    const device float* state_in    [[buffer(3)]],
    const device float* x_in        [[buffer(4)]],
    constant MingruGateParams& p    [[buffer(5)]],
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
    float x = x_in[idx];

    float gate_sig = sigmoid_f(gate);
    float hidden_tilde = tilde_relu_fwd(hidden);
    float mingru_out = lerp_f(state, hidden_tilde, gate_sig);
    float proj_sig = sigmoid_f(proj);

    next_state[idx] = max(mingru_out, 1e-30f);
    out[idx] = proj_sig * mingru_out + (1.0f - proj_sig) * x;
}

constant int CHECKPOINT_INTERVAL = 4;

struct ScanParams {
    int T_seq;
    int H;
    int B;
};

inline float scan_read(const device float* src, int idx) { return src[idx]; }
inline float scan_read(const device half* src, int idx) { return float(src[idx]); }

inline float scan_log_state(const device float* state, int idx) { return fast::log(state[idx]); }
inline float scan_log_state(const device half* state, int idx) { return log(float(state[idx])); }

inline float scan_exp(device float*, float x) { return fast::exp(x); }
inline float scan_exp(device half*, float x) { return exp(x); }

inline void scan_write(device float* dst, int idx, float value) { dst[idx] = value; }
inline void scan_write(device half* dst, int idx, float value) {
    dst[idx] = half(clamp(value, -65000.0f, 65000.0f));
}

inline void scan_write_next_checkpointed(device float* dst, int idx, float value) {
    dst[idx] = max(value, 1e-30f);
}

inline void scan_write_next_checkpointed(device half* dst, int idx, float value) {
    dst[idx] = half(min(max(value, 1e-30f), 65000.0f));
}

template <typename T>
inline void mingru_scan_forward_checkpointed_body(
    device T* out,
    device T* next_state,
    device float* a_star_buf,
    device float* s_buf,
    device float* log_values_buf,
    const device T* combined,
    const device T* state,
    const device T* input,
    constant ScanParams& p,
    uint idx
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
    float s = scan_log_state(state, bH + h);
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
        float hidden_val = scan_read(combined, cbase + h + t_offset);
        float gate_val = scan_read(combined, cbase + p.H + h + t_offset);
        float proj_val = scan_read(combined, cbase + 2 * p.H + h + t_offset);
        float x_val = scan_read(input, out_base + (t - 1) * p.H);

        float log_coeff_val;
        log_coeffs_and_values_fwd(gate_val, hidden_val, log_coeff_val, log_value);

        a_star += log_coeff_val;

        float z = log_value - a_star;
        float max_val = fmax(s, z);
        s = max_val + log1p_f(scan_exp(out, -abs(s - z)));

        float scan_result = scan_exp(out, a_star + s);
        float proj_sigmoid = sigmoid_f(proj_val);
        float out_val = proj_sigmoid * scan_result + (1.0f - proj_sigmoid) * x_val;

        scan_write(out, out_curr, out_val);

        buf_curr += p.H;
        out_curr += p.H;
        t_offset += H3;

        if (t % CHECKPOINT_INTERVAL == 0) {
            a_star_buf[buf_curr] = a_star;
            s_buf[buf_curr] = s;
            log_values_buf[buf_curr] = log_value;
        }
    }

    scan_write_next_checkpointed(next_state, bH + h, scan_exp(next_state, a_star + s));
}

kernel void mingru_scan_forward_checkpointed(
    device float* out               [[buffer(0)]],
    device float* next_state        [[buffer(1)]],
    device float* a_star_buf        [[buffer(2)]],
    device float* s_buf             [[buffer(3)]],
    device float* log_values_buf    [[buffer(4)]],
    const device float* combined    [[buffer(5)]],
    const device float* state       [[buffer(6)]],
    const device float* input       [[buffer(7)]],
    constant ScanParams& p          [[buffer(8)]],
    uint idx [[thread_position_in_grid]]
) {
    mingru_scan_forward_checkpointed_body<float>(
        out, next_state, a_star_buf, s_buf, log_values_buf, combined, state, input,
        p, idx);
}

template <typename T>
inline void mingru_scan_forward_reset_body(
    device T* out,
    device T* next_state,
    device float* curr_buf,
    device float* prev_buf,
    const device T* combined,
    const device T* state,
    const device T* input,
    const device float* reset,
    constant ScanParams& p,
    uint idx
) {
    if ((int)idx >= p.B * p.H) return;

    int b = (int)idx / p.H;
    int h = (int)idx % p.H;
    int bH = b * p.H;
    int H3 = 3 * p.H;
    int H2 = 2 * p.H;
    int bHT = bH * p.T_seq;
    int out_base = bHT + h;
    int cbase = 3 * bHT;
    int T_out = p.T_seq + 1;
    int buf_base = b * T_out * p.H + h;

    float prev = scan_read(state, bH + h);
    curr_buf[buf_base] = prev;
    prev_buf[buf_base] = prev;

    for (int t = 0; t < p.T_seq; t++) {
        if (reset[b * p.T_seq + t] > 0.5f) prev = 0.0f;

        int t_offset = t * H3;
        int input_idx = out_base + t * p.H;
        float hidden_val = scan_read(combined, cbase + h + t_offset);
        float gate_val = scan_read(combined, cbase + p.H + h + t_offset);
        float proj_val = scan_read(combined, cbase + H2 + h + t_offset);
        float x_val = scan_read(input, input_idx);
        float gate_sigmoid = sigmoid_f(gate_val);
        float hidden_tilde = tilde_relu_fwd(hidden_val);
        float curr = lerp_f(prev, hidden_tilde, gate_sigmoid);
        float proj_sigmoid = sigmoid_f(proj_val);
        float out_val = proj_sigmoid * curr + (1.0f - proj_sigmoid) * x_val;

        scan_write(out, input_idx, out_val);
        curr_buf[buf_base + (t + 1) * p.H] = curr;
        prev_buf[buf_base + (t + 1) * p.H] = prev;
        prev = curr;
    }

    scan_write(next_state, bH + h, prev);
}

kernel void mingru_scan_forward_reset(
    device float* out               [[buffer(0)]],
    device float* next_state        [[buffer(1)]],
    device float* curr_buf          [[buffer(2)]],
    device float* prev_buf          [[buffer(3)]],
    device float* unused_buf        [[buffer(4)]],
    const device float* combined    [[buffer(5)]],
    const device float* state       [[buffer(6)]],
    const device float* input       [[buffer(7)]],
    const device float* reset       [[buffer(8)]],
    constant ScanParams& p          [[buffer(9)]],
    uint idx [[thread_position_in_grid]]
) {
    mingru_scan_forward_reset_body<float>(
        out, next_state, curr_buf, prev_buf, combined, state, input, reset, p, idx);
}

template <typename T>
inline void mingru_scan_backward_checkpointed_body(
    device T* grad_combined,
    device T* grad_state,
    device T* grad_input,
    const device T* grad_out,
    const device T* grad_next_state,
    const device T* combined,
    const device T* state,
    const device T* input,
    const device float* a_star_buf,
    const device float* s_buf,
    const device float* log_values_buf,
    constant ScanParams& p,
    uint idx
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

        float chunk_a_star[CHECKPOINT_INTERVAL];
        float chunk_s[CHECKPOINT_INTERVAL];
        float chunk_log_values[CHECKPOINT_INTERVAL];
        float chunk_hidden[CHECKPOINT_INTERVAL];
        float chunk_gate[CHECKPOINT_INTERVAL];

        int ckpt_buf_idx = buf_base + chunk_start * p.H;
        float recomp_a_star = a_star_buf[ckpt_buf_idx];
        float recomp_s = s_buf[ckpt_buf_idx];
        float recomp_log_value = log_values_buf[ckpt_buf_idx];

        for (int i = 0; i < chunk_len; i++) {
            int t = chunk_start + 1 + i;
            int t_offset = (t - 1) * H3;
            float hv = scan_read(combined, cbase + h + t_offset);
            float gv = scan_read(combined, cbase + p.H + h + t_offset);

            float lc;
            log_coeffs_and_values_fwd(gv, hv, lc, recomp_log_value);
            recomp_a_star += lc;

            float z = recomp_log_value - recomp_a_star;
            float mv = fmax(recomp_s, z);
            recomp_s = mv + log1p_f(scan_exp(grad_combined, -abs(recomp_s - z)));

            chunk_a_star[i] = recomp_a_star;
            chunk_s[i] = recomp_s;
            chunk_log_values[i] = recomp_log_value;
            chunk_hidden[i] = hv;
            chunk_gate[i] = gv;
        }

        for (int i = chunk_len - 1; i >= 0; i--) {
            int t = chunk_start + 1 + i;
            int t_offset = (t - 1) * H3;

            float a_star_t = chunk_a_star[i];
            float s_t = chunk_s[i];
            float log_value_t = chunk_log_values[i];
            float hidden_val = chunk_hidden[i];
            float gate_val = chunk_gate[i];
            float proj_val = scan_read(combined, cbase + 2 * p.H + h + t_offset);
            int input_idx = out_base + (t - 1) * p.H;
            float x_val = scan_read(input, input_idx);

            float scan_result = scan_exp(grad_combined, a_star_t + s_t);
            float z = log_value_t - a_star_t;

            float grad_out_val = scan_read(grad_out, input_idx);
            float grad_scan_from_next = (t == p.T_seq) ? scan_read(grad_next_state, state_idx) : 0.0f;

            float proj_sigmoid = sigmoid_f(proj_val);
            float grad_scan_result = grad_scan_from_next + grad_out_val * proj_sigmoid;
            float grad_proj = grad_out_val * (scan_result - x_val) * proj_sigmoid * (1.0f - proj_sigmoid);
            scan_write(grad_input, input_idx, grad_out_val * (1.0f - proj_sigmoid));

            float grad_log_h = grad_scan_result * scan_result;
            float grad_s = grad_log_h;

            if (t == p.T_seq) {
                acc = grad_s;
            } else {
                acc = grad_s + acc * scan_exp(grad_combined, s_t - s_val_next);
            }
            float grad_z = acc * scan_exp(grad_combined, z - s_t);
            s_val_next = s_t;

            float grad_a = grad_log_h + carry_grad_a - grad_z;
            carry_grad_a = grad_a;

            float grad_g, grad_h;
            log_coeffs_and_values_bwd(grad_a, grad_z, gate_val, hidden_val, grad_g, grad_h);

            scan_write(grad_combined, cbase + h + t_offset, grad_h);
            scan_write(grad_combined, cbase + p.H + h + t_offset, grad_g);
            scan_write(grad_combined, cbase + 2 * p.H + h + t_offset, grad_proj);
        }
    }

    int ckpt_0_idx = buf_base;
    float a_star_0 = a_star_buf[ckpt_0_idx];
    float s_0 = s_buf[ckpt_0_idx];
    float log_value_0 = log_values_buf[ckpt_0_idx];

    acc = acc * scan_exp(grad_combined, s_0 - s_val_next);
    float grad_z_0 = acc * scan_exp(grad_combined, (log_value_0 - a_star_0) - s_0);

    float state_val = scan_read(state, state_idx);
    scan_write(grad_state, state_idx, (state_val > 0.0f) ? (grad_z_0 / state_val) : 0.0f);
}

kernel void mingru_scan_backward_checkpointed(
    device float* grad_combined          [[buffer(0)]],
    device float* grad_state             [[buffer(1)]],
    device float* grad_input             [[buffer(2)]],
    const device float* grad_out         [[buffer(3)]],
    const device float* grad_next_state  [[buffer(4)]],
    const device float* combined         [[buffer(5)]],
    const device float* state            [[buffer(6)]],
    const device float* input            [[buffer(7)]],
    const device float* a_star_buf       [[buffer(8)]],
    const device float* s_buf            [[buffer(9)]],
    const device float* log_values_buf   [[buffer(10)]],
    constant ScanParams& p               [[buffer(11)]],
    uint idx [[thread_position_in_grid]]
) {
    mingru_scan_backward_checkpointed_body<float>(
        grad_combined, grad_state, grad_input, grad_out, grad_next_state,
        combined, state, input, a_star_buf, s_buf, log_values_buf, p, idx);
}

template <typename T>
inline void mingru_scan_backward_reset_body(
    device T* grad_combined,
    device T* grad_state,
    device T* grad_input,
    const device T* grad_out,
    const device T* grad_next_state,
    const device T* combined,
    const device T* input,
    const device float* curr_buf,
    const device float* prev_buf,
    const device float* reset,
    constant ScanParams& p,
    uint idx
) {
    if ((int)idx >= p.B * p.H) return;

    int b = (int)idx / p.H;
    int h = (int)idx % p.H;
    int bHT = b * p.H * p.T_seq;
    int cbase = 3 * bHT;
    int H3 = 3 * p.H;
    int H2 = 2 * p.H;
    int state_idx = b * p.H + h;
    int out_base = bHT + h;
    int T_out = p.T_seq + 1;
    int buf_base = b * T_out * p.H + h;

    float grad_next = scan_read(grad_next_state, state_idx);
    for (int t = p.T_seq - 1; t >= 0; t--) {
        int t_offset = t * H3;
        int input_idx = out_base + t * p.H;
        float curr = curr_buf[buf_base + (t + 1) * p.H];
        float prev = prev_buf[buf_base + (t + 1) * p.H];
        float hidden_val = scan_read(combined, cbase + h + t_offset);
        float gate_val = scan_read(combined, cbase + p.H + h + t_offset);
        float proj_val = scan_read(combined, cbase + H2 + h + t_offset);
        float x_val = scan_read(input, input_idx);
        float gate_sigmoid = sigmoid_f(gate_val);
        float hidden_tilde = tilde_relu_fwd(hidden_val);
        float hidden_grad = hidden_val >= 0.0f
            ? 1.0f
            : hidden_tilde * (1.0f - hidden_tilde);
        float proj_sigmoid = sigmoid_f(proj_val);
        float grad_out_val = scan_read(grad_out, input_idx);
        float grad_curr = grad_next + grad_out_val * proj_sigmoid;
        float grad_proj = grad_out_val * (curr - x_val) * proj_sigmoid * (1.0f - proj_sigmoid);
        float grad_gate = grad_curr * (hidden_tilde - prev) * gate_sigmoid * (1.0f - gate_sigmoid);
        float grad_hidden = grad_curr * gate_sigmoid * hidden_grad;
        float grad_prev = grad_curr * (1.0f - gate_sigmoid);

        scan_write(grad_input, input_idx, grad_out_val * (1.0f - proj_sigmoid));
        scan_write(grad_combined, cbase + h + t_offset, grad_hidden);
        scan_write(grad_combined, cbase + p.H + h + t_offset, grad_gate);
        scan_write(grad_combined, cbase + H2 + h + t_offset, grad_proj);
        grad_next = reset[b * p.T_seq + t] > 0.5f ? 0.0f : grad_prev;
    }

    scan_write(grad_state, state_idx, grad_next);
}

kernel void mingru_scan_backward_reset(
    device float* grad_combined          [[buffer(0)]],
    device float* grad_state             [[buffer(1)]],
    device float* grad_input             [[buffer(2)]],
    const device float* grad_out         [[buffer(3)]],
    const device float* grad_next_state  [[buffer(4)]],
    const device float* combined         [[buffer(5)]],
    const device float* state            [[buffer(6)]],
    const device float* input            [[buffer(7)]],
    const device float* curr_buf         [[buffer(8)]],
    const device float* prev_buf         [[buffer(9)]],
    const device float* unused_buf       [[buffer(10)]],
    const device float* reset            [[buffer(11)]],
    constant ScanParams& p               [[buffer(12)]],
    uint idx [[thread_position_in_grid]]
) {
    mingru_scan_backward_reset_body<float>(
        grad_combined, grad_state, grad_input, grad_out, grad_next_state,
        combined, input, curr_buf, prev_buf, reset, p, idx);
}

struct SampleParams {
    uint64_t seed;
    uint offset;
    int num_atns;
    int B;
    int logits_stride;
    int value_stride;
    int is_continuous;  // 1 for continuous, 0 for discrete
    int mask_stride;    // stride between rows in mask buffer
};

inline float masked_logit(float l, float m) {
    if (m < 0.5f) l = -1e9f;
    return l;
}

kernel void sample_logits_kernel(
    device float* actions               [[buffer(0)]],
    device float* logprobs              [[buffer(1)]],
    device float* value_out             [[buffer(2)]],
    const device float* logits          [[buffer(3)]],
    const device float* logstd          [[buffer(4)]],
    const device float* value           [[buffer(5)]],
    const device int* act_sizes         [[buffer(6)]],
    constant SampleParams& sp           [[buffer(7)]],
    const device float* action_mask     [[buffer(8)]],
    uint idx [[thread_position_in_grid]]
) {
    if ((int)idx >= sp.B) return;

    uint offset = sp.offset;

    // Generate Philox RNG state
    uint4 counter = uint4((uint)idx, offset, 0u, 0u);
    uint2 key = uint2((uint)(sp.seed & 0xFFFFFFFF), (uint)(sp.seed >> 32));
    uint4 rng_out = philox4x32_10(counter, key);
    uint rng_idx = 0;

    int logits_base = (int)idx * sp.logits_stride;
    float total_log_prob = 0.0f;

    if (sp.is_continuous) {
        constexpr float LOG_2PI = 1.8378770664093453f;

        for (int h = 0; h < sp.num_atns; h++) {
            float mean = logits[logits_base + h];
            float log_std = logstd[h];
            float std = exp(log_std);

            // Need 2 uniforms for Box-Muller
            float u1 = philox_uniform(rng_idx, rng_out);
            float u2 = philox_uniform(rng_idx, rng_out);
            if (rng_idx >= 4) {
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
        // CUDA joint-ratio: accumulate scalar total_log_prob across heads
        int logits_offset = 0;

        for (int h = 0; h < sp.num_atns; h++) {
            int A = act_sizes[h];

            // Mask base index for this env (mask_stride allows non-contiguous layout)
            int mask_base = (int)idx * sp.mask_stride;

            // Max + logsumexp (with mask)
            float max_val = -INFINITY;
            bool has_valid_action = false;
            for (int a = 0; a < A; a++) {
                has_valid_action = has_valid_action || action_mask[mask_base + logits_offset + a] >= 0.5f;
                float l = masked_logit(logits[logits_base + logits_offset + a],
                                       action_mask[mask_base + logits_offset + a]);
                max_val = fmax(max_val, l);
            }
            if (!has_valid_action) {
                actions[(int)idx * sp.num_atns + h] = NAN;
                total_log_prob = NAN;
                logits_offset += A;
                continue;
            }
            float sum_exp = 0.0f;
            for (int a = 0; a < A; a++) {
                float l = masked_logit(logits[logits_base + logits_offset + a],
                                       action_mask[mask_base + logits_offset + a]);
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

            // Inverse CDF sampling (with mask)
            float cumsum = 0.0f;
            int sampled_action = A - 1;
            for (int a = 0; a < A; a++) {
                float l = masked_logit(logits[logits_base + logits_offset + a],
                                       action_mask[mask_base + logits_offset + a]);
                float prob = exp(l - logsumexp_val);
                cumsum += prob;
                if (rand_val < cumsum) {
                    sampled_action = a;
                    break;
                }
            }

            float log_prob = masked_logit(
                logits[logits_base + logits_offset + sampled_action],
                action_mask[mask_base + logits_offset + sampled_action]) - logsumexp_val;

            actions[(int)idx * sp.num_atns + h] = float(sampled_action);
            total_log_prob += log_prob;

            logits_offset += A;
        }
    }
    // Scalar joint logprob (matches CUDA kernels.cu:995)
    logprobs[(int)idx] = total_log_prob;
    value_out[idx] = value[(int)idx * sp.value_stride];
}

//
// Used when CPU inference produces actions but logprobs need GPU-precision
// exp/log to match PPO training kernels. Without this, the importance ratio
// exp(new_logp - old_logp) sees a systematic IEEE vs fast::exp bias that
// causes NaN with overlap (stale weights amplify the mismatch).

struct RecomputeLogprobsParams {
    int B;
    int num_atns;
    int logits_stride;   // fused_cols per row
    int mask_stride;     // 0 = broadcast (all-ones)
};

kernel void recompute_logprobs_kernel(
    device float* logprobs              [[buffer(0)]],
    const device float* logits          [[buffer(1)]],
    const device float* actions_f32     [[buffer(2)]],
    const device int* act_sizes         [[buffer(3)]],
    const device float* action_mask     [[buffer(4)]],
    constant RecomputeLogprobsParams& rp [[buffer(5)]],
    uint idx [[thread_position_in_grid]]
) {
    if ((int)idx >= rp.B) return;

    int logits_base = (int)idx * rp.logits_stride;
    int mask_base = (rp.mask_stride == 0) ? 0 : (int)idx * rp.mask_stride;

    float total_log_prob = 0.0f;
    int logits_offset = 0;

    for (int h = 0; h < rp.num_atns; h++) {
        int A = act_sizes[h];
        int act = int(actions_f32[(int)idx * rp.num_atns + h]);

        // Max + logsumexp (with mask)
        float max_val = -INFINITY;
        bool has_valid_action = false;
        for (int a = 0; a < A; a++) {
            has_valid_action = has_valid_action || action_mask[mask_base + logits_offset + a] >= 0.5f;
            max_val = fmax(max_val, masked_logit(
                logits[logits_base + logits_offset + a],
                action_mask[mask_base + logits_offset + a]));
        }
        if (!has_valid_action) {
            total_log_prob = NAN;
            logits_offset += A;
            continue;
        }
        float sum_exp = 0.0f;
        for (int a = 0; a < A; a++) {
            sum_exp += exp(masked_logit(
                logits[logits_base + logits_offset + a],
                action_mask[mask_base + logits_offset + a]) - max_val);
        }
        float logsumexp_val = max_val + log(sum_exp);

        float head_lp = masked_logit(
            logits[logits_base + logits_offset + act],
            action_mask[mask_base + logits_offset + act]) - logsumexp_val;
        total_log_prob += head_lp;

        logits_offset += A;
    }
    // Scalar joint logprob (matches CUDA kernels.cu:995)
    logprobs[(int)idx] = total_log_prob;
}

constant int PPO_THREADS = 256;
constant int LOSS_PG = 0;
constant int LOSS_VF = 1;
constant int LOSS_ENT = 2;
constant int LOSS_TOTAL = 3;
constant int LOSS_OLD_APPROX_KL = 4;
constant int LOSS_APPROX_KL = 5;
constant int LOSS_CLIPFRAC = 6;
constant int LOSS_BC = 7;
constant int LOSS_N = 8;
constant int MAX_ATN_HEADS = 16;

inline void ppo_discrete_head(
    const device float* logits,
    int logits_base, int logits_stride_a, int logits_offset,
    int A, int act,
    const device float* mask, int mask_offset,
    thread float& out_logsumexp, thread float& out_entropy, thread float& out_logp
) {
    float max_logit = -INFINITY;
    float sum = 0.0f;
    float act_logit = 0.0f;
    bool has_valid_action = false;

    for (int a = 0; a < A; a++) {
        float m = mask[mask_offset + a];
        float l = masked_logit(logits[logits_base + (logits_offset + a) * logits_stride_a], m);
        if (a == act) act_logit = l;
        if (m < 0.5f) continue;
        has_valid_action = true;
        if (l > max_logit) {
            sum *= exp(max_logit - l);
            max_logit = l;
        }
        sum += exp(l - max_logit);
    }
    if (!has_valid_action || !isfinite(max_logit) || !isfinite(sum) || sum <= 0.0f) {
        out_logsumexp = NAN;
        out_entropy = NAN;
        out_logp = NAN;
        return;
    }
    float lse = max_logit + log(sum);

    float ent = 0.0f;
    for (int a = 0; a < A; a++) {
        if (mask[mask_offset + a] < 0.5f) continue;
        float l = masked_logit(logits[logits_base + (logits_offset + a) * logits_stride_a],
                               mask[mask_offset + a]);
        float logp = l - lse;
        float p = exp(clamp(logp, -80.0f, 80.0f));
        ent -= p * logp;
    }

    out_logsumexp = lse;
    out_entropy = ent;
    out_logp = act_logit - lse;
}

// PPO helper: compute log_prob and entropy for a single continuous head
inline void ppo_continuous_head(
    float mean, float log_std, float action,
    thread float& out_logp, thread float& out_entropy
) {
    constexpr float HALF_LOG_2PI = 0.9189385332046727f;
    constexpr float HALF_1_PLUS_LOG_2PI = 1.4189385332046727f;
    float std = exp(log_std);
    float normalized = (action - mean) / std;
    out_logp = -0.5f * normalized * normalized - HALF_LOG_2PI - log_std;
    out_entropy = HALF_1_PLUS_LOG_2PI + log_std;
}

struct PPOFusedParams {
    int num_atns;
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
    int is_continuous;
    int mask_stride;     // stride in floats between consecutive mask rows in obs
};

// Fused PPO forward + backward: computes loss partials AND gradients in one pass.
// Per-block partial sums written to ppo_partials, reduced by ppo_loss_reduce_kernel.
kernel void ppo_loss_fwd_bwd_kernel(
    device float* ppo_partials              [[buffer(0)]],
    device float* grad_logits               [[buffer(1)]],
    device float* grad_logstd               [[buffer(2)]],
    device float* grad_values_pred          [[buffer(3)]],
    const device float* logits              [[buffer(4)]],
    const device float* logstd              [[buffer(5)]],
    const device float* values_pred         [[buffer(6)]],
    const device float* actions             [[buffer(7)]],
    const device float* old_logprobs        [[buffer(8)]],
    const device float* advantages          [[buffer(9)]],
    const device float* prio                [[buffer(10)]],
    const device float* values              [[buffer(11)]],
    const device float* returns_buf         [[buffer(12)]],
    const device float* adv_mean            [[buffer(13)]],
    const device float* adv_var             [[buffer(14)]],
    const device int* act_sizes             [[buffer(15)]],
    constant PPOFusedParams& pp             [[buffer(16)]],
    const device float* action_mask         [[buffer(17)]],
    device float* out_ratio                 [[buffer(18)]],
    device float* out_newvalue              [[buffer(19)]],
    const device float* row_weights         [[buffer(20)]],
    const device float* bc_weights          [[buffer(21)]],
    const device float* bc_actions          [[buffer(22)]],
    const device float* head_weights        [[buffer(23)]],
    uint idx [[thread_position_in_grid]],
    uint tid [[thread_index_in_threadgroup]],
    uint block_id [[threadgroup_position_in_grid]]
) {
    int total_elements = pp.N * pp.T_seq;
    float inv_NT = 1.0f / float(total_elements);

    threadgroup float block_losses[LOSS_N][PPO_THREADS];
    for (int c = 0; c < LOSS_N; c++) {
        block_losses[c][tid] = 0.0f;
    }

    // MSL has no goto — use if/else instead of the CUDA goto pattern
    if ((int)idx < total_elements) {
        int n = (int)idx / pp.T_seq;
        int t = (int)idx % pp.T_seq;
        int nt = n * pp.T_seq + t;

        int logits_base = n * pp.logits_stride_n + t * pp.logits_stride_t;
        int values_idx = n * pp.values_stride_n + t * pp.values_stride_t;
        int grad_logits_base = nt * pp.A_total;

        // Shared computation (used by both forward and backward)
        float adv = advantages[nt];
        float w = prio[n];
        float val = values[nt];
        float ret = returns_buf[nt];
        float val_pred = values_pred[values_idx];
        out_newvalue[nt] = val_pred;

        float adv_std = sqrt(adv_var[0]);
        float adv_normalized = (adv - adv_mean[0]) / (adv_std + 1e-8f);

        float dL = inv_NT;
        float rw = row_weights[nt];
        float d_pg_loss = dL * rw;
        float d_entropy_term = dL * (-pp.ent_coef) * rw;

        // Value loss (forward) + value gradient (backward)
        float v_error = val_pred - val;
        float v_clipped = val + clamp(v_error, -pp.vf_clip_coef, pp.vf_clip_coef);
        float v_loss_unclipped = (val_pred - ret) * (val_pred - ret);
        float v_loss_clipped = (v_clipped - ret) * (v_clipped - ret);
        float v_loss = 0.5f * fmax(v_loss_unclipped, v_loss_clipped);

        float d_val_pred = 0.0f;
        if (v_loss_clipped > v_loss_unclipped) {
            if (v_error >= -pp.vf_clip_coef && v_error <= pp.vf_clip_coef) {
                d_val_pred = v_clipped - ret;
            }
        } else {
            d_val_pred = val_pred - ret;
        }
        grad_values_pred[nt] = (rw > 0.0f) ? (dL * pp.vf_coef * d_val_pred) : 0.0f;

        // Policy loss + gradients. Both branches produce pg_loss, total_entropy,
        // logratio, ratio — the block-loss accumulation is shared after the if/else.
        float pg_loss, total_entropy, logratio, ratio;

        if (pp.is_continuous) {
            // Continuous: old_logprobs is scalar (matches CUDA)
            float old_logp = old_logprobs[nt];
            float total_log_prob = 0.0f;
            total_entropy = 0.0f;
            for (int h = 0; h < pp.num_atns; h++) {
                float mean = logits[logits_base + h * pp.logits_stride_a];
                float log_std = logstd[h];
                float action = actions[nt * pp.num_atns + h];
                float lp, ent;
                ppo_continuous_head(mean, log_std, action, lp, ent);
                total_log_prob += lp;
                total_entropy += ent;
            }

            logratio = total_log_prob - old_logp;
            ratio = exp(logratio);
            out_ratio[nt] = ratio;
            float ratio_clipped = clamp(ratio, 1.0f - pp.clip_coef, 1.0f + pp.clip_coef);
            float wa = -w * adv_normalized;
            pg_loss = fmax(wa * ratio, wa * ratio_clipped);

            // Backward: policy gradient
            float d_ratio = wa * d_pg_loss;
            if (wa * ratio_clipped > wa * ratio) {
                if (ratio <= (1.0f - pp.clip_coef) || ratio >= (1.0f + pp.clip_coef))
                    d_ratio = 0.0f;
            }
            float d_new_logp = d_ratio * ratio;

            for (int h = 0; h < pp.num_atns; h++) {
                float mean = logits[logits_base + h * pp.logits_stride_a];
                float log_std = logstd[h];
                float std = exp(log_std);
                float var = std * std;
                float action = actions[nt * pp.num_atns + h];
                float diff = action - mean;

                grad_logits[grad_logits_base + h] = d_new_logp * diff / var;
                grad_logstd[grad_logits_base + h] = d_new_logp * (diff * diff / var - 1.0f) + d_entropy_term;
            }
        } else {
            // Discrete joint-ratio clipping (matches CUDA kernels.cu:738-807).
            // Sum per-head logprobs into scalar, compute single joint ratio, clip once.
            float head_logsumexp[MAX_ATN_HEADS];
            float head_entropy[MAX_ATN_HEADS];
            int head_act[MAX_ATN_HEADS];
            int mask_base = (int)idx * pp.mask_stride;

            int logits_offset = 0;
            float total_log_prob = 0.0f;
            total_entropy = 0.0f;

            for (int h = 0; h < pp.num_atns; h++) {
                int A = act_sizes[h];
                int act = int(actions[nt * pp.num_atns + h]);
                head_act[h] = act;
                float lse, ent, lp;
                ppo_discrete_head(logits, logits_base, pp.logits_stride_a, logits_offset,
                    A, act, action_mask, mask_base + logits_offset, lse, ent, lp);
                head_logsumexp[h] = lse;
                head_entropy[h] = ent;
                total_log_prob += lp;
                total_entropy += ent;
                logits_offset += A;
            }

            float old_logp = old_logprobs[nt];
            logratio = total_log_prob - old_logp;
            ratio = (rw > 0.0f) ? exp(logratio) : 1.0f;
            float ratio_clipped = clamp(ratio, 1.0f - pp.clip_coef, 1.0f + pp.clip_coef);
            float wa = -w * adv_normalized;
            pg_loss = fmax(wa * ratio, wa * ratio_clipped);

            // gate d_new_logp computation by rw: ratio can be inf on BC
            // rows (extreme logits on out-of-distribution obs), and
            // 0 * inf = NaN propagates into grad_logits.
            float d_new_logp = 0.0f;
            if (rw > 0.0f) {
                float d_ratio = wa * d_pg_loss;
                if (wa * ratio_clipped > wa * ratio) {
                    if (ratio <= (1.0f - pp.clip_coef) || ratio >= (1.0f + pp.clip_coef))
                        d_ratio = 0.0f;
                }
                d_new_logp = d_ratio * ratio;
                out_ratio[nt] = ratio;
            } else {
                out_ratio[nt] = 1.0f;
            }

            float bc_w = bc_weights[nt];
            float bc_loss_acc = 0.0f;
            logits_offset = 0;
            for (int h = 0; h < pp.num_atns; h++) {
                int A = act_sizes[h];
                int act = head_act[h];
                int bc_act = (bc_w > 0.0f) ? int(bc_actions[nt * pp.num_atns + h]) : -1;
                float head_w = head_weights[h];
                float lse = head_logsumexp[h];
                float ent = head_entropy[h];
                bool head_finite = isfinite(lse) && isfinite(ent);
                /* state drift can leave a BC action with mask=0 in the current
                   obs; skip BC for that head rather than feeding a -1e9 logit
                   into CE */
                bool bc_valid = bc_w > 0.0f && bc_act >= 0 && bc_act < A && head_finite &&
                    action_mask[mask_base + logits_offset + bc_act] >= 0.5f;

                for (int a = 0; a < A; a++) {
                    float raw_l = logits[logits_base + (logits_offset + a) * pp.logits_stride_a];
                    float m = action_mask[mask_base + logits_offset + a];
                    if (m < 0.5f || !head_finite) {
                        grad_logits[grad_logits_base + logits_offset + a] = 0.0f;
                        continue;
                    }
                    float l = masked_logit(raw_l, m);
                    float logp = l - lse;
                    float p = exp(logp);
                    float d_logit = 0.0f;
                    if (rw > 0.0f) {
                        d_logit = (a == act) ? d_new_logp : 0.0f;
                        d_logit -= p * d_new_logp;
                        d_logit += d_entropy_term * p * (-ent - logp);
                    }
                    if (bc_valid) {
                        float bc_indicator = (a == bc_act) ? 1.0f : 0.0f;
                        d_logit += bc_w * head_w * (p - bc_indicator) * dL;
                        if (a == bc_act) bc_loss_acc += head_w * (lse - l);
                    }
                    grad_logits[grad_logits_base + logits_offset + a] = d_logit;
                }
                logits_offset += A;
            }
            block_losses[LOSS_BC][tid] = bc_w * bc_loss_acc * inv_NT;
        }

        // gate by rw because NaN * 0 = NaN in IEEE; BC rows with extreme
        // policy outputs would otherwise poison online PPO stats.
        if (rw > 0.0f) {
            block_losses[LOSS_PG][tid] = pg_loss * inv_NT;
            block_losses[LOSS_VF][tid] = v_loss * inv_NT;
            block_losses[LOSS_ENT][tid] = total_entropy * inv_NT;
            block_losses[LOSS_TOTAL][tid] = (pg_loss + pp.vf_coef * v_loss - pp.ent_coef * total_entropy) * inv_NT;
            block_losses[LOSS_OLD_APPROX_KL][tid] = (-logratio) * inv_NT;
            block_losses[LOSS_APPROX_KL][tid] = ((ratio - 1.0f) - logratio) * inv_NT;
            block_losses[LOSS_CLIPFRAC][tid] = (abs(ratio - 1.0f) > pp.clip_coef ? 1.0f : 0.0f) * inv_NT;
        }
    } // end if (idx < total_elements)

    threadgroup_barrier(mem_flags::mem_threadgroup);

    // Block reduction (tree reduction)
    for (int stride = PPO_THREADS / 2; stride > 0; stride >>= 1) {
        if ((int)tid < stride) {
            for (int c = 0; c < LOSS_N; c++) {
                block_losses[c][tid] += block_losses[c][tid + stride];
            }
        }
        threadgroup_barrier(mem_flags::mem_threadgroup);
    }

    if (tid == 0) {
        int base_out = (int)block_id * (LOSS_N + 1);
        ppo_partials[base_out] = block_losses[LOSS_TOTAL][0];
        for (int c = 0; c < LOSS_N; c++) {
            ppo_partials[base_out + 1 + c] = block_losses[c][0];
        }
    }
}

// Deterministic reduction of per-block PPO loss partials + count increment
kernel void ppo_loss_reduce_kernel(
    device float* loss                      [[buffer(0)]],
    device float* losses_acc                [[buffer(1)]],
    const device float* partials            [[buffer(2)]],
    constant int& num_blocks                [[buffer(3)]],
    uint tid [[thread_index_in_threadgroup]]
) {
    if ((int)tid > LOSS_N) return;

    float sum = 0.0f;
    for (int b = 0; b < num_blocks; b++) {
        sum += partials[b * (LOSS_N + 1) + (int)tid];
    }

    if (tid == 0) {
        *loss += sum;
    } else {
        losses_acc[(int)tid - 1] += sum;
    }

    // Fold add_scalar: increment epoch count
    if (tid == 0) {
        losses_acc[LOSS_N] += 1.0f;
    }
}

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
        bool vector_mode = (p.horizon % 4) == 0;
        float delta = vector_mode
            ? rho_t * (r[t_next] + p.gamma * v[t_next] * nextnonterminal - v[t])
            : rho_t * r[t_next] + p.gamma * v[t_next] * nextnonterminal - v[t];
        lastpufferlam = delta + p.gamma * p.lambda * c_t * lastpufferlam * nextnonterminal;
        adv[t] = lastpufferlam;
    }
}

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
    for (int t = (int)tx; t < pp.stride; t += 32) {
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

kernel void prio_normalize_kernel(
    device float* prio_weights          [[buffer(0)]],
    constant int& length                [[buffer(1)]],
    uint tx [[thread_index_in_threadgroup]],
    uint simd_lane [[thread_index_in_simdgroup]],
    uint simd_id [[simdgroup_index_in_threadgroup]]
) {
    constexpr float eps = 1e-6f;
    constexpr int NUM_WARPS = 8;  // 256 / 32

    threadgroup float shmem[NUM_WARPS];
    threadgroup float block_sum;

    float local_sum = 0.0f;
    for (int t = (int)tx; t < length; t += 256) {
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

    for (int t = (int)tx; t < length; t += 256) {
        prio_weights[t] = (prio_weights[t] + eps) / block_sum;
    }
}

struct PrioImpParams {
    int total_agents;
    float anneal_beta;
    int minibatch_segments;
};

struct PrioSampleParams {
    uint64_t seed;
    uint base_offset;
    int total_segments;
    int minibatch_segments;
};

kernel void prio_sample_kernel(
    device int64_t* indices            [[buffer(0)]],
    const device float* prio_probs     [[buffer(1)]],
    constant PrioSampleParams& pp      [[buffer(2)]],
    uint tx [[thread_position_in_grid]]
) {
    if ((int)tx >= pp.minibatch_segments) return;

    uint4 counter = uint4(pp.base_offset + tx, 0u, 0u, 0u);
    uint2 key = uint2((uint)(pp.seed & 0xFFFFFFFF), (uint)(pp.seed >> 32));
    uint4 rng_out = philox4x32_10(counter, key);
    uint rng_idx = 0;
    float u = philox_uniform(rng_idx, rng_out);

    float cumsum = 0.0f;
    int sampled = pp.total_segments - 1;
    for (int i = 0; i < pp.total_segments; i++) {
        cumsum += prio_probs[i];
        if (u <= cumsum) {
            sampled = i;
            break;
        }
    }
    indices[tx] = sampled;
}

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

struct FillParams {
    float val;
    int n;
};

kernel void fill_f32(
    device float* dst           [[buffer(0)]],
    constant FillParams& p      [[buffer(1)]],
    uint idx [[thread_position_in_grid]]
) {
    if ((int)idx < p.n) dst[idx] = p.val;
}

kernel void copy_f32(
    device float* dst           [[buffer(0)]],
    device const float* src     [[buffer(1)]],
    constant int& n             [[buffer(2)]],
    uint idx [[thread_position_in_grid]]
) {
    if ((int)idx < n) dst[idx] = src[idx];
}

struct ClampParams {
    float lo;
    float hi;
    int n;
};

kernel void clamp_f32(
    device float* dst           [[buffer(0)]],
    constant ClampParams& p     [[buffer(1)]],
    uint idx [[thread_position_in_grid]]
) {
    if ((int)idx < p.n) dst[idx] = clamp(dst[idx], p.lo, p.hi);
}

struct ScaleParams {
    float alpha;
    int n;
};

kernel void scale_f32(
    device float* dst           [[buffer(0)]],
    constant ScaleParams& p     [[buffer(1)]],
    uint idx [[thread_position_in_grid]]
) {
    if ((int)idx < p.n) dst[idx] *= p.alpha;
}

struct AxpyParams {
    float alpha;
    int n;
};

kernel void axpy_f32(
    device float* dst               [[buffer(0)]],
    const device float* src         [[buffer(1)]],
    constant AxpyParams& p          [[buffer(2)]],
    uint idx [[thread_position_in_grid]]
) {
    if ((int)idx < p.n) dst[idx] += p.alpha * src[idx];
}

kernel void add_f32(
    device float* dst               [[buffer(0)]],
    const device float* src         [[buffer(1)]],
    constant int& n                 [[buffer(2)]],
    uint idx [[thread_position_in_grid]]
) {
    if ((int)idx < n) dst[idx] += src[idx];
}

kernel void add_f16(
    device half* dst                [[buffer(0)]],
    const device half* src          [[buffer(1)]],
    constant int& n                 [[buffer(2)]],
    uint idx [[thread_position_in_grid]]
) {
    if ((int)idx < n) dst[idx] = half(float(dst[idx]) + float(src[idx]));
}

struct NesterovParams {
    float mu;
    int n;
};

// Fused Nesterov momentum: mb = mu*mb + gc; gc = gc + mu*mb (note: gc uses updated mb)
kernel void nesterov_f32(
    device float* mb                [[buffer(0)]],
    device float* gc                [[buffer(1)]],
    constant NesterovParams& p      [[buffer(2)]],
    uint idx [[thread_position_in_grid]]
) {
    if ((int)idx < p.n) {
        float m = p.mu * mb[idx] + gc[idx];
        mb[idx] = m;
        gc[idx] += p.mu * m;
    }
}

struct MuonParams {
    int n;
    float scale;
};

// Weight update: wb = wb * (1 - lr * wd) - lr * scale * up  (matches CUDA muon.cu)
kernel void muon_weight_update_kernel(
    device float* wb                    [[buffer(0)]],
    const device float* up              [[buffer(1)]],
    const device float* lr_ptr          [[buffer(2)]],
    constant MuonParams& p              [[buffer(3)]],
    uint idx [[thread_position_in_grid]]
) {
    if ((int)idx >= p.n) return;
    float lr = *lr_ptr;
    wb[idx] = wb[idx] - lr * p.scale * up[idx];
}

struct AnchorBlendParams {
    int n;
    float coef;
};

kernel void anchor_blend_weights_kernel(
    device float* weights               [[buffer(0)]],
    const device float* anchor          [[buffer(1)]],
    constant AnchorBlendParams& p       [[buffer(2)]],
    uint idx [[thread_position_in_grid]]
) {
    if ((int)idx >= p.n) return;
    weights[idx] += p.coef * (anchor[idx] - weights[idx]);
}

struct TransposeParams {
    int R;
    int C;
};

// Transpose R x C matrix: dst[c*R + r] = src[r*C + c]
kernel void transpose_f32(
    device float* dst               [[buffer(0)]],
    const device float* src         [[buffer(1)]],
    constant TransposeParams& p     [[buffer(2)]],
    uint idx [[thread_position_in_grid]]
) {
    if ((int)idx >= p.R * p.C) return;
    dst[(idx % p.C) * p.R + idx / p.C] = src[idx];
}

struct Transpose01Params {
    int A;
    int B;
    int C;
};

template <typename T>
inline void transpose_01_body(
    device T* dst,
    const device T* src,
    constant Transpose01Params& p,
    uint idx
) {
    int total = p.A * p.B * p.C;
    if ((int)idx >= total) return;
    int a = (int)idx / (p.B * p.C);
    int rem = (int)idx % (p.B * p.C);
    int b = rem / p.C;
    int c = rem % p.C;
    dst[b * p.A * p.C + a * p.C + c] = src[idx];
}

kernel void transpose_01(
    device float* dst               [[buffer(0)]],
    const device float* src         [[buffer(1)]],
    constant Transpose01Params& p   [[buffer(2)]],
    uint idx [[thread_position_in_grid]]
) {
    transpose_01_body<float>(dst, src, p, idx);
}

kernel void transpose_01_u64(
    device uint2* dst                   [[buffer(0)]],
    const device uint2* src             [[buffer(1)]],
    constant Transpose01Params& p       [[buffer(2)]],
    uint idx [[thread_position_in_grid]]
) {
    transpose_01_body<uint2>(dst, src, p, idx);
}

// Per-block sum of squares (partial reduction)
kernel void norm_f32_kernel(
    device float* partials              [[buffer(0)]],
    const device float* src             [[buffer(1)]],
    constant int& n                     [[buffer(2)]],
    uint idx [[thread_position_in_grid]],
    uint tid [[thread_index_in_threadgroup]],
    uint block_id [[threadgroup_position_in_grid]],
    uint grid_size [[threads_per_grid]],
    uint simd_lane [[thread_index_in_simdgroup]],
    uint simd_id [[simdgroup_index_in_threadgroup]]
) {
    constexpr int NUM_WARPS = 8;  // 256 / 32
    threadgroup float sdata[NUM_WARPS];
    float sum = 0.0f;
    for (int i = (int)idx; i < n; i += (int)grid_size) {
        sum += src[i] * src[i];
    }
    sum = simd_sum(sum);
    if (simd_lane == 0) sdata[simd_id] = sum;
    threadgroup_barrier(mem_flags::mem_threadgroup);
    if (simd_id == 0) {
        sum = (simd_lane < NUM_WARPS) ? sdata[simd_lane] : 0.0f;
        sum = simd_sum(sum);
        if (simd_lane == 0) partials[block_id] = sum;
    }
}

// Reduce per-block partials to a single sum-of-squares value
kernel void norm_reduce_kernel(
    device float* out                   [[buffer(0)]],
    const device float* partials        [[buffer(1)]],
    constant int& num_blocks            [[buffer(2)]],
    uint tid [[thread_index_in_threadgroup]],
    uint simd_lane [[thread_index_in_simdgroup]],
    uint simd_id [[simdgroup_index_in_threadgroup]]
) {
    constexpr int NUM_WARPS = 8;
    threadgroup float sdata[NUM_WARPS];
    float val = ((int)tid < num_blocks) ? partials[tid] : 0.0f;
    val = simd_sum(val);
    if (simd_lane == 0) sdata[simd_id] = val;
    threadgroup_barrier(mem_flags::mem_threadgroup);
    if (simd_id == 0) {
        val = (simd_lane < NUM_WARPS) ? sdata[simd_lane] : 0.0f;
        val = simd_sum(val);
        if (simd_lane == 0) *out = val;
    }
}

struct ClipByNormParams {
    float max_norm;
    float eps;
    int n;
};

// Clip gradient by global norm: dst[i] *= min(max_norm / (sqrt(sum_sq) + eps), 1.0)
// When clip_coef is 0 (norm overflow), zero directly to avoid inf*0=NaN.
kernel void clip_by_norm_f32(
    device float* dst                       [[buffer(0)]],
    const device float* sum_sq_ptr          [[buffer(1)]],
    constant ClipByNormParams& p            [[buffer(2)]],
    uint idx [[thread_position_in_grid]]
) {
    float clip_coef = min(p.max_norm / (sqrt(*sum_sq_ptr) + p.eps), 1.0f);
    if ((int)idx < p.n) {
        dst[idx] = (clip_coef > 0.0f) ? dst[idx] * clip_coef : 0.0f;
    }
}

struct NormalizeParams {
    float eps;
    int n;
};

// dst[i] /= max(sqrt(*norm), eps) — matches CUDA (no cap)
kernel void normalize_f32(
    device float* dst                       [[buffer(0)]],
    const device float* norm_ptr            [[buffer(1)]],
    constant NormalizeParams& p             [[buffer(2)]],
    uint idx [[thread_position_in_grid]]
) {
    float inv_norm = 1.0f / max(sqrt(*norm_ptr), p.eps);
    if ((int)idx < p.n) dst[idx] = dst[idx] * inv_norm;
}

// Compute variance and mean of a float array (single threadgroup)
kernel void var_mean_kernel(
    const device float* src         [[buffer(0)]],
    device float* var_out           [[buffer(1)]],
    device float* mean_out          [[buffer(2)]],
    constant int& n                 [[buffer(3)]],
    uint tid [[thread_index_in_threadgroup]],
    uint simd_lane [[thread_index_in_simdgroup]],
    uint simd_id [[simdgroup_index_in_threadgroup]]
) {
    constexpr int NUM_WARPS = 8;
    threadgroup float sdata[NUM_WARPS];

    // Pass 1: compute mean
    float sum = 0.0f;
    for (int i = (int)tid; i < n; i += 256) sum += src[i];
    sum = simd_sum(sum);
    if (simd_lane == 0) sdata[simd_id] = sum;
    threadgroup_barrier(mem_flags::mem_threadgroup);
    if (simd_id == 0) {
        sum = (simd_lane < NUM_WARPS) ? sdata[simd_lane] : 0.0f;
        sum = simd_sum(sum);
        if (simd_lane == 0) sdata[0] = sum;
    }
    threadgroup_barrier(mem_flags::mem_threadgroup);
    float mean = sdata[0] / float(n);
    if (tid == 0) *mean_out = mean;

    // Pass 2: compute variance
    float ss = 0.0f;
    for (int i = (int)tid; i < n; i += 256) {
        float d = src[i] - mean;
        ss += d * d;
    }
    ss = simd_sum(ss);
    if (simd_lane == 0) sdata[simd_id] = ss;
    threadgroup_barrier(mem_flags::mem_threadgroup);
    if (simd_id == 0) {
        ss = (simd_lane < NUM_WARPS) ? sdata[simd_lane] : 0.0f;
        ss = simd_sum(ss);
        if (simd_lane == 0) sdata[0] = ss;
    }
    threadgroup_barrier(mem_flags::mem_threadgroup);
    if (tid == 0) *var_out = sdata[0] / float(n - 1);
}

struct SumRowsParams {
    int R;
    int C;
};

// dst[c] = sum over rows of src[:, c]
kernel void sum_rows_to_f32_kernel(
    device float* dst               [[buffer(0)]],
    const device float* src         [[buffer(1)]],
    constant SumRowsParams& p       [[buffer(2)]],
    uint col [[thread_position_in_grid]]
) {
    if ((int)col >= p.C) return;
    float sum = 0.0f;
    for (int r = 0; r < p.R; r++) sum += src[r * p.C + (int)col];
    dst[col] = sum;
}

struct AssembleDecoderGradParams {
    int B_TT;
    int od;
    int od_plus_1;
};

// Assemble gradient: dst = [grad_logits | grad_value] in fused layout
kernel void assemble_decoder_grad_f32(
    device float* dst                       [[buffer(0)]],
    const device float* grad_logits         [[buffer(1)]],
    const device float* grad_value          [[buffer(2)]],
    constant AssembleDecoderGradParams& p   [[buffer(3)]],
    uint idx [[thread_position_in_grid]]
) {
    if ((int)idx >= p.B_TT * p.od_plus_1) return;
    int row = (int)idx / p.od_plus_1;
    int col = (int)idx % p.od_plus_1;
    dst[idx] = (col < p.od) ? grad_logits[row * p.od + col] : grad_value[row];
}

struct SelectCopyParams {
    int obs_row_bytes;
    int act_row_bytes;
    int lp_row_bytes;
    int term_row_bytes;
    int horizon;
    int train_fp16;  // 1: encoder reads fp16_obs_out (skip mb_obs f32 write); 0: encoder reads mb_obs (skip f16 write)
};

// Minibatch assembly: copy observations, actions, logprobs, values+advantages+returns, prio
// Channel 0 fuses obs gather + f32→f16 cast. Only the variant the encoder will read gets written.
// Dispatched as (minibatch_size, 5) threadgroups, each handles one channel for one row.
kernel void select_copy_kernel(
    device char* mb_obs                 [[buffer(0)]],
    device char* mb_actions             [[buffer(1)]],
    device char* mb_logprobs            [[buffer(2)]],
    device float* mb_values             [[buffer(3)]],
    device float* mb_advantages         [[buffer(4)]],
    device float* mb_returns            [[buffer(5)]],
    device float* mb_prio_out           [[buffer(6)]],
    device char* mb_terminals           [[buffer(7)]],
    const device char* src_obs          [[buffer(8)]],
    const device char* src_actions      [[buffer(9)]],
    const device char* src_logprobs     [[buffer(10)]],
    const device float* src_values      [[buffer(11)]],
    const device float* advantages      [[buffer(12)]],
    const device int64_t* idx           [[buffer(13)]],
    const device float* mb_prio_in      [[buffer(14)]],
    const device char* src_terminals    [[buffer(15)]],
    constant SelectCopyParams& p        [[buffer(16)]],
    device half* fp16_obs_out           [[buffer(17)]],
    uint2 group_id [[threadgroup_position_in_grid]],
    uint tid [[thread_index_in_threadgroup]]
) {
    int mb = (int)group_id.x;
    int ch = (int)group_id.y;
    int src_row = (int)idx[mb];

    if (ch == 0) {
        // Fused obs gather + (optional) f32→f16 cast. Encoder reads exactly one variant
        // depending on train_fp16; the other is dead-on-arrival, so skip writing it.
        const device float* sptr = (const device float*)(src_obs + (int64_t)src_row * p.obs_row_bytes);
        int count = p.obs_row_bytes / 4;
        if (p.train_fp16) {
            device half* f16ptr = fp16_obs_out + (int64_t)mb * count;
            for (int i = (int)tid; i < count; i += 256) {
                f16ptr[i] = half(sptr[i]);
            }
        } else {
            device float* f32ptr = (device float*)(mb_obs + (int64_t)mb * p.obs_row_bytes);
            for (int i = (int)tid; i < count; i += 256) {
                f32ptr[i] = sptr[i];
            }
        }
    } else if (ch == 1) {
        // Copy actions
        const device int* sptr = (const device int*)(src_actions + (int64_t)src_row * p.act_row_bytes);
        device int* dptr = (device int*)(mb_actions + (int64_t)mb * p.act_row_bytes);
        for (int i = (int)tid; i < p.act_row_bytes / 4; i += 256) dptr[i] = sptr[i];
    } else if (ch == 2) {
        // Copy logprobs
        const device int* sptr = (const device int*)(src_logprobs + (int64_t)src_row * p.lp_row_bytes);
        device int* dptr = (device int*)(mb_logprobs + (int64_t)mb * p.lp_row_bytes);
        for (int i = (int)tid; i < p.lp_row_bytes / 4; i += 256) dptr[i] = sptr[i];
    } else if (ch == 3) {
        // Copy values + advantages, compute returns = values + advantages
        int srh = src_row * p.horizon;
        int drh = mb * p.horizon;
        for (int i = (int)tid; i < p.horizon; i += 256) {
            float val = src_values[srh + i];
            float adv = advantages[srh + i];
            mb_values[drh + i] = val;
            mb_advantages[drh + i] = adv;
            mb_returns[drh + i] = val + adv;
        }
    } else if (ch == 4) {
        // Copy prio weight
        if (tid == 0) {
            mb_prio_out[mb] = mb_prio_in[mb];
        }
    } else if (ch == 5) {
        const device int* sptr = (const device int*)(src_terminals + (int64_t)src_row * p.term_row_bytes);
        device int* dptr = (device int*)(mb_terminals + (int64_t)mb * p.term_row_bytes);
        for (int i = (int)tid; i < p.term_row_bytes / 4; i += 256) dptr[i] = sptr[i];
    }
}

struct IndexCopyParams {
    int num_idx;
    int row_bytes;
};

inline void copy_indexed_row(device char* dst, const device char* src, int row_bytes) {
    int words = row_bytes / 4;
    const device uint* src4 = (const device uint*)src;
    device uint* dst4 = (device uint*)dst;
    for (int b = 0; b < words; b++) dst4[b] = src4[b];
    for (int b = words * 4; b < row_bytes; b++) dst[b] = src[b];
}

kernel void index_copy_kernel(
    device char* dst                    [[buffer(0)]],
    const device int64_t* idx           [[buffer(1)]],
    const device char* src              [[buffer(2)]],
    constant IndexCopyParams& p         [[buffer(3)]],
    uint i [[thread_position_in_grid]]
) {
    if ((int)i >= p.num_idx) return;
    int64_t dst_row = idx[i];
    copy_indexed_row(dst + dst_row * p.row_bytes,
                     src + (int64_t)i * p.row_bytes,
                     p.row_bytes);
}

kernel void index_gather_kernel(
    device char* dst                    [[buffer(0)]],
    const device int64_t* idx           [[buffer(1)]],
    const device char* src              [[buffer(2)]],
    constant IndexCopyParams& p         [[buffer(3)]],
    uint i [[thread_position_in_grid]]
) {
    if ((int)i >= p.num_idx) return;
    int64_t src_row = idx[i];
    copy_indexed_row(dst + (int64_t)i * p.row_bytes,
                     src + src_row * p.row_bytes,
                     p.row_bytes);
}

struct GemmParams {
    int M;       // result rows
    int N;       // result columns
    int K;       // inner dimension
    int lda;     // leading dimension of A (physical columns)
    int ldb;     // leading dimension of B (physical columns)
    int ldc;     // leading dimension of C (= N)
    float alpha;
    float beta;
    int trans_a; // 0 = no transpose, 1 = transpose
    int trans_b;
};

constant int BM = 32;   // tile rows (shared by ksplit and fp16 register GEMMs)
constant int BN = 32;   // tile cols
constant int BK = 16;   // tile K dimension
constant int TM = 4;    // per-thread tile rows
constant int TN = 4;    // per-thread tile cols

kernel void sgemm_ksplit(
    device const float* A          [[buffer(0)]],
    device const float* B          [[buffer(1)]],
    device float* partials         [[buffer(2)]],
    constant GemmParams& p         [[buffer(3)]],
    constant int& k_per_split      [[buffer(4)]],
    uint3 group_id    [[threadgroup_position_in_grid]],
    uint3 local_id    [[thread_position_in_threadgroup]]
) {
    threadgroup float sA[BM][BK];
    threadgroup float sB[BK][BN];

    int trow = (int)local_id.y;
    int tcol = (int)local_id.x;
    int tid = trow * (BN / TN) + tcol;

    float acc[TM][TN];
    for (int m = 0; m < TM; m++)
        for (int n = 0; n < TN; n++)
            acc[m][n] = 0.0f;

    int k_start = (int)group_id.z * k_per_split;
    int k_end = min(k_start + k_per_split, p.K);
    int num_k_tiles = (k_end - k_start + BK - 1) / BK;

    for (int kt = 0; kt < num_k_tiles; kt++) {
        int k_base = k_start + kt * BK;

        for (int i = 0; i < (BM * BK) / 64; i++) {
            int idx = tid + i * 64;
            int r = idx / BK;
            int c = idx % BK;
            int gr = (int)group_id.y * BM + r;
            int gc = k_base + c;
            sA[r][c] = (gr < p.M && gc < k_end)
                ? (p.trans_a ? A[gc * p.lda + gr] : A[gr * p.lda + gc])
                : 0.0f;
        }

        for (int i = 0; i < (BK * BN) / 64; i++) {
            int idx = tid + i * 64;
            int r = idx / BN;
            int c = idx % BN;
            int gr = k_base + r;
            int gc = (int)group_id.x * BN + c;
            sB[r][c] = (gr < k_end && gc < p.N)
                ? (p.trans_b ? B[gc * p.ldb + gr] : B[gr * p.ldb + gc])
                : 0.0f;
        }

        threadgroup_barrier(mem_flags::mem_threadgroup);

        for (int k = 0; k < BK; k++) {
            float a_reg[TM];
            float b_reg[TN];
            for (int m = 0; m < TM; m++) a_reg[m] = sA[trow * TM + m][k];
            for (int n = 0; n < TN; n++) b_reg[n] = sB[k][tcol * TN + n];
            for (int m = 0; m < TM; m++)
                for (int n = 0; n < TN; n++)
                    acc[m][n] += a_reg[m] * b_reg[n];
        }

        threadgroup_barrier(mem_flags::mem_threadgroup);
    }

    // Write partial results: partials[split_idx * M * N + row * N + col]
    int split_offset = (int)group_id.z * p.M * p.N;
    int row_base = (int)group_id.y * BM + trow * TM;
    int col_base = (int)group_id.x * BN + tcol * TN;
    for (int m = 0; m < TM; m++) {
        for (int n = 0; n < TN; n++) {
            int r = row_base + m;
            int c = col_base + n;
            if (r < p.M && c < p.N) {
                partials[split_offset + r * p.N + c] = acc[m][n];
            }
        }
    }
}

struct ReduceKsplitParams {
    int MN;
    int num_splits;
    float alpha;
    float beta;
};

kernel void reduce_ksplit(
    device const float* partials   [[buffer(0)]],
    device float* C                [[buffer(1)]],
    constant ReduceKsplitParams& p [[buffer(2)]],
    uint gid [[thread_position_in_grid]]
) {
    if ((int)gid >= p.MN) return;
    float sum = 0.0f;
    for (int s = 0; s < p.num_splits; s++) {
        sum += partials[s * p.MN + (int)gid];
    }
    C[gid] = p.alpha * sum + p.beta * C[gid];
}

// Reads fp32 grad_logits and grad_value (from PPO kernel), writes fp16 grad_out.
// grad_out[row * od1 + col] = (col < od) ? grad_logits[row * od + col] : grad_value[row]
kernel void assemble_decoder_grad_f32_to_f16(
    device half* grad_out                   [[buffer(0)]],
    const device float* grad_logits         [[buffer(1)]],
    const device float* grad_value          [[buffer(2)]],
    constant AssembleDecoderGradParams& p   [[buffer(3)]],
    uint gid [[thread_position_in_grid]]
) {
    if ((int)gid >= p.B_TT * p.od_plus_1) return;
    int row = (int)gid / p.od_plus_1;
    int col = (int)gid % p.od_plus_1;
    float val = (col < p.od) ? grad_logits[row * p.od + col] : grad_value[row];
    // Clamp to fp16 range to prevent inf (Metal fp16 max ~65504, unlike CUDA bf16)
    grad_out[gid] = half(clamp(val, -65000.0f, 65000.0f));
}

kernel void cast_f32_to_f16(
    device half* dst            [[buffer(0)]],
    const device float* src     [[buffer(1)]],
    constant int& count         [[buffer(2)]],
    uint gid [[thread_position_in_grid]]
) {
    if ((int)gid >= count) return;
    dst[gid] = half(src[gid]);
}

kernel void cast_f16_to_f32(
    device float* dst           [[buffer(0)]],
    const device half* src      [[buffer(1)]],
    constant int& count         [[buffer(2)]],
    uint gid [[thread_position_in_grid]]
) {
    if ((int)gid >= count) return;
    dst[gid] = float(src[gid]);
}

kernel void mingru_scan_forward_checkpointed_fp16(
    device half* out                [[buffer(0)]],
    device half* next_state         [[buffer(1)]],
    device float* a_star_buf        [[buffer(2)]],
    device float* s_buf             [[buffer(3)]],
    device float* log_values_buf    [[buffer(4)]],
    const device half* combined     [[buffer(5)]],
    const device half* state        [[buffer(6)]],
    const device half* input        [[buffer(7)]],
    constant ScanParams& p          [[buffer(8)]],
    uint idx [[thread_position_in_grid]]
) {
    mingru_scan_forward_checkpointed_body<half>(
        out, next_state, a_star_buf, s_buf, log_values_buf, combined, state, input,
        p, idx);
}

kernel void mingru_scan_forward_reset_fp16(
    device half* out                [[buffer(0)]],
    device half* next_state         [[buffer(1)]],
    device float* curr_buf          [[buffer(2)]],
    device float* prev_buf          [[buffer(3)]],
    device float* unused_buf        [[buffer(4)]],
    const device half* combined     [[buffer(5)]],
    const device half* state        [[buffer(6)]],
    const device half* input        [[buffer(7)]],
    const device float* reset       [[buffer(8)]],
    constant ScanParams& p          [[buffer(9)]],
    uint idx [[thread_position_in_grid]]
) {
    mingru_scan_forward_reset_body<half>(
        out, next_state, curr_buf, prev_buf, combined, state, input, reset, p, idx);
}

kernel void mingru_scan_backward_checkpointed_fp16(
    device half* grad_combined            [[buffer(0)]],
    device half* grad_state               [[buffer(1)]],
    device half* grad_input               [[buffer(2)]],
    const device half* grad_out           [[buffer(3)]],
    const device half* grad_next_state    [[buffer(4)]],
    const device half* combined           [[buffer(5)]],
    const device half* state              [[buffer(6)]],
    const device half* input              [[buffer(7)]],
    const device float* a_star_buf        [[buffer(8)]],
    const device float* s_buf             [[buffer(9)]],
    const device float* log_values_buf    [[buffer(10)]],
    constant ScanParams& p                [[buffer(11)]],
    uint idx [[thread_position_in_grid]]
) {
    mingru_scan_backward_checkpointed_body<half>(
        grad_combined, grad_state, grad_input, grad_out, grad_next_state,
        combined, state, input, a_star_buf, s_buf, log_values_buf, p, idx);
}

kernel void mingru_scan_backward_reset_fp16(
    device half* grad_combined            [[buffer(0)]],
    device half* grad_state               [[buffer(1)]],
    device half* grad_input               [[buffer(2)]],
    const device half* grad_out           [[buffer(3)]],
    const device half* grad_next_state    [[buffer(4)]],
    const device half* combined           [[buffer(5)]],
    const device half* state              [[buffer(6)]],
    const device half* input              [[buffer(7)]],
    const device float* curr_buf          [[buffer(8)]],
    const device float* prev_buf          [[buffer(9)]],
    const device float* unused_buf        [[buffer(10)]],
    const device float* reset             [[buffer(11)]],
    constant ScanParams& p                [[buffer(12)]],
    uint idx [[thread_position_in_grid]]
) {
    mingru_scan_backward_reset_body<half>(
        grad_combined, grad_state, grad_input, grad_out, grad_next_state,
        combined, input, curr_buf, prev_buf, reset, p, idx);
}

constant int STEEL_BM = 64;
constant int STEEL_BN = 64;
constant int STEEL_WM = 2;
constant int STEEL_WN = 2;
constant int STEEL_TM = STEEL_BM / (8 * STEEL_WM);
constant int STEEL_TN = STEEL_BN / (8 * STEEL_WN);

// Direct device loads beat threadgroup staging here because Apple Silicon L2 handles tile reuse.
template <typename T, typename Frag, bool StoreHalf>
inline void steel_gemm_body(
    device const T* A,
    device const T* B,
    device T* C,
    constant GemmParams& p,
    threadgroup float* smem,
    uint2 tgid,
    uint sgid,
    uint lane
) {
    const int bm = (int)tgid.y * STEEL_BM;
    const int bn = (int)tgid.x * STEEL_BN;
    const int wm = (int)(sgid / STEEL_WN);
    const int wn = (int)(sgid % STEEL_WN);
    const int tid = (int)(sgid * 32 + lane);
    const int sm = bm + wm * 32;
    const int sn = bn + wn * 32;

    simdgroup_float8x8 acc[STEEL_TM][STEEL_TN];
    for (int i = 0; i < STEEL_TM; i++)
        for (int j = 0; j < STEEL_TN; j++)
            acc[i][j] = simdgroup_float8x8(0);

    const int K_aligned = (p.K / 8) * 8;
    if (!p.trans_a && p.trans_b) {
        for (int k = 0; k < K_aligned; k += 8) {
            Frag a_frag[STEEL_TM];
            for (int i = 0; i < STEEL_TM; i++)
                simdgroup_load(a_frag[i], A + (long)(sm + i*8) * p.lda + k, p.lda);
            for (int j = 0; j < STEEL_TN; j++) {
                Frag b_frag;
                simdgroup_load(b_frag, B + (long)(sn + j*8) * p.ldb + k, p.ldb,
                               ulong2(0,0), true);
                for (int i = 0; i < STEEL_TM; i++)
                    simdgroup_multiply_accumulate(acc[i][j], a_frag[i], b_frag, acc[i][j]);
            }
        }
    } else if (!p.trans_a && !p.trans_b) {
        for (int k = 0; k < K_aligned; k += 8) {
            Frag a_frag[STEEL_TM];
            for (int i = 0; i < STEEL_TM; i++)
                simdgroup_load(a_frag[i], A + (long)(sm + i*8) * p.lda + k, p.lda);
            for (int j = 0; j < STEEL_TN; j++) {
                Frag b_frag;
                simdgroup_load(b_frag, B + (long)k * p.ldb + sn + j*8, p.ldb);
                for (int i = 0; i < STEEL_TM; i++)
                    simdgroup_multiply_accumulate(acc[i][j], a_frag[i], b_frag, acc[i][j]);
            }
        }
    } else if (p.trans_a && !p.trans_b) {
        for (int k = 0; k < K_aligned; k += 8) {
            Frag a_frag[STEEL_TM];
            for (int i = 0; i < STEEL_TM; i++)
                simdgroup_load(a_frag[i], A + (long)k * p.lda + sm + i*8, p.lda,
                               ulong2(0,0), true);
            for (int j = 0; j < STEEL_TN; j++) {
                Frag b_frag;
                simdgroup_load(b_frag, B + (long)k * p.ldb + sn + j*8, p.ldb);
                for (int i = 0; i < STEEL_TM; i++)
                    simdgroup_multiply_accumulate(acc[i][j], a_frag[i], b_frag, acc[i][j]);
            }
        }
    }

    constexpr int SMEM_STRIDE = STEEL_BN;

    if (K_aligned < p.K) {
        threadgroup T* sA = (threadgroup T*)smem;
        threadgroup T* sB = sA + STEEL_BM * 9;
        constexpr int sB_stride = STEEL_BN + 1;
        int k = K_aligned;
        int rem = p.K - k;

        for (int idx = tid; idx < STEEL_BM * 8; idx += 128) {
            int r = idx / 8;
            int c = idx % 8;
            int gr = bm + r;
            int gc = k + c;
            sA[r * 9 + c] = (gr < p.M && c < rem)
                ? (p.trans_a ? A[(long)gc * p.lda + gr] : A[(long)gr * p.lda + gc])
                : T(0);
        }

        for (int idx = tid; idx < 8 * STEEL_BN; idx += 128) {
            int r = idx / STEEL_BN;
            int c = idx % STEEL_BN;
            int gr = k + r;
            int gc = bn + c;
            sB[r * sB_stride + c] = (r < rem && gc < p.N)
                ? (p.trans_b ? B[(long)gc * p.ldb + gr] : B[(long)gr * p.ldb + gc])
                : T(0);
        }

        threadgroup_barrier(mem_flags::mem_threadgroup);

        Frag a_frag[STEEL_TM];
        for (int i = 0; i < STEEL_TM; i++)
            simdgroup_load(a_frag[i], sA + (wm * 32 + i * 8) * 9, 9);
        for (int j = 0; j < STEEL_TN; j++) {
            Frag b_frag;
            simdgroup_load(b_frag, sB + (wn * 32 + j * 8), sB_stride);
            for (int i = 0; i < STEEL_TM; i++)
                simdgroup_multiply_accumulate(acc[i][j], a_frag[i], b_frag, acc[i][j]);
        }
    }

    if constexpr (!StoreHalf) {
        bool fast_store = (p.alpha == 1.0f && p.beta == 0.0f
                           && bm + STEEL_BM <= p.M && bn + STEEL_BN <= p.N);
        if (fast_store) {
            for (int i = 0; i < STEEL_TM; i++)
                for (int j = 0; j < STEEL_TN; j++)
                    simdgroup_store(acc[i][j],
                        C + (long)(sm + i*8) * p.ldc + sn + j*8, p.ldc);
            return;
        }
    } else {
        threadgroup_barrier(mem_flags::mem_threadgroup);
    }

    for (int i = 0; i < STEEL_TM; i++)
        for (int j = 0; j < STEEL_TN; j++)
            simdgroup_store(acc[i][j],
                smem + (wm*32 + i*8) * SMEM_STRIDE + (wn*32 + j*8),
                SMEM_STRIDE);
    threadgroup_barrier(mem_flags::mem_threadgroup);

    for (int idx = tid; idx < STEEL_BM * STEEL_BN; idx += 128) {
        int r = idx / STEEL_BN;
        int c = idx % STEEL_BN;
        int gr = bm + r;
        int gc = bn + c;
        if (gr < p.M && gc < p.N) {
            long out_idx = (long)gr * p.ldc + gc;
            float val = smem[r * SMEM_STRIDE + c];
            if (p.beta == 0.0f)
                C[out_idx] = T(p.alpha * val);
            else
                C[out_idx] = T(p.alpha * val + p.beta * float(C[out_idx]));
        }
    }
}

kernel void steel_gemm(
    device const float* A      [[buffer(0)]],
    device const float* B      [[buffer(1)]],
    device float* C            [[buffer(2)]],
    constant GemmParams& p     [[buffer(3)]],
    uint2 tgid                 [[threadgroup_position_in_grid]],
    uint sgid                  [[simdgroup_index_in_threadgroup]],
    uint lane                  [[thread_index_in_simdgroup]]
) {
    threadgroup float smem[STEEL_BM * STEEL_BN];
    steel_gemm_body<float, simdgroup_float8x8, false>(A, B, C, p, smem, tgid, sgid, lane);
}

struct SmallGemmParams {
    uint N;
    uint K;
};

kernel void small_gemm_nt_f32(
    const device float* A          [[buffer(0)]],
    const device float* B          [[buffer(1)]],
    device float* C                [[buffer(2)]],
    constant SmallGemmParams& p    [[buffer(3)]],
    uint tgid [[threadgroup_position_in_grid]],
    uint tid  [[thread_index_in_threadgroup]])
{
    uint m = tgid;
    if (tid >= p.N) return;

    const device float* a_row = A + m * p.K;
    const device float* b_row = B + tid * p.K;

    float sum = 0.0f;
    uint K4 = p.K & ~3u;
    for (uint k = 0; k < K4; k += 4) {
        float4 a4 = *reinterpret_cast<const device float4*>(a_row + k);
        float4 b4 = *reinterpret_cast<const device float4*>(b_row + k);
        sum += dot(a4, b4);
    }
    for (uint k = K4; k < p.K; k++)
        sum += a_row[k] * b_row[k];

    C[m * p.N + tid] = sum;
}

kernel void steel_gemm_f16(
    device const half* A       [[buffer(0)]],
    device const half* B       [[buffer(1)]],
    device half* C             [[buffer(2)]],
    constant GemmParams& p     [[buffer(3)]],
    uint2 tgid                 [[threadgroup_position_in_grid]],
    uint sgid                  [[simdgroup_index_in_threadgroup]],
    uint lane                  [[thread_index_in_simdgroup]]
) {
    threadgroup float smem[STEEL_BM * STEEL_BN];
    steel_gemm_body<half, simdgroup_half8x8, true>(A, B, C, p, smem, tgid, sgid, lane);
}

)METAL";
}

#endif // PUFFERLIB_METAL_SHADER_SRC_H
