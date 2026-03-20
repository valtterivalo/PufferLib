// Shared type definitions for PufferLib static-native.
// Included by both CUDA (models.cu) and Metal (metal_models.mm) paths.
// No platform-specific kernel code lives here — only data structures,
// buffer registration, and vtable interfaces.

#ifndef PUFFERLIB_PUF_TYPES_H
#define PUFFERLIB_PUF_TYPES_H

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

using std::vector;

// ============================================================================
// Platform abstraction
// ============================================================================

#ifdef __CUDACC__
#define PUF_HD __host__ __device__
#include <cuda_runtime.h>
#else
#define PUF_HD
typedef void *cudaStream_t;
#define CUDA_STREAM_T_DEFINED
#endif

// ============================================================================
// Compile-time precision: default bf16, pass -DPRECISION_FLOAT for float32.
// Metal always uses PRECISION_FLOAT (MPS has no bf16 compute).
// ============================================================================

#ifdef PRECISION_FLOAT
constexpr bool USE_BF16 = false;
constexpr int PRECISION_SIZE = 4; // bytes per element
#else
constexpr bool USE_BF16 = true;
constexpr int PRECISION_SIZE = 2; // bytes per element
#endif

// ============================================================================
// PufTensor — minimal tensor view (no torch dependency)
// ============================================================================

#define PUF_MAX_DIMS 8

// Minimal tensor: raw pointer + shape, no torch dependency in the struct
// itself. Memory is owned by an Allocator buffer — PufTensor is just a view.
struct PufTensor {
  char *bytes = nullptr;
  int64_t shape[PUF_MAX_DIMS] = {};
  int dtype_size =
      0; // bytes per element (2 for bf16/f16, 4 for f32, 8 for f64)

  PUF_HD int ndim() const {
    int n = 0;
    while (n < PUF_MAX_DIMS && shape[n] != 0) {
      n++;
    }
    return n;
  }

  PUF_HD int64_t numel() const {
    int64_t n = 1;
    for (int i = 0; i < PUF_MAX_DIMS && shape[i] != 0; i++) {
      n *= shape[i];
    }
    return n;
  }

  // Merge shape[dim] into shape[dim+1]: {B, TT, H} -> {B*TT, H}
  PufTensor squeeze(int dim) {
    int n = ndim();
    shape[dim + 1] *= shape[dim];
    for (int i = dim; i < n - 1; i++)
      shape[i] = shape[i + 1];
    shape[n - 1] = 0;
    return *this;
  }

  // Split shape[dim] into two: {B*TT, H} with unsqueeze(0, B, TT) -> {B, TT, H}
  PufTensor unsqueeze(int dim, int64_t d0, int64_t d1) {
    assert(d0 * d1 == shape[dim] && "unsqueeze: d0 * d1 must equal shape[dim]");
    int n = ndim();
    for (int i = n; i > dim; i--)
      shape[i] = shape[i - 1];
    shape[dim] = d0;
    shape[dim + 1] = d1;
    return *this;
  }

  // Product of all dims except the last two (1 if ndim <= 2)
  int64_t batch_size() const {
    int n = ndim();
    int64_t b = 1;
    for (int i = 0; i < n - 2; i++)
      b *= shape[i];
    return b;
  }

  const char *dtype_name() const {
    switch (dtype_size) {
    case 1:
      return "i8";
    case 2:
      return "bf16";
    case 4:
      return "f32";
    case 8:
      return "f64";
    default:
      return "?";
    }
  }

  const char *repr() const {
    static char buf[256];
    if (!bytes) {
      snprintf(buf, sizeof(buf), "PufTensor(empty)");
      return buf;
    }
    int pos = snprintf(buf, sizeof(buf), "PufTensor(%s, [", dtype_name());
    for (int i = 0; i < ndim() && pos < (int)sizeof(buf) - 32; i++) {
      pos += snprintf(buf + pos, sizeof(buf) - pos, "%s%lld", i ? ", " : "",
                      (long long)shape[i]);
    }
    snprintf(buf + pos, sizeof(buf) - pos, "], %lld elems)",
             (long long)numel());
    return buf;
  }
};

// ============================================================================
// Loss component indices
// ============================================================================

enum LossIdx {
  LOSS_PG = 0,
  LOSS_VF = 1,
  LOSS_ENT = 2,
  LOSS_TOTAL = 3,
  LOSS_OLD_APPROX_KL = 4,
  LOSS_APPROX_KL = 5,
  LOSS_CLIPFRAC = 6,
  LOSS_N = 7,
  NUM_LOSSES = 8,
};

// ============================================================================
// PrefixScan buffers (MinGRU training)
// ============================================================================

