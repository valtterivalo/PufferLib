#!/bin/bash
set -e

# Usage:
#   ./build.sh breakout              # Build _C.so with breakout statically linked
#   ./build.sh breakout --float      # float32 precision
#   ./build.sh breakout --debug      # Debug build
#   ./build.sh breakout --local      # Standalone executable (debug, sanitizers)
#   ./build.sh breakout --fast       # Standalone executable (optimized)
#   ./build.sh breakout --web        # Emscripten web build
#   ./build.sh breakout --profile    # Kernel profiling binary

ENV=${1:?Usage: ./build.sh ENV_NAME [--float] [--debug] [--local|--fast|--web|--profile|--cpu]}
MODE=""
PRECISION=""
DEBUG=""
for arg in "${@:2}"; do
    case $arg in
        --float) PRECISION="-DPRECISION_FLOAT" ;;
        --debug) DEBUG=1 ;;
        --local) MODE=local ;;
        --fast)  MODE=fast ;;
        --web)   MODE=web ;;
        --profile) MODE=profile ;;
        --cpu)   MODE=cpu; PRECISION="-DPRECISION_FLOAT" ;;
    esac
done

CLANG_WARN="\
    -Wall \
    -ferror-limit=3 \
    -Werror=incompatible-pointer-types \
    -Werror=return-type \
    -Wno-error=incompatible-pointer-types-discards-qualifiers \
    -Wno-incompatible-pointer-types-discards-qualifiers \
    -Wno-error=array-parameter"

PLATFORM="$(uname -s)"
if [ -n "${PYTHON:-}" ]; then
    PYTHON_BIN="$PYTHON"
elif command -v python >/dev/null 2>&1; then
    PYTHON_BIN=python
elif [ -x ".venv/bin/python" ]; then
    PYTHON_BIN=".venv/bin/python"
else
    PYTHON_BIN=python3
fi

if [ -n "$DEBUG" ] || [ "$MODE" = "local" ]; then
    CLANG_OPT="-g -O0 $CLANG_WARN"
    NVCC_OPT="-O0 -g"
    LINK_OPT="-g"
    [ "$PLATFORM" = "Linux" ] && CLANG_OPT="$CLANG_OPT -fsanitize=address,undefined,bounds,pointer-overflow,leak -fno-omit-frame-pointer"
else
    CLANG_OPT="-O2 -DNDEBUG $CLANG_WARN"
    NVCC_OPT="-O3"
    LINK_OPT="-O2"
fi

# ============================================================================
# Platform + dependencies
# ============================================================================
if [ -d "ocean/$ENV" ]; then
    SRC_DIR="ocean/$ENV"
elif [ -d "$ENV" ]; then
    SRC_DIR="$ENV"
else
    echo "Error: environment '$ENV' not found" && exit 1
fi

# OSRS envs share headers from ocean/osrs
OSRS_INCLUDE=""
if [[ "$SRC_DIR" == *osrs* ]]; then
    OSRS_INCLUDE="-Iocean/osrs"
    bash ocean/osrs/scripts/setup-data.sh
fi

ENV_DEFINES=()
if [ "$ENV" = "osrs_inferno" ]; then
    ENV_DEFINES+=(-DPUFFER_ENV_OSRS_INFERNO=1)
fi

if [ "$PLATFORM" = "Linux" ]; then
    RAYLIB_NAME='raylib-5.5_linux_amd64'
else
    RAYLIB_NAME='raylib-5.5_macos'
fi

RAYLIB_URL="https://github.com/raysan5/raylib/releases/download/5.5"

download() {
    local name=$1 url=$2
    [ -d "$name" ] && return
    echo "Downloading $name..."
    if [[ "$url" == *.zip ]]; then
        curl -sL "$url" -o "$name.zip" && unzip -q "$name.zip" && rm "$name.zip"
    else
        curl -sL "$url" -o "$name.tar.gz" && tar xf "$name.tar.gz" && rm "$name.tar.gz"
    fi
}

