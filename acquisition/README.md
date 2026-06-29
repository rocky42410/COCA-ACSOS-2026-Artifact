# Data Acquisition (hardware-dependent - not part of the reviewer repro path)

This directory contains the on-robot data logger used to record the joint-torque
telemetry analysed in the paper. **Reviewers are not expected to build or run
this** - it requires a physical Unitree Go2 and the vendor SDK. It is included
for reproducibility and reuse: it documents exactly how the dataset in `../data`
was produced and lets other labs collect comparable recordings.

## What it is

`csv_feature_logger_full.cpp` is a DDS subscriber built on the Unitree Go2 SDK.
It subscribes to the robot's `LowState`, `SportModeState`, `UwbState`, and
`HeightMap` topics, fuses them into a fixed-rate feature vector (up to 256
features, windowed statistics), and writes a timestamped CSV at a configurable
rate (50 Hz in the paper).

It is a **logger only**: it records telemetry and does not issue motion commands.
The reset procedure is implemented separately in `reset.cpp` (see below) and is
applied operationally *before* logging begins.

## reset.cpp - startup reset procedure

`reset.cpp` issues the SDK control sequence for the paper's reset procedure:
`StandDown` -> 2 s settle -> `Damp` (cut active control; gravity-settled prone)
-> 1 s -> `RecoveryStand` (idle pose). It takes the network interface as its
only argument (e.g. `./reset eth0`). Like the logger it requires `unitree_sdk2`
and a physical robot, so it is not part of the reviewer repro path.

> The paper Sec. III.D wording around the final stand command (`Balance_Stand` vs the
> SDK's `RecoveryStand`) is being reconciled in the revision. Build/verify on
> hardware before relying on it.

## feature_config_full.csv

`feature_config_full.csv` is the logger's feature-selection file
(`index,name,enabled,description`). It defines the full telemetry feature set
used for the detector-validation data in `../detector_validation`.

## Hardware & software requirements

| Requirement | Detail |
|-------------|--------|
| Robot       | Unitree Go2 Pro (paper used two units, "Robot A" / "Robot B"). |
| Transport   | CycloneDDS **0.10.2** (bundled with the SDK), robot reachable over the wired interface. |
| SDK         | `unitree_sdk2` **v2.0.0** (provides `unitree/idl/go2/*` and `unitree/robot/channel/*`). The Go2 API changes across releases, so 2.0.0 is required. |
| Toolchain   | **gcc 9.4.0**, C++17; link against `unitree_sdk2` 2.0.0 and its DDS libraries (`ddsc`/`ddscxx`). |
| OS          | **Ubuntu 20.04 LTS** (aarch64 on the robot; x86_64 also supported). |

This component **cannot be built in a generic container** because it depends on
the proprietary Unitree SDK headers/libraries. It was built against
`unitree_sdk2` 2.0.0 (CycloneDDS 0.10.2) with gcc 9.4.0 on Ubuntu 20.04.

## Build (unitree_sdk2 2.0.0 - adjust paths to your install)

```bash
g++ -std=c++17 -O2 csv_feature_logger_full.cpp -o csv_feature_logger \
    -I/path/to/unitree_sdk2/include \
    -L/path/to/unitree_sdk2/lib -lunitree_sdk2 -lddsc -lddscxx -lpthread
```

## Usage

```bash
./csv_feature_logger \
    --interface eth0 \                # network interface to the robot
    --config    feature_config_full.csv \  # which features to log + their names
    --output    nob_reset_1.csv \     # output CSV
    --rate      50 \                  # Hz (paper: 50)
    --duration  30                    # seconds (paper: 30 -> ~1500 samples)
```

The CSV column names are taken from the feature-config file (see below); the
first column is always `timestamp_ms`.

## Relationship to the dataset

Running this logger (with the four-joint feature config, `--rate 50
--duration 30`) for each trial, with the reset procedure applied beforehand for
the reset/idle-mode trials, reproduces the recordings in `../data`. See
`../data/README.md` for the robot/mode naming.
