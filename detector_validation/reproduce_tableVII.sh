#!/usr/bin/env bash
# Reproduce the within-unit rows of Table VII using the paper's trained models.
# Scores each model on a held-out nominal idle recording and on the kick recording.
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/.." && pwd)"
TEST="$ROOT/build/coca_test"
[ -x "$TEST" ] || { echo "Build first:  (cd '$ROOT' && ./build.sh)"; exit 1; }
NOM="$HERE/nob_idle_holdout.csv"; KICK="$HERE/nob_kick.csv"
score(){ "$TEST" --model "$1" --test "$2" 2>/dev/null; }
printf "%-26s %8s %10s %8s %11s\n" "model" "thr" "nom_med" "nom_FP" "kick_med"
for M in nob_idle_model nob_bad_model; do
  O=$(score "$HERE/models/$M.coca" "$NOM")
  THR=$(echo "$O"|grep -m1 "Threshold:"|awk '{print $2}')
  NM=$(echo "$O"|grep -m1 "Median:"|awk '{print $2}')
  FP=$(echo "$O"|grep -m1 "Anomalies detected"|sed 's/.*(//;s/)//')
  KM=$(score "$HERE/models/$M.coca" "$KICK"|grep -m1 "Median:"|awk '{print $2}')
  printf "%-26s %8s %10s %8s %11s\n" "$M" "$THR" "$NM" "$FP" "$KM"
done
cat <<'NOTE'

Paper Table VII (within-unit rows):
  nob_idle_model (reset/idle) : thr 2.86  nom 1.79  FP 0%    kick 11.10  -> usable detector
  nob_bad_model  (no-reset)   : thr 3.01  nom 7.15  FP 100%  kick  6.51  -> fails (kick < nominal)
The reset-trained model separates kick from nominal; the no-reset model does not.
(Held-out nominal here is nob_idle_1; the paper used a specific held-out recording, so the
nominal median differs slightly. Cross-unit row needs the mic-side model, not included here.)
NOTE