struct PrefixScan {
  void *combined_ptr = nullptr;
  void *state_ptr = nullptr;
  void *input_ptr = nullptr;
  int B = 0, T = 0, H = 0;
  PufTensor a_star, s_vals, log_values_buf;
  PufTensor out, next_state;
  PufTensor grad_combined, grad_state, grad_input;
};

// ============================================================================
// Allocator — single contiguous buffer with PufTensor views.
// On CUDA: cudaMalloc. On CPU/Metal: calloc (unified memory).
// ============================================================================

struct Allocator {
  std::vector<PufTensor *> regs;
  void *mem = nullptr;
  int64_t total_elems = 0;

  void reg(PufTensor *ptr) { regs.push_back(ptr); }

  // Register all PufTensors in a struct laid out as consecutive PufTensors.
  // Only use on plain buffer structs (RolloutBuf, TrainGraph, etc.), not on
  // network structs with non-PufTensor members.
  template <typename T> void dangerously_register(T *s) {
    int n = sizeof(T) / sizeof(PufTensor);
    PufTensor *first = (PufTensor *)s;
    for (int i = 0; i < n; i++)
      regs.push_back(&first[i]);
  }

  void create() {
    int64_t total_bytes = 0;
    total_elems = 0;
    for (auto *t : regs) {
      total_bytes = (total_bytes + 15) & ~15; // align each tensor to 16 bytes
      total_bytes += t->numel() * t->dtype_size;
      total_elems += t->numel();
    }
    if (total_bytes > 0) {
#ifdef __CUDACC__
      cudaMalloc(&mem, total_bytes);
      cudaMemset(mem, 0, total_bytes);
#elif defined(WITH_METAL)
      // Page-aligned for Metal's newBufferWithBytesNoCopy (ARM64 page = 16KB)
      posix_memalign(&mem, 16384, total_bytes);
      memset(mem, 0, total_bytes);
#else
      mem = calloc(1, total_bytes);
#endif
      int64_t offset = 0;
      for (auto *t : regs) {
        offset = (offset + 15) & ~15; // align each tensor to 16 bytes
        t->bytes = (char *)mem + offset;
        offset += t->numel() * t->dtype_size;
      }
    }
  }

  void destroy() {
#ifdef __CUDACC__
    if (mem) {
      cudaFree(mem);
      mem = nullptr;
    }
#else
    if (mem) {
      free(mem);
      mem = nullptr;
    }
#endif
  }
};

// Groups 3 allocators for policy: params, grads, activations
struct AllocSet {
  Allocator params, grads, acts;
  int esz = 0; // element size for params/grads
  void create() {
    params.create();
    grads.create();
    acts.create();
  }
  void destroy() {
    params.destroy();
    grads.destroy();
    acts.destroy();
  }
};

// ============================================================================
// Pre-allocated buffer structs and registration functions.
// Registration is pure CPU (shape setup + alloc.reg), no platform deps.
// ============================================================================

struct PrioBuffers {
  PufTensor prio_probs, cdf, idx, mb_prio;
};

inline void register_prio_buffers(PrioBuffers &bufs, Allocator &alloc, int S,
                                  int minibatch_segments) {
  bufs = (PrioBuffers){
      .prio_probs = {.shape = {S}, .dtype_size = sizeof(float)},
      .cdf = {.shape = {S}, .dtype_size = sizeof(float)},
      .idx = {.shape = {minibatch_segments}, .dtype_size = sizeof(int64_t)},
      .mb_prio = {.shape = {minibatch_segments, 1},
                  .dtype_size = sizeof(float)},
  };
  alloc.dangerously_register(&bufs);
}

struct PPOBuffersPuf {
  PufTensor loss_output, saved_for_bwd, grad_loss;
  PufTensor grad_logits, grad_values, grad_logstd, adv_scratch;
};

inline void register_ppo_buffers(PPOBuffersPuf &bufs, Allocator &alloc, int N,
                                 int T, int A_total, bool is_continuous) {
  int64_t total = (int64_t)N * T;
  int f = sizeof(float);
  bufs = (PPOBuffersPuf){
      .loss_output = {.shape = {1}, .dtype_size = f},
      .saved_for_bwd = {.shape = {total, 5}, .dtype_size = (int)sizeof(double)},
      .grad_loss = {.shape = {1}, .dtype_size = f},
      .grad_logits = {.shape = {N, T, A_total}, .dtype_size = f},
      .grad_values = {.shape = {N, T, 1}, .dtype_size = f},
      .grad_logstd = {.shape = {N, T, A_total}, .dtype_size = f},
      .adv_scratch = {.shape = {2}, .dtype_size = f},
  };
  alloc.reg(&bufs.loss_output);
  alloc.reg(&bufs.saved_for_bwd);
  alloc.reg(&bufs.grad_loss);
  alloc.reg(&bufs.grad_logits);
  alloc.reg(&bufs.grad_values);
  if (is_continuous)
    alloc.reg(&bufs.grad_logstd);
  alloc.reg(&bufs.adv_scratch);
}

