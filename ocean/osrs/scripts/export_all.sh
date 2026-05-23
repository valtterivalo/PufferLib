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

echo "=== exporting inferno collision map ==="
if [ -f "$KEYS" ]; then
    python export_collision_map_modern.py \
        --cache "$CACHE" --keys "$KEYS" \
        --output "$DATA_DIR/inferno.cmap" \
        --regions 35,83
else
    python export_collision_map_modern.py \
        --cache "$CACHE" \
        --output "$DATA_DIR/inferno.cmap" \
        --regions 35,83
fi

echo ""
echo "=== exporting inferno terrain ==="
python export_terrain.py \
    --modern-cache "$CACHE" \
    --output "$DATA_DIR/inferno.terrain" \
    --regions "35,83"

echo ""
echo "=== exporting inferno objects ==="
if [ -f "$KEYS" ]; then
    python export_objects.py \
        --modern-cache "$CACHE" --keys "$KEYS" \
        --output "$DATA_DIR/inferno.objects" \
        --regions "35,83"
else
    python export_objects.py \
        --modern-cache "$CACHE" \
        --output "$DATA_DIR/inferno.objects" \
        --regions "35,83"
fi

echo ""
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
echo "=== exporting zulrah NPC models and animations ==="
python ../tools/export_encounter_npcs.py \
    --group zulrah \
    --modern-cache "$CACHE" \
    --manifest "$PROJECT_ROOT/ocean/osrs/tools/monsters_manifest.json" \
    --output-dir "$DATA_DIR"

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
    --models 26393,35982 \
    --spotanims "$DATA_DIR/spotanims.bin" \
    --spotanim-ids 15,24,27,85,231,368,369,377,659,660,665,1040,1042,1043,1101,1103,1116,1120,1122,1123,1374,1383,1468,1619,1620,3364,3365,3366,3367,3368 \
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

# PvP LMS arena lives in regions (47, 55) and (48, 55), covering world tiles
# (3008, 3520) to (3135, 3583). Fight area is (3041, 3530) + 61x28, so two
# regions give comfortable margin around the arena. wilderness.* files below
# are scoped to that slice — small enough to commit-and-ship like inferno/zulrah.
PVP_REGIONS="47,55 48,55"

echo ""
echo "=== exporting wilderness (LMS arena) collision map ==="
if [ -f "$KEYS" ]; then
    python export_collision_map_modern.py \
        --cache "$CACHE" --keys "$KEYS" \
        --output "$DATA_DIR/wilderness.cmap" \
        --regions $PVP_REGIONS
else
    python export_collision_map_modern.py \
        --cache "$CACHE" \
        --output "$DATA_DIR/wilderness.cmap" \
        --regions $PVP_REGIONS
fi

echo ""
echo "=== exporting wilderness (LMS arena) terrain ==="
python export_terrain.py \
    --modern-cache "$CACHE" \
    --output "$DATA_DIR/wilderness.terrain" \
    --regions "$PVP_REGIONS"

echo ""
echo "=== exporting wilderness (LMS arena) objects ==="
if [ -f "$KEYS" ]; then
    python export_objects.py \
        --modern-cache "$CACHE" --keys "$KEYS" \
        --output "$DATA_DIR/wilderness.objects" \
        --regions "$PVP_REGIONS"
else
    python export_objects.py \
        --modern-cache "$CACHE" \
        --output "$DATA_DIR/wilderness.objects" \
        --regions "$PVP_REGIONS"
fi

echo ""
echo "done. all assets exported to $DATA_DIR/"
echo ""
echo "notes:"
echo "  - item sprites (inventory icons) require the Java exporter:"
echo "    javac -cp <runelite-cache-jar> scripts/ExportItemSprites.java"
echo "    java -cp .:scripts:<runelite-cache-jar> ExportItemSprites <cache_dir> ocean/osrs/data/sprites/items/"
