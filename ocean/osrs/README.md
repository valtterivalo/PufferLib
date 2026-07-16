# OSRS encounters

Shared OSRS combat/sim layers plus two PufferLib envs: `osrs_colosseum` (Fortis
Colosseum) and `osrs_inferno` (The Inferno). Each env is one header compiled into
the native trainer via `-DENV_HEADER`.

## Build

```bash
./build.sh osrs_colosseum
./build.sh osrs_inferno
```

`build.sh` auto-runs `ocean/osrs/scripts/setup-data.sh`, which fetches and verifies
the render/model assets into `ocean/osrs/data` from `ocean/osrs/asset_manifest.json`.

## Train and eval

The native binary is `./puffer`. Configs live at `config/osrs_colosseum.ini` and
`config/osrs_inferno.ini`.

```bash
./puffer train osrs_colosseum
./puffer eval osrs_colosseum
./puffer eval osrs_colosseum --load-model-path=checkpoints/osrs_colosseum/model.bin
./puffer sweep osrs_inferno
```

Any `[base]` / `[train]` / `[env]` key can be overridden on the CLI, e.g.
`--train.total-timesteps=50_000_000` or `--env.start-wave=1`.

## Tests

Each file in `ocean/osrs/tests` is a self-contained C binary with its own `main()`;
exit code 0 means pass. Run one from the repo root:

```bash
cc -std=c11 -O2 -I. -o /tmp/t ocean/osrs/tests/test_colosseum_golden.c -lm && /tmp/t
```
