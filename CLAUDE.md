# pufferlib-metal

custom Metal (Apple GPU) backend for the PufferLib RL training framework.
targets Apple Silicon (M4 Pro). upstream reference: PufferAI/PufferLib static-native branch (CUDA).

## build

```bash
python setup.py build_breakout --force    # breakout env
python setup.py build_g2048 --force       # 2048 env
python setup.py build_osrs_pvp --force    # osrs pvp env
python setup.py build_osrs_zulrah --force # osrs zulrah env
```

output: `pufferlib/_C.cpython-312-darwin.so`

## run training

```bash
python bench.py --env breakout
python bench.py --env osrs_pvp --cpu-inference
python sweep_bench.py --env osrs_pvp --timeout 8
```

## research tracking with flywheel

IMPORTANT: all research findings, experiment results, architecture decisions, and sweep analyses
MUST be captured in Flywheel nodes. this is non-negotiable. the repo is tracked under:
`git@github.com-personal:valtterivalo/PufferLib.git`

when to create/update flywheel nodes:
- completing a sweep phase or significant training run
- discovering architectural differences or bugs
- making key decisions (deleting code paths, changing hyperparams, etc.)
- edge analysis results that inform next steps
- profiling results and optimization findings

use `flywheel_stage_node_create` for new research checkpoints, `flywheel_stage_node_update`
to add findings, and `flywheel_commit_node` when a checkpoint is decision-ready. always
include `repo_url` and `branch_name` when creating/updating nodes.

node kinds:
- `empirical`: sweep results, training runs, profiling data (needs hypothesis + artifacts)
- `insight`: architecture findings, decision rationale, analysis conclusions (needs insights list)

## key architecture notes

- MinGRU scan kernel uses highway/residual connection (matches upstream static-native, NOT storm fork)
- scan kernel uses fast::exp/fast::log in fp32 path (matches upstream __expf/__logf)
- CPU inference for rollouts (cblas_sgemm), GPU for training
- Muon optimizer only (adam removed)
