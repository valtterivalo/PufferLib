#!/usr/bin/env bash
# Builds and runs every ocean/osrs/tests/test_*.c (cc -std=gnu11 -O2 -I. -Isrc ... -lm -lpthread, cwd = repo root; gnu11 so glibc exposes clock_gettime).
# RAYLIB_INC=<dir> adds the raylib headers (the model tests need them).
#   ocean/osrs/tests/run_suite.sh [-j JOBS] [-t TIMEOUT_SECONDS] [test_name ...]
# One line per test on stdout: PASS|FAIL|BUILD name seconds; logs under $OUT (default /tmp/osrs-suite). Exit 1 if anything failed.
set -u
JOBS=4
TIMEOUT=900
while getopts "j:t:" opt; do
  case $opt in
    j) JOBS=$OPTARG ;;
    t) TIMEOUT=$OPTARG ;;
    *) exit 2 ;;
  esac
done
shift $((OPTIND - 1))
cd "$(git rev-parse --show-toplevel 2>/dev/null || pwd)"
export OUT="${OUT:-/tmp/osrs-suite}"
export TIMEOUT
mkdir -p "$OUT"
rm -f "$OUT"/*.status

if [ $# -gt 0 ]; then
  names=("$@")
else
  names=()
  for f in ocean/osrs/tests/test_*.c; do
    n=$(basename "$f" .c)
    names+=("$n")
  done
fi

# Per-test needs: oracle fixtures as argv, -D encounter selectors (one build per variant), raylib headers for the model tests.
CFLAGS="-std=gnu11 -O2 -I. -Isrc"
[ -n "${RAYLIB_INC:-}" ] && CFLAGS="$CFLAGS -I$RAYLIB_INC"
export CFLAGS

args_for() {
  case $1 in
    test_gs_auto_eat) echo ocean/osrs/tests/fixtures/auto_eat_oracle.tap ;;
    test_gs_auto_spec) echo ocean/osrs/tests/fixtures/auto_spec_oracle.tap ;;
    test_gs_infer) echo ocean/osrs/tests/fixtures/infer_oracle.wire ;;
    test_gs_fake_xp_wire) echo ocean/osrs/tests/fixtures/fake_xp_oracle.wire ;;
  esac
}
export -f args_for

run_variant() { # label source defines...
  label=$1; src=$2; shift 2
  start=$(date +%s)
  if ! cc $CFLAGS "$@" -o "$OUT/$label" "ocean/osrs/tests/$src.c" -lm -lpthread >"$OUT/$label.build.log" 2>&1; then
    echo "BUILD $label $(( $(date +%s) - start ))" | tee "$OUT/$label.status"
    return
  fi
  timeout "$TIMEOUT" "$OUT/$label" $(args_for "$src") >"$OUT/$label.log" 2>&1
  rc=$?
  if [ $rc -eq 0 ]; then
    echo "PASS $label $(( $(date +%s) - start ))" | tee "$OUT/$label.status"
  else
    echo "FAIL $label $(( $(date +%s) - start )) rc=$rc" | tee "$OUT/$label.status"
  fi
}
export -f run_variant

run_one() {
  n=$1
  if [ "$n" = test_encounter_route_topology_setup ]; then
    for t in INFERNO ZULRAH COLOSSEUM NH_PVP; do run_variant "${n}_$t" "$n" "-DTEST_ROUTE_TOPOLOGY_$t"; done
  else
    run_variant "$n" "$n"
  fi
}
export -f run_one

printf '%s\n' "${names[@]}" | xargs -P "$JOBS" -I{} bash -c 'run_one {}'
cat "$OUT"/*.status | awk '{c[$1]++} END {for (k in c) printf "%s=%d ", k, c[k]; print ""}'
! grep -qE '^(FAIL|BUILD)' "$OUT"/*.status
