#!/usr/bin/env bash
# export all binary assets from an OSRS modern cache (OpenRS2 flat file format).
#
# usage:
#   ./scripts/export_all.sh <cache_dir>
#
# the cache can be downloaded from https://archive.openrs2.org/ — pick any
# recent OSRS revision, download the "flat file" export. the directory should
# contain numbered subdirectories (0/, 1/, 2/, 7/, 255/) and a keys.json.
#
# this script produces everything needed for training and the visual debug
# viewer. all output goes to data/.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
DATA_DIR="$SCRIPT_DIR/../data"
mkdir -p "$DATA_DIR" "$DATA_DIR/sprites"

if [ $# -lt 1 ]; then
    echo "usage: $0 <path-to-modern-osrs-cache>"
    echo ""
    echo "download a cache from https://archive.openrs2.org/"
    echo "pick any recent OSRS revision, use the 'flat file' export."
    exit 1
fi

CACHE="$1"
KEYS="$CACHE/keys.json"

if [ ! -d "$CACHE/2" ]; then
    echo "error: $CACHE doesn't look like a modern cache (missing 2/ subdir)"
    exit 1
fi
if [ ! -f "$KEYS" ]; then
    echo "warning: no keys.json found — XTEA-encrypted regions will be skipped"
    KEYS=""
fi

cd "$SCRIPT_DIR"

echo "=== exporting zulrah collision map ==="
python export_collision_map_modern.py \
    --cache "$CACHE" --keys "$KEYS" \
    --output "$DATA_DIR/zulrah.cmap" \
    --regions 35,47 35,48

echo ""
echo "=== exporting zulrah terrain ==="
python export_terrain.py \
    --modern-cache "$CACHE" \
    --output "$DATA_DIR/zulrah.terrain" \
    --regions 35,47 35,48

echo ""
echo "=== exporting zulrah objects ==="
python export_objects.py \
    --modern-cache "$CACHE" --keys "$KEYS" \
    --output "$DATA_DIR/zulrah.objects" \
    --regions 35,47 35,48

echo ""
echo "=== exporting equipment models ==="
python export_models.py \
    --modern-cache "$CACHE" \
    --output "$DATA_DIR/equipment.models" \
    --extra-models 14407,14408,14409,10415,20390,11221,26593,4086

echo ""
echo "=== exporting animations ==="
python export_animations.py \
    --modern-cache "$CACHE" \
    --output "$DATA_DIR/equipment.anims"

echo ""
echo "=== exporting GUI sprites (prayer icons, hitsplats) ==="
python export_sprites_modern.py \
    --cache "$CACHE" \
    --output "$DATA_DIR/sprites/gui"

echo ""
echo "=== exporting wilderness collision map ==="
python export_collision_map_modern.py \
    --cache "$CACHE" --keys "$KEYS" \
    --output "$DATA_DIR/wilderness.cmap" \
    --wilderness

echo ""
echo "=== exporting wilderness terrain ==="
python export_terrain.py \
    --modern-cache "$CACHE" \
    --output "$DATA_DIR/wilderness.terrain" \
    --wilderness

echo ""
echo "done. all assets exported to $DATA_DIR/"
echo ""
echo "notes:"
echo "  - wilderness.objects (685MB+) is not exported by default."
echo "    run manually if needed:"
echo "    python scripts/export_objects.py --modern-cache $CACHE --keys $KEYS --output data/wilderness.objects --wilderness"
echo ""
echo "  - item sprites (inventory icons) require the Java exporter:"
echo "    javac -cp <runelite-cache-jar> scripts/ExportItemSprites.java"
echo "    java -cp .:scripts:<runelite-cache-jar> ExportItemSprites <cache_dir> data/sprites/items/"
