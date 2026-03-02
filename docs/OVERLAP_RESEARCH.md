# Metal GPU overlap on Apple Silicon — research + implementation plan

status: ACTIVE — working reference for overlap optimization

## executive summary

two MTLCommandQueues don't give concurrent compute on Apple Silicon. the GPU serializes/time-slices across queues. our -7% SPS result is almost certainly queueing/scheduling delay, not bandwidth contention: rollout's tiny command buffers get stuck behind a 44ms training monolith.

the fix is cooperative scheduling — training must yield often so rollout can cut in line.

## hardware reality (M4 Pro)

- single GPU, 16 cores, ~273 GB/s unified memory, 24 GB
- Metal guarantees ordering within a queue; across queues the driver chooses (Apple forums thread/63736)
- no public guarantee of parallel compute from two queues
- community attempts (M1 Ultra, M3) show serialization even with untracked resources (thread/721118)
- Metal 4 (MTL4CommandQueue) removes implicit hazard tracking but doesn't create parallel compute

## what failed and why

overlap attempt: flush training async on train_queue, run rollout on default queue.

result: -7% SPS. PROF_ROLLOUT_FWD degraded 25ms → 52ms (+85%).

diagnosis from research:
1. **queueing delay** — one 44ms training command buffer monopolizes the GPU. rollout's tiny command buffers (0.28ms each) queue behind it. the 85% degradation is mostly wait time, not slower kernels.
2. **possible hazard tracking** — if any MTLBuffer is shared between queues (even for reads around writes), the driver forces whole-command-buffer serialization. our allocator pools are separate, but need to verify no accidental sharing.

## iteration flow (current sequential, ~96ms)

```
Python loop:
  _C.rollouts(pufferl)  →  static_vec_omp_step()
  _C.train(pufferl)     →  train_impl()
  _C.log_losses()       →  reads loss values

Rollout inner loop (per buffer thread, 32 steps):
  net_callback_wrapper:
    CPU obs copy (~0.5ms)
    GPU fwd+sample (dispatched on compute encoder, ~0.28ms GPU)
    ensure_gpu_synced (commit+wait)
    CPU expand f32→f64 + action copy
  cudaStreamSynchronize (no-op, sync already happened)
  c_step: OMP parallel env steps (~1.4ms CPU, GPU IDLE)
  cudaMemcpyAsync obs/rewards/terminals (no-op on Metal)

Training (train_impl, ~44ms):
  puf_set_gpu_training(true)
  advantages + prio precompute (1 sync point)
  8 minibatches, each:
    prio sample → select_copy → cast f32→f16
    fp16 forward → cast f16→f32 → PPO loss
    backward → cast f16→f32 grads → clip → muon step → cast f32→f16 weights
  puf_set_gpu_training(false)
  ensure_gpu_synced (or flush if overlap)
```

## key timing

| phase | time | notes |
|-------|------|-------|
| rollout total | ~52ms | 32 steps, dominated by CPU env |
| per-step GPU | ~0.28ms | fwd + sample on compute encoder |
| per-step CPU env | ~1.4ms | OMP parallel c_step (GPU IDLE) |
| per-step sync overhead | ~0.15ms | commit + waitUntilCompleted |
| training total | ~44ms | all GPU, 8 minibatches |
| per-minibatch GPU | ~5.5ms | estimated from total / 8 |
| GPU idle during rollout | ~45ms | 32 × 1.4ms CPU env steps |

the GPU is idle for ~45ms during rollout (CPU env steps), which almost exactly matches training's 44ms. perfect overlap would give ~52ms/iter → ~620k SPS.

## approach: chunked training on second queue

lowest-effort change that addresses the root cause (one giant command buffer).

### what changes

in `train_impl`, after each minibatch, commit the current command buffer and start a new one on train_queue. this creates 8 scheduling yield points (~5.5ms each) instead of one 44ms monolith.

