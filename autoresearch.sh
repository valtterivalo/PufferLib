#!/usr/bin/env bash
# Deterministic Inferno + Colosseum env harness:
# golden/sim fidelity gates, then CPU step throughput.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")" && pwd)"
cd "$ROOT"

export OMP_NUM_THREADS=1
export MKL_NUM_THREADS=1
export VECLIB_MAXIMUM_THREADS=1
export PYTHONHASHSEED=0

CC="${CC:-cc}"
CFLAGS_STD="-std=c11"
CFLAGS_OPT="-O2"
CFLAGS_INC="-I."
OUT="$ROOT/build/autoresearch"
mkdir -p "$OUT"

compile() {
    local dst="$1"
    shift
    "$CC" "$CFLAGS_STD" "$CFLAGS_OPT" "$CFLAGS_INC" -o "$dst" "$@" -lm
}
compile "$OUT/inf_golden" ocean/osrs/tests/test_inferno_golden.c
compile "$OUT/colo_golden" ocean/osrs/tests/test_colosseum_golden.c
compile "$OUT/colo_sim" ocean/osrs/tests/test_colosseum_sim_invariant.c
compile "$OUT/bench_inferno" -DBENCH_INFERNO ocean/osrs/tests/bench_osrs_arena.c
compile "$OUT/bench_colosseum" -DBENCH_COLOSSEUM ocean/osrs/tests/bench_osrs_arena.c

"$OUT/inf_golden" >/dev/null
"$OUT/colo_golden" >/dev/null
"$OUT/colo_sim" >/dev/null

parse_sps() {
    awk '/^SPS / { print $2; found=1 } END { if (!found) exit 1 }'
}

inf_sps="$("$OUT/bench_inferno" | parse_sps)"
colo_sps="$("$OUT/bench_colosseum" | parse_sps)"

python3 - "$inf_sps" "$colo_sps" <<'PY'
import sys
inf = float(sys.argv[1])
colo = float(sys.argv[2])
geomean = (inf * colo) ** 0.5
print(f"METRIC geomean_sps={geomean:.3f}")
print(f"METRIC inferno_sps={inf:.3f}")
print(f"METRIC colosseum_sps={colo:.3f}")
PY

# Encounter sim + env wrappers + shared combat/path used by both arenas.
loc=$(
    cat \
        ocean/osrs/encounters/encounter_inferno.h \
        ocean/osrs/encounters/encounter_colosseum.h \
        ocean/osrs/encounters/inferno/*.inc \
        ocean/osrs/encounters/colosseum/*.inc \
        ocean/osrs_inferno/*.[ch] \
        ocean/osrs_colosseum/*.[ch] \
        ocean/osrs/osrs_combat.h \
        ocean/osrs/osrs_collision.h \
        ocean/osrs/osrs_pathfinding.h \
        ocean/osrs/osrs_encounter.h \
        ocean/osrs/osrs_encounter_player.h \
        | wc -l | tr -d ' '
)
echo "METRIC loc=${loc}"
echo "METRIC golden_pass=1"