# Raylib: web builds always need the wasm archive
if [ "$MODE" = "web" ]; then
    RAYLIB_NAME='raylib-5.5_webassembly'
    if [[ "$SRC_DIR" == *osrs* ]] && [ -d "ocean/osrs/$RAYLIB_NAME" ]; then
        RAYLIB_NAME="ocean/osrs/$RAYLIB_NAME"
    else
        download "$RAYLIB_NAME" "$RAYLIB_URL/$RAYLIB_NAME.zip"
    fi
elif [[ "$SRC_DIR" == *osrs* ]] && [ -d "ocean/osrs/$RAYLIB_NAME" ]; then
    RAYLIB_NAME="ocean/osrs/$RAYLIB_NAME"
else
    download "$RAYLIB_NAME" "$RAYLIB_URL/$RAYLIB_NAME.tar.gz"
fi
[ ! -f "$RAYLIB_NAME/include/rlights.h" ] && \
    curl -sL "https://raw.githubusercontent.com/raysan5/raylib/master/examples/shaders/rlights.h" \
        -o "$RAYLIB_NAME/include/rlights.h"

RAYLIB_A="$RAYLIB_NAME/lib/libraylib.a"
INCLUDES=(-I./$RAYLIB_NAME/include -I./src)
LINK_ARCHIVES="$RAYLIB_A"
EXTRA_SRC=""

# Box2d (impulse_wars only)
if [ "$ENV" = "impulse_wars" ]; then
    if [ "$MODE" = "web" ]; then BOX2D_NAME='box2d-web'
    elif [ "$PLATFORM" = "Linux" ]; then BOX2D_NAME='box2d-linux-amd64'
    else BOX2D_NAME='box2d-macos-arm64'
    fi
    BOX2D_URL="https://github.com/capnspacehook/box2d/releases/latest/download"
    download "$BOX2D_NAME" "$BOX2D_URL/$BOX2D_NAME.tar.gz"
    INCLUDES+=(-I./$BOX2D_NAME/include -I./$BOX2D_NAME/src)
    LINK_ARCHIVES="$LINK_ARCHIVES ./$BOX2D_NAME/libbox2d.a"
fi

# Constellation needs cJSON
[ "$ENV" = "constellation" ] && EXTRA_SRC="vendor/cJSON.c" && INCLUDES+=(-I./vendor) && OUTPUT_NAME="seethestars"

# ============================================================================
# OpenMP detection (macOS)
# ============================================================================
find_omp_include() {
    # Try Homebrew libomp first
    local omp_prefix
    omp_prefix=$(brew --prefix libomp 2>/dev/null) && [ -d "$omp_prefix/include" ] && echo "$omp_prefix/include" && return
    echo ""
}

# ============================================================================
# Standalone builds: --local, --fast, --web
# ============================================================================

