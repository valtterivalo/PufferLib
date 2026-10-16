#!/usr/bin/env bash
# His reaction to damage, recordings against sim, before and after the eat hazard's damage features: the whole pipeline of the table.
#   ocean/osrs/tests/reaction_fit.sh OUT_DIR [TAPES_DIR SPECTATOR_DUELS]      (cwd = repo root; defaults to the c-engine export in ~/.light2/c-engine)
# Builds fit_opponent and reaction_replay, fits three eat hazards held out by recording (v2 as fitted: the rate of shown meals; v2's structure with the visibility-aware
# likelihood; the new one: plus the damage features), replays every damage window of the recordings through the sim under each, and writes OUT_DIR/reaction.md.
# The deployed v2 (before) is the committed fixture of the commit before this one: pass it as V2_MODEL=... V2_EAT=... with V2_REPLAY=<reaction_replay built from that commit>.
set -eu
OUT=${1:?out dir}
TAPES=${2:-$HOME/.light2/c-engine/tapes}
DUELS=${3:-$HOME/.light2/c-engine/spectator.duels}
REPS=${REPS:-20}
mkdir -p "$OUT"
cc -std=c11 -O2 -Iocean/osrs/tests -I. -o "$OUT/fit_opponent" ocean/osrs/tests/fit_opponent.c -lm
cc -std=c11 -O2 -Iocean/osrs/tests -I. -o "$OUT/reaction_replay" ocean/osrs/tests/reaction_replay.c -lm
V2_ENV='OPP_EAT_GROUPS=47'   # hp type our-timer our-spec low-hp*soon, the structure of v2
mkdir -p "$OUT/fit_new" "$OUT/fit_v2_shown" "$OUT/fit_v2_visible"
"$OUT/fit_opponent" "$OUT/fit_new" "$DUELS" "$TAPES"/*.tape >"$OUT/fit_new/log.txt" 2>&1 &
env $V2_ENV OPP_EAT_SHOWN_RATE=1 "$OUT/fit_opponent" "$OUT/fit_v2_shown" "$DUELS" "$TAPES"/*.tape >"$OUT/fit_v2_shown/log.txt" 2>&1 &
env $V2_ENV "$OUT/fit_opponent" "$OUT/fit_v2_visible" "$DUELS" "$TAPES"/*.tape >"$OUT/fit_v2_visible/log.txt" 2>&1 &
wait
ARGS=()
if [ -n "${V2_REPLAY:-}" ]; then
  "$V2_REPLAY" -r "$REPS" -x -m "$V2_MODEL" "$V2_EAT" -o "$OUT/rows_v2_deployed.tsv" "$DUELS" "$TAPES"/*.tape
  ARGS+=("v2 deployed=$OUT/rows_v2_deployed.tsv")
fi
for fit in v2_shown v2_visible new; do
  "$OUT/reaction_replay" -r "$REPS" -x -F "$OUT/fit_$fit" -o "$OUT/rows_$fit.tsv" "$DUELS" "$TAPES"/*.tape
  ARGS+=("$fit=$OUT/rows_$fit.tsv")
done
python3 ocean/osrs/tests/reaction_table.py "${ARGS[@]}" >"$OUT/reaction.md"
echo "$OUT/reaction.md"
