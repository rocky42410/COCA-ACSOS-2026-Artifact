# Detector Validation (Table VII)

Reproduces the downstream anomaly-detection result from the paper using the
**full-telemetry** detector (53 features), distinct from the 4-joint variance
data in `../data/variance`.

## Contents
- `models/nob_idle_model.coca` - detector trained on reset-controlled idle data.
- `models/nob_bad_model.coca` - detector trained on no-reset (baseline) data.
- `nob_idle_holdout.csv` - held-out nominal idle recording.
- `nob_kick.csv` - recording with kick perturbations (anomalies).
- `reproduce_tableVII.sh` - scores both models on nominal and kick data.

## Run
```bash
cd ..            # artifact root
./build.sh       # builds coca_test
./detector_validation/reproduce_tableVII.sh
```

## Expected (within-unit rows of Table VII)
| model | threshold | nominal median | nominal FP | kick median |
|-------|-----------|----------------|-----------|-------------|
| reset/idle (`nob_idle_model`) | ~2.86 | < threshold | ~0% | ~11.1 |
| no-reset (`nob_bad_model`)    | ~3.01 | ~7.15 (> threshold) | ~100% | ~6.51 |

The reset-trained detector keeps nominal scores below threshold while kicks rise
far above it; the no-reset detector fires on everything and cannot separate kicks
from nominal. The cross-unit row (Robot A model evaluated on Robot B) requires the
mic-side model, which is not part of this supplement.
