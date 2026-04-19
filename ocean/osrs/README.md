# osrs envs

build the compiled backend with the normal puffer flow:

```bash
./build.sh osrs_inferno
./build.sh osrs_zulrah
./build.sh osrs_pvp
```

the active python entrypoint is `puffer`, backed by `pufferlib/pufferl.py`.

```bash
puffer train osrs_inferno
puffer sweep osrs_inferno
puffer eval osrs_inferno
puffer eval osrs_inferno --load-model-path /path/to/checkpoint.bin
```

env configs live in `config/ocean/<env>.ini`.

inferno best replay recording is opt-in:

```bash
puffer train osrs_inferno --env.record-best-replay-path checkpoints/osrs_inferno/best.replay
puffer eval osrs_inferno --env.play-replay-path checkpoints/osrs_inferno/best.replay
```

the standalone visual binary still exists for direct rendering:

```bash
cd ocean/osrs
make visual
./osrs_visual --encounter inferno --replay /path/to/best.replay
```
