// Shared type definitions for PufferLib Metal backend.
// Uses upstream 4.0 typed tensor system (FloatTensor, PrecisionTensor, etc.)
// instead of runtime-typed PufTensor.
//
// Only included by Metal path (metal_platform.h). CUDA path has its own
// type definitions in models.cu / kernels.cu.

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

#include "tensor.h"

using std::vector;

// ============================================================================
// Platform abstraction
// ============================================================================

#define PUF_HD
typedef void *cudaStream_t;
#define CUDA_STREAM_T_DEFINED

// ============================================================================
// Compile-time precision: Metal always uses fp32 (no bf16 compute).
// ============================================================================

constexpr bool USE_BF16 = false;
constexpr int PRECISION_SIZE = 4; // bytes per element (Metal: always fp32)

// ============================================================================
// PufTensor — legacy runtime-typed tensor view (kept for transition code).
// New code should use FloatTensor, PrecisionTensor, IntTensor, LongTensor.
// ============================================================================

// Minimal tensor: raw pointer + shape + runtime dtype_size.
// Being phased out in favor of statically typed tensors from tensor.h.
struct PufTensor {
  char *bytes = nullptr;
  int64_t shape[PUF_MAX_DIMS] = {};
  int dtype_size =
      0; // bytes per element (2 for bf16/f16, 4 for f32, 8 for f64)

  PUF_HD int ndim() const {
    return puf_ndim(shape);
  }

  PUF_HD int64_t numel() const {
    return puf_numel(shape);
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
    return puf_batch_size(shape);
  }

