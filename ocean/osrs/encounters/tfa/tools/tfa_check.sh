#!/bin/zsh
# Smoke test, then every implemented boss against its recorded laws. Exit nonzero on any failure.
set -uo pipefail
cd "$(dirname "$0")/../../../../.."
out=$(mktemp -d)
nice -n 19 cc -std=c11 -O2 -Wno-unused-function -I. -o $out/smoke ocean/osrs/tests/test_tfa_smoke.c -lm || exit 1
nice -n 19 cc -std=c11 -O2 -Wno-unused-function -I. -o $out/dump ocean/osrs/tests/tfa_dump_events.c -lm || exit 1
nice -n 19 $out/smoke | tail -1 || exit 1
nice -n 19 cc -std=c11 -O2 -Wno-unused-function -I. -o $out/raid ocean/osrs/tests/test_tfa_raid.c -lm || exit 1
nice -n 19 $out/raid | tail -1 || exit 1
nice -n 19 cc -std=c11 -O1 -g -fsanitize=undefined -fno-sanitize-recover=undefined -Wno-unused-function -I. -o $out/fuzz ocean/osrs/tests/fuzz_tfa.c -lm || exit 1
nice -n 19 $out/fuzz | tail -1 || exit 1
nice -n 19 cc -std=c11 -O2 -Wno-unused-function -I. -o $out/replay_guardian ocean/osrs/tests/tfa_replay_guardian.c -lm || exit 1
nice -n 19 cc -std=c11 -O2 -Wno-unused-function -I. -o $out/replay_ezekiel ocean/osrs/tests/tfa_replay_ezekiel.c -lm || exit 1
echo "== ezekiel forced-damage replay"
nice -n 19 $out/replay_ezekiel ocean/osrs/encounters/tfa/fixtures/tfa_ezekiel_events.tsv ocean/osrs/encounters/tfa/fixtures/tfa_fights.tsv | tail -3 || exit 1
echo "== guardian forced-damage replay"
nice -n 19 $out/replay_guardian ocean/osrs/encounters/tfa/fixtures/tfa_guardian_events.tsv ocean/osrs/encounters/tfa/fixtures/tfa_fights.tsv | tail -3 || exit 1
nice -n 19 cc -std=c11 -O2 -Wno-unused-function -I. -o $out/replay_dreadnoth ocean/osrs/tests/tfa_replay_dreadnoth.c -lm || exit 1
echo "== dreadnoth forced-damage replay"
nice -n 19 $out/replay_dreadnoth ocean/osrs/encounters/tfa/fixtures/tfa_dreadnoth_events.tsv | tail -3 || exit 1
nice -n 19 cc -std=c11 -O2 -Wno-unused-function -I. -I./raylib-5.5_macos/include -I./src -I./vendor -o $out/wrapper ocean/osrs/tests/test_tfa_wrapper.c -lm || exit 1
nice -n 19 $out/wrapper || exit 1
fail=0
for spec in "guardian 0 2" "guardian 0 3" "xarth 5 3" "bork 1 2" "moia 4 3" "dreadnoth 3 3" "ezekiel 2 3"; do
    set -- ${=spec}
    nice -n 19 $out/dump $2 $3 60 1 > $out/$1_$3.tsv 2>/dev/null || exit 1
    echo "== $1 team $3"
    nice -n 19 python3 ocean/osrs/encounters/tfa/tools/tfa_compare.py $1 $out/$1_$3.tsv $3 | tail -1
    [ ${pipestatus[1]} -eq 0 ] || fail=1
done
rm -rf $out
exit $fail
