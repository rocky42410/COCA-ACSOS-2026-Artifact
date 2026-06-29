#!/usr/bin/env python3
"""Generate schema-correct sample CSV data so reviewers can smoke-test COCA
without the real robot dataset.

Output (written into --outdir, default ./data):
  train.csv   normal-only sensor windows for training
  test.csv    normal rows with an injected anomaly segment
  labels.txt  one 0/1 label PER WINDOW (matches coca_test's window indexing)

CSV layout matches what coca_train/coca_test expect by default:
  - first row is a header
  - first column is a timestamp (skipped via --no-timestamp logic in the apps)
  - remaining columns are float features; empty cells are allowed (-> NaN)
"""
import argparse, random, math

def feature_cols(d):
    return [f"feat_{i:02d}" for i in range(d)]

def normal_row(t, d, rng):
    vals = []
    for i in range(d):
        base = math.sin(0.05 * t + i) * 0.5
        vals.append(f"{base + rng.gauss(0, 0.05):.6f}")
    return [str(t)] + vals

def anomaly_row(t, d, rng):
    vals = []
    for i in range(d):
        vals.append(f"{rng.gauss(0, 1.0) + 3.0:.6f}")  # shifted + high variance
    return [str(t)] + vals

def write_csv(path, rows, header):
    with open(path, "w") as f:
        f.write(",".join(header) + "\n")
        for r in rows:
            f.write(",".join(r) + "\n")

def n_windows(n_rows, window, stride):
    if n_rows < window:
        return 0
    return (n_rows - window) // stride + 1

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--outdir", default="data")
    ap.add_argument("--features", type=int, default=16)
    ap.add_argument("--train-rows", type=int, default=2000)
    ap.add_argument("--test-rows", type=int, default=600)
    ap.add_argument("--window", type=int, default=10)
    ap.add_argument("--stride", type=int, default=5)
    ap.add_argument("--seed", type=int, default=42)
    a = ap.parse_args()
    rng = random.Random(a.seed)
    import os
    os.makedirs(a.outdir, exist_ok=True)
    header = ["timestamp_ms"] + feature_cols(a.features)

    train = [normal_row(t, a.features, rng) for t in range(a.train_rows)]
    write_csv(os.path.join(a.outdir, "train.csv"), train, header)

    # test: normal, then an anomalous segment, then normal again
    anom_start = int(a.test_rows * 0.6)
    anom_end   = int(a.test_rows * 0.8)
    test, row_is_anom = [], []
    for t in range(a.test_rows):
        if anom_start <= t < anom_end:
            test.append(anomaly_row(t, a.features, rng)); row_is_anom.append(True)
        else:
            test.append(normal_row(t, a.features, rng)); row_is_anom.append(False)
    write_csv(os.path.join(a.outdir, "test.csv"), test, header)

    # per-window labels: window is anomalous if ANY constituent row is anomalous
    nw = n_windows(a.test_rows, a.window, a.stride)
    with open(os.path.join(a.outdir, "labels.txt"), "w") as f:
        for w in range(nw):
            s = w * a.stride
            seg = row_is_anom[s:s + a.window]
            f.write(("1" if any(seg) else "0") + "\n")

    print(f"Wrote {a.outdir}/train.csv ({a.train_rows} rows), "
          f"test.csv ({a.test_rows} rows), labels.txt ({nw} window labels)")

if __name__ == "__main__":
    main()
