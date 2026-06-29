#!/usr/bin/env bash
# One-command reproduction for the COCA artifact.
#
#   ./repro.sh                # builds, generates sample data, trains, tests
#   DATA_DIR=/path ./repro.sh # uses your own data instead of the sample set
#
# DATA_DIR must contain:
#   train.csv   normal-only training data (header row, first col = timestamp)
#   test.csv    evaluation data (same schema)
#   labels.txt  OPTIONAL: one 0/1 label per TEST WINDOW (enables TPR/FPR metrics)
#
# Window/stride must match between training and testing; override with
# WINDOW= and STRIDE= (defaults 10 / 5).
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$HERE"

WINDOW="${WINDOW:-10}"
STRIDE="${STRIDE:-5}"
OUT="${OUT:-results_repro}"
mkdir -p "$OUT"

# 1. Build
echo "==> Building"
./build.sh

# 2. Data: use provided DATA_DIR, otherwise generate a reproducible sample set
if [[ -n "${DATA_DIR:-}" ]]; then
  echo "==> Using DATA_DIR=$DATA_DIR"
else
  echo "==> No DATA_DIR set; generating sample data in ./data"
  python3 scripts/make_sample_data.py --outdir data --window "$WINDOW" --stride "$STRIDE"
  DATA_DIR="data"
fi

TRAIN="$DATA_DIR/train.csv"
TEST="$DATA_DIR/test.csv"
LABELS="$DATA_DIR/labels.txt"

# 3. Train
echo "==> Training"
./build/coca_train --data "$TRAIN" --config coca_config.yaml \
    --window "$WINDOW" --stride "$STRIDE" --output "$OUT/model.coca"

# 4. Test (with labels if available)
echo "==> Testing"
if [[ -f "$LABELS" ]]; then
  ./build/coca_test --model "$OUT/model.coca" --test "$TEST" --labels "$LABELS" \
      --window "$WINDOW" --stride "$STRIDE" --verbose | tee "$OUT/test_report.txt"
else
  ./build/coca_test --model "$OUT/model.coca" --test "$TEST" \
      --window "$WINDOW" --stride "$STRIDE" --verbose | tee "$OUT/test_report.txt"
fi

echo "==> Done. Model: $OUT/model.coca  Report: $OUT/test_report.txt"
