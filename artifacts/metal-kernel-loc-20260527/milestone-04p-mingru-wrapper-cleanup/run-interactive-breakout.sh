#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
"$SCRIPT_DIR/../bin/run-interactive-smoke.sh" milestone-04p-mingru-wrapper-cleanup mingru-wrapper-cleanup
