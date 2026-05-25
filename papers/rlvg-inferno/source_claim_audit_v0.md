# RLVG Inferno Source Claim Audit v0

Date: 2026-05-25.

Status: current source and artifact evidence for the concrete claims in `paper_v0.pdf`.

## Checkout

- Repo: `/Users/valtterivalo/Projects/pufferlib-metal`
- Branch: `inferno-upstream-sync-may-14`
- Evidence mode: command-output verification on the active branch, with exact commands recorded below.
- Inventory command: `python3 ~/.codex/skills/pufferlib/scripts/puffer_inventory.py .`
- Inventory result: clean checkout, PufferLib package version `4.0.0`

## Environment Surface

Direct backend check:

```text
env_name osrs_inferno
obs_size 833
num_action_heads 9
action_dims [25, 6, 38, 4, 2, 4, 3, 2, 5]
action_sum 89
```

Evidence:

- `python3 - <<'PY' ... import pufferlib._C as C ... C.env_name ... C.env_obs_size() ... C.env_num_action_heads() ... C.env_action_dims() ... PY`
- `uv run ./build.sh osrs_inferno` refreshed the ignored Python 3.11 extension, then `uv run python - <<'PY' ... import pufferlib._C as C ... PY` reported the same surface.
- `ocean/osrs_inferno/binding.c` defines `INF_TOTAL_OBS` as `INF_NUM_OBS + INF_ACTION_MASK_SIZE`.
- `ocean/osrs/encounters/inferno/encounter_inferno_forecast.inc` defines `INF_NUM_OBS` from player, pillar, NPC, step-out forecast, pending-hit, and pending-spark features.
- `ocean/osrs/encounters/inferno/encounter_inferno_player_actions.inc` defines the nine action heads and `INF_ACTION_MASK_SIZE`.
- `config/ocean/osrs_inferno.ini` documents the intended current surface as 9 heads, 89 choices, 744 base observation features, 89 mask entries, and 833 trainer-row floats.

Conclusion: the paper's current-surface claim is supported by both compiled backend introspection and source definitions.

## Score Definition

Evidence:

- `ocean/osrs_inferno/binding.c` computes full-start score as `wins + (1 - wins) * wave_frac * 0.5`.
- `wave_frac` is `log->wave / INF_NUM_WAVES`.
- `INF_NUM_WAVES` is 69 in `ocean/osrs/encounters/inferno/encounter_inferno_model.inc`.

Spot check:

```text
0.489635 + (1 - 0.489635) * 66.720871 / 69 * 0.5 = 0.736389
```

Conclusion: Table 3's score explanation matches the active logging code.

## Checkpoint Telemetry

Artifact: `checkpoints/osrs_inferno/redemption_j6bgoiu4_compact/run_whl5mxay.json`.

Final logged values, with scalar telemetry read from `metrics.<name>[-1]`:

```text
metrics.agent_steps[-1]=171704320
metrics.SPS[-1]=256536
metrics.env/wins[-1]=0.4896352291107178
metrics.env/score[-1]=0.7363886833190918
metrics.env/wave[-1]=66.72087097167969
metrics.env/min_zuk_hp_normal[-1]=240.5355987548828
metrics.env/prayer_correct_rate[-1]=0.8533950448036194
metrics.env/frac_healer_spawned_normal[-1]=0.8470191359519958
metrics.env/frac_all_zuk_healers_dead_normal[-1]=0.718624472618103
metrics.env/frac_normal[-1]=1.0
metrics.env/n[-1]=6223.0
eval_episodes=10000
seed=73
train.seed=42
train.state_curriculum_mode=1
policy.hidden_size=512
policy.num_layers=2
vec.total_agents=4096
train.horizon=16
train.replay_ratio=4
```

The sibling README records the compact Redemption action mapping, the run id `whl5mxay`, and the same headline metrics.

Conclusion: Table 3 is correctly described as stored development telemetry from the compact Redemption checkpoint line, not as a frozen-schema evaluation of the current surface.

## PufferLib 4 Systems Claims

Evidence:

- `src/pufferlib.cu` registers static rollout and train buffers, copies optional action masks, captures CUDA graphs for rollout, implements priority replay, and transposes rollout layout from `(T, B)` to `(B, T)` for training.
- `src/models.cu` names MinGRU fused scan as a core 4.0 path and implements MinGRU train and rollout registrations.
- `src/models.cu` applies highway-style output mixing in both rollout and scan paths: `sigmoid(proj) * mingru_out + (1 - sigmoid(proj)) * x` for rollout and the same projection gate over `scan_result` during sequence training.
- `src/puffernet.h` implements the C PufferNet inference path used by browser demos and standalone visual runners, with architecture `Linear encoder -> N x MinGRU -> Linear decoder`.
- `src/puffernet.h` states that its MinGRU inference layer matches the fused gate and highway connection in `models.cu`.
- `pufferlib/sweep.py` implements the Protein sweep loop used for score and cost search.

Conclusion: the paper's systems framing is grounded in active source files, not only public prose sources.

## Caveats Preserved

- The current compiled surface is 833 floats with an explicit 89-choice action mask.
- The checkpoint evidence comes from the compact Redemption action mapping and remains incompatible with the current explicit-Redemption decoder.
- The stored run JSON has `eval_episodes=10000`, but the downsampled final `env/n=6223.0` is not used as a precise denominator in the paper.
- The paper keeps the evidence boundary explicit in the abstract and Section 4.
