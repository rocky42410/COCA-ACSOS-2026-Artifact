#!/usr/bin/env python3
"""Reproduce the within-unit SD (Tables II/III) and cross-unit offset (Table VI)
from the 4-joint startup-variance data in ../data/variance.

Filename convention (raw data) vs paper terminology:
  *_reset_* files == paper IDLE mode (reset procedure applied)
  *_idle_*  files == paper NO-RESET (baseline) mode
Robots:  mic == Robot B,  nob == Robot A.
"""
import csv, glob, os, statistics as st

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.join(HERE, "..", "data", "variance")
ROBOT = {"nob": "A", "mic": "B"}     # filename prefix -> paper robot label

def trial_means(path):
    cols = [[], [], [], []]
    data = open(path, errors="ignore").read().replace("\x00", "")
    rows = list(csv.reader(data.splitlines()))
    for row in rows[1:]:
        if len(row) < 5:
            continue
        for j in range(4):
            try: cols[j].append(float(row[j + 1]))
            except ValueError: pass
    return [st.mean(c) if c else float("nan") for c in cols]

def group(robot, filemode):
    files = sorted(glob.glob(os.path.join(ROOT, robot, f"{robot}_{filemode}_*.csv")))
    return [trial_means(f) for f in files]

print("Within-unit SD of per-trial mean torque (Tables II/III), per joint i=1..4")
print(f"{'robot':6s} {'joint':5s} {'SD no-reset':>12s} {'SD reset(idle)':>15s} {'%reduction':>11s}")
for r in ("nob", "mic"):
    base = group(r, "idle")    # paper NO-RESET
    rst  = group(r, "reset")   # paper IDLE
    for j in range(4):
        sb = st.stdev([t[j] for t in base]); sr = st.stdev([t[j] for t in rst])
        red = (sb - sr) / sb * 100 if sb else float("nan")
        print(f"{('Robot '+ROBOT[r]):6s} {j+1:<5d} {sb:12.3f} {sr:15.3f} {red:10.0f}%")

print("\nCross-unit offset under reset/idle mode (Table VI)")
print(f"{'joint':5s} {'mean Robot A':>13s} {'mean Robot B':>13s} {'delta':>8s}")
A = {j: st.mean([t[j] for t in group('nob', 'reset')]) for j in range(4)}
B = {j: st.mean([t[j] for t in group('mic', 'reset')]) for j in range(4)}
for j in range(4):
    print(f"{j+1:<5d} {A[j]:13.3f} {B[j]:13.3f} {abs(A[j]-B[j]):8.2f}")
