# Startup-variance dataset (4-joint hip torque)

Raw per-trial recordings behind the variance analysis (paper Tables II-VI).
~1500 samples/trial (50 Hz x 30 s), columns: `timestamp_ms, joint_val_1..4`
(four hip joints: front-left, front-right, rear-left, rear-right).

Filename vs paper terminology, and robot mapping:

| folder | paper robot | `*_reset_*` files | `*_idle_*` files |
|--------|-------------|-------------------|------------------|
| `mic/` | Robot B     | paper **idle** (reset applied) | paper **no-reset** (baseline) |
| `nob/` | Robot A     | paper **idle** (reset applied) | paper **no-reset** (baseline) |

12 trials per (robot, mode). Run `python3 ../../analysis/compute_variance.py`.

Note: for Robot A, trials `nob_reset_1` and `nob_reset_7` are the corrected
re-takes of two faulted runs (the original captures were discarded); the files
here are the versions used in the paper.

## Reproduction status

Reproduces the published tables:
- **Table II (Robot A)** SD reductions 33/40/29/27% - exact.
- **Table III (Robot B)** SD reductions 25/59/29/30% (published 25/59/29/31%).
- **Table VI** cross-unit offsets delta ~ 0.30/0.41/1.12/0.37 (published 0.30/0.40/1.08/0.34).
