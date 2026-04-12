#!/usr/bin/env bash
# export all visual assets from an OSRS modern cache (OpenRS2 flat file format).
#
# usage:
#   cd ocean/osrs
#   ./scripts/export_all.sh <cache_dir> [keys.json]
#
# the cache can be downloaded from https://archive.openrs2.org/ — pick any
# recent OSRS revision, download the "flat file" export. the directory should
# contain numbered subdirectories (0/, 1/, 2/, 7/, 255/) and a keys.json.
#
# XTEA keys (keys.json) are needed for terrain/objects in encrypted regions.
# if not provided, the script looks for keys.json inside the cache dir.
#
# idempotent: skips any asset that already exists. delete a file to re-export it.
# all output goes to data/.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
TOOLS_DIR="$SCRIPT_DIR/../tools"
DATA_DIR="$SCRIPT_DIR/../data"
mkdir -p "$DATA_DIR" "$DATA_DIR/sprites/gui" "$DATA_DIR/sprites/items"

if [ $# -lt 1 ]; then
    echo "usage: $0 <path-to-modern-osrs-cache> [keys.json]"
    echo ""
    echo "download a cache from https://archive.openrs2.org/"
    echo "pick any recent OSRS revision, use the 'flat file' export."
    exit 1
fi

CACHE="$1"
KEYS="${2:-$CACHE/keys.json}"

if [ ! -d "$CACHE/2" ]; then
    echo "error: $CACHE doesn't look like a modern cache (missing 2/ subdir)"
    exit 1
fi
if [ ! -f "$KEYS" ]; then
    echo "warning: no keys.json — encrypted regions (terrain/objects) will fail"
    KEYS=""
fi

KEYS_ARG=""
[ -n "$KEYS" ] && KEYS_ARG="--keys $KEYS"

skip_if_exists() {
    if [ -f "$1" ]; then
        echo "  skip: $1 (exists)"
        return 0
    fi
    return 1
}

# ============================================================================
# shared assets (all encounters)
# ============================================================================

echo "=== equipment models (player body + worn gear) ==="
if ! skip_if_exists "$DATA_DIR/equipment.models"; then
    python "$SCRIPT_DIR/export_models.py" \
        --modern-cache "$CACHE" \
        --output "$DATA_DIR/equipment.models" \
        --extra-models 14407,14408,14409,10415,20390,11221,26593,4086
fi

echo "=== equipment animations ==="
if ! skip_if_exists "$DATA_DIR/equipment.anims"; then
    python "$SCRIPT_DIR/export_animations.py" \
        --modern-cache "$CACHE" \
        --output "$DATA_DIR/equipment.anims"
fi

echo "=== GUI sprites (prayer icons, hitsplats, UI chrome) ==="
# sprites are many small files; check for a sentinel
if ! skip_if_exists "$DATA_DIR/sprites/gui/pray_melee.png"; then
    python "$SCRIPT_DIR/export_sprites_modern.py" \
        --cache "$CACHE" \
        --output "$DATA_DIR/sprites/gui"
fi

# ============================================================================
# zulrah
# ============================================================================

echo "=== zulrah NPC models + animations ==="
if ! skip_if_exists "$DATA_DIR/zulrah.models"; then
    python "$TOOLS_DIR/export_encounter_npcs.py" \
        --group zulrah \
        --modern-cache "$CACHE" \
        --output-dir "$DATA_DIR"
fi

echo "=== zulrah collision map ==="
if ! skip_if_exists "$DATA_DIR/zulrah.cmap"; then
    python "$SCRIPT_DIR/export_collision_map_modern.py" \
        --cache "$CACHE" $KEYS_ARG \
        --output "$DATA_DIR/zulrah.cmap" \
        --regions 35,47 35,48
fi

echo "=== zulrah terrain ==="
if ! skip_if_exists "$DATA_DIR/zulrah.terrain"; then
    python "$SCRIPT_DIR/export_terrain.py" \
        --modern-cache "$CACHE" \
        --output "$DATA_DIR/zulrah.terrain" \
        --regions 35,47 35,48
fi

echo "=== zulrah objects ==="
if ! skip_if_exists "$DATA_DIR/zulrah.objects"; then
    python "$SCRIPT_DIR/export_objects.py" \
        --modern-cache "$CACHE" $KEYS_ARG \
        --output "$DATA_DIR/zulrah.objects" \
        --regions 35,47 35,48
fi

# ============================================================================
# inferno
# ============================================================================

echo "=== inferno NPC models + animations ==="
if ! skip_if_exists "$DATA_DIR/inferno.models"; then
    python "$TOOLS_DIR/export_encounter_npcs.py" \
        --group inferno \
        --modern-cache "$CACHE" \
        --output-dir "$DATA_DIR"
fi

echo "=== inferno collision map ==="
if ! skip_if_exists "$DATA_DIR/inferno.cmap"; then
    python "$SCRIPT_DIR/export_collision_map_modern.py" \
        --cache "$CACHE" $KEYS_ARG \
        --output "$DATA_DIR/inferno.cmap" \
        --regions 35,83
fi

echo "=== inferno terrain ==="
if ! skip_if_exists "$DATA_DIR/inferno.terrain"; then
    python "$SCRIPT_DIR/export_terrain.py" \
        --modern-cache "$CACHE" \
        --output "$DATA_DIR/inferno.terrain" \
        --regions 35,83
fi

echo "=== inferno objects (full arena) ==="
if ! skip_if_exists "$DATA_DIR/inferno.objects"; then
    python "$SCRIPT_DIR/export_objects.py" \
        --modern-cache "$CACHE" $KEYS_ARG \
        --output "$DATA_DIR/inferno.objects" \
        --regions 35,83
fi

echo "=== inferno objects (zuk arena only, pillars removed) ==="
if ! skip_if_exists "$DATA_DIR/inferno_zuk.objects"; then
    python "$SCRIPT_DIR/export_objects.py" \
        --modern-cache "$CACHE" $KEYS_ARG \
        --output "$DATA_DIR/inferno_zuk.objects" \
        --regions 35,83 \
        --exclude-ids "30327,30328,30329,30330,30331,30332,30333,30334,30335,30336,30337,30338,30356"
fi

# ============================================================================
# PvP (wilderness)
# ============================================================================

echo "=== wilderness collision map ==="
if ! skip_if_exists "$DATA_DIR/wilderness.cmap"; then
    python "$SCRIPT_DIR/export_collision_map_modern.py" \
        --cache "$CACHE" $KEYS_ARG \
        --output "$DATA_DIR/wilderness.cmap" \
        --wilderness
fi

echo "=== wilderness terrain ==="
if ! skip_if_exists "$DATA_DIR/wilderness.terrain"; then
    python "$SCRIPT_DIR/export_terrain.py" \
        --modern-cache "$CACHE" \
        --output "$DATA_DIR/wilderness.terrain" \
        --wilderness
fi

# wilderness.objects is 685MB+ — skip by default
echo "=== wilderness objects (skipped, 685MB+) ==="
echo "  run manually: python scripts/export_objects.py --modern-cache \$CACHE --keys \$KEYS --output data/wilderness.objects --wilderness"

# ============================================================================
# item sprites (inventory icons) — uses Java + runelite-cache
# ============================================================================

# default item IDs: the loadout items used by inferno + zulrah + pvp.
# add more here as needed. comma-separated.
ITEM_IDS="11230,22461,22464,22467,22470,12625,12627,12629,12631"
ITEM_IDS+=",4151,22325,26374,12926,27277,28254"  # weapons (whip/scythe/bp/tbow/scb)
ITEM_IDS+=",10828,21018,13239,27235,27238,27229"  # gear (helm/body/legs/torva etc.)
ITEM_IDS+=",6685,6687,6689,6691,3024,3026,3028,3030"  # brew + restore
ITEM_IDS+=",385,3144,2434,139,141,143"  # food + prayer pot

echo "=== item inventory sprites (needs Java + runelite-cache, auto-fetched) ==="
if ! command -v javac >/dev/null 2>&1; then
    echo "  skip: javac not found. install openjdk-11+ to export item sprites."
else
    if ! skip_if_exists "$DATA_DIR/sprites/items/11230.png"; then
        "$SCRIPT_DIR/export_items.sh" "$CACHE" "$ITEM_IDS"
    fi
fi

# ============================================================================
# done
# ============================================================================

echo ""
echo "done. assets exported to $DATA_DIR/"
