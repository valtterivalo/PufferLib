#!/bin/zsh
# Smoke test, then every implemented boss against its recorded laws. Exit nonzero on any failure.
set -uo pipefail
cd "$(dirname "$0")/../../../../.."
out=$(mktemp -d)
nice -n 19 cc -std=c11 -O2 -Wno-unused-function -I. -o $out/smoke ocean/osrs/tests/test_tfa_smoke.c -lm || exit 1
nice -n 19 cc -std=c11 -O2 -Wno-unused-function -I. -o $out/dump ocean/osrs/tests/tfa_dump_events.c -lm || exit 1
nice -n 19 $out/smoke | tail -1 || exit 1
fail=0
for spec in "guardian 0 2" "guardian 0 3" "xarth 5 3"; do
    set -- ${=spec}
    nice -n 19 $out/dump $2 $3 30 1 > $out/$1_$3.tsv 2>/dev/null || exit 1
    echo "== $1 team $3"
    nice -n 19 python3 ocean/osrs/encounters/tfa/tools/tfa_compare.py $1 $out/$1_$3.tsv $3 | tail -1
    [ ${pipestatus[1]} -eq 0 ] || fail=1
done
rm -rf $out
exit $fail