if [ "$MODE" = "web" ]; then
    SHELL_FILE="./minshell.html"
    WEB_SRC="$SRC_DIR/$ENV.c"
    WEB_EXTRA=""
    PRELOAD="--preload-file resources/$ENV@resources/$ENV --preload-file resources/shared@resources/shared"
    WEB_DEFINES="-DNDEBUG -DPLATFORM_WEB -DGRAPHICS_API_OPENGL_ES3"
    if [[ "$SRC_DIR" == *osrs* ]]; then
        SHELL_FILE="ocean/osrs/web/osrs_shell.html"
        WEB_SRC="ocean/osrs/osrs_visual.c"
        WEB_EXTRA="-Iocean/osrs"
        PRELOAD="--preload-file ocean/osrs/data@ocean/osrs/data"
        WEB_DEFINES="$WEB_DEFINES -DOSRS_VISUAL"
        if [ "$ENV" = "osrs_inferno" ]; then
            INFERNO_WEB_POLICY="checkpoints/osrs_inferno/redemption_j6bgoiu4_compact/latest_eval_0000000255655936.bin"
            if [ ! -f "$INFERNO_WEB_POLICY" ]; then
                echo "Error: missing Inferno web policy: $INFERNO_WEB_POLICY"
                exit 1
            fi
            PRELOAD="$PRELOAD --preload-file $INFERNO_WEB_POLICY@resources/osrs_inferno/osrs_inferno_redemption_j6bgoiu4_compact.bin"
        fi
    fi
    if [ ! -f "$SHELL_FILE" ]; then
        if [ "$SHELL_FILE" = "./minshell.html" ]; then
            curl -sL "https://raw.githubusercontent.com/raysan5/raylib/master/src/minshell.html" -o minshell.html
        else
            echo "Error: web shell missing: $SHELL_FILE"
            exit 1
        fi
    fi

    mkdir -p "build_web/$ENV"
    echo "Building $ENV for web..."
    emcc \
        -o "build_web/$ENV/game.html" \
        "$WEB_SRC" $EXTRA_SRC \
        -O3 -Wall \
        $LINK_ARCHIVES \
        "${INCLUDES[@]}" $WEB_EXTRA \
        "${ENV_DEFINES[@]}" \
        -L. -L./$RAYLIB_NAME/lib \
        -sASSERTIONS=2 -gsource-map \
        -sUSE_GLFW=3 -sUSE_WEBGL2=1 -sASYNCIFY -sFILESYSTEM -sFORCE_FILESYSTEM=1 \
        -sINCOMING_MODULE_JS_API=arguments,canvas,locateFile,print,printErr,onRuntimeInitialized,setStatus,monitorRunDependencies \
        --shell-file "$SHELL_FILE" \
        -sINITIAL_MEMORY=512MB -sALLOW_MEMORY_GROWTH -sSTACK_SIZE=512KB \
        $WEB_DEFINES \
        $PRELOAD
    if [[ "$SRC_DIR" == *osrs* ]]; then
        python3 ocean/osrs/scripts/chunk_web_data.py "build_web/$ENV/game.data" --remove-source
        WEB_ASSET_VERSION=$(python3 - "build_web/$ENV" <<'PY'
from hashlib import sha256
from pathlib import Path
import sys

root = Path(sys.argv[1])
hasher = sha256()
for path in [
    root / "game.js",
    root / "game.wasm",
    root / "game.data.chunks.json",
    *sorted(root.glob("game.data.part*")),
]:
    hasher.update(path.name.encode())
    hasher.update(b"\0")
    hasher.update(path.read_bytes())
print(hasher.hexdigest()[:16])
PY
)
        python3 - "$WEB_ASSET_VERSION" "build_web/$ENV/game.html" <<'PY'
from pathlib import Path
import sys

version = sys.argv[1]
path = Path(sys.argv[2])
html = path.read_text()
html = html.replace("__OSRS_LAB_ASSET_VERSION__", version)
html = html.replace("src=game.js", f"src=game.js?v={version}")
html = html.replace('src="game.js"', f'src="game.js?v={version}"')
path.write_text(html)
PY
    fi
    echo "Built: build_web/$ENV/game.html"
    exit 0
fi

