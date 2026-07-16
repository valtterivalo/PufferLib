# OSRS Inferno

Build the CUDA backend with the normal Puffer flow:

```bash
./build.sh osrs_inferno
```

The active Python entrypoint is `puffer`, backed by `pufferlib/pufferl.py`.

```bash
puffer train osrs_inferno
puffer sweep osrs_inferno
puffer eval osrs_inferno
puffer eval osrs_inferno --load-model-path /path/to/checkpoint.bin
```

Env configs live in `config/ocean/osrs_inferno.ini`.

Inferno best replay recording is opt-in:

```bash
puffer train osrs_inferno --env.record-best-replay-path checkpoints/osrs_inferno/best.replay
puffer eval osrs_inferno --env.play-replay-path checkpoints/osrs_inferno/best.replay
```

The standalone visual binary is useful for local render and sim checks:

```bash
./build.sh osrs_inferno --local
./osrs_inferno --encounter inferno
./osrs_inferno --encounter inferno --replay /path/to/best.replay
```
