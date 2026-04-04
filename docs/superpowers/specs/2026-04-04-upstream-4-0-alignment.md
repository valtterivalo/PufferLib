# upstream 4.0 layout alignment

date: 2026-04-04
status: approved

## problem

our repo structure diverges from upstream PufferLib 4.0: ocean/ is inside pufferlib/,
src/ is inside pufferlib/, and we use setup.py instead of build.sh. this makes
cherry-picking upstream changes harder and complicates the OSRS env review for 4.0.

## directory moves

```
ocean/*              → ocean/*          (all envs)
src/*                → src/*            (CUDA + Metal files)
pufferlib/config/metal/        → config/metal/    (training configs)
```

pufferlib/ stays as the Python package (models.py, muon.py, __init__.py).

## build.sh

replace setup.py's Metal build logic with build.sh following upstream's pattern.

usage:
```bash
./build.sh osrs_inferno              # build _C.so
./build.sh osrs_pvp --float          # float32
./build.sh osrs_pvp --local          # standalone visual
```

platform detection:
- Darwin → Metal (clang++ ObjC++ -framework Metal -framework Accelerate)
- Linux → CUDA (nvcc, upstream logic unchanged)

env compilation (both platforms):
```
clang -c ocean/$ENV/binding.c -I. -Isrc -Iocean/$ENV -Iocean/osrs → .o
ar rcs src/libstatic_$ENV.a .o
```

Metal GPU bindings (Darwin):
```
clang++ -c -ObjC++ src/metal_bindings.mm → .o
clang++ -c -ObjC++ src/metal_platform.mm → .o
clang++ -shared .o .o libstatic.a libraylib.a -framework Metal ... → pufferlib/_C.so
```

## pufferl.py

stays at repo root. update config path: `pufferlib/config/metal/` → `config/metal/`.

## Makefile

`ocean/osrs/Makefile` — update DEMO_SRC path. visual binary built from ocean/osrs/.

## verification

```bash
./build.sh osrs_inferno
./build.sh osrs_zulrah
./build.sh osrs_pvp
python pufferl.py train osrs_inferno --total-timesteps 10000
cd ocean/osrs && make visual && ./osrs_visual --encounter inferno
# all test suites (update paths in commands)
```