if [ "$MODE" = "local" ] || [ "$MODE" = "fast" ]; then
    # OSRS envs use ocean/osrs/osrs_visual.c as the visual binary source
    if [[ "$SRC_DIR" == *osrs* ]]; then
        LOCAL_SRC="ocean/osrs/osrs_visual.c"
    else
        LOCAL_SRC="$SRC_DIR/$ENV.c"
    fi
    FLAGS=(
        "${INCLUDES[@]}"
        -I. -I$SRC_DIR $OSRS_INCLUDE
        "${ENV_DEFINES[@]}"
        "$LOCAL_SRC" $EXTRA_SRC -o "${OUTPUT_NAME:-$ENV}"
        $LINK_ARCHIVES
        -DPLATFORM_DESKTOP -DOSRS_VISUAL
        -lm
    )
    if [ "$PLATFORM" = "Darwin" ]; then
        OMP_INC=$(find_omp_include)
        OMP_DIR=$(brew --prefix libomp 2>/dev/null)/lib
        [ -n "$OMP_INC" ] && FLAGS+=(-I"$OMP_INC" -Xclang -fopenmp -L"$OMP_DIR" -lomp)
        FLAGS+=(-framework Cocoa -framework OpenGL -framework IOKit -framework CoreVideo)
    else
        FLAGS+=(-fopenmp -lGL -lpthread)
    fi
    clang $CLANG_OPT "${FLAGS[@]}"
    echo "Built: ./${OUTPUT_NAME:-$ENV}"
    exit 0
fi

STANDALONE_OUTPUT="${OUTPUT_NAME:-$ENV}"
if [ -e "$STANDALONE_OUTPUT" ] || [ -e "${STANDALONE_OUTPUT}.exe" ]; then
    echo "WARNING: ./$STANDALONE_OUTPUT is a standalone visual binary and was not rebuilt."
    echo "         Use './build.sh $ENV --local' to rebuild it, or 'puffer eval $ENV' for policy eval."
fi

# ============================================================================
# Default: build _C.so with env statically linked
# ============================================================================

PYTHON_INCLUDE=$("$PYTHON_BIN" -c "import sysconfig; print(sysconfig.get_path('include'))")
PYBIND_INCLUDE=$("$PYTHON_BIN" -c "import pybind11; print(pybind11.get_include())")
NUMPY_INCLUDE=$("$PYTHON_BIN" -c "import numpy; print(numpy.get_include())")
EXT_SUFFIX=$("$PYTHON_BIN" -c "import sysconfig; print(sysconfig.get_config_var('EXT_SUFFIX'))")
OUTPUT="pufferlib/_C${EXT_SUFFIX}"

# Step 1: Static env library
BINDING_SRC="$SRC_DIR/binding.c"
STATIC_OBJ="src/libstatic_${ENV}.o"
STATIC_LIB="src/libstatic_${ENV}.a"
[ ! -f "$BINDING_SRC" ] && echo "Error: $BINDING_SRC not found" && exit 1

echo "=== Building static env: $ENV ==="

OMP_INCLUDE_FLAG=""
if [ "$PLATFORM" = "Darwin" ]; then
    OMP_INC=$(find_omp_include)
    [ -n "$OMP_INC" ] && OMP_INCLUDE_FLAG="-I$OMP_INC"
    OMP_COMPILE_FLAG="-Xclang -fopenmp"
else
    OMP_COMPILE_FLAG="-fopenmp"
fi

clang -c $CLANG_OPT \
    -I. -Isrc -I$SRC_DIR $OSRS_INCLUDE \
    "${ENV_DEFINES[@]}" \
    -I./$RAYLIB_NAME/include \
    -DENV_NAME=$ENV \
    -DPLATFORM_DESKTOP \
    -fno-semantic-interposition -fvisibility=hidden \
    -fPIC $OMP_INCLUDE_FLAG $OMP_COMPILE_FLAG \
    "$BINDING_SRC" -o "$STATIC_OBJ"
ar rcs "$STATIC_LIB" "$STATIC_OBJ"

# ============================================================================
# Platform-specific: compile GPU bindings + link
# ============================================================================

