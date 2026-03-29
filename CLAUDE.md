# pufferlib-metal

custom Metal (Apple GPU) backend for the PufferLib RL training framework.
targets Apple Silicon (M4 Pro). current upstream source of truth: PufferAI/PufferLib `4.0`.

## build

```bash
python setup.py build_breakout --force      # breakout env
python setup.py build_g2048 --force         # 2048 env
python setup.py build_osrs_pvp --force      # osrs pvp env
python setup.py build_osrs_zulrah --force   # osrs zulrah env
python setup.py build_osrs_inferno --force  # osrs inferno env
```

output: `pufferlib/_C.cpython-312-darwin.so`

## run training

```bash
python pufferl.py train breakout                              # uses .ini defaults
python pufferl.py train breakout --total-timesteps 2000000    # CLI override
python pufferl.py train osrs_pvp --replay-ratio 0.25          # multi-head envs need low replay
python pufferl.py sweep breakout --timeout 4                  # Protein hyperparameter sweep
python pufferl.py results breakout                            # print sweep results
```

## config system

configs live in `pufferlib/config/metal/`:
- `default.ini` -- shared Metal defaults for all envs
- `ocean/<env>.ini` -- per-env overrides: `[base]`, `[env]`, `[vec]`, `[train]`, `[policy]`, `[sweep.*]`

`pufferl.py` reads default.ini + env .ini via configparser, merges them, then builds argparse
dynamically from all keys. any .ini key is available as a CLI flag (e.g. `--learning-rate 0.05`).
sweep ranges use `[sweep.train.learning_rate]` sections with `distribution`, `min`, `max`, `scale`.

to add a new env: create `pufferlib/config/metal/ocean/<env>.ini` with the relevant sections.
pufferl.py is completely env-agnostic -- zero env-specific code.

PFSP (prioritized fictitious self-play) logic for osrs_pvp lives in
`pufferlib/ocean/osrs_pvp/pfsp.py`, imported conditionally during sweep trials.

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

- MinGRU scan kernel uses highway/residual connection (matches upstream `4.0`, not storm fork)
- scan kernel uses fast::exp/fast::log in fp32 path (matches upstream __expf/__logf)
- CPU inference for rollouts (cblas_sgemm), GPU for training
- Muon optimizer only (adam removed)
