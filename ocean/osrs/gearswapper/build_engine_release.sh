#!/usr/bin/env bash
# Builds the engine the Java plugin launches and lays it out the way EngineBridge expects (see ENGINE_WIRE.md, "Release build").
#   ocean/osrs/gearswapper/build_engine_release.sh [RELEASE_DIR]        default RELEASE_DIR: ~/.light2/engine-release
# Needs ocean/osrs/data/wilderness.cmap (override with OSRS_DATA=<dir>). Run from anywhere inside the checkout.
set -euo pipefail
cd "$(git rev-parse --show-toplevel)"
RELEASE=${1:-$HOME/.light2/engine-release}
DATA=${OSRS_DATA:-ocean/osrs/data}
[ -f "$DATA/wilderness.cmap" ] || { echo "missing $DATA/wilderness.cmap" >&2; exit 1; }
[ -z "$(git status --porcelain --untracked-files=no)" ] || echo "warning: building from a dirty tree" >&2

mkdir -p "$RELEASE/home/ocean/osrs/data"
# Native arm64 (Mac) or whatever the host is; -O2, no sanitizers, no -DNDEBUG: a violated protocol invariant must stay a loud abort (exit 2).
cc -std=c11 -O2 -I. -o "$RELEASE/gs_lookahead_server.new" ocean/osrs/tests/gs_lookahead_server.c -lm -lpthread
mv "$RELEASE/gs_lookahead_server.new" "$RELEASE/gs_lookahead_server"
cp ocean/osrs/tests/fixtures/model.fit ocean/osrs/tests/fixtures/eat_mixture.fit "$RELEASE/"
cp "$DATA/wilderness.cmap" "$RELEASE/home/ocean/osrs/data/wilderness.cmap"
echo "$(git rev-parse HEAD) $(date -u +%FT%TZ) $(cc --version | head -1) $(uname -sm)" > "$RELEASE/BUILD"

# Smoke: the server must self-warm and print READY inside the plugin's 120 s start-up window (stdin at EOF ends it after READY).
ready=$(cd "$RELEASE/home" && timeout 120 "$RELEASE/gs_lookahead_server" "$RELEASE/model.fit" "$RELEASE/eat_mixture.fit" </dev/null 2>/dev/null)
case $ready in READY*) echo "${ready:0:100}" ;; *) echo "server did not print READY" >&2; exit 1 ;; esac
echo "release: $RELEASE"
