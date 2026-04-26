# pufferlib-metal

custom Metal (Apple GPU) backend for the PufferLib RL training framework.
targets Apple Silicon (M4 Pro). current upstream source of truth: PufferAI/PufferLib `4.0`.

## build

```bash
./build.sh breakout       # breakout env
./build.sh g2048          # 2048 env
./build.sh osrs_pvp       # osrs pvp env
./build.sh osrs_zulrah    # osrs zulrah env
./build.sh osrs_inferno   # osrs inferno env
```

output: `pufferlib/_C.cpython-312-darwin.so`

build.sh auto-detects Darwin and wires the Metal path (`-Xclang -fopenmp`, Metal
bindings in `src/metal_*.mm`). on Linux the same command builds the CUDA path.

## run training and eval

```bash
puffer train osrs_inferno                                 # uses .ini defaults
puffer train osrs_inferno --train.total-timesteps 2000000 # CLI override
puffer train osrs_pvp --train.replay-ratio 0.25           # multi-head envs want low replay
puffer sweep osrs_inferno --timeout 4                     # Protein hyperparameter sweep
puffer results osrs_inferno                               # print sweep results

puffer eval osrs_inferno                                  # auto-loads latest checkpoint
puffer eval osrs_inferno --env.start-wave 69              # jump straight to Zuk
puffer eval osrs_inferno --load-model-path /path/to.bin   # specific checkpoint
```

`puffer eval` with no `--load-model-path` auto-resolves the newest
`checkpoints/<env>/**/*.bin` (or warns and runs random weights if none exists).

## workflow note: editable install pins pufferlib to one path

the `puffer` CLI is a `pip install -e` entry point that imports pufferlib from
the path you installed from (see `__editable___pufferlib_4_0_0_finder.py` in
site-packages). it does NOT follow the current working directory.

consequence: if your main repo is on `pr500-osrs` but you `cd` into a worktree
on `inferno-encounter`, `puffer eval` still imports main's pufferlib — the
worktree's code is ignored.

do the active-development branch in the main repo. use worktrees for
side-by-side diffing, not for long-running work. if you swap long-term
contexts, re-run `pip install -e .` from the new active directory.

## config system

configs live in `pufferlib/config/`:
- `default.ini` — shared defaults for all envs
- `ocean/<env>.ini` — per-env overrides: `[base]`, `[env]`, `[vec]`, `[train]`, `[policy]`, `[sweep.*]`

`config/` at the repo root is a symlink to `pufferlib/config/`.

`pufferlib/pufferl.py` reads default.ini + env .ini via configparser, merges
them, then builds argparse dynamically from all keys. any .ini key is
available as a CLI flag (e.g. `--train.learning-rate 0.05`). sweep ranges use
`[sweep.train.learning_rate]` sections with `distribution`, `min`, `max`,
`scale`.

to add a new env: create `pufferlib/config/ocean/<env>.ini` with the relevant
sections. `pufferl.py` is completely env-agnostic — zero env-specific code.

PFSP (prioritized fictitious self-play) logic for osrs_pvp lives in
`ocean/osrs_pvp/pfsp.py`, imported conditionally during sweep trials.

## key architecture notes

- MinGRU scan kernel uses highway/residual connection (matches upstream `4.0`, not storm fork)
- scan kernel uses fast::exp/fast::log in fp32 path (matches upstream __expf/__logf)
- CPU inference for rollouts (cblas_sgemm), GPU for training
- Muon optimizer only (adam removed)
