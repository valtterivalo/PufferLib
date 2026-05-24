# Review Notes For Draft v1

Status: blocker review from ChatGPT browser critique plus local evidence check on 2026-05-25.

## Main Diagnosis

Draft v1 fixed the paper shape. The remaining risks are trust-level:

- Score needs to be labeled as an internal progress metric unless the formula is placed in the paper.
- The stored run JSON is downsampled, so `env/n = 6223` is not safe to present as the true eval denominator.
- The local explicit eval path is blocked on current Metal config and checkpoint shape mismatch.
- The alignment framing is too broad for what the paper actually proves.
- The replay figure is load-bearing because inspectability is the thesis.

## Local Evidence Check

- Built `osrs_inferno` native backend successfully after finding the checkout had been compiled for `osrs_pvp`.
- Explicit no-render eval with the checkpoint config first failed because Metal rejects state-buffer curriculum.
- Overriding `state_curriculum_mode`, `state_buffer_size`, `cl_frac`, and `warmup_states` got to weight loading.
- Both packaged checkpoint files then failed with a weight-size mismatch: expected `7999488` bytes, got `7997440`.
- Therefore the paper should rely on the stored run summary for now and avoid presenting a fresh local eval denominator.

## V2 Changes

- Remove the 6,223 completed-eval claim.
- Describe `score` as an internal development progress score.
- Replace alignment wording with behavioral evaluation.
- Add one sentence that one environment step equals one OSRS game tick.
- Add a figure todo block so the missing artifact is explicit.

