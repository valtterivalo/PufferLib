#!/usr/bin/env bash
# export all binary assets from an OSRS modern cache.
#
# usage:
#   ./scripts/export_all.sh <cache_dir>
#
# supported cache layouts:
#   - OpenRS2 flat export with numbered subdirectories (0/, 1/, 2/, 7/, 255/)
#   - Jagex disk store with main_file_cache.dat2 and main_file_cache.idx*
#
# Modern caches are read directly. If a legacy keys.json exists, exporters use
# it as a fallback for encrypted map groups.
#
# this script produces everything needed for training and the visual debug
# viewer. all output goes to ocean/osrs/data/.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../../.." && pwd)"
DATA_DIR="$SCRIPT_DIR/../data"
mkdir -p "$DATA_DIR" "$DATA_DIR/sprites"

if [ $# -lt 1 ]; then
    echo "usage: $0 <path-to-modern-osrs-cache>"
    echo ""
    echo "pass an OpenRS2 flat cache or a Jagex main_file_cache.dat2 store."
    exit 1
fi

CACHE_INPUT="$1"
if [ ! -d "$CACHE_INPUT/2" ] && [ ! -f "$CACHE_INPUT/main_file_cache.dat2" ]; then
    echo "error: $CACHE_INPUT doesn't look like a supported OSRS cache"
    exit 1
fi
CACHE="$(cd "$CACHE_INPUT" && pwd)"
KEYS="$CACHE/keys.json"
cd "$SCRIPT_DIR"

echo "=== exporting zulrah collision map ==="
if [ -f "$KEYS" ]; then
    python export_collision_map_modern.py \
        --cache "$CACHE" --keys "$KEYS" \
        --output "$DATA_DIR/zulrah.cmap" \
        --regions 35,47 35,48
else
    python export_collision_map_modern.py \
        --cache "$CACHE" \
        --output "$DATA_DIR/zulrah.cmap" \
        --regions 35,47 35,48
fi

echo ""
echo "=== exporting zulrah terrain ==="
python export_terrain.py \
    --modern-cache "$CACHE" \
    --output "$DATA_DIR/zulrah.terrain" \
    --regions "35,47 35,48"

echo ""
echo "=== exporting zulrah objects ==="
if [ -f "$KEYS" ]; then
    python export_objects.py \
        --modern-cache "$CACHE" --keys "$KEYS" \
        --output "$DATA_DIR/zulrah.objects" \
        --regions "35,47 35,48"
else
    python export_objects.py \
        --modern-cache "$CACHE" \
        --output "$DATA_DIR/zulrah.objects" \
        --regions "35,47 35,48"
fi

echo ""
echo "=== exporting equipment models ==="
python export_models.py \
    --modern-cache "$CACHE" \
    --output "$DATA_DIR/equipment.models" \
    --extra-models 14407,14408,14409,10415,20390,11221,26593,4086

echo ""
echo "=== exporting spotanims and projectile models ==="
python export_spotanims.py \
    --modern-cache "$CACHE" \
    --output "$DATA_DIR/spotanims.bin"
python export_projectile_models.py \
    --cache "$CACHE" \
    --spotanims "$DATA_DIR/spotanims.bin" \
    --spotanim-ids 15,27,85,231,368,369,377,659,660,665,1040,1042,1043,1120,1122,1468 \
    --output "$DATA_DIR/projectiles.models"

echo ""
echo "=== exporting animations ==="
ANIMATION_ARGS=(
    --modern-cache "$CACHE"
    --output "$DATA_DIR/equipment.anims"
    --include-spotanim-sequences
)
RUNEC_ITEM_RENDER_MAP="$PROJECT_ROOT/refs/RuneC/data/models/item_render.map"
RUNEC_COMBAT_VISUALS="$PROJECT_ROOT/refs/RuneC/data/defs/combat_visuals.tsv"
LOCAL_COMBAT_VISUALS="$PROJECT_ROOT/ocean/osrs/tools/combat_visuals_extra.tsv"
if [ -f "$RUNEC_ITEM_RENDER_MAP" ]; then
    ANIMATION_ARGS+=(--item-render-map "$RUNEC_ITEM_RENDER_MAP")
fi
if [ -f "$RUNEC_COMBAT_VISUALS" ]; then
    ANIMATION_ARGS+=(--combat-visuals "$RUNEC_COMBAT_VISUALS")
fi
ANIMATION_ARGS+=(--combat-visuals "$LOCAL_COMBAT_VISUALS")
python export_animations.py \
    "${ANIMATION_ARGS[@]}"

echo ""
echo "=== exporting GUI sprites (prayer icons, hitsplats) ==="
python export_sprites_modern.py \
    --cache "$CACHE" \
    --output "$DATA_DIR/sprites/gui"

python validate_gui_assets.py \
    --assets-dir "$DATA_DIR/sprites/gui" \
    --full-ancient-spellbook \
    --require-transparent

echo ""
echo "=== exporting wilderness collision map ==="
if [ -f "$KEYS" ]; then
    python export_collision_map_modern.py \
        --cache "$CACHE" --keys "$KEYS" \
        --output "$DATA_DIR/wilderness.cmap" \
        --wilderness
else
    python export_collision_map_modern.py \
        --cache "$CACHE" \
        --output "$DATA_DIR/wilderness.cmap" \
        --wilderness
fi

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
if [ -f "$KEYS" ]; then
    echo "    python scripts/export_objects.py --modern-cache $CACHE --keys $KEYS --output data/wilderness.objects --wilderness"
else
    echo "    python scripts/export_objects.py --modern-cache $CACHE --output data/wilderness.objects --wilderness"
fi
echo ""
echo "  - item sprites (inventory icons) require the Java exporter:"
echo "    javac -cp <runelite-cache-jar> scripts/ExportItemSprites.java"
echo "    java -cp .:scripts:<runelite-cache-jar> ExportItemSprites <cache_dir> ocean/osrs/data/sprites/items/"
