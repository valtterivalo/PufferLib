# Review Notes v6

Status: local final-surface audit after reference polish.

## Finding

- The paper's 89-way surface claim exposed a real native binding mismatch.
- Encounter code used `ENCOUNTER_OVERHEAD_DIM_PVE_REDEMPTION`, so `INF_ACTION_MASK_SIZE` was 89.
- `ocean/osrs_inferno/binding.c` still used `ENCOUNTER_OVERHEAD_DIM_PVE` in `ACT_SIZES`, which made the decoder action surface 88 choices.
- Fixed by sharing `INF_ACTION_DIMS_INIT` between the encounter constants and the native binding.

## Verification

- `python3 - <<'PY' ... C.env_action_dims() ... PY` reports `[25, 6, 38, 4, 2, 4, 3, 2, 5]`, sum `89`, and obs size `833`.
- `cc -std=c11 -O0 -g -I. -o /tmp/test_inferno_attack_styles ocean/osrs/tests/test_inferno_attack_styles.c -lm && /tmp/test_inferno_attack_styles` passed `1441/1441`.
- `./build.sh osrs_inferno` rebuilt `pufferlib/_C.cpython-312-darwin.so`.

## Paper Change

- Replaced "mask logits" wording with "89 discrete action choices with matching embedded mask entries."
