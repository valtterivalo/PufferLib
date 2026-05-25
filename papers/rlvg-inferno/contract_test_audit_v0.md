# RLVG Inferno Contract Test Audit v0

Date: 2026-05-25.

Command:

```bash
cc -std=c11 -O0 -g -I. -o /tmp/test_inferno_attack_styles ocean/osrs/tests/test_inferno_attack_styles.c -lm && /tmp/test_inferno_attack_styles
```

Result: `1441/1441 tests passed`.

## Coverage Relevant To Paper Claims

- Inferno action surface uses `INF_ACTION_DIMS_INIT`.
- The test asserts `INF_NUM_OBS == 744` and `INF_ACTION_MASK_SIZE == 89`, matching the 833-float rollout row described in the paper.
- Observation shape includes step-out forecast features.
- Action masks cover prayer, movement, target, spell, gear, food, potion, special attack, and offensive prayer behavior.
- Zuk movement, shield pathing, healer phases, projectile timing, pending hits, and render identity have regression coverage.
- Snapshot and restore paths preserve environment state and derived state.
- Binding config forwards reward, mask, loadout, terminal penalty, and trace settings used in the paper notes.

## Conclusion

The focused C contract test supports the paper's current claims about Inferno observation, action, mask, render, and replay contracts.