if [ "$PLATFORM" = "Darwin" ]; then
    # =======================================================================
    # Metal path (macOS / Apple Silicon)
    # =======================================================================

    # Step 2: Compile Metal ObjC++ bindings
    echo "=== Compiling Metal bindings ==="
    for SRC_FILE in src/metal_bindings.mm src/metal_platform.mm; do
        OBJ_FILE="${SRC_FILE%.mm}.o"
        clang++ -c -fPIC -std=c++17 -ObjC++ -fobjc-arc \
            $PRECISION \
            -DWITH_METAL \
            -DENV_NAME=$ENV \
            "${ENV_DEFINES[@]}" \
            -DNPY_NO_DEPRECATED_API=NPY_1_7_API_VERSION \
            -DPLATFORM_DESKTOP \
            -I. -Isrc \
            -I$PYTHON_INCLUDE -I$PYBIND_INCLUDE -I$NUMPY_INCLUDE \
            -I./$RAYLIB_NAME/include \
            $OMP_INCLUDE_FLAG -Xclang -fopenmp \
            $CLANG_OPT \
            "$SRC_FILE" -o "$OBJ_FILE"
        echo "  $OBJ_FILE"
    done

    # Step 3: Link _C.so
    echo "=== Linking $OUTPUT ==="

    # OpenMP runtime: prefer torch's libomp, then Homebrew
    OMP_FLAG=""
    OMP_LIB=$("$PYTHON_BIN" -c "import torch; print(torch.__path__[0] + '/lib/libomp.dylib')" 2>/dev/null || echo "")
    OMP_SOURCE=""
    if [ -f "$OMP_LIB" ]; then
        OMP_DIR=$(dirname "$OMP_LIB")
        OMP_FLAG="-L$OMP_DIR -Wl,-rpath,$OMP_DIR -lomp"
        OMP_SOURCE="torch"
    else
        OMP_DIR=$(brew --prefix libomp 2>/dev/null)/lib
        if [ -d "$OMP_DIR" ]; then
            OMP_FLAG="-L$OMP_DIR -Wl,-rpath,$OMP_DIR -lomp"
            OMP_SOURCE="homebrew"
        else
            OMP_FLAG="-lomp"
            OMP_SOURCE="system"
        fi
    fi
    echo "  OpenMP: $OMP_SOURCE"

    clang++ -shared -fPIC -undefined dynamic_lookup \
        src/metal_bindings.o src/metal_platform.o \
        -Wl,-force_load,"$STATIC_LIB" \
        "$RAYLIB_A" \
        -framework Metal -framework Accelerate -framework Foundation \
        -framework Cocoa -framework OpenGL -framework IOKit \
        -framework CoreGraphics -framework CoreFoundation \
        -framework CoreVideo -framework CoreAudio \
        -framework AudioToolbox -framework UniformTypeIdentifiers \
        $OMP_FLAG \
        $LINK_OPT \
        -o "$OUTPUT"

    # Normalize OpenMP linkage to @rpath when using torch's libomp,
    # so _C.so doesn't pin to a different absolute path than torch.
    if [ "$OMP_SOURCE" = "torch" ]; then
        for INSTALL_NAME in \
            "/opt/llvm-openmp/lib/libomp.dylib" \
            "/opt/homebrew/opt/libomp/lib/libomp.dylib" \
            "/usr/local/opt/libomp/lib/libomp.dylib"; do
            install_name_tool -change "$INSTALL_NAME" "@rpath/libomp.dylib" "$OUTPUT" 2>/dev/null || true
        done
    fi

