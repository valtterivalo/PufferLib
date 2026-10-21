#!/bin/zsh
# Smoke test, then every implemented boss against its recorded laws. Exit nonzero on any failure.
set -uo pipefail
cd "$(dirname "$0")/../../../../.."
out=$(mktemp -d)
echo '#include "ocean/osrs_tfa/osrs_tfa.h"' > $out/tfa_cxx.cpp
echo "== C++ syntax of the TFA headers (the CUDA trainer compiles them with nvcc)"
nice -n 19 clang++ -std=c++17 -fsyntax-only -w -Werror=c++11-narrowing -Werror=address-of-temporary -D_POSIX_C_SOURCE=200809L -I. -I./raylib-5.5_macos/include -I./src -I./vendor $out/tfa_cxx.cpp || exit 1
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
nice -n 19 cc -std=c11 -O2 -Wno-unused-function -I. -o $out/replay_ocellus ocean/osrs/tests/tfa_replay_ocellus.c -lm || exit 1
echo "== ocellus forced-damage replay"
nice -n 19 $out/replay_ocellus ocean/osrs/encounters/tfa/fixtures/tfa_ocellus_events.tsv | tail -3 || exit 1
nice -n 19 cc -std=c11 -O2 -Wno-unused-function -I. -o $out/ocellus_p4 ocean/osrs/tests/test_tfa_ocellus_p4.c -lm || exit 1
echo "== ocellus phase 4 gate"
nice -n 19 $out/ocellus_p4 | tail -1 || exit 1
nice -n 19 cc -std=c11 -O2 -Wno-unused-function -I. -I./raylib-5.5_macos/include -I./src -I./vendor -o $out/wrapper ocean/osrs/tests/test_tfa_wrapper.c -lm || exit 1
nice -n 19 $out/wrapper || exit 1
nice -n 19 cc -std=c11 -O2 -Wno-unused-function -I. -o $out/assets ocean/osrs/tests/test_tfa_assets.c -lm || exit 1
echo "== viewer assets"
nice -n 19 $out/assets 2>/dev/null | tail -1 || exit 1
nice -n 19 cc -std=c11 -O2 -Wno-unused-function -I. -o $out/human ocean/osrs/tests/test_tfa_human.c -lm || exit 1
echo "== human walk and attack clicks"
nice -n 19 $out/human | tail -1 || exit 1
nice -n 19 cc -std=c11 -O2 -Wno-unused-function -I. -o $out/byte_codes ocean/osrs/tests/test_stat_item_byte_codes.c -lm || exit 1
echo "== stat slot one-byte codes"
nice -n 19 $out/byte_codes | tail -1 || exit 1
fail=0
for spec in "guardian_slam 0 1" "guardian 0 2" "guardian 0 3" "xarth 5 3" "bork 1 4 2+" "moia 4 3" "dreadnoth 3 3" "ezekiel 2 3" "ocellus 7 3 3+" "ocellus_p4 7 3 3+ invulnerable"; do
    set -- ${=spec}
    nice -n 19 $out/dump $2 $3 60 1 ${5:-} > $out/$1_$3.tsv 2>/dev/null || exit 1
    echo "== $1 team $3"
    nice -n 19 python3 ocean/osrs/encounters/tfa/tools/tfa_compare.py $1 $out/$1_$3.tsv ${4:-$3} | tail -1
    [ ${pipestatus[1]} -eq 0 ] || fail=1
done
rm -rf $out
exit $fail
