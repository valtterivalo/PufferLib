#!/usr/bin/env bash
# Export item inventory sprites from an OSRS modern cache.
#
# Uses RuneLite's ItemSpriteFactory (Java) to render 3D item models to 2D
# inventory icons (36x32 PNG), matching the real OSRS client exactly.
#
# Usage:
#   cd ocean/osrs
#   ./scripts/export_items.sh <cache_dir> <item_id>[,<item_id>...]
#
# Example:
#   ./scripts/export_items.sh ~/osrs-cache 11230,22461,22464,22467,22470
#
# Dependencies are auto-fetched from Maven Central on first run. Requires
# Java 11+ and curl. Output goes to data/sprites/items/<item_id>.png.

set -eo pipefail

if [ $# -lt 2 ]; then
    echo "usage: $0 <cache_dir> <item_id>[,<item_id>...]"
    echo ""
    echo "cache_dir: OpenRS2 flat-file cache (download from https://archive.openrs2.org/)"
    echo "item_ids:  comma-separated OSRS item IDs to export"
    echo ""
    echo "example:"
    echo "  $0 ~/osrs-cache 11230,22461,22464,22467,22470,12625,12627,12629,12631"
    exit 1
fi

CACHE_DIR="$1"
ITEM_IDS="$2"

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
OSRS_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
BUILD_DIR="$OSRS_DIR/build/item_exporter"
DEPS_DIR="$BUILD_DIR/deps"
OUTPUT_DIR="$OSRS_DIR/data/sprites/items"

mkdir -p "$BUILD_DIR" "$DEPS_DIR" "$OUTPUT_DIR"

# Jar name | download URL (RuneLite cache jar from RuneLite repo, rest from Maven Central)
JARS=(
    "cache-1.11.9.jar|https://repo.runelite.net/net/runelite/cache/1.11.9/cache-1.11.9.jar"
    "commons-compress-1.21.jar|https://repo1.maven.org/maven2/org/apache/commons/commons-compress/1.21/commons-compress-1.21.jar"
    "commons-cli-1.4.jar|https://repo1.maven.org/maven2/commons-cli/commons-cli/1.4/commons-cli-1.4.jar"
    "guava-30.1.1-jre.jar|https://repo1.maven.org/maven2/com/google/guava/guava/30.1.1-jre/guava-30.1.1-jre.jar"
    "gson-2.10.1.jar|https://repo1.maven.org/maven2/com/google/code/gson/gson/2.10.1/gson-2.10.1.jar"
    "slf4j-api-1.7.36.jar|https://repo1.maven.org/maven2/org/slf4j/slf4j-api/1.7.36/slf4j-api-1.7.36.jar"
    "slf4j-simple-1.7.36.jar|https://repo1.maven.org/maven2/org/slf4j/slf4j-simple/1.7.36/slf4j-simple-1.7.36.jar"
    "failureaccess-1.0.1.jar|https://repo1.maven.org/maven2/com/google/guava/failureaccess/1.0.1/failureaccess-1.0.1.jar"
    "jna-5.9.0.jar|https://repo1.maven.org/maven2/net/java/dev/jna/jna/5.9.0/jna-5.9.0.jar"
)

for entry in "${JARS[@]}"; do
    jar="${entry%%|*}"
    url="${entry#*|}"
    if [ ! -f "$DEPS_DIR/$jar" ]; then
        echo "downloading $jar..."
        curl -sL -o "$DEPS_DIR/$jar" "$url"
    fi
done

# Build classpath
CP="$BUILD_DIR"
for jar in "$DEPS_DIR"/*.jar; do
    CP="$CP:$jar"
done

# Compile if needed
if [ ! -f "$BUILD_DIR/ExportItemSprites.class" ] || \
   [ "$SCRIPT_DIR/ExportItemSprites.java" -nt "$BUILD_DIR/ExportItemSprites.class" ]; then
    echo "compiling ExportItemSprites.java..."
    javac -cp "$CP" -d "$BUILD_DIR" "$SCRIPT_DIR/ExportItemSprites.java"
fi

echo "exporting item sprites for IDs: $ITEM_IDS"
java -cp "$CP" ExportItemSprites \
    --cache "$CACHE_DIR" \
    --output "$OUTPUT_DIR" \
    --ids "$ITEM_IDS"

echo ""
echo "done. sprites in $OUTPUT_DIR/"
