# COCA - Competitive One-Class Anomaly Detection (ACSOS 2026 Artifact)

This artifact accompanies the ACSOS 2026 paper on COCA, a one-class anomaly
detection method for time-series sensor data from autonomic/self-* systems.
COCA learns a representation of *normal* behaviour and flags windows that
deviate from it, using a combination of an **invariance** loss (consistency
between an encoding and its reconstruction's re-encoding), a **variance** loss
(prevents representation collapse), and an optional **reconstruction** loss.

The artifact provides the two core executables used in the paper  - 
`coca_train` (model training) and `coca_test` (evaluation / scoring) - together
with their full dependency set, a reproducible synthetic-data generator, unit
tests, and a one-command end-to-end reproduction.

---

## 1. Getting Started

The fastest path (Docker - no toolchain setup, identical on Mac/Windows/Linux):

```bash
docker build -t coca-artifact .
docker run --rm coca-artifact          # builds, generates sample data, trains, tests
```

This runs `repro.sh`, which builds the binaries, generates a small reproducible
synthetic dataset, trains a model, evaluates it, and prints classification
metrics (Accuracy / Precision / Recall / F1 and TPR @ fixed FPR). Expected
runtime: **under 2 minutes** on a typical laptop. This exercises the complete
train -> score -> evaluate pipeline end to end.

To reproduce with **your own data** instead of the synthetic sample, mount a
directory containing `train.csv`, `test.csv`, and (optionally) `labels.txt`:

```bash
docker run --rm -v /absolute/path/to/your/data:/work/data coca-artifact
```

### Native build (no Docker)

```bash
./build.sh --test     # configures with CMake, builds, runs unit tests
./repro.sh            # full reproduction with the synthetic sample set
```

---

## 2. System & Environment Requirements

| Resource | Requirement |
|----------|-------------|
| OS       | Linux, macOS, or Windows. Verified build on Ubuntu 24.04 (Docker image). |
| CPU      | Any x86-64 or ARM64. No GPU required. |
| RAM      | < 1 GB for the sample reproduction; scales with dataset/window size. |
| Disk     | < 200 MB including the built binaries. |
| GPU      | Not used. |
| Toolchain (native build) | C++17 compiler (GCC >= 9 or Clang >= 10), CMake >= 3.10, OpenMP (optional, auto-detected), Python >= 3.8 (only for the sample-data generator). |
| Toolchain (Docker)       | Docker only - everything else is inside the image. |

The project is **header-only** apart from OpenMP; there are no third-party C++
library dependencies.

---

## 3. Step-by-Step Instructions

### 3.1 Build

```bash
./build.sh            # Release build into ./build/
./build.sh --test     # also runs the unit-test suite (ctest)
./build.sh --native   # adds -march=native (faster, but binaries become
                      # CPU-specific - leave OFF for portable/reproducible runs)
```

Artifacts produced: `build/coca_train`, `build/coca_test`, `build/coca_unit_tests`.

### 3.2 Prepare data

Either generate a reproducible synthetic set:

```bash
python3 scripts/make_sample_data.py --outdir data --window 10 --stride 5
```

or place your own `train.csv` / `test.csv` / `labels.txt` in `data/`
(see [`data/README.md`](data/README.md) for the exact schema).

### 3.3 Train

```bash
mkdir -p results
./build/coca_train \
    --data   data/train.csv \
    --config coca_config.yaml \
    --window 10 --stride 5 \
    --output results/model.coca
```

> Note: `coca_train` creates the `--output` directory if it does not exist and
> exits with a non-zero status if the model cannot be written.

Run `./build/coca_train --help` for all options (multiple `--data` files,
`--no-header`, `--no-timestamp`, etc.).

### 3.4 Test / Score

```bash
./build/coca_test \
    --model  results/model.coca \
    --test   data/test.csv \
    --labels data/labels.txt \
    --window 10 --stride 5 \
    --verbose
```

### 3.5 Check the results

`coca_test` prints a results block and writes per-window scores to
`test_results.csv`. With `--labels`, it reports a confusion matrix plus
Accuracy, Precision, Recall, F1, and TPR at 1% / 5% / 10% FPR. On the bundled
synthetic data the injected anomaly segment is cleanly separable, so the run
should report near-perfect separation - confirming the pipeline is wired
correctly. On real data, metrics will reflect dataset difficulty.

---

## 4. Reproducing with One Command

```bash
./repro.sh                          # synthetic sample data
DATA_DIR=/path/to/data ./repro.sh   # your own data
WINDOW=20 STRIDE=10 ./repro.sh      # override windowing
```

Outputs land in `results_repro/` (`model.coca`, `test_report.txt`).

---

## 5. Repository Layout

```
.
├── apps/
│   ├── coca_train.cpp        # training entry point
│   └── coca_test.cpp         # evaluation / scoring entry point
├── src/
│   ├── coca_model.hpp        # COCA model: encoder/decoder/projector, losses, Adam, scoring
│   ├── io/csv_reader.hpp     # CSV ingestion + windowing (NaN-aware)
│   └── utils/
│       ├── config_parser.hpp # config loading
│       └── model_io.hpp      # .coca model serialization (magic 0x434F4341)
├── tests/test_losses.cpp     # unit tests: losses, center update, scoring, thresholds
├── acquisition/             # on-robot logger + reset.cpp + feature config (hardware-dependent)
├── detector_validation/     # Table VII repro: trained models + kick/nominal data
├── analysis/                # compute_variance.py -> Tables II/III, VI
├── data/variance/           # 4-joint startup-variance dataset (two robots, two modes)
├── scripts/make_sample_data.py  # reproducible CSV sample-data generator
├── data/README.md            # data slot + CSV schema
├── coca_config.yaml          # hyperparameters
├── CMakeLists.txt, build.sh  # build
├── repro.sh                  # one-command reproduction
├── Dockerfile, .dockerignore # containerised build/run
└── LICENSE                   # MIT
```

### Pipeline

```
CSV -> windowing (TxD) -> Encoder -> latent z -> Decoder -> x_hat -> re-encode z'
                                  |                                    |
                                  +------- project to q, q' -----------+
        Losses:  L = lambda_inv*L_inv + lambda_var*L_var + lambda_rec*L_rec
        Score:   per-window anomaly score vs. calibrated threshold
```

### Reproducing the paper's tables

- **Tables II/III, VI (startup variance)** - `python3 analysis/compute_variance.py`
  over `data/variance/` (4-joint torque, two robots, two modes).
- **Table VII (detector validation)** - `./detector_validation/reproduce_tableVII.sh`
  after `./build.sh`; scores the paper's trained models on held-out nominal and
  kick recordings (full 53-feature telemetry).

### Data acquisition (hardware-dependent)

`acquisition/` holds the Unitree Go2 SDK logger, the reset-procedure script
(`reset.cpp`), and the feature config. These require a physical robot + vendor
SDK (`unitree_sdk2` 2.0.0, CycloneDDS 0.10.2, gcc 9.4.0, Ubuntu 20.04) and are
**not** part of the reviewer repro path. See
[`acquisition/README.md`](acquisition/README.md).

---

## 6. Configuration

Edit `coca_config.yaml`. Key hyperparameters: `lambda_inv`, `lambda_var`,
`lambda_rec`, `zeta`, `variance_epsilon`, `score_mix`
(`inv_only` | `inv_plus_rec`), and `threshold_mode` (`quantile` | `zscore`).
Window size and stride are passed on the command line and **must match between
training and testing**.

### Extending / reusing

- Swap in a new dataset by conforming to the CSV schema in `data/README.md`  - 
  no code changes required.
- The model and I/O are header-only; `src/coca_model.hpp` exposes
  `score_window()` for embedding COCA into an online detector.
- New scoring or thresholding strategies can be added in `coca_model.hpp`
  alongside the existing `score_mix` / `threshold_mode` switches.

---

## 7. Limitations

- **Synthetic sample data is illustrative, not the paper's dataset.** It exists
  so reviewers can exercise the pipeline without the original robot recordings.
  Reproducing the paper's reported numbers requires the corresponding dataset in
  `data/` (see section 3.2).
- **Windowing must be consistent.** Mismatched `--window`/`--stride` between
  training and testing will produce meaningless scores; `labels.txt` must have
  exactly one entry per test window for the chosen settings.
- **`--native` builds are CPU-specific.** Keep it off for portable/reproducible
  artifacts; the default build is portable.
- **CPU-only.** No GPU acceleration; very large datasets are bounded by
  single-machine memory since data is loaded fully into RAM.
- Feature alignment between train/test is by position; use
  `coca_test --align-features <truncate|pad>` if dimensionalities differ.

---

## 8. License

Released under the MIT License - see [`LICENSE`](LICENSE).

## 9. Citation

If you use this artifact, please cite the corresponding ACSOS 2026 paper.
A demo video (<= 5 min) is linked from the paper / submission.
