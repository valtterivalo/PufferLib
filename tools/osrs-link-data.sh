#!/usr/bin/env bash
#
# Link this git worktree's ocean/osrs/data to the PRIMARY checkout's data dir,
# so multiple local worktrees share one populated data dir instead of each
# downloading the asset archive. ocean/osrs/data is gitignored; on a fresh
# machine (e.g. pufferbox) build.sh/setup-data.sh downloads the manifest archive
# instead. Run once after `git worktree add`.
#
# Commits no machine-specific path: the primary checkout is derived from the
# shared git common dir, so this script is portable across machines.
set -euo pipefail

toplevel="$(git rev-parse --show-toplevel)"
primary="$(cd "$(git rev-parse --git-common-dir)/.." && pwd)"

if [ "${primary}" = "${toplevel}" ]; then
    echo "osrs-link-data: this is the primary checkout, not a linked worktree; nothing to do." >&2
    exit 0
fi

target="${primary}/ocean/osrs/data"
link="${toplevel}/ocean/osrs/data"

if [ ! -d "${target}" ]; then
    echo "osrs-link-data: primary data dir missing: ${target}" >&2
    echo "osrs-link-data: populate it first (run ./build.sh osrs_<env> in the primary, or setup-data.sh)." >&2
    exit 1
fi

ln -sfn "${target}" "${link}"
echo "osrs-link-data: linked ${link} -> ${target}"