// ============================================================================
// Rollout and training graph buffer structs
// ============================================================================

struct RolloutBuf {
  PufTensor observations; // (horizon, segments, input_size) PRECISION
  PufTensor actions;      // (horizon, segments, num_atns) f64
  PufTensor values;       // (horizon, segments) PRECISION
  PufTensor logprobs;     // (horizon, segments) PRECISION
  PufTensor rewards;      // (horizon, segments) PRECISION
  PufTensor terminals;    // (horizon, segments) PRECISION
  PufTensor ratio;        // (horizon, segments) PRECISION
  PufTensor importance;   // (horizon, segments) PRECISION
};

inline void register_rollout_buffers(RolloutBuf &bufs, Allocator &alloc, int H,
                                     int S, int input_size, int num_atns) {
  int p = PRECISION_SIZE;
  bufs = (RolloutBuf){
      .observations = {.shape = {H, S, input_size}, .dtype_size = p},
      .actions = {.shape = {H, S, num_atns}, .dtype_size = (int)sizeof(double)},
      .values = {.shape = {H, S}, .dtype_size = p},
      .logprobs = {.shape = {H, S}, .dtype_size = p},
      .rewards = {.shape = {H, S}, .dtype_size = p},
      .terminals = {.shape = {H, S}, .dtype_size = p},
      .ratio = {.shape = {H, S}, .dtype_size = p},
      .importance = {.shape = {H, S}, .dtype_size = p},
  };
  alloc.dangerously_register(&bufs);
}

struct TrainGraph {
  PufTensor mb_obs;        // (S, H, input_size) PRECISION
  PufTensor mb_state;      // (L, S, 1, hidden) PRECISION
  PufTensor mb_actions;    // (S, H, num_atns) f64
  PufTensor mb_logprobs;   // (S, H) PRECISION
  PufTensor mb_advantages; // (S, H) f32
  PufTensor mb_prio;       // (S, 1) PRECISION
  PufTensor mb_values;     // (S, H) PRECISION
  PufTensor mb_returns;    // (S, H) PRECISION
  PufTensor mb_ratio;      // (S, H) PRECISION
  PufTensor mb_newvalue;   // (S, H, 1) PRECISION
};

inline void register_train_buffers(TrainGraph &bufs, Allocator &alloc, int S,
                                   int H, int input_size, int hidden_size,
                                   int num_atns, int num_layers) {
  int p = PRECISION_SIZE;
  bufs = (TrainGraph){
      .mb_obs = {.shape = {S, H, input_size}, .dtype_size = p},
      .mb_state = {.shape = {num_layers, S, 1, hidden_size}, .dtype_size = p},
      .mb_actions = {.shape = {S, H, num_atns},
                     .dtype_size = (int)sizeof(double)},
      .mb_logprobs = {.shape = {S, H}, .dtype_size = p},
      .mb_advantages = {.shape = {S, H}, .dtype_size = (int)sizeof(float)},
      .mb_prio = {.shape = {S, 1}, .dtype_size = p},
      .mb_values = {.shape = {S, H}, .dtype_size = p},
      .mb_returns = {.shape = {S, H}, .dtype_size = p},
      .mb_ratio = {.shape = {S, H}, .dtype_size = p},
      .mb_newvalue = {.shape = {S, H, 1}, .dtype_size = p},
  };
  alloc.dangerously_register(&bufs);
}

// ============================================================================
// Policy vtables — function pointer interfaces.
// cudaStream_t is void* on non-CUDA platforms; Metal casts to its own type.
// ============================================================================

typedef void (*init_weights_fn)(void *weights, uint64_t *seed,
                                cudaStream_t stream);
typedef void (*reg_params_fn)(void *weights, Allocator *alloc, int esz);
typedef void (*reg_train_fn)(void *weights, void *buf, Allocator *acts,
                             Allocator *grads, int B_TT, int precision);
typedef void (*reg_rollout_fn)(void *weights, void *buf, Allocator *alloc,
                               int B);
typedef PufTensor (*forward_fn)(void *weights, void *activations,
                                PufTensor input, cudaStream_t stream);
