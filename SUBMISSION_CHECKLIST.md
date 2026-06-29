# ACSOS 2026 Artifact - Submission Checklist

Submission type: **Artifact from an accepted main-track paper** (badge attaches
to the accepted paper). Submit via EasyChair (conf = acsos2026). Review is
single-blind - **no anonymization needed**.

## Done (in this package)

- [x] Self-contained detector source: `coca_train`, `coca_test` + full dependency
      closure (`coca_model.hpp`, `csv_reader.hpp`, `config_parser.hpp`, `model_io.hpp`)
      and `tests/test_losses.cpp`. Debug/build cruft removed.
- [x] **LICENSE** (MIT) and **README.md** (Getting Started, step-by-step,
      system requirements, limitations).
- [x] **Dockerfile** (Ubuntu 24.04; `-march=native` opt-in), **repro.sh**, and
      **make_sample_data.py** so reviewers can run end-to-end in < 20 min.
- [x] `coca_train` creates the `--output` directory and exits non-zero on write
      failure (previously failed silently).
- [x] Config verified against the paper **and** the trained model file:
      `score_mix: inv_plus_rec`, `threshold_quantile: 0.99` (99th pct, paper SecV.C).
- [x] **Variance results reproduce** - `analysis/compute_variance.py` over
      `data/variance/` matches Table II (33/40/29/27%), Table III (25/59/29/30%),
      and Table VI offsets (0.30/0.40/1.08/0.34). Robot A trials 1 & 7 use the
      corrected re-takes.
- [x] **Detector validation reproduces** - `detector_validation/reproduce_tableVII.sh`
      matches Table VII within-unit rows (thr 2.86/3.01, FP 0%/100%, kick 11.10/6.51).
- [x] **Acquisition tier** under `acquisition/`: logger, `feature_config_full.csv`,
      and `reset.cpp` (hardware-dependent; documented; kept out of the reviewer path).
- [x] **Security**: excluded `.ssh`/`authorized_keys`, `.bash_history`, `.pcap`,
      and the ~600 MB vendored Unitree SDK. `.gitignore` keeps the committed
      `.coca` models while ignoring generated ones.

## Before you submit (external - not in the package)

- [ ] **Push to a public GitHub repo** under MIT; put the link in the EasyChair
      submission (current remote `rocky42410/RoCA-Model` is private/SSH).
- [ ] **Record the <= 5-min YouTube demo** and link it in the paper/submission.
- [ ] **Attach the pre-print** (`Final Submission.pdf`) to the artifact submission
      (required for an artifact-from-a-paper).
- [ ] Confirm the included data is **cleared for public release**.

## Before relying on the acquisition tier

- [x] Unitree SDK version pinned in `acquisition/README.md` (`unitree_sdk2` 2.0.0,
      CycloneDDS 0.10.2, gcc 9.4.0, Ubuntu 20.04).
- [ ] **Bench-verify `reset.cpp`** on the robot (StandDown -> 2 s -> Damp -> 1 s ->
      RecoveryStand).
- [ ] Reconcile the `Balance_Stand` vs `RecoveryStand` wording in the paper revision.

## Camera-ready (later, by 20 July)

- [ ] Deposit a **permanent archive** (Zenodo/figshare) and cite the DOI. Avoid Google Drive.
- [ ] Optional: map each repro output to its specific figure/table in the README.