  const char *dtype_name() const {
    switch (dtype_size) {
    case 1:
      return "i8";
    case 2:
      return "f16";
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
             (long long)puf_numel(shape));
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
// Allocator — single contiguous buffer with typed tensor views.
// On Metal: page-aligned calloc (unified memory).
// Registers heterogeneous tensor types via alloc_register overloads.
// ============================================================================

struct AllocEntry {
  void **data_ptr;
  int64_t *shape;
  int elem_size;
};

struct Allocator {
  std::vector<AllocEntry> regs;
  // Legacy PufTensor registrations (being phased out)
  std::vector<PufTensor *> legacy_regs;
  void *mem = nullptr;
  int64_t total_elems = 0;

  void create() {
    int64_t total_bytes = 0;
    total_elems = 0;

    // Typed tensor entries
    for (auto &e : regs) {
      total_bytes = (total_bytes + 15) & ~15;
      int64_t n = puf_numel(e.shape);
      total_bytes += n * e.elem_size;
      total_elems += n;
    }
    // Legacy PufTensor entries
    for (auto *t : legacy_regs) {
      total_bytes = (total_bytes + 15) & ~15;
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
      // Assign typed tensor data pointers
      for (auto &e : regs) {
        offset = (offset + 15) & ~15;
        *e.data_ptr = (char *)mem + offset;
        offset += puf_numel(e.shape) * e.elem_size;
      }
      // Assign legacy PufTensor byte pointers
      for (auto *t : legacy_regs) {
        offset = (offset + 15) & ~15;
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

// Typed tensor registration (upstream 4.0 pattern)
inline void alloc_register(Allocator *a, FloatTensor *t) {
  a->regs.push_back({(void **)&t->data, t->shape, (int)sizeof(float)});
}
inline void alloc_register(Allocator *a, PrecisionTensor *t) {
  a->regs.push_back({(void **)&t->data, t->shape, PRECISION_SIZE});
}
inline void alloc_register(Allocator *a, IntTensor *t) {
  a->regs.push_back({(void **)&t->data, t->shape, (int)sizeof(int)});
}
inline void alloc_register(Allocator *a, LongTensor *t) {
  a->regs.push_back({(void **)&t->data, t->shape, (int)sizeof(long)});
}

// Legacy PufTensor registration (for code not yet migrated)
inline void alloc_register_legacy(Allocator *a, PufTensor *t) {
  a->legacy_regs.push_back(t);
}

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
// ============================================================================

struct PrioBuffers {
  FloatTensor prio_probs, cdf, mb_prio;
  LongTensor idx;
};

inline void register_prio_buffers(PrioBuffers &bufs, Allocator &alloc, int S,
                                  int minibatch_segments) {
  bufs = (PrioBuffers){
      .prio_probs = {.shape = {S}},
      .cdf = {.shape = {S}},
      .mb_prio = {.shape = {minibatch_segments, 1}},
      .idx = {.shape = {minibatch_segments}},
  };
  alloc_register(&alloc, &bufs.prio_probs);
  alloc_register(&alloc, &bufs.cdf);
  alloc_register(&alloc, &bufs.mb_prio);
  alloc_register(&alloc, &bufs.idx);
}

struct PPOBuffersPuf {
  FloatTensor loss_output;
  FloatTensor grad_logits, grad_values, grad_logstd, adv_scratch;
};

inline void register_ppo_buffers(PPOBuffersPuf &bufs, Allocator &alloc, int N,
                                 int T, int A_total, bool is_continuous) {
  bufs = (PPOBuffersPuf){
      .loss_output = {.shape = {1}},
      .grad_logits = {.shape = {N, T, A_total}},
      .grad_values = {.shape = {N, T, 1}},
      .grad_logstd = {.shape = {N, T, A_total}},
      .adv_scratch = {.shape = {2}},
  };
  alloc_register(&alloc, &bufs.loss_output);
  alloc_register(&alloc, &bufs.grad_logits);
  alloc_register(&alloc, &bufs.grad_values);
  if (is_continuous)
    alloc_register(&alloc, &bufs.grad_logstd);
  alloc_register(&alloc, &bufs.adv_scratch);
}

// ============================================================================
// Rollout and training graph buffer structs
// ============================================================================

struct RolloutBuf {
  FloatTensor observations; // (horizon, segments, input_size) fp32
  PufTensor actions;        // (horizon, segments, num_atns) f64 — legacy, needs double
  FloatTensor values;       // (horizon, segments) fp32
  FloatTensor logprobs;     // (horizon, segments) fp32
  FloatTensor rewards;      // (horizon, segments) fp32
  FloatTensor terminals;    // (horizon, segments) fp32
  FloatTensor ratio;        // (horizon, segments) fp32
  FloatTensor importance;   // (horizon, segments) fp32
};

inline void register_rollout_buffers(RolloutBuf &bufs, Allocator &alloc, int H,
                                     int S, int input_size, int num_atns) {
  bufs.observations = {.shape = {H, S, input_size}};
  bufs.actions = {.shape = {H, S, num_atns}, .dtype_size = (int)sizeof(double)};
  bufs.values = {.shape = {H, S}};
  bufs.logprobs = {.shape = {H, S}};
  bufs.rewards = {.shape = {H, S}};
  bufs.terminals = {.shape = {H, S}};
  bufs.ratio = {.shape = {H, S}};
  bufs.importance = {.shape = {H, S}};
  alloc_register(&alloc, &bufs.observations);
  alloc_register_legacy(&alloc, &bufs.actions);
  alloc_register(&alloc, &bufs.values);
  alloc_register(&alloc, &bufs.logprobs);
  alloc_register(&alloc, &bufs.rewards);
  alloc_register(&alloc, &bufs.terminals);
  alloc_register(&alloc, &bufs.ratio);
  alloc_register(&alloc, &bufs.importance);
}

struct TrainGraph {
  FloatTensor mb_obs;        // (S, H, input_size) fp32
  FloatTensor mb_state;      // (L, S, 1, hidden) fp32
  PufTensor mb_actions;      // (S, H, num_atns) f64 — legacy, needs double
  FloatTensor mb_logprobs;   // (S, H) fp32
  FloatTensor mb_advantages; // (S, H) f32
  FloatTensor mb_prio;       // (S, 1) fp32
  FloatTensor mb_values;     // (S, H) fp32
  FloatTensor mb_returns;    // (S, H) fp32
  FloatTensor mb_ratio;      // (S, H) fp32
  FloatTensor mb_newvalue;   // (S, H, 1) fp32
};

inline void register_train_buffers(TrainGraph &bufs, Allocator &alloc, int S,
                                   int H, int input_size, int hidden_size,
                                   int num_atns, int num_layers) {
  bufs.mb_obs = {.shape = {S, H, input_size}};
  bufs.mb_state = {.shape = {num_layers, S, 1, hidden_size}};
  bufs.mb_actions = {.shape = {S, H, num_atns}, .dtype_size = (int)sizeof(double)};
  bufs.mb_logprobs = {.shape = {S, H}};
  bufs.mb_advantages = {.shape = {S, H}};
  bufs.mb_prio = {.shape = {S, 1}};
  bufs.mb_values = {.shape = {S, H}};
  bufs.mb_returns = {.shape = {S, H}};
  bufs.mb_ratio = {.shape = {S, H}};
  bufs.mb_newvalue = {.shape = {S, H, 1}};
  alloc_register(&alloc, &bufs.mb_obs);
  alloc_register(&alloc, &bufs.mb_state);
  alloc_register_legacy(&alloc, &bufs.mb_actions);
  alloc_register(&alloc, &bufs.mb_logprobs);
  alloc_register(&alloc, &bufs.mb_advantages);
  alloc_register(&alloc, &bufs.mb_prio);
  alloc_register(&alloc, &bufs.mb_values);
  alloc_register(&alloc, &bufs.mb_returns);
  alloc_register(&alloc, &bufs.mb_ratio);
  alloc_register(&alloc, &bufs.mb_newvalue);
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

// Encoder: single linear projection (obs -> hidden), matching upstream CUDA
struct EncoderWeights {
  PufTensor weight; // (out_dim, in_dim)
  int in_dim, out_dim;
};
struct EncoderActivations {
  PufTensor out;         // (B, out_dim)
  PufTensor saved_input; // (B, in_dim) -- training only
  PufTensor wgrad;       // (out_dim, in_dim) -- training only
};

// Decoder: single linear projection (hidden -> logits+value).
// Fused weight (od+1, H) registered with Muon -- value row participates in NS.
struct DecoderWeights {
  PufTensor weight;       // (output_dim+1, hidden_dim)
  PufTensor logstd;       // continuous only: (1, output_dim)
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
  double momentum;
  float lr_val_init;
  int ns_iters;  // Newton-Schulz iterations (default 5, sweepable)
  float *lr_ptr;
  float *lr_derived_ptr;
  FloatTensor lr_puf, lr_derived_puf;
  FloatTensor ns_norm_puf;
  FloatTensor wb_puf, mb_puf, gc_puf, up_puf;
  NSScratch ns;
  Allocator *param_alloc; // fp32 params allocator -- shapes used by muon_step
};

// ============================================================================
// Utility functions
// ============================================================================

// Slice a PufTensor: select dim0 index t, then narrow dim0 from start for count.
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

// Slice a FloatTensor: select dim0 index t, then narrow dim0 from start for count.
inline FloatTensor puf_slice(FloatTensor &p, int t, int start, int count) {
  if (puf_ndim(p.shape) == 3) {
    int64_t S = p.shape[1], F = p.shape[2];
    return {.data = p.data + (t * S + start) * F, .shape = {count, F}};
  } else {
    int64_t S = p.shape[1];
    return {.data = p.data + (t * S + start), .shape = {count}};
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
  PufTensor obs;       // (total_agents, obs_size) -- runtime dtype
  int obs_raw_dtype;   // raw env dtype (FLOAT, INT, UNSIGNED_CHAR, etc.)
  PufTensor actions;   // (total_agents, num_atns) f64
  FloatTensor rewards;   // (total_agents,) f32
  FloatTensor terminals; // (total_agents,) f32
};

#endif // PUFFERLIB_PUF_TYPES_H
