#!/usr/bin/env bash
set -euo pipefail

if [ $# -lt 1 ]; then
    echo "Usage: tools/metal/build.sh ENV_NAME"
    exit 1
fi

ENV=$1
shift

if [ "$(uname -s)" != "Darwin" ]; then
    echo "Metal overlay builds only run on macOS"
    exit 1
fi

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
cd "$ROOT"

PYTHON_BIN="${PYTHON:-}"
if [ -z "$PYTHON_BIN" ]; then
    if command -v python >/dev/null 2>&1; then
        PYTHON_BIN=python
    elif command -v python3 >/dev/null 2>&1; then
        PYTHON_BIN=python3
    else
        echo "python or python3 not found"
        exit 1
    fi
fi

if [ -d "ocean/$ENV" ]; then
    SRC_DIR="ocean/$ENV"
elif [ "$ENV" = "constellation" ]; then
    SRC_DIR="constellation"
elif [ "$ENV" = "trailer" ]; then
    SRC_DIR="trailer"
else
    echo "environment '$ENV' not found"
    exit 1
fi

BINDING_SRC="$SRC_DIR/binding.c"
if [ ! -f "$BINDING_SRC" ]; then
    echo "$BINDING_SRC not found"
    exit 1
fi

bash ./build.sh "$ENV" --cpu

RAYLIB_NAME="raylib-5.5_macos"
RAYLIB_A="$RAYLIB_NAME/lib/libraylib.a"
if [ ! -f "$RAYLIB_A" ]; then
    echo "$RAYLIB_A not found after CPU build"
    exit 1
fi

PYTHON_INCLUDE=$("$PYTHON_BIN" -c "import sysconfig; print(sysconfig.get_path('include'))")
PYBIND_INCLUDE=$("$PYTHON_BIN" -c "import pybind11; print(pybind11.get_include())")
NUMPY_INCLUDE=$("$PYTHON_BIN" -c "import numpy; print(numpy.get_include())")
EXT_SUFFIX=$("$PYTHON_BIN" -c "import sysconfig; print(sysconfig.get_config_var('EXT_SUFFIX'))")
OUTPUT="pufferlib/_C${EXT_SUFFIX}"

OMP_INCLUDE=()
OMP_LINK=()
OMP_SOURCE="system"
OMP_PREFIX="$(brew --prefix libomp 2>/dev/null || true)"
if [ -n "$OMP_PREFIX" ]; then
    OMP_INCLUDE=(-I"$OMP_PREFIX/include")
fi
TORCH_OMP=$("$PYTHON_BIN" -c "import torch; print(torch.__path__[0] + '/lib/libomp.dylib')" 2>/dev/null || true)
if [ -f "$TORCH_OMP" ]; then
    TORCH_OMP_DIR="$(dirname "$TORCH_OMP")"
    OMP_LINK=(-L"$TORCH_OMP_DIR" -Wl,-rpath,"$TORCH_OMP_DIR" -lomp)
    OMP_SOURCE="torch"
else
    if [ -n "$OMP_PREFIX" ]; then
        OMP_LINK=(-L"$OMP_PREFIX/lib" -Wl,-rpath,"$OMP_PREFIX/lib" -lomp)
        OMP_SOURCE="homebrew"
    else
        OMP_LINK=(-lomp)
    fi
fi

mkdir -p build/metal

COMMON_FLAGS=(
    -fPIC
    -std=c++17
    -ObjC++
    -fobjc-arc
    -O2
    -DNDEBUG
    -DWITH_METAL
    -DENV_NAME="$ENV"
    -DENV_BINDING_SRC="\"$BINDING_SRC\""
    -DNPY_NO_DEPRECATED_API=NPY_1_7_API_VERSION
    -DPLATFORM_DESKTOP
    -I.
    -Isrc/metal
    -Isrc
    -I"$SRC_DIR"
    -Ivendor
    -I"$RAYLIB_NAME/include"
    -I"$PYTHON_INCLUDE"
    -I"$PYBIND_INCLUDE"
    -I"$NUMPY_INCLUDE"
    "${OMP_INCLUDE[@]}"
    -Wno-address-of-temporary
    -Wno-c++11-narrowing
    -Xclang
    -fopenmp
)

echo "Compiling Metal overlay for $ENV"
clang++ -c "${COMMON_FLAGS[@]}" src/metal/bindings.mm -o build/metal/bindings.o
clang++ -c "${COMMON_FLAGS[@]}" src/metal/platform.mm -o build/metal/platform.o

echo "Linking $OUTPUT"
echo "OpenMP: $OMP_SOURCE"
clang++ -shared -fPIC -undefined dynamic_lookup \
    build/metal/bindings.o build/metal/platform.o \
    "$RAYLIB_A" \
    -framework Metal -framework Accelerate -framework Foundation \
    -framework Cocoa -framework OpenGL -framework IOKit \
    -framework CoreGraphics -framework CoreFoundation \
    -framework CoreVideo -framework CoreAudio \
    -framework AudioToolbox -framework UniformTypeIdentifiers \
    "${OMP_LINK[@]}" \
    -o "$OUTPUT"

if [ "$OMP_SOURCE" = "torch" ]; then
    for INSTALL_NAME in \
        "/opt/llvm-openmp/lib/libomp.dylib" \
        "/opt/homebrew/opt/libomp/lib/libomp.dylib" \
        "/usr/local/opt/libomp/lib/libomp.dylib"; do
        install_name_tool -change "$INSTALL_NAME" "@rpath/libomp.dylib" "$OUTPUT" 2>/dev/null || true
    done
fi

echo "Built: $OUTPUT"