else
    # =======================================================================
    # CUDA path (Linux)
    # =======================================================================

    CUDA_HOME=${CUDA_HOME:-${CUDA_PATH:-/usr/local/cuda}}
    NVCC="$CUDA_HOME/bin/nvcc"
    NVTX_LINK=()
    if [ -f "$CUDA_HOME/lib64/libnvToolsExt.so" ] || \
       [ -f "$CUDA_HOME/lib/libnvToolsExt.so" ] || \
       ldconfig -p 2>/dev/null | grep -q 'libnvToolsExt\.so'; then
        NVTX_LINK=(-lnvToolsExt)
    fi

    # Detect OBS_TENSOR_T from the static object
    OBS_TENSOR_T=$(strings "$STATIC_OBJ" | grep 'Tensor$' | head -1)
    [ -z "$OBS_TENSOR_T" ] && echo "Error: Could not find OBS_TENSOR_T" && exit 1
    echo "OBS_TENSOR_T=$OBS_TENSOR_T"

    # Step 2: Profile binary or Python bindings
    if [ "$MODE" = "profile" ]; then
        ARCH=${NVCC_ARCH:-sm_89}
        echo "=== Building profile binary (arch=$ARCH) ==="
        $NVCC $NVCC_OPT -arch=$ARCH -std=c++17 \
            -I. -Isrc -I$SRC_DIR \
            -I$CUDA_HOME/include -I$RAYLIB_NAME/include \
            -DOBS_TENSOR_T=$OBS_TENSOR_T \
            -DENV_NAME=$ENV \
            -Xcompiler=-DPLATFORM_DESKTOP \
            $PRECISION \
            -Xcompiler=-fopenmp \
            tests/profile_kernels.cu ini.c \
            "$STATIC_LIB" "$RAYLIB_A" \
            -lnccl -lnvidia-ml -lcublas -lcurand -lcudnn "${NVTX_LINK[@]}" \
            -lGL -lm -lpthread -lomp5 \
            -o profile
        echo "=== Built: ./profile ==="
        exit 0
    fi

    if [ "$MODE" = "cpu" ]; then
        echo "=== Compiling bindings_cpu.cpp ==="
        g++ -c -fPIC -fopenmp \
            -D_GLIBCXX_USE_CXX11_ABI=1 \
            -DENV_NAME=$ENV \
            -DPLATFORM_DESKTOP \
            -std=c++17 \
            -I. -Isrc \
            -I$PYTHON_INCLUDE -I$PYBIND_INCLUDE \
            -DOBS_TENSOR_T=$OBS_TENSOR_T \
            $PRECISION $LINK_OPT \
            src/bindings_cpu.cpp -o src/bindings_cpu.o

        echo "=== Linking $OUTPUT (CPU) ==="
        LINK_CMD=(
            g++ -shared -fPIC -fopenmp
            src/bindings_cpu.o "$STATIC_LIB" "$RAYLIB_A"
            -lm -lpthread -lomp5
            $LINK_OPT
        )
        LINK_CMD+=(-Bsymbolic-functions)
        LINK_CMD+=(-o "$OUTPUT")
        "${LINK_CMD[@]}"
        echo "=== Built: $OUTPUT (CPU) ==="
        exit 0
    fi

    echo "=== Compiling bindings.cu ==="
    $NVCC -c -Xcompiler -fPIC \
        -Xcompiler=-D_GLIBCXX_USE_CXX11_ABI=1 \
        -Xcompiler=-DNPY_NO_DEPRECATED_API=NPY_1_7_API_VERSION \
        -Xcompiler=-DPLATFORM_DESKTOP \
        -std=c++17 \
        -I. -Isrc \
        -I$PYTHON_INCLUDE -I$PYBIND_INCLUDE -I$NUMPY_INCLUDE \
        -I$CUDA_HOME/include -I$RAYLIB_NAME/include \
        -Xcompiler=-fopenmp \
        -DENV_NAME=$ENV \
        -DOBS_TENSOR_T=$OBS_TENSOR_T \
        $PRECISION $NVCC_OPT \
        src/bindings.cu -o src/bindings.o

    # Step 3: Link
    echo "=== Linking $OUTPUT ==="
    LINK_CMD=(
        g++ -shared -fPIC -fopenmp
        src/bindings.o "$STATIC_LIB" "$RAYLIB_A"
        -L$CUDA_HOME/lib64
        -lcudart -lnccl -lnvidia-ml -lcublas -lcusolver -lcurand -lcudnn
        "${NVTX_LINK[@]}" -lomp5
        $LINK_OPT
    )
    LINK_CMD+=(-Bsymbolic-functions)
    LINK_CMD+=(-o "$OUTPUT")
    "${LINK_CMD[@]}"
fi

echo "=== Built: $OUTPUT ==="
