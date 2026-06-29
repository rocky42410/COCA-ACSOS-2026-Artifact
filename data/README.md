# Data slot

This directory is the input slot for the **runnable detector** (`repro.sh`). By
default `repro.sh` generates a reproducible synthetic set here via
`scripts/make_sample_data.py`; you can instead point `DATA_DIR` at your own data.

The paper's datasets are already included elsewhere and do not go in this slot:
- startup-variance data -> [`data/variance/`](variance/) (see its README)
- detector-validation data + models -> [`../detector_validation/`](../detector_validation/)

Slot file schema (for `repro.sh` / your own runs):

| File         | Required | Description                                                        |
|--------------|----------|--------------------------------------------------------------------|
| `train.csv`  | yes      | Normal-only training data. Header row; first column is a timestamp. |
| `test.csv`   | yes      | Evaluation data, same schema as `train.csv`.                       |
| `labels.txt` | optional | One `0`/`1` label **per test window** (0 = normal, 1 = anomaly).   |

Notes:
- Feature columns are floats; empty cells are read as `NaN` and skipped in the
  statistics and loss computations (see `src/io/csv_reader.hpp`).
- The number of windows is `floor((rows - window) / stride) + 1`. `labels.txt`
  must have exactly that many lines for the run you execute (same `--window` and
  `--stride`).
- Use `--no-header` / `--no-timestamp` flags on the binaries if your CSV omits
  those; `repro.sh` assumes both are present.