typedef void (*encoder_backward_fn)(void *weights, void *activations,
                                    PufTensor grad, cudaStream_t stream);
typedef PufTensor (*decoder_backward_fn)(void *weights, void *activations,
                                         PufTensor grad_logits,
                                         PufTensor grad_logstd,
                                         PufTensor grad_value,
                                         cudaStream_t stream);
typedef PufTensor (*network_forward_fn)(void *weights, PufTensor x,
                                        PufTensor state, void *activations,
                                        cudaStream_t stream);
typedef PufTensor (*network_forward_train_fn)(void *weights, PufTensor x,
                                              PufTensor state,
                                              void *activations,
                                              cudaStream_t stream);
typedef PufTensor (*network_backward_fn)(void *weights, PufTensor grad,
                                         void *activations,
                                         cudaStream_t stream);

struct Encoder {
  forward_fn forward;
  encoder_backward_fn backward;
  init_weights_fn init_weights;
  reg_params_fn reg_params;
  reg_train_fn reg_train;
  reg_rollout_fn reg_rollout;
};

struct Decoder {
  forward_fn forward;
  decoder_backward_fn backward;
  init_weights_fn init_weights;
  reg_params_fn reg_params;
  reg_train_fn reg_train;
  reg_rollout_fn reg_rollout;
};

struct Network {
  network_forward_fn forward;
  network_forward_train_fn forward_train;
  network_backward_fn backward;
  init_weights_fn init_weights;
  reg_params_fn reg_params;
  reg_train_fn reg_train;
  reg_rollout_fn reg_rollout;
};

// ============================================================================
// Weight and activation structs for encoder, decoder, MinGRU
// ============================================================================

// Encoder: single linear projection (obs → hidden), matching upstream CUDA
struct EncoderWeights {
  PufTensor weight; // (out_dim, in_dim)
  int in_dim, out_dim;
};
struct EncoderActivations {
  PufTensor out;         // (B, out_dim)
  PufTensor saved_input; // (B, in_dim) — training only
  PufTensor wgrad;       // (out_dim, in_dim) — training only
};

// Decoder: single linear projection (hidden → logits+value), matching upstream CUDA
struct DecoderWeights {
  PufTensor weight; // (output_dim+1, hidden_dim)
  PufTensor logstd; // continuous only: (1, output_dim)
  int hidden_dim, output_dim;
  bool continuous;
};
struct DecoderActivations {
  PufTensor out;            // (B, output_dim+1)
  PufTensor grad_out;       // (B_TT, output_dim+1)
  PufTensor saved_input;    // (B_TT, hidden_dim)
  PufTensor grad_input;     // (B_TT, hidden_dim)
  PufTensor wgrad;          // (output_dim+1, hidden_dim)
  PufTensor logstd_scratch; // continuous: (1, output_dim)
};

struct MinGRUActivations {
  int num_layers;
  // Rollout
  vector<PufTensor> combined; // per-layer (B_inf, 3*H)
  PufTensor out;              // (B_inf, H)
  PufTensor next_state;       // (B_inf, H)
  // Training
  vector<PufTensor> saved_inputs;  // per-layer (B, TT, H)
  vector<PrefixScan> scan_bufs;    // per-layer scan state
  vector<PufTensor> combined_bufs; // per-layer (B_TT, 3*H)
  vector<PufTensor> wgrad_scratch; // per-layer (3*H, H) weight grad output
  PufTensor grad_input_buf;        // (B_TT, H)
  PufTensor grad_next_state;       // (B, 1, H)
};

struct MinGRUWeights {
  int hidden, num_layers, horizon;
  vector<PufTensor> weights;
  PufTensor fused_enc_layer0;   // (3*H, obs_dim) — fused encoder+layer0 (NT layout for puf_mm)
  int fused_obs_dim;            // obs_dim for the fused weight
};

// ============================================================================
// Policy composite types
// ============================================================================

struct Policy {
  Encoder encoder;
  Decoder decoder;
  Network network;
  int input_dim, hidden_dim, output_dim;
  int num_atns;
};

struct PolicyActivations {
  void *encoder;
  void *decoder;
  void *network;
};
struct PolicyWeights {
  void *encoder;
  void *decoder;
  void *network;
};

// Policy dispatch through vtable pointers (no platform deps)
inline PufTensor policy_forward(Policy *p, PolicyWeights &w,
                                PolicyActivations &activations, PufTensor obs,
                                PufTensor state, cudaStream_t stream) {
  PufTensor enc_out =
      p->encoder.forward(w.encoder, activations.encoder, obs, stream);
  PufTensor h = p->network.forward(w.network, enc_out, state,
                                   activations.network, stream);
  return p->decoder.forward(w.decoder, activations.decoder, h, stream);
}

