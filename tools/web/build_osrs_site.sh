#!/usr/bin/env bash
set -euo pipefail

ENV=osrs_inferno
SRC_DIR=ocean/osrs_inferno
OUT_DIR=build_web/osrs_inferno
RAYLIB_NAME=raylib-5.5_webassembly
RAYLIB_URL=https://github.com/raysan5/raylib/releases/download/5.5/$RAYLIB_NAME.zip

download_raylib() {
    if [ -d "$RAYLIB_NAME" ]; then
        return
    fi
    echo "Downloading $RAYLIB_NAME..."
    curl -sL "$RAYLIB_URL" -o "$RAYLIB_NAME.zip"
    unzip -q "$RAYLIB_NAME.zip"
    rm "$RAYLIB_NAME.zip"
}

if [ ! -d "$SRC_DIR" ]; then
    echo "missing $SRC_DIR"
    exit 1
fi

download_raylib
bash ocean/osrs/scripts/setup-data.sh
mkdir -p "$OUT_DIR"

emcc \
    -o "$OUT_DIR/game.html" \
    "$SRC_DIR/$ENV.c" \
    -O3 -Wall \
    "$RAYLIB_NAME/lib/libraylib.a" \
    -I. -I./src -I./vendor -I./"$RAYLIB_NAME"/include \
    -L. -L./"$RAYLIB_NAME"/lib \
    -sASSERTIONS=2 -gsource-map \
    -sUSE_GLFW=3 -sUSE_WEBGL2=1 -sASYNCIFY -sFILESYSTEM -sFORCE_FILESYSTEM=1 \
    --shell-file tools/web/osrs_shell.html \
    -sINITIAL_MEMORY=512MB -sALLOW_MEMORY_GROWTH -sSTACK_SIZE=512KB \
    -DNDEBUG -DPLATFORM_WEB -DGRAPHICS_API_OPENGL_ES3 \
    --preload-file ocean/osrs/data@ocean/osrs/data

python3 tools/web/prepare_osrs_site.py --out "$OUT_DIR"
echo "Built: $OUT_DIR/game.html"