```
BEFORE (one giant training cmd buffer):
  [===== 44ms training =====][rollout stuck behind it]

AFTER (8 small chunks):
  [5.5ms][5.5ms][5.5ms][5.5ms][5.5ms][5.5ms][5.5ms][5.5ms]
  rollout's 0.28ms command buffers can squeeze between chunks
```

Metal guarantees in-order execution within a queue, so waiting on the last command buffer guarantees all previous ones completed.

### code changes needed

**metal_pufferlib.mm — train_impl minibatch loop:**
after each minibatch (except the last), commit current train stream cmd buffer and begin a new one.

```cpp
// at end of each minibatch iteration (inside the for loop):
if (pufferl.overlap_enabled && mb < total_minibatches - 1) {
    MetalStream* ts = (MetalStream*)train_stream;
    ts->end_compute();
    [ts->cmd commit];
    ts->cmd = [ts->queue commandBuffer];
    ts->enc_active = false;
    ts->pending_work = false;
}
```

the final minibatch is handled by the existing flush/sync_pending_train logic.

### diagnostic instrumentation (do first)

before implementing, add per-command-buffer timing to prove the theory:

```objc
// In MetalStream::sync():
[cmd addCompletedHandler:^(id<MTLCommandBuffer> buf) {
    double gpu_ms = (buf.GPUEndTime - buf.GPUStartTime) * 1000.0;
    double sched_ms = (buf.GPUStartTime - buf.kernelStartTime) * 1000.0;
    fprintf(stderr, "[metal-diag] gpu=%.3fms sched_wait=%.3fms\n", gpu_ms, sched_ms);
}];
```

if `sched_wait` is large during overlap, that confirms queueing delay (not kernel slowdown).

### hazard tracking check

verify no MTLBuffer is shared between train and rollout queues:
- `alloc_fp32.params` (train writes) vs `infer_params_alloc` (rollout reads) — separate allocators, separate MTLBuffers, GOOD
- `fused_enc_layer0` — separate alloc per PolicyWeights, GOOD
- `rollouts` buffer — only rollout writes, training reads. **this is shared!** training's select_copy reads from rollouts.observations/actions/logprobs/values. need to check if this forces hazard ordering.
- `losses_puf`, `prio_bufs`, `advantages_puf` — training-only, should be fine
- `addmm_temp` buffer — training-only (muon), fine

the rollouts buffer is the most suspicious shared resource. if hazard tracking on it forces the entire training command buffer to wait for rollout (or vice versa), that could explain the slowdown. possible fix: use `MTLResourceHazardTrackingModeUntracked` on the rollouts allocator and manage synchronization explicitly.

## future: cooperative interleaving (if chunked training isn't enough)

if chunked training on two queues still shows degradation, the next step is interleaving training chunks into rollout CPU gaps on a SINGLE queue:

```
step 0: [GPU fwd 0.28ms] [sync] [submit train chunk 0] [CPU env 1.4ms]
step 1: [GPU fwd 0.28ms] [sync] [submit train chunk 1] [CPU env 1.4ms]
...
```

this is harder because:
- rollout runs on worker threads (per-buffer), need coordination
- need to restructure train_impl into a pull-based "next chunk" iterator
- single queue means no Metal scheduling uncertainty

## MTLEvent approach (if we need GPU→GPU synchronization)

MTLSharedEvent can replace waitUntilCompleted for cross-queue coordination:
- training signals event value N after each minibatch
- rollout can wait on event value N before reading updated weights
- CPU code can listen to event values without blocking GPU

not needed for the initial chunked approach (sync_pending_train already handles weight copy after all training completes), but useful if we later want per-minibatch weight updates.

## references

- Apple forums thread/63736 — multi-queue scheduling (driver decides)
- Apple forums thread/87964 — compute+render interleaving solution (split work)
- Apple forums thread/721118 — M1 Ultra parallel compute attempt (no throughput gain)
- WWDC2025/205 — Metal 4 concurrency model
- MTLHazardTrackingMode docs — implicit buffer ordering