inline PufTensor policy_forward_train(Policy *p, PolicyWeights &w,
                                      PolicyActivations &activations,
                                      PufTensor x, PufTensor state,
                                      cudaStream_t stream) {
  int B = x.shape[0], TT = x.shape[1];
  PufTensor h =
      p->encoder.forward(w.encoder, activations.encoder, x.squeeze(0), stream);
  h = p->network.forward_train(w.network, h.unsqueeze(0, B, TT), state,
                               activations.network, stream);
  PufTensor dec_out =
      p->decoder.forward(w.decoder, activations.decoder, h.squeeze(0), stream);
  return dec_out.unsqueeze(0, B, TT);
}

inline void policy_backward(Policy *p, PolicyWeights &w,
                            PolicyActivations &activations,
                            PufTensor grad_logits, PufTensor grad_logstd,
                            PufTensor grad_value, cudaStream_t stream) {
  int B = grad_logits.shape[0], TT = grad_logits.shape[1];
  PufTensor grad_h = p->decoder.backward(w.decoder, activations.decoder,
                                         grad_logits.squeeze(0), grad_logstd,
                                         grad_value.squeeze(0), stream);
  grad_h = p->network.backward(w.network, grad_h.unsqueeze(0, B, TT),
                               activations.network, stream);
  p->encoder.backward(w.encoder, activations.encoder, grad_h, stream);
}

// ============================================================================
// Muon optimizer types
// ============================================================================

inline float cosine_annealing(float lr_base, float lr_min, int t, int T) {
  if (T == 0)
    return lr_base;
  float ratio = (float)t / (float)T;
  ratio = std::max(0.0f, std::min(1.0f, ratio));
  return lr_min + 0.5f * (lr_base - lr_min) * (1.0f + std::cos(M_PI * ratio));
}

static constexpr double ns_coeffs[5][3] = {
    {4.0848, -6.8946, 2.9270}, {3.9505, -6.3029, 2.6377},
    {3.7418, -5.5913, 2.3037}, {2.8769, -3.1427, 1.2046},
    {2.8366, -3.0525, 1.2012},
};

struct NSScratch {
  PufTensor x, A, gram, tmp;
  PufTensor result_f32;
  float *norm_ptr;
  int64_t max_M, max_N;
};

inline PufTensor ns_slice(PufTensor &buf, int64_t rows, int64_t cols) {
  return {
      .bytes = buf.bytes, .shape = {rows, cols}, .dtype_size = buf.dtype_size};
}

struct Muon {
  double momentum, weight_decay;
  float lr_val_init;
  int ns_iters;  // Newton-Schulz iterations (default 5, sweepable)
  float *lr_ptr;
  float *lr_derived_ptr;
  PufTensor lr_puf, lr_derived_puf, ns_norm_puf;
  PufTensor wb_puf, mb_puf, gc_puf, up_puf;
  NSScratch ns;
  Allocator *param_alloc; // fp32 params allocator -- shapes used by muon_step
};

// ============================================================================
// Utility functions (pure PufTensor operations, no platform deps)
// ============================================================================

// Slice a PufTensor: select dim0 index t, then narrow dim0 from start for
// count.
inline PufTensor puf_slice(PufTensor &p, int t, int start, int count) {
  if (p.ndim() == 3) {
    int64_t S = p.shape[1], F = p.shape[2];
    return {.bytes = p.bytes + (t * S + start) * F * p.dtype_size,
            .shape = {count, F},
            .dtype_size = p.dtype_size};
  } else {
    int64_t S = p.shape[1];
    return {.bytes = p.bytes + (t * S + start) * p.dtype_size,
            .shape = {count},
            .dtype_size = p.dtype_size};
  }
}

// Extract per-layer state view from (num_layers, B, H) state tensor
inline PufTensor mingru_state_layer(MinGRUWeights *m, PufTensor &state, int i) {
  int64_t B = state.shape[1], H = state.shape[2];
  return {.bytes = state.bytes + i * B * H * state.dtype_size,
          .shape = {B, H},
          .dtype_size = state.dtype_size};
}

// Environment observation/action buffer
struct EnvBuf {
  PufTensor obs;       // (total_agents, obs_size)
  int obs_raw_dtype;   // raw env dtype (FLOAT, INT, UNSIGNED_CHAR, etc.)
  PufTensor actions;   // (total_agents, num_atns) f64
  PufTensor rewards;   // (total_agents,) f32
  PufTensor terminals; // (total_agents,) f32
};

#endif // PUFFERLIB_PUF_TYPES_H
